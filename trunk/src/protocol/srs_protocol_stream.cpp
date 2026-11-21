//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_protocol_stream.hpp>

#include <stdlib.h>

#include <srs_core_performance.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_kernel_utility.hpp>

#define SRS_DEFAULT_RECV_BUFFER_SIZE 131072

// 256KB, for 3Mbps stream delivery: 800*2000/8=200000B (about 195KB).
// Higher bitrates work too, since one chunk fits in 256KB.
#define SRS_MAX_SOCKET_BUFFER 262144

#define SRS_RTMP_MAX_MESSAGE_HEADER 11

#ifdef SRS_PERF_MERGED_READ
IMergeReadHandler::IMergeReadHandler()
{
}

IMergeReadHandler::~IMergeReadHandler()
{
}
#endif

SrsFastStream::SrsFastStream(int size)
{
#ifdef SRS_PERF_MERGED_READ
    merged_read_ = false;
    handler_ = NULL;
#endif

    nb_buffer_ = size ? size : SRS_DEFAULT_RECV_BUFFER_SIZE;
    buffer_ = (char *)malloc(nb_buffer_);
    p_ = end_ = buffer_;
}

SrsFastStream::~SrsFastStream()
{
    free(buffer_);
    buffer_ = NULL;
}

int SrsFastStream::size()
{
    return (int)(end_ - p_);
}

char *SrsFastStream::bytes()
{
    return p_;
}

void SrsFastStream::set_buffer(int buffer_size)
{
    if (buffer_size > SRS_MAX_SOCKET_BUFFER) {
        srs_warn("limit buffer size %d to %d", buffer_size, SRS_MAX_SOCKET_BUFFER);
    }

    int nb_resize_buf = srs_min(buffer_size, SRS_MAX_SOCKET_BUFFER);

    if (nb_resize_buf <= nb_buffer_) {
        return;
    }

    int start = (int)(p_ - buffer_);
    int nb_bytes = (int)(end_ - p_);

    buffer_ = (char *)realloc(buffer_, nb_resize_buf);
    nb_buffer_ = nb_resize_buf;
    p_ = buffer_ + start;
    end_ = p_ + nb_bytes;
}

char SrsFastStream::read_1byte()
{
    srs_assert(end_ - p_ >= 1);
    return *p_++;
}

char *SrsFastStream::read_slice(int size)
{
    srs_assert(size >= 0);
    srs_assert(end_ - p_ >= size);
    srs_assert(p_ + size >= buffer_);

    char *ptr = p_;
    p_ += size;

    return ptr;
}

void SrsFastStream::skip(int size)
{
    srs_assert(end_ - p_ >= size);
    srs_assert(p_ + size >= buffer_);
    p_ += size;
}

srs_error_t SrsFastStream::grow(ISrsReader *reader, int required_size)
{
    srs_error_t err = srs_success;

    if (end_ - p_ >= required_size) {
        return err;
    }

    srs_assert(required_size > 0);

    int nb_free_space = (int)(buffer_ + nb_buffer_ - end_);

    int nb_exists_bytes = (int)(end_ - p_);
    srs_assert(nb_exists_bytes >= 0);

    if (nb_exists_bytes + nb_free_space < required_size) {
        if (!nb_exists_bytes) {
            p_ = end_ = buffer_;
        } else if (nb_exists_bytes < nb_buffer_ && p_ > buffer_) {
            // A full buffer is not moved, so the check below returns an error; it used to crash.
            // @see https://github.com/ossrs/srs/issues/848
            buffer_ = (char *)memmove(buffer_, p_, nb_exists_bytes);
            p_ = buffer_;
            end_ = p_ + nb_exists_bytes;
        }

        nb_free_space = (int)(buffer_ + nb_buffer_ - end_);
        if (nb_exists_bytes + nb_free_space < required_size) {
            return srs_error_new(ERROR_READER_BUFFER_OVERFLOW, "overflow, required=%d, max=%d, left=%d", required_size, nb_buffer_, nb_free_space);
        }
    }

    while (end_ - p_ < required_size) {
        ssize_t nread;
        if ((err = reader->read(end_, nb_free_space, &nread)) != srs_success) {
            return srs_error_wrap(err, "read bytes");
        }

#ifdef SRS_PERF_MERGED_READ
        if (merged_read_ && handler_) {
            handler_->on_read(nread);
        }
#endif

        srs_assert((int)nread > 0);
        end_ += nread;
        nb_free_space -= (int)nread;
    }

    return err;
}

#ifdef SRS_PERF_MERGED_READ
void SrsFastStream::set_merge_read(bool v, IMergeReadHandler *handler)
{
    merged_read_ = v;
    handler_ = handler;
}
#endif
