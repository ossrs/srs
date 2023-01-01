//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#ifndef SRS_PROTOCOL_MESSAGE_ARRAY_HPP
#define SRS_PROTOCOL_MESSAGE_ARRAY_HPP

#include <srs_core.hpp>

class SrsSharedPtrMessage;

// The class to auto free the shared ptr message array.
// When need to get some messages, for instance, from Consumer queue,
// create a message array, whose msgs can used to accept the msgs,
// then send each message and set to NULL.
//
// @remark: user must free all msgs in array, for the SRS2.0 protocol stack
//       provides an api to send messages, @see send_and_free_messages
class SrsMessageArray
{
public:
    // When user already send all msgs, please set to NULL,
    // for instance, msg= msgs.msgs[i], msgs.msgs[i]=NULL, send(msg),
    // where send(msg) will always send and free it.
    SrsSharedPtrMessage** msgs;
    int max;
public:
    // Create msg array, initialize array to NULL ptrs.
    SrsMessageArray(int max_msgs);
    // Free the msgs not sent out(not NULL).
    virtual ~SrsMessageArray();
public:
    // Free specified count of messages.
    virtual void free(int count);
private:
    // Zero initialize the message array.
    virtual void zero(int count);
};

#endif

