/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2020 Winlin
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <srs_app_hybrid.hpp>

#include <srs_app_server.hpp>
#include <srs_app_config.hpp>
#include <srs_kernel_error.hpp>
#include <srs_service_st.hpp>
#include <srs_app_utility.hpp>

using namespace std;

extern SrsPps* _srs_pps_cids_get;
extern SrsPps* _srs_pps_cids_set;

extern SrsPps* _srs_pps_timer;
extern SrsPps* _srs_pps_pub;
extern SrsPps* _srs_pps_conn;
extern SrsPps* _srs_pps_dispose;

extern unsigned long long _st_stat_recvfrom;
extern unsigned long long _st_stat_recvfrom_eagain;
extern unsigned long long _st_stat_sendto;
extern unsigned long long _st_stat_sendto_eagain;
SrsPps* _srs_pps_recvfrom = new SrsPps(_srs_clock);
SrsPps* _srs_pps_recvfrom_eagain = new SrsPps(_srs_clock);
SrsPps* _srs_pps_sendto = new SrsPps(_srs_clock);
SrsPps* _srs_pps_sendto_eagain = new SrsPps(_srs_clock);

extern unsigned long long _st_stat_read;
extern unsigned long long _st_stat_read_eagain;
extern unsigned long long _st_stat_readv;
extern unsigned long long _st_stat_readv_eagain;
extern unsigned long long _st_stat_writev;
extern unsigned long long _st_stat_writev_eagain;
SrsPps* _srs_pps_read = new SrsPps(_srs_clock);
SrsPps* _srs_pps_read_eagain = new SrsPps(_srs_clock);
SrsPps* _srs_pps_readv = new SrsPps(_srs_clock);
SrsPps* _srs_pps_readv_eagain = new SrsPps(_srs_clock);
SrsPps* _srs_pps_writev = new SrsPps(_srs_clock);
SrsPps* _srs_pps_writev_eagain = new SrsPps(_srs_clock);

extern unsigned long long _st_stat_recvmsg;
extern unsigned long long _st_stat_recvmsg_eagain;
extern unsigned long long _st_stat_sendmsg;
extern unsigned long long _st_stat_sendmsg_eagain;
extern unsigned long long _st_stat_sendmmsg;
extern unsigned long long _st_stat_sendmmsg_eagain;
SrsPps* _srs_pps_recvmsg = new SrsPps(_srs_clock);
SrsPps* _srs_pps_recvmsg_eagain = new SrsPps(_srs_clock);
SrsPps* _srs_pps_sendmsg = new SrsPps(_srs_clock);
SrsPps* _srs_pps_sendmsg_eagain = new SrsPps(_srs_clock);
SrsPps* _srs_pps_sendmmsg = new SrsPps(_srs_clock);
SrsPps* _srs_pps_sendmmsg_eagain = new SrsPps(_srs_clock);

extern unsigned long long _st_stat_epoll;
extern unsigned long long _st_stat_epoll_zero;
extern unsigned long long _st_stat_epoll_shake;
extern unsigned long long _st_stat_epoll_spin;
SrsPps* _srs_pps_epoll = new SrsPps(_srs_clock);
SrsPps* _srs_pps_epoll_zero = new SrsPps(_srs_clock);
SrsPps* _srs_pps_epoll_shake = new SrsPps(_srs_clock);
SrsPps* _srs_pps_epoll_spin = new SrsPps(_srs_clock);

extern unsigned long long _st_stat_clock_us;
extern unsigned long long _st_stat_clock_10ms;
extern unsigned long long _st_stat_clock_20ms;
extern unsigned long long _st_stat_clock_40ms;
extern unsigned long long _st_stat_clock_80ms;
extern unsigned long long _st_stat_clock_160ms;
extern unsigned long long _st_stat_clock_320ms;
extern unsigned long long _st_stat_clock_1000ms;
extern unsigned long long _st_stat_clock_s;
SrsPps* _srs_pps_clock_us = new SrsPps(_srs_clock);
SrsPps* _srs_pps_clock_10ms = new SrsPps(_srs_clock);
SrsPps* _srs_pps_clock_20ms = new SrsPps(_srs_clock);
SrsPps* _srs_pps_clock_40ms = new SrsPps(_srs_clock);
SrsPps* _srs_pps_clock_80ms = new SrsPps(_srs_clock);
SrsPps* _srs_pps_clock_160ms = new SrsPps(_srs_clock);
SrsPps* _srs_pps_clock_320ms = new SrsPps(_srs_clock);
SrsPps* _srs_pps_clock_1000ms = new SrsPps(_srs_clock);
SrsPps* _srs_pps_clock_s = new SrsPps(_srs_clock);

ISrsHybridServer::ISrsHybridServer()
{
}

ISrsHybridServer::~ISrsHybridServer()
{
}

SrsServerAdapter::SrsServerAdapter()
{
    srs = new SrsServer();
}

SrsServerAdapter::~SrsServerAdapter()
{
    srs_freep(srs);
}

srs_error_t SrsServerAdapter::initialize()
{
    srs_error_t err = srs_success;
    return err;
}

srs_error_t SrsServerAdapter::run()
{
    srs_error_t err = srs_success;

    // Initialize the whole system, set hooks to handle server level events.
    if ((err = srs->initialize(NULL)) != srs_success) {
        return srs_error_wrap(err, "server initialize");
    }

    if ((err = srs->initialize_st()) != srs_success) {
        return srs_error_wrap(err, "initialize st");
    }

    if ((err = srs->acquire_pid_file()) != srs_success) {
        return srs_error_wrap(err, "acquire pid file");
    }

    if ((err = srs->initialize_signal()) != srs_success) {
        return srs_error_wrap(err, "initialize signal");
    }

    if ((err = srs->listen()) != srs_success) {
        return srs_error_wrap(err, "listen");
    }

    if ((err = srs->register_signal()) != srs_success) {
        return srs_error_wrap(err, "register signal");
    }

    if ((err = srs->http_handle()) != srs_success) {
        return srs_error_wrap(err, "http handle");
    }

    if ((err = srs->ingest()) != srs_success) {
        return srs_error_wrap(err, "ingest");
    }

    if ((err = srs->start()) != srs_success) {
        return srs_error_wrap(err, "start");
    }

    return err;
}

void SrsServerAdapter::stop()
{
}

SrsServer* SrsServerAdapter::instance()
{
    return srs;
}

SrsHybridServer::SrsHybridServer()
{
    timer_ = NULL;
}

SrsHybridServer::~SrsHybridServer()
{
    vector<ISrsHybridServer*>::iterator it;
    for (it = servers.begin(); it != servers.end(); ++it) {
        ISrsHybridServer* server = *it;
        srs_freep(server);
    }
    servers.clear();
}

void SrsHybridServer::register_server(ISrsHybridServer* svr)
{
    servers.push_back(svr);
}

srs_error_t SrsHybridServer::initialize()
{
    srs_error_t err = srs_success;

    // init st
    if ((err = srs_st_init()) != srs_success) {
        return srs_error_wrap(err, "initialize st failed");
    }

    if ((err = setup_ticks()) != srs_success) {
        return srs_error_wrap(err, "tick");
    }

    vector<ISrsHybridServer*>::iterator it;
    for (it = servers.begin(); it != servers.end(); ++it) {
        ISrsHybridServer* server = *it;

        if ((err = server->initialize()) != srs_success) {
            return srs_error_wrap(err, "init server");
        }
    }

    return err;
}

srs_error_t SrsHybridServer::run()
{
    srs_error_t err = srs_success;

    vector<ISrsHybridServer*>::iterator it;
    for (it = servers.begin(); it != servers.end(); ++it) {
        ISrsHybridServer* server = *it;

        if ((err = server->run()) != srs_success) {
            return srs_error_wrap(err, "run server");
        }
    }

    // Wait for all server to quit.
    srs_thread_exit(NULL);

    return err;
}

void SrsHybridServer::stop()
{
    vector<ISrsHybridServer*>::iterator it;
    for (it = servers.begin(); it != servers.end(); ++it) {
        ISrsHybridServer* server = *it;
        server->stop();
    }
}

SrsServerAdapter* SrsHybridServer::srs()
{
    for (vector<ISrsHybridServer*>::iterator it = servers.begin(); it != servers.end(); ++it) {
        if (dynamic_cast<SrsServerAdapter*>(*it)) {
            return dynamic_cast<SrsServerAdapter*>(*it);
        }
    }
    return NULL;
}

srs_error_t SrsHybridServer::setup_ticks()
{
    srs_error_t err = srs_success;

    timer_ = new SrsHourGlass("hybrid", this, 1 * SRS_UTIME_SECONDS);

    if ((err = timer_->tick(1, 5 * SRS_UTIME_SECONDS)) != srs_success) {
        return srs_error_wrap(err, "tick");
    }

    if ((err = timer_->start()) != srs_success) {
        return srs_error_wrap(err, "start");
    }

    return err;
}

srs_error_t SrsHybridServer::notify(int event, srs_utime_t interval, srs_utime_t tick)
{
    srs_error_t err = srs_success;

    // Show statistics for RTC server.
    SrsProcSelfStat* u = srs_get_self_proc_stat();
    // Resident Set Size: number of pages the process has in real memory.
    int memory = (int)(u->rss * 4 / 1024);

    static char buf[128];

    string cid_desc;
    _srs_pps_cids_get->update(); _srs_pps_cids_set->update();
    if (_srs_pps_cids_get->r10s() || _srs_pps_cids_set->r10s()) {
        snprintf(buf, sizeof(buf), ", cid=%d,%d", _srs_pps_cids_get->r10s(), _srs_pps_cids_set->r10s());
        cid_desc = buf;
    }

    string timer_desc;
    _srs_pps_timer->update(); _srs_pps_pub->update(); _srs_pps_conn->update();
    if (_srs_pps_timer->r10s() || _srs_pps_pub->r10s() || _srs_pps_conn->r10s()) {
        snprintf(buf, sizeof(buf), ", timer=%d,%d,%d", _srs_pps_timer->r10s(), _srs_pps_pub->r10s(), _srs_pps_conn->r10s());
        timer_desc = buf;
    }

    string free_desc;
    _srs_pps_dispose->update();
    if (_srs_pps_dispose->r10s()) {
        snprintf(buf, sizeof(buf), ", free=%d", _srs_pps_dispose->r10s());
        free_desc = buf;
    }

    string recvfrom_desc;
    _srs_pps_recvfrom->update(_st_stat_recvfrom); _srs_pps_recvfrom_eagain->update(_st_stat_recvfrom_eagain);
    _srs_pps_sendto->update(_st_stat_sendto); _srs_pps_sendto_eagain->update(_st_stat_sendto_eagain);
    if (_srs_pps_recvfrom->r10s() || _srs_pps_recvfrom_eagain->r10s() || _srs_pps_sendto->r10s() || _srs_pps_sendto_eagain->r10s()) {
        snprintf(buf, sizeof(buf), ", udp=%d,%d,%d,%d", _srs_pps_recvfrom->r10s(), _srs_pps_recvfrom_eagain->r10s(), _srs_pps_sendto->r10s(), _srs_pps_sendto_eagain->r10s());
        recvfrom_desc = buf;
    }

    string io_desc;
    _srs_pps_read->update(_st_stat_read); _srs_pps_read_eagain->update(_st_stat_read_eagain);
    _srs_pps_readv->update(_st_stat_readv); _srs_pps_readv_eagain->update(_st_stat_readv_eagain);
    _srs_pps_writev->update(_st_stat_writev); _srs_pps_writev_eagain->update(_st_stat_writev_eagain);
    if (_srs_pps_read->r10s() || _srs_pps_read_eagain->r10s() || _srs_pps_readv->r10s() || _srs_pps_readv_eagain->r10s() || _srs_pps_writev->r10s() || _srs_pps_writev_eagain->r10s()) {
        snprintf(buf, sizeof(buf), ", io=%d,%d,%d,%d,%d,%d", _srs_pps_read->r10s(), _srs_pps_read_eagain->r10s(), _srs_pps_readv->r10s(), _srs_pps_readv_eagain->r10s(), _srs_pps_writev->r10s(), _srs_pps_writev_eagain->r10s());
        io_desc = buf;
    }

    string msg_desc;
    _srs_pps_recvmsg->update(_st_stat_recvmsg); _srs_pps_recvmsg_eagain->update(_st_stat_recvmsg_eagain);
    _srs_pps_sendmsg->update(_st_stat_sendmsg); _srs_pps_sendmsg_eagain->update(_st_stat_sendmsg_eagain);
    _srs_pps_sendmmsg->update(_st_stat_sendmmsg); _srs_pps_sendmmsg_eagain->update(_st_stat_sendmmsg_eagain);
    if (_srs_pps_recvmsg->r10s() || _srs_pps_recvmsg_eagain->r10s() || _srs_pps_sendmsg->r10s() || _srs_pps_sendmsg_eagain->r10s() || _srs_pps_sendmmsg->r10s() || _srs_pps_sendmmsg_eagain->r10s()) {
        snprintf(buf, sizeof(buf), ", msg=%d,%d,%d,%d,%d,%d", _srs_pps_recvmsg->r10s(), _srs_pps_recvmsg_eagain->r10s(), _srs_pps_sendmsg->r10s(), _srs_pps_sendmsg_eagain->r10s(), _srs_pps_sendmmsg->r10s(), _srs_pps_sendmmsg_eagain->r10s());
        msg_desc = buf;
    }

    string epoll_desc;
    _srs_pps_epoll->update(_st_stat_epoll); _srs_pps_epoll_zero->update(_st_stat_epoll_zero);
    _srs_pps_epoll_shake->update(_st_stat_epoll_shake); _srs_pps_epoll_spin->update(_st_stat_epoll_spin);
    if (_srs_pps_epoll->r10s() || _srs_pps_epoll_zero->r10s() || _srs_pps_epoll_shake->r10s() || _srs_pps_epoll_spin->r10s()) {
        snprintf(buf, sizeof(buf), ", epoll=%d,%d,%d,%d", _srs_pps_epoll->r10s(), _srs_pps_epoll_zero->r10s(), _srs_pps_epoll_shake->r10s(), _srs_pps_epoll_spin->r10s());
        epoll_desc = buf;
    }

    string clock_desc;
    _srs_pps_clock_us->update(_st_stat_clock_us); _srs_pps_clock_s->update(_st_stat_clock_s);
    _srs_pps_clock_10ms->update(_st_stat_clock_10ms); _srs_pps_clock_20ms->update(_st_stat_clock_20ms);
    _srs_pps_clock_40ms->update(_st_stat_clock_40ms); _srs_pps_clock_80ms->update(_st_stat_clock_80ms);
    _srs_pps_clock_160ms->update(_st_stat_clock_160ms); _srs_pps_clock_320ms->update(_st_stat_clock_320ms);
    _srs_pps_clock_1000ms->update(_st_stat_clock_1000ms);
    if (_srs_pps_clock_us->r10s() || _srs_pps_clock_s->r10s() || _srs_pps_clock_10ms->r10s() || _srs_pps_clock_20ms->r10s() || _srs_pps_clock_40ms->r10s() || _srs_pps_clock_80ms->r10s() || _srs_pps_clock_160ms->r10s() || _srs_pps_clock_320ms->r10s() || _srs_pps_clock_1000ms->r10s()) {
        snprintf(buf, sizeof(buf), ", clock=%d,%d,%d,%d,%d,%d,%d,%d,%d", _srs_pps_clock_us->r10s(), _srs_pps_clock_10ms->r10s(), _srs_pps_clock_20ms->r10s(), _srs_pps_clock_40ms->r10s(), _srs_pps_clock_80ms->r10s(), _srs_pps_clock_160ms->r10s(), _srs_pps_clock_320ms->r10s(), _srs_pps_clock_1000ms->r10s(), _srs_pps_clock_s->r10s());
        clock_desc = buf;
    }

    srs_trace("Hybrid cpu=%.2f%%,%dMB%s%s%s%s%s%s%s%s",
        u->percent * 100, memory,
        cid_desc.c_str(), timer_desc.c_str(), free_desc.c_str(),
        recvfrom_desc.c_str(), io_desc.c_str(), msg_desc.c_str(),
        epoll_desc.c_str(), clock_desc.c_str()
    );

    return err;
}

SrsHybridServer* _srs_hybrid = new SrsHybridServer();

