//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#include <srs_app_srt_conn.hpp>

using namespace std;

#include <srs_kernel_buffer.hpp>
#include <srs_kernel_flv.hpp>
#include <srs_kernel_stream.hpp>
#include <srs_core_autofree.hpp>
#include <srs_protocol_rtmp_stack.hpp>
#include <srs_protocol_srt.hpp>
#include <srs_app_config.hpp>
#include <srs_app_http_hooks.hpp>
#include <srs_app_pithy_print.hpp>
#include <srs_app_srt_server.hpp>
#include <srs_app_srt_source.hpp>
#include <srs_app_rtc_source.hpp>
#include <srs_app_statistic.hpp>
#include <srs_protocol_rtmp_stack.hpp>
#include <srs_kernel_utility.hpp>

SrsSrtConnection::SrsSrtConnection(srs_srt_t srt_fd)
{
    srt_fd_ = srt_fd;
    srt_skt_ = new SrsSrtSocket(_srt_eventloop->poller(), srt_fd_);
}

SrsSrtConnection::~SrsSrtConnection()
{
    srs_freep(srt_skt_);
}

srs_error_t SrsSrtConnection::initialize()
{
    srs_error_t err = srs_success;
    return err;
}

void SrsSrtConnection::set_recv_timeout(srs_utime_t tm)
{
    srt_skt_->set_recv_timeout(tm);
}

srs_utime_t SrsSrtConnection::get_recv_timeout()
{
    return srt_skt_->get_recv_timeout();
}

srs_error_t SrsSrtConnection::read_fully(void* buf, size_t size, ssize_t* nread)
{
    return srs_error_new(ERROR_SRT_CONN, "unsupport method");
}

int64_t SrsSrtConnection::get_recv_bytes()
{
    return srt_skt_->get_recv_bytes();
}

int64_t SrsSrtConnection::get_send_bytes()
{
    return srt_skt_->get_send_bytes();
}

srs_error_t SrsSrtConnection::read(void* buf, size_t size, ssize_t* nread)
{
    return srt_skt_->recvmsg(buf, size, nread);
}

void SrsSrtConnection::set_send_timeout(srs_utime_t tm)
{
    srt_skt_->set_send_timeout(tm);
}

srs_utime_t SrsSrtConnection::get_send_timeout()
{
    return srt_skt_->get_send_timeout();
}

srs_error_t SrsSrtConnection::write(void* buf, size_t size, ssize_t* nwrite)
{
    return srt_skt_->sendmsg(buf, size, nwrite);
}

srs_error_t SrsSrtConnection::writev(const iovec *iov, int iov_size, ssize_t* nwrite)
{
    return srs_error_new(ERROR_SRT_CONN, "unsupport method");
}

SrsSrtRecvThread::SrsSrtRecvThread(SrsSrtConnection* srt_conn)
{
    srt_conn_ = srt_conn;
    trd_ = new SrsSTCoroutine("srt-recv", this, _srs_context->get_id());
    recv_err_ = srs_success;
}

SrsSrtRecvThread::~SrsSrtRecvThread()
{
    srs_freep(trd_);
    srs_error_reset(recv_err_);
}

srs_error_t SrsSrtRecvThread::cycle()
{
    srs_error_t err = srs_success;

    if ((err = do_cycle()) != srs_success) {
        recv_err_ = srs_error_copy(err);
    }

    return err;
}

srs_error_t SrsSrtRecvThread::do_cycle()
{
    srs_error_t err = srs_success;

    while (true) {
        if ((err = trd_->pull()) != srs_success) {
            return srs_error_wrap(err, "srt: thread quit");
        }

        char buf[1316];
        ssize_t nb = 0;
        if ((err = srt_conn_->read(buf, sizeof(buf), &nb)) != srs_success) {
            if (srs_error_code(err) != ERROR_SRT_TIMEOUT) {
                return srs_error_wrap(err, "srt read");
            }
        }
    }

    return err;
}

srs_error_t SrsSrtRecvThread::start()
{
    srs_error_t err = srs_success;

    if ((err = trd_->start()) != srs_success) {
        return srs_error_wrap(err, "start srt recv thread");
    }

    return err;
}

srs_error_t SrsSrtRecvThread::get_recv_err()
{
    return srs_error_copy(recv_err_);
}

SrsMpegtsSrtConn::SrsMpegtsSrtConn(SrsSrtServer* srt_server, srs_srt_t srt_fd, std::string ip, int port)
{
    // Create a identify for this client.
    _srs_context->set_id(_srs_context->generate_id());

    srt_server_ = srt_server;

    srt_fd_ = srt_fd;
    srt_conn_ = new SrsSrtConnection(srt_fd_);
    ip_ = ip;
    port_ = port;

    kbps_ = new SrsNetworkKbps();
    kbps_->set_io(srt_conn_, srt_conn_);
    delta_ = new SrsNetworkDelta();
    delta_->set_io(srt_conn_, srt_conn_);

    trd_ = new SrsSTCoroutine("ts-srt", this, _srs_context->get_id());

    srt_source_ = NULL;
    req_ = new SrsRequest();
    req_->ip = ip;
}

SrsMpegtsSrtConn::~SrsMpegtsSrtConn()
{
    srs_freep(trd_);

    srs_freep(kbps_);
    srs_freep(delta_);
    srs_freep(srt_conn_);
    srs_freep(req_);
}

std::string SrsMpegtsSrtConn::desc()
{
    return "srt-ts-conn";
}

ISrsKbpsDelta* SrsMpegtsSrtConn::delta()
{
    return delta_;
}

void SrsMpegtsSrtConn::expire()
{
    trd_->interrupt();
}

srs_error_t SrsMpegtsSrtConn::start()
{
    srs_error_t err = srs_success;

    if ((err = trd_->start()) != srs_success) {
        return srs_error_wrap(err, "coroutine");
    }

    return err;
}

std::string SrsMpegtsSrtConn::remote_ip()
{
    return ip_;
}

const SrsContextId& SrsMpegtsSrtConn::get_id()
{
    return trd_->cid();
}

srs_error_t SrsMpegtsSrtConn::cycle()
{
    srs_error_t err = do_cycle();

    // Update statistic when done.
    SrsStatistic* stat = SrsStatistic::instance();
    stat->kbps_add_delta(get_id().c_str(), delta_);
    stat->on_disconnect(get_id().c_str(), err);

    // Notify manager to remove it.
    // Note that we create this object, so we use manager to remove it.
    srt_server_->remove(this);

    // success.
    if (err == srs_success) {
        srs_trace("srt client finished.");
        return err;
    }

    srs_error("srt serve error %s", srs_error_desc(err).c_str());
    srs_freep(err);
    return srs_success;
}

srs_error_t SrsMpegtsSrtConn::do_cycle()
{
    srs_error_t err = srs_success;

    srs_trace("SRT client ip=%s:%d, fd=%d", ip_.c_str(), port_, srt_fd_);

    string streamid = "";
    if ((err = srs_srt_get_streamid(srt_fd_, streamid)) != srs_success) {
        return srs_error_wrap(err, "get srt streamid");
    }

    // If streamid empty, using default streamid instead.
    if (streamid.empty()) {
        streamid = "#!::r=live/livestream,m=publish";
        srs_warn("srt get empty streamid, using default steramid %s instead", streamid.c_str());
    }

    // Detect streamid of srt to request.
    SrtMode mode = SrtModePull;
    if (!srs_srt_streamid_to_request(streamid, mode, req_)) {
        return srs_error_new(ERROR_SRT_CONN, "invalid srt streamid=%s", streamid.c_str());
    }

    // discovery vhost, resolve the vhost from config
    SrsConfDirective* parsed_vhost = _srs_config->get_vhost(req_->vhost);
    if (parsed_vhost) {
        req_->vhost = parsed_vhost->arg0();
    }

    if (! _srs_config->get_srt_enabled(req_->vhost)) {
        return srs_error_new(ERROR_SRT_CONN, "srt disabled, vhost=%s", req_->vhost.c_str());
    }

    srs_trace("@srt, streamid=%s, stream_url=%s, vhost=%s, app=%s, stream=%s, param=%s",
              streamid.c_str(), req_->get_stream_url().c_str(), req_->vhost.c_str(), req_->app.c_str(), req_->stream.c_str(), req_->param.c_str());

    if ((err = _srs_srt_sources->fetch_or_create(req_, &srt_source_)) != srs_success) {
        return srs_error_wrap(err, "fetch srt source");
    }

    if ((err = http_hooks_on_connect()) != srs_success) {
        return srs_error_wrap(err, "on connect");
    }

    if (mode == SrtModePush) {
        err = publishing();
    } else if (mode == SrtModePull) {
        err = playing();
    }
    
    http_hooks_on_close();
    
    return err;
}

srs_error_t SrsMpegtsSrtConn::publishing()
{
    srs_error_t err = srs_success;

    // We must do stat the client before hooks, because hooks depends on it.
    SrsStatistic* stat = SrsStatistic::instance();
    if ((err = stat->on_client(_srs_context->get_id().c_str(), req_, this, SrsSrtConnPublish)) != srs_success) {
        return srs_error_wrap(err, "srt: stat client");
    }

    // We must do hook after stat, because depends on it.
    if ((err = http_hooks_on_publish()) != srs_success) {
        return srs_error_wrap(err, "srt: callback on publish");
    }
    
    if ((err = acquire_publish()) == srs_success) {
        err = do_publishing();
        release_publish();
    }
    
    http_hooks_on_unpublish();
    
    return err;
}

srs_error_t SrsMpegtsSrtConn::playing()
{
    srs_error_t err = srs_success;

    // We must do stat the client before hooks, because hooks depends on it.
    SrsStatistic* stat = SrsStatistic::instance();
    if ((err = stat->on_client(_srs_context->get_id().c_str(), req_, this, SrsSrtConnPlay)) != srs_success) {
        return srs_error_wrap(err, "rtmp: stat client");
    }

    // We must do hook after stat, because depends on it.
    if ((err = http_hooks_on_play()) != srs_success) {
        return srs_error_wrap(err, "rtmp: callback on play");
    }
    
    err = do_playing();
    http_hooks_on_stop();
    
    return err;
}

// TODO: FIXME: It's not atomic and has risk between multiple source checking.
srs_error_t SrsMpegtsSrtConn::acquire_publish()
{
    srs_error_t err = srs_success;

    // Check srt stream is busy.
    if (! srt_source_->can_publish()) {
        return srs_error_new(ERROR_SRT_SOURCE_BUSY, "srt stream %s busy", req_->get_stream_url().c_str());
    }

    // Check rtmp stream is busy.
    SrsLiveSource *live_source = _srs_sources->fetch(req_);
    if (live_source && !live_source->can_publish(false)) {
        return srs_error_new(ERROR_SYSTEM_STREAM_BUSY, "live_source stream %s busy", req_->get_stream_url().c_str());
    }

    if ((err = _srs_sources->fetch_or_create(req_, _srs_hybrid->srs()->instance(), &live_source)) != srs_success) {
        return srs_error_wrap(err, "create source");
    }

    srs_assert(live_source != NULL);

    bool enabled_cache = _srs_config->get_gop_cache(req_->vhost);
    int gcmf = _srs_config->get_gop_cache_max_frames(req_->vhost);
    live_source->set_cache(enabled_cache);
    live_source->set_gop_cache_max_frames(gcmf);

    // srt->rtmp->rtc
    // TODO: FIXME: the code below is repeat in srs_app_rtmp_conn.cpp, refactor it later, use function instead.

    // Check whether RTC stream is busy.
#ifdef SRS_RTC
    SrsRtcSource* rtc = NULL;
    bool rtc_server_enabled = _srs_config->get_rtc_server_enabled();
    bool rtc_enabled = _srs_config->get_rtc_enabled(req_->vhost);
    bool edge = _srs_config->get_vhost_is_edge(req_->vhost);
    if (rtc_server_enabled && rtc_enabled && ! edge) {
        if ((err = _srs_rtc_sources->fetch_or_create(req_, &rtc)) != srs_success) {
            return srs_error_wrap(err, "create source");
        }

        if (!rtc->can_publish()) {
            return srs_error_new(ERROR_SYSTEM_STREAM_BUSY, "rtc stream %s busy", req_->get_stream_url().c_str());
        }
    }
#endif

    if (_srs_config->get_srt_to_rtmp(req_->vhost)) {
        // Bridge to RTMP and RTC streaming.
        SrsCompositeBridge* bridge = new SrsCompositeBridge();
        bridge->append(new SrsFrameToRtmpBridge(live_source));

#if defined(SRS_RTC) && defined(SRS_FFMPEG_FIT)
        if (rtc && _srs_config->get_rtc_from_rtmp(req_->vhost)) {
            bridge->append(new SrsFrameToRtcBridge(rtc));
        }
#endif

        if ((err = bridge->initialize(req_)) != srs_success) {
            srs_freep(bridge);
            return srs_error_wrap(err, "create bridge");
        }

        srt_source_->set_bridge(bridge);
    }

    if ((err = srt_source_->on_publish()) != srs_success) {
        return srs_error_wrap(err, "srt source publish");
    }

    return err;
}

void SrsMpegtsSrtConn::release_publish()
{
    srt_source_->on_unpublish();
}

srs_error_t SrsMpegtsSrtConn::do_publishing()
{
    srs_error_t err = srs_success;

    SrsPithyPrint* pprint = SrsPithyPrint::create_srt_publish();
    SrsAutoFree(SrsPithyPrint, pprint);

    int nb_packets = 0;

    // Max udp packet size equal to 1500.
    char buf[1500];
    while (true) {
        if ((err = trd_->pull()) != srs_success) {
            return srs_error_wrap(err, "srt: thread quit");
        }

        pprint->elapse();
        if (pprint->can_print()) {
            SrsSrtStat s;
            if ((err = s.fetch(srt_fd_, true)) != srs_success) {
                srs_freep(err);
            } else {
                srs_trace("<- " SRS_CONSTS_LOG_SRT_PUBLISH " Transport Stats # pktRecv=%" PRId64 ", pktRcvLoss=%d, pktRcvRetrans=%d, pktRcvDrop=%d",
                    s.pktRecv(), s.pktRcvLoss(), s.pktRcvRetrans(), s.pktRcvDrop());
            }

            kbps_->sample();
            srs_trace("<- " SRS_CONSTS_LOG_SRT_PUBLISH " time=%" PRId64 ", packets=%d, okbps=%d,%d,%d, ikbps=%d,%d,%d",
                srsu2ms(pprint->age()), nb_packets, kbps_->get_send_kbps(), kbps_->get_send_kbps_30s(), kbps_->get_send_kbps_5m(),
                kbps_->get_recv_kbps(), kbps_->get_recv_kbps_30s(), kbps_->get_recv_kbps_5m());
            nb_packets = 0;
        }

        ssize_t nb = 0;
        if ((err = srt_conn_->read(buf, sizeof(buf), &nb)) != srs_success) {
            return srs_error_wrap(err, "srt: recvmsg");
        }

        ++nb_packets;

        if ((err = on_srt_packet(buf, nb)) != srs_success) {
            return srs_error_wrap(err, "srt: process packet");
        }
    }
        
    return err;
}

srs_error_t SrsMpegtsSrtConn::do_playing()
{
    srs_error_t err = srs_success;

    SrsSrtConsumer* consumer = NULL;
    SrsAutoFree(SrsSrtConsumer, consumer);
    if ((err = srt_source_->create_consumer(consumer)) != srs_success) {
        return srs_error_wrap(err, "create consumer, ts source=%s", req_->get_stream_url().c_str());
    }
    srs_assert(consumer);

    // TODO: FIXME: Dumps the SPS/PPS from gop cache, without other frames.
    if ((err = srt_source_->consumer_dumps(consumer)) != srs_success) {
        return srs_error_wrap(err, "dumps consumer, url=%s", req_->get_stream_url().c_str());
    }

    SrsPithyPrint* pprint = SrsPithyPrint::create_srt_play();
    SrsAutoFree(SrsPithyPrint, pprint);

    SrsSrtRecvThread srt_recv_trd(srt_conn_);
    if ((err = srt_recv_trd.start()) != srs_success) {
        return srs_error_wrap(err, "start srt recv trd");
    }

    int nb_packets = 0;

    while (true) {
        if ((err = trd_->pull()) != srs_success) {
            return srs_error_wrap(err, "srt play thread");
        }

        if ((err = srt_recv_trd.get_recv_err()) != srs_success) {
            return srs_error_wrap(err, "srt play recv thread");
        }

        // Wait for amount of packets.
        SrsSrtPacket* pkt = NULL;
        SrsAutoFree(SrsSrtPacket, pkt);
        consumer->dump_packet(&pkt);
        if (!pkt) {
            // TODO: FIXME: We should check the quit event.
            consumer->wait(1, 1000 * SRS_UTIME_MILLISECONDS);
            continue;
        }

        ++nb_packets;

        // reportable
        pprint->elapse();
        if (pprint->can_print()) {
            SrsSrtStat s;
            if ((err = s.fetch(srt_fd_, true)) != srs_success) {
                srs_freep(err);
            } else {
                srs_trace("-> " SRS_CONSTS_LOG_SRT_PLAY " Transport Stats # pktSent=%" PRId64 ", pktSndLoss=%d, pktRetrans=%d, pktSndDrop=%d",
                    s.pktSent(), s.pktSndLoss(), s.pktRetrans(), s.pktSndDrop());
            }

            kbps_->sample();
            srs_trace("-> " SRS_CONSTS_LOG_SRT_PLAY " time=%" PRId64 ", packets=%d, okbps=%d,%d,%d, ikbps=%d,%d,%d",
                srsu2ms(pprint->age()), nb_packets, kbps_->get_send_kbps(), kbps_->get_send_kbps_30s(), kbps_->get_send_kbps_5m(),
                kbps_->get_recv_kbps(), kbps_->get_recv_kbps_30s(), kbps_->get_recv_kbps_5m());
            nb_packets = 0;
        }

        ssize_t nb_write = 0;
        if ((err = srt_conn_->write(pkt->data(), pkt->size(), &nb_write)) != srs_success) {
            return srs_error_wrap(err, "srt send, size=%d", pkt->size());
        }

        // Yield to another coroutines.
        // @see https://github.com/ossrs/srs/issues/2194#issuecomment-777542162
        // TODO: FIXME: Please check whether SRT sendmsg causing clock deviation, see srs_thread_yield of SrsUdpMuxSocket::sendto
    }

    return err;
}

srs_error_t SrsMpegtsSrtConn::on_srt_packet(char* buf, int nb_buf)
{
    srs_error_t err = srs_success;

    // Ignore if invalid length.
    if (nb_buf <= 0) {
        return err;
    }

    // Check srt payload, mpegts must be N times of SRS_TS_PACKET_SIZE
    if ((nb_buf % SRS_TS_PACKET_SIZE) != 0) {
        return srs_error_new(ERROR_SRT_CONN, "invalid ts packet len=%d", nb_buf);
    }

    // Check srt payload, the first byte must be 0x47
    if (buf[0] != 0x47) {
        return srs_error_new(ERROR_SRT_CONN, "invalid ts packet first=%#x", (uint8_t)buf[0]);
    }

    SrsSrtPacket* packet = new SrsSrtPacket();
    SrsAutoFree(SrsSrtPacket, packet);
    packet->wrap(buf, nb_buf);

    if ((err = srt_source_->on_packet(packet)) != srs_success) {
        return srs_error_wrap(err, "on srt packet");
    }
    
    return err;
}

srs_error_t SrsMpegtsSrtConn::http_hooks_on_connect()
{
    srs_error_t err = srs_success;
    
    if (!_srs_config->get_vhost_http_hooks_enabled(req_->vhost)) {
        return err;
    }
    
    // the http hooks will cause context switch,
    // so we must copy all hooks for the on_connect may freed.
    // @see https://github.com/ossrs/srs/issues/475
    vector<string> hooks;
    
    if (true) {
        SrsConfDirective* conf = _srs_config->get_vhost_on_connect(req_->vhost);
        
        if (!conf) {
            return err;
        }
        
        hooks = conf->args;
    }
    
    for (int i = 0; i < (int)hooks.size(); i++) {
        std::string url = hooks.at(i);
        if ((err = SrsHttpHooks::on_connect(url, req_)) != srs_success) {
            return srs_error_wrap(err, "srt on_connect %s", url.c_str());
        }
    }
    
    return err;
}

void SrsMpegtsSrtConn::http_hooks_on_close()
{
    if (!_srs_config->get_vhost_http_hooks_enabled(req_->vhost)) {
        return;
    }
    
    // the http hooks will cause context switch,
    // so we must copy all hooks for the on_connect may freed.
    // @see https://github.com/ossrs/srs/issues/475
    vector<string> hooks;
    
    if (true) {
        SrsConfDirective* conf = _srs_config->get_vhost_on_close(req_->vhost);
        
        if (!conf) {
            return;
        }
        
        hooks = conf->args;
    }
    
    for (int i = 0; i < (int)hooks.size(); i++) {
        std::string url = hooks.at(i);
        SrsHttpHooks::on_close(url, req_, srt_conn_->get_send_bytes(), srt_conn_->get_recv_bytes());
    }
}

srs_error_t SrsMpegtsSrtConn::http_hooks_on_publish()
{
    srs_error_t err = srs_success;
    
    if (!_srs_config->get_vhost_http_hooks_enabled(req_->vhost)) {
        return err;
    }
    
    // the http hooks will cause context switch,
    // so we must copy all hooks for the on_connect may freed.
    // @see https://github.com/ossrs/srs/issues/475
    vector<string> hooks;
    
    if (true) {
        SrsConfDirective* conf = _srs_config->get_vhost_on_publish(req_->vhost);
        
        if (!conf) {
            return err;
        }
        
        hooks = conf->args;
    }
    
    for (int i = 0; i < (int)hooks.size(); i++) {
        std::string url = hooks.at(i);
        if ((err = SrsHttpHooks::on_publish(url, req_)) != srs_success) {
            return srs_error_wrap(err, "srt on_publish %s", url.c_str());
        }
    }
    
    return err;
}

void SrsMpegtsSrtConn::http_hooks_on_unpublish()
{
    if (!_srs_config->get_vhost_http_hooks_enabled(req_->vhost)) {
        return;
    }
    
    // the http hooks will cause context switch,
    // so we must copy all hooks for the on_connect may freed.
    // @see https://github.com/ossrs/srs/issues/475
    vector<string> hooks;
    
    if (true) {
        SrsConfDirective* conf = _srs_config->get_vhost_on_unpublish(req_->vhost);
        
        if (!conf) {
            return;
        }
        
        hooks = conf->args;
    }
    
    for (int i = 0; i < (int)hooks.size(); i++) {
        std::string url = hooks.at(i);
        SrsHttpHooks::on_unpublish(url, req_);
    }
}

srs_error_t SrsMpegtsSrtConn::http_hooks_on_play()
{
    srs_error_t err = srs_success;
    
    if (!_srs_config->get_vhost_http_hooks_enabled(req_->vhost)) {
        return err;
    }
    
    // the http hooks will cause context switch,
    // so we must copy all hooks for the on_connect may freed.
    // @see https://github.com/ossrs/srs/issues/475
    vector<string> hooks;
    
    if (true) {
        SrsConfDirective* conf = _srs_config->get_vhost_on_play(req_->vhost);
        
        if (!conf) {
            return err;
        }
        
        hooks = conf->args;
    }
    
    for (int i = 0; i < (int)hooks.size(); i++) {
        std::string url = hooks.at(i);
        if ((err = SrsHttpHooks::on_play(url, req_)) != srs_success) {
            return srs_error_wrap(err, "srt on_play %s", url.c_str());
        }
    }
    
    return err;
}

void SrsMpegtsSrtConn::http_hooks_on_stop()
{
    if (!_srs_config->get_vhost_http_hooks_enabled(req_->vhost)) {
        return;
    }
    
    // the http hooks will cause context switch,
    // so we must copy all hooks for the on_connect may freed.
    // @see https://github.com/ossrs/srs/issues/475
    vector<string> hooks;
    
    if (true) {
        SrsConfDirective* conf = _srs_config->get_vhost_on_stop(req_->vhost);
        
        if (!conf) {
            return;
        }
        
        hooks = conf->args;
    }
    
    for (int i = 0; i < (int)hooks.size(); i++) {
        std::string url = hooks.at(i);
        SrsHttpHooks::on_stop(url, req_);
    }
    
    return;
}
