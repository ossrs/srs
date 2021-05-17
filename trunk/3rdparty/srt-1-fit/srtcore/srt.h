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
written by
   Haivision Systems Inc.
 *****************************************************************************/

#ifndef INC__SRTC_H
#define INC__SRTC_H

#include "version.h"

#include "platform_sys.h"

#include <string.h>
#include <stdlib.h>

#include "srt4udt.h"
#include "logging_api.h"

////////////////////////////////////////////////////////////////////////////////

//if compiling on VC6.0 or pre-WindowsXP systems
//use -DLEGACY_WIN32

//if compiling with MinGW, it only works on XP or above
//use -D_WIN32_WINNT=0x0501


#ifdef _WIN32
   #ifndef __MINGW__
      // Explicitly define 32-bit and 64-bit numbers
      typedef __int32 int32_t;
      typedef __int64 int64_t;
      typedef unsigned __int32 uint32_t;
      #ifndef LEGACY_WIN32
         typedef unsigned __int64 uint64_t;
      #else
         // VC 6.0 does not support unsigned __int64: may cause potential problems.
         typedef __int64 uint64_t;
      #endif

      #ifdef SRT_DYNAMIC
         #ifdef SRT_EXPORTS
            #define SRT_API __declspec(dllexport)
         #else
            #define SRT_API __declspec(dllimport)
         #endif
      #else
         #define SRT_API
      #endif
   #else // __MINGW__
      #define SRT_API
   #endif
#else
   #define SRT_API __attribute__ ((visibility("default")))
#endif


// For feature tests if you need.
// You can use these constants with SRTO_MINVERSION option.
#define SRT_VERSION_FEAT_HSv5 0x010300

// When compiling in C++17 mode, use the standard C++17 attributes
// (out of these, only [[deprecated]] is supported in C++14, so
// for all lesser standard use compiler-specific attributes).
#if defined(SRT_NO_DEPRECATED)

#define SRT_ATR_UNUSED
#define SRT_ATR_DEPRECATED
#define SRT_ATR_NODISCARD

#elif defined(__cplusplus) && __cplusplus > 201406

#define SRT_ATR_UNUSED [[maybe_unused]]
#define SRT_ATR_DEPRECATED [[deprecated]]
#define SRT_ATR_NODISCARD [[nodiscard]]

// GNUG is GNU C/C++; this syntax is also supported by Clang
#elif defined( __GNUC__)
#define SRT_ATR_UNUSED __attribute__((unused))
#define SRT_ATR_DEPRECATED __attribute__((deprecated))
#define SRT_ATR_NODISCARD __attribute__((warn_unused_result))
#else
#define SRT_ATR_UNUSED
#define SRT_ATR_DEPRECATED
#define SRT_ATR_NODISCARD
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef int SRTSOCKET; // SRTSOCKET is a typedef to int anyway, and it's not even in UDT namespace :)

#ifdef _WIN32
   #ifndef __MINGW__
      typedef SOCKET SYSSOCKET;
   #else
      typedef int SYSSOCKET;
   #endif
#else
   typedef int SYSSOCKET;
#endif

typedef SYSSOCKET UDPSOCKET;


// Values returned by srt_getsockstate()
typedef enum SRT_SOCKSTATUS {
   SRTS_INIT = 1,
   SRTS_OPENED,
   SRTS_LISTENING,
   SRTS_CONNECTING,
   SRTS_CONNECTED,
   SRTS_BROKEN,
   SRTS_CLOSING,
   SRTS_CLOSED,
   SRTS_NONEXIST
} SRT_SOCKSTATUS;

// This is a duplicate enum. Must be kept in sync with the original UDT enum for
// backward compatibility until all compat is destroyed.
typedef enum SRT_SOCKOPT {

   SRTO_MSS = 0,             // the Maximum Transfer Unit
   SRTO_SNDSYN = 1,          // if sending is blocking
   SRTO_RCVSYN = 2,          // if receiving is blocking
   SRTO_ISN = 3,             // Initial Sequence Number (valid only after srt_connect or srt_accept-ed sockets)
   SRTO_FC = 4,              // Flight flag size (window size)
   SRTO_SNDBUF = 5,          // maximum buffer in sending queue
   SRTO_RCVBUF = 6,          // UDT receiving buffer size
   SRTO_LINGER = 7,          // waiting for unsent data when closing
   SRTO_UDP_SNDBUF = 8,      // UDP sending buffer size
   SRTO_UDP_RCVBUF = 9,      // UDP receiving buffer size
   // XXX Free space for 2 options
   // after deprecated ones are removed
   SRTO_RENDEZVOUS = 12,     // rendezvous connection mode
   SRTO_SNDTIMEO = 13,       // send() timeout
   SRTO_RCVTIMEO = 14,       // recv() timeout
   SRTO_REUSEADDR = 15,      // reuse an existing port or create a new one
   SRTO_MAXBW = 16,          // maximum bandwidth (bytes per second) that the connection can use
   SRTO_STATE = 17,          // current socket state, see UDTSTATUS, read only
   SRTO_EVENT = 18,          // current available events associated with the socket
   SRTO_SNDDATA = 19,        // size of data in the sending buffer
   SRTO_RCVDATA = 20,        // size of data available for recv
   SRTO_SENDER = 21,         // Sender mode (independent of conn mode), for encryption, tsbpd handshake.
   SRTO_TSBPDMODE = 22,      // Enable/Disable TsbPd. Enable -> Tx set origin timestamp, Rx deliver packet at origin time + delay
   SRTO_LATENCY = 23,        // NOT RECOMMENDED. SET: to both SRTO_RCVLATENCY and SRTO_PEERLATENCY. GET: same as SRTO_RCVLATENCY.
   SRTO_TSBPDDELAY = 23,     // DEPRECATED. ALIAS: SRTO_LATENCY
   SRTO_INPUTBW = 24,        // Estimated input stream rate.
   SRTO_OHEADBW,             // MaxBW ceiling based on % over input stream rate. Applies when UDT_MAXBW=0 (auto).
   SRTO_PASSPHRASE = 26,     // Crypto PBKDF2 Passphrase size[0,10..64] 0:disable crypto
   SRTO_PBKEYLEN,            // Crypto key len in bytes {16,24,32} Default: 16 (128-bit)
   SRTO_KMSTATE,             // Key Material exchange status (UDT_SRTKmState)
   SRTO_IPTTL = 29,          // IP Time To Live (passthru for system sockopt IPPROTO_IP/IP_TTL)
   SRTO_IPTOS,               // IP Type of Service (passthru for system sockopt IPPROTO_IP/IP_TOS)
   SRTO_TLPKTDROP = 31,      // Enable receiver pkt drop
   SRTO_SNDDROPDELAY = 32,   // Extra delay towards latency for sender TLPKTDROP decision (-1 to off)
   SRTO_NAKREPORT = 33,      // Enable receiver to send periodic NAK reports
   SRTO_VERSION = 34,        // Local SRT Version
   SRTO_PEERVERSION,         // Peer SRT Version (from SRT Handshake)
   SRTO_CONNTIMEO = 36,      // Connect timeout in msec. Ccaller default: 3000, rendezvous (x 10)
   // deprecated: SRTO_TWOWAYDATA, SRTO_SNDPBKEYLEN, SRTO_RCVPBKEYLEN (@c below)
   _DEPRECATED_SRTO_SNDPBKEYLEN = 38, // (needed to use inside the code without generating -Wswitch)
   //
   SRTO_SNDKMSTATE = 40,     // (GET) the current state of the encryption at the peer side
   SRTO_RCVKMSTATE,          // (GET) the current state of the encryption at the agent side
   SRTO_LOSSMAXTTL,          // Maximum possible packet reorder tolerance (number of packets to receive after loss to send lossreport)
   SRTO_RCVLATENCY,          // TsbPd receiver delay (mSec) to absorb burst of missed packet retransmission
   SRTO_PEERLATENCY,         // Minimum value of the TsbPd receiver delay (mSec) for the opposite side (peer)
   SRTO_MINVERSION,          // Minimum SRT version needed for the peer (peers with less version will get connection reject)
   SRTO_STREAMID,            // A string set to a socket and passed to the listener's accepted socket
   SRTO_CONGESTION,          // Congestion controller type selection
   SRTO_MESSAGEAPI,          // In File mode, use message API (portions of data with boundaries)
   SRTO_PAYLOADSIZE,         // Maximum payload size sent in one UDP packet (0 if unlimited)
   SRTO_TRANSTYPE = 50,      // Transmission type (set of options required for given transmission type)
   SRTO_KMREFRESHRATE,       // After sending how many packets the encryption key should be flipped to the new key
   SRTO_KMPREANNOUNCE,       // How many packets before key flip the new key is annnounced and after key flip the old one decommissioned
   SRTO_ENFORCEDENCRYPTION,  // Connection to be rejected or quickly broken when one side encryption set or bad password
   SRTO_IPV6ONLY,            // IPV6_V6ONLY mode
   SRTO_PEERIDLETIMEO,       // Peer-idle timeout (max time of silence heard from peer) in [ms]
   // (some space left)
   SRTO_PACKETFILTER = 60          // Add and configure a packet filter
} SRT_SOCKOPT;


#ifdef __cplusplus

typedef SRT_ATR_DEPRECATED SRT_SOCKOPT SRT_SOCKOPT_DEPRECATED;
#define SRT_DEPRECATED_OPTION(value) ((SRT_SOCKOPT_DEPRECATED)value)

#else

// deprecated enum labels are supported only since gcc 6, so in C there
// will be a whole deprecated enum type, as it's not an error in C to mix
// enum types
enum SRT_ATR_DEPRECATED SRT_SOCKOPT_DEPRECATED
{

    // Dummy last option, as every entry ends with a comma
    SRTO_DEPRECATED_END = 0

};
#define SRT_DEPRECATED_OPTION(value) ((enum SRT_SOCKOPT_DEPRECATED)value)
#endif

// DEPRECATED OPTIONS:

// SRTO_TWOWAYDATA: not to be used. SRT connection is always bidirectional if
// both clients support HSv5 - that is, since version 1.3.0. This flag was
// introducted around 1.2.0 version when full bidirectional support was added,
// but the bidirectional feature was decided no to be enabled due to huge
// differences between bidirectional support (especially concerning encryption)
// with HSv4 and HSv5 (that is, HSv4 was decided to remain unidirectional only,
// even though partial support is already provided in this version).

#define SRTO_TWOWAYDATA SRT_DEPRECATED_OPTION(37)

// This has been deprecated a long time ago, treat this as never implemented.
// The value is also already reused for another option.
#define SRTO_TSBPDMAXLAG SRT_DEPRECATED_OPTION(32)

// This option is a derivative from UDT; the mechanism that uses it is now
// settable by SRTO_CONGESTION, or more generally by SRTO_TRANSTYPE. The freed
// number has been reused for a read-only option SRTO_ISN. This option should
// have never been used anywhere, just for safety this is temporarily declared
// as deprecated.
#define SRTO_CC SRT_DEPRECATED_OPTION(3)

// These two flags were derived from UDT, but they were never used.
// Probably it didn't make sense anyway. The maximum size of the message
// in File/Message mode is defined by SRTO_SNDBUF, and the MSGTTL is
// a parameter used in `srt_sendmsg` and `srt_sendmsg2`.
#define SRTO_MAXMSG SRT_DEPRECATED_OPTION(10)
#define SRTO_MSGTTL SRT_DEPRECATED_OPTION(11)

// These flags come from an older experimental implementation of bidirectional
// encryption support, which were used two different SEKs, KEKs and passphrases
// per direction. The current implementation uses just one in both directions,
// so SRTO_PBKEYLEN should be used for both cases.
#define SRTO_SNDPBKEYLEN SRT_DEPRECATED_OPTION(38)
#define SRTO_RCVPBKEYLEN SRT_DEPRECATED_OPTION(39)

// Keeping old name for compatibility (deprecated)
#define SRTO_SMOOTHER SRT_DEPRECATED_OPTION(47)
#define SRTO_STRICTENC SRT_DEPRECATED_OPTION(53)

typedef enum SRT_TRANSTYPE
{
    SRTT_LIVE,
    SRTT_FILE,
    SRTT_INVALID
} SRT_TRANSTYPE;

// These sizes should be used for Live mode. In Live mode you should not
// exceed the size that fits in a single MTU.

// This is for MPEG TS and it's a default SRTO_PAYLOADSIZE for SRTT_LIVE.
static const int SRT_LIVE_DEF_PLSIZE = 1316; // = 188*7, recommended for MPEG TS

// This is the maximum payload size for Live mode, should you have a different
// payload type than MPEG TS.
static const int SRT_LIVE_MAX_PLSIZE = 1456; // MTU(1500) - UDP.hdr(28) - SRT.hdr(16)

// Latency for Live transmission: default is 120
static const int SRT_LIVE_DEF_LATENCY_MS = 120;

// Importrant note: please add new fields to this structure to the end and don't remove any existing fields 
struct CBytePerfMon
{
   // global measurements
   int64_t  msTimeStamp;                // time since the UDT entity is started, in milliseconds
   int64_t  pktSentTotal;               // total number of sent data packets, including retransmissions
   int64_t  pktRecvTotal;               // total number of received packets
   int      pktSndLossTotal;            // total number of lost packets (sender side)
   int      pktRcvLossTotal;            // total number of lost packets (receiver side)
   int      pktRetransTotal;            // total number of retransmitted packets
   int      pktSentACKTotal;            // total number of sent ACK packets
   int      pktRecvACKTotal;            // total number of received ACK packets
   int      pktSentNAKTotal;            // total number of sent NAK packets
   int      pktRecvNAKTotal;            // total number of received NAK packets
   int64_t  usSndDurationTotal;         // total time duration when UDT is sending data (idle time exclusive)
   //>new
   int      pktSndDropTotal;            // number of too-late-to-send dropped packets
   int      pktRcvDropTotal;            // number of too-late-to play missing packets
   int      pktRcvUndecryptTotal;       // number of undecrypted packets
   uint64_t byteSentTotal;              // total number of sent data bytes, including retransmissions
   uint64_t byteRecvTotal;              // total number of received bytes
#ifdef SRT_ENABLE_LOSTBYTESCOUNT
   uint64_t byteRcvLossTotal;           // total number of lost bytes
#endif
   uint64_t byteRetransTotal;           // total number of retransmitted bytes
   uint64_t byteSndDropTotal;           // number of too-late-to-send dropped bytes
   uint64_t byteRcvDropTotal;           // number of too-late-to play missing bytes (estimate based on average packet size)
   uint64_t byteRcvUndecryptTotal;      // number of undecrypted bytes
   //<

   // local measurements
   int64_t  pktSent;                    // number of sent data packets, including retransmissions
   int64_t  pktRecv;                    // number of received packets
   int      pktSndLoss;                 // number of lost packets (sender side)
   int      pktRcvLoss;                 // number of lost packets (receiver side)
   int      pktRetrans;                 // number of retransmitted packets
   int      pktRcvRetrans;              // number of retransmitted packets received
   int      pktSentACK;                 // number of sent ACK packets
   int      pktRecvACK;                 // number of received ACK packets
   int      pktSentNAK;                 // number of sent NAK packets
   int      pktRecvNAK;                 // number of received NAK packets
   double   mbpsSendRate;               // sending rate in Mb/s
   double   mbpsRecvRate;               // receiving rate in Mb/s
   int64_t  usSndDuration;              // busy sending time (i.e., idle time exclusive)
   int      pktReorderDistance;         // size of order discrepancy in received sequences
   double   pktRcvAvgBelatedTime;       // average time of packet delay for belated packets (packets with sequence past the ACK)
   int64_t  pktRcvBelated;              // number of received AND IGNORED packets due to having come too late
   //>new
   int      pktSndDrop;                 // number of too-late-to-send dropped packets
   int      pktRcvDrop;                 // number of too-late-to play missing packets
   int      pktRcvUndecrypt;            // number of undecrypted packets
   uint64_t byteSent;                   // number of sent data bytes, including retransmissions
   uint64_t byteRecv;                   // number of received bytes
#ifdef SRT_ENABLE_LOSTBYTESCOUNT
   uint64_t byteRcvLoss;                // number of retransmitted bytes
#endif
   uint64_t byteRetrans;                // number of retransmitted bytes
   uint64_t byteSndDrop;                // number of too-late-to-send dropped bytes
   uint64_t byteRcvDrop;                // number of too-late-to play missing bytes (estimate based on average packet size)
   uint64_t byteRcvUndecrypt;           // number of undecrypted bytes
   //<

   // instant measurements
   double   usPktSndPeriod;             // packet sending period, in microseconds
   int      pktFlowWindow;              // flow window size, in number of packets
   int      pktCongestionWindow;        // congestion window size, in number of packets
   int      pktFlightSize;              // number of packets on flight
   double   msRTT;                      // RTT, in milliseconds
   double   mbpsBandwidth;              // estimated bandwidth, in Mb/s
   int      byteAvailSndBuf;            // available UDT sender buffer size
   int      byteAvailRcvBuf;            // available UDT receiver buffer size
   //>new
   double   mbpsMaxBW;                  // Transmit Bandwidth ceiling (Mbps)
   int      byteMSS;                    // MTU

   int      pktSndBuf;                  // UnACKed packets in UDT sender
   int      byteSndBuf;                 // UnACKed bytes in UDT sender
   int      msSndBuf;                   // UnACKed timespan (msec) of UDT sender
   int      msSndTsbPdDelay;            // Timestamp-based Packet Delivery Delay

   int      pktRcvBuf;                  // Undelivered packets in UDT receiver
   int      byteRcvBuf;                 // Undelivered bytes of UDT receiver
   int      msRcvBuf;                   // Undelivered timespan (msec) of UDT receiver
   int      msRcvTsbPdDelay;            // Timestamp-based Packet Delivery Delay

   int      pktSndFilterExtraTotal;     // number of control packets supplied by packet filter
   int      pktRcvFilterExtraTotal;     // number of control packets received and not supplied back
   int      pktRcvFilterSupplyTotal;    // number of packets that the filter supplied extra (e.g. FEC rebuilt)
   int      pktRcvFilterLossTotal;      // number of packet loss not coverable by filter

   int      pktSndFilterExtra;          // number of control packets supplied by packet filter
   int      pktRcvFilterExtra;          // number of control packets received and not supplied back
   int      pktRcvFilterSupply;         // number of packets that the filter supplied extra (e.g. FEC rebuilt)
   int      pktRcvFilterLoss;           // number of packet loss not coverable by filter
   int      pktReorderTolerance;        // packet reorder tolerance value
   //<
};

////////////////////////////////////////////////////////////////////////////////

// Error codes - define outside the CUDTException class
// because otherwise you'd have to use CUDTException::MJ_SUCCESS etc.
// in all throw CUDTException expressions.
enum CodeMajor
{
    MJ_UNKNOWN    = -1,
    MJ_SUCCESS    =  0,
    MJ_SETUP      =  1,
    MJ_CONNECTION =  2,
    MJ_SYSTEMRES  =  3,
    MJ_FILESYSTEM =  4,
    MJ_NOTSUP     =  5,
    MJ_AGAIN      =  6,
    MJ_PEERERROR  =  7
};

enum CodeMinor
{
    // These are "minor" error codes from various "major" categories
    // MJ_SETUP
    MN_NONE            =  0,
    MN_TIMEOUT         =  1,
    MN_REJECTED        =  2,
    MN_NORES           =  3,
    MN_SECURITY        =  4,
    // MJ_CONNECTION
    MN_CONNLOST        =  1,
    MN_NOCONN          =  2,
    // MJ_SYSTEMRES
    MN_THREAD          =  1,
    MN_MEMORY          =  2,
    // MJ_FILESYSTEM
    MN_SEEKGFAIL       =  1,
    MN_READFAIL        =  2,
    MN_SEEKPFAIL       =  3,
    MN_WRITEFAIL       =  4,
    // MJ_NOTSUP
    MN_ISBOUND         =  1,
    MN_ISCONNECTED     =  2,
    MN_INVAL           =  3,
    MN_SIDINVAL        =  4,
    MN_ISUNBOUND       =  5,
    MN_NOLISTEN        =  6,
    MN_ISRENDEZVOUS    =  7,
    MN_ISRENDUNBOUND   =  8,
    MN_INVALMSGAPI     =  9,
    MN_INVALBUFFERAPI  = 10,
    MN_BUSY            = 11,
    MN_XSIZE           = 12,
    MN_EIDINVAL        = 13,
    // MJ_AGAIN
    MN_WRAVAIL         =  1,
    MN_RDAVAIL         =  2,
    MN_XMTIMEOUT       =  3,
    MN_CONGESTION      =  4
};

static const enum CodeMinor MN_ISSTREAM SRT_ATR_DEPRECATED = (enum CodeMinor)(9);
static const enum CodeMinor MN_ISDGRAM SRT_ATR_DEPRECATED = (enum CodeMinor)(10);

// Stupid, but effective. This will be #undefined, so don't worry.
#define MJ(major) (1000 * MJ_##major)
#define MN(major, minor) (1000 * MJ_##major + MN_##minor)

// Some better way to define it, and better for C language.
typedef enum SRT_ERRNO
{
    SRT_EUNKNOWN        = -1,
    SRT_SUCCESS         = MJ_SUCCESS,

    SRT_ECONNSETUP      = MJ(SETUP),
    SRT_ENOSERVER       = MN(SETUP, TIMEOUT),
    SRT_ECONNREJ        = MN(SETUP, REJECTED),
    SRT_ESOCKFAIL       = MN(SETUP, NORES),
    SRT_ESECFAIL        = MN(SETUP, SECURITY),

    SRT_ECONNFAIL       = MJ(CONNECTION),
    SRT_ECONNLOST       = MN(CONNECTION, CONNLOST),
    SRT_ENOCONN         = MN(CONNECTION, NOCONN),

    SRT_ERESOURCE       = MJ(SYSTEMRES),
    SRT_ETHREAD         = MN(SYSTEMRES, THREAD),
    SRT_ENOBUF          = MN(SYSTEMRES, MEMORY),

    SRT_EFILE           = MJ(FILESYSTEM),
    SRT_EINVRDOFF       = MN(FILESYSTEM, SEEKGFAIL),
    SRT_ERDPERM         = MN(FILESYSTEM, READFAIL),
    SRT_EINVWROFF       = MN(FILESYSTEM, SEEKPFAIL),
    SRT_EWRPERM         = MN(FILESYSTEM, WRITEFAIL),

    SRT_EINVOP          = MJ(NOTSUP),
    SRT_EBOUNDSOCK      = MN(NOTSUP, ISBOUND),
    SRT_ECONNSOCK       = MN(NOTSUP, ISCONNECTED),
    SRT_EINVPARAM       = MN(NOTSUP, INVAL),
    SRT_EINVSOCK        = MN(NOTSUP, SIDINVAL),
    SRT_EUNBOUNDSOCK    = MN(NOTSUP, ISUNBOUND),
    SRT_ENOLISTEN       = MN(NOTSUP, NOLISTEN),
    SRT_ERDVNOSERV      = MN(NOTSUP, ISRENDEZVOUS),
    SRT_ERDVUNBOUND     = MN(NOTSUP, ISRENDUNBOUND),
    SRT_EINVALMSGAPI    = MN(NOTSUP, INVALMSGAPI),
    SRT_EINVALBUFFERAPI = MN(NOTSUP, INVALBUFFERAPI),
    SRT_EDUPLISTEN      = MN(NOTSUP, BUSY),
    SRT_ELARGEMSG       = MN(NOTSUP, XSIZE),
    SRT_EINVPOLLID      = MN(NOTSUP, EIDINVAL),

    SRT_EASYNCFAIL      = MJ(AGAIN),
    SRT_EASYNCSND       = MN(AGAIN, WRAVAIL),
    SRT_EASYNCRCV       = MN(AGAIN, RDAVAIL),
    SRT_ETIMEOUT        = MN(AGAIN, XMTIMEOUT),
    SRT_ECONGEST        = MN(AGAIN, CONGESTION),

    SRT_EPEERERR        = MJ(PEERERROR)
} SRT_ERRNO;

static const SRT_ERRNO SRT_EISSTREAM SRT_ATR_DEPRECATED = (SRT_ERRNO) MN(NOTSUP, INVALMSGAPI);
static const SRT_ERRNO SRT_EISDGRAM  SRT_ATR_DEPRECATED = (SRT_ERRNO) MN(NOTSUP, INVALBUFFERAPI);

#undef MJ
#undef MN

enum SRT_REJECT_REASON
{
    SRT_REJ_UNKNOWN,     // initial set when in progress
    SRT_REJ_SYSTEM,      // broken due to system function error
    SRT_REJ_PEER,        // connection was rejected by peer
    SRT_REJ_RESOURCE,    // internal problem with resource allocation
    SRT_REJ_ROGUE,       // incorrect data in handshake messages
    SRT_REJ_BACKLOG,     // listener's backlog exceeded
    SRT_REJ_IPE,         // internal program error
    SRT_REJ_CLOSE,       // socket is closing
    SRT_REJ_VERSION,     // peer is older version than agent's minimum set
    SRT_REJ_RDVCOOKIE,   // rendezvous cookie collision
    SRT_REJ_BADSECRET,   // wrong password
    SRT_REJ_UNSECURE,    // password required or unexpected
    SRT_REJ_MESSAGEAPI,  // streamapi/messageapi collision
    SRT_REJ_CONGESTION,  // incompatible congestion-controller type
    SRT_REJ_FILTER,       // incompatible packet filter

    SRT_REJ__SIZE,
};

// Logging API - specialization for SRT.

// Define logging functional areas for log selection.
// Use values greater than 0. Value 0 is reserved for LOGFA_GENERAL,
// which is considered always enabled.

// Logger Functional Areas
// Note that 0 is "general".

// Made by #define so that it's available also for C API.
#define SRT_LOGFA_GENERAL   0
#define SRT_LOGFA_BSTATS    1
#define SRT_LOGFA_CONTROL   2
#define SRT_LOGFA_DATA      3
#define SRT_LOGFA_TSBPD     4
#define SRT_LOGFA_REXMIT    5
#define SRT_LOGFA_HAICRYPT  6
#define SRT_LOGFA_CONGEST   7

// To make a typical int32_t size, although still use std::bitset.
// C API will carry it over.
#define SRT_LOGFA_LASTNONE 31

enum SRT_KM_STATE
{
    SRT_KM_S_UNSECURED = 0,      //No encryption
    SRT_KM_S_SECURING  = 1,      //Stream encrypted, exchanging Keying Material
    SRT_KM_S_SECURED   = 2,      //Stream encrypted, keying Material exchanged, decrypting ok.
    SRT_KM_S_NOSECRET  = 3,      //Stream encrypted and no secret to decrypt Keying Material
    SRT_KM_S_BADSECRET = 4       //Stream encrypted and wrong secret, cannot decrypt Keying Material
};

enum SRT_EPOLL_OPT
{
   SRT_EPOLL_OPT_NONE = 0x0, // fallback
   // this values are defined same as linux epoll.h
   // so that if system values are used by mistake, they should have the same effect
   SRT_EPOLL_IN       = 0x1,
   SRT_EPOLL_OUT      = 0x4,
   SRT_EPOLL_ERR      = 0x8,
   SRT_EPOLL_ET       = 1u << 31
};
// These are actually flags - use a bit container:
typedef int32_t SRT_EPOLL_T;

enum SRT_EPOLL_FLAGS
{
    /// This allows the EID container to be empty when calling the waiting
    /// function with infinite time. This means an infinite hangup, although
    /// a socket can be added to this EID from a separate thread.
    SRT_EPOLL_ENABLE_EMPTY = 1,

    /// This makes the waiting function check if there is output container
    /// passed to it, and report an error if it isn't. By default it is allowed
    /// that the output container is 0 size or NULL and therefore the readiness
    /// state is reported only as a number of ready sockets from return value.
    SRT_EPOLL_ENABLE_OUTPUTCHECK = 2
};

#ifdef __cplusplus
// In C++ these enums cannot be treated as int and glued by operator |.
// Unless this operator is defined.
inline SRT_EPOLL_OPT operator|(SRT_EPOLL_OPT a1, SRT_EPOLL_OPT a2)
{
    return SRT_EPOLL_OPT( (int)a1 | (int)a2 );
}

inline bool operator&(int flags, SRT_EPOLL_OPT eflg)
{
    // Using an enum prevents treating int automatically as enum,
    // requires explicit enum to be passed here, and minimizes the
    // risk that the right side value will contain multiple flags.
    return (flags & int(eflg)) != 0;
}
#endif



typedef struct CBytePerfMon SRT_TRACEBSTATS;

static const SRTSOCKET SRT_INVALID_SOCK = -1;
static const int SRT_ERROR = -1;

// library initialization
SRT_API       int srt_startup(void);
SRT_API       int srt_cleanup(void);

//
// Socket operations
//
SRT_API SRTSOCKET srt_socket       (int af, int type, int protocol);
SRT_API SRTSOCKET srt_create_socket();
SRT_API       int srt_bind         (SRTSOCKET u, const struct sockaddr* name, int namelen);
SRT_API       int srt_bind_peerof  (SRTSOCKET u, UDPSOCKET udpsock);
SRT_API       int srt_listen       (SRTSOCKET u, int backlog);
SRT_API SRTSOCKET srt_accept       (SRTSOCKET u, struct sockaddr* addr, int* addrlen);
typedef int srt_listen_callback_fn   (void* opaq, SRTSOCKET ns, int hsversion, const struct sockaddr* peeraddr, const char* streamid);
SRT_API       int srt_listen_callback(SRTSOCKET lsn, srt_listen_callback_fn* hook_fn, void* hook_opaque);
SRT_API       int srt_connect      (SRTSOCKET u, const struct sockaddr* name, int namelen);
SRT_API       int srt_connect_debug(SRTSOCKET u, const struct sockaddr* name, int namelen, int forced_isn);
SRT_API       int srt_rendezvous   (SRTSOCKET u, const struct sockaddr* local_name, int local_namelen,
                                    const struct sockaddr* remote_name, int remote_namelen);
SRT_API       int srt_close        (SRTSOCKET u);
SRT_API       int srt_getpeername  (SRTSOCKET u, struct sockaddr* name, int* namelen);
SRT_API       int srt_getsockname  (SRTSOCKET u, struct sockaddr* name, int* namelen);
SRT_API       int srt_getsockopt   (SRTSOCKET u, int level /*ignored*/, SRT_SOCKOPT optname, void* optval, int* optlen);
SRT_API       int srt_setsockopt   (SRTSOCKET u, int level /*ignored*/, SRT_SOCKOPT optname, const void* optval, int optlen);
SRT_API       int srt_getsockflag  (SRTSOCKET u, SRT_SOCKOPT opt, void* optval, int* optlen);
SRT_API       int srt_setsockflag  (SRTSOCKET u, SRT_SOCKOPT opt, const void* optval, int optlen);


// XXX Note that the srctime functionality doesn't work yet and needs fixing.
typedef struct SRT_MsgCtrl_
{
   int flags;            // Left for future
   int msgttl;           // TTL for a message, default -1 (no TTL limitation)
   int inorder;          // Whether a message is allowed to supersede partially lost one. Unused in stream and live mode.
   int boundary;         // 0:mid pkt, 1(01b):end of frame, 2(11b):complete frame, 3(10b): start of frame
   uint64_t srctime;     // source timestamp (usec), 0: use internal time     
   int32_t pktseq;       // sequence number of the first packet in received message (unused for sending)
   int32_t msgno;        // message number (output value for both sending and receiving)
} SRT_MSGCTRL;

// You are free to use either of these two methods to set SRT_MSGCTRL object
// to default values: either call srt_msgctrl_init(&obj) or obj = srt_msgctrl_default.
SRT_API void srt_msgctrl_init(SRT_MSGCTRL* mctrl);
SRT_API extern const SRT_MSGCTRL srt_msgctrl_default;

// The send/receive functions.
// These functions have different names due to different sets of parameters
// to be supplied. Not all of them are needed or make sense in all modes:

// Plain: supply only the buffer and its size.
// Msg: supply additionally
// - TTL (message is not delivered when exceeded) and
// - INORDER (when false, the message is allowed to be delivered in different
// order than when it was sent, when the later message is earlier ready to
// deliver)
// Msg2: Supply extra parameters in SRT_MSGCTRL. When receiving, these
// parameters will be filled, as needed. NULL is acceptable, in which case
// the defaults are used.

// NOTE: srt_send and srt_recv have the last "..." left to allow ignore a
// deprecated and unused "flags" parameter. After confirming that all
// compat applications that pass useless 0 there are fixed, this will be
// removed.

//
// Sending functions
//
SRT_API int srt_send    (SRTSOCKET u, const char* buf, int len);
SRT_API int srt_sendmsg (SRTSOCKET u, const char* buf, int len, int ttl/* = -1*/, int inorder/* = false*/);
SRT_API int srt_sendmsg2(SRTSOCKET u, const char* buf, int len, SRT_MSGCTRL *mctrl);

//
// Receiving functions
//
SRT_API int srt_recv    (SRTSOCKET u, char* buf, int len);

// srt_recvmsg is actually an alias to srt_recv, it stays under the old name for compat reasons.
SRT_API int srt_recvmsg (SRTSOCKET u, char* buf, int len);
SRT_API int srt_recvmsg2(SRTSOCKET u, char *buf, int len, SRT_MSGCTRL *mctrl);


// Special send/receive functions for files only.
#define SRT_DEFAULT_SENDFILE_BLOCK 364000
#define SRT_DEFAULT_RECVFILE_BLOCK 7280000
SRT_API int64_t srt_sendfile(SRTSOCKET u, const char* path, int64_t* offset, int64_t size, int block);
SRT_API int64_t srt_recvfile(SRTSOCKET u, const char* path, int64_t* offset, int64_t size, int block);


// last error detection
SRT_API const char* srt_getlasterror_str(void);
SRT_API        int  srt_getlasterror(int* errno_loc);
SRT_API const char* srt_strerror(int code, int errnoval);
SRT_API       void  srt_clearlasterror(void);

// performance track
// perfmon with Byte counters for better bitrate estimation.
SRT_API int srt_bstats(SRTSOCKET u, SRT_TRACEBSTATS * perf, int clear);
// permon with Byte counters and instantaneous stats instead of moving averages for Snd/Rcvbuffer sizes.
SRT_API int srt_bistats(SRTSOCKET u, SRT_TRACEBSTATS * perf, int clear, int instantaneous);

// Socket Status (for problem tracking)
SRT_API SRT_SOCKSTATUS srt_getsockstate(SRTSOCKET u);

SRT_API int srt_epoll_create(void);
SRT_API int srt_epoll_add_usock(int eid, SRTSOCKET u, const int* events);
SRT_API int srt_epoll_add_ssock(int eid, SYSSOCKET s, const int* events);
SRT_API int srt_epoll_remove_usock(int eid, SRTSOCKET u);
SRT_API int srt_epoll_remove_ssock(int eid, SYSSOCKET s);
SRT_API int srt_epoll_update_usock(int eid, SRTSOCKET u, const int* events);
SRT_API int srt_epoll_update_ssock(int eid, SYSSOCKET s, const int* events);

SRT_API int srt_epoll_wait(int eid, SRTSOCKET* readfds, int* rnum, SRTSOCKET* writefds, int* wnum, int64_t msTimeOut,
                           SYSSOCKET* lrfds, int* lrnum, SYSSOCKET* lwfds, int* lwnum);
typedef struct SRT_EPOLL_EVENT_
{
    SRTSOCKET fd;
    int       events; // SRT_EPOLL_IN | SRT_EPOLL_OUT | SRT_EPOLL_ERR
} SRT_EPOLL_EVENT;
SRT_API int srt_epoll_uwait(int eid, SRT_EPOLL_EVENT* fdsSet, int fdsSize, int64_t msTimeOut);

SRT_API int32_t srt_epoll_set(int eid, int32_t flags);
SRT_API int srt_epoll_release(int eid);

// Logging control

SRT_API void srt_setloglevel(int ll);
SRT_API void srt_addlogfa(int fa);
SRT_API void srt_dellogfa(int fa);
SRT_API void srt_resetlogfa(const int* fara, size_t fara_size);
// This isn't predicted, will be only available in SRT C++ API.
// For the time being, until this API is ready, use UDT::setlogstream.
// SRT_API void srt_setlogstream(std::ostream& stream);
SRT_API void srt_setloghandler(void* opaque, SRT_LOG_HANDLER_FN* handler);
SRT_API void srt_setlogflags(int flags);


SRT_API int srt_getsndbuffer(SRTSOCKET sock, size_t* blocks, size_t* bytes);

SRT_API enum SRT_REJECT_REASON srt_getrejectreason(SRTSOCKET sock);
SRT_API extern const char* const srt_rejectreason_msg [];
const char* srt_rejectreason_str(enum SRT_REJECT_REASON id);

#ifdef __cplusplus
}
#endif

#endif
