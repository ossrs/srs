//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_app_srt_source.hpp>

#include <algorithm>
using namespace std;

#include <srs_app_factory.hpp>
#include <srs_app_rtmp_source.hpp>
#include <srs_app_statistic.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_buffer.hpp>
#include <srs_kernel_flv.hpp>
#include <srs_kernel_kbps.hpp>
#include <srs_kernel_pithy_print.hpp>
#include <srs_kernel_stream.hpp>
#include <srs_kernel_ts.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_format.hpp>
#include <srs_protocol_raw_avc.hpp>
#include <srs_protocol_rtmp_stack.hpp>

#define SRS_SRT_SOURCE_CLEANUP (3 * SRS_UTIME_SECONDS)

SrsSrtPacket::SrsSrtPacket()
{
    shared_buffer_ = NULL;
    actual_buffer_size_ = 0;
}

SrsSrtPacket::~SrsSrtPacket()
{
    srs_freep(shared_buffer_);
}

char *SrsSrtPacket::wrap(int size)
{
    actual_buffer_size_ = size;

    if (shared_buffer_ && shared_buffer_->size() >= size) {
        return shared_buffer_->payload();
    }

    srs_freep(shared_buffer_);
    shared_buffer_ = new SrsMediaPacket();

    char *buf = new char[size];
    shared_buffer_->wrap(buf, size);

    return shared_buffer_->payload();
}

char *SrsSrtPacket::wrap(char *data, int size)
{
    char *buf = wrap(size);
    memcpy(buf, data, size);
    return buf;
}

char *SrsSrtPacket::wrap(SrsMediaPacket *msg)
{
    srs_freep(shared_buffer_);

    shared_buffer_ = msg->copy();
    actual_buffer_size_ = shared_buffer_->size();

    return msg->payload();
}

SrsSrtPacket *SrsSrtPacket::copy()
{
    SrsSrtPacket *cp = new SrsSrtPacket();

    cp->shared_buffer_ = shared_buffer_ ? shared_buffer_->copy() : NULL;
    cp->actual_buffer_size_ = actual_buffer_size_;

    return cp;
}

char *SrsSrtPacket::data()
{
    return shared_buffer_->payload();
}

int SrsSrtPacket::size()
{
    return shared_buffer_->size();
}

ISrsSrtSourceManager::ISrsSrtSourceManager()
{
}

ISrsSrtSourceManager::~ISrsSrtSourceManager()
{
}

SrsSrtSourceManager::SrsSrtSourceManager()
{
    lock_ = srs_mutex_new();
    timer_ = NULL;

    app_factory_ = _srs_app_factory;
}

void SrsSrtSourceManager::assemble()
{
    timer_ = app_factory_->create_hourglass("sources", this, 1 * SRS_UTIME_SECONDS);
}

SrsSrtSourceManager::~SrsSrtSourceManager()
{
    srs_mutex_destroy(lock_);
    srs_freep(timer_);

    app_factory_ = NULL;
}

srs_error_t SrsSrtSourceManager::initialize()
{
    return setup_ticks();
}

srs_error_t SrsSrtSourceManager::setup_ticks()
{
    srs_error_t err = srs_success;

    if ((err = timer_->tick(1, 3 * SRS_UTIME_SECONDS)) != srs_success) {
        return srs_error_wrap(err, "tick");
    }

    if ((err = timer_->start()) != srs_success) {
        return srs_error_wrap(err, "timer");
    }

    return err;
}

srs_error_t SrsSrtSourceManager::notify(int event, srs_utime_t interval, srs_utime_t tick)
{
    srs_error_t err = srs_success;

    std::map<std::string, SrsSharedPtr<SrsSrtSource> >::iterator it;
    for (it = pool_.begin(); it != pool_.end();) {
        SrsSharedPtr<SrsSrtSource> &source = it->second;

        // @see https://github.com/ossrs/srs/issues/713
        if (source->stream_is_dead()) {
            SrsContextId cid = source->source_id();
            if (cid.empty())
                cid = source->pre_source_id();
            srs_trace("SRT: cleanup die source, id=[%s], total=%d", cid.c_str(), (int)pool_.size());
            pool_.erase(it++);
        } else {
            ++it;
        }
    }

    return err;
}

srs_error_t SrsSrtSourceManager::fetch_or_create(ISrsRequest *r, SrsSharedPtr<SrsSrtSource> &pps)
{
    srs_error_t err = srs_success;

    bool created = false;
    // Should never invoke any function during the locking.
    if (true) {
        // The lock guards against a coroutine switch.
        // @bug https://github.com/ossrs/srs/issues/1230
        SrsLocker(&lock_);

        string stream_url = r->get_stream_url();
        std::map<std::string, SrsSharedPtr<SrsSrtSource> >::iterator it = pool_.find(stream_url);
        if (it != pool_.end()) {
            SrsSharedPtr<SrsSrtSource> source = it->second;
            pps = source;
        } else {
            SrsSharedPtr<SrsSrtSource> source(new SrsSrtSource());
            source->assemble();
            srs_trace("new srt source, stream_url=%s, dead=%d", stream_url.c_str(), source->stream_is_dead());
            pps = source;

            pool_[stream_url] = source;
            created = true;
        }
    }

    if (created && (err = pps->initialize(r)) != srs_success) {
        return srs_error_wrap(err, "init source %s", r->get_stream_url().c_str());
    }

    // With origin auth on, the token of the old request may be invalid.
    if (!created) {
        pps->update_auth(r);
    }

    return err;
}

SrsSharedPtr<SrsSrtSource> SrsSrtSourceManager::fetch(ISrsRequest *r)
{
    // The lock guards against a coroutine switch.
    // @bug https://github.com/ossrs/srs/issues/1230
    SrsLocker(&lock_);

    string stream_url = r->get_stream_url();
    std::map<std::string, SrsSharedPtr<SrsSrtSource> >::iterator it = pool_.find(stream_url);

    SrsSharedPtr<SrsSrtSource> source;
    if (it == pool_.end()) {
        return source;
    }

    source = it->second;
    return source;
}

SrsSrtSourceManager *_srs_srt_sources = NULL;

ISrsSrtConsumer::ISrsSrtConsumer()
{
}

ISrsSrtConsumer::~ISrsSrtConsumer()
{
}

SrsSrtConsumer::SrsSrtConsumer(ISrsSrtSource *s)
{
    source_ = s;
    should_update_source_id_ = false;

    mw_wait_ = srs_cond_new();
    mw_min_msgs_ = 0;
    mw_waiting_ = false;
}

SrsSrtConsumer::~SrsSrtConsumer()
{
    source_->on_consumer_destroy(this);

    vector<SrsSrtPacket *>::iterator it;
    for (it = queue_.begin(); it != queue_.end(); ++it) {
        SrsSrtPacket *pkt = *it;
        srs_freep(pkt);
    }

    srs_cond_destroy(mw_wait_);
}

void SrsSrtConsumer::update_source_id()
{
    should_update_source_id_ = true;
}

srs_error_t SrsSrtConsumer::enqueue(SrsSrtPacket *packet)
{
    srs_error_t err = srs_success;

    queue_.push_back(packet);

    if (mw_waiting_) {
        if ((int)queue_.size() >= mw_min_msgs_) {
            srs_cond_signal(mw_wait_);
            mw_waiting_ = false;
            return err;
        }
    }

    return err;
}

srs_error_t SrsSrtConsumer::dump_packet(SrsSrtPacket **ppkt)
{
    srs_error_t err = srs_success;

    if (should_update_source_id_) {
        srs_trace("update source_id=%s/%s", source_->source_id().c_str(), source_->pre_source_id().c_str());
        should_update_source_id_ = false;
    }

    // TODO: FIXME: Refine performance by ring buffer.
    if (!queue_.empty()) {
        *ppkt = queue_.front();
        queue_.erase(queue_.begin());
    }

    return err;
}

bool SrsSrtConsumer::wait(int nb_msgs, srs_utime_t timeout)
{
    mw_min_msgs_ = nb_msgs;

    if ((int)queue_.size() >= mw_min_msgs_) {
        return true;
    }

    mw_waiting_ = true;

    srs_cond_timedwait(mw_wait_, timeout);

    return (int)queue_.size() >= mw_min_msgs_;
}

SrsSrtFrameBuilder::SrsSrtFrameBuilder(ISrsFrameTarget *target)
{
    ts_ctx_ = new SrsTsContext();

    sps_pps_change_ = false;
    sps_ = "";
    pps_ = "";

    req_ = NULL;
    frame_target_ = target;

    video_streamid_ = 1;
    audio_streamid_ = 2;

    pp_audio_duration_ = new SrsAlonePithyPrint();
}

SrsSrtFrameBuilder::~SrsSrtFrameBuilder()
{
    srs_freep(ts_ctx_);
    srs_freep(req_);

    srs_freep(pp_audio_duration_);
}

void SrsSrtFrameBuilder::assemble()
{
    pp_audio_duration_->assemble();
}

srs_error_t SrsSrtFrameBuilder::on_publish()
{
    return srs_success;
}

srs_error_t SrsSrtFrameBuilder::on_srt_packet(SrsSrtPacket *pkt)
{
    srs_error_t err = srs_success;

    char *buf = pkt->data();
    int nb_buf = pkt->size();

    int nb_packet = nb_buf / SRS_TS_PACKET_SIZE;
    for (int i = 0; i < nb_packet; i++) {
        char *p = buf + (i * SRS_TS_PACKET_SIZE);
        SrsUniquePtr<SrsBuffer> stream(new SrsBuffer(p, SRS_TS_PACKET_SIZE));

        // UDP jitter, packet loss or a wrong sequence may cause a video glitch. SRT handles it, see tlpktdrop at
        // https://ossrs.io/lts/en-us/docs/v7/doc/srt
        // TODO: FIXME: Refine it, a frame the target refuses, such as HLS failing with hls_on_error disconnect, is
        // logged and skipped as a bad TS packet, so the encoder is not disconnected.
        if ((err = ts_ctx_->decode(stream.get(), this)) != srs_success) {
            srs_warn("parse ts packet err=%s", srs_error_desc(err).c_str());
            srs_freep(err);
            continue;
        }
    }

    return err;
}

void SrsSrtFrameBuilder::on_unpublish()
{
}

srs_error_t SrsSrtFrameBuilder::initialize(ISrsRequest *req)
{
    srs_error_t err = srs_success;

    // TODO: FIXME: check srt2rtmp enable in config.
    req_ = req->copy();

    return err;
}

srs_error_t SrsSrtFrameBuilder::on_ts_message(SrsTsMessage *msg)
{
    srs_error_t err = srs_success;

    // @see https://github.com/ossrs/srs/issues/740
    if (msg->channel_->apply_ == SrsTsPidApplyAudio && msg->sid_ == SrsTsPESStreamIdPrivateStream1) {
        msg->sid_ = SrsTsPESStreamIdAudioCommon;
    }

    if (msg->stream_number() != 0) {
        return srs_error_new(ERROR_STREAM_CASTER_TS_ES, "ts: unsupported stream format, sid=%#x(%s-%d)",
                             msg->sid_, msg->is_audio() ? "A" : msg->is_video() ? "V"
                                                                                : "N",
                             msg->stream_number());
    }

    if (msg->channel_->stream_ != SrsTsStreamVideoH264 && msg->channel_->stream_ != SrsTsStreamVideoHEVC && msg->channel_->stream_ != SrsTsStreamAudioAAC) {
        return srs_error_new(ERROR_STREAM_CASTER_TS_CODEC, "ts: unsupported stream codec=%d", msg->channel_->stream_);
    }

    SrsBuffer avs(msg->payload_->bytes(), msg->payload_->length());

    if (msg->channel_->stream_ == SrsTsStreamVideoH264) {
        if ((err = on_ts_video_avc(msg, &avs)) != srs_success) {
            return srs_error_wrap(err, "ts: consume video");
        }
    }
    if (msg->channel_->stream_ == SrsTsStreamAudioAAC) {
        if ((err = on_ts_audio(msg, &avs)) != srs_success) {
            return srs_error_wrap(err, "ts: consume audio");
        }
    }

    // TODO: FIXME: implements other codec?
    if (msg->channel_->stream_ == SrsTsStreamVideoHEVC) {
        if ((err = on_ts_video_hevc(msg, &avs)) != srs_success) {
            return srs_error_wrap(err, "ts: consume hevc video");
        }
    }

    return err;
}

srs_error_t SrsSrtFrameBuilder::on_ts_video_avc(SrsTsMessage *msg, SrsBuffer *avs)
{
    srs_error_t err = srs_success;

    vector<pair<char *, int> > ipb_frames;

    SrsUniquePtr<SrsRawH264Stream> avc(new SrsRawH264Stream());

    while (!avs->empty()) {
        char *frame = NULL;
        int frame_size = 0;
        if ((err = avc->annexb_demux(avs, &frame, &frame_size)) != srs_success) {
            return srs_error_wrap(err, "demux annexb");
        }

        if (frame == NULL || frame_size == 0) {
            continue;
        }

        if (avc->is_sps(frame, frame_size)) {
            std::string sps;
            if ((err = avc->sps_demux(frame, frame_size, sps)) != srs_success) {
                return srs_error_wrap(err, "demux sps");
            }

            if (!sps.empty() && sps_ != sps) {
                sps_pps_change_ = true;
            }

            sps_ = sps;
            continue;
        }

        if (avc->is_pps(frame, frame_size)) {
            std::string pps;
            if ((err = avc->pps_demux(frame, frame_size, pps)) != srs_success) {
                return srs_error_wrap(err, "demux pps");
            }

            if (!pps.empty() && pps_ != pps) {
                sps_pps_change_ = true;
            }

            pps_ = pps;
            continue;
        }

        ipb_frames.push_back(make_pair(frame, frame_size));
    }

    if ((err = check_sps_pps_change(msg)) != srs_success) {
        return srs_error_wrap(err, "check sps pps");
    }

    return on_h264_frame(msg, ipb_frames);
}

srs_error_t SrsSrtFrameBuilder::check_sps_pps_change(SrsTsMessage *msg)
{
    srs_error_t err = srs_success;

    if (!sps_pps_change_) {
        return err;
    }

    if (sps_.empty() || pps_.empty()) {
        return srs_error_new(ERROR_SRT_TO_RTMP_EMPTY_SPS_PPS, "sps or pps empty");
    }

    sps_pps_change_ = false;

    uint32_t dts = (uint32_t)(msg->dts_ / 90);

    std::string sh;
    SrsUniquePtr<SrsRawH264Stream> avc(new SrsRawH264Stream());

    if ((err = avc->mux_sequence_header(sps_, pps_, sh)) != srs_success) {
        return srs_error_wrap(err, "mux sequence header");
    }

    char *flv = NULL;
    int nb_flv = 0;
    if ((err = avc->mux_avc2flv(sh, SrsVideoAvcFrameTypeKeyFrame, SrsVideoAvcFrameTraitSequenceHeader, dts, dts, &flv, &nb_flv)) != srs_success) {
        return srs_error_wrap(err, "avc to flv");
    }

    SrsMessageHeader header;
    header.initialize_video(nb_flv, dts, video_streamid_);
    SrsRtmpCommonMessage rtmp;
    if ((err = rtmp.create(&header, flv, nb_flv)) != srs_success) {
        return srs_error_wrap(err, "create rtmp");
    }

    SrsMediaPacket frame;
    rtmp.to_msg(&frame);

    if ((err = frame_target_->on_frame(&frame)) != srs_success) {
        return srs_error_wrap(err, "srt to rtmp sps/pps");
    }

    return err;
}

srs_error_t SrsSrtFrameBuilder::on_h264_frame(SrsTsMessage *msg, vector<pair<char *, int> > &ipb_frames)
{
    srs_error_t err = srs_success;

    if (ipb_frames.empty()) {
        return srs_error_new(ERROR_SRT_CONN, "empty frame");
    }

    bool is_keyframe = false;

    uint32_t dts = (uint32_t)(msg->dts_ / 90);
    uint32_t pts = (uint32_t)(msg->pts_ / 90);
    int32_t cts = pts - dts;

    int frame_size = 5; // 5bytes video tag header
    for (size_t i = 0; i != ipb_frames.size(); ++i) {
        // 4 bytes for nalu length.
        frame_size += 4 + ipb_frames[i].second;
        if (((SrsAvcNaluType)(ipb_frames[i].first[0] & 0x1f)) == SrsAvcNaluTypeIDR) {
            is_keyframe = true;
        }
    }

    SrsRtmpCommonMessage rtmp;
    rtmp.header_.initialize_video(frame_size, dts, video_streamid_);
    rtmp.create_payload(frame_size);
    SrsBuffer payload(rtmp.payload(), rtmp.size());
    if (is_keyframe) {
        payload.write_1bytes(0x17); // type(4 bits): key frame; code(4bits): avc
    } else {
        payload.write_1bytes(0x27); // type(4 bits): inter frame; code(4bits): avc
    }
    payload.write_1bytes(0x01); // avc_type: nalu
    payload.write_3bytes(cts);  // composition time

    for (size_t i = 0; i != ipb_frames.size(); ++i) {
        char *nal = ipb_frames[i].first;
        int nal_size = ipb_frames[i].second;

        payload.write_4bytes(nal_size);
        payload.write_bytes(nal, nal_size);
    }

    SrsMediaPacket frame;
    rtmp.to_msg(&frame);

    if ((err = frame_target_->on_frame(&frame)) != srs_success) {
        return srs_error_wrap(err, "srt ts video to rtmp");
    }

    return err;
}

srs_error_t SrsSrtFrameBuilder::on_ts_video_hevc(SrsTsMessage *msg, SrsBuffer *avs)
{
    srs_error_t err = srs_success;

    vector<pair<char *, int> > ipb_frames;
    SrsUniquePtr<SrsRawHEVCStream> hevc(new SrsRawHEVCStream());

    std::vector<std::string> hevc_pps;
    while (!avs->empty()) {
        char *frame = NULL;
        int frame_size = 0;
        if ((err = hevc->annexb_demux(avs, &frame, &frame_size)) != srs_success) {
            return srs_error_wrap(err, "demux hevc annexb");
        }

        if (frame == NULL || frame_size == 0) {
            continue;
        }

        if (hevc->is_vps(frame, frame_size)) {
            std::string vps;
            if ((err = hevc->vps_demux(frame, frame_size, vps)) != srs_success) {
                return srs_error_wrap(err, "demux vps");
            }

            if (!vps.empty() && hevc_vps_ != vps) {
                vps_sps_pps_change_ = true;
            }

            hevc_vps_ = vps;
            continue;
        }

        if (hevc->is_sps(frame, frame_size)) {
            std::string sps;
            if ((err = hevc->sps_demux(frame, frame_size, sps)) != srs_success) {
                return srs_error_wrap(err, "demux sps");
            }

            if (!sps.empty() && hevc_sps_ != sps) {
                vps_sps_pps_change_ = true;
            }

            hevc_sps_ = sps;
            continue;
        }

        if (hevc->is_pps(frame, frame_size)) {
            std::string pps;
            if ((err = hevc->pps_demux(frame, frame_size, pps)) != srs_success) {
                return srs_error_wrap(err, "demux pps");
            }

            hevc_pps.push_back(pps);
            continue;
        }

        ipb_frames.push_back(make_pair(frame, frame_size));
    }

    // TS repeats the parameter sets before each keyframe, so only PPS that differ
    // from the last ones are a change.
    if (!hevc_pps.empty()) {
        if (hevc_pps_ != hevc_pps) {
            vps_sps_pps_change_ = true;
        }
        hevc_pps_ = hevc_pps;
    }

    if ((err = check_vps_sps_pps_change(msg)) != srs_success) {
        return srs_error_wrap(err, "check vps sps pps");
    }

    return on_hevc_frame(msg, ipb_frames);
}

srs_error_t SrsSrtFrameBuilder::check_vps_sps_pps_change(SrsTsMessage *msg)
{
    srs_error_t err = srs_success;

    if (!vps_sps_pps_change_) {
        return err;
    }

    if (hevc_vps_.empty() || hevc_sps_.empty() || hevc_pps_.empty()) {
        return err;
    }

    vps_sps_pps_change_ = false;

    uint32_t dts = (uint32_t)(msg->dts_ / 90);
    uint32_t pts = (uint32_t)(msg->pts_ / 90);

    std::string sh;
    SrsUniquePtr<SrsRawHEVCStream> hevc(new SrsRawHEVCStream());

    if ((err = hevc->mux_sequence_header(hevc_vps_, hevc_sps_, hevc_pps_, sh)) != srs_success) {
        return srs_error_wrap(err, "mux sequence header");
    }

    char *flv = NULL;
    int nb_flv = 0;
    if ((err = hevc->mux_hevc2flv_enhanced(sh,
                                           SrsVideoAvcFrameTypeKeyFrame,
                                           SrsVideoHEVCFrameTraitPacketTypeSequenceStart,
                                           dts,
                                           pts,
                                           &flv,
                                           &nb_flv)) != srs_success) {
        return srs_error_wrap(err, "hevc sh to flv");
    }

    SrsMessageHeader header;
    header.initialize_video(nb_flv, dts, video_streamid_);
    SrsRtmpCommonMessage rtmp;
    if ((err = rtmp.create(&header, flv, nb_flv)) != srs_success) {
        return srs_error_wrap(err, "create rtmp");
    }

    SrsMediaPacket frame;
    rtmp.to_msg(&frame);

    if ((err = frame_target_->on_frame(&frame)) != srs_success) {
        return srs_error_wrap(err, "srt to rtmp vps/sps/pps");
    }

    return err;
}

srs_error_t SrsSrtFrameBuilder::on_hevc_frame(SrsTsMessage *msg, vector<pair<char *, int> > &ipb_frames)
{
    srs_error_t err = srs_success;

    if (ipb_frames.empty()) {
        return err;
    }

    uint32_t dts = (uint32_t)(msg->dts_ / 90);
    uint32_t pts = (uint32_t)(msg->pts_ / 90);
    int32_t cts = pts - dts;

    SrsVideoAvcFrameType frame_type = SrsVideoAvcFrameTypeInterFrame;

    // A frame not shown at its decode time, such as a B-frame, carries its
    // composition time in a CodedFrames tag, 3 bytes after the fourcc.
    int8_t packet_type = SrsVideoHEVCFrameTraitPacketTypeCodedFramesX;
    if (cts != 0) {
        packet_type = SrsVideoHEVCFrameTraitPacketTypeCodedFrames;
    }

    // 5bytes video tag header, and 3bytes composition time for CodedFrames.
    int frame_size = (packet_type == SrsVideoHEVCFrameTraitPacketTypeCodedFrames) ? 8 : 5;
    for (size_t i = 0; i != ipb_frames.size(); ++i) {
        // 4 bytes for nalu length.
        frame_size += 4 + ipb_frames[i].second;
        SrsHevcNaluType nalu_type = SrsHevcNaluTypeParse(ipb_frames[i].first[0]);
        if (SrsIsIRAP(nalu_type)) {
            frame_type = SrsVideoAvcFrameTypeKeyFrame;
        }
    }

    SrsRtmpCommonMessage rtmp;
    rtmp.header_.initialize_video(frame_size, dts, video_streamid_);
    rtmp.create_payload(frame_size);
    SrsBuffer payload(rtmp.payload(), rtmp.size());

    // @see: https://veovera.org/docs/enhanced/enhanced-rtmp-v1.pdf, page 8
    payload.write_1bytes(SRS_FLV_IS_EX_HEADER | (frame_type << 4) | packet_type);
    payload.write_4bytes(0x68766331); // 'h' 'v' 'c' '1'
    if (packet_type == SrsVideoHEVCFrameTraitPacketTypeCodedFrames) {
        payload.write_3bytes(cts); // composition time
    }

    for (size_t i = 0; i != ipb_frames.size(); ++i) {
        char *nal = ipb_frames[i].first;
        int nal_size = ipb_frames[i].second;

        payload.write_4bytes(nal_size);
        payload.write_bytes(nal, nal_size);
    }

    SrsMediaPacket frame;
    rtmp.to_msg(&frame);

    if ((err = frame_target_->on_frame(&frame)) != srs_success) {
        return srs_error_wrap(err, "srt ts hevc video to rtmp");
    }

    return err;
}

srs_error_t SrsSrtFrameBuilder::on_ts_audio(SrsTsMessage *msg, SrsBuffer *avs)
{
    srs_error_t err = srs_success;

    SrsUniquePtr<SrsRawAacStream> aac(new SrsRawAacStream());

    uint32_t pts = (uint32_t)(msg->pts_ / 90);

    int frame_idx = 0;
    int duration_ms = 0;

    while (!avs->empty()) {
        char *frame = NULL;
        int frame_size = 0;
        SrsRawAacStreamCodec codec;
        if ((err = aac->adts_demux(avs, &frame, &frame_size, codec)) != srs_success) {
            return srs_error_wrap(err, "demux adts");
        }

        if (frame_size <= 0) {
            continue;
        }

        std::string sh;
        if ((err = aac->mux_sequence_header(&codec, sh)) != srs_success) {
            return srs_error_wrap(err, "mux sequence header");
        }

        if (!sh.empty() && sh != audio_sh_) {
            audio_sh_ = sh;
            audio_sh_change_ = true;
        }

        // A PES packet may hold several AAC frames with one timestamp, so each frame's timestamp comes from the
        // frame's sample rate, not the FLV sound rate, which has no 48kHz, or 44.1kHz when the index is invalid.
        int sample_rate = 44100;
        if (codec.sampling_frequency_index_ < SrsAAcSampleRateNumbers && srs_aac_srates[codec.sampling_frequency_index_] > 0) {
            sample_rate = srs_aac_srates[codec.sampling_frequency_index_];
        }
        uint32_t frame_pts = (double)pts + (frame_idx * (1024.0 * 1000.0 / sample_rate));
        duration_ms += 1024.0 * 1000.0 / sample_rate;
        ++frame_idx;

        if ((err = check_audio_sh_change(msg, frame_pts)) != srs_success) {
            return srs_error_wrap(err, "audio sh");
        }

        if ((err = on_aac_frame(msg, frame_pts, frame, frame_size)) != srs_success) {
            return srs_error_wrap(err, "audio frame");
        }
    }

    pp_audio_duration_->elapse();

    if ((duration_ms >= 200) && pp_audio_duration_->can_print()) {
        // MPEG-TS may merge several audio frames into one PES packet, which may cause high latency and AV sync errors.
        // @see https://github.com/ossrs/srs/issues/3164
        srs_warn("srt to rtmp, audio duration=%dms too large, audio frames=%d, may cause high latency and AV synchronization errors, "
                 "read https://ossrs.io/lts/en-us/docs/v7/doc/srt#ffmpeg-push-srt-stream",
                 duration_ms, frame_idx);
    }

    return err;
}

srs_error_t SrsSrtFrameBuilder::check_audio_sh_change(SrsTsMessage *msg, uint32_t pts)
{
    srs_error_t err = srs_success;

    if (!audio_sh_change_) {
        return err;
    }

    audio_sh_change_ = false;

    int rtmp_len = audio_sh_.size() + 2;

    SrsRtmpCommonMessage rtmp;
    rtmp.header_.initialize_audio(rtmp_len, pts, audio_streamid_);
    rtmp.create_payload(rtmp_len);

    SrsBuffer stream(rtmp.payload(), rtmp_len);
    uint8_t aac_flag = (SrsAudioCodecIdAAC << 4) | (SrsAudioSampleRate44100 << 2) | (SrsAudioSampleBits16bit << 1) | SrsAudioChannelsStereo;
    stream.write_1bytes(aac_flag);
    stream.write_1bytes(0);
    stream.write_bytes((char *)audio_sh_.data(), audio_sh_.size());

    SrsMediaPacket frame;
    rtmp.to_msg(&frame);

    if ((err = frame_target_->on_frame(&frame)) != srs_success) {
        return srs_error_wrap(err, "srt to rtmp audio sh");
    }

    return err;
}

srs_error_t SrsSrtFrameBuilder::on_aac_frame(SrsTsMessage *msg, uint32_t pts, char *data, int data_size)
{
    srs_error_t err = srs_success;

    int rtmp_len = data_size + 2 /* 2 bytes of FLV audio tag header */;

    SrsRtmpCommonMessage rtmp;
    rtmp.header_.initialize_audio(rtmp_len, pts, audio_streamid_);
    rtmp.create_payload(rtmp_len);

    SrsBuffer stream(rtmp.payload(), rtmp_len);
    uint8_t aac_flag = (SrsAudioCodecIdAAC << 4) | (SrsAudioSampleRate44100 << 2) | (SrsAudioSampleBits16bit << 1) | SrsAudioChannelsStereo;
    stream.write_1bytes(aac_flag);
    stream.write_1bytes(1);
    stream.write_bytes(data, data_size);

    SrsMediaPacket frame;
    rtmp.to_msg(&frame);

    if ((err = frame_target_->on_frame(&frame)) != srs_success) {
        return srs_error_wrap(err, "srt to rtmp audio sh");
    }

    return err;
}

ISrsSrtFormat::ISrsSrtFormat()
{
}

ISrsSrtFormat::~ISrsSrtFormat()
{
}

SrsSrtFormat::SrsSrtFormat()
{
    req_ = NULL;
    ts_ctx_ = new SrsTsContext();
    format_ = new SrsRtmpFormat();
    video_codec_reported_ = false;
    audio_codec_reported_ = false;
    nn_video_frames_ = 0;
    nn_audio_frames_ = 0;

    stat_ = _srs_stat;
}

SrsSrtFormat::~SrsSrtFormat()
{
    req_ = NULL;
    srs_freep(ts_ctx_);
    srs_freep(format_);

    stat_ = NULL;
}

srs_error_t SrsSrtFormat::initialize(ISrsRequest *req)
{
    srs_error_t err = srs_success;

    req_ = req;

    return err;
}

srs_error_t SrsSrtFormat::on_srt_packet(SrsSrtPacket *pkt)
{
    srs_error_t err = srs_success;

    char *buf = pkt->data();
    int nb_buf = pkt->size();

    int nb_packet = nb_buf / SRS_TS_PACKET_SIZE;
    for (int i = 0; i < nb_packet; i++) {
        char *p = buf + (i * SRS_TS_PACKET_SIZE);

        // Decode the TS only until both codecs are reported, which avoids unnecessary
        // TS decoding and log flooding from unrecognized stream types.
        if (!video_codec_reported_ || !audio_codec_reported_) {
            SrsBuffer stream(p, SRS_TS_PACKET_SIZE);

            if ((err = ts_ctx_->decode(&stream, this)) != srs_success) {
                srs_warn("srt format parse ts packet err=%s", srs_error_desc(err).c_str());
                srs_freep(err);
            }
        }

        count_ts_frame(p);
    }

    return err;
}

void SrsSrtFormat::count_ts_frame(char *p)
{
    // Each PES, a frame, starts in a TS packet with the payload_unit_start_indicator.
    if ((uint8_t)p[0] != 0x47 || (p[1] & 0x40) == 0) {
        return;
    }

    // The PMT, decoded while the codecs are found, maps the PID to its stream type.
    int pid = ((p[1] & 0x1f) << 8) | (uint8_t)p[2];
    SrsTsChannel *channel = ts_ctx_->get(pid);
    if (!channel) {
        return;
    }

    if (channel->stream_ == SrsTsStreamVideoH264 || channel->stream_ == SrsTsStreamVideoHEVC) {
        update_ts_message_stats(false);
    } else if (channel->stream_ == SrsTsStreamAudioAAC) {
        update_ts_message_stats(true);
    }
}

void SrsSrtFormat::update_ts_message_stats(bool is_audio)
{
    srs_error_t err = srs_success;

    if (is_audio) {
        ++nn_audio_frames_;
    } else {
        ++nn_video_frames_;
    }

    if (nn_video_frames_ > 288) {
        if ((err = stat_->on_video_frames(req_, nn_video_frames_)) != srs_success) {
            srs_warn("SRT: stat video frames err %s", srs_error_desc(err).c_str());
            srs_freep(err);
        }
        nn_video_frames_ = 0;
    }

    if (nn_audio_frames_ > 288) {
        if ((err = stat_->on_audio_frames(req_, nn_audio_frames_)) != srs_success) {
            srs_warn("SRT: stat audio frames err %s", srs_error_desc(err).c_str());
            srs_freep(err);
        }
        nn_audio_frames_ = 0;
    }
}

srs_error_t SrsSrtFormat::on_ts_message(SrsTsMessage *msg)
{
    srs_error_t err = srs_success;

    if (msg->channel_->stream_ == SrsTsStreamVideoH264 || msg->channel_->stream_ == SrsTsStreamVideoHEVC) {
        if (video_codec_reported_) {
            return err;
        }

        // A codec left by an earlier message whose SPS failed to parse has no size, so report only the codec this
        // message parsed.
        bool parsed = false;
        if ((err = parse_video_codec(msg, parsed)) != srs_success) {
            return srs_error_wrap(err, "parse video codec");
        }

        if (parsed && format_->vcodec_) {
            video_codec_reported_ = true;

            SrsVideoCodecConfig *c = format_->vcodec_;
            if ((err = stat_->on_video_info(req_, c->id_, c->avc_profile_, c->avc_level_, c->width_, c->height_)) != srs_success) {
                return srs_error_wrap(err, "stat video info");
            }

            srs_trace("srt: parsed %s codec, profile=%s, level=%s, %dx%d",
                      srs_video_codec_id2str(c->id_).c_str(),
                      srs_avc_profile2str(c->avc_profile_).c_str(), srs_avc_level2str(c->avc_level_).c_str(),
                      c->width_, c->height_);
        }
    } else if (msg->channel_->stream_ == SrsTsStreamAudioAAC) {
        if (audio_codec_reported_) {
            return err;
        }

        if ((err = parse_audio_codec(msg)) != srs_success) {
            return srs_error_wrap(err, "parse audio codec");
        }

        if (format_->acodec_) {
            audio_codec_reported_ = true;

            SrsAudioCodecConfig *c = format_->acodec_;
            SrsAudioChannels channels = c->sound_type_;
            if (c->id_ == SrsAudioCodecIdAAC) {
                channels = (c->aac_channels_ == 1) ? SrsAudioChannelsMono : SrsAudioChannelsStereo;
            }
            if ((err = stat_->on_audio_info(req_, c->id_, c->sound_rate_, channels, c->aac_object_)) != srs_success) {
                return srs_error_wrap(err, "stat audio info");
            }

            srs_trace("srt: parsed %s codec, sample_rate=%dHZ, channels=%d, profile=%s",
                      srs_audio_codec_id2str(c->id_).c_str(),
                      srs_audio_sample_rate2number(c->sound_rate_), (int)channels + 1,
                      srs_aac_object2str(c->aac_object_).c_str());
        }
    }

    return err;
}

srs_error_t SrsSrtFormat::parse_video_codec(SrsTsMessage *msg, bool &parsed)
{
    srs_error_t err = srs_success;

    SrsBuffer avs(msg->payload_->bytes(), msg->payload_->length());

    // TODO: FIXME: Implement HEVC codec parsing similar to H.264
    if (msg->channel_->stream_ == SrsTsStreamVideoHEVC) {
        video_codec_reported_ = true;
        return err;
    }

    if (msg->channel_->stream_ == SrsTsStreamVideoH264) {
        SrsUniquePtr<SrsRawH264Stream> avc(new SrsRawH264Stream());
        std::string sps, pps;

        while (!avs.empty()) {
            char *data = NULL;
            int data_size = 0;
            if ((err = avc->annexb_demux(&avs, &data, &data_size)) != srs_success) {
                return srs_error_wrap(err, "demux annexb");
            }

            if (data == NULL || data_size == 0) {
                continue;
            }

            if (avc->is_sps(data, data_size)) {
                if ((err = avc->sps_demux(data, data_size, sps)) != srs_success) {
                    return srs_error_wrap(err, "demux sps");
                }
            }

            if (avc->is_pps(data, data_size)) {
                if ((err = avc->pps_demux(data, data_size, pps)) != srs_success) {
                    return srs_error_wrap(err, "demux pps");
                }
            }

            if (sps.empty() || pps.empty()) {
                continue;
            }

            std::string sh;
            if ((err = avc->mux_sequence_header(sps, pps, sh)) != srs_success) {
                return srs_error_wrap(err, "mux sequence header");
            }

            char *flv = NULL;
            int nb_flv = 0;
            uint32_t dts = 0;
            if ((err = avc->mux_avc2flv(sh, SrsVideoAvcFrameTypeKeyFrame, SrsVideoAvcFrameTraitSequenceHeader, dts, dts, &flv, &nb_flv)) != srs_success) {
                return srs_error_wrap(err, "avc to flv");
            }

            SrsMediaPacket frame;
            frame.wrap(flv, nb_flv);

            if ((err = format_->on_video(&frame)) != srs_success) {
                return srs_error_wrap(err, "format parse video");
            }
            parsed = true;

            return err;
        }
    }

    return err;
}

srs_error_t SrsSrtFormat::parse_audio_codec(SrsTsMessage *msg)
{
    srs_error_t err = srs_success;

    SrsBuffer avs(msg->payload_->bytes(), msg->payload_->length());

    if (msg->channel_->stream_ == SrsTsStreamAudioAAC) {
        SrsUniquePtr<SrsRawAacStream> aac(new SrsRawAacStream());

        char *frame = NULL;
        int frame_size = 0;
        SrsRawAacStreamCodec codec;
        if ((err = aac->adts_demux(&avs, &frame, &frame_size, codec)) != srs_success) {
            return srs_error_wrap(err, "demux adts");
        }

        if (frame_size <= 0) {
            return err;
        }

        std::string sh;
        if ((err = aac->mux_sequence_header(&codec, sh)) != srs_success) {
            return srs_error_wrap(err, "mux sequence header");
        }

        int rtmp_len = sh.size() + 2;
        char *buf = new char[rtmp_len];
        SrsBuffer stream(buf, rtmp_len);
        uint8_t aac_flag = (SrsAudioCodecIdAAC << 4) | (codec.sound_rate_ << 2) | (codec.sound_size_ << 1) | codec.sound_type_;
        stream.write_1bytes(aac_flag);
        stream.write_1bytes(0);
        stream.write_bytes((char *)sh.data(), sh.size());

        SrsMediaPacket frame_pkt;
        frame_pkt.wrap(buf, rtmp_len);

        if ((err = format_->on_audio(&frame_pkt)) != srs_success) {
            return srs_error_wrap(err, "format parse audio");
        }
    }

    return err;
}

ISrsSrtSource::ISrsSrtSource()
{
}

ISrsSrtSource::~ISrsSrtSource()
{
}

SrsSrtSource::SrsSrtSource()
{
    req_ = NULL;
    can_publish_ = true;
    srt_bridge_ = NULL;
    stream_die_at_ = 0;

    stat_ = _srs_stat;
    format_ = new SrsSrtFormat();
    clk_ = _srs_clock;
}

void SrsSrtSource::assemble()
{
    // Otherwise stream_is_dead() takes a new source as dead at once.
    // @see https://github.com/ossrs/srs/issues/4449
    stream_die_at_ = clk_->now();
}

SrsSrtSource::~SrsSrtSource()
{
    // Never free the consumers; they are auto freed.
    consumers_.clear();

    srs_freep(srt_bridge_);
    srs_freep(req_);
    srs_freep(format_);

    SrsContextId cid = source_id_;
    if (cid.empty())
        cid = pre_source_id_;
    srs_trace("free srt source id=[%s]", cid.c_str());

    stat_ = NULL;
    clk_ = NULL;
}

// Called after fetch_or_create adds the source to the pool (see PR 4449), which stops coroutines from creating
// duplicate sources for one stream. So it MUST NOT switch coroutines before the fields are set up, or other
// coroutines may get the source from the pool half set up, and crash.
srs_error_t SrsSrtSource::initialize(ISrsRequest *r)
{
    srs_error_t err = srs_success;

    req_ = r->copy();

    if ((err = format_->initialize(req_)) != srs_success) {
        return srs_error_wrap(err, "format initialize");
    }

    return err;
}

bool SrsSrtSource::stream_is_dead()
{
    if (!can_publish_) {
        return false;
    }

    if (!consumers_.empty()) {
        return false;
    }

    srs_utime_t now = clk_->now();
    if (now < stream_die_at_ + SRS_SRT_SOURCE_CLEANUP) {
        return false;
    }

    return true;
}

srs_error_t SrsSrtSource::on_source_id_changed(SrsContextId id)
{
    srs_error_t err = srs_success;

    if (!source_id_.compare(id)) {
        return err;
    }

    if (pre_source_id_.empty()) {
        pre_source_id_ = id;
    }
    source_id_ = id;

    std::vector<ISrsSrtConsumer *>::iterator it;
    for (it = consumers_.begin(); it != consumers_.end(); ++it) {
        ISrsSrtConsumer *consumer = *it;
        SrsSrtConsumer *consumer_impl = dynamic_cast<SrsSrtConsumer *>(consumer);
        if (consumer_impl) {
            consumer_impl->update_source_id();
        }
    }

    return err;
}

SrsContextId SrsSrtSource::source_id()
{
    return source_id_;
}

SrsContextId SrsSrtSource::pre_source_id()
{
    return pre_source_id_;
}

void SrsSrtSource::update_auth(ISrsRequest *r)
{
    req_->update_auth(r);
}

void SrsSrtSource::set_bridge(ISrsSrtBridge *bridge)
{
    srs_freep(srt_bridge_);
    srt_bridge_ = bridge;
}

srs_error_t SrsSrtSource::create_consumer(ISrsSrtConsumer *&consumer)
{
    srs_error_t err = srs_success;

    consumer = new SrsSrtConsumer(this);
    consumers_.push_back(consumer);

    stream_die_at_ = 0;

    return err;
}

srs_error_t SrsSrtSource::consumer_dumps(ISrsSrtConsumer *consumer)
{
    srs_error_t err = srs_success;

    srs_trace("create ts consumer, no gop cache");

    return err;
}

void SrsSrtSource::on_consumer_destroy(ISrsSrtConsumer *consumer)
{
    std::vector<ISrsSrtConsumer *>::iterator it;
    it = std::find(consumers_.begin(), consumers_.end(), consumer);
    if (it != consumers_.end()) {
        it = consumers_.erase(it);
    }

    if (can_publish_ && consumers_.empty()) {
        stream_die_at_ = clk_->now();
    }
}

bool SrsSrtSource::can_publish()
{
    return can_publish_;
}

srs_error_t SrsSrtSource::on_publish()
{
    srs_error_t err = srs_success;

    can_publish_ = false;

    if ((err = on_source_id_changed(_srs_context->get_id())) != srs_success) {
        return srs_error_wrap(err, "source id change");
    }

    if (srt_bridge_ && (err = srt_bridge_->on_publish()) != srs_success) {
        return srs_error_wrap(err, "bridge on publish");
    }

    stat_->on_stream_publish(req_, source_id_.c_str());

    return err;
}

void SrsSrtSource::on_unpublish()
{
    if (can_publish_) {
        return;
    }

    stat_->on_stream_close(req_);

    if (srt_bridge_) {
        srt_bridge_->on_unpublish();
        srs_freep(srt_bridge_);
    }

    if (consumers_.empty()) {
        stream_die_at_ = clk_->now();
    }

    // Should never change the final state before all cleanup is done.
    can_publish_ = true;
}

srs_error_t SrsSrtSource::on_srt_packet(SrsSrtPacket *packet)
{
    srs_error_t err = srs_success;

    if ((err = format_->on_srt_packet(packet)) != srs_success) {
        srs_warn("srt source parse packet err=%s", srs_error_desc(err).c_str());
        srs_freep(err);
    }

    for (int i = 0; i < (int)consumers_.size(); i++) {
        ISrsSrtConsumer *consumer = consumers_.at(i);
        if ((err = consumer->enqueue(packet->copy())) != srs_success) {
            return srs_error_wrap(err, "consume ts packet");
        }
    }

    if (srt_bridge_ && (err = srt_bridge_->on_srt_packet(packet)) != srs_success) {
        return srs_error_wrap(err, "bridge consume message");
    }

    return err;
}
