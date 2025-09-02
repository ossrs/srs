//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_KERNEL_KBPS_HPP
#define SRS_KERNEL_KBPS_HPP

#include <srs_core.hpp>

#include <srs_kernel_kbps.hpp>

#include <string>

class SrsWallClock;

// A sample for rate-based stat, such as kbps or kps.
class SrsRateSample
{
public:
    int64_t total;
    srs_utime_t time;
    // kbps or kps
    int rate;

public:
    SrsRateSample();
    virtual ~SrsRateSample();

public:
    virtual SrsRateSample *update(int64_t nn, srs_utime_t t, int k);
};

// A pps manager every some duration.
class SrsPps
{
private:
    SrsWallClock *clk_;

private:
    // samples
    SrsRateSample sample_10s_;
    SrsRateSample sample_30s_;
    SrsRateSample sample_1m_;
    SrsRateSample sample_5m_;
    SrsRateSample sample_60m_;

public:
    // Sugar for target to stat.
    int64_t sugar;

public:
    SrsPps();
    virtual ~SrsPps();

public:
    // Update with the nn which is target.
    void update();
    // Update with the nn.
    void update(int64_t nn);
    // Get the 10s average stat.
    int r10s();
    // Get the 30s average stat.
    int r30s();
};

/**
 * A time source to provide wall clock.
 */
class SrsWallClock
{
public:
    SrsWallClock();
    virtual ~SrsWallClock();

public:
    /**
     * Current time in srs_utime_t.
     */
    virtual srs_utime_t now();
};

// The global clock.
extern SrsWallClock *_srs_clock;

// Global SrsPps statistics variables
// I/O operations statistics
extern SrsPps *_srs_pps_recvfrom;
extern SrsPps *_srs_pps_recvfrom_eagain;
extern SrsPps *_srs_pps_sendto;
extern SrsPps *_srs_pps_sendto_eagain;

extern SrsPps *_srs_pps_read;
extern SrsPps *_srs_pps_read_eagain;
extern SrsPps *_srs_pps_readv;
extern SrsPps *_srs_pps_readv_eagain;
extern SrsPps *_srs_pps_writev;
extern SrsPps *_srs_pps_writev_eagain;

extern SrsPps *_srs_pps_recvmsg;
extern SrsPps *_srs_pps_recvmsg_eagain;
extern SrsPps *_srs_pps_sendmsg;
extern SrsPps *_srs_pps_sendmsg_eagain;

// Clock and timing statistics
extern SrsPps *_srs_pps_clock_15ms;
extern SrsPps *_srs_pps_clock_20ms;
extern SrsPps *_srs_pps_clock_25ms;
extern SrsPps *_srs_pps_clock_30ms;
extern SrsPps *_srs_pps_clock_35ms;
extern SrsPps *_srs_pps_clock_40ms;
extern SrsPps *_srs_pps_clock_80ms;
extern SrsPps *_srs_pps_clock_160ms;
extern SrsPps *_srs_pps_timer_s;

// WebRTC packet statistics
extern SrsPps *_srs_pps_rpkts;
extern SrsPps *_srs_pps_rstuns;
extern SrsPps *_srs_pps_rrtps;
extern SrsPps *_srs_pps_rrtcps;
extern SrsPps *_srs_pps_addrs;
extern SrsPps *_srs_pps_fast_addrs;

extern SrsPps *_srs_pps_spkts;
extern SrsPps *_srs_pps_sstuns;
extern SrsPps *_srs_pps_srtcps;
extern SrsPps *_srs_pps_srtps;

// Object and resource statistics
extern SrsPps *_srs_pps_ids;
extern SrsPps *_srs_pps_fids;
extern SrsPps *_srs_pps_fids_level0;
extern SrsPps *_srs_pps_dispose;

extern SrsPps *_srs_pps_timer;
extern SrsPps *_srs_pps_pub;
extern SrsPps *_srs_pps_conn;

extern SrsPps *_srs_pps_cids_get;
extern SrsPps *_srs_pps_cids_set;

// NACK and loss statistics
extern SrsPps *_srs_pps_snack;
extern SrsPps *_srs_pps_snack2;
extern SrsPps *_srs_pps_snack3;
extern SrsPps *_srs_pps_snack4;
extern SrsPps *_srs_pps_sanack;
extern SrsPps *_srs_pps_svnack;
extern SrsPps *_srs_pps_aloss2;

extern SrsPps *_srs_pps_rnack;
extern SrsPps *_srs_pps_rnack2;
extern SrsPps *_srs_pps_rhnack;
extern SrsPps *_srs_pps_rmnack;

// WebRTC control statistics
extern SrsPps *_srs_pps_pli;
extern SrsPps *_srs_pps_twcc;
extern SrsPps *_srs_pps_rr;

// Object statistics
extern SrsPps *_srs_pps_objs_rtps;
extern SrsPps *_srs_pps_objs_rraw;
extern SrsPps *_srs_pps_objs_rfua;
extern SrsPps *_srs_pps_objs_rbuf;
extern SrsPps *_srs_pps_objs_msgs;
extern SrsPps *_srs_pps_objs_rothers;

extern SrsPps *_srs_pps_objs_msgs;

#if defined(SRS_DEBUG) && defined(SRS_DEBUG_STATS)
// Debug thread statistics
extern SrsPps *_srs_pps_thread_run;
extern SrsPps *_srs_pps_thread_idle;
extern SrsPps *_srs_pps_thread_yield;
extern SrsPps *_srs_pps_thread_yield2;

// Debug epoll statistics
extern SrsPps *_srs_pps_epoll;
extern SrsPps *_srs_pps_epoll_zero;
extern SrsPps *_srs_pps_epoll_shake;
extern SrsPps *_srs_pps_epoll_spin;

// Debug scheduler statistics
extern SrsPps *_srs_pps_sched_15ms;
extern SrsPps *_srs_pps_sched_20ms;
extern SrsPps *_srs_pps_sched_25ms;
extern SrsPps *_srs_pps_sched_30ms;
extern SrsPps *_srs_pps_sched_35ms;
extern SrsPps *_srs_pps_sched_40ms;
extern SrsPps *_srs_pps_sched_80ms;
extern SrsPps *_srs_pps_sched_160ms;
extern SrsPps *_srs_pps_sched_s;
#endif

// Initialize the global kbps statistics variables
srs_error_t srs_global_kbps_initialize();

class SrsKbpsStats
{
public:
    std::string cid_desc;
    std::string timer_desc;
    std::string free_desc;
    std::string recvfrom_desc;
    std::string io_desc;
    std::string msg_desc;
    std::string epoll_desc;
    std::string sched_desc;
    std::string clock_desc;
    std::string thread_desc;
    std::string objs_desc;
};

// Update the global kbps statistics variables
void srs_global_kbps_update(SrsKbpsStats *stats);

class SrsKbsRtcStats
{
public:
    std::string rpkts_desc;
    std::string spkts_desc;
    std::string rtcp_desc;
    std::string snk_desc;
    std::string rnk_desc;
    std::string fid_desc;
};

// Update the global rtc statistics variables
void srs_global_rtc_update(SrsKbsRtcStats *stats);

#endif
