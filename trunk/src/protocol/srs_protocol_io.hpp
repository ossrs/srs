/*
The MIT License (MIT)

Copyright (c) 2013-2017 SRS(ossrs)

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

#ifndef SRS_PROTOCOL_IO_HPP
#define SRS_PROTOCOL_IO_HPP

/*
#include <srs_protocol_io.hpp>
*/

#include <srs_core.hpp>

// for srs-librtmp, @see https://github.com/ossrs/srs/issues/213
#ifndef _WIN32
#include <sys/uio.h>
#endif

/**
* the system io reader/writer architecture:
+---------------+     +--------------------+      +---------------+
| IBufferReader |     |    IStatistic      |      | IBufferWriter |
+---------------+     +--------------------+      +---------------+
| + read()      |     | + get_recv_bytes() |      | + write()     |
+------+--------+     | + get_send_bytes() |      | + writev()    |
      / \             +---+--------------+-+      +-------+-------+
       |                 / \            / \              / \
       |                  |              |                |
+------+------------------+-+      +-----+----------------+--+
| IProtocolReader           |      | IProtocolWriter         |
+---------------------------+      +-------------------------+
| + readfully()             |      | + set_send_timeout()    |
| + set_recv_timeout()      |      +-------+-----------------+
+------------+--------------+             / \     
            / \                            |   
             |                             | 
          +--+-----------------------------+-+
          |       IProtocolReaderWriter      |
          +----------------------------------+
          | + is_never_timeout()             |
          +----------------------------------+
*/

/**
* the reader for the buffer to read from whatever channel.
*/
class ISrsBufferReader
{
public:
    ISrsBufferReader();
    virtual ~ISrsBufferReader();
// for protocol/amf0/msg-codec
public:
    virtual int read(void* buf, size_t size, ssize_t* nread) = 0;
};

/**
* the writer for the buffer to write to whatever channel.
*/
class ISrsBufferWriter
{
public:
    ISrsBufferWriter();
    virtual ~ISrsBufferWriter();
// for protocol
public:
    /**
    * write bytes over writer.
    * @nwrite the actual written bytes. NULL to ignore.
    */
    virtual int write(void* buf, size_t size, ssize_t* nwrite) = 0;
    /**
    * write iov over writer.
    * @nwrite the actual written bytes. NULL to ignore.
    */
    virtual int writev(const iovec *iov, int iov_size, ssize_t* nwrite) = 0;
};

/**
* get the statistic of channel.
*/
class ISrsProtocolStatistic
{
public:
    ISrsProtocolStatistic();
    virtual ~ISrsProtocolStatistic();
// for protocol
public:
    /**
    * get the total recv bytes over underlay fd.
    */
    virtual int64_t get_recv_bytes() = 0;
    /**
    * get the total send bytes over underlay fd.
    */
    virtual int64_t get_send_bytes() = 0;
};

/**
* the reader for the protocol to read from whatever channel.
*/
class ISrsProtocolReader : public virtual ISrsBufferReader, public virtual ISrsProtocolStatistic
{
public:
    ISrsProtocolReader();
    virtual ~ISrsProtocolReader();
// for protocol
public:
    /**
    * set the recv timeout in us, recv will error when timeout.
    * @remark, if not set, use ST_UTIME_NO_TIMEOUT, never timeout.
    */
    virtual void set_recv_timeout(int64_t timeout_us) = 0;
    /**
    * get the recv timeout in us.
    */
    virtual int64_t get_recv_timeout() = 0;
// for handshake.
public:
    /**
    * read specified size bytes of data
    * @param nread, the actually read size, NULL to ignore.
    */
    virtual int read_fully(void* buf, size_t size, ssize_t* nread) = 0;
};

/**
* the writer for the protocol to write to whatever channel.
*/
class ISrsProtocolWriter : public virtual ISrsBufferWriter, public virtual ISrsProtocolStatistic
{
public:
    ISrsProtocolWriter();
    virtual ~ISrsProtocolWriter();
// for protocol
public:
    /**
    * set the send timeout in us, send will error when timeout.
    * @remark, if not set, use ST_UTIME_NO_TIMEOUT, never timeout.
    */
    virtual void set_send_timeout(int64_t timeout_us) = 0;
    /**
    * get the send timeout in us.
    */
    virtual int64_t get_send_timeout() = 0;
};

/**
* the reader and writer.
*/
class ISrsProtocolReaderWriter : public virtual ISrsProtocolReader, public virtual ISrsProtocolWriter
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
};

#endif

