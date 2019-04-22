/*
The MIT License (MIT)

Copyright (c) 2013-2019 Winlin

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

#ifndef SRS_UTEST_PROTOCOL_HPP
#define SRS_UTEST_PROTOCOL_HPP

/*
#include <srs_utest_protocol.hpp>
*/
#include <srs_utest.hpp>

#include <string>
#include <srs_protocol_utility.hpp>

#include <srs_rtmp_stack.hpp>
#include <srs_rtmp_handshake.hpp>
#include <srs_protocol_stream.hpp>
#include <srs_protocol_kbps.hpp>

using namespace _srs_internal;

#include <srs_protocol_io.hpp>

class MockEmptyIO : public ISrsProtocolReadWriter
{
public:
    MockEmptyIO();
    virtual ~MockEmptyIO();
// for protocol
public:
    virtual bool is_never_timeout(srs_utime_t tm);
// for handshake.
public:
    virtual srs_error_t read_fully(void* buf, size_t size, ssize_t* nread);
    virtual srs_error_t write(void* buf, size_t size, ssize_t* nwrite);
// for protocol
public:
    virtual void set_recv_timeout(srs_utime_t tm);
    virtual srs_utime_t get_recv_timeout();
    virtual int64_t get_recv_bytes();
// for protocol
public:
    virtual void set_send_timeout(srs_utime_t tm);
    virtual srs_utime_t get_send_timeout();
    virtual int64_t get_send_bytes();
    virtual srs_error_t writev(const iovec *iov, int iov_size, ssize_t* nwrite);
// for protocol/amf0/msg-codec
public:
    virtual srs_error_t read(void* buf, size_t size, ssize_t* nread);
};

class MockBufferIO : public ISrsProtocolReadWriter
{
public:
    // The send/recv timeout in srs_utime_t.
    srs_utime_t rtm;
    srs_utime_t stm;
    // The send/recv data in bytes.
    int64_t rbytes;
    int64_t sbytes;
    // data source for socket read.
    SrsSimpleStream in_buffer;
    // data buffer for socket send.
    SrsSimpleStream out_buffer;
public:
    MockBufferIO();
    virtual ~MockBufferIO();
public:
    virtual MockBufferIO* append(std::string data);
// for protocol
public:
    virtual bool is_never_timeout(srs_utime_t tm);
// for handshake.
public:
    virtual srs_error_t read_fully(void* buf, size_t size, ssize_t* nread);
    virtual srs_error_t write(void* buf, size_t size, ssize_t* nwrite);
// for protocol
public:
    virtual void set_recv_timeout(srs_utime_t tm);
    virtual srs_utime_t get_recv_timeout();
    virtual int64_t get_recv_bytes();
// for protocol
public:
    virtual void set_send_timeout(srs_utime_t tm);
    virtual srs_utime_t get_send_timeout();
    virtual int64_t get_send_bytes();
    virtual srs_error_t writev(const iovec *iov, int iov_size, ssize_t* nwrite);
// for protocol/amf0/msg-codec
public:
    virtual srs_error_t read(void* buf, size_t size, ssize_t* nread);
};

class MockStatistic : public ISrsProtocolStatistic
{
private:
    int64_t in;
    int64_t out;
public:
    MockStatistic();
    virtual ~MockStatistic();
public:
    virtual int64_t get_recv_bytes();
    virtual int64_t get_send_bytes();
public:
    MockStatistic* set_in(int64_t v);
    MockStatistic* set_out(int64_t v);
    MockStatistic* add_in(int64_t v);
    MockStatistic* add_out(int64_t v);
};

class MockWallClock : public SrsWallClock
{
private:
    int64_t clock;
public:
    MockWallClock();
    virtual ~MockWallClock();
public:
    virtual srs_utime_t now();
public:
    virtual MockWallClock* set_clock(srs_utime_t v);
};

#endif

