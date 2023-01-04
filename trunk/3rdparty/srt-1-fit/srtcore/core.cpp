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

#include "platform_sys.h"

// Linux specific
#ifdef SRT_ENABLE_BINDTODEVICE
#include <linux/if.h>
#endif

#include <cmath>
#include <sstream>
#include <algorithm>
#include <iterator>
#include "srt.h"
#include "queue.h"
#include "api.h"
#include "core.h"
#include "logging.h"
#include "crypto.h"
#include "logging_api.h" // Required due to containing extern srt_logger_config
#include "logger_defs.h"

#if !HAVE_CXX11
// for pthread_once
#include <pthread.h>
#endif

// Again, just in case when some "smart guy" provided such a global macro
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

using namespace std;
using namespace srt;
using namespace srt::sync;
using namespace srt_logging;

const SRTSOCKET UDT::INVALID_SOCK = srt::CUDT::INVALID_SOCK;
const int       UDT::ERROR        = srt::CUDT::ERROR;

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

extern const SRT_SOCKOPT srt_post_opt_list [SRT_SOCKOPT_NPOST] = {
    SRTO_SNDSYN,
    SRTO_RCVSYN,
    SRTO_LINGER,
    SRTO_SNDTIMEO,
    SRTO_RCVTIMEO,
    SRTO_MAXBW,
    SRTO_INPUTBW,
    SRTO_MININPUTBW,
    SRTO_OHEADBW,
    SRTO_SNDDROPDELAY,
    SRTO_DRIFTTRACER,
    SRTO_LOSSMAXTTL
};

const int32_t
    SRTO_R_PREBIND = BIT(0), //< cannot be modified after srt_bind()
    SRTO_R_PRE = BIT(1),     //< cannot be modified after connection is established
    SRTO_POST_SPEC = BIT(2); //< executes some action after setting the option


namespace srt
{

struct SrtOptionAction
{
    int flags[SRTO_E_SIZE];
    std::map<SRT_SOCKOPT, std::string> private_default;
    SrtOptionAction()
    {
        // Set everything to 0 to clear all flags
        // When an option isn't present here, it means that:
        // * it is not settable, or
        // * the option is POST (non-restricted)
        // * it has no post-actions
        // The post-action may be defined independently on restrictions.
        memset(flags, 0, sizeof flags);

        flags[SRTO_MSS]                = SRTO_R_PREBIND;
        flags[SRTO_FC]                 = SRTO_R_PRE;
        flags[SRTO_SNDBUF]             = SRTO_R_PREBIND;
        flags[SRTO_RCVBUF]             = SRTO_R_PREBIND;
        flags[SRTO_UDP_SNDBUF]         = SRTO_R_PREBIND;
        flags[SRTO_UDP_RCVBUF]         = SRTO_R_PREBIND;
        flags[SRTO_RENDEZVOUS]         = SRTO_R_PRE;
        flags[SRTO_REUSEADDR]          = SRTO_R_PREBIND;
        flags[SRTO_MAXBW]              = SRTO_POST_SPEC;
        flags[SRTO_SENDER]             = SRTO_R_PRE;
        flags[SRTO_TSBPDMODE]          = SRTO_R_PRE;
        flags[SRTO_LATENCY]            = SRTO_R_PRE;
        flags[SRTO_INPUTBW]            = SRTO_POST_SPEC;
        flags[SRTO_MININPUTBW]         = SRTO_POST_SPEC;
        flags[SRTO_OHEADBW]            = SRTO_POST_SPEC;
        flags[SRTO_PASSPHRASE]         = SRTO_R_PRE;
        flags[SRTO_PBKEYLEN]           = SRTO_R_PRE;
        flags[SRTO_IPTTL]              = SRTO_R_PREBIND;
        flags[SRTO_IPTOS]              = SRTO_R_PREBIND;
        flags[SRTO_TLPKTDROP]          = SRTO_R_PRE;
        flags[SRTO_SNDDROPDELAY]       = SRTO_POST_SPEC;
        flags[SRTO_NAKREPORT]          = SRTO_R_PRE;
        flags[SRTO_VERSION]            = SRTO_R_PRE;
        flags[SRTO_CONNTIMEO]          = SRTO_R_PRE;
        flags[SRTO_LOSSMAXTTL]         = SRTO_POST_SPEC;
        flags[SRTO_RCVLATENCY]         = SRTO_R_PRE;
        flags[SRTO_PEERLATENCY]        = SRTO_R_PRE;
        flags[SRTO_MINVERSION]         = SRTO_R_PRE;
        flags[SRTO_STREAMID]           = SRTO_R_PRE;
        flags[SRTO_CONGESTION]         = SRTO_R_PRE;
        flags[SRTO_MESSAGEAPI]         = SRTO_R_PRE;
        flags[SRTO_PAYLOADSIZE]        = SRTO_R_PRE;
        flags[SRTO_TRANSTYPE]          = SRTO_R_PREBIND;
        flags[SRTO_KMREFRESHRATE]      = SRTO_R_PRE;
        flags[SRTO_KMPREANNOUNCE]      = SRTO_R_PRE;
        flags[SRTO_ENFORCEDENCRYPTION] = SRTO_R_PRE;
        flags[SRTO_IPV6ONLY]           = SRTO_R_PREBIND;
        flags[SRTO_PEERIDLETIMEO]      = SRTO_R_PRE;
#ifdef SRT_ENABLE_BINDTODEVICE
        flags[SRTO_BINDTODEVICE]       = SRTO_R_PREBIND;
#endif
#if ENABLE_BONDING
        flags[SRTO_GROUPCONNECT]       = SRTO_R_PRE;
        flags[SRTO_GROUPMINSTABLETIMEO]= SRTO_R_PRE;
#endif
        flags[SRTO_PACKETFILTER]       = SRTO_R_PRE;
        flags[SRTO_RETRANSMITALGO]     = SRTO_R_PRE;

        // For "private" options (not derived from the listener
        // socket by an accepted socket) provide below private_default
        // to which these options will be reset after blindly
        // copying the option object from the listener socket.
        // Note that this option cannot have runtime-dependent
        // default value, like options affected by SRTO_TRANSTYPE.

        // Options may be of different types, but this value should be only
        // used as a source of the value. For example, in case of int64_t you'd
        // have to place here a string of 8 characters. It should be copied
        // always in the hardware order, as this is what will be directly
        // passed to a setting function.
        private_default[SRTO_STREAMID] = string();
    }
};

const SrtOptionAction s_sockopt_action;

} // namespace srt

#if HAVE_CXX11

CUDTUnited& srt::CUDT::uglobal()
{
    static CUDTUnited instance;
    return instance;
}

#else // !HAVE_CXX11

static pthread_once_t s_UDTUnitedOnce = PTHREAD_ONCE_INIT;

static CUDTUnited *getInstance()
{
    static CUDTUnited instance;
    return &instance;
}

CUDTUnited& srt::CUDT::uglobal()
{
    // We don't want lock each time, pthread_once can be faster than mutex.
    pthread_once(&s_UDTUnitedOnce, reinterpret_cast<void (*)()>(getInstance));
    return *getInstance();
}

#endif

void srt::CUDT::construct()
{
    m_pSndBuffer           = NULL;
    m_pRcvBuffer           = NULL;
    m_pSndLossList         = NULL;
    m_pRcvLossList         = NULL;
    m_iReorderTolerance    = 0;
    // How many times so far the packet considered lost has been received
    // before TTL expires.
    m_iConsecEarlyDelivery   = 0; 
    m_iConsecOrderedDelivery = 0;

    m_pSndQueue = NULL;
    m_pRcvQueue = NULL;
    m_pSNode    = NULL;
    m_pRNode    = NULL;

    // Will be reset to 0 for HSv5, this value is important for HSv4.
    m_iSndHsRetryCnt = SRT_MAX_HSRETRY + 1;

    m_PeerID              = 0;
    m_bOpened             = false;
    m_bListening          = false;
    m_bConnecting         = false;
    m_bConnected          = false;
    m_bClosing            = false;
    m_bShutdown           = false;
    m_bBroken             = false;
    m_bBreakAsUnstable    = false;
    // TODO: m_iBrokenCounter should be still set to some default.
    m_bPeerHealth         = true;
    m_RejectReason        = SRT_REJ_UNKNOWN;
    m_tsLastReqTime.store(steady_clock::time_point());
    m_SrtHsSide           = HSD_DRAW;
    m_uPeerSrtVersion     = 0;  // Not defined until connected.
    m_iTsbPdDelay_ms      = 0;
    m_iPeerTsbPdDelay_ms  = 0;
    m_bPeerTsbPd          = false;
    m_iPeerTsbPdDelay_ms  = 0;
    m_bTsbPd              = false;
    m_bTsbPdAckWakeup     = false;
    m_bGroupTsbPd         = false;
    m_bPeerTLPktDrop      = false;

    // Initilize mutex and condition variables.
    initSynch();

    // TODO: Uncomment when the callback is implemented.
    // m_cbPacketArrival.set(this, &CUDT::defaultPacketArrival);
}

srt::CUDT::CUDT(CUDTSocket* parent): m_parent(parent)
{
    construct();

    (void)SRT_DEF_VERSION;

    // Runtime fields
#if ENABLE_BONDING
    m_HSGroupType           = SRT_GTYPE_UNDEFINED;
#endif
    m_bTLPktDrop            = true; // Too-late Packet Drop

    m_pCache = NULL;
    // This is in order to set it ANY kind of initial value, however
    // this value should not be used when not connected and should be
    // updated in the handshake. When this value is 0, it means that
    // packets shall not be sent, as the other party doesn't have a
    // room to receive and store it. Therefore this value should be
    // overridden before any sending happens.
    m_iFlowWindowSize = 0;

}

srt::CUDT::CUDT(CUDTSocket* parent, const CUDT& ancestor): m_parent(parent)
{
    construct();

    // XXX Consider all below fields (except m_bReuseAddr) to be put
    // into a separate class for easier copying.

    m_config            = ancestor.m_config;
    // Reset values that shall not be derived to default ones.
    // These declarations should be consistent with SRTO_R_PRIVATE flag.
    for (size_t i = 0; i < Size(s_sockopt_action.flags); ++i)
    {
        const string* pdef = map_getp(s_sockopt_action.private_default, SRT_SOCKOPT(i));
        if (pdef)
        {
            try
            {
                // Ignore errors here - this is a development-time granted
                // value, not user-provided value.
                m_config.set(SRT_SOCKOPT(i), pdef->data(), (int) pdef->size());
            }
            catch (...)
            {
                LOGC(gglog.Error, log << "IPE: failed to set a declared default option!");
            }
        }
    }

    m_SrtHsSide         = ancestor.m_SrtHsSide; // actually it sets it to HSD_RESPONDER
    m_bTLPktDrop        = ancestor.m_bTLPktDrop;
    m_iReorderTolerance = m_config.iMaxReorderTolerance;  // Initialize with maximum value

    // Runtime
    m_pCache = ancestor.m_pCache;
}

srt::CUDT::~CUDT()
{
    // release mutex/condtion variables
    destroySynch();

    // destroy the data structures
    delete m_pSndBuffer;
    delete m_pRcvBuffer;
    delete m_pSndLossList;
    delete m_pRcvLossList;
    delete m_pSNode;
    delete m_pRNode;
}

void srt::CUDT::setOpt(SRT_SOCKOPT optName, const void* optval, int optlen)
{
    if (m_bBroken || m_bClosing)
        throw CUDTException(MJ_CONNECTION, MN_CONNLOST, 0);

    // Match check (confirm optName as index for s_sockopt_action)
    if (int(optName) < 0 || int(optName) >= int(SRTO_E_SIZE))
        throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);

    // Restriction check
    const int oflags = s_sockopt_action.flags[optName];

    ScopedLock cg (m_ConnectionLock);
    ScopedLock sendguard (m_SendLock);
    ScopedLock recvguard (m_RecvLock);

    HLOGC(aclog.Debug,
          log << CONID() << "OPTION: #" << optName << " value:" << FormatBinaryString((uint8_t*)optval, optlen));

    if (IsSet(oflags, SRTO_R_PREBIND) && m_bOpened)
        throw CUDTException(MJ_NOTSUP, MN_ISBOUND, 0);

    if (IsSet(oflags, SRTO_R_PRE) && (m_bConnected || m_bConnecting || m_bListening))
        throw CUDTException(MJ_NOTSUP, MN_ISCONNECTED, 0);

    // Option execution. If this returns -1, there's no such option.
    const int status = m_config.set(optName, optval, optlen);
    if (status == -1)
    {
        LOGC(aclog.Error, log << CONID() << "OPTION: #" << optName << " UNKNOWN");
        throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);
    }

    // Post-action, if applicable
    if (IsSet(oflags, SRTO_POST_SPEC) && m_bConnected)
    {
        switch (optName)
        {
        case SRTO_MAXBW:
            updateCC(TEV_INIT, EventVariant(TEV_INIT_RESET));
            break;

        case SRTO_INPUTBW:
        case SRTO_MININPUTBW:
            updateCC(TEV_INIT, EventVariant(TEV_INIT_INPUTBW));
            break;

        case SRTO_OHEADBW:
            updateCC(TEV_INIT, EventVariant(TEV_INIT_OHEADBW));
            break;

        case SRTO_LOSSMAXTTL:
            m_iReorderTolerance = m_config.iMaxReorderTolerance;

        default: break;
        }
    }
}

void srt::CUDT::getOpt(SRT_SOCKOPT optName, void *optval, int &optlen)
{
    ScopedLock cg(m_ConnectionLock);

    switch (optName)
    {
    case SRTO_MSS:
        *(int *)optval = m_config.iMSS;
        optlen         = sizeof(int);
        break;

    case SRTO_SNDSYN:
        *(bool *)optval = m_config.bSynSending;
        optlen          = sizeof(bool);
        break;

    case SRTO_RCVSYN:
        *(bool *)optval = m_config.bSynRecving;
        optlen          = sizeof(bool);
        break;

    case SRTO_ISN:
        *(int *)optval = m_iISN;
        optlen         = sizeof(int);
        break;

    case SRTO_FC:
        *(int *)optval = m_config.iFlightFlagSize;
        optlen         = sizeof(int);
        break;

    case SRTO_SNDBUF:
        *(int *)optval = m_config.iSndBufSize * (m_config.iMSS - CPacket::UDP_HDR_SIZE);
        optlen         = sizeof(int);
        break;

    case SRTO_RCVBUF:
        *(int *)optval = m_config.iRcvBufSize * (m_config.iMSS - CPacket::UDP_HDR_SIZE);
        optlen         = sizeof(int);
        break;

    case SRTO_LINGER:
        if (optlen < (int)(sizeof(linger)))
            throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);

        *(linger *)optval = m_config.Linger;
        optlen            = sizeof(linger);
        break;

    case SRTO_UDP_SNDBUF:
        *(int *)optval = m_config.iUDPSndBufSize;
        optlen         = sizeof(int);
        break;

    case SRTO_UDP_RCVBUF:
        *(int *)optval = m_config.iUDPRcvBufSize;
        optlen         = sizeof(int);
        break;

    case SRTO_RENDEZVOUS:
        *(bool *)optval = m_config.bRendezvous;
        optlen          = sizeof(bool);
        break;

    case SRTO_SNDTIMEO:
        *(int *)optval = m_config.iSndTimeOut;
        optlen         = sizeof(int);
        break;

    case SRTO_RCVTIMEO:
        *(int *)optval = m_config.iRcvTimeOut;
        optlen         = sizeof(int);
        break;

    case SRTO_REUSEADDR:
        *(bool *)optval = m_config.bReuseAddr;
        optlen          = sizeof(bool);
        break;

    case SRTO_MAXBW:
        if (size_t(optlen) < sizeof(m_config.llMaxBW))
            throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);
        *(int64_t *)optval = m_config.llMaxBW;
        optlen             = sizeof(int64_t);
        break;

    case SRTO_INPUTBW:
        if (size_t(optlen) < sizeof(m_config.llInputBW))
            throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);
       *(int64_t*)optval = m_config.llInputBW;
       optlen            = sizeof(int64_t);
       break;

    case SRTO_MININPUTBW:
        if (size_t(optlen) < sizeof (m_config.llMinInputBW))
            throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);
        *(int64_t*)optval = m_config.llMinInputBW;
        optlen            = sizeof(int64_t);
        break;

    case SRTO_OHEADBW:
        *(int32_t *)optval = m_config.iOverheadBW;
        optlen = sizeof(int32_t);
        break;

    case SRTO_STATE:
        *(int32_t *)optval = uglobal().getStatus(m_SocketID);
        optlen             = sizeof(int32_t);
        break;

    case SRTO_EVENT:
    {
        int32_t event = 0;
        if (m_bBroken)
            event |= SRT_EPOLL_ERR;
        else
        {
            enterCS(m_RecvLock);
            if (m_pRcvBuffer && isRcvBufferReady())
                event |= SRT_EPOLL_IN;
            leaveCS(m_RecvLock);
            if (m_pSndBuffer && (m_config.iSndBufSize > m_pSndBuffer->getCurrBufSize()))
                event |= SRT_EPOLL_OUT;
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
            enterCS(m_RecvLock);
            *(int32_t *)optval = m_pRcvBuffer->getRcvDataSize();
            leaveCS(m_RecvLock);
        }
        else
            *(int32_t *)optval = 0;
        optlen = sizeof(int32_t);
        break;

    case SRTO_IPTTL:
        if (m_bOpened)
            *(int32_t *)optval = m_pSndQueue->getIpTTL();
        else
            *(int32_t *)optval = m_config.iIpTTL;
        optlen = sizeof(int32_t);
        break;

    case SRTO_IPTOS:
        if (m_bOpened)
            *(int32_t *)optval = m_pSndQueue->getIpToS();
        else
            *(int32_t *)optval = m_config.iIpToS;
        optlen = sizeof(int32_t);
        break;

    case SRTO_BINDTODEVICE:
#ifdef SRT_ENABLE_BINDTODEVICE
        if (optlen < IFNAMSIZ)
            throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);

        if (m_bOpened && m_pSndQueue->getBind(((char*)optval), optlen))
        {
            optlen = strlen((char*)optval);
            break;
        }

        // Fallback: return from internal data
        strcpy(((char*)optval), m_config.sBindToDevice.c_str());
        optlen = m_config.sBindToDevice.size();
#else
        LOGC(smlog.Error, log << "SRTO_BINDTODEVICE is not supported on that platform");
        throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);
#endif
        break;

    case SRTO_SENDER:
        *(bool *)optval = m_config.bDataSender;
        optlen             = sizeof(bool);
        break;

    case SRTO_TSBPDMODE:
        *(bool *)optval = m_config.bTSBPD;
        optlen             = sizeof(bool);
        break;

    case SRTO_LATENCY:
    case SRTO_RCVLATENCY:
        if (m_bConnected)
            *(int32_t *)optval = m_iTsbPdDelay_ms;
        else
            *(int32_t *)optval = m_config.iRcvLatency;
        optlen             = sizeof(int32_t);
        break;

    case SRTO_PEERLATENCY:
        if (m_bConnected)
            *(int32_t *)optval = m_iPeerTsbPdDelay_ms;
        else
            *(int32_t *)optval = m_config.iPeerLatency;

        optlen             = sizeof(int32_t);
        break;

    case SRTO_TLPKTDROP:
        if (m_bConnected)
            *(bool *)optval = m_bTLPktDrop;
        else
            *(bool *)optval = m_config.bTLPktDrop;

        optlen          = sizeof(bool);
        break;

    case SRTO_SNDDROPDELAY:
        *(int32_t *)optval = m_config.iSndDropDelay;
        optlen             = sizeof(int32_t);
        break;

    case SRTO_PBKEYLEN:
        if (m_pCryptoControl)
            *(int32_t *)optval = (int32_t) m_pCryptoControl->KeyLen(); // Running Key length.
        else
            *(int32_t *)optval = m_config.iSndCryptoKeyLen; // May be 0.
        optlen = sizeof(int32_t);
        break;

    case SRTO_KMSTATE:
        if (!m_pCryptoControl)
            *(int32_t *)optval = SRT_KM_S_UNSECURED;
        else if (m_config.bDataSender)
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
        *(int32_t*)optval = m_config.iMaxReorderTolerance;
        optlen = sizeof(int32_t);
        break;

    case SRTO_NAKREPORT:
        *(bool *)optval = m_config.bRcvNakReport;
        optlen          = sizeof(bool);
        break;

    case SRTO_VERSION:
        *(int32_t *)optval = m_config.uSrtVersion;
        optlen             = sizeof(int32_t);
        break;

    case SRTO_PEERVERSION:
        *(int32_t *)optval = m_uPeerSrtVersion;
        optlen             = sizeof(int32_t);
        break;

    case SRTO_CONNTIMEO:
        *(int*)optval = (int) count_milliseconds(m_config.tdConnTimeOut);
        optlen        = sizeof(int);
        break;

    case SRTO_DRIFTTRACER:
        *(bool*)optval = m_config.bDriftTracer;
        optlen         = sizeof(bool);
        break;

    case SRTO_MINVERSION:
        *(uint32_t *)optval = m_config.uMinimumPeerSrtVersion;
        optlen              = sizeof(uint32_t);
        break;

    case SRTO_STREAMID:
        if (size_t(optlen) < m_config.sStreamName.size() + 1)
            throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);

        strcpy((char *)optval, m_config.sStreamName.c_str());
        optlen = (int) m_config.sStreamName.size();
        break;

    case SRTO_CONGESTION:
        if (size_t(optlen) < m_config.sCongestion.size() + 1)
            throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);

        strcpy((char *)optval, m_config.sCongestion.c_str());
        optlen = (int) m_config.sCongestion.size();
        break;

    case SRTO_MESSAGEAPI:
        optlen          = sizeof(bool);
        *(bool *)optval = m_config.bMessageAPI;
        break;

    case SRTO_PAYLOADSIZE:
        optlen         = sizeof(int);
        *(int *)optval = (int) m_config.zExpPayloadSize;
        break;

    case SRTO_KMREFRESHRATE:
        optlen = sizeof(int);
        *(int*)optval = (int)m_config.uKmRefreshRatePkt;
        break;

    case SRTO_KMPREANNOUNCE:
        optlen = sizeof(int);
        *(int*)optval = (int)m_config.uKmPreAnnouncePkt;
        break;

#if ENABLE_BONDING
    case SRTO_GROUPCONNECT:
        optlen        = sizeof (int);
        *(int*)optval = m_config.iGroupConnect;
        break;

    case SRTO_GROUPMINSTABLETIMEO:
        optlen = sizeof(int);
        *(int*)optval = (int)m_config.uMinStabilityTimeout_ms;
        break;

    case SRTO_GROUPTYPE:
        optlen         = sizeof (int);
        *(int*)optval = m_HSGroupType;
        break;
#endif

    case SRTO_ENFORCEDENCRYPTION:
        optlen          = sizeof(bool);
        *(bool *)optval = m_config.bEnforcedEnc;
        break;

    case SRTO_IPV6ONLY:
        optlen         = sizeof(int);
        *(int *)optval = m_config.iIpV6Only;
        break;

    case SRTO_PEERIDLETIMEO:
        *(int *)optval = m_config.iPeerIdleTimeout_ms;
        optlen         = sizeof(int);
        break;

    case SRTO_PACKETFILTER:
        if (size_t(optlen) < m_config.sPacketFilterConfig.size() + 1)
            throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);

        strcpy((char *)optval, m_config.sPacketFilterConfig.c_str());
        optlen = (int) m_config.sPacketFilterConfig.size();
        break;

    case SRTO_RETRANSMITALGO:
        *(int32_t *)optval = m_config.iRetransmitAlgo;
        optlen         = sizeof(int32_t);
        break;

    default:
        throw CUDTException(MJ_NOTSUP, MN_NONE, 0);
    }
}


#if ENABLE_BONDING
SRT_ERRNO srt::CUDT::applyMemberConfigObject(const SRT_SocketOptionObject& opt)
{
    SRT_SOCKOPT this_opt = SRTO_VERSION;
    for (size_t i = 0; i < opt.options.size(); ++i)
    {
        SRT_SocketOptionObject::SingleOption* o = opt.options[i];
        HLOGC(smlog.Debug, log << "applyMemberConfigObject: OPTION @" << m_SocketID << " #" << o->option);
        this_opt = SRT_SOCKOPT(o->option);
        setOpt(this_opt, o->storage, o->length);
    }
    return SRT_SUCCESS;
}
#endif

bool srt::CUDT::setstreamid(SRTSOCKET u, const std::string &sid)
{
    CUDT *that = getUDTHandle(u);
    if (!that)
        return false;

    if (sid.size() > CSrtConfig::MAX_SID_LENGTH)
        return false;

    if (that->m_bConnected)
        return false;

    that->m_config.sStreamName.set(sid);
    return true;
}

string srt::CUDT::getstreamid(SRTSOCKET u)
{
    CUDT *that = getUDTHandle(u);
    if (!that)
        return "";

    return that->m_config.sStreamName.str();
}

// XXX REFACTOR: Make common code for CUDT constructor and clearData,
// possibly using CUDT::construct.
void srt::CUDT::clearData()
{
    // Initial sequence number, loss, acknowledgement, etc.
    int udpsize = m_config.iMSS - CPacket::UDP_HDR_SIZE;

    m_iMaxSRTPayloadSize = udpsize - CPacket::HDR_SIZE;

    HLOGC(cnlog.Debug, log << "clearData: PAYLOAD SIZE: " << m_iMaxSRTPayloadSize);

    m_iEXPCount  = 1;
    m_iBandwidth = 1; // pkts/sec
    // XXX use some constant for this 16
    m_iDeliveryRate     = 16;
    m_iByteDeliveryRate = 16 * m_iMaxSRTPayloadSize;
    m_iAckSeqNo         = 0;
    m_tsLastAckTime     = steady_clock::now();

    // trace information
    {
        ScopedLock stat_lock(m_StatsLock);

        m_stats.tsStartTime = steady_clock::now();
        m_stats.sndr.reset();
        m_stats.rcvr.reset();

        m_stats.tsLastSampleTime = steady_clock::now();
        m_stats.traceReorderDistance = 0;
        m_stats.sndDuration = m_stats.m_sndDurationTotal = 0;
    }

    // Resetting these data because this happens when agent isn't connected.
    m_bPeerTsbPd         = false;
    m_iPeerTsbPdDelay_ms = 0;

    // TSBPD as state should be set to FALSE here.
    // Only when the HSREQ handshake is exchanged,
    // should they be set to possibly true.
    m_bTsbPd         = false;
    m_bGroupTsbPd    = false;
    m_iTsbPdDelay_ms = m_config.iRcvLatency;
    m_bTLPktDrop     = m_config.bTLPktDrop;
    m_bPeerTLPktDrop = false;

    m_bPeerNakReport = false;

    m_bPeerRexmitFlag = false;

    m_RdvState           = CHandShake::RDV_INVALID;
    m_tsRcvPeerStartTime = steady_clock::time_point();
}

void srt::CUDT::open()
{
    ScopedLock cg(m_ConnectionLock);

    clearData();

    // structures for queue
    if (m_pSNode == NULL)
        m_pSNode = new CSNode;
    m_pSNode->m_pUDT      = this;
    m_pSNode->m_tsTimeStamp = steady_clock::now();
    m_pSNode->m_iHeapLoc  = -1;

    if (m_pRNode == NULL)
        m_pRNode = new CRNode;
    m_pRNode->m_pUDT      = this;
    m_pRNode->m_tsTimeStamp = steady_clock::now();
    m_pRNode->m_pPrev = m_pRNode->m_pNext = NULL;
    m_pRNode->m_bOnList                   = false;

    // Set initial values of smoothed RTT and RTT variance.
    m_iSRTT               = INITIAL_RTT;
    m_iRTTVar             = INITIAL_RTTVAR;
    m_bIsFirstRTTReceived = false;

    // set minimum NAK and EXP timeout to 300ms
    m_tdMinNakInterval = milliseconds_from(300);
    m_tdMinExpInterval = milliseconds_from(300);

    m_tdACKInterval = microseconds_from(COMM_SYN_INTERVAL_US);
    m_tdNAKInterval = m_tdMinNakInterval;

    const steady_clock::time_point currtime = steady_clock::now();
    m_tsLastRspTime.store(currtime);
    m_tsNextACKTime.store(currtime + m_tdACKInterval);
    m_tsNextNAKTime.store(currtime + m_tdNAKInterval);
    m_tsLastRspAckTime = currtime;
    m_tsLastSndTime.store(currtime);

    m_tsUnstableSince   = steady_clock::time_point();
    m_tsFreshActivation = steady_clock::time_point();
    m_tsWarySince       = steady_clock::time_point();

    m_iReXmitCount   = 1;
    m_iPktCount      = 0;
    m_iLightACKCount = 1;
    m_tsNextSendTime = steady_clock::time_point();
    m_tdSendTimeDiff = microseconds_from(0);

    // Now UDT is opened.
    m_bOpened = true;
}

void srt::CUDT::setListenState()
{
    ScopedLock cg(m_ConnectionLock);

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

size_t srt::CUDT::fillSrtHandshake(uint32_t *aw_srtdata, size_t srtlen, int msgtype, int hs_version)
{
    if (srtlen < SRT_HS_E_SIZE)
    {
        LOGC(cnlog.Fatal,
             log << "IPE: fillSrtHandshake: buffer too small: " << srtlen << " (expected: " << SRT_HS_E_SIZE << ")");
        return 0;
    }

    srtlen = SRT_HS_E_SIZE; // We use only that much space.

    memset((aw_srtdata), 0, sizeof(uint32_t) * srtlen);
    /* Current version (1.x.x) SRT handshake */
    aw_srtdata[SRT_HS_VERSION] = m_config.uSrtVersion; /* Required version */
    aw_srtdata[SRT_HS_FLAGS] |= SrtVersionCapabilities();

    switch (msgtype)
    {
    case SRT_CMD_HSREQ:
        return fillSrtHandshake_HSREQ((aw_srtdata), srtlen, hs_version);
    case SRT_CMD_HSRSP:
        return fillSrtHandshake_HSRSP((aw_srtdata), srtlen, hs_version);
    default:
        LOGC(cnlog.Fatal, log << "IPE: fillSrtHandshake/sendSrtMsg called with value " << msgtype);
        return 0;
    }
}

size_t srt::CUDT::fillSrtHandshake_HSREQ(uint32_t *aw_srtdata, size_t /* srtlen - unused */, int hs_version)
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
    if (m_config.bTSBPD)
    {
        m_iTsbPdDelay_ms     = m_config.iRcvLatency;
        m_iPeerTsbPdDelay_ms = m_config.iPeerLatency;
        /*
         * Sent data is real-time, use Time-based Packet Delivery,
         * set option bit and configured delay
         */
        aw_srtdata[SRT_HS_FLAGS] |= SRT_OPT_TSBPDSND;

        if (hs_version < CUDT::HS_VERSION_SRT1)
        {
            // HSv4 - this uses only one value.
            aw_srtdata[SRT_HS_LATENCY] = SRT_HS_LATENCY_LEG::wrap(m_iPeerTsbPdDelay_ms);
        }
        else
        {
            // HSv5 - this will be understood only since this version when this exists.
            aw_srtdata[SRT_HS_LATENCY] = SRT_HS_LATENCY_SND::wrap(m_iPeerTsbPdDelay_ms);

            // And in the reverse direction.
            aw_srtdata[SRT_HS_FLAGS] |= SRT_OPT_TSBPDRCV;
            aw_srtdata[SRT_HS_LATENCY] |= SRT_HS_LATENCY_RCV::wrap(m_iTsbPdDelay_ms);

            // This wasn't there for HSv4, this setting is only for the receiver.
            // HSv5 is bidirectional, so every party is a receiver.

            if (m_bTLPktDrop)
                aw_srtdata[SRT_HS_FLAGS] |= SRT_OPT_TLPKTDROP;
        }
    }

    if (m_config.bRcvNakReport)
        aw_srtdata[SRT_HS_FLAGS] |= SRT_OPT_NAKREPORT;

    // I support SRT_OPT_REXMITFLG. Do you?
    aw_srtdata[SRT_HS_FLAGS] |= SRT_OPT_REXMITFLG;

    // Declare the API used. The flag is set for "stream" API because
    // the older versions will never set this flag, but all old SRT versions use message API.
    if (!m_config.bMessageAPI)
        aw_srtdata[SRT_HS_FLAGS] |= SRT_OPT_STREAM;

    HLOGC(cnlog.Debug,
          log << "HSREQ/snd: LATENCY[SND:" << SRT_HS_LATENCY_SND::unwrap(aw_srtdata[SRT_HS_LATENCY])
              << " RCV:" << SRT_HS_LATENCY_RCV::unwrap(aw_srtdata[SRT_HS_LATENCY]) << "] FLAGS["
              << SrtFlagString(aw_srtdata[SRT_HS_FLAGS]) << "]");

    return 3;
}

size_t srt::CUDT::fillSrtHandshake_HSRSP(uint32_t *aw_srtdata, size_t /* srtlen - unused */, int hs_version)
{
    // Setting m_tsRcvPeerStartTime is done in processSrtMsg_HSREQ(), so
    // this condition will be skipped only if this function is called without
    // getting first received HSREQ. Doesn't look possible in both HSv4 and HSv5.
    if (is_zero(m_tsRcvPeerStartTime))
    {
        LOGC(cnlog.Fatal, log << "IPE: fillSrtHandshake_HSRSP: m_tsRcvPeerStartTime NOT SET!");
        return 0;
    }

    // If Agent doesn't set TSBPD, it will not set the TSBPD flag back to the Peer.
    // The peer doesn't have be disturbed by it anyway.
    if (isOPT_TsbPd())
    {
        /*
         * We got and transposed peer start time (HandShake request timestamp),
         * we can support Timestamp-based Packet Delivery
         */
        aw_srtdata[SRT_HS_FLAGS] |= SRT_OPT_TSBPDRCV;

        if (hs_version < HS_VERSION_SRT1)
        {
            // HSv4 - this uses only one value
            aw_srtdata[SRT_HS_LATENCY] = SRT_HS_LATENCY_LEG::wrap(m_iTsbPdDelay_ms);
        }
        else
        {
            // HSv5 - this puts "agent's" latency into RCV field and "peer's" -
            // into SND field.
            aw_srtdata[SRT_HS_LATENCY] = SRT_HS_LATENCY_RCV::wrap(m_iTsbPdDelay_ms);
        }
    }
    else
    {
        HLOGC(cnlog.Debug, log << "HSRSP/snd: TSBPD off, NOT responding TSBPDRCV flag.");
    }

    // Hsv5, only when peer has declared TSBPD mode.
    // The flag was already set, and the value already "maximized" in processSrtMsg_HSREQ().
    if (m_bPeerTsbPd && hs_version >= HS_VERSION_SRT1)
    {
        // HSv5 is bidirectional - so send the TSBPDSND flag, and place also the
        // peer's latency into SND field.
        aw_srtdata[SRT_HS_FLAGS] |= SRT_OPT_TSBPDSND;
        aw_srtdata[SRT_HS_LATENCY] |= SRT_HS_LATENCY_SND::wrap(m_iPeerTsbPdDelay_ms);

        HLOGC(cnlog.Debug,
              log << "HSRSP/snd: HSv5 peer uses TSBPD, responding TSBPDSND latency=" << m_iPeerTsbPdDelay_ms);
    }
    else
    {
        HLOGC(cnlog.Debug,
              log << "HSRSP/snd: HSv" << (hs_version == CUDT::HS_VERSION_UDT4 ? 4 : 5)
                  << " with peer TSBPD=" << (m_bPeerTsbPd ? "on" : "off") << " - NOT responding TSBPDSND");
    }

    if (m_bTLPktDrop)
        aw_srtdata[SRT_HS_FLAGS] |= SRT_OPT_TLPKTDROP;

    if (m_config.bRcvNakReport)
    {
        // HSv5: Note that this setting is independent on the value of
        // m_bPeerNakReport, which represent this setting in the peer.

        aw_srtdata[SRT_HS_FLAGS] |= SRT_OPT_NAKREPORT;
        /*
         * NAK Report is so efficient at controlling bandwidth that sender TLPktDrop
         * is not needed. SRT 1.0.5 to 1.0.7 sender TLPktDrop combined with SRT 1.0
         * Timestamp-Based Packet Delivery was not well implemented and could drop
         * big I-Frame tail before sending once on low latency setups.
         * Disabling TLPktDrop in the receiver SRT Handshake Reply prevents the sender
         * from enabling Too-Late Packet Drop.
         */
        if (m_uPeerSrtVersion <= SrtVersion(1, 0, 7))
            aw_srtdata[SRT_HS_FLAGS] &= ~SRT_OPT_TLPKTDROP;
    }

    if (m_config.uSrtVersion >= SrtVersion(1, 2, 0))
    {
        if (!m_bPeerRexmitFlag)
        {
            // Peer does not request to use rexmit flag, if so,
            // we won't use as well.
            HLOGC(cnlog.Debug, log << "HSRSP/snd: AGENT understands REXMIT flag, but PEER DOES NOT. NOT setting.");
        }
        else
        {
            // Request that the rexmit bit be used as a part of msgno.
            aw_srtdata[SRT_HS_FLAGS] |= SRT_OPT_REXMITFLG;
            HLOGF(cnlog.Debug, "HSRSP/snd: AGENT UNDERSTANDS REXMIT flag and PEER reported that it does, too.");
        }
    }
    else
    {
        // Since this is now in the code, it can occur only in case when you change the
        // version specification in the build configuration.
        HLOGF(cnlog.Debug, "HSRSP/snd: AGENT DOES NOT UNDERSTAND REXMIT flag");
    }

    HLOGC(cnlog.Debug,
          log << "HSRSP/snd: LATENCY[SND:" << SRT_HS_LATENCY_SND::unwrap(aw_srtdata[SRT_HS_LATENCY])
              << " RCV:" << SRT_HS_LATENCY_RCV::unwrap(aw_srtdata[SRT_HS_LATENCY]) << "] FLAGS["
              << SrtFlagString(aw_srtdata[SRT_HS_FLAGS]) << "]");

    return 3;
}

size_t srt::CUDT::prepareSrtHsMsg(int cmd, uint32_t *srtdata, size_t size)
{
    size_t srtlen = fillSrtHandshake(srtdata, size, cmd, handshakeVersion());
    HLOGF(cnlog.Debug,
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

void srt::CUDT::sendSrtMsg(int cmd, uint32_t *srtdata_in, size_t srtlen_in)
{
    CPacket srtpkt;
    int32_t srtcmd = (int32_t)cmd;

    SRT_STATIC_ASSERT(SRTDATA_MAXSIZE >= SRT_HS_E_SIZE, "SRT_CMD_MAXSZ is too small to hold all the data");
    // This will be effectively larger than SRT_HS_E_SIZE, but it will be also used for incoming data.
    uint32_t srtdata[SRTDATA_MAXSIZE];

    size_t srtlen = 0;

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
        srtlen = prepareSrtHsMsg(cmd, srtdata, SRTDATA_MAXSIZE);
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
        LOGF(cnlog.Error, "sndSrtMsg: IPE: cmd=%d unsupported", cmd);
        break;
    }

    if (srtlen > 0)
    {
        /* srtpkt.pack will set message data in network order */
        srtpkt.pack(UMSG_EXT, &srtcmd, srtdata, srtlen * sizeof(int32_t));
        addressAndSend(srtpkt);
    }
}

size_t srt::CUDT::fillHsExtConfigString(uint32_t* pcmdspec, int cmd, const string& str)
{
    uint32_t* space = pcmdspec + 1;
    size_t wordsize         = (str.size() + 3) / 4;
    size_t aligned_bytesize = wordsize * 4;

    memset((space), 0, aligned_bytesize);
    memcpy((space), str.data(), str.size());
    // Preswap to little endian (in place due to possible padding zeros)
    HtoILA((space), space, wordsize);

    *pcmdspec = HS_CMDSPEC_CMD::wrap(cmd) | HS_CMDSPEC_SIZE::wrap((uint32_t) wordsize);

    return wordsize;
}

#if ENABLE_BONDING
// [[using locked(m_parent->m_ControlLock)]]
// [[using locked(s_UDTUnited.m_GlobControlLock)]]
size_t srt::CUDT::fillHsExtGroup(uint32_t* pcmdspec)
{
    SRT_ASSERT(m_parent->m_GroupOf != NULL);
    uint32_t* space = pcmdspec + 1;

    SRTSOCKET id = m_parent->m_GroupOf->id();
    SRT_GROUP_TYPE tp = m_parent->m_GroupOf->type();
    uint32_t flags = 0;

    // Note: if agent is a listener, and the current version supports
    // both sync methods, this flag might have been changed according to
    // the wish of the caller.
    if (m_parent->m_GroupOf->synconmsgno())
        flags |= SRT_GFLAG_SYNCONMSG;

    // NOTE: this code remains as is for historical reasons.
    // The initial implementation stated that the peer id be
    // extracted so that it can be reported and possibly the
    // start time somehow encoded and written into the group
    // extension, but it was later seen not necessary. Therefore
    // this code remains, but now it's informational only.
#if ENABLE_HEAVY_LOGGING
    m_parent->m_GroupOf->debugMasterData(m_SocketID);
#endif

    // See CUDT::interpretGroup()

    uint32_t dataword = 0
        | SrtHSRequest::HS_GROUP_TYPE::wrap(tp)
        | SrtHSRequest::HS_GROUP_FLAGS::wrap(flags)
        | SrtHSRequest::HS_GROUP_WEIGHT::wrap(m_parent->m_GroupMemberData->weight);

    const uint32_t storedata [GRPD_E_SIZE] = { uint32_t(id), dataword };
    memcpy((space), storedata, sizeof storedata);

    const size_t ra_size = Size(storedata);
    *pcmdspec = HS_CMDSPEC_CMD::wrap(SRT_CMD_GROUP) | HS_CMDSPEC_SIZE::wrap(ra_size);

    return ra_size;
}
#endif

size_t srt::CUDT::fillHsExtKMREQ(uint32_t* pcmdspec, size_t ki)
{
    uint32_t* space = pcmdspec + 1;

    size_t msglen = m_pCryptoControl->getKmMsg_size(ki);
    // Make ra_size back in element unit
    // Add one extra word if the size isn't aligned to 32-bit.
    size_t ra_size = (msglen / sizeof(uint32_t)) + (msglen % sizeof(uint32_t) ? 1 : 0);

    // Store the CMD + SIZE in the next field
    *pcmdspec = HS_CMDSPEC_CMD::wrap(SRT_CMD_KMREQ) | HS_CMDSPEC_SIZE::wrap((uint32_t) ra_size);

    // Copy the key - do the endian inversion because another endian inversion
    // will be done for every control message before sending, and this KM message
    // is ALREADY in network order.
    const uint32_t* keydata = reinterpret_cast<const uint32_t*>(m_pCryptoControl->getKmMsg_data(ki));

    HLOGC(cnlog.Debug,
            log << "createSrtHandshake: KMREQ: adding key #" << ki << " length=" << ra_size
            << " words (KmMsg_size=" << msglen << ")");
    // XXX INSECURE ": [" << FormatBinaryString((uint8_t*)keydata, msglen) << "]";

    // Yes, I know HtoNLA and NtoHLA do exactly the same operation, but I want
    // to be clear about the true intention.
    NtoHLA((space), keydata, ra_size);

    return ra_size;
}

size_t srt::CUDT::fillHsExtKMRSP(uint32_t* pcmdspec, const uint32_t* kmdata, size_t kmdata_wordsize)
{
    uint32_t* space = pcmdspec + 1;
    const uint32_t failure_kmrsp[] = {SRT_KM_S_UNSECURED};
    const uint32_t* keydata = 0;

    // Shift the starting point with the value of previously added block,
    // to start with the new one.

    size_t ra_size;

    if (kmdata_wordsize == 0)
    {
        LOGC(cnlog.Warn, log << "createSrtHandshake: Agent has PW, but Peer sent no KMREQ. Sending error KMRSP response");
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
            LOGC(cnlog.Fatal, log << "createSrtHandshake: IPE: srtkm_cmd=SRT_CMD_KMRSP and no kmdata!");
            return 0;
        }
        ra_size = kmdata_wordsize;
        keydata = reinterpret_cast<const uint32_t *>(kmdata);
    }

    *pcmdspec = HS_CMDSPEC_CMD::wrap(SRT_CMD_KMRSP) | HS_CMDSPEC_SIZE::wrap((uint32_t) ra_size);
    HLOGC(cnlog.Debug,
            log << "createSrtHandshake: KMRSP: applying returned key length="
            << ra_size); // XXX INSECURE << " words: [" << FormatBinaryString((uint8_t*)kmdata,
            // kmdata_wordsize*sizeof(uint32_t)) << "]";

    NtoHLA((space), keydata, ra_size);
    return ra_size;
}


// PREREQUISITE:
// pkt must be set the buffer and configured for UMSG_HANDSHAKE.
// Note that this function replaces also serialization for the HSv4.
bool srt::CUDT::createSrtHandshake(
        int             srths_cmd,
        int             srtkm_cmd,
        const uint32_t* kmdata,
        size_t          kmdata_wordsize, // IN WORDS, NOT BYTES!!!
        CPacket&        w_pkt,
        CHandShake&     w_hs)
{
    // This function might be called before the opposite version was recognized.
    // Check if the version is exactly 4 because this means that the peer has already
    // sent something - asynchronously, and usually in rendezvous - and we already know
    // that the peer is version 4. In this case, agent must behave as HSv4, til the end.
    if (m_ConnRes.m_iVersion == HS_VERSION_UDT4)
    {
        w_hs.m_iVersion = HS_VERSION_UDT4;
        w_hs.m_iType    = UDT_DGRAM;
        if (w_hs.m_extension)
        {
            // Should be impossible
            LOGC(cnlog.Error, log << "createSrtHandshake: IPE: EXTENSION SET WHEN peer reports version 4 - fixing...");
            w_hs.m_extension = false;
        }
    }
    else
    {
        w_hs.m_iType = 0; // Prepare it for flags
    }

    HLOGC(cnlog.Debug,
          log << "createSrtHandshake: buf size=" << w_pkt.getLength() << " hsx=" << MessageTypeStr(UMSG_EXT, srths_cmd)
              << " kmx=" << MessageTypeStr(UMSG_EXT, srtkm_cmd) << " kmdata_wordsize=" << kmdata_wordsize
              << " version=" << w_hs.m_iVersion);

    // Once you are certain that the version is HSv5, set the enc type flags
    // to advertise pbkeylen. Otherwise make sure that the old interpretation
    // will correctly pick up the type field. PBKEYLEN should be advertized
    // regardless of what URQ stage the handshake is (note that in case of rendezvous
    // CONCLUSION might be the FIRST MESSAGE EVER RECEIVED by a party).
    if (w_hs.m_iVersion > HS_VERSION_UDT4)
    {
        // Check if there was a failure to receie HSREQ before trying to craft HSRSP.
        // If fillSrtHandshake_HSRSP catches the condition of m_tsRcvPeerStartTime == steady_clock::zero(),
        // it will return size 0, which will mess up with further extension procedures;
        // PREVENT THIS HERE.
        if (w_hs.m_iReqType == URQ_CONCLUSION && srths_cmd == SRT_CMD_HSRSP && is_zero(m_tsRcvPeerStartTime))
        {
            LOGC(cnlog.Error,
                 log << "createSrtHandshake: IPE (non-fatal): Attempting to craft HSRSP without received HSREQ. "
                        "BLOCKING extensions.");
            w_hs.m_extension = false;
        }

        // The situation when this function is called without requested extensions
        // is URQ_CONCLUSION in rendezvous mode in some of the transitions.
        // In this case for version 5 just clear the m_iType field, as it has
        // different meaning in HSv5 and contains extension flags.
        //
        // Keep 0 in the SRT_HSTYPE_HSFLAGS field, but still advertise PBKEYLEN
        // in the SRT_HSTYPE_ENCFLAGS field.
        w_hs.m_iType                  = SrtHSRequest::wrapFlags(false /*no magic in HSFLAGS*/, m_config.iSndCryptoKeyLen);

        IF_HEAVY_LOGGING(bool whether = m_config.iSndCryptoKeyLen != 0);
        HLOGC(cnlog.Debug,
              log << "createSrtHandshake: " << (whether ? "" : "NOT ")
                  << " Advertising PBKEYLEN - value = " << m_config.iSndCryptoKeyLen);

        // Note: This is required only when sending a HS message without SRT extensions.
        // When this is to be sent with SRT extensions, then KMREQ will be attached here
        // and the PBKEYLEN will be extracted from it. If this is going to attach KMRSP
        // here, it's already too late (it should've been advertised before getting the first
        // handshake message with KMREQ).
    }
    else
    {
        w_hs.m_iType = UDT_DGRAM;
    }

    // values > URQ_CONCLUSION include also error types
    // if (w_hs.m_iVersion == HS_VERSION_UDT4 || w_hs.m_iReqType > URQ_CONCLUSION) <--- This condition was checked b4 and
    // it's only valid for caller-listener mode
    if (!w_hs.m_extension)
    {
        // Serialize only the basic handshake, if this is predicted for
        // Hsv4 peer or this is URQ_INDUCTION or URQ_WAVEAHAND.
        size_t hs_size = w_pkt.getLength();
        w_hs.store_to((w_pkt.m_pcData), (hs_size));
        w_pkt.setLength(hs_size);
        HLOGC(cnlog.Debug, log << "createSrtHandshake: (no ext) size=" << hs_size << " data: " << w_hs.show());
        return true;
    }

    // Sanity check, applies to HSv5 only cases.
    if (srths_cmd == SRT_CMD_HSREQ && m_SrtHsSide == HSD_RESPONDER)
    {
        m_RejectReason = SRT_REJ_IPE;
        LOGC(cnlog.Fatal, log << "IPE: SRT_CMD_HSREQ was requested to be sent in HSv5 by an INITIATOR side!");
        return false; // should cause rejection
    }

    ostringstream logext;
    logext << "HSX";

    // Install the SRT extensions
    w_hs.m_iType |= CHandShake::HS_EXT_HSREQ;

    bool have_sid = false;
    if (srths_cmd == SRT_CMD_HSREQ && !m_config.sStreamName.empty())
    {
        have_sid = true;
        w_hs.m_iType |= CHandShake::HS_EXT_CONFIG;
        logext << ",SID";
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
        else if (IsSet(m_uPeerSrtFlags, SRT_OPT_FILTERCAP))
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
    bool have_filter  = false;
    if (peer_filter_capable && !m_config.sPacketFilterConfig.empty())
    {
        have_filter = true;
        w_hs.m_iType |= CHandShake::HS_EXT_CONFIG;
        logext << ",filter";
    }

    bool have_congctl = false;
    const string sm = m_config.sCongestion.str();
    if (sm != "" && sm != "live")
    {
        have_congctl = true;
        w_hs.m_iType |= CHandShake::HS_EXT_CONFIG;
        logext << ",CONGCTL";
    }

    bool have_kmreq   = false;
    // Prevent adding KMRSP only in case when BOTH:
    // - Agent has set no password
    // - no KMREQ has arrived from Peer
    // KMRSP must be always sent when:
    // - Agent set a password, Peer did not send KMREQ: Agent sets snd=NOSECRET.
    // - Agent set no password, but Peer sent KMREQ: Ageng sets rcv=NOSECRET.
    if (m_config.CryptoSecret.len > 0 || kmdata_wordsize > 0)
    {
        have_kmreq = true;
        w_hs.m_iType |= CHandShake::HS_EXT_KMREQ;
        logext << ",KMX";
    }

#if ENABLE_BONDING
    bool have_group = false;

    // Note: this is done without locking because we have the following possibilities:
    //
    // 1. Most positive: the group will be the same all the time up to the moment when we use it.
    // 2. The group will disappear when next time we try to use it having now have_group set true.
    //
    // Not possible that a group is NULL now but would appear later: the group must be either empty
    // or already set as valid at this time.
    //
    // If the 2nd possibility happens, then simply it means that the group has been closed during
    // the operation and the socket got this information updated in the meantime. This means that
    // it was an abnormal interrupt during the processing so the handshake process should be aborted
    // anyway, and that's what will be done.

    // LOCKING INFORMATION: accesing this field just for NULL check doesn't
    // hurt, even if this field could be dangling in the moment. This will be
    // followed by an additional check, done this time under lock, and there will
    // be no dangling pointers at this time.
    if (m_parent->m_GroupOf)
    {
        // Whatever group this socket belongs to, the information about
        // the group is always sent the same way with the handshake.
        have_group = true;
        w_hs.m_iType |= CHandShake::HS_EXT_CONFIG;
        logext << ",GROUP";
    }
#endif

    HLOGC(cnlog.Debug, log << "createSrtHandshake: (ext: " << logext.str() << ") data: " << w_hs.show());

    // NOTE: The HSREQ is practically always required, although may happen
    // in future that CONCLUSION can be sent multiple times for a separate
    // stream encryption support, and this way it won't enclose HSREQ.
    // Also, KMREQ may occur multiple times.

    // So, initially store the UDT legacy handshake.
    size_t hs_size = w_pkt.getLength(), total_ra_size = (hs_size / sizeof(uint32_t)); // Maximum size of data
    w_hs.store_to((w_pkt.m_pcData), (hs_size));                                        // hs_size is updated

    size_t ra_size = hs_size / sizeof(int32_t);

    // Now attach the SRT handshake for HSREQ
    size_t    offset = ra_size;
    uint32_t *p      = reinterpret_cast<uint32_t *>(w_pkt.m_pcData);
    // NOTE: since this point, ra_size has a size in int32_t elements, NOT BYTES.

    // The first 4-byte item is the CMD/LENGTH spec.
    uint32_t *pcmdspec = p + offset; // Remember the location to be filled later, when we know the length
    ++offset;

    // Now use the original function to store the actual SRT_HS data
    // ra_size after that
    // NOTE: so far, ra_size is m_iMaxSRTPayloadSize expressed in number of elements.
    // WILL BE CHANGED HERE.
    ra_size   = fillSrtHandshake((p + offset), total_ra_size - offset, srths_cmd, HS_VERSION_SRT1);
    *pcmdspec = HS_CMDSPEC_CMD::wrap(srths_cmd) | HS_CMDSPEC_SIZE::wrap((uint32_t) ra_size);

    HLOGC(cnlog.Debug,
          log << "createSrtHandshake: after HSREQ: offset=" << offset << " HSREQ size=" << ra_size
              << " space left: " << (total_ra_size - offset));

    // Use only in REQ phase and only if stream name is set
    if (have_sid)
    {
        // Now prepare the string with 4-byte alignment. The string size is limited
        // to half the payload size. Just a sanity check to not pack too much into
        // the conclusion packet.
        size_t size_limit = m_iMaxSRTPayloadSize / 2;

        if (m_config.sStreamName.size() >= size_limit)
        {
            m_RejectReason = SRT_REJ_ROGUE;
            LOGC(cnlog.Warn,
                 log << "createSrtHandshake: stream id too long, limited to " << (size_limit - 1) << " bytes");
            return false;
        }

        offset += ra_size + 1;
        ra_size = fillHsExtConfigString(p + offset - 1, SRT_CMD_SID, m_config.sStreamName.str());

        HLOGC(cnlog.Debug,
              log << "createSrtHandshake: after SID [" << m_config.sStreamName.c_str()
                  << "] length=" << m_config.sStreamName.size() << " alignedln=" << (4 * ra_size)
                  << ": offset=" << offset << " SID size=" << ra_size << " space left: " << (total_ra_size - offset));
    }

    if (have_congctl)
    {
        // Pass the congctl to the other side as informational.
        // The other side should reject connection if it uses a different congctl.
        // The other side should also respond with the congctl it uses, if its non-default (for backward compatibility).

        offset += ra_size + 1;
        ra_size = fillHsExtConfigString(p + offset - 1, SRT_CMD_CONGESTION, sm);

        HLOGC(cnlog.Debug,
              log << "createSrtHandshake: after CONGCTL [" << sm << "] length=" << sm.size()
                  << " alignedln=" << (4 * ra_size) << ": offset=" << offset << " CONGCTL size=" << ra_size
                  << " space left: " << (total_ra_size - offset));
    }

    if (have_filter)
    {
        offset += ra_size + 1;
        ra_size = fillHsExtConfigString(p + offset - 1, SRT_CMD_FILTER, m_config.sPacketFilterConfig.str());

        HLOGC(cnlog.Debug,
              log << "createSrtHandshake: after filter [" << m_config.sPacketFilterConfig.c_str() << "] length="
                  << m_config.sPacketFilterConfig.size() << " alignedln=" << (4 * ra_size) << ": offset=" << offset
                  << " filter size=" << ra_size << " space left: " << (total_ra_size - offset));
    }

#if ENABLE_BONDING
    // Note that this will fire in both cases:
    // - When the group has been set by the user on a socket (or socket was created as a part of the group),
    //   and the handshake request is to be sent with informing the peer that this conenction belongs to a group
    // - When the agent received a HS request with a group, has created its mirror group on its side, and
    //   now sends the HS response to the peer, with ITS OWN group id (the mirror one).
    //
    // XXX Probably a condition should be checked here around the group type.
    // The time synchronization should be done only on any kind of parallel sending group.
    // Currently all groups are such groups (broadcast, backup, balancing), but it may
    // need to be changed for some other types.
    if (have_group)
    {
        // NOTE: See information about mutex ordering in api.h
        ScopedLock gdrg (uglobal().m_GlobControlLock);
        if (!m_parent->m_GroupOf)
        {
            // This may only happen if since last check of m_GroupOf pointer the socket was removed
            // from the group in the meantime, which can only happen due to that the group was closed.
            // In such a case it simply means that the handshake process was requested to be interrupted.
            LOGC(cnlog.Fatal, log << "GROUP DISAPPEARED. Socket not capable of continuing HS");
            return false;
        }
        else
        {
            if (m_parent->m_GroupOf->closing())
            {
                m_RejectReason = SRT_REJ_IPE;
                LOGC(cnlog.Error, log << "createSrtHandshake: group is closing during the process, rejecting.");
                return false;

            }
            offset += ra_size + 1;
            ra_size = fillHsExtGroup(p + offset - 1);

            HLOGC(cnlog.Debug, log << "createSrtHandshake: after GROUP [" << sm << "] length=" << sm.size()
                    << ": offset=" << offset << " GROUP size=" << ra_size << " space left: " << (total_ra_size - offset));
        }
    }
#endif

    // When encryption turned on
    if (have_kmreq)
    {
        HLOGC(cnlog.Debug,
              log << "createSrtHandshake: "
                  << (m_config.CryptoSecret.len > 0 ? "Agent uses ENCRYPTION" : "Peer requires ENCRYPTION"));

        if (!m_pCryptoControl && (srtkm_cmd == SRT_CMD_KMREQ || srtkm_cmd == SRT_CMD_KMRSP))
        {
            m_RejectReason = SRT_REJ_IPE;
            LOGC(cnlog.Error, log << "createSrtHandshake: IPE: need to send KM, but CryptoControl does not exist."
                << " Socket state: connected=" << boolalpha << m_bConnected << ", connecting=" << m_bConnecting
                << ", broken=" << m_bBroken << ", closing=" << m_bClosing << ".");
            return false;
        }

        if (srtkm_cmd == SRT_CMD_KMREQ)
        {
            bool have_any_keys = false;
            for (size_t ki = 0; ki < 2; ++ki)
            {
                // Skip those that have expired
                if (!m_pCryptoControl->getKmMsg_needSend(ki, false))
                    continue;

                m_pCryptoControl->getKmMsg_markSent(ki, false);

                offset += ra_size + 1;
                ra_size = fillHsExtKMREQ(p + offset - 1, ki);

                have_any_keys = true;
            }

            if (!have_any_keys)
            {
                m_RejectReason = SRT_REJ_IPE;
                LOGC(cnlog.Error, log << "createSrtHandshake: IPE: all keys have expired, no KM to send.");
                return false;
            }
        }
        else if (srtkm_cmd == SRT_CMD_KMRSP)
        {
            offset += ra_size + 1;
            ra_size = fillHsExtKMRSP(p + offset - 1, kmdata, kmdata_wordsize);
        }
        else
        {
            m_RejectReason = SRT_REJ_IPE;
            LOGC(cnlog.Fatal, log << "createSrtHandshake: IPE: wrong value of srtkm_cmd: " << srtkm_cmd);
            return false;
        }
    }

    if (ra_size == 0)
    {
        // m_RejectReason is expected to be set by fillHsExtKMRSP(..) in this case.
        return false;
    }

    // ra_size + offset has a value in element unit.
    // Switch it again to byte unit.
    w_pkt.setLength((ra_size + offset) * sizeof(int32_t));

    HLOGC(cnlog.Debug,
          log << "createSrtHandshake: filled HSv5 handshake flags: " << CHandShake::ExtensionFlagStr(w_hs.m_iType)
              << " length: " << w_pkt.getLength() << " bytes");

    return true;
}

template <class Integer>
static inline int FindExtensionBlock(Integer* begin, size_t total_length,
        size_t& w_out_len, Integer*& w_next_block)
{
    // Check if there's anything to process
    if (total_length == 0)
    {
        w_next_block = NULL;
        w_out_len    = 0;
        return SRT_CMD_NONE;
    }

    // This function extracts the block command from the block and its length.
    // The command value is returned as a function result.
    // The size of that command block is stored into w_out_len.
    // The beginning of the prospective next block is stored in w_next_block.

    // The caller must be aware that:
    // - exactly one element holds the block header (cmd+size), so the actual data are after this one.
    // - the returned size is the number of uint32_t elements since that first data element
    // - the remaining size should be manually calculated as total_length - 1 - w_out_len, or
    // simply, as w_next_block - begin.

    // Note that if the total_length is too short to extract the whole block, it will return
    // SRT_CMD_NONE. Note that total_length includes this first CMDSPEC word.
    //
    // When SRT_CMD_NONE is returned, it means that nothing has been extracted and nothing else
    // can be further extracted from this block.

    int    cmd  = HS_CMDSPEC_CMD::unwrap(*begin);
    size_t size = HS_CMDSPEC_SIZE::unwrap(*begin);

    if (size + 1 > total_length)
        return SRT_CMD_NONE;

    w_out_len = size;

    if (total_length == size + 1)
        w_next_block = NULL;
    else
        w_next_block = begin + 1 + size;

    return cmd;
}

// NOTE: the rule of order of arguments is broken here because this order
// serves better the logics and readability.
template <class Integer>
static inline bool NextExtensionBlock(Integer*& w_begin, Integer* next, size_t& w_length)
{
    if (!next)
        return false;

    w_length = w_length - (next - w_begin);
    w_begin  = next;
    return true;
}

void SrtExtractHandshakeExtensions(const char* bufbegin, size_t buflength,
        vector<SrtHandshakeExtension>& w_output)
{
    const uint32_t *begin = reinterpret_cast<const uint32_t *>(bufbegin + CHandShake::m_iContentSize);
    size_t    size  = buflength - CHandShake::m_iContentSize; // Due to previous cond check we grant it's >0
    const uint32_t *next  = 0;
    size_t    length   = size / sizeof(uint32_t);
    size_t    blocklen = 0;

    for (;;) // ONE SHOT, but continuable loop
    {
        const int cmd = FindExtensionBlock(begin, length, (blocklen), (next));

        if (cmd == SRT_CMD_NONE)
        {
            // End of blocks
            break;
        }

        w_output.push_back(SrtHandshakeExtension(cmd));

        SrtHandshakeExtension& ext = w_output.back();

        std::copy(begin+1, begin+blocklen+1, back_inserter(ext.contents));

        // Any other kind of message extracted. Search on.
        if (!NextExtensionBlock((begin), next, (length)))
            break;
    }
}

#if SRT_DEBUG_RTT
class RttTracer
{
public:
    RttTracer()
    {
    }

    ~RttTracer()
    {
        srt::sync::ScopedLock lck(m_mtx);
        m_fout.close();
    }

    void trace(const srt::sync::steady_clock::time_point& currtime,
               const std::string& event, int rtt_sample, int rttvar_sample,
               bool is_smoothed_rtt_reset, int64_t recvTotal,
               int smoothed_rtt, int rttvar)
    {
        srt::sync::ScopedLock lck(m_mtx);
        create_file();
        
        m_fout << srt::sync::FormatTimeSys(currtime) << ",";
        m_fout << srt::sync::FormatTime(currtime) << ",";
        m_fout << event << ",";
        m_fout << rtt_sample << ",";
        m_fout << rttvar_sample << ",";
        m_fout << is_smoothed_rtt_reset << ",";
        m_fout << recvTotal << ",";
        m_fout << smoothed_rtt << ",";
        m_fout << rttvar << "\n";
        m_fout.flush();
    }

private:
    void print_header()
    {
        m_fout << "Timepoint_SYST,Timepoint_STDY,Event,usRTTSample,"
                  "usRTTVarSample,IsSmoothedRTTReset,pktsRecvTotal,"
                  "usSmoothedRTT,usRTTVar\n";
    }

    void create_file()
    {
        if (m_fout.is_open())
            return;

        std::string str_tnow = srt::sync::FormatTimeSys(srt::sync::steady_clock::now());
        str_tnow.resize(str_tnow.size() - 7); // remove trailing ' [SYST]' part
        while (str_tnow.find(':') != std::string::npos) {
            str_tnow.replace(str_tnow.find(':'), 1, 1, '_');
        }
        const std::string fname = "rtt_trace_" + str_tnow + "_" + SRT_SYNC_CLOCK_STR + ".csv";
        m_fout.open(fname, std::ofstream::out);
        if (!m_fout)
            std::cerr << "IPE: Failed to open " << fname << "!!!\n";

        print_header();
    }

private:
    srt::sync::Mutex m_mtx;
    std::ofstream m_fout;
};

RttTracer s_rtt_trace;
#endif


bool srt::CUDT::processSrtMsg(const CPacket *ctrlpkt)
{
    uint32_t *srtdata = (uint32_t *)ctrlpkt->m_pcData;
    size_t    len     = ctrlpkt->getLength();
    int       etype   = ctrlpkt->getExtendedType();
    uint32_t  ts      = ctrlpkt->m_iTimeStamp;

    int res = SRT_CMD_NONE;

    HLOGC(cnlog.Debug, log << "Dispatching message type=" << etype << " data length=" << (len / sizeof(int32_t)));
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
            res = m_pCryptoControl->processSrtMsg_KMREQ(srtdata, len, CUDT::HS_VERSION_UDT4,
                    (srtdata_out), (len_out));
            if (res == SRT_CMD_KMRSP)
            {
                if (len_out == 1)
                {
                    if (m_config.bEnforcedEnc)
                    {
                        LOGC(cnlog.Warn,
                             log << "KMREQ FAILURE: " << KmStateStr(SRT_KM_STATE(srtdata_out[0]))
                                 << " - rejecting per enforced encryption");
                        res = SRT_CMD_NONE;
                        break;
                    }
                    HLOGC(cnlog.Debug,
                          log << "MKREQ -> KMRSP FAILURE state: " << KmStateStr(SRT_KM_STATE(srtdata_out[0])));
                }
                else
                {
                    HLOGC(cnlog.Debug, log << "KMREQ -> requested to send KMRSP length=" << len_out);
                }
                sendSrtMsg(SRT_CMD_KMRSP, srtdata_out, len_out);
            }
            // XXX Dead code. processSrtMsg_KMREQ now doesn't return any other value now.
            // Please review later.
            else
            {
                LOGC(cnlog.Warn, log << "KMREQ failed to process the request - ignoring");
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

int srt::CUDT::processSrtMsg_HSREQ(const uint32_t *srtdata, size_t bytelen, uint32_t ts, int hsv)
{
    // Set this start time in the beginning, regardless as to whether TSBPD is being
    // used or not. This must be done in the Initiator as well as Responder.

    /*
     * Compute peer StartTime in our time reference
     * This takes time zone, time drift into account.
     * Also includes current packet transit time (rtt/2)
     */
    m_tsRcvPeerStartTime = steady_clock::now() - microseconds_from(ts);
    // (in case of bonding group, this value will be OVERWRITTEN
    // later in CUDT::interpretGroup).

    // Prepare the initial runtime values of latency basing on the option values.
    // They are going to get the value fixed HERE.
    m_iTsbPdDelay_ms     = m_config.iRcvLatency;
    m_iPeerTsbPdDelay_ms = m_config.iPeerLatency;

    if (bytelen < SRT_CMD_HSREQ_MINSZ)
    {
        m_RejectReason = SRT_REJ_ROGUE;
        /* Packet smaller than minimum compatible packet size */
        LOGF(cnlog.Error, "HSREQ/rcv: cmd=%d(HSREQ) len=%" PRIzu " invalid", SRT_CMD_HSREQ, bytelen);
        return SRT_CMD_NONE;
    }

    LOGF(cnlog.Note,
         "HSREQ/rcv: cmd=%d(HSREQ) len=%" PRIzu " vers=0x%x opts=0x%x delay=%d",
         SRT_CMD_HSREQ,
         bytelen,
         srtdata[SRT_HS_VERSION],
         srtdata[SRT_HS_FLAGS],
         SRT_HS_LATENCY_RCV::unwrap(srtdata[SRT_HS_LATENCY]));

    m_uPeerSrtVersion = srtdata[SRT_HS_VERSION];
    m_uPeerSrtFlags   = srtdata[SRT_HS_FLAGS];

    if (hsv == CUDT::HS_VERSION_UDT4)
    {
        if (m_uPeerSrtVersion >= SRT_VERSION_FEAT_HSv5)
        {
            m_RejectReason = SRT_REJ_ROGUE;
            LOGC(cnlog.Error,
                 log << "HSREQ/rcv: With HSv4 version >= " << SrtVersionString(SRT_VERSION_FEAT_HSv5)
                     << " is not acceptable.");
            return SRT_CMD_REJECT;
        }
    }
    else
    {
        if (m_uPeerSrtVersion < SRT_VERSION_FEAT_HSv5)
        {
            m_RejectReason = SRT_REJ_ROGUE;
            LOGC(cnlog.Error,
                 log << "HSREQ/rcv: With HSv5 version must be >= " << SrtVersionString(SRT_VERSION_FEAT_HSv5) << " .");
            return SRT_CMD_REJECT;
        }
    }

    // Check also if the version satisfies the minimum required version
    if (m_uPeerSrtVersion < m_config.uMinimumPeerSrtVersion)
    {
        m_RejectReason = SRT_REJ_VERSION;
        LOGC(cnlog.Error,
             log << "HSREQ/rcv: Peer version: " << SrtVersionString(m_uPeerSrtVersion)
                 << " is too old for requested: " << SrtVersionString(m_config.uMinimumPeerSrtVersion)
                 << " - REJECTING");
        return SRT_CMD_REJECT;
    }

    HLOGC(cnlog.Debug,
          log << "HSREQ/rcv: PEER Version: " << SrtVersionString(m_uPeerSrtVersion) << " Flags: " << m_uPeerSrtFlags
              << "(" << SrtFlagString(m_uPeerSrtFlags)
              << ") Min req version:" << SrtVersionString(m_config.uMinimumPeerSrtVersion));

    m_bPeerRexmitFlag = IsSet(m_uPeerSrtFlags, SRT_OPT_REXMITFLG);
    HLOGF(cnlog.Debug, "HSREQ/rcv: peer %s REXMIT flag", m_bPeerRexmitFlag ? "UNDERSTANDS" : "DOES NOT UNDERSTAND");

    // Check if both use the same API type. Reject if not.
    bool peer_message_api = !IsSet(m_uPeerSrtFlags, SRT_OPT_STREAM);
    if (peer_message_api != m_config.bMessageAPI)
    {
        m_RejectReason = SRT_REJ_MESSAGEAPI;
        LOGC(cnlog.Error,
             log << "HSREQ/rcv: Agent uses " << (m_config.bMessageAPI ? "MESSAGE" : "STREAM")
                 << " API, but the Peer declares " << (peer_message_api ? "MESSAGE" : "STREAM")
                 << " API. Not compatible transmission type, rejecting.");
        return SRT_CMD_REJECT;
    }

    SRT_STATIC_ASSERT(SRT_HS_E_SIZE == SRT_HS_LATENCY + 1, "Assuming latency is the last field");
    if (bytelen < (SRT_HS_E_SIZE * sizeof(uint32_t)))
    {
        // Handshake extension message includes VERSION, FLAGS and LATENCY
        // (3 x 32 bits). SRT v1.2.0 and earlier might supply shorter extension message,
        // without LATENCY fields.
        // It is acceptable, as long as the latency flags are not set on our side.
        //
        //  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
        // +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
        // |                          SRT Version                          |
        // +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
        // |                           SRT Flags                           |
        // +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
        // |      Receiver TSBPD Delay     |       Sender TSBPD Delay      |
        // +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
        if (IsSet(m_uPeerSrtFlags, SRT_OPT_TSBPDSND) || IsSet(m_uPeerSrtFlags, SRT_OPT_TSBPDRCV))
        {
            m_RejectReason = SRT_REJ_ROGUE;
            LOGC(cnlog.Error,
                 log << "HSREQ/rcv: Peer sent only VERSION + FLAGS HSREQ, but TSBPD flags are set. Rejecting.");
            return SRT_CMD_REJECT;
        }

        LOGC(cnlog.Warn, log << "HSREQ/rcv: Peer sent only VERSION + FLAGS HSREQ, not getting any TSBPD settings.");
        // Don't process any further settings in this case. Turn off TSBPD, just for a case.
        m_bTsbPd     = false;
        m_bPeerTsbPd = false;
        return SRT_CMD_HSRSP;
    }

    const uint32_t latencystr = srtdata[SRT_HS_LATENCY];

    if (IsSet(m_uPeerSrtFlags, SRT_OPT_TSBPDSND))
    {
        // TimeStamp-based Packet Delivery feature enabled
        if (!isOPT_TsbPd())
        {
            LOGC(cnlog.Warn, log << "HSREQ/rcv: Agent did not set rcv-TSBPD - ignoring proposed latency from peer");

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
            HLOGC(cnlog.Debug,
                  log << "HSREQ/rcv: LOCAL/RCV LATENCY: Agent:" << m_iTsbPdDelay_ms << " Peer:" << peer_decl_latency
                      << "  Selecting:" << maxdelay);
            m_iTsbPdDelay_ms = maxdelay;
            m_bTsbPd = true;
        }
    }
    else
    {
        std::string how_about_agent = isOPT_TsbPd() ? "BUT AGENT DOES" : "and nor does Agent";
        HLOGC(cnlog.Debug, log << "HSREQ/rcv: Peer DOES NOT USE latency for sending - " << how_about_agent);
    }

    // This happens when the HSv5 RESPONDER receives the HSREQ message; it declares
    // that the peer INITIATOR will receive the data and informs about its predefined
    // latency. We need to maximize this with our setting of the peer's latency and
    // record as peer's latency, which will be then sent back with HSRSP.
    if (hsv > CUDT::HS_VERSION_UDT4 && IsSet(m_uPeerSrtFlags, SRT_OPT_TSBPDRCV))
    {
        // So, PEER uses TSBPD, set the flag.
        // NOTE: it doesn't matter, if AGENT uses TSBPD.
        m_bPeerTsbPd = true;

        // SRT_HS_LATENCY_RCV is the value that the peer declares as to be
        // used by it when receiving data. We take this as a peer's value,
        // and select the maximum of this one and our proposed latency for the peer.
        int peer_decl_latency = SRT_HS_LATENCY_RCV::unwrap(latencystr);
        int maxdelay          = std::max(m_iPeerTsbPdDelay_ms, peer_decl_latency);
        HLOGC(cnlog.Debug,
              log << "HSREQ/rcv: PEER/RCV LATENCY: Agent:" << m_iPeerTsbPdDelay_ms << " Peer:" << peer_decl_latency
                  << " Selecting:" << maxdelay);
        m_iPeerTsbPdDelay_ms = maxdelay;
    }
    else
    {
        std::string how_about_agent = isOPT_TsbPd() ? "BUT AGENT DOES" : "and nor does Agent";
        HLOGC(cnlog.Debug, log << "HSREQ/rcv: Peer DOES NOT USE latency for receiving - " << how_about_agent);
    }

    if (hsv > CUDT::HS_VERSION_UDT4)
    {
        // This is HSv5, do the same things as required for the sending party in HSv4,
        // as in HSv5 this can also be a sender.
        if (IsSet(m_uPeerSrtFlags, SRT_OPT_TLPKTDROP))
        {
            // Too late packets dropping feature supported
            m_bPeerTLPktDrop = true;
        }
        if (IsSet(m_uPeerSrtFlags, SRT_OPT_NAKREPORT))
        {
            // Peer will send Periodic NAK Reports
            m_bPeerNakReport = true;
        }
    }

    return SRT_CMD_HSRSP;
}

int srt::CUDT::processSrtMsg_HSRSP(const uint32_t *srtdata, size_t bytelen, uint32_t ts, int hsv)
{
    // XXX Check for mis-version
    // With HSv4 we accept only version less than 1.3.0
    if (hsv == CUDT::HS_VERSION_UDT4 && srtdata[SRT_HS_VERSION] >= SRT_VERSION_FEAT_HSv5)
    {
        LOGC(cnlog.Error, log << "HSRSP/rcv: With HSv4 version >= 1.2.0 is not acceptable.");
        return SRT_CMD_NONE;
    }

    if (bytelen < SRT_CMD_HSRSP_MINSZ)
    {
        /* Packet smaller than minimum compatible packet size */
        LOGF(cnlog.Error, "HSRSP/rcv: cmd=%d(HSRSP) len=%" PRIzu " invalid", SRT_CMD_HSRSP, bytelen);
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

    if (is_zero(m_tsRcvPeerStartTime))
    {
        // Do not set this time when it's already set, which may be the case
        // if the agent has this value already "borrowed" from a master socket
        // that was in the group at the time when it was added.
        m_tsRcvPeerStartTime = steady_clock::now() - microseconds_from(ts);
        HLOGC(cnlog.Debug, log << "HSRSP/rcv: PEER START TIME not yet defined, setting: " << FormatTime(m_tsRcvPeerStartTime));
    }
    else
    {
        HLOGC(cnlog.Debug, log << "HSRSP/rcv: PEER START TIME already set (derived): " << FormatTime(m_tsRcvPeerStartTime));
    }

    m_uPeerSrtVersion = srtdata[SRT_HS_VERSION];
    m_uPeerSrtFlags   = srtdata[SRT_HS_FLAGS];

    HLOGF(cnlog.Debug,
          "HSRSP/rcv: Version: %s Flags: SND:%08X (%s)",
          SrtVersionString(m_uPeerSrtVersion).c_str(),
          m_uPeerSrtFlags,
          SrtFlagString(m_uPeerSrtFlags).c_str());

    // Basic version check
    if (m_uPeerSrtVersion < m_config.uMinimumPeerSrtVersion)
    {
        m_RejectReason = SRT_REJ_VERSION;
        LOGC(cnlog.Error,
             log << "HSRSP/rcv: Peer version: " << SrtVersionString(m_uPeerSrtVersion)
                 << " is too old for requested: " << SrtVersionString(m_config.uMinimumPeerSrtVersion)
                 << " - REJECTING");
        return SRT_CMD_REJECT;
    }

    if (hsv == CUDT::HS_VERSION_UDT4)
    {
        // The old HSv4 way: extract just one value and put it under peer.
        if (IsSet(m_uPeerSrtFlags, SRT_OPT_TSBPDRCV))
        {
            // TsbPd feature enabled
            m_bPeerTsbPd         = true;
            m_iPeerTsbPdDelay_ms = SRT_HS_LATENCY_LEG::unwrap(srtdata[SRT_HS_LATENCY]);
            HLOGC(cnlog.Debug,
                  log << "HSRSP/rcv: LATENCY: Peer/snd:" << m_iPeerTsbPdDelay_ms
                      << " (Agent: declared:" << m_iTsbPdDelay_ms << " rcv:" << m_iTsbPdDelay_ms << ")");
        }
        // TSBPDSND isn't set in HSv4 by the RESPONDER, because HSv4 RESPONDER is always RECEIVER.
    }
    else
    {
        // HSv5 way: extract the receiver latency and sender latency, if used.

        // PEER WILL RECEIVE TSBPD == AGENT SHALL SEND TSBPD.
        if (IsSet(m_uPeerSrtFlags, SRT_OPT_TSBPDRCV))
        {
            // TsbPd feature enabled
            m_bPeerTsbPd         = true;
            m_iPeerTsbPdDelay_ms = SRT_HS_LATENCY_RCV::unwrap(srtdata[SRT_HS_LATENCY]);
            HLOGC(cnlog.Debug, log << "HSRSP/rcv: LATENCY: Peer/snd:" << m_iPeerTsbPdDelay_ms << "ms");
        }
        else
        {
            HLOGC(cnlog.Debug, log << "HSRSP/rcv: Peer (responder) DOES NOT USE latency");
        }

        // PEER WILL SEND TSBPD == AGENT SHALL RECEIVE TSBPD.
        if (IsSet(m_uPeerSrtFlags, SRT_OPT_TSBPDSND))
        {
            if (!isOPT_TsbPd())
            {
                LOGC(cnlog.Warn,
                     log << "HSRSP/rcv: BUG? Peer (responder) declares sending latency, but Agent turned off TSBPD.");
            }
            else
            {
                m_bTsbPd = true; // NOTE: in case of Group TSBPD receiving, this field will be SWITCHED TO m_bGroupTsbPd.
                // Take this value as a good deal. In case when the Peer did not "correct" the latency
                // because it has TSBPD turned off, just stay with the present value defined in options.
                m_iTsbPdDelay_ms = SRT_HS_LATENCY_SND::unwrap(srtdata[SRT_HS_LATENCY]);
                HLOGC(cnlog.Debug, log << "HSRSP/rcv: LATENCY Agent/rcv: " << m_iTsbPdDelay_ms << "ms");
            }
        }
    }

    if ((m_config.uSrtVersion >= SrtVersion(1, 0, 5)) && IsSet(m_uPeerSrtFlags, SRT_OPT_TLPKTDROP))
    {
        // Too late packets dropping feature supported
        m_bPeerTLPktDrop = true;
    }

    if ((m_config.uSrtVersion >= SrtVersion(1, 1, 0)) && IsSet(m_uPeerSrtFlags, SRT_OPT_NAKREPORT))
    {
        // Peer will send Periodic NAK Reports
        m_bPeerNakReport = true;
    }

    if (m_config.uSrtVersion >= SrtVersion(1, 2, 0))
    {
        if (IsSet(m_uPeerSrtFlags, SRT_OPT_REXMITFLG))
        {
            // Peer will use REXMIT flag in packet retransmission.
            m_bPeerRexmitFlag = true;
            HLOGP(cnlog.Debug, "HSRSP/rcv: 1.2.0+ Agent understands REXMIT flag and so does peer.");
        }
        else
        {
            HLOGP(cnlog.Debug, "HSRSP/rcv: Agent understands REXMIT flag, but PEER DOES NOT");
        }
    }
    else
    {
        HLOGF(cnlog.Debug, "HSRSP/rcv: <1.2.0 Agent DOESN'T understand REXMIT flag");
    }

    handshakeDone();

    return SRT_CMD_NONE;
}

// This function is called only when the URQ_CONCLUSION handshake has been received from the peer.
bool srt::CUDT::interpretSrtHandshake(const CHandShake& hs,
                                 const CPacket&    hspkt,
                                 uint32_t*         out_data SRT_ATR_UNUSED,
                                 size_t*           pw_len)
{
    // Initialize pw_len to 0 to handle the unencrypted case
    if (pw_len)
        *pw_len = 0;

    // The version=0 statement as rejection is used only since HSv5.
    // The HSv4 sends the AGREEMENT handshake message with version=0, do not misinterpret it.
    if (m_ConnRes.m_iVersion > HS_VERSION_UDT4 && hs.m_iVersion == 0)
    {
        m_RejectReason = SRT_REJ_PEER;
        LOGC(cnlog.Error, log << "HS VERSION = 0, meaning the handshake has been rejected.");
        return false;
    }

    if (hs.m_iVersion < HS_VERSION_SRT1)
    {
        if (m_config.uMinimumPeerSrtVersion && m_config.uMinimumPeerSrtVersion >= SRT_VERSION_FEAT_HSv5)
        {
            m_RejectReason = SRT_REJ_VERSION;
            // This means that a version with minimum 1.3.0 that features HSv5 is required,
            // hence all HSv4 clients should be rejected.
            LOGP(cnlog.Error, "interpretSrtHandshake: minimum peer version 1.3.0 (HSv5 only), rejecting HSv4 client");
            return false;
        }
        return true; // do nothing
    }

    // Anyway, check if the handshake contains any extra data.
    if (hspkt.getLength() <= CHandShake::m_iContentSize)
    {
        m_RejectReason = SRT_REJ_ROGUE;
        // This would mean that the handshake was at least HSv5, but somehow no extras were added.
        // Dismiss it then, however this has to be logged.
        LOGC(cnlog.Error, log << "HS VERSION=" << hs.m_iVersion << " but no handshake extension found!");
        return false;
    }

    // We still believe it should work, let's check the flags.
    const int ext_flags = SrtHSRequest::SRT_HSTYPE_HSFLAGS::unwrap(hs.m_iType);
    if (ext_flags == 0)
    {
        m_RejectReason = SRT_REJ_ROGUE;
        LOGC(cnlog.Error, log << "HS VERSION=" << hs.m_iVersion << " but no handshake extension flags are set!");
        return false;
    }

    HLOGC(cnlog.Debug,
          log << "HS VERSION=" << hs.m_iVersion << " EXTENSIONS: " << CHandShake::ExtensionFlagStr(ext_flags));

    // Ok, now find the beginning of an int32_t array that follows the UDT handshake.
    uint32_t* p    = reinterpret_cast<uint32_t*>(hspkt.m_pcData + CHandShake::m_iContentSize);
    size_t    size = hspkt.getLength() - CHandShake::m_iContentSize; // Due to previous cond check we grant it's >0

    int hsreq_type_cmd SRT_ATR_UNUSED = SRT_CMD_NONE;

    if (IsSet(ext_flags, CHandShake::HS_EXT_HSREQ))
    {
        HLOGC(cnlog.Debug, log << "interpretSrtHandshake: extracting HSREQ/RSP type extension");
        uint32_t *begin    = p;
        uint32_t *next     = 0;
        size_t    length   = size / sizeof(uint32_t);
        size_t    blocklen = 0;

        for (;;) // this is ONE SHOT LOOP
        {
            int cmd = FindExtensionBlock(begin, length, (blocklen), (next));

            size_t bytelen = blocklen * sizeof(uint32_t);

            if (cmd == SRT_CMD_HSREQ)
            {
                hsreq_type_cmd = cmd;
                // Set is the size as it should, then give it for interpretation for
                // the proper function.
                if (blocklen < SRT_HS_E_SIZE)
                {
                    m_RejectReason = SRT_REJ_ROGUE;
                    LOGC(cnlog.Error,
                         log << "HS-ext HSREQ found but invalid size: " << bytelen << " (expected: " << SRT_HS_E_SIZE
                             << ")");
                    return false; // don't interpret
                }

                int rescmd = processSrtMsg_HSREQ(begin + 1, bytelen, hspkt.m_iTimeStamp, HS_VERSION_SRT1);
                // Interpreted? Then it should be responded with SRT_CMD_HSRSP.
                if (rescmd != SRT_CMD_HSRSP)
                {
                    // m_RejectReason already set
                    LOGC(cnlog.Error,
                         log << "interpretSrtHandshake: process HSREQ returned unexpected value " << rescmd);
                    return false;
                }
                handshakeDone();
                // updateAfterSrtHandshake -> moved to postConnect and processRendezvous
            }
            else if (cmd == SRT_CMD_HSRSP)
            {
                hsreq_type_cmd = cmd;
                // Set is the size as it should, then give it for interpretation for
                // the proper function.
                if (blocklen < SRT_HS_E_SIZE)
                {
                    m_RejectReason = SRT_REJ_ROGUE;
                    LOGC(cnlog.Error,
                         log << "HS-ext HSRSP found but invalid size: " << bytelen << " (expected: " << SRT_HS_E_SIZE
                             << ")");

                    return false; // don't interpret
                }

                int rescmd = processSrtMsg_HSRSP(begin + 1, bytelen, hspkt.m_iTimeStamp, HS_VERSION_SRT1);
                // Interpreted? Then it should be responded with SRT_CMD_NONE.
                // (nothing to be responded for HSRSP, unless there was some kinda problem)
                if (rescmd != SRT_CMD_NONE)
                {
                    // Just formally; the current code doesn't seem to return anything else
                    // (unless it's already set)
                    if (m_RejectReason == SRT_REJ_UNKNOWN)
                        m_RejectReason = SRT_REJ_ROGUE;
                    LOGC(cnlog.Error,
                         log << "interpretSrtHandshake: process HSRSP returned unexpected value " << rescmd);
                    return false;
                }
                handshakeDone();
                // updateAfterSrtHandshake -> moved to postConnect and processRendezvous
            }
            else if (cmd == SRT_CMD_NONE)
            {
                m_RejectReason = SRT_REJ_ROGUE;
                LOGC(cnlog.Warn, log << "interpretSrtHandshake: no HSREQ/HSRSP block found in the handshake msg!");
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

    HLOGC(cnlog.Debug, log << "interpretSrtHandshake: HSREQ done, checking KMREQ");

    // Now check the encrypted

    bool encrypted = false;

    if (IsSet(ext_flags, CHandShake::HS_EXT_KMREQ))
    {
        HLOGC(cnlog.Debug, log << "interpretSrtHandshake: extracting KMREQ/RSP type extension");

#ifdef SRT_ENABLE_ENCRYPTION
        if (!m_pCryptoControl->hasPassphrase())
        {
            if (m_config.bEnforcedEnc)
            {
                m_RejectReason = SRT_REJ_UNSECURE;
                LOGC(cnlog.Error,
                    log << "HS KMREQ: Peer declares encryption, but agent does not - rejecting per enforced encryption");
                return false;
            }

            LOGC(cnlog.Warn,
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
            int cmd = FindExtensionBlock(begin, length, (blocklen), (next));

            HLOGC(cnlog.Debug,
                  log << "interpretSrtHandshake: found extension: (" << cmd << ") " << MessageTypeStr(UMSG_EXT, cmd));

            size_t bytelen = blocklen * sizeof(uint32_t);
            if (cmd == SRT_CMD_KMREQ)
            {
                if (!out_data || !pw_len)
                {
                    m_RejectReason = SRT_REJ_IPE;
                    LOGC(cnlog.Fatal, log << "IPE: HS/KMREQ extracted without passing target buffer!");
                    return false;
                }

                int res = m_pCryptoControl->processSrtMsg_KMREQ(begin + 1, bytelen, HS_VERSION_SRT1,
                            (out_data), (*pw_len));
                if (res != SRT_CMD_KMRSP)
                {
                    m_RejectReason = SRT_REJ_IPE;
                    // Something went wrong.
                    HLOGC(cnlog.Debug,
                          log << "interpretSrtHandshake: IPE/EPE KMREQ processing failed - returned " << res);
                    return false;
                }
                if (*pw_len == 1)
                {
                    // This means that there was an abnormal encryption situation occurred.
                    // This is inacceptable in case of strict encryption.
                    if (m_config.bEnforcedEnc)
                    {
                        if (m_pCryptoControl->m_RcvKmState == SRT_KM_S_BADSECRET)
                        {
                            m_RejectReason = SRT_REJ_BADSECRET;
                        }
                        else
                        {
                            m_RejectReason = SRT_REJ_UNSECURE;
                        }
                        LOGC(cnlog.Error,
                             log << "interpretSrtHandshake: KMREQ result abnornal - rejecting per enforced encryption");
                        return false;
                    }
                }
                encrypted = true;
            }
            else if (cmd == SRT_CMD_KMRSP)
            {
                int res = m_pCryptoControl->processSrtMsg_KMRSP(begin + 1, bytelen, HS_VERSION_SRT1);
                if (m_config.bEnforcedEnc && res == -1)
                {
                    m_RejectReason = SRT_REJ_UNSECURE;
                    LOGC(cnlog.Error, log << "KMRSP failed - rejecting connection as per enforced encryption.");
                    return false;
                }
                encrypted = true;
            }
            else if (cmd == SRT_CMD_NONE)
            {
                m_RejectReason = SRT_REJ_ROGUE;
                LOGC(cnlog.Error, log << "HS KMREQ expected - none found!");
                return false;
            }
            else
            {
                HLOGC(cnlog.Debug, log << "interpretSrtHandshake: ... skipping " << MessageTypeStr(UMSG_EXT, cmd));
                if (NextExtensionBlock((begin), next, (length)))
                    continue;
            }

            break;
        }
#else
        // When encryption is not enabled at compile time, behave as if encryption wasn't set,
        // so accordingly to StrictEncryption flag.

        if (m_config.bEnforcedEnc)
        {
            m_RejectReason = SRT_REJ_UNSECURE;
            LOGC(cnlog.Error,
                 log << "HS KMREQ: Peer declares encryption, but agent didn't enable it at compile time - rejecting "
                        "per enforced encryption");
            return false;
        }

        LOGC(cnlog.Warn,
             log << "HS KMREQ: Peer declares encryption, but agent didn't enable it at compile time - still allowing "
                    "connection.");
        encrypted = true;
#endif
    }

    bool   have_congctl = false;
    bool   have_filter  = false;
    string agsm = m_config.sCongestion.str();
    if (agsm == "")
    {
        agsm = "live";
        m_config.sCongestion.set("live", 4);
    }

    bool have_group SRT_ATR_UNUSED = false;

    if (IsSet(ext_flags, CHandShake::HS_EXT_CONFIG))
    {
        HLOGC(cnlog.Debug, log << "interpretSrtHandshake: extracting various CONFIG extensions");

        uint32_t *begin    = p;
        uint32_t *next     = 0;
        size_t    length   = size / sizeof(uint32_t);
        size_t    blocklen = 0;

        for (;;) // This is one shot loop, unless REPEATED by 'continue'.
        {
            int cmd = FindExtensionBlock(begin, length, (blocklen), (next));

            HLOGC(cnlog.Debug,
                  log << "interpretSrtHandshake: found extension: (" << cmd << ") " << MessageTypeStr(UMSG_EXT, cmd));

            const size_t bytelen = blocklen * sizeof(uint32_t);
            if (cmd == SRT_CMD_SID)
            {
                if (!bytelen || bytelen > CSrtConfig::MAX_SID_LENGTH)
                {
                    LOGC(cnlog.Error,
                         log << "interpretSrtHandshake: STREAMID length " << bytelen << " is 0 or > " << +CSrtConfig::MAX_SID_LENGTH
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
                char target[CSrtConfig::MAX_SID_LENGTH + 1];
                memset((target), 0, CSrtConfig::MAX_SID_LENGTH + 1);
                memcpy((target), begin + 1, bytelen);

                // Un-swap on big endian machines
                ItoHLA((uint32_t *)target, (uint32_t *)target, blocklen);

                m_config.sStreamName.set(target, strlen(target));
                HLOGC(cnlog.Debug,
                      log << "CONNECTOR'S REQUESTED SID [" << m_config.sStreamName.c_str() << "] (bytelen=" << bytelen
                          << " blocklen=" << blocklen << ")");
            }
            else if (cmd == SRT_CMD_CONGESTION)
            {
                if (have_congctl)
                {
                    m_RejectReason = SRT_REJ_ROGUE;
                    LOGC(cnlog.Error, log << "CONGCTL BLOCK REPEATED!");
                    return false;
                }

                if (!bytelen || bytelen > CSrtConfig::MAX_CONG_LENGTH)
                {
                    LOGC(cnlog.Error,
                         log << "interpretSrtHandshake: CONGESTION-control type length " << bytelen << " is 0 or > "
                             << +CSrtConfig::MAX_CONG_LENGTH << " - PROTOCOL ERROR, REJECTING");
                    return false;
                }
                // Declare that congctl has been received
                have_congctl = true;

                char target[CSrtConfig::MAX_CONG_LENGTH + 1];
                memset((target), 0, CSrtConfig::MAX_CONG_LENGTH + 1);
                memcpy((target), begin + 1, bytelen);
                // Un-swap on big endian machines
                ItoHLA((uint32_t *)target, (uint32_t *)target, blocklen);

                string sm = target;

                // As the congctl has been declared by the peer,
                // check if your congctl is compatible.
                // sm cannot be empty, but the agent's sm can be empty meaning live.
                if (sm != agsm)
                {
                    m_RejectReason = SRT_REJ_CONGESTION;
                    LOGC(cnlog.Error,
                         log << "PEER'S CONGCTL '" << sm << "' does not match AGENT'S CONGCTL '" << agsm << "'");
                    return false;
                }

                HLOGC(cnlog.Debug,
                      log << "CONNECTOR'S CONGCTL [" << sm << "] (bytelen=" << bytelen << " blocklen=" << blocklen
                          << ")");
            }
            else if (cmd == SRT_CMD_FILTER)
            {
                if (have_filter)
                {
                    m_RejectReason = SRT_REJ_FILTER;
                    LOGC(cnlog.Error, log << "FILTER BLOCK REPEATED!");
                    return false;
                }

                if (!bytelen || bytelen > CSrtConfig::MAX_PFILTER_LENGTH)
                {
                    LOGC(cnlog.Error,
                         log << "interpretSrtHandshake: packet-filter type length " << bytelen << " is 0 or > "
                             << +CSrtConfig::MAX_PFILTER_LENGTH << " - PROTOCOL ERROR, REJECTING");
                    return false;
                }
                // Declare that filter has been received
                have_filter = true;

                char target[CSrtConfig::MAX_PFILTER_LENGTH + 1];
                memset((target), 0, CSrtConfig::MAX_PFILTER_LENGTH + 1);
                memcpy((target), begin + 1, bytelen);
                string fltcfg = target;

                HLOGC(cnlog.Debug,
                      log << "PEER'S FILTER CONFIG [" << fltcfg << "] (bytelen=" << bytelen << " blocklen=" << blocklen
                          << ")");

                if (!checkApplyFilterConfig(fltcfg))
                {
                    m_RejectReason = SRT_REJ_FILTER;
                    LOGC(cnlog.Error, log << "PEER'S FILTER CONFIG [" << fltcfg << "] has been rejected");
                    return false;
                }
            }
#if ENABLE_BONDING
            else if ( cmd == SRT_CMD_GROUP )
            {
                // Note that this will fire in both cases:
                // - When receiving HS request from the Initiator, which belongs to a group, and agent must
                //   create the mirror group on his side (or join the existing one, if there's already
                //   a mirror group for that group ID).
                // - When receiving HS response from the Responder, with its mirror group ID, so the agent
                //   must put the group into his peer group data
                int32_t groupdata[GRPD_E_SIZE] = {};
                if (bytelen < GRPD_MIN_SIZE * GRPD_FIELD_SIZE || bytelen % GRPD_FIELD_SIZE)
                {
                    m_RejectReason = SRT_REJ_ROGUE;
                    LOGC(cnlog.Error, log << "PEER'S GROUP wrong size: " << (bytelen/GRPD_FIELD_SIZE));
                    return false;
                }
                size_t groupdata_size = bytelen / GRPD_FIELD_SIZE;

                memcpy(groupdata, begin+1, bytelen);
                if (!interpretGroup(groupdata, groupdata_size, hsreq_type_cmd) )
                {
                    // m_RejectReason handled inside interpretGroup().
                    return false;
                }

                have_group = true;
                HLOGC(cnlog.Debug, log << "CONNECTOR'S PEER GROUP [" << groupdata[0] << "] (bytelen=" << bytelen << " blocklen=" << blocklen << ")");
            }
#endif
            else if (cmd == SRT_CMD_NONE)
            {
                break;
            }
            else
            {
                // Found some block that is not interesting here. Skip this and get the next one.
                HLOGC(cnlog.Debug, log << "interpretSrtHandshake: ... skipping " << MessageTypeStr(UMSG_EXT, cmd));
            }

            if (!NextExtensionBlock((begin), next, (length)))
                break;
        }
    }

    // Post-checks
    // Check if peer declared encryption
    if (!encrypted && m_config.CryptoSecret.len > 0)
    {
        if (m_config.bEnforcedEnc)
        {
            m_RejectReason = SRT_REJ_UNSECURE;
            LOGC(cnlog.Error,
                 log << "HS EXT: Agent declares encryption, but Peer does not - rejecting connection per "
                        "enforced encryption.");
            return false;
        }

        LOGC(cnlog.Warn,
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
        LOGC(cnlog.Error,
             log << "HS EXT: Agent uses '" << agsm << "' congctl, but peer DID NOT DECLARE congctl (assuming 'live').");
        return false;
    }

#if ENABLE_BONDING
    // m_GroupOf and locking info: NULL check won't hurt here. If the group
    // was deleted in the meantime, it will be found out later anyway and result with error.
    if (m_SrtHsSide == HSD_INITIATOR && m_parent->m_GroupOf)
    {
        // XXX Later probably needs to check if this group REQUIRES the group
        // response. Currently this implements the bonding-category group, and this
        // always requires that the listener respond with the group id, otherwise
        // it probably DID NOT UNDERSTAND THE GROUP, so the connection should be rejected.
        if (!have_group)
        {
            m_RejectReason = SRT_REJ_GROUP;
            LOGC(cnlog.Error, log << "HS EXT: agent is a group member, but the listener did not respond with group ID. Rejecting.");
            return false;
        }
    }
#endif

    // Ok, finished, for now.
    return true;
}

bool srt::CUDT::checkApplyFilterConfig(const std::string &confstr)
{
    SrtFilterConfig cfg;
    if (!ParseFilterConfig(confstr, (cfg)))
        return false;

    // Now extract the type, if present, and
    // check if you have this type of corrector available.
    if (!PacketFilter::correctConfig(cfg))
        return false;

    string thisconf = m_config.sPacketFilterConfig.str();

    // Now parse your own string, if you have it.
    if (thisconf != "")
    {
        // - for rendezvous, both must be exactly the same (it's unspecified, which will be the first one)
        if (m_config.bRendezvous && thisconf != confstr)
        {
            return false;
        }

        SrtFilterConfig mycfg;
        if (!ParseFilterConfig(thisconf, (mycfg)))
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
            if (!CheckFilterCompat((mycfg), cfg))
                return false;
        }

        HLOGC(cnlog.Debug,
              log << "checkApplyFilterConfig: param: LOCAL: " << Printable(mycfg.parameters)
                  << " FORGN: " << Printable(cfg.parameters));

        ostringstream myos;
        myos << mycfg.type;
        for (map<string, string>::iterator x = mycfg.parameters.begin(); x != mycfg.parameters.end(); ++x)
        {
            myos << "," << x->first << ":" << x->second;
        }

        m_config.sPacketFilterConfig.set(myos.str());

        HLOGC(cnlog.Debug, log << "checkApplyFilterConfig: Effective config: " << thisconf);
    }
    else
    {
        // Take the foreign configuration as a good deal.
        HLOGC(cnlog.Debug, log << "checkApplyFilterConfig: Good deal config: " << thisconf);
        m_config.sPacketFilterConfig.set(confstr);
    }

    size_t efc_max_payload_size = SRT_LIVE_MAX_PLSIZE - cfg.extra_size;
    if (m_config.zExpPayloadSize > efc_max_payload_size)
    {
        LOGC(cnlog.Warn,
             log << "Due to filter-required extra " << cfg.extra_size << " bytes, SRTO_PAYLOADSIZE fixed to "
                 << efc_max_payload_size << " bytes");
        m_config.zExpPayloadSize = efc_max_payload_size;
    }

    return true;
}

#if ENABLE_BONDING
bool srt::CUDT::interpretGroup(const int32_t groupdata[], size_t data_size SRT_ATR_UNUSED, int hsreq_type_cmd SRT_ATR_UNUSED)
{
    // `data_size` isn't checked because we believe it's checked earlier.
    // Also this code doesn't predict to get any other format than the official one,
    // so there are only data in two fields. Passing this argument is only left
    // for consistency and possibly changes in future.

    // We are granted these two fields do exist
    SRTSOCKET grpid = groupdata[GRPD_GROUPID];
    uint32_t gd = groupdata[GRPD_GROUPDATA];

    SRT_GROUP_TYPE gtp = SRT_GROUP_TYPE(SrtHSRequest::HS_GROUP_TYPE::unwrap(gd));
    int link_weight = SrtHSRequest::HS_GROUP_WEIGHT::unwrap(gd);
    uint32_t link_flags = SrtHSRequest::HS_GROUP_FLAGS::unwrap(gd);

    if (m_config.iGroupConnect == 0)
    {
        m_RejectReason = SRT_REJ_GROUP;
        LOGC(cnlog.Error, log << "HS/GROUP: this socket is not allowed for group connect.");
        return false;
    }

    // This is called when the group type has come in the handshake is invalid.
    if (gtp >= SRT_GTYPE_E_END)
    {
        m_RejectReason = SRT_REJ_GROUP;
        LOGC(cnlog.Error, log << "HS/GROUP: incorrect group type value " << gtp << " (max is " << SRT_GTYPE_E_END << ")");
        return false;
    }

    if ((grpid & SRTGROUP_MASK) == 0)
    {
        m_RejectReason = SRT_REJ_ROGUE;
        LOGC(cnlog.Error, log << "HS/GROUP: socket ID passed as a group ID is not a group ID");
        return false;
    }

    // We have the group, now take appropriate action.
    // The redundancy group requires to make a mirror group
    // on this side, and the newly created socket should
    // be made belong to it.

#if ENABLE_HEAVY_LOGGING
    static const char* hs_side_name[] = {"draw", "initiator", "responder"};
    HLOGC(cnlog.Debug, log << "interpretGroup: STATE: HsSide=" << hs_side_name[m_SrtHsSide] << " HS MSG: " << MessageTypeStr(UMSG_EXT, hsreq_type_cmd)
            << " $" << grpid << " type=" << gtp << " weight=" << link_weight << " flags=0x" << std::hex << link_flags);
#endif

    // XXX Here are two separate possibilities:
    //
    // 1. This is a HS request and this is a newly created socket not yet part of any group.
    // 2. This is a HS response and the group is the mirror group for the group to which the agent belongs; we need to pin the mirror group as peer group
    //
    // These two situations can be only distinguished by the HS side.
    if (m_SrtHsSide == HSD_DRAW)
    {
        m_RejectReason = SRT_REJ_IPE;
        LOGC(cnlog.Error, log << "IPE: interpretGroup: The HS side should have been already decided; it's still DRAW. Grouping rejected.");
        return false;
    }

    ScopedLock guard_group_existence (uglobal().m_GlobControlLock);

    if (m_SrtHsSide == HSD_INITIATOR)
    {
        // This is a connection initiator that has requested the peer to make a
        // mirror group and join it, then respond its mirror group id. The
        // `grpid` variable contains this group ID; map this as your peer
        // group. If your group already has a peer group set, check if this is
        // the same id, otherwise the connection should be rejected.

        // So, first check the group of the current socket and see if a peer is set.
        CUDTGroup* pg = m_parent->m_GroupOf;
        if (!pg)
        {
            // This means that the responder has responded with a group membership,
            // but the initiator did not request any group membership presence.
            // Currently impossible situation.
            m_RejectReason = SRT_REJ_IPE;
            LOGC(cnlog.Error, log << "IPE: HS/RSP: group membership responded, while not requested.");
            return false;
        }

        // Group existence is guarded, so we can now lock the group as well.
        ScopedLock gl(*pg->exp_groupLock());

        // Now we know the group exists, but it might still be closed
        if (pg->closing())
        {
            LOGC(cnlog.Error, log << "HS/RSP: group was closed in the process, can't continue connecting");
            m_RejectReason = SRT_REJ_IPE;
            return false;
        }

        SRTSOCKET peer = pg->peerid();
        if (peer == -1)
        {
            // This is the first connection within this group, so this group
            // has just been informed about the peer membership. Accept it.
            pg->set_peerid(grpid);
            HLOGC(cnlog.Debug, log << "HS/RSP: group $" << pg->id() << " -> peer $" << pg->peerid() << ", copying characteristic data");

            // The call to syncWithSocket is copying
            // some interesting data from the first connected
            // socket. This should be only done for the first successful connection.
            pg->syncWithSocket(*this, HSD_INITIATOR);
        }
        // Otherwise the peer id must be the same as existing, otherwise
        // this group is considered already bound to another peer group.
        // (Note that the peer group is peer-specific, and peer id numbers
        // may repeat among sockets connected to groups established on
        // different peers).
        else if (peer != grpid)
        {
            LOGC(cnlog.Error, log << "IPE: HS/RSP: group membership responded for peer $" << grpid
                    << " but the current socket's group $" << pg->id() << " has already a peer $" << peer);
            m_RejectReason = SRT_REJ_GROUP;
            return false;
        }
        else
        {
            HLOGC(cnlog.Debug, log << "HS/RSP: group $" << pg->id() << " ALREADY MAPPED to peer mirror $" << pg->peerid());
        }
    }
    else
    {
        // This is a connection responder that has been requested to make a
        // mirror group and join it. Later on, the HS response will be sent
        // and its group ID will be added to the HS extensions as mirror group
        // ID to the peer.

        SRTSOCKET lgid = makeMePeerOf(grpid, gtp, link_flags);
        if (!lgid)
            return true; // already done

        if (lgid == -1)
        {
            // NOTE: This error currently isn't reported by makeMePeerOf,
            // so this is left to handle a possible error introduced in future.
            m_RejectReason = SRT_REJ_GROUP;
            return false; // error occurred
        }

        if (!m_parent->m_GroupOf)
        {
            // Strange, we just added it...
            m_RejectReason = SRT_REJ_IPE;
            LOGC(cnlog.Fatal, log << "IPE: socket not in group after adding to it");
            return false;
        }

        groups::SocketData* f = m_parent->m_GroupMemberData;

        f->weight = link_weight;
        f->agent = m_parent->m_SelfAddr;
        f->peer = m_PeerAddr;
    }

    m_parent->m_GroupOf->debugGroup();

    // That's all. For specific things concerning group
    // types, this will be later.
    return true;
}
#endif

#if ENABLE_BONDING
// NOTE: This function is called only in one place and it's done
// exclusively on the listener side (HSD_RESPONDER, HSv5+).

// [[using locked(s_UDTUnited.m_GlobControlLock)]]
SRTSOCKET srt::CUDT::makeMePeerOf(SRTSOCKET peergroup, SRT_GROUP_TYPE gtp, uint32_t link_flags)
{
    // Note: This function will lock pg->m_GroupLock!

    CUDTSocket* s = m_parent;

    // Note that the socket being worked out here is about to be returned
    // from `srt_accept` call, and until this moment it will be inaccessible
    // for any other thread. It is then assumed that no other thread is accessing
    // it right now so there's no need to lock s->m_ControlLock.

    // Check if there exists a group that this one is a peer of.
    CUDTGroup* gp = uglobal().findPeerGroup_LOCKED(peergroup);
    bool was_empty = true;
    if (gp)
    {
        if (gp->type() != gtp)
        {
            LOGC(gmlog.Error, log << "HS: GROUP TYPE COLLISION: peer group=$" << peergroup << " type " << gtp
                << " agent group=$" << gp->id() << " type" << gp->type());
            return -1;
        }

        HLOGC(gmlog.Debug, log << "makeMePeerOf: group for peer=$" << peergroup << " found: $" << gp->id());

        if (!gp->groupEmpty())
            was_empty = false;
    }
    else
    {
        try
        {
            gp = &newGroup(gtp);
        }
        catch (...)
        {
            // Expected exceptions are only those referring to system resources
            return -1;
        }

        if (!gp->applyFlags(link_flags, m_SrtHsSide))
        {
            // Wrong settings. Must reject. Delete group.
            uglobal().deleteGroup_LOCKED(gp);
            return -1;
        }

        gp->set_peerid(peergroup);
        gp->deriveSettings(this);

        // This can only happen on a listener (it's only called on a site that is
        // HSD_RESPONDER), so it was a response for a groupwise connection.
        // Therefore such a group shall always be considered opened.
        gp->setOpen();

        HLOGC(gmlog.Debug, log << "makeMePeerOf: no group has peer=$" << peergroup << " - creating new mirror group $" << gp->id());
    }


    {
        ScopedLock glock (*gp->exp_groupLock());
        if (gp->closing())
        {
            HLOGC(gmlog.Debug, log << CONID() << "makeMePeerOf: group $" << gp->id() << " is being closed, can't process");
        }

        if (was_empty)
        {
            gp->syncWithSocket(s->core(), HSD_RESPONDER);
        }
    }

    // Setting non-blocking reading for group socket.
    s->core().m_config.bSynRecving = false;
    s->core().m_config.bSynSending = false;

    // Copy of addSocketToGroup. No idea how many parts could be common, not much.

    // Check if the socket already is in the group
    groups::SocketData* f;
    if (gp->contains(m_SocketID, (f)))
    {
        // XXX This is internal error. Report it, but continue
        // (A newly created socket from acceptAndRespond should not have any group membership yet)
        LOGC(gmlog.Error, log << "IPE (non-fatal): the socket is in the group, but has no clue about it!");
        s->m_GroupOf = gp;
        s->m_GroupMemberData = f;
        return 0;
    }

    s->m_GroupMemberData = gp->add(groups::prepareSocketData(s));
    s->m_GroupOf = gp;
    m_HSGroupType = gtp;

    // Record the remote address in the group data.

    return gp->id();
}

void srt::CUDT::synchronizeWithGroup(CUDTGroup* gp)
{
    ScopedLock gl (*gp->exp_groupLock());

    // We have blocked here the process of connecting a new
    // socket and adding anything new to the group, so no such
    // thing may happen in the meantime.
    steady_clock::time_point start_time, peer_start_time;

    start_time = m_stats.tsStartTime;
    peer_start_time = m_tsRcvPeerStartTime;

    if (!gp->applyGroupTime((start_time), (peer_start_time)))
    {
        HLOGC(gmlog.Debug, log << "synchronizeWithGroup: @" << m_SocketID
                << " DERIVED: ST="
                << FormatTime(m_stats.tsStartTime) << " -> "
                << FormatTime(start_time) << " PST="
                << FormatTime(m_tsRcvPeerStartTime) << " -> "
                << FormatTime(peer_start_time));
        m_stats.tsStartTime = start_time;
        m_tsRcvPeerStartTime = peer_start_time;
    }
    else
    {
        // This was the first connected socket and it defined start time.
        HLOGC(gmlog.Debug, log << "synchronizeWithGroup: @" << m_SocketID
                << " DEFINED: ST="
                << FormatTime(m_stats.tsStartTime)
                << " PST=" << FormatTime(m_tsRcvPeerStartTime));
    }

    steady_clock::time_point rcv_buffer_time_base;
    bool rcv_buffer_wrap_period = false;
    steady_clock::duration rcv_buffer_udrift(0);
    if (m_bTsbPd && gp->getBufferTimeBase(this, (rcv_buffer_time_base), (rcv_buffer_wrap_period), (rcv_buffer_udrift)))
    {
        // We have at least one socket in the group, each socket should have
        // the value of the timebase set exactly THE SAME.

        // In case when we have the following situation:

        // - the existing link is before [LAST30] (so wrap period is off)
        // - the new link gets the timestamp from [LAST30] range
        // --> this will be recognized as entering the wrap period, next
        //     timebase will get added a segment to this value
        //
        // The only dangerous situations could be when one link gets
        // timestamps from the [FOLLOWING30] and the other in [FIRST30],
        // but between them there's a 30s distance, considered large enough
        // time to not fill a network window.
        enterCS(m_RecvLock);
        m_pRcvBuffer->applyGroupTime(rcv_buffer_time_base, rcv_buffer_wrap_period, m_iTsbPdDelay_ms * 1000, rcv_buffer_udrift);
#if ENABLE_NEW_RCVBUFFER
        m_pRcvBuffer->setPeerRexmitFlag(m_bPeerRexmitFlag);
#endif
        leaveCS(m_RecvLock);

        HLOGF(gmlog.Debug,  "AFTER HS: Set Rcv TsbPd mode: delay=%u.%03us GROUP TIME BASE: %s%s",
                m_iTsbPdDelay_ms/1000,
                m_iTsbPdDelay_ms%1000,
                FormatTime(rcv_buffer_time_base).c_str(),
                rcv_buffer_wrap_period ? " (WRAP PERIOD)" : " (NOT WRAP PERIOD)");
    }
    else
    {
        HLOGC(gmlog.Debug, log << "AFTER HS: (GROUP, but " << (m_bTsbPd ? "FIRST SOCKET is initialized normally)" : "no TSBPD set)"));
        updateSrtRcvSettings();
    }

    // This function currently does nothing, just left for consistency
    // with updateAfterSrtHandshake().
    updateSrtSndSettings();

    if (gp->synconmsgno())
    {
        HLOGC(gmlog.Debug, log << "synchronizeWithGroup: @" << m_SocketID << ": NOT synchronizing sequence numbers.");
    }
    else
    {
        // These are the values that are normally set initially by setters.
        int32_t snd_isn = m_iSndLastAck, rcv_isn = m_iRcvLastAck;
        if (!gp->applyGroupSequences(m_SocketID, (snd_isn), (rcv_isn)))
        {
            HLOGC(gmlog.Debug, log << "synchronizeWithGroup: @" << m_SocketID
                    << " DERIVED ISN: RCV=%" << m_iRcvLastAck << " -> %" << rcv_isn
                    << " (shift by " << CSeqNo::seqcmp(rcv_isn, m_iRcvLastAck)
                    << ") SND=%" << m_iSndLastAck << " -> %" << snd_isn
                    << " (shift by " << CSeqNo::seqcmp(snd_isn, m_iSndLastAck) << ")");
            setInitialRcvSeq(rcv_isn);
            setInitialSndSeq(snd_isn);
        }
        else
        {
            HLOGC(gmlog.Debug, log << "synchronizeWithGroup: @" << m_SocketID
                    << " DEFINED ISN: RCV=%" << m_iRcvLastAck
                    << " SND=%" << m_iSndLastAck);
        }
    }
}
#endif

void srt::CUDT::startConnect(const sockaddr_any& serv_addr, int32_t forced_isn)
{
    ScopedLock cg (m_ConnectionLock);

    HLOGC(aclog.Debug, log << CONID() << "startConnect: -> " << serv_addr.str()
            << (m_config.bSynRecving ? " (SYNCHRONOUS)" : " (ASYNCHRONOUS)") << "...");

    if (!m_bOpened)
        throw CUDTException(MJ_NOTSUP, MN_NONE, 0);

    if (m_bListening)
        throw CUDTException(MJ_NOTSUP, MN_ISCONNECTED, 0);

    if (m_bConnecting || m_bConnected)
        throw CUDTException(MJ_NOTSUP, MN_ISCONNECTED, 0);

    // record peer/server address
    m_PeerAddr = serv_addr;

    // register this socket in the rendezvous queue
    // RendezevousQueue is used to temporarily store incoming handshake, non-rendezvous connections also require this
    // function
    steady_clock::duration ttl = m_config.tdConnTimeOut;

    if (m_config.bRendezvous)
        ttl *= 10;

    const steady_clock::time_point ttl_time = steady_clock::now() + ttl;
    m_pRcvQueue->registerConnector(m_SocketID, this, serv_addr, ttl_time);

    // The m_iType is used in the INDUCTION for nothing. This value is only regarded
    // in CONCLUSION handshake, however this must be created after the handshake version
    // is already known. UDT_DGRAM is the value that was the only valid in the old SRT
    // with HSv4 (it supported only live transmission), for HSv5 it will be changed to
    // handle handshake extension flags.
    m_ConnReq.m_iType = UDT_DGRAM;

    // This is my current configuration
    if (m_config.bRendezvous)
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
        m_ConnReq.m_iType           = SrtHSRequest::wrapFlags(false /* no MAGIC here */, m_config.iSndCryptoKeyLen);
        bool whether SRT_ATR_UNUSED = m_config.iSndCryptoKeyLen != 0;
        HLOGC(aclog.Debug,
              log << "startConnect (rnd): " << (whether ? "" : "NOT ")
                  << " Advertising PBKEYLEN - value = " << m_config.iSndCryptoKeyLen);
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

    m_ConnReq.m_iMSS            = m_config.iMSS;
    // Defined as the size of the receiver buffer in packets, unless
    // SRTO_FC has been set to a less value.
    m_ConnReq.m_iFlightFlagSize = m_config.flightCapacity();
    m_ConnReq.m_iID             = m_SocketID;
    CIPAddress::ntop(serv_addr, (m_ConnReq.m_piPeerIP));

    if (forced_isn == SRT_SEQNO_NONE)
    {
        forced_isn = generateISN();
        HLOGC(aclog.Debug, log << "startConnect: ISN generated = " << forced_isn);
    }
    else
    {
        HLOGC(aclog.Debug, log << "startConnect: ISN forced = " << forced_isn);
    }

    m_iISN = m_ConnReq.m_iISN = forced_isn;

    setInitialSndSeq(m_iISN);
    m_SndLastAck2Time = steady_clock::now();

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
    m_ConnReq.store_to((reqpkt.m_pcData), (hs_size));

    // Note that CPacket::allocate() sets also the size
    // to the size of the allocated buffer, which not
    // necessarily is to be the size of the data.
    reqpkt.setLength(hs_size);

    steady_clock::time_point now = steady_clock::now();
    setPacketTS(reqpkt, now);

    HLOGC(cnlog.Debug,
          log << CONID() << "CUDT::startConnect: REQ-TIME set HIGH (TimeStamp: " << reqpkt.m_iTimeStamp << "). SENDING HS: " << m_ConnReq.show());

    /*
     * Race condition if non-block connect response thread scheduled before we set m_bConnecting to true?
     * Connect response will be ignored and connecting will wait until timeout.
     * Maybe m_ConnectionLock handling problem? Not used in CUDT::connect(const CPacket& response)
     */
    m_tsLastReqTime = now;
    m_bConnecting = true;
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

    //////////////////////////////////////////////////////
    // SYNCHRO BAR
    //////////////////////////////////////////////////////
    if (!m_config.bSynRecving)
    {
        HLOGC(cnlog.Debug, log << CONID() << "startConnect: ASYNC MODE DETECTED. Deferring the process to RcvQ:worker");
        return;
    }

    // Below this bar, rest of function maintains only and exclusively
    // the SYNCHRONOUS (blocking) connection process. 

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
        const steady_clock::duration tdiff = steady_clock::now() - m_tsLastReqTime.load();
        // avoid sending too many requests, at most 1 request per 250ms

        // SHORT VERSION:
        // The immediate first run of this loop WILL SKIP THIS PART, so
        // the processing really begins AFTER THIS CONDITION.
        //
        // Note that some procedures inside may set m_tsLastReqTime to 0,
        // which will result of this condition to trigger immediately in
        // the next iteration.
        if (count_milliseconds(tdiff) > 250)
        {
            HLOGC(cnlog.Debug,
                  log << "startConnect: LOOP: time to send (" << count_milliseconds(tdiff) << " > 250 ms). size=" << reqpkt.getLength());

            if (m_config.bRendezvous)
                reqpkt.m_iID = m_ConnRes.m_iID;

            now = steady_clock::now();
#if ENABLE_HEAVY_LOGGING
            {
                CHandShake debughs;
                debughs.load_from(reqpkt.m_pcData, reqpkt.getLength());
                HLOGC(cnlog.Debug,
                      log << CONID() << "startConnect: REQ-TIME HIGH."
                          << " cont/sending HS to peer: " << debughs.show());
            }
#endif

            m_tsLastReqTime       = now;
            setPacketTS(reqpkt, now);
            m_pSndQueue->sendto(serv_addr, reqpkt);
        }
        else
        {
            HLOGC(cnlog.Debug, log << "startConnect: LOOP: too early to send - " << count_milliseconds(tdiff) << " < 250ms");
        }

        cst = CONN_CONTINUE;
        response.setLength(m_iMaxSRTPayloadSize);
        if (m_pRcvQueue->recvfrom(m_SocketID, (response)) > 0)
        {
            HLOGC(cnlog.Debug, log << CONID() << "startConnect: got response for connect request");
            cst = processConnectResponse(response, &e);

            HLOGC(cnlog.Debug, log << CONID() << "startConnect: response processing result: " << ConnectStatusStr(cst));

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
                cst = processRendezvous(&response, serv_addr, RST_OK, (reqpkt));
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
            HLOGC(cnlog.Debug,
                  log << "startConnect: SYNC CONNECTION STATUS:" << ConnectStatusStr(cst) << ", REQ-TIME: LOW.");
            m_tsLastReqTime = steady_clock::time_point();

            // Now serialize the handshake again to the existing buffer so that it's
            // then sent later in this loop.

            // First, set the size back to the original size, m_iMaxSRTPayloadSize because
            // this is the size of the originally allocated space. It might have been
            // shrunk by serializing the INDUCTION handshake (which was required before
            // sending this packet to the output queue) and therefore be too
            // small to store the CONCLUSION handshake (with HSv5 extensions).
            reqpkt.setLength(m_iMaxSRTPayloadSize);

            HLOGC(cnlog.Debug, log << "startConnect: creating HS CONCLUSION: buffer size=" << reqpkt.getLength());

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
            if (!createSrtHandshake(SRT_CMD_HSREQ, SRT_CMD_KMREQ, 0, 0, (reqpkt), (m_ConnReq)))
            {
                LOGC(cnlog.Warn, log << "createSrtHandshake failed - REJECTING.");
                cst = CONN_REJECT;
                break;
            }
            // These last 2 parameters designate the buffer, which is in use only for SRT_CMD_KMRSP.
            // If m_ConnReq.m_iVersion == HS_VERSION_UDT4, this function will do nothing,
            // except just serializing the UDT handshake.
            // The trick is that the HS challenge is with version HS_VERSION_UDT4, but the
            // listener should respond with HS_VERSION_SRT1, if it is HSv5 capable.
        }

        HLOGC(cnlog.Debug,
              log << "startConnect: timeout from Q:recvfrom, looping again; cst=" << ConnectStatusStr(cst));

#if ENABLE_HEAVY_LOGGING
        // Non-fatal assertion
        if (cst == CONN_REJECT) // Might be returned by processRendezvous
        {
            LOGC(cnlog.Error,
                 log << "startConnect: IPE: cst=REJECT NOT EXPECTED HERE, the loop should've been interrupted!");
            break;
        }
#endif

        if (steady_clock::now() > ttl_time)
        {
            // timeout
            e = CUDTException(MJ_SETUP, MN_TIMEOUT, 0);
            m_RejectReason = SRT_REJ_TIMEOUT;
            HLOGC(cnlog.Debug, log << "startConnect: TTL time " << FormatTime(ttl_time) << " exceeded, TIMEOUT.");
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
            e = CUDTException(MJ_SETUP, MN_CLOSED, 0);
        else if (m_ConnRes.m_iReqType > URQ_FAILURE_TYPES) // connection request rejected
        {
            m_RejectReason = RejectReasonForURQ(m_ConnRes.m_iReqType);
            e              = CUDTException(MJ_SETUP, MN_REJECTED, 0);
        }
        else if ((!m_config.bRendezvous) && (m_ConnRes.m_iISN != m_iISN)) // secuity check
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

    HLOGC(cnlog.Debug,
          log << CONID() << "startConnect: handshake exchange succeeded.");

    // Parameters at the end.
    HLOGC(cnlog.Debug,
          log << "startConnect: END. Parameters:"
                 " mss="
              << m_config.iMSS << " max-cwnd-size=" << m_CongCtl->cgWindowMaxSize()
              << " cwnd-size=" << m_CongCtl->cgWindowSize() << " rtt=" << m_iSRTT << " bw=" << m_iBandwidth);
}

// Asynchronous connection
EConnectStatus srt::CUDT::processAsyncConnectResponse(const CPacket &pkt) ATR_NOEXCEPT
{
    EConnectStatus cst = CONN_CONTINUE;
    CUDTException  e;

    ScopedLock cg(m_ConnectionLock);
    HLOGC(cnlog.Debug, log << CONID() << "processAsyncConnectResponse: got response for connect request, processing");
    cst = processConnectResponse(pkt, &e);

    HLOGC(cnlog.Debug,
          log << CONID() << "processAsyncConnectResponse: response processing result: " << ConnectStatusStr(cst)
              << "; REQ-TIME LOW to enforce immediate response");
    m_tsLastReqTime = steady_clock::time_point();

    return cst;
}

bool srt::CUDT::processAsyncConnectRequest(EReadStatus         rst,
                                      EConnectStatus      cst,
                                      const CPacket*      pResponse /*[[nullable]]*/,
                                      const sockaddr_any& serv_addr)
{
    // IMPORTANT!

    // This function is called, still asynchronously, but in the order
    // of call just after the call to the above processAsyncConnectResponse.
    // This should have got the original value returned from
    // processConnectResponse through processAsyncConnectResponse.

    CPacket request;
    request.setControl(UMSG_HANDSHAKE);
    request.allocate(m_iMaxSRTPayloadSize);
    const steady_clock::time_point now = steady_clock::now();
    setPacketTS(request, now);

    HLOGC(cnlog.Debug,
          log << "processAsyncConnectRequest: REQ-TIME: HIGH. Should prevent too quick responses.");
    m_tsLastReqTime = now;
    // ID = 0, connection request
    request.m_iID = !m_config.bRendezvous ? 0 : m_ConnRes.m_iID;

    bool status = true;

    ScopedLock cg(m_ConnectionLock);
    if (!m_bOpened) // Check the socket has not been closed before already.
        return false;

    if (cst == CONN_RENDEZVOUS)
    {
        HLOGC(cnlog.Debug, log << "processAsyncConnectRequest: passing to processRendezvous");
        cst = processRendezvous(pResponse, serv_addr, rst, (request));
        if (cst == CONN_ACCEPT)
        {
            HLOGC(cnlog.Debug,
                  log << "processAsyncConnectRequest: processRendezvous completed the process and responded by itself. "
                         "Done.");
            return true;
        }

        if (cst != CONN_CONTINUE)
        {
            // processRendezvous already set the reject reason
            LOGC(cnlog.Warn,
                 log << "processAsyncConnectRequest: REJECT reported from processRendezvous, not processing further.");
            status = false;
        }
    }
    else if (cst == CONN_REJECT)
    {
        // m_RejectReason already set at worker_ProcessAddressedPacket.
        LOGC(cnlog.Warn,
             log << "processAsyncConnectRequest: REJECT reported from HS processing: "
             << srt_rejectreason_str(m_RejectReason)
             << " - not processing further");
        // m_tsLastReqTime = steady_clock::time_point(); XXX ?
        return false;
    }
    else
    {
        // (this procedure will be also run for HSv4 rendezvous)
        HLOGC(cnlog.Debug, log << "processAsyncConnectRequest: serializing HS: buffer size=" << request.getLength());
        if (!createSrtHandshake(SRT_CMD_HSREQ, SRT_CMD_KMREQ, 0, 0, (request), (m_ConnReq)))
        {
            // All 'false' returns from here are IPE-type, mostly "invalid argument" plus "all keys expired".
            LOGC(cnlog.Error, log << "IPE: processAsyncConnectRequest: createSrtHandshake failed, dismissing.");
            status = false;
        }
        else
        {
            HLOGC(cnlog.Debug,
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
        zhs.store_to((request.m_pcData), (size));
        request.setLength(size);
        */
    }

    HLOGC(cnlog.Debug, log << "processAsyncConnectRequest: setting REQ-TIME HIGH, SENDING HS:" << m_ConnReq.show());
    m_tsLastReqTime = steady_clock::now();
    m_pSndQueue->sendto(serv_addr, request);
    return status;
}

void srt::CUDT::cookieContest()
{
    if (m_SrtHsSide != HSD_DRAW)
        return;

    LOGC(cnlog.Debug, log << "cookieContest: agent=" << m_ConnReq.m_iCookie << " peer=" << m_ConnRes.m_iCookie);

    // Here m_ConnReq.m_iCookie is a local cookie value sent in connection request to the peer.
    // m_ConnRes.m_iCookie is a cookie value sent by the peer in its connection request.
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
    // 
    // In SRT v1.4.3 and prior the below subtraction was performed in 32-bit arithmetic.
    // The result of subtraction can overflow 32-bits. 
    // Example
    // m_ConnReq.m_iCookie = -1480577720;
    // m_ConnRes.m_iCookie = 811599203;
    // int64_t llBetterCookie = -1480577720 - 811599203 = -2292176923 (FFFF FFFF 7760 27E5);
    // int32_t iBetterCookie  = 2002790373 (7760 27E5);
    // 
    // Now 64-bit arithmetic is used to calculate the actual result of subtraction.
    // The 31-st bit is then checked to check if the resulting is negative in 32-bit aritmetics.
    // This way the old contest behavior is preserved, and potential compiler optimisations are avoided.
    const int64_t contest = int64_t(m_ConnReq.m_iCookie) - int64_t(m_ConnRes.m_iCookie);

    if ((contest & 0xFFFFFFFF) == 0)
    {
        // DRAW! The only way to continue would be to force the
        // cookies to be regenerated and to start over. But it's
        // not worth a shot - this is an extremely rare case.
        // This can simply do reject so that it can be started again.

        // Pretend then that the cookie contest wasn't done so that
        // it's done again. Cookies are baked every time anew, however
        // the successful initial contest remains valid no matter how
        // cookies will change.

        m_SrtHsSide = HSD_DRAW;
        return;
    }

    if (contest & 0x80000000)
    {
        m_SrtHsSide = HSD_RESPONDER;
        return;
    }

    m_SrtHsSide = HSD_INITIATOR;
}

// This function should complete the data for KMX needed for an out-of-band
// handshake response. Possibilities are:
// - There's no KMX (including first responder's handshake in rendezvous). This writes 0 to w_kmdatasize.
// - The encryption status is failure. Respond with fail code and w_kmdatasize = 1.
// - The last KMX was successful. Respond with the original kmdata and their size in w_kmdatasize.
EConnectStatus srt::CUDT::craftKmResponse(uint32_t* aw_kmdata, size_t& w_kmdatasize)
{
    // If the last CONCLUSION message didn't contain the KMX extension, there's
    // no key recorded yet, so it can't be extracted. Mark this w_kmdatasize empty though.
    int hs_flags = SrtHSRequest::SRT_HSTYPE_HSFLAGS::unwrap(m_ConnRes.m_iType);
    if (IsSet(hs_flags, CHandShake::HS_EXT_KMREQ))
    {
        // m_pCryptoControl can be NULL if the socket has been closed already. See issue #2231.
        if (!m_pCryptoControl)
        {
            m_RejectReason = SRT_REJ_IPE;
            LOGC(cnlog.Error, log << "IPE: craftKmResponse needs to send KM, but CryptoControl does not exist."
                << " Socket state: connected=" << boolalpha << m_bConnected << ", connecting=" << m_bConnecting
                << ", broken=" << m_bBroken << ", opened " << m_bOpened << ", closing=" << m_bClosing << ".");
            return CONN_REJECT;
        }
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
                    HLOGC(cnlog.Debug,
                            log << "craftKmResponse: No KMX recorded, status = "
                            << KmStateStr(m_pCryptoControl->m_RcvKmState) << ". Respond it.");

                    // Just do the same thing as in CCryptoControl::processSrtMsg_KMREQ for that case,
                    // that is, copy the NOSECRET code into KMX message.
                    memcpy((aw_kmdata), &m_pCryptoControl->m_RcvKmState, sizeof(int32_t));
                    w_kmdatasize = 1;
                }
                break; // Treat as ACCEPT in general; might change to REJECT on enforced-encryption

            default:
                // Remaining values:
                // UNSECURED: should not fall here at all
                // SECURING: should not happen in HSv5
                // SECURED: should have received the recorded KMX correctly (getKmMsg_size(0) > 0)
                {
                    m_RejectReason = SRT_REJ_IPE;
                    // Remaining situations:
                    // - password only on this site: shouldn't be considered to be sent to a no-password site
                    LOGC(cnlog.Error,
                            log << "craftKmResponse: IPE: PERIODIC HS: NO KMREQ RECORDED KMSTATE: RCV="
                            << KmStateStr(m_pCryptoControl->m_RcvKmState)
                            << " SND=" << KmStateStr(m_pCryptoControl->m_SndKmState));
                    return CONN_REJECT;
                }
                break;
            }
        }
        else
        {
            w_kmdatasize = msgsize / 4;
            if (msgsize > w_kmdatasize * 4)
            {
                // Sanity check
                LOGC(cnlog.Error, log << "IPE: KMX data not aligned to 4 bytes! size=" << msgsize);
                memset((aw_kmdata + (w_kmdatasize * 4)), 0, msgsize - (w_kmdatasize * 4));
                ++w_kmdatasize;
            }

            HLOGC(cnlog.Debug,
                    log << "craftKmResponse: getting KM DATA from the fore-recorded KMX from KMREQ, size="
                    << w_kmdatasize);
            memcpy((aw_kmdata), m_pCryptoControl->getKmMsg_data(0), msgsize);
        }
    }
    else
    {
        HLOGC(cnlog.Debug, log << "craftKmResponse: no KMX flag - not extracting KM data for KMRSP");
        w_kmdatasize = 0;
    }

    return CONN_ACCEPT;
}

EConnectStatus srt::CUDT::processRendezvous(
    const CPacket* pResponse /*[[nullable]]*/, const sockaddr_any& serv_addr,
    EReadStatus rst, CPacket& w_reqpkt)
{
    if (m_RdvState == CHandShake::RDV_CONNECTED)
    {
        HLOGC(cnlog.Debug, log << "processRendezvous: already in CONNECTED state.");
        return CONN_ACCEPT;
    }

    uint32_t kmdata[SRTDATA_MAXSIZE];
    size_t   kmdatasize = SRTDATA_MAXSIZE;

    cookieContest();

    // We know that the other side was contacted and the other side has sent
    // the handshake message - we know then both cookies. If it's a draw, it's
    // a very rare case of creating identical cookies.
    if (m_SrtHsSide == HSD_DRAW)
    {
        m_RejectReason = SRT_REJ_RDVCOOKIE;
        LOGC(cnlog.Error,
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
    rendezvousSwitchState((rsp_type), (needs_extension), (needs_hsrsp));
    if (rsp_type > URQ_FAILURE_TYPES)
    {
        m_RejectReason = RejectReasonForURQ(rsp_type);
        HLOGC(cnlog.Debug,
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

    // This must be done before prepareConnectionObjects(), because it sets ISN and m_iMaxSRTPayloadSize needed to create buffers.
    if (!applyResponseSettings())
    {
        LOGC(cnlog.Error, log << "processRendezvous: rogue peer");
        return CONN_REJECT;
    }

    // The CryptoControl must be created by the prepareConnectionObjects() before interpreting and creating HSv5 extensions
    // because the it will be used there.
    if (!prepareConnectionObjects(m_ConnRes, m_SrtHsSide, NULL))
    {
        // m_RejectReason already handled
        HLOGC(cnlog.Debug, log << "processRendezvous: rejecting due to problems in prepareConnectionObjects.");
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
            m_tsLastReqTime = steady_clock::time_point();
            if (!pResponse || pResponse->getLength() == size_t(-1))
            {
                m_RejectReason = SRT_REJ_IPE;
                LOGC(cnlog.Fatal,
                     log << "IPE: rst=RST_OK, but the packet has set -1 length - REJECTING (REQ-TIME: LOW)");
                return CONN_REJECT;
            }

            if (!interpretSrtHandshake(m_ConnRes, *pResponse, kmdata, &kmdatasize))
            {
                HLOGC(cnlog.Debug,
                      log << "processRendezvous: rejecting due to problems in interpretSrtHandshake REQ-TIME: LOW.");
                return CONN_REJECT;
            }

            updateAfterSrtHandshake(HS_VERSION_SRT1);

            // Pass on, inform about the shortened response-waiting period.
            HLOGC(cnlog.Debug, log << "processRendezvous: setting REQ-TIME: LOW. Forced to respond immediately.");
        }
        else
        {
            // This is a repeated handshake, so you can't use the incoming data to
            // prepare data for createSrtHandshake. They have to be extracted from inside.
            EConnectStatus conn = craftKmResponse((kmdata), (kmdatasize));
            if (conn != CONN_ACCEPT)
                return conn;
        }

        // No matter the value of needs_extension, the extension is always needed
        // when HSREQ was interpreted (to store HSRSP extension).
        m_ConnReq.m_extension = true;

        HLOGC(cnlog.Debug,
              log << "processRendezvous: HSREQ extension ok, creating HSRSP response. kmdatasize=" << kmdatasize);

        w_reqpkt.setLength(m_iMaxSRTPayloadSize);
        if (!createSrtHandshake(SRT_CMD_HSRSP, SRT_CMD_KMRSP,
                    kmdata, kmdatasize,
                    (w_reqpkt), (m_ConnReq)))
        {
            HLOGC(cnlog.Debug,
                  log << "processRendezvous: rejecting due to problems in createSrtHandshake. REQ-TIME: LOW");
            m_tsLastReqTime = steady_clock::time_point();
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

        if (rst != RST_OK || !pResponse || pResponse->getLength() == size_t(-1))
        {
            // Actually the -1 length would be an IPE, but it's likely that this was reported already.
            HLOGC(
                cnlog.Debug,
                log << "processRendezvous: no INCOMING packet, NOT interpreting extensions (relying on exising data)");
        }
        else
        {
            HLOGC(cnlog.Debug,
                  log << "processRendezvous: INITIATOR, will send AGREEMENT - interpreting HSRSP extension");
            if (!interpretSrtHandshake(m_ConnRes, *pResponse, 0, 0))
            {
                // m_RejectReason is already set, so set the reqtype accordingly
                m_ConnReq.m_iReqType = URQFailure(m_RejectReason);
            }
        }
        // This should be false, make a kinda assert here.
        if (needs_extension)
        {
            LOGC(cnlog.Fatal, log << "IPE: INITIATOR responding AGREEMENT should declare no extensions to HS");
            m_ConnReq.m_extension = false;
        }
        updateAfterSrtHandshake(HS_VERSION_SRT1);
    }

    HLOGC(cnlog.Debug,
          log << CONID() << "processRendezvous: COOKIES Agent/Peer: " << m_ConnReq.m_iCookie << "/"
              << m_ConnRes.m_iCookie << " HSD:" << (m_SrtHsSide == HSD_INITIATOR ? "initiator" : "responder")
              << " STATE:" << CHandShake::RdvStateStr(m_RdvState) << " ...");

    if (rsp_type == URQ_DONE)
    {
        HLOGC(cnlog.Debug, log << "... WON'T SEND any response, both sides considered connected");
    }
    else
    {
        HLOGC(cnlog.Debug,
              log << "... WILL SEND " << RequestTypeStr(rsp_type) << " " << (m_ConnReq.m_extension ? "with" : "without")
                  << " SRT HS extensions");
    }

    // This marks the information for the serializer that
    // the SRT handshake extension is required.
    // Rest of the data will be filled together with
    // serialization.
    m_ConnReq.m_extension = needs_extension;

    w_reqpkt.setLength(m_iMaxSRTPayloadSize);
    if (m_RdvState == CHandShake::RDV_CONNECTED)
    {
        int cst = postConnect(pResponse, true, 0);
        if (cst == CONN_REJECT)
        {
            // m_RejectReason already set
            HLOGC(cnlog.Debug, log << "processRendezvous: rejecting due to problems in postConnect.");
            return CONN_REJECT;
        }
    }

    // URQ_DONE or URQ_AGREEMENT can be the result if the state is RDV_CONNECTED.
    // If URQ_DONE, then there's nothing to be done, when URQ_AGREEMENT then return
    // CONN_CONTINUE to make the caller send again the contents if the packet buffer,
    // this time with URQ_AGREEMENT message, but still consider yourself connected.
    if (rsp_type == URQ_DONE)
    {
        HLOGC(cnlog.Debug, log << "processRendezvous: rsp=DONE, reporting ACCEPT (nothing to respond)");
        return CONN_ACCEPT;
    }

    // createSrtHandshake moved here because if the above conditions are satisfied,
    // no response is going to be send, so nothing needs to be "created".

    // needs_extension here distinguishes between cases 1 and 3.
    // NOTE: in case when interpretSrtHandshake was run under the conditions above (to interpret HSRSP),
    // then createSrtHandshake below will create only empty AGREEMENT message.
    if (!createSrtHandshake(SRT_CMD_HSREQ, SRT_CMD_KMREQ, 0, 0,
                (w_reqpkt), (m_ConnReq)))
    {
        // m_RejectReason already set
        LOGC(cnlog.Warn, log << "createSrtHandshake failed (IPE?), connection rejected. REQ-TIME: LOW");
        m_tsLastReqTime = steady_clock::time_point();
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

        const steady_clock::time_point now = steady_clock::now();
        m_tsLastReqTime                    = now;
        setPacketTS(w_reqpkt, now);
        HLOGC(cnlog.Debug,
              log << "processRendezvous: rsp=AGREEMENT, reporting ACCEPT and sending just this one, REQ-TIME HIGH.");

        m_pSndQueue->sendto(serv_addr, w_reqpkt);

        return CONN_ACCEPT;
    }

    if (rst == RST_OK)
    {
        // the request time must be updated so that the next handshake can be sent out immediately
        HLOGC(cnlog.Debug,
              log << "processRendezvous: rsp=" << RequestTypeStr(m_ConnReq.m_iReqType)
                  << " REQ-TIME: LOW to send immediately, consider yourself conencted");
        m_tsLastReqTime = steady_clock::time_point();
    }
    else
    {
        HLOGC(cnlog.Debug, log << "processRendezvous: REQ-TIME: remains previous value, consider yourself connected");
    }
    return CONN_CONTINUE;
}

// [[using locked(m_ConnectionLock)]];
EConnectStatus srt::CUDT::processConnectResponse(const CPacket& response, CUDTException* eout) ATR_NOEXCEPT
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
    HLOGC(cnlog.Debug,
          log << "processConnectResponse: TYPE:"
              << (response.isControl() ? MessageTypeStr(response.getType(), response.getExtendedType())
                                       : string("DATA")));
    // ConnectStatus res = CONN_REJECT; // used later for status - must be declared here due to goto POST_CONNECT.

    // For HSv4, the data sender is INITIATOR, and the data receiver is RESPONDER,
    // regardless of the connecting side affiliation. This will be changed for HSv5.
    bool          bidirectional = false;
    HandshakeSide hsd           = m_config.bDataSender ? HSD_INITIATOR : HSD_RESPONDER;
    // (defined here due to 'goto' below).

    // SRT peer may send the SRT handshake private message (type 0x7fff) before a keep-alive.

    // This condition is checked when the current agent is trying to do connect() in rendezvous mode,
    // but the peer was faster to send a handshake packet earlier. This makes it continue with connecting
    // process if the peer is already behaving as if the connection was already established.

    // This value will check either the initial value, which is less than SRT1, or
    // the value previously loaded to m_ConnReq during the previous handshake response.
    // For the initial form this value should not be checked.
    bool hsv5 = m_ConnRes.m_iVersion >= HS_VERSION_SRT1;

    if (m_config.bRendezvous &&
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
        HLOGC(cnlog.Debug, log << CONID() << "processConnectResponse: already connected - pinning in");
        if (hsv5)
        {
            m_RdvState = CHandShake::RDV_CONNECTED;
        }

        return postConnect(&response, hsv5, eout);
    }

    if (!response.isControl(UMSG_HANDSHAKE))
    {
        m_RejectReason = SRT_REJ_ROGUE;
        if (!response.isControl())
        {
            LOGC(cnlog.Warn, log << CONID() << "processConnectResponse: received DATA while HANDSHAKE expected");
        }
        else
        {
            LOGC(cnlog.Error,
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
        LOGC(cnlog.Error,
             log << CONID()
                 << "processConnectResponse: HANDSHAKE data buffer too small - possible blueboxing. Rejecting.");
        return CONN_REJECT;
    }

    HLOGC(cnlog.Debug, log << CONID() << "processConnectResponse: HS RECEIVED: " << m_ConnRes.show());
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
        LOGC(cnlog.Fatal, log << "MSS size " << m_config.iMSS << "exceeds MTU size!");
        return CONN_REJECT;
    }

    // (see createCrypter() call below)
    //
    // The CCryptoControl attached object must be created early
    // because it will be required to create a conclusion handshake in HSv5
    //
    if (m_config.bRendezvous)
    {
        // SANITY CHECK: A rendezvous socket should reject any caller requests (it's not a listener)
        if (m_ConnRes.m_iReqType == URQ_INDUCTION)
        {
            m_RejectReason = SRT_REJ_ROGUE;
            LOGC(cnlog.Error,
                 log << CONID()
                     << "processConnectResponse: Rendezvous-point received INDUCTION handshake (expected WAVEAHAND). "
                        "Rejecting.");
            return CONN_REJECT;
        }

        // The procedure for version 5 is completely different and changes the states
        // differently, so the old code will still maintain HSv4 the old way.

        if (m_ConnRes.m_iVersion > HS_VERSION_UDT4)
        {
            HLOGC(cnlog.Debug, log << CONID() << "processConnectResponse: Rendezvous HSv5 DETECTED.");
            return CONN_RENDEZVOUS; // --> will continue in CUDT::processRendezvous().
        }

        HLOGC(cnlog.Debug, log << CONID() << "processConnectResponse: Rendsezvous HSv4 DETECTED.");
        // So, here it has either received URQ_WAVEAHAND handshake message (while it should be in URQ_WAVEAHAND itself)
        // or it has received URQ_CONCLUSION/URQ_AGREEMENT message while this box has already sent URQ_WAVEAHAND to the
        // peer, and DID NOT send the URQ_CONCLUSION yet.

        if (m_ConnReq.m_iReqType == URQ_WAVEAHAND || m_ConnRes.m_iReqType == URQ_WAVEAHAND)
        {
            HLOGC(cnlog.Debug,
                  log << CONID() << "processConnectResponse: REQ-TIME LOW. got HS RDV. Agent state:"
                      << RequestTypeStr(m_ConnReq.m_iReqType) << " Peer HS:" << m_ConnRes.show());

            // Here we could have received WAVEAHAND or CONCLUSION.
            // For HSv4 simply switch to CONCLUSION for the sake of further handshake rolling.
            // For HSv5, make the cookie contest and basing on this decide, which party
            // should provide the HSREQ/KMREQ attachment.

           if (!createCrypter(hsd, false /* unidirectional */))
           {
               m_RejectReason = SRT_REJ_RESOURCE;
               m_ConnReq.m_iReqType = URQFailure(SRT_REJ_RESOURCE);
               // the request time must be updated so that the next handshake can be sent out immediately.
               m_tsLastReqTime = steady_clock::time_point();
               return CONN_REJECT;
           }

            m_ConnReq.m_iReqType = URQ_CONCLUSION;
            // the request time must be updated so that the next handshake can be sent out immediately.
            m_tsLastReqTime = steady_clock::time_point();
            return CONN_CONTINUE;
        }
        else
        {
            HLOGC(cnlog.Debug, log << CONID() << "processConnectResponse: Rendezvous HSv4 PAST waveahand");
        }
    }
    else
    {
        // set cookie
        if (m_ConnRes.m_iReqType == URQ_INDUCTION)
        {
            HLOGC(cnlog.Debug,
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
                    LOGC(cnlog.Warn, log << "processConnectResponse: Listener HSv5 did not set the SRT_MAGIC_CODE");
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
                // The m_config.bDataSender value should be completely ignored and the
                // connection is always bidirectional.
                bidirectional = true;
                hsd           = HSD_INITIATOR;
                m_SrtHsSide   = hsd;
            }

            m_tsLastReqTime = steady_clock::time_point();
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

    return postConnect(&response, false, eout);
}

bool srt::CUDT::applyResponseSettings() ATR_NOEXCEPT
{
    if (!m_ConnRes.valid())
    {
        LOGC(cnlog.Error, log << "applyResponseSettings: ROGUE HANDSHAKE - rejecting");
        m_RejectReason = SRT_REJ_ROGUE;
        return false;
    }

    // Re-configure according to the negotiated values.
    m_config.iMSS        = m_ConnRes.m_iMSS;
    m_iFlowWindowSize    = m_ConnRes.m_iFlightFlagSize;
    const int udpsize    = m_config.iMSS - CPacket::UDP_HDR_SIZE;
    m_iMaxSRTPayloadSize = udpsize - CPacket::HDR_SIZE;
    m_iPeerISN           = m_ConnRes.m_iISN;

    setInitialRcvSeq(m_iPeerISN);

    m_iRcvCurrPhySeqNo = CSeqNo::decseq(m_ConnRes.m_iISN);
    m_PeerID           = m_ConnRes.m_iID;
    memcpy((m_piSelfIP), m_ConnRes.m_piPeerIP, sizeof m_piSelfIP);

    HLOGC(cnlog.Debug,
          log << CONID() << "applyResponseSettings: HANSHAKE CONCLUDED. SETTING: payload-size=" << m_iMaxSRTPayloadSize
              << " mss=" << m_ConnRes.m_iMSS << " flw=" << m_ConnRes.m_iFlightFlagSize << " isn=" << m_ConnRes.m_iISN
              << " peerID=" << m_ConnRes.m_iID);

    return true;
}

EConnectStatus srt::CUDT::postConnect(const CPacket* pResponse, bool rendezvous, CUDTException *eout) ATR_NOEXCEPT
{
    if (m_ConnRes.m_iVersion < HS_VERSION_SRT1)
        m_tsRcvPeerStartTime = steady_clock::time_point(); // will be set correctly in SRT HS.

    // This procedure isn't being executed in rendezvous because
    // in rendezvous it's completed before calling this function.
    if (!rendezvous)
    {
        // The "local storage depleted" case shouldn't happen here, but
        // this is a theoretical path that needs prevention.
        bool ok = pResponse;
        if (!ok)
        {
            m_RejectReason = SRT_REJ_IPE;
            if (eout)
            {
                *eout = CUDTException(MJ_SETUP, MN_REJECTED, 0);
            }
            return CONN_REJECT;
        }

        // [[assert (pResponse != NULL)]];

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
        ok = applyResponseSettings();

        // This will actually be done also in rendezvous HSv4,
        // however in this case the HSREQ extension will not be attached,
        // so it will simply go the "old way".
        // (&&: skip if failed already)
        // Must be called before interpretSrtHandshake() to create the CryptoControl.
        ok = ok &&  prepareConnectionObjects(m_ConnRes, m_SrtHsSide, eout);

        // May happen that 'response' contains a data packet that was sent in rendezvous mode.
        // In this situation the interpretation of handshake was already done earlier.
        ok = ok && pResponse->isControl();
        ok = ok && interpretSrtHandshake(m_ConnRes, *pResponse, 0, 0);

        if (!ok)
        {
            if (eout)
            {
                *eout = CUDTException(MJ_SETUP, MN_REJECTED, 0);
            }
            // m_RejectReason already set
            return CONN_REJECT;
        }
    }

    bool have_group = false;

    {
#if ENABLE_BONDING
        ScopedLock cl (uglobal().m_GlobControlLock);
        CUDTGroup* g = m_parent->m_GroupOf;
        if (g)
        {
            // This is the last moment when this can be done.
            // The updateAfterSrtHandshake call will copy the receiver
            // start time to the receiver buffer data, so the correct
            // value must be set before this happens.
            synchronizeWithGroup(g);
            have_group = true;
        }
#endif
    }

    if (!have_group)
    {
        // This function will be called internally inside
        // synchronizeWithGroup(). This is just more complicated.
        updateAfterSrtHandshake(m_ConnRes.m_iVersion);
    }

    CInfoBlock ib;
    ib.m_iIPversion = m_PeerAddr.family();
    CInfoBlock::convert(m_PeerAddr, ib.m_piIP);
    if (m_pCache->lookup(&ib) >= 0)
    {
        m_iSRTT      = ib.m_iSRTT;
        m_iRTTVar    = ib.m_iSRTT / 2;
        m_iBandwidth = ib.m_iBandwidth;
    }

#if SRT_DEBUG_RTT
    s_rtt_trace.trace(steady_clock::now(), "Connect", -1, -1,
                      m_bIsFirstRTTReceived, -1, m_iSRTT, m_iRTTVar);
#endif

    SRT_REJECT_REASON rr = setupCC();
    if (rr != SRT_REJ_UNKNOWN)
    {
        m_RejectReason = rr;
        return CONN_REJECT;
    }

    // And, I am connected too.
    m_bConnecting = false;

    // The lock on m_ConnectionLock should still be applied, but
    // the socket could have been started removal before this function
    // has started. Do a sanity check before you continue with the
    // connection process.
    CUDTSocket* s = uglobal().locateSocket(m_SocketID);
    if (s)
    {
        // The socket could be closed at this very moment.
        // Continue with removing the socket from the pending structures,
        // but prevent it from setting it as connected.
        m_bConnected  = true;

        // register this socket for receiving data packets
        m_pRNode->m_bOnList = true;
        m_pRcvQueue->setNewEntry(this);
    }

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
    m_pRcvQueue->removeConnector(m_SocketID);

    // Ok, no more things to be done as per "clear connecting state"
    if (!s)
    {
        LOGC(cnlog.Error, log << "Connection broken in the process - socket @" << m_SocketID << " closed");
        m_RejectReason = SRT_REJ_CLOSE;
        if (eout)
        {
            *eout = CUDTException(MJ_CONNECTION, MN_CONNLOST, 0);
        }
        return CONN_REJECT;
    }

    // copy address information of local node
    // the local port must be correctly assigned BEFORE CUDT::startConnect(),
    // otherwise if startConnect() fails, the multiplexer cannot be located
    // by garbage collection and will cause leak
    s->core().m_pSndQueue->m_pChannel->getSockAddr((s->m_SelfAddr));
    CIPAddress::pton((s->m_SelfAddr), s->core().m_piSelfIP, m_PeerAddr);

    //int token = -1;
#if ENABLE_BONDING
    {
        ScopedLock cl (uglobal().m_GlobControlLock);
        CUDTGroup* g = m_parent->m_GroupOf;
        if (g)
        {
            // XXX this might require another check of group type.
            // For redundancy group, at least, update the status in the group.

            // LEAVING as comment for historical reasons. Locking is here most
            // likely not necessary because the socket cannot be removed from the
            // group until the socket isn't removed, and this requires locking of
            // m_GlobControlLock. This should ensure that when m_GroupOf is
            // not NULL, m_GroupMemberData is also valid.
            // ScopedLock glock(g->m_GroupLock);

            HLOGC(cnlog.Debug, log << "group: Socket @" << m_parent->m_SocketID << " fresh connected, setting IDLE");

            groups::SocketData* gi       = m_parent->m_GroupMemberData;
            gi->sndstate   = SRT_GST_IDLE;
            gi->rcvstate   = SRT_GST_IDLE;
            gi->laststatus = SRTS_CONNECTED;
            //token = gi->token;
            g->setGroupConnected();
        }
    }
#endif

    s->m_Status = SRTS_CONNECTED;

    // acknowledde any waiting epolls to write
    uglobal().m_EPoll.update_events(m_SocketID, m_sPollID, SRT_EPOLL_CONNECT, true);

    CGlobEvent::triggerEvent();

/* XXX Likely it should NOT be called here for two reasons:

  - likely lots of mutexes are locked here so any
    API call from here might cause a deadlock
  - if called from an asynchronous connection process, it was
    already called from inside updateConnStatus
  - if called from startConnect (synchronous mode), it is even wrong.

    if (m_cbConnectHook)
    {
        CALLBACK_CALL(m_cbConnectHook, m_SocketID, SRT_SUCCESS, m_PeerAddr.get(), token);
    }

    */

    LOGC(cnlog.Note, log << CONID() << "Connection established to: " << m_PeerAddr.str());

    return CONN_ACCEPT;
}

void srt::CUDT::checkUpdateCryptoKeyLen(const char *loghdr SRT_ATR_UNUSED, int32_t typefield)
{
    int enc_flags = SrtHSRequest::SRT_HSTYPE_ENCFLAGS::unwrap(typefield);

    // potentially 0-7 values are possible.
    // When 0, don't change anything - it should rely on the value 0.
    // When 1, 5, 6, 7, this is kinda internal error - ignore.
    if (enc_flags >= 2 && enc_flags <= 4) // 2 = 128, 3 = 192, 4 = 256
    {
        int rcv_pbkeylen = SrtHSRequest::SRT_PBKEYLEN_BITS::wrap(enc_flags);
        if (m_config.iSndCryptoKeyLen == 0)
        {
            m_config.iSndCryptoKeyLen = rcv_pbkeylen;
            HLOGC(cnlog.Debug, log << loghdr << ": PBKEYLEN adopted from advertised value: "
                  << m_config.iSndCryptoKeyLen);
        }
        else if (m_config.iSndCryptoKeyLen != rcv_pbkeylen)
        {
            // Conflict. Use SRTO_SENDER flag to check if this side should accept
            // the enforcement, otherwise simply let it win.
            if (!m_config.bDataSender)
            {
                LOGC(cnlog.Warn,
                     log << loghdr << ": PBKEYLEN conflict - OVERRIDDEN " << m_config.iSndCryptoKeyLen << " by "
                         << rcv_pbkeylen << " from PEER (as AGENT is not SRTO_SENDER)");
                m_config.iSndCryptoKeyLen = rcv_pbkeylen;
            }
            else
            {
                LOGC(cnlog.Warn,
                     log << loghdr << ": PBKEYLEN conflict - keep " << m_config.iSndCryptoKeyLen
                         << "; peer-advertised PBKEYLEN " << rcv_pbkeylen << " rejected because Agent is SRTO_SENDER");
            }
        }
    }
    else if (enc_flags != 0)
    {
        LOGC(cnlog.Error, log << loghdr << ": IPE: enc_flags outside allowed 2, 3, 4: " << enc_flags);
    }
    else
    {
        HLOGC(cnlog.Debug, log << loghdr << ": No encryption flags found in type field: " << typefield);
    }
}

// Rendezvous
void srt::CUDT::rendezvousSwitchState(UDTRequestType& w_rsptype, bool& w_needs_extension, bool& w_needs_hsrsp)
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
    w_needs_extension = false;
    w_needs_hsrsp     = false;

    string reason;

#if ENABLE_HEAVY_LOGGING

    HLOGC(cnlog.Debug, log << "rendezvousSwitchState: HS: " << m_ConnRes.show());

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
            HLOGC(cnlog.Debug,
                  log << "rendezvousSwitchState: STATE[" << CHandShake::RdvStateStr(ost) << "->"
                      << CHandShake::RdvStateStr(nst) << "] REQTYPE[" << RequestTypeStr(orq) << "->"
                      << RequestTypeStr(nrq) << "] "
                      << "ext:" << (needext ? (needrsp ? "HSRSP" : "HSREQ") : "NONE")
                      << (reason == "" ? string() : "reason:" + reason));
        }
    } l_logend = {m_RdvState, req, m_RdvState, w_rsptype, w_needs_extension, w_needs_hsrsp, reason};

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
            w_rsptype = URQ_CONCLUSION;
            if (hsd == HSD_INITIATOR)
                w_needs_extension = true;
            return;
        }

        if (req == URQ_CONCLUSION)
        {
            m_RdvState = CHandShake::RDV_FINE;
            w_rsptype   = URQ_CONCLUSION;

            w_needs_extension = true; // (see below - this needs to craft either HSREQ or HSRSP)
            // if this->isWinner(), then craft HSREQ for that response.
            // if this->isLoser(), then this packet should bring HSREQ, so craft HSRSP for the response.
            if (hsd == HSD_RESPONDER)
                w_needs_hsrsp = true;
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
            w_rsptype = URQ_CONCLUSION;
            if (hsd == HSD_INITIATOR)
                w_needs_extension = true;
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
                        cnlog.Debug,
                        log << "rendezvousSwitchState: "
                               "{INITIATOR}[ATTENTION] awaits CONCLUSION+HSRSP, got CONCLUSION, remain in [ATTENTION]");
                    w_rsptype         = URQ_CONCLUSION;
                    w_needs_extension = true; // If you expect to receive HSRSP, continue sending HSREQ
                    return;
                }
                m_RdvState = CHandShake::RDV_CONNECTED;
                w_rsptype   = URQ_AGREEMENT;
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
                        cnlog.Warn,
                        log << "rendezvousSwitchState: (IPE!)"
                               "{RESPONDER}[ATTENTION] awaits CONCLUSION+HSREQ, got CONCLUSION, remain in [ATTENTION]");
                    w_rsptype         = URQ_CONCLUSION;
                    w_needs_extension = false; // If you received WITHOUT extensions, respond WITHOUT extensions (wait
                                               // for the right message)
                    return;
                }
                m_RdvState       = CHandShake::RDV_INITIATED;
                w_rsptype         = URQ_CONCLUSION;
                w_needs_extension = true;
                w_needs_hsrsp     = true;
                return;
            }

            LOGC(cnlog.Error, log << "RENDEZVOUS COOKIE DRAW! Cannot resolve to a valid state.");
            // Fallback for cookie draw
            m_RdvState = CHandShake::RDV_INVALID;
            w_rsptype   = URQFailure(SRT_REJ_RDVCOOKIE);
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
                w_rsptype = URQ_DONE;
                return;
            }

            if (hsd == HSD_RESPONDER)
            {
                // In this case the missed URQ_CONCLUSION was sent with extensions, so
                // we have to request this once again. Send URQ_CONCLUSION in order to
                // inform the other party that we need the conclusion message once again.
                // The ATTENTION state should be maintained.
                w_rsptype         = URQ_CONCLUSION;
                w_needs_extension = true;
                w_needs_hsrsp     = true;
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
                HLOGC(cnlog.Debug,
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
                HLOGC(cnlog.Debug,
                      log << "rendezvousSwitchState: {RESPONDER}[FINE] <CONCLUSION. Stay in [FINE], await AGREEMENT");
            }
            else
            {
                correct_switch = true;
            }

            if (!correct_switch)
            {
                w_rsptype = URQ_CONCLUSION;
                // initiator should send HSREQ, responder HSRSP,
                // in both cases extension is needed
                w_needs_extension = true;
                w_needs_hsrsp     = hsd == HSD_RESPONDER;
                return;
            }

            m_RdvState = CHandShake::RDV_CONNECTED;
            w_rsptype   = URQ_AGREEMENT;
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
            w_rsptype   = URQ_DONE;
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
                HLOGC(cnlog.Debug, log << "<-- AGREEMENT: already connected");
            }
            else
            {
                HLOGC(cnlog.Debug, log << "<-- AGREEMENT: switched to connected");
            }
            m_RdvState = CHandShake::RDV_CONNECTED;
            w_rsptype   = URQ_DONE;
            return;
        }

        if (req == URQ_CONCLUSION)
        {
            // Receiving conclusion in this state means that the other party
            // didn't get our conclusion, so send it again, the same as when
            // exiting the ATTENTION state.
            w_rsptype = URQ_CONCLUSION;
            if (hsd == HSD_RESPONDER)
            {
                HLOGC(cnlog.Debug,
                      log << "rendezvousSwitchState: "
                             "{RESPONDER}[INITIATED] awaits AGREEMENT, "
                             "got CONCLUSION, sending CONCLUSION+HSRSP");
                w_needs_extension = true;
                w_needs_hsrsp     = true;
                return;
            }

            // Loser, initiated? This may only happen in parallel arrangement, where
            // the agent exchanges empty conclusion messages with the peer, simultaneously
            // exchanging HSREQ-HSRSP conclusion messages. Check if THIS message contained
            // HSREQ, and set responding HSRSP in that case.
            if (hs_flags == 0)
            {
                HLOGC(cnlog.Debug,
                      log << "rendezvousSwitchState: "
                             "{INITIATOR}[INITIATED] awaits AGREEMENT, "
                             "got empty CONCLUSION, STILL RESPONDING CONCLUSION+HSRSP");
            }
            else
            {

                HLOGC(cnlog.Debug,
                      log << "rendezvousSwitchState: "
                             "{INITIATOR}[INITIATED] awaits AGREEMENT, "
                             "got CONCLUSION+HSREQ, responding CONCLUSION+HSRSP");
            }
            w_needs_extension = true;
            w_needs_hsrsp     = true;
            return;
        }
    }

        reason = "INITIATED -> AGREEMENT(done)";
        break;

    case CHandShake::RDV_CONNECTED:
        // Do nothing. This theoretically should never happen.
        w_rsptype = URQ_DONE;
        return;
    }

    HLOGC(cnlog.Debug, log << "rendezvousSwitchState: INVALID STATE TRANSITION, result: INVALID");
    // All others are treated as errors
    m_RdvState = CHandShake::RDV_WAVING;
    w_rsptype   = URQFailure(SRT_REJ_ROGUE);
}

/*
 * Timestamp-based Packet Delivery (TsbPd) thread
 * This thread runs only if TsbPd mode is enabled
 * Hold received packets until its time to 'play' them, at PktTimeStamp + TsbPdDelay.
 */
#if ENABLE_NEW_RCVBUFFER
void * srt::CUDT::tsbpd(void* param)
{
    CUDT* self = (CUDT*)param;

    THREAD_STATE_INIT("SRT:TsbPd");

#if ENABLE_BONDING
    // Make the TSBPD thread a "client" of the group,
    // which will ensure that the group will not be physically
    // deleted until this thread exits.
    // NOTE: DO NOT LEAD TO EVER CANCEL THE THREAD!!!
    CUDTUnited::GroupKeeper gkeeper(self->uglobal(), self->m_parent);
#endif

    CUniqueSync recvdata_lcc (self->m_RecvLock, self->m_RecvDataCond);
    CSync tsbpd_cc(self->m_RcvTsbPdCond, recvdata_lcc.locker());

    self->m_bTsbPdAckWakeup = true;
    while (!self->m_bClosing)
    {
        steady_clock::time_point tsNextDelivery; // Next packet delivery time
        bool                     rxready = false;
#if ENABLE_BONDING
        bool shall_update_group = false;
#endif

        enterCS(self->m_RcvBufferLock);
        const steady_clock::time_point tnow = steady_clock::now();

        self->m_pRcvBuffer->updRcvAvgDataSize(tnow);
        const srt::CRcvBufferNew::PacketInfo info = self->m_pRcvBuffer->getFirstValidPacketInfo();

        const bool is_time_to_deliver = !is_zero(info.tsbpd_time) && (tnow >= info.tsbpd_time);
        tsNextDelivery = info.tsbpd_time;

        if (!self->m_bTLPktDrop)
        {
            rxready = !info.seq_gap && is_time_to_deliver;
        }
        else if (is_time_to_deliver)
        {
            rxready = true;
            if (info.seq_gap)
            {
                const int iDropCnt SRT_ATR_UNUSED = self->rcvDropTooLateUpTo(info.seqno);
#if ENABLE_BONDING
                shall_update_group = true;
#endif

#if ENABLE_LOGGING
                const int64_t timediff_us = count_microseconds(tnow - info.tsbpd_time);
#if ENABLE_HEAVY_LOGGING
                HLOGC(tslog.Debug,
                    log << self->CONID() << "tsbpd: DROPSEQ: up to seqno %" << CSeqNo::decseq(info.seqno) << " ("
                    << iDropCnt << " packets) playable at " << FormatTime(info.tsbpd_time) << " delayed "
                    << (timediff_us / 1000) << "." << std::setw(3) << std::setfill('0') << (timediff_us % 1000) << " ms");
#endif
                LOGC(brlog.Warn, log << self->CONID() << "RCV-DROPPED " << iDropCnt << " packet(s). Packet seqno %" << info.seqno
                    << " delayed for " << (timediff_us / 1000) << "." << std::setw(3) << std::setfill('0')
                    << (timediff_us % 1000) << " ms");
#endif

                tsNextDelivery = steady_clock::time_point(); // Ready to read, nothing to wait for.
            }
        }
        leaveCS(self->m_RcvBufferLock);

        if (rxready)
        {
            HLOGC(tslog.Debug,
                log << self->CONID() << "tsbpd: PLAYING PACKET seq=" << info.seqno << " (belated "
                << (count_milliseconds(steady_clock::now() - info.tsbpd_time)) << "ms)");
            /*
             * There are packets ready to be delivered
             * signal a waiting "recv" call if there is any data available
             */
            if (self->m_config.bSynRecving)
            {
                recvdata_lcc.notify_one();
            }
            /*
             * Set EPOLL_IN to wakeup any thread waiting on epoll
             */
            self->uglobal().m_EPoll.update_events(self->m_SocketID, self->m_sPollID, SRT_EPOLL_IN, true);
#if ENABLE_BONDING
            // If this is NULL, it means:
            // - the socket never was a group member
            // - the socket was a group member, but:
            //    - was just removed as a part of closure
            //    - and will never be member of the group anymore

            // If this is not NULL, it means:
            // - This socket is currently member of the group
            // - This socket WAS a member of the group, though possibly removed from it already, BUT:
            //   - the group that this socket IS OR WAS member of is in the GroupKeeper
            //   - the GroupKeeper prevents the group from being deleted
            //   - it is then completely safe to access the group here,
            //     EVEN IF THE SOCKET THAT WAS ITS MEMBER IS BEING DELETED.

            // It is ensured that the group object exists here because GroupKeeper
            // keeps it busy, even if you just closed the socket, remove it as a member
            // or even the group is empty and was explicitly closed.
            if (gkeeper.group)
            {
                // Functions called below will lock m_GroupLock, which in hierarchy
                // lies after m_RecvLock. Must unlock m_RecvLock to be able to lock
                // m_GroupLock inside the calls.
                InvertedLock unrecv(self->m_RecvLock);
                // The current "APP reader" needs to simply decide as to whether
                // the next CUDTGroup::recv() call should return with no blocking or not.
                // When the group is read-ready, it should update its pollers as it sees fit.

                // NOTE: this call will set lock to m_IncludedGroup->m_GroupLock
                HLOGC(tslog.Debug, log << self->CONID() << "tsbpd: GROUP: checking if %" << info.seqno << " makes group readable");
                gkeeper.group->updateReadState(self->m_SocketID, info.seqno);

                if (shall_update_group)
                {
                    // A group may need to update the parallelly used idle links,
                    // should it have any. Pass the current socket position in order
                    // to skip it from the group loop.
                    // NOTE: SELF LOCKING.
                    gkeeper.group->updateLatestRcv(self->m_parent);
                }
            }
#endif
            CGlobEvent::triggerEvent();
            tsNextDelivery = steady_clock::time_point(); // Ready to read, nothing to wait for.
        }

        if (!is_zero(tsNextDelivery))
        {
            IF_HEAVY_LOGGING(const steady_clock::duration timediff = tsNextDelivery - tnow);
            /*
             * Buffer at head of queue is not ready to play.
             * Schedule wakeup when it will be.
             */
            self->m_bTsbPdAckWakeup = false;
            HLOGC(tslog.Debug,
                log << self->CONID() << "tsbpd: FUTURE PACKET seq=" << info.seqno
                << " T=" << FormatTime(tsNextDelivery) << " - waiting " << count_milliseconds(timediff) << "ms");
            THREAD_PAUSED();
            tsbpd_cc.wait_until(tsNextDelivery);
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
            tsbpd_cc.wait();
            THREAD_RESUMED();
        }

        HLOGC(tslog.Debug, log << self->CONID() << "tsbpd: WAKE UP!!!");
    }
    THREAD_EXIT();
    HLOGC(tslog.Debug, log << self->CONID() << "tsbpd: EXITING");
    return NULL;
}

int srt::CUDT::rcvDropTooLateUpTo(int seqno)
{
    // Make sure that it would not drop over m_iRcvCurrSeqNo, which may break senders.
    if (CSeqNo::seqcmp(seqno, CSeqNo::incseq(m_iRcvCurrSeqNo)) > 0)
        seqno = CSeqNo::incseq(m_iRcvCurrSeqNo);

    const int seq_gap_len = CSeqNo::seqoff(m_iRcvLastSkipAck, seqno);

    // seq_gap_len can be <= 0 if a packet has been dropped by the sender.
    if (seq_gap_len > 0)
    {
        // Remove [from,to-inclusive]
        dropFromLossLists(m_iRcvLastSkipAck, CSeqNo::decseq(seqno));
        m_iRcvLastSkipAck = seqno;
    }

    const int iDropCnt = m_pRcvBuffer->dropUpTo(seqno);
    if (iDropCnt > 0)
    {
        enterCS(m_StatsLock);
        // Estimate dropped bytes from average payload size.
        const uint64_t avgpayloadsz = m_pRcvBuffer->getRcvAvgPayloadSize();
        m_stats.rcvr.dropped.count(stats::BytesPackets(iDropCnt * avgpayloadsz, (uint32_t) iDropCnt));
        leaveCS(m_StatsLock);
    }
    return iDropCnt;
}

#else
void * srt::CUDT::tsbpd(void *param)
{
    CUDT *self = (CUDT *)param;

    THREAD_STATE_INIT("SRT:TsbPd");

#if ENABLE_BONDING
    // Make the TSBPD thread a "client" of the group,
    // which will ensure that the group will not be physically
    // deleted until this thread exits.
    // NOTE: DO NOT LEAD TO EVER CANCEL THE THREAD!!!
    CUDTUnited::GroupKeeper gkeeper (self->uglobal(), self->m_parent);
#endif

    UniqueLock recv_lock  (self->m_RecvLock);
    CSync recvdata_cc (self->m_RecvDataCond, recv_lock);
    CSync tsbpd_cc    (self->m_RcvTsbPdCond, recv_lock);

    self->m_bTsbPdAckWakeup = true;
    while (!self->m_bClosing)
    {
        int32_t                  current_pkt_seq = 0;
        steady_clock::time_point tsbpdtime;
        bool                     rxready = false;
        int32_t                  rcv_base_seq = SRT_SEQNO_NONE;
#if ENABLE_BONDING
        bool shall_update_group = false;
        if (gkeeper.group)
        {
            // Functions called below will lock m_GroupLock, which in hierarchy
            // lies after m_RecvLock. Must unlock m_RecvLock to be able to lock
            // m_GroupLock inside the calls.
            InvertedLock unrecv(self->m_RecvLock);
            rcv_base_seq = gkeeper.group->getRcvBaseSeqNo();
        }
#endif

        enterCS(self->m_RcvBufferLock);

        self->m_pRcvBuffer->updRcvAvgDataSize(steady_clock::now());

        if (self->m_bTLPktDrop)
        {
            int32_t skiptoseqno = SRT_SEQNO_NONE;
            bool    passack     = true; // Get next packet to wait for even if not acked
            rxready = self->m_pRcvBuffer->getRcvFirstMsg((tsbpdtime), (passack), (skiptoseqno), (current_pkt_seq), rcv_base_seq);

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

                if (skiptoseqno != SRT_SEQNO_NONE && seqlen > 0)
                {
                    /*
                     * skiptoseqno != SRT_SEQNO_NONE,
                     * packet ready to play but preceeded by missing packets (hole).
                     */

                    self->updateForgotten(seqlen, self->m_iRcvLastSkipAck, skiptoseqno);
                    self->m_pRcvBuffer->skipData(seqlen);

                    self->m_iRcvLastSkipAck = skiptoseqno;
#if ENABLE_BONDING
                    shall_update_group = true;
#endif

#if ENABLE_LOGGING
                    int64_t timediff_us = 0;
                    if (!is_zero(tsbpdtime))
                        timediff_us = count_microseconds(steady_clock::now() - tsbpdtime);
#if ENABLE_HEAVY_LOGGING
                    HLOGC(tslog.Debug,
                          log << self->CONID() << "tsbpd: DROPSEQ: up to seqno %" << CSeqNo::decseq(skiptoseqno) << " ("
                              << seqlen << " packets) playable at " << FormatTime(tsbpdtime) << " delayed "
                              << (timediff_us / 1000) << "." << std::setw(3) << std::setfill('0') << (timediff_us % 1000) << " ms");
#endif
                    LOGC(brlog.Warn,
                         log << self->CONID() << "RCV-DROPPED " << seqlen << " packet(s), packet seqno %" << skiptoseqno
                             << " delayed for " << (timediff_us / 1000) << "." << std::setw(3) << std::setfill('0')
                             << (timediff_us % 1000) << " ms");
#endif

                    tsbpdtime = steady_clock::time_point(); //Next sent ack will unblock
                    rxready   = false;
                }
                else if (passack)
                {
                    /* Packets ready to play but not yet acknowledged (should happen within 10ms) */
                    rxready   = false;
                    tsbpdtime = steady_clock::time_point(); // Next sent ack will unblock
                }                  /* else packet ready to play */
            }                      /* else packets not ready to play */
        }
        else
        {
            rxready = self->m_pRcvBuffer->isRcvDataReady((tsbpdtime), (current_pkt_seq), -1 /*get first ready*/);
        }
        leaveCS(self->m_RcvBufferLock);

        if (rxready)
        {
            HLOGC(tslog.Debug,
                  log << self->CONID() << "tsbpd: PLAYING PACKET seq=" << current_pkt_seq << " (belated "
                      << (count_milliseconds(steady_clock::now() - tsbpdtime)) << "ms)");
            /*
             * There are packets ready to be delivered
             * signal a waiting "recv" call if there is any data available
             */
            if (self->m_config.bSynRecving)
            {
                recvdata_cc.notify_one_locked(recv_lock);
            }
            /*
             * Set EPOLL_IN to wakeup any thread waiting on epoll
             */
            self->uglobal().m_EPoll.update_events(self->m_SocketID, self->m_sPollID, SRT_EPOLL_IN, true);
#if ENABLE_BONDING
            // If this is NULL, it means:
            // - the socket never was a group member
            // - the socket was a group member, but:
            //    - was just removed as a part of closure
            //    - and will never be member of the group anymore

            // If this is not NULL, it means:
            // - This socket is currently member of the group
            // - This socket WAS a member of the group, though possibly removed from it already, BUT:
            //   - the group that this socket IS OR WAS member of is in the GroupKeeper
            //   - the GroupKeeper prevents the group from being deleted
            //   - it is then completely safe to access the group here,
            //     EVEN IF THE SOCKET THAT WAS ITS MEMBER IS BEING DELETED.

            // It is ensured that the group object exists here because GroupKeeper
            // keeps it busy, even if you just closed the socket, remove it as a member
            // or even the group is empty and was explicitly closed.
            if (gkeeper.group)
            {
                // Functions called below will lock m_GroupLock, which in hierarchy
                // lies after m_RecvLock. Must unlock m_RecvLock to be able to lock
                // m_GroupLock inside the calls.
                InvertedLock unrecv(self->m_RecvLock);
                // The current "APP reader" needs to simply decide as to whether
                // the next CUDTGroup::recv() call should return with no blocking or not.
                // When the group is read-ready, it should update its pollers as it sees fit.

                // NOTE: this call will set lock to m_GroupOf->m_GroupLock
                HLOGC(tslog.Debug, log << self->CONID() << "tsbpd: GROUP: checking if %" << current_pkt_seq << " makes group readable");
                gkeeper.group->updateReadState(self->m_SocketID, current_pkt_seq);

                if (shall_update_group)
                {
                    // A group may need to update the parallelly used idle links,
                    // should it have any. Pass the current socket position in order
                    // to skip it from the group loop.
                    // NOTE: SELF LOCKING.
                    gkeeper.group->updateLatestRcv(self->m_parent);
                }
            }
#endif
            CGlobEvent::triggerEvent();
            tsbpdtime = steady_clock::time_point();
        }

        if (!is_zero(tsbpdtime))
        {
            IF_HEAVY_LOGGING(const steady_clock::duration timediff = tsbpdtime - steady_clock::now());
            /*
             * Buffer at head of queue is not ready to play.
             * Schedule wakeup when it will be.
             */
            self->m_bTsbPdAckWakeup = false;
            HLOGC(tslog.Debug,
                  log << self->CONID() << "tsbpd: FUTURE PACKET seq=" << current_pkt_seq
                      << " T=" << FormatTime(tsbpdtime) << " - waiting " << count_milliseconds(timediff) << "ms");
            THREAD_PAUSED();
            tsbpd_cc.wait_until(tsbpdtime);
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
            tsbpd_cc.wait();
            THREAD_RESUMED();
        }

        HLOGC(tslog.Debug, log << self->CONID() << "tsbpd: WAKE UP!!!");
    }
    THREAD_EXIT();
    HLOGC(tslog.Debug, log << self->CONID() << "tsbpd: EXITING");
    return NULL;
}
#endif // ENABLE_NEW_RCVBUFFER

void srt::CUDT::setInitialRcvSeq(int32_t isn)
{
    m_iRcvLastAck = isn;
#ifdef ENABLE_LOGGING
    m_iDebugPrevLastAck = m_iRcvLastAck;
#endif
    m_iRcvLastSkipAck = m_iRcvLastAck;
    m_iRcvLastAckAck = isn;
    m_iRcvCurrSeqNo = CSeqNo::decseq(isn);

#if ENABLE_NEW_RCVBUFFER
    sync::ScopedLock rb(m_RcvBufferLock);
    if (m_pRcvBuffer)
    {
        if (!m_pRcvBuffer->empty())
        {
            LOGC(cnlog.Error, log << "IPE: setInitialRcvSeq expected empty RCV buffer. Dropping all.");
            const int iDropCnt = m_pRcvBuffer->dropAll();
            const uint64_t avgpayloadsz = m_pRcvBuffer->getRcvAvgPayloadSize();
            sync::ScopedLock sl(m_StatsLock);
            m_stats.rcvr.dropped.count(stats::BytesPackets(iDropCnt * avgpayloadsz, (uint32_t) iDropCnt));
        }

        m_pRcvBuffer->setStartSeqNo(m_iRcvLastSkipAck);
    }
#endif
}

void srt::CUDT::updateForgotten(int seqlen, int32_t lastack, int32_t skiptoseqno)
{
    enterCS(m_StatsLock);
    // Estimate dropped bytes from average payload size.
    const uint64_t avgpayloadsz = m_pRcvBuffer->getRcvAvgPayloadSize();
    m_stats.rcvr.dropped.count(stats::BytesPackets(seqlen * avgpayloadsz, (uint32_t) seqlen));
    leaveCS(m_StatsLock);

    dropFromLossLists(lastack, CSeqNo::decseq(skiptoseqno)); //remove(from,to-inclusive)
}

bool srt::CUDT::prepareConnectionObjects(const CHandShake &hs, HandshakeSide hsd, CUDTException *eout)
{
    // This will be lazily created due to being the common
    // code with HSv5 rendezvous, in which this will be run
    // in a little bit "randomly selected" moment, but must
    // be run once in the whole connection process.
    if (m_pSndBuffer)
    {
        HLOGC(rslog.Debug, log << "prepareConnectionObjects: (lazy) already created.");
        return true;
    }

    // HSv5 is always bidirectional
    const bool bidirectional = (hs.m_iVersion > HS_VERSION_UDT4);

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
            hsd = m_config.bDataSender ? HSD_INITIATOR : HSD_RESPONDER;
        }
    }

    try
    {
        m_pSndBuffer = new CSndBuffer(32, m_iMaxSRTPayloadSize);
#if ENABLE_NEW_RCVBUFFER
        SRT_ASSERT(m_iISN != -1);
        m_pRcvBuffer = new srt::CRcvBufferNew(m_iISN, m_config.iRcvBufSize, m_pRcvQueue->m_pUnitQueue, m_config.bMessageAPI);
#else
        m_pRcvBuffer = new CRcvBuffer(m_pRcvQueue->m_pUnitQueue, m_config.iRcvBufSize);
#endif
        // after introducing lite ACK, the sndlosslist may not be cleared in time, so it requires twice space.
        m_pSndLossList = new CSndLossList(m_iFlowWindowSize * 2);
        m_pRcvLossList = new CRcvLossList(m_config.iFlightFlagSize);
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

void srt::CUDT::rewriteHandshakeData(const sockaddr_any& peer, CHandShake& w_hs)
{
    // this is a reponse handshake
    w_hs.m_iReqType        = URQ_CONCLUSION;
    w_hs.m_iMSS            = m_config.iMSS;
    w_hs.m_iFlightFlagSize = m_config.flightCapacity();
    w_hs.m_iID             = m_SocketID;

    if (w_hs.m_iVersion > HS_VERSION_UDT4)
    {
        // The version is agreed; this code is executed only in case
        // when AGENT is listener. In this case, conclusion response
        // must always contain HSv5 handshake extensions.
        w_hs.m_extension = true;
    }

    CIPAddress::ntop(peer, (w_hs.m_piPeerIP));
}

void srt::CUDT::acceptAndRespond(const sockaddr_any& agent, const sockaddr_any& peer, const CPacket& hspkt, CHandShake& w_hs)
{
    HLOGC(cnlog.Debug, log << "acceptAndRespond: setting up data according to handshake");

    ScopedLock cg(m_ConnectionLock);

    m_tsRcvPeerStartTime = steady_clock::time_point(); // will be set correctly at SRT HS

    // Uses the smaller MSS between the peers
    m_config.iMSS = std::min(m_config.iMSS, w_hs.m_iMSS);

    // exchange info for maximum flow window size
    m_iFlowWindowSize = w_hs.m_iFlightFlagSize;
    m_iPeerISN        = w_hs.m_iISN;
    setInitialRcvSeq(m_iPeerISN);
    m_iRcvCurrPhySeqNo = CSeqNo::decseq(w_hs.m_iISN);

    m_PeerID = w_hs.m_iID;

    // use peer's ISN and send it back for security check
    m_iISN = w_hs.m_iISN;

    setInitialSndSeq(m_iISN);
    m_SndLastAck2Time = steady_clock::now();

    // get local IP address and send the peer its IP address (because UDP cannot get local IP address)
    memcpy((m_piSelfIP), w_hs.m_piPeerIP, sizeof m_piSelfIP);
    m_parent->m_SelfAddr = agent;
    CIPAddress::pton((m_parent->m_SelfAddr), m_piSelfIP, peer);

    rewriteHandshakeData(peer, (w_hs));

    int udpsize          = m_config.iMSS - CPacket::UDP_HDR_SIZE;
    m_iMaxSRTPayloadSize = udpsize - CPacket::HDR_SIZE;
    HLOGC(cnlog.Debug, log << "acceptAndRespond: PAYLOAD SIZE: " << m_iMaxSRTPayloadSize);

    // Prepare all structures
    if (!prepareConnectionObjects(w_hs, HSD_DRAW, 0))
    {
        HLOGC(cnlog.Debug, log << "acceptAndRespond: prepareConnectionObjects failed - responding with REJECT.");
        // If the SRT Handshake extension was provided and wasn't interpreted
        // correctly, the connection should be rejected.
        //
        // Respond with the rejection message and exit with exception
        // so that the caller will know that this new socket should be deleted.
        w_hs.m_iReqType = URQFailure(m_RejectReason);
        throw CUDTException(MJ_SETUP, MN_REJECTED, 0);
    }
    // Since now you can use m_pCryptoControl

    CInfoBlock ib;
    ib.m_iIPversion = peer.family();
    CInfoBlock::convert(peer, ib.m_piIP);
    if (m_pCache->lookup(&ib) >= 0)
    {
        m_iSRTT      = ib.m_iSRTT;
        m_iRTTVar    = ib.m_iSRTT / 2;
        m_iBandwidth = ib.m_iBandwidth;
    }

#if SRT_DEBUG_RTT
    s_rtt_trace.trace(steady_clock::now(), "Accept", -1, -1,
                      m_bIsFirstRTTReceived, -1, m_iSRTT, m_iRTTVar);
#endif

    m_PeerAddr = peer;

    // This should extract the HSREQ and KMREQ portion in the handshake packet.
    // This could still be a HSv4 packet and contain no such parts, which will leave
    // this entity as "non-SRT-handshaken", and await further HSREQ and KMREQ sent
    // as UMSG_EXT.
    uint32_t kmdata[SRTDATA_MAXSIZE];
    size_t   kmdatasize = SRTDATA_MAXSIZE;
    if (!interpretSrtHandshake(w_hs, hspkt, (kmdata), (&kmdatasize)))
    {
        HLOGC(cnlog.Debug, log << "acceptAndRespond: interpretSrtHandshake failed - responding with REJECT.");
        // If the SRT Handshake extension was provided and wasn't interpreted
        // correctly, the connection should be rejected.
        //
        // Respond with the rejection message and return false from
        // this function so that the caller will know that this new
        // socket should be deleted.
        w_hs.m_iReqType = URQFailure(m_RejectReason);
        throw CUDTException(MJ_SETUP, MN_REJECTED, 0);
    }

   // Synchronize the time NOW because the following function is about
   // to use the start time to pass it to the receiver buffer data.
    bool have_group = false;

    {
#if ENABLE_BONDING
        ScopedLock cl (uglobal().m_GlobControlLock);
        CUDTGroup* g = m_parent->m_GroupOf;
        if (g)
        {
            // This is the last moment when this can be done.
            // The updateAfterSrtHandshake call will copy the receiver
            // start time to the receiver buffer data, so the correct
            // value must be set before this happens.
            synchronizeWithGroup(g);
            have_group = true;
        }
#endif
    }

    if (!have_group)
    {
        // This function will be called internally inside
        // synchronizeWithGroup(). This is just more complicated.
        updateAfterSrtHandshake(w_hs.m_iVersion);
    }

    SRT_REJECT_REASON rr = setupCC();
    // UNKNOWN used as a "no error" value
    if (rr != SRT_REJ_UNKNOWN)
    {
        w_hs.m_iReqType = URQFailure(rr);
        m_RejectReason = rr;
        throw CUDTException(MJ_SETUP, MN_REJECTED, 0);
    }

    // And of course, it is connected.
    m_bConnected = true;

    // Register this socket for receiving data packets.
    m_pRNode->m_bOnList = true;
    m_pRcvQueue->setNewEntry(this);

    // Save the handshake in m_ConnRes in case when needs repeating.
    m_ConnRes = w_hs;

    // Send the response to the peer, see listen() for more discussions
    // about this.
    // TODO: Here create CONCLUSION RESPONSE with:
    // - just the UDT handshake, if HS_VERSION_UDT4,
    // - if higher, the UDT handshake, the SRT HSRSP, the SRT KMRSP.
    size_t size = m_iMaxSRTPayloadSize;
    // Allocate the maximum possible memory for an SRT payload.
    // This is a maximum you can send once.
    CPacket response;
    response.setControl(UMSG_HANDSHAKE);
    response.allocate(size);

    // This will serialize the handshake according to its current form.
    HLOGC(cnlog.Debug,
          log << "acceptAndRespond: creating CONCLUSION response (HSv5: with HSRSP/KMRSP) buffer size=" << size);
    if (!createSrtHandshake(SRT_CMD_HSRSP, SRT_CMD_KMRSP, kmdata, kmdatasize, (response), (w_hs)))
    {
        LOGC(cnlog.Error, log << "acceptAndRespond: error creating handshake response");
        throw CUDTException(MJ_SETUP, MN_REJECTED, 0);
    }

#if ENABLE_HEAVY_LOGGING
    {
        // To make sure what REALLY is being sent, parse back the handshake
        // data that have been just written into the buffer.
        CHandShake debughs;
        debughs.load_from(response.m_pcData, response.getLength());
        HLOGC(cnlog.Debug,
              log << CONID() << "acceptAndRespond: sending HS from agent @"
                << debughs.m_iID << " to peer @" << response.m_iID
                << "HS:" << debughs.show());
    }
#endif

    // NOTE: BLOCK THIS instruction in order to cause the final
    // handshake to be missed and cause the problem solved in PR #417.
    // When missed this message, the caller should not accept packets
    // coming as connected, but continue repeated handshake until finally
    // received the listener's handshake.
    addressAndSend((response));
}

// This function is required to be called when a caller receives an INDUCTION
// response from the listener and would like to create a CONCLUSION that includes
// the SRT handshake extension. This extension requires that the crypter object
// be created, but it's still too early for it to be completely configured.
// This function then precreates the object so that the handshake extension can
// be created, as this happens before the completion of the connection (and
// therefore configuration of the crypter object), which can only take place upon
// reception of CONCLUSION response from the listener.
bool srt::CUDT::createCrypter(HandshakeSide side, bool bidirectional)
{
    // Lazy initialization
    if (m_pCryptoControl)
        return true;

    // Write back this value, when it was just determined.
    m_SrtHsSide = side;

    m_pCryptoControl.reset(new CCryptoControl(m_SocketID));

    // XXX These below are a little bit controversial.
    // These data should probably be filled only upon
    // reception of the conclusion handshake - otherwise
    // they have outdated values.
    m_pCryptoControl->setCryptoSecret(m_config.CryptoSecret);

    if (bidirectional || m_config.bDataSender)
    {
        HLOGC(rslog.Debug, log << "createCrypter: setting RCV/SND KeyLen=" << m_config.iSndCryptoKeyLen);
        m_pCryptoControl->setCryptoKeylen(m_config.iSndCryptoKeyLen);
    }

    return m_pCryptoControl->init(side, m_config, bidirectional);
}

SRT_REJECT_REASON srt::CUDT::setupCC()
{
    // Prepare configuration object,
    // Create the CCC object and configure it.

    // UDT also sets back the congestion window: ???
    // m_dCongestionWindow = m_pCC->m_dCWndSize;

    // XXX Not sure about that. May happen that AGENT wants
    // tsbpd mode, but PEER doesn't, even in bidirectional mode.
    // This way, the reception side should get precedense.
    // if (bidirectional || m_config.bDataSender || m_bTwoWayData)
    //    m_bPeerTsbPd = m_bTSBPD;

    // SrtCongestion will retrieve whatever parameters it needs
    // from *this.

    bool res = m_CongCtl.select(m_config.sCongestion.str());
    if (!res || !m_CongCtl.configure(this))
    {
        return SRT_REJ_CONGESTION;
    }

    // Configure filter module
    if (!m_config.sPacketFilterConfig.empty())
    {
        // This string, when nonempty, defines that the corrector shall be
        // configured. Otherwise it's left uninitialized.

        // At this point we state everything is checked and the appropriate
        // corrector type is already selected, so now create it.
        HLOGC(pflog.Debug, log << "filter: Configuring: " << m_config.sPacketFilterConfig.c_str());
        bool status = true;
        try
        {
            // The filter configurer is build the way that allows to quit immediately
            // exit by exception, but the exception is meant for the filter only.
            status = m_PacketFilter.configure(this, m_pRcvQueue->m_pUnitQueue, m_config.sPacketFilterConfig.str());
        }
        catch (CUDTException& )
        {
            status = false;
        }

        if (!status)
            return SRT_REJ_FILTER;

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
    const steady_clock::duration min_nak = microseconds_from(m_CongCtl->minNAKInterval());
    if (min_nak != steady_clock::duration::zero())
        m_tdMinNakInterval = min_nak;

    // Update timers
    const steady_clock::time_point currtime = steady_clock::now();
    m_tsLastRspTime.store(currtime);
    m_tsNextACKTime.store(currtime + m_tdACKInterval);
    m_tsNextNAKTime.store(currtime + m_tdNAKInterval);
    m_tsLastRspAckTime = currtime;
    m_tsLastSndTime.store(currtime);

    HLOGC(rslog.Debug,
          log << "setupCC: setting parameters: mss=" << m_config.iMSS << " maxCWNDSize/FlowWindowSize=" << m_iFlowWindowSize
              << " rcvrate=" << m_iDeliveryRate << "p/s (" << m_iByteDeliveryRate << "B/S)"
              << " rtt=" << m_iSRTT << " bw=" << m_iBandwidth);

    if (!updateCC(TEV_INIT, EventVariant(TEV_INIT_RESET)))
    {
        LOGC(rslog.Error, log << "setupCC: IPE: resrouces not yet initialized!");
        return SRT_REJ_IPE;
    }
    return SRT_REJ_UNKNOWN;
}

void srt::CUDT::considerLegacySrtHandshake(const steady_clock::time_point &timebase)
{
    // Do a fast pre-check first - this simply declares that agent uses HSv5
    // and the legacy SRT Handshake is not to be done. Second check is whether
    // agent is sender (=initiator in HSv4).
    if (!isOPT_TsbPd() || !m_config.bDataSender)
        return;

    if (m_iSndHsRetryCnt <= 0)
    {
        HLOGC(cnlog.Debug, log << "Legacy HSREQ: not needed, expire counter=" << m_iSndHsRetryCnt);
        return;
    }

    const steady_clock::time_point now = steady_clock::now();
    if (!is_zero(timebase))
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
            HLOGC(cnlog.Debug, log << "Legacy HSREQ: TOO EARLY, will still retry " << m_iSndHsRetryCnt << " times");
            return;
        }
    }
    // If 0 timebase, it means that this is the initial sending with the very first
    // payload packet sent. Send only if this is still set to maximum+1 value.
    else if (m_iSndHsRetryCnt < SRT_MAX_HSRETRY + 1)
    {
        HLOGC(cnlog.Debug,
              log << "Legacy HSREQ: INITIAL, REPEATED, so not to be done. Will repeat on sending " << m_iSndHsRetryCnt
                  << " times");
        return;
    }

    HLOGC(cnlog.Debug, log << "Legacy HSREQ: SENDING, will repeat " << m_iSndHsRetryCnt << " times if no response");
    m_iSndHsRetryCnt--;
    m_tsSndHsLastTime = now;
    sendSrtMsg(SRT_CMD_HSREQ);
}

void srt::CUDT::checkSndTimers(Whether2RegenKm regen)
{
    if (m_SrtHsSide == HSD_INITIATOR)
    {
        HLOGC(cnlog.Debug, log << "checkSndTimers: HS SIDE: INITIATOR, considering legacy handshake with timebase");
        // Legacy method for HSREQ, only if initiator.
        considerLegacySrtHandshake(m_tsSndHsLastTime + microseconds_from(m_iSRTT * 3 / 2));
    }
    else
    {
        HLOGC(cnlog.Debug,
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
            m_pCryptoControl->sendKeysToPeer(this, SRTT(), regen);
    }
}

void srt::CUDT::addressAndSend(CPacket& w_pkt)
{
    w_pkt.m_iID        = m_PeerID;
    setPacketTS(w_pkt, steady_clock::now());

    // NOTE: w_pkt isn't modified in this call,
    // just in CChannel::sendto it's modified in place
    // before sending for performance purposes,
    // and then modification is undone. Logically then
    // there's no modification here.
    m_pSndQueue->sendto(m_PeerAddr, w_pkt);
}

// [[using maybe_locked(m_GlobControlLock, if called from GC)]]
bool srt::CUDT::closeInternal()
{
    // NOTE: this function is called from within the garbage collector thread.

    if (!m_bOpened)
    {
        return false;
    }

    // IMPORTANT:
    // This function may block indefinitely, if called for a socket
    // that has m_bBroken == false or m_bConnected == true.
    // If it is intended to forcefully close the socket, make sure
    // that it's in response to a broken connection.
    HLOGC(smlog.Debug, log << CONID() << " - closing socket:");

    if (m_config.Linger.l_onoff != 0)
    {
        const steady_clock::time_point entertime = steady_clock::now();

        HLOGC(smlog.Debug, log << CONID() << " ... (linger)");
        while (!m_bBroken && m_bConnected && (m_pSndBuffer->getCurrBufSize() > 0) &&
               (steady_clock::now() - entertime < seconds_from(m_config.Linger.l_linger)))
        {
            // linger has been checked by previous close() call and has expired
            if (m_tsLingerExpiration >= entertime)
                break;

            if (!m_config.bSynSending)
            {
                // if this socket enables asynchronous sending, return immediately and let GC to close it later
                if (is_zero(m_tsLingerExpiration))
                    m_tsLingerExpiration = entertime + seconds_from(m_config.Linger.l_linger);

                HLOGC(smlog.Debug,
                      log << "CUDT::close: linger-nonblocking, setting expire time T="
                          << FormatTime(m_tsLingerExpiration));

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
     * removing usock for EPolls right after (update_usocks) clears it (in other HAI patch).
     *
     * What is in EPoll shall be the responsibility of the application, if it want local close event,
     * it would remove the socket from the EPoll after close.
     */

    // Make a copy under a lock because other thread might access it
    // at the same time.
    enterCS(uglobal().m_EPoll.m_EPollLock);
    set<int> epollid = m_sPollID;
    leaveCS(uglobal().m_EPoll.m_EPollLock);

    // trigger any pending IO events.
    HLOGC(smlog.Debug, log << "close: SETTING ERR readiness on E" << Printable(epollid) << " of @" << m_SocketID);
    uglobal().m_EPoll.update_events(m_SocketID, m_sPollID, SRT_EPOLL_ERR, true);
    // then remove itself from all epoll monitoring
    int no_events = 0;
    for (set<int>::iterator i = epollid.begin(); i != epollid.end(); ++i)
    {
        HLOGC(smlog.Debug, log << "close: CLEARING subscription on E" << (*i) << " of @" << m_SocketID);
        try
        {
            uglobal().m_EPoll.update_usock(*i, m_SocketID, &no_events);
        }
        catch (...)
        {
            // The goal of this loop is to remove all subscriptions in
            // the epoll system to this socket. If it's unsubscribed already,
            // that's even better.
        }
        HLOGC(smlog.Debug, log << "close: removing E" << (*i) << " from back-subscribers of @" << m_SocketID);
    }

    // Not deleting elements from m_sPollID inside the loop because it invalidates
    // the control iterator of the loop. Instead, all will be removed at once.

    // IMPORTANT: there's theoretically little time between setting ERR readiness
    // and unsubscribing, however if there's an application waiting on this event,
    // it should be informed before this below instruction locks the epoll mutex.
    enterCS(uglobal().m_EPoll.m_EPollLock);
    m_sPollID.clear();
    leaveCS(uglobal().m_EPoll.m_EPollLock);

    // XXX What's this, could any of the above actions make it !m_bOpened?
    if (!m_bOpened)
    {
        return true;
    }

    // Inform the threads handler to stop.
    m_bClosing = true;

    HLOGC(smlog.Debug, log << CONID() << "CLOSING STATE. Acquiring connection lock");

    ScopedLock connectguard(m_ConnectionLock);

    // Signal the sender and recver if they are waiting for data.
    releaseSynch();

    HLOGC(smlog.Debug, log << CONID() << "CLOSING, removing from listener/connector");

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
            HLOGC(smlog.Debug, log << CONID() << "CLOSING - sending SHUTDOWN to the peer @" << m_PeerID);
            sendCtrl(UMSG_SHUTDOWN);
        }

        // Store current connection information.
        CInfoBlock ib;
        ib.m_iIPversion = m_PeerAddr.family();
        CInfoBlock::convert(m_PeerAddr, ib.m_piIP);
        ib.m_iSRTT      = m_iSRTT;
        ib.m_iBandwidth = m_iBandwidth;
        m_pCache->update(&ib);

#if SRT_DEBUG_RTT
    s_rtt_trace.trace(steady_clock::now(), "Cache", -1, -1,
                      m_bIsFirstRTTReceived, -1, m_iSRTT, -1);
#endif

        m_bConnected = false;
    }

    HLOGC(smlog.Debug, log << "CLOSING, joining send/receive threads");

    // waiting all send and recv calls to stop
    ScopedLock sendguard(m_SendLock);
    ScopedLock recvguard(m_RecvLock);

    // Locking m_RcvBufferLock to protect calling to m_pCryptoControl->decrypt((packet))
    // from the processData(...) function while resetting Crypto Control.
    enterCS(m_RcvBufferLock);
    if (m_pCryptoControl)
        m_pCryptoControl->close();

    m_pCryptoControl.reset();
    leaveCS(m_RcvBufferLock);

    m_uPeerSrtVersion        = SRT_VERSION_UNK;
    m_tsRcvPeerStartTime     = steady_clock::time_point();

    m_bOpened = false;

    return true;
}

int srt::CUDT::receiveBuffer(char *data, int len)
{
    if (!m_CongCtl->checkTransArgs(SrtCongestion::STA_BUFFER, SrtCongestion::STAD_RECV, data, len, SRT_MSGTTL_INF, false))
        throw CUDTException(MJ_NOTSUP, MN_INVALBUFFERAPI, 0);

    if (isOPT_TsbPd())
    {
        LOGP(arlog.Error, "recv: This function is not intended to be used in Live mode with TSBPD.");
        throw CUDTException(MJ_NOTSUP, MN_INVALBUFFERAPI, 0);
    }

    UniqueLock recvguard(m_RecvLock);

    if ((m_bBroken || m_bClosing) && !isRcvBufferReady())
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
            HLOGC(arlog.Debug, log << "STREAM API, SHUTDOWN: marking as EOF");
            return 0;
        }
        HLOGC(arlog.Debug,
            log << (m_config.bMessageAPI ? "MESSAGE" : "STREAM") << " API, " << (m_bShutdown ? "" : "no")
            << " SHUTDOWN. Reporting as BROKEN.");
        throw CUDTException(MJ_CONNECTION, MN_CONNLOST, 0);
    }

    CSync rcond  (m_RecvDataCond, recvguard);
    CSync tscond (m_RcvTsbPdCond, recvguard);
    if (!isRcvBufferReady())
    {
        if (!m_config.bSynRecving)
        {
            throw CUDTException(MJ_AGAIN, MN_RDAVAIL, 0);
        }

        // Kick TsbPd thread to schedule next wakeup (if running)
        if (m_config.iRcvTimeOut < 0)
        {
            THREAD_PAUSED();
            while (stillConnected() && !isRcvBufferReady())
            {
                // Do not block forever, check connection status each 1 sec.
                rcond.wait_for(seconds_from(1));
            }
            THREAD_RESUMED();
        }
        else
        {
            const steady_clock::time_point exptime =
                steady_clock::now() + milliseconds_from(m_config.iRcvTimeOut);
            THREAD_PAUSED();
            while (stillConnected() && !isRcvBufferReady())
            {
                if (!rcond.wait_until(exptime)) // NOT means "not received a signal"
                    break; // timeout
            }
            THREAD_RESUMED();
        }
    }

    // throw an exception if not connected
    if (!m_bConnected)
        throw CUDTException(MJ_CONNECTION, MN_NOCONN, 0);

    if ((m_bBroken || m_bClosing) && !isRcvBufferReady())
    {
        // See at the beginning
        if (!m_config.bMessageAPI && m_bShutdown)
        {
            HLOGC(arlog.Debug, log << "STREAM API, SHUTDOWN: marking as EOF");
            return 0;
        }
        HLOGC(arlog.Debug,
              log << (m_config.bMessageAPI ? "MESSAGE" : "STREAM") << " API, " << (m_bShutdown ? "" : "no")
                  << " SHUTDOWN. Reporting as BROKEN.");

        throw CUDTException(MJ_CONNECTION, MN_CONNLOST, 0);
    }

    enterCS(m_RcvBufferLock);
    const int res = m_pRcvBuffer->readBuffer(data, len);
    leaveCS(m_RcvBufferLock);

    /* Kick TsbPd thread to schedule next wakeup (if running) */
    if (m_bTsbPd)
    {
        HLOGP(tslog.Debug, "Ping TSBPD thread to schedule wakeup");
        tscond.notify_one_locked(recvguard);
    }
    else
    {
        HLOGP(tslog.Debug, "NOT pinging TSBPD - not set");
    }

    if (!isRcvBufferReady())
    {
        // read is not available any more
        uglobal().m_EPoll.update_events(m_SocketID, m_sPollID, SRT_EPOLL_IN, false);
    }

    if ((res <= 0) && (m_config.iRcvTimeOut >= 0))
        throw CUDTException(MJ_AGAIN, MN_XMTIMEOUT, 0);

    return res;
}

// [[using maybe_locked(CUDTGroup::m_GroupLock, m_parent->m_GroupOf != NULL)]];
// [[using locked(m_SendLock)]];
int srt::CUDT::sndDropTooLate()
{
    if (!m_bPeerTLPktDrop)
        return 0;

    if (!m_config.bMessageAPI)
    {
        LOGC(aslog.Error, log << "The SRTO_TLPKTDROP flag can only be used with message API.");
        throw CUDTException(MJ_NOTSUP, MN_INVALBUFFERAPI, 0);
    }

    const time_point tnow = steady_clock::now();
    const int buffdelay_ms = (int) count_milliseconds(m_pSndBuffer->getBufferingDelay(tnow));

    // high threshold (msec) at tsbpd_delay plus sender/receiver reaction time (2 * 10ms)
    // Minimum value must accomodate an I-Frame (~8 x average frame size)
    // >>need picture rate or app to set min treshold
    // >>using 1 sec for worse case 1 frame using all bit budget.
    // picture rate would be useful in auto SRT setting for min latency
    // XXX Make SRT_TLPKTDROP_MINTHRESHOLD_MS option-configurable
    const int threshold_ms = (m_config.iSndDropDelay >= 0)
        ? std::max(m_iPeerTsbPdDelay_ms + m_config.iSndDropDelay, +SRT_TLPKTDROP_MINTHRESHOLD_MS)
            + (2 * COMM_SYN_INTERVAL_US / 1000)
        : 0;

    if (threshold_ms == 0 || buffdelay_ms <= threshold_ms)
        return 0;

    // protect packet retransmission
    ScopedLock rcvlck(m_RecvAckLock);
    int dbytes;
    int32_t first_msgno;
    const int dpkts = m_pSndBuffer->dropLateData((dbytes), (first_msgno), tnow - milliseconds_from(threshold_ms));
    if (dpkts <= 0)
        return 0;

    // If some packets were dropped update stats, socket state, loss list and the parent group if any.
    enterCS(m_StatsLock);
    m_stats.sndr.dropped.count(dbytes);;
    leaveCS(m_StatsLock);

    IF_HEAVY_LOGGING(const int32_t realack = m_iSndLastDataAck);
    const int32_t fakeack = CSeqNo::incseq(m_iSndLastDataAck, dpkts);

    m_iSndLastAck     = fakeack;
    m_iSndLastDataAck = fakeack;

    const int32_t minlastack = CSeqNo::decseq(m_iSndLastDataAck);
    m_pSndLossList->removeUpTo(minlastack);
    /* If we dropped packets not yet sent, advance current position */
    // THIS MEANS: m_iSndCurrSeqNo = MAX(m_iSndCurrSeqNo, m_iSndLastDataAck-1)
    if (CSeqNo::seqcmp(m_iSndCurrSeqNo, minlastack) < 0)
    {
        m_iSndCurrSeqNo = minlastack;
    }

    HLOGC(aslog.Debug, log << "SND-DROP: %(" << realack << "-" << m_iSndCurrSeqNo << ") n="
        << dpkts << "pkt " << dbytes << "B, span=" << buffdelay_ms << " ms, FIRST #" << first_msgno);

#if ENABLE_BONDING
    // This is done with a presumption that the group
    // exists and if this is not NULL, it means that this
    // function was called with locked m_GroupLock, as sendmsg2
    // function was called from inside CUDTGroup::send, which
    // locks the whole function.
    //
    // XXX This is true only because all existing groups are managed
    // groups, that is, sockets cannot be added or removed from group
    // manually, nor can send/recv operation be done on a single socket
    // from the API call directly. This should be extra verified, if that
    // changes in the future.
    //
    if (m_parent->m_GroupOf)
    {
        // What's important is that the lock on GroupLock cannot be applied
        // here, both because it might be applied already, that is, according
        // to the condition defined at this function's header, it is applied
        // under this condition. Hence ackMessage can be defined as 100% locked.
        m_parent->m_GroupOf->ackMessage(first_msgno);
    }
#endif

    return dpkts;
}

int srt::CUDT::sendmsg(const char *data, int len, int msttl, bool inorder, int64_t srctime)
{
    SRT_MSGCTRL mctrl = srt_msgctrl_default;
    mctrl.msgttl      = msttl;
    mctrl.inorder     = inorder;
    mctrl.srctime     = srctime;
    return this->sendmsg2(data, len, (mctrl));
}

// [[using maybe_locked(CUDTGroup::m_GroupLock, m_parent->m_GroupOf != NULL)]]
// GroupLock is applied when this function is called from inside CUDTGroup::send,
// which is the only case when the m_parent->m_GroupOf is not NULL.
int srt::CUDT::sendmsg2(const char *data, int len, SRT_MSGCTRL& w_mctrl)
{
    // throw an exception if not connected
    if (m_bBroken || m_bClosing)
        throw CUDTException(MJ_CONNECTION, MN_CONNLOST, 0);
    else if (!m_bConnected || !m_CongCtl.ready())
        throw CUDTException(MJ_CONNECTION, MN_NOCONN, 0);

    if (len <= 0)
    {
        LOGC(aslog.Error, log << "INVALID: Data size for sending declared with length: " << len);
        return 0;
    }

    if (w_mctrl.msgno != -1) // most unlikely, unless you use balancing groups
    {
        if (w_mctrl.msgno < 1 || w_mctrl.msgno > MSGNO_SEQ_MAX)
        {
            LOGC(aslog.Error, log << "INVALID forced msgno " << w_mctrl.msgno << ": can be -1 (trap) or <1..." << MSGNO_SEQ_MAX << ">");
            throw CUDTException(MJ_NOTSUP, MN_INVAL);
        }
    }

    int  msttl   = w_mctrl.msgttl;
    bool inorder = w_mctrl.inorder;

    // Sendmsg isn't restricted to the congctl type, however the congctl
    // may want to have something to say here.
    // NOTE: SrtCongestion is also allowed to throw CUDTException() by itself!
    {
        SrtCongestion::TransAPI api = SrtCongestion::STA_MESSAGE;
        CodeMinor               mn  = MN_INVALMSGAPI;
        if (!m_config.bMessageAPI)
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

    if (m_config.bMessageAPI && len > int(m_config.iSndBufSize * m_iMaxSRTPayloadSize))
    {
        LOGC(aslog.Error,
             log << "Message length (" << len << ") exceeds the size of sending buffer: "
                 << (m_config.iSndBufSize * m_iMaxSRTPayloadSize) << ". Use SRTO_SNDBUF if needed.");
        throw CUDTException(MJ_NOTSUP, MN_XSIZE, 0);
    }

    /* XXX
       This might be worth preserving for several occasions, but it
       must be at least conditional because it breaks backward compat.
    if (!m_pCryptoControl || !m_pCryptoControl->isSndEncryptionOK())
    {
        LOGC(aslog.Error, log << "Encryption is required, but the peer did not supply correct credentials. Sending
    rejected."); throw CUDTException(MJ_SETUP, MN_SECURITY, 0);
    }
    */

    UniqueLock sendguard(m_SendLock);

    if (m_pSndBuffer->getCurrBufSize() == 0)
    {
        // delay the EXP timer to avoid mis-fired timeout
        ScopedLock ack_lock(m_RecvAckLock);
        m_tsLastRspAckTime = steady_clock::now();
        m_iReXmitCount   = 1;
    }

    // sndDropTooLate(...) may lock m_RecvAckLock
    // to modify m_pSndBuffer and m_pSndLossList
    const int iPktsTLDropped SRT_ATR_UNUSED = sndDropTooLate();

    int minlen = 1; // Minimum sender buffer space required for STREAM API
    if (m_config.bMessageAPI)
    {
        // For MESSAGE API the minimum outgoing buffer space required is
        // the size that can carry over the whole message as passed here.
        minlen = (len + m_iMaxSRTPayloadSize - 1) / m_iMaxSRTPayloadSize;
    }

    if (sndBuffersLeft() < minlen)
    {
        //>>We should not get here if SRT_ENABLE_TLPKTDROP
        // XXX Check if this needs to be removed, or put to an 'else' condition for m_bTLPktDrop.
        if (!m_config.bSynSending)
            throw CUDTException(MJ_AGAIN, MN_WRAVAIL, 0);

        {
            // wait here during a blocking sending
            UniqueLock sendblock_lock (m_SendBlockLock);

            if (m_config.iSndTimeOut < 0)
            {
                while (stillConnected() && sndBuffersLeft() < minlen && m_bPeerHealth)
                    m_SendBlockCond.wait(sendblock_lock);
            }
            else
            {
                const steady_clock::time_point exptime =
                    steady_clock::now() + milliseconds_from(m_config.iSndTimeOut);
                THREAD_PAUSED();
                while (stillConnected() && sndBuffersLeft() < minlen && m_bPeerHealth)
                {
                    if (!m_SendBlockCond.wait_until(sendblock_lock, exptime))
                        break;
                }
                THREAD_RESUMED();
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
            if (m_config.iSndTimeOut >= 0)
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
            LOGC(aslog.Fatal,
                 log << "IPE: sendmsg: the loop exited, while not enough size, still connected, peer healthy. "
                        "Impossible.");

            return 0;
        }
    }

    // If the sender's buffer is empty,
    // record total time used for sending
    if (m_pSndBuffer->getCurrBufSize() == 0)
    {
        ScopedLock lock(m_StatsLock);
        m_stats.sndDurationCounter = steady_clock::now();
    }

    int size = len;
    if (!m_config.bMessageAPI)
    {
        // For STREAM API it's allowed to send less bytes than the given buffer.
        // Just return how many bytes were actually scheduled for writing.
        // XXX May be reasonable to add a flag that requires that the function
        // not return until the buffer is sent completely.
        size = min(len, sndBuffersLeft() * m_iMaxSRTPayloadSize);
    }

    {
        ScopedLock recvAckLock(m_RecvAckLock);
        // insert the user buffer into the sending list

        int32_t seqno = m_iSndNextSeqNo;
        IF_HEAVY_LOGGING(int32_t orig_seqno = seqno);
        IF_HEAVY_LOGGING(steady_clock::time_point ts_srctime =
                             steady_clock::time_point() + microseconds_from(w_mctrl.srctime));

#if ENABLE_BONDING
        // Check if seqno has been set, in case when this is a group sender.
        // If the sequence is from the past towards the "next sequence",
        // simply return the size, pretending that it has been sent.

        // NOTE: it's assumed that if this is a group member, then
        // an attempt to call srt_sendmsg2 has been rejected, and so
        // the pktseq field has been set by the internal group sender function.
        if (m_parent->m_GroupOf
                && w_mctrl.pktseq != SRT_SEQNO_NONE
                && m_iSndNextSeqNo != SRT_SEQNO_NONE)
        {
            if (CSeqNo::seqcmp(w_mctrl.pktseq, seqno) < 0)
            {
                HLOGC(aslog.Debug, log << CONID() << "sock:SENDING (NOT): group-req %" << w_mctrl.pktseq
                        << " OLDER THAN next expected %" << seqno << " - FAKE-SENDING.");
                return size;
            }
        }
#endif

        // Set this predicted next sequence to the control information.
        // It's the sequence of the FIRST (!) packet from all packets used to send
        // this buffer. Values from this field will be monotonic only if you always
        // have one packet per buffer (as it's in live mode).
        w_mctrl.pktseq = seqno;

        // Now seqno is the sequence to which it was scheduled
        // XXX Conversion from w_mctrl.srctime -> steady_clock::time_point need not be accurrate.
        HLOGC(aslog.Debug, log << CONID() << "buf:SENDING (BEFORE) srctime:"
                << (w_mctrl.srctime ? FormatTime(ts_srctime) : "none")
                << " DATA SIZE: " << size << " sched-SEQUENCE: " << seqno
                << " STAMP: " << BufferStamp(data, size));

        if (w_mctrl.srctime && w_mctrl.srctime < count_microseconds(m_stats.tsStartTime.time_since_epoch()))
        {
            LOGC(aslog.Error,
                log << "Wrong source time was provided. Sending is rejected.");
            throw CUDTException(MJ_NOTSUP, MN_INVALMSGAPI);
        }

        if (w_mctrl.srctime && (!m_config.bMessageAPI || !m_bTsbPd))
        {
            HLOGC(aslog.Warn,
                log << "Source time can only be used with TSBPD and Message API enabled. Using default time instead.");
            w_mctrl.srctime = 0;
        }

        // w_mctrl.seqno is INPUT-OUTPUT value:
        // - INPUT: the current sequence number to be placed for the next scheduled packet
        // - OUTPUT: value of the sequence number to be put on the first packet at the next sendmsg2 call.
        // We need to supply to the output the value that was STAMPED ON THE PACKET,
        // which is seqno. In the output we'll get the next sequence number.
        m_pSndBuffer->addBuffer(data, size, (w_mctrl));
        m_iSndNextSeqNo = w_mctrl.pktseq;
        w_mctrl.pktseq = seqno;

        HLOGC(aslog.Debug, log << CONID() << "buf:SENDING srctime:" << FormatTime(ts_srctime)
              << " size=" << size << " #" << w_mctrl.msgno << " SCHED %" << orig_seqno
              << "(>> %" << seqno << ") !" << BufferStamp(data, size));

        if (sndBuffersLeft() < 1) // XXX Not sure if it should test if any space in the buffer, or as requried.
        {
            // write is not available any more
            uglobal().m_EPoll.update_events(m_SocketID, m_sPollID, SRT_EPOLL_OUT, false);
        }
    }

    // Insert this socket to the snd list if it is not on the list already.
    // m_pSndUList->pop may lock CSndUList::m_ListLock and then m_RecvAckLock
    m_pSndQueue->m_pSndUList->update(this, CSndUList::DONT_RESCHEDULE);

#ifdef SRT_ENABLE_ECN
    // IF there was a packet drop on the sender side, report congestion to the app.
    if (iPktsTLDropped > 0)
    {
        LOGC(aslog.Error, log << "sendmsg2: CONGESTION; reporting error");
        throw CUDTException(MJ_AGAIN, MN_CONGESTION, 0);
    }
#endif /* SRT_ENABLE_ECN */

    HLOGC(aslog.Debug, log << CONID() << "sock:SENDING (END): success, size=" << size);
    return size;
}

int srt::CUDT::recv(char* data, int len)
{
    SRT_MSGCTRL mctrl = srt_msgctrl_default;
    return recvmsg2(data, len, (mctrl));
}

int srt::CUDT::recvmsg(char* data, int len, int64_t& srctime)
{
    SRT_MSGCTRL mctrl = srt_msgctrl_default;
    int res = recvmsg2(data, len, (mctrl));
    srctime = mctrl.srctime;
    return res;
}

// [[using maybe_locked(CUDTGroup::m_GroupLock, m_parent->m_GroupOf != NULL)]]
// GroupLock is applied when this function is called from inside CUDTGroup::recv,
// which is the only case when the m_parent->m_GroupOf is not NULL.
int srt::CUDT::recvmsg2(char* data, int len, SRT_MSGCTRL& w_mctrl)
{
    // Check if the socket is a member of a receiver group.
    // If so, then reading by receiveMessage is disallowed.

#if ENABLE_BONDING
    if (m_parent->m_GroupOf && m_parent->m_GroupOf->isGroupReceiver())
    {
        LOGP(arlog.Error, "recv*: This socket is a receiver group member. Use group ID, NOT socket ID.");
        throw CUDTException(MJ_NOTSUP, MN_INVALMSGAPI, 0);
    }
#endif

    if (!m_bConnected || !m_CongCtl.ready())
        throw CUDTException(MJ_CONNECTION, MN_NOCONN, 0);

    if (len <= 0)
    {
        LOGC(arlog.Error, log << "Length of '" << len << "' supplied to srt_recvmsg.");
        throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);
    }

    if (m_config.bMessageAPI)
        return receiveMessage(data, len, (w_mctrl));

    return receiveBuffer(data, len);
}

size_t srt::CUDT::getAvailRcvBufferSizeLock() const
{
    ScopedLock lck(m_RcvBufferLock);
    return getAvailRcvBufferSizeNoLock();
}

size_t srt::CUDT::getAvailRcvBufferSizeNoLock() const
{
#if ENABLE_NEW_RCVBUFFER
    return m_pRcvBuffer->getAvailSize(m_iRcvLastAck);
#else
    return m_pRcvBuffer->getAvailBufSize();
#endif
}

bool srt::CUDT::isRcvBufferReady() const
{
    ScopedLock lck(m_RcvBufferLock);
#if ENABLE_NEW_RCVBUFFER
    return m_pRcvBuffer->isRcvDataReady(steady_clock::now());
#else
    return m_pRcvBuffer->isRcvDataReady();
#endif
}

// int by_exception: accepts values of CUDTUnited::ErrorHandling:
// - 0 - by return value
// - 1 - by exception
// - 2 - by abort (unused)
int srt::CUDT::receiveMessage(char* data, int len, SRT_MSGCTRL& w_mctrl, int by_exception)
{
    // Recvmsg isn't restricted to the congctl type, it's the most
    // basic method of passing the data. You can retrieve data as
    // they come in, however you need to match the size of the buffer.

    // Note: if by_exception = ERH_RETURN, this would still break it
    // by exception. The intention of by_exception isn't to prevent
    // exceptions here, but to intercept the erroneous situation should
    // it be handled by the caller in a less than general way. As this
    // is only used internally, we state that the problem that would be
    // handled by exception here should not happen, and in case if it does,
    // it's a bug to fix, so the exception is nothing wrong.
    if (!m_CongCtl->checkTransArgs(SrtCongestion::STA_MESSAGE, SrtCongestion::STAD_RECV, data, len, SRT_MSGTTL_INF, false))
        throw CUDTException(MJ_NOTSUP, MN_INVALMSGAPI, 0);

    UniqueLock recvguard (m_RecvLock);
    CSync tscond     (m_RcvTsbPdCond,  recvguard);

    /* XXX DEBUG STUFF - enable when required
       char charbool[2] = {'0', '1'};
       char ptrn [] = "RECVMSG/BEGIN BROKEN 1 CONN 1 CLOSING 1 SYNCR 1 NMSG                                ";
       int pos [] = {21, 28, 38, 46, 53};
       ptrn[pos[0]] = charbool[m_bBroken];
       ptrn[pos[1]] = charbool[m_bConnected];
       ptrn[pos[2]] = charbool[m_bClosing];
       ptrn[pos[3]] = charbool[m_config.m_bSynRecving];
       int wrtlen = sprintf(ptrn + pos[4], "%d", m_pRcvBuffer->getRcvMsgNum());
       strcpy(ptrn + pos[4] + wrtlen, "\n");
       fputs(ptrn, stderr);
    // */

    if (m_bBroken || m_bClosing)
    {
        HLOGC(arlog.Debug, log << CONID() << "receiveMessage: CONNECTION BROKEN - reading from recv buffer just for formality");
        enterCS(m_RcvBufferLock);
#if ENABLE_NEW_RCVBUFFER
        const int res = (m_pRcvBuffer->isRcvDataReady(steady_clock::now()))
            ? m_pRcvBuffer->readMessage(data, len, &w_mctrl)
            : 0;
#else
        const int res = m_pRcvBuffer->readMsg(data, len);
#endif
        leaveCS(m_RcvBufferLock);
        w_mctrl.srctime = 0;

        // Kick TsbPd thread to schedule next wakeup (if running)
        if (m_bTsbPd)
        {
            HLOGP(tslog.Debug, "Ping TSBPD thread to schedule wakeup");
            tscond.notify_one_locked(recvguard);
        }
        else
        {
            HLOGP(tslog.Debug, "NOT pinging TSBPD - not set");
        }

        if (!isRcvBufferReady())
        {
            // read is not available any more
            uglobal().m_EPoll.update_events(m_SocketID, m_sPollID, SRT_EPOLL_IN, false);
        }

        if (res == 0)
        {
            if (!m_config.bMessageAPI && m_bShutdown)
                return 0;
            // Forced to return error instead of throwing exception.
            if (!by_exception)
                return APIError(MJ_CONNECTION, MN_CONNLOST, 0);
            throw CUDTException(MJ_CONNECTION, MN_CONNLOST, 0);
        }
        else
            return res;
    }

#if !ENABLE_NEW_RCVBUFFER
    const int seqdistance = -1;
#endif

    if (!m_config.bSynRecving)
    {
        HLOGC(arlog.Debug, log << CONID() << "receiveMessage: BEGIN ASYNC MODE. Going to extract payload size=" << len);
        enterCS(m_RcvBufferLock);
#if ENABLE_NEW_RCVBUFFER
        const int res = (m_pRcvBuffer->isRcvDataReady(steady_clock::now()))
            ? m_pRcvBuffer->readMessage(data, len, &w_mctrl)
            : 0;
#else
        const int res = m_pRcvBuffer->readMsg(data, len, (w_mctrl), seqdistance);
#endif
        leaveCS(m_RcvBufferLock);
        HLOGC(arlog.Debug, log << CONID() << "AFTER readMsg: (NON-BLOCKING) result=" << res);

        if (res == 0)
        {
            // read is not available any more
            // Kick TsbPd thread to schedule next wakeup (if running)
            if (m_bTsbPd)
            {
                HLOGP(arlog.Debug, "receiveMessage: nothing to read, kicking TSBPD, return AGAIN");
                tscond.notify_one_locked(recvguard);
            }
            else
            {
                HLOGP(arlog.Debug, "receiveMessage: nothing to read, return AGAIN");
            }

            // Shut up EPoll if no more messages in non-blocking mode
            uglobal().m_EPoll.update_events(m_SocketID, m_sPollID, SRT_EPOLL_IN, false);
            // Forced to return 0 instead of throwing exception, in case of AGAIN/READ
            if (!by_exception)
                return 0;
            throw CUDTException(MJ_AGAIN, MN_RDAVAIL, 0);
        }

        if (!isRcvBufferReady())
        {
            // Kick TsbPd thread to schedule next wakeup (if running)
            if (m_bTsbPd)
            {
                HLOGP(arlog.Debug, "receiveMessage: DATA READ, but nothing more - kicking TSBPD.");
                tscond.notify_one_locked(recvguard);
            }
            else
            {
                HLOGP(arlog.Debug, "receiveMessage: DATA READ, but nothing more");
            }

            // Shut up EPoll if no more messages in non-blocking mode
            uglobal().m_EPoll.update_events(m_SocketID, m_sPollID, SRT_EPOLL_IN, false);

            // After signaling the tsbpd for ready data, report the bandwidth.
#if ENABLE_HEAVY_LOGGING
            double bw = Bps2Mbps(int64_t(m_iBandwidth) * m_iMaxSRTPayloadSize );
            HLOGC(arlog.Debug, log << CONID() << "CURRENT BANDWIDTH: " << bw << "Mbps (" << m_iBandwidth << " buffers per second)");
#endif
        }
        return res;
    }

    HLOGC(arlog.Debug, log << CONID() << "receiveMessage: BEGIN SYNC MODE. Going to extract payload size max=" << len);

    int  res     = 0;
    bool timeout = false;
    // Do not block forever, check connection status each 1 sec.
    const steady_clock::duration recv_timeout = m_config.iRcvTimeOut < 0 ? seconds_from(1) : milliseconds_from(m_config.iRcvTimeOut);

    CSync recv_cond (m_RecvDataCond, recvguard);

    do
    {
#if ENABLE_NEW_RCVBUFFER
        if (stillConnected() && !timeout && !m_pRcvBuffer->isRcvDataReady(steady_clock::now()))
#else
        steady_clock::time_point tstime SRT_ATR_UNUSED;
        int32_t seqno;
        if (stillConnected() && !timeout && !m_pRcvBuffer->isRcvDataReady((tstime), (seqno), seqdistance))
#endif
        {
            /* Kick TsbPd thread to schedule next wakeup (if running) */
            if (m_bTsbPd)
            {
                // XXX Experimental, so just inform:
                // Check if the last check of isRcvDataReady has returned any "next time for a packet".
                // If so, then it means that TSBPD has fallen asleep only up to this time, so waking it up
                // would be "spurious". If a new packet comes ahead of the packet which's time is returned
                // in tstime (as TSBPD sleeps up to then), the procedure that receives it is responsible
                // of kicking TSBPD.
                // bool spurious = (tstime != 0);

                HLOGC(tslog.Debug, log << CONID() << "receiveMessage: KICK tsbpd");
                tscond.notify_one_locked(recvguard);
            }

            THREAD_PAUSED();
            do
            {
                // `wait_for(recv_timeout)` wouldn't be correct here. Waiting should be
                // only until the time that is now + timeout since the first moment
                // when this started, or sliced-waiting for 1 second, if timtout is
                // higher than this.
                const steady_clock::time_point exptime = steady_clock::now() + recv_timeout;

                HLOGC(tslog.Debug,
                      log << CONID() << "receiveMessage: fall asleep up to TS=" << FormatTime(exptime)
                          << " lock=" << (&m_RecvLock) << " cond=" << (&m_RecvDataCond));

                if (!recv_cond.wait_until(exptime))
                {
                    if (m_config.iRcvTimeOut >= 0) // otherwise it's "no timeout set"
                        timeout = true;
                    HLOGP(tslog.Debug,
                          "receiveMessage: DATA COND: EXPIRED -- checking connection conditions and rolling again");
                }
                else
                {
                    HLOGP(tslog.Debug, "receiveMessage: DATA COND: KICKED.");
                }
            } while (stillConnected() && !timeout && (!isRcvBufferReady()));
            THREAD_RESUMED();

            HLOGC(tslog.Debug,
                  log << CONID() << "receiveMessage: lock-waiting loop exited: stillConntected=" << stillConnected()
                      << " timeout=" << timeout << " data-ready=" << isRcvBufferReady());
        }

        /* XXX DEBUG STUFF - enable when required
        LOGC(arlog.Debug, "RECVMSG/GO-ON BROKEN " << m_bBroken << " CONN " << m_bConnected
                << " CLOSING " << m_bClosing << " TMOUT " << timeout
                << " NMSG " << m_pRcvBuffer->getRcvMsgNum());
                */

        enterCS(m_RcvBufferLock);
#if ENABLE_NEW_RCVBUFFER
        res = m_pRcvBuffer->readMessage((data), len, &w_mctrl);
#else
        res = m_pRcvBuffer->readMsg((data), len, (w_mctrl), seqdistance);
#endif
        leaveCS(m_RcvBufferLock);
        HLOGC(arlog.Debug, log << CONID() << "AFTER readMsg: (BLOCKING) result=" << res);

        if (m_bBroken || m_bClosing)
        {
            // Forced to return 0 instead of throwing exception.
            if (!by_exception)
                return APIError(MJ_CONNECTION, MN_CONNLOST, 0);
            if (!m_config.bMessageAPI && m_bShutdown)
                return 0;
            throw CUDTException(MJ_CONNECTION, MN_CONNLOST, 0);
        }
        else if (!m_bConnected)
        {
            // Forced to return -1 instead of throwing exception.
            if (!by_exception)
                return APIError(MJ_CONNECTION, MN_NOCONN, 0);
            throw CUDTException(MJ_CONNECTION, MN_NOCONN, 0);
        }
    } while ((res == 0) && !timeout);

    if (!isRcvBufferReady())
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
            tscond.notify_one_locked(recvguard);
        }

        // Shut up EPoll if no more messages in non-blocking mode
        uglobal().m_EPoll.update_events(m_SocketID, m_sPollID, SRT_EPOLL_IN, false);
    }

    // Unblock when required
    // LOGC(tslog.Debug, "RECVMSG/EXIT RES " << res << " RCVTIMEOUT");

    if ((res <= 0) && (m_config.iRcvTimeOut >= 0))
    {
        // Forced to return -1 instead of throwing exception.
        if (!by_exception)
            return APIError(MJ_AGAIN, MN_XMTIMEOUT, 0);
        throw CUDTException(MJ_AGAIN, MN_XMTIMEOUT, 0);
    }

    return res;
}

int64_t srt::CUDT::sendfile(fstream &ifs, int64_t &offset, int64_t size, int block)
{
    if (m_bBroken || m_bClosing)
        throw CUDTException(MJ_CONNECTION, MN_CONNLOST, 0);
    else if (!m_bConnected || !m_CongCtl.ready())
        throw CUDTException(MJ_CONNECTION, MN_NOCONN, 0);

    if (size <= 0 && size != -1)
        return 0;

    if (!m_CongCtl->checkTransArgs(SrtCongestion::STA_FILE, SrtCongestion::STAD_SEND, 0, size, SRT_MSGTTL_INF, false))
        throw CUDTException(MJ_NOTSUP, MN_INVALBUFFERAPI, 0);

    if (!m_pCryptoControl || !m_pCryptoControl->isSndEncryptionOK())
    {
        LOGC(aslog.Error,
             log << "Encryption is required, but the peer did not supply correct credentials. Sending rejected.");
        throw CUDTException(MJ_SETUP, MN_SECURITY, 0);
    }

    ScopedLock sendguard (m_SendLock);

    if (m_pSndBuffer->getCurrBufSize() == 0)
    {
        // delay the EXP timer to avoid mis-fired timeout
        // XXX Lock ???  ScopedLock ack_lock(m_RecvAckLock);
        m_tsLastRspAckTime = steady_clock::now();
        m_iReXmitCount   = 1;
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
            UniqueLock lock(m_SendBlockLock);

            THREAD_PAUSED();
            while (stillConnected() && (sndBuffersLeft() <= 0) && m_bPeerHealth)
                m_SendBlockCond.wait(lock);
            THREAD_RESUMED();
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
            ScopedLock lock(m_StatsLock);
            m_stats.sndDurationCounter = steady_clock::now();
        }

        {
            ScopedLock        recvAckLock(m_RecvAckLock);
            const int64_t sentsize = m_pSndBuffer->addBufferFromFile(ifs, unitsize);

            if (sentsize > 0)
            {
                tosend -= sentsize;
                offset += sentsize;
            }

            if (sndBuffersLeft() <= 0)
            {
                // write is not available any more
                uglobal().m_EPoll.update_events(m_SocketID, m_sPollID, SRT_EPOLL_OUT, false);
            }
        }

        // insert this socket to snd list if it is not on the list yet
        m_pSndQueue->m_pSndUList->update(this, CSndUList::DONT_RESCHEDULE);
    }

    return size - tosend;
}

int64_t srt::CUDT::recvfile(fstream &ofs, int64_t &offset, int64_t size, int block)
{
    if (!m_bConnected || !m_CongCtl.ready())
        throw CUDTException(MJ_CONNECTION, MN_NOCONN, 0);
    else if ((m_bBroken || m_bClosing) && !isRcvBufferReady())
    {
        if (!m_config.bMessageAPI && m_bShutdown)
            return 0;
        throw CUDTException(MJ_CONNECTION, MN_CONNLOST, 0);
    }

    if (size <= 0)
        return 0;

    if (!m_CongCtl->checkTransArgs(SrtCongestion::STA_FILE, SrtCongestion::STAD_RECV, 0, size, SRT_MSGTTL_INF, false))
        throw CUDTException(MJ_NOTSUP, MN_INVALBUFFERAPI, 0);

    if (isOPT_TsbPd())
    {
        LOGC(arlog.Error, log << "Reading from file is incompatible with TSBPD mode and would cause a deadlock\n");
        throw CUDTException(MJ_NOTSUP, MN_INVALBUFFERAPI, 0);
    }

    UniqueLock recvguard(m_RecvLock);

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

        {
            CSync rcond (m_RecvDataCond, recvguard);

            THREAD_PAUSED();
            while (stillConnected() && !isRcvBufferReady())
                rcond.wait();
            THREAD_RESUMED();
        }

        if (!m_bConnected)
            throw CUDTException(MJ_CONNECTION, MN_NOCONN, 0);
        else if ((m_bBroken || m_bClosing) && !isRcvBufferReady())
        {
            if (!m_config.bMessageAPI && m_bShutdown)
                return 0;
            throw CUDTException(MJ_CONNECTION, MN_CONNLOST, 0);
        }

        unitsize = int((torecv > block) ? block : torecv);
        enterCS(m_RcvBufferLock);
        recvsize = m_pRcvBuffer->readBufferToFile(ofs, unitsize);
        leaveCS(m_RcvBufferLock);

        if (recvsize > 0)
        {
            torecv -= recvsize;
            offset += recvsize;
        }
    }

    if (!isRcvBufferReady())
    {
        // read is not available any more
        uglobal().m_EPoll.update_events(m_SocketID, m_sPollID, SRT_EPOLL_IN, false);
    }

    return size - torecv;
}

void srt::CUDT::bstats(CBytePerfMon *perf, bool clear, bool instantaneous)
{
    if (!m_bConnected)
        throw CUDTException(MJ_CONNECTION, MN_NOCONN, 0);
    if (m_bBroken || m_bClosing)
        throw CUDTException(MJ_CONNECTION, MN_CONNLOST, 0);

    const int pktHdrSize = CPacket::HDR_SIZE + CPacket::UDP_HDR_SIZE;
    {
        ScopedLock statsguard(m_StatsLock);

        const steady_clock::time_point currtime = steady_clock::now();

        perf->msTimeStamp          = count_milliseconds(currtime - m_stats.tsStartTime);
        perf->pktSent              = m_stats.sndr.sent.trace.count();
        perf->pktSentUnique        = m_stats.sndr.sentUnique.trace.count();
        perf->pktRecv              = m_stats.rcvr.recvd.trace.count();
        perf->pktRecvUnique        = m_stats.rcvr.recvdUnique.trace.count();

        perf->pktSndLoss           = m_stats.sndr.lost.trace.count();
        perf->pktRcvLoss           = m_stats.rcvr.lost.trace.count();
        perf->pktRetrans           = m_stats.sndr.sentRetrans.trace.count();
        perf->pktRcvRetrans        = m_stats.rcvr.recvdRetrans.trace.count();
        perf->pktSentACK           = m_stats.rcvr.sentAck.trace.count();
        perf->pktRecvACK           = m_stats.sndr.recvdAck.trace.count();
        perf->pktSentNAK           = m_stats.rcvr.sentNak.trace.count();
        perf->pktRecvNAK           = m_stats.sndr.recvdNak.trace.count();
        perf->usSndDuration        = m_stats.sndDuration;
        perf->pktReorderDistance   = m_stats.traceReorderDistance;
        perf->pktReorderTolerance  = m_iReorderTolerance;
        perf->pktRcvAvgBelatedTime = m_stats.traceBelatedTime;
        perf->pktRcvBelated        = m_stats.rcvr.recvdBelated.trace.count();

        perf->pktSndFilterExtra  = m_stats.sndr.sentFilterExtra.trace.count();
        perf->pktRcvFilterExtra  = m_stats.rcvr.recvdFilterExtra.trace.count();
        perf->pktRcvFilterSupply = m_stats.rcvr.suppliedByFilter.trace.count();
        perf->pktRcvFilterLoss   = m_stats.rcvr.lossFilter.trace.count();

        /* perf byte counters include all headers (SRT+UDP+IP) */
        perf->byteSent       = m_stats.sndr.sent.trace.bytesWithHdr();
        perf->byteSentUnique = m_stats.sndr.sentUnique.trace.bytesWithHdr();
        perf->byteRecv       = m_stats.rcvr.recvd.trace.bytesWithHdr();
        perf->byteRecvUnique = m_stats.rcvr.recvdUnique.trace.bytesWithHdr();
        perf->byteRetrans    = m_stats.sndr.sentRetrans.trace.bytesWithHdr();
        perf->byteRcvLoss    = m_stats.rcvr.lost.trace.bytesWithHdr();

        perf->pktSndDrop  = m_stats.sndr.dropped.trace.count();
        perf->pktRcvDrop  = m_stats.rcvr.dropped.trace.count() + m_stats.rcvr.undecrypted.trace.count();
        perf->byteSndDrop = m_stats.sndr.dropped.trace.bytesWithHdr();
        perf->byteRcvDrop = m_stats.rcvr.dropped.trace.bytesWithHdr();
        perf->pktRcvUndecrypt  = m_stats.rcvr.undecrypted.trace.count();
        perf->byteRcvUndecrypt = m_stats.rcvr.undecrypted.trace.bytes();

        perf->pktSentTotal       = m_stats.sndr.sent.total.count();
        perf->pktSentUniqueTotal = m_stats.sndr.sentUnique.total.count();
        perf->pktRecvTotal       = m_stats.rcvr.recvd.total.count();
        perf->pktRecvUniqueTotal = m_stats.rcvr.recvdUnique.total.count();
        perf->pktSndLossTotal    = m_stats.sndr.lost.total.count();
        perf->pktRcvLossTotal    = m_stats.rcvr.lost.total.count();
        perf->pktRetransTotal    = m_stats.sndr.sentRetrans.total.count();
        perf->pktSentACKTotal    = m_stats.rcvr.sentAck.total.count();
        perf->pktRecvACKTotal    = m_stats.sndr.recvdAck.total.count();
        perf->pktSentNAKTotal    = m_stats.rcvr.sentNak.total.count();
        perf->pktRecvNAKTotal    = m_stats.sndr.recvdNak.total.count();
        perf->usSndDurationTotal = m_stats.m_sndDurationTotal;

        perf->byteSentTotal           = m_stats.sndr.sent.total.bytesWithHdr();
        perf->byteSentUniqueTotal     = m_stats.sndr.sentUnique.total.bytesWithHdr();
        perf->byteRecvTotal           = m_stats.rcvr.recvd.total.bytesWithHdr();
        perf->byteRecvUniqueTotal     = m_stats.rcvr.recvdUnique.total.bytesWithHdr();
        perf->byteRetransTotal        = m_stats.sndr.sentRetrans.total.bytesWithHdr();
        perf->pktSndFilterExtraTotal  = m_stats.sndr.sentFilterExtra.total.count();
        perf->pktRcvFilterExtraTotal  = m_stats.rcvr.recvdFilterExtra.total.count();
        perf->pktRcvFilterSupplyTotal = m_stats.rcvr.suppliedByFilter.total.count();
        perf->pktRcvFilterLossTotal   = m_stats.rcvr.lossFilter.total.count();

        perf->byteRcvLossTotal = m_stats.rcvr.lost.total.bytesWithHdr();
        perf->pktSndDropTotal  = m_stats.sndr.dropped.total.count();
        perf->pktRcvDropTotal  = m_stats.rcvr.dropped.total.count() + m_stats.rcvr.undecrypted.total.count();
        // TODO: The payload is dropped. Probably header sizes should not be counted?
        perf->byteSndDropTotal = m_stats.sndr.dropped.total.bytesWithHdr();
        perf->byteRcvDropTotal = m_stats.rcvr.dropped.total.bytesWithHdr() + m_stats.rcvr.undecrypted.total.bytesWithHdr();
        perf->pktRcvUndecryptTotal  = m_stats.rcvr.undecrypted.total.count();
        perf->byteRcvUndecryptTotal = m_stats.rcvr.undecrypted.total.bytes();

        // TODO: The following class members must be protected with a different mutex, not the m_StatsLock.
        const double interval     = (double) count_microseconds(currtime - m_stats.tsLastSampleTime);
        perf->mbpsSendRate        = double(perf->byteSent) * 8.0 / interval;
        perf->mbpsRecvRate        = double(perf->byteRecv) * 8.0 / interval;
        perf->usPktSndPeriod      = (double) count_microseconds(m_tdSendInterval.load());
        perf->pktFlowWindow       = m_iFlowWindowSize.load();
        perf->pktCongestionWindow = (int)m_dCongestionWindow;
        perf->pktFlightSize       = getFlightSpan();
        perf->msRTT               = (double)m_iSRTT / 1000.0;
        perf->msSndTsbPdDelay     = m_bPeerTsbPd ? m_iPeerTsbPdDelay_ms : 0;
        perf->msRcvTsbPdDelay     = isOPT_TsbPd() ? m_iTsbPdDelay_ms : 0;
        perf->byteMSS             = m_config.iMSS;

        perf->mbpsMaxBW = m_config.llMaxBW > 0 ? Bps2Mbps(m_config.llMaxBW)
                        : m_CongCtl.ready()    ? Bps2Mbps(m_CongCtl->sndBandwidth())
                                                : 0;

        if (clear)
        {
            m_stats.sndr.resetTrace();
            m_stats.rcvr.resetTrace();

            m_stats.sndDuration = 0;
            m_stats.tsLastSampleTime = currtime;
        }
    }

    const int64_t availbw = m_iBandwidth == 1 ? m_RcvTimeWindow.getBandwidth() : m_iBandwidth.load();

    perf->mbpsBandwidth = Bps2Mbps(availbw * (m_iMaxSRTPayloadSize + pktHdrSize));

    if (tryEnterCS(m_ConnectionLock))
    {
        if (m_pSndBuffer)
        {
            if (instantaneous)
            {
                /* Get instant SndBuf instead of moving average for application-based Algorithm
                   (such as NAE) in need of fast reaction to network condition changes. */
                perf->pktSndBuf = m_pSndBuffer->getCurrBufSize((perf->byteSndBuf), (perf->msSndBuf));
            }
            else
            {
                perf->pktSndBuf = m_pSndBuffer->getAvgBufSize((perf->byteSndBuf), (perf->msSndBuf));
            }
            perf->byteSndBuf += (perf->pktSndBuf * pktHdrSize);
            perf->byteAvailSndBuf = (m_config.iSndBufSize - perf->pktSndBuf) * m_config.iMSS;
        }
        else
        {
            perf->byteAvailSndBuf = 0;
            perf->pktSndBuf  = 0;
            perf->byteSndBuf = 0;
            perf->msSndBuf   = 0;
        }

        if (m_pRcvBuffer)
        {
            ScopedLock lck(m_RcvBufferLock);
            perf->byteAvailRcvBuf = (int) getAvailRcvBufferSizeNoLock() * m_config.iMSS;
            if (instantaneous) // no need for historical API for Rcv side
            {
                perf->pktRcvBuf = m_pRcvBuffer->getRcvDataSize(perf->byteRcvBuf, perf->msRcvBuf);
            }
            else
            {
                perf->pktRcvBuf = m_pRcvBuffer->getRcvAvgDataSize(perf->byteRcvBuf, perf->msRcvBuf);
            }
        }
        else
        {
            perf->byteAvailRcvBuf = 0;
            perf->pktRcvBuf  = 0;
            perf->byteRcvBuf = 0;
            perf->msRcvBuf   = 0;
        }

        leaveCS(m_ConnectionLock);
    }
    else
    {
        perf->byteAvailSndBuf = 0;
        perf->byteAvailRcvBuf = 0;
        perf->pktSndBuf  = 0;
        perf->byteSndBuf = 0;
        perf->msSndBuf   = 0;
        perf->byteRcvBuf = 0;
        perf->msRcvBuf   = 0;
    }
}

bool srt::CUDT::updateCC(ETransmissionEvent evt, const EventVariant arg)
{
    // Special things that must be done HERE, not in SrtCongestion,
    // because it involves the input buffer in CUDT. It would be
    // slightly dangerous to give SrtCongestion access to it.

    // According to the rules, the congctl should be ready at the same
    // time when the sending buffer. For sanity check, check both first.
    if (!m_CongCtl.ready() || !m_pSndBuffer)
    {
        LOGC(rslog.Error,
             log << CONID() << "updateCC: CAN'T DO UPDATE - congctl " << (m_CongCtl.ready() ? "ready" : "NOT READY")
            << "; sending buffer " << (m_pSndBuffer ? "NOT CREATED" : "created"));

        return false;
    }

    HLOGC(rslog.Debug, log << "updateCC: EVENT:" << TransmissionEventStr(evt));

    if (evt == TEV_INIT)
    {
        // only_input uses:
        // 0: in the beginning and when SRTO_MAXBW was changed
        // 1: SRTO_INPUTBW was changed
        // 2: SRTO_OHEADBW was changed
        EInitEvent only_input = arg.get<EventVariant::INIT>();
        // false = TEV_INIT_RESET: in the beginning, or when MAXBW was changed.

        if (only_input != TEV_INIT_RESET && m_config.llMaxBW)
        {
            HLOGC(rslog.Debug, log << CONID() << "updateCC/TEV_INIT: non-RESET stage and m_config.llMaxBW already set to " << m_config.llMaxBW);
            // Don't change
        }
        else // either m_config.llMaxBW == 0 or only_input == TEV_INIT_RESET
        {
            // Use the values:
            // - if SRTO_MAXBW is >0, use it.
            // - if SRTO_MAXBW == 0, use SRTO_INPUTBW + SRTO_OHEADBW
            // - if SRTO_INPUTBW == 0, pass 0 to requst in-buffer sampling
            // Bytes/s
            const int64_t bw = m_config.llMaxBW != 0 ? m_config.llMaxBW :                   // When used SRTO_MAXBW
                               m_config.llInputBW != 0 ? withOverhead(m_config.llInputBW) : // SRTO_INPUTBW + SRT_OHEADBW
                               0; // When both MAXBW and INPUTBW are 0, request in-buffer sampling

            // Note: setting bw == 0 uses BW_INFINITE value in LiveCC
            m_CongCtl->updateBandwidth(m_config.llMaxBW, bw);

            if (only_input == TEV_INIT_OHEADBW)
            {
                // On updated SRTO_OHEADBW don't change input rate.
                // This only influences the call to withOverhead().
            }
            else
            {
                // No need to calculate input rate if the bandwidth is set
                const bool disable_in_rate_calc = (bw != 0);
                m_pSndBuffer->resetInputRateSmpPeriod(disable_in_rate_calc);
            }

            HLOGC(rslog.Debug,
                  log << CONID() << "updateCC/TEV_INIT: updating BW=" << m_config.llMaxBW
                      << (only_input == TEV_INIT_RESET
                              ? " (UNCHANGED)"
                              : only_input == TEV_INIT_OHEADBW ? " (only Overhead)" : " (updated sampling rate)"));
        }
    }

    // This part is also required only by LiveCC, however not
    // moved there due to that it needs access to CSndBuffer.
    if (evt == TEV_ACK || evt == TEV_LOSSREPORT || evt == TEV_CHECKTIMER || evt == TEV_SYNC)
    {
        // Specific part done when MaxBW is set to 0 (auto) and InputBW is 0.
        // This requests internal input rate sampling.
        if (m_config.llMaxBW == 0 && m_config.llInputBW == 0)
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
            if (inputbw >= 0)
                m_CongCtl->updateBandwidth(0, withOverhead(std::max(m_config.llMinInputBW, inputbw))); // Bytes/sec
        }
    }

    HLOGC(rslog.Debug, log << CONID() << "updateCC: emitting signal for EVENT:" << TransmissionEventStr(evt));

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
        m_tdSendInterval    = microseconds_from((int64_t)m_CongCtl->pktSndPeriod_us());
        m_dCongestionWindow = m_CongCtl->cgWindowSize();
#if ENABLE_HEAVY_LOGGING
        HLOGC(rslog.Debug,
              log << CONID() << "updateCC: updated values from congctl: interval=" << count_microseconds(m_tdSendInterval) << " us ("
                  << "tk (" << m_CongCtl->pktSndPeriod_us() << "us) cgwindow="
                  << std::setprecision(3) << m_dCongestionWindow);
#endif
    }

    HLOGC(rslog.Debug, log << "udpateCC: finished handling for EVENT:" << TransmissionEventStr(evt));

    return true;
}

void srt::CUDT::initSynch()
{
    setupMutex(m_SendBlockLock, "SendBlock");
    setupCond(m_SendBlockCond, "SendBlock");
    setupCond(m_RecvDataCond, "RecvData");
    setupMutex(m_SendLock, "Send");
    setupMutex(m_RecvLock, "Recv");
    setupMutex(m_RcvLossLock, "RcvLoss");
    setupMutex(m_RecvAckLock, "RecvAck");
    setupMutex(m_RcvBufferLock, "RcvBuffer");
    setupMutex(m_ConnectionLock, "Connection");
    setupMutex(m_StatsLock, "Stats");
    setupCond(m_RcvTsbPdCond, "RcvTsbPd");
}

void srt::CUDT::destroySynch()
{
    releaseMutex(m_SendBlockLock);

    // Just in case, signal the CV, on which some
    // other thread is possibly waiting, because a
    // process hanging on a pthread_cond_wait would
    // cause the call to destroy a CV hang up.
    m_SendBlockCond.notify_all();
    releaseCond(m_SendBlockCond);

    m_RecvDataCond.notify_all();
    releaseCond(m_RecvDataCond);
    releaseMutex(m_SendLock);
    releaseMutex(m_RecvLock);
    releaseMutex(m_RcvLossLock);
    releaseMutex(m_RecvAckLock);
    releaseMutex(m_RcvBufferLock);
    releaseMutex(m_ConnectionLock);
    releaseMutex(m_StatsLock);

    m_RcvTsbPdCond.notify_all();
    releaseCond(m_RcvTsbPdCond);
}

void srt::CUDT::releaseSynch()
{
    SRT_ASSERT(m_bClosing);
    // wake up user calls
    CSync::lock_notify_one(m_SendBlockCond, m_SendBlockLock);

    enterCS(m_SendLock);
    leaveCS(m_SendLock);

    // Awake tsbpd() and srt_recv*(..) threads for them to check m_bClosing.
    CSync::lock_notify_one(m_RecvDataCond, m_RecvLock);
    CSync::lock_notify_one(m_RcvTsbPdCond, m_RecvLock);

    // Azquiring m_RcvTsbPdStartupLock protects race in starting
    // the tsbpd() thread in CUDT::processData().
    // Wait for tsbpd() thread to finish.
    enterCS(m_RcvTsbPdStartupLock);
    if (m_RcvTsbPdThread.joinable())
    {
        m_RcvTsbPdThread.join();
    }
    leaveCS(m_RcvTsbPdStartupLock);

    // Acquiring the m_RecvLock it is assumed that both tsbpd()
    // and srt_recv*(..) threads will be aware about the state of m_bClosing.
    enterCS(m_RecvLock);
    leaveCS(m_RecvLock);
}

// [[using locked(m_RcvBufferLock)]];
void srt::CUDT::ackDataUpTo(int32_t ack)
{
    const int acksize SRT_ATR_UNUSED = CSeqNo::seqoff(m_iRcvLastSkipAck, ack);

    HLOGC(xtlog.Debug, log << "ackDataUpTo: %" << m_iRcvLastSkipAck << " -> %" << ack
            << " (" << acksize << " packets)");

    m_iRcvLastAck = ack;
    m_iRcvLastSkipAck = ack;

#if !ENABLE_NEW_RCVBUFFER
    // NOTE: This is new towards UDT and prevents spurious
    // wakeup of select/epoll functions when no new packets
    // were signed off for extraction.
    if (acksize > 0)
    {
        m_pRcvBuffer->ackData(acksize);
    }
#endif
}

#if ENABLE_BONDING && ENABLE_NEW_RCVBUFFER
void srt::CUDT::dropToGroupRecvBase() {
    int32_t group_recv_base = SRT_SEQNO_NONE;
    if (m_parent->m_GroupOf)
    {
        // Check is first done before locking to avoid unnecessary
        // mutex locking. The condition for this field is that it
        // can be either never set, already reset, or ever set
        // and possibly dangling. The re-check after lock eliminates
        // the dangling case.
        ScopedLock glock (uglobal().m_GlobControlLock);

        // Note that getRcvBaseSeqNo() will lock m_GroupOf->m_GroupLock,
        // but this is an intended order.
        if (m_parent->m_GroupOf)
            group_recv_base = m_parent->m_GroupOf->getRcvBaseSeqNo();
    }
    if (group_recv_base == SRT_SEQNO_NONE)
        return;

    ScopedLock lck(m_RcvBufferLock);
    int cnt = rcvDropTooLateUpTo(CSeqNo::incseq(group_recv_base));
    if (cnt > 0)
    {
        HLOGC(grlog.Debug,
              log << "dropToGroupRecvBase: " << CONID() << " dropped " << cnt << " packets before ACK: group_recv_base="
                  << group_recv_base << " m_iRcvLastSkipAck=" << m_iRcvLastSkipAck
                  << " m_iRcvCurrSeqNo=" << m_iRcvCurrSeqNo << " m_bTsbPd=" << m_bTsbPd);
    }
}
#endif

namespace srt {
#if ENABLE_HEAVY_LOGGING
static void DebugAck(string hdr, int prev, int ack)
{
    if (!prev)
    {
        HLOGC(xtlog.Debug, log << hdr << "ACK " << ack);
        return;
    }

    prev     = CSeqNo::incseq(prev);
    int diff = CSeqNo::seqoff(prev, ack);
    if (diff < 0)
    {
        HLOGC(xtlog.Debug, log << hdr << "ACK ERROR: " << prev << "-" << ack << "(diff " << diff << ")");
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
    HLOGC(xtlog.Debug, log << hdr << "ACK (" << (diff + 1) << "): " << ackv.str() << ack);
}
#else
static inline void DebugAck(string, int, int) {}
#endif
}

void srt::CUDT::sendCtrl(UDTMessageType pkttype, const int32_t* lparam, void* rparam, int size)
{
    CPacket ctrlpkt;
    setPacketTS(ctrlpkt, steady_clock::now());

    int nbsent        = 0;

    switch (pkttype)
    {
    case UMSG_ACK: // 010 - Acknowledgement
    {
        nbsent = sendCtrlAck(ctrlpkt, size);
        break;
    }

    case UMSG_ACKACK: // 110 - Acknowledgement of Acknowledgement
        ctrlpkt.pack(pkttype, lparam);
        ctrlpkt.m_iID = m_PeerID;
        nbsent        = m_pSndQueue->sendto(m_PeerAddr, ctrlpkt);

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
            nbsent        = m_pSndQueue->sendto(m_PeerAddr, ctrlpkt);

            enterCS(m_StatsLock);
            m_stats.rcvr.sentNak.count(1);
            leaveCS(m_StatsLock);
        }
        // Call with no arguments - get loss list from internal data.
        else if (m_pRcvLossList->getLossLength() > 0)
        {
            ScopedLock lock(m_RcvLossLock);
            // this is periodically NAK report; make sure NAK cannot be sent back too often

            // read loss list from the local receiver loss list
            int32_t *data = new int32_t[m_iMaxSRTPayloadSize / 4];
            int      losslen;
            m_pRcvLossList->getLossArray(data, losslen, m_iMaxSRTPayloadSize / 4);

            if (0 < losslen)
            {
                ctrlpkt.pack(pkttype, NULL, data, losslen * 4);
                ctrlpkt.m_iID = m_PeerID;
                nbsent        = m_pSndQueue->sendto(m_PeerAddr, ctrlpkt);

                enterCS(m_StatsLock);
                m_stats.rcvr.sentNak.count(1);
                leaveCS(m_StatsLock);
            }

            delete[] data;
        }

        // update next NAK time, which should wait enough time for the retansmission, but not too long
        m_tdNAKInterval = microseconds_from(m_iSRTT + 4 * m_iRTTVar);

        // Fix the NAKreport period according to the congctl
        m_tdNAKInterval =
            microseconds_from(m_CongCtl->updateNAKInterval(count_microseconds(m_tdNAKInterval),
                                                                      m_RcvTimeWindow.getPktRcvSpeed(),
                                                                      m_pRcvLossList->getLossLength()));

        // This is necessary because a congctl need not wish to define
        // its own minimum interval, in which case the default one is used.
        if (m_tdNAKInterval < m_tdMinNakInterval)
            m_tdNAKInterval = m_tdMinNakInterval;

        break;
    }

    case UMSG_CGWARNING: // 100 - Congestion Warning
        ctrlpkt.pack(pkttype);
        ctrlpkt.m_iID = m_PeerID;
        nbsent        = m_pSndQueue->sendto(m_PeerAddr, ctrlpkt);

        m_tsLastWarningTime = steady_clock::now();

        break;

    case UMSG_KEEPALIVE: // 001 - Keep-alive
        ctrlpkt.pack(pkttype);
        ctrlpkt.m_iID = m_PeerID;
        nbsent        = m_pSndQueue->sendto(m_PeerAddr, ctrlpkt);

        break;

    case UMSG_HANDSHAKE: // 000 - Handshake
        ctrlpkt.pack(pkttype, NULL, rparam, sizeof(CHandShake));
        ctrlpkt.m_iID = m_PeerID;
        nbsent        = m_pSndQueue->sendto(m_PeerAddr, ctrlpkt);

        break;

    case UMSG_SHUTDOWN: // 101 - Shutdown
        if (m_PeerID == 0) // Dont't send SHUTDOWN if we don't know peer ID.
            break;
        ctrlpkt.pack(pkttype);
        ctrlpkt.m_iID = m_PeerID;
        nbsent        = m_pSndQueue->sendto(m_PeerAddr, ctrlpkt);

        break;

    case UMSG_DROPREQ: // 111 - Msg drop request
        ctrlpkt.pack(pkttype, lparam, rparam, 8);
        ctrlpkt.m_iID = m_PeerID;
        nbsent        = m_pSndQueue->sendto(m_PeerAddr, ctrlpkt);

        break;

    case UMSG_PEERERROR: // 1000 - acknowledge the peer side a special error
        ctrlpkt.pack(pkttype, lparam);
        ctrlpkt.m_iID = m_PeerID;
        nbsent        = m_pSndQueue->sendto(m_PeerAddr, ctrlpkt);

        break;

    case UMSG_EXT: // 0x7FFF - Resevered for future use
        break;

    default:
        break;
    }

    // Fix keepalive
    if (nbsent)
        m_tsLastSndTime.store(steady_clock::now());
}

int srt::CUDT::sendCtrlAck(CPacket& ctrlpkt, int size)
{
    SRT_ASSERT(ctrlpkt.getMsgTimeStamp() != 0);
    int32_t ack;    // First unacknowledged packet seqnuence number (acknowledge up to ack).
    int nbsent = 0;
    int local_prevack = 0;

#if ENABLE_HEAVY_LOGGING
    struct SaveBack
    {
        int& target;
        const int& source;

        ~SaveBack() { target = source; }
    } l_saveback = { m_iDebugPrevLastAck, m_iRcvLastAck };
    (void)l_saveback; // kill compiler warning: unused variable `l_saveback` [-Wunused-variable]

    local_prevack = m_iDebugPrevLastAck;

    string reason = "first lost"; // just for "a reason" of giving particular % for ACK
#endif

#if ENABLE_BONDING && ENABLE_NEW_RCVBUFFER
    dropToGroupRecvBase();
#endif

    {
        // If there is no loss, the ACK is the current largest sequence number plus 1;
        // Otherwise it is the smallest sequence number in the receiver loss list.
        ScopedLock lock(m_RcvLossLock);
        // TODO: Consider the Fresh Loss list as well!!!
        ack = m_pRcvLossList->getFirstLostSeq();
    }

    // We don't need to check the length prematurely,
    // if length is 0, this will return SRT_SEQNO_NONE.
    // If so happened, simply use the latest received pkt + 1.
    if (ack == SRT_SEQNO_NONE)
    {
        ack = CSeqNo::incseq(m_iRcvCurrSeqNo);
        IF_HEAVY_LOGGING(reason = "expected next");
    }

    if (m_iRcvLastAckAck == ack)
    {
        HLOGC(xtlog.Debug, log << "sendCtrl(UMSG_ACK): last ACK %" << ack << "(" << reason << ") == last ACKACK");
        return nbsent;
    }

    // send out a lite ACK
    // to save time on buffer processing and bandwidth/AS measurement, a lite ACK only feeds back an ACK number
    if (size == SEND_LITE_ACK)
    {
        ctrlpkt.pack(UMSG_ACK, NULL, &ack, size);
        ctrlpkt.m_iID = m_PeerID;
        nbsent = m_pSndQueue->sendto(m_PeerAddr, ctrlpkt);
        DebugAck("sendCtrl(lite):" + CONID(), local_prevack, ack);
        return nbsent;
    }

    // There are new received packets to acknowledge, update related information.
    /* tsbpd thread may also call ackData when skipping packet so protect code */
    UniqueLock bufflock(m_RcvBufferLock);

    // IF ack %> m_iRcvLastAck
    if (CSeqNo::seqcmp(ack, m_iRcvLastAck) > 0)
    {
        ackDataUpTo(ack);

#if ENABLE_BONDING
#if ENABLE_NEW_RCVBUFFER
        const int32_t group_read_seq = m_pRcvBuffer->getFirstReadablePacketInfo(steady_clock::now()).seqno;
#else
        const int32_t group_read_seq = CSeqNo::decseq(ack);
#endif
#endif

        InvertedLock un_bufflock (m_RcvBufferLock);

#if ENABLE_BONDING
        // This actually should be done immediately after the ACK pointers were
        // updated in this socket, but it can't be done inside this function due
        // to being run under a lock.

        // At this moment no locks are applied. The only lock used so far
        // was m_RcvBufferLock, but this was lifed above. At this moment
        // it is safe to apply any locks here. This function is affined
        // to CRcvQueue::worker thread, so it is free to apply locks as
        // required in the defined order. At present we only need the lock
        // on m_GlobControlLock to prevent the group from being deleted
        // in the meantime
        if (m_parent->m_GroupOf)
        {
            // Check is first done before locking to avoid unnecessary
            // mutex locking. The condition for this field is that it
            // can be either never set, already reset, or ever set
            // and possibly dangling. The re-check after lock eliminates
            // the dangling case.
            ScopedLock glock (uglobal().m_GlobControlLock);

            // Note that updateLatestRcv will lock m_GroupOf->m_GroupLock,
            // but this is an intended order.
            if (m_parent->m_GroupOf)
            {
                // A group may need to update the parallelly used idle links,
                // should it have any. Pass the current socket position in order
                // to skip it from the group loop.
                m_parent->m_GroupOf->updateLatestRcv(m_parent);
            }
        }
#endif
        IF_HEAVY_LOGGING(int32_t oldack = m_iRcvLastSkipAck);

        // If TSBPD is enabled, then INSTEAD OF signaling m_RecvDataCond,
        // signal m_RcvTsbPdCond. This will kick in the tsbpd thread, which
        // will signal m_RecvDataCond when there's time to play for particular
        // data packet.
        HLOGC(xtlog.Debug, log << "ACK: clip %" << oldack << "-%" << ack
            << ", REVOKED " << CSeqNo::seqoff(ack, m_iRcvLastAck) << " from RCV buffer");

        if (m_bTsbPd)
        {
            /* Newly acknowledged data, signal TsbPD thread */
            CUniqueSync tslcc (m_RecvLock, m_RcvTsbPdCond);
            // m_bTsbPdAckWakeup is protected by m_RecvLock in the tsbpd() thread
            if (m_bTsbPdAckWakeup)
                tslcc.notify_one();
        }
        else
        {
            {
                CUniqueSync rdcc (m_RecvLock, m_RecvDataCond);

#if ENABLE_NEW_RCVBUFFER
                // Locks m_RcvBufferLock, which is unlocked above by InvertedLock un_bufflock.
                // Must check read-readiness under m_RecvLock to protect the epoll from concurrent changes in readBuffer()
                if (isRcvBufferReady())
#endif
                {
                    if (m_config.bSynRecving)
                    {
                        // signal a waiting "recv" call if there is any data available
                        rdcc.notify_one();
                    }
                    // acknowledge any waiting epolls to read
                    // fix SRT_EPOLL_IN event loss but rcvbuffer still have data：
                    // 1. user call receive/receivemessage(about line number:6482)
                    // 2. after read/receive, if rcvbuffer is empty, will set SRT_EPOLL_IN event to false
                    // 3. but if we do not do some lock work here, will cause some sync problems between threads:
                    //      (1) user thread: call receive/receivemessage
                    //      (2) user thread: read data
                    //      (3) user thread: no data in rcvbuffer, set SRT_EPOLL_IN event to false
                    //      (4) receive thread: receive data and set SRT_EPOLL_IN to true
                    //      (5) user thread: set SRT_EPOLL_IN to false
                    // 4. so , m_RecvLock must be used here to protect epoll event
                    uglobal().m_EPoll.update_events(m_SocketID, m_sPollID, SRT_EPOLL_IN, true);
                }
            }
#if ENABLE_BONDING
            if (group_read_seq != SRT_SEQNO_NONE && m_parent->m_GroupOf)
            {
                // See above explanation for double-checking
                ScopedLock glock (uglobal().m_GlobControlLock);

                if (m_parent->m_GroupOf)
                {
                    // The current "APP reader" needs to simply decide as to whether
                    // the next CUDTGroup::recv() call should return with no blocking or not.
                    // When the group is read-ready, it should update its pollers as it sees fit.
                    m_parent->m_GroupOf->updateReadState(m_SocketID, group_read_seq);
                }
            }
#endif
            CGlobEvent::triggerEvent();
        }
    }
    else if (ack == m_iRcvLastAck)
    {
        // If the ACK was just sent already AND elapsed time did not exceed RTT,
        if ((steady_clock::now() - m_tsLastAckTime) <
            (microseconds_from(m_iSRTT + 4 * m_iRTTVar)))
        {
            HLOGC(xtlog.Debug, log << "sendCtrl(UMSG_ACK): ACK %" << ack << " just sent - too early to repeat");
            return nbsent;
        }
    }
    else
    {
        // Not possible (m_iRcvCurrSeqNo+1 <% m_iRcvLastAck ?)
        LOGC(xtlog.Error, log << "sendCtrl(UMSG_ACK): IPE: curr %" << ack
            << " <% last %" << m_iRcvLastAck);
        return nbsent;
    }

    // [[using assert( ack >= m_iRcvLastAck && is_periodic_ack ) ]];
    // [[using locked(m_RcvBufferLock)]];

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
        m_iAckSeqNo = CAckNo::incack(m_iAckSeqNo);
        data[ACKD_RCVLASTACK] = m_iRcvLastAck;
        data[ACKD_RTT] = m_iSRTT;
        data[ACKD_RTTVAR] = m_iRTTVar;
        data[ACKD_BUFFERLEFT] = (int) getAvailRcvBufferSizeNoLock();
        // a minimum flow window of 2 is used, even if buffer is full, to break potential deadlock
        if (data[ACKD_BUFFERLEFT] < 2)
            data[ACKD_BUFFERLEFT] = 2;

        if (steady_clock::now() - m_tsLastAckTime > m_tdACKInterval)
        {
            int rcvRate;
            int ctrlsz = ACKD_TOTAL_SIZE_UDTBASE * ACKD_FIELD_SIZE; // Minimum required size

            data[ACKD_RCVSPEED] = m_RcvTimeWindow.getPktRcvSpeed((rcvRate));
            data[ACKD_BANDWIDTH] = m_RcvTimeWindow.getBandwidth();

            //>>Patch while incompatible (1.0.2) receiver floating around
            if (m_uPeerSrtVersion == SrtVersion(1, 0, 2))
            {
                data[ACKD_RCVRATE] = rcvRate;                                     // bytes/sec
                data[ACKD_XMRATE_VER102_ONLY] = data[ACKD_BANDWIDTH] * m_iMaxSRTPayloadSize; // bytes/sec
                ctrlsz = ACKD_FIELD_SIZE * ACKD_TOTAL_SIZE_VER102_ONLY;
            }
            else if (m_uPeerSrtVersion >= SrtVersion(1, 0, 3))
            {
                // Normal, currently expected version.
                data[ACKD_RCVRATE] = rcvRate; // bytes/sec
                ctrlsz = ACKD_FIELD_SIZE * ACKD_TOTAL_SIZE_VER101;
            }
            // ELSE: leave the buffer with ...UDTBASE size.

            ctrlpkt.pack(UMSG_ACK, &m_iAckSeqNo, data, ctrlsz);
            m_tsLastAckTime = steady_clock::now();
        }
        else
        {
            ctrlpkt.pack(UMSG_ACK, &m_iAckSeqNo, data, ACKD_FIELD_SIZE * ACKD_TOTAL_SIZE_SMALL);
        }

        ctrlpkt.m_iID = m_PeerID;
        setPacketTS(ctrlpkt, steady_clock::now());
        nbsent = m_pSndQueue->sendto(m_PeerAddr, ctrlpkt);
        DebugAck("sendCtrl(UMSG_ACK): " + CONID(), local_prevack, ack);

        m_ACKWindow.store(m_iAckSeqNo, m_iRcvLastAck);

        enterCS(m_StatsLock);
        m_stats.rcvr.sentAck.count(1);
        leaveCS(m_StatsLock);
    }
    else
    {
        HLOGC(xtlog.Debug, log << "sendCtrl(UMSG_ACK): " << CONID() << "ACK %" << m_iRcvLastAck
            << " <=%  ACKACK %" << m_iRcvLastAckAck << " - NOT SENDING ACK");
    }

    return nbsent;
}

void srt::CUDT::updateSndLossListOnACK(int32_t ackdata_seqno)
{
#if ENABLE_BONDING
    // This is for the call of CSndBuffer::getMsgNoAt that returns
    // this value as a notfound-trap.
    int32_t msgno_at_last_acked_seq = SRT_MSGNO_CONTROL;
    bool is_group = m_parent->m_GroupOf;
#endif

    // Update sender's loss list and acknowledge packets in the sender's buffer
    {
        // m_RecvAckLock protects sender's loss list and epoll
        ScopedLock ack_lock(m_RecvAckLock);

        const int offset = CSeqNo::seqoff(m_iSndLastDataAck, ackdata_seqno);
        // IF distance between m_iSndLastDataAck and ack is nonempty...
        if (offset <= 0)
            return;

        // update sending variables
        m_iSndLastDataAck = ackdata_seqno;

#if ENABLE_BONDING
        if (is_group)
        {
            // Get offset-1 because 'offset' points actually to past-the-end
            // of the sender buffer. We have already checked that offset is
            // at least 1.
            msgno_at_last_acked_seq = m_pSndBuffer->getMsgNoAt(offset-1);
            // Just keep this value prepared; it can't be updated exactly right
            // now because accessing the group needs some locks to be applied
            // with preserved the right locking order.
        }
#endif

        // remove any loss that predates 'ack' (not to be considered loss anymore)
        m_pSndLossList->removeUpTo(CSeqNo::decseq(m_iSndLastDataAck));

        // acknowledge the sending buffer (remove data that predate 'ack')
        m_pSndBuffer->ackData(offset);

        // acknowledde any waiting epolls to write
        uglobal().m_EPoll.update_events(m_SocketID, m_sPollID, SRT_EPOLL_OUT, true);
        CGlobEvent::triggerEvent();
    }

#if ENABLE_BONDING
    if (is_group)
    {
        // m_RecvAckLock is ordered AFTER m_GlobControlLock, so this can only
        // be done now that m_RecvAckLock is unlocked.
        ScopedLock glock (uglobal().m_GlobControlLock);
        if (m_parent->m_GroupOf)
        {
            HLOGC(inlog.Debug, log << "ACK: acking group sender buffer for #" << msgno_at_last_acked_seq);

            // Guard access to m_iSndAckedMsgNo field
            // Note: This can't be done inside CUDTGroup::ackMessage
            // because this function is also called from CUDT::sndDropTooLate
            // called from CUDT::sendmsg2 called from CUDTGroup::send, which
            // applies the lock on m_GroupLock already.
            ScopedLock glk (*m_parent->m_GroupOf->exp_groupLock());

            // NOTE: ackMessage also accepts and ignores the trap representation
            // which is SRT_MSGNO_CONTROL.
            m_parent->m_GroupOf->ackMessage(msgno_at_last_acked_seq);
        }
    }
#endif

    // insert this socket to snd list if it is not on the list yet
    const steady_clock::time_point currtime = steady_clock::now();
    m_pSndQueue->m_pSndUList->update(this, CSndUList::DONT_RESCHEDULE, currtime);

    if (m_config.bSynSending)
    {
        CSync::lock_notify_one(m_SendBlockCond, m_SendBlockLock);
    }

    // record total time used for sending
    enterCS(m_StatsLock);
    m_stats.sndDuration += count_microseconds(currtime - m_stats.sndDurationCounter);
    m_stats.m_sndDurationTotal += count_microseconds(currtime - m_stats.sndDurationCounter);
    m_stats.sndDurationCounter = currtime;
    leaveCS(m_StatsLock);
}

void srt::CUDT::processCtrlAck(const CPacket &ctrlpkt, const steady_clock::time_point& currtime)
{
    const int32_t* ackdata       = (const int32_t*)ctrlpkt.m_pcData;
    const int32_t  ackdata_seqno = ackdata[ACKD_RCVLASTACK];

    // Check the value of ACK in case when it was some rogue peer
    if (ackdata_seqno < 0)
    {
        // This embraces all cases when the most significant bit is set,
        // as the variable is of a signed type. So, SRT_SEQNO_NONE is
        // included, but it also triggers for any other kind of invalid value.
        // This check MUST BE DONE before making any operation on this number.
        LOGC(inlog.Error, log << CONID() << "ACK: IPE/EPE: received invalid ACK value: " << ackdata_seqno
                << " " << std::hex << ackdata_seqno << " (IGNORED)");
        return;
    }

    const bool isLiteAck = ctrlpkt.getLength() == (size_t)SEND_LITE_ACK;
    HLOGC(inlog.Debug,
          log << CONID() << "ACK covers: " << m_iSndLastDataAck << " - " << ackdata_seqno << " [ACK=" << m_iSndLastAck
              << "]" << (isLiteAck ? "[LITE]" : "[FULL]"));

    updateSndLossListOnACK(ackdata_seqno);

    // Process a lite ACK
    if (isLiteAck)
    {
        if (CSeqNo::seqcmp(ackdata_seqno, m_iSndLastAck) >= 0)
        {
            ScopedLock ack_lock(m_RecvAckLock);
            m_iFlowWindowSize = m_iFlowWindowSize - CSeqNo::seqoff(m_iSndLastAck, ackdata_seqno);
            m_iSndLastAck = ackdata_seqno;

            // TODO: m_tsLastRspAckTime should be protected with m_RecvAckLock
            // because the sendmsg2 may want to change it at the same time.
            m_tsLastRspAckTime = currtime;
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
        if ((currtime - m_SndLastAck2Time > microseconds_from(COMM_SYN_INTERVAL_US)) || (ack_seqno == m_iSndLastAck2))
        {
            sendCtrl(UMSG_ACKACK, &ack_seqno);
            m_iSndLastAck2       = ack_seqno;
            m_SndLastAck2Time = currtime;
        }
    }

    //
    // Begin of the new code with TLPKTDROP.
    //

    // Protect packet retransmission
    enterCS(m_RecvAckLock);

    // Check the validation of the ack
    if (CSeqNo::seqcmp(ackdata_seqno, CSeqNo::incseq(m_iSndCurrSeqNo)) > 0)
    {
        leaveCS(m_RecvAckLock);
        // this should not happen: attack or bug
        LOGC(gglog.Error,
                log << CONID() << "ATTACK/IPE: incoming ack seq " << ackdata_seqno << " exceeds current "
                    << m_iSndCurrSeqNo << " by " << (CSeqNo::seqoff(m_iSndCurrSeqNo, ackdata_seqno) - 1) << "!");
        m_bBroken        = true;
        m_iBrokenCounter = 0;
        return;
    }

    if (CSeqNo::seqcmp(ackdata_seqno, m_iSndLastAck) >= 0)
    {
        // Update Flow Window Size, must update before and together with m_iSndLastAck
        m_iFlowWindowSize = ackdata[ACKD_BUFFERLEFT];
        m_iSndLastAck     = ackdata_seqno;
        m_tsLastRspAckTime  = currtime;
        m_iReXmitCount    = 1; // Reset re-transmit count since last ACK
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
        leaveCS(m_RecvAckLock);
        return;
    }
    m_iSndLastFullAck = ackdata_seqno;

    //
    // END of the new code with TLPKTDROP
    //
    leaveCS(m_RecvAckLock);
#if ENABLE_BONDING
    if (m_parent->m_GroupOf)
    {
        ScopedLock glock (uglobal().m_GlobControlLock);
        if (m_parent->m_GroupOf)
        {
            // Will apply m_GroupLock, ordered after m_GlobControlLock.
            // m_GlobControlLock is necessary for group existence.
            m_parent->m_GroupOf->updateWriteState();
        }
    }
#endif

    size_t acksize   = ctrlpkt.getLength(); // TEMPORARY VALUE FOR CHECKING
    bool   wrongsize = 0 != (acksize % ACKD_FIELD_SIZE);
    acksize          = acksize / ACKD_FIELD_SIZE; // ACTUAL VALUE

    if (wrongsize)
    {
        // Issue a log, but don't do anything but skipping the "odd" bytes from the payload.
        LOGC(inlog.Warn,
             log << CONID() << "Received UMSG_ACK payload is not evened up to 4-byte based field size - cutting to "
                 << acksize << " fields");
    }

    // Start with checking the base size.
    if (acksize < ACKD_TOTAL_SIZE_SMALL)
    {
        LOGC(inlog.Warn, log << CONID() << "Invalid ACK size " << acksize << " fields - less than minimum required!");
        // Ack is already interpreted, just skip further parts.
        return;
    }
    // This check covers fields up to ACKD_BUFFERLEFT.

    // Extract RTT estimate and RTTVar from the ACK packet.
    const int rtt    = ackdata[ACKD_RTT];
    const int rttvar = ackdata[ACKD_RTTVAR];

    // Update the values of smoothed RTT and the variation in RTT samples
    // on subsequent RTT estimates extracted from the ACK packets
    // (during transmission).
    if (m_bIsFirstRTTReceived)
    {
        // Suppose transmission is bidirectional if sender is also receiving
        // data packets.
        enterCS(m_StatsLock);
        const bool bPktsReceived = m_stats.rcvr.recvd.total.count() != 0;
        leaveCS(m_StatsLock);

        if (bPktsReceived)  // Transmission is bidirectional.
        {
            // RTT value extracted from the ACK packet (rtt) is already smoothed
            // RTT obtained at the receiver side. Apply EWMA anyway for the second
            // time on the sender side. Ignore initial values which might arrive
            // after the smoothed RTT on the sender side has been
            // reset to the very first RTT sample received from the receiver.
            // TODO: The case of bidirectional transmission requires further
            // improvements and testing. Double smoothing is applied here to be
            // consistent with the previous behavior.
            if (rtt != INITIAL_RTT || rttvar != INITIAL_RTTVAR)
            {
                int iSRTT = m_iSRTT.load(), iRTTVar = m_iRTTVar.load();
                iRTTVar = avg_iir<4>(iRTTVar, abs(rtt - iSRTT));
                iSRTT   = avg_iir<8>(iSRTT, rtt);
                m_iSRTT = iSRTT;
                m_iRTTVar = iRTTVar;
            }
        }
        else  // Transmission is unidirectional.
        {
            // Simply take the values of smoothed RTT and RTT variance from
            // the ACK packet.
            m_iSRTT   = rtt;
            m_iRTTVar = rttvar;
        }
    }
    // Reset the value of smoothed RTT to the first real RTT estimate extracted
    // from an ACK after initialization (at the beginning of transmission).
    // In case of resumed connection over the same network, the very first RTT
    // value sent within an ACK will be taken from cache and equal to previous
    // connection's final smoothed RTT value. The reception of such a value
    // will also trigger the smoothed RTT reset at the sender side.
    else if (rtt != INITIAL_RTT && rttvar != INITIAL_RTTVAR)
    {
        m_iSRTT               = rtt;
        m_iRTTVar             = rttvar;
        m_bIsFirstRTTReceived = true;
    }

#if SRT_DEBUG_RTT
    s_rtt_trace.trace(currtime, "ACK", rtt, rttvar, m_bIsFirstRTTReceived,
                      m_stats.recvTotal, m_iSRTT, m_iRTTVar);
#endif

    /* Version-dependent fields:
     * Original UDT (total size: ACKD_TOTAL_SIZE_SMALL):
     *   ACKD_RCVLASTACK
     *   ACKD_RTT
     *   ACKD_RTTVAR
     *   ACKD_BUFFERLEFT
     * Additional UDT fields, not always attached:
     *   ACKD_RCVSPEED
     *   ACKD_BANDWIDTH
     * SRT extension since v1.0.1:
     *   ACKD_RCVRATE
     * SRT extension in v1.0.2 only:
     *   ACKD_XMRATE_VER102_ONLY
     */

    if (acksize > ACKD_TOTAL_SIZE_SMALL)
    {
        // This means that ACKD_RCVSPEED and ACKD_BANDWIDTH fields are available.
        int pktps     = ackdata[ACKD_RCVSPEED];
        int bandwidth = ackdata[ACKD_BANDWIDTH];
        int bytesps;

        /* SRT v1.0.2 Bytes-based stats: bandwidth (pcData[ACKD_XMRATE_VER102_ONLY]) and delivery rate (pcData[ACKD_RCVRATE]) in
         * bytes/sec instead of pkts/sec */
        /* SRT v1.0.3 Bytes-based stats: only delivery rate (pcData[ACKD_RCVRATE]) in bytes/sec instead of pkts/sec */
        if (acksize > ACKD_TOTAL_SIZE_UDTBASE)
            bytesps = ackdata[ACKD_RCVRATE];
        else
            bytesps = pktps * m_iMaxSRTPayloadSize;

        m_iBandwidth        = avg_iir<8>(m_iBandwidth.load(), bandwidth);
        m_iDeliveryRate     = avg_iir<8>(m_iDeliveryRate.load(), pktps);
        m_iByteDeliveryRate = avg_iir<8>(m_iByteDeliveryRate.load(), bytesps);

        // Update Estimated Bandwidth and packet delivery rate
        // m_iRcvRate = m_iDeliveryRate;
        // ^^ This has been removed because with the SrtCongestion class
        // instead of reading the m_iRcvRate local field this will read
        // cudt->deliveryRate() instead.
    }

    checkSndTimers(REGEN_KM);
    updateCC(TEV_ACK, EventVariant(ackdata_seqno));

    enterCS(m_StatsLock);
    m_stats.sndr.recvdAck.count(1);
    leaveCS(m_StatsLock);
}

void srt::CUDT::processCtrlAckAck(const CPacket& ctrlpkt, const time_point& tsArrival)
{
    int32_t ack = 0;

    // Calculate RTT estimate on the receiver side based on ACK/ACKACK pair.
    const int rtt = m_ACKWindow.acknowledge(ctrlpkt.getAckSeqNo(), ack, tsArrival);

    if (rtt == -1)
    {
        if (ctrlpkt.getAckSeqNo() > (m_iAckSeqNo - static_cast<int>(ACK_WND_SIZE)) && ctrlpkt.getAckSeqNo() <= m_iAckSeqNo)
        {
            LOGC(inlog.Note,
                log << CONID() << "ACKACK out of order, skipping RTT calculation "
                << "(ACK number: " << ctrlpkt.getAckSeqNo() << ", last ACK sent: " << m_iAckSeqNo
                << ", RTT (EWMA): " << m_iSRTT << ")");
            return;
        }

        LOGC(inlog.Error,
            log << CONID() << "IPE: ACK record not found, can't estimate RTT "
            << "(ACK number: " << ctrlpkt.getAckSeqNo() << ", last ACK sent: " << m_iAckSeqNo
            << ", RTT (EWMA): " << m_iSRTT << ")");
        return;
    }

    if (rtt <= 0)
    {
        LOGC(inlog.Error,
            log << CONID() << "IPE: invalid RTT estimate " << rtt
            << ", possible time shift. Clock: " << SRT_SYNC_CLOCK_STR);
        return;
    }

    // If increasing delay is detected.
    //   sendCtrl(UMSG_CGWARNING);

    // Update the values of smoothed RTT and the variation in RTT samples
    // on subsequent RTT samples (during transmission).
    if (m_bIsFirstRTTReceived)
    {
        m_iRTTVar = avg_iir<4>(m_iRTTVar.load(), abs(rtt - m_iSRTT.load()));
        m_iSRTT   = avg_iir<8>(m_iSRTT.load(), rtt);
    }
    // Reset the value of smoothed RTT on the first RTT sample after initialization
    // (at the beginning of transmission).
    // In case of resumed connection over the same network, the initial RTT
    // value will be taken from cache and equal to previous connection's
    // final smoothed RTT value.
    else
    {
        m_iSRTT               = rtt;
        m_iRTTVar             = rtt / 2;
        m_bIsFirstRTTReceived = true;
    }

#if SRT_DEBUG_RTT
    s_rtt_trace.trace(tsArrival, "ACKACK", rtt, -1, m_bIsFirstRTTReceived,
                      -1, m_iSRTT, m_iRTTVar);
#endif

    updateCC(TEV_ACKACK, EventVariant(ack));

    // This function will put a lock on m_RecvLock by itself, as needed.
    // It must be done inside because this function reads the current time
    // and if waiting for the lock has caused a delay, the time will be
    // inaccurate. Additionally it won't lock if TSBPD mode is off, and
    // won't update anything. Note that if you set TSBPD mode and use
    // srt_recvfile (which doesn't make any sense), you'll have a deadlock.
    if (m_config.bDriftTracer)
    {
        const bool drift_updated SRT_ATR_UNUSED = m_pRcvBuffer->addRcvTsbPdDriftSample(ctrlpkt.getMsgTimeStamp(), tsArrival, rtt);
#if ENABLE_BONDING
        if (drift_updated && m_parent->m_GroupOf)
        {
            ScopedLock glock(uglobal().m_GlobControlLock);
            if (m_parent->m_GroupOf)
            {
                m_parent->m_GroupOf->synchronizeDrift(this);
            }
        }
#endif
    }

    // Update last ACK that has been received by the sender
    if (CSeqNo::seqcmp(ack, m_iRcvLastAckAck) > 0)
        m_iRcvLastAckAck = ack;
}

void srt::CUDT::processCtrlLossReport(const CPacket& ctrlpkt)
{
    const int32_t* losslist = (int32_t*)(ctrlpkt.m_pcData);
    const size_t   losslist_len = ctrlpkt.getLength() / 4;

    bool secure = true;

    // This variable is used in "normal" logs, so it may cause a warning
    // when logging is forcefully off.
    int32_t wrong_loss SRT_ATR_UNUSED = CSeqNo::m_iMaxSeqNo;

    // protect packet retransmission
    {
        ScopedLock ack_lock(m_RecvAckLock);

        // decode loss list message and insert loss into the sender loss list
        for (int i = 0, n = (int)(ctrlpkt.getLength() / 4); i < n; ++i)
        {
            if (IsSet(losslist[i], LOSSDATA_SEQNO_RANGE_FIRST))
            {
                // Then it's this is a <lo, hi> specification with HI in a consecutive cell.
                const int32_t losslist_lo = SEQNO_VALUE::unwrap(losslist[i]);
                const int32_t losslist_hi = losslist[i + 1];
                // <lo, hi> specification means that the consecutive cell has been already interpreted.
                ++i;

                HLOGF(inlog.Debug,
                    "%sreceived UMSG_LOSSREPORT: %d-%d (%d packets)...", CONID().c_str(),
                    losslist_lo,
                    losslist_hi,
                    CSeqNo::seqoff(losslist_lo, losslist_hi) + 1);

                if ((CSeqNo::seqcmp(losslist_lo, losslist_hi) > 0) ||
                    (CSeqNo::seqcmp(losslist_hi, m_iSndCurrSeqNo) > 0))
                {
                    LOGC(inlog.Warn, log << CONID() << "rcv LOSSREPORT rng " << losslist_lo << " - " << losslist_hi
                        << " with last sent " << m_iSndCurrSeqNo << " - DISCARDING");
                    // seq_a must not be greater than seq_b; seq_b must not be greater than the most recent sent seq
                    secure = false;
                    wrong_loss = losslist_hi;
                    break;
                }

                int num = 0;
                //   IF losslist_lo %>= m_iSndLastAck
                if (CSeqNo::seqcmp(losslist_lo, m_iSndLastAck) >= 0)
                {
                    HLOGC(inlog.Debug, log << CONID() << "LOSSREPORT: adding "
                        << losslist_lo << " - " << losslist_hi << " to loss list");
                    num = m_pSndLossList->insert(losslist_lo, losslist_hi);
                }
                // ELSE IF losslist_hi %>= m_iSndLastAck
                else if (CSeqNo::seqcmp(losslist_hi, m_iSndLastAck) >= 0)
                {
                    // This should be theoretically impossible because this would mean
                    // that the received packet loss report informs about the loss that predates
                    // the ACK sequence.
                    // However, this can happen if the packet reordering has caused the earlier sent
                    // LOSSREPORT will be delivered after later sent ACK. Whatever, ACK should be
                    // more important, so simply drop the part that predates ACK.
                    HLOGC(inlog.Debug, log << CONID() << "LOSSREPORT: adding "
                        << m_iSndLastAck << "[ACK] - " << losslist_hi << " to loss list");
                    num = m_pSndLossList->insert(m_iSndLastAck, losslist_hi);
                }
                else
                {
                    // This should be treated as IPE, but this may happen in one situtation:
                    // - redundancy second link (ISN was screwed up initially, but late towards last sent)
                    // - initial DROPREQ was lost
                    // This just causes repeating DROPREQ, as when the receiver continues sending
                    // LOSSREPORT, it's probably UNAWARE OF THE SITUATION.
                    //
                    // When this DROPREQ gets lost in UDP again, the receiver will do one of these:
                    // - repeatedly send LOSSREPORT (as per NAKREPORT), so this will happen again
                    // - finally give up rexmit request as per TLPKTDROP (DROPREQ should make
                    //   TSBPD wake up should it still wait for new packets to get ACK-ed)

                    HLOGC(inlog.Debug, log << CONID() << "LOSSREPORT: IGNORED with SndLastAck=%"
                        << m_iSndLastAck << ": %" << losslist_lo << "-" << losslist_hi
                        << " - sending DROPREQ (IPE or DROPREQ lost with ISN screw)");

                    // This means that the loss touches upon a range that wasn't ever sent.
                    // Normally this should never happen, but this might be a case when the
                    // ISN FIX for redundant connection was missed.

                    // In distinction to losslist, DROPREQ has always a range
                    // always just one range, and the data are <LO, HI>, with no range bit.
                    int32_t seqpair[2] = { losslist_lo, losslist_hi };
                    const int32_t no_msgno = 0; // We don't know - this wasn't ever sent

                    sendCtrl(UMSG_DROPREQ, &no_msgno, seqpair, sizeof(seqpair));
                }

                enterCS(m_StatsLock);
                m_stats.sndr.lost.count(num);
                leaveCS(m_StatsLock);
            }
            else if (CSeqNo::seqcmp(losslist[i], m_iSndLastAck) >= 0)
            {
                if (CSeqNo::seqcmp(losslist[i], m_iSndCurrSeqNo) > 0)
                {
                    LOGC(inlog.Warn, log << CONID() << "rcv LOSSREPORT pkt %" << losslist[i]
                        << " with last sent %" << m_iSndCurrSeqNo << " - DISCARDING");
                    // seq_a must not be greater than the most recent sent seq
                    secure = false;
                    wrong_loss = losslist[i];
                    break;
                }

                HLOGC(inlog.Debug, log << CONID() << "rcv LOSSREPORT: %"
                    << losslist[i] << " (1 packet)");
                const int num = m_pSndLossList->insert(losslist[i], losslist[i]);

                enterCS(m_StatsLock);
                m_stats.sndr.lost.count(num);
                leaveCS(m_StatsLock);
            }
        }
    }

    updateCC(TEV_LOSSREPORT, EventVariant(losslist, losslist_len));

    if (!secure)
    {
        LOGC(inlog.Warn,
            log << CONID() << "out-of-band LOSSREPORT received; BUG or ATTACK - last sent %" << m_iSndCurrSeqNo
            << " vs loss %" << wrong_loss);
        // this should not happen: attack or bug
        m_bBroken = true;
        m_iBrokenCounter = 0;
        return;
    }

    // the lost packet (retransmission) should be sent out immediately
    m_pSndQueue->m_pSndUList->update(this, CSndUList::DONT_RESCHEDULE);

    enterCS(m_StatsLock);
    m_stats.sndr.recvdNak.count(1);
    leaveCS(m_StatsLock);
}

void srt::CUDT::processCtrlHS(const CPacket& ctrlpkt)
{
    CHandShake req;
    req.load_from(ctrlpkt.m_pcData, ctrlpkt.getLength());

    HLOGC(inlog.Debug, log << CONID() << "processCtrl: got HS: " << req.show());

    if ((req.m_iReqType > URQ_INDUCTION_TYPES) // acually it catches URQ_INDUCTION and URQ_ERROR_* symbols...???
        || (m_config.bRendezvous && (req.m_iReqType != URQ_AGREEMENT))) // rnd sends AGREEMENT in rsp to CONCLUSION
    {
        // The peer side has not received the handshake message, so it keeps querying
        // resend the handshake packet

        // This condition embraces cases when:
        // - this is normal accept() and URQ_INDUCTION was received
        // - this is rendezvous accept() and there's coming any kind of URQ except AGREEMENT (should be RENDEZVOUS
        // or CONCLUSION)
        // - this is any of URQ_ERROR_* - well...
        CHandShake initdata;
        initdata.m_iISN = m_iISN;
        initdata.m_iMSS = m_config.iMSS;
        initdata.m_iFlightFlagSize = m_config.iFlightFlagSize;

        // For rendezvous we do URQ_WAVEAHAND/URQ_CONCLUSION --> URQ_AGREEMENT.
        // For client-server we do URQ_INDUCTION --> URQ_CONCLUSION.
        initdata.m_iReqType = (!m_config.bRendezvous) ? URQ_CONCLUSION : URQ_AGREEMENT;
        initdata.m_iID = m_SocketID;

        uint32_t kmdata[SRTDATA_MAXSIZE];
        size_t   kmdatasize = SRTDATA_MAXSIZE;
        bool     have_hsreq = false;
        if (req.m_iVersion > HS_VERSION_UDT4)
        {
            initdata.m_iVersion = HS_VERSION_SRT1; // if I remember correctly, this is induction/listener...
            const int hs_flags = SrtHSRequest::SRT_HSTYPE_HSFLAGS::unwrap(m_ConnRes.m_iType);
            if (hs_flags != 0) // has SRT extensions
            {
                HLOGC(inlog.Debug,
                    log << CONID() << "processCtrl/HS: got HS reqtype=" << RequestTypeStr(req.m_iReqType)
                    << " WITH SRT ext");
                have_hsreq = interpretSrtHandshake(req, ctrlpkt, (kmdata), (&kmdatasize));
                if (!have_hsreq)
                {
                    initdata.m_iVersion = 0;
                    m_RejectReason = SRT_REJ_ROGUE;
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
                    if (m_config.bRendezvous && m_SrtHsSide == HSD_RESPONDER)
                    {
                        LOGC(inlog.Error,
                            log << CONID() << "processCtrl/HS: IPE???: RESPONDER should receive all its handshakes in "
                            "handshake phase.");
                    }

                    // The 'extension' flag will be set from this variable; set it to false
                    // in case when the AGREEMENT response is to be sent.
                    have_hsreq = initdata.m_iReqType == URQ_CONCLUSION;
                    HLOGC(inlog.Debug,
                        log << CONID() << "processCtrl/HS: processing ok, reqtype=" << RequestTypeStr(initdata.m_iReqType)
                        << " kmdatasize=" << kmdatasize);
                }
            }
            else
            {
                HLOGC(inlog.Debug, log << CONID() << "processCtrl/HS: got HS reqtype=" << RequestTypeStr(req.m_iReqType));
            }
        }
        else
        {
            initdata.m_iVersion = HS_VERSION_UDT4;
            kmdatasize = 0; // HSv4 doesn't add any extensions, no KMX
        }

        initdata.m_extension = have_hsreq;

        HLOGC(inlog.Debug,
            log << CONID() << "processCtrl: responding HS reqtype=" << RequestTypeStr(initdata.m_iReqType)
            << (have_hsreq ? " WITH SRT HS response extensions" : ""));

        CPacket response;
        response.setControl(UMSG_HANDSHAKE);
        response.allocate(m_iMaxSRTPayloadSize);

        // If createSrtHandshake failed, don't send anything. Actually it can only fail on IPE.
        // There is also no possible IPE condition in case of HSv4 - for this version it will always return true.
        if (createSrtHandshake(SRT_CMD_HSRSP, SRT_CMD_KMRSP, kmdata, kmdatasize,
            (response), (initdata)))
        {
            response.m_iID = m_PeerID;
            setPacketTS(response, steady_clock::now());
            const int nbsent = m_pSndQueue->sendto(m_PeerAddr, response);
            if (nbsent)
            {
                m_tsLastSndTime.store(steady_clock::now());
            }
        }
    }
    else
    {
        HLOGC(inlog.Debug, log << CONID() << "processCtrl: ... not INDUCTION, not ERROR, not rendezvous - IGNORED.");
    }
}

void srt::CUDT::processCtrlDropReq(const CPacket& ctrlpkt)
{
    const int32_t* dropdata = (const int32_t*) ctrlpkt.m_pcData;

    {
        CUniqueSync rcvtscc (m_RecvLock, m_RcvTsbPdCond);
        // With both TLPktDrop and TsbPd enabled, a message always consists only of one packet.
        // It will be dropped as too late anyway. Not dropping it from the receiver buffer
        // in advance reduces false drops if the packet somehow manages to arrive.
        // Still remove the record from the loss list to cease further retransmission requests.
        if (!m_bTLPktDrop || !m_bTsbPd)
        {
            const bool using_rexmit_flag = m_bPeerRexmitFlag;
            ScopedLock rblock(m_RcvBufferLock);
#if ENABLE_NEW_RCVBUFFER
            const int iDropCnt = m_pRcvBuffer->dropMessage(dropdata[0], dropdata[1], ctrlpkt.getMsgSeq(using_rexmit_flag));

            if (iDropCnt > 0)
            {
                LOGC(brlog.Warn, log << CONID() << "RCV-DROPPED " << iDropCnt << " packet(s), seqno range %"
                    << dropdata[0] << "-%" << dropdata[1] << ", msgno " << ctrlpkt.getMsgSeq(using_rexmit_flag)
                    << " (SND DROP REQUEST).");

                enterCS(m_StatsLock);
                // Estimate dropped bytes from average payload size.
                const uint64_t avgpayloadsz = m_pRcvBuffer->getRcvAvgPayloadSize();
                m_stats.rcvr.dropped.count(stats::BytesPackets(iDropCnt * avgpayloadsz, (uint32_t) iDropCnt));
                leaveCS(m_StatsLock);
            }
#else
            m_pRcvBuffer->dropMsg(ctrlpkt.getMsgSeq(using_rexmit_flag), using_rexmit_flag);
#endif
        }
        // When the drop request was received, it means that there are
        // packets for which there will never be ACK sent; if the TSBPD thread
        // is currently in the ACK-waiting state, it may never exit.
        if (m_bTsbPd)
        {
            HLOGP(inlog.Debug, "DROPREQ: signal TSBPD");
            rcvtscc.notify_one();
        }
    }

    dropFromLossLists(dropdata[0], dropdata[1]);

    // If dropping ahead of the current largest sequence number,
    // move the recv seq number forward.
    if ((CSeqNo::seqcmp(dropdata[0], CSeqNo::incseq(m_iRcvCurrSeqNo)) <= 0)
        && (CSeqNo::seqcmp(dropdata[1], m_iRcvCurrSeqNo) > 0))
    {
        HLOGC(inlog.Debug, log << CONID() << "DROPREQ: dropping %"
            << dropdata[0] << "-" << dropdata[1] << " <-- set as current seq");
        m_iRcvCurrSeqNo = dropdata[1];
    }
    else
    {
        HLOGC(inlog.Debug, log << CONID() << "DROPREQ: dropping %"
            << dropdata[0] << "-" << dropdata[1] << " current %" << m_iRcvCurrSeqNo);
    }
}

void srt::CUDT::processCtrlShutdown()
{
    m_bShutdown = true;
    m_bClosing = true;
    m_bBroken = true;
    m_iBrokenCounter = 60;

    // This does the same as it would happen on connection timeout,
    // just we know about this state prematurely thanks to this message.
    updateBrokenConnection();
    completeBrokenConnectionDependencies(SRT_ECONNLOST); // LOCKS!
}

void srt::CUDT::processCtrlUserDefined(const CPacket& ctrlpkt)
{
    HLOGC(inlog.Debug, log << CONID() << "CONTROL EXT MSG RECEIVED:"
        << MessageTypeStr(ctrlpkt.getType(), ctrlpkt.getExtendedType())
        << ", value=" << ctrlpkt.getExtendedType());

    // This has currently two roles in SRT:
    // - HSv4 (legacy) handshake
    // - refreshed KMX (initial KMX is done still in the HS process in HSv5)
    const bool understood = processSrtMsg(&ctrlpkt);
    // CAREFUL HERE! This only means that this update comes from the UMSG_EXT
    // message received, REGARDLESS OF WHAT IT IS. This version doesn't mean
    // the handshake version, but the reason of calling this function.
    //
    // Fortunately, the only messages taken into account in this function
    // are HSREQ and HSRSP, which should *never* be interchanged when both
    // parties are HSv5.
    if (understood)
    {
        if (ctrlpkt.getExtendedType() == SRT_CMD_HSREQ || ctrlpkt.getExtendedType() == SRT_CMD_HSRSP)
        {
            updateAfterSrtHandshake(HS_VERSION_UDT4);
        }
    }
    else
    {
        updateCC(TEV_CUSTOM, EventVariant(&ctrlpkt));
    }
}

void srt::CUDT::processCtrl(const CPacket &ctrlpkt)
{
    // Just heard from the peer, reset the expiration count.
    m_iEXPCount = 1;
    const steady_clock::time_point currtime = steady_clock::now();
    m_tsLastRspTime = currtime;

    HLOGC(inlog.Debug,
          log << CONID() << "incoming UMSG:" << ctrlpkt.getType() << " ("
              << MessageTypeStr(ctrlpkt.getType(), ctrlpkt.getExtendedType()) << ") socket=%" << ctrlpkt.m_iID);

    switch (ctrlpkt.getType())
    {
    case UMSG_ACK: // 010 - Acknowledgement
        processCtrlAck(ctrlpkt, currtime);
        break;

    case UMSG_ACKACK: // 110 - Acknowledgement of Acknowledgement
        processCtrlAckAck(ctrlpkt, currtime);
        break;

    case UMSG_LOSSREPORT: // 011 - Loss Report
        processCtrlLossReport(ctrlpkt);
        break;

    case UMSG_CGWARNING: // 100 - Delay Warning
        // One way packet delay is increasing, so decrease the sending rate
        m_tdSendInterval = (m_tdSendInterval.load() * 1125) / 1000;
        // XXX Note as interesting fact: this is only prepared for handling,
        // but nothing in the code is sending this message. Probably predicted
        // for a custom congctl. There's a predicted place to call it under
        // UMSG_ACKACK handling, but it's commented out.

        break;

    case UMSG_KEEPALIVE: // 001 - Keep-alive
        processKeepalive(ctrlpkt, currtime);
        break;

    case UMSG_HANDSHAKE: // 000 - Handshake
        processCtrlHS(ctrlpkt);
        break;

    case UMSG_SHUTDOWN: // 101 - Shutdown
        processCtrlShutdown();
        break;

    case UMSG_DROPREQ: // 111 - Msg drop request
        processCtrlDropReq(ctrlpkt);
        break;

    case UMSG_PEERERROR: // 1000 - An error has happened to the peer side
        // int err_type = packet.getAddInfo();

        // currently only this error is signalled from the peer side
        // if recvfile() failes (e.g., due to disk fail), blcoked sendfile/send should return immediately
        // giving the app a chance to fix the issue
        m_bPeerHealth = false;

        break;

    case UMSG_EXT: // 0x7FFF - reserved and user defined messages
        processCtrlUserDefined(ctrlpkt);
        break;

    default:
        break;
    }
}

void srt::CUDT::updateSrtRcvSettings()
{
    // CHANGED: we need to apply the tsbpd delay only for socket TSBPD.
    // For Group TSBPD the buffer will have to deliver packets always on request
    // by sequence number, although the buffer will have to solve all the TSBPD
    // things internally anyway. Extracting by sequence number means only that
    // the packet can be retrieved from the buffer before its time to play comes
    // (unlike in normal situation when reading directly from socket), however
    // its time to play shall be properly defined.
    ScopedLock lock(m_RecvLock);

    // NOTE: remember to also update synchronizeWithGroup() if more settings are updated here.
#if ENABLE_NEW_RCVBUFFER
    m_pRcvBuffer->setPeerRexmitFlag(m_bPeerRexmitFlag);
#endif

    // XXX m_bGroupTsbPd is ignored with SRT_ENABLE_APP_READER
    if (m_bTsbPd || m_bGroupTsbPd)
    {
#if ENABLE_NEW_RCVBUFFER
        m_pRcvBuffer->setTsbPdMode(m_tsRcvPeerStartTime, false, milliseconds_from(m_iTsbPdDelay_ms));
#else
        m_pRcvBuffer->setRcvTsbPdMode(m_tsRcvPeerStartTime, milliseconds_from(m_iTsbPdDelay_ms));
#endif

        HLOGF(cnlog.Debug,
              "AFTER HS: Set Rcv TsbPd mode%s: delay=%u.%03us RCV START: %s",
              (m_bGroupTsbPd ? " (AS GROUP MEMBER)" : ""),
              m_iTsbPdDelay_ms / 1000,
              m_iTsbPdDelay_ms % 1000,
              FormatTime(m_tsRcvPeerStartTime).c_str());
    }
    else
    {
        HLOGC(cnlog.Debug, log << "AFTER HS: Rcv TsbPd mode not set");
    }
}

void srt::CUDT::updateSrtSndSettings()
{
    if (m_bPeerTsbPd)
    {
        /* We are TsbPd sender */
        // XXX Check what happened here.
        // m_iPeerTsbPdDelay_ms = m_CongCtl->getSndPeerTsbPdDelay();// + ((m_iSRTT + (4 * m_iRTTVar)) / 1000);
        /*
         * For sender to apply Too-Late Packet Drop
         * option (m_bTLPktDrop) must be enabled and receiving peer shall support it
         */
        HLOGF(cnlog.Debug,
              "AFTER HS: Set Snd TsbPd mode %s TLPktDrop: delay=%d.%03ds START TIME: %s",
              m_bPeerTLPktDrop ? "with" : "without",
              m_iPeerTsbPdDelay_ms/1000, m_iPeerTsbPdDelay_ms%1000,
              FormatTime(m_stats.tsStartTime).c_str());
    }
    else
    {
        HLOGC(cnlog.Debug, log << "AFTER HS: Snd TsbPd mode not set");
    }
}

void srt::CUDT::updateAfterSrtHandshake(int hsv)
{
    HLOGC(cnlog.Debug, log << "updateAfterSrtHandshake: HS version " << hsv);
    // This is blocked from being run in the "app reader" version because here
    // every socket does its TsbPd independently, just the sequence screwup is
    // done and the application reader sorts out packets by sequence numbers,
    // but only when they are signed off by TsbPd.

    // The only possibility here is one of these two:
    // - Agent is RESPONDER and it receives HSREQ.
    // - Agent is INITIATOR and it receives HSRSP.
    //
    // In HSv4, INITIATOR is sender and RESPONDER is receiver.
    // In HSv5, both are sender AND receiver.
    //
    // This function will be called only ONCE in this
    // instance, through either HSREQ or HSRSP.
#if ENABLE_HEAVY_LOGGING
    const char* hs_side[] = { "DRAW", "INITIATOR", "RESPONDER" };
#if ENABLE_BONDING
    string grpspec;

    if (m_parent->m_GroupOf)
    {
        ScopedLock glock (uglobal().m_GlobControlLock);
        grpspec = m_parent->m_GroupOf
            ? " group=$" + Sprint(m_parent->m_GroupOf->id())
            : string();
    }
#else
    const char* grpspec = "";
#endif

    HLOGC(cnlog.Debug, log << "updateAfterSrtHandshake: version="
            << m_ConnRes.m_iVersion << " side=" << hs_side[m_SrtHsSide]
            << grpspec);
#endif

    if (hsv > HS_VERSION_UDT4)
    {
        updateSrtRcvSettings();
        updateSrtSndSettings();
    }
    else if (m_SrtHsSide == HSD_INITIATOR)
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

int srt::CUDT::packLostData(CPacket& w_packet, steady_clock::time_point& w_origintime)
{
    // protect m_iSndLastDataAck from updating by ACK processing
    UniqueLock ackguard(m_RecvAckLock);
    const steady_clock::time_point time_now = steady_clock::now();
    const steady_clock::time_point time_nak = time_now - microseconds_from(m_iSRTT - 4 * m_iRTTVar);

    while ((w_packet.m_iSeqNo = m_pSndLossList->popLostSeq()) >= 0)
    {
        // XXX See the note above the m_iSndLastDataAck declaration in core.h
        // This is the place where the important sequence numbers for
        // sender buffer are actually managed by this field here.
        const int offset = CSeqNo::seqoff(m_iSndLastDataAck, w_packet.m_iSeqNo);
        if (offset < 0)
        {
            // XXX Likely that this will never be executed because if the upper
            // sequence is not in the sender buffer, then most likely the loss 
            // was completely ignored.
            LOGC(qrlog.Error, log << "IPE/EPE: packLostData: LOST packet negative offset: seqoff(m_iSeqNo "
                << w_packet.m_iSeqNo << ", m_iSndLastDataAck " << m_iSndLastDataAck
                << ")=" << offset << ". Continue");

            // No matter whether this is right or not (maybe the attack case should be
            // considered, and some LOSSREPORT flood prevention), send the drop request
            // to the peer.
            int32_t seqpair[2] = {
                w_packet.m_iSeqNo,
                CSeqNo::decseq(m_iSndLastDataAck)
            };
            w_packet.m_iMsgNo = 0; // Message number is not known, setting all 32 bits to 0.

            HLOGC(qrlog.Debug, log << "PEER reported LOSS not from the sending buffer - requesting DROP: "
                    << "msg=" << MSGNO_SEQ::unwrap(w_packet.m_iMsgNo) << " SEQ:"
                    << seqpair[0] << " - " << seqpair[1] << "(" << (-offset) << " packets)");

            sendCtrl(UMSG_DROPREQ, &w_packet.m_iMsgNo, seqpair, sizeof(seqpair));
            continue;
        }

        if (m_bPeerNakReport && m_config.iRetransmitAlgo != 0)
        {
            const steady_clock::time_point tsLastRexmit = m_pSndBuffer->getPacketRexmitTime(offset);
            if (tsLastRexmit >= time_nak)
            {
                HLOGC(qrlog.Debug, log << CONID() << "REXMIT: ignoring seqno "
                    << w_packet.m_iSeqNo << ", last rexmit " << (is_zero(tsLastRexmit) ? "never" : FormatTime(tsLastRexmit))
                    << " RTT=" << m_iSRTT << " RTTVar=" << m_iRTTVar
                    << " now=" << FormatTime(time_now));
                continue;
            }
        }

        int msglen;

        const int payload = m_pSndBuffer->readData(offset, (w_packet), (w_origintime), (msglen));
        if (payload == -1)
        {
            int32_t seqpair[2];
            seqpair[0] = w_packet.m_iSeqNo;
            SRT_ASSERT(msglen >= 1);
            seqpair[1] = CSeqNo::incseq(seqpair[0], msglen - 1);

            HLOGC(qrlog.Debug,
                  log << "loss-reported packets expired in SndBuf - requesting DROP: "
                      << "msgno=" << MSGNO_SEQ::unwrap(w_packet.m_iMsgNo) << " msglen=" << msglen
                      << " SEQ:" << seqpair[0] << " - " << seqpair[1]);
            sendCtrl(UMSG_DROPREQ, &w_packet.m_iMsgNo, seqpair, sizeof(seqpair));

            // skip all dropped packets
            m_pSndLossList->removeUpTo(seqpair[1]);
            m_iSndCurrSeqNo = CSeqNo::maxseq(m_iSndCurrSeqNo, seqpair[1]);
            continue;
        }
        else if (payload == 0)
            continue;

        // At this point we no longer need the ACK lock,
        // because we are going to return from the function.
        // Therefore unlocking in order not to block other threads.
        ackguard.unlock();

        enterCS(m_StatsLock);
        m_stats.sndr.sentRetrans.count(payload);
        leaveCS(m_StatsLock);

        // Despite the contextual interpretation of packet.m_iMsgNo around
        // CSndBuffer::readData version 2 (version 1 doesn't return -1), in this particular
        // case we can be sure that this is exactly the value of PH_MSGNO as a bitset.
        // So, set here the rexmit flag if the peer understands it.
        if (m_bPeerRexmitFlag)
        {
            w_packet.m_iMsgNo |= PACKET_SND_REXMIT;
        }

        return payload;
    }

    return 0;
}

#if SRT_DEBUG_TRACE_SND
class snd_logger
{
    typedef srt::sync::steady_clock steady_clock;

public:
    snd_logger() {}

    ~snd_logger()
    {
        ScopedLock lck(m_mtx);
        m_fout.close();
    }

    struct
    {
        typedef srt::sync::steady_clock steady_clock;
        long long usElapsed;
        steady_clock::time_point tsNow;
        int usSRTT;
        int usRTTVar;
        int msSndBuffSpan;
        int msTimespanTh;
        int msNextUniqueToSend;
        long long usElapsedLastDrop;
        bool canRexmit;
        int iPktSeqno;
        bool isRetransmitted;
    } state;

    void trace()
    {
        using namespace srt::sync;
        ScopedLock lck(m_mtx);
        create_file();

        m_fout << state.usElapsed << ",";
        m_fout << state.usSRTT << ",";
        m_fout << state.usRTTVar << ",";
        m_fout << state.msSndBuffSpan << ",";
        m_fout << state.msTimespanTh << ",";
        m_fout << state.msNextUniqueToSend << ",";
        m_fout << state.usElapsedLastDrop << ",";
        m_fout << state.canRexmit << ",";
        m_fout << state.iPktSeqno << ',';
        m_fout << state.isRetransmitted << '\n';

        m_fout.flush();
    }

private:
    void print_header()
    {
        m_fout << "usElapsed,usSRTT,usRTTVar,msSndBuffTimespan,msTimespanTh,msNextUniqueToSend,usDLastDrop,canRexmit,sndPktSeqno,isRexmit";
        m_fout << "\n";
    }

    void create_file()
    {
        if (m_fout.is_open())
            return;

        m_start_time = srt::sync::steady_clock::now();
        std::string str_tnow = srt::sync::FormatTimeSys(m_start_time);
        str_tnow.resize(str_tnow.size() - 7); // remove trailing ' [SYST]' part
        while (str_tnow.find(':') != std::string::npos)
        {
            str_tnow.replace(str_tnow.find(':'), 1, 1, '_');
        }
        const std::string fname = "snd_trace_" + str_tnow + ".csv";
        m_fout.open(fname, std::ofstream::out);
        if (!m_fout)
            std::cerr << "IPE: Failed to open " << fname << "!!!\n";

        print_header();
    }

private:
    srt::sync::Mutex                    m_mtx;
    std::ofstream                       m_fout;
    srt::sync::steady_clock::time_point m_start_time;
};

snd_logger g_snd_logger;
#endif // SRT_DEBUG_TRACE_SND

bool srt::CUDT::isRetransmissionAllowed(const time_point& tnow SRT_ATR_UNUSED)
{
    // Prioritization of original packets only applies to Live CC.
    if (!m_bPeerTLPktDrop || !m_config.bMessageAPI)
        return true;

    // TODO: lock sender buffer?
    const time_point tsNextPacket = m_pSndBuffer->peekNextOriginal();

#if SRT_DEBUG_TRACE_SND
    const int buffdelay_ms = count_milliseconds(m_pSndBuffer->getBufferingDelay(tnow));
    // If there is a small loss, still better to retransmit. If timespan is already big,
    // then consider sending original packets.
    const int threshold_ms_min = (2 * m_iSRTT + 4 * m_iRTTVar + COMM_SYN_INTERVAL_US) / 1000;
    const int msNextUniqueToSend = count_milliseconds(tnow - tsNextPacket) + m_iPeerTsbPdDelay_ms;

    g_snd_logger.state.tsNow = tnow;
    g_snd_logger.state.usElapsed = count_microseconds(tnow - m_stats.tsStartTime);
    g_snd_logger.state.usSRTT = m_iSRTT;
    g_snd_logger.state.usRTTVar = m_iRTTVar;
    g_snd_logger.state.msSndBuffSpan = buffdelay_ms;
    g_snd_logger.state.msTimespanTh = threshold_ms_min;
    g_snd_logger.state.msNextUniqueToSend = msNextUniqueToSend;
    g_snd_logger.state.usElapsedLastDrop = count_microseconds(tnow - m_tsLastTLDrop);
    g_snd_logger.state.canRexmit = false;
#endif

    if (tsNextPacket != time_point())
    {
        // Can send original packet, so just send it
        return false;
    }

#if SRT_DEBUG_TRACE_SND
    g_snd_logger.state.canRexmit = true;
#endif
    return true;
}

std::pair<bool, steady_clock::time_point> srt::CUDT::packData(CPacket& w_packet)
{
    int payload = 0;
    bool probe = false;
    steady_clock::time_point origintime;
    bool new_packet_packed = false;
    bool filter_ctl_pkt = false;

    const steady_clock::time_point enter_time = steady_clock::now();

    if (!is_zero(m_tsNextSendTime) && enter_time > m_tsNextSendTime)
    {
        m_tdSendTimeDiff = m_tdSendTimeDiff.load() + (enter_time - m_tsNextSendTime);
    }

    string reason = "reXmit";

    ScopedLock connectguard(m_ConnectionLock);
    // If a closing action is done simultaneously, then
    // m_bOpened should already be false, and it's set
    // just before releasing this lock.
    //
    // If this lock is caught BEFORE the closing could
    // start the dissolving process, this process will
    // not be started until this function is finished.
    if (!m_bOpened)
        return std::make_pair(false, enter_time);

    payload = isRetransmissionAllowed(enter_time)
        ? packLostData((w_packet), (origintime))
        : 0;

    if (payload > 0)
    {
        reason = "reXmit";
    }
    else if (m_PacketFilter &&
             m_PacketFilter.packControlPacket(m_iSndCurrSeqNo, m_pCryptoControl->getSndCryptoFlags(), (w_packet)))
    {
        HLOGC(qslog.Debug, log << "filter: filter/CTL packet ready - packing instead of data.");
        payload        = (int) w_packet.getLength();
        reason         = "filter";
        filter_ctl_pkt = true; // Mark that this packet ALREADY HAS timestamp field and it should not be set

        // Stats
        ScopedLock lg(m_StatsLock);
        m_stats.sndr.sentFilterExtra.count(1);
    }
    else
    {
        if (!packUniqueData(w_packet, origintime))
        {
            m_tsNextSendTime = steady_clock::time_point();
            m_tdSendTimeDiff = steady_clock::duration();
            return std::make_pair(false, enter_time);
        }
        new_packet_packed = true;

        // every 16 (0xF) packets, a packet pair is sent
        if ((w_packet.m_iSeqNo & PUMASK_SEQNO_PROBE) == 0)
            probe = true;

        payload = (int) w_packet.getLength();
        reason = "normal";
    }

    // Normally packet.m_iTimeStamp field is set exactly here,
    // usually as taken from m_stats.tsStartTime and current time, unless live
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
             * XXX Isn't it then better to not decrease it by m_stats.tsStartTime? As long as it
             * doesn't screw up the start time on the other side.
             */
            if (origintime >= m_stats.tsStartTime)
            {
                setPacketTS(w_packet, origintime);
            }
            else
            {
                setPacketTS(w_packet, steady_clock::now());
                LOGC(qslog.Warn, log << "packData: reference time=" << FormatTime(origintime)
                        << " is in the past towards start time=" << FormatTime(m_stats.tsStartTime)
                        << " - setting NOW as reference time for the data packet");
            }
        }
        else
        {
            setPacketTS(w_packet, steady_clock::now());
        }
    }

    w_packet.m_iID = m_PeerID;

    if (new_packet_packed && m_PacketFilter)
    {
        HLOGC(qslog.Debug, log << "filter: Feeding packet for source clip");
        m_PacketFilter.feedSource((w_packet));
    }

#if ENABLE_HEAVY_LOGGING // Required because of referring to MessageFlagStr()
    HLOGC(qslog.Debug,
          log << CONID() << "packData: " << reason << " packet seq=" << w_packet.m_iSeqNo << " (ACK=" << m_iSndLastAck
              << " ACKDATA=" << m_iSndLastDataAck << " MSG/FLAGS: " << w_packet.MessageFlagStr() << ")");
#endif

    // Fix keepalive
    m_tsLastSndTime.store(enter_time);

    considerLegacySrtHandshake(steady_clock::time_point());

    // WARNING: TEV_SEND is the only event that is reported from
    // the CSndQueue::worker thread. All others are reported from
    // CRcvQueue::worker. If you connect to this signal, make sure
    // that you are aware of prospective simultaneous access.
    updateCC(TEV_SEND, EventVariant(&w_packet));

    // XXX This was a blocked code also originally in UDT. Probably not required.
    // Left untouched for historical reasons.
    // Might be possible that it was because of that this is send from
    // different thread than the rest of the signals.
    // m_pSndTimeWindow->onPktSent(w_packet.m_iTimeStamp);

    enterCS(m_StatsLock);
    m_stats.sndr.sent.count(payload);
    if (new_packet_packed)
        m_stats.sndr.sentUnique.count(payload);
    leaveCS(m_StatsLock);

    const duration sendint = m_tdSendInterval;
    if (probe)
    {
        // sends out probing packet pair
        m_tsNextSendTime = enter_time;
        // Sending earlier, need to adjust the pace later on.
        m_tdSendTimeDiff = m_tdSendTimeDiff.load() - sendint;
        probe          = false;
    }
    else
    {
#if USE_BUSY_WAITING
        m_tsNextSendTime = enter_time + m_tdSendInterval.load();
#else
        const duration sendbrw = m_tdSendTimeDiff;

        if (sendbrw >= sendint)
        {
            // Send immidiately
            m_tsNextSendTime = enter_time;

            // ATOMIC NOTE: this is the only thread that
            // modifies this field
            m_tdSendTimeDiff = sendbrw - sendint;
        }
        else
        {
            m_tsNextSendTime = enter_time + (sendint - sendbrw);
            m_tdSendTimeDiff = duration();
        }
#endif
    }

    return std::make_pair(payload >= 0, m_tsNextSendTime);
}

bool srt::CUDT::packUniqueData(CPacket& w_packet, time_point& w_origintime)
{
    // Check the congestion/flow window limit
    const int cwnd    = std::min(int(m_iFlowWindowSize), int(m_dCongestionWindow));
    const int flightspan = getFlightSpan();
    if (cwnd <= flightspan)
    {
        HLOGC(qslog.Debug, log << "packData: CONGESTED: cwnd=min(" << m_iFlowWindowSize << "," << m_dCongestionWindow
            << ")=" << cwnd << " seqlen=(" << m_iSndLastAck << "-" << m_iSndCurrSeqNo << ")=" << flightspan);
        return false;
    }

    // XXX Here it's needed to set kflg to msgno_bitset in the block stored in the
    // send buffer. This should be somehow avoided, the crypto flags should be set
    // together with encrypting, and the packet should be sent as is, when rexmitting.
    // It would be nice to research as to whether CSndBuffer::Block::m_iMsgNoBitset field
    // isn't a useless redundant state copy. If it is, then taking the flags here can be removed.
    const int kflg = m_pCryptoControl->getSndCryptoFlags();
    int pktskipseqno = 0;
    const int pld_size = m_pSndBuffer->readData((w_packet), (w_origintime), kflg, (pktskipseqno));
    if (pktskipseqno)
    {
        // Some packets were skipped due to TTL expiry.
        m_iSndCurrSeqNo = CSeqNo::incseq(m_iSndCurrSeqNo, pktskipseqno);
    }

    if (pld_size == 0)
    {
        return false;
    }

    // A CHANGE. The sequence number is currently added to the packet
    // when scheduling, not when extracting. This is a inter-migration form,
    // only override extraction sequence with scheduling sequence in group mode.
    m_iSndCurrSeqNo = CSeqNo::incseq(m_iSndCurrSeqNo);

#if ENABLE_BONDING
    // Fortunately the group itself isn't being accessed.
    if (m_parent->m_GroupOf)
    {
        const int packetspan = CSeqNo::seqoff(m_iSndCurrSeqNo, w_packet.m_iSeqNo);
        if (packetspan > 0)
        {
            // After increasing by 1, but being previously set as ISN-1, this should be == ISN,
            // if this is the very first packet to send.
            if (m_iSndCurrSeqNo == m_iISN)
            {
                // This is the very first packet to be sent; so there's nothing in
                // the sending buffer yet, and therefore we are in a situation as just
                // after connection. No packets in the buffer, no packets are sent,
                // no ACK to be awaited. We can screw up all the variables that are
                // initialized from ISN just after connection.
                LOGC(qslog.Note,
                     log << CONID() << "packData: Fixing EXTRACTION sequence " << m_iSndCurrSeqNo
                         << " from SCHEDULING sequence " << w_packet.m_iSeqNo << " for the first packet: DIFF="
                         << packetspan << " STAMP=" << BufferStamp(w_packet.m_pcData, w_packet.getLength()));
            }
            else
            {
                // There will be a serious data discrepancy between the agent and the peer.
                LOGC(qslog.Error,
                     log << CONID() << "IPE: packData: Fixing EXTRACTION sequence " << m_iSndCurrSeqNo
                         << " from SCHEDULING sequence " << w_packet.m_iSeqNo << " in the middle of transition: DIFF="
                         << packetspan << " STAMP=" << BufferStamp(w_packet.m_pcData, w_packet.getLength()));
            }

            // Additionally send the drop request to the peer so that it
            // won't stupidly request the packets to be retransmitted.
            // Don't do it if the difference isn't positive or exceeds the threshold.
            int32_t seqpair[2];
            seqpair[0]             = m_iSndCurrSeqNo;
            seqpair[1]             = CSeqNo::decseq(w_packet.m_iSeqNo);
            const int32_t no_msgno = 0;
            LOGC(qslog.Debug,
                 log << CONID() << "packData: Sending DROPREQ: SEQ: " << seqpair[0] << " - " << seqpair[1] << " ("
                     << packetspan << " packets)");
            sendCtrl(UMSG_DROPREQ, &no_msgno, seqpair, sizeof(seqpair));
            // In case when this message is lost, the peer will still get the
            // UMSG_DROPREQ message when the agent realizes that the requested
            // packet are not present in the buffer (preadte the send buffer).

            // Override extraction sequence with scheduling sequence.
            m_iSndCurrSeqNo = w_packet.m_iSeqNo;
            ScopedLock ackguard(m_RecvAckLock);
            m_iSndLastAck     = w_packet.m_iSeqNo;
            m_iSndLastDataAck = w_packet.m_iSeqNo;
            m_iSndLastFullAck = w_packet.m_iSeqNo;
            m_iSndLastAck2    = w_packet.m_iSeqNo;
        }
        else if (packetspan < 0)
        {
            LOGC(qslog.Error,
                 log << CONID() << "IPE: packData: SCHEDULING sequence " << w_packet.m_iSeqNo
                     << " is behind of EXTRACTION sequence " << m_iSndCurrSeqNo << ", dropping this packet: DIFF="
                     << packetspan << " STAMP=" << BufferStamp(w_packet.m_pcData, w_packet.getLength()));
            // XXX: Probably also change the socket state to broken?
            return false;
        }
    }
    else
#endif
    {
        HLOGC(qslog.Debug,
              log << CONID() << "packData: Applying EXTRACTION sequence " << m_iSndCurrSeqNo
                  << " over SCHEDULING sequence " << w_packet.m_iSeqNo << " for socket not in group:"
                  << " DIFF=" << CSeqNo::seqcmp(m_iSndCurrSeqNo, w_packet.m_iSeqNo)
                  << " STAMP=" << BufferStamp(w_packet.m_pcData, w_packet.getLength()));
        // Do this always when not in a group.
        w_packet.m_iSeqNo = m_iSndCurrSeqNo;
    }

    // Encrypt if 1st time this packet is sent and crypto is enabled
    if (kflg != EK_NOENC)
    {
        // Note that the packet header must have a valid seqno set, as it is used as a counter for encryption.
        // Other fields of the data packet header (e.g. timestamp, destination socket ID) are not used for the counter.
        // Cypher may change packet length!
        if (m_pCryptoControl->encrypt((w_packet)) != ENCS_CLEAR)
        {
            // Encryption failed
            //>>Add stats for crypto failure
            LOGC(qslog.Warn, log << "ENCRYPT FAILED - packet won't be sent, size=" << pld_size);
            return false;
        }
    }

#if SRT_DEBUG_TRACE_SND
    g_snd_logger.state.iPktSeqno = w_packet.m_iSeqNo;
    g_snd_logger.state.isRetransmitted = w_packet.getRexmitFlag(); 
    g_snd_logger.trace();
#endif

    return true;
}

// This is a close request, but called from the
void srt::CUDT::processClose()
{
    sendCtrl(UMSG_SHUTDOWN);

    m_bShutdown      = true;
    m_bClosing       = true;
    m_bBroken        = true;
    m_iBrokenCounter = 60;

    HLOGP(smlog.Debug, "processClose: sent message and set flags");

    if (m_bTsbPd)
    {
        HLOGP(smlog.Debug, "processClose: lock-and-signal TSBPD");
        CSync::lock_notify_one(m_RcvTsbPdCond, m_RecvLock);
    }

    // Signal the sender and recver if they are waiting for data.
    releaseSynch();
    // Unblock any call so they learn the connection_broken error
    uglobal().m_EPoll.update_events(m_SocketID, m_sPollID, SRT_EPOLL_ERR, true);

    HLOGP(smlog.Debug, "processClose: triggering timer event to spread the bad news");
    CGlobEvent::triggerEvent();
}

void srt::CUDT::sendLossReport(const std::vector<std::pair<int32_t, int32_t> > &loss_seqs)
{
    typedef vector<pair<int32_t, int32_t> > loss_seqs_t;

    vector<int32_t> seqbuffer;
    seqbuffer.reserve(2 * loss_seqs.size()); // pessimistic
    for (loss_seqs_t::const_iterator i = loss_seqs.begin(); i != loss_seqs.end(); ++i)
    {
        if (i->first == i->second)
        {
            seqbuffer.push_back(i->first);
            HLOGF(qrlog.Debug, "lost packet %d: sending LOSSREPORT", i->first);
        }
        else
        {
            seqbuffer.push_back(i->first | LOSSDATA_SEQNO_RANGE_FIRST);
            seqbuffer.push_back(i->second);
            HLOGF(qrlog.Debug,
                  "lost packets %d-%d (%d packets): sending LOSSREPORT",
                  i->first,
                  i->second,
                  1 + CSeqNo::seqcmp(i->second, i->first));
        }
    }

    if (!seqbuffer.empty())
    {
        sendCtrl(UMSG_LOSSREPORT, NULL, &seqbuffer[0], (int) seqbuffer.size());
    }
}


bool srt::CUDT::overrideSndSeqNo(int32_t seq)
{
    // This function is intended to be called from the socket
    // group managmenet functions to synchronize the sequnece in
    // all sockes in the bonding group. THIS sequence given
    // here is the sequence TO BE STAMPED AT THE EXACTLY NEXT
    // sent payload. Therefore, screw up the ISN to exactly this
    // value, and the send sequence to the value one less - because
    // the m_iSndCurrSeqNo is increased by one immediately before
    // stamping it to the packet.

    // This function can only be called:
    // - from the operation on an idle socket in the socket group
    // - IMMEDIATELY after connection established and BEFORE the first payload
    // - The corresponding socket at the peer side must be also
    //   in this idle state!

    ScopedLock cg (m_RecvAckLock);

    // Both the scheduling and sending sequences should be fixed.
    // The new sequence normally should jump over several sequence numbers
    // towards what is currently in m_iSndCurrSeqNo.
    // Therefore it's not allowed that:
    // - the jump go backward: backward packets should be already there
    // - the jump go forward by a value larger than half the period: DISCREPANCY.
    const int diff = CSeqNo(seq) - CSeqNo(m_iSndCurrSeqNo);
    if (diff < 0 || diff > CSeqNo::m_iSeqNoTH)
    {
        LOGC(gslog.Error, log << CONID() << "IPE: Overriding with seq %" << seq << " DISCREPANCY against current %"
                << m_iSndCurrSeqNo << " and next sched %" << m_iSndNextSeqNo << " - diff=" << diff);
        return false;
    }

    //
    // The peer will have to do the same, as a reaction on perceived
    // packet loss. When it recognizes that this initial screwing up
    // has happened, it should simply ignore the loss and go on.
    // ISN isn't being changed here - it doesn't make much sense now.

    setInitialSndSeq(seq);

    // m_iSndCurrSeqNo will be most likely lower than m_iSndNextSeqNo because
    // the latter is ahead with the number of packets already scheduled, but
    // not yet sent.

    HLOGC(gslog.Debug, log << CONID() << "overrideSndSeqNo: sched-seq=" << m_iSndNextSeqNo << " send-seq=" << m_iSndCurrSeqNo
        << " (unchanged)"
        );
    return true;
}

int srt::CUDT::processData(CUnit* in_unit)
{
    if (m_bClosing)
        return -1;

    CPacket &packet = in_unit->m_Packet;

    // Just heard from the peer, reset the expiration count.
    m_iEXPCount = 1;
    m_tsLastRspTime.store(steady_clock::now());

    const bool need_tsbpd = m_bTsbPd || m_bGroupTsbPd;

    // We are receiving data, start tsbpd thread if TsbPd is enabled
    if (need_tsbpd && !m_RcvTsbPdThread.joinable())
    {
        ScopedLock lock(m_RcvTsbPdStartupLock);

        if (m_bClosing) // Check again to protect join() in CUDT::releaseSync()
            return -1;

        HLOGP(qrlog.Debug, "Spawning Socket TSBPD thread");
#if ENABLE_HEAVY_LOGGING
        std::ostringstream tns1, tns2;
        // Take the last 2 ciphers from the socket ID.
        tns1 << m_SocketID;
        std::string s = tns1.str();
        tns2 << "SRT:TsbPd:@" << s.substr(s.size()-2, 2);

        const string& tn = tns2.str();

        ThreadName tnkeep(tn);
        const string& thname = tn;
#else
        const string thname = "SRT:TsbPd";
#endif
        if (!StartThread(m_RcvTsbPdThread, CUDT::tsbpd, this, thname))
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
        enterCS(m_StatsLock);
        m_stats.rcvr.recvdRetrans.count(packet.getLength());
        leaveCS(m_StatsLock);

#if ENABLE_HEAVY_LOGGING
        // Check if packet was retransmitted on request or on ack timeout
        // Search the sequence in the loss record.
        rexmit_reason = " by ";
        if (!m_pRcvLossList->find(packet.m_iSeqNo, packet.m_iSeqNo))
            rexmit_reason += "BLIND";
        else
            rexmit_reason += "NAKREPORT";
#endif
    }

#if ENABLE_HEAVY_LOGGING
   {
       steady_clock::duration tsbpddelay = milliseconds_from(m_iTsbPdDelay_ms); // (value passed to CRcvBuffer::setRcvTsbPdMode)

       // It's easier to remove the latency factor from this value than to add a function
       // that exposes the details basing on which this value is calculated.
       steady_clock::time_point pts = m_pRcvBuffer->getPktTsbPdTime(packet.getMsgTimeStamp());
       steady_clock::time_point ets = pts - tsbpddelay;

       HLOGC(qrlog.Debug, log << CONID() << "processData: RECEIVED DATA: size=" << packet.getLength()
           << " seq=" << packet.getSeqNo()
           // XXX FIX IT. OTS should represent the original sending time, but it's relative.
           //<< " OTS=" << FormatTime(packet.getMsgTimeStamp())
           << " ETS=" << FormatTime(ets)
           << " PTS=" << FormatTime(pts));
   }
#endif

    updateCC(TEV_RECEIVE, EventVariant(&packet));
    ++m_iPktCount;

    const int pktsz = (int) packet.getLength();
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

    enterCS(m_StatsLock);
    m_stats.rcvr.recvd.count(pktsz);
    leaveCS(m_StatsLock);

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

    if (packet.getMsgSeq() != SRT_MSGNO_CONTROL) // disregard filter-control packets, their seq may mean nothing
    {
        int diff = CSeqNo::seqoff(m_iRcvCurrPhySeqNo, packet.m_iSeqNo);
       // Difference between these two sequence numbers is expected to be:
       // 0 - duplicated last packet (theory only)
       // 1 - subsequent packet (alright)
       // <0 - belated or recovered packet
       // >1 - jump over a packet loss (loss = seqdiff-1)
        if (diff > 1)
        {
            const int loss = diff - 1; // loss is all that is above diff == 1
            ScopedLock lg(m_StatsLock);
            const uint64_t avgpayloadsz = m_pRcvBuffer->getRcvAvgPayloadSize();
            m_stats.rcvr.lost.count(stats::BytesPackets(loss * avgpayloadsz, (uint32_t) loss));

            HLOGC(qrlog.Debug,
                  log << "LOSS STATS: n=" << loss << " SEQ: [" << CSeqNo::incseq(m_iRcvCurrPhySeqNo) << " "
                      << CSeqNo::decseq(packet.m_iSeqNo) << "]");
        }

        if (diff > 0)
        {
            // Record if it was further than latest
            m_iRcvCurrPhySeqNo = packet.m_iSeqNo;
        }
    }

    // [[using locked()]];  // (NOTHING locked)

#if ENABLE_BONDING
    // Switch to RUNNING even if there was a discrepancy, unless
    // it was long way forward.
    // XXX Important: This code is in the dead function defaultPacketArrival
    // but normally it should be called here regardless if the packet was
    // accepted or rejected because if it was belated it may result in a
    // "runaway train" problem as the IDLE links are being updated the base
    // reception sequence pointer stating that this link is not receiving.
    if (m_parent->m_GroupOf)
    {
        ScopedLock protect_group_existence (uglobal().m_GlobControlLock);
        groups::SocketData* gi = m_parent->m_GroupMemberData;

        // This check is needed as after getting the lock the socket
        // could be potentially removed. It is however granted that as long
        // as gi is non-NULL iterator, the group does exist and it does contain
        // this socket as member (that is, 'gi' cannot be a dangling iterator).
        if (gi != NULL)
        {
            if (gi->rcvstate < SRT_GST_RUNNING) // PENDING or IDLE, tho PENDING is unlikely
            {
                HLOGC(qrlog.Debug,
                      log << "processData: IN-GROUP rcv state transition " << srt_log_grp_state[gi->rcvstate]
                          << " -> RUNNING.");
                gi->rcvstate = SRT_GST_RUNNING;
            }
            else
            {
                HLOGC(qrlog.Debug, log << "processData: IN-GROUP rcv state transition NOT DONE - state:"
                        << srt_log_grp_state[gi->rcvstate]);
            }
        }
    }
#endif

    {
        // Start of offset protected section
        // Prevent TsbPd thread from modifying Ack position while adding data
        // offset from RcvLastAck in RcvBuffer must remain valid between seqoff() and addData()
        UniqueLock recvbuf_acklock(m_RcvBufferLock);

        // vector<CUnit*> undec_units;
        if (m_PacketFilter)
        {
            // Stuff this data into the filter
            m_PacketFilter.receive(in_unit, (incoming), (filter_loss_seqs));
            HLOGC(qrlog.Debug,
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
        for (vector<CUnit *>::iterator unitIt = incoming.begin(); unitIt != incoming.end(); ++unitIt)
        {
            CUnit *  u    = *unitIt;
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
                steady_clock::time_point tsbpdtime = m_pRcvBuffer->getPktTsbPdTime(rpkt.getMsgTimeStamp());
                const double bltime = (double) CountIIR<uint64_t>(
                        uint64_t(m_stats.traceBelatedTime) * 1000,
                        count_microseconds(steady_clock::now() - tsbpdtime), 0.2);

                enterCS(m_StatsLock);
                m_stats.traceBelatedTime = bltime / 1000.0;
                m_stats.rcvr.recvdBelated.count(rpkt.getLength());
                leaveCS(m_StatsLock);
                HLOGC(qrlog.Debug,
                      log << CONID() << "RECEIVED: seq=" << packet.m_iSeqNo << " offset=" << offset << " (BELATED/"
                          << rexmitstat[pktrexmitflag] << rexmit_reason << ") FLAGS: " << packet.MessageFlagStr());
                continue;
            }

            const int avail_bufsize = (int) getAvailRcvBufferSizeNoLock();
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

                    LOGC(qrlog.Error, log << CONID() <<
                            "SEQUENCE DISCREPANCY. BREAKING CONNECTION."
                            " seq=" << rpkt.m_iSeqNo
                            << " buffer=(" << m_iRcvLastSkipAck
                            << ":" << m_iRcvCurrSeqNo                   // -1 = size to last index
                            << "+" << CSeqNo::incseq(m_iRcvLastSkipAck, int(m_pRcvBuffer->capacity()) - 1)
                            << "), " << (offset-avail_bufsize+1)
                            << " past max. Reception no longer possible. REQUESTING TO CLOSE.");

                    // This is a scoped lock with AckLock, but for the moment
                    // when processClose() is called this lock must be taken out,
                    // otherwise this will cause a deadlock. We don't need this
                    // lock anymore, and at 'return' it will be unlocked anyway.
                    recvbuf_acklock.unlock();
                    processClose();
                    return -1;
                }
                else
                {
#if ENABLE_NEW_RCVBUFFER
                    LOGC(qrlog.Warn, log << CONID() << "No room to store incoming packet seqno " << rpkt.m_iSeqNo
                        << ", insert offset " << offset << ". "
                        << m_pRcvBuffer->strFullnessState(m_iRcvLastAck, steady_clock::now())
                    );
#else
                    LOGC(qrlog.Warn, log << CONID() << "No room to store incoming packet seqno " << rpkt.m_iSeqNo
                        << ", insert offset " << offset << ". "
                        << m_pRcvBuffer->strFullnessState(steady_clock::now())
                    );
#endif

                    return -1;
                }
            }

            bool adding_successful = true;
#if ENABLE_NEW_RCVBUFFER
            if (m_pRcvBuffer->insert(u) < 0)
#else
            if (m_pRcvBuffer->addData(u, offset) < 0)
#endif
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
                if (u->m_Packet.getMsgCryptoFlags() != EK_NOENC)
                {
                    EncryptionStatus rc = m_pCryptoControl ? m_pCryptoControl->decrypt((u->m_Packet)) : ENCS_NOTSUP;
                    if (rc != ENCS_CLEAR)
                    {
                        // Heavy log message because if seen once the message may happen very often.
                        HLOGC(qrlog.Debug, log << CONID() << "ERROR: packet not decrypted, dropping data.");
                        adding_successful = false;
                        IF_HEAVY_LOGGING(exc_type = "UNDECRYPTED");

                        ScopedLock lg(m_StatsLock);
                        m_stats.rcvr.undecrypted.count(stats::BytesPackets(pktsz, 1));
                    }
                }
            }

            if (adding_successful)
            {
                ScopedLock statslock(m_StatsLock);
                m_stats.rcvr.recvdUnique.count(u->m_Packet.getLength());
            }

#if ENABLE_HEAVY_LOGGING
            std::ostringstream expectspec;
            if (excessive)
                expectspec << "EXCESSIVE(" << exc_type << rexmit_reason << ")";
            else
                expectspec << "ACCEPTED";

            LOGC(qrlog.Debug, log << CONID() << "RECEIVED: seq=" << rpkt.m_iSeqNo
                    << " offset=" << offset
                    << " BUFr=" << avail_bufsize
                    << " avail=" << getAvailRcvBufferSizeNoLock()
                    << " buffer=(" << m_iRcvLastSkipAck
                    << ":" << m_iRcvCurrSeqNo                   // -1 = size to last index
                    << "+" << CSeqNo::incseq(m_iRcvLastSkipAck, m_pRcvBuffer->capacity()-1)
                    << ") "
                    << " RSL=" << expectspec.str()
                    << " SN=" << rexmitstat[pktrexmitflag]
                    << " FLAGS: "
                    << rpkt.MessageFlagStr());
#endif

            // Decryption should have made the crypto flags EK_NOENC.
            // Otherwise it's an error.
            if (adding_successful)
            {
                // XXX move this code do CUDT::defaultPacketArrival and call it from here:
                // srt_loss_seqs = CALLBACK_CALL(m_cbPacketArrival, rpkt);

                HLOGC(qrlog.Debug,
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
                        HLOGC(qrlog.Debug,
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
                m_tsNextACKTime.store(steady_clock::now());
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

            HLOGC(qrlog.Debug, log << "processData: LOSS DETECTED, %: " << Printable(srt_loss_seqs) << " - RECORDING.");
            // if record_loss == false, nothing will be contained here
            // Insert lost sequence numbers to the receiver loss list
            ScopedLock lg(m_RcvLossLock);
            for (loss_seqs_t::iterator i = srt_loss_seqs.begin(); i != srt_loss_seqs.end(); ++i)
            {
                // If loss found, insert them to the receiver loss list
                m_pRcvLossList->insert(i->first, i->second);
            }
        }

        const bool report_recorded_loss = !m_PacketFilter || m_PktFilterRexmitLevel == SRT_ARQ_ALWAYS;
        if (!reorder_prevent_lossreport && report_recorded_loss)
        {
            HLOGC(qrlog.Debug, log << "WILL REPORT LOSSES (SRT): " << Printable(srt_loss_seqs));
            sendLossReport(srt_loss_seqs);
        }

        if (m_bTsbPd)
        {
            HLOGC(qrlog.Debug, log << "loss: signaling TSBPD cond");
            CSync::lock_notify_one(m_RcvTsbPdCond, m_RecvLock);
        }
        else
        {
            HLOGC(qrlog.Debug, log << "loss: socket is not TSBPD, not signaling");
        }
    }

    // Separately report loss records of those reported by a filter.
    // ALWAYS report whatever has been reported back by a filter. Note that
    // the filter never reports anything when rexmit fallback level is ALWAYS or NEVER.
    // With ALWAYS only those are reported that were recorded here by SRT.
    // With NEVER, nothing is to be reported.
    if (!filter_loss_seqs.empty())
    {
        HLOGC(qrlog.Debug, log << "WILL REPORT LOSSES (filter): " << Printable(filter_loss_seqs));
        sendLossReport(filter_loss_seqs);

        if (m_bTsbPd)
        {
            HLOGC(qrlog.Debug, log << "loss: signaling TSBPD cond");
            CSync::lock_notify_one(m_RcvTsbPdCond, m_RecvLock);
        }
    }

    // Now review the list of FreshLoss to see if there's any "old enough" to send UMSG_LOSSREPORT to it.

    // PERFORMANCE CONSIDERATIONS:
    // This list is quite inefficient as a data type and finding the candidate to send UMSG_LOSSREPORT
    // is linear time. On the other hand, there are some special cases that are important for performance:
    // - only the first (plus some following) could have had TTL drown to 0
    // - the only (little likely) possibility that the next-to-first record has TTL=0 is when there was
    //   a loss range split (due to dropFromLossLists() of one sequence)
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
        ScopedLock lg(m_RcvLossLock);

        // XXX There was a mysterious crash around m_FreshLoss. When the initial_loss_ttl is 0
        // (that is, "belated loss report" feature is off), don't even touch m_FreshLoss.
        if (initial_loss_ttl && !m_FreshLoss.empty())
        {
            deque<CRcvFreshLoss>::iterator i = m_FreshLoss.begin();

            // Phase 1: take while TTL <= 0.
            // There can be more than one record with the same TTL, if it has happened before
            // that there was an 'unlost' (@c dropFromLossLists) sequence that has split one detected loss
            // into two records.
            for (; i != m_FreshLoss.end() && i->ttl <= 0; ++i)
            {
                HLOGF(qrlog.Debug,
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
                HLOGP(qrlog.Debug, "NO MORE FRESH LOSS RECORDS.");
            }
            else
            {
                HLOGF(qrlog.Debug,
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
        sendCtrl(UMSG_LOSSREPORT, NULL, &lossdata[0], (int) lossdata.size());
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
                enterCS(m_StatsLock);
                m_stats.traceReorderDistance--;
                leaveCS(m_StatsLock);
                HLOGF(qrlog.Debug,
                      "ORDERED DELIVERY of 50 packets in a row - decreasing tolerance to %d",
                      m_iReorderTolerance);
            }
        }
    }

    return 0;
}

#if ENABLE_BONDING
void srt::CUDT::updateIdleLinkFrom(CUDT* source)
{
    ScopedLock lg (m_RecvLock);

    if (!m_pRcvBuffer->empty())
    {
        HLOGC(grlog.Debug, log << "grp: NOT updating rcv-seq in @" << m_SocketID << ": receiver buffer not empty");
        return;
    }

    // XXX Try to optimize this. Note that here happens:
    // - decseq just to have a value to compare directly
    // - seqcmp with that value
    // - if passed, in setInitialRcvSeq there's the same decseq again
    int32_t new_last_rcv = CSeqNo::decseq(source->m_iRcvLastSkipAck);

    // if (new_last_rcv <% m_iRcvCurrSeqNo)
    if (CSeqNo::seqcmp(new_last_rcv, m_iRcvCurrSeqNo) < 0)
    {
        // Reject the change because that would shift the reception pointer backwards.
        HLOGC(grlog.Debug, log << "grp: NOT updating rcv-seq in @" << m_SocketID
                << ": backward setting rejected: %" << m_iRcvCurrSeqNo
                << " -> %" << new_last_rcv);
        return;
    }

    HLOGC(grlog.Debug, log << "grp: updating rcv-seq in @" << m_SocketID
            << " from @" << source->m_SocketID << ": %" << source->m_iRcvLastSkipAck);
    setInitialRcvSeq(source->m_iRcvLastSkipAck);
}

// XXX This function is currently unused. It should be fixed and put into use.
// See the blocked call in CUDT::processData().
// XXX REVIEW LOCKS WHEN REACTIVATING!
srt::CUDT::loss_seqs_t srt::CUDT::defaultPacketArrival(void* vself, CPacket& pkt)
{
// [[using affinity(m_pRcvBuffer->workerThread())]];
    CUDT* self = (CUDT*)vself;
    loss_seqs_t output;

    // XXX When an alternative packet arrival callback is installed
    // in case of groups, move this part to the groupwise version.

    if (self->m_parent->m_GroupOf)
    {
        groups::SocketData* gi = self->m_parent->m_GroupMemberData;
        if (gi->rcvstate < SRT_GST_RUNNING) // PENDING or IDLE, tho PENDING is unlikely
        {
            HLOGC(qrlog.Debug, log << "defaultPacketArrival: IN-GROUP rcv state transition to RUNNING. NOT checking for loss");
            gi->rcvstate = SRT_GST_RUNNING;
            return output;
        }
    }

    const int initial_loss_ttl = (self->m_bPeerRexmitFlag) ? self->m_iReorderTolerance : 0;

    int seqdiff = CSeqNo::seqcmp(pkt.m_iSeqNo, self->m_iRcvCurrSeqNo);

    HLOGC(qrlog.Debug, log << "defaultPacketArrival: checking sequence " << pkt.m_iSeqNo
            << " against latest " << self->m_iRcvCurrSeqNo << " (distance: " << seqdiff << ")");

    // Loss detection.
    if (seqdiff > 1) // packet is later than the very subsequent packet
    {
        const int32_t seqlo = CSeqNo::incseq(self->m_iRcvCurrSeqNo);
        const int32_t seqhi = CSeqNo::decseq(pkt.m_iSeqNo);

        {
            // If loss found, insert them to the receiver loss list
            ScopedLock lg (self->m_RcvLossLock);
            self->m_pRcvLossList->insert(seqlo, seqhi);

            if (initial_loss_ttl)
            {
                // pack loss list for (possibly belated) NAK
                // The LOSSREPORT will be sent in a while.
                self->m_FreshLoss.push_back(CRcvFreshLoss(seqlo, seqhi, initial_loss_ttl));
                HLOGF(qrlog.Debug, "defaultPacketArrival: added loss sequence %d-%d (%d) with tolerance %d", seqlo, seqhi,
                        1+CSeqNo::seqcmp(seqhi, seqlo), initial_loss_ttl);
            }
        }

        if (!initial_loss_ttl)
        {
            // old code; run immediately when tolerance = 0
            // or this feature isn't used because of the peer
            output.push_back(make_pair(seqlo, seqhi));
        }
    }

    return output;
}
#endif

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
/// The tolerance is not increased infinitely - it's bordered by iMaxReorderTolerance.
/// This value can be set in options - SRT_LOSSMAXTTL.
void srt::CUDT::unlose(const CPacket &packet)
{
    ScopedLock lg(m_RcvLossLock);
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
            HLOGF(qrlog.Debug, "received out-of-band packet seq %d", sequence);

            const int seqdiff = abs(CSeqNo::seqcmp(m_iRcvCurrSeqNo, packet.m_iSeqNo));
            enterCS(m_StatsLock);
            m_stats.traceReorderDistance = max(seqdiff, m_stats.traceReorderDistance);
            leaveCS(m_StatsLock);
            if (seqdiff > m_iReorderTolerance)
            {
                const int new_tolerance = min(seqdiff, m_config.iMaxReorderTolerance);
                HLOGF(qrlog.Debug,
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
            HLOGC(qrlog.Debug, log << CONID() << "received reXmitted packet seq=" << sequence);
        }
    }
    else
    {
        HLOGF(qrlog.Debug, "received reXmitted or belated packet seq %d (distinction not supported by peer)", sequence);
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
        HLOGF(qrlog.Debug, "sequence %d removed from belated lossreport record", sequence);
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
            HLOGF(qrlog.Debug, "... arrived at TTL %d case %d", had_ttl, m_iConsecEarlyDelivery);

            // After 10 consecutive
            if (m_iConsecEarlyDelivery >= 10)
            {
                m_iConsecEarlyDelivery = 0;
                if (m_iReorderTolerance > 0)
                {
                    m_iReorderTolerance--;
                    enterCS(m_StatsLock);
                    m_stats.traceReorderDistance--;
                    leaveCS(m_StatsLock);
                    HLOGF(qrlog.Debug,
                          "... reached %d times - decreasing tolerance to %d",
                          m_iConsecEarlyDelivery,
                          m_iReorderTolerance);
                }
            }
        }
        // If hasn't increased tolerance, but the packet appeared at TTL less than 2, do nothing.
    }
}

void srt::CUDT::dropFromLossLists(int32_t from, int32_t to)
{
    ScopedLock lg(m_RcvLossLock);
    m_pRcvLossList->remove(from, to);

    HLOGF(qrlog.Debug, "%sTLPKTDROP seq %d-%d (%d packets)", CONID().c_str(), from, to, CSeqNo::seqoff(from, to));

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
int32_t srt::CUDT::bake(const sockaddr_any& addr, int32_t current_cookie, int correction)
{
    static unsigned int distractor = 0;
    unsigned int        rollover   = distractor + 10;

    for (;;)
    {
        // SYN cookie
        char clienthost[NI_MAXHOST];
        char clientport[NI_MAXSERV];
        getnameinfo(addr.get(),
                    addr.size(),
                    clienthost,
                    sizeof(clienthost),
                    clientport,
                    sizeof(clientport),
                    NI_NUMERICHOST | NI_NUMERICSERV);
        int64_t timestamp = (count_microseconds(steady_clock::now() - m_stats.tsStartTime) / 60000000) + distractor +
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

// [[using locked(m_pRcvQueue->m_LSLock)]];
int srt::CUDT::processConnectRequest(const sockaddr_any& addr, CPacket& packet)
{
    // XXX ASSUMPTIONS:
    // [[using assert(packet.m_iID == 0)]]

    HLOGC(cnlog.Debug, log << "processConnectRequest: received a connection request");

    if (m_bClosing)
    {
        m_RejectReason = SRT_REJ_CLOSE;
        HLOGC(cnlog.Debug, log << "processConnectRequest: ... NOT. Rejecting because closing.");
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
        HLOGC(cnlog.Debug, log << "processConnectRequest: ... NOT. Rejecting because broken.");
        return m_RejectReason;
    }
    // When CHandShake::m_iContentSize is used in log, the file fails to link!
    size_t exp_len = CHandShake::m_iContentSize;

    // NOTE!!! Old version of SRT code checks if the size of the HS packet
    // is EQUAL to the above CHandShake::m_iContentSize.

    // Changed to < exp_len because we actually need that the packet
    // be at least of a size for handshake, although it may contain
    // more data, depending on what's inside.
    if (packet.getLength() < exp_len)
    {
        m_RejectReason = SRT_REJ_ROGUE;
        HLOGC(cnlog.Debug,
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
        LOGC(cnlog.Error, log << "processConnectRequest: the packet received as handshake is not a handshake message");
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

    HLOGC(cnlog.Debug, log << "processConnectRequest: new cookie: " << hex << cookie_val);

    // REQUEST:INDUCTION.
    // Set a cookie, a target ID, and send back the same as
    // RESPONSE:INDUCTION.
    if (hs.m_iReqType == URQ_INDUCTION)
    {
        HLOGC(cnlog.Debug, log << "processConnectRequest: received type=induction, sending back with cookie+socket");

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
        hs.m_iType = SrtHSRequest::wrapFlags(true /*put SRT_MAGIC_CODE in HSFLAGS*/, m_config.iSndCryptoKeyLen);
        bool whether SRT_ATR_UNUSED = m_config.iSndCryptoKeyLen != 0;
        HLOGC(cnlog.Debug,
              log << "processConnectRequest: " << (whether ? "" : "NOT ")
                  << " Advertising PBKEYLEN - value = " << m_config.iSndCryptoKeyLen);

        size_t size = packet.getLength();
        hs.store_to((packet.m_pcData), (size));
        setPacketTS(packet, steady_clock::now());

        // Display the HS before sending it to peer
        HLOGC(cnlog.Debug, log << "processConnectRequest: SENDING HS (i): " << hs.show());

        m_pSndQueue->sendto(addr, packet);
        return SRT_REJ_UNKNOWN; // EXCEPTION: this is a "no-error" code.
    }

    // Otherwise this should be REQUEST:CONCLUSION.
    // Should then come with the correct cookie that was
    // set in the above INDUCTION, in the HS_VERSION_SRT1
    // should also contain extra data.

    if (!hs.valid())
    {
        LOGC(cnlog.Error, log << "processConnectRequest: ROGUE HS RECEIVED. Rejecting");
        m_RejectReason = SRT_REJ_ROGUE;
        return SRT_REJ_ROGUE;
    }

    HLOGC(cnlog.Debug,
          log << "processConnectRequest: received type=" << RequestTypeStr(hs.m_iReqType) << " - checking cookie...");
    if (hs.m_iCookie != cookie_val)
    {
        cookie_val = bake(addr, cookie_val, -1); // SHOULD generate an earlier, distracted cookie

        if (hs.m_iCookie != cookie_val)
        {
            m_RejectReason = SRT_REJ_RDVCOOKIE;
            HLOGC(cnlog.Debug, log << "processConnectRequest: ...wrong cookie " << hex << cookie_val << ". Ignoring.");
            return m_RejectReason;
        }

        HLOGC(cnlog.Debug, log << "processConnectRequest: ... correct (FIXED) cookie. Proceeding.");
    }
    else
    {
        HLOGC(cnlog.Debug, log << "processConnectRequest: ... correct (ORIGINAL) cookie. Proceeding.");
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
        HLOGC(cnlog.Debug,
              log << "processConnectRequest: version/type mismatch. Sending REJECT code:" << m_RejectReason
              << " MSG: " << srt_rejectreason_str(m_RejectReason));
        // mismatch, reject the request
        hs.m_iReqType = URQFailure(m_RejectReason);
        size_t size   = CHandShake::m_iContentSize;
        hs.store_to((packet.m_pcData), (size));
        packet.m_iID        = id;
        setPacketTS(packet, steady_clock::now());
        HLOGC(cnlog.Debug, log << "processConnectRequest: SENDING HS (e): " << hs.show());
        m_pSndQueue->sendto(addr, packet);
    }
    else
    {
        // IMPORTANT!!!
        // If the newConnection() detects there is already a socket connection associated with the remote peer,
        // it returns the socket via `acpu`, and the `result` returned is 0.
        // Else if a new connection is successfully created, the conclusion handshake response
        // is sent by the function itself (it calls the acceptAndRespond(..)), the `acpu` remains null, the `result` is 1.
        int error  = SRT_REJ_UNKNOWN;
        CUDT* acpu = NULL;
        int result = uglobal().newConnection(m_SocketID, addr, packet, (hs), (error), (acpu));

        // This is listener - m_RejectReason need not be set
        // because listener has no functionality of giving the app
        // insight into rejected callers.

        // --->
        //        (global.) CUDTUnited::updateListenerMux
        //        (new Socket.) CUDT::acceptAndRespond
        if (result == -1)
        {
            hs.m_iReqType = URQFailure(error);
            LOGF(cnlog.Warn, "processConnectRequest: rsp(REJECT): %d - %s", hs.m_iReqType, srt_rejectreason_str(error));
        }

        // The `acpu` not NULL means connection exists, the `result` should be 0. It is not checked here though.
        // The `newConnection(..)` only sends reponse for newly created connection.
        // The connection already exists (no new connection has been created, no response sent).
        // Send the conclusion response manually here in case the peer has missed the first one.
        // The value  `result` here should be 0.
        if (acpu)
        {
            // This is an existing connection, so the handshake is only needed
            // because of the rule that every handshake request must be covered
            // by the handshake response. It wouldn't be good to call interpretSrtHandshake
            // here because the data from the handshake have been already interpreted
            // and recorded. We just need to craft a response.
            HLOGC(cnlog.Debug,
                  log << CONID() << "processConnectRequest: sending REPEATED handshake response req="
                      << RequestTypeStr(hs.m_iReqType));

            // Rewrite already updated previously data in acceptAndRespond
            acpu->rewriteHandshakeData(acpu->m_PeerAddr, (hs));

            uint32_t kmdata[SRTDATA_MAXSIZE];
            size_t   kmdatasize = SRTDATA_MAXSIZE;
            EConnectStatus conn = CONN_ACCEPT;

            if (hs.m_iVersion >= HS_VERSION_SRT1)
            {
                // Always attach extension.
                hs.m_extension = true;
                conn = acpu->craftKmResponse((kmdata), (kmdatasize));
            }
            else
            {
                kmdatasize = 0;
            }

            if (conn != CONN_ACCEPT)
                return conn;

            packet.setLength(m_iMaxSRTPayloadSize);
            if (!acpu->createSrtHandshake(SRT_CMD_HSRSP, SRT_CMD_KMRSP,
                        kmdata, kmdatasize,
                        (packet), (hs)))
            {
                HLOGC(cnlog.Debug,
                        log << "processConnectRequest: rejecting due to problems in createSrtHandshake.");
                result = -1; // enforce fallthrough for the below condition!
                hs.m_iReqType = URQFailure(m_RejectReason == SRT_REJ_UNKNOWN ? int(SRT_REJ_IPE) : m_RejectReason.load());
            }
            else
            {
                // Send the crafted handshake
                HLOGC(cnlog.Debug, log << "processConnectRequest: SENDING (repeated) HS (a): " << hs.show());
                acpu->addressAndSend((packet));
            }
        }

        if (result == 1)
        {
            // BUG! There is no need to update write-readiness on the listener socket once new connection is accepted.
            // Only read-readiness has to be updated, but it is done so in the newConnection(..) function.
            // See PR #1831 and issue #1667.
            HLOGC(cnlog.Debug, log << "processConnectRequest: @" << m_SocketID
                   << " accepted connection, updating epoll to write-ready");

            // New connection has been accepted or an existing one has been found. Update epoll write-readiness.
            // a new connection has been created, enable epoll for write
            // Note: not using SRT_EPOLL_CONNECT symbol because this is a procedure
            // executed for the accepted socket.
            uglobal().m_EPoll.update_events(m_SocketID, m_sPollID, SRT_EPOLL_OUT, true);
        }
        else if (result == -1)
        {
            // The new connection failed
            // or the connection already existed, but manually sending the HS response above has failed.
            // HSv4: Send the SHUTDOWN message to the peer (see PR #2010) in order to disallow the peer to connect.
            //       The HSv4 clients do not interpret the error handshake response correctly.
            // HSv5: Send a handshake with an error code (hs.m_iReqType set earlier) to the peer.
            if (hs.m_iVersion < HS_VERSION_SRT1)
            {
                HLOGC(cnlog.Debug, log << CONID() << "processConnectRequest: HSv4 caller, sending SHUTDOWN after rejection with "
                        << RequestTypeStr(hs.m_iReqType));
                CPacket rsp;
                setPacketTS((rsp), steady_clock::now());
                rsp.pack(UMSG_SHUTDOWN);
                rsp.m_iID = m_PeerID;
                m_pSndQueue->sendto(addr, rsp);
            }
            else
            {
                HLOGC(cnlog.Debug,
                        log << CONID() << "processConnectRequest: sending ABNORMAL handshake info req="
                        << RequestTypeStr(hs.m_iReqType));
                size_t size = CHandShake::m_iContentSize;
                hs.store_to((packet.m_pcData), (size));
                packet.setLength(size);
                packet.m_iID = id;
                setPacketTS(packet, steady_clock::now());
                HLOGC(cnlog.Debug, log << "processConnectRequest: SENDING HS (a): " << hs.show());
                m_pSndQueue->sendto(addr, packet);
            }
        }
    }
    LOGC(cnlog.Note, log << "listen ret: " << hs.m_iReqType << " - " << RequestTypeStr(hs.m_iReqType));

    return RejectReasonForURQ(hs.m_iReqType);
}

void srt::CUDT::addLossRecord(std::vector<int32_t> &lr, int32_t lo, int32_t hi)
{
    if (lo == hi)
        lr.push_back(lo);
    else
    {
        lr.push_back(lo | LOSSDATA_SEQNO_RANGE_FIRST);
        lr.push_back(hi);
    }
}

int srt::CUDT::checkACKTimer(const steady_clock::time_point &currtime)
{
    int because_decision = BECAUSE_NO_REASON;
    if (currtime > m_tsNextACKTime.load()  // ACK time has come
                                  // OR the number of sent packets since last ACK has reached
                                  // the congctl-defined value of ACK Interval
                                  // (note that none of the builtin congctls defines ACK Interval)
        || (m_CongCtl->ACKMaxPackets() > 0 && m_iPktCount >= m_CongCtl->ACKMaxPackets()))
    {
        // ACK timer expired or ACK interval is reached
        sendCtrl(UMSG_ACK);

        const steady_clock::duration ack_interval = m_CongCtl->ACKTimeout_us() > 0
            ? microseconds_from(m_CongCtl->ACKTimeout_us())
            : m_tdACKInterval;
        m_tsNextACKTime.store(currtime + ack_interval);

        m_iPktCount      = 0;
        m_iLightACKCount = 1;
        because_decision = BECAUSE_ACK;
    }

    // Or the transfer rate is so high that the number of packets
    // have reached the value of SelfClockInterval * LightACKCount before
    // the time has come according to m_tsNextACKTime. In this case a "lite ACK"
    // is sent, which doesn't contain statistical data and nothing more
    // than just the ACK number. The "fat ACK" packets will be still sent
    // normally according to the timely rules.
    else if (m_iPktCount >= SELF_CLOCK_INTERVAL * m_iLightACKCount)
    {
        // send a "light" ACK
        sendCtrl(UMSG_ACK, NULL, NULL, SEND_LITE_ACK);
        ++m_iLightACKCount;
        because_decision = BECAUSE_LITEACK;
    }

    return because_decision;
}

int srt::CUDT::checkNAKTimer(const steady_clock::time_point& currtime)
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
    if (!m_config.bRcvNakReport || m_PktFilterRexmitLevel != SRT_ARQ_ALWAYS)
        return BECAUSE_NO_REASON;

    /*
     * m_config.bRcvNakReport enables NAK reports for SRT.
     * Retransmission based on timeout is bandwidth consuming,
     * not knowing what to retransmit when the only NAK sent by receiver is lost,
     * all packets past last ACK are retransmitted (rexmitMethod() == SRM_FASTREXMIT).
     */
    const int loss_len = m_pRcvLossList->getLossLength();
    SRT_ASSERT(loss_len >= 0);
    int debug_decision = BECAUSE_NO_REASON;

    if (loss_len > 0)
    {
        if (currtime <= m_tsNextNAKTime.load())
            return BECAUSE_NO_REASON; // wait for next NAK time

        sendCtrl(UMSG_LOSSREPORT);
        debug_decision = BECAUSE_NAKREPORT;
    }

    m_tsNextNAKTime.store(currtime + m_tdNAKInterval);
    return debug_decision;
}

bool srt::CUDT::checkExpTimer(const steady_clock::time_point& currtime, int check_reason SRT_ATR_UNUSED)
{
    // VERY HEAVY LOGGING
#if ENABLE_HEAVY_LOGGING & 1
    static const char* const decisions [] = {
        "ACK",
        "LITE-ACK",
        "NAKREPORT"
    };

    string decision = "NOTHING";
    if (check_reason)
    {
        ostringstream decd;
        decision = "";
        for (int i = 0; i < LAST_BECAUSE_BIT; ++i)
        {
            int flag = 1 << i;
            if (check_reason & flag)
                decd << decisions[i] << " ";
        }
        decision = decd.str();
    }
    HLOGC(xtlog.Debug, log << CONID() << "checkTimer: ACTIVITIES PERFORMED: " << decision);
#endif

    // In UDT the m_bUserDefinedRTO and m_iRTO were in CCC class.
    // There's nothing in the original code that alters these values.

    steady_clock::time_point next_exp_time;
    if (m_CongCtl->RTO())
    {
        next_exp_time = m_tsLastRspTime.load() + microseconds_from(m_CongCtl->RTO());
    }
    else
    {
        steady_clock::duration exp_timeout =
            microseconds_from(m_iEXPCount * (m_iSRTT + 4 * m_iRTTVar) + COMM_SYN_INTERVAL_US);
        if (exp_timeout < (m_iEXPCount * m_tdMinExpInterval))
            exp_timeout = m_iEXPCount * m_tdMinExpInterval;
        next_exp_time = m_tsLastRspTime.load() + exp_timeout;
    }

    if (currtime <= next_exp_time && !m_bBreakAsUnstable)
        return false;

    // ms -> us
    const int PEER_IDLE_TMO_US = m_config.iPeerIdleTimeout_ms * 1000;
    // Haven't received any information from the peer, is it dead?!
    // timeout: at least 16 expirations and must be greater than 5 seconds
    time_point last_rsp_time = m_tsLastRspTime.load();
    if (m_bBreakAsUnstable || ((m_iEXPCount > COMM_RESPONSE_MAX_EXP) &&
        (currtime - last_rsp_time > microseconds_from(PEER_IDLE_TMO_US))))
    {
        //
        // Connection is broken.
        // UDT does not signal any information about this instead of to stop quietly.
        // Application will detect this when it calls any UDT methods next time.
        //
        HLOGC(xtlog.Debug,
              log << "CONNECTION EXPIRED after " << count_milliseconds(currtime - last_rsp_time) << "ms");
        m_bClosing       = true;
        m_bBroken        = true;
        m_iBrokenCounter = 30;

        // update snd U list to remove this socket
        m_pSndQueue->m_pSndUList->update(this, CSndUList::DO_RESCHEDULE);

        updateBrokenConnection();
        completeBrokenConnectionDependencies(SRT_ECONNLOST); // LOCKS!

        return true;
    }

    HLOGC(xtlog.Debug,
          log << "EXP TIMER: count=" << m_iEXPCount << "/" << (+COMM_RESPONSE_MAX_EXP) << " elapsed="
              << (count_microseconds(currtime - last_rsp_time)) << "/" << (+PEER_IDLE_TMO_US) << "us");

    ++m_iEXPCount;

    /*
     * (keepalive fix)
     * duB:
     * It seems there is confusion of the direction of the Response here.
     * lastRspTime is supposed to be when receiving (data/ctrl) from peer
     * as shown in processCtrl and processData,
     * Here we set because we sent something?
     *
     * Disabling this code that prevent quick reconnection when peer disappear
     */
    // Reset last response time since we've just sent a heart-beat.
    // (fixed) m_tsLastRspTime = currtime_tk;

    return false;
}

void srt::CUDT::checkRexmitTimer(const steady_clock::time_point& currtime)
{
    // There are two algorithms of blind packet retransmission: LATEREXMIT and FASTREXMIT.
    //
    // LATEREXMIT is only used with FileCC.
    // The RTO is triggered when some time has passed since the last ACK from
    // the receiver, while there is still some unacknowledged data in the sender's buffer,
    // and the loss list is empty at the moment of RTO (nothing to retransmit yet).
    //
    // FASTREXMIT is only used with LiveCC.
    // The RTO is triggered if the receiver is not configured to send periodic NAK reports,
    // when some time has passed since the last ACK from the receiver,
    // while there is still some unacknowledged data in the sender's buffer.
    //
    // In case the above conditions are met, the unacknowledged packets
    // in the sender's buffer will be added to the SND loss list and retransmitted.
    //

    const uint64_t rtt_syn = (m_iSRTT + 4 * m_iRTTVar + 2 * COMM_SYN_INTERVAL_US);
    const uint64_t exp_int_us = (m_iReXmitCount * rtt_syn + COMM_SYN_INTERVAL_US);

    if (currtime <= (m_tsLastRspAckTime + microseconds_from(exp_int_us)))
        return;

    // If there is no unacknowledged data in the sending buffer,
    // then there is nothing to retransmit.
    if (m_pSndBuffer->getCurrBufSize() <= 0)
        return;

    const bool is_laterexmit = m_CongCtl->rexmitMethod() == SrtCongestion::SRM_LATEREXMIT; // FileCC
    const bool is_fastrexmit = m_CongCtl->rexmitMethod() == SrtCongestion::SRM_FASTREXMIT; // LiveCC

    // If the receiver will send periodic NAK reports, then FASTREXMIT (live) is inactive.
    // TODO: Probably some method of "blind rexmit" MUST BE DONE, when TLPKTDROP is off.
    if (is_fastrexmit && m_bPeerNakReport)
        return;

    // Schedule a retransmission IF:
    // - there are packets in flight (getFlightSpan() > 0);
    // - in case of LATEREXMIT (File Mode): the sender loss list is empty
    //   (the receiver didn't send any LOSSREPORT, or LOSSREPORT was lost on track).
    // - in case of FASTREXMIT (Live Mode): the RTO (rtt_syn) was triggered, therefore
    //   schedule unacknowledged packets for retransmission regardless of the loss list emptiness.
    if (getFlightSpan() > 0 && (!is_laterexmit || m_pSndLossList->getLossLength() == 0))
    {
        // Sender: Insert all the packets sent after last received acknowledgement into the sender loss list.
        ScopedLock acklock(m_RecvAckLock); // Protect packet retransmission
        // Resend all unacknowledged packets on timeout, but only if there is no packet in the loss list
        const int32_t csn = m_iSndCurrSeqNo;
        const int     num = m_pSndLossList->insert(m_iSndLastAck, csn);
        if (num > 0)
        {
            enterCS(m_StatsLock);
            m_stats.sndr.lost.count(num);
            leaveCS(m_StatsLock);

            HLOGC(xtlog.Debug,
                  log << CONID() << "ENFORCED " << (is_laterexmit ? "LATEREXMIT" : "FASTREXMIT")
                      << " by ACK-TMOUT (scheduling): " << CSeqNo::incseq(m_iSndLastAck) << "-" << csn << " ("
                      << CSeqNo::seqoff(m_iSndLastAck, csn) << " packets)");
        }
    }

    ++m_iReXmitCount;

    checkSndTimers(DONT_REGEN_KM);
    const ECheckTimerStage stage = is_fastrexmit ? TEV_CHT_FASTREXMIT : TEV_CHT_REXMIT;
    updateCC(TEV_CHECKTIMER, EventVariant(stage));

    // schedule sending if not scheduled already
    m_pSndQueue->m_pSndUList->update(this, CSndUList::DONT_RESCHEDULE);
}

void srt::CUDT::checkTimers()
{
    // update CC parameters
    updateCC(TEV_CHECKTIMER, EventVariant(TEV_CHT_INIT));

    const steady_clock::time_point currtime = steady_clock::now();

    // This is a very heavy log, unblock only for temporary debugging!
#if 0
    HLOGC(xtlog.Debug, log << CONID() << "checkTimers: nextacktime=" << FormatTime(m_tsNextACKTime)
        << " AckInterval=" << m_iACKInterval
        << " pkt-count=" << m_iPktCount << " liteack-count=" << m_iLightACKCount);
#endif

    // Check if it is time to send ACK
    int debug_decision = checkACKTimer(currtime);

    // Check if it is time to send a loss report
    debug_decision |= checkNAKTimer(currtime);

    // Check if the connection is expired
    if (checkExpTimer(currtime, debug_decision))
        return;

    // Check if FAST or LATE packet retransmission is required
    checkRexmitTimer(currtime);

    if (currtime > m_tsLastSndTime.load() + microseconds_from(COMM_KEEPALIVE_PERIOD_US))
    {
        sendCtrl(UMSG_KEEPALIVE);
#if ENABLE_BONDING
        if (m_parent->m_GroupOf)
        {
            ScopedLock glock (uglobal().m_GlobControlLock);
            if (m_parent->m_GroupOf)
            {
                // Pass socket ID because it's about changing group socket data
                m_parent->m_GroupOf->internalKeepalive(m_parent->m_GroupMemberData);
                // NOTE: GroupLock is unnecessary here because the only data read and
                // modified is the target of the iterator from m_GroupMemberData. The
                // iterator will be valid regardless of any container modifications.
            }
        }
#endif
        HLOGP(xtlog.Debug, "KEEPALIVE");
    }
}

void srt::CUDT::updateBrokenConnection()
{
    m_bClosing = true;
    releaseSynch();
    // app can call any UDT API to learn the connection_broken error
    uglobal().m_EPoll.update_events(m_SocketID, m_sPollID, SRT_EPOLL_IN | SRT_EPOLL_OUT | SRT_EPOLL_ERR, true);
    CGlobEvent::triggerEvent();
}

void srt::CUDT::completeBrokenConnectionDependencies(int errorcode)
{
    int token = -1;

#if ENABLE_BONDING
    bool pending_broken = false;
    {
        ScopedLock guard_group_existence (uglobal().m_GlobControlLock);
        if (m_parent->m_GroupOf)
        {
            token = m_parent->m_GroupMemberData->token;
            if (m_parent->m_GroupMemberData->sndstate == SRT_GST_PENDING)
            {
                HLOGC(gmlog.Debug, log << "updateBrokenConnection: a pending link was broken - will be removed");
                pending_broken = true;
            }
            else
            {
                HLOGC(gmlog.Debug, log << "updateBrokenConnection: state=" << CUDTGroup::StateStr(m_parent->m_GroupMemberData->sndstate) << " a used link was broken - not closing automatically");
            }

            m_parent->m_GroupMemberData->sndstate = SRT_GST_BROKEN;
            m_parent->m_GroupMemberData->rcvstate = SRT_GST_BROKEN;
        }
    }
#endif

    if (m_cbConnectHook)
    {
        CALLBACK_CALL(m_cbConnectHook, m_SocketID, errorcode, m_PeerAddr.get(), token);
    }

#if ENABLE_BONDING
    {
        // Lock GlobControlLock in order to make sure that
        // the state if the socket having the group and the
        // existence of the group will not be changed during
        // the operation. The attempt of group deletion will
        // have to wait until this operation completes.
        ScopedLock lock(uglobal().m_GlobControlLock);
        CUDTGroup* pg = m_parent->m_GroupOf;
        if (pg)
        {
            // Bound to one call because this requires locking
            pg->updateFailedLink();
        }
    }

    // Sockets that never succeeded to connect must be deleted
    // explicitly, otherwise they will never be deleted.
    if (pending_broken)
    {
        // XXX This somehow can cause a deadlock
        // uglobal()->close(m_parent);
        m_parent->setBrokenClosed();
    }
#endif
}

void srt::CUDT::addEPoll(const int eid)
{
    enterCS(uglobal().m_EPoll.m_EPollLock);
    m_sPollID.insert(eid);
    leaveCS(uglobal().m_EPoll.m_EPollLock);

    if (!stillConnected())
        return;

    enterCS(m_RecvLock);
    if (isRcvBufferReady())
    {
        uglobal().m_EPoll.update_events(m_SocketID, m_sPollID, SRT_EPOLL_IN, true);
    }
    leaveCS(m_RecvLock);

    if (m_config.iSndBufSize > m_pSndBuffer->getCurrBufSize())
    {
        uglobal().m_EPoll.update_events(m_SocketID, m_sPollID, SRT_EPOLL_OUT, true);
    }
}

void srt::CUDT::removeEPollEvents(const int eid)
{
    // clear IO events notifications;
    // since this happens after the epoll ID has been removed, they cannot be set again
    set<int> remove;
    remove.insert(eid);
    uglobal().m_EPoll.update_events(m_SocketID, remove, SRT_EPOLL_IN | SRT_EPOLL_OUT, false);
}

void srt::CUDT::removeEPollID(const int eid)
{
    enterCS(uglobal().m_EPoll.m_EPollLock);
    m_sPollID.erase(eid);
    leaveCS(uglobal().m_EPoll.m_EPollLock);
}

void srt::CUDT::ConnectSignal(ETransmissionEvent evt, EventSlot sl)
{
    if (evt >= TEV_E_SIZE)
        return; // sanity check

    m_Slots[evt].push_back(sl);
}

void srt::CUDT::DisconnectSignal(ETransmissionEvent evt)
{
    if (evt >= TEV_E_SIZE)
        return; // sanity check

    m_Slots[evt].clear();
}

void srt::CUDT::EmitSignal(ETransmissionEvent tev, EventVariant var)
{
    for (std::vector<EventSlot>::iterator i = m_Slots[tev].begin(); i != m_Slots[tev].end(); ++i)
    {
        i->emit(tev, var);
    }
}

int srt::CUDT::getsndbuffer(SRTSOCKET u, size_t *blocks, size_t *bytes)
{
    CUDTSocket *s = uglobal().locateSocket(u);
    if (!s)
        return -1;

    CSndBuffer *b = s->core().m_pSndBuffer;

    if (!b)
        return -1;

    int bytecount, timespan;
    int count = b->getCurrBufSize((bytecount), (timespan));

    if (blocks)
        *blocks = count;

    if (bytes)
        *bytes = bytecount;

    return std::abs(timespan);
}

int srt::CUDT::rejectReason(SRTSOCKET u)
{
    CUDTSocket* s = uglobal().locateSocket(u);
    if (!s)
        return SRT_REJ_UNKNOWN;

    return s->core().m_RejectReason;
}

int srt::CUDT::rejectReason(SRTSOCKET u, int value)
{
    CUDTSocket* s = uglobal().locateSocket(u);
    if (!s)
        return APIError(MJ_NOTSUP, MN_SIDINVAL);

    if (value < SRT_REJC_PREDEFINED)
        return APIError(MJ_NOTSUP, MN_INVAL);

    s->core().m_RejectReason = value;
    return 0;
}

int64_t srt::CUDT::socketStartTime(SRTSOCKET u)
{
    CUDTSocket* s = uglobal().locateSocket(u);
    if (!s)
        return APIError(MJ_NOTSUP, MN_SIDINVAL);

    return count_microseconds(s->core().m_stats.tsStartTime.time_since_epoch());
}

bool srt::CUDT::runAcceptHook(CUDT *acore, const sockaddr* peer, const CHandShake& hs, const CPacket& hspkt)
{
    // Prepare the information for the hook.

    // We need streamid.
    char target[CSrtConfig::MAX_SID_LENGTH + 1];
    memset((target), 0, CSrtConfig::MAX_SID_LENGTH + 1);

    // Just for a case, check the length.
    // This wasn't done before, and we could risk memory crash.
    // In case of error, this will remain unset and the empty
    // string will be passed as streamid.

    int ext_flags = SrtHSRequest::SRT_HSTYPE_HSFLAGS::unwrap(hs.m_iType);

#if ENABLE_BONDING
    bool have_group = false;
    SRT_GROUP_TYPE gt = SRT_GTYPE_UNDEFINED;
#endif

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
            int cmd = FindExtensionBlock(begin, length, (blocklen), (next));

            const size_t bytelen = blocklen * sizeof(uint32_t);

            if (cmd == SRT_CMD_SID)
            {
                if (!bytelen || bytelen > CSrtConfig::MAX_SID_LENGTH)
                {
                    LOGC(cnlog.Error,
                         log << "interpretSrtHandshake: STREAMID length " << bytelen << " is 0 or > " << +CSrtConfig::MAX_SID_LENGTH
                             << " - PROTOCOL ERROR, REJECTING");
                    return false;
                }
                // See comment at CUDT::interpretSrtHandshake().
                memcpy((target), begin + 1, bytelen);

                // Un-swap on big endian machines
                ItoHLA(((uint32_t *)target), (uint32_t *)target, blocklen);
            }
#if ENABLE_BONDING
            else if (cmd == SRT_CMD_GROUP)
            {
                uint32_t* groupdata = begin + 1;
                have_group = true; // Even if parse error happes
                if (bytelen / sizeof(int32_t) >= GRPD_E_SIZE)
                {
                    uint32_t gd = groupdata[GRPD_GROUPDATA];
                    gt = SRT_GROUP_TYPE(SrtHSRequest::HS_GROUP_TYPE::unwrap(gd));
                }
            }
#endif
            else if (cmd == SRT_CMD_NONE)
            {
                // End of blocks
                break;
            }

            // Any other kind of message extracted. Search on.
            if (!NextExtensionBlock((begin), next, (length)))
                break;
        }
    }

#if ENABLE_BONDING
    if (have_group && acore->m_config.iGroupConnect == 0)
    {
        HLOGC(cnlog.Debug, log << "runAcceptHook: REJECTING connection WITHOUT calling the hook - groups not allowed");
        return false;
    }

    // Update the groupconnect flag
    acore->m_config.iGroupConnect = have_group ? 1 : 0;
    acore->m_HSGroupType = gt;
#endif

    try
    {
        int result = CALLBACK_CALL(m_cbAcceptHook, acore->m_SocketID, hs.m_iVersion, peer, target);
        if (result == -1)
            return false;
    }
    catch (...)
    {
        LOGP(cnlog.Warn, "runAcceptHook: hook interrupted by exception");
        return false;
    }

    return true;
}

void srt::CUDT::processKeepalive(const CPacket& ctrlpkt, const time_point& tsArrival)
{
    // Here can be handled some protocol definition
    // for extra data sent through keepalive.

#if ENABLE_BONDING
    if (m_parent->m_GroupOf)
    {
        // Lock GlobControlLock in order to make sure that
        // the state if the socket having the group and the
        // existence of the group will not be changed during
        // the operation. The attempt of group deletion will
        // have to wait until this operation completes.
        ScopedLock lock(uglobal().m_GlobControlLock);
        CUDTGroup* pg = m_parent->m_GroupOf;
        if (pg)
        {
            // Whether anything is to be done with this socket
            // about the fact that keepalive arrived, let the
            // group handle it
            pg->processKeepalive(m_parent->m_GroupMemberData);
        }
    }
#endif

#if ENABLE_NEW_RCVBUFFER
    ScopedLock lck(m_RcvBufferLock);
    m_pRcvBuffer->updateTsbPdTimeBase(ctrlpkt.getMsgTimeStamp());
    if (m_config.bDriftTracer)
        m_pRcvBuffer->addRcvTsbPdDriftSample(ctrlpkt.getMsgTimeStamp(), tsArrival, -1);
#endif
}
