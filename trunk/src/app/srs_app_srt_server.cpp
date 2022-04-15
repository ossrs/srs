//
// Copyright (c) 2013-2021 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#include <srs_app_srt_server.hpp>

using namespace std;

#include <srs_kernel_log.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_service_log.hpp>
#include <srs_app_config.hpp>
#include <srs_app_srt_conn.hpp>

std::string srs_srt_listener_type2string(SrsSrtListenerType type)
{
    switch (type) {
        case SrsSrtListenerMpegts:
            return "SRT-MPEGTS";
        default:
            return "UNKONWN";
    }
}

SrsSrtAcceptor::SrsSrtAcceptor(SrsSrtServer* srt_server, SrsSrtListenerType t)
{
    port_ = 0;
    srt_server_ = srt_server;
    type_ = t;
}

SrsSrtAcceptor::~SrsSrtAcceptor()
{
}

SrsSrtListenerType SrsSrtAcceptor::listen_type()
{
    return type_;
}

SrsSrtMessageAcceptor::SrsSrtMessageAcceptor(SrsSrtServer* srt_server, SrsSrtListenerType listen_type)
    : SrsSrtAcceptor(srt_server, listen_type)
{
    listener_ = NULL;
}

SrsSrtMessageAcceptor::~SrsSrtMessageAcceptor()
{
    srs_freep(listener_);
}

srs_error_t SrsSrtMessageAcceptor::listen(std::string ip, int port)
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
    
    string v = srs_srt_listener_type2string(type_);
    srs_trace("%s listen at srt://%s:%d, fd=%d", v.c_str(), ip_.c_str(), port_, listener_->fd());
    
    return err;
}

srs_error_t SrsSrtMessageAcceptor::set_srt_opt()
{
    srs_error_t err = srs_success;

    if ((err = srs_srt_set_maxbw(listener_->fd(), _srs_config->get_srto_maxbw())) != srs_success) {
        return srs_error_wrap(err, "set opt");
    }

    if ((err = srs_srt_set_mss(listener_->fd(), _srs_config->get_srto_mss())) != srs_success) {
        return srs_error_wrap(err, "set opt");
    }

    if ((err = srs_srt_set_tsbpdmode(listener_->fd(), _srs_config->get_srto_tsbpdmode())) != srs_success) {
        return srs_error_wrap(err, "set opt");
    }

    if ((err = srs_srt_set_latency(listener_->fd(), _srs_config->get_srto_latency())) != srs_success) {
        return srs_error_wrap(err, "set opt");
    }

    if ((err = srs_srt_set_rcv_latency(listener_->fd(), _srs_config->get_srto_recv_latency())) != srs_success) {
        return srs_error_wrap(err, "set opt");
    }

    if ((err = srs_srt_set_peer_latency(listener_->fd(), _srs_config->get_srto_peer_latency())) != srs_success) {
        return srs_error_wrap(err, "set opt");
    }

    if ((err = srs_srt_set_tlpktdrop(listener_->fd(), _srs_config->get_srto_tlpktdrop())) != srs_success) {
        return srs_error_wrap(err, "set opt");
    }

    if ((err = srs_srt_set_connect_timeout(listener_->fd(), _srs_config->get_srto_conntimeout())) != srs_success) {
        return srs_error_wrap(err, "set opt");
    }

    if ((err = srs_srt_set_peer_idle_timeout(listener_->fd(), _srs_config->get_srto_peeridletimeout())) != srs_success) {
        return srs_error_wrap(err, "set opt");
    }

    if ((err = srs_srt_set_sndbuf(listener_->fd(), _srs_config->get_srto_sendbuf())) != srs_success) {
        return srs_error_wrap(err, "set opt");
    }

    if ((err = srs_srt_set_rcvbuf(listener_->fd(), _srs_config->get_srto_recvbuf())) != srs_success) {
        return srs_error_wrap(err, "set opt");
    }

    if ((err = srs_srt_set_payload_size(listener_->fd(), _srs_config->get_srto_payloadsize())) != srs_success) {
        return srs_error_wrap(err, "set opt");
    }

    return err;
}

srs_error_t SrsSrtMessageAcceptor::on_srt_client(SRTSOCKET srt_fd)
{
    // Notify srt server to accept srt client, and create new SrsSrtConn on it.
    srs_error_t err = srt_server_->accept_srt_client(type_, srt_fd);
    if (err != srs_success) {
        srs_warn("accept srt client failed, err is %s", srs_error_desc(err).c_str());
        srs_freep(err);
    }
    
    return srs_success;
}

SrsSrtServer::SrsSrtServer()
{
    conn_manager_ = new SrsResourceManager("SRT", true);
}

SrsSrtServer::~SrsSrtServer()
{
    srs_freep(conn_manager_);
}

srs_error_t SrsSrtServer::initialize()
{
    srs_error_t err = srs_success;
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
    
    // TODO: FIXME: bad code, refine it.
    std::vector<std::string> ip_ports;
    std::stringstream ss;
    ss << _srs_config->get_srt_listen_port();
    ip_ports.push_back(ss.str());
    
    close_listeners(SrsSrtListenerMpegts);
    
    for (int i = 0; i < (int)ip_ports.size(); i++) {
        SrsSrtAcceptor* acceptor = new SrsSrtMessageAcceptor(this, SrsSrtListenerMpegts);
        acceptors_.push_back(acceptor);

        int port; string ip;
        srs_parse_endpoint(ip_ports[i], ip, port);
        
        if ((err = acceptor->listen(ip, port)) != srs_success) {
            return srs_error_wrap(err, "srt listen %s:%d", ip.c_str(), port);
        }
    }
    
    return err;
}

void SrsSrtServer::close_listeners(SrsSrtListenerType type)
{
    std::vector<SrsSrtAcceptor*>::iterator it;
    for (it = acceptors_.begin(); it != acceptors_.end();) {
        SrsSrtAcceptor* acceptor = *it;
        
        if (acceptor->listen_type() != type) {
            ++it;
            continue;
        }
        
        srs_freep(acceptor);
        it = acceptors_.erase(it);
    }
}

srs_error_t SrsSrtServer::accept_srt_client(SrsSrtListenerType type, SRTSOCKET srt_fd)
{
    srs_error_t err = srs_success;

    ISrsStartableConneciton* conn = NULL;
    
    if ((err = fd_to_resource(type, srt_fd, &conn)) != srs_success) {
        //close fd on conn error, otherwise will lead to fd leak -gs
        srt_close(srt_fd);
        return srs_error_wrap(err, "srt fd to resource");
    }
    srs_assert(conn);
    
    // directly enqueue, the cycle thread will remove the client.
    conn_manager_->add(conn);

    if ((err = conn->start()) != srs_success) {
        return srs_error_wrap(err, "start srt conn coroutine");
    }
    
    return err;
}

srs_error_t SrsSrtServer::fd_to_resource(SrsSrtListenerType type, SRTSOCKET srt_fd, ISrsStartableConneciton** pr)
{
    srs_error_t err = srs_success;
    
    string ip = "";
    int port = 0;

    if ((err = srs_srt_get_remote_ip_port(srt_fd, ip, port)) != srs_success) {
        return srs_error_wrap(err, "get srt ip port");
    }

    srs_trace("accept srt client from %s:%d, fd=%d", ip.c_str(), port, srt_fd);
    
    // TODO: FIXME: need to check max connection?

    // The context id may change during creating the bellow objects.
    SrsContextRestore(_srs_context->get_id());
    
    if (type == SrsSrtListenerMpegts) {
        *pr = new SrsMpegtsSrtConn(this, srt_fd, ip, port);
    } else {
        srs_warn("close for no service handler. srtfd=%d, ip=%s:%d", srt_fd, ip.c_str(), port);
        srt_close(srt_fd);
        return err;
    }
    
    return err;
}

void SrsSrtServer::remove(ISrsResource* c)
{
    // TODO: FIXME: add some statistic of srt.
    // ISrsStartableConneciton* conn = dynamic_cast<ISrsStartableConneciton*>(c);

    // SrsStatistic* stat = SrsStatistic::instance();
    // stat->kbps_add_delta(c->get_id().c_str(), conn);
    // stat->on_disconnect(c->get_id().c_str());

    // use manager to free it async.
    conn_manager_->remove(c);
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

    srt_poller_ = new SrsSrtPoller();

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
       
        if ((err = srt_poller_->wait(0)) != srs_success) {
            srs_error("srt poll wait failed, err=%s", srs_error_desc(err).c_str());
            srs_error_reset(err);
        }

        srs_usleep(10 * SRS_UTIME_MILLISECONDS);
    }
    
    return err;
}
