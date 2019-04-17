/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2019 Winlin
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef SRS_PROTOCOL_IO_HPP
#define SRS_PROTOCOL_IO_HPP

#include <srs_core.hpp>

#include <srs_kernel_io.hpp>

/**
 * The system io reader/writer architecture:
 *                                         +---------------+  +---------------+
 *                                         | IStreamWriter |  | IVectorWriter |
 *                                         +---------------+  +---------------+
 *                                         | + write()     |  | + writev()    |
 *                                         +-------------+-+  ++--------------+
 * +----------+     +--------------------+               /\   /\
 * | IReader  |     |    IStatistic      |                 \ /
 * +----------+     +--------------------+                  V
 * | + read() |     | + get_recv_bytes() |           +------+----+
 * +------+---+     | + get_send_bytes() |           |  IWriter  |
 *       / \        +---+--------------+-+           +-------+---+
 *        |            / \            / \                   / \
 *        |             |              |                     |
 * +------+-------------+------+      ++---------------------+--+
 * | IProtocolReader           |      | IProtocolWriter         |
 * +---------------------------+      +-------------------------+
 * | + readfully()             |      | + set_send_timeout()    |
 * | + set_recv_timeout()      |      +-------+-----------------+
 * +------------+--------------+             / \
 *             / \                            |
 *              |                             |
 *           +--+-----------------------------+-+
 *           |       IProtocolReadWriter        |
 *           +----------------------------------+
 *           | + is_never_timeout()             |
 *           +----------------------------------+
 */

/**
 * Get the statistic of channel.
 */
class ISrsProtocolStatistic
{
public:
    ISrsProtocolStatistic();
    virtual ~ISrsProtocolStatistic();
// For protocol
public:
    // Get the total recv bytes over underlay fd.
    virtual int64_t get_recv_bytes() = 0;
    // Get the total send bytes over underlay fd.
    virtual int64_t get_send_bytes() = 0;
};

/**
 * the reader for the protocol to read from whatever channel.
 */
class ISrsProtocolReader : virtual public ISrsReader, virtual public ISrsProtocolStatistic
{
public:
    ISrsProtocolReader();
    virtual ~ISrsProtocolReader();
// for protocol
public:
    // Set the timeout tm in ms for recv bytes from peer.
    // @remark Use SRS_UTIME_NO_TIMEOUT to never timeout.
    virtual void set_recv_timeout(int64_t tm) = 0;
    // Get the timeout in ms for recv bytes from peer.
    virtual int64_t get_recv_timeout() = 0;
// For handshake.
public:
    // Read specified size bytes of data
    // @param nread, the actually read size, NULL to ignore.
    virtual srs_error_t read_fully(void* buf, size_t size, ssize_t* nread) = 0;
};

/**
 * the writer for the protocol to write to whatever channel.
 */
class ISrsProtocolWriter : virtual public ISrsWriter, virtual public ISrsProtocolStatistic
{
public:
    ISrsProtocolWriter();
    virtual ~ISrsProtocolWriter();
// For protocol
public:
    // Set the timeout tm in srs_utime_t for send bytes to peer.
    // @remark Use SRS_UTIME_NO_TIMEOUT to never timeout.
    virtual void set_send_timeout(srs_utime_t tm) = 0;
    // Get the timeout in srs_utime_t for send bytes to peer.
    virtual srs_utime_t get_send_timeout() = 0;
};

/**
 * The reader and writer.
 */
class ISrsProtocolReadWriter : virtual public ISrsProtocolReader, virtual public ISrsProtocolWriter
{
public:
    ISrsProtocolReadWriter();
    virtual ~ISrsProtocolReadWriter();
// For protocol
public:
    // Whether the specified tm in ms is never timeout.
    virtual bool is_never_timeout(int64_t tm) = 0;
};

#endif

