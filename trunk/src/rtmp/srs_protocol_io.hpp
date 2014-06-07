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

#ifndef SRS_RTMP_PROTOCOL_IO_HPP
#define SRS_RTMP_PROTOCOL_IO_HPP

/*
#include <srs_protocol_io.hpp>
*/

#include <srs_core.hpp>

#include <sys/uio.h>

#include <srs_kernel_buffer.hpp>

/**
* the reader for the protocol to read from whatever channel.
*/
class ISrsProtocolReader : public ISrsBufferReader
{
public:
    ISrsProtocolReader();
    virtual ~ISrsProtocolReader();
// for protocol
public:
    virtual void set_recv_timeout(int64_t timeout_us) = 0;
    virtual int64_t get_recv_timeout() = 0;
    virtual int64_t get_recv_bytes() = 0;
};

/**
* the writer for the protocol to write to whatever channel.
*/
class ISrsProtocolWriter
{
public:
    ISrsProtocolWriter();
    virtual ~ISrsProtocolWriter();
// for protocol
public:
    virtual void set_send_timeout(int64_t timeout_us) = 0;
    virtual int64_t get_send_timeout() = 0;
    virtual int64_t get_send_bytes() = 0;
    virtual int writev(const iovec *iov, int iov_size, ssize_t* nwrite) = 0;
};

class ISrsProtocolReaderWriter : public ISrsProtocolReader, public ISrsProtocolWriter
{
public:
    ISrsProtocolReaderWriter();
    virtual ~ISrsProtocolReaderWriter();
// for protocol
public:
    /**
    * whether the specified timeout_us is never timeout.
    */
    virtual bool is_never_timeout(int64_t timeout_us) = 0;
// for handshake.
public:
    virtual int read_fully(void* buf, size_t size, ssize_t* nread) = 0;
    virtual int write(void* buf, size_t size, ssize_t* nwrite) = 0;
};

#endif
