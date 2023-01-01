//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

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
class ISrsProtocolReader : public ISrsReader, virtual public ISrsProtocolStatistic
{
public:
    ISrsProtocolReader();
    virtual ~ISrsProtocolReader();
// for protocol
public:
    // Set the timeout tm in srs_utime_t for recv bytes from peer.
    // @remark Use SRS_UTIME_NO_TIMEOUT to never timeout.
    virtual void set_recv_timeout(srs_utime_t tm) = 0;
    // Get the timeout in srs_utime_t for recv bytes from peer.
    virtual srs_utime_t get_recv_timeout() = 0;
// For handshake.
public:
    // Read specified size bytes of data
    // @param nread, the actually read size, NULL to ignore.
    virtual srs_error_t read_fully(void* buf, size_t size, ssize_t* nread) = 0;
};

/**
 * the writer for the protocol to write to whatever channel.
 */
class ISrsProtocolWriter : public ISrsWriter, virtual public ISrsProtocolStatistic
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
class ISrsProtocolReadWriter : public ISrsProtocolReader, public ISrsProtocolWriter
{
public:
    ISrsProtocolReadWriter();
    virtual ~ISrsProtocolReadWriter();
};

#endif

