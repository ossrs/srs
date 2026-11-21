//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_PROTOCOL_IO_HPP
#define SRS_PROTOCOL_IO_HPP

#include <srs_core.hpp>

#include <srs_kernel_io.hpp>

class ISrsProtocolReader : public ISrsReader, virtual public ISrsProtocolStatistic
{
public:
    ISrsProtocolReader();
    virtual ~ISrsProtocolReader();

public:
    // Use SRS_UTIME_NO_TIMEOUT to never time out.
    virtual void set_recv_timeout(srs_utime_t tm) = 0;
    virtual srs_utime_t get_recv_timeout() = 0;

public:
    // The nread may be NULL.
    virtual srs_error_t read_fully(void *buf, size_t size, ssize_t *nread) = 0;
};

class ISrsProtocolWriter : public ISrsWriter, virtual public ISrsProtocolStatistic
{
public:
    ISrsProtocolWriter();
    virtual ~ISrsProtocolWriter();

public:
    // Use SRS_UTIME_NO_TIMEOUT to never time out.
    virtual void set_send_timeout(srs_utime_t tm) = 0;
    virtual srs_utime_t get_send_timeout() = 0;
};

class ISrsProtocolReadWriter : public ISrsProtocolReader, public ISrsProtocolWriter
{
public:
    ISrsProtocolReadWriter();
    virtual ~ISrsProtocolReadWriter();
};

#endif
