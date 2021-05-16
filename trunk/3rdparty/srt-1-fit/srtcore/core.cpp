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
   Yunhong Gu, last updated 02/28/2012
modified by
   Haivision Systems Inc.
*****************************************************************************/

#ifndef _WIN32
#include <unistd.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#else
#include <winsock2.h>
#include <ws2tcpip.h>
#endif
#include <cmath>
#include <sstream>
#include "srt.h"
#include "queue.h"
#include "core.h"
#include "logging.h"
#include "crypto.h"
#include "logging_api.h" // Required due to containing extern srt_logger_config

// Again, just in case when some "smart guy" provided such a global macro
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

using namespace std;

namespace srt_logging
{

struct AllFaOn
{
    LogConfig::fa_bitset_t allfa;

    AllFaOn()
    {
        //        allfa.set(SRT_LOGFA_BSTATS, true);
        allfa.set(SRT_LOGFA_CONTROL, true);
        allfa.set(SRT_LOGFA_DATA, true);
        allfa.set(SRT_LOGFA_TSBPD, true);
        allfa.set(SRT_LOGFA_REXMIT, true);
        allfa.set(SRT_LOGFA_CONGEST, true);
#if ENABLE_HAICRYPT_LOGGING
        allfa.set(SRT_LOGFA_HAICRYPT, true);
#endif
    }
} logger_fa_all;

} // namespace srt_logging

// We need it outside the namespace to preserve the global name.
// It's a part of "hidden API" (used by applications)
SRT_API srt_logging::LogConfig srt_logger_config(srt_logging::logger_fa_all.allfa);

namespace srt_logging
{

Logger glog(SRT_LOGFA_GENERAL, srt_logger_config, "SRT.g");
// Unused. If not found useful, maybe reuse for another FA.
// Logger blog(SRT_LOGFA_BSTATS, srt_logger_config, "SRT.b");
Logger mglog(SRT_LOGFA_CONTROL, srt_logger_config, "SRT.c");
Logger dlog(SRT_LOGFA_DATA, srt_logger_config, "SRT.d");
Logger tslog(SRT_LOGFA_TSBPD, srt_logger_config, "SRT.t");
Logger rxlog(SRT_LOGFA_REXMIT, srt_logger_config, "SRT.r");
Logger cclog(SRT_LOGFA_CONGEST, srt_logger_config, "SRT.cc");

} // namespace srt_logging

using namespace srt_logging;

CUDTUnited CUDT::s_UDTUnited;

const SRTSOCKET UDT::INVALID_SOCK = CUDT::INVALID_SOCK;
const int       UDT::ERROR        = CUDT::ERROR;

// SRT Version constants
#define SRT_VERSION_UNK     0
#define SRT_VERSION_MAJ1    0x010000            /* Version 1 major */
#define SRT_VERSION_MAJ(v) (0xFF0000 & (v))     /* Major number ensuring backward compatibility */
#define SRT_VERSION_MIN(v) (0x00FF00 & (v))
#define SRT_VERSION_PCH(v) (0x0000FF & (v))

// NOTE: SRT_VERSION is primarily defined in the build file.
const int32_t SRT_DEF_VERSION = SrtParseVersion(SRT_VERSION);

//#define SRT_CMD_HSREQ       1           /* SRT Handshake Request (sender) */
#define SRT_CMD_HSREQ_MINSZ 8 /* Minumum Compatible (1.x.x) packet size (bytes) */
#define SRT_CMD_HSREQ_SZ 12   /* Current version packet size */
#if SRT_CMD_HSREQ_SZ > SRT_CMD_MAXSZ
#error SRT_CMD_MAXSZ too small
#endif
/*      Handshake Request (Network Order)
        0[31..0]:   SRT version     SRT_DEF_VERSION
        1[31..0]:   Options         0 [ | SRT_OPT_TSBPDSND ][ | SRT_OPT_HAICRYPT ]
        2[31..16]:  TsbPD resv      0
        2[15..0]:   TsbPD delay     [0..60000] msec
*/

//#define SRT_CMD_HSRSP       2           /* SRT Handshake Response (receiver) */
#define SRT_CMD_HSRSP_MINSZ 8 /* Minumum Compatible (1.x.x) packet size (bytes) */
#define SRT_CMD_HSRSP_SZ 12   /* Current version packet size */
#if SRT_CMD_HSRSP_SZ > SRT_CMD_MAXSZ
#error SRT_CMD_MAXSZ too small
#endif
/*      Handshake Response (Network Order)
        0[31..0]:   SRT version     SRT_DEF_VERSION
        1[31..0]:   Options         0 [ | SRT_OPT_TSBPDRCV [| SRT_OPT_TLPKTDROP ]][ | SRT_OPT_HAICRYPT]
                                      [ | SRT_OPT_NAKREPORT ] [ | SRT_OPT_REXMITFLG ]
        2[31..16]:  TsbPD resv      0
        2[15..0]:   TsbPD delay     [0..60000] msec
*/

void CUDT::construct()
{
    m_pSndBuffer           = NULL;
    m_pRcvBuffer           = NULL;
    m_pSndLossList         = NULL;
    m_pRcvLossList         = NULL;
    m_iReorderTolerance    = 0;
    m_iMaxReorderTolerance = 0; // Sensible optimal value is 10, 0 preserves old behavior
    m_iConsecEarlyDelivery = 0; // how many times so far the packet considered lost has been received before TTL expires
    m_iConsecOrderedDelivery = 0;

    m_pSndQueue = NULL;
    m_pRcvQueue = NULL;
    m_pPeerAddr = NULL;
    m_pSNode    = NULL;
    m_pRNode    = NULL;

    m_ullSndHsLastTime_us = 0;
    m_iSndHsRetryCnt      = SRT_MAX_HSRETRY + 1; // Will be reset to 0 for HSv5, this value is important for HSv4

    // Initial status
    m_bOpened             = false;
    m_bListening          = false;
    m_bConnecting         = false;
    m_bConnected          = false;
    m_bClosing            = false;
    m_bShutdown           = false;
    m_bBroken             = false;
    m_bPeerHealth         = true;
    m_RejectReason        = SRT_REJ_UNKNOWN;
    m_ullLingerExpiration = 0;
    m_llLastReqTime       = 0;

    m_lSrtVersion            = SRT_DEF_VERSION;
    m_lPeerSrtVersion        = 0; // not defined until connected.
    m_lMinimumPeerSrtVersion = SRT_VERSION_MAJ1;

    m_iTsbPdDelay_ms     = 0;
    m_iPeerTsbPdDelay_ms = 0;

    m_bPeerTsbPd         = false;
    m_iPeerTsbPdDelay_ms = 0;
    m_bTsbPd             = false;
    m_bTsbPdAckWakeup    = false;
    m_bPeerTLPktDrop     = false;

    m_uKmRefreshRatePkt = 0;
    m_uKmPreAnnouncePkt = 0;

    // Initilize mutex and condition variables
    initSynch();
}

CUDT::CUDT()
{
    construct();

    (void)SRT_DEF_VERSION;

    // Default UDT configurations
    m_iMSS            = 1500;
    m_bSynSending     = true;
    m_bSynRecving     = true;
    m_iFlightFlagSize = 25600;
    m_iSndBufSize     = 8192;
    m_iRcvBufSize = 8192; // Rcv buffer MUST NOT be bigger than Flight Flag size

    // Linger: LIVE mode defaults, please refer to `SRTO_TRANSTYPE` option
    // for other modes.
    m_Linger.l_onoff  = 0;
    m_Linger.l_linger = 0;
    m_iUDPSndBufSize  = 65536;
    m_iUDPRcvBufSize  = m_iRcvBufSize * m_iMSS;
    m_iSockType       = UDT_DGRAM;
    m_iIPversion      = AF_INET;
    m_bRendezvous     = false;
#ifdef SRT_ENABLE_CONNTIMEO
    m_iConnTimeOut = 3000;
#endif
    m_iSndTimeOut = -1;
    m_iRcvTimeOut = -1;
    m_bReuseAddr  = true;
    m_llMaxBW     = -1;
#ifdef SRT_ENABLE_IPOPTS
    m_iIpTTL = -1;
    m_iIpToS = -1;
#endif
    m_CryptoSecret.len = 0;
    m_iSndCryptoKeyLen = 0;
    // Cfg
    m_bDataSender           = false; // Sender only if true: does not recv data
    m_bOPT_TsbPd            = true;  // Enable TsbPd on sender
    m_iOPT_TsbPdDelay       = SRT_LIVE_DEF_LATENCY_MS;
    m_iOPT_PeerTsbPdDelay   = 0; // Peer's TsbPd delay as receiver (here is its minimum value, if used)
    m_bOPT_TLPktDrop        = true;
    m_iOPT_SndDropDelay     = 0;
    m_bOPT_StrictEncryption = true;
    m_iOPT_PeerIdleTimeout  = COMM_RESPONSE_TIMEOUT_MS;
    m_bTLPktDrop            = true; // Too-late Packet Drop
    m_bMessageAPI           = true;
    m_zOPT_ExpPayloadSize   = SRT_LIVE_DEF_PLSIZE;
    m_iIpV6Only             = -1;
    // Runtime
    m_bRcvNakReport             = true; // Receiver's Periodic NAK Reports
    m_llInputBW                 = 0;    // Application provided input bandwidth (internal input rate sampling == 0)
    m_iOverheadBW               = 25;   // Percent above input stream rate (applies if m_llMaxBW == 0)
    m_OPT_PktFilterConfigString = "";

    m_pCache = NULL;

    // Default congctl is "live".
    // Available builtin congctl: "file".
    // Other congctls can be registerred.

    // Note that 'select' returns false if there's no such congctl.
    // If so, congctl becomes unselected. Calling 'configure' on an
    // unselected congctl results in exception.
    m_CongCtl.select("live");
}

CUDT::CUDT(const CUDT &ancestor)
{
    construct();

    // XXX Consider all below fields (except m_bReuseAddr) to be put
    // into a separate class for easier copying.

    // Default UDT configurations
    m_iMSS            = ancestor.m_iMSS;
    m_bSynSending     = ancestor.m_bSynSending;
    m_bSynRecving     = ancestor.m_bSynRecving;
    m_iFlightFlagSize = ancestor.m_iFlightFlagSize;
    m_iSndBufSize     = ancestor.m_iSndBufSize;
    m_iRcvBufSize     = ancestor.m_iRcvBufSize;
    m_Linger          = ancestor.m_Linger;
    m_iUDPSndBufSize  = ancestor.m_iUDPSndBufSize;
    m_iUDPRcvBufSize  = ancestor.m_iUDPRcvBufSize;
    m_iSockType       = ancestor.m_iSockType;
    m_iIPversion      = ancestor.m_iIPversion;
    m_bRendezvous     = ancestor.m_bRendezvous;
#ifdef SRT_ENABLE_CONNTIMEO
    m_iConnTimeOut = ancestor.m_iConnTimeOut;
#endif
    m_iSndTimeOut = ancestor.m_iSndTimeOut;
    m_iRcvTimeOut = ancestor.m_iRcvTimeOut;
    m_bReuseAddr  = true; // this must be true, because all accepted sockets share the same port with the listener
    m_llMaxBW     = ancestor.m_llMaxBW;
#ifdef SRT_ENABLE_IPOPTS
    m_iIpTTL = ancestor.m_iIpTTL;
    m_iIpToS = ancestor.m_iIpToS;
#endif
    m_llInputBW             = ancestor.m_llInputBW;
    m_iOverheadBW           = ancestor.m_iOverheadBW;
    m_bDataSender           = ancestor.m_bDataSender;
    m_bOPT_TsbPd            = ancestor.m_bOPT_TsbPd;
    m_iOPT_TsbPdDelay       = ancestor.m_iOPT_TsbPdDelay;
    m_iOPT_PeerTsbPdDelay   = ancestor.m_iOPT_PeerTsbPdDelay;
    m_bOPT_TLPktDrop        = ancestor.m_bOPT_TLPktDrop;
    m_iOPT_SndDropDelay     = ancestor.m_iOPT_SndDropDelay;
    m_bOPT_StrictEncryption = ancestor.m_bOPT_StrictEncryption;
    m_iOPT_PeerIdleTimeout  = ancestor.m_iOPT_PeerIdleTimeout;
    m_zOPT_ExpPayloadSize   = ancestor.m_zOPT_ExpPayloadSize;
    m_bTLPktDrop            = ancestor.m_bTLPktDrop;
    m_bMessageAPI           = ancestor.m_bMessageAPI;
    m_iIpV6Only             = ancestor.m_iIpV6Only;
    m_iReorderTolerance     = ancestor.m_iMaxReorderTolerance;  // Initialize with maximum value
    m_iMaxReorderTolerance  = ancestor.m_iMaxReorderTolerance;
    // Runtime
    m_bRcvNakReport             = ancestor.m_bRcvNakReport;
    m_OPT_PktFilterConfigString = ancestor.m_OPT_PktFilterConfigString;

    m_CryptoSecret     = ancestor.m_CryptoSecret;
    m_iSndCryptoKeyLen = ancestor.m_iSndCryptoKeyLen;

    m_uKmRefreshRatePkt = ancestor.m_uKmRefreshRatePkt;
    m_uKmPreAnnouncePkt = ancestor.m_uKmPreAnnouncePkt;

    m_pCache = ancestor.m_pCache;

    // SrtCongestion's copy constructor copies the selection,
    // but not the underlying congctl object. After
    // copy-constructed, the 'configure' must be called on it again.
    m_CongCtl = ancestor.m_CongCtl;
}

CUDT::~CUDT()
{
    // release mutex/condtion variables
    destroySynch();

    // Wipeout critical data
    memset(&m_CryptoSecret, 0, sizeof(m_CryptoSecret));

    // destroy the data structures
    delete m_pSndBuffer;
    delete m_pRcvBuffer;
    delete m_pSndLossList;
    delete m_pRcvLossList;
    delete m_pPeerAddr;
    delete m_pSNode;
    delete m_pRNode;
}

// This function is to make it possible for both C and C++
// API to accept both bool and int types for boolean options.
// (it's not that C couldn't use <stdbool.h>, it's that people
// often forget to use correct type).
static bool bool_int_value(const void *optval, int optlen)
{
    if (optlen == sizeof(bool))
    {
        return *(bool *)optval;
    }

    if (optlen == sizeof(int))
    {
        return 0 != *(int *)optval; // 0!= is a windows warning-killer int-to-bool conversion
    }
    return false;
}

void CUDT::setOpt(SRT_SOCKOPT optName, const void *optval, int optlen)
{
    if (m_bBroken || m_bClosing)
        throw CUDTException(MJ_CONNECTION, MN_CONNLOST, 0);

    CGuard cg(m_ConnectionLock);
    CGuard sendguard(m_SendLock);
    CGuard recvguard(m_RecvLock);

    switch (optName)
    {
    case SRTO_MSS:
        if (m_bOpened)
            throw CUDTException(MJ_NOTSUP, MN_ISBOUND, 0);

        if (*(int *)optval < int(CPacket::UDP_HDR_SIZE + CHandShake::m_iContentSize))
            throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);

        m_iMSS = *(int *)optval;

        // Packet size cannot be greater than UDP buffer size
        if (m_iMSS > m_iUDPSndBufSize)
            m_iMSS = m_iUDPSndBufSize;
        if (m_iMSS > m_iUDPRcvBufSize)
            m_iMSS = m_iUDPRcvBufSize;

        break;

    case SRTO_SNDSYN:
        m_bSynSending = bool_int_value(optval, optlen);
        break;

    case SRTO_RCVSYN:
        m_bSynRecving = bool_int_value(optval, optlen);
        break;

    case SRTO_FC:
        if (m_bConnecting || m_bConnected)
            throw CUDTException(MJ_NOTSUP, MN_ISCONNECTED, 0);

        if (*(int *)optval < 1)
            throw CUDTException(MJ_NOTSUP, MN_INVAL);

        // Mimimum recv flight flag size is 32 packets
        if (*(int *)optval > 32)
            m_iFlightFlagSize = *(int *)optval;
        else
            m_iFlightFlagSize = 32;

        break;

    case SRTO_SNDBUF:
        if (m_bOpened)
            throw CUDTException(MJ_NOTSUP, MN_ISBOUND, 0);

        if (*(int *)optval <= 0)
            throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);

        m_iSndBufSize = *(int *)optval / (m_iMSS - CPacket::UDP_HDR_SIZE);

        break;

    case SRTO_RCVBUF:
        if (m_bOpened)
            throw CUDTException(MJ_NOTSUP, MN_ISBOUND, 0);

        if (*(int *)optval <= 0)
            throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);

        {
            // This weird cast through int is required because
            // API requires 'int', and internals require 'size_t';
            // their size is different on 64-bit systems.
            size_t val = size_t(*(int *)optval);

            // Mimimum recv buffer size is 32 packets
            size_t mssin_size = m_iMSS - CPacket::UDP_HDR_SIZE;

            // XXX This magic 32 deserves some constant
            if (val > mssin_size * 32)
                m_iRcvBufSize = val / mssin_size;
            else
                m_iRcvBufSize = 32;

            // recv buffer MUST not be greater than FC size
            if (m_iRcvBufSize > m_iFlightFlagSize)
                m_iRcvBufSize = m_iFlightFlagSize;
        }

        break;

    case SRTO_LINGER:
        m_Linger = *(linger *)optval;
        break;

    case SRTO_UDP_SNDBUF:
        if (m_bOpened)
            throw CUDTException(MJ_NOTSUP, MN_ISBOUND, 0);

        m_iUDPSndBufSize = *(int *)optval;

        if (m_iUDPSndBufSize < m_iMSS)
            m_iUDPSndBufSize = m_iMSS;

        break;

    case SRTO_UDP_RCVBUF:
        if (m_bOpened)
            throw CUDTException(MJ_NOTSUP, MN_ISBOUND, 0);

        m_iUDPRcvBufSize = *(int *)optval;

        if (m_iUDPRcvBufSize < m_iMSS)
            m_iUDPRcvBufSize = m_iMSS;

        break;

    case SRTO_RENDEZVOUS:
        if (m_bConnecting || m_bConnected)
            throw CUDTException(MJ_NOTSUP, MN_ISBOUND, 0);
        m_bRendezvous = bool_int_value(optval, optlen);
        break;

    case SRTO_SNDTIMEO:
        m_iSndTimeOut = *(int *)optval;
        break;

    case SRTO_RCVTIMEO:
        m_iRcvTimeOut = *(int *)optval;
        break;

    case SRTO_REUSEADDR:
        if (m_bOpened)
            throw CUDTException(MJ_NOTSUP, MN_ISBOUND, 0);
        m_bReuseAddr = bool_int_value(optval, optlen);
        break;

    case SRTO_MAXBW:
        m_llMaxBW = *(int64_t *)optval;

        // This can be done on both connected and unconnected socket.
        // When not connected, this will do nothing, however this
        // event will be repeated just after connecting anyway.
        if (m_bConnected)
            updateCC(TEV_INIT, TEV_INIT_RESET);
        break;

#ifdef SRT_ENABLE_IPOPTS
    case SRTO_IPTTL:
        if (m_bOpened)
            throw CUDTException(MJ_NOTSUP, MN_ISBOUND, 0);
        if (!(*(int *)optval == -1) && !((*(int *)optval >= 1) && (*(int *)optval <= 255)))
            throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);
        m_iIpTTL = *(int *)optval;
        break;

    case SRTO_IPTOS:
        if (m_bOpened)
            throw CUDTException(MJ_NOTSUP, MN_ISBOUND, 0);
        m_iIpToS = *(int *)optval;
        break;
#endif

    case SRTO_INPUTBW:
        m_llInputBW = *(int64_t *)optval;
        // (only if connected; if not, then the value
        // from m_iOverheadBW will be used initially)
        if (m_bConnected)
            updateCC(TEV_INIT, TEV_INIT_INPUTBW);
        break;

    case SRTO_OHEADBW:
        if ((*(int *)optval < 5) || (*(int *)optval > 100))
            throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);
        m_iOverheadBW = *(int *)optval;

        // Changed overhead BW, so spread the change
        // (only if connected; if not, then the value
        // from m_iOverheadBW will be used initially)
        if (m_bConnected)
            updateCC(TEV_INIT, TEV_INIT_OHEADBW);
        break;

    case SRTO_SENDER:
        if (m_bConnected)
            throw CUDTException(MJ_NOTSUP, MN_ISCONNECTED, 0);
        m_bDataSender = bool_int_value(optval, optlen);
        break;

    case SRTO_TSBPDMODE:
        if (m_bConnected)
            throw CUDTException(MJ_NOTSUP, MN_ISCONNECTED, 0);
        m_bOPT_TsbPd = bool_int_value(optval, optlen);
        break;

    case SRTO_LATENCY:
        if (m_bConnected)
            throw CUDTException(MJ_NOTSUP, MN_ISCONNECTED, 0);
        m_iOPT_TsbPdDelay     = *(int *)optval;
        m_iOPT_PeerTsbPdDelay = *(int *)optval;
        break;

    case SRTO_RCVLATENCY:
        if (m_bConnected)
            throw CUDTException(MJ_NOTSUP, MN_ISCONNECTED, 0);
        m_iOPT_TsbPdDelay = *(int *)optval;
        break;

    case SRTO_PEERLATENCY:
        if (m_bConnected)
            throw CUDTException(MJ_NOTSUP, MN_ISCONNECTED, 0);
        m_iOPT_PeerTsbPdDelay = *(int *)optval;
        break;

    case SRTO_TLPKTDROP:
        if (m_bConnected)
            throw CUDTException(MJ_NOTSUP, MN_ISCONNECTED, 0);
        m_bOPT_TLPktDrop = bool_int_value(optval, optlen);
        break;

    case SRTO_SNDDROPDELAY:
        // Surprise: you may be connected to alter this option.
        // The application may manipulate this option on sender while transmitting.
        m_iOPT_SndDropDelay = *(int *)optval;
        break;

    case SRTO_PASSPHRASE:
        // For consistency, throw exception when connected,
        // no matter if otherwise the password can be set.
        if (m_bConnected)
            throw CUDTException(MJ_NOTSUP, MN_ISCONNECTED, 0);

#ifdef SRT_ENABLE_ENCRYPTION
        // Password must be 10-80 characters.
        // Or it can be empty to clear the password.
        if ((optlen != 0) && (optlen < 10 || optlen > HAICRYPT_SECRET_MAX_SZ))
            throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);

        memset(&m_CryptoSecret, 0, sizeof(m_CryptoSecret));
        m_CryptoSecret.typ = HAICRYPT_SECTYP_PASSPHRASE;
        m_CryptoSecret.len = (optlen <= (int)sizeof(m_CryptoSecret.str) ? optlen : (int)sizeof(m_CryptoSecret.str));
        memcpy(m_CryptoSecret.str, optval, m_CryptoSecret.len);
#else
        if (optlen == 0)
            break;

        LOGC(mglog.Error, log << "SRTO_PASSPHRASE: encryption not enabled at compile time");
        throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);
#endif
        break;

    case SRTO_PBKEYLEN:
    case _DEPRECATED_SRTO_SNDPBKEYLEN:
        if (m_bConnected)
            throw CUDTException(MJ_NOTSUP, MN_ISCONNECTED, 0);
#ifdef SRT_ENABLE_ENCRYPTION
        {
            int v          = *(int *)optval;
            int allowed[4] = {
                0,  // Default value, if this results for initiator, defaults to 16. See below.
                16, // AES-128
                24, // AES-192
                32  // AES-256
            };
            int *allowed_end = allowed + 4;
            if (find(allowed, allowed_end, v) == allowed_end)
            {
                LOGC(mglog.Error,
                     log << "Invalid value for option SRTO_PBKEYLEN: " << v << "; allowed are: 0, 16, 24, 32");
                throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);
            }

            // Note: This works a little different in HSv4 and HSv5.

            // HSv4:
            // The party that is set SRTO_SENDER will send KMREQ, and it will
            // use default value 16, if SRTO_PBKEYLEN is the default value 0.
            // The responder that receives KMRSP has nothing to say about
            // PBKEYLEN anyway and it will take the length of the key from
            // the initiator (sender) as a good deal.
            //
            // HSv5:
            // The initiator (independently on the sender) will send KMREQ,
            // and as it should be the sender to decide about the PBKEYLEN.
            // Your application should do the following then:
            // 1. The sender should set PBKEYLEN to the required value.
            // 2. If the sender is initiator, it will create the key using
            //    its preset PBKEYLEN (or default 16, if not set) and the
            //    receiver-responder will take it as a good deal.
            // 3. Leave the PBKEYLEN value on the receiver as default 0.
            // 4. If sender is responder, it should then advertise the PBKEYLEN
            //    value in the initial handshake messages (URQ_INDUCTION if
            //    listener, and both URQ_WAVEAHAND and URQ_CONCLUSION in case
            //    of rendezvous, as it is the matter of luck who of them will
            //    eventually become the initiator). This way the receiver
            //    being an initiator will set m_iSndCryptoKeyLen before setting
            //    up KMREQ for sending to the sender-responder.
            //
            // Note that in HSv5 if both sides set PBKEYLEN, the responder
            // wins, unless the initiator is a sender (the effective PBKEYLEN
            // will be the one advertised by the responder). If none sets,
            // PBKEYLEN will default to 16.

            m_iSndCryptoKeyLen = v;
        }
#else
        LOGC(mglog.Error, log << "SRTO_PBKEYLEN: encryption not enabled at compile time");
        throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);
#endif
        break;

    case SRTO_NAKREPORT:
        if (m_bConnected)
            throw CUDTException(MJ_NOTSUP, MN_ISCONNECTED, 0);
        m_bRcvNakReport = bool_int_value(optval, optlen);
        break;

#ifdef SRT_ENABLE_CONNTIMEO
    case SRTO_CONNTIMEO:
        m_iConnTimeOut = *(int *)optval;
        break;
#endif

    case SRTO_LOSSMAXTTL:
        m_iMaxReorderTolerance = *(int *)optval;
        if (!m_bConnected)
            m_iReorderTolerance = m_iMaxReorderTolerance;
        break;

    case SRTO_VERSION:
        if (m_bConnected)
            throw CUDTException(MJ_NOTSUP, MN_ISCONNECTED, 0);
        m_lSrtVersion = *(uint32_t *)optval;
        break;

    case SRTO_MINVERSION:
        if (m_bConnected)
            throw CUDTException(MJ_NOTSUP, MN_ISCONNECTED, 0);
        m_lMinimumPeerSrtVersion = *(uint32_t *)optval;
        break;

    case SRTO_STREAMID:
        if (m_bConnected)
            throw CUDTException(MJ_NOTSUP, MN_ISCONNECTED, 0);

        if (size_t(optlen) > MAX_SID_LENGTH)
            throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);

        m_sStreamName.assign((const char *)optval, optlen);
        break;

    case SRTO_CONGESTION:
        if (m_bConnected)
            throw CUDTException(MJ_NOTSUP, MN_ISCONNECTED, 0);

        {
            string val;
            if (optlen == -1)
                val = (const char *)optval;
            else
                val.assign((const char *)optval, optlen);

            // Translate alias
            if (val == "vod")
                val = "file";

            bool res = m_CongCtl.select(val);
            if (!res)
                throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);
        }
        break;

    case SRTO_MESSAGEAPI:
        if (m_bConnected)
            throw CUDTException(MJ_NOTSUP, MN_ISCONNECTED, 0);

        m_bMessageAPI = bool_int_value(optval, optlen);
        break;

    case SRTO_PAYLOADSIZE:
        if (m_bConnected)
            throw CUDTException(MJ_NOTSUP, MN_ISCONNECTED, 0);

        if (*(int *)optval > SRT_LIVE_MAX_PLSIZE)
        {
            LOGC(mglog.Error, log << "SRTO_PAYLOADSIZE: value exceeds SRT_LIVE_MAX_PLSIZE, maximum payload per MTU.");
            throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);
        }

        if (m_OPT_PktFilterConfigString != "")
        {
            // This means that the filter might have been installed before,
            // and the fix to the maximum payload size was already applied.
            // This needs to be checked now.
            SrtFilterConfig fc;
            if (!ParseFilterConfig(m_OPT_PktFilterConfigString, fc))
            {
                // Break silently. This should not happen
                LOGC(mglog.Error, log << "SRTO_PAYLOADSIZE: IPE: failing filter configuration installed");
                throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);
            }

            size_t efc_max_payload_size = SRT_LIVE_MAX_PLSIZE - fc.extra_size;
            if (m_zOPT_ExpPayloadSize > efc_max_payload_size)
            {
                LOGC(mglog.Error,
                     log << "SRTO_PAYLOADSIZE: value exceeds SRT_LIVE_MAX_PLSIZE decreased by " << fc.extra_size
                         << " required for packet filter header");
                throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);
            }
        }

        m_zOPT_ExpPayloadSize = *(int *)optval;
        break;

    case SRTO_TRANSTYPE:
        if (m_bConnected)
            throw CUDTException(MJ_NOTSUP, MN_ISCONNECTED, 0);

        // XXX Note that here the configuration for SRTT_LIVE
        // is the same as DEFAULT VALUES for these fields set
        // in CUDT::CUDT.
        switch (*(SRT_TRANSTYPE *)optval)
        {
        case SRTT_LIVE:
            // Default live options:
            // - tsbpd: on
            // - latency: 120ms
            // - linger: off
            // - congctl: live
            // - extraction method: message (reading call extracts one message)
            m_bOPT_TsbPd          = true;
            m_iOPT_TsbPdDelay     = SRT_LIVE_DEF_LATENCY_MS;
            m_iOPT_PeerTsbPdDelay = 0;
            m_bOPT_TLPktDrop      = true;
            m_iOPT_SndDropDelay   = 0;
            m_bMessageAPI         = true;
            m_bRcvNakReport       = true;
            m_zOPT_ExpPayloadSize = SRT_LIVE_DEF_PLSIZE;
            m_Linger.l_onoff      = 0;
            m_Linger.l_linger     = 0;
            m_CongCtl.select("live");
            break;

        case SRTT_FILE:
            // File transfer mode:
            // - tsbpd: off
            // - latency: 0
            // - linger: 2 minutes (180s)
            // - congctl: file (original UDT congestion control)
            // - extraction method: stream (reading call extracts as many bytes as available and fits in buffer)
            m_bOPT_TsbPd          = false;
            m_iOPT_TsbPdDelay     = 0;
            m_iOPT_PeerTsbPdDelay = 0;
            m_bOPT_TLPktDrop      = false;
            m_iOPT_SndDropDelay   = -1;
            m_bMessageAPI         = false;
            m_bRcvNakReport       = false;
            m_zOPT_ExpPayloadSize = 0; // use maximum
            m_Linger.l_onoff      = 1;
            m_Linger.l_linger     = 180; // 2 minutes
            m_CongCtl.select("file");
            break;

        default:
            throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);
        }
        break;

    case SRTO_KMREFRESHRATE:
        if (m_bConnected)
            throw CUDTException(MJ_NOTSUP, MN_ISCONNECTED, 0);

        // If you first change the KMREFRESHRATE, KMPREANNOUNCE
        // will be set to the maximum allowed value
        m_uKmRefreshRatePkt = *(int *)optval;
        if (m_uKmPreAnnouncePkt == 0 || m_uKmPreAnnouncePkt > (m_uKmRefreshRatePkt - 1) / 2)
        {
            m_uKmPreAnnouncePkt = (m_uKmRefreshRatePkt - 1) / 2;
            LOGC(mglog.Warn,
                 log << "SRTO_KMREFRESHRATE=0x" << hex << m_uKmRefreshRatePkt << ": setting SRTO_KMPREANNOUNCE=0x"
                     << hex << m_uKmPreAnnouncePkt);
        }
        break;

    case SRTO_KMPREANNOUNCE:
        if (m_bConnected)
            throw CUDTException(MJ_NOTSUP, MN_ISCONNECTED, 0);
        {
            int val   = *(int *)optval;
            int kmref = m_uKmRefreshRatePkt == 0 ? HAICRYPT_DEF_KM_REFRESH_RATE : m_uKmRefreshRatePkt;
            if (val > (kmref - 1) / 2)
            {
                LOGC(mglog.Error,
                     log << "SRTO_KMPREANNOUNCE=0x" << hex << val << " exceeds KmRefresh/2, 0x" << ((kmref - 1) / 2)
                         << " - OPTION REJECTED.");
                throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);
            }

            m_uKmPreAnnouncePkt = val;
        }
        break;

    case SRTO_ENFORCEDENCRYPTION:
        if (m_bConnected)
            throw CUDTException(MJ_NOTSUP, MN_ISCONNECTED, 0);

        m_bOPT_StrictEncryption = bool_int_value(optval, optlen);
        break;

    case SRTO_PEERIDLETIMEO:

        if (m_bConnected)
            throw CUDTException(MJ_NOTSUP, MN_ISCONNECTED, 0);
        m_iOPT_PeerIdleTimeout = *(int *)optval;
        break;

    case SRTO_IPV6ONLY:
        if (m_bConnected)
            throw CUDTException(MJ_NOTSUP, MN_ISCONNECTED, 0);

        m_iIpV6Only = *(int *)optval;
        break;

    case SRTO_PACKETFILTER:
        if (m_bConnected)
            throw CUDTException(MJ_NOTSUP, MN_ISCONNECTED, 0);

        {
            string arg((char *)optval, optlen);
            // Parse the configuration string prematurely
            SrtFilterConfig fc;
            if (!ParseFilterConfig(arg, fc))
            {
                LOGC(mglog.Error,
                     log << "SRTO_FILTER: Incorrect syntax. Use: FILTERTYPE[,KEY:VALUE...]. "
                            "FILTERTYPE ("
                         << fc.type << ") must be installed (or builtin)");
                throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);
            }

            size_t efc_max_payload_size = SRT_LIVE_MAX_PLSIZE - fc.extra_size;
            if (m_zOPT_ExpPayloadSize > efc_max_payload_size)
            {
                LOGC(mglog.Warn,
                     log << "Due to filter-required extra " << fc.extra_size << " bytes, SRTO_PAYLOADSIZE fixed to "
                         << efc_max_payload_size << " bytes");
                m_zOPT_ExpPayloadSize = efc_max_payload_size;
            }

            m_OPT_PktFilterConfigString = arg;
        }
        break;

    default:
        throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);
    }
}

void CUDT::getOpt(SRT_SOCKOPT optName, void *optval, int &optlen)
{
    CGuard cg(m_ConnectionLock);

    switch (optName)
    {
    case SRTO_MSS:
        *(int *)optval = m_iMSS;
        optlen         = sizeof(int);
        break;

    case SRTO_SNDSYN:
        *(bool *)optval = m_bSynSending;
        optlen          = sizeof(bool);
        break;

    case SRTO_RCVSYN:
        *(bool *)optval = m_bSynRecving;
        optlen          = sizeof(bool);
        break;

    case SRTO_ISN:
        *(int *)optval = m_iISN;
        optlen         = sizeof(int);
        break;

    case SRTO_FC:
        *(int *)optval = m_iFlightFlagSize;
        optlen         = sizeof(int);
        break;

    case SRTO_SNDBUF:
        *(int *)optval = m_iSndBufSize * (m_iMSS - CPacket::UDP_HDR_SIZE);
        optlen         = sizeof(int);
        break;

    case SRTO_RCVBUF:
        *(int *)optval = m_iRcvBufSize * (m_iMSS - CPacket::UDP_HDR_SIZE);
        optlen         = sizeof(int);
        break;

    case SRTO_LINGER:
        if (optlen < (int)(sizeof(linger)))
            throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);

        *(linger *)optval = m_Linger;
        optlen            = sizeof(linger);
        break;

    case SRTO_UDP_SNDBUF:
        *(int *)optval = m_iUDPSndBufSize;
        optlen         = sizeof(int);
        break;

    case SRTO_UDP_RCVBUF:
        *(int *)optval = m_iUDPRcvBufSize;
        optlen         = sizeof(int);
        break;

    case SRTO_RENDEZVOUS:
        *(bool *)optval = m_bRendezvous;
        optlen          = sizeof(bool);
        break;

    case SRTO_SNDTIMEO:
        *(int *)optval = m_iSndTimeOut;
        optlen         = sizeof(int);
        break;

    case SRTO_RCVTIMEO:
        *(int *)optval = m_iRcvTimeOut;
        optlen         = sizeof(int);
        break;

    case SRTO_REUSEADDR:
        *(bool *)optval = m_bReuseAddr;
        optlen          = sizeof(bool);
        break;

    case SRTO_MAXBW:
        *(int64_t *)optval = m_llMaxBW;
        optlen             = sizeof(int64_t);
        break;

    case SRTO_STATE:
        *(int32_t *)optval = s_UDTUnited.getStatus(m_SocketID);
        optlen             = sizeof(int32_t);
        break;

    case SRTO_EVENT:
    {
        int32_t event = 0;
        if (m_bBroken)
            event |= UDT_EPOLL_ERR;
        else
        {
            CGuard::enterCS(m_RecvLock);
            if (m_pRcvBuffer && m_pRcvBuffer->isRcvDataReady())
                event |= UDT_EPOLL_IN;
            CGuard::leaveCS(m_RecvLock);
            if (m_pSndBuffer && (m_iSndBufSize > m_pSndBuffer->getCurrBufSize()))
                event |= UDT_EPOLL_OUT;
        }
        *(int32_t *)optval = event;
        optlen             = sizeof(int32_t);
        break;
    }

    case SRTO_SNDDATA:
        if (m_pSndBuffer)
            *(int32_t *)optval = m_pSndBuffer->getCurrBufSize();
        else
            *(int32_t *)optval = 0;
        optlen = sizeof(int32_t);
        break;

    case SRTO_RCVDATA:
        if (m_pRcvBuffer)
        {
            CGuard::enterCS(m_RecvLock);
            *(int32_t *)optval = m_pRcvBuffer->getRcvDataSize();
            CGuard::leaveCS(m_RecvLock);
        }
        else
            *(int32_t *)optval = 0;
        optlen = sizeof(int32_t);
        break;

#ifdef SRT_ENABLE_IPOPTS
    case SRTO_IPTTL:
        if (m_bOpened)
            *(int32_t *)optval = m_pSndQueue->getIpTTL();
        else
            *(int32_t *)optval = m_iIpTTL;
        optlen = sizeof(int32_t);
        break;

    case SRTO_IPTOS:
        if (m_bOpened)
            *(int32_t *)optval = m_pSndQueue->getIpToS();
        else
            *(int32_t *)optval = m_iIpToS;
        optlen = sizeof(int32_t);
        break;
#endif

    case SRTO_SENDER:
        *(int32_t *)optval = m_bDataSender;
        optlen             = sizeof(int32_t);
        break;

    case SRTO_TSBPDMODE:
        *(int32_t *)optval = m_bOPT_TsbPd;
        optlen             = sizeof(int32_t);
        break;

    case SRTO_LATENCY:
    case SRTO_RCVLATENCY:
        *(int32_t *)optval = m_iTsbPdDelay_ms;
        optlen             = sizeof(int32_t);
        break;

    case SRTO_PEERLATENCY:
        *(int32_t *)optval = m_iPeerTsbPdDelay_ms;
        optlen             = sizeof(int32_t);
        break;

    case SRTO_TLPKTDROP:
        *(int32_t *)optval = m_bTLPktDrop;
        optlen             = sizeof(int32_t);
        break;

    case SRTO_SNDDROPDELAY:
        *(int32_t *)optval = m_iOPT_SndDropDelay;
        optlen             = sizeof(int32_t);
        break;

    case SRTO_PBKEYLEN:
        if (m_pCryptoControl)
            *(int32_t *)optval = m_pCryptoControl->KeyLen(); // Running Key length.
        else
            *(int32_t *)optval = m_iSndCryptoKeyLen; // May be 0.
        optlen = sizeof(int32_t);
        break;

    case SRTO_KMSTATE:
        if (!m_pCryptoControl)
            *(int32_t *)optval = SRT_KM_S_UNSECURED;
        else if (m_bDataSender)
            *(int32_t *)optval = m_pCryptoControl->m_SndKmState;
        else
            *(int32_t *)optval = m_pCryptoControl->m_RcvKmState;
        optlen = sizeof(int32_t);
        break;

    case SRTO_SNDKMSTATE: // State imposed by Agent depending on PW and KMX
        if (m_pCryptoControl)
            *(int32_t *)optval = m_pCryptoControl->m_SndKmState;
        else
            *(int32_t *)optval = SRT_KM_S_UNSECURED;
        optlen = sizeof(int32_t);
        break;

    case SRTO_RCVKMSTATE: // State returned by Peer as informed during KMX
        if (m_pCryptoControl)
            *(int32_t *)optval = m_pCryptoControl->m_RcvKmState;
        else
            *(int32_t *)optval = SRT_KM_S_UNSECURED;
        optlen = sizeof(int32_t);
        break;

    case SRTO_LOSSMAXTTL:
        *(int32_t*)optval = m_iMaxReorderTolerance;
        optlen = sizeof(int32_t);
        break;

    case SRTO_NAKREPORT:
        *(bool *)optval = m_bRcvNakReport;
        optlen          = sizeof(bool);
        break;

    case SRTO_VERSION:
        *(int32_t *)optval = m_lSrtVersion;
        optlen             = sizeof(int32_t);
        break;

    case SRTO_PEERVERSION:
        *(int32_t *)optval = m_lPeerSrtVersion;
        optlen             = sizeof(int32_t);
        break;

#ifdef SRT_ENABLE_CONNTIMEO
    case SRTO_CONNTIMEO:
        *(int *)optval = m_iConnTimeOut;
        optlen         = sizeof(int);
        break;
#endif

    case SRTO_MINVERSION:
        *(uint32_t *)optval = m_lMinimumPeerSrtVersion;
        optlen              = sizeof(uint32_t);
        break;

    case SRTO_STREAMID:
        if (size_t(optlen) < m_sStreamName.size() + 1)
            throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);

        strcpy((char *)optval, m_sStreamName.c_str());
        optlen = m_sStreamName.size();
        break;

    case SRTO_CONGESTION:
    {
        string tt = m_CongCtl.selected_name();
        strcpy((char *)optval, tt.c_str());
        optlen = tt.size();
    }
    break;

    case SRTO_MESSAGEAPI:
        optlen          = sizeof(bool);
        *(bool *)optval = m_bMessageAPI;
        break;

    case SRTO_PAYLOADSIZE:
        optlen         = sizeof(int);
        *(int *)optval = m_zOPT_ExpPayloadSize;
        break;

    case SRTO_ENFORCEDENCRYPTION:
        optlen             = sizeof(int32_t); // also with TSBPDMODE and SENDER
        *(int32_t *)optval = m_bOPT_StrictEncryption;
        break;

    case SRTO_IPV6ONLY:
        optlen         = sizeof(int);
        *(int *)optval = m_iIpV6Only;
        break;

    case SRTO_PEERIDLETIMEO:
        *(int *)optval = m_iOPT_PeerIdleTimeout;
        optlen         = sizeof(int);
        break;

    case SRTO_PACKETFILTER:
        if (size_t(optlen) < m_OPT_PktFilterConfigString.size() + 1)
            throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);

        strcpy((char *)optval, m_OPT_PktFilterConfigString.c_str());
        optlen = m_OPT_PktFilterConfigString.size();
        break;

    default:
        throw CUDTException(MJ_NOTSUP, MN_NONE, 0);
    }
}

bool CUDT::setstreamid(SRTSOCKET u, const std::string &sid)
{
    CUDT *that = getUDTHandle(u);
    if (!that)
        return false;

    if (sid.size() > MAX_SID_LENGTH)
        return false;

    if (that->m_bConnected)
        return false;

    that->m_sStreamName = sid;
    return true;
}

std::string CUDT::getstreamid(SRTSOCKET u)
{
    CUDT *that = getUDTHandle(u);
    if (!that)
        return "";

    return that->m_sStreamName;
}

// XXX REFACTOR: Make common code for CUDT constructor and clearData,
// possibly using CUDT::construct.
void CUDT::clearData()
{
    // Initial sequence number, loss, acknowledgement, etc.
    int udpsize = m_iMSS - CPacket::UDP_HDR_SIZE;

    m_iMaxSRTPayloadSize = udpsize - CPacket::HDR_SIZE;

    HLOGC(mglog.Debug, log << "clearData: PAYLOAD SIZE: " << m_iMaxSRTPayloadSize);

    m_iEXPCount  = 1;
    m_iBandwidth = 1; // pkts/sec
    // XXX use some constant for this 16
    m_iDeliveryRate     = 16;
    m_iByteDeliveryRate = 16 * m_iMaxSRTPayloadSize;
    m_iAckSeqNo         = 0;
    m_ullLastAckTime_tk = 0;

    // trace information
    CGuard::enterCS(m_StatsLock);
    m_stats.startTime = CTimer::getTime();
    m_stats.sentTotal = m_stats.recvTotal = m_stats.sndLossTotal = m_stats.rcvLossTotal = m_stats.retransTotal =
        m_stats.sentACKTotal = m_stats.recvACKTotal = m_stats.sentNAKTotal = m_stats.recvNAKTotal = 0;
    m_stats.lastSampleTime                                                                        = CTimer::getTime();
    m_stats.traceSent = m_stats.traceRecv = m_stats.traceSndLoss = m_stats.traceRcvLoss = m_stats.traceRetrans =
        m_stats.sentACK = m_stats.recvACK = m_stats.sentNAK = m_stats.recvNAK = 0;
    m_stats.traceRcvRetrans                                                   = 0;
    m_stats.traceReorderDistance                                              = 0;
    m_stats.traceBelatedTime                                                  = 0.0;
    m_stats.traceRcvBelated                                                   = 0;

    m_stats.sndDropTotal = 0;
    m_stats.traceSndDrop = 0;
    m_stats.rcvDropTotal = 0;
    m_stats.traceRcvDrop = 0;

    m_stats.m_rcvUndecryptTotal = 0;
    m_stats.traceRcvUndecrypt   = 0;

    m_stats.bytesSentTotal    = 0;
    m_stats.bytesRecvTotal    = 0;
    m_stats.bytesRetransTotal = 0;
    m_stats.traceBytesSent    = 0;
    m_stats.traceBytesRecv    = 0;
    m_stats.sndFilterExtra    = 0;
    m_stats.rcvFilterExtra    = 0;
    m_stats.rcvFilterSupply   = 0;
    m_stats.rcvFilterLoss     = 0;

    m_stats.traceBytesRetrans = 0;
#ifdef SRT_ENABLE_LOSTBYTESCOUNT
    m_stats.traceRcvBytesLoss = 0;
#endif
    m_stats.sndBytesDropTotal        = 0;
    m_stats.rcvBytesDropTotal        = 0;
    m_stats.traceSndBytesDrop        = 0;
    m_stats.traceRcvBytesDrop        = 0;
    m_stats.m_rcvBytesUndecryptTotal = 0;
    m_stats.traceRcvBytesUndecrypt   = 0;

    m_stats.sndDuration = m_stats.m_sndDurationTotal = 0;
    CGuard::leaveCS(m_StatsLock);

    // Resetting these data because this happens when agent isn't connected.
    m_bPeerTsbPd         = false;
    m_iPeerTsbPdDelay_ms = 0;

    m_bTsbPd         = m_bOPT_TsbPd; // Take the values from user-configurable options
    m_iTsbPdDelay_ms = m_iOPT_TsbPdDelay;
    m_bTLPktDrop     = m_bOPT_TLPktDrop;
    m_bPeerTLPktDrop = false;

    m_bPeerNakReport = false;

    m_bPeerRexmitFlag = false;

    m_RdvState            = CHandShake::RDV_INVALID;
    m_ullRcvPeerStartTime = 0;
}

void CUDT::open()
{
    CGuard cg(m_ConnectionLock);

    clearData();

    // structures for queue
    if (m_pSNode == NULL)
        m_pSNode = new CSNode;
    m_pSNode->m_pUDT           = this;
    m_pSNode->m_llTimeStamp_tk = 1;
    m_pSNode->m_iHeapLoc       = -1;

    if (m_pRNode == NULL)
        m_pRNode = new CRNode;
    m_pRNode->m_pUDT           = this;
    m_pRNode->m_llTimeStamp_tk = 1;
    m_pRNode->m_pPrev = m_pRNode->m_pNext = NULL;
    m_pRNode->m_bOnList                   = false;

    m_iRTT            = 10 * COMM_SYN_INTERVAL_US;
    m_iRTTVar         = m_iRTT >> 1;
    m_ullCPUFrequency = CTimer::getCPUFrequency();

    // set minimum NAK and EXP timeout to 300ms
    /*
       XXX This code is blocked because the value of
       m_ullMinNakInt_tk will be overwritten again in setupCC.
       And in setupCC it will have an opportunity to make the
       value overridden according to the statements in the SrtCongestion.

 #ifdef SRT_ENABLE_NAKREPORT
    if (m_bRcvNakReport)
       m_ullMinNakInt_tk = m_iMinNakInterval_us * m_ullCPUFrequency;
    else
 #endif
 */
    // Set up timers
    m_ullMinNakInt_tk = 300000 * m_ullCPUFrequency;
    m_ullMinExpInt_tk = 300000 * m_ullCPUFrequency;

    m_ullACKInt_tk = COMM_SYN_INTERVAL_US * m_ullCPUFrequency;
    m_ullNAKInt_tk = m_ullMinNakInt_tk;

    uint64_t currtime_tk;
    CTimer::rdtsc(currtime_tk);
    m_ullLastRspTime_tk    = currtime_tk;
    m_ullNextACKTime_tk    = currtime_tk + m_ullACKInt_tk;
    m_ullNextNAKTime_tk    = currtime_tk + m_ullNAKInt_tk;
    m_ullLastRspAckTime_tk = currtime_tk;
    m_ullLastSndTime_tk    = currtime_tk;
    m_iReXmitCount         = 1;

    m_iPktCount      = 0;
    m_iLightACKCount = 1;

    m_ullTargetTime_tk = 0;
    m_ullTimeDiff_tk   = 0;

    // Now UDT is opened.
    m_bOpened = true;
}

void CUDT::setListenState()
{
    CGuard cg(m_ConnectionLock);

    if (!m_bOpened)
        throw CUDTException(MJ_NOTSUP, MN_NONE, 0);

    if (m_bConnecting || m_bConnected)
        throw CUDTException(MJ_NOTSUP, MN_ISCONNECTED, 0);

    // listen can be called more than once
    if (m_bListening)
        return;

    // if there is already another socket listening on the same port
    if (m_pRcvQueue->setListener(this) < 0)
        throw CUDTException(MJ_NOTSUP, MN_BUSY, 0);

    m_bListening = true;
}

size_t CUDT::fillSrtHandshake(uint32_t *srtdata, size_t srtlen, int msgtype, int hs_version)
{
    if (srtlen < SRT_HS__SIZE)
    {
        LOGC(mglog.Fatal,
             log << "IPE: fillSrtHandshake: buffer too small: " << srtlen << " (expected: " << SRT_HS__SIZE << ")");
        return 0;
    }

    srtlen = SRT_HS__SIZE; // We use only that much space.

    memset(srtdata, 0, sizeof(uint32_t) * srtlen);
    /* Current version (1.x.x) SRT handshake */
    srtdata[SRT_HS_VERSION] = m_lSrtVersion; /* Required version */
    srtdata[SRT_HS_FLAGS] |= SrtVersionCapabilities();

    switch (msgtype)
    {
    case SRT_CMD_HSREQ:
        return fillSrtHandshake_HSREQ(srtdata, srtlen, hs_version);
    case SRT_CMD_HSRSP:
        return fillSrtHandshake_HSRSP(srtdata, srtlen, hs_version);
    default:
        LOGC(mglog.Fatal, log << "IPE: createSrtHandshake/sendSrtMsg called with value " << msgtype);
        return 0;
    }
}

size_t CUDT::fillSrtHandshake_HSREQ(uint32_t *srtdata, size_t /* srtlen - unused */, int hs_version)
{
    // INITIATOR sends HSREQ.

    // The TSBPD(SND|RCV) options are being set only if the TSBPD is set in the current agent.
    // The agent has a decisive power only in the range of RECEIVING the data, however it can
    // also influence the peer's latency. If agent doesn't set TSBPD mode, it doesn't send any
    // latency flags, although the peer might still want to do Rx with TSBPD. When agent sets
    // TsbPd mode, it defines latency values for Rx (itself) and Tx (peer's Rx). If peer does
    // not set TsbPd mode, it will simply ignore the proposed latency (PeerTsbPdDelay), although
    // if it has received the Rx latency as well, it must honor it and respond accordingly
    // (the latter is only in case of HSv5 and bidirectional connection).
    if (m_bOPT_TsbPd)
    {
        m_iTsbPdDelay_ms     = m_iOPT_TsbPdDelay;
        m_iPeerTsbPdDelay_ms = m_iOPT_PeerTsbPdDelay;
        /*
         * Sent data is real-time, use Time-based Packet Delivery,
         * set option bit and configured delay
         */
        srtdata[SRT_HS_FLAGS] |= SRT_OPT_TSBPDSND;

        if (hs_version < CUDT::HS_VERSION_SRT1)
        {
            // HSv4 - this uses only one value.
            srtdata[SRT_HS_LATENCY] = SRT_HS_LATENCY_LEG::wrap(m_iPeerTsbPdDelay_ms);
        }
        else
        {
            // HSv5 - this will be understood only since this version when this exists.
            srtdata[SRT_HS_LATENCY] = SRT_HS_LATENCY_SND::wrap(m_iPeerTsbPdDelay_ms);

            m_bTsbPd = true;
            // And in the reverse direction.
            srtdata[SRT_HS_FLAGS] |= SRT_OPT_TSBPDRCV;
            srtdata[SRT_HS_LATENCY] |= SRT_HS_LATENCY_RCV::wrap(m_iTsbPdDelay_ms);

            // This wasn't there for HSv4, this setting is only for the receiver.
            // HSv5 is bidirectional, so every party is a receiver.

            if (m_bTLPktDrop)
                srtdata[SRT_HS_FLAGS] |= SRT_OPT_TLPKTDROP;
        }
    }

    if (m_bRcvNakReport)
        srtdata[SRT_HS_FLAGS] |= SRT_OPT_NAKREPORT;

    // I support SRT_OPT_REXMITFLG. Do you?
    srtdata[SRT_HS_FLAGS] |= SRT_OPT_REXMITFLG;

    // Declare the API used. The flag is set for "stream" API because
    // the older versions will never set this flag, but all old SRT versions use message API.
    if (!m_bMessageAPI)
        srtdata[SRT_HS_FLAGS] |= SRT_OPT_STREAM;

    HLOGC(mglog.Debug,
          log << "HSREQ/snd: LATENCY[SND:" << SRT_HS_LATENCY_SND::unwrap(srtdata[SRT_HS_LATENCY])
              << " RCV:" << SRT_HS_LATENCY_RCV::unwrap(srtdata[SRT_HS_LATENCY]) << "] FLAGS["
              << SrtFlagString(srtdata[SRT_HS_FLAGS]) << "]");

    return 3;
}

size_t CUDT::fillSrtHandshake_HSRSP(uint32_t *srtdata, size_t /* srtlen - unused */, int hs_version)
{
    // Setting m_ullRcvPeerStartTime is done in processSrtMsg_HSREQ(), so
    // this condition will be skipped only if this function is called without
    // getting first received HSREQ. Doesn't look possible in both HSv4 and HSv5.
    if (m_ullRcvPeerStartTime != 0)
    {
        // If Agent doesn't set TSBPD, it will not set the TSBPD flag back to the Peer.
        // The peer doesn't have be disturbed by it anyway.
        if (m_bTsbPd)
        {
            /*
             * We got and transposed peer start time (HandShake request timestamp),
             * we can support Timestamp-based Packet Delivery
             */
            srtdata[SRT_HS_FLAGS] |= SRT_OPT_TSBPDRCV;

            if (hs_version < HS_VERSION_SRT1)
            {
                // HSv4 - this uses only one value
                srtdata[SRT_HS_LATENCY] = SRT_HS_LATENCY_LEG::wrap(m_iTsbPdDelay_ms);
            }
            else
            {
                // HSv5 - this puts "agent's" latency into RCV field and "peer's" -
                // into SND field.
                srtdata[SRT_HS_LATENCY] = SRT_HS_LATENCY_RCV::wrap(m_iTsbPdDelay_ms);
            }
        }
        else
        {
            HLOGC(mglog.Debug, log << "HSRSP/snd: TSBPD off, NOT responding TSBPDRCV flag.");
        }

        // Hsv5, only when peer has declared TSBPD mode.
        // The flag was already set, and the value already "maximized" in processSrtMsg_HSREQ().
        if (m_bPeerTsbPd && hs_version >= HS_VERSION_SRT1)
        {
            // HSv5 is bidirectional - so send the TSBPDSND flag, and place also the
            // peer's latency into SND field.
            srtdata[SRT_HS_FLAGS] |= SRT_OPT_TSBPDSND;
            srtdata[SRT_HS_LATENCY] |= SRT_HS_LATENCY_SND::wrap(m_iPeerTsbPdDelay_ms);

            HLOGC(mglog.Debug,
                  log << "HSRSP/snd: HSv5 peer uses TSBPD, responding TSBPDSND latency=" << m_iPeerTsbPdDelay_ms);
        }
        else
        {
            HLOGC(mglog.Debug,
                  log << "HSRSP/snd: HSv" << (hs_version == CUDT::HS_VERSION_UDT4 ? 4 : 5)
                      << " with peer TSBPD=" << (m_bPeerTsbPd ? "on" : "off") << " - NOT responding TSBPDSND");
        }

        if (m_bTLPktDrop)
            srtdata[SRT_HS_FLAGS] |= SRT_OPT_TLPKTDROP;
    }
    else
    {
        LOGC(mglog.Fatal, log << "IPE: fillSrtHandshake_HSRSP: m_ullRcvPeerStartTime NOT SET!");
        return 0;
    }

    if (m_bRcvNakReport)
    {
        // HSv5: Note that this setting is independent on the value of
        // m_bPeerNakReport, which represent this setting in the peer.

        srtdata[SRT_HS_FLAGS] |= SRT_OPT_NAKREPORT;
        /*
         * NAK Report is so efficient at controlling bandwidth that sender TLPktDrop
         * is not needed. SRT 1.0.5 to 1.0.7 sender TLPktDrop combined with SRT 1.0
         * Timestamp-Based Packet Delivery was not well implemented and could drop
         * big I-Frame tail before sending once on low latency setups.
         * Disabling TLPktDrop in the receiver SRT Handshake Reply prevents the sender
         * from enabling Too-Late Packet Drop.
         */
        if (m_lPeerSrtVersion <= SrtVersion(1, 0, 7))
            srtdata[SRT_HS_FLAGS] &= ~SRT_OPT_TLPKTDROP;
    }

    if (m_lSrtVersion >= SrtVersion(1, 2, 0))
    {
        if (!m_bPeerRexmitFlag)
        {
            // Peer does not request to use rexmit flag, if so,
            // we won't use as well.
            HLOGC(mglog.Debug, log << "HSRSP/snd: AGENT understands REXMIT flag, but PEER DOES NOT. NOT setting.");
        }
        else
        {
            // Request that the rexmit bit be used as a part of msgno.
            srtdata[SRT_HS_FLAGS] |= SRT_OPT_REXMITFLG;
            HLOGF(mglog.Debug, "HSRSP/snd: AGENT UNDERSTANDS REXMIT flag and PEER reported that it does, too.");
        }
    }
    else
    {
        // Since this is now in the code, it can occur only in case when you change the
        // version specification in the build configuration.
        HLOGF(mglog.Debug, "HSRSP/snd: AGENT DOES NOT UNDERSTAND REXMIT flag");
    }

    HLOGC(mglog.Debug,
          log << "HSRSP/snd: LATENCY[SND:" << SRT_HS_LATENCY_SND::unwrap(srtdata[SRT_HS_LATENCY])
              << " RCV:" << SRT_HS_LATENCY_RCV::unwrap(srtdata[SRT_HS_LATENCY]) << "] FLAGS["
              << SrtFlagString(srtdata[SRT_HS_FLAGS]) << "]");

    return 3;
}

size_t CUDT::prepareSrtHsMsg(int cmd, uint32_t *srtdata, size_t size)
{
    size_t srtlen = fillSrtHandshake(srtdata, size, cmd, handshakeVersion());
    HLOGF(mglog.Debug,
          "CMD:%s(%d) Len:%d Version: %s Flags: %08X (%s) sdelay:%d",
          MessageTypeStr(UMSG_EXT, cmd).c_str(),
          cmd,
          (int)(srtlen * sizeof(int32_t)),
          SrtVersionString(srtdata[SRT_HS_VERSION]).c_str(),
          srtdata[SRT_HS_FLAGS],
          SrtFlagString(srtdata[SRT_HS_FLAGS]).c_str(),
          srtdata[SRT_HS_LATENCY]);

    return srtlen;
}

void CUDT::sendSrtMsg(int cmd, uint32_t *srtdata_in, int srtlen_in)
{
    CPacket srtpkt;
    int32_t srtcmd = (int32_t)cmd;

    static const size_t SRTDATA_MAXSIZE = SRT_CMD_MAXSZ / sizeof(int32_t);

    // This is in order to issue a compile error if the SRT_CMD_MAXSZ is
    // too small to keep all the data. As this is "static const", declaring
    // an array of such specified size in C++ isn't considered VLA.
    static const int SRTDATA_SIZE = SRTDATA_MAXSIZE >= SRT_HS__SIZE ? SRTDATA_MAXSIZE : -1;

    // This will be effectively larger than SRT_HS__SIZE, but it will be also used
    // for incoming data. We have a guarantee that it won't be larger than SRTDATA_MAXSIZE.
    uint32_t srtdata[SRTDATA_SIZE];

    int srtlen = 0;

    if (cmd == SRT_CMD_REJECT)
    {
        // This is a value returned by processSrtMsg underlying layer, potentially
        // to be reported here. Should this happen, just send a rejection message.
        cmd                     = SRT_CMD_HSRSP;
        srtdata[SRT_HS_VERSION] = 0;
    }

    switch (cmd)
    {
    case SRT_CMD_HSREQ:
    case SRT_CMD_HSRSP:
        srtlen = prepareSrtHsMsg(cmd, srtdata, SRTDATA_SIZE);
        break;

    case SRT_CMD_KMREQ: // Sender
    case SRT_CMD_KMRSP: // Receiver
        srtlen = srtlen_in;
        /* Msg already in network order
         * But CChannel:sendto will swap again (assuming 32-bit fields)
         * Pre-swap to cancel it.
         */
        HtoNLA(srtdata, srtdata_in, srtlen);
        m_pCryptoControl->updateKmState(cmd, srtlen); // <-- THIS function can't be moved to CUDT

        break;

    default:
        LOGF(mglog.Error, "sndSrtMsg: cmd=%d unsupported", cmd);
        break;
    }

    if (srtlen > 0)
    {
        /* srtpkt.pack will set message data in network order */
        srtpkt.pack(UMSG_EXT, &srtcmd, srtdata, srtlen * sizeof(int32_t));
        addressAndSend(srtpkt);
    }
}

// PREREQUISITE:
// pkt must be set the buffer and configured for UMSG_HANDSHAKE.
// Note that this function replaces also serialization for the HSv4.
bool CUDT::createSrtHandshake(ref_t<CPacket>    r_pkt,
                              ref_t<CHandShake> r_hs,
                              int               srths_cmd,
                              int               srtkm_cmd,
                              const uint32_t *  kmdata,
                              size_t            kmdata_wordsize /* IN WORDS, NOT BYTES!!! */)
{
    CPacket &   pkt = *r_pkt;
    CHandShake &hs  = *r_hs;

    // This function might be called before the opposite version was recognized.
    // Check if the version is exactly 4 because this means that the peer has already
    // sent something - asynchronously, and usually in rendezvous - and we already know
    // that the peer is version 4. In this case, agent must behave as HSv4, til the end.
    if (m_ConnRes.m_iVersion == HS_VERSION_UDT4)
    {
        hs.m_iVersion = HS_VERSION_UDT4;
        hs.m_iType    = UDT_DGRAM;
        if (hs.m_extension)
        {
            // Should be impossible
            LOGC(mglog.Error, log << "createSrtHandshake: IPE: EXTENSION SET WHEN peer reports version 4 - fixing...");
            hs.m_extension = false;
        }
    }
    else
    {
        hs.m_iType = 0; // Prepare it for flags
    }

    HLOGC(mglog.Debug,
          log << "createSrtHandshake: buf size=" << pkt.getLength() << " hsx=" << MessageTypeStr(UMSG_EXT, srths_cmd)
              << " kmx=" << MessageTypeStr(UMSG_EXT, srtkm_cmd) << " kmdata_wordsize=" << kmdata_wordsize
              << " version=" << hs.m_iVersion);

    // Once you are certain that the version is HSv5, set the enc type flags
    // to advertise pbkeylen. Otherwise make sure that the old interpretation
    // will correctly pick up the type field. PBKEYLEN should be advertized
    // regardless of what URQ stage the handshake is (note that in case of rendezvous
    // CONCLUSION might be the FIRST MESSAGE EVER RECEIVED by a party).
    if (hs.m_iVersion > HS_VERSION_UDT4)
    {
        // Check if there was a failure to receie HSREQ before trying to craft HSRSP.
        // If fillSrtHandshake_HSRSP catches the condition of m_ullRcvPeerStartTime == 0,
        // it will return size 0, which will mess up with further extension procedures;
        // PREVENT THIS HERE.
        if (hs.m_iReqType == URQ_CONCLUSION && srths_cmd == SRT_CMD_HSRSP && m_ullRcvPeerStartTime == 0)
        {
            LOGC(mglog.Error,
                 log << "createSrtHandshake: IPE (non-fatal): Attempting to craft HSRSP without received HSREQ. "
                        "BLOCKING extensions.");
            hs.m_extension = false;
        }

        // The situation when this function is called without requested extensions
        // is URQ_CONCLUSION in rendezvous mode in some of the transitions.
        // In this case for version 5 just clear the m_iType field, as it has
        // different meaning in HSv5 and contains extension flags.
        //
        // Keep 0 in the SRT_HSTYPE_HSFLAGS field, but still advertise PBKEYLEN
        // in the SRT_HSTYPE_ENCFLAGS field.
        hs.m_iType                  = SrtHSRequest::wrapFlags(false /*no magic in HSFLAGS*/, m_iSndCryptoKeyLen);
        bool whether SRT_ATR_UNUSED = m_iSndCryptoKeyLen != 0;
        HLOGC(mglog.Debug,
              log << "createSrtHandshake: " << (whether ? "" : "NOT ")
                  << " Advertising PBKEYLEN - value = " << m_iSndCryptoKeyLen);

        // Note: This is required only when sending a HS message without SRT extensions.
        // When this is to be sent with SRT extensions, then KMREQ will be attached here
        // and the PBKEYLEN will be extracted from it. If this is going to attach KMRSP
        // here, it's already too late (it should've been advertised before getting the first
        // handshake message with KMREQ).
    }
    else
    {
        hs.m_iType = UDT_DGRAM;
    }

    // values > URQ_CONCLUSION include also error types
    // if (hs.m_iVersion == HS_VERSION_UDT4 || hs.m_iReqType > URQ_CONCLUSION) <--- This condition was checked b4 and
    // it's only valid for caller-listener mode
    if (!hs.m_extension)
    {
        // Serialize only the basic handshake, if this is predicted for
        // Hsv4 peer or this is URQ_INDUCTION or URQ_WAVEAHAND.
        size_t hs_size = pkt.getLength();
        hs.store_to(pkt.m_pcData, Ref(hs_size));
        pkt.setLength(hs_size);
        HLOGC(mglog.Debug, log << "createSrtHandshake: (no ext) size=" << hs_size << " data: " << hs.show());
        return true;
    }

    // Sanity check, applies to HSv5 only cases.
    if (srths_cmd == SRT_CMD_HSREQ && m_SrtHsSide == HSD_RESPONDER)
    {
        m_RejectReason = SRT_REJ_IPE;
        LOGC(mglog.Fatal, log << "IPE: SRT_CMD_HSREQ was requested to be sent in HSv5 by an INITIATOR side!");
        return false; // should cause rejection
    }

    string logext = "HSX";

    bool have_kmreq   = false;
    bool have_sid     = false;
    bool have_congctl = false;
    bool have_filter  = false;

    // Install the SRT extensions
    hs.m_iType |= CHandShake::HS_EXT_HSREQ;

    if (srths_cmd == SRT_CMD_HSREQ)
    {
        if (m_sStreamName != "")
        {
            have_sid = true;
            hs.m_iType |= CHandShake::HS_EXT_CONFIG;
            logext += ",SID";
        }
    }

    // If this is a response, we have also information
    // on the peer. If Peer is NOT filter capable, don't
    // put filter config, even if agent is capable.
    bool peer_filter_capable = true;
    if (srths_cmd == SRT_CMD_HSRSP)
    {
        if (m_sPeerPktFilterConfigString != "")
        {
            peer_filter_capable = true;
        }
        else if (IsSet(m_lPeerSrtFlags, SRT_OPT_FILTERCAP))
        {
            peer_filter_capable = true;
        }
        else
        {
            peer_filter_capable = false;
        }
    }

    // Now, if this is INITIATOR, then it has its
    // filter config already set, if configured, otherwise
    // it should not attach the filter config extension.

    // If this is a RESPONDER, then it has already received
    // the filter config string from the peer and therefore
    // possibly confronted with the contents of m_OPT_FECConfigString,
    // and if it decided to go with filter, it will be nonempty.
    if (peer_filter_capable && m_OPT_PktFilterConfigString != "")
    {
        have_filter = true;
        hs.m_iType |= CHandShake::HS_EXT_CONFIG;
        logext += ",filter";
    }

    string sm = m_CongCtl.selected_name();
    if (sm != "" && sm != "live")
    {
        have_congctl = true;
        hs.m_iType |= CHandShake::HS_EXT_CONFIG;
        logext += ",CONGCTL";
    }

    // Prevent adding KMRSP only in case when BOTH:
    // - Agent has set no password
    // - no KMREQ has arrived from Peer
    // KMRSP must be always sent when:
    // - Agent set a password, Peer did not send KMREQ: Agent sets snd=NOSECRET.
    // - Agent set no password, but Peer sent KMREQ: Ageng sets rcv=NOSECRET.
    if (m_CryptoSecret.len > 0 || kmdata_wordsize > 0)
    {
        have_kmreq = true;
        hs.m_iType |= CHandShake::HS_EXT_KMREQ;
        logext += ",KMX";
    }

    HLOGC(mglog.Debug, log << "createSrtHandshake: (ext: " << logext << ") data: " << hs.show());

    // NOTE: The HSREQ is practically always required, although may happen
    // in future that CONCLUSION can be sent multiple times for a separate
    // stream encryption support, and this way it won't enclose HSREQ.
    // Also, KMREQ may occur multiple times.

    // So, initially store the UDT legacy handshake.
    size_t hs_size = pkt.getLength(), total_ra_size = (hs_size / sizeof(uint32_t)); // Maximum size of data
    hs.store_to(pkt.m_pcData, Ref(hs_size));                                        // hs_size is updated

    size_t ra_size = hs_size / sizeof(int32_t);

    // Now attach the SRT handshake for HSREQ
    size_t    offset = ra_size;
    uint32_t *p      = reinterpret_cast<uint32_t *>(pkt.m_pcData);
    // NOTE: since this point, ra_size has a size in int32_t elements, NOT BYTES.

    // The first 4-byte item is the CMD/LENGTH spec.
    uint32_t *pcmdspec = p + offset; // Remember the location to be filled later, when we know the length
    ++offset;

    // Now use the original function to store the actual SRT_HS data
    // ra_size after that
    // NOTE: so far, ra_size is m_iMaxSRTPayloadSize expressed in number of elements.
    // WILL BE CHANGED HERE.
    ra_size   = fillSrtHandshake(p + offset, total_ra_size - offset, srths_cmd, HS_VERSION_SRT1);
    *pcmdspec = HS_CMDSPEC_CMD::wrap(srths_cmd) | HS_CMDSPEC_SIZE::wrap(ra_size);

    HLOGC(mglog.Debug,
          log << "createSrtHandshake: after HSREQ: offset=" << offset << " HSREQ size=" << ra_size
              << " space left: " << (total_ra_size - offset));

    if (have_sid)
    {
        // Use only in REQ phase and only if stream name is set
        offset += ra_size;
        pcmdspec = p + offset;
        ++offset;

        // Now prepare the string with 4-byte alignment. The string size is limited
        // to half the payload size. Just a sanity check to not pack too much into
        // the conclusion packet.
        size_t size_limit = m_iMaxSRTPayloadSize / 2;

        if (m_sStreamName.size() >= size_limit)
        {
            m_RejectReason = SRT_REJ_ROGUE;
            LOGC(mglog.Error,
                 log << "createSrtHandshake: stream id too long, limited to " << (size_limit - 1) << " bytes");
            return false;
        }

        size_t wordsize         = (m_sStreamName.size() + 3) / 4;
        size_t aligned_bytesize = wordsize * 4;

        memset(p + offset, 0, aligned_bytesize);
        memcpy(p + offset, m_sStreamName.data(), m_sStreamName.size());
        // Preswap to little endian (in place due to possible padding zeros)
        HtoILA((uint32_t *)(p + offset), (uint32_t *)(p + offset), wordsize);

        ra_size   = wordsize;
        *pcmdspec = HS_CMDSPEC_CMD::wrap(SRT_CMD_SID) | HS_CMDSPEC_SIZE::wrap(ra_size);

        HLOGC(mglog.Debug,
              log << "createSrtHandshake: after SID [" << m_sStreamName << "] length=" << m_sStreamName.size()
                  << " alignedln=" << aligned_bytesize << ": offset=" << offset << " SID size=" << ra_size
                  << " space left: " << (total_ra_size - offset));
    }

    if (have_congctl)
    {
        // Pass the congctl to the other side as informational.
        // The other side should reject connection if it uses a different congctl.
        // The other side should also respond with the congctl it uses, if its non-default (for backward compatibility).

        // XXX Consider change the congctl settings in the listener socket to "adaptive"
        // congctl and also "adaptive" value of CUDT::m_bMessageAPI so that the caller
        // may ask for whatever kind of transmission it wants, or select transmission
        // type differently for different connections, however with the same listener.

        offset += ra_size;
        pcmdspec = p + offset;
        ++offset;

        size_t wordsize         = (sm.size() + 3) / 4;
        size_t aligned_bytesize = wordsize * 4;

        memset(p + offset, 0, aligned_bytesize);

        memcpy(p + offset, sm.data(), sm.size());
        // Preswap to little endian (in place due to possible padding zeros)
        HtoILA((uint32_t *)(p + offset), (uint32_t *)(p + offset), wordsize);

        ra_size   = wordsize;
        *pcmdspec = HS_CMDSPEC_CMD::wrap(SRT_CMD_CONGESTION) | HS_CMDSPEC_SIZE::wrap(ra_size);

        HLOGC(mglog.Debug,
              log << "createSrtHandshake: after CONGCTL [" << sm << "] length=" << sm.size()
                  << " alignedln=" << aligned_bytesize << ": offset=" << offset << " CONGCTL size=" << ra_size
                  << " space left: " << (total_ra_size - offset));
    }

    if (have_filter)
    {
        offset += ra_size;
        pcmdspec = p + offset;
        ++offset;

        size_t wordsize         = (m_OPT_PktFilterConfigString.size() + 3) / 4;
        size_t aligned_bytesize = wordsize * 4;

        memset(p + offset, 0, aligned_bytesize);
        memcpy(p + offset, m_OPT_PktFilterConfigString.data(), m_OPT_PktFilterConfigString.size());

        ra_size   = wordsize;
        *pcmdspec = HS_CMDSPEC_CMD::wrap(SRT_CMD_FILTER) | HS_CMDSPEC_SIZE::wrap(ra_size);

        HLOGC(mglog.Debug,
              log << "createSrtHandshake: after filter [" << m_OPT_PktFilterConfigString << "] length="
                  << m_OPT_PktFilterConfigString.size() << " alignedln=" << aligned_bytesize << ": offset=" << offset
                  << " filter size=" << ra_size << " space left: " << (total_ra_size - offset));
    }

    // When encryption turned on
    if (have_kmreq)
    {
        HLOGC(mglog.Debug,
              log << "createSrtHandshake: "
                  << (m_CryptoSecret.len > 0 ? "Agent uses ENCRYPTION" : "Peer requires ENCRYPTION"));
        if (srtkm_cmd == SRT_CMD_KMREQ)
        {
            bool have_any_keys = false;
            for (size_t ki = 0; ki < 2; ++ki)
            {
                // Skip those that have expired
                if (!m_pCryptoControl->getKmMsg_needSend(ki, false))
                    continue;

                m_pCryptoControl->getKmMsg_markSent(ki, false);

                offset += ra_size;

                size_t msglen = m_pCryptoControl->getKmMsg_size(ki);
                // Make ra_size back in element unit
                // Add one extra word if the size isn't aligned to 32-bit.
                ra_size = (msglen / sizeof(uint32_t)) + (msglen % sizeof(uint32_t) ? 1 : 0);

                // Store the CMD + SIZE in the next field
                *(p + offset) = HS_CMDSPEC_CMD::wrap(srtkm_cmd) | HS_CMDSPEC_SIZE::wrap(ra_size);
                ++offset;

                // Copy the key - do the endian inversion because another endian inversion
                // will be done for every control message before sending, and this KM message
                // is ALREADY in network order.
                const uint32_t *keydata = reinterpret_cast<const uint32_t *>(m_pCryptoControl->getKmMsg_data(ki));

                HLOGC(mglog.Debug,
                      log << "createSrtHandshake: KMREQ: adding key #" << ki << " length=" << ra_size
                          << " words (KmMsg_size=" << msglen << ")");
                // XXX INSECURE ": [" << FormatBinaryString((uint8_t*)keydata, msglen) << "]";

                // Yes, I know HtoNLA and NtoHLA do exactly the same operation, but I want
                // to be clear about the true intention.
                NtoHLA(p + offset, keydata, ra_size);
                have_any_keys = true;
            }

            if (!have_any_keys)
            {
                m_RejectReason = SRT_REJ_IPE;
                LOGC(mglog.Error, log << "createSrtHandshake: IPE: all keys have expired, no KM to send.");
                return false;
            }
        }
        else if (srtkm_cmd == SRT_CMD_KMRSP)
        {
            uint32_t        failure_kmrsp[] = {SRT_KM_S_UNSECURED};
            const uint32_t *keydata         = 0;

            // Shift the starting point with the value of previously added block,
            // to start with the new one.
            offset += ra_size;

            if (kmdata_wordsize == 0)
            {
                LOGC(mglog.Error,
                     log << "createSrtHandshake: Agent has PW, but Peer sent no KMREQ. Sending error KMRSP response");
                ra_size = 1;
                keydata = failure_kmrsp;

                // Update the KM state as well
                m_pCryptoControl->m_SndKmState = SRT_KM_S_NOSECRET;  // Agent has PW, but Peer won't decrypt
                m_pCryptoControl->m_RcvKmState = SRT_KM_S_UNSECURED; // Peer won't encrypt as well.
            }
            else
            {
                if (!kmdata)
                {
                    m_RejectReason = SRT_REJ_IPE;
                    LOGC(mglog.Fatal, log << "createSrtHandshake: IPE: srtkm_cmd=SRT_CMD_KMRSP and no kmdata!");
                    return false;
                }
                ra_size = kmdata_wordsize;
                keydata = reinterpret_cast<const uint32_t *>(kmdata);
            }

            *(p + offset) = HS_CMDSPEC_CMD::wrap(srtkm_cmd) | HS_CMDSPEC_SIZE::wrap(ra_size);
            ++offset; // Once cell, containting CMD spec and size
            HLOGC(mglog.Debug,
                  log << "createSrtHandshake: KMRSP: applying returned key length="
                      << ra_size); // XXX INSECURE << " words: [" << FormatBinaryString((uint8_t*)kmdata,
                                   // kmdata_wordsize*sizeof(uint32_t)) << "]";

            NtoHLA(p + offset, keydata, ra_size);
        }
        else
        {
            m_RejectReason = SRT_REJ_IPE;
            LOGC(mglog.Fatal, log << "createSrtHandshake: IPE: wrong value of srtkm_cmd: " << srtkm_cmd);
            return false;
        }
    }

    // ra_size + offset has a value in element unit.
    // Switch it again to byte unit.
    pkt.setLength((ra_size + offset) * sizeof(int32_t));

    HLOGC(mglog.Debug,
          log << "createSrtHandshake: filled HSv5 handshake flags: " << CHandShake::ExtensionFlagStr(hs.m_iType)
              << " length: " << pkt.getLength() << " bytes");

    return true;
}

static int
FindExtensionBlock(uint32_t *begin, size_t total_length, ref_t<size_t> r_out_len, ref_t<uint32_t *> r_next_block)
{
    // Check if there's anything to process
    if (total_length == 0)
    {
        *r_next_block = NULL;
        *r_out_len    = 0;
        return SRT_CMD_NONE;
    }

    size_t &   out_len    = *r_out_len;
    uint32_t *&next_block = *r_next_block;
    // This function extracts the block command from the block and its length.
    // The command value is returned as a function result.
    // The size of that command block is stored into out_len.
    // The beginning of the prospective next block is stored in next_block.

    // The caller must be aware that:
    // - exactly one element holds the block header (cmd+size), so the actual data are after this one.
    // - the returned size is the number of uint32_t elements since that first data element
    // - the remaining size should be manually calculated as total_length - 1 - out_len, or
    // simply, as next_block - begin.

    // Note that if the total_length is too short to extract the whole block, it will return
    // SRT_CMD_NONE. Note that total_length includes this first CMDSPEC word.
    //
    // When SRT_CMD_NONE is returned, it means that nothing has been extracted and nothing else
    // can be further extracted from this block.

    int    cmd  = HS_CMDSPEC_CMD::unwrap(*begin);
    size_t size = HS_CMDSPEC_SIZE::unwrap(*begin);

    if (size + 1 > total_length)
        return SRT_CMD_NONE;

    out_len = size;

    if (total_length == size + 1)
        next_block = NULL;
    else
        next_block = begin + 1 + size;

    return cmd;
}

static inline bool NextExtensionBlock(ref_t<uint32_t *> begin, uint32_t *next, ref_t<size_t> length)
{
    if (!next)
        return false;

    *length = *length - (next - *begin);
    *begin  = next;
    return true;
}

bool CUDT::processSrtMsg(const CPacket *ctrlpkt)
{
    uint32_t *srtdata = (uint32_t *)ctrlpkt->m_pcData;
    size_t    len     = ctrlpkt->getLength();
    int       etype   = ctrlpkt->getExtendedType();
    uint32_t  ts      = ctrlpkt->m_iTimeStamp;

    int res = SRT_CMD_NONE;

    HLOGC(mglog.Debug, log << "Dispatching message type=" << etype << " data length=" << (len / sizeof(int32_t)));
    switch (etype)
    {
    case SRT_CMD_HSREQ:
    {
        res = processSrtMsg_HSREQ(srtdata, len, ts, CUDT::HS_VERSION_UDT4);
        break;
    }
    case SRT_CMD_HSRSP:
    {
        res = processSrtMsg_HSRSP(srtdata, len, ts, CUDT::HS_VERSION_UDT4);
        break;
    }
    case SRT_CMD_KMREQ:
        // Special case when the data need to be processed here
        // and the appropriate message must be constructed for sending.
        // No further processing required
        {
            uint32_t srtdata_out[SRTDATA_MAXSIZE];
            size_t   len_out = 0;
            res = m_pCryptoControl->processSrtMsg_KMREQ(srtdata, len, srtdata_out, Ref(len_out), CUDT::HS_VERSION_UDT4);
            if (res == SRT_CMD_KMRSP)
            {
                if (len_out == 1)
                {
                    if (m_bOPT_StrictEncryption)
                    {
                        LOGC(mglog.Error,
                             log << "KMREQ FAILURE: " << KmStateStr(SRT_KM_STATE(srtdata_out[0]))
                                 << " - rejecting per strict encryption");
                        return false;
                    }
                    HLOGC(mglog.Debug,
                          log << "MKREQ -> KMRSP FAILURE state: " << KmStateStr(SRT_KM_STATE(srtdata_out[0])));
                }
                else
                {
                    HLOGC(mglog.Debug, log << "KMREQ -> requested to send KMRSP length=" << len_out);
                }
                sendSrtMsg(SRT_CMD_KMRSP, srtdata_out, len_out);
            }
            // XXX Dead code. processSrtMsg_KMREQ now doesn't return any other value now.
            // Please review later.
            else
            {
                LOGC(mglog.Error, log << "KMREQ failed to process the request - ignoring");
            }

            return true; // already done what's necessary
        }

    case SRT_CMD_KMRSP:
    {
        // KMRSP doesn't expect any following action
        m_pCryptoControl->processSrtMsg_KMRSP(srtdata, len, CUDT::HS_VERSION_UDT4);
        return true; // nothing to do
    }

    default:
        return false;
    }

    if (res == SRT_CMD_NONE)
        return true;

    // Send the message that the message handler requested.
    sendSrtMsg(res);

    return true;
}

int CUDT::processSrtMsg_HSREQ(const uint32_t *srtdata, size_t len, uint32_t ts, int hsv)
{
    // Set this start time in the beginning, regardless as to whether TSBPD is being
    // used or not. This must be done in the Initiator as well as Responder.

    /*
     * Compute peer StartTime in our time reference
     * This takes time zone, time drift into account.
     * Also includes current packet transit time (rtt/2)
     */
#if 0 // Debug PeerStartTime if not 1st HS packet
    {
        uint64_t oldPeerStartTime = m_ullRcvPeerStartTime;
        m_ullRcvPeerStartTime = CTimer::getTime() - (uint64_t)((uint32_t)ts);
        if (oldPeerStartTime) {
            LOGC(mglog.Note, log << "rcvSrtMsg: 2nd PeerStartTime diff=" <<  
                    (m_ullRcvPeerStartTime - oldPeerStartTime) << " usec");

        }
    }
#else
    m_ullRcvPeerStartTime = CTimer::getTime() - (uint64_t)((uint32_t)ts);
#endif

    // Prepare the initial runtime values of latency basing on the option values.
    // They are going to get the value fixed HERE.
    m_iTsbPdDelay_ms     = m_iOPT_TsbPdDelay;
    m_iPeerTsbPdDelay_ms = m_iOPT_PeerTsbPdDelay;

    if (len < SRT_CMD_HSREQ_MINSZ)
    {
        m_RejectReason = SRT_REJ_ROGUE;
        /* Packet smaller than minimum compatible packet size */
        LOGF(mglog.Error, "HSREQ/rcv: cmd=%d(HSREQ) len=%" PRIzu " invalid", SRT_CMD_HSREQ, len);
        return SRT_CMD_NONE;
    }

    LOGF(mglog.Note,
         "HSREQ/rcv: cmd=%d(HSREQ) len=%" PRIzu " vers=0x%x opts=0x%x delay=%d",
         SRT_CMD_HSREQ,
         len,
         srtdata[SRT_HS_VERSION],
         srtdata[SRT_HS_FLAGS],
         SRT_HS_LATENCY_RCV::unwrap(srtdata[SRT_HS_LATENCY]));

    m_lPeerSrtVersion = srtdata[SRT_HS_VERSION];
    m_lPeerSrtFlags   = srtdata[SRT_HS_FLAGS];

    if (hsv == CUDT::HS_VERSION_UDT4)
    {
        if (m_lPeerSrtVersion >= SRT_VERSION_FEAT_HSv5)
        {
            m_RejectReason = SRT_REJ_ROGUE;
            LOGC(mglog.Error,
                 log << "HSREQ/rcv: With HSv4 version >= " << SrtVersionString(SRT_VERSION_FEAT_HSv5)
                     << " is not acceptable.");
            return SRT_CMD_REJECT;
        }
    }
    else
    {
        if (m_lPeerSrtVersion < SRT_VERSION_FEAT_HSv5)
        {
            m_RejectReason = SRT_REJ_ROGUE;
            LOGC(mglog.Error,
                 log << "HSREQ/rcv: With HSv5 version must be >= " << SrtVersionString(SRT_VERSION_FEAT_HSv5) << " .");
            return SRT_CMD_REJECT;
        }
    }

    // Check also if the version satisfies the minimum required version
    if (m_lPeerSrtVersion < m_lMinimumPeerSrtVersion)
    {
        m_RejectReason = SRT_REJ_VERSION;
        LOGC(mglog.Error,
             log << "HSREQ/rcv: Peer version: " << SrtVersionString(m_lPeerSrtVersion)
                 << " is too old for requested: " << SrtVersionString(m_lMinimumPeerSrtVersion) << " - REJECTING");
        return SRT_CMD_REJECT;
    }

    HLOGC(mglog.Debug,
          log << "HSREQ/rcv: PEER Version: " << SrtVersionString(m_lPeerSrtVersion) << " Flags: " << m_lPeerSrtFlags
              << "(" << SrtFlagString(m_lPeerSrtFlags) << ")");

    m_bPeerRexmitFlag = IsSet(m_lPeerSrtFlags, SRT_OPT_REXMITFLG);
    HLOGF(mglog.Debug, "HSREQ/rcv: peer %s REXMIT flag", m_bPeerRexmitFlag ? "UNDERSTANDS" : "DOES NOT UNDERSTAND");

    // Check if both use the same API type. Reject if not.
    bool peer_message_api = !IsSet(m_lPeerSrtFlags, SRT_OPT_STREAM);
    if (peer_message_api != m_bMessageAPI)
    {
        m_RejectReason = SRT_REJ_MESSAGEAPI;
        LOGC(mglog.Error,
             log << "HSREQ/rcv: Agent uses " << (m_bMessageAPI ? "MESSAGE" : "STREAM") << " API, but the Peer declares "
                 << (peer_message_api ? "MESSAGE" : "STREAM") << " API. Not compatible transmission type, rejecting.");
        return SRT_CMD_REJECT;
    }

    if (len < SRT_HS_LATENCY + 1)
    {
        // 3 is the size when containing VERSION, FLAGS and LATENCY. Less size
        // makes it contain only the first two. Let's make it acceptable, as long
        // as the latency flags aren't set.
        if (IsSet(m_lPeerSrtFlags, SRT_OPT_TSBPDSND) || IsSet(m_lPeerSrtFlags, SRT_OPT_TSBPDRCV))
        {
            m_RejectReason = SRT_REJ_ROGUE;
            LOGC(mglog.Error,
                 log << "HSREQ/rcv: Peer sent only VERSION + FLAGS HSREQ, but TSBPD flags are set. Rejecting.");
            return SRT_CMD_REJECT;
        }

        LOGC(mglog.Warn, log << "HSREQ/rcv: Peer sent only VERSION + FLAGS HSREQ, not getting any TSBPD settings.");
        // Don't process any further settings in this case. Turn off TSBPD, just for a case.
        m_bTsbPd     = false;
        m_bPeerTsbPd = false;
        return SRT_CMD_HSRSP;
    }

    uint32_t latencystr = srtdata[SRT_HS_LATENCY];

    if (IsSet(m_lPeerSrtFlags, SRT_OPT_TSBPDSND))
    {
        // TimeStamp-based Packet Delivery feature enabled
        if (!m_bTsbPd)
        {
            LOGC(mglog.Warn, log << "HSREQ/rcv: Agent did not set rcv-TSBPD - ignoring proposed latency from peer");

            // Note: also don't set the peer TSBPD flag HERE because
            // - in HSv4 it will be a sender, so it doesn't matter anyway
            // - in HSv5 if it's going to receive, the TSBPDRCV flag will define it.
        }
        else
        {
            int peer_decl_latency;
            if (hsv < CUDT::HS_VERSION_SRT1)
            {
                // In HSv4 there is only one value and this is the latency
                // that the sender peer proposes for the agent.
                peer_decl_latency = SRT_HS_LATENCY_LEG::unwrap(latencystr);
            }
            else
            {
                // In HSv5 there are latency declared for sending and receiving separately.

                // SRT_HS_LATENCY_SND is the value that the peer proposes to be the
                // value used by agent when receiving data. We take this as a local latency value.
                peer_decl_latency = SRT_HS_LATENCY_SND::unwrap(srtdata[SRT_HS_LATENCY]);
            }

            // Use the maximum latency out of latency from our settings and the latency
            // "proposed" by the peer.
            int maxdelay = std::max(m_iTsbPdDelay_ms, peer_decl_latency);
            HLOGC(mglog.Debug,
                  log << "HSREQ/rcv: LOCAL/RCV LATENCY: Agent:" << m_iTsbPdDelay_ms << " Peer:" << peer_decl_latency
                      << "  Selecting:" << maxdelay);
            m_iTsbPdDelay_ms = maxdelay;
        }
    }
    else
    {
        std::string how_about_agent = m_bTsbPd ? "BUT AGENT DOES" : "and nor does Agent";
        HLOGC(mglog.Debug, log << "HSREQ/rcv: Peer DOES NOT USE latency for sending - " << how_about_agent);
    }

    // This happens when the HSv5 RESPONDER receives the HSREQ message; it declares
    // that the peer INITIATOR will receive the data and informs about its predefined
    // latency. We need to maximize this with our setting of the peer's latency and
    // record as peer's latency, which will be then sent back with HSRSP.
    if (hsv > CUDT::HS_VERSION_UDT4 && IsSet(m_lPeerSrtFlags, SRT_OPT_TSBPDRCV))
    {
        // So, PEER uses TSBPD, set the flag.
        // NOTE: it doesn't matter, if AGENT uses TSBPD.
        m_bPeerTsbPd = true;

        // SRT_HS_LATENCY_RCV is the value that the peer declares as to be
        // used by it when receiving data. We take this as a peer's value,
        // and select the maximum of this one and our proposed latency for the peer.
        int peer_decl_latency = SRT_HS_LATENCY_RCV::unwrap(latencystr);
        int maxdelay          = std::max(m_iPeerTsbPdDelay_ms, peer_decl_latency);
        HLOGC(mglog.Debug,
              log << "HSREQ/rcv: PEER/RCV LATENCY: Agent:" << m_iPeerTsbPdDelay_ms << " Peer:" << peer_decl_latency
                  << " Selecting:" << maxdelay);
        m_iPeerTsbPdDelay_ms = maxdelay;
    }
    else
    {
        std::string how_about_agent = m_bTsbPd ? "BUT AGENT DOES" : "and nor does Agent";
        HLOGC(mglog.Debug, log << "HSREQ/rcv: Peer DOES NOT USE latency for receiving - " << how_about_agent);
    }

    if (hsv > CUDT::HS_VERSION_UDT4)
    {
        // This is HSv5, do the same things as required for the sending party in HSv4,
        // as in HSv5 this can also be a sender.
        if (IsSet(m_lPeerSrtFlags, SRT_OPT_TLPKTDROP))
        {
            // Too late packets dropping feature supported
            m_bPeerTLPktDrop = true;
        }
        if (IsSet(m_lPeerSrtFlags, SRT_OPT_NAKREPORT))
        {
            // Peer will send Periodic NAK Reports
            m_bPeerNakReport = true;
        }
    }

    return SRT_CMD_HSRSP;
}

int CUDT::processSrtMsg_HSRSP(const uint32_t *srtdata, size_t len, uint32_t ts, int hsv)
{
    // XXX Check for mis-version
    // With HSv4 we accept only version less than 1.2.0
    if (hsv == CUDT::HS_VERSION_UDT4 && srtdata[SRT_HS_VERSION] >= SRT_VERSION_FEAT_HSv5)
    {
        LOGC(mglog.Error, log << "HSRSP/rcv: With HSv4 version >= 1.2.0 is not acceptable.");
        return SRT_CMD_NONE;
    }

    if (len < SRT_CMD_HSRSP_MINSZ)
    {
        /* Packet smaller than minimum compatible packet size */
        LOGF(mglog.Error, "HSRSP/rcv: cmd=%d(HSRSP) len=%" PRIzu " invalid", SRT_CMD_HSRSP, len);
        return SRT_CMD_NONE;
    }

    // Set this start time in the beginning, regardless as to whether TSBPD is being
    // used or not. This must be done in the Initiator as well as Responder. In case when
    // agent is sender only (HSv4) this value simply won't be used.

    /*
     * Compute peer StartTime in our time reference
     * This takes time zone, time drift into account.
     * Also includes current packet transit time (rtt/2)
     */
#if 0 // Debug PeerStartTime if not 1st HS packet
    {
        uint64_t oldPeerStartTime = m_ullRcvPeerStartTime;
        m_ullRcvPeerStartTime = CTimer::getTime() - (uint64_t)((uint32_t)ts);
        if (oldPeerStartTime) {
            LOGC(mglog.Note, log << "rcvSrtMsg: 2nd PeerStartTime diff=" <<  
                    (m_ullRcvPeerStartTime - oldPeerStartTime) << " usec");

        }
    }
#else
    m_ullRcvPeerStartTime = CTimer::getTime() - (uint64_t)((uint32_t)ts);
#endif

    m_lPeerSrtVersion = srtdata[SRT_HS_VERSION];
    m_lPeerSrtFlags   = srtdata[SRT_HS_FLAGS];

    HLOGF(mglog.Debug,
          "HSRSP/rcv: Version: %s Flags: SND:%08X (%s)",
          SrtVersionString(m_lPeerSrtVersion).c_str(),
          m_lPeerSrtFlags,
          SrtFlagString(m_lPeerSrtFlags).c_str());

    if (hsv == CUDT::HS_VERSION_UDT4)
    {
        // The old HSv4 way: extract just one value and put it under peer.
        if (IsSet(m_lPeerSrtFlags, SRT_OPT_TSBPDRCV))
        {
            // TsbPd feature enabled
            m_bPeerTsbPd         = true;
            m_iPeerTsbPdDelay_ms = SRT_HS_LATENCY_LEG::unwrap(srtdata[SRT_HS_LATENCY]);
            HLOGC(mglog.Debug,
                  log << "HSRSP/rcv: LATENCY: Peer/snd:" << m_iPeerTsbPdDelay_ms
                      << " (Agent: declared:" << m_iTsbPdDelay_ms << " rcv:" << m_iTsbPdDelay_ms << ")");
        }
        // TSBPDSND isn't set in HSv4 by the RESPONDER, because HSv4 RESPONDER is always RECEIVER.
    }
    else
    {
        // HSv5 way: extract the receiver latency and sender latency, if used.

        if (IsSet(m_lPeerSrtFlags, SRT_OPT_TSBPDRCV))
        {
            // TsbPd feature enabled
            m_bPeerTsbPd         = true;
            m_iPeerTsbPdDelay_ms = SRT_HS_LATENCY_RCV::unwrap(srtdata[SRT_HS_LATENCY]);
            HLOGC(mglog.Debug, log << "HSRSP/rcv: LATENCY: Peer/snd:" << m_iPeerTsbPdDelay_ms << "ms");
        }
        else
        {
            HLOGC(mglog.Debug, log << "HSRSP/rcv: Peer (responder) DOES NOT USE latency");
        }

        if (IsSet(m_lPeerSrtFlags, SRT_OPT_TSBPDSND))
        {
            if (!m_bTsbPd)
            {
                LOGC(mglog.Warn,
                     log << "HSRSP/rcv: BUG? Peer (responder) declares sending latency, but Agent turned off TSBPD.");
            }
            else
            {
                // Take this value as a good deal. In case when the Peer did not "correct" the latency
                // because it has TSBPD turned off, just stay with the present value defined in options.
                m_iTsbPdDelay_ms = SRT_HS_LATENCY_SND::unwrap(srtdata[SRT_HS_LATENCY]);
                HLOGC(mglog.Debug, log << "HSRSP/rcv: LATENCY Agent/rcv: " << m_iTsbPdDelay_ms << "ms");
            }
        }
    }

    if ((m_lSrtVersion >= SrtVersion(1, 0, 5)) && IsSet(m_lPeerSrtFlags, SRT_OPT_TLPKTDROP))
    {
        // Too late packets dropping feature supported
        m_bPeerTLPktDrop = true;
    }

    if ((m_lSrtVersion >= SrtVersion(1, 1, 0)) && IsSet(m_lPeerSrtFlags, SRT_OPT_NAKREPORT))
    {
        // Peer will send Periodic NAK Reports
        m_bPeerNakReport = true;
    }

    if (m_lSrtVersion >= SrtVersion(1, 2, 0))
    {
        if (IsSet(m_lPeerSrtFlags, SRT_OPT_REXMITFLG))
        {
            // Peer will use REXMIT flag in packet retransmission.
            m_bPeerRexmitFlag = true;
            HLOGP(mglog.Debug, "HSRSP/rcv: 1.2.0+ Agent understands REXMIT flag and so does peer.");
        }
        else
        {
            HLOGP(mglog.Debug, "HSRSP/rcv: Agent understands REXMIT flag, but PEER DOES NOT");
        }
    }
    else
    {
        HLOGF(mglog.Debug, "HSRSP/rcv: <1.2.0 Agent DOESN'T understand REXMIT flag");
    }

    handshakeDone();

    return SRT_CMD_NONE;
}

// This function is called only when the URQ_CONCLUSION handshake has been received from the peer.
bool CUDT::interpretSrtHandshake(const CHandShake &hs,
                                 const CPacket &   hspkt,
                                 uint32_t *out_data SRT_ATR_UNUSED,
                                 size_t *           out_len)
{
    // Initialize out_len to 0 to handle the unencrypted case
    if (out_len)
        *out_len = 0;

    // The version=0 statement as rejection is used only since HSv5.
    // The HSv4 sends the AGREEMENT handshake message with version=0, do not misinterpret it.
    if (m_ConnRes.m_iVersion > HS_VERSION_UDT4 && hs.m_iVersion == 0)
    {
        m_RejectReason = SRT_REJ_PEER;
        LOGC(mglog.Error, log << "HS VERSION = 0, meaning the handshake has been rejected.");
        return false;
    }

    if (hs.m_iVersion < HS_VERSION_SRT1)
        return true; // do nothing

    // Anyway, check if the handshake contains any extra data.
    if (hspkt.getLength() <= CHandShake::m_iContentSize)
    {
        m_RejectReason = SRT_REJ_ROGUE;
        // This would mean that the handshake was at least HSv5, but somehow no extras were added.
        // Dismiss it then, however this has to be logged.
        LOGC(mglog.Error, log << "HS VERSION=" << hs.m_iVersion << " but no handshake extension found!");
        return false;
    }

    // We still believe it should work, let's check the flags.
    int ext_flags = SrtHSRequest::SRT_HSTYPE_HSFLAGS::unwrap(hs.m_iType);
    if (ext_flags == 0)
    {
        m_RejectReason = SRT_REJ_ROGUE;
        LOGC(mglog.Error, log << "HS VERSION=" << hs.m_iVersion << " but no handshake extension flags are set!");
        return false;
    }

    HLOGC(mglog.Debug,
          log << "HS VERSION=" << hs.m_iVersion << " EXTENSIONS: " << CHandShake::ExtensionFlagStr(ext_flags));

    // Ok, now find the beginning of an int32_t array that follows the UDT handshake.
    uint32_t *p    = reinterpret_cast<uint32_t *>(hspkt.m_pcData + CHandShake::m_iContentSize);
    size_t    size = hspkt.getLength() - CHandShake::m_iContentSize; // Due to previous cond check we grant it's >0

    if (IsSet(ext_flags, CHandShake::HS_EXT_HSREQ))
    {
        HLOGC(mglog.Debug, log << "interpretSrtHandshake: extracting HSREQ/RSP type extension");
        uint32_t *begin    = p;
        uint32_t *next     = 0;
        size_t    length   = size / sizeof(uint32_t);
        size_t    blocklen = 0;

        for (;;) // this is ONE SHOT LOOP
        {
            int cmd = FindExtensionBlock(begin, length, Ref(blocklen), Ref(next));

            size_t bytelen = blocklen * sizeof(uint32_t);

            if (cmd == SRT_CMD_HSREQ)
            {
                // Set is the size as it should, then give it for interpretation for
                // the proper function.
                if (blocklen < SRT_HS__SIZE)
                {
                    m_RejectReason = SRT_REJ_ROGUE;
                    LOGC(mglog.Error,
                         log << "HS-ext HSREQ found but invalid size: " << bytelen << " (expected: " << SRT_HS__SIZE
                             << ")");
                    return false; // don't interpret
                }

                int rescmd = processSrtMsg_HSREQ(begin + 1, bytelen, hspkt.m_iTimeStamp, HS_VERSION_SRT1);
                // Interpreted? Then it should be responded with SRT_CMD_HSRSP.
                if (rescmd != SRT_CMD_HSRSP)
                {
                    // m_RejectReason already set
                    LOGC(mglog.Error,
                         log << "interpretSrtHandshake: process HSREQ returned unexpected value " << rescmd);
                    return false;
                }
                handshakeDone();
                updateAfterSrtHandshake(SRT_CMD_HSREQ, HS_VERSION_SRT1);
            }
            else if (cmd == SRT_CMD_HSRSP)
            {
                // Set is the size as it should, then give it for interpretation for
                // the proper function.
                if (blocklen < SRT_HS__SIZE)
                {
                    m_RejectReason = SRT_REJ_ROGUE;
                    LOGC(mglog.Error,
                         log << "HS-ext HSRSP found but invalid size: " << bytelen << " (expected: " << SRT_HS__SIZE
                             << ")");

                    return false; // don't interpret
                }

                int rescmd = processSrtMsg_HSRSP(begin + 1, bytelen, hspkt.m_iTimeStamp, HS_VERSION_SRT1);
                // Interpreted? Then it should be responded with SRT_CMD_NONE.
                // (nothing to be responded for HSRSP, unless there was some kinda problem)
                if (rescmd != SRT_CMD_NONE)
                {
                    // Just formally; the current code doesn't seem to return anything else.
                    m_RejectReason = SRT_REJ_ROGUE;
                    LOGC(mglog.Error,
                         log << "interpretSrtHandshake: process HSRSP returned unexpected value " << rescmd);
                    return false;
                }
                handshakeDone();
                updateAfterSrtHandshake(SRT_CMD_HSRSP, HS_VERSION_SRT1);
            }
            else if (cmd == SRT_CMD_NONE)
            {
                m_RejectReason = SRT_REJ_ROGUE;
                LOGC(mglog.Error, log << "interpretSrtHandshake: no HSREQ/HSRSP block found in the handshake msg!");
                // This means that there can be no more processing done by FindExtensionBlock().
                // And we haven't found what we need - otherwise one of the above cases would pass
                // and lead to exit this loop immediately.
                return false;
            }
            else
            {
                // Any other kind of message extracted. Search on.
                length -= (next - begin);
                begin = next;
                if (begin)
                    continue;
            }

            break;
        }
    }

    HLOGC(mglog.Debug, log << "interpretSrtHandshake: HSREQ done, checking KMREQ");

    // Now check the encrypted

    bool encrypted = false;

    if (IsSet(ext_flags, CHandShake::HS_EXT_KMREQ))
    {
        HLOGC(mglog.Debug, log << "interpretSrtHandshake: extracting KMREQ/RSP type extension");

#ifdef SRT_ENABLE_ENCRYPTION
        if (!m_pCryptoControl->hasPassphrase())
        {
            if (m_bOPT_StrictEncryption)
            {
                m_RejectReason = SRT_REJ_UNSECURE;
                LOGC(
                    mglog.Error,
                    log << "HS KMREQ: Peer declares encryption, but agent does not - rejecting per strict requirement");
                return false;
            }

            LOGC(mglog.Error,
                 log << "HS KMREQ: Peer declares encryption, but agent does not - still allowing connection.");

            // Still allow for connection, and allow Agent to send unencrypted stream to the peer.
            // Also normally allow the key to be processed; worst case it will send the failure response.
        }

        uint32_t *begin    = p;
        uint32_t *next     = 0;
        size_t    length   = size / sizeof(uint32_t);
        size_t    blocklen = 0;

        for (;;) // This is one shot loop, unless REPEATED by 'continue'.
        {
            int cmd = FindExtensionBlock(begin, length, Ref(blocklen), Ref(next));

            HLOGC(mglog.Debug,
                  log << "interpretSrtHandshake: found extension: (" << cmd << ") " << MessageTypeStr(UMSG_EXT, cmd));

            size_t bytelen = blocklen * sizeof(uint32_t);
            if (cmd == SRT_CMD_KMREQ)
            {
                if (!out_data || !out_len)
                {
                    m_RejectReason = SRT_REJ_IPE;
                    LOGC(mglog.Fatal, log << "IPE: HS/KMREQ extracted without passing target buffer!");
                    return false;
                }

                int res =
                    m_pCryptoControl->processSrtMsg_KMREQ(begin + 1, bytelen, out_data, Ref(*out_len), HS_VERSION_SRT1);
                if (res != SRT_CMD_KMRSP)
                {
                    m_RejectReason = SRT_REJ_IPE;
                    // Something went wrong.
                    HLOGC(mglog.Debug,
                          log << "interpretSrtHandshake: IPE/EPE KMREQ processing failed - returned " << res);
                    return false;
                }
                if (*out_len == 1)
                {
                    // This means that there was an abnormal encryption situation occurred.
                    // This is inacceptable in case of strict encryption.
                    if (m_bOPT_StrictEncryption)
                    {
                        if (m_pCryptoControl->m_RcvKmState == SRT_KM_S_BADSECRET)
                        {
                            m_RejectReason = SRT_REJ_BADSECRET;
                        }
                        else
                        {
                            m_RejectReason = SRT_REJ_UNSECURE;
                        }
                        LOGC(mglog.Error,
                             log << "interpretSrtHandshake: KMREQ result abnornal - rejecting per strict encryption");
                        return false;
                    }
                }
                encrypted = true;
            }
            else if (cmd == SRT_CMD_KMRSP)
            {
                int res = m_pCryptoControl->processSrtMsg_KMRSP(begin + 1, bytelen, HS_VERSION_SRT1);
                if (m_bOPT_StrictEncryption && res == -1)
                {
                    m_RejectReason = SRT_REJ_UNSECURE;
                    LOGC(mglog.Error, log << "KMRSP failed - rejecting connection as per strict encryption.");
                    return false;
                }
                encrypted = true;
            }
            else if (cmd == SRT_CMD_NONE)
            {
                m_RejectReason = SRT_REJ_ROGUE;
                LOGC(mglog.Error, log << "HS KMREQ expected - none found!");
                return false;
            }
            else
            {
                HLOGC(mglog.Debug, log << "interpretSrtHandshake: ... skipping " << MessageTypeStr(UMSG_EXT, cmd));
                if (NextExtensionBlock(Ref(begin), next, Ref(length)))
                    continue;
            }

            break;
        }
#else
        // When encryption is not enabled at compile time, behave as if encryption wasn't set,
        // so accordingly to StrictEncryption flag.

        if (m_bOPT_StrictEncryption)
        {
            m_RejectReason = SRT_REJ_UNSECURE;
            LOGC(mglog.Error,
                 log << "HS KMREQ: Peer declares encryption, but agent didn't enable it at compile time - rejecting "
                        "per strict requirement");
            return false;
        }

        LOGC(mglog.Error,
             log << "HS KMREQ: Peer declares encryption, but agent didn't enable it at compile time - still allowing "
                    "connection.");
        encrypted = true;
#endif
    }

    bool   have_congctl = false;
    bool   have_filter  = false;
    string agsm         = m_CongCtl.selected_name();
    if (agsm == "")
    {
        agsm = "live";
        m_CongCtl.select("live");
    }

    if (IsSet(ext_flags, CHandShake::HS_EXT_CONFIG))
    {
        HLOGC(mglog.Debug, log << "interpretSrtHandshake: extracting various CONFIG extensions");

        uint32_t *begin    = p;
        uint32_t *next     = 0;
        size_t    length   = size / sizeof(uint32_t);
        size_t    blocklen = 0;

        for (;;) // This is one shot loop, unless REPEATED by 'continue'.
        {
            int cmd = FindExtensionBlock(begin, length, Ref(blocklen), Ref(next));

            HLOGC(mglog.Debug,
                  log << "interpretSrtHandshake: found extension: (" << cmd << ") " << MessageTypeStr(UMSG_EXT, cmd));

            const size_t bytelen = blocklen * sizeof(uint32_t);
            if (cmd == SRT_CMD_SID)
            {
                if (!bytelen || bytelen > MAX_SID_LENGTH)
                {
                    LOGC(mglog.Error,
                         log << "interpretSrtHandshake: STREAMID length " << bytelen << " is 0 or > " << +MAX_SID_LENGTH
                             << " - PROTOCOL ERROR, REJECTING");
                    return false;
                }
                // Copied through a cleared array. This is because the length is aligned to 4
                // where the padding is filled by zero bytes. For the case when the string is
                // exactly of a 4-divisible length, we make a big array with maximum allowed size
                // filled with zeros. Copying to this array should then copy either only the valid
                // characters of the string (if the lenght is divisible by 4), or the string with
                // padding zeros. In all these cases in the resulting array we should have all
                // subsequent characters of the string plus at least one '\0' at the end. This will
                // make it a perfect NUL-terminated string, to be used to initialize a string.
                char target[MAX_SID_LENGTH + 1];
                memset(target, 0, MAX_SID_LENGTH + 1);
                memcpy(target, begin + 1, bytelen);

                // Un-swap on big endian machines
                ItoHLA((uint32_t *)target, (uint32_t *)target, blocklen);

                m_sStreamName = target;
                HLOGC(mglog.Debug,
                      log << "CONNECTOR'S REQUESTED SID [" << m_sStreamName << "] (bytelen=" << bytelen
                          << " blocklen=" << blocklen << ")");
            }
            else if (cmd == SRT_CMD_CONGESTION)
            {
                if (have_congctl)
                {
                    m_RejectReason = SRT_REJ_ROGUE;
                    LOGC(mglog.Error, log << "CONGCTL BLOCK REPEATED!");
                    return false;
                }

                if (!bytelen || bytelen > MAX_SID_LENGTH)
                {
                    LOGC(mglog.Error,
                         log << "interpretSrtHandshake: CONGESTION-control type length " << bytelen << " is 0 or > "
                             << +MAX_SID_LENGTH << " - PROTOCOL ERROR, REJECTING");
                    return false;
                }
                // Declare that congctl has been received
                have_congctl = true;

                char target[MAX_SID_LENGTH + 1];
                memset(target, 0, MAX_SID_LENGTH + 1);
                memcpy(target, begin + 1, bytelen);
                // Un-swap on big endian machines
                ItoHLA((uint32_t *)target, (uint32_t *)target, blocklen);

                string sm = target;

                // As the congctl has been declared by the peer,
                // check if your congctl is compatible.
                // sm cannot be empty, but the agent's sm can be empty meaning live.
                if (sm != agsm)
                {
                    m_RejectReason = SRT_REJ_CONGESTION;
                    LOGC(mglog.Error,
                         log << "PEER'S CONGCTL '" << sm << "' does not match AGENT'S CONGCTL '" << agsm << "'");
                    return false;
                }

                HLOGC(mglog.Debug,
                      log << "CONNECTOR'S CONGCTL [" << sm << "] (bytelen=" << bytelen << " blocklen=" << blocklen
                          << ")");
            }
            else if (cmd == SRT_CMD_FILTER)
            {
                if (have_filter)
                {
                    m_RejectReason = SRT_REJ_FILTER;
                    LOGC(mglog.Error, log << "FILTER BLOCK REPEATED!");
                    return false;
                }
                // Declare that filter has been received
                have_filter = true;

                // XXX This is the maximum string, but filter config
                // shall be normally limited somehow, especially if used
                // together with SID!
                char target[MAX_SID_LENGTH + 1];
                memset(target, 0, MAX_SID_LENGTH + 1);
                memcpy(target, begin + 1, bytelen);
                string fltcfg = target;

                HLOGC(mglog.Debug,
                      log << "PEER'S FILTER CONFIG [" << fltcfg << "] (bytelen=" << bytelen << " blocklen=" << blocklen
                          << ")");

                if (!checkApplyFilterConfig(fltcfg))
                {
                    LOGC(mglog.Error, log << "PEER'S FILTER CONFIG [" << fltcfg << "] has been rejected");
                    return false;
                }
            }
            else if (cmd == SRT_CMD_NONE)
            {
                break;
            }
            else
            {
                // Found some block that is not interesting here. Skip this and get the next one.
                HLOGC(mglog.Debug, log << "interpretSrtHandshake: ... skipping " << MessageTypeStr(UMSG_EXT, cmd));
            }

            if (!NextExtensionBlock(Ref(begin), next, Ref(length)))
                break;
        }
    }

    // Post-checks
    // Check if peer declared encryption
    if (!encrypted && m_CryptoSecret.len > 0)
    {
        if (m_bOPT_StrictEncryption)
        {
            m_RejectReason = SRT_REJ_UNSECURE;
            LOGC(mglog.Error,
                 log << "HS EXT: Agent declares encryption, but Peer does not - rejecting connection per strict "
                        "requirement.");
            return false;
        }

        LOGC(mglog.Error,
             log << "HS EXT: Agent declares encryption, but Peer does not (Agent can still receive unencrypted packets "
                    "from Peer).");

        // This is required so that the sender is still allowed to send data, when encryption is required,
        // just this will be for waste because the receiver won't decrypt them anyway.
        m_pCryptoControl->createFakeSndContext();
        m_pCryptoControl->m_SndKmState = SRT_KM_S_NOSECRET;  // Because Peer did not send KMX, though Agent has pw
        m_pCryptoControl->m_RcvKmState = SRT_KM_S_UNSECURED; // Because Peer has no PW, as has sent no KMREQ.
        return true;
    }

    // If agent has set some nondefault congctl, then congctl is expected from the peer.
    if (agsm != "live" && !have_congctl)
    {
        m_RejectReason = SRT_REJ_CONGESTION;
        LOGC(mglog.Error,
             log << "HS EXT: Agent uses '" << agsm << "' congctl, but peer DID NOT DECLARE congctl (assuming 'live').");
        return false;
    }

    // Ok, finished, for now.
    return true;
}

bool CUDT::checkApplyFilterConfig(const std::string &confstr)
{
    SrtFilterConfig cfg;
    if (!ParseFilterConfig(confstr, cfg))
        return false;

    // Now extract the type, if present, and
    // check if you have this type of corrector available.
    if (!PacketFilter::correctConfig(cfg))
        return false;

    // Now parse your own string, if you have it.
    if (m_OPT_PktFilterConfigString != "")
    {
        // - for rendezvous, both must be exactly the same, or only one side specified.
        if (m_bRendezvous && m_OPT_PktFilterConfigString != confstr)
        {
            return false;
        }

        SrtFilterConfig mycfg;
        if (!ParseFilterConfig(m_OPT_PktFilterConfigString, mycfg))
            return false;

        // Check only if both have set a filter of the same type.
        if (mycfg.type != cfg.type)
            return false;

        // If so, then:
        // - for caller-listener configuration, accept the listener version.
        if (m_SrtHsSide == HSD_INITIATOR)
        {
            // This is a caller, this should apply all parameters received
            // from the listener, forcefully.
            for (map<string, string>::iterator x = cfg.parameters.begin(); x != cfg.parameters.end(); ++x)
            {
                mycfg.parameters[x->first] = x->second;
            }
        }
        else
        {
            // On a listener, only apply those that you haven't set
            for (map<string, string>::iterator x = cfg.parameters.begin(); x != cfg.parameters.end(); ++x)
            {
                if (!mycfg.parameters.count(x->first))
                    mycfg.parameters[x->first] = x->second;
            }
        }

        HLOGC(mglog.Debug,
              log << "checkApplyFilterConfig: param: LOCAL: " << Printable(mycfg.parameters)
                  << " FORGN: " << Printable(cfg.parameters));

        ostringstream myos;
        myos << mycfg.type;
        for (map<string, string>::iterator x = mycfg.parameters.begin(); x != mycfg.parameters.end(); ++x)
        {
            myos << "," << x->first << ":" << x->second;
        }

        m_OPT_PktFilterConfigString = myos.str();

        HLOGC(mglog.Debug, log << "checkApplyFilterConfig: Effective config: " << m_OPT_PktFilterConfigString);
    }
    else
    {
        // Take the foreign configuration as a good deal.
        HLOGC(mglog.Debug, log << "checkApplyFilterConfig: Good deal config: " << m_OPT_PktFilterConfigString);
        m_OPT_PktFilterConfigString = confstr;
    }

    size_t efc_max_payload_size = SRT_LIVE_MAX_PLSIZE - cfg.extra_size;
    if (m_zOPT_ExpPayloadSize > efc_max_payload_size)
    {
        LOGC(mglog.Warn,
             log << "Due to filter-required extra " << cfg.extra_size << " bytes, SRTO_PAYLOADSIZE fixed to "
                 << efc_max_payload_size << " bytes");
        m_zOPT_ExpPayloadSize = efc_max_payload_size;
    }

    return true;
}

void CUDT::startConnect(const sockaddr *serv_addr, int32_t forced_isn)
{
    CGuard cg(m_ConnectionLock);

    HLOGC(mglog.Debug, log << "startConnect: -> " << SockaddrToString(serv_addr) << "...");

    if (!m_bOpened)
        throw CUDTException(MJ_NOTSUP, MN_NONE, 0);

    if (m_bListening)
        throw CUDTException(MJ_NOTSUP, MN_ISCONNECTED, 0);

    if (m_bConnecting || m_bConnected)
        throw CUDTException(MJ_NOTSUP, MN_ISCONNECTED, 0);

    // record peer/server address
    delete m_pPeerAddr;
    m_pPeerAddr = (AF_INET == m_iIPversion) ? (sockaddr *)new sockaddr_in : (sockaddr *)new sockaddr_in6;
    memcpy(m_pPeerAddr, serv_addr, (AF_INET == m_iIPversion) ? sizeof(sockaddr_in) : sizeof(sockaddr_in6));

    // register this socket in the rendezvous queue
    // RendezevousQueue is used to temporarily store incoming handshake, non-rendezvous connections also require this
    // function
#ifdef SRT_ENABLE_CONNTIMEO
    uint64_t ttl = m_iConnTimeOut * uint64_t(1000);
#else
    uint64_t ttl = 3000000;
#endif
    // XXX DEBUG
    // ttl = 0x1000000000000000;
    // XXX
    if (m_bRendezvous)
        ttl *= 10;
    ttl += CTimer::getTime();
    m_pRcvQueue->registerConnector(m_SocketID, this, m_iIPversion, serv_addr, ttl);

    // The m_iType is used in the INDUCTION for nothing. This value is only regarded
    // in CONCLUSION handshake, however this must be created after the handshake version
    // is already known. UDT_DGRAM is the value that was the only valid in the old SRT
    // with HSv4 (it supported only live transmission), for HSv5 it will be changed to
    // handle handshake extension flags.
    m_ConnReq.m_iType = UDT_DGRAM;

    // This is my current configuration
    if (m_bRendezvous)
    {
        // For rendezvous, use version 5 in the waveahand and the cookie.
        // In case when you get the version 4 waveahand, simply switch to
        // the legacy HSv4 rendezvous and this time send version 4 CONCLUSION.

        // The HSv4 client simply won't check the version nor the cookie and it
        // will be sending its waveahands with version 4. Only when the party
        // has sent version 5 waveahand should the agent continue with HSv5
        // rendezvous.
        m_ConnReq.m_iVersion = HS_VERSION_SRT1;
        // m_ConnReq.m_iVersion = HS_VERSION_UDT4; // <--- Change in order to do regression test.
        m_ConnReq.m_iReqType = URQ_WAVEAHAND;
        m_ConnReq.m_iCookie  = bake(serv_addr);

        // This will be also passed to a HSv4 rendezvous, but fortunately the old
        // SRT didn't read this field from URQ_WAVEAHAND message, only URQ_CONCLUSION.
        m_ConnReq.m_iType           = SrtHSRequest::wrapFlags(false /* no MAGIC here */, m_iSndCryptoKeyLen);
        bool whether SRT_ATR_UNUSED = m_iSndCryptoKeyLen != 0;
        HLOGC(mglog.Debug,
              log << "startConnect (rnd): " << (whether ? "" : "NOT ")
                  << " Advertising PBKEYLEN - value = " << m_iSndCryptoKeyLen);
        m_RdvState  = CHandShake::RDV_WAVING;
        m_SrtHsSide = HSD_DRAW; // initially not resolved.
    }
    else
    {
        // For caller-listener configuration, set the version 4 for INDUCTION
        // due to a serious problem in UDT code being also in the older SRT versions:
        // the listener peer simply sents the EXACT COPY of the caller's induction
        // handshake, except the cookie, which means that when the caller sents version 5,
        // the listener will respond with version 5, which is a false information. Therefore
        // HSv5 clients MUST send HS_VERSION_UDT4 from the caller, regardless of currently
        // supported handshake version.
        //
        // The HSv5 listener should only respond with INDUCTION with m_iVersion == HS_VERSION_SRT1.
        m_ConnReq.m_iVersion = HS_VERSION_UDT4;
        m_ConnReq.m_iReqType = URQ_INDUCTION;
        m_ConnReq.m_iCookie  = 0;
        m_RdvState           = CHandShake::RDV_INVALID;
    }

    m_ConnReq.m_iMSS            = m_iMSS;
    m_ConnReq.m_iFlightFlagSize = (m_iRcvBufSize < m_iFlightFlagSize) ? m_iRcvBufSize : m_iFlightFlagSize;
    m_ConnReq.m_iID             = m_SocketID;
    CIPAddress::ntop(serv_addr, m_ConnReq.m_piPeerIP, m_iIPversion);

    if (forced_isn == 0)
    {
        // Random Initial Sequence Number (normal mode)
        srand((unsigned int)CTimer::getTime());
        m_iISN = m_ConnReq.m_iISN = (int32_t)(CSeqNo::m_iMaxSeqNo * (double(rand()) / RAND_MAX));
    }
    else
    {
        // Predefined ISN (for debug purposes)
        m_iISN = m_ConnReq.m_iISN = forced_isn;
    }

    m_iLastDecSeq        = m_iISN - 1;
    m_iSndLastAck        = m_iISN;
    m_iSndLastDataAck    = m_iISN;
    m_iSndLastFullAck    = m_iISN;
    m_iSndCurrSeqNo      = m_iISN - 1;
    m_iSndLastAck2       = m_iISN;
    m_ullSndLastAck2Time = CTimer::getTime();

    // Inform the server my configurations.
    CPacket reqpkt;
    reqpkt.setControl(UMSG_HANDSHAKE);
    reqpkt.allocate(m_iMaxSRTPayloadSize);
    // XXX NOTE: Now the memory for the payload part is allocated automatically,
    // and such allocated memory is also automatically deallocated in the
    // destructor. If you use CPacket::allocate, remember that you must not:
    // - delete this memory
    // - assign to m_pcData.
    // If you use only manual assignment to m_pCData, this is then manual
    // allocation and so it won't be deallocated in the destructor.
    //
    // (Desired would be to disallow modification of m_pcData outside the
    // control of methods.)

    // ID = 0, connection request
    reqpkt.m_iID = 0;

    size_t hs_size = m_iMaxSRTPayloadSize;
    m_ConnReq.store_to(reqpkt.m_pcData, Ref(hs_size));

    // Note that CPacket::allocate() sets also the size
    // to the size of the allocated buffer, which not
    // necessarily is to be the size of the data.
    reqpkt.setLength(hs_size);

    uint64_t now        = CTimer::getTime();
    reqpkt.m_iTimeStamp = int32_t(now - m_stats.startTime);

    HLOGC(mglog.Debug,
          log << CONID() << "CUDT::startConnect: REQ-TIME set HIGH (" << now << "). SENDING HS: " << m_ConnReq.show());

    /*
     * Race condition if non-block connect response thread scheduled before we set m_bConnecting to true?
     * Connect response will be ignored and connecting will wait until timeout.
     * Maybe m_ConnectionLock handling problem? Not used in CUDT::connect(const CPacket& response)
     */
    m_llLastReqTime = now;
    m_bConnecting   = true;
    m_pSndQueue->sendto(serv_addr, reqpkt);

    //
    ///
    ////  ---> CONTINUE TO: <PEER>.CUDT::processConnectRequest()
    ///        (Take the part under condition: hs.m_iReqType == URQ_INDUCTION)
    ////  <--- RETURN WHEN: m_pSndQueue->sendto() is called.
    ////  .... SKIP UNTIL m_pRcvQueue->recvfrom() HERE....
    ////       (the first "sendto" will not be called due to being too early)
    ///
    //

    // asynchronous connect, return immediately
    if (!m_bSynRecving)
    {
        HLOGC(mglog.Debug, log << CONID() << "startConnect: ASYNC MODE DETECTED. Deferring the process to RcvQ:worker");
        return;
    }

    // Wait for the negotiated configurations from the peer side.

    // This packet only prepares the storage where we will read the
    // next incoming packet.
    CPacket response;
    response.setControl(UMSG_HANDSHAKE);
    response.allocate(m_iMaxSRTPayloadSize);

    CUDTException  e;
    EConnectStatus cst = CONN_CONTINUE;

    while (!m_bClosing)
    {
        int64_t tdiff = CTimer::getTime() - m_llLastReqTime;
        // avoid sending too many requests, at most 1 request per 250ms

        // SHORT VERSION:
        // The immediate first run of this loop WILL SKIP THIS PART, so
        // the processing really begins AFTER THIS CONDITION.
        //
        // Note that some procedures inside may set m_llLastReqTime to 0,
        // which will result of this condition to trigger immediately in
        // the next iteration.
        if (tdiff > 250000)
        {
            HLOGC(mglog.Debug,
                  log << "startConnect: LOOP: time to send (" << tdiff << " > 250000). size=" << reqpkt.getLength());

            if (m_bRendezvous)
                reqpkt.m_iID = m_ConnRes.m_iID;

            now = CTimer::getTime();
#if ENABLE_HEAVY_LOGGING
            {
                CHandShake debughs;
                debughs.load_from(reqpkt.m_pcData, reqpkt.getLength());
                HLOGC(mglog.Debug,
                      log << CONID() << "startConnect: REQ-TIME HIGH (" << now
                          << "). cont/sending HS to peer: " << debughs.show());
            }
#endif

            m_llLastReqTime     = now;
            reqpkt.m_iTimeStamp = int32_t(now - m_stats.startTime);
            m_pSndQueue->sendto(serv_addr, reqpkt);
        }
        else
        {
            HLOGC(mglog.Debug, log << "startConnect: LOOP: too early to send - " << tdiff << " < 250000");
        }

        cst = CONN_CONTINUE;
        response.setLength(m_iMaxSRTPayloadSize);
        if (m_pRcvQueue->recvfrom(m_SocketID, Ref(response)) > 0)
        {
            HLOGC(mglog.Debug, log << CONID() << "startConnect: got response for connect request");
            cst = processConnectResponse(response, &e, true /*synchro*/);

            HLOGC(mglog.Debug, log << CONID() << "startConnect: response processing result: " << ConnectStatusStr(cst));

            // Expected is that:
            // - the peer responded with URQ_INDUCTION + cookie. This above function
            //   should check that and craft the URQ_CONCLUSION handshake, in which
            //   case this function returns CONN_CONTINUE. As an extra action taken
            //   for that case, we set the SECURING mode if encryption requested,
            //   and serialize again the handshake, possibly together with HS extension
            //   blocks, if HSv5 peer responded. The serialized handshake will be then
            //   sent again, as the loop is repeated.
            // - the peer responded with URQ_CONCLUSION. This handshake was accepted
            //   as a connection, and for >= HSv5 the HS extension blocks have been
            //   also read and interpreted. In this case this function returns:
            //   - CONN_ACCEPT, if everything was correct - break this loop and return normally
            //   - CONN_REJECT in case of any problems with the delivered handshake
            //     (incorrect data or data conflict) - throw error exception
            // - the peer responded with any of URQ_ERROR_*.  - throw error exception
            //
            // The error exception should make the API connect() function fail, if blocking
            // or mark the failure for that socket in epoll, if non-blocking.

            if (cst == CONN_RENDEZVOUS)
            {
                // When this function returned CONN_RENDEZVOUS, this requires
                // very special processing for the Rendezvous-v5 algorithm. This MAY
                // involve also preparing a new handshake form, also interpreting the
                // SRT handshake extension and crafting SRT handshake extension for the
                // peer, which should be next sent. When this function returns CONN_CONTINUE,
                // it means that it has done all that was required, however none of the below
                // things has to be done (this function will do it by itself if needed).
                // Otherwise the handshake rolling can be interrupted and considered complete.
                cst = processRendezvous(Ref(reqpkt), response, serv_addr, true /*synchro*/, RST_OK);
                if (cst == CONN_CONTINUE)
                    continue;
                break;
            }

            if (cst == CONN_REJECT)
                sendCtrl(UMSG_SHUTDOWN);

            if (cst != CONN_CONTINUE && cst != CONN_CONFUSED)
                break; // --> OUTSIDE-LOOP

            // IMPORTANT
            // [[using assert(m_pCryptoControl != nullptr)]];

            // new request/response should be sent out immediately on receving a response
            HLOGC(mglog.Debug,
                  log << "startConnect: SYNC CONNECTION STATUS:" << ConnectStatusStr(cst) << ", REQ-TIME: LOW.");
            m_llLastReqTime = 0;

            // Now serialize the handshake again to the existing buffer so that it's
            // then sent later in this loop.

            // First, set the size back to the original size, m_iMaxSRTPayloadSize because
            // this is the size of the originally allocated space. It might have been
            // shrunk by serializing the INDUCTION handshake (which was required before
            // sending this packet to the output queue) and therefore be too
            // small to store the CONCLUSION handshake (with HSv5 extensions).
            reqpkt.setLength(m_iMaxSRTPayloadSize);

            HLOGC(mglog.Debug, log << "startConnect: creating HS CONCLUSION: buffer size=" << reqpkt.getLength());

            // NOTE: BUGFIX: SERIALIZE AGAIN.
            // The original UDT code didn't do it, so it was theoretically
            // turned into conclusion, but was sending still the original
            // induction handshake challenge message. It was working only
            // thanks to that simultaneously there were being sent handshake
            // messages from a separate thread (CSndQueue::worker) from
            // RendezvousQueue, this time serialized properly, which caused
            // that with blocking mode there was a kinda initial "drunk
            // passenger with taxi driver talk" until the RendezvousQueue sends
            // (when "the time comes") the right CONCLUSION handshake
            // challenge message.
            //
            // Now that this is fixed, the handshake messages from RendezvousQueue
            // are sent only when there is a rendezvous mode or non-blocking mode.
            if (!createSrtHandshake(Ref(reqpkt), Ref(m_ConnReq), SRT_CMD_HSREQ, SRT_CMD_KMREQ, 0, 0))
            {
                LOGC(mglog.Error, log << "createSrtHandshake failed - REJECTING.");
                cst = CONN_REJECT;
                break;
            }
            // These last 2 parameters designate the buffer, which is in use only for SRT_CMD_KMRSP.
            // If m_ConnReq.m_iVersion == HS_VERSION_UDT4, this function will do nothing,
            // except just serializing the UDT handshake.
            // The trick is that the HS challenge is with version HS_VERSION_UDT4, but the
            // listener should respond with HS_VERSION_SRT1, if it is HSv5 capable.
        }

        HLOGC(mglog.Debug,
              log << "startConnect: timeout from Q:recvfrom, looping again; cst=" << ConnectStatusStr(cst));

#if ENABLE_HEAVY_LOGGING
        // Non-fatal assertion
        if (cst == CONN_REJECT) // Might be returned by processRendezvous
        {
            LOGC(mglog.Error,
                 log << "startConnect: IPE: cst=REJECT NOT EXPECTED HERE, the loop should've been interrupted!");
            break;
        }
#endif

        if (CTimer::getTime() > ttl)
        {
            // timeout
            e = CUDTException(MJ_SETUP, MN_TIMEOUT, 0);
            break;
        }
    }

    // <--- OUTSIDE-LOOP
    // Here will fall the break when not CONN_CONTINUE.
    // CONN_RENDEZVOUS is handled by processRendezvous.
    // CONN_ACCEPT will skip this and pass on.
    if (cst == CONN_REJECT)
    {
        e = CUDTException(MJ_SETUP, MN_REJECTED, 0);
    }

    if (e.getErrorCode() == 0)
    {
        if (m_bClosing)                                    // if the socket is closed before connection...
            e = CUDTException(MJ_SETUP);                   // XXX NO MN ?
        else if (m_ConnRes.m_iReqType > URQ_FAILURE_TYPES) // connection request rejected
        {
            m_RejectReason = RejectReasonForURQ(m_ConnRes.m_iReqType);
            e              = CUDTException(MJ_SETUP, MN_REJECTED, 0);
        }
        else if ((!m_bRendezvous) && (m_ConnRes.m_iISN != m_iISN)) // secuity check
            e = CUDTException(MJ_SETUP, MN_SECURITY, 0);
    }

    if (e.getErrorCode() != 0)
    {
        m_bConnecting = false;
        // The process is to be abnormally terminated, remove the connector
        // now because most likely no other processing part has done anything with it.
        m_pRcvQueue->removeConnector(m_SocketID);
        throw e;
    }

    HLOGC(mglog.Debug, log << CONID() << "startConnect: handshake exchange succeeded");

    // Parameters at the end.
    HLOGC(mglog.Debug,
          log << "startConnect: END. Parameters:"
                 " mss="
              << m_iMSS << " max-cwnd-size=" << m_CongCtl->cgWindowMaxSize()
              << " cwnd-size=" << m_CongCtl->cgWindowSize() << " rtt=" << m_iRTT << " bw=" << m_iBandwidth);
}

// Asynchronous connection
EConnectStatus CUDT::processAsyncConnectResponse(const CPacket &pkt) ATR_NOEXCEPT
{
    EConnectStatus cst = CONN_CONTINUE;
    CUDTException  e;

    CGuard cg(m_ConnectionLock); // FIX
    HLOGC(mglog.Debug, log << CONID() << "processAsyncConnectResponse: got response for connect request, processing");
    cst = processConnectResponse(pkt, &e, false);

    HLOGC(mglog.Debug,
          log << CONID() << "processAsyncConnectResponse: response processing result: " << ConnectStatusStr(cst)
              << "REQ-TIME LOW to enforce immediate response");
    m_llLastReqTime = 0;

    return cst;
}

bool CUDT::processAsyncConnectRequest(EReadStatus     rst,
                                      EConnectStatus  cst,
                                      const CPacket & response,
                                      const sockaddr *serv_addr)
{
    // IMPORTANT!

    // This function is called, still asynchronously, but in the order
    // of call just after the call to the above processAsyncConnectResponse.
    // This should have got the original value returned from
    // processConnectResponse through processAsyncConnectResponse.

    CPacket request;
    request.setControl(UMSG_HANDSHAKE);
    request.allocate(m_iMaxSRTPayloadSize);
    uint64_t now         = CTimer::getTime();
    request.m_iTimeStamp = int(now - m_stats.startTime);

    HLOGC(mglog.Debug,
          log << "processAsyncConnectRequest: REQ-TIME: HIGH (" << now << "). Should prevent too quick responses.");
    m_llLastReqTime = now;
    // ID = 0, connection request
    request.m_iID = !m_bRendezvous ? 0 : m_ConnRes.m_iID;

    bool status = true;

    if (cst == CONN_RENDEZVOUS)
    {
        HLOGC(mglog.Debug, log << "processAsyncConnectRequest: passing to processRendezvous");
        cst = processRendezvous(Ref(request), response, serv_addr, false /*asynchro*/, rst);
        if (cst == CONN_ACCEPT)
        {
            HLOGC(mglog.Debug,
                  log << "processAsyncConnectRequest: processRendezvous completed the process and responded by itself. "
                         "Done.");
            return true;
        }

        if (cst != CONN_CONTINUE)
        {
            // processRendezvous already set the reject reason
            LOGC(mglog.Error,
                 log << "processAsyncConnectRequest: REJECT reported from processRendezvous, not processing further.");
            status = false;
        }
    }
    else if (cst == CONN_REJECT)
    {
        // m_RejectReason already set at worker_ProcessAddressedPacket.
        LOGC(mglog.Error,
             log << "processAsyncConnectRequest: REJECT reported from HS processing, not processing further.");
        return false;
    }
    else
    {
        // (this procedure will be also run for HSv4 rendezvous)
        HLOGC(mglog.Debug, log << "processAsyncConnectRequest: serializing HS: buffer size=" << request.getLength());
        if (!createSrtHandshake(Ref(request), Ref(m_ConnReq), SRT_CMD_HSREQ, SRT_CMD_KMREQ, 0, 0))
        {
            // All 'false' returns from here are IPE-type, mostly "invalid argument" plus "all keys expired".
            LOGC(mglog.Error, log << "IPE: processAsyncConnectRequest: createSrtHandshake failed, dismissing.");
            status = false;
        }
        else
        {
            HLOGC(mglog.Debug,
                  log << "processAsyncConnectRequest: sending HS reqtype=" << RequestTypeStr(m_ConnReq.m_iReqType)
                      << " to socket " << request.m_iID << " size=" << request.getLength());
        }
    }

    if (!status)
    {
        return false;
        /* XXX Shouldn't it send a single response packet for the rejection?
        // Set the version to 0 as "handshake rejection" status and serialize it
        CHandShake zhs;
        size_t size = request.getLength();
        zhs.store_to(request.m_pcData, Ref(size));
        request.setLength(size);
        */
    }

    HLOGC(mglog.Debug, log << "processAsyncConnectRequest: sending request packet, setting REQ-TIME HIGH.");
    m_llLastReqTime = CTimer::getTime();
    m_pSndQueue->sendto(serv_addr, request);
    return status;
}

void CUDT::cookieContest()
{
    if (m_SrtHsSide != HSD_DRAW)
        return;

    HLOGC(mglog.Debug, log << "cookieContest: agent=" << m_ConnReq.m_iCookie << " peer=" << m_ConnRes.m_iCookie);

    if (m_ConnReq.m_iCookie == 0 || m_ConnRes.m_iCookie == 0)
    {
        // Note that it's virtually impossible that Agent's cookie is not ready, this
        // shall be considered IPE.
        // Not all cookies are ready, don't start the contest.
        return;
    }

    // INITIATOR/RESPONDER role is resolved by COOKIE CONTEST.
    //
    // The cookie contest must be repeated every time because it
    // may change the state at some point.
    int better_cookie = m_ConnReq.m_iCookie - m_ConnRes.m_iCookie;

    if (better_cookie > 0)
    {
        m_SrtHsSide = HSD_INITIATOR;
        return;
    }

    if (better_cookie < 0)
    {
        m_SrtHsSide = HSD_RESPONDER;
        return;
    }

    // DRAW! The only way to continue would be to force the
    // cookies to be regenerated and to start over. But it's
    // not worth a shot - this is an extremely rare case.
    // This can simply do reject so that it can be started again.

    // Pretend then that the cookie contest wasn't done so that
    // it's done again. Cookies are baked every time anew, however
    // the successful initial contest remains valid no matter how
    // cookies will change.

    m_SrtHsSide = HSD_DRAW;
}

EConnectStatus CUDT::processRendezvous(
    ref_t<CPacket> reqpkt, const CPacket &response, const sockaddr *serv_addr, bool synchro, EReadStatus rst)
{
    if (m_RdvState == CHandShake::RDV_CONNECTED)
    {
        HLOGC(mglog.Debug, log << "processRendezvous: already in CONNECTED state.");
        return CONN_ACCEPT;
    }

    uint32_t kmdata[SRTDATA_MAXSIZE];
    size_t   kmdatasize = SRTDATA_MAXSIZE;
    CPacket &rpkt       = *reqpkt;

    cookieContest();

    // We know that the other side was contacted and the other side has sent
    // the handshake message - we know then both cookies. If it's a draw, it's
    // a very rare case of creating identical cookies.
    if (m_SrtHsSide == HSD_DRAW)
    {
        m_RejectReason = SRT_REJ_RDVCOOKIE;
        LOGC(mglog.Error,
             log << "COOKIE CONTEST UNRESOLVED: can't assign connection roles, please wait another minute.");
        return CONN_REJECT;
    }

    UDTRequestType rsp_type = URQ_FAILURE_TYPES; // just to track uninitialized errors

    // We can assume that the Handshake packet received here as 'response'
    // is already serialized in m_ConnRes. Check extra flags that are meaningful
    // for further processing here.

    int  ext_flags       = SrtHSRequest::SRT_HSTYPE_HSFLAGS::unwrap(m_ConnRes.m_iType);
    bool needs_extension = ext_flags != 0; // Initial value: received HS has extensions.
    bool needs_hsrsp;
    rendezvousSwitchState(Ref(rsp_type), Ref(needs_extension), Ref(needs_hsrsp));
    if (rsp_type > URQ_FAILURE_TYPES)
    {
        m_RejectReason = RejectReasonForURQ(rsp_type);
        HLOGC(mglog.Debug,
              log << "processRendezvous: rejecting due to switch-state response: " << RequestTypeStr(rsp_type));
        return CONN_REJECT;
    }
    checkUpdateCryptoKeyLen("processRendezvous", m_ConnRes.m_iType);

    // We have three possibilities here as it comes to HSREQ extensions:

    // 1. The agent is loser in attention state, it sends EMPTY conclusion (without extensions)
    // 2. The agent is loser in initiated state, it interprets incoming HSREQ and creates HSRSP
    // 3. The agent is winner in attention or fine state, it sends HSREQ extension
    m_ConnReq.m_iReqType  = rsp_type;
    m_ConnReq.m_extension = needs_extension;

    // This must be done before prepareConnectionObjects().
    applyResponseSettings();

    // This must be done before interpreting and creating HSv5 extensions.
    if (!prepareConnectionObjects(m_ConnRes, m_SrtHsSide, 0))
    {
        // m_RejectReason already handled
        HLOGC(mglog.Debug, log << "processRendezvous: rejecting due to problems in prepareConnectionObjects.");
        return CONN_REJECT;
    }

    // Case 2.
    if (needs_hsrsp)
    {
        // This means that we have received HSREQ extension with the handshake, so we need to interpret
        // it and craft the response.
        if (rst == RST_OK)
        {
            // We have JUST RECEIVED packet in this session (not that this is called as periodic update).
            // Sanity check
            m_llLastReqTime = 0;
            if (response.getLength() == size_t(-1))
            {
                m_RejectReason = SRT_REJ_IPE;
                LOGC(mglog.Fatal,
                     log << "IPE: rst=RST_OK, but the packet has set -1 length - REJECTING (REQ-TIME: LOW)");
                return CONN_REJECT;
            }

            if (!interpretSrtHandshake(m_ConnRes, response, kmdata, &kmdatasize))
            {
                HLOGC(mglog.Debug,
                      log << "processRendezvous: rejecting due to problems in interpretSrtHandshake REQ-TIME: LOW.");
                return CONN_REJECT;
            }

            // Pass on, inform about the shortened response-waiting period.
            HLOGC(mglog.Debug, log << "processRendezvous: setting REQ-TIME: LOW. Forced to respond immediately.");
        }
        else
        {
            // If the last CONCLUSION message didn't contain the KMX extension, there's
            // no key recorded yet, so it can't be extracted. Mark this kmdatasize empty though.
            int hs_flags = SrtHSRequest::SRT_HSTYPE_HSFLAGS::unwrap(m_ConnRes.m_iType);
            if (IsSet(hs_flags, CHandShake::HS_EXT_KMREQ))
            {
                // This is a periodic handshake update, so you need to extract the KM data from the
                // first message, provided that it is there.
                size_t msgsize = m_pCryptoControl->getKmMsg_size(0);
                if (msgsize == 0)
                {
                    switch (m_pCryptoControl->m_RcvKmState)
                    {
                        // If the KMX process ended up with a failure, the KMX is not recorded.
                        // In this case as the KMRSP answer the "failure status" should be crafted.
                    case SRT_KM_S_NOSECRET:
                    case SRT_KM_S_BADSECRET:
                    {
                        HLOGC(mglog.Debug,
                              log << "processRendezvous: No KMX recorded, status = NOSECRET. Respond with NOSECRET.");

                        // Just do the same thing as in CCryptoControl::processSrtMsg_KMREQ for that case,
                        // that is, copy the NOSECRET code into KMX message.
                        memcpy(kmdata, &m_pCryptoControl->m_RcvKmState, sizeof(int32_t));
                        kmdatasize = 1;
                    }
                    break;

                    default:
                        // Remaining values:
                        // UNSECURED: should not fall here at alll
                        // SECURING: should not happen in HSv5
                        // SECURED: should have received the recorded KMX correctly (getKmMsg_size(0) > 0)
                        {
                            m_RejectReason = SRT_REJ_IPE;
                            // Remaining situations:
                            // - password only on this site: shouldn't be considered to be sent to a no-password site
                            LOGC(mglog.Error,
                                 log << "processRendezvous: IPE: PERIODIC HS: NO KMREQ RECORDED KMSTATE: RCV="
                                     << KmStateStr(m_pCryptoControl->m_RcvKmState)
                                     << " SND=" << KmStateStr(m_pCryptoControl->m_SndKmState));
                            return CONN_REJECT;
                        }
                        break;
                    }
                }
                else
                {
                    kmdatasize = msgsize / 4;
                    if (msgsize > kmdatasize * 4)
                    {
                        // Sanity check
                        LOGC(mglog.Error, log << "IPE: KMX data not aligned to 4 bytes! size=" << msgsize);
                        memset(kmdata + (kmdatasize * 4), 0, msgsize - (kmdatasize * 4));
                        ++kmdatasize;
                    }

                    HLOGC(mglog.Debug,
                          log << "processRendezvous: getting KM DATA from the fore-recorded KMX from KMREQ, size="
                              << kmdatasize);
                    memcpy(kmdata, m_pCryptoControl->getKmMsg_data(0), msgsize);
                }
            }
            else
            {
                HLOGC(mglog.Debug, log << "processRendezvous: no KMX flag - not extracting KM data for KMRSP");
                kmdatasize = 0;
            }
        }

        // No matter the value of needs_extension, the extension is always needed
        // when HSREQ was interpreted (to store HSRSP extension).
        m_ConnReq.m_extension = true;

        HLOGC(mglog.Debug,
              log << "processRendezvous: HSREQ extension ok, creating HSRSP response. kmdatasize=" << kmdatasize);

        rpkt.setLength(m_iMaxSRTPayloadSize);
        if (!createSrtHandshake(reqpkt, Ref(m_ConnReq), SRT_CMD_HSRSP, SRT_CMD_KMRSP, kmdata, kmdatasize))
        {
            HLOGC(mglog.Debug,
                  log << "processRendezvous: rejecting due to problems in createSrtHandshake. REQ-TIME: LOW");
            m_llLastReqTime = 0;
            return CONN_REJECT;
        }

        // This means that it has received URQ_CONCLUSION with HSREQ, agent is then in RDV_FINE
        // state, it sends here URQ_CONCLUSION with HSREQ/KMREQ extensions and it awaits URQ_AGREEMENT.
        return CONN_CONTINUE;
    }

    // Special case: if URQ_AGREEMENT is to be sent, when this side is INITIATOR,
    // then it must have received HSRSP, so it must interpret it. Otherwise it would
    // end up with URQ_DONE, which means that it is the other side to interpret HSRSP.
    if (m_SrtHsSide == HSD_INITIATOR && m_ConnReq.m_iReqType == URQ_AGREEMENT)
    {
        // The same is done in CUDT::postConnect(), however this section will
        // not be done in case of rendezvous. The section in postConnect() is
        // predicted to run only in regular CALLER handling.

        if (rst != RST_OK || response.getLength() == size_t(-1))
        {
            // Actually the -1 length would be an IPE, but it's likely that this was reported already.
            HLOGC(
                mglog.Debug,
                log << "processRendezvous: no INCOMING packet, NOT interpreting extensions (relying on exising data)");
        }
        else
        {
            HLOGC(mglog.Debug,
                  log << "processRendezvous: INITIATOR, will send AGREEMENT - interpreting HSRSP extension");
            if (!interpretSrtHandshake(m_ConnRes, response, 0, 0))
            {
                // m_RejectReason is already set, so set the reqtype accordingly
                m_ConnReq.m_iReqType = URQFailure(m_RejectReason);
            }
        }
        // This should be false, make a kinda assert here.
        if (needs_extension)
        {
            LOGC(mglog.Fatal, log << "IPE: INITIATOR responding AGREEMENT should declare no extensions to HS");
            m_ConnReq.m_extension = false;
        }
    }

    HLOGC(mglog.Debug,
          log << CONID() << "processRendezvous: COOKIES Agent/Peer: " << m_ConnReq.m_iCookie << "/"
              << m_ConnRes.m_iCookie << " HSD:" << (m_SrtHsSide == HSD_INITIATOR ? "initiator" : "responder")
              << " STATE:" << CHandShake::RdvStateStr(m_RdvState) << " ...");

    if (rsp_type == URQ_DONE)
    {
        HLOGC(mglog.Debug, log << "... WON'T SEND any response, both sides considered connected");
    }
    else
    {
        HLOGC(mglog.Debug,
              log << "... WILL SEND " << RequestTypeStr(rsp_type) << " " << (m_ConnReq.m_extension ? "with" : "without")
                  << " SRT HS extensions");
    }

    // This marks the information for the serializer that
    // the SRT handshake extension is required.
    // Rest of the data will be filled together with
    // serialization.
    m_ConnReq.m_extension = needs_extension;

    rpkt.setLength(m_iMaxSRTPayloadSize);
    if (m_RdvState == CHandShake::RDV_CONNECTED)
    {
        // When synchro=false, don't lock a mutex for rendezvous queue.
        // This is required when this function is called in the
        // receive queue worker thread - it would lock itself.
        int cst = postConnect(response, true, 0, synchro);
        if (cst == CONN_REJECT)
        {
            // m_RejectReason already set
            HLOGC(mglog.Debug, log << "processRendezvous: rejecting due to problems in postConnect.");
            return CONN_REJECT;
        }
    }

    // URQ_DONE or URQ_AGREEMENT can be the result if the state is RDV_CONNECTED.
    // If URQ_DONE, then there's nothing to be done, when URQ_AGREEMENT then return
    // CONN_CONTINUE to make the caller send again the contents if the packet buffer,
    // this time with URQ_AGREEMENT message, but still consider yourself connected.
    if (rsp_type == URQ_DONE)
    {
        HLOGC(mglog.Debug, log << "processRendezvous: rsp=DONE, reporting ACCEPT (nothing to respond)");
        return CONN_ACCEPT;
    }

    // createSrtHandshake moved here because if the above conditions are satisfied,
    // no response is going to be send, so nothing needs to be "created".

    // needs_extension here distinguishes between cases 1 and 3.
    // NOTE: in case when interpretSrtHandshake was run under the conditions above (to interpret HSRSP),
    // then createSrtHandshake below will create only empty AGREEMENT message.
    if (!createSrtHandshake(reqpkt, Ref(m_ConnReq), SRT_CMD_HSREQ, SRT_CMD_KMREQ, 0, 0))
    {
        // m_RejectReason already set
        LOGC(mglog.Error, log << "createSrtHandshake failed (IPE?), connection rejected. REQ-TIME: LOW");
        m_llLastReqTime = 0;
        return CONN_REJECT;
    }

    if (rsp_type == URQ_AGREEMENT && m_RdvState == CHandShake::RDV_CONNECTED)
    {
        // We are using our own serialization method (not the one called after
        // processConnectResponse, this is skipped in case when this function
        // is called), so we can also send this immediately. Agreement must be
        // sent just once and the party must switch into CONNECTED state - in
        // contrast to CONCLUSION messages, which should be sent in loop repeatedly.
        //
        // Even though in theory the AGREEMENT message sent just once may miss
        // the target (as normal thing in UDP), this is little probable to happen,
        // and this doesn't matter much because even if the other party doesn't
        // get AGREEMENT, but will get payload or KEEPALIVE messages, it will
        // turn into connected state as well. The AGREEMENT is rather kinda
        // catalyzer here and may turn the entity on the right track faster. When
        // AGREEMENT is missed, it may have kinda initial tearing.

        const uint64_t now = CTimer::getTime();
        m_llLastReqTime    = now;
        rpkt.m_iTimeStamp  = int32_t(now - m_stats.startTime);
        HLOGC(mglog.Debug,
              log << "processRendezvous: rsp=AGREEMENT, reporting ACCEPT and sending just this one, REQ-TIME HIGH ("
                  << now << ").");

        m_pSndQueue->sendto(serv_addr, rpkt);

        return CONN_ACCEPT;
    }

    if (rst == RST_OK)
    {
        // the request time must be updated so that the next handshake can be sent out immediately
        HLOGC(mglog.Debug,
              log << "processRendezvous: rsp=" << RequestTypeStr(m_ConnReq.m_iReqType)
                  << " REQ-TIME: LOW to send immediately, consider yourself conencted");
        m_llLastReqTime = 0;
    }
    else
    {
        HLOGC(mglog.Debug, log << "processRendezvous: REQ-TIME: remains previous value, consider yourself connected");
    }
    return CONN_CONTINUE;
}

EConnectStatus CUDT::processConnectResponse(const CPacket &response, CUDTException *eout, bool synchro) ATR_NOEXCEPT
{
    // NOTE: ASSUMED LOCK ON: m_ConnectionLock.

    // this is the 2nd half of a connection request. If the connection is setup successfully this returns 0.
    // Returned values:
    // - CONN_REJECT: there was some error when processing the response, connection should be rejected
    // - CONN_ACCEPT: the handshake is done and finished correctly
    // - CONN_CONTINUE: the induction handshake has been processed correctly, and expects CONCLUSION handshake

    if (!m_bConnecting)
        return CONN_REJECT;

    // This is required in HSv5 rendezvous, in which it should send the URQ_AGREEMENT message to
    // the peer, however switch to connected state.
    HLOGC(mglog.Debug,
          log << "processConnectResponse: TYPE:"
              << (response.isControl() ? MessageTypeStr(response.getType(), response.getExtendedType())
                                       : string("DATA")));
    // ConnectStatus res = CONN_REJECT; // used later for status - must be declared here due to goto POST_CONNECT.

    // For HSv4, the data sender is INITIATOR, and the data receiver is RESPONDER,
    // regardless of the connecting side affiliation. This will be changed for HSv5.
    bool          bidirectional = false;
    HandshakeSide hsd           = m_bDataSender ? HSD_INITIATOR : HSD_RESPONDER;
    // (defined here due to 'goto' below).

    // SRT peer may send the SRT handshake private message (type 0x7fff) before a keep-alive.

    // This condition is checked when the current agent is trying to do connect() in rendezvous mode,
    // but the peer was faster to send a handshake packet earlier. This makes it continue with connecting
    // process if the peer is already behaving as if the connection was already established.

    // This value will check either the initial value, which is less than SRT1, or
    // the value previously loaded to m_ConnReq during the previous handshake response.
    // For the initial form this value should not be checked.
    bool hsv5 = m_ConnRes.m_iVersion >= HS_VERSION_SRT1;

    if (m_bRendezvous &&
        (m_RdvState == CHandShake::RDV_CONNECTED   // somehow Rendezvous-v5 switched it to CONNECTED.
         || !response.isControl()                  // WAS A PAYLOAD PACKET.
         || (response.getType() == UMSG_KEEPALIVE) // OR WAS A UMSG_KEEPALIVE message.
         || (response.getType() == UMSG_EXT) // OR WAS a CONTROL packet of some extended type (i.e. any SRT specific)
         )
        // This may happen if this is an initial state in which the socket type was not yet set.
        // If this is a field that holds the response handshake record from the peer, this means that it wasn't received
        // yet. HSv5: added version check because in HSv5 the m_iType field has different meaning and it may be 0 in
        // case when the handshake does not carry SRT extensions.
        && (hsv5 || m_ConnRes.m_iType != UDT_UNDEFINED))
    {
        // a data packet or a keep-alive packet comes, which means the peer side is already connected
        // in this situation, the previously recorded response will be used
        // In HSv5 this situation is theoretically possible if this party has missed the URQ_AGREEMENT message.
        HLOGC(mglog.Debug, log << CONID() << "processConnectResponse: already connected - pinning in");
        if (hsv5)
        {
            m_RdvState = CHandShake::RDV_CONNECTED;
        }

        return postConnect(response, hsv5, eout, synchro);
    }

    if (!response.isControl(UMSG_HANDSHAKE))
    {
        m_RejectReason = SRT_REJ_ROGUE;
        if (!response.isControl())
        {
            LOGC(mglog.Error, log << CONID() << "processConnectResponse: received DATA while HANDSHAKE expected");
        }
        else
        {
            LOGC(mglog.Error,
                 log << CONID()
                     << "processConnectResponse: CONFUSED: expected UMSG_HANDSHAKE as connection not yet established, "
                        "got: "
                     << MessageTypeStr(response.getType(), response.getExtendedType()));
        }
        return CONN_CONFUSED;
    }

    if (m_ConnRes.load_from(response.m_pcData, response.getLength()) == -1)
    {
        m_RejectReason = SRT_REJ_ROGUE;
        // Handshake data were too small to reach the Handshake structure. Reject.
        LOGC(mglog.Error,
             log << CONID()
                 << "processConnectResponse: HANDSHAKE data buffer too small - possible blueboxing. Rejecting.");
        return CONN_REJECT;
    }

    HLOGC(mglog.Debug, log << CONID() << "processConnectResponse: HS RECEIVED: " << m_ConnRes.show());
    if (m_ConnRes.m_iReqType > URQ_FAILURE_TYPES)
    {
        m_RejectReason = RejectReasonForURQ(m_ConnRes.m_iReqType);
        return CONN_REJECT;
    }

    if (size_t(m_ConnRes.m_iMSS) > CPacket::ETH_MAX_MTU_SIZE)
    {
        // Yes, we do abort to prevent buffer overrun. Set your MSS correctly
        // and you'll avoid problems.
        m_RejectReason = SRT_REJ_ROGUE;
        LOGC(mglog.Fatal, log << "MSS size " << m_iMSS << "exceeds MTU size!");
        return CONN_REJECT;
    }

    // (see createCrypter() call below)
    //
    // The CCryptoControl attached object must be created early
    // because it will be required to create a conclusion handshake in HSv5
    //
    if (m_bRendezvous)
    {
        // SANITY CHECK: A rendezvous socket should reject any caller requests (it's not a listener)
        if (m_ConnRes.m_iReqType == URQ_INDUCTION)
        {
            m_RejectReason = SRT_REJ_ROGUE;
            LOGC(mglog.Error,
                 log << CONID()
                     << "processConnectResponse: Rendezvous-point received INDUCTION handshake (expected WAVEAHAND). "
                        "Rejecting.");
            return CONN_REJECT;
        }

        // The procedure for version 5 is completely different and changes the states
        // differently, so the old code will still maintain HSv4 the old way.

        if (m_ConnRes.m_iVersion > HS_VERSION_UDT4)
        {
            HLOGC(mglog.Debug, log << CONID() << "processConnectResponse: Rendezvous HSv5 DETECTED.");
            return CONN_RENDEZVOUS; // --> will continue in CUDT::processRendezvous().
        }

        HLOGC(mglog.Debug, log << CONID() << "processConnectResponse: Rendsezvous HSv4 DETECTED.");
        // So, here it has either received URQ_WAVEAHAND handshake message (while it should be in URQ_WAVEAHAND itself)
        // or it has received URQ_CONCLUSION/URQ_AGREEMENT message while this box has already sent URQ_WAVEAHAND to the
        // peer, and DID NOT send the URQ_CONCLUSION yet.

        if (m_ConnReq.m_iReqType == URQ_WAVEAHAND || m_ConnRes.m_iReqType == URQ_WAVEAHAND)
        {
            HLOGC(mglog.Debug,
                  log << CONID() << "processConnectResponse: REQ-TIME LOW. got HS RDV. Agent state:"
                      << RequestTypeStr(m_ConnReq.m_iReqType) << " Peer HS:" << m_ConnRes.show());

            // Here we could have received WAVEAHAND or CONCLUSION.
            // For HSv4 simply switch to CONCLUSION for the sake of further handshake rolling.
            // For HSv5, make the cookie contest and basing on this decide, which party
            // should provide the HSREQ/KMREQ attachment.

            if (!createCrypter(hsd, false /* unidirectional */))
            {
                m_RejectReason       = SRT_REJ_RESOURCE;
                m_ConnReq.m_iReqType = URQFailure(SRT_REJ_RESOURCE);
                // the request time must be updated so that the next handshake can be sent out immediately.
                m_llLastReqTime = 0;
                return CONN_REJECT;
            }

            m_ConnReq.m_iReqType = URQ_CONCLUSION;
            // the request time must be updated so that the next handshake can be sent out immediately.
            m_llLastReqTime = 0;
            return CONN_CONTINUE;
        }
        else
        {
            HLOGC(mglog.Debug, log << CONID() << "processConnectResponse: Rendezvous HSv4 PAST waveahand");
        }
    }
    else
    {
        // set cookie
        if (m_ConnRes.m_iReqType == URQ_INDUCTION)
        {
            HLOGC(mglog.Debug,
                  log << CONID() << "processConnectResponse: REQ-TIME LOW; got INDUCTION HS response (cookie:" << hex
                      << m_ConnRes.m_iCookie << " version:" << dec << m_ConnRes.m_iVersion
                      << "), sending CONCLUSION HS with this cookie");

            m_ConnReq.m_iCookie  = m_ConnRes.m_iCookie;
            m_ConnReq.m_iReqType = URQ_CONCLUSION;

            // Here test if the LISTENER has responded with version HS_VERSION_SRT1,
            // it means that it is HSv5 capable. It can still accept the HSv4 handshake.
            if (m_ConnRes.m_iVersion > HS_VERSION_UDT4)
            {
                int hs_flags = SrtHSRequest::SRT_HSTYPE_HSFLAGS::unwrap(m_ConnRes.m_iType);

                if (hs_flags != SrtHSRequest::SRT_MAGIC_CODE)
                {
                    LOGC(mglog.Warn, log << "processConnectResponse: Listener HSv5 did not set the SRT_MAGIC_CODE");
                }

                checkUpdateCryptoKeyLen("processConnectResponse", m_ConnRes.m_iType);

                // This will catch HS_VERSION_SRT1 and any newer.
                // Set your highest version.
                m_ConnReq.m_iVersion = HS_VERSION_SRT1;
                // CONTROVERSIAL: use 0 as m_iType according to the meaning in HSv5.
                // The HSv4 client might not understand it, which means that agent
                // must switch itself to HSv4 rendezvous, and this time iType sould
                // be set to UDT_DGRAM value.
                m_ConnReq.m_iType = 0;

                // This marks the information for the serializer that
                // the SRT handshake extension is required.
                // Rest of the data will be filled together with
                // serialization.
                m_ConnReq.m_extension = true;

                // For HSv5, the caller is INITIATOR and the listener is RESPONDER.
                // The m_bDataSender value should be completely ignored and the
                // connection is always bidirectional.
                bidirectional = true;
                hsd           = HSD_INITIATOR;
            }
            m_llLastReqTime = 0;
            if (!createCrypter(hsd, bidirectional))
            {
                m_RejectReason = SRT_REJ_RESOURCE;
                return CONN_REJECT;
            }
            // NOTE: This setup sets URQ_CONCLUSION and appropriate data in the handshake structure.
            // The full handshake to be sent will be filled back in the caller function -- CUDT::startConnect().
            return CONN_CONTINUE;
        }
    }

    return postConnect(response, false, eout, synchro);
}

void CUDT::applyResponseSettings()
{
    // Re-configure according to the negotiated values.
    m_iMSS               = m_ConnRes.m_iMSS;
    m_iFlowWindowSize    = m_ConnRes.m_iFlightFlagSize;
    int udpsize          = m_iMSS - CPacket::UDP_HDR_SIZE;
    m_iMaxSRTPayloadSize = udpsize - CPacket::HDR_SIZE;
    m_iPeerISN           = m_ConnRes.m_iISN;
    m_iRcvLastAck        = m_ConnRes.m_iISN;
#ifdef ENABLE_LOGGING
    m_iDebugPrevLastAck = m_iRcvLastAck;
#endif
    m_iRcvLastSkipAck  = m_iRcvLastAck;
    m_iRcvLastAckAck   = m_ConnRes.m_iISN;
    m_iRcvCurrSeqNo    = m_ConnRes.m_iISN - 1;
    m_iRcvCurrPhySeqNo = m_ConnRes.m_iISN - 1;
    m_PeerID           = m_ConnRes.m_iID;
    memcpy(m_piSelfIP, m_ConnRes.m_piPeerIP, 16);

    HLOGC(mglog.Debug,
          log << CONID() << "applyResponseSettings: HANSHAKE CONCLUDED. SETTING: payload-size=" << m_iMaxSRTPayloadSize
              << " mss=" << m_ConnRes.m_iMSS << " flw=" << m_ConnRes.m_iFlightFlagSize << " isn=" << m_ConnRes.m_iISN
              << " peerID=" << m_ConnRes.m_iID);
}

EConnectStatus CUDT::postConnect(const CPacket &response, bool rendezvous, CUDTException *eout, bool synchro)
{
    if (m_ConnRes.m_iVersion < HS_VERSION_SRT1)
        m_ullRcvPeerStartTime = 0; // will be set correctly in SRT HS.

    // This procedure isn't being executed in rendezvous because
    // in rendezvous it's completed before calling this function.
    if (!rendezvous)
    {
        // NOTE: THIS function must be called before calling prepareConnectionObjects.
        // The reason why it's not part of prepareConnectionObjects is that the activities
        // done there are done SIMILAR way in acceptAndRespond, which also calls this
        // function. In fact, prepareConnectionObjects() represents the code that was
        // done separately in processConnectResponse() and acceptAndRespond(), so this way
        // this code is now common. Now acceptAndRespond() does "manually" something similar
        // to applyResponseSettings(), just a little bit differently. This SHOULD be made
        // common as a part of refactoring job, just needs a bit more time.
        //
        // Currently just this function must be called always BEFORE prepareConnectionObjects
        // everywhere except acceptAndRespond().
        applyResponseSettings();

        // This will actually be done also in rendezvous HSv4,
        // however in this case the HSREQ extension will not be attached,
        // so it will simply go the "old way".
        bool ok = prepareConnectionObjects(m_ConnRes, m_SrtHsSide, eout);
        // May happen that 'response' contains a data packet that was sent in rendezvous mode.
        // In this situation the interpretation of handshake was already done earlier.
        if (ok && response.isControl())
        {
            ok = interpretSrtHandshake(m_ConnRes, response, 0, 0);
            if (!ok && eout)
            {
                *eout = CUDTException(MJ_SETUP, MN_REJECTED, 0);
            }
        }
        if (!ok) // m_RejectReason already set
            return CONN_REJECT;
    }

    CInfoBlock ib;
    ib.m_iIPversion = m_iIPversion;
    CInfoBlock::convert(m_pPeerAddr, m_iIPversion, ib.m_piIP);
    if (m_pCache->lookup(&ib) >= 0)
    {
        m_iRTT       = ib.m_iRTT;
        m_iBandwidth = ib.m_iBandwidth;
    }

    SRT_REJECT_REASON rr = setupCC();
    if (rr != SRT_REJ_UNKNOWN)
    {
        m_RejectReason = rr;
        return CONN_REJECT;
    }

    // And, I am connected too.
    m_bConnecting = false;
    m_bConnected  = true;

    // register this socket for receiving data packets
    m_pRNode->m_bOnList = true;
    m_pRcvQueue->setNewEntry(this);

    // XXX Problem around CONN_CONFUSED!
    // If some too-eager packets were received from a listener
    // that thinks it's connected, but his last handshake was missed,
    // they are collected by CRcvQueue::storePkt. The removeConnector
    // function will want to delete them all, so it would be nice
    // if these packets can be re-delivered. Of course the listener
    // should be prepared to resend them (as every packet can be lost
    // on UDP), but it's kinda overkill when we have them already and
    // can dispatch them.

    // Remove from rendezvous queue (in this particular case it's
    // actually removing the socket that undergoes asynchronous HS processing).
    // Removing at THIS point because since when setNewEntry is called,
    // the next iteration in the CRcvQueue::worker loop will be dispatching
    // packets normally, as within-connection, so the "connector" won't
    // play any role since this time.
    // The connector, however, must stay alive until the setNewEntry is called
    // because otherwise the packets that are coming for this socket before the
    // connection process is complete will be rejected as "attack", instead of
    // being enqueued for later pickup from the queue.
    m_pRcvQueue->removeConnector(m_SocketID, synchro);

    // acknowledge the management module.
    CUDTSocket* s = s_UDTUnited.locate(m_SocketID);
    if (!s)
    {
        if (eout)
        {
            *eout = CUDTException(MJ_NOTSUP, MN_SIDINVAL, 0);
        }

        m_RejectReason = SRT_REJ_CLOSE;
        return CONN_REJECT;
    }

    // copy address information of local node
    // the local port must be correctly assigned BEFORE CUDT::startConnect(),
    // otherwise if startConnect() fails, the multiplexer cannot be located
    // by garbage collection and will cause leak
    s->m_pUDT->m_pSndQueue->m_pChannel->getSockAddr(s->m_pSelfAddr);
    CIPAddress::pton(s->m_pSelfAddr, s->m_pUDT->m_piSelfIP, s->m_iIPversion);

    s->m_Status = SRTS_CONNECTED;

    // acknowledde any waiting epolls to write
    s_UDTUnited.m_EPoll.update_events(m_SocketID, m_sPollID, UDT_EPOLL_OUT, true);

    LOGC(mglog.Note, log << "Connection established to: " << SockaddrToString(m_pPeerAddr));

    return CONN_ACCEPT;
}

void CUDT::checkUpdateCryptoKeyLen(const char *loghdr SRT_ATR_UNUSED, int32_t typefield)
{
    int enc_flags = SrtHSRequest::SRT_HSTYPE_ENCFLAGS::unwrap(typefield);

    // potentially 0-7 values are possible.
    // When 0, don't change anything - it should rely on the value 0.
    // When 1, 5, 6, 7, this is kinda internal error - ignore.
    if (enc_flags >= 2 && enc_flags <= 4) // 2 = 128, 3 = 192, 4 = 256
    {
        int rcv_pbkeylen = SrtHSRequest::SRT_PBKEYLEN_BITS::wrap(enc_flags);
        if (m_iSndCryptoKeyLen == 0)
        {
            m_iSndCryptoKeyLen = rcv_pbkeylen;
            HLOGC(mglog.Debug, log << loghdr << ": PBKEYLEN adopted from advertised value: " << m_iSndCryptoKeyLen);
        }
        else if (m_iSndCryptoKeyLen != rcv_pbkeylen)
        {
            // Conflict. Use SRTO_SENDER flag to check if this side should accept
            // the enforcement, otherwise simply let it win.
            if (!m_bDataSender)
            {
                LOGC(mglog.Warn,
                     log << loghdr << ": PBKEYLEN conflict - OVERRIDDEN " << m_iSndCryptoKeyLen << " by "
                         << rcv_pbkeylen << " from PEER (as AGENT is not SRTO_SENDER)");
                m_iSndCryptoKeyLen = rcv_pbkeylen;
            }
            else
            {
                LOGC(mglog.Warn,
                     log << loghdr << ": PBKEYLEN conflict - keep " << m_iSndCryptoKeyLen
                         << "; peer-advertised PBKEYLEN " << rcv_pbkeylen << " rejected because Agent is SRTO_SENDER");
            }
        }
    }
    else if (enc_flags != 0)
    {
        LOGC(mglog.Error, log << loghdr << ": IPE: enc_flags outside allowed 2, 3, 4: " << enc_flags);
    }
    else
    {
        HLOGC(mglog.Debug, log << loghdr << ": No encryption flags found in type field: " << typefield);
    }
}

// Rendezvous
void CUDT::rendezvousSwitchState(ref_t<UDTRequestType> rsptype, ref_t<bool> needs_extension, ref_t<bool> needs_hsrsp)
{
    UDTRequestType req           = m_ConnRes.m_iReqType;
    int            hs_flags      = SrtHSRequest::SRT_HSTYPE_HSFLAGS::unwrap(m_ConnRes.m_iType);
    bool           has_extension = !!hs_flags; // it holds flags, if no flags, there are no extensions.

    const HandshakeSide &hsd = m_SrtHsSide;
    // Note important possibilities that are considered here:

    // 1. The serial arrangement. This happens when one party has missed the
    // URQ_WAVEAHAND message, it sent its own URQ_WAVEAHAND message, and then the
    // firstmost message it received from the peer is URQ_CONCLUSION, as a response
    // for agent's URQ_WAVEAHAND.
    //
    // In this case, Agent switches to RDV_FINE state and Peer switches to RDV_ATTENTION state.
    //
    // 2. The parallel arrangement. This happens when the URQ_WAVEAHAND message sent
    // by both parties are almost in a perfect synch (a rare, but possible case). In this
    // case, both parties receive one another's URQ_WAVEAHAND message and both switch to
    // RDV_ATTENTION state.
    //
    // It's not possible to predict neither which arrangement will happen, or which
    // party will be RDV_FINE in case when the serial arrangement has happened. What
    // will actually happen will depend on random conditions.
    //
    // No matter this randomity, we have a limited number of possible conditions:
    //
    // Stating that "agent" is the party that has received the URQ_WAVEAHAND in whatever
    // arrangement, we are certain, that "agent" switched to RDV_ATTENTION, and peer:
    //
    // - switched to RDV_ATTENTION state (so, both are in the same state independently)
    // - switched to RDV_FINE state (so, the message interchange is actually more-less sequenced)
    //
    // In particular, there's no possibility of a situation that both are in RDV_FINE state
    // because the agent can switch to RDV_FINE state only if it received URQ_CONCLUSION from
    // the peer, while the peer could not send URQ_CONCLUSION without switching off RDV_WAVING
    // (actually to RDV_ATTENTION). There's also no exit to RDV_FINE from RDV_ATTENTION.

    // DEFAULT STATEMENT: don't attach extensions to URQ_CONCLUSION, neither HSREQ nor HSRSP.
    *needs_extension = false;
    *needs_hsrsp     = false;

    string reason;

#if ENABLE_HEAVY_LOGGING

    HLOGC(mglog.Debug, log << "rendezvousSwitchState: HS: " << m_ConnRes.show());

    struct LogAtTheEnd
    {
        CHandShake::RendezvousState        ost;
        UDTRequestType                     orq;
        const CHandShake::RendezvousState &nst;
        const UDTRequestType &             nrq;
        bool &                             needext;
        bool &                             needrsp;
        string &                           reason;

        ~LogAtTheEnd()
        {
            HLOGC(mglog.Debug,
                  log << "rendezvousSwitchState: STATE[" << CHandShake::RdvStateStr(ost) << "->"
                      << CHandShake::RdvStateStr(nst) << "] REQTYPE[" << RequestTypeStr(orq) << "->"
                      << RequestTypeStr(nrq) << "] "
                      << "ext:" << (needext ? (needrsp ? "HSRSP" : "HSREQ") : "NONE")
                      << (reason == "" ? string() : "reason:" + reason));
        }
    } l_logend = {m_RdvState, req, m_RdvState, *rsptype, *needs_extension, *needs_hsrsp, reason};

#endif

    switch (m_RdvState)
    {
    case CHandShake::RDV_INVALID:
        return;

    case CHandShake::RDV_WAVING:
    {
        if (req == URQ_WAVEAHAND)
        {
            m_RdvState = CHandShake::RDV_ATTENTION;

            // NOTE: if this->isWinner(), attach HSREQ
            *rsptype = URQ_CONCLUSION;
            if (hsd == HSD_INITIATOR)
                *needs_extension = true;
            return;
        }

        if (req == URQ_CONCLUSION)
        {
            m_RdvState = CHandShake::RDV_FINE;
            *rsptype   = URQ_CONCLUSION;

            *needs_extension = true; // (see below - this needs to craft either HSREQ or HSRSP)
            // if this->isWinner(), then craft HSREQ for that response.
            // if this->isLoser(), then this packet should bring HSREQ, so craft HSRSP for the response.
            if (hsd == HSD_RESPONDER)
                *needs_hsrsp = true;
            return;
        }
    }
        reason = "WAVING -> WAVEAHAND or CONCLUSION";
        break;

    case CHandShake::RDV_ATTENTION:
    {
        if (req == URQ_WAVEAHAND)
        {
            // This is only possible if the URQ_CONCLUSION sent to the peer
            // was lost on track. The peer is then simply unaware that the
            // agent has switched to ATTENTION state and continues sending
            // waveahands. In this case, just remain in ATTENTION state and
            // retry with URQ_CONCLUSION, as normally.
            *rsptype = URQ_CONCLUSION;
            if (hsd == HSD_INITIATOR)
                *needs_extension = true;
            return;
        }

        if (req == URQ_CONCLUSION)
        {
            // We have two possibilities here:
            //
            // WINNER (HSD_INITIATOR): send URQ_AGREEMENT
            if (hsd == HSD_INITIATOR)
            {
                // WINNER should get a response with HSRSP, otherwise this is kinda empty conclusion.
                // If no HSRSP attached, stay in this state.
                if (hs_flags == 0)
                {
                    HLOGC(
                        mglog.Debug,
                        log << "rendezvousSwitchState: "
                               "{INITIATOR}[ATTENTION] awaits CONCLUSION+HSRSP, got CONCLUSION, remain in [ATTENTION]");
                    *rsptype         = URQ_CONCLUSION;
                    *needs_extension = true; // If you expect to receive HSRSP, continue sending HSREQ
                    return;
                }
                m_RdvState = CHandShake::RDV_CONNECTED;
                *rsptype   = URQ_AGREEMENT;
                return;
            }

            // LOSER (HSD_RESPONDER): send URQ_CONCLUSION and attach HSRSP extension, then expect URQ_AGREEMENT
            if (hsd == HSD_RESPONDER)
            {
                // If no HSREQ attached, stay in this state.
                // (Although this seems completely impossible).
                if (hs_flags == 0)
                {
                    LOGC(
                        mglog.Warn,
                        log << "rendezvousSwitchState: (IPE!)"
                               "{RESPONDER}[ATTENTION] awaits CONCLUSION+HSREQ, got CONCLUSION, remain in [ATTENTION]");
                    *rsptype         = URQ_CONCLUSION;
                    *needs_extension = false; // If you received WITHOUT extensions, respond WITHOUT extensions (wait
                                              // for the right message)
                    return;
                }
                m_RdvState       = CHandShake::RDV_INITIATED;
                *rsptype         = URQ_CONCLUSION;
                *needs_extension = true;
                *needs_hsrsp     = true;
                return;
            }

            LOGC(mglog.Error, log << "RENDEZVOUS COOKIE DRAW! Cannot resolve to a valid state.");
            // Fallback for cookie draw
            m_RdvState = CHandShake::RDV_INVALID;
            *rsptype   = URQFailure(SRT_REJ_RDVCOOKIE);
            return;
        }

        if (req == URQ_AGREEMENT)
        {
            // This means that the peer has received our URQ_CONCLUSION, but
            // the agent missed the peer's URQ_CONCLUSION (received only initial
            // URQ_WAVEAHAND).
            if (hsd == HSD_INITIATOR)
            {
                // In this case the missed URQ_CONCLUSION was sent without extensions,
                // whereas the peer received our URQ_CONCLUSION with HSREQ, and therefore
                // it sent URQ_AGREEMENT already with HSRSP. This isn't a problem for
                // us, we can go on with it, especially that the peer is already switched
                // into CHandShake::RDV_CONNECTED state.
                m_RdvState = CHandShake::RDV_CONNECTED;

                // Both sides are connected, no need to send anything anymore.
                *rsptype = URQ_DONE;
                return;
            }

            if (hsd == HSD_RESPONDER)
            {
                // In this case the missed URQ_CONCLUSION was sent with extensions, so
                // we have to request this once again. Send URQ_CONCLUSION in order to
                // inform the other party that we need the conclusion message once again.
                // The ATTENTION state should be maintained.
                *rsptype         = URQ_CONCLUSION;
                *needs_extension = true;
                *needs_hsrsp     = true;
                return;
            }
        }
    }
        reason = "ATTENTION -> WAVEAHAND(conclusion), CONCLUSION(agreement/conclusion), AGREEMENT (done/conclusion)";
        break;

    case CHandShake::RDV_FINE:
    {
        // In FINE state we can't receive URQ_WAVEAHAND because if the peer has already
        // sent URQ_CONCLUSION, it's already in CHandShake::RDV_ATTENTION, and in this state it can
        // only send URQ_CONCLUSION, whereas when it isn't in CHandShake::RDV_ATTENTION, it couldn't
        // have sent URQ_CONCLUSION, and if it didn't, the agent wouldn't be in CHandShake::RDV_FINE state.

        if (req == URQ_CONCLUSION)
        {
            // There's only one case when it should receive CONCLUSION in FINE state:
            // When it's the winner. If so, it should then contain HSREQ extension.
            // In case of loser, it shouldn't receive CONCLUSION at all - it should
            // receive AGREEMENT.

            // The winner case, received CONCLUSION + HSRSP - switch to CONNECTED and send AGREEMENT.
            // So, check first if HAS EXTENSION

            bool correct_switch = false;
            if (hsd == HSD_INITIATOR && !has_extension)
            {
                // Received REPEATED empty conclusion that has initially switched it into FINE state.
                // To exit FINE state we need the CONCLUSION message with HSRSP.
                HLOGC(mglog.Debug,
                      log << "rendezvousSwitchState: {INITIATOR}[FINE] <CONCLUSION without HSRSP. Stay in [FINE], "
                             "await CONCLUSION+HSRSP");
            }
            else if (hsd == HSD_RESPONDER)
            {
                // In FINE state the RESPONDER expects only to be sent AGREEMENT.
                // It has previously received CONCLUSION in WAVING state and this has switched
                // it to FINE state. That CONCLUSION message should have contained extension,
                // so if this is a repeated CONCLUSION+HSREQ, it should be responded with
                // CONCLUSION+HSRSP.
                HLOGC(mglog.Debug,
                      log << "rendezvousSwitchState: {RESPONDER}[FINE] <CONCLUSION. Stay in [FINE], await AGREEMENT");
            }
            else
            {
                correct_switch = true;
            }

            if (!correct_switch)
            {
                *rsptype = URQ_CONCLUSION;
                // initiator should send HSREQ, responder HSRSP,
                // in both cases extension is needed
                *needs_extension = true;
                *needs_hsrsp     = hsd == HSD_RESPONDER;
                return;
            }

            m_RdvState = CHandShake::RDV_CONNECTED;
            *rsptype   = URQ_AGREEMENT;
            return;
        }

        if (req == URQ_AGREEMENT)
        {
            // The loser case, the agreement was sent in response to conclusion that
            // already carried over the HSRSP extension.

            // There's a theoretical case when URQ_AGREEMENT can be received in case of
            // parallel arrangement, while the agent is already in CHandShake::RDV_CONNECTED state.
            // This will be dispatched in the main loop and discarded.

            m_RdvState = CHandShake::RDV_CONNECTED;
            *rsptype   = URQ_DONE;
            return;
        }
    }

        reason = "FINE -> CONCLUSION(agreement), AGREEMENT(done)";
        break;
    case CHandShake::RDV_INITIATED:
    {
        // In this state we just wait for URQ_AGREEMENT, which should cause it to
        // switch to CONNECTED. No response required.
        if (req == URQ_AGREEMENT)
        {
            // No matter in which state we'd be, just switch to connected.
            if (m_RdvState == CHandShake::RDV_CONNECTED)
            {
                HLOGC(mglog.Debug, log << "<-- AGREEMENT: already connected");
            }
            else
            {
                HLOGC(mglog.Debug, log << "<-- AGREEMENT: switched to connected");
            }
            m_RdvState = CHandShake::RDV_CONNECTED;
            *rsptype   = URQ_DONE;
            return;
        }

        if (req == URQ_CONCLUSION)
        {
            // Receiving conclusion in this state means that the other party
            // didn't get our conclusion, so send it again, the same as when
            // exiting the ATTENTION state.
            *rsptype = URQ_CONCLUSION;
            if (hsd == HSD_RESPONDER)
            {
                HLOGC(mglog.Debug,
                      log << "rendezvousSwitchState: "
                             "{RESPONDER}[INITIATED] awaits AGREEMENT, "
                             "got CONCLUSION, sending CONCLUSION+HSRSP");
                *needs_extension = true;
                *needs_hsrsp     = true;
                return;
            }

            // Loser, initiated? This may only happen in parallel arrangement, where
            // the agent exchanges empty conclusion messages with the peer, simultaneously
            // exchanging HSREQ-HSRSP conclusion messages. Check if THIS message contained
            // HSREQ, and set responding HSRSP in that case.
            if (hs_flags == 0)
            {
                HLOGC(mglog.Debug,
                      log << "rendezvousSwitchState: "
                             "{INITIATOR}[INITIATED] awaits AGREEMENT, "
                             "got empty CONCLUSION, STILL RESPONDING CONCLUSION+HSRSP");
            }
            else
            {

                HLOGC(mglog.Debug,
                      log << "rendezvousSwitchState: "
                             "{INITIATOR}[INITIATED] awaits AGREEMENT, "
                             "got CONCLUSION+HSREQ, responding CONCLUSION+HSRSP");
            }
            *needs_extension = true;
            *needs_hsrsp     = true;
            return;
        }
    }

        reason = "INITIATED -> AGREEMENT(done)";
        break;

    case CHandShake::RDV_CONNECTED:
        // Do nothing. This theoretically should never happen.
        *rsptype = URQ_DONE;
        return;
    }

    HLOGC(mglog.Debug, log << "rendezvousSwitchState: INVALID STATE TRANSITION, result: INVALID");
    // All others are treated as errors
    m_RdvState = CHandShake::RDV_WAVING;
    *rsptype   = URQFailure(SRT_REJ_ROGUE);
}

/*
 * Timestamp-based Packet Delivery (TsbPd) thread
 * This thread runs only if TsbPd mode is enabled
 * Hold received packets until its time to 'play' them, at PktTimeStamp + TsbPdDelay.
 */
void *CUDT::tsbpd(void *param)
{
    CUDT *self = (CUDT *)param;

    THREAD_STATE_INIT("SRT:TsbPd");

    CGuard::enterCS(self->m_RecvLock);
    self->m_bTsbPdAckWakeup = true;
    while (!self->m_bClosing)
    {
        int32_t  current_pkt_seq = 0;
        uint64_t tsbpdtime       = 0;
        bool     rxready         = false;

        CGuard::enterCS(self->m_RcvBufferLock);

#ifdef SRT_ENABLE_RCVBUFSZ_MAVG
        self->m_pRcvBuffer->updRcvAvgDataSize(CTimer::getTime());
#endif

        if (self->m_bTLPktDrop)
        {
            int32_t skiptoseqno = -1;
            bool passack = true; // Get next packet to wait for even if not acked

            rxready = self->m_pRcvBuffer->getRcvFirstMsg(
                Ref(tsbpdtime), Ref(passack), Ref(skiptoseqno), Ref(current_pkt_seq));

            HLOGC(tslog.Debug,
                  log << boolalpha << "NEXT PKT CHECK: rdy=" << rxready << " passack=" << passack << " skipto=%"
                      << skiptoseqno << " current=%" << current_pkt_seq << " buf-base=%" << self->m_iRcvLastSkipAck);
            /*
             * VALUES RETURNED:
             *
             * rxready:     if true, packet at head of queue ready to play
             * tsbpdtime:   timestamp of packet at head of queue, ready or not. 0 if none.
             * passack:     if true, ready head of queue not yet acknowledged
             * skiptoseqno: sequence number of packet at head of queue if ready to play but
             *              some preceeding packets are missing (need to be skipped). -1 if none.
             */
            if (rxready)
            {
                /* Packet ready to play according to time stamp but... */
                int seqlen = CSeqNo::seqoff(self->m_iRcvLastSkipAck, skiptoseqno);

                if (skiptoseqno != -1 && seqlen > 0)
                {
                    /*
                     * skiptoseqno != -1,
                     * packet ready to play but preceeded by missing packets (hole).
                     */

                    /* Update drop/skip stats */
                    CGuard::enterCS(self->m_StatsLock);
                    self->m_stats.rcvDropTotal += seqlen;
                    self->m_stats.traceRcvDrop += seqlen;
                    /* Estimate dropped/skipped bytes from average payload */
                    int avgpayloadsz = self->m_pRcvBuffer->getRcvAvgPayloadSize();
                    self->m_stats.rcvBytesDropTotal += seqlen * avgpayloadsz;
                    self->m_stats.traceRcvBytesDrop += seqlen * avgpayloadsz;
                    CGuard::leaveCS(self->m_StatsLock);

                    self->dropFromLossLists(self->m_iRcvLastSkipAck,
                                            CSeqNo::decseq(skiptoseqno)); // remove(from,to-inclusive)
                    self->m_pRcvBuffer->skipData(seqlen);

                    self->m_iRcvLastSkipAck = skiptoseqno;

#if ENABLE_LOGGING
                    int64_t timediff = 0;
                    if (tsbpdtime)
                        timediff = int64_t(tsbpdtime) - int64_t(CTimer::getTime());
#if ENABLE_HEAVY_LOGGING
                    HLOGC(tslog.Debug,
                          log << self->CONID() << "tsbpd: DROPSEQ: up to seq=" << CSeqNo::decseq(skiptoseqno) << " ("
                              << seqlen << " packets) playable at " << FormatTime(tsbpdtime) << " delayed "
                              << (timediff / 1000) << "." << (timediff % 1000) << " ms");
#endif
                    LOGC(dlog.Debug, log << "RCV-DROPPED packet delay=" << (timediff / 1000) << "ms");
#endif

                    tsbpdtime = 0; // Next sent ack will unblock
                    rxready   = false;
                }
                else if (passack)
                {
                    /* Packets ready to play but not yet acknowledged (should happen within 10ms) */
                    rxready   = false;
                    tsbpdtime = 0; // Next sent ack will unblock
                }                  /* else packet ready to play */
            }                      /* else packets not ready to play */
        }
        else
        {
            rxready = self->m_pRcvBuffer->isRcvDataReady(Ref(tsbpdtime), Ref(current_pkt_seq));
        }
        CGuard::leaveCS(self->m_RcvBufferLock);

        if (rxready)
        {
            HLOGC(tslog.Debug,
                  log << self->CONID() << "tsbpd: PLAYING PACKET seq=" << current_pkt_seq << " (belated "
                      << ((CTimer::getTime() - tsbpdtime) / 1000.0) << "ms)");
            /*
             * There are packets ready to be delivered
             * signal a waiting "recv" call if there is any data available
             */
            if (self->m_bSynRecving)
            {
                pthread_cond_signal(&self->m_RecvDataCond);
            }
            /*
             * Set EPOLL_IN to wakeup any thread waiting on epoll
             */
            self->s_UDTUnited.m_EPoll.update_events(self->m_SocketID, self->m_sPollID, UDT_EPOLL_IN, true);
            CTimer::triggerEvent();
            tsbpdtime = 0;
        }

        if (tsbpdtime != 0)
        {
            int64_t timediff = int64_t(tsbpdtime) - int64_t(CTimer::getTime());
            /*
             * Buffer at head of queue is not ready to play.
             * Schedule wakeup when it will be.
             */
            self->m_bTsbPdAckWakeup = false;
            THREAD_PAUSED();
            HLOGC(tslog.Debug,
                  log << self->CONID() << "tsbpd: FUTURE PACKET seq=" << current_pkt_seq
                      << " T=" << FormatTime(tsbpdtime) << " - waiting " << (timediff / 1000.0) << "ms");
            CTimer::condTimedWaitUS(&self->m_RcvTsbPdCond, &self->m_RecvLock, timediff);
            THREAD_RESUMED();
        }
        else
        {
            /*
             * We have just signaled epoll; or
             * receive queue is empty; or
             * next buffer to deliver is not in receive queue (missing packet in sequence).
             *
             * Block until woken up by one of the following event:
             * - All ready-to-play packets have been pulled and EPOLL_IN cleared (then loop to block until next pkt time
             * if any)
             * - New buffers ACKed
             * - Closing the connection
             */
            HLOGC(tslog.Debug, log << self->CONID() << "tsbpd: no data, scheduling wakeup at ack");
            self->m_bTsbPdAckWakeup = true;
            THREAD_PAUSED();
            pthread_cond_wait(&self->m_RcvTsbPdCond, &self->m_RecvLock);
            THREAD_RESUMED();
        }
    }
    CGuard::leaveCS(self->m_RecvLock);
    THREAD_EXIT();
    HLOGC(tslog.Debug, log << self->CONID() << "tsbpd: EXITING");
    return NULL;
}

bool CUDT::prepareConnectionObjects(const CHandShake &hs, HandshakeSide hsd, CUDTException *eout)
{
    // This will be lazily created due to being the common
    // code with HSv5 rendezvous, in which this will be run
    // in a little bit "randomly selected" moment, but must
    // be run once in the whole connection process.
    if (m_pSndBuffer)
    {
        HLOGC(mglog.Debug, log << "prepareConnectionObjects: (lazy) already created.");
        return true;
    }

    bool bidirectional = false;
    if (hs.m_iVersion > HS_VERSION_UDT4)
    {
        bidirectional = true; // HSv5 is always bidirectional
    }

    // HSD_DRAW is received only if this side is listener.
    // If this side is caller with HSv5, HSD_INITIATOR should be passed.
    // If this is a rendezvous connection with HSv5, the handshake role
    // is taken from m_SrtHsSide field.
    if (hsd == HSD_DRAW)
    {
        if (bidirectional)
        {
            hsd = HSD_RESPONDER; // In HSv5, listener is always RESPONDER and caller always INITIATOR.
        }
        else
        {
            hsd = m_bDataSender ? HSD_INITIATOR : HSD_RESPONDER;
        }
    }

    try
    {
        m_pSndBuffer = new CSndBuffer(32, m_iMaxSRTPayloadSize);
        m_pRcvBuffer = new CRcvBuffer(&(m_pRcvQueue->m_UnitQueue), m_iRcvBufSize);
        // after introducing lite ACK, the sndlosslist may not be cleared in time, so it requires twice space.
        m_pSndLossList = new CSndLossList(m_iFlowWindowSize * 2);
        m_pRcvLossList = new CRcvLossList(m_iFlightFlagSize);
    }
    catch (...)
    {
        // Simply reject.
        if (eout)
        {
            *eout = CUDTException(MJ_SYSTEMRES, MN_MEMORY, 0);
        }
        m_RejectReason = SRT_REJ_RESOURCE;
        return false;
    }

    if (!createCrypter(hsd, bidirectional)) // Make sure CC is created (lazy)
    {
        m_RejectReason = SRT_REJ_RESOURCE;
        return false;
    }

    return true;
}

void CUDT::acceptAndRespond(const sockaddr *peer, CHandShake *hs, const CPacket &hspkt)
{
    HLOGC(mglog.Debug, log << "acceptAndRespond: setting up data according to handshake");

    CGuard cg(m_ConnectionLock);

    m_ullRcvPeerStartTime = 0; // will be set correctly at SRT HS

    // Uses the smaller MSS between the peers
    if (hs->m_iMSS > m_iMSS)
        hs->m_iMSS = m_iMSS;
    else
        m_iMSS = hs->m_iMSS;

    // exchange info for maximum flow window size
    m_iFlowWindowSize     = hs->m_iFlightFlagSize;
    hs->m_iFlightFlagSize = (m_iRcvBufSize < m_iFlightFlagSize) ? m_iRcvBufSize : m_iFlightFlagSize;

    m_iPeerISN = hs->m_iISN;

    m_iRcvLastAck = hs->m_iISN;
#ifdef ENABLE_LOGGING
    m_iDebugPrevLastAck = m_iRcvLastAck;
#endif
    m_iRcvLastSkipAck  = m_iRcvLastAck;
    m_iRcvLastAckAck   = hs->m_iISN;
    m_iRcvCurrSeqNo    = hs->m_iISN - 1;
    m_iRcvCurrPhySeqNo = hs->m_iISN - 1;

    m_PeerID  = hs->m_iID;
    hs->m_iID = m_SocketID;

    // use peer's ISN and send it back for security check
    m_iISN = hs->m_iISN;

    m_iLastDecSeq        = m_iISN - 1;
    m_iSndLastAck        = m_iISN;
    m_iSndLastDataAck    = m_iISN;
    m_iSndLastFullAck    = m_iISN;
    m_iSndCurrSeqNo      = m_iISN - 1;
    m_iSndLastAck2       = m_iISN;
    m_ullSndLastAck2Time = CTimer::getTime();

    // this is a reponse handshake
    hs->m_iReqType = URQ_CONCLUSION;

    if (hs->m_iVersion > HS_VERSION_UDT4)
    {
        // The version is agreed; this code is executed only in case
        // when AGENT is listener. In this case, conclusion response
        // must always contain HSv5 handshake extensions.
        hs->m_extension = true;
    }

    // get local IP address and send the peer its IP address (because UDP cannot get local IP address)
    memcpy(m_piSelfIP, hs->m_piPeerIP, 16);
    CIPAddress::ntop(peer, hs->m_piPeerIP, m_iIPversion);

    int udpsize          = m_iMSS - CPacket::UDP_HDR_SIZE;
    m_iMaxSRTPayloadSize = udpsize - CPacket::HDR_SIZE;
    HLOGC(mglog.Debug, log << "acceptAndRespond: PAYLOAD SIZE: " << m_iMaxSRTPayloadSize);

    // Prepare all structures
    if (!prepareConnectionObjects(*hs, HSD_DRAW, 0))
    {
        HLOGC(mglog.Debug, log << "acceptAndRespond: prepareConnectionObjects failed - responding with REJECT.");
        // If the SRT Handshake extension was provided and wasn't interpreted
        // correctly, the connection should be rejected.
        //
        // Respond with the rejection message and exit with exception
        // so that the caller will know that this new socket should be deleted.
        hs->m_iReqType = URQFailure(m_RejectReason);
        throw CUDTException(MJ_SETUP, MN_REJECTED, 0);
    }
    // Since now you can use m_pCryptoControl

    CInfoBlock ib;
    ib.m_iIPversion = m_iIPversion;
    CInfoBlock::convert(peer, m_iIPversion, ib.m_piIP);
    if (m_pCache->lookup(&ib) >= 0)
    {
        m_iRTT       = ib.m_iRTT;
        m_iBandwidth = ib.m_iBandwidth;
    }

    // This should extract the HSREQ and KMREQ portion in the handshake packet.
    // This could still be a HSv4 packet and contain no such parts, which will leave
    // this entity as "non-SRT-handshaken", and await further HSREQ and KMREQ sent
    // as UMSG_EXT.
    uint32_t kmdata[SRTDATA_MAXSIZE];
    size_t   kmdatasize = SRTDATA_MAXSIZE;
    if (!interpretSrtHandshake(*hs, hspkt, kmdata, &kmdatasize))
    {
        HLOGC(mglog.Debug, log << "acceptAndRespond: interpretSrtHandshake failed - responding with REJECT.");
        // If the SRT Handshake extension was provided and wasn't interpreted
        // correctly, the connection should be rejected.
        //
        // Respond with the rejection message and return false from
        // this function so that the caller will know that this new
        // socket should be deleted.
        hs->m_iReqType = URQFailure(m_RejectReason);
        throw CUDTException(MJ_SETUP, MN_REJECTED, 0);
    }

    SRT_REJECT_REASON rr = setupCC();
    // UNKNOWN used as a "no error" value
    if (rr != SRT_REJ_UNKNOWN)
    {
        hs->m_iReqType = URQFailure(rr);
        m_RejectReason = rr;
        throw CUDTException(MJ_SETUP, MN_REJECTED, 0);
    }

    m_pPeerAddr = (AF_INET == m_iIPversion) ? (sockaddr *)new sockaddr_in : (sockaddr *)new sockaddr_in6;
    memcpy(m_pPeerAddr, peer, (AF_INET == m_iIPversion) ? sizeof(sockaddr_in) : sizeof(sockaddr_in6));

    // And of course, it is connected.
    m_bConnected = true;

    // register this socket for receiving data packets
    m_pRNode->m_bOnList = true;
    m_pRcvQueue->setNewEntry(this);

    // send the response to the peer, see listen() for more discussions about this
    // XXX Here create CONCLUSION RESPONSE with:
    // - just the UDT handshake, if HS_VERSION_UDT4,
    // - if higher, the UDT handshake, the SRT HSRSP, the SRT KMRSP
    size_t size = m_iMaxSRTPayloadSize;
    // Allocate the maximum possible memory for an SRT payload.
    // This is a maximum you can send once.
    CPacket response;
    response.setControl(UMSG_HANDSHAKE);
    response.allocate(size);

    // This will serialize the handshake according to its current form.
    HLOGC(mglog.Debug,
          log << "acceptAndRespond: creating CONCLUSION response (HSv5: with HSRSP/KMRSP) buffer size=" << size);
    if (!createSrtHandshake(Ref(response), Ref(*hs), SRT_CMD_HSRSP, SRT_CMD_KMRSP, kmdata, kmdatasize))
    {
        LOGC(mglog.Error, log << "acceptAndRespond: error creating handshake response");
        throw CUDTException(MJ_SETUP, MN_REJECTED, 0);
    }

    // Set target socket ID to the value from received handshake's source ID.
    response.m_iID = m_PeerID;

#if ENABLE_HEAVY_LOGGING
    {
        // To make sure what REALLY is being sent, parse back the handshake
        // data that have been just written into the buffer.
        CHandShake debughs;
        debughs.load_from(response.m_pcData, response.getLength());
        HLOGC(mglog.Debug,
              log << CONID() << "acceptAndRespond: sending HS to peer, reqtype=" << RequestTypeStr(debughs.m_iReqType)
                  << " version=" << debughs.m_iVersion << " (connreq:" << RequestTypeStr(m_ConnReq.m_iReqType)
                  << "), target_socket=" << response.m_iID << ", my_socket=" << debughs.m_iID);
    }
#endif

    // NOTE: BLOCK THIS instruction in order to cause the final
    // handshake to be missed and cause the problem solved in PR #417.
    // When missed this message, the caller should not accept packets
    // coming as connected, but continue repeated handshake until finally
    // received the listener's handshake.
    m_pSndQueue->sendto(peer, response);
}

// This function is required to be called when a caller receives an INDUCTION
// response from the listener and would like to create a CONCLUSION that includes
// the SRT handshake extension. This extension requires that the crypter object
// be created, but it's still too early for it to be completely configured.
// This function then precreates the object so that the handshake extension can
// be created, as this happens before the completion of the connection (and
// therefore configuration of the crypter object), which can only take place upon
// reception of CONCLUSION response from the listener.
bool CUDT::createCrypter(HandshakeSide side, bool bidirectional)
{
    // Lazy initialization
    if (m_pCryptoControl)
        return true;

    // Write back this value, when it was just determined.
    m_SrtHsSide = side;

    m_pCryptoControl.reset(new CCryptoControl(this, m_SocketID));

    // XXX These below are a little bit controversial.
    // These data should probably be filled only upon
    // reception of the conclusion handshake - otherwise
    // they have outdated values.
    m_pCryptoControl->setCryptoSecret(m_CryptoSecret);

    if (bidirectional || m_bDataSender)
    {
        HLOGC(mglog.Debug, log << "createCrypter: setting RCV/SND KeyLen=" << m_iSndCryptoKeyLen);
        m_pCryptoControl->setCryptoKeylen(m_iSndCryptoKeyLen);
    }

    return m_pCryptoControl->init(side, bidirectional);
}

SRT_REJECT_REASON CUDT::setupCC()
{
    // Prepare configuration object,
    // Create the CCC object and configure it.

    // UDT also sets back the congestion window: ???
    // m_dCongestionWindow = m_pCC->m_dCWndSize;

    // XXX Not sure about that. May happen that AGENT wants
    // tsbpd mode, but PEER doesn't, even in bidirectional mode.
    // This way, the reception side should get precedense.
    // if (bidirectional || m_bDataSender || m_bTwoWayData)
    //    m_bPeerTsbPd = m_bOPT_TsbPd;

    // SrtCongestion will retrieve whatever parameters it needs
    // from *this.
    if (!m_CongCtl.configure(this))
    {
        return SRT_REJ_CONGESTION;
    }

    // Configure filter module
    if (m_OPT_PktFilterConfigString != "")
    {
        // This string, when nonempty, defines that the corrector shall be
        // configured. Otherwise it's left uninitialized.

        // At this point we state everything is checked and the appropriate
        // corrector type is already selected, so now create it.
        HLOGC(mglog.Debug, log << "filter: Configuring Corrector: " << m_OPT_PktFilterConfigString);
        if (!m_PacketFilter.configure(this, m_pRcvBuffer->getUnitQueue(), m_OPT_PktFilterConfigString))
        {
            return SRT_REJ_FILTER;
        }

        m_PktFilterRexmitLevel = m_PacketFilter.arqLevel();
    }
    else
    {
        // When we have no filter, ARQ should work in ALWAYS mode.
        m_PktFilterRexmitLevel = SRT_ARQ_ALWAYS;
    }

    // Override the value of minimum NAK interval, per SrtCongestion's wish.
    // When default 0 value is returned, the current value set by CUDT
    // is preserved.
    uint64_t min_nak_tk = m_CongCtl->minNAKInterval();
    if (min_nak_tk)
        m_ullMinNakInt_tk = min_nak_tk;

    // Update timers
    uint64_t currtime_tk;
    CTimer::rdtsc(currtime_tk);
    m_ullLastRspTime_tk    = currtime_tk;
    m_ullNextACKTime_tk    = currtime_tk + m_ullACKInt_tk;
    m_ullNextNAKTime_tk    = currtime_tk + m_ullNAKInt_tk;
    m_ullLastRspAckTime_tk = currtime_tk;
    m_ullLastSndTime_tk    = currtime_tk;

    HLOGC(mglog.Debug,
          log << "setupCC: setting parameters: mss=" << m_iMSS << " maxCWNDSize/FlowWindowSize=" << m_iFlowWindowSize
              << " rcvrate=" << m_iDeliveryRate << "p/s (" << m_iByteDeliveryRate << "B/S)"
              << " rtt=" << m_iRTT << " bw=" << m_iBandwidth);

    updateCC(TEV_INIT, TEV_INIT_RESET);
    return SRT_REJ_UNKNOWN;
}

void CUDT::considerLegacySrtHandshake(uint64_t timebase)
{
    // Do a fast pre-check first - this simply declares that agent uses HSv5
    // and the legacy SRT Handshake is not to be done. Second check is whether
    // agent is sender (=initiator in HSv4).
    if (!isTsbPd() || !m_bDataSender)
        return;

    if (m_iSndHsRetryCnt <= 0)
    {
        HLOGC(mglog.Debug, log << "Legacy HSREQ: not needed, expire counter=" << m_iSndHsRetryCnt);
        return;
    }

    uint64_t now = CTimer::getTime();
    if (timebase != 0)
    {
        // Then this should be done only if it's the right time,
        // the TSBPD mode is on, and when the counter is "still rolling".
        /*
         * SRT Handshake with peer:
         * If...
         * - we want TsbPd mode; and
         * - we have not tried more than CSRTCC_MAXRETRY times (peer may not be SRT); and
         * - and did not get answer back from peer
         * - last sent handshake req should have been replied (RTT*1.5 elapsed); and
         * then (re-)send handshake request.
         */
        if (timebase > now) // too early
        {
            HLOGC(mglog.Debug, log << "Legacy HSREQ: TOO EARLY, will still retry " << m_iSndHsRetryCnt << " times");
            return;
        }
    }
    // If 0 timebase, it means that this is the initial sending with the very first
    // payload packet sent. Send only if this is still set to maximum+1 value.
    else if (m_iSndHsRetryCnt < SRT_MAX_HSRETRY + 1)
    {
        HLOGC(mglog.Debug,
              log << "Legacy HSREQ: INITIAL, REPEATED, so not to be done. Will repeat on sending " << m_iSndHsRetryCnt
                  << " times");
        return;
    }

    HLOGC(mglog.Debug, log << "Legacy HSREQ: SENDING, will repeat " << m_iSndHsRetryCnt << " times if no response");
    m_iSndHsRetryCnt--;
    m_ullSndHsLastTime_us = now;
    sendSrtMsg(SRT_CMD_HSREQ);
}

void CUDT::checkSndTimers(Whether2RegenKm regen)
{
    if (m_SrtHsSide == HSD_INITIATOR)
    {
        HLOGC(mglog.Debug, log << "checkSndTimers: HS SIDE: INITIATOR, considering legacy handshake with timebase");
        // Legacy method for HSREQ, only if initiator.
        considerLegacySrtHandshake(m_ullSndHsLastTime_us + m_iRTT * 3 / 2);
    }
    else
    {
        HLOGC(mglog.Debug,
              log << "checkSndTimers: HS SIDE: " << (m_SrtHsSide == HSD_RESPONDER ? "RESPONDER" : "DRAW (IPE?)")
                  << " - not considering legacy handshake");
    }

    // This must be done always on sender, regardless of HS side.
    // When regen == DONT_REGEN_KM, it's a handshake call, so do
    // it only for initiator.
    if (regen || m_SrtHsSide == HSD_INITIATOR)
    {
        // Don't call this function in "non-regen mode" (sending only),
        // if this side is RESPONDER. This shall be called only with
        // regeneration request, which is required by the sender.
        if (m_pCryptoControl)
            m_pCryptoControl->sendKeysToPeer(regen);
    }
}

void CUDT::addressAndSend(CPacket &pkt)
{
    pkt.m_iID        = m_PeerID;
    pkt.m_iTimeStamp = int(CTimer::getTime() - m_stats.startTime);

    m_pSndQueue->sendto(m_pPeerAddr, pkt);
}

bool CUDT::close()
{
    // NOTE: this function is called from within the garbage collector thread.

    if (!m_bOpened)
    {
        return false;
    }

    HLOGC(mglog.Debug, log << CONID() << " - closing socket:");

    if (m_Linger.l_onoff != 0)
    {
        uint64_t entertime = CTimer::getTime();

        HLOGC(mglog.Debug, log << CONID() << " ... (linger)");
        while (!m_bBroken && m_bConnected && (m_pSndBuffer->getCurrBufSize() > 0) &&
               (CTimer::getTime() - entertime < m_Linger.l_linger * uint64_t(1000000)))
        {
            // linger has been checked by previous close() call and has expired
            if (m_ullLingerExpiration >= entertime)
                break;

            if (!m_bSynSending)
            {
                // if this socket enables asynchronous sending, return immediately and let GC to close it later
                if (m_ullLingerExpiration == 0)
                    m_ullLingerExpiration = entertime + m_Linger.l_linger * uint64_t(1000000);

                HLOGC(mglog.Debug,
                      log << "CUDT::close: linger-nonblocking, setting expire time T="
                          << FormatTime(m_ullLingerExpiration));

                return false;
            }

#ifndef _WIN32
            timespec ts;
            ts.tv_sec  = 0;
            ts.tv_nsec = 1000000;
            nanosleep(&ts, NULL);
#else
            Sleep(1);
#endif
        }
    }

    // remove this socket from the snd queue
    if (m_bConnected)
        m_pSndQueue->m_pSndUList->remove(this);

    /*
     * update_events below useless
     * removing usock for EPolls right after (remove_usocks) clears it (in other HAI patch).
     *
     * What is in EPoll shall be the responsibility of the application, if it want local close event,
     * it would remove the socket from the EPoll after close.
     */
    // trigger any pending IO events.
    s_UDTUnited.m_EPoll.update_events(m_SocketID, m_sPollID, UDT_EPOLL_ERR, true);
    // then remove itself from all epoll monitoring
    try
    {
        for (set<int>::iterator i = m_sPollID.begin(); i != m_sPollID.end(); ++i)
            s_UDTUnited.m_EPoll.remove_usock(*i, m_SocketID);
    }
    catch (...)
    {
    }

    // XXX What's this, could any of the above actions make it !m_bOpened?
    if (!m_bOpened)
    {
        return true;
    }

    // Inform the threads handler to stop.
    m_bClosing = true;

    HLOGC(mglog.Debug, log << CONID() << "CLOSING STATE. Acquiring connection lock");

    CGuard cg(m_ConnectionLock);

    // Signal the sender and recver if they are waiting for data.
    releaseSynch();

    HLOGC(mglog.Debug, log << CONID() << "CLOSING, removing from listener/connector");

    if (m_bListening)
    {
        m_bListening = false;
        m_pRcvQueue->removeListener(this);
    }
    else if (m_bConnecting)
    {
        m_pRcvQueue->removeConnector(m_SocketID);
    }

    if (m_bConnected)
    {
        if (!m_bShutdown)
        {
            HLOGC(mglog.Debug, log << CONID() << "CLOSING - sending SHUTDOWN to the peer");
            sendCtrl(UMSG_SHUTDOWN);
        }

        m_pCryptoControl->close();

        // Store current connection information.
        CInfoBlock ib;
        ib.m_iIPversion = m_iIPversion;
        CInfoBlock::convert(m_pPeerAddr, m_iIPversion, ib.m_piIP);
        ib.m_iRTT       = m_iRTT;
        ib.m_iBandwidth = m_iBandwidth;
        m_pCache->update(&ib);

        m_bConnected = false;
    }

    if (m_bTsbPd && !pthread_equal(m_RcvTsbPdThread, pthread_t()))
    {
        HLOGC(mglog.Debug, log << "CLOSING, joining TSBPD thread...");
        void *retval;
        int ret SRT_ATR_UNUSED = pthread_join(m_RcvTsbPdThread, &retval);
        HLOGC(mglog.Debug, log << "... " << (ret == 0 ? "SUCCEEDED" : "FAILED"));
    }

    HLOGC(mglog.Debug, log << "CLOSING, joining send/receive threads");

    // waiting all send and recv calls to stop
    CGuard sendguard(m_SendLock);
    CGuard recvguard(m_RecvLock);

    // Locking m_RcvBufferLock to protect calling to m_pCryptoControl->decrypt(Ref(packet))
    // from the processData(...) function while resetting Crypto Control.
    CGuard::enterCS(m_RcvBufferLock);
    m_pCryptoControl.reset();
    CGuard::leaveCS(m_RcvBufferLock);

    m_lSrtVersion            = SRT_DEF_VERSION;
    m_lPeerSrtVersion        = SRT_VERSION_UNK;
    m_lMinimumPeerSrtVersion = SRT_VERSION_MAJ1;
    m_ullRcvPeerStartTime    = 0;

    m_bOpened = false;

    return true;
}

/*
 Old, mostly original UDT based version of CUDT::send.
 Left for historical reasons.

int CUDT::send(const char* data, int len)
{
   // throw an exception if not connected
   if (m_bBroken || m_bClosing)
      throw CUDTException(MJ_CONNECTION, MN_CONNLOST, 0);
   else if (!m_bConnected || !m_CongCtl.ready())
      throw CUDTException(MJ_CONNECTION, MN_NOCONN, 0);

   if (len <= 0)
      return 0;

   // Check if the current congctl accepts the call with given parameters.
   if (!m_CongCtl->checkTransArgs(SrtCongestion::STA_BUFFER, SrtCongestion::STAD_SEND, data, len, -1, false))
      throw CUDTException(MJ_NOTSUP, MN_INVALBUFFERAPI, 0);

   CGuard sendguard(m_SendLock);

   if (m_pSndBuffer->getCurrBufSize() == 0)
   {
      // delay the EXP timer to avoid mis-fired timeout
      uint64_t currtime_tk;
      CTimer::rdtsc(currtime_tk);
      // (fix keepalive) m_ullLastRspTime_tk = currtime_tk;
      m_ullLastRspAckTime_tk = currtime_tk;
      m_iReXmitCount = 1;
   }
   if (sndBuffersLeft() <= 0)
   {
      if (!m_bSynSending)
         throw CUDTException(MJ_AGAIN, MN_WRAVAIL, 0);
      else
      {
          {
              // wait here during a blocking sending
              CGuard sendblock_lock(m_SendBlockLock);
              if (m_iSndTimeOut < 0)
              {
                  while (stillConnected() && (sndBuffersLeft() <= 0) && m_bPeerHealth)
                      pthread_cond_wait(&m_SendBlockCond, &m_SendBlockLock);
              }
              else
              {
                  uint64_t exptime = CTimer::getTime() + m_iSndTimeOut * uint64_t(1000);
                  timespec locktime;

                  locktime.tv_sec = exptime / 1000000;
                  locktime.tv_nsec = (exptime % 1000000) * 1000;

                  while (stillConnected() && (sndBuffersLeft() <= 0) && m_bPeerHealth && (CTimer::getTime() < exptime))
                      pthread_cond_timedwait(&m_SendBlockCond, &m_SendBlockLock, &locktime);
              }
          }

         // check the connection status
         if (m_bBroken || m_bClosing)
            throw CUDTException(MJ_CONNECTION, MN_CONNLOST, 0);
         else if (!m_bConnected)
            throw CUDTException(MJ_CONNECTION, MN_NOCONN, 0);
         else if (!m_bPeerHealth)
         {
            m_bPeerHealth = true;
            throw CUDTException(MJ_PEERERROR);
         }
      }
   }

   if (sndBuffersLeft() <= 0)
   {
      if (m_iSndTimeOut >= 0)
         throw CUDTException(MJ_AGAIN, MN_XMTIMEOUT, 0);

      return 0;
   }

   int size = min(len, sndBuffersLeft() * m_iMaxSRTPayloadSize);

   // record total time used for sending
   if (m_pSndBuffer->getCurrBufSize() == 0)
      m_llSndDurationCounter = CTimer::getTime();

   // insert the user buffer into the sending list
   m_pSndBuffer->addBuffer(data, size); // inorder=false, ttl=-1

   // insert this socket to snd list if it is not on the list yet
   m_pSndQueue->m_pSndUList->update(this, CSndUList::DONT_RESCHEDULE);

   if (sndBuffersLeft() <= 0)
   {
      // write is not available any more
      s_UDTUnited.m_EPoll.update_events(m_SocketID, m_sPollID, UDT_EPOLL_OUT, false);
   }

   return size;
}
*/

int CUDT::receiveBuffer(char *data, int len)
{
    if (!m_CongCtl->checkTransArgs(SrtCongestion::STA_BUFFER, SrtCongestion::STAD_RECV, data, len, -1, false))
        throw CUDTException(MJ_NOTSUP, MN_INVALBUFFERAPI, 0);

    CGuard recvguard(m_RecvLock);

    if ((m_bBroken || m_bClosing) && !m_pRcvBuffer->isRcvDataReady())
    {
        if (m_bShutdown)
        {
            // For stream API, return 0 as a sign of EOF for transmission.
            // That's a bit controversial because theoretically the
            // UMSG_SHUTDOWN message may be lost as every UDP packet, although
            // another theory states that this will never happen because this
            // packet has a total size of 42 bytes and such packets are
            // declared as never dropped - but still, this is UDP so there's no
            // guarantee.

            // The most reliable way to inform the party that the transmission
            // has ended would be to send a single empty packet (that is,
            // a data packet that contains only an SRT header in the UDP
            // payload), which is a normal data packet that can undergo
            // normal sequence check and retransmission rules, so it's ensured
            // that this packet will be received. Receiving such a packet should
            // make this function return 0, potentially also without breaking
            // the connection and potentially also with losing no ability to
            // send some larger portion of data next time.
            HLOGC(mglog.Debug, log << "STREAM API, SHUTDOWN: marking as EOF");
            return 0;
        }
        HLOGC(mglog.Debug,
              log << (m_bMessageAPI ? "MESSAGE" : "STREAM") << " API, " << (m_bShutdown ? "" : "no")
                  << " SHUTDOWN. Reporting as BROKEN.");
        throw CUDTException(MJ_CONNECTION, MN_CONNLOST, 0);
    }

    if (!m_pRcvBuffer->isRcvDataReady())
    {
        if (!m_bSynRecving)
        {
            throw CUDTException(MJ_AGAIN, MN_RDAVAIL, 0);
        }
        else
        {
            /* Kick TsbPd thread to schedule next wakeup (if running) */
            if (m_iRcvTimeOut < 0)
            {
                while (stillConnected() && !m_pRcvBuffer->isRcvDataReady())
                {
                    // Do not block forever, check connection status each 1 sec.
                    CTimer::condTimedWaitUS(&m_RecvDataCond, &m_RecvLock, 1000000);
                }
            }
            else
            {
                uint64_t exptime = CTimer::getTime() + m_iRcvTimeOut * 1000;
                while (stillConnected() && !m_pRcvBuffer->isRcvDataReady())
                {
                    CTimer::condTimedWaitUS(&m_RecvDataCond, &m_RecvLock, m_iRcvTimeOut * 1000);
                    if (CTimer::getTime() >= exptime)
                        break;
                }
            }
        }
    }

    // throw an exception if not connected
    if (!m_bConnected)
        throw CUDTException(MJ_CONNECTION, MN_NOCONN, 0);

    if ((m_bBroken || m_bClosing) && !m_pRcvBuffer->isRcvDataReady())
    {
        // See at the beginning
        if (!m_bMessageAPI && m_bShutdown)
        {
            HLOGC(mglog.Debug, log << "STREAM API, SHUTDOWN: marking as EOF");
            return 0;
        }
        HLOGC(mglog.Debug,
              log << (m_bMessageAPI ? "MESSAGE" : "STREAM") << " API, " << (m_bShutdown ? "" : "no")
                  << " SHUTDOWN. Reporting as BROKEN.");

        throw CUDTException(MJ_CONNECTION, MN_CONNLOST, 0);
    }

    const int res = m_pRcvBuffer->readBuffer(data, len);

    /* Kick TsbPd thread to schedule next wakeup (if running) */
    if (m_bTsbPd)
    {
        HLOGP(tslog.Debug, "Ping TSBPD thread to schedule wakeup");
        pthread_cond_signal(&m_RcvTsbPdCond);
    }

    if (!m_pRcvBuffer->isRcvDataReady())
    {
        // read is not available any more
        s_UDTUnited.m_EPoll.update_events(m_SocketID, m_sPollID, UDT_EPOLL_IN, false);
    }

    if ((res <= 0) && (m_iRcvTimeOut >= 0))
        throw CUDTException(MJ_AGAIN, MN_XMTIMEOUT, 0);

    return res;
}

void CUDT::checkNeedDrop(ref_t<bool> bCongestion)
{
    if (!m_bPeerTLPktDrop)
        return;

    if (!m_bMessageAPI)
    {
        LOGC(dlog.Error, log << "The SRTO_TLPKTDROP flag can only be used with message API.");
        throw CUDTException(MJ_NOTSUP, MN_INVALBUFFERAPI, 0);
    }

    int bytes, timespan_ms;
    // (returns buffer size in buffer units, ignored)
    m_pSndBuffer->getCurrBufSize(Ref(bytes), Ref(timespan_ms));

    // high threshold (msec) at tsbpd_delay plus sender/receiver reaction time (2 * 10ms)
    // Minimum value must accomodate an I-Frame (~8 x average frame size)
    // >>need picture rate or app to set min treshold
    // >>using 1 sec for worse case 1 frame using all bit budget.
    // picture rate would be useful in auto SRT setting for min latency
    // XXX Make SRT_TLPKTDROP_MINTHRESHOLD_MS option-configurable
    int threshold_ms = 0;
    if (m_iOPT_SndDropDelay >= 0)
    {
        threshold_ms = std::max(m_iPeerTsbPdDelay_ms + m_iOPT_SndDropDelay, +SRT_TLPKTDROP_MINTHRESHOLD_MS) +
                       (2 * COMM_SYN_INTERVAL_US / 1000);
    }

    if (threshold_ms && timespan_ms > threshold_ms)
    {
        // protect packet retransmission
        CGuard::enterCS(m_RecvAckLock);
        int dbytes;
        int dpkts = m_pSndBuffer->dropLateData(dbytes, CTimer::getTime() - (threshold_ms * 1000));
        if (dpkts > 0)
        {
            CGuard::enterCS(m_StatsLock);
            m_stats.traceSndDrop += dpkts;
            m_stats.sndDropTotal += dpkts;
            m_stats.traceSndBytesDrop += dbytes;
            m_stats.sndBytesDropTotal += dbytes;
            CGuard::leaveCS(m_StatsLock);

#if ENABLE_HEAVY_LOGGING
            int32_t realack = m_iSndLastDataAck;
#endif
            int32_t fakeack = CSeqNo::incseq(m_iSndLastDataAck, dpkts);

            m_iSndLastAck     = fakeack;
            m_iSndLastDataAck = fakeack;

            int32_t minlastack = CSeqNo::decseq(m_iSndLastDataAck);
            m_pSndLossList->remove(minlastack);
            /* If we dropped packets not yet sent, advance current position */
            // THIS MEANS: m_iSndCurrSeqNo = MAX(m_iSndCurrSeqNo, m_iSndLastDataAck-1)
            if (CSeqNo::seqcmp(m_iSndCurrSeqNo, minlastack) < 0)
            {
                m_iSndCurrSeqNo = minlastack;
            }
            LOGC(dlog.Error, log << "SND-DROPPED " << dpkts << " packets - lost delaying for " << timespan_ms << "ms");

            HLOGC(dlog.Debug,
                  log << "drop,now " << CTimer::getTime() << "us," << realack << "-" << m_iSndCurrSeqNo << " seqs,"
                      << dpkts << " pkts," << dbytes << " bytes," << timespan_ms << " ms");
        }
        *bCongestion = true;
        CGuard::leaveCS(m_RecvAckLock);
    }
    else if (timespan_ms > (m_iPeerTsbPdDelay_ms / 2))
    {
        HLOGC(mglog.Debug,
              log << "cong, NOW: " << CTimer::getTime() << "us, BYTES " << bytes << ", TMSPAN " << timespan_ms << "ms");

        *bCongestion = true;
    }
}

int CUDT::sendmsg(const char *data, int len, int msttl, bool inorder, uint64_t srctime)
{
    SRT_MSGCTRL mctrl = srt_msgctrl_default;
    mctrl.msgttl      = msttl;
    mctrl.inorder     = inorder;
    mctrl.srctime     = srctime;
    return this->sendmsg2(data, len, Ref(mctrl));
}

int CUDT::sendmsg2(const char *data, int len, ref_t<SRT_MSGCTRL> r_mctrl)
{
    SRT_MSGCTRL &mctrl       = *r_mctrl;
    bool         bCongestion = false;

    // throw an exception if not connected
    if (m_bBroken || m_bClosing)
        throw CUDTException(MJ_CONNECTION, MN_CONNLOST, 0);
    else if (!m_bConnected || !m_CongCtl.ready())
        throw CUDTException(MJ_CONNECTION, MN_NOCONN, 0);

    if (len <= 0)
    {
        LOGC(dlog.Error, log << "INVALID: Data size for sending declared with length: " << len);
        return 0;
    }

    int  msttl   = mctrl.msgttl;
    bool inorder = mctrl.inorder;

    // Sendmsg isn't restricted to the congctl type, however the congctl
    // may want to have something to say here.
    // NOTE: SrtCongestion is also allowed to throw CUDTException() by itself!
    {
        SrtCongestion::TransAPI api = SrtCongestion::STA_MESSAGE;
        CodeMinor               mn  = MN_INVALMSGAPI;
        if (!m_bMessageAPI)
        {
            api = SrtCongestion::STA_BUFFER;
            mn  = MN_INVALBUFFERAPI;
        }

        if (!m_CongCtl->checkTransArgs(api, SrtCongestion::STAD_SEND, data, len, msttl, inorder))
            throw CUDTException(MJ_NOTSUP, mn, 0);
    }

    // NOTE: the length restrictions differ in STREAM API and in MESSAGE API:

    // - STREAM API:
    //   At least 1 byte free sending buffer space is needed
    //   (in practice, one unit buffer of 1456 bytes).
    //   This function will send as much as possible, and return
    //   how much was actually sent.

    // - MESSAGE API:
    //   At least so many bytes free in the sending buffer is needed,
    //   as the length of the data, otherwise this function will block
    //   or return MJ_AGAIN until this condition is satisfied. The EXACTLY
    //   such number of data will be then written out, and this function
    //   will effectively return either -1 (error) or the value of 'len'.
    //   This call will be also rejected from upside when trying to send
    //   out a message of a length that exceeds the total size of the sending
    //   buffer (configurable by SRTO_SNDBUF).

    if (m_bMessageAPI && len > int(m_iSndBufSize * m_iMaxSRTPayloadSize))
    {
        LOGC(dlog.Error,
             log << "Message length (" << len << ") exceeds the size of sending buffer: "
                 << (m_iSndBufSize * m_iMaxSRTPayloadSize) << ". Use SRTO_SNDBUF if needed.");
        throw CUDTException(MJ_NOTSUP, MN_XSIZE, 0);
    }

    /* XXX
       This might be worth preserving for several occasions, but it
       must be at least conditional because it breaks backward compat.
    if (!m_pCryptoControl || !m_pCryptoControl->isSndEncryptionOK())
    {
        LOGC(dlog.Error, log << "Encryption is required, but the peer did not supply correct credentials. Sending
    rejected."); throw CUDTException(MJ_SETUP, MN_SECURITY, 0);
    }
    */

    CGuard sendguard(m_SendLock);

    if (m_pSndBuffer->getCurrBufSize() == 0)
    {
        // delay the EXP timer to avoid mis-fired timeout
        uint64_t currtime_tk;
        CTimer::rdtsc(currtime_tk);

        CGuard ack_lock(m_RecvAckLock);
        m_ullLastRspAckTime_tk = currtime_tk; // (fix keepalive)
        m_iReXmitCount         = 1; // can be modified in checkRexmitTimer and processCtrlAck (receiver's thread)
    }

    // checkNeedDrop(...) may lock m_RecvAckLock
    // to modify m_pSndBuffer and m_pSndLossList
    checkNeedDrop(Ref(bCongestion));

    int minlen = 1; // Minimum sender buffer space required for STREAM API
    if (m_bMessageAPI)
    {
        // For MESSAGE API the minimum outgoing buffer space required is
        // the size that can carry over the whole message as passed here.
        minlen = (len + m_iMaxSRTPayloadSize - 1) / m_iMaxSRTPayloadSize;
    }

    if (sndBuffersLeft() < minlen)
    {
        //>>We should not get here if SRT_ENABLE_TLPKTDROP
        // XXX Check if this needs to be removed, or put to an 'else' condition for m_bTLPktDrop.
        if (!m_bSynSending)
            throw CUDTException(MJ_AGAIN, MN_WRAVAIL, 0);

        {
            // wait here during a blocking sending
            CGuard sendblock_lock(m_SendBlockLock);

            if (m_iSndTimeOut < 0)
            {
                while (stillConnected() && sndBuffersLeft() < minlen && m_bPeerHealth)
                    pthread_cond_wait(&m_SendBlockCond, &m_SendBlockLock);
            }
            else
            {
                const uint64_t exptime = CTimer::getTime() + m_iSndTimeOut * uint64_t(1000);

                while (stillConnected() && sndBuffersLeft() < minlen && m_bPeerHealth && exptime > CTimer::getTime())
                    CTimer::condTimedWaitUS(&m_SendBlockCond, &m_SendBlockLock, m_iSndTimeOut * uint64_t(1000));
            }
        }

        // check the connection status
        if (m_bBroken || m_bClosing)
            throw CUDTException(MJ_CONNECTION, MN_CONNLOST, 0);
        else if (!m_bConnected)
            throw CUDTException(MJ_CONNECTION, MN_NOCONN, 0);
        else if (!m_bPeerHealth)
        {
            m_bPeerHealth = true;
            throw CUDTException(MJ_PEERERROR);
        }

        /*
         * The code below is to return ETIMEOUT when blocking mode could not get free buffer in time.
         * If no free buffer available in non-blocking mode, we alredy returned. If buffer availaible,
         * we test twice if this code is outside the else section.
         * This fix move it in the else (blocking-mode) section
         */
        if (sndBuffersLeft() < minlen)
        {
            if (m_iSndTimeOut >= 0)
                throw CUDTException(MJ_AGAIN, MN_XMTIMEOUT, 0);

            // XXX This looks very weird here, however most likely
            // this will happen only in the following case, when
            // the above loop has been interrupted, which happens when:
            // 1. The buffers left gets enough for minlen - but this is excluded
            //    in the first condition here.
            // 2. In the case of sending timeout, the above loop was interrupted
            //    due to reaching timeout, but this is excluded by the second
            //    condition here
            // 3. The 'stillConnected()' or m_bPeerHealth condition is false, of which:
            //    - broken/closing status is checked and responded with CONNECTION/CONNLOST
            //    - not connected status is checked and responded with CONNECTION/NOCONN
            //    - m_bPeerHealth condition is checked and responded with PEERERROR
            //
            // ERGO: never happens?
            LOGC(mglog.Fatal,
                 log << "IPE: sendmsg: the loop exited, while not enough size, still connected, peer healthy. "
                        "Impossible.");

            return 0;
        }
    }

    // If the sender's buffer is empty,
    // record total time used for sending
    if (m_pSndBuffer->getCurrBufSize() == 0)
    {
        CGuard::enterCS(m_StatsLock);
        m_stats.sndDurationCounter = CTimer::getTime();
        CGuard::leaveCS(m_StatsLock);
    }

    int size = len;
    if (!m_bMessageAPI)
    {
        // For STREAM API it's allowed to send less bytes than the given buffer.
        // Just return how many bytes were actually scheduled for writing.
        // XXX May be reasonable to add a flag that requires that the function
        // not return until the buffer is sent completely.
        size = min(len, sndBuffersLeft() * m_iMaxSRTPayloadSize);
    }

    {
        CGuard recvAckLock(m_RecvAckLock);
        // insert the user buffer into the sending list
        // This should be protected by a mutex. m_SendLock does this.
        m_pSndBuffer->addBuffer(data, size, mctrl.msgttl, mctrl.inorder, mctrl.srctime, Ref(mctrl.msgno));
        HLOGC(dlog.Debug, log << CONID() << "sock:SENDING srctime: " << mctrl.srctime << "us DATA SIZE: " << size);

        if (sndBuffersLeft() < 1) // XXX Not sure if it should test if any space in the buffer, or as requried.
        {
            // write is not available any more
            s_UDTUnited.m_EPoll.update_events(m_SocketID, m_sPollID, UDT_EPOLL_OUT, false);
        }
    }

    // insert this socket to the snd list if it is not on the list yet
    // m_pSndUList->pop may lock CSndUList::m_ListLock and then m_RecvAckLock
    m_pSndQueue->m_pSndUList->update(this, CSndUList::rescheduleIf(bCongestion));

#ifdef SRT_ENABLE_ECN
    if (bCongestion)
        throw CUDTException(MJ_AGAIN, MN_CONGESTION, 0);
#endif /* SRT_ENABLE_ECN */
    return size;
}

int CUDT::recv(char *data, int len)
{
    if (!m_bConnected || !m_CongCtl.ready())
        throw CUDTException(MJ_CONNECTION, MN_NOCONN, 0);

    if (len <= 0)
    {
        LOGC(dlog.Error, log << "Length of '" << len << "' supplied to srt_recv.");
        throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);
    }

    if (m_bMessageAPI)
    {
        SRT_MSGCTRL mctrl = srt_msgctrl_default;
        return receiveMessage(data, len, Ref(mctrl));
    }

    return receiveBuffer(data, len);
}

int CUDT::recvmsg(char *data, int len, uint64_t &srctime)
{
    if (!m_bConnected || !m_CongCtl.ready())
        throw CUDTException(MJ_CONNECTION, MN_NOCONN, 0);

    if (len <= 0)
    {
        LOGC(dlog.Error, log << "Length of '" << len << "' supplied to srt_recvmsg.");
        throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);
    }

    if (m_bMessageAPI)
    {
        SRT_MSGCTRL mctrl = srt_msgctrl_default;
        int         ret   = receiveMessage(data, len, Ref(mctrl));
        srctime           = mctrl.srctime;
        return ret;
    }

    return receiveBuffer(data, len);
}

int CUDT::recvmsg2(char *data, int len, ref_t<SRT_MSGCTRL> mctrl)
{
    if (!m_bConnected || !m_CongCtl.ready())
        throw CUDTException(MJ_CONNECTION, MN_NOCONN, 0);

    if (len <= 0)
    {
        LOGC(dlog.Error, log << "Length of '" << len << "' supplied to srt_recvmsg.");
        throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);
    }

    if (m_bMessageAPI)
        return receiveMessage(data, len, mctrl);

    return receiveBuffer(data, len);
}

int CUDT::receiveMessage(char *data, int len, ref_t<SRT_MSGCTRL> r_mctrl)
{
    SRT_MSGCTRL &mctrl = *r_mctrl;
    // Recvmsg isn't restricted to the congctl type, it's the most
    // basic method of passing the data. You can retrieve data as
    // they come in, however you need to match the size of the buffer.
    if (!m_CongCtl->checkTransArgs(SrtCongestion::STA_MESSAGE, SrtCongestion::STAD_RECV, data, len, -1, false))
        throw CUDTException(MJ_NOTSUP, MN_INVALMSGAPI, 0);

    CGuard recvguard(m_RecvLock);

    /* XXX DEBUG STUFF - enable when required
       char charbool[2] = {'0', '1'};
       char ptrn [] = "RECVMSG/BEGIN BROKEN 1 CONN 1 CLOSING 1 SYNCR 1 NMSG                                ";
       int pos [] = {21, 28, 38, 46, 53};
       ptrn[pos[0]] = charbool[m_bBroken];
       ptrn[pos[1]] = charbool[m_bConnected];
       ptrn[pos[2]] = charbool[m_bClosing];
       ptrn[pos[3]] = charbool[m_bSynRecving];
       int wrtlen = sprintf(ptrn + pos[4], "%d", m_pRcvBuffer->getRcvMsgNum());
       strcpy(ptrn + pos[4] + wrtlen, "\n");
       fputs(ptrn, stderr);
    // */

    if (m_bBroken || m_bClosing)
    {
        int res       = m_pRcvBuffer->readMsg(data, len);
        mctrl.srctime = 0;

        /* Kick TsbPd thread to schedule next wakeup (if running) */
        if (m_bTsbPd)
            pthread_cond_signal(&m_RcvTsbPdCond);

        if (!m_pRcvBuffer->isRcvDataReady())
        {
            // read is not available any more
            s_UDTUnited.m_EPoll.update_events(m_SocketID, m_sPollID, UDT_EPOLL_IN, false);
        }

        if (res == 0)
        {
            if (!m_bMessageAPI && m_bShutdown)
                return 0;
            throw CUDTException(MJ_CONNECTION, MN_CONNLOST, 0);
        }
        else
            return res;
    }

    if (!m_bSynRecving)
    {

        int res = m_pRcvBuffer->readMsg(data, len, r_mctrl);
        if (res == 0)
        {
            // read is not available any more

            // Kick TsbPd thread to schedule next wakeup (if running)
            if (m_bTsbPd)
                pthread_cond_signal(&m_RcvTsbPdCond);

            // Shut up EPoll if no more messages in non-blocking mode
            s_UDTUnited.m_EPoll.update_events(m_SocketID, m_sPollID, UDT_EPOLL_IN, false);
            throw CUDTException(MJ_AGAIN, MN_RDAVAIL, 0);
        }
        else
        {
            if (!m_pRcvBuffer->isRcvDataReady())
            {
                // Kick TsbPd thread to schedule next wakeup (if running)
                if (m_bTsbPd)
                    pthread_cond_signal(&m_RcvTsbPdCond);

                // Shut up EPoll if no more messages in non-blocking mode
                s_UDTUnited.m_EPoll.update_events(m_SocketID, m_sPollID, UDT_EPOLL_IN, false);

                // After signaling the tsbpd for ready data, report the bandwidth.
                double bw SRT_ATR_UNUSED = Bps2Mbps(m_iBandwidth * m_iMaxSRTPayloadSize);
                HLOGC(mglog.Debug,
                      log << CONID() << "CURRENT BANDWIDTH: " << bw << "Mbps (" << m_iBandwidth
                          << " buffers per second)");
            }
            return res;
        }
    }

    int  res     = 0;
    bool timeout = false;
    // Do not block forever, check connection status each 1 sec.
    uint64_t recvtmo = m_iRcvTimeOut < 0 ? 1000 : m_iRcvTimeOut;

    do
    {
        if (stillConnected() && !timeout && (!m_pRcvBuffer->isRcvDataReady()))
        {
            /* Kick TsbPd thread to schedule next wakeup (if running) */
            if (m_bTsbPd)
            {
                HLOGP(tslog.Debug, "recvmsg: KICK tsbpd()");
                pthread_cond_signal(&m_RcvTsbPdCond);
            }

            do
            {
                if (CTimer::condTimedWaitUS(&m_RecvDataCond, &m_RecvLock, recvtmo * 1000) == ETIMEDOUT)
                {
                    if (!(m_iRcvTimeOut < 0))
                        timeout = true;
                    HLOGP(tslog.Debug, "recvmsg: DATA COND: EXPIRED -- trying to get data anyway");
                }
                else
                {
                    HLOGP(tslog.Debug, "recvmsg: DATA COND: KICKED.");
                }
            } while (stillConnected() && !timeout && (!m_pRcvBuffer->isRcvDataReady()));
        }

        /* XXX DEBUG STUFF - enable when required
        LOGC(dlog.Debug, "RECVMSG/GO-ON BROKEN " << m_bBroken << " CONN " << m_bConnected
                << " CLOSING " << m_bClosing << " TMOUT " << timeout
                << " NMSG " << m_pRcvBuffer->getRcvMsgNum());
                */

        res = m_pRcvBuffer->readMsg(data, len, r_mctrl);

        if (m_bBroken || m_bClosing)
        {
            if (!m_bMessageAPI && m_bShutdown)
                return 0;
            throw CUDTException(MJ_CONNECTION, MN_CONNLOST, 0);
        }
        else if (!m_bConnected)
            throw CUDTException(MJ_CONNECTION, MN_NOCONN, 0);
    } while ((res == 0) && !timeout);

    if (!m_pRcvBuffer->isRcvDataReady())
    {
        // Falling here means usually that res == 0 && timeout == true.
        // res == 0 would repeat the above loop, unless there was also a timeout.
        // timeout has interrupted the above loop, but with res > 0 this condition
        // wouldn't be satisfied.

        // read is not available any more

        // Kick TsbPd thread to schedule next wakeup (if running)
        if (m_bTsbPd)
        {
            HLOGP(tslog.Debug, "recvmsg: KICK tsbpd() (buffer empty)");
            pthread_cond_signal(&m_RcvTsbPdCond);
        }

        // Shut up EPoll if no more messages in non-blocking mode
        s_UDTUnited.m_EPoll.update_events(m_SocketID, m_sPollID, UDT_EPOLL_IN, false);
    }

    // Unblock when required
    // LOGC(tslog.Debug, "RECVMSG/EXIT RES " << res << " RCVTIMEOUT");

    if ((res <= 0) && (m_iRcvTimeOut >= 0))
        throw CUDTException(MJ_AGAIN, MN_XMTIMEOUT, 0);

    return res;
}

int64_t CUDT::sendfile(fstream &ifs, int64_t &offset, int64_t size, int block)
{
    if (m_bBroken || m_bClosing)
        throw CUDTException(MJ_CONNECTION, MN_CONNLOST, 0);
    else if (!m_bConnected || !m_CongCtl.ready())
        throw CUDTException(MJ_CONNECTION, MN_NOCONN, 0);

    if (size <= 0 && size != -1)
        return 0;

    if (!m_CongCtl->checkTransArgs(SrtCongestion::STA_FILE, SrtCongestion::STAD_SEND, 0, size, -1, false))
        throw CUDTException(MJ_NOTSUP, MN_INVALBUFFERAPI, 0);

    if (!m_pCryptoControl || !m_pCryptoControl->isSndEncryptionOK())
    {
        LOGC(dlog.Error,
             log << "Encryption is required, but the peer did not supply correct credentials. Sending rejected.");
        throw CUDTException(MJ_SETUP, MN_SECURITY, 0);
    }

    CGuard sendguard(m_SendLock);

    if (m_pSndBuffer->getCurrBufSize() == 0)
    {
        // delay the EXP timer to avoid mis-fired timeout
        uint64_t currtime_tk;
        CTimer::rdtsc(currtime_tk);
        // (fix keepalive) m_ullLastRspTime_tk = currtime_tk;
        m_ullLastRspAckTime_tk = currtime_tk;
        m_iReXmitCount         = 1;
    }

    // positioning...
    try
    {
        if (size == -1)
        {
            ifs.seekg(0, std::ios::end);
            size = ifs.tellg();
            if (offset > size)
                throw 0; // let it be caught below
        }

        // This will also set the position back to the beginning
        // in case when it was moved to the end for measuring the size.
        // This will also fail if the offset exceeds size, so measuring
        // the size can be skipped if not needed.
        ifs.seekg((streamoff)offset);
        if (!ifs.good())
            throw 0;
    }
    catch (...)
    {
        // XXX It would be nice to note that this is reported
        // by exception only if explicitly requested by setting
        // the exception flags in the stream. Here it's fixed so
        // that when this isn't set, the exception is "thrown manually".
        throw CUDTException(MJ_FILESYSTEM, MN_SEEKGFAIL);
    }

    int64_t tosend = size;
    int     unitsize;

    // sending block by block
    while (tosend > 0)
    {
        if (ifs.fail())
            throw CUDTException(MJ_FILESYSTEM, MN_WRITEFAIL);

        if (ifs.eof())
            break;

        unitsize = int((tosend >= block) ? block : tosend);

        {
            CGuard lk(m_SendBlockLock);

            while (stillConnected() && (sndBuffersLeft() <= 0) && m_bPeerHealth)
                pthread_cond_wait(&m_SendBlockCond, &m_SendBlockLock);
        }

        if (m_bBroken || m_bClosing)
            throw CUDTException(MJ_CONNECTION, MN_CONNLOST, 0);
        else if (!m_bConnected)
            throw CUDTException(MJ_CONNECTION, MN_NOCONN, 0);
        else if (!m_bPeerHealth)
        {
            // reset peer health status, once this error returns, the app should handle the situation at the peer side
            m_bPeerHealth = true;
            throw CUDTException(MJ_PEERERROR);
        }

        // record total time used for sending
        if (m_pSndBuffer->getCurrBufSize() == 0)
        {
            CGuard::enterCS(m_StatsLock);
            m_stats.sndDurationCounter = CTimer::getTime();
            CGuard::leaveCS(m_StatsLock);
        }

        {
            CGuard        recvAckLock(m_RecvAckLock);
            const int64_t sentsize = m_pSndBuffer->addBufferFromFile(ifs, unitsize);

            if (sentsize > 0)
            {
                tosend -= sentsize;
                offset += sentsize;
            }

            if (sndBuffersLeft() <= 0)
            {
                // write is not available any more
                s_UDTUnited.m_EPoll.update_events(m_SocketID, m_sPollID, UDT_EPOLL_OUT, false);
            }
        }

        // insert this socket to snd list if it is not on the list yet
        m_pSndQueue->m_pSndUList->update(this, CSndUList::DONT_RESCHEDULE);
    }

    return size - tosend;
}

int64_t CUDT::recvfile(fstream &ofs, int64_t &offset, int64_t size, int block)
{
    if (!m_bConnected || !m_CongCtl.ready())
        throw CUDTException(MJ_CONNECTION, MN_NOCONN, 0);
    else if ((m_bBroken || m_bClosing) && !m_pRcvBuffer->isRcvDataReady())
    {
        if (!m_bMessageAPI && m_bShutdown)
            return 0;
        throw CUDTException(MJ_CONNECTION, MN_CONNLOST, 0);
    }

    if (size <= 0)
        return 0;

    if (!m_CongCtl->checkTransArgs(SrtCongestion::STA_FILE, SrtCongestion::STAD_RECV, 0, size, -1, false))
        throw CUDTException(MJ_NOTSUP, MN_INVALBUFFERAPI, 0);

    if (m_bTsbPd)
    {
        LOGC(dlog.Error, log << "Reading from file is incompatible with TSBPD mode and would cause a deadlock\n");
        throw CUDTException(MJ_NOTSUP, MN_INVALBUFFERAPI, 0);
    }

    CGuard recvguard(m_RecvLock);

    // Well, actually as this works over a FILE (fstream), not just a stream,
    // the size can be measured anyway and predicted if setting the offset might
    // have a chance to work or not.

    // positioning...
    try
    {
        if (offset > 0)
        {
            // Don't do anything around here if the offset == 0, as this
            // is the default offset after opening. Whether this operation
            // is performed correctly, it highly depends on how the file
            // has been open. For example, if you want to overwrite parts
            // of an existing file, the file must exist, and the ios::trunc
            // flag must not be set. If the file is open for only ios::out,
            // then the file will be truncated since the offset position on
            // at the time when first written; if ios::in|ios::out, then
            // it won't be truncated, just overwritten.

            // What is required here is that if offset is 0, don't try to
            // change the offset because this might be impossible with
            // the current flag set anyway.

            // Also check the status and CAUSE exception manually because
            // you don't know, as well, whether the user has set exception
            // flags.

            ofs.seekp((streamoff)offset);
            if (!ofs.good())
                throw 0; // just to get caught :)
        }
    }
    catch (...)
    {
        // XXX It would be nice to note that this is reported
        // by exception only if explicitly requested by setting
        // the exception flags in the stream. For a case, when it's not,
        // an additional explicit throwing happens when failbit is set.
        throw CUDTException(MJ_FILESYSTEM, MN_SEEKPFAIL);
    }

    int64_t torecv   = size;
    int     unitsize = block;
    int     recvsize;

    // receiving... "recvfile" is always blocking
    while (torecv > 0)
    {
        if (ofs.fail())
        {
            // send the sender a signal so it will not be blocked forever
            int32_t err_code = CUDTException::EFILE;
            sendCtrl(UMSG_PEERERROR, &err_code);

            throw CUDTException(MJ_FILESYSTEM, MN_WRITEFAIL);
        }

        pthread_mutex_lock(&m_RecvDataLock);
        while (stillConnected() && !m_pRcvBuffer->isRcvDataReady())
            pthread_cond_wait(&m_RecvDataCond, &m_RecvDataLock);
        pthread_mutex_unlock(&m_RecvDataLock);

        if (!m_bConnected)
            throw CUDTException(MJ_CONNECTION, MN_NOCONN, 0);
        else if ((m_bBroken || m_bClosing) && !m_pRcvBuffer->isRcvDataReady())
        {

            if (!m_bMessageAPI && m_bShutdown)
                return 0;
            throw CUDTException(MJ_CONNECTION, MN_CONNLOST, 0);
        }

        unitsize = int((torecv == -1 || torecv >= block) ? block : torecv);
        recvsize = m_pRcvBuffer->readBufferToFile(ofs, unitsize);

        if (recvsize > 0)
        {
            torecv -= recvsize;
            offset += recvsize;
        }
    }

    if (!m_pRcvBuffer->isRcvDataReady())
    {
        // read is not available any more
        s_UDTUnited.m_EPoll.update_events(m_SocketID, m_sPollID, UDT_EPOLL_IN, false);
    }

    return size - torecv;
}

void CUDT::bstats(CBytePerfMon *perf, bool clear, bool instantaneous)
{
    if (!m_bConnected)
        throw CUDTException(MJ_CONNECTION, MN_NOCONN, 0);
    if (m_bBroken || m_bClosing)
        throw CUDTException(MJ_CONNECTION, MN_CONNLOST, 0);

    CGuard statsguard(m_StatsLock);

    uint64_t currtime = CTimer::getTime();
    perf->msTimeStamp = (currtime - m_stats.startTime) / 1000;

    perf->pktSent              = m_stats.traceSent;
    perf->pktRecv              = m_stats.traceRecv;
    perf->pktSndLoss           = m_stats.traceSndLoss;
    perf->pktRcvLoss           = m_stats.traceRcvLoss;
    perf->pktRetrans           = m_stats.traceRetrans;
    perf->pktRcvRetrans        = m_stats.traceRcvRetrans;
    perf->pktSentACK           = m_stats.sentACK;
    perf->pktRecvACK           = m_stats.recvACK;
    perf->pktSentNAK           = m_stats.sentNAK;
    perf->pktRecvNAK           = m_stats.recvNAK;
    perf->usSndDuration        = m_stats.sndDuration;
    perf->pktReorderDistance   = m_stats.traceReorderDistance;
    perf->pktReorderTolerance  = m_iReorderTolerance;
    perf->pktRcvAvgBelatedTime = m_stats.traceBelatedTime;
    perf->pktRcvBelated        = m_stats.traceRcvBelated;

    perf->pktSndFilterExtra  = m_stats.sndFilterExtra;
    perf->pktRcvFilterExtra  = m_stats.rcvFilterExtra;
    perf->pktRcvFilterSupply = m_stats.rcvFilterSupply;
    perf->pktRcvFilterLoss   = m_stats.rcvFilterLoss;

    /* perf byte counters include all headers (SRT+UDP+IP) */
    const int pktHdrSize = CPacket::HDR_SIZE + CPacket::UDP_HDR_SIZE;
    perf->byteSent       = m_stats.traceBytesSent + (m_stats.traceSent * pktHdrSize);
    perf->byteRecv       = m_stats.traceBytesRecv + (m_stats.traceRecv * pktHdrSize);
    perf->byteRetrans    = m_stats.traceBytesRetrans + (m_stats.traceRetrans * pktHdrSize);
#ifdef SRT_ENABLE_LOSTBYTESCOUNT
    perf->byteRcvLoss = m_stats.traceRcvBytesLoss + (m_stats.traceRcvLoss * pktHdrSize);
#endif

    perf->pktSndDrop  = m_stats.traceSndDrop;
    perf->pktRcvDrop  = m_stats.traceRcvDrop + m_stats.traceRcvUndecrypt;
    perf->byteSndDrop = m_stats.traceSndBytesDrop + (m_stats.traceSndDrop * pktHdrSize);
    perf->byteRcvDrop =
        m_stats.traceRcvBytesDrop + (m_stats.traceRcvDrop * pktHdrSize) + m_stats.traceRcvBytesUndecrypt;
    perf->pktRcvUndecrypt  = m_stats.traceRcvUndecrypt;
    perf->byteRcvUndecrypt = m_stats.traceRcvBytesUndecrypt;

    perf->pktSentTotal       = m_stats.sentTotal;
    perf->pktRecvTotal       = m_stats.recvTotal;
    perf->pktSndLossTotal    = m_stats.sndLossTotal;
    perf->pktRcvLossTotal    = m_stats.rcvLossTotal;
    perf->pktRetransTotal    = m_stats.retransTotal;
    perf->pktSentACKTotal    = m_stats.sentACKTotal;
    perf->pktRecvACKTotal    = m_stats.recvACKTotal;
    perf->pktSentNAKTotal    = m_stats.sentNAKTotal;
    perf->pktRecvNAKTotal    = m_stats.recvNAKTotal;
    perf->usSndDurationTotal = m_stats.m_sndDurationTotal;

    perf->byteSentTotal           = m_stats.bytesSentTotal + (m_stats.sentTotal * pktHdrSize);
    perf->byteRecvTotal           = m_stats.bytesRecvTotal + (m_stats.recvTotal * pktHdrSize);
    perf->byteRetransTotal        = m_stats.bytesRetransTotal + (m_stats.retransTotal * pktHdrSize);
    perf->pktSndFilterExtraTotal  = m_stats.sndFilterExtraTotal;
    perf->pktRcvFilterExtraTotal  = m_stats.rcvFilterExtraTotal;
    perf->pktRcvFilterSupplyTotal = m_stats.rcvFilterSupplyTotal;
    perf->pktRcvFilterLossTotal   = m_stats.rcvFilterLossTotal;

#ifdef SRT_ENABLE_LOSTBYTESCOUNT
    perf->byteRcvLossTotal = m_stats.rcvBytesLossTotal + (m_stats.rcvLossTotal * pktHdrSize);
#endif
    perf->pktSndDropTotal  = m_stats.sndDropTotal;
    perf->pktRcvDropTotal  = m_stats.rcvDropTotal + m_stats.m_rcvUndecryptTotal;
    perf->byteSndDropTotal = m_stats.sndBytesDropTotal + (m_stats.sndDropTotal * pktHdrSize);
    perf->byteRcvDropTotal =
        m_stats.rcvBytesDropTotal + (m_stats.rcvDropTotal * pktHdrSize) + m_stats.m_rcvBytesUndecryptTotal;
    perf->pktRcvUndecryptTotal  = m_stats.m_rcvUndecryptTotal;
    perf->byteRcvUndecryptTotal = m_stats.m_rcvBytesUndecryptTotal;
    //<

    double interval = double(currtime - m_stats.lastSampleTime);

    //>mod
    perf->mbpsSendRate = double(perf->byteSent) * 8.0 / interval;
    perf->mbpsRecvRate = double(perf->byteRecv) * 8.0 / interval;
    //<

    perf->usPktSndPeriod      = m_ullInterval_tk / double(m_ullCPUFrequency);
    perf->pktFlowWindow       = m_iFlowWindowSize;
    perf->pktCongestionWindow = (int)m_dCongestionWindow;
    perf->pktFlightSize       = CSeqNo::seqlen(m_iSndLastAck, CSeqNo::incseq(m_iSndCurrSeqNo)) - 1;
    perf->msRTT               = (double)m_iRTT / 1000.0;
    //>new
    perf->msSndTsbPdDelay = m_bPeerTsbPd ? m_iPeerTsbPdDelay_ms : 0;
    perf->msRcvTsbPdDelay = m_bTsbPd ? m_iTsbPdDelay_ms : 0;
    perf->byteMSS         = m_iMSS;

    perf->mbpsMaxBW = m_llMaxBW > 0 ? Bps2Mbps(m_llMaxBW) : m_CongCtl.ready() ? Bps2Mbps(m_CongCtl->sndBandwidth()) : 0;

    //<
    uint32_t availbw = (uint64_t)(m_iBandwidth == 1 ? m_RcvTimeWindow.getBandwidth() : m_iBandwidth);

    perf->mbpsBandwidth = Bps2Mbps(availbw * (m_iMaxSRTPayloadSize + pktHdrSize));

    if (pthread_mutex_trylock(&m_ConnectionLock) == 0)
    {
        if (m_pSndBuffer)
        {
#ifdef SRT_ENABLE_SNDBUFSZ_MAVG
            if (instantaneous)
            {
                /* Get instant SndBuf instead of moving average for application-based Algorithm
                   (such as NAE) in need of fast reaction to network condition changes. */
                perf->pktSndBuf = m_pSndBuffer->getCurrBufSize(Ref(perf->byteSndBuf), Ref(perf->msSndBuf));
            }
            else
            {
                perf->pktSndBuf = m_pSndBuffer->getAvgBufSize(Ref(perf->byteSndBuf), Ref(perf->msSndBuf));
            }
#else
            perf->pktSndBuf = m_pSndBuffer->getCurrBufSize(Ref(perf->byteSndBuf), Ref(perf->msSndBuf));
#endif
            perf->byteSndBuf += (perf->pktSndBuf * pktHdrSize);
            //<
            perf->byteAvailSndBuf = (m_iSndBufSize - perf->pktSndBuf) * m_iMSS;
        }
        else
        {
            perf->byteAvailSndBuf = 0;
            // new>
            perf->pktSndBuf  = 0;
            perf->byteSndBuf = 0;
            perf->msSndBuf   = 0;
            //<
        }

        if (m_pRcvBuffer)
        {
            perf->byteAvailRcvBuf = m_pRcvBuffer->getAvailBufSize() * m_iMSS;
            // new>
#ifdef SRT_ENABLE_RCVBUFSZ_MAVG
            if (instantaneous) // no need for historical API for Rcv side
            {
                perf->pktRcvBuf = m_pRcvBuffer->getRcvDataSize(perf->byteRcvBuf, perf->msRcvBuf);
            }
            else
            {
                perf->pktRcvBuf = m_pRcvBuffer->getRcvAvgDataSize(perf->byteRcvBuf, perf->msRcvBuf);
            }
#else
            perf->pktRcvBuf = m_pRcvBuffer->getRcvDataSize(perf->byteRcvBuf, perf->msRcvBuf);
#endif
            //<
        }
        else
        {
            perf->byteAvailRcvBuf = 0;
            // new>
            perf->pktRcvBuf  = 0;
            perf->byteRcvBuf = 0;
            perf->msRcvBuf   = 0;
            //<
        }

        pthread_mutex_unlock(&m_ConnectionLock);
    }
    else
    {
        perf->byteAvailSndBuf = 0;
        perf->byteAvailRcvBuf = 0;
        // new>
        perf->pktSndBuf  = 0;
        perf->byteSndBuf = 0;
        perf->msSndBuf   = 0;

        perf->byteRcvBuf = 0;
        perf->msRcvBuf   = 0;
        //<
    }

    if (clear)
    {
        m_stats.traceSndDrop           = 0;
        m_stats.traceRcvDrop           = 0;
        m_stats.traceSndBytesDrop      = 0;
        m_stats.traceRcvBytesDrop      = 0;
        m_stats.traceRcvUndecrypt      = 0;
        m_stats.traceRcvBytesUndecrypt = 0;
        // new>
        m_stats.traceBytesSent = m_stats.traceBytesRecv = m_stats.traceBytesRetrans = 0;
        //<
        m_stats.traceSent = m_stats.traceRecv = m_stats.traceSndLoss = m_stats.traceRcvLoss = m_stats.traceRetrans =
            m_stats.sentACK = m_stats.recvACK = m_stats.sentNAK = m_stats.recvNAK = 0;
        m_stats.sndDuration                                                       = 0;
        m_stats.traceRcvRetrans                                                   = 0;
        m_stats.traceRcvBelated                                                   = 0;
#ifdef SRT_ENABLE_LOSTBYTESCOUNT
        m_stats.traceRcvBytesLoss = 0;
#endif

        m_stats.sndFilterExtra = 0;
        m_stats.rcvFilterExtra = 0;

        m_stats.rcvFilterSupply = 0;
        m_stats.rcvFilterLoss   = 0;

        m_stats.lastSampleTime = currtime;
    }
}

void CUDT::updateCC(ETransmissionEvent evt, EventVariant arg)
{
    // Special things that must be done HERE, not in SrtCongestion,
    // because it involves the input buffer in CUDT. It would be
    // slightly dangerous to give SrtCongestion access to it.

    // According to the rules, the congctl should be ready at the same
    // time when the sending buffer. For sanity check, check both first.
    if (!m_CongCtl.ready() || !m_pSndBuffer)
    {
        LOGC(mglog.Error,
             log << "updateCC: CAN'T DO UPDATE - congctl " << (m_CongCtl.ready() ? "ready" : "NOT READY")
                 << "; sending buffer " << (m_pSndBuffer ? "NOT CREATED" : "created"));

        return;
    }

    HLOGC(mglog.Debug, log << "updateCC: EVENT:" << TransmissionEventStr(evt));

    if (evt == TEV_INIT)
    {
        // only_input uses:
        // 0: in the beginning and when SRTO_MAXBW was changed
        // 1: SRTO_INPUTBW was changed
        // 2: SRTO_OHEADBW was changed
        EInitEvent only_input = arg.get<EventVariant::INIT>();
        // false = TEV_INIT_RESET: in the beginning, or when MAXBW was changed.

        if (only_input && m_llMaxBW)
        {
            HLOGC(mglog.Debug, log << "updateCC/TEV_INIT: non-RESET stage and m_llMaxBW already set to " << m_llMaxBW);
            // Don't change
        }
        else // either m_llMaxBW == 0 or only_input == TEV_INIT_RESET
        {
            // Use the values:
            // - if SRTO_MAXBW is >0, use it.
            // - if SRTO_MAXBW == 0, use SRTO_INPUTBW + SRTO_OHEADBW
            // - if SRTO_INPUTBW == 0, pass 0 to requst in-buffer sampling
            // Bytes/s
            int bw = m_llMaxBW != 0 ? m_llMaxBW :                       // When used SRTO_MAXBW
                         m_llInputBW != 0 ? withOverhead(m_llInputBW) : // SRTO_INPUTBW + SRT_OHEADBW
                             0; // When both MAXBW and INPUTBW are 0, request in-buffer sampling

            // Note: setting bw == 0 uses BW_INFINITE value in LiveCC
            m_CongCtl->updateBandwidth(m_llMaxBW, bw);

            if (only_input == TEV_INIT_OHEADBW)
            {
                // On updated SRTO_OHEADBW don't change input rate.
                // This only influences the call to withOverhead().
            }
            else
            {
                // No need to calculate input reate if the bandwidth is set
                const bool disable_in_rate_calc = (bw != 0);
                m_pSndBuffer->resetInputRateSmpPeriod(disable_in_rate_calc);
            }

            HLOGC(mglog.Debug,
                  log << "updateCC/TEV_INIT: updating BW=" << m_llMaxBW
                      << (only_input == TEV_INIT_RESET
                              ? " (UNCHANGED)"
                              : only_input == TEV_INIT_OHEADBW ? " (only Overhead)" : " (updated sampling rate)"));
        }
    }

    // This part is also required only by LiveCC, however not
    // moved there due to that it needs access to CSndBuffer.
    if (evt == TEV_ACK || evt == TEV_LOSSREPORT || evt == TEV_CHECKTIMER)
    {
        // Specific part done when MaxBW is set to 0 (auto) and InputBW is 0.
        // This requests internal input rate sampling.
        if (m_llMaxBW == 0 && m_llInputBW == 0)
        {
            // Get auto-calculated input rate, Bytes per second
            const int64_t inputbw = m_pSndBuffer->getInputRate();

            /*
             * On blocked transmitter (tx full) and until connection closes,
             * auto input rate falls to 0 but there may be still lot of packet to retransmit
             * Calling updateBandwidth with 0 sets maxBW to default BW_INFINITE (1 Gbps)
             * and sendrate skyrockets for retransmission.
             * Keep previously set maximum in that case (inputbw == 0).
             */
            if (inputbw != 0)
                m_CongCtl->updateBandwidth(0, withOverhead(inputbw)); // Bytes/sec
        }
    }

    HLOGC(mglog.Debug, log << "udpateCC: emitting signal for EVENT:" << TransmissionEventStr(evt));

    // Now execute a congctl-defined action for that event.
    EmitSignal(evt, arg);

    // This should be done with every event except ACKACK and SEND/RECEIVE
    // After any action was done by the congctl, update the congestion window and sending interval.
    if (evt != TEV_ACKACK && evt != TEV_SEND && evt != TEV_RECEIVE)
    {
        // This part comes from original UDT.
        // NOTE: THESE things come from CCC class:
        // - m_dPktSndPeriod
        // - m_dCWndSize
        m_ullInterval_tk    = (uint64_t)(m_CongCtl->pktSndPeriod_us() * m_ullCPUFrequency);
        m_dCongestionWindow = m_CongCtl->cgWindowSize();
#if ENABLE_HEAVY_LOGGING
        HLOGC(mglog.Debug,
              log << "updateCC: updated values from congctl: interval=" << m_ullInterval_tk << "tk ("
                  << m_CongCtl->pktSndPeriod_us() << "us) cgwindow=" << std::setprecision(3) << m_dCongestionWindow);
#endif
    }

    HLOGC(mglog.Debug, log << "udpateCC: finished handling for EVENT:" << TransmissionEventStr(evt));

#if 0 // debug
    static int callcnt = 0;
    if (!(callcnt++ % 250)) cerr << "SndPeriod=" << (m_ullInterval_tk/m_ullCPUFrequency) << "\n");

#endif
}

void CUDT::initSynch()
{
    pthread_mutex_init(&m_SendBlockLock, NULL);
    pthread_cond_init(&m_SendBlockCond, NULL);
    pthread_mutex_init(&m_RecvDataLock, NULL);
    pthread_cond_init(&m_RecvDataCond, NULL);
    pthread_mutex_init(&m_SendLock, NULL);
    pthread_mutex_init(&m_RecvLock, NULL);
    pthread_mutex_init(&m_RcvLossLock, NULL);
    pthread_mutex_init(&m_RecvAckLock, NULL);
    pthread_mutex_init(&m_RcvBufferLock, NULL);
    pthread_mutex_init(&m_ConnectionLock, NULL);
    pthread_mutex_init(&m_StatsLock, NULL);

    memset(&m_RcvTsbPdThread, 0, sizeof m_RcvTsbPdThread);
    pthread_cond_init(&m_RcvTsbPdCond, NULL);
}

void CUDT::destroySynch()
{
    pthread_mutex_destroy(&m_SendBlockLock);
    pthread_cond_destroy(&m_SendBlockCond);
    pthread_mutex_destroy(&m_RecvDataLock);
    pthread_cond_destroy(&m_RecvDataCond);
    pthread_mutex_destroy(&m_SendLock);
    pthread_mutex_destroy(&m_RecvLock);
    pthread_mutex_destroy(&m_RcvLossLock);
    pthread_mutex_destroy(&m_RecvAckLock);
    pthread_mutex_destroy(&m_RcvBufferLock);
    pthread_mutex_destroy(&m_ConnectionLock);
    pthread_mutex_destroy(&m_StatsLock);
    pthread_cond_destroy(&m_RcvTsbPdCond);
}

void CUDT::releaseSynch()
{
    // wake up user calls
    pthread_mutex_lock(&m_SendBlockLock);
    pthread_cond_signal(&m_SendBlockCond);
    pthread_mutex_unlock(&m_SendBlockLock);

    pthread_mutex_lock(&m_SendLock);
    pthread_mutex_unlock(&m_SendLock);

    pthread_mutex_lock(&m_RecvDataLock);
    pthread_cond_signal(&m_RecvDataCond);
    pthread_mutex_unlock(&m_RecvDataLock);

    pthread_mutex_lock(&m_RecvLock);
    pthread_cond_signal(&m_RcvTsbPdCond);
    pthread_mutex_unlock(&m_RecvLock);

    pthread_mutex_lock(&m_RecvDataLock);
    if (!pthread_equal(m_RcvTsbPdThread, pthread_t()))
    {
        pthread_join(m_RcvTsbPdThread, NULL);
        m_RcvTsbPdThread = pthread_t();
    }
    pthread_mutex_unlock(&m_RecvDataLock);

    pthread_mutex_lock(&m_RecvLock);
    pthread_mutex_unlock(&m_RecvLock);
}

#if ENABLE_HEAVY_LOGGING
static void DebugAck(string hdr, int prev, int ack)
{
    if (!prev)
    {
        HLOGC(mglog.Debug, log << hdr << "ACK " << ack);
        return;
    }

    prev     = CSeqNo::incseq(prev);
    int diff = CSeqNo::seqoff(prev, ack);
    if (diff < 0)
    {
        HLOGC(mglog.Debug, log << hdr << "ACK ERROR: " << prev << "-" << ack << "(diff " << diff << ")");
        return;
    }

    bool shorted = diff > 100; // sanity
    if (shorted)
        ack = CSeqNo::incseq(prev, 100);

    ostringstream ackv;
    for (; prev != ack; prev = CSeqNo::incseq(prev))
        ackv << prev << " ";
    if (shorted)
        ackv << "...";
    HLOGC(mglog.Debug, log << hdr << "ACK (" << (diff + 1) << "): " << ackv.str() << ack);
}
#else
static inline void DebugAck(string, int, int) {}
#endif

void CUDT::sendCtrl(UDTMessageType pkttype, const void *lparam, void *rparam, int size)
{
    CPacket  ctrlpkt;
    uint64_t currtime_tk;
    CTimer::rdtsc(currtime_tk);

    ctrlpkt.m_iTimeStamp = int(CTimer::getTime() - m_stats.startTime);

    int nbsent        = 0;
    int local_prevack = 0;

#if ENABLE_HEAVY_LOGGING
    struct SaveBack
    {
        int &      target;
        const int &source;

        ~SaveBack() { target = source; }
    } l_saveback = {m_iDebugPrevLastAck, m_iRcvLastAck};
    (void)l_saveback; // kill compiler warning: unused variable `l_saveback` [-Wunused-variable]

    local_prevack = m_iDebugPrevLastAck;
#endif

    switch (pkttype)
    {
    case UMSG_ACK: // 010 - Acknowledgement
    {
        int32_t ack;

        // If there is no loss, the ACK is the current largest sequence number plus 1;
        // Otherwise it is the smallest sequence number in the receiver loss list.
        if (m_pRcvLossList->getLossLength() == 0)
            ack = CSeqNo::incseq(m_iRcvCurrSeqNo);
        else
            ack = m_pRcvLossList->getFirstLostSeq();

        if (m_iRcvLastAckAck == ack)
            break;

        // send out a lite ACK
        // to save time on buffer processing and bandwidth/AS measurement, a lite ACK only feeds back an ACK number
        if (size == SEND_LITE_ACK)
        {
            ctrlpkt.pack(pkttype, NULL, &ack, size);
            ctrlpkt.m_iID = m_PeerID;
            nbsent        = m_pSndQueue->sendto(m_pPeerAddr, ctrlpkt);
            DebugAck("sendCtrl(lite):" + CONID(), local_prevack, ack);
            break;
        }

        // There are new received packets to acknowledge, update related information.
        /* tsbpd thread may also call ackData when skipping packet so protect code */
        CGuard::enterCS(m_RcvBufferLock);

        // IF ack > m_iRcvLastAck
        if (CSeqNo::seqcmp(ack, m_iRcvLastAck) > 0)
        {
            int acksize = CSeqNo::seqoff(m_iRcvLastSkipAck, ack);

            IF_HEAVY_LOGGING(int32_t oldack = m_iRcvLastSkipAck);
            m_iRcvLastAck     = ack;
            m_iRcvLastSkipAck = ack;

            // XXX Unknown as to whether it matters.
            // This if (acksize) causes that ackData() won't be called.
            // With size == 0 it wouldn't do anything except calling CTimer::triggerEvent().
            // This, again, signals the condition, CTimer::m_EventCond.
            // This releases CTimer::waitForEvent() call used in CUDTUnited::selectEx().
            // Preventing to call this on zero size makes sense, if it prevents false alerts.
            if (acksize > 0)
                m_pRcvBuffer->ackData(acksize);
            CGuard::leaveCS(m_RcvBufferLock);

            // If TSBPD is enabled, then INSTEAD OF signaling m_RecvDataCond,
            // signal m_RcvTsbPdCond. This will kick in the tsbpd thread, which
            // will signal m_RecvDataCond when there's time to play for particular
            // data packet.
            HLOGC(dlog.Debug,
                  log << "ACK: clip %" << oldack << "-%" << ack << ", REVOKED " << acksize << " from RCV buffer");

            if (m_bTsbPd)
            {
                /* Newly acknowledged data, signal TsbPD thread */
                pthread_mutex_lock(&m_RecvLock);
                if (m_bTsbPdAckWakeup)
                    pthread_cond_signal(&m_RcvTsbPdCond);
                pthread_mutex_unlock(&m_RecvLock);
            }
            else
            {
                if (m_bSynRecving)
                {
                    // signal a waiting "recv" call if there is any data available
                    pthread_mutex_lock(&m_RecvDataLock);
                    pthread_cond_signal(&m_RecvDataCond);
                    pthread_mutex_unlock(&m_RecvDataLock);
                }
                // acknowledge any waiting epolls to read
                s_UDTUnited.m_EPoll.update_events(m_SocketID, m_sPollID, UDT_EPOLL_IN, true);
                CTimer::triggerEvent();
            }
            CGuard::enterCS(m_RcvBufferLock);
        }
        else if (ack == m_iRcvLastAck)
        {
            // If the ACK was just sent already AND elapsed time did not exceed RTT,
            if ((currtime_tk - m_ullLastAckTime_tk) < ((m_iRTT + 4 * m_iRTTVar) * m_ullCPUFrequency))
            {
                CGuard::leaveCS(m_RcvBufferLock);
                break;
            }
        }
        else
        {
            // Not possible (m_iRcvCurrSeqNo+1 < m_iRcvLastAck ?)
            CGuard::leaveCS(m_RcvBufferLock);
            break;
        }

        // [[using assert( ack >= m_iRcvLastAck && is_periodic_ack ) ]]

        // Send out the ACK only if has not been received by the sender before
        if (CSeqNo::seqcmp(m_iRcvLastAck, m_iRcvLastAckAck) > 0)
        {
            // NOTE: The BSTATS feature turns on extra fields above size 6
            // also known as ACKD_TOTAL_SIZE_VER100.
            int32_t data[ACKD_TOTAL_SIZE];

            // Case you care, CAckNo::incack does exactly the same thing as
            // CSeqNo::incseq. Logically the ACK number is a different thing
            // than sequence number (it's a "journal" for ACK request-response,
            // and starts from 0, unlike sequence, which starts from a random
            // number), but still the numbers are from exactly the same domain.
            m_iAckSeqNo           = CAckNo::incack(m_iAckSeqNo);
            data[ACKD_RCVLASTACK] = m_iRcvLastAck;
            data[ACKD_RTT]        = m_iRTT;
            data[ACKD_RTTVAR]     = m_iRTTVar;
            data[ACKD_BUFFERLEFT] = m_pRcvBuffer->getAvailBufSize();
            // a minimum flow window of 2 is used, even if buffer is full, to break potential deadlock
            if (data[ACKD_BUFFERLEFT] < 2)
                data[ACKD_BUFFERLEFT] = 2;

            // NOTE: m_CongCtl->ACKTimeout_us() should be taken into account.
            if (currtime_tk - m_ullLastAckTime_tk > m_ullACKInt_tk)
            {
                int rcvRate;
                int ctrlsz = ACKD_TOTAL_SIZE_UDTBASE * ACKD_FIELD_SIZE; // Minimum required size

                data[ACKD_RCVSPEED]  = m_RcvTimeWindow.getPktRcvSpeed(Ref(rcvRate));
                data[ACKD_BANDWIDTH] = m_RcvTimeWindow.getBandwidth();

                //>>Patch while incompatible (1.0.2) receiver floating around
                if (m_lPeerSrtVersion == SrtVersion(1, 0, 2))
                {
                    data[ACKD_RCVRATE] = rcvRate;                                     // bytes/sec
                    data[ACKD_XMRATE]  = data[ACKD_BANDWIDTH] * m_iMaxSRTPayloadSize; // bytes/sec
                    ctrlsz             = ACKD_FIELD_SIZE * ACKD_TOTAL_SIZE_VER102;
                }
                else if (m_lPeerSrtVersion >= SrtVersion(1, 0, 3))
                {
                    // Normal, currently expected version.
                    data[ACKD_RCVRATE] = rcvRate; // bytes/sec
                    ctrlsz             = ACKD_FIELD_SIZE * ACKD_TOTAL_SIZE_VER101;
                }
                // ELSE: leave the buffer with ...UDTBASE size.

                ctrlpkt.pack(pkttype, &m_iAckSeqNo, data, ctrlsz);
                CTimer::rdtsc(m_ullLastAckTime_tk);
            }
            else
            {
                ctrlpkt.pack(pkttype, &m_iAckSeqNo, data, ACKD_FIELD_SIZE * ACKD_TOTAL_SIZE_SMALL);
            }

            ctrlpkt.m_iID        = m_PeerID;
            ctrlpkt.m_iTimeStamp = int(CTimer::getTime() - m_stats.startTime);
            nbsent               = m_pSndQueue->sendto(m_pPeerAddr, ctrlpkt);
            DebugAck("sendCtrl: " + CONID(), local_prevack, ack);

            m_ACKWindow.store(m_iAckSeqNo, m_iRcvLastAck);

            CGuard::enterCS(m_StatsLock);
            ++m_stats.sentACK;
            ++m_stats.sentACKTotal;
            CGuard::leaveCS(m_StatsLock);
        }
        CGuard::leaveCS(m_RcvBufferLock);
        break;
    }

    case UMSG_ACKACK: // 110 - Acknowledgement of Acknowledgement
        ctrlpkt.pack(pkttype, lparam);
        ctrlpkt.m_iID = m_PeerID;
        nbsent        = m_pSndQueue->sendto(m_pPeerAddr, ctrlpkt);

        break;

    case UMSG_LOSSREPORT: // 011 - Loss Report
    {
        // Explicitly defined lost sequences
        if (rparam)
        {
            int32_t *lossdata = (int32_t *)rparam;

            size_t bytes = sizeof(*lossdata) * size;
            ctrlpkt.pack(pkttype, NULL, lossdata, bytes);

            ctrlpkt.m_iID = m_PeerID;
            nbsent        = m_pSndQueue->sendto(m_pPeerAddr, ctrlpkt);

            CGuard::enterCS(m_StatsLock);
            ++m_stats.sentNAK;
            ++m_stats.sentNAKTotal;
            CGuard::leaveCS(m_StatsLock);
        }
        // Call with no arguments - get loss list from internal data.
        else if (m_pRcvLossList->getLossLength() > 0)
        {
            // this is periodically NAK report; make sure NAK cannot be sent back too often

            // read loss list from the local receiver loss list
            int32_t *data = new int32_t[m_iMaxSRTPayloadSize / 4];
            int      losslen;
            m_pRcvLossList->getLossArray(data, losslen, m_iMaxSRTPayloadSize / 4);

            if (0 < losslen)
            {
                ctrlpkt.pack(pkttype, NULL, data, losslen * 4);
                ctrlpkt.m_iID = m_PeerID;
                nbsent        = m_pSndQueue->sendto(m_pPeerAddr, ctrlpkt);

                CGuard::enterCS(m_StatsLock);
                ++m_stats.sentNAK;
                ++m_stats.sentNAKTotal;
                CGuard::leaveCS(m_StatsLock);
            }

            delete[] data;
        }

        // update next NAK time, which should wait enough time for the retansmission, but not too long
        m_ullNAKInt_tk = (m_iRTT + 4 * m_iRTTVar) * m_ullCPUFrequency;

        // Fix the NAKreport period according to the congctl
        m_ullNAKInt_tk = m_CongCtl->updateNAKInterval(
            m_ullNAKInt_tk, m_RcvTimeWindow.getPktRcvSpeed(), m_pRcvLossList->getLossLength());

        // This is necessary because a congctl need not wish to define
        // its own minimum interval, in which case the default one is used.
        if (m_ullNAKInt_tk < m_ullMinNakInt_tk)
            m_ullNAKInt_tk = m_ullMinNakInt_tk;

        break;
    }

    case UMSG_CGWARNING: // 100 - Congestion Warning
        ctrlpkt.pack(pkttype);
        ctrlpkt.m_iID = m_PeerID;
        nbsent        = m_pSndQueue->sendto(m_pPeerAddr, ctrlpkt);

        CTimer::rdtsc(m_ullLastWarningTime);

        break;

    case UMSG_KEEPALIVE: // 001 - Keep-alive
        ctrlpkt.pack(pkttype);
        ctrlpkt.m_iID = m_PeerID;
        nbsent        = m_pSndQueue->sendto(m_pPeerAddr, ctrlpkt);

        break;

    case UMSG_HANDSHAKE: // 000 - Handshake
        ctrlpkt.pack(pkttype, NULL, rparam, sizeof(CHandShake));
        ctrlpkt.m_iID = m_PeerID;
        nbsent        = m_pSndQueue->sendto(m_pPeerAddr, ctrlpkt);

        break;

    case UMSG_SHUTDOWN: // 101 - Shutdown
        ctrlpkt.pack(pkttype);
        ctrlpkt.m_iID = m_PeerID;
        nbsent        = m_pSndQueue->sendto(m_pPeerAddr, ctrlpkt);

        break;

    case UMSG_DROPREQ: // 111 - Msg drop request
        ctrlpkt.pack(pkttype, lparam, rparam, 8);
        ctrlpkt.m_iID = m_PeerID;
        nbsent        = m_pSndQueue->sendto(m_pPeerAddr, ctrlpkt);

        break;

    case UMSG_PEERERROR: // 1000 - acknowledge the peer side a special error
        ctrlpkt.pack(pkttype, lparam);
        ctrlpkt.m_iID = m_PeerID;
        nbsent        = m_pSndQueue->sendto(m_pPeerAddr, ctrlpkt);

        break;

    case UMSG_EXT: // 0x7FFF - Resevered for future use
        break;

    default:
        break;
    }

    // Fix keepalive
    if (nbsent)
        m_ullLastSndTime_tk = currtime_tk;
}

void CUDT::updateSndLossListOnACK(int32_t ackdata_seqno)
{
    // Update sender's loss list and acknowledge packets in the sender's buffer
    {
        // m_RecvAckLock protects sender's loss list and epoll
        CGuard ack_lock(m_RecvAckLock);

        const int offset = CSeqNo::seqoff(m_iSndLastDataAck, ackdata_seqno);
        // IF distance between m_iSndLastDataAck and ack is nonempty...
        if (offset <= 0)
            return;

        // update sending variables
        m_iSndLastDataAck = ackdata_seqno;

        // remove any loss that predates 'ack' (not to be considered loss anymore)
        m_pSndLossList->remove(CSeqNo::decseq(m_iSndLastDataAck));

        // acknowledge the sending buffer (remove data that predate 'ack')
        m_pSndBuffer->ackData(offset);

        // acknowledde any waiting epolls to write
        s_UDTUnited.m_EPoll.update_events(m_SocketID, m_sPollID, UDT_EPOLL_OUT, true);
    }

    // insert this socket to snd list if it is not on the list yet
    m_pSndQueue->m_pSndUList->update(this, CSndUList::DONT_RESCHEDULE);

    if (m_bSynSending)
    {
        CGuard lk(m_SendBlockLock);
        pthread_cond_signal(&m_SendBlockCond);
    }

    const int64_t currtime = CTimer::getTime();
    // record total time used for sending
    CGuard::enterCS(m_StatsLock);
    m_stats.sndDuration += currtime - m_stats.sndDurationCounter;
    m_stats.m_sndDurationTotal += currtime - m_stats.sndDurationCounter;
    m_stats.sndDurationCounter = currtime;
    CGuard::leaveCS(m_StatsLock);
}

void CUDT::processCtrlAck(const CPacket &ctrlpkt, const uint64_t currtime_tk)
{
    const int32_t *ackdata       = (const int32_t *)ctrlpkt.m_pcData;
    const int32_t  ackdata_seqno = ackdata[ACKD_RCVLASTACK];

    const bool isLiteAck = ctrlpkt.getLength() == (size_t)SEND_LITE_ACK;
    HLOGC(mglog.Debug,
          log << CONID() << "ACK covers: " << m_iSndLastDataAck << " - " << ackdata_seqno << " [ACK=" << m_iSndLastAck
              << "]" << (isLiteAck ? "[LITE]" : "[FULL]"));

    updateSndLossListOnACK(ackdata_seqno);

    // Process a lite ACK
    if (isLiteAck)
    {
        if (CSeqNo::seqcmp(ackdata_seqno, m_iSndLastAck) >= 0)
        {
            CGuard ack_lock(m_RecvAckLock);
            m_iFlowWindowSize -= CSeqNo::seqoff(m_iSndLastAck, ackdata_seqno);
            m_iSndLastAck = ackdata_seqno;

            // TODO: m_ullLastRspAckTime_tk should be protected with m_RecvAckLock
            // because the sendmsg2 may want to change it at the same time.
            m_ullLastRspAckTime_tk = currtime_tk;
            m_iReXmitCount         = 1; // Reset re-transmit count since last ACK
        }

        return;
    }

    // Decide to send ACKACK or not
    {
        // Sequence number of the ACK packet
        const int32_t ack_seqno = ctrlpkt.getAckSeqNo();

        // Send ACK acknowledgement (UMSG_ACKACK).
        // There can be less ACKACK packets in the stream, than the number of ACK packets.
        // Only send ACKACK every syn interval or if ACK packet with the sequence number
        // already acknowledged (with ACKACK) has come again, which probably means ACKACK was lost.
        const uint64_t now = CTimer::getTime();
        if ((now - m_ullSndLastAck2Time > (uint64_t)COMM_SYN_INTERVAL_US) || (ack_seqno == m_iSndLastAck2))
        {
            sendCtrl(UMSG_ACKACK, &ack_seqno);
            m_iSndLastAck2       = ack_seqno;
            m_ullSndLastAck2Time = now;
        }
    }

    //
    // Begin of the new code with TLPKTDROP.
    //

    // Protect packet retransmission
    CGuard::enterCS(m_RecvAckLock);

    // Check the validation of the ack
    if (CSeqNo::seqcmp(ackdata_seqno, CSeqNo::incseq(m_iSndCurrSeqNo)) > 0)
    {
        CGuard::leaveCS(m_RecvAckLock);
        // this should not happen: attack or bug
        LOGC(glog.Error,
             log << CONID() << "ATTACK/IPE: incoming ack seq " << ackdata_seqno << " exceeds current "
                 << m_iSndCurrSeqNo << " by " << (CSeqNo::seqoff(m_iSndCurrSeqNo, ackdata_seqno) - 1) << "!");
        m_bBroken        = true;
        m_iBrokenCounter = 0;
        return;
    }

    if (CSeqNo::seqcmp(ackdata_seqno, m_iSndLastAck) >= 0)
    {
        // Update Flow Window Size, must update before and together with m_iSndLastAck
        m_iFlowWindowSize      = ackdata[ACKD_BUFFERLEFT];
        m_iSndLastAck          = ackdata_seqno;
        m_ullLastRspAckTime_tk = currtime_tk; // Should be protected with m_RecvAckLock
        m_iReXmitCount         = 1;           // Reset re-transmit count since last ACK
    }

    /*
     * We must not ignore full ack received by peer
     * if data has been artificially acked by late packet drop.
     * Therefore, a distinct ack state is used for received Ack (iSndLastFullAck)
     * and ack position in send buffer (m_iSndLastDataAck).
     * Otherwise, when severe congestion causing packet drops (and m_iSndLastDataAck update)
     * occures, we drop received acks (as duplicates) and do not update stats like RTT,
     * which may go crazy and stay there, preventing proper stream recovery.
     */

    if (CSeqNo::seqoff(m_iSndLastFullAck, ackdata_seqno) <= 0)
    {
        // discard it if it is a repeated ACK
        CGuard::leaveCS(m_RecvAckLock);
        return;
    }
    m_iSndLastFullAck = ackdata_seqno;

    //
    // END of the new code with TLPKTDROP
    //
    CGuard::leaveCS(m_RecvAckLock);

    size_t acksize   = ctrlpkt.getLength(); // TEMPORARY VALUE FOR CHECKING
    bool   wrongsize = 0 != (acksize % ACKD_FIELD_SIZE);
    acksize          = acksize / ACKD_FIELD_SIZE; // ACTUAL VALUE

    if (wrongsize)
    {
        // Issue a log, but don't do anything but skipping the "odd" bytes from the payload.
        LOGC(mglog.Error,
             log << CONID() << "Received UMSG_ACK payload is not evened up to 4-byte based field size - cutting to "
                 << acksize << " fields");
    }

    // Start with checking the base size.
    if (acksize < ACKD_TOTAL_SIZE_SMALL)
    {
        LOGC(mglog.Error, log << CONID() << "Invalid ACK size " << acksize << " fields - less than minimum required!");
        // Ack is already interpreted, just skip further parts.
        return;
    }
    // This check covers fields up to ACKD_BUFFERLEFT.

    // Update RTT
    // m_iRTT = ackdata[ACKD_RTT];
    // m_iRTTVar = ackdata[ACKD_RTTVAR];
    // XXX These ^^^ commented-out were blocked in UDT;
    // the current RTT calculations are exactly the same as in UDT4.
    const int rtt = ackdata[ACKD_RTT];

    m_iRTTVar = avg_iir<4>(m_iRTTVar, abs(rtt - m_iRTT));
    m_iRTT    = avg_iir<8>(m_iRTT, rtt);

    /* Version-dependent fields:
     * Original UDT (total size: ACKD_TOTAL_SIZE_SMALL):
     *   ACKD_RCVLASTACK
     *   ACKD_RTT
     *   ACKD_RTTVAR
     *   ACKD_BUFFERLEFT
     * Additional UDT fields, not always attached:
     *   ACKD_RCVSPEED
     *   ACKD_BANDWIDTH
     * SRT extension version 1.0.2 (bstats):
     *   ACKD_RCVRATE
     * SRT extension version 1.0.4:
     *   ACKD_XMRATE
     */

    if (acksize > ACKD_TOTAL_SIZE_SMALL)
    {
        // This means that ACKD_RCVSPEED and ACKD_BANDWIDTH fields are available.
        int pktps     = ackdata[ACKD_RCVSPEED];
        int bandwidth = ackdata[ACKD_BANDWIDTH];
        int bytesps;

        /* SRT v1.0.2 Bytes-based stats: bandwidth (pcData[ACKD_XMRATE]) and delivery rate (pcData[ACKD_RCVRATE]) in
         * bytes/sec instead of pkts/sec */
        /* SRT v1.0.3 Bytes-based stats: only delivery rate (pcData[ACKD_RCVRATE]) in bytes/sec instead of pkts/sec */
        if (acksize > ACKD_TOTAL_SIZE_UDTBASE)
            bytesps = ackdata[ACKD_RCVRATE];
        else
            bytesps = pktps * m_iMaxSRTPayloadSize;

        m_iBandwidth        = avg_iir<8>(m_iBandwidth, bandwidth);
        m_iDeliveryRate     = avg_iir<8>(m_iDeliveryRate, pktps);
        m_iByteDeliveryRate = avg_iir<8>(m_iByteDeliveryRate, bytesps);
        // XXX not sure if ACKD_XMRATE is of any use. This is simply
        // calculated as ACKD_BANDWIDTH * m_iMaxSRTPayloadSize.

        // Update Estimated Bandwidth and packet delivery rate
        // m_iRcvRate = m_iDeliveryRate;
        // ^^ This has been removed because with the SrtCongestion class
        // instead of reading the m_iRcvRate local field this will read
        // cudt->deliveryRate() instead.
    }

    checkSndTimers(REGEN_KM);
    updateCC(TEV_ACK, ackdata_seqno);

    CGuard::enterCS(m_StatsLock);
    ++m_stats.recvACK;
    ++m_stats.recvACKTotal;
    CGuard::leaveCS(m_StatsLock);
}

void CUDT::processCtrl(CPacket &ctrlpkt)
{
    // Just heard from the peer, reset the expiration count.
    m_iEXPCount = 1;
    uint64_t currtime_tk;
    CTimer::rdtsc(currtime_tk);
    m_ullLastRspTime_tk    = currtime_tk;
    bool using_rexmit_flag = m_bPeerRexmitFlag;

    HLOGC(mglog.Debug,
          log << CONID() << "incoming UMSG:" << ctrlpkt.getType() << " ("
              << MessageTypeStr(ctrlpkt.getType(), ctrlpkt.getExtendedType()) << ") socket=%" << ctrlpkt.m_iID);

    switch (ctrlpkt.getType())
    {
    case UMSG_ACK: // 010 - Acknowledgement
        processCtrlAck(ctrlpkt, currtime_tk);
        break;

    case UMSG_ACKACK: // 110 - Acknowledgement of Acknowledgement
    {
        int32_t ack = 0;
        int     rtt = -1;

        // update RTT
        rtt = m_ACKWindow.acknowledge(ctrlpkt.getAckSeqNo(), ack);
        if (rtt <= 0)
        {
            LOGC(mglog.Error,
                 log << "IPE: ACK node overwritten when acknowledging " << ctrlpkt.getAckSeqNo()
                     << " (ack extracted: " << ack << ")");
            break;
        }

        // if increasing delay detected...
        //   sendCtrl(UMSG_CGWARNING);

        // RTT EWMA
        m_iRTTVar = (m_iRTTVar * 3 + abs(rtt - m_iRTT)) >> 2;
        m_iRTT    = (m_iRTT * 7 + rtt) >> 3;

        updateCC(TEV_ACKACK, ack);

        // This function will put a lock on m_RecvLock by itself, as needed.
        // It must be done inside because this function reads the current time
        // and if waiting for the lock has caused a delay, the time will be
        // inaccurate. Additionally it won't lock if TSBPD mode is off, and
        // won't update anything. Note that if you set TSBPD mode and use
        // srt_recvfile (which doesn't make any sense), you'll have a deadlock.
        m_pRcvBuffer->addRcvTsbPdDriftSample(ctrlpkt.getMsgTimeStamp(), m_RecvLock);

        // update last ACK that has been received by the sender
        if (CSeqNo::seqcmp(ack, m_iRcvLastAckAck) > 0)
            m_iRcvLastAckAck = ack;

        break;
    }

    case UMSG_LOSSREPORT: // 011 - Loss Report
    {
        int32_t *losslist     = (int32_t *)(ctrlpkt.m_pcData);
        size_t   losslist_len = ctrlpkt.getLength() / 4;

        bool secure = true;

        // protect packet retransmission
        CGuard::enterCS(m_RecvAckLock);

        // This variable is used in "normal" logs, so it may cause a warning
        // when logging is forcefully off.
        int32_t wrong_loss SRT_ATR_UNUSED = CSeqNo::m_iMaxSeqNo;

        // decode loss list message and insert loss into the sender loss list
        for (int i = 0, n = (int)(ctrlpkt.getLength() / 4); i < n; ++i)
        {
            if (IsSet(losslist[i], LOSSDATA_SEQNO_RANGE_FIRST))
            {
                // Then it's this is a <lo, hi> specification with HI in a consecutive cell.
                int32_t losslist_lo = SEQNO_VALUE::unwrap(losslist[i]);
                int32_t losslist_hi = losslist[i + 1];
                // <lo, hi> specification means that the consecutive cell has been already interpreted.
                ++i;

                HLOGF(mglog.Debug,
                      "received UMSG_LOSSREPORT: %d-%d (%d packets)...",
                      losslist_lo,
                      losslist_hi,
                      CSeqNo::seqoff(losslist_lo, losslist_hi) + 1);

                if ((CSeqNo::seqcmp(losslist_lo, losslist_hi) > 0) ||
                    (CSeqNo::seqcmp(losslist_hi, m_iSndCurrSeqNo) > 0))
                {
                    // seq_a must not be greater than seq_b; seq_b must not be greater than the most recent sent seq
                    secure     = false;
                    wrong_loss = losslist_hi;
                    // XXX leaveCS: really necessary? 'break' will break the 'for' loop, not the 'switch' statement.
                    // and the leaveCS is done again next to the 'for' loop end.
                    CGuard::leaveCS(m_RecvAckLock);
                    break;
                }

                int num = 0;
                if (CSeqNo::seqcmp(losslist_lo, m_iSndLastAck) >= 0)
                    num = m_pSndLossList->insert(losslist_lo, losslist_hi);
                else if (CSeqNo::seqcmp(losslist_hi, m_iSndLastAck) >= 0)
                {
                    // This should be theoretically impossible because this would mean
                    // that the received packet loss report informs about the loss that predates
                    // the ACK sequence.
                    // However, this can happen if the packet reordering has caused the earlier sent
                    // LOSSREPORT will be delivered after later sent ACK. Whatever, ACK should be
                    // more important, so simply drop the part that predates ACK.
                    num = m_pSndLossList->insert(m_iSndLastAck, losslist_hi);
                }

                CGuard::enterCS(m_StatsLock);
                m_stats.traceSndLoss += num;
                m_stats.sndLossTotal += num;
                CGuard::leaveCS(m_StatsLock);
            }
            else if (CSeqNo::seqcmp(losslist[i], m_iSndLastAck) >= 0)
            {
                HLOGF(mglog.Debug, "received UMSG_LOSSREPORT: %d (1 packet)...", losslist[i]);

                if (CSeqNo::seqcmp(losslist[i], m_iSndCurrSeqNo) > 0)
                {
                    // seq_a must not be greater than the most recent sent seq
                    secure     = false;
                    wrong_loss = losslist[i];
                    CGuard::leaveCS(m_RecvAckLock);
                    break;
                }

                int num = m_pSndLossList->insert(losslist[i], losslist[i]);

                CGuard::enterCS(m_StatsLock);
                m_stats.traceSndLoss += num;
                m_stats.sndLossTotal += num;
                CGuard::leaveCS(m_StatsLock);
            }
        }
        CGuard::leaveCS(m_RecvAckLock);

        updateCC(TEV_LOSSREPORT, EventVariant(losslist, losslist_len));

        if (!secure)
        {
            LOGC(mglog.Warn,
                 log << "out-of-band LOSSREPORT received; BUG or ATTACK - last sent %" << m_iSndCurrSeqNo
                     << " vs loss %" << wrong_loss);
            // this should not happen: attack or bug
            m_bBroken        = true;
            m_iBrokenCounter = 0;
            break;
        }

        // the lost packet (retransmission) should be sent out immediately
        m_pSndQueue->m_pSndUList->update(this, CSndUList::DO_RESCHEDULE);

        CGuard::enterCS(m_StatsLock);
        ++m_stats.recvNAK;
        ++m_stats.recvNAKTotal;
        CGuard::leaveCS(m_StatsLock);

        break;
    }

    case UMSG_CGWARNING: // 100 - Delay Warning
        // One way packet delay is increasing, so decrease the sending rate
        m_ullInterval_tk = (uint64_t)ceil(m_ullInterval_tk * 1.125);
        m_iLastDecSeq    = m_iSndCurrSeqNo;
        // XXX Note as interesting fact: this is only prepared for handling,
        // but nothing in the code is sending this message. Probably predicted
        // for a custom congctl. There's a predicted place to call it under
        // UMSG_ACKACK handling, but it's commented out.

        break;

    case UMSG_KEEPALIVE: // 001 - Keep-alive
        // The only purpose of keep-alive packet is to tell that the peer is still alive
        // nothing needs to be done.

        break;

    case UMSG_HANDSHAKE: // 000 - Handshake
    {
        CHandShake req;
        req.load_from(ctrlpkt.m_pcData, ctrlpkt.getLength());

        HLOGC(mglog.Debug, log << "processCtrl: got HS: " << req.show());

        if ((req.m_iReqType > URQ_INDUCTION_TYPES) // acually it catches URQ_INDUCTION and URQ_ERROR_* symbols...???
            || (m_bRendezvous && (req.m_iReqType != URQ_AGREEMENT))) // rnd sends AGREEMENT in rsp to CONCLUSION
        {
            // The peer side has not received the handshake message, so it keeps querying
            // resend the handshake packet

            // This condition embraces cases when:
            // - this is normal accept() and URQ_INDUCTION was received
            // - this is rendezvous accept() and there's coming any kind of URQ except AGREEMENT (should be RENDEZVOUS
            // or CONCLUSION)
            // - this is any of URQ_ERROR_* - well...
            CHandShake initdata;
            initdata.m_iISN            = m_iISN;
            initdata.m_iMSS            = m_iMSS;
            initdata.m_iFlightFlagSize = m_iFlightFlagSize;

            // For rendezvous we do URQ_WAVEAHAND/URQ_CONCLUSION --> URQ_AGREEMENT.
            // For client-server we do URQ_INDUCTION --> URQ_CONCLUSION.
            initdata.m_iReqType = (!m_bRendezvous) ? URQ_CONCLUSION : URQ_AGREEMENT;
            initdata.m_iID      = m_SocketID;

            uint32_t kmdata[SRTDATA_MAXSIZE];
            size_t   kmdatasize = SRTDATA_MAXSIZE;
            bool     have_hsreq = false;
            if (req.m_iVersion > HS_VERSION_UDT4)
            {
                initdata.m_iVersion = HS_VERSION_SRT1; // if I remember correctly, this is induction/listener...
                int hs_flags        = SrtHSRequest::SRT_HSTYPE_HSFLAGS::unwrap(m_ConnRes.m_iType);
                if (hs_flags != 0) // has SRT extensions
                {
                    HLOGC(mglog.Debug,
                          log << "processCtrl/HS: got HS reqtype=" << RequestTypeStr(req.m_iReqType)
                              << " WITH SRT ext");
                    have_hsreq = interpretSrtHandshake(req, ctrlpkt, kmdata, &kmdatasize);
                    if (!have_hsreq)
                    {
                        initdata.m_iVersion = 0;
                        m_RejectReason      = SRT_REJ_ROGUE;
                        initdata.m_iReqType = URQFailure(m_RejectReason);
                    }
                    else
                    {
                        // Extensions are added only in case of CONCLUSION (not AGREEMENT).
                        // Actually what is expected here is that this may either process the
                        // belated-repeated handshake from a caller (and then it's CONCLUSION,
                        // and should be added with HSRSP/KMRSP), or it's a belated handshake
                        // of Rendezvous when it has already considered itself connected.
                        // Sanity check - according to the rules, there should be no such situation
                        if (m_bRendezvous && m_SrtHsSide == HSD_RESPONDER)
                        {
                            LOGC(mglog.Error,
                                 log << "processCtrl/HS: IPE???: RESPONDER should receive all its handshakes in "
                                        "handshake phase.");
                        }

                        // The 'extension' flag will be set from this variable; set it to false
                        // in case when the AGREEMENT response is to be sent.
                        have_hsreq = initdata.m_iReqType == URQ_CONCLUSION;
                        HLOGC(mglog.Debug,
                              log << "processCtrl/HS: processing ok, reqtype=" << RequestTypeStr(initdata.m_iReqType)
                                  << " kmdatasize=" << kmdatasize);
                    }
                }
                else
                {
                    HLOGC(mglog.Debug, log << "processCtrl/HS: got HS reqtype=" << RequestTypeStr(req.m_iReqType));
                }
            }
            else
            {
                initdata.m_iVersion = HS_VERSION_UDT4;
            }

            initdata.m_extension = have_hsreq;

            HLOGC(mglog.Debug,
                  log << CONID() << "processCtrl: responding HS reqtype=" << RequestTypeStr(initdata.m_iReqType)
                      << (have_hsreq ? " WITH SRT HS response extensions" : ""));

            // XXX here interpret SRT handshake extension
            CPacket response;
            response.setControl(UMSG_HANDSHAKE);
            response.allocate(m_iMaxSRTPayloadSize);

            // If createSrtHandshake failed, don't send anything. Actually it can only fail on IPE.
            // There is also no possible IPE condition in case of HSv4 - for this version it will always return true.
            if (createSrtHandshake(Ref(response), Ref(initdata), SRT_CMD_HSRSP, SRT_CMD_KMRSP, kmdata, kmdatasize))
            {
                response.m_iID        = m_PeerID;
                response.m_iTimeStamp = int(CTimer::getTime() - m_stats.startTime);
                int nbsent            = m_pSndQueue->sendto(m_pPeerAddr, response);
                if (nbsent)
                {
                    uint64_t currtime_tk;
                    CTimer::rdtsc(currtime_tk);
                    m_ullLastSndTime_tk = currtime_tk;
                }
            }
        }
        else
        {
            HLOGC(mglog.Debug, log << "processCtrl: ... not INDUCTION, not ERROR, not rendezvous - IGNORED.");
        }

        break;
    }

    case UMSG_SHUTDOWN: // 101 - Shutdown
        m_bShutdown      = true;
        m_bClosing       = true;
        m_bBroken        = true;
        m_iBrokenCounter = 60;

        // Signal the sender and recver if they are waiting for data.
        releaseSynch();
        // Unblock any call so they learn the connection_broken error
        s_UDTUnited.m_EPoll.update_events(m_SocketID, m_sPollID, UDT_EPOLL_ERR, true);

        CTimer::triggerEvent();

        break;

    case UMSG_DROPREQ: // 111 - Msg drop request
        CGuard::enterCS(m_RecvLock);
        m_pRcvBuffer->dropMsg(ctrlpkt.getMsgSeq(using_rexmit_flag), using_rexmit_flag);
        CGuard::leaveCS(m_RecvLock);

        dropFromLossLists(*(int32_t *)ctrlpkt.m_pcData, *(int32_t *)(ctrlpkt.m_pcData + 4));

        // move forward with current recv seq no.
        if ((CSeqNo::seqcmp(*(int32_t *)ctrlpkt.m_pcData, CSeqNo::incseq(m_iRcvCurrSeqNo)) <= 0) &&
            (CSeqNo::seqcmp(*(int32_t *)(ctrlpkt.m_pcData + 4), m_iRcvCurrSeqNo) > 0))
        {
            m_iRcvCurrSeqNo = *(int32_t *)(ctrlpkt.m_pcData + 4);
        }

        break;

    case UMSG_PEERERROR: // 1000 - An error has happened to the peer side
        // int err_type = packet.getAddInfo();

        // currently only this error is signalled from the peer side
        // if recvfile() failes (e.g., due to disk fail), blcoked sendfile/send should return immediately
        // giving the app a chance to fix the issue

        m_bPeerHealth = false;

        break;

    case UMSG_EXT: // 0x7FFF - reserved and user defined messages
        HLOGF(mglog.Debug, "CONTROL EXT MSG RECEIVED: %08X\n", ctrlpkt.getExtendedType());
        {
            // This has currently two roles in SRT:
            // - HSv4 (legacy) handshake
            // - refreshed KMX (initial KMX is done still in the HS process in HSv5)
            bool understood = processSrtMsg(&ctrlpkt);
            // CAREFUL HERE! This only means that this update comes from the UMSG_EXT
            // message received, REGARDLESS OF WHAT IT IS. This version doesn't mean
            // the handshake version, but the reason of calling this function.
            //
            // Fortunately, the only messages taken into account in this function
            // are HSREQ and HSRSP, which should *never* be interchanged when both
            // parties are HSv5.
            if (understood)
            {
                updateAfterSrtHandshake(ctrlpkt.getExtendedType(), HS_VERSION_UDT4);
            }
            else
            {
                updateCC(TEV_CUSTOM, &ctrlpkt);
            }
        }
        break;

    default:
        break;
    }
}

void CUDT::updateSrtRcvSettings()
{
    if (m_bTsbPd)
    {
        /* We are TsbPd receiver */
        CGuard::enterCS(m_RecvLock);
        m_pRcvBuffer->setRcvTsbPdMode(m_ullRcvPeerStartTime, m_iTsbPdDelay_ms * 1000);
        CGuard::leaveCS(m_RecvLock);

        HLOGF(mglog.Debug,
              "AFTER HS: Set Rcv TsbPd mode: delay=%u.%03u secs",
              m_iTsbPdDelay_ms / 1000,
              m_iTsbPdDelay_ms % 1000);
    }
    else
    {
        HLOGC(mglog.Debug, log << "AFTER HS: Rcv TsbPd mode not set");
    }
}

void CUDT::updateSrtSndSettings()
{
    if (m_bPeerTsbPd)
    {
        /* We are TsbPd sender */
        // XXX Check what happened here.
        // m_iPeerTsbPdDelay_ms = m_CongCtl->getSndPeerTsbPdDelay();// + ((m_iRTT + (4 * m_iRTTVar)) / 1000);
        /*
         * For sender to apply Too-Late Packet Drop
         * option (m_bTLPktDrop) must be enabled and receiving peer shall support it
         */
        HLOGF(mglog.Debug,
              "AFTER HS: Set Snd TsbPd mode %s: delay=%d.%03d secs",
              m_bPeerTLPktDrop ? "with TLPktDrop" : "without TLPktDrop",
              m_iPeerTsbPdDelay_ms / 1000,
              m_iPeerTsbPdDelay_ms % 1000);
    }
    else
    {
        HLOGC(mglog.Debug, log << "AFTER HS: Snd TsbPd mode not set");
    }
}

void CUDT::updateAfterSrtHandshake(int srt_cmd, int hsv)
{

    switch (srt_cmd)
    {
    case SRT_CMD_HSREQ:
    case SRT_CMD_HSRSP:
        break;
    default:
        return;
    }

    // The only possibility here is one of these two:
    // - Agent is RESPONDER and it receives HSREQ.
    // - Agent is INITIATOR and it receives HSRSP.
    //
    // In HSv4, INITIATOR is sender and RESPONDER is receiver.
    // In HSv5, both are sender AND receiver.
    //
    // This function will be called only ONCE in this
    // instance, through either HSREQ or HSRSP.

    if (hsv > HS_VERSION_UDT4)
    {
        updateSrtRcvSettings();
        updateSrtSndSettings();
    }
    else if (srt_cmd == SRT_CMD_HSRSP)
    {
        // HSv4 INITIATOR is sender
        updateSrtSndSettings();
    }
    else
    {
        // HSv4 RESPONDER is receiver
        updateSrtRcvSettings();
    }
}

int CUDT::packLostData(CPacket &packet, uint64_t &origintime)
{
    // protect m_iSndLastDataAck from updating by ACK processing
    CGuard ackguard(m_RecvAckLock);

    while ((packet.m_iSeqNo = m_pSndLossList->popLostSeq()) >= 0)
    {
        const int offset = CSeqNo::seqoff(m_iSndLastDataAck, packet.m_iSeqNo);
        if (offset < 0)
        {
            LOGC(dlog.Error,
                 log << "IPE: packLostData: LOST packet negative offset: seqoff(m_iSeqNo " << packet.m_iSeqNo
                     << ", m_iSndLastDataAck " << m_iSndLastDataAck << ")=" << offset << ". Continue");
            continue;
        }

        int msglen;

        const int payload = m_pSndBuffer->readData(&(packet.m_pcData), offset, packet.m_iMsgNo, origintime, msglen);
        SRT_ASSERT(payload != 0);
        if (payload == -1)
        {
            int32_t seqpair[2];
            seqpair[0] = packet.m_iSeqNo;
            seqpair[1] = CSeqNo::incseq(seqpair[0], msglen);
            sendCtrl(UMSG_DROPREQ, &packet.m_iMsgNo, seqpair, 8);

            // only one msg drop request is necessary
            m_pSndLossList->remove(seqpair[1]);

            // skip all dropped packets
            if (CSeqNo::seqcmp(m_iSndCurrSeqNo, CSeqNo::incseq(seqpair[1])) < 0)
                m_iSndCurrSeqNo = CSeqNo::incseq(seqpair[1]);

            continue;
        }
        // NOTE: This is just a sanity check. Returning 0 is impossible to happen
        // in case of retransmission. If the offset was a positive value, then the
        // block must exist in the old blocks because it wasn't yet cut off by ACK
        // and has been already recorded as sent (otherwise the peer wouldn't send
        // back the loss report). May something happen here in case when the send
        // loss record has been updated by the FASTREXMIT.
        else if (payload == 0)
            continue;

        // At this point we no longer need the ACK lock,
        // because we are going to return from the function.
        // Therefore unlocking in order not to block other threads.
        ackguard.forceUnlock();

        CGuard::enterCS(m_StatsLock);
        ++m_stats.traceRetrans;
        ++m_stats.retransTotal;
        m_stats.traceBytesRetrans += payload;
        m_stats.bytesRetransTotal += payload;
        CGuard::leaveCS(m_StatsLock);

        // Despite the contextual interpretation of packet.m_iMsgNo around
        // CSndBuffer::readData version 2 (version 1 doesn't return -1), in this particular
        // case we can be sure that this is exactly the value of PH_MSGNO as a bitset.
        // So, set here the rexmit flag if the peer understands it.
        if (m_bPeerRexmitFlag)
        {
            packet.m_iMsgNo |= PACKET_SND_REXMIT;
        }

        return payload;
    }

    return 0;
}

int CUDT::packData(CPacket &packet, uint64_t &ts_tk)
{
    int      payload           = 0;
    bool     probe             = false;
    uint64_t origintime        = 0;
    bool     new_packet_packed = false;
    bool     filter_ctl_pkt    = false;

    int kflg = EK_NOENC;

    uint64_t entertime_tk;
    CTimer::rdtsc(entertime_tk);

#if 0 // debug: TimeDiff histogram
   static int lldiffhisto[23] = {0};
   static int llnodiff = 0;
   if (m_ullTargetTime_tk != 0)
   {
      int ofs = 11 + ((entertime_tk - m_ullTargetTime_tk)/(int64_t)m_ullCPUFrequency)/1000;
      if (ofs < 0) ofs = 0;
      else if (ofs > 22) ofs = 22;
      lldiffhisto[ofs]++;
   }
   else if(m_ullTargetTime_tk == 0)
   {
      llnodiff++;
   }
   static int callcnt = 0;
   if (!(callcnt++ % 5000)) {
      fprintf(stderr, "%6d %6d %6d %6d %6d %6d %6d %6d %6d %6d %6d %6d\n",
        lldiffhisto[0],lldiffhisto[1],lldiffhisto[2],lldiffhisto[3],lldiffhisto[4],lldiffhisto[5],
        lldiffhisto[6],lldiffhisto[7],lldiffhisto[8],lldiffhisto[9],lldiffhisto[10],lldiffhisto[11]);
      fprintf(stderr, "%6d %6d %6d %6d %6d %6d %6d %6d %6d %6d %6d %6d\n",
        lldiffhisto[12],lldiffhisto[13],lldiffhisto[14],lldiffhisto[15],lldiffhisto[16],lldiffhisto[17],
        lldiffhisto[18],lldiffhisto[19],lldiffhisto[20],lldiffhisto[21],lldiffhisto[21],llnodiff);
   }
#endif
    if ((0 != m_ullTargetTime_tk) && (entertime_tk > m_ullTargetTime_tk))
        m_ullTimeDiff_tk += entertime_tk - m_ullTargetTime_tk;

    string reason;

    payload = packLostData(packet, origintime);
    if (payload > 0)
    {
        reason = "reXmit";
    }
    else if (m_PacketFilter &&
             m_PacketFilter.packControlPacket(Ref(packet), m_iSndCurrSeqNo, m_pCryptoControl->getSndCryptoFlags()))
    {
        HLOGC(mglog.Debug, log << "filter: filter/CTL packet ready - packing instead of data.");
        payload        = packet.getLength();
        reason         = "filter";
        filter_ctl_pkt = true; // Mark that this packet ALREADY HAS timestamp field and it should not be set

        // Stats

        {
            CGuard lg(m_StatsLock);
            ++m_stats.sndFilterExtra;
            ++m_stats.sndFilterExtraTotal;
        }
    }
    else
    {
        // If no loss, and no packetfilter control packet, pack a new packet.

        // check congestion/flow window limit
        int cwnd    = std::min(int(m_iFlowWindowSize), int(m_dCongestionWindow));
        int seqdiff = CSeqNo::seqlen(m_iSndLastAck, CSeqNo::incseq(m_iSndCurrSeqNo));
        if (cwnd >= seqdiff)
        {
            // XXX Here it's needed to set kflg to msgno_bitset in the block stored in the
            // send buffer. This should be somehow avoided, the crypto flags should be set
            // together with encrypting, and the packet should be sent as is, when rexmitting.
            // It would be nice to research as to whether CSndBuffer::Block::m_iMsgNoBitset field
            // isn't a useless redundant state copy. If it is, then taking the flags here can be removed.
            kflg    = m_pCryptoControl->getSndCryptoFlags();
            payload = m_pSndBuffer->readData(&(packet.m_pcData), packet.m_iMsgNo, origintime, kflg);
            if (payload)
            {
                m_iSndCurrSeqNo = CSeqNo::incseq(m_iSndCurrSeqNo);
                // m_pCryptoControl->m_iSndCurrSeqNo = m_iSndCurrSeqNo;

                packet.m_iSeqNo = m_iSndCurrSeqNo;

                // every 16 (0xF) packets, a packet pair is sent
                if ((packet.m_iSeqNo & PUMASK_SEQNO_PROBE) == 0)
                    probe = true;

                new_packet_packed = true;
            }
            else
            {
                m_ullTargetTime_tk = 0;
                m_ullTimeDiff_tk   = 0;
                ts_tk              = 0;
                return 0;
            }
        }
        else
        {
            HLOGC(dlog.Debug,
                  log << "packData: CONGESTED: cwnd=min(" << m_iFlowWindowSize << "," << m_dCongestionWindow
                      << ")=" << cwnd << " seqlen=(" << m_iSndLastAck << "-" << m_iSndCurrSeqNo << ")=" << seqdiff);
            m_ullTargetTime_tk = 0;
            m_ullTimeDiff_tk   = 0;
            ts_tk              = 0;
            return 0;
        }

        reason = "normal";
    }

    // Normally packet.m_iTimeStamp field is set exactly here,
    // usually as taken from m_StartTime and current time, unless live
    // mode in which case it is based on 'origintime' as set during scheduling.
    // In case when this is a filter control packet, the m_iTimeStamp field already
    // contains the exactly needed value, and it's a timestamp clip, not a real
    // timestamp.
    if (!filter_ctl_pkt)
    {
        if (m_bPeerTsbPd)
        {
            /*
             * When timestamp is carried over in this sending stream from a received stream,
             * it may be older than the session start time causing a negative packet time
             * that may block the receiver's Timestamp-based Packet Delivery.
             * XXX Isn't it then better to not decrease it by m_StartTime? As long as it
             * doesn't screw up the start time on the other side.
             */
            if (origintime >= m_stats.startTime)
                packet.m_iTimeStamp = int(origintime - m_stats.startTime);
            else
                packet.m_iTimeStamp = int(CTimer::getTime() - m_stats.startTime);
        }
        else
        {
            packet.m_iTimeStamp = int(CTimer::getTime() - m_stats.startTime);
        }
    }

    packet.m_iID = m_PeerID;
    packet.setLength(payload);

    /* Encrypt if 1st time this packet is sent and crypto is enabled */
    if (kflg)
    {
        // XXX Encryption flags are already set on the packet before calling this.
        // See readData() above.
        if (m_pCryptoControl->encrypt(Ref(packet)))
        {
            // Encryption failed
            //>>Add stats for crypto failure
            ts_tk = 0;
            LOGC(dlog.Error, log << "ENCRYPT FAILED - packet won't be sent, size=" << payload);
            return -1; // Encryption failed
        }
        payload = packet.getLength(); /* Cipher may change length */
        reason += " (encrypted)";
    }

    if (new_packet_packed && m_PacketFilter)
    {
        HLOGC(mglog.Debug, log << "filter: Feeding packet for source clip");
        m_PacketFilter.feedSource(Ref(packet));
    }

#if ENABLE_HEAVY_LOGGING // Required because of referring to MessageFlagStr()
    HLOGC(mglog.Debug,
          log << CONID() << "packData: " << reason << " packet seq=" << packet.m_iSeqNo << " (ACK=" << m_iSndLastAck
              << " ACKDATA=" << m_iSndLastDataAck << " MSG/FLAGS: " << packet.MessageFlagStr() << ")");
#endif

    // Fix keepalive
    m_ullLastSndTime_tk = entertime_tk;

    considerLegacySrtHandshake(0);

    // WARNING: TEV_SEND is the only event that is reported from
    // the CSndQueue::worker thread. All others are reported from
    // CRcvQueue::worker. If you connect to this signal, make sure
    // that you are aware of prospective simultaneous access.
    updateCC(TEV_SEND, &packet);

    // XXX This was a blocked code also originally in UDT. Probably not required.
    // Left untouched for historical reasons.
    // Might be possible that it was because of that this is send from
    // different thread than the rest of the signals.
    // m_pSndTimeWindow->onPktSent(packet.m_iTimeStamp);

    CGuard::enterCS(m_StatsLock);
    m_stats.traceBytesSent += payload;
    m_stats.bytesSentTotal += payload;
    ++m_stats.traceSent;
    ++m_stats.sentTotal;
    CGuard::leaveCS(m_StatsLock);

    if (probe)
    {
        // sends out probing packet pair
        ts_tk = entertime_tk;
        probe = false;
    }
    else
    {
#if USE_BUSY_WAITING
        ts_tk = entertime_tk + m_ullInterval_tk;
#else
        if (m_ullTimeDiff_tk >= m_ullInterval_tk)
        {
            ts_tk = entertime_tk;
            m_ullTimeDiff_tk -= m_ullInterval_tk;
        }
        else
        {
            ts_tk = entertime_tk + m_ullInterval_tk - m_ullTimeDiff_tk;
            m_ullTimeDiff_tk = 0;
        }
#endif
    }

    m_ullTargetTime_tk = ts_tk;

    return payload;
}

// This is a close request, but called from the
void CUDT::processClose()
{
    sendCtrl(UMSG_SHUTDOWN);

    m_bShutdown      = true;
    m_bClosing       = true;
    m_bBroken        = true;
    m_iBrokenCounter = 60;

    HLOGP(mglog.Debug, "processClose: sent message and set flags");

    if (m_bTsbPd)
    {
        HLOGP(mglog.Debug, "processClose: lock-and-signal TSBPD");
        CGuard rl(m_RecvLock);
        pthread_cond_signal(&m_RcvTsbPdCond);
    }

    // Signal the sender and recver if they are waiting for data.
    releaseSynch();
    // Unblock any call so they learn the connection_broken error
    s_UDTUnited.m_EPoll.update_events(m_SocketID, m_sPollID, UDT_EPOLL_ERR, true);

    HLOGP(mglog.Debug, "processClose: triggering timer event to spread the bad news");
    CTimer::triggerEvent();
}

void CUDT::sendLossReport(const std::vector<std::pair<int32_t, int32_t> > &loss_seqs)
{
    typedef vector<pair<int32_t, int32_t> > loss_seqs_t;

    vector<int32_t> seqbuffer;
    seqbuffer.reserve(2 * loss_seqs.size()); // pessimistic
    for (loss_seqs_t::const_iterator i = loss_seqs.begin(); i != loss_seqs.end(); ++i)
    {
        if (i->first == i->second)
        {
            seqbuffer.push_back(i->first);
            HLOGF(mglog.Debug, "lost packet %d: sending LOSSREPORT", i->first);
        }
        else
        {
            seqbuffer.push_back(i->first | LOSSDATA_SEQNO_RANGE_FIRST);
            seqbuffer.push_back(i->second);
            HLOGF(mglog.Debug,
                  "lost packets %d-%d (%d packets): sending LOSSREPORT",
                  i->first,
                  i->second,
                  1 + CSeqNo::seqcmp(i->second, i->first));
        }
    }

    if (!seqbuffer.empty())
    {
        sendCtrl(UMSG_LOSSREPORT, NULL, &seqbuffer[0], seqbuffer.size());
    }
}

int CUDT::processData(CUnit *in_unit)
{
    CPacket &packet = in_unit->m_Packet;

    // XXX This should be called (exclusively) here:
    // m_pRcvBuffer->addLocalTsbPdDriftSample(packet.getMsgTimeStamp());
    // Just heard from the peer, reset the expiration count.
    m_iEXPCount = 1;
    uint64_t currtime_tk;
    CTimer::rdtsc(currtime_tk);
    m_ullLastRspTime_tk = currtime_tk;

    // We are receiving data, start tsbpd thread if TsbPd is enabled
    if (m_bTsbPd && pthread_equal(m_RcvTsbPdThread, pthread_t()))
    {
        HLOGP(mglog.Debug, "Spawning TSBPD thread");
        int st = 0;
        {
            ThreadName tn("SRT:TsbPd");
            st = pthread_create(&m_RcvTsbPdThread, NULL, CUDT::tsbpd, this);
        }
        if (st != 0)
            return -1;
    }

    const int pktrexmitflag = m_bPeerRexmitFlag ? (packet.getRexmitFlag() ? 1 : 0) : 2;
#if ENABLE_HEAVY_LOGGING
    static const char *const rexmitstat[] = {"ORIGINAL", "REXMITTED", "RXS-UNKNOWN"};
    string                   rexmit_reason;
#endif

    if (pktrexmitflag == 1)
    {
        // This packet was retransmitted
        CGuard::enterCS(m_StatsLock);
        m_stats.traceRcvRetrans++;
        CGuard::leaveCS(m_StatsLock);

#if ENABLE_HEAVY_LOGGING
        // Check if packet was retransmitted on request or on ack timeout
        // Search the sequence in the loss record.
        rexmit_reason = " by ";
        if (!m_pRcvLossList->find(packet.m_iSeqNo, packet.m_iSeqNo))
            rexmit_reason += "REQUEST";
        else
            rexmit_reason += "ACK-TMOUT";
#endif
    }

    HLOGC(dlog.Debug,
          log << CONID() << "processData: RECEIVED DATA: size=" << packet.getLength() << " seq=" << packet.getSeqNo());

    updateCC(TEV_RECEIVE, &packet);
    ++m_iPktCount;

    const int pktsz = packet.getLength();
    // Update time information
   // XXX Note that this adds the byte size of a packet
   // of which we don't yet know as to whether this has
   // carried out some useful data or some excessive data
   // that will be later discarded.
   // FIXME: before adding this on the rcv time window,
   // make sure that this packet isn't going to be
   // effectively discarded, as repeated retransmission,
   // for example, burdens the link, but doesn't better the speed.
    m_RcvTimeWindow.onPktArrival(pktsz);

   // Probe the packet pair if needed.
   // Conditions and any extra data required for the packet
   // this function will extract and test as needed.

    const bool unordered = CSeqNo::seqcmp(packet.m_iSeqNo, m_iRcvCurrSeqNo) <= 0;
    const bool retransmitted = m_bPeerRexmitFlag && packet.getRexmitFlag();

    // Retransmitted and unordered packets do not provide expected measurement.
    // We expect the 16th and 17th packet to be sent regularly,
    // otherwise measurement must be rejected.
    m_RcvTimeWindow.probeArrival(packet, unordered || retransmitted);

    CGuard::enterCS(m_StatsLock);
    m_stats.traceBytesRecv += pktsz;
    m_stats.bytesRecvTotal += pktsz;
    ++m_stats.traceRecv;
    ++m_stats.recvTotal;
    CGuard::leaveCS(m_StatsLock);

    typedef vector<pair<int32_t, int32_t> > loss_seqs_t;
    loss_seqs_t                             filter_loss_seqs;
    loss_seqs_t                             srt_loss_seqs;
    vector<CUnit *>                         incoming;
    bool                                    was_sent_in_order          = true;
    bool                                    reorder_prevent_lossreport = false;

    // If the peer doesn't understand REXMIT flag, send rexmit request
    // always immediately.
    int initial_loss_ttl = 0;
    if (m_bPeerRexmitFlag)
        initial_loss_ttl = m_iReorderTolerance;

    // After introduction of packet filtering, the "recordable loss detection"
    // does not exactly match the true loss detection. When a FEC filter is
    // working, for example, then getting one group filled with all packet but
    // the last one and the FEC control packet, in this special case this packet
    // won't be notified at all as lost because it will be recovered by the
    // filter immediately before anyone notices what happened (and the loss
    // detection for the further functionality is checked only afterwards,
    // and in this case the immediate recovery makes the loss to not be noticed
    // at all).
    //
    // Because of that the check for losses must happen BEFORE passing the packet
    // to the filter and before the filter could recover the packet before anyone
    // notices :)

    if (packet.getMsgSeq() != 0) // disregard filter-control packets, their seq may mean nothing
    {
        int diff = CSeqNo::seqoff(m_iRcvCurrPhySeqNo, packet.m_iSeqNo);
        if (diff > 1)
        {
            CGuard lg(m_StatsLock);
            int    loss = diff - 1; // loss is all that is above diff == 1
            m_stats.traceRcvLoss += loss;
            m_stats.rcvLossTotal += loss;
            uint64_t lossbytes = loss * m_pRcvBuffer->getRcvAvgPayloadSize();
            m_stats.traceRcvBytesLoss += lossbytes;
            m_stats.rcvBytesLossTotal += lossbytes;
            HLOGC(mglog.Debug,
                  log << "LOSS STATS: n=" << loss << " SEQ: [" << CSeqNo::incseq(m_iRcvCurrPhySeqNo) << " "
                      << CSeqNo::decseq(packet.m_iSeqNo) << "]");
        }

        if (diff > 0)
        {
            // Record if it was further than latest
            m_iRcvCurrPhySeqNo = packet.m_iSeqNo;
        }
    }

    {
        // Start of offset protected section
        // Prevent TsbPd thread from modifying Ack position while adding data
        // offset from RcvLastAck in RcvBuffer must remain valid between seqoff() and addData()
        CGuard recvbuf_acklock(m_RcvBufferLock);

        // vector<CUnit*> undec_units;
        if (m_PacketFilter)
        {
            // Stuff this data into the filter
            m_PacketFilter.receive(in_unit, Ref(incoming), Ref(filter_loss_seqs));
            HLOGC(mglog.Debug,
                  log << "(FILTER) fed data, received " << incoming.size() << " pkts, " << Printable(filter_loss_seqs)
                      << " loss to report, "
                      << (m_PktFilterRexmitLevel == SRT_ARQ_ALWAYS ? "FIND & REPORT LOSSES YOURSELF"
                                                                   : "REPORT ONLY THOSE"));
        }
        else
        {
            // Stuff in just one packet that has come in.
            incoming.push_back(in_unit);
        }

        bool excessive = true; // stays true unless it was successfully added

        // Needed for possibly check for needsQuickACK.
        bool incoming_belated = (CSeqNo::seqcmp(in_unit->m_Packet.m_iSeqNo, m_iRcvLastSkipAck) < 0);

        // Loop over all incoming packets that were filtered out.
        // In case when there is no filter, there's just one packet in 'incoming',
        // the one that came in the input of this function.
        for (vector<CUnit *>::iterator i = incoming.begin(); i != incoming.end(); ++i)
        {
            CUnit *  u    = *i;
            CPacket &rpkt = u->m_Packet;

            // m_iRcvLastSkipAck is the base sequence number for the receiver buffer.
            // This is the offset in the buffer; if this is negative, it means that
            // this sequence is already in the past and the buffer is not interested.
            // Meaning, this packet will be rejected, even if it could potentially be
            // one of missing packets in the transmission.
            int32_t offset = CSeqNo::seqoff(m_iRcvLastSkipAck, rpkt.m_iSeqNo);

            IF_HEAVY_LOGGING(const char *exc_type = "EXPECTED");

            if (offset < 0)
            {
                IF_HEAVY_LOGGING(exc_type = "BELATED");
                uint64_t tsbpdtime = m_pRcvBuffer->getPktTsbPdTime(rpkt.getMsgTimeStamp());
                uint64_t bltime =
                    CountIIR(uint64_t(m_stats.traceBelatedTime) * 1000, CTimer::getTime() - tsbpdtime, 0.2);

                CGuard::enterCS(m_StatsLock);
                m_stats.traceBelatedTime = double(bltime) / 1000.0;
                m_stats.traceRcvBelated++;
                CGuard::leaveCS(m_StatsLock);
                HLOGC(mglog.Debug,
                      log << CONID() << "RECEIVED: seq=" << packet.m_iSeqNo << " offset=" << offset << " (BELATED/"
                          << rexmitstat[pktrexmitflag] << rexmit_reason << ") FLAGS: " << packet.MessageFlagStr());
                continue;
            }

            const int avail_bufsize = m_pRcvBuffer->getAvailBufSize();
            if (offset >= avail_bufsize)
            {
                // This is already a sequence discrepancy. Probably there could be found
                // some way to make it continue reception by overriding the sequence and
                // make a kinda TLKPTDROP, but there has been found no reliable way to do this.
                if (m_bTsbPd && m_bTLPktDrop && m_pRcvBuffer->empty())
                {
                    // Only in live mode. In File mode this shall not be possible
                    // because the sender should stop sending in this situation.
                    // In Live mode this means that there is a gap between the
                    // lowest sequence in the empty buffer and the incoming sequence
                    // that exceeds the buffer size. Receiving data in this situation
                    // is no longer possible and this is a point of no return.
                    LOGC(mglog.Error,
                         log << CONID() << "SEQUENCE DISCREPANCY. BREAKING CONNECTION. offset=" << offset
                             << " avail=" << avail_bufsize << " ack.seq=" << m_iRcvLastSkipAck
                             << " pkt.seq=" << rpkt.m_iSeqNo << " rcv-remain=" << m_pRcvBuffer->debugGetSize());

                    // This is a scoped lock with AckLock, but for the moment
                    // when processClose() is called this lock must be taken out,
                    // otherwise this will cause a deadlock. We don't need this
                    // lock anymore, and at 'return' it will be unlocked anyway.
                    recvbuf_acklock.forceUnlock();
                    processClose();
                    return -1;
                }
                else
                {
                    LOGC(mglog.Error,
                         log << CONID() << "No room to store incoming packet: offset=" << offset
                             << " avail=" << avail_bufsize << " ack.seq=" << m_iRcvLastSkipAck
                             << " pkt.seq=" << rpkt.m_iSeqNo << " rcv-remain=" << m_pRcvBuffer->debugGetSize());
                    return -1;
                }
            }

            bool adding_successful = true;
            if (m_pRcvBuffer->addData(*i, offset) < 0)
            {
                // addData returns -1 if at the m_iLastAckPos+offset position there already is a packet.
                // So this packet is "redundant".
                IF_HEAVY_LOGGING(exc_type = "UNACKED");
                adding_successful = false;
            }
            else
            {
                IF_HEAVY_LOGGING(exc_type = "ACCEPTED");
                excessive = false;
                if (u->m_Packet.getMsgCryptoFlags())
                {
                    EncryptionStatus rc = m_pCryptoControl ? m_pCryptoControl->decrypt(Ref(u->m_Packet)) : ENCS_NOTSUP;
                    if (rc != ENCS_CLEAR)
                    {
                        // Could not decrypt
                        // Keep packet in received buffer
                        // Crypto flags are still set
                        // It will be acknowledged
                        {
                            CGuard lg(m_StatsLock);
                            m_stats.traceRcvUndecrypt += 1;
                            m_stats.traceRcvBytesUndecrypt += pktsz;
                            m_stats.m_rcvUndecryptTotal += 1;
                            m_stats.m_rcvBytesUndecryptTotal += pktsz;
                        }

                        // Log message degraded to debug because it may happen very often
                        HLOGC(dlog.Debug, log << CONID() << "ERROR: packet not decrypted, dropping data.");
                        adding_successful = false;
                        IF_HEAVY_LOGGING(exc_type = "UNDECRYPTED");
                    }
                }
            }

            HLOGC(mglog.Debug,
                  log << CONID() << "RECEIVED: seq=" << rpkt.m_iSeqNo << " offset=" << offset
                  << " BUFr=" << avail_bufsize
                  << " (" << exc_type << "/" << rexmitstat[pktrexmitflag] << rexmit_reason << ") FLAGS: "
                  << packet.MessageFlagStr());

            // Decryption should have made the crypto flags EK_NOENC.
            // Otherwise it's an error.
            if (adding_successful)
            {
                HLOGC(dlog.Debug,
                      log << "CONTIGUITY CHECK: sequence distance: " << CSeqNo::seqoff(m_iRcvCurrSeqNo, rpkt.m_iSeqNo));
                if (CSeqNo::seqcmp(rpkt.m_iSeqNo, CSeqNo::incseq(m_iRcvCurrSeqNo)) > 0) // Loss detection.
                {
                    int32_t seqlo = CSeqNo::incseq(m_iRcvCurrSeqNo);
                    int32_t seqhi = CSeqNo::decseq(rpkt.m_iSeqNo);

                    srt_loss_seqs.push_back(make_pair(seqlo, seqhi));

                    if (initial_loss_ttl)
                    {
                        // pack loss list for (possibly belated) NAK
                        // The LOSSREPORT will be sent in a while.

                        for (loss_seqs_t::iterator i = srt_loss_seqs.begin(); i != srt_loss_seqs.end(); ++i)
                        {
                            m_FreshLoss.push_back(CRcvFreshLoss(i->first, i->second, initial_loss_ttl));
                        }
                        HLOGC(mglog.Debug,
                              log << "FreshLoss: added sequences: " << Printable(srt_loss_seqs)
                                  << " tolerance: " << initial_loss_ttl);
                        reorder_prevent_lossreport = true;
                    }
                }
            }

            // Update the current largest sequence number that has been received.
            // Or it is a retransmitted packet, remove it from receiver loss list.
            if (CSeqNo::seqcmp(rpkt.m_iSeqNo, m_iRcvCurrSeqNo) > 0)
            {
                m_iRcvCurrSeqNo = rpkt.m_iSeqNo; // Latest possible received
            }
            else
            {
                unlose(rpkt); // was BELATED or RETRANSMITTED
                was_sent_in_order &= 0 != pktrexmitflag;
            }
        }

        // This is moved earlier after introducing filter because it shouldn't
        // be executed in case when the packet was rejected by the receiver buffer.
        // However now the 'excessive' condition may be true also in case when
        // a truly non-excessive packet has been received, just it has been temporarily
        // stored for better times by the filter module. This way 'excessive' is also true,
        // although the old condition that a packet with a newer sequence number has arrived
        // or arrived out of order may still be satisfied.
        if (!incoming_belated && was_sent_in_order)
        {
            // Basing on some special case in the packet, it might be required
            // to enforce sending ACK immediately (earlier than normally after
            // a given period).
            if (m_CongCtl->needsQuickACK(packet))
            {
                CTimer::rdtsc(m_ullNextACKTime_tk);
            }
        }

        if (excessive)
        {
            return -1;
        }

    } // End of recvbuf_acklock

    if (m_bClosing)
    {
        // RcvQueue worker thread can call processData while closing (or close while processData)
        // This race condition exists in the UDT design but the protection against TsbPd thread
        // (with AckLock) and decryption enlarged the probability window.
        // Application can crash deep in decrypt stack since crypto context is deleted in close.
        // RcvQueue worker thread will not necessarily be deleted with this connection as it can be
        // used by others (socket multiplexer).
        return -1;
    }

    if (incoming.empty())
    {
        // Treat as excessive. This is when a filter cumulates packets
        // until the loss is rebuilt, or eats up a filter control packet
        return -1;
    }

    if (!srt_loss_seqs.empty())
    {
        // A loss is detected
        {
            // TODO: Can unlock rcvloss after m_pRcvLossList->insert(...)?
            // And probably protect m_FreshLoss as well.

            HLOGC(mglog.Debug, log << "processData: LOSS DETECTED, %: " << Printable(srt_loss_seqs) << " - RECORDING.");
            // if record_loss == false, nothing will be contained here
            // Insert lost sequence numbers to the receiver loss list
            CGuard lg(m_RcvLossLock);
            for (loss_seqs_t::iterator i = srt_loss_seqs.begin(); i != srt_loss_seqs.end(); ++i)
            {
                // If loss found, insert them to the receiver loss list
                m_pRcvLossList->insert(i->first, i->second);
            }
        }

        const bool report_recorded_loss = !m_PacketFilter || m_PktFilterRexmitLevel == SRT_ARQ_ALWAYS;
        if (!reorder_prevent_lossreport && report_recorded_loss)
        {
            HLOGC(mglog.Debug, log << "WILL REPORT LOSSES (SRT): " << Printable(srt_loss_seqs));
            sendLossReport(srt_loss_seqs);
        }

        if (m_bTsbPd)
        {
            pthread_mutex_lock(&m_RecvLock);
            pthread_cond_signal(&m_RcvTsbPdCond);
            pthread_mutex_unlock(&m_RecvLock);
        }
    }

    // Separately report loss records of those reported by a filter.
    // ALWAYS report whatever has been reported back by a filter. Note that
    // the filter never reports anything when rexmit fallback level is ALWAYS or NEVER.
    // With ALWAYS only those are reported that were recorded here by SRT.
    // With NEVER, nothing is to be reported.
    if (!filter_loss_seqs.empty())
    {
        HLOGC(mglog.Debug, log << "WILL REPORT LOSSES (filter): " << Printable(filter_loss_seqs));
        sendLossReport(filter_loss_seqs);

        if (m_bTsbPd)
        {
            pthread_mutex_lock(&m_RecvLock);
            pthread_cond_signal(&m_RcvTsbPdCond);
            pthread_mutex_unlock(&m_RecvLock);
        }
    }

    // Now review the list of FreshLoss to see if there's any "old enough" to send UMSG_LOSSREPORT to it.

    // PERFORMANCE CONSIDERATIONS:
    // This list is quite inefficient as a data type and finding the candidate to send UMSG_LOSSREPORT
    // is linear time. On the other hand, there are some special cases that are important for performance:
    // - only the first (plus some following) could have had TTL drown to 0
    // - the only (little likely) possibility that the next-to-first record has TTL=0 is when there was
    //   a loss range split (due to unlose() of one sequence)
    // - first found record with TTL>0 means end of "ready to LOSSREPORT" records
    // So:
    // All you have to do is:
    //  - start with first element and continue with next elements, as long as they have TTL=0
    //    If so, send the loss report and remove this element.
    //  - Since the first element that has TTL>0, iterate until the end of container and decrease TTL.
    //
    // This will be efficient becase the loop to increment one field (without any condition check)
    // can be quite well optimized.

    vector<int32_t> lossdata;
    {
        CGuard lg(m_RcvLossLock);

        // XXX There was a mysterious crash around m_FreshLoss. When the initial_loss_ttl is 0
        // (that is, "belated loss report" feature is off), don't even touch m_FreshLoss.
        if (initial_loss_ttl && !m_FreshLoss.empty())
        {
            deque<CRcvFreshLoss>::iterator i = m_FreshLoss.begin();

            // Phase 1: take while TTL <= 0.
            // There can be more than one record with the same TTL, if it has happened before
            // that there was an 'unlost' (@c unlose) sequence that has split one detected loss
            // into two records.
            for (; i != m_FreshLoss.end() && i->ttl <= 0; ++i)
            {
                HLOGF(mglog.Debug,
                      "Packet seq %d-%d (%d packets) considered lost - sending LOSSREPORT",
                      i->seq[0],
                      i->seq[1],
                      CSeqNo::seqoff(i->seq[0], i->seq[1]) + 1);
                addLossRecord(lossdata, i->seq[0], i->seq[1]);
            }

            // Remove elements that have been processed and prepared for lossreport.
            if (i != m_FreshLoss.begin())
            {
                m_FreshLoss.erase(m_FreshLoss.begin(), i);
                i = m_FreshLoss.begin();
            }

            if (m_FreshLoss.empty())
            {
                HLOGP(mglog.Debug, "NO MORE FRESH LOSS RECORDS.");
            }
            else
            {
                HLOGF(mglog.Debug,
                      "STILL %" PRIzu " FRESH LOSS RECORDS, FIRST: %d-%d (%d) TTL: %d",
                      m_FreshLoss.size(),
                      i->seq[0],
                      i->seq[1],
                      1 + CSeqNo::seqoff(i->seq[0], i->seq[1]),
                      i->ttl);
            }

            // Phase 2: rest of the records should have TTL decreased.
            for (; i != m_FreshLoss.end(); ++i)
                --i->ttl;
        }
    }
    if (!lossdata.empty())
    {
        sendCtrl(UMSG_LOSSREPORT, NULL, &lossdata[0], lossdata.size());
    }

    // was_sent_in_order means either of:
    // - packet was sent in order (first if branch above)
    // - packet was sent as old, but was a retransmitted packet

    if (m_bPeerRexmitFlag && was_sent_in_order)
    {
        ++m_iConsecOrderedDelivery;
        if (m_iConsecOrderedDelivery >= 50)
        {
            m_iConsecOrderedDelivery = 0;
            if (m_iReorderTolerance > 0)
            {
                m_iReorderTolerance--;
                CGuard::enterCS(m_StatsLock);
                m_stats.traceReorderDistance--;
                CGuard::leaveCS(m_StatsLock);
                HLOGF(mglog.Debug,
                      "ORDERED DELIVERY of 50 packets in a row - decreasing tolerance to %d",
                      m_iReorderTolerance);
            }
        }
    }

    return 0;
}

/// This function is called when a packet has arrived, which was behind the current
/// received sequence - that is, belated or retransmitted. Try to remove the packet
/// from both loss records: the general loss record and the fresh loss record.
///
/// Additionally, check - if supported by the peer - whether the "latecoming" packet
/// has been sent due to retransmission or due to reordering, by checking the rexmit
/// support flag and rexmit flag itself. If this packet was surely ORIGINALLY SENT
/// it means that the current network connection suffers of packet reordering. This
/// way try to introduce a dynamic tolerance by calculating the difference between
/// the current packet reception sequence and this packet's sequence. This value
/// will be set to the tolerance value, which means that later packet retransmission
/// will not be required immediately, but only after receiving N next packets that
/// do not include the lacking packet.
/// The tolerance is not increased infinitely - it's bordered by m_iMaxReorderTolerance.
/// This value can be set in options - SRT_LOSSMAXTTL.
void CUDT::unlose(const CPacket &packet)
{
    CGuard  lg(m_RcvLossLock);
    int32_t sequence = packet.m_iSeqNo;
    m_pRcvLossList->remove(sequence);

    // Rest of this code concerns only the "belated lossreport" feature.

    bool has_increased_tolerance = false;
    bool was_reordered           = false;

    if (m_bPeerRexmitFlag)
    {
        // If the peer understands the REXMIT flag, it means that the REXMIT flag is contained
        // in the PH_MSGNO field.

        // The packet is considered coming originally (just possibly out of order), if REXMIT
        // flag is NOT set.
        was_reordered = !packet.getRexmitFlag();
        if (was_reordered)
        {
            HLOGF(mglog.Debug, "received out-of-band packet seq %d", sequence);

            const int seqdiff = abs(CSeqNo::seqcmp(m_iRcvCurrSeqNo, packet.m_iSeqNo));
            CGuard::enterCS(m_StatsLock);
            m_stats.traceReorderDistance = max(seqdiff, m_stats.traceReorderDistance);
            CGuard::leaveCS(m_StatsLock);
            if (seqdiff > m_iReorderTolerance)
            {
                const int new_tolerance = min(seqdiff, m_iMaxReorderTolerance);
                HLOGF(mglog.Debug,
                      "Belated by %d seqs - Reorder tolerance %s %d",
                      seqdiff,
                      (new_tolerance == m_iReorderTolerance) ? "REMAINS with" : "increased to",
                      new_tolerance);
                m_iReorderTolerance = new_tolerance;
                has_increased_tolerance =
                    true; // Yes, even if reorder tolerance is already at maximum - this prevents decreasing tolerance.
            }
        }
        else
        {
            HLOGC(mglog.Debug, log << CONID() << "received reXmitted packet seq=" << sequence);
        }
    }
    else
    {
        HLOGF(mglog.Debug, "received reXmitted or belated packet seq %d (distinction not supported by peer)", sequence);
    }

    // Don't do anything if "belated loss report" feature is not used.
    // In that case the FreshLoss list isn't being filled in at all, the
    // loss report is sent directly.
    // Note that this condition blocks two things being done in this function:
    // - remove given sequence from the fresh loss record
    //   (in this case it's empty anyway)
    // - decrease current reorder tolerance based on whether packets come in order
    //   (current reorder tolerance is 0 anyway)
    if (m_bPeerRexmitFlag == 0 || m_iReorderTolerance == 0)
        return;

    size_t i       = 0;
    int    had_ttl = 0;
    for (i = 0; i < m_FreshLoss.size(); ++i)
    {
        had_ttl = m_FreshLoss[i].ttl;
        switch (m_FreshLoss[i].revoke(sequence))
        {
        case CRcvFreshLoss::NONE:
            continue; // Not found. Search again.

        case CRcvFreshLoss::STRIPPED:
            goto breakbreak; // Found and the modification is applied. We're done here.

        case CRcvFreshLoss::DELETE:
            // No more elements. Kill it.
            m_FreshLoss.erase(m_FreshLoss.begin() + i);
            // Every loss is unique. We're done here.
            goto breakbreak;

        case CRcvFreshLoss::SPLIT:
            // Oh, this will be more complicated. This means that it was in between.
            {
                // So create a new element that will hold the upper part of the range,
                // and this one modify to be the lower part of the range.

                // Keep the current end-of-sequence value for the second element
                int32_t next_end = m_FreshLoss[i].seq[1];

                // seq-1 set to the end of this element
                m_FreshLoss[i].seq[1] = CSeqNo::decseq(sequence);
                // seq+1 set to the begin of the next element
                int32_t next_begin = CSeqNo::incseq(sequence);

                // Use position of the NEXT element because insertion happens BEFORE pointed element.
                // Use the same TTL (will stay the same in the other one).
                m_FreshLoss.insert(m_FreshLoss.begin() + i + 1,
                                   CRcvFreshLoss(next_begin, next_end, m_FreshLoss[i].ttl));
            }
            goto breakbreak;
        }
    }

    // Could have made the "return" instruction instead of goto, but maybe there will be something
    // to add in future, so keeping that.
breakbreak:;

    if (i != m_FreshLoss.size())
    {
        HLOGF(mglog.Debug, "sequence %d removed from belated lossreport record", sequence);
    }

    if (was_reordered)
    {
        m_iConsecOrderedDelivery = 0;
        if (has_increased_tolerance)
        {
            m_iConsecEarlyDelivery = 0; // reset counter
        }
        else if (had_ttl > 2)
        {
            ++m_iConsecEarlyDelivery; // otherwise, and if it arrived quite earlier, increase counter
            HLOGF(mglog.Debug, "... arrived at TTL %d case %d", had_ttl, m_iConsecEarlyDelivery);

            // After 10 consecutive
            if (m_iConsecEarlyDelivery >= 10)
            {
                m_iConsecEarlyDelivery = 0;
                if (m_iReorderTolerance > 0)
                {
                    m_iReorderTolerance--;
                    CGuard::enterCS(m_StatsLock);
                    m_stats.traceReorderDistance--;
                    CGuard::leaveCS(m_StatsLock);
                    HLOGF(mglog.Debug,
                          "... reached %d times - decreasing tolerance to %d",
                          m_iConsecEarlyDelivery,
                          m_iReorderTolerance);
                }
            }
        }
        // If hasn't increased tolerance, but the packet appeared at TTL less than 2, do nothing.
    }
}

void CUDT::dropFromLossLists(int32_t from, int32_t to)
{
    CGuard lg(m_RcvLossLock);
    m_pRcvLossList->remove(from, to);

    HLOGF(mglog.Debug, "TLPKTDROP seq %d-%d (%d packets)", from, to, CSeqNo::seqoff(from, to));

    if (m_bPeerRexmitFlag == 0 || m_iReorderTolerance == 0)
        return;

    // All code below concerns only "belated lossreport" feature.

    // It's highly unlikely that this is waiting to send a belated UMSG_LOSSREPORT,
    // so treat it rather as a sanity check.

    // It's enough to check if the first element of the list starts with a sequence older than 'to'.
    // If not, just do nothing.

    size_t delete_index = 0;
    for (size_t i = 0; i < m_FreshLoss.size(); ++i)
    {
        CRcvFreshLoss::Emod result = m_FreshLoss[i].revoke(from, to);
        switch (result)
        {
        case CRcvFreshLoss::DELETE:
            delete_index = i + 1; // PAST THE END
            continue;             // There may be further ranges that are included in this one, so check on.

        case CRcvFreshLoss::NONE:
        case CRcvFreshLoss::STRIPPED:
            break; // THIS BREAKS ONLY 'switch', not 'for'!

        case CRcvFreshLoss::SPLIT:; // This function never returns it. It's only a compiler shut-up.
        }

        break; // Now this breaks also FOR.
    }

    m_FreshLoss.erase(m_FreshLoss.begin(),
                      m_FreshLoss.begin() + delete_index); // with delete_index == 0 will do nothing
}

// This function, as the name states, should bake a new cookie.
int32_t CUDT::bake(const sockaddr *addr, int32_t current_cookie, int correction)
{
    static unsigned int distractor = 0;
    unsigned int        rollover   = distractor + 10;

    for (;;)
    {
        // SYN cookie
        char clienthost[NI_MAXHOST];
        char clientport[NI_MAXSERV];
        getnameinfo(addr,
                    (m_iIPversion == AF_INET) ? sizeof(sockaddr_in) : sizeof(sockaddr_in6),
                    clienthost,
                    sizeof(clienthost),
                    clientport,
                    sizeof(clientport),
                    NI_NUMERICHOST | NI_NUMERICSERV);
        int64_t timestamp = ((CTimer::getTime() - m_stats.startTime) / 60000000) + distractor -
                            correction; // secret changes every one minute
        stringstream cookiestr;
        cookiestr << clienthost << ":" << clientport << ":" << timestamp;
        union {
            unsigned char cookie[16];
            int32_t       cookie_val;
        };
        CMD5::compute(cookiestr.str().c_str(), cookie);

        if (cookie_val != current_cookie)
            return cookie_val;

        ++distractor;

        // This is just to make the loop formally breakable,
        // but this is virtually impossible to happen.
        if (distractor == rollover)
            return cookie_val;
    }
}

// XXX This is quite a mystery, why this function has a return value
// and what the purpose for it was. There's just one call of this
// function in the whole code and in that call the return value is
// ignored. Actually this call happens in the CRcvQueue::worker thread,
// where it makes a response for incoming UDP packet that might be
// a connection request. Should any error occur in this process, there
// is no way to "report error" that happened here. Basing on that
// these values in original UDT code were quite like the values
// for m_iReqType, they have been changed to URQ_* symbols, which
// may mean that the intent for the return value was to send this
// value back as a control packet back to the connector.
//
// This function is run when the CRcvQueue object is reading packets
// from the multiplexer (@c CRcvQueue::worker_RetrieveUnit) and the
// target socket ID is 0.
//
// XXX Make this function return EConnectStatus enum type (extend if needed),
// and this will be directly passed to the caller.
SRT_REJECT_REASON CUDT::processConnectRequest(const sockaddr *addr, CPacket &packet)
{
    // XXX ASSUMPTIONS:
    // [[using assert(packet.m_iID == 0)]]

    HLOGC(mglog.Debug, log << "processConnectRequest: received a connection request");

    if (m_bClosing)
    {
        m_RejectReason = SRT_REJ_CLOSE;
        HLOGC(mglog.Debug, log << "processConnectRequest: ... NOT. Rejecting because closing.");
        return m_RejectReason;
    }

    /*
     * Closing a listening socket only set bBroken
     * If a connect packet is received while closing it gets through
     * processing and crashes later.
     */
    if (m_bBroken)
    {
        m_RejectReason = SRT_REJ_CLOSE;
        HLOGC(mglog.Debug, log << "processConnectRequest: ... NOT. Rejecting because broken.");
        return m_RejectReason;
    }
    size_t exp_len =
        CHandShake::m_iContentSize; // When CHandShake::m_iContentSize is used in log, the file fails to link!

    // NOTE!!! Old version of SRT code checks if the size of the HS packet
    // is EQUAL to the above CHandShake::m_iContentSize.

    // Changed to < exp_len because we actually need that the packet
    // be at least of a size for handshake, although it may contain
    // more data, depending on what's inside.
    if (packet.getLength() < exp_len)
    {
        m_RejectReason = SRT_REJ_ROGUE;
        HLOGC(mglog.Debug,
              log << "processConnectRequest: ... NOT. Wrong size: " << packet.getLength() << " (expected: " << exp_len
                  << ")");
        return m_RejectReason;
    }

    // Dunno why the original UDT4 code only MUCH LATER was checking if the packet was UMSG_HANDSHAKE.
    // It doesn't seem to make sense to deserialize it into the handshake structure if we are not
    // sure that the packet contains the handshake at all!
    if (!packet.isControl(UMSG_HANDSHAKE))
    {
        m_RejectReason = SRT_REJ_ROGUE;
        LOGC(mglog.Error, log << "processConnectRequest: the packet received as handshake is not a handshake message");
        return m_RejectReason;
    }

    CHandShake hs;
    hs.load_from(packet.m_pcData, packet.getLength());

    // XXX MOST LIKELY this hs should be now copied into m_ConnRes field, which holds
    // the handshake structure sent from the peer (no matter the role or mode).
    // This should simplify the createSrtHandshake() function which can this time
    // simply write the crafted handshake structure into m_ConnReq, which needs no
    // participation of the local handshake and passing it as a parameter through
    // newConnection() -> acceptAndRespond() -> createSrtHandshake(). This is also
    // required as a source of the peer's information used in processing in other
    // structures.

    int32_t cookie_val = bake(addr);

    HLOGC(mglog.Debug, log << "processConnectRequest: new cookie: " << hex << cookie_val);

    // REQUEST:INDUCTION.
    // Set a cookie, a target ID, and send back the same as
    // RESPONSE:INDUCTION.
    if (hs.m_iReqType == URQ_INDUCTION)
    {
        HLOGC(mglog.Debug, log << "processConnectRequest: received type=induction, sending back with cookie+socket");

        // XXX That looks weird - the calculated md5 sum out of the given host/port/timestamp
        // is 16 bytes long, but CHandShake::m_iCookie has 4 bytes. This then effectively copies
        // only the first 4 bytes. Moreover, it's dangerous on some platforms because the char
        // array need not be aligned to int32_t - changed to union in a hope that using int32_t
        // inside a union will enforce whole union to be aligned to int32_t.
        hs.m_iCookie = cookie_val;
        packet.m_iID = hs.m_iID;

        // Ok, now's the time. The listener sets here the version 5 handshake,
        // even though the request was 4. This is because the old client would
        // simply return THE SAME version, not even looking into it, giving the
        // listener false impression as if it supported version 5.
        //
        // If the caller was really HSv4, it will simply ignore the version 5 in INDUCTION;
        // it will respond with CONCLUSION, but with its own set version, which is version 4.
        //
        // If the caller was really HSv5, it will RECOGNIZE this version 5 in INDUCTION, so
        // it will respond with version 5 when sending CONCLUSION.

        hs.m_iVersion = HS_VERSION_SRT1;

        // Additionally, set this field to a MAGIC value. This field isn't used during INDUCTION
        // by HSv4 client, HSv5 client can use it to additionally verify that this is a HSv5 listener.
        // In this field we also advertise the PBKEYLEN value. When 0, it's considered not advertised.
        hs.m_iType = SrtHSRequest::wrapFlags(true /*put SRT_MAGIC_CODE in HSFLAGS*/, m_iSndCryptoKeyLen);
        bool whether SRT_ATR_UNUSED = m_iSndCryptoKeyLen != 0;
        HLOGC(mglog.Debug,
              log << "processConnectRequest: " << (whether ? "" : "NOT ")
                  << " Advertising PBKEYLEN - value = " << m_iSndCryptoKeyLen);

        size_t size = packet.getLength();
        hs.store_to(packet.m_pcData, Ref(size));
        packet.m_iTimeStamp = int(CTimer::getTime() - m_stats.startTime);
        m_pSndQueue->sendto(addr, packet);
        return SRT_REJ_UNKNOWN; // EXCEPTION: this is a "no-error" code.
    }

    // Otherwise this should be REQUEST:CONCLUSION.
    // Should then come with the correct cookie that was
    // set in the above INDUCTION, in the HS_VERSION_SRT1
    // should also contain extra data.

    HLOGC(mglog.Debug,
          log << "processConnectRequest: received type=" << RequestTypeStr(hs.m_iReqType) << " - checking cookie...");
    if (hs.m_iCookie != cookie_val)
    {
        cookie_val = bake(addr, cookie_val, -1); // SHOULD generate an earlier, distracted cookie

        if (hs.m_iCookie != cookie_val)
        {
            m_RejectReason = SRT_REJ_RDVCOOKIE;
            HLOGC(mglog.Debug, log << "processConnectRequest: ...wrong cookie " << hex << cookie_val << ". Ignoring.");
            return m_RejectReason;
        }

        HLOGC(mglog.Debug, log << "processConnectRequest: ... correct (FIXED) cookie. Proceeding.");
    }
    else
    {
        HLOGC(mglog.Debug, log << "processConnectRequest: ... correct (ORIGINAL) cookie. Proceeding.");
    }

    int32_t id = hs.m_iID;

    // HANDSHAKE: The old client sees the version that does not match HS_VERSION_UDT4 (5).
    // In this case it will respond with URQ_ERROR_REJECT. Rest of the data are the same
    // as in the handshake request. When this message is received, the connector side should
    // switch itself to the version number HS_VERSION_UDT4 and continue the old way (that is,
    // continue sending URQ_INDUCTION, but this time with HS_VERSION_UDT4).

    bool accepted_hs = true;

    if (hs.m_iVersion == HS_VERSION_SRT1)
    {
        // No further check required.
        // The m_iType contains handshake extension flags.
    }
    else if (hs.m_iVersion == HS_VERSION_UDT4)
    {
        // In UDT, and so in older SRT version, the hs.m_iType field should contain
        // the socket type, although SRT only allowed this field to be UDT_DGRAM.
        // Older SRT version contained that value in a field, but now that this can
        // only contain UDT_DGRAM the field itself has been abandoned.
        // For the sake of any old client that reports version 4 handshake, interpret
        // this hs.m_iType field as a socket type and check if it's UDT_DGRAM.

        // Note that in HSv5 hs.m_iType contains extension flags.
        if (hs.m_iType != UDT_DGRAM)
        {
            m_RejectReason = SRT_REJ_ROGUE;
            accepted_hs    = false;
        }
    }
    else
    {
        // Unsupported version
        // (NOTE: This includes "version=0" which is a rejection flag).
        m_RejectReason = SRT_REJ_VERSION;
        accepted_hs    = false;
    }

    if (!accepted_hs)
    {
        HLOGC(mglog.Debug,
              log << "processConnectRequest: version/type mismatch. Sending REJECT code:" << m_RejectReason
                  << " MSG: " << srt_rejectreason_str(m_RejectReason));
        // mismatch, reject the request
        hs.m_iReqType = URQFailure(m_RejectReason);
        size_t size   = CHandShake::m_iContentSize;
        hs.store_to(packet.m_pcData, Ref(size));
        packet.m_iID        = id;
        packet.m_iTimeStamp = int(CTimer::getTime() - m_stats.startTime);
        m_pSndQueue->sendto(addr, packet);
    }
    else
    {
        SRT_REJECT_REASON error  = SRT_REJ_UNKNOWN;
        int               result = s_UDTUnited.newConnection(m_SocketID, addr, &hs, packet, Ref(error));

        // This is listener - m_RejectReason need not be set
        // because listener has no functionality of giving the app
        // insight into rejected callers.

        // --->
        //        (global.) CUDTUnited::updateListenerMux
        //        (new Socket.) CUDT::acceptAndRespond
        if (result == -1)
        {
            hs.m_iReqType = URQFailure(error);
            LOGF(mglog.Error, "UU:newConnection: rsp(REJECT): %d - %s", hs.m_iReqType, srt_rejectreason_str(error));
        }

        // CONFUSION WARNING!
        //
        // The newConnection() will call acceptAndRespond() if the processing
        // was successful - IN WHICH CASE THIS PROCEDURE SHOULD DO NOTHING.
        // Ok, almost nothing - see update_events below.
        //
        // If newConnection() failed, acceptAndRespond() will not be called.
        // Ok, more precisely, the thing that acceptAndRespond() is expected to do
        // will not be done (this includes sending any response to the peer).
        //
        // Now read CAREFULLY. The newConnection() will return:
        //
        // - -1: The connection processing failed due to errors like:
        //       - memory alloation error
        //       - listen backlog exceeded
        //       - any error propagated from CUDT::open and CUDT::acceptAndRespond
        // - 0: The connection already exists
        // - 1: Connection accepted.
        //
        // So, update_events is called only if the connection is established.
        // Both 0 (repeated) and -1 (error) require that a response be sent.
        // The CPacket object that has arrived as a connection request is here
        // reused for the connection rejection response (see URQ_ERROR_REJECT set
        // as m_iReqType).

        // send back a response if connection failed or connection already existed
        // new connection response should be sent in acceptAndRespond()
        if (result != 1)
        {
            HLOGC(mglog.Debug,
                  log << CONID() << "processConnectRequest: sending ABNORMAL handshake info req="
                      << RequestTypeStr(hs.m_iReqType));
            size_t size = CHandShake::m_iContentSize;
            hs.store_to(packet.m_pcData, Ref(size));
            packet.m_iID        = id;
            packet.m_iTimeStamp = int(CTimer::getTime() - m_stats.startTime);
            m_pSndQueue->sendto(addr, packet);
        }
        else
        {
            // a new connection has been created, enable epoll for write
            s_UDTUnited.m_EPoll.update_events(m_SocketID, m_sPollID, UDT_EPOLL_OUT, true);
        }
    }
    LOGC(mglog.Note, log << "listen ret: " << hs.m_iReqType << " - " << RequestTypeStr(hs.m_iReqType));

    return RejectReasonForURQ(hs.m_iReqType);
}

void CUDT::addLossRecord(std::vector<int32_t> &lr, int32_t lo, int32_t hi)
{
    if (lo == hi)
        lr.push_back(lo);
    else
    {
        lr.push_back(lo | LOSSDATA_SEQNO_RANGE_FIRST);
        lr.push_back(hi);
    }
}

void CUDT::checkACKTimer(uint64_t currtime_tk)
{
    if (currtime_tk > m_ullNextACKTime_tk // ACK time has come
                                          // OR the number of sent packets since last ACK has reached
                                          // the congctl-defined value of ACK Interval
                                          // (note that none of the builtin congctls defines ACK Interval)
        || (m_CongCtl->ACKMaxPackets() > 0 && m_iPktCount >= m_CongCtl->ACKMaxPackets()))
    {
        // ACK timer expired or ACK interval is reached
        sendCtrl(UMSG_ACK);
        CTimer::rdtsc(currtime_tk);

        const int ack_interval_tk =
            m_CongCtl->ACKTimeout_us() > 0 ? m_CongCtl->ACKTimeout_us() * m_ullCPUFrequency : m_ullACKInt_tk;
        m_ullNextACKTime_tk = currtime_tk + ack_interval_tk;

        m_iPktCount      = 0;
        m_iLightACKCount = 1;
    }
    // Or the transfer rate is so high that the number of packets
    // have reached the value of SelfClockInterval * LightACKCount before
    // the time has come according to m_ullNextACKTime_tk. In this case a "lite ACK"
    // is sent, which doesn't contain statistical data and nothing more
    // than just the ACK number. The "fat ACK" packets will be still sent
    // normally according to the timely rules.
    else if (m_iPktCount >= SELF_CLOCK_INTERVAL * m_iLightACKCount)
    {
        // send a "light" ACK
        sendCtrl(UMSG_ACK, NULL, NULL, SEND_LITE_ACK);
        ++m_iLightACKCount;
    }
}

void CUDT::checkNAKTimer(uint64_t currtime_tk)
{
    // XXX The problem with working NAKREPORT with SRT_ARQ_ONREQ
    // is not that it would be inappropriate, but because it's not
    // implemented. The reason for it is that the structure of the
    // loss list container (m_pRcvLossList) is such that it is expected
    // that the loss records are ordered by sequence numbers (so
    // that two ranges sticking together are merged in place).
    // Unfortunately in case of SRT_ARQ_ONREQ losses must be recorded
    // as before, but they should not be reported, until confirmed
    // by the filter. By this reason they appear often out of order
    // and for adding them properly the loss list container wasn't
    // prepared. This then requires some more effort to implement.
    if (!m_bRcvNakReport || m_PktFilterRexmitLevel != SRT_ARQ_ALWAYS)
        return;

    /*
     * m_bRcvNakReport enables NAK reports for SRT.
     * Retransmission based on timeout is bandwidth consuming,
     * not knowing what to retransmit when the only NAK sent by receiver is lost,
     * all packets past last ACK are retransmitted (rexmitMethod() == SRM_FASTREXMIT).
     */
    const int loss_len = m_pRcvLossList->getLossLength();
    SRT_ASSERT(loss_len >= 0);

    if (loss_len > 0)
    {
        if (currtime_tk <= m_ullNextNAKTime_tk)
            return; // wait for next NAK time

        sendCtrl(UMSG_LOSSREPORT);
    }

    m_ullNextNAKTime_tk = currtime_tk + m_ullNAKInt_tk;
}

bool CUDT::checkExpTimer(uint64_t currtime_tk)
{
    // In UDT the m_bUserDefinedRTO and m_iRTO were in CCC class.
    // There's nothing in the original code that alters these values.

    uint64_t next_exp_time_tk;
    if (m_CongCtl->RTO())
    {
        next_exp_time_tk = m_ullLastRspTime_tk + m_CongCtl->RTO() * m_ullCPUFrequency;
    }
    else
    {
        uint64_t exp_int_tk = (m_iEXPCount * (m_iRTT + 4 * m_iRTTVar) + COMM_SYN_INTERVAL_US) * m_ullCPUFrequency;
        if (exp_int_tk < m_iEXPCount * m_ullMinExpInt_tk)
            exp_int_tk = m_iEXPCount * m_ullMinExpInt_tk;
        next_exp_time_tk = m_ullLastRspTime_tk + exp_int_tk;
    }

    if (currtime_tk <= next_exp_time_tk)
        return false;

    // ms -> us
    const int PEER_IDLE_TMO_US = m_iOPT_PeerIdleTimeout * 1000;
    // Haven't received any information from the peer, is it dead?!
    // timeout: at least 16 expirations and must be greater than 5 seconds
    if ((m_iEXPCount > COMM_RESPONSE_MAX_EXP) &&
        (currtime_tk - m_ullLastRspTime_tk > PEER_IDLE_TMO_US * m_ullCPUFrequency))
    {
        //
        // Connection is broken.
        // UDT does not signal any information about this instead of to stop quietly.
        // Application will detect this when it calls any UDT methods next time.
        //
        HLOGC(mglog.Debug,
              log << "CONNECTION EXPIRED after " << ((currtime_tk - m_ullLastRspTime_tk) / m_ullCPUFrequency) << "ms");
        m_bClosing       = true;
        m_bBroken        = true;
        m_iBrokenCounter = 30;

        // update snd U list to remove this socket
        m_pSndQueue->m_pSndUList->update(this, CSndUList::DO_RESCHEDULE);

        releaseSynch();

        // app can call any UDT API to learn the connection_broken error
        s_UDTUnited.m_EPoll.update_events(m_SocketID, m_sPollID, UDT_EPOLL_IN | UDT_EPOLL_OUT | UDT_EPOLL_ERR, true);

        CTimer::triggerEvent();

        return true;
    }

    HLOGC(mglog.Debug,
          log << "EXP TIMER: count=" << m_iEXPCount << "/" << (+COMM_RESPONSE_MAX_EXP) << " elapsed="
              << ((currtime_tk - m_ullLastRspTime_tk) / m_ullCPUFrequency) << "/" << (+PEER_IDLE_TMO_US) << "us");

    ++m_iEXPCount;

    /*
     * (keepalive fix)
     * duB:
     * It seems there is confusion of the direction of the Response here.
     * LastRspTime is supposed to be when receiving (data/ctrl) from peer
     * as shown in processCtrl and processData,
     * Here we set because we sent something?
     *
     * Disabling this code that prevent quick reconnection when peer disappear
     */
    // Reset last response time since we've just sent a heart-beat.
    // (fixed) m_ullLastRspTime_tk = currtime_tk;

    return false;
}

void CUDT::checkRexmitTimer(uint64_t currtime_tk)
{
    /* There are two algorithms of blind packet retransmission: LATEREXMIT and FASTREXMIT.
     *
     * LATEREXMIT is only used with FileCC.
     * The mode is triggered when some time has passed since the last ACK from
     * the receiver, while there is still some unacknowledged data in the sender's buffer,
     * and the loss list is empty.
     *
     * FASTREXMIT is only used with LiveCC.
     * The mode is triggered if the receiver does not send periodic NAK reports,
     * when some time has passed since the last ACK from the receiver,
     * while there is still some unacknowledged data in the sender's buffer.
     *
     * In case the above conditions are met, the unacknowledged packets
     * in the sender's buffer will be added to loss list and retransmitted.
     */

    const uint64_t rtt_syn = (m_iRTT + 4 * m_iRTTVar + 2 * COMM_SYN_INTERVAL_US);
    const uint64_t exp_int = (m_iReXmitCount * rtt_syn + COMM_SYN_INTERVAL_US) * m_ullCPUFrequency;

    if (currtime_tk <= (m_ullLastRspAckTime_tk + exp_int))
        return;

    // If there is no unacknowledged data in the sending buffer,
    // then there is nothing to retransmit.
    if (m_pSndBuffer->getCurrBufSize() <= 0)
        return;

    const bool is_laterexmit = m_CongCtl->rexmitMethod() == SrtCongestion::SRM_LATEREXMIT;
    const bool is_fastrexmit = m_CongCtl->rexmitMethod() == SrtCongestion::SRM_FASTREXMIT;

    // If the receiver will send periodic NAK reports, then FASTREXMIT is inactive.
    // MIND that probably some method of "blind rexmit" MUST BE DONE, when TLPKTDROP is off.
    if (is_fastrexmit && m_bPeerNakReport)
        return;

    // We need to retransmit only when the data in the sender's buffer was already sent.
    // Otherwise it might still be sent regulary.
    bool retransmit = false;
    // - the sender loss list is empty (the receiver didn't send any LOSSREPORT, or LOSSREPORT was lost on track)
    if (is_laterexmit && (CSeqNo::incseq(m_iSndCurrSeqNo) != m_iSndLastAck) && m_pSndLossList->getLossLength() == 0)
        retransmit = true;

    if (is_fastrexmit && (CSeqNo::seqoff(m_iSndLastAck, CSeqNo::incseq(m_iSndCurrSeqNo)) > 0))
        retransmit = true;

    if (retransmit)
    {
        // Sender: Insert all the packets sent after last received acknowledgement into the sender loss list.
        CGuard acklock(m_RecvAckLock); // Protect packet retransmission
        // Resend all unacknowledged packets on timeout, but only if there is no packet in the loss list
        const int32_t csn = m_iSndCurrSeqNo;
        const int     num = m_pSndLossList->insert(m_iSndLastAck, csn);
        if (num > 0)
        {
            CGuard::enterCS(m_StatsLock);
            m_stats.traceSndLoss += num;
            m_stats.sndLossTotal += num;
            CGuard::leaveCS(m_StatsLock);

            HLOGC(mglog.Debug,
                  log << CONID() << "ENFORCED " << (is_laterexmit ? "LATEREXMIT" : "FASTREXMIT")
                      << " by ACK-TMOUT (scheduling): " << CSeqNo::incseq(m_iSndLastAck) << "-" << csn << " ("
                      << CSeqNo::seqoff(m_iSndLastAck, csn) << " packets)");
        }
    }

    ++m_iReXmitCount;

    checkSndTimers(DONT_REGEN_KM);
    const ECheckTimerStage stage = is_fastrexmit ? TEV_CHT_FASTREXMIT : TEV_CHT_REXMIT;
    updateCC(TEV_CHECKTIMER, stage);

    // immediately restart transmission
    m_pSndQueue->m_pSndUList->update(this, CSndUList::DO_RESCHEDULE);
}

void CUDT::checkTimers()
{
    // update CC parameters
    updateCC(TEV_CHECKTIMER, TEV_CHT_INIT);
    // uint64_t minint = (uint64_t)(m_ullCPUFrequency * m_pSndTimeWindow->getMinPktSndInt() * 0.9);
    // if (m_ullInterval_tk < minint)
    //   m_ullInterval_tk = minint;
    // NOTE: This commented-out ^^^ code was commented out in original UDT. Leaving for historical reasons

    uint64_t currtime_tk;
    CTimer::rdtsc(currtime_tk);

    // This is a very heavy log, unblock only for temporary debugging!
#if 0
    HLOGC(mglog.Debug, log << CONID() << "checkTimers: nextacktime=" << FormatTime(m_ullNextACKTime_tk)
        << " AckInterval=" << m_iACKInterval
        << " pkt-count=" << m_iPktCount << " liteack-count=" << m_iLightACKCount);
#endif

    // Check if it is time to send ACK
    checkACKTimer(currtime_tk);

    // Check if it is time to send a loss report
    checkNAKTimer(currtime_tk);

    // Check if the connection is expired
    if (checkExpTimer(currtime_tk))
        return;

    // Check if FAST or LATE packet retransmission is required
    checkRexmitTimer(currtime_tk);

    //   uint64_t exp_int = (m_iRTT + 4 * m_iRTTVar + COMM_SYN_INTERVAL_US) * m_ullCPUFrequency;
    if (currtime_tk > m_ullLastSndTime_tk + (COMM_KEEPALIVE_PERIOD_US * m_ullCPUFrequency))
    {
        sendCtrl(UMSG_KEEPALIVE);
        HLOGP(mglog.Debug, "KEEPALIVE");
    }
}

void CUDT::addEPoll(const int eid)
{
    CGuard::enterCS(s_UDTUnited.m_EPoll.m_EPollLock);
    m_sPollID.insert(eid);
    CGuard::leaveCS(s_UDTUnited.m_EPoll.m_EPollLock);

    if (!stillConnected())
        return;

    CGuard::enterCS(m_RecvLock);
    if (m_pRcvBuffer->isRcvDataReady())
    {
        s_UDTUnited.m_EPoll.update_events(m_SocketID, m_sPollID, UDT_EPOLL_IN, true);
    }
    CGuard::leaveCS(m_RecvLock);

    if (m_iSndBufSize > m_pSndBuffer->getCurrBufSize())
    {
        s_UDTUnited.m_EPoll.update_events(m_SocketID, m_sPollID, UDT_EPOLL_OUT, true);
    }
}

void CUDT::removeEPoll(const int eid)
{
    // clear IO events notifications;
    // since this happens after the epoll ID has been removed, they cannot be set again
    set<int> remove;
    remove.insert(eid);
    s_UDTUnited.m_EPoll.update_events(m_SocketID, remove, UDT_EPOLL_IN | UDT_EPOLL_OUT, false);

    CGuard::enterCS(s_UDTUnited.m_EPoll.m_EPollLock);
    m_sPollID.erase(eid);
    CGuard::leaveCS(s_UDTUnited.m_EPoll.m_EPollLock);
}

void CUDT::ConnectSignal(ETransmissionEvent evt, EventSlot sl)
{
    if (evt >= TEV__SIZE)
        return; // sanity check

    m_Slots[evt].push_back(sl);
}

void CUDT::DisconnectSignal(ETransmissionEvent evt)
{
    if (evt >= TEV__SIZE)
        return; // sanity check

    m_Slots[evt].clear();
}

void CUDT::EmitSignal(ETransmissionEvent tev, EventVariant var)
{
    for (std::vector<EventSlot>::iterator i = m_Slots[tev].begin(); i != m_Slots[tev].end(); ++i)
    {
        i->emit(tev, var);
    }
}

int CUDT::getsndbuffer(SRTSOCKET u, size_t *blocks, size_t *bytes)
{
    CUDTSocket *s = s_UDTUnited.locate(u);
    if (!s || !s->m_pUDT)
        return -1;

    CSndBuffer *b = s->m_pUDT->m_pSndBuffer;

    if (!b)
        return -1;

    int bytecount, timespan;
    int count = b->getCurrBufSize(Ref(bytecount), Ref(timespan));

    if (blocks)
        *blocks = count;

    if (bytes)
        *bytes = bytecount;

    return std::abs(timespan);
}

SRT_REJECT_REASON CUDT::rejectReason(SRTSOCKET u)
{
    CUDTSocket *s = s_UDTUnited.locate(u);
    if (!s || !s->m_pUDT)
        return SRT_REJ_UNKNOWN;

    return s->m_pUDT->m_RejectReason;
}

bool CUDT::runAcceptHook(CUDT *acore, const sockaddr *peer, const CHandShake *hs, const CPacket &hspkt)
{
    // Prepare the information for the hook.

    // We need streamid.
    char target[MAX_SID_LENGTH + 1];
    memset(target, 0, MAX_SID_LENGTH + 1);

    // Just for a case, check the length.
    // This wasn't done before, and we could risk memory crash.
    // In case of error, this will remain unset and the empty
    // string will be passed as streamid.

    int ext_flags = SrtHSRequest::SRT_HSTYPE_HSFLAGS::unwrap(hs->m_iType);

    // This tests if there are any extensions.
    if (hspkt.getLength() > CHandShake::m_iContentSize + 4 && IsSet(ext_flags, CHandShake::HS_EXT_CONFIG))
    {
        uint32_t *begin = reinterpret_cast<uint32_t *>(hspkt.m_pcData + CHandShake::m_iContentSize);
        size_t    size  = hspkt.getLength() - CHandShake::m_iContentSize; // Due to previous cond check we grant it's >0
        uint32_t *next  = 0;
        size_t    length   = size / sizeof(uint32_t);
        size_t    blocklen = 0;

        for (;;) // ONE SHOT, but continuable loop
        {
            int cmd = FindExtensionBlock(begin, length, Ref(blocklen), Ref(next));

            const size_t bytelen = blocklen * sizeof(uint32_t);

            if (cmd == SRT_CMD_SID)
            {
                if (!bytelen || bytelen > MAX_SID_LENGTH)
                {
                    LOGC(mglog.Error,
                         log << "interpretSrtHandshake: STREAMID length " << bytelen << " is 0 or > " << +MAX_SID_LENGTH
                             << " - PROTOCOL ERROR, REJECTING");
                    return false;
                }
                // See comment at CUDT::interpretSrtHandshake().
                memcpy(target, begin + 1, bytelen);

                // Un-swap on big endian machines
                ItoHLA((uint32_t *)target, (uint32_t *)target, blocklen);

                // Nothing more expected from connection block.
                break;
            }
            else if (cmd == SRT_CMD_NONE)
            {
                // End of blocks
                break;
            }
            else
            {
                // Any other kind of message extracted. Search on.
                length -= (next - begin);
                begin = next;
                if (begin)
                    continue;
            }

            break;
        }
    }

    try
    {
        int result = CALLBACK_CALL(m_cbAcceptHook, acore->m_SocketID, hs->m_iVersion, peer, target);
        if (result == -1)
            return false;
    }
    catch (...)
    {
        LOGP(mglog.Error, "runAcceptHook: hook interrupted by exception");
        return false;
    }

    return true;
}
