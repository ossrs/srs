//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_KERNEL_STREAM_HPP
#define SRS_KERNEL_STREAM_HPP

#include <srs_core.hpp>

#include <vector>

class SrsSimpleStream
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    std::vector<char> data_;

public:
    SrsSimpleStream();
    virtual ~SrsSimpleStream();

public:
    virtual int length();
    virtual char *bytes();
    virtual void erase(int size);
    virtual void append(const char *bytes, int size);
    virtual void append(SrsSimpleStream *src);
};

#endif
