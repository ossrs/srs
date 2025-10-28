//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_app_rtc_source.hpp>

#include <math.h>
#include <unistd.h>

#include <srs_app_circuit_breaker.hpp>
#include <srs_app_config.hpp>
#include <srs_app_log.hpp>
#include <srs_app_rtc_conn.hpp>
#include <srs_app_rtmp_source.hpp>
#include <srs_app_server.hpp>
#include <srs_app_statistic.hpp>
#include <srs_core_autofree.hpp>
#include <srs_core_deprecated.hpp>
#include <srs_kernel_buffer.hpp>
#include <srs_kernel_codec.hpp>
#include <srs_kernel_flv.hpp>
#include <srs_kernel_hourglass.hpp>
#include <srs_kernel_pithy_print.hpp>
#include <srs_kernel_rtc_queue.hpp>
#include <srs_kernel_rtc_rtp.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_conn.hpp>
#include <srs_protocol_format.hpp>
#include <srs_protocol_json.hpp>
#include <srs_protocol_rtmp_msg_array.hpp>
#include <srs_protocol_rtmp_stack.hpp>
#include <srs_protocol_utility.hpp>
#ifdef SRS_FFMPEG_FIT
#include <srs_app_rtc_codec.hpp>
#endif
#include <srs_app_factory.hpp>
#include <srs_kernel_kbps.hpp>
#include <srs_protocol_raw_avc.hpp>
#include <srs_protocol_rtp.hpp>

// The NACK sent by us(SFU).
SrsPps *_srs_pps_snack = NULL;
SrsPps *_srs_pps_snack2 = NULL;
SrsPps *_srs_pps_snack3 = NULL;
SrsPps *_srs_pps_snack4 = NULL;
SrsPps *_srs_pps_sanack = NULL;
SrsPps *_srs_pps_svnack = NULL;

SrsPps *_srs_pps_rnack = NULL;
SrsPps *_srs_pps_rnack2 = NULL;
SrsPps *_srs_pps_rhnack = NULL;
SrsPps *_srs_pps_rmnack = NULL;

extern SrsPps *_srs_pps_aloss2;
extern SrsServer *_srs_server;

static const int kAudioChannel = 2;
static const int kAudioSamplerate = 48000;

static const int kVideoSamplerate = 90000;

using namespace std;

// the time to cleanup source.
#define SRS_RTC_SOURCE_CLEANUP (3 * SRS_UTIME_SECONDS)

// TODO: Add this function into SrsRtpMux class.
srs_error_t aac_raw_append_adts_header(SrsMediaPacket *shared_audio, SrsFormat *format, char **pbuf, int *pnn_buf)
{
    srs_error_t err = srs_success;

    if (format->is_aac_sequence_header()) {
        return err;
    }

    // If no audio RAW frame, or not parsed for no sequence header, drop the packet.
    if (format->audio_->nb_samples_ == 0) {
        srs_warn("RTC: Drop AAC %d bytes for no sample", shared_audio->size());
        return err;
    }

    if (format->audio_->nb_samples_ != 1) {
        return srs_error_new(ERROR_RTC_RTP_MUXER, "adts samples=%d", format->audio_->nb_samples_);
    }

    int nb_buf = format->audio_->samples_[0].size_ + 7;
    char *buf = new char[nb_buf];
    SrsBuffer stream(buf, nb_buf);

    // TODO: Add comment.
    stream.write_1bytes(0xFF);
    stream.write_1bytes(0xF9);
    stream.write_1bytes(((format->acodec_->aac_object_ - 1) << 6) | ((format->acodec_->aac_sample_rate_ & 0x0F) << 2) | ((format->acodec_->aac_channels_ & 0x04) >> 2));
    stream.write_1bytes(((format->acodec_->aac_channels_ & 0x03) << 6) | ((nb_buf >> 11) & 0x03));
    stream.write_1bytes((nb_buf >> 3) & 0xFF);
    stream.write_1bytes(((nb_buf & 0x07) << 5) | 0x1F);
    stream.write_1bytes(0xFC);

    stream.write_bytes(format->audio_->samples_[0].bytes_, format->audio_->samples_[0].size_);

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

ISrsRtcSourceForConsumer::ISrsRtcSourceForConsumer()
{
}

ISrsRtcSourceForConsumer::~ISrsRtcSourceForConsumer()
{
}

ISrsRtcConsumer::ISrsRtcConsumer()
{
}

ISrsRtcConsumer::~ISrsRtcConsumer()
{
}

SrsRtcConsumer::SrsRtcConsumer(ISrsRtcSourceForConsumer *s)
{
    source_ = s;
    should_update_source_id_ = false;
    handler_ = NULL;

    mw_wait_ = srs_cond_new();
    mw_min_msgs_ = 0;
    mw_waiting_ = false;
}

SrsRtcConsumer::~SrsRtcConsumer()
{
    source_->on_consumer_destroy(this);

    vector<SrsRtpPacket *>::iterator it;
    for (it = queue_.begin(); it != queue_.end(); ++it) {
        SrsRtpPacket *pkt = *it;
        srs_freep(pkt);
    }

    srs_cond_destroy(mw_wait_);
}

void SrsRtcConsumer::update_source_id()
{
    should_update_source_id_ = true;
}

srs_error_t SrsRtcConsumer::enqueue(SrsRtpPacket *pkt)
{
    srs_error_t err = srs_success;

    queue_.push_back(pkt);

    if (mw_waiting_) {
        if ((int)queue_.size() > mw_min_msgs_) {
            srs_cond_signal(mw_wait_);
            mw_waiting_ = false;
            return err;
        }
    }

    return err;
}

srs_error_t SrsRtcConsumer::dump_packet(SrsRtpPacket **ppkt)
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

void SrsRtcConsumer::wait(int nb_msgs)
{
    mw_min_msgs_ = nb_msgs;

    // when duration ok, signal to flush.
    if ((int)queue_.size() > mw_min_msgs_) {
        return;
    }

    // the enqueue will notify this cond.
    mw_waiting_ = true;

    // use cond block wait for high performance mode.
    srs_cond_wait(mw_wait_);
}

void SrsRtcConsumer::on_stream_change(SrsRtcSourceDescription *desc)
{
    if (handler_) {
        handler_->on_stream_change(desc);
    }
}

ISrsRtcSourceManager::ISrsRtcSourceManager()
{
}

ISrsRtcSourceManager::~ISrsRtcSourceManager()
{
}

SrsRtcSourceManager::SrsRtcSourceManager()
{
    lock_ = srs_mutex_new();
    timer_ = new SrsHourGlass("sources", this, 1 * SRS_UTIME_SECONDS);
}

SrsRtcSourceManager::~SrsRtcSourceManager()
{
    srs_mutex_destroy(lock_);
    srs_freep(timer_);
}

srs_error_t SrsRtcSourceManager::initialize()
{
    return setup_ticks();
}

srs_error_t SrsRtcSourceManager::setup_ticks()
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

srs_error_t SrsRtcSourceManager::notify(int event, srs_utime_t interval, srs_utime_t tick)
{
    srs_error_t err = srs_success;

    std::map<std::string, SrsSharedPtr<SrsRtcSource> >::iterator it;
    for (it = pool_.begin(); it != pool_.end();) {
        SrsSharedPtr<SrsRtcSource> &source = it->second;

        // When source expired, remove it.
        // @see https://github.com/ossrs/srs/issues/713
        if (source->stream_is_dead()) {
            SrsContextId cid = source->source_id();
            if (cid.empty())
                cid = source->pre_source_id();
            srs_trace("RTC: cleanup die source, id=[%s], total=%d", cid.c_str(), (int)pool_.size());
            pool_.erase(it++);
        } else {
            ++it;
        }
    }

    return err;
}

srs_error_t SrsRtcSourceManager::fetch_or_create(ISrsRequest *r, SrsSharedPtr<SrsRtcSource> &pps)
{
    srs_error_t err = srs_success;

    bool created = false;
    // Should never invoke any function during the locking.
    if (true) {
        // Use lock to protect coroutine switch.
        // @bug https://github.com/ossrs/srs/issues/1230
        SrsLocker(&lock_);

        string stream_url = r->get_stream_url();
        std::map<std::string, SrsSharedPtr<SrsRtcSource> >::iterator it = pool_.find(stream_url);

        if (it != pool_.end()) {
            SrsSharedPtr<SrsRtcSource> source = it->second;
            pps = source;
        } else {
            SrsSharedPtr<SrsRtcSource> source = SrsSharedPtr<SrsRtcSource>(new SrsRtcSource());
            srs_trace("new rtc source, stream_url=%s", stream_url.c_str());
            pps = source;

            pool_[stream_url] = source;
            created = true;
        }
    }

    // Initialize source.
    if (created && (err = pps->initialize(r)) != srs_success) {
        return srs_error_wrap(err, "init source %s", r->get_stream_url().c_str());
    }

    // we always update the request of resource,
    // for origin auth is on, the token in request maybe invalid,
    // and we only need to update the token of request, it's simple.
    if (!created) {
        pps->update_auth(r);
    }

    return err;
}

SrsSharedPtr<SrsRtcSource> SrsRtcSourceManager::fetch(ISrsRequest *r)
{
    // Use lock to protect coroutine switch.
    // @bug https://github.com/ossrs/srs/issues/1230
    SrsLocker(&lock_);

    string stream_url = r->get_stream_url();
    std::map<std::string, SrsSharedPtr<SrsRtcSource> >::iterator it = pool_.find(stream_url);

    SrsSharedPtr<SrsRtcSource> source;
    if (it == pool_.end()) {
        return source;
    }

    source = it->second;
    return source;
}

SrsRtcSourceManager *_srs_rtc_sources = NULL;

ISrsRtcSourceEventHandler::ISrsRtcSourceEventHandler()
{
}

ISrsRtcSourceEventHandler::~ISrsRtcSourceEventHandler()
{
}

SrsRtcSource::SrsRtcSource()
{
    is_created_ = false;
    is_delivering_packets_ = false;

    publish_stream_ = NULL;
    stream_desc_ = NULL;

    req_ = NULL;
    rtc_bridge_ = NULL;
    circuit_breaker_ = _srs_circuit_breaker;

    pli_for_rtmp_ = pli_elapsed_ = 0;
    stream_die_at_ = 0;

    app_factory_ = _srs_app_factory;
}

SrsRtcSource::~SrsRtcSource()
{
    // never free the consumers,
    // for all consumers are auto free.
    consumers_.clear();

    srs_freep(rtc_bridge_);
    srs_freep(req_);
    srs_freep(stream_desc_);

    SrsContextId cid = _source_id;
    if (cid.empty())
        cid = _pre_source_id;
    srs_trace("free rtc source id=[%s]", cid.c_str());

    app_factory_ = NULL;
}

// CRITICAL: This method is called AFTER the source has been added to the source pool
// in the fetch_or_create pattern (see PR 4449).
//
// IMPORTANT: All field initialization in this method MUST NOT cause coroutine context switches
// before completing the basic field setup.
//
// If context switches occur before all fields are properly initialized, other coroutines
// accessing this source from the pool may encounter uninitialized state, leading to crashes
// or undefined behavior.
//
// This prevents the race condition where multiple coroutines could create duplicate sources
// for the same stream when context switches occurred during initialization.
srs_error_t SrsRtcSource::initialize(ISrsRequest *r)
{
    srs_error_t err = srs_success;

    srs_freep(req_);
    req_ = r->copy();

    // Create default relations to allow play before publishing.
    // @see https://github.com/ossrs/srs/issues/2362
    init_for_play_before_publishing();

    return err;
}

bool SrsRtcSource::stream_is_dead()
{
    // still publishing?
    if (is_created_) {
        return false;
    }

    // has any consumers?
    if (!consumers_.empty()) {
        return false;
    }

    // Delay cleanup source.
    srs_utime_t now = srs_time_now_cached();
    if (now < stream_die_at_ + SRS_RTC_SOURCE_CLEANUP) {
        return false;
    }

    return true;
}

// CRITICAL: This method is called AFTER the source has been added to the source pool
// in the fetch_or_create pattern (see PR 4449).
//
// IMPORTANT: All field initialization in this method MUST NOT cause coroutine context switches.
// This prevents the race condition where multiple coroutines could create duplicate sources
// for the same stream when context switches occurred during initialization.
void SrsRtcSource::init_for_play_before_publishing()
{
    // If the stream description has already been setup by RTC publisher,
    // we should ignore and it's ok, because we only need to setup it for bridge.
    if (stream_desc_) {
        return;
    }

    SrsUniquePtr<SrsRtcSourceDescription> stream_desc(new SrsRtcSourceDescription());

    SrsRand rand;

    // audio track description
    if (true) {
        SrsRtcTrackDescription *audio_track_desc = new SrsRtcTrackDescription();
        stream_desc->audio_track_desc_ = audio_track_desc;

        audio_track_desc->type_ = "audio";
        audio_track_desc->id_ = "audio-" + rand.gen_str(8);

        uint32_t audio_ssrc = SrsRtcSSRCGenerator::instance()->generate_ssrc();
        audio_track_desc->ssrc_ = audio_ssrc;
        audio_track_desc->direction_ = "recvonly";

        audio_track_desc->media_ = new SrsAudioPayload(kAudioPayloadType, "opus", kAudioSamplerate, kAudioChannel);
    }

    // video track descriptions - support both H.264 and H.265 for play before publishing
    // This allows clients to choose their preferred codec during SDP negotiation
    if (true) {
        // H.264 track description
        SrsRtcTrackDescription *h264_track_desc = new SrsRtcTrackDescription();
        stream_desc->video_track_descs_.push_back(h264_track_desc);

        h264_track_desc->type_ = "video";
        h264_track_desc->id_ = "video-h264-" + rand.gen_str(8);

        uint32_t h264_ssrc = SrsRtcSSRCGenerator::instance()->generate_ssrc();
        h264_track_desc->ssrc_ = h264_ssrc;
        h264_track_desc->direction_ = "recvonly";

        SrsVideoPayload *h264_payload = new SrsVideoPayload(kVideoPayloadType, "H264", kVideoSamplerate);
        h264_track_desc->media_ = h264_payload;

        h264_payload->set_h264_param_desc("level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42e01f");
    }

    if (true) {
        // H.265 track description
        SrsRtcTrackDescription *h265_track_desc = new SrsRtcTrackDescription();
        stream_desc->video_track_descs_.push_back(h265_track_desc);

        h265_track_desc->type_ = "video";
        h265_track_desc->id_ = "video-h265-" + rand.gen_str(8);

        uint32_t h265_ssrc = SrsRtcSSRCGenerator::instance()->generate_ssrc();
        h265_track_desc->ssrc_ = h265_ssrc;
        h265_track_desc->direction_ = "recvonly";

        SrsVideoPayload *h265_payload = new SrsVideoPayload(KVideoPayloadTypeHevc, "H265", kVideoSamplerate);
        h265_track_desc->media_ = h265_payload;

        h265_payload->set_h265_param_desc("level-id=156;profile-id=1;tier-flag=0;tx-mode=SRST");
    }

    set_stream_desc(stream_desc.get());
}

void SrsRtcSource::update_auth(ISrsRequest *r)
{
    req_->update_auth(r);
}

srs_error_t SrsRtcSource::on_source_changed()
{
    srs_error_t err = srs_success;

    // Update context id if changed.
    bool id_changed = false;
    const SrsContextId &id = _srs_context->get_id();
    if (_source_id.compare(id)) {
        id_changed = true;

        if (_pre_source_id.empty()) {
            _pre_source_id = id;
        }
        _source_id = id;
    }

    // Notify all consumers.
    std::vector<ISrsRtcConsumer *>::iterator it;
    for (it = consumers_.begin(); it != consumers_.end(); ++it) {
        ISrsRtcConsumer *consumer = *it;

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

void SrsRtcSource::set_bridge(ISrsRtcBridge *bridge)
{
    srs_freep(rtc_bridge_);
    rtc_bridge_ = bridge;
}

srs_error_t SrsRtcSource::create_consumer(ISrsRtcConsumer *&consumer)
{
    srs_error_t err = srs_success;

    consumer = new SrsRtcConsumer(this);
    consumers_.push_back(consumer);

    stream_die_at_ = 0;

    // TODO: FIXME: Implements edge cluster.

    return err;
}

srs_error_t SrsRtcSource::consumer_dumps(ISrsRtcConsumer *consumer, bool ds, bool dm, bool dg)
{
    srs_error_t err = srs_success;

    // print status.
    srs_trace("create consumer, no gop cache");

    return err;
}

void SrsRtcSource::on_consumer_destroy(ISrsRtcConsumer *consumer)
{
    std::vector<ISrsRtcConsumer *>::iterator it;
    it = std::find(consumers_.begin(), consumers_.end(), consumer);
    if (it != consumers_.end()) {
        it = consumers_.erase(it);
    }

    // When all consumers finished, notify publisher to handle it.
    if (publish_stream_ && consumers_.empty()) {
        for (size_t i = 0; i < event_handlers_.size(); i++) {
            ISrsRtcSourceEventHandler *h = event_handlers_.at(i);
            h->on_consumers_finished();
        }
    }

    // Destroy and cleanup source when no publishers and consumers.
    if (!is_created_ && consumers_.empty()) {
        stream_die_at_ = srs_time_now_cached();
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
    srs_assert(req_);

    // For RTC, DTLS is done, and we are ready to deliver packets.
    // @note For compatible with RTMP, we also set the is_created_, it MUST be created here.
    is_created_ = true;
    is_delivering_packets_ = true;

    // Notify the consumers about stream change event.
    if ((err = on_source_changed()) != srs_success) {
        return srs_error_wrap(err, "source id change");
    }

    // Setup the audio and video codec.
    SrsAudioCodecId audio_codec = SrsAudioCodecIdOpus;
    if (stream_desc_->audio_track_desc_ && stream_desc_->audio_track_desc_->media_) {
        audio_codec = SrsAudioCodecId(stream_desc_->audio_track_desc_->media_->codec(false));
    }

    SrsVideoCodecId video_codec = SrsVideoCodecIdAVC;
    if (stream_desc_->video_track_descs_.size() > 0) {
        SrsRtcTrackDescription *track_desc = stream_desc_->video_track_descs_.at(0);
        video_codec = SrsVideoCodecId(track_desc->media_->codec(true));
    }

    // If bridge to other source, handle event and start timer to request PLI.
    if (rtc_bridge_) {
        if ((err = rtc_bridge_->initialize(req_)) != srs_success) {
            return srs_error_wrap(err, "rtp bridge initialize");
        }

        if ((err = rtc_bridge_->setup_codec(audio_codec, video_codec)) != srs_success) {
            return srs_error_wrap(err, "rtp bridge setup codec");
        }

        if ((err = rtc_bridge_->on_publish()) != srs_success) {
            return srs_error_wrap(err, "rtp bridge on publish");
        }

        // The PLI interval for RTC2RTMP.
        pli_for_rtmp_ = _srs_config->get_rtc_pli_for_rtmp(req_->vhost_);

        // @see SrsRtcSource::on_timer()
        _srs_shared_timer->timer100ms()->subscribe(this);
    }

    SrsStatistic *stat = _srs_stat;
    stat->on_stream_publish(req_, _source_id.c_str());

    return err;
}

void SrsRtcSource::on_unpublish()
{
    // ignore when already unpublished.
    if (!is_created_) {
        return;
    }

    srs_trace("cleanup when unpublish, created=%u, deliver=%u", is_created_, is_delivering_packets_);

    if (!_source_id.empty()) {
        _pre_source_id = _source_id;
    }
    _source_id = SrsContextId();

    for (size_t i = 0; i < event_handlers_.size(); i++) {
        ISrsRtcSourceEventHandler *h = event_handlers_.at(i);
        h->on_unpublish();
    }

    // free bridge resource
    if (rtc_bridge_) {
        // For SrsRtcSource::on_timer()
        _srs_shared_timer->timer100ms()->unsubscribe(this);

        rtc_bridge_->on_unpublish();
        srs_freep(rtc_bridge_);
    }

    SrsStatistic *stat = _srs_stat;
    stat->on_stream_close(req_);

    // Destroy and cleanup source when no publishers and consumers.
    if (consumers_.empty()) {
        stream_die_at_ = srs_time_now_cached();
    }

    // Should never change the final state before all cleanup is done.
    is_created_ = false;
    is_delivering_packets_ = false;
}

void SrsRtcSource::rtc_source_subscribe(ISrsRtcSourceEventHandler *h)
{
    if (std::find(event_handlers_.begin(), event_handlers_.end(), h) == event_handlers_.end()) {
        event_handlers_.push_back(h);
    }
}

void SrsRtcSource::rtc_source_unsubscribe(ISrsRtcSourceEventHandler *h)
{
    std::vector<ISrsRtcSourceEventHandler *>::iterator it;
    it = std::find(event_handlers_.begin(), event_handlers_.end(), h);
    if (it != event_handlers_.end()) {
        it = event_handlers_.erase(it);
    }
}

ISrsRtcPublishStream *SrsRtcSource::publish_stream()
{
    return publish_stream_;
}

void SrsRtcSource::set_publish_stream(ISrsRtcPublishStream *v)
{
    publish_stream_ = v;
}

srs_error_t SrsRtcSource::on_rtp(SrsRtpPacket *pkt)
{
    srs_error_t err = srs_success;

    // If circuit-breaker is dying, drop packet.
    if (circuit_breaker_ && circuit_breaker_->hybrid_dying_water_level()) {
        _srs_pps_aloss2->sugar_ += (int64_t)consumers_.size();
        return err;
    }

    for (int i = 0; i < (int)consumers_.size(); i++) {
        ISrsRtcConsumer *consumer = consumers_.at(i);
        if ((err = consumer->enqueue(pkt->copy())) != srs_success) {
            return srs_error_wrap(err, "consume message");
        }
    }

    if (rtc_bridge_ && (err = rtc_bridge_->on_rtp(pkt)) != srs_success) {
        return srs_error_wrap(err, "rtp bridge consume packet");
    }

    return err;
}

bool SrsRtcSource::has_stream_desc()
{
    return stream_desc_;
}

void SrsRtcSource::set_stream_desc(SrsRtcSourceDescription *stream_desc)
{
    srs_freep(stream_desc_);

    if (stream_desc) {
        stream_desc_ = stream_desc->copy();
    }
}

std::vector<SrsRtcTrackDescription *> SrsRtcSource::get_track_desc(std::string type, std::string media_name)
{
    std::vector<SrsRtcTrackDescription *> track_descs;
    if (!stream_desc_) {
        return track_descs;
    }

    if (type == "audio") {
        if (!stream_desc_->audio_track_desc_) {
            return track_descs;
        }

        SrsAudioCodecId codec = SrsAudioCodecId(stream_desc_->audio_track_desc_->media_->codec(false));
        if (codec == srs_audio_codec_str2id(media_name)) {
            track_descs.push_back(stream_desc_->audio_track_desc_);
        }
    }

    if (type == "video") {
        std::vector<SrsRtcTrackDescription *>::iterator it = stream_desc_->video_track_descs_.begin();
        for (; it != stream_desc_->video_track_descs_.end(); ++it) {
            SrsRtcTrackDescription *track_desc = *it;

            if (media_name.empty()) {
                track_descs.push_back(track_desc);
            } else {
                SrsVideoCodecId codec = SrsVideoCodecId(track_desc->media_->codec(true));
                if (codec == srs_video_codec_str2id(media_name)) {
                    track_descs.push_back(track_desc);
                }
            }
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
        SrsRtcTrackDescription *desc = stream_desc_->video_track_descs_.at(i);
        srs_trace("RTC: to rtmp bridge request key frame, ssrc=%u, publisher cid=%s", desc->ssrc_, publish_stream_->context_id().c_str());
        publish_stream_->request_keyframe(desc->ssrc_, publish_stream_->context_id());
    }

    return err;
}

#ifdef SRS_FFMPEG_FIT

SrsRtcRtpBuilder::SrsRtcRtpBuilder(ISrsAppFactory *factory, ISrsRtpTarget *target, SrsSharedPtr<SrsRtcSource> source)
{
    rtp_target_ = target;
    source_ = source;

    req_ = NULL;
    format_ = new SrsRtmpFormat();
    codec_ = factory->create_audio_transcoder();
    latest_codec_ = SrsAudioCodecIdForbidden;
    keep_bframe_ = false;
    keep_avc_nalu_sei_ = true;
    merge_nalus_ = false;
    meta_ = new SrsMetaCache();
    audio_sequence_ = 0;
    video_builder_ = new SrsRtpVideoBuilder();

    // Initialize with default values - will be set during lazy initialization
    audio_ssrc_ = 0;
    audio_payload_type_ = 0;

    // Lazy initialization flags
    audio_initialized_ = false;
    video_initialized_ = false;

    app_factory_ = factory;
}

SrsRtcRtpBuilder::~SrsRtcRtpBuilder()
{
    srs_freep(format_);
    srs_freep(codec_);
    srs_freep(meta_);
    srs_freep(video_builder_);

    app_factory_ = NULL;
}

srs_error_t SrsRtcRtpBuilder::initialize_audio_track(SrsAudioCodecId codec)
{
    srs_error_t err = srs_success;

    // Get the audio track description for the specified codec, as we will always
    // transcode to opus for WebRTC.
    std::string codec_name = "opus";
    std::vector<SrsRtcTrackDescription *> descs = source_->get_track_desc("audio", "opus");

    if (!descs.empty()) {
        // Note we must use the PT of source, see https://github.com/ossrs/srs/pull/3079
        SrsRtcTrackDescription *track = descs.at(0);
        audio_ssrc_ = track->ssrc_;
        audio_payload_type_ = track->media_->pt_;
    } else {
        audio_payload_type_ = kAudioPayloadType;
    }

    srs_trace("RTMP2RTC: Initialize audio track for %s with codec=%s, ssrc=%u, pt=%d",
              srs_audio_codec_id2str(codec).c_str(), codec_name.c_str(), audio_ssrc_, audio_payload_type_);

    return err;
}

srs_error_t SrsRtcRtpBuilder::initialize_video_track(SrsVideoCodecId codec)
{
    srs_error_t err = srs_success;

    // Get the video track description for the detected codec
    std::string codec_name = srs_video_codec_id2str(codec);
    std::vector<SrsRtcTrackDescription *> descs = source_->get_track_desc("video", codec_name);

    uint32_t video_ssrc = 0;
    uint8_t video_payload_type = 0;
    if (!descs.empty()) {
        // Note we must use the PT of source, see https://github.com/ossrs/srs/pull/3079
        SrsRtcTrackDescription *track = descs.at(0);
        video_ssrc = track->ssrc_;
        video_payload_type = track->media_->pt_;
    } else {
        video_payload_type = kVideoPayloadType;
    }

    SrsFormat *format = meta_->vsh_format();
    if ((err = video_builder_->initialize(format, video_ssrc, video_payload_type)) != srs_success) {
        return srs_error_wrap(err, "initialize video builder");
    }

    srs_trace("RTMP2RTC: Initialize video track with codec=%s, ssrc=%u, pt=%d",
              codec_name.c_str(), video_ssrc, video_payload_type);

    return err;
}

srs_error_t SrsRtcRtpBuilder::initialize(ISrsRequest *r)
{
    srs_error_t err = srs_success;

    req_ = r;

    if ((err = format_->initialize()) != srs_success) {
        return srs_error_wrap(err, "format initialize");
    }

    // Setup the SPS/PPS parsing strategy.
    format_->try_annexb_first_ = _srs_config->try_annexb_first(r->vhost_);

    keep_bframe_ = _srs_config->get_rtc_keep_bframe(req_->vhost_);
    keep_avc_nalu_sei_ = _srs_config->get_rtc_keep_avc_nalu_sei(req_->vhost_);
    merge_nalus_ = _srs_config->get_rtc_server_merge_nalus();
    srs_trace("RTC bridge from RTMP, keep_bframe=%d, keep_avc_nalu_sei=%d, merge_nalus=%d",
              keep_bframe_, keep_avc_nalu_sei_, merge_nalus_);

    return err;
}

srs_error_t SrsRtcRtpBuilder::on_publish()
{
    srs_error_t err = srs_success;

    // Reset the metadata cache, to make VLC happy when disable/enable stream.
    // @see https://github.com/ossrs/srs/issues/1630#issuecomment-597979448
    meta_->clear();

    return err;
}

void SrsRtcRtpBuilder::on_unpublish()
{
    // Reset the metadata cache, to make VLC happy when disable/enable stream.
    // @see https://github.com/ossrs/srs/issues/1630#issuecomment-597979448
    meta_->update_previous_vsh();
    meta_->update_previous_ash();
}

srs_error_t SrsRtcRtpBuilder::on_frame(SrsMediaPacket *frame)
{
    if (frame->is_audio()) {
        return on_audio(frame);
    } else if (frame->is_video()) {
        return on_video(frame);
    }
    return srs_success;
}

srs_error_t SrsRtcRtpBuilder::on_audio(SrsMediaPacket *msg)
{
    srs_error_t err = srs_success;

    // TODO: FIXME: Support parsing OPUS for RTC.
    if ((err = format_->on_audio(msg)) != srs_success) {
        return srs_error_wrap(err, "format consume audio");
    }

    // Try to init codec when startup or codec changed.
    if (format_->acodec_ && (err = init_codec(format_->acodec_->id_)) != srs_success) {
        return srs_error_wrap(err, "init codec");
    }

    // Ignore if no format->acodec, it means the codec is not parsed, or unknown codec.
    // @issue https://github.com/ossrs/srs/issues/1506#issuecomment-562079474
    if (!format_->acodec_) {
        return err;
    }

    // support audio codec: aac/mp3
    SrsAudioCodecId acodec = format_->acodec_->id_;
    if (acodec != SrsAudioCodecIdAAC && acodec != SrsAudioCodecIdMP3) {
        return err;
    }

    // Initialize audio track on first packet with actual codec
    if (!audio_initialized_) {
        if ((err = initialize_audio_track(acodec)) != srs_success) {
            return srs_error_wrap(err, "init audio track");
        }
        audio_initialized_ = true;
    }

    // ignore sequence header
    srs_assert(format_->audio_);

    if (format_->acodec_->id_ == SrsAudioCodecIdMP3) {
        return transcode(format_->audio_);
    }

    // When drop aac audio packet, never transcode.
    if (acodec != SrsAudioCodecIdAAC) {
        return err;
    }

    char *adts_audio = NULL;
    int nn_adts_audio = 0;
    // TODO: FIXME: Reserve 7 bytes header when create shared message.
    if ((err = aac_raw_append_adts_header(msg, format_, &adts_audio, &nn_adts_audio)) != srs_success) {
        return srs_error_wrap(err, "aac append header");
    }

    if (!adts_audio) {
        return err;
    }

    SrsParsedAudioPacket aac;
    aac.dts_ = format_->audio_->dts_;
    aac.cts_ = format_->audio_->cts_;
    if ((err = aac.add_sample(adts_audio, nn_adts_audio)) == srs_success) {
        // If OK, transcode the AAC to Opus and consume it.
        err = transcode(&aac);
    }

    srs_freepa(adts_audio);

    return err;
}

srs_error_t SrsRtcRtpBuilder::init_codec(SrsAudioCodecId codec)
{
    srs_error_t err = srs_success;

    // Ignore if not changed.
    if (latest_codec_ == codec)
        return err;

    // Create a new codec.
    srs_freep(codec_);
    codec_ = app_factory_->create_audio_transcoder();

    // Initialize the codec according to the codec in stream.
    int bitrate = _srs_config->get_rtc_opus_bitrate(req_->vhost_); // The output bitrate in bps.
    if ((err = codec_->initialize(codec, SrsAudioCodecIdOpus, kAudioChannel, kAudioSamplerate, bitrate)) != srs_success) {
        return srs_error_wrap(err, "init codec=%d", codec);
    }

    // Update the latest codec in stream.
    if (latest_codec_ == SrsAudioCodecIdForbidden) {
        srs_trace("RTMP2RTC: Init audio transcoder codec to %d(%s)", codec, srs_audio_codec_id2str(codec).c_str());
    } else {
        srs_trace("RTMP2RTC: Switch audio transcoder codec %d(%s) to %d(%s)", latest_codec_, srs_audio_codec_id2str(latest_codec_).c_str(),
                  codec, srs_audio_codec_id2str(codec).c_str());
    }
    latest_codec_ = codec;

    return err;
}

srs_error_t SrsRtcRtpBuilder::transcode(SrsParsedAudioPacket *audio)
{
    srs_error_t err = srs_success;

    std::vector<SrsParsedAudioPacket *> out_audios;
    if ((err = codec_->transcode(audio, out_audios)) != srs_success) {
        return srs_error_wrap(err, "recode error");
    }

    // Save OPUS packets in shared message.
    if (out_audios.empty()) {
        return err;
    }

    for (std::vector<SrsParsedAudioPacket *>::iterator it = out_audios.begin(); it != out_audios.end(); ++it) {
        SrsParsedAudioPacket *out_audio = *it;
        SrsUniquePtr<SrsRtpPacket> pkt(new SrsRtpPacket());

        if ((err = package_opus(out_audio, pkt.get())) != srs_success) {
            err = srs_error_wrap(err, "package opus");
            break;
        }

        if ((err = rtp_target_->on_rtp(pkt.get())) != srs_success) {
            err = srs_error_wrap(err, "consume opus");
            break;
        }
    }

    codec_->free_frames(out_audios);

    return err;
}

srs_error_t SrsRtcRtpBuilder::package_opus(SrsParsedAudioPacket *audio, SrsRtpPacket *pkt)
{
    srs_error_t err = srs_success;

    pkt->header_.set_payload_type(audio_payload_type_);
    pkt->header_.set_ssrc(audio_ssrc_);
    pkt->frame_type_ = SrsFrameTypeAudio;
    pkt->header_.set_marker(true);
    pkt->header_.set_sequence(audio_sequence_++);
    pkt->header_.set_timestamp(audio->dts_ * 48);

    SrsRtpRawPayload *raw = new SrsRtpRawPayload();
    pkt->set_payload(raw, SrsRtpPacketPayloadTypeRaw);

    srs_assert(audio->nb_samples_ == 1);
    raw->payload_ = pkt->wrap(audio->samples_[0].bytes_, audio->samples_[0].size_);
    raw->nn_payload_ = audio->samples_[0].size_;

    return err;
}

static void free_packets(vector<SrsRtpPacket *> *pkts)
{
    if (!pkts)
        return;

    for (size_t i = 0; i < pkts->size(); i++) {
        srs_freep((*pkts)[i]);
    }
    pkts->clear();
}

srs_error_t SrsRtcRtpBuilder::on_video(SrsMediaPacket *msg)
{
    srs_error_t err = srs_success;

    // cache the sequence header if h264
    bool is_sequence_header = SrsFlvVideo::sh(msg->payload(), msg->size());
    if (is_sequence_header && (err = meta_->update_vsh(msg)) != srs_success) {
        return srs_error_wrap(err, "meta update video");
    }

    if ((err = format_->on_video(msg)) != srs_success) {
        return srs_error_wrap(err, "format consume video");
    }

    // Ignore if no format->vcodec, it means the codec is not parsed, or unsupport/unknown codec
    // such as H.263 codec
    if (!format_->vcodec_) {
        return err;
    }

    // support video codec: h264/h265
    SrsVideoCodecId vcodec = format_->vcodec_->id_;
    if (vcodec != SrsVideoCodecIdAVC && vcodec != SrsVideoCodecIdHEVC) {
        return err;
    }

    // Initialize video track on first packet with actual codec
    if (!video_initialized_) {
        if ((err = initialize_video_track(vcodec)) != srs_success) {
            return srs_error_wrap(err, "init video track");
        }
        video_initialized_ = true;
    }

    bool has_idr = false;
    vector<SrsNaluSample *> samples;
    if ((err = filter(msg, format_, has_idr, samples)) != srs_success) {
        return srs_error_wrap(err, "filter video");
    }
    int nn_samples = (int)samples.size();

    // Well, for each IDR, we append a SPS/PPS before it, which is packaged in STAP-A.
    if (has_idr) {
        SrsUniquePtr<SrsRtpPacket> pkt(new SrsRtpPacket());

        if ((err = package_stap_a(msg, pkt.get())) != srs_success) {
            return srs_error_wrap(err, "package stap-a");
        }

        if ((err = rtp_target_->on_rtp(pkt.get())) != srs_success) {
            return srs_error_wrap(err, "consume sps/pps");
        }
    }

    // If merge Nalus, we pcakges all NALUs(samples) as one NALU, in a RTP or FUA packet.
    vector<SrsRtpPacket *> pkts;
    // TODO: FIXME: Should rename to pkts_disposer.
    SrsUniquePtr<vector<SrsRtpPacket *> > pkts_ptr(&pkts, free_packets);

    if (merge_nalus_ && nn_samples > 1) {
        if ((err = package_nalus(msg, samples, pkts)) != srs_success) {
            return srs_error_wrap(err, "package nalus as one");
        }
    } else {
        // By default, we package each NALU(sample) to a RTP or FUA packet.
        for (int i = 0; i < nn_samples; i++) {
            SrsNaluSample *sample = samples[i];

            if (sample->size_ <= kRtpMaxPayloadSize) {
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
        pkts.back()->header_.set_marker(true);
    }

    return consume_packets(pkts);
}

srs_error_t SrsRtcRtpBuilder::filter(SrsMediaPacket *msg, SrsFormat *format, bool &has_idr, vector<SrsNaluSample *> &samples)
{
    srs_error_t err = srs_success;

    // If IDR, we will insert SPS/PPS before IDR frame.
    if (format->video_ && format->video_->has_idr_) {
        has_idr = true;
    }

    // Update samples to shared frame.
    for (int i = 0; i < format->video_->nb_samples_; ++i) {
        SrsNaluSample *sample = &format->video_->samples_[i];

        if (!keep_avc_nalu_sei_ && format_->vcodec_->id_ == SrsVideoCodecIdAVC) {
            SrsAvcNaluType avc_nalu_type;

            if ((err = SrsParsedVideoPacket::parse_avc_nalu_type(sample, avc_nalu_type)) != srs_success) {
                return srs_error_wrap(err, "parse avc nalu_type");
            }
            if (avc_nalu_type == SrsAvcNaluTypeSEI) {
                // srs_warn("skip avc nalu type SEI, size=%d", sample->size);
                continue;
            }
        }

        // Because RTC does not support B-frame, so we will drop them.
        // TODO: Drop B-frame in better way, which not cause picture corruption.
        if (!keep_bframe_) {
            bool is_b_frame = false;
            if (format->vcodec_->id_ == SrsVideoCodecIdAVC) {
                if ((err = SrsParsedVideoPacket::parse_avc_bframe(sample, is_b_frame)) != srs_success) {
                    return srs_error_wrap(err, "parse bframe");
                }
            } else if (format->vcodec_->id_ == SrsVideoCodecIdHEVC) {
                if ((err = SrsParsedVideoPacket::parse_hevc_bframe(sample, format, is_b_frame)) != srs_success) {
                    return srs_error_wrap(err, "parse bframe");
                }
            }
            if (is_b_frame) {
                continue;
            }
        }

        samples.push_back(sample);
    }

    return err;
}

srs_error_t SrsRtcRtpBuilder::package_stap_a(SrsMediaPacket *msg, SrsRtpPacket *pkt)
{
    srs_error_t err = srs_success;

    SrsFormat *format = meta_->vsh_format();
    if (!format || !format->vcodec_) {
        return err;
    }

    return video_builder_->package_stap_a(msg, pkt);
}

srs_error_t SrsRtcRtpBuilder::package_nalus(SrsMediaPacket *msg, const vector<SrsNaluSample *> &samples, vector<SrsRtpPacket *> &pkts)
{
    srs_error_t err = srs_success;

    SrsFormat *format = meta_->vsh_format();
    if (!format || !format->vcodec_) {
        return err;
    }

    return video_builder_->package_nalus(msg, samples, pkts);
}

// Single NAL Unit Packet @see https://tools.ietf.org/html/rfc6184#section-5.6
srs_error_t SrsRtcRtpBuilder::package_single_nalu(SrsMediaPacket *msg, SrsNaluSample *sample, vector<SrsRtpPacket *> &pkts)
{
    return video_builder_->package_single_nalu(msg, sample, pkts);
}

srs_error_t SrsRtcRtpBuilder::package_fu_a(SrsMediaPacket *msg, SrsNaluSample *sample, int fu_payload_size, vector<SrsRtpPacket *> &pkts)
{
    srs_error_t err = srs_success;

    SrsFormat *format = meta_->vsh_format();
    if (!format || !format->vcodec_) {
        return err;
    }

    return video_builder_->package_fu_a(msg, sample, fu_payload_size, pkts);
}

srs_error_t SrsRtcRtpBuilder::consume_packets(vector<SrsRtpPacket *> &pkts)
{
    srs_error_t err = srs_success;

    // TODO: FIXME: Consume a range of packets.
    for (int i = 0; i < (int)pkts.size(); i++) {
        SrsRtpPacket *pkt = pkts[i];
        if ((err = rtp_target_->on_rtp(pkt)) != srs_success) {
            err = srs_error_wrap(err, "consume sps/pps");
            break;
        }
    }

    return err;
}

ISrsRtcFrameBuilderVideoPacketCache::ISrsRtcFrameBuilderVideoPacketCache()
{
}

ISrsRtcFrameBuilderVideoPacketCache::~ISrsRtcFrameBuilderVideoPacketCache()
{
}

SrsRtcFrameBuilderVideoPacketCache::SrsRtcFrameBuilderVideoPacketCache()
{
    memset(cache_pkts_, 0, sizeof(cache_pkts_));
}

SrsRtcFrameBuilderVideoPacketCache::~SrsRtcFrameBuilderVideoPacketCache()
{
    clear_all();
}

SrsRtpPacket *SrsRtcFrameBuilderVideoPacketCache::get_packet(uint16_t sequence_number)
{
    uint16_t index = cache_index(sequence_number);
    const RtcPacketCache &cache = cache_pkts_[index];

    // Since cache uses modulo indexing, different sequence numbers can map to the
    // same cache slot, so we must verify the stored sn matches the requested one.
    if (!cache.in_use_ || cache.sn_ != sequence_number) {
        return NULL;
    }

    return cache.pkt_;
}

void SrsRtcFrameBuilderVideoPacketCache::store_packet(SrsRtpPacket *pkt)
{
    if (!pkt) {
        return; // Ignore null packets
    }

    uint16_t index = cache_index(pkt->header_.get_sequence());
    RtcPacketCache &cache = cache_pkts_[index];

    cache.in_use_ = true;
    srs_freep(cache.pkt_);
    cache.pkt_ = pkt;
    cache.sn_ = pkt->header_.get_sequence();
    cache.ts_ = pkt->get_avsync_time();
    cache.rtp_ts_ = pkt->header_.get_timestamp();
}

bool SrsRtcFrameBuilderVideoPacketCache::is_slot_in_use(uint16_t sequence_number)
{
    uint16_t index = cache_index(sequence_number);
    const RtcPacketCache &cache = cache_pkts_[index];
    return cache.in_use_;
}

uint32_t SrsRtcFrameBuilderVideoPacketCache::get_rtp_timestamp(uint16_t sequence_number)
{
    uint16_t index = cache_index(sequence_number);
    const RtcPacketCache &cache = cache_pkts_[index];
    return cache.rtp_ts_;
}

void SrsRtcFrameBuilderVideoPacketCache::clear_all()
{
    for (size_t i = 0; i < cache_size_; i++) {
        RtcPacketCache &cache = cache_pkts_[i];
        if (cache.in_use_) {
            srs_freep(cache.pkt_);
            cache.sn_ = 0;
            cache.ts_ = 0;
            cache.rtp_ts_ = 0;
            cache.in_use_ = false;
        }
    }
}

SrsRtpPacket *SrsRtcFrameBuilderVideoPacketCache::take_packet(uint16_t sequence_number)
{
    uint16_t index = cache_index(sequence_number);
    RtcPacketCache &cache = cache_pkts_[index];

    // Since cache uses modulo indexing, different sequence numbers can map to the
    // same cache slot, so we must verify the stored sn matches the requested one.
    if (!cache.in_use_ || cache.sn_ != sequence_number) {
        return NULL;
    }

    SrsRtpPacket *pkt = cache.pkt_;

    // Clear the slot after taking the packet
    cache.in_use_ = false;
    // Note: No memory leak here - the packet ownership is transferred to caller
    cache.pkt_ = NULL;
    cache.ts_ = 0;
    cache.rtp_ts_ = 0;
    cache.sn_ = 0;

    return pkt;
}

// TODO: Should improve the frame builder by jitter and NACK, consider if only RTC2RTMP without
// any RTC player, then if one RTP packet is lost, the frame builder will not request the lost
// packet. This makes the whole GOP not available.
int32_t SrsRtcFrameBuilderVideoPacketCache::find_next_lost_sn(uint16_t current_sn, uint16_t header_sn, uint16_t &end_sn)
{
    uint32_t last_rtp_ts = get_rtp_timestamp(header_sn);
    for (int i = 0; i < cache_size_; ++i) {
        uint16_t lost_sn = current_sn + i;

        if (!is_slot_in_use(lost_sn)) {
            return lost_sn;
        }

        // check time first, avoid two small frame mixed case decode fail
        if (last_rtp_ts != get_rtp_timestamp(lost_sn)) {
            end_sn = lost_sn - 1;
            return -1;
        }

        SrsRtpPacket *pkt = get_packet(lost_sn);
        if (pkt && pkt->header_.get_marker()) {
            end_sn = lost_sn;
            return -1;
        }
    }

    srs_warn("cache overflow. the packet count of video frame is more than %u", cache_size_);
    return -2;
}

bool SrsRtcFrameBuilderVideoPacketCache::check_frame_complete(const uint16_t start, const uint16_t end)
{
    int16_t cnt = srs_rtp_seq_distance(start, end) + 1;

    // If the sequence range is invalid (end before start), return false
    if (cnt <= 0) {
        srs_warn("invalid sequence range start=%u, end=%u, cnt=%d", start, end, cnt);
        return false;
    }

    uint16_t nn_fu_start = 0;
    uint16_t nn_fu_end = 0;
    for (uint16_t i = 0; i < (uint16_t)cnt; ++i) {
        uint16_t sequence_number = start + i;
        SrsRtpPacket *pkt = get_packet(sequence_number);

        // fix crash when pkt->payload() if pkt is nullptr;
        if (!pkt)
            continue;

        SrsRtpFUAPayload2 *fua_payload = dynamic_cast<SrsRtpFUAPayload2 *>(pkt->payload());
        if (!fua_payload)
            continue;

        if (fua_payload->start_) {
            ++nn_fu_start;
        }

        if (fua_payload->end_) {
            ++nn_fu_end;
        }
    }

    return nn_fu_start == nn_fu_end;
}

ISrsRtcFrameBuilderVideoFrameDetector::ISrsRtcFrameBuilderVideoFrameDetector()
{
}

ISrsRtcFrameBuilderVideoFrameDetector::~ISrsRtcFrameBuilderVideoFrameDetector()
{
}

SrsRtcFrameBuilderVideoFrameDetector::SrsRtcFrameBuilderVideoFrameDetector(ISrsRtcFrameBuilderVideoPacketCache *cache)
{
    video_cache_ = cache;
    header_sn_ = 0;
    lost_sn_ = 0;
    rtp_key_frame_ts_ = -1;
}

SrsRtcFrameBuilderVideoFrameDetector::~SrsRtcFrameBuilderVideoFrameDetector()
{
}

void SrsRtcFrameBuilderVideoFrameDetector::on_keyframe_start(SrsRtpPacket *pkt)
{
    if (-1 == rtp_key_frame_ts_) {
        rtp_key_frame_ts_ = pkt->header_.get_timestamp();
        header_sn_ = pkt->header_.get_sequence();
        lost_sn_ = header_sn_ + 1;
        // Received key frame and clean cache of old p frame pkts.
        // TODO: Should use jitter buffer to avoid clear previous P frame in case of reordering.
        video_cache_->clear_all();
        srs_trace("RTC2RTMP: keyframe set ts=%u, header=%hu, lost=%hu", (uint32_t)rtp_key_frame_ts_, header_sn_, lost_sn_);
    } else if (rtp_key_frame_ts_ != pkt->header_.get_timestamp()) {
        // new key frame, clean cache
        int64_t old_ts = rtp_key_frame_ts_;
        uint16_t old_header_sn = header_sn_;
        uint16_t old_lost_sn = lost_sn_;
        rtp_key_frame_ts_ = pkt->header_.get_timestamp();
        header_sn_ = pkt->header_.get_sequence();
        lost_sn_ = header_sn_ + 1;
        // Received key frame and clean cache of old p frame pkts.
        // TODO: Should use jitter buffer to avoid clear previous P frame in case of reordering.
        video_cache_->clear_all();
        srs_warn("RTC2RTMP: keyframe drop old ts=%u, header=%hu, lost=%hu, set new ts=%u, header=%hu, lost=%hu",
                 (uint32_t)old_ts, old_header_sn, old_lost_sn, (uint32_t)rtp_key_frame_ts_, header_sn_, lost_sn_);
    }
}

srs_error_t SrsRtcFrameBuilderVideoFrameDetector::detect_frame(uint16_t received, uint16_t &frame_start, uint16_t &frame_end, bool &frame_ready)
{
    srs_error_t err = srs_success;
    frame_ready = false;

    int32_t sn;
    uint16_t tail_sn = 0;
    if (srs_rtp_seq_distance(header_sn_, received) < 0) {
        // When receive previous pkt in the same frame, update header sn;
        header_sn_ = received;
        sn = video_cache_->find_next_lost_sn(received, header_sn_, tail_sn);
    } else if (lost_sn_ == received) {
        sn = video_cache_->find_next_lost_sn(received, header_sn_, tail_sn);
    } else {
        sn = lost_sn_;
    }

    if (-1 == sn) {
        if (video_cache_->check_frame_complete(header_sn_, tail_sn)) {
            frame_start = header_sn_;
            frame_end = tail_sn;
            frame_ready = true;
        }
    } else if (-2 == sn) {
        return srs_error_new(ERROR_RTC_RTP_MUXER, "video cache is overflow");
    } else {
        lost_sn_ = (uint16_t)sn;
    }

    return err;
}

srs_error_t SrsRtcFrameBuilderVideoFrameDetector::detect_next_frame(uint16_t next_head, uint16_t &frame_start, uint16_t &frame_end, bool &frame_ready)
{
    srs_error_t err = srs_success;
    frame_ready = false;

    header_sn_ = next_head;
    uint16_t tail_sn = 0;
    int32_t sn = video_cache_->find_next_lost_sn(header_sn_, header_sn_, tail_sn);

    if (-1 == sn) {
        if (video_cache_->check_frame_complete(header_sn_, tail_sn)) {
            frame_start = header_sn_;
            frame_end = tail_sn;
            frame_ready = true;
        }
    } else if (-2 == sn) {
        return srs_error_new(ERROR_RTC_RTP_MUXER, "video cache is overflow");
    } else {
        lost_sn_ = (uint16_t)sn;
    }

    return err;
}

void SrsRtcFrameBuilderVideoFrameDetector::on_keyframe_detached()
{
    rtp_key_frame_ts_ = -1;
}

bool SrsRtcFrameBuilderVideoFrameDetector::is_lost_sn(uint16_t received)
{
    return lost_sn_ == received;
}

ISrsRtcFrameBuilderAudioPacketCache::ISrsRtcFrameBuilderAudioPacketCache()
{
}

ISrsRtcFrameBuilderAudioPacketCache::~ISrsRtcFrameBuilderAudioPacketCache()
{
}

SrsRtcFrameBuilderAudioPacketCache::SrsRtcFrameBuilderAudioPacketCache()
{
    last_audio_seq_num_ = 0;
    last_audio_process_time_ = 0;
    initialized_ = false;
    timeout_ = MAX_AUDIO_WAIT_MS * SRS_UTIME_MILLISECONDS; // Default timeout in microseconds
}

SrsRtcFrameBuilderAudioPacketCache::~SrsRtcFrameBuilderAudioPacketCache()
{
    clear_all();
}

void SrsRtcFrameBuilderAudioPacketCache::set_timeout(srs_utime_t timeout)
{
    timeout_ = timeout;
}

srs_error_t SrsRtcFrameBuilderAudioPacketCache::process_packet(SrsRtpPacket *src, std::vector<SrsRtpPacket *> &ready_packets)
{
    srs_error_t err = srs_success;

    uint16_t seq = src->header_.get_sequence();
    srs_utime_t now = srs_time_now_realtime();

    if (!initialized_) {
        last_audio_seq_num_ = seq - 1;
        last_audio_process_time_ = now;
        initialized_ = true;
    }

    // Check if packet is too old (already processed)
    if (srs_rtp_seq_distance(last_audio_seq_num_, seq) < 0) {
        srs_warn("Discard late audio packet, seq=%u, last_seq=%u", seq, last_audio_seq_num_);
        return err;
    }

    // Store packet in jitter buffer
    if (true) {
        std::map<uint16_t, SrsRtpPacket *>::iterator it = audio_buffer_.find(seq);
        if (it != audio_buffer_.end()) {
            SrsRtpPacket *pkt = it->second;
            srs_freep(pkt);
        }
        audio_buffer_[seq] = src->copy();
    }

    // Try to process packets in the sliding window
    bool force_process = audio_buffer_.size() >= AUDIO_JITTER_BUFFER_SIZE ||
                         (now - last_audio_process_time_) > timeout_;
    uint16_t window_end = last_audio_seq_num_ + SLIDING_WINDOW_SIZE;

    // TODO: Should improve the audio cache by NACK, consider if only RTC2RTMP without
    // any RTC player, then if one RTP packet is lost, the audio cache will not request the lost
    // packet. This means some audio packets are lost.
    while (!audio_buffer_.empty()) {
        std::map<uint16_t, SrsRtpPacket *>::iterator it = audio_buffer_.begin();
        uint16_t next_seq = it->first;

        // Check if the packet is within our sliding window
        if (!force_process) {
            // If packet is before window start (shouldn't happen normally)
            if (srs_rtp_seq_distance(last_audio_seq_num_, next_seq) < 0) {
                // Process it anyway as it's already late
                srs_warn("Late audio packet, seq=%u, expected>=%u", next_seq, last_audio_seq_num_);
            } else if (srs_rtp_seq_distance(next_seq, window_end) < 0) {
                // If packet is beyond window end, stop processing
                srs_warn("Audio packet beyond window end, seq=%u, window_end=%u", next_seq, window_end);
                break;
            } else if (srs_rtp_seq_distance(last_audio_seq_num_, next_seq) > 1) {
                // If there's a gap and we haven't exceeded wait time, wait for missing packets
                if ((now - last_audio_process_time_) <= timeout_) {
                    break;
                }
                srs_warn("Audio packet loss, expected=%u, got=%u", last_audio_seq_num_ + 1, next_seq);
            }
        }

        // Take the packet from buffer
        SrsRtpPacket *pkt = it->second;
        audio_buffer_.erase(it);

        // Update last sequence number
        last_audio_seq_num_ = next_seq;
        last_audio_process_time_ = now;

        // Add to ready packets for processing
        ready_packets.push_back(pkt);

        // Update window end for next iteration
        window_end = last_audio_seq_num_ + SLIDING_WINDOW_SIZE;
    }

    // If buffer is getting too full, force process oldest packets
    if (audio_buffer_.size() >= AUDIO_JITTER_BUFFER_SIZE * 0.8) {
        srs_warn("Audio jitter buffer nearly full, size=%zu", audio_buffer_.size());
    }

    return err;
}

void SrsRtcFrameBuilderAudioPacketCache::clear_all()
{
    std::map<uint16_t, SrsRtpPacket *>::iterator it;
    for (it = audio_buffer_.begin(); it != audio_buffer_.end(); ++it) {
        SrsRtpPacket *pkt = it->second;
        srs_freep(pkt);
    }

    audio_buffer_.clear();
}

SrsRtcFrameBuilder::SrsRtcFrameBuilder(ISrsAppFactory *factory, ISrsFrameTarget *target)
{
    frame_target_ = target;
    is_first_audio_ = true;
    audio_transcoder_ = NULL;
    video_codec_ = SrsVideoCodecIdAVC;
    audio_cache_ = factory->create_rtc_frame_builder_audio_packet_cache();
    video_cache_ = new SrsRtcFrameBuilderVideoPacketCache();
    frame_detector_ = new SrsRtcFrameBuilderVideoFrameDetector(video_cache_);
    sync_state_ = -1;
    obs_whip_vps_ = obs_whip_sps_ = obs_whip_pps_ = NULL;

    app_factory_ = factory;
}

SrsRtcFrameBuilder::~SrsRtcFrameBuilder()
{
    srs_freep(audio_transcoder_);
    srs_freep(audio_cache_);
    srs_freep(video_cache_);
    srs_freep(frame_detector_);
    srs_freep(obs_whip_vps_);
    srs_freep(obs_whip_sps_);
    srs_freep(obs_whip_pps_);

    app_factory_ = NULL;
}

srs_error_t SrsRtcFrameBuilder::initialize(ISrsRequest *r, SrsAudioCodecId audio_codec, SrsVideoCodecId video_codec)
{
    srs_error_t err = srs_success;

    srs_freep(audio_transcoder_);
    audio_transcoder_ = app_factory_->create_audio_transcoder();

    SrsAudioCodecId to = SrsAudioCodecIdAAC;                   // The output audio codec.
    int channels = 2;                                          // The output audio channels.
    int sample_rate = 48000;                                   // The output audio sample rate in HZ.
    int bitrate = _srs_config->get_rtc_aac_bitrate(r->vhost_); // The output audio bitrate in bps.

    // TODO: FIXME:
    // In the future, when we support enhanced-RTMP with Opus format,
    // this transcoding will no longer be necessary.
    if ((err = audio_transcoder_->initialize(audio_codec, to, channels, sample_rate, bitrate)) != srs_success) {
        return srs_error_wrap(err, "bridge initialize");
    }

    video_codec_ = video_codec;

    return err;
}

srs_error_t SrsRtcFrameBuilder::on_publish()
{
    is_first_audio_ = true;

    return srs_success;
}

void SrsRtcFrameBuilder::on_unpublish()
{
    audio_cache_->clear_all();
}

srs_error_t SrsRtcFrameBuilder::on_rtp(SrsRtpPacket *pkt)
{
    srs_error_t err = srs_success;

    if (!pkt->payload()) {
        return err;
    }

    // Check if avsync_time is valid (> 0).
    // NOTE: This check should NEVER fail unless SDP has no sample rate, in which case packets are discarded
    // to avoid timestamp problems in live source.
    const SrsRtpHeader &h = pkt->header_;
    if (pkt->get_avsync_time() <= 0) {
        if (sync_state_ < 0) {
            srs_trace("RTC: Discard no-sync %s, ssrc=%u, seq=%u, ts=%u, state=%d", pkt->is_audio() ? "Audio" : "Video",
                      h.get_ssrc(), h.get_sequence(), h.get_timestamp(), sync_state_);
            sync_state_ = 0;
        }
        return err;
    } else if (sync_state_ < 1) {
        srs_trace("RTC: Accept sync %s, ssrc=%u, seq=%u, ts=%u, state=%d", pkt->is_audio() ? "Audio" : "Video",
                  h.get_ssrc(), h.get_sequence(), h.get_timestamp(), sync_state_);
        sync_state_ = 2;
    }

    if (pkt->is_audio()) {
        err = packet_audio(pkt);
    } else {
        err = packet_video(pkt);
    }

    return err;
}

srs_error_t SrsRtcFrameBuilder::packet_audio(SrsRtpPacket *src)
{
    srs_error_t err = srs_success;

    std::vector<SrsRtpPacket *> ready_packets;
    SrsUniquePtr<vector<SrsRtpPacket *> > pkts_disposer(&ready_packets, free_packets);

    // Use audio cache to process packet through jitter buffer
    if ((err = audio_cache_->process_packet(src, ready_packets)) != srs_success) {
        return srs_error_wrap(err, "audio cache process");
    }

    // Process all ready packets in order
    for (size_t i = 0; i < ready_packets.size(); ++i) {
        SrsRtpPacket *pkt = ready_packets[i];

        if ((err = transcode_audio(pkt)) != srs_success) {
            return srs_error_wrap(err, "transcode audio");
        }
    }

    return err;
}

srs_error_t SrsRtcFrameBuilder::transcode_audio(SrsRtpPacket *pkt)
{
    srs_error_t err = srs_success;

    // Generate a sequence header for the first audio packet
    uint32_t ts = pkt->get_avsync_time();
    if (is_first_audio_) {
        int header_len = 0;
        uint8_t *header = NULL;
        audio_transcoder_->aac_codec_header(&header, &header_len);
        if (header_len <= 0) {
            return srs_error_new(ERROR_RTC_RTP_MUXER, "no aac header");
        }

        SrsRtmpCommonMessage out_rtmp;
        packet_aac(&out_rtmp, (char *)header, header_len, ts, is_first_audio_);

        SrsMediaPacket msg;
        out_rtmp.to_msg(&msg);

        if ((err = frame_target_->on_frame(&msg)) != srs_success) {
            return srs_error_wrap(err, "source on audio");
        }

        is_first_audio_ = false;
    }

    // TODO: FIXME: Should use SrsUniquePtr to dispose it automatically.
    std::vector<SrsParsedAudioPacket *> out_pkts;
    SrsRtpRawPayload *payload = dynamic_cast<SrsRtpRawPayload *>(pkt->payload());

    SrsParsedAudioPacket frame;
    frame.add_sample(payload->payload_, payload->nn_payload_);
    frame.dts_ = ts;
    frame.cts_ = 0;

    err = audio_transcoder_->transcode(&frame, out_pkts);
    if (err != srs_success) {
        return err;
    }

    for (std::vector<SrsParsedAudioPacket *>::iterator it = out_pkts.begin(); it != out_pkts.end(); ++it) {
        SrsRtmpCommonMessage out_rtmp;
        // TODO: FIXME: Should never directly use it, please define a variable with class name.
        out_rtmp.header_.timestamp_ = (*it)->dts_;
        packet_aac(&out_rtmp, (*it)->samples_[0].bytes_, (*it)->samples_[0].size_, ts, is_first_audio_);

        SrsMediaPacket msg;
        out_rtmp.to_msg(&msg);

        if ((err = frame_target_->on_frame(&msg)) != srs_success) {
            err = srs_error_wrap(err, "source on audio");
            break;
        }
    }
    audio_transcoder_->free_frames(out_pkts);

    return err;
}

void SrsRtcFrameBuilder::packet_aac(SrsRtmpCommonMessage *audio, char *data, int len, uint32_t pts, bool is_header)
{
    int rtmp_len = len + 2;
    audio->header_.initialize_audio(rtmp_len, pts, 1);
    audio->create_payload(rtmp_len);
    SrsBuffer stream(audio->payload(), rtmp_len);
    uint8_t aac_flag = (SrsAudioCodecIdAAC << 4) | (SrsAudioSampleRate44100 << 2) | (SrsAudioSampleBits16bit << 1) | SrsAudioChannelsStereo;
    stream.write_1bytes(aac_flag);
    if (is_header) {
        stream.write_1bytes(0);
    } else {
        stream.write_1bytes(1);
    }
    stream.write_bytes(data, len);
}

srs_error_t SrsRtcFrameBuilder::packet_video(SrsRtpPacket *pkt)
{
    srs_error_t err = srs_success;

    // For keyframe.
    if (pkt->is_keyframe(video_codec_)) {
        return packet_video_key_frame(pkt);
    }

    // For non-keyframe.
    video_cache_->store_packet(pkt->copy());

    // check whether to recovery lost packet and can construct a video frame
    uint16_t current_sn = pkt->header_.get_sequence();
    if (frame_detector_->is_lost_sn(current_sn)) {
        uint16_t start, end;
        bool got_frame;
        if ((err = frame_detector_->detect_frame(current_sn, start, end, got_frame)) != srs_success) {
            return srs_error_wrap(err, "detect frame failed");
        }
        if (got_frame) {
            if ((err = packet_video_rtmp(start, end)) != srs_success) {
                err = srs_error_wrap(err, "fail to pack video frame, start=%u, end=%u", start, end);
            }
        }
    }

    return err;
}

srs_error_t SrsRtcFrameBuilder::packet_video_key_frame(SrsRtpPacket *pkt)
{
    srs_error_t err = srs_success;

    if (video_codec_ == SrsVideoCodecIdAVC) {
        err = packet_sequence_header_avc(pkt);
    } else if (video_codec_ == SrsVideoCodecIdHEVC) {
        err = packet_sequence_header_hevc(pkt);
    } else {
        err = srs_error_new(ERROR_RTC_RTP_MUXER, "unsupported video codec %d", video_codec_);
    }

    if (err != srs_success) {
        return srs_error_wrap(err, "packet video key frame");
    }

    frame_detector_->on_keyframe_start(pkt);

    // If only contains SPS/PPS, no need to store in cache.
    SrsRtpSTAPPayload *stap_payload = dynamic_cast<SrsRtpSTAPPayload *>(pkt->payload());
    if (stap_payload && stap_payload->get_idr() == NULL) {
        return err;
    }
    SrsRtpSTAPPayloadHevc *stap_payload_hevc = dynamic_cast<SrsRtpSTAPPayloadHevc *>(pkt->payload());
    if (stap_payload_hevc && stap_payload_hevc->get_idr() == NULL) {
        return err;
    }

    // Cache the keyframe packet, which contains IDR, as long as SPS/PPS/VPS sequence header.
    video_cache_->store_packet(pkt->copy());

    uint16_t current_sn = pkt->header_.get_sequence();
    if (frame_detector_->is_lost_sn(current_sn)) {
        uint16_t start, end;
        bool got_frame;
        if ((err = frame_detector_->detect_frame(current_sn, start, end, got_frame)) != srs_success) {
            return srs_error_wrap(err, "detect frame failed");
        }
        if (got_frame) {
            if ((err = packet_video_rtmp(start, end)) != srs_success) {
                err = srs_error_wrap(err, "fail to pack video frame, start=%u, end=%u", start, end);
            }
        }
    }

    return err;
}

srs_error_t SrsRtcFrameBuilder::packet_sequence_header_avc(SrsRtpPacket *pkt)
{
    srs_error_t err = srs_success;

    // For OBS WHIP, it uses RTP Raw packet with SPS/PPS/IDR frame. Note that not all
    // raw payload is SPS/PPS.
    bool has_sps_pps_in_raw_payload = false;
    SrsRtpRawPayload *raw_payload = dynamic_cast<SrsRtpRawPayload *>(pkt->payload());
    if (raw_payload) {
        if (pkt->nalu_type_ == SrsAvcNaluTypeSPS) {
            has_sps_pps_in_raw_payload = true;
            srs_freep(obs_whip_sps_);
            obs_whip_sps_ = pkt->copy();
        } else if (pkt->nalu_type_ == SrsAvcNaluTypePPS) {
            has_sps_pps_in_raw_payload = true;
            srs_freep(obs_whip_pps_);
            obs_whip_pps_ = pkt->copy();
        }
        // Ignore if one of OBS WHIP SPS/PPS is not ready.
        if (has_sps_pps_in_raw_payload && (!obs_whip_sps_ || !obs_whip_pps_)) {
            return err;
        }
    }

    // Generally, there will be SPS+PPS+IDR in a STAP-A packet.
    SrsRtpSTAPPayload *stap_payload = dynamic_cast<SrsRtpSTAPPayload *>(pkt->payload());

    // Handle SPS/PPS in cache or STAP-A packet.
    if (stap_payload || has_sps_pps_in_raw_payload) {
        // Get the SPS/PPS from cache or STAP-A packet.
        SrsNaluSample *sps = stap_payload ? stap_payload->get_sps() : NULL;
        if (!sps && obs_whip_sps_)
            sps = dynamic_cast<SrsRtpRawPayload *>(obs_whip_sps_->payload())->sample_;
        SrsNaluSample *pps = stap_payload ? stap_payload->get_pps() : NULL;
        if (!pps && obs_whip_pps_)
            pps = dynamic_cast<SrsRtpRawPayload *>(obs_whip_pps_->payload())->sample_;
        if (!sps || !pps) {
            return srs_error_new(ERROR_RTC_RTP_MUXER, "no sps or pps in stap-a rtp. sps: %p, pps:%p", sps, pps);
        }

        // Packet SPS/PPS to RTMP keyframe.
        err = do_packet_sequence_header_avc(pkt, sps, pps);

        // Always reset the SPS/PPS cache after used it.
        srs_freep(obs_whip_sps_);
        srs_freep(obs_whip_pps_);

        if (err != srs_success) {
            return srs_error_wrap(err, "packet sps/pps");
        }
    }

    return err;
}

srs_error_t SrsRtcFrameBuilder::do_packet_sequence_header_avc(SrsRtpPacket *pkt, SrsNaluSample *sps, SrsNaluSample *pps)
{
    srs_error_t err = srs_success;

    // h264 raw to h264 packet.
    std::string sh;
    SrsUniquePtr<SrsRawH264Stream> avc(new SrsRawH264Stream());

    string sps2 = string(sps->bytes_, sps->size_);
    string pps2 = string(pps->bytes_, pps->size_);
    if ((err = avc->mux_sequence_header(sps2, pps2, sh)) != srs_success) {
        return srs_error_wrap(err, "mux sequence header");
    }

    // h264 packet to flv packet.
    char *flv = NULL;
    int nb_flv = 0;
    if ((err = avc->mux_avc2flv(sh, SrsVideoAvcFrameTypeKeyFrame,
                                SrsVideoAvcFrameTraitSequenceHeader, pkt->get_avsync_time(),
                                pkt->get_avsync_time(), &flv, &nb_flv)) != srs_success) {
        return srs_error_wrap(err, "avc to flv");
    }

    SrsMessageHeader header;
    header.initialize_video(nb_flv, pkt->get_avsync_time(), 1);
    SrsRtmpCommonMessage rtmp;
    if ((err = rtmp.create(&header, flv, nb_flv)) != srs_success) {
        return srs_error_wrap(err, "create rtmp");
    }

    SrsMediaPacket msg;
    rtmp.to_msg(&msg);

    if ((err = frame_target_->on_frame(&msg)) != srs_success) {
        return err;
    }

    return err;
}

srs_error_t SrsRtcFrameBuilder::packet_sequence_header_hevc(SrsRtpPacket *pkt)
{
    srs_error_t err = srs_success;

    // For OBS WHIP, it uses RTP Raw packet with VPS/SPS/PPS/IDR frame. Note that not all
    // raw payload is VPS/SPS/PPS.
    bool has_vps_sps_pps_in_raw_payload = false;
    SrsRtpRawPayload *raw_payload = dynamic_cast<SrsRtpRawPayload *>(pkt->payload());
    if (raw_payload) {
        if (pkt->nalu_type_ == SrsHevcNaluType_VPS) {
            has_vps_sps_pps_in_raw_payload = true;
            srs_freep(obs_whip_vps_);
            obs_whip_vps_ = pkt->copy();
        } else if (pkt->nalu_type_ == SrsHevcNaluType_SPS) {
            has_vps_sps_pps_in_raw_payload = true;
            srs_freep(obs_whip_sps_);
            obs_whip_sps_ = pkt->copy();
        } else if (pkt->nalu_type_ == SrsHevcNaluType_PPS) {
            has_vps_sps_pps_in_raw_payload = true;
            srs_freep(obs_whip_pps_);
            obs_whip_pps_ = pkt->copy();
        }
        // Ignore if one of OBS WHIP VPS/SPS/PPS is not ready.
        if (has_vps_sps_pps_in_raw_payload && (!obs_whip_vps_ || !obs_whip_sps_ || !obs_whip_pps_)) {
            return err;
        }
    }

    // Generally, there will be SPS+PPS+IDR in a STAP-A packet.
    SrsRtpSTAPPayloadHevc *stap_payload_hevc = dynamic_cast<SrsRtpSTAPPayloadHevc *>(pkt->payload());
    if (video_codec_ == SrsVideoCodecIdHEVC && (stap_payload_hevc || has_vps_sps_pps_in_raw_payload)) {
        SrsNaluSample *vps = stap_payload_hevc ? stap_payload_hevc->get_vps() : NULL;
        if (!vps && obs_whip_vps_)
            vps = dynamic_cast<SrsRtpRawPayload *>(obs_whip_vps_->payload())->sample_;
        SrsNaluSample *sps = stap_payload_hevc ? stap_payload_hevc->get_sps() : NULL;
        if (!sps && obs_whip_sps_)
            sps = dynamic_cast<SrsRtpRawPayload *>(obs_whip_sps_->payload())->sample_;
        SrsNaluSample *pps = stap_payload_hevc ? stap_payload_hevc->get_pps() : NULL;
        if (!pps && obs_whip_pps_)
            pps = dynamic_cast<SrsRtpRawPayload *>(obs_whip_pps_->payload())->sample_;
        if (!vps || !sps || !pps) {
            return srs_error_new(ERROR_RTC_RTP_MUXER, "no vps/sps/pps in stap-a hevc rtp. vps: %p, sps:%p, pps:%p", vps, sps, pps);
        }

        err = do_packet_sequence_header_hevc(pkt, vps, sps, pps);

        // Always reset the VPS/SPS/PPS cache after used it.
        srs_freep(obs_whip_vps_);
        srs_freep(obs_whip_sps_);
        srs_freep(obs_whip_pps_);

        if (err != srs_success) {
            return srs_error_wrap(err, "packet vps/sps/pps");
        }
    }

    return err;
}

srs_error_t SrsRtcFrameBuilder::do_packet_sequence_header_hevc(SrsRtpPacket *pkt, SrsNaluSample *vps, SrsNaluSample *sps, SrsNaluSample *pps)
{
    srs_error_t err = srs_success;

    SrsUniquePtr<SrsRawHEVCStream> hevc(new SrsRawHEVCStream());

    std::vector<string> h265_pps;
    h265_pps.push_back(string(pps->bytes_, pps->size_));

    std::string sh;
    if ((err = hevc->mux_sequence_header(string(vps->bytes_, vps->size_), string(sps->bytes_, sps->size_), h265_pps, sh)) != srs_success) {
        return srs_error_wrap(err, "mux sequence header");
    }

    char *flv = NULL;
    int nb_flv = 0;
    if ((err = hevc->mux_hevc2flv_enhanced(sh, SrsVideoAvcFrameTypeKeyFrame, SrsVideoHEVCFrameTraitPacketTypeSequenceStart, pkt->get_avsync_time(),
                                           pkt->get_avsync_time(), &flv, &nb_flv)) != srs_success) {
        return srs_error_wrap(err, "mux sequence header");
    }

    SrsMessageHeader header;
    header.initialize_video(nb_flv, pkt->get_avsync_time(), 1);
    SrsRtmpCommonMessage rtmp;
    if ((err = rtmp.create(&header, flv, nb_flv)) != srs_success) {
        return srs_error_wrap(err, "create rtmp");
    }

    SrsMediaPacket msg;
    rtmp.to_msg(&msg);

    if ((err = frame_target_->on_frame(&msg)) != srs_success) {
        return err;
    }

    return err;
}

int SrsRtcFrameBuilder::calculate_packet_payload_size(SrsRtpPacket *pkt)
{
    if (!pkt || !pkt->payload()) {
        return 0;
    }

    // H.264 FU-A payload
    SrsRtpFUAPayload2 *fua_payload = dynamic_cast<SrsRtpFUAPayload2 *>(pkt->payload());
    if (fua_payload) {
        int size = fua_payload->size_;
        if (size <= 0) {
            return 0;
        }
        if (fua_payload->start_) {
            size += 1 + 4; // NALU header + length prefix
        }
        return size;
    }

    // H.264 STAP-A payload
    SrsRtpSTAPPayload *stap_payload = dynamic_cast<SrsRtpSTAPPayload *>(pkt->payload());
    if (stap_payload) {
        int size = 0;
        for (int j = 0; j < (int)stap_payload->nalus_.size(); ++j) {
            SrsNaluSample *sample = stap_payload->nalus_.at(j);
            if (sample->size_ > 0) {
                size += 4 + sample->size_; // length prefix + NALU
            }
        }
        return size;
    }

    // H.265 FU-A payload
    SrsRtpFUAPayloadHevc2 *fua_payload_hevc = dynamic_cast<SrsRtpFUAPayloadHevc2 *>(pkt->payload());
    if (fua_payload_hevc) {
        int size = fua_payload_hevc->size_;
        if (size <= 0) {
            return 0;
        }
        if (fua_payload_hevc->start_) {
            size += 2 + 4; // HEVC NALU header + length prefix
        }
        return size;
    }

    // H.265 STAP payload
    SrsRtpSTAPPayloadHevc *stap_payload_hevc = dynamic_cast<SrsRtpSTAPPayloadHevc *>(pkt->payload());
    if (stap_payload_hevc) {
        int size = 0;
        for (int j = 0; j < (int)stap_payload_hevc->nalus_.size(); ++j) {
            SrsNaluSample *sample = stap_payload_hevc->nalus_.at(j);
            if (sample->size_ > 0) {
                size += 4 + sample->size_; // length prefix + NALU
            }
        }
        return size;
    }

    // Raw payload
    SrsRtpRawPayload *raw_payload = dynamic_cast<SrsRtpRawPayload *>(pkt->payload());
    if (raw_payload) {
        if (raw_payload->nn_payload_ <= 1) {
            return 0; // Ignore empty payload, which only has the NALU header.
        }
        return 4 + raw_payload->nn_payload_; // length prefix + payload
    }

    return 0;
}

void SrsRtcFrameBuilder::write_packet_payload_to_buffer(SrsRtpPacket *pkt, SrsBuffer &payload, int &nalu_len)
{
    if (!pkt || !pkt->payload()) {
        return;
    }

    // H.264 FU-A payload
    SrsRtpFUAPayload2 *fua_payload = dynamic_cast<SrsRtpFUAPayload2 *>(pkt->payload());
    if (fua_payload) {
        int size = fua_payload->size_;
        if (size <= 0) {
            return;
        }
        if (fua_payload->start_) {
            nalu_len = size + 1;
            payload.skip(4); // Skip 4 bytes to write nalu_len later
            payload.write_1bytes(fua_payload->nri_ | fua_payload->nalu_type_);
            payload.write_bytes(fua_payload->payload_, size);
        } else {
            nalu_len += size;
            payload.write_bytes(fua_payload->payload_, size);
            if (fua_payload->end_) {
                // Write nalu_len back
                payload.skip(-(4 + nalu_len));
                payload.write_4bytes(nalu_len);
                payload.skip(nalu_len);
            }
        }
        return;
    }

    // H.264 STAP-A payload
    SrsRtpSTAPPayload *stap_payload = dynamic_cast<SrsRtpSTAPPayload *>(pkt->payload());
    if (stap_payload) {
        for (int j = 0; j < (int)stap_payload->nalus_.size(); ++j) {
            SrsNaluSample *sample = stap_payload->nalus_.at(j);
            if (sample->size_ > 0) {
                payload.write_4bytes(sample->size_);
                payload.write_bytes(sample->bytes_, sample->size_);
            }
        }
        return;
    }

    // H.265 FU-A payload
    SrsRtpFUAPayloadHevc2 *fua_payload_hevc = dynamic_cast<SrsRtpFUAPayloadHevc2 *>(pkt->payload());
    if (fua_payload_hevc) {
        int size = fua_payload_hevc->size_;
        if (size <= 0) {
            return;
        }
        if (fua_payload_hevc->start_) {
            nalu_len = size + 2;
            payload.skip(4); // Skip 4 bytes to write nalu_len later
            payload.write_1bytes(fua_payload_hevc->nalu_type_ << 1);
            payload.write_1bytes(0x01);
            payload.write_bytes(fua_payload_hevc->payload_, size);
        } else {
            nalu_len += size;
            payload.write_bytes(fua_payload_hevc->payload_, size);
            if (fua_payload_hevc->end_) {
                // Write nalu_len back
                payload.skip(-(4 + nalu_len));
                payload.write_4bytes(nalu_len);
                payload.skip(nalu_len);
            }
        }
        return;
    }

    // H.265 STAP payload
    SrsRtpSTAPPayloadHevc *stap_payload_hevc = dynamic_cast<SrsRtpSTAPPayloadHevc *>(pkt->payload());
    if (stap_payload_hevc) {
        for (int j = 0; j < (int)stap_payload_hevc->nalus_.size(); ++j) {
            SrsNaluSample *sample = stap_payload_hevc->nalus_.at(j);
            if (sample->size_ > 0) {
                payload.write_4bytes(sample->size_);
                payload.write_bytes(sample->bytes_, sample->size_);
            }
        }
        return;
    }

    // Raw payload
    SrsRtpRawPayload *raw_payload = dynamic_cast<SrsRtpRawPayload *>(pkt->payload());
    if (raw_payload) {
        if (raw_payload->nn_payload_ <= 1) {
            return; // Ignore empty payload, which only has the NALU header.
        }
        payload.write_4bytes(raw_payload->nn_payload_);
        payload.write_bytes(raw_payload->payload_, raw_payload->nn_payload_);
        return;
    }
}

srs_error_t SrsRtcFrameBuilder::packet_video_rtmp(const uint16_t start, const uint16_t end)
{
    srs_error_t err = srs_success;

    int nb_payload = 0;
    int16_t cnt = srs_rtp_seq_distance(start, end) + 1;
    srs_assert(cnt >= 1);

    // The start position packet may be null, so we need to find the actual first packet.
    SrsRtpPacket *first_frame_pkt = NULL;

    // First loop: Calculate total payload size and find first packet
    for (uint16_t i = 0; i < (uint16_t)cnt; ++i) {
        uint16_t sn = start + i;
        SrsRtpPacket *pkt = video_cache_->get_packet(sn);

        if (!pkt)
            continue;

        // Set the first available packet of the frame
        if (!first_frame_pkt) {
            first_frame_pkt = pkt;
        }

        // Calculate payload size using helper function
        nb_payload += calculate_packet_payload_size(pkt);
    }

    if (0 == nb_payload) {
        srs_info("RTC2RTMP: empty rtp packet, start=%u, end=%u, cnt=%d", start, end, cnt);

        // The chrome web browser send RTP packet with empty payload frequently,
        // reset header_sn_, lost_sn_ and continue to found next frame in this case,
        // otherwise, all the cached RTP packets are dropped before next key frame arrive.
        uint16_t next_start, next_end;
        bool got_frame;
        if ((err = frame_detector_->detect_next_frame(end + 1, next_start, next_end, got_frame)) != srs_success) {
            return srs_error_wrap(err, "update frame detector failed");
        }
        if (got_frame) {
            if ((err = packet_video_rtmp(next_start, next_end)) != srs_success) {
                err = srs_error_wrap(err, "fail to pack video frame, start=%u, end=%u", next_start, next_end);
            }
        }

        return err;
    }

    // If no first frame packet, it make no sense to continue.
    if (!first_frame_pkt) {
        return srs_error_new(ERROR_RTC_RTP_MUXER, "no available packets in frame range, start=%u, end=%u", start, end);
    }

    // h265: IsExHeader | FrameType | PacketType + Video FourCC
    // h264: FrameType | CodecID + avc_type + composition time + nalu size + nalu
    nb_payload += 5;

    // Note that the start position may be null, so it's not the real correct start
    // packet of a video frame, therefore we use the first available packet instead.
    SrsRtpPacket *pkt = first_frame_pkt;

    if (pkt->is_keyframe(video_codec_)) {
        frame_detector_->on_keyframe_detached();
    }

    SrsVideoAvcFrameType frame_type = SrsVideoAvcFrameTypeInterFrame;
    if (pkt->is_keyframe(video_codec_)) {
        frame_type = SrsVideoAvcFrameTypeKeyFrame;
    }

    SrsRtmpCommonMessage rtmp;
    rtmp.header_.initialize_video(nb_payload, pkt->get_avsync_time(), 1);
    rtmp.create_payload(nb_payload);
    SrsBuffer payload(rtmp.payload(), rtmp.size());
    if (video_codec_ == SrsVideoCodecIdHEVC) {
        // @see: https://veovera.org/docs/enhanced/enhanced-rtmp-v1.pdf, page 8
        payload.write_1bytes(SRS_FLV_IS_EX_HEADER | (frame_type << 4) | SrsVideoHEVCFrameTraitPacketTypeCodedFramesX);
        payload.write_4bytes(0x68766331); // 'h' 'v' 'c' '1'
    } else {
        // @see: E.4.3 Video Tags, video_file_format_spec_v10_1.pdf, page 78
        // Frame Type, Type of video frame.
        // CodecID, Codec Identifier.
        // set the rtmp header
        payload.write_1bytes((frame_type << 4) | video_codec_);
        payload.write_1bytes(0x01); // avc_type: nalu
        payload.write_1bytes(0x0);  // composition time
        payload.write_1bytes(0x0);
        payload.write_1bytes(0x0);
    }

    // Second loop: Write payload data using helper function
    int nalu_len = 0;
    for (uint16_t i = 0; i < (uint16_t)cnt; ++i) {
        uint16_t sequence_number = start + i;
        SrsRtpPacket *pkt_raw = video_cache_->take_packet(sequence_number);

        if (!pkt_raw)
            continue;

        SrsUniquePtr<SrsRtpPacket> pkt(pkt_raw);
        write_packet_payload_to_buffer(pkt.get(), payload, nalu_len);
    }

    SrsMediaPacket msg;
    rtmp.to_msg(&msg);

    if ((err = frame_target_->on_frame(&msg)) != srs_success) {
        srs_warn("fail to pack video frame: %s", srs_error_summary(err).c_str());
        srs_freep(err);
    }

    // Try to detect and detach next RTMP packet.
    uint16_t next_start, next_end;
    bool got_frame;
    if ((err = frame_detector_->detect_next_frame(end + 1, next_start, next_end, got_frame)) != srs_success) {
        return srs_error_wrap(err, "update frame detector failed");
    }
    if (got_frame) {
        if ((err = packet_video_rtmp(next_start, next_end)) != srs_success) {
            err = srs_error_wrap(err, "fail to pack video frame, start=%u, end=%u", next_start, next_end);
        }
    }

    return err;
}

#endif

SrsCodecPayload::SrsCodecPayload()
{
    pt_of_publisher_ = pt_ = 0;
    sample_ = 0;
    codec_ = -1;
}

SrsCodecPayload::SrsCodecPayload(uint8_t pt, std::string encode_name, int sample)
{
    pt_of_publisher_ = pt_ = pt;
    name_ = encode_name;
    sample_ = sample;
    codec_ = -1;
}

SrsCodecPayload::~SrsCodecPayload()
{
}

int8_t SrsCodecPayload::codec(bool video)
{
    // Return cached value if already initialized
    if (codec_ != -1) {
        return codec_;
    }

    // Parse codec based on context (video or audio)
    if (video) {
        // For unknown video codecs like H.266, still return SrsVideoCodecIdReserved
        // but it's correctly identified as a video codec due to the context
        codec_ = srs_video_codec_str2id(name_);
    } else {
        codec_ = srs_audio_codec_str2id(name_);
    }

    return codec_;
}

SrsCodecPayload *SrsCodecPayload::copy()
{
    SrsCodecPayload *cp = new SrsCodecPayload();

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
    : SrsCodecPayload(pt, encode_name, sample)
{
    type_ = "video";
    h264_param_.profile_level_id_ = "";
    h264_param_.packetization_mode_ = "";
    h264_param_.level_asymmetry_allow_ = "";
}

SrsVideoPayload::~SrsVideoPayload()
{
}

SrsVideoPayload *SrsVideoPayload::copy()
{
    SrsVideoPayload *cp = new SrsVideoPayload();

    cp->type_ = type_;
    cp->pt_ = pt_;
    cp->pt_of_publisher_ = pt_of_publisher_;
    cp->name_ = name_;
    cp->sample_ = sample_;
    cp->rtcp_fbs_ = rtcp_fbs_;
    cp->h264_param_ = h264_param_;
    cp->h265_param_ = h265_param_;

    return cp;
}

SrsMediaPayloadType SrsVideoPayload::generate_media_payload_type()
{
    SrsMediaPayloadType media_payload_type(pt_);

    media_payload_type.encoding_name_ = name_;
    media_payload_type.clock_rate_ = sample_;
    media_payload_type.rtcp_fb_ = rtcp_fbs_;

    std::ostringstream format_specific_param;
    bool has_param = false;

    if (!h264_param_.level_asymmetry_allow_.empty()) {
        format_specific_param << "level-asymmetry-allowed=" << h264_param_.level_asymmetry_allow_;
        has_param = true;
    }
    if (!h264_param_.packetization_mode_.empty()) {
        if (has_param)
            format_specific_param << ";";
        format_specific_param << "packetization-mode=" << h264_param_.packetization_mode_;
        has_param = true;
    }
    if (!h264_param_.profile_level_id_.empty()) {
        if (has_param)
            format_specific_param << ";";
        format_specific_param << "profile-level-id=" << h264_param_.profile_level_id_;
    }

    media_payload_type.format_specific_param_ = format_specific_param.str();

    return media_payload_type;
}

SrsMediaPayloadType SrsVideoPayload::generate_media_payload_type_h265()
{
    SrsMediaPayloadType media_payload_type(pt_);

    media_payload_type.encoding_name_ = name_;
    media_payload_type.clock_rate_ = sample_;
    media_payload_type.rtcp_fb_ = rtcp_fbs_;

    std::ostringstream format_specific_param;
    bool has_param = false;

    if (!h265_param_.level_id_.empty()) {
        format_specific_param << "level-id=" << h265_param_.level_id_;
        has_param = true;
    }
    if (!h265_param_.profile_id_.empty()) {
        if (has_param)
            format_specific_param << ";";
        format_specific_param << "profile-id=" << h265_param_.profile_id_;
        has_param = true;
    }
    if (!h265_param_.tier_flag_.empty()) {
        if (has_param)
            format_specific_param << ";";
        format_specific_param << "tier-flag=" << h265_param_.tier_flag_;
        has_param = true;
    }
    if (!h265_param_.tx_mode_.empty()) {
        if (has_param)
            format_specific_param << ";";
        format_specific_param << "tx-mode=" << h265_param_.tx_mode_;
    }

    media_payload_type.format_specific_param_ = format_specific_param.str();

    return media_payload_type;
}

// level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42e01f
srs_error_t SrsVideoPayload::set_h264_param_desc(std::string fmtp)
{
    srs_error_t err = srs_success;

    std::vector<std::string> attributes = split_str(fmtp, ";");

    for (size_t i = 0; i < attributes.size(); ++i) {
        std::string attribute = attributes.at(i);

        std::vector<std::string> kv = split_str(attribute, "=");
        if (kv.size() != 2) {
            return srs_error_new(ERROR_RTC_SDP_DECODE, "invalid h264 param=%s", attribute.c_str());
        }

        if (kv[0] == "profile-level-id") {
            h264_param_.profile_level_id_ = kv[1];
        } else if (kv[0] == "packetization-mode") {
            // 6.3.  Non-Interleaved Mode
            // This mode is in use when the value of the OPTIONAL packetization-mode
            // media type parameter is equal to 1.  This mode SHOULD be supported.
            // It is primarily intended for low-delay applications.  Only single NAL
            // unit packets, STAP-As, and FU-As MAY be used in this mode.  STAP-Bs,
            // MTAPs, and FU-Bs MUST NOT be used.  The transmission order of NAL
            // units MUST comply with the NAL unit decoding order.
            // @see https://tools.ietf.org/html/rfc6184#section-6.3
            h264_param_.packetization_mode_ = kv[1];
        } else if (kv[0] == "level-asymmetry-allowed") {
            h264_param_.level_asymmetry_allow_ = kv[1];
        } else {
            return srs_error_new(ERROR_RTC_SDP_DECODE, "invalid h264 param=%s", kv[0].c_str());
        }
    }

    return err;
}

// level-id=156;profile-id=1;tier-flag=0;tx-mode=SRST
srs_error_t SrsVideoPayload::set_h265_param_desc(std::string fmtp)
{
    std::vector<std::string> attributes = split_str(fmtp, ";");
    for (size_t i = 0; i < attributes.size(); ++i) {
        std::string attribute = attributes.at(i);
        std::vector<std::string> kv = split_str(attribute, "=");
        if (kv.size() != 2) {
            return srs_error_new(ERROR_RTC_SDP_DECODE, "invalid h265 param=%s", attribute.c_str());
        }
        if (kv[0] == "level-id") {
            h265_param_.level_id_ = kv[1];
        } else if (kv[0] == "profile-id") {
            h265_param_.profile_id_ = kv[1];
        } else if (kv[0] == "tier-flag") {
            h265_param_.tier_flag_ = kv[1];
        } else if (kv[0] == "tx-mode") {
            h265_param_.tx_mode_ = kv[1];
        } else {
            return srs_error_new(ERROR_RTC_SDP_DECODE, "invalid h265 param=%s", kv[0].c_str());
        }
    }
    return srs_success;
}

SrsAudioPayload::SrsAudioPayload()
{
    channel_ = 0;
}

SrsAudioPayload::SrsAudioPayload(uint8_t pt, std::string encode_name, int sample, int channel)
    : SrsCodecPayload(pt, encode_name, sample)
{
    type_ = "audio";
    channel_ = channel;
    opus_param_.minptime_ = 0;
    opus_param_.use_inband_fec_ = false;
    opus_param_.stereo_ = false;
    opus_param_.usedtx_ = false;
}

SrsAudioPayload::~SrsAudioPayload()
{
}

SrsAudioPayload *SrsAudioPayload::copy()
{
    SrsAudioPayload *cp = new SrsAudioPayload();

    cp->type_ = type_;
    cp->pt_ = pt_;
    cp->pt_of_publisher_ = pt_of_publisher_;
    cp->name_ = name_;
    cp->sample_ = sample_;
    cp->rtcp_fbs_ = rtcp_fbs_;
    cp->channel_ = channel_;
    cp->opus_param_ = opus_param_;
    cp->aac_config_hex_ = aac_config_hex_;

    return cp;
}

SrsMediaPayloadType SrsAudioPayload::generate_media_payload_type()
{
    SrsMediaPayloadType media_payload_type(pt_);

    media_payload_type.encoding_name_ = name_;
    media_payload_type.clock_rate_ = sample_;
    if (channel_ != 0) {
        media_payload_type.encoding_param_ = srs_strconv_format_int(channel_);
    }
    media_payload_type.rtcp_fb_ = rtcp_fbs_;

    std::ostringstream format_specific_param;
    if (opus_param_.minptime_) {
        format_specific_param << "minptime=" << opus_param_.minptime_;
    }
    if (opus_param_.use_inband_fec_) {
        format_specific_param << ";useinbandfec=1";
    }
    if (opus_param_.stereo_) {
        format_specific_param << ";stereo=1";
    }
    if (opus_param_.usedtx_) {
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
                opus_param_.minptime_ = (int)::atol(kv[1].c_str());
            } else if (kv[0] == "useinbandfec") {
                opus_param_.use_inband_fec_ = (kv[1] == "1") ? true : false;
            } else if (kv[0] == "stereo") {
                opus_param_.stereo_ = (kv[1] == "1") ? true : false;
            } else if (kv[0] == "usedtx") {
                opus_param_.usedtx_ = (kv[1] == "1") ? true : false;
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
    : SrsCodecPayload(pt, encode_name, sample)
{
    channel_ = channel;
}

SrsRedPayload::~SrsRedPayload()
{
}

SrsRedPayload *SrsRedPayload::copy()
{
    SrsRedPayload *cp = new SrsRedPayload();

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
        media_payload_type.encoding_param_ = srs_strconv_format_int(channel_);
    }
    media_payload_type.rtcp_fb_ = rtcp_fbs_;

    return media_payload_type;
}

SrsRtxPayloadDes::SrsRtxPayloadDes()
{
}

SrsRtxPayloadDes::SrsRtxPayloadDes(uint8_t pt, uint8_t apt) : SrsCodecPayload(pt, "rtx", 8000), apt_(apt)
{
}

SrsRtxPayloadDes::~SrsRtxPayloadDes()
{
}

SrsRtxPayloadDes *SrsRtxPayloadDes::copy()
{
    SrsRtxPayloadDes *cp = new SrsRtxPayloadDes();

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
    format_specific_param << "fmtp:" << pt_ << " apt=" << apt_;

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
    for (std::map<int, std::string>::iterator it = extmaps_.begin(); it != extmaps_.end(); ++it) {
        if (uri == it->second) {
            extmaps_.erase(it++);
            break;
        }
    }
}

void SrsRtcTrackDescription::set_direction(std::string direction)
{
    direction_ = direction;
}

void SrsRtcTrackDescription::set_codec_payload(SrsCodecPayload *payload)
{
    media_ = payload;
}

void SrsRtcTrackDescription::create_auxiliary_payload(const std::vector<SrsMediaPayloadType> payloads)
{
    if (!payloads.size()) {
        return;
    }

    SrsMediaPayloadType payload = payloads.at(0);
    if (payload.encoding_name_ == "red") {
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
        if (uri == it->second) {
            return it->first;
        }
    }

    return 0;
}

SrsRtcTrackDescription *SrsRtcTrackDescription::copy()
{
    SrsRtcTrackDescription *cp = new SrsRtcTrackDescription();

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
    cp->media_ = media_ ? media_->copy() : NULL;
    cp->red_ = red_ ? red_->copy() : NULL;
    cp->rtx_ = rtx_ ? rtx_->copy() : NULL;
    cp->ulpfec_ = ulpfec_ ? ulpfec_->copy() : NULL;

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

SrsRtcSourceDescription *SrsRtcSourceDescription::copy()
{
    SrsRtcSourceDescription *stream_desc = new SrsRtcSourceDescription();

    if (audio_track_desc_) {
        stream_desc->audio_track_desc_ = audio_track_desc_->copy();
    }

    for (int i = 0; i < (int)video_track_descs_.size(); ++i) {
        stream_desc->video_track_descs_.push_back(video_track_descs_.at(i)->copy());
    }

    return stream_desc;
}

SrsRtcTrackDescription *SrsRtcSourceDescription::find_track_description_by_ssrc(uint32_t ssrc)
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

ISrsRtcPacketReceiver::ISrsRtcPacketReceiver()
{
}

ISrsRtcPacketReceiver::~ISrsRtcPacketReceiver()
{
}

SrsRtcRecvTrack::SrsRtcRecvTrack(ISrsRtcPacketReceiver *receiver, SrsRtcTrackDescription *track_desc, bool is_audio, bool init_rate_from_sdp)
{
    receiver_ = receiver;
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

    // Initialize rate from SDP sample rate
    // rate_ is RTP units per millisecond (e.g., 90 for video 90kHz, 48 for audio 48kHz)
    // This allows immediate A/V sync before receiving 2 RTCP SR packets
    // Will be updated to precise rate after receiving 2nd SR
    rate_ = 0.0;
    if (init_rate_from_sdp && track_desc_->media_) {
        rate_ = static_cast<double>(track_desc_->media_->sample_) / 1000.0;
        srs_trace("RTC: Init %s track, ssrc=%u, rate from SDP=%.0f (RTP units per ms, will be updated after 2nd SR)",
                  track_desc_->type_.c_str(), track_desc_->ssrc_, rate_);
    }

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

void SrsRtcRecvTrack::update_send_report_time(const SrsNtp &ntp, uint32_t rtp_time)
{
    last_sender_report_ntp1_ = last_sender_report_ntp_;
    last_sender_report_rtp_time1_ = last_sender_report_rtp_time_;

    last_sender_report_ntp_ = ntp;
    last_sender_report_rtp_time_ = rtp_time;

    // TODO: FIXME: Use system wall clock.
    last_sender_report_sys_time_ = srs_time_now_realtime();

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

        if (rate > 0) {
            if (rate_ != rate) {
                srs_warn("RTC: SR update %s, ssrc=%u, ntp_ms=%u->%u (delta=%.0fms), rtp_time=%u->%u (delta=%.0f), rate %.0f->%.0f",
                         track_desc_->type_.c_str(), track_desc_->ssrc_,
                         last_sender_report_ntp1_.system_ms_, last_sender_report_ntp_.system_ms_, sys_time_elapsed,
                         (uint32_t)last_sender_report_rtp_time1_, (uint32_t)last_sender_report_rtp_time_, rtp_time_elpased,
                         rate_, rate);
            }
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
    const uint64_t &last_time = last_sender_report_sys_time_;
    if ((err = receiver_->send_rtcp_rr(ssrc, rtp_queue_, last_time, last_sender_report_ntp_)) != srs_success) {
        return srs_error_wrap(err, "ssrc=%u, last_time=%" PRId64, ssrc, last_time);
    }

    return err;
}

srs_error_t SrsRtcRecvTrack::send_rtcp_xr_rrtr()
{
    srs_error_t err = srs_success;

    if ((err = receiver_->send_rtcp_xr_rrtr(track_desc_->ssrc_)) != srs_success) {
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

srs_error_t SrsRtcRecvTrack::on_nack(SrsRtpPacket **ppkt)
{
    srs_error_t err = srs_success;

    SrsRtpPacket *pkt = *ppkt;
    uint16_t seq = pkt->header_.get_sequence();
    SrsRtpNackInfo *nack_info = nack_receiver_->find(seq);

    if (nack_info) {
        // seq had been received.
        nack_receiver_->remove(seq);
#ifdef SRS_NACK_DEBUG_DROP_ENABLED
        srs_trace("NACK: recovered seq=%u", seq);
#endif
    } else {
        // insert check nack list
        uint16_t nack_first = 0, nack_last = 0;
        if (!rtp_queue_->update(seq, nack_first, nack_last)) {
            srs_warn("NACK: too old seq %u, range [%u, %u]", seq, rtp_queue_->begin_,
                     rtp_queue_->end_);
        }

        if (srs_rtp_seq_distance(nack_first, nack_last) > 0) {
            // If circuit-breaker is enabled, disable nack.
            if (_srs_circuit_breaker->hybrid_high_water_level()) {
                ++_srs_pps_snack4->sugar_;
            } else {
                srs_trace("NACK: update seq=%u, nack range [%u, %u]", seq, nack_first,
                          nack_last);

                nack_receiver_->insert(nack_first, nack_last);
                nack_receiver_->check_queue_size();
            }
        }
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

srs_error_t SrsRtcRecvTrack::do_check_send_nacks(uint32_t &timeout_nacks)
{
    srs_error_t err = srs_success;

    uint32_t sent_nacks = 0;
    receiver_->check_send_nacks(nack_receiver_, track_desc_->ssrc_, sent_nacks, timeout_nacks);

    return err;
}

SrsRtcAudioRecvTrack::SrsRtcAudioRecvTrack(ISrsRtcPacketReceiver *receiver, SrsRtcTrackDescription *track_desc, bool init_rate_from_sdp)
    : SrsRtcRecvTrack(receiver, track_desc, true, init_rate_from_sdp)
{
}

SrsRtcAudioRecvTrack::~SrsRtcAudioRecvTrack()
{
}

void SrsRtcAudioRecvTrack::on_before_decode_payload(SrsRtpPacket *pkt, SrsBuffer *buf, ISrsRtpPayloader **ppayload, SrsRtpPacketPayloadType *ppt)
{
    // No payload, ignore.
    if (buf->empty()) {
        return;
    }

    *ppayload = new SrsRtpRawPayload();
    *ppt = SrsRtpPacketPayloadTypeRaw;
}

srs_error_t SrsRtcAudioRecvTrack::on_rtp(SrsSharedPtr<SrsRtcSource> &source, SrsRtpPacket *pkt)
{
    srs_error_t err = srs_success;

    pkt->set_avsync_time(cal_avsync_time(pkt->header_.get_timestamp()));
    srs_info("Audio async rate=%d, rtp=%u, corrected=%" PRId64, (int)rate_, pkt->header_.get_timestamp(), pkt->get_avsync_time());

    if ((err = source->on_rtp(pkt)) != srs_success) {
        return srs_error_wrap(err, "source on rtp");
    }

    return err;
}

srs_error_t SrsRtcAudioRecvTrack::check_send_nacks()
{
    srs_error_t err = srs_success;

    ++_srs_pps_sanack->sugar_;

    uint32_t timeout_nacks = 0;
    if ((err = do_check_send_nacks(timeout_nacks)) != srs_success) {
        return srs_error_wrap(err, "audio");
    }

    return err;
}

SrsRtcVideoRecvTrack::SrsRtcVideoRecvTrack(ISrsRtcPacketReceiver *receiver, SrsRtcTrackDescription *track_desc, bool init_rate_from_sdp)
    : SrsRtcRecvTrack(receiver, track_desc, false, init_rate_from_sdp)
{
}

SrsRtcVideoRecvTrack::~SrsRtcVideoRecvTrack()
{
}

void SrsRtcVideoRecvTrack::on_before_decode_payload(SrsRtpPacket *pkt, SrsBuffer *buf, ISrsRtpPayloader **ppayload, SrsRtpPacketPayloadType *ppt)
{
    // No payload, ignore.
    if (buf->empty()) {
        return;
    }

    SrsVideoCodecId codec = (SrsVideoCodecId)track_desc_->media_->codec(true);
    if (codec == SrsVideoCodecIdAVC) {
        uint8_t v = SrsAvcNaluTypeParse(buf->head()[0]);
        pkt->nalu_type_ = v;

        if (v == kStapA) {
            *ppayload = new SrsRtpSTAPPayload();
            *ppt = SrsRtpPacketPayloadTypeSTAP;
        } else if (v == kFuA) {
            *ppayload = new SrsRtpFUAPayload2();
            *ppt = SrsRtpPacketPayloadTypeFUA2;
        } else {
            *ppayload = new SrsRtpRawPayload();
            *ppt = SrsRtpPacketPayloadTypeRaw;
        }
    } else if (codec == SrsVideoCodecIdHEVC) {
        uint8_t v = SrsHevcNaluTypeParse(buf->head()[0]);
        pkt->nalu_type_ = v;

        if (v == kStapHevc) {
            *ppayload = new SrsRtpSTAPPayloadHevc();
            *ppt = SrsRtpPacketPayloadTypeSTAPHevc;
        } else if (v == kFuHevc) {
            *ppayload = new SrsRtpFUAPayloadHevc2();
            *ppt = SrsRtpPacketPayloadTypeFUAHevc2;
        } else {
            *ppayload = new SrsRtpRawPayload();
            *ppt = SrsRtpPacketPayloadTypeRaw;
        }
    } else {
        *ppayload = NULL;
        *ppt = SrsRtpPacketPayloadTypeUnknown;
    }
}

srs_error_t SrsRtcVideoRecvTrack::on_rtp(SrsSharedPtr<SrsRtcSource> &source, SrsRtpPacket *pkt)
{
    srs_error_t err = srs_success;

    pkt->set_avsync_time(cal_avsync_time(pkt->header_.get_timestamp()));
    srs_info("Video async rate=%d, rtp=%u, corrected=%" PRId64, (int)rate_, pkt->header_.get_timestamp(), pkt->get_avsync_time());

    if ((err = source->on_rtp(pkt)) != srs_success) {
        return srs_error_wrap(err, "source on rtp");
    }

    return err;
}

srs_error_t SrsRtcVideoRecvTrack::check_send_nacks()
{
    srs_error_t err = srs_success;

    ++_srs_pps_svnack->sugar_;

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

ISrsRtcPacketSender::ISrsRtcPacketSender()
{
}

ISrsRtcPacketSender::~ISrsRtcPacketSender()
{
}

SrsRtcSendTrack::SrsRtcSendTrack(ISrsRtcPacketSender *sender, SrsRtcTrackDescription *track_desc, bool is_audio)
{
    sender_ = sender;
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

SrsRtpPacket *SrsRtcSendTrack::fetch_rtp_packet(uint16_t seq)
{
    SrsRtpPacket *pkt = rtp_queue_->at(seq);

    if (pkt == NULL) {
        return pkt;
    }

    // For NACK, it sequence must match exactly, or it cause SRTP fail.
    // Return packet only when sequence is equal.
    if (pkt->header_.get_sequence() == seq) {
        ++_srs_pps_rhnack->sugar_;
        return pkt;
    }
    ++_srs_pps_rmnack->sugar_;

    // Ignore if sequence not match.
    uint32_t nn = 0;
    if (nack_epp->can_print(pkt->header_.get_ssrc(), &nn)) {
        srs_trace("RTC: NACK miss seq=%u, require_seq=%u, ssrc=%u, ts=%u, count=%u/%u, %d bytes", seq, pkt->header_.get_sequence(),
                  pkt->header_.get_ssrc(), pkt->header_.get_timestamp(), nn, nack_epp->nn_count_, pkt->nb_bytes());
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

void SrsRtcSendTrack::rebuild_packet(SrsRtpPacket *pkt)
{
    // Rebuild the sequence number.
    int16_t seq = pkt->header_.get_sequence();
    pkt->header_.set_sequence(jitter_seq_->correct(seq));

    // Rebuild the timestamp.
    uint32_t ts = pkt->header_.get_timestamp();
    pkt->header_.set_timestamp(jitter_ts_->correct(ts));

    srs_info("RTC: Correct %s seq=%u/%u, ts=%u/%u", track_desc_->type_.c_str(), seq, pkt->header_.get_sequence(), ts, pkt->header_.get_timestamp());
}

srs_error_t SrsRtcSendTrack::on_nack(SrsRtpPacket **ppkt)
{
    srs_error_t err = srs_success;

    SrsRtpPacket *pkt = *ppkt;
    uint16_t seq = pkt->header_.get_sequence();

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

srs_error_t SrsRtcSendTrack::on_recv_nack(const vector<uint16_t> &lost_seqs)
{
    srs_error_t err = srs_success;

    ++_srs_pps_rnack2->sugar_;

    for (int i = 0; i < (int)lost_seqs.size(); ++i) {
        uint16_t seq = lost_seqs.at(i);
        SrsRtpPacket *pkt = fetch_rtp_packet(seq);
        if (pkt == NULL) {
            continue;
        }

        uint32_t nn = 0;
        if (nack_epp->can_print(pkt->header_.get_ssrc(), &nn)) {
            srs_trace("RTC: NACK ARQ seq=%u, ssrc=%u, ts=%u, count=%u/%u, %d bytes", pkt->header_.get_sequence(),
                      pkt->header_.get_ssrc(), pkt->header_.get_timestamp(), nn, nack_epp->nn_count_, pkt->nb_bytes());
        }

        // By default, we send packets by sendmmsg.
        if ((err = sender_->do_send_packet(pkt)) != srs_success) {
            return srs_error_wrap(err, "raw send");
        }
    }

    return err;
}

SrsRtcAudioSendTrack::SrsRtcAudioSendTrack(ISrsRtcPacketSender *sender, SrsRtcTrackDescription *track_desc)
    : SrsRtcSendTrack(sender, track_desc, true)
{
}

SrsRtcAudioSendTrack::~SrsRtcAudioSendTrack()
{
}

srs_error_t SrsRtcAudioSendTrack::on_rtp(SrsRtpPacket *pkt)
{
    srs_error_t err = srs_success;

    if (!track_desc_->is_active_) {
        return err;
    }

    pkt->header_.set_ssrc(track_desc_->ssrc_);

    // Should update PT, because subscriber may use different PT to publisher.
    if (track_desc_->media_ && pkt->header_.get_payload_type() == track_desc_->media_->pt_of_publisher_) {
        // If PT is media from publisher, change to PT of media for subscriber.
        pkt->header_.set_payload_type(track_desc_->media_->pt_);
    } else if (track_desc_->red_ && pkt->header_.get_payload_type() == track_desc_->red_->pt_of_publisher_) {
        // If PT is RED from publisher, change to PT of RED for subscriber.
        pkt->header_.set_payload_type(track_desc_->red_->pt_);
    } else {
        // TODO: FIXME: Should update PT for RTX.
    }

    // Rebuild the sequence number and timestamp of packet, see https://github.com/ossrs/srs/issues/3167
    rebuild_packet(pkt);

    if ((err = sender_->do_send_packet(pkt)) != srs_success) {
        return srs_error_wrap(err, "raw send");
    }

    srs_info("RTC: Send audio ssrc=%d, seqno=%d, keyframe=%d, ts=%u", pkt->header_.get_ssrc(),
             pkt->header_.get_sequence(), pkt->is_keyframe(), pkt->header_.get_timestamp());

    return err;
}

srs_error_t SrsRtcAudioSendTrack::on_rtcp(SrsRtpPacket *pkt)
{
    srs_error_t err = srs_success;
    // process rtcp
    return err;
}

SrsRtcVideoSendTrack::SrsRtcVideoSendTrack(ISrsRtcPacketSender *sender, SrsRtcTrackDescription *track_desc)
    : SrsRtcSendTrack(sender, track_desc, false)
{
}

SrsRtcVideoSendTrack::~SrsRtcVideoSendTrack()
{
}

srs_error_t SrsRtcVideoSendTrack::on_rtp(SrsRtpPacket *pkt)
{
    srs_error_t err = srs_success;

    if (!track_desc_->is_active_) {
        return err;
    }

    pkt->header_.set_ssrc(track_desc_->ssrc_);

    // Should update PT, because subscriber may use different PT to publisher.
    if (track_desc_->media_ && pkt->header_.get_payload_type() == track_desc_->media_->pt_of_publisher_) {
        // If PT is media from publisher, change to PT of media for subscriber.
        pkt->header_.set_payload_type(track_desc_->media_->pt_);
    } else if (track_desc_->red_ && pkt->header_.get_payload_type() == track_desc_->red_->pt_of_publisher_) {
        // If PT is RED from publisher, change to PT of RED for subscriber.
        pkt->header_.set_payload_type(track_desc_->red_->pt_);
    } else {
        // TODO: FIXME: Should update PT for RTX.
    }

    // Rebuild the sequence number and timestamp of packet, see https://github.com/ossrs/srs/issues/3167
    rebuild_packet(pkt);

    if ((err = sender_->do_send_packet(pkt)) != srs_success) {
        return srs_error_wrap(err, "raw send");
    }

    srs_info("RTC: Send video ssrc=%d, seqno=%d, keyframe=%d, ts=%u", pkt->header_.get_ssrc(),
             pkt->header_.get_sequence(), pkt->is_keyframe(), pkt->header_.get_timestamp());

    return err;
}

srs_error_t SrsRtcVideoSendTrack::on_rtcp(SrsRtpPacket *pkt)
{
    srs_error_t err = srs_success;
    // process rtcp
    return err;
}

SrsRtcSSRCGenerator *SrsRtcSSRCGenerator::instance_ = NULL;

SrsRtcSSRCGenerator::SrsRtcSSRCGenerator()
{
    ssrc_num_ = 0;
}

SrsRtcSSRCGenerator::~SrsRtcSSRCGenerator()
{
}

SrsRtcSSRCGenerator *SrsRtcSSRCGenerator::instance()
{
    if (!instance_) {
        instance_ = new SrsRtcSSRCGenerator();
    }
    return instance_;
}

uint32_t SrsRtcSSRCGenerator::generate_ssrc()
{
    if (!ssrc_num_) {
        ssrc_num_ = ::getpid() * 10000 + ::getpid() * 100 + ::getpid();
    }

    return ++ssrc_num_;
}
