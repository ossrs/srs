//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#include <srs_app_srt_server.hpp>

using namespace std;

#include <srs_kernel_log.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_log.hpp>
#include <srs_app_config.hpp>
#include <srs_app_srt_conn.hpp>
#include <srs_app_statistic.hpp>

#ifdef SRS_SRT
SrsSrtEventLoop* _srt_eventloop = NULL;
#endif

SrsSrtAcceptor::SrsSrtAcceptor(SrsSrtServer* srt_server)
{
    port_ = 0;
    srt_server_ = srt_server;
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
    if (! passphrase.empty()) {
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

    // Notify srt server to accept srt client, and create new SrsSrtConn on it.
    if ((err = srt_server_->accept_srt_client(srt_fd)) != srs_success) {
        srs_warn("accept srt client failed, err is %s", srs_error_desc(err).c_str());
        srs_freep(err);
    }
    
    return err;
}

SrsSrtServer::SrsSrtServer()
{
    conn_manager_ = new SrsResourceManager("SRT", true);
    timer_ = NULL;
}

SrsSrtServer::~SrsSrtServer()
{
    srs_freep(conn_manager_);
    srs_freep(timer_);
}

srs_error_t SrsSrtServer::initialize()
{
    srs_error_t err = srs_success;

    if (! _srs_config->get_srt_enabled()) {
        return err;
    }

    if ((err = setup_ticks()) != srs_success) {
        return srs_error_wrap(err, "tick");
    }

    return err;
}

srs_error_t SrsSrtServer::listen()
{
    srs_error_t err = srs_success;
    
    // Listen mpegts over srt.
    if ((err = listen_srt_mpegts()) != srs_success) {
        return srs_error_wrap(err, "srt mpegts listen");
    }

    if ((err = conn_manager_->start()) != srs_success) {
        return srs_error_wrap(err, "srt connection manager");
    }

    return err;
}

srs_error_t SrsSrtServer::listen_srt_mpegts()
{
    srs_error_t err = srs_success;

    if (! _srs_config->get_srt_enabled()) {
        return err;
    }

    // Close all listener for SRT if exists.
    close_listeners();

    // Start a listener for SRT, we might need multiple listeners in the future.
    SrsSrtAcceptor* acceptor = new SrsSrtAcceptor(this);
    acceptors_.push_back(acceptor);

    int port; string ip;
    srs_parse_endpoint(srs_int2str(_srs_config->get_srt_listen_port()), ip, port);

    if ((err = acceptor->listen(ip, port)) != srs_success) {
        return srs_error_wrap(err, "srt listen %s:%d", ip.c_str(), port);
    }

    return err;
}

void SrsSrtServer::close_listeners()
{
    std::vector<SrsSrtAcceptor*>::iterator it;
    for (it = acceptors_.begin(); it != acceptors_.end();) {
        SrsSrtAcceptor* acceptor = *it;
        srs_freep(acceptor);

        it = acceptors_.erase(it);
    }
}

srs_error_t SrsSrtServer::accept_srt_client(srs_srt_t srt_fd)
{
    srs_error_t err = srs_success;

    ISrsResource* resource = NULL;
    if ((err = fd_to_resource(srt_fd, &resource)) != srs_success) {
        //close fd on conn error, otherwise will lead to fd leak -gs
        // TODO: FIXME: Handle error.
        srs_srt_close(srt_fd);
        return srs_error_wrap(err, "srt fd to resource");
    }
    srs_assert(resource);
    
    // directly enqueue, the cycle thread will remove the client.
    conn_manager_->add(resource);

    ISrsStartable* conn = dynamic_cast<ISrsStartable*>(resource);
    if ((err = conn->start()) != srs_success) {
        return srs_error_wrap(err, "start srt conn coroutine");
    }
    
    return err;
}

srs_error_t SrsSrtServer::fd_to_resource(srs_srt_t srt_fd, ISrsResource** pr)
{
    srs_error_t err = srs_success;
    
    string ip = "";
    int port = 0;
    if ((err = srs_srt_get_remote_ip_port(srt_fd, ip, port)) != srs_success) {
        return srs_error_wrap(err, "get srt ip port");
    }

    // TODO: FIXME: need to check max connection?

    // The context id may change during creating the bellow objects.
    SrsContextRestore(_srs_context->get_id());

    // Covert to SRT conection.
    *pr = new SrsMpegtsSrtConn(this, srt_fd, ip, port);
    
    return err;
}

void SrsSrtServer::remove(ISrsResource* c)
{
    // use manager to free it async.
    conn_manager_->remove(c);
}

srs_error_t SrsSrtServer::setup_ticks()
{
    srs_error_t err = srs_success;

    srs_freep(timer_);
    timer_ = new SrsHourGlass("srt", this, 1 * SRS_UTIME_SECONDS);

    if (_srs_config->get_stats_enabled()) {
        if ((err = timer_->tick(8, 3 * SRS_UTIME_SECONDS)) != srs_success) {
            return srs_error_wrap(err, "tick");
        }
    }

    if ((err = timer_->start()) != srs_success) {
        return srs_error_wrap(err, "timer");
    }

    return err;
}

srs_error_t SrsSrtServer::notify(int event, srs_utime_t interval, srs_utime_t tick)
{
    srs_error_t err = srs_success;

    switch (event) {
        case 8: resample_kbps(); break;
    }

    return err;
}

void SrsSrtServer::resample_kbps()
{
    // collect delta from all clients.
    for (int i = 0; i < (int)conn_manager_->size(); i++) {
        ISrsResource* c = conn_manager_->at(i);

        SrsMpegtsSrtConn* conn = dynamic_cast<SrsMpegtsSrtConn*>(c);
        srs_assert(conn);

        // add delta of connection to server kbps.,
        // for next sample() of server kbps can get the stat.
        SrsStatistic::instance()->kbps_add_delta(c->get_id().c_str(), conn->delta());
    }
}

SrsSrtServerAdapter::SrsSrtServerAdapter()
{
    srt_server_ = new SrsSrtServer();
}

SrsSrtServerAdapter::~SrsSrtServerAdapter()
{
    srs_freep(srt_server_);
}

srs_error_t SrsSrtServerAdapter::initialize()
{
    srs_error_t err = srs_success;

    if ((err = srs_srt_log_initialize()) != srs_success) {
        return srs_error_wrap(err, "srt log initialize");
    }

    _srt_eventloop = new SrsSrtEventLoop();

    if ((err = _srt_eventloop->initialize()) != srs_success) {
        return srs_error_wrap(err, "srt poller initialize");
    }

    if ((err = _srt_eventloop->start()) != srs_success) {
        return srs_error_wrap(err, "srt poller start");
    }

    return err;
}

srs_error_t SrsSrtServerAdapter::run(SrsWaitGroup* wg)
{
    srs_error_t err = srs_success;

    // Initialize the whole system, set hooks to handle server level events.
    if ((err = srt_server_->initialize()) != srs_success) {
        return srs_error_wrap(err, "srt server initialize");
    }

    if ((err = srt_server_->listen()) != srs_success) {
        return srs_error_wrap(err, "srt listen");
    }

    return err;
}

void SrsSrtServerAdapter::stop()
{
}

SrsSrtServer* SrsSrtServerAdapter::instance()
{
    return srt_server_;
}

SrsSrtEventLoop::SrsSrtEventLoop()
{
    srt_poller_ = NULL;
    trd_ = NULL;
}

SrsSrtEventLoop::~SrsSrtEventLoop()
{
    srs_freep(trd_);
    srs_freep(srt_poller_);
}

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
       
        // Check and notify fired SRT events by epoll.
        //
        // Note that the SRT poller use a dedicated and isolated epoll, which is not the same as the one of SRS, in
        // short, the wait won't switch to other coroutines when no fd is active, so we must use timeout(0) to make sure
        // to return directly, then use srs_usleep to do the coroutine switch.
        int n_fds = 0;
        if ((err = srt_poller_->wait(0, &n_fds)) != srs_success) {
            srs_warn("srt poll wait failed, n_fds=%d, err=%s", n_fds, srs_error_desc(err).c_str());
            srs_error_reset(err);
        }

        // We use sleep to switch to other coroutines, because the SRT poller is not possible to do this.
        srs_usleep((n_fds ? 1 : 10) * SRS_UTIME_MILLISECONDS);
    }
    
    return err;
}

