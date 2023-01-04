/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2021 Haivision Systems Inc.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 */

/*****************************************************************************
Written by
   Haivision Systems Inc.
*****************************************************************************/

#include "platform_sys.h"

#include "group_common.h"
#include "api.h"

namespace srt
{
namespace groups
{

SocketData prepareSocketData(CUDTSocket* s)
{
    // This uses default SRT_GST_BROKEN because when the group operation is done,
    // then the SRT_GST_IDLE state automatically turns into SRT_GST_RUNNING. This is
    // recognized as an initial state of the fresh added socket to the group,
    // so some "initial configuration" must be done on it, after which it's
    // turned into SRT_GST_RUNNING, that is, it's treated as all others. When
    // set to SRT_GST_BROKEN, this socket is disregarded. This socket isn't cleaned
    // up, however, unless the status is simultaneously SRTS_BROKEN.

    // The order of operations is then:
    // - add the socket to the group in this "broken" initial state
    // - connect the socket (or get it extracted from accept)
    // - update the socket state (should be SRTS_CONNECTED)
    // - once the connection is established (may take time with connect), set SRT_GST_IDLE
    // - the next operation of send/recv will automatically turn it into SRT_GST_RUNNING
    SocketData sd = {
        s->m_SocketID,
        s,
        -1,
        SRTS_INIT,
        SRT_GST_BROKEN,
        SRT_GST_BROKEN,
        -1,
        -1,
        sockaddr_any(),
        sockaddr_any(),
        false,
        false,
        false,
        0, // weight
        0  // pktSndDropTotal
    };
    return sd;
}

} // namespace groups
} // namespace srt
