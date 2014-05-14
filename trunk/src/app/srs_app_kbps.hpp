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

class ISrsProtocolReader;
class ISrsProtocolWriter;

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
* @remark user use set_io to start new session.
*/
class SrsKbpsSlice
{
private:
    union slice_io {
        ISrsProtocolReader* in;
        ISrsProtocolWriter* out;
    };
public:
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
* to statistic the kbps of io.
*/
class SrsKbps
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
    virtual void set_io(ISrsProtocolReader* in, ISrsProtocolWriter* out);
public:
    /**
    * get total kbps, duration is from the startup of io.
    * @remark, use sample() to update data.
    */
    virtual int get_send_kbps();
    virtual int get_recv_kbps();
    // 30s
    virtual int get_send_kbps_sample_high();
    virtual int get_recv_kbps_sample_high();
    // 5m
    virtual int get_send_kbps_sample_medium();
    virtual int get_recv_kbps_sample_medium();
public:
    /**
    * get the total send/recv bytes, from the startup of the oldest io.
    * @remark, use sample() to update data.
    */
    virtual int64_t get_send_bytes();
    virtual int64_t get_recv_bytes();
public:
    /**
    * resample all samples.
    */
    virtual void sample();
};

#endif