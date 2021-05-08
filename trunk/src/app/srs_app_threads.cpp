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

#include <srs_app_threads.hpp>

#include <srs_app_config.hpp>
#include <srs_app_hybrid.hpp>
#include <srs_app_utility.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_app_rtc_source.hpp>
#include <srs_app_source.hpp>
#include <srs_app_pithy_print.hpp>
#include <srs_app_rtc_server.hpp>
#include <srs_app_rtc_dtls.hpp>
#include <srs_app_rtc_conn.hpp>
#include <srs_app_log.hpp>

#include <string>
using namespace std;

extern ISrsLog* _srs_log;
extern ISrsContext* _srs_context;
extern SrsConfig* _srs_config;

extern SrsStageManager* _srs_stages;
extern SrsRtcBlackhole* _srs_blackhole;
extern SrsResourceManager* _srs_rtc_manager;

extern SrsRtpObjectCacheManager<SrsRtpPacket2>* _srs_rtp_cache;
extern SrsRtpObjectCacheManager<SrsRtpRawPayload>* _srs_rtp_raw_cache;
extern SrsRtpObjectCacheManager<SrsRtpFUAPayload2>* _srs_rtp_fua_cache;

extern SrsRtpObjectCacheManager<SrsSharedPtrMessage>* _srs_rtp_msg_cache_buffers;
extern SrsRtpObjectCacheManager<SrsSharedPtrMessage>* _srs_rtp_msg_cache_objs;

extern SrsResourceManager* _srs_rtc_manager;
extern SrsDtlsCertificate* _srs_rtc_dtls_certificate;

#include <srs_protocol_kbps.hpp>

extern SrsPps* _srs_pps_snack2;
extern SrsPps* _srs_pps_snack3;
extern SrsPps* _srs_pps_snack4;

SrsPps* _srs_pps_aloss2 = NULL;

extern SrsPps* _srs_pps_ids;
extern SrsPps* _srs_pps_fids;
extern SrsPps* _srs_pps_fids_level0;
extern SrsPps* _srs_pps_dispose;

extern SrsPps* _srs_pps_timer;

extern SrsPps* _srs_pps_snack;
extern SrsPps* _srs_pps_snack2;
extern SrsPps* _srs_pps_snack3;
extern SrsPps* _srs_pps_snack4;
extern SrsPps* _srs_pps_sanack;
extern SrsPps* _srs_pps_svnack;

extern SrsPps* _srs_pps_rnack;
extern SrsPps* _srs_pps_rnack2;
extern SrsPps* _srs_pps_rhnack;
extern SrsPps* _srs_pps_rmnack;

#if defined(SRS_DEBUG) && defined(SRS_DEBUG_STATS)
extern SrsPps* _srs_pps_recvfrom;
extern SrsPps* _srs_pps_recvfrom_eagain;
extern SrsPps* _srs_pps_sendto;
extern SrsPps* _srs_pps_sendto_eagain;

extern SrsPps* _srs_pps_read;
extern SrsPps* _srs_pps_read_eagain;
extern SrsPps* _srs_pps_readv;
extern SrsPps* _srs_pps_readv_eagain;
extern SrsPps* _srs_pps_writev;
extern SrsPps* _srs_pps_writev_eagain;

extern SrsPps* _srs_pps_recvmsg;
extern SrsPps* _srs_pps_recvmsg_eagain;
extern SrsPps* _srs_pps_sendmsg;
extern SrsPps* _srs_pps_sendmsg_eagain;

extern SrsPps* _srs_pps_epoll;
extern SrsPps* _srs_pps_epoll_zero;
extern SrsPps* _srs_pps_epoll_shake;
extern SrsPps* _srs_pps_epoll_spin;

extern SrsPps* _srs_pps_sched_15ms;
extern SrsPps* _srs_pps_sched_20ms;
extern SrsPps* _srs_pps_sched_25ms;
extern SrsPps* _srs_pps_sched_30ms;
extern SrsPps* _srs_pps_sched_35ms;
extern SrsPps* _srs_pps_sched_40ms;
extern SrsPps* _srs_pps_sched_80ms;
extern SrsPps* _srs_pps_sched_160ms;
extern SrsPps* _srs_pps_sched_s;
#endif

extern SrsPps* _srs_pps_clock_15ms;
extern SrsPps* _srs_pps_clock_20ms;
extern SrsPps* _srs_pps_clock_25ms;
extern SrsPps* _srs_pps_clock_30ms;
extern SrsPps* _srs_pps_clock_35ms;
extern SrsPps* _srs_pps_clock_40ms;
extern SrsPps* _srs_pps_clock_80ms;
extern SrsPps* _srs_pps_clock_160ms;
extern SrsPps* _srs_pps_timer_s;

#if defined(SRS_DEBUG) && defined(SRS_DEBUG_STATS)
extern SrsPps* _srs_pps_thread_run;
extern SrsPps* _srs_pps_thread_idle;
extern SrsPps* _srs_pps_thread_yield;
extern SrsPps* _srs_pps_thread_yield2;
#endif

extern SrsPps* _srs_pps_rpkts;
extern SrsPps* _srs_pps_addrs;
extern SrsPps* _srs_pps_fast_addrs;

extern SrsPps* _srs_pps_spkts;

extern SrsPps* _srs_pps_sstuns;
extern SrsPps* _srs_pps_srtcps;
extern SrsPps* _srs_pps_srtps;

extern SrsPps* _srs_pps_pli;
extern SrsPps* _srs_pps_twcc;
extern SrsPps* _srs_pps_rr;
extern SrsPps* _srs_pps_pub;
extern SrsPps* _srs_pps_conn;

extern SrsPps* _srs_pps_rstuns;
extern SrsPps* _srs_pps_rrtps;
extern SrsPps* _srs_pps_rrtcps;

extern SrsPps* _srs_pps_aloss2;

extern SrsPps* _srs_pps_cids_get;
extern SrsPps* _srs_pps_cids_set;

extern SrsPps* _srs_pps_objs_msgs;

extern SrsPps* _srs_pps_objs_rtps;
extern SrsPps* _srs_pps_objs_rraw;
extern SrsPps* _srs_pps_objs_rfua;
extern SrsPps* _srs_pps_objs_rbuf;
extern SrsPps* _srs_pps_objs_rothers;
extern SrsPps* _srs_pps_objs_drop;

SrsCircuitBreaker::SrsCircuitBreaker()
{
    enabled_ = false;
    high_threshold_ = 0;
    high_pulse_ = 0;
    critical_threshold_ = 0;
    critical_pulse_ = 0;
    dying_threshold_ = 0;
    dying_pulse_ = 0;

    hybrid_high_water_level_ = 0;
    hybrid_critical_water_level_ = 0;
    hybrid_dying_water_level_ = 0;
}

SrsCircuitBreaker::~SrsCircuitBreaker()
{
}

srs_error_t SrsCircuitBreaker::initialize()
{
    srs_error_t err = srs_success;

    enabled_ = _srs_config->get_circuit_breaker();
    high_threshold_ = _srs_config->get_high_threshold();
    high_pulse_ = _srs_config->get_high_pulse();
    critical_threshold_ = _srs_config->get_critical_threshold();
    critical_pulse_ = _srs_config->get_critical_pulse();
    dying_threshold_ = _srs_config->get_dying_threshold();
    dying_pulse_ = _srs_config->get_dying_pulse();

    // Update the water level for circuit breaker.
    // @see SrsCircuitBreaker::on_timer()
    _srs_hybrid->timer1s()->subscribe(this);

    srs_trace("CircuitBreaker: enabled=%d, high=%dx%d, critical=%dx%d, dying=%dx%d", enabled_,
        high_pulse_, high_threshold_, critical_pulse_, critical_threshold_,
        dying_pulse_, dying_threshold_);

    return err;
}

bool SrsCircuitBreaker::hybrid_high_water_level()
{
    return enabled_ && (hybrid_critical_water_level() || hybrid_high_water_level_);
}

bool SrsCircuitBreaker::hybrid_critical_water_level()
{
    return enabled_ && (hybrid_dying_water_level() || hybrid_critical_water_level_);
}

bool SrsCircuitBreaker::hybrid_dying_water_level()
{
    return enabled_ && dying_pulse_ && hybrid_dying_water_level_ >= dying_pulse_;
}

srs_error_t SrsCircuitBreaker::on_timer(srs_utime_t interval)
{
    srs_error_t err = srs_success;

    // Update the CPU usage.
    srs_update_proc_stat();
    SrsProcSelfStat* stat = srs_get_self_proc_stat();

    // Reset the high water-level when CPU is low for N times.
    if (stat->percent * 100 > high_threshold_) {
        hybrid_high_water_level_ = high_pulse_;
    } else if (hybrid_high_water_level_ > 0) {
        hybrid_high_water_level_--;
    }

    // Reset the critical water-level when CPU is low for N times.
    if (stat->percent * 100 > critical_threshold_) {
        hybrid_critical_water_level_ = critical_pulse_;
    } else if (hybrid_critical_water_level_ > 0) {
        hybrid_critical_water_level_--;
    }

    // Reset the dying water-level when CPU is low for N times.
    if (stat->percent * 100 > dying_threshold_) {
        hybrid_dying_water_level_ = srs_min(dying_pulse_ + 1, hybrid_dying_water_level_ + 1);
    } else if (hybrid_dying_water_level_ > 0) {
        hybrid_dying_water_level_ = 0;
    }

    // Show statistics for RTC server.
    SrsProcSelfStat* u = srs_get_self_proc_stat();
    // Resident Set Size: number of pages the process has in real memory.
    int memory = (int)(u->rss * 4 / 1024);

    // The hybrid thread cpu and memory.
    float thread_percent = stat->percent * 100;

    if (enabled_ && (hybrid_high_water_level() || hybrid_critical_water_level() || _srs_pps_snack2->r10s())) {
        srs_trace("CircuitBreaker: cpu=%.2f%%,%dMB, break=%d,%d,%d, cond=%.2f%%, snk=%d,%d,%d",
            u->percent * 100, memory,
            hybrid_high_water_level(), hybrid_critical_water_level(), hybrid_dying_water_level(), // Whether Circuit-Break is enable.
            thread_percent, // The conditions to enable Circuit-Breaker.
            _srs_pps_snack2->r10s(), _srs_pps_snack3->r10s(), _srs_pps_snack4->r10s() // NACK packet,seqs sent.
        );
    }

    return err;
}

SrsCircuitBreaker* _srs_circuit_breaker = NULL;

srs_error_t srs_thread_initialize()
{
    srs_error_t err = srs_success;

    // Root global objects.
    _srs_log = new SrsFileLog();
    _srs_context = new SrsThreadContext();
    _srs_config = new SrsConfig();

    // The clock wall object.
    _srs_clock = new SrsWallClock();

    // The pps cids depends by st init.
    _srs_pps_cids_get = new SrsPps();
    _srs_pps_cids_set = new SrsPps();

    // Initialize ST, which depends on pps cids.
    if ((err = srs_st_init()) != srs_success) {
        return srs_error_wrap(err, "initialize st failed");
    }

    // The global objects which depends on ST.
    _srs_hybrid = new SrsHybridServer();
    _srs_rtc_sources = new SrsRtcStreamManager();
    _srs_sources = new SrsSourceManager();
    _srs_stages = new SrsStageManager();
    _srs_blackhole = new SrsRtcBlackhole();
    _srs_rtc_manager = new SrsResourceManager("RTC", true);
    _srs_circuit_breaker = new SrsCircuitBreaker();

    _srs_rtp_cache = new SrsRtpObjectCacheManager<SrsRtpPacket2>(sizeof(SrsRtpPacket2));
    _srs_rtp_raw_cache = new SrsRtpObjectCacheManager<SrsRtpRawPayload>(sizeof(SrsRtpRawPayload));
    _srs_rtp_fua_cache = new SrsRtpObjectCacheManager<SrsRtpFUAPayload2>(sizeof(SrsRtpFUAPayload2));

    _srs_rtp_msg_cache_buffers = new SrsRtpObjectCacheManager<SrsSharedPtrMessage>(sizeof(SrsSharedPtrMessage) + kRtpPacketSize);
    _srs_rtp_msg_cache_objs = new SrsRtpObjectCacheManager<SrsSharedPtrMessage>(sizeof(SrsSharedPtrMessage));

    _srs_rtc_manager = new SrsResourceManager("RTC", true);
    _srs_rtc_dtls_certificate = new SrsDtlsCertificate();

    // Initialize global pps, which depends on _srs_clock
    _srs_pps_ids = new SrsPps();
    _srs_pps_fids = new SrsPps();
    _srs_pps_fids_level0 = new SrsPps();
    _srs_pps_dispose = new SrsPps();

    _srs_pps_timer = new SrsPps();

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

    _srs_pps_sstuns = new SrsPps();
    _srs_pps_srtcps = new SrsPps();
    _srs_pps_srtps = new SrsPps();

    _srs_pps_pli = new SrsPps();
    _srs_pps_twcc = new SrsPps();
    _srs_pps_rr = new SrsPps();
    _srs_pps_pub = new SrsPps();
    _srs_pps_conn = new SrsPps();

    _srs_pps_rstuns = new SrsPps();
    _srs_pps_rrtps = new SrsPps();
    _srs_pps_rrtcps = new SrsPps();

    _srs_pps_aloss2 = new SrsPps();

    _srs_pps_objs_msgs = new SrsPps();

    _srs_pps_objs_rtps = new SrsPps();
    _srs_pps_objs_rraw = new SrsPps();
    _srs_pps_objs_rfua = new SrsPps();
    _srs_pps_objs_rbuf = new SrsPps();
    _srs_pps_objs_rothers = new SrsPps();
    _srs_pps_objs_drop = new SrsPps();

    return err;
}

