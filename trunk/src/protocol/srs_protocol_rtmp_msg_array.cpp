//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_protocol_rtmp_msg_array.hpp>

#include <srs_protocol_rtmp_stack.hpp>

SrsMessageArray::SrsMessageArray(int max_msgs)
{
    srs_assert(max_msgs > 0);

    msgs_ = new SrsMediaPacket *[max_msgs];
    max_ = max_msgs;

    zero(max_msgs);
}

SrsMessageArray::~SrsMessageArray()
{
    // we just free the msgs itself,
    // both delete and delete[] is ok,
    // for all msgs is already freed by send_and_free_messages.
    srs_freepa(msgs_);
}

void SrsMessageArray::free(int count)
{
    // initialize
    for (int i = 0; i < count; i++) {
        SrsMediaPacket *msg = msgs_[i];
        srs_freep(msg);

        msgs_[i] = NULL;
    }
}

void SrsMessageArray::zero(int count)
{
    // initialize
    for (int i = 0; i < count; i++) {
        msgs_[i] = NULL;
    }
}
