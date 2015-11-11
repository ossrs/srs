/*
The MIT License (MIT)

Copyright (c) 2013-2015 SRS(ossrs)

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

#ifndef SRS_LIB_SIMPLE_SOCKET_HPP
#define SRS_LIB_SIMPLE_SOCKET_HPP

/*
#include <srs_lib_simple_socket.hpp>
*/

#include <srs_core.hpp>

#include <srs_protocol_io.hpp>
#include <srs_librtmp.hpp>

// for srs-librtmp, @see https://github.com/ossrs/srs/issues/213
#ifndef _WIN32
    #define SOCKET int
#endif

/**
* simple socket stream,
* use tcp socket, sync block mode, for client like srs-librtmp.
*/
class SimpleSocketStream : public ISrsProtocolReaderWriter
{
private:
    srs_hijack_io_t io;
public:
    SimpleSocketStream();
    virtual ~SimpleSocketStream();
public:
    virtual srs_hijack_io_t hijack_io();
    virtual int create_socket();
    virtual int connect(const char* server, int port);
// ISrsBufferReader
public:
    virtual int read(void* buf, size_t size, ssize_t* nread);
// ISrsProtocolReader
public:
    virtual void set_recv_timeout(int64_t timeout_us);
    virtual int64_t get_recv_timeout();
    virtual int64_t get_recv_bytes();
// ISrsProtocolWriter
public:
    virtual void set_send_timeout(int64_t timeout_us);
    virtual int64_t get_send_timeout();
    virtual int64_t get_send_bytes();
    virtual int writev(const iovec *iov, int iov_size, ssize_t* nwrite);
// ISrsProtocolReaderWriter
public:
    virtual bool is_never_timeout(int64_t timeout_us);
    virtual int read_fully(void* buf, size_t size, ssize_t* nread);
    virtual int write(void* buf, size_t size, ssize_t* nwrite);
};

#endif

