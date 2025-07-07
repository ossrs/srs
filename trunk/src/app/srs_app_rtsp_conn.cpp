//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_app_rtsp_conn.hpp>

using namespace std;

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>

#include <queue>
#include <sstream>

#include <srs_core_autofree.hpp>
#include <srs_kernel_buffer.hpp>
#include <srs_kernel_rtc_rtp.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_protocol_rtc_stun.hpp>
#include <srs_protocol_rtmp_stack.hpp>
#include <srs_protocol_rtmp_msg_array.hpp>
#include <srs_app_utility.hpp>
#include <srs_app_config.hpp>
#include <srs_app_rtc_queue.hpp>
#include <srs_app_source.hpp>
#include <srs_app_server.hpp>
#include <srs_protocol_utility.hpp>
#include <srs_protocol_http_stack.hpp>
#include <srs_app_http_api.hpp>
#include <srs_app_statistic.hpp>
#include <srs_app_pithy_print.hpp>
#include <srs_protocol_st.hpp>
#include <srs_app_rtc_server.hpp>
#include <srs_app_rtc_source.hpp>
#include <srs_protocol_utility.hpp>
#include <srs_app_threads.hpp>
#include <srs_protocol_log.hpp>
#include <srs_app_log.hpp>
#include <srs_app_http_hooks.hpp>
#include <srs_protocol_kbps.hpp>
#include <srs_kernel_kbps.hpp>
#include <srs_app_rtc_network.hpp>
#include <srs_app_srt_source.hpp>
#include <srs_app_rtsp_source.hpp>
#include <srs_app_rtsp.hpp>
#include <srs_protocol_rtsp_stack.hpp>

extern SrsPps* _srs_pps_snack;
extern SrsPps* _srs_pps_snack2;
extern SrsPps* _srs_pps_snack3;
extern SrsPps* _srs_pps_snack4;

extern SrsPps* _srs_pps_rnack;
extern SrsPps* _srs_pps_rnack2;

extern SrsPps* _srs_pps_pub;
extern SrsPps* _srs_pps_conn;

SrsRtspPlayStream::SrsRtspPlayStream(SrsRtcConnection2* s, const SrsContextId& cid) : source_(new SrsRtspSource())
{
    cid_ = cid;
    trd_ = NULL;

    req_ = NULL;

    is_started = false;
    session_ = s;

    cache_ssrc0_ = cache_ssrc1_ = cache_ssrc2_ = 0;
    cache_track0_ = cache_track1_ = cache_track2_ = NULL;
}

SrsRtspPlayStream::~SrsRtspPlayStream()
{
    srs_freep(trd_);
    srs_freep(req_);

    if (true) {
        std::map<uint32_t, SrsRtspAudioSendTrack*>::iterator it;
        for (it = audio_tracks_.begin(); it != audio_tracks_.end(); ++it) {
            srs_freep(it->second);
        }
    }

    if (true) {
        std::map<uint32_t, SrsRtspVideoSendTrack*>::iterator it;
        for (it = video_tracks_.begin(); it != video_tracks_.end(); ++it) {
            srs_freep(it->second);
        }
    }
	
    // update the statistic when client coveried.
    SrsStatistic* stat = SrsStatistic::instance();
    // TODO: FIXME: Should finger out the err.
    stat->on_disconnect(cid_.c_str(), srs_success);
}

srs_error_t SrsRtspPlayStream::initialize(SrsRequest* req, std::map<uint32_t, SrsRtcTrackDescription*> sub_relations)
{
    srs_error_t err = srs_success;

    req_ = req->copy();

    // We must do stat the client before hooks, because hooks depends on it.
    SrsStatistic* stat = SrsStatistic::instance();
    if ((err = stat->on_client(cid_.c_str(), req_, session_, SrsRtcConnPlay)) != srs_success) {
        return srs_error_wrap(err, "RTSP: stat client");
    }

    if ((err = _srs_rtsp_sources->fetch_or_create(req_, source_)) != srs_success) {
        return srs_error_wrap(err, "RTSP: fetch source failed");
    }

    for (map<uint32_t, SrsRtcTrackDescription*>::iterator it = sub_relations.begin(); it != sub_relations.end(); ++it) {
        uint32_t ssrc = it->first;
        SrsRtcTrackDescription* desc = it->second;

        if (desc->type_ == "audio") {
            SrsRtspAudioSendTrack* track = new SrsRtspAudioSendTrack(session_, desc);
            audio_tracks_.insert(make_pair(ssrc, track));
        }

        if (desc->type_ == "video") {
            SrsRtspVideoSendTrack* track = new SrsRtspVideoSendTrack(session_, desc);
            video_tracks_.insert(make_pair(ssrc, track));
        }
    }

    return err;
}

// TODO: Remove it for RTSP?
void SrsRtspPlayStream::on_stream_change(SrsRtcSourceDescription* desc)
{
    if (!desc) return;

    // Refresh the relation for audio.
    // TODO: FIXME: Match by label?
    if (desc && desc->audio_track_desc_ && audio_tracks_.size() == 1) {
        if (!audio_tracks_.empty()) {
            uint32_t ssrc = desc->audio_track_desc_->ssrc_;
            SrsRtspAudioSendTrack* track = audio_tracks_.begin()->second;

            if (track->track_desc_->media_->pt_of_publisher_ != desc->audio_track_desc_->media_->pt_) {
                track->track_desc_->media_->pt_of_publisher_ = desc->audio_track_desc_->media_->pt_;
            }

            if (desc->audio_track_desc_->red_ && track->track_desc_->red_ && 
                    track->track_desc_->red_->pt_of_publisher_ != desc->audio_track_desc_->red_->pt_) {
                track->track_desc_->red_->pt_of_publisher_ = desc->audio_track_desc_->red_->pt_;
            }

            audio_tracks_.clear();
            audio_tracks_.insert(make_pair(ssrc, track));
        }
    }

    // Refresh the relation for video.
    // TODO: FIMXE: Match by label?
    if (desc && desc->video_track_descs_.size() == 1) {
        if (!video_tracks_.empty()) {
            SrsRtcTrackDescription* vdesc = desc->video_track_descs_.at(0);
            uint32_t ssrc = vdesc->ssrc_;
            SrsRtspVideoSendTrack* track = video_tracks_.begin()->second;

            if (track->track_desc_->media_->pt_of_publisher_ != vdesc->media_->pt_) {
                track->track_desc_->media_->pt_of_publisher_ = vdesc->media_->pt_;
            }

            if (vdesc->red_ && track->track_desc_->red_ && 
                    track->track_desc_->red_->pt_of_publisher_ != vdesc->red_->pt_) {
                track->track_desc_->red_->pt_of_publisher_ = vdesc->red_->pt_;
            }

            video_tracks_.clear();
            video_tracks_.insert(make_pair(ssrc, track));
        }
    }
}

const SrsContextId& SrsRtspPlayStream::context_id()
{
    return cid_;
}

srs_error_t SrsRtspPlayStream::start()
{
    srs_error_t err = srs_success;

    // If player coroutine allocated, we think the player is started.
    // To prevent play multiple times for this play stream.
    // @remark Allow start multiple times, for DTLS may retransmit the final packet.
    if (is_started) {
        return err;
    }

    srs_freep(trd_);
    trd_ = new SrsFastCoroutine("rtsp_sender", this, cid_);

    if ((err = trd_->start()) != srs_success) {
        return srs_error_wrap(err, "rtsp_sender");
    }

    is_started = true;

    return err;
}

void SrsRtspPlayStream::stop()
{
    if (trd_) {
        trd_->stop();
    }
}

srs_error_t SrsRtspPlayStream::cycle()
{
    srs_error_t err = srs_success;

    SrsSharedPtr<SrsRtspSource>& source = source_;
    srs_assert(source.get());

    SrsRtspConsumer* consumer_raw = NULL;
    if ((err = source->create_consumer(consumer_raw)) != srs_success) {
        return srs_error_wrap(err, "create consumer, source=%s", req_->get_stream_url().c_str());
    }

    srs_assert(consumer_raw);
    SrsUniquePtr<SrsRtspConsumer> consumer(consumer_raw);

    consumer->set_handler(this);

    // TODO: FIXME: Dumps the SPS/PPS from gop cache, without other frames.
    if ((err = source->consumer_dumps(consumer.get())) != srs_success) {
        return srs_error_wrap(err, "dumps consumer, url=%s", req_->get_stream_url().c_str());
    }

    // TODO: FIXME: Add cost in ms.
    SrsContextId cid = source->source_id();
    srs_trace("RTSP: start play url=%s, source_id=%s/%s", req_->get_stream_url().c_str(),
        cid.c_str(), source->pre_source_id().c_str());

    SrsUniquePtr<SrsErrorPithyPrint> epp(new SrsErrorPithyPrint());

    // For RTSP, donot use merged write.
    int mw_msgs = 1;
    while (true) {
        if ((err = trd_->pull()) != srs_success) {
            return srs_error_wrap(err, "RTSP sender thread");
        }

        // Wait for amount of packets.
        SrsRtpPacket* pkt = NULL;
        consumer->dump_packet(&pkt);
        if (!pkt) {
            // TODO: FIXME: We should check the quit event.
            consumer->wait(mw_msgs);
            continue;
        }

        // Send-out the RTP packet and do cleanup
        // @remark Note that the pkt might be set to NULL.
        if ((err = send_packet(pkt)) != srs_success) {
            uint32_t nn = 0;
            if (epp->can_print(err, &nn)) {
                srs_warn("play send packets=%u, nn=%u/%u, err: %s", 1, epp->nn_count, nn, srs_error_desc(err).c_str());
            }
            srs_freep(err);
        }

        // Free the packet.
        // @remark Note that the pkt might be set to NULL.
        srs_freep(pkt);
    }
}

srs_error_t SrsRtspPlayStream::send_packet(SrsRtpPacket*& pkt)
{
    srs_error_t err = srs_success;

    uint32_t ssrc = pkt->header.get_ssrc();

    // Try to find track from cache.
    SrsRtspSendTrack* track = NULL;
    if (cache_ssrc0_ == ssrc) {
        track = cache_track0_;
    } else if (cache_ssrc1_ == ssrc) {
        track = cache_track1_;
    } else if (cache_ssrc2_ == ssrc) {
        track = cache_track2_;
    }

    // Find by original tracks and build fast cache.
    if (!track) {
        if (pkt->is_audio()) {
            map<uint32_t, SrsRtspAudioSendTrack*>::iterator it = audio_tracks_.find(ssrc);
            if (it != audio_tracks_.end()) {
                track = it->second;
            }
        } else {
            map<uint32_t, SrsRtspVideoSendTrack*>::iterator it = video_tracks_.find(ssrc);
            if (it != video_tracks_.end()) {
                track = it->second;
            }
        }

        if (track && !cache_ssrc2_) {
            if (!cache_ssrc0_) {
                cache_ssrc0_ = ssrc;
                cache_track0_ = track;
            } else if (!cache_ssrc1_) {
                cache_ssrc1_ = ssrc;
                cache_track1_ = track;
            } else if (!cache_ssrc2_) {
                cache_ssrc2_ = ssrc;
                cache_track2_ = track;
            }
        }
    }

    // Ignore if no track found.
    if (!track) {
        srs_warn("RTSP: Drop for ssrc %u not found", ssrc);
        return err;
    }

    // Consume packet by track.
    if ((err = track->on_rtp(pkt)) != srs_success) {
        return srs_error_wrap(err, "audio track, SSRC=%u, SEQ=%u", ssrc, pkt->header.get_sequence());
    }

    return err;
}

void SrsRtspPlayStream::set_all_tracks_status(bool status)
{
    std::ostringstream merged_log;

    // set video track status
    if (true) {
        std::map<uint32_t, SrsRtspVideoSendTrack*>::iterator it;
        for (it = video_tracks_.begin(); it != video_tracks_.end(); ++it) {
            SrsRtspVideoSendTrack* track = it->second;

            bool previous = track->set_track_status(status);
            merged_log << "{track: " << track->get_track_id() << ", is_active: " << previous << "=>" << status << "},";
        }
    }

    // set audio track status
    if (true) {
        std::map<uint32_t, SrsRtspAudioSendTrack*>::iterator it;
        for (it = audio_tracks_.begin(); it != audio_tracks_.end(); ++it) {
            SrsRtspAudioSendTrack* track = it->second;

            bool previous = track->set_track_status(status);
            merged_log << "{track: " << track->get_track_id() << ", is_active: " << previous << "=>" << status << "},";
        }
    }

    srs_trace("RTSP: Init tracks %s ok", merged_log.str().c_str());
}

SrsRtcConnection2::SrsRtcConnection2(const SrsContextId& cid)
{
    cid_ = cid;

    last_stun_time = 0;
    session_timeout = 0;
    disposing_ = false;

    _srs_rtsp_manager->subscribe(this);
}

SrsRtcConnection2::~SrsRtcConnection2()
{
    _srs_rtsp_manager->unsubscribe(this);
}

void SrsRtcConnection2::on_before_dispose(ISrsResource* c)
{
    if (disposing_) {
        return;
    }

    SrsRtcConnection2* session = dynamic_cast<SrsRtcConnection2*>(c);
    if (session == this) {
        disposing_ = true;
    }

    if (session && session == this) {
        _srs_context->set_id(cid_);
        srs_trace("RTSP: session detach from [%s](%s), disposing=%d", c->get_id().c_str(),
            c->desc().c_str(), disposing_);
    }
}

void SrsRtcConnection2::on_disposing(ISrsResource* c)
{
    if (disposing_) {
        return;
    }
}

const SrsContextId& SrsRtcConnection2::get_id()
{
    return cid_;
}

std::string SrsRtcConnection2::desc()
{
    return "RtspConn";
}

void SrsRtcConnection2::expire()
{
    // TODO: FIXME: Should set session to expired and remove it by heartbeat checking. Should not remove it directly.
    _srs_rtsp_manager->remove(this);
}

void SrsRtcConnection2::switch_to_context()
{
    _srs_context->set_id(cid_);
}

const SrsContextId& SrsRtcConnection2::context_id()
{
    return cid_;
}

bool SrsRtcConnection2::is_alive()
{
    return last_stun_time + session_timeout > srs_get_system_time();
}

void SrsRtcConnection2::alive()
{
    last_stun_time = srs_get_system_time();
}

