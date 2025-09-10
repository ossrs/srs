//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_app_rtsp_conn.hpp>

using namespace std;

#include <sys/socket.h>

#include <sstream>

#include <srs_app_config.hpp>
#include <srs_app_http_hooks.hpp>
#include <srs_app_pithy_print.hpp>
#include <srs_app_rtsp_source.hpp>
#include <srs_app_security.hpp>
#include <srs_app_statistic.hpp>
#include <srs_app_utility.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_buffer.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_kbps.hpp>
#include <srs_kernel_log.hpp>
#include <srs_kernel_rtc_rtp.hpp>
#include <srs_protocol_kbps.hpp>
#include <srs_protocol_rtsp_stack.hpp>
#include <srs_protocol_st.hpp>
#include <srs_protocol_utility.hpp>

extern SrsPps *_srs_pps_snack;
extern SrsPps *_srs_pps_snack2;
extern SrsPps *_srs_pps_snack3;
extern SrsPps *_srs_pps_snack4;

extern SrsPps *_srs_pps_rnack;
extern SrsPps *_srs_pps_rnack2;

extern SrsPps *_srs_pps_pub;
extern SrsPps *_srs_pps_conn;

SrsRtspPlayStream::SrsRtspPlayStream(SrsRtspConnection *s, const SrsContextId &cid) : source_(new SrsRtspSource())
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
        std::map<uint32_t, SrsRtspAudioSendTrack *>::iterator it;
        for (it = audio_tracks_.begin(); it != audio_tracks_.end(); ++it) {
            srs_freep(it->second);
        }
    }

    if (true) {
        std::map<uint32_t, SrsRtspVideoSendTrack *>::iterator it;
        for (it = video_tracks_.begin(); it != video_tracks_.end(); ++it) {
            srs_freep(it->second);
        }
    }

    // update the statistic when client coveried.
    SrsStatistic *stat = SrsStatistic::instance();
    // TODO: FIXME: Should finger out the err.
    stat->on_disconnect(cid_.c_str(), srs_success);
}

srs_error_t SrsRtspPlayStream::initialize(ISrsRequest *req, std::map<uint32_t, SrsRtcTrackDescription *> sub_relations)
{
    srs_error_t err = srs_success;

    req_ = req->copy();

    // We must do stat the client before hooks, because hooks depends on it.
    SrsStatistic *stat = SrsStatistic::instance();
    if ((err = stat->on_client(cid_.c_str(), req_, session_, SrsRtcConnPlay)) != srs_success) {
        return srs_error_wrap(err, "RTSP: stat client");
    }

    if ((err = _srs_rtsp_sources->fetch_or_create(req_, source_)) != srs_success) {
        return srs_error_wrap(err, "RTSP: fetch source failed");
    }

    for (map<uint32_t, SrsRtcTrackDescription *>::iterator it = sub_relations.begin(); it != sub_relations.end(); ++it) {
        uint32_t ssrc = it->first;
        SrsRtcTrackDescription *desc = it->second;

        if (desc->type_ == "audio") {
            SrsRtspAudioSendTrack *track = new SrsRtspAudioSendTrack(session_, desc);
            audio_tracks_.insert(make_pair(ssrc, track));
        }

        if (desc->type_ == "video") {
            SrsRtspVideoSendTrack *track = new SrsRtspVideoSendTrack(session_, desc);
            video_tracks_.insert(make_pair(ssrc, track));
        }
    }

    return err;
}

// TODO: Remove it for RTSP?
void SrsRtspPlayStream::on_stream_change(SrsRtcSourceDescription *desc)
{
    if (!desc)
        return;

    // Refresh the relation for audio.
    // TODO: FIXME: Match by label?
    if (desc && desc->audio_track_desc_ && audio_tracks_.size() == 1) {
        if (!audio_tracks_.empty()) {
            uint32_t ssrc = desc->audio_track_desc_->ssrc_;
            SrsRtspAudioSendTrack *track = audio_tracks_.begin()->second;

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
            SrsRtcTrackDescription *vdesc = desc->video_track_descs_.at(0);
            uint32_t ssrc = vdesc->ssrc_;
            SrsRtspVideoSendTrack *track = video_tracks_.begin()->second;

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

const SrsContextId &SrsRtspPlayStream::context_id()
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

    SrsSharedPtr<SrsRtspSource> &source = source_;
    srs_assert(source.get());

    SrsRtspConsumer *consumer_raw = NULL;
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
        SrsRtpPacket *pkt = NULL;
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

srs_error_t SrsRtspPlayStream::send_packet(SrsRtpPacket *&pkt)
{
    srs_error_t err = srs_success;

    uint32_t ssrc = pkt->header.get_ssrc();

    // Try to find track from cache.
    SrsRtspSendTrack *track = NULL;
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
            map<uint32_t, SrsRtspAudioSendTrack *>::iterator it = audio_tracks_.find(ssrc);
            if (it != audio_tracks_.end()) {
                track = it->second;
            }
        } else {
            map<uint32_t, SrsRtspVideoSendTrack *>::iterator it = video_tracks_.find(ssrc);
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
        std::map<uint32_t, SrsRtspVideoSendTrack *>::iterator it;
        for (it = video_tracks_.begin(); it != video_tracks_.end(); ++it) {
            SrsRtspVideoSendTrack *track = it->second;

            bool previous = track->set_track_status(status);
            merged_log << "{track: " << track->get_track_id() << ", is_active: " << previous << "=>" << status << "},";
        }
    }

    // set audio track status
    if (true) {
        std::map<uint32_t, SrsRtspAudioSendTrack *>::iterator it;
        for (it = audio_tracks_.begin(); it != audio_tracks_.end(); ++it) {
            SrsRtspAudioSendTrack *track = it->second;

            bool previous = track->set_track_status(status);
            merged_log << "{track: " << track->get_track_id() << ", is_active: " << previous << "=>" << status << "},";
        }
    }

    srs_trace("RTSP: Init tracks %s ok", merged_log.str().c_str());
}

SrsRtspConnection::SrsRtspConnection(ISrsResourceManager *cm, ISrsProtocolReadWriter *skt, std::string cip, int port)
{
    manager_ = cm;
    cid_ = _srs_context->generate_id();
    _srs_context->set_id(cid_);

    // Initialize timeout management fields from SrsRtspConnection2
    last_stun_time = 0;
    session_timeout = 0;
    disposing_ = false;

    request_ = new SrsRequest();
    request_->ip_ = cip;
    ip_ = cip;
    port_ = port;
    rtsp_ = new SrsRtspStack(skt);
    trd_ = new SrsSTCoroutine("rtsp", this, _srs_context->get_id());

    // Initialize merged SrsRtspSession members
    skt_ = skt;
    source_ = NULL;
    player_ = NULL;

    cache_iov_ = new iovec();
    cache_iov_->iov_base = new char[kRtpPacketSize];
    cache_iov_->iov_len = kRtpPacketSize;
    cache_buffer_ = new SrsBuffer((char *)cache_iov_->iov_base, kRtpPacketSize);

    delta_ = new SrsEphemeralDelta();
    security_ = new SrsSecurity();

    _srs_rtsp_manager->subscribe(this);
}

SrsRtspConnection::~SrsRtspConnection()
{
    _srs_rtsp_manager->unsubscribe(this);

    srs_freep(request_);
    srs_freep(rtsp_);
    srs_freep(trd_);

    // Cleanup merged SrsRtspSession members
    for (std::map<uint32_t, SrsRtcTrackDescription *>::iterator it = tracks_.begin(); it != tracks_.end(); ++it) {
        srs_freep(it->second);
    }
    tracks_.clear();

    for (std::map<uint32_t, ISrsStreamWriter *>::iterator it = networks_.begin(); it != networks_.end(); ++it) {
        srs_freep(it->second);
    }
    networks_.clear();

    srs_freep(delta_);
    srs_freep(security_);
    srs_freep(player_);

    if (true) {
        char *iov_base = (char *)cache_iov_->iov_base;
        srs_freepa(iov_base);
        srs_freep(cache_iov_);
    }
    srs_freep(cache_buffer_);
}

srs_error_t SrsRtspConnection::do_send_packet(SrsRtpPacket *pkt)
{
    srs_error_t err = srs_success;

    uint32_t ssrc = pkt->header.get_ssrc();
    ISrsStreamWriter *network = networks_[ssrc];
    if (!network) {
        return srs_error_new(ERROR_RTSP_NO_TRACK, "network not found for ssrc: %u", ssrc);
    }

    iovec *iov = cache_iov_;
    cache_buffer_->skip(-1 * cache_buffer_->pos());

    // Marshal packet to bytes in iovec.
    if (true) {
        if ((err = pkt->encode(cache_buffer_)) != srs_success) {
            return srs_error_wrap(err, "encode packet");
        }
        iov->iov_len = cache_buffer_->pos();
    }

    ssize_t write = 0;
    if ((err = network->write(iov->iov_base, iov->iov_len, &write)) != srs_success) {
        return srs_error_wrap(err, "send rtp packet");
    }

    delta_->add_delta(0, write);

    return err;
}

ISrsKbpsDelta *SrsRtspConnection::delta()
{
    return delta_;
}

std::string SrsRtspConnection::desc()
{
    return "Rtsp";
}

const SrsContextId &SrsRtspConnection::get_id()
{
    return cid_;
}

std::string SrsRtspConnection::remote_ip()
{
    return ip_;
}

void SrsRtspConnection::expire()
{
    trd_->interrupt();
}

srs_error_t SrsRtspConnection::start()
{
    srs_error_t err = srs_success;

    if ((err = trd_->start()) != srs_success) {
        return srs_error_wrap(err, "coroutine");
    }

    return err;
}

srs_error_t SrsRtspConnection::cycle()
{
    srs_error_t err = srs_success;

    // Serve the client.
    err = do_cycle();

    // Update statistic when done.
    SrsStatistic *stat = SrsStatistic::instance();
    stat->kbps_add_delta(get_id().c_str(), delta());

    do_teardown();

    // Notify manager to remove it.
    // Note that we create this object, so we use manager to remove it.
    manager_->remove(this);

    // success.
    if (err == srs_success) {
        srs_trace("RTSP: client finished.");
        return err;
    }

    // It maybe success with message.
    if (srs_error_code(err) == ERROR_SUCCESS) {
        srs_trace("RTSP: client finished%s.", srs_error_summary(err).c_str());
        srs_freep(err);
        return err;
    }

    // client close peer.
    // TODO: FIXME: Only reset the error when client closed it.
    if (srs_is_client_gracefully_close(err)) {
        srs_warn("RTSP: client disconnect peer. ret=%d", srs_error_code(err));
    } else if (srs_is_server_gracefully_close(err)) {
        srs_warn("RTSP: server disconnect. ret=%d", srs_error_code(err));
    } else {
        srs_error("RTSP: serve error %s", srs_error_desc(err).c_str());
    }

    srs_freep(err);
    return srs_success;
}

srs_error_t SrsRtspConnection::do_cycle()
{
    srs_error_t err = srs_success;
    srs_trace("RTSP: client ip=%s, port=%d", ip_.c_str(), port_);

    // consume all rtsp messages.
    while (true) {
        if ((err = trd_->pull()) != srs_success) {
            return srs_error_wrap(err, "rtsp cycle");
        }

        SrsRtspRequest *req_raw = NULL;
        if ((err = rtsp_->recv_message(&req_raw)) != srs_success) {
            return srs_error_wrap(err, "recv message");
        }
        SrsUniquePtr<SrsRtspRequest> req(req_raw);

        if (req->is_options()) {
            srs_trace("RTSP: OPTIONS cseq=%ld, url=%s, client=%s:%d", req->seq, req->uri.c_str(), ip_.c_str(), port_);
            SrsUniquePtr<SrsRtspOptionsResponse> res(new SrsRtspOptionsResponse((int)req->seq));
            if ((err = rtsp_->send_message(res.get())) != srs_success) {
                return srs_error_wrap(err, "response option");
            }
        } else if (req->is_describe()) {
            // create session.
            if (session_id_.empty()) {
                session_id_ = srs_rand_gen_str(8);
            }

            SrsUniquePtr<SrsRtspDescribeResponse> res(new SrsRtspDescribeResponse((int)req->seq));
            res->session = session_id_;

            std::string sdp;
            if ((err = do_describe(req.get(), sdp)) != srs_success) {
                res->status = SRS_CONSTS_RTSP_InternalServerError;
                if (srs_error_code(err) == ERROR_RTSP_NO_TRACK) {
                    res->status = SRS_CONSTS_RTSP_NotFound;
                } else if (srs_error_code(err) == ERROR_SYSTEM_SECURITY_DENY) {
                    res->status = SRS_CONSTS_RTSP_Forbidden;
                }
                srs_warn("RTSP: DESCRIBE failed: %s", srs_error_desc(err).c_str());
                srs_error_reset(err);
            }

            res->sdp = sdp;
            if ((err = rtsp_->send_message(res.get())) != srs_success) {
                return srs_error_wrap(err, "response describe");
            }

            // Filter the \r\n to \\r\\n for JSON.
            std::string local_sdp_escaped = srs_strings_replace(sdp.c_str(), "\r\n", "\\r\\n");
            srs_trace("RTSP: DESCRIBE cseq=%ld, session=%s, sdp: %s", req->seq, session_id_.c_str(), local_sdp_escaped.c_str());
        } else if (req->is_setup()) {
            srs_assert(req->transport);

            SrsUniquePtr<SrsRtspSetupResponse> res(new SrsRtspSetupResponse((int)req->seq));
            res->session = session_id_;

            uint32_t ssrc = 0;
            if ((err = do_setup(req.get(), &ssrc)) != srs_success) {
                if (srs_error_code(err) == ERROR_RTSP_TRANSPORT_NOT_SUPPORTED) {
                    res->status = SRS_CONSTS_RTSP_UnsupportedTransport;
                    srs_warn("RTSP: SETUP failed: %s", srs_error_summary(err).c_str());
                } else {
                    res->status = SRS_CONSTS_RTSP_InternalServerError;
                    srs_warn("RTSP: SETUP failed: %s", srs_error_desc(err).c_str());
                }
                srs_error_reset(err);
            }

            res->transport->copy(req->transport);
            res->session = session_id_;
            res->ssrc = srs_strconv_format_int(ssrc);
            res->client_port_min = req->transport->client_port_min;
            res->client_port_max = req->transport->client_port_max;
            // TODO: FIXME: listen local port
            res->local_port_min = 0;
            res->local_port_max = 0;
            if ((err = rtsp_->send_message(res.get())) != srs_success) {
                return srs_error_wrap(err, "response setup");
            }
            srs_trace("RTSP: SETUP cseq=%ld, session=%s, transport=%s/%s/%s, ssrc=%u, client_port=%d-%d",
                      req->seq, session_id_.c_str(), req->transport->transport.c_str(), req->transport->profile.c_str(),
                      req->transport->lower_transport.c_str(), ssrc, req->transport->client_port_min, req->transport->client_port_max);
        } else if (req->is_play()) {
            SrsUniquePtr<SrsRtspResponse> res(new SrsRtspResponse((int)req->seq));
            res->session = session_id_;
            if ((err = rtsp_->send_message(res.get())) != srs_success) {
                return srs_error_wrap(err, "response record");
            }

            if ((err = do_play(req.get(), this)) != srs_success) {
                return srs_error_wrap(err, "prepare play");
            }
            srs_trace("RTSP: PLAY cseq=%ld, session=%s, streaming started", req->seq, session_id_.c_str());
        } else if (req->is_teardown()) {
            SrsUniquePtr<SrsRtspResponse> res(new SrsRtspResponse((int)req->seq));
            res->session = session_id_;
            if ((err = rtsp_->send_message(res.get())) != srs_success) {
                return srs_error_wrap(err, "response teardown");
            }

            if ((err = do_teardown()) != srs_success) {
                return srs_error_wrap(err, "teardown");
            }
            srs_trace("RTSP: TEARDOWN cseq=%ld, session=%s, streaming stopped", req->seq, session_id_.c_str());
        }
    }

    return err;
}

void SrsRtspConnection::on_before_dispose(ISrsResource *c)
{
    if (disposing_) {
        return;
    }

    SrsRtspConnection *session = dynamic_cast<SrsRtspConnection *>(c);
    if (session == this) {
        disposing_ = true;
    }

    if (session && session == this) {
        _srs_context->set_id(cid_);
        srs_trace("RTSP: session detach from [%s](%s), disposing=%d", c->get_id().c_str(),
                  c->desc().c_str(), disposing_);
    }
}

void SrsRtspConnection::on_disposing(ISrsResource *c)
{
    if (disposing_) {
        return;
    }
}

void SrsRtspConnection::switch_to_context()
{
    _srs_context->set_id(cid_);
}

const SrsContextId &SrsRtspConnection::context_id()
{
    return cid_;
}

bool SrsRtspConnection::is_alive()
{
    return last_stun_time + session_timeout > srs_time_now_cached();
}

void SrsRtspConnection::alive()
{
    last_stun_time = srs_time_now_cached();
}

srs_error_t SrsRtspConnection::do_describe(SrsRtspRequest *req, std::string &sdp)
{
    srs_error_t err = srs_success;
    srs_net_url_parse_rtmp_url(req->uri, request_->tcUrl_, request_->stream_);

    srs_net_url_parse_tcurl(request_->tcUrl_, request_->schema_, request_->host_, request_->vhost_,
                            request_->app_, request_->stream_, request_->port_, request_->param_);

    // discovery vhost, resolve the vhost from config
    SrsConfDirective *parsed_vhost = _srs_config->get_vhost(request_->vhost_);
    if (parsed_vhost) {
        request_->vhost_ = parsed_vhost->arg0();
    }

    if ((err = security_->check(SrsRtcConnPlay, ip_, request_)) != srs_success) {
        return srs_error_wrap(err, "RTSP: security check");
    }

    if ((err = http_hooks_on_play(request_)) != srs_success) {
        return srs_error_wrap(err, "RTSP: http_hooks_on_play");
    }

    if ((err = _srs_rtsp_sources->fetch_or_create(request_, source_)) != srs_success) {
        return srs_error_wrap(err, "create source");
    }

    SrsSdp local_sdp;
    local_sdp.version_ = "0";
    local_sdp.username_ = "SRS RTSP Server";
    local_sdp.session_id_ = "0";
    local_sdp.session_version_ = "0";
    local_sdp.nettype_ = "IN";
    local_sdp.addrtype_ = "IP4";
    local_sdp.unicast_address_ = "0.0.0.0";
    local_sdp.session_name_ = "Play";
    local_sdp.control_ = req->uri;
    local_sdp.ice_lite_ = ""; // Disable this line.

    uint32_t track_id = 0;
    SrsRtcTrackDescription *audio_desc = source_->audio_desc();
    if (audio_desc) {
        SrsRtcTrackDescription *audio_track_desc = audio_desc->copy();
        audio_track_desc->id_ = srs_strconv_format_int(track_id);
        tracks_.insert(std::make_pair(audio_track_desc->ssrc_, audio_track_desc));

        SrsMediaDesc media_audio("audio");
        media_audio.port_ = 0;           // Port 0 indicates no UDP transport available
        media_audio.protos_ = "RTP/AVP"; // MUST be RTP/AVP
        media_audio.control_ = req->uri + "/trackID=" + srs_strconv_format_int(track_id);
        media_audio.recvonly_ = true;
        media_audio.rtcp_mux_ = true;

        media_audio.payload_types_.push_back(SrsMediaPayloadType(audio_track_desc->media_->pt_));
        SrsMediaPayloadType &ps_audio = media_audio.payload_types_.at(0);
        ps_audio.encoding_name_ = audio_track_desc->media_->name_;
        ps_audio.clock_rate_ = audio_track_desc->media_->sample_;

        // if the payload is opus, and the encoding_param_ is channel
        SrsAudioPayload *ap = dynamic_cast<SrsAudioPayload *>(audio_track_desc->media_);
        if (ap) {
            ps_audio.encoding_param_ = srs_strconv_format_int(ap->channel_);

            // Append the AAC config hex to the fmtp line.
            if (ap->name_ == "MPEG4-GENERIC" && !ap->aac_config_hex_.empty()) {
                // streamtype=5 - Mandatory (indicates audio stream)
                // mode=AAC-hbr - Mandatory (AAC High Bit Rate mode)
                // sizelength=13 - Mandatory, defaults to 13 bits for AAC
                // indexlength=3 - Mandatory, defaults to 3 bits
                // profile-level-id=1 - Optional, defaults to 1 (AAC Main Profile)
                // indexdeltalength=3 - Optional, defaults to 3 bits
                ps_audio.format_specific_param_ = "streamtype=5;mode=AAC-hbr;sizelength=13;indexlength=3";
                ps_audio.format_specific_param_ += ";config=" + ap->aac_config_hex_;
                srs_trace("RTSP: Added AAC fmtp: %s", ps_audio.format_specific_param_.c_str());
            }
        }

        local_sdp.media_descs_.push_back(media_audio);
        track_id++;
    }

    SrsRtcTrackDescription *video_desc = source_->video_desc();
    if (video_desc) {
        SrsRtcTrackDescription *video_track_desc = video_desc->copy();
        video_track_desc->id_ = srs_strconv_format_int(track_id);
        tracks_.insert(std::make_pair(video_track_desc->ssrc_, video_track_desc));

        SrsMediaDesc media_video("video");
        media_video.port_ = 0;           // Port 0 indicates no UDP transport available
        media_video.protos_ = "RTP/AVP"; // MUST be RTP/AVP
        media_video.control_ = req->uri + "/trackID=" + srs_strconv_format_int(track_id);
        media_video.recvonly_ = true;
        media_video.rtcp_mux_ = true;

        media_video.payload_types_.push_back(SrsMediaPayloadType(video_track_desc->media_->pt_));
        SrsMediaPayloadType &ps_video = media_video.payload_types_.at(0);
        ps_video.encoding_name_ = video_track_desc->media_->name_;
        ps_video.clock_rate_ = video_track_desc->media_->sample_;

        local_sdp.media_descs_.push_back(media_video);
        track_id++;
    }

    if (track_id == 0) {
        return srs_error_new(ERROR_RTSP_NO_TRACK, "no track found");
    }

    std::ostringstream ss;
    if ((err = local_sdp.encode(ss)) != srs_success) {
        return srs_error_wrap(err, "encode sdp");
    }

    sdp = ss.str();
    return srs_success;
}

srs_error_t SrsRtspConnection::do_setup(SrsRtspRequest *req, uint32_t *pssrc)
{
    srs_error_t err = srs_success;

    uint32_t ssrc = 0;
    if ((err = get_ssrc_by_stream_id(req->stream_id, &ssrc)) != srs_success) {
        return srs_error_wrap(err, "get ssrc by stream_id");
    }

    // Only support TCP transport, reject UDP
    // This ensures better firewall/NAT compatibility and eliminates port allocation complexity
    if (req->transport->lower_transport != "TCP") {
        return srs_error_new(ERROR_RTSP_TRANSPORT_NOT_SUPPORTED,
                             "UDP transport not supported, only TCP/interleaved mode is supported");
    }

    SrsRtspTcpNetwork *network = new SrsRtspTcpNetwork(skt_, req->transport->interleaved_min);
    networks_[ssrc] = network;

    *pssrc = ssrc;

    return srs_success;
}

srs_error_t SrsRtspConnection::do_play(SrsRtspRequest *req, SrsRtspConnection *conn)
{
    srs_error_t err = srs_success;

    srs_freep(player_);
    player_ = new SrsRtspPlayStream(conn, cid_);

    if ((err = player_->initialize(request_, tracks_)) != srs_success) {
        srs_freep(player_);
        return srs_error_wrap(err, "SrsRtspPlayStream init");
    }
    player_->set_all_tracks_status(true);
    if ((err = player_->start()) != srs_success) {
        return srs_error_wrap(err, "start play");
    }

    srs_trace("RTSP: Subscriber url=%s established", req->uri.c_str());

    return err;
}

srs_error_t SrsRtspConnection::do_teardown()
{
    if (player_) {
        player_->stop();
        srs_freep(player_);
    }

    return srs_success;
}

srs_error_t SrsRtspConnection::http_hooks_on_play(ISrsRequest *req)
{
    srs_error_t err = srs_success;

    if (!_srs_config->get_vhost_http_hooks_enabled(req->vhost_)) {
        return err;
    }

    // the http hooks will cause context switch,
    // so we must copy all hooks for the on_connect may freed.
    // @see https://github.com/ossrs/srs/issues/475
    std::vector<std::string> hooks;

    if (true) {
        SrsConfDirective *conf = _srs_config->get_vhost_on_play(req->vhost_);

        if (!conf) {
            return err;
        }

        hooks = conf->args;
    }

    for (int i = 0; i < (int)hooks.size(); i++) {
        std::string url = hooks.at(i);
        if ((err = _srs_hooks->on_play(url, req)) != srs_success) {
            return srs_error_wrap(err, "on_play %s", url.c_str());
        }
    }

    return err;
}

srs_error_t SrsRtspConnection::get_ssrc_by_stream_id(uint32_t stream_id, uint32_t *ssrc)
{
    for (std::map<uint32_t, SrsRtcTrackDescription *>::iterator it = tracks_.begin(); it != tracks_.end(); ++it) {
        if (it->second->id_ == srs_strconv_format_int(stream_id)) {
            *ssrc = it->second->ssrc_;
            return srs_success;
        }
    }
    return srs_error_new(ERROR_RTSP_NO_TRACK, "track not found for stream_id: %u", stream_id);
}

SrsRtspTcpNetwork::SrsRtspTcpNetwork(ISrsProtocolReadWriter *skt, int ch) : skt_(skt), channel_(ch)
{
}

SrsRtspTcpNetwork::~SrsRtspTcpNetwork()
{
}

srs_error_t SrsRtspTcpNetwork::write(void *buf, size_t size, ssize_t *nwrite)
{
    srs_error_t err = srs_success;

    srs_assert(size <= 65535);

    // Encode and send 4 bytes size, in network order.
    const int kRtpTcpPacketHeaderSize = 4;
    char header[kRtpTcpPacketHeaderSize];

    // Use SrsBuffer to handle endianness properly
    SrsBuffer hb(header, kRtpTcpPacketHeaderSize);
    hb.write_1bytes(0x24);              // Magic byte '$'
    hb.write_1bytes(uint8_t(channel_)); // Channel number
    hb.write_2bytes(uint16_t(size));    // Packet size in network order

    if ((err = skt_->write(header, kRtpTcpPacketHeaderSize, NULL)) != srs_success) {
        return srs_error_wrap(err, "RTSP tcp write len(%d)", size);
    }

    if ((err = skt_->write(buf, size, nwrite)) != srs_success) {
        return srs_error_wrap(err, "RTSP send rtp packet");
    }

    // Add the size of the header to the write count.
    *nwrite += kRtpTcpPacketHeaderSize;

    return err;
}
