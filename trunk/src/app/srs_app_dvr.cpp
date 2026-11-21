//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_app_dvr.hpp>

#include <algorithm>
#include <fcntl.h>
#include <sstream>
using namespace std;

#include <srs_app_config.hpp>
#include <srs_app_factory.hpp>
#include <srs_app_fragment.hpp>
#include <srs_app_http_hooks.hpp>
#include <srs_app_statistic.hpp>
#include <srs_app_utility.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_buffer.hpp>
#include <srs_kernel_codec.hpp>
#include <srs_kernel_file.hpp>
#include <srs_kernel_flv.hpp>
#include <srs_kernel_mp4.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_amf0.hpp>
#include <srs_protocol_json.hpp>
#include <srs_protocol_rtmp_stack.hpp>

#define SRS_FWRITE_CACHE_SIZE 65536

ISrsDvrSegmenter::ISrsDvrSegmenter()
{
}

ISrsDvrSegmenter::~ISrsDvrSegmenter()
{
}

SrsDvrSegmenter::SrsDvrSegmenter()
{
    req_ = NULL;
    jitter_ = NULL;
    plan_ = NULL;
    wait_keyframe_ = true;

    fragment_ = new SrsFragment();
    fs_ = new SrsFileWriter();
    jitter_algorithm_ = SrsRtmpJitterAlgorithmOFF;

    config_ = _srs_config;
}

void SrsDvrSegmenter::assemble()
{
    config_->subscribe(this);
}

SrsDvrSegmenter::~SrsDvrSegmenter()
{
    config_->unsubscribe(this);

    srs_freep(fragment_);
    srs_freep(jitter_);
    srs_freep(fs_);

    config_ = NULL;
}

// Called after fetch_or_create adds the source to the pool (PR 4449). It must not switch
// coroutines, which let other coroutines create duplicate sources for the same stream.
srs_error_t SrsDvrSegmenter::initialize(ISrsDvrPlan *p, ISrsRequest *r)
{
    req_ = r;
    plan_ = p;

    jitter_algorithm_ = (SrsRtmpJitterAlgorithm)config_->get_dvr_time_jitter(req_->vhost_);
    wait_keyframe_ = config_->get_dvr_wait_keyframe(req_->vhost_);

    return srs_success;
}

SrsFragment *SrsDvrSegmenter::current()
{
    return fragment_;
}

srs_error_t SrsDvrSegmenter::open()
{
    srs_error_t err = srs_success;

    if (fs_->is_open()) {
        return err;
    }

    string path = generate_path();
    SrsPath path_util;
    if (path_util.exists(path)) {
        return srs_error_new(ERROR_DVR_CANNOT_APPEND, "DVR can't append to exists path=%s", path.c_str());
    }

    // Each file spans only its own frames.
    srs_freep(fragment_);
    fragment_ = new SrsFragment();
    fragment_->set_path(path);

    if ((err = fragment_->create_dir()) != srs_success) {
        return srs_error_wrap(err, "create dir");
    }

    srs_freep(jitter_);
    jitter_ = new SrsRtmpJitter();

    string tmp_dvr_file = fragment_->tmppath();
    if ((err = fs_->open(tmp_dvr_file)) != srs_success) {
        return srs_error_wrap(err, "open file %s", path.c_str());
    }

    // The encoder writes the file header.
    if ((err = fs_->set_iobuf_size(SRS_FWRITE_CACHE_SIZE)) != srs_success) {
        err = srs_error_wrap(err, "set iobuf size for file %s", path.c_str());
    } else if ((err = open_encoder()) != srs_success) {
        err = srs_error_wrap(err, "open encoder");
    }

    // A file whose header can't be written, such as on a full disk, holds no recording. Close and remove it, so a
    // later close neither renames nor reports it.
    if (err != srs_success) {
        fs_->close();
        srs_error_t r0 = fragment_->unlink_tmpfile();
        if (r0 != srs_success) {
            srs_warn("dvr unlink tmp file failed %s", srs_error_desc(r0).c_str());
            srs_freep(r0);
        }
        return err;
    }

    srs_trace("dvr stream %s to file %s", req_->stream_.c_str(), path.c_str());
    return err;
}

srs_error_t SrsDvrSegmenter::write_metadata(SrsMediaPacket *metadata)
{
    return encode_metadata(metadata);
}

srs_error_t SrsDvrSegmenter::write_audio(SrsMediaPacket *shared_audio, SrsFormat *format)
{
    srs_error_t err = srs_success;

    // TODO: FIXME: Use SrsSharedPtr instead.
    SrsUniquePtr<SrsMediaPacket> audio(shared_audio->copy());

    if ((err = jitter_->correct(audio.get(), jitter_algorithm_)) != srs_success) {
        return srs_error_wrap(err, "jitter");
    }

    // The sequence headers are not media. A new segment gets the cached ones, which keep their old timestamp, so
    // counting them would span the segment from that time.
    bool is_sequence_header = format->is_aac_sequence_header() || format->is_mp3_sequence_header();
    if (!is_sequence_header && (err = on_update_duration(audio.get())) != srs_success) {
        return srs_error_wrap(err, "update duration");
    }

    if ((err = encode_audio(audio.get(), format)) != srs_success) {
        return srs_error_wrap(err, "encode audio");
    }

    return err;
}

srs_error_t SrsDvrSegmenter::write_video(SrsMediaPacket *shared_video, SrsFormat *format)
{
    srs_error_t err = srs_success;

    // TODO: FIXME: Use SrsSharedPtr instead.
    SrsUniquePtr<SrsMediaPacket> video(shared_video->copy());

    if ((err = jitter_->correct(video.get(), jitter_algorithm_)) != srs_success) {
        return srs_error_wrap(err, "jitter");
    }

    if ((err = encode_video(video.get(), format)) != srs_success) {
        return srs_error_wrap(err, "encode video");
    }

    // The sequence header is not media. A new segment gets the cached one, which keeps its old timestamp, so counting
    // it would span the segment from that time.
    if (!format->is_avc_sequence_header() && (err = on_update_duration(video.get())) != srs_success) {
        return srs_error_wrap(err, "update duration");
    }

    return err;
}

srs_error_t SrsDvrSegmenter::close()
{
    srs_error_t err = srs_success;

    if (!fs_->is_open()) {
        return err;
    }

    err = close_encoder();
    fs_->close();
    if (err != srs_success) {
        // An MP4 with no sample, such as a publish with no frame, is never reaped, so remove its tmp file.
        if (srs_error_code(err) == ERROR_MP4_ILLEGAL_MOOV) {
            srs_error_t r0 = fragment_->unlink_tmpfile();
            if (r0 != srs_success) {
                srs_warn("dvr unlink tmp file failed %s", srs_error_desc(r0).c_str());
                srs_freep(r0);
            }
        }
        return srs_error_wrap(err, "close encoder");
    }

    if ((err = fragment_->rename()) != srs_success) {
        return srs_error_wrap(err, "rename fragment");
    }

    // TODO: FIXME: the http callback is async, which will trigger a coroutine switch,
    //          so on_video may be invoked during the http callback, and fail.
    if ((err = plan_->on_reap_segment()) != srs_success) {
        return srs_error_wrap(err, "reap segment");
    }

    return err;
}

string SrsDvrSegmenter::generate_path()
{
    // Such as /data/[vhost]/[app]/[stream]/[2006]/[01]/[02]/[15].[04].[05].[999].flv
    std::string path_config = config_->get_dvr_path(req_->vhost_);

    if (!srs_strings_ends_with(path_config, ".flv", ".mp4")) {
        path_config += "/[stream].[timestamp].flv";
    }

    std::string flv_path = path_config;
    flv_path = srs_path_build_stream(flv_path, req_->vhost_, req_->app_, req_->stream_);
    flv_path = srs_path_build_timestamp(config_, flv_path);

    return flv_path;
}

srs_error_t SrsDvrSegmenter::on_update_duration(SrsMediaPacket *msg)
{
    fragment_->append(msg->timestamp_);
    return srs_success;
}

SrsDvrFlvSegmenter::SrsDvrFlvSegmenter()
{
    enc_ = new SrsFlvTransmuxer();

    duration_offset_ = 0;
    filesize_offset_ = 0;

    has_keyframe_ = false;

    app_factory_ = _srs_app_factory;
}

SrsDvrFlvSegmenter::~SrsDvrFlvSegmenter()
{
    srs_freep(enc_);

    app_factory_ = NULL;
}

srs_error_t SrsDvrFlvSegmenter::refresh_metadata()
{
    srs_error_t err = srs_success;

    if (!duration_offset_ || !filesize_offset_) {
        return err;
    }

    int64_t cur = fs_->tellg();

    SrsUniquePtr<char[]> buf(new char[SrsAmf0Size::number()]);
    SrsBuffer stream(buf.get(), SrsAmf0Size::number());

    SrsUniquePtr<SrsAmf0Any> size(SrsAmf0Any::number((double)cur));

    stream.skip(-1 * stream.pos());
    if ((err = size->write(&stream)) != srs_success) {
        return srs_error_wrap(err, "write filesize");
    }

    if ((err = fs_->seek2(filesize_offset_)) != srs_success) {
        return srs_error_wrap(err, "seek filesize");
    }
    if ((err = fs_->write(buf.get(), SrsAmf0Size::number(), NULL)) != srs_success) {
        return srs_error_wrap(err, "update filesize");
    }

    SrsUniquePtr<SrsAmf0Any> dur(SrsAmf0Any::number((double)srsu2ms(fragment_->duration()) / 1000.0));

    stream.skip(-1 * stream.pos());
    if ((err = dur->write(&stream)) != srs_success) {
        return srs_error_wrap(err, "write duration");
    }

    if ((err = fs_->seek2(duration_offset_)) != srs_success) {
        return srs_error_wrap(err, "seek duration");
    }
    if ((err = fs_->write(buf.get(), SrsAmf0Size::number(), NULL)) != srs_success) {
        return srs_error_wrap(err, "update duration");
    }

    if ((err = fs_->seek2(cur)) != srs_success) {
        return srs_error_wrap(err, "seek end");
    }

    return err;
}

srs_error_t SrsDvrFlvSegmenter::open_encoder()
{
    srs_error_t err = srs_success;

    has_keyframe_ = false;

    duration_offset_ = 0;
    filesize_offset_ = 0;

    srs_freep(enc_);
    enc_ = app_factory_->create_flv_transmuxer();

    if ((err = enc_->initialize(fs_)) != srs_success) {
        return srs_error_wrap(err, "init encoder");
    }

    if ((err = enc_->write_header()) != srs_success) {
        return srs_error_wrap(err, "write flv header");
    }

    return err;
}

srs_error_t SrsDvrFlvSegmenter::encode_metadata(SrsMediaPacket *metadata)
{
    srs_error_t err = srs_success;

    if (duration_offset_ || filesize_offset_) {
        return err;
    }

    SrsBuffer stream(metadata->payload(), metadata->size());

    SrsUniquePtr<SrsAmf0Any> name(SrsAmf0Any::str());
    if ((err = name->read(&stream)) != srs_success) {
        return srs_error_wrap(err, "read name");
    }

    SrsUniquePtr<SrsAmf0Object> obj(SrsAmf0Any::object());
    if ((err = obj->read(&stream)) != srs_success) {
        return srs_error_wrap(err, "read object");
    }

    obj->set("filesize", NULL);
    obj->set("duration", NULL);

    obj->set("service", SrsAmf0Any::str(RTMP_SIG_SRS_SERVER));
    obj->set("filesize", SrsAmf0Any::number(0));
    obj->set("duration", SrsAmf0Any::number(0));

    int size = name->total_size() + obj->total_size();
    SrsUniquePtr<char[]> payload(new char[size]);

    // 11B FLV tag header, 3B object EOF, 1B number marker and 8B number value.
    duration_offset_ = fs_->tellg() + size + 11 - SrsAmf0Size::object_eof() - SrsAmf0Size::number();
    // 2B length and 8B 'duration' key, 1B number marker and 8B number value.
    filesize_offset_ = duration_offset_ - SrsAmf0Size::utf8("duration") - SrsAmf0Size::number();

    SrsBuffer buf(payload.get(), size);

    if ((err = name->write(&buf)) != srs_success) {
        return srs_error_wrap(err, "write name");
    }
    if ((err = obj->write(&buf)) != srs_success) {
        return srs_error_wrap(err, "write object");
    }

    if ((err = enc_->write_metadata(18, payload.get(), size)) != srs_success) {
        return srs_error_wrap(err, "write metadata");
    }

    return err;
}

srs_error_t SrsDvrFlvSegmenter::encode_audio(SrsMediaPacket *audio, SrsFormat *format)
{
    srs_error_t err = srs_success;

    char *payload = audio->payload();
    int size = audio->size();
    if ((err = enc_->write_audio(audio->timestamp_, payload, size)) != srs_success) {
        return srs_error_wrap(err, "write audio");
    }

    return err;
}

srs_error_t SrsDvrFlvSegmenter::encode_video(SrsMediaPacket *video, SrsFormat *format)
{
    srs_error_t err = srs_success;

    char *payload = video->payload();
    int size = video->size();
    bool sh = (format->video_->avc_packet_type_ == SrsVideoAvcFrameTraitSequenceHeader);
    bool keyframe = (!sh && format->video_->frame_type_ == SrsVideoAvcFrameTypeKeyFrame);

    if (keyframe) {
        has_keyframe_ = true;
    }

    if (!has_keyframe_ && !sh && wait_keyframe_) {
        return err;
    }

    if ((err = enc_->write_video(video->timestamp_, payload, size)) != srs_success) {
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
    enc_ = new SrsMp4Encoder();

    app_factory_ = _srs_app_factory;
}

SrsDvrMp4Segmenter::~SrsDvrMp4Segmenter()
{
    srs_freep(enc_);

    app_factory_ = NULL;
}

srs_error_t SrsDvrMp4Segmenter::refresh_metadata()
{
    return srs_success;
}

srs_error_t SrsDvrMp4Segmenter::open_encoder()
{
    srs_error_t err = srs_success;

    srs_freep(enc_);
    enc_ = app_factory_->create_mp4_encoder();

    if ((err = enc_->initialize(fs_)) != srs_success) {
        return srs_error_wrap(err, "init encoder");
    }

    return err;
}

srs_error_t SrsDvrMp4Segmenter::encode_metadata(SrsMediaPacket * /*metadata*/)
{
    return srs_success;
}

srs_error_t SrsDvrMp4Segmenter::encode_audio(SrsMediaPacket *audio, SrsFormat *format)
{
    srs_error_t err = srs_success;

    SrsAudioCodecId sound_format = format->acodec_->id_;
    SrsAudioSampleRate sound_rate = format->acodec_->sound_rate_;
    SrsAudioSampleBits sound_size = format->acodec_->sound_size_;
    SrsAudioChannels channels = format->acodec_->sound_type_;

    SrsAudioAacFrameTrait ct = format->audio_->aac_packet_type_;
    if (ct == SrsAudioAacFrameTraitSequenceHeader || ct == SrsAudioMp3FrameTraitSequenceHeader) {
        enc_->set_audio_codec(sound_format, sound_rate, sound_size, channels);
    }

    uint8_t *sample = (uint8_t *)format->raw_;
    uint32_t nb_sample = (uint32_t)format->nb_raw_;

    uint32_t dts = (uint32_t)audio->timestamp_;
    if ((err = enc_->write_sample(format, SrsMp4HandlerTypeSOUN, 0x00, ct, dts, dts, sample, nb_sample)) != srs_success) {
        return srs_error_wrap(err, "write sample");
    }

    return err;
}

srs_error_t SrsDvrMp4Segmenter::encode_video(SrsMediaPacket *video, SrsFormat *format)
{
    srs_error_t err = srs_success;

    SrsVideoAvcFrameType frame_type = format->video_->frame_type_;
    SrsVideoCodecId codec_id = format->vcodec_->id_;

    // Ignore the info frame, such as the colorInfo metadata of enhanced RTMP: it holds no
    // picture and does not set the sample, so writing it writes stale bytes. HLS and DASH
    // skip it too, while FLV keeps it as a tag.
    if (frame_type == SrsVideoAvcFrameTypeVideoInfoFrame) {
        return err;
    }

    SrsVideoAvcFrameTrait ct = format->video_->avc_packet_type_;
    uint32_t cts = (uint32_t)format->video_->cts_;

    if (ct == SrsVideoAvcFrameTraitSequenceHeader) {
        enc_->vcodec_ = codec_id;
    }

    uint32_t dts = (uint32_t)video->timestamp_;
    uint32_t pts = dts + cts;

    uint8_t *sample = (uint8_t *)format->raw_;
    uint32_t nb_sample = (uint32_t)format->nb_raw_;
    if ((err = enc_->write_sample(format, SrsMp4HandlerTypeVIDE, frame_type, ct, dts, pts, sample, nb_sample)) != srs_success) {
        return srs_error_wrap(err, "write sample");
    }

    return err;
}

srs_error_t SrsDvrMp4Segmenter::close_encoder()
{
    srs_error_t err = srs_success;

    if ((err = enc_->flush()) != srs_success) {
        return srs_error_wrap(err, "flush encoder");
    }

    return err;
}

SrsDvrAsyncCallOnDvr::SrsDvrAsyncCallOnDvr(SrsContextId c, ISrsRequest *r, string p, const SrsStatisticStreamSnapshot &stream)
{
    cid_ = c;
    req_ = r->copy();
    path_ = p;
    stream_ = stream;

    hooks_ = _srs_hooks;
    config_ = _srs_config;
}

SrsDvrAsyncCallOnDvr::~SrsDvrAsyncCallOnDvr()
{
    srs_freep(req_);

    hooks_ = NULL;
    config_ = NULL;
}

srs_error_t SrsDvrAsyncCallOnDvr::call()
{
    srs_error_t err = srs_success;

    if (!config_->get_vhost_http_hooks_enabled(req_->vhost_)) {
        return err;
    }

    // The hooks switch coroutines, during which the directive may be freed, so copy its URLs first.
    // @see https://github.com/ossrs/srs/issues/475
    vector<string> hooks;

    if (true) {
        SrsConfDirective *conf = config_->get_vhost_on_dvr(req_->vhost_);
        if (conf) {
            hooks = conf->args_;
        }
    }

    for (int i = 0; i < (int)hooks.size(); i++) {
        std::string url = hooks.at(i);
        if ((err = hooks_->on_dvr(cid_, url, req_, path_, stream_)) != srs_success) {
            return srs_error_wrap(err, "callback on_dvr %s", url.c_str());
        }
    }

    return err;
}

string SrsDvrAsyncCallOnDvr::to_string()
{
    std::stringstream ss;
    ss << "vhost=" << req_->vhost_ << ", file=" << path_;
    return ss.str();
}

ISrsDvrPlan::ISrsDvrPlan()
{
}

ISrsDvrPlan::~ISrsDvrPlan()
{
}

SrsDvrPlan::SrsDvrPlan()
{
    req_ = NULL;
    hub_ = NULL;

    dvr_enabled_ = false;
    segment_ = NULL;

    async_ = _srs_dvr_async;
    config_ = _srs_config;
    stat_ = _srs_stat;
}

SrsDvrPlan::~SrsDvrPlan()
{
    srs_freep(segment_);
    srs_freep(req_);

    async_ = NULL;
    config_ = NULL;
    stat_ = NULL;
}

// Called after fetch_or_create adds the source to the pool (PR 4449). It must not switch
// coroutines, which let other coroutines create duplicate sources for the same stream.
srs_error_t SrsDvrPlan::initialize(ISrsOriginHub *h, ISrsDvrSegmenter *s, ISrsRequest *r)
{
    srs_error_t err = srs_success;

    hub_ = h;
    req_ = r->copy();
    segment_ = s;

    if ((err = segment_->initialize(this, r)) != srs_success) {
        return srs_error_wrap(err, "segmenter");
    }

    return err;
}

srs_error_t SrsDvrPlan::on_publish(ISrsRequest *r)
{
    // @see https://github.com/ossrs/srs/issues/1613#issuecomment-960623359
    srs_freep(req_);
    req_ = r->copy();

    return srs_success;
}

void SrsDvrPlan::on_unpublish()
{
}

srs_error_t SrsDvrPlan::on_meta_data(SrsMediaPacket *shared_metadata)
{
    srs_error_t err = srs_success;

    if (!dvr_enabled_) {
        return err;
    }

    return segment_->write_metadata(shared_metadata);
}

srs_error_t SrsDvrPlan::on_audio(SrsMediaPacket *shared_audio, SrsFormat *format)
{
    srs_error_t err = srs_success;

    if (!dvr_enabled_) {
        return err;
    }

    if ((err = segment_->write_audio(shared_audio, format)) != srs_success) {
        return srs_error_wrap(err, "write audio");
    }

    return err;
}

srs_error_t SrsDvrPlan::on_video(SrsMediaPacket *shared_video, SrsFormat *format)
{
    srs_error_t err = srs_success;

    if (!dvr_enabled_) {
        return err;
    }

    if ((err = segment_->write_video(shared_video, format)) != srs_success) {
        return srs_error_wrap(err, "write video");
    }

    return err;
}

srs_error_t SrsDvrPlan::on_reap_segment()
{
    srs_error_t err = srs_success;

    SrsContextId cid = _srs_context->get_id();

    SrsFragment *fragment = segment_->current();
    string fullpath = fragment->fullpath();

    // Take the stream now: when the end of a publish closes the file, the stats drop
    // the stream with the publisher, maybe before the queue runs the hook.
    SrsStatisticStreamSnapshot stream(stat_, req_);
    if ((err = async_->execute(new SrsDvrAsyncCallOnDvr(cid, req_, fullpath, stream))) != srs_success) {
        return srs_error_wrap(err, "reap segment");
    }

    return err;
}

// Called after fetch_or_create adds the source to the pool (PR 4449). It must not switch
// coroutines, which let other coroutines create duplicate sources for the same stream.
srs_error_t SrsDvrPlan::create_plan(ISrsAppConfig *config, string vhost, ISrsDvrPlan **pplan)
{
    SrsDvrPlan *dvr_plan = NULL;

    std::string plan = config->get_dvr_plan(vhost);
    if (srs_config_dvr_is_plan_segment(plan)) {
        dvr_plan = new SrsDvrSegmentPlan();
    } else if (srs_config_dvr_is_plan_session(plan)) {
        dvr_plan = new SrsDvrSessionPlan();
    } else {
        return srs_error_new(ERROR_DVR_ILLEGAL_PLAN, "illegal plan=%s, vhost=%s",
                             plan.c_str(), vhost.c_str());
    }

    // The plan reads the config its DVR reads.
    dvr_plan->config_ = config;
    *pplan = dvr_plan;

    return srs_success;
}

SrsDvrSessionPlan::SrsDvrSessionPlan()
{
}

SrsDvrSessionPlan::~SrsDvrSessionPlan()
{
}

srs_error_t SrsDvrSessionPlan::on_publish(ISrsRequest *r)
{
    srs_error_t err = srs_success;

    if ((err = SrsDvrPlan::on_publish(r)) != srs_success) {
        return err;
    }

    if (dvr_enabled_) {
        return err;
    }

    if (!config_->get_dvr_enabled(req_->vhost_)) {
        return err;
    }

    if ((err = segment_->close()) != srs_success) {
        return srs_error_wrap(err, "close segment");
    }

    if ((err = segment_->open()) != srs_success) {
        return srs_error_wrap(err, "open segment");
    }

    dvr_enabled_ = true;

    return err;
}

void SrsDvrSessionPlan::on_unpublish()
{
    if (!dvr_enabled_) {
        return;
    }

    srs_error_t err = segment_->close();
    if (err != srs_success) {
        srs_warn("ignore dvr segment close failed. ret=%d", srs_error_code(err));
        srs_freep(err);
    }

    dvr_enabled_ = false;

    // @see https://github.com/ossrs/srs/issues/1601
    SrsDvrPlan::on_unpublish();
}

SrsDvrSegmentPlan::SrsDvrSegmentPlan()
{
    cduration_ = 0;
    wait_keyframe_ = false;
    reopening_segment_ = false;
    last_timestamp_ = -1;
    has_video_ = false;
}

SrsDvrSegmentPlan::~SrsDvrSegmentPlan()
{
}

srs_error_t SrsDvrSegmentPlan::initialize(ISrsOriginHub *h, ISrsDvrSegmenter *s, ISrsRequest *r)
{
    srs_error_t err = srs_success;

    if ((err = SrsDvrPlan::initialize(h, s, r)) != srs_success) {
        return srs_error_wrap(err, "segment plan");
    }

    wait_keyframe_ = config_->get_dvr_wait_keyframe(req_->vhost_);

    cduration_ = config_->get_dvr_duration(req_->vhost_);

    return srs_success;
}

srs_error_t SrsDvrSegmentPlan::on_publish(ISrsRequest *r)
{
    srs_error_t err = srs_success;

    if ((err = SrsDvrPlan::on_publish(r)) != srs_success) {
        return err;
    }

    if (dvr_enabled_) {
        return err;
    }

    if (!config_->get_dvr_enabled(req_->vhost_)) {
        return err;
    }

    if ((err = segment_->close()) != srs_success) {
        return srs_error_wrap(err, "segment close");
    }

    if ((err = segment_->open()) != srs_success) {
        return srs_error_wrap(err, "segment open");
    }

    last_timestamp_ = -1;
    has_video_ = false;
    dvr_enabled_ = true;

    return err;
}

void SrsDvrSegmentPlan::on_unpublish()
{
    srs_error_t err = srs_success;

    if ((err = segment_->close()) != srs_success) {
        srs_warn("ignore err %s", srs_error_desc(err).c_str());
        srs_freep(err);
    }

    dvr_enabled_ = false;

    // @see https://github.com/ossrs/srs/issues/1601
    SrsDvrPlan::on_unpublish();
}

srs_error_t SrsDvrSegmentPlan::on_audio(SrsMediaPacket *shared_audio, SrsFormat *format)
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

srs_error_t SrsDvrSegmentPlan::on_video(SrsMediaPacket *shared_video, SrsFormat *format)
{
    srs_error_t err = srs_success;

    has_video_ = true;

    if ((err = update_duration(shared_video)) != srs_success) {
        return srs_error_wrap(err, "update duration");
    }

    if ((err = SrsDvrPlan::on_video(shared_video, format)) != srs_success) {
        return srs_error_wrap(err, "consume video");
    }

    return err;
}

srs_error_t SrsDvrSegmentPlan::update_duration(SrsMediaPacket *msg)
{
    srs_error_t err = srs_success;

    // When reopening the segment, never update the duration, because there is actually no media data.
    // @see https://github.com/ossrs/srs/issues/2717
    if (reopening_segment_) {
        return err;
    }

    srs_assert(segment_);

    // The file spans up to this message, which comes after its last message, so
    // with a GOP that divides the duration the keyframe at the duration cuts it.
    SrsFragment *fragment = segment_->current();
    srs_utime_t duration = fragment->duration();
    if (last_timestamp_ >= 0 && msg->timestamp_ > last_timestamp_) {
        duration += (msg->timestamp_ - last_timestamp_) * SRS_UTIME_MILLISECONDS;
    }
    last_timestamp_ = msg->timestamp_;

    if (cduration_ <= 0 || duration < cduration_) {
        return err;
    }

    // A publish with no video has no keyframe to wait for, so any message at the duration
    // cuts it, as HLS and DASH cut pure audio by their duration.
    if (wait_keyframe_ && has_video_) {
        if (!msg->is_video()) {
            return err;
        }

        char *payload = msg->payload();
        int size = msg->size();

        bool codec_ok = SrsFlvVideo::h264(payload, size);
        codec_ok = codec_ok ? true : SrsFlvVideo::hevc(payload, size);

        bool is_key_frame = codec_ok && SrsFlvVideo::keyframe(payload, size) && !SrsFlvVideo::sh(payload, size);
        if (!is_key_frame) {
            return err;
        }
    }

    if ((err = segment_->close()) != srs_success) {
        return srs_error_wrap(err, "segment close");
    }

    if ((err = segment_->open()) != srs_success) {
        return srs_error_wrap(err, "segment open");
    }

    // The hub sends the sequence headers back through on_audio and on_video, which would call this again.
    reopening_segment_ = true;
    err = hub_->on_dvr_request_sh();
    reopening_segment_ = false;
    if (err != srs_success) {
        return srs_error_wrap(err, "request sh");
    }

    return err;
}

ISrsDvr::ISrsDvr()
{
}

ISrsDvr::~ISrsDvr()
{
}

SrsDvr::SrsDvr()
{
    hub_ = NULL;
    plan_ = NULL;
    req_ = NULL;
    actived_ = false;

    config_ = _srs_config;
    app_factory_ = _srs_app_factory;
}

void SrsDvr::assemble()
{
    config_->subscribe(this);
}

SrsDvr::~SrsDvr()
{
    config_->unsubscribe(this);

    srs_freep(plan_);
    srs_freep(req_);

    config_ = NULL;
    app_factory_ = NULL;
}

// Called after fetch_or_create adds the source to the pool (PR 4449). It must not switch
// coroutines, which let other coroutines create duplicate sources for the same stream.
srs_error_t SrsDvr::initialize(ISrsOriginHub *h, ISrsRequest *r)
{
    srs_error_t err = srs_success;

    req_ = r->copy();
    hub_ = h;

    SrsConfDirective *conf = config_->get_dvr_apply(r->vhost_);
    actived_ = srs_config_apply_filter(conf, r);

    srs_freep(plan_);
    if ((err = SrsDvrPlan::create_plan(config_, r->vhost_, &plan_)) != srs_success) {
        return srs_error_wrap(err, "create plan");
    }

    std::string path = config_->get_dvr_path(r->vhost_);
    ISrsDvrSegmenter *segmenter = NULL;
    if (srs_strings_ends_with(path, ".mp4")) {
        segmenter = app_factory_->create_dvr_mp4_segmenter();
    } else {
        segmenter = app_factory_->create_dvr_flv_segmenter();
    }
    segmenter->assemble();

    if ((err = plan_->initialize(hub_, segmenter, r)) != srs_success) {
        return srs_error_wrap(err, "plan initialize");
    }

    return err;
}

srs_error_t SrsDvr::on_publish(ISrsRequest *r)
{
    srs_error_t err = srs_success;

    if (!actived_) {
        return err;
    }

    if ((err = plan_->on_publish(r)) != srs_success) {
        return srs_error_wrap(err, "publish");
    }

    srs_freep(req_);
    req_ = r->copy();

    return err;
}

void SrsDvr::on_unpublish()
{
    plan_->on_unpublish();
}

srs_error_t SrsDvr::on_meta_data(SrsMediaPacket *metadata)
{
    srs_error_t err = srs_success;

    if (!actived_) {
        return err;
    }

    if ((err = plan_->on_meta_data(metadata)) != srs_success) {
        return srs_error_wrap(err, "metadata");
    }

    return err;
}

srs_error_t SrsDvr::on_audio(SrsMediaPacket *shared_audio, SrsFormat *format)
{
    if (!actived_) {
        return srs_success;
    }

    return plan_->on_audio(shared_audio, format);
}

srs_error_t SrsDvr::on_video(SrsMediaPacket *shared_video, SrsFormat *format)
{
    if (!actived_) {
        return srs_success;
    }

    return plan_->on_video(shared_video, format);
}
