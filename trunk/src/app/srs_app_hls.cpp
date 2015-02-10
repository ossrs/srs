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

#include <algorithm>
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

// drop the segment when duration of ts too small.
#define SRS_AUTO_HLS_SEGMENT_MIN_DURATION_MS 100

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

SrsHlsSegment::SrsHlsSegment(bool write_cache, bool write_file)
{
    duration = 0;
    sequence_no = 0;
    segment_start_dts = 0;
    is_sequence_header = false;
    writer = new SrsHlsCacheWriter(write_cache, write_file);
    muxer = new SrsTSMuxer(writer);
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

SrsHlsMuxer::SrsHlsMuxer(ISrsHlsHandler* h)
{
    req = NULL;
    handler = h;
    hls_fragment = hls_window = 0;
    _sequence_no = 0;
    current = NULL;
    acodec = SrsCodecAudioReserved1;
    should_write_cache = false;
    should_write_file = true;
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
}

int SrsHlsMuxer::sequence_no()
{
    return _sequence_no;
}

int SrsHlsMuxer::update_config(SrsRequest* r, string path, int fragment, int window) 
{
    int ret = ERROR_SUCCESS;
    
    srs_freep(req);
    req = r->copy();

    hls_path = path;
    hls_fragment = fragment;
    hls_window = window;

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
    
    // TODO: create all parents dirs.
    // create dir for app.
    if (should_write_file && (ret = create_dir()) != ERROR_SUCCESS) {
        return ret;
    }
    
    // when segment open, the current segment must be NULL.
    srs_assert(!current);
    
    // new segment.
    current = new SrsHlsSegment(should_write_cache, should_write_file);
    current->sequence_no = _sequence_no++;
    current->segment_start_dts = segment_start_dts;
    
    // generate filename.
    char filename[128];
    snprintf(filename, sizeof(filename), 
        "%s-%d.ts", req->stream.c_str(), current->sequence_no);
    
    // TODO: use temp file and rename it.
    current->full_path = hls_path;
    current->full_path += "/";
    current->full_path += req->app;
    current->full_path += "/";
    current->full_path += filename;
    
    // TODO: support base url, and so on.
    current->uri = filename;
    
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
    return current->duration >= hls_fragment;
}

bool SrsHlsMuxer::is_segment_absolutely_overflow()
{
    srs_assert(current);
    return current->duration >= 2 * hls_fragment;
}

int SrsHlsMuxer::update_acodec(SrsCodecAudio ac)
{
    srs_assert(current);
    srs_assert(current->muxer);
    acodec = ac;
    return current->muxer->update_acodec(ac);
}

int SrsHlsMuxer::flush_audio(SrsMpegtsFrame* af, SrsSimpleBuffer* ab)
{
    int ret = ERROR_SUCCESS;

    // if flushed yet, ignore.
    if (ab->length() == 0) {
        srs_info("ignore hls segment audio flushed yet.");
        return ret;
    }

    // if current is NULL, segment is not open, ignore the flush event.
    if (!current) {
        srs_warn("flush audio ignored, for segment is not open.");
        return ret;
    }
    
    if (ab->length() <= 0) {
        return ret;
    }
    
    // update the duration of segment.
    current->update_duration(af->pts);
    
    if ((ret = current->muxer->write_audio(af, ab)) != ERROR_SUCCESS) {
        return ret;
    }
    
    // write success, clear and free the buffer
    ab->erase(ab->length());

    return ret;
}

int SrsHlsMuxer::flush_video(SrsMpegtsFrame* /*af*/, SrsSimpleBuffer* /*ab*/, SrsMpegtsFrame* vf, SrsSimpleBuffer* vb)
{
    int ret = ERROR_SUCCESS;

    // if flushed yet, ignore.
    if (vb->length() == 0) {
        srs_info("ignore hls segment video flushed yet.");
        return ret;
    }

    // if current is NULL, segment is not open, ignore the flush event.
    if (!current) {
        srs_warn("flush video ignored, for segment is not open.");
        return ret;
    }
    
    srs_assert(current);
    
    // update the duration of segment.
    current->update_duration(vf->dts);
    
    if ((ret = current->muxer->write_video(vf, vb)) != ERROR_SUCCESS) {
        return ret;
    }
    
    // write success, clear and free the buffer
    vb->erase(vb->length());
    
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
    
        srs_info("%s reap ts segment, sequence_no=%d, uri=%s, duration=%.2f, start=%"PRId64"",
            log_desc.c_str(), current->sequence_no, current->uri.c_str(), current->duration, 
            current->segment_start_dts);
        
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
    for (int i = segments.size() - 1; i >= 0; i--) {
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
        unlink(segment->full_path.c_str());
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
    
    std::string m3u8_file = hls_path;
    m3u8_file += "/";
    m3u8_file += req->app;
    m3u8_file += "/";
    m3u8_file += req->stream;
    m3u8_file += ".m3u8";
    
    m3u8 = m3u8_file;
    m3u8_file += ".temp";
    
    if ((ret = _refresh_m3u8(m3u8_file)) == ERROR_SUCCESS) {
        if (should_write_file && rename(m3u8_file.c_str(), m3u8.c_str()) < 0) {
            ret = ERROR_HLS_WRITE_FAILED;
            srs_error("rename m3u8 file failed. %s => %s, ret=%d", m3u8_file.c_str(), m3u8.c_str(), ret);
        }
    }
    
    // remove the temp file.
    unlink(m3u8_file.c_str());
    
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
    
    // #EXTM3U\n#EXT-X-VERSION:3\n
    char header[] = {
        // #EXTM3U\n
        0x23, 0x45, 0x58, 0x54, 0x4d, 0x33, 0x55, SRS_CONSTS_LF, 
        // #EXT-X-VERSION:3\n
        0x23, 0x45, 0x58, 0x54, 0x2d, 0x58, 0x2d, 0x56, 0x45, 0x52, 
        0x53, 0x49, 0x4f, 0x4e, 0x3a, 0x33, SRS_CONSTS_LF,
        // #EXT-X-ALLOW-CACHE:NO\n
        0x23, 0x45, 0x58, 0x54, 0x2d, 0x58, 0x2d, 0x41, 0x4c, 0x4c, 
        0x4f, 0x57, 0x2d, 0x43, 0x41, 0x43, 0x48, 0x45, 0x3a, 0x4e, 0x4f, SRS_CONSTS_LF
    };
    if ((ret = writer.write(header, sizeof(header), NULL)) != ERROR_SUCCESS) {
        srs_error("write m3u8 header failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("write m3u8 header success.");
    
    // #EXT-X-MEDIA-SEQUENCE:4294967295\n
    SrsHlsSegment* first = *segments.begin();
    char sequence[34] = {};
    int len = snprintf(sequence, sizeof(sequence), "#EXT-X-MEDIA-SEQUENCE:%d%c", first->sequence_no, SRS_CONSTS_LF);
    if ((ret = writer.write(sequence, len, NULL)) != ERROR_SUCCESS) {
        srs_error("write m3u8 sequence failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("write m3u8 sequence success.");
    
    // #EXT-X-TARGETDURATION:4294967295\n
    int target_duration = 0;
    /**
    * @see hls-m3u8-draft-pantos-http-live-streaming-12.pdf, page 25
    * The Media Playlist file MUST contain an EXT-X-TARGETDURATION tag.
    * Its value MUST be equal to or greater than the EXTINF duration of any
    * media segment that appears or will appear in the Playlist file,
    * rounded to the nearest integer. Its value MUST NOT change. A
    * typical target duration is 10 seconds.
    */
    // TODO: FIXME: finger it out whether it should not changed.
    std::vector<SrsHlsSegment*>::iterator it;
    for (it = segments.begin(); it != segments.end(); ++it) {
        SrsHlsSegment* segment = *it;
        target_duration = srs_max(target_duration, (int)segment->duration);
    }
    // TODO: maybe need to take an around value
    target_duration += 1;
    char duration[34]; // 23+10+1
    len = snprintf(duration, sizeof(duration), "#EXT-X-TARGETDURATION:%d%c", target_duration, SRS_CONSTS_LF);
    if ((ret = writer.write(duration, len, NULL)) != ERROR_SUCCESS) {
        srs_error("write m3u8 duration failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("write m3u8 duration success.");
    
    // write all segments
    for (it = segments.begin(); it != segments.end(); ++it) {
        SrsHlsSegment* segment = *it;
        
        if (segment->is_sequence_header) {
            // #EXT-X-DISCONTINUITY\n
            char ext_discon[22]; // 21+1
            len = snprintf(ext_discon, sizeof(ext_discon), "#EXT-X-DISCONTINUITY%c", SRS_CONSTS_LF);
            if ((ret = writer.write(ext_discon, len, NULL)) != ERROR_SUCCESS) {
                srs_error("write m3u8 segment discontinuity failed. ret=%d", ret);
                return ret;
            }
            srs_verbose("write m3u8 segment discontinuity success.");
        }
        
        // "#EXTINF:4294967295.208,\n"
        char ext_info[25]; // 14+10+1
        len = snprintf(ext_info, sizeof(ext_info), "#EXTINF:%.3f,%c", segment->duration, SRS_CONSTS_LF);
        if ((ret = writer.write(ext_info, len, NULL)) != ERROR_SUCCESS) {
            srs_error("write m3u8 segment info failed. ret=%d", ret);
            return ret;
        }
        srs_verbose("write m3u8 segment info success.");
        
        // {file name}\n
        std::string filename = segment->uri;
        filename += SRS_CONSTS_LF;
        if ((ret = writer.write((char*)filename.c_str(), (int)filename.length(), NULL)) != ERROR_SUCCESS) {
            srs_error("write m3u8 segment uri failed. ret=%d", ret);
            return ret;
        }
        srs_verbose("write m3u8 segment uri success.");
    }
    srs_info("write m3u8 %s success.", m3u8_file.c_str());

    // notify handler for update m3u8.
    if (handler && (ret = handler->on_update_m3u8(req, writer.cache())) != ERROR_SUCCESS) {
        srs_error("notify handler for update m3u8 failed. ret=%d", ret);
        return ret;
    }
    
    return ret;
}

int SrsHlsMuxer::create_dir()
{
    int ret = ERROR_SUCCESS;

    if (!should_write_file) {
        return ret;
    }
    
    std::string app_dir = hls_path;
    app_dir += "/";
    app_dir += req->app;
    
    // TODO: cleanup the dir when startup.
    
    if ((ret = srs_create_dir_recursively(app_dir)) != ERROR_SUCCESS) {
        srs_error("create app dir %s failed. ret=%d", app_dir.c_str(), ret);
        return ret;
    }
    srs_info("create app dir %s ok", app_dir.c_str());

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
    
    int hls_fragment = (int)_srs_config->get_hls_fragment(vhost);
    int hls_window = (int)_srs_config->get_hls_window(vhost);
    
    // get the hls path config
    std::string hls_path = _srs_config->get_hls_path(vhost);
    
    // TODO: FIXME: support load exists m3u8, to continue publish stream.
    // for the HLS donot requires the EXT-X-MEDIA-SEQUENCE be monotonically increase.
    
    // open muxer
    if ((ret = muxer->update_config(req, hls_path, hls_fragment, hls_window)) != ERROR_SUCCESS) {
        srs_error("m3u8 muxer update config failed. ret=%d", ret);
        return ret;
    }
    
    if ((ret = muxer->segment_open(segment_start_dts)) != ERROR_SUCCESS) {
        srs_error("m3u8 muxer open segment failed. ret=%d", ret);
        return ret;
    }
    
    return ret;
}

int SrsHlsCache::on_unpublish(SrsHlsMuxer* muxer)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = muxer->flush_audio(cache->af, cache->ab)) != ERROR_SUCCESS) {
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
    if (cache->ab->length() > SRS_AUTO_HLS_AUDIO_CACHE_SIZE) {
        if ((ret = muxer->flush_audio(cache->af, cache->ab)) != ERROR_SUCCESS) {
            return ret;
        }
    }

    // TODO: config it.
    // in ms, audio delay to flush the audios.
    int64_t audio_delay = SRS_CONF_DEFAULT_AAC_DELAY;
    // flush if audio delay exceed
    if (pts - cache->audio_buffer_start_pts > audio_delay * 90) {
        if ((ret = muxer->flush_audio(cache->af, cache->ab)) != ERROR_SUCCESS) {
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
    if (muxer->is_segment_absolutely_overflow()) {
        if ((ret = reap_segment("audio", muxer, cache->af->pts)) != ERROR_SUCCESS) {
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
        if ((ret = reap_segment("video", muxer, cache->vf->dts)) != ERROR_SUCCESS) {
            return ret;
        }
    }
    
    // flush video when got one
    if ((ret = muxer->flush_video(cache->af, cache->ab, cache->vf, cache->vb)) != ERROR_SUCCESS) {
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
    if ((ret = muxer->flush_video(cache->af, cache->ab, cache->vf, cache->vb)) != ERROR_SUCCESS) {
        srs_error("m3u8 muxer flush video failed. ret=%d", ret);
        return ret;
    }

    // segment open, flush the audio.
    // @see: ngx_rtmp_hls_open_fragment
    /* start fragment with audio to make iPhone happy */
    if ((ret = muxer->flush_audio(cache->af, cache->ab)) != ERROR_SUCCESS) {
        srs_error("m3u8 muxer flush audio failed. ret=%d", ret);
        return ret;
    }
    
    return ret;
}

SrsHls::SrsHls(SrsSource* s, ISrsHlsHandler* h)
{
    source = s;
    handler = h;
    
    hls_enabled = false;

    codec = new SrsAvcAacCodec();
    sample = new SrsCodecSample();
    jitter = new SrsRtmpJitter();
    
    muxer = new SrsHlsMuxer(h);
    hls_cache = new SrsHlsCache();

    pithy_print = new SrsPithyPrint(SRS_CONSTS_STAGE_HLS);
    stream_dts = 0;
}

SrsHls::~SrsHls()
{
    srs_freep(codec);
    srs_freep(sample);
    srs_freep(jitter);
    
    srs_freep(muxer);
    srs_freep(hls_cache);
    
    srs_freep(pithy_print);
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

int SrsHls::on_audio(SrsSharedPtrMessage* __audio)
{
    int ret = ERROR_SUCCESS;
    
    if (!hls_enabled) {
        return ret;
    }

    SrsSharedPtrMessage* audio = __audio->copy();
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

int SrsHls::on_video(SrsSharedPtrMessage* __video)
{
    int ret = ERROR_SUCCESS;
    
    if (!hls_enabled) {
        return ret;
    }

    SrsSharedPtrMessage* video = __video->copy();
    SrsAutoFree(SrsSharedPtrMessage, video);
    
    sample->clear();
    if ((ret = codec->video_avc_demux(video->payload, video->size, sample)) != ERROR_SUCCESS) {
        srs_error("hls codec demux video failed. ret=%d", ret);
        return ret;
    }
    
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
    
    hls_mux();
    
    return ret;
}

void SrsHls::hls_mux()
{
    // reportable
    if (pithy_print->can_print()) {
        // the run time is not equals to stream time,
        // @see: https://github.com/winlinvip/simple-rtmp-server/issues/81#issuecomment-48100994
        // it's ok.
        srs_trace("-> "SRS_CONSTS_LOG_HLS
            " time=%"PRId64", stream dts=%"PRId64"(%"PRId64"ms), sequence_no=%d", 
            pithy_print->age(), stream_dts, stream_dts / 90, muxer->sequence_no());
    }
    
    pithy_print->elapse();
}

#endif


