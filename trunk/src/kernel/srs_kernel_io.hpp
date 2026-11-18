//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_KERNEL_IO_HPP
#define SRS_KERNEL_IO_HPP

#include <srs_core.hpp>

// For srs-librtmp, @see https://github.com/ossrs/srs/issues/213
#ifndef _WIN32
#include <sys/uio.h>
#endif

class ISrsReader
{
public:
    ISrsReader();
    virtual ~ISrsReader();

public:
    // The nread may be NULL.
    virtual srs_error_t read(void *buf, size_t size, ssize_t *nread) = 0;
};

class ISrsSeeker
{
public:
    ISrsSeeker();
    virtual ~ISrsSeeker();

public:
    // Works like lseek(2) and gives the new offset from the start in seeked, which may be NULL.
    virtual srs_error_t lseek(srs_off_t offset, int whence, srs_off_t *seeked) = 0;
};

class ISrsReadSeeker : public ISrsReader, public ISrsSeeker
{
public:
    ISrsReadSeeker();
    virtual ~ISrsReadSeeker();
};

class ISrsStreamWriter
{
public:
    ISrsStreamWriter();
    virtual ~ISrsStreamWriter();

public:
    // The nwrite may be NULL.
    virtual srs_error_t write(void *buf, size_t size, ssize_t *nwrite) = 0;
};

class ISrsVectorWriter
{
public:
    ISrsVectorWriter();
    virtual ~ISrsVectorWriter();

public:
    // The nwrite may be NULL. HTTP FLV uses writev to improve performance.
    // @see https://github.com/ossrs/srs/issues/405
    virtual srs_error_t writev(const iovec *iov, int iov_size, ssize_t *nwrite) = 0;
};

class ISrsWriter : public ISrsStreamWriter, public ISrsVectorWriter
{
public:
    ISrsWriter();
    virtual ~ISrsWriter();
};

class ISrsWriteSeeker : public ISrsWriter, public ISrsSeeker
{
public:
    ISrsWriteSeeker();
    virtual ~ISrsWriteSeeker();
};

class ISrsProtocolStatistic
{
public:
    ISrsProtocolStatistic();
    virtual ~ISrsProtocolStatistic();

public:
    // The total bytes received and sent over the underlying fd.
    virtual int64_t get_recv_bytes() = 0;
    virtual int64_t get_send_bytes() = 0;
};

#endif
