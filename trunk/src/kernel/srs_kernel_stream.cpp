//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_kernel_stream.hpp>

#include <srs_core_performance.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_kernel_utility.hpp>

SrsSimpleStream::SrsSimpleStream()
{
}

SrsSimpleStream::~SrsSimpleStream()
{
}

int SrsSimpleStream::length()
{
    int len = (int)data_.size();
    srs_assert(len >= 0);
    return len;
}

char *SrsSimpleStream::bytes()
{
    return (length() == 0) ? NULL : &data_.at(0);
}

void SrsSimpleStream::erase(int size)
{
    if (size <= 0) {
        return;
    }

    if (size >= length()) {
        data_.clear();
        return;
    }

    data_.erase(data_.begin(), data_.begin() + size);
}

void SrsSimpleStream::append(const char *bytes, int size)
{
    if (size > 0) {
        data_.insert(data_.end(), bytes, bytes + size);
    }
}

void SrsSimpleStream::append(SrsSimpleStream *src)
{
    append(src->bytes(), src->length());
}
