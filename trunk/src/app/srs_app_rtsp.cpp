#include <srs_app_rtsp.hpp>
#include <srs_app_statistic.hpp>
#include <srs_app_utility.hpp>
#include <srs_app_rtc_sdp.hpp>
#include <sstream>

#define SRS_RTSP_PACKET_MAX 1500

extern bool srs_is_rtp_or_rtcp(const uint8_t* data, size_t len);
extern bool srs_is_rtcp(const uint8_t* data, size_t len);

SrsRtspConn::SrsRtspConn()
{
    wrapper_ = NULL;
    owner_coroutine_ = NULL;
    owner_cid_ = NULL;
    cid_ = _srs_context->get_id();

    pkt_ = NULL;
    delta_ = NULL;
    skt_ = NULL;
    trd_ = NULL;
    rtsp_ = NULL;
}

SrsRtspConn::SrsRtspConn(ISrsResourceManager* cm,  ISrsProtocolReadWriter* skt, std::string cip, int port) : SrsRtspConn()
{
    ip_ = cip;
    port_ = port;
    skt_ = skt;
    rtsp_ = new SrsRtspStack(skt);
    trd_ = new SrsSTCoroutine("rtsp", this, _srs_context->get_id());

    manager_ = cm;
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

void SrsRtspConn::setup_owner(SrsSharedResource<SrsRtspConn>* wrapper, ISrsInterruptable* owner_coroutine, ISrsContextIdSetter* owner_cid)
{
    wrapper_ = wrapper;
    owner_coroutine_ = owner_coroutine;
    owner_cid_ = owner_cid;
}

ISrsKbpsDelta* SrsRtspConn::delta()
{
    return delta_;  
}

void SrsRtspConn::interrupt()
{
    if (owner_coroutine_) owner_coroutine_->interrupt();
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

void SrsRtspConn::on_executor_done(ISrsInterruptable* executor)
{
    owner_coroutine_ = NULL;
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
        } else if (req->is_play()) {
            SrsRtspResponse* res = new SrsRtspResponse((int)req->seq);
            res->session = session_;
            if ((err = rtsp_->send_message(res)) != srs_success) {
                return srs_error_wrap(err, "response record");
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

srs_error_t SrsRtspConn::read_packet(char* pkt, int* nb_pkt)
{
    srs_error_t err = srs_success;

    // Read length in 2 bytes @doc: https://www.rfc-editor.org/rfc/rfc4571#section-2
    ssize_t nread = 0; uint8_t b[2];
    if((err = skt_->read_fully((char*)b, sizeof(b), &nread)) != srs_success) {
        return srs_error_wrap(err, "rtc tcp conn read len");
    }

    uint16_t npkt = uint16_t(b[0])<<8 | uint16_t(b[1]);
    if (npkt > *nb_pkt) {
        return srs_error_new(ERROR_RTC_TCP_SIZE, "invalid size=%u exceed %d", npkt, *nb_pkt);
    }

    // Read a RTC pkt such as STUN, DTLS or RTP/RTCP
    if((err = skt_->read_fully(pkt, npkt, &nread)) != srs_success) {
        return srs_error_wrap(err, "rtc tcp conn read body");
    }

    *nb_pkt = npkt;

    return err;
}

srs_error_t SrsRtspConn::on_tcp_pkt(char* pkt, int nb_pkt)
{
    srs_error_t err = srs_success;

    bool is_rtp_or_rtcp = srs_is_rtp_or_rtcp((uint8_t*)pkt, nb_pkt);
    bool is_rtcp = srs_is_rtcp((uint8_t*)pkt, nb_pkt);

    // if (is_rtp_or_rtcp && !is_rtcp) {
    //     return session_->tcp()->on_rtp(pkt, nb_pkt);
    // }

    // if (is_rtp_or_rtcp && is_rtcp) {
    //     return session_->tcp()->on_rtcp(pkt, nb_pkt);
    // }

    return srs_error_new(ERROR_RTC_UDP, "unknown packet");
}









