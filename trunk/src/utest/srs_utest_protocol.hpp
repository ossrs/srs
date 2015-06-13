/*
The MIT License (MIT)

Copyright (c) 2013-2015 SRS(simple-rtmp-server)

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
#include <srs_rtmp_utility.hpp>

#include <srs_rtmp_stack.hpp>
#include <srs_rtmp_handshake.hpp>
#include <srs_protocol_buffer.hpp>

#ifdef SRS_AUTO_SSL
using namespace _srs_internal;
#endif

#include <srs_rtmp_io.hpp>

class MockEmptyIO : public ISrsProtocolReaderWriter
{
public:
    MockEmptyIO();
    virtual ~MockEmptyIO();
// for protocol
public:
    virtual bool is_never_timeout(int64_t timeout_us);
// for handshake.
public:
    virtual int read_fully(void* buf, size_t size, ssize_t* nread);
    virtual int write(void* buf, size_t size, ssize_t* nwrite);
// for protocol
public:
    virtual void set_recv_timeout(int64_t timeout_us);
    virtual int64_t get_recv_timeout();
    virtual int64_t get_recv_bytes();
// for protocol
public:
    virtual void set_send_timeout(int64_t timeout_us);
    virtual int64_t get_send_timeout();
    virtual int64_t get_send_bytes();
    virtual int writev(const iovec *iov, int iov_size, ssize_t* nwrite);
// for protocol/amf0/msg-codec
public:
    virtual int read(void* buf, size_t size, ssize_t* nread);
};

class MockBufferIO : public ISrsProtocolReaderWriter
{
public:
    int64_t recv_timeout;
    int64_t send_timeout;
    int64_t recv_bytes;
    int64_t send_bytes;
    // data source for socket read.
    SrsSimpleBuffer in_buffer;
    // data buffer for socket send.
    SrsSimpleBuffer out_buffer;
public:
    MockBufferIO();
    virtual ~MockBufferIO();
// for protocol
public:
    virtual bool is_never_timeout(int64_t timeout_us);
// for handshake.
public:
    virtual int read_fully(void* buf, size_t size, ssize_t* nread);
    virtual int write(void* buf, size_t size, ssize_t* nwrite);
// for protocol
public:
    virtual void set_recv_timeout(int64_t timeout_us);
    virtual int64_t get_recv_timeout();
    virtual int64_t get_recv_bytes();
// for protocol
public:
    virtual void set_send_timeout(int64_t timeout_us);
    virtual int64_t get_send_timeout();
    virtual int64_t get_send_bytes();
    virtual int writev(const iovec *iov, int iov_size, ssize_t* nwrite);
// for protocol/amf0/msg-codec
public:
    virtual int read(void* buf, size_t size, ssize_t* nread);
};

#endif

