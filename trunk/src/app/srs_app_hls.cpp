/*
The MIT License (MIT)

Copyright (c) 2013-2015 winlin

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include <srs_app_hls.hpp>

/**
* the HLS section, only available when HLS enabled.
*/
#ifdef SRS_AUTO_HLS

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <algorithm>
#include <sstream>
using namespace std;

#include <srs_kernel_error.hpp>
#include <srs_kernel_codec.hpp>
#include <srs_rtmp_amf0.hpp>
#include <srs_rtmp_stack.hpp>
#include <srs_app_config.hpp>
#include <srs_app_source.hpp>
#include <srs_core_autofree.hpp>
#include <srs_rtmp_sdk.hpp>
#include <srs_app_pithy_print.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_kernel_codec.hpp>
#include <srs_kernel_file.hpp>
#include <srs_rtmp_buffer.hpp>
#include <srs_kernel_ts.hpp>
#include <srs_app_utility.hpp>
#include <srs_app_http_hooks.hpp>

// drop the segment when duration of ts too small.
#define SRS_AUTO_HLS_SEGMENT_MIN_DURATION_MS 100

// startup piece, the first piece, fragment percent to reap.
#define SRS_HLS_FLOOR_STARTUP_PERCENT 0.1
// fragment plus the deviation percent.
#define SRS_HLS_FLOOR_REAP_PERCENT 0.2

ISrsHlsHandler::ISrsHlsHandler()
{
}

ISrsHlsHandler::~ISrsHlsHandler()
{
}

SrsHlsCacheWriter::SrsHlsCacheWriter(bool write_cache, bool write_file)
{
    should_write_cache = write_cache;
    should_write_file = write_file;
}

SrsHlsCacheWriter::~SrsHlsCacheWriter()
{
}

int SrsHlsCacheWriter::open(string file)
{
    if (!should_write_file) {
        return ERROR_SUCCESS;
    }

    return impl.open(file);
}

void SrsHlsCacheWriter::close()
{
    if (!should_write_file) {
        return;
    }

    impl.close();
}

bool SrsHlsCacheWriter::is_open()
{
    if (!should_write_file) {
        return true;
    }

    return impl.is_open();
}

int64_t SrsHlsCacheWriter::tellg()
{
    if (!should_write_file) {
        return 0;
    }

    return impl.tellg();
}

int SrsHlsCacheWriter::write(void* buf, size_t count, ssize_t* pnwrite)
{
    if (should_write_cache) {
        if (count > 0) {
            data.append((char*)buf, count);
        }
    }

    if (should_write_file) {
        return impl.write(buf, count, pnwrite);
    }

    return ERROR_SUCCESS;
}

string SrsHlsCacheWriter::cache()
{
    return data;
}

SrsHlsSegment::SrsHlsSegment(bool write_cache, bool write_file, SrsCodecAudio ac, SrsCodecVideo vc)
{
    duration = 0;
    sequence_no = 0;
    segment_start_dts = 0;
    is_sequence_header = false;
    writer = new SrsHlsCacheWriter(write_cache, write_file);
    muxer = new SrsTSMuxer(writer, ac, vc);
}

SrsHlsSegment::~SrsHlsSegment()
{
    srs_freep(muxer);
    srs_freep(writer);
}

void SrsHlsSegment::update_duration(int64_t current_frame_dts)
{
    // we use video/audio to update segment duration,
    // so when reap segment, some previous audio frame will
    // update the segment duration, which is nagetive,
    // just ignore it.
    if (current_frame_dts < segment_start_dts) {
        return;
    }
    
    duration = (current_frame_dts - segment_start_dts) / 90000.0;
    srs_assert(duration >= 0);
    
    return;
}

SrsDvrAsyncCallOnHls::SrsDvrAsyncCallOnHls(SrsRequest* r, string p, int s, double d)
{
    req = r;
    path = p;
    seq_no = s;
    duration = d;
}

SrsDvrAsyncCallOnHls::~SrsDvrAsyncCallOnHls()
{
}

int SrsDvrAsyncCallOnHls::call()
{
    int ret = ERROR_SUCCESS;
    
#ifdef SRS_AUTO_HTTP_CALLBACK
    // http callback for on_hls in config.
    if (_srs_config->get_vhost_http_hooks_enabled(req->vhost)) {
        // HTTP: on_hls
        SrsConfDirective* on_hls = _srs_config->get_vhost_on_hls(req->vhost);
        if (!on_hls) {
            srs_info("ignore the empty http callback: on_hls");
            return ret;
        }
        
        std::string file = path;
        int sn = seq_no;
        for (int i = 0; i < (int)on_hls->args.size(); i++) {
            std::string url = on_hls->args.at(i);
            if ((ret = SrsHttpHooks::on_hls(url, req, file, sn, duration)) != ERROR_SUCCESS) {
                srs_error("hook client on_hls failed. url=%s, ret=%d", url.c_str(), ret);
                return ret;
            }
        }
    }
#endif
    
    return ret;
}

string SrsDvrAsyncCallOnHls::to_string()
{
    std::stringstream ss;
    ss << "vhost=" << req->vhost << ", file=" << path;
    return ss.str();
}

SrsHlsMuxer::SrsHlsMuxer()
{
    req = NULL;
    handler = NULL;
    hls_fragment = hls_window = 0;
    hls_aof_ratio = 1.0;
    hls_fragment_deviation = 0;
    hls_cleanup = true;
    previous_floor_ts = 0;
    accept_floor_ts = 0;
    hls_ts_floor = false;
    target_duration = 0;
    _sequence_no = 0;
    current = NULL;
    acodec = SrsCodecAudioReserved1;
    should_write_cache = false;
    should_write_file = true;
    async = new SrsDvrAsyncCallThread();
}

SrsHlsMuxer::~SrsHlsMuxer()
{
    std::vector<SrsHlsSegment*>::iterator it;
    for (it = segments.begin(); it != segments.end(); ++it) {
        SrsHlsSegment* segment = *it;
        srs_freep(segment);
    }
    segments.clear();
    
    srs_freep(current);
    srs_freep(req);
    srs_freep(async);
}

int SrsHlsMuxer::sequence_no()
{
    return _sequence_no;
}

string SrsHlsMuxer::ts_url()
{
    return current? current->uri:"";
}

double SrsHlsMuxer::duration()
{
    return current? current->duration:0;
}

double SrsHlsMuxer::deviation()
{
    return hls_fragment_deviation;
}

int SrsHlsMuxer::initialize(ISrsHlsHandler* h)
{
    int ret = ERROR_SUCCESS;
    
    handler = h;
    
    if ((ret = async->start()) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int SrsHlsMuxer::update_config(SrsRequest* r, string entry_prefix,
    string path, string m3u8_file, string ts_file, double fragment, double window,
    bool ts_floor, double aof_ratio, bool cleanup
) {
    int ret = ERROR_SUCCESS;
    
    srs_freep(req);
    req = r->copy();

    hls_entry_prefix = entry_prefix;
    hls_path = path;
    hls_ts_file = ts_file;
    hls_fragment = fragment;
    hls_aof_ratio = aof_ratio;
    hls_ts_floor = ts_floor;
    hls_cleanup = cleanup;
    previous_floor_ts = 0;
    accept_floor_ts = 0;
    hls_window = window;
    // for the first time, we set to -N% of fragment,
    // that is, the first piece always smaller.
    hls_fragment_deviation = -1 * (fragment * SRS_HLS_FLOOR_STARTUP_PERCENT);
    
    // generate the m3u8 dir and path.
    m3u8 = path + "/" + m3u8_file;
    m3u8 = srs_path_build_stream(m3u8, req->vhost, req->app, req->stream);
    
    m3u8_dir = m3u8;
    size_t pos = string::npos;
    if ((pos = m3u8_dir.rfind("/")) != string::npos) {
        m3u8_dir = m3u8_dir.substr(0, pos);
    }

    // we always keep the target duration increasing.
    int max_td = srs_max(target_duration, (int)(fragment * _srs_config->get_hls_td_ratio(r->vhost)));
    srs_info("hls update target duration %d=>%d, aof=%.2f", target_duration, max_td, aof_ratio);
    target_duration = max_td;

    std::string storage = _srs_config->get_hls_storage(r->vhost);
    if (storage == "ram") {
        should_write_cache = true;
        should_write_file = false;
    } else if (storage == "disk") {
        should_write_cache = false;
        should_write_file = true;
    } else {
        srs_assert(storage == "both");
        should_write_cache = true;
        should_write_file = true;
    }
    
    return ret;
}

int SrsHlsMuxer::segment_open(int64_t segment_start_dts)
{
    int ret = ERROR_SUCCESS;
    
    if (current) {
        srs_warn("ignore the segment open, for segment is already open.");
        return ret;
    }
    
    // when segment open, the current segment must be NULL.
    srs_assert(!current);

    // load the default acodec from config.
    SrsCodecAudio default_acodec = SrsCodecAudioAAC;
    if (true) {
        std::string default_acodec_str = _srs_config->get_hls_acodec(req->vhost);
        if (default_acodec_str == "mp3") {
            default_acodec = SrsCodecAudioMP3;
            srs_info("hls: use default mp3 acodec");
        } else if (default_acodec_str == "aac") {
            default_acodec = SrsCodecAudioAAC;
            srs_info("hls: use default aac acodec");
        } else {
            srs_warn("hls: use aac for other codec=%s", default_acodec_str.c_str());
        }
    }

    // load the default vcodec from config.
    SrsCodecVideo default_vcodec = SrsCodecVideoAVC;
    if (true) {
        std::string default_vcodec_str = _srs_config->get_hls_vcodec(req->vhost);
        if (default_vcodec_str == "h264") {
            default_vcodec = SrsCodecVideoAVC;
            srs_info("hls: use default h264 vcodec");
        } else if (default_vcodec_str == "vn") {
            default_vcodec = SrsCodecVideoDisabled;
            srs_info("hls: use default vn vcodec for pure audio");
        } else {
            srs_warn("hls: use h264 for other codec=%s", default_vcodec_str.c_str());
        }
    }
    
    // new segment.
    current = new SrsHlsSegment(should_write_cache, should_write_file, default_acodec, default_vcodec);
    current->sequence_no = _sequence_no++;
    current->segment_start_dts = segment_start_dts;
    
    // generate filename.
    std::string ts_file = hls_ts_file;
    ts_file = srs_path_build_stream(ts_file, req->vhost, req->app, req->stream);
    if (hls_ts_floor) {
        // accept the floor ts for the first piece.
        int64_t floor_ts = (int64_t)(srs_get_system_time_ms() / (1000 * hls_fragment));
        if (!accept_floor_ts) {
            accept_floor_ts = floor_ts - 1;
        } else {
            accept_floor_ts++;
        }
        
        // we always ensure the piece is increase one by one.
        std::stringstream ts_floor;
        ts_floor << accept_floor_ts;
        ts_file = srs_string_replace(ts_file, "[timestamp]", ts_floor.str());
        
        // dup/jmp detect for ts in floor mode.
        if (previous_floor_ts && previous_floor_ts != floor_ts - 1) {
            srs_warn("hls: dup or jmp for floor ts, previous=%"PRId64", current=%"PRId64", ts=%s, deviation=%.2f",
                     previous_floor_ts, floor_ts, ts_file.c_str(), hls_fragment_deviation);
        }
        previous_floor_ts = floor_ts;
    }
    ts_file = srs_path_build_timestamp(ts_file);
    if (true) {
        std::stringstream ss;
        ss << current->sequence_no;
        ts_file = srs_string_replace(ts_file, "[seq]", ss.str());
    }
    current->full_path = hls_path + "/" + ts_file;
    srs_info("hls: generate ts path %s, tmpl=%s, floor=%d", ts_file.c_str(), hls_ts_file.c_str(), hls_ts_floor);
    
    // the ts url, relative or absolute url.
    std::string ts_url = current->full_path;
    if (srs_string_starts_with(ts_url, m3u8_dir)) {
        ts_url = ts_url.substr(m3u8_dir.length());
    }
    while (srs_string_starts_with(ts_url, "/")) {
        ts_url = ts_url.substr(1);
    }
    current->uri += hls_entry_prefix;
    if (!hls_entry_prefix.empty() && !srs_string_ends_with(hls_entry_prefix, "/")) {
        current->uri += "/";
    }
    current->uri += ts_url;
    
    // create dir recursively for hls.
    if (should_write_file && (ret = srs_create_dir_recursively(m3u8_dir)) != ERROR_SUCCESS) {
        srs_error("create app dir %s failed. ret=%d", m3u8_dir.c_str(), ret);
        return ret;
    }
    srs_info("create app dir %s ok", m3u8_dir.c_str());
    
    // open temp ts file.
    std::string tmp_file = current->full_path + ".tmp";
    if ((ret = current->muxer->open(tmp_file.c_str())) != ERROR_SUCCESS) {
        srs_error("open hls muxer failed. ret=%d", ret);
        return ret;
    }
    srs_info("open HLS muxer success. path=%s, tmp=%s",
        current->full_path.c_str(), tmp_file.c_str());

    // set the segment muxer audio codec.
    if (acodec != SrsCodecAudioReserved1) {
        current->muxer->update_acodec(acodec);
    }
    
    return ret;
}

int SrsHlsMuxer::on_sequence_header()
{
    int ret = ERROR_SUCCESS;
    
    srs_assert(current);
    
    // set the current segment to sequence header,
    // when close the segement, it will write a discontinuity to m3u8 file.
    current->is_sequence_header = true;
    
    return ret;
}

bool SrsHlsMuxer::is_segment_overflow()
{
    srs_assert(current);
    
    // use N% deviation, to smoother.
    double deviation = hls_ts_floor? SRS_HLS_FLOOR_REAP_PERCENT * hls_fragment_deviation : 0.0;
    
    return current->duration >= hls_fragment + deviation;
}

bool SrsHlsMuxer::is_segment_absolutely_overflow()
{
    // @see https://github.com/winlinvip/simple-rtmp-server/issues/151#issuecomment-83553950
    srs_assert(current);
    
    // use N% deviation, to smoother.
    double deviation = hls_ts_floor? SRS_HLS_FLOOR_REAP_PERCENT * hls_fragment_deviation : 0.0;
    
    return current->duration >= hls_aof_ratio * (hls_fragment + deviation);
}

int SrsHlsMuxer::update_acodec(SrsCodecAudio ac)
{
    srs_assert(current);
    srs_assert(current->muxer);
    acodec = ac;
    return current->muxer->update_acodec(ac);
}

int SrsHlsMuxer::flush_audio(SrsTsCache* cache)
{
    int ret = ERROR_SUCCESS;

    // if current is NULL, segment is not open, ignore the flush event.
    if (!current) {
        srs_warn("flush audio ignored, for segment is not open.");
        return ret;
    }
    
    if (!cache->audio || cache->audio->payload->length() <= 0) {
        return ret;
    }
    
    // update the duration of segment.
    current->update_duration(cache->audio->pts);
    
    if ((ret = current->muxer->write_audio(cache->audio)) != ERROR_SUCCESS) {
        return ret;
    }
    
    // write success, clear and free the msg
    srs_freep(cache->audio);

    return ret;
}

int SrsHlsMuxer::flush_video(SrsTsCache* cache)
{
    int ret = ERROR_SUCCESS;

    // if current is NULL, segment is not open, ignore the flush event.
    if (!current) {
        srs_warn("flush video ignored, for segment is not open.");
        return ret;
    }
    
    if (!cache->video || cache->video->payload->length() <= 0) {
        return ret;
    }
    
    srs_assert(current);
    
    // update the duration of segment.
    current->update_duration(cache->video->dts);
    
    if ((ret = current->muxer->write_video(cache->video)) != ERROR_SUCCESS) {
        return ret;
    }
    
    // write success, clear and free the msg
    srs_freep(cache->video);
    
    return ret;
}

int SrsHlsMuxer::segment_close(string log_desc)
{
    int ret = ERROR_SUCCESS;
    
    if (!current) {
        srs_warn("ignore the segment close, for segment is not open.");
        return ret;
    }
    
    // when close current segment, the current segment must not be NULL.
    srs_assert(current);

    // assert segment duplicate.
    std::vector<SrsHlsSegment*>::iterator it;
    it = std::find(segments.begin(), segments.end(), current);
    srs_assert(it == segments.end());

    // valid, add to segments if segment duration is ok
    if (current->duration * 1000 >= SRS_AUTO_HLS_SEGMENT_MIN_DURATION_MS) {
        segments.push_back(current);
        
        // when reap ts, adjust the deviation.
        if (hls_ts_floor) {
            hls_fragment_deviation += (double)(hls_fragment - current->duration);
        }
        
        // use async to call the http hooks, for it will cause thread switch.
        if ((ret = async->call(new SrsDvrAsyncCallOnHls(req, current->full_path, current->sequence_no, current->duration))) != ERROR_SUCCESS) {
            return ret;
        }
    
        srs_info("%s reap ts segment, sequence_no=%d, uri=%s, duration=%.2f, start=%"PRId64", deviation=%.2f",
            log_desc.c_str(), current->sequence_no, current->uri.c_str(), current->duration, 
            current->segment_start_dts, hls_fragment_deviation);
        
        // notify handler for update ts.
        srs_assert(current->writer);
        if (handler && (ret = handler->on_update_ts(req, current->uri, current->writer->cache())) != ERROR_SUCCESS) {
            srs_error("notify handler for update ts failed. ret=%d", ret);
            return ret;
        }
    
        // close the muxer of finished segment.
        srs_freep(current->muxer);
        std::string full_path = current->full_path;
        current = NULL;
        
        // rename from tmp to real path
        std::string tmp_file = full_path + ".tmp";
        if (should_write_file && rename(tmp_file.c_str(), full_path.c_str()) < 0) {
            ret = ERROR_HLS_WRITE_FAILED;
            srs_error("rename ts file failed, %s => %s. ret=%d", 
                tmp_file.c_str(), full_path.c_str(), ret);
            return ret;
        }
    } else {
        // reuse current segment index.
        _sequence_no--;
        
        srs_trace("%s drop ts segment, sequence_no=%d, uri=%s, duration=%.2f, start=%"PRId64"",
            log_desc.c_str(), current->sequence_no, current->uri.c_str(), current->duration, 
            current->segment_start_dts);
        
        // rename from tmp to real path
        std::string tmp_file = current->full_path + ".tmp";
        if (should_write_file) {
            unlink(tmp_file.c_str());
        }
        
        srs_freep(current);
    }
    
    // the segments to remove
    std::vector<SrsHlsSegment*> segment_to_remove;
    
    // shrink the segments.
    double duration = 0;
    int remove_index = -1;
    for (int i = (int)segments.size() - 1; i >= 0; i--) {
        SrsHlsSegment* segment = segments[i];
        duration += segment->duration;
        
        if ((int)duration > hls_window) {
            remove_index = i;
            break;
        }
    }
    for (int i = 0; i < remove_index && !segments.empty(); i++) {
        SrsHlsSegment* segment = *segments.begin();
        segments.erase(segments.begin());
        segment_to_remove.push_back(segment);
    }
    
    // refresh the m3u8, donot contains the removed ts
    ret = refresh_m3u8();

    // remove the ts file.
    for (int i = 0; i < (int)segment_to_remove.size(); i++) {
        SrsHlsSegment* segment = segment_to_remove[i];
        
        if (hls_cleanup) {
            unlink(segment->full_path.c_str());
        }
        
        srs_freep(segment);
    }
    segment_to_remove.clear();
    
    // check ret of refresh m3u8
    if (ret != ERROR_SUCCESS) {
        srs_error("refresh m3u8 failed. ret=%d", ret);
        return ret;
    }
    
    return ret;
}

int SrsHlsMuxer::refresh_m3u8()
{
    int ret = ERROR_SUCCESS;
    
    std::string temp_m3u8 = m3u8 + ".temp";
    if ((ret = _refresh_m3u8(temp_m3u8)) == ERROR_SUCCESS) {
        if (should_write_file && rename(temp_m3u8.c_str(), m3u8.c_str()) < 0) {
            ret = ERROR_HLS_WRITE_FAILED;
            srs_error("rename m3u8 file failed. %s => %s, ret=%d", temp_m3u8.c_str(), m3u8.c_str(), ret);
        }
    }
    
    // remove the temp file.
    unlink(temp_m3u8.c_str());
    
    return ret;
}

int SrsHlsMuxer::_refresh_m3u8(string m3u8_file)
{
    int ret = ERROR_SUCCESS;
    
    // no segments, return.
    if (segments.size() == 0) {
        return ret;
    }

    SrsHlsCacheWriter writer(should_write_cache, should_write_file);
    if ((ret = writer.open(m3u8_file)) != ERROR_SUCCESS) {
        srs_error("open m3u8 file %s failed. ret=%d", m3u8_file.c_str(), ret);
        return ret;
    }
    srs_info("open m3u8 file %s success.", m3u8_file.c_str());
    
    // #EXTM3U\n
    // #EXT-X-VERSION:3\n
    // #EXT-X-ALLOW-CACHE:YES\n
    std::stringstream ss;
    ss << "#EXTM3U" << SRS_CONSTS_LF
        << "#EXT-X-VERSION:3" << SRS_CONSTS_LF
        << "#EXT-X-ALLOW-CACHE:YES" << SRS_CONSTS_LF;
    srs_verbose("write m3u8 header success.");
    
    // #EXT-X-MEDIA-SEQUENCE:4294967295\n
    SrsHlsSegment* first = *segments.begin();
    ss << "#EXT-X-MEDIA-SEQUENCE:" << first->sequence_no << SRS_CONSTS_LF;
    srs_verbose("write m3u8 sequence success.");
    
    // #EXT-X-TARGETDURATION:4294967295\n
    /**
    * @see hls-m3u8-draft-pantos-http-live-streaming-12.pdf, page 25
    * The Media Playlist file MUST contain an EXT-X-TARGETDURATION tag.
    * Its value MUST be equal to or greater than the EXTINF duration of any
    * media segment that appears or will appear in the Playlist file,
    * rounded to the nearest integer. Its value MUST NOT change. A
    * typical target duration is 10 seconds.
    */
    // @see https://github.com/winlinvip/simple-rtmp-server/issues/304#issuecomment-74000081
    std::vector<SrsHlsSegment*>::iterator it;
    for (it = segments.begin(); it != segments.end(); ++it) {
        SrsHlsSegment* segment = *it;
        target_duration = srs_max(target_duration, (int)ceil(segment->duration));
    }
    ss << "#EXT-X-TARGETDURATION:" << target_duration << SRS_CONSTS_LF;
    srs_verbose("write m3u8 duration success.");
    
    // write all segments
    for (it = segments.begin(); it != segments.end(); ++it) {
        SrsHlsSegment* segment = *it;
        
        if (segment->is_sequence_header) {
            // #EXT-X-DISCONTINUITY\n
            ss << "#EXT-X-DISCONTINUITY" << SRS_CONSTS_LF;
            srs_verbose("write m3u8 segment discontinuity success.");
        }
        
        // "#EXTINF:4294967295.208,\n"
        ss.precision(3);
        ss.setf(std::ios::fixed, std::ios::floatfield);
        ss << "#EXTINF:" << segment->duration << "," << SRS_CONSTS_LF;
        srs_verbose("write m3u8 segment info success.");
        
        // {file name}\n
        ss << segment->uri << SRS_CONSTS_LF;
        srs_verbose("write m3u8 segment uri success.");
    }

    // write m3u8 to writer.
    std::string m3u8 = ss.str();
    if ((ret = writer.write((char*)m3u8.c_str(), (int)m3u8.length(), NULL)) != ERROR_SUCCESS) {
        srs_error("write m3u8 failed. ret=%d", ret);
        return ret;
    }
    srs_info("write m3u8 %s success.", m3u8_file.c_str());

    // notify handler for update m3u8.
    if (handler && (ret = handler->on_update_m3u8(req, writer.cache())) != ERROR_SUCCESS) {
        srs_error("notify handler for update m3u8 failed. ret=%d", ret);
        return ret;
    }
    
    return ret;
}

SrsHlsCache::SrsHlsCache()
{
    cache = new SrsTsCache();
}

SrsHlsCache::~SrsHlsCache()
{
    srs_freep(cache);
}

int SrsHlsCache::on_publish(SrsHlsMuxer* muxer, SrsRequest* req, int64_t segment_start_dts)
{
    int ret = ERROR_SUCCESS;

    std::string vhost = req->vhost;
    std::string stream = req->stream;
    std::string app = req->app;
    
    double hls_fragment = _srs_config->get_hls_fragment(vhost);
    double hls_window = _srs_config->get_hls_window(vhost);
    
    // get the hls m3u8 ts list entry prefix config
    std::string entry_prefix = _srs_config->get_hls_entry_prefix(vhost);
    // get the hls path config
    std::string path = _srs_config->get_hls_path(vhost);
    std::string m3u8_file = _srs_config->get_hls_m3u8_file(vhost);
    std::string ts_file = _srs_config->get_hls_ts_file(vhost);
    bool cleanup = _srs_config->get_hls_cleanup(vhost);
    // the audio overflow, for pure audio to reap segment.
    double hls_aof_ratio = _srs_config->get_hls_aof_ratio(vhost);
    // whether use floor(timestamp/hls_fragment) for variable timestamp
    bool ts_floor = _srs_config->get_hls_ts_floor(vhost);
    
    // TODO: FIXME: support load exists m3u8, to continue publish stream.
    // for the HLS donot requires the EXT-X-MEDIA-SEQUENCE be monotonically increase.
    
    // open muxer
    if ((ret = muxer->update_config(req, entry_prefix,
        path, m3u8_file, ts_file, hls_fragment, hls_window, ts_floor, hls_aof_ratio,
        cleanup)) != ERROR_SUCCESS
    ) {
        srs_error("m3u8 muxer update config failed. ret=%d", ret);
        return ret;
    }
    
    if ((ret = muxer->segment_open(segment_start_dts)) != ERROR_SUCCESS) {
        srs_error("m3u8 muxer open segment failed. ret=%d", ret);
        return ret;
    }
    srs_trace("hls: win=%.2f, frag=%.2f, prefix=%s, path=%s, m3u8=%s, ts=%s, aof=%.2f, floor=%d",
        hls_window, hls_fragment, entry_prefix.c_str(), path.c_str(), m3u8_file.c_str(),
        ts_file.c_str(), hls_aof_ratio, ts_floor);
    
    return ret;
}

int SrsHlsCache::on_unpublish(SrsHlsMuxer* muxer)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = muxer->flush_audio(cache)) != ERROR_SUCCESS) {
        srs_error("m3u8 muxer flush audio failed. ret=%d", ret);
        return ret;
    }
    
    if ((ret = muxer->segment_close("unpublish")) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int SrsHlsCache::on_sequence_header(SrsHlsMuxer* muxer)
{
    // TODO: support discontinuity for the same stream
    // currently we reap and insert discontinity when encoder republish,
    // but actually, event when stream is not republish, the 
    // sequence header may change, for example,
    // ffmpeg ingest a external rtmp stream and push to srs,
    // when the sequence header changed, the stream is not republish.
    return muxer->on_sequence_header();
}
    
int SrsHlsCache::write_audio(SrsAvcAacCodec* codec, SrsHlsMuxer* muxer, int64_t pts, SrsCodecSample* sample)
{
    int ret = ERROR_SUCCESS;
    
    // write audio to cache.
    if ((ret = cache->cache_audio(codec, pts, sample)) != ERROR_SUCCESS) {
        return ret;
    }
    
    // flush if buffer exceed max size.
    if (cache->audio->payload->length() > SRS_AUTO_HLS_AUDIO_CACHE_SIZE) {
        if ((ret = muxer->flush_audio(cache)) != ERROR_SUCCESS) {
            return ret;
        }
    }

    // TODO: config it.
    // in ms, audio delay to flush the audios.
    int64_t audio_delay = SRS_CONF_DEFAULT_AAC_DELAY;
    // flush if audio delay exceed
    // cache->audio will be free in flush_audio
    // so we must check whether it's null ptr.
    if (cache->audio && pts - cache->audio->start_pts > audio_delay * 90) {
        if ((ret = muxer->flush_audio(cache)) != ERROR_SUCCESS) {
            return ret;
        }
    }
    
    // reap when current source is pure audio.
    // it maybe changed when stream info changed,
    // for example, pure audio when start, audio/video when publishing,
    // pure audio again for audio disabled.
    // so we reap event when the audio incoming when segment overflow.
    // @see https://github.com/winlinvip/simple-rtmp-server/issues/151
    // we use absolutely overflow of segment to make jwplayer/ffplay happy
    // @see https://github.com/winlinvip/simple-rtmp-server/issues/151#issuecomment-71155184
    if (cache->audio && muxer->is_segment_absolutely_overflow()) {
        if ((ret = reap_segment("audio", muxer, cache->audio->pts)) != ERROR_SUCCESS) {
            return ret;
        }
    }
    
    return ret;
}
    
int SrsHlsCache::write_video(SrsAvcAacCodec* codec, SrsHlsMuxer* muxer, int64_t dts, SrsCodecSample* sample)
{
    int ret = ERROR_SUCCESS;
    
    // write video to cache.
    if ((ret = cache->cache_video(codec, dts, sample)) != ERROR_SUCCESS) {
        return ret;
    }
    
    // new segment when:
    // 1. base on gop.
    // 2. some gops duration overflow.
    if (sample->frame_type == SrsCodecVideoAVCFrameKeyFrame && muxer->is_segment_overflow()) {
        if ((ret = reap_segment("video", muxer, cache->video->dts)) != ERROR_SUCCESS) {
            return ret;
        }
        return ret;
    }
    
    // flush video when got one
    if ((ret = muxer->flush_video(cache)) != ERROR_SUCCESS) {
        srs_error("m3u8 muxer flush video failed. ret=%d", ret);
        return ret;
    }
    
    return ret;
}

int SrsHlsCache::reap_segment(string log_desc, SrsHlsMuxer* muxer, int64_t segment_start_dts)
{
    int ret = ERROR_SUCCESS;

    if ((ret = muxer->segment_close(log_desc)) != ERROR_SUCCESS) {
        srs_error("m3u8 muxer close segment failed. ret=%d", ret);
        return ret;
    }
    
    if ((ret = muxer->segment_open(segment_start_dts)) != ERROR_SUCCESS) {
        srs_error("m3u8 muxer open segment failed. ret=%d", ret);
        return ret;
    }

    // TODO: flush audio before or after segment?
    // TODO: fresh segment begin with audio or video?
    
    // segment open, flush video first.
    if ((ret = muxer->flush_video(cache)) != ERROR_SUCCESS) {
        srs_error("m3u8 muxer flush video failed. ret=%d", ret);
        return ret;
    }

    // segment open, flush the audio.
    // @see: ngx_rtmp_hls_open_fragment
    /* start fragment with audio to make iPhone happy */
    if ((ret = muxer->flush_audio(cache)) != ERROR_SUCCESS) {
        srs_error("m3u8 muxer flush audio failed. ret=%d", ret);
        return ret;
    }
    
    return ret;
}

SrsHls::SrsHls()
{
    source = NULL;
    handler = NULL;
    
    hls_enabled = false;

    codec = new SrsAvcAacCodec();
    sample = new SrsCodecSample();
    jitter = new SrsRtmpJitter();
    
    muxer = new SrsHlsMuxer();
    hls_cache = new SrsHlsCache();

    pprint = SrsPithyPrint::create_hls();
    stream_dts = 0;
}

SrsHls::~SrsHls()
{
    srs_freep(codec);
    srs_freep(sample);
    srs_freep(jitter);
    
    srs_freep(muxer);
    srs_freep(hls_cache);
    
    srs_freep(pprint);
}

int SrsHls::initialize(SrsSource* s, ISrsHlsHandler* h)
{
    int ret = ERROR_SUCCESS;

    source = s;
    handler = h;

    if ((ret = muxer->initialize(h)) != ERROR_SUCCESS) {
        return ret;
    }

    return ret;
}

int SrsHls::on_publish(SrsRequest* req)
{
    int ret = ERROR_SUCCESS;
    
    // support multiple publish.
    if (hls_enabled) {
        return ret;
    }
    
    std::string vhost = req->vhost;
    if (!_srs_config->get_hls_enabled(vhost)) {
        return ret;
    }
    
    if ((ret = hls_cache->on_publish(muxer, req, stream_dts)) != ERROR_SUCCESS) {
        return ret;
    }
    
    // if enabled, open the muxer.
    hls_enabled = true;
    
    // notice the source to get the cached sequence header.
    // when reload to start hls, hls will never get the sequence header in stream,
    // use the SrsSource.on_hls_start to push the sequence header to HLS.
    if ((ret = source->on_hls_start()) != ERROR_SUCCESS) {
        srs_error("callback source hls start failed. ret=%d", ret);
        return ret;
    }

    return ret;
}

void SrsHls::on_unpublish()
{
    int ret = ERROR_SUCCESS;
    
    // support multiple unpublish.
    if (!hls_enabled) {
        return;
    }

    if ((ret = hls_cache->on_unpublish(muxer)) != ERROR_SUCCESS) {
        srs_error("ignore m3u8 muxer flush/close audio failed. ret=%d", ret);
    }
    
    hls_enabled = false;
}

int SrsHls::on_meta_data(SrsAmf0Object* metadata)
{
    int ret = ERROR_SUCCESS;

    if (!metadata) {
        srs_trace("no metadata persent, hls ignored it.");
        return ret;
    }
    
    if (metadata->count() <= 0) {
        srs_trace("no metadata persent, hls ignored it.");
        return ret;
    }
    
    return ret;
}

int SrsHls::on_audio(SrsSharedPtrMessage* shared_audio)
{
    int ret = ERROR_SUCCESS;
    
    if (!hls_enabled) {
        return ret;
    }

    SrsSharedPtrMessage* audio = shared_audio->copy();
    SrsAutoFree(SrsSharedPtrMessage, audio);
    
    sample->clear();
    if ((ret = codec->audio_aac_demux(audio->payload, audio->size, sample)) != ERROR_SUCCESS) {
        if (ret != ERROR_HLS_TRY_MP3) {
            srs_error("hls aac demux audio failed. ret=%d", ret);
            return ret;
        }
        if ((ret = codec->audio_mp3_demux(audio->payload, audio->size, sample)) != ERROR_SUCCESS) {
            srs_error("hls mp3 demux audio failed. ret=%d", ret);
            return ret;
        }
    }
    srs_info("audio decoded, type=%d, codec=%d, cts=%d, size=%d, time=%"PRId64, 
        sample->frame_type, codec->audio_codec_id, sample->cts, audio->size, audio->timestamp);
    SrsCodecAudio acodec = (SrsCodecAudio)codec->audio_codec_id;
    
    // ts support audio codec: aac/mp3
    if (acodec != SrsCodecAudioAAC && acodec != SrsCodecAudioMP3) {
        return ret;
    }

    // when codec changed, write new header.
    if ((ret = muxer->update_acodec(acodec)) != ERROR_SUCCESS) {
        srs_error("http: ts audio write header failed. ret=%d", ret);
        return ret;
    }
    
    // ignore sequence header
    if (acodec == SrsCodecAudioAAC && sample->aac_packet_type == SrsCodecAudioTypeSequenceHeader) {
        return hls_cache->on_sequence_header(muxer);
    }
    
    if ((ret = jitter->correct(audio, 0, 0, SrsRtmpJitterAlgorithmFULL)) != ERROR_SUCCESS) {
        srs_error("rtmp jitter correct audio failed. ret=%d", ret);
        return ret;
    }
    
    // the dts calc from rtmp/flv header.
    int64_t dts = audio->timestamp * 90;
    
    // for pure audio, we need to update the stream dts also.
    stream_dts = dts;
    
    if ((ret = hls_cache->write_audio(codec, muxer, dts, sample)) != ERROR_SUCCESS) {
        srs_error("hls cache write audio failed. ret=%d", ret);
        return ret;
    }
    
    return ret;
}

int SrsHls::on_video(SrsSharedPtrMessage* shared_video)
{
    int ret = ERROR_SUCCESS;
    
    if (!hls_enabled) {
        return ret;
    }

    SrsSharedPtrMessage* video = shared_video->copy();
    SrsAutoFree(SrsSharedPtrMessage, video);
    
    sample->clear();
    if ((ret = codec->video_avc_demux(video->payload, video->size, sample)) != ERROR_SUCCESS) {
        srs_error("hls codec demux video failed. ret=%d", ret);
        return ret;
    }
    srs_info("video decoded, type=%d, codec=%d, avc=%d, cts=%d, size=%d, time=%"PRId64, 
        sample->frame_type, codec->video_codec_id, sample->avc_packet_type, sample->cts, video->size, video->timestamp);
    
    // ignore info frame,
    // @see https://github.com/winlinvip/simple-rtmp-server/issues/288#issuecomment-69863909
    if (sample->frame_type == SrsCodecVideoAVCFrameVideoInfoFrame) {
        return ret;
    }
    
    if (codec->video_codec_id != SrsCodecVideoAVC) {
        return ret;
    }
    
    // ignore sequence header
    if (sample->frame_type == SrsCodecVideoAVCFrameKeyFrame
         && sample->avc_packet_type == SrsCodecVideoAVCTypeSequenceHeader) {
        return hls_cache->on_sequence_header(muxer);
    }
    
    if ((ret = jitter->correct(video, 0, 0, SrsRtmpJitterAlgorithmFULL)) != ERROR_SUCCESS) {
        srs_error("rtmp jitter correct video failed. ret=%d", ret);
        return ret;
    }
    
    int64_t dts = video->timestamp * 90;
    stream_dts = dts;
    if ((ret = hls_cache->write_video(codec, muxer, dts, sample)) != ERROR_SUCCESS) {
        srs_error("hls cache write video failed. ret=%d", ret);
        return ret;
    }
    
    // pithy print message.
    hls_show_mux_log();
    
    return ret;
}

void SrsHls::hls_show_mux_log()
{
    pprint->elapse();

    // reportable
    if (pprint->can_print()) {
        // the run time is not equals to stream time,
        // @see: https://github.com/winlinvip/simple-rtmp-server/issues/81#issuecomment-48100994
        // it's ok.
        srs_trace("-> "SRS_CONSTS_LOG_HLS" time=%"PRId64", stream dts=%"PRId64"(%"PRId64"ms), sequence_no=%d, ts=%s, duration=%.2f, deviation=%.2f",
            pprint->age(), stream_dts, stream_dts / 90, muxer->sequence_no(), muxer->ts_url().c_str(),
            muxer->duration(), muxer->deviation());
    }
}

#endif


