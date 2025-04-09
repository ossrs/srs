#include <srs_app_rtsp.hpp>
#include <srs_app_statistic.hpp>
#include <srs_app_utility.hpp>

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

SrsRtspConn::SrsRtspConn(ISrsProtocolReadWriter* skt, std::string cip, int port) : SrsRtspConn()
{
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
    manager->remove(this);

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
            // if (rtsp_tcUrl.empty()) {
            //     rtsp_tcUrl = req->uri;
            // }
            // size_t pos = string::npos;
            // if ((pos = rtsp_tcUrl.rfind(".sdp")) != string::npos) {
            //     rtsp_tcUrl = rtsp_tcUrl.substr(0, pos);
            // }
            // srs_parse_rtmp_url(rtsp_tcUrl, rtsp_tcUrl, rtsp_stream);
            
            // srs_assert(req->sdp);
            // video_id = ::atoi(req->sdp->video_stream_id.c_str());
            // audio_id = ::atoi(req->sdp->audio_stream_id.c_str());
            // video_codec = req->sdp->video_codec;
            // audio_codec = req->sdp->audio_codec;
            // audio_sample_rate = ::atoi(req->sdp->audio_sample_rate.c_str());
            // audio_channel = ::atoi(req->sdp->audio_channel.c_str());
            // h264_sps = req->sdp->video_sps;
            // h264_pps = req->sdp->video_pps;
            // aac_specific_config = req->sdp->audio_sh;
            // srs_trace("rtsp: video(#%d, %s, %s/%s), audio(#%d, %s, %s/%s, %dHZ %dchannels), %s/%s",
            //           video_id, video_codec.c_str(), req->sdp->video_protocol.c_str(), req->sdp->video_transport_format.c_str(),
            //           audio_id, audio_codec.c_str(), req->sdp->audio_protocol.c_str(), req->sdp->audio_transport_format.c_str(),
            //           audio_sample_rate, audio_channel, rtsp_tcUrl.c_str(), rtsp_stream.c_str()
            //           );
            
            // SrsRtspResponse* res = new SrsRtspResponse((int)req->seq);
            // res->session = session;
            // if ((err = rtsp->send_message(res)) != srs_success) {
            //     return srs_error_wrap(err, "response announce");
            // }
        } else if (req->is_setup()) {
            // srs_assert(req->transport);
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
            
            // // create session.
            // if (session.empty()) {
            //     session = "O9EaZ4bf"; // TODO: FIXME: generate session id.
            // }
            
            // SrsRtspSetupResponse* res = new SrsRtspSetupResponse((int)req->seq);
            // res->client_port_min = req->transport->client_port_min;
            // res->client_port_max = req->transport->client_port_max;
            // res->local_port_min = lpm;
            // res->local_port_max = lpm + 1;
            // res->session = session;
            // if ((err = rtsp->send_message(res)) != srs_success) {
            //     return srs_error_wrap(err, "response setup");
            // }
        } else if (req->is_play()) {
            SrsRtspResponse* res = new SrsRtspResponse((int)req->seq);
            res->session = session_;
            if ((err = rtsp_->send_message(res)) != srs_success) {
                return srs_error_wrap(err, "response record");
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









