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
   Yunhong Gu, last updated 07/09/2011
modified by
   Haivision Systems Inc.
*****************************************************************************/

#include <exception>
#include <stdexcept>
#include <typeinfo>
#include <iterator>

#include <cstring>
#include "platform_sys.h"
#include "api.h"
#include "core.h"
#include "logging.h"
#include "threadname.h"
#include "srt.h"

#ifdef _WIN32
   #include <win/wintime.h>
#endif

#ifdef _MSC_VER
   #pragma warning(error: 4530)
#endif

using namespace std;
using namespace srt_logging;
extern LogConfig srt_logger_config;


CUDTSocket::CUDTSocket():
m_Status(SRTS_INIT),
m_ClosureTimeStamp(0),
m_iIPversion(0),
m_pSelfAddr(NULL),
m_pPeerAddr(NULL),
m_SocketID(0),
m_ListenSocket(0),
m_PeerID(0),
m_iISN(0),
m_pUDT(NULL),
m_pQueuedSockets(NULL),
m_pAcceptSockets(NULL),
m_AcceptCond(),
m_AcceptLock(),
m_uiBackLog(0),
m_iMuxID(-1)
{
      pthread_mutex_init(&m_AcceptLock, NULL);
      pthread_cond_init(&m_AcceptCond, NULL);
      pthread_mutex_init(&m_ControlLock, NULL);
}

CUDTSocket::~CUDTSocket()
{
   if (m_iIPversion == AF_INET)
   {
      delete (sockaddr_in*)m_pSelfAddr;
      delete (sockaddr_in*)m_pPeerAddr;
   }
   else
   {
      delete (sockaddr_in6*)m_pSelfAddr;
      delete (sockaddr_in6*)m_pPeerAddr;
   }

   delete m_pUDT;
   m_pUDT = NULL;

   delete m_pQueuedSockets;
   delete m_pAcceptSockets;

   pthread_mutex_destroy(&m_AcceptLock);
   pthread_cond_destroy(&m_AcceptCond);
   pthread_mutex_destroy(&m_ControlLock);
}

////////////////////////////////////////////////////////////////////////////////

CUDTUnited::CUDTUnited():
m_Sockets(),
m_ControlLock(),
m_IDLock(),
m_SocketIDGenerator(0),
m_TLSError(),
m_mMultiplexer(),
m_MultiplexerLock(),
m_pCache(NULL),
m_bClosing(false),
m_GCStopLock(),
m_GCStopCond(),
m_InitLock(),
m_iInstanceCount(0),
m_bGCStatus(false),
m_GCThread(),
m_ClosedSockets()
{
   // Socket ID MUST start from a random value
   // Note. Don't use CTimer here, because s_UDTUnited is a static instance of CUDTUnited
   // with dynamic initialization (calling this constructor), while CTimer has
   // a static member s_ullCPUFrequency with dynamic initialization.
   // The order of initialization is not guaranteed.
   timeval t;
   gettimeofday(&t, 0);
   srand((unsigned int)t.tv_usec);
   m_SocketIDGenerator = 1 + (int)((1 << 30) * (double(rand()) / RAND_MAX));

   pthread_mutex_init(&m_ControlLock, NULL);
   pthread_mutex_init(&m_IDLock, NULL);
   pthread_mutex_init(&m_InitLock, NULL);

   pthread_key_create(&m_TLSError, TLSDestroy);

   m_pCache = new CCache<CInfoBlock>;
}

CUDTUnited::~CUDTUnited()
{
    // Call it if it wasn't called already.
    // This will happen at the end of main() of the application,
    // when the user didn't call srt_cleanup().
    if (m_bGCStatus)
    {
        cleanup();
    }

    pthread_mutex_destroy(&m_ControlLock);
    pthread_mutex_destroy(&m_IDLock);
    pthread_mutex_destroy(&m_InitLock);

    delete (CUDTException*)pthread_getspecific(m_TLSError);
    pthread_key_delete(m_TLSError);

    delete m_pCache;
}

std::string CUDTUnited::CONID(SRTSOCKET sock)
{
    if ( sock == 0 )
        return "";

    std::ostringstream os;
    os << "@" << sock << ":";
    return os.str();
}

int CUDTUnited::startup()
{
   CGuard gcinit(m_InitLock);

   if (m_iInstanceCount++ > 0)
      return 0;

   // Global initialization code
   #ifdef _WIN32
      WORD wVersionRequested;
      WSADATA wsaData;
      wVersionRequested = MAKEWORD(2, 2);

      if (0 != WSAStartup(wVersionRequested, &wsaData))
         throw CUDTException(MJ_SETUP, MN_NONE,  WSAGetLastError());
   #endif

   PacketFilter::globalInit();

   //init CTimer::EventLock

   if (m_bGCStatus)
      return true;

   m_bClosing = false;
   pthread_mutex_init(&m_GCStopLock, NULL);
#if ENABLE_MONOTONIC_CLOCK
   pthread_condattr_t  CondAttribs;
   pthread_condattr_init(&CondAttribs);
   pthread_condattr_setclock(&CondAttribs, CLOCK_MONOTONIC);
   pthread_cond_init(&m_GCStopCond, &CondAttribs);
#else
   pthread_cond_init(&m_GCStopCond, NULL);
#endif
   {
       ThreadName tn("SRT:GC");
       pthread_create(&m_GCThread, NULL, garbageCollect, this);
   }

   m_bGCStatus = true;

   return 0;
}

int CUDTUnited::cleanup()
{
   CGuard gcinit(m_InitLock);

   if (--m_iInstanceCount > 0)
      return 0;

   //destroy CTimer::EventLock

   if (!m_bGCStatus)
      return 0;

   m_bClosing = true;
   pthread_cond_signal(&m_GCStopCond);
   pthread_join(m_GCThread, NULL);
   
   // XXX There's some weird bug here causing this
   // to hangup on Windows. This might be either something
   // bigger, or some problem in pthread-win32. As this is
   // the application cleanup section, this can be temporarily
   // tolerated with simply exit the application without cleanup,
   // counting on that the system will take care of it anyway.
#ifndef _WIN32
   pthread_mutex_destroy(&m_GCStopLock);
   pthread_cond_destroy(&m_GCStopCond);
#endif

   m_bGCStatus = false;

   // Global destruction code
   #ifdef _WIN32
      WSACleanup();
   #endif

   return 0;
}

SRTSOCKET CUDTUnited::newSocket(int af, int)
{

   CUDTSocket* ns = NULL;

   try
   {
      // XXX REFACTOR:
      // Use sockaddr_any for m_pSelfAddr and just initialize it
      // with 'af'.
      ns = new CUDTSocket;
      ns->m_pUDT = new CUDT;
      if (af == AF_INET)
      {
         ns->m_pSelfAddr = (sockaddr*)(new sockaddr_in);
         ((sockaddr_in*)(ns->m_pSelfAddr))->sin_port = 0;
      }
      else
      {
         ns->m_pSelfAddr = (sockaddr*)(new sockaddr_in6);
         ((sockaddr_in6*)(ns->m_pSelfAddr))->sin6_port = 0;
      }
   }
   catch (...)
   {
      delete ns;
      throw CUDTException(MJ_SYSTEMRES, MN_MEMORY, 0);
   }

   CGuard::enterCS(m_IDLock);
   ns->m_SocketID = -- m_SocketIDGenerator;
   CGuard::leaveCS(m_IDLock);

   ns->m_Status = SRTS_INIT;
   ns->m_ListenSocket = 0;
   ns->m_pUDT->m_SocketID = ns->m_SocketID;
   // The "Socket type" is deprecated. For the sake of
   // HSv4 there will be only a "socket type" field set
   // in the handshake, always to UDT_DGRAM.
   //ns->m_pUDT->m_iSockType = (type == SOCK_STREAM) ? UDT_STREAM : UDT_DGRAM;
   ns->m_pUDT->m_iSockType = UDT_DGRAM;
   ns->m_pUDT->m_iIPversion = ns->m_iIPversion = af;
   ns->m_pUDT->m_pCache = m_pCache;

   // protect the m_Sockets structure.
   CGuard::enterCS(m_ControlLock);
   try
   {
      HLOGC(mglog.Debug, log << CONID(ns->m_SocketID)
         << "newSocket: mapping socket "
         << ns->m_SocketID);
      m_Sockets[ns->m_SocketID] = ns;
   }
   catch (...)
   {
      //failure and rollback
      CGuard::leaveCS(m_ControlLock);
      delete ns;
      ns = NULL;
   }
   CGuard::leaveCS(m_ControlLock);

   if (!ns)
      throw CUDTException(MJ_SYSTEMRES, MN_MEMORY, 0);

   return ns->m_SocketID;
}

int CUDTUnited::newConnection(const SRTSOCKET listen, const sockaddr* peer, CHandShake* hs, const CPacket& hspkt,
        ref_t<SRT_REJECT_REASON> r_error)
{
   CUDTSocket* ns = NULL;

   *r_error = SRT_REJ_IPE;

   // Can't manage this error through an exception because this is
   // running in the listener loop.
   CUDTSocket* ls = locate(listen);
   if (!ls)
   {
       LOGC(mglog.Error, log << "IPE: newConnection by listener socket id=" << listen << " which DOES NOT EXIST.");
       return -1;
   }

   // if this connection has already been processed
   if ((ns = locate(peer, hs->m_iID, hs->m_iISN)) != NULL)
   {
      if (ns->m_pUDT->m_bBroken)
      {
         // last connection from the "peer" address has been broken
         ns->m_Status = SRTS_CLOSED;
         ns->m_ClosureTimeStamp = CTimer::getTime();

         CGuard::enterCS(ls->m_AcceptLock);
         ls->m_pQueuedSockets->erase(ns->m_SocketID);
         ls->m_pAcceptSockets->erase(ns->m_SocketID);
         CGuard::leaveCS(ls->m_AcceptLock);
      }
      else
      {
         // connection already exist, this is a repeated connection request
         // respond with existing HS information

         hs->m_iISN = ns->m_pUDT->m_iISN;
         hs->m_iMSS = ns->m_pUDT->m_iMSS;
         hs->m_iFlightFlagSize = ns->m_pUDT->m_iFlightFlagSize;
         hs->m_iReqType = URQ_CONCLUSION;
         hs->m_iID = ns->m_SocketID;

         return 0;

         //except for this situation a new connection should be started
      }
   }

   // exceeding backlog, refuse the connection request
   if (ls->m_pQueuedSockets->size() >= ls->m_uiBackLog)
   {
       *r_error = SRT_REJ_BACKLOG;
       LOGC(mglog.Error, log << "newConnection: listen backlog=" << ls->m_uiBackLog << " EXCEEDED");
       return -1;
   }

   try
   {
      ns = new CUDTSocket;
      ns->m_pUDT = new CUDT(*(ls->m_pUDT));
      if (ls->m_iIPversion == AF_INET)
      {
         ns->m_pSelfAddr = (sockaddr*)(new sockaddr_in);
         ((sockaddr_in*)(ns->m_pSelfAddr))->sin_port = 0;
         ns->m_pPeerAddr = (sockaddr*)(new sockaddr_in);
         memcpy(ns->m_pPeerAddr, peer, sizeof(sockaddr_in));
      }
      else
      {
         ns->m_pSelfAddr = (sockaddr*)(new sockaddr_in6);
         ((sockaddr_in6*)(ns->m_pSelfAddr))->sin6_port = 0;
         ns->m_pPeerAddr = (sockaddr*)(new sockaddr_in6);
         memcpy(ns->m_pPeerAddr, peer, sizeof(sockaddr_in6));
      }
   }
   catch (...)
   {
      *r_error = SRT_REJ_RESOURCE;
      delete ns;
      LOGC(mglog.Error, log << "IPE: newConnection: unexpected exception (probably std::bad_alloc)");
      return -1;
   }

   CGuard::enterCS(m_IDLock);
   ns->m_SocketID = -- m_SocketIDGenerator;
   HLOGF(mglog.Debug, "newConnection: generated socket id %d", ns->m_SocketID);
   CGuard::leaveCS(m_IDLock);

   ns->m_ListenSocket = listen;
   ns->m_iIPversion = ls->m_iIPversion;
   ns->m_pUDT->m_SocketID = ns->m_SocketID;
   ns->m_PeerID = hs->m_iID;
   ns->m_iISN = hs->m_iISN;

   int error = 0;

   // Set the error code for all prospective problems below.
   // It won't be interpreted when result was successful.
   *r_error = SRT_REJ_RESOURCE;

   // These can throw exception only when the memory allocation failed.
   // CUDT::connect() translates exception into CUDTException.
   // CUDT::open() may only throw original std::bad_alloc from new.
   // This is only to make the library extra safe (when your machine lacks
   // memory, it will continue to work, but fail to accept connection).
   try
   {
       // This assignment must happen b4 the call to CUDT::connect() because
       // this call causes sending the SRT Handshake through this socket.
       // Without this mapping the socket cannot be found and therefore
       // the SRT Handshake message would fail.
       HLOGF(mglog.Debug, 
               "newConnection: incoming %s, mapping socket %d",
               SockaddrToString(peer).c_str(), ns->m_SocketID);
       {
           CGuard cg(m_ControlLock);
           m_Sockets[ns->m_SocketID] = ns;
       }

       // bind to the same addr of listening socket
       ns->m_pUDT->open();
       updateListenerMux(ns, ls);
       if (ls->m_pUDT->m_cbAcceptHook)
       {
           if (!ls->m_pUDT->runAcceptHook(ns->m_pUDT, peer, hs, hspkt))
           {
               error = 1;
               goto ERR_ROLLBACK;
           }
       }
       ns->m_pUDT->acceptAndRespond(peer, hs, hspkt);
   }
   catch (...)
   {
       // Extract the error that was set in this new failed entity.
       *r_error = ns->m_pUDT->m_RejectReason;
       error = 1;
       goto ERR_ROLLBACK;
   }

   ns->m_Status = SRTS_CONNECTED;

   // copy address information of local node
   ns->m_pUDT->m_pSndQueue->m_pChannel->getSockAddr(ns->m_pSelfAddr);
   CIPAddress::pton(ns->m_pSelfAddr, ns->m_pUDT->m_piSelfIP, ns->m_iIPversion);

   // protect the m_Sockets structure.
   CGuard::enterCS(m_ControlLock);
   try
   {
       HLOGF(mglog.Debug, 
               "newConnection: mapping peer %d to that socket (%d)\n",
               ns->m_PeerID, ns->m_SocketID);
       m_PeerRec[ns->getPeerSpec()].insert(ns->m_SocketID);
   }
   catch (...)
   {
      error = 2;
   }
   CGuard::leaveCS(m_ControlLock);

   CGuard::enterCS(ls->m_AcceptLock);
   try
   {
      ls->m_pQueuedSockets->insert(ns->m_SocketID);
   }
   catch (...)
   {
      error = 3;
   }
   CGuard::leaveCS(ls->m_AcceptLock);

   // acknowledge users waiting for new connections on the listening socket
   m_EPoll.update_events(listen, ls->m_pUDT->m_sPollID, UDT_EPOLL_IN, true);

   CTimer::triggerEvent();

   ERR_ROLLBACK:
   // XXX the exact value of 'error' is ignored
   if (error > 0)
   {
#if ENABLE_LOGGING
       static const char* why [] = {
           "UNKNOWN ERROR",
           "CONNECTION REJECTED",
           "IPE when mapping a socket",
           "IPE when inserting a socket"
       };
       LOGC(mglog.Error, log << CONID(ns->m_SocketID) << "newConnection: connection rejected due to: " << why[error]);
#endif
      SRTSOCKET id = ns->m_SocketID;
      ns->m_pUDT->close();
      ns->m_Status = SRTS_CLOSED;
      ns->m_ClosureTimeStamp = CTimer::getTime();
      // The mapped socket should be now unmapped to preserve the situation that
      // was in the original UDT code.
      // In SRT additionally the acceptAndRespond() function (it was called probably
      // connect() in UDT code) may fail, in which case this socket should not be
      // further processed and should be removed.
      {
          CGuard cg(m_ControlLock);
          m_Sockets.erase(id);
          m_ClosedSockets[id] = ns;
      }

      return -1;
   }

   // wake up a waiting accept() call
   pthread_mutex_lock(&(ls->m_AcceptLock));
   pthread_cond_signal(&(ls->m_AcceptCond));
   pthread_mutex_unlock(&(ls->m_AcceptLock));

   return 1;
}

int CUDTUnited::installAcceptHook(const SRTSOCKET lsn, srt_listen_callback_fn* hook, void* opaq)
{
    try
    {
        CUDT* lc = lookup(lsn);
        lc->installAcceptHook(hook, opaq);

    }
    catch (CUDTException& e)
    {
        setError(new CUDTException(e));
        return SRT_ERROR;
    }

    return 0;
}

CUDT* CUDTUnited::lookup(const SRTSOCKET u)
{
   // protects the m_Sockets structure
   CGuard cg(m_ControlLock);

   map<SRTSOCKET, CUDTSocket*>::iterator i = m_Sockets.find(u);

   if ((i == m_Sockets.end()) || (i->second->m_Status == SRTS_CLOSED))
      throw CUDTException(MJ_NOTSUP, MN_SIDINVAL, 0);

   return i->second->m_pUDT;
}

SRT_SOCKSTATUS CUDTUnited::getStatus(const SRTSOCKET u)
{
    // protects the m_Sockets structure
    CGuard cg(m_ControlLock);

    map<SRTSOCKET, CUDTSocket*>::const_iterator i = m_Sockets.find(u);

    if (i == m_Sockets.end())
    {
        if (m_ClosedSockets.find(u) != m_ClosedSockets.end())
            return SRTS_CLOSED;

        return SRTS_NONEXIST;
    }
    const CUDTSocket* s = i->second;

    if (s->m_pUDT->m_bBroken)
        return SRTS_BROKEN;

    // TTL in CRendezvousQueue::updateConnStatus() will set m_bConnecting to false.
    // Although m_Status is still SRTS_CONNECTING, the connection is in fact to be closed due to TTL expiry.
    // In this case m_bConnected is also false. Both checks are required to avoid hitting
    // a regular state transition from CONNECTING to CONNECTED.
    if ((s->m_Status == SRTS_CONNECTING) && !s->m_pUDT->m_bConnecting && !s->m_pUDT->m_bConnected)
        return SRTS_BROKEN;

    return s->m_Status;
}

int CUDTUnited::bind(const SRTSOCKET u, const sockaddr* name, int namelen)
{
   CUDTSocket* s = locate(u);
   if (!s)
      throw CUDTException(MJ_NOTSUP, MN_SIDINVAL, 0);

   CGuard cg(s->m_ControlLock);

   // cannot bind a socket more than once
   if (s->m_Status != SRTS_INIT)
      throw CUDTException(MJ_NOTSUP, MN_NONE, 0);

   // check the size of SOCKADDR structure
   if (s->m_iIPversion == AF_INET)
   {
      if (namelen != sizeof(sockaddr_in))
         throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);
   }
   else
   {
      if (namelen != sizeof(sockaddr_in6))
         throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);
   }

   s->m_pUDT->open();
   updateMux(s, name);
   s->m_Status = SRTS_OPENED;

   // copy address information of local node
   s->m_pUDT->m_pSndQueue->m_pChannel->getSockAddr(s->m_pSelfAddr);

   return 0;
}

int CUDTUnited::bind(SRTSOCKET u, UDPSOCKET udpsock)
{
   CUDTSocket* s = locate(u);
   if (!s)
      throw CUDTException(MJ_NOTSUP, MN_SIDINVAL, 0);

   CGuard cg(s->m_ControlLock);

   // cannot bind a socket more than once
   if (s->m_Status != SRTS_INIT)
      throw CUDTException(MJ_NOTSUP, MN_NONE, 0);

   sockaddr_in name4;
   sockaddr_in6 name6;
   sockaddr* name;
   socklen_t namelen;

   if (s->m_iIPversion == AF_INET)
   {
      namelen = sizeof(sockaddr_in);
      name = (sockaddr*)&name4;
   }
   else
   {
      namelen = sizeof(sockaddr_in6);
      name = (sockaddr*)&name6;
   }

   if (::getsockname(udpsock, name, &namelen) == -1)
      throw CUDTException(MJ_NOTSUP, MN_INVAL);

   s->m_pUDT->open();
   updateMux(s, name, &udpsock);
   s->m_Status = SRTS_OPENED;

   // copy address information of local node
   s->m_pUDT->m_pSndQueue->m_pChannel->getSockAddr(s->m_pSelfAddr);

   return 0;
}

int CUDTUnited::listen(const SRTSOCKET u, int backlog)
{
   if (backlog <= 0)
      throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);

   // Don't search for the socket if it's already -1;
   // this never is a valid socket.
   if (u == UDT::INVALID_SOCK)
      throw CUDTException(MJ_NOTSUP, MN_SIDINVAL, 0);

   CUDTSocket* s = locate(u);
   if (!s)
      throw CUDTException(MJ_NOTSUP, MN_SIDINVAL, 0);

   CGuard cg(s->m_ControlLock);

   // NOTE: since now the socket is protected against simultaneous access.
   // In the meantime the socket might have been closed, which means that
   // it could have changed the state. It could be also set listen in another
   // thread, so check it out.

   // do nothing if the socket is already listening
   if (s->m_Status == SRTS_LISTENING)
      return 0;

   // a socket can listen only if is in OPENED status
   if (s->m_Status != SRTS_OPENED)
      throw CUDTException(MJ_NOTSUP, MN_ISUNBOUND, 0);

   // [[using assert(s->m_Status == OPENED)]];

   // listen is not supported in rendezvous connection setup
   if (s->m_pUDT->m_bRendezvous)
      throw CUDTException(MJ_NOTSUP, MN_ISRENDEZVOUS, 0);

   s->m_uiBackLog = backlog;

   try
   {
      s->m_pQueuedSockets = new set<SRTSOCKET>;
      s->m_pAcceptSockets = new set<SRTSOCKET>;
   }
   catch (...)
   {
      delete s->m_pQueuedSockets;
      delete s->m_pAcceptSockets;

      // XXX Translated std::bad_alloc into CUDTException specifying
      // memory allocation failure...
      throw CUDTException(MJ_SYSTEMRES, MN_MEMORY, 0);
   }

   // [[using assert(s->m_Status == OPENED)]]; // (still, unchanged)

   s->m_pUDT->setListenState();  // propagates CUDTException,
                                 // if thrown, remains in OPENED state if so.
   s->m_Status = SRTS_LISTENING;

   return 0;
}

SRTSOCKET CUDTUnited::accept(const SRTSOCKET listen, sockaddr* addr, int* addrlen)
{
   if ((addr) && (!addrlen))
      throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);

   CUDTSocket* ls = locate(listen);

   if (ls == NULL)
      throw CUDTException(MJ_NOTSUP, MN_SIDINVAL, 0);

   // the "listen" socket must be in LISTENING status
   if (ls->m_Status != SRTS_LISTENING)
      throw CUDTException(MJ_NOTSUP, MN_NOLISTEN, 0);

   // no "accept" in rendezvous connection setup
   if (ls->m_pUDT->m_bRendezvous)
      throw CUDTException(MJ_NOTSUP, MN_ISRENDEZVOUS, 0);

   SRTSOCKET u = CUDT::INVALID_SOCK;
   bool accepted = false;

   // !!only one conection can be set up each time!!
   while (!accepted)
   {
       CGuard cg(ls->m_AcceptLock);

       if ((ls->m_Status != SRTS_LISTENING) || ls->m_pUDT->m_bBroken)
       {
           // This socket has been closed.
           accepted = true;
       }
       else if (ls->m_pQueuedSockets->size() > 0)
       {
           // XXX REFACTORING REQUIRED HERE!
           // Actually this should at best be something like that:
           // set<SRTSOCKET>::iterator b = ls->m_pQueuedSockets->begin();
           // u = *b;
           // ls->m_pQueuedSockets->erase(b);
           // ls->m_pAcceptSockets->insert(u);
           //
           // It is also questionable why m_pQueuedSockets should be of type 'set'.
           // There's no quick-searching capabilities of that container used anywhere except
           // checkBrokenSockets and garbageCollect, which aren't performance-critical,
           // whereas it's mainly used for getting the first element and iterating
           // over elements, which is slow in case of std::set. It's also doubtful
           // as to whether the sorting capability of std::set is properly used;
           // the first is taken here, which is actually the socket with lowest
           // possible descriptor value (as default operator< and ascending sorting
           // used for std::set<SRTSOCKET> where SRTSOCKET=int).
           //
           // Consider using std::list or std::vector here.

           u = *(ls->m_pQueuedSockets->begin());
           ls->m_pAcceptSockets->insert(ls->m_pAcceptSockets->end(), u);
           ls->m_pQueuedSockets->erase(ls->m_pQueuedSockets->begin());
           accepted = true;
       }
       else if (!ls->m_pUDT->m_bSynRecving)
       {
           accepted = true;
       }

       if (!accepted && (ls->m_Status == SRTS_LISTENING))
           pthread_cond_wait(&(ls->m_AcceptCond), &(ls->m_AcceptLock));

       if (ls->m_pQueuedSockets->empty())
           m_EPoll.update_events(listen, ls->m_pUDT->m_sPollID, UDT_EPOLL_IN, false);
   }

   if (u == CUDT::INVALID_SOCK)
   {
      // non-blocking receiving, no connection available
      if (!ls->m_pUDT->m_bSynRecving)
         throw CUDTException(MJ_AGAIN, MN_RDAVAIL, 0);

      // listening socket is closed
      throw CUDTException(MJ_NOTSUP, MN_NOLISTEN, 0);
   }

   if ((addr != NULL) && (addrlen != NULL))
   {
      CUDTSocket* s = locate(u);
      if (s == NULL)
         throw CUDTException(MJ_NOTSUP, MN_SIDINVAL, 0);

      CGuard cg(s->m_ControlLock);

      if (AF_INET == s->m_iIPversion)
         *addrlen = sizeof(sockaddr_in);
      else
         *addrlen = sizeof(sockaddr_in6);

      // copy address information of peer node
      memcpy(addr, s->m_pPeerAddr, *addrlen);
   }

   return u;
}

int CUDTUnited::connect(const SRTSOCKET u, const sockaddr* name, int namelen, int32_t forced_isn)
{
   CUDTSocket* s = locate(u);
   if (!s)
      throw CUDTException(MJ_NOTSUP, MN_SIDINVAL, 0);

   CGuard cg(s->m_ControlLock);

   // XXX Consider translating this to using sockaddr_any,
   // this should take out all the "IP version check" things.
   if (AF_INET == s->m_iIPversion)
   {
      if (namelen != sizeof(sockaddr_in))
         throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);
   }
   else
   {
      if (namelen != sizeof(sockaddr_in6))
         throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);
   }

   // a socket can "connect" only if it is in INIT or OPENED status
   if (s->m_Status == SRTS_INIT)
   {
      if (!s->m_pUDT->m_bRendezvous)
      {
         s->m_pUDT->open(); // XXX here use the AF_* family value from 'name'
         updateMux(s);  // <<---- updateMux
                        // -> C(Snd|Rcv)Queue::init
                        // -> pthread_create(...C(Snd|Rcv)Queue::worker...)
         s->m_Status = SRTS_OPENED;
      }
      else
         throw CUDTException(MJ_NOTSUP, MN_ISRENDUNBOUND, 0);
   }
   else if (s->m_Status != SRTS_OPENED)
      throw CUDTException(MJ_NOTSUP, MN_ISCONNECTED, 0);

   // connect_complete() may be called before connect() returns.
   // So we need to update the status before connect() is called,
   // otherwise the status may be overwritten with wrong value
   // (CONNECTED vs. CONNECTING).
   s->m_Status = SRTS_CONNECTING;

   /* 
   * In blocking mode, connect can block for up to 30 seconds for
   * rendez-vous mode. Holding the s->m_ControlLock prevent close
   * from cancelling the connect
   */
   try
   {
       // InvertedGuard unlocks in the constructor, then locks in the
       // destructor, no matter if an exception has fired.
       InvertedGuard l_unlocker( s->m_pUDT->m_bSynRecving ? &s->m_ControlLock : 0 );
       s->m_pUDT->startConnect(name, forced_isn);
   }
   catch (CUDTException& e) // Interceptor, just to change the state.
   {
      s->m_Status = SRTS_OPENED;
      throw e;
   }

   // record peer address
   delete s->m_pPeerAddr;
   if (AF_INET == s->m_iIPversion)
   {
      s->m_pPeerAddr = (sockaddr*)(new sockaddr_in);
      memcpy(s->m_pPeerAddr, name, sizeof(sockaddr_in));
   }
   else
   {
      s->m_pPeerAddr = (sockaddr*)(new sockaddr_in6);
      memcpy(s->m_pPeerAddr, name, sizeof(sockaddr_in6));
   }

   // CGuard destructor will delete cg and unlock s->m_ControlLock

   return 0;
}


int CUDTUnited::close(const SRTSOCKET u)
{
   CUDTSocket* s = locate(u);
   if (!s)
      throw CUDTException(MJ_NOTSUP, MN_SIDINVAL, 0);

   HLOGC(mglog.Debug, log << s->m_pUDT->CONID() << " CLOSE. Acquiring control lock");

   CGuard socket_cg(s->m_ControlLock);

   HLOGC(mglog.Debug, log << s->m_pUDT->CONID() << " CLOSING (removing from listening, closing CUDT)");

   bool synch_close_snd = s->m_pUDT->m_bSynSending;
   //bool synch_close_rcv = s->m_pUDT->m_bSynRecving;

   if (s->m_Status == SRTS_LISTENING)
   {
      if (s->m_pUDT->m_bBroken)
         return 0;

      s->m_ClosureTimeStamp = CTimer::getTime();
      s->m_pUDT->m_bBroken = true;

      // Change towards original UDT: 
      // Leave all the closing activities for garbageCollect to happen,
      // however remove the listener from the RcvQueue IMMEDIATELY.
      // Even though garbageCollect would eventually remove the listener
      // as well, there would be some time interval between now and the
      // moment when it's done, and during this time the application will
      // be unable to bind to this port that the about-to-delete listener
      // is currently occupying (due to blocked slot in the RcvQueue).

      HLOGC(mglog.Debug, log << s->m_pUDT->CONID() << " CLOSING (removing listener immediately)");
      {
          CGuard cg(s->m_pUDT->m_ConnectionLock);
          s->m_pUDT->m_bListening = false;
          s->m_pUDT->m_pRcvQueue->removeListener(s->m_pUDT);
      }

      // broadcast all "accept" waiting
      pthread_mutex_lock(&(s->m_AcceptLock));
      pthread_cond_broadcast(&(s->m_AcceptCond));
      pthread_mutex_unlock(&(s->m_AcceptLock));

   }
   else
   {
       s->m_pUDT->close();

       // synchronize with garbage collection.
       HLOGC(mglog.Debug, log << "@" << u << "U::close done. GLOBAL CLOSE: " << s->m_pUDT->CONID() << ". Acquiring GLOBAL control lock");
       CGuard manager_cg(m_ControlLock);

       // since "s" is located before m_ControlLock, locate it again in case
       // it became invalid
       map<SRTSOCKET, CUDTSocket*>::iterator i = m_Sockets.find(u);
       if ((i == m_Sockets.end()) || (i->second->m_Status == SRTS_CLOSED))
       {
           HLOGC(mglog.Debug, log << "@" << u << "U::close: NOT AN ACTIVE SOCKET, returning.");
           return 0;
       }
       s = i->second;

       s->m_Status = SRTS_CLOSED;

       // a socket will not be immediately removed when it is closed
       // in order to prevent other methods from accessing invalid address
       // a timer is started and the socket will be removed after approximately
       // 1 second
       s->m_ClosureTimeStamp = CTimer::getTime();

       m_Sockets.erase(s->m_SocketID);
       m_ClosedSockets[s->m_SocketID] = s;
       HLOGC(mglog.Debug, log << "@" << u << "U::close: Socket MOVED TO CLOSED for collecting later.");

       CTimer::triggerEvent();
   }

   HLOGC(mglog.Debug, log << "%" << u << ": GLOBAL: CLOSING DONE");

   // Check if the ID is still in closed sockets before you access it
   // (the last triggerEvent could have deleted it).
   if ( synch_close_snd )
   {
#if SRT_ENABLE_CLOSE_SYNCH

       HLOGC(mglog.Debug, log << "@" << u << " GLOBAL CLOSING: sync-waiting for releasing sender resources...");
       for (;;)
       {
           CSndBuffer* sb = s->m_pUDT->m_pSndBuffer;

           // Disconnected from buffer - nothing more to check.
           if (!sb)
           {
               HLOGC(mglog.Debug, log << "@" << u << " GLOBAL CLOSING: sending buffer disconnected. Allowed to close.");
               break;
           }

           // Sender buffer empty
           if (sb->getCurrBufSize() == 0)
           {
               HLOGC(mglog.Debug, log << "@" << u << " GLOBAL CLOSING: sending buffer depleted. Allowed to close.");
               break;
           }

           // Ok, now you are keeping GC thread hands off the internal data.
           // You can check then if it has already deleted the socket or not.
           // The socket is either in m_ClosedSockets or is already gone.

           // Done the other way, but still done. You can stop waiting.
           bool isgone = false;
           {
               CGuard manager_cg(m_ControlLock);
               isgone = m_ClosedSockets.count(u) == 0;
           }
           if (!isgone)
           {
               isgone = !s->m_pUDT->m_bOpened;
           }
           if (isgone)
           {
               HLOGC(mglog.Debug, log << "@" << u << " GLOBAL CLOSING: ... gone in the meantime, whatever. Exiting close().");
               break;
           }

           HLOGC(mglog.Debug, log << "@" << u << " GLOBAL CLOSING: ... still waiting for any update.");
           CTimer::EWait wt = CTimer::waitForEvent();

           if ( wt == CTimer::WT_ERROR )
           {
               HLOGC(mglog.Debug, log << "GLOBAL CLOSING: ... ERROR WHEN WAITING FOR EVENT. Exiting close() to prevent hangup.");
               break;
           }

           // Continue waiting in case when an event happened or 1s waiting time passed for checkpoint.
       }
#endif
   }

   /*
      This code is PUT ASIDE for now.
      Most likely this will be never required.
      It had to hold the closing activity until the time when the receiver buffer is depleted.
      However the closing of the socket should only happen when the receiver has received
      an information about that the reading is no longer possible (error report from recv/recvfile).
      When this happens, the receiver buffer is definitely depleted already and there's no need to check
      anything.

      Should there appear any other conditions in future under which the closing process should be
      delayed until the receiver buffer is empty, this code can be filled here.

   if ( synch_close_rcv )
   {
   ...
   }
   */

   return 0;
}

int CUDTUnited::getpeername(const SRTSOCKET u, sockaddr* name, int* namelen)
{
   if (getStatus(u) != SRTS_CONNECTED)
      throw CUDTException(MJ_CONNECTION, MN_NOCONN, 0);

   CUDTSocket* s = locate(u);

   if (!s)
      throw CUDTException(MJ_NOTSUP, MN_SIDINVAL, 0);

   if (!s->m_pUDT->m_bConnected || s->m_pUDT->m_bBroken)
      throw CUDTException(MJ_CONNECTION, MN_NOCONN, 0);

   if (AF_INET == s->m_iIPversion)
      *namelen = sizeof(sockaddr_in);
   else
      *namelen = sizeof(sockaddr_in6);

   // copy address information of peer node
   memcpy(name, s->m_pPeerAddr, *namelen);

   return 0;
}

int CUDTUnited::getsockname(const SRTSOCKET u, sockaddr* name, int* namelen)
{
   CUDTSocket* s = locate(u);

   if (!s)
      throw CUDTException(MJ_NOTSUP, MN_SIDINVAL, 0);

   if (s->m_pUDT->m_bBroken)
      throw CUDTException(MJ_NOTSUP, MN_SIDINVAL, 0);

   if (s->m_Status == SRTS_INIT)
      throw CUDTException(MJ_CONNECTION, MN_NOCONN, 0);

   if (AF_INET == s->m_iIPversion)
      *namelen = sizeof(sockaddr_in);
   else
      *namelen = sizeof(sockaddr_in6);

   // copy address information of local node
   memcpy(name, s->m_pSelfAddr, *namelen);

   return 0;
}

int CUDTUnited::select(
   ud_set* readfds, ud_set* writefds, ud_set* exceptfds, const timeval* timeout)
{
   uint64_t entertime = CTimer::getTime();

   uint64_t to;
   if (!timeout)
      to = 0xFFFFFFFFFFFFFFFFULL;
   else
      to = timeout->tv_sec * 1000000 + timeout->tv_usec;

   // initialize results
   int count = 0;
   set<SRTSOCKET> rs, ws, es;

   // retrieve related UDT sockets
   vector<CUDTSocket*> ru, wu, eu;
   CUDTSocket* s;
   if (readfds)
      for (set<SRTSOCKET>::iterator i1 = readfds->begin();
         i1 != readfds->end(); ++ i1)
      {
         if (getStatus(*i1) == SRTS_BROKEN)
         {
            rs.insert(*i1);
            ++ count;
         }
         else if (!(s = locate(*i1)))
            throw CUDTException(MJ_NOTSUP, MN_SIDINVAL, 0);
         else
            ru.push_back(s);
      }
   if (writefds)
      for (set<SRTSOCKET>::iterator i2 = writefds->begin();
         i2 != writefds->end(); ++ i2)
      {
         if (getStatus(*i2) == SRTS_BROKEN)
         {
            ws.insert(*i2);
            ++ count;
         }
         else if (!(s = locate(*i2)))
            throw CUDTException(MJ_NOTSUP, MN_SIDINVAL, 0);
         else
            wu.push_back(s);
      }
   if (exceptfds)
      for (set<SRTSOCKET>::iterator i3 = exceptfds->begin();
         i3 != exceptfds->end(); ++ i3)
      {
         if (getStatus(*i3) == SRTS_BROKEN)
         {
            es.insert(*i3);
            ++ count;
         }
         else if (!(s = locate(*i3)))
            throw CUDTException(MJ_NOTSUP, MN_SIDINVAL, 0);
         else
            eu.push_back(s);
      }

   do
   {
      // query read sockets
      for (vector<CUDTSocket*>::iterator j1 = ru.begin(); j1 != ru.end(); ++ j1)
      {
         s = *j1;

         if ((s->m_pUDT->m_bConnected
               && s->m_pUDT->m_pRcvBuffer->isRcvDataReady()
            )
            || (!s->m_pUDT->m_bListening
               && (s->m_pUDT->m_bBroken || !s->m_pUDT->m_bConnected))
            || (s->m_pUDT->m_bListening && (s->m_pQueuedSockets->size() > 0))
            || (s->m_Status == SRTS_CLOSED))
         {
            rs.insert(s->m_SocketID);
            ++ count;
         }
      }

      // query write sockets
      for (vector<CUDTSocket*>::iterator j2 = wu.begin(); j2 != wu.end(); ++ j2)
      {
         s = *j2;

         if ((s->m_pUDT->m_bConnected
               && (s->m_pUDT->m_pSndBuffer->getCurrBufSize()
                  < s->m_pUDT->m_iSndBufSize))
            || s->m_pUDT->m_bBroken
            || !s->m_pUDT->m_bConnected
            || (s->m_Status == SRTS_CLOSED))
         {
            ws.insert(s->m_SocketID);
            ++ count;
         }
      }

      // query exceptions on sockets
      for (vector<CUDTSocket*>::iterator j3 = eu.begin(); j3 != eu.end(); ++ j3)
      {
         // check connection request status, not supported now
      }

      if (0 < count)
         break;

      CTimer::waitForEvent();
   } while (to > CTimer::getTime() - entertime);

   if (readfds)
      *readfds = rs;

   if (writefds)
      *writefds = ws;

   if (exceptfds)
      *exceptfds = es;

   return count;
}

int CUDTUnited::selectEx(
   const vector<SRTSOCKET>& fds,
   vector<SRTSOCKET>* readfds,
   vector<SRTSOCKET>* writefds,
   vector<SRTSOCKET>* exceptfds,
   int64_t msTimeOut)
{
   uint64_t entertime = CTimer::getTime();

   uint64_t to;
   if (msTimeOut >= 0)
      to = msTimeOut * 1000;
   else
      to = 0xFFFFFFFFFFFFFFFFULL;

   // initialize results
   int count = 0;
   if (readfds)
      readfds->clear();
   if (writefds)
      writefds->clear();
   if (exceptfds)
      exceptfds->clear();

   do
   {
      for (vector<SRTSOCKET>::const_iterator i = fds.begin();
         i != fds.end(); ++ i)
      {
         CUDTSocket* s = locate(*i);

         if ((!s) || s->m_pUDT->m_bBroken || (s->m_Status == SRTS_CLOSED))
         {
            if (exceptfds)
            {
               exceptfds->push_back(*i);
               ++ count;
            }
            continue;
         }

         if (readfds)
         {
            if ((s->m_pUDT->m_bConnected
                  && s->m_pUDT->m_pRcvBuffer->isRcvDataReady()
               )
               || (s->m_pUDT->m_bListening
                  && (s->m_pQueuedSockets->size() > 0)))
            {
               readfds->push_back(s->m_SocketID);
               ++ count;
            }
         }

         if (writefds)
         {
            if (s->m_pUDT->m_bConnected
               && (s->m_pUDT->m_pSndBuffer->getCurrBufSize()
                  < s->m_pUDT->m_iSndBufSize))
            {
               writefds->push_back(s->m_SocketID);
               ++ count;
            }
         }
      }

      if (count > 0)
         break;

      CTimer::waitForEvent();
   } while (to > CTimer::getTime() - entertime);

   return count;
}

int CUDTUnited::epoll_create()
{
   return m_EPoll.create();
}

int CUDTUnited::epoll_add_usock(
   const int eid, const SRTSOCKET u, const int* events)
{
   CUDTSocket* s = locate(u);
   int ret = -1;
   if (s)
   {
      ret = m_EPoll.add_usock(eid, u, events);
      s->m_pUDT->addEPoll(eid);
   }
   else
   {
      throw CUDTException(MJ_NOTSUP, MN_SIDINVAL);
   }

   return ret;
}

int CUDTUnited::epoll_add_ssock(
   const int eid, const SYSSOCKET s, const int* events)
{
   return m_EPoll.add_ssock(eid, s, events);
}

int CUDTUnited::epoll_update_usock(
   const int eid, const SRTSOCKET u, const int* events)
{
   CUDTSocket* s = locate(u);
   int ret = -1;
   if (s)
   {
      ret = m_EPoll.update_usock(eid, u, events);
      s->m_pUDT->addEPoll(eid);
   }
   else
   {
      throw CUDTException(MJ_NOTSUP, MN_SIDINVAL);
   }

   return ret;
}

int CUDTUnited::epoll_update_ssock(
   const int eid, const SYSSOCKET s, const int* events)
{
   return m_EPoll.update_ssock(eid, s, events);
}

int CUDTUnited::epoll_remove_usock(const int eid, const SRTSOCKET u)
{
   int ret = m_EPoll.remove_usock(eid, u);

   CUDTSocket* s = locate(u);
   if (s)
   {
      s->m_pUDT->removeEPoll(eid);
   }
   //else
   //{
   //   throw CUDTException(MJ_NOTSUP, MN_SIDINVAL);
   //}

   return ret;
}

int CUDTUnited::epoll_remove_ssock(const int eid, const SYSSOCKET s)
{
   return m_EPoll.remove_ssock(eid, s);
}

int CUDTUnited::epoll_wait(
   const int eid,
   set<SRTSOCKET>* readfds,
   set<SRTSOCKET>* writefds,
   int64_t msTimeOut,
   set<SYSSOCKET>* lrfds,
   set<SYSSOCKET>* lwfds)
{
   return m_EPoll.wait(eid, readfds, writefds, msTimeOut, lrfds, lwfds);
}

int CUDTUnited::epoll_uwait(
   const int eid,
   SRT_EPOLL_EVENT* fdsSet,
   int fdsSize, 
   int64_t msTimeOut)
{
   return m_EPoll.uwait(eid, fdsSet, fdsSize, msTimeOut);
}

int32_t CUDTUnited::epoll_set(int eid, int32_t flags)
{
    return m_EPoll.setflags(eid, flags);
}

int CUDTUnited::epoll_release(const int eid)
{
   return m_EPoll.release(eid);
}

CUDTSocket* CUDTUnited::locate(const SRTSOCKET u)
{
   CGuard cg(m_ControlLock);

   map<SRTSOCKET, CUDTSocket*>::iterator i = m_Sockets.find(u);

   if ((i == m_Sockets.end()) || (i->second->m_Status == SRTS_CLOSED))
      return NULL;

   return i->second;
}

CUDTSocket* CUDTUnited::locate(
   const sockaddr* peer,
   const SRTSOCKET id,
   int32_t isn)
{
   CGuard cg(m_ControlLock);

   map<int64_t, set<SRTSOCKET> >::iterator i = m_PeerRec.find(
      CUDTSocket::getPeerSpec(id, isn));
   if (i == m_PeerRec.end())
      return NULL;

   for (set<SRTSOCKET>::iterator j = i->second.begin();
      j != i->second.end(); ++ j)
   {
      map<SRTSOCKET, CUDTSocket*>::iterator k = m_Sockets.find(*j);
      // this socket might have been closed and moved m_ClosedSockets
      if (k == m_Sockets.end())
         continue;

      if (CIPAddress::ipcmp(
         peer, k->second->m_pPeerAddr, k->second->m_iIPversion))
      {
         return k->second;
      }
   }

   return NULL;
}

void CUDTUnited::checkBrokenSockets()
{
   CGuard cg(m_ControlLock);

   // set of sockets To Be Closed and To Be Removed
   vector<SRTSOCKET> tbc;
   vector<SRTSOCKET> tbr;

   for (map<SRTSOCKET, CUDTSocket*>::iterator i = m_Sockets.begin();
      i != m_Sockets.end(); ++ i)
   {
       CUDTSocket* s = i->second;

      // HLOGF(mglog.Debug, "checking EXISTING socket: %d\n", i->first);
      // check broken connection
      if (s->m_pUDT->m_bBroken)
      {
         if (s->m_Status == SRTS_LISTENING)
         {
            uint64_t elapsed = CTimer::getTime() - s->m_ClosureTimeStamp;
            // for a listening socket, it should wait an extra 3 seconds
            // in case a client is connecting
            if (elapsed < 3000000) // XXX MAKE A SYMBOLIC CONSTANT HERE!
            {
               // HLOGF(mglog.Debug, "STILL KEEPING socket %d
               // (listener, too early, w8 %fs)\n", i->first,
               // double(elapsed)/1000000);
               continue;
            }
         }
         else if ((s->m_pUDT->m_pRcvBuffer != NULL)
            // FIXED: calling isRcvDataAvailable() just to get the information
            // whether there are any data waiting in the buffer,
            // NOT WHETHER THEY ARE ALSO READY TO PLAY at the time when
            // this function is called (isRcvDataReady also checks if the
            // available data is "ready to play").
            && s->m_pUDT->m_pRcvBuffer->isRcvDataAvailable()
            && (s->m_pUDT->m_iBrokenCounter -- > 0))
         {
            // HLOGF(mglog.Debug, "STILL KEEPING socket (still have data):
            // %d\n", i->first);
            // if there is still data in the receiver buffer, wait longer
            continue;
         }

         // HLOGF(mglog.Debug, "moving socket to CLOSED: %d\n", i->first);

         //close broken connections and start removal timer
         s->m_Status = SRTS_CLOSED;
         s->m_ClosureTimeStamp = CTimer::getTime();
         tbc.push_back(i->first);
         m_ClosedSockets[i->first] = s;

         // remove from listener's queue
         map<SRTSOCKET, CUDTSocket*>::iterator ls = m_Sockets.find(
            s->m_ListenSocket);
         if (ls == m_Sockets.end())
         {
            ls = m_ClosedSockets.find(s->m_ListenSocket);
            if (ls == m_ClosedSockets.end())
               continue;
         }

         CGuard::enterCS(ls->second->m_AcceptLock);
         ls->second->m_pQueuedSockets->erase(s->m_SocketID);
         ls->second->m_pAcceptSockets->erase(s->m_SocketID);
         CGuard::leaveCS(ls->second->m_AcceptLock);
      }
   }

   for (map<SRTSOCKET, CUDTSocket*>::iterator j = m_ClosedSockets.begin();
      j != m_ClosedSockets.end(); ++ j)
   {
      // HLOGF(mglog.Debug, "checking CLOSED socket: %d\n", j->first);
      if (j->second->m_pUDT->m_ullLingerExpiration > 0)
      {
         // asynchronous close:
         if ((!j->second->m_pUDT->m_pSndBuffer)
            || (0 == j->second->m_pUDT->m_pSndBuffer->getCurrBufSize())
            || (j->second->m_pUDT->m_ullLingerExpiration <= CTimer::getTime()))
         {
            j->second->m_pUDT->m_ullLingerExpiration = 0;
            j->second->m_pUDT->m_bClosing = true;
            j->second->m_ClosureTimeStamp = CTimer::getTime();
         }
      }

      // timeout 1 second to destroy a socket AND it has been removed from
      // RcvUList
      if ((CTimer::getTime() - j->second->m_ClosureTimeStamp > 1000000)
         && ((!j->second->m_pUDT->m_pRNode)
            || !j->second->m_pUDT->m_pRNode->m_bOnList))
      {
         // HLOGF(mglog.Debug, "will unref socket: %d\n", j->first);
         tbr.push_back(j->first);
      }
   }

   // move closed sockets to the ClosedSockets structure
   for (vector<SRTSOCKET>::iterator k = tbc.begin(); k != tbc.end(); ++ k)
      m_Sockets.erase(*k);

   // remove those timeout sockets
   for (vector<SRTSOCKET>::iterator l = tbr.begin(); l != tbr.end(); ++ l)
      removeSocket(*l);
}

void CUDTUnited::removeSocket(const SRTSOCKET u)
{
   map<SRTSOCKET, CUDTSocket*>::iterator i = m_ClosedSockets.find(u);

   // invalid socket ID
   if (i == m_ClosedSockets.end())
      return;

   // decrease multiplexer reference count, and remove it if necessary
   const int mid = i->second->m_iMuxID;

   if (i->second->m_pQueuedSockets)
   {
       CGuard cg(i->second->m_AcceptLock);

      // if it is a listener, close all un-accepted sockets in its queue
      // and remove them later
      for (set<SRTSOCKET>::iterator q = i->second->m_pQueuedSockets->begin();
         q != i->second->m_pQueuedSockets->end(); ++ q)
      {
         m_Sockets[*q]->m_pUDT->m_bBroken = true;
         m_Sockets[*q]->m_pUDT->close();
         m_Sockets[*q]->m_ClosureTimeStamp = CTimer::getTime();
         m_Sockets[*q]->m_Status = SRTS_CLOSED;
         m_ClosedSockets[*q] = m_Sockets[*q];
         m_Sockets.erase(*q);
      }

   }

   // remove from peer rec
   map<int64_t, set<SRTSOCKET> >::iterator j = m_PeerRec.find(
      i->second->getPeerSpec());
   if (j != m_PeerRec.end())
   {
      j->second.erase(u);
      if (j->second.empty())
         m_PeerRec.erase(j);
   }

   /*
   * Socket may be deleted while still having ePoll events set that would
   * remains forever causing epoll_wait to unblock continuously for inexistent
   * sockets. Get rid of all events for this socket.
   */
   m_EPoll.update_events(u, i->second->m_pUDT->m_sPollID,
      UDT_EPOLL_IN|UDT_EPOLL_OUT|UDT_EPOLL_ERR, false);

   // delete this one
   HLOGC(mglog.Debug, log << "GC/removeSocket: closing associated UDT %" << u);
   i->second->m_pUDT->close();
   HLOGC(mglog.Debug, log << "GC/removeSocket: DELETING SOCKET %" << u);
   delete i->second;
   m_ClosedSockets.erase(i);

   if (mid == -1)
       return;

   map<int, CMultiplexer>::iterator m;
   m = m_mMultiplexer.find(mid);
   if (m == m_mMultiplexer.end())
   {
      LOGC(mglog.Fatal, log << "IPE: For socket %" << u << " MUXER id=" << mid << " NOT FOUND!");
      return;
   }

   CMultiplexer& mx = m->second;

   mx.m_iRefCount --;
   // HLOGF(mglog.Debug, "unrefing underlying socket for %u: %u\n",
   //    u, mx.m_iRefCount);
   if (0 == mx.m_iRefCount)
   {
       HLOGC(mglog.Debug, log << "MUXER id=" << mid << " lost last socket %"
           << u << " - deleting muxer bound to port "
           << mx.m_pChannel->bindAddressAny().hport());
      // The channel has no access to the queues and
      // it looks like the multiplexer is the master of all of them.
      // The queues must be silenced before closing the channel
      // because this will cause error to be returned in any operation
      // being currently done in the queues, if any.
      mx.m_pSndQueue->setClosing();
      mx.m_pRcvQueue->setClosing();
      delete mx.m_pSndQueue;
      delete mx.m_pRcvQueue;
      mx.m_pChannel->close();
      delete mx.m_pTimer;
      delete mx.m_pChannel;
      m_mMultiplexer.erase(m);
   }
}

void CUDTUnited::setError(CUDTException* e)
{
    delete (CUDTException*)pthread_getspecific(m_TLSError);
    pthread_setspecific(m_TLSError, e);
}

CUDTException* CUDTUnited::getError()
{
    if(!pthread_getspecific(m_TLSError))
        pthread_setspecific(m_TLSError, new CUDTException);
    return (CUDTException*)pthread_getspecific(m_TLSError);
}


void CUDTUnited::updateMux(
   CUDTSocket* s, const sockaddr* addr, const UDPSOCKET* udpsock)
{
   CGuard cg(m_ControlLock);

   if ((s->m_pUDT->m_bReuseAddr) && (addr))
   {
      int port = (AF_INET == s->m_pUDT->m_iIPversion)
         ? ntohs(((sockaddr_in*)addr)->sin_port)
         : ntohs(((sockaddr_in6*)addr)->sin6_port);

      // find a reusable address
      for (map<int, CMultiplexer>::iterator i = m_mMultiplexer.begin();
         i != m_mMultiplexer.end(); ++ i)
      {
         if ((i->second.m_iIPversion == s->m_pUDT->m_iIPversion)
            && (i->second.m_iMSS == s->m_pUDT->m_iMSS)
#ifdef SRT_ENABLE_IPOPTS
            &&  (i->second.m_iIpTTL == s->m_pUDT->m_iIpTTL)
            && (i->second.m_iIpToS == s->m_pUDT->m_iIpToS)
#endif
            && (i->second.m_iIpV6Only == s->m_pUDT->m_iIpV6Only)
            &&  i->second.m_bReusable)
         {
            if (i->second.m_iPort == port)
            {
               // HLOGF(mglog.Debug, "reusing multiplexer for port
               // %hd\n", port);
               // reuse the existing multiplexer
               ++ i->second.m_iRefCount;
               s->m_pUDT->m_pSndQueue = i->second.m_pSndQueue;
               s->m_pUDT->m_pRcvQueue = i->second.m_pRcvQueue;
               s->m_iMuxID = i->second.m_iID;
               return;
            }
         }
      }
   }

   // a new multiplexer is needed
   CMultiplexer m;
   m.m_iMSS = s->m_pUDT->m_iMSS;
   m.m_iIPversion = s->m_pUDT->m_iIPversion;
#ifdef SRT_ENABLE_IPOPTS
   m.m_iIpTTL = s->m_pUDT->m_iIpTTL;
   m.m_iIpToS = s->m_pUDT->m_iIpToS;
#endif
   m.m_iRefCount = 1;
   m.m_iIpV6Only = s->m_pUDT->m_iIpV6Only;
   m.m_bReusable = s->m_pUDT->m_bReuseAddr;
   m.m_iID = s->m_SocketID;

   m.m_pChannel = new CChannel(s->m_pUDT->m_iIPversion);
#ifdef SRT_ENABLE_IPOPTS
   m.m_pChannel->setIpTTL(s->m_pUDT->m_iIpTTL);
   m.m_pChannel->setIpToS(s->m_pUDT->m_iIpToS);
#endif
   m.m_pChannel->setSndBufSize(s->m_pUDT->m_iUDPSndBufSize);
   m.m_pChannel->setRcvBufSize(s->m_pUDT->m_iUDPRcvBufSize);
   if (s->m_pUDT->m_iIpV6Only != -1)
      m.m_pChannel->setIpV6Only(s->m_pUDT->m_iIpV6Only);

   try
   {
      if (udpsock)
         m.m_pChannel->attach(*udpsock);
      else
         m.m_pChannel->open(addr);
   }
   catch (CUDTException& e)
   {
      m.m_pChannel->close();
      delete m.m_pChannel;
      throw;
   }

   // XXX Simplify this. Use sockaddr_any.
   sockaddr* sa = (AF_INET == s->m_pUDT->m_iIPversion)
      ? (sockaddr*) new sockaddr_in
      : (sockaddr*) new sockaddr_in6;
   m.m_pChannel->getSockAddr(sa);
   m.m_iPort = (AF_INET == s->m_pUDT->m_iIPversion)
      ? ntohs(((sockaddr_in*)sa)->sin_port)
      : ntohs(((sockaddr_in6*)sa)->sin6_port);

   if (AF_INET == s->m_pUDT->m_iIPversion)
      delete (sockaddr_in*)sa;
   else
      delete (sockaddr_in6*)sa;

   m.m_pTimer = new CTimer;

   m.m_pSndQueue = new CSndQueue;
   m.m_pSndQueue->init(m.m_pChannel, m.m_pTimer);
   m.m_pRcvQueue = new CRcvQueue;
   m.m_pRcvQueue->init(
      32, s->m_pUDT->maxPayloadSize(), m.m_iIPversion, 1024,
      m.m_pChannel, m.m_pTimer);

   m_mMultiplexer[m.m_iID] = m;

   s->m_pUDT->m_pSndQueue = m.m_pSndQueue;
   s->m_pUDT->m_pRcvQueue = m.m_pRcvQueue;
   s->m_iMuxID = m.m_iID;

   HLOGF(mglog.Debug, 
      "creating new multiplexer for port %i\n", m.m_iPort);
}

// XXX This functionality needs strong refactoring.
//
// This function is going to find a multiplexer for the port contained
// in the 'ls' listening socket, by searching through the multiplexer
// container.
//
// Somehow, however, it's not even predicted a situation that the multiplexer
// for that port doesn't exist - that is, this function WILL find the
// multiplexer. How can it be so certain? It's because the listener has
// already created the multiplexer during the call to bind(), so if it
// didn't, this function wouldn't even have a chance to be called.
//
// Why can't then the multiplexer be recorded in the 'ls' listening socket data
// to be accessed immediately, especially when one listener can't bind to more
// than one multiplexer at a time (well, even if it could, there's still no
// reason why this should be extracted by "querying")?
//
// Maybe because the multiplexer container is a map, not a list.
// Why is this then a map? Because it's addressed by MuxID. Why do we need
// mux id? Because we don't have a list... ?
// 
// But what's the multiplexer ID? It's a socket ID for which it was originally
// created.
//
// Is this then shared? Yes, only between the listener socket and the accepted
// sockets, or in case of "bound" connecting sockets (by binding you can
// enforce the port number, which can be the same for multiple SRT sockets).
// Not shared in case of unbound connecting socket or rendezvous socket.
//
// Ok, in which situation do we need dispatching by mux id? Only when the
// socket is being deleted. How does the deleting procedure know the muxer id?
// Because it is recorded here at the time when it's found, as... the socket ID
// of the actual listener socket being actually the first socket to create the
// multiplexer, so the multiplexer gets its id.
//
// Still, no reasons found why the socket can't contain a list iterator to a
// multiplexer INSTEAD of m_iMuxID. There's no danger in this solution because
// the multiplexer is never deleted until there's at least one socket using it.
//
// The multiplexer may even physically be contained in the CUDTUnited object,
// just track the multiple users of it (the listener and the accepted sockets).
// When deleting, you simply "unsubscribe" yourself from the multiplexer, which
// will unref it and remove the list element by the iterator kept by the
// socket.
void CUDTUnited::updateListenerMux(CUDTSocket* s, const CUDTSocket* ls)
{
   CGuard cg(m_ControlLock);

   int port = (AF_INET == ls->m_iIPversion)
      ? ntohs(((sockaddr_in*)ls->m_pSelfAddr)->sin_port)
      : ntohs(((sockaddr_in6*)ls->m_pSelfAddr)->sin6_port);

   // find the listener's address
   for (map<int, CMultiplexer>::iterator i = m_mMultiplexer.begin();
      i != m_mMultiplexer.end(); ++ i)
   {
      if (i->second.m_iPort == port)
      {
         HLOGF(mglog.Debug, 
            "updateMux: reusing multiplexer for port %i\n", port);
         // reuse the existing multiplexer
         ++ i->second.m_iRefCount;
         s->m_pUDT->m_pSndQueue = i->second.m_pSndQueue;
         s->m_pUDT->m_pRcvQueue = i->second.m_pRcvQueue;
         s->m_iMuxID = i->second.m_iID;
         return;
      }
   }
}

void* CUDTUnited::garbageCollect(void* p)
{
   CUDTUnited* self = (CUDTUnited*)p;

   THREAD_STATE_INIT("SRT:GC");

   CGuard gcguard(self->m_GCStopLock);

   while (!self->m_bClosing)
   {
       INCREMENT_THREAD_ITERATIONS();
       self->checkBrokenSockets();

       //#ifdef _WIN32
       //      self->checkTLSValue();
       //#endif

       timespec timeout;
#if ENABLE_MONOTONIC_CLOCK
       clock_gettime(CLOCK_MONOTONIC, &timeout);
       timeout.tv_sec++;
       HLOGC(mglog.Debug, log << "GC: sleep until " << FormatTime(uint64_t(timeout.tv_nsec)/1000 + 1000000*(timeout.tv_sec)));
#else
       timeval now;
       gettimeofday(&now, 0);
       timeout.tv_sec = now.tv_sec + 1;
       timeout.tv_nsec = now.tv_usec * 1000;

       HLOGC(mglog.Debug, log << "GC: sleep until " << FormatTime(uint64_t(now.tv_usec) + 1000000*(timeout.tv_sec)));
#endif
       pthread_cond_timedwait(
               &self->m_GCStopCond, &self->m_GCStopLock, &timeout);
   }

   // remove all sockets and multiplexers
   HLOGC(mglog.Debug, log << "GC: GLOBAL EXIT - releasing all pending sockets. Acquring control lock...");
   CGuard::enterCS(self->m_ControlLock);
   for (map<SRTSOCKET, CUDTSocket*>::iterator i = self->m_Sockets.begin();
      i != self->m_Sockets.end(); ++ i)
   {
      i->second->m_pUDT->m_bBroken = true;
      i->second->m_pUDT->close();
      i->second->m_Status = SRTS_CLOSED;
      i->second->m_ClosureTimeStamp = CTimer::getTime();
      self->m_ClosedSockets[i->first] = i->second;

      // remove from listener's queue
      map<SRTSOCKET, CUDTSocket*>::iterator ls = self->m_Sockets.find(
         i->second->m_ListenSocket);
      if (ls == self->m_Sockets.end())
      {
         ls = self->m_ClosedSockets.find(i->second->m_ListenSocket);
         if (ls == self->m_ClosedSockets.end())
            continue;
      }

      CGuard::enterCS(ls->second->m_AcceptLock);
      ls->second->m_pQueuedSockets->erase(i->second->m_SocketID);
      ls->second->m_pAcceptSockets->erase(i->second->m_SocketID);
      CGuard::leaveCS(ls->second->m_AcceptLock);
   }
   self->m_Sockets.clear();

   for (map<SRTSOCKET, CUDTSocket*>::iterator j = self->m_ClosedSockets.begin();
      j != self->m_ClosedSockets.end(); ++ j)
   {
      j->second->m_ClosureTimeStamp = 0;
   }
   CGuard::leaveCS(self->m_ControlLock);

   HLOGC(mglog.Debug, log << "GC: GLOBAL EXIT - releasing all CLOSED sockets.");
   while (true)
   {
      self->checkBrokenSockets();

      CGuard::enterCS(self->m_ControlLock);
      bool empty = self->m_ClosedSockets.empty();
      CGuard::leaveCS(self->m_ControlLock);

      if (empty)
         break;

      CTimer::sleep();
   }

   THREAD_EXIT();
   return NULL;
}

////////////////////////////////////////////////////////////////////////////////

int CUDT::startup()
{
   return s_UDTUnited.startup();
}

int CUDT::cleanup()
{
   return s_UDTUnited.cleanup();
}

SRTSOCKET CUDT::socket(int af, int, int)
{
   if (!s_UDTUnited.m_bGCStatus)
      s_UDTUnited.startup();

   try
   {
      return s_UDTUnited.newSocket(af, 0);
   }
   catch (const CUDTException& e)
   {
      s_UDTUnited.setError(new CUDTException(e));
      return INVALID_SOCK;
   }
   catch (bad_alloc&)
   {
      s_UDTUnited.setError(new CUDTException(MJ_SYSTEMRES, MN_MEMORY, 0));
      return INVALID_SOCK;
   }
   catch (const std::exception& ee)
   {
      LOGC(mglog.Fatal, log << "socket: UNEXPECTED EXCEPTION: "
         << typeid(ee).name()
         << ": " << ee.what());
      s_UDTUnited.setError(new CUDTException(MJ_UNKNOWN, MN_NONE, 0));
      return INVALID_SOCK;
   }
}

int CUDT::bind(SRTSOCKET u, const sockaddr* name, int namelen)
{
   try
   {
      return s_UDTUnited.bind(u, name, namelen);
   }
   catch (const CUDTException& e)
   {
      s_UDTUnited.setError(new CUDTException(e));
      return ERROR;
   }
   catch (bad_alloc&)
   {
      s_UDTUnited.setError(new CUDTException(MJ_SYSTEMRES, MN_MEMORY, 0));
      return ERROR;
   }
   catch (const std::exception& ee)
   {
      LOGC(mglog.Fatal, log << "bind: UNEXPECTED EXCEPTION: "
         << typeid(ee).name()
         << ": " << ee.what());
      s_UDTUnited.setError(new CUDTException(MJ_UNKNOWN, MN_NONE, 0));
      return ERROR;
   }
}

int CUDT::bind(SRTSOCKET u, UDPSOCKET udpsock)
{
   try
   {
      return s_UDTUnited.bind(u, udpsock);
   }
   catch (const CUDTException& e)
   {
      s_UDTUnited.setError(new CUDTException(e));
      return ERROR;
   }
   catch (bad_alloc&)
   {
      s_UDTUnited.setError(new CUDTException(MJ_SYSTEMRES, MN_MEMORY, 0));
      return ERROR;
   }
   catch (const std::exception& ee)
   {
      LOGC(mglog.Fatal, log << "bind/udp: UNEXPECTED EXCEPTION: "
         << typeid(ee).name() << ": " << ee.what());
      s_UDTUnited.setError(new CUDTException(MJ_UNKNOWN, MN_NONE, 0));
      return ERROR;
   }
}

int CUDT::listen(SRTSOCKET u, int backlog)
{
   try
   {
      return s_UDTUnited.listen(u, backlog);
   }
   catch (const CUDTException& e)
   {
      s_UDTUnited.setError(new CUDTException(e));
      return ERROR;
   }
   catch (bad_alloc&)
   {
      s_UDTUnited.setError(new CUDTException(MJ_SYSTEMRES, MN_MEMORY, 0));
      return ERROR;
   }
   catch (const std::exception& ee)
   {
      LOGC(mglog.Fatal, log << "listen: UNEXPECTED EXCEPTION: "
         << typeid(ee).name() << ": " << ee.what());
      s_UDTUnited.setError(new CUDTException(MJ_UNKNOWN, MN_NONE, 0));
      return ERROR;
   }
}

SRTSOCKET CUDT::accept(SRTSOCKET u, sockaddr* addr, int* addrlen)
{
   try
   {
      return s_UDTUnited.accept(u, addr, addrlen);
   }
   catch (const CUDTException& e)
   {
      s_UDTUnited.setError(new CUDTException(e));
      return INVALID_SOCK;
   }
   catch (const std::exception& ee)
   {
      LOGC(mglog.Fatal, log << "accept: UNEXPECTED EXCEPTION: "
         << typeid(ee).name() << ": " << ee.what());
      s_UDTUnited.setError(new CUDTException(MJ_UNKNOWN, MN_NONE, 0));
      return INVALID_SOCK;
   }
}

int CUDT::connect(
   SRTSOCKET u, const sockaddr* name, int namelen, int32_t forced_isn)
{
   try
   {
      return s_UDTUnited.connect(u, name, namelen, forced_isn);
   }
   catch (const CUDTException e)
   {
      s_UDTUnited.setError(new CUDTException(e));
      return ERROR;
   }
   catch (bad_alloc&)
   {
      s_UDTUnited.setError(new CUDTException(MJ_SYSTEMRES, MN_MEMORY, 0));
      return ERROR;
   }
   catch (const std::exception& ee)
   {
      LOGC(mglog.Fatal, log << "connect: UNEXPECTED EXCEPTION: "
         << typeid(ee).name() << ": " << ee.what());
      s_UDTUnited.setError(new CUDTException(MJ_UNKNOWN, MN_NONE, 0));
      return ERROR;
   }
}

int CUDT::close(SRTSOCKET u)
{
   try
   {
      return s_UDTUnited.close(u);
   }
   catch (const CUDTException& e)
   {
      s_UDTUnited.setError(new CUDTException(e));
      return ERROR;
   }
   catch (const std::exception& ee)
   {
      LOGC(mglog.Fatal, log << "close: UNEXPECTED EXCEPTION: "
         << typeid(ee).name() << ": " << ee.what());
      s_UDTUnited.setError(new CUDTException(MJ_UNKNOWN, MN_NONE, 0));
      return ERROR;
   }
}

int CUDT::getpeername(SRTSOCKET u, sockaddr* name, int* namelen)
{
   try
   {
      return s_UDTUnited.getpeername(u, name, namelen);
   }
   catch (const CUDTException& e)
   {
      s_UDTUnited.setError(new CUDTException(e));
      return ERROR;
   }
   catch (const std::exception& ee)
   {
      LOGC(mglog.Fatal, log << "getpeername: UNEXPECTED EXCEPTION: "
         << typeid(ee).name() << ": " << ee.what());
      s_UDTUnited.setError(new CUDTException(MJ_UNKNOWN, MN_NONE, 0));
      return ERROR;
   }
}

int CUDT::getsockname(SRTSOCKET u, sockaddr* name, int* namelen)
{
   try
   {
      return s_UDTUnited.getsockname(u, name, namelen);;
   }
   catch (const CUDTException& e)
   {
      s_UDTUnited.setError(new CUDTException(e));
      return ERROR;
   }
   catch (const std::exception& ee)
   {
      LOGC(mglog.Fatal, log << "getsockname: UNEXPECTED EXCEPTION: "
         << typeid(ee).name() << ": " << ee.what());
      s_UDTUnited.setError(new CUDTException(MJ_UNKNOWN, MN_NONE, 0));
      return ERROR;
   }
}

int CUDT::getsockopt(
   SRTSOCKET u, int, SRT_SOCKOPT optname, void* optval, int* optlen)
{
   try
   {
      CUDT* udt = s_UDTUnited.lookup(u);
      udt->getOpt(optname, optval, *optlen);
      return 0;
   }
   catch (const CUDTException& e)
   {
      s_UDTUnited.setError(new CUDTException(e));
      return ERROR;
   }
   catch (const std::exception& ee)
   {
      LOGC(mglog.Fatal, log << "getsockopt: UNEXPECTED EXCEPTION: "
         << typeid(ee).name() << ": " << ee.what());
      s_UDTUnited.setError(new CUDTException(MJ_UNKNOWN, MN_NONE, 0));
      return ERROR;
   }
}

int CUDT::setsockopt(SRTSOCKET u, int, SRT_SOCKOPT optname, const void* optval, int optlen)
{
   try
   {
      CUDT* udt = s_UDTUnited.lookup(u);
      udt->setOpt(optname, optval, optlen);
      return 0;
   }
   catch (const CUDTException& e)
   {
      s_UDTUnited.setError(new CUDTException(e));
      return ERROR;
   }
   catch (const std::exception& ee)
   {
      LOGC(mglog.Fatal, log << "setsockopt: UNEXPECTED EXCEPTION: "
         << typeid(ee).name() << ": " << ee.what());
      s_UDTUnited.setError(new CUDTException(MJ_UNKNOWN, MN_NONE, 0));
      return ERROR;
   }
}

int CUDT::send(SRTSOCKET u, const char* buf, int len, int)
{
   try
   {
      CUDT* udt = s_UDTUnited.lookup(u);
      return udt->send(buf, len);
   }
   catch (const CUDTException& e)
   {
      s_UDTUnited.setError(new CUDTException(e));
      return ERROR;
   }
   catch (bad_alloc&)
   {
      s_UDTUnited.setError(new CUDTException(MJ_SYSTEMRES, MN_MEMORY, 0));
      return ERROR;
   }
   catch (const std::exception& ee)
   {
      LOGC(mglog.Fatal, log << "send: UNEXPECTED EXCEPTION: "
         << typeid(ee).name() << ": " << ee.what());
      s_UDTUnited.setError(new CUDTException(MJ_UNKNOWN, MN_NONE, 0));
      return ERROR;
   }
}

int CUDT::recv(SRTSOCKET u, char* buf, int len, int)
{
   try
   {
      CUDT* udt = s_UDTUnited.lookup(u);
      return udt->recv(buf, len);
   }
   catch (const CUDTException& e)
   {
      s_UDTUnited.setError(new CUDTException(e));
      return ERROR;
   }
   catch (const std::exception& ee)
   {
      LOGC(mglog.Fatal, log << "recv: UNEXPECTED EXCEPTION: "
         << typeid(ee).name() << ": " << ee.what());
      s_UDTUnited.setError(new CUDTException(MJ_UNKNOWN, MN_NONE, 0));
      return ERROR;
   }
}

int CUDT::sendmsg(
   SRTSOCKET u, const char* buf, int len, int ttl, bool inorder,
   uint64_t srctime)
{
   try
   {
      CUDT* udt = s_UDTUnited.lookup(u);
      return udt->sendmsg(buf, len, ttl, inorder, srctime);
   }
   catch (const CUDTException& e)
   {
      s_UDTUnited.setError(new CUDTException(e));
      return ERROR;
   }
   catch (bad_alloc&)
   {
      s_UDTUnited.setError(new CUDTException(MJ_SYSTEMRES, MN_MEMORY, 0));
      return ERROR;
   }
   catch (const std::exception& ee)
   {
      LOGC(mglog.Fatal, log << "sendmsg: UNEXPECTED EXCEPTION: "
         << typeid(ee).name() << ": " << ee.what());
      s_UDTUnited.setError(new CUDTException(MJ_UNKNOWN, MN_NONE, 0));
      return ERROR;
   }
}

int CUDT::sendmsg2(
   SRTSOCKET u, const char* buf, int len, ref_t<SRT_MSGCTRL> r_m)
{
   try
   {
      CUDT* udt = s_UDTUnited.lookup(u);
      return udt->sendmsg2(buf, len, r_m);
   }
   catch (const CUDTException& e)
   {
      s_UDTUnited.setError(new CUDTException(e));
      return ERROR;
   }
   catch (bad_alloc&)
   {
      s_UDTUnited.setError(new CUDTException(MJ_SYSTEMRES, MN_MEMORY, 0));
      return ERROR;
   }
   catch (const std::exception& ee)
   {
      LOGC(mglog.Fatal, log << "sendmsg: UNEXPECTED EXCEPTION: "
         << typeid(ee).name() << ": " << ee.what());
      s_UDTUnited.setError(new CUDTException(MJ_UNKNOWN, MN_NONE, 0));
      return ERROR;
   }
}

int CUDT::recvmsg(SRTSOCKET u, char* buf, int len, uint64_t& srctime)
{
   try
   {
      CUDT* udt = s_UDTUnited.lookup(u);
      return udt->recvmsg(buf, len, srctime);
   }
   catch (const CUDTException& e)
   {
      s_UDTUnited.setError(new CUDTException(e));
      return ERROR;
   }
   catch (const std::exception& ee)
   {
      LOGC(mglog.Fatal, log << "recvmsg: UNEXPECTED EXCEPTION: "
         << typeid(ee).name() << ": " << ee.what());
      s_UDTUnited.setError(new CUDTException(MJ_UNKNOWN, MN_NONE, 0));
      return ERROR;
   }
}

int CUDT::recvmsg2(SRTSOCKET u, char* buf, int len, ref_t<SRT_MSGCTRL> r_m)
{
   try
   {
      CUDT* udt = s_UDTUnited.lookup(u);
      return udt->recvmsg2(buf, len, r_m);
   }
   catch (const CUDTException& e)
   {
      s_UDTUnited.setError(new CUDTException(e));
      return ERROR;
   }
   catch (const std::exception& ee)
   {
      LOGC(mglog.Fatal, log << "recvmsg: UNEXPECTED EXCEPTION: "
         << typeid(ee).name() << ": " << ee.what());
      s_UDTUnited.setError(new CUDTException(MJ_UNKNOWN, MN_NONE, 0));
      return ERROR;
   }
}
int64_t CUDT::sendfile(
   SRTSOCKET u, fstream& ifs, int64_t& offset, int64_t size, int block)
{
   try
   {
      CUDT* udt = s_UDTUnited.lookup(u);
      return udt->sendfile(ifs, offset, size, block);
   }
   catch (const CUDTException& e)
   {
      s_UDTUnited.setError(new CUDTException(e));
      return ERROR;
   }
   catch (bad_alloc&)
   {
      s_UDTUnited.setError(new CUDTException(MJ_SYSTEMRES, MN_MEMORY, 0));
      return ERROR;
   }
   catch (const std::exception& ee)
   {
      LOGC(mglog.Fatal, log << "sendfile: UNEXPECTED EXCEPTION: "
         << typeid(ee).name() << ": " << ee.what());
      s_UDTUnited.setError(new CUDTException(MJ_UNKNOWN, MN_NONE, 0));
      return ERROR;
   }
}

int64_t CUDT::recvfile(
   SRTSOCKET u, fstream& ofs, int64_t& offset, int64_t size, int block)
{
   try
   {
      CUDT* udt = s_UDTUnited.lookup(u);
      return udt->recvfile(ofs, offset, size, block);
   }
   catch (const CUDTException& e)
   {
      s_UDTUnited.setError(new CUDTException(e));
      return ERROR;
   }
   catch (const std::exception& ee)
   {
      LOGC(mglog.Fatal, log << "recvfile: UNEXPECTED EXCEPTION: "
         << typeid(ee).name() << ": " << ee.what());
      s_UDTUnited.setError(new CUDTException(MJ_UNKNOWN, MN_NONE, 0));
      return ERROR;
   }
}

int CUDT::select(
   int,
   ud_set* readfds,
   ud_set* writefds,
   ud_set* exceptfds,
   const timeval* timeout)
{
   if ((!readfds) && (!writefds) && (!exceptfds))
   {
      s_UDTUnited.setError(new CUDTException(MJ_NOTSUP, MN_INVAL, 0));
      return ERROR;
   }

   try
   {
      return s_UDTUnited.select(readfds, writefds, exceptfds, timeout);
   }
   catch (const CUDTException& e)
   {
      s_UDTUnited.setError(new CUDTException(e));
      return ERROR;
   }
   catch (bad_alloc&)
   {
      s_UDTUnited.setError(new CUDTException(MJ_SYSTEMRES, MN_MEMORY, 0));
      return ERROR;
   }
   catch (const std::exception& ee)
   {
      LOGC(mglog.Fatal, log << "select: UNEXPECTED EXCEPTION: "
         << typeid(ee).name() << ": " << ee.what());
      s_UDTUnited.setError(new CUDTException(MJ_UNKNOWN, MN_NONE, 0));
      return ERROR;
   }
}

int CUDT::selectEx(
   const vector<SRTSOCKET>& fds,
   vector<SRTSOCKET>* readfds,
   vector<SRTSOCKET>* writefds,
   vector<SRTSOCKET>* exceptfds,
   int64_t msTimeOut)
{
   if ((!readfds) && (!writefds) && (!exceptfds))
   {
      s_UDTUnited.setError(new CUDTException(MJ_NOTSUP, MN_INVAL, 0));
      return ERROR;
   }

   try
   {
      return s_UDTUnited.selectEx(fds, readfds, writefds, exceptfds, msTimeOut);
   }
   catch (const CUDTException& e)
   {
      s_UDTUnited.setError(new CUDTException(e));
      return ERROR;
   }
   catch (bad_alloc&)
   {
      s_UDTUnited.setError(new CUDTException(MJ_SYSTEMRES, MN_MEMORY, 0));
      return ERROR;
   }
   catch (const std::exception& ee)
   {
      LOGC(mglog.Fatal, log << "selectEx: UNEXPECTED EXCEPTION: "
         << typeid(ee).name() << ": " << ee.what());
      s_UDTUnited.setError(new CUDTException(MJ_UNKNOWN));
      return ERROR;
   }
}

int CUDT::epoll_create()
{
   try
   {
      return s_UDTUnited.epoll_create();
   }
   catch (const CUDTException& e)
   {
      s_UDTUnited.setError(new CUDTException(e));
      return ERROR;
   }
   catch (const std::exception& ee)
   {
      LOGC(mglog.Fatal, log << "epoll_create: UNEXPECTED EXCEPTION: "
         << typeid(ee).name() << ": " << ee.what());
      s_UDTUnited.setError(new CUDTException(MJ_UNKNOWN, MN_NONE, 0));
      return ERROR;
   }
}

int CUDT::epoll_add_usock(const int eid, const SRTSOCKET u, const int* events)
{
   try
   {
      return s_UDTUnited.epoll_add_usock(eid, u, events);
   }
   catch (const CUDTException& e)
   {
      s_UDTUnited.setError(new CUDTException(e));
      return ERROR;
   }
   catch (const std::exception& ee)
   {
      LOGC(mglog.Fatal, log << "epoll_add_usock: UNEXPECTED EXCEPTION: "
         << typeid(ee).name() << ": " << ee.what());
      s_UDTUnited.setError(new CUDTException(MJ_UNKNOWN, MN_NONE, 0));
      return ERROR;
   }
}

int CUDT::epoll_add_ssock(const int eid, const SYSSOCKET s, const int* events)
{
   try
   {
      return s_UDTUnited.epoll_add_ssock(eid, s, events);
   }
   catch (const CUDTException& e)
   {
      s_UDTUnited.setError(new CUDTException(e));
      return ERROR;
   }
   catch (const std::exception& ee)
   {
      LOGC(mglog.Fatal, log << "epoll_add_ssock: UNEXPECTED EXCEPTION: "
         << typeid(ee).name() << ": " << ee.what());
      s_UDTUnited.setError(new CUDTException(MJ_UNKNOWN, MN_NONE, 0));
      return ERROR;
   }
}

int CUDT::epoll_update_usock(
   const int eid, const SRTSOCKET u, const int* events)
{
   try
   {
      return s_UDTUnited.epoll_update_usock(eid, u, events);
   }
   catch (const CUDTException& e)
   {
      s_UDTUnited.setError(new CUDTException(e));
      return ERROR;
   }
   catch (const std::exception& ee)
   {
      LOGC(mglog.Fatal, log << "epoll_update_usock: UNEXPECTED EXCEPTION: "
         << typeid(ee).name() << ": " << ee.what());
      s_UDTUnited.setError(new CUDTException(MJ_UNKNOWN, MN_NONE, 0));
      return ERROR;
   }
}

int CUDT::epoll_update_ssock(
   const int eid, const SYSSOCKET s, const int* events)
{
   try
   {
      return s_UDTUnited.epoll_update_ssock(eid, s, events);
   }
   catch (const CUDTException& e)
   {
      s_UDTUnited.setError(new CUDTException(e));
      return ERROR;
   }
   catch (const std::exception& ee)
   {
      LOGC(mglog.Fatal, log << "epoll_update_ssock: UNEXPECTED EXCEPTION: "
         << typeid(ee).name() << ": " << ee.what());
      s_UDTUnited.setError(new CUDTException(MJ_UNKNOWN, MN_NONE, 0));
      return ERROR;
   }
}


int CUDT::epoll_remove_usock(const int eid, const SRTSOCKET u)
{
   try
   {
      return s_UDTUnited.epoll_remove_usock(eid, u);
   }
   catch (const CUDTException& e)
   {
      s_UDTUnited.setError(new CUDTException(e));
      return ERROR;
   }
   catch (const std::exception& ee)
   {
      LOGC(mglog.Fatal, log << "epoll_remove_usock: UNEXPECTED EXCEPTION: "
         << typeid(ee).name() << ": " << ee.what());
      s_UDTUnited.setError(new CUDTException(MJ_UNKNOWN, MN_NONE, 0));
      return ERROR;
   }
}

int CUDT::epoll_remove_ssock(const int eid, const SYSSOCKET s)
{
   try
   {
      return s_UDTUnited.epoll_remove_ssock(eid, s);
   }
   catch (const CUDTException& e)
   {
      s_UDTUnited.setError(new CUDTException(e));
      return ERROR;
   }
   catch (const std::exception& ee)
   {
      LOGC(mglog.Fatal, log << "epoll_remove_ssock: UNEXPECTED EXCEPTION: "
         << typeid(ee).name() << ": " << ee.what());
      s_UDTUnited.setError(new CUDTException(MJ_UNKNOWN, MN_NONE, 0));
      return ERROR;
   }
}

int CUDT::epoll_wait(
   const int eid,
   set<SRTSOCKET>* readfds,
   set<SRTSOCKET>* writefds,
   int64_t msTimeOut,
   set<SYSSOCKET>* lrfds,
   set<SYSSOCKET>* lwfds)
{
   try
   {
      return s_UDTUnited.epoll_wait(
         eid, readfds, writefds, msTimeOut, lrfds, lwfds);
   }
   catch (const CUDTException& e)
   {
      s_UDTUnited.setError(new CUDTException(e));
      return ERROR;
   }
   catch (const std::exception& ee)
   {
      LOGC(mglog.Fatal, log << "epoll_wait: UNEXPECTED EXCEPTION: "
         << typeid(ee).name() << ": " << ee.what());
      s_UDTUnited.setError(new CUDTException(MJ_UNKNOWN, MN_NONE, 0));
      return ERROR;
   }
}

int CUDT::epoll_uwait(
   const int eid,
   SRT_EPOLL_EVENT* fdsSet,
   int fdsSize,
   int64_t msTimeOut)
{
   try
   {
      return s_UDTUnited.epoll_uwait(eid, fdsSet, fdsSize, msTimeOut);
   }
   catch (const CUDTException& e)
   {
      s_UDTUnited.setError(new CUDTException(e));
      return ERROR;
   }
   catch (const std::exception& ee)
   {
      LOGC(mglog.Fatal, log << "epoll_uwait: UNEXPECTED EXCEPTION: "
         << typeid(ee).name() << ": " << ee.what());
      s_UDTUnited.setError(new CUDTException(MJ_UNKNOWN, MN_NONE, 0));
      return ERROR;
   }
}

int32_t CUDT::epoll_set(
   const int eid,
   int32_t flags)
{
   try
   {
      return s_UDTUnited.epoll_set(eid, flags);
   }
   catch (const CUDTException& e)
   {
      s_UDTUnited.setError(new CUDTException(e));
      return ERROR;
   }
   catch (const std::exception& ee)
   {
      LOGC(mglog.Fatal, log << "epoll_set: UNEXPECTED EXCEPTION: "
         << typeid(ee).name() << ": " << ee.what());
      s_UDTUnited.setError(new CUDTException(MJ_UNKNOWN, MN_NONE, 0));
      return ERROR;
   }
}

int CUDT::epoll_release(const int eid)
{
   try
   {
      return s_UDTUnited.epoll_release(eid);
   }
   catch (const CUDTException& e)
   {
      s_UDTUnited.setError(new CUDTException(e));
      return ERROR;
   }
   catch (const std::exception& ee)
   {
      LOGC(mglog.Fatal, log << "epoll_release: UNEXPECTED EXCEPTION: "
         << typeid(ee).name() << ": " << ee.what());
      s_UDTUnited.setError(new CUDTException(MJ_UNKNOWN, MN_NONE, 0));
      return ERROR;
   }
}

CUDTException& CUDT::getlasterror()
{
   return *s_UDTUnited.getError();
}

int CUDT::bstats(SRTSOCKET u, CBytePerfMon* perf, bool clear, bool instantaneous)
{
   try
   {
      CUDT* udt = s_UDTUnited.lookup(u);
      udt->bstats(perf, clear, instantaneous);
      return 0;
   }
   catch (const CUDTException& e)
   {
      s_UDTUnited.setError(new CUDTException(e));
      return ERROR;
   }
   catch (const std::exception& ee)
   {
      LOGC(mglog.Fatal, log << "bstats: UNEXPECTED EXCEPTION: "
         << typeid(ee).name() << ": " << ee.what());
      s_UDTUnited.setError(new CUDTException(MJ_UNKNOWN, MN_NONE, 0));
      return ERROR;
   }
}

CUDT* CUDT::getUDTHandle(SRTSOCKET u)
{
   try
   {
      return s_UDTUnited.lookup(u);
   }
   catch (const CUDTException& e)
   {
      s_UDTUnited.setError(new CUDTException(e));
      return NULL;
   }
   catch (const std::exception& ee)
   {
      LOGC(mglog.Fatal, log << "getUDTHandle: UNEXPECTED EXCEPTION: "
         << typeid(ee).name() << ": " << ee.what());
      s_UDTUnited.setError(new CUDTException(MJ_UNKNOWN, MN_NONE, 0));
      return NULL;
   }
}

vector<SRTSOCKET> CUDT::existingSockets()
{
    vector<SRTSOCKET> out;
    for (std::map<SRTSOCKET,CUDTSocket*>::iterator i
         = s_UDTUnited.m_Sockets.begin();
      i != s_UDTUnited.m_Sockets.end(); ++i)
    {
        out.push_back(i->first);
    }
    return out;
}

SRT_SOCKSTATUS CUDT::getsockstate(SRTSOCKET u)
{
   try
   {
      return s_UDTUnited.getStatus(u);
   }
   catch (const CUDTException &e)
   {
      s_UDTUnited.setError(new CUDTException(e));
      return SRTS_NONEXIST;
   }
   catch (const std::exception& ee)
   {
      LOGC(mglog.Fatal, log << "getsockstate: UNEXPECTED EXCEPTION: "
         << typeid(ee).name() << ": " << ee.what());
      s_UDTUnited.setError(new CUDTException(MJ_UNKNOWN, MN_NONE, 0));
      return SRTS_NONEXIST;
   }
}


////////////////////////////////////////////////////////////////////////////////

namespace UDT
{

int startup()
{
   return CUDT::startup();
}

int cleanup()
{
   return CUDT::cleanup();
}

SRTSOCKET socket(int af, int type, int protocol)
{
   return CUDT::socket(af, type, protocol);
}

int bind(SRTSOCKET u, const struct sockaddr* name, int namelen)
{
   return CUDT::bind(u, name, namelen);
}

int bind2(SRTSOCKET u, UDPSOCKET udpsock)
{
   return CUDT::bind(u, udpsock);
}

int listen(SRTSOCKET u, int backlog)
{
   return CUDT::listen(u, backlog);
}

SRTSOCKET accept(SRTSOCKET u, struct sockaddr* addr, int* addrlen)
{
   return CUDT::accept(u, addr, addrlen);
}

int connect(SRTSOCKET u, const struct sockaddr* name, int namelen)
{
   return CUDT::connect(u, name, namelen, 0);
}

int close(SRTSOCKET u)
{
   return CUDT::close(u);
}

int getpeername(SRTSOCKET u, struct sockaddr* name, int* namelen)
{
   return CUDT::getpeername(u, name, namelen);
}

int getsockname(SRTSOCKET u, struct sockaddr* name, int* namelen)
{
   return CUDT::getsockname(u, name, namelen);
}

int getsockopt(
   SRTSOCKET u, int level, SRT_SOCKOPT optname, void* optval, int* optlen)
{
   return CUDT::getsockopt(u, level, optname, optval, optlen);
}

int setsockopt(
   SRTSOCKET u, int level, SRT_SOCKOPT optname, const void* optval, int optlen)
{
   return CUDT::setsockopt(u, level, optname, optval, optlen);
}

// DEVELOPER API

int connect_debug(
   SRTSOCKET u, const struct sockaddr* name, int namelen, int32_t forced_isn)
{
   return CUDT::connect(u, name, namelen, forced_isn);
}

int send(SRTSOCKET u, const char* buf, int len, int flags)
{
   return CUDT::send(u, buf, len, flags);
}

int recv(SRTSOCKET u, char* buf, int len, int flags)
{
   return CUDT::recv(u, buf, len, flags);
}


int sendmsg(
   SRTSOCKET u, const char* buf, int len, int ttl, bool inorder,
   uint64_t srctime)
{
   return CUDT::sendmsg(u, buf, len, ttl, inorder, srctime);
}

int recvmsg(SRTSOCKET u, char* buf, int len, uint64_t& srctime)
{
   return CUDT::recvmsg(u, buf, len, srctime);
}

int recvmsg(SRTSOCKET u, char* buf, int len)
{
   uint64_t srctime;

   return CUDT::recvmsg(u, buf, len, srctime);
}

int64_t sendfile(
   SRTSOCKET u,
   fstream& ifs,
   int64_t& offset,
   int64_t size,
   int block)
{
   return CUDT::sendfile(u, ifs, offset, size, block);
}

int64_t recvfile(
   SRTSOCKET u,
   fstream& ofs,
   int64_t& offset,
   int64_t size,
   int block)
{
   return CUDT::recvfile(u, ofs, offset, size, block);
}

int64_t sendfile2(
   SRTSOCKET u,
   const char* path,
   int64_t* offset,
   int64_t size,
   int block)
{
   fstream ifs(path, ios::binary | ios::in);
   int64_t ret = CUDT::sendfile(u, ifs, *offset, size, block);
   ifs.close();
   return ret;
}

int64_t recvfile2(
   SRTSOCKET u,
   const char* path,
   int64_t* offset,
   int64_t size,
   int block)
{
   fstream ofs(path, ios::binary | ios::out);
   int64_t ret = CUDT::recvfile(u, ofs, *offset, size, block);
   ofs.close();
   return ret;
}

int select(
   int nfds,
   UDSET* readfds,
   UDSET* writefds,
   UDSET* exceptfds,
   const struct timeval* timeout)
{
   return CUDT::select(nfds, readfds, writefds, exceptfds, timeout);
}

int selectEx(
   const vector<SRTSOCKET>& fds,
   vector<SRTSOCKET>* readfds,
   vector<SRTSOCKET>* writefds,
   vector<SRTSOCKET>* exceptfds,
   int64_t msTimeOut)
{
   return CUDT::selectEx(fds, readfds, writefds, exceptfds, msTimeOut);
}

int epoll_create()
{
   return CUDT::epoll_create();
}

int epoll_add_usock(int eid, SRTSOCKET u, const int* events)
{
   return CUDT::epoll_add_usock(eid, u, events);
}

int epoll_add_ssock(int eid, SYSSOCKET s, const int* events)
{
   return CUDT::epoll_add_ssock(eid, s, events);
}

int epoll_update_usock(int eid, SRTSOCKET u, const int* events)
{
   return CUDT::epoll_update_usock(eid, u, events);
}

int epoll_update_ssock(int eid, SYSSOCKET s, const int* events)
{
   return CUDT::epoll_update_ssock(eid, s, events);
}

int epoll_remove_usock(int eid, SRTSOCKET u)
{
   return CUDT::epoll_remove_usock(eid, u);
}

int epoll_remove_ssock(int eid, SYSSOCKET s)
{
   return CUDT::epoll_remove_ssock(eid, s);
}

int epoll_wait(
   int eid,
   set<SRTSOCKET>* readfds,
   set<SRTSOCKET>* writefds,
   int64_t msTimeOut,
   set<SYSSOCKET>* lrfds,
   set<SYSSOCKET>* lwfds)
{
   return CUDT::epoll_wait(eid, readfds, writefds, msTimeOut, lrfds, lwfds);
}

/*

#define SET_RESULT(val, num, fds, it) \
   if (val != NULL) \
   { \
      if (val->empty()) \
      { \
         if (num) *num = 0; \
      } \
      else \
      { \
         if (*num > static_cast<int>(val->size())) \
            *num = val->size(); \
         int count = 0; \
         for (it = val->begin(); it != val->end(); ++ it) \
         { \
            if (count >= *num) \
               break; \
            fds[count ++] = *it; \
         } \
      } \
   }

*/

template <class SOCKTYPE>
inline void set_result(set<SOCKTYPE>* val, int* num, SOCKTYPE* fds)
{
    if ( !val || !num || !fds )
        return;

    if (*num > int(val->size()))
        *num = int(val->size()); // will get 0 if val->empty()
    int count = 0;

    // This loop will run 0 times if val->empty()
    for (typename set<SOCKTYPE>::const_iterator it = val->begin(); it != val->end(); ++ it)
    {
        if (count >= *num)
            break;
        fds[count ++] = *it;
    }
}

int epoll_wait2(
   int eid, SRTSOCKET* readfds,
   int* rnum, SRTSOCKET* writefds,
   int* wnum,
   int64_t msTimeOut,
   SYSSOCKET* lrfds,
   int* lrnum,
   SYSSOCKET* lwfds,
   int* lwnum)
{
   // This API is an alternative format for epoll_wait, created for
   // compatability with other languages. Users need to pass in an array
   // for holding the returned sockets, with the maximum array length
   // stored in *rnum, etc., which will be updated with returned number
   // of sockets.

   set<SRTSOCKET> readset;
   set<SRTSOCKET> writeset;
   set<SYSSOCKET> lrset;
   set<SYSSOCKET> lwset;
   set<SRTSOCKET>* rval = NULL;
   set<SRTSOCKET>* wval = NULL;
   set<SYSSOCKET>* lrval = NULL;
   set<SYSSOCKET>* lwval = NULL;
   if ((readfds != NULL) && (rnum != NULL))
      rval = &readset;
   if ((writefds != NULL) && (wnum != NULL))
      wval = &writeset;
   if ((lrfds != NULL) && (lrnum != NULL))
      lrval = &lrset;
   if ((lwfds != NULL) && (lwnum != NULL))
      lwval = &lwset;

   int ret = CUDT::epoll_wait(eid, rval, wval, msTimeOut, lrval, lwval);
   if (ret > 0)
   {
      //set<SRTSOCKET>::const_iterator i;
      //SET_RESULT(rval, rnum, readfds, i);
      set_result(rval, rnum, readfds);
      //SET_RESULT(wval, wnum, writefds, i);
      set_result(wval, wnum, writefds);

      //set<SYSSOCKET>::const_iterator j;
      //SET_RESULT(lrval, lrnum, lrfds, j);
      set_result(lrval, lrnum, lrfds);
      //SET_RESULT(lwval, lwnum, lwfds, j);
      set_result(lwval, lwnum, lwfds);
   }
   return ret;
}

int epoll_uwait(int eid, SRT_EPOLL_EVENT* fdsSet, int fdsSize, int64_t msTimeOut)
{
   return CUDT::epoll_uwait(eid, fdsSet, fdsSize, msTimeOut);
}

int epoll_release(int eid)
{
   return CUDT::epoll_release(eid);
}

ERRORINFO& getlasterror()
{
   return CUDT::getlasterror();
}

int getlasterror_code()
{
   return CUDT::getlasterror().getErrorCode();
}

const char* getlasterror_desc()
{
   return CUDT::getlasterror().getErrorMessage();
}

int getlasterror_errno()
{
   return CUDT::getlasterror().getErrno();
}

// Get error string of a given error code
const char* geterror_desc(int code, int err)
{
   CUDTException e (CodeMajor(code/1000), CodeMinor(code%1000), err);
   return(e.getErrorMessage());
}

int bstats(SRTSOCKET u, TRACEBSTATS* perf, bool clear)
{
   return CUDT::bstats(u, perf, clear);
}

SRT_SOCKSTATUS getsockstate(SRTSOCKET u)
{
   return CUDT::getsockstate(u);
}

void setloglevel(LogLevel::type ll)
{
    CGuard gg(srt_logger_config.mutex);
    srt_logger_config.max_level = ll;
}

void addlogfa(LogFA fa)
{
    CGuard gg(srt_logger_config.mutex);
    srt_logger_config.enabled_fa.set(fa, true);
}

void dellogfa(LogFA fa)
{
    CGuard gg(srt_logger_config.mutex);
    srt_logger_config.enabled_fa.set(fa, false);
}

void resetlogfa(set<LogFA> fas)
{
    CGuard gg(srt_logger_config.mutex);
    for (int i = 0; i <= SRT_LOGFA_LASTNONE; ++i)
        srt_logger_config.enabled_fa.set(i, fas.count(i));
}

void resetlogfa(const int* fara, size_t fara_size)
{
    CGuard gg(srt_logger_config.mutex);
    srt_logger_config.enabled_fa.reset();
    for (const int* i = fara; i != fara + fara_size; ++i)
        srt_logger_config.enabled_fa.set(*i, true);
}

void setlogstream(std::ostream& stream)
{
    CGuard gg(srt_logger_config.mutex);
    srt_logger_config.log_stream = &stream;
}

void setloghandler(void* opaque, SRT_LOG_HANDLER_FN* handler)
{
    CGuard gg(srt_logger_config.mutex);
    srt_logger_config.loghandler_opaque = opaque;
    srt_logger_config.loghandler_fn = handler;
}

void setlogflags(int flags)
{
    CGuard gg(srt_logger_config.mutex);
    srt_logger_config.flags = flags;
}

SRT_API bool setstreamid(SRTSOCKET u, const std::string& sid)
{
    return CUDT::setstreamid(u, sid);
}
SRT_API std::string getstreamid(SRTSOCKET u)
{
    return CUDT::getstreamid(u);
}

SRT_REJECT_REASON getrejectreason(SRTSOCKET u)
{
    return CUDT::rejectReason(u);
}

}  // namespace UDT
