//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_PROTOCOL_STREAM_HPP
#define SRS_PROTOCOL_STREAM_HPP

#include <srs_core.hpp>

#include <srs_core_performance.hpp>
#include <srs_kernel_stream.hpp>
#include <srs_protocol_io.hpp>

#ifdef SRS_PERF_MERGED_READ
// Merged read: when a read gets only a few bytes, the handler sleeps to wait for more data,
// so the next read gets more bytes at once.
// @see https://github.com/ossrs/srs/issues/241
class IMergeReadHandler
{
public:
    IMergeReadHandler();
    virtual ~IMergeReadHandler();

public:
    virtual void on_read(ssize_t nread) = 0;
};
#endif

// TODO: FIXME: add utest for it.
class SrsFastStream
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
#ifdef SRS_PERF_MERGED_READ
    bool merged_read_;
    IMergeReadHandler *handler_;
#endif
    // buffer_ <= p_ <= end_ <= buffer_ + nb_buffer_, where p_ is the read position and end_ the content end.
    char *p_;
    char *end_;
    char *buffer_;
    int nb_buffer_;

public:
    SrsFastStream(int size = 0);
    virtual ~SrsFastStream();

public:
    virtual int size();
    virtual char *bytes();
    virtual void set_buffer(int buffer_size);

public:
    virtual char read_1byte();
    // The returned pointer is valid only until the next grow() or set_buffer(), which may move the buffer.
    virtual char *read_slice(int size);
    virtual void skip(int size);

public:
    virtual srs_error_t grow(ISrsReader *reader, int required_size);

public:
#ifdef SRS_PERF_MERGED_READ
    virtual void set_merge_read(bool v, IMergeReadHandler *handler);
#endif
};

#endif
