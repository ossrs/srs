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

#if defined(SRS_DEBUG) && defined(SRS_DEBUG_STATS)
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

extern unsigned long long _st_stat_sched_15ms;
extern unsigned long long _st_stat_sched_20ms;
extern unsigned long long _st_stat_sched_25ms;
extern unsigned long long _st_stat_sched_30ms;
extern unsigned long long _st_stat_sched_35ms;
extern unsigned long long _st_stat_sched_40ms;
extern unsigned long long _st_stat_sched_80ms;
extern unsigned long long _st_stat_sched_160ms;
extern unsigned long long _st_stat_sched_s;
SrsPps* _srs_pps_sched_15ms = new SrsPps(_srs_clock);
SrsPps* _srs_pps_sched_20ms = new SrsPps(_srs_clock);
SrsPps* _srs_pps_sched_25ms = new SrsPps(_srs_clock);
SrsPps* _srs_pps_sched_30ms = new SrsPps(_srs_clock);
SrsPps* _srs_pps_sched_35ms = new SrsPps(_srs_clock);
SrsPps* _srs_pps_sched_40ms = new SrsPps(_srs_clock);
SrsPps* _srs_pps_sched_80ms = new SrsPps(_srs_clock);
SrsPps* _srs_pps_sched_160ms = new SrsPps(_srs_clock);
SrsPps* _srs_pps_sched_s = new SrsPps(_srs_clock);
#endif

SrsPps* _srs_pps_clock_15ms = new SrsPps(_srs_clock);
SrsPps* _srs_pps_clock_20ms = new SrsPps(_srs_clock);
SrsPps* _srs_pps_clock_25ms = new SrsPps(_srs_clock);
SrsPps* _srs_pps_clock_30ms = new SrsPps(_srs_clock);
SrsPps* _srs_pps_clock_35ms = new SrsPps(_srs_clock);
SrsPps* _srs_pps_clock_40ms = new SrsPps(_srs_clock);
SrsPps* _srs_pps_clock_80ms = new SrsPps(_srs_clock);
SrsPps* _srs_pps_clock_160ms = new SrsPps(_srs_clock);
SrsPps* _srs_pps_timer_s = new SrsPps(_srs_clock);

#if defined(SRS_DEBUG) && defined(SRS_DEBUG_STATS)
extern int _st_active_count;
extern unsigned long long _st_stat_thread_run;
extern unsigned long long _st_stat_thread_idle;
extern unsigned long long _st_stat_thread_yield;
extern unsigned long long _st_stat_thread_yield2;
SrsPps* _srs_pps_thread_run = new SrsPps(_srs_clock);
SrsPps* _srs_pps_thread_idle = new SrsPps(_srs_clock);
SrsPps* _srs_pps_thread_yield = new SrsPps(_srs_clock);
SrsPps* _srs_pps_thread_yield2 = new SrsPps(_srs_clock);
#endif

ISrsHybridServer::ISrsHybridServer()
{
}

ISrsHybridServer::~ISrsHybridServer()
{
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
    srs_usleep(SRS_UTIME_NO_TIMEOUT);

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

    // Start timer for system global works.
    timer_ = new SrsHourGlass("hybrid", this, 20 * SRS_UTIME_MILLISECONDS);

    if ((err = timer_->tick(1, 20 * SRS_UTIME_MILLISECONDS)) != srs_success) {
        return srs_error_wrap(err, "tick");
    }

    if ((err = timer_->tick(2, 5 * SRS_UTIME_SECONDS)) != srs_success) {
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

    // Update system wall clock.
    if (event == 1) {
        static srs_utime_t clock = 0;

        srs_utime_t now = srs_update_system_time();
        if (!clock) {
            clock = now;
            return err;
        }

        srs_utime_t elapsed = now - clock;
        clock = now;

        if (elapsed <= 15 * SRS_UTIME_MILLISECONDS) {
            ++_srs_pps_clock_15ms->sugar;
        } else if (elapsed <= 21 * SRS_UTIME_MILLISECONDS) {
            ++_srs_pps_clock_20ms->sugar;
        } else if (elapsed <= 25 * SRS_UTIME_MILLISECONDS) {
            ++_srs_pps_clock_25ms->sugar;
        } else if (elapsed <= 30 * SRS_UTIME_MILLISECONDS) {
            ++_srs_pps_clock_30ms->sugar;
        } else if (elapsed <= 35 * SRS_UTIME_MILLISECONDS) {
            ++_srs_pps_clock_35ms->sugar;
        } else if (elapsed <= 40 * SRS_UTIME_MILLISECONDS) {
            ++_srs_pps_clock_40ms->sugar;
        } else if (elapsed <= 80 * SRS_UTIME_MILLISECONDS) {
            ++_srs_pps_clock_80ms->sugar;
        } else if (elapsed <= 160 * SRS_UTIME_MILLISECONDS) {
            ++_srs_pps_clock_160ms->sugar;
        } else {
            ++_srs_pps_timer_s->sugar;
        }
        return err;
    }

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
#if defined(SRS_DEBUG) && defined(SRS_DEBUG_STATS)
    _srs_pps_recvfrom->update(_st_stat_recvfrom); _srs_pps_recvfrom_eagain->update(_st_stat_recvfrom_eagain);
    _srs_pps_sendto->update(_st_stat_sendto); _srs_pps_sendto_eagain->update(_st_stat_sendto_eagain);
    if (_srs_pps_recvfrom->r10s() || _srs_pps_recvfrom_eagain->r10s() || _srs_pps_sendto->r10s() || _srs_pps_sendto_eagain->r10s()) {
        snprintf(buf, sizeof(buf), ", udp=%d,%d,%d,%d", _srs_pps_recvfrom->r10s(), _srs_pps_recvfrom_eagain->r10s(), _srs_pps_sendto->r10s(), _srs_pps_sendto_eagain->r10s());
        recvfrom_desc = buf;
    }
#endif

    string io_desc;
#if defined(SRS_DEBUG) && defined(SRS_DEBUG_STATS)
    _srs_pps_read->update(_st_stat_read); _srs_pps_read_eagain->update(_st_stat_read_eagain);
    _srs_pps_readv->update(_st_stat_readv); _srs_pps_readv_eagain->update(_st_stat_readv_eagain);
    _srs_pps_writev->update(_st_stat_writev); _srs_pps_writev_eagain->update(_st_stat_writev_eagain);
    if (_srs_pps_read->r10s() || _srs_pps_read_eagain->r10s() || _srs_pps_readv->r10s() || _srs_pps_readv_eagain->r10s() || _srs_pps_writev->r10s() || _srs_pps_writev_eagain->r10s()) {
        snprintf(buf, sizeof(buf), ", io=%d,%d,%d,%d,%d,%d", _srs_pps_read->r10s(), _srs_pps_read_eagain->r10s(), _srs_pps_readv->r10s(), _srs_pps_readv_eagain->r10s(), _srs_pps_writev->r10s(), _srs_pps_writev_eagain->r10s());
        io_desc = buf;
    }
#endif

    string msg_desc;
#if defined(SRS_DEBUG) && defined(SRS_DEBUG_STATS)
    _srs_pps_recvmsg->update(_st_stat_recvmsg); _srs_pps_recvmsg_eagain->update(_st_stat_recvmsg_eagain);
    _srs_pps_sendmsg->update(_st_stat_sendmsg); _srs_pps_sendmsg_eagain->update(_st_stat_sendmsg_eagain);
    _srs_pps_sendmmsg->update(_st_stat_sendmmsg); _srs_pps_sendmmsg_eagain->update(_st_stat_sendmmsg_eagain);
    if (_srs_pps_recvmsg->r10s() || _srs_pps_recvmsg_eagain->r10s() || _srs_pps_sendmsg->r10s() || _srs_pps_sendmsg_eagain->r10s() || _srs_pps_sendmmsg->r10s() || _srs_pps_sendmmsg_eagain->r10s()) {
        snprintf(buf, sizeof(buf), ", msg=%d,%d,%d,%d,%d,%d", _srs_pps_recvmsg->r10s(), _srs_pps_recvmsg_eagain->r10s(), _srs_pps_sendmsg->r10s(), _srs_pps_sendmsg_eagain->r10s(), _srs_pps_sendmmsg->r10s(), _srs_pps_sendmmsg_eagain->r10s());
        msg_desc = buf;
    }
#endif

    string epoll_desc;
#if defined(SRS_DEBUG) && defined(SRS_DEBUG_STATS)
    _srs_pps_epoll->update(_st_stat_epoll); _srs_pps_epoll_zero->update(_st_stat_epoll_zero);
    _srs_pps_epoll_shake->update(_st_stat_epoll_shake); _srs_pps_epoll_spin->update(_st_stat_epoll_spin);
    if (_srs_pps_epoll->r10s() || _srs_pps_epoll_zero->r10s() || _srs_pps_epoll_shake->r10s() || _srs_pps_epoll_spin->r10s()) {
        snprintf(buf, sizeof(buf), ", epoll=%d,%d,%d,%d", _srs_pps_epoll->r10s(), _srs_pps_epoll_zero->r10s(), _srs_pps_epoll_shake->r10s(), _srs_pps_epoll_spin->r10s());
        epoll_desc = buf;
    }
#endif

    string sched_desc;
#if defined(SRS_DEBUG) && defined(SRS_DEBUG_STATS)
    _srs_pps_sched_160ms->update(_st_stat_sched_160ms); _srs_pps_sched_s->update(_st_stat_sched_s);
    _srs_pps_sched_15ms->update(_st_stat_sched_15ms); _srs_pps_sched_20ms->update(_st_stat_sched_20ms);
    _srs_pps_sched_25ms->update(_st_stat_sched_25ms); _srs_pps_sched_30ms->update(_st_stat_sched_30ms);
    _srs_pps_sched_35ms->update(_st_stat_sched_35ms); _srs_pps_sched_40ms->update(_st_stat_sched_40ms);
    _srs_pps_sched_80ms->update(_st_stat_sched_80ms);
    if (_srs_pps_sched_160ms->r10s() || _srs_pps_sched_s->r10s() || _srs_pps_sched_15ms->r10s() || _srs_pps_sched_20ms->r10s() || _srs_pps_sched_25ms->r10s() || _srs_pps_sched_30ms->r10s() || _srs_pps_sched_35ms->r10s() || _srs_pps_sched_40ms->r10s() || _srs_pps_sched_80ms->r10s()) {
        snprintf(buf, sizeof(buf), ", sched=%d,%d,%d,%d,%d,%d,%d,%d,%d", _srs_pps_sched_15ms->r10s(), _srs_pps_sched_20ms->r10s(), _srs_pps_sched_25ms->r10s(), _srs_pps_sched_30ms->r10s(), _srs_pps_sched_35ms->r10s(), _srs_pps_sched_40ms->r10s(), _srs_pps_sched_80ms->r10s(), _srs_pps_sched_160ms->r10s(), _srs_pps_sched_s->r10s());
        sched_desc = buf;
    }
#endif

    string clock_desc;
    _srs_pps_clock_15ms->update(); _srs_pps_clock_20ms->update();
    _srs_pps_clock_25ms->update(); _srs_pps_clock_30ms->update();
    _srs_pps_clock_35ms->update(); _srs_pps_clock_40ms->update();
    _srs_pps_clock_80ms->update(); _srs_pps_clock_160ms->update();
    _srs_pps_timer_s->update();
    if (_srs_pps_clock_15ms->r10s() || _srs_pps_timer_s->r10s() || _srs_pps_clock_20ms->r10s() || _srs_pps_clock_25ms->r10s() || _srs_pps_clock_30ms->r10s() || _srs_pps_clock_35ms->r10s() || _srs_pps_clock_40ms->r10s() || _srs_pps_clock_80ms->r10s() || _srs_pps_clock_160ms->r10s()) {
        snprintf(buf, sizeof(buf), ", clock=%d,%d,%d,%d,%d,%d,%d,%d,%d", _srs_pps_clock_15ms->r10s(), _srs_pps_clock_20ms->r10s(), _srs_pps_clock_25ms->r10s(), _srs_pps_clock_30ms->r10s(), _srs_pps_clock_35ms->r10s(), _srs_pps_clock_40ms->r10s(), _srs_pps_clock_80ms->r10s(), _srs_pps_clock_160ms->r10s(), _srs_pps_timer_s->r10s());
        clock_desc = buf;
    }

    string thread_desc;
#if defined(SRS_DEBUG) && defined(SRS_DEBUG_STATS)
    _srs_pps_thread_run->update(_st_stat_thread_run); _srs_pps_thread_idle->update(_st_stat_thread_idle);
    _srs_pps_thread_yield->update(_st_stat_thread_yield); _srs_pps_thread_yield2->update(_st_stat_thread_yield2);
    if (_st_active_count > 0 || _srs_pps_thread_run->r10s() || _srs_pps_thread_idle->r10s() || _srs_pps_thread_yield->r10s() || _srs_pps_thread_yield2->r10s()) {
        snprintf(buf, sizeof(buf), ", co=%d,%d,%d, yield=%d,%d", _st_active_count, _srs_pps_thread_run->r10s(), _srs_pps_thread_idle->r10s(), _srs_pps_thread_yield->r10s(), _srs_pps_thread_yield2->r10s());
        thread_desc = buf;
    }
#endif

    srs_trace("Hybrid cpu=%.2f%%,%dMB%s%s%s%s%s%s%s%s%s%s",
        u->percent * 100, memory,
        cid_desc.c_str(), timer_desc.c_str(),
        recvfrom_desc.c_str(), io_desc.c_str(), msg_desc.c_str(),
        epoll_desc.c_str(), sched_desc.c_str(), clock_desc.c_str(),
        thread_desc.c_str(),
        free_desc.c_str()
    );

    return err;
}

SrsHybridServer* _srs_hybrid = new SrsHybridServer();

