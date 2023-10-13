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
   Haivision Systems Inc.
*****************************************************************************/

#ifndef INC_SRT_SOCKETCONFIG_H
#define INC_SRT_SOCKETCONFIG_H

#include "platform_sys.h"
#ifdef SRT_ENABLE_BINDTODEVICE
#include <linux/if.h>
#endif
#include <string>
#include "haicrypt.h"
#include "congctl.h"
#include "packet.h"
#include "handshake.h"
#include "logger_defs.h"
#include "packetfilter.h"

// SRT Version constants
#define SRT_VERSION_UNK     0
#define SRT_VERSION_MAJ1    0x010000            /* Version 1 major */
#define SRT_VERSION_MAJ(v) (0xFF0000 & (v))     /* Major number ensuring backward compatibility */
#define SRT_VERSION_MIN(v) (0x00FF00 & (v))
#define SRT_VERSION_PCH(v) (0x0000FF & (v))

// NOTE: SRT_VERSION is primarily defined in the build file.
extern const int32_t SRT_DEF_VERSION;

namespace srt
{

struct CSrtMuxerConfig
{
    static const int DEF_UDP_BUFFER_SIZE = 65536;

    int  iIpTTL;
    int  iIpToS;
    int  iIpV6Only;  // IPV6_V6ONLY option (-1 if not set)
    bool bReuseAddr; // reuse an exiting port or not, for UDP multiplexer

#ifdef SRT_ENABLE_BINDTODEVICE
    std::string sBindToDevice;
#endif
    int iUDPSndBufSize; // UDP sending buffer size
    int iUDPRcvBufSize; // UDP receiving buffer size

    // NOTE: this operator is not reversable. The syntax must use:
    //  muxer_entry == socket_entry
    bool isCompatWith(const CSrtMuxerConfig& other) const
    {
#define CEQUAL(field) (field == other.field)
        return CEQUAL(iIpTTL)
            && CEQUAL(iIpToS)
            && CEQUAL(bReuseAddr)
#ifdef SRT_ENABLE_BINDTODEVICE
            && CEQUAL(sBindToDevice)
#endif
            && CEQUAL(iUDPSndBufSize)
            && CEQUAL(iUDPRcvBufSize)
            && (other.iIpV6Only == -1 || CEQUAL(iIpV6Only))
            // NOTE: iIpV6Only is not regarded because
            // this matches only in case of IPv6 with "any" address.
            // And this aspect must be checked separately because here
            // this procedure has no access to neither the address,
            // nor the IP version (family).
#undef CEQUAL
            && true;
    }

    CSrtMuxerConfig()
        : iIpTTL(-1) /* IPv4 TTL or IPv6 HOPs [1..255] (-1:undefined) */
        , iIpToS(-1) /* IPv4 Type of Service or IPv6 Traffic Class [0x00..0xff] (-1:undefined) */
        , iIpV6Only(-1)
        , bReuseAddr(true) // This is default in SRT
        , iUDPSndBufSize(DEF_UDP_BUFFER_SIZE)
        , iUDPRcvBufSize(DEF_UDP_BUFFER_SIZE)
    {
    }
};

struct CSrtConfig;

template <size_t SIZE>
class StringStorage
{
    char     stor[SIZE + 1];
    uint16_t len;

    // NOTE: default copying allowed.

public:
    StringStorage()
        : len(0)
    {
        memset(stor, 0, sizeof stor);
    }

    bool set(const char* s, size_t length)
    {
        if (length > SIZE)
            return false;

        memcpy(stor, s, length);
        stor[length] = 0;
        len          = (int) length;
        return true;
    }

    bool set(const std::string& s)
    {
        return set(s.c_str(), s.size());
    }

    size_t copy(char* s, size_t length) const
    {
        if (!s)
            return 0;

        size_t copy_len = std::min((size_t)len, length);
        memcpy(s, stor, copy_len);
        return copy_len;
    }

    std::string str() const
    {
        return len == 0 ? std::string() : std::string(stor);
    }

    const char* c_str() const
    {
        return stor;
    }

    size_t size() const { return size_t(len); }
    bool   empty() const { return len == 0; }
};

struct CSrtConfig: CSrtMuxerConfig
{
    typedef srt::sync::steady_clock::time_point time_point;
    typedef srt::sync::steady_clock::duration   duration;

    static const int
        DEF_MSS = 1500,
        DEF_FLIGHT_SIZE = 25600,
        DEF_BUFFER_SIZE = 8192, //Rcv buffer MUST NOT be bigger than Flight Flag size
        DEF_LINGER_S = 3*60,    // 3 minutes
        DEF_CONNTIMEO_S = 3;    // 3 seconds

    enum
    {
        CIPHER_MODE_AUTO = 0,
        CIPHER_MODE_AES_CTR = 1,
        CIPHER_MODE_AES_GCM = 2
    };

    static const int      COMM_RESPONSE_TIMEOUT_MS      = 5 * 1000; // 5 seconds
    static const uint32_t COMM_DEF_MIN_STABILITY_TIMEOUT_MS = 60;   // 60 ms

    // Mimimum recv flight flag size is 32 packets
    static const int    DEF_MIN_FLIGHT_PKT = 32;
    static const size_t MAX_SID_LENGTH     = 512;
    static const size_t MAX_PFILTER_LENGTH = 64;
    static const size_t MAX_CONG_LENGTH    = 16;

    int    iMSS;            // Maximum Segment Size, in bytes
    size_t zExpPayloadSize; // Expected average payload size (user option)

    // Options
    bool   bSynSending;     // Sending synchronization mode
    bool   bSynRecving;     // Receiving synchronization mode
    int    iFlightFlagSize; // Maximum number of packets in flight from the peer side
    int    iSndBufSize;     // Maximum UDT sender buffer size
    int    iRcvBufSize;     // Maximum UDT receiver buffer size
    linger Linger;          // Linger information on close
    bool   bRendezvous;     // Rendezvous connection mode

    duration tdConnTimeOut; // connect timeout in milliseconds
    bool     bDriftTracer;
    int      iSndTimeOut; // sending timeout in milliseconds
    int      iRcvTimeOut; // receiving timeout in milliseconds
    int64_t  llMaxBW;     // maximum data transfer rate (threshold)
#ifdef ENABLE_MAXREXMITBW
    int64_t  llMaxRexmitBW; // maximum bandwidth limit for retransmissions (Bytes/s).
#endif

    // These fields keep the options for encryption
    // (SRTO_PASSPHRASE, SRTO_PBKEYLEN). Crypto object is
    // created later and takes values from these.
    HaiCrypt_Secret CryptoSecret;
    int             iSndCryptoKeyLen;

    // XXX Consider removing. The bDataSender stays here
    // in order to maintain the HS side selection in HSv4.
    bool bDataSender;

    bool     bMessageAPI;
    bool     bTSBPD;        // Whether AGENT will do TSBPD Rx (whether peer does, is not agent's problem)
    int      iRcvLatency;   // Agent's Rx latency
    int      iPeerLatency;  // Peer's Rx latency for the traffic made by Agent's Tx.
    bool     bTLPktDrop;    // Whether Agent WILL DO TLPKTDROP on Rx.
    int      iSndDropDelay; // Extra delay when deciding to snd-drop for TLPKTDROP, -1 to off
    bool     bEnforcedEnc;  // Off by default. When on, any connection other than nopw-nopw & pw1-pw1 is rejected.
    int      iGroupConnect;    // 1 - allow group connections
    int      iPeerIdleTimeout_ms; // Timeout for hearing anything from the peer (ms).
    uint32_t uMinStabilityTimeout_ms;
    int      iRetransmitAlgo;
    int      iCryptoMode; // SRTO_CRYPTOMODE

    int64_t llInputBW;         // Input stream rate (bytes/sec). 0: use internally estimated input bandwidth
    int64_t llMinInputBW;      // Minimum input stream rate estimate (bytes/sec)
    int  iOverheadBW;          // Percent above input stream rate (applies if llMaxBW == 0)
    bool bRcvNakReport;        // Enable Receiver Periodic NAK Reports
    int  iMaxReorderTolerance; //< Maximum allowed value for dynamic reorder tolerance

    // For the use of CCryptoControl
    // HaiCrypt configuration
    unsigned int uKmRefreshRatePkt;
    unsigned int uKmPreAnnouncePkt;

    uint32_t uSrtVersion;
    uint32_t uMinimumPeerSrtVersion;

    StringStorage<MAX_CONG_LENGTH>    sCongestion;
    StringStorage<MAX_PFILTER_LENGTH> sPacketFilterConfig;
    StringStorage<MAX_SID_LENGTH>     sStreamName;

    // Shortcuts and utilities
    int32_t flightCapacity()
    {
        return std::min(iRcvBufSize, iFlightFlagSize);
    }

    CSrtConfig()
        : iMSS(DEF_MSS)
        , zExpPayloadSize(SRT_LIVE_DEF_PLSIZE)
        , bSynSending(true)
        , bSynRecving(true)
        , iFlightFlagSize(DEF_FLIGHT_SIZE)
        , iSndBufSize(DEF_BUFFER_SIZE)
        , iRcvBufSize(DEF_BUFFER_SIZE)
        , bRendezvous(false)
        , tdConnTimeOut(srt::sync::seconds_from(DEF_CONNTIMEO_S))
        , bDriftTracer(true)
        , iSndTimeOut(-1)
        , iRcvTimeOut(-1)
        , llMaxBW(-1)
#ifdef ENABLE_MAXREXMITBW
        , llMaxRexmitBW(-1)
#endif
        , bDataSender(false)
        , bMessageAPI(true)
        , bTSBPD(true)
        , iRcvLatency(SRT_LIVE_DEF_LATENCY_MS)
        , iPeerLatency(0)
        , bTLPktDrop(true)
        , iSndDropDelay(0)
        , bEnforcedEnc(true)
        , iGroupConnect(0)
        , iPeerIdleTimeout_ms(COMM_RESPONSE_TIMEOUT_MS)
        , uMinStabilityTimeout_ms(COMM_DEF_MIN_STABILITY_TIMEOUT_MS)
        , iRetransmitAlgo(1)
        , iCryptoMode(CIPHER_MODE_AUTO)
        , llInputBW(0)
        , llMinInputBW(0)
        , iOverheadBW(25)
        , bRcvNakReport(true)
        , iMaxReorderTolerance(0) // Sensible optimal value is 10, 0 preserves old behavior
        , uKmRefreshRatePkt(0)
        , uKmPreAnnouncePkt(0)
        , uSrtVersion(SRT_DEF_VERSION)
        , uMinimumPeerSrtVersion(SRT_VERSION_MAJ1)

    {
        // Default UDT configurations
        iUDPRcvBufSize = iRcvBufSize * iMSS;

        // Linger: LIVE mode defaults, please refer to `SRTO_TRANSTYPE` option
        // for other modes.
        Linger.l_onoff   = 0;
        Linger.l_linger  = 0;
        CryptoSecret.len = 0;
        iSndCryptoKeyLen = 0;

        // Default congestion is "live".
        // Available builtin congestions: "file".
        // Others can be registerred.
        sCongestion.set("live", 4);
    }

    ~CSrtConfig()
    {
        // Wipeout critical data
        memset(&CryptoSecret, 0, sizeof(CryptoSecret));
    }

    int set(SRT_SOCKOPT optName, const void* val, int size);
};

template <typename T>
inline T cast_optval(const void* optval)
{
    return *reinterpret_cast<const T*>(optval);
}

template <typename T>
inline T cast_optval(const void* optval, int optlen)
{
    if (optlen > 0 && optlen != sizeof(T))
        throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);

    return cast_optval<T>(optval);
}

// This function is to make it possible for both C and C++
// API to accept both bool and int types for boolean options.
// (it's not that C couldn't use <stdbool.h>, it's that people
// often forget to use correct type).
template <>
inline bool cast_optval(const void* optval, int optlen)
{
    if (optlen == sizeof(bool))
    {
        return *reinterpret_cast<const bool*>(optval);
    }

    if (optlen == sizeof(int))
    {
        // 0!= is a windows warning-killer int-to-bool conversion
        return 0 != *reinterpret_cast<const int*>(optval);
    }
    return false;
}

} // namespace srt

struct SRT_SocketOptionObject
{
    struct SingleOption
    {
        uint16_t      option;
        uint16_t      length;
        unsigned char storage[1]; // NOTE: Variable length object!
    };

    std::vector<SingleOption*> options;

    SRT_SocketOptionObject() {}

    ~SRT_SocketOptionObject()
    {
        for (size_t i = 0; i < options.size(); ++i)
        {
            // Convert back
            unsigned char* mem = reinterpret_cast<unsigned char*>(options[i]);
            delete[] mem;
        }
    }

    bool add(SRT_SOCKOPT optname, const void* optval, size_t optlen);
};

#endif
