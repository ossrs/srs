//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_app_rtsp_source.hpp>

#ifdef SRS_RTC

#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_app_config.hpp>
#include <srs_app_pithy_print.hpp>
#include <srs_core_autofree.hpp>
#include <srs_app_statistic.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_app_rtc_source.hpp>
#include <srs_app_stream_bridge.hpp>
#include <srs_protocol_utility.hpp>
#include <srs_app_hybrid.hpp>
#include <srs_app_threads.hpp>

using namespace std;

extern SrsPps* _srs_pps_aloss2;

// the time to cleanup source.
#define SRS_RTSP_SOURCE_CLEANUP (3 * SRS_UTIME_SECONDS)

SrsRtspConsumer::SrsRtspConsumer(SrsRtspSource* s)
{
    source_ = s;
    should_update_source_id = false;
    handler_ = NULL;

    mw_wait = srs_cond_new();
    mw_min_msgs = 0;
    mw_waiting = false;
}

SrsRtspConsumer::~SrsRtspConsumer()
{
    source_->on_consumer_destroy(this);

    vector<SrsRtpPacket*>::iterator it;
    for (it = queue.begin(); it != queue.end(); ++it) {
        SrsRtpPacket* pkt = *it;
        srs_freep(pkt);
    }

    srs_cond_destroy(mw_wait);
}

void SrsRtspConsumer::update_source_id()
{
    should_update_source_id = true;
}

srs_error_t SrsRtspConsumer::enqueue(SrsRtpPacket* pkt)
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

srs_error_t SrsRtspConsumer::dump_packet(SrsRtpPacket** ppkt)
{
    srs_error_t err = srs_success;

    // TODO: FIXME: Refine performance by ring buffer.
    if (!queue.empty()) {
        *ppkt = queue.front();
        queue.erase(queue.begin());
    }
    
    return err;
}

void SrsRtspConsumer::wait(int nb_msgs)
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

void SrsRtspConsumer::on_stream_change(SrsRtcSourceDescription* desc)
{
    if (handler_) {
        handler_->on_stream_change(desc);
    }
}

SrsRtspSourceManager::SrsRtspSourceManager()
{
    lock = srs_mutex_new();
    timer_ = new SrsHourGlass("sources", this, 1 * SRS_UTIME_SECONDS);
}

SrsRtspSourceManager::~SrsRtspSourceManager()
{
    srs_mutex_destroy(lock);
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

    std::map< std::string, SrsSharedPtr<SrsRtspSource> >::iterator it;
    for (it = pool.begin(); it != pool.end();) {
        SrsSharedPtr<SrsRtspSource>& source = it->second;

        // When source expired, remove it.
        // @see https://github.com/ossrs/srs/issues/713
        if (source->stream_is_dead()) {
            SrsContextId cid = source->source_id();
            if (cid.empty()) cid = source->pre_source_id();
            srs_trace("RTC: cleanup die source, id=[%s], total=%d", cid.c_str(), (int)pool.size());
            pool.erase(it++);
        } else {
            ++it;
        }
    }

    return err;
}

srs_error_t SrsRtspSourceManager::fetch_or_create(SrsRequest* r, SrsSharedPtr<SrsRtspSource>& pps)
{
    srs_error_t err = srs_success;

    // Use lock to protect coroutine switch.
    // @bug https://github.com/ossrs/srs/issues/1230
    SrsLocker(lock);

    string stream_url = r->get_stream_url();
    std::map< std::string, SrsSharedPtr<SrsRtspSource> >::iterator it = pool.find(stream_url);

    if (it != pool.end()) {
        SrsSharedPtr<SrsRtspSource> source = it->second;

        // we always update the request of resource,
        // for origin auth is on, the token in request maybe invalid,
        // and we only need to update the token of request, it's simple.
        source->update_auth(r);
        pps = source;

        return err;
    }

    SrsSharedPtr<SrsRtspSource> source = SrsSharedPtr<SrsRtspSource>(new SrsRtspSource());
    srs_trace("new rtc source, stream_url=%s", stream_url.c_str());

    if ((err = source->initialize(r)) != srs_success) {
        return srs_error_wrap(err, "init source %s", r->get_stream_url().c_str());
    }

    pool[stream_url] = source;
    pps = source;

    return err;
}

SrsSharedPtr<SrsRtspSource> SrsRtspSourceManager::fetch(SrsRequest* r)
{
    // Use lock to protect coroutine switch.
    // @bug https://github.com/ossrs/srs/issues/1230
    SrsLocker(lock);

    string stream_url = r->get_stream_url();
    std::map< std::string, SrsSharedPtr<SrsRtspSource> >::iterator it = pool.find(stream_url);

    SrsSharedPtr<SrsRtspSource> source;
    if (it == pool.end()) {
        return source;
    }

    source = it->second;
    return source;
}

SrsRtspSourceManager* _srs_rtsp_sources = NULL;

SrsRtspSource::SrsRtspSource()
{
    is_created_ = false;
    is_delivering_packets_ = false;

    stream_desc_ = NULL;

    req = NULL;
    bridge_ = NULL;

    stream_die_at_ = 0;
}

SrsRtspSource::~SrsRtspSource()
{
    // never free the consumers,
    // for all consumers are auto free.
    consumers.clear();

    srs_freep(bridge_);
    srs_freep(req);
    srs_freep(stream_desc_);

    SrsContextId cid = _source_id;
    if (cid.empty()) cid = _pre_source_id;
    srs_trace("free rtc source id=[%s]", cid.c_str());
}

srs_error_t SrsRtspSource::initialize(SrsRequest* r)
{
    srs_error_t err = srs_success;

    req = r->copy();

	// Create default relations to allow play before publishing.
	// @see https://github.com/ossrs/srs/issues/2362
	init_for_play_before_publishing();

	return err;
}

bool SrsRtspSource::stream_is_dead()
{
    // still publishing?
    if (is_created_) {
        return false;
    }

    // has any consumers?
    if (!consumers.empty()) {
        return false;
    }

    // Delay cleanup source.
    srs_utime_t now = srs_get_system_time();
    if (now < stream_die_at_ + SRS_RTSP_SOURCE_CLEANUP) {
        return false;
    }

    return true;
}

void SrsRtspSource::init_for_play_before_publishing()
{
    // If the stream description has already been setup by RTC publisher,
    // we should ignore and it's ok, because we only need to setup it for bridge.
    if (stream_desc_) {
        return;
    }

    SrsUniquePtr<SrsRtcSourceDescription> stream_desc(new SrsRtcSourceDescription());

    // audio track description
    if (true) {
        SrsRtcTrackDescription* audio_track_desc = new SrsRtcTrackDescription();
        stream_desc->audio_track_desc_ = audio_track_desc;

        audio_track_desc->type_ = "audio";
        audio_track_desc->id_ = "audio-" + srs_random_str(8);

        uint32_t audio_ssrc = SrsRtcSSRCGenerator::instance()->generate_ssrc();
        audio_track_desc->ssrc_ = audio_ssrc;
        audio_track_desc->direction_ = "sendonly";

        audio_track_desc->media_ = new SrsAudioPayload(kAudioPayloadType, "opus", kAudioSamplerate, kAudioChannel);
    }

    // video track descriptions - support both H.264 and H.265 for play before publishing
    // This allows clients to choose their preferred codec during SDP negotiation
    if (true) {
        // H.264 track description
        SrsRtcTrackDescription* h264_track_desc = new SrsRtcTrackDescription();
        stream_desc->video_track_descs_.push_back(h264_track_desc);

        h264_track_desc->type_ = "video";
        h264_track_desc->id_ = "video-h264-" + srs_random_str(8);

        uint32_t h264_ssrc = SrsRtcSSRCGenerator::instance()->generate_ssrc();
        h264_track_desc->ssrc_ = h264_ssrc;
        h264_track_desc->direction_ = "sendonly";

        SrsVideoPayload* h264_payload = new SrsVideoPayload(kVideoPayloadType, "H264", kVideoSamplerate);
        h264_track_desc->media_ = h264_payload;

        h264_payload->set_h264_param_desc("level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42e01f");
    }

    if (true) {
        // H.265 track description
        SrsRtcTrackDescription* h265_track_desc = new SrsRtcTrackDescription();
        stream_desc->video_track_descs_.push_back(h265_track_desc);

        h265_track_desc->type_ = "video";
        h265_track_desc->id_ = "video-h265-" + srs_random_str(8);

        uint32_t h265_ssrc = SrsRtcSSRCGenerator::instance()->generate_ssrc();
        h265_track_desc->ssrc_ = h265_ssrc;
        h265_track_desc->direction_ = "sendonly";

        SrsVideoPayload* h265_payload = new SrsVideoPayload(KVideoPayloadTypeHevc, "H265", kVideoSamplerate);
        h265_track_desc->media_ = h265_payload;

        h265_payload->set_h265_param_desc("level-id=156;profile-id=1;tier-flag=0;tx-mode=SRST");
    }

    set_stream_desc(stream_desc.get());
}

void SrsRtspSource::update_auth(SrsRequest* r)
{
    req->update_auth(r);
}

srs_error_t SrsRtspSource::on_source_changed()
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
    std::vector<SrsRtspConsumer*>::iterator it;
    for (it = consumers.begin(); it != consumers.end(); ++it) {
        SrsRtspConsumer* consumer = *it;

        // Notify if context id changed.
        if (id_changed) {
            consumer->update_source_id();
        }

        // Notify about stream description.
        consumer->on_stream_change(stream_desc_);
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

void SrsRtspSource::set_bridge(ISrsStreamBridge* bridge)
{
    srs_freep(bridge_);
    bridge_ = bridge;
}

srs_error_t SrsRtspSource::create_consumer(SrsRtspConsumer*& consumer)
{
    srs_error_t err = srs_success;

    consumer = new SrsRtspConsumer(this);
    consumers.push_back(consumer);

    stream_die_at_ = 0;

    // TODO: FIXME: Implements edge cluster.

    return err;
}

srs_error_t SrsRtspSource::consumer_dumps(SrsRtspConsumer* consumer, bool ds, bool dm, bool dg)
{
    srs_error_t err = srs_success;

    // print status.
    srs_trace("create rtsp consumer, no gop cache");

    return err;
}

void SrsRtspSource::on_consumer_destroy(SrsRtspConsumer* consumer)
{
    std::vector<SrsRtspConsumer*>::iterator it;
    it = std::find(consumers.begin(), consumers.end(), consumer);
    if (it != consumers.end()) {
        it = consumers.erase(it);
    }

    // TODO: When all consumers finished, notify publisher to handle it.

    // Destroy and cleanup source when no publishers and consumers.
    if (!is_created_ && consumers.empty()) {
        stream_die_at_ = srs_get_system_time();
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
    }

    SrsStatistic* stat = SrsStatistic::instance();
    stat->on_stream_publish(req, _source_id.c_str());

    return err;
}

void SrsRtspSource::on_unpublish()
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

    //free bridge resource
    if (bridge_) {
        bridge_->on_unpublish();
        srs_freep(bridge_);
    }

    SrsStatistic* stat = SrsStatistic::instance();
    stat->on_stream_close(req);

    // Destroy and cleanup source when no publishers and consumers.
    if (consumers.empty()) {
        stream_die_at_ = srs_get_system_time();
    }
}

srs_error_t SrsRtspSource::on_rtp(SrsRtpPacket* pkt)
{
    srs_error_t err = srs_success;

    // If circuit-breaker is dying, drop packet.
    if (_srs_circuit_breaker->hybrid_dying_water_level()) {
        _srs_pps_aloss2->sugar += (int64_t)consumers.size();
        return err;
    }

    for (int i = 0; i < (int)consumers.size(); i++) {
        SrsRtspConsumer* consumer = consumers.at(i);
        if ((err = consumer->enqueue(pkt->copy())) != srs_success) {
            return srs_error_wrap(err, "consume message");
        }
    }

    return err;
}

bool SrsRtspSource::has_stream_desc()
{
    return stream_desc_;
}

void SrsRtspSource::set_stream_desc(SrsRtcSourceDescription* stream_desc)
{
    srs_freep(stream_desc_);

    if (stream_desc) {
        stream_desc_ = stream_desc->copy();
    }
}

std::vector<SrsRtcTrackDescription*> SrsRtspSource::get_track_desc(std::string type, std::string media_name)
{
    std::vector<SrsRtcTrackDescription*> track_descs;
    if (!stream_desc_) {
        return track_descs;
    }

    if (type == "audio") {
        if (! stream_desc_->audio_track_desc_) {
            return track_descs;
        }

        SrsAudioCodecId codec = SrsAudioCodecId(stream_desc_->audio_track_desc_->media_->codec(false));
        if (codec == srs_audio_codec_str2id(media_name)) {
            track_descs.push_back(stream_desc_->audio_track_desc_);
        }
    }

    if (type == "video") {
        std::vector<SrsRtcTrackDescription*>::iterator it = stream_desc_->video_track_descs_.begin();
        for (; it != stream_desc_->video_track_descs_.end(); ++it){
            SrsRtcTrackDescription* track_desc = *it;
            
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

#endif
