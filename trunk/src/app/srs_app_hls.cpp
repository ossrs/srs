/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2017 SRS(ossrs)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <srs_app_hls.hpp>

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
#include <srs_protocol_amf0.hpp>
#include <srs_rtmp_stack.hpp>
#include <srs_app_config.hpp>
#include <srs_app_source.hpp>
#include <srs_core_autofree.hpp>
#include <srs_rtmp_stack.hpp>
#include <srs_app_pithy_print.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_kernel_codec.hpp>
#include <srs_kernel_file.hpp>
#include <srs_protocol_stream.hpp>
#include <srs_kernel_ts.hpp>
#include <srs_app_utility.hpp>
#include <srs_app_http_hooks.hpp>
#include <srs_protocol_format.hpp>

// drop the segment when duration of ts too small.
#define SRS_AUTO_HLS_SEGMENT_MIN_DURATION_MS 100

// fragment plus the deviation percent.
#define SRS_HLS_FLOOR_REAP_PERCENT 0.3
// reset the piece id when deviation overflow this.
#define SRS_JUMP_WHEN_PIECE_DEVIATION 20

SrsHlsSegment::SrsHlsSegment(SrsTsContext* c, SrsAudioCodecId ac, SrsVideoCodecId vc)
{
    sequence_no = 0;
    writer = new SrsFileWriter();
    tscw = new SrsTsContextWriter(writer, c, ac, vc);
}

SrsHlsSegment::~SrsHlsSegment()
{
    srs_freep(tscw);
    srs_freep(writer);
}

SrsDvrAsyncCallOnHls::SrsDvrAsyncCallOnHls(int c, SrsRequest* r, string p, string t, string m, string mu, int s, double d)
{
    req = r->copy();
    cid = c;
    path = p;
    ts_url = t;
    m3u8 = m;
    m3u8_url = mu;
    seq_no = s;
    duration = d;
}

SrsDvrAsyncCallOnHls::~SrsDvrAsyncCallOnHls()
{
    srs_freep(req);
}

int SrsDvrAsyncCallOnHls::call()
{
    int ret = ERROR_SUCCESS;
    
    if (!_srs_config->get_vhost_http_hooks_enabled(req->vhost)) {
        return ret;
    }
    
    // the http hooks will cause context switch,
    // so we must copy all hooks for the on_connect may freed.
    // @see https://github.com/ossrs/srs/issues/475
    vector<string> hooks;
    
    if (true) {
        SrsConfDirective* conf = _srs_config->get_vhost_on_hls(req->vhost);
        
        if (!conf) {
            srs_info("ignore the empty http callback: on_hls");
            return ret;
        }
        
        hooks = conf->args;
    }
    
    for (int i = 0; i < (int)hooks.size(); i++) {
        std::string url = hooks.at(i);
        if ((ret = SrsHttpHooks::on_hls(cid, url, req, path, ts_url, m3u8, m3u8_url, seq_no, duration)) != ERROR_SUCCESS) {
            srs_error("hook client on_hls failed. url=%s, ret=%d", url.c_str(), ret);
            return ret;
        }
    }
    
    return ret;
}

string SrsDvrAsyncCallOnHls::to_string()
{
    return "on_hls: " + path;
}

SrsDvrAsyncCallOnHlsNotify::SrsDvrAsyncCallOnHlsNotify(int c, SrsRequest* r, string u)
{
    cid = c;
    req = r->copy();
    ts_url = u;
}

SrsDvrAsyncCallOnHlsNotify::~SrsDvrAsyncCallOnHlsNotify()
{
    srs_freep(req);
}

int SrsDvrAsyncCallOnHlsNotify::call()
{
    int ret = ERROR_SUCCESS;
    
    if (!_srs_config->get_vhost_http_hooks_enabled(req->vhost)) {
        return ret;
    }
    
    // the http hooks will cause context switch,
    // so we must copy all hooks for the on_connect may freed.
    // @see https://github.com/ossrs/srs/issues/475
    vector<string> hooks;
    
    if (true) {
        SrsConfDirective* conf = _srs_config->get_vhost_on_hls_notify(req->vhost);
        
        if (!conf) {
            srs_info("ignore the empty http callback: on_hls_notify");
            return ret;
        }
        
        hooks = conf->args;
    }
    
    int nb_notify = _srs_config->get_vhost_hls_nb_notify(req->vhost);
    for (int i = 0; i < (int)hooks.size(); i++) {
        std::string url = hooks.at(i);
        if ((ret = SrsHttpHooks::on_hls_notify(cid, url, req, ts_url, nb_notify)) != ERROR_SUCCESS) {
            srs_error("hook client on_hls_notify failed. url=%s, ret=%d", url.c_str(), ret);
            return ret;
        }
    }
    
    return ret;
}

string SrsDvrAsyncCallOnHlsNotify::to_string()
{
    return "on_hls_notify: " + ts_url;
}

SrsHlsMuxer::SrsHlsMuxer()
{
    req = NULL;
    hls_fragment = hls_window = 0;
    hls_aof_ratio = 1.0;
    deviation_ts = 0;
    hls_cleanup = true;
    hls_wait_keyframe = true;
    previous_floor_ts = 0;
    accept_floor_ts = 0;
    hls_ts_floor = false;
    max_td = 0;
    _sequence_no = 0;
    current = NULL;
    async = new SrsAsyncCallWorker();
    context = new SrsTsContext();
    segments = new SrsFragmentWindow();
}

SrsHlsMuxer::~SrsHlsMuxer()
{
    srs_freep(current);
    srs_freep(req);
    srs_freep(async);
    srs_freep(context);
}

void SrsHlsMuxer::dispose()
{
    int ret = ERROR_SUCCESS;
    
    segments->dispose();
    
    if (current) {
        if ((ret = current->unlink_tmpfile()) != ERROR_SUCCESS) {
            srs_warn("Unlink tmp ts failed, ret=%d", ret);
        }
        srs_freep(current);
    }
    
    if (unlink(m3u8.c_str()) < 0) {
        srs_warn("dispose unlink path failed. file=%s", m3u8.c_str());
    }
    
    srs_trace("gracefully dispose hls %s", req? req->get_stream_url().c_str() : "");
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
    return current? current->duration()/1000.0:0;
}

int SrsHlsMuxer::deviation()
{
    // no floor, no deviation.
    if (!hls_ts_floor) {
        return 0;
    }
    
    return deviation_ts;
}

int SrsHlsMuxer::initialize()
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = async->start()) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int SrsHlsMuxer::update_config(SrsRequest* r, string entry_prefix,
    string path, string m3u8_file, string ts_file, double fragment, double window,
    bool ts_floor, double aof_ratio, bool cleanup, bool wait_keyframe
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
    hls_wait_keyframe = wait_keyframe;
    previous_floor_ts = 0;
    accept_floor_ts = 0;
    hls_window = window;
    deviation_ts = 0;
    
    // generate the m3u8 dir and path.
    m3u8_url = srs_path_build_stream(m3u8_file, req->vhost, req->app, req->stream);
    m3u8 = path + "/" + m3u8_url;
    
    // when update config, reset the history target duration.
    max_td = (int)(fragment * _srs_config->get_hls_td_ratio(r->vhost));
    
    // create m3u8 dir once.
    m3u8_dir = srs_path_dirname(m3u8);
    if ((ret = srs_create_dir_recursively(m3u8_dir)) != ERROR_SUCCESS) {
        srs_error("create app dir %s failed. ret=%d", m3u8_dir.c_str(), ret);
        return ret;
    }
    srs_info("create m3u8 dir %s ok", m3u8_dir.c_str());
    
    return ret;
}

int SrsHlsMuxer::segment_open()
{
    int ret = ERROR_SUCCESS;
    
    if (current) {
        srs_warn("ignore the segment open, for segment is already open.");
        return ret;
    }
    
    // when segment open, the current segment must be NULL.
    srs_assert(!current);
    
    // load the default acodec from config.
    SrsAudioCodecId default_acodec = SrsAudioCodecIdAAC;
    if (true) {
        std::string default_acodec_str = _srs_config->get_hls_acodec(req->vhost);
        if (default_acodec_str == "mp3") {
            default_acodec = SrsAudioCodecIdMP3;
            srs_info("hls: use default mp3 acodec");
        } else if (default_acodec_str == "aac") {
            default_acodec = SrsAudioCodecIdAAC;
            srs_info("hls: use default aac acodec");
        } else if (default_acodec_str == "an") {
            default_acodec = SrsAudioCodecIdDisabled;
            srs_info("hls: use default an acodec for pure video");
        } else {
            srs_warn("hls: use aac for other codec=%s", default_acodec_str.c_str());
        }
    }
    
    // load the default vcodec from config.
    SrsVideoCodecId default_vcodec = SrsVideoCodecIdAVC;
    if (true) {
        std::string default_vcodec_str = _srs_config->get_hls_vcodec(req->vhost);
        if (default_vcodec_str == "h264") {
            default_vcodec = SrsVideoCodecIdAVC;
            srs_info("hls: use default h264 vcodec");
        } else if (default_vcodec_str == "vn") {
            default_vcodec = SrsVideoCodecIdDisabled;
            srs_info("hls: use default vn vcodec for pure audio");
        } else {
            srs_warn("hls: use h264 for other codec=%s", default_vcodec_str.c_str());
        }
    }
    
    // new segment.
    current = new SrsHlsSegment(context, default_acodec, default_vcodec);
    current->sequence_no = _sequence_no++;
    
    // generate filename.
    std::string ts_file = hls_ts_file;
    ts_file = srs_path_build_stream(ts_file, req->vhost, req->app, req->stream);
    if (hls_ts_floor) {
        // accept the floor ts for the first piece.
        int64_t current_floor_ts = (int64_t)(srs_update_system_time_ms() / (1000 * hls_fragment));
        if (!accept_floor_ts) {
            accept_floor_ts = current_floor_ts - 1;
        } else {
            accept_floor_ts++;
        }
        
        // jump when deviation more than 10p
        if (accept_floor_ts - current_floor_ts > SRS_JUMP_WHEN_PIECE_DEVIATION) {
            srs_warn("hls: jmp for ts deviation, current=%"PRId64", accept=%"PRId64, current_floor_ts, accept_floor_ts);
            accept_floor_ts = current_floor_ts - 1;
        }
        
        // when reap ts, adjust the deviation.
        deviation_ts = (int)(accept_floor_ts - current_floor_ts);
        
        // dup/jmp detect for ts in floor mode.
        if (previous_floor_ts && previous_floor_ts != current_floor_ts - 1) {
            srs_warn("hls: dup/jmp ts, previous=%"PRId64", current=%"PRId64", accept=%"PRId64", deviation=%d",
                     previous_floor_ts, current_floor_ts, accept_floor_ts, deviation_ts);
        }
        previous_floor_ts = current_floor_ts;
        
        // we always ensure the piece is increase one by one.
        std::stringstream ts_floor;
        ts_floor << accept_floor_ts;
        ts_file = srs_string_replace(ts_file, "[timestamp]", ts_floor.str());
        
        // TODO: FIMXE: we must use the accept ts floor time to generate the hour variable.
        ts_file = srs_path_build_timestamp(ts_file);
    } else {
        ts_file = srs_path_build_timestamp(ts_file);
    }
    if (true) {
        std::stringstream ss;
        ss << current->sequence_no;
        ts_file = srs_string_replace(ts_file, "[seq]", ss.str());
    }
    current->set_path(hls_path + "/" + ts_file);
    srs_info("hls: generate ts path %s, tmpl=%s, floor=%d", ts_file.c_str(), hls_ts_file.c_str(), hls_ts_floor);
    
    // the ts url, relative or absolute url.
    // TODO: FIXME: Use url and path manager.
    std::string ts_url = current->fullpath();
    if (srs_string_starts_with(ts_url, m3u8_dir)) {
        ts_url = ts_url.substr(m3u8_dir.length());
    }
    while (srs_string_starts_with(ts_url, "/")) {
        ts_url = ts_url.substr(1);
    }
    current->uri += hls_entry_prefix;
    if (!hls_entry_prefix.empty() && !srs_string_ends_with(hls_entry_prefix, "/")) {
        current->uri += "/";
        
        // add the http dir to uri.
        string http_dir = srs_path_dirname(m3u8_url);
        if (!http_dir.empty()) {
            current->uri += http_dir + "/";
        }
    }
    current->uri += ts_url;
    
    // create dir recursively for hls.
    if ((ret = current->create_dir()) != ERROR_SUCCESS) {
        return ret;
    }
    
    // open temp ts file.
    std::string tmp_file = current->tmppath();
    if ((ret = current->tscw->open(tmp_file.c_str())) != ERROR_SUCCESS) {
        srs_error("open hls muxer failed. ret=%d", ret);
        return ret;
    }
    srs_info("open HLS muxer success. path=%s, tmp=%s", current->fullpath().c_str(), tmp_file.c_str());
    
    return ret;
}

int SrsHlsMuxer::on_sequence_header()
{
    int ret = ERROR_SUCCESS;
    
    srs_assert(current);
    
    // set the current segment to sequence header,
    // when close the segement, it will write a discontinuity to m3u8 file.
    current->set_sequence_header(true);
    
    return ret;
}

bool SrsHlsMuxer::is_segment_overflow()
{
    srs_assert(current);
    
    // to prevent very small segment.
    if (current->duration() < 2 * SRS_AUTO_HLS_SEGMENT_MIN_DURATION_MS) {
        return false;
    }
    
    // use N% deviation, to smoother.
    double deviation = hls_ts_floor? SRS_HLS_FLOOR_REAP_PERCENT * deviation_ts * hls_fragment : 0.0;
    srs_info("hls: dur=%"PRId64"ms, tar=%.2f, dev=%.2fms/%dp, frag=%.2f",
             current->duration(), hls_fragment + deviation, deviation, deviation_ts, hls_fragment);
    
    return current->duration() >= (hls_fragment + deviation) * 1000;
}

bool SrsHlsMuxer::wait_keyframe()
{
    return hls_wait_keyframe;
}

bool SrsHlsMuxer::is_segment_absolutely_overflow()
{
    // @see https://github.com/ossrs/srs/issues/151#issuecomment-83553950
    srs_assert(current);
    
    // to prevent very small segment.
    if (current->duration() < 2 * SRS_AUTO_HLS_SEGMENT_MIN_DURATION_MS) {
        return false;
    }
    
    // use N% deviation, to smoother.
    double deviation = hls_ts_floor? SRS_HLS_FLOOR_REAP_PERCENT * deviation_ts * hls_fragment : 0.0;
    srs_info("hls: dur=%"PRId64"ms, tar=%.2f, dev=%.2fms/%dp, frag=%.2f",
             current->duration(), hls_fragment + deviation, deviation, deviation_ts, hls_fragment);
    
    return current->duration() >= (hls_aof_ratio * hls_fragment + deviation) * 1000;
}

bool SrsHlsMuxer::pure_audio()
{
    return current && current->tscw && current->tscw->video_codec() == SrsVideoCodecIdDisabled;
}

int SrsHlsMuxer::flush_audio(SrsTsMessageCache* cache)
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
    current->append(cache->audio->pts / 90);
    
    if ((ret = current->tscw->write_audio(cache->audio)) != ERROR_SUCCESS) {
        return ret;
    }
    
    // write success, clear and free the msg
    srs_freep(cache->audio);
    
    return ret;
}

int SrsHlsMuxer::flush_video(SrsTsMessageCache* cache)
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
    current->append(cache->video->dts / 90);
    
    if ((ret = current->tscw->write_video(cache->video)) != ERROR_SUCCESS) {
        return ret;
    }
    
    // write success, clear and free the msg
    srs_freep(cache->video);
    
    return ret;
}

int SrsHlsMuxer::segment_close()
{
    int ret = ERROR_SUCCESS;
    
    if (!current) {
        srs_warn("ignore the segment close, for segment is not open.");
        return ret;
    }
    
    // when close current segment, the current segment must not be NULL.
    srs_assert(current);
    
    // valid, add to segments if segment duration is ok
    // when too small, it maybe not enough data to play.
    // when too large, it maybe timestamp corrupt.
    // make the segment more acceptable, when in [min, max_td * 2], it's ok.
    if (current->duration() >= SRS_AUTO_HLS_SEGMENT_MIN_DURATION_MS && (int)current->duration() <= max_td * 2 * 1000) {
        // use async to call the http hooks, for it will cause thread switch.
        if ((ret = async->execute(new SrsDvrAsyncCallOnHls(
            _srs_context->get_id(), req,
            current->fullpath(), current->uri, m3u8, m3u8_url,
            current->sequence_no, current->duration() / 1000.0))) != ERROR_SUCCESS)
        {
            return ret;
        }
        
        // use async to call the http hooks, for it will cause thread switch.
        if ((ret = async->execute(new SrsDvrAsyncCallOnHlsNotify(_srs_context->get_id(), req, current->uri))) != ERROR_SUCCESS) {
            return ret;
        }
        srs_info("Reap ts segment, sequence_no=%d, uri=%s, duration=%"PRId64"ms", current->sequence_no, current->uri.c_str(), current->duration());
        
        // close the muxer of finished segment.
        srs_freep(current->tscw);
        
        // rename from tmp to real path
        if ((ret = current->rename()) != ERROR_SUCCESS) {
            return ret;
        }
        
        segments->append(current);
        current = NULL;
    } else {
        // reuse current segment index.
        _sequence_no--;
        
        srs_trace("Drop ts segment, sequence_no=%d, uri=%s, duration=%"PRId64"ms", current->sequence_no, current->uri.c_str(), current->duration());
        
        // rename from tmp to real path
        if ((ret = current->unlink_tmpfile()) != ERROR_SUCCESS) {
            return ret;
        }
        srs_freep(current);
    }
    
    // shrink the segments.
    segments->shrink(hls_window * 1000);
    
    // refresh the m3u8, donot contains the removed ts
    ret = refresh_m3u8();
    
    // remove the ts file.
    segments->clear_expired(hls_cleanup);
    
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
    
    // no segments, also no m3u8, return.
    if (segments->empty()) {
        return ret;
    }
    
    std::string temp_m3u8 = m3u8 + ".temp";
    if ((ret = _refresh_m3u8(temp_m3u8)) == ERROR_SUCCESS) {
        if (rename(temp_m3u8.c_str(), m3u8.c_str()) < 0) {
            ret = ERROR_HLS_WRITE_FAILED;
            srs_error("rename m3u8 file failed. %s => %s, ret=%d", temp_m3u8.c_str(), m3u8.c_str(), ret);
        }
    }
    
    // remove the temp file.
    if (srs_path_exists(temp_m3u8)) {
        if (unlink(temp_m3u8.c_str()) < 0) {
            srs_warn("ignore remove m3u8 failed, %s", temp_m3u8.c_str());
        }
    }
    
    return ret;
}

int SrsHlsMuxer::_refresh_m3u8(string m3u8_file)
{
    int ret = ERROR_SUCCESS;
    
    // no segments, return.
    if (segments->empty()) {
        return ret;
    }
    
    SrsFileWriter writer;
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
    SrsHlsSegment* first = dynamic_cast<SrsHlsSegment*>(segments->first());
    ss << "#EXT-X-MEDIA-SEQUENCE:" << first->sequence_no << SRS_CONSTS_LF;
    srs_verbose("write m3u8 sequence success.");
    
    // iterator shared for td generation and segemnts wrote.
    std::vector<SrsHlsSegment*>::iterator it;
    
    // #EXT-X-TARGETDURATION:4294967295\n
    /**
     * @see hls-m3u8-draft-pantos-http-live-streaming-12.pdf, page 25
     * The Media Playlist file MUST contain an EXT-X-TARGETDURATION tag.
     * Its value MUST be equal to or greater than the EXTINF duration of any
     * media segment that appears or will appear in the Playlist file,
     * rounded to the nearest integer. Its value MUST NOT change. A
     * typical target duration is 10 seconds.
     */
    // @see https://github.com/ossrs/srs/issues/304#issuecomment-74000081
    int target_duration = (int)ceil(segments->max_duration() / 1000.0);
    target_duration = srs_max(target_duration, max_td);
    
    ss << "#EXT-X-TARGETDURATION:" << target_duration << SRS_CONSTS_LF;
    srs_verbose("write m3u8 duration success.");
    
    // write all segments
    for (int i = 0; i < segments->size(); i++) {
        SrsHlsSegment* segment = dynamic_cast<SrsHlsSegment*>(segments->at(i));
        
        if (segment->is_sequence_header()) {
            // #EXT-X-DISCONTINUITY\n
            ss << "#EXT-X-DISCONTINUITY" << SRS_CONSTS_LF;
            srs_verbose("write m3u8 segment discontinuity success.");
        }
        
        // "#EXTINF:4294967295.208,\n"
        ss.precision(3);
        ss.setf(std::ios::fixed, std::ios::floatfield);
        ss << "#EXTINF:" << segment->duration() / 1000.0 << ", no desc" << SRS_CONSTS_LF;
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
    
    return ret;
}

SrsHlsController::SrsHlsController()
{
    tsmc = new SrsTsMessageCache();
    muxer = new SrsHlsMuxer();
}

SrsHlsController::~SrsHlsController()
{
    srs_freep(muxer);
    srs_freep(tsmc);
}

int SrsHlsController::initialize()
{
    return muxer->initialize();
}

void SrsHlsController::dispose()
{
    muxer->dispose();
}

int SrsHlsController::sequence_no()
{
    return muxer->sequence_no();
}

string SrsHlsController::ts_url()
{
    return muxer->ts_url();
}

double SrsHlsController::duration()
{
    return muxer->duration();
}

int SrsHlsController::deviation()
{
    return muxer->deviation();
}

int SrsHlsController::on_publish(SrsRequest* req)
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
    bool wait_keyframe = _srs_config->get_hls_wait_keyframe(vhost);
    // the audio overflow, for pure audio to reap segment.
    double hls_aof_ratio = _srs_config->get_hls_aof_ratio(vhost);
    // whether use floor(timestamp/hls_fragment) for variable timestamp
    bool ts_floor = _srs_config->get_hls_ts_floor(vhost);
    // the seconds to dispose the hls.
    int hls_dispose = _srs_config->get_hls_dispose(vhost);
    
    // TODO: FIXME: support load exists m3u8, to continue publish stream.
    // for the HLS donot requires the EXT-X-MEDIA-SEQUENCE be monotonically increase.
    
    // open muxer
    if ((ret = muxer->update_config(req, entry_prefix,
        path, m3u8_file, ts_file, hls_fragment, hls_window, ts_floor, hls_aof_ratio,
        cleanup, wait_keyframe)) != ERROR_SUCCESS
    ) {
        srs_error("m3u8 muxer update config failed. ret=%d", ret);
        return ret;
    }
    
    if ((ret = muxer->segment_open()) != ERROR_SUCCESS) {
        srs_error("m3u8 muxer open segment failed. ret=%d", ret);
        return ret;
    }
    srs_trace("hls: win=%.2f, frag=%.2f, prefix=%s, path=%s, m3u8=%s, ts=%s, aof=%.2f, floor=%d, clean=%d, waitk=%d, dispose=%d",
              hls_window, hls_fragment, entry_prefix.c_str(), path.c_str(), m3u8_file.c_str(),
              ts_file.c_str(), hls_aof_ratio, ts_floor, cleanup, wait_keyframe, hls_dispose);
    
    return ret;
}

int SrsHlsController::on_unpublish()
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = muxer->flush_audio(tsmc)) != ERROR_SUCCESS) {
        srs_error("m3u8 muxer flush audio failed. ret=%d", ret);
        return ret;
    }
    
    if ((ret = muxer->segment_close()) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int SrsHlsController::on_sequence_header()
{
    // TODO: support discontinuity for the same stream
    // currently we reap and insert discontinity when encoder republish,
    // but actually, event when stream is not republish, the
    // sequence header may change, for example,
    // ffmpeg ingest a external rtmp stream and push to srs,
    // when the sequence header changed, the stream is not republish.
    return muxer->on_sequence_header();
}

int SrsHlsController::write_audio(SrsAudioFrame* frame, int64_t pts)
{
    int ret = ERROR_SUCCESS;
    
    // write audio to cache.
    if ((ret = tsmc->cache_audio(frame, pts)) != ERROR_SUCCESS) {
        return ret;
    }
    
    // reap when current source is pure audio.
    // it maybe changed when stream info changed,
    // for example, pure audio when start, audio/video when publishing,
    // pure audio again for audio disabled.
    // so we reap event when the audio incoming when segment overflow.
    // @see https://github.com/ossrs/srs/issues/151
    // we use absolutely overflow of segment to make jwplayer/ffplay happy
    // @see https://github.com/ossrs/srs/issues/151#issuecomment-71155184
    if (tsmc->audio && muxer->is_segment_absolutely_overflow()) {
        srs_info("hls: absolute audio reap segment.");
        if ((ret = reap_segment()) != ERROR_SUCCESS) {
            return ret;
        }
    }
    
    // for pure audio, aggregate some frame to one.
    if (muxer->pure_audio() && tsmc->audio) {
        if (pts - tsmc->audio->start_pts < SRS_CONSTS_HLS_PURE_AUDIO_AGGREGATE) {
            return ret;
        }
    }
    
    // directly write the audio frame by frame to ts,
    // it's ok for the hls overload, or maybe cause the audio corrupt,
    // which introduced by aggregate the audios to a big one.
    // @see https://github.com/ossrs/srs/issues/512
    if ((ret = muxer->flush_audio(tsmc)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int SrsHlsController::write_video(SrsVideoFrame* frame, int64_t dts)
{
    int ret = ERROR_SUCCESS;
    
    // write video to cache.
    if ((ret = tsmc->cache_video(frame, dts)) != ERROR_SUCCESS) {
        return ret;
    }
    
    // when segment overflow, reap if possible.
    if (muxer->is_segment_overflow()) {
        // do reap ts if any of:
        //      a. wait keyframe and got keyframe.
        //      b. always reap when not wait keyframe.
        if (!muxer->wait_keyframe() || frame->frame_type == SrsVideoAvcFrameTypeKeyFrame) {
            // reap the segment, which will also flush the video.
            if ((ret = reap_segment()) != ERROR_SUCCESS) {
                return ret;
            }
        }
    }
    
    // flush video when got one
    if ((ret = muxer->flush_video(tsmc)) != ERROR_SUCCESS) {
        srs_error("m3u8 muxer flush video failed. ret=%d", ret);
        return ret;
    }
    
    return ret;
}

int SrsHlsController::reap_segment()
{
    int ret = ERROR_SUCCESS;
    
    // TODO: flush audio before or after segment?
    // TODO: fresh segment begin with audio or video?
    
    // close current ts.
    if ((ret = muxer->segment_close()) != ERROR_SUCCESS) {
        srs_error("m3u8 muxer close segment failed. ret=%d", ret);
        return ret;
    }
    
    // open new ts.
    if ((ret = muxer->segment_open()) != ERROR_SUCCESS) {
        srs_error("m3u8 muxer open segment failed. ret=%d", ret);
        return ret;
    }
    
    // segment open, flush video first.
    if ((ret = muxer->flush_video(tsmc)) != ERROR_SUCCESS) {
        srs_error("m3u8 muxer flush video failed. ret=%d", ret);
        return ret;
    }
    
    // segment open, flush the audio.
    // @see: ngx_rtmp_hls_open_fragment
    /* start fragment with audio to make iPhone happy */
    if ((ret = muxer->flush_audio(tsmc)) != ERROR_SUCCESS) {
        srs_error("m3u8 muxer flush audio failed. ret=%d", ret);
        return ret;
    }
    
    return ret;
}

SrsHls::SrsHls()
{
    req = NULL;
    hub = NULL;
    
    enabled = false;
    disposable = false;
    last_update_time = 0;
    
    jitter = new SrsRtmpJitter();
    controller = new SrsHlsController();
    
    pprint = SrsPithyPrint::create_hls();
}

SrsHls::~SrsHls()
{
    srs_freep(jitter);
    srs_freep(controller);
    srs_freep(pprint);
}

void SrsHls::dispose()
{
    if (enabled) {
        on_unpublish();
    }
    
    controller->dispose();
}

int SrsHls::cycle()
{
    int ret = ERROR_SUCCESS;
    
    if (last_update_time <= 0) {
        last_update_time = srs_get_system_time_ms();
    }
    
    if (!req) {
        return ret;
    }
    
    int hls_dispose = _srs_config->get_hls_dispose(req->vhost) * 1000;
    if (hls_dispose <= 0) {
        return ret;
    }
    if (srs_get_system_time_ms() - last_update_time <= hls_dispose) {
        return ret;
    }
    last_update_time = srs_get_system_time_ms();
    
    if (!disposable) {
        return ret;
    }
    disposable = false;
    
    srs_trace("hls cycle to dispose hls %s, timeout=%dms", req->get_stream_url().c_str(), hls_dispose);
    dispose();
    
    return ret;
}

int SrsHls::initialize(SrsOriginHub* h, SrsRequest* r)
{
    int ret = ERROR_SUCCESS;
    
    hub = h;
    req = r;
    
    if ((ret = controller->initialize()) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int SrsHls::on_publish()
{
    int ret = ERROR_SUCCESS;
    
    // update the hls time, for hls_dispose.
    last_update_time = srs_get_system_time_ms();
    
    // support multiple publish.
    if (enabled) {
        return ret;
    }
    
    if (!_srs_config->get_hls_enabled(req->vhost)) {
        return ret;
    }
    
    if ((ret = controller->on_publish(req)) != ERROR_SUCCESS) {
        return ret;
    }
    
    // if enabled, open the muxer.
    enabled = true;
    
    // ok, the hls can be dispose, or need to be dispose.
    disposable = true;
    
    return ret;
}

void SrsHls::on_unpublish()
{
    int ret = ERROR_SUCCESS;
    
    // support multiple unpublish.
    if (!enabled) {
        return;
    }
    
    if ((ret = controller->on_unpublish()) != ERROR_SUCCESS) {
        srs_error("ignore m3u8 muxer flush/close audio failed. ret=%d", ret);
    }
    
    enabled = false;
}

int SrsHls::on_audio(SrsSharedPtrMessage* shared_audio, SrsFormat* format)
{
    int ret = ERROR_SUCCESS;
    
    if (!enabled) {
        return ret;
    }
    
    // update the hls time, for hls_dispose.
    last_update_time = srs_get_system_time_ms();
    
    SrsSharedPtrMessage* audio = shared_audio->copy();
    SrsAutoFree(SrsSharedPtrMessage, audio);
    
    // ts support audio codec: aac/mp3
    srs_assert(format->acodec);
    SrsAudioCodecId acodec = format->acodec->id;
    if (acodec != SrsAudioCodecIdAAC && acodec != SrsAudioCodecIdMP3) {
        return ret;
    }
    
    // ignore sequence header
    srs_assert(format->audio);
    if (acodec == SrsAudioCodecIdAAC && format->audio->aac_packet_type == SrsAudioAacFrameTraitSequenceHeader) {
        return controller->on_sequence_header();
    }
    
    // TODO: FIXME: config the jitter of HLS.
    if ((ret = jitter->correct(audio, SrsRtmpJitterAlgorithmOFF)) != ERROR_SUCCESS) {
        srs_error("rtmp jitter correct audio failed. ret=%d", ret);
        return ret;
    }
    
    // the dts calc from rtmp/flv header.
    int64_t dts = audio->timestamp * 90;
    
    if ((ret = controller->write_audio(format->audio, dts)) != ERROR_SUCCESS) {
        srs_error("hls cache write audio failed. ret=%d", ret);
        return ret;
    }
    
    return ret;
}

int SrsHls::on_video(SrsSharedPtrMessage* shared_video, SrsFormat* format)
{
    int ret = ERROR_SUCCESS;
    
    if (!enabled) {
        return ret;
    }
    
    // update the hls time, for hls_dispose.
    last_update_time = srs_get_system_time_ms();
    
    SrsSharedPtrMessage* video = shared_video->copy();
    SrsAutoFree(SrsSharedPtrMessage, video);
    
    // ignore info frame,
    // @see https://github.com/ossrs/srs/issues/288#issuecomment-69863909
    srs_assert(format->video);
    if (format->video->frame_type == SrsVideoAvcFrameTypeVideoInfoFrame) {
        return ret;
    }
    
    srs_assert(format->vcodec);
    if (format->vcodec->id != SrsVideoCodecIdAVC) {
        return ret;
    }
    
    // ignore sequence header
    if (format->video->avc_packet_type == SrsVideoAvcFrameTraitSequenceHeader) {
        return controller->on_sequence_header();
    }
    
    // TODO: FIXME: config the jitter of HLS.
    if ((ret = jitter->correct(video, SrsRtmpJitterAlgorithmOFF)) != ERROR_SUCCESS) {
        srs_error("rtmp jitter correct video failed. ret=%d", ret);
        return ret;
    }
    
    int64_t dts = video->timestamp * 90;
    if ((ret = controller->write_video(format->video, dts)) != ERROR_SUCCESS) {
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
    
    if (!pprint->can_print()) {
        return;
    }
    
    // the run time is not equals to stream time,
    // @see: https://github.com/ossrs/srs/issues/81#issuecomment-48100994
    // it's ok.
    srs_trace("-> "SRS_CONSTS_LOG_HLS" time=%"PRId64", sno=%d, ts=%s, dur=%.2f, dva=%dp",
              pprint->age(), controller->sequence_no(), controller->ts_url().c_str(),
              controller->duration(), controller->deviation());
}


