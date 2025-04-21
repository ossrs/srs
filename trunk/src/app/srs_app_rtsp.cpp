#include <srs_app_config.hpp>
#include <srs_app_rtsp.hpp>
#include <srs_app_statistic.hpp>
#include <srs_app_utility.hpp>
#include <srs_app_rtc_sdp.hpp>
#include <srs_app_rtc_conn.hpp>
#include <srs_app_rtc_server.hpp>
#include <srs_app_rtc_source.hpp>
#include <srs_app_rtc_network.hpp>
#include <srs_protocol_st.hpp>
#include <sstream>

extern SrsResourceManager* _srs_rtc_manager;

#define SRS_RTSP_PACKET_MAX 1500
#define SRS_RTP_TCP_PACKET_HEADER_SIZE 4

SrsRtspConn::SrsRtspConn(ISrsResourceManager* cm, ISrsProtocolReadWriter* skt, std::string cip, int port) : SrsRtcConnection(NULL, _srs_context->get_id())
{
    source_ = NULL;
    manager_ = cm;
    cid_ = _srs_context->get_id();
    request_ = new SrsRequest();
    request_->ip = cip;
    ip_ = cip;
    port_ = port;
    skt_ = skt;
    rtsp_ = new SrsRtspStack(skt);
    trd_ = new SrsSTCoroutine("rtsp", this, _srs_context->get_id());

    delta_ = new SrsNetworkDelta();
    delta_->set_io(skt_, skt_);
    pkt_ = new char[SRS_RTSP_PACKET_MAX];

    cache_iov_ = new iovec();
    cache_iov_->iov_base = new char[kRtpPacketSize];
    cache_iov_->iov_len = kRtpPacketSize;
    cache_buffer_ = new SrsBuffer((char*)cache_iov_->iov_base, kRtpPacketSize);
}

SrsRtspConn::~SrsRtspConn()
{
    srs_freepa(pkt_);
    srs_freep(delta_);
    srs_freep(skt_);
    if (true) {
        char* iov_base = (char*)cache_iov_->iov_base;
        srs_freepa(iov_base);
        srs_freep(cache_iov_);
    }
    srs_freep(cache_buffer_);

    for (std::map<uint32_t, SrsUdpClient*>::iterator it = udp_clients_.begin(); it != udp_clients_.end(); ++it) {
        srs_freep(it->second);
    }
    udp_clients_.clear();
}

srs_error_t SrsRtspConn::do_send_packet(SrsRtpPacket *pkt)
{
    return is_udp_ ? do_send_udp_packet(pkt) : do_send_tcp_packet(pkt);
}

srs_error_t SrsRtspConn::do_send_udp_packet(SrsRtpPacket *pkt)
{
    srs_error_t err = srs_success;

    uint32_t ssrc = pkt->header.get_ssrc();
    SrsUdpClient* udp_client = udp_clients_[ssrc];
    if (!udp_client) {
        return srs_error_new(-1, "UDP socket not found for ssrc: %u", ssrc);
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

    if (true) {
        int nwrite = udp_client->sendto(iov->iov_base, iov->iov_len);
        if (nwrite <= 0) {
            return srs_error_new(-1, "send udp packet");
        }
    }

    return err;
}

srs_error_t SrsRtspConn::do_send_tcp_packet(SrsRtpPacket *pkt)
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
    cache_buffer_->write_1bytes(0x00);
    cache_buffer_->write_2bytes(iov->iov_len);

    if ((err = rtsp_->send_rtp_packet(iov->iov_base, iov->iov_len + SRS_RTP_TCP_PACKET_HEADER_SIZE)) != srs_success) {
        return srs_error_wrap(err, "send rtp packet");
    }

    return err;
}

srs_error_t SrsRtspConn::do_describe(SrsRtspRequest* req, std::string& sdp)
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

srs_error_t SrsRtspConn::do_setup(SrsRtspRequest* req, uint32_t* pssrc)
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

    is_udp_ = !(req->transport->lower_transport == "TCP");
    if (is_udp_) {
        SrsUdpClient* udp_client = new SrsUdpClient();
        if ((err = udp_client->initialize(ip_, req->transport->client_port_min)) != srs_success) {
            return srs_error_wrap(err, "initialize udp client");
        }
        udp_clients_[ssrc] = udp_client;
    }

    *pssrc = ssrc;

    return srs_success;
}

srs_error_t SrsRtspConn::do_play(SrsRtspRequest* req)
{
    srs_error_t err = srs_success;

    SrsRtcPlayStream* player = new SrsRtcPlayStream(this, _srs_context->get_id());
    if ((err = player->initialize(request_, sub_relations_)) != srs_success) {
        srs_freep(player);
        return srs_error_wrap(err, "SrsRtspPlayStream init");
    }
    player->set_all_tracks_status(true);
    players_.insert(make_pair(request_->get_stream_url(), player));

    // start all player
    for(std::map<std::string, SrsRtcPlayStream*>::iterator it = players_.begin(); it != players_.end(); ++it) {
        std::string url = it->first;
        SrsRtcPlayStream* player = it->second;

        srs_trace("RTSP: Subscriber url=%s established", url.c_str());

        if ((err = player->start()) != srs_success) {
            return srs_error_wrap(err, "start play");
        }
    }

    return err;
}

srs_error_t SrsRtspConn::do_teardown()
{
    // stop all player
    for(std::map<std::string, SrsRtcPlayStream*>::iterator it = players_.begin(); it != players_.end(); ++it) {
        std::string url = it->first;
        SrsRtcPlayStream* player = it->second;
        player->stop();
    }
    players_.clear();

    return srs_success;
}

void SrsRtspConn::on_before_dispose(ISrsResource *c)
{
    if (disposing_) {
        return;
    }

    SrsRtspConn* session = dynamic_cast<SrsRtspConn*>(c);
    if (session == this) {
        disposing_ = true;
    }

    if (session && session == this) {
        _srs_context->set_id(cid_);
        srs_trace("RTSP: session detach from [%s](%s), disposing=%d", c->get_id().c_str(),
            c->desc().c_str(), disposing_);
    }
}

void SrsRtspConn::on_disposing(ISrsResource *c)
{
    if (disposing_) {
        return;
    }
}

ISrsKbpsDelta* SrsRtspConn::delta()
{
    return delta_;  
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
    stat->kbps_add_delta(get_id().c_str(), delta_);
    stat->on_disconnect(get_id().c_str(), err);

    err = do_teardown();
    if (err != srs_success) {
        return srs_error_wrap(err, "teardown");
    }

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
            res->session = session_;
            if ((err = rtsp_->send_message(res)) != srs_success) {
                return  srs_error_wrap(err, "response option");
            }
        } else if (req->is_describe()) {
            // create session.
            if (session_.empty()) {
                session_ = srs_random_str(8);
            }

            SrsRtspDescribeResponse* res = new SrsRtspDescribeResponse((int)req->seq);
            res->session = session_;

            std::string sdp;
            err = do_describe(req, sdp);
            if (err != srs_success) {
                res->status = 500;
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
            res->session = session_;

            uint32_t ssrc = 0;
            err = do_setup(req, &ssrc);
            if (err != srs_success) {
                res->status = 500;
                srs_warn("setup failed: %s", srs_error_desc(err).c_str());
                srs_error_reset(err);
            }
    
            res->transport->copy(req->transport);
            res->session = session_;
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
            res->session = session_;
            if ((err = rtsp_->send_message(res)) != srs_success) {
                return srs_error_wrap(err, "response record");
            }

            err = do_play(req);
            if (err != srs_success) {
                return srs_error_wrap(err, "prepare play");
            }
            
        } else if (req->is_teardown()) {
            SrsRtspResponse* res = new SrsRtspResponse((int)req->seq);
            res->session = session_;
            if ((err = rtsp_->send_message(res)) != srs_success) {
                return srs_error_wrap(err, "response teardown");
            }

            err = do_teardown();
            if (err != srs_success) {
                return srs_error_wrap(err, "teardown");
            }
        }
    }
    
    return err;
}

SrsUdpClient::SrsUdpClient()
{
    addr_ = NULL;
    stfd_ = NULL;
}

SrsUdpClient::~SrsUdpClient()
{
    srs_close_stfd(stfd_);
    srs_freep(addr_);
}

srs_error_t SrsUdpClient::initialize(std::string ip, int port)
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

int SrsUdpClient::sendto(void* data, int len)
{
    return srs_sendto(stfd_, data, len, (sockaddr*)addr_, sizeof(sockaddr_in), SRS_UTIME_NO_TIMEOUT);
}