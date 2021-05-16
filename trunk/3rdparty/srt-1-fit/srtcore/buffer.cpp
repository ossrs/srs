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

#include <cstring>
#include <cmath>
#include "buffer.h"
#include "packet.h"
#include "core.h" // provides some constants
#include "logging.h"

using namespace std;
using namespace srt_logging;

CSndBuffer::CSndBuffer(int size, int mss)
    : m_BufLock()
    , m_pBlock(NULL)
    , m_pFirstBlock(NULL)
    , m_pCurrBlock(NULL)
    , m_pLastBlock(NULL)
    , m_pBuffer(NULL)
    , m_iNextMsgNo(1)
    , m_iSize(size)
    , m_iMSS(mss)
    , m_iCount(0)
    , m_iBytesCount(0)
    , m_ullLastOriginTime_us(0)
#ifdef SRT_ENABLE_SNDBUFSZ_MAVG
    , m_LastSamplingTime(0)
    , m_iCountMAvg(0)
    , m_iBytesCountMAvg(0)
    , m_TimespanMAvg(0)
#endif
    , m_iInRatePktsCount(0)
    , m_iInRateBytesCount(0)
    , m_InRateStartTime(0)
    , m_InRatePeriod(INPUTRATE_FAST_START_US)   // 0.5 sec (fast start)
    , m_iInRateBps(INPUTRATE_INITIAL_BYTESPS)
{
   // initial physical buffer of "size"
   m_pBuffer = new Buffer;
   m_pBuffer->m_pcData = new char [m_iSize * m_iMSS];
   m_pBuffer->m_iSize = m_iSize;
   m_pBuffer->m_pNext = NULL;

   // circular linked list for out bound packets
   m_pBlock = new Block;
   Block* pb = m_pBlock;
   for (int i = 1; i < m_iSize; ++ i)
   {
      pb->m_pNext = new Block;
      pb->m_iMsgNoBitset = 0;
      pb = pb->m_pNext;
   }
   pb->m_pNext = m_pBlock;

   pb = m_pBlock;
   char* pc = m_pBuffer->m_pcData;
   for (int i = 0; i < m_iSize; ++ i)
   {
      pb->m_pcData = pc;
      pb = pb->m_pNext;
      pc += m_iMSS;
   }

   m_pFirstBlock = m_pCurrBlock = m_pLastBlock = m_pBlock;

   pthread_mutex_init(&m_BufLock, NULL);
}

CSndBuffer::~CSndBuffer()
{
   Block* pb = m_pBlock->m_pNext;
   while (pb != m_pBlock)
   {
      Block* temp = pb;
      pb = pb->m_pNext;
      delete temp;
   }
   delete m_pBlock;

   while (m_pBuffer != NULL)
   {
      Buffer* temp = m_pBuffer;
      m_pBuffer = m_pBuffer->m_pNext;
      delete [] temp->m_pcData;
      delete temp;
   }

   pthread_mutex_destroy(&m_BufLock);
}

void CSndBuffer::addBuffer(const char* data, int len, int ttl, bool order, uint64_t srctime, ref_t<int32_t> r_msgno)
{
    int32_t& msgno = *r_msgno;

    int size = len / m_iMSS;
    if ((len % m_iMSS) != 0)
        size ++;

    HLOGC(mglog.Debug, log << "addBuffer: size=" << m_iCount << " reserved=" << m_iSize << " needs=" << size << " buffers for " << len << " bytes");

    // dynamically increase sender buffer
    while (size + m_iCount >= m_iSize)
    {
        HLOGC(mglog.Debug, log << "addBuffer: ... still lacking " << (size + m_iCount - m_iSize) << " buffers...");
        increase();
    }

    const uint64_t time = CTimer::getTime();
    int32_t inorder = order ? MSGNO_PACKET_INORDER::mask : 0;

    HLOGC(dlog.Debug, log << CONID() << "addBuffer: adding "
        << size << " packets (" << len << " bytes) to send, msgno=" << m_iNextMsgNo
        << (inorder ? "" : " NOT") << " in order");

    Block* s = m_pLastBlock;
    msgno = m_iNextMsgNo;
    for (int i = 0; i < size; ++ i)
    {
        int pktlen = len - i * m_iMSS;
        if (pktlen > m_iMSS)
            pktlen = m_iMSS;

        HLOGC(dlog.Debug, log << "addBuffer: spreading from=" << (i*m_iMSS) << " size=" << pktlen << " TO BUFFER:" << (void*)s->m_pcData);
        memcpy(s->m_pcData, data + i * m_iMSS, pktlen);
        s->m_iLength = pktlen;

        s->m_iMsgNoBitset = m_iNextMsgNo | inorder;
        if (i == 0)
            s->m_iMsgNoBitset |= PacketBoundaryBits(PB_FIRST);
        if (i == size - 1)
            s->m_iMsgNoBitset |= PacketBoundaryBits(PB_LAST);
        // NOTE: if i is neither 0 nor size-1, it resuls with PB_SUBSEQUENT.
        //       if i == 0 == size-1, it results with PB_SOLO. 
        // Packets assigned to one message can be:
        // [PB_FIRST] [PB_SUBSEQUENT] [PB_SUBSEQUENT] [PB_LAST] - 4 packets per message
        // [PB_FIRST] [PB_LAST] - 2 packets per message
        // [PB_SOLO] - 1 packet per message

        s->m_ullSourceTime_us = srctime;
        s->m_ullOriginTime_us = time;
        s->m_iTTL = ttl;

        // XXX unchecked condition: s->m_pNext == NULL.
        // Should never happen, as the call to increase() should ensure enough buffers.
        SRT_ASSERT(s->m_pNext);
        s = s->m_pNext;
    }
    m_pLastBlock = s;

    CGuard::enterCS(m_BufLock);
    m_iCount += size;

    m_iBytesCount += len;
    m_ullLastOriginTime_us = time;

    updateInputRate(time, size, len);

#ifdef SRT_ENABLE_SNDBUFSZ_MAVG
    updAvgBufSize(time);
#endif

    CGuard::leaveCS(m_BufLock);


    // MSGNO_SEQ::mask has a form: 00000011111111...
    // At least it's known that it's from some index inside til the end (to bit 0).
    // If this value has been reached in a step of incrementation, it means that the
    // maximum value has been reached. Casting to int32_t to ensure the same sign
    // in comparison, although it's far from reaching the sign bit.

    m_iNextMsgNo ++;
    if (m_iNextMsgNo == int32_t(MSGNO_SEQ::mask))
        m_iNextMsgNo = 1;
}

void CSndBuffer::setInputRateSmpPeriod(int period)
{
    m_InRatePeriod = (uint64_t)period; //(usec) 0=no input rate calculation
}

void CSndBuffer::updateInputRate(uint64_t time, int pkts, int bytes)
{
    //no input rate calculation
    if (m_InRatePeriod == 0)
        return;

    if (m_InRateStartTime == 0)
    {
        m_InRateStartTime = time;
        return;
    }

    m_iInRatePktsCount  += pkts;
    m_iInRateBytesCount += bytes;

    // Trigger early update in fast start mode
    const bool early_update = (m_InRatePeriod < INPUTRATE_RUNNING_US)
        && (m_iInRatePktsCount > INPUTRATE_MAX_PACKETS);

    const uint64_t period_us = (time - m_InRateStartTime);
    if (early_update || period_us > m_InRatePeriod)
    {
        //Required Byte/sec rate (payload + headers)
        m_iInRateBytesCount += (m_iInRatePktsCount * CPacket::SRT_DATA_HDR_SIZE);
        m_iInRateBps = (int)(((int64_t)m_iInRateBytesCount * 1000000) / period_us);
        HLOGC(dlog.Debug, log << "updateInputRate: pkts:" << m_iInRateBytesCount << " bytes:" << m_iInRatePktsCount
                << " rate=" << (m_iInRateBps*8)/1000
                << "kbps interval=" << period_us);
        m_iInRatePktsCount = 0;
        m_iInRateBytesCount = 0;
        m_InRateStartTime = time;

        setInputRateSmpPeriod(INPUTRATE_RUNNING_US);
    }
}


int CSndBuffer::addBufferFromFile(fstream& ifs, int len)
{
   int size = len / m_iMSS;
   if ((len % m_iMSS) != 0)
      size ++;

   HLOGC(mglog.Debug, log << "addBufferFromFile: size=" << m_iCount << " reserved=" << m_iSize << " needs=" << size << " buffers for " << len << " bytes");

   // dynamically increase sender buffer
   while (size + m_iCount >= m_iSize)
   {
      HLOGC(mglog.Debug, log << "addBufferFromFile: ... still lacking " << (size + m_iCount - m_iSize) << " buffers...");
      increase();
   }

   HLOGC(dlog.Debug, log << CONID() << "addBufferFromFile: adding "
       << size << " packets (" << len << " bytes) to send, msgno=" << m_iNextMsgNo);

   Block* s = m_pLastBlock;
   int total = 0;
   for (int i = 0; i < size; ++ i)
   {
      if (ifs.bad() || ifs.fail() || ifs.eof())
         break;

      int pktlen = len - i * m_iMSS;
      if (pktlen > m_iMSS)
         pktlen = m_iMSS;

      HLOGC(dlog.Debug, log << "addBufferFromFile: reading from=" << (i*m_iMSS) << " size=" << pktlen << " TO BUFFER:" << (void*)s->m_pcData);
      ifs.read(s->m_pcData, pktlen);
      if ((pktlen = int(ifs.gcount())) <= 0)
         break;

      // currently file transfer is only available in streaming mode, message is always in order, ttl = infinite
      s->m_iMsgNoBitset = m_iNextMsgNo | MSGNO_PACKET_INORDER::mask;
      if (i == 0)
         s->m_iMsgNoBitset |= PacketBoundaryBits(PB_FIRST);
      if (i == size - 1)
         s->m_iMsgNoBitset |= PacketBoundaryBits(PB_LAST);
      // NOTE: PB_FIRST | PB_LAST == PB_SOLO.
      // none of PB_FIRST & PB_LAST == PB_SUBSEQUENT.

      s->m_iLength = pktlen;
      s->m_iTTL = -1;
      s = s->m_pNext;

      total += pktlen;
   }
   m_pLastBlock = s;

   CGuard::enterCS(m_BufLock);
   m_iCount += size;
   m_iBytesCount += total;

   CGuard::leaveCS(m_BufLock);

   m_iNextMsgNo ++;
   if (m_iNextMsgNo == int32_t(MSGNO_SEQ::mask))
      m_iNextMsgNo = 1;

   return total;
}

int CSndBuffer::readData(char** data, int32_t& msgno_bitset, uint64_t& srctime, int kflgs)
{
   // No data to read
   if (m_pCurrBlock == m_pLastBlock)
      return 0;

   // Make the packet REFLECT the data stored in the buffer.
   *data = m_pCurrBlock->m_pcData;
   int readlen = m_pCurrBlock->m_iLength;

   // XXX This is probably done because the encryption should happen
   // just once, and so this sets the encryption flags to both msgno bitset
   // IN THE PACKET and IN THE BLOCK. This is probably to make the encryption
   // happen at the time when scheduling a new packet to send, but the packet
   // must remain in the send buffer until it's ACKed. For the case of rexmit
   // the packet will be taken "as is" (that is, already encrypted).
   //
   // The problem is in the order of things:
   // 0. When the application stores the data, some of the flags for PH_MSGNO are set.
   // 1. The readData() is called to get the original data sent by the application.
   // 2. The data are original and must be encrypted. They WILL BE encrypted, later.
   // 3. So far we are in readData() so the encryption flags must be updated NOW because
   //    later we won't have access to the block's data.
   // 4. After exiting from readData(), the packet is being encrypted. It's immediately
   //    sent, however the data must remain in the sending buffer until they are ACKed.
   // 5. In case when rexmission is needed, the second overloaded version of readData
   //    is being called, and the buffer + PH_MSGNO value is extracted. All interesting
   //    flags must be present and correct at that time.
   //
   // The only sensible way to fix this problem is to encrypt the packet not after
   // extracting from here, but when the packet is stored into CSndBuffer. The appropriate
   // flags for PH_MSGNO will be applied directly there. Then here the value for setting
   // PH_MSGNO will be set as is.

   if (kflgs == -1)
   {
       HLOGC(dlog.Debug, log << CONID() << " CSndBuffer: ERROR: encryption required and not possible. NOT SENDING.");
       readlen = 0;
   }
   else
   {
       m_pCurrBlock->m_iMsgNoBitset |= MSGNO_ENCKEYSPEC::wrap(kflgs);
   }
   msgno_bitset = m_pCurrBlock->m_iMsgNoBitset;

   srctime =
      m_pCurrBlock->m_ullSourceTime_us ? m_pCurrBlock->m_ullSourceTime_us :
      m_pCurrBlock->m_ullOriginTime_us;

   m_pCurrBlock = m_pCurrBlock->m_pNext;

   HLOGC(dlog.Debug, log << CONID() << "CSndBuffer: extracting packet size=" << readlen << " to send");

   return readlen;
}

int CSndBuffer::readData(char** data, const int offset, int32_t& msgno_bitset, uint64_t& srctime, int& msglen)
{
   CGuard bufferguard(m_BufLock);

   Block* p = m_pFirstBlock;

   // XXX Suboptimal procedure to keep the blocks identifiable
   // by sequence number. Consider using some circular buffer.
   for (int i = 0; i < offset; ++ i)
      p = p->m_pNext;

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
   if ((p->m_iTTL >= 0) && ((CTimer::getTime() - p->m_ullOriginTime_us) / 1000 > (uint64_t)p->m_iTTL))
   {
      int32_t msgno = p->getMsgSeq();
      msglen = 1;
      p = p->m_pNext;
      bool move = false;
      while (msgno == p->getMsgSeq())
      {
         if (p == m_pCurrBlock)
            move = true;
         p = p->m_pNext;
         if (move)
            m_pCurrBlock = p;
         msglen ++;
      }

      HLOGC(dlog.Debug, log << "CSndBuffer::readData: due to TTL exceeded, " << msglen << " messages to drop, up to " << msgno);

      // If readData returns -1, then msgno_bitset is understood as a Message ID to drop.
      // This means that in this case it should be written by the message sequence value only
      // (not the whole 4-byte bitset written at PH_MSGNO).
      msgno_bitset = msgno;
      return -1;
   }

   *data = p->m_pcData;
   int readlen = p->m_iLength;

   // XXX Here the value predicted to be applied to PH_MSGNO field is extracted.
   // As this function is predicted to extract the data to send as a rexmited packet,
   // the packet must be in the form ready to send - so, in case of encryption,
   // encrypted, and with all ENC flags already set. So, the first call to send
   // the packet originally (the other overload of this function) must set these
   // flags.
   msgno_bitset = p->m_iMsgNoBitset;

   srctime = 
      p->m_ullSourceTime_us ? p->m_ullSourceTime_us :
      p->m_ullOriginTime_us;

   HLOGC(dlog.Debug, log << CONID() << "CSndBuffer: extracting packet size=" << readlen << " to send [REXMIT]");

   return readlen;
}

void CSndBuffer::ackData(int offset)
{
   CGuard bufferguard(m_BufLock);

   bool move = false;
   for (int i = 0; i < offset; ++ i)
   {
      m_iBytesCount -= m_pFirstBlock->m_iLength;
      if (m_pFirstBlock == m_pCurrBlock)
          move = true;
      m_pFirstBlock = m_pFirstBlock->m_pNext;
   }
   if (move)
       m_pCurrBlock = m_pFirstBlock;

   m_iCount -= offset;

#ifdef SRT_ENABLE_SNDBUFSZ_MAVG
   updAvgBufSize(CTimer::getTime());
#endif

   CTimer::triggerEvent();
}

int CSndBuffer::getCurrBufSize() const
{
   return m_iCount;
}

#ifdef SRT_ENABLE_SNDBUFSZ_MAVG

int CSndBuffer::getAvgBufSize(ref_t<int> r_bytes, ref_t<int> r_tsp)
{
    int& bytes = *r_bytes;
    int& timespan = *r_tsp;
    CGuard bufferguard(m_BufLock); /* Consistency of pkts vs. bytes vs. spantime */

    /* update stats in case there was no add/ack activity lately */
    updAvgBufSize(CTimer::getTime());

    bytes = m_iBytesCountMAvg;
    timespan = m_TimespanMAvg;
    return(m_iCountMAvg);
}

void CSndBuffer::updAvgBufSize(uint64_t now)
{
   const uint64_t elapsed_ms = (now - m_LastSamplingTime) / 1000; //ms since last sampling

   if ((1000000 / SRT_MAVG_SAMPLING_RATE) / 1000 > elapsed_ms)
      return;

   if (1000 < elapsed_ms)
   {
      /* No sampling in last 1 sec, initialize average */
      m_iCountMAvg = getCurrBufSize(Ref(m_iBytesCountMAvg), Ref(m_TimespanMAvg));
      m_LastSamplingTime = now;
   } 
   else //((1000000 / SRT_MAVG_SAMPLING_RATE) / 1000 <= elapsed_ms)
   {
      /*
      * weight last average value between -1 sec and last sampling time (LST)
      * and new value between last sampling time and now
      *                                      |elapsed_ms|
      *   +----------------------------------+-------+
      *  -1                                 LST      0(now)
      */
      int instspan;
      int bytescount;
      int count = getCurrBufSize(Ref(bytescount), Ref(instspan));

      HLOGC(dlog.Debug, log << "updAvgBufSize: " << elapsed_ms
              << ": " << count << " " << bytescount
              << " " << instspan << "ms");

      m_iCountMAvg      = (int)(((count      * (1000 - elapsed_ms)) + (count      * elapsed_ms)) / 1000);
      m_iBytesCountMAvg = (int)(((bytescount * (1000 - elapsed_ms)) + (bytescount * elapsed_ms)) / 1000);
      m_TimespanMAvg    = (int)(((instspan   * (1000 - elapsed_ms)) + (instspan   * elapsed_ms)) / 1000);
      m_LastSamplingTime = now;
   }
}

#endif /* SRT_ENABLE_SNDBUFSZ_MAVG */

int CSndBuffer::getCurrBufSize(ref_t<int> bytes, ref_t<int> timespan)
{
   *bytes = m_iBytesCount;
   /* 
   * Timespan can be less then 1000 us (1 ms) if few packets. 
   * Also, if there is only one pkt in buffer, the time difference will be 0.
   * Therefore, always add 1 ms if not empty.
   */
   *timespan = 0 < m_iCount ? int((m_ullLastOriginTime_us - m_pFirstBlock->m_ullOriginTime_us) / 1000) + 1 : 0;

   return m_iCount;
}

int CSndBuffer::dropLateData(int &bytes, uint64_t latetime)
{
   int dpkts = 0;
   int dbytes = 0;
   bool move = false;

   CGuard bufferguard(m_BufLock);
   for (int i = 0; i < m_iCount && m_pFirstBlock->m_ullOriginTime_us < latetime; ++ i)
   {
      dpkts++;
      dbytes += m_pFirstBlock->m_iLength;

      if (m_pFirstBlock == m_pCurrBlock) move = true;
      m_pFirstBlock = m_pFirstBlock->m_pNext;
   }
   if (move) m_pCurrBlock = m_pFirstBlock;
   m_iCount -= dpkts;

   m_iBytesCount -= dbytes;
   bytes = dbytes;

#ifdef SRT_ENABLE_SNDBUFSZ_MAVG
   updAvgBufSize(CTimer::getTime());
#endif /* SRT_ENABLE_SNDBUFSZ_MAVG */

// CTimer::triggerEvent();
   return(dpkts);
}

void CSndBuffer::increase()
{
   int unitsize = m_pBuffer->m_iSize;

   // new physical buffer
   Buffer* nbuf = NULL;
   try
   {
      nbuf  = new Buffer;
      nbuf->m_pcData = new char [unitsize * m_iMSS];
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
   for (int i = 1; i < unitsize; ++ i)
   {
      pb->m_pNext = new Block;
      pb = pb->m_pNext;
   }

   // insert the new blocks onto the existing one
   pb->m_pNext = m_pLastBlock->m_pNext;
   m_pLastBlock->m_pNext = nblk;

   pb = nblk;
   char* pc = nbuf->m_pcData;
   for (int i = 0; i < unitsize; ++ i)
   {
      pb->m_pcData = pc;
      pb = pb->m_pNext;
      pc += m_iMSS;
   }

   m_iSize += unitsize;

   HLOGC(dlog.Debug, log << "CSndBuffer: BUFFER FULL - adding " << (unitsize*m_iMSS) << " bytes spread to " << unitsize << " blocks"
       << " (total size: " << m_iSize << " bytes)");

}

////////////////////////////////////////////////////////////////////////////////

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


// XXX Init values moved to in-class.
//const uint32_t CRcvBuffer::TSBPD_WRAP_PERIOD = (30*1000000);    //30 seconds (in usec)
//const int CRcvBuffer::TSBPD_DRIFT_MAX_VALUE   = 5000;  // usec
//const int CRcvBuffer::TSBPD_DRIFT_MAX_SAMPLES = 1000;  // ACK-ACK packets
#ifdef SRT_DEBUG_TSBPD_DRIFT
//const int CRcvBuffer::TSBPD_DRIFT_PRT_SAMPLES = 200;   // ACK-ACK packets
#endif

CRcvBuffer::CRcvBuffer(CUnitQueue* queue, int bufsize_pkts):
m_pUnit(NULL),
m_iSize(bufsize_pkts),
m_pUnitQueue(queue),
m_iStartPos(0),
m_iLastAckPos(0),
m_iMaxPos(0),
m_iNotch(0)
,m_BytesCountLock()
,m_iBytesCount(0)
,m_iAckedPktsCount(0)
,m_iAckedBytesCount(0)
,m_iAvgPayloadSz(7*188)
,m_bTsbPdMode(false)
,m_uTsbPdDelay(0)
,m_ullTsbPdTimeBase(0)
,m_bTsbPdWrapCheck(false)
//,m_iTsbPdDrift(0)
//,m_TsbPdDriftSum(0)
//,m_iTsbPdDriftNbSamples(0)
#ifdef SRT_ENABLE_RCVBUFSZ_MAVG
,m_LastSamplingTime(0)
,m_TimespanMAvg(0)
,m_iCountMAvg(0)
,m_iBytesCountMAvg(0)
#endif
{
   m_pUnit = new CUnit* [m_iSize];
   for (int i = 0; i < m_iSize; ++ i)
      m_pUnit[i] = NULL;

#ifdef SRT_DEBUG_TSBPD_DRIFT
   memset(m_TsbPdDriftHisto100us, 0, sizeof(m_TsbPdDriftHisto100us));
   memset(m_TsbPdDriftHisto1ms, 0, sizeof(m_TsbPdDriftHisto1ms));
#endif

   pthread_mutex_init(&m_BytesCountLock, NULL);
}

CRcvBuffer::~CRcvBuffer()
{
   for (int i = 0; i < m_iSize; ++ i)
   {
      if (m_pUnit[i] != NULL)
      {
          m_pUnitQueue->makeUnitFree(m_pUnit[i]);
      }
   }

   delete [] m_pUnit;

   pthread_mutex_destroy(&m_BytesCountLock);
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
   CGuard cg(m_BytesCountLock);

   if (!acked) //adding new pkt in RcvBuffer
   {
       m_iBytesCount += bytes; /* added or removed bytes from rcv buffer */
       if (bytes > 0) /* Assuming one pkt when adding bytes */
          m_iAvgPayloadSz = ((m_iAvgPayloadSz * (100 - 1)) + bytes) / 100; 
   }
   else // acking/removing pkts to/from buffer
   {
       m_iAckedPktsCount += pkts; /* acked or removed pkts from rcv buffer */
       m_iAckedBytesCount += bytes; /* acked or removed bytes from rcv buffer */

       if (bytes < 0) m_iBytesCount += bytes; /* removed bytes from rcv buffer */
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

   if (m_pUnit[pos] != NULL) {
      HLOGC(dlog.Debug, log << "addData: unit %" << unit->m_Packet.m_iSeqNo
              << " rejected, already exists");
      return -1;
   }
   m_pUnit[pos] = unit;
   countBytes(1, (int) unit->m_Packet.getLength());

   m_pUnitQueue->makeUnitGood(unit);

   HLOGC(dlog.Debug, log << "addData: unit %" << unit->m_Packet.m_iSeqNo
           << " accepted, off=" << offset << " POS=" << pos);
   return 0;
}

int CRcvBuffer::readBuffer(char* data, int len)
{
    int p = m_iStartPos;
    int lastack = m_iLastAckPos;
    int rs = len;
#if ENABLE_HEAVY_LOGGING
    char* begin = data;
#endif

    const uint64_t now = (m_bTsbPdMode ? CTimer::getTime() : uint64_t());

    HLOGC(dlog.Debug, log << CONID() << "readBuffer: start=" << p << " lastack=" << lastack);
    while ((p != lastack) && (rs > 0))
    {
        if (m_pUnit[p] == NULL)
        {
            LOGC(dlog.Error, log << CONID() << " IPE readBuffer on null packet pointer");
            return -1;
        }

        if (m_bTsbPdMode)
        {
            HLOGC(dlog.Debug, log << CONID() << "readBuffer: chk if time2play: NOW=" << now << " PKT TS=" << getPktTsbPdTime(m_pUnit[p]->m_Packet.getMsgTimeStamp()));
            if ((getPktTsbPdTime(m_pUnit[p]->m_Packet.getMsgTimeStamp()) > now))
                break; /* too early for this unit, return whatever was copied */
        }

        int unitsize = (int) m_pUnit[p]->m_Packet.getLength() - m_iNotch;
        if (unitsize > rs)
            unitsize = rs;

        HLOGC(dlog.Debug, log << CONID() << "readBuffer: copying buffer #" << p
                << " targetpos=" << int(data-begin) << " sourcepos=" << m_iNotch << " size=" << unitsize << " left=" << (unitsize-rs));
        memcpy(data, m_pUnit[p]->m_Packet.m_pcData + m_iNotch, unitsize);
        data += unitsize;

        if ((rs > unitsize) || (rs == int(m_pUnit[p]->m_Packet.getLength()) - m_iNotch))
        {
            CUnit* tmp = m_pUnit[p];
            m_pUnit[p] = NULL;
            m_pUnitQueue->makeUnitFree(tmp);

            if (++ p == m_iSize)
                p = 0;

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
   int p = m_iStartPos;
   int lastack = m_iLastAckPos;
   int rs = len;

   while ((p != lastack) && (rs > 0))
   {
      int unitsize = (int) m_pUnit[p]->m_Packet.getLength() - m_iNotch;
      if (unitsize > rs)
         unitsize = rs;

      ofs.write(m_pUnit[p]->m_Packet.m_pcData + m_iNotch, unitsize);
      if (ofs.fail())
         break;

      if ((rs > unitsize) || (rs == int(m_pUnit[p]->m_Packet.getLength()) - m_iNotch))
      {
         CUnit* tmp = m_pUnit[p];
         m_pUnit[p] = NULL;
         m_pUnitQueue->makeUnitFree(tmp);

         if (++ p == m_iSize)
            p = 0;

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

void CRcvBuffer::ackData(int len)
{
   SRT_ASSERT(len < m_iSize);
   SRT_ASSERT(len > 0);

   {
      int pkts = 0;
      int bytes = 0;
      for (int i = m_iLastAckPos, n = (m_iLastAckPos + len) % m_iSize; i != n; i = (i + 1) % m_iSize)
      {
          if (m_pUnit[i] == NULL)
              continue;

          pkts++;
          bytes += (int) m_pUnit[i]->m_Packet.getLength();
      }
      if (pkts > 0) countBytes(pkts, bytes, true);
   }
   m_iLastAckPos = (m_iLastAckPos + len) % m_iSize;
   m_iMaxPos -= len;
   if (m_iMaxPos < 0)
      m_iMaxPos = 0;

   CTimer::triggerEvent();
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

bool CRcvBuffer::getRcvFirstMsg(ref_t<uint64_t> r_tsbpdtime, ref_t<bool> r_passack, ref_t<int32_t> r_skipseqno, ref_t<int32_t> r_curpktseq)
{
    int32_t& skipseqno = *r_skipseqno;
    bool& passack = *r_passack;
    skipseqno = -1;
    passack = false;
    // tsbpdtime will be retrieved by the below call
    // Returned values:
    // - tsbpdtime: real time when the packet is ready to play (whether ready to play or not)
    // - passack: false (the report concerns a packet with an exactly next sequence)
    // - skipseqno == -1: no packets to skip towards the first RTP
    // - ppkt: that exactly packet that is reported (for debugging purposes)
    // - @return: whether the reported packet is ready to play

    /* Check the acknowledged packets */
    if (getRcvReadyMsg(r_tsbpdtime, r_curpktseq))
    {
        HLOGC(dlog.Debug, log << "getRcvFirstMsg: ready CONTIG packet: %" << (*r_curpktseq));
        return true;
    }
    else if (*r_tsbpdtime != 0)
    {
        HLOGC(dlog.Debug, log << "getRcvFirstMsg: no packets found");
        return false;
    }

    // getRcvReadyMsg returned false and tsbpdtime == 0.

    // Below this line we have only two options:
    // - m_iMaxPos == 0, which means that no more packets are in the buffer
    //    - returned: tsbpdtime=0, passack=true, skipseqno=-1, ppkt=0, @return false
    // - m_iMaxPos > 0, which means that there are packets arrived after a lost packet:
    //    - returned: tsbpdtime=PKT.TS, passack=true, skipseqno=PKT.SEQ, ppkt=PKT, @return LOCAL(PKT.TS) <= NOW

    /* 
     * No acked packets ready but caller want to know next packet to wait for
     * Check the not yet acked packets that may be stuck by missing packet(s).
     */
    bool haslost = false;
    *r_tsbpdtime = 0; // redundant, for clarity
    passack = true;

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
    // 1. Check if the VERY FIRST PACKET is valid; if so then:
    //    - check if it's ready to play, return boolean value that marks it.

    for (int i = m_iLastAckPos, n = (m_iLastAckPos + m_iMaxPos) % m_iSize; i != n; i = (i + 1) % m_iSize)
    {
        if ( !m_pUnit[i]
                || m_pUnit[i]->m_iFlag != CUnit::GOOD )
        {
            /* There are packets in the sequence not received yet */
            haslost = true;
            HLOGC(dlog.Debug, log << "getRcvFirstMsg: empty hole at *" << i);
        }
        else
        {
            /* We got the 1st valid packet */
            *r_tsbpdtime = getPktTsbPdTime(m_pUnit[i]->m_Packet.getMsgTimeStamp());
            if (*r_tsbpdtime <= CTimer::getTime())
            {
                /* Packet ready to play */
                if (haslost)
                {
                    /* 
                     * Packet stuck on non-acked side because of missing packets.
                     * Tell 1st valid packet seqno so caller can skip (drop) the missing packets.
                     */
                    skipseqno = m_pUnit[i]->m_Packet.m_iSeqNo;
                    *r_curpktseq = skipseqno;
                }

                HLOGC(dlog.Debug, log << "getRcvFirstMsg: found ready packet, nSKIPPED: "
                        << ((i - m_iLastAckPos + m_iSize) % m_iSize));

                // NOTE: if haslost is not set, it means that this is the VERY FIRST
                // packet, that is, packet currently at pos = m_iLastAckPos. There's no
                // possibility that it is so otherwise because:
                // - if this first good packet is ready to play, THIS HERE RETURNS NOW.
                // ...
                return true;
            }
            HLOGC(dlog.Debug, log << "getRcvFirstMsg: found NOT READY packet, nSKIPPED: "
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
    HLOGC(dlog.Debug, log << "getRcvFirstMsg: found NO PACKETS");
    return false;
}

bool CRcvBuffer::getRcvReadyMsg(ref_t<uint64_t> tsbpdtime, ref_t<int32_t> curpktseq)
{
    *tsbpdtime = 0;

    IF_HEAVY_LOGGING(const char* reason = "NOT RECEIVED");

    for (int i = m_iStartPos, n = m_iLastAckPos; i != n; i = (i + 1) % m_iSize)
    {
        bool freeunit = false;

        /* Skip any invalid skipped/dropped packets */
        if (m_pUnit[i] == NULL)
        {
            HLOGC(mglog.Debug, log << "getRcvReadyMsg: POS=" << i
                    << " +" << ((i - m_iStartPos + m_iSize) % m_iSize)
                    << " SKIPPED - no unit there");
            if (++ m_iStartPos == m_iSize)
                m_iStartPos = 0;
            continue;
        }

        *curpktseq = m_pUnit[i]->m_Packet.getSeqNo();

        if (m_pUnit[i]->m_iFlag != CUnit::GOOD)
        {
            HLOGC(mglog.Debug, log << "getRcvReadyMsg: POS=" << i
                    << " +" << ((i - m_iStartPos + m_iSize) % m_iSize)
                    << " SKIPPED - unit not good");
            freeunit = true;
        }
        else
        {
            *tsbpdtime = getPktTsbPdTime(m_pUnit[i]->m_Packet.getMsgTimeStamp());
            int64_t towait = (*tsbpdtime - CTimer::getTime());
            if (towait > 0)
            {
                HLOGC(mglog.Debug, log << "getRcvReadyMsg: POS=" << i
                        << " +" << ((i - m_iStartPos + m_iSize) % m_iSize)
                        << " pkt %" << curpktseq.get()
                        << " NOT ready to play (only in " << (towait/1000.0) << "ms)");
                return false;
            }

            if (m_pUnit[i]->m_Packet.getMsgCryptoFlags() != EK_NOENC)
            {
                IF_HEAVY_LOGGING(reason = "DECRYPTION FAILED");
                freeunit = true; /* packet not decrypted */
            }
            else
            {
                HLOGC(mglog.Debug, log << "getRcvReadyMsg: POS=" << i
                        << " +" << ((i - m_iStartPos + m_iSize) % m_iSize)
                        << " pkt %" << curpktseq.get()
                        << " ready to play (delayed " << (-towait/1000.0) << "ms)");
                return true;
            }
        }

        if (freeunit)
        {
            HLOGC(mglog.Debug, log << "getRcvReadyMsg: POS=" << i << " FREED");
            /* removed skipped, dropped, undecryptable bytes from rcv buffer */
            const int rmbytes = (int)m_pUnit[i]->m_Packet.getLength();
            countBytes(-1, -rmbytes, true);

            CUnit* tmp = m_pUnit[i];
            m_pUnit[i] = NULL;
            m_pUnitQueue->makeUnitFree(tmp);

            if (++m_iStartPos == m_iSize)
                m_iStartPos = 0;
        }
    }

    HLOGC(mglog.Debug, log << "getRcvReadyMsg: nothing to deliver: " << reason);
    return false;
}


/*
* Return receivable data status (packet timestamp ready to play if TsbPd mode)
* Return playtime (tsbpdtime) of 1st packet in queue, ready to play or not
*
* Return data ready to be received (packet timestamp ready to play if TsbPd mode)
* Using getRcvDataSize() to know if there is something to read as it was widely
* used in the code (core.cpp) is expensive in TsbPD mode, hence this simpler function
* that only check if first packet in queue is ready.
*/
bool CRcvBuffer::isRcvDataReady(ref_t<uint64_t> tsbpdtime, ref_t<int32_t> curpktseq)
{
    *tsbpdtime = 0;

    if (m_bTsbPdMode)
    {
        CPacket* pkt = getRcvReadyPacket();
        if (!pkt)
            return false;

        /*
        * Acknowledged data is available,
        * Only say ready if time to deliver.
        * Report the timestamp, ready or not.
        */
        *curpktseq = pkt->getSeqNo();
        *tsbpdtime = getPktTsbPdTime(pkt->getMsgTimeStamp());

        return (*tsbpdtime <= CTimer::getTime());
    }

    return isRcvDataAvailable();
}

// XXX This function may be called only after checking
// if m_bTsbPdMode.
CPacket* CRcvBuffer::getRcvReadyPacket()
{
    for (int i = m_iStartPos, n = m_iLastAckPos; i != n; i = (i + 1) % m_iSize)
    {
        /* 
         * Skip missing packets that did not arrive in time.
         */
        if ( m_pUnit[i] && m_pUnit[i]->m_iFlag == CUnit::GOOD )
            return &m_pUnit[i]->m_Packet;
    }

    return 0;
}

#if ENABLE_HEAVY_LOGGING
// This function is for debug purposes only and it's called only
// from within HLOG* macros.
void CRcvBuffer::reportBufferStats()
{
    int nmissing = 0;
    int32_t low_seq= -1, high_seq = -1;
    int32_t low_ts = 0, high_ts = 0;

    for (int i = m_iStartPos, n = m_iLastAckPos; i != n; i = (i + 1) % m_iSize)
    {
        if ( m_pUnit[i] && m_pUnit[i]->m_iFlag == CUnit::GOOD )
        {
            low_seq = m_pUnit[i]->m_Packet.m_iSeqNo;
            low_ts = m_pUnit[i]->m_Packet.m_iTimeStamp;
            break;
        }
        ++nmissing;
    }

    // Not sure if a packet MUST BE at the last ack pos position, so check, just in case.
    int n = m_iLastAckPos;
    if (m_pUnit[n] && m_pUnit[n]->m_iFlag == CUnit::GOOD)
    {
        high_ts = m_pUnit[n]->m_Packet.m_iTimeStamp;
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
        upper_time += uint64_t(CPacket::MAX_TIMESTAMP)+1;

    int32_t timespan = upper_time - lower_time;
    int seqspan = 0;
    if (low_seq != -1 && high_seq != -1)
    {
        seqspan = CSeqNo::seqoff(low_seq, high_seq);
    }

    LOGC(dlog.Debug, log << "RCV BUF STATS: seqspan=%(" << low_seq << "-" << high_seq << ":" << seqspan << ") missing=" << nmissing << "pkts");
    LOGC(dlog.Debug, log << "RCV BUF STATS: timespan=" << timespan << "us (lo=" << FormatTime(lower_time) << " hi=" << FormatTime(upper_time) << ")");
}

#endif // ENABLE_HEAVY_LOGGING

bool CRcvBuffer::isRcvDataReady()
{
   uint64_t tsbpdtime;
   int32_t seq;

   return isRcvDataReady(Ref(tsbpdtime), Ref(seq));
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


bool CRcvBuffer::empty() const
{
    // This will not always return the intended value,
    // that is, it may return false when the buffer really is
    // empty - but it will return true then in one of next calls.
    // This function will be always called again at some point
    // if it returned false, and on true the connection
    // is going to be broken - so this behavior is acceptable.
    return m_iStartPos == m_iLastAckPos;
}


#ifdef SRT_ENABLE_RCVBUFSZ_MAVG
/* Return moving average of acked data pkts, bytes, and timespan (ms) of the receive buffer */
int CRcvBuffer::getRcvAvgDataSize(int &bytes, int &timespan)
{
   timespan = m_TimespanMAvg;
   bytes = m_iBytesCountMAvg;
   return(m_iCountMAvg);
}

/* Update moving average of acked data pkts, bytes, and timespan (ms) of the receive buffer */
void CRcvBuffer::updRcvAvgDataSize(uint64_t now)
{
   const uint64_t elapsed_ms = (now - m_LastSamplingTime) / 1000; //ms since last sampling

   if ((1000000 / SRT_MAVG_SAMPLING_RATE) / 1000 > elapsed_ms)
      return; /* Last sampling too recent, skip */

   if (1000 < elapsed_ms)
   {
      /* No sampling in last 1 sec, initialize/reset moving average */
      m_iCountMAvg = getRcvDataSize(m_iBytesCountMAvg, m_TimespanMAvg);
      m_LastSamplingTime = now;

      HLOGC(dlog.Debug, log << "getRcvDataSize: " << m_iCountMAvg << " " << m_iBytesCountMAvg
              << " " << m_TimespanMAvg << " ms elapsed_ms: " << elapsed_ms << " ms");
   }
   else if ((1000000 / SRT_MAVG_SAMPLING_RATE) / 1000 <= elapsed_ms)
   {
      /*
      * Weight last average value between -1 sec and last sampling time (LST)
      * and new value between last sampling time and now
      *                                      |elapsed_ms|
      *   +----------------------------------+-------+
      *  -1                                 LST      0(now)
      */
      int instspan;
      int bytescount;
      int count = getRcvDataSize(bytescount, instspan);

      m_iCountMAvg      = (int)(((count      * (1000 - elapsed_ms)) + (count      * elapsed_ms)) / 1000);
      m_iBytesCountMAvg = (int)(((bytescount * (1000 - elapsed_ms)) + (bytescount * elapsed_ms)) / 1000);
      m_TimespanMAvg    = (int)(((instspan   * (1000 - elapsed_ms)) + (instspan   * elapsed_ms)) / 1000);
      m_LastSamplingTime = now;

      HLOGC(dlog.Debug, log << "getRcvDataSize: " << count << " " << bytescount << " " << instspan
              << " ms elapsed_ms: " << elapsed_ms << " ms");
   }
}
#endif /* SRT_ENABLE_RCVBUFSZ_MAVG */

/* Return acked data pkts, bytes, and timespan (ms) of the receive buffer */
int CRcvBuffer::getRcvDataSize(int &bytes, int &timespan)
{
   timespan = 0;
   if (m_bTsbPdMode)
   {
      // Get a valid startpos.
      // Skip invalid entries in the beginning, if any.
      int startpos = m_iStartPos;
      for (; startpos != m_iLastAckPos; startpos = (startpos + 1) % m_iSize)
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
         if ((m_iMaxPos <= 0)
                 || (!m_pUnit[m_iLastAckPos])
                 || (m_pUnit[m_iLastAckPos]->m_iFlag != CUnit::GOOD))
         {
            endpos = (m_iLastAckPos == 0 ? m_iSize - 1 : m_iLastAckPos - 1);
         }

         if ((NULL != m_pUnit[endpos]) && (NULL != m_pUnit[startpos]))
         {
            const uint64_t startstamp = getPktTsbPdTime(m_pUnit[startpos]->m_Packet.getMsgTimeStamp());
            const uint64_t endstamp = getPktTsbPdTime(m_pUnit[endpos]->m_Packet.getMsgTimeStamp());
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
                timespan = (int)((endstamp - startstamp) / 1000);
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
   HLOGF(dlog.Debug, "getRcvDataSize: %6d %6d %6d ms\n", m_iAckedPktsCount, m_iAckedBytesCount, timespan);
   bytes = m_iAckedBytesCount;
   return m_iAckedPktsCount;
}

int CRcvBuffer::getRcvAvgPayloadSize() const
{
   return m_iAvgPayloadSz;
}

void CRcvBuffer::dropMsg(int32_t msgno, bool using_rexmit_flag)
{
   for (int i = m_iStartPos, n = (m_iLastAckPos + m_iMaxPos) % m_iSize; i != n; i = (i + 1) % m_iSize)
      if ((m_pUnit[i] != NULL) 
              && (m_pUnit[i]->m_Packet.getMsgSeq(using_rexmit_flag) == msgno))
         m_pUnit[i]->m_iFlag = CUnit::DROPPED;
}

uint64_t CRcvBuffer::getTsbPdTimeBase(uint32_t timestamp_us)
{
    /*
    * Packet timestamps wrap around every 01h11m35s (32-bit in usec)
    * When added to the peer start time (base time),
    * wrapped around timestamps don't provide a valid local packet delevery time.
    *
    * A wrap check period starts 30 seconds before the wrap point.
    * In this period, timestamps smaller than 30 seconds are considered to have wrapped around (then adjusted).
    * The wrap check period ends 30 seconds after the wrap point, afterwhich time base has been adjusted.
    */
    uint64_t carryover = 0;

    // This function should generally return the timebase for the given timestamp_us.
    // It's assumed that the timestamp_us, for which this function is being called,
    // is received as monotonic clock. This function then traces the changes in the
    // timestamps passed as argument and catches the moment when the 64-bit timebase
    // should be increased by a "segment length" (MAX_TIMESTAMP+1).

    // The checks will be provided for the following split:
    // [INITIAL30][FOLLOWING30]....[LAST30] <-- == CPacket::MAX_TIMESTAMP
    //
    // The following actions should be taken:
    // 1. Check if this is [LAST30]. If so, ENTER TSBPD-wrap-check state
    // 2. Then, it should turn into [INITIAL30] at some point. If so, use carryover MAX+1.
    // 3. Then it should switch to [FOLLOWING30]. If this is detected,
    //    - EXIT TSBPD-wrap-check state
    //    - save the carryover as the current time base.

    if (m_bTsbPdWrapCheck)
    {
        // Wrap check period.

        if (timestamp_us < TSBPD_WRAP_PERIOD)
        {
            carryover = uint64_t(CPacket::MAX_TIMESTAMP) + 1;
        }
        //
        else if ((timestamp_us >= TSBPD_WRAP_PERIOD)
            && (timestamp_us <= (TSBPD_WRAP_PERIOD * 2)))
        {
            /* Exiting wrap check period (if for packet delivery head) */
            m_bTsbPdWrapCheck = false;
            m_ullTsbPdTimeBase += uint64_t(CPacket::MAX_TIMESTAMP) + 1;
            tslog.Debug("tsbpd wrap period ends");
        }
    }
    // Check if timestamp_us is in the last 30 seconds before reaching the MAX_TIMESTAMP.
    else if (timestamp_us > (CPacket::MAX_TIMESTAMP - TSBPD_WRAP_PERIOD))
    {
        /* Approching wrap around point, start wrap check period (if for packet delivery head) */
        m_bTsbPdWrapCheck = true;
        tslog.Debug("tsbpd wrap period begins");
    }

    return (m_ullTsbPdTimeBase + carryover);
}

uint64_t CRcvBuffer::getPktTsbPdTime(uint32_t timestamp)
{
   return(getTsbPdTimeBase(timestamp) + m_uTsbPdDelay + timestamp + m_DriftTracer.drift());
}

int CRcvBuffer::setRcvTsbPdMode(uint64_t timebase, uint32_t delay)
{
    m_bTsbPdMode = true;
    m_bTsbPdWrapCheck = false;

    // Timebase passed here comes is calculated as:
    // >>> CTimer::getTime() - ctrlpkt->m_iTimeStamp
    // where ctrlpkt is the packet with SRT_CMD_HSREQ message.
    //
    // This function is called in the HSREQ reception handler only.
    m_ullTsbPdTimeBase = timebase;
    // XXX Seems like this may not work correctly.
    // At least this solution this way won't work with application-supplied
    // timestamps. For that case the timestamps should be taken exclusively
    // from the data packets because in case of application-supplied timestamps
    // they come from completely different server and undergo different rules
    // of network latency and drift.
    m_uTsbPdDelay = delay;
    return 0;
}

#ifdef SRT_DEBUG_TSBPD_DRIFT
void CRcvBuffer::printDriftHistogram(int64_t iDrift)
{
     /*
      * Build histogram of drift values
      * First line  (ms): <=-10.0 -9.0 ... -1.0 - 0.0 + 1.0 ... 9.0 >=10.0
      * Second line (ms):         -0.9 ... -0.1 - 0.0 + 0.1 ... 0.9
      *  0    0    0    0    0    0    0    0    0    0 -    0 +    0    0    0    1    0    0    0    0    0    0
      *       0    0    0    0    0    0    0    0    0 -    0 +    0    0    0    0    0    0    0    0    0
      */
    iDrift /= 100;  // uSec to 100 uSec (0.1ms)
    if (-10 < iDrift && iDrift < 10)
    {
        /* Fill 100us histogram -900 .. 900 us 100 us increments */
        m_TsbPdDriftHisto100us[10 + iDrift]++;
    }
    else
    {
        /* Fill 1ms histogram <=-10.0, -9.0 .. 9.0, >=10.0 ms in 1 ms increments */
        iDrift /= 10;   // 100uSec to 1ms
        if (-10 < iDrift && iDrift < 10) m_TsbPdDriftHisto1ms[10 + iDrift]++;
        else if (iDrift <= -10)          m_TsbPdDriftHisto1ms[0]++;
        else                             m_TsbPdDriftHisto1ms[20]++;
    }

    if ((m_iTsbPdDriftNbSamples % TSBPD_DRIFT_PRT_SAMPLES) == 0)
    {
        int *histo = m_TsbPdDriftHisto1ms;

        fprintf(stderr, "%4d %4d %4d %4d %4d %4d %4d %4d %4d %4d - %4d + ",
                histo[0],histo[1],histo[2],histo[3],histo[4],
                histo[5],histo[6],histo[7],histo[8],histo[9],histo[10]);
        fprintf(stderr, "%4d %4d %4d %4d %4d %4d %4d %4d %4d %4d\n",
                histo[11],histo[12],histo[13],histo[14],histo[15],
                histo[16],histo[17],histo[18],histo[19],histo[20]);

        histo = m_TsbPdDriftHisto100us;
        fprintf(stderr, "     %4d %4d %4d %4d %4d %4d %4d %4d %4d - %4d + ",
                histo[1],histo[2],histo[3],histo[4],histo[5],
                histo[6],histo[7],histo[8],histo[9],histo[10]);
        fprintf(stderr, "%4d %4d %4d %4d %4d %4d %4d %4d %4d\n",
                histo[11],histo[12],histo[13],histo[14],histo[15],
                histo[16],histo[17],histo[18],histo[19]);
    }
}

void CRcvBuffer::printDriftOffset(int tsbPdOffset, int tsbPdDriftAvg)
{
    char szTime[32] = {};
    uint64_t now = CTimer::getTime();
    time_t tnow = (time_t)(now/1000000);
    strftime(szTime, sizeof(szTime), "%H:%M:%S", localtime(&tnow));
    fprintf(stderr, "%s.%03d: tsbpd offset=%d drift=%d usec\n", 
            szTime, (int)((now%1000000)/1000), tsbPdOffset, tsbPdDriftAvg);
    memset(m_TsbPdDriftHisto100us, 0, sizeof(m_TsbPdDriftHisto100us));
    memset(m_TsbPdDriftHisto1ms, 0, sizeof(m_TsbPdDriftHisto1ms));
}
#endif /* SRT_DEBUG_TSBPD_DRIFT */

void CRcvBuffer::addRcvTsbPdDriftSample(uint32_t timestamp, pthread_mutex_t& mutex_to_lock)
{
    if (!m_bTsbPdMode) // Not checked unless in TSBPD mode
        return;
    /*
     * TsbPD time drift correction
     * TsbPD time slowly drift over long period depleting decoder buffer or raising latency
     * Re-evaluate the time adjustment value using a receiver control packet (ACK-ACK).
     * ACK-ACK timestamp is RTT/2 ago (in sender's time base)
     * Data packet have origin time stamp which is older when retransmitted so not suitable for this.
     *
     * Every TSBPD_DRIFT_MAX_SAMPLES packets, the average drift is calculated
     * if -TSBPD_DRIFT_MAX_VALUE < avgTsbPdDrift < TSBPD_DRIFT_MAX_VALUE uSec, pass drift value to RcvBuffer to adjust delevery time.
     * if outside this range, adjust this->TsbPdTimeOffset and RcvBuffer->TsbPdTimeBase by +-TSBPD_DRIFT_MAX_VALUE uSec
     * to maintain TsbPdDrift values in reasonable range (-5ms .. +5ms).
     */

    // Note important thing: this function is being called _EXCLUSIVELY_ in the handler
    // of UMSG_ACKACK command reception. This means that the timestamp used here comes
    // from the CONTROL domain, not DATA domain (timestamps from DATA domain may be
    // either schedule time or a time supplied by the application).

    int64_t iDrift = CTimer::getTime() - (getTsbPdTimeBase(timestamp) + timestamp);

    CGuard::enterCS(mutex_to_lock);

    bool updated = m_DriftTracer.update(iDrift);

#ifdef SRT_DEBUG_TSBPD_DRIFT
    printDriftHistogram(iDrift);
#endif /* SRT_DEBUG_TSBPD_DRIFT */

    if ( updated )
    {
#ifdef SRT_DEBUG_TSBPD_DRIFT
        printDriftOffset(m_DriftTracer.overdrift(), m_DriftTracer.drift());
#endif /* SRT_DEBUG_TSBPD_DRIFT */

#if ENABLE_HEAVY_LOGGING
        uint64_t oldbase = m_ullTsbPdTimeBase;
#endif
        m_ullTsbPdTimeBase += m_DriftTracer.overdrift();

        HLOGC(dlog.Debug, log << "DRIFT=" << (iDrift/1000.0) << "ms AVG="
                << (m_DriftTracer.drift()/1000.0) << "ms, TB: "
                << FormatTime(oldbase) << " UPDATED TO: " << FormatTime(m_ullTsbPdTimeBase));
    }
    else
    {
        HLOGC(dlog.Debug, log << "DRIFT=" << (iDrift/1000.0) << "ms TB REMAINS: " << FormatTime(m_ullTsbPdTimeBase));
    }

    CGuard::leaveCS(mutex_to_lock);
}

int CRcvBuffer::readMsg(char* data, int len)
{
    SRT_MSGCTRL dummy = srt_msgctrl_default;
    return readMsg(data, len, Ref(dummy));
}


int CRcvBuffer::readMsg(char* data, int len, ref_t<SRT_MSGCTRL> r_msgctl)
{
    SRT_MSGCTRL& msgctl = *r_msgctl;
    int p, q;
    bool passack;
    bool empty = true;
    uint64_t& rplaytime = msgctl.srctime;

#ifdef ENABLE_HEAVY_LOGGING
    reportBufferStats();
#endif

    if (m_bTsbPdMode)
    {
        passack = false;
        int seq = 0;

        if (getRcvReadyMsg(Ref(rplaytime), Ref(seq)))
        {
            empty = false;

            // In TSBPD mode you always read one message
            // at a time and a message always fits in one UDP packet,
            // so in one "unit".
            p = q = m_iStartPos;

#ifdef SRT_DEBUG_TSBPD_OUTJITTER
            uint64_t now = CTimer::getTime();
            if ((now - rplaytime)/10 < 10)
                m_ulPdHisto[0][(now - rplaytime)/10]++;
            else if ((now - rplaytime)/100 < 10)
                m_ulPdHisto[1][(now - rplaytime)/100]++;
            else if ((now - rplaytime)/1000 < 10)
                m_ulPdHisto[2][(now - rplaytime)/1000]++;
            else
                m_ulPdHisto[3][1]++;
#endif   /* SRT_DEBUG_TSBPD_OUTJITTER */
        }
    }
    else
    {
        rplaytime = 0;
        if (scanMsg(Ref(p), Ref(q), Ref(passack)))
            empty = false;

    }

    if (empty)
        return 0;

    // This should happen just once. By 'empty' condition
    // we have a guarantee that m_pUnit[p] exists and is valid.
    CPacket& pkt1 = m_pUnit[p]->m_Packet;

    // This returns the sequence number and message number to
    // the API caller.
    msgctl.pktseq = pkt1.getSeqNo();
    msgctl.msgno = pkt1.getMsgSeq();

    SRT_ASSERT(len > 0);
    int rs = len > 0 ? len : 0;
    while (p != (q + 1) % m_iSize)
    {
        const int pktlen = (int)m_pUnit[p]->m_Packet.getLength();
        // When unitsize is less than pktlen, only a fragment is copied to the output 'data',
        // but still the whole packet is removed from the receiver buffer.
        if (pktlen > 0)
            countBytes(-1, -pktlen, true);

        const int unitsize = ((rs >= 0) && (pktlen > rs)) ? rs : pktlen;

        HLOGC(mglog.Debug, log << "readMsg: checking unit POS=" << p);

        if (unitsize > 0)
        {
            memcpy(data, m_pUnit[p]->m_Packet.m_pcData, unitsize);
            data += unitsize;
            rs -= unitsize;

#if ENABLE_HEAVY_LOGGING
            {
                static uint64_t prev_now;
                static uint64_t prev_srctime;
                CPacket& pkt = m_pUnit[p]->m_Packet;

                int32_t seq = pkt.m_iSeqNo;

                uint64_t nowtime = CTimer::getTime();
                //CTimer::rdtsc(nowtime);
                uint64_t srctime = getPktTsbPdTime(m_pUnit[p]->m_Packet.getMsgTimeStamp());

                int64_t timediff = nowtime - srctime;
                int64_t nowdiff = prev_now ? (nowtime - prev_now) : 0;
                uint64_t srctimediff = prev_srctime ? (srctime - prev_srctime) : 0;

                HLOGC(dlog.Debug, log << CONID() << "readMsg: DELIVERED seq=" << seq
                        << " from POS=" << p << " T="
                        << FormatTime(srctime) << " in " << (timediff/1000.0)
                        << "ms - TIME-PREVIOUS: PKT: " << (srctimediff/1000.0)
                        << " LOCAL: " << (nowdiff/1000.0)
                        << " !" << BufferStamp(pkt.data(), pkt.size()));

                prev_now = nowtime;
                prev_srctime = srctime;
            }
#endif
        }
        else
        {
            HLOGC(dlog.Debug, log << CONID() << "readMsg: SKIPPED POS=" << p << " - ZERO SIZE UNIT");
        }

        if (!passack)
        {
            HLOGC(dlog.Debug, log << CONID() << "readMsg: FREEING UNIT POS=" << p);
            CUnit* tmp = m_pUnit[p];
            m_pUnit[p] = NULL;
            m_pUnitQueue->makeUnitFree(tmp);
        }
        else
        {
            HLOGC(dlog.Debug, log << CONID() << "readMsg: PASSACK UNIT POS=" << p);
            m_pUnit[p]->m_iFlag = CUnit::PASSACK;
        }

        if (++ p == m_iSize)
            p = 0;
    }

    if (!passack)
        m_iStartPos = (q + 1) % m_iSize;

    return len - rs;
}


bool CRcvBuffer::scanMsg(ref_t<int> r_p, ref_t<int> r_q, ref_t<bool> passack)
{
    int& p = *r_p;
    int& q = *r_q;

    // empty buffer
    if ((m_iStartPos == m_iLastAckPos) && (m_iMaxPos <= 0))
    {
        HLOGC(mglog.Debug, log << "scanMsg: empty buffer");
        return false;
    }

    int rmpkts = 0;
    int rmbytes = 0;
    //skip all bad msgs at the beginning
    while (m_iStartPos != m_iLastAckPos)
    {
        // Roll up to the first valid unit
        if (!m_pUnit[m_iStartPos])
        {
            if (++ m_iStartPos == m_iSize)
                m_iStartPos = 0;
            continue;
        }

        // Note: PB_FIRST | PB_LAST == PB_SOLO.
        // testing if boundary() & PB_FIRST tests if the msg is first OR solo.
        if ( m_pUnit[m_iStartPos]->m_iFlag == CUnit::GOOD
                && m_pUnit[m_iStartPos]->m_Packet.getMsgBoundary() & PB_FIRST )
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
                if ( m_pUnit[i]->m_Packet.getMsgBoundary() & PB_LAST )
                    break;

                if (++ i == m_iSize)
                    i = 0;
            }

            if (good)
                break;
        }

        CUnit* tmp = m_pUnit[m_iStartPos];
        m_pUnit[m_iStartPos] = NULL;
        rmpkts++;
        rmbytes += (int) tmp->m_Packet.getLength();
        m_pUnitQueue->makeUnitFree(tmp);

        if (++ m_iStartPos == m_iSize)
            m_iStartPos = 0;
    }
    /* we removed bytes form receive buffer */
    countBytes(-rmpkts, -rmbytes, true);

    // Not sure if this is correct, but this above 'while' loop exits
    // under the following conditions only:
    // - m_iStartPos == m_iLastAckPos (that makes passack = true)
    // - found at least GOOD unit with PB_FIRST and not all messages up to PB_LAST are good,
    //   in which case it returns with m_iStartPos <% m_iLastAckPos (earlier)
    // Also all units that lied before m_iStartPos are removed.

    p = -1;                  // message head
    q = m_iStartPos;         // message tail
    *passack = m_iStartPos == m_iLastAckPos;
    bool found = false;

    // looking for the first message
    //>>m_pUnit[size + m_iMaxPos] is not valid 

    // XXX Would be nice to make some very thorough refactoring here.

    // This rolls by q variable from m_iStartPos up to m_iLastAckPos,
    // actually from the first message up to the one with PB_LAST
    // or PB_SOLO boundary.

    // The 'i' variable used in this loop is just a stub, and the
    // upper value is just to make it "virtually infinite, but with
    // no exaggeration" (actually it makes sure that this loop does
    // not roll more than around the whole cyclic container). This variable
    // isn't used inside the loop at all.

    for (int i = 0, n = m_iMaxPos + getRcvDataSize(); i < n; ++ i)
    {
        if (m_pUnit[q] && m_pUnit[q]->m_iFlag == CUnit::GOOD)
        {
            // Equivalent pseudocode:
            // PacketBoundary bound = m_pUnit[q]->m_Packet.getMsgBoundary();
            // if ( IsSet(bound, PB_FIRST) )
            //     p = q;
            // if ( IsSet(bound, PB_LAST) && p != -1 ) 
            //     found = true;
            //
            // Not implemented this way because it uselessly check p for -1
            // also after setting it explicitly.

            switch (m_pUnit[q]->m_Packet.getMsgBoundary())
            {
            case PB_SOLO: // 11
                p = q;
                found = true;
                break;

            case PB_FIRST: // 10
                p = q;
                break;

            case PB_LAST: // 01
                if (p != -1)
                    found = true;
                break;

            case PB_SUBSEQUENT:
                ; // do nothing (caught first, rolling for last)
            }
        }
        else
        {
            // a hole in this message, not valid, restart search
            p = -1;
        }

        // 'found' is set when the current iteration hit a message with PB_LAST
        // (including PB_SOLO since the very first message).
        if (found)
        {
            // the msg has to be ack'ed or it is allowed to read out of order, and was not read before
            if (!*passack || !m_pUnit[q]->m_Packet.getMsgOrderFlag())
            {
                HLOGC(mglog.Debug, log << "scanMsg: found next-to-broken message, delivering OUT OF ORDER.");
                break;
            }

            found = false;
        }

        if (++ q == m_iSize)
            q = 0;

        if (q == m_iLastAckPos)
            *passack = true;
    }

    // no msg found
    if (!found)
    {
        // NOTE:
        // This situation may only happen if:
        // - Found a packet with PB_FIRST, so p = q at the moment when it was found
        // - Possibly found following components of that message up to shifted q
        // - Found no terminal packet (PB_LAST) for that message.

        // if the message is larger than the receiver buffer, return part of the message
        if ((p != -1) && ((q + 1) % m_iSize == p))
        {
            HLOGC(mglog.Debug, log << "scanMsg: BUFFER FULL and message is INCOMPLETE. Returning PARTIAL MESSAGE.");
            found = true;
        }
        else
        {
            HLOGC(mglog.Debug, log << "scanMsg: PARTIAL or NO MESSAGE found: p=" << p << " q=" << q);
        }
    }
    else
    {
        HLOGC(mglog.Debug, log << "scanMsg: extracted message p=" << p << " q=" << q << " (" << ((q-p+m_iSize+1)%m_iSize) << " packets)");
    }

    return found;
}
