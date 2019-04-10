/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2019 Winlin
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

srs_error_t SrsDvrSegmenter::open()
{
    srs_error_t err = srs_success;
    
    // ignore when already open.
    if (fs->is_open()) {
        return err;
    }
    
    string path = generate_path();
    if (srs_path_exists(path)) {
        return srs_error_new(ERROR_DVR_CANNOT_APPEND, "DVR can't append to exists path=%s", path.c_str());
    }
    fragment->set_path(path);
    
    // create dir first.
    if ((err = fragment->create_dir()) != srs_success) {
        return srs_error_wrap(err, "create dir");
    }
    
    // create jitter.
    srs_freep(jitter);
    jitter = new SrsRtmpJitter();
    
    // open file writer, in append or create mode.
    string tmp_dvr_file = fragment->tmppath();
    if ((err = fs->open(tmp_dvr_file)) != srs_success) {
        return srs_error_wrap(err, "open file %s", path.c_str());
    }
    
    // initialize the encoder.
    if ((err = open_encoder()) != srs_success) {
        return srs_error_wrap(err, "open encoder");
    }
    
    srs_trace("dvr stream %s to file %s", req->stream.c_str(), path.c_str());
    return err;
}

srs_error_t SrsDvrSegmenter::write_metadata(SrsSharedPtrMessage* metadata)
{
    return encode_metadata(metadata);
}

srs_error_t SrsDvrSegmenter::write_audio(SrsSharedPtrMessage* shared_audio, SrsFormat* format)
{
    srs_error_t err = srs_success;
    
    SrsSharedPtrMessage* audio = shared_audio->copy();
    SrsAutoFree(SrsSharedPtrMessage, audio);
    
    if ((err = jitter->correct(audio, jitter_algorithm)) != srs_success) {
        return srs_error_wrap(err, "jitter");
    }
    
    if ((err = on_update_duration(audio)) != srs_success) {
        return srs_error_wrap(err, "update duration");
    }
    
    if ((err = encode_audio(audio, format)) != srs_success) {
        return srs_error_wrap(err, "encode audio");
    }
    
    return err;
}

srs_error_t SrsDvrSegmenter::write_video(SrsSharedPtrMessage* shared_video, SrsFormat* format)
{
    srs_error_t err = srs_success;
    
    SrsSharedPtrMessage* video = shared_video->copy();
    SrsAutoFree(SrsSharedPtrMessage, video);
    
    if ((err = jitter->correct(video, jitter_algorithm)) != srs_success) {
        return srs_error_wrap(err, "jitter");
    }
    
    if ((err = encode_video(video, format)) != srs_success) {
        return srs_error_wrap(err, "encode video");
    }
    
    if ((err = on_update_duration(video)) != srs_success) {
        return srs_error_wrap(err, "update duration");
    }
    
    return err;
}

srs_error_t SrsDvrSegmenter::close()
{
    srs_error_t err = srs_success;
    
    // ignore when already closed.
    if (!fs->is_open()) {
        return err;
    }
    
    // Close the encoder, then close the fs object.
    if ((err = close_encoder()) != srs_success) {
        return srs_error_wrap(err, "close encoder");
    }
    
    fs->close();
    
    // when tmp flv file exists, reap it.
    if ((err = fragment->rename()) != srs_success) {
        return srs_error_wrap(err, "rename fragment");
    }
    
    // TODO: FIXME: the http callback is async, which will trigger thread switch,
    //          so the on_video maybe invoked during the http callback, and error.
    if ((err = plan->on_reap_segment()) != srs_success) {
        return srs_error_wrap(err, "reap segment");
    }
    
    return err;
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

srs_error_t SrsDvrSegmenter::on_update_duration(SrsSharedPtrMessage* msg)
{
    fragment->append(msg->timestamp);
    return srs_success;
}

srs_error_t SrsDvrSegmenter::on_reload_vhost_dvr(std::string vhost)
{
    srs_error_t err = srs_success;
    
    if (req->vhost != vhost) {
        return err;
    }
    
    jitter_algorithm = (SrsRtmpJitterAlgorithm)_srs_config->get_dvr_time_jitter(req->vhost);
    wait_keyframe = _srs_config->get_dvr_wait_keyframe(req->vhost);
    
    return err;
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

srs_error_t SrsDvrFlvSegmenter::refresh_metadata()
{
    srs_error_t err = srs_success;
    
    // no duration or filesize specified.
    if (!duration_offset || !filesize_offset) {
        return err;
    }
    
    int64_t cur = fs->tellg();
    
    // buffer to write the size.
    char* buf = new char[SrsAmf0Size::number()];
    SrsAutoFreeA(char, buf);
    
    SrsBuffer stream(buf, SrsAmf0Size::number());
    
    // filesize to buf.
    SrsAmf0Any* size = SrsAmf0Any::number((double)cur);
    SrsAutoFree(SrsAmf0Any, size);
    
    stream.skip(-1 * stream.pos());
    if ((err = size->write(&stream)) != srs_success) {
        return srs_error_wrap(err, "write filesize");
    }
    
    // update the flesize.
    fs->seek2(filesize_offset);
    if ((err = fs->write(buf, SrsAmf0Size::number(), NULL)) != srs_success) {
        return srs_error_wrap(err, "update filesize");
    }
    
    // duration to buf
    SrsAmf0Any* dur = SrsAmf0Any::number((double)fragment->duration() / 1000.0);
    SrsAutoFree(SrsAmf0Any, dur);
    
    stream.skip(-1 * stream.pos());
    if ((err = dur->write(&stream)) != srs_success) {
        return srs_error_wrap(err, "write duration");
    }
    
    // update the duration
    fs->seek2(duration_offset);
    if ((err = fs->write(buf, SrsAmf0Size::number(), NULL)) != srs_success) {
        return srs_error_wrap(err, "update duration");
    }
    
    // reset the offset.
    fs->seek2(cur);
    
    return err;
}

srs_error_t SrsDvrFlvSegmenter::open_encoder()
{
    srs_error_t err = srs_success;
    
    has_keyframe = false;
    
    // update the duration and filesize offset.
    duration_offset = 0;
    filesize_offset = 0;
    
    srs_freep(enc);
    enc = new SrsFlvTransmuxer();
    
    if ((err = enc->initialize(fs)) != srs_success) {
        return srs_error_wrap(err, "init encoder");
    }
    
    // write the flv header to writer.
    if ((err = enc->write_header()) != srs_success) {
        return srs_error_wrap(err, "write flv header");
    }
    
    return err;
}

srs_error_t SrsDvrFlvSegmenter::encode_metadata(SrsSharedPtrMessage* metadata)
{
    srs_error_t err = srs_success;
    
    // Ignore when metadata already written.
    if (duration_offset || filesize_offset) {
        return err;
    }

    SrsBuffer stream(metadata->payload, metadata->size);
    
    SrsAmf0Any* name = SrsAmf0Any::str();
    SrsAutoFree(SrsAmf0Any, name);
    if ((err = name->read(&stream)) != srs_success) {
        return srs_error_wrap(err, "read name");
    }
    
    SrsAmf0Object* obj = SrsAmf0Any::object();
    SrsAutoFree(SrsAmf0Object, obj);
    if ((err = obj->read(&stream)) != srs_success) {
        return srs_error_wrap(err, "read object");
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
    SrsBuffer buf(payload, size);
    
    if ((err = name->write(&buf)) != srs_success) {
        return srs_error_wrap(err, "write name");
    }
    if ((err = obj->write(&buf)) != srs_success) {
        return srs_error_wrap(err, "write object");
    }
    
    // to flv file.
    if ((err = enc->write_metadata(18, payload, size)) != srs_success) {
        return srs_error_wrap(err, "write metadata");
    }
    
    return err;
}

srs_error_t SrsDvrFlvSegmenter::encode_audio(SrsSharedPtrMessage* audio, SrsFormat* format)
{
    srs_error_t err = srs_success;
    
    char* payload = audio->payload;
    int size = audio->size;
    if ((err = enc->write_audio(audio->timestamp, payload, size)) != srs_success) {
        return srs_error_wrap(err, "write audio");
    }
    
    return err;
}

srs_error_t SrsDvrFlvSegmenter::encode_video(SrsSharedPtrMessage* video, SrsFormat* format)
{
    srs_error_t err = srs_success;
    
    char* payload = video->payload;
    int size = video->size;
    bool sh = (format->video->avc_packet_type == SrsVideoAvcFrameTraitSequenceHeader);
    bool keyframe = (!sh && format->video->frame_type == SrsVideoAvcFrameTypeKeyFrame);
    
    if (keyframe) {
        has_keyframe = true;
    }
    
    // accept the sequence header here.
    // when got no keyframe, ignore when should wait keyframe.
    if (!has_keyframe && !sh && wait_keyframe) {
        return err;
    }
    
    if ((err = enc->write_video(video->timestamp, payload, size)) != srs_success) {
        return srs_error_wrap(err, "write video");
    }
    
    return err;
}

srs_error_t SrsDvrFlvSegmenter::close_encoder()
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

srs_error_t SrsDvrMp4Segmenter::refresh_metadata()
{
    return srs_success;
}

srs_error_t SrsDvrMp4Segmenter::open_encoder()
{
    srs_error_t err = srs_success;
    
    srs_freep(enc);
    enc = new SrsMp4Encoder();
    
    if ((err = enc->initialize(fs)) != srs_success) {
        return srs_error_wrap(err, "init encoder");
    }
    
    return err;
}

srs_error_t SrsDvrMp4Segmenter::encode_metadata(SrsSharedPtrMessage* /*metadata*/)
{
    return srs_success;
}

srs_error_t SrsDvrMp4Segmenter::encode_audio(SrsSharedPtrMessage* audio, SrsFormat* format)
{
    srs_error_t err = srs_success;
    
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
    if ((err = enc->write_sample(SrsMp4HandlerTypeSOUN, 0x00, ct, dts, dts, sample, nb_sample)) != srs_success) {
        return srs_error_wrap(err, "write sample");
    }
    
    return err;
}

srs_error_t SrsDvrMp4Segmenter::encode_video(SrsSharedPtrMessage* video, SrsFormat* format)
{
    srs_error_t err = srs_success;
    
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
    if ((err = enc->write_sample(SrsMp4HandlerTypeVIDE, frame_type, ct, dts, pts, sample, nb_sample)) != srs_success) {
        return srs_error_wrap(err, "write sample");
    }
    
    return err;
}

srs_error_t SrsDvrMp4Segmenter::close_encoder()
{
    srs_error_t err = srs_success;
    
    if ((err = enc->flush()) != srs_success) {
        return srs_error_wrap(err, "flush encoder");
    }
    
    return err;
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

srs_error_t SrsDvrAsyncCallOnDvr::call()
{
    srs_error_t err = srs_success;
    
    if (!_srs_config->get_vhost_http_hooks_enabled(req->vhost)) {
        return err;
    }
    
    // the http hooks will cause context switch,
    // so we must copy all hooks for the on_connect may freed.
    // @see https://github.com/ossrs/srs/issues/475
    vector<string> hooks;
    
    if (true) {
        SrsConfDirective* conf = _srs_config->get_vhost_on_dvr(req->vhost);
        if (conf) {
            hooks = conf->args;
        }
    }
    
    for (int i = 0; i < (int)hooks.size(); i++) {
        std::string url = hooks.at(i);
        if ((err = SrsHttpHooks::on_dvr(cid, url, req, path)) != srs_success) {
            return srs_error_wrap(err, "callback on_dvr %s", url.c_str());
        }
    }
    
    return err;
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
    srs_error_t err = srs_success;
    
    hub = h;
    req = r;
    segment = s;
    
    if ((err = segment->initialize(this, r)) != srs_success) {
        return srs_error_wrap(err, "segmenter");
    }
    
    if ((err = async->start()) != srs_success) {
        return srs_error_wrap(err, "async");
    }
    
    return err;
}

srs_error_t SrsDvrPlan::on_meta_data(SrsSharedPtrMessage* shared_metadata)
{
    srs_error_t err = srs_success;
    
    if (!dvr_enabled) {
        return err;
    }
    
    return segment->write_metadata(shared_metadata);
}

srs_error_t SrsDvrPlan::on_audio(SrsSharedPtrMessage* shared_audio, SrsFormat* format)
{
    srs_error_t err = srs_success;
    
    if (!dvr_enabled) {
        return err;
    }
    
    if ((err = segment->write_audio(shared_audio, format)) != srs_success) {
        return srs_error_wrap(err, "write audio");
    }
    
    return err;
}

srs_error_t SrsDvrPlan::on_video(SrsSharedPtrMessage* shared_video, SrsFormat* format)
{
    srs_error_t err = srs_success;
    
    if (!dvr_enabled) {
        return err;
    }
    
    if ((err = segment->write_video(shared_video, format)) != srs_success) {
        return srs_error_wrap(err, "write video");
    }
    
    return err;
}

srs_error_t SrsDvrPlan::on_reap_segment()
{
    srs_error_t err = srs_success;
    
    int cid = _srs_context->get_id();
    
    SrsFragment* fragment = segment->current();
    string fullpath = fragment->fullpath();
    
    if ((err = async->execute(new SrsDvrAsyncCallOnDvr(cid, req, fullpath))) != srs_success) {
        return srs_error_wrap(err, "reap segment");
    }
    
    return err;
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

srs_error_t SrsDvrSessionPlan::on_publish()
{
    srs_error_t err = srs_success;
    
    // support multiple publish.
    if (dvr_enabled) {
        return err;
    }
    
    if (!_srs_config->get_dvr_enabled(req->vhost)) {
        return err;
    }
    
    if ((err = segment->close()) != srs_success) {
        return srs_error_wrap(err, "close segment");
    }
    
    if ((err = segment->open()) != srs_success) {
        return srs_error_wrap(err, "open segment");
    }
    
    dvr_enabled = true;
    
    return err;
}

void SrsDvrSessionPlan::on_unpublish()
{
    // support multiple publish.
    if (!dvr_enabled) {
        return;
    }
    
    // ignore error.
    srs_error_t err = segment->close();
    if (err != srs_success) {
        srs_warn("ignore flv close error %s", srs_error_desc(err).c_str());
    }
    
    dvr_enabled = false;
}

SrsDvrSegmentPlan::SrsDvrSegmentPlan()
{
    cduration = 0;
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
    
    return srs_success;
}

srs_error_t SrsDvrSegmentPlan::on_publish()
{
    srs_error_t err = srs_success;
    
    // support multiple publish.
    if (dvr_enabled) {
        return err;
    }
    
    if (!_srs_config->get_dvr_enabled(req->vhost)) {
        return err;
    }
    
    if ((err = segment->close()) != srs_success) {
        return srs_error_wrap(err, "segment close");
    }
    
    if ((err = segment->open()) != srs_success) {
        return srs_error_wrap(err, "segment open");
    }
    
    dvr_enabled = true;
    
    return err;
}

void SrsDvrSegmentPlan::on_unpublish()
{
}

srs_error_t SrsDvrSegmentPlan::on_audio(SrsSharedPtrMessage* shared_audio, SrsFormat* format)
{
    srs_error_t err = srs_success;
    
    if ((err = update_duration(shared_audio)) != srs_success) {
        return srs_error_wrap(err, "update duration");
    }
    
    if ((err = SrsDvrPlan::on_audio(shared_audio, format)) != srs_success) {
        return srs_error_wrap(err, "consume audio");
    }
    
    return err;
}

srs_error_t SrsDvrSegmentPlan::on_video(SrsSharedPtrMessage* shared_video, SrsFormat* format)
{
    srs_error_t err = srs_success;
    
    if ((err = update_duration(shared_video)) != srs_success) {
        return srs_error_wrap(err, "update duration");
    }
    
    if ((err = SrsDvrPlan::on_video(shared_video, format)) != srs_success) {
        return srs_error_wrap(err, "consume video");
    }
    
    return err;
}

srs_error_t SrsDvrSegmentPlan::update_duration(SrsSharedPtrMessage* msg)
{
    srs_error_t err = srs_success;
    
    srs_assert(segment);
    
    // ignore if duration ok.
    SrsFragment* fragment = segment->current();
    if (cduration <= 0 || fragment->duration() < int64_t(srsu2ms(cduration))) {
        return err;
    }
    
    // when wait keyframe, ignore if no frame arrived.
    // @see https://github.com/ossrs/srs/issues/177
    if (wait_keyframe) {
        if (!msg->is_video()) {
            return err;
        }
        
        char* payload = msg->payload;
        int size = msg->size;
        bool is_key_frame = SrsFlvVideo::h264(payload, size) && SrsFlvVideo::keyframe(payload, size) && !SrsFlvVideo::sh(payload, size);
        if (!is_key_frame) {
            return err;
        }
    }
    
    // reap segment
    if ((err = segment->close()) != srs_success) {
        return srs_error_wrap(err, "segment close");
    }
    
    // open new flv file
    if ((err = segment->open()) != srs_success) {
        return srs_error_wrap(err, "segment open");
    }
    
    // update sequence header
    if ((err = hub->on_dvr_request_sh()) != srs_success) {
        return srs_error_wrap(err, "request sh");
    }
    
    return err;
}

srs_error_t SrsDvrSegmentPlan::on_reload_vhost_dvr(string vhost)
{
    srs_error_t err = srs_success;
    
    if (req->vhost != vhost) {
        return err;
    }
    
    wait_keyframe = _srs_config->get_dvr_wait_keyframe(req->vhost);
    
    cduration = _srs_config->get_dvr_duration(req->vhost);
    
    return err;
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

srs_error_t SrsDvr::on_publish()
{
    srs_error_t err = srs_success;
    
    // the dvr for this stream is not actived.
    if (!actived) {
        return err;
    }
    
    if ((err = plan->on_publish()) != srs_success) {
        return srs_error_wrap(err, "publish");
    }
    
    return err;
}

void SrsDvr::on_unpublish()
{
    plan->on_unpublish();
}

srs_error_t SrsDvr::on_meta_data(SrsSharedPtrMessage* metadata)
{
    srs_error_t err = srs_success;
    
    // the dvr for this stream is not actived.
    if (!actived) {
        return err;
    }
    
    if ((err = plan->on_meta_data(metadata)) != srs_success) {
        return srs_error_wrap(err, "metadata");
    }
    
    return err;
}

srs_error_t SrsDvr::on_audio(SrsSharedPtrMessage* shared_audio, SrsFormat* format)
{
    // the dvr for this stream is not actived.
    if (!actived) {
        return srs_success;
    }
    
    return plan->on_audio(shared_audio, format);
}

srs_error_t SrsDvr::on_video(SrsSharedPtrMessage* shared_video, SrsFormat* format)
{
    // the dvr for this stream is not actived.
    if (!actived) {
        return srs_success;
    }
    
    return plan->on_video(shared_video, format);
}

srs_error_t SrsDvr::on_reload_vhost_dvr_apply(string vhost)
{
    srs_error_t err = srs_success;
    
    SrsConfDirective* conf = _srs_config->get_dvr_apply(req->vhost);
    bool v = srs_config_apply_filter(conf, req);
    
    // the apply changed, republish the dvr.
    if (v == actived) {
        return err;
    }
    actived = v;
    
    on_unpublish();
    if (!actived) {
        return err;
    }
    
    if ((err = on_publish()) != srs_success) {
        return srs_error_wrap(err, "on publish");
    }
    if ((err = hub->on_dvr_request_sh()) != srs_success) {
        return srs_error_wrap(err, "request sh");
    }
    
    return err;
}


