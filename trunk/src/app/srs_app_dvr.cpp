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
#include <algorithm>
using namespace std;

#include <srs_app_config.hpp>
#include <srs_rtmp_sdk.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_app_http_hooks.hpp>
#include <srs_kernel_codec.hpp>
#include <srs_kernel_flv.hpp>
#include <srs_kernel_file.hpp>
#include <srs_rtmp_amf0.hpp>
#include <srs_kernel_stream.hpp>
#include <srs_app_json.hpp>

// update the flv duration and filesize every this interval in ms.
#define __SRS_DVR_UPDATE_DURATION_INTERVAL 60000

// the sleep interval for http async callback.
#define SRS_AUTO_ASYNC_CALLBACL_SLEEP_US 300000

// the use raction for dvr rpc.
#define SRS_DVR_USER_ACTION_REAP_SEGMENT "reap_segment"

SrsFlvSegment::SrsFlvSegment(SrsDvrPlan* p)
{
    req = NULL;
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

    duration_offset = 0;
    filesize_offset = 0;

    _srs_config->subscribe(this);
}

SrsFlvSegment::~SrsFlvSegment()
{
    _srs_config->unsubscribe(this);

    srs_freep(jitter);
    srs_freep(fs);
    srs_freep(enc);
}

int SrsFlvSegment::initialize(SrsRequest* r)
{
    int ret = ERROR_SUCCESS;

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

    // initialize the encoder.
    if ((ret = enc->initialize(fs)) != ERROR_SUCCESS) {
        srs_error("initialize enc by fs for file %s failed. ret=%d", path.c_str(), ret);
        return ret;
    }
    
    // when exists, donot write flv header.
    if (fresh_flv_file) {
        // write the flv header to writer.
        if ((ret = enc->write_header()) != ERROR_SUCCESS) {
            srs_error("write flv header failed. ret=%d", ret);
            return ret;
        }
    }

    // update the duration and filesize offset.
    duration_offset = 0;
    filesize_offset = 0;
    
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

    // update duration and filesize.
    if ((ret = update_flv_metadata()) != ERROR_SUCCESS) {
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

    // TODO: FIXME: the http callback is async, which will trigger thread switch,
    //          so the on_video maybe invoked during the http callback, and error.
    if ((ret = plan->on_reap_segment()) != ERROR_SUCCESS) {
        srs_error("dvr: notify plan to reap segment failed. ret=%d", ret);
        return ret;
    }

    return ret;
}

int SrsFlvSegment::write_metadata(SrsSharedPtrMessage* metadata)
{
    int ret = ERROR_SUCCESS;

    if (duration_offset || filesize_offset) {
        return ret;
    }

    SrsStream stream;
    if ((ret = stream.initialize(metadata->payload, metadata->size)) != ERROR_SUCCESS) {
        return ret;
    }

    SrsAmf0Any* name = SrsAmf0Any::str();
    SrsAutoFree(SrsAmf0Any, name);
    if ((ret = name->read(&stream)) != ERROR_SUCCESS) {
        return ret;
    }

    SrsAmf0Object* obj = SrsAmf0Any::object();
    SrsAutoFree(SrsAmf0Object, obj);
    if ((ret = obj->read(&stream)) != ERROR_SUCCESS) {
        return ret;
    }

    // remove duration and filesize.
    obj->set("filesize", NULL);
    obj->set("duration", NULL);

    // add properties.
    obj->set("service", SrsAmf0Any::str(RTMP_SIG_SRS_SERVER));
    obj->set("filesize", SrsAmf0Any::number(0));
    obj->set("duration", SrsAmf0Any::number(0));
    
    int size = name->total_size() + obj->total_size();
    char* payload = new char[size];
    SrsAutoFree(char, payload);

    // 11B flv header, 3B object EOF, 8B number value, 1B number flag.
    duration_offset = fs->tellg() + size + 11 - SrsAmf0Size::object_eof() - SrsAmf0Size::number();
    // 2B string flag, 8B number value, 8B string 'duration', 1B number flag
    filesize_offset = duration_offset - SrsAmf0Size::utf8("duration") - SrsAmf0Size::number();

    // convert metadata to bytes.
    if ((ret = stream.initialize(payload, size)) != ERROR_SUCCESS) {
        return ret;
    }
    if ((ret = name->write(&stream)) != ERROR_SUCCESS) {
        return ret;
    }
    if ((ret = obj->write(&stream)) != ERROR_SUCCESS) {
        return ret;
    }
    
    // to flv file.
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

int SrsFlvSegment::update_flv_metadata()
{
    int ret = ERROR_SUCCESS;

    // no duration or filesize specified.
    if (!duration_offset || !filesize_offset) {
        return ret;
    }

    int64_t cur = fs->tellg();

    // buffer to write the size.
    char* buf = new char[SrsAmf0Size::number()];
    SrsAutoFree(char, buf);

    SrsStream stream;
    if ((ret = stream.initialize(buf, SrsAmf0Size::number())) != ERROR_SUCCESS) {
        return ret;
    }

    // filesize to buf.
    SrsAmf0Any* size = SrsAmf0Any::number((double)cur);
    SrsAutoFree(SrsAmf0Any, size);

    stream.skip(-1 * stream.pos());
    if ((ret = size->write(&stream)) != ERROR_SUCCESS) {
        return ret;
    }

    // update the flesize.
    fs->lseek(filesize_offset);
    if ((ret = fs->write(buf, SrsAmf0Size::number(), NULL)) != ERROR_SUCCESS) {
        return ret;
    }
    
    // duration to buf
    SrsAmf0Any* dur = SrsAmf0Any::number((double)duration / 1000.0);
    SrsAutoFree(SrsAmf0Any, dur);
    
    stream.skip(-1 * stream.pos());
    if ((ret = dur->write(&stream)) != ERROR_SUCCESS) {
        return ret;
    }

    // update the duration
    fs->lseek(duration_offset);
    if ((ret = fs->write(buf, SrsAmf0Size::number(), NULL)) != ERROR_SUCCESS) {
        return ret;
    }

    // reset the offset.
    fs->lseek(cur);

    return ret;
}

string SrsFlvSegment::get_path()
{
    return path;
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

ISrsDvrAsyncCall::ISrsDvrAsyncCall()
{
}

ISrsDvrAsyncCall::~ISrsDvrAsyncCall()
{
}

SrsDvrAsyncCallOnDvr::SrsDvrAsyncCallOnDvr(SrsRequest* r, string p)
{
    req = r;
    path = p;
}

SrsDvrAsyncCallOnDvr::~SrsDvrAsyncCallOnDvr()
{
}

int SrsDvrAsyncCallOnDvr::call()
{
    int ret = ERROR_SUCCESS;
    
#ifdef SRS_AUTO_HTTP_CALLBACK
    // http callback for on_dvr in config.
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

string SrsDvrAsyncCallOnDvr::to_string()
{
    std::stringstream ss;
    ss << "vhost=" << req->vhost << ", file=" << path;
    return ss.str();
}

SrsDvrAsyncCallThread::SrsDvrAsyncCallThread()
{
    pthread = new SrsThread("async", this, SRS_AUTO_ASYNC_CALLBACL_SLEEP_US, true);
}

SrsDvrAsyncCallThread::~SrsDvrAsyncCallThread()
{
    stop();
    srs_freep(pthread);

    std::vector<ISrsDvrAsyncCall*>::iterator it;
    for (it = callbacks.begin(); it != callbacks.end(); ++it) {
        ISrsDvrAsyncCall* call = *it;
        srs_freep(call);
    }
    callbacks.clear();
}

int SrsDvrAsyncCallThread::call(ISrsDvrAsyncCall* c)
{
    int ret = ERROR_SUCCESS;

    callbacks.push_back(c);

    return ret;
}

int SrsDvrAsyncCallThread::start()
{
    return pthread->start();
}

void SrsDvrAsyncCallThread::stop()
{
    pthread->stop();
}

int SrsDvrAsyncCallThread::cycle()
{
    int ret = ERROR_SUCCESS;
    
    std::vector<ISrsDvrAsyncCall*> copies = callbacks;
    callbacks.clear();

    std::vector<ISrsDvrAsyncCall*>::iterator it;
    for (it = copies.begin(); it != copies.end(); ++it) {
        ISrsDvrAsyncCall* call = *it;
        if ((ret = call->call()) != ERROR_SUCCESS) {
            srs_warn("dvr: ignore callback %s, ret=%d", call->to_string().c_str(), ret);
        }
        srs_freep(call);
    }

    return ret;
}

SrsDvrPlan::SrsDvrPlan()
{
    req = NULL;

    dvr_enabled = false;
    segment = new SrsFlvSegment(this);
    async = new SrsDvrAsyncCallThread();
}

SrsDvrPlan::~SrsDvrPlan()
{
    srs_freep(segment);
    srs_freep(async);
}

int SrsDvrPlan::initialize(SrsRequest* r)
{
    int ret = ERROR_SUCCESS;
    
    req = r;

    if ((ret = segment->initialize(r)) != ERROR_SUCCESS) {
        return ret;
    }

    if ((ret = async->start()) != ERROR_SUCCESS) {
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

int SrsDvrPlan::on_meta_data(SrsSharedPtrMessage* __metadata)
{
    int ret = ERROR_SUCCESS;
    
    if (!dvr_enabled) {
        return ret;
    }
    
    return segment->write_metadata(__metadata);
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

int SrsDvrPlan::on_reap_segment()
{
    int ret = ERROR_SUCCESS;

    if ((ret = async->call(new SrsDvrAsyncCallOnDvr(req, segment->get_path()))) != ERROR_SUCCESS) {
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
    } else if (plan == SRS_CONF_DEFAULT_DVR_PLAN_API) {
        /**
        * @remark the api plan maybe create by publish event or http api post create dvr event.
        *       so when we got from pool first when create it.
        */
        SrsApiDvrPool* pool = SrsApiDvrPool::instance();
        SrsDvrApiPlan* plan = pool->get_dvr(vhost);
        if (plan) {
            return plan;
        }
        return new SrsDvrApiPlan();
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

SrsDvrApiPlan::SrsDvrApiPlan()
{
    autostart = false;
    started = false;

    metadata = sh_audio = sh_video = NULL;
}

SrsDvrApiPlan::~SrsDvrApiPlan()
{
    SrsApiDvrPool* pool = SrsApiDvrPool::instance();
    pool->detach_dvr(this);

    srs_freep(metadata);
    srs_freep(sh_audio);
    srs_freep(sh_video);
}

int SrsDvrApiPlan::initialize(SrsRequest* r)
{
    int ret = ERROR_SUCCESS;

    if ((ret = SrsDvrPlan::initialize(r)) != ERROR_SUCCESS) {
        return ret;
    }

    SrsApiDvrPool* pool = SrsApiDvrPool::instance();
    if ((ret = pool->add_dvr(this)) != ERROR_SUCCESS) {
        return ret;
    }

    autostart = _srs_config->get_dvr_autostart(r->vhost);

    return ret;
}

int SrsDvrApiPlan::on_publish()
{
    int ret = ERROR_SUCCESS;
    
    // support multiple publish.
    if (dvr_enabled) {
        return ret;
    }

    if (!_srs_config->get_dvr_enabled(req->vhost)) {
        return ret;
    }

    // api disabled dvr when not autostart.
    bool autostart = _srs_config->get_dvr_autostart(req->vhost);
    if (!autostart && !started) {
        srs_warn("dvr: api not start and disabled for not autostart.");
        return ret;
    }

    dvr_enabled = true;

    if ((ret = segment->close()) != ERROR_SUCCESS) {
        return ret;
    }

    if ((ret = segment->open()) != ERROR_SUCCESS) {
        return ret;
    }
    
    // update sequence header
    if (metadata && (ret = SrsDvrPlan::on_meta_data(metadata)) != ERROR_SUCCESS) {
        return ret;
    }
    if (sh_video && (ret = SrsDvrPlan::on_video(sh_video)) != ERROR_SUCCESS) {
        return ret;
    }
    if (sh_audio && (ret = SrsDvrPlan::on_audio(sh_audio)) != ERROR_SUCCESS) {
        return ret;
    }

    return ret;
}

void SrsDvrApiPlan::on_unpublish()
{
}

int SrsDvrApiPlan::on_meta_data(SrsSharedPtrMessage* __metadata)
{
    int ret = ERROR_SUCCESS;

    srs_freep(metadata);
    metadata = __metadata->copy();

    return ret;
}

int SrsDvrApiPlan::on_audio(SrsSharedPtrMessage* __audio)
{
    int ret = ERROR_SUCCESS;
    
    if (SrsFlvCodec::audio_is_sequence_header(__audio->payload, __audio->size)) {
        srs_freep(sh_audio);
        sh_audio = __audio->copy();
    }
    
    if ((ret = SrsDvrPlan::on_audio(__audio)) != ERROR_SUCCESS) {
        return ret;
    }

    return ret;
}

int SrsDvrApiPlan::on_video(SrsSharedPtrMessage* __video)
{
    int ret = ERROR_SUCCESS;

    if (SrsFlvCodec::video_is_sequence_header(__video->payload, __video->size)) {
        srs_freep(sh_video);
        sh_video = __video->copy();
    }
    
    if ((ret = check_user_actions(__video)) != ERROR_SUCCESS) {
        return ret;
    }
    
    if ((ret = SrsDvrPlan::on_video(__video)) != ERROR_SUCCESS) {
        return ret;
    }

    return ret;
}

int SrsDvrApiPlan::set_plan()
{
    _srs_config->set_dvr_plan(req->vhost, SRS_CONF_DEFAULT_DVR_PLAN_API);
    return ERROR_SUCCESS;
}

int SrsDvrApiPlan::set_path_tmpl(string path_tmpl)
{
    _srs_config->set_dvr_path(req->vhost, path_tmpl);
    return ERROR_SUCCESS;
}

int SrsDvrApiPlan::set_callback(string value)
{
    _srs_config->set_vhost_http_hooks_enabled(req->vhost, true);
    _srs_config->set_vhost_on_dvr(req->vhost, value);
    return ERROR_SUCCESS;
}

int SrsDvrApiPlan::set_wait_keyframe(bool wait_keyframe)
{
    _srs_config->set_dvr_wait_keyframe(req->vhost, wait_keyframe);
    return ERROR_SUCCESS;
}

int SrsDvrApiPlan::start()
{
    int ret = ERROR_SUCCESS;

    if (started) {
        return ret;
    }

    // enable the config.
    _srs_config->set_dvr_enabled(req->vhost, true);

    // stop dvr
    if (dvr_enabled) {
        // ignore error.
        int ret = segment->close();
        if (ret != ERROR_SUCCESS) {
            srs_warn("ignore flv close error. ret=%d", ret);
        }
    
        dvr_enabled = false;
    }

    // start dvr
    if ((ret = on_publish()) != ERROR_SUCCESS) {
        return ret;
    }

    started = true;
    return ret;
}

int SrsDvrApiPlan::dumps(stringstream& ss)
{
    int ret = ERROR_SUCCESS;

    bool wait_keyframe = _srs_config->get_dvr_wait_keyframe(req->vhost);
    std::string path_template = _srs_config->get_dvr_path(req->vhost);
    SrsConfDirective* callbacks = _srs_config->get_vhost_on_dvr(req->vhost);

    ss << __SRS_JOBJECT_START
            << __SRS_JFIELD_STR("path_tmpl", path_template) << __SRS_JFIELD_CONT
            << __SRS_JFIELD_STR("path_dvr", segment->get_path()) << __SRS_JFIELD_CONT
            << __SRS_JFIELD_BOOL("wait_keyframe", wait_keyframe) << __SRS_JFIELD_CONT
            << __SRS_JFIELD_STR("vhost", req->vhost) << __SRS_JFIELD_CONT
            << __SRS_JFIELD_STR("app", req->app) << __SRS_JFIELD_CONT
            << __SRS_JFIELD_STR("stream", req->stream) << __SRS_JFIELD_CONT
            << __SRS_JFIELD_STR("callback", callbacks->arg0()) << __SRS_JFIELD_CONT
            << __SRS_JFIELD_STR("status", (dvr_enabled? "start":"stop"))
        << __SRS_JOBJECT_END;

    return ret;
}

int SrsDvrApiPlan::stop()
{
    int ret = ERROR_SUCCESS;

    _srs_config->set_dvr_enabled(req->vhost, false);
    started = false;

    // stop dvr
    if (dvr_enabled) {
        // ignore error.
        int ret = segment->close();
        if (ret != ERROR_SUCCESS) {
            srs_warn("ignore flv close error. ret=%d", ret);
        }
    
        dvr_enabled = false;
    }

    srs_trace("dvr: stop dvr of vhost=%s", req->vhost.c_str());

    return ret;
}

int SrsDvrApiPlan::rpc(SrsJsonObject* obj)
{
    int ret = ERROR_SUCCESS;

    SrsJsonAny* prop = NULL;
    if ((prop = obj->ensure_property_string("action")) == NULL) {
        ret = ERROR_HTTP_DVR_REQUEST;
        srs_error("dvr: rpc required action request. ret=%d", ret);
        return ret;
    }

    action = prop->to_str();
    if (action == SRS_DVR_USER_ACTION_REAP_SEGMENT) {
        if ((prop = obj->ensure_property_string("path_tmpl")) != NULL) {
            path_template = prop->to_str();
        }
    } else {
        ret = ERROR_HTTP_DVR_REQUEST;
    }

    return ret;
}

int SrsDvrApiPlan::check_user_actions(SrsSharedPtrMessage* msg)
{
    int ret = ERROR_SUCCESS;
    
    srs_assert(segment);

    if (action == SRS_DVR_USER_ACTION_REAP_SEGMENT) {
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

        // use new path template if user specified.
        if (!path_template.empty() && (ret = set_path_tmpl(path_template))  != ERROR_SUCCESS) {
            return ret;
        }
    
        // open new flv file
        if ((ret = segment->open()) != ERROR_SUCCESS) {
            return ret;
        }
    
        // update sequence header
        if (metadata && (ret = SrsDvrPlan::on_meta_data(metadata)) != ERROR_SUCCESS) {
            return ret;
        }
        if (sh_video && (ret = SrsDvrPlan::on_video(sh_video)) != ERROR_SUCCESS) {
            return ret;
        }
        if (sh_audio && (ret = SrsDvrPlan::on_audio(sh_audio)) != ERROR_SUCCESS) {
            return ret;
        }
    }

    // reset rcp params.
    action = "";
    path_template = "";
    
    return ret;
}

SrsDvrAppendPlan::SrsDvrAppendPlan()
{
    last_update_time = 0;
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

int SrsDvrAppendPlan::on_audio(SrsSharedPtrMessage* __audio)
{
    int ret = ERROR_SUCCESS;

    if ((ret = update_duration(__audio)) != ERROR_SUCCESS) {
        return ret;
    }
    
    if ((ret = SrsDvrPlan::on_audio(__audio)) != ERROR_SUCCESS) {
        return ret;
    }

    return ret;
}

int SrsDvrAppendPlan::on_video(SrsSharedPtrMessage* __video)
{
    int ret = ERROR_SUCCESS;

    if ((ret = update_duration(__video)) != ERROR_SUCCESS) {
        return ret;
    }
    
    if ((ret = SrsDvrPlan::on_video(__video)) != ERROR_SUCCESS) {
        return ret;
    }

    return ret;
}

int SrsDvrAppendPlan::update_duration(SrsSharedPtrMessage* msg)
{
    int ret = ERROR_SUCCESS;

    if (last_update_time <= 0) {
        last_update_time = msg->timestamp;
        return ret;
    }

    if (msg->timestamp < last_update_time) {
        last_update_time = msg->timestamp;
        return ret;
    }

    if (__SRS_DVR_UPDATE_DURATION_INTERVAL > msg->timestamp - last_update_time) {
        return ret;
    }
    last_update_time = msg->timestamp;
    
    srs_assert(segment);
    if (!segment->update_flv_metadata()) {
        return ret;
    }

    return ret;
}

SrsDvrSegmentPlan::SrsDvrSegmentPlan()
{
    segment_duration = -1;
    metadata = sh_video = sh_audio = NULL;
}

SrsDvrSegmentPlan::~SrsDvrSegmentPlan()
{
    srs_freep(sh_video);
    srs_freep(sh_audio);
    srs_freep(metadata);
}

int SrsDvrSegmentPlan::initialize(SrsRequest* req)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = SrsDvrPlan::initialize(req)) != ERROR_SUCCESS) {
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

int SrsDvrSegmentPlan::on_meta_data(SrsSharedPtrMessage* __metadata)
{
    int ret = ERROR_SUCCESS;
    
    srs_freep(metadata);
    metadata = __metadata->copy();
    
    if ((ret = SrsDvrPlan::on_meta_data(__metadata)) != ERROR_SUCCESS) {
        return ret;
    }

    return ret;
}

int SrsDvrSegmentPlan::on_audio(SrsSharedPtrMessage* __audio)
{
    int ret = ERROR_SUCCESS;
    
    if (SrsFlvCodec::audio_is_sequence_header(__audio->payload, __audio->size)) {
        srs_freep(sh_audio);
        sh_audio = __audio->copy();
    }

    if ((ret = update_duration(__audio)) != ERROR_SUCCESS) {
        return ret;
    }
    
    if ((ret = SrsDvrPlan::on_audio(__audio)) != ERROR_SUCCESS) {
        return ret;
    }

    return ret;
}

int SrsDvrSegmentPlan::on_video(SrsSharedPtrMessage* __video)
{
    int ret = ERROR_SUCCESS;

    if (SrsFlvCodec::video_is_sequence_header(__video->payload, __video->size)) {
        srs_freep(sh_video);
        sh_video = __video->copy();
    }

    if ((ret = update_duration(__video)) != ERROR_SUCCESS) {
        return ret;
    }
    
    if ((ret = SrsDvrPlan::on_video(__video)) != ERROR_SUCCESS) {
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
    if (metadata && (ret = SrsDvrPlan::on_meta_data(metadata)) != ERROR_SUCCESS) {
        return ret;
    }
    if (sh_video && (ret = SrsDvrPlan::on_video(sh_video)) != ERROR_SUCCESS) {
        return ret;
    }
    if (sh_audio && (ret = SrsDvrPlan::on_audio(sh_audio)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

SrsApiDvrPool* SrsApiDvrPool::_instance = new SrsApiDvrPool();

SrsApiDvrPool* SrsApiDvrPool::instance()
{
    return SrsApiDvrPool::_instance;
}

SrsApiDvrPool::SrsApiDvrPool()
{
}

SrsApiDvrPool::~SrsApiDvrPool()
{
    dvrs.clear();
}

SrsDvrApiPlan* SrsApiDvrPool::get_dvr(string vhost)
{
    std::vector<SrsDvrApiPlan*>::iterator it;
    for (it = dvrs.begin(); it != dvrs.end(); ++it) {
        SrsDvrApiPlan* plan = *it;
        if (plan->req->vhost == vhost) {
            return plan;
        }
    }

    return NULL;
}

int SrsApiDvrPool::add_dvr(SrsDvrApiPlan* dvr)
{
    dvrs.push_back(dvr);
    return ERROR_SUCCESS;
}

void SrsApiDvrPool::detach_dvr(SrsDvrApiPlan* dvr)
{
    std::vector<SrsDvrApiPlan*>::iterator it;
    it = ::find(dvrs.begin(), dvrs.end(), dvr);

    if (it != dvrs.end()) {
        dvrs.erase(it);
    }
}

int SrsApiDvrPool::dumps(string vhost, string app, string stream, stringstream& ss)
{
    int ret = ERROR_SUCCESS;

    ss << __SRS_JARRAY_START;

    std::vector<SrsDvrApiPlan*> plans;
    for (int i = 0; i < (int)dvrs.size(); i++) {
        SrsDvrApiPlan* plan = dvrs.at(i);
        if (!vhost.empty() && plan->req->vhost != vhost) {
            continue;
        }
        if (!app.empty() && plan->req->app != app) {
            continue;
        }
        if (!stream.empty() && plan->req->stream != stream) {
            continue;
        }
        plans.push_back(plan);
    }

    for (int i = 0; i < (int)plans.size(); i++) {
        SrsDvrApiPlan* plan = plans.at(i);

        if ((ret = plan->dumps(ss)) != ERROR_SUCCESS) {
            return ret;
        }

        if (i < (int)plans.size() - 1) {
            ss << __SRS_JFIELD_CONT;
        }
    }

    ss << __SRS_JARRAY_END;

    return ret;
}

int SrsApiDvrPool::create(SrsJsonAny* json)
{
    int ret = ERROR_SUCCESS;
    
    srs_assert(json);
    if (!json->is_object()) {
        ret = ERROR_HTTP_DVR_CREATE_REQUEST;
        srs_error("dvr: api create dvr request requires json object. ret=%d", ret);
        return ret;
    }

    SrsJsonObject* obj = json->to_object();
    SrsJsonAny* prop = NULL;
    if ((prop = obj->ensure_property_string("vhost")) == NULL) {
        ret = ERROR_HTTP_DVR_CREATE_REQUEST;
        srs_error("dvr: api create dvr request requires vhost. ret=%d", ret);
        return ret;
    }
    std::string vhost = prop->to_str();

    if ((prop = obj->ensure_property_string("app")) == NULL) {
        ret = ERROR_HTTP_DVR_CREATE_REQUEST;
        srs_error("dvr: api create dvr request requires app. ret=%d", ret);
        return ret;
    }
    std::string app = prop->to_str();

    if ((prop = obj->ensure_property_string("stream")) == NULL) {
        ret = ERROR_HTTP_DVR_CREATE_REQUEST;
        srs_error("dvr: api create dvr request requires stream. ret=%d", ret);
        return ret;
    }
    std::string stream = prop->to_str();

    if (vhost.empty() || app.empty() || stream.empty()) {
        ret = ERROR_HTTP_DVR_CREATE_REQUEST;
        srs_error("dvr: api create dvr request requires vhost/app/stream. ret=%d", ret);
        return ret;
    }

    SrsDvrApiPlan* dvr = NULL;
    for (int i = 0; i < (int)dvrs.size(); i++) {
        SrsDvrApiPlan* plan = dvrs.at(i);
        if (plan->req->vhost != vhost || plan->req->app != app || plan->req->stream != stream) {
            continue;
        }
        dvr = plan;
        break;
    }

    // mock the client request for dvr.
    SrsRequest* req = new SrsRequest();
    SrsAutoFree(SrsRequest, req);

    // should notice the source to reload dvr when already publishing.
    SrsSource* source = NULL;

    // create if not exists
    if (!dvr) {
        dvr = new SrsDvrApiPlan();

        req->vhost = vhost;
        req->app = app;
        req->stream = stream;
        req->tcUrl = "rtmp://" + vhost + "/" + app + "/" + stream;

        // fetch source from pool.
        //      NULL, create without source, ignore.
        //      start dvr when already publishing.
        source = SrsSource::fetch(req);

        // initialize for dvr pool to create it.
        if ((ret = dvr->initialize(req)) != ERROR_SUCCESS) {
            return ret;
        }
    }

    // update optional parameters for plan.
    if ((ret = dvr->set_plan()) != ERROR_SUCCESS) {
        return ret;
    }
    if ((prop = obj->ensure_property_string("path_tmpl")) != NULL) {
        if ((ret = dvr->set_path_tmpl(prop->to_str())) != ERROR_SUCCESS) {
            return ret;
        }
    }
    if ((prop = obj->ensure_property_boolean("wait_keyframe")) != NULL) {
        if ((ret = dvr->set_wait_keyframe(prop->to_boolean())) != ERROR_SUCCESS) {
            return ret;
        }
    }
    if ((prop = obj->ensure_property_string("callback")) != NULL) {
        if ((ret = dvr->set_callback(prop->to_str())) != ERROR_SUCCESS) {
            return ret;
        }
    }

    if ((ret = dvr->start()) != ERROR_SUCCESS) {
        return ret;
    }

    // do reload for source when already publishing.
    // when reload, the source will use the request instead.
    if (source) {
        if ((ret = source->on_reload_vhost_dvr(vhost)) != ERROR_SUCCESS) {
            return ret;
        }
    }

    return ret;
}

int SrsApiDvrPool::stop(string vhost, string app, string stream)
{
    int ret = ERROR_SUCCESS;

    std::vector<SrsDvrApiPlan*> plans;
    for (int i = 0; i < (int)dvrs.size(); i++) {
        SrsDvrApiPlan* plan = dvrs.at(i);
        if (!vhost.empty() && plan->req->vhost != vhost) {
            continue;
        }
        if (!app.empty() && plan->req->app != app) {
            continue;
        }
        if (!stream.empty() && plan->req->stream != stream) {
            continue;
        }
        plans.push_back(plan);
    }

    if (plans.empty()) {
        ret = ERROR_HTTP_DVR_NO_TAEGET;
        srs_error("dvr: stop not found for url=%s/%s/%s, ret=%d", vhost.c_str(), app.c_str(), stream.c_str(), ret);
        return ret;
    }

    for (int i = 0; i < (int)plans.size(); i++) {
        SrsDvrApiPlan* plan = plans.at(i);

        if ((ret = plan->stop()) != ERROR_SUCCESS) {
            return ret;
        }
    }

    return ret;
}

int SrsApiDvrPool::rpc(SrsJsonAny* json)
{
    int ret = ERROR_SUCCESS;

    if (!json->is_object()) {
        ret = ERROR_HTTP_DVR_REQUEST;
        srs_error("dvr: rpc required object request. ret=%d", ret);
        return ret;
    }

    SrsJsonObject* obj = json->to_object();

    SrsJsonAny* prop = NULL;
    if ((prop = obj->ensure_property_string("vhost")) == NULL) {
        ret = ERROR_HTTP_DVR_REQUEST;
        srs_error("dvr: rpc required vhost request. ret=%d", ret);
        return ret;
    }
    std::string vhost = prop->to_str();
    std::string app, stream;
    if ((prop = obj->ensure_property_string("app")) != NULL) {
        app = prop->to_str();
    }
    if ((prop = obj->ensure_property_string("stream")) != NULL) {
        stream = prop->to_str();
    }

    std::vector<SrsDvrApiPlan*> plans;
    for (int i = 0; i < (int)dvrs.size(); i++) {
        SrsDvrApiPlan* plan = dvrs.at(i);
        if (!vhost.empty() && plan->req->vhost != vhost) {
            continue;
        }
        plans.push_back(plan);
    }

    if (plans.empty()) {
        ret = ERROR_HTTP_DVR_NO_TAEGET;
        srs_error("dvr: rpc not found for url=%s/%s/%s, ret=%d", vhost.c_str(), app.c_str(), stream.c_str(), ret);
        return ret;
    }

    for (int i = 0; i < (int)plans.size(); i++) {
        SrsDvrApiPlan* plan = plans.at(i);

        if ((ret = plan->rpc(obj)) != ERROR_SUCCESS) {
            return ret;
        }
    }

    return ret;
}

SrsDvr::SrsDvr()
{
    source = NULL;
    plan = NULL;
}

SrsDvr::~SrsDvr()
{
    srs_freep(plan);
}

int SrsDvr::initialize(SrsSource* s, SrsRequest* r)
{
    int ret = ERROR_SUCCESS;

    source = s;
    
    srs_freep(plan);
    plan = SrsDvrPlan::create_plan(r->vhost);

    if ((ret = plan->initialize(r)) != ERROR_SUCCESS) {
        return ret;
    }

    if ((ret = source->on_dvr_request_sh()) != ERROR_SUCCESS) {
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

// TODO: FIXME: source should use shared message instead.
int SrsDvr::on_meta_data(SrsOnMetaDataPacket* m)
{
    int ret = ERROR_SUCCESS;

    int size = 0;
    char* payload = NULL;
    if ((ret = m->encode(size, payload)) != ERROR_SUCCESS) {
        return ret;
    }

    SrsSharedPtrMessage metadata;
    if ((ret = metadata.create(NULL, payload, size)) != ERROR_SUCCESS) {
        return ret;
    }

    if ((ret = plan->on_meta_data(&metadata)) != ERROR_SUCCESS) {
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


