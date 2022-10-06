//
// Copyright (c) 2013-2022 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#include <srs_app_rtc_source.hpp>

#include <math.h>
#include <unistd.h>

#include <srs_app_conn.hpp>
#include <srs_protocol_rtmp_stack.hpp>
#include <srs_app_config.hpp>
#include <srs_app_source.hpp>
#include <srs_kernel_flv.hpp>
#include <srs_kernel_codec.hpp>
#include <srs_protocol_rtmp_msg_array.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_format.hpp>
#include <srs_kernel_buffer.hpp>
#include <srs_kernel_rtc_rtp.hpp>
#include <srs_core_autofree.hpp>
#include <srs_app_rtc_queue.hpp>
#include <srs_app_rtc_conn.hpp>
#include <srs_protocol_utility.hpp>
#include <srs_protocol_json.hpp>
#include <srs_app_pithy_print.hpp>
#include <srs_app_log.hpp>
#include <srs_app_threads.hpp>
#include <srs_app_statistic.hpp>

#ifdef SRS_FFMPEG_FIT
#include <srs_app_rtc_codec.hpp>
#endif

#include <srs_protocol_kbps.hpp>
#include <srs_protocol_raw_avc.hpp>

// The NACK sent by us(SFU).
SrsPps* _srs_pps_snack = NULL;
SrsPps* _srs_pps_snack2 = NULL;
SrsPps* _srs_pps_snack3 = NULL;
SrsPps* _srs_pps_snack4 = NULL;
SrsPps* _srs_pps_sanack = NULL;
SrsPps* _srs_pps_svnack = NULL;

SrsPps* _srs_pps_rnack = NULL;
SrsPps* _srs_pps_rnack2 = NULL;
SrsPps* _srs_pps_rhnack = NULL;
SrsPps* _srs_pps_rmnack = NULL;

extern SrsPps* _srs_pps_aloss2;

// Firefox defaults as 109, Chrome is 111.
const int kAudioPayloadType     = 111;
const int kAudioChannel         = 2;
const int kAudioSamplerate      = 48000;

// Firefox defaults as 126, Chrome is 102.
const int kVideoPayloadType = 102;
const int kVideoSamplerate  = 90000;

// The RTP payload max size, reserved some paddings for SRTP as such:
//      kRtpPacketSize = kRtpMaxPayloadSize + paddings
// For example, if kRtpPacketSize is 1500, recommend to set kRtpMaxPayloadSize to 1400,
// which reserves 100 bytes for SRTP or paddings.
// otherwise, the kRtpPacketSize must less than MTU, in webrtc source code, 
// the rtp max size is assigned by kVideoMtu = 1200.
// so we set kRtpMaxPayloadSize = 1200.
// see @doc https://groups.google.com/g/discuss-webrtc/c/gH5ysR3SoZI
const int kRtpMaxPayloadSize = kRtpPacketSize - 300;

using namespace std;

// TODO: Add this function into SrsRtpMux class.
srs_error_t aac_raw_append_adts_header(SrsSharedPtrMessage* shared_audio, SrsFormat* format, char** pbuf, int* pnn_buf)
{
    srs_error_t err = srs_success;

    if (format->is_aac_sequence_header()) {
        return err;
    }

    // If no audio RAW frame, or not parsed for no sequence header, drop the packet.
    if (format->audio->nb_samples == 0) {
        srs_warn("RTC: Drop AAC %d bytes for no sample", shared_audio->size);
        return err;
    }

    if (format->audio->nb_samples != 1) {
        return srs_error_new(ERROR_RTC_RTP_MUXER, "adts samples=%d", format->audio->nb_samples);
    }

    int nb_buf = format->audio->samples[0].size + 7;
    char* buf = new char[nb_buf];
    SrsBuffer stream(buf, nb_buf);

    // TODO: Add comment.
    stream.write_1bytes(0xFF);
    stream.write_1bytes(0xF9);
    stream.write_1bytes(((format->acodec->aac_object - 1) << 6) | ((format->acodec->aac_sample_rate & 0x0F) << 2) | ((format->acodec->aac_channels & 0x04) >> 2));
    stream.write_1bytes(((format->acodec->aac_channels & 0x03) << 6) | ((nb_buf >> 11) & 0x03));
    stream.write_1bytes((nb_buf >> 3) & 0xFF);
    stream.write_1bytes(((nb_buf & 0x07) << 5) | 0x1F);
    stream.write_1bytes(0xFC);

    stream.write_bytes(format->audio->samples[0].bytes, format->audio->samples[0].size);

    *pbuf = buf;
    *pnn_buf = nb_buf;

    return err;
}

uint64_t SrsNtp::kMagicNtpFractionalUnit = 1ULL << 32;

SrsNtp::SrsNtp()
{
    system_ms_ = 0;
    ntp_ = 0;
    ntp_second_ = 0;
    ntp_fractions_ = 0;
}

SrsNtp::~SrsNtp()
{
}

SrsNtp SrsNtp::from_time_ms(uint64_t ms)
{
    SrsNtp srs_ntp;
    srs_ntp.system_ms_ = ms;
    srs_ntp.ntp_second_ = ms / 1000;
    srs_ntp.ntp_fractions_ = (static_cast<double>(ms % 1000 / 1000.0)) * kMagicNtpFractionalUnit;
    srs_ntp.ntp_ = (static_cast<uint64_t>(srs_ntp.ntp_second_) << 32) | srs_ntp.ntp_fractions_;
    return srs_ntp;
}

SrsNtp SrsNtp::to_time_ms(uint64_t ntp)
{
    SrsNtp srs_ntp;
    srs_ntp.ntp_ = ntp;
    srs_ntp.ntp_second_ = (ntp & 0xFFFFFFFF00000000ULL) >> 32;
    srs_ntp.ntp_fractions_ = (ntp & 0x00000000FFFFFFFFULL);
    srs_ntp.system_ms_ = (static_cast<uint64_t>(srs_ntp.ntp_second_) * 1000) +
        round((static_cast<double>(static_cast<uint64_t>(srs_ntp.ntp_fractions_) * 1000.0) / kMagicNtpFractionalUnit));
    return srs_ntp;
}

ISrsRtcSourceChangeCallback::ISrsRtcSourceChangeCallback()
{
}

ISrsRtcSourceChangeCallback::~ISrsRtcSourceChangeCallback()
{
}

SrsRtcConsumer::SrsRtcConsumer(SrsRtcSource* s)
{
    source = s;
    should_update_source_id = false;
    handler_ = NULL;

    mw_wait = srs_cond_new();
    mw_min_msgs = 0;
    mw_waiting = false;
}

SrsRtcConsumer::~SrsRtcConsumer()
{
    source->on_consumer_destroy(this);

    vector<SrsRtpPacket*>::iterator it;
    for (it = queue.begin(); it != queue.end(); ++it) {
        SrsRtpPacket* pkt = *it;
        srs_freep(pkt);
    }

    srs_cond_destroy(mw_wait);
}

void SrsRtcConsumer::update_source_id()
{
    should_update_source_id = true;
}

srs_error_t SrsRtcConsumer::enqueue(SrsRtpPacket* pkt)
{
    srs_error_t err = srs_success;

    queue.push_back(pkt);

    if (mw_waiting) {
        if ((int)queue.size() > mw_min_msgs) {
            srs_cond_signal(mw_wait);
            mw_waiting = false;
            return err;
        }
    }

    return err;
}

srs_error_t SrsRtcConsumer::dump_packet(SrsRtpPacket** ppkt)
{
    srs_error_t err = srs_success;

    if (should_update_source_id) {
        srs_trace("update source_id=%s/%s", source->source_id().c_str(), source->pre_source_id().c_str());
        should_update_source_id = false;
    }

    // TODO: FIXME: Refine performance by ring buffer.
    if (!queue.empty()) {
        *ppkt = queue.front();
        queue.erase(queue.begin());
    }

    return err;
}

void SrsRtcConsumer::wait(int nb_msgs)
{
    mw_min_msgs = nb_msgs;

    // when duration ok, signal to flush.
    if ((int)queue.size() > mw_min_msgs) {
        return;
    }

    // the enqueue will notify this cond.
    mw_waiting = true;

    // use cond block wait for high performance mode.
    srs_cond_wait(mw_wait);
}

void SrsRtcConsumer::on_stream_change(SrsRtcSourceDescription* desc)
{
    if (handler_) {
        handler_->on_stream_change(desc);
    }
}

SrsRtcSourceManager::SrsRtcSourceManager()
{
    lock = srs_mutex_new();
}

SrsRtcSourceManager::~SrsRtcSourceManager()
{
    srs_mutex_destroy(lock);
}

srs_error_t SrsRtcSourceManager::fetch_or_create(SrsRequest* r, SrsRtcSource** pps)
{
    srs_error_t err = srs_success;

    // Use lock to protect coroutine switch.
    // @bug https://github.com/ossrs/srs/issues/1230
    SrsLocker(lock);

    SrsRtcSource* source = NULL;
    if ((source = fetch(r)) != NULL) {
        // we always update the request of resource,
        // for origin auth is on, the token in request maybe invalid,
        // and we only need to update the token of request, it's simple.
        source->update_auth(r);
        *pps = source;
        return err;
    }

    string stream_url = r->get_stream_url();
    string vhost = r->vhost;

    // should always not exists for create a source.
    srs_assert (pool.find(stream_url) == pool.end());

    srs_trace("new rtc source, stream_url=%s", stream_url.c_str());

    source = new SrsRtcSource();
    if ((err = source->initialize(r)) != srs_success) {
        return srs_error_wrap(err, "init source %s", r->get_stream_url().c_str());
    }

    pool[stream_url] = source;

    *pps = source;

    return err;
}

SrsRtcSource* SrsRtcSourceManager::fetch(SrsRequest* r)
{
    SrsRtcSource* source = NULL;

    string stream_url = r->get_stream_url();
    if (pool.find(stream_url) == pool.end()) {
        return NULL;
    }

    source = pool[stream_url];

    return source;
}

SrsRtcSourceManager* _srs_rtc_sources = NULL;

ISrsRtcPublishStream::ISrsRtcPublishStream()
{
}

ISrsRtcPublishStream::~ISrsRtcPublishStream()
{
}

ISrsRtcSourceEventHandler::ISrsRtcSourceEventHandler()
{
}

ISrsRtcSourceEventHandler::~ISrsRtcSourceEventHandler()
{
}

ISrsRtcSourceBridge::ISrsRtcSourceBridge()
{
}

ISrsRtcSourceBridge::~ISrsRtcSourceBridge()
{
}

SrsRtcSource::SrsRtcSource()
{
    is_created_ = false;
    is_delivering_packets_ = false;

    publish_stream_ = NULL;
    stream_desc_ = NULL;

    req = NULL;
    bridge_ = NULL;

    pli_for_rtmp_ = pli_elapsed_ = 0;
}

SrsRtcSource::~SrsRtcSource()
{
    // never free the consumers,
    // for all consumers are auto free.
    consumers.clear();

    srs_freep(bridge_);
    srs_freep(req);
    srs_freep(stream_desc_);
}

srs_error_t SrsRtcSource::initialize(SrsRequest* r)
{
    srs_error_t err = srs_success;

    req = r->copy();

	// Create default relations to allow play before publishing.
	// @see https://github.com/ossrs/srs/issues/2362
	init_for_play_before_publishing();

	return err;
}

void SrsRtcSource::init_for_play_before_publishing()
{
    // If the stream description has already been setup by RTC publisher,
    // we should ignore and it's ok, because we only need to setup it for bridge.
    if (stream_desc_) {
        return;
    }

    SrsRtcSourceDescription* stream_desc = new SrsRtcSourceDescription();
    SrsAutoFree(SrsRtcSourceDescription, stream_desc);

    // audio track description
    if (true) {
        SrsRtcTrackDescription* audio_track_desc = new SrsRtcTrackDescription();
        stream_desc->audio_track_desc_ = audio_track_desc;

        audio_track_desc->type_ = "audio";
        audio_track_desc->id_ = "audio-" + srs_random_str(8);

        uint32_t audio_ssrc = SrsRtcSSRCGenerator::instance()->generate_ssrc();
        audio_track_desc->ssrc_ = audio_ssrc;
        audio_track_desc->direction_ = "recvonly";

        audio_track_desc->media_ = new SrsAudioPayload(kAudioPayloadType, "opus", kAudioSamplerate, kAudioChannel);
    }

    // video track description
    if (true) {
        SrsRtcTrackDescription* video_track_desc = new SrsRtcTrackDescription();
        stream_desc->video_track_descs_.push_back(video_track_desc);

        video_track_desc->type_ = "video";
        video_track_desc->id_ = "video-" + srs_random_str(8);

        uint32_t video_ssrc = SrsRtcSSRCGenerator::instance()->generate_ssrc();
        video_track_desc->ssrc_ = video_ssrc;
        video_track_desc->direction_ = "recvonly";

        SrsVideoPayload* video_payload = new SrsVideoPayload(kVideoPayloadType, "H264", kVideoSamplerate);
        video_track_desc->media_ = video_payload;

        video_payload->set_h264_param_desc("level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42e01f");
    }

    set_stream_desc(stream_desc);
}

void SrsRtcSource::update_auth(SrsRequest* r)
{
    req->update_auth(r);
}

srs_error_t SrsRtcSource::on_source_changed()
{
    srs_error_t err = srs_success;

    // Update context id if changed.
    bool id_changed = false;
    const SrsContextId& id = _srs_context->get_id();
    if (_source_id.compare(id)) {
        id_changed = true;

        if (_pre_source_id.empty()) {
            _pre_source_id = id;
        }
        _source_id = id;
    }

    // Notify all consumers.
    std::vector<SrsRtcConsumer*>::iterator it;
    for (it = consumers.begin(); it != consumers.end(); ++it) {
        SrsRtcConsumer* consumer = *it;

        // Notify if context id changed.
        if (id_changed) {
            consumer->update_source_id();
        }

        // Notify about stream description.
        consumer->on_stream_change(stream_desc_);
    }

    return err;
}

SrsContextId SrsRtcSource::source_id()
{
    return _source_id;
}

SrsContextId SrsRtcSource::pre_source_id()
{
    return _pre_source_id;
}

void SrsRtcSource::set_bridge(ISrsRtcSourceBridge *bridge)
{
    srs_freep(bridge_);
    bridge_ = bridge;
}

srs_error_t SrsRtcSource::create_consumer(SrsRtcConsumer*& consumer)
{
    srs_error_t err = srs_success;

    consumer = new SrsRtcConsumer(this);
    consumers.push_back(consumer);

    // TODO: FIXME: Implements edge cluster.

    return err;
}

srs_error_t SrsRtcSource::consumer_dumps(SrsRtcConsumer* consumer, bool ds, bool dm, bool dg)
{
    srs_error_t err = srs_success;

    // print status.
    srs_trace("create consumer, no gop cache");

    return err;
}

void SrsRtcSource::on_consumer_destroy(SrsRtcConsumer* consumer)
{
    std::vector<SrsRtcConsumer*>::iterator it;
    it = std::find(consumers.begin(), consumers.end(), consumer);
    if (it != consumers.end()) {
        it = consumers.erase(it);
    }

    // When all consumers finished, notify publisher to handle it.
    if (publish_stream_ && consumers.empty()) {
        for (size_t i = 0; i < event_handlers_.size(); i++) {
            ISrsRtcSourceEventHandler* h = event_handlers_.at(i);
            h->on_consumers_finished();
        }
    }
}

bool SrsRtcSource::can_publish()
{
    // TODO: FIXME: Should check the status of bridge.
    
    return !is_created_;
}

void SrsRtcSource::set_stream_created()
{
    srs_assert(!is_created_ && !is_delivering_packets_);
    is_created_ = true;
}

srs_error_t SrsRtcSource::on_publish()
{
    srs_error_t err = srs_success;

    // update the request object.
    srs_assert(req);

    // For RTC, DTLS is done, and we are ready to deliver packets.
    // @note For compatible with RTMP, we also set the is_created_, it MUST be created here.
    is_created_ = true;
    is_delivering_packets_ = true;

    // Notify the consumers about stream change event.
    if ((err = on_source_changed()) != srs_success) {
        return srs_error_wrap(err, "source id change");
    }

    // If bridge to other source, handle event and start timer to request PLI.
    if (bridge_) {
        if ((err = bridge_->on_publish()) != srs_success) {
            return srs_error_wrap(err, "bridge on publish");
        }

        // The PLI interval for RTC2RTMP.
        pli_for_rtmp_ = _srs_config->get_rtc_pli_for_rtmp(req->vhost);

        // @see SrsRtcSource::on_timer()
        _srs_hybrid->timer100ms()->subscribe(this);
    }

    SrsStatistic* stat = SrsStatistic::instance();
    stat->on_stream_publish(req, _source_id.c_str());

    return err;
}

void SrsRtcSource::on_unpublish()
{
    // ignore when already unpublished.
    if (!is_created_) {
        return;
    }

    srs_trace("cleanup when unpublish, created=%u, deliver=%u", is_created_, is_delivering_packets_);

    is_created_ = false;
    is_delivering_packets_ = false;

    if (!_source_id.empty()) {
        _pre_source_id = _source_id;
    }
    _source_id = SrsContextId();

    for (size_t i = 0; i < event_handlers_.size(); i++) {
        ISrsRtcSourceEventHandler* h = event_handlers_.at(i);
        h->on_unpublish();
    }

    //free bridge resource
    if (bridge_) {
        // For SrsRtcSource::on_timer()
        _srs_hybrid->timer100ms()->unsubscribe(this);

        bridge_->on_unpublish();
        srs_freep(bridge_);
    }

    SrsStatistic* stat = SrsStatistic::instance();
    stat->on_stream_close(req);
}

void SrsRtcSource::subscribe(ISrsRtcSourceEventHandler* h)
{
    if (std::find(event_handlers_.begin(), event_handlers_.end(), h) == event_handlers_.end()) {
        event_handlers_.push_back(h);
    }
}

void SrsRtcSource::unsubscribe(ISrsRtcSourceEventHandler* h)
{
    std::vector<ISrsRtcSourceEventHandler*>::iterator it;
    it = std::find(event_handlers_.begin(), event_handlers_.end(), h);
    if (it != event_handlers_.end()) {
        it = event_handlers_.erase(it);
    }
}

ISrsRtcPublishStream* SrsRtcSource::publish_stream()
{
    return publish_stream_;
}

void SrsRtcSource::set_publish_stream(ISrsRtcPublishStream* v)
{
    publish_stream_ = v;
}

srs_error_t SrsRtcSource::on_rtp(SrsRtpPacket* pkt)
{
    srs_error_t err = srs_success;

    // If circuit-breaker is dying, drop packet.
    if (_srs_circuit_breaker->hybrid_dying_water_level()) {
        _srs_pps_aloss2->sugar += (int64_t)consumers.size();
        return err;
    }

    for (int i = 0; i < (int)consumers.size(); i++) {
        SrsRtcConsumer* consumer = consumers.at(i);
        if ((err = consumer->enqueue(pkt->copy())) != srs_success) {
            return srs_error_wrap(err, "consume message");
        }
    }

    if (bridge_ && (err = bridge_->on_rtp(pkt)) != srs_success) {
        return srs_error_wrap(err, "bridge consume message");
    }

    return err;
}

bool SrsRtcSource::has_stream_desc()
{
    return stream_desc_;
}

void SrsRtcSource::set_stream_desc(SrsRtcSourceDescription* stream_desc)
{
    srs_freep(stream_desc_);

    if (stream_desc) {
        stream_desc_ = stream_desc->copy();
    }
}

std::vector<SrsRtcTrackDescription*> SrsRtcSource::get_track_desc(std::string type, std::string media_name)
{
    std::vector<SrsRtcTrackDescription*> track_descs;
    if (!stream_desc_) {
        return track_descs;
    }

    if (type == "audio") {
        if (! stream_desc_->audio_track_desc_) {
            return track_descs;
        }
        if (stream_desc_->audio_track_desc_->media_->name_ == media_name) {
            track_descs.push_back(stream_desc_->audio_track_desc_);
        }
    }

    if (type == "video") {
        std::vector<SrsRtcTrackDescription*>::iterator it = stream_desc_->video_track_descs_.begin();
        while (it != stream_desc_->video_track_descs_.end() ){
            track_descs.push_back(*it);
            ++it;
        }
    }

    return track_descs;
}

srs_error_t SrsRtcSource::on_timer(srs_utime_t interval)
{
    srs_error_t err = srs_success;

    if (!publish_stream_) {
        return err;
    }

    // Request PLI and reset the timer.
    if (true) {
        pli_elapsed_ += interval;
        if (pli_elapsed_ < pli_for_rtmp_) {
            return err;
        }
        pli_elapsed_ = 0;
    }

    for (int i = 0; i < (int)stream_desc_->video_track_descs_.size(); i++) {
        SrsRtcTrackDescription* desc = stream_desc_->video_track_descs_.at(i);
        publish_stream_->request_keyframe(desc->ssrc_);
    }

    return err;
}

#ifdef SRS_FFMPEG_FIT
SrsRtcFromRtmpBridge::SrsRtcFromRtmpBridge(SrsRtcSource* source)
{
    req = NULL;
    source_ = source;
    format = new SrsRtmpFormat();
    codec_ = new SrsAudioTranscoder();
    rtmp_to_rtc = false;
    keep_bframe = false;
    merge_nalus = false;
    meta = new SrsMetaCache();
    audio_sequence = 0;
    video_sequence = 0;

    // audio track ssrc
    if (true) {
        std::vector<SrsRtcTrackDescription*> descs = source->get_track_desc("audio", "opus");
        if (!descs.empty()) {
            audio_ssrc = descs.at(0)->ssrc_;
        }
    }

    // video track ssrc
    if (true) {
        std::vector<SrsRtcTrackDescription*> descs = source->get_track_desc("video", "H264");
        if (!descs.empty()) {
            video_ssrc = descs.at(0)->ssrc_;
        }
    }
}

SrsRtcFromRtmpBridge::~SrsRtcFromRtmpBridge()
{
    srs_freep(format);
    srs_freep(codec_);
    srs_freep(meta);
}

srs_error_t SrsRtcFromRtmpBridge::initialize(SrsRequest* r)
{
    srs_error_t err = srs_success;

    req = r;
    rtmp_to_rtc = _srs_config->get_rtc_from_rtmp(req->vhost);

    if (rtmp_to_rtc) {
        if ((err = format->initialize()) != srs_success) {
            return srs_error_wrap(err, "format initialize");
        }

        // Setup the SPS/PPS parsing strategy.
        format->try_annexb_first = _srs_config->try_annexb_first(r->vhost);

        int bitrate = 48000; // The output bitrate in bps.
        if ((err = codec_->initialize(SrsAudioCodecIdAAC, SrsAudioCodecIdOpus, kAudioChannel, kAudioSamplerate,
                                      bitrate)) != srs_success) {
            return srs_error_wrap(err, "init codec");
        }
    }

    keep_bframe = _srs_config->get_rtc_keep_bframe(req->vhost);
    merge_nalus = _srs_config->get_rtc_server_merge_nalus();
    srs_trace("RTC bridge from RTMP, rtmp2rtc=%d, keep_bframe=%d, merge_nalus=%d",
              rtmp_to_rtc, keep_bframe, merge_nalus);

    return err;
}

srs_error_t SrsRtcFromRtmpBridge::on_publish()
{
    srs_error_t err = srs_success;

    if (!rtmp_to_rtc) {
        return err;
    }

    // TODO: FIXME: Should sync with bridge?
    if ((err = source_->on_publish()) != srs_success) {
        return srs_error_wrap(err, "source publish");
    }

    // Reset the metadata cache, to make VLC happy when disable/enable stream.
    // @see https://github.com/ossrs/srs/issues/1630#issuecomment-597979448
    meta->clear();

    return err;
}

void SrsRtcFromRtmpBridge::on_unpublish()
{
    if (!rtmp_to_rtc) {
        return;
    }

    // Reset the metadata cache, to make VLC happy when disable/enable stream.
    // @see https://github.com/ossrs/srs/issues/1630#issuecomment-597979448
    meta->update_previous_vsh();
    meta->update_previous_ash();

    // @remark This bridge might be disposed here, so never use it.
    // TODO: FIXME: Should sync with bridge?
    source_->on_unpublish();
}

srs_error_t SrsRtcFromRtmpBridge::on_audio(SrsSharedPtrMessage* msg)
{
    srs_error_t err = srs_success;

    if (!rtmp_to_rtc) {
        return err;
    }

    // TODO: FIXME: Support parsing OPUS for RTC.
    if ((err = format->on_audio(msg)) != srs_success) {
        return srs_error_wrap(err, "format consume audio");
    }

    // Ignore if no format->acodec, it means the codec is not parsed, or unknown codec.
    // @issue https://github.com/ossrs/srs/issues/1506#issuecomment-562079474
    if (!format->acodec) {
        return err;
    }

    // ts support audio codec: aac/mp3
    SrsAudioCodecId acodec = format->acodec->id;
    if (acodec != SrsAudioCodecIdAAC && acodec != SrsAudioCodecIdMP3) {
        return err;
    }

    // When drop aac audio packet, never transcode.
    if (acodec != SrsAudioCodecIdAAC) {
        return err;
    }

    // ignore sequence header
    srs_assert(format->audio);

    char* adts_audio = NULL;
    int nn_adts_audio = 0;
    // TODO: FIXME: Reserve 7 bytes header when create shared message.
    if ((err = aac_raw_append_adts_header(msg, format, &adts_audio, &nn_adts_audio)) != srs_success) {
        return srs_error_wrap(err, "aac append header");
    }

    if (!adts_audio) {
        return err;
    }

    SrsAudioFrame aac;
    aac.dts = format->audio->dts;
    aac.cts = format->audio->cts;
    if ((err = aac.add_sample(adts_audio, nn_adts_audio)) == srs_success) {
        // If OK, transcode the AAC to Opus and consume it.
        err = transcode(&aac);
    }

    srs_freepa(adts_audio);

    return err;
}

srs_error_t SrsRtcFromRtmpBridge::transcode(SrsAudioFrame* audio)
{
    srs_error_t err = srs_success;

    std::vector<SrsAudioFrame *> out_audios;
    if ((err = codec_->transcode(audio, out_audios)) != srs_success) {
        return srs_error_wrap(err, "recode error");
    }

    // Save OPUS packets in shared message.
    if (out_audios.empty()) {
        return err;
    }

    for (std::vector<SrsAudioFrame*>::iterator it = out_audios.begin(); it != out_audios.end(); ++it) {
        SrsAudioFrame* out_audio = *it;

        SrsRtpPacket* pkt = new SrsRtpPacket();
        SrsAutoFree(SrsRtpPacket, pkt);

        if ((err = package_opus(out_audio, pkt)) != srs_success) {
            err = srs_error_wrap(err, "package opus");
            break;
        }

        if ((err = source_->on_rtp(pkt)) != srs_success) {
            err = srs_error_wrap(err, "consume opus");
            break;
        }
    }

    codec_->free_frames(out_audios);

    return err;
}

srs_error_t SrsRtcFromRtmpBridge::package_opus(SrsAudioFrame* audio, SrsRtpPacket* pkt)
{
    srs_error_t err = srs_success;

    pkt->header.set_payload_type(kAudioPayloadType);
    pkt->header.set_ssrc(audio_ssrc);
    pkt->frame_type = SrsFrameTypeAudio;
    pkt->header.set_marker(true);
    pkt->header.set_sequence(audio_sequence++);
    pkt->header.set_timestamp(audio->dts * 48);

    SrsRtpRawPayload* raw = new SrsRtpRawPayload();
    pkt->set_payload(raw, SrsRtspPacketPayloadTypeRaw);

    srs_assert(audio->nb_samples == 1);
    raw->payload = pkt->wrap(audio->samples[0].bytes, audio->samples[0].size);
    raw->nn_payload = audio->samples[0].size;

    return err;
}

srs_error_t SrsRtcFromRtmpBridge::on_video(SrsSharedPtrMessage* msg)
{
    srs_error_t err = srs_success;

    if (!rtmp_to_rtc) {
        return err;
    }

    // cache the sequence header if h264
    bool is_sequence_header = SrsFlvVideo::sh(msg->payload, msg->size);
    if (is_sequence_header && (err = meta->update_vsh(msg)) != srs_success) {
        return srs_error_wrap(err, "meta update video");
    }

    if ((err = format->on_video(msg)) != srs_success) {
        return srs_error_wrap(err, "format consume video");
    }

    // Ignore if no format->vcodec, it means the codec is not parsed, or unsupport/unknown codec
    // such as H.263 codec
    if (!format->vcodec) {
        return err;
    }

    bool has_idr = false;
    vector<SrsSample*> samples;
    if ((err = filter(msg, format, has_idr, samples)) != srs_success) {
        return srs_error_wrap(err, "filter video");
    }
    int nn_samples = (int)samples.size();

    // Well, for each IDR, we append a SPS/PPS before it, which is packaged in STAP-A.
    if (has_idr) {
        SrsRtpPacket* pkt = new SrsRtpPacket();
        SrsAutoFree(SrsRtpPacket, pkt);

        if ((err = package_stap_a(source_, msg, pkt)) != srs_success) {
            return srs_error_wrap(err, "package stap-a");
        }

        if ((err = source_->on_rtp(pkt)) != srs_success) {
            return srs_error_wrap(err, "consume sps/pps");
        }
    }

    // If merge Nalus, we pcakges all NALUs(samples) as one NALU, in a RTP or FUA packet.
    vector<SrsRtpPacket*> pkts;
    if (merge_nalus && nn_samples > 1) {
        if ((err = package_nalus(msg, samples, pkts)) != srs_success) {
            return srs_error_wrap(err, "package nalus as one");
        }
    } else {
        // By default, we package each NALU(sample) to a RTP or FUA packet.
        for (int i = 0; i < nn_samples; i++) {
            SrsSample* sample = samples[i];

            // We always ignore bframe here, if config to discard bframe,
            // the bframe flag will not be set.
            if (sample->bframe) {
                continue;
            }

            if (sample->size <= kRtpMaxPayloadSize) {
                if ((err = package_single_nalu(msg, sample, pkts)) != srs_success) {
                    return srs_error_wrap(err, "package single nalu");
                }
            } else {
                if ((err = package_fu_a(msg, sample, kRtpMaxPayloadSize, pkts)) != srs_success) {
                    return srs_error_wrap(err, "package fu-a");
                }
            }
        }
    }

    if (!pkts.empty()) {
        pkts.back()->header.set_marker(true);
    }

    return consume_packets(pkts);
}

srs_error_t SrsRtcFromRtmpBridge::filter(SrsSharedPtrMessage* msg, SrsFormat* format, bool& has_idr, vector<SrsSample*>& samples)
{
    srs_error_t err = srs_success;

    // If IDR, we will insert SPS/PPS before IDR frame.
    if (format->video && format->video->has_idr) {
        has_idr = true;
    }

    // Update samples to shared frame.
    for (int i = 0; i < format->video->nb_samples; ++i) {
        SrsSample* sample = &format->video->samples[i];

        // Because RTC does not support B-frame, so we will drop them.
        // TODO: Drop B-frame in better way, which not cause picture corruption.
        if (!keep_bframe) {
            if ((err = sample->parse_bframe()) != srs_success) {
                return srs_error_wrap(err, "parse bframe");
            }
            if (sample->bframe) {
                continue;
            }
        }

        samples.push_back(sample);
    }

    return err;
}

srs_error_t SrsRtcFromRtmpBridge::package_stap_a(SrsRtcSource* source, SrsSharedPtrMessage* msg, SrsRtpPacket* pkt)
{
    srs_error_t err = srs_success;

    SrsFormat* format = meta->vsh_format();
    if (!format || !format->vcodec) {
        return err;
    }

    // Note that the sps/pps may change, so we should copy it.
    const vector<char>& sps = format->vcodec->sequenceParameterSetNALUnit;
    const vector<char>& pps = format->vcodec->pictureParameterSetNALUnit;
    if (sps.empty() || pps.empty()) {
        return srs_error_new(ERROR_RTC_RTP_MUXER, "sps/pps empty");
    }

    pkt->header.set_payload_type(kVideoPayloadType);
    pkt->header.set_ssrc(video_ssrc);
    pkt->frame_type = SrsFrameTypeVideo;
    pkt->nalu_type = (SrsAvcNaluType)kStapA;
    pkt->header.set_marker(false);
    pkt->header.set_sequence(video_sequence++);
    pkt->header.set_timestamp(msg->timestamp * 90);

    SrsRtpSTAPPayload* stap = new SrsRtpSTAPPayload();
    pkt->set_payload(stap, SrsRtspPacketPayloadTypeSTAP);

    uint8_t header = sps[0];
    stap->nri = (SrsAvcNaluType)header;

    // Copy the SPS/PPS bytes, because it may change.
    int size = (int)(sps.size() + pps.size());
    char* payload = pkt->wrap(size);

    if (true) {
        SrsSample* sample = new SrsSample();
        sample->bytes = payload;
        sample->size = (int)sps.size();
        stap->nalus.push_back(sample);

        memcpy(payload, (char*)&sps[0], sps.size());
        payload += (int)sps.size();
    }

    if (true) {
        SrsSample* sample = new SrsSample();
        sample->bytes = payload;
        sample->size = (int)pps.size();
        stap->nalus.push_back(sample);

        memcpy(payload, (char*)&pps[0], pps.size());
        payload += (int)pps.size();
    }

    srs_info("RTC STAP-A seq=%u, sps %d, pps %d bytes", pkt->header.get_sequence(), sps.size(), pps.size());

    return err;
}

srs_error_t SrsRtcFromRtmpBridge::package_nalus(SrsSharedPtrMessage* msg, const vector<SrsSample*>& samples, vector<SrsRtpPacket*>& pkts)
{
    srs_error_t err = srs_success;

    SrsRtpRawNALUs* raw = new SrsRtpRawNALUs();
    SrsAvcNaluType first_nalu_type = SrsAvcNaluTypeReserved;

    for (int i = 0; i < (int)samples.size(); i++) {
        SrsSample* sample = samples[i];

        // We always ignore bframe here, if config to discard bframe,
        // the bframe flag will not be set.
        if (sample->bframe) {
            continue;
        }

        if (!sample->size) {
            continue;
        }

        if (first_nalu_type == SrsAvcNaluTypeReserved) {
            first_nalu_type = SrsAvcNaluType((uint8_t)(sample->bytes[0] & kNalTypeMask));
        }

        raw->push_back(sample->copy());
    }

    // Ignore empty.
    int nn_bytes = raw->nb_bytes();
    if (nn_bytes <= 0) {
        srs_freep(raw);
        return err;
    }

    if (nn_bytes < kRtpMaxPayloadSize) {
        // Package NALUs in a single RTP packet.
        SrsRtpPacket* pkt = new SrsRtpPacket();
        pkts.push_back(pkt);

        pkt->header.set_payload_type(kVideoPayloadType);
        pkt->header.set_ssrc(video_ssrc);
        pkt->frame_type = SrsFrameTypeVideo;
        pkt->nalu_type = (SrsAvcNaluType)first_nalu_type;
        pkt->header.set_sequence(video_sequence++);
        pkt->header.set_timestamp(msg->timestamp * 90);
        pkt->set_payload(raw, SrsRtspPacketPayloadTypeNALU);
        pkt->wrap(msg);
    } else {
        // We must free it, should never use RTP packets to free it,
        // because more than one RTP packet will refer to it.
        SrsAutoFree(SrsRtpRawNALUs, raw);

        // Package NALUs in FU-A RTP packets.
        int fu_payload_size = kRtpMaxPayloadSize;

        // The first byte is store in FU-A header.
        uint8_t header = raw->skip_first_byte();
        uint8_t nal_type = header & kNalTypeMask;
        int nb_left = nn_bytes - 1;

        int num_of_packet = 1 + (nn_bytes - 1) / fu_payload_size;
        for (int i = 0; i < num_of_packet; ++i) {
            int packet_size = srs_min(nb_left, fu_payload_size);

            SrsRtpFUAPayload* fua = new SrsRtpFUAPayload();
            if ((err = raw->read_samples(fua->nalus, packet_size)) != srs_success) {
                srs_freep(fua);
                return srs_error_wrap(err, "read samples %d bytes, left %d, total %d", packet_size, nb_left, nn_bytes);
            }

            SrsRtpPacket* pkt = new SrsRtpPacket();
            pkts.push_back(pkt);

            pkt->header.set_payload_type(kVideoPayloadType);
            pkt->header.set_ssrc(video_ssrc);
            pkt->frame_type = SrsFrameTypeVideo;
            pkt->nalu_type = (SrsAvcNaluType)kFuA;
            pkt->header.set_sequence(video_sequence++);
            pkt->header.set_timestamp(msg->timestamp * 90);

            fua->nri = (SrsAvcNaluType)header;
            fua->nalu_type = (SrsAvcNaluType)nal_type;
            fua->start = bool(i == 0);
            fua->end = bool(i == num_of_packet - 1);

            pkt->set_payload(fua, SrsRtspPacketPayloadTypeFUA);
            pkt->wrap(msg);

            nb_left -= packet_size;
        }
    }

    return err;
}

// Single NAL Unit Packet @see https://tools.ietf.org/html/rfc6184#section-5.6
srs_error_t SrsRtcFromRtmpBridge::package_single_nalu(SrsSharedPtrMessage* msg, SrsSample* sample, vector<SrsRtpPacket*>& pkts)
{
    srs_error_t err = srs_success;

    SrsRtpPacket* pkt = new SrsRtpPacket();
    pkts.push_back(pkt);

    pkt->header.set_payload_type(kVideoPayloadType);
    pkt->header.set_ssrc(video_ssrc);
    pkt->frame_type = SrsFrameTypeVideo;
    pkt->header.set_sequence(video_sequence++);
    pkt->header.set_timestamp(msg->timestamp * 90);

    SrsRtpRawPayload* raw = new SrsRtpRawPayload();
    pkt->set_payload(raw, SrsRtspPacketPayloadTypeRaw);

    raw->payload = sample->bytes;
    raw->nn_payload = sample->size;

    pkt->wrap(msg);

    return err;
}

srs_error_t SrsRtcFromRtmpBridge::package_fu_a(SrsSharedPtrMessage* msg, SrsSample* sample, int fu_payload_size, vector<SrsRtpPacket*>& pkts)
{
    srs_error_t err = srs_success;

    char* p = sample->bytes + 1;
    int nb_left = sample->size - 1;
    uint8_t header = sample->bytes[0];
    uint8_t nal_type = header & kNalTypeMask;

    int num_of_packet = 1 + (nb_left - 1) / fu_payload_size;
    for (int i = 0; i < num_of_packet; ++i) {
        int packet_size = srs_min(nb_left, fu_payload_size);

        SrsRtpPacket* pkt = new SrsRtpPacket();
        pkts.push_back(pkt);

        pkt->header.set_payload_type(kVideoPayloadType);
        pkt->header.set_ssrc(video_ssrc);
        pkt->frame_type = SrsFrameTypeVideo;
        pkt->header.set_sequence(video_sequence++);
        pkt->header.set_timestamp(msg->timestamp * 90);

        SrsRtpFUAPayload2* fua = new SrsRtpFUAPayload2();
        pkt->set_payload(fua, SrsRtspPacketPayloadTypeFUA2);

        fua->nri = (SrsAvcNaluType)header;
        fua->nalu_type = (SrsAvcNaluType)nal_type;
        fua->start = bool(i == 0);
        fua->end = bool(i == num_of_packet - 1);

        fua->payload = p;
        fua->size = packet_size;

        pkt->wrap(msg);

        p += packet_size;
        nb_left -= packet_size;
    }

    return err;
}

srs_error_t SrsRtcFromRtmpBridge::consume_packets(vector<SrsRtpPacket*>& pkts)
{
    srs_error_t err = srs_success;

    // TODO: FIXME: Consume a range of packets.
    for (int i = 0; i < (int)pkts.size(); i++) {
        SrsRtpPacket* pkt = pkts[i];
        if ((err = source_->on_rtp(pkt)) != srs_success) {
            err = srs_error_wrap(err, "consume sps/pps");
            break;
        }
    }

    for (int i = 0; i < (int)pkts.size(); i++) {
        SrsRtpPacket* pkt = pkts[i];
        srs_freep(pkt);
    }

    return err;
}

SrsRtmpFromRtcBridge::SrsRtmpFromRtcBridge(SrsLiveSource *src)
{
    source_ = src;
    codec_ = NULL;
    is_first_audio = true;
    is_first_video = true;
    format = NULL;
    rtp_key_frame_ts_ = -1;
    header_sn_ = 0;
    memset(cache_video_pkts_, 0, sizeof(cache_video_pkts_));
}

SrsRtmpFromRtcBridge::~SrsRtmpFromRtcBridge()
{
    srs_freep(codec_);
    srs_freep(format);
    clear_cached_video();
}

srs_error_t SrsRtmpFromRtcBridge::initialize(SrsRequest* r)
{
    srs_error_t err = srs_success;

    codec_ = new SrsAudioTranscoder();
    format = new SrsRtmpFormat();

    SrsAudioCodecId from = SrsAudioCodecIdOpus; // TODO: From SDP?
    SrsAudioCodecId to = SrsAudioCodecIdAAC; // The output audio codec.
    int channels = 2; // The output audio channels.
    int sample_rate = 48000; // The output audio sample rate in HZ.
    int bitrate = 48000; // The output audio bitrate in bps.
    if ((err = codec_->initialize(from, to, channels, sample_rate, bitrate)) != srs_success) {
        return srs_error_wrap(err, "bridge initialize");
    }

    if ((err = format->initialize()) != srs_success) {
        return srs_error_wrap(err, "format initialize");
    }

    // Setup the SPS/PPS parsing strategy.
    format->try_annexb_first = _srs_config->try_annexb_first(r->vhost);

    return err;
}

srs_error_t SrsRtmpFromRtcBridge::on_publish()
{
    srs_error_t err = srs_success;

    is_first_audio = true;
    is_first_video = true;

    // TODO: FIXME: Should sync with bridge?
    if ((err = source_->on_publish()) != srs_success) {
        return srs_error_wrap(err, "source publish");
    }

    return err;
}

srs_error_t SrsRtmpFromRtcBridge::on_rtp(SrsRtpPacket *pkt)
{
    srs_error_t err = srs_success;

    if (!pkt->payload()) {
        return err;
    }

    // Have no received any sender report, can't calculate avsync_time, 
    // discard it to avoid timestamp problem in live source
    if (pkt->get_avsync_time() <= 0) {
        return err;
    }

    if (pkt->is_audio()) {
        err = transcode_audio(pkt);
    } else {
        err = packet_video(pkt);
    }

    return err;
}

void SrsRtmpFromRtcBridge::on_unpublish()
{
    // TODO: FIXME: Should sync with bridge?
    source_->on_unpublish();
}

srs_error_t SrsRtmpFromRtcBridge::transcode_audio(SrsRtpPacket *pkt)
{
    srs_error_t err = srs_success;

    // to common message.
    uint32_t ts = pkt->get_avsync_time();
    if (is_first_audio) {
        int header_len = 0;
        uint8_t* header = NULL;
        codec_->aac_codec_header(&header, &header_len);

        SrsCommonMessage out_rtmp;
        packet_aac(&out_rtmp, (char *)header, header_len, ts, is_first_audio);

        if ((err = source_->on_audio(&out_rtmp)) != srs_success) {
            return srs_error_wrap(err, "source on audio");
        }

        is_first_audio = false;
    }

    std::vector<SrsAudioFrame *> out_pkts;
    SrsRtpRawPayload *payload = dynamic_cast<SrsRtpRawPayload *>(pkt->payload());

    SrsAudioFrame frame;
    frame.add_sample(payload->payload, payload->nn_payload);
    frame.dts = ts;
    frame.cts = 0;

    err = codec_->transcode(&frame, out_pkts);
    if (err != srs_success) {
        return err;
    }

    for (std::vector<SrsAudioFrame *>::iterator it = out_pkts.begin(); it != out_pkts.end(); ++it) {
        SrsCommonMessage out_rtmp;
        out_rtmp.header.timestamp = (*it)->dts;
        packet_aac(&out_rtmp, (*it)->samples[0].bytes, (*it)->samples[0].size, ts, is_first_audio);

        if ((err = source_->on_audio(&out_rtmp)) != srs_success) {
            err = srs_error_wrap(err, "source on audio");
            break;
        }
    }
    codec_->free_frames(out_pkts);

    return err;
}

void SrsRtmpFromRtcBridge::packet_aac(SrsCommonMessage* audio, char* data, int len, uint32_t pts, bool is_header)
{
    int rtmp_len = len + 2;
    audio->header.initialize_audio(rtmp_len, pts, 1);
    audio->create_payload(rtmp_len);
    SrsBuffer stream(audio->payload, rtmp_len);
    uint8_t aac_flag = (SrsAudioCodecIdAAC << 4) | (SrsAudioSampleRate44100 << 2) | (SrsAudioSampleBits16bit << 1) | SrsAudioChannelsStereo;
    stream.write_1bytes(aac_flag);
    if (is_header) {
        stream.write_1bytes(0);
    } else {
        stream.write_1bytes(1);
    }
    stream.write_bytes(data, len);
    audio->size = rtmp_len;
}

srs_error_t SrsRtmpFromRtcBridge::packet_video(SrsRtpPacket* src)
{
    srs_error_t err = srs_success;

    // TODO: Only copy when need
    SrsRtpPacket* pkt = src->copy();

    if (pkt->is_keyframe()) {
        return packet_video_key_frame(pkt);
    }

    // store in cache
    int index = cache_index(pkt->header.get_sequence());
    cache_video_pkts_[index].in_use = true;
    srs_freep(cache_video_pkts_[index].pkt);
    cache_video_pkts_[index].pkt = pkt;
    cache_video_pkts_[index].sn = pkt->header.get_sequence();
    cache_video_pkts_[index].ts = pkt->get_avsync_time();
    cache_video_pkts_[index].rtp_ts = pkt->header.get_timestamp();

    // check whether to recovery lost packet and can construct a video frame
    if (lost_sn_ == pkt->header.get_sequence()) {
        uint16_t tail_sn = 0;
        int sn = find_next_lost_sn(lost_sn_, tail_sn);
        if (-1 == sn ) {
            if (check_frame_complete(header_sn_, tail_sn)) {
                if ((err = packet_video_rtmp(header_sn_, tail_sn)) != srs_success) {
                    err = srs_error_wrap(err, "fail to pack video frame");
                }
            }
        } else if (-2 == sn) {
            return srs_error_new(ERROR_RTC_RTP_MUXER, "video cache is overflow");
        } else {
            lost_sn_ = (uint16_t)sn;
        }
    }

    return err;
}

srs_error_t SrsRtmpFromRtcBridge::packet_video_key_frame(SrsRtpPacket* pkt)
{
    srs_error_t err = srs_success;

    // TODO: handle sps and pps in 2 rtp packets
    SrsRtpSTAPPayload* stap_payload = dynamic_cast<SrsRtpSTAPPayload*>(pkt->payload());
    if (stap_payload) {
        SrsSample* sps = stap_payload->get_sps();
        SrsSample* pps = stap_payload->get_pps();
        if (NULL == sps || NULL == pps) {
            return srs_error_new(ERROR_RTC_RTP_MUXER, "no sps or pps in stap-a rtp. sps: %p, pps:%p", sps, pps);
        } else {
            // h264 raw to h264 packet.
            std::string sh;
            SrsRawH264Stream* avc = new SrsRawH264Stream();
            SrsAutoFree(SrsRawH264Stream, avc);

            if ((err = avc->mux_sequence_header(string(sps->bytes, sps->size), string(pps->bytes, pps->size), sh)) != srs_success) {
                return srs_error_wrap(err, "mux sequence header");
            }

            // h264 packet to flv packet.
            char* flv = NULL;
            int nb_flv = 0;
            if ((err = avc->mux_avc2flv(sh, SrsVideoAvcFrameTypeKeyFrame, SrsVideoAvcFrameTraitSequenceHeader, pkt->get_avsync_time(),
                    pkt->get_avsync_time(), &flv, &nb_flv)) != srs_success) {
                return srs_error_wrap(err, "avc to flv");
            }

            SrsMessageHeader header;
            header.initialize_video(nb_flv, pkt->get_avsync_time(), 1);
            SrsCommonMessage rtmp;
            if ((err = rtmp.create(&header, flv, nb_flv)) != srs_success) {
                return srs_error_wrap(err, "create rtmp");
            }

            if ((err = source_->on_video(&rtmp)) != srs_success) {
                return err;
            }
        }
    }

    if (-1 == rtp_key_frame_ts_) {
        rtp_key_frame_ts_ = pkt->header.get_timestamp();
        header_sn_ = pkt->header.get_sequence();
        lost_sn_ = header_sn_ + 1;
        // Received key frame and clean cache of old p frame pkts
        clear_cached_video();
        srs_trace("set ts=%u, header=%hu, lost=%hu", (uint32_t)rtp_key_frame_ts_, header_sn_, lost_sn_);
    } else if (rtp_key_frame_ts_ != pkt->header.get_timestamp()) {
        //new key frame, clean cache
        int64_t old_ts = rtp_key_frame_ts_;
        uint16_t old_header_sn = header_sn_;
        uint16_t old_lost_sn = lost_sn_;
        rtp_key_frame_ts_ = pkt->header.get_timestamp();
        header_sn_ = pkt->header.get_sequence();
        lost_sn_ = header_sn_ + 1;
        clear_cached_video();
        srs_warn("drop old ts=%u, header=%hu, lost=%hu, set new ts=%u, header=%hu, lost=%hu",
            (uint32_t)old_ts, old_header_sn, old_lost_sn, (uint32_t)rtp_key_frame_ts_, header_sn_, lost_sn_);
    }

    uint16_t index = cache_index(pkt->header.get_sequence());
    cache_video_pkts_[index].in_use = true;
    srs_freep(cache_video_pkts_[index].pkt);
    cache_video_pkts_[index].pkt = pkt;
    cache_video_pkts_[index].sn = pkt->header.get_sequence();
    cache_video_pkts_[index].ts = pkt->get_avsync_time();
    cache_video_pkts_[index].rtp_ts = pkt->header.get_timestamp();

    int32_t sn = lost_sn_;
    uint16_t tail_sn = 0;
    if (srs_rtp_seq_distance(header_sn_, pkt->header.get_sequence()) < 0){
        // When receive previous pkt in the same frame, update header sn;
        header_sn_ = pkt->header.get_sequence();
        sn = find_next_lost_sn(header_sn_, tail_sn);
    } else if (lost_sn_ == pkt->header.get_sequence()) {
        sn = find_next_lost_sn(lost_sn_, tail_sn);
    }

    if (-1 == sn) {
        if (check_frame_complete(header_sn_, tail_sn)) {
            if ((err = packet_video_rtmp(header_sn_, tail_sn)) != srs_success) {
                err = srs_error_wrap(err, "fail to packet key frame");
            }
        }
    } else if (-2 == sn) {
        return srs_error_new(ERROR_RTC_RTP_MUXER, "video cache is overflow");
    } else {
        lost_sn_ = (uint16_t)sn;
    }

    return err;
}

srs_error_t SrsRtmpFromRtcBridge::packet_video_rtmp(const uint16_t start, const uint16_t end)
{
    srs_error_t err = srs_success;

    int nb_payload = 0;
    int16_t cnt = srs_rtp_seq_distance(start, end) + 1;
    srs_assert(cnt >= 1);

    for (uint16_t i = 0; i < (uint16_t)cnt; ++i) {
        uint16_t sn = start + i;
        uint16_t index = cache_index(sn);
        SrsRtpPacket* pkt = cache_video_pkts_[index].pkt;

        // fix crash when pkt->payload() if pkt is nullptr;
        if (!pkt) continue;

        // calculate nalu len
        SrsRtpFUAPayload2* fua_payload = dynamic_cast<SrsRtpFUAPayload2*>(pkt->payload());
        if (fua_payload && fua_payload->size > 0) {
            if (fua_payload->start) {
                nb_payload += 1 + 4;
            }
            nb_payload += fua_payload->size;
            continue;
        }

        SrsRtpSTAPPayload* stap_payload = dynamic_cast<SrsRtpSTAPPayload*>(pkt->payload());
        if (stap_payload) {
            for (int j = 0; j < (int)stap_payload->nalus.size(); ++j) {
                SrsSample* sample = stap_payload->nalus.at(j);
                if (sample->size > 0) {    
                    nb_payload += 4 + sample->size;
                }
            }
            continue;
        }

        SrsRtpRawPayload* raw_payload = dynamic_cast<SrsRtpRawPayload*>(pkt->payload());
        if (raw_payload && raw_payload->nn_payload > 0) {
            nb_payload += 4 + raw_payload->nn_payload;
            continue;
        }
    }

    if (0 == nb_payload) {
        srs_warn("empty nalu");
        return err;
    }
	
    //type_codec1 + avc_type + composition time + nalu size + nalu
    nb_payload += 1 + 1 + 3;

    SrsCommonMessage rtmp;
    SrsRtpPacket* pkt = cache_video_pkts_[cache_index(start)].pkt;
    rtmp.header.initialize_video(nb_payload, pkt->get_avsync_time(), 1);
    rtmp.create_payload(nb_payload);
    rtmp.size = nb_payload;
    SrsBuffer payload(rtmp.payload, rtmp.size);
    if (pkt->is_keyframe()) {
        payload.write_1bytes(0x17); // type(4 bits): key frame; code(4bits): avc
        rtp_key_frame_ts_ = -1;
    } else {
        payload.write_1bytes(0x27); // type(4 bits): inter frame; code(4bits): avc
    }
    payload.write_1bytes(0x01); // avc_type: nalu
    payload.write_1bytes(0x0);  // composition time
    payload.write_1bytes(0x0);
    payload.write_1bytes(0x0);

    int nalu_len = 0;
    for (uint16_t i = 0; i < (uint16_t)cnt; ++i) {
        uint16_t index = cache_index((start + i));
        SrsRtpPacket* pkt = cache_video_pkts_[index].pkt;

        // fix crash when pkt->payload() if pkt is nullptr;
        if (!pkt) continue;

        cache_video_pkts_[index].in_use = false;
        cache_video_pkts_[index].pkt = NULL;
        cache_video_pkts_[index].ts = 0;
        cache_video_pkts_[index].rtp_ts = 0;
        cache_video_pkts_[index].sn = 0;

        SrsRtpFUAPayload2* fua_payload = dynamic_cast<SrsRtpFUAPayload2*>(pkt->payload());
        if (fua_payload && fua_payload->size > 0) {
            if (fua_payload->start) {
                nalu_len = fua_payload->size + 1;
                //skip 4 bytes to write nalu_len future
                payload.skip(4);
                payload.write_1bytes(fua_payload->nri | fua_payload->nalu_type);
                payload.write_bytes(fua_payload->payload, fua_payload->size);
            } else {
                nalu_len += fua_payload->size;
                payload.write_bytes(fua_payload->payload, fua_payload->size);
                if (fua_payload->end) {
                    //write nalu_len back
                    payload.skip(-(4 + nalu_len));
                    payload.write_4bytes(nalu_len);
                    payload.skip(nalu_len);
                }
            }
            srs_freep(pkt);
            continue;
        }

        SrsRtpSTAPPayload* stap_payload = dynamic_cast<SrsRtpSTAPPayload*>(pkt->payload());
        if (stap_payload) {
            for (int j = 0; j < (int)stap_payload->nalus.size(); ++j) {
                SrsSample* sample = stap_payload->nalus.at(j);
                if (sample->size > 0) {  
                    payload.write_4bytes(sample->size);
                    payload.write_bytes(sample->bytes, sample->size);
                }
            }
            srs_freep(pkt);
            continue;
        }

        SrsRtpRawPayload* raw_payload = dynamic_cast<SrsRtpRawPayload*>(pkt->payload());
        if (raw_payload && raw_payload->nn_payload > 0) {
            payload.write_4bytes(raw_payload->nn_payload);
            payload.write_bytes(raw_payload->payload, raw_payload->nn_payload);
            srs_freep(pkt);
            continue;
        }

        srs_freep(pkt);
    }

    if ((err = source_->on_video(&rtmp)) != srs_success) {
        srs_warn("fail to pack video frame");
    }

    header_sn_ = end + 1;
    uint16_t tail_sn = 0;
    int sn = find_next_lost_sn(header_sn_, tail_sn);
    if (-1 == sn) {
        if (check_frame_complete(header_sn_, tail_sn)) {
            err = packet_video_rtmp(header_sn_, tail_sn);
        }
    } else if (-2 == sn) {
        return srs_error_new(ERROR_RTC_RTP_MUXER, "video cache is overflow");
    } else {
        lost_sn_ = sn;
    }

    return err;
}

int32_t SrsRtmpFromRtcBridge::find_next_lost_sn(uint16_t current_sn, uint16_t& end_sn)
{
    uint32_t last_rtp_ts = cache_video_pkts_[cache_index(header_sn_)].rtp_ts;
    for (int i = 0; i < s_cache_size; ++i) {
        uint16_t lost_sn = current_sn + i;
        int index = cache_index(lost_sn);

        if (!cache_video_pkts_[index].in_use) {
            return lost_sn;
        }
        //check time first, avoid two small frame mixed case decode fail
        if (last_rtp_ts != cache_video_pkts_[index].rtp_ts) {
            end_sn = lost_sn - 1;
            return -1;
        }

        if (cache_video_pkts_[index].pkt->header.get_marker()) {
            end_sn = lost_sn;
            return -1;
        }
    }

    srs_error("cache overflow. the packet count of video frame is more than %u", s_cache_size);
    return -2;
}

void SrsRtmpFromRtcBridge::clear_cached_video()
{
    for (size_t i = 0; i < s_cache_size; i++)
    {
        if (cache_video_pkts_[i].in_use) {
            srs_freep(cache_video_pkts_[i].pkt);
            cache_video_pkts_[i].sn = 0;
            cache_video_pkts_[i].ts = 0;
            cache_video_pkts_[i].rtp_ts = 0;
            cache_video_pkts_[i].in_use = false;
        }
    }
}

bool SrsRtmpFromRtcBridge::check_frame_complete(const uint16_t start, const uint16_t end)
{
    int16_t cnt = srs_rtp_seq_distance(start, end) + 1;
    srs_assert(cnt >= 1);

    uint16_t fu_s_c = 0;
    uint16_t fu_e_c = 0;
    for (uint16_t i = 0; i < (uint16_t)cnt; ++i) {
        int index = cache_index((start + i));
        SrsRtpPacket* pkt = cache_video_pkts_[index].pkt;

        // fix crash when pkt->payload() if pkt is nullptr;
        if (!pkt) continue;

        SrsRtpFUAPayload2* fua_payload = dynamic_cast<SrsRtpFUAPayload2*>(pkt->payload());
        if (!fua_payload) continue;

        if (fua_payload->start) {
            ++fu_s_c;
        }

        if (fua_payload->end) {
            ++fu_e_c;
        }
    }

    return fu_s_c == fu_e_c;
}
#endif

SrsCodecPayload::SrsCodecPayload()
{
    pt_of_publisher_ = pt_ = 0;
    sample_ = 0;
}

SrsCodecPayload::SrsCodecPayload(uint8_t pt, std::string encode_name, int sample)
{
    pt_of_publisher_ = pt_ = pt;
    name_ = encode_name;
    sample_ = sample;
}

SrsCodecPayload::~SrsCodecPayload()
{
}

SrsCodecPayload* SrsCodecPayload::copy()
{
    SrsCodecPayload* cp = new SrsCodecPayload();

    cp->type_ = type_;
    cp->pt_ = pt_;
    cp->pt_of_publisher_ = pt_of_publisher_;
    cp->name_ = name_;
    cp->sample_ = sample_;
    cp->rtcp_fbs_ = rtcp_fbs_;

    return cp;
}

SrsMediaPayloadType SrsCodecPayload::generate_media_payload_type()
{
    SrsMediaPayloadType media_payload_type(pt_);

    media_payload_type.encoding_name_ = name_;
    media_payload_type.clock_rate_ = sample_;
    media_payload_type.rtcp_fb_ = rtcp_fbs_;

    return media_payload_type;
}

SrsVideoPayload::SrsVideoPayload()
{
    type_ = "video";
}

SrsVideoPayload::SrsVideoPayload(uint8_t pt, std::string encode_name, int sample)
    :SrsCodecPayload(pt, encode_name, sample)
{
    type_ = "video";
    h264_param_.profile_level_id = "";
    h264_param_.packetization_mode = "";
    h264_param_.level_asymmerty_allow = "";
}

SrsVideoPayload::~SrsVideoPayload()
{
}

SrsVideoPayload* SrsVideoPayload::copy()
{
    SrsVideoPayload* cp = new SrsVideoPayload();

    cp->type_ = type_;
    cp->pt_ = pt_;
    cp->pt_of_publisher_ = pt_of_publisher_;
    cp->name_ = name_;
    cp->sample_ = sample_;
    cp->rtcp_fbs_ = rtcp_fbs_;
    cp->h264_param_ = h264_param_;

    return cp;
}

SrsMediaPayloadType SrsVideoPayload::generate_media_payload_type()
{
    SrsMediaPayloadType media_payload_type(pt_);

    media_payload_type.encoding_name_ = name_;
    media_payload_type.clock_rate_ = sample_;
    media_payload_type.rtcp_fb_ = rtcp_fbs_;

    std::ostringstream format_specific_param;
    if (!h264_param_.level_asymmerty_allow.empty()) {
        format_specific_param << "level-asymmetry-allowed=" << h264_param_.level_asymmerty_allow;
    }
    if (!h264_param_.packetization_mode.empty()) {
        format_specific_param << ";packetization-mode=" << h264_param_.packetization_mode;
    }
    if (!h264_param_.profile_level_id.empty()) {
        format_specific_param << ";profile-level-id=" << h264_param_.profile_level_id;
    }

    media_payload_type.format_specific_param_ = format_specific_param.str();

    return media_payload_type;
}

srs_error_t SrsVideoPayload::set_h264_param_desc(std::string fmtp)
{
    srs_error_t err = srs_success;

    // For example: level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42e01f
    std::vector<std::string> attributes = split_str(fmtp, ";");

    for (size_t i = 0; i < attributes.size(); ++i) {
        std::string attribute = attributes.at(i);

        std::vector<std::string> kv = split_str(attribute, "=");
        if (kv.size() != 2) {
            return srs_error_new(ERROR_RTC_SDP_DECODE, "invalid h264 param=%s", attribute.c_str());
        }

        if (kv[0] == "profile-level-id") {
            h264_param_.profile_level_id = kv[1];
        } else if (kv[0] == "packetization-mode") {
            // 6.3.  Non-Interleaved Mode
            // This mode is in use when the value of the OPTIONAL packetization-mode
            // media type parameter is equal to 1.  This mode SHOULD be supported.
            // It is primarily intended for low-delay applications.  Only single NAL
            // unit packets, STAP-As, and FU-As MAY be used in this mode.  STAP-Bs,
            // MTAPs, and FU-Bs MUST NOT be used.  The transmission order of NAL
            // units MUST comply with the NAL unit decoding order.
            // @see https://tools.ietf.org/html/rfc6184#section-6.3
            h264_param_.packetization_mode = kv[1];
        } else if (kv[0] == "level-asymmetry-allowed") {
            h264_param_.level_asymmerty_allow = kv[1];
        } else {
            return srs_error_new(ERROR_RTC_SDP_DECODE, "invalid h264 param=%s", kv[0].c_str());
        }
    }

    return err;
}

SrsAudioPayload::SrsAudioPayload()
{
    channel_ = 0;
}

SrsAudioPayload::SrsAudioPayload(uint8_t pt, std::string encode_name, int sample, int channel)
    :SrsCodecPayload(pt, encode_name, sample)
{
    type_ = "audio";
    channel_ = channel;
    opus_param_.minptime = 0;
    opus_param_.use_inband_fec = false;
    opus_param_.usedtx = false;
}

SrsAudioPayload::~SrsAudioPayload()
{
}

SrsAudioPayload* SrsAudioPayload::copy()
{
    SrsAudioPayload* cp = new SrsAudioPayload();

    cp->type_ = type_;
    cp->pt_ = pt_;
    cp->pt_of_publisher_ = pt_of_publisher_;
    cp->name_ = name_;
    cp->sample_ = sample_;
    cp->rtcp_fbs_ = rtcp_fbs_;
    cp->channel_ = channel_;
    cp->opus_param_ = opus_param_;

    return cp;
}

SrsMediaPayloadType SrsAudioPayload::generate_media_payload_type()
{
    SrsMediaPayloadType media_payload_type(pt_);

    media_payload_type.encoding_name_ = name_;
    media_payload_type.clock_rate_ = sample_;
    if (channel_ != 0) {
        media_payload_type.encoding_param_ = srs_int2str(channel_);
    }
    media_payload_type.rtcp_fb_ = rtcp_fbs_;

    std::ostringstream format_specific_param;
    if (opus_param_.minptime) {
        format_specific_param << "minptime=" << opus_param_.minptime;
    }
    if (opus_param_.use_inband_fec) {
        format_specific_param << ";useinbandfec=1";
    }
    if (opus_param_.usedtx) {
        format_specific_param << ";usedtx=1";
    }
    media_payload_type.format_specific_param_ = format_specific_param.str();

    return media_payload_type;
}

srs_error_t SrsAudioPayload::set_opus_param_desc(std::string fmtp)
{
    srs_error_t err = srs_success;
    std::vector<std::string> vec = split_str(fmtp, ";");
    for (size_t i = 0; i < vec.size(); ++i) {
        std::vector<std::string> kv = split_str(vec[i], "=");
        if (kv.size() == 2) {
            if (kv[0] == "minptime") {
                opus_param_.minptime = (int)::atol(kv[1].c_str());
            } else if (kv[0] == "useinbandfec") {
                opus_param_.use_inband_fec = (kv[1] == "1") ? true : false;
            } else if (kv[0] == "usedtx") {
                opus_param_.usedtx = (kv[1] == "1") ? true : false;
            }
        } else {
            return srs_error_new(ERROR_RTC_SDP_DECODE, "invalid opus param=%s", vec[i].c_str());
        }
    }

    return err;
}

SrsRedPayload::SrsRedPayload()
{
    channel_ = 0;
}

SrsRedPayload::SrsRedPayload(uint8_t pt, std::string encode_name, int sample, int channel)
    :SrsCodecPayload(pt, encode_name, sample)
{
    channel_ = channel;
}

SrsRedPayload::~SrsRedPayload()
{
}

SrsRedPayload* SrsRedPayload::copy()
{
    SrsRedPayload* cp = new SrsRedPayload();

    cp->type_ = type_;
    cp->pt_ = pt_;
    cp->pt_of_publisher_ = pt_of_publisher_;
    cp->name_ = name_;
    cp->sample_ = sample_;
    cp->rtcp_fbs_ = rtcp_fbs_;
    cp->channel_ = channel_;

    return cp;
}

SrsMediaPayloadType SrsRedPayload::generate_media_payload_type()
{
    SrsMediaPayloadType media_payload_type(pt_);

    media_payload_type.encoding_name_ = name_;
    media_payload_type.clock_rate_ = sample_;
    if (channel_ != 0) {
        media_payload_type.encoding_param_ = srs_int2str(channel_);
    }
    media_payload_type.rtcp_fb_ = rtcp_fbs_;

    return media_payload_type;
}

SrsRtxPayloadDes::SrsRtxPayloadDes()
{
}

SrsRtxPayloadDes::SrsRtxPayloadDes(uint8_t pt, uint8_t apt):SrsCodecPayload(pt, "rtx", 8000), apt_(apt)
{
}

SrsRtxPayloadDes::~SrsRtxPayloadDes()
{
}

SrsRtxPayloadDes* SrsRtxPayloadDes::copy()
{
    SrsRtxPayloadDes* cp = new SrsRtxPayloadDes();

    cp->type_ = type_;
    cp->pt_ = pt_;
    cp->pt_of_publisher_ = pt_of_publisher_;
    cp->name_ = name_;
    cp->sample_ = sample_;
    cp->rtcp_fbs_ = rtcp_fbs_;
    cp->apt_ = apt_;

    return cp;
}

SrsMediaPayloadType SrsRtxPayloadDes::generate_media_payload_type()
{
    SrsMediaPayloadType media_payload_type(pt_);

    media_payload_type.encoding_name_ = name_;
    media_payload_type.clock_rate_ = sample_;
    std::ostringstream format_specific_param;
    format_specific_param << "fmtp:" << pt_ << " apt="<< apt_;

    media_payload_type.format_specific_param_ = format_specific_param.str();

    return media_payload_type;
}

SrsRtcTrackDescription::SrsRtcTrackDescription()
{
    ssrc_ = 0;
    rtx_ssrc_ = 0;
    fec_ssrc_ = 0;
    is_active_ = false;

    media_ = NULL;
    red_ = NULL;
    rtx_ = NULL;
    ulpfec_ = NULL;
}

SrsRtcTrackDescription::~SrsRtcTrackDescription()
{
    srs_freep(media_);
    srs_freep(red_);
    srs_freep(rtx_);
    srs_freep(ulpfec_);
}

bool SrsRtcTrackDescription::has_ssrc(uint32_t ssrc)
{
    if (!is_active_) {
        return false;
    }

    if (ssrc == ssrc_ || ssrc == rtx_ssrc_ || ssrc == fec_ssrc_) {
        return true;
    }

    return false;
}

void SrsRtcTrackDescription::add_rtp_extension_desc(int id, std::string uri)
{
    extmaps_[id] = uri;
}

void SrsRtcTrackDescription::del_rtp_extension_desc(std::string uri)
{
    for(std::map<int, std::string>::iterator it = extmaps_.begin(); it != extmaps_.end(); ++it) {
        if(uri == it->second) {
            extmaps_.erase(it++);
            break;
        }
    }
}

void SrsRtcTrackDescription::set_direction(std::string direction)
{
    direction_ = direction;
}

void SrsRtcTrackDescription::set_codec_payload(SrsCodecPayload* payload)
{
    media_ = payload;
}

void SrsRtcTrackDescription::create_auxiliary_payload(const std::vector<SrsMediaPayloadType> payloads)
{
    if (!payloads.size()) {
        return;
    }

    SrsMediaPayloadType payload = payloads.at(0);
    if (payload.encoding_name_ == "red"){
        srs_freep(red_);
        red_ = new SrsRedPayload(payload.payload_type_, "red", payload.clock_rate_, ::atol(payload.encoding_param_.c_str()));
    } else if (payload.encoding_name_ == "rtx") {
        srs_freep(rtx_);
        // TODO: FIXME: Rtx clock_rate should be payload.clock_rate_
        rtx_ = new SrsRtxPayloadDes(payload.payload_type_, ::atol(payload.encoding_param_.c_str()));
    } else if (payload.encoding_name_ == "ulpfec") {
        srs_freep(ulpfec_);
        ulpfec_ = new SrsCodecPayload(payload.payload_type_, "ulpfec", payload.clock_rate_);
    }
}

void SrsRtcTrackDescription::set_rtx_ssrc(uint32_t ssrc)
{
    rtx_ssrc_ = ssrc;
}

void SrsRtcTrackDescription::set_fec_ssrc(uint32_t ssrc)
{
    fec_ssrc_ = ssrc;
}

void SrsRtcTrackDescription::set_mid(std::string mid)
{
    mid_ = mid;
}

int SrsRtcTrackDescription::get_rtp_extension_id(std::string uri)
{
    for (std::map<int, std::string>::iterator it = extmaps_.begin(); it != extmaps_.end(); ++it) {
        if(uri == it->second) {
            return it->first;
        }
    }

    return 0;
}

SrsRtcTrackDescription* SrsRtcTrackDescription::copy()
{
    SrsRtcTrackDescription* cp = new SrsRtcTrackDescription();

    cp->type_ = type_;
    cp->id_ = id_;
    cp->ssrc_ = ssrc_;
    cp->fec_ssrc_ = fec_ssrc_;
    cp->rtx_ssrc_ = rtx_ssrc_;
    cp->extmaps_ = extmaps_;
    cp->direction_ = direction_;
    cp->mid_ = mid_;
    cp->msid_ = msid_;
    cp->is_active_ = is_active_;
    cp->media_ = media_ ? media_->copy():NULL;
    cp->red_ = red_ ? red_->copy():NULL;
    cp->rtx_ = rtx_ ? rtx_->copy():NULL;
    cp->ulpfec_ = ulpfec_ ? ulpfec_->copy():NULL;

    return cp;
}

SrsRtcSourceDescription::SrsRtcSourceDescription()
{
    audio_track_desc_ = NULL;
}

SrsRtcSourceDescription::~SrsRtcSourceDescription()
{
    srs_freep(audio_track_desc_);

    for (int i = 0; i < (int)video_track_descs_.size(); ++i) {
        srs_freep(video_track_descs_.at(i));
    }
    video_track_descs_.clear();
}

SrsRtcSourceDescription* SrsRtcSourceDescription::copy()
{
    SrsRtcSourceDescription* stream_desc = new SrsRtcSourceDescription();

    if (audio_track_desc_) {
        stream_desc->audio_track_desc_ = audio_track_desc_->copy();
    }

    for (int i = 0; i < (int)video_track_descs_.size(); ++i) {
        stream_desc->video_track_descs_.push_back(video_track_descs_.at(i)->copy());
    }

    return stream_desc;
}

SrsRtcTrackDescription* SrsRtcSourceDescription::find_track_description_by_ssrc(uint32_t ssrc)
{
    if (audio_track_desc_ && audio_track_desc_->has_ssrc(ssrc)) {
        return audio_track_desc_;
    }

    for (int i = 0; i < (int)video_track_descs_.size(); ++i) {
        if (video_track_descs_.at(i)->has_ssrc(ssrc)) {
            return video_track_descs_.at(i);
        }
    }

    return NULL;
}

SrsRtcRecvTrack::SrsRtcRecvTrack(SrsRtcConnection* session, SrsRtcTrackDescription* track_desc, bool is_audio)
{
    session_ = session;
    track_desc_ = track_desc->copy();
    nack_no_copy_ = false;

    if (is_audio) {
        rtp_queue_ = new SrsRtpRingBuffer(100);
        nack_receiver_ = new SrsRtpNackForReceiver(rtp_queue_, 100 * 2 / 3);
    } else {
        rtp_queue_ = new SrsRtpRingBuffer(1000);
        nack_receiver_ = new SrsRtpNackForReceiver(rtp_queue_, 1000 * 2 / 3);
    }

    last_sender_report_rtp_time_ = 0;
    last_sender_report_rtp_time1_ = 0;
    rate_ = 0.0;

    last_sender_report_sys_time_ = 0;
}

SrsRtcRecvTrack::~SrsRtcRecvTrack()
{
    srs_freep(rtp_queue_);
    srs_freep(nack_receiver_);
    srs_freep(track_desc_);
}

bool SrsRtcRecvTrack::has_ssrc(uint32_t ssrc)
{
    return track_desc_->has_ssrc(ssrc);
}

uint32_t SrsRtcRecvTrack::get_ssrc()
{
    return track_desc_->ssrc_;
}

void SrsRtcRecvTrack::update_rtt(int rtt)
{
    nack_receiver_->update_rtt(rtt);
}

void SrsRtcRecvTrack::update_send_report_time(const SrsNtp& ntp, uint32_t rtp_time)
{
    last_sender_report_ntp1_ = last_sender_report_ntp_;
    last_sender_report_rtp_time1_ = last_sender_report_rtp_time_;

    last_sender_report_ntp_ = ntp;
    last_sender_report_rtp_time_ = rtp_time;

    // TODO: FIXME: Use system wall clock.
    last_sender_report_sys_time_ = srs_update_system_time();

    if (last_sender_report_rtp_time1_ > 0) {
        // WebRTC using sender report to sync audio/video timestamp, because audio video have different timebase,
        // typical audio opus is 48000Hz, video is 90000Hz.
        // We using two sender report point to calculate avsync timestamp(clock time) with any given rtp timestamp.
        // For example, there are two history sender report of audio as below.
        //   sender_report1: rtp_time1 = 10000, ntp_time1 = 40000
        //   sender_report : rtp_time  = 10960, ntp_time  = 40020
        //   (rtp_time - rtp_time1) / (ntp_time - ntp_time1) = 960 / 20 = 48,
        // Now we can calcualte ntp time(ntp_x) of any given rtp timestamp(rtp_x),
        //   (rtp_x - rtp_time) / (ntp_x - ntp_time) = 48   =>   ntp_x = (rtp_x - rtp_time) / 48 + ntp_time;
        double sys_time_elapsed = static_cast<double>(last_sender_report_ntp_.system_ms_) - static_cast<double>(last_sender_report_ntp1_.system_ms_);

        // Check sys_time_elapsed is equal to zero.
        if (fpclassify(sys_time_elapsed) == FP_ZERO) {
            return;
        }
        
        double rtp_time_elpased = static_cast<double>(last_sender_report_rtp_time_) - static_cast<double>(last_sender_report_rtp_time1_);
        double rate = round(rtp_time_elpased / sys_time_elapsed);

        // TODO: FIXME: use the sample rate from sdp.
        if (rate > 0) {
            rate_ = rate;
        }
    }
}

int64_t SrsRtcRecvTrack::cal_avsync_time(uint32_t rtp_time)
{
    if (rate_ < 0.001) {
        return -1;
    }

    double delta = round((rtp_time - last_sender_report_rtp_time_) / rate_);

    int64_t avsync_time = delta + last_sender_report_ntp_.system_ms_;

    return avsync_time;
}

srs_error_t SrsRtcRecvTrack::send_rtcp_rr()
{
    srs_error_t err = srs_success;

    uint32_t ssrc = track_desc_->ssrc_;
    const uint64_t& last_time = last_sender_report_sys_time_;
    if ((err = session_->send_rtcp_rr(ssrc, rtp_queue_, last_time, last_sender_report_ntp_)) != srs_success) {
        return srs_error_wrap(err, "ssrc=%u, last_time=%" PRId64, ssrc, last_time);
    }

    return err;
}

srs_error_t SrsRtcRecvTrack::send_rtcp_xr_rrtr()
{
    srs_error_t err = srs_success;

    if ((err = session_->send_rtcp_xr_rrtr(track_desc_->ssrc_)) != srs_success) {
        return srs_error_wrap(err, "ssrc=%u", track_desc_->ssrc_);
    }

    return err;
}

bool SrsRtcRecvTrack::set_track_status(bool active)
{
    bool previous_status = track_desc_->is_active_;
    track_desc_->is_active_ = active;
    return previous_status;
}

bool SrsRtcRecvTrack::get_track_status()
{
    return track_desc_->is_active_;
}

std::string SrsRtcRecvTrack::get_track_id()
{
    return track_desc_->id_;
}

srs_error_t SrsRtcRecvTrack::on_nack(SrsRtpPacket** ppkt)
{
    srs_error_t err = srs_success;

    SrsRtpPacket* pkt = *ppkt;
    uint16_t seq = pkt->header.get_sequence();
    SrsRtpNackInfo* nack_info = nack_receiver_->find(seq);
    if (nack_info) {
        // seq had been received.
        nack_receiver_->remove(seq);
        return err;
    }

    // insert check nack list
    uint16_t nack_first = 0, nack_last = 0;
    if (!rtp_queue_->update(seq, nack_first, nack_last)) {
        srs_warn("NACK: too old seq %u, range [%u, %u]", seq, rtp_queue_->begin, rtp_queue_->end);
    }
    if (srs_rtp_seq_distance(nack_first, nack_last) > 0) {
        srs_trace("NACK: update seq=%u, nack range [%u, %u]", seq, nack_first, nack_last);
        nack_receiver_->insert(nack_first, nack_last);
        nack_receiver_->check_queue_size();
    }

    // insert into video_queue and audio_queue
    // We directly use the pkt, never copy it, so we should set the pkt to NULL.
    if (nack_no_copy_) {
        rtp_queue_->set(seq, pkt);
        *ppkt = NULL;
    } else {
        rtp_queue_->set(seq, pkt->copy());
    }

    return err;
}

srs_error_t SrsRtcRecvTrack::do_check_send_nacks(uint32_t& timeout_nacks)
{
    srs_error_t err = srs_success;

    uint32_t sent_nacks = 0;
    session_->check_send_nacks(nack_receiver_, track_desc_->ssrc_, sent_nacks, timeout_nacks);

    return err;
}

SrsRtcAudioRecvTrack::SrsRtcAudioRecvTrack(SrsRtcConnection* session, SrsRtcTrackDescription* track_desc)
    : SrsRtcRecvTrack(session, track_desc, true)
{
}

SrsRtcAudioRecvTrack::~SrsRtcAudioRecvTrack()
{
}

void SrsRtcAudioRecvTrack::on_before_decode_payload(SrsRtpPacket* pkt, SrsBuffer* buf, ISrsRtpPayloader** ppayload, SrsRtspPacketPayloadType* ppt)
{
    // No payload, ignore.
    if (buf->empty()) {
        return;
    }

    *ppayload = new SrsRtpRawPayload();
    *ppt = SrsRtspPacketPayloadTypeRaw;
}

srs_error_t SrsRtcAudioRecvTrack::on_rtp(SrsRtcSource* source, SrsRtpPacket* pkt)
{
    srs_error_t err = srs_success;

    pkt->set_avsync_time(cal_avsync_time(pkt->header.get_timestamp()));

    if ((err = source->on_rtp(pkt)) != srs_success) {
        return srs_error_wrap(err, "source on rtp");
    }

    return err;
}

srs_error_t SrsRtcAudioRecvTrack::check_send_nacks()
{
    srs_error_t err = srs_success;

    ++_srs_pps_sanack->sugar;

    uint32_t timeout_nacks = 0;
    if ((err = do_check_send_nacks(timeout_nacks)) != srs_success) {
        return srs_error_wrap(err, "audio");
    }

    return err;
}

SrsRtcVideoRecvTrack::SrsRtcVideoRecvTrack(SrsRtcConnection* session, SrsRtcTrackDescription* track_desc)
    : SrsRtcRecvTrack(session, track_desc, false)
{
}

SrsRtcVideoRecvTrack::~SrsRtcVideoRecvTrack()
{
}

void SrsRtcVideoRecvTrack::on_before_decode_payload(SrsRtpPacket* pkt, SrsBuffer* buf, ISrsRtpPayloader** ppayload, SrsRtspPacketPayloadType* ppt)
{
    // No payload, ignore.
    if (buf->empty()) {
        return;
    }

    uint8_t v = (uint8_t)(buf->head()[0] & kNalTypeMask);
    pkt->nalu_type = SrsAvcNaluType(v);

    if (v == kStapA) {
        *ppayload = new SrsRtpSTAPPayload();
        *ppt = SrsRtspPacketPayloadTypeSTAP;
    } else if (v == kFuA) {
        *ppayload = new SrsRtpFUAPayload2();
        *ppt = SrsRtspPacketPayloadTypeFUA2;
    } else {
        *ppayload = new SrsRtpRawPayload();
        *ppt = SrsRtspPacketPayloadTypeRaw;
    }
}

srs_error_t SrsRtcVideoRecvTrack::on_rtp(SrsRtcSource* source, SrsRtpPacket* pkt)
{
    srs_error_t err = srs_success;

    pkt->set_avsync_time(cal_avsync_time(pkt->header.get_timestamp()));

    if ((err = source->on_rtp(pkt)) != srs_success) {
        return srs_error_wrap(err, "source on rtp");
    }

    return err;
}

srs_error_t SrsRtcVideoRecvTrack::check_send_nacks()
{
    srs_error_t err = srs_success;

    ++_srs_pps_svnack->sugar;

    uint32_t timeout_nacks = 0;
    if ((err = do_check_send_nacks(timeout_nacks)) != srs_success) {
        return srs_error_wrap(err, "video");
    }

    // If NACK timeout, start PLI if not requesting.
    if (timeout_nacks == 0) {
        return err;
    }

    srs_trace2(TAG_MAYBE, "RTC: NACK timeout=%u, request PLI, track=%s, ssrc=%u", timeout_nacks,
        track_desc_->id_.c_str(), track_desc_->ssrc_);

    return err;
}

SrsRtcTsJitter::SrsRtcTsJitter(uint32_t base)
{
    int32_t threshold = 3 * 90 * 1000; // 3s in TBN=90K.
    jitter_ = new SrsRtcJitter<uint32_t, int32_t>(base, threshold, srs_rtp_ts_distance);
}

SrsRtcTsJitter::~SrsRtcTsJitter()
{
    srs_freep(jitter_);
}

uint32_t SrsRtcTsJitter::correct(uint32_t value)
{
    return jitter_->correct(value);
}

SrsRtcSeqJitter::SrsRtcSeqJitter(uint16_t base)
{
    jitter_ = new SrsRtcJitter<uint16_t, int16_t>(base, 128, srs_rtp_seq_distance);
}

SrsRtcSeqJitter::~SrsRtcSeqJitter()
{
    srs_freep(jitter_);
}

uint16_t SrsRtcSeqJitter::correct(uint16_t value)
{
    return jitter_->correct(value);
}

SrsRtcSendTrack::SrsRtcSendTrack(SrsRtcConnection* session, SrsRtcTrackDescription* track_desc, bool is_audio)
{
    session_ = session;
    track_desc_ = track_desc->copy();
    nack_no_copy_ = false;

    // Make a different start of sequence number, for debugging.
    jitter_ts_ = new SrsRtcTsJitter(track_desc_->type_ == "audio" ? 10000 : 20000);
    jitter_seq_ = new SrsRtcSeqJitter(track_desc_->type_ == "audio" ? 100 : 200);

    if (is_audio) {
        rtp_queue_ = new SrsRtpRingBuffer(100);
    } else {
        rtp_queue_ = new SrsRtpRingBuffer(1000);
    }

    nack_epp = new SrsErrorPithyPrint();
}

SrsRtcSendTrack::~SrsRtcSendTrack()
{
    srs_freep(rtp_queue_);
    srs_freep(track_desc_);
    srs_freep(nack_epp);
    srs_freep(jitter_ts_);
    srs_freep(jitter_seq_);
}

bool SrsRtcSendTrack::has_ssrc(uint32_t ssrc)
{
    return track_desc_->has_ssrc(ssrc);
}

SrsRtpPacket* SrsRtcSendTrack::fetch_rtp_packet(uint16_t seq)
{
    SrsRtpPacket* pkt = rtp_queue_->at(seq);

    if (pkt == NULL) {
        return pkt;
    }

    // For NACK, it sequence must match exactly, or it cause SRTP fail.
    // Return packet only when sequence is equal.
    if (pkt->header.get_sequence() == seq) {
        ++_srs_pps_rhnack->sugar;
        return pkt;
    }
    ++_srs_pps_rmnack->sugar;

    // Ignore if sequence not match.
    uint32_t nn = 0;
    if (nack_epp->can_print(pkt->header.get_ssrc(), &nn)) {
        srs_trace("RTC: NACK miss seq=%u, require_seq=%u, ssrc=%u, ts=%u, count=%u/%u, %d bytes", seq, pkt->header.get_sequence(),
            pkt->header.get_ssrc(), pkt->header.get_timestamp(), nn, nack_epp->nn_count, pkt->nb_bytes());
    }
    return NULL;
}

// TODO: FIXME: Should refine logs, set tracks in a time.
bool SrsRtcSendTrack::set_track_status(bool active)
{
    bool previous_status = track_desc_->is_active_;
    track_desc_->is_active_ = active;
    return previous_status;
}

bool SrsRtcSendTrack::get_track_status()
{
    return track_desc_->is_active_;
}

std::string SrsRtcSendTrack::get_track_id()
{
    return track_desc_->id_;
}

void SrsRtcSendTrack::rebuild_packet(SrsRtpPacket* pkt)
{
    // Rebuild the sequence number.
    int16_t seq = pkt->header.get_sequence();
    pkt->header.set_sequence(jitter_seq_->correct(seq));

    // Rebuild the timestamp.
    uint32_t ts = pkt->header.get_timestamp();
    pkt->header.set_timestamp(jitter_ts_->correct(ts));

    srs_info("RTC: Correct %s seq=%u/%u, ts=%u/%u", track_desc_->type_.c_str(), seq, pkt->header.get_sequence(), ts, pkt->header.get_timestamp());
}

srs_error_t SrsRtcSendTrack::on_nack(SrsRtpPacket** ppkt)
{
    srs_error_t err = srs_success;

    SrsRtpPacket* pkt = *ppkt;
    uint16_t seq = pkt->header.get_sequence();

    // insert into video_queue and audio_queue
    // We directly use the pkt, never copy it, so we should set the pkt to NULL.
    if (nack_no_copy_) {
        rtp_queue_->set(seq, pkt);
        *ppkt = NULL;
    } else {
        rtp_queue_->set(seq, pkt->copy());
    }

    return err;
}

srs_error_t SrsRtcSendTrack::on_recv_nack(const vector<uint16_t>& lost_seqs)
{
    srs_error_t err = srs_success;

    ++_srs_pps_rnack2->sugar;

    for(int i = 0; i < (int)lost_seqs.size(); ++i) {
        uint16_t seq = lost_seqs.at(i);
        SrsRtpPacket* pkt = fetch_rtp_packet(seq);
        if (pkt == NULL) {
            continue;
        }

        uint32_t nn = 0;
        if (nack_epp->can_print(pkt->header.get_ssrc(), &nn)) {
            srs_trace("RTC: NACK ARQ seq=%u, ssrc=%u, ts=%u, count=%u/%u, %d bytes", pkt->header.get_sequence(),
                pkt->header.get_ssrc(), pkt->header.get_timestamp(), nn, nack_epp->nn_count, pkt->nb_bytes());
        }

        // By default, we send packets by sendmmsg.
        if ((err = session_->do_send_packet(pkt)) != srs_success) {
            return srs_error_wrap(err, "raw send");
        }
    }

    return err;
}

SrsRtcAudioSendTrack::SrsRtcAudioSendTrack(SrsRtcConnection* session, SrsRtcTrackDescription* track_desc)
    : SrsRtcSendTrack(session, track_desc, true)
{
}

SrsRtcAudioSendTrack::~SrsRtcAudioSendTrack()
{
}

srs_error_t SrsRtcAudioSendTrack::on_rtp(SrsRtpPacket* pkt)
{
    srs_error_t err = srs_success;

    if (!track_desc_->is_active_) {
        return err;
    }

    pkt->header.set_ssrc(track_desc_->ssrc_);

    // Should update PT, because subscriber may use different PT to publisher.
    if (track_desc_->media_ && pkt->header.get_payload_type() == track_desc_->media_->pt_of_publisher_) {
        // If PT is media from publisher, change to PT of media for subscriber.
        pkt->header.set_payload_type(track_desc_->media_->pt_);
    } else if (track_desc_->red_ && pkt->header.get_payload_type() == track_desc_->red_->pt_of_publisher_) {
        // If PT is RED from publisher, change to PT of RED for subscriber.
        pkt->header.set_payload_type(track_desc_->red_->pt_);
    } else {
        // TODO: FIXME: Should update PT for RTX.
    }

    // Rebuild the sequence number and timestamp of packet, see https://github.com/ossrs/srs/issues/3167
    rebuild_packet(pkt);

    if ((err = session_->do_send_packet(pkt)) != srs_success) {
        return srs_error_wrap(err, "raw send");
    }

    srs_info("RTC: Send audio ssrc=%d, seqno=%d, keyframe=%d, ts=%u", pkt->header.get_ssrc(),
        pkt->header.get_sequence(), pkt->is_keyframe(), pkt->header.get_timestamp());

    return err;
}

srs_error_t SrsRtcAudioSendTrack::on_rtcp(SrsRtpPacket* pkt)
{
    srs_error_t err = srs_success;
    // process rtcp
    return err;
}

SrsRtcVideoSendTrack::SrsRtcVideoSendTrack(SrsRtcConnection* session, SrsRtcTrackDescription* track_desc)
    : SrsRtcSendTrack(session, track_desc, false)
{
}

SrsRtcVideoSendTrack::~SrsRtcVideoSendTrack()
{
}

srs_error_t SrsRtcVideoSendTrack::on_rtp(SrsRtpPacket* pkt)
{
    srs_error_t err = srs_success;

    if (!track_desc_->is_active_) {
        return err;
    }
    
    pkt->header.set_ssrc(track_desc_->ssrc_);

    // Should update PT, because subscriber may use different PT to publisher.
    if (track_desc_->media_ && pkt->header.get_payload_type() == track_desc_->media_->pt_of_publisher_) {
        // If PT is media from publisher, change to PT of media for subscriber.
        pkt->header.set_payload_type(track_desc_->media_->pt_);
    } else if (track_desc_->red_ && pkt->header.get_payload_type() == track_desc_->red_->pt_of_publisher_) {
        // If PT is RED from publisher, change to PT of RED for subscriber.
        pkt->header.set_payload_type(track_desc_->red_->pt_);
    } else {
        // TODO: FIXME: Should update PT for RTX.
    }

    // Rebuild the sequence number and timestamp of packet, see https://github.com/ossrs/srs/issues/3167
    rebuild_packet(pkt);

    if ((err = session_->do_send_packet(pkt)) != srs_success) {
        return srs_error_wrap(err, "raw send");
    }

    srs_info("RTC: Send video ssrc=%d, seqno=%d, keyframe=%d, ts=%u", pkt->header.get_ssrc(),
        pkt->header.get_sequence(), pkt->is_keyframe(), pkt->header.get_timestamp());

    return err;
}

srs_error_t SrsRtcVideoSendTrack::on_rtcp(SrsRtpPacket* pkt)
{
    srs_error_t err = srs_success;
    // process rtcp
    return err;
}

SrsRtcSSRCGenerator* SrsRtcSSRCGenerator::_instance = NULL;

SrsRtcSSRCGenerator::SrsRtcSSRCGenerator()
{
    ssrc_num = 0;
}

SrsRtcSSRCGenerator::~SrsRtcSSRCGenerator()
{
}

SrsRtcSSRCGenerator* SrsRtcSSRCGenerator::instance()
{
    if (!_instance) {
        _instance = new SrsRtcSSRCGenerator();
    }
    return _instance;
}

uint32_t SrsRtcSSRCGenerator::generate_ssrc()
{
    if (!ssrc_num) {
        ssrc_num = ::getpid() * 10000 + ::getpid() * 100 + ::getpid();
    }

    return ++ssrc_num;
}

