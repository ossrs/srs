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

#ifndef INC_SRT_GROUP_BACKUP_H
#define INC_SRT_GROUP_BACKUP_H

#include "srt.h"
#include "common.h"
#include "group_common.h"

#include <list>

namespace srt
{
namespace groups
{
    enum BackupMemberState
    {
        BKUPST_UNKNOWN = -1,

        BKUPST_PENDING = 0,
        BKUPST_STANDBY = 1,
        BKUPST_BROKEN  = 2,

        BKUPST_ACTIVE_UNSTABLE = 3,
        BKUPST_ACTIVE_UNSTABLE_WARY = 4,
        BKUPST_ACTIVE_FRESH = 5,
        BKUPST_ACTIVE_STABLE = 6,

        BKUPST_E_SIZE = 7
    };

    const char* stateToStr(BackupMemberState state);

    inline bool isStateActive(BackupMemberState state)
    {
        if (state == BKUPST_ACTIVE_FRESH
            || state == BKUPST_ACTIVE_STABLE
            || state == BKUPST_ACTIVE_UNSTABLE
            || state == BKUPST_ACTIVE_UNSTABLE_WARY)
        {
            return true;
        }

        return false;
    }

    struct BackupMemberStateEntry
    {
        BackupMemberStateEntry(SocketData* psock, BackupMemberState st)
            : pSocketData(psock)
            , socketID(psock->id)
            , state(st)
        {}

        SocketData* pSocketData; // accessing pSocketDataIt requires m_GroupLock
        SRTSOCKET socketID;  // therefore socketID is saved separately (needed to close broken sockets)
        BackupMemberState state;
    };

    /// @brief A context needed for main/backup sending function.
    /// @todo Using gli_t here does not allow to safely store the context outside of the sendBackup calls.
    class SendBackupCtx
    {
    public:
        SendBackupCtx()
            : m_stateCounter() // default init with zeros
            , m_activeMaxWeight()
            , m_standbyMaxWeight()
        {
        }

        /// @brief  Adds or updates a record of the member socket state.
        /// @param pSocketDataIt Iterator to a socket
        /// @param st State of the memmber socket
        /// @todo Implement updating member state
        void recordMemberState(SocketData* pSocketDataIt, BackupMemberState st);

        /// @brief  Updates a record of the member socket state.
        /// @param pSocketDataIt Iterator to a socket
        /// @param st State of the memmber socket
        /// @todo To be replaced by recordMemberState
        /// @todo Update max weights?
        void updateMemberState(const SocketData* pSocketDataIt, BackupMemberState st);

        /// @brief sorts members in order
        /// Higher weight comes first, same weight: stable first, then fresh active.
        void sortByWeightAndState();

        BackupMemberState getMemberState(const SocketData* pSocketDataIt) const;

        unsigned countMembersByState(BackupMemberState st) const;

        const std::vector<BackupMemberStateEntry>& memberStates() const { return m_memberStates; }

        uint16_t maxStandbyWeight() const { return m_standbyMaxWeight; }
        uint16_t maxActiveWeight() const { return m_activeMaxWeight; }

        std::string printMembers() const;

        void setRateEstimate(const CRateEstimator& rate) { m_rateEstimate = rate; }

        const CRateEstimator& getRateEstimate() const { return m_rateEstimate; }

    private:
        std::vector<BackupMemberStateEntry> m_memberStates; // TODO: consider std::map here?
        unsigned m_stateCounter[BKUPST_E_SIZE];
        uint16_t m_activeMaxWeight;
        uint16_t m_standbyMaxWeight;
        CRateEstimator m_rateEstimate; // The rate estimator state of the active link to copy to a backup on activation.
    };

} // namespace groups
} // namespace srt

#endif // INC_SRT_GROUP_BACKUP_H
