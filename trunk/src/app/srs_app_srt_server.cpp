//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_app_srt_server.hpp>

using namespace std;

#include <srs_app_config.hpp>
#include <srs_app_rtmp_source.hpp>
#include <srs_app_server.hpp>
#include <srs_app_srt_conn.hpp>
#include <srs_app_statistic.hpp>
#include <srs_kernel_log.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_log.hpp>

SrsSrtEventLoop *_srt_eventloop = NULL;

ISrsSrtClientHandler::ISrsSrtClientHandler()
{
}

ISrsSrtClientHandler::~ISrsSrtClientHandler()
{
}

// LCOV_EXCL_START
SrsSrtAcceptor::SrsSrtAcceptor(ISrsSrtClientHandler *srt_handler)
{
    port_ = 0;
    srt_handler_ = srt_handler;
    listener_ = NULL;
}

SrsSrtAcceptor::~SrsSrtAcceptor()
{
    srs_freep(listener_);
}

srs_error_t SrsSrtAcceptor::listen(std::string ip, int port)
{
    srs_error_t err = srs_success;

    ip_ = ip;
    port_ = port;

    srs_freep(listener_);
    listener_ = new SrsSrtListener(this, ip_, port_);

    // Create srt socket.
    if ((err = listener_->create_socket()) != srs_success) {
        return srs_error_wrap(err, "message srt acceptor");
    }

    // Set all the srt option from config.
    if ((err = set_srt_opt()) != srs_success) {
        return srs_error_wrap(err, "set opt");
    }

    // Start listen srt socket, this function will set the socket in async mode.
    if ((err = listener_->listen()) != srs_success) {
        return srs_error_wrap(err, "message srt acceptor");
    }

    srs_trace("srt listen at udp://%s:%d, fd=%d", ip_.c_str(), port_, listener_->fd());

    return err;
}

srs_error_t SrsSrtAcceptor::set_srt_opt()
{
    srs_error_t err = srs_success;

    if ((err = srs_srt_set_maxbw(listener_->fd(), _srs_config->get_srto_maxbw())) != srs_success) {
        return srs_error_wrap(err, "set opt maxbw=%" PRId64 " failed", _srs_config->get_srto_maxbw());
    }

    if ((err = srs_srt_set_mss(listener_->fd(), _srs_config->get_srto_mss())) != srs_success) {
        return srs_error_wrap(err, "set opt mss=%d failed", _srs_config->get_srto_mss());
    }

    if ((err = srs_srt_set_tsbpdmode(listener_->fd(), _srs_config->get_srto_tsbpdmode())) != srs_success) {
        return srs_error_wrap(err, "set opt tsbpdmode=%d failed", _srs_config->get_srto_tsbpdmode());
    }

    if ((err = srs_srt_set_latency(listener_->fd(), _srs_config->get_srto_latency())) != srs_success) {
        return srs_error_wrap(err, "set opt latency=%d failed", _srs_config->get_srto_latency());
    }

    if ((err = srs_srt_set_rcv_latency(listener_->fd(), _srs_config->get_srto_recv_latency())) != srs_success) {
        return srs_error_wrap(err, "set opt recvlatency=%d failed", _srs_config->get_srto_recv_latency());
    }

    if ((err = srs_srt_set_peer_latency(listener_->fd(), _srs_config->get_srto_peer_latency())) != srs_success) {
        return srs_error_wrap(err, "set opt peerlatency=%d failed", _srs_config->get_srto_peer_latency());
    }

    if ((err = srs_srt_set_tlpktdrop(listener_->fd(), _srs_config->get_srto_tlpktdrop())) != srs_success) {
        return srs_error_wrap(err, "set opt tlpktdrop=%d failed", _srs_config->get_srto_tlpktdrop());
    }

    if ((err = srs_srt_set_connect_timeout(listener_->fd(), srsu2msi(_srs_config->get_srto_conntimeout()))) != srs_success) {
        return srs_error_wrap(err, "set opt connect_timeout=%d failed", _srs_config->get_srto_conntimeout());
    }

    if ((err = srs_srt_set_peer_idle_timeout(listener_->fd(), srsu2msi(_srs_config->get_srto_peeridletimeout()))) != srs_success) {
        return srs_error_wrap(err, "set opt peer_idle_timeout=%d failed", _srs_config->get_srto_peeridletimeout());
    }

    if ((err = srs_srt_set_sndbuf(listener_->fd(), _srs_config->get_srto_sendbuf())) != srs_success) {
        return srs_error_wrap(err, "set opt sendbuf=%d failed", _srs_config->get_srto_sendbuf());
    }

    if ((err = srs_srt_set_rcvbuf(listener_->fd(), _srs_config->get_srto_recvbuf())) != srs_success) {
        return srs_error_wrap(err, "set opt recvbuf=%d failed", _srs_config->get_srto_recvbuf());
    }

    if ((err = srs_srt_set_payload_size(listener_->fd(), _srs_config->get_srto_payloadsize())) != srs_success) {
        return srs_error_wrap(err, "set opt payload_size=%d failed", _srs_config->get_srto_payloadsize());
    }

    string passphrase = _srs_config->get_srto_passphrase();
    if (!passphrase.empty()) {
        if ((err = srs_srt_set_passphrase(listener_->fd(), passphrase)) != srs_success) {
            return srs_error_wrap(err, "set opt passphrase=%s failed", passphrase.c_str());
        }

        int pbkeylen = _srs_config->get_srto_pbkeylen();
        if ((err = srs_srt_set_pbkeylen(listener_->fd(), pbkeylen)) != srs_success) {
            return srs_error_wrap(err, "set opt pbkeylen=%d failed", pbkeylen);
        }
    }

    return err;
}

srs_error_t SrsSrtAcceptor::on_srt_client(srs_srt_t srt_fd)
{
    srs_error_t err = srs_success;

    // Notify srt handler to accept srt client, and create new SrsSrtConn on it.
    if ((err = srt_handler_->accept_srt_client(srt_fd)) != srs_success) {
        srs_warn("accept srt client failed, err is %s", srs_error_desc(err).c_str());
        srs_freep(err);
    }

    return err;
}
// LCOV_EXCL_STOP

SrsSrtEventLoop::SrsSrtEventLoop()
{
    srt_poller_ = NULL;
    trd_ = NULL;
}

// LCOV_EXCL_START
SrsSrtEventLoop::~SrsSrtEventLoop()
{
    srs_freep(trd_);
    srs_freep(srt_poller_);
}
// LCOV_EXCL_STOP

srs_error_t SrsSrtEventLoop::initialize()
{
    srs_error_t err = srs_success;

    srt_poller_ = srs_srt_poller_new();

    if ((err = srt_poller_->initialize()) != srs_success) {
        return srs_error_wrap(err, "srt poller initialize");
    }

    return err;
}

srs_error_t SrsSrtEventLoop::start()
{
    srs_error_t err = srs_success;

    trd_ = new SrsSTCoroutine("srt_listener", this);
    if ((err = trd_->start()) != srs_success) {
        return srs_error_wrap(err, "start coroutine");
    }

    return err;
}

srs_error_t SrsSrtEventLoop::cycle()
{
    srs_error_t err = srs_success;

    while (true) {
        if ((err = trd_->pull()) != srs_success) {
            return srs_error_wrap(err, "srt listener");
        }

        // LCOV_EXCL_START
        // Check and notify fired SRT events by epoll.
        //
        // Note that the SRT poller use a dedicated and isolated epoll, which is not the same as the one of SRS, in
        // short, the wait won't switch to other coroutines when no fd is active, so we must use timeout(0) to make sure
        // to return directly, then use srs_usleep to do the coroutine switch.
        int n_fds = 0;
        if ((err = srt_poller_->wait(0, &n_fds)) != srs_success) {
            srs_warn("srt poll wait failed, n_fds=%d, err=%s", n_fds, srs_error_desc(err).c_str());
            srs_freep(err);
        }
        // LCOV_EXCL_STOP

        // We use sleep to switch to other coroutines, because the SRT poller is not possible to do this.
        srs_usleep((n_fds ? 1 : 10) * SRS_UTIME_MILLISECONDS);
    }

    return err;
}
