//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_PROTOCOL_MESSAGE_ARRAY_HPP
#define SRS_PROTOCOL_MESSAGE_ARRAY_HPP

#include <srs_core.hpp>

class SrsMediaPacket;

// The array never frees its messages; the user frees them, such as by
// send_and_free_messages().
class SrsMessageArray
{
public:
    SrsMediaPacket **msgs_;
    int max_;

public:
    SrsMessageArray(int max_msgs);
    virtual ~SrsMessageArray();

public:
    virtual void free(int count);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual void zero(int count);
};

#endif
