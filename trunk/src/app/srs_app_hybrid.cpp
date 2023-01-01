//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#include <srs_app_hybrid.hpp>

#include <srs_app_server.hpp>
#include <srs_app_config.hpp>
#include <srs_kernel_error.hpp>
#include <srs_protocol_st.hpp>
#include <srs_app_utility.hpp>
#include <srs_app_dvr.hpp>
#include <srs_app_tencentcloud.hpp>

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
SrsPps* _srs_pps_recvfrom = NULL;
SrsPps* _srs_pps_recvfrom_eagain = NULL;
SrsPps* _srs_pps_sendto = NULL;
SrsPps* _srs_pps_sendto_eagain = NULL;

extern unsigned long long _st_stat_read;
extern unsigned long long _st_stat_read_eagain;
extern unsigned long long _st_stat_readv;
extern unsigned long long _st_stat_readv_eagain;
extern unsigned long long _st_stat_writev;
extern unsigned long long _st_stat_writev_eagain;
SrsPps* _srs_pps_read = NULL;
SrsPps* _srs_pps_read_eagain = NULL;
SrsPps* _srs_pps_readv = NULL;
SrsPps* _srs_pps_readv_eagain = NULL;
SrsPps* _srs_pps_writev = NULL;
SrsPps* _srs_pps_writev_eagain = NULL;

extern unsigned long long _st_stat_recvmsg;
extern unsigned long long _st_stat_recvmsg_eagain;
extern unsigned long long _st_stat_sendmsg;
extern unsigned long long _st_stat_sendmsg_eagain;
SrsPps* _srs_pps_recvmsg = NULL;
SrsPps* _srs_pps_recvmsg_eagain = NULL;
SrsPps* _srs_pps_sendmsg = NULL;
SrsPps* _srs_pps_sendmsg_eagain = NULL;

extern unsigned long long _st_stat_epoll;
extern unsigned long long _st_stat_epoll_zero;
extern unsigned long long _st_stat_epoll_shake;
extern unsigned long long _st_stat_epoll_spin;
SrsPps* _srs_pps_epoll = NULL;
SrsPps* _srs_pps_epoll_zero = NULL;
SrsPps* _srs_pps_epoll_shake = NULL;
SrsPps* _srs_pps_epoll_spin = NULL;

extern unsigned long long _st_stat_sched_15ms;
extern unsigned long long _st_stat_sched_20ms;
extern unsigned long long _st_stat_sched_25ms;
extern unsigned long long _st_stat_sched_30ms;
extern unsigned long long _st_stat_sched_35ms;
extern unsigned long long _st_stat_sched_40ms;
extern unsigned long long _st_stat_sched_80ms;
extern unsigned long long _st_stat_sched_160ms;
extern unsigned long long _st_stat_sched_s;
SrsPps* _srs_pps_sched_15ms = NULL;
SrsPps* _srs_pps_sched_20ms = NULL;
SrsPps* _srs_pps_sched_25ms = NULL;
SrsPps* _srs_pps_sched_30ms = NULL;
SrsPps* _srs_pps_sched_35ms = NULL;
SrsPps* _srs_pps_sched_40ms = NULL;
SrsPps* _srs_pps_sched_80ms = NULL;
SrsPps* _srs_pps_sched_160ms = NULL;
SrsPps* _srs_pps_sched_s = NULL;
#endif

SrsPps* _srs_pps_clock_15ms = NULL;
SrsPps* _srs_pps_clock_20ms = NULL;
SrsPps* _srs_pps_clock_25ms = NULL;
SrsPps* _srs_pps_clock_30ms = NULL;
SrsPps* _srs_pps_clock_35ms = NULL;
SrsPps* _srs_pps_clock_40ms = NULL;
SrsPps* _srs_pps_clock_80ms = NULL;
SrsPps* _srs_pps_clock_160ms = NULL;
SrsPps* _srs_pps_timer_s = NULL;

#if defined(SRS_DEBUG) && defined(SRS_DEBUG_STATS)
extern int _st_active_count;
extern unsigned long long _st_stat_thread_run;
extern unsigned long long _st_stat_thread_idle;
extern unsigned long long _st_stat_thread_yield;
extern unsigned long long _st_stat_thread_yield2;
SrsPps* _srs_pps_thread_run = NULL;
SrsPps* _srs_pps_thread_idle = NULL;
SrsPps* _srs_pps_thread_yield = NULL;
SrsPps* _srs_pps_thread_yield2 = NULL;
#endif

extern SrsPps* _srs_pps_objs_rtps;
extern SrsPps* _srs_pps_objs_rraw;
extern SrsPps* _srs_pps_objs_rfua;
extern SrsPps* _srs_pps_objs_rbuf;
extern SrsPps* _srs_pps_objs_msgs;
extern SrsPps* _srs_pps_objs_rothers;

ISrsHybridServer::ISrsHybridServer()
{
}

ISrsHybridServer::~ISrsHybridServer()
{
}

SrsHybridServer::SrsHybridServer()
{
    // Create global shared timer.
    timer20ms_ = new SrsFastTimer("hybrid", 20 * SRS_UTIME_MILLISECONDS);
    timer100ms_ = new SrsFastTimer("hybrid", 100 * SRS_UTIME_MILLISECONDS);
    timer1s_ = new SrsFastTimer("hybrid", 1 * SRS_UTIME_SECONDS);
    timer5s_ = new SrsFastTimer("hybrid", 5 * SRS_UTIME_SECONDS);

    clock_monitor_ = new SrsClockWallMonitor();
}

SrsHybridServer::~SrsHybridServer()
{
    srs_freep(clock_monitor_);

    srs_freep(timer20ms_);
    srs_freep(timer100ms_);
    srs_freep(timer1s_);
    srs_freep(timer5s_);

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

    // Start the timer first.
    if ((err = timer20ms_->start()) != srs_success) {
        return srs_error_wrap(err, "start timer");
    }

    if ((err = timer100ms_->start()) != srs_success) {
        return srs_error_wrap(err, "start timer");
    }

    if ((err = timer1s_->start()) != srs_success) {
        return srs_error_wrap(err, "start timer");
    }

    if ((err = timer5s_->start()) != srs_success) {
        return srs_error_wrap(err, "start timer");
    }

    // Start the DVR async call.
    if ((err = _srs_dvr_async->start()) != srs_success) {
        return srs_error_wrap(err, "dvr async");
    }

#ifdef SRS_APM
    // Initialize TencentCloud CLS object.
    if ((err = _srs_cls->initialize()) != srs_success) {
        return srs_error_wrap(err, "cls client");
    }
    if ((err = _srs_apm->initialize()) != srs_success) {
        return srs_error_wrap(err, "apm client");
    }
#endif

    // Register some timers.
    timer20ms_->subscribe(clock_monitor_);
    timer5s_->subscribe(this);

    // Initialize all hybrid servers.
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

    // Wait for all servers which need to do cleanup.
    SrsWaitGroup wg;

    vector<ISrsHybridServer*>::iterator it;
    for (it = servers.begin(); it != servers.end(); ++it) {
        ISrsHybridServer* server = *it;

        if ((err = server->run(&wg)) != srs_success) {
            return srs_error_wrap(err, "run server");
        }
    }

    // Wait for all server to quit.
    wg.wait();

    return err;
}

void SrsHybridServer::stop()
{
    vector<ISrsHybridServer*>::iterator it;
    for (it = servers.begin(); it != servers.end(); ++it) {
        ISrsHybridServer* server = *it;
        server->stop();
    }

    srs_st_destroy();
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

SrsFastTimer* SrsHybridServer::timer20ms()
{
    return timer20ms_;
}

SrsFastTimer* SrsHybridServer::timer100ms()
{
    return timer100ms_;
}

SrsFastTimer* SrsHybridServer::timer1s()
{
    return timer1s_;
}

SrsFastTimer* SrsHybridServer::timer5s()
{
    return timer5s_;
}

srs_error_t SrsHybridServer::on_timer(srs_utime_t interval)
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
    if (_srs_pps_recvmsg->r10s() || _srs_pps_recvmsg_eagain->r10s() || _srs_pps_sendmsg->r10s() || _srs_pps_sendmsg_eagain->r10s()) {
        snprintf(buf, sizeof(buf), ", msg=%d,%d,%d,%d", _srs_pps_recvmsg->r10s(), _srs_pps_recvmsg_eagain->r10s(), _srs_pps_sendmsg->r10s(), _srs_pps_sendmsg_eagain->r10s());
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

    string objs_desc;
#ifdef SRS_RTC
    _srs_pps_objs_rtps->update(); _srs_pps_objs_rraw->update(); _srs_pps_objs_rfua->update(); _srs_pps_objs_rbuf->update(); _srs_pps_objs_msgs->update(); _srs_pps_objs_rothers->update();
    if (_srs_pps_objs_rtps->r10s() || _srs_pps_objs_rraw->r10s() || _srs_pps_objs_rfua->r10s() || _srs_pps_objs_rbuf->r10s() || _srs_pps_objs_msgs->r10s() || _srs_pps_objs_rothers->r10s()) {
        snprintf(buf, sizeof(buf), ", objs=(pkt:%d,raw:%d,fua:%d,msg:%d,oth:%d,buf:%d)",
            _srs_pps_objs_rtps->r10s(), _srs_pps_objs_rraw->r10s(), _srs_pps_objs_rfua->r10s(),
            _srs_pps_objs_msgs->r10s(), _srs_pps_objs_rothers->r10s(), _srs_pps_objs_rbuf->r10s());
        objs_desc = buf;
    }
#endif

    srs_trace("Hybrid cpu=%.2f%%,%dMB%s%s%s%s%s%s%s%s%s%s%s",
        u->percent * 100, memory,
        cid_desc.c_str(), timer_desc.c_str(),
        recvfrom_desc.c_str(), io_desc.c_str(), msg_desc.c_str(),
        epoll_desc.c_str(), sched_desc.c_str(), clock_desc.c_str(),
        thread_desc.c_str(), free_desc.c_str(), objs_desc.c_str()
    );

#ifdef SRS_APM
    // Report logs to CLS if enabled.
    if ((err = _srs_cls->report()) != srs_success) {
        srs_warn("ignore cls err %s", srs_error_desc(err).c_str());
        srs_freep(err);
    }

    // Report logs to APM if enabled.
    if ((err = _srs_apm->report()) != srs_success) {
        srs_warn("ignore apm err %s", srs_error_desc(err).c_str());
        srs_freep(err);
    }
#endif

    return err;
}

SrsHybridServer* _srs_hybrid = NULL;

