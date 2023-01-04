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
#include <algorithm>
#include <sstream>

#include "group_backup.h"


namespace srt
{
namespace groups
{

using namespace std;
using namespace srt_logging;

const char* stateToStr(BackupMemberState state)
{
    switch (state)
    {
    case srt::groups::BKUPST_UNKNOWN:
        return "UNKNOWN";
    case srt::groups::BKUPST_PENDING:
        return "PENDING";
    case srt::groups::BKUPST_STANDBY:
        return "STANDBY";
    case srt::groups::BKUPST_ACTIVE_FRESH:
        return "ACTIVE_FRESH";
    case srt::groups::BKUPST_ACTIVE_STABLE:
        return "ACTIVE_STABLE";
    case srt::groups::BKUPST_ACTIVE_UNSTABLE:
        return "ACTIVE_UNSTABLE";
    case srt::groups::BKUPST_ACTIVE_UNSTABLE_WARY:
        return "ACTIVE_UNSTABLE_WARY";
    case srt::groups::BKUPST_BROKEN:
        return "BROKEN";
    default:
        break;
    }

    return "WRONG_STATE";
}

/// @brief Compares group members by their weight (higher weight comes first), then state.
/// Higher weight comes first, same weight: stable, then fresh active.
struct FCompareByWeight
{
    /// @returns true if the first argument is less than (i.e. is ordered before) the second.
    bool operator()(const BackupMemberStateEntry& a, const BackupMemberStateEntry& b)
    {
        if (a.pSocketData != NULL && b.pSocketData != NULL
            && (a.pSocketData->weight != b.pSocketData->weight))
            return a.pSocketData->weight > b.pSocketData->weight;

        if (a.state != b.state)
        {
            SRT_STATIC_ASSERT(BKUPST_ACTIVE_STABLE > BKUPST_ACTIVE_FRESH, "Wrong ordering");
            return a.state > b.state;
        }

        // the order does not matter, but comparator must return a different value for not equal a and b
        return a.socketID < b.socketID;
    }
};

void SendBackupCtx::recordMemberState(SocketData* pSockData, BackupMemberState st)
{
    m_memberStates.push_back(BackupMemberStateEntry(pSockData, st));
    ++m_stateCounter[st];

    if (st == BKUPST_STANDBY)
    {
        m_standbyMaxWeight = max(m_standbyMaxWeight, pSockData->weight);
    }
    else if (isStateActive(st))
    {
        m_activeMaxWeight = max(m_activeMaxWeight, pSockData->weight);
    }
}

void SendBackupCtx::updateMemberState(const SocketData* pSockData, BackupMemberState st)
{
    typedef vector<BackupMemberStateEntry>::iterator iter_t;
    for (iter_t i = m_memberStates.begin(); i != m_memberStates.end(); ++i)
    {
        if (i->pSocketData == NULL)
            continue;

        if (i->pSocketData != pSockData)
            continue;

        if (i->state == st)
            return;

        --m_stateCounter[i->state];
        ++m_stateCounter[st];
        i->state = st;

        return;
    }


    LOGC(gslog.Error,
        log << "IPE: SendBackupCtx::updateMemberState failed to locate member");
}

void SendBackupCtx::sortByWeightAndState()
{
    sort(m_memberStates.begin(), m_memberStates.end(), FCompareByWeight());
}

BackupMemberState SendBackupCtx::getMemberState(const SocketData* pSockData) const
{
    typedef vector<BackupMemberStateEntry>::const_iterator const_iter_t;
    for (const_iter_t i = m_memberStates.begin(); i != m_memberStates.end(); ++i)
    {
        if (i->pSocketData != pSockData)
            continue;

        return i->state;
    }

    // The entry was not found
    // TODO: Maybe throw an exception here?
    return BKUPST_UNKNOWN;
}

unsigned SendBackupCtx::countMembersByState(BackupMemberState st) const
{
    return m_stateCounter[st];
}

std::string SendBackupCtx::printMembers() const
{
    stringstream ss;
    typedef vector<BackupMemberStateEntry>::const_iterator const_iter_t;
    for (const_iter_t i = m_memberStates.begin(); i != m_memberStates.end(); ++i)
    {
        ss << "@" << i->socketID << " w " << i->pSocketData->weight << " state " << stateToStr(i->state) << ", ";
    }
    return ss.str();
}

} // namespace groups
} // namespace srt
