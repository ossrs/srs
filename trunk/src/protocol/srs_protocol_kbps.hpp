//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#ifndef SRS_PROTOCOL_KBPS_HPP
#define SRS_PROTOCOL_KBPS_HPP

#include <srs_core.hpp>

#include <srs_protocol_io.hpp>
#include <srs_kernel_kbps.hpp>

/**
 * The slice of kbps statistic, for input or output.
 */
class SrsKbpsSlice
{
private:
    SrsWallClock* clk;
public:
    // session startup bytes
    // @remark, use total_bytes() to get the total bytes of slice.
    int64_t bytes;
    // slice starttime, the first time to record bytes.
    srs_utime_t starttime;
    // samples
    SrsRateSample sample_30s;
    SrsRateSample sample_1m;
    SrsRateSample sample_5m;
    SrsRateSample sample_60m;
public:
    SrsKbpsSlice(SrsWallClock* clk);
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
    virtual void remark(int64_t* in, int64_t* out) = 0;
};

// A delta data source for SrsKbps, used in ephemeral case, for example, UDP server to increase stat when received or
// sent out each UDP packet.
class SrsEphemeralDelta : public ISrsKbpsDelta
{
private:
    uint64_t in_;
    uint64_t out_;
public:
    SrsEphemeralDelta();
    virtual ~SrsEphemeralDelta();
public:
    virtual void add_delta(int64_t in, int64_t out);
// Interface ISrsKbpsDelta.
public:
    virtual void remark(int64_t* in, int64_t* out);
};

// A network delta data source for SrsKbps.
class SrsNetworkDelta : public ISrsKbpsDelta
{
private:
    ISrsProtocolStatistic* in_;
    ISrsProtocolStatistic* out_;
    uint64_t in_base_;
    uint64_t in_delta_;
    uint64_t out_base_;
    uint64_t out_delta_;
public:
    SrsNetworkDelta();
    virtual ~SrsNetworkDelta();
public:
    // Switch the under-layer network io, we use the bytes as a fresh delta.
    virtual void set_io(ISrsProtocolStatistic* in, ISrsProtocolStatistic* out);
// Interface ISrsKbpsDelta.
public:
    virtual void remark(int64_t* in, int64_t* out);
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
private:
    SrsKbpsSlice* is;
    SrsKbpsSlice* os;
    SrsWallClock* clk;
public:
    // Note that we won't free the clock c.
    SrsKbps(SrsWallClock* c = NULL);
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
    virtual void add_delta(ISrsKbpsDelta* delta);
    // Sample the kbps to get the kbps in N seconds.
    virtual void sample();
public:
    virtual int64_t get_send_bytes();
    virtual int64_t get_recv_bytes();
};

// A sugar to use SrsNetworkDelta and SrsKbps.
class SrsNetworkKbps
{
private:
    SrsNetworkDelta* delta_;
    SrsKbps* kbps_;
public:
    SrsNetworkKbps(SrsWallClock* c = NULL);
    virtual ~SrsNetworkKbps();
public:
    virtual void set_io(ISrsProtocolStatistic* in, ISrsProtocolStatistic* out);
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
