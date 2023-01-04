/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2018 Haivision Systems Inc.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 */

/*****************************************************************************
Copyright (c) 2001 - 2009, The Board of Trustees of the University of Illinois.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

* Redistributions of source code must retain the above
  copyright notice, this list of conditions and the
  following disclaimer.

* Redistributions in binary form must reproduce the
  above copyright notice, this list of conditions
  and the following disclaimer in the documentation
  and/or other materials provided with the distribution.

* Neither the name of the University of Illinois
  nor the names of its contributors may be used to
  endorse or promote products derived from this
  software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*****************************************************************************/

/*****************************************************************************
written by
   Yunhong Gu, last updated 05/05/2009
modified by
   Haivision Systems Inc.
*****************************************************************************/

#ifndef INC_SRT_BUFFER_H
#define INC_SRT_BUFFER_H

#include "udt.h"
#include "list.h"
#include "queue.h"
#include "tsbpd_time.h"
#include "utilities.h"

// The notation used for "circular numbers" in comments:
// The "cicrular numbers" are numbers that when increased up to the
// maximum become zero, and similarly, when the zero value is decreased,
// it turns into the maximum value minus one. This wrapping works the
// same for adding and subtracting. Circular numbers cannot be multiplied.

// Operations done on these numbers are marked with additional % character:
// a %> b : a is later than b
// a ++% (++%a) : shift a by 1 forward
// a +% b : shift a by b
// a == b : equality is same as for just numbers

namespace srt {

/// The AvgBufSize class is used to calculate moving average of the buffer (RCV or SND)
class AvgBufSize
{
    typedef sync::steady_clock::time_point time_point;

public:
    AvgBufSize()
        : m_dBytesCountMAvg(0.0)
        , m_dCountMAvg(0.0)
        , m_dTimespanMAvg(0.0)
    {
    }

public:
    bool isTimeToUpdate(const time_point& now) const;
    void update(const time_point& now, int pkts, int bytes, int timespan_ms);

public:
    inline double pkts() const { return m_dCountMAvg; }
    inline double timespan_ms() const { return m_dTimespanMAvg; }
    inline double bytes() const { return m_dBytesCountMAvg; }

private:
    time_point m_tsLastSamplingTime;
    double     m_dBytesCountMAvg;
    double     m_dCountMAvg;
    double     m_dTimespanMAvg;
};

/// The class to estimate source bitrate based on samples submitted to the buffer.
/// Is currently only used by the CSndBuffer.
class CRateEstimator
{
    typedef sync::steady_clock::time_point time_point;
    typedef sync::steady_clock::duration   duration;
public:
    CRateEstimator();

public:
    uint64_t getInRatePeriod() const { return m_InRatePeriod; }

    /// Retrieve input bitrate in bytes per second
    int getInputRate() const { return m_iInRateBps; }

    void setInputRateSmpPeriod(int period);

    /// Update input rate calculation.
    /// @param [in] time   current time in microseconds
    /// @param [in] pkts   number of packets newly added to the buffer
    /// @param [in] bytes  number of payload bytes in those newly added packets
    ///
    /// @return Current size of the data in the sending list.
    void updateInputRate(const time_point& time, int pkts = 0, int bytes = 0);

    void resetInputRateSmpPeriod(bool disable = false) { setInputRateSmpPeriod(disable ? 0 : INPUTRATE_FAST_START_US); }

private:                                                       // Constants
    static const uint64_t INPUTRATE_FAST_START_US   = 500000;  //  500 ms
    static const uint64_t INPUTRATE_RUNNING_US      = 1000000; // 1000 ms
    static const int64_t  INPUTRATE_MAX_PACKETS     = 2000;    // ~ 21 Mbps of 1316 bytes payload
    static const int      INPUTRATE_INITIAL_BYTESPS = BW_INFINITE;

private:
    int        m_iInRatePktsCount;  // number of payload bytes added since InRateStartTime
    int        m_iInRateBytesCount; // number of payload bytes added since InRateStartTime
    time_point m_tsInRateStartTime;
    uint64_t   m_InRatePeriod;  // usec
    int        m_iInRateBps;    // Input Rate in Bytes/sec
};

class CSndBuffer
{
    typedef sync::steady_clock::time_point time_point;
    typedef sync::steady_clock::duration   duration;

public:
    // XXX There's currently no way to access the socket ID set for
    // whatever the buffer is currently working for. Required to find
    // some way to do this, possibly by having a "reverse pointer".
    // Currently just "unimplemented".
    std::string CONID() const { return ""; }

    /// @brief CSndBuffer constructor.
    /// @param size initial number of blocks (each block to store one packet payload).
    /// @param maxpld maximum packet payload.
    CSndBuffer(int size = 32, int maxpld = 1500);
    ~CSndBuffer();

public:
    /// Insert a user buffer into the sending list.
    /// For @a w_mctrl the following fields are used:
    /// INPUT:
    /// - msgttl: timeout for retransmitting the message, if lost
    /// - inorder: request to deliver the message in order of sending
    /// - srctime: local time as a base for packet's timestamp (0 if unused)
    /// - pktseq: sequence number to be stamped on the packet (-1 if unused)
    /// - msgno: message number to be stamped on the packet (-1 if unused)
    /// OUTPUT:
    /// - srctime: local time stamped on the packet (same as input, if input wasn't 0)
    /// - pktseq: sequence number to be stamped on the next packet
    /// - msgno: message number stamped on the packet
    /// @param [in] data pointer to the user data block.
    /// @param [in] len size of the block.
    /// @param [inout] w_mctrl Message control data
    SRT_ATTR_EXCLUDES(m_BufLock)
    void addBuffer(const char* data, int len, SRT_MSGCTRL& w_mctrl);

    /// Read a block of data from file and insert it into the sending list.
    /// @param [in] ifs input file stream.
    /// @param [in] len size of the block.
    /// @return actual size of data added from the file.
    SRT_ATTR_EXCLUDES(m_BufLock)
    int addBufferFromFile(std::fstream& ifs, int len);

    /// Find data position to pack a DATA packet from the furthest reading point.
    /// @param [out] packet the packet to read.
    /// @param [out] origintime origin time stamp of the message
    /// @param [in] kflags Odd|Even crypto key flag
    /// @param [out] seqnoinc the number of packets skipped due to TTL, so that seqno should be incremented.
    /// @return Actual length of data read.
    SRT_ATTR_EXCLUDES(m_BufLock)
    int readData(CPacket& w_packet, time_point& w_origintime, int kflgs, int& w_seqnoinc);

    /// Peek an information on the next original data packet to send.
    /// @return origin time stamp of the next packet; epoch start time otherwise.
    SRT_ATTR_EXCLUDES(m_BufLock)
    time_point peekNextOriginal() const;

    /// Find data position to pack a DATA packet for a retransmission.
    /// @param [in] offset offset from the last ACK point (backward sequence number difference)
    /// @param [out] packet the packet to read.
    /// @param [out] origintime origin time stamp of the message
    /// @param [out] msglen length of the message
    /// @return Actual length of data read (return 0 if offset too large, -1 if TTL exceeded).
    SRT_ATTR_EXCLUDES(m_BufLock)
    int readData(const int offset, CPacket& w_packet, time_point& w_origintime, int& w_msglen);

    /// Get the time of the last retransmission (if any) of the DATA packet.
    /// @param [in] offset offset from the last ACK point (backward sequence number difference)
    ///
    /// @return Last time of the last retransmission event for the corresponding DATA packet.
    SRT_ATTR_EXCLUDES(m_BufLock)
    time_point getPacketRexmitTime(const int offset);

    /// Update the ACK point and may release/unmap/return the user data according to the flag.
    /// @param [in] offset number of packets acknowledged.
    int32_t getMsgNoAt(const int offset);

    void ackData(int offset);

    /// Read size of data still in the sending list.
    /// @return Current size of the data in the sending list.
    int getCurrBufSize() const;

    SRT_ATTR_EXCLUDES(m_BufLock)
    int dropLateData(int& bytes, int32_t& w_first_msgno, const time_point& too_late_time);

    void updAvgBufSize(const time_point& time);
    int  getAvgBufSize(int& bytes, int& timespan);
    int  getCurrBufSize(int& bytes, int& timespan);

    /// @brief Get the buffering delay of the oldest message in the buffer.
    /// @return the delay value.
    SRT_ATTR_EXCLUDES(m_BufLock)
    duration getBufferingDelay(const time_point& tnow) const;

    uint64_t getInRatePeriod() const { return m_rateEstimator.getInRatePeriod(); }

    /// Retrieve input bitrate in bytes per second
    int getInputRate() const { return m_rateEstimator.getInputRate(); }

    void resetInputRateSmpPeriod(bool disable = false) { m_rateEstimator.resetInputRateSmpPeriod(disable); }

    const CRateEstimator& getRateEstimator() const { return m_rateEstimator; }

    void setRateEstimator(const CRateEstimator& other) { m_rateEstimator = other; }

private:
    void increase();

private:
    mutable sync::Mutex m_BufLock; // used to synchronize buffer operation

    struct Block
    {
        char* m_pcData;  // pointer to the data block
        int   m_iLength; // payload length of the block.

        int32_t    m_iMsgNoBitset; // message number
        int32_t    m_iSeqNo;       // sequence number for scheduling
        time_point m_tsOriginTime; // block origin time (either provided from above or equals the time a message was submitted for sending.
        time_point m_tsRexmitTime; // packet retransmission time
        int        m_iTTL; // time to live (milliseconds)

        Block* m_pNext; // next block

        int32_t getMsgSeq()
        {
            // NOTE: this extracts message ID with regard to REXMIT flag.
            // This is valid only for message ID that IS GENERATED in this instance,
            // not provided by the peer. This can be otherwise sent to the peer - it doesn't matter
            // for the peer that it uses LESS bits to represent the message.
            return m_iMsgNoBitset & MSGNO_SEQ::mask;
        }

    } * m_pBlock, *m_pFirstBlock, *m_pCurrBlock, *m_pLastBlock;

    // m_pBlock:         The head pointer
    // m_pFirstBlock:    The first block
    // m_pCurrBlock:	 The current block
    // m_pLastBlock:     The last block (if first == last, buffer is empty)

    struct Buffer
    {
        char*   m_pcData; // buffer
        int     m_iSize;  // size
        Buffer* m_pNext;  // next buffer
    } * m_pBuffer;        // physical buffer

    int32_t m_iNextMsgNo; // next message number

    int m_iSize; // buffer size (number of packets)
    const int m_iBlockLen;  // maximum length of a block holding packet payload (excluding packet header).
    int m_iCount; // number of used blocks

    int        m_iBytesCount; // number of payload bytes in queue
    time_point m_tsLastOriginTime;

    AvgBufSize m_mavg;
    CRateEstimator m_rateEstimator;

private:
    CSndBuffer(const CSndBuffer&);
    CSndBuffer& operator=(const CSndBuffer&);
};

////////////////////////////////////////////////////////////////////////////////

#if (!ENABLE_NEW_RCVBUFFER)

class CRcvBuffer
{
    typedef sync::steady_clock::time_point time_point;
    typedef sync::steady_clock::duration   duration;

public:
    // XXX There's currently no way to access the socket ID set for
    // whatever the queue is currently working for. Required to find
    // some way to do this, possibly by having a "reverse pointer".
    // Currently just "unimplemented".
    std::string CONID() const { return ""; }

    static const int DEFAULT_SIZE = 65536;
    /// Construct the buffer.
    /// @param [in] queue  CUnitQueue that actually holds the units (packets)
    /// @param [in] bufsize_pkts in units (packets)
    CRcvBuffer(CUnitQueue* queue, int bufsize_pkts = DEFAULT_SIZE);
    ~CRcvBuffer();

public:
    /// Write data into the buffer.
    /// @param [in] unit pointer to a data unit containing new packet
    /// @param [in] offset offset from last ACK point.
    /// @return 0 is success, -1 if data is repeated.
    int addData(CUnit* unit, int offset);

    /// Read data into a user buffer.
    /// @param [in] data pointer to user buffer.
    /// @param [in] len length of user buffer.
    /// @return size of data read.
    int readBuffer(char* data, int len);

    /// Read data directly into file.
    /// @param [in] file C++ file stream.
    /// @param [in] len expected length of data to write into the file.
    /// @return size of data read.
    int readBufferToFile(std::fstream& ofs, int len);

    /// Update the ACK point of the buffer.
    /// @param [in] len number of units to be acknowledged.
    /// @return 1 if a user buffer is fulfilled, otherwise 0.
    int ackData(int len);

    /// Query how many buffer space left for data receiving.
    /// Actually only acknowledged packets, that are still in the buffer,
    /// are considered to take buffer space.
    ///
    /// @return size of available buffer space (including user buffer) for data receiving.
    ///         Not counting unacknowledged packets.
    int getAvailBufSize() const;

    /// Query how many data has been continuously received (for reading) and ready to play (tsbpdtime < now).
    /// @return size of valid (continous) data for reading.
    int getRcvDataSize() const;

    /// Query how many data was received and acknowledged.
    /// @param [out] bytes bytes
    /// @param [out] spantime spantime
    /// @return size in pkts of acked data.
    int getRcvDataSize(int& bytes, int& spantime);

    /// Query a 1 sec moving average of how many data was received and acknowledged.
    /// @param [out] bytes bytes
    /// @param [out] spantime spantime
    /// @return size in pkts of acked data.
    int getRcvAvgDataSize(int& bytes, int& spantime);

    /// Query how many data of the receive buffer is acknowledged.
    /// @param [in] now current time in us.
    /// @return none.
    void updRcvAvgDataSize(const time_point& now);

    /// Query the received average payload size.
    /// @return size (bytes) of payload size
    unsigned getRcvAvgPayloadSize() const;

    struct ReadingState
    {
        time_point tsStart;
        time_point tsLastAck;
        time_point tsEnd;
        int iNumAcknowledged;
        int iNumUnacknowledged;
    };

    ReadingState debugGetReadingState() const;

    /// Form a string of the current buffer fullness state.
    /// number of packets acknowledged, TSBPD readiness, etc.
    std::string strFullnessState(const time_point& tsNow) const;

    /// Mark the message to be dropped from the message list.
    /// @param [in] msgno message number.
    /// @param [in] using_rexmit_flag whether the MSGNO field uses rexmit flag (if not, one more bit is part of the
    /// msgno value)
    void dropMsg(int32_t msgno, bool using_rexmit_flag);

    /// read a message.
    /// @param [out] data buffer to write the message into.
    /// @param [in] len size of the buffer.
    /// @return actuall size of data read.
    int readMsg(char* data, int len);

#if ENABLE_HEAVY_LOGGING
    void readMsgHeavyLogging(int p);
#endif

    /// read a message.
    /// @param [out] data buffer to write the message into.
    /// @param [in] len size of the buffer.
    /// @param [out] tsbpdtime localtime-based (uSec) packet time stamp including buffering delay
    /// @return actuall size of data read.
    int readMsg(char* data, int len, SRT_MSGCTRL& w_mctrl, int upto);

    /// Query if data is ready to read (tsbpdtime <= now if TsbPD is active).
    /// @param [out] tsbpdtime localtime-based (uSec) packet time stamp including buffering delay
    ///                        of next packet in recv buffer, ready or not.
    /// @param [out] curpktseq Sequence number of the packet if there is one ready to play
    /// @return true if ready to play, false otherwise (tsbpdtime may be !0 in
    /// both cases).
    bool isRcvDataReady(time_point& w_tsbpdtime, int32_t& w_curpktseq, int32_t seqdistance);

#ifdef SRT_DEBUG_TSBPD_OUTJITTER
    void debugTraceJitter(time_point t);
#else
    void debugTraceJitter(time_point) {}
#endif   /* SRT_DEBUG_TSBPD_OUTJITTER */

    bool     isRcvDataReady();
    bool     isRcvDataAvailable() { return m_iLastAckPos != m_iStartPos; }
    CPacket* getRcvReadyPacket(int32_t seqdistance);

    /// Set TimeStamp-Based Packet Delivery Rx Mode
    /// @param [in] timebase localtime base (uSec) of packet time stamps including buffering delay
    /// @param [in] delay aggreed TsbPD delay
    void setRcvTsbPdMode(const time_point& timebase, const duration& delay);

    /// Add packet timestamp for drift caclculation and compensation
    /// @param [in] timestamp packet time stamp
    /// @param [in] tsPktArrival arrival time of the packet used to extract the drift sample.
    /// @param [in] rtt RTT sample
    bool addRcvTsbPdDriftSample(uint32_t timestamp, const time_point& tsPktArrival, int rtt);

#ifdef SRT_DEBUG_TSBPD_DRIFT
    void printDriftHistogram(int64_t iDrift);
    void printDriftOffset(int tsbPdOffset, int tsbPdDriftAvg);
#endif

    /// Get information on the 1st message in queue.
    // Parameters (of the 1st packet queue, ready to play or not):
    /// @param [out] w_tsbpdtime localtime-based (uSec) packet time stamp including buffering delay of 1st packet or 0
    /// if none
    /// @param [out] w_passack   true if 1st ready packet is not yet acknowleged (allowed to be delivered to the app)
    /// @param [out] w_skipseqno SRT_SEQNO_NONE or seq number of 1st unacknowledged pkt ready to play preceeded by
    /// missing packets.
    /// @param base_seq          SRT_SEQNO_NONE or desired, ignore seq smaller than base if exist packet ready-to-play
    /// and larger than base
    /// @retval true 1st packet ready to play (tsbpdtime <= now). Not yet acknowledged if passack == true
    /// @retval false IF tsbpdtime = 0: rcv buffer empty; ELSE:
    ///                   IF skipseqno != SRT_SEQNO_NONE, packet ready to play preceeded by missing packets.;
    ///                   IF skipseqno == SRT_SEQNO_NONE, no missing packet but 1st not ready to play.
    bool getRcvFirstMsg(time_point& w_tsbpdtime,
                        bool&       w_passack,
                        int32_t&    w_skipseqno,
                        int32_t&    w_curpktseq,
                        int32_t     base_seq = SRT_SEQNO_NONE);

    /// Update the ACK point of the buffer.
    /// @param [in] len size of data to be skip & acknowledged.
    void skipData(int len);

#if ENABLE_HEAVY_LOGGING
    void reportBufferStats() const; // Heavy logging Debug only
#endif
    bool empty() const
    {
        // This will not always return the intended value,
        // that is, it may return false when the buffer really is
        // empty - but it will return true then in one of next calls.
        // This function will be always called again at some point
        // if it returned false, and on true the connection
        // is going to be broken - so this behavior is acceptable.
        return m_iStartPos == m_iLastAckPos;
    }
    bool full() const { return m_iStartPos == (m_iLastAckPos + 1) % m_iSize; }
    int  capacity() const { return m_iSize; }

private:
    /// This gives up unit at index p. The unit is given back to the
    /// free unit storage for further assignment for the new incoming
    /// data.
    size_t freeUnitAt(size_t p)
    {
        CUnit* u       = m_pUnit[p];
        m_pUnit[p]     = NULL;
        size_t rmbytes = u->m_Packet.getLength();
        m_pUnitQueue->makeUnitFree(u);
        return rmbytes;
    }

    /// Adjust receive queue to 1st ready to play message (tsbpdtime < now).
    /// Parameters (of the 1st packet queue, ready to play or not):
    /// @param [out] tsbpdtime localtime-based (uSec) packet time stamp including buffering delay of 1st packet or 0 if
    /// none
    /// @param base_seq        SRT_SEQNO_NONE or desired, ignore seq smaller than base
    /// @retval true 1st packet ready to play without discontinuity (no hole)
    /// @retval false tsbpdtime = 0: no packet ready to play
    bool getRcvReadyMsg(time_point& w_tsbpdtime, int32_t& w_curpktseq, int upto, int base_seq = SRT_SEQNO_NONE);

public:
    /// @brief Get clock drift in microseconds.
    int64_t getDrift() const { return m_tsbpd.drift(); }

public:
    int32_t getTopMsgno() const;

    void getInternalTimeBase(time_point& w_tb, bool& w_wrp, duration& w_udrift);

    void       applyGroupTime(const time_point& timebase, bool wrapcheck, uint32_t delay, const duration& udrift);
    void       applyGroupDrift(const time_point& timebase, bool wrapcheck, const duration& udrift);
    time_point getPktTsbPdTime(uint32_t timestamp);
    int        debugGetSize() const;
    time_point debugGetDeliveryTime(int offset);

    size_t dropData(int len);

private:
    int  extractData(char* data, int len, int p, int q, bool passack);
    bool accessMsg(int& w_p, int& w_q, bool& w_passack, int64_t& w_playtime, int upto);

    /// Describes the state of the first N packets
    std::string debugTimeState(size_t first_n_pkts) const;

    /// thread safe bytes counter of the Recv & Ack buffer
    /// @param [in] pkts  acked or removed pkts from rcv buffer (used with acked = true)
    /// @param [in] bytes number of bytes added/delete (if negative) to/from rcv buffer.
    /// @param [in] acked true when adding new pkt in RcvBuffer; false when acking/removing pkts to/from buffer
    void countBytes(int pkts, int bytes, bool acked = false);

private:
    bool scanMsg(int& w_start, int& w_end, bool& w_passack);

    int shift(int basepos, int shift) const { return (basepos + shift) % m_iSize; }

    /// Simplified versions with ++ and --; avoid using division instruction
    int shiftFwd(int basepos) const
    {
        if (++basepos == m_iSize)
            return 0;
        return basepos;
    }

    int shiftBack(int basepos) const
    {
        if (basepos == 0)
            return m_iSize - 1;
        return --basepos;
    }

private:
    CUnit**     m_pUnit;      // Array of pointed units collected in the buffer
    const int   m_iSize;      // Size of the internal array of CUnit* items
    CUnitQueue* m_pUnitQueue; // the shared unit queue

    int m_iStartPos;   // HEAD: first packet available for reading
    int m_iLastAckPos; // the last ACKed position (exclusive), follows the last readable
                       // EMPTY: m_iStartPos = m_iLastAckPos   FULL: m_iStartPos = m_iLastAckPos + 1
    int m_iMaxPos;     // delta between acked-TAIL and reception-TAIL

    int m_iNotch; // the starting read point of the first unit
                  // (this is required for stream reading mode; it's
                  // the position in the first unit in the list
                  // up to which data are already retrieved;
                  // in message reading mode it's unused and always 0)

    sync::Mutex m_BytesCountLock;   // used to protect counters operations
    int         m_iBytesCount;      // Number of payload bytes in the buffer
    int         m_iAckedPktsCount;  // Number of acknowledged pkts in the buffer
    int         m_iAckedBytesCount; // Number of acknowledged payload bytes in the buffer
    unsigned    m_uAvgPayloadSz;    // Average payload size for dropped bytes estimation

    CTsbpdTime  m_tsbpd;

    AvgBufSize  m_mavg;

private:
    CRcvBuffer();
    CRcvBuffer(const CRcvBuffer&);
    CRcvBuffer& operator=(const CRcvBuffer&);
};

#endif // !ENABLE_NEW_RCVBUFFER

} // namespace srt

#endif
