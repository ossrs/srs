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
   Yunhong Gu, last updated 01/18/2011
modified by
   Haivision Systems Inc.
*****************************************************************************/

/* WARNING!!!
 * Since now this file is a "C and C++ header".
 * It should be then able to be interpreted by C compiler, so
 * all C++-oriented things must be ifdef'd-out by __cplusplus.
 *
 * Mind also comments - to prevent any portability problems,
 * B/C++ comments (// -> EOL) should not be used unless the
 * area is under __cplusplus condition already.
 *
 * NOTE: this file contains _STRUCTURES_ that are common to C and C++,
 * plus some functions and other functionalities ONLY FOR C++. This
 * file doesn't contain _FUNCTIONS_ predicted to be used in C - see udtc.h
 */

#ifndef __UDT_H__
#define __UDT_H__

#include "srt.h"

/*
* SRT_ENABLE_THREADCHECK (THIS IS SET IN MAKEFILE NOT HERE)
*/
#if defined(SRT_ENABLE_THREADCHECK)
#include <threadcheck.h>
#else
#define THREAD_STATE_INIT(name)
#define THREAD_EXIT()
#define THREAD_PAUSED()
#define THREAD_RESUMED()
#define INCREMENT_THREAD_ITERATIONS()
#endif

/* Obsolete way to define MINGW */
#ifndef __MINGW__
#if defined(__MINGW32__) || defined(__MINGW64__)
#define __MINGW__ 1
#endif
#endif

#ifdef __cplusplus
#include <fstream>
#include <set>
#include <string>
#include <vector>
#endif


// Legacy/backward/deprecated
#define UDT_API SRT_API

////////////////////////////////////////////////////////////////////////////////

//if compiling on VC6.0 or pre-WindowsXP systems
//use -DLEGACY_WIN32

//if compiling with MinGW, it only works on XP or above
//use -D_WIN32_WINNT=0x0501


////////////////////////////////////////////////////////////////////////////////

#ifdef __cplusplus
// This facility is used only for select() function.
// This is considered obsolete and the epoll() functionality rather should be used.
typedef std::set<SRTSOCKET> ud_set;
#define UD_CLR(u, uset) ((uset)->erase(u))
#define UD_ISSET(u, uset) ((uset)->find(u) != (uset)->end())
#define UD_SET(u, uset) ((uset)->insert(u))
#define UD_ZERO(uset) ((uset)->clear())
#endif

////////////////////////////////////////////////////////////////////////////////

// Legacy names

#define UDT_MSS SRTO_MSS
#define UDT_SNDSYN SRTO_SNDSYN
#define UDT_RCVSYN SRTO_RCVSYN
#define UDT_FC SRTO_FC
#define UDT_SNDBUF SRTO_SNDBUF
#define UDT_RCVBUF SRTO_RCVBUF
#define UDT_LINGER SRTO_LINGER
#define UDP_SNDBUF SRTO_UDP_SNDBUF
#define UDP_RCVBUF SRTO_UDP_RCVBUF
#define UDT_MAXMSG SRTO_MAXMSG
#define UDT_MSGTTL SRTO_MSGTTL
#define UDT_RENDEZVOUS SRTO_RENDEZVOUS
#define UDT_SNDTIMEO SRTO_SNDTIMEO
#define UDT_RCVTIMEO SRTO_RCVTIMEO
#define UDT_REUSEADDR SRTO_REUSEADDR
#define UDT_MAXBW SRTO_MAXBW
#define UDT_STATE SRTO_STATE
#define UDT_EVENT SRTO_EVENT
#define UDT_SNDDATA SRTO_SNDDATA
#define UDT_RCVDATA SRTO_RCVDATA
#define SRT_SENDER SRTO_SENDER
#define SRT_TSBPDMODE SRTO_TSBPDMODE
#define SRT_TSBPDDELAY SRTO_TSBPDDELAY
#define SRT_INPUTBW SRTO_INPUTBW
#define SRT_OHEADBW SRTO_OHEADBW
#define SRT_PASSPHRASE SRTO_PASSPHRASE
#define SRT_PBKEYLEN SRTO_PBKEYLEN
#define SRT_KMSTATE SRTO_KMSTATE
#define SRT_IPTTL SRTO_IPTTL
#define SRT_IPTOS SRTO_IPTOS
#define SRT_TLPKTDROP SRTO_TLPKTDROP
#define SRT_TSBPDMAXLAG SRTO_TSBPDMAXLAG
#define SRT_RCVNAKREPORT SRTO_NAKREPORT
#define SRT_CONNTIMEO SRTO_CONNTIMEO
#define SRT_SNDPBKEYLEN SRTO_SNDPBKEYLEN
#define SRT_RCVPBKEYLEN SRTO_RCVPBKEYLEN
#define SRT_SNDPEERKMSTATE SRTO_SNDPEERKMSTATE
#define SRT_RCVKMSTATE SRTO_RCVKMSTATE

#define UDT_EPOLL_OPT SRT_EPOLL_OPT
#define UDT_EPOLL_IN SRT_EPOLL_IN
#define UDT_EPOLL_OUT SRT_EPOLL_OUT
#define UDT_EPOLL_ERR SRT_EPOLL_ERR

/* Binary backward compatibility obsolete options */
#define SRT_NAKREPORT   SRT_RCVNAKREPORT

#if !defined(SRT_DISABLE_LEGACY_UDTSTATUS)
#define UDTSTATUS    SRT_SOCKSTATUS
#define INIT         SRTS_INIT
#define OPENED       SRTS_OPENED
#define LISTENING    SRTS_LISTENING
#define CONNECTING   SRTS_CONNECTING
#define CONNECTED    SRTS_CONNECTED
#define BROKEN       SRTS_BROKEN
#define CLOSING      SRTS_CLOSING
#define CLOSED       SRTS_CLOSED
#define NONEXIST     SRTS_NONEXIST
#endif

////////////////////////////////////////////////////////////////////////////////

struct CPerfMon
{
   // global measurements
   int64_t msTimeStamp;                 // time since the UDT entity is started, in milliseconds
   int64_t pktSentTotal;                // total number of sent data packets, including retransmissions
   int64_t pktRecvTotal;                // total number of received packets
   int pktSndLossTotal;                 // total number of lost packets (sender side)
   int pktRcvLossTotal;                 // total number of lost packets (receiver side)
   int pktRetransTotal;                 // total number of retransmitted packets
   int pktRcvRetransTotal;              // total number of retransmitted packets received
   int pktSentACKTotal;                 // total number of sent ACK packets
   int pktRecvACKTotal;                 // total number of received ACK packets
   int pktSentNAKTotal;                 // total number of sent NAK packets
   int pktRecvNAKTotal;                 // total number of received NAK packets
   int64_t usSndDurationTotal;		// total time duration when UDT is sending data (idle time exclusive)

   // local measurements
   int64_t pktSent;                     // number of sent data packets, including retransmissions
   int64_t pktRecv;                     // number of received packets
   int pktSndLoss;                      // number of lost packets (sender side)
   int pktRcvLoss;                      // number of lost packets (receiver side)
   int pktRetrans;                      // number of retransmitted packets
   int pktRcvRetrans;                   // number of retransmitted packets received
   int pktSentACK;                      // number of sent ACK packets
   int pktRecvACK;                      // number of received ACK packets
   int pktSentNAK;                      // number of sent NAK packets
   int pktRecvNAK;                      // number of received NAK packets
   double mbpsSendRate;                 // sending rate in Mb/s
   double mbpsRecvRate;                 // receiving rate in Mb/s
   int64_t usSndDuration;		// busy sending time (i.e., idle time exclusive)
   int pktReorderDistance;              // size of order discrepancy in received sequences
   double pktRcvAvgBelatedTime;             // average time of packet delay for belated packets (packets with sequence past the ACK)
   int64_t pktRcvBelated;              // number of received AND IGNORED packets due to having come too late

   // instant measurements
   double usPktSndPeriod;               // packet sending period, in microseconds
   int pktFlowWindow;                   // flow window size, in number of packets
   int pktCongestionWindow;             // congestion window size, in number of packets
   int pktFlightSize;                   // number of packets on flight
   double msRTT;                        // RTT, in milliseconds
   double mbpsBandwidth;                // estimated bandwidth, in Mb/s
   int byteAvailSndBuf;                 // available UDT sender buffer size
   int byteAvailRcvBuf;                 // available UDT receiver buffer size
};

typedef SRTSOCKET UDTSOCKET; //legacy alias

#ifdef __cplusplus

// Class CUDTException exposed for C++ API.
// This is actually useless, unless you'd use a DIRECT C++ API,
// however there's no such API so far. The current C++ API for UDT/SRT
// is predicted to NEVER LET ANY EXCEPTION out of implementation,
// so it's useless to catch this exception anyway.

class UDT_API CUDTException
{
public:

   CUDTException(CodeMajor major = MJ_SUCCESS, CodeMinor minor = MN_NONE, int err = -1);
   CUDTException(const CUDTException& e);

   ~CUDTException();

      /// Get the description of the exception.
      /// @return Text message for the exception description.

   const char* getErrorMessage();

      /// Get the system errno for the exception.
      /// @return errno.

   int getErrorCode() const;

      /// Get the system network errno for the exception.
      /// @return errno.

   int getErrno() const;
      /// Clear the error code.

   void clear();

private:
   CodeMajor m_iMajor;        // major exception categories
   CodeMinor m_iMinor;		// for specific error reasons
   int m_iErrno;		// errno returned by the system if there is any
   std::string m_strMsg;	// text error message

   std::string m_strAPI;	// the name of UDT function that returns the error
   std::string m_strDebug;	// debug information, set to the original place that causes the error

public: // Legacy Error Code

    static const int EUNKNOWN = SRT_EUNKNOWN;
    static const int SUCCESS = SRT_SUCCESS;
    static const int ECONNSETUP = SRT_ECONNSETUP;
    static const int ENOSERVER = SRT_ENOSERVER;
    static const int ECONNREJ = SRT_ECONNREJ;
    static const int ESOCKFAIL = SRT_ESOCKFAIL;
    static const int ESECFAIL = SRT_ESECFAIL;
    static const int ECONNFAIL = SRT_ECONNFAIL;
    static const int ECONNLOST = SRT_ECONNLOST;
    static const int ENOCONN = SRT_ENOCONN;
    static const int ERESOURCE = SRT_ERESOURCE;
    static const int ETHREAD = SRT_ETHREAD;
    static const int ENOBUF = SRT_ENOBUF;
    static const int EFILE = SRT_EFILE;
    static const int EINVRDOFF = SRT_EINVRDOFF;
    static const int ERDPERM = SRT_ERDPERM;
    static const int EINVWROFF = SRT_EINVWROFF;
    static const int EWRPERM = SRT_EWRPERM;
    static const int EINVOP = SRT_EINVOP;
    static const int EBOUNDSOCK = SRT_EBOUNDSOCK;
    static const int ECONNSOCK = SRT_ECONNSOCK;
    static const int EINVPARAM = SRT_EINVPARAM;
    static const int EINVSOCK = SRT_EINVSOCK;
    static const int EUNBOUNDSOCK = SRT_EUNBOUNDSOCK;
    static const int ESTREAMILL = SRT_EINVALMSGAPI;
    static const int EDGRAMILL = SRT_EINVALBUFFERAPI;
    static const int ENOLISTEN = SRT_ENOLISTEN;
    static const int ERDVNOSERV = SRT_ERDVNOSERV;
    static const int ERDVUNBOUND = SRT_ERDVUNBOUND;
    static const int EINVALMSGAPI = SRT_EINVALMSGAPI;
    static const int EINVALBUFFERAPI = SRT_EINVALBUFFERAPI;
    static const int EDUPLISTEN = SRT_EDUPLISTEN;
    static const int ELARGEMSG = SRT_ELARGEMSG;
    static const int EINVPOLLID = SRT_EINVPOLLID;
    static const int EASYNCFAIL = SRT_EASYNCFAIL;
    static const int EASYNCSND = SRT_EASYNCSND;
    static const int EASYNCRCV = SRT_EASYNCRCV;
    static const int ETIMEOUT = SRT_ETIMEOUT;
    static const int ECONGEST = SRT_ECONGEST;
    static const int EPEERERR = SRT_EPEERERR;
};

namespace UDT
{

typedef CUDTException ERRORINFO;
//typedef UDT_SOCKOPT SOCKOPT;
typedef CPerfMon TRACEINFO;
typedef CBytePerfMon TRACEBSTATS;
typedef ud_set UDSET;

UDT_API extern const SRTSOCKET INVALID_SOCK;
#undef ERROR
UDT_API extern const int ERROR;

UDT_API int startup();
UDT_API int cleanup();
UDT_API UDTSOCKET socket(int af, int type, int protocol);
UDT_API int bind(UDTSOCKET u, const struct sockaddr* name, int namelen);
UDT_API int bind2(UDTSOCKET u, UDPSOCKET udpsock);
UDT_API int listen(UDTSOCKET u, int backlog);
UDT_API UDTSOCKET accept(UDTSOCKET u, struct sockaddr* addr, int* addrlen);
UDT_API int connect(UDTSOCKET u, const struct sockaddr* name, int namelen);
UDT_API int close(UDTSOCKET u);
UDT_API int getpeername(UDTSOCKET u, struct sockaddr* name, int* namelen);
UDT_API int getsockname(UDTSOCKET u, struct sockaddr* name, int* namelen);
UDT_API int getsockopt(UDTSOCKET u, int level, SRT_SOCKOPT optname, void* optval, int* optlen);
UDT_API int setsockopt(UDTSOCKET u, int level, SRT_SOCKOPT optname, const void* optval, int optlen);
UDT_API int send(UDTSOCKET u, const char* buf, int len, int flags);
UDT_API int recv(UDTSOCKET u, char* buf, int len, int flags);

UDT_API int sendmsg(UDTSOCKET u, const char* buf, int len, int ttl = -1, bool inorder = false, uint64_t srctime = 0);
UDT_API int recvmsg(UDTSOCKET u, char* buf, int len, uint64_t& srctime);
UDT_API int recvmsg(UDTSOCKET u, char* buf, int len);

UDT_API int64_t sendfile(UDTSOCKET u, std::fstream& ifs, int64_t& offset, int64_t size, int block = 364000);
UDT_API int64_t recvfile(UDTSOCKET u, std::fstream& ofs, int64_t& offset, int64_t size, int block = 7280000);
UDT_API int64_t sendfile2(UDTSOCKET u, const char* path, int64_t* offset, int64_t size, int block = 364000);
UDT_API int64_t recvfile2(UDTSOCKET u, const char* path, int64_t* offset, int64_t size, int block = 7280000);

// select and selectEX are DEPRECATED; please use epoll. 
UDT_API int select(int nfds, UDSET* readfds, UDSET* writefds, UDSET* exceptfds, const struct timeval* timeout);
UDT_API int selectEx(const std::vector<UDTSOCKET>& fds, std::vector<UDTSOCKET>* readfds,
                     std::vector<UDTSOCKET>* writefds, std::vector<UDTSOCKET>* exceptfds, int64_t msTimeOut);

UDT_API int epoll_create();
UDT_API int epoll_add_usock(int eid, UDTSOCKET u, const int* events = NULL);
UDT_API int epoll_add_ssock(int eid, SYSSOCKET s, const int* events = NULL);
UDT_API int epoll_remove_usock(int eid, UDTSOCKET u);
UDT_API int epoll_remove_ssock(int eid, SYSSOCKET s);
UDT_API int epoll_update_usock(int eid, UDTSOCKET u, const int* events = NULL);
UDT_API int epoll_update_ssock(int eid, SYSSOCKET s, const int* events = NULL);
UDT_API int epoll_wait(int eid, std::set<UDTSOCKET>* readfds, std::set<UDTSOCKET>* writefds, int64_t msTimeOut,
                       std::set<SYSSOCKET>* lrfds = NULL, std::set<SYSSOCKET>* wrfds = NULL);
UDT_API int epoll_wait2(int eid, UDTSOCKET* readfds, int* rnum, UDTSOCKET* writefds, int* wnum, int64_t msTimeOut,
                        SYSSOCKET* lrfds = NULL, int* lrnum = NULL, SYSSOCKET* lwfds = NULL, int* lwnum = NULL);
UDT_API int epoll_uwait(const int eid, SRT_EPOLL_EVENT* fdsSet, int fdsSize, int64_t msTimeOut);
UDT_API int epoll_release(int eid);
UDT_API ERRORINFO& getlasterror();
UDT_API int getlasterror_code();
UDT_API const char* getlasterror_desc();
UDT_API int bstats(UDTSOCKET u, TRACEBSTATS* perf, bool clear = true);
UDT_API SRT_SOCKSTATUS getsockstate(UDTSOCKET u);

// This is a C++ SRT API extension. This is not a part of legacy UDT API.
UDT_API void setloglevel(srt_logging::LogLevel::type ll);
UDT_API void addlogfa(srt_logging::LogFA fa);
UDT_API void dellogfa(srt_logging::LogFA fa);
UDT_API void resetlogfa(std::set<srt_logging::LogFA> fas);
UDT_API void resetlogfa(const int* fara, size_t fara_size);
UDT_API void setlogstream(std::ostream& stream);
UDT_API void setloghandler(void* opaque, SRT_LOG_HANDLER_FN* handler);
UDT_API void setlogflags(int flags);

UDT_API bool setstreamid(UDTSOCKET u, const std::string& sid);
UDT_API std::string getstreamid(UDTSOCKET u);

}  // namespace UDT

// This is a log configuration used inside SRT.
// Applications using SRT, if they want to use the logging mechanism
// are free to create their own logger configuration objects for their
// own logger FA objects, or create their own. The object of this type
// is required to initialize the logger FA object.
namespace srt_logging { struct LogConfig; }
UDT_API extern srt_logging::LogConfig srt_logger_config;


#endif /* __cplusplus */

#endif
