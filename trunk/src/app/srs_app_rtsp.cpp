//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_app_config.hpp>
#include <srs_app_rtsp.hpp>
#include <srs_app_rtsp_source.hpp>
#include <srs_app_statistic.hpp>
#include <srs_app_utility.hpp>
#include <srs_app_rtc_sdp.hpp>
#include <srs_app_rtc_server.hpp>
#include <srs_protocol_st.hpp>
#include <srs_kernel_buffer.hpp>

#include <sstream>

SrsRtspSession::SrsRtspSession(SrsContextId cid, SrsRequest* r, ISrsProtocolReadWriter* skt, std::string ip, int port)
{
    cid_ = cid;
    request_ = r;
    skt_ = skt;
    ip_ = ip;
    port_ = port;
    source_ = NULL;
    player_ = NULL;

    cache_iov_ = new iovec();
    cache_iov_->iov_base = new char[kRtpPacketSize];
    cache_iov_->iov_len = kRtpPacketSize;
    cache_buffer_ = new SrsBuffer((char*)cache_iov_->iov_base, kRtpPacketSize);

    delta_ = new SrsEphemeralDelta();
    security_ = new SrsSecurity();
}

SrsRtspSession::~SrsRtspSession()
{
    for (std::map<uint32_t, SrsRtcTrackDescription*>::iterator it = tracks_.begin(); it != tracks_.end(); ++it) {
        srs_freep(it->second);
    }
    tracks_.clear();

    for (std::map<uint32_t, ISrsStreamWriter*>::iterator it = networks_.begin(); it != networks_.end(); ++it) {
        srs_freep(it->second);
    }
    networks_.clear();

    srs_freep(delta_);
    srs_freep(security_);
    srs_freep(skt_);

    if (true) {
        char* iov_base = (char*)cache_iov_->iov_base;
        srs_freepa(iov_base);
        srs_freep(cache_iov_);
    }
    srs_freep(cache_buffer_);
}

ISrsKbpsDelta* SrsRtspSession::delta()
{
    return delta_;
}

srs_error_t SrsRtspSession::do_send_packet(SrsRtpPacket* pkt)
{
    srs_error_t err = srs_success;

    uint32_t ssrc = pkt->header.get_ssrc();
    ISrsStreamWriter* network = networks_[ssrc];
    if (!network) {
        return srs_error_new(ERROR_RTSP_NO_TRACK, "network not found for ssrc: %u", ssrc);
    }
    
    iovec* iov = cache_iov_;
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

srs_error_t SrsRtspSession::do_describe(SrsRtspRequest* req, std::string& sdp)
{
    srs_error_t err = srs_success;
    srs_parse_rtmp_url(req->uri, request_->tcUrl, request_->stream);

    srs_discovery_tc_url(request_->tcUrl, request_->schema, request_->host, request_->vhost,
                        request_->app, request_->stream, request_->port, request_->param);

    // discovery vhost, resolve the vhost from config
    SrsConfDirective* parsed_vhost = _srs_config->get_vhost(request_->vhost);
    if (parsed_vhost) {
        request_->vhost = parsed_vhost->arg0();
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

    // Add session-level attributes to indicate TCP-only support
    local_sdp.session_info_.setup_ = "passive";  // Server is passive for TCP connections

    uint32_t track_id = 0;
    SrsRtcTrackDescription* audio_desc = source_->audio_desc();
    if (!audio_desc) {
        SrsRtcTrackDescription* audio_track_desc = audio_desc->copy();
        audio_track_desc->id_ = srs_int2str(track_id);
        tracks_.insert(std::make_pair(audio_track_desc->ssrc_, audio_track_desc));

        SrsMediaDesc media_audio("audio");
        media_audio.port_ = 0;  // Port 0 indicates no UDP transport available
        media_audio.protos_ = "RTP/AVP/TCP";  // Explicitly advertise TCP transport
        media_audio.control_ = req->uri + "/trackID=" + srs_int2str(track_id);
        media_audio.recvonly_ = true;
        media_audio.rtcp_mux_ = true;

        // Add SDP attributes to indicate TCP-only support
        media_audio.session_info_.setup_ = "passive";  // Server is passive for TCP connections

        media_audio.payload_types_.push_back(SrsMediaPayloadType(audio_track_desc->media_->pt_));
        SrsMediaPayloadType& ps_audio = media_audio.payload_types_.at(0);
        ps_audio.encoding_name_ = audio_track_desc->media_->name_;
        ps_audio.clock_rate_ = audio_track_desc->media_->sample_;

        // if the payload is opus, and the encoding_param_ is channel
        SrsAudioPayload* ap = dynamic_cast<SrsAudioPayload*>(audio_track_desc->media_);
        if (ap) {
            ps_audio.encoding_param_ = srs_int2str(ap->channel_);
        }

        local_sdp.media_descs_.push_back(media_audio);
        track_id++;
    }
    
    SrsRtcTrackDescription* video_desc = source_->video_desc();
    if (!video_desc) {
        SrsRtcTrackDescription* video_track_desc = video_desc->copy();
        video_track_desc->id_ = srs_int2str(track_id);
        tracks_.insert(std::make_pair(video_track_desc->ssrc_, video_track_desc));

        SrsMediaDesc media_video("video");
        media_video.port_ = 0;  // Port 0 indicates no UDP transport available
        media_video.protos_ = "RTP/AVP/TCP";  // Explicitly advertise TCP transport
        media_video.control_ = req->uri + "/trackID=" + srs_int2str(track_id);
        media_video.recvonly_ = true;
        media_video.rtcp_mux_ = true;

        // Add SDP attributes to indicate TCP-only support
        media_video.session_info_.setup_ = "passive";  // Server is passive for TCP connections

        media_video.payload_types_.push_back(SrsMediaPayloadType(video_track_desc->media_->pt_));
        SrsMediaPayloadType& ps_video = media_video.payload_types_.at(0);
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

srs_error_t SrsRtspSession::do_setup(SrsRtspRequest* req, uint32_t* pssrc)
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

    SrsRtspTcpNetwork* network = new SrsRtspTcpNetwork(skt_, req->transport->interleaved_min);
    networks_[ssrc] = network;

    *pssrc = ssrc;

    return srs_success;
}

srs_error_t SrsRtspSession::do_play(SrsRtspRequest* req, SrsRtspConn* conn)
{
    srs_error_t err = srs_success;

    srs_freep(player_);
    player_ = new SrsRtcPlayStream(conn, cid_);

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

srs_error_t SrsRtspSession::do_teardown()
{
    if (player_) {
        player_->stop();
        srs_freep(player_);
    }
    
    return srs_success;
}

srs_error_t SrsRtspSession::http_hooks_on_play(SrsRequest* req)
{
    srs_error_t err = srs_success;

    if (!_srs_config->get_vhost_http_hooks_enabled(req->vhost)) {
        return err;
    }

    // the http hooks will cause context switch,
    // so we must copy all hooks for the on_connect may freed.
    // @see https://github.com/ossrs/srs/issues/475
    std::vector<std::string> hooks;

    if (true) {
        SrsConfDirective* conf = _srs_config->get_vhost_on_play(req->vhost);

        if (!conf) {
            return err;
        }

        hooks = conf->args;
    }

    for (int i = 0; i < (int)hooks.size(); i++) {
        std::string url = hooks.at(i);
        if ((err = SrsHttpHooks::on_play(url, req)) != srs_success) {
            return srs_error_wrap(err, "on_play %s", url.c_str());
        }
    }

    return err;
}

srs_error_t SrsRtspSession::get_ssrc_by_stream_id(uint32_t stream_id, uint32_t* ssrc)
{
    for (std::map<uint32_t, SrsRtcTrackDescription*>::iterator it = tracks_.begin(); it != tracks_.end(); ++it) {
        if (it->second->id_ == srs_int2str(stream_id)) {
            *ssrc = it->second->ssrc_;
            return srs_success;
        }
    }
    return srs_error_new(ERROR_RTSP_NO_TRACK, "track not found for stream_id: %u", stream_id);
}

SrsRtspConn::SrsRtspConn(ISrsResourceManager* cm, ISrsProtocolReadWriter* skt, std::string cip, int port) : SrsRtcConnection(NULL, _srs_context->generate_id())
{
    manager_ = cm;
    cid_ = SrsRtcConnection::get_id();
    _srs_context->set_id(cid_);
    request_ = new SrsRequest();
    request_->ip = cip;
    ip_ = cip;
    port_ = port;
    session_ = new SrsRtspSession(cid_, request_, skt, cip, port);
    rtsp_ = new SrsRtspStack(skt);
    trd_ = new SrsSTCoroutine("rtsp", this, _srs_context->get_id());
}

SrsRtspConn::~SrsRtspConn()
{
    srs_freep(request_);
    srs_freep(session_);
    srs_freep(rtsp_);
    srs_freep(trd_);
}

srs_error_t SrsRtspConn::do_send_packet(SrsRtpPacket* pkt)
{
    return session_->do_send_packet(pkt);
}

ISrsKbpsDelta* SrsRtspConn::delta()
{
    return session_->delta();  
}

std::string SrsRtspConn::desc()
{
    return "Rtsp";
}

const SrsContextId& SrsRtspConn::get_id()
{
    return cid_;
}

std::string SrsRtspConn::remote_ip()
{
    return ip_;
}

void SrsRtspConn::expire()
{
    trd_->interrupt();
}

srs_error_t SrsRtspConn::start()
{
    srs_error_t err = srs_success;

    if ((err = trd_->start()) != srs_success) {
        return srs_error_wrap(err, "coroutine");
    }

    return err;
}

srs_error_t SrsRtspConn::cycle()
{
    srs_error_t err = srs_success;

    // Serve the client.
    err = do_cycle();

    // Update statistic when done.
    SrsStatistic* stat = SrsStatistic::instance();
    stat->kbps_add_delta(get_id().c_str(), session_->delta());

    session_->do_teardown();

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

srs_error_t SrsRtspConn::do_cycle()
{
    srs_error_t err = srs_success;
    srs_trace("RTSP: client ip=%s, port=%d", ip_.c_str(), port_);

    // consume all rtsp messages.
    while (true) {
        if ((err = trd_->pull()) != srs_success) {
            return srs_error_wrap(err, "rtsp cycle");
        }
        
        SrsRtspRequest* req_raw = NULL;
        if ((err = rtsp_->recv_message(&req_raw)) != srs_success) {
            return srs_error_wrap(err, "recv message");
        }
        SrsUniquePtr<SrsRtspRequest> req(req_raw);
        
        if (req->is_options()) {
            srs_trace("RTSP: OPTIONS cseq=%ld, url=%s, client=%s:%d", req->seq, req->uri.c_str(), ip_.c_str(), port_);
            SrsUniquePtr<SrsRtspOptionsResponse> res(new SrsRtspOptionsResponse((int)req->seq));
            if ((err = rtsp_->send_message(res.get())) != srs_success) {
                return  srs_error_wrap(err, "response option");
            }
        } else if (req->is_describe()) {
            // create session.
            if (session_id_.empty()) {
                session_id_ = srs_random_str(8);
            }

            SrsUniquePtr<SrsRtspDescribeResponse> res(new SrsRtspDescribeResponse((int)req->seq));
            res->session = session_id_;

            std::string sdp;
            if ((err = session_->do_describe(req.get(), sdp)) != srs_success) {
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
                return  srs_error_wrap(err, "response describe");
            }
                    
            // Filter the \r\n to \\r\\n for JSON.
            std::string local_sdp_escaped = srs_string_replace(sdp.c_str(), "\r\n", "\\r\\n");
            srs_trace("RTSP: DESCRIBE cseq=%ld, session=%s, sdp: %s", req->seq, session_id_.c_str(), local_sdp_escaped.c_str());
        } else if (req->is_setup()) {
            srs_assert(req->transport);            

            SrsUniquePtr<SrsRtspSetupResponse> res(new SrsRtspSetupResponse((int)req->seq));
            res->session = session_id_;

            uint32_t ssrc = 0;
            if ((err = session_->do_setup(req.get(), &ssrc)) != srs_success) {
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
            res->ssrc = srs_int2str(ssrc);
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
            
            if ((err = session_->do_play(req.get(), this)) != srs_success) {
                return srs_error_wrap(err, "prepare play");
            }
            srs_trace("RTSP: PLAY cseq=%ld, session=%s, streaming started", req->seq, session_id_.c_str());
        } else if (req->is_teardown()) {
            SrsUniquePtr<SrsRtspResponse> res(new SrsRtspResponse((int)req->seq));
            res->session = session_id_;
            if ((err = rtsp_->send_message(res.get())) != srs_success) {
                return srs_error_wrap(err, "response teardown");
            }

            if ((err = session_->do_teardown()) != srs_success) {
                return srs_error_wrap(err, "teardown");
            }
            srs_trace("RTSP: TEARDOWN cseq=%ld, session=%s, streaming stopped", req->seq, session_id_.c_str());
        }
    }
    
    return err;
}

SrsRtspTcpNetwork::SrsRtspTcpNetwork(ISrsProtocolReadWriter* skt, int ch) : skt_(skt), channel_(ch)
{
}

SrsRtspTcpNetwork::~SrsRtspTcpNetwork()
{
}

srs_error_t SrsRtspTcpNetwork::write(void* buf, size_t size, ssize_t* nwrite)
{
    srs_error_t err = srs_success;

    srs_assert(size <= 65535);

    // Encode and send 4 bytes size, in network order.
    const int kRtpTcpPacketHeaderSize = 4;
    char header[kRtpTcpPacketHeaderSize];

    // Use SrsBuffer to handle endianness properly
    SrsBuffer hb(header, kRtpTcpPacketHeaderSize);
    hb.write_1bytes(0x24);                    // Magic byte '$'
    hb.write_1bytes(uint8_t(channel_));       // Channel number
    hb.write_2bytes(uint16_t(size));          // Packet size in network order

    if((err = skt_->write(header, kRtpTcpPacketHeaderSize, NULL)) != srs_success) {
        return srs_error_wrap(err, "rtc tcp write len(%d)", size);
    }

    if ((err = skt_->write(buf, size, nwrite)) != srs_success) {
        return srs_error_wrap(err, "send rtp packet");
    }

    // Add the size of the header to the write count.
    *nwrite += kRtpTcpPacketHeaderSize;

    return err;
}
