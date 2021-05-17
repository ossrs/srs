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


#ifndef __UDT_QUEUE_H__
#define __UDT_QUEUE_H__

#include "channel.h"
#include "common.h"
#include "packet.h"
#include "netinet_any.h"
#include "utilities.h"
#include <list>
#include <map>
#include <queue>
#include <vector>

class CUDT;

struct CUnit
{
   CPacket m_Packet;		// packet
   enum Flag { FREE = 0, GOOD = 1, PASSACK = 2, DROPPED = 3 };
   Flag m_iFlag;			// 0: free, 1: occupied, 2: msg read but not freed (out-of-order), 3: msg dropped
};

class CUnitQueue
{

public:

   CUnitQueue();
   ~CUnitQueue();

public:     // Storage size operations

      /// Initialize the unit queue.
      /// @param [in] size queue size
      /// @param [in] mss maximum segment size
      /// @param [in] version IP version
      /// @return 0: success, -1: failure.

   int init(int size, int mss, int version);

      /// Increase (double) the unit queue size.
      /// @return 0: success, -1: failure.

   int increase();

      /// Decrease (halve) the unit queue size.
      /// @return 0: success, -1: failure.

   int shrink();

public:     // Operations on units

      /// find an available unit for incoming packet.
      /// @return Pointer to the available unit, NULL if not found.

   CUnit* getNextAvailUnit();


   void makeUnitFree(CUnit * unit);

   void makeUnitGood(CUnit * unit);

public:

    inline int getIPversion() const { return m_iIPversion; }


private:
   struct CQEntry
   {
      CUnit* m_pUnit;		// unit queue
      char* m_pBuffer;		// data buffer
      int m_iSize;		// size of each queue

      CQEntry* m_pNext;
   }
   *m_pQEntry,			// pointer to the first unit queue
   *m_pCurrQueue,		// pointer to the current available queue
   *m_pLastQueue;		// pointer to the last unit queue

   CUnit* m_pAvailUnit;         // recent available unit

   int m_iSize;			// total size of the unit queue, in number of packets
   int m_iCount;		// total number of valid packets in the queue

   int m_iMSS;			// unit buffer size
   int m_iIPversion;		// IP version

private:
   CUnitQueue(const CUnitQueue&);
   CUnitQueue& operator=(const CUnitQueue&);
};

struct CSNode
{
   CUDT* m_pUDT;		// Pointer to the instance of CUDT socket
   uint64_t m_llTimeStamp_tk;      // Time Stamp

   int m_iHeapLoc;		// location on the heap, -1 means not on the heap
};

class CSndUList
{
friend class CSndQueue;

public:
   CSndUList();
   ~CSndUList();

public:

   enum EReschedule { DONT_RESCHEDULE = 0, DO_RESCHEDULE = 1 };

   static EReschedule rescheduleIf(bool cond) { return cond ? DO_RESCHEDULE : DONT_RESCHEDULE; }

      /// Update the timestamp of the UDT instance on the list.
      /// @param [in] u pointer to the UDT instance
      /// @param [in] reschedule if the timestamp should be rescheduled

   void update(const CUDT* u, EReschedule reschedule);

      /// Retrieve the next packet and peer address from the first entry, and reschedule it in the queue.
      /// @param [out] addr destination address of the next packet
      /// @param [out] pkt the next packet to be sent
      /// @return 1 if successfully retrieved, -1 if no packet found.

   int pop(sockaddr*& addr, CPacket& pkt);

      /// Remove UDT instance from the list.
      /// @param [in] u pointer to the UDT instance

   void remove(const CUDT* u);

      /// Retrieve the next scheduled processing time.
      /// @return Scheduled processing time of the first UDT socket in the list.

   uint64_t getNextProcTime();

private:

   /// Doubles the size of the list.
   ///
   void realloc_();

   /// Insert a new UDT instance into the list with realloc if required.
   ///
   /// @param [in] ts time stamp: next processing time
   /// @param [in] u pointer to the UDT instance
   void insert_(int64_t ts, const CUDT* u);

   /// Insert a new UDT instance into the list without realloc.
   /// Should be called if there is a gauranteed space for the element.
   ///
   /// @param [in] ts time stamp: next processing time
   /// @param [in] u pointer to the UDT instance
   void insert_norealloc_(int64_t ts, const CUDT* u);

   void remove_(const CUDT* u);

private:
   CSNode** m_pHeap;			// The heap array
   int m_iArrayLength;			// physical length of the array
   int m_iLastEntry;			// position of last entry on the heap array

   pthread_mutex_t m_ListLock;

   pthread_mutex_t* m_pWindowLock;
   pthread_cond_t* m_pWindowCond;

   CTimer* m_pTimer;

private:
   CSndUList(const CSndUList&);
   CSndUList& operator=(const CSndUList&);
};

struct CRNode
{
   CUDT* m_pUDT;                // Pointer to the instance of CUDT socket
   uint64_t m_llTimeStamp_tk;      // Time Stamp

   CRNode* m_pPrev;             // previous link
   CRNode* m_pNext;             // next link

   bool m_bOnList;              // if the node is already on the list
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
   CRNode* m_pUList;		// the head node

private:
   CRNode* m_pLast;		// the last node

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
      int32_t m_iID;		// Socket ID
      CUDT* m_pUDT;		// Socket instance

      CBucket* m_pNext;		// next bucket
   } **m_pBucket;		// list of buckets (the hash table)

   int m_iHashSize;		// size of hash table

private:
   CHash(const CHash&);
   CHash& operator=(const CHash&);
};

class CRendezvousQueue
{
public:
   CRendezvousQueue();
   ~CRendezvousQueue();

public:
   void insert(const SRTSOCKET& id, CUDT* u, int ipv, const sockaddr* addr, uint64_t ttl);

   // The should_lock parameter is given here to state as to whether
   // the lock should be applied here. If called from some internals
   // and the lock IS ALREADY APPLIED, use false here to prevent
   // double locking and deadlock in result.
   void remove(const SRTSOCKET& id, bool should_lock);
   CUDT* retrieve(const sockaddr* addr, ref_t<SRTSOCKET> id);

   void updateConnStatus(EReadStatus rst, EConnectStatus, const CPacket& response);

private:
   struct CRL
   {
      SRTSOCKET m_iID;			// UDT socket ID (self)
      CUDT* m_pUDT;			// UDT instance
      int m_iIPversion;                 // IP version
      sockaddr* m_pPeerAddr;		// UDT sonnection peer address
      uint64_t m_ullTTL;			// the time that this request expires
   };
   std::list<CRL> m_lRendezvousID;      // The sockets currently in rendezvous mode

   pthread_mutex_t m_RIDVectorLock;
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

   void init(CChannel* c, CTimer* t);

      /// Send out a packet to a given address.
      /// @param [in] addr destination address
      /// @param [in] packet packet to be sent out
      /// @return Size of data sent out.

   int sendto(const sockaddr* addr, CPacket& packet);

#ifdef SRT_ENABLE_IPOPTS
      /// Get the IP TTL.
      /// @param [in] ttl IP Time To Live.
      /// @return TTL.

   int getIpTTL() const;

      /// Get the IP Type of Service.
      /// @return ToS.

   int getIpToS() const;
#endif

   int ioctlQuery(int type) const { return m_pChannel->ioctlQuery(type); }
   int sockoptQuery(int level, int type) const { return m_pChannel->sockoptQuery(level, type); }

   void setClosing()
   {
       m_bClosing = true;
   }

private:
   static void* worker(void* param);
   pthread_t m_WorkerThread;


private:
   CSndUList* m_pSndUList;		// List of UDT instances for data sending
   CChannel* m_pChannel;                // The UDP channel for data sending
   CTimer* m_pTimer;			// Timing facility

   pthread_mutex_t m_WindowLock;
   pthread_cond_t m_WindowCond;

   volatile bool m_bClosing;		// closing the worker

#if defined(SRT_DEBUG_SNDQ_HIGHRATE)//>>debug high freq worker
   uint64_t m_ullDbgPeriod;
   uint64_t m_ullDbgTime;
   struct {
        unsigned long lIteration;   //
        unsigned long lSleepTo;     //SleepTo
        unsigned long lNotReadyPop; //Continue
        unsigned long lSendTo;
        unsigned long lNotReadyTs;  
        unsigned long lCondWait;    //block on m_WindowCond
   } m_WorkerStats;
#endif /* SRT_DEBUG_SNDQ_HIGHRATE */

private:
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

   void init(int size, int payload, int version, int hsize, CChannel* c, CTimer* t);

      /// Read a packet for a specific UDT socket id.
      /// @param [in] id Socket ID
      /// @param [out] packet received packet
      /// @return Data size of the packet

   int recvfrom(int32_t id, ref_t<CPacket> packet);

   void setClosing()
   {
       m_bClosing = true;
   }

private:
   static void* worker(void* param);
   pthread_t m_WorkerThread;
   // Subroutines of worker
   EReadStatus worker_RetrieveUnit(ref_t<int32_t> id, ref_t<CUnit*> unit, sockaddr* sa);
   EConnectStatus worker_ProcessConnectionRequest(CUnit* unit, const sockaddr* sa);
   EConnectStatus worker_TryAsyncRend_OrStore(int32_t id, CUnit* unit, const sockaddr* sa);
   EConnectStatus worker_ProcessAddressedPacket(int32_t id, CUnit* unit, const sockaddr* sa);

private:
   CUnitQueue m_UnitQueue;		// The received packet queue

   CRcvUList* m_pRcvUList;		// List of UDT instances that will read packets from the queue
   CHash* m_pHash;			// Hash table for UDT socket looking up
   CChannel* m_pChannel;		// UDP channel for receving packets
   CTimer* m_pTimer;			// shared timer with the snd queue

   int m_iPayloadSize;                  // packet payload size

   volatile bool m_bClosing;            // closing the worker

private:
   int setListener(CUDT* u);
   void removeListener(const CUDT* u);

   void registerConnector(const SRTSOCKET& id, CUDT* u, int ipv, const sockaddr* addr, uint64_t ttl);
   void removeConnector(const SRTSOCKET& id, bool should_lock = true);

   void setNewEntry(CUDT* u);
   bool ifNewEntry();
   CUDT* getNewEntry();

   void storePkt(int32_t id, CPacket* pkt);

private:
   pthread_mutex_t m_LSLock;
   CUDT* m_pListener;                                   // pointer to the (unique, if any) listening UDT entity
   CRendezvousQueue* m_pRendezvousQueue;                // The list of sockets in rendezvous mode

   std::vector<CUDT*> m_vNewEntry;                      // newly added entries, to be inserted
   pthread_mutex_t m_IDLock;

   std::map<int32_t, std::queue<CPacket*> > m_mBuffer;	// temporary buffer for rendezvous connection request
   pthread_mutex_t m_PassLock;
   pthread_cond_t m_PassCond;

private:
   CRcvQueue(const CRcvQueue&);
   CRcvQueue& operator=(const CRcvQueue&);
};

struct CMultiplexer
{
   CSndQueue* m_pSndQueue;  // The sending queue
   CRcvQueue* m_pRcvQueue;  // The receiving queue
   CChannel* m_pChannel;    // The UDP channel for sending and receiving
   CTimer* m_pTimer;        // The timer

   int m_iPort;         // The UDP port number of this multiplexer
   int m_iIPversion;    // IP version
#ifdef SRT_ENABLE_IPOPTS
   int m_iIpTTL;
   int m_iIpToS;
#endif
   int m_iMSS;          // Maximum Segment Size
   int m_iRefCount;     // number of UDT instances that are associated with this multiplexer
   int m_iIpV6Only;     // IPV6_V6ONLY option
   bool m_bReusable;    // if this one can be shared with others

   int m_iID;           // multiplexer ID
};

#endif
