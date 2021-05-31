//
// Copyright (c) 2013-2021 Winlin
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_PROTOCOL_KBPS_HPP
#define SRS_PROTOCOL_KBPS_HPP

#include <srs_core.hpp>

#include <srs_protocol_io.hpp>
#include <srs_kernel_kbps.hpp>

/**
 * a slice of kbps statistic, for input or output.
 * a slice contains a set of sessions, which has a base offset of bytes,
 * where a slice is:
 *       starttime(oldest session startup time)
 *               bytes(total bytes of previous sessions)
 *               io_bytes_base(bytes offset of current session)
 *                       last_bytes(bytes of current session)
 * so, the total send bytes now is:
 *       send_bytes = bytes + last_bytes - io_bytes_base
 * so, the bytes sent duration current session is:
 *       send_bytes = last_bytes - io_bytes_base
 * @remark use set_io to start new session.
 * @remakr the slice is a data collection object driven by SrsKbps.
 */
class SrsKbpsSlice
{
private:
    SrsWallClock* clk;
public:
    // the slice io used for SrsKbps to invoke,
    // the SrsKbpsSlice itself never use it.
    ISrsProtocolStatistic* io;
    // session startup bytes
    // @remark, use total_bytes() to get the total bytes of slice.
    int64_t bytes;
    // slice starttime, the first time to record bytes.
    srs_utime_t starttime;
    // session startup bytes number for io when set it,
    // the base offset of bytes for io.
    int64_t io_bytes_base;
    // last updated bytes number,
    // cache for io maybe freed.
    int64_t last_bytes;
    // samples
    SrsRateSample sample_30s;
    SrsRateSample sample_1m;
    SrsRateSample sample_5m;
    SrsRateSample sample_60m;
public:
    // for the delta bytes.
    int64_t delta_bytes;
public:
    SrsKbpsSlice(SrsWallClock* clk);
    virtual ~SrsKbpsSlice();
public:
    // Get current total bytes, it doesn't depend on sample().
    virtual int64_t get_total_bytes();
    // Resample the slice to calculate the kbps.
    virtual void sample();
};

/**
 * the interface which provices delta of bytes.
 * for a delta, for example, a live stream connection, we can got the delta by:
 *       ISrsKbpsDelta* delta = ...;
 *       int64_t in, out;
 *       delta->remark(&in, &out);
 *       kbps->add_delta(in, out);
 */
class ISrsKbpsDelta
{
public:
    ISrsKbpsDelta();
    virtual ~ISrsKbpsDelta();
public:
    /**
     * resample to generate the value of delta bytes.
     */
    virtual void remark(int64_t* in, int64_t* out) = 0;
};

/**
 * to statistic the kbps of io.
 * itself can be a statistic source, for example, used for SRS bytes stat.
 * there are some usage scenarios:
 * 1. connections to calc kbps by sample():
 *       SrsKbps* kbps = ...;
 *       kbps->set_io(in, out)
 *       kbps->sample()
 *       kbps->get_xxx_kbps().
 *   the connections know how many bytes already send/recv.
 * 2. server to calc kbps by add_delta():
 *       SrsKbps* kbps = ...;
 *       kbps->set_io(NULL, NULL)
 *       for each connection in connections:
 *           ISrsKbpsDelta* delta = connection; // where connection implements ISrsKbpsDelta
 *           int64_t in, out;
 *           delta->remark(&in, &out)
 *           kbps->add_delta(in, out)
 *       kbps->sample()
 *       kbps->get_xxx_kbps().
 * 3. kbps used as ISrsKbpsDelta, to provides delta bytes:
 *      SrsKbps* kbps = ...;
 *      kbps->set_io(in, out);
 *      ISrsKbpsDelta* delta = (ISrsKbpsDelta*)kbps;
 *      int64_t in, out;
 *      delta->remark(&in, out);
 *      printf("delta is %d/%d", in, out);
 * 4. kbps used as ISrsProtocolStatistic, to provides raw bytes:
 *      SrsKbps* kbps = ...;
 *      kbps->set_io(in, out);
 *      // both kbps->get_recv_bytes() and kbps->get_send_bytes() are available.
 *      // we can use the kbps as the data source of another kbps:
 *      SrsKbps* user = ...;
 *      user->set_io(kbps, kbps);
 *   the server never know how many bytes already send/recv, for the connection maybe closed.
 */
class SrsKbps : public ISrsProtocolStatistic, public ISrsKbpsDelta
{
private:
    SrsKbpsSlice is;
    SrsKbpsSlice os;
    SrsWallClock* clk;
public:
    // We won't free the clock c.
    SrsKbps(SrsWallClock* c);
    virtual ~SrsKbps();
public:
    /**
     * set io to start new session.
     * set the underlayer reader/writer,
     * if the io destroied, for instance, the forwarder reconnect,
     * user must set the io of SrsKbps to NULL to continue to use the kbps object.
     * @param in the input stream statistic. can be NULL.
     * @param out the output stream statistic. can be NULL.
     * @remark if in/out is NULL, use the cached data for kbps.
     * @remark User must set_io(NULL, NULL) then free the in and out.
     */
    virtual void set_io(ISrsProtocolStatistic* in, ISrsProtocolStatistic* out);
public:
    /**
     * get total kbps, duration is from the startup of io.
     * @remark, use sample() to update data.
     */
    virtual int get_send_kbps();
    virtual int get_recv_kbps();
    // 30s
    virtual int get_send_kbps_30s();
    virtual int get_recv_kbps_30s();
    // 5m
    virtual int get_send_kbps_5m();
    virtual int get_recv_kbps_5m();
public:
    /**
     * add delta to kbps clac mechenism.
     * we donot know the total bytes, but know the delta, for instance,
     * for rtmp server to calc total bytes and kbps.
     * @remark user must invoke sample() to calc result after invoke this method.
     * @param delta, assert should never be NULL.
     */
    virtual void add_delta(int64_t in, int64_t out);
    /**
     * resample all samples, ignore if in/out is NULL.
     * used for user to calc the kbps, to sample new kbps value.
     * @remark if user, for instance, the rtmp server to calc the total bytes,
     *       use the add_delta() is better solutions.
     */
    virtual void sample();
// Interface ISrsProtocolStatistic
public:
    virtual int64_t get_send_bytes();
    virtual int64_t get_recv_bytes();
// Interface ISrsKbpsDelta
public:
    virtual void remark(int64_t* in, int64_t* out);
// Interface ISrsMemorySizer
public:
    virtual int size_memory();
};

#endif
