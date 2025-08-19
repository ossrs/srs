/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2020 Haivision Systems Inc.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 */

#ifndef INC_SRT_BUFFER_RCV_H
#define INC_SRT_BUFFER_RCV_H

#include "buffer_tools.h" // AvgBufSize
#include "common.h"
#include "queue.h"
#include "tsbpd_time.h"

namespace srt
{

/*
 *   Circular receiver buffer.
 *
 *   |<------------------- m_szSize ---------------------------->|
 *   |       |<------------ m_iMaxPosOff ----------->|           |
 *   |       |                                       |           |
 *   +---+---+---+---+---+---+---+---+---+---+---+---+---+   +---+
 *   | 0 | 0 | 1 | 1 | 1 | 0 | 1 | 1 | 1 | 1 | 0 | 1 | 0 |...| 0 | m_pUnit[]
 *   +---+---+---+---+---+---+---+---+---+---+---+---+---+   +---+
 *             |                                   |
 *             |                                   \__last pkt received
 *             |
 *             \___ m_iStartPos: first message to read
 *
 *   m_pUnit[i]->status_: 0: free, 1: good, 2: read, 3: dropped (can be combined with read?)
 *
 *   thread safety:
 *    start_pos_:      CUDT::m_RecvLock
 *    first_unack_pos_:    CUDT::m_AckLock
 *    max_pos_inc_:        none? (modified on add and ack
 *    first_nonread_pos_:
 */

class CRcvBuffer
{
    typedef sync::steady_clock::time_point time_point;
    typedef sync::steady_clock::duration   duration;

public:
    CRcvBuffer(int initSeqNo, size_t size, CUnitQueue* unitqueue, bool bMessageAPI);

    ~CRcvBuffer();

public:
    /// Insert a unit into the buffer.
    /// Similar to CRcvBuffer::addData(CUnit* unit, int offset)
    ///
    /// @param [in] unit pointer to a data unit containing new packet
    /// @param [in] offset offset from last ACK point.
    ///
    /// @return  0 on success, -1 if packet is already in buffer, -2 if packet is before m_iStartSeqNo.
    /// -3 if a packet is offset is ahead the buffer capacity.
    // TODO: Previously '-2' also meant 'already acknowledged'. Check usage of this value.
    int insert(CUnit* unit);

    /// Drop packets in the receiver buffer from the current position up to the seqno (excluding seqno).
    /// @param [in] seqno drop units up to this sequence number
    /// @return  number of dropped packets.
    int dropUpTo(int32_t seqno);

    /// @brief Drop all the packets in the receiver buffer.
    /// The starting position and seqno are shifted right after the last packet in the buffer.
    /// @return the number of dropped packets.
    int dropAll();

    enum DropActionIfExists {
        DROP_EXISTING = 0,
        KEEP_EXISTING = 1
    };

    /// @brief Drop a sequence of packets from the buffer.
    /// If @a msgno is valid, sender has requested to drop the whole message by TTL. In this case it has to also provide a pkt seqno range.
    /// However, if a message has been partially acknowledged and already removed from the SND buffer,
    /// the @a seqnolo might specify some position in the middle of the message, not the very first packet.
    /// If those packets have been acknowledged, they must exist in the receiver buffer unless already read.
    /// In this case the @a msgno should be used to determine starting packets of the message.
    /// Some packets of the message can be missing on the receiver, therefore the actual drop should still be performed by pkt seqno range.
    /// If message number is 0 or SRT_MSGNO_NONE, then use sequence numbers to locate sequence range to drop [seqnolo, seqnohi].
    /// A SOLO message packet can be kept depending on @a actionOnExisting value.
    /// TODO: A message in general can be kept if all of its packets are in the buffer, depending on @a actionOnExisting value.
    /// This is done to avoid dropping existing packet when the sender was asked to re-transmit a packet from an outdated loss report,
    /// which is already not available in the SND buffer.
    /// @param seqnolo sequence number of the first packet in the dropping range.
    /// @param seqnohi sequence number of the last packet in the dropping range.
    /// @param msgno message number to drop (0 if unknown)
    /// @param actionOnExisting Should an exising SOLO packet be dropped from the buffer or preserved?
    /// @return the number of packets actually dropped.
    int dropMessage(int32_t seqnolo, int32_t seqnohi, int32_t msgno, DropActionIfExists actionOnExisting);

    /// Read the whole message from one or several packets.
    ///
    /// @param [in,out] data buffer to write the message into.
    /// @param [in] len size of the buffer.
    /// @param [in,out] message control data
    ///
    /// @return actual number of bytes extracted from the buffer.
    ///          0 if nothing to read.
    ///         -1 on failure.
    int readMessage(char* data, size_t len, SRT_MSGCTRL* msgctrl = NULL);

    /// Read acknowledged data into a user buffer.
    /// @param [in, out] dst pointer to the target user buffer.
    /// @param [in] len length of user buffer.
    /// @return size of data read. -1 on error.
    int readBuffer(char* dst, int len);

    /// Read acknowledged data directly into file.
    /// @param [in] ofs C++ file stream.
    /// @param [in] len expected length of data to write into the file.
    /// @return size of data read. -1 on error.
    int readBufferToFile(std::fstream& ofs, int len);

public:
    /// Get the starting position of the buffer as a packet sequence number.
    int getStartSeqNo() const { return m_iStartSeqNo; }

    /// Sets the start seqno of the buffer.
    /// Must be used with caution and only when the buffer is empty.
    void setStartSeqNo(int seqno) { m_iStartSeqNo = seqno; }

    /// Given the sequence number of the first unacknowledged packet
    /// tells the size of the buffer available for packets.
    /// Effective returns capacity of the buffer minus acknowledged packet still kept in it.
    // TODO: Maybe does not need to return minus one slot now to distinguish full and empty buffer.
    size_t getAvailSize(int iFirstUnackSeqNo) const
    {
        // Receiver buffer allows reading unacknowledged packets.
        // Therefore if the first packet in the buffer is ahead of the iFirstUnackSeqNo
        // then it does not have acknowledged packets and its full capacity is available.
        // Otherwise subtract the number of acknowledged but not yet read packets from its capacity.
        const int iRBufSeqNo  = getStartSeqNo();
        if (CSeqNo::seqcmp(iRBufSeqNo, iFirstUnackSeqNo) >= 0) // iRBufSeqNo >= iFirstUnackSeqNo
        {
            // Full capacity is available.
            return capacity();
        }

        // Note: CSeqNo::seqlen(n, n) returns 1.
        return capacity() - CSeqNo::seqlen(iRBufSeqNo, iFirstUnackSeqNo) + 1;
    }

    /// @brief Checks if the buffer has packets available for reading regardless of the TSBPD.
    /// A message is available for reading only if all of its packets are present in the buffer.
    /// @return true if there are packets available for reading, false otherwise.
    bool hasAvailablePackets() const;

    /// Query how many data has been continuously received (for reading) and available for reading out
    /// regardless of the TSBPD.
    /// TODO: Rename to countAvailablePackets().
    /// @return size of valid (continuous) data for reading.
    int getRcvDataSize() const;

    /// Get the number of packets, bytes and buffer timespan.
    /// Differs from getRcvDataSize() that it counts all packets in the buffer, not only continious.
    int getRcvDataSize(int& bytes, int& timespan) const;

    struct PacketInfo
    {
        int        seqno;
        bool       seq_gap; //< true if there are missing packets in the buffer, preceding current packet
        time_point tsbpd_time;
    };

    /// Get information on the 1st message in queue.
    /// Similar to CRcvBuffer::getRcvFirstMsg
    /// Parameters (of the 1st packet queue, ready to play or not):
    /// @param [out] tsbpdtime localtime-based (uSec) packet time stamp including buffering delay of 1st packet or 0 if
    /// none
    /// @param [out] passack   true if 1st ready packet is not yet acknowledged (allowed to be delivered to the app)
    /// @param [out] skipseqno -1 or sequence number of 1st unacknowledged packet (after one or more missing packets) that is ready to play.
    /// @retval true 1st packet ready to play (tsbpdtime <= now). Not yet acknowledged if passack == true
    /// @retval false IF tsbpdtime = 0: rcv buffer empty; ELSE:
    ///                   IF skipseqno != -1, packet ready to play preceded by missing packets.;
    ///                   IF skipseqno == -1, no missing packet but 1st not ready to play.
    PacketInfo getFirstValidPacketInfo() const;

    PacketInfo getFirstReadablePacketInfo(time_point time_now) const;

    /// Get information on packets available to be read.
    /// @returns a pair of sequence numbers (first available; first unavailable).
    /// 
    /// @note CSeqNo::seqoff(first, second) is 0 if nothing to read.
    std::pair<int, int> getAvailablePacketsRange() const;

    size_t countReadable() const;

    bool empty() const
    {
        return (m_iMaxPosOff == 0);
    }

    /// Return buffer capacity.
    /// One slot had to be empty in order to tell the difference between "empty buffer" and "full buffer".
    /// E.g. m_iFirstNonreadPos would again point to m_iStartPos if m_szSize entries are added continiously.
    /// TODO: Old receiver buffer capacity returns the actual size. Check for conflicts.
    size_t capacity() const
    {
        return m_szSize - 1;
    }

    int64_t getDrift() const { return m_tsbpd.drift(); }

    // TODO: make thread safe?
    int debugGetSize() const
    {
        return getRcvDataSize();
    }

    /// Zero time to include all available packets.
    /// TODO: Rename to 'canRead`.
    bool isRcvDataReady(time_point time_now = time_point()) const;

    int  getRcvAvgDataSize(int& bytes, int& timespan);
    void updRcvAvgDataSize(const time_point& now);

    unsigned getRcvAvgPayloadSize() const { return m_uAvgPayloadSz; }

    void getInternalTimeBase(time_point& w_timebase, bool& w_wrp, duration& w_udrift)
    {
        return m_tsbpd.getInternalTimeBase(w_timebase, w_wrp, w_udrift);
    }

public: // Used for testing
    /// Peek unit in position of seqno
    const CUnit* peek(int32_t seqno);

private:
    inline int incPos(int pos, int inc = 1) const { return (pos + inc) % m_szSize; }
    inline int decPos(int pos) const { return (pos - 1) >= 0 ? (pos - 1) : int(m_szSize - 1); }
    inline int offPos(int pos1, int pos2) const { return (pos2 >= pos1) ? (pos2 - pos1) : int(m_szSize + pos2 - pos1); }
    inline int cmpPos(int pos2, int pos1) const
    {
        // XXX maybe not the best implementation, but this keeps up to the rule
        const int off1 = pos1 >= m_iStartPos ? pos1 - m_iStartPos : pos1 + (int)m_szSize - m_iStartPos;
        const int off2 = pos2 >= m_iStartPos ? pos2 - m_iStartPos : pos2 + (int)m_szSize - m_iStartPos;

        return off2 - off1;
    }

    // NOTE: Assumes that pUnit != NULL
    CPacket& packetAt(int pos) { return m_entries[pos].pUnit->m_Packet; }
    const CPacket& packetAt(int pos) const { return m_entries[pos].pUnit->m_Packet; }

private:
    void countBytes(int pkts, int bytes);
    void updateNonreadPos();
    void releaseUnitInPos(int pos);

    /// @brief Drop a unit from the buffer.
    /// @param pos position in the m_entries of the unit to drop.
    /// @return false if nothing to drop, true if the unit was dropped successfully.
    bool dropUnitInPos(int pos);

    /// Release entries following the current buffer position if they were already
    /// read out of order (EntryState_Read) or dropped (EntryState_Drop).
    void releaseNextFillerEntries();

    bool hasReadableInorderPkts() const { return (m_iFirstNonreadPos != m_iStartPos); }

    /// Find position of the last packet of the message.
    int findLastMessagePkt();

    /// Scan for availability of out of order packets.
    void onInsertNotInOrderPacket(int insertpos);
    // Check if m_iFirstReadableOutOfOrder is still readable.
    bool checkFirstReadableOutOfOrder();
    void updateFirstReadableOutOfOrder();
    int  scanNotInOrderMessageRight(int startPos, int msgNo) const;
    int  scanNotInOrderMessageLeft(int startPos, int msgNo) const;

    typedef bool copy_to_dst_f(char* data, int len, int dst_offset, void* arg);

    /// Read acknowledged data directly into file.
    /// @param [in] ofs C++ file stream.
    /// @param [in] len expected length of data to write into the file.
    /// @return size of data read.
    int readBufferTo(int len, copy_to_dst_f funcCopyToDst, void* arg);

    /// @brief Estimate timespan of the stored packets (acknowledged and unacknowledged).
    /// @return timespan in milliseconds
    int getTimespan_ms() const;

private:
    // TODO: Call makeUnitTaken upon assignment, and makeUnitFree upon clearing.
    // TODO: CUnitPtr is not in use at the moment, but may be a smart pointer.
    // class CUnitPtr
    // {
    // public:
    //     void operator=(CUnit* pUnit)
    //     {
    //         if (m_pUnit != NULL)
    //         {
    //             // m_pUnitQueue->makeUnitFree(m_entries[i].pUnit);
    //         }
    //         m_pUnit = pUnit;
    //     }
    // private:
    //     CUnit* m_pUnit;
    // };

    enum EntryStatus
    {
        EntryState_Empty,   //< No CUnit record.
        EntryState_Avail,   //< Entry is available for reading.
        EntryState_Read,    //< Entry has already been read (out of order).
        EntryState_Drop     //< Entry has been dropped.
    };
    struct Entry
    {
        Entry()
            : pUnit(NULL)
            , status(EntryState_Empty)
        {}

        CUnit*      pUnit;
        EntryStatus status;
    };

    //static Entry emptyEntry() { return Entry { NULL, EntryState_Empty }; }

    FixedArray<Entry> m_entries;

    const size_t m_szSize;     // size of the array of units (buffer)
    CUnitQueue*  m_pUnitQueue; // the shared unit queue

    int m_iStartSeqNo;
    int m_iStartPos;        // the head position for I/O (inclusive)
    int m_iFirstNonreadPos; // First position that can't be read (<= m_iLastAckPos)
    int m_iMaxPosOff;       // the furthest data position
    int m_iNotch;           // the starting read point of the first unit

    size_t m_numOutOfOrderPackets;  // The number of stored packets with "inorder" flag set to false
    int m_iFirstReadableOutOfOrder; // In case of out ouf order packet, points to a position of the first such packet to
                                    // read
    bool m_bPeerRexmitFlag;         // Needed to read message number correctly
    const bool m_bMessageAPI;       // Operation mode flag: message or stream.

public: // TSBPD public functions
    /// Set TimeStamp-Based Packet Delivery Rx Mode
    /// @param [in] timebase localtime base (uSec) of packet time stamps including buffering delay
    /// @param [in] wrap Is in wrapping period
    /// @param [in] delay agreed TsbPD delay
    ///
    /// @return 0
    void setTsbPdMode(const time_point& timebase, bool wrap, duration delay);

    void setPeerRexmitFlag(bool flag) { m_bPeerRexmitFlag = flag; } 

    void applyGroupTime(const time_point& timebase, bool wrp, uint32_t delay, const duration& udrift);

    void applyGroupDrift(const time_point& timebase, bool wrp, const duration& udrift);

    bool addRcvTsbPdDriftSample(uint32_t usTimestamp, const time_point& tsPktArrival, int usRTTSample);

    time_point getPktTsbPdTime(uint32_t usPktTimestamp) const;

    time_point getTsbPdTimeBase(uint32_t usPktTimestamp) const;
    void       updateTsbPdTimeBase(uint32_t usPktTimestamp);

    bool isTsbPd() const { return m_tsbpd.isEnabled(); }

    /// Form a string of the current buffer fullness state.
    /// number of packets acknowledged, TSBPD readiness, etc.
    std::string strFullnessState(int iFirstUnackSeqNo, const time_point& tsNow) const;

private:
    CTsbpdTime  m_tsbpd;

private: // Statistics
    AvgBufSize m_mavg;

    // TODO: m_BytesCountLock is probably not needed as the buffer has to be protected from simultaneous access.
    mutable sync::Mutex m_BytesCountLock;   // used to protect counters operations
    int         m_iBytesCount;      // Number of payload bytes in the buffer
    int         m_iPktsCount;       // Number of payload bytes in the buffer
    unsigned    m_uAvgPayloadSz;    // Average payload size for dropped bytes estimation
};

} // namespace srt

#endif // INC_SRT_BUFFER_RCV_H
