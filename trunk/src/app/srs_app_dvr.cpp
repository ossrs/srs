/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2017 OSSRS(winlin)
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

#include <srs_app_dvr.hpp>

#include <fcntl.h>
#include <sstream>
#include <algorithm>
using namespace std;

#include <srs_app_config.hpp>
#include <srs_rtmp_stack.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_app_http_hooks.hpp>
#include <srs_kernel_codec.hpp>
#include <srs_kernel_flv.hpp>
#include <srs_kernel_file.hpp>
#include <srs_protocol_amf0.hpp>
#include <srs_kernel_buffer.hpp>
#include <srs_protocol_json.hpp>
#include <srs_app_utility.hpp>
#include <srs_kernel_mp4.hpp>
#include <srs_app_fragment.hpp>

SrsDvrSegmenter::SrsDvrSegmenter()
{
    req = NULL;
    jitter = NULL;
    plan = NULL;
    
    fragment = new SrsFragment();
    fs = new SrsFileWriter();
    jitter_algorithm = SrsRtmpJitterAlgorithmOFF;
    
    _srs_config->subscribe(this);
}

SrsDvrSegmenter::~SrsDvrSegmenter()
{
    _srs_config->unsubscribe(this);
    
    srs_freep(fragment);
    srs_freep(jitter);
    srs_freep(fs);
}

srs_error_t SrsDvrSegmenter::initialize(SrsDvrPlan* p, SrsRequest* r)
{
    req = r;
    plan = p;
    
    jitter_algorithm = (SrsRtmpJitterAlgorithm)_srs_config->get_dvr_time_jitter(req->vhost);
    wait_keyframe = _srs_config->get_dvr_wait_keyframe(req->vhost);
    
    return srs_success;
}

SrsFragment* SrsDvrSegmenter::current()
{
    return fragment;
}

int SrsDvrSegmenter::open()
{
    int ret = ERROR_SUCCESS;
    
    // ignore when already open.
    if (fs->is_open()) {
        return ret;
    }
    
    string path = generate_path();
    if (srs_path_exists(path)) {
        ret = ERROR_DVR_CANNOT_APPEND;
        srs_error("DVR can't append to exists path=%s. ret=%d", path.c_str(), ret);
        return ret;
    }
    fragment->set_path(path);
    
    // create dir first.
    if ((ret = fragment->create_dir()) != ERROR_SUCCESS) {
        return ret;
    }
    
    // create jitter.
    srs_freep(jitter);
    jitter = new SrsRtmpJitter();
    
    // open file writer, in append or create mode.
    string tmp_dvr_file = fragment->tmppath();
    if ((ret = fs->open(tmp_dvr_file)) != ERROR_SUCCESS) {
        srs_error("open file stream for file %s failed. ret=%d", path.c_str(), ret);
        return ret;
    }
    
    // initialize the encoder.
    if ((ret = open_encoder()) != ERROR_SUCCESS) {
        srs_error("initialize enc by fs for file %s failed. ret=%d", path.c_str(), ret);
        return ret;
    }
    
    srs_trace("dvr stream %s to file %s", req->stream.c_str(), path.c_str());
    
    return ret;
}

int SrsDvrSegmenter::write_metadata(SrsSharedPtrMessage* metadata)
{
    return encode_metadata(metadata);
}

int SrsDvrSegmenter::write_audio(SrsSharedPtrMessage* shared_audio, SrsFormat* format)
{
    int ret = ERROR_SUCCESS;
    
    SrsSharedPtrMessage* audio = shared_audio->copy();
    SrsAutoFree(SrsSharedPtrMessage, audio);
    
    if ((jitter->correct(audio, jitter_algorithm)) != ERROR_SUCCESS) {
        return ret;
    }
    
    if ((ret = on_update_duration(audio)) != ERROR_SUCCESS) {
        return ret;
    }
    
    if ((ret = encode_audio(audio, format)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int SrsDvrSegmenter::write_video(SrsSharedPtrMessage* shared_video, SrsFormat* format)
{
    int ret = ERROR_SUCCESS;
    
    SrsSharedPtrMessage* video = shared_video->copy();
    SrsAutoFree(SrsSharedPtrMessage, video);
    
    if ((jitter->correct(video, jitter_algorithm)) != ERROR_SUCCESS) {
        return ret;
    }
    
    if ((ret = encode_video(video, format)) != ERROR_SUCCESS) {
        return ret;
    }
    
    if ((ret = on_update_duration(video)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int SrsDvrSegmenter::close()
{
    int ret = ERROR_SUCCESS;
    
    // ignore when already closed.
    if (!fs->is_open()) {
        return ret;
    }
    
    // Close the encoder, then close the fs object.
    if ((ret = close_encoder()) != ERROR_SUCCESS) {
        return ret;
    }
    
    fs->close();
    
    // when tmp flv file exists, reap it.
    if ((ret = fragment->rename()) != ERROR_SUCCESS) {
        return ret;
    }
    
    // TODO: FIXME: the http callback is async, which will trigger thread switch,
    //          so the on_video maybe invoked during the http callback, and error.
    if ((ret = plan->on_reap_segment()) != ERROR_SUCCESS) {
        srs_error("dvr: notify plan to reap segment failed. ret=%d", ret);
        return ret;
    }
    
    return ret;
}

int SrsDvrSegmenter::on_update_duration(SrsSharedPtrMessage* msg)
{
    int ret = ERROR_SUCCESS;
    
    fragment->append(msg->timestamp);
    
    return ret;
}

string SrsDvrSegmenter::generate_path()
{
    // the path in config, for example,
    //      /data/[vhost]/[app]/[stream]/[2006]/[01]/[02]/[15].[04].[05].[999].flv
    std::string path_config = _srs_config->get_dvr_path(req->vhost);
    
    // add [stream].[timestamp].flv as filename for dir
    if (!srs_string_ends_with(path_config, ".flv", ".mp4")) {
        path_config += "/[stream].[timestamp].flv";
    }
    
    // the flv file path
    std::string flv_path = path_config;
    flv_path = srs_path_build_stream(flv_path, req->vhost, req->app, req->stream);
    flv_path = srs_path_build_timestamp(flv_path);
    
    return flv_path;
}

int SrsDvrSegmenter::on_reload_vhost_dvr(std::string vhost)
{
    int ret = ERROR_SUCCESS;
    
    if (req->vhost != vhost) {
        return ret;
    }
    
    jitter_algorithm = (SrsRtmpJitterAlgorithm)_srs_config->get_dvr_time_jitter(req->vhost);
    wait_keyframe = _srs_config->get_dvr_wait_keyframe(req->vhost);
    
    return ret;
}

SrsDvrFlvSegmenter::SrsDvrFlvSegmenter()
{
    enc = new SrsFlvTransmuxer();
    
    duration_offset = 0;
    filesize_offset = 0;
    
    has_keyframe = false;
}

SrsDvrFlvSegmenter::~SrsDvrFlvSegmenter()
{
    srs_freep(enc);
}

int SrsDvrFlvSegmenter::refresh_metadata()
{
    int ret = ERROR_SUCCESS;
    
    // no duration or filesize specified.
    if (!duration_offset || !filesize_offset) {
        return ret;
    }
    
    int64_t cur = fs->tellg();
    
    // buffer to write the size.
    char* buf = new char[SrsAmf0Size::number()];
    SrsAutoFreeA(char, buf);
    
    SrsBuffer stream;
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
    fs->seek2(filesize_offset);
    if ((ret = fs->write(buf, SrsAmf0Size::number(), NULL)) != ERROR_SUCCESS) {
        return ret;
    }
    
    // duration to buf
    SrsAmf0Any* dur = SrsAmf0Any::number((double)fragment->duration() / 1000.0);
    SrsAutoFree(SrsAmf0Any, dur);
    
    stream.skip(-1 * stream.pos());
    if ((ret = dur->write(&stream)) != ERROR_SUCCESS) {
        return ret;
    }
    
    // update the duration
    fs->seek2(duration_offset);
    if ((ret = fs->write(buf, SrsAmf0Size::number(), NULL)) != ERROR_SUCCESS) {
        return ret;
    }
    
    // reset the offset.
    fs->seek2(cur);
    
    return ret;
}

int SrsDvrFlvSegmenter::open_encoder()
{
    int ret = ERROR_SUCCESS;
    
    has_keyframe = false;
    
    // update the duration and filesize offset.
    duration_offset = 0;
    filesize_offset = 0;
    
    srs_freep(enc);
    enc = new SrsFlvTransmuxer();
    
    if ((ret = enc->initialize(fs)) != ERROR_SUCCESS) {
        return ret;
    }
    
    // write the flv header to writer.
    if ((ret = enc->write_header()) != ERROR_SUCCESS) {
        srs_error("write flv header failed. ret=%d", ret);
        return ret;
    }
    
    return ret;
}

int SrsDvrFlvSegmenter::encode_metadata(SrsSharedPtrMessage* metadata)
{
    int ret = ERROR_SUCCESS;
    
    // Ignore when metadata already written.
    if (duration_offset || filesize_offset) {
        return ret;
    }
    
    SrsBuffer stream;
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
    SrsAutoFreeA(char, payload);
    
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

int SrsDvrFlvSegmenter::encode_audio(SrsSharedPtrMessage* audio, SrsFormat* format)
{
    int ret = ERROR_SUCCESS;
    
    char* payload = audio->payload;
    int size = audio->size;
    if ((ret = enc->write_audio(audio->timestamp, payload, size)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int SrsDvrFlvSegmenter::encode_video(SrsSharedPtrMessage* video, SrsFormat* format)
{
    int ret = ERROR_SUCCESS;
    
    char* payload = video->payload;
    int size = video->size;
    bool sh = (format->video->avc_packet_type == SrsVideoAvcFrameTraitSequenceHeader);
    bool keyframe = (!sh && format->video->frame_type == SrsVideoAvcFrameTypeKeyFrame);
    
    if (keyframe) {
        has_keyframe = true;
    }
    
    // accept the sequence header here.
    // when got no keyframe, ignore when should wait keyframe.
    if (!has_keyframe && !sh) {
        if (wait_keyframe) {
            srs_info("dvr: ignore when wait keyframe.");
            return ret;
        }
    }
    
    if ((ret = enc->write_video(video->timestamp, payload, size)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int SrsDvrFlvSegmenter::close_encoder()
{
    return refresh_metadata();
}

SrsDvrMp4Segmenter::SrsDvrMp4Segmenter()
{
    enc = new SrsMp4Encoder();
}

SrsDvrMp4Segmenter::~SrsDvrMp4Segmenter()
{
    srs_freep(enc);
}

int SrsDvrMp4Segmenter::refresh_metadata()
{
    return ERROR_SUCCESS;
}

int SrsDvrMp4Segmenter::open_encoder()
{
    int ret = ERROR_SUCCESS;
    
    srs_freep(enc);
    enc = new SrsMp4Encoder();
    
    if ((ret = enc->initialize(fs)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int SrsDvrMp4Segmenter::encode_metadata(SrsSharedPtrMessage* /*metadata*/)
{
    return ERROR_SUCCESS;
}

int SrsDvrMp4Segmenter::encode_audio(SrsSharedPtrMessage* audio, SrsFormat* format)
{
    SrsAudioCodecId sound_format = format->acodec->id;
    SrsAudioSampleRate sound_rate = format->acodec->sound_rate;
    SrsAudioSampleBits sound_size = format->acodec->sound_size;
    SrsAudioChannels channels = format->acodec->sound_type;
    
    SrsAudioAacFrameTrait ct = format->audio->aac_packet_type;
    if (ct == SrsAudioAacFrameTraitSequenceHeader) {
        enc->acodec = sound_format;
        enc->sample_rate = sound_rate;
        enc->sound_bits = sound_size;
        enc->channels = channels;
    }
    
    uint8_t* sample = (uint8_t*)format->raw;
    uint32_t nb_sample = (uint32_t)format->nb_raw;
    
    uint32_t dts = (uint32_t)audio->timestamp;
    return enc->write_sample(SrsMp4HandlerTypeSOUN, 0x00, ct, dts, dts, sample, nb_sample);
}

int SrsDvrMp4Segmenter::encode_video(SrsSharedPtrMessage* video, SrsFormat* format)
{
    SrsVideoAvcFrameType frame_type = format->video->frame_type;
    SrsVideoCodecId codec_id = format->vcodec->id;
    
    SrsVideoAvcFrameTrait ct = format->video->avc_packet_type;
    uint32_t cts = (uint32_t)format->video->cts;
    
    if (ct == SrsVideoAvcFrameTraitSequenceHeader) {
        enc->vcodec = codec_id;
    }
    
    uint32_t dts = (uint32_t)video->timestamp;
    uint32_t pts = dts + cts;
    
    uint8_t* sample = (uint8_t*)format->raw;
    uint32_t nb_sample = (uint32_t)format->nb_raw;
    return enc->write_sample(SrsMp4HandlerTypeVIDE, frame_type, ct, dts, pts, sample, nb_sample);
}

int SrsDvrMp4Segmenter::close_encoder()
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = enc->flush()) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

SrsDvrAsyncCallOnDvr::SrsDvrAsyncCallOnDvr(int c, SrsRequest* r, string p)
{
    cid = c;
    req = r->copy();
    path = p;
}

SrsDvrAsyncCallOnDvr::~SrsDvrAsyncCallOnDvr()
{
    srs_freep(req);
}

int SrsDvrAsyncCallOnDvr::call()
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
        SrsConfDirective* conf = _srs_config->get_vhost_on_dvr(req->vhost);
        
        if (!conf) {
            srs_info("ignore the empty http callback: on_dvr");
            return ret;
        }
        
        hooks = conf->args;
    }
    
    for (int i = 0; i < (int)hooks.size(); i++) {
        std::string url = hooks.at(i);
        if ((ret = SrsHttpHooks::on_dvr(cid, url, req, path)) != ERROR_SUCCESS) {
            srs_error("hook client on_dvr failed. url=%s, ret=%d", url.c_str(), ret);
            return ret;
        }
    }
    
    return ret;
}

string SrsDvrAsyncCallOnDvr::to_string()
{
    std::stringstream ss;
    ss << "vhost=" << req->vhost << ", file=" << path;
    return ss.str();
}

SrsDvrPlan::SrsDvrPlan()
{
    req = NULL;
    
    dvr_enabled = false;
    segment = NULL;
    async = new SrsAsyncCallWorker();
}

SrsDvrPlan::~SrsDvrPlan()
{
    srs_freep(segment);
    srs_freep(async);
}

srs_error_t SrsDvrPlan::initialize(SrsOriginHub* h, SrsDvrSegmenter* s, SrsRequest* r)
{
    int ret = ERROR_SUCCESS;
    srs_error_t err = srs_success;
    
    hub = h;
    req = r;
    segment = s;
    
    if ((err = segment->initialize(this, r)) != srs_success) {
        return srs_error_wrap(err, "segmenter");
    }
    
    if ((ret = async->start()) != ERROR_SUCCESS) {
        return srs_error_new(ret, "async");
    }
    
    return err;
}

int SrsDvrPlan::on_meta_data(SrsSharedPtrMessage* shared_metadata)
{
    int ret = ERROR_SUCCESS;
    
    if (!dvr_enabled) {
        return ret;
    }
    
    return segment->write_metadata(shared_metadata);
}

int SrsDvrPlan::on_audio(SrsSharedPtrMessage* shared_audio, SrsFormat* format)
{
    int ret = ERROR_SUCCESS;
    
    if (!dvr_enabled) {
        return ret;
    }
    
    if ((ret = segment->write_audio(shared_audio, format)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int SrsDvrPlan::on_video(SrsSharedPtrMessage* shared_video, SrsFormat* format)
{
    int ret = ERROR_SUCCESS;
    
    if (!dvr_enabled) {
        return ret;
    }
    
    if ((ret = segment->write_video(shared_video, format)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int SrsDvrPlan::on_reap_segment()
{
    int ret = ERROR_SUCCESS;
    
    int cid = _srs_context->get_id();
    
    SrsFragment* fragment = segment->current();
    string fullpath = fragment->fullpath();
    
    if ((ret = async->execute(new SrsDvrAsyncCallOnDvr(cid, req, fullpath))) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

srs_error_t SrsDvrPlan::create_plan(string vhost, SrsDvrPlan** pplan)
{
    std::string plan = _srs_config->get_dvr_plan(vhost);
    if (srs_config_dvr_is_plan_segment(plan)) {
        *pplan = new SrsDvrSegmentPlan();
    } else if (srs_config_dvr_is_plan_session(plan)) {
        *pplan = new SrsDvrSessionPlan();
    } else {
        return srs_error_new(ERROR_DVR_ILLEGAL_PLAN, "illegal plan=%s, vhost=%s",
            plan.c_str(), vhost.c_str());
    }
    
    return srs_success;
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

SrsDvrSegmentPlan::SrsDvrSegmentPlan()
{
    cduration = -1;
    wait_keyframe = false;
}

SrsDvrSegmentPlan::~SrsDvrSegmentPlan()
{
}

srs_error_t SrsDvrSegmentPlan::initialize(SrsOriginHub* h, SrsDvrSegmenter* s, SrsRequest* r)
{
    srs_error_t err = srs_success;
    
    if ((err = SrsDvrPlan::initialize(h, s, r)) != srs_success) {
        return srs_error_wrap(err, "segment plan");
    }
    
    wait_keyframe = _srs_config->get_dvr_wait_keyframe(req->vhost);
    
    cduration = _srs_config->get_dvr_duration(req->vhost);
    // to ms
    cduration *= 1000;
    
    return srs_success;
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

int SrsDvrSegmentPlan::on_audio(SrsSharedPtrMessage* shared_audio, SrsFormat* format)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = update_duration(shared_audio)) != ERROR_SUCCESS) {
        return ret;
    }
    
    if ((ret = SrsDvrPlan::on_audio(shared_audio, format)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int SrsDvrSegmentPlan::on_video(SrsSharedPtrMessage* shared_video, SrsFormat* format)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = update_duration(shared_video)) != ERROR_SUCCESS) {
        return ret;
    }
    
    if ((ret = SrsDvrPlan::on_video(shared_video, format)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int SrsDvrSegmentPlan::update_duration(SrsSharedPtrMessage* msg)
{
    int ret = ERROR_SUCCESS;
    
    srs_assert(segment);
    
    // ignore if duration ok.
    SrsFragment* fragment = segment->current();
    if (cduration <= 0 || fragment->duration() < cduration) {
        return ret;
    }
    
    // when wait keyframe, ignore if no frame arrived.
    // @see https://github.com/ossrs/srs/issues/177
    if (wait_keyframe) {
        if (!msg->is_video()) {
            return ret;
        }
        
        char* payload = msg->payload;
        int size = msg->size;
        bool is_key_frame = SrsFlvVideo::h264(payload, size)
        && SrsFlvVideo::keyframe(payload, size)
        && !SrsFlvVideo::sh(payload, size);
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
    if ((ret = hub->on_dvr_request_sh()) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int SrsDvrSegmentPlan::on_reload_vhost_dvr(string vhost)
{
    int ret = ERROR_SUCCESS;
    
    if (req->vhost != vhost) {
        return ret;
    }
    
    wait_keyframe = _srs_config->get_dvr_wait_keyframe(req->vhost);
    
    cduration = _srs_config->get_dvr_duration(req->vhost);
    // to ms
    cduration *= 1000;
    
    return ret;
}

SrsDvr::SrsDvr()
{
    hub = NULL;
    plan = NULL;
    req = NULL;
    actived = false;
    
    _srs_config->subscribe(this);
}

SrsDvr::~SrsDvr()
{
    _srs_config->unsubscribe(this);
    
    srs_freep(plan);
}

srs_error_t SrsDvr::initialize(SrsOriginHub* h, SrsRequest* r)
{
    srs_error_t err = srs_success;
    
    req = r;
    hub = h;
    
    SrsConfDirective* conf = _srs_config->get_dvr_apply(r->vhost);
    actived = srs_config_apply_filter(conf, r);
    
    srs_freep(plan);
    if ((err = SrsDvrPlan::create_plan(r->vhost, &plan)) != srs_success) {
        return srs_error_wrap(err, "create plan");
    }
    
    std::string path = _srs_config->get_dvr_path(r->vhost);
    SrsDvrSegmenter* segmenter = NULL;
    if (srs_string_ends_with(path, ".mp4")) {
        segmenter = new SrsDvrMp4Segmenter();
    } else {
        segmenter = new SrsDvrFlvSegmenter();
    }
    
    if ((err = plan->initialize(hub, segmenter, r)) != srs_success) {
        return srs_error_wrap(err, "plan initialize");
    }
    
    return err;
}

int SrsDvr::on_publish()
{
    int ret = ERROR_SUCCESS;
    
    // the dvr for this stream is not actived.
    if (!actived) {
        return ret;
    }
    
    if ((ret = plan->on_publish()) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

void SrsDvr::on_unpublish()
{
    plan->on_unpublish();
}

int SrsDvr::on_meta_data(SrsSharedPtrMessage* metadata)
{
    int ret = ERROR_SUCCESS;
    
    // the dvr for this stream is not actived.
    if (!actived) {
        return ret;
    }
    
    if ((ret = plan->on_meta_data(metadata)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int SrsDvr::on_audio(SrsSharedPtrMessage* shared_audio, SrsFormat* format)
{
    // the dvr for this stream is not actived.
    if (!actived) {
        return ERROR_SUCCESS;
    }
    
    return plan->on_audio(shared_audio, format);
}

int SrsDvr::on_video(SrsSharedPtrMessage* shared_video, SrsFormat* format)
{
    // the dvr for this stream is not actived.
    if (!actived) {
        return ERROR_SUCCESS;
    }
    
    return plan->on_video(shared_video, format);
}

int SrsDvr::on_reload_vhost_dvr_apply(string vhost)
{
    int ret = ERROR_SUCCESS;
    
    SrsConfDirective* conf = _srs_config->get_dvr_apply(req->vhost);
    bool v = srs_config_apply_filter(conf, req);
    
    // the apply changed, republish the dvr.
    if (v == actived) {
        return ret;
    }
    actived = v;
    
    on_unpublish();
    if (!actived) {
        return ret;
    }
    
    if ((ret = on_publish()) != ERROR_SUCCESS) {
        return ret;
    }
    if ((ret = hub->on_dvr_request_sh()) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}


