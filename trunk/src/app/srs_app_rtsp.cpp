#include <srs_app_config.hpp>
#include <srs_app_rtsp.hpp>
#include <srs_app_statistic.hpp>
#include <srs_app_utility.hpp>
#include <srs_app_rtc_sdp.hpp>
#include <srs_app_rtc_conn.hpp>
#include <srs_app_rtc_server.hpp>
#include <srs_app_rtc_source.hpp>
#include <sstream>

extern SrsResourceManager* _srs_rtc_manager;

#define SRS_RTSP_PACKET_MAX 1500

SrsRtspConn::SrsRtspConn(ISrsProtocolReadWriter* skt, std::string cip, int port) : SrsRtcConnection(NULL, _srs_context->get_id())
{
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
}

SrsRtspConn::~SrsRtspConn()
{
    srs_freepa(pkt_);
    srs_freep(delta_);
    srs_freep(skt_);
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

    // Notify manager to remove it.
    // Note that we create this object, so we use manager to remove it.
    // manager_->remove(this);

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

            SrsRtspDescribeResponse* res = new SrsRtspDescribeResponse((int)req->seq);
            res->session = session_;
            SrsSdp local_sdp;
            local_sdp.version_ = "0";
            local_sdp.username_ = "SRS RTSP Server";
            local_sdp.session_id_ = "0";
            local_sdp.session_version_ = "0";
            local_sdp.nettype_ = "IN";
            local_sdp.addrtype_ = "IP4";
            local_sdp.unicast_address_ = "0.0.0.0"; // Parse from CANDIDATE
            local_sdp.session_name_ = "Play";
            local_sdp.control_ = req->uri;

            local_sdp.media_descs_.push_back(SrsMediaDesc("audio"));
            SrsMediaDesc& media_audio = local_sdp.media_descs_.at(0);
            media_audio.port_ = 8554; // Read from config.
            media_audio.protos_ = "RTP/AVP/TCP";
            media_audio.control_ = req->uri + "/trackID=1";
            media_audio.recvonly_ = true;
            
            media_audio.payload_types_.push_back(SrsMediaPayloadType(104));
            SrsMediaPayloadType& ps_audio = media_audio.payload_types_.at(0);
            ps_audio.encoding_name_ = "AAC";
            ps_audio.clock_rate_ = 90000;

            local_sdp.media_descs_.push_back(SrsMediaDesc("video"));
            SrsMediaDesc& media_video = local_sdp.media_descs_.at(1);
            media_video.port_ = 8554; // Read from config.
            media_video.protos_ = "RTP/AVP/TCP";
            media_video.control_ = req->uri + "/trackID=2";
            media_video.recvonly_ = true;

            media_video.payload_types_.push_back(SrsMediaPayloadType(96));
            SrsMediaPayloadType& ps_video = media_video.payload_types_.at(0);
            ps_video.encoding_name_ = "H264";
            ps_video.clock_rate_ = 90000;

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
            // int lpm = 0;
            // if ((err = caster->alloc_port(&lpm)) != srs_success) {
            //     return srs_error_wrap(err, "alloc port");
            // }
            
            // SrsRtpConn* rtp = NULL;
            // if (req->stream_id == video_id) {
            //     srs_freep(video_rtp);
            //     rtp = video_rtp = new SrsRtpConn(this, lpm, video_id);
            // } else {
            //     srs_freep(audio_rtp);
            //     rtp = audio_rtp = new SrsRtpConn(this, lpm, audio_id);
            // }
            // if ((err = rtp->listen()) != srs_success) {
            //     return srs_error_wrap(err, "rtp listen");
            // }
            // srs_trace("rtsp: #%d %s over %s/%s/%s %s client-port=%d-%d, server-port=%d-%d",
            //     req->stream_id, (req->stream_id == video_id)? "Video":"Audio",
            //     req->transport->transport.c_str(), req->transport->profile.c_str(), req->transport->lower_transport.c_str(),
            //     req->transport->cast_type.c_str(), req->transport->client_port_min, req->transport->client_port_max,
            //     lpm, lpm + 1);
            
            // create session.
            if (session_.empty()) {
                session_ = "O9EaZ4bf"; // TODO: FIXME: generate session id.
            }
            
            SrsRtspSetupResponse* res = new SrsRtspSetupResponse((int)req->seq);
            res->transport->copy(req->transport);
            res->session = session_;
            res->ssrc = "1375e756";
            if (res->transport->lower_transport != "TCP") {
                res->status = SRS_CONSTS_RTSP_UnsupportedTransport;
            }
            if ((err = rtsp_->send_message(res)) != srs_success) {
                return srs_error_wrap(err, "response setup");
            }

            SrsRtspConn* rtsp_conn = dynamic_cast<SrsRtspConn*>(_srs_rtc_manager->find_by_name(session_));
            if (!rtsp_conn) {
                _srs_rtc_manager->subscribe(this);
            }

            // Ignore if exists.
            if(players_.end() != players_.find(request_->get_stream_url())) {
                return err;
            }

            // sub_relations[1] = new SrsRtcTrackDescription("audio");

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
        } else {
            SrsRtspResponse* res = new SrsRtspResponse((int)req->seq);
            res->session = session_;
            if ((err = rtsp_->send_message(res)) != srs_success) {
                return srs_error_wrap(err, "response unsupported");
            }
        }
    }
    
    return err;
}


// SrsRtspPlayStream::SrsRtspPlayStream(SrsRtspConn * conn, const SrsContextId & cid)
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

SrsRtspServer::SrsRtspServer()
{
}

SrsRtspServer::~SrsRtspServer()
{
}

srs_error_t SrsRtspServer::exec_async_work(ISrsAsyncCallTask* t)
{
    return srs_success;
}

srs_error_t SrsRtspServer::listen_udp()
{
    srs_error_t err = srs_success;

    std::string ip = srs_any_address_for_listener();
    int port = 8554;
    srs_assert(listeners.empty());

    SrsUdpMuxListener* listener = new SrsUdpMuxListener(this, ip, port);
    if ((err = listener->listen()) != srs_success) {
        srs_freep(listener);
        return srs_error_wrap(err, "listen %s:%d", ip.c_str(), port);
    }

    srs_trace("rtsp listen at udp://%s:%d, fd=%d", ip.c_str(), port, listener->fd());
    listeners.push_back(listener);

    return err;
}

srs_error_t SrsRtspServer::on_udp_packet(SrsUdpMuxSocket* skt)
{
    return srs_success;
}
