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
   Yunhong Gu, last updated 01/12/2011
modified by
   Haivision Systems Inc.
*****************************************************************************/

#ifndef INC_SRT_QUEUE_H
#define INC_SRT_QUEUE_H

#include "common.h"
#include "packet.h"
#include "socketconfig.h"
#include "netinet_any.h"
#include "utilities.h"
#include <list>
#include <map>
#include <queue>
#include <vector>

namespace srt
{
class CChannel;
class CUDT;

struct CUnit
{
    CPacket m_Packet; // packet
    sync::atomic<bool> m_bTaken; // true if the unit is is use (can be stored in the RCV buffer).
};

class CUnitQueue
{
public:
    /// @brief Construct a unit queue.
    /// @param mss Initial number of units to allocate.
    /// @param mss Maximum segment size meaning the size of each unit.
    /// @throws CUDTException SRT_ENOBUF.
    CUnitQueue(int initNumUnits, int mss);
    ~CUnitQueue();

public:
    int capacity() const { return m_iSize; }
    int size() const { return m_iSize - m_iNumTaken; }

public:
    /// @brief Find an available unit for incoming packet. Allocate new units if 90% or more are in use.
    /// @note This function is not thread-safe. Currently only CRcvQueue::worker thread calls it, thus
    /// it is not an issue. However, must be protected if used from several threads in the future.
    /// @return Pointer to the available unit, NULL if not found.
    CUnit* getNextAvailUnit();

    void makeUnitFree(CUnit* unit);

    void makeUnitTaken(CUnit* unit);

private:
    struct CQEntry
    {
        CUnit* m_pUnit;   // unit queue
        char*  m_pBuffer; // data buffer
        int    m_iSize;   // size of each queue

        CQEntry* m_pNext;
    };

    /// Increase the unit queue size (by @a m_iBlockSize units).
    /// Uses m_mtx to protect access and changes of the queue state.
    /// @return 0: success, -1: failure.
    int increase_();

    /// @brief Allocated a CQEntry of iNumUnits with each unit of mss bytes.
    /// @param iNumUnits a number of units to allocate
    /// @param mss the size of each unit in bytes.
    /// @return a pointer to a newly allocated entry on success, NULL otherwise.
    static CQEntry* allocateEntry(const int iNumUnits, const int mss);

private:
    CQEntry* m_pQEntry;    // pointer to the first unit queue
    CQEntry* m_pCurrQueue; // pointer to the current available queue
    CQEntry* m_pLastQueue; // pointer to the last unit queue
    CUnit* m_pAvailUnit; // recent available unit
    int m_iSize;  // total size of the unit queue, in number of packets
    sync::atomic<int> m_iNumTaken; // total number of valid (occupied) packets in the queue
    const int m_iMSS; // unit buffer size
    const int m_iBlockSize; // Number of units in each CQEntry.

private:
    CUnitQueue(const CUnitQueue&);
    CUnitQueue& operator=(const CUnitQueue&);
};

struct CSNode
{
    CUDT*                          m_pUDT; // Pointer to the instance of CUDT socket
    sync::steady_clock::time_point m_tsTimeStamp;

    sync::atomic<int> m_iHeapLoc; // location on the heap, -1 means not on the heap
};

class CSndUList
{
public:
    CSndUList(sync::CTimer* pTimer);
    ~CSndUList();

public:
    enum EReschedule
    {
        DONT_RESCHEDULE = 0,
        DO_RESCHEDULE   = 1
    };

    static EReschedule rescheduleIf(bool cond) { return cond ? DO_RESCHEDULE : DONT_RESCHEDULE; }

    /// Update the timestamp of the UDT instance on the list.
    /// @param [in] u pointer to the UDT instance
    /// @param [in] reschedule if the timestamp should be rescheduled
    /// @param [in] ts the next time to trigger sending logic on the CUDT
    void update(const CUDT* u, EReschedule reschedule, sync::steady_clock::time_point ts = sync::steady_clock::now());

    /// Retrieve the next (in time) socket from the heap to process its sending request.
    /// @return a pointer to CUDT instance to process next.
    CUDT* pop();

    /// Remove UDT instance from the list.
    /// @param [in] u pointer to the UDT instance
    void remove(const CUDT* u);// EXCLUDES(m_ListLock);

    /// Retrieve the next scheduled processing time.
    /// @return Scheduled processing time of the first UDT socket in the list.
    sync::steady_clock::time_point getNextProcTime();

    /// Wait for the list to become non empty.
    void waitNonEmpty() const;

    /// Signal to stop waiting in waitNonEmpty().
    void signalInterrupt() const;

private:
    /// Doubles the size of the list.
    ///
    void realloc_();// REQUIRES(m_ListLock);

    /// Insert a new UDT instance into the list with realloc if required.
    ///
    /// @param [in] ts time stamp: next processing time
    /// @param [in] u pointer to the UDT instance
    void insert_(const sync::steady_clock::time_point& ts, const CUDT* u);

    /// Insert a new UDT instance into the list without realloc.
    /// Should be called if there is a guaranteed space for the element.
    ///
    /// @param [in] ts time stamp: next processing time
    /// @param [in] u pointer to the UDT instance
    void insert_norealloc_(const sync::steady_clock::time_point& ts, const CUDT* u);// REQUIRES(m_ListLock);

    /// Removes CUDT entry from the list.
    /// If the last entry is removed, calls sync::CTimer::interrupt().
    void remove_(const CUDT* u);

private:
    CSNode** m_pHeap;        // The heap array
    int      m_iArrayLength; // physical length of the array
    int      m_iLastEntry;   // position of last entry on the heap array or -1 if empty.

    mutable sync::Mutex     m_ListLock; // Protects the list (m_pHeap, m_iArrayLength, m_iLastEntry).
    mutable sync::Condition m_ListCond;

    sync::CTimer* const m_pTimer;

private:
    CSndUList(const CSndUList&);
    CSndUList& operator=(const CSndUList&);
};

struct CRNode
{
    CUDT*                          m_pUDT;        // Pointer to the instance of CUDT socket
    sync::steady_clock::time_point m_tsTimeStamp; // Time Stamp

    CRNode* m_pPrev; // previous link
    CRNode* m_pNext; // next link

    sync::atomic<bool> m_bOnList; // if the node is already on the list
};

class CRcvUList
{
public:
    CRcvUList();
    ~CRcvUList();

public:
    /// Insert a new UDT instance to the list.
    /// @param [in] u pointer to the UDT instance

    void insert(const CUDT* u);

    /// Remove the UDT instance from the list.
    /// @param [in] u pointer to the UDT instance

    void remove(const CUDT* u);

    /// Move the UDT instance to the end of the list, if it already exists; otherwise, do nothing.
    /// @param [in] u pointer to the UDT instance

    void update(const CUDT* u);

public:
    CRNode* m_pUList; // the head node

private:
    CRNode* m_pLast; // the last node

private:
    CRcvUList(const CRcvUList&);
    CRcvUList& operator=(const CRcvUList&);
};

class CHash
{
public:
    CHash();
    ~CHash();

public:
    /// Initialize the hash table.
    /// @param [in] size hash table size

    void init(int size);

    /// Look for a UDT instance from the hash table.
    /// @param [in] id socket ID
    /// @return Pointer to a UDT instance, or NULL if not found.

    CUDT* lookup(int32_t id);

    /// Insert an entry to the hash table.
    /// @param [in] id socket ID
    /// @param [in] u pointer to the UDT instance

    void insert(int32_t id, CUDT* u);

    /// Remove an entry from the hash table.
    /// @param [in] id socket ID

    void remove(int32_t id);

private:
    struct CBucket
    {
        int32_t m_iID;  // Socket ID
        CUDT*   m_pUDT; // Socket instance

        CBucket* m_pNext; // next bucket
    } * *m_pBucket;       // list of buckets (the hash table)

    int m_iHashSize; // size of hash table

private:
    CHash(const CHash&);
    CHash& operator=(const CHash&);
};

/// @brief A queue of sockets pending for connection.
/// It can be either a caller socket in a non-blocking mode
/// (the connection has to be handled in background),
/// or a socket in rendezvous connection mode.
class CRendezvousQueue
{
public:
    CRendezvousQueue();
    ~CRendezvousQueue();

public:
    /// @brief Insert a new socket pending for connection (non-blocking caller or rendezvous).
    /// @param id socket ID.
    /// @param u pointer to a corresponding CUDT instance.
    /// @param addr remote address to connect to.
    /// @param ttl timepoint for connection attempt to expire.
    void insert(const SRTSOCKET& id, CUDT* u, const sockaddr_any& addr, const srt::sync::steady_clock::time_point& ttl);

    /// @brief Remove a socket from the connection pending list.
    /// @param id socket ID.
    void remove(const SRTSOCKET& id);

    /// @brief Locate a socket in the connection pending queue.
    /// @param addr source address of the packet received over UDP (peer address).
    /// @param id socket ID.
    /// @return a pointer to CUDT instance retrieved, or NULL if nothing was found.
    CUDT* retrieve(const sockaddr_any& addr, SRTSOCKET& id) const;

    /// @brief Update status of connections in the pending queue.
    /// Stop connecting if TTL expires. Resend handshake request every 250 ms if no response from the peer.
    /// @param rst result of reading from a UDP socket: received packet / nothin read / read error.
    /// @param cst target status for pending connection: reject or proceed.
    /// @param pktIn packet received from the UDP socket.
    void updateConnStatus(EReadStatus rst, EConnectStatus cst, CUnit* unit);

private:
    struct LinkStatusInfo
    {
        CUDT*        u;
        SRTSOCKET    id;
        int          errorcode;
        sockaddr_any peeraddr;
        int          token;

        struct HasID
        {
            SRTSOCKET id;
            HasID(SRTSOCKET p)
                : id(p)
            {
            }
            bool operator()(const LinkStatusInfo& i) { return i.id == id; }
        };
    };

    /// @brief Qualify pending connections:
    /// - Sockets with expired TTL go to the 'to_remove' list and removed from the queue straight away.
    /// - If HS request is to be resent (resend 250 ms if no response from the peer) go to the 'to_process' list.
    ///
    /// @param rst result of reading from a UDP socket: received packet / nothin read / read error.
    /// @param cst target status for pending connection: reject or proceed.
    /// @param iDstSockID destination socket ID of the received packet.
    /// @param[in,out] toRemove stores sockets with expired TTL.
    /// @param[in,out] toProcess stores sockets which should repeat (resend) HS connection request.
    bool qualifyToHandle(EReadStatus                  rst,
                         EConnectStatus               cst,
                         int                          iDstSockID,
                         std::vector<LinkStatusInfo>& toRemove,
                         std::vector<LinkStatusInfo>& toProcess);

private:
    struct CRL
    {
        SRTSOCKET                      m_iID;      // SRT socket ID (self)
        CUDT*                          m_pUDT;     // CUDT instance
        sockaddr_any                   m_PeerAddr; // SRT sonnection peer address
        sync::steady_clock::time_point m_tsTTL;    // the time that this request expires
    };
    std::list<CRL> m_lRendezvousID; // The sockets currently in rendezvous mode

    mutable sync::Mutex m_RIDListLock;
};

class CSndQueue
{
    friend class CUDT;
    friend class CUDTUnited;

public:
    CSndQueue();
    ~CSndQueue();

public:
    // XXX There's currently no way to access the socket ID set for
    // whatever the queue is currently working for. Required to find
    // some way to do this, possibly by having a "reverse pointer".
    // Currently just "unimplemented".
    std::string CONID() const { return ""; }

    /// Initialize the sending queue.
    /// @param [in] c UDP channel to be associated to the queue
    /// @param [in] t Timer
    void init(CChannel* c, sync::CTimer* t);

    /// Send out a packet to a given address. The @a src parameter is
    /// blindly passed by the caller down the call with intention to
    /// be received eventually by CChannel::sendto, and used only if
    /// appropriate conditions state so.
    /// @param [in] addr destination address
    /// @param [in,ref] packet packet to be sent out
    /// @param [in] src The source IP address (details above)
    /// @return Size of data sent out.
    int sendto(const sockaddr_any& addr, CPacket& packet, const sockaddr_any& src);

    /// Get the IP TTL.
    /// @param [in] ttl IP Time To Live.
    /// @return TTL.
    int getIpTTL() const;

    /// Get the IP Type of Service.
    /// @return ToS.
    int getIpToS() const;

#ifdef SRT_ENABLE_BINDTODEVICE
    bool getBind(char* dst, size_t len) const;
#endif

    int ioctlQuery(int type) const;
    int sockoptQuery(int level, int type) const;

    void setClosing() { m_bClosing = true; }

private:
    static void*  worker(void* param);
    sync::CThread m_WorkerThread;

private:
    CSndUList*    m_pSndUList; // List of UDT instances for data sending
    CChannel*     m_pChannel;  // The UDP channel for data sending
    sync::CTimer* m_pTimer;    // Timing facility

    sync::atomic<bool> m_bClosing;            // closing the worker

public:
#if defined(SRT_DEBUG_SNDQ_HIGHRATE) //>>debug high freq worker
    sync::steady_clock::duration m_DbgPeriod;
    mutable sync::steady_clock::time_point m_DbgTime;
    struct
    {
        unsigned long lIteration;   //
        unsigned long lSleepTo;     // SleepTo
        unsigned long lNotReadyPop; // Continue
        unsigned long lSendTo;
        unsigned long lNotReadyTs;
        unsigned long lCondWait; // block on m_WindowCond
    } mutable m_WorkerStats;
#endif /* SRT_DEBUG_SNDQ_HIGHRATE */

private:

#if ENABLE_LOGGING
    static int m_counter;
#endif

    CSndQueue(const CSndQueue&);
    CSndQueue& operator=(const CSndQueue&);
};

class CRcvQueue
{
    friend class CUDT;
    friend class CUDTUnited;

public:
    CRcvQueue();
    ~CRcvQueue();

public:
    // XXX There's currently no way to access the socket ID set for
    // whatever the queue is currently working. Required to find
    // some way to do this, possibly by having a "reverse pointer".
    // Currently just "unimplemented".
    std::string CONID() const { return ""; }

    /// Initialize the receiving queue.
    /// @param [in] size queue size
    /// @param [in] mss maximum packet size
    /// @param [in] version IP version
    /// @param [in] hsize hash table size
    /// @param [in] c UDP channel to be associated to the queue
    /// @param [in] t timer
    void init(int size, size_t payload, int version, int hsize, CChannel* c, sync::CTimer* t);

    /// Read a packet for a specific UDT socket id.
    /// @param [in] id Socket ID
    /// @param [out] packet received packet
    /// @return Data size of the packet
    int recvfrom(int32_t id, CPacket& to_packet);

    void stopWorker();

    void setClosing() { m_bClosing = true; }

    int getIPversion() { return m_iIPversion; }

private:
    static void*  worker(void* param);
    sync::CThread m_WorkerThread;
    // Subroutines of worker
    EReadStatus    worker_RetrieveUnit(int32_t& id, CUnit*& unit, sockaddr_any& sa);
    EConnectStatus worker_ProcessConnectionRequest(CUnit* unit, const sockaddr_any& sa);
    EConnectStatus worker_TryAsyncRend_OrStore(int32_t id, CUnit* unit, const sockaddr_any& sa);
    EConnectStatus worker_ProcessAddressedPacket(int32_t id, CUnit* unit, const sockaddr_any& sa);

private:
    CUnitQueue*   m_pUnitQueue; // The received packet queue
    CRcvUList*    m_pRcvUList;  // List of UDT instances that will read packets from the queue
    CHash*        m_pHash;      // Hash table for UDT socket looking up
    CChannel*     m_pChannel;   // UDP channel for receiving packets
    sync::CTimer* m_pTimer;     // shared timer with the snd queue

    int m_iIPversion;           // IP version
    size_t m_szPayloadSize;     // packet payload size

    sync::atomic<bool> m_bClosing; // closing the worker
#if ENABLE_LOGGING
    static srt::sync::atomic<int> m_counter; // A static counter to log RcvQueue worker thread number.
#endif

private:
    int  setListener(CUDT* u);
    void removeListener(const CUDT* u);

    void registerConnector(const SRTSOCKET&                      id,
                           CUDT*                                 u,
                           const sockaddr_any&                   addr,
                           const sync::steady_clock::time_point& ttl);
    void removeConnector(const SRTSOCKET& id);

    void  setNewEntry(CUDT* u);
    bool  ifNewEntry();
    CUDT* getNewEntry();

    void storePktClone(int32_t id, const CPacket& pkt);

private:
    sync::Mutex       m_LSLock;
    CUDT*             m_pListener;        // pointer to the (unique, if any) listening UDT entity
    CRendezvousQueue* m_pRendezvousQueue; // The list of sockets in rendezvous mode

    std::vector<CUDT*> m_vNewEntry; // newly added entries, to be inserted
    sync::Mutex        m_IDLock;

    std::map<int32_t, std::queue<CPacket*> > m_mBuffer; // temporary buffer for rendezvous connection request
    sync::Mutex                              m_BufferLock;
    sync::Condition                          m_BufferCond;

private:
    CRcvQueue(const CRcvQueue&);
    CRcvQueue& operator=(const CRcvQueue&);
};

struct CMultiplexer
{
    CSndQueue*    m_pSndQueue; // The sending queue
    CRcvQueue*    m_pRcvQueue; // The receiving queue
    CChannel*     m_pChannel;  // The UDP channel for sending and receiving
    sync::CTimer* m_pTimer;    // The timer

    int m_iPort;      // The UDP port number of this multiplexer
    int m_iIPversion; // Address family (AF_INET or AF_INET6)
    int m_iRefCount;  // number of UDT instances that are associated with this multiplexer

    CSrtMuxerConfig m_mcfg;

    int m_iID; // multiplexer ID

    // Constructor should reset all pointers to NULL
    // to prevent dangling pointer when checking for memory alloc fails
    CMultiplexer()
        : m_pSndQueue(NULL)
        , m_pRcvQueue(NULL)
        , m_pChannel(NULL)
        , m_pTimer(NULL)
    {
    }

    void destroy();
};

} // namespace srt

#endif
