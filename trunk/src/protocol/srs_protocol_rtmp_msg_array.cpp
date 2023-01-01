//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#include <srs_protocol_rtmp_msg_array.hpp>

#include <srs_protocol_rtmp_stack.hpp>

SrsMessageArray::SrsMessageArray(int max_msgs)
{
    srs_assert(max_msgs > 0);
    
    msgs = new SrsSharedPtrMessage*[max_msgs];
    max = max_msgs;
    
    zero(max_msgs);
}

SrsMessageArray::~SrsMessageArray()
{
    // we just free the msgs itself,
    // both delete and delete[] is ok,
    // for all msgs is already freed by send_and_free_messages.
    srs_freepa(msgs);
}

void SrsMessageArray::free(int count)
{
    // initialize
    for (int i = 0; i < count; i++) {
        SrsSharedPtrMessage* msg = msgs[i];
        srs_freep(msg);
        
        msgs[i] = NULL;
    }
}

void SrsMessageArray::zero(int count)
{
    // initialize
    for (int i = 0; i < count; i++) {
        msgs[i] = NULL;
    }
}


