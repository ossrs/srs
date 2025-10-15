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

class ISrsProtocolStatistic;

/**
 * Interface for clock abstraction to provide wall clock time.
 * This interface enables dependency injection and testability.
 */
class ISrsClock
{
public:
    ISrsClock();
    virtual ~ISrsClock();

public:
    /**
     * Current time in srs_utime_t.
     */
    virtual srs_utime_t now() = 0;
};

/**
 * A time source to provide wall clock.
 */
class SrsWallClock : public ISrsClock
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

// A sample for rate-based stat, such as kbps or kps.
class SrsRateSample
{
public:
    int64_t total_;
    srs_utime_t time_;
    // kbps or kps
    int rate_;

public:
    SrsRateSample();
    virtual ~SrsRateSample();

public:
    virtual SrsRateSample *update(int64_t nn, srs_utime_t t, int k);
};

// A pps manager every some duration.
class SrsPps
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsClock *clk_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // samples
    SrsRateSample sample_10s_;
    SrsRateSample sample_30s_;
    SrsRateSample sample_1m_;
    SrsRateSample sample_5m_;
    SrsRateSample sample_60m_;

public:
    // Sugar for target to stat.
    int64_t sugar_;

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

// Update the global kbps statistics variables
void srs_global_kbps_update(SrsKbpsStats *stats);

class SrsKbsRtcStats
{
public:
    std::string rpkts_desc_;
    std::string spkts_desc_;
    std::string rtcp_desc_;
    std::string snk_desc_;
    std::string rnk_desc_;
    std::string fid_desc_;
};

// Update the global rtc statistics variables
void srs_global_rtc_update(SrsKbsRtcStats *stats);

/**
 * The slice of kbps statistic, for input or output.
 */
class SrsKbpsSlice
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsClock *clk_;

public:
    // session startup bytes
    // @remark, use total_bytes() to get the total bytes of slice.
    int64_t bytes_;
    // slice starttime, the first time to record bytes.
    srs_utime_t starttime_;
    // samples
    SrsRateSample sample_30s_;
    SrsRateSample sample_1m_;
    SrsRateSample sample_5m_;
    SrsRateSample sample_60m_;

public:
    SrsKbpsSlice(ISrsClock *c);
    virtual ~SrsKbpsSlice();

public:
    // Resample the slice to calculate the kbps.
    virtual void sample();
};

/**
 * The interface which provices delta of bytes. For example, we got a delta from a TCP client:
 *       ISrsKbpsDelta* delta = ...;
 * Now, we can add delta simple to a kbps:
 *      kbps->add_delta(delta);
 * Or by multiple kbps:
 *       int64_t in, out;
 *       delta->remark(&in, &out);
 *       kbps1->add_delta(in, out);
 *       kbpsN->add_delta(in, out);
 * Then you're able to use the kbps object.
 */
class ISrsKbpsDelta
{
public:
    ISrsKbpsDelta();
    virtual ~ISrsKbpsDelta();

public:
    // Resample to get the value of delta bytes.
    // @remark If no delta bytes, both in and out will be set to 0.
    virtual void remark(int64_t *in, int64_t *out) = 0;
};

// The interface which provices delta of bytes. For example, we got a delta from a UDP client:
class ISrsEphemeralDelta : public ISrsKbpsDelta
{
public:
    ISrsEphemeralDelta();
    virtual ~ISrsEphemeralDelta();

public:
    virtual void add_delta(int64_t in, int64_t out) = 0;
};

// A delta data source for SrsKbps, used in ephemeral case, for example, UDP server to increase stat when received or
// sent out each UDP packet.
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
    // Interface ISrsKbpsDelta.
public:
    virtual void remark(int64_t *in, int64_t *out);
};

// The interface which provices delta of bytes. For example, we got a delta from a TCP client:
class ISrsNetworkDelta : public ISrsKbpsDelta
{
public:
    ISrsNetworkDelta();
    virtual ~ISrsNetworkDelta();

public:
    virtual void set_io(ISrsProtocolStatistic *in, ISrsProtocolStatistic *out) = 0;
};

// A network delta data source for SrsKbps.
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
    // Switch the under-layer network io, we use the bytes as a fresh delta.
    virtual void set_io(ISrsProtocolStatistic *in, ISrsProtocolStatistic *out);
    // Interface ISrsKbpsDelta.
public:
    virtual void remark(int64_t *in, int64_t *out);
};

/**
 * To statistic the kbps. For example, we got a set of connections and add the total delta:
 *       SrsKbps* kbps = ...;
 *       for conn in connections:
 *           kbps->add_delta(conn->delta()) // Which return an ISrsKbpsDelta object.
 * Then we sample and got the total kbps:
 *       kbps->sample()
 *       kbps->get_xxx_kbps().
 */
class SrsKbps
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    SrsKbpsSlice *is_;
    SrsKbpsSlice *os_;
    ISrsClock *clk_;

public:
    // Note that we won't free the clock c.
    SrsKbps(ISrsClock *c = NULL);
    virtual ~SrsKbps();

public:
    // Get total average kbps.
    virtual int get_send_kbps();
    virtual int get_recv_kbps();
    // Get the average kbps in 30s.
    virtual int get_send_kbps_30s();
    virtual int get_recv_kbps_30s();
    // Get the average kbps in 5m or 300s.
    virtual int get_send_kbps_5m();
    virtual int get_recv_kbps_5m();

public:
    // Add delta to kbps. Please call sample() after all deltas are added to kbps.
    virtual void add_delta(int64_t in, int64_t out);
    virtual void add_delta(ISrsKbpsDelta *delta);
    // Sample the kbps to get the kbps in N seconds.
    virtual void sample();

public:
    virtual int64_t get_send_bytes();
    virtual int64_t get_recv_bytes();
};

// A sugar to use SrsNetworkDelta and SrsKbps.
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
