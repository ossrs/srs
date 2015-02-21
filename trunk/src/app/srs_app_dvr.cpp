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

#include <srs_app_dvr.hpp>

#ifdef SRS_AUTO_DVR

#include <fcntl.h>
#include <sstream>
#include <sys/time.h>
using namespace std;

#include <srs_app_config.hpp>
#include <srs_rtmp_sdk.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_app_http_hooks.hpp>
#include <srs_kernel_codec.hpp>
#include <srs_kernel_flv.hpp>
#include <srs_kernel_file.hpp>

SrsFlvSegment::SrsFlvSegment(SrsDvrPlan* p)
{
    req = NULL;
    source = NULL;
    jitter = NULL;
    plan = p;

    fs = new SrsFileWriter();
    enc = new SrsFlvEncoder();
    jitter_algorithm = SrsRtmpJitterAlgorithmOFF;

    path = "";
    has_keyframe = false;
    duration = 0;
    starttime = -1;
    stream_starttime = 0;
    stream_previous_pkt_time = -1;
    stream_duration = 0;

    _srs_config->subscribe(this);
}

SrsFlvSegment::~SrsFlvSegment()
{
    _srs_config->unsubscribe(this);

    srs_freep(jitter);
    srs_freep(fs);
    srs_freep(enc);
}

int SrsFlvSegment::initialize(SrsSource* s, SrsRequest* r)
{
    int ret = ERROR_SUCCESS;

    source = s;
    req = r;
    jitter_algorithm = (SrsRtmpJitterAlgorithm)_srs_config->get_dvr_time_jitter(req->vhost);

    return ret;
}

bool SrsFlvSegment::is_overflow(int64_t max_duration)
{
    return duration >= max_duration;
}

int SrsFlvSegment::open(bool use_tmp_file)
{
    int ret = ERROR_SUCCESS;
    
    // ignore when already open.
    if (fs->is_open()) {
        return ret;
    }

    path = generate_path();
    bool fresh_flv_file = !srs_path_exists(path);
    
    // create dir first.
    std::string dir = path.substr(0, path.rfind("/"));
    if ((ret = srs_create_dir_recursively(dir)) != ERROR_SUCCESS) {
        srs_error("create dir=%s failed. ret=%d", dir.c_str(), ret);
        return ret;
    }
    srs_info("create dir=%s ok", dir.c_str());

    // create jitter.
    if ((ret = create_jitter(!fresh_flv_file)) != ERROR_SUCCESS) {
        srs_error("create jitter failed, path=%s, fresh=%d. ret=%d", path.c_str(), fresh_flv_file, ret);
        return ret;
    }
    
    // generate the tmp flv path.
    if (!fresh_flv_file || !use_tmp_file) {
        // when path exists, always append to it.
        // so we must use the target flv path as output flv.
        tmp_flv_file = path;
    } else {
        // when path not exists, dvr to tmp file.
        tmp_flv_file = path + ".tmp";
    }
    
    // open file writer, in append or create mode.
    if (!fresh_flv_file) {
        if ((ret = fs->open_append(tmp_flv_file)) != ERROR_SUCCESS) {
            srs_error("append file stream for file %s failed. ret=%d", path.c_str(), ret);
            return ret;
        }
        srs_trace("dvr: always append to when exists, file=%s.", path.c_str());
    } else {
        if ((ret = fs->open(tmp_flv_file)) != ERROR_SUCCESS) {
            srs_error("open file stream for file %s failed. ret=%d", path.c_str(), ret);
            return ret;
        }
    }
    
    // when exists, donot write flv header.
    if (fresh_flv_file) {
        // initialize the encoder.
        if ((ret = enc->initialize(fs)) != ERROR_SUCCESS) {
            srs_error("initialize enc by fs for file %s failed. ret=%d", path.c_str(), ret);
            return ret;
        }

        // write the flv header to writer.
        if ((ret = enc->write_header()) != ERROR_SUCCESS) {
            srs_error("write flv header failed. ret=%d", ret);
            return ret;
        }
    }
    
    srs_trace("dvr stream %s to file %s", req->stream.c_str(), path.c_str());

    return ret;
}

int SrsFlvSegment::close()
{
    int ret = ERROR_SUCCESS;
    
    // ignore when already closed.
    if (!fs->is_open()) {
        return ret;
    }
    
    fs->close();
    
    // when tmp flv file exists, reap it.
    if (tmp_flv_file != path) {
        if (rename(tmp_flv_file.c_str(), path.c_str()) < 0) {
            ret = ERROR_SYSTEM_FILE_RENAME;
            srs_error("rename flv file failed, %s => %s. ret=%d", 
                tmp_flv_file.c_str(), path.c_str(), ret);
            return ret;
        }
    }
    
#ifdef SRS_AUTO_HTTP_CALLBACK
    if (_srs_config->get_vhost_http_hooks_enabled(req->vhost)) {
        // HTTP: on_dvr 
        SrsConfDirective* on_dvr = _srs_config->get_vhost_on_dvr(req->vhost);
        if (!on_dvr) {
            srs_info("ignore the empty http callback: on_dvr");
            return ret;
        }
        
        int connection_id = _srs_context->get_id();
        std::string ip = req->ip;
        std::string cwd = _srs_config->cwd();
        std::string file = path;
        for (int i = 0; i < (int)on_dvr->args.size(); i++) {
            std::string url = on_dvr->args.at(i);
            if ((ret = SrsHttpHooks::on_dvr(url, connection_id, ip, req, cwd, file)) != ERROR_SUCCESS) {
                srs_error("hook client on_dvr failed. url=%s, ret=%d", url.c_str(), ret);
                return ret;
            }
        }
    }
#endif

    return ret;
}

int SrsFlvSegment::write_metadata(SrsOnMetaDataPacket* metadata)
{
    int ret = ERROR_SUCCESS;
    
    int size = 0;
    char* payload = NULL;
    if ((ret = metadata->encode(size, payload)) != ERROR_SUCCESS) {
        return ret;
    }
    SrsAutoFree(char, payload);
    
    if ((ret = enc->write_metadata(18, payload, size)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int SrsFlvSegment::write_audio(SrsSharedPtrMessage* __audio)
{
    int ret = ERROR_SUCCESS;

    SrsSharedPtrMessage* audio = __audio->copy();
    SrsAutoFree(SrsSharedPtrMessage, audio);
    
    if ((jitter->correct(audio, 0, 0, jitter_algorithm)) != ERROR_SUCCESS) {
        return ret;
    }
    
    char* payload = audio->payload;
    int size = audio->size;
    int64_t timestamp = plan->filter_timestamp(audio->timestamp);
    if ((ret = enc->write_audio(timestamp, payload, size)) != ERROR_SUCCESS) {
        return ret;
    }

    if ((ret = on_update_duration(audio)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int SrsFlvSegment::write_video(SrsSharedPtrMessage* __video)
{
    int ret = ERROR_SUCCESS;

    SrsSharedPtrMessage* video = __video->copy();
    SrsAutoFree(SrsSharedPtrMessage, video);
    
    char* payload = video->payload;
    int size = video->size;
    
#ifdef SRS_AUTO_HTTP_CALLBACK
    bool is_key_frame = SrsFlvCodec::video_is_h264(payload, size) 
        && SrsFlvCodec::video_is_keyframe(payload, size) 
        && !SrsFlvCodec::video_is_sequence_header(payload, size);
    if (is_key_frame) {
        has_keyframe = true;
        if ((ret = plan->on_video_keyframe()) != ERROR_SUCCESS) {
            return ret;
        }
    }
    srs_verbose("dvr video is key: %d", is_key_frame);
#endif
    
    if ((jitter->correct(video, 0, 0, jitter_algorithm)) != ERROR_SUCCESS) {
        return ret;
    }
    
    // update segment duration, session plan just update the duration,
    // the segment plan will reap segment if exceed, this video will write to next segment.
    if ((ret = on_update_duration(video)) != ERROR_SUCCESS) {
        return ret;
    }
    
    int32_t timestamp = plan->filter_timestamp(video->timestamp);
    if ((ret = enc->write_video(timestamp, payload, size)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

string SrsFlvSegment::generate_path()
{
    // the path in config, for example, 
    //      /data/[vhost]/[app]/[stream]/[2006]/[01]/[02]/[15].[04].[05].[999].flv
    std::string path_config = _srs_config->get_dvr_path(req->vhost);
    
    // add [stream].[timestamp].flv as filename for dir
    if (path_config.find(".flv") != path_config.length() - 4) {
        path_config += "/[stream].[timestamp].flv";
    }
    
    // the flv file path
    std::string flv_path = path_config;
    
    // variable [vhost]
    flv_path = srs_string_replace(flv_path, "[vhost]", req->vhost);
    // variable [app]
    flv_path = srs_string_replace(flv_path, "[app]", req->app);
    // variable [stream]
    flv_path = srs_string_replace(flv_path, "[stream]", req->stream);
    
    // date and time substitude
    // clock time
    timeval tv;
    if (gettimeofday(&tv, NULL) == -1) {
        return flv_path;
    }
    
    // to calendar time
    struct tm* tm;
    if ((tm = localtime(&tv.tv_sec)) == NULL) {
        return flv_path;
    }
    
    // the buffer to format the date and time.
    char buf[64];
    
    // [2006], replace with current year.
    if (true) {
        snprintf(buf, sizeof(buf), "%d", 1900 + tm->tm_year);
        flv_path = srs_string_replace(flv_path, "[2006]", buf);
    }
    // [2006], replace with current year.
    if (true) {
        snprintf(buf, sizeof(buf), "%d", 1900 + tm->tm_year);
        flv_path = srs_string_replace(flv_path, "[2006]", buf);
    }
    // [01], replace this const to current month.
    if (true) {
        snprintf(buf, sizeof(buf), "%d", 1 + tm->tm_mon);
        flv_path = srs_string_replace(flv_path, "[01]", buf);
    }
    // [02], replace this const to current date.
    if (true) {
        snprintf(buf, sizeof(buf), "%d", tm->tm_mday);
        flv_path = srs_string_replace(flv_path, "[02]", buf);
    }
    // [15], replace this const to current hour.
    if (true) {
        snprintf(buf, sizeof(buf), "%d", tm->tm_hour);
        flv_path = srs_string_replace(flv_path, "[15]", buf);
    }
    // [04], repleace this const to current minute.
    if (true) {
        snprintf(buf, sizeof(buf), "%d", tm->tm_min);
        flv_path = srs_string_replace(flv_path, "[04]", buf);
    }
    // [05], repleace this const to current second.
    if (true) {
        snprintf(buf, sizeof(buf), "%d", tm->tm_sec);
        flv_path = srs_string_replace(flv_path, "[05]", buf);
    }
    // [999], repleace this const to current millisecond.
    if (true) {
        snprintf(buf, sizeof(buf), "%03d", (int)(tv.tv_usec / 1000));
        flv_path = srs_string_replace(flv_path, "[999]", buf);
    }
    // [timestamp],replace this const to current UNIX timestamp in ms.
    if (true) {
        int64_t now_us = ((int64_t)tv.tv_sec) * 1000 * 1000 + (int64_t)tv.tv_usec;
        snprintf(buf, sizeof(buf), "%"PRId64, now_us / 1000);
        flv_path = srs_string_replace(flv_path, "[timestamp]", buf);
    }

    return flv_path;
}

int SrsFlvSegment::create_jitter(bool loads_from_flv)
{
    int ret = ERROR_SUCCESS;
    
    // when path exists, use exists jitter.
    if (!loads_from_flv) {
        // jitter when publish, ensure whole stream start from 0.
        srs_freep(jitter);
        jitter = new SrsRtmpJitter();

        // fresh stream starting.
        starttime = -1;
        stream_previous_pkt_time = -1;
        stream_starttime = srs_update_system_time_ms();
        stream_duration = 0;
    
        // fresh segment starting.
        has_keyframe = false;
        duration = 0;

        return ret;
    }

    // when jitter ok, do nothing.
    if (jitter) {
        return ret;
    }

    // always ensure the jitter crote.
    // for the first time, initialize jitter from exists file.
    jitter = new SrsRtmpJitter();

    // TODO: FIXME: implements it.

    return ret;
}

int SrsFlvSegment::on_update_duration(SrsSharedPtrMessage* msg)
{
    int ret = ERROR_SUCCESS;
    
    // we must assumpt that the stream timestamp is monotonically increase,
    // that is, always use time jitter to correct the timestamp.
    // except the time jitter is disabled in config.
    
    // set the segment starttime at first time
    if (starttime < 0) {
        starttime = msg->timestamp;
    }
    
    // no previous packet or timestamp overflow.
    if (stream_previous_pkt_time < 0 || stream_previous_pkt_time > msg->timestamp) {
        stream_previous_pkt_time = msg->timestamp;
    }
    
    // collect segment and stream duration, timestamp overflow is ok.
    duration += msg->timestamp - stream_previous_pkt_time;
    stream_duration += msg->timestamp - stream_previous_pkt_time;
    
    // update previous packet time
    stream_previous_pkt_time = msg->timestamp;
    
    return ret;
}

int SrsFlvSegment::on_reload_vhost_dvr(std::string /*vhost*/)
{
    int ret = ERROR_SUCCESS;
    
    jitter_algorithm = (SrsRtmpJitterAlgorithm)_srs_config->get_dvr_time_jitter(req->vhost);
    
    return ret;
}

SrsDvrPlan::SrsDvrPlan()
{
    source = NULL;
    req = NULL;

    dvr_enabled = false;
    segment = new SrsFlvSegment(this);
}

SrsDvrPlan::~SrsDvrPlan()
{
    srs_freep(segment);
}

int SrsDvrPlan::initialize(SrsSource* s, SrsRequest* r)
{
    int ret = ERROR_SUCCESS;

    source = s;
    req = r;

    if ((ret = segment->initialize(s, r)) != ERROR_SUCCESS) {
        return ret;
    }

    return ret;
}

int SrsDvrPlan::on_dvr_request_sh()
{
    int ret = ERROR_SUCCESS;
    
    // the dvr is enabled, notice the source to push the data.
    if ((ret = source->on_dvr_request_sh()) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int SrsDvrPlan::on_video_keyframe()
{
    return ERROR_SUCCESS;
}

int64_t SrsDvrPlan::filter_timestamp(int64_t timestamp)
{
    return timestamp;
}

int SrsDvrPlan::on_meta_data(SrsOnMetaDataPacket* metadata)
{
    int ret = ERROR_SUCCESS;
    
    if (!dvr_enabled) {
        return ret;
    }
    
    return segment->write_metadata(metadata);
}

int SrsDvrPlan::on_audio(SrsSharedPtrMessage* __audio)
{
    int ret = ERROR_SUCCESS;
    
    if (!dvr_enabled) {
        return ret;
    }

    if ((ret = segment->write_audio(__audio)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int SrsDvrPlan::on_video(SrsSharedPtrMessage* __video)
{
    int ret = ERROR_SUCCESS;
    
    if (!dvr_enabled) {
        return ret;
    }

    if ((ret = segment->write_video(__video)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

SrsDvrPlan* SrsDvrPlan::create_plan(string vhost)
{
    std::string plan = _srs_config->get_dvr_plan(vhost);
    if (plan == SRS_CONF_DEFAULT_DVR_PLAN_SEGMENT) {
        return new SrsDvrSegmentPlan();
    } else if (plan == SRS_CONF_DEFAULT_DVR_PLAN_SESSION) {
        return new SrsDvrSessionPlan();
    } else if (plan == SRS_CONF_DEFAULT_DVR_PLAN_APPEND) {
        return new SrsDvrAppendPlan();
    } else {
        srs_error("invalid dvr plan=%s, vhost=%s", plan.c_str(), vhost.c_str());
        srs_assert(false);
    }
}

SrsDvrSessionPlan::SrsDvrSessionPlan()
{
}

SrsDvrSessionPlan::~SrsDvrSessionPlan()
{
}

int SrsDvrSessionPlan::on_publish()
{
    int ret = ERROR_SUCCESS;
    
    // support multiple publish.
    if (dvr_enabled) {
        return ret;
    }

    if (!_srs_config->get_dvr_enabled(req->vhost)) {
        return ret;
    }

    if ((ret = segment->close()) != ERROR_SUCCESS) {
        return ret;
    }

    if ((ret = segment->open()) != ERROR_SUCCESS) {
        return ret;
    }

    dvr_enabled = true;

    return ret;
}

void SrsDvrSessionPlan::on_unpublish()
{
    // support multiple publish.
    if (!dvr_enabled) {
        return;
    }
    
    // ignore error.
    int ret = segment->close();
    if (ret != ERROR_SUCCESS) {
        srs_warn("ignore flv close error. ret=%d", ret);
    }
    
    dvr_enabled = false;
}

SrsDvrAppendPlan::SrsDvrAppendPlan()
{
}

SrsDvrAppendPlan::~SrsDvrAppendPlan()
{
}

int SrsDvrAppendPlan::on_publish()
{
    int ret = ERROR_SUCCESS;
    
    // support multiple publish.
    if (dvr_enabled) {
        return ret;
    }

    if (!_srs_config->get_dvr_enabled(req->vhost)) {
        return ret;
    }

    if ((ret = segment->open(false)) != ERROR_SUCCESS) {
        return ret;
    }

    dvr_enabled = true;

    return ret;
}

void SrsDvrAppendPlan::on_unpublish()
{
}

SrsDvrSegmentPlan::SrsDvrSegmentPlan()
{
    segment_duration = -1;
    sh_video = sh_audio = NULL;
}

SrsDvrSegmentPlan::~SrsDvrSegmentPlan()
{
    srs_freep(sh_video);
    srs_freep(sh_audio);
}

int SrsDvrSegmentPlan::initialize(SrsSource* source, SrsRequest* req)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = SrsDvrPlan::initialize(source, req)) != ERROR_SUCCESS) {
        return ret;
    }
    
    segment_duration = _srs_config->get_dvr_duration(req->vhost);
    // to ms
    segment_duration *= 1000;
    
    return ret;
}

int SrsDvrSegmentPlan::on_publish()
{
    int ret = ERROR_SUCCESS;
    
    // support multiple publish.
    if (dvr_enabled) {
        return ret;
    }

    if (!_srs_config->get_dvr_enabled(req->vhost)) {
        return ret;
    }

    if ((ret = segment->close()) != ERROR_SUCCESS) {
        return ret;
    }

    if ((ret = segment->open()) != ERROR_SUCCESS) {
        return ret;
    }

    dvr_enabled = true;

    return ret;
}

void SrsDvrSegmentPlan::on_unpublish()
{
}

int SrsDvrSegmentPlan::on_audio(SrsSharedPtrMessage* audio)
{
    int ret = ERROR_SUCCESS;
    
    if (SrsFlvCodec::audio_is_sequence_header(audio->payload, audio->size)) {
        srs_freep(sh_audio);
        sh_audio = audio->copy();
    }

    if ((ret = update_duration(audio)) != ERROR_SUCCESS) {
        return ret;
    }
    
    if ((ret = SrsDvrPlan::on_audio(audio)) != ERROR_SUCCESS) {
        return ret;
    }

    return ret;
}

int SrsDvrSegmentPlan::on_video(SrsSharedPtrMessage* video)
{
    int ret = ERROR_SUCCESS;

    if (SrsFlvCodec::video_is_sequence_header(video->payload, video->size)) {
        srs_freep(sh_video);
        sh_video = video->copy();
    }

    if ((ret = update_duration(video)) != ERROR_SUCCESS) {
        return ret;
    }
    
    if ((ret = SrsDvrPlan::on_video(video)) != ERROR_SUCCESS) {
        return ret;
    }

    return ret;
}

int SrsDvrSegmentPlan::update_duration(SrsSharedPtrMessage* msg)
{
    int ret = ERROR_SUCCESS;
    
    srs_assert(segment);
    
    // ignore if duration ok.
    if (segment_duration <= 0 || !segment->is_overflow(segment_duration)) {
        return ret;
    }
    
    // when wait keyframe, ignore if no frame arrived.
    // @see https://github.com/winlinvip/simple-rtmp-server/issues/177
    if (_srs_config->get_dvr_wait_keyframe(req->vhost)) {
        if (!msg->is_video()) {
            return ret;
        }
        
        char* payload = msg->payload;
        int size = msg->size;
        bool is_key_frame = SrsFlvCodec::video_is_h264(payload, size) 
            && SrsFlvCodec::video_is_keyframe(payload, size) 
            && !SrsFlvCodec::video_is_sequence_header(payload, size);
        if (!is_key_frame) {
            return ret;
        }
    }
    
    // reap segment
    if ((ret = segment->close()) != ERROR_SUCCESS) {
        return ret;
    }
    
    // open new flv file
    if ((ret = segment->open()) != ERROR_SUCCESS) {
        return ret;
    }
    
    // update sequence header
    if (sh_video && (ret = SrsDvrPlan::on_video(sh_video)) != ERROR_SUCCESS) {
        return ret;
    }
    if (sh_audio && (ret = SrsDvrPlan::on_audio(sh_audio)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

SrsDvr::SrsDvr(SrsSource* s)
{
    source = s;
    plan = NULL;
}

SrsDvr::~SrsDvr()
{
    srs_freep(plan);
}

int SrsDvr::initialize(SrsRequest* r)
{
    int ret = ERROR_SUCCESS;
    
    srs_freep(plan);
    plan = SrsDvrPlan::create_plan(r->vhost);

    if ((ret = plan->initialize(source, r)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int SrsDvr::on_publish(SrsRequest* /*r*/)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = plan->on_publish()) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

void SrsDvr::on_unpublish()
{
    plan->on_unpublish();
}

int SrsDvr::on_meta_data(SrsOnMetaDataPacket* m)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = plan->on_meta_data(m)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int SrsDvr::on_audio(SrsSharedPtrMessage* __audio)
{
    return plan->on_audio(__audio);
}

int SrsDvr::on_video(SrsSharedPtrMessage* __video)
{
    return plan->on_video(__video);
}

#endif


