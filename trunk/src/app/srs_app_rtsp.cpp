#include <srs_app_config.hpp>
#include <srs_app_rtsp.hpp>
#include <srs_app_statistic.hpp>
#include <srs_app_utility.hpp>
#include <srs_app_rtc_sdp.hpp>
#include <srs_app_rtc_conn.hpp>
#include <srs_app_rtc_server.hpp>
#include <srs_app_rtc_source.hpp>
#include <srs_app_rtc_network.hpp>
#include <sstream>
#include <sys/socket.h>

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

    ssize_t nwrite = 0;
    if ((err = skt_->write(iov->iov_base, iov->iov_len + SRS_RTP_TCP_PACKET_HEADER_SIZE, &nwrite)) != srs_success) {
        return srs_error_wrap(err, "send message");
    }

    return err;
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

    // stop all player
    for(std::map<std::string, SrsRtcPlayStream*>::iterator it = players_.begin(); it != players_.end(); ++it) {
        std::string url = it->first;
        SrsRtcPlayStream* player = it->second;
        player->stop();
    }
    players_.clear();

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
    
    // retrieve ip of client.
    // std::string ip = srs_get_peer_ip(srs_netfd_fileno(stfd));
    // if (ip.empty() && !_srs_config->empty_ip_ok()) {
    //     srs_warn("empty ip for fd=%d", srs_netfd_fileno(stfd));
    // }
    //srs_trace("rtsp: serve %s", ip.c_str());

    
    // consume all rtsp messages.
    while (true) {
        if ((err = trd_->pull()) != srs_success) {
            return srs_error_wrap(err, "rtsp cycle");
        }
        
        SrsRtspRequest* req = NULL;
        if ((err = rtsp_->recv_message(&req)) != srs_success) {
            return srs_error_wrap(err, "recv message");
        }
        // SrsAutoFree(SrsRtspRequest, req);
        SrsUniquePtr<SrsRtspRequest> req_ptr(req);

        srs_info("rtsp: got rtsp request");
        
        if (req->is_options()) {
            SrsRtspOptionsResponse* res = new SrsRtspOptionsResponse((int)req->seq);
            res->session = session_;
            if ((err = rtsp_->send_message(res)) != srs_success) {
                return  srs_error_wrap(err, "response option");
            }
        } else if (req->is_describe()) {
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

            // TODO: FIXME: get audio and video track id from source.
            int audio_track_id = 1;
            int video_track_id = 2;

            id_track_[audio_track_id] = "audio";
            id_track_[video_track_id] = "video";

            int port = _srs_config->get_rtc_server_listen();

            SrsRtspDescribeResponse* res = new SrsRtspDescribeResponse((int)req->seq);
            res->session = session_;
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

            if (true) {
                SrsMediaDesc media_audio("audio");
                media_audio.port_ = 0;
                media_audio.protos_ = "RTP/AVP";
                media_audio.control_ = req->uri + "/trackID=" + srs_int2str(audio_track_id);
                media_audio.recvonly_ = true;
                media_audio.rtcp_mux_ = true;

                media_audio.payload_types_.push_back(SrsMediaPayloadType(104));
                SrsMediaPayloadType& ps_audio = media_audio.payload_types_.at(0);
                ps_audio.encoding_name_ = "OPUS";
                ps_audio.clock_rate_ = 48000;

                local_sdp.media_descs_.push_back(media_audio);
            }

            if (true) {
                SrsMediaDesc media_video("video");
                media_video.port_ = 0;
                media_video.protos_ = "RTP/AVP";
                media_video.control_ = req->uri + "/trackID=" + srs_int2str(video_track_id);
                media_video.recvonly_ = true;
                media_video.rtcp_mux_ = true;

                media_video.payload_types_.push_back(SrsMediaPayloadType(96));
                SrsMediaPayloadType& ps_video = media_video.payload_types_.at(0);
                ps_video.encoding_name_ = "H264";
                ps_video.clock_rate_ = 90000;

                local_sdp.media_descs_.push_back(media_video);
            }

            std::ostringstream ss;
            if ((err = local_sdp.encode(ss)) != srs_success) {
                return srs_error_wrap(err, "encode sdp");
            }

            res->sdp = ss.str();
            if ((err = rtsp_->send_message(res)) != srs_success) {
                return  srs_error_wrap(err, "response describe");
            }
        } else if (req->is_setup()) {
            srs_assert(req->transport);            
            // create session.
            if (session_.empty()) {
                session_ = srs_random_str(8);
            }

            uint32_t ssrc = 0;
            std::string stream_name = id_track_[req->stream_id];

            std::vector<SrsRtcTrackDescription*> track_descs;
            // TODO: 判断source_是否为空 
            if (stream_name == "audio" && source_.get()) {
                track_descs = source_->get_track_desc("audio", "opus");
            } else if (stream_name == "video" && source_.get()) {
                track_descs = source_->get_track_desc("video", "");
            }
            if (!track_descs.empty()) {
                SrsRtcTrackDescription* track_desc = track_descs.at(0);
                ssrc = track_desc->ssrc_;
            }

            int port = _srs_config->get_rtc_server_listen();
            
            SrsRtspSetupResponse* res = new SrsRtspSetupResponse((int)req->seq);
            res->transport->copy(req->transport);
            res->session = session_;
            res->ssrc = srs_int2str(ssrc);
            res->client_port_min = req->transport->client_port_min;
            res->client_port_max = req->transport->client_port_max;
            res->local_port_min = port;
            res->local_port_max = port;
            if ((err = rtsp_->send_message(res)) != srs_success) {
                return srs_error_wrap(err, "response setup");
            }

            is_udp_ = !(req->transport->lower_transport == "TCP");
            if (is_udp_) {
                SrsUdpClient* udp_client = new SrsUdpClient();
                if ((err = udp_client->initialize("172.29.144.1", req->transport->client_port_min)) != srs_success) {
                    srs_warn("failed to initialize udp client: %s", srs_error_desc(err).c_str());
                    srs_error_reset(err);
                } else {
                    udp_clients_[ssrc] = udp_client;
                }
            }

            SrsRtspConn* rtsp_conn = dynamic_cast<SrsRtspConn*>(_srs_rtc_manager->find_by_name(session_));
            if (!rtsp_conn) {
                _srs_rtc_manager->subscribe(this);
            }

            // Ignore if exists.
            if(players_.end() != players_.find(request_->get_stream_url())) {
                return err;
            }

            SrsRtcTrackDescription* track = new SrsRtcTrackDescription();

            track->type_ = stream_name;
            track->id_ = stream_name + "-" + srs_random_str(8);

            track->ssrc_ = ssrc;
            track->direction_ = "recvonly";

            if (stream_name == "audio") {
                track->media_ = new SrsAudioPayload(kAudioPayloadType, "opus", 48000, 2);
                track->media_->pt_ = 104;
            } else if (stream_name == "video") {
                SrsVideoPayload* video_payload = new SrsVideoPayload(kVideoPayloadType, "h264", 90000);
                video_payload->pt_ = 96;
                track->media_ = video_payload;
                video_payload->set_h264_param_desc("level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42e01f");
            }
            sub_relations_.insert(std::make_pair(ssrc, track));
        } else if (req->is_play()) {
            SrsRtspResponse* res = new SrsRtspResponse((int)req->seq);
            res->session = session_;
            if ((err = rtsp_->send_message(res)) != srs_success) {
                return srs_error_wrap(err, "response record");
            }

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
        } else if (req->is_teardown()) {
            SrsRtspResponse* res = new SrsRtspResponse((int)req->seq);
            res->session = session_;
            if ((err = rtsp_->send_message(res)) != srs_success) {
                return srs_error_wrap(err, "response teardown");
            }

            SrsRtspConn* rtsp_conn = dynamic_cast<SrsRtspConn*>(_srs_rtc_manager->find_by_name(session_));
            if (rtsp_conn) {
                _srs_rtc_manager->remove(this);
            }

            // stop all player
            for(std::map<std::string, SrsRtcPlayStream*>::iterator it = players_.begin(); it != players_.end(); ++it) {
                std::string url = it->first;
                SrsRtcPlayStream* player = it->second;
                player->stop();
            }
            players_.clear();
        }
    }
    
    return err;
}

// SrsRtspPlayStream::SrsRtspPlayStream(SrsRtspConn *conn, const SrsContextId &cid)
    // {
    //     conn_ = conn;
    //     cid_ = cid;
    //     trd_ = NULL;
    //     is_started_ = false;
    // }

    // SrsRtspPlayStream::~SrsRtspPlayStream()
    // {
    // }

    // srs_error_t SrsRtspPlayStream::start()
    // {
    //     srs_error_t err = srs_success;

    //     // If player coroutine allocated, we think the player is started.
    //     // To prevent play multiple times for this play stream.
    //     // @remark Allow start multiple times, for DTLS may retransmit the final packet.
    //     if (is_started_) {
    //         return err;
    //     }

    //     srs_freep(trd_);
    //     trd_ = new SrsFastCoroutine("rtc_sender", this, cid_);

    //     if ((err = trd_->start()) != srs_success) {
    //         return srs_error_wrap(err, "rtc_sender");
    //     }

    //     is_started_ = true;

    //     return err;
    // }

    // void SrsRtspPlayStream::stop()
    // {
    //     if (trd_) {
    //         trd_->stop();
    //     }
    // }

    // srs_error_t SrsRtspPlayStream::cycle()
    // {
    //     srs_error_t err = srs_success;

    //     SrsSharedPtr<SrsRtcSource>& source = source_;
    //     srs_assert(source.get());

    //     SrsRtcConsumer* consumer_raw = NULL;
    //     if ((err = source->create_consumer(consumer_raw)) != srs_success) {
    //         return srs_error_wrap(err, "create consumer, source=%s", req_->get_stream_url().c_str());
    //     }

    //     srs_assert(consumer_raw);
    //     SrsUniquePtr<SrsRtcConsumer> consumer(consumer_raw);

    //     consumer->set_handler(this);

    //     // TODO: FIXME: Dumps the SPS/PPS from gop cache, without other frames.
    //     if ((err = source->consumer_dumps(consumer.get())) != srs_success) {
    //         return srs_error_wrap(err, "dumps consumer, url=%s", req_->get_stream_url().c_str());
    //     }

    //     realtime = _srs_config->get_realtime_enabled(req_->vhost, true);
    //     mw_msgs = _srs_config->get_mw_msgs(req_->vhost, realtime, true);

    //     // TODO: FIXME: Add cost in ms.
    //     SrsContextId cid = source->source_id();
    //     srs_trace("RTC: start play url=%s, source_id=%s/%s, realtime=%d, mw_msgs=%d", req_->get_stream_url().c_str(),
    //         cid.c_str(), source->pre_source_id().c_str(), realtime, mw_msgs);

    //     SrsUniquePtr<SrsErrorPithyPrint> epp(new SrsErrorPithyPrint());

    //     while (true) {
    //         if ((err = trd_->pull()) != srs_success) {
    //             return srs_error_wrap(err, "rtc sender thread");
    //         }

    //         // Wait for amount of packets.
    //         SrsRtpPacket* pkt = NULL;
    //         consumer->dump_packet(&pkt);
    //         if (!pkt) {
    //             // TODO: FIXME: We should check the quit event.
    //             consumer->wait(mw_msgs);
    //             continue;
    //         }

    //         // Send-out the RTP packet and do cleanup
    //         // @remark Note that the pkt might be set to NULL.
    //         if ((err = send_packet(pkt)) != srs_success) {
    //             uint32_t nn = 0;
    //             if (epp->can_print(err, &nn)) {
    //                 srs_warn("play send packets=%u, nn=%u/%u, err: %s", 1, epp->nn_count, nn, srs_error_desc(err).c_str());
    //             }
    //             srs_freep(err);
    //         }

    //         // Free the packet.
    //         // @remark Note that the pkt might be set to NULL.
    //         srs_freep(pkt);
    //     }
    // }

    // srs_error_t SrsRtspPlayStream::initialize(SrsRequest * request)
    // {
    //     return srs_success;
    // }


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

    if ((err = srs_fd_reuseaddr(fd)) != srs_success) {
        // ::close(fd);
        return srs_error_wrap(err, "set reuseaddr");
    }

    // Bind to local port 8000 on all interfaces
    struct sockaddr_in local_addr;
    memset(&local_addr, 0, sizeof(local_addr));
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    local_addr.sin_port = htons(8000);

    if (::bind(fd, (struct sockaddr*)&local_addr, sizeof(local_addr)) < 0) {
        int e = errno;
        // ::close(fd);
        return srs_error_new(ERROR_SOCKET_BIND, "bind local port 8000 failed, errno=%d", e);
    }

    // Wrap the socket in stfd
    stfd_ = srs_netfd_open_socket(fd);
    srs_assert(stfd_);

    srs_trace("udp client %s:%d, fd=%d, local_port=8000", ip.c_str(), port, fd);

    return err;
}

int SrsUdpClient::sendto(void* data, int len)
{
    return srs_sendto(stfd_, data, len, (sockaddr*)addr_, sizeof(sockaddr_in), SRS_UTIME_NO_TIMEOUT);
}