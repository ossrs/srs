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
   Yunhong Gu, last updated 05/05/2011
modified by
   Haivision Systems Inc.
*****************************************************************************/

#include "platform_sys.h"

#include <cstring>

#include "common.h"
#include "api.h"
#include "netinet_any.h"
#include "threadname.h"
#include "logging.h"
#include "queue.h"

using namespace std;
using namespace srt::sync;
using namespace srt_logging;

srt::CUnitQueue::CUnitQueue(int initNumUnits, int mss)
    : m_iNumTaken(0)
    , m_iMSS(mss)
    , m_iBlockSize(initNumUnits)
{
    CQEntry* tempq = allocateEntry(m_iBlockSize, m_iMSS);

    if (tempq == NULL)
        throw CUDTException(MJ_SYSTEMRES, MN_MEMORY);

    m_pQEntry = m_pCurrQueue = m_pLastQueue = tempq;
    m_pQEntry->m_pNext = m_pQEntry;

    m_pAvailUnit = m_pCurrQueue->m_pUnit;

    m_iSize = m_iBlockSize;
}

srt::CUnitQueue::~CUnitQueue()
{
    CQEntry* p = m_pQEntry;

    while (p != NULL)
    {
        delete[] p->m_pUnit;
        delete[] p->m_pBuffer;

        CQEntry* q = p;
        if (p == m_pLastQueue)
            p = NULL;
        else
            p = p->m_pNext;
        delete q;
    }
}

srt::CUnitQueue::CQEntry* srt::CUnitQueue::allocateEntry(const int iNumUnits, const int mss)
{
    CQEntry* tempq = NULL;
    CUnit* tempu   = NULL;
    char* tempb    = NULL;

    try
    {
        tempq = new CQEntry;
        tempu = new CUnit[iNumUnits];
        tempb = new char[iNumUnits * mss];
    }
    catch (...)
    {
        delete tempq;
        delete[] tempu;
        delete[] tempb;

        LOGC(rslog.Error, log << "CUnitQueue: failed to allocate " << iNumUnits << " units.");
        return NULL;
    }

    for (int i = 0; i < iNumUnits; ++i)
    {
        tempu[i].m_iFlag = CUnit::FREE;
        tempu[i].m_Packet.m_pcData = tempb + i * mss;
    }

    tempq->m_pUnit   = tempu;
    tempq->m_pBuffer = tempb;
    tempq->m_iSize   = iNumUnits;

    return tempq;
}

int srt::CUnitQueue::increase_()
{
    const int numUnits = m_iBlockSize;
    HLOGC(qrlog.Debug, log << "CUnitQueue::increase: Capacity" << capacity() << " + " << numUnits << " new units, " << m_iNumTaken << " in use.");

    CQEntry* tempq = allocateEntry(numUnits, m_iMSS);
    if (tempq == NULL)
        return -1;

    m_pLastQueue->m_pNext = tempq;
    m_pLastQueue          = tempq;
    m_pLastQueue->m_pNext = m_pQEntry;

    m_iSize += numUnits;

    return 0;
}

srt::CUnit* srt::CUnitQueue::getNextAvailUnit()
{
    const int iNumUnitsTotal = capacity();
    if (m_iNumTaken * 10 > iNumUnitsTotal * 9) // 90% or more are in use.
        increase_();

    if (m_iNumTaken >= capacity())
    {
        LOGC(qrlog.Error, log << "CUnitQueue: No free units to take. Capacity" << capacity() << ".");
        return NULL;
    }

    int units_checked = 0;
    do
    {
        const CUnit* end = m_pCurrQueue->m_pUnit + m_pCurrQueue->m_iSize;
        for (; m_pAvailUnit != end; ++m_pAvailUnit, ++units_checked)
        {
            if (m_pAvailUnit->m_iFlag == CUnit::FREE)
            {
                return m_pAvailUnit;
            }
        }

        m_pCurrQueue = m_pCurrQueue->m_pNext;
        m_pAvailUnit = m_pCurrQueue->m_pUnit;
    } while (units_checked < m_iSize);

    return NULL;
}

void srt::CUnitQueue::makeUnitFree(CUnit* unit)
{
    SRT_ASSERT(unit != NULL);
    SRT_ASSERT(unit->m_iFlag != CUnit::FREE);
    unit->m_iFlag.store(CUnit::FREE);

    --m_iNumTaken;
}

void srt::CUnitQueue::makeUnitGood(CUnit* unit)
{
    ++m_iNumTaken;

    SRT_ASSERT(unit != NULL);
    SRT_ASSERT(unit->m_iFlag == CUnit::FREE);
    unit->m_iFlag.store(CUnit::GOOD);
}

srt::CSndUList::CSndUList(sync::CTimer* pTimer)
    : m_pHeap(NULL)
    , m_iArrayLength(512)
    , m_iLastEntry(-1)
    , m_ListLock()
    , m_pTimer(pTimer)
{
    setupCond(m_ListCond, "CSndUListCond");
    m_pHeap = new CSNode*[m_iArrayLength];
}

srt::CSndUList::~CSndUList()
{
    releaseCond(m_ListCond);
    delete[] m_pHeap;
}

void srt::CSndUList::update(const CUDT* u, EReschedule reschedule, sync::steady_clock::time_point ts)
{
    ScopedLock listguard(m_ListLock);

    CSNode* n = u->m_pSNode;

    if (n->m_iHeapLoc >= 0)
    {
        if (reschedule == DONT_RESCHEDULE)
            return;

        if (n->m_tsTimeStamp <= ts)
            return;

        if (n->m_iHeapLoc == 0)
        {
            n->m_tsTimeStamp = ts;
            m_pTimer->interrupt();
            return;
        }

        remove_(u);
        insert_norealloc_(ts, u);
        return;
    }

    insert_(ts, u);
}

srt::CUDT* srt::CSndUList::pop()
{
    ScopedLock listguard(m_ListLock);

    if (-1 == m_iLastEntry)
        return NULL;

    // no pop until the next scheduled time
    if (m_pHeap[0]->m_tsTimeStamp > steady_clock::now())
        return NULL;

    CUDT* u = m_pHeap[0]->m_pUDT;
    remove_(u);
    return u;
}

void srt::CSndUList::remove(const CUDT* u)
{
    ScopedLock listguard(m_ListLock);
    remove_(u);
}

steady_clock::time_point srt::CSndUList::getNextProcTime()
{
    ScopedLock listguard(m_ListLock);

    if (-1 == m_iLastEntry)
        return steady_clock::time_point();

    return m_pHeap[0]->m_tsTimeStamp;
}

void srt::CSndUList::waitNonEmpty() const
{
    UniqueLock listguard(m_ListLock);
    if (m_iLastEntry >= 0)
        return;

    m_ListCond.wait(listguard);
}

void srt::CSndUList::signalInterrupt() const
{
    ScopedLock listguard(m_ListLock);
    m_ListCond.notify_one();
}

void srt::CSndUList::realloc_()
{
    CSNode** temp = NULL;

    try
    {
        temp = new CSNode*[2 * m_iArrayLength];
    }
    catch (...)
    {
        throw CUDTException(MJ_SYSTEMRES, MN_MEMORY, 0);
    }

    memcpy((temp), m_pHeap, sizeof(CSNode*) * m_iArrayLength);
    m_iArrayLength *= 2;
    delete[] m_pHeap;
    m_pHeap = temp;
}

void srt::CSndUList::insert_(const steady_clock::time_point& ts, const CUDT* u)
{
    // increase the heap array size if necessary
    if (m_iLastEntry == m_iArrayLength - 1)
        realloc_();

    insert_norealloc_(ts, u);
}

void srt::CSndUList::insert_norealloc_(const steady_clock::time_point& ts, const CUDT* u)
{
    CSNode* n = u->m_pSNode;

    // do not insert repeated node
    if (n->m_iHeapLoc >= 0)
        return;

    SRT_ASSERT(m_iLastEntry < m_iArrayLength);

    m_iLastEntry++;
    m_pHeap[m_iLastEntry] = n;
    n->m_tsTimeStamp      = ts;

    int q = m_iLastEntry;
    int p = q;
    while (p != 0)
    {
        p = (q - 1) >> 1;
        if (m_pHeap[p]->m_tsTimeStamp <= m_pHeap[q]->m_tsTimeStamp)
            break;

        swap(m_pHeap[p], m_pHeap[q]);
        m_pHeap[q]->m_iHeapLoc = q;
        q                      = p;
    }

    n->m_iHeapLoc = q;

    // an earlier event has been inserted, wake up sending worker
    if (n->m_iHeapLoc == 0)
        m_pTimer->interrupt();

    // first entry, activate the sending queue
    if (0 == m_iLastEntry)
    {
        // m_ListLock is assumed to be locked.
        m_ListCond.notify_one();
    }
}

void srt::CSndUList::remove_(const CUDT* u)
{
    CSNode* n = u->m_pSNode;

    if (n->m_iHeapLoc >= 0)
    {
        // remove the node from heap
        m_pHeap[n->m_iHeapLoc] = m_pHeap[m_iLastEntry];
        m_iLastEntry--;
        m_pHeap[n->m_iHeapLoc]->m_iHeapLoc = n->m_iHeapLoc.load();

        int q = n->m_iHeapLoc;
        int p = q * 2 + 1;
        while (p <= m_iLastEntry)
        {
            if ((p + 1 <= m_iLastEntry) && (m_pHeap[p]->m_tsTimeStamp > m_pHeap[p + 1]->m_tsTimeStamp))
                p++;

            if (m_pHeap[q]->m_tsTimeStamp > m_pHeap[p]->m_tsTimeStamp)
            {
                swap(m_pHeap[p], m_pHeap[q]);
                m_pHeap[p]->m_iHeapLoc = p;
                m_pHeap[q]->m_iHeapLoc = q;

                q = p;
                p = q * 2 + 1;
            }
            else
                break;
        }

        n->m_iHeapLoc = -1;
    }

    // the only event has been deleted, wake up immediately
    if (0 == m_iLastEntry)
        m_pTimer->interrupt();
}

//
srt::CSndQueue::CSndQueue()
    : m_pSndUList(NULL)
    , m_pChannel(NULL)
    , m_pTimer(NULL)
    , m_bClosing(false)
{
}

srt::CSndQueue::~CSndQueue()
{
    m_bClosing = true;

    if (m_pTimer != NULL)
    {
        m_pTimer->interrupt();
    }

    // Unblock CSndQueue worker thread if it is waiting.
    m_pSndUList->signalInterrupt();

    if (m_WorkerThread.joinable())
    {
        HLOGC(rslog.Debug, log << "SndQueue: EXIT");
        m_WorkerThread.join();
    }

    delete m_pSndUList;
}

int srt::CSndQueue::ioctlQuery(int type) const
{
    return m_pChannel->ioctlQuery(type);
}
int srt::CSndQueue::sockoptQuery(int level, int type) const
{
    return m_pChannel->sockoptQuery(level, type);
}

#if ENABLE_LOGGING
int srt::CSndQueue::m_counter = 0;
#endif

void srt::CSndQueue::init(CChannel* c, CTimer* t)
{
    m_pChannel  = c;
    m_pTimer    = t;
    m_pSndUList = new CSndUList(t);

#if ENABLE_LOGGING
    ++m_counter;
    const std::string thrname = "SRT:SndQ:w" + Sprint(m_counter);
    const char*       thname  = thrname.c_str();
#else
    const char* thname = "SRT:SndQ";
#endif
    if (!StartThread(m_WorkerThread, CSndQueue::worker, this, thname))
        throw CUDTException(MJ_SYSTEMRES, MN_THREAD);
}

int srt::CSndQueue::getIpTTL() const
{
    return m_pChannel ? m_pChannel->getIpTTL() : -1;
}

int srt::CSndQueue::getIpToS() const
{
    return m_pChannel ? m_pChannel->getIpToS() : -1;
}

#ifdef SRT_ENABLE_BINDTODEVICE
bool srt::CSndQueue::getBind(char* dst, size_t len) const
{
    return m_pChannel ? m_pChannel->getBind(dst, len) : false;
}
#endif

void* srt::CSndQueue::worker(void* param)
{
    CSndQueue* self = (CSndQueue*)param;

#if ENABLE_LOGGING
    THREAD_STATE_INIT(("SRT:SndQ:w" + Sprint(m_counter)).c_str());
#else
    THREAD_STATE_INIT("SRT:SndQ:worker");
#endif

#if defined(SRT_DEBUG_SNDQ_HIGHRATE)
    CTimer::rdtsc(self->m_ullDbgTime);
    self->m_ullDbgPeriod = uint64_t(5000000) * CTimer::getCPUFrequency();
    self->m_ullDbgTime += self->m_ullDbgPeriod;
#endif /* SRT_DEBUG_SNDQ_HIGHRATE */

    while (!self->m_bClosing)
    {
        const steady_clock::time_point next_time = self->m_pSndUList->getNextProcTime();

#if defined(SRT_DEBUG_SNDQ_HIGHRATE)
        self->m_WorkerStats.lIteration++;
#endif /* SRT_DEBUG_SNDQ_HIGHRATE */

        if (is_zero(next_time))
        {
#if defined(SRT_DEBUG_SNDQ_HIGHRATE)
            self->m_WorkerStats.lNotReadyTs++;
#endif /* SRT_DEBUG_SNDQ_HIGHRATE */

            // wait here if there is no sockets with data to be sent
            THREAD_PAUSED();
            if (!self->m_bClosing)
            {
                self->m_pSndUList->waitNonEmpty();

#if defined(SRT_DEBUG_SNDQ_HIGHRATE)
                self->m_WorkerStats.lCondWait++;
#endif /* SRT_DEBUG_SNDQ_HIGHRATE */
            }
            THREAD_RESUMED();

            continue;
        }

        // wait until next processing time of the first socket on the list
        const steady_clock::time_point currtime = steady_clock::now();

#if defined(SRT_DEBUG_SNDQ_HIGHRATE)
        if (self->m_ullDbgTime <= currtime)
        {
            fprintf(stdout,
                    "SndQueue %lu slt:%lu nrp:%lu snt:%lu nrt:%lu ctw:%lu\n",
                    self->m_WorkerStats.lIteration,
                    self->m_WorkerStats.lSleepTo,
                    self->m_WorkerStats.lNotReadyPop,
                    self->m_WorkerStats.lSendTo,
                    self->m_WorkerStats.lNotReadyTs,
                    self->m_WorkerStats.lCondWait);
            memset(&self->m_WorkerStats, 0, sizeof(self->m_WorkerStats));
            self->m_ullDbgTime = currtime + self->m_ullDbgPeriod;
        }
#endif /* SRT_DEBUG_SNDQ_HIGHRATE */

        THREAD_PAUSED();
        if (currtime < next_time)
        {
            self->m_pTimer->sleep_until(next_time);

#if defined(HAI_DEBUG_SNDQ_HIGHRATE)
            self->m_WorkerStats.lSleepTo++;
#endif /* SRT_DEBUG_SNDQ_HIGHRATE */
        }
        THREAD_RESUMED();

        // Get a socket with a send request if any.
        CUDT* u = self->m_pSndUList->pop();
        if (u == NULL)
        {
#if defined(SRT_DEBUG_SNDQ_HIGHRATE)
            self->m_WorkerStats.lNotReadyPop++;
#endif /* SRT_DEBUG_SNDQ_HIGHRATE */
            continue;
        }
        
#define UST(field) ((u->m_b##field) ? "+" : "-") << #field << " "
        HLOGC(qslog.Debug,
            log << "CSndQueue: requesting packet from @" << u->socketID() << " STATUS: " << UST(Listening)
                << UST(Connecting) << UST(Connected) << UST(Closing) << UST(Shutdown) << UST(Broken) << UST(PeerHealth)
                << UST(Opened));
#undef UST

        if (!u->m_bConnected || u->m_bBroken)
        {
#if defined(SRT_DEBUG_SNDQ_HIGHRATE)
            self->m_WorkerStats.lNotReadyPop++;
#endif /* SRT_DEBUG_SNDQ_HIGHRATE */
            continue;
        }

        // pack a packet from the socket
        CPacket pkt;
        const std::pair<bool, steady_clock::time_point> res_time = u->packData((pkt));

        // Check if payload size is invalid.
        if (res_time.first == false)
        {
#if defined(SRT_DEBUG_SNDQ_HIGHRATE)
            self->m_WorkerStats.lNotReadyPop++;
#endif /* SRT_DEBUG_SNDQ_HIGHRATE */
            continue;
        }

        const sockaddr_any addr = u->m_PeerAddr;
        const steady_clock::time_point next_send_time = res_time.second;
        if (!is_zero(next_send_time))
            self->m_pSndUList->update(u, CSndUList::DO_RESCHEDULE, next_send_time);

        HLOGC(qslog.Debug, log << self->CONID() << "chn:SENDING: " << pkt.Info());
        self->m_pChannel->sendto(addr, pkt);

#if defined(SRT_DEBUG_SNDQ_HIGHRATE)
        self->m_WorkerStats.lSendTo++;
#endif /* SRT_DEBUG_SNDQ_HIGHRATE */
    }

    THREAD_EXIT();
    return NULL;
}

int srt::CSndQueue::sendto(const sockaddr_any& w_addr, CPacket& w_packet)
{
    // send out the packet immediately (high priority), this is a control packet
    m_pChannel->sendto(w_addr, w_packet);
    return (int)w_packet.getLength();
}

//
srt::CRcvUList::CRcvUList()
    : m_pUList(NULL)
    , m_pLast(NULL)
{
}

srt::CRcvUList::~CRcvUList() {}

void srt::CRcvUList::insert(const CUDT* u)
{
    CRNode* n        = u->m_pRNode;
    n->m_tsTimeStamp = steady_clock::now();

    if (NULL == m_pUList)
    {
        // empty list, insert as the single node
        n->m_pPrev = n->m_pNext = NULL;
        m_pLast = m_pUList = n;

        return;
    }

    // always insert at the end for RcvUList
    n->m_pPrev       = m_pLast;
    n->m_pNext       = NULL;
    m_pLast->m_pNext = n;
    m_pLast          = n;
}

void srt::CRcvUList::remove(const CUDT* u)
{
    CRNode* n = u->m_pRNode;

    if (!n->m_bOnList)
        return;

    if (NULL == n->m_pPrev)
    {
        // n is the first node
        m_pUList = n->m_pNext;
        if (NULL == m_pUList)
            m_pLast = NULL;
        else
            m_pUList->m_pPrev = NULL;
    }
    else
    {
        n->m_pPrev->m_pNext = n->m_pNext;
        if (NULL == n->m_pNext)
        {
            // n is the last node
            m_pLast = n->m_pPrev;
        }
        else
            n->m_pNext->m_pPrev = n->m_pPrev;
    }

    n->m_pNext = n->m_pPrev = NULL;
}

void srt::CRcvUList::update(const CUDT* u)
{
    CRNode* n = u->m_pRNode;

    if (!n->m_bOnList)
        return;

    n->m_tsTimeStamp = steady_clock::now();

    // if n is the last node, do not need to change
    if (NULL == n->m_pNext)
        return;

    if (NULL == n->m_pPrev)
    {
        m_pUList          = n->m_pNext;
        m_pUList->m_pPrev = NULL;
    }
    else
    {
        n->m_pPrev->m_pNext = n->m_pNext;
        n->m_pNext->m_pPrev = n->m_pPrev;
    }

    n->m_pPrev       = m_pLast;
    n->m_pNext       = NULL;
    m_pLast->m_pNext = n;
    m_pLast          = n;
}

//
srt::CHash::CHash()
    : m_pBucket(NULL)
    , m_iHashSize(0)
{
}

srt::CHash::~CHash()
{
    for (int i = 0; i < m_iHashSize; ++i)
    {
        CBucket* b = m_pBucket[i];
        while (NULL != b)
        {
            CBucket* n = b->m_pNext;
            delete b;
            b = n;
        }
    }

    delete[] m_pBucket;
}

void srt::CHash::init(int size)
{
    m_pBucket = new CBucket*[size];

    for (int i = 0; i < size; ++i)
        m_pBucket[i] = NULL;

    m_iHashSize = size;
}

srt::CUDT* srt::CHash::lookup(int32_t id)
{
    // simple hash function (% hash table size); suitable for socket descriptors
    CBucket* b = m_pBucket[id % m_iHashSize];

    while (NULL != b)
    {
        if (id == b->m_iID)
            return b->m_pUDT;
        b = b->m_pNext;
    }

    return NULL;
}

void srt::CHash::insert(int32_t id, CUDT* u)
{
    CBucket* b = m_pBucket[id % m_iHashSize];

    CBucket* n = new CBucket;
    n->m_iID   = id;
    n->m_pUDT  = u;
    n->m_pNext = b;

    m_pBucket[id % m_iHashSize] = n;
}

void srt::CHash::remove(int32_t id)
{
    CBucket* b = m_pBucket[id % m_iHashSize];
    CBucket* p = NULL;

    while (NULL != b)
    {
        if (id == b->m_iID)
        {
            if (NULL == p)
                m_pBucket[id % m_iHashSize] = b->m_pNext;
            else
                p->m_pNext = b->m_pNext;

            delete b;

            return;
        }

        p = b;
        b = b->m_pNext;
    }
}

//
srt::CRendezvousQueue::CRendezvousQueue()
    : m_lRendezvousID()
    , m_RIDListLock()
{
}

srt::CRendezvousQueue::~CRendezvousQueue()
{
    m_lRendezvousID.clear();
}

void srt::CRendezvousQueue::insert(const SRTSOCKET&           id,
                              CUDT*                           u,
                              const sockaddr_any&             addr,
                              const steady_clock::time_point& ttl)
{
    ScopedLock vg(m_RIDListLock);

    CRL r;
    r.m_iID      = id;
    r.m_pUDT     = u;
    r.m_PeerAddr = addr;
    r.m_tsTTL    = ttl;

    m_lRendezvousID.push_back(r);
    HLOGC(cnlog.Debug,
          log << "RID: adding socket @" << id << " for address: " << addr.str() << " expires: " << FormatTime(ttl)
              << " (total connectors: " << m_lRendezvousID.size() << ")");
}

void srt::CRendezvousQueue::remove(const SRTSOCKET& id)
{
    ScopedLock lkv(m_RIDListLock);

    for (list<CRL>::iterator i = m_lRendezvousID.begin(); i != m_lRendezvousID.end(); ++i)
    {
        if (i->m_iID == id)
        {
            m_lRendezvousID.erase(i);
            break;
        }
    }
}

srt::CUDT* srt::CRendezvousQueue::retrieve(const sockaddr_any& addr, SRTSOCKET& w_id) const
{
    ScopedLock vg(m_RIDListLock);

    // TODO: optimize search
    for (list<CRL>::const_iterator i = m_lRendezvousID.begin(); i != m_lRendezvousID.end(); ++i)
    {
        if (i->m_PeerAddr == addr && ((w_id == 0) || (w_id == i->m_iID)))
        {
            HLOGC(cnlog.Debug,
                  log << "RID: found id @" << i->m_iID << " while looking for "
                      << (w_id ? "THIS ID FROM " : "A NEW CONNECTION FROM ") << i->m_PeerAddr.str());
            w_id = i->m_iID;
            return i->m_pUDT;
        }
    }

#if ENABLE_HEAVY_LOGGING
    std::ostringstream spec;
    if (w_id == 0)
        spec << "A NEW CONNECTION REQUEST";
    else
        spec << " AGENT @" << w_id;
    HLOGC(cnlog.Debug,
          log << "RID: NO CONNECTOR FOR ADR:" << addr.str() << " while looking for " << spec.str() << " ("
              << m_lRendezvousID.size() << " connectors total)");
#endif

    return NULL;
}

void srt::CRendezvousQueue::updateConnStatus(EReadStatus rst, EConnectStatus cst, CUnit* unit)
{
    vector<LinkStatusInfo> toRemove, toProcess;

    const CPacket* pkt = unit ? &unit->m_Packet : NULL;

    // Need a stub value for a case when there's no unit provided ("storage depleted" case).
    // It should be normally NOT IN USE because in case of "storage depleted", rst != RST_OK.
    const SRTSOCKET dest_id = pkt ? pkt->m_iID : 0;

    // If no socket were qualified for further handling, finish here.
    // Otherwise toRemove and toProcess contain items to handle.
    if (!qualifyToHandle(rst, cst, dest_id, (toRemove), (toProcess)))
        return;

    HLOGC(cnlog.Debug,
          log << "updateConnStatus: collected " << toProcess.size() << " for processing, " << toRemove.size()
              << " to close");

    // Repeat (resend) connection request.
    for (vector<LinkStatusInfo>::iterator i = toProcess.begin(); i != toProcess.end(); ++i)
    {
        // IMPORTANT INFORMATION concerning changes towards UDT legacy.
        // In the UDT code there was no attempt to interpret any incoming data.
        // All data from the incoming packet were considered to be already deployed into
        // m_ConnRes field, and m_ConnReq field was considered at this time accordingly updated.
        // Therefore this procedure did only one thing: craft a new handshake packet and send it.
        // In SRT this may also interpret extra data (extensions in case when Agent is Responder)
        // and the `pktIn` packet may sometimes contain no data. Therefore the passed `rst`
        // must be checked to distinguish the call by periodic update (RST_AGAIN) from a call
        // due to have received the packet (RST_OK).
        //
        // In the below call, only the underlying `processRendezvous` function will be attempting
        // to interpret these data (for caller-listener this was already done by `processConnectRequest`
        // before calling this function), and it checks for the data presence.

        EReadStatus    read_st = rst;
        EConnectStatus conn_st = cst;

        if (i->id != dest_id)
        {
            read_st = RST_AGAIN;
            conn_st = CONN_AGAIN;
        }

        HLOGC(cnlog.Debug,
              log << "updateConnStatus: processing async conn for @" << i->id << " FROM " << i->peeraddr.str());

        if (!i->u->processAsyncConnectRequest(read_st, conn_st, pkt, i->peeraddr))
        {
            // cst == CONN_REJECT can only be result of worker_ProcessAddressedPacket and
            // its already set in this case.
            LinkStatusInfo fi = *i;
            fi.errorcode      = SRT_ECONNREJ;
            toRemove.push_back(fi);
            i->u->sendCtrl(UMSG_SHUTDOWN);
        }
    }

    // NOTE: it is "believed" here that all CUDT objects will not be
    // deleted in the meantime. This is based on a statement that at worst
    // they have been "just" declared failed and it will pass at least 1s until
    // they are moved to ClosedSockets and it is believed that this function will
    // not be held on mutexes that long.

    for (vector<LinkStatusInfo>::iterator i = toRemove.begin(); i != toRemove.end(); ++i)
    {
        HLOGC(cnlog.Debug, log << "updateConnStatus: COMPLETING dep objects update on failed @" << i->id);
        //
        // Setting m_bConnecting to false, and need to remove the socket from the rendezvous queue
        // because the next CUDT::close will not remove it from the queue when m_bConnecting = false,
        // and may crash on next pass.
        //
        // TODO: maybe lock i->u->m_ConnectionLock?
        i->u->m_bConnecting = false;
        remove(i->u->m_SocketID);

        // DO NOT close the socket here because in this case it might be
        // unable to get status from at the right moment. Also only member
        // sockets should be taken care of internally - single sockets should
        // be normally closed by the application, after it is done with them.

        // app can call any UDT API to learn the connection_broken error
        CUDT::uglobal().m_EPoll.update_events(
            i->u->m_SocketID, i->u->m_sPollID, SRT_EPOLL_IN | SRT_EPOLL_OUT | SRT_EPOLL_ERR, true);

        i->u->completeBrokenConnectionDependencies(i->errorcode);
    }

    {
        // Now, additionally for every failed link reset the TTL so that
        // they are set expired right now.
        ScopedLock vg(m_RIDListLock);
        for (list<CRL>::iterator i = m_lRendezvousID.begin(); i != m_lRendezvousID.end(); ++i)
        {
            if (find_if(toRemove.begin(), toRemove.end(), LinkStatusInfo::HasID(i->m_iID)) != toRemove.end())
            {
                LOGC(cnlog.Error,
                     log << "updateConnStatus: processAsyncConnectRequest FAILED on @" << i->m_iID
                         << ". Setting TTL as EXPIRED.");
                i->m_tsTTL =
                    steady_clock::time_point(); // Make it expire right now, will be picked up at the next iteration
            }
        }
    }
}

bool srt::CRendezvousQueue::qualifyToHandle(EReadStatus    rst,
                                       EConnectStatus cst      SRT_ATR_UNUSED,
                                       int                     iDstSockID,
                                       vector<LinkStatusInfo>& toRemove,
                                       vector<LinkStatusInfo>& toProcess)
{
    ScopedLock vg(m_RIDListLock);

    if (m_lRendezvousID.empty())
        return false; // nothing to process.

    HLOGC(cnlog.Debug,
          log << "updateConnStatus: updating after getting pkt with DST socket ID @" << iDstSockID
              << " status: " << ConnectStatusStr(cst));

    for (list<CRL>::iterator i = m_lRendezvousID.begin(), i_next = i; i != m_lRendezvousID.end(); i = i_next)
    {
        // Safe iterator to the next element. If the current element is erased, the iterator is updated again.
        ++i_next;

        const steady_clock::time_point tsNow = steady_clock::now();

        if (tsNow >= i->m_tsTTL)
        {
            HLOGC(cnlog.Debug,
                  log << "RID: socket @" << i->m_iID
                      << " removed - EXPIRED ("
                      // The "enforced on FAILURE" is below when processAsyncConnectRequest failed.
                      << (is_zero(i->m_tsTTL) ? "enforced on FAILURE" : "passed TTL") << "). WILL REMOVE from queue.");

            // Set appropriate error information, but do not update yet.
            // Exit the lock first. Collect objects to update them later.
            int ccerror = SRT_ECONNREJ;
            if (i->m_pUDT->m_RejectReason == SRT_REJ_UNKNOWN)
            {
                if (!is_zero(i->m_tsTTL))
                {
                    // Timer expired, set TIMEOUT forcefully
                    i->m_pUDT->m_RejectReason = SRT_REJ_TIMEOUT;
                    ccerror                   = SRT_ENOSERVER;
                }
                else
                {
                    // In case of unknown reason, rejection should at least
                    // suggest error on the peer
                    i->m_pUDT->m_RejectReason = SRT_REJ_PEER;
                }
            }

            // The call to completeBrokenConnectionDependencies() cannot happen here
            // under the lock of m_RIDListLock as it risks a deadlock.
            // Collect in 'toRemove' to update later.
            LinkStatusInfo fi = {i->m_pUDT, i->m_iID, ccerror, i->m_PeerAddr, -1};
            toRemove.push_back(fi);

            // i_next was preincremented, but this is guaranteed to point to
            // the element next to erased one.
            i_next = m_lRendezvousID.erase(i);
            continue;
        }
        else
        {
            HLOGC(cnlog.Debug,
                  log << "RID: socket @" << i->m_iID << " still active (remaining " << std::fixed
                      << (count_microseconds(i->m_tsTTL - tsNow) / 1000000.0) << "s of TTL)...");
        }

        const steady_clock::time_point tsLastReq = i->m_pUDT->m_tsLastReqTime;
        const steady_clock::time_point tsRepeat =
            tsLastReq + milliseconds_from(250); // Repeat connection request (send HS).

        // A connection request is repeated every 250 ms if there was no response from the peer:
        // - RST_AGAIN means no packet was received over UDP.
        // - a packet was received, but not for THIS socket.
        if ((rst == RST_AGAIN || i->m_iID != iDstSockID) && tsNow <= tsRepeat)
        {
            HLOGC(cnlog.Debug,
                  log << "RID:@" << i->m_iID << std::fixed << count_microseconds(tsNow - tsLastReq) / 1000.0
                      << " ms passed since last connection request.");

            continue;
        }

        HLOGC(cnlog.Debug,
              log << "RID:@" << i->m_iID << " cst=" << ConnectStatusStr(cst) << " -- repeating connection request.");

        // This queue is used only in case of Async mode (rendezvous or caller-listener).
        // Synchronous connection requests are handled in startConnect() completely.
        if (!i->m_pUDT->m_config.bSynRecving)
        {
            // Collect them so that they can be updated out of m_RIDListLock.
            LinkStatusInfo fi = {i->m_pUDT, i->m_iID, SRT_SUCCESS, i->m_PeerAddr, -1};
            toProcess.push_back(fi);
        }
        else
        {
            HLOGC(cnlog.Debug, log << "RID: socket @" << i->m_iID << " is SYNCHRONOUS, NOT UPDATING");
        }
    }

    return !toRemove.empty() || !toProcess.empty();
}

//
srt::CRcvQueue::CRcvQueue()
    : m_WorkerThread()
    , m_pUnitQueue(NULL)
    , m_pRcvUList(NULL)
    , m_pHash(NULL)
    , m_pChannel(NULL)
    , m_pTimer(NULL)
    , m_iIPversion()
    , m_szPayloadSize()
    , m_bClosing(false)
    , m_LSLock()
    , m_pListener(NULL)
    , m_pRendezvousQueue(NULL)
    , m_vNewEntry()
    , m_IDLock()
    , m_mBuffer()
    , m_BufferCond()
{
    setupCond(m_BufferCond, "QueueBuffer");
}

srt::CRcvQueue::~CRcvQueue()
{
    m_bClosing = true;

    if (m_WorkerThread.joinable())
    {
        HLOGC(rslog.Debug, log << "RcvQueue: EXIT");
        m_WorkerThread.join();
    }
    releaseCond(m_BufferCond);

    delete m_pUnitQueue;
    delete m_pRcvUList;
    delete m_pHash;
    delete m_pRendezvousQueue;

    // remove all queued messages
    for (map<int32_t, std::queue<CPacket*> >::iterator i = m_mBuffer.begin(); i != m_mBuffer.end(); ++i)
    {
        while (!i->second.empty())
        {
            CPacket* pkt = i->second.front();
            delete[] pkt->m_pcData;
            delete pkt;
            i->second.pop();
        }
    }
}

#if ENABLE_LOGGING
srt::sync::atomic<int> srt::CRcvQueue::m_counter(0);
#endif

void srt::CRcvQueue::init(int qsize, size_t payload, int version, int hsize, CChannel* cc, CTimer* t)
{
    m_iIPversion    = version;
    m_szPayloadSize = payload;

    SRT_ASSERT(m_pUnitQueue == NULL);
    m_pUnitQueue = new CUnitQueue(qsize, (int)payload);

    m_pHash = new CHash;
    m_pHash->init(hsize);

    m_pChannel = cc;
    m_pTimer   = t;

    m_pRcvUList        = new CRcvUList;
    m_pRendezvousQueue = new CRendezvousQueue;

#if ENABLE_LOGGING
    const int cnt = ++m_counter;
    const std::string thrname = "SRT:RcvQ:w" + Sprint(cnt);
#else
    const std::string thrname = "SRT:RcvQ:w";
#endif

    if (!StartThread(m_WorkerThread, CRcvQueue::worker, this, thrname.c_str()))
    {
        throw CUDTException(MJ_SYSTEMRES, MN_THREAD);
    }
}

void* srt::CRcvQueue::worker(void* param)
{
    CRcvQueue*   self = (CRcvQueue*)param;
    sockaddr_any sa(self->getIPversion());
    int32_t      id = 0;

#if ENABLE_LOGGING
    THREAD_STATE_INIT(("SRT:RcvQ:w" + Sprint(m_counter)).c_str());
#else
    THREAD_STATE_INIT("SRT:RcvQ:worker");
#endif

    CUnit*         unit = 0;
    EConnectStatus cst  = CONN_AGAIN;
    while (!self->m_bClosing)
    {
        bool        have_received = false;
        EReadStatus rst           = self->worker_RetrieveUnit((id), (unit), (sa));
        if (rst == RST_OK)
        {
            if (id < 0)
            {
                // User error on peer. May log something, but generally can only ignore it.
                // XXX Think maybe about sending some "connection rejection response".
                HLOGC(qrlog.Debug,
                      log << self->CONID() << "RECEIVED negative socket id '" << id
                          << "', rejecting (POSSIBLE ATTACK)");
                continue;
            }

            // NOTE: cst state is being changed here.
            // This state should be maintained through any next failed calls to worker_RetrieveUnit.
            // Any error switches this to rejection, just for a case.

            // Note to rendezvous connection. This can accept:
            // - ID == 0 - take the first waiting rendezvous socket
            // - ID > 0  - find the rendezvous socket that has this ID.
            if (id == 0)
            {
                // ID 0 is for connection request, which should be passed to the listening socket or rendezvous sockets
                cst = self->worker_ProcessConnectionRequest(unit, sa);
            }
            else
            {
                // Otherwise ID is expected to be associated with:
                // - an enqueued rendezvous socket
                // - a socket connected to a peer
                cst = self->worker_ProcessAddressedPacket(id, unit, sa);
                // CAN RETURN CONN_REJECT, but m_RejectReason is already set
            }
            HLOGC(qrlog.Debug, log << self->CONID() << "worker: result for the unit: " << ConnectStatusStr(cst));
            if (cst == CONN_AGAIN)
            {
                HLOGC(qrlog.Debug, log << self->CONID() << "worker: packet not dispatched, continuing reading.");
                continue;
            }
            have_received = true;
        }
        else if (rst == RST_ERROR)
        {
            // According to the description by CChannel::recvfrom, this can be either of:
            // - IPE: all errors except EBADF
            // - socket was closed in the meantime by another thread: EBADF
            // If EBADF, then it's expected that the "closing" state is also set.
            // Check that just to report possible errors, but interrupt the loop anyway.
            if (self->m_bClosing)
            {
                HLOGC(qrlog.Debug,
                      log << self->CONID() << "CChannel reported error, but Queue is closing - INTERRUPTING worker.");
            }
            else
            {
                LOGC(qrlog.Fatal,
                     log << self->CONID()
                         << "CChannel reported ERROR DURING TRANSMISSION - IPE. INTERRUPTING worker anyway.");
            }
            cst = CONN_REJECT;
            break;
        }
        // OTHERWISE: this is an "AGAIN" situation. No data was read, but the process should continue.

        // take care of the timing event for all UDT sockets
        const steady_clock::time_point curtime_minus_syn =
            steady_clock::now() - microseconds_from(CUDT::COMM_SYN_INTERVAL_US);

        CRNode* ul = self->m_pRcvUList->m_pUList;
        while ((NULL != ul) && (ul->m_tsTimeStamp < curtime_minus_syn))
        {
            CUDT* u = ul->m_pUDT;

            if (u->m_bConnected && !u->m_bBroken && !u->m_bClosing)
            {
                u->checkTimers();
                self->m_pRcvUList->update(u);
            }
            else
            {
                HLOGC(qrlog.Debug,
                      log << CUDTUnited::CONID(u->m_SocketID) << " SOCKET broken, REMOVING FROM RCV QUEUE/MAP.");
                // the socket must be removed from Hash table first, then RcvUList
                self->m_pHash->remove(u->m_SocketID);
                self->m_pRcvUList->remove(u);
                u->m_pRNode->m_bOnList = false;
            }

            ul = self->m_pRcvUList->m_pUList;
        }

        if (have_received)
        {
            HLOGC(qrlog.Debug,
                  log << "worker: RECEIVED PACKET --> updateConnStatus. cst=" << ConnectStatusStr(cst) << " id=" << id
                      << " pkt-payload-size=" << unit->m_Packet.getLength());
        }

        // Check connection requests status for all sockets in the RendezvousQueue.
        // Pass the connection status from the last call of:
        // worker_ProcessAddressedPacket --->
        // worker_TryAsyncRend_OrStore --->
        // CUDT::processAsyncConnectResponse --->
        // CUDT::processConnectResponse
        self->m_pRendezvousQueue->updateConnStatus(rst, cst, unit);

        // XXX updateConnStatus may have removed the connector from the list,
        // however there's still m_mBuffer in CRcvQueue for that socket to care about.
    }

    HLOGC(qrlog.Debug, log << "worker: EXIT");

    THREAD_EXIT();
    return NULL;
}

srt::EReadStatus srt::CRcvQueue::worker_RetrieveUnit(int32_t& w_id, CUnit*& w_unit, sockaddr_any& w_addr)
{
#if !USE_BUSY_WAITING
    // This might be not really necessary, and probably
    // not good for extensive bidirectional communication.
    m_pTimer->tick();
#endif

    // check waiting list, if new socket, insert it to the list
    while (ifNewEntry())
    {
        CUDT* ne = getNewEntry();
        if (ne)
        {
            HLOGC(qrlog.Debug,
                  log << CUDTUnited::CONID(ne->m_SocketID)
                      << " SOCKET pending for connection - ADDING TO RCV QUEUE/MAP");
            m_pRcvUList->insert(ne);
            m_pHash->insert(ne->m_SocketID, ne);
        }
    }
    // find next available slot for incoming packet
    w_unit = m_pUnitQueue->getNextAvailUnit();
    if (!w_unit)
    {
        // no space, skip this packet
        CPacket temp;
        temp.m_pcData = new char[m_szPayloadSize];
        temp.setLength(m_szPayloadSize);
        THREAD_PAUSED();
        EReadStatus rst = m_pChannel->recvfrom((w_addr), (temp));
        THREAD_RESUMED();
        // Note: this will print nothing about the packet details unless heavy logging is on.
        LOGC(qrlog.Error, log << CONID() << "LOCAL STORAGE DEPLETED. Dropping 1 packet: " << temp.Info());
        delete[] temp.m_pcData;

        // Be transparent for RST_ERROR, but ignore the correct
        // data read and fake that the packet was dropped.
        return rst == RST_ERROR ? RST_ERROR : RST_AGAIN;
    }

    w_unit->m_Packet.setLength(m_szPayloadSize);

    // reading next incoming packet, recvfrom returns -1 is nothing has been received
    THREAD_PAUSED();
    EReadStatus rst = m_pChannel->recvfrom((w_addr), (w_unit->m_Packet));
    THREAD_RESUMED();

    if (rst == RST_OK)
    {
        w_id = w_unit->m_Packet.m_iID;
        HLOGC(qrlog.Debug,
              log << "INCOMING PACKET: FROM=" << w_addr.str() << " BOUND=" << m_pChannel->bindAddressAny().str() << " "
                  << w_unit->m_Packet.Info());
    }
    return rst;
}

srt::EConnectStatus srt::CRcvQueue::worker_ProcessConnectionRequest(CUnit* unit, const sockaddr_any& addr)
{
    HLOGC(cnlog.Debug,
          log << "Got sockID=0 from " << addr.str() << " - trying to resolve it as a connection request...");
    // Introduced protection because it may potentially happen
    // that another thread could have closed the socket at
    // the same time and inject a bug between checking the
    // pointer for NULL and using it.
    int  listener_ret  = SRT_REJ_UNKNOWN;
    bool have_listener = false;
    {
        ScopedLock cg(m_LSLock);
        if (m_pListener)
        {
            LOGC(cnlog.Note, log << "PASSING request from: " << addr.str() << " to agent:" << m_pListener->socketID());
            listener_ret = m_pListener->processConnectRequest(addr, unit->m_Packet);

            // This function does return a code, but it's hard to say as to whether
            // anything can be done about it. In case when it's stated possible, the
            // listener will try to send some rejection response to the caller, but
            // that's already done inside this function. So it's only used for
            // displaying the error in logs.

            have_listener = true;
        }
    }

    // NOTE: Rendezvous sockets do bind(), but not listen(). It means that the socket is
    // ready to accept connection requests, but they are not being redirected to the listener
    // socket, as this is not a listener socket at all. This goes then HERE.

    if (have_listener) // That is, the above block with m_pListener->processConnectRequest was executed
    {
        LOGC(cnlog.Note,
             log << CONID() << "Listener managed the connection request from: " << addr.str()
                 << " result:" << RequestTypeStr(UDTRequestType(listener_ret)));
        return listener_ret == SRT_REJ_UNKNOWN ? CONN_CONTINUE : CONN_REJECT;
    }

    // If there's no listener waiting for the packet, just store it into the queue.
    return worker_TryAsyncRend_OrStore(0, unit, addr); // 0 id because the packet came in with that very ID.
}

srt::EConnectStatus srt::CRcvQueue::worker_ProcessAddressedPacket(int32_t id, CUnit* unit, const sockaddr_any& addr)
{
    CUDT* u = m_pHash->lookup(id);
    if (!u)
    {
        // Pass this to either async rendezvous connection,
        // or store the packet in the queue.
        HLOGC(cnlog.Debug, log << "worker_ProcessAddressedPacket: resending to QUEUED socket @" << id);
        return worker_TryAsyncRend_OrStore(id, unit, addr);
    }

    // Found associated CUDT - process this as control or data packet
    // addressed to an associated socket.
    if (addr != u->m_PeerAddr)
    {
        HLOGC(cnlog.Debug,
              log << CONID() << "Packet for SID=" << id << " asoc with " << u->m_PeerAddr.str() << " received from "
                  << addr.str() << " (CONSIDERED ATTACK ATTEMPT)");
        // This came not from the address that is the peer associated
        // with the socket. Ignore it.
        return CONN_AGAIN;
    }

    if (!u->m_bConnected || u->m_bBroken || u->m_bClosing)
    {
        u->m_RejectReason = SRT_REJ_CLOSE;
        // The socket is currently in the process of being disconnected
        // or destroyed. Ignore.
        // XXX send UMSG_SHUTDOWN in this case?
        // XXX May it require mutex protection?
        return CONN_REJECT;
    }

    if (unit->m_Packet.isControl())
        u->processCtrl(unit->m_Packet);
    else
        u->processData(unit);

    u->checkTimers();
    m_pRcvUList->update(u);

    return CONN_RUNNING;
}

// This function responds to the fact that a packet has come
// for a socket that does not expect to receive a normal connection
// request. This can be then:
// - a normal packet of whatever kind, just to be processed by the message loop
// - a rendezvous connection
// This function then tries to manage the packet as a rendezvous connection
// request in ASYNC mode; when this is not applicable, it stores the packet
// in the "receiving queue" so that it will be picked up in the "main" thread.
srt::EConnectStatus srt::CRcvQueue::worker_TryAsyncRend_OrStore(int32_t id, CUnit* unit, const sockaddr_any& addr)
{
    // This 'retrieve' requires that 'id' be either one of those
    // stored in the rendezvous queue (see CRcvQueue::registerConnector)
    // or simply 0, but then at least the address must match one of these.
    // If the id was 0, it will be set to the actual socket ID of the returned CUDT.
    CUDT* u = m_pRendezvousQueue->retrieve(addr, (id));
    if (!u)
    {
        // this socket is then completely unknown to the system.
        // Note that this situation may also happen at a very unfortunate
        // coincidence that the socket is already bound, but the registerConnector()
        // has not yet started. In case of rendezvous this may mean that the other
        // side just started sending its handshake packets, the local side has already
        // run the CRcvQueue::worker thread, and this worker thread is trying to dispatch
        // the handshake packet too early, before the dispatcher has a chance to see
        // this socket registerred in the RendezvousQueue, which causes the packet unable
        // to be dispatched. Therefore simply treat every "out of band" packet (with socket
        // not belonging to the connection and not registered as rendezvous) as "possible
        // attack" and ignore it. This also should better protect the rendezvous socket
        // against a rogue connector.
        if (id == 0)
        {
            HLOGC(cnlog.Debug,
                  log << CONID() << "AsyncOrRND: no sockets expect connection from " << addr.str()
                      << " - POSSIBLE ATTACK, ignore packet");
        }
        else
        {
            HLOGC(cnlog.Debug,
                  log << CONID() << "AsyncOrRND: no sockets expect socket " << id << " from " << addr.str()
                      << " - POSSIBLE ATTACK, ignore packet");
        }
        return CONN_AGAIN; // This means that the packet should be ignored.
    }

    // asynchronous connect: call connect here
    // otherwise wait for the UDT socket to retrieve this packet
    if (!u->m_config.bSynRecving)
    {
        HLOGC(cnlog.Debug, log << "AsyncOrRND: packet RESOLVED TO @" << id << " -- continuing as ASYNC CONNECT");
        // This is practically same as processConnectResponse, just this applies
        // appropriate mutex lock - which can't be done here because it's intentionally private.
        // OTOH it can't be applied to processConnectResponse because the synchronous
        // call to this method applies the lock by itself, and same-thread-double-locking is nonportable (crashable).
        EConnectStatus cst = u->processAsyncConnectResponse(unit->m_Packet);

        if (cst == CONN_CONFUSED)
        {
            LOGC(cnlog.Warn, log << "AsyncOrRND: PACKET NOT HANDSHAKE - re-requesting handshake from peer");
            storePkt(id, unit->m_Packet.clone());
            if (!u->processAsyncConnectRequest(RST_AGAIN, CONN_CONTINUE, &unit->m_Packet, u->m_PeerAddr))
            {
                // Reuse previous behavior to reject a packet
                cst = CONN_REJECT;
            }
            else
            {
                cst = CONN_CONTINUE;
            }
        }

        // It might be that this is a data packet, which has turned the connection
        // into "connected" state, removed the connector (so since now every next packet
        // will land directly in the queue), but this data packet shall still be delivered.
        if (cst == CONN_ACCEPT && !unit->m_Packet.isControl())
        {
            // The process as called through processAsyncConnectResponse() should have put the
            // socket into the pending queue for pending connection (don't ask me, this is so).
            // This pending queue is being purged every time in the beginning of this loop, so
            // currently the socket is in the pending queue, but not yet in the connection queue.
            // It will be done at the next iteration of the reading loop, but it will be too late,
            // we have a pending data packet now and we must either dispatch it to an already connected
            // socket or disregard it, and rather prefer the former. So do this transformation now
            // that we KNOW (by the cst == CONN_ACCEPT result) that the socket should be inserted
            // into the pending anteroom.

            CUDT* ne = getNewEntry(); // This function actuall removes the entry and returns it.
            // This **should** now always return a non-null value, but check it first
            // because if this accidentally isn't true, the call to worker_ProcessAddressedPacket will
            // result in redirecting it to here and so on until the call stack overflow. In case of
            // this "accident" simply disregard the packet from any further processing, it will be later
            // loss-recovered.
            // XXX (Probably the old contents of UDT's CRcvQueue::worker should be shaped a little bit
            // differently throughout the functions).
            if (ne)
            {
                HLOGC(cnlog.Debug,
                      log << CUDTUnited::CONID(ne->m_SocketID)
                          << " SOCKET pending for connection - ADDING TO RCV QUEUE/MAP");
                m_pRcvUList->insert(ne);
                m_pHash->insert(ne->m_SocketID, ne);

                // The current situation is that this has passed processAsyncConnectResponse, but actually
                // this packet *SHOULD HAVE BEEN* handled by worker_ProcessAddressedPacket, however the
                // connection state wasn't completed at the moment when dispatching this packet. This has
                // been now completed inside the call to processAsyncConnectResponse, but this is still a
                // data packet that should have expected the connection to be already established. Therefore
                // redirect it once again into worker_ProcessAddressedPacket here.

                HLOGC(cnlog.Debug,
                      log << "AsyncOrRND: packet SWITCHED TO CONNECTED with ID=" << id
                          << " -- passing to worker_ProcessAddressedPacket");

                // Theoretically we should check if m_pHash->lookup(ne->m_SocketID) returns 'ne', but this
                // has been just added to m_pHash, so the check would be extremely paranoid here.
                cst = worker_ProcessAddressedPacket(id, unit, addr);
                if (cst == CONN_REJECT)
                    return cst;
                return CONN_ACCEPT; // this function usually will return CONN_CONTINUE, which doesn't represent current
                                    // situation.
            }
            else
            {
                LOGC(cnlog.Error,
                     log << "IPE: AsyncOrRND: packet SWITCHED TO CONNECTED, but ID=" << id
                         << " is still not present in the socket ID dispatch hash - DISREGARDING");
            }
        }
        return cst;
    }
    HLOGC(cnlog.Debug,
          log << "AsyncOrRND: packet RESOLVED TO ID=" << id << " -- continuing through CENTRAL PACKET QUEUE");
    // This is where also the packets for rendezvous connection will be landing,
    // in case of a synchronous connection.
    storePkt(id, unit->m_Packet.clone());

    return CONN_CONTINUE;
}

void srt::CRcvQueue::stopWorker()
{
    // We use the decent way, so we say to the thread "please exit".
    m_bClosing = true;

    // Sanity check of the function's affinity.
    if (srt::sync::this_thread::get_id() == m_WorkerThread.get_id())
    {
        LOGC(rslog.Error, log << "IPE: RcvQ:WORKER TRIES TO CLOSE ITSELF!");
        return; // do nothing else, this would cause a hangup or crash.
    }

    HLOGC(rslog.Debug, log << "RcvQueue: EXIT (forced)");
    // And we trust the thread that it does.
    m_WorkerThread.join();
}

int srt::CRcvQueue::recvfrom(int32_t id, CPacket& w_packet)
{
    CUniqueSync buffercond(m_BufferLock, m_BufferCond);

    map<int32_t, std::queue<CPacket*> >::iterator i = m_mBuffer.find(id);

    if (i == m_mBuffer.end())
    {
        THREAD_PAUSED();
        buffercond.wait_for(seconds_from(1));
        THREAD_RESUMED();

        i = m_mBuffer.find(id);
        if (i == m_mBuffer.end())
        {
            w_packet.setLength(-1);
            return -1;
        }
    }

    // retrieve the earliest packet
    CPacket* newpkt = i->second.front();

    if (w_packet.getLength() < newpkt->getLength())
    {
        w_packet.setLength(-1);
        return -1;
    }

    // copy packet content
    // XXX Check if this wouldn't be better done by providing
    // copy constructor for DynamicStruct.
    // XXX Another thing: this looks wasteful. This expects an already
    // allocated memory on the packet, this thing gets the packet,
    // copies it into the passed packet and then the source packet
    // gets deleted. Why not simply return the originally stored packet,
    // without copying, allocation and deallocation?
    memcpy((w_packet.m_nHeader), newpkt->m_nHeader, CPacket::HDR_SIZE);
    memcpy((w_packet.m_pcData), newpkt->m_pcData, newpkt->getLength());
    w_packet.setLength(newpkt->getLength());

    delete[] newpkt->m_pcData;
    delete newpkt;

    // remove this message from queue,
    // if no more messages left for this socket, release its data structure
    i->second.pop();
    if (i->second.empty())
        m_mBuffer.erase(i);

    return (int)w_packet.getLength();
}

int srt::CRcvQueue::setListener(CUDT* u)
{
    ScopedLock lslock(m_LSLock);

    if (NULL != m_pListener)
        return -1;

    m_pListener = u;
    return 0;
}

void srt::CRcvQueue::removeListener(const CUDT* u)
{
    ScopedLock lslock(m_LSLock);

    if (u == m_pListener)
        m_pListener = NULL;
}

void srt::CRcvQueue::registerConnector(const SRTSOCKET&                id,
                                  CUDT*                           u,
                                  const sockaddr_any&             addr,
                                  const steady_clock::time_point& ttl)
{
    HLOGC(cnlog.Debug,
          log << "registerConnector: adding @" << id << " addr=" << addr.str() << " TTL=" << FormatTime(ttl));
    m_pRendezvousQueue->insert(id, u, addr, ttl);
}

void srt::CRcvQueue::removeConnector(const SRTSOCKET& id)
{
    HLOGC(cnlog.Debug, log << "removeConnector: removing @" << id);
    m_pRendezvousQueue->remove(id);

    ScopedLock bufferlock(m_BufferLock);

    map<int32_t, std::queue<CPacket*> >::iterator i = m_mBuffer.find(id);
    if (i != m_mBuffer.end())
    {
        HLOGC(cnlog.Debug,
              log << "removeConnector: ... and its packet queue with " << i->second.size() << " packets collected");
        while (!i->second.empty())
        {
            delete[] i->second.front()->m_pcData;
            delete i->second.front();
            i->second.pop();
        }
        m_mBuffer.erase(i);
    }
}

void srt::CRcvQueue::setNewEntry(CUDT* u)
{
    HLOGC(cnlog.Debug, log << CUDTUnited::CONID(u->m_SocketID) << "setting socket PENDING FOR CONNECTION");
    ScopedLock listguard(m_IDLock);
    m_vNewEntry.push_back(u);
}

bool srt::CRcvQueue::ifNewEntry()
{
    return !(m_vNewEntry.empty());
}

srt::CUDT* srt::CRcvQueue::getNewEntry()
{
    ScopedLock listguard(m_IDLock);

    if (m_vNewEntry.empty())
        return NULL;

    CUDT* u = (CUDT*)*(m_vNewEntry.begin());
    m_vNewEntry.erase(m_vNewEntry.begin());

    return u;
}

void srt::CRcvQueue::storePkt(int32_t id, CPacket* pkt)
{
    CUniqueSync passcond(m_BufferLock, m_BufferCond);

    map<int32_t, std::queue<CPacket*> >::iterator i = m_mBuffer.find(id);

    if (i == m_mBuffer.end())
    {
        m_mBuffer[id].push(pkt);
        passcond.notify_one();
    }
    else
    {
        // avoid storing too many packets, in case of malfunction or attack
        if (i->second.size() > 16)
            return;

        i->second.push(pkt);
    }
}

void srt::CMultiplexer::destroy()
{
    // Reverse order of the assigned
    delete m_pRcvQueue;
    delete m_pSndQueue;
    delete m_pTimer;

    if (m_pChannel)
    {
        m_pChannel->close();
        delete m_pChannel;
    }
}
