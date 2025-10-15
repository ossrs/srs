//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_kernel_kbps.hpp>

#include <srs_kernel_error.hpp>
#include <srs_kernel_io.hpp>
#include <srs_kernel_utility.hpp>

#include <cstdio>
#include <string>
using namespace std;

SrsRateSample::SrsRateSample()
{
    total_ = time_ = -1;
    rate_ = 0;
}

SrsRateSample::~SrsRateSample()
{
}

SrsRateSample *SrsRateSample::update(int64_t nn, srs_utime_t t, int k)
{
    total_ = nn;
    time_ = t;
    rate_ = k;
    return this;
}

void srs_pps_init(SrsRateSample &sample, int64_t nn, srs_utime_t now)
{
    if (sample.time_ < 0 || nn < sample.total_) {
        sample.update(nn, now, 0);
    }
}

void srs_pps_update(SrsRateSample &sample, int64_t nn, srs_utime_t now)
{
    int pps = (int)((nn - sample.total_) * 1000 / srsu2ms(now - sample.time_));
    if (pps == 0 && nn > sample.total_) {
        pps = 1; // For pps in (0, 1), we set to 1.
    }
    sample.update(nn, now, pps);
}

SrsPps::SrsPps()
{
    clk_ = _srs_clock;
    sugar_ = 0;
}

SrsPps::~SrsPps()
{
    clk_ = NULL;
}

void SrsPps::update()
{
    update(sugar_);
}

void SrsPps::update(int64_t nn)
{
    srs_assert(clk_);

    srs_utime_t now = clk_->now();

    srs_pps_init(sample_10s_, nn, now);
    srs_pps_init(sample_30s_, nn, now);
    srs_pps_init(sample_1m_, nn, now);
    srs_pps_init(sample_5m_, nn, now);
    srs_pps_init(sample_60m_, nn, now);

    if (now - sample_10s_.time_ >= 10 * SRS_UTIME_SECONDS) {
        srs_pps_update(sample_10s_, nn, now);
    }
    if (now - sample_30s_.time_ >= 30 * SRS_UTIME_SECONDS) {
        srs_pps_update(sample_30s_, nn, now);
    }
    if (now - sample_1m_.time_ >= 60 * SRS_UTIME_SECONDS) {
        srs_pps_update(sample_1m_, nn, now);
    }
    if (now - sample_5m_.time_ >= 300 * SRS_UTIME_SECONDS) {
        srs_pps_update(sample_5m_, nn, now);
    }
    if (now - sample_60m_.time_ >= 3600 * SRS_UTIME_SECONDS) {
        srs_pps_update(sample_60m_, nn, now);
    }
}

int SrsPps::r10s()
{
    return sample_10s_.rate_;
}

int SrsPps::r30s()
{
    return sample_30s_.rate_;
}

ISrsClock::ISrsClock()
{
}

ISrsClock::~ISrsClock()
{
}

SrsWallClock::SrsWallClock()
{
}

SrsWallClock::~SrsWallClock()
{
}

srs_utime_t SrsWallClock::now()
{
    return srs_time_now_cached();
}

SrsWallClock *_srs_clock = NULL;

// Global SrsPps statistics variables implementations
// I/O operations statistics
SrsPps *_srs_pps_recvfrom = NULL;
SrsPps *_srs_pps_recvfrom_eagain = NULL;
SrsPps *_srs_pps_sendto = NULL;
SrsPps *_srs_pps_sendto_eagain = NULL;

SrsPps *_srs_pps_read = NULL;
SrsPps *_srs_pps_read_eagain = NULL;
SrsPps *_srs_pps_readv = NULL;
SrsPps *_srs_pps_readv_eagain = NULL;
SrsPps *_srs_pps_writev = NULL;
SrsPps *_srs_pps_writev_eagain = NULL;

SrsPps *_srs_pps_recvmsg = NULL;
SrsPps *_srs_pps_recvmsg_eagain = NULL;
SrsPps *_srs_pps_sendmsg = NULL;
SrsPps *_srs_pps_sendmsg_eagain = NULL;

// Clock and timing statistics
SrsPps *_srs_pps_clock_15ms = NULL;
SrsPps *_srs_pps_clock_20ms = NULL;
SrsPps *_srs_pps_clock_25ms = NULL;
SrsPps *_srs_pps_clock_30ms = NULL;
SrsPps *_srs_pps_clock_35ms = NULL;
SrsPps *_srs_pps_clock_40ms = NULL;
SrsPps *_srs_pps_clock_80ms = NULL;
SrsPps *_srs_pps_clock_160ms = NULL;
SrsPps *_srs_pps_timer_s = NULL;

// WebRTC packet statistics (only the ones originally in srs_app_server.cpp)
SrsPps *_srs_pps_rstuns = NULL;
SrsPps *_srs_pps_rrtps = NULL;
SrsPps *_srs_pps_rrtcps = NULL;

// NACK and loss statistics (only _srs_pps_aloss2 was originally in srs_app_server.cpp)
SrsPps *_srs_pps_aloss2 = NULL;

SrsPps *_srs_pps_objs_msgs = NULL;

#if defined(SRS_DEBUG) && defined(SRS_DEBUG_STATS)
// Debug thread statistics
SrsPps *_srs_pps_thread_run = NULL;
SrsPps *_srs_pps_thread_idle = NULL;
SrsPps *_srs_pps_thread_yield = NULL;
SrsPps *_srs_pps_thread_yield2 = NULL;

// Debug epoll statistics
SrsPps *_srs_pps_epoll = NULL;
SrsPps *_srs_pps_epoll_zero = NULL;
SrsPps *_srs_pps_epoll_shake = NULL;
SrsPps *_srs_pps_epoll_spin = NULL;

// Debug scheduler statistics
SrsPps *_srs_pps_sched_15ms = NULL;
SrsPps *_srs_pps_sched_20ms = NULL;
SrsPps *_srs_pps_sched_25ms = NULL;
SrsPps *_srs_pps_sched_30ms = NULL;
SrsPps *_srs_pps_sched_35ms = NULL;
SrsPps *_srs_pps_sched_40ms = NULL;
SrsPps *_srs_pps_sched_80ms = NULL;
SrsPps *_srs_pps_sched_160ms = NULL;
SrsPps *_srs_pps_sched_s = NULL;
#endif

#if defined(SRS_DEBUG) && defined(SRS_DEBUG_STATS)
extern "C" {
// External ST statistics
extern __thread unsigned long long _st_stat_recvfrom;
extern __thread unsigned long long _st_stat_recvfrom_eagain;
extern __thread unsigned long long _st_stat_sendto;
extern __thread unsigned long long _st_stat_sendto_eagain;

extern __thread unsigned long long _st_stat_read;
extern __thread unsigned long long _st_stat_read_eagain;
extern __thread unsigned long long _st_stat_readv;
extern __thread unsigned long long _st_stat_readv_eagain;
extern __thread unsigned long long _st_stat_writev;
extern __thread unsigned long long _st_stat_writev_eagain;

extern __thread unsigned long long _st_stat_recvmsg;
extern __thread unsigned long long _st_stat_recvmsg_eagain;
extern __thread unsigned long long _st_stat_sendmsg;
extern __thread unsigned long long _st_stat_sendmsg_eagain;

extern __thread unsigned long long _st_stat_epoll;
extern __thread unsigned long long _st_stat_epoll_zero;
extern __thread unsigned long long _st_stat_epoll_shake;
extern __thread unsigned long long _st_stat_epoll_spin;

extern __thread unsigned long long _st_stat_sched_15ms;
extern __thread unsigned long long _st_stat_sched_20ms;
extern __thread unsigned long long _st_stat_sched_25ms;
extern __thread unsigned long long _st_stat_sched_30ms;
extern __thread unsigned long long _st_stat_sched_35ms;
extern __thread unsigned long long _st_stat_sched_40ms;
extern __thread unsigned long long _st_stat_sched_80ms;
extern __thread unsigned long long _st_stat_sched_160ms;
extern __thread unsigned long long _st_stat_sched_s;

extern __thread int _st_active_count;
extern __thread int _st_num_free_stacks;

extern __thread unsigned long long _st_stat_thread_run;
extern __thread unsigned long long _st_stat_thread_idle;
extern __thread unsigned long long _st_stat_thread_yield;
extern __thread unsigned long long _st_stat_thread_yield2;
}
#endif

srs_error_t srs_global_kbps_initialize()
{
    srs_error_t err = srs_success;

    // The clock wall object.
    _srs_clock = new SrsWallClock();

    // Initialize global pps, which depends on _srs_clock
    _srs_pps_ids = new SrsPps();
    _srs_pps_fids = new SrsPps();
    _srs_pps_fids_level0 = new SrsPps();
    _srs_pps_dispose = new SrsPps();

    _srs_pps_timer = new SrsPps();
    _srs_pps_conn = new SrsPps();
    _srs_pps_pub = new SrsPps();

    _srs_pps_snack = new SrsPps();
    _srs_pps_snack2 = new SrsPps();
    _srs_pps_snack3 = new SrsPps();
    _srs_pps_snack4 = new SrsPps();
    _srs_pps_sanack = new SrsPps();
    _srs_pps_svnack = new SrsPps();

    _srs_pps_rnack = new SrsPps();
    _srs_pps_rnack2 = new SrsPps();
    _srs_pps_rhnack = new SrsPps();
    _srs_pps_rmnack = new SrsPps();

#if defined(SRS_DEBUG) && defined(SRS_DEBUG_STATS)
    _srs_pps_recvfrom = new SrsPps();
    _srs_pps_recvfrom_eagain = new SrsPps();
    _srs_pps_sendto = new SrsPps();
    _srs_pps_sendto_eagain = new SrsPps();

    _srs_pps_read = new SrsPps();
    _srs_pps_read_eagain = new SrsPps();
    _srs_pps_readv = new SrsPps();
    _srs_pps_readv_eagain = new SrsPps();
    _srs_pps_writev = new SrsPps();
    _srs_pps_writev_eagain = new SrsPps();

    _srs_pps_recvmsg = new SrsPps();
    _srs_pps_recvmsg_eagain = new SrsPps();
    _srs_pps_sendmsg = new SrsPps();
    _srs_pps_sendmsg_eagain = new SrsPps();

    _srs_pps_epoll = new SrsPps();
    _srs_pps_epoll_zero = new SrsPps();
    _srs_pps_epoll_shake = new SrsPps();
    _srs_pps_epoll_spin = new SrsPps();

    _srs_pps_sched_15ms = new SrsPps();
    _srs_pps_sched_20ms = new SrsPps();
    _srs_pps_sched_25ms = new SrsPps();
    _srs_pps_sched_30ms = new SrsPps();
    _srs_pps_sched_35ms = new SrsPps();
    _srs_pps_sched_40ms = new SrsPps();
    _srs_pps_sched_80ms = new SrsPps();
    _srs_pps_sched_160ms = new SrsPps();
    _srs_pps_sched_s = new SrsPps();
#endif

    _srs_pps_clock_15ms = new SrsPps();
    _srs_pps_clock_20ms = new SrsPps();
    _srs_pps_clock_25ms = new SrsPps();
    _srs_pps_clock_30ms = new SrsPps();
    _srs_pps_clock_35ms = new SrsPps();
    _srs_pps_clock_40ms = new SrsPps();
    _srs_pps_clock_80ms = new SrsPps();
    _srs_pps_clock_160ms = new SrsPps();
    _srs_pps_timer_s = new SrsPps();

#if defined(SRS_DEBUG) && defined(SRS_DEBUG_STATS)
    _srs_pps_thread_run = new SrsPps();
    _srs_pps_thread_idle = new SrsPps();
    _srs_pps_thread_yield = new SrsPps();
    _srs_pps_thread_yield2 = new SrsPps();
#endif

    _srs_pps_rpkts = new SrsPps();
    _srs_pps_addrs = new SrsPps();
    _srs_pps_fast_addrs = new SrsPps();

    _srs_pps_spkts = new SrsPps();
    _srs_pps_objs_msgs = new SrsPps();

    _srs_pps_sstuns = new SrsPps();
    _srs_pps_srtcps = new SrsPps();
    _srs_pps_srtps = new SrsPps();

    _srs_pps_rstuns = new SrsPps();
    _srs_pps_rrtps = new SrsPps();
    _srs_pps_rrtcps = new SrsPps();

    _srs_pps_aloss2 = new SrsPps();

    _srs_pps_pli = new SrsPps();
    _srs_pps_twcc = new SrsPps();
    _srs_pps_rr = new SrsPps();

    _srs_pps_objs_rtps = new SrsPps();
    _srs_pps_objs_rraw = new SrsPps();
    _srs_pps_objs_rfua = new SrsPps();
    _srs_pps_objs_rbuf = new SrsPps();
    _srs_pps_objs_rothers = new SrsPps();

    // The pps cids depends by st init.
    _srs_pps_cids_get = new SrsPps();
    _srs_pps_cids_set = new SrsPps();

    return err;
}

void srs_global_kbps_update(SrsKbpsStats *stats)
{
    static char buf[128];

    string &cid_desc = stats->cid_desc_;
    _srs_pps_cids_get->update();
    _srs_pps_cids_set->update();
    if (_srs_pps_cids_get->r10s() || _srs_pps_cids_set->r10s()) {
        snprintf(buf, sizeof(buf), ", cid=%d,%d", _srs_pps_cids_get->r10s(), _srs_pps_cids_set->r10s());
        cid_desc = buf;
    }
    string &timer_desc = stats->timer_desc_;
    _srs_pps_timer->update();
    _srs_pps_pub->update();
    _srs_pps_conn->update();
    if (_srs_pps_timer->r10s() || _srs_pps_pub->r10s() || _srs_pps_conn->r10s()) {
        snprintf(buf, sizeof(buf), ", timer=%d,%d,%d", _srs_pps_timer->r10s(), _srs_pps_pub->r10s(), _srs_pps_conn->r10s());
        timer_desc = buf;
    }

    string &free_desc = stats->free_desc_;
    _srs_pps_dispose->update();
    if (_srs_pps_dispose->r10s()) {
        snprintf(buf, sizeof(buf), ", free=%d", _srs_pps_dispose->r10s());
        free_desc = buf;
    }

    string &recvfrom_desc = stats->recvfrom_desc_;
    (void)recvfrom_desc;
#if defined(SRS_DEBUG) && defined(SRS_DEBUG_STATS)
    _srs_pps_recvfrom->update(_st_stat_recvfrom);
    _srs_pps_recvfrom_eagain->update(_st_stat_recvfrom_eagain);
    _srs_pps_sendto->update(_st_stat_sendto);
    _srs_pps_sendto_eagain->update(_st_stat_sendto_eagain);
    if (_srs_pps_recvfrom->r10s() || _srs_pps_recvfrom_eagain->r10s() || _srs_pps_sendto->r10s() || _srs_pps_sendto_eagain->r10s()) {
        snprintf(buf, sizeof(buf), ", udp=%d,%d,%d,%d", _srs_pps_recvfrom->r10s(), _srs_pps_recvfrom_eagain->r10s(), _srs_pps_sendto->r10s(), _srs_pps_sendto_eagain->r10s());
        recvfrom_desc = buf;
    }
#endif

    string &io_desc = stats->io_desc_;
    (void)io_desc;
#if defined(SRS_DEBUG) && defined(SRS_DEBUG_STATS)
    _srs_pps_read->update(_st_stat_read);
    _srs_pps_read_eagain->update(_st_stat_read_eagain);
    _srs_pps_readv->update(_st_stat_readv);
    _srs_pps_readv_eagain->update(_st_stat_readv_eagain);
    _srs_pps_writev->update(_st_stat_writev);
    _srs_pps_writev_eagain->update(_st_stat_writev_eagain);
    if (_srs_pps_read->r10s() || _srs_pps_read_eagain->r10s() || _srs_pps_readv->r10s() || _srs_pps_readv_eagain->r10s() || _srs_pps_writev->r10s() || _srs_pps_writev_eagain->r10s()) {
        snprintf(buf, sizeof(buf), ", io=%d,%d,%d,%d,%d,%d", _srs_pps_read->r10s(), _srs_pps_read_eagain->r10s(), _srs_pps_readv->r10s(), _srs_pps_readv_eagain->r10s(), _srs_pps_writev->r10s(), _srs_pps_writev_eagain->r10s());
        io_desc = buf;
    }
#endif

    string &msg_desc = stats->msg_desc_;
    (void)msg_desc;
#if defined(SRS_DEBUG) && defined(SRS_DEBUG_STATS)
    _srs_pps_recvmsg->update(_st_stat_recvmsg);
    _srs_pps_recvmsg_eagain->update(_st_stat_recvmsg_eagain);
    _srs_pps_sendmsg->update(_st_stat_sendmsg);
    _srs_pps_sendmsg_eagain->update(_st_stat_sendmsg_eagain);
    if (_srs_pps_recvmsg->r10s() || _srs_pps_recvmsg_eagain->r10s() || _srs_pps_sendmsg->r10s() || _srs_pps_sendmsg_eagain->r10s()) {
        snprintf(buf, sizeof(buf), ", msg=%d,%d,%d,%d", _srs_pps_recvmsg->r10s(), _srs_pps_recvmsg_eagain->r10s(), _srs_pps_sendmsg->r10s(), _srs_pps_sendmsg_eagain->r10s());
        msg_desc = buf;
    }
#endif

    string &epoll_desc = stats->epoll_desc_;
    (void)epoll_desc;
#if defined(SRS_DEBUG) && defined(SRS_DEBUG_STATS)
    _srs_pps_epoll->update(_st_stat_epoll);
    _srs_pps_epoll_zero->update(_st_stat_epoll_zero);
    _srs_pps_epoll_shake->update(_st_stat_epoll_shake);
    _srs_pps_epoll_spin->update(_st_stat_epoll_spin);
    if (_srs_pps_epoll->r10s() || _srs_pps_epoll_zero->r10s() || _srs_pps_epoll_shake->r10s() || _srs_pps_epoll_spin->r10s()) {
        snprintf(buf, sizeof(buf), ", epoll=%d,%d,%d,%d", _srs_pps_epoll->r10s(), _srs_pps_epoll_zero->r10s(), _srs_pps_epoll_shake->r10s(), _srs_pps_epoll_spin->r10s());
        epoll_desc = buf;
    }
#endif

    string &sched_desc = stats->sched_desc_;
    (void)sched_desc;
#if defined(SRS_DEBUG) && defined(SRS_DEBUG_STATS)
    _srs_pps_sched_160ms->update(_st_stat_sched_160ms);
    _srs_pps_sched_s->update(_st_stat_sched_s);
    _srs_pps_sched_15ms->update(_st_stat_sched_15ms);
    _srs_pps_sched_20ms->update(_st_stat_sched_20ms);
    _srs_pps_sched_25ms->update(_st_stat_sched_25ms);
    _srs_pps_sched_30ms->update(_st_stat_sched_30ms);
    _srs_pps_sched_35ms->update(_st_stat_sched_35ms);
    _srs_pps_sched_40ms->update(_st_stat_sched_40ms);
    _srs_pps_sched_80ms->update(_st_stat_sched_80ms);
    if (_srs_pps_sched_160ms->r10s() || _srs_pps_sched_s->r10s() || _srs_pps_sched_15ms->r10s() || _srs_pps_sched_20ms->r10s() || _srs_pps_sched_25ms->r10s() || _srs_pps_sched_30ms->r10s() || _srs_pps_sched_35ms->r10s() || _srs_pps_sched_40ms->r10s() || _srs_pps_sched_80ms->r10s()) {
        snprintf(buf, sizeof(buf), ", sched=%d,%d,%d,%d,%d,%d,%d,%d,%d", _srs_pps_sched_15ms->r10s(), _srs_pps_sched_20ms->r10s(), _srs_pps_sched_25ms->r10s(), _srs_pps_sched_30ms->r10s(), _srs_pps_sched_35ms->r10s(), _srs_pps_sched_40ms->r10s(), _srs_pps_sched_80ms->r10s(), _srs_pps_sched_160ms->r10s(), _srs_pps_sched_s->r10s());
        sched_desc = buf;
    }
#endif

    string &clock_desc = stats->clock_desc_;
    _srs_pps_clock_15ms->update();
    _srs_pps_clock_20ms->update();
    _srs_pps_clock_25ms->update();
    _srs_pps_clock_30ms->update();
    _srs_pps_clock_35ms->update();
    _srs_pps_clock_40ms->update();
    _srs_pps_clock_80ms->update();
    _srs_pps_clock_160ms->update();
    _srs_pps_timer_s->update();
    if (_srs_pps_clock_15ms->r10s() || _srs_pps_timer_s->r10s() || _srs_pps_clock_20ms->r10s() || _srs_pps_clock_25ms->r10s() || _srs_pps_clock_30ms->r10s() || _srs_pps_clock_35ms->r10s() || _srs_pps_clock_40ms->r10s() || _srs_pps_clock_80ms->r10s() || _srs_pps_clock_160ms->r10s()) {
        snprintf(buf, sizeof(buf), ", clock=%d,%d,%d,%d,%d,%d,%d,%d,%d", _srs_pps_clock_15ms->r10s(), _srs_pps_clock_20ms->r10s(), _srs_pps_clock_25ms->r10s(), _srs_pps_clock_30ms->r10s(), _srs_pps_clock_35ms->r10s(), _srs_pps_clock_40ms->r10s(), _srs_pps_clock_80ms->r10s(), _srs_pps_clock_160ms->r10s(), _srs_pps_timer_s->r10s());
        clock_desc = buf;
    }

    string &thread_desc = stats->thread_desc_;
    (void)thread_desc;
#if defined(SRS_DEBUG) && defined(SRS_DEBUG_STATS)
    _srs_pps_thread_run->update(_st_stat_thread_run);
    _srs_pps_thread_idle->update(_st_stat_thread_idle);
    _srs_pps_thread_yield->update(_st_stat_thread_yield);
    _srs_pps_thread_yield2->update(_st_stat_thread_yield2);
    if (_st_active_count > 0 || _st_num_free_stacks > 0 || _srs_pps_thread_run->r10s() || _srs_pps_thread_idle->r10s() || _srs_pps_thread_yield->r10s() || _srs_pps_thread_yield2->r10s()) {
        snprintf(buf, sizeof(buf), ", co=%d,%d,%d, stk=%d, yield=%d,%d", _st_active_count, _srs_pps_thread_run->r10s(), _srs_pps_thread_idle->r10s(), _st_num_free_stacks, _srs_pps_thread_yield->r10s(), _srs_pps_thread_yield2->r10s());
        thread_desc = buf;
    }
#endif

    string &objs_desc = stats->objs_desc_;
    _srs_pps_objs_rtps->update();
    _srs_pps_objs_rraw->update();
    _srs_pps_objs_rfua->update();
    _srs_pps_objs_rbuf->update();
    _srs_pps_objs_msgs->update();
    _srs_pps_objs_rothers->update();
    if (_srs_pps_objs_rtps->r10s() || _srs_pps_objs_rraw->r10s() || _srs_pps_objs_rfua->r10s() || _srs_pps_objs_rbuf->r10s() || _srs_pps_objs_msgs->r10s() || _srs_pps_objs_rothers->r10s()) {
        snprintf(buf, sizeof(buf), ", objs=(pkt:%d,raw:%d,fua:%d,msg:%d,oth:%d,buf:%d)",
                 _srs_pps_objs_rtps->r10s(), _srs_pps_objs_rraw->r10s(), _srs_pps_objs_rfua->r10s(),
                 _srs_pps_objs_msgs->r10s(), _srs_pps_objs_rothers->r10s(), _srs_pps_objs_rbuf->r10s());
        objs_desc = buf;
    }
}

void srs_global_rtc_update(SrsKbsRtcStats *stats)
{
    static char buf[128];

    string &rpkts_desc = stats->rpkts_desc_;
    _srs_pps_rpkts->update();
    _srs_pps_rrtps->update();
    _srs_pps_rstuns->update();
    _srs_pps_rrtcps->update();
    if (_srs_pps_rpkts->r10s() || _srs_pps_rrtps->r10s() || _srs_pps_rstuns->r10s() || _srs_pps_rrtcps->r10s()) {
        snprintf(buf, sizeof(buf), ", rpkts=(%d,rtp:%d,stun:%d,rtcp:%d)", _srs_pps_rpkts->r10s(), _srs_pps_rrtps->r10s(), _srs_pps_rstuns->r10s(), _srs_pps_rrtcps->r10s());
        rpkts_desc = buf;
    }

    string &spkts_desc = stats->spkts_desc_;
    _srs_pps_spkts->update();
    _srs_pps_srtps->update();
    _srs_pps_sstuns->update();
    _srs_pps_srtcps->update();
    if (_srs_pps_spkts->r10s() || _srs_pps_srtps->r10s() || _srs_pps_sstuns->r10s() || _srs_pps_srtcps->r10s()) {
        snprintf(buf, sizeof(buf), ", spkts=(%d,rtp:%d,stun:%d,rtcp:%d)", _srs_pps_spkts->r10s(), _srs_pps_srtps->r10s(), _srs_pps_sstuns->r10s(), _srs_pps_srtcps->r10s());
        spkts_desc = buf;
    }

    string &rtcp_desc = stats->rtcp_desc_;
    _srs_pps_pli->update();
    _srs_pps_twcc->update();
    _srs_pps_rr->update();
    if (_srs_pps_pli->r10s() || _srs_pps_twcc->r10s() || _srs_pps_rr->r10s()) {
        snprintf(buf, sizeof(buf), ", rtcp=(pli:%d,twcc:%d,rr:%d)", _srs_pps_pli->r10s(), _srs_pps_twcc->r10s(), _srs_pps_rr->r10s());
        rtcp_desc = buf;
    }

    string &snk_desc = stats->snk_desc_;
    _srs_pps_snack->update();
    _srs_pps_snack2->update();
    _srs_pps_sanack->update();
    _srs_pps_svnack->update();
    if (_srs_pps_snack->r10s() || _srs_pps_sanack->r10s() || _srs_pps_svnack->r10s() || _srs_pps_snack2->r10s()) {
        snprintf(buf, sizeof(buf), ", snk=(%d,a:%d,v:%d,h:%d)", _srs_pps_snack->r10s(), _srs_pps_sanack->r10s(), _srs_pps_svnack->r10s(), _srs_pps_snack2->r10s());
        snk_desc = buf;
    }

    string &rnk_desc = stats->rnk_desc_;
    _srs_pps_rnack->update();
    _srs_pps_rnack2->update();
    _srs_pps_rhnack->update();
    _srs_pps_rmnack->update();
    if (_srs_pps_rnack->r10s() || _srs_pps_rnack2->r10s() || _srs_pps_rhnack->r10s() || _srs_pps_rmnack->r10s()) {
        snprintf(buf, sizeof(buf), ", rnk=(%d,%d,h:%d,m:%d)", _srs_pps_rnack->r10s(), _srs_pps_rnack2->r10s(), _srs_pps_rhnack->r10s(), _srs_pps_rmnack->r10s());
        rnk_desc = buf;
    }

    string &fid_desc = stats->fid_desc_;
    _srs_pps_ids->update();
    _srs_pps_fids->update();
    _srs_pps_fids_level0->update();
    _srs_pps_addrs->update();
    _srs_pps_fast_addrs->update();
    if (_srs_pps_ids->r10s(), _srs_pps_fids->r10s(), _srs_pps_fids_level0->r10s(), _srs_pps_addrs->r10s(), _srs_pps_fast_addrs->r10s()) {
        snprintf(buf, sizeof(buf), ", fid=(id:%d,fid:%d,ffid:%d,addr:%d,faddr:%d)", _srs_pps_ids->r10s(), _srs_pps_fids->r10s(), _srs_pps_fids_level0->r10s(), _srs_pps_addrs->r10s(), _srs_pps_fast_addrs->r10s());
        fid_desc = buf;
    }
}

SrsKbpsSlice::SrsKbpsSlice(ISrsClock *c)
{
    clk_ = c;
    starttime_ = 0;
    bytes_ = 0;
}

SrsKbpsSlice::~SrsKbpsSlice()
{
    clk_ = NULL;
}

void SrsKbpsSlice::sample()
{
    srs_utime_t now = clk_->now();

    if (sample_30s_.time_ < 0) {
        sample_30s_.update(bytes_, now, 0);
    }
    if (sample_1m_.time_ < 0) {
        sample_1m_.update(bytes_, now, 0);
    }
    if (sample_5m_.time_ < 0) {
        sample_5m_.update(bytes_, now, 0);
    }
    if (sample_60m_.time_ < 0) {
        sample_60m_.update(bytes_, now, 0);
    }

    if (now - sample_30s_.time_ >= 30 * SRS_UTIME_SECONDS) {
        int kbps = (int)((bytes_ - sample_30s_.total_) * 8 / srsu2ms(now - sample_30s_.time_));
        sample_30s_.update(bytes_, now, kbps);
    }
    if (now - sample_1m_.time_ >= 60 * SRS_UTIME_SECONDS) {
        int kbps = (int)((bytes_ - sample_1m_.total_) * 8 / srsu2ms(now - sample_1m_.time_));
        sample_1m_.update(bytes_, now, kbps);
    }
    if (now - sample_5m_.time_ >= 300 * SRS_UTIME_SECONDS) {
        int kbps = (int)((bytes_ - sample_5m_.total_) * 8 / srsu2ms(now - sample_5m_.time_));
        sample_5m_.update(bytes_, now, kbps);
    }
    if (now - sample_60m_.time_ >= 3600 * SRS_UTIME_SECONDS) {
        int kbps = (int)((bytes_ - sample_60m_.total_) * 8 / srsu2ms(now - sample_60m_.time_));
        sample_60m_.update(bytes_, now, kbps);
    }
}

ISrsKbpsDelta::ISrsKbpsDelta()
{
}

ISrsKbpsDelta::~ISrsKbpsDelta()
{
}

ISrsEphemeralDelta::ISrsEphemeralDelta()
{
}

ISrsEphemeralDelta::~ISrsEphemeralDelta()
{
}

SrsEphemeralDelta::SrsEphemeralDelta()
{
    in_ = out_ = 0;
}

SrsEphemeralDelta::~SrsEphemeralDelta()
{
}

void SrsEphemeralDelta::add_delta(int64_t in, int64_t out)
{
    in_ += in;
    out_ += out;
}

void SrsEphemeralDelta::remark(int64_t *in, int64_t *out)
{
    if (in)
        *in = in_;
    if (out)
        *out = out_;
    in_ = out_ = 0;
}

ISrsNetworkDelta::ISrsNetworkDelta()
{
}

ISrsNetworkDelta::~ISrsNetworkDelta()
{
}

SrsNetworkDelta::SrsNetworkDelta()
{
    in_ = out_ = NULL;
    in_base_ = in_delta_ = 0;
    out_base_ = out_delta_ = 0;
}

SrsNetworkDelta::~SrsNetworkDelta()
{
}

void SrsNetworkDelta::set_io(ISrsProtocolStatistic *in, ISrsProtocolStatistic *out)
{
    if (in_) {
        in_delta_ += in_->get_recv_bytes() - in_base_;
    }
    if (in) {
        in_base_ = in->get_recv_bytes();
        in_delta_ += in_base_;
    }
    in_ = in;

    if (out_) {
        out_delta_ += out_->get_send_bytes() - out_base_;
    }
    if (out) {
        out_base_ = out->get_send_bytes();
        out_delta_ += out_base_;
    }
    out_ = out;
}

void SrsNetworkDelta::remark(int64_t *in, int64_t *out)
{
    if (in_) {
        in_delta_ += in_->get_recv_bytes() - in_base_;
        in_base_ = in_->get_recv_bytes();
    }
    if (out_) {
        out_delta_ += out_->get_send_bytes() - out_base_;
        out_base_ = out_->get_send_bytes();
    }

    *in = in_delta_;
    *out = out_delta_;
    in_delta_ = out_delta_ = 0;
}

SrsKbps::SrsKbps(ISrsClock *c)
{
    clk_ = c ? c : _srs_clock;
    is_ = new SrsKbpsSlice(clk_);
    os_ = new SrsKbpsSlice(clk_);
}

SrsKbps::~SrsKbps()
{
    srs_freep(is_);
    srs_freep(os_);
    clk_ = NULL;
}

int SrsKbps::get_send_kbps()
{
    int duration = srsu2ms(clk_->now() - is_->starttime_);
    if (duration <= 0) {
        return 0;
    }

    int64_t bytes = get_send_bytes();
    return (int)(bytes * 8 / duration);
}

int SrsKbps::get_recv_kbps()
{
    int duration = srsu2ms(clk_->now() - os_->starttime_);
    if (duration <= 0) {
        return 0;
    }

    int64_t bytes = get_recv_bytes();
    return (int)(bytes * 8 / duration);
}

int SrsKbps::get_send_kbps_30s()
{
    return os_->sample_30s_.rate_;
}

int SrsKbps::get_recv_kbps_30s()
{
    return is_->sample_30s_.rate_;
}

int SrsKbps::get_send_kbps_5m()
{
    return os_->sample_5m_.rate_;
}

int SrsKbps::get_recv_kbps_5m()
{
    return is_->sample_5m_.rate_;
}

void SrsKbps::add_delta(ISrsKbpsDelta *delta)
{
    if (!delta)
        return;

    int64_t in, out;
    delta->remark(&in, &out);
    add_delta(in, out);
}

void SrsKbps::add_delta(int64_t in, int64_t out)
{
    // update the total bytes
    is_->bytes_ += in;
    os_->bytes_ += out;

    // we donot sample, please use sample() to do resample.
}

void SrsKbps::sample()
{
    is_->sample();
    os_->sample();
}

int64_t SrsKbps::get_send_bytes()
{
    return os_->bytes_;
}

int64_t SrsKbps::get_recv_bytes()
{
    return is_->bytes_;
}

SrsNetworkKbps::SrsNetworkKbps(ISrsClock *clock)
{
    delta_ = new SrsNetworkDelta();
    kbps_ = new SrsKbps(clock);
}

SrsNetworkKbps::~SrsNetworkKbps()
{
    srs_freep(kbps_);
    srs_freep(delta_);
}

void SrsNetworkKbps::set_io(ISrsProtocolStatistic *in, ISrsProtocolStatistic *out)
{
    delta_->set_io(in, out);
}

void SrsNetworkKbps::sample()
{
    kbps_->add_delta(delta_);
    kbps_->sample();
}

int SrsNetworkKbps::get_send_kbps()
{
    return kbps_->get_send_kbps();
}

int SrsNetworkKbps::get_recv_kbps()
{
    return kbps_->get_recv_kbps();
}

int SrsNetworkKbps::get_send_kbps_30s()
{
    return kbps_->get_send_kbps_30s();
}

int SrsNetworkKbps::get_recv_kbps_30s()
{
    return kbps_->get_recv_kbps_30s();
}

int SrsNetworkKbps::get_send_kbps_5m()
{
    return kbps_->get_send_kbps_5m();
}

int SrsNetworkKbps::get_recv_kbps_5m()
{
    return kbps_->get_recv_kbps_5m();
}

int64_t SrsNetworkKbps::get_send_bytes()
{
    return kbps_->get_send_bytes();
}

int64_t SrsNetworkKbps::get_recv_bytes()
{
    return kbps_->get_recv_bytes();
}
