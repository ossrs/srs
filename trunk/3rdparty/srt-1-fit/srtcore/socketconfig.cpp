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
#include <utility>

#include "srt.h"
#include "socketconfig.h"

using namespace srt;
extern const int32_t SRT_DEF_VERSION = SrtParseVersion(SRT_VERSION);

namespace {
typedef void setter_function(CSrtConfig& co, const void* optval, int optlen);

template<SRT_SOCKOPT name>
struct CSrtConfigSetter
{
    static setter_function set;
};

template<>
struct CSrtConfigSetter<SRTO_MSS>
{
    static void set(CSrtConfig& co, const void* optval, int optlen)
    {
        int ival = cast_optval<int>(optval, optlen);
        if (ival < int(CPacket::UDP_HDR_SIZE + CHandShake::m_iContentSize))
            throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);

        co.iMSS = ival;

        // Packet size cannot be greater than UDP buffer size
        if (co.iMSS > co.iUDPSndBufSize)
            co.iMSS = co.iUDPSndBufSize;
        if (co.iMSS > co.iUDPRcvBufSize)
            co.iMSS = co.iUDPRcvBufSize;
    }
};

template<>
struct CSrtConfigSetter<SRTO_FC>
{
    static void set(CSrtConfig& co, const void* optval, int optlen)
    {
        using namespace srt_logging;
        const int fc = cast_optval<int>(optval, optlen);
        if (fc < co.DEF_MIN_FLIGHT_PKT)
        {
            LOGC(kmlog.Error, log << "SRTO_FC: minimum allowed value is 32 (provided: " << fc << ")");
            throw CUDTException(MJ_NOTSUP, MN_INVAL);
        }

        co.iFlightFlagSize = fc;
    }
};

template<>
struct CSrtConfigSetter<SRTO_SNDBUF>
{
    static void set(CSrtConfig& co, const void* optval, int optlen)
    {
        int bs = cast_optval<int>(optval, optlen);
        if (bs <= 0)
            throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);

        co.iSndBufSize = bs / (co.iMSS - CPacket::UDP_HDR_SIZE);
    }
};

template<>
struct CSrtConfigSetter<SRTO_RCVBUF>
{
    static void set(CSrtConfig& co, const void* optval, int optlen)
    {
        const int val = cast_optval<int>(optval, optlen);
        if (val <= 0)
            throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);

        // Mimimum recv buffer size is 32 packets
        const int mssin_size = co.iMSS - CPacket::UDP_HDR_SIZE;

        if (val > mssin_size * co.DEF_MIN_FLIGHT_PKT)
            co.iRcvBufSize = val / mssin_size;
        else
            co.iRcvBufSize = co.DEF_MIN_FLIGHT_PKT;

        // recv buffer MUST not be greater than FC size
        if (co.iRcvBufSize > co.iFlightFlagSize)
            co.iRcvBufSize = co.iFlightFlagSize;
    }
};

template<>
struct CSrtConfigSetter<SRTO_LINGER>
{
    static void set(CSrtConfig& co, const void* optval, int optlen)
    {
        co.Linger = cast_optval<linger>(optval, optlen);
    }
};

template<>
struct CSrtConfigSetter<SRTO_UDP_SNDBUF>
{
    static void set(CSrtConfig& co, const void* optval, int optlen)
    {
        co.iUDPSndBufSize = std::max(co.iMSS, cast_optval<int>(optval, optlen));
    }
};

template<>
struct CSrtConfigSetter<SRTO_UDP_RCVBUF>
{
    static void set(CSrtConfig& co, const void* optval, int optlen)
    {
        co.iUDPRcvBufSize = std::max(co.iMSS, cast_optval<int>(optval, optlen));
    }
};
template<>
struct CSrtConfigSetter<SRTO_RENDEZVOUS>
{
    static void set(CSrtConfig& co, const void* optval, int optlen)
    {
        co.bRendezvous = cast_optval<bool>(optval, optlen);
    }
};

template<>
struct CSrtConfigSetter<SRTO_SNDTIMEO>
{
    static void set(CSrtConfig& co, const void* optval, int optlen)
    {
        const int val = cast_optval<int>(optval, optlen);
        if (val < -1)
            throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);

        co.iSndTimeOut = val;
    }
};

template<>
struct CSrtConfigSetter<SRTO_RCVTIMEO>
{
    static void set(CSrtConfig& co, const void* optval, int optlen)
    {
        const int val = cast_optval<int>(optval, optlen);
        if (val < -1)
            throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);

        co.iRcvTimeOut = val;
    }
};

template<>
struct CSrtConfigSetter<SRTO_SNDSYN>
{
    static void set(CSrtConfig& co, const void* optval, int optlen)
    {
        co.bSynSending = cast_optval<bool>(optval, optlen);
    }
};
template<>
struct CSrtConfigSetter<SRTO_RCVSYN>
{
    static void set(CSrtConfig& co, const void* optval, int optlen)
    {
        co.bSynRecving = cast_optval<bool>(optval, optlen);
    }
};

template<>
struct CSrtConfigSetter<SRTO_REUSEADDR>
{
    static void set(CSrtConfig& co, const void* optval, int optlen)
    {
        co.bReuseAddr = cast_optval<bool>(optval, optlen);
    }
};

template<>
struct CSrtConfigSetter<SRTO_MAXBW>
{
    static void set(CSrtConfig& co, const void* optval, int optlen)
    {
        const int64_t val = cast_optval<int64_t>(optval, optlen);
        if (val < -1)
            throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);

        co.llMaxBW = val;
    }
};

template<>
struct CSrtConfigSetter<SRTO_IPTTL>
{
    static void set(CSrtConfig& co, const void* optval, int optlen)
    {
        int val = cast_optval<int>(optval, optlen);
        if (!(val == -1) && !((val >= 1) && (val <= 255)))
            throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);
        co.iIpTTL = cast_optval<int>(optval);
    }
};
template<>
struct CSrtConfigSetter<SRTO_IPTOS>
{
    static void set(CSrtConfig& co, const void* optval, int optlen)
    {
        co.iIpToS = cast_optval<int>(optval, optlen);
    }
};

template<>
struct CSrtConfigSetter<SRTO_BINDTODEVICE>
{
    static void set(CSrtConfig& co, const void* optval, int optlen)
    {
        using namespace srt_logging;
#ifdef SRT_ENABLE_BINDTODEVICE
        using namespace std;

        string val;
        if (optlen == -1)
            val = (const char *)optval;
        else
            val.assign((const char *)optval, optlen);
        if (val.size() >= IFNAMSIZ)
        {
            LOGC(kmlog.Error, log << "SRTO_BINDTODEVICE: device name too long (max: IFNAMSIZ=" << IFNAMSIZ << ")");
            throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);
        }

        co.sBindToDevice = val;
#else
        (void)co; // prevent warning
        (void)optval;
        (void)optlen;
        LOGC(kmlog.Error, log << "SRTO_BINDTODEVICE is not supported on that platform");
        throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);
#endif
    }
};

template<>
struct CSrtConfigSetter<SRTO_INPUTBW>
{
    static void set(CSrtConfig& co, const void* optval, int optlen)
    {
        const int64_t val = cast_optval<int64_t>(optval, optlen);
        if (val < 0)
            throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);
        co.llInputBW = val;
    }
};
template<>
struct CSrtConfigSetter<SRTO_MININPUTBW>
{
    static void set(CSrtConfig& co, const void* optval, int optlen)
    {
        const int64_t val = cast_optval<int64_t>(optval, optlen);
        if (val < 0)
            throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);
        co.llMinInputBW = val;
    }
};
template<>
struct CSrtConfigSetter<SRTO_OHEADBW>
{
    static void set(CSrtConfig& co, const void* optval, int optlen)
    {
        const int32_t val = cast_optval<int32_t>(optval, optlen);
        if (val < 5 || val > 100)
            throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);
        co.iOverheadBW = val;
    }
};
template<>
struct CSrtConfigSetter<SRTO_SENDER>
{
    static void set(CSrtConfig& co, const void* optval, int optlen)
    {
        co.bDataSender = cast_optval<bool>(optval, optlen);
    }
};
template<>
struct CSrtConfigSetter<SRTO_TSBPDMODE>
{
    static void set(CSrtConfig& co, const void* optval, int optlen)
    {
        co.bTSBPD = cast_optval<bool>(optval, optlen);
    }
};
template<>
struct CSrtConfigSetter<SRTO_LATENCY>
{
    static void set(CSrtConfig& co, const void* optval, int optlen)
    {
        const int val = cast_optval<int>(optval, optlen);
        if (val < 0)
            throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);

        co.iRcvLatency  = val;
        co.iPeerLatency = val;
    }
};
template<>
struct CSrtConfigSetter<SRTO_RCVLATENCY>
{
    static void set(CSrtConfig& co, const void* optval, int optlen)
    {
        const int val = cast_optval<int>(optval, optlen);
        if (val < 0)
            throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);

        co.iRcvLatency = val;
    }
};
template<>
struct CSrtConfigSetter<SRTO_PEERLATENCY>
{
    static void set(CSrtConfig& co, const void* optval, int optlen)
    {
        const int val = cast_optval<int>(optval, optlen);
        if (val < 0)
            throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);

        co.iPeerLatency = val;
    }
};
template<>
struct CSrtConfigSetter<SRTO_TLPKTDROP>
{
    static void set(CSrtConfig& co, const void* optval, int optlen)
    {
        co.bTLPktDrop = cast_optval<bool>(optval, optlen);
    }
};
template<>
struct CSrtConfigSetter<SRTO_SNDDROPDELAY>
{
    static void set(CSrtConfig& co, const void* optval, int optlen)
    {
        const int val = cast_optval<int>(optval, optlen);
        if (val < -1)
            throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);

        co.iSndDropDelay = val;
    }
};
template<>
struct CSrtConfigSetter<SRTO_PASSPHRASE>
{
    static void set(CSrtConfig& co, const void* optval, int optlen)
    {
        using namespace srt_logging;
#ifdef SRT_ENABLE_ENCRYPTION
        // Password must be 10-80 characters.
        // Or it can be empty to clear the password.
        if ((optlen != 0) && (optlen < 10 || optlen > HAICRYPT_SECRET_MAX_SZ))
            throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);

        memset(&co.CryptoSecret, 0, sizeof(co.CryptoSecret));
        co.CryptoSecret.typ = HAICRYPT_SECTYP_PASSPHRASE;
        co.CryptoSecret.len = (optlen <= (int)sizeof(co.CryptoSecret.str) ? optlen : (int)sizeof(co.CryptoSecret.str));
        memcpy((co.CryptoSecret.str), optval, co.CryptoSecret.len);
#else
        (void)co; // prevent warning
        (void)optval;
        if (optlen == 0)
            return; // Allow to set empty passphrase if no encryption supported.

        LOGC(aclog.Error, log << "SRTO_PASSPHRASE: encryption not enabled at compile time");
        throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);
#endif
    }
};
template<>
struct CSrtConfigSetter<SRTO_PBKEYLEN>
{
    static void set(CSrtConfig& co, const void* optval, int optlen)
    {
        using namespace srt_logging;
#ifdef SRT_ENABLE_ENCRYPTION
        const int v    = cast_optval<int>(optval, optlen);
        int const allowed[4] = {
            0,  // Default value, if this results for initiator, defaults to 16. See below.
            16, // AES-128
            24, // AES-192
            32  // AES-256
        };
        const int *const allowed_end = allowed + 4;
        if (std::find(allowed, allowed_end, v) == allowed_end)
        {
            LOGC(aclog.Error,
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
        //    being an initiator will set iSndCryptoKeyLen before setting
        //    up KMREQ for sending to the sender-responder.
        //
        // Note that in HSv5 if both sides set PBKEYLEN, the responder
        // wins, unless the initiator is a sender (the effective PBKEYLEN
        // will be the one advertised by the responder). If none sets,
        // PBKEYLEN will default to 16.

        co.iSndCryptoKeyLen = v;
#else
        (void)co; // prevent warning
        (void)optval;
        (void)optlen;
        LOGC(aclog.Error, log << "SRTO_PBKEYLEN: encryption not enabled at compile time");
        throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);
#endif
    }
};

template<>
struct CSrtConfigSetter<SRTO_NAKREPORT>
{
    static void set(CSrtConfig& co, const void* optval, int optlen)
    {
        co.bRcvNakReport = cast_optval<bool>(optval, optlen);
    }
};

template<>
struct CSrtConfigSetter<SRTO_CONNTIMEO>
{
    static void set(CSrtConfig& co, const void* optval, int optlen)
    {
        const int val = cast_optval<int>(optval, optlen);
        if (val < 0)
            throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);

        using namespace sync;
        co.tdConnTimeOut = milliseconds_from(val);
    }
};

template<>
struct CSrtConfigSetter<SRTO_DRIFTTRACER>
{
    static void set(CSrtConfig& co, const void* optval, int optlen)
    {
        co.bDriftTracer = cast_optval<bool>(optval, optlen);
    }
};

template<>
struct CSrtConfigSetter<SRTO_LOSSMAXTTL>
{
    static void set(CSrtConfig& co, const void* optval, int optlen)
    {
        co.iMaxReorderTolerance = cast_optval<int>(optval, optlen);
    }
};

template<>
struct CSrtConfigSetter<SRTO_VERSION>
{
    static void set(CSrtConfig& co, const void* optval, int optlen)
    {
        co.uSrtVersion = cast_optval<uint32_t>(optval, optlen);
    }
};

template<>
struct CSrtConfigSetter<SRTO_MINVERSION>
{
    static void set(CSrtConfig& co, const void* optval, int optlen)
    {
        co.uMinimumPeerSrtVersion = cast_optval<uint32_t>(optval, optlen);
    }
};

template<>
struct CSrtConfigSetter<SRTO_STREAMID>
{
    static void set(CSrtConfig& co, const void* optval, int optlen)
    {
        if (size_t(optlen) > CSrtConfig::MAX_SID_LENGTH)
            throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);

        co.sStreamName.set((const char*)optval, optlen);
    }
};

template<>
struct CSrtConfigSetter<SRTO_CONGESTION>
{
    static void set(CSrtConfig& co, const void* optval, int optlen)
    {
        std::string val;
        if (optlen == -1)
            val = (const char*)optval;
        else
            val.assign((const char*)optval, optlen);

        // Translate alias
        if (val == "vod")
            val = "file";

        bool res = SrtCongestion::exists(val);
        if (!res)
            throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);

        co.sCongestion.set(val);
    }
};

template<>
struct CSrtConfigSetter<SRTO_MESSAGEAPI>
{
    static void set(CSrtConfig& co, const void* optval, int optlen)
    {
        co.bMessageAPI = cast_optval<bool>(optval, optlen);
    }
};

template<>
struct CSrtConfigSetter<SRTO_PAYLOADSIZE>
{
    static void set(CSrtConfig& co, const void* optval, int optlen)
    {
        using namespace srt_logging;
        const int val = cast_optval<int>(optval, optlen);
        if (val < 0)
        {
            throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);
        }

        if (val > SRT_LIVE_MAX_PLSIZE)
        {
            LOGC(aclog.Error, log << "SRTO_PAYLOADSIZE: value exceeds SRT_LIVE_MAX_PLSIZE, maximum payload per MTU.");
            throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);
        }

        if (!co.sPacketFilterConfig.empty())
        {
            // This means that the filter might have been installed before,
            // and the fix to the maximum payload size was already applied.
            // This needs to be checked now.
            SrtFilterConfig fc;
            if (!ParseFilterConfig(co.sPacketFilterConfig.str(), fc))
            {
                // Break silently. This should not happen
                LOGC(aclog.Error, log << "SRTO_PAYLOADSIZE: IPE: failing filter configuration installed");
                throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);
            }

            const size_t efc_max_payload_size = SRT_LIVE_MAX_PLSIZE - fc.extra_size;
            if (size_t(val) > efc_max_payload_size)
            {
                LOGC(aclog.Error,
                     log << "SRTO_PAYLOADSIZE: value exceeds SRT_LIVE_MAX_PLSIZE decreased by " << fc.extra_size
                         << " required for packet filter header");
                throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);
            }
        }

        co.zExpPayloadSize = val;
    }
};

template<>
struct CSrtConfigSetter<SRTO_TRANSTYPE>
{
    static void set(CSrtConfig& co, const void* optval, int optlen)
    {
        // XXX Note that here the configuration for SRTT_LIVE
        // is the same as DEFAULT VALUES for these fields set
        // in CUDT::CUDT.
        switch (cast_optval<SRT_TRANSTYPE>(optval, optlen))
        {
        case SRTT_LIVE:
            // Default live options:
            // - tsbpd: on
            // - latency: 120ms
            // - linger: off
            // - congctl: live
            // - extraction method: message (reading call extracts one message)
            co.bTSBPD          = true;
            co.iRcvLatency     = SRT_LIVE_DEF_LATENCY_MS;
            co.iPeerLatency    = 0;
            co.bTLPktDrop      = true;
            co.iSndDropDelay   = 0;
            co.bMessageAPI     = true;
            co.bRcvNakReport   = true;
            co.iRetransmitAlgo = 1;
            co.zExpPayloadSize = SRT_LIVE_DEF_PLSIZE;
            co.Linger.l_onoff  = 0;
            co.Linger.l_linger = 0;
            co.sCongestion.set("live", 4);
            break;

        case SRTT_FILE:
            // File transfer mode:
            // - tsbpd: off
            // - latency: 0
            // - linger: on
            // - congctl: file (original UDT congestion control)
            // - extraction method: stream (reading call extracts as many bytes as available and fits in buffer)
            co.bTSBPD          = false;
            co.iRcvLatency     = 0;
            co.iPeerLatency    = 0;
            co.bTLPktDrop      = false;
            co.iSndDropDelay   = -1;
            co.bMessageAPI     = false;
            co.bRcvNakReport   = false;
            co.iRetransmitAlgo = 0;
            co.zExpPayloadSize = 0; // use maximum
            co.Linger.l_onoff  = 1;
            co.Linger.l_linger = CSrtConfig::DEF_LINGER_S;
            co.sCongestion.set("file", 4);
            break;

        default:
            throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);
        }
    }
};

#if ENABLE_BONDING
template<>
struct CSrtConfigSetter<SRTO_GROUPCONNECT>
{
    static void set(CSrtConfig& co, const void* optval, int optlen)
    {
        co.iGroupConnect = cast_optval<int>(optval, optlen);
    }
};
#endif

template<>
struct CSrtConfigSetter<SRTO_KMREFRESHRATE>
{
    static void set(CSrtConfig& co, const void* optval, int optlen)
    {
        using namespace srt_logging;

        const int val = cast_optval<int>(optval, optlen);
        if (val < 0)
        {
            LOGC(aclog.Error,
                 log << "SRTO_KMREFRESHRATE=" << val << " can't be negative");
            throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);
        }

        // Changing the KMREFRESHRATE sets KMPREANNOUNCE to the maximum allowed value
        co.uKmRefreshRatePkt = (unsigned) val;

        if (co.uKmPreAnnouncePkt == 0 && co.uKmRefreshRatePkt == 0)
            return; // Both values are default

        const unsigned km_preanno = co.uKmPreAnnouncePkt == 0 ? HAICRYPT_DEF_KM_PRE_ANNOUNCE : co.uKmPreAnnouncePkt;
        const unsigned km_refresh = co.uKmRefreshRatePkt == 0 ? HAICRYPT_DEF_KM_REFRESH_RATE : co.uKmRefreshRatePkt;

        if (co.uKmPreAnnouncePkt == 0 || km_preanno > (km_refresh - 1) / 2)
        {
            co.uKmPreAnnouncePkt = (km_refresh - 1) / 2;
            LOGC(aclog.Warn,
                 log << "SRTO_KMREFRESHRATE=0x" << std::hex << km_refresh << ": setting SRTO_KMPREANNOUNCE=0x"
                     << std::hex << co.uKmPreAnnouncePkt);
        }
    }
};

template<>
struct CSrtConfigSetter<SRTO_KMPREANNOUNCE>
{
    static void set(CSrtConfig& co, const void* optval, int optlen)
    {
        using namespace srt_logging;

        const int val = cast_optval<int>(optval, optlen);
        if (val < 0)
        {
            LOGC(aclog.Error,
                 log << "SRTO_KMPREANNOUNCE=" << val << " can't be negative");
            throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);
        }

        const unsigned km_preanno = val == 0 ? HAICRYPT_DEF_KM_PRE_ANNOUNCE : val;
        const unsigned kmref = co.uKmRefreshRatePkt == 0 ? HAICRYPT_DEF_KM_REFRESH_RATE : co.uKmRefreshRatePkt;
        if (km_preanno > (kmref - 1) / 2)
        {
            LOGC(aclog.Error,
                 log << "SRTO_KMPREANNOUNCE=0x" << std::hex << km_preanno << " exceeds KmRefresh/2, 0x" << ((kmref - 1) / 2)
                     << " - OPTION REJECTED.");
            throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);
        }

        co.uKmPreAnnouncePkt = val;
    }
};

template<>
struct CSrtConfigSetter<SRTO_ENFORCEDENCRYPTION>
{
    static void set(CSrtConfig& co, const void* optval, int optlen)
    {
        co.bEnforcedEnc = cast_optval<bool>(optval, optlen);
    }
};

template<>
struct CSrtConfigSetter<SRTO_PEERIDLETIMEO>
{
    static void set(CSrtConfig& co, const void* optval, int optlen)
    {
        const int val = cast_optval<int>(optval, optlen);
        if (val < 0)
            throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);

        co.iPeerIdleTimeout_ms = val;
    }
};

template<>
struct CSrtConfigSetter<SRTO_IPV6ONLY>
{
    static void set(CSrtConfig& co, const void* optval, int optlen)
    {
        co.iIpV6Only = cast_optval<int>(optval, optlen);
    }
};

template<>
struct CSrtConfigSetter<SRTO_PACKETFILTER>
{
    static void set(CSrtConfig& co, const void* optval, int optlen)
    {
        using namespace srt_logging;
        std::string arg((const char*)optval, optlen);
        // Parse the configuration string prematurely
        SrtFilterConfig fc;
        PacketFilter::Factory* fax = 0;
        if (!ParseFilterConfig(arg, (fc), (&fax)))
        {
            LOGC(aclog.Error,
                 log << "SRTO_PACKETFILTER: Incorrect syntax. Use: FILTERTYPE[,KEY:VALUE...]. "
                        "FILTERTYPE ("
                     << fc.type << ") must be installed (or builtin)");
            throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);
        }
        std::string error;
        if (!fax->verifyConfig(fc, (error)))
        {
            LOGC(aclog.Error, log << "SRTO_PACKETFILTER: Incorrect config: " << error);
            throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);
        }

        size_t efc_max_payload_size = SRT_LIVE_MAX_PLSIZE - fc.extra_size;
        if (co.zExpPayloadSize > efc_max_payload_size)
        {
            LOGC(aclog.Warn,
                 log << "Due to filter-required extra " << fc.extra_size << " bytes, SRTO_PAYLOADSIZE fixed to "
                     << efc_max_payload_size << " bytes");
            co.zExpPayloadSize = efc_max_payload_size;
        }

        co.sPacketFilterConfig.set(arg);
    }
};

#if ENABLE_BONDING
template<>
struct CSrtConfigSetter<SRTO_GROUPMINSTABLETIMEO>
{
    static void set(CSrtConfig& co, const void* optval, int optlen)
    {
        using namespace srt_logging;
        // This option is meaningless for the socket itself.
        // It's set here just for the sake of setting it on a listener
        // socket so that it is then applied on the group when a
        // group connection is configured.
        const int val_ms = cast_optval<int>(optval, optlen);
        const int min_timeo_ms = (int) CSrtConfig::COMM_DEF_MIN_STABILITY_TIMEOUT_MS;

        if (val_ms < min_timeo_ms)
        {
            LOGC(qmlog.Error,
                log << "group option: SRTO_GROUPMINSTABLETIMEO min allowed value is "
                    << min_timeo_ms << " ms.");
            throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);
        }

        const int idletmo_ms = co.iPeerIdleTimeout_ms;

        if (val_ms > idletmo_ms)
        {
            LOGC(aclog.Error, log << "group option: SRTO_GROUPMINSTABLETIMEO(" << val_ms
                                  << ") exceeds SRTO_PEERIDLETIMEO(" << idletmo_ms << ")");
            throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);
        }

        co.uMinStabilityTimeout_ms = val_ms;
        LOGC(smlog.Error, log << "SRTO_GROUPMINSTABLETIMEO set " << val_ms);
    }
};
#endif

template<>
struct CSrtConfigSetter<SRTO_RETRANSMITALGO>
{
    static void set(CSrtConfig& co, const void* optval, int optlen)
    {
        const int val = cast_optval<int>(optval, optlen);
        if (val < 0 || val > 1)
            throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);

        co.iRetransmitAlgo = val;
    }
};

int dispatchSet(SRT_SOCKOPT optName, CSrtConfig& co, const void* optval, int optlen)
{
    switch (optName)
    {
#define DISPATCH(optname) case optname: CSrtConfigSetter<optname>::set(co, optval, optlen); return 0;

        DISPATCH(SRTO_MSS);
        DISPATCH(SRTO_FC);
        DISPATCH(SRTO_SNDBUF);
        DISPATCH(SRTO_RCVBUF);
        DISPATCH(SRTO_LINGER);
        DISPATCH(SRTO_UDP_SNDBUF);
        DISPATCH(SRTO_UDP_RCVBUF);
        DISPATCH(SRTO_RENDEZVOUS);
        DISPATCH(SRTO_SNDTIMEO);
        DISPATCH(SRTO_RCVTIMEO);
        DISPATCH(SRTO_SNDSYN);
        DISPATCH(SRTO_RCVSYN);
        DISPATCH(SRTO_REUSEADDR);
        DISPATCH(SRTO_MAXBW);
        DISPATCH(SRTO_IPTTL);
        DISPATCH(SRTO_IPTOS);
        DISPATCH(SRTO_BINDTODEVICE);
        DISPATCH(SRTO_INPUTBW);
        DISPATCH(SRTO_MININPUTBW);
        DISPATCH(SRTO_OHEADBW);
        DISPATCH(SRTO_SENDER);
        DISPATCH(SRTO_TSBPDMODE);
        DISPATCH(SRTO_LATENCY);
        DISPATCH(SRTO_RCVLATENCY);
        DISPATCH(SRTO_PEERLATENCY);
        DISPATCH(SRTO_TLPKTDROP);
        DISPATCH(SRTO_SNDDROPDELAY);
        DISPATCH(SRTO_PASSPHRASE);
        DISPATCH(SRTO_PBKEYLEN);
        DISPATCH(SRTO_NAKREPORT);
        DISPATCH(SRTO_CONNTIMEO);
        DISPATCH(SRTO_DRIFTTRACER);
        DISPATCH(SRTO_LOSSMAXTTL);
        DISPATCH(SRTO_VERSION);
        DISPATCH(SRTO_MINVERSION);
        DISPATCH(SRTO_STREAMID);
        DISPATCH(SRTO_CONGESTION);
        DISPATCH(SRTO_MESSAGEAPI);
        DISPATCH(SRTO_PAYLOADSIZE);
        DISPATCH(SRTO_TRANSTYPE);
#if ENABLE_BONDING
        DISPATCH(SRTO_GROUPCONNECT);
        DISPATCH(SRTO_GROUPMINSTABLETIMEO);
#endif
        DISPATCH(SRTO_KMREFRESHRATE);
        DISPATCH(SRTO_KMPREANNOUNCE);
        DISPATCH(SRTO_ENFORCEDENCRYPTION);
        DISPATCH(SRTO_PEERIDLETIMEO);
        DISPATCH(SRTO_IPV6ONLY);
        DISPATCH(SRTO_PACKETFILTER);
        DISPATCH(SRTO_RETRANSMITALGO);

#undef DISPATCH
    default:
        return -1;
    }
}

} // anonymous namespace

int CSrtConfig::set(SRT_SOCKOPT optName, const void* optval, int optlen)
{
    return dispatchSet(optName, *this, optval, optlen);
}

#if ENABLE_BONDING
bool SRT_SocketOptionObject::add(SRT_SOCKOPT optname, const void* optval, size_t optlen)
{
    // Check first if this option is allowed to be set
    // as on a member socket.

    switch (optname)
    {
    case SRTO_BINDTODEVICE:
    case SRTO_CONNTIMEO:
    case SRTO_DRIFTTRACER:
        //SRTO_FC - not allowed to be different among group members
    case SRTO_GROUPMINSTABLETIMEO:
        //SRTO_INPUTBW - per transmission setting
    case SRTO_IPTOS:
    case SRTO_IPTTL:
    case SRTO_KMREFRESHRATE:
    case SRTO_KMPREANNOUNCE:
        //SRTO_LATENCY - per transmission setting
        //SRTO_LINGER - not for managed sockets
    case SRTO_LOSSMAXTTL:
        //SRTO_MAXBW - per transmission setting
        //SRTO_MESSAGEAPI - groups are live mode only
        //SRTO_MINVERSION - per group connection setting
    case SRTO_NAKREPORT:
        //SRTO_OHEADBW - per transmission setting
        //SRTO_PACKETFILTER - per transmission setting
        //SRTO_PASSPHRASE - per group connection setting
        //SRTO_PASSPHRASE - per transmission setting
        //SRTO_PBKEYLEN - per group connection setting
    case SRTO_PEERIDLETIMEO:
    case SRTO_RCVBUF:
        //SRTO_RCVSYN - must be always false in groups
        //SRTO_RCVTIMEO - must be alwyas -1 in groups
    case SRTO_SNDBUF:
    case SRTO_SNDDROPDELAY:
        //SRTO_TLPKTDROP - per transmission setting
        //SRTO_TSBPDMODE - per transmission setting
    case SRTO_UDP_RCVBUF:
    case SRTO_UDP_SNDBUF:
        break;

    default:
        // Other options are not allowed
        return false;
    }

    // Header size will get the size likely aligned, but it won't
    // hurt if the memory size will be up to 4 bytes more than
    // needed - and it's better to not risk that alighment rules
    // will make these calculations result in less space than needed.
    const size_t headersize = sizeof(SingleOption);
    const size_t payload = std::min(sizeof(uint32_t), optlen);
    unsigned char* mem = new unsigned char[headersize + payload];
    SingleOption* option = reinterpret_cast<SingleOption*>(mem);
    option->option = optname;
    option->length = (uint16_t) optlen;
    memcpy(option->storage, optval, optlen);

    options.push_back(option);

    return true;
}
#endif
