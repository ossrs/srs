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

#ifndef INC_SRT_GROUP_COMMON_H
#define INC_SRT_GROUP_COMMON_H

#include "srt.h"
#include "common.h"
#include "core.h"

#include <list>

namespace srt
{
namespace groups
{
    typedef SRT_MEMBERSTATUS GroupState;

    struct SocketData
    {
        SRTSOCKET      id; // same as ps->m_SocketID
        CUDTSocket*    ps;
        int            token;
        SRT_SOCKSTATUS laststatus;
        GroupState     sndstate;
        GroupState     rcvstate;
        int            sndresult;
        int            rcvresult;
        sockaddr_any   agent;
        sockaddr_any   peer;
        bool           ready_read;
        bool           ready_write;
        bool           ready_error;

        // Configuration
        uint16_t       weight;

        // Stats
        int64_t        pktSndDropTotal;
    };

    SocketData prepareSocketData(CUDTSocket* s);

    typedef std::list<SocketData> group_t;
    typedef group_t::iterator     gli_t;

} // namespace groups
} // namespace srt

#endif // INC_SRT_GROUP_COMMON_H
