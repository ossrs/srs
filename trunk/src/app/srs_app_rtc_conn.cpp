//
// Copyright (c) 2013-2021 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_app_rtc_conn.hpp>

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
#include <srs_rtc_stun_stack.hpp>
#include <srs_rtmp_stack.hpp>
#include <srs_rtmp_msg_array.hpp>
#include <srs_app_utility.hpp>
#include <srs_app_config.hpp>
#include <srs_app_rtc_queue.hpp>
#include <srs_app_source.hpp>
#include <srs_app_server.hpp>
#include <srs_service_utility.hpp>
#include <srs_http_stack.hpp>
#include <srs_app_http_api.hpp>
#include <srs_app_statistic.hpp>
#include <srs_app_pithy_print.hpp>
#include <srs_service_st.hpp>
#include <srs_app_rtc_server.hpp>
#include <srs_app_rtc_source.hpp>
#include <srs_protocol_utility.hpp>
#include <srs_app_threads.hpp>
#include <srs_service_log.hpp>
#include <srs_app_log.hpp>
#include <srs_app_http_hooks.hpp>
#include <srs_protocol_kbps.hpp>

SrsPps* _srs_pps_sstuns = NULL;
SrsPps* _srs_pps_srtcps = NULL;
SrsPps* _srs_pps_srtps = NULL;

SrsPps* _srs_pps_pli = NULL;
SrsPps* _srs_pps_twcc = NULL;
SrsPps* _srs_pps_rr = NULL;

extern SrsPps* _srs_pps_snack;
extern SrsPps* _srs_pps_snack2;
extern SrsPps* _srs_pps_snack3;
extern SrsPps* _srs_pps_snack4;

extern SrsPps* _srs_pps_rnack;
extern SrsPps* _srs_pps_rnack2;

extern SrsPps* _srs_pps_pub;
extern SrsPps* _srs_pps_conn;

ISrsRtcTransport::ISrsRtcTransport()
{
}

ISrsRtcTransport::~ISrsRtcTransport()
{
}

SrsSecurityTransport::SrsSecurityTransport(SrsRtcConnection* s)
{
    session_ = s;

    dtls_ = new SrsDtls((ISrsDtlsCallback*)this);
    srtp_ = new SrsSRTP();

    handshake_done = false;
}

SrsSecurityTransport::~SrsSecurityTransport()
{
    srs_freep(dtls_);
    srs_freep(srtp_);
}

srs_error_t SrsSecurityTransport::initialize(SrsSessionConfig* cfg)
{
    return dtls_->initialize(cfg->dtls_role, cfg->dtls_version);
}

srs_error_t SrsSecurityTransport::start_active_handshake()
{
    return dtls_->start_active_handshake();
}

srs_error_t SrsSecurityTransport::write_dtls_data(void* data, int size) 
{
    srs_error_t err = srs_success;

    if (!size) {
        return err;
    }

    ++_srs_pps_sstuns->sugar;

    if ((err = session_->sendonly_skt->sendto(data, size, 0)) != srs_success) {
        return srs_error_wrap(err, "send dtls packet");
    }

    if (_srs_blackhole->blackhole) {
        _srs_blackhole->sendto(data, size);
    }

    return err;
}

srs_error_t SrsSecurityTransport::on_dtls(char* data, int nb_data)
{
    return dtls_->on_dtls(data, nb_data);
}

srs_error_t SrsSecurityTransport::on_dtls_alert(std::string type, std::string desc)
{
    return session_->on_dtls_alert(type, desc);
}

srs_error_t SrsSecurityTransport::on_dtls_handshake_done()
{
    srs_error_t err = srs_success;

    if (handshake_done) {
        return err;
    }
    handshake_done = true;

    // TODO: FIXME: Add cost for DTLS.
    srs_trace("RTC: DTLS handshake done.");

    if ((err = srtp_initialize()) != srs_success) {
        return srs_error_wrap(err, "srtp init");
    }

    return session_->on_connection_established();
}

srs_error_t SrsSecurityTransport::on_dtls_application_data(const char* buf, const int nb_buf)
{
    srs_error_t err = srs_success;

    // TODO: process SCTP protocol(WebRTC datachannel support)

    return err;
}

srs_error_t SrsSecurityTransport::srtp_initialize()
{
    srs_error_t err = srs_success;

    std::string send_key;
    std::string recv_key;

    if ((err = dtls_->get_srtp_key(recv_key, send_key)) != srs_success) {
        return err;
    }
    
    if ((err = srtp_->initialize(recv_key, send_key)) != srs_success) {
        return srs_error_wrap(err, "srtp init");
    }

    return err;
}

srs_error_t SrsSecurityTransport::protect_rtp(void* packet, int* nb_cipher)
{
    return srtp_->protect_rtp(packet, nb_cipher);
}

srs_error_t SrsSecurityTransport::protect_rtcp(void* packet, int* nb_cipher)
{
    return srtp_->protect_rtcp(packet, nb_cipher);
}

srs_error_t SrsSecurityTransport::unprotect_rtp(void* packet, int* nb_plaintext)
{
    return srtp_->unprotect_rtp(packet, nb_plaintext);
}

srs_error_t SrsSecurityTransport::unprotect_rtcp(void* packet, int* nb_plaintext)
{
    return srtp_->unprotect_rtcp(packet, nb_plaintext);
}

SrsSemiSecurityTransport::SrsSemiSecurityTransport(SrsRtcConnection* s) : SrsSecurityTransport(s)
{
}

SrsSemiSecurityTransport::~SrsSemiSecurityTransport()
{
}

srs_error_t SrsSemiSecurityTransport::protect_rtp(void* packet, int* nb_cipher)
{
    return srs_success;
}

srs_error_t SrsSemiSecurityTransport::protect_rtcp(void* packet, int* nb_cipher)
{
    return srs_success;
}

SrsPlaintextTransport::SrsPlaintextTransport(SrsRtcConnection* s)
{
    session_ = s;
}

SrsPlaintextTransport::~SrsPlaintextTransport()
{
}

srs_error_t SrsPlaintextTransport::initialize(SrsSessionConfig* cfg)
{
    return srs_success;
}

srs_error_t SrsPlaintextTransport::start_active_handshake()
{
    return on_dtls_handshake_done();
}

srs_error_t SrsPlaintextTransport::on_dtls(char* data, int nb_data)
{
    return srs_success;
}

srs_error_t SrsPlaintextTransport::on_dtls_alert(std::string type, std::string desc)
{
    return srs_success;
}

srs_error_t SrsPlaintextTransport::on_dtls_handshake_done()
{
    srs_trace("RTC: DTLS handshake done.");
    return session_->on_connection_established();
}

srs_error_t SrsPlaintextTransport::on_dtls_application_data(const char* data, const int len)
{
    return srs_success;
}

srs_error_t SrsPlaintextTransport::write_dtls_data(void* data, int size)
{
    return srs_success;
}

srs_error_t SrsPlaintextTransport::protect_rtp(void* packet, int* nb_cipher)
{
    return srs_success;
}

srs_error_t SrsPlaintextTransport::protect_rtcp(void* packet, int* nb_cipher)
{
    return srs_success;
}

srs_error_t SrsPlaintextTransport::unprotect_rtp(void* packet, int* nb_plaintext)
{
    return srs_success;
}

srs_error_t SrsPlaintextTransport::unprotect_rtcp(void* packet, int* nb_plaintext)
{
    return srs_success;
}

ISrsRtcPLIWorkerHandler::ISrsRtcPLIWorkerHandler()
{
}

ISrsRtcPLIWorkerHandler::~ISrsRtcPLIWorkerHandler()
{
}

SrsRtcPLIWorker::SrsRtcPLIWorker(ISrsRtcPLIWorkerHandler* h)
{
    handler_ = h;
    wait_ = srs_cond_new();
    trd_ = new SrsSTCoroutine("pli", this, _srs_context->get_id());
}

SrsRtcPLIWorker::~SrsRtcPLIWorker()
{
    srs_cond_signal(wait_);
    trd_->stop();

    srs_freep(trd_);
    srs_cond_destroy(wait_);
}

srs_error_t SrsRtcPLIWorker::start()
{
    srs_error_t err = srs_success;

    if ((err = trd_->start()) != srs_success) {
        return srs_error_wrap(err, "start pli worker");
    }

    return err;
}

void SrsRtcPLIWorker::request_keyframe(uint32_t ssrc, SrsContextId cid)
{
    plis_.insert(make_pair(ssrc, cid));
    srs_cond_signal(wait_);
}

srs_error_t SrsRtcPLIWorker::cycle()
{
    srs_error_t err = srs_success;

    while (true) {
        if ((err = trd_->pull()) != srs_success) {
            return srs_error_wrap(err, "quit");
        }

        while (!plis_.empty()) {
            std::map<uint32_t, SrsContextId> plis;
            plis.swap(plis_);

            for (map<uint32_t, SrsContextId>::iterator it = plis.begin(); it != plis.end(); ++it) {
                uint32_t ssrc = it->first;
                SrsContextId cid = it->second;

                ++_srs_pps_pli->sugar;

                if ((err = handler_->do_request_keyframe(ssrc, cid)) != srs_success) {
                    srs_warn("PLI error, %s", srs_error_desc(err).c_str());
                    srs_error_reset(err);
                }
            }
        }
        srs_cond_wait(wait_);
    }

    return err;
}

SrsRtcAsyncCallOnStop::SrsRtcAsyncCallOnStop(SrsContextId c, SrsRequest * r)
{
    cid = c;
    req = r->copy();
}

SrsRtcAsyncCallOnStop::~SrsRtcAsyncCallOnStop()
{
    srs_freep(req);
}

srs_error_t SrsRtcAsyncCallOnStop::call()
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
        SrsConfDirective* conf = _srs_config->get_vhost_on_stop(req->vhost);

        if (!conf) {
            return err;
        }

        hooks = conf->args;
    }

    for (int i = 0; i < (int)hooks.size(); i++) {
        std::string url = hooks.at(i);
        SrsHttpHooks::on_stop(url, req);
    }

    return err;
}

std::string SrsRtcAsyncCallOnStop::to_string()
{
    return std::string("");
}

SrsRtcPlayStream::SrsRtcPlayStream(SrsRtcConnection* s, const SrsContextId& cid)
{
    cid_ = cid;
    trd_ = NULL;

    req_ = NULL;
    source_ = NULL;

    is_started = false;
    session_ = s;

    mw_msgs = 0;
    realtime = true;

    nack_enabled_ = false;
    nack_no_copy_ = false;

    _srs_config->subscribe(this);
    nack_epp = new SrsErrorPithyPrint();
    pli_worker_ = new SrsRtcPLIWorker(this);

    cache_ssrc0_ = cache_ssrc1_ = cache_ssrc2_ = 0;
    cache_track0_ = cache_track1_ = cache_track2_ = NULL;
}

SrsRtcPlayStream::~SrsRtcPlayStream()
{
    if (req_) {
        session_->server_->exec_async_work(new SrsRtcAsyncCallOnStop(cid_, req_));
    }

    // TODO: FIXME: Should not do callback in de-constructor?
    if (_srs_rtc_hijacker) {
        _srs_rtc_hijacker->on_stop_play(session_, this, req_);
    }

    _srs_config->unsubscribe(this);

    srs_freep(nack_epp);
    srs_freep(pli_worker_);
    srs_freep(trd_);
    srs_freep(req_);

    if (true) {
        std::map<uint32_t, SrsRtcAudioSendTrack*>::iterator it;
        for (it = audio_tracks_.begin(); it != audio_tracks_.end(); ++it) {
            srs_freep(it->second);
        }
    }

    if (true) {
        std::map<uint32_t, SrsRtcVideoSendTrack*>::iterator it;
        for (it = video_tracks_.begin(); it != video_tracks_.end(); ++it) {
            srs_freep(it->second);
        }
    }
	
    // update the statistic when client coveried.
    SrsStatistic* stat = SrsStatistic::instance();
    stat->on_disconnect(cid_.c_str());
}

srs_error_t SrsRtcPlayStream::initialize(SrsRequest* req, std::map<uint32_t, SrsRtcTrackDescription*> sub_relations)
{
    srs_error_t err = srs_success;

    req_ = req->copy();

    if ((err = _srs_rtc_sources->fetch_or_create(req_, &source_)) != srs_success) {
        return srs_error_wrap(err, "rtc fetch source failed");
    }

    for (map<uint32_t, SrsRtcTrackDescription*>::iterator it = sub_relations.begin(); it != sub_relations.end(); ++it) {
        uint32_t ssrc = it->first;
        SrsRtcTrackDescription* desc = it->second;

        if (desc->type_ == "audio") {
            SrsRtcAudioSendTrack* track = new SrsRtcAudioSendTrack(session_, desc);
            audio_tracks_.insert(make_pair(ssrc, track));
        }

        if (desc->type_ == "video") {
            SrsRtcVideoSendTrack* track = new SrsRtcVideoSendTrack(session_, desc);
            video_tracks_.insert(make_pair(ssrc, track));
        }
    }

    // TODO: FIXME: Support reload.
    nack_enabled_ = _srs_config->get_rtc_nack_enabled(req->vhost);
    nack_no_copy_ = _srs_config->get_rtc_nack_no_copy(req->vhost);
    srs_trace("RTC player nack=%d, nnc=%d", nack_enabled_, nack_no_copy_);

    // Setup tracks.
    for (map<uint32_t, SrsRtcAudioSendTrack*>::iterator it = audio_tracks_.begin(); it != audio_tracks_.end(); ++it) {
        SrsRtcAudioSendTrack* track = it->second;
        track->set_nack_no_copy(nack_no_copy_);
    }

    for (map<uint32_t, SrsRtcVideoSendTrack*>::iterator it = video_tracks_.begin(); it != video_tracks_.end(); ++it) {
        SrsRtcVideoSendTrack* track = it->second;
        track->set_nack_no_copy(nack_no_copy_);
    }

    return err;
}

void SrsRtcPlayStream::on_stream_change(SrsRtcSourceDescription* desc)
{
    // Refresh the relation for audio.
    // TODO: FIXME: Match by label?
    if (desc && desc->audio_track_desc_ && audio_tracks_.size() == 1) {
        if (! audio_tracks_.empty()) {
            uint32_t ssrc = desc->audio_track_desc_->ssrc_;
            SrsRtcAudioSendTrack* track = audio_tracks_.begin()->second;

            audio_tracks_.clear();
            audio_tracks_.insert(make_pair(ssrc, track));
        }
    }

    // Refresh the relation for video.
    // TODO: FIMXE: Match by label?
    if (desc && desc->video_track_descs_.size() == 1) {
        if (! video_tracks_.empty()) {
            SrsRtcTrackDescription* vdesc = desc->video_track_descs_.at(0);
            uint32_t ssrc = vdesc->ssrc_;
            SrsRtcVideoSendTrack* track = video_tracks_.begin()->second;

            video_tracks_.clear();
            video_tracks_.insert(make_pair(ssrc, track));
        }
    }
}

srs_error_t SrsRtcPlayStream::on_reload_vhost_play(string vhost)
{
    if (req_->vhost != vhost) {
        return srs_success;
    }

    realtime = _srs_config->get_realtime_enabled(req_->vhost, true);
    mw_msgs = _srs_config->get_mw_msgs(req_->vhost, realtime, true);

    srs_trace("Reload play realtime=%d, mw_msgs=%d", realtime, mw_msgs);

    return srs_success;
}

srs_error_t SrsRtcPlayStream::on_reload_vhost_realtime(string vhost)
{
    return on_reload_vhost_play(vhost);
}

const SrsContextId& SrsRtcPlayStream::context_id()
{
    return cid_;
}

srs_error_t SrsRtcPlayStream::start()
{
    srs_error_t err = srs_success;

    // If player coroutine allocated, we think the player is started.
    // To prevent play multiple times for this play stream.
    // @remark Allow start multiple times, for DTLS may retransmit the final packet.
    if (is_started) {
        return err;
    }

    srs_freep(trd_);
    trd_ = new SrsFastCoroutine("rtc_sender", this, cid_);

    if ((err = trd_->start()) != srs_success) {
        return srs_error_wrap(err, "rtc_sender");
    }

    if ((err = pli_worker_->start()) != srs_success) {
        return srs_error_wrap(err, "start pli worker");
    }

    if (_srs_rtc_hijacker) {
        if ((err = _srs_rtc_hijacker->on_start_play(session_, this, req_)) != srs_success) {
            return srs_error_wrap(err, "on start play");
        }
    }
	
    // update the statistic when client discoveried.
    SrsStatistic* stat = SrsStatistic::instance();
    if ((err = stat->on_client(cid_.c_str(), req_, session_, SrsRtcConnPlay)) != srs_success) {
	return srs_error_wrap(err, "rtc: stat client");
    }

    is_started = true;

    return err;
}

void SrsRtcPlayStream::stop()
{
    if (trd_) {
        trd_->stop();
    }
}

srs_error_t SrsRtcPlayStream::cycle()
{
    srs_error_t err = srs_success;

    SrsRtcSource* source = source_;

    SrsRtcConsumer* consumer = NULL;
    SrsAutoFree(SrsRtcConsumer, consumer);
    if ((err = source->create_consumer(consumer)) != srs_success) {
        return srs_error_wrap(err, "create consumer, source=%s", req_->get_stream_url().c_str());
    }

    srs_assert(consumer);
    consumer->set_handler(this);

    // TODO: FIXME: Dumps the SPS/PPS from gop cache, without other frames.
    if ((err = source->consumer_dumps(consumer)) != srs_success) {
        return srs_error_wrap(err, "dumps consumer, url=%s", req_->get_stream_url().c_str());
    }

    realtime = _srs_config->get_realtime_enabled(req_->vhost, true);
    mw_msgs = _srs_config->get_mw_msgs(req_->vhost, realtime, true);

    // TODO: FIXME: Add cost in ms.
    SrsContextId cid = source->source_id();
    srs_trace("RTC: start play url=%s, source_id=%s/%s, realtime=%d, mw_msgs=%d", req_->get_stream_url().c_str(),
        cid.c_str(), source->pre_source_id().c_str(), realtime, mw_msgs);

    SrsErrorPithyPrint* epp = new SrsErrorPithyPrint();
    SrsAutoFree(SrsErrorPithyPrint, epp);

    if (_srs_rtc_hijacker) {
        if ((err = _srs_rtc_hijacker->on_start_consume(session_, this, req_, consumer)) != srs_success) {
            return srs_error_wrap(err, "on start consuming");
        }
    }

    while (true) {
        if ((err = trd_->pull()) != srs_success) {
            return srs_error_wrap(err, "rtc sender thread");
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

srs_error_t SrsRtcPlayStream::send_packet(SrsRtpPacket*& pkt)
{
    srs_error_t err = srs_success;

    uint32_t ssrc = pkt->header.get_ssrc();

    // Try to find track from cache.
    SrsRtcSendTrack* track = NULL;
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
            map<uint32_t, SrsRtcAudioSendTrack*>::iterator it = audio_tracks_.find(ssrc);
            if (it != audio_tracks_.end()) {
                track = it->second;
            }
        } else {
            map<uint32_t, SrsRtcVideoSendTrack*>::iterator it = video_tracks_.find(ssrc);
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
        srs_warn("RTC: Drop for ssrc %u not found", ssrc);
        return err;
    }

    // Consume packet by track.
    if ((err = track->on_rtp(pkt)) != srs_success) {
        return srs_error_wrap(err, "audio track, SSRC=%u, SEQ=%u", ssrc, pkt->header.get_sequence());
    }

    // For NACK to handle packet.
    // @remark Note that the pkt might be set to NULL.
    if (nack_enabled_) {
        if ((err = track->on_nack(&pkt)) != srs_success) {
            return srs_error_wrap(err, "on nack");
        }
    }

    return err;
}

void SrsRtcPlayStream::set_all_tracks_status(bool status)
{
    std::ostringstream merged_log;

    // set video track status
    if (true) {
        std::map<uint32_t, SrsRtcVideoSendTrack*>::iterator it;
        for (it = video_tracks_.begin(); it != video_tracks_.end(); ++it) {
            SrsRtcVideoSendTrack* track = it->second;

            bool previous = track->set_track_status(status);
            merged_log << "{track: " << track->get_track_id() << ", is_active: " << previous << "=>" << status << "},";
        }
    }

    // set audio track status
    if (true) {
        std::map<uint32_t, SrsRtcAudioSendTrack*>::iterator it;
        for (it = audio_tracks_.begin(); it != audio_tracks_.end(); ++it) {
            SrsRtcAudioSendTrack* track = it->second;

            bool previous = track->set_track_status(status);
            merged_log << "{track: " << track->get_track_id() << ", is_active: " << previous << "=>" << status << "},";
        }
    }

    srs_trace("RTC: Init tracks %s ok", merged_log.str().c_str());
}

srs_error_t SrsRtcPlayStream::on_rtcp(SrsRtcpCommon* rtcp)
{
    if(SrsRtcpType_rr == rtcp->type()) {
        SrsRtcpRR* rr = dynamic_cast<SrsRtcpRR*>(rtcp);
        return on_rtcp_rr(rr);
    } else if(SrsRtcpType_rtpfb == rtcp->type()) {
        //currently rtpfb of nack will be handle by player. TWCC will be handled by SrsRtcConnection
        SrsRtcpNack* nack = dynamic_cast<SrsRtcpNack*>(rtcp);
        return on_rtcp_nack(nack);
    } else if(SrsRtcpType_psfb == rtcp->type()) {
        SrsRtcpPsfbCommon* psfb = dynamic_cast<SrsRtcpPsfbCommon*>(rtcp);
        return on_rtcp_ps_feedback(psfb);
    } else if(SrsRtcpType_xr == rtcp->type()) {
        SrsRtcpXr* xr = dynamic_cast<SrsRtcpXr*>(rtcp);
        return on_rtcp_xr(xr);
    } else if(SrsRtcpType_bye == rtcp->type()) {
        // TODO: FIXME: process rtcp bye.
        return srs_success;
    } else {
        return srs_error_new(ERROR_RTC_RTCP_CHECK, "unknown rtcp type=%u", rtcp->type());
    }
}

srs_error_t SrsRtcPlayStream::on_rtcp_rr(SrsRtcpRR* rtcp)
{
    srs_error_t err = srs_success;

    // TODO: FIXME: Implements it.

    return err;
}

srs_error_t SrsRtcPlayStream::on_rtcp_xr(SrsRtcpXr* rtcp)
{
    srs_error_t err = srs_success;

    // TODO: FIXME: Implements it.

    return err;
}

srs_error_t SrsRtcPlayStream::on_rtcp_nack(SrsRtcpNack* rtcp)
{
    srs_error_t err = srs_success;

    ++_srs_pps_rnack->sugar;

    uint32_t ssrc = rtcp->get_media_ssrc();

    // If NACK disabled, print a log.
    if (!nack_enabled_) {
        vector<uint16_t> sns = rtcp->get_lost_sns();
        srs_trace("RTC: NACK ssrc=%u, seq=%s, ignored", ssrc, srs_join_vector_string(sns, ",").c_str());
        return err;
    }

    SrsRtcSendTrack* target = NULL;
    // Try audio track first.
    for (map<uint32_t, SrsRtcAudioSendTrack*>::iterator it = audio_tracks_.begin(); it != audio_tracks_.end(); ++it) {
        SrsRtcAudioSendTrack* track = it->second;
        if (!track->get_track_status() || !track->has_ssrc(ssrc)) {
            continue;
        }

        target = track;
        break;
    }
    // If not found, try video track.
    for (map<uint32_t, SrsRtcVideoSendTrack*>::iterator it = video_tracks_.begin(); !target && it != video_tracks_.end(); ++it) {
        SrsRtcVideoSendTrack* track = it->second;
        if (!track->get_track_status() || !track->has_ssrc(ssrc)) {
            continue;
        }

        target = track;
        break;
    }
    // Error if no track.
    if (!target) {
        return srs_error_new(ERROR_RTC_NO_TRACK, "no track for %u ssrc", ssrc);
    }

    vector<uint16_t> seqs = rtcp->get_lost_sns();
    if((err = target->on_recv_nack(seqs)) != srs_success) {
        return srs_error_wrap(err, "track response nack. id:%s, ssrc=%u", target->get_track_id().c_str(), ssrc);
    }

    return err;
}

srs_error_t SrsRtcPlayStream::on_rtcp_ps_feedback(SrsRtcpPsfbCommon* rtcp)
{
    srs_error_t err = srs_success;

    uint8_t fmt = rtcp->get_rc();
    switch (fmt) {
        case kPLI: {
            uint32_t ssrc = get_video_publish_ssrc(rtcp->get_media_ssrc());
            if (ssrc) {
                pli_worker_->request_keyframe(ssrc, cid_);
            }
            break;
        }
        case kSLI: {
            srs_verbose("sli");
            break;
        }
        case kRPSI: {
            srs_verbose("rpsi");
            break;
        }
        case kAFB: {
            srs_verbose("afb");
            break;
        }
        default: {
            return srs_error_new(ERROR_RTC_RTCP, "unknown payload specific feedback=%u", fmt);
        }
    }

    return err;
}

uint32_t SrsRtcPlayStream::get_video_publish_ssrc(uint32_t play_ssrc)
{
    std::map<uint32_t, SrsRtcVideoSendTrack*>::iterator it;
    for (it = video_tracks_.begin(); it != video_tracks_.end(); ++it) {
        if (it->second->has_ssrc(play_ssrc)) {
            return it->first;
        }
    }

    return 0;
}

srs_error_t SrsRtcPlayStream::do_request_keyframe(uint32_t ssrc, SrsContextId cid)
{
    srs_error_t err = srs_success;

    // The source MUST exists, when PLI thread is running.
    srs_assert(source_);

    ISrsRtcPublishStream* publisher = source_->publish_stream();
    if (!publisher) {
        return err;
    }

    publisher->request_keyframe(ssrc);

    return err;
}

SrsRtcPublishRtcpTimer::SrsRtcPublishRtcpTimer(SrsRtcPublishStream* p) : p_(p)
{
    _srs_hybrid->timer1s()->subscribe(this);
}

SrsRtcPublishRtcpTimer::~SrsRtcPublishRtcpTimer()
{
    _srs_hybrid->timer1s()->unsubscribe(this);
}

srs_error_t SrsRtcPublishRtcpTimer::on_timer(srs_utime_t interval)
{
    srs_error_t err = srs_success;

    ++_srs_pps_pub->sugar;

    if (!p_->is_started) {
        return err;
    }

    // For RR and RRTR.
    ++_srs_pps_rr->sugar;

    if ((err = p_->send_rtcp_rr()) != srs_success) {
        srs_warn("RR err %s", srs_error_desc(err).c_str());
        srs_freep(err);
    }

    if ((err = p_->send_rtcp_xr_rrtr()) != srs_success) {
        srs_warn("XR err %s", srs_error_desc(err).c_str());
        srs_freep(err);
    }

    return err;
}

SrsRtcPublishTwccTimer::SrsRtcPublishTwccTimer(SrsRtcPublishStream* p) : p_(p)
{
    _srs_hybrid->timer100ms()->subscribe(this);
}

SrsRtcPublishTwccTimer::~SrsRtcPublishTwccTimer()
{
    _srs_hybrid->timer100ms()->unsubscribe(this);
}

srs_error_t SrsRtcPublishTwccTimer::on_timer(srs_utime_t interval)
{
    srs_error_t err = srs_success;

    ++_srs_pps_pub->sugar;

    if (!p_->is_started) {
        return err;
    }

    // For TWCC feedback.
    if (!p_->twcc_enabled_) {
        return err;
    }

    ++_srs_pps_twcc->sugar;

    // If circuit-breaker is dropping packet, disable TWCC.
    if (_srs_circuit_breaker->hybrid_critical_water_level()) {
        ++_srs_pps_snack4->sugar;
        return err;
    }

    // We should not depends on the received packet,
    // instead we should send feedback every Nms.
    if ((err = p_->send_periodic_twcc()) != srs_success) {
        srs_warn("TWCC err %s", srs_error_desc(err).c_str());
        srs_freep(err);
    }

    return err;
}


SrsRtcAsyncCallOnUnpublish::SrsRtcAsyncCallOnUnpublish(SrsContextId c, SrsRequest * r)
{
    cid = c;
    req = r->copy();
}

SrsRtcAsyncCallOnUnpublish::~SrsRtcAsyncCallOnUnpublish()
{
    srs_freep(req);
}

srs_error_t SrsRtcAsyncCallOnUnpublish::call()
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
        SrsConfDirective* conf = _srs_config->get_vhost_on_unpublish(req->vhost);

        if (!conf) {
            return err;
        }

        hooks = conf->args;
    }

    for (int i = 0; i < (int)hooks.size(); i++) {
        std::string url = hooks.at(i);
        SrsHttpHooks::on_unpublish(url, req);
    }

    return err;
}

std::string SrsRtcAsyncCallOnUnpublish::to_string()
{
    return std::string("");
}

SrsRtcPublishStream::SrsRtcPublishStream(SrsRtcConnection* session, const SrsContextId& cid)
{
    cid_ = cid;
    is_started = false;
    session_ = session;
    request_keyframe_ = false;
    pli_epp = new SrsErrorPithyPrint();
    twcc_epp_ = new SrsErrorPithyPrint(3.0);

    req_ = NULL;
    source = NULL;
    nn_simulate_nack_drop = 0;
    nack_enabled_ = false;
    nack_no_copy_ = false;
    pt_to_drop_ = 0;

    nn_audio_frames = 0;
    twcc_enabled_ = false;
    twcc_id_ = 0;
    twcc_fb_count_ = 0;
    
    pli_worker_ = new SrsRtcPLIWorker(this);
    last_time_send_twcc_ = 0;

    timer_rtcp_ = new SrsRtcPublishRtcpTimer(this);
    timer_twcc_ = new SrsRtcPublishTwccTimer(this);
}

SrsRtcPublishStream::~SrsRtcPublishStream()
{
    if (req_) {
        session_->server_->exec_async_work(new SrsRtcAsyncCallOnUnpublish(cid_, req_));
    }

    srs_freep(timer_rtcp_);
    srs_freep(timer_twcc_);

    // TODO: FIXME: Should remove and delete source.
    if (source) {
        source->set_publish_stream(NULL);
        source->on_unpublish();
    }

    // TODO: FIXME: Should not do callback in de-constructor?
    // NOTE: on_stop_publish lead to switch io,
    // it must be called after source stream unpublish (set source stream is_created=false).
    // if not, it lead to republish failed.
    if (_srs_rtc_hijacker) {
        _srs_rtc_hijacker->on_stop_publish(session_, this, req_);
    }

    for (int i = 0; i < (int)video_tracks_.size(); ++i) {
        SrsRtcVideoRecvTrack* track = video_tracks_.at(i);
        srs_freep(track);
    }
    video_tracks_.clear();

    for (int i = 0; i < (int)audio_tracks_.size(); ++i) {
        SrsRtcAudioRecvTrack* track = audio_tracks_.at(i);
        srs_freep(track);
    }
    audio_tracks_.clear();

    srs_freep(pli_worker_);
    srs_freep(twcc_epp_);
    srs_freep(pli_epp);
    srs_freep(req_);
	
    // update the statistic when client coveried.
    SrsStatistic* stat = SrsStatistic::instance();
    stat->on_disconnect(cid_.c_str());
}

srs_error_t SrsRtcPublishStream::initialize(SrsRequest* r, SrsRtcSourceDescription* stream_desc)
{
    srs_error_t err = srs_success;

    req_ = r->copy();

    if (stream_desc->audio_track_desc_) {
        audio_tracks_.push_back(new SrsRtcAudioRecvTrack(session_, stream_desc->audio_track_desc_));
    }

    for (int i = 0; i < (int)stream_desc->video_track_descs_.size(); ++i) {
        SrsRtcTrackDescription* desc = stream_desc->video_track_descs_.at(i);
        video_tracks_.push_back(new SrsRtcVideoRecvTrack(session_, desc));
    }

    int twcc_id = -1;
    uint32_t media_ssrc = 0;
    // because audio_track_desc have not twcc id, for example, h5demo
    // fetch twcc_id from video track description, 
    for (int i = 0; i < (int)stream_desc->video_track_descs_.size(); ++i) {
        SrsRtcTrackDescription* desc = stream_desc->video_track_descs_.at(i);
        twcc_id = desc->get_rtp_extension_id(kTWCCExt);
        media_ssrc = desc->ssrc_;
        break;
    }
    if (twcc_id > 0) {
        twcc_id_ = twcc_id;
        extension_types_.register_by_uri(twcc_id_, kTWCCExt);
        rtcp_twcc_.set_media_ssrc(media_ssrc);
    }

    nack_enabled_ = _srs_config->get_rtc_nack_enabled(req_->vhost);
    nack_no_copy_ = _srs_config->get_rtc_nack_no_copy(req_->vhost);
    pt_to_drop_ = (uint16_t)_srs_config->get_rtc_drop_for_pt(req_->vhost);
    twcc_enabled_ = _srs_config->get_rtc_twcc_enabled(req_->vhost);

    // No TWCC when negotiate, disable it.
    if (twcc_id <= 0) {
        twcc_enabled_ = false;
    }

    srs_trace("RTC publisher nack=%d, nnc=%d, pt-drop=%u, twcc=%u/%d", nack_enabled_, nack_no_copy_, pt_to_drop_, twcc_enabled_, twcc_id);

    // Setup tracks.
    for (int i = 0; i < (int)audio_tracks_.size(); i++) {
        SrsRtcAudioRecvTrack* track = audio_tracks_.at(i);
        track->set_nack_no_copy(nack_no_copy_);
    }

    for (int i = 0; i < (int)video_tracks_.size(); i++) {
        SrsRtcVideoRecvTrack* track = video_tracks_.at(i);
        track->set_nack_no_copy(nack_no_copy_);
    }

    // Setup the publish stream in source to enable PLI as such.
    if ((err = _srs_rtc_sources->fetch_or_create(req_, &source)) != srs_success) {
        return srs_error_wrap(err, "create source");
    }
    source->set_publish_stream(this);

    // Bridge to rtmp
#if defined(SRS_RTC) && defined(SRS_FFMPEG_FIT)
    bool rtc_to_rtmp = _srs_config->get_rtc_to_rtmp(req_->vhost);
    if (rtc_to_rtmp) {
        SrsLiveSource *rtmp = NULL;
        if ((err = _srs_sources->fetch_or_create(r, _srs_hybrid->srs()->instance(), &rtmp)) != srs_success) {
            return srs_error_wrap(err, "create source");
        }

        // TODO: FIMXE: Check it in SrsRtcConnection::add_publisher?
        if (!rtmp->can_publish(false)) {
            return srs_error_new(ERROR_SYSTEM_STREAM_BUSY, "rtmp stream %s busy", r->get_stream_url().c_str());
        }

        // Disable GOP cache for RTC2RTMP bridger, to keep the streams in sync,
        // especially for stream merging.
        rtmp->set_cache(false);

        SrsRtmpFromRtcBridger *bridger = new SrsRtmpFromRtcBridger(rtmp);
        if ((err = bridger->initialize(r)) != srs_success) {
            srs_freep(bridger);
            return srs_error_wrap(err, "create bridger");
        }

        source->set_bridger(bridger);
    }
#endif

    return err;
}

srs_error_t SrsRtcPublishStream::start()
{
    srs_error_t err = srs_success;

    if (is_started) {
        return err;
    }

    if ((err = source->on_publish()) != srs_success) {
        return srs_error_wrap(err, "on publish");
    }

    if ((err = pli_worker_->start()) != srs_success) {
        return srs_error_wrap(err, "start pli worker");
    }

    if (_srs_rtc_hijacker) {
        if ((err = _srs_rtc_hijacker->on_start_publish(session_, this, req_)) != srs_success) {
            return srs_error_wrap(err, "on start publish");
        }
    }
	
    // update the statistic when client discoveried.
    SrsStatistic* stat = SrsStatistic::instance();
    if ((err = stat->on_client(cid_.c_str(), req_, session_, SrsRtcConnPublish)) != srs_success) {
        return srs_error_wrap(err, "rtc: stat client");
    }

    is_started = true;

    return err;
}

void SrsRtcPublishStream::set_all_tracks_status(bool status)
{
    std::ostringstream merged_log;

    // set video track status
    if (true) {
        std::vector<SrsRtcVideoRecvTrack*>::iterator it;
        for (it = video_tracks_.begin(); it != video_tracks_.end(); ++it) {
            SrsRtcVideoRecvTrack* track = *it;

            bool previous = track->set_track_status(status);
            merged_log << "{track: " << track->get_track_id() << ", is_active: " << previous << "=>" << status << "},";
        }
    }

    // set audio track status
    if (true) {
        std::vector<SrsRtcAudioRecvTrack*>::iterator it;
        for (it = audio_tracks_.begin(); it != audio_tracks_.end(); ++it) {
            SrsRtcAudioRecvTrack* track = *it;

            bool previous = track->set_track_status(status);
            merged_log << "{track: " << track->get_track_id() << ", is_active: " << previous << "=>" << status << "},";
        }
    }

    srs_trace("RTC: Init tracks %s ok", merged_log.str().c_str());
}

const SrsContextId& SrsRtcPublishStream::context_id()
{
    return cid_;
}

srs_error_t SrsRtcPublishStream::send_rtcp_rr()
{
    srs_error_t err = srs_success;

    for (int i = 0; i < (int)video_tracks_.size(); ++i) {
        SrsRtcVideoRecvTrack* track = video_tracks_.at(i);
        if ((err = track->send_rtcp_rr()) != srs_success) {
            return srs_error_wrap(err, "track=%s", track->get_track_id().c_str());
        }
    }

    for (int i = 0; i < (int)audio_tracks_.size(); ++i) {
        SrsRtcAudioRecvTrack* track = audio_tracks_.at(i);
        if ((err = track->send_rtcp_rr()) != srs_success) {
            return srs_error_wrap(err, "track=%s", track->get_track_id().c_str());
        }
    }

    return err;
}

srs_error_t SrsRtcPublishStream::send_rtcp_xr_rrtr()
{
    srs_error_t err = srs_success;

    for (int i = 0; i < (int)video_tracks_.size(); ++i) {
        SrsRtcVideoRecvTrack* track = video_tracks_.at(i);
        if ((err = track->send_rtcp_xr_rrtr()) != srs_success) {
            return srs_error_wrap(err, "track=%s", track->get_track_id().c_str());
        }
    }

    for (int i = 0; i < (int)audio_tracks_.size(); ++i) {
        SrsRtcAudioRecvTrack* track = audio_tracks_.at(i);
        if ((err = track->send_rtcp_xr_rrtr()) != srs_success) {
            return srs_error_wrap(err, "track=%s", track->get_track_id().c_str());
        }
    }

    return err;
}

srs_error_t SrsRtcPublishStream::on_twcc(uint16_t sn) {
    srs_error_t err = srs_success;

    srs_utime_t now = srs_get_system_time();
    err = rtcp_twcc_.recv_packet(sn, now);

    return err;
}

srs_error_t SrsRtcPublishStream::on_rtp(char* data, int nb_data)
{
    srs_error_t err = srs_success;

    // For NACK simulator, drop packet.
    if (nn_simulate_nack_drop) {
        SrsBuffer b(data, nb_data); SrsRtpHeader h; h.ignore_padding(true);
        err = h.decode(&b); srs_freep(err); // Ignore any error for simluate drop.
        simulate_drop_packet(&h, nb_data);
        return err;
    }

    // Decode the header first.
    if (twcc_id_) {
        // We must parse the TWCC from RTP header before SRTP unprotect, because:
        //      1. Client may send some padding packets with invalid SequenceNumber, which causes the SRTP fail.
        //      2. Server may send multiple duplicated NACK to client, and got more than one ARQ packet, which also fail SRTP.
        // so, we must parse the header before SRTP unprotect(which may fail and drop packet).
        uint16_t twcc_sn = 0;
        if ((err = srs_rtp_fast_parse_twcc(data, nb_data, twcc_id_, twcc_sn)) == srs_success) {
            if((err = on_twcc(twcc_sn)) != srs_success) {
                return srs_error_wrap(err, "on twcc");
            }
        } else {
            srs_error_reset(err);
        }
    }

    // If payload type is configed to drop, ignore this packet.
    if (pt_to_drop_) {
        uint8_t pt = srs_rtp_fast_parse_pt(data, nb_data);
        if (pt_to_drop_ == pt) {
            return err;
        }
    }

    // Decrypt the cipher to plaintext RTP data.
    char* plaintext = data;
    int nb_plaintext = nb_data;
    if ((err = session_->transport_->unprotect_rtp(plaintext, &nb_plaintext)) != srs_success) {
        // We try to decode the RTP header for more detail error informations.
        SrsBuffer b(data, nb_data); SrsRtpHeader h; h.ignore_padding(true);
        srs_error_t r0 = h.decode(&b); srs_freep(r0); // Ignore any error for header decoding.

        err = srs_error_wrap(err, "marker=%u, pt=%u, seq=%u, ts=%u, ssrc=%u, pad=%u, payload=%uB", h.get_marker(), h.get_payload_type(),
            h.get_sequence(), h.get_timestamp(), h.get_ssrc(), h.get_padding(), nb_data - b.pos());

        return err;
    }

    // Handle the plaintext RTP packet.
    if ((err = on_rtp_plaintext(plaintext, nb_plaintext)) != srs_success) {
        // We try to decode the RTP header for more detail error informations.
        SrsBuffer b(data, nb_data); SrsRtpHeader h; h.ignore_padding(true);
        srs_error_t r0 = h.decode(&b); srs_freep(r0); // Ignore any error for header decoding.

        int nb_header = h.nb_bytes();
        const char* body = data + nb_header;
        int nb_body = nb_data - nb_header;
        return srs_error_wrap(err, "cipher=%u, plaintext=%u, body=[%s]", nb_data, nb_plaintext,
            srs_string_dumps_hex(body, nb_body, 8).c_str());
    }

    return err;
}

srs_error_t SrsRtcPublishStream::on_rtp_plaintext(char* plaintext, int nb_plaintext)
{
    srs_error_t err = srs_success;

    if (_srs_blackhole->blackhole) {
        _srs_blackhole->sendto(plaintext, nb_plaintext);
    }

    // Allocate packet form cache.
    SrsRtpPacket* pkt = new SrsRtpPacket();

    // Copy the packet body.
    char* p = pkt->wrap(plaintext, nb_plaintext);

    // Handle the packet.
    SrsBuffer buf(p, nb_plaintext);

    // @remark Note that the pkt might be set to NULL.
    err = do_on_rtp_plaintext(pkt, &buf);

    // Free the packet.
    // @remark Note that the pkt might be set to NULL.
    srs_freep(pkt);

    return err;
}

srs_error_t SrsRtcPublishStream::do_on_rtp_plaintext(SrsRtpPacket*& pkt, SrsBuffer* buf)
{
    srs_error_t err = srs_success;

    pkt->set_decode_handler(this);
    pkt->set_extension_types(&extension_types_);
    pkt->header.ignore_padding(false);

    if ((err = pkt->decode(buf)) != srs_success) {
        return srs_error_wrap(err, "decode rtp packet");
    }

    // For source to consume packet.
    uint32_t ssrc = pkt->header.get_ssrc();
    SrsRtcAudioRecvTrack* audio_track = get_audio_track(ssrc);
    SrsRtcVideoRecvTrack* video_track = get_video_track(ssrc);
    if (audio_track) {
        pkt->frame_type = SrsFrameTypeAudio;
        if ((err = audio_track->on_rtp(source, pkt)) != srs_success) {
            return srs_error_wrap(err, "on audio");
        }
    } else if (video_track) {
        pkt->frame_type = SrsFrameTypeVideo;
        if ((err = video_track->on_rtp(source, pkt)) != srs_success) {
            return srs_error_wrap(err, "on video");
        }
    } else {
        return srs_error_new(ERROR_RTC_RTP, "unknown ssrc=%u", ssrc);
    }

    if (_srs_rtc_hijacker) {
        if ((err = _srs_rtc_hijacker->on_rtp_packet(session_, this, req_, pkt)) != srs_success) {
            return srs_error_wrap(err, "on rtp packet");
        }
    }

    // If circuit-breaker is enabled, disable nack.
    if (_srs_circuit_breaker->hybrid_critical_water_level()) {
        ++_srs_pps_snack4->sugar;
        return err;
    }

    // For NACK to handle packet.
    // @remark Note that the pkt might be set to NULL.
    if (nack_enabled_) {
        if (audio_track) {
            if ((err = audio_track->on_nack(&pkt)) != srs_success) {
                return srs_error_wrap(err, "on nack");
            }
        } else if (video_track) {
            if ((err = video_track->on_nack(&pkt)) != srs_success) {
                return srs_error_wrap(err, "on nack");
            }
        }
    }

    return err;
}

srs_error_t SrsRtcPublishStream::check_send_nacks()
{
    srs_error_t err = srs_success;

    if (!nack_enabled_) {
        return err;
    }

    for (int i = 0; i < (int)video_tracks_.size(); ++i) {
        SrsRtcVideoRecvTrack* track = video_tracks_.at(i);
        if ((err = track->check_send_nacks()) != srs_success) {
            return srs_error_wrap(err, "video track=%s", track->get_track_id().c_str());
        }
    }

    for (int i = 0; i < (int)audio_tracks_.size(); ++i) {
        SrsRtcAudioRecvTrack* track = audio_tracks_.at(i);
        if ((err = track->check_send_nacks()) != srs_success) {
            return srs_error_wrap(err, "audio track=%s", track->get_track_id().c_str());
        }
    }

    return err;
}

void SrsRtcPublishStream::on_before_decode_payload(SrsRtpPacket* pkt, SrsBuffer* buf, ISrsRtpPayloader** ppayload, SrsRtspPacketPayloadType* ppt)
{
    // No payload, ignore.
    if (buf->empty()) {
        return;
    }

    uint32_t ssrc = pkt->header.get_ssrc();
    SrsRtcAudioRecvTrack* audio_track = get_audio_track(ssrc);
    SrsRtcVideoRecvTrack* video_track = get_video_track(ssrc);

    if (audio_track) {
        audio_track->on_before_decode_payload(pkt, buf, ppayload, ppt);
    } else if (video_track) {
        video_track->on_before_decode_payload(pkt, buf, ppayload, ppt);
    }
}

srs_error_t SrsRtcPublishStream::send_periodic_twcc()
{
    srs_error_t err = srs_success;

    if (last_time_send_twcc_) {
        uint32_t nn = 0;
        srs_utime_t duration = srs_duration(last_time_send_twcc_, srs_get_system_time());
        if (duration > 130 * SRS_UTIME_MILLISECONDS && twcc_epp_->can_print(0, &nn)) {
            srs_warn2(TAG_LARGE_TIMER, "twcc delay %dms > 100ms, count=%u/%u",
                srsu2msi(duration), nn, twcc_epp_->nn_count);
        }
    }
    last_time_send_twcc_ = srs_get_system_time();

    if (!rtcp_twcc_.need_feedback()) {
        return err;
    }

    ++_srs_pps_srtcps->sugar;

    // limit the max count=1024 to avoid dead loop.
    for (int i = 0; i < 1024 && rtcp_twcc_.need_feedback(); ++i) {
        char pkt[kMaxUDPDataSize];
        SrsBuffer *buffer = new SrsBuffer(pkt, sizeof(pkt));
        SrsAutoFree(SrsBuffer, buffer);

        rtcp_twcc_.set_feedback_count(twcc_fb_count_);
        twcc_fb_count_++;

        if((err = rtcp_twcc_.encode(buffer)) != srs_success) {
            return srs_error_wrap(err, "encode, count=%u", twcc_fb_count_);
        }

        if((err = session_->send_rtcp(pkt, buffer->pos())) != srs_success) {
            return srs_error_wrap(err, "send twcc, count=%u", twcc_fb_count_);
        }
    }

    return err;
}

srs_error_t SrsRtcPublishStream::on_rtcp(SrsRtcpCommon* rtcp)
{
    if(SrsRtcpType_sr == rtcp->type()) {
        SrsRtcpSR* sr = dynamic_cast<SrsRtcpSR*>(rtcp);
        return on_rtcp_sr(sr);
    } else if(SrsRtcpType_xr == rtcp->type()) {
        SrsRtcpXr* xr = dynamic_cast<SrsRtcpXr*>(rtcp);
        return on_rtcp_xr(xr);
    } else if(SrsRtcpType_sdes == rtcp->type()) {
        //ignore RTCP SDES
        return srs_success;
    } else if(SrsRtcpType_bye == rtcp->type()) {
        // TODO: FIXME: process rtcp bye.
        return srs_success;
    } else {
        return srs_error_new(ERROR_RTC_RTCP_CHECK, "unknown rtcp type=%u", rtcp->type());
    }
}

srs_error_t SrsRtcPublishStream::on_rtcp_sr(SrsRtcpSR* rtcp)
{
    srs_error_t err = srs_success;
    SrsNtp srs_ntp = SrsNtp::to_time_ms(rtcp->get_ntp());

    srs_verbose("sender report, ssrc_of_sender=%u, rtp_time=%u, sender_packet_count=%u, sender_octec_count=%u, ms=%u",
        rtcp->get_ssrc(), rtcp->get_rtp_ts(), rtcp->get_rtp_send_packets(), rtcp->get_rtp_send_bytes(), srs_ntp.system_ms_);

    update_send_report_time(rtcp->get_ssrc(), srs_ntp, rtcp->get_rtp_ts());

    return err;
}

srs_error_t SrsRtcPublishStream::on_rtcp_xr(SrsRtcpXr* rtcp)
{
    srs_error_t err = srs_success;

    /*
     @see: http://www.rfc-editor.org/rfc/rfc3611.html#section-2

      0                   1                   2                   3
      0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     |V=2|P|reserved |   PT=XR=207   |             length            |
     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     |                              SSRC                             |
     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     :                         report blocks                         :
     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     */

    SrsBuffer stream(rtcp->data(), rtcp->size());
    /*uint8_t first = */stream.read_1bytes();
    uint8_t pt = stream.read_1bytes();
    srs_assert(pt == kXR);
    uint16_t length = (stream.read_2bytes() + 1) * 4;
    /*uint32_t ssrc = */stream.read_4bytes();

    if (length > rtcp->size()) {
        return srs_error_new(ERROR_RTC_RTCP_CHECK, "invalid XR packet, length=%u, nb_buf=%d", length, rtcp->size());
    }

    while (stream.pos() + 4 < length) {
        uint8_t bt = stream.read_1bytes();
        stream.skip(1);
        uint16_t block_length = (stream.read_2bytes() + 1) * 4;

        if (stream.pos() + block_length - 4 > rtcp->size()) {
            return srs_error_new(ERROR_RTC_RTCP_CHECK, "invalid XR packet block, block_length=%u, nb_buf=%d", block_length, rtcp->size());
        }

        if (bt == 5) {
            for (int i = 4; i < block_length; i += 12) {
                uint32_t ssrc = stream.read_4bytes();
                uint32_t lrr = stream.read_4bytes();
                uint32_t dlrr = stream.read_4bytes();

                SrsNtp cur_ntp = SrsNtp::from_time_ms(srs_update_system_time() / 1000);
                uint32_t compact_ntp = (cur_ntp.ntp_second_ << 16) | (cur_ntp.ntp_fractions_ >> 16);

                int rtt_ntp = compact_ntp - lrr - dlrr;
                int rtt = ((rtt_ntp * 1000) >> 16) + ((rtt_ntp >> 16) * 1000);
                srs_verbose("ssrc=%u, compact_ntp=%u, lrr=%u, dlrr=%u, rtt=%d",
                    ssrc, compact_ntp, lrr, dlrr, rtt);

                update_rtt(ssrc, rtt);
            }
        }
    }

    return err;
}

void SrsRtcPublishStream::request_keyframe(uint32_t ssrc)
{
    SrsContextId sub_cid = _srs_context->get_id();
    pli_worker_->request_keyframe(ssrc, sub_cid);
	
    uint32_t nn = 0;
    if (pli_epp->can_print(ssrc, &nn)) {
        // The player(subscriber) cid, which requires PLI.
        srs_trace("RTC: Need PLI ssrc=%u, play=[%s], publish=[%s], count=%u/%u", ssrc, sub_cid.c_str(),
            cid_.c_str(), nn, pli_epp->nn_count);
    }
}

srs_error_t SrsRtcPublishStream::do_request_keyframe(uint32_t ssrc, SrsContextId sub_cid)
{
    srs_error_t err = srs_success;
    if ((err = session_->send_rtcp_fb_pli(ssrc, sub_cid)) != srs_success) {
        srs_warn("PLI err %s", srs_error_desc(err).c_str());
        srs_freep(err);
    }

    return err;
}

void SrsRtcPublishStream::simulate_nack_drop(int nn)
{
    nn_simulate_nack_drop = nn;
}

void SrsRtcPublishStream::simulate_drop_packet(SrsRtpHeader* h, int nn_bytes)
{
    srs_warn("RTC: NACK simulator #%d drop seq=%u, ssrc=%u/%s, ts=%u, %d bytes", nn_simulate_nack_drop,
        h->get_sequence(), h->get_ssrc(), (get_video_track(h->get_ssrc())? "Video":"Audio"), h->get_timestamp(),
        nn_bytes);

    nn_simulate_nack_drop--;
}

SrsRtcVideoRecvTrack* SrsRtcPublishStream::get_video_track(uint32_t ssrc)
{
    for (int i = 0; i < (int)video_tracks_.size(); ++i) {
        SrsRtcVideoRecvTrack* track = video_tracks_.at(i);
        if (track->has_ssrc(ssrc)) {
            return track;
        }
    }

    return NULL;
}

SrsRtcAudioRecvTrack* SrsRtcPublishStream::get_audio_track(uint32_t ssrc)
{
    for (int i = 0; i < (int)audio_tracks_.size(); ++i) {
        SrsRtcAudioRecvTrack* track = audio_tracks_.at(i);
        if (track->has_ssrc(ssrc)) {
            return track;
        }
    }

    return NULL;
}

void SrsRtcPublishStream::update_rtt(uint32_t ssrc, int rtt)
{
    SrsRtcVideoRecvTrack* video_track = get_video_track(ssrc);
    if (video_track) {
        return video_track->update_rtt(rtt);
    }

    SrsRtcAudioRecvTrack* audio_track = get_audio_track(ssrc);
    if (audio_track) {
        return audio_track->update_rtt(rtt);
    }
}

void SrsRtcPublishStream::update_send_report_time(uint32_t ssrc, const SrsNtp& ntp, uint32_t rtp_time)
{
    SrsRtcVideoRecvTrack* video_track = get_video_track(ssrc);
    if (video_track) {
        return video_track->update_send_report_time(ntp, rtp_time);
    }

    SrsRtcAudioRecvTrack* audio_track = get_audio_track(ssrc);
    if (audio_track) {
        return audio_track->update_send_report_time(ntp, rtp_time);
    }
}

ISrsRtcConnectionHijacker::ISrsRtcConnectionHijacker()
{
}

ISrsRtcConnectionHijacker::~ISrsRtcConnectionHijacker()
{
}

SrsRtcConnectionNackTimer::SrsRtcConnectionNackTimer(SrsRtcConnection* p) : p_(p)
{
    _srs_hybrid->timer20ms()->subscribe(this);
}

SrsRtcConnectionNackTimer::~SrsRtcConnectionNackTimer()
{
    _srs_hybrid->timer20ms()->unsubscribe(this);
}

srs_error_t SrsRtcConnectionNackTimer::on_timer(srs_utime_t interval)
{
    srs_error_t err = srs_success;

    if (!p_->nack_enabled_) {
        return err;
    }

    ++_srs_pps_conn->sugar;

    // If circuit-breaker is enabled, disable nack.
    if (_srs_circuit_breaker->hybrid_critical_water_level()) {
        ++_srs_pps_snack4->sugar;
        return err;
    }

    std::map<std::string, SrsRtcPublishStream*>::iterator it;
    for (it = p_->publishers_.begin(); it != p_->publishers_.end(); it++) {
        SrsRtcPublishStream* publisher = it->second;

        if ((err = publisher->check_send_nacks()) != srs_success) {
            srs_warn("ignore nack err %s", srs_error_desc(err).c_str());
            srs_freep(err);
        }
    }

    return err;
}

SrsRtcConnection::SrsRtcConnection(SrsRtcServer* s, const SrsContextId& cid)
{
    req_ = NULL;
    cid_ = cid;
    hijacker_ = NULL;

    sendonly_skt = NULL;
    server_ = s;
    transport_ = new SrsSecurityTransport(this);

    cache_iov_ = new iovec();
    cache_iov_->iov_base = new char[kRtpPacketSize];
    cache_iov_->iov_len = kRtpPacketSize;
    cache_buffer_ = new SrsBuffer((char*)cache_iov_->iov_base, kRtpPacketSize);

    state_ = INIT;
    last_stun_time = 0;
    session_timeout = 0;
    disposing_ = false;

    twcc_id_ = 0;
    nn_simulate_player_nack_drop = 0;
    pp_address_change = new SrsErrorPithyPrint();
    pli_epp = new SrsErrorPithyPrint();

    nack_enabled_ = false;
    timer_nack_ = new SrsRtcConnectionNackTimer(this);

    _srs_rtc_manager->subscribe(this);
}

SrsRtcConnection::~SrsRtcConnection()
{
    _srs_rtc_manager->unsubscribe(this);

    srs_freep(timer_nack_);

    // Cleanup publishers.
    for(map<string, SrsRtcPublishStream*>::iterator it = publishers_.begin(); it != publishers_.end(); ++it) {
        SrsRtcPublishStream* publisher = it->second;
        srs_freep(publisher);
    }
    publishers_.clear();
    publishers_ssrc_map_.clear();

    // Cleanup players.
    for(map<string, SrsRtcPlayStream*>::iterator it = players_.begin(); it != players_.end(); ++it) {
        SrsRtcPlayStream* player = it->second;
        srs_freep(player);
    }
    players_.clear();
    players_ssrc_map_.clear();

    // Note that we should never delete the sendonly_skt,
    // it's just point to the object in peer_addresses_.
    map<string, SrsUdpMuxSocket*>::iterator it;
    for (it = peer_addresses_.begin(); it != peer_addresses_.end(); ++it) {
        SrsUdpMuxSocket* addr = it->second;
        srs_freep(addr);
    }

    if (true) {
        char* iov_base = (char*)cache_iov_->iov_base;
        srs_freepa(iov_base);
        srs_freep(cache_iov_);
    }
    srs_freep(cache_buffer_);

    srs_freep(transport_);
    srs_freep(req_);
    srs_freep(pp_address_change);
    srs_freep(pli_epp);
}

void SrsRtcConnection::on_before_dispose(ISrsResource* c)
{
    if (disposing_) {
        return;
    }

    SrsRtcConnection* session = dynamic_cast<SrsRtcConnection*>(c);
    if (session == this) {
        disposing_ = true;
    }

    if (session && session == this) {
        _srs_context->set_id(cid_);
        srs_trace("RTC: session detach from [%s](%s), disposing=%d", c->get_id().c_str(),
            c->desc().c_str(), disposing_);
    }
}

void SrsRtcConnection::on_disposing(ISrsResource* c)
{
    if (disposing_) {
        return;
    }
}

SrsSdp* SrsRtcConnection::get_local_sdp()
{
    return &local_sdp;
}

void SrsRtcConnection::set_local_sdp(const SrsSdp& sdp)
{
    local_sdp = sdp;
}

SrsSdp* SrsRtcConnection::get_remote_sdp()
{
    return &remote_sdp;
}

void SrsRtcConnection::set_remote_sdp(const SrsSdp& sdp)
{
    remote_sdp = sdp;
}

SrsRtcConnectionStateType SrsRtcConnection::state()
{
    return state_;
}

void SrsRtcConnection::set_state(SrsRtcConnectionStateType state)
{
    state_ = state;
}

string SrsRtcConnection::username()
{
    return username_;
}

vector<SrsUdpMuxSocket*> SrsRtcConnection::peer_addresses()
{
    vector<SrsUdpMuxSocket*> addresses;

    map<string, SrsUdpMuxSocket*>::iterator it;
    for (it = peer_addresses_.begin(); it != peer_addresses_.end(); ++it) {
        SrsUdpMuxSocket* addr = it->second;
        addresses.push_back(addr);
    }

    return addresses;
}

const SrsContextId& SrsRtcConnection::get_id()
{
    return cid_;
}

std::string SrsRtcConnection::desc()
{
    return "RtcConn";
}

void SrsRtcConnection::expire()
{
    _srs_rtc_manager->remove(this);
}

void SrsRtcConnection::switch_to_context()
{
    _srs_context->set_id(cid_);
}

const SrsContextId& SrsRtcConnection::context_id()
{
    return cid_;
}

srs_error_t SrsRtcConnection::add_publisher(SrsRtcUserConfig* ruc, SrsSdp& local_sdp)
{
    srs_error_t err = srs_success;

    SrsRequest* req = ruc->req_;

    SrsRtcSourceDescription* stream_desc = new SrsRtcSourceDescription();
    SrsAutoFree(SrsRtcSourceDescription, stream_desc);

    // TODO: FIXME: Change to api of stream desc.
    if ((err = negotiate_publish_capability(ruc, stream_desc)) != srs_success) {
        return srs_error_wrap(err, "publish negotiate");
    }

    if ((err = generate_publish_local_sdp(req, local_sdp, stream_desc, ruc->remote_sdp_.is_unified())) != srs_success) {
        return srs_error_wrap(err, "generate local sdp");
    }

    SrsRtcSource* source = NULL;
    if ((err = _srs_rtc_sources->fetch_or_create(req, &source)) != srs_success) {
        return srs_error_wrap(err, "create source");
    }

    // When SDP is done, we set the stream to create state, to prevent multiple publisher.
    // @note Here, we check the stream again.
    if (!source->can_publish()) {
        return srs_error_new(ERROR_RTC_SOURCE_BUSY, "stream %s busy", req->get_stream_url().c_str());
    }
    source->set_stream_created();

    // Apply the SDP to source.
    source->set_stream_desc(stream_desc);

    // TODO: FIXME: What happends when error?
    if ((err = create_publisher(req, stream_desc)) != srs_success) {
        return srs_error_wrap(err, "create publish");
    }

    return err;
}

// TODO: FIXME: Error when play before publishing.
srs_error_t SrsRtcConnection::add_player(SrsRtcUserConfig* ruc, SrsSdp& local_sdp)
{
    srs_error_t err = srs_success;

    SrsRequest* req = ruc->req_;

    if (_srs_rtc_hijacker) {
        if ((err = _srs_rtc_hijacker->on_before_play(this, req)) != srs_success) {
            return srs_error_wrap(err, "before play");
        }
    }

    std::map<uint32_t, SrsRtcTrackDescription*> play_sub_relations;
    if ((err = negotiate_play_capability(ruc, play_sub_relations)) != srs_success) {
        return srs_error_wrap(err, "play negotiate");
    }

    if (!play_sub_relations.size()) {
        return srs_error_new(ERROR_RTC_SDP_EXCHANGE, "no play relations");
    }

    SrsRtcSourceDescription* stream_desc = new SrsRtcSourceDescription();
    SrsAutoFree(SrsRtcSourceDescription, stream_desc);
    std::map<uint32_t, SrsRtcTrackDescription*>::iterator it = play_sub_relations.begin();
    while (it != play_sub_relations.end()) {
        SrsRtcTrackDescription* track_desc = it->second;

        // TODO: FIXME: we only support one audio track.
        if (track_desc->type_ == "audio" && !stream_desc->audio_track_desc_) {
            stream_desc->audio_track_desc_ = track_desc->copy();
        }

        if (track_desc->type_ == "video") {
            stream_desc->video_track_descs_.push_back(track_desc->copy());
        }
        ++it;
    }

    if ((err = generate_play_local_sdp(req, local_sdp, stream_desc, ruc->remote_sdp_.is_unified())) != srs_success) {
        return srs_error_wrap(err, "generate local sdp");
    }

    if ((err = create_player(req, play_sub_relations)) != srs_success) {
        return srs_error_wrap(err, "create player");
    }

    return err;
}

srs_error_t SrsRtcConnection::initialize(SrsRequest* r, bool dtls, bool srtp, string username)
{
    srs_error_t err = srs_success;

    username_ = username;
    req_ = r->copy();

    if (!srtp) {
        srs_freep(transport_);
        if (dtls) {
            transport_ = new SrsSemiSecurityTransport(this);
        } else {
            transport_ = new SrsPlaintextTransport(this);
        }
    }

    SrsSessionConfig* cfg = &local_sdp.session_negotiate_;
    if ((err = transport_->initialize(cfg)) != srs_success) {
        return srs_error_wrap(err, "init");
    }

    // TODO: FIXME: Support reload.
    session_timeout = _srs_config->get_rtc_stun_timeout(req_->vhost);
    last_stun_time = srs_get_system_time();

    nack_enabled_ = _srs_config->get_rtc_nack_enabled(req_->vhost);

    srs_trace("RTC init session, user=%s, url=%s, encrypt=%u/%u, DTLS(role=%s, version=%s), timeout=%dms, nack=%d",
        username.c_str(), r->get_stream_url().c_str(), dtls, srtp, cfg->dtls_role.c_str(), cfg->dtls_version.c_str(),
        srsu2msi(session_timeout), nack_enabled_);

    return err;
}

srs_error_t SrsRtcConnection::on_stun(SrsUdpMuxSocket* skt, SrsStunPacket* r)
{
    srs_error_t err = srs_success;

    if (!r->is_binding_request()) {
        return err;
    }

    // We are running in the ice-lite(server) mode. If client have multi network interface,
    // we only choose one candidate pair which is determined by client.
    update_sendonly_socket(skt);

    // Write STUN messages to blackhole.
    if (_srs_blackhole->blackhole) {
        _srs_blackhole->sendto(skt->data(), skt->size());
    }

    if ((err = on_binding_request(r)) != srs_success) {
        return srs_error_wrap(err, "stun binding request failed");
    }

    return err;
}

srs_error_t SrsRtcConnection::on_dtls(char* data, int nb_data)
{
    return transport_->on_dtls(data, nb_data);
}

srs_error_t SrsRtcConnection::on_rtcp(char* data, int nb_data)
{
    srs_error_t err = srs_success;

    int nb_unprotected_buf = nb_data;
    if ((err = transport_->unprotect_rtcp(data, &nb_unprotected_buf)) != srs_success) {
        return srs_error_wrap(err, "rtcp unprotect");
    }

    char* unprotected_buf = data;
    if (_srs_blackhole->blackhole) {
        _srs_blackhole->sendto(unprotected_buf, nb_unprotected_buf);
    }

    SrsBuffer* buffer = new SrsBuffer(unprotected_buf, nb_unprotected_buf);
    SrsAutoFree(SrsBuffer, buffer);

    SrsRtcpCompound rtcp_compound;
    if(srs_success != (err = rtcp_compound.decode(buffer))) {
        return srs_error_wrap(err, "decode rtcp plaintext=%u, bytes=[%s], at=%s", nb_unprotected_buf,
            srs_string_dumps_hex(unprotected_buf, nb_unprotected_buf, 8).c_str(),
            srs_string_dumps_hex(buffer->head(), buffer->left(), 8).c_str());
    }

    SrsRtcpCommon* rtcp = NULL;
    while(NULL != (rtcp = rtcp_compound.get_next_rtcp())) {
        err = dispatch_rtcp(rtcp);
        SrsAutoFree(SrsRtcpCommon, rtcp);

        if(srs_success != err) {
            return srs_error_wrap(err, "cipher=%u, plaintext=%u, bytes=[%s], rtcp=(%u,%u,%u,%u)", nb_data, nb_unprotected_buf,
                srs_string_dumps_hex(rtcp->data(), rtcp->size(), rtcp->size()).c_str(),
                rtcp->get_rc(), rtcp->type(), rtcp->get_ssrc(), rtcp->size());
        }
    }

    return err;
}

srs_error_t SrsRtcConnection::dispatch_rtcp(SrsRtcpCommon* rtcp)
{
    srs_error_t err = srs_success;

    // For TWCC packet.
    if (SrsRtcpType_rtpfb == rtcp->type() && 15 == rtcp->get_rc()) {
        return on_rtcp_feedback_twcc(rtcp->data(), rtcp->size());
    }

    // For REMB packet.
    if (SrsRtcpType_psfb == rtcp->type()) {
        SrsRtcpPsfbCommon* psfb = dynamic_cast<SrsRtcpPsfbCommon*>(rtcp);
        if (15 == psfb->get_rc()) {
            return on_rtcp_feedback_remb(psfb);
        }
    }

    // Ignore special packet.
    if (SrsRtcpType_rr == rtcp->type()) {
        SrsRtcpRR* rr = dynamic_cast<SrsRtcpRR*>(rtcp);
        if (rr->get_rb_ssrc() == 0) { //for native client
            return err;
        }
    }

    // The feedback packet for specified SSRC.
    // For example, if got SR packet, we required a publisher to handle it.
    uint32_t required_publisher_ssrc = 0, required_player_ssrc = 0;
    if (SrsRtcpType_sr == rtcp->type()) {
        required_publisher_ssrc = rtcp->get_ssrc();
    } else if (SrsRtcpType_rr == rtcp->type()) {
        SrsRtcpRR* rr = dynamic_cast<SrsRtcpRR*>(rtcp);
        required_player_ssrc = rr->get_rb_ssrc();
    } else if (SrsRtcpType_rtpfb == rtcp->type()) {
        if(1 == rtcp->get_rc()) {
            SrsRtcpNack* nack = dynamic_cast<SrsRtcpNack*>(rtcp);
            required_player_ssrc = nack->get_media_ssrc();
        }
    } else if(SrsRtcpType_psfb == rtcp->type()) {
        SrsRtcpPsfbCommon* psfb = dynamic_cast<SrsRtcpPsfbCommon*>(rtcp);
        required_player_ssrc = psfb->get_media_ssrc();
    }

    // Find the publisher or player by SSRC, always try to got one.
    SrsRtcPlayStream* player = NULL;
    SrsRtcPublishStream* publisher = NULL;
    if (true) {
        uint32_t ssrc = required_publisher_ssrc? required_publisher_ssrc : rtcp->get_ssrc();
        map<uint32_t, SrsRtcPublishStream*>::iterator it = publishers_ssrc_map_.find(ssrc);
        if (it != publishers_ssrc_map_.end()) {
            publisher = it->second;
        }
    }

    if (true) {
        uint32_t ssrc = required_player_ssrc? required_player_ssrc : rtcp->get_ssrc();
        map<uint32_t, SrsRtcPlayStream*>::iterator it = players_ssrc_map_.find(ssrc);
        if (it != players_ssrc_map_.end()) {
            player = it->second;
        }
    }

    // Ignore if packet is required by publisher or player.
    if (required_publisher_ssrc && !publisher) {
        srs_warn("no ssrc %u in publishers. rtcp type:%u", required_publisher_ssrc, rtcp->type());
        return err;
    }
    if (required_player_ssrc && !player) {
        srs_warn("no ssrc %u in players. rtcp type:%u", required_player_ssrc, rtcp->type());
        return err;
    }

    // Handle packet by publisher or player.
    if (publisher && srs_success != (err = publisher->on_rtcp(rtcp))) {
        return srs_error_wrap(err, "handle rtcp");
    }
    if (player && srs_success != (err = player->on_rtcp(rtcp))) {
        return srs_error_wrap(err, "handle rtcp");
    }

    return err;
}

srs_error_t SrsRtcConnection::on_rtcp_feedback_twcc(char* data, int nb_data)
{
    return srs_success;
}

srs_error_t SrsRtcConnection::on_rtcp_feedback_remb(SrsRtcpPsfbCommon *rtcp)
{
    //ignore REMB
    return srs_success;
}

void SrsRtcConnection::set_hijacker(ISrsRtcConnectionHijacker* h)
{
    hijacker_ = h;
}

srs_error_t SrsRtcConnection::on_rtp(char* data, int nb_data)
{
    srs_error_t err = srs_success;

    SrsRtcPublishStream* publisher = NULL;
    if ((err = find_publisher(data, nb_data, &publisher)) != srs_success) {
        return srs_error_wrap(err, "find");
    }
    srs_assert(publisher);

    return publisher->on_rtp(data, nb_data);
}

srs_error_t SrsRtcConnection::find_publisher(char* buf, int size, SrsRtcPublishStream** ppublisher)
{
    srs_error_t err = srs_success;

    if (publishers_.size() == 0) {
        return srs_error_new(ERROR_RTC_RTCP, "no publisher");
    }

    uint32_t ssrc = srs_rtp_fast_parse_ssrc(buf, size);
    if (ssrc == 0) {
        return srs_error_new(ERROR_RTC_NO_PUBLISHER, "invalid ssrc");
    }

    map<uint32_t, SrsRtcPublishStream*>::iterator it = publishers_ssrc_map_.find(ssrc);
    if(it == publishers_ssrc_map_.end()) {
        return srs_error_new(ERROR_RTC_NO_PUBLISHER, "no publisher for ssrc:%u", ssrc);
    }

    *ppublisher = it->second;

    return err;
}

srs_error_t SrsRtcConnection::on_connection_established()
{
    srs_error_t err = srs_success;

    // Ignore if disposing.
    if (disposing_) {
        return err;
    }

    // If DTLS done packet received many times, such as ARQ, ignore.
    if(ESTABLISHED == state_) {
        return err;
    }
    state_ = ESTABLISHED;

    srs_trace("RTC: session pub=%u, sub=%u, to=%dms connection established", publishers_.size(), players_.size(),
        srsu2msi(session_timeout));

    // start all publisher
    for(map<string, SrsRtcPublishStream*>::iterator it = publishers_.begin(); it != publishers_.end(); ++it) {
        string url = it->first;
        SrsRtcPublishStream* publisher = it->second;

        srs_trace("RTC: Publisher url=%s established", url.c_str());

        if ((err = publisher->start()) != srs_success) {
            return srs_error_wrap(err, "start publish");
        }
    }

    // start all player
    for(map<string, SrsRtcPlayStream*>::iterator it = players_.begin(); it != players_.end(); ++it) {
        string url = it->first;
        SrsRtcPlayStream* player = it->second;

        srs_trace("RTC: Subscriber url=%s established", url.c_str());

        if ((err = player->start()) != srs_success) {
            return srs_error_wrap(err, "start play");
        }
    }

    if (hijacker_) {
        if ((err = hijacker_->on_dtls_done()) != srs_success) {
            return srs_error_wrap(err, "hijack on dtls done");
        }
    }

    return err;
}

srs_error_t SrsRtcConnection::on_dtls_alert(std::string type, std::string desc)
{
    srs_error_t err = srs_success;

    // CN(Close Notify) is sent when client close the PeerConnection.
    if (type == "warning" && desc == "CN") {
        SrsContextRestore(_srs_context->get_id());
        switch_to_context();

        srs_trace("RTC: session destroy by DTLS alert, username=%s", username_.c_str());
        _srs_rtc_manager->remove(this);
    }

    return err;
}

srs_error_t SrsRtcConnection::start_play(string stream_uri)
{
    srs_error_t err = srs_success;

    map<string, SrsRtcPlayStream*>::iterator it = players_.find(stream_uri);
    if(it == players_.end()) {
        return srs_error_new(ERROR_RTC_NO_PLAYER, "not subscribe %s", stream_uri.c_str());
    }

    SrsRtcPlayStream* player = it->second;
    if ((err = player->start()) != srs_success) {
        return srs_error_wrap(err, "start");
    }

    return err;
}

srs_error_t SrsRtcConnection::start_publish(std::string stream_uri)
{
    srs_error_t err = srs_success;

    map<string, SrsRtcPublishStream*>::iterator it = publishers_.find(stream_uri);
    if(it == publishers_.end()) {
        return srs_error_new(ERROR_RTC_NO_PUBLISHER, "no %s publisher", stream_uri.c_str());
    }

    if ((err = it->second->start()) != srs_success) {
        return srs_error_wrap(err, "start");
    }

    return err;
}

bool SrsRtcConnection::is_alive()
{
    return last_stun_time + session_timeout > srs_get_system_time();
}

void SrsRtcConnection::alive()
{
    last_stun_time = srs_get_system_time();
}

void SrsRtcConnection::update_sendonly_socket(SrsUdpMuxSocket* skt)
{
    // TODO: FIXME: Refine performance.
    string prev_peer_id, peer_id = skt->peer_id();
    if (sendonly_skt) {
        prev_peer_id = sendonly_skt->peer_id();
    }

    // Ignore if same address.
    if (prev_peer_id == peer_id) {
        return;
    }

    // Find object from cache.
    SrsUdpMuxSocket* addr_cache = NULL;
    if (true) {
        map<string, SrsUdpMuxSocket*>::iterator it = peer_addresses_.find(peer_id);
        if (it != peer_addresses_.end()) {
            addr_cache = it->second;
        }
    }

    // Show address change log.
    if (prev_peer_id.empty()) {
        srs_trace("RTC: session address init %s", peer_id.c_str());
    } else {
        uint32_t nn = 0;
        if (pp_address_change->can_print(skt->get_peer_port(), &nn)) {
            srs_trace("RTC: session address change %s -> %s, cached=%d, nn_change=%u/%u, nn_address=%u", prev_peer_id.c_str(),
                peer_id.c_str(), (addr_cache? 1:0), pp_address_change->nn_count, nn, peer_addresses_.size());
        }
    }

    // If no cache, build cache and setup the relations in connection.
    if (!addr_cache) {
        peer_addresses_[peer_id] = addr_cache = skt->copy_sendonly();
        _srs_rtc_manager->add_with_id(peer_id, this);

        uint64_t fast_id = skt->fast_id();
        if (fast_id) {
            _srs_rtc_manager->add_with_fast_id(fast_id, this);
        }
    }

    // Update the transport.
    sendonly_skt = addr_cache;
}

srs_error_t SrsRtcConnection::send_rtcp(char *data, int nb_data)
{
    srs_error_t err = srs_success;

    ++_srs_pps_srtcps->sugar;

    int  nb_buf = nb_data;
    if ((err = transport_->protect_rtcp(data, &nb_buf)) != srs_success) {
        return srs_error_wrap(err, "protect rtcp");
    }

    if ((err = sendonly_skt->sendto(data, nb_buf, 0)) != srs_success) {
        return srs_error_wrap(err, "send");
    }

    return err;
}

void SrsRtcConnection::check_send_nacks(SrsRtpNackForReceiver* nack, uint32_t ssrc, uint32_t& sent_nacks, uint32_t& timeout_nacks)
{
    ++_srs_pps_snack->sugar;

    SrsRtcpNack rtcpNack(ssrc);

    rtcpNack.set_media_ssrc(ssrc);
    nack->get_nack_seqs(rtcpNack, timeout_nacks);

    if(rtcpNack.empty()){
        return;
    }

    ++_srs_pps_snack2->sugar;
    ++_srs_pps_srtcps->sugar;

    char buf[kRtcpPacketSize];
    SrsBuffer stream(buf, sizeof(buf));

    // TODO: FIXME: Check error.
    rtcpNack.encode(&stream);

    // TODO: FIXME: Check error.
    send_rtcp(stream.data(), stream.pos());
}

srs_error_t SrsRtcConnection::send_rtcp_rr(uint32_t ssrc, SrsRtpRingBuffer* rtp_queue, const uint64_t& last_send_systime, const SrsNtp& last_send_ntp)
{
    ++_srs_pps_srtcps->sugar;

    // @see https://tools.ietf.org/html/rfc3550#section-6.4.2
    char buf[kRtpPacketSize];
    SrsBuffer stream(buf, sizeof(buf));
    stream.write_1bytes(0x81);
    stream.write_1bytes(kRR);
    stream.write_2bytes(7);
    stream.write_4bytes(ssrc); // TODO: FIXME: Should be 1?

    uint8_t fraction_lost = 0;
    uint32_t cumulative_number_of_packets_lost = 0 & 0x7FFFFF;
    uint32_t extended_highest_sequence = rtp_queue->get_extended_highest_sequence();
    uint32_t interarrival_jitter = 0;

    uint32_t rr_lsr = 0;
    uint32_t rr_dlsr = 0;

    if (last_send_systime > 0) {
        rr_lsr = (last_send_ntp.ntp_second_ << 16) | (last_send_ntp.ntp_fractions_ >> 16);
        uint32_t dlsr = (srs_update_system_time() - last_send_systime) / 1000;
        rr_dlsr = ((dlsr / 1000) << 16) | ((dlsr % 1000) * 65536 / 1000);
    }

    stream.write_4bytes(ssrc);
    stream.write_1bytes(fraction_lost);
    stream.write_3bytes(cumulative_number_of_packets_lost);
    stream.write_4bytes(extended_highest_sequence);
    stream.write_4bytes(interarrival_jitter);
    stream.write_4bytes(rr_lsr);
    stream.write_4bytes(rr_dlsr);

    srs_info("RR ssrc=%u, fraction_lost=%u, cumulative_number_of_packets_lost=%u, extended_highest_sequence=%u, interarrival_jitter=%u",
        ssrc, fraction_lost, cumulative_number_of_packets_lost, extended_highest_sequence, interarrival_jitter);

    return send_rtcp(stream.data(), stream.pos());
}

srs_error_t SrsRtcConnection::send_rtcp_xr_rrtr(uint32_t ssrc)
{
    ++_srs_pps_srtcps->sugar;

    /*
     @see: http://www.rfc-editor.org/rfc/rfc3611.html#section-2

      0                   1                   2                   3
      0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     |V=2|P|reserved |   PT=XR=207   |             length            |
     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     |                              SSRC                             |
     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     :                         report blocks                         :
     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

     @see: http://www.rfc-editor.org/rfc/rfc3611.html#section-4.4

      0                   1                   2                   3
         0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
        +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
        |     BT=4      |   reserved    |       block length = 2        |
        +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
        |              NTP timestamp, most significant word             |
        +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
        |             NTP timestamp, least significant word             |
        +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    */
    srs_utime_t now = srs_update_system_time();
    SrsNtp cur_ntp = SrsNtp::from_time_ms(now / 1000);

    char buf[kRtpPacketSize];
    SrsBuffer stream(buf, sizeof(buf));
    stream.write_1bytes(0x80);
    stream.write_1bytes(kXR);
    stream.write_2bytes(4);
    stream.write_4bytes(ssrc);
    stream.write_1bytes(4);
    stream.write_1bytes(0);
    stream.write_2bytes(2);
    stream.write_4bytes(cur_ntp.ntp_second_);
    stream.write_4bytes(cur_ntp.ntp_fractions_);

    return send_rtcp(stream.data(), stream.pos());
}

srs_error_t SrsRtcConnection::send_rtcp_fb_pli(uint32_t ssrc, const SrsContextId& cid_of_subscriber)
{
    ++_srs_pps_srtcps->sugar;

    char buf[kRtpPacketSize];
    SrsBuffer stream(buf, sizeof(buf));
    stream.write_1bytes(0x81);
    stream.write_1bytes(kPsFb);
    stream.write_2bytes(2);
    stream.write_4bytes(ssrc);
    stream.write_4bytes(ssrc);

    uint32_t nn = 0;
    if (pli_epp->can_print(ssrc, &nn)) {
        srs_trace("RTC: Request PLI ssrc=%u, play=[%s], count=%u/%u, bytes=%uB", ssrc, cid_of_subscriber.c_str(),
            nn, pli_epp->nn_count, stream.pos());
    }

    if (_srs_blackhole->blackhole) {
        _srs_blackhole->sendto(stream.data(), stream.pos());
    }

    return send_rtcp(stream.data(), stream.pos());
}

void SrsRtcConnection::simulate_nack_drop(int nn)
{
    for(map<string, SrsRtcPublishStream*>::iterator it = publishers_.begin(); it != publishers_.end(); ++it) {
        SrsRtcPublishStream* publisher = it->second;
        publisher->simulate_nack_drop(nn);
    }

    nn_simulate_player_nack_drop = nn;
}

void SrsRtcConnection::simulate_player_drop_packet(SrsRtpHeader* h, int nn_bytes)
{
    srs_warn("RTC: NACK simulator #%d player drop seq=%u, ssrc=%u, ts=%u, %d bytes", nn_simulate_player_nack_drop,
        h->get_sequence(), h->get_ssrc(), h->get_timestamp(),
        nn_bytes);

    nn_simulate_player_nack_drop--;
}

srs_error_t SrsRtcConnection::do_send_packet(SrsRtpPacket* pkt)
{
    srs_error_t err = srs_success;

    // For this message, select the first iovec.
    iovec* iov = cache_iov_;
    iov->iov_len = kRtpPacketSize;
    cache_buffer_->skip(-1 * cache_buffer_->pos());

    // Marshal packet to bytes in iovec.
    if (true) {
        if ((err = pkt->encode(cache_buffer_)) != srs_success) {
            return srs_error_wrap(err, "encode packet");
        }
        iov->iov_len = cache_buffer_->pos();
    }

    // Cipher RTP to SRTP packet.
    if (true) {
        int nn_encrypt = (int)iov->iov_len;
        if ((err = transport_->protect_rtp(iov->iov_base, &nn_encrypt)) != srs_success) {
            return srs_error_wrap(err, "srtp protect");
        }
        iov->iov_len = (size_t)nn_encrypt;
    }

    // For NACK simulator, drop packet.
    if (nn_simulate_player_nack_drop) {
        simulate_player_drop_packet(&pkt->header, (int)iov->iov_len);
        iov->iov_len = 0;
        return err;
    }

    ++_srs_pps_srtps->sugar;

    // TODO: FIXME: Handle error.
    sendonly_skt->sendto(iov->iov_base, iov->iov_len, 0);

    // Detail log, should disable it in release version.
    srs_info("RTC: SEND PT=%u, SSRC=%#x, SEQ=%u, Time=%u, %u/%u bytes", pkt->header.get_payload_type(), pkt->header.get_ssrc(),
        pkt->header.get_sequence(), pkt->header.get_timestamp(), pkt->nb_bytes(), iov->iov_len);

    return err;
}

void SrsRtcConnection::set_all_tracks_status(std::string stream_uri, bool is_publish, bool status)
{
    // For publishers.
    if (is_publish) {
        map<string, SrsRtcPublishStream*>::iterator it = publishers_.find(stream_uri);
        if (publishers_.end() == it) {
            return;
        }

        SrsRtcPublishStream* publisher = it->second;
        publisher->set_all_tracks_status(status);
        return;
    }

    // For players.
    map<string, SrsRtcPlayStream*>::iterator it = players_.find(stream_uri);
    if (players_.end() == it) {
        return;
    }

    SrsRtcPlayStream* player = it->second;
    player->set_all_tracks_status(status);
}

#ifdef SRS_OSX
// These functions are similar to the older byteorder(3) family of functions.
// For example, be32toh() is identical to ntohl().
// @see https://linux.die.net/man/3/be32toh
#define be32toh ntohl
#endif

srs_error_t SrsRtcConnection::on_binding_request(SrsStunPacket* r)
{
    srs_error_t err = srs_success;

    ++_srs_pps_sstuns->sugar;

    bool strict_check = _srs_config->get_rtc_stun_strict_check(req_->vhost);
    if (strict_check && r->get_ice_controlled()) {
        // @see: https://tools.ietf.org/html/draft-ietf-ice-rfc5245bis-00#section-6.1.3.1
        // TODO: Send 487 (Role Conflict) error response.
        return srs_error_new(ERROR_RTC_STUN, "Peer must not in ice-controlled role in ice-lite mode.");
    }

    SrsStunPacket stun_binding_response;
    char buf[kRtpPacketSize];
    SrsBuffer* stream = new SrsBuffer(buf, sizeof(buf));
    SrsAutoFree(SrsBuffer, stream);

    stun_binding_response.set_message_type(BindingResponse);
    stun_binding_response.set_local_ufrag(r->get_remote_ufrag());
    stun_binding_response.set_remote_ufrag(r->get_local_ufrag());
    stun_binding_response.set_transcation_id(r->get_transcation_id());
    // FIXME: inet_addr is deprecated, IPV6 support
    stun_binding_response.set_mapped_address(be32toh(inet_addr(sendonly_skt->get_peer_ip().c_str())));
    stun_binding_response.set_mapped_port(sendonly_skt->get_peer_port());

    if ((err = stun_binding_response.encode(get_local_sdp()->get_ice_pwd(), stream)) != srs_success) {
        return srs_error_wrap(err, "stun binding response encode failed");
    }

    if ((err = sendonly_skt->sendto(stream->data(), stream->pos(), 0)) != srs_success) {
        return srs_error_wrap(err, "stun binding response send failed");
    }

    if (state_ == WAITING_STUN) {
        state_ = DOING_DTLS_HANDSHAKE;
        // TODO: FIXME: Add cost.
        srs_trace("RTC: session STUN done, waiting DTLS handshake.");

        if((err = transport_->start_active_handshake()) != srs_success) {
            return srs_error_wrap(err, "fail to dtls handshake");
        }
    }

    if (_srs_blackhole->blackhole) {
        _srs_blackhole->sendto(stream->data(), stream->pos());
    }

    return err;
}

bool srs_sdp_has_h264_profile(const SrsMediaPayloadType& payload_type, const string& profile)
{
    srs_error_t err = srs_success;

    if (payload_type.format_specific_param_.empty()) {
        return false;
    }

    H264SpecificParam h264_param;
    if ((err = srs_parse_h264_fmtp(payload_type.format_specific_param_, h264_param)) != srs_success) {
        srs_error_reset(err);
        return false;
    }

    if (h264_param.profile_level_id == profile) {
        return true;
    }

    return false;
}

// For example, 42001f 42e01f, see https://blog.csdn.net/epubcn/article/details/102802108
bool srs_sdp_has_h264_profile(const SrsSdp& sdp, const string& profile)
{
    for (size_t i = 0; i < sdp.media_descs_.size(); ++i) {
        const SrsMediaDesc& desc = sdp.media_descs_[i];
        if (!desc.is_video()) {
            continue;
        }

        std::vector<SrsMediaPayloadType> payloads = desc.find_media_with_encoding_name("H264");
        if (payloads.empty()) {
            continue;
        }

        for (std::vector<SrsMediaPayloadType>::iterator it = payloads.begin(); it != payloads.end(); ++it) {
            const SrsMediaPayloadType& payload_type = *it;
            if (srs_sdp_has_h264_profile(payload_type, profile)) {
                return true;
            }
        }
    }

    return false;
}

srs_error_t SrsRtcConnection::negotiate_publish_capability(SrsRtcUserConfig* ruc, SrsRtcSourceDescription* stream_desc)
{
    srs_error_t err = srs_success;

    if (!stream_desc) {
        return srs_error_new(ERROR_RTC_SDP_EXCHANGE, "stream description is NULL");
    }

    SrsRequest* req = ruc->req_;
    const SrsSdp& remote_sdp = ruc->remote_sdp_;

    bool nack_enabled = _srs_config->get_rtc_nack_enabled(req->vhost);
    bool twcc_enabled = _srs_config->get_rtc_twcc_enabled(req->vhost);
    // TODO: FIME: Should check packetization-mode=1 also.
    bool has_42e01f = srs_sdp_has_h264_profile(remote_sdp, "42e01f");

    for (int i = 0; i < (int)remote_sdp.media_descs_.size(); ++i) {
        const SrsMediaDesc& remote_media_desc = remote_sdp.media_descs_.at(i);

        SrsRtcTrackDescription* track_desc = new SrsRtcTrackDescription();
        SrsAutoFree(SrsRtcTrackDescription, track_desc);

        track_desc->set_direction("recvonly");
        track_desc->set_mid(remote_media_desc.mid_);
        // Whether feature enabled in remote extmap.
        int remote_twcc_id = 0;
        if (true) {
            map<int, string> extmaps = remote_media_desc.get_extmaps();
            for(map<int, string>::iterator it = extmaps.begin(); it != extmaps.end(); ++it) {
                if (it->second == kTWCCExt) {
                    remote_twcc_id = it->first;
                    break;
                }
            }
        }

        if (twcc_enabled && remote_twcc_id) {
            track_desc->add_rtp_extension_desc(remote_twcc_id, kTWCCExt);
        }

        if (remote_media_desc.is_audio()) {
            // TODO: check opus format specific param
            std::vector<SrsMediaPayloadType> payloads = remote_media_desc.find_media_with_encoding_name("opus");
            if (payloads.empty()) {
                return srs_error_new(ERROR_RTC_SDP_EXCHANGE, "no valid found opus payload type");
            }

            for (int j = 0; j < (int)payloads.size(); j++) {
                const SrsMediaPayloadType& payload = payloads.at(j);

                // if the payload is opus, and the encoding_param_ is channel
                SrsAudioPayload* audio_payload = new SrsAudioPayload(payload.payload_type_, payload.encoding_name_, payload.clock_rate_, ::atol(payload.encoding_param_.c_str()));
                audio_payload->set_opus_param_desc(payload.format_specific_param_);

                // TODO: FIXME: Only support some transport algorithms.
                for (int k = 0; k < (int)payload.rtcp_fb_.size(); ++k) {
                    const string& rtcp_fb = payload.rtcp_fb_.at(k);

                    if (nack_enabled) {
                        if (rtcp_fb == "nack" || rtcp_fb == "nack pli") {
                            audio_payload->rtcp_fbs_.push_back(rtcp_fb);
                        }
                    }
                    if (twcc_enabled && remote_twcc_id) {
                        if (rtcp_fb == "transport-cc") {
                            audio_payload->rtcp_fbs_.push_back(rtcp_fb);
                        }
                    }
                }

                track_desc->type_ = "audio";
                track_desc->set_codec_payload((SrsCodecPayload*)audio_payload);
                // Only choose one match opus codec.
                break;
            }
        } else if (remote_media_desc.is_video() && ruc->codec_ == "av1") {
            std::vector<SrsMediaPayloadType> payloads = remote_media_desc.find_media_with_encoding_name("AV1X");
            if (payloads.empty()) {
                return srs_error_new(ERROR_RTC_SDP_EXCHANGE, "no found valid AV1 payload type");
            }

            for (int j = 0; j < (int)payloads.size(); j++) {
                const SrsMediaPayloadType& payload = payloads.at(j);

                // Generate video payload for av1.
                SrsVideoPayload* video_payload = new SrsVideoPayload(payload.payload_type_, payload.encoding_name_, payload.clock_rate_);

                // TODO: FIXME: Only support some transport algorithms.
                for (int k = 0; k < (int)payload.rtcp_fb_.size(); ++k) {
                    const string& rtcp_fb = payload.rtcp_fb_.at(k);

                    if (nack_enabled) {
                        if (rtcp_fb == "nack" || rtcp_fb == "nack pli") {
                            video_payload->rtcp_fbs_.push_back(rtcp_fb);
                        }
                    }
                    if (twcc_enabled && remote_twcc_id) {
                        if (rtcp_fb == "transport-cc") {
                            video_payload->rtcp_fbs_.push_back(rtcp_fb);
                        }
                    }
                }

                track_desc->type_ = "video";
                track_desc->set_codec_payload((SrsCodecPayload*)video_payload);
                break;
            }
        } else if (remote_media_desc.is_video()) {
            std::vector<SrsMediaPayloadType> payloads = remote_media_desc.find_media_with_encoding_name("H264");
            if (payloads.empty()) {
                return srs_error_new(ERROR_RTC_SDP_EXCHANGE, "no found valid H.264 payload type");
            }

            std::deque<SrsMediaPayloadType> backup_payloads;
            for (int j = 0; j < (int)payloads.size(); j++) {
                const SrsMediaPayloadType& payload = payloads.at(j);

                if (payload.format_specific_param_.empty()) {
                    backup_payloads.push_front(payload);
                    continue;
                }
                H264SpecificParam h264_param;
                if ((err = srs_parse_h264_fmtp(payload.format_specific_param_, h264_param)) != srs_success) {
                    srs_error_reset(err); continue;
                }

                // If not exists 42e01f, we pick up any profile such as 42001f.
                bool profile_matched = (!has_42e01f || h264_param.profile_level_id == "42e01f");

                // Try to pick the "best match" H.264 payload type.
                if (profile_matched && h264_param.packetization_mode == "1" && h264_param.level_asymmerty_allow == "1") {
                    // if the playload is opus, and the encoding_param_ is channel
                    SrsVideoPayload* video_payload = new SrsVideoPayload(payload.payload_type_, payload.encoding_name_, payload.clock_rate_);
                    video_payload->set_h264_param_desc(payload.format_specific_param_);

                    // TODO: FIXME: Only support some transport algorithms.
                    for (int k = 0; k < (int)payload.rtcp_fb_.size(); ++k) {
                        const string& rtcp_fb = payload.rtcp_fb_.at(k);

                        if (nack_enabled) {
                            if (rtcp_fb == "nack" || rtcp_fb == "nack pli") {
                                video_payload->rtcp_fbs_.push_back(rtcp_fb);
                            }
                        }
                        if (twcc_enabled && remote_twcc_id) {
                            if (rtcp_fb == "transport-cc") {
                                video_payload->rtcp_fbs_.push_back(rtcp_fb);
                            }
                        }
                    }

                    track_desc->type_ = "video";
                    track_desc->set_codec_payload((SrsCodecPayload*)video_payload);
                    // Only choose first match H.264 payload type.
                    break;
                }

                backup_payloads.push_back(payload);
            }

            // Try my best to pick at least one media payload type.
            if (!track_desc->media_ && ! backup_payloads.empty()) {
                const SrsMediaPayloadType& payload = backup_payloads.front();

                // if the playload is opus, and the encoding_param_ is channel
                SrsVideoPayload* video_payload = new SrsVideoPayload(payload.payload_type_, payload.encoding_name_, payload.clock_rate_);

                // TODO: FIXME: Only support some transport algorithms.
                for (int k = 0; k < (int)payload.rtcp_fb_.size(); ++k) {
                    const string& rtcp_fb = payload.rtcp_fb_.at(k);

                    if (nack_enabled) {
                        if (rtcp_fb == "nack" || rtcp_fb == "nack pli") {
                            video_payload->rtcp_fbs_.push_back(rtcp_fb);
                        }
                    }

                    if (twcc_enabled && remote_twcc_id) {
                        if (rtcp_fb == "transport-cc") {
                            video_payload->rtcp_fbs_.push_back(rtcp_fb);
                        }
                    }
                }

                track_desc->set_codec_payload((SrsCodecPayload*)video_payload);
                srs_warn("choose backup H.264 payload type=%d", payload.payload_type_);
            }

            // TODO: FIXME: Support RRTR?
            //local_media_desc.payload_types_.back().rtcp_fb_.push_back("rrtr");
        }

        // TODO: FIXME: use one parse payload from sdp.

        track_desc->create_auxiliary_payload(remote_media_desc.find_media_with_encoding_name("red"));
        track_desc->create_auxiliary_payload(remote_media_desc.find_media_with_encoding_name("rtx"));
        track_desc->create_auxiliary_payload(remote_media_desc.find_media_with_encoding_name("ulpfec"));

        std::string track_id;
        for (int j = 0; j < (int)remote_media_desc.ssrc_infos_.size(); ++j) {
            const SrsSSRCInfo& ssrc_info = remote_media_desc.ssrc_infos_.at(j);

            // ssrc have same track id, will be description in the same track description.
            if(track_id != ssrc_info.msid_tracker_) {
                SrsRtcTrackDescription* track_desc_copy = track_desc->copy();
                track_desc_copy->ssrc_ = ssrc_info.ssrc_;
                track_desc_copy->id_ = ssrc_info.msid_tracker_;
                track_desc_copy->msid_ = ssrc_info.msid_;

                if (remote_media_desc.is_audio() && !stream_desc->audio_track_desc_) {
                    stream_desc->audio_track_desc_ = track_desc_copy;
                } else if (remote_media_desc.is_video()) {
                    stream_desc->video_track_descs_.push_back(track_desc_copy);
                }
            }
            track_id = ssrc_info.msid_tracker_;
        }

        // set track fec_ssrc and rtx_ssrc
        for (int j = 0; j < (int)remote_media_desc.ssrc_groups_.size(); ++j) {
            const SrsSSRCGroup& ssrc_group = remote_media_desc.ssrc_groups_.at(j);

            SrsRtcTrackDescription* track_desc = stream_desc->find_track_description_by_ssrc(ssrc_group.ssrcs_[0]);
            if (!track_desc) {
                continue;
            }

            if (ssrc_group.semantic_ == "FID") {
                track_desc->set_rtx_ssrc(ssrc_group.ssrcs_[1]);
            } else if (ssrc_group.semantic_ == "FEC") {
                track_desc->set_fec_ssrc(ssrc_group.ssrcs_[1]);
            }
        }
    }

    return err;
}

srs_error_t SrsRtcConnection::generate_publish_local_sdp(SrsRequest* req, SrsSdp& local_sdp, SrsRtcSourceDescription* stream_desc, bool unified_plan)
{
    srs_error_t err = srs_success;

    if (!stream_desc) {
        return srs_error_new(ERROR_RTC_SDP_EXCHANGE, "stream description is NULL");
    }

    local_sdp.version_ = "0";

    local_sdp.username_        = RTMP_SIG_SRS_SERVER;
    local_sdp.session_id_      = srs_int2str((int64_t)this);
    local_sdp.session_version_ = "2";
    local_sdp.nettype_         = "IN";
    local_sdp.addrtype_        = "IP4";
    local_sdp.unicast_address_ = "0.0.0.0";

    local_sdp.session_name_ = "SRSPublishSession";

    local_sdp.msid_semantic_ = "WMS";
    std::string stream_id = req->app + "/" + req->stream;
    local_sdp.msids_.push_back(stream_id);

    local_sdp.group_policy_ = "BUNDLE";

    // generate audio media desc
    if (stream_desc->audio_track_desc_) {
        SrsRtcTrackDescription* audio_track = stream_desc->audio_track_desc_;

        local_sdp.media_descs_.push_back(SrsMediaDesc("audio"));
        SrsMediaDesc& local_media_desc = local_sdp.media_descs_.back();

        local_media_desc.port_ = 9;
        local_media_desc.protos_ = "UDP/TLS/RTP/SAVPF";
        local_media_desc.rtcp_mux_ = true;
        local_media_desc.rtcp_rsize_ = true;

        local_media_desc.mid_ = audio_track->mid_;
        local_sdp.groups_.push_back(local_media_desc.mid_);

        // anwer not need set stream_id and track_id;
        // local_media_desc.msid_ = stream_id;
        // local_media_desc.msid_tracker_ = audio_track->id_;
        local_media_desc.extmaps_ = audio_track->extmaps_;

        if (audio_track->direction_ == "recvonly") {
            local_media_desc.recvonly_ = true;
        } else if (audio_track->direction_ == "sendonly") {
            local_media_desc.sendonly_ = true;
        } else if (audio_track->direction_ == "sendrecv") {
            local_media_desc.sendrecv_ = true;
        } else if (audio_track->direction_ == "inactive") {
            local_media_desc.inactive_ = true;
        }

        SrsAudioPayload* payload = (SrsAudioPayload*)audio_track->media_;
        local_media_desc.payload_types_.push_back(payload->generate_media_payload_type());
    }

    for (int i = 0;  i < (int)stream_desc->video_track_descs_.size(); ++i) {
        SrsRtcTrackDescription* video_track = stream_desc->video_track_descs_.at(i);

        local_sdp.media_descs_.push_back(SrsMediaDesc("video"));
        SrsMediaDesc& local_media_desc = local_sdp.media_descs_.back();

        local_media_desc.port_ = 9;
        local_media_desc.protos_ = "UDP/TLS/RTP/SAVPF";
        local_media_desc.rtcp_mux_ = true;
        local_media_desc.rtcp_rsize_ = true;

        local_media_desc.mid_ = video_track->mid_;
        local_sdp.groups_.push_back(local_media_desc.mid_);

        // anwer not need set stream_id and track_id;
        //local_media_desc.msid_ = stream_id;
        //local_media_desc.msid_tracker_ = video_track->id_;
        local_media_desc.extmaps_ = video_track->extmaps_;

        if (video_track->direction_ == "recvonly") {
            local_media_desc.recvonly_ = true;
        } else if (video_track->direction_ == "sendonly") {
            local_media_desc.sendonly_ = true;
        } else if (video_track->direction_ == "sendrecv") {
            local_media_desc.sendrecv_ = true;
        } else if (video_track->direction_ == "inactive") {
            local_media_desc.inactive_ = true;
        }

        SrsVideoPayload* payload = (SrsVideoPayload*)video_track->media_;
        local_media_desc.payload_types_.push_back(payload->generate_media_payload_type());

        if (video_track->red_) {
            SrsRedPayload* payload = (SrsRedPayload*)video_track->red_;
            local_media_desc.payload_types_.push_back(payload->generate_media_payload_type());
        }

        if(!unified_plan) {
            // For PlanB, only need media desc info, not ssrc info;
            break;
        }
    }

    return err;
}

srs_error_t SrsRtcConnection::negotiate_play_capability(SrsRtcUserConfig* ruc, std::map<uint32_t, SrsRtcTrackDescription*>& sub_relations)
{
    srs_error_t err = srs_success;

    SrsRequest* req = ruc->req_;
    const SrsSdp& remote_sdp = ruc->remote_sdp_;

    bool nack_enabled = _srs_config->get_rtc_nack_enabled(req->vhost);
    bool twcc_enabled = _srs_config->get_rtc_twcc_enabled(req->vhost);
    // TODO: FIME: Should check packetization-mode=1 also.
    bool has_42e01f = srs_sdp_has_h264_profile(remote_sdp, "42e01f");

    SrsRtcSource* source = NULL;
    if ((err = _srs_rtc_sources->fetch_or_create(req, &source)) != srs_success) {
        return srs_error_wrap(err, "fetch rtc source");
    }

    for (int i = 0; i < (int)remote_sdp.media_descs_.size(); ++i) {
        const SrsMediaDesc& remote_media_desc = remote_sdp.media_descs_.at(i);

        // Whether feature enabled in remote extmap.
        int remote_twcc_id = 0;
        if (true) {
            map<int, string> extmaps = remote_media_desc.get_extmaps();
            for(map<int, string>::iterator it = extmaps.begin(); it != extmaps.end(); ++it) {
                if (it->second == kTWCCExt) {
                    remote_twcc_id = it->first;
                    break;
                }
            }
        }

        std::vector<SrsRtcTrackDescription*> track_descs;
        SrsMediaPayloadType remote_payload(0);
        if (remote_media_desc.is_audio()) {
            // TODO: check opus format specific param
            // get the audio track frome source
            track_descs = source->get_track_desc("audio", "");
            if(track_descs.size() == 0) {
                srs_warn("no audio track in rtc source!");
                continue;
            }

            vector<SrsMediaPayloadType> payloads = remote_media_desc.find_media_with_encoding_name(track_descs.at(0)->media_->name_);
            if (payloads.empty()) {
                return srs_error_new(ERROR_RTC_SDP_EXCHANGE, "no valid found audio payload type");
            }

            remote_payload = payloads.at(0);
        } else if (remote_media_desc.is_video() && ruc->codec_ == "av1") {
            std::vector<SrsMediaPayloadType> payloads = remote_media_desc.find_media_with_encoding_name("AV1X");
            if (payloads.empty()) {
                return srs_error_new(ERROR_RTC_SDP_EXCHANGE, "no found valid AV1 payload type");
            }

            remote_payload = payloads.at(0);
            track_descs = source->get_track_desc("video", "AV1X");
        } else if (remote_media_desc.is_video()) {
            // TODO: check opus format specific param
            vector<SrsMediaPayloadType> payloads = remote_media_desc.find_media_with_encoding_name("H264");
            if (payloads.empty()) {
                return srs_error_new(ERROR_RTC_SDP_EXCHANGE, "no valid found h264 payload type");
            }

            remote_payload = payloads.at(0);
            for (int j = 0; j < (int)payloads.size(); j++) {
                const SrsMediaPayloadType& payload = payloads.at(j);

                // If exists 42e01f profile, choose it; otherwise, use the first payload.
                // TODO: FIME: Should check packetization-mode=1 also.
                if (!has_42e01f || srs_sdp_has_h264_profile(payload, "42e01f")) {
                    remote_payload = payload;
                    break;
                }
            }

            track_descs = source->get_track_desc("video", "H264");
        }

        for (int j = 0; j < (int)track_descs.size(); ++j) {
            SrsRtcTrackDescription* track = track_descs.at(j)->copy();

            // We should clear the extmaps of source(publisher).
            // @see https://github.com/ossrs/srs/issues/2370
            track->extmaps_.clear();
			
            // We should clear the rtcp_fbs of source(publisher).
            // @see https://github.com/ossrs/srs/issues/2371
            track->media_->rtcp_fbs_.clear();

            // Use remote/source/offer PayloadType.
            track->media_->pt_of_publisher_ = track->media_->pt_;
            track->media_->pt_ = remote_payload.payload_type_;

            vector<SrsMediaPayloadType> red_pts = remote_media_desc.find_media_with_encoding_name("red");
            if (!red_pts.empty() && track->red_) {
                SrsMediaPayloadType red_pt = red_pts.at(0);

                track->red_->pt_of_publisher_ = track->red_->pt_;
                track->red_->pt_ = red_pt.payload_type_;
            }

            track->mid_ = remote_media_desc.mid_;
            uint32_t publish_ssrc = track->ssrc_;

            vector<string> rtcp_fb;
            remote_payload.rtcp_fb_.swap(rtcp_fb);
            for (int j = 0; j < (int)rtcp_fb.size(); j++) {
                if (nack_enabled) {
                    if (rtcp_fb.at(j) == "nack" || rtcp_fb.at(j) == "nack pli") {
                        track->media_->rtcp_fbs_.push_back(rtcp_fb.at(j));
                    }
                }
                if (twcc_enabled && remote_twcc_id) {
                    if (rtcp_fb.at(j) == "transport-cc") {
                        track->media_->rtcp_fbs_.push_back(rtcp_fb.at(j));
                    }
                    track->add_rtp_extension_desc(remote_twcc_id, kTWCCExt);
                }
            }

            track->ssrc_ = SrsRtcSSRCGenerator::instance()->generate_ssrc();
            
            // TODO: FIXME: set audio_payload rtcp_fbs_,
            // according by whether downlink is support transport algorithms.
            // TODO: FIXME: if we support downlink RTX, MUST assign rtx_ssrc_, rtx_pt, rtx_apt
            // not support rtx
            if (true) {
                srs_freep(track->rtx_);
                track->rtx_ssrc_ = 0;
            }

            track->set_direction("sendonly");
            sub_relations.insert(make_pair(publish_ssrc, track));
        }
    }

    return err;
}

void video_track_generate_play_offer(SrsRtcTrackDescription* track, string mid, SrsSdp& local_sdp)
{
    local_sdp.media_descs_.push_back(SrsMediaDesc("video"));
    SrsMediaDesc& local_media_desc = local_sdp.media_descs_.back();

    local_media_desc.port_ = 9;
    local_media_desc.protos_ = "UDP/TLS/RTP/SAVPF";
    local_media_desc.rtcp_mux_ = true;
    local_media_desc.rtcp_rsize_ = true;

    local_media_desc.extmaps_ = track->extmaps_;

    // If mid not duplicated, use mid_ of track. Otherwise, use transformed mid.
    if (true) {
        bool mid_duplicated = false;
        for (int i = 0; i < (int)local_sdp.groups_.size(); ++i) {
            string& existed_mid = local_sdp.groups_.at(i);
            if(existed_mid == track->mid_) {
                mid_duplicated = true;
                break;
            }
        }
        if (mid_duplicated) {
            local_media_desc.mid_ = mid;
        } else {
            local_media_desc.mid_ = track->mid_;
        }
        local_sdp.groups_.push_back(local_media_desc.mid_);
    }

    if (track->direction_ == "recvonly") {
        local_media_desc.recvonly_ = true;
    } else if (track->direction_ == "sendonly") {
        local_media_desc.sendonly_ = true;
    } else if (track->direction_ == "sendrecv") {
        local_media_desc.sendrecv_ = true;
    } else if (track->direction_ == "inactive") {
        local_media_desc.inactive_ = true;
    }

    SrsVideoPayload* payload = (SrsVideoPayload*)track->media_;

    local_media_desc.payload_types_.push_back(payload->generate_media_payload_type());

    if (track->red_) {
        SrsRedPayload* red_payload = (SrsRedPayload*)track->red_;
        local_media_desc.payload_types_.push_back(red_payload->generate_media_payload_type());
    }
}

srs_error_t SrsRtcConnection::generate_play_local_sdp(SrsRequest* req, SrsSdp& local_sdp, SrsRtcSourceDescription* stream_desc, bool unified_plan)
{
    srs_error_t err = srs_success;

    if (!stream_desc) {
        return srs_error_new(ERROR_RTC_SDP_EXCHANGE, "stream description is NULL");
    }

    local_sdp.version_ = "0";

    local_sdp.username_        = RTMP_SIG_SRS_SERVER;
    local_sdp.session_id_      = srs_int2str((int64_t)this);
    local_sdp.session_version_ = "2";
    local_sdp.nettype_         = "IN";
    local_sdp.addrtype_        = "IP4";
    local_sdp.unicast_address_ = "0.0.0.0";

    local_sdp.session_name_ = "SRSPlaySession";

    local_sdp.msid_semantic_ = "WMS";
    std::string stream_id = req->app + "/" + req->stream;
    local_sdp.msids_.push_back(stream_id);

    local_sdp.group_policy_ = "BUNDLE";

    std::string cname = srs_random_str(16);

    // generate audio media desc
    if (stream_desc->audio_track_desc_) {
        SrsRtcTrackDescription* audio_track = stream_desc->audio_track_desc_;

        local_sdp.media_descs_.push_back(SrsMediaDesc("audio"));
        SrsMediaDesc& local_media_desc = local_sdp.media_descs_.back();

        local_media_desc.port_ = 9;
        local_media_desc.protos_ = "UDP/TLS/RTP/SAVPF";
        local_media_desc.rtcp_mux_ = true;
        local_media_desc.rtcp_rsize_ = true;

        local_media_desc.extmaps_ = audio_track->extmaps_;

        local_media_desc.mid_ = audio_track->mid_;
        local_sdp.groups_.push_back(local_media_desc.mid_);

        if (audio_track->direction_ == "recvonly") {
            local_media_desc.recvonly_ = true;
        } else if (audio_track->direction_ == "sendonly") {
            local_media_desc.sendonly_ = true;
        } else if (audio_track->direction_ == "sendrecv") {
            local_media_desc.sendrecv_ = true;
        } else if (audio_track->direction_ == "inactive") {
            local_media_desc.inactive_ = true;
        }

        if (audio_track->red_) {
            SrsRedPayload* red_payload = (SrsRedPayload*)audio_track->red_;
            local_media_desc.payload_types_.push_back(red_payload->generate_media_payload_type());
        }

        SrsAudioPayload* payload = (SrsAudioPayload*)audio_track->media_;
        local_media_desc.payload_types_.push_back(payload->generate_media_payload_type()); 

        //TODO: FIXME: add red, rtx, ulpfec..., payload_types_.
        //local_media_desc.payload_types_.push_back(payload->generate_media_payload_type());

        local_media_desc.ssrc_infos_.push_back(SrsSSRCInfo(audio_track->ssrc_, cname, audio_track->msid_, audio_track->id_));

        if (audio_track->rtx_) {
            std::vector<uint32_t> group_ssrcs;
            group_ssrcs.push_back(audio_track->ssrc_);
            group_ssrcs.push_back(audio_track->rtx_ssrc_);

            local_media_desc.ssrc_groups_.push_back(SrsSSRCGroup("FID", group_ssrcs));
            local_media_desc.ssrc_infos_.push_back(SrsSSRCInfo(audio_track->rtx_ssrc_, cname, audio_track->msid_, audio_track->id_));
        }

        if (audio_track->ulpfec_) {
            std::vector<uint32_t> group_ssrcs;
            group_ssrcs.push_back(audio_track->ssrc_);
            group_ssrcs.push_back(audio_track->fec_ssrc_);
            local_media_desc.ssrc_groups_.push_back(SrsSSRCGroup("FEC", group_ssrcs));

            local_media_desc.ssrc_infos_.push_back(SrsSSRCInfo(audio_track->fec_ssrc_, cname, audio_track->msid_, audio_track->id_));
        }
    }

    for (int i = 0;  i < (int)stream_desc->video_track_descs_.size(); ++i) {
        SrsRtcTrackDescription* track = stream_desc->video_track_descs_[i];

        if (!unified_plan) {
            // for plan b, we only add one m= for video track.
            if (i == 0) {
                video_track_generate_play_offer(track, "video-" +srs_int2str(i), local_sdp);
            }
        } else {
            // unified plan SDP, generate a m= for each video track.
            video_track_generate_play_offer(track, "video-" +srs_int2str(i), local_sdp);
        }

        SrsMediaDesc& local_media_desc = local_sdp.media_descs_.back();
        local_media_desc.ssrc_infos_.push_back(SrsSSRCInfo(track->ssrc_, cname, track->msid_, track->id_));

        if (track->rtx_ && track->rtx_ssrc_) {
            std::vector<uint32_t> group_ssrcs;
            group_ssrcs.push_back(track->ssrc_);
            group_ssrcs.push_back(track->rtx_ssrc_);

            local_media_desc.ssrc_groups_.push_back(SrsSSRCGroup("FID", group_ssrcs));
            local_media_desc.ssrc_infos_.push_back(SrsSSRCInfo(track->rtx_ssrc_, cname, track->msid_, track->id_));
        }

        if (track->ulpfec_ && track->fec_ssrc_) {
            std::vector<uint32_t> group_ssrcs;
            group_ssrcs.push_back(track->ssrc_);
            group_ssrcs.push_back(track->fec_ssrc_);
            local_media_desc.ssrc_groups_.push_back(SrsSSRCGroup("FEC", group_ssrcs));

            local_media_desc.ssrc_infos_.push_back(SrsSSRCInfo(track->fec_ssrc_, cname, track->msid_, track->id_));
        }
    }

    return err;
}

srs_error_t SrsRtcConnection::create_player(SrsRequest* req, std::map<uint32_t, SrsRtcTrackDescription*> sub_relations)
{
    srs_error_t err = srs_success;

    // Ignore if exists.
    if(players_.end() != players_.find(req->get_stream_url())) {
        return err;
    }

    SrsRtcPlayStream* player = new SrsRtcPlayStream(this, _srs_context->get_id());
    if ((err = player->initialize(req, sub_relations)) != srs_success) {
        srs_freep(player);
        return srs_error_wrap(err, "SrsRtcPlayStream init");
    }
    players_.insert(make_pair(req->get_stream_url(), player));

    // make map between ssrc and player for fastly searching
    for(map<uint32_t, SrsRtcTrackDescription*>::iterator it = sub_relations.begin(); it != sub_relations.end(); ++it) {
        SrsRtcTrackDescription* track_desc = it->second;
        map<uint32_t, SrsRtcPlayStream*>::iterator it_player = players_ssrc_map_.find(track_desc->ssrc_);
        if((players_ssrc_map_.end() != it_player) && (player != it_player->second)) {
            return srs_error_new(ERROR_RTC_DUPLICATED_SSRC, "duplicate ssrc %d, track id: %s",
                track_desc->ssrc_, track_desc->id_.c_str());
        }
        players_ssrc_map_[track_desc->ssrc_] = player;

        if(0 != track_desc->fec_ssrc_) {
            if(players_ssrc_map_.end() != players_ssrc_map_.find(track_desc->fec_ssrc_)) {
                return srs_error_new(ERROR_RTC_DUPLICATED_SSRC, "duplicate fec ssrc %d, track id: %s",
                    track_desc->fec_ssrc_, track_desc->id_.c_str());
            }
            players_ssrc_map_[track_desc->fec_ssrc_] = player;
        }

        if(0 != track_desc->rtx_ssrc_) {
            if(players_ssrc_map_.end() != players_ssrc_map_.find(track_desc->rtx_ssrc_)) {
                return srs_error_new(ERROR_RTC_DUPLICATED_SSRC, "duplicate rtx ssrc %d, track id: %s",
                    track_desc->rtx_ssrc_, track_desc->id_.c_str());
            }
            players_ssrc_map_[track_desc->rtx_ssrc_] = player;
        }
    }

    // TODO: FIXME: Support reload.
    // The TWCC ID is the ext-map ID in local SDP, and we set to enable GCC.
    // Whatever the ext-map, we will disable GCC when config disable it.
    int twcc_id = 0;
    if (true) {
        std::map<uint32_t, SrsRtcTrackDescription*>::iterator it = sub_relations.begin();
        while (it != sub_relations.end()) {
            if (it->second->type_ == "video") {
                SrsRtcTrackDescription* track = it->second;
                twcc_id = track->get_rtp_extension_id(kTWCCExt);
            }
            ++it;
        }
    }
    srs_trace("RTC connection player gcc=%d", twcc_id);

    // If DTLS done, start the player. Because maybe create some players after DTLS done.
    // For example, for single PC, we maybe start publisher when create it, because DTLS is done.
    if(ESTABLISHED == state_) {
        if(srs_success != (err = player->start())) {
            return srs_error_wrap(err, "start player");
        }
    }

    return err;
}

srs_error_t SrsRtcConnection::create_publisher(SrsRequest* req, SrsRtcSourceDescription* stream_desc)
{
    srs_error_t err = srs_success;

    srs_assert(stream_desc);

    // Ignore if exists.
    if(publishers_.end() != publishers_.find(req->get_stream_url())) {
        return err;
    }

    SrsRtcPublishStream* publisher = new SrsRtcPublishStream(this, _srs_context->get_id());
    if ((err = publisher->initialize(req, stream_desc)) != srs_success) {
        srs_freep(publisher);
        return srs_error_wrap(err, "rtc publisher init");
    }
    publishers_[req->get_stream_url()] = publisher;

    if(NULL != stream_desc->audio_track_desc_) {
        if(publishers_ssrc_map_.end() != publishers_ssrc_map_.find(stream_desc->audio_track_desc_->ssrc_)) {
            return srs_error_new(ERROR_RTC_DUPLICATED_SSRC, " duplicate ssrc %d, track id: %s",
                stream_desc->audio_track_desc_->ssrc_, stream_desc->audio_track_desc_->id_.c_str());
        }
        publishers_ssrc_map_[stream_desc->audio_track_desc_->ssrc_] = publisher;

        if(0 != stream_desc->audio_track_desc_->fec_ssrc_
                && stream_desc->audio_track_desc_->ssrc_ != stream_desc->audio_track_desc_->fec_ssrc_) {
            if(publishers_ssrc_map_.end() != publishers_ssrc_map_.find(stream_desc->audio_track_desc_->fec_ssrc_)) {
                return srs_error_new(ERROR_RTC_DUPLICATED_SSRC, " duplicate fec ssrc %d, track id: %s",
                    stream_desc->audio_track_desc_->fec_ssrc_, stream_desc->audio_track_desc_->id_.c_str());
            }
            publishers_ssrc_map_[stream_desc->audio_track_desc_->fec_ssrc_] = publisher;
        }

        if(0 != stream_desc->audio_track_desc_->rtx_ssrc_
                && stream_desc->audio_track_desc_->ssrc_ != stream_desc->audio_track_desc_->rtx_ssrc_) {
            if(publishers_ssrc_map_.end() != publishers_ssrc_map_.find(stream_desc->audio_track_desc_->rtx_ssrc_)) {
                return srs_error_new(ERROR_RTC_DUPLICATED_SSRC, " duplicate rtx ssrc %d, track id: %s",
                    stream_desc->audio_track_desc_->rtx_ssrc_, stream_desc->audio_track_desc_->id_.c_str());
            }
            publishers_ssrc_map_[stream_desc->audio_track_desc_->rtx_ssrc_] = publisher;
        }
    }

    for(int i = 0; i < (int)stream_desc->video_track_descs_.size(); ++i) {
        SrsRtcTrackDescription* track_desc = stream_desc->video_track_descs_.at(i);
        if(publishers_ssrc_map_.end() != publishers_ssrc_map_.find(track_desc->ssrc_)) {
            return srs_error_new(ERROR_RTC_DUPLICATED_SSRC, " duplicate ssrc %d, track id: %s",
                track_desc->ssrc_, track_desc->id_.c_str());
        }
        publishers_ssrc_map_[track_desc->ssrc_] = publisher;

        if(0 != track_desc->fec_ssrc_ && track_desc->ssrc_ != track_desc->fec_ssrc_) {
            if(publishers_ssrc_map_.end() != publishers_ssrc_map_.find(track_desc->fec_ssrc_)) {
                return srs_error_new(ERROR_RTC_DUPLICATED_SSRC, " duplicate fec ssrc %d, track id: %s",
                    track_desc->fec_ssrc_, track_desc->id_.c_str());
            }
            publishers_ssrc_map_[track_desc->fec_ssrc_] = publisher;
        }

        if(0 != track_desc->rtx_ssrc_ && track_desc->ssrc_ != track_desc->rtx_ssrc_) {
            if(publishers_ssrc_map_.end() != publishers_ssrc_map_.find(track_desc->rtx_ssrc_)) {
                return srs_error_new(ERROR_RTC_DUPLICATED_SSRC, " duplicate rtx ssrc %d, track id: %s",
                    track_desc->rtx_ssrc_, track_desc->id_.c_str());
            }
            publishers_ssrc_map_[track_desc->rtx_ssrc_] = publisher;
        }
    }

    if (_srs_rtc_hijacker) {
        if ((err = _srs_rtc_hijacker->on_create_publish(this, publisher, req)) != srs_success) {
            return srs_error_wrap(err, "on create publish");
        }
    }

    // If DTLS done, start the publisher. Because maybe create some publishers after DTLS done.
    // For example, for single PC, we maybe start publisher when create it, because DTLS is done.
    if(ESTABLISHED == state()) {
        if(srs_success != (err = publisher->start())) {
            return srs_error_wrap(err, "start publisher");
        }
    }

    return err;
}

ISrsRtcHijacker::ISrsRtcHijacker()
{
}

ISrsRtcHijacker::~ISrsRtcHijacker()
{
}

ISrsRtcHijacker* _srs_rtc_hijacker = NULL;

