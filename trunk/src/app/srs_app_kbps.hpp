/*
The MIT License (MIT)

Copyright (c) 2013-2014 winlin

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#ifndef SRS_APP_KBPS_HPP
#define SRS_APP_KBPS_HPP

/*
#include <srs_app_kbps.hpp>
*/

#include <srs_core.hpp>

#include <srs_protocol_io.hpp>

/**
* a kbps sample, for example, 1minute kbps, 
* 10minute kbps sample.
*/
class SrsKbpsSample
{
public:
    int64_t bytes;
    int64_t time;
    int kbps;
public:
    SrsKbpsSample();
};

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
    union slice_io {
        ISrsProtocolStatistic* in;
        ISrsProtocolStatistic* out;
    };
public:
    // the slice io used for SrsKbps to invoke,
    // the SrsKbpsSlice itself never use it.
    slice_io io;
    // session startup bytes
    // @remark, use total_bytes() to get the total bytes of slice.
    int64_t bytes;
    // slice starttime, the first time to record bytes.
    int64_t starttime;
    // session startup bytes number for io when set it,
    // the base offset of bytes for io.
    int64_t io_bytes_base;
    // last updated bytes number,
    // cache for io maybe freed.
    int64_t last_bytes;
    // samples
    SrsKbpsSample sample_30s;
    SrsKbpsSample sample_1m;
    SrsKbpsSample sample_5m;
    SrsKbpsSample sample_60m;
public:
    // for the delta bytes.
    int64_t delta_bytes;
public:
    SrsKbpsSlice();
    virtual ~SrsKbpsSlice();
public:
    /**
    * get current total bytes.
    */
    virtual int64_t get_total_bytes();
    /**
    * resample all samples.
    */
    virtual void sample();
};

/**
* the interface which provices delta of bytes.
*/
class IKbpsDelta
{
public:
    IKbpsDelta();
    virtual ~IKbpsDelta();
public:
    virtual int64_t get_send_bytes_delta() = 0;
    virtual int64_t get_recv_bytes_delta() = 0;
};

/**
* to statistic the kbps of io.
* itself can be a statistic source, for example, used for SRS bytes stat.
* there are two usage scenarios:
* 1. connections to calc kbps:
*       set_io(in, out)
*       sample()
*       get_xxx_kbps().
*   the connections know how many bytes already send/recv.
* 2. server to calc kbps:
*       set_io(NULL, NULL)
*       for each connection in connections:
*           add_delta(connections) // where connection is a IKbpsDelta*
*       sample()
*       get_xxx_kbps().
*   the server never know how many bytes already send/recv, for the connection maybe closed.
*/
class SrsKbps : public virtual ISrsProtocolStatistic, public virtual IKbpsDelta
{
private:
    SrsKbpsSlice is;
    SrsKbpsSlice os;
public:
    SrsKbps();
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
    * get the total send/recv bytes, from the startup of the oldest io.
    * @remark, use sample() to update data.
    */
    virtual int64_t get_send_bytes();
    virtual int64_t get_recv_bytes();
    /**
    * get the delta of send/recv bytes.
    * @remark, used for add_delta to calc the total system bytes/kbps.
    */
    virtual int64_t get_send_bytes_delta();
    virtual int64_t get_recv_bytes_delta();
public:
    /**
    * add delta to kbps clac mechenism.
    * we donot know the total bytes, but know the delta, for instance, 
    * for rtmp server to calc total bytes and kbps.
    * @remark user must invoke sample() when invoke this method.
    * @param delta, assert should never be NULL.
    */
    virtual void add_delta(IKbpsDelta* delta);
    /**
    * resample all samples, ignore if in/out is NULL.
    * used for user to calc the kbps, to sample new kbps value.
    * @remark if user, for instance, the rtmp server to calc the total bytes,
    *       use the add_delta() is better solutions.
    */
    virtual void sample();
};

#endif