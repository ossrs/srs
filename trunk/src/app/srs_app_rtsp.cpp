#include <srs_app_config.hpp>
#include <srs_app_rtsp.hpp>
#include <srs_app_statistic.hpp>
#include <srs_app_utility.hpp>
#include <srs_app_rtc_sdp.hpp>
#include <srs_app_rtc_server.hpp>
#include <srs_protocol_st.hpp>
#include <sstream>

extern SrsResourceManager* _srs_rtc_manager;

#define SRS_RTSP_PACKET_MAX 1500
#define SRS_RTP_TCP_PACKET_HEADER_SIZE 4

SrsRtspSession::SrsRtspSession(SrsContextId cid, SrsRequest* r, ISrsProtocolReadWriter* skt, std::string ip, int port)
{
    cid_ = cid;
    request_ = r;
    skt_ = skt;
    ip_ = ip;
    port_ = port;
    source_ = NULL;
    player_ = NULL;

    delta_ = new SrsEphemeralDelta();
    security_ = new SrsSecurity();
}

SrsRtspSession::~SrsRtspSession()
{
    for (std::map<uint32_t, SrsRtspNetwork*>::iterator it = networks_.begin(); it != networks_.end(); ++it) {
        srs_freep(it->second);
    }
    networks_.clear();

    srs_freep(delta_);
    srs_freep(security_);
    srs_freep(skt_);
}

ISrsKbpsDelta* SrsRtspSession::delta()
{
    return delta_;
}

srs_error_t SrsRtspSession::do_send_packet(SrsRtpPacket* pkt)
{
    srs_error_t err = srs_success;

    uint32_t ssrc = pkt->header.get_ssrc();
    SrsRtspNetwork* network = networks_[ssrc];
    if (!network) {
        return srs_error_new(-1, "network not found for ssrc: %u", ssrc);
    }
    int64_t write = 0;
    if ((err = network->write(pkt, &write)) != srs_success) {
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

    if ((err = _srs_rtc_sources->fetch_or_create(request_, source_)) != srs_success) {
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

    std::vector<SrsRtcTrackDescription*> audio_track_descs = source_->get_track_desc("audio", "opus");
    if (!audio_track_descs.empty()) {
        SrsRtcTrackDescription* audio_track_desc = audio_track_descs.at(0);
        id_track_[1] = "audio";

        SrsMediaDesc media_audio("audio");
        media_audio.port_ = 0;
        media_audio.protos_ = "RTP/AVP";
        media_audio.control_ = req->uri + "/trackID=" + srs_int2str(1);
        media_audio.recvonly_ = true;
        media_audio.rtcp_mux_ = true;

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
    }
    
    std::vector<SrsRtcTrackDescription*> video_track_descs = source_->get_track_desc("video", "");
    if (!video_track_descs.empty()) {
        SrsRtcTrackDescription* video_track_desc = video_track_descs.at(0);
        id_track_[2] = "video";

        SrsMediaDesc media_video("video");
        media_video.port_ = 0;
        media_video.protos_ = "RTP/AVP";
        media_video.control_ = req->uri + "/trackID=" + srs_int2str(2);
        media_video.recvonly_ = true;
        media_video.rtcp_mux_ = true;

        media_video.payload_types_.push_back(SrsMediaPayloadType(video_track_desc->media_->pt_));
        SrsMediaPayloadType& ps_video = media_video.payload_types_.at(0);
        ps_video.encoding_name_ = video_track_desc->media_->name_;
        ps_video.clock_rate_ = video_track_desc->media_->sample_;

        local_sdp.media_descs_.push_back(media_video);
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

    size_t pos = std::string::npos;
    std::string stream_id = srs_path_basename(req->uri);
    if ((pos = stream_id.find("=")) != std::string::npos) {
        stream_id = stream_id.substr(pos + 1);
    }
    req->stream_id = ::atoi(stream_id.c_str());
    srs_info("rtsp: setup stream id=%d", req->stream_id);

    std::string stream_name = id_track_[req->stream_id];

    if (!source_.get()) {
        return srs_error_new(-1, "source not found");
    }

    uint32_t ssrc = 0;
    std::vector<SrsRtcTrackDescription*> track_descs;
    if (stream_name == "audio") {
        track_descs = source_->get_track_desc("audio", "opus");
    } else if (stream_name == "video") {
        track_descs = source_->get_track_desc("video", "");
    }
    if (track_descs.empty()) {
        return srs_error_new(-1, "track not found");
    }

    SrsRtcTrackDescription* track_desc = track_descs.at(0);
    ssrc = track_desc->ssrc_;
    sub_relations_.insert(std::make_pair(ssrc, track_desc->copy()));

    if (req->transport->lower_transport != "TCP") {
        SrsRtspUdpNetwork* network = new SrsRtspUdpNetwork();
        if ((err = network->initialize(ip_, req->transport->client_port_min)) != srs_success) {
            return srs_error_wrap(err, "initialize udp client");
        }
        networks_[ssrc] = network;
    } else {
        uint32_t rtp, rtcp;
        if ((err = parse_interleaved(req->transport->interleaved, &rtp, &rtcp)) != srs_success) {
            return srs_error_wrap(err, "parse_interleaved");
        }
        SrsRtspTcpNetwork* network = new SrsRtspTcpNetwork(skt_, rtp);
        networks_[ssrc] = network;
    }

    *pssrc = ssrc;

    return srs_success;
}

srs_error_t SrsRtspSession::do_play(SrsRtspRequest* req, SrsRtcPlayStream* player)
{
    srs_error_t err = srs_success;

    if ((err = player->initialize(request_, sub_relations_)) != srs_success) {
        srs_freep(player);
        return srs_error_wrap(err, "SrsRtspPlayStream init");
    }
    player->set_all_tracks_status(true);
    if ((err = player->start()) != srs_success) {
        return srs_error_wrap(err, "start play");
    }

    player_ = player;

    srs_trace("RTSP: Subscriber url=%s established", req->uri.c_str());

    return err;
}

srs_error_t SrsRtspSession::do_teardown()
{
    player_->stop();
    srs_freep(player_);

    return srs_success;
}

srs_error_t SrsRtspSession::parse_interleaved(std::string interleaved, uint32_t* min, uint32_t* max)
{
    srs_error_t err = srs_success;

    std::string::size_type pos = interleaved.find("-");
    if (pos == std::string::npos) {
        return srs_error_new(-1, "123");
    }

    *min = atoi(interleaved.substr(0, pos).c_str());

    return err;
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
        srs_trace("client finished.");
        return err;
    }

    // It maybe success with message.
    if (srs_error_code(err) == ERROR_SUCCESS) {
        srs_trace("client finished%s.", srs_error_summary(err).c_str());
        srs_freep(err);
        return err;
    }

    // client close peer.
    // TODO: FIXME: Only reset the error when client closed it.
    if (srs_is_client_gracefully_close(err)) {
        srs_warn("client disconnect peer. ret=%d", srs_error_code(err));
    } else if (srs_is_server_gracefully_close(err)) {
        srs_warn("server disconnect. ret=%d", srs_error_code(err));
    } else {
        srs_error("serve error %s", srs_error_desc(err).c_str());
    }

    srs_freep(err);
    return srs_success;
}

srs_error_t SrsRtspConn::do_cycle()
{
    srs_error_t err = srs_success;
    srs_trace("RTSP: client ip=%s, port=%d", ip_.c_str(), port_);

    bool rtc_enabled = _srs_config->get_rtc_server_enabled();
    if (!rtc_enabled) {
        return srs_error_new(ERROR_RTC_DISABLED, "RTC is disabled, but it is a necessary dependency.");
    } 

    // consume all rtsp messages.
    while (true) {
        if ((err = trd_->pull()) != srs_success) {
            return srs_error_wrap(err, "rtsp cycle");
        }
        
        SrsRtspRequest* req = NULL;
        if ((err = rtsp_->recv_message(&req)) != srs_success) {
            return srs_error_wrap(err, "recv message");
        }
        SrsUniquePtr<SrsRtspRequest> req_ptr(req);
        
        if (req->is_options()) {
            SrsRtspOptionsResponse* res = new SrsRtspOptionsResponse((int)req->seq);
            res->session = session_id_;
            if ((err = rtsp_->send_message(res)) != srs_success) {
                return  srs_error_wrap(err, "response option");
            }
        } else if (req->is_describe()) {
            // create session.
            if (session_id_.empty()) {
                session_id_ = srs_random_str(8);
            }

            SrsRtspDescribeResponse* res = new SrsRtspDescribeResponse((int)req->seq);
            res->session = session_id_;

            std::string sdp;
            err = session_->do_describe(req, sdp);
            if (err != srs_success) {
                res->status = SRS_CONSTS_RTSP_InternalServerError;
                if (srs_error_code(err) == ERROR_SYSTEM_SECURITY_DENY) {
                    res->status = SRS_CONSTS_RTSP_Forbidden;
                }
                srs_warn("describe failed: %s", srs_error_desc(err).c_str());
                srs_error_reset(err);
            }

            res->sdp = sdp;
            if ((err = rtsp_->send_message(res)) != srs_success) {
                return  srs_error_wrap(err, "response describe");
            }
        } else if (req->is_setup()) {
            srs_assert(req->transport);            

            SrsRtspSetupResponse* res = new SrsRtspSetupResponse((int)req->seq);
            res->session = session_id_;

            uint32_t ssrc = 0;
            err = session_->do_setup(req, &ssrc);
            if (err != srs_success) {
                res->status = SRS_CONSTS_RTSP_InternalServerError;
                srs_warn("setup failed: %s", srs_error_desc(err).c_str());
                srs_error_reset(err);
            }
    
            res->transport->copy(req->transport);
            res->session = session_id_;
            res->ssrc = srs_int2str(ssrc);
            res->client_port_min = req->transport->client_port_min;
            res->client_port_max = req->transport->client_port_max;
            // TODO: FIXME: get local port from udp client.
            res->local_port_min = 0;
            res->local_port_max = 0;
            if ((err = rtsp_->send_message(res)) != srs_success) {
                return srs_error_wrap(err, "response setup");
            }
        } else if (req->is_play()) {
            SrsRtspResponse* res = new SrsRtspResponse((int)req->seq);
            res->session = session_id_;
            if ((err = rtsp_->send_message(res)) != srs_success) {
                return srs_error_wrap(err, "response record");
            }
            err = session_->do_play(req, new SrsRtcPlayStream(this, cid_));
            if (err != srs_success) {
                return srs_error_wrap(err, "prepare play");
            }
        } else if (req->is_teardown()) {
            SrsRtspResponse* res = new SrsRtspResponse((int)req->seq);
            res->session = session_id_;
            if ((err = rtsp_->send_message(res)) != srs_success) {
                return srs_error_wrap(err, "response teardown");
            }

            err = session_->do_teardown();
            if (err != srs_success) {
                return srs_error_wrap(err, "teardown");
            }
        }
    }
    
    return err;
}

SrsRtspNetwork::SrsRtspNetwork()
{
    cache_iov_ = new iovec();
    cache_iov_->iov_base = new char[kRtpPacketSize];
    cache_iov_->iov_len = kRtpPacketSize;
    cache_buffer_ = new SrsBuffer((char*)cache_iov_->iov_base, kRtpPacketSize);
}

SrsRtspNetwork::~SrsRtspNetwork()
{
    if (true) {
        char* iov_base = (char*)cache_iov_->iov_base;
        srs_freepa(iov_base);
        srs_freep(cache_iov_);
    }
    srs_freep(cache_buffer_);
}

SrsRtspUdpNetwork::SrsRtspUdpNetwork()
{
    addr_ = NULL;
    stfd_ = NULL;
}

SrsRtspUdpNetwork::~SrsRtspUdpNetwork()
{
    srs_close_stfd(stfd_);
    srs_freep(addr_);
}

srs_error_t SrsRtspUdpNetwork::initialize(std::string ip, int port)
{
    srs_error_t err = srs_success;

    // Allocate and set remote address
    addr_ = new sockaddr_in();
    addr_->sin_family = AF_INET;
    addr_->sin_addr.s_addr = inet_addr(ip.c_str());
    addr_->sin_port = htons(port);

    // Create UDP socket
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        return srs_error_new(ERROR_SOCKET_CREATE, "create socket failed, ret=%d", fd);
    }

    // Wrap the socket in stfd
    stfd_ = srs_netfd_open_socket(fd);
    srs_assert(stfd_);

    int local_port = srs_get_local_port(fd);
    srs_trace("udp client %s:%d, fd=%d, local_port=%d", ip.c_str(), port, fd, local_port);

    return err;
}

srs_error_t SrsRtspUdpNetwork::write(SrsRtpPacket* pkt, int64_t * write)
{
    srs_error_t err = srs_success;

    iovec* iov = cache_iov_;
    cache_buffer_->skip(-1 * cache_buffer_->pos());

    // Marshal packet to bytes in iovec.
    if (true) {
        if ((err = pkt->encode(cache_buffer_)) != srs_success) {
            return srs_error_wrap(err, "encode packet");
        }
        iov->iov_len = cache_buffer_->pos();
    }

    if (true) {
        // int nwrite = udp_client->sendto(iov->iov_base, iov->iov_len);
        int nwrite = srs_sendto(stfd_, iov->iov_base, iov->iov_len, (sockaddr*)addr_, sizeof(sockaddr_in), SRS_UTIME_NO_TIMEOUT);
        if (nwrite <= 0) {
            return srs_error_new(-1, "send udp packet");
        }
        *write = nwrite;
    }

    return err;
}

SrsRtspTcpNetwork::SrsRtspTcpNetwork(ISrsProtocolReadWriter* skt, int ch) : skt_(skt), channel_(ch)
{
}

SrsRtspTcpNetwork::~SrsRtspTcpNetwork()
{
}

srs_error_t SrsRtspTcpNetwork::write(SrsRtpPacket* pkt, int64_t * write)
{
    srs_error_t err = srs_success;

    iovec* iov = cache_iov_;
    cache_buffer_->skip(-1 * cache_buffer_->pos() + SRS_RTP_TCP_PACKET_HEADER_SIZE);

    // Marshal packet to bytes in iovec.
    if (true) {
        if ((err = pkt->encode(cache_buffer_)) != srs_success) {
            return srs_error_wrap(err, "encode packet");
        }
        iov->iov_len = cache_buffer_->pos();
    }

    cache_buffer_->skip(-1 * cache_buffer_->pos());
    cache_buffer_->write_1bytes(0x24);
    cache_buffer_->write_1bytes(channel_);
    cache_buffer_->write_2bytes(iov->iov_len - SRS_RTP_TCP_PACKET_HEADER_SIZE);

    if ((err = skt_->write(iov->iov_base, iov->iov_len, write)) != srs_success) {
        return srs_error_wrap(err, "send rtp packet");
    }

    return err;
}

srs_error_t SrsRtspConn::do_send_packet(SrsRtpPacket *pkt)
{
    return session_->do_send_packet(pkt);
}
