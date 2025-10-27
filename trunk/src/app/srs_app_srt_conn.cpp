//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_app_srt_conn.hpp>

using namespace std;

#include <srs_app_config.hpp>
#include <srs_app_factory.hpp>
#include <srs_app_http_hooks.hpp>
#include <srs_app_rtc_source.hpp>
#include <srs_app_rtmp_source.hpp>
#include <srs_app_srt_server.hpp>
#include <srs_app_srt_source.hpp>
#include <srs_app_statistic.hpp>
#include <srs_app_stream_token.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_buffer.hpp>
#include <srs_kernel_flv.hpp>
#include <srs_kernel_pithy_print.hpp>
#include <srs_kernel_stream.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_rtmp_stack.hpp>
#include <srs_protocol_srt.hpp>

ISrsSrtConnection::ISrsSrtConnection()
{
}

ISrsSrtConnection::~ISrsSrtConnection()
{
}

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

srs_srt_t SrsSrtConnection::srtfd()
{
    return srt_fd_;
}

srs_error_t SrsSrtConnection::get_streamid(std::string &streamid)
{
    return srs_srt_get_streamid(srt_fd_, streamid);
}

srs_error_t SrsSrtConnection::get_stats(SrsSrtStat &stat)
{
    return stat.fetch(srt_fd_, true);
}

void SrsSrtConnection::set_recv_timeout(srs_utime_t tm)
{
    srt_skt_->set_recv_timeout(tm);
}

srs_utime_t SrsSrtConnection::get_recv_timeout()
{
    return srt_skt_->get_recv_timeout();
}

srs_error_t SrsSrtConnection::read_fully(void *buf, size_t size, ssize_t *nread)
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

srs_error_t SrsSrtConnection::read(void *buf, size_t size, ssize_t *nread)
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

srs_error_t SrsSrtConnection::write(void *buf, size_t size, ssize_t *nwrite)
{
    return srt_skt_->sendmsg(buf, size, nwrite);
}

srs_error_t SrsSrtConnection::writev(const iovec *iov, int iov_size, ssize_t *nwrite)
{
    return srs_error_new(ERROR_SRT_CONN, "unsupport method");
}

ISrsSrtRecvThread::ISrsSrtRecvThread()
{
}

ISrsSrtRecvThread::~ISrsSrtRecvThread()
{
}

SrsSrtRecvThread::SrsSrtRecvThread(ISrsProtocolReadWriter *srt_conn)
{
    srt_conn_ = srt_conn;
    trd_ = new SrsSTCoroutine("srt-recv", this, _srs_context->get_id());
    recv_err_ = srs_success;
}

SrsSrtRecvThread::~SrsSrtRecvThread()
{
    srs_freep(trd_);
    srs_freep(recv_err_);
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

ISrsMpegtsSrtConnection::ISrsMpegtsSrtConnection()
{
}

ISrsMpegtsSrtConnection::~ISrsMpegtsSrtConnection()
{
}

SrsMpegtsSrtConn::SrsMpegtsSrtConn(ISrsResourceManager *resource_manager, srs_srt_t srt_fd, std::string ip, int port) : srt_source_(new SrsSrtSource())
{
    // Create a identify for this client.
    _srs_context->set_id(_srs_context->generate_id());

    resource_manager_ = resource_manager;

    srt_conn_ = new SrsSrtConnection(srt_fd);
    ip_ = ip;
    port_ = port;

    kbps_ = new SrsNetworkKbps();
    kbps_->set_io(srt_conn_, srt_conn_);
    delta_ = new SrsNetworkDelta();
    delta_->set_io(srt_conn_, srt_conn_);

    trd_ = new SrsSTCoroutine("ts-srt", this, _srs_context->get_id());

    req_ = new SrsRequest();
    req_->ip_ = ip;

    security_ = new SrsSecurity();

    stat_ = _srs_stat;
    config_ = _srs_config;
    stream_publish_tokens_ = _srs_stream_publish_tokens;
    srt_sources_ = _srs_srt_sources;
    live_sources_ = _srs_sources;
    rtc_sources_ = _srs_rtc_sources;
    hooks_ = _srs_hooks;
}

SrsMpegtsSrtConn::~SrsMpegtsSrtConn()
{
    srs_freep(trd_);

    srs_freep(kbps_);
    srs_freep(delta_);
    srs_freep(srt_conn_);
    srs_freep(req_);
    srs_freep(security_);

    stat_ = NULL;
    config_ = NULL;
    stream_publish_tokens_ = NULL;
    srt_sources_ = NULL;
    live_sources_ = NULL;
    rtc_sources_ = NULL;
    hooks_ = NULL;
}

std::string SrsMpegtsSrtConn::desc()
{
    return "srt-ts-conn";
}

ISrsKbpsDelta *SrsMpegtsSrtConn::delta()
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

void SrsMpegtsSrtConn::stop()
{
    trd_->interrupt();
    trd_->stop();
}

std::string SrsMpegtsSrtConn::remote_ip()
{
    return ip_;
}

const SrsContextId &SrsMpegtsSrtConn::get_id()
{
    return trd_->cid();
}

srs_error_t SrsMpegtsSrtConn::cycle()
{
    srs_error_t err = do_cycle();

    // Update statistic when done.
    stat_->kbps_add_delta(get_id().c_str(), delta_);
    stat_->on_disconnect(get_id().c_str(), err);

    // Notify manager to remove it.
    // Note that we create this object, so we use manager to remove it.
    resource_manager_->remove(this);

    // success.
    if (err == srs_success) {
        srs_trace("srt client finished.");
        return err;
    }

    // It maybe success with message.
    if (srs_error_code(err) == ERROR_SUCCESS) {
        srs_trace("srt client finished%s.", srs_error_summary(err).c_str());
        srs_freep(err);
        return err;
    }

    // client close peer.
    // TODO: FIXME: Only reset the error when client closed it.
    if (srs_is_client_gracefully_close(err)) {
        srs_warn("srt client disconnect peer. ret=%d", srs_error_code(err));
    } else if (srs_is_server_gracefully_close(err)) {
        srs_warn("srt server disconnect. ret=%d", srs_error_code(err));
    } else {
        srs_error("srt serve error %s", srs_error_desc(err).c_str());
    }

    srs_freep(err);
    return srs_success;
}

srs_error_t SrsMpegtsSrtConn::do_cycle()
{
    srs_error_t err = srs_success;

    srs_trace("SRT client ip=%s:%d, fd=%d", ip_.c_str(), port_, (int)srt_conn_->srtfd());

    string streamid = "";
    if ((err = srt_conn_->get_streamid(streamid)) != srs_success) {
        return srs_error_wrap(err, "get srt streamid");
    }

    // If streamid empty, using default streamid instead.
    if (streamid.empty()) {
        streamid = config_->get_srt_default_streamid();
        srs_warn("srt get empty streamid, using default streamid %s instead", streamid.c_str());
    }

    // Detect streamid of srt to request.
    SrtMode mode = SrtModePull;
    if (!srs_srt_streamid_to_request(streamid, mode, req_)) {
        return srs_error_new(ERROR_SRT_CONN, "invalid srt streamid=%s", streamid.c_str());
    }

    // discovery vhost, resolve the vhost from config
    SrsConfDirective *parsed_vhost = config_->get_vhost(req_->vhost_);
    if (parsed_vhost) {
        req_->vhost_ = parsed_vhost->arg0();
    }

    bool srt_enabled = config_->get_srt_enabled(req_->vhost_);
    bool edge = config_->get_vhost_is_edge(req_->vhost_);

    if (srt_enabled && edge) {
        srt_enabled = false;
        srs_warn("disable SRT for edge vhost=%s", req_->vhost_.c_str());
    }

    if (!srt_enabled) {
        return srs_error_new(ERROR_SRT_CONN, "srt disabled, vhost=%s", req_->vhost_.c_str());
    }

    srs_trace("@srt, streamid=%s, stream_url=%s, vhost=%s, app=%s, stream=%s, param=%s",
              streamid.c_str(), req_->get_stream_url().c_str(), req_->vhost_.c_str(), req_->app_.c_str(), req_->stream_.c_str(), req_->param_.c_str());

    // Acquire stream publish token to prevent race conditions across all protocols.
    SrsStreamPublishToken *publish_token_raw = NULL;
    if (mode == SrtModePush && (err = stream_publish_tokens_->acquire_token(req_, publish_token_raw)) != srs_success) {
        return srs_error_wrap(err, "acquire stream publish token");
    }
    SrsUniquePtr<SrsStreamPublishToken> publish_token(publish_token_raw);
    if (publish_token.get()) {
        srs_trace("stream publish token acquired, type=srt, url=%s", req_->get_stream_url().c_str());
    }

    if ((err = srt_sources_->fetch_or_create(req_, srt_source_)) != srs_success) {
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
    if ((err = stat_->on_client(_srs_context->get_id().c_str(), req_, this, SrsSrtConnPublish)) != srs_success) {
        return srs_error_wrap(err, "srt: stat client");
    }

    if ((err = security_->check(SrsSrtConnPublish, ip_, req_)) != srs_success) {
        return srs_error_wrap(err, "srt: security check");
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
    if ((err = stat_->on_client(_srs_context->get_id().c_str(), req_, this, SrsSrtConnPlay)) != srs_success) {
        return srs_error_wrap(err, "srt: stat client");
    }

    if ((err = security_->check(SrsSrtConnPlay, ip_, req_)) != srs_success) {
        return srs_error_wrap(err, "srt: security check");
    }

    // We must do hook after stat, because depends on it.
    if ((err = http_hooks_on_play()) != srs_success) {
        return srs_error_wrap(err, "srt: callback on play");
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
    if (!srt_source_->can_publish()) {
        return srs_error_new(ERROR_SRT_SOURCE_BUSY, "srt stream %s busy", req_->get_stream_url().c_str());
    }

    // Check rtmp stream is busy.
    SrsSharedPtr<SrsLiveSource> live_source = live_sources_->fetch(req_);
    if (live_source.get() && !live_source->can_publish(false)) {
        return srs_error_new(ERROR_SYSTEM_STREAM_BUSY, "live_source stream %s busy", req_->get_stream_url().c_str());
    }

    if ((err = live_sources_->fetch_or_create(req_, live_source)) != srs_success) {
        return srs_error_wrap(err, "create source");
    }

    srs_assert(live_source.get() != NULL);

    bool enabled_cache = config_->get_gop_cache(req_->vhost_);
    int gcmf = config_->get_gop_cache_max_frames(req_->vhost_);
    live_source->set_cache(enabled_cache);
    live_source->set_gop_cache_max_frames(gcmf);

    // srt->rtmp->rtc
    // TODO: FIXME: the code below is repeat in srs_app_rtmp_conn.cpp, refactor it later, use function instead.

    // Check whether RTC stream is busy.
    SrsSharedPtr<SrsRtcSource> rtc;
    bool rtc_server_enabled = config_->get_rtc_server_enabled();
    bool rtc_enabled = config_->get_rtc_enabled(req_->vhost_);
    bool edge = config_->get_vhost_is_edge(req_->vhost_);

    if (rtc_enabled && edge) {
        rtc_enabled = false;
        srs_warn("disable WebRTC for edge vhost=%s", req_->vhost_.c_str());
    }

    if (rtc_server_enabled && rtc_enabled) {
        if ((err = rtc_sources_->fetch_or_create(req_, rtc)) != srs_success) {
            return srs_error_wrap(err, "create source");
        }

        if (!rtc->can_publish()) {
            return srs_error_new(ERROR_SYSTEM_STREAM_BUSY, "rtc stream %s busy", req_->get_stream_url().c_str());
        }
    }

    // Bridge to RTMP and RTC streaming.
    SrsSrtBridge *bridge = new SrsSrtBridge(_srs_app_factory);

    bool srt_to_rtmp = config_->get_srt_to_rtmp(req_->vhost_);
    if (srt_to_rtmp && edge) {
        srt_to_rtmp = false;
        srs_warn("disable SRT to RTMP for edge vhost=%s", req_->vhost_.c_str());
    }

    if (srt_to_rtmp) {
        bridge->enable_srt2rtmp(live_source);
    }

    bool rtmp_to_rtc = config_->get_rtc_from_rtmp(req_->vhost_);
    if (rtmp_to_rtc && edge) {
        rtmp_to_rtc = false;
        srs_warn("disable RTMP to WebRTC for edge vhost=%s", req_->vhost_.c_str());
    }

    if (rtc.get() && rtmp_to_rtc) {
        bridge->enable_srt2rtc(rtc);
    }

    if (bridge->empty()) {
        srs_freep(bridge);
    } else if ((err = bridge->initialize(req_)) != srs_success) {
        srs_freep(bridge);
        return srs_error_wrap(err, "bridge init");
    }

    srt_source_->set_bridge(bridge);

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

    srs_trace("SRT: start publish url=%s", req_->get_stream_url().c_str());

    SrsUniquePtr<SrsPithyPrint> pprint(SrsPithyPrint::create_srt_publish());

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
            if ((err = srt_conn_->get_stats(s)) != srs_success) {
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

    ISrsSrtConsumer *consumer_raw = NULL;
    if ((err = srt_source_->create_consumer(consumer_raw)) != srs_success) {
        return srs_error_wrap(err, "create consumer, ts source=%s", req_->get_stream_url().c_str());
    }

    srs_assert(consumer_raw);
    SrsUniquePtr<ISrsSrtConsumer> consumer(consumer_raw);

    // TODO: FIXME: Dumps the SPS/PPS from gop cache, without other frames.
    if ((err = srt_source_->consumer_dumps(consumer.get())) != srs_success) {
        return srs_error_wrap(err, "dumps consumer, url=%s", req_->get_stream_url().c_str());
    }

    SrsUniquePtr<SrsPithyPrint> pprint(SrsPithyPrint::create_srt_play());

    SrsSrtRecvThread srt_recv_trd(srt_conn_);
    if ((err = srt_recv_trd.start()) != srs_success) {
        return srs_error_wrap(err, "start srt recv trd");
    }

    int nb_packets = 0;

    while (true) {
        // Check recv thread error first, so we can detect the client disconnecting event.
        if ((err = srt_recv_trd.get_recv_err()) != srs_success) {
            return srs_error_wrap(err, "srt play recv thread");
        }

        if ((err = trd_->pull()) != srs_success) {
            return srs_error_wrap(err, "srt play thread");
        }

        // Wait for amount of packets.
        SrsSrtPacket *pkt_raw = NULL;
        consumer->dump_packet(&pkt_raw);
        if (!pkt_raw) {
            // TODO: FIXME: We should check the quit event.
            consumer->wait(1, 1000 * SRS_UTIME_MILLISECONDS);
            continue;
        }

        SrsUniquePtr<SrsSrtPacket> pkt(pkt_raw);

        ++nb_packets;

        // reportable
        pprint->elapse();
        if (pprint->can_print()) {
            SrsSrtStat s;
            if ((err = srt_conn_->get_stats(s)) != srs_success) {
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

srs_error_t SrsMpegtsSrtConn::on_srt_packet(char *buf, int nb_buf)
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

    SrsUniquePtr<SrsSrtPacket> packet(new SrsSrtPacket());
    packet->wrap(buf, nb_buf);

    if ((err = srt_source_->on_packet(packet.get())) != srs_success) {
        return srs_error_wrap(err, "on srt packet");
    }

    return err;
}

srs_error_t SrsMpegtsSrtConn::http_hooks_on_connect()
{
    srs_error_t err = srs_success;

    if (!config_->get_vhost_http_hooks_enabled(req_->vhost_)) {
        return err;
    }

    // the http hooks will cause context switch,
    // so we must copy all hooks for the on_connect may freed.
    // @see https://github.com/ossrs/srs/issues/475
    vector<string> hooks;

    if (true) {
        SrsConfDirective *conf = config_->get_vhost_on_connect(req_->vhost_);

        if (!conf) {
            return err;
        }

        hooks = conf->args_;
    }

    for (int i = 0; i < (int)hooks.size(); i++) {
        std::string url = hooks.at(i);
        if ((err = hooks_->on_connect(url, req_)) != srs_success) {
            return srs_error_wrap(err, "srt on_connect %s", url.c_str());
        }
    }

    return err;
}

void SrsMpegtsSrtConn::http_hooks_on_close()
{
    if (!config_->get_vhost_http_hooks_enabled(req_->vhost_)) {
        return;
    }

    // the http hooks will cause context switch,
    // so we must copy all hooks for the on_connect may freed.
    // @see https://github.com/ossrs/srs/issues/475
    vector<string> hooks;

    if (true) {
        SrsConfDirective *conf = config_->get_vhost_on_close(req_->vhost_);

        if (!conf) {
            return;
        }

        hooks = conf->args_;
    }

    for (int i = 0; i < (int)hooks.size(); i++) {
        std::string url = hooks.at(i);
        hooks_->on_close(url, req_, srt_conn_->get_send_bytes(), srt_conn_->get_recv_bytes());
    }
}

srs_error_t SrsMpegtsSrtConn::http_hooks_on_publish()
{
    srs_error_t err = srs_success;

    if (!config_->get_vhost_http_hooks_enabled(req_->vhost_)) {
        return err;
    }

    // the http hooks will cause context switch,
    // so we must copy all hooks for the on_connect may freed.
    // @see https://github.com/ossrs/srs/issues/475
    vector<string> hooks;

    if (true) {
        SrsConfDirective *conf = config_->get_vhost_on_publish(req_->vhost_);

        if (!conf) {
            return err;
        }

        hooks = conf->args_;
    }

    for (int i = 0; i < (int)hooks.size(); i++) {
        std::string url = hooks.at(i);
        if ((err = hooks_->on_publish(url, req_)) != srs_success) {
            return srs_error_wrap(err, "srt on_publish %s", url.c_str());
        }
    }

    return err;
}

void SrsMpegtsSrtConn::http_hooks_on_unpublish()
{
    if (!config_->get_vhost_http_hooks_enabled(req_->vhost_)) {
        return;
    }

    // the http hooks will cause context switch,
    // so we must copy all hooks for the on_connect may freed.
    // @see https://github.com/ossrs/srs/issues/475
    vector<string> hooks;

    if (true) {
        SrsConfDirective *conf = config_->get_vhost_on_unpublish(req_->vhost_);

        if (!conf) {
            return;
        }

        hooks = conf->args_;
    }

    for (int i = 0; i < (int)hooks.size(); i++) {
        std::string url = hooks.at(i);
        hooks_->on_unpublish(url, req_);
    }
}

srs_error_t SrsMpegtsSrtConn::http_hooks_on_play()
{
    srs_error_t err = srs_success;

    if (!config_->get_vhost_http_hooks_enabled(req_->vhost_)) {
        return err;
    }

    // the http hooks will cause context switch,
    // so we must copy all hooks for the on_connect may freed.
    // @see https://github.com/ossrs/srs/issues/475
    vector<string> hooks;

    if (true) {
        SrsConfDirective *conf = config_->get_vhost_on_play(req_->vhost_);

        if (!conf) {
            return err;
        }

        hooks = conf->args_;
    }

    for (int i = 0; i < (int)hooks.size(); i++) {
        std::string url = hooks.at(i);
        if ((err = hooks_->on_play(url, req_)) != srs_success) {
            return srs_error_wrap(err, "srt on_play %s", url.c_str());
        }
    }

    return err;
}

void SrsMpegtsSrtConn::http_hooks_on_stop()
{
    if (!config_->get_vhost_http_hooks_enabled(req_->vhost_)) {
        return;
    }

    // the http hooks will cause context switch,
    // so we must copy all hooks for the on_connect may freed.
    // @see https://github.com/ossrs/srs/issues/475
    vector<string> hooks;

    if (true) {
        SrsConfDirective *conf = config_->get_vhost_on_stop(req_->vhost_);

        if (!conf) {
            return;
        }

        hooks = conf->args_;
    }

    for (int i = 0; i < (int)hooks.size(); i++) {
        std::string url = hooks.at(i);
        hooks_->on_stop(url, req_);
    }

    return;
}
