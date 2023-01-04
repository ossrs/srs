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
Copyright (c) 2001 - 2011, The Board of Trustees of the University of Illinois.
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
   Yunhong Gu, last updated 03/12/2011
modified by
   Haivision Systems Inc.
*****************************************************************************/

#include "platform_sys.h"

#include <cstring>
#include <cmath>
#include "buffer.h"
#include "packet.h"
#include "core.h" // provides some constants
#include "logging.h"

namespace srt {

using namespace std;
using namespace srt_logging;
using namespace sync;

// You can change this value at build config by using "ENFORCE" options.
#if !defined(SRT_MAVG_SAMPLING_RATE)
#define SRT_MAVG_SAMPLING_RATE 40
#endif

bool AvgBufSize::isTimeToUpdate(const time_point& now) const
{
    const int      usMAvgBasePeriod = 1000000; // 1s in microseconds
    const int      us2ms            = 1000;
    const int      msMAvgPeriod     = (usMAvgBasePeriod / SRT_MAVG_SAMPLING_RATE) / us2ms;
    const uint64_t elapsed_ms       = count_milliseconds(now - m_tsLastSamplingTime); // ms since last sampling
    return (elapsed_ms >= msMAvgPeriod);
}

void AvgBufSize::update(const steady_clock::time_point& now, int pkts, int bytes, int timespan_ms)
{
    const uint64_t elapsed_ms       = count_milliseconds(now - m_tsLastSamplingTime); // ms since last sampling
    m_tsLastSamplingTime            = now;
    const uint64_t one_second_in_ms = 1000;
    if (elapsed_ms > one_second_in_ms)
    {
        // No sampling in last 1 sec, initialize average
        m_dCountMAvg      = pkts;
        m_dBytesCountMAvg = bytes;
        m_dTimespanMAvg   = timespan_ms;
        return;
    }

    //
    // weight last average value between -1 sec and last sampling time (LST)
    // and new value between last sampling time and now
    //                                      |elapsed_ms|
    //   +----------------------------------+-------+
    //  -1                                 LST      0(now)
    //
    m_dCountMAvg      = avg_iir_w<1000, double>(m_dCountMAvg, pkts, elapsed_ms);
    m_dBytesCountMAvg = avg_iir_w<1000, double>(m_dBytesCountMAvg, bytes, elapsed_ms);
    m_dTimespanMAvg   = avg_iir_w<1000, double>(m_dTimespanMAvg, timespan_ms, elapsed_ms);
}

int round_val(double val)
{
    return static_cast<int>(round(val));
}

CRateEstimator::CRateEstimator()
    : m_iInRatePktsCount(0)
    , m_iInRateBytesCount(0)
    , m_InRatePeriod(INPUTRATE_FAST_START_US) // 0.5 sec (fast start)
    , m_iInRateBps(INPUTRATE_INITIAL_BYTESPS)
{}

void CRateEstimator::setInputRateSmpPeriod(int period)
{
    m_InRatePeriod = (uint64_t)period; //(usec) 0=no input rate calculation
}

void CRateEstimator::updateInputRate(const time_point& time, int pkts, int bytes)
{
    // no input rate calculation
    if (m_InRatePeriod == 0)
        return;

    if (is_zero(m_tsInRateStartTime))
    {
        m_tsInRateStartTime = time;
        return;
    }
    else if (time < m_tsInRateStartTime)
    {
        // Old packets are being submitted for estimation, e.g. during the backup link activation.
        return;
    }

    m_iInRatePktsCount  += pkts;
    m_iInRateBytesCount += bytes;

    // Trigger early update in fast start mode
    const bool early_update = (m_InRatePeriod < INPUTRATE_RUNNING_US) && (m_iInRatePktsCount > INPUTRATE_MAX_PACKETS);

    const uint64_t period_us = count_microseconds(time - m_tsInRateStartTime);
    if (early_update || period_us > m_InRatePeriod)
    {
        // Required Byte/sec rate (payload + headers)
        m_iInRateBytesCount += (m_iInRatePktsCount * CPacket::SRT_DATA_HDR_SIZE);
        m_iInRateBps = (int)(((int64_t)m_iInRateBytesCount * 1000000) / period_us);
        HLOGC(bslog.Debug,
              log << "updateInputRate: pkts:" << m_iInRateBytesCount << " bytes:" << m_iInRatePktsCount
                  << " rate=" << (m_iInRateBps * 8) / 1000 << "kbps interval=" << period_us);
        m_iInRatePktsCount  = 0;
        m_iInRateBytesCount = 0;
        m_tsInRateStartTime = time;

        setInputRateSmpPeriod(INPUTRATE_RUNNING_US);
    }
}

CSndBuffer::CSndBuffer(int size, int maxpld)
    : m_BufLock()
    , m_pBlock(NULL)
    , m_pFirstBlock(NULL)
    , m_pCurrBlock(NULL)
    , m_pLastBlock(NULL)
    , m_pBuffer(NULL)
    , m_iNextMsgNo(1)
    , m_iSize(size)
    , m_iBlockLen(maxpld)
    , m_iCount(0)
    , m_iBytesCount(0)
{
    // initial physical buffer of "size"
    m_pBuffer           = new Buffer;
    m_pBuffer->m_pcData = new char[m_iSize * m_iBlockLen];
    m_pBuffer->m_iSize  = m_iSize;
    m_pBuffer->m_pNext  = NULL;

    // circular linked list for out bound packets
    m_pBlock  = new Block;
    Block* pb = m_pBlock;
    char* pc  = m_pBuffer->m_pcData;

    for (int i = 0; i < m_iSize; ++i)
    {
        pb->m_iMsgNoBitset = 0;
        pb->m_pcData       = pc;
        pc                += m_iBlockLen;

        if (i < m_iSize - 1)
        {
            pb->m_pNext        = new Block;
            pb                 = pb->m_pNext;
        }
    }
    pb->m_pNext = m_pBlock;

    m_pFirstBlock = m_pCurrBlock = m_pLastBlock = m_pBlock;

    setupMutex(m_BufLock, "Buf");
}

CSndBuffer::~CSndBuffer()
{
    Block* pb = m_pBlock->m_pNext;
    while (pb != m_pBlock)
    {
        Block* temp = pb;
        pb          = pb->m_pNext;
        delete temp;
    }
    delete m_pBlock;

    while (m_pBuffer != NULL)
    {
        Buffer* temp = m_pBuffer;
        m_pBuffer    = m_pBuffer->m_pNext;
        delete[] temp->m_pcData;
        delete temp;
    }

    releaseMutex(m_BufLock);
}

void CSndBuffer::addBuffer(const char* data, int len, SRT_MSGCTRL& w_mctrl)
{
    int32_t& w_msgno    = w_mctrl.msgno;
    int32_t& w_seqno    = w_mctrl.pktseq;
    int64_t& w_srctime  = w_mctrl.srctime;
    const int& ttl      = w_mctrl.msgttl;
    const int iPktLen   = m_iBlockLen; // Payload length per packet.
    int      iNumBlocks = len / iPktLen;
    if ((len % m_iBlockLen) != 0)
        ++iNumBlocks;

    HLOGC(bslog.Debug,
          log << "addBuffer: needs=" << iNumBlocks << " buffers for " << len << " bytes. Taken=" << m_iCount << "/" << m_iSize);
    // Retrieve current time before locking the mutex to be closer to packet submission event.
    const steady_clock::time_point tnow = steady_clock::now();

    ScopedLock bufferguard(m_BufLock);
    // Dynamically increase sender buffer if there is not enough room.
    while (iNumBlocks + m_iCount >= m_iSize)
    {
        HLOGC(bslog.Debug, log << "addBuffer: ... still lacking " << (iNumBlocks + m_iCount - m_iSize) << " buffers...");
        increase();
    }

    const int32_t inorder = w_mctrl.inorder ? MSGNO_PACKET_INORDER::mask : 0;
    HLOGC(bslog.Debug,
          log << CONID() << "addBuffer: adding " << iNumBlocks << " packets (" << len << " bytes) to send, msgno="
              << (w_msgno > 0 ? w_msgno : m_iNextMsgNo) << (inorder ? "" : " NOT") << " in order");

    // Calculate origin time (same for all blocks of the message).
    m_tsLastOriginTime = w_srctime ? time_point() + microseconds_from(w_srctime) : tnow;
    // Rewrite back the actual value, even if it stays the same, so that the calling facilities can reuse it.
    // May also be a subject to conversion error, thus the actual value is signalled back.
    w_srctime = count_microseconds(m_tsLastOriginTime.time_since_epoch());

    // The sequence number passed to this function is the sequence number
    // that the very first packet from the packet series should get here.
    // If there's more than one packet, this function must increase it by itself
    // and then return the accordingly modified sequence number in the reference.

    Block* s = m_pLastBlock;

    if (w_msgno == SRT_MSGNO_NONE) // DEFAULT-UNCHANGED msgno supplied
    {
        HLOGC(bslog.Debug, log << "addBuffer: using internally managed msgno=" << m_iNextMsgNo);
        w_msgno = m_iNextMsgNo;
    }
    else
    {
        HLOGC(bslog.Debug, log << "addBuffer: OVERWRITTEN by msgno supplied by caller: msgno=" << w_msgno);
        m_iNextMsgNo = w_msgno;
    }

    for (int i = 0; i < iNumBlocks; ++i)
    {
        int pktlen = len - i * iPktLen;
        if (pktlen > iPktLen)
            pktlen = iPktLen;

        HLOGC(bslog.Debug,
              log << "addBuffer: %" << w_seqno << " #" << w_msgno << " offset=" << (i * iPktLen)
                  << " size=" << pktlen << " TO BUFFER:" << (void*)s->m_pcData);
        memcpy((s->m_pcData), data + i * iPktLen, pktlen);
        s->m_iLength = pktlen;

        s->m_iSeqNo = w_seqno;
        w_seqno     = CSeqNo::incseq(w_seqno);

        s->m_iMsgNoBitset = m_iNextMsgNo | inorder;
        if (i == 0)
            s->m_iMsgNoBitset |= PacketBoundaryBits(PB_FIRST);
        if (i == iNumBlocks - 1)
            s->m_iMsgNoBitset |= PacketBoundaryBits(PB_LAST);
        // NOTE: if i is neither 0 nor size-1, it resuls with PB_SUBSEQUENT.
        //       if i == 0 == size-1, it results with PB_SOLO.
        // Packets assigned to one message can be:
        // [PB_FIRST] [PB_SUBSEQUENT] [PB_SUBSEQUENT] [PB_LAST] - 4 packets per message
        // [PB_FIRST] [PB_LAST] - 2 packets per message
        // [PB_SOLO] - 1 packet per message

        s->m_iTTL = ttl;
        s->m_tsRexmitTime = time_point();
        s->m_tsOriginTime = m_tsLastOriginTime;
        
        // Should never happen, as the call to increase() should ensure enough buffers.
        SRT_ASSERT(s->m_pNext);
        s = s->m_pNext;
    }
    m_pLastBlock = s;

    m_iCount += iNumBlocks;
    m_iBytesCount += len;

    m_rateEstimator.updateInputRate(m_tsLastOriginTime, iNumBlocks, len);
    updAvgBufSize(m_tsLastOriginTime);

    // MSGNO_SEQ::mask has a form: 00000011111111...
    // At least it's known that it's from some index inside til the end (to bit 0).
    // If this value has been reached in a step of incrementation, it means that the
    // maximum value has been reached. Casting to int32_t to ensure the same sign
    // in comparison, although it's far from reaching the sign bit.

    const int nextmsgno = ++MsgNo(m_iNextMsgNo);
    HLOGC(bslog.Debug, log << "CSndBuffer::addBuffer: updating msgno: #" << m_iNextMsgNo << " -> #" << nextmsgno);
    m_iNextMsgNo = nextmsgno;
}

int CSndBuffer::addBufferFromFile(fstream& ifs, int len)
{
    const int iPktLen   = m_iBlockLen; // Payload length per packet.
    int      iNumBlocks = len / iPktLen;
    if ((len % m_iBlockLen) != 0)
        ++iNumBlocks;

    HLOGC(bslog.Debug,
          log << "addBufferFromFile: size=" << m_iCount << " reserved=" << m_iSize << " needs=" << iPktLen
              << " buffers for " << len << " bytes");

    // dynamically increase sender buffer
    while (iPktLen + m_iCount >= m_iSize)
    {
        HLOGC(bslog.Debug,
              log << "addBufferFromFile: ... still lacking " << (iPktLen + m_iCount - m_iSize) << " buffers...");
        increase();
    }

    HLOGC(bslog.Debug,
          log << CONID() << "addBufferFromFile: adding " << iPktLen << " packets (" << len
              << " bytes) to send, msgno=" << m_iNextMsgNo);

    Block* s     = m_pLastBlock;
    int    total = 0;
    for (int i = 0; i < iPktLen; ++i)
    {
        if (ifs.bad() || ifs.fail() || ifs.eof())
            break;

        int pktlen = len - i * iPktLen;
        if (pktlen > iPktLen)
            pktlen = iPktLen;

        HLOGC(bslog.Debug,
              log << "addBufferFromFile: reading from=" << (i * iPktLen) << " size=" << pktlen
                  << " TO BUFFER:" << (void*)s->m_pcData);
        ifs.read(s->m_pcData, pktlen);
        if ((pktlen = int(ifs.gcount())) <= 0)
            break;

        // currently file transfer is only available in streaming mode, message is always in order, ttl = infinite
        s->m_iMsgNoBitset = m_iNextMsgNo | MSGNO_PACKET_INORDER::mask;
        if (i == 0)
            s->m_iMsgNoBitset |= PacketBoundaryBits(PB_FIRST);
        if (i == iPktLen - 1)
            s->m_iMsgNoBitset |= PacketBoundaryBits(PB_LAST);
        // NOTE: PB_FIRST | PB_LAST == PB_SOLO.
        // none of PB_FIRST & PB_LAST == PB_SUBSEQUENT.

        s->m_iLength = pktlen;
        s->m_iTTL    = SRT_MSGTTL_INF;
        s            = s->m_pNext;

        total += pktlen;
    }
    m_pLastBlock = s;

    enterCS(m_BufLock);
    m_iCount += iPktLen;
    m_iBytesCount += total;

    leaveCS(m_BufLock);

    m_iNextMsgNo++;
    if (m_iNextMsgNo == int32_t(MSGNO_SEQ::mask))
        m_iNextMsgNo = 1;

    return total;
}

int CSndBuffer::readData(CPacket& w_packet, steady_clock::time_point& w_srctime, int kflgs, int& w_seqnoinc)
{
    int readlen = 0;
    w_seqnoinc = 0;

    ScopedLock bufferguard(m_BufLock);
    while (m_pCurrBlock != m_pLastBlock)
    {
        // Make the packet REFLECT the data stored in the buffer.
        w_packet.m_pcData = m_pCurrBlock->m_pcData;
        readlen = m_pCurrBlock->m_iLength;
        w_packet.setLength(readlen);
        w_packet.m_iSeqNo = m_pCurrBlock->m_iSeqNo;

        // 1. On submission (addBuffer), the KK flag is set to EK_NOENC (0).
        // 2. The readData() is called to get the original (unique) payload not ever sent yet.
        //    The payload must be encrypted for the first time if the encryption
        //    is enabled (arg kflgs != EK_NOENC). The KK encryption flag of the data packet
        //    header must be set and remembered accordingly (see EncryptionKeySpec).
        // 3. The next time this packet is read (only for retransmission), the payload is already
        //    encrypted, and the proper flag value is already stored.
        
        // TODO: Alternatively, encryption could happen before the packet is submitted to the buffer
        // (before the addBuffer() call), and corresponding flags could be set accordingly.
        // This may also put an encryption burden on the application thread, rather than the sending thread,
        // which could be more efficient. Note that packet sequence number must be properly set in that case,
        // as it is used as a counter for the AES encryption.
        if (kflgs == -1)
        {
            HLOGC(bslog.Debug, log << CONID() << " CSndBuffer: ERROR: encryption required and not possible. NOT SENDING.");
            readlen = 0;
        }
        else
        {
            m_pCurrBlock->m_iMsgNoBitset |= MSGNO_ENCKEYSPEC::wrap(kflgs);
        }

        Block* p = m_pCurrBlock;
        w_packet.m_iMsgNo = m_pCurrBlock->m_iMsgNoBitset;
        w_srctime = m_pCurrBlock->m_tsOriginTime;
        m_pCurrBlock = m_pCurrBlock->m_pNext;

        if ((p->m_iTTL >= 0) && (count_milliseconds(steady_clock::now() - w_srctime) > p->m_iTTL))
        {
            LOGC(bslog.Warn, log << CONID() << "CSndBuffer: skipping packet %" << p->m_iSeqNo << " #" << p->getMsgSeq() << " with TTL=" << p->m_iTTL);
            // Skip this packet due to TTL expiry.
            readlen = 0;
            ++w_seqnoinc;
            continue;
        }

        HLOGC(bslog.Debug, log << CONID() << "CSndBuffer: extracting packet size=" << readlen << " to send");
        break;
    }

    return readlen;
}

CSndBuffer::time_point CSndBuffer::peekNextOriginal() const
{
    ScopedLock bufferguard(m_BufLock);
    if (m_pCurrBlock == m_pLastBlock)
        return time_point();

    return m_pCurrBlock->m_tsOriginTime;
}

int32_t CSndBuffer::getMsgNoAt(const int offset)
{
    ScopedLock bufferguard(m_BufLock);

    Block* p = m_pFirstBlock;

    if (p)
    {
        HLOGC(bslog.Debug,
              log << "CSndBuffer::getMsgNoAt: FIRST MSG: size=" << p->m_iLength << " %" << p->m_iSeqNo << " #"
                  << p->getMsgSeq() << " !" << BufferStamp(p->m_pcData, p->m_iLength));
    }

    if (offset >= m_iCount)
    {
        // Prevent accessing the last "marker" block
        LOGC(bslog.Error,
             log << "CSndBuffer::getMsgNoAt: IPE: offset=" << offset << " not found, max offset=" << m_iCount);
        return SRT_MSGNO_CONTROL;
    }

    // XXX Suboptimal procedure to keep the blocks identifiable
    // by sequence number. Consider using some circular buffer.
    int       i;
    Block* ee SRT_ATR_UNUSED = 0;
    for (i = 0; i < offset && p; ++i)
    {
        ee = p;
        p  = p->m_pNext;
    }

    if (!p)
    {
        LOGC(bslog.Error,
             log << "CSndBuffer::getMsgNoAt: IPE: offset=" << offset << " not found, stopped at " << i << " with #"
                 << (ee ? ee->getMsgSeq() : SRT_MSGNO_NONE));
        return SRT_MSGNO_CONTROL;
    }

    HLOGC(bslog.Debug,
          log << "CSndBuffer::getMsgNoAt: offset=" << offset << " found, size=" << p->m_iLength << " %" << p->m_iSeqNo
              << " #" << p->getMsgSeq() << " !" << BufferStamp(p->m_pcData, p->m_iLength));

    return p->getMsgSeq();
}

int CSndBuffer::readData(const int offset, CPacket& w_packet, steady_clock::time_point& w_srctime, int& w_msglen)
{
    int32_t& msgno_bitset = w_packet.m_iMsgNo;

    ScopedLock bufferguard(m_BufLock);

    Block* p = m_pFirstBlock;

    // XXX Suboptimal procedure to keep the blocks identifiable
    // by sequence number. Consider using some circular buffer.
    for (int i = 0; i < offset && p != m_pLastBlock; ++i)
    {
        p = p->m_pNext;
    }
    if (p == m_pLastBlock)
    {
        LOGC(qslog.Error, log << "CSndBuffer::readData: offset " << offset << " too large!");
        return 0;
    }
#if ENABLE_HEAVY_LOGGING
    const int32_t first_seq = p->m_iSeqNo;
    int32_t last_seq = p->m_iSeqNo;
#endif

    // Check if the block that is the next candidate to send (m_pCurrBlock pointing) is stale.

    // If so, then inform the caller that it should first take care of the whole
    // message (all blocks with that message id). Shift the m_pCurrBlock pointer
    // to the position past the last of them. Then return -1 and set the
    // msgno_bitset return reference to the message id that should be dropped as
    // a whole.

    // After taking care of that, the caller should immediately call this function again,
    // this time possibly in order to find the real data to be sent.

    // if found block is stale
    // (This is for messages that have declared TTL - messages that fail to be sent
    // before the TTL defined time comes, will be dropped).

    if ((p->m_iTTL >= 0) && (count_milliseconds(steady_clock::now() - p->m_tsOriginTime) > p->m_iTTL))
    {
        int32_t msgno = p->getMsgSeq();
        w_msglen      = 1;
        p             = p->m_pNext;
        bool move     = false;
        while (p != m_pLastBlock && msgno == p->getMsgSeq())
        {
#if ENABLE_HEAVY_LOGGING
            last_seq = p->m_iSeqNo;
#endif
            if (p == m_pCurrBlock)
                move = true;
            p = p->m_pNext;
            if (move)
                m_pCurrBlock = p;
            w_msglen++;
        }

        HLOGC(qslog.Debug,
              log << "CSndBuffer::readData: due to TTL exceeded, SEQ " << first_seq << " - " << last_seq << ", "
                  << w_msglen << " packets to drop, msgno=" << msgno);

        // If readData returns -1, then msgno_bitset is understood as a Message ID to drop.
        // This means that in this case it should be written by the message sequence value only
        // (not the whole 4-byte bitset written at PH_MSGNO).
        msgno_bitset = msgno;
        return -1;
    }

    w_packet.m_pcData = p->m_pcData;
    const int readlen = p->m_iLength;
    w_packet.setLength(readlen);

    // XXX Here the value predicted to be applied to PH_MSGNO field is extracted.
    // As this function is predicted to extract the data to send as a rexmited packet,
    // the packet must be in the form ready to send - so, in case of encryption,
    // encrypted, and with all ENC flags already set. So, the first call to send
    // the packet originally (the other overload of this function) must set these
    // flags.
    w_packet.m_iMsgNo = p->m_iMsgNoBitset;
    w_srctime = p->m_tsOriginTime;

    // This function is called when packet retransmission is triggered.
    // Therefore we are setting the rexmit time.
    p->m_tsRexmitTime = steady_clock::now();

    HLOGC(qslog.Debug,
          log << CONID() << "CSndBuffer: getting packet %" << p->m_iSeqNo << " as per %" << w_packet.m_iSeqNo
              << " size=" << readlen << " to send [REXMIT]");

    return readlen;
}

sync::steady_clock::time_point CSndBuffer::getPacketRexmitTime(const int offset)
{
    ScopedLock bufferguard(m_BufLock);
    const Block* p = m_pFirstBlock;

    // XXX Suboptimal procedure to keep the blocks identifiable
    // by sequence number. Consider using some circular buffer.
    for (int i = 0; i < offset; ++i)
    {
        SRT_ASSERT(p);
        p = p->m_pNext;
    }

    SRT_ASSERT(p);
    return p->m_tsRexmitTime;
}

void CSndBuffer::ackData(int offset)
{
    ScopedLock bufferguard(m_BufLock);

    bool move = false;
    for (int i = 0; i < offset; ++i)
    {
        m_iBytesCount -= m_pFirstBlock->m_iLength;
        if (m_pFirstBlock == m_pCurrBlock)
            move = true;
        m_pFirstBlock = m_pFirstBlock->m_pNext;
    }
    if (move)
        m_pCurrBlock = m_pFirstBlock;

    m_iCount -= offset;

    updAvgBufSize(steady_clock::now());
}

int CSndBuffer::getCurrBufSize() const
{
    return m_iCount;
}

int CSndBuffer::getAvgBufSize(int& w_bytes, int& w_tsp)
{
    ScopedLock bufferguard(m_BufLock); /* Consistency of pkts vs. bytes vs. spantime */

    /* update stats in case there was no add/ack activity lately */
    updAvgBufSize(steady_clock::now());

    // Average number of packets and timespan could be small,
    // so rounding is beneficial, while for the number of
    // bytes in the buffer is a higher value, so rounding can be omitted,
    // but probably better to round all three values.
    w_bytes = round_val(m_mavg.bytes());
    w_tsp   = round_val(m_mavg.timespan_ms());
    return round_val(m_mavg.pkts());
}

void CSndBuffer::updAvgBufSize(const steady_clock::time_point& now)
{
    if (!m_mavg.isTimeToUpdate(now))
        return;

    int       bytes       = 0;
    int       timespan_ms = 0;
    const int pkts        = getCurrBufSize((bytes), (timespan_ms));
    m_mavg.update(now, pkts, bytes, timespan_ms);
}

int CSndBuffer::getCurrBufSize(int& w_bytes, int& w_timespan)
{
    w_bytes = m_iBytesCount;
    /*
     * Timespan can be less then 1000 us (1 ms) if few packets.
     * Also, if there is only one pkt in buffer, the time difference will be 0.
     * Therefore, always add 1 ms if not empty.
     */
    w_timespan = 0 < m_iCount ? (int) count_milliseconds(m_tsLastOriginTime - m_pFirstBlock->m_tsOriginTime) + 1 : 0;

    return m_iCount;
}

CSndBuffer::duration CSndBuffer::getBufferingDelay(const time_point& tnow) const
{
    ScopedLock lck(m_BufLock);
    SRT_ASSERT(m_pFirstBlock);
    if (m_iCount == 0)
        return duration(0);

    return tnow - m_pFirstBlock->m_tsOriginTime;
}

int CSndBuffer::dropLateData(int& w_bytes, int32_t& w_first_msgno, const steady_clock::time_point& too_late_time)
{
    int     dpkts  = 0;
    int     dbytes = 0;
    bool    move   = false;
    int32_t msgno  = 0;

    ScopedLock bufferguard(m_BufLock);
    for (int i = 0; i < m_iCount && m_pFirstBlock->m_tsOriginTime < too_late_time; ++i)
    {
        dpkts++;
        dbytes += m_pFirstBlock->m_iLength;
        msgno = m_pFirstBlock->getMsgSeq();

        if (m_pFirstBlock == m_pCurrBlock)
            move = true;
        m_pFirstBlock = m_pFirstBlock->m_pNext;
    }

    if (move)
    {
        m_pCurrBlock = m_pFirstBlock;
    }
    m_iCount -= dpkts;

    m_iBytesCount -= dbytes;
    w_bytes = dbytes;

    // We report the increased number towards the last ever seen
    // by the loop, as this last one is the last received. So remained
    // (even if "should remain") is the first after the last removed one.
    w_first_msgno = ++MsgNo(msgno);

    updAvgBufSize(steady_clock::now());

    return (dpkts);
}

void CSndBuffer::increase()
{
    int unitsize = m_pBuffer->m_iSize;

    // new physical buffer
    Buffer* nbuf = NULL;
    try
    {
        nbuf           = new Buffer;
        nbuf->m_pcData = new char[unitsize * m_iBlockLen];
    }
    catch (...)
    {
        delete nbuf;
        throw CUDTException(MJ_SYSTEMRES, MN_MEMORY, 0);
    }
    nbuf->m_iSize = unitsize;
    nbuf->m_pNext = NULL;

    // insert the buffer at the end of the buffer list
    Buffer* p = m_pBuffer;
    while (p->m_pNext != NULL)
        p = p->m_pNext;
    p->m_pNext = nbuf;

    // new packet blocks
    Block* nblk = NULL;
    try
    {
        nblk = new Block;
    }
    catch (...)
    {
        delete nblk;
        throw CUDTException(MJ_SYSTEMRES, MN_MEMORY, 0);
    }
    Block* pb = nblk;
    for (int i = 1; i < unitsize; ++i)
    {
        pb->m_pNext = new Block;
        pb          = pb->m_pNext;
    }

    // insert the new blocks onto the existing one
    pb->m_pNext           = m_pLastBlock->m_pNext;
    m_pLastBlock->m_pNext = nblk;

    pb       = nblk;
    char* pc = nbuf->m_pcData;
    for (int i = 0; i < unitsize; ++i)
    {
        pb->m_pcData = pc;
        pb           = pb->m_pNext;
        pc += m_iBlockLen;
    }

    m_iSize += unitsize;

    HLOGC(bslog.Debug,
          log << "CSndBuffer: BUFFER FULL - adding " << (unitsize * m_iBlockLen) << " bytes spread to " << unitsize
              << " blocks"
              << " (total size: " << m_iSize << " bytes)");
}

////////////////////////////////////////////////////////////////////////////////

#if (!ENABLE_NEW_RCVBUFFER)

/*
 *   RcvBuffer (circular buffer):
 *
 *   |<------------------- m_iSize ----------------------------->|
 *   |       |<--- acked pkts -->|<--- m_iMaxPos --->|           |
 *   |       |                   |                   |           |
 *   +---+---+---+---+---+---+---+---+---+---+---+---+---+   +---+
 *   | 0 | 0 | 1 | 1 | 1 | 0 | 1 | 1 | 1 | 1 | 0 | 1 | 0 |...| 0 | m_pUnit[]
 *   +---+---+---+---+---+---+---+---+---+---+---+---+---+   +---+
 *             |                 | |               |
 *             |                   |               \__last pkt received
 *             |                   \___ m_iLastAckPos: last ack sent
 *             \___ m_iStartPos: first message to read
 *
 *   m_pUnit[i]->m_iFlag: 0:free, 1:good, 2:passack, 3:dropped
 *
 *   thread safety:
 *    m_iStartPos:   CUDT::m_RecvLock
 *    m_iLastAckPos: CUDT::m_AckLock
 *    m_iMaxPos:     none? (modified on add and ack
 */

CRcvBuffer::CRcvBuffer(CUnitQueue* queue, int bufsize_pkts)
    : m_pUnit(NULL)
    , m_iSize(bufsize_pkts)
    , m_pUnitQueue(queue)
    , m_iStartPos(0)
    , m_iLastAckPos(0)
    , m_iMaxPos(0)
    , m_iNotch(0)
    , m_BytesCountLock()
    , m_iBytesCount(0)
    , m_iAckedPktsCount(0)
    , m_iAckedBytesCount(0)
    , m_uAvgPayloadSz(7 * 188)
{
    m_pUnit = new CUnit*[m_iSize];
    for (int i = 0; i < m_iSize; ++i)
        m_pUnit[i] = NULL;

    setupMutex(m_BytesCountLock, "BytesCount");
}

CRcvBuffer::~CRcvBuffer()
{
    for (int i = 0; i < m_iSize; ++i)
    {
        if (m_pUnit[i] != NULL)
        {
            m_pUnitQueue->makeUnitFree(m_pUnit[i]);
        }
    }

    delete[] m_pUnit;

    releaseMutex(m_BytesCountLock);
}

void CRcvBuffer::countBytes(int pkts, int bytes, bool acked)
{
    /*
     * Byte counter changes from both sides (Recv & Ack) of the buffer
     * so the higher level lock is not enough for thread safe op.
     *
     * pkts are...
     *  added (bytes>0, acked=false),
     *  acked (bytes>0, acked=true),
     *  removed (bytes<0, acked=n/a)
     */
    ScopedLock cg(m_BytesCountLock);

    if (!acked) // adding new pkt in RcvBuffer
    {
        m_iBytesCount += bytes; /* added or removed bytes from rcv buffer */
        if (bytes > 0)          /* Assuming one pkt when adding bytes */
            m_uAvgPayloadSz = ((m_uAvgPayloadSz * (100 - 1)) + bytes) / 100;
    }
    else // acking/removing pkts to/from buffer
    {
        m_iAckedPktsCount += pkts;   /* acked or removed pkts from rcv buffer */
        m_iAckedBytesCount += bytes; /* acked or removed bytes from rcv buffer */

        if (bytes < 0)
            m_iBytesCount += bytes; /* removed bytes from rcv buffer */
    }
}

int CRcvBuffer::addData(CUnit* unit, int offset)
{
    SRT_ASSERT(unit != NULL);
    if (offset >= getAvailBufSize())
        return -1;

    const int pos = (m_iLastAckPos + offset) % m_iSize;
    if (offset >= m_iMaxPos)
        m_iMaxPos = offset + 1;

    if (m_pUnit[pos] != NULL)
    {
        HLOGC(qrlog.Debug, log << "addData: unit %" << unit->m_Packet.m_iSeqNo << " rejected, already exists");
        return -1;
    }
    m_pUnit[pos] = unit;
    countBytes(1, (int)unit->m_Packet.getLength());

    m_pUnitQueue->makeUnitGood(unit);

    HLOGC(qrlog.Debug,
          log << "addData: unit %" << unit->m_Packet.m_iSeqNo << " accepted, off=" << offset << " POS=" << pos);
    return 0;
}

int CRcvBuffer::readBuffer(char* data, int len)
{
    int p       = m_iStartPos;
    int lastack = m_iLastAckPos;
    int rs      = len;
    IF_HEAVY_LOGGING(char* begin = data);

    const bool bTsbPdEnabled = m_tsbpd.isEnabled();
    const steady_clock::time_point now = (bTsbPdEnabled ? steady_clock::now() : steady_clock::time_point());

    HLOGC(brlog.Debug, log << CONID() << "readBuffer: start=" << p << " lastack=" << lastack);
    while ((p != lastack) && (rs > 0))
    {
        if (m_pUnit[p] == NULL)
        {
            LOGC(brlog.Error, log << CONID() << "IPE readBuffer on null packet pointer");
            return -1;
        }

        const CPacket& pkt = m_pUnit[p]->m_Packet;

        if (bTsbPdEnabled)
        {
            HLOGC(brlog.Debug,
                  log << CONID() << "readBuffer: chk if time2play:"
                      << " NOW=" << FormatTime(now)
                      << " PKT TS=" << FormatTime(getPktTsbPdTime(pkt.getMsgTimeStamp())));

            if ((getPktTsbPdTime(pkt.getMsgTimeStamp()) > now))
                break; /* too early for this unit, return whatever was copied */
        }

        const int pktlen = (int) pkt.getLength();
        const int remain_pktlen = pktlen - m_iNotch;

        const int unitsize = std::min(remain_pktlen, rs);

        HLOGC(brlog.Debug,
              log << CONID() << "readBuffer: copying buffer #" << p << " targetpos=" << int(data - begin)
                  << " sourcepos=" << m_iNotch << " size=" << unitsize << " left=" << (unitsize - rs));
        memcpy((data), pkt.m_pcData + m_iNotch, unitsize);

        data += unitsize;

        if (rs >= remain_pktlen)
        {
            freeUnitAt(p);
            p = shiftFwd(p);

            m_iNotch = 0;
        }
        else
            m_iNotch += rs;

        rs -= unitsize;
    }

    /* we removed acked bytes form receive buffer */
    countBytes(-1, -(len - rs), true);
    m_iStartPos = p;

    return len - rs;
}

int CRcvBuffer::readBufferToFile(fstream& ofs, int len)
{
    int p       = m_iStartPos;
    int lastack = m_iLastAckPos;
    int rs      = len;

    int32_t trace_seq SRT_ATR_UNUSED = SRT_SEQNO_NONE;
    int trace_shift SRT_ATR_UNUSED = -1;

    while ((p != lastack) && (rs > 0))
    {
#if ENABLE_LOGGING
        ++trace_shift;
#endif
        // Skip empty units. Note that this shouldn't happen
        // in case of a file transfer.
        if (!m_pUnit[p])
        {
            p = shiftFwd(p);
            LOGC(brlog.Error, log << "readBufferToFile: IPE: NULL unit found in file transmission, last good %"
                    << trace_seq << " + " << trace_shift);
            continue;
        }

        const CPacket& pkt = m_pUnit[p]->m_Packet;

#if ENABLE_LOGGING
        trace_seq = pkt.getSeqNo();
#endif
        const int pktlen = (int) pkt.getLength();
        const int remain_pktlen = pktlen - m_iNotch;

        const int unitsize = std::min(remain_pktlen, rs);

        ofs.write(pkt.m_pcData + m_iNotch, unitsize);
        if (ofs.fail())
            break;

        if (rs >= remain_pktlen)
        {
            freeUnitAt(p);
            p = shiftFwd(p);

            m_iNotch = 0;
        }
        else
            m_iNotch += rs;

        rs -= unitsize;
    }

    /* we removed acked bytes form receive buffer */
    countBytes(-1, -(len - rs), true);
    m_iStartPos = p;

    return len - rs;
}

int CRcvBuffer::ackData(int len)
{
    SRT_ASSERT(len < m_iSize);
    SRT_ASSERT(len > 0);
    int end = shift(m_iLastAckPos, len);

    {
        int pkts  = 0;
        int bytes = 0;
        for (int i = m_iLastAckPos; i != end; i = shiftFwd(i))
        {
            if (m_pUnit[i] == NULL)
                continue;

            pkts++;
            bytes += (int)m_pUnit[i]->m_Packet.getLength();
        }
        if (pkts > 0)
            countBytes(pkts, bytes, true);
    }

    HLOGC(brlog.Debug,
          log << "ackData: shift by " << len << ", start=" << m_iStartPos << " end=" << m_iLastAckPos << " -> " << end);

    m_iLastAckPos = end;
    m_iMaxPos -= len;
    if (m_iMaxPos < 0)
        m_iMaxPos = 0;

    // Returned value is the distance towards the starting
    // position from m_iLastAckPos, which is in sync with CUDT::m_iRcvLastSkipAck.
    // This should help determine the sequence number at first read-ready position.

    const int dist = m_iLastAckPos - m_iStartPos;
    if (dist < 0)
        return dist + m_iSize;
    return dist;
}

void CRcvBuffer::skipData(int len)
{
    /*
     * Caller need protect both AckLock and RecvLock
     * to move both m_iStartPos and m_iLastAckPost
     */
    if (m_iStartPos == m_iLastAckPos)
        m_iStartPos = (m_iStartPos + len) % m_iSize;
    m_iLastAckPos = (m_iLastAckPos + len) % m_iSize;
    m_iMaxPos -= len;
    if (m_iMaxPos < 0)
        m_iMaxPos = 0;
}

size_t CRcvBuffer::dropData(int len)
{
    // This function does the same as skipData, although skipData
    // should work in the condition of absence of data, so no need
    // to force the units in the range to be freed. This function
    // works in more general condition where we don't know if there
    // are any data in the given range, but want to remove these
    // "sequence positions" from the buffer, whether there are data
    // at them or not.

    size_t stats_bytes = 0;

    int p      = m_iStartPos;
    int past_q = shift(p, len);
    while (p != past_q)
    {
        if (m_pUnit[p] && m_pUnit[p]->m_iFlag == CUnit::GOOD)
        {
            stats_bytes += m_pUnit[p]->m_Packet.getLength();
            freeUnitAt(p);
        }

        p = shiftFwd(p);
    }

    m_iStartPos = past_q;
    return stats_bytes;
}

bool CRcvBuffer::getRcvFirstMsg(steady_clock::time_point& w_tsbpdtime,
                                bool&                     w_passack,
                                int32_t&                  w_skipseqno,
                                int32_t&                  w_curpktseq,
                                int32_t                   base_seq)
{
    HLOGC(brlog.Debug, log << "getRcvFirstMsg: base_seq=" << base_seq);
    w_skipseqno = SRT_SEQNO_NONE;
    w_passack   = false;
    // tsbpdtime will be retrieved by the below call
    // Returned values:
    // - tsbpdtime: real time when the packet is ready to play (whether ready to play or not)
    // - w_passack: false (the report concerns a packet with an exactly next sequence)
    // - w_skipseqno == SRT_SEQNO_NONE: no packets to skip towards the first RTP
    // - w_curpktseq: that exactly packet that is reported (for debugging purposes)
    // - @return: whether the reported packet is ready to play

    /* Check the acknowledged packets */
    // getRcvReadyMsg returns true if the time to play for the first message
    // that larger than base_seq is in the past.
    if (getRcvReadyMsg((w_tsbpdtime), (w_curpktseq), -1, base_seq))
    {
        HLOGC(brlog.Debug, log << "getRcvFirstMsg: ready CONTIG packet: %" << w_curpktseq);
        return true;
    }
    else if (!is_zero(w_tsbpdtime))
    {
        HLOGC(brlog.Debug, log << "getRcvFirstMsg: packets found, but in future");
        // This means that a message next to be played, has been found,
        // but the time to play is in future.
        return false;
    }

    // Falling here means that there are NO PACKETS in the ACK-ed region
    // (m_iStartPos - m_iLastAckPos), but we may have something in the
    // region (m_iLastAckPos - (m_iLastAckPos+m_iMaxPos)), that is, packets
    // that may be separated from the last ACK-ed by lost ones.

    // Below this line we have only two options:
    // - m_iMaxPos == 0, which means that no more packets are in the buffer
    //    - returned: tsbpdtime=0, w_passack=true, w_skipseqno=SRT_SEQNO_NONE, w_curpktseq=<unchanged>, @return false
    // - m_iMaxPos > 0, which means that there are packets arrived after a lost packet:
    //    - returned: tsbpdtime=PKT.TS, w_passack=true, w_skipseqno=PKT.SEQ, w_curpktseq=PKT, @return LOCAL(PKT.TS) <=
    //    NOW

    /*
     * No acked packets ready but caller want to know next packet to wait for
     * Check the not yet acked packets that may be stuck by missing packet(s).
     */
    bool                     haslost        = false;
    int                      last_ready_pos = -1;
    steady_clock::time_point tsbpdtime      = steady_clock::time_point();
    w_tsbpdtime                             = steady_clock::time_point();
    w_passack                               = true;

    // XXX SUSPECTED ISSUE with this algorithm:
    // The above call to getRcvReadyMsg() should report as to whether:
    // - there is an EXACTLY NEXT SEQUENCE packet
    // - this packet is ready to play.
    //
    // Situations handled after the call are when:
    // - there's the next sequence packet available and it is ready to play
    // - there are no packets at all, ready to play or not
    //
    // So, the remaining situation is that THERE ARE PACKETS that follow
    // the current sequence, but they are not ready to play. This includes
    // packets that have the exactly next sequence and packets that jump
    // over a lost packet.
    //
    // As the getRcvReadyMsg() function walks through the incoming units
    // to see if there's anything that satisfies these conditions, it *SHOULD*
    // be also capable of checking if the next available packet, if it is
    // there, is the next sequence packet or not. Retrieving this exactly
    // packet would be most useful, as the test for play-readiness and
    // sequentiality can be done on it directly.
    //
    // When done so, the below loop would be completely unnecessary.

    // Logical description of the below algorithm:
    // 1. update w_tsbpdtime and w_curpktseq if found one packet ready to play
    //    - keep check the next packet if still smaller than base_seq
    // 2. set w_skipseqno if found packets before w_curpktseq lost
    // if no packets larger than base_seq ready to play, return the largest RTP
    // else return the first one that larger than base_seq and rady to play

    for (int i = m_iLastAckPos, n = shift(m_iLastAckPos, m_iMaxPos); i != n; i = shiftFwd(i))
    {
        if (!m_pUnit[i] || m_pUnit[i]->m_iFlag != CUnit::GOOD)
        {
            /* There are packets in the sequence not received yet */
            haslost = true;
            HLOGC(brlog.Debug, log << "getRcvFirstMsg: empty hole at *" << i);
        }
        else
        {
            tsbpdtime = getPktTsbPdTime(m_pUnit[i]->m_Packet.getMsgTimeStamp());
            /* Packet ready to play */
            if (tsbpdtime <= steady_clock::now())
            {
                // If the last ready-to-play packet exists, free it.
                if (!is_zero(w_tsbpdtime)) {
                    HLOGC(brlog.Debug,
                          log << "getRcvFirstMsg: found next ready packet, free last %"
                              << w_curpktseq << " POS=" << last_ready_pos);
                    SRT_ASSERT(w_curpktseq != SRT_SEQNO_NONE);
                    freeUnitAt(last_ready_pos);
                }
                w_tsbpdtime    = tsbpdtime;
                w_curpktseq    = m_pUnit[i]->m_Packet.m_iSeqNo;
                last_ready_pos = i;
                if (haslost)
                    w_skipseqno = w_curpktseq;

                if (base_seq != SRT_SEQNO_NONE && CSeqNo::seqcmp(w_curpktseq, base_seq) <= 0)
                {
                    HLOGC(brlog.Debug,
                          log << "getRcvFirstMsg: found ready packet %" << w_curpktseq
                              << " but not larger than base_seq, try next");
                    continue;
                }

                HLOGC(brlog.Debug,
                      log << "getRcvFirstMsg: found ready packet, nSKIPPED: "
                          << ((i - m_iLastAckPos + m_iSize) % m_iSize));

                // NOTE: if haslost is not set, it means that this is the VERY FIRST
                // packet, that is, packet currently at pos = m_iLastAckPos. There's no
                // possibility that it is so otherwise because:
                // - if this first good packet is ready to play, THIS HERE RETURNS NOW.
                // ...
                return true;
            }

            if (!is_zero(w_tsbpdtime)) {
                return true;
            }
            HLOGC(brlog.Debug,
                  log << "getRcvFirstMsg: found NOT READY packet, nSKIPPED: "
                      << ((i - m_iLastAckPos + m_iSize) % m_iSize));
            // ... and if this first good packet WASN'T ready to play, THIS HERE RETURNS NOW, TOO,
            // just states that there's no ready packet to play.
            // ...
            return false;
        }
        // ... and if this first packet WASN'T GOOD, the loop continues, however since now
        // the 'haslost' is set, which means that it continues only to find the first valid
        // packet after stating that the very first packet isn't valid.
    }
    if (!is_zero(w_tsbpdtime)) {
        return true;
    }
    HLOGC(brlog.Debug, log << "getRcvFirstMsg: found NO PACKETS");
    return false;
}

steady_clock::time_point CRcvBuffer::debugGetDeliveryTime(int offset)
{
    int i;
    if (offset > 0)
        i = shift(m_iStartPos, offset);
    else
        i = m_iStartPos;

    CUnit* u = m_pUnit[i];
    if (!u || u->m_iFlag != CUnit::GOOD)
        return steady_clock::time_point();

    return getPktTsbPdTime(u->m_Packet.getMsgTimeStamp());
}

int32_t CRcvBuffer::getTopMsgno() const
{
    if (m_iStartPos == m_iLastAckPos)
        return SRT_MSGNO_NONE; // No message is waiting

    if (!m_pUnit[m_iStartPos])
        return SRT_MSGNO_NONE; // pity

    return m_pUnit[m_iStartPos]->m_Packet.getMsgSeq();
}

bool CRcvBuffer::getRcvReadyMsg(steady_clock::time_point& w_tsbpdtime, int32_t& w_curpktseq, int upto, int base_seq)
{
    const bool havelimit = upto != -1;
    int        end = -1, past_end = -1;
    if (havelimit)
    {
        int stretch = (m_iSize + m_iStartPos - m_iLastAckPos) % m_iSize;
        if (upto > stretch)
        {
            HLOGC(brlog.Debug, log << "position back " << upto << " exceeds stretch " << stretch);
            // Do nothing. This position is already gone.
            return false;
        }

        end = m_iLastAckPos - upto;
        if (end < 0)
            end += m_iSize;
        past_end = shiftFwd(end); // For in-loop comparison
        HLOGC(brlog.Debug, log << "getRcvReadyMsg: will read from position " << end);
    }

    // NOTE: position m_iLastAckPos in the buffer represents the sequence number of
    // CUDT::m_iRcvLastSkipAck. Therefore 'upto' contains a positive value that should
    // be decreased from m_iLastAckPos to get the position in the buffer that represents
    // the sequence number up to which we'd like to read.
    IF_HEAVY_LOGGING(const char* reason = "NOT RECEIVED");

    for (int i = m_iStartPos, n = m_iLastAckPos; i != n; i = shiftFwd(i))
    {
        // In case when we want to read only up to given sequence number, stop
        // the loop if this number was reached. This number must be extracted from
        // the buffer and any following must wait here for "better times". Note
        // that the unit that points to the requested sequence must remain in
        // the buffer, unless there is no valid packet at that position, in which
        // case it is allowed to point to the NEXT sequence towards it, however
        // if it does, this cell must remain in the buffer for prospective recovery.
        if (havelimit && i == past_end)
            break;

        bool freeunit = false;

        /* Skip any invalid skipped/dropped packets */
        if (m_pUnit[i] == NULL)
        {
            HLOGC(brlog.Debug,
                  log << "getRcvReadyMsg: POS=" << i << " +" << ((i - m_iStartPos + m_iSize) % m_iSize)
                      << " SKIPPED - no unit there");
            m_iStartPos = shiftFwd(m_iStartPos);
            continue;
        }

        w_curpktseq = m_pUnit[i]->m_Packet.getSeqNo();

        if (m_pUnit[i]->m_iFlag != CUnit::GOOD)
        {
            HLOGC(brlog.Debug,
                  log << "getRcvReadyMsg: POS=" << i << " +" << ((i - m_iStartPos + m_iSize) % m_iSize)
                      << " SKIPPED - unit not good");
            freeunit = true;
        }
        else
        {
            // This does:
            // 1. Get the TSBPD time of the unit. Stop and return false if this unit
            //    is not yet ready to play.
            // 2. If it's ready to play, check also if it's decrypted. If not, skip it.
            // 3. Check also if it's larger than base_seq, if not, skip it.
            // 4. If it's ready to play, decrypted and larger than base, stop and return it.
            if (!havelimit)
            {
                w_tsbpdtime                         = getPktTsbPdTime(m_pUnit[i]->m_Packet.getMsgTimeStamp());
                const steady_clock::duration towait = (w_tsbpdtime - steady_clock::now());
                if (towait.count() > 0)
                {
                    HLOGC(brlog.Debug,
                          log << "getRcvReadyMsg: POS=" << i << " +" << ((i - m_iStartPos + m_iSize) % m_iSize)
                              << " pkt %" << w_curpktseq << " NOT ready to play (only in " << count_milliseconds(towait)
                              << "ms)");
                    return false;
                }

                if (m_pUnit[i]->m_Packet.getMsgCryptoFlags() != EK_NOENC)
                {
                    IF_HEAVY_LOGGING(reason = "DECRYPTION FAILED");
                    freeunit = true; /* packet not decrypted */
                }
                else if (base_seq != SRT_SEQNO_NONE && CSeqNo::seqcmp(w_curpktseq, base_seq) <= 0)
                {
                    IF_HEAVY_LOGGING(reason = "smaller than base_seq");
                    w_tsbpdtime = steady_clock::time_point();
                    freeunit = true;
                }
                else
                {
                    HLOGC(brlog.Debug,
                          log << "getRcvReadyMsg: POS=" << i << " +" << ((i - m_iStartPos + m_iSize) % m_iSize)
                              << " pkt %" << w_curpktseq << " ready to play (delayed " << count_milliseconds(towait)
                              << "ms)");
                    return true;
                }
            }
            // In this case:
            // 1. We don't even look into the packet if this is not the requested sequence.
            //    All packets that are earlier than the required sequence will be dropped.
            // 2. When found the packet with expected sequence number, and the condition for
            //    good unit is passed, we get the timestamp.
            // 3. If the packet is not decrypted, we allow it to be removed
            // 4. If we reached the required sequence, and the packet is good, KEEP IT in the buffer,
            //    and return with the pointer pointing to this very buffer. Only then return true.
            else
            {
                // We have a limit up to which the reading will be done,
                // no matter if the time has come or not - although retrieve it.
                if (i == end)
                {
                    HLOGC(brlog.Debug, log << "CAUGHT required seq position " << i);
                    // We have the packet we need. Extract its data.
                    w_tsbpdtime = getPktTsbPdTime(m_pUnit[i]->m_Packet.getMsgTimeStamp());

                    // If we have a decryption failure, allow the unit to be released.
                    if (m_pUnit[i]->m_Packet.getMsgCryptoFlags() != EK_NOENC)
                    {
                        IF_HEAVY_LOGGING(reason = "DECRYPTION FAILED");
                        freeunit = true; /* packet not decrypted */
                    }
                    else
                    {
                        // Stop here and keep the packet in the buffer, so it will be
                        // next extracted.
                        HLOGC(brlog.Debug,
                              log << "getRcvReadyMsg: packet seq=" << w_curpktseq << " ready for extraction");
                        return true;
                    }
                }
                else
                {
                    HLOGC(brlog.Debug, log << "SKIPPING position " << i);
                    // Continue the loop and remove the current packet because
                    // its sequence number is too old.
                    freeunit = true;
                }
            }
        }

        if (freeunit)
        {
            HLOGC(brlog.Debug, log << "getRcvReadyMsg: POS=" << i << " FREED: " << reason);
            /* removed skipped, dropped, undecryptable bytes from rcv buffer */
            const int rmbytes = (int)m_pUnit[i]->m_Packet.getLength();
            countBytes(-1, -rmbytes, true);

            freeUnitAt(i);
            m_iStartPos = shiftFwd(m_iStartPos);
        }
    }

    HLOGC(brlog.Debug, log << "getRcvReadyMsg: nothing to deliver: " << reason);
    return false;
}

/*
 * Return receivable data status (packet timestamp_us ready to play if TsbPd mode)
 * Return playtime (tsbpdtime) of 1st packet in queue, ready to play or not
 *
 * Return data ready to be received (packet timestamp_us ready to play if TsbPd mode)
 * Using getRcvDataSize() to know if there is something to read as it was widely
 * used in the code (core.cpp) is expensive in TsbPD mode, hence this simpler function
 * that only check if first packet in queue is ready.
 */
bool CRcvBuffer::isRcvDataReady(steady_clock::time_point& w_tsbpdtime, int32_t& w_curpktseq, int32_t seqdistance)
{
    w_tsbpdtime = steady_clock::time_point();

    if (m_tsbpd.isEnabled())
    {
        const CPacket* pkt = getRcvReadyPacket(seqdistance);
        if (!pkt)
        {
            HLOGC(brlog.Debug, log << "isRcvDataReady: packet NOT extracted.");
            return false;
        }

        /*
         * Acknowledged data is available,
         * Only say ready if time to deliver.
         * Report the timestamp, ready or not.
         */
        w_curpktseq = pkt->getSeqNo();
        w_tsbpdtime = getPktTsbPdTime(pkt->getMsgTimeStamp());

        // If seqdistance was passed, then return true no matter what the
        // TSBPD time states.
        if (seqdistance != -1 || w_tsbpdtime <= steady_clock::now())
        {
            HLOGC(brlog.Debug,
                  log << "isRcvDataReady: packet extracted seqdistance=" << seqdistance
                      << " TsbPdTime=" << FormatTime(w_tsbpdtime));
            return true;
        }

        HLOGC(brlog.Debug, log << "isRcvDataReady: packet extracted, but NOT READY");
        return false;
    }

    return isRcvDataAvailable();
}

// XXX This function may be called only after checking
// if m_bTsbPdMode.
CPacket* CRcvBuffer::getRcvReadyPacket(int32_t seqdistance)
{
    // If asked for readiness of a packet at given sequence distance
    // (that is, we need to extract the packet with given sequence number),
    // only check if this cell is occupied in the buffer, and if so,
    // if it's occupied with a "good" unit. That's all. It doesn't
    // matter whether it's ready to play.
    if (seqdistance != -1)
    {
        // Note: seqdistance is the value to to go BACKWARDS from m_iLastAckPos,
        // which is the position that is in sync with CUDT::m_iRcvLastSkipAck. This
        // position is the sequence number of a packet that is NOT received, but it's
        // expected to be received as next. So the minimum value of seqdistance is 1.

        // SANITY CHECK
        if (seqdistance == 0)
        {
            LOGC(brlog.Fatal, log << "IPE: trying to extract packet past the last ACK-ed!");
            return 0;
        }

        if (seqdistance > getRcvDataSize())
        {
            HLOGC(brlog.Debug,
                  log << "getRcvReadyPacket: Sequence offset=" << seqdistance
                      << " is in the past (start=" << m_iStartPos << " end=" << m_iLastAckPos << ")");
            return 0;
        }

        int i = shift(m_iLastAckPos, -seqdistance);
        if (m_pUnit[i] && m_pUnit[i]->m_iFlag == CUnit::GOOD)
        {
            HLOGC(brlog.Debug, log << "getRcvReadyPacket: FOUND PACKET %" << m_pUnit[i]->m_Packet.getSeqNo());
            return &m_pUnit[i]->m_Packet;
        }

        HLOGC(brlog.Debug, log << "getRcvReadyPacket: Sequence offset=" << seqdistance << " IS NOT RECEIVED.");
        return 0;
    }

    IF_HEAVY_LOGGING(int nskipped = 0);
    for (int i = m_iStartPos, n = m_iLastAckPos; i != n; i = shiftFwd(i))
    {
        /*
         * Skip missing packets that did not arrive in time.
         */
        if (m_pUnit[i] && m_pUnit[i]->m_iFlag == CUnit::GOOD)
        {
            HLOGC(brlog.Debug,
                log << "getRcvReadyPacket: Found next packet seq=%" << m_pUnit[i]->m_Packet.getSeqNo() << " ("
                << nskipped << " empty cells skipped)");
            return &m_pUnit[i]->m_Packet;
        }
        IF_HEAVY_LOGGING(++nskipped);
    }

    return 0;
}

#if ENABLE_HEAVY_LOGGING
// This function is for debug purposes only and it's called only
// from within HLOG* macros.
void CRcvBuffer::reportBufferStats() const
{
    int     nmissing = 0;
    int32_t low_seq = SRT_SEQNO_NONE, high_seq = SRT_SEQNO_NONE;
    int32_t low_ts = 0, high_ts = 0;

    for (int i = m_iStartPos, n = m_iLastAckPos; i != n; i = (i + 1) % m_iSize)
    {
        if (m_pUnit[i] && m_pUnit[i]->m_iFlag == CUnit::GOOD)
        {
            low_seq = m_pUnit[i]->m_Packet.m_iSeqNo;
            low_ts  = m_pUnit[i]->m_Packet.m_iTimeStamp;
            break;
        }
        ++nmissing;
    }

    // Not sure if a packet MUST BE at the last ack pos position, so check, just in case.
    int n = m_iLastAckPos;
    if (m_pUnit[n] && m_pUnit[n]->m_iFlag == CUnit::GOOD)
    {
        high_ts  = m_pUnit[n]->m_Packet.m_iTimeStamp;
        high_seq = m_pUnit[n]->m_Packet.m_iSeqNo;
    }
    else
    {
        // Possibilities are:
        // m_iStartPos == m_iLastAckPos, high_ts == low_ts, defined.
        // No packet: low_ts == 0, so high_ts == 0, too.
        high_ts = low_ts;
    }
    // The 32-bit timestamps are relative and roll over oftten; what
    // we really need is the timestamp difference. The only place where
    // we can ask for the time base is the upper time because when trying
    // to receive the time base for the lower time we'd break the requirement
    // for monotonic clock.

    uint64_t upper_time = high_ts;
    uint64_t lower_time = low_ts;

    if (lower_time > upper_time)
        upper_time += uint64_t(CPacket::MAX_TIMESTAMP) + 1;

    int32_t timespan = upper_time - lower_time;
    int     seqspan  = 0;
    if (low_seq != SRT_SEQNO_NONE && high_seq != SRT_SEQNO_NONE)
    {
        seqspan = CSeqNo::seqoff(low_seq, high_seq);
    }

    LOGC(brlog.Debug,
         log << "RCV BUF STATS: seqspan=%(" << low_seq << "-" << high_seq << ":" << seqspan << ") missing=" << nmissing
             << "pkts");
    LOGC(brlog.Debug,
         log << "RCV BUF STATS: timespan=" << timespan << "us (lo=" << lower_time << " hi=" << upper_time << ")");
}

#endif // ENABLE_HEAVY_LOGGING

bool CRcvBuffer::isRcvDataReady()
{
    steady_clock::time_point tsbpdtime;
    int32_t                  seq;

    return isRcvDataReady((tsbpdtime), (seq), -1);
}

int CRcvBuffer::getAvailBufSize() const
{
    // One slot must be empty in order to tell the difference between "empty buffer" and "full buffer"
    return m_iSize - getRcvDataSize() - 1;
}

int CRcvBuffer::getRcvDataSize() const
{
    if (m_iLastAckPos >= m_iStartPos)
        return m_iLastAckPos - m_iStartPos;

    return m_iSize + m_iLastAckPos - m_iStartPos;
}

int CRcvBuffer::debugGetSize() const
{
    // Does exactly the same as getRcvDataSize, but
    // it should be used FOR INFORMATIONAL PURPOSES ONLY.
    // The source values might be changed in another thread
    // during the calculation, although worst case the
    // resulting value may differ to the real buffer size by 1.
    int from = m_iStartPos, to = m_iLastAckPos;
    int size = to - from;
    if (size < 0)
        size += m_iSize;

    return size;
}

/* Return moving average of acked data pkts, bytes, and timespan (ms) of the receive buffer */
int CRcvBuffer::getRcvAvgDataSize(int& bytes, int& timespan)
{
    // Average number of packets and timespan could be small,
    // so rounding is beneficial, while for the number of
    // bytes in the buffer is a higher value, so rounding can be omitted,
    // but probably better to round all three values.
    timespan = round_val(m_mavg.timespan_ms());
    bytes    = round_val(m_mavg.bytes());
    return round_val(m_mavg.pkts());
}

/* Update moving average of acked data pkts, bytes, and timespan (ms) of the receive buffer */
void CRcvBuffer::updRcvAvgDataSize(const steady_clock::time_point& now)
{
    if (!m_mavg.isTimeToUpdate(now))
        return;

    int       bytes       = 0;
    int       timespan_ms = 0;
    const int pkts        = getRcvDataSize(bytes, timespan_ms);
    m_mavg.update(now, pkts, bytes, timespan_ms);
}

/* Return acked data pkts, bytes, and timespan (ms) of the receive buffer */
int CRcvBuffer::getRcvDataSize(int& bytes, int& timespan)
{
    timespan = 0;
    if (m_tsbpd.isEnabled())
    {
        // Get a valid startpos.
        // Skip invalid entries in the beginning, if any.
        int startpos = m_iStartPos;
        for (; startpos != m_iLastAckPos; startpos = shiftFwd(startpos))
        {
            if ((NULL != m_pUnit[startpos]) && (CUnit::GOOD == m_pUnit[startpos]->m_iFlag))
                break;
        }

        int endpos = m_iLastAckPos;

        if (m_iLastAckPos != startpos)
        {
            /*
             *     |<--- DataSpan ---->|<- m_iMaxPos ->|
             * +---+---+---+---+---+---+---+---+---+---+---+---
             * |   | 1 | 1 | 1 | 0 | 0 | 1 | 1 | 0 | 1 |   |     m_pUnits[]
             * +---+---+---+---+---+---+---+---+---+---+---+---
             *       |                   |
             *       \_ m_iStartPos      \_ m_iLastAckPos
             *
             * m_pUnits[startpos] shall be valid (->m_iFlag==CUnit::GOOD).
             * If m_pUnits[m_iLastAckPos-1] is not valid (NULL or ->m_iFlag!=CUnit::GOOD),
             * it means m_pUnits[m_iLastAckPos] is valid since a valid unit is needed to skip.
             * Favor m_pUnits[m_iLastAckPos] if valid over [m_iLastAckPos-1] to include the whole acked interval.
             */
            if ((m_iMaxPos <= 0) || (!m_pUnit[m_iLastAckPos]) || (m_pUnit[m_iLastAckPos]->m_iFlag != CUnit::GOOD))
            {
                endpos = (m_iLastAckPos == 0 ? m_iSize - 1 : m_iLastAckPos - 1);
            }

            if ((NULL != m_pUnit[endpos]) && (NULL != m_pUnit[startpos]))
            {
                const steady_clock::time_point startstamp =
                    getPktTsbPdTime(m_pUnit[startpos]->m_Packet.getMsgTimeStamp());
                const steady_clock::time_point endstamp = getPktTsbPdTime(m_pUnit[endpos]->m_Packet.getMsgTimeStamp());
                /*
                 * There are sampling conditions where spantime is < 0 (big unsigned value).
                 * It has been observed after changing the SRT latency from 450 to 200 on the sender.
                 *
                 * Possible packet order corruption when dropping packet,
                 * cause by bad thread protection when adding packet in queue
                 * was later discovered and fixed. Security below kept.
                 *
                 * DateTime                 RecvRate LostRate DropRate AvailBw     RTT   RecvBufs PdDelay
                 * 2014-12-08T15:04:25-0500     4712      110        0   96509  33.710        393     450
                 * 2014-12-08T15:04:35-0500     4512       95        0  107771  33.493 1496542976     200
                 * 2014-12-08T15:04:40-0500     4213      106        3  107352  53.657    9499425     200
                 * 2014-12-08T15:04:45-0500     4575      104        0  102194  53.614      59666     200
                 * 2014-12-08T15:04:50-0500     4475      124        0  100543  53.526        505     200
                 */
                if (endstamp > startstamp)
                    timespan = count_milliseconds(endstamp - startstamp);
            }
            /*
             * Timespan can be less then 1000 us (1 ms) if few packets.
             * Also, if there is only one pkt in buffer, the time difference will be 0.
             * Therefore, always add 1 ms if not empty.
             */
            if (0 < m_iAckedPktsCount)
                timespan += 1;
        }
    }
    HLOGF(brlog.Debug, "getRcvDataSize: %6d %6d %6d ms\n", m_iAckedPktsCount, m_iAckedBytesCount, timespan);
    bytes = m_iAckedBytesCount;
    return m_iAckedPktsCount;
}

unsigned CRcvBuffer::getRcvAvgPayloadSize() const
{
    return m_uAvgPayloadSz;
}

CRcvBuffer::ReadingState CRcvBuffer::debugGetReadingState() const
{
    ReadingState readstate;

    readstate.iNumAcknowledged = 0;
    readstate.iNumUnacknowledged = m_iMaxPos;

    if ((NULL != m_pUnit[m_iStartPos]) && (m_pUnit[m_iStartPos]->m_iFlag == CUnit::GOOD))
    {
        if (m_tsbpd.isEnabled())
            readstate.tsStart = m_tsbpd.getPktTsbPdTime(m_pUnit[m_iStartPos]->m_Packet.getMsgTimeStamp());

        readstate.iNumAcknowledged = m_iLastAckPos > m_iStartPos
            ? m_iLastAckPos - m_iStartPos
            : m_iLastAckPos + (m_iSize - m_iStartPos);
    }

    // All further stats are valid if TSBPD is enabled.
    if (!m_tsbpd.isEnabled())
        return readstate;

    // m_iLastAckPos points to the first unacknowledged packet
    const int iLastAckPos = (m_iLastAckPos - 1) % m_iSize;
    if (m_iLastAckPos != m_iStartPos && (NULL != m_pUnit[iLastAckPos]) && (m_pUnit[iLastAckPos]->m_iFlag == CUnit::GOOD))
    {
        readstate.tsLastAck = m_tsbpd.getPktTsbPdTime(m_pUnit[iLastAckPos]->m_Packet.getMsgTimeStamp());
    }

    const int iEndPos = (m_iLastAckPos + m_iMaxPos - 1) % m_iSize;
    if (m_iMaxPos == 0)
    {
        readstate.tsEnd = readstate.tsLastAck;
    }
    else if ((NULL != m_pUnit[iEndPos]) && (m_pUnit[iEndPos]->m_iFlag == CUnit::GOOD))
    {
        readstate.tsEnd = m_tsbpd.getPktTsbPdTime(m_pUnit[iEndPos]->m_Packet.getMsgTimeStamp());
    }

    return readstate;
}

string CRcvBuffer::strFullnessState(const time_point& tsNow) const
{
    const ReadingState bufstate = debugGetReadingState();
    stringstream ss;

    ss << "Space avail " << getAvailBufSize() << "/" << m_iSize;
    ss << " pkts. Packets ACKed: " << bufstate.iNumAcknowledged;
    if (!is_zero(bufstate.tsStart) && !is_zero(bufstate.tsLastAck))
    {
        ss << " (TSBPD ready in ";
        ss << count_milliseconds(bufstate.tsStart - tsNow);
        ss << " : ";
        ss << count_milliseconds(bufstate.tsLastAck - tsNow);
        ss << " ms)";
    }

    ss << ", not ACKed: " << bufstate.iNumUnacknowledged;
    if (!is_zero(bufstate.tsStart) && !is_zero(bufstate.tsEnd))
    {
        ss << ", timespan ";
        ss << count_milliseconds(bufstate.tsEnd - bufstate.tsStart);
        ss << " ms";
    }

    ss << ". " SRT_SYNC_CLOCK_STR " drift " << getDrift() / 1000 << " ms.";
    return ss.str();
}

void CRcvBuffer::dropMsg(int32_t msgno, bool using_rexmit_flag)
{
    for (int i = m_iStartPos, n = shift(m_iLastAckPos, m_iMaxPos); i != n; i = shiftFwd(i))
        if ((m_pUnit[i] != NULL) && (m_pUnit[i]->m_Packet.getMsgSeq(using_rexmit_flag) == msgno))
            m_pUnit[i]->m_iFlag = CUnit::DROPPED;
}

void CRcvBuffer::applyGroupTime(const steady_clock::time_point& timebase,
                                bool                            wrp,
                                uint32_t                        delay,
                                const steady_clock::duration&   udrift)
{
    m_tsbpd.applyGroupTime(timebase, wrp, delay, udrift);
}

void CRcvBuffer::applyGroupDrift(const steady_clock::time_point& timebase,
                                 bool                            wrp,
                                 const steady_clock::duration&   udrift)
{
    m_tsbpd.applyGroupDrift(timebase, wrp, udrift);
}

void CRcvBuffer::getInternalTimeBase(steady_clock::time_point& w_timebase, bool& w_wrp, steady_clock::duration& w_udrift)
{
    return m_tsbpd.getInternalTimeBase(w_timebase, w_wrp, w_udrift);
}

steady_clock::time_point CRcvBuffer::getPktTsbPdTime(uint32_t usPktTimestamp)
{
    // Updating TSBPD time here is not very accurate and prevents from making the function constant.
    // For now preserving the existing behavior.
    m_tsbpd.updateTsbPdTimeBase(usPktTimestamp);
    return m_tsbpd.getPktTsbPdTime(usPktTimestamp);
}

void CRcvBuffer::setRcvTsbPdMode(const steady_clock::time_point& timebase, const steady_clock::duration& delay)
{
    const bool no_wrap_check = false;
    m_tsbpd.setTsbPdMode(timebase, no_wrap_check, delay);
}

bool CRcvBuffer::addRcvTsbPdDriftSample(uint32_t timestamp_us, const time_point& tsPktArrival, int rtt)
{
    return m_tsbpd.addDriftSample(timestamp_us, tsPktArrival, rtt);
}

int CRcvBuffer::readMsg(char* data, int len)
{
    SRT_MSGCTRL dummy = srt_msgctrl_default;
    return readMsg(data, len, (dummy), -1);
}

// NOTE: The order of ref-arguments is odd because:
// - data and len shall be close to one another
// - upto is last because it's a kind of unusual argument that has a default value
int CRcvBuffer::readMsg(char* data, int len, SRT_MSGCTRL& w_msgctl, int upto)
{
    int  p = -1, q = -1;
    bool passack;

    bool empty = accessMsg((p), (q), (passack), (w_msgctl.srctime), upto);
    if (empty)
        return 0;

    // This should happen just once. By 'empty' condition
    // we have a guarantee that m_pUnit[p] exists and is valid.
    CPacket& pkt1 = m_pUnit[p]->m_Packet;

    // This returns the sequence number and message number to
    // the API caller.
    w_msgctl.pktseq = pkt1.getSeqNo();
    w_msgctl.msgno  = pkt1.getMsgSeq();

    return extractData((data), len, p, q, passack);
}

#ifdef SRT_DEBUG_TSBPD_OUTJITTER
void CRcvBuffer::debugTraceJitter(time_point playtime)
{
    uint64_t ms = count_microseconds(steady_clock::now() - playtime);
    if (ms / 10 < 10)
        m_ulPdHisto[0][ms / 10]++;
    else if (ms / 100 < 10)
        m_ulPdHisto[1][ms / 100]++;
    else if (ms / 1000 < 10)
        m_ulPdHisto[2][ms / 1000]++;
    else
        m_ulPdHisto[3][1]++;
}
#endif /* SRT_DEBUG_TSBPD_OUTJITTER */

bool CRcvBuffer::accessMsg(int& w_p, int& w_q, bool& w_passack, int64_t& w_playtime, int upto)
{
    // This function should do the following:
    // 1. Find the first packet starting the next message (or just next packet)
    // 2. When found something ready for extraction, return true.
    // 3. w_p and w_q point the index range for extraction
    // 4. passack decides if this range shall be removed after extraction

    bool empty = true;

    if (m_tsbpd.isEnabled())
    {
        w_passack = false;
        int seq   = 0;

        steady_clock::time_point play_time;
        const bool               isReady = getRcvReadyMsg(play_time, (seq), upto);
        w_playtime                       = count_microseconds(play_time.time_since_epoch());

        if (isReady)
        {
            empty = false;
            // In TSBPD mode you always read one message
            // at a time and a message always fits in one UDP packet,
            // so in one "unit".
            w_p = w_q = m_iStartPos;

            debugTraceJitter(play_time);
        }
    }
    else
    {
        w_playtime = 0;
        if (scanMsg((w_p), (w_q), (w_passack)))
            empty = false;
    }

    return empty;
}

int CRcvBuffer::extractData(char* data, int len, int p, int q, bool passack)
{
    SRT_ASSERT(len > 0);
    int       rs     = len > 0 ? len : 0;
    const int past_q = shiftFwd(q);
    while (p != past_q)
    {
        const int pktlen = (int)m_pUnit[p]->m_Packet.getLength();
        // When unitsize is less than pktlen, only a fragment is copied to the output 'data',
        // but still the whole packet is removed from the receiver buffer.
        if (pktlen > 0)
            countBytes(-1, -pktlen, true);

        const int unitsize = ((rs >= 0) && (pktlen > rs)) ? rs : pktlen;

        HLOGC(brlog.Debug, log << "readMsg: checking unit POS=" << p);

        if (unitsize > 0)
        {
            memcpy((data), m_pUnit[p]->m_Packet.m_pcData, unitsize);
            data += unitsize;
            rs -= unitsize;
            IF_HEAVY_LOGGING(readMsgHeavyLogging(p));
        }
        else
        {
            HLOGC(brlog.Debug, log << CONID() << "readMsg: SKIPPED POS=" << p << " - ZERO SIZE UNIT");
        }

        // Note special case for live mode (one packet per message and TSBPD=on):
        //  - p == q (that is, this loop passes only once)
        //  - no passack (the unit is always removed from the buffer)
        if (!passack)
        {
            HLOGC(brlog.Debug, log << CONID() << "readMsg: FREEING UNIT POS=" << p);
            freeUnitAt(p);
        }
        else
        {
            HLOGC(brlog.Debug, log << CONID() << "readMsg: PASSACK UNIT POS=" << p);
            m_pUnit[p]->m_iFlag = CUnit::PASSACK;
        }

        p = shiftFwd(p);
    }

    if (!passack)
        m_iStartPos = past_q;

    HLOGC(brlog.Debug,
          log << "rcvBuf/extractData: begin=" << m_iStartPos << " reporting extraction size=" << (len - rs));

    return len - rs;
}

string CRcvBuffer::debugTimeState(size_t first_n_pkts) const
{
    stringstream ss;
    int          ipos = m_iStartPos;
    for (size_t i = 0; i < first_n_pkts; ++i, ipos = CSeqNo::incseq(ipos))
    {
        const CUnit* unit = m_pUnit[ipos];
        if (!unit)
        {
            ss << "pkt[" << i << "] missing, ";
            continue;
        }

        const CPacket& pkt = unit->m_Packet;
        ss << "pkt[" << i << "] ts=" << pkt.getMsgTimeStamp() << ", ";
    }
    return ss.str();
}

#if ENABLE_HEAVY_LOGGING
void CRcvBuffer::readMsgHeavyLogging(int p)
{
    static steady_clock::time_point prev_now;
    static steady_clock::time_point prev_srctime;
    const CPacket&                  pkt = m_pUnit[p]->m_Packet;

    const int32_t seq = pkt.m_iSeqNo;

    steady_clock::time_point nowtime = steady_clock::now();
    steady_clock::time_point srctime = getPktTsbPdTime(m_pUnit[p]->m_Packet.getMsgTimeStamp());

    const int64_t timediff_ms    = count_milliseconds(nowtime - srctime);
    const int64_t nowdiff_ms     = is_zero(prev_now) ? count_milliseconds(nowtime - prev_now) : 0;
    const int64_t srctimediff_ms = is_zero(prev_srctime) ? count_milliseconds(srctime - prev_srctime) : 0;

    const int next_p = shiftFwd(p);
    CUnit*    u      = m_pUnit[next_p];
    string    next_playtime;
    if (u && u->m_iFlag == CUnit::GOOD)
    {
        next_playtime = FormatTime(getPktTsbPdTime(u->m_Packet.getMsgTimeStamp()));
    }
    else
    {
        next_playtime = "NONE";
    }

    LOGC(brlog.Debug,
         log << CONID() << "readMsg: DELIVERED seq=" << seq << " T=" << FormatTime(srctime) << " in " << timediff_ms
             << "ms - TIME-PREVIOUS: PKT: " << srctimediff_ms << " LOCAL: " << nowdiff_ms << " !"
             << BufferStamp(pkt.data(), pkt.size()) << " NEXT pkt T=" << next_playtime);

    prev_now     = nowtime;
    prev_srctime = srctime;
}
#endif

bool CRcvBuffer::scanMsg(int& w_p, int& w_q, bool& w_passack)
{
    // empty buffer
    if ((m_iStartPos == m_iLastAckPos) && (m_iMaxPos <= 0))
    {
        HLOGC(brlog.Debug, log << "scanMsg: empty buffer");
        return false;
    }

    int rmpkts  = 0;
    int rmbytes = 0;
    // skip all bad msgs at the beginning
    // This loop rolls until the "buffer is empty" (head == tail),
    // in particular, there's no unit accessible for the reader.
    while (m_iStartPos != m_iLastAckPos)
    {
        // Roll up to the first valid unit
        if (!m_pUnit[m_iStartPos])
        {
            if (++m_iStartPos == m_iSize)
                m_iStartPos = 0;
            continue;
        }

        // Note: PB_FIRST | PB_LAST == PB_SOLO.
        // testing if boundary() & PB_FIRST tests if the msg is first OR solo.
        if (m_pUnit[m_iStartPos]->m_iFlag == CUnit::GOOD && m_pUnit[m_iStartPos]->m_Packet.getMsgBoundary() & PB_FIRST)
        {
            bool good = true;

            // look ahead for the whole message

            // We expect to see either of:
            // [PB_FIRST] [PB_SUBSEQUENT] [PB_SUBSEQUENT] [PB_LAST]
            // [PB_SOLO]
            // but not:
            // [PB_FIRST] NULL ...
            // [PB_FIRST] FREE/PASSACK/DROPPED...
            // If the message didn't look as expected, interrupt this.

            // This begins with a message starting at m_iStartPos
            // up to m_iLastAckPos OR until the PB_LAST message is found.
            // If any of the units on this way isn't good, this OUTER loop
            // will be interrupted.
            for (int i = m_iStartPos; i != m_iLastAckPos;)
            {
                if (!m_pUnit[i] || m_pUnit[i]->m_iFlag != CUnit::GOOD)
                {
                    good = false;
                    break;
                }

                // Likewise, boundary() & PB_LAST will be satisfied for last OR solo.
                if (m_pUnit[i]->m_Packet.getMsgBoundary() & PB_LAST)
                    break;

                if (++i == m_iSize)
                    i = 0;
            }

            if (good)
                break;
        }

        rmpkts++;
        rmbytes += (int) freeUnitAt((size_t) m_iStartPos);

        m_iStartPos = shiftFwd(m_iStartPos);
    }
    /* we removed bytes form receive buffer */
    countBytes(-rmpkts, -rmbytes, true);

    // Not sure if this is correct, but this above 'while' loop exits
    // under the following conditions only:
    // - m_iStartPos == m_iLastAckPos (that makes passack = true)
    // - found at least GOOD unit with PB_FIRST and not all messages up to PB_LAST are good,
    //   in which case it returns with m_iStartPos <% m_iLastAckPos (earlier)
    // Also all units that lied before m_iStartPos are removed.

    w_p        = -1;          // message head
    w_q        = m_iStartPos; // message tail
    w_passack  = m_iStartPos == m_iLastAckPos;
    bool found = false;

    // looking for the first message
    //>>m_pUnit[size + m_iMaxPos] is not valid

    // XXX Would be nice to make some very thorough refactoring here.

    // This rolls by q variable from m_iStartPos up to m_iLastAckPos,
    // actually from the first message up to the one with PB_LAST
    // or PB_SOLO boundary.

    // The 'i' variable used in this loop is just a stub and it's
    // even hard to define the unit here. It is "shift towards
    // m_iStartPos", so the upper value is m_iMaxPos + size.
    // m_iMaxPos is itself relative to m_iLastAckPos, so
    // the upper value is m_iMaxPos + difference between
    // m_iLastAckPos and m_iStartPos, so that this value is relative
    // to m_iStartPos.
    //
    // The 'i' value isn't used anywhere, although the 'q' value rolls
    // in this loop in sync with 'i', with the difference that 'q' is
    // wrapped around, and 'i' is just incremented normally.
    //
    // This makes that this loop rolls in the range by 'q' from
    // m_iStartPos to m_iStartPos + UPPER,
    // where UPPER = m_iLastAckPos -% m_iStartPos + m_iMaxPos
    // This embraces the range from the current reading head up to
    // the last packet ever received.
    //
    // 'passack' is set to true when the 'q' has passed through
    // the border of m_iLastAckPos and fallen into the range
    // of unacknowledged packets.

    for (int i = 0, n = m_iMaxPos + getRcvDataSize(); i < n; ++i)
    {
        if (m_pUnit[w_q] && m_pUnit[w_q]->m_iFlag == CUnit::GOOD)
        {
            // Equivalent pseudocode:
            // PacketBoundary bound = m_pUnit[w_q]->m_Packet.getMsgBoundary();
            // if ( IsSet(bound, PB_FIRST) )
            //     w_p = w_q;
            // if ( IsSet(bound, PB_LAST) && w_p != -1 )
            //     found = true;
            //
            // Not implemented this way because it uselessly check w_p for -1
            // also after setting it explicitly.

            switch (m_pUnit[w_q]->m_Packet.getMsgBoundary())
            {
            case PB_SOLO: // 11
                w_p   = w_q;
                found = true;
                break;

            case PB_FIRST: // 10
                w_p = w_q;
                break;

            case PB_LAST: // 01
                if (w_p != -1)
                    found = true;
                break;

            case PB_SUBSEQUENT:; // do nothing (caught first, rolling for last)
            }
        }
        else
        {
            // a hole in this message, not valid, restart search
            w_p = -1;
        }

        // 'found' is set when the current iteration hit a message with PB_LAST
        // (including PB_SOLO since the very first message).
        if (found)
        {
            // the msg has to be ack'ed or it is allowed to read out of order, and was not read before
            if (!w_passack || !m_pUnit[w_q]->m_Packet.getMsgOrderFlag())
            {
                HLOGC(brlog.Debug, log << "scanMsg: found next-to-broken message, delivering OUT OF ORDER.");
                break;
            }

            found = false;
        }

        if (++w_q == m_iSize)
            w_q = 0;

        if (w_q == m_iLastAckPos)
            w_passack = true;
    }

    // no msg found
    if (!found)
    {
        // NOTE:
        // This situation may only happen if:
        // - Found a packet with PB_FIRST, so w_p = w_q at the moment when it was found
        // - Possibly found following components of that message up to shifted w_q
        // - Found no terminal packet (PB_LAST) for that message.

        // if the message is larger than the receiver buffer, return part of the message
        if ((w_p != -1) && (shiftFwd(w_q) == w_p))
        {
            HLOGC(brlog.Debug, log << "scanMsg: BUFFER FULL and message is INCOMPLETE. Returning PARTIAL MESSAGE.");
            found = true;
        }
        else
        {
            HLOGC(brlog.Debug, log << "scanMsg: PARTIAL or NO MESSAGE found: p=" << w_p << " q=" << w_q);
        }
    }
    else
    {
        HLOGC(brlog.Debug,
              log << "scanMsg: extracted message p=" << w_p << " q=" << w_q << " ("
                  << ((w_q - w_p + m_iSize + 1) % m_iSize) << " packets)");
    }

    return found;
}

#endif // !ENABLE_NEW_RCVBUFFER

} // namespace srt
