//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_app_srt_server.hpp>

using namespace std;

#include <srs_app_config.hpp>
#include <srs_app_factory.hpp>
#include <srs_app_rtmp_source.hpp>
#include <srs_app_server.hpp>
#include <srs_app_srt_conn.hpp>
#include <srs_app_statistic.hpp>
#include <srs_kernel_log.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_log.hpp>

SrsSrtEventLoop *_srs_srt_eventloop = NULL;

ISrsSrtClientHandler::ISrsSrtClientHandler()
{
}

ISrsSrtClientHandler::~ISrsSrtClientHandler()
{
}

#define SRS_SRT_GROUP_MAX_LINKS 8

SrsSrtGroups::SrsSrtGroups()
{
}

SrsSrtGroups::~SrsSrtGroups()
{
}

void SrsSrtGroups::add(srs_srt_t group)
{
    if (!srs_srt_is_group(group)) {
        return;
    }

    // A connection closes its group when it ends and tells nobody, so the groups that ended are dropped here too.
    limit_links();
    groups_.push_back(group);
}

void SrsSrtGroups::limit_links()
{
    for (vector<srs_srt_t>::iterator it = groups_.begin(); it != groups_.end();) {
        srs_srt_t group = *it;

        vector<SrsSrtGroupLink> links;
        srs_error_t err = srs_srt_group_links(group, links);
        if (err != srs_success) {
            srs_freep(err);
            it = groups_.erase(it);
            continue;
        }

        for (int i = SRS_SRT_GROUP_MAX_LINKS; i < (int)links.size(); i++) {
            SrsSrtGroupLink &link = links[i];
            srs_warn("srt group=%d has %d links, close link=%d over the most of %d", group, (int)links.size(), link.id_, SRS_SRT_GROUP_MAX_LINKS);
            if ((err = srs_srt_close(link.id_)) != srs_success) {
                srs_freep(err);
            }
        }
        ++it;
    }
}

int SrsSrtGroups::size()
{
    return (int)groups_.size();
}

ISrsSrtAcceptor::ISrsSrtAcceptor()
{
}

ISrsSrtAcceptor::~ISrsSrtAcceptor()
{
}

SrsSrtAcceptor::SrsSrtAcceptor(ISrsSrtClientHandler *srt_handler)
{
    port_ = 0;
    srt_handler_ = srt_handler;
    listener_ = NULL;

    config_ = _srs_config;
    app_factory_ = _srs_app_factory;
    srt_options_ = new SrsSrtOptions();
}

SrsSrtAcceptor::~SrsSrtAcceptor()
{
    srs_freep(listener_);
    srs_freep(srt_options_);

    config_ = NULL;
    app_factory_ = NULL;
}

srs_error_t SrsSrtAcceptor::listen(std::string ip, int port)
{
    srs_error_t err = srs_success;

    ip_ = ip;
    port_ = port;

    srs_freep(listener_);
    listener_ = app_factory_->create_srt_listener(this, ip_, port_);

    if ((err = listener_->create_socket()) != srs_success) {
        return srs_error_wrap(err, "message srt acceptor");
    }

    if ((err = set_srt_opt()) != srs_success) {
        return srs_error_wrap(err, "set opt");
    }

    if ((err = listener_->listen()) != srs_success) {
        return srs_error_wrap(err, "message srt acceptor");
    }

    srs_trace("srt listen at udp://%s:%d, fd=%d", ip_.c_str(), port_, listener_->fd());

    return err;
}

srs_error_t SrsSrtAcceptor::set_srt_opt()
{
    srs_error_t err = srs_success;

    if ((err = srt_options_->set_maxbw(listener_->fd(), config_->get_srto_maxbw())) != srs_success) {
        return srs_error_wrap(err, "set opt maxbw=%" PRId64 " failed", config_->get_srto_maxbw());
    }

    if ((err = srt_options_->set_mss(listener_->fd(), config_->get_srto_mss())) != srs_success) {
        return srs_error_wrap(err, "set opt mss=%d failed", config_->get_srto_mss());
    }

    if ((err = srt_options_->set_tsbpdmode(listener_->fd(), config_->get_srto_tsbpdmode())) != srs_success) {
        return srs_error_wrap(err, "set opt tsbpdmode=%d failed", config_->get_srto_tsbpdmode());
    }

    // Set only the latencies the user configured, as ffmpeg does, so a default never undoes the latency, which sets
    // both the recv and peer latency. The latency goes first, so the recv or peer latency set itself wins.
    int latency = config_->get_srto_latency();
    if (latency >= 0 && (err = srt_options_->set_latency(listener_->fd(), latency)) != srs_success) {
        return srs_error_wrap(err, "set opt latency=%d failed", latency);
    }

    int recv_latency = config_->get_srto_recv_latency();
    if (recv_latency >= 0 && (err = srt_options_->set_rcv_latency(listener_->fd(), recv_latency)) != srs_success) {
        return srs_error_wrap(err, "set opt recvlatency=%d failed", recv_latency);
    }

    int peer_latency = config_->get_srto_peer_latency();
    if (peer_latency >= 0 && (err = srt_options_->set_peer_latency(listener_->fd(), peer_latency)) != srs_success) {
        return srs_error_wrap(err, "set opt peerlatency=%d failed", peer_latency);
    }

    if ((err = srt_options_->set_tlpktdrop(listener_->fd(), config_->get_srto_tlpktdrop())) != srs_success) {
        return srs_error_wrap(err, "set opt tlpktdrop=%d failed", config_->get_srto_tlpktdrop());
    }

    if ((err = srt_options_->set_connect_timeout(listener_->fd(), srsu2msi(config_->get_srto_conntimeout()))) != srs_success) {
        return srs_error_wrap(err, "set opt connect_timeout=%d failed", config_->get_srto_conntimeout());
    }

    if ((err = srt_options_->set_peer_idle_timeout(listener_->fd(), srsu2msi(config_->get_srto_peeridletimeout()))) != srs_success) {
        return srs_error_wrap(err, "set opt peer_idle_timeout=%d failed", config_->get_srto_peeridletimeout());
    }

    if ((err = srt_options_->set_sndbuf(listener_->fd(), config_->get_srto_sendbuf())) != srs_success) {
        return srs_error_wrap(err, "set opt sendbuf=%d failed", config_->get_srto_sendbuf());
    }

    if ((err = srt_options_->set_rcvbuf(listener_->fd(), config_->get_srto_recvbuf())) != srs_success) {
        return srs_error_wrap(err, "set opt recvbuf=%d failed", config_->get_srto_recvbuf());
    }

    if ((err = srt_options_->set_payload_size(listener_->fd(), config_->get_srto_payloadsize())) != srs_success) {
        return srs_error_wrap(err, "set opt payload_size=%d failed", config_->get_srto_payloadsize());
    }

    string passphrase = config_->get_srto_passphrase();
    if (!passphrase.empty()) {
        if ((err = srt_options_->set_passphrase(listener_->fd(), passphrase)) != srs_success) {
            return srs_error_wrap(err, "set opt passphrase=%s failed", passphrase.c_str());
        }

        int pbkeylen = config_->get_srto_pbkeylen();
        if ((err = srt_options_->set_pbkeylen(listener_->fd(), pbkeylen)) != srs_success) {
            return srs_error_wrap(err, "set opt pbkeylen=%d failed", pbkeylen);
        }
    }

    if (config_->get_srt_bonding() && (err = srt_options_->set_group_connect(listener_->fd(), true)) != srs_success) {
        return srs_error_wrap(err, "srt_server.bonding on needs a libsrt built with bonding");
    }

    return err;
}

srs_error_t SrsSrtAcceptor::on_srt_client(srs_srt_t srt_fd, std::string ip, int port)
{
    srs_error_t err = srs_success;

    if ((err = srt_handler_->accept_srt_client(srt_fd, ip, port)) != srs_success) {
        srs_warn("accept srt client failed, err is %s", srs_error_desc(err).c_str());
        srs_freep(err);
    }

    return err;
}

void SrsSrtAcceptor::on_srt_links_joined()
{
    srt_handler_->on_srt_links_joined();
}

ISrsSrtEventLoop::ISrsSrtEventLoop()
{
}

ISrsSrtEventLoop::~ISrsSrtEventLoop()
{
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

    if ((err = srs_srt_log_initialize()) != srs_success) {
        return srs_error_wrap(err, "srt log initialize");
    }

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

ISrsSrtPoller *SrsSrtEventLoop::poller()
{
    return srt_poller_;
}

srs_error_t SrsSrtEventLoop::cycle()
{
    srs_error_t err = srs_success;

    while (true) {
        if ((err = trd_->pull()) != srs_success) {
            return srs_error_wrap(err, "srt listener");
        }

        // The SRT poller has its own epoll, not the one of state-threads, so its wait never switches to other
        // coroutines. Wait with timeout 0, then sleep to switch.
        int n_fds = 0;
        if ((err = srt_poller_->wait(0, &n_fds)) != srs_success) {
            srs_warn("srt poll wait failed, n_fds=%d, err=%s", n_fds, srs_error_desc(err).c_str());
            srs_freep(err);
        }

        srs_usleep((n_fds ? 1 : 10) * SRS_UTIME_MILLISECONDS);
    }

    return err;
}
