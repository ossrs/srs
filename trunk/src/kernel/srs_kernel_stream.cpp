//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#include <srs_kernel_stream.hpp>

#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_core_performance.hpp>

SrsSimpleStream::SrsSimpleStream()
{
}

SrsSimpleStream::~SrsSimpleStream()
{
}

int SrsSimpleStream::length()
{
    int len = (int)data.size();
    srs_assert(len >= 0);
    return len;
}

char* SrsSimpleStream::bytes()
{
    return (length() == 0)? NULL : &data.at(0);
}

void SrsSimpleStream::erase(int size)
{
    if (size <= 0) {
        return;
    }
    
    if (size >= length()) {
        data.clear();
        return;
    }
    
    data.erase(data.begin(), data.begin() + size);
}

void SrsSimpleStream::append(const char* bytes, int size)
{
    if (size > 0) {
        data.insert(data.end(), bytes, bytes + size);
    }
}

void SrsSimpleStream::append(SrsSimpleStream* src)
{
    append(src->bytes(), src->length());
}
