//
// Copyright (c) 2013-2026 The SRS Authors
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
    srt_skt_ = NULL;

    srt_eventloop_ = _srs_srt_eventloop;
}

void SrsSrtConnection::assemble()
{
    srt_skt_ = new SrsSrtSocket(srt_eventloop_->poller(), srt_fd_);
}

SrsSrtConnection::~SrsSrtConnection()
{
    srs_freep(srt_skt_);

    srt_eventloop_ = NULL;
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
    trd_ = NULL;
    recv_err_ = srs_success;

    app_factory_ = _srs_app_factory;
    context_ = _srs_context;
}

void SrsSrtRecvThread::assemble()
{
    trd_ = app_factory_->create_coroutine("srt-recv", this, context_->get_id());
}

SrsSrtRecvThread::~SrsSrtRecvThread()
{
    srs_freep(trd_);
    srs_freep(recv_err_);

    app_factory_ = NULL;
    context_ = NULL;
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

            // A player sends nothing, so its read times out after each idle period; ignore it.
            srs_freep(err);
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
    resource_manager_ = resource_manager;

    srt_conn_ = new SrsSrtConnection(srt_fd);
    ip_ = ip;
    port_ = port;

    kbps_ = new SrsNetworkKbps();
    delta_ = new SrsNetworkDelta();

    trd_ = NULL;

    req_ = new SrsRequest();
    req_->ip_ = ip;

    security_ = new SrsEnhancedSecurity();

    app_factory_ = _srs_app_factory;
    context_ = _srs_context;
    stat_ = _srs_stat;
    config_ = _srs_config;
    stream_publish_tokens_ = _srs_stream_publish_tokens;
    srt_sources_ = _srs_srt_sources;
    live_sources_ = _srs_sources;
    rtc_sources_ = _srs_rtc_sources;
    hooks_ = _srs_hooks;
}

void SrsMpegtsSrtConn::assemble()
{
    srt_conn_->assemble();

    context_->set_id(context_->generate_id());

    kbps_->set_io(srt_conn_, srt_conn_);
    delta_->set_io(srt_conn_, srt_conn_);

    trd_ = app_factory_->create_coroutine("ts-srt", this, context_->get_id());
}

SrsMpegtsSrtConn::~SrsMpegtsSrtConn()
{
    srs_freep(trd_);

    srs_freep(kbps_);
    srs_freep(delta_);
    srs_freep(srt_conn_);
    srs_freep(req_);
    srs_freep(security_);

    app_factory_ = NULL;
    context_ = NULL;
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

    stat_->kbps_add_delta(get_id().c_str(), delta_);
    stat_->on_disconnect(get_id().c_str(), err);

    resource_manager_->remove(this);

    if (err == srs_success) {
        srs_trace("srt client finished.");
        return err;
    }

    if (srs_error_code(err) == ERROR_SUCCESS) {
        srs_trace("srt client finished%s.", srs_error_summary(err).c_str());
        srs_freep(err);
        return err;
    }

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

    if (streamid.empty()) {
        streamid = config_->get_srt_default_streamid();
        srs_warn("srt get empty streamid, using default streamid %s instead", streamid.c_str());
    }

    SrtMode mode = SrtModePull;
    if (!srs_srt_streamid_to_request(config_, streamid, mode, req_)) {
        return srs_error_new(ERROR_SRT_CONN, "invalid srt streamid=%s", streamid.c_str());
    }

    // A bonded caller may only publish; a socket group is never a player.
    if (mode != SrtModePush && srs_srt_is_group(srt_conn_->srtfd())) {
        return srs_error_new(ERROR_SRT_CONN, "srt bonding is publish only, refuse group to play, streamid=%s", streamid.c_str());
    }

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

    // The token prevents races between publishers of all protocols.
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

    // The hooks depend on the stat of the client.
    if ((err = stat_->on_client(context_->get_id().c_str(), req_, this, SrsSrtConnPublish)) != srs_success) {
        return srs_error_wrap(err, "srt: stat client");
    }

    if ((err = security_->check(SrsSrtConnPublish, ip_, req_)) != srs_success) {
        return srs_error_wrap(err, "srt: security check");
    }

    if ((err = http_hooks_on_publish()) != srs_success) {
        return srs_error_wrap(err, "srt: callback on publish");
    }

    srs_error_t acquire_err = acquire_publish();
    if ((err = acquire_err) == srs_success) {
        err = do_publishing();
    }

    // A busy stream is owned by another session, which must keep it.
    int acquire_code = srs_error_code(acquire_err);
    if (acquire_code != ERROR_SRT_SOURCE_BUSY && acquire_code != ERROR_SYSTEM_STREAM_BUSY) {
        release_publish();
    }

    http_hooks_on_unpublish();

    return err;
}

srs_error_t SrsMpegtsSrtConn::playing()
{
    srs_error_t err = srs_success;

    // The hooks depend on the stat of the client.
    if ((err = stat_->on_client(context_->get_id().c_str(), req_, this, SrsSrtConnPlay)) != srs_success) {
        return srs_error_wrap(err, "srt: stat client");
    }

    if ((err = security_->check(SrsSrtConnPlay, ip_, req_)) != srs_success) {
        return srs_error_wrap(err, "srt: security check");
    }

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

    if (!srt_source_->can_publish()) {
        return srs_error_new(ERROR_SRT_SOURCE_BUSY, "srt stream %s busy", req_->get_stream_url().c_str());
    }

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

    // TODO: FIXME: the code below is repeat in srs_app_rtmp_conn.cpp, refactor it later, use function instead.
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

    SrsSrtBridge *bridge = new SrsSrtBridge(app_factory_);
    bridge->assemble();

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

    // @see https://github.com/ossrs/srs/issues/4600
    srs_utime_t timeout = config_->get_srto_peeridletimeout();
    srt_conn_->set_recv_timeout(timeout);
    srt_conn_->set_send_timeout(timeout);

    SrsUniquePtr<SrsPithyPrint> pprint(SrsPithyPrint::create_srt_publish());

    // A bonded encoder publishes over a socket group, whose links are logged as it starts and at each pithy print.
    bool is_group = srs_srt_is_group(srt_conn_->srtfd());
    if (is_group) {
        update_group_links();
    }

    int nb_packets = 0;

    // The max UDP packet size is 1500.
    char buf[1500];
    while (true) {
        if ((err = trd_->pull()) != srs_success) {
            return srs_error_wrap(err, "srt: thread quit");
        }

        pprint->elapse();
        if (pprint->can_print()) {
            SrsSrtStat s;
            if (is_group) {
                update_group_links();
                print_group_stats();
            } else if ((err = srt_conn_->get_stats(s)) != srs_success) {
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

    // @see https://github.com/ossrs/srs/issues/4600
    srs_utime_t timeout = config_->get_srto_peeridletimeout();
    srt_conn_->set_recv_timeout(timeout);
    srt_conn_->set_send_timeout(timeout);

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
    srt_recv_trd.assemble();
    if ((err = srt_recv_trd.start()) != srs_success) {
        return srs_error_wrap(err, "start srt recv trd");
    }

    int nb_packets = 0;

    while (true) {
        // The recv thread is how a player's disconnect is detected.
        if ((err = srt_recv_trd.get_recv_err()) != srs_success) {
            return srs_error_wrap(err, "srt play recv thread");
        }

        if ((err = trd_->pull()) != srs_success) {
            return srs_error_wrap(err, "srt play thread");
        }

        SrsSrtPacket *pkt_raw = NULL;
        consumer->dump_packet(&pkt_raw);
        if (!pkt_raw) {
            // enqueue() signals the cond, so the wait ends as soon as a packet arrives.
            // @see https://github.com/ossrs/srs/issues/4591
            bool has_msgs = consumer->wait(1, timeout);
            if (!has_msgs && srt_source_->can_publish()) {
                return srs_error_new(ERROR_SRT_SOURCE_DISCONNECTED, "srt source disconnected");
            }
            continue;
        }

        SrsUniquePtr<SrsSrtPacket> pkt(pkt_raw);

        ++nb_packets;

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

        // @see https://github.com/ossrs/srs/issues/2194#issuecomment-777542162
        // TODO: FIXME: Please check whether SRT sendmsg causing clock deviation, see srs_thread_yield of SrsUdpMuxSocket::sendto
    }

    return err;
}

srs_error_t SrsMpegtsSrtConn::on_srt_packet(char *buf, int nb_buf)
{
    srs_error_t err = srs_success;

    if (nb_buf <= 0) {
        return err;
    }

    if ((nb_buf % SRS_TS_PACKET_SIZE) != 0) {
        return srs_error_new(ERROR_SRT_CONN, "invalid ts packet len=%d", nb_buf);
    }

    // 0x47 is the TS sync byte.
    if (buf[0] != 0x47) {
        return srs_error_new(ERROR_SRT_CONN, "invalid ts packet first=%#x", (uint8_t)buf[0]);
    }

    SrsUniquePtr<SrsSrtPacket> packet(new SrsSrtPacket());
    packet->wrap(buf, nb_buf);

    if ((err = srt_source_->on_srt_packet(packet.get())) != srs_success) {
        return srs_error_wrap(err, "on srt packet");
    }

    return err;
}

static bool srs_srt_has_link(const vector<SrsSrtGroupLink> &links, srs_srt_t id)
{
    for (int i = 0; i < (int)links.size(); i++) {
        const SrsSrtGroupLink &link = links[i];
        if (link.id_ == id) {
            return true;
        }
    }
    return false;
}

void SrsMpegtsSrtConn::update_group_links()
{
    srs_srt_t group = srt_conn_->srtfd();

    vector<SrsSrtGroupLink> links;
    srs_error_t err = srs_srt_group_links(group, links);
    if (err != srs_success) {
        srs_warn("srt group=%d links, %s", group, srs_error_desc(err).c_str());
        srs_freep(err);
        return;
    }

    for (int i = 0; i < (int)group_links_.size(); i++) {
        SrsSrtGroupLink &link = group_links_[i];
        if (!srs_srt_has_link(links, link.id_)) {
            srs_trace("SRT: group=%d link=%d left, peer=%s:%d, links=%d", group, link.id_, link.ip_.c_str(), link.port_, (int)links.size());
        }
    }
    for (int i = 0; i < (int)links.size(); i++) {
        SrsSrtGroupLink &link = links[i];
        if (!srs_srt_has_link(group_links_, link.id_)) {
            srs_trace("SRT: group=%d link=%d joined, peer=%s:%d, links=%d", group, link.id_, link.ip_.c_str(), link.port_, (int)links.size());
        }
    }

    group_links_ = links;
}

void SrsMpegtsSrtConn::print_group_stats()
{
    srs_error_t err = srs_success;

    SrsSrtStat s;
    if ((err = srt_conn_->get_stats(s)) != srs_success) {
        srs_freep(err);
    } else {
        srs_trace("<- " SRS_CONSTS_LOG_SRT_PUBLISH " Group Stats # links=%d, pktRecvUnique=%" PRId64 ", pktRcvDrop=%d",
                  (int)group_links_.size(), s.pktRecvUnique(), s.pktRcvDrop());
    }

    srs_srt_t group = srt_conn_->srtfd();
    for (int i = 0; i < (int)group_links_.size(); i++) {
        SrsSrtGroupLink &link = group_links_[i];

        // A link that went since the update has no stats, only its state.
        SrsSrtStat ls;
        if ((err = ls.fetch(link.id_, true)) != srs_success) {
            srs_freep(err);
            srs_trace("<- " SRS_CONSTS_LOG_SRT_PUBLISH " Link Stats # group=%d, link=%d, peer=%s:%d, state=%s, weight=%d",
                      group, link.id_, link.ip_.c_str(), link.port_, link.state_.c_str(), link.weight_);
            continue;
        }
        srs_trace("<- " SRS_CONSTS_LOG_SRT_PUBLISH " Link Stats # group=%d, link=%d, peer=%s:%d, state=%s, weight=%d, pktRecv=%" PRId64 ", pktRcvLoss=%d, pktRcvRetrans=%d, pktRcvDrop=%d, rtt=%.1fms",
                  group, link.id_, link.ip_.c_str(), link.port_, link.state_.c_str(), link.weight_,
                  ls.pktRecv(), ls.pktRcvLoss(), ls.pktRcvRetrans(), ls.pktRcvDrop(), ls.msRTT());
    }
}

srs_error_t SrsMpegtsSrtConn::http_hooks_on_connect()
{
    srs_error_t err = srs_success;

    if (!config_->get_vhost_http_hooks_enabled(req_->vhost_)) {
        return err;
    }

    // The hooks switch coroutines, during which the directive may be freed, so copy its URLs first.
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

    // The hooks switch coroutines, during which the directive may be freed, so copy its URLs first.
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

    // The hooks switch coroutines, during which the directive may be freed, so copy its URLs first.
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
        if ((err = hooks_->on_publish(url, req_, SrsSrtConnPublish)) != srs_success) {
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

    // The hooks switch coroutines, during which the directive may be freed, so copy its URLs first.
    // @see https://github.com/ossrs/srs/issues/475
    vector<string> hooks;

    if (true) {
        SrsConfDirective *conf = config_->get_vhost_on_unpublish(req_->vhost_);

        if (!conf) {
            return;
        }

        hooks = conf->args_;
    }

    SrsStatisticStreamSnapshot stream(stat_, req_);
    for (int i = 0; i < (int)hooks.size(); i++) {
        std::string url = hooks.at(i);
        hooks_->on_unpublish(url, req_, SrsSrtConnPublish, stream);
    }
}

srs_error_t SrsMpegtsSrtConn::http_hooks_on_play()
{
    srs_error_t err = srs_success;

    if (!config_->get_vhost_http_hooks_enabled(req_->vhost_)) {
        return err;
    }

    // The hooks switch coroutines, during which the directive may be freed, so copy its URLs first.
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
        if ((err = hooks_->on_play(url, req_, SrsSrtConnPlay)) != srs_success) {
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

    // The hooks switch coroutines, during which the directive may be freed, so copy its URLs first.
    // @see https://github.com/ossrs/srs/issues/475
    vector<string> hooks;

    if (true) {
        SrsConfDirective *conf = config_->get_vhost_on_stop(req_->vhost_);

        if (!conf) {
            return;
        }

        hooks = conf->args_;
    }

    SrsStatisticStreamSnapshot stream(stat_, req_);
    for (int i = 0; i < (int)hooks.size(); i++) {
        std::string url = hooks.at(i);
        hooks_->on_stop(url, req_, SrsSrtConnPlay, stream);
    }

    return;
}
