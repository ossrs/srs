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

#include <cmath>
#include "buffer_snd.h"
#include "packet.h"
#include "core.h" // provides some constants
#include "logging.h"

namespace srt {

using namespace std;
using namespace srt_logging;
using namespace sync;

CSndBuffer::CSndBuffer(int size, int maxpld, int authtag)
    : m_BufLock()
    , m_pBlock(NULL)
    , m_pFirstBlock(NULL)
    , m_pCurrBlock(NULL)
    , m_pLastBlock(NULL)
    , m_pBuffer(NULL)
    , m_iNextMsgNo(1)
    , m_iSize(size)
    , m_iBlockLen(maxpld)
    , m_iAuthTagSize(authtag)
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
    int32_t& w_msgno     = w_mctrl.msgno;
    int32_t& w_seqno     = w_mctrl.pktseq;
    int64_t& w_srctime   = w_mctrl.srctime;
    const int& ttl       = w_mctrl.msgttl;
    const int iPktLen    = getMaxPacketLen();
    const int iNumBlocks = countNumPacketsRequired(len, iPktLen);

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
    const int iPktLen    = getMaxPacketLen();
    const int iNumBlocks = countNumPacketsRequired(len, iPktLen);

    HLOGC(bslog.Debug,
          log << "addBufferFromFile: size=" << m_iCount << " reserved=" << m_iSize << " needs=" << iPktLen
              << " buffers for " << len << " bytes");

    // dynamically increase sender buffer
    while (iNumBlocks + m_iCount >= m_iSize)
    {
        HLOGC(bslog.Debug,
              log << "addBufferFromFile: ... still lacking " << (iNumBlocks + m_iCount - m_iSize) << " buffers...");
        increase();
    }

    HLOGC(bslog.Debug,
          log << CONID() << "addBufferFromFile: adding " << iPktLen << " packets (" << len
              << " bytes) to send, msgno=" << m_iNextMsgNo);

    Block* s     = m_pLastBlock;
    int    total = 0;
    for (int i = 0; i < iNumBlocks; ++i)
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
        if (i == iNumBlocks - 1)
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
    m_iCount += iNumBlocks;
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
        w_packet.setLength(readlen, m_iBlockLen);
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
    w_packet.setLength(readlen, m_iBlockLen);

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

int CSndBuffer::getMaxPacketLen() const
{
    return m_iBlockLen - m_iAuthTagSize;
}

int CSndBuffer::countNumPacketsRequired(int iPldLen) const
{
    const int iPktLen = getMaxPacketLen();
    return countNumPacketsRequired(iPldLen, iPktLen);
}

int CSndBuffer::countNumPacketsRequired(int iPldLen, int iPktLen) const
{
    return (iPldLen + iPktLen - 1) / iPktLen;
}

namespace {
int round_val(double val)
{
    return static_cast<int>(round(val));
}
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

int CSndBuffer::getCurrBufSize(int& w_bytes, int& w_timespan) const
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

} // namespace srt
