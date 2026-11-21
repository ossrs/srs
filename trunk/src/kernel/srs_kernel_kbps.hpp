//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_KERNEL_KBPS_HPP
#define SRS_KERNEL_KBPS_HPP

#include <srs_core.hpp>

#include <srs_kernel_kbps.hpp>

#include <string>

class ISrsProtocolStatistic;

// An interface so the utest can mock the wall clock.
class ISrsClock
{
public:
    ISrsClock();
    virtual ~ISrsClock();

public:
    virtual srs_utime_t now() = 0;
};

class SrsWallClock : public ISrsClock
{
public:
    SrsWallClock();
    virtual ~SrsWallClock();

public:
    virtual srs_utime_t now();
};

extern SrsWallClock *_srs_clock;

class SrsRateSample
{
public:
    int64_t total_;
    srs_utime_t time_;
    // Kbps for SrsKbpsSlice, or count per second for SrsPps.
    int rate_;

public:
    SrsRateSample();
    virtual ~SrsRateSample();

public:
    virtual SrsRateSample *update(int64_t nn, srs_utime_t t, int k);
};

class SrsPps
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsClock *clk_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    SrsRateSample sample_10s_;
    SrsRateSample sample_30s_;
    SrsRateSample sample_1m_;
    SrsRateSample sample_5m_;
    SrsRateSample sample_60m_;

public:
    int64_t sugar_;

public:
    SrsPps();
    virtual ~SrsPps();

public:
    void update();
    void update(int64_t nn);
    int r10s();
    int r30s();
};

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

extern SrsPps *_srs_pps_clock_15ms;
extern SrsPps *_srs_pps_clock_20ms;
extern SrsPps *_srs_pps_clock_25ms;
extern SrsPps *_srs_pps_clock_30ms;
extern SrsPps *_srs_pps_clock_35ms;
extern SrsPps *_srs_pps_clock_40ms;
extern SrsPps *_srs_pps_clock_80ms;
extern SrsPps *_srs_pps_clock_160ms;
extern SrsPps *_srs_pps_timer_s;

// WebRTC packets.
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

// Objects and resources.
extern SrsPps *_srs_pps_ids;
extern SrsPps *_srs_pps_fids;
extern SrsPps *_srs_pps_fids_level0;
extern SrsPps *_srs_pps_dispose;

extern SrsPps *_srs_pps_timer;
extern SrsPps *_srs_pps_pub;
extern SrsPps *_srs_pps_conn;

extern SrsPps *_srs_pps_cids_get;
extern SrsPps *_srs_pps_cids_set;

// NACK and loss.
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

// RFC 4588 RTX: sent, received, unwrapped, and padding-only dropped.
extern SrsPps *_srs_pps_srtx;
extern SrsPps *_srs_pps_rrtx;
extern SrsPps *_srs_pps_rrtx_unwrap;
extern SrsPps *_srs_pps_rrtx_padding;

// WebRTC control.
extern SrsPps *_srs_pps_pli;
extern SrsPps *_srs_pps_twcc;
extern SrsPps *_srs_pps_rr;

extern SrsPps *_srs_pps_objs_rtps;
extern SrsPps *_srs_pps_objs_rraw;
extern SrsPps *_srs_pps_objs_rfua;
extern SrsPps *_srs_pps_objs_rbuf;
extern SrsPps *_srs_pps_objs_msgs;
extern SrsPps *_srs_pps_objs_rothers;

extern SrsPps *_srs_pps_objs_msgs;

#if defined(SRS_DEBUG) && defined(SRS_DEBUG_STATS)
extern SrsPps *_srs_pps_thread_run;
extern SrsPps *_srs_pps_thread_idle;
extern SrsPps *_srs_pps_thread_yield;
extern SrsPps *_srs_pps_thread_yield2;

extern SrsPps *_srs_pps_epoll;
extern SrsPps *_srs_pps_epoll_zero;
extern SrsPps *_srs_pps_epoll_shake;
extern SrsPps *_srs_pps_epoll_spin;

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

srs_error_t srs_global_kbps_initialize();

class SrsKbpsStats
{
public:
    std::string cid_desc_;
    std::string timer_desc_;
    std::string free_desc_;
    std::string recvfrom_desc_;
    std::string io_desc_;
    std::string msg_desc_;
    std::string epoll_desc_;
    std::string sched_desc_;
    std::string clock_desc_;
    std::string thread_desc_;
    std::string objs_desc_;
};

void srs_global_kbps_update(SrsKbpsStats *stats);

class SrsKbsRtcStats
{
public:
    std::string rpkts_desc_;
    std::string spkts_desc_;
    std::string rtcp_desc_;
    std::string snk_desc_;
    std::string rnk_desc_;
    std::string rtx_desc_;
    std::string fid_desc_;
};

void srs_global_rtc_update(SrsKbsRtcStats *stats);

class SrsKbpsSlice
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsClock *clk_;

public:
    int64_t bytes_;
    srs_utime_t starttime_;
    SrsRateSample sample_30s_;
    SrsRateSample sample_1m_;
    SrsRateSample sample_5m_;
    SrsRateSample sample_60m_;

public:
    SrsKbpsSlice(ISrsClock *c);
    virtual ~SrsKbpsSlice();

public:
    virtual void sample();
};

class ISrsKbpsDelta
{
public:
    ISrsKbpsDelta();
    virtual ~ISrsKbpsDelta();

public:
    // Gets the bytes since the last call, 0 if none.
    virtual void remark(int64_t *in, int64_t *out) = 0;
};

class ISrsEphemeralDelta : public ISrsKbpsDelta
{
public:
    ISrsEphemeralDelta();
    virtual ~ISrsEphemeralDelta();

public:
    virtual void add_delta(int64_t in, int64_t out) = 0;
};

// For example, a UDP server adds each packet it receives or sends.
class SrsEphemeralDelta : public ISrsEphemeralDelta
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    uint64_t in_;
    uint64_t out_;

public:
    SrsEphemeralDelta();
    virtual ~SrsEphemeralDelta();

public:
    virtual void add_delta(int64_t in, int64_t out);

public:
    virtual void remark(int64_t *in, int64_t *out);
};

class ISrsNetworkDelta : public ISrsKbpsDelta
{
public:
    ISrsNetworkDelta();
    virtual ~ISrsNetworkDelta();

public:
    virtual void set_io(ISrsProtocolStatistic *in, ISrsProtocolStatistic *out) = 0;
};

class SrsNetworkDelta : public ISrsNetworkDelta
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsProtocolStatistic *in_;
    ISrsProtocolStatistic *out_;
    uint64_t in_base_;
    uint64_t in_delta_;
    uint64_t out_base_;
    uint64_t out_delta_;

public:
    SrsNetworkDelta();
    virtual ~SrsNetworkDelta();

public:
    // Bytes the new io has already counted are added as a delta too.
    virtual void set_io(ISrsProtocolStatistic *in, ISrsProtocolStatistic *out);

public:
    virtual void remark(int64_t *in, int64_t *out);
};

class SrsKbps
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    SrsKbpsSlice *is_;
    SrsKbpsSlice *os_;
    ISrsClock *clk_;

public:
    SrsKbps(ISrsClock *c = NULL);
    virtual ~SrsKbps();

public:
    virtual int get_send_kbps();
    virtual int get_recv_kbps();
    virtual int get_send_kbps_30s();
    virtual int get_recv_kbps_30s();
    virtual int get_send_kbps_5m();
    virtual int get_recv_kbps_5m();

public:
    // The rates change only in sample(), so call it after adding all deltas.
    virtual void add_delta(int64_t in, int64_t out);
    virtual void add_delta(ISrsKbpsDelta *delta);
    virtual void sample();

public:
    virtual int64_t get_send_bytes();
    virtual int64_t get_recv_bytes();
};

class SrsNetworkKbps
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    SrsNetworkDelta *delta_;
    SrsKbps *kbps_;

public:
    SrsNetworkKbps(ISrsClock *c = NULL);
    virtual ~SrsNetworkKbps();

public:
    virtual void set_io(ISrsProtocolStatistic *in, ISrsProtocolStatistic *out);
    virtual void sample();

public:
    virtual int get_send_kbps();
    virtual int get_recv_kbps();
    virtual int get_send_kbps_30s();
    virtual int get_recv_kbps_30s();
    virtual int get_send_kbps_5m();
    virtual int get_recv_kbps_5m();

public:
    virtual int64_t get_send_bytes();
    virtual int64_t get_recv_bytes();
};

#endif
