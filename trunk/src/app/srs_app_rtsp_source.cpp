//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_app_rtsp_source.hpp>

#include <srs_app_circuit_breaker.hpp>
#include <srs_app_config.hpp>
#include <srs_app_rtc_source.hpp>
#include <srs_app_rtsp_conn.hpp>
#include <srs_app_statistic.hpp>
#include <srs_app_stream_bridge.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_codec.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_rtp.hpp>
#include <srs_protocol_utility.hpp>

using namespace std;

extern SrsPps *_srs_pps_aloss2;

static const int kVideoSamplerate = 90000;

// the time to cleanup source.
#define SRS_RTSP_SOURCE_CLEANUP (3 * SRS_UTIME_SECONDS)

SrsRtspConsumer::SrsRtspConsumer(SrsRtspSource *s)
{
    source_ = s;
    should_update_source_id_ = false;
    handler_ = NULL;

    mw_wait_ = srs_cond_new();
    mw_min_msgs_ = 0;
    mw_waiting_ = false;
}

SrsRtspConsumer::~SrsRtspConsumer()
{
    source_->on_consumer_destroy(this);

    vector<SrsRtpPacket *>::iterator it;
    for (it = queue_.begin(); it != queue_.end(); ++it) {
        SrsRtpPacket *pkt = *it;
        srs_freep(pkt);
    }

    srs_cond_destroy(mw_wait_);
}

void SrsRtspConsumer::update_source_id()
{
    should_update_source_id_ = true;
}

srs_error_t SrsRtspConsumer::enqueue(SrsRtpPacket *pkt)
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

srs_error_t SrsRtspConsumer::dump_packet(SrsRtpPacket **ppkt)
{
    srs_error_t err = srs_success;

    // TODO: FIXME: Refine performance by ring buffer.
    if (!queue_.empty()) {
        *ppkt = queue_.front();
        queue_.erase(queue_.begin());
    }

    return err;
}

void SrsRtspConsumer::wait(int nb_msgs)
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

void SrsRtspConsumer::on_stream_change(SrsRtcSourceDescription *desc)
{
    if (handler_) {
        handler_->on_stream_change(desc);
    }
}

ISrsRtspSourceManager::ISrsRtspSourceManager()
{
}

ISrsRtspSourceManager::~ISrsRtspSourceManager()
{
}

SrsRtspSourceManager::SrsRtspSourceManager()
{
    lock_ = srs_mutex_new();
    timer_ = new SrsHourGlass("sources", this, 1 * SRS_UTIME_SECONDS);
}

SrsRtspSourceManager::~SrsRtspSourceManager()
{
    srs_mutex_destroy(lock_);
    srs_freep(timer_);
}

srs_error_t SrsRtspSourceManager::initialize()
{
    return setup_ticks();
}

srs_error_t SrsRtspSourceManager::setup_ticks()
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

srs_error_t SrsRtspSourceManager::notify(int event, srs_utime_t interval, srs_utime_t tick)
{
    srs_error_t err = srs_success;

    std::map<std::string, SrsSharedPtr<SrsRtspSource> >::iterator it;
    for (it = pool_.begin(); it != pool_.end();) {
        SrsSharedPtr<SrsRtspSource> &source = it->second;

        // When source expired, remove it.
        // @see https://github.com/ossrs/srs/issues/713
        if (source->stream_is_dead()) {
            SrsContextId cid = source->source_id();
            if (cid.empty())
                cid = source->pre_source_id();
            srs_trace("RTSP: cleanup die source, id=[%s], total=%d", cid.c_str(), (int)pool_.size());
            pool_.erase(it++);
        } else {
            ++it;
        }
    }

    return err;
}

srs_error_t SrsRtspSourceManager::fetch_or_create(ISrsRequest *r, SrsSharedPtr<SrsRtspSource> &pps)
{
    srs_error_t err = srs_success;

    bool created = false;
    // Should never invoke any function during the locking.
    if (true) {
        // Use lock to protect coroutine switch.
        // @bug https://github.com/ossrs/srs/issues/1230
        SrsLocker(&lock_);

        string stream_url = r->get_stream_url();
        std::map<std::string, SrsSharedPtr<SrsRtspSource> >::iterator it = pool_.find(stream_url);

        if (it != pool_.end()) {
            SrsSharedPtr<SrsRtspSource> source = it->second;
            pps = source;
        } else {
            SrsSharedPtr<SrsRtspSource> source = SrsSharedPtr<SrsRtspSource>(new SrsRtspSource());
            srs_trace("new rtsp source, stream_url=%s", stream_url.c_str());
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

SrsSharedPtr<SrsRtspSource> SrsRtspSourceManager::fetch(ISrsRequest *r)
{
    // Use lock to protect coroutine switch.
    // @bug https://github.com/ossrs/srs/issues/1230
    SrsLocker(&lock_);

    string stream_url = r->get_stream_url();
    std::map<std::string, SrsSharedPtr<SrsRtspSource> >::iterator it = pool_.find(stream_url);

    SrsSharedPtr<SrsRtspSource> source;
    if (it == pool_.end()) {
        return source;
    }

    source = it->second;
    return source;
}

SrsRtspSourceManager *_srs_rtsp_sources = NULL;

SrsResourceManager *_srs_rtsp_manager = NULL;

SrsRtspSource::SrsRtspSource()
{
    is_created_ = false;
    is_delivering_packets_ = false;

    audio_desc_ = NULL;
    video_desc_ = NULL;

    req_ = NULL;

    stream_die_at_ = 0;

    stat_ = _srs_stat;
    circuit_breaker_ = _srs_circuit_breaker;
}

SrsRtspSource::~SrsRtspSource()
{
    // never free the consumers,
    // for all consumers are auto free.
    consumers_.clear();

    srs_freep(req_);
    srs_freep(audio_desc_);
    srs_freep(video_desc_);

    SrsContextId cid = _source_id;
    if (cid.empty())
        cid = _pre_source_id;
    srs_trace("free rtc source id=[%s]", cid.c_str());

    stat_ = NULL;
    circuit_breaker_ = NULL;
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
srs_error_t SrsRtspSource::initialize(ISrsRequest *r)
{
    srs_error_t err = srs_success;

    req_ = r->copy();

    return err;
}

bool SrsRtspSource::stream_is_dead()
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
    if (now < stream_die_at_ + SRS_RTSP_SOURCE_CLEANUP) {
        return false;
    }

    return true;
}

void SrsRtspSource::update_auth(ISrsRequest *r)
{
    req_->update_auth(r);
}

srs_error_t SrsRtspSource::on_source_changed()
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

    // Build stream description.
    SrsUniquePtr<SrsRtcSourceDescription> stream_desc(new SrsRtcSourceDescription());
    if (audio_desc_) {
        stream_desc->audio_track_desc_ = audio_desc_->copy();
    }
    if (video_desc_) {
        stream_desc->video_track_descs_.push_back(video_desc_->copy());
    }

    // Notify all consumers.
    std::vector<SrsRtspConsumer *>::iterator it;
    for (it = consumers_.begin(); it != consumers_.end(); ++it) {
        SrsRtspConsumer *consumer = *it;

        // Notify if context id changed.
        if (id_changed) {
            consumer->update_source_id();
        }

        // Notify about stream description.
        consumer->on_stream_change(stream_desc.get());
    }

    return err;
}

SrsContextId SrsRtspSource::source_id()
{
    return _source_id;
}

SrsContextId SrsRtspSource::pre_source_id()
{
    return _pre_source_id;
}

srs_error_t SrsRtspSource::create_consumer(SrsRtspConsumer *&consumer)
{
    srs_error_t err = srs_success;

    consumer = new SrsRtspConsumer(this);
    consumers_.push_back(consumer);

    stream_die_at_ = 0;

    // TODO: FIXME: Implements edge cluster.

    return err;
}

srs_error_t SrsRtspSource::consumer_dumps(SrsRtspConsumer *consumer, bool ds, bool dm, bool dg)
{
    srs_error_t err = srs_success;

    // print status.
    srs_trace("create rtsp consumer, no gop cache");

    return err;
}

void SrsRtspSource::on_consumer_destroy(SrsRtspConsumer *consumer)
{
    std::vector<SrsRtspConsumer *>::iterator it;
    it = std::find(consumers_.begin(), consumers_.end(), consumer);
    if (it != consumers_.end()) {
        it = consumers_.erase(it);
    }

    // TODO: When all consumers finished, notify publisher to handle it.

    // Destroy and cleanup source when no publishers and consumers.
    if (!is_created_ && consumers_.empty()) {
        stream_die_at_ = srs_time_now_cached();
    }
}

bool SrsRtspSource::can_publish()
{
    // TODO: FIXME: Should check the status of bridge.

    return !is_created_;
}

void SrsRtspSource::set_stream_created()
{
    srs_assert(!is_created_ && !is_delivering_packets_);
    is_created_ = true;
}

srs_error_t SrsRtspSource::on_publish()
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

    stat_->on_stream_publish(req_, _source_id.c_str());

    return err;
}

void SrsRtspSource::on_unpublish()
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

    stat_->on_stream_close(req_);

    // Destroy and cleanup source when no publishers and consumers.
    if (consumers_.empty()) {
        stream_die_at_ = srs_time_now_cached();
    }

    // Should never change the final state before all cleanup is done.
    is_created_ = false;
    is_delivering_packets_ = false;
}

srs_error_t SrsRtspSource::on_rtp(SrsRtpPacket *pkt)
{
    srs_error_t err = srs_success;

    // If circuit-breaker is dying, drop packet.
    if (circuit_breaker_->hybrid_dying_water_level()) {
        _srs_pps_aloss2->sugar_ += (int64_t)consumers_.size();
        return err;
    }

    for (int i = 0; i < (int)consumers_.size(); i++) {
        SrsRtspConsumer *consumer = consumers_.at(i);
        if ((err = consumer->enqueue(pkt->copy())) != srs_success) {
            return srs_error_wrap(err, "consume message");
        }
    }

    return err;
}

SrsRtcTrackDescription *SrsRtspSource::audio_desc()
{
    return audio_desc_;
}

void SrsRtspSource::set_audio_desc(SrsRtcTrackDescription *audio_desc)
{
    srs_freep(audio_desc_);
    audio_desc_ = audio_desc->copy();
}

SrsRtcTrackDescription *SrsRtspSource::video_desc()
{
    return video_desc_;
}

void SrsRtspSource::set_video_desc(SrsRtcTrackDescription *video_desc)
{
    srs_freep(video_desc_);
    video_desc_ = video_desc->copy();
}

SrsRtspRtpBuilder::SrsRtspRtpBuilder(ISrsRtpTarget *target, SrsSharedPtr<SrsRtspSource> source)
{
    rtp_target_ = target;
    source_ = source;

    req_ = NULL;
    format_ = new SrsRtmpFormat();
    meta_ = new SrsMetaCache();
    video_builder_ = new SrsRtpVideoBuilder();
    audio_sequence_ = 0;

    // Initialize with default values - will be set during lazy initialization
    audio_ssrc_ = 0;
    audio_payload_type_ = 0;
    audio_sample_rate_ = 0;

    // Lazy initialization flags
    audio_initialized_ = false;
    video_initialized_ = false;

    config_ = _srs_config;
}

SrsRtspRtpBuilder::~SrsRtspRtpBuilder()
{
    srs_freep(format_);
    srs_freep(meta_);
    srs_freep(video_builder_);

    config_ = NULL;
}

srs_error_t SrsRtspRtpBuilder::initialize_audio_track(SrsAudioCodecId codec)
{
    srs_error_t err = srs_success;

    // RTSP behavior: Build track description from real audio format, not default values
    // This is different from RTC which uses default track descriptions

    SrsRand rand;

    // Create audio track description from actual format data
    SrsUniquePtr<SrsRtcTrackDescription> audio_desc(new SrsRtcTrackDescription());
    audio_desc->type_ = "audio";
    audio_desc->id_ = "audio-" + rand.gen_str(8);
    audio_desc->direction_ = "recvonly";

    // Generate SSRC for this track
    audio_ssrc_ = SrsRtcSSRCGenerator::instance()->generate_ssrc();
    audio_desc->ssrc_ = audio_ssrc_;

    int sample_rate = srs_flv_srates[format_->acodec_->sound_rate_];
    audio_sample_rate_ = sample_rate;

    // Build payload from actual audio format
    if (codec == SrsAudioCodecIdOpus) {
        // For Opus, use actual format parameters if available
        int channels = (format_->acodec_->sound_type_ == SrsAudioChannelsStereo) ? 2 : 1;
        audio_payload_type_ = kAudioPayloadType;
        audio_desc->media_ = new SrsAudioPayload(audio_payload_type_, "opus", sample_rate, channels);
    } else if (codec == SrsAudioCodecIdAAC) {
        // For AAC, extract parameters from format
        int channels = format_->acodec_->aac_channels_;
        audio_payload_type_ = kAudioPayloadType;

        // Note: Use "MPEG4-GENERIC" instead of "AAC" for RTSP/SDP compliance
        // RFC 3640 specifies that AAC should be advertised as "MPEG4-GENERIC" in SDP rtpmap
        // "AAC" is non-standard and not widely supported by RTSP clients
        SrsAudioPayload *aac_payload = new SrsAudioPayload(audio_payload_type_, "MPEG4-GENERIC", sample_rate, channels);

        // AAC requires AudioSpecificConfig in SDP fmtp line
        // Build the config string from AAC sequence header
        const std::vector<char> &asc = format_->acodec_->aac_extra_data_;
        if (!asc.empty()) {
            int hex_len = asc.size() * 2;
            SrsUniquePtr<char> hex_buf(new char[hex_len + 1]);
            srs_hex_encode_to_string(hex_buf.get(), (const uint8_t *)asc.data(), asc.size());

            hex_buf.get()[hex_len] = '\0'; // Null terminate
            std::string config_hex = std::string(hex_buf.get());

            // Set the AAC configuration directly in the audio payload
            aac_payload->aac_config_hex_ = config_hex;
            srs_trace("RTSP: AAC config hex: %s", config_hex.c_str());
        }

        audio_desc->media_ = aac_payload;
    } else {
        return srs_error_new(ERROR_RTC_RTP_MUXER, "unsupported audio codec %d", codec);
    }

    // Extract info for logging before setting to source
    int sample_rate_for_log = audio_desc->media_->sample_;
    int channels_for_log = (audio_desc->media_->type_ == "audio") ? ((SrsAudioPayload *)audio_desc->media_)->channel_ : 0;

    // Set the audio description to source
    source_->set_audio_desc(audio_desc.get());

    srs_trace("RTSP: Initialize audio track from format - codec=%s, ssrc=%u, pt=%d, sample_rate=%d, channels=%d",
              srs_audio_codec_id2str(codec).c_str(), audio_ssrc_, audio_payload_type_,
              sample_rate_for_log, channels_for_log);

    return err;
}

srs_error_t SrsRtspRtpBuilder::initialize_video_track(SrsVideoCodecId codec)
{
    srs_error_t err = srs_success;

    // RTSP behavior: Build track description from real video format, not default values
    // This is different from RTC which uses default track descriptions

    std::string codec_name = srs_video_codec_id2str(codec);

    SrsRand rand;

    // Create video track description from actual format data
    SrsUniquePtr<SrsRtcTrackDescription> video_desc(new SrsRtcTrackDescription());
    video_desc->type_ = "video";
    video_desc->id_ = "video-" + codec_name + "-" + rand.gen_str(8);
    video_desc->direction_ = "recvonly";

    // Generate SSRC for this track
    uint32_t video_ssrc = SrsRtcSSRCGenerator::instance()->generate_ssrc();
    video_desc->ssrc_ = video_ssrc;

    // Build payload from actual video format
    uint8_t video_payload_type = 0;
    if (codec == SrsVideoCodecIdAVC) {
        // H.264 track with actual format parameters
        video_payload_type = kVideoPayloadType;
        SrsVideoPayload *h264_payload = new SrsVideoPayload(video_payload_type, "H264", kVideoSamplerate);
        h264_payload->set_h264_param_desc("level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42e01f");
        video_desc->media_ = h264_payload;

    } else if (codec == SrsVideoCodecIdHEVC) {
        // H.265 track with actual format parameters
        video_payload_type = KVideoPayloadTypeHevc;
        SrsVideoPayload *h265_payload = new SrsVideoPayload(video_payload_type, "H265", kVideoSamplerate);
        h265_payload->set_h265_param_desc("level-id=156;profile-id=1;tier-flag=0;tx-mode=SRST");
        video_desc->media_ = h265_payload;

    } else {
        return srs_error_new(ERROR_RTC_RTP_MUXER, "unsupported video codec %d", codec);
    }

    SrsFormat *format = meta_->vsh_format();
    if ((err = video_builder_->initialize(format, video_ssrc, video_payload_type)) != srs_success) {
        return srs_error_wrap(err, "initialize video builder");
    }

    // Set the video description to source
    source_->set_video_desc(video_desc.get());

    srs_trace("RTSP: Initialize video track from format - codec=%s, ssrc=%u, pt=%d, sample_rate=%d",
              codec_name.c_str(), video_ssrc, video_payload_type, kVideoSamplerate);

    return err;
}

srs_error_t SrsRtspRtpBuilder::initialize(ISrsRequest *r)
{
    srs_error_t err = srs_success;

    req_ = r;

    if ((err = format_->initialize()) != srs_success) {
        return srs_error_wrap(err, "format initialize");
    }

    // Setup the SPS/PPS parsing strategy.
    format_->try_annexb_first_ = config_->try_annexb_first(r->vhost_);

    srs_trace("RTSP bridge from RTMP, try_annexb_first=%d", format_->try_annexb_first_);

    return err;
}

srs_error_t SrsRtspRtpBuilder::on_publish()
{
    srs_error_t err = srs_success;

    // Reset the metadata cache, to make VLC happy when disable/enable stream.
    // @see https://github.com/ossrs/srs/issues/1630#issuecomment-597979448
    meta_->clear();

    return err;
}

void SrsRtspRtpBuilder::on_unpublish()
{
    // Reset the metadata cache, to make VLC happy when disable/enable stream.
    // @see https://github.com/ossrs/srs/issues/1630#issuecomment-597979448
    meta_->update_previous_vsh();
    meta_->update_previous_ash();
}

srs_error_t SrsRtspRtpBuilder::on_frame(SrsMediaPacket *frame)
{
    if (frame->is_audio()) {
        return on_audio(frame);
    } else if (frame->is_video()) {
        return on_video(frame);
    }
    return srs_success;
}

srs_error_t SrsRtspRtpBuilder::on_audio(SrsMediaPacket *msg)
{
    srs_error_t err = srs_success;

    if ((err = format_->on_audio(msg)) != srs_success) {
        return srs_error_wrap(err, "format consume audio");
    }

    // Ignore if no format->acodec, it means the codec is not parsed, or unknown codec.
    // @issue https://github.com/ossrs/srs/issues/1506#issuecomment-562079474
    if (!format_->acodec_) {
        return err;
    }

    // support audio codec: aac/opus
    SrsAudioCodecId acodec = format_->acodec_->id_;
    if (acodec != SrsAudioCodecIdAAC && acodec != SrsAudioCodecIdOpus) {
        return err;
    }

    // Initialize audio track on first packet with actual codec
    if (!audio_initialized_) {
        if ((err = initialize_audio_track(acodec)) != srs_success) {
            return srs_error_wrap(err, "init audio track");
        }
        audio_initialized_ = true;
    }

    // Skip empty audio frames
    if (format_->audio_->nb_samples_ == 0) {
        return err;
    }

    // Convert to RTP packet.
    SrsUniquePtr<SrsRtpPacket> pkt(new SrsRtpPacket());

    if (acodec == SrsAudioCodecIdAAC) {
        if ((err = package_aac(format_->audio_, pkt.get())) != srs_success) {
            return srs_error_wrap(err, "package aac");
        }
    } else {
        return srs_error_new(ERROR_NOT_IMPLEMENTED, "codec %d not implemented", acodec);
    }

    if ((err = rtp_target_->on_rtp(pkt.get())) != srs_success) {
        return srs_error_wrap(err, "consume audio packet");
    }

    return err;
}

srs_error_t SrsRtspRtpBuilder::package_aac(SrsParsedAudioPacket *audio, SrsRtpPacket *pkt)
{
    srs_error_t err = srs_success;

    srs_assert(audio->nb_samples_);

    // For RTSP, audio TBN is not fixed, but use the sample rate, so we
    // need to convert FLV TBN(1000) to the sample rate TBN.
    int64_t dts = (int64_t)audio->dts_;
    dts *= (int64_t)audio_sample_rate_;
    dts /= 1000;

    pkt->header_.set_payload_type(audio_payload_type_);
    pkt->header_.set_ssrc(audio_ssrc_);
    pkt->frame_type_ = SrsFrameTypeAudio;
    pkt->header_.set_marker(true);
    pkt->header_.set_sequence(audio_sequence_++);
    pkt->header_.set_timestamp(dts);

    SrsRtpRawPayload *raw = new SrsRtpRawPayload();
    pkt->set_payload(raw, SrsRtpPacketPayloadTypeRaw);

    // For AAC, we need to package according to RFC 3640 (MPEG-4 Audio)
    // Use AAC-hbr mode with AU-headers
    // Calculate total size for all AU samples
    int total_au_size = 0;
    for (int i = 0; i < audio->nb_samples_; i++) {
        total_au_size += audio->samples_[i].size_;
    }

    // AU-headers: 16 bits per AU (13 bits for size + 3 bits for index)
    int au_headers_length = audio->nb_samples_ * 16;         // bits
    int au_headers_bytes = (au_headers_length + 7) / 8;      // convert to bytes
    int payload_size = 2 + au_headers_bytes + total_au_size; // AU-headers-length(2) + AU-headers + AU data

    // Use SrsBuffer for proper byte marshaling
    SrsUniquePtr<char[]> payload(new char[payload_size]);
    SrsBuffer buffer(payload.get(), payload_size);

    // AU-headers-length (16 bits) - this is the length in BITS, not bytes
    buffer.write_2bytes(au_headers_length);

    // Write AU-headers for each sample
    for (int i = 0; i < audio->nb_samples_; i++) {
        // AU-header: AU-size(13 bits) + AU-index(3 bits) = 16 bits
        // According to RFC 3640, AU-size comes first (MSB), then AU-index (LSB)
        uint16_t au_size = audio->samples_[i].size_ & 0x1FFF; // 13 bits mask
        uint16_t au_index = i & 0x07;                         // 3 bits mask
        buffer.write_2bytes((au_size << 3) | au_index);
    }

    // Copy all AAC AU data
    for (int i = 0; i < audio->nb_samples_; i++) {
        buffer.write_bytes(audio->samples_[i].bytes_, audio->samples_[i].size_);
    }

    // Wrap the payload in the RTP packet
    raw->payload_ = pkt->wrap(payload.get(), payload_size);
    raw->nn_payload_ = payload_size;

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

srs_error_t SrsRtspRtpBuilder::on_video(SrsMediaPacket *msg)
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
    // auto free when exit
    SrsUniquePtr<vector<SrsRtpPacket *> > pkts_ptr(&pkts, free_packets);

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

    if (!pkts.empty()) {
        pkts.back()->header_.set_marker(true);
    }

    return consume_packets(pkts);
}

srs_error_t SrsRtspRtpBuilder::filter(SrsMediaPacket *msg, SrsFormat *format, bool &has_idr, vector<SrsNaluSample *> &samples)
{
    srs_error_t err = srs_success;

    // If IDR, we will insert SPS/PPS before IDR frame.
    if (format->video_ && format->video_->has_idr_) {
        has_idr = true;
    }

    // Update samples to shared frame.
    for (int i = 0; i < format->video_->nb_samples_; ++i) {
        SrsNaluSample *sample = &format->video_->samples_[i];
        samples.push_back(sample);
    }

    return err;
}

srs_error_t SrsRtspRtpBuilder::package_stap_a(SrsMediaPacket *msg, SrsRtpPacket *pkt)
{
    srs_error_t err = srs_success;

    SrsFormat *format = meta_->vsh_format();
    if (!format || !format->vcodec_) {
        return err;
    }

    return video_builder_->package_stap_a(msg, pkt);
}

srs_error_t SrsRtspRtpBuilder::package_nalus(SrsMediaPacket *msg, const vector<SrsNaluSample *> &samples, vector<SrsRtpPacket *> &pkts)
{
    srs_error_t err = srs_success;

    SrsFormat *format = meta_->vsh_format();
    if (!format || !format->vcodec_) {
        return err;
    }

    return video_builder_->package_nalus(msg, samples, pkts);
}

// Single NAL Unit Packet @see https://tools.ietf.org/html/rfc6184#section-5.6
srs_error_t SrsRtspRtpBuilder::package_single_nalu(SrsMediaPacket *msg, SrsNaluSample *sample, vector<SrsRtpPacket *> &pkts)
{
    return video_builder_->package_single_nalu(msg, sample, pkts);
}

srs_error_t SrsRtspRtpBuilder::package_fu_a(SrsMediaPacket *msg, SrsNaluSample *sample, int fu_payload_size, vector<SrsRtpPacket *> &pkts)
{
    srs_error_t err = srs_success;

    SrsFormat *format = meta_->vsh_format();
    if (!format || !format->vcodec_) {
        return err;
    }

    return video_builder_->package_fu_a(msg, sample, fu_payload_size, pkts);
}

srs_error_t SrsRtspRtpBuilder::consume_packets(vector<SrsRtpPacket *> &pkts)
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

ISrsRtspSendTrack::ISrsRtspSendTrack()
{
}

ISrsRtspSendTrack::~ISrsRtspSendTrack()
{
}

SrsRtspSendTrack::SrsRtspSendTrack(ISrsRtspConnection *session, SrsRtcTrackDescription *track_desc, bool is_audio)
{
    session_ = session;
    track_desc_ = track_desc->copy();
}

SrsRtspSendTrack::~SrsRtspSendTrack()
{
    srs_freep(track_desc_);
}

bool SrsRtspSendTrack::has_ssrc(uint32_t ssrc)
{
    return track_desc_->has_ssrc(ssrc);
}

// TODO: FIXME: Should refine logs, set tracks in a time.
bool SrsRtspSendTrack::set_track_status(bool active)
{
    bool previous_status = track_desc_->is_active_;
    track_desc_->is_active_ = active;
    return previous_status;
}

bool SrsRtspSendTrack::get_track_status()
{
    return track_desc_->is_active_;
}

std::string SrsRtspSendTrack::get_track_id()
{
    return track_desc_->id_;
}

SrsRtcTrackDescription *SrsRtspSendTrack::track_desc()
{
    return track_desc_;
}

SrsRtspAudioSendTrack::SrsRtspAudioSendTrack(ISrsRtspConnection *session, SrsRtcTrackDescription *track_desc)
    : SrsRtspSendTrack(session, track_desc, true)
{
}

SrsRtspAudioSendTrack::~SrsRtspAudioSendTrack()
{
}

srs_error_t SrsRtspAudioSendTrack::on_rtp(SrsRtpPacket *pkt)
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

    if ((err = session_->do_send_packet(pkt)) != srs_success) {
        return srs_error_wrap(err, "raw send");
    }

    srs_info("RTSP: Send audio ssrc=%d, seqno=%d, keyframe=%d, ts=%u", pkt->header_.get_ssrc(),
             pkt->header_.get_sequence(), pkt->is_keyframe(), pkt->header_.get_timestamp());

    return err;
}

SrsRtspVideoSendTrack::SrsRtspVideoSendTrack(ISrsRtspConnection *session, SrsRtcTrackDescription *track_desc)
    : SrsRtspSendTrack(session, track_desc, false)
{
}

SrsRtspVideoSendTrack::~SrsRtspVideoSendTrack()
{
}

srs_error_t SrsRtspVideoSendTrack::on_rtp(SrsRtpPacket *pkt)
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

    if ((err = session_->do_send_packet(pkt)) != srs_success) {
        return srs_error_wrap(err, "raw send");
    }

    srs_info("RTSP: Send video ssrc=%d, seqno=%d, keyframe=%d, ts=%u", pkt->header_.get_ssrc(),
             pkt->header_.get_sequence(), pkt->is_keyframe(), pkt->header_.get_timestamp());

    return err;
}
