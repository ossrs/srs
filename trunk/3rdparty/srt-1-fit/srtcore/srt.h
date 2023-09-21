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

#ifndef INC_SRTC_H
#define INC_SRTC_H

#include "version.h"

#include "platform_sys.h"

#include <string.h>
#include <stdlib.h>

#include "logging_api.h"

////////////////////////////////////////////////////////////////////////////////

//if compiling on VC6.0 or pre-WindowsXP systems
//use -DLEGACY_WIN32

//if compiling with MinGW, it only works on XP or above
//use -D_WIN32_WINNT=0x0501


#ifdef _WIN32
   #ifndef __MINGW32__
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
   #endif
   #ifdef SRT_DYNAMIC
      #ifdef SRT_EXPORTS
         #define SRT_API __declspec(dllexport)
      #else
         #define SRT_API __declspec(dllimport)
      #endif
   #else // !SRT_DYNAMIC
      #define SRT_API
   #endif
#else
   #define SRT_API __attribute__ ((visibility("default")))
#endif


// For feature tests if you need.
// You can use these constants with SRTO_MINVERSION option.
#define SRT_VERSION_FEAT_HSv5 0x010300

#if defined(__cplusplus) && __cplusplus > 201406
#define SRT_HAVE_CXX17 1
#else
#define SRT_HAVE_CXX17 0
#endif


// Standard attributes

// When compiling in C++17 mode, use the standard C++17 attributes
// (out of these, only [[deprecated]] is supported in C++14, so
// for all lesser standard use compiler-specific attributes).
#if SRT_HAVE_CXX17

// Unused: DO NOT issue a warning if this entity is unused.
#define SRT_ATR_UNUSED [[maybe_unused]]

// Nodiscard: issue a warning if the return value was discarded.
#define SRT_ATR_NODISCARD [[nodiscard]]

// GNUG is GNU C/C++; this syntax is also supported by Clang
#elif defined(__GNUC__)
#define SRT_ATR_UNUSED __attribute__((unused))
#define SRT_ATR_NODISCARD __attribute__((warn_unused_result))
#elif defined(_MSC_VER)
#define SRT_ATR_UNUSED __pragma(warning(suppress: 4100 4101))
#define SRT_ATR_NODISCARD _Check_return_
#else
#define SRT_ATR_UNUSED
#define SRT_ATR_NODISCARD
#endif


// DEPRECATED attributes

// There's needed DEPRECATED and DEPRECATED_PX, as some compilers require them
// before the entity, others after the entity.
// The *_PX version is the prefix attribute, which applies only
// to functions (Microsoft compilers).

// When deprecating a function, mark it:
//
// SRT_ATR_DEPRECATED_PX retval function(arguments) SRT_ATR_DEPRECATED;
//

// When SRT_NO_DEPRECATED defined, do not issue any deprecation warnings.
// Regardless of the compiler type.
#if defined(SRT_NO_DEPRECATED)

#define SRT_ATR_DEPRECATED
#define SRT_ATR_DEPRECATED_PX

#elif SRT_HAVE_CXX17

#define SRT_ATR_DEPRECATED
#define SRT_ATR_DEPRECATED_PX [[deprecated]]

// GNUG is GNU C/C++; this syntax is also supported by Clang
#elif defined(__GNUC__)
#define SRT_ATR_DEPRECATED_PX
#define SRT_ATR_DEPRECATED __attribute__((deprecated))
#elif defined(_MSC_VER)
#define SRT_ATR_DEPRECATED_PX __declspec(deprecated)
#define SRT_ATR_DEPRECATED // no postfix-type modifier
#else
#define SRT_ATR_DEPRECATED_PX
#define SRT_ATR_DEPRECATED
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef int32_t SRTSOCKET;

// The most significant bit 31 (sign bit actually) is left unused,
// so that all people who check the value for < 0 instead of -1
// still get what they want. The bit 30 is reserved for marking
// the "socket group". Most of the API functions should work
// transparently with the socket descriptor designating a single
// socket or a socket group.
static const int32_t SRTGROUP_MASK = (1 << 30);

#ifdef _WIN32
   typedef SOCKET SYSSOCKET;
#else
   typedef int SYSSOCKET;
#endif

#ifndef ENABLE_BONDING
#define ENABLE_BONDING 0
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
   // (some space left)
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
   SRTO_INPUTBW = 24,        // Estimated input stream rate.
   SRTO_OHEADBW,             // MaxBW ceiling based on % over input stream rate. Applies when UDT_MAXBW=0 (auto).
   SRTO_PASSPHRASE = 26,     // Crypto PBKDF2 Passphrase (must be 10..79 characters, or empty to disable encryption)
   SRTO_PBKEYLEN,            // Crypto key len in bytes {16,24,32} Default: 16 (AES-128)
   SRTO_KMSTATE,             // Key Material exchange status (UDT_SRTKmState)
   SRTO_IPTTL = 29,          // IP Time To Live (passthru for system sockopt IPPROTO_IP/IP_TTL)
   SRTO_IPTOS,               // IP Type of Service (passthru for system sockopt IPPROTO_IP/IP_TOS)
   SRTO_TLPKTDROP = 31,      // Enable receiver pkt drop
   SRTO_SNDDROPDELAY = 32,   // Extra delay towards latency for sender TLPKTDROP decision (-1 to off)
   SRTO_NAKREPORT = 33,      // Enable receiver to send periodic NAK reports
   SRTO_VERSION = 34,        // Local SRT Version
   SRTO_PEERVERSION,         // Peer SRT Version (from SRT Handshake)
   SRTO_CONNTIMEO = 36,      // Connect timeout in msec. Caller default: 3000, rendezvous (x 10)
   SRTO_DRIFTTRACER = 37,    // Enable or disable drift tracer
   SRTO_MININPUTBW = 38,     // Minimum estimate of input stream rate.
   // (some space left)
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
   SRTO_BINDTODEVICE,        // Forward the SOL_SOCKET/SO_BINDTODEVICE option on socket (pass packets only from that device)
   SRTO_GROUPCONNECT,        // Set on a listener to allow group connection (ENABLE_BONDING)
   SRTO_GROUPMINSTABLETIMEO, // Minimum Link Stability timeout (backup mode) in milliseconds (ENABLE_BONDING)
   SRTO_GROUPTYPE,           // Group type to which an accepted socket is about to be added, available in the handshake (ENABLE_BONDING)
   SRTO_PACKETFILTER = 60,   // Add and configure a packet filter
   SRTO_RETRANSMITALGO = 61, // An option to select packet retransmission algorithm
#ifdef ENABLE_AEAD_API_PREVIEW
   SRTO_CRYPTOMODE = 62,     // Encryption cipher mode (AES-CTR, AES-GCM, ...).
#endif
#ifdef ENABLE_MAXREXMITBW
   SRTO_MAXREXMITBW = 63,    // Maximum bandwidth limit for retransmision (Bytes/s)
#endif

   SRTO_E_SIZE // Always last element, not a valid option.
} SRT_SOCKOPT;


#ifdef __cplusplus


#if __cplusplus > 199711L // C++11
    // Newer compilers report error when [[deprecated]] is applied to types,
    // and C++11 and higher uses this.
    // Note that this doesn't exactly use the 'deprecated' attribute,
    // as it's introduced in C++14. What is actually used here is the
    // fact that unknown attributes are ignored, but still warned about.
    // This should only catch an eye - and that's what it does.
#define SRT_DEPRECATED_OPTION(value) ((SRT_SOCKOPT [[deprecated]])value)
#else
    // Older (pre-C++11) compilers use gcc deprecated applied to a typedef
    typedef SRT_ATR_DEPRECATED_PX SRT_SOCKOPT SRT_SOCKOPT_DEPRECATED SRT_ATR_DEPRECATED;
#define SRT_DEPRECATED_OPTION(value) ((SRT_SOCKOPT_DEPRECATED)value)
#endif


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

// Note that there are no deprecated options at the moment, but the mechanism
// stays so that it can be used in future. Example:
// #define SRTO_STRICTENC SRT_DEPRECATED_OPTION(53)

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
   uint64_t byteRcvLossTotal;           // total number of lost bytes
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
   uint64_t byteRcvLoss;                // number of retransmitted bytes
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

   // New stats in 1.5.0

   // Total
   int64_t  pktSentUniqueTotal;         // total number of data packets sent by the application
   int64_t  pktRecvUniqueTotal;         // total number of packets to be received by the application
   uint64_t byteSentUniqueTotal;        // total number of data bytes, sent by the application
   uint64_t byteRecvUniqueTotal;        // total number of data bytes to be received by the application

   // Local
   int64_t  pktSentUnique;              // number of data packets sent by the application
   int64_t  pktRecvUnique;              // number of packets to be received by the application
   uint64_t byteSentUnique;             // number of data bytes, sent by the application
   uint64_t byteRecvUnique;             // number of data bytes to be received by the application
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
    MN_CLOSED          =  5,
    // MJ_CONNECTION
    MN_CONNLOST        =  1,
    MN_NOCONN          =  2,
    // MJ_SYSTEMRES
    MN_THREAD          =  1,
    MN_MEMORY          =  2,
    MN_OBJECT          =  3,
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
    MN_EEMPTY          = 14,
    MN_BUSYPORT        = 15,
    // MJ_AGAIN
    MN_WRAVAIL         =  1,
    MN_RDAVAIL         =  2,
    MN_XMTIMEOUT       =  3,
    MN_CONGESTION      =  4
};


// Stupid, but effective. This will be #undefined, so don't worry.
#define SRT_EMJ(major) (1000 * MJ_##major)
#define SRT_EMN(major, minor) (1000 * MJ_##major + MN_##minor)

// Some better way to define it, and better for C language.
typedef enum SRT_ERRNO
{
    SRT_EUNKNOWN        = -1,
    SRT_SUCCESS         = MJ_SUCCESS,

    SRT_ECONNSETUP      = SRT_EMJ(SETUP),
    SRT_ENOSERVER       = SRT_EMN(SETUP, TIMEOUT),
    SRT_ECONNREJ        = SRT_EMN(SETUP, REJECTED),
    SRT_ESOCKFAIL       = SRT_EMN(SETUP, NORES),
    SRT_ESECFAIL        = SRT_EMN(SETUP, SECURITY),
    SRT_ESCLOSED        = SRT_EMN(SETUP, CLOSED),

    SRT_ECONNFAIL       = SRT_EMJ(CONNECTION),
    SRT_ECONNLOST       = SRT_EMN(CONNECTION, CONNLOST),
    SRT_ENOCONN         = SRT_EMN(CONNECTION, NOCONN),

    SRT_ERESOURCE       = SRT_EMJ(SYSTEMRES),
    SRT_ETHREAD         = SRT_EMN(SYSTEMRES, THREAD),
    SRT_ENOBUF          = SRT_EMN(SYSTEMRES, MEMORY),
    SRT_ESYSOBJ         = SRT_EMN(SYSTEMRES, OBJECT),

    SRT_EFILE           = SRT_EMJ(FILESYSTEM),
    SRT_EINVRDOFF       = SRT_EMN(FILESYSTEM, SEEKGFAIL),
    SRT_ERDPERM         = SRT_EMN(FILESYSTEM, READFAIL),
    SRT_EINVWROFF       = SRT_EMN(FILESYSTEM, SEEKPFAIL),
    SRT_EWRPERM         = SRT_EMN(FILESYSTEM, WRITEFAIL),

    SRT_EINVOP          = SRT_EMJ(NOTSUP),
    SRT_EBOUNDSOCK      = SRT_EMN(NOTSUP, ISBOUND),
    SRT_ECONNSOCK       = SRT_EMN(NOTSUP, ISCONNECTED),
    SRT_EINVPARAM       = SRT_EMN(NOTSUP, INVAL),
    SRT_EINVSOCK        = SRT_EMN(NOTSUP, SIDINVAL),
    SRT_EUNBOUNDSOCK    = SRT_EMN(NOTSUP, ISUNBOUND),
    SRT_ENOLISTEN       = SRT_EMN(NOTSUP, NOLISTEN),
    SRT_ERDVNOSERV      = SRT_EMN(NOTSUP, ISRENDEZVOUS),
    SRT_ERDVUNBOUND     = SRT_EMN(NOTSUP, ISRENDUNBOUND),
    SRT_EINVALMSGAPI    = SRT_EMN(NOTSUP, INVALMSGAPI),
    SRT_EINVALBUFFERAPI = SRT_EMN(NOTSUP, INVALBUFFERAPI),
    SRT_EDUPLISTEN      = SRT_EMN(NOTSUP, BUSY),
    SRT_ELARGEMSG       = SRT_EMN(NOTSUP, XSIZE),
    SRT_EINVPOLLID      = SRT_EMN(NOTSUP, EIDINVAL),
    SRT_EPOLLEMPTY      = SRT_EMN(NOTSUP, EEMPTY),
    SRT_EBINDCONFLICT   = SRT_EMN(NOTSUP, BUSYPORT),

    SRT_EASYNCFAIL      = SRT_EMJ(AGAIN),
    SRT_EASYNCSND       = SRT_EMN(AGAIN, WRAVAIL),
    SRT_EASYNCRCV       = SRT_EMN(AGAIN, RDAVAIL),
    SRT_ETIMEOUT        = SRT_EMN(AGAIN, XMTIMEOUT),
    SRT_ECONGEST        = SRT_EMN(AGAIN, CONGESTION),

    SRT_EPEERERR        = SRT_EMJ(PEERERROR)
} SRT_ERRNO;

#undef SRT_EMJ
#undef SRT_EMN

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
    SRT_REJ_FILTER,      // incompatible packet filter
    SRT_REJ_GROUP,       // incompatible group
    SRT_REJ_TIMEOUT,     // connection timeout
#ifdef ENABLE_AEAD_API_PREVIEW
    SRT_REJ_CRYPTO,      // conflicting cryptographic configurations
#endif

    SRT_REJ_E_SIZE,
};

// XXX This value remains for some time, but it's deprecated
// Planned deprecation removal: rel1.6.0.
#define SRT_REJ__SIZE SRT_REJ_E_SIZE

// Reject category codes:

#define SRT_REJC_VALUE(code) (1000 * (code/1000))
#define SRT_REJC_INTERNAL 0     // Codes from above SRT_REJECT_REASON enum
#define SRT_REJC_PREDEFINED 1000  // Standard server error codes
#define SRT_REJC_USERDEFINED 2000    // User defined error codes


// Logging API - specialization for SRT.

// WARNING: This part is generated.

// Logger Functional Areas
// Note that 0 is "general".

// Values 0* - general, unqualified
// Values 1* - control
// Values 2* - receiving
// Values 3* - sending
// Values 4* - management

// Made by #define so that it's available also for C API.

// Use ../scripts/generate-logging-defs.tcl to regenerate.

// SRT_LOGFA BEGIN GENERATED SECTION {

#define SRT_LOGFA_GENERAL    0   // gglog: General uncategorized log, for serious issues only
#define SRT_LOGFA_SOCKMGMT   1   // smlog: Socket create/open/close/configure activities
#define SRT_LOGFA_CONN       2   // cnlog: Connection establishment and handshake
#define SRT_LOGFA_XTIMER     3   // xtlog: The checkTimer and around activities
#define SRT_LOGFA_TSBPD      4   // tslog: The TsBPD thread
#define SRT_LOGFA_RSRC       5   // rslog: System resource allocation and management

#define SRT_LOGFA_CONGEST    7   // cclog: Congestion control module
#define SRT_LOGFA_PFILTER    8   // pflog: Packet filter module

#define SRT_LOGFA_API_CTRL   11  // aclog: API part for socket and library managmenet

#define SRT_LOGFA_QUE_CTRL   13  // qclog: Queue control activities

#define SRT_LOGFA_EPOLL_UPD  16  // eilog: EPoll, internal update activities

#define SRT_LOGFA_API_RECV   21  // arlog: API part for receiving
#define SRT_LOGFA_BUF_RECV   22  // brlog: Buffer, receiving side
#define SRT_LOGFA_QUE_RECV   23  // qrlog: Queue, receiving side
#define SRT_LOGFA_CHN_RECV   24  // krlog: CChannel, receiving side
#define SRT_LOGFA_GRP_RECV   25  // grlog: Group, receiving side

#define SRT_LOGFA_API_SEND   31  // aslog: API part for sending
#define SRT_LOGFA_BUF_SEND   32  // bslog: Buffer, sending side
#define SRT_LOGFA_QUE_SEND   33  // qslog: Queue, sending side
#define SRT_LOGFA_CHN_SEND   34  // kslog: CChannel, sending side
#define SRT_LOGFA_GRP_SEND   35  // gslog: Group, sending side

#define SRT_LOGFA_INTERNAL   41  // inlog: Internal activities not connected directly to a socket

#define SRT_LOGFA_QUE_MGMT   43  // qmlog: Queue, management part
#define SRT_LOGFA_CHN_MGMT   44  // kmlog: CChannel, management part
#define SRT_LOGFA_GRP_MGMT   45  // gmlog: Group, management part
#define SRT_LOGFA_EPOLL_API  46  // ealog: EPoll, API part

#define SRT_LOGFA_HAICRYPT   6   // hclog: Haicrypt module area
#define SRT_LOGFA_APPLOG     10  // aplog: Applications

// } SRT_LOGFA END GENERATED SECTION

// To make a typical int64_t size, although still use std::bitset.
// C API will carry it over.
#define SRT_LOGFA_LASTNONE 63

enum SRT_KM_STATE
{
    SRT_KM_S_UNSECURED     = 0, // No encryption
    SRT_KM_S_SECURING      = 1, // Stream encrypted, exchanging Keying Material
    SRT_KM_S_SECURED       = 2, // Stream encrypted, keying Material exchanged, decrypting ok.
    SRT_KM_S_NOSECRET      = 3, // Stream encrypted and no secret to decrypt Keying Material
    SRT_KM_S_BADSECRET     = 4 // Stream encrypted and wrong secret is used, cannot decrypt Keying Material
#ifdef ENABLE_AEAD_API_PREVIEW
    ,SRT_KM_S_BADCRYPTOMODE = 5  // Stream encrypted but wrong cryptographic mode is used, cannot decrypt. Since v1.5.2.
#endif
};

enum SRT_EPOLL_OPT
{
   SRT_EPOLL_OPT_NONE = 0x0, // fallback

   // Values intended to be the same as in `<sys/epoll.h>`.
   // so that if system values are used by mistake, they should have the same effect
   // This applies to: IN, OUT, ERR and ET.

   /// Ready for 'recv' operation:
   ///
   /// - For stream mode it means that at least 1 byte is available.
   /// In this mode the buffer may extract only a part of the packet,
   /// leaving next data possible for extraction later.
   ///
   /// - For message mode it means that there is at least one packet
   /// available (this may change in future, as it is desired that
   /// one full message should only wake up, not single packet of a
   /// not yet extractable message).
   ///
   /// - For live mode it means that there's at least one packet
   /// ready to play.
   ///
   /// - For listener sockets, this means that there is a new connection
   /// waiting for pickup through the `srt_accept()` call, that is,
   /// the next call to `srt_accept()` will succeed without blocking
   /// (see an alias SRT_EPOLL_ACCEPT below).
   SRT_EPOLL_IN       = 0x1,

   /// Ready for 'send' operation.
   ///
   /// - For stream mode it means that there's a free space in the
   /// sender buffer for at least 1 byte of data. The next send
   /// operation will only allow to send as much data as it is free
   /// space in the buffer.
   ///
   /// - For message mode it means that there's a free space for at
   /// least one UDP packet. The edge-triggered mode can be used to
   /// pick up updates as the free space in the sender buffer grows.
   ///
   /// - For live mode it means that there's a free space for at least
   /// one UDP packet. On the other hand, no readiness for OUT usually
   /// means an extraordinary congestion on the link, meaning also that
   /// you should immediately slow down the sending rate or you may get
   /// a connection break soon.
   ///
   /// - For non-blocking sockets used with `srt_connect*` operation,
   /// this flag simply means that the connection was established.
   SRT_EPOLL_OUT      = 0x4,

   /// The socket has encountered an error in the last operation
   /// and the next operation on that socket will end up with error.
   /// You can retry the operation, but getting the error from it
   /// is certain, so you may as well close the socket.
   SRT_EPOLL_ERR      = 0x8,

   // To avoid confusion in the internal code, the following
   // duplicates are introduced to improve clarity.
   SRT_EPOLL_CONNECT = SRT_EPOLL_OUT,
   SRT_EPOLL_ACCEPT = SRT_EPOLL_IN,

   SRT_EPOLL_UPDATE = 0x10,
   SRT_EPOLL_ET       = 1u << 31
};
// These are actually flags - use a bit container:
typedef int32_t SRT_EPOLL_T;

// Define which epoll flags determine events. All others are special flags.
#define SRT_EPOLL_EVENTTYPES (SRT_EPOLL_IN | SRT_EPOLL_OUT | SRT_EPOLL_UPDATE | SRT_EPOLL_ERR)
#define SRT_EPOLL_ETONLY (SRT_EPOLL_UPDATE)

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
// DEPRECATED: srt_socket with 3 arguments. All these arguments are ignored
// and socket creation doesn't need any arguments. Use srt_create_socket().
// Planned deprecation removal: rel1.6.0
SRT_ATR_DEPRECATED_PX SRT_API SRTSOCKET srt_socket(int, int, int) SRT_ATR_DEPRECATED;
SRT_API       SRTSOCKET srt_create_socket(void);

SRT_API       int srt_bind         (SRTSOCKET u, const struct sockaddr* name, int namelen);
SRT_API       int srt_bind_acquire (SRTSOCKET u, UDPSOCKET sys_udp_sock);
// Old name of srt_bind_acquire(), please don't use
// Planned deprecation removal: rel1.6.0
SRT_ATR_DEPRECATED_PX static inline int srt_bind_peerof(SRTSOCKET u, UDPSOCKET sys_udp_sock) SRT_ATR_DEPRECATED;
static inline int srt_bind_peerof  (SRTSOCKET u, UDPSOCKET sys_udp_sock) { return srt_bind_acquire(u, sys_udp_sock); }
SRT_API       int srt_listen       (SRTSOCKET u, int backlog);
SRT_API SRTSOCKET srt_accept       (SRTSOCKET u, struct sockaddr* addr, int* addrlen);
SRT_API SRTSOCKET srt_accept_bond  (const SRTSOCKET listeners[], int lsize, int64_t msTimeOut);
typedef int srt_listen_callback_fn   (void* opaq, SRTSOCKET ns, int hsversion, const struct sockaddr* peeraddr, const char* streamid);
SRT_API       int srt_listen_callback(SRTSOCKET lsn, srt_listen_callback_fn* hook_fn, void* hook_opaque);
typedef void srt_connect_callback_fn  (void* opaq, SRTSOCKET ns, int errorcode, const struct sockaddr* peeraddr, int token);
SRT_API       int srt_connect_callback(SRTSOCKET clr, srt_connect_callback_fn* hook_fn, void* hook_opaque);
SRT_API       int srt_connect      (SRTSOCKET u, const struct sockaddr* name, int namelen);
SRT_API       int srt_connect_debug(SRTSOCKET u, const struct sockaddr* name, int namelen, int forced_isn);
SRT_API       int srt_connect_bind (SRTSOCKET u, const struct sockaddr* source,
                                    const struct sockaddr* target, int len);
SRT_API       int srt_rendezvous   (SRTSOCKET u, const struct sockaddr* local_name, int local_namelen,
                                    const struct sockaddr* remote_name, int remote_namelen);

SRT_API       int srt_close        (SRTSOCKET u);
SRT_API       int srt_getpeername  (SRTSOCKET u, struct sockaddr* name, int* namelen);
SRT_API       int srt_getsockname  (SRTSOCKET u, struct sockaddr* name, int* namelen);
SRT_API       int srt_getsockopt   (SRTSOCKET u, int level /*ignored*/, SRT_SOCKOPT optname, void* optval, int* optlen);
SRT_API       int srt_setsockopt   (SRTSOCKET u, int level /*ignored*/, SRT_SOCKOPT optname, const void* optval, int optlen);
SRT_API       int srt_getsockflag  (SRTSOCKET u, SRT_SOCKOPT opt, void* optval, int* optlen);
SRT_API       int srt_setsockflag  (SRTSOCKET u, SRT_SOCKOPT opt, const void* optval, int optlen);

typedef struct SRT_SocketGroupData_ SRT_SOCKGROUPDATA;

typedef struct SRT_MsgCtrl_
{
   int flags;            // Left for future
   int msgttl;           // TTL for a message (millisec), default -1 (no TTL limitation)
   int inorder;          // Whether a message is allowed to supersede partially lost one. Unused in stream and live mode.
   int boundary;         // 0:mid pkt, 1(01b):end of frame, 2(11b):complete frame, 3(10b): start of frame
   int64_t srctime;      // source time since epoch (usec), 0: use internal time (sender)
   int32_t pktseq;       // sequence number of the first packet in received message (unused for sending)
   int32_t msgno;        // message number (output value for both sending and receiving)
   SRT_SOCKGROUPDATA* grpdata;
   size_t grpdata_size;
} SRT_MSGCTRL;

// Trap representation for sequence and message numbers
// This value means that this is "unset", and it's never
// a result of an operation made on this number.
static const int32_t SRT_SEQNO_NONE = -1;    // -1: no seq (0 is a valid seqno!)
static const int32_t SRT_MSGNO_NONE = -1;    // -1: unset
static const int32_t SRT_MSGNO_CONTROL = 0;  //  0: control (used by packet filter)

static const int SRT_MSGTTL_INF = -1; // unlimited TTL specification for message TTL

// XXX Might be useful also other special uses of -1:
// - -1 as infinity for srt_epoll_wait
// - -1 as a trap index value used in list.cpp

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

// Performance tracking
// Performance monitor with Byte counters for better bitrate estimation.
SRT_API int srt_bstats(SRTSOCKET u, SRT_TRACEBSTATS * perf, int clear);
// Performance monitor with Byte counters and instantaneous stats instead of moving averages for Snd/Rcvbuffer sizes.
SRT_API int srt_bistats(SRTSOCKET u, SRT_TRACEBSTATS * perf, int clear, int instantaneous);

// Socket Status (for problem tracking)
SRT_API SRT_SOCKSTATUS srt_getsockstate(SRTSOCKET u);

SRT_API int srt_epoll_create(void);
SRT_API int srt_epoll_clear_usocks(int eid);
SRT_API int srt_epoll_add_usock(int eid, SRTSOCKET u, const int* events);
SRT_API int srt_epoll_add_ssock(int eid, SYSSOCKET s, const int* events);
SRT_API int srt_epoll_remove_usock(int eid, SRTSOCKET u);
SRT_API int srt_epoll_remove_ssock(int eid, SYSSOCKET s);
SRT_API int srt_epoll_update_usock(int eid, SRTSOCKET u, const int* events);
SRT_API int srt_epoll_update_ssock(int eid, SYSSOCKET s, const int* events);

SRT_API int srt_epoll_wait(int eid, SRTSOCKET* readfds, int* rnum, SRTSOCKET* writefds, int* wnum, int64_t msTimeOut,
                           SYSSOCKET* lrfds, int* lrnum, SYSSOCKET* lwfds, int* lwnum);
typedef struct SRT_EPOLL_EVENT_STR
{
    SRTSOCKET fd;
    int       events; // SRT_EPOLL_IN | SRT_EPOLL_OUT | SRT_EPOLL_ERR
#ifdef __cplusplus
    SRT_EPOLL_EVENT_STR(SRTSOCKET s, int ev): fd(s), events(ev) {}
    SRT_EPOLL_EVENT_STR(): fd(-1), events(0) {} // NOTE: allows singular values, no init.
#endif
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

SRT_API int srt_getrejectreason(SRTSOCKET sock);
SRT_API int srt_setrejectreason(SRTSOCKET sock, int value);
// The srt_rejectreason_msg[] array is deprecated (as unsafe).
// Planned removal: v1.6.0.
SRT_API SRT_ATR_DEPRECATED extern const char* const srt_rejectreason_msg [];
SRT_API const char* srt_rejectreason_str(int id);

SRT_API uint32_t srt_getversion(void);

SRT_API int64_t srt_time_now(void);

SRT_API int64_t srt_connection_time(SRTSOCKET sock);

// Possible internal clock types
#define SRT_SYNC_CLOCK_STDCXX_STEADY      0 // C++11 std::chrono::steady_clock
#define SRT_SYNC_CLOCK_GETTIME_MONOTONIC  1 // clock_gettime with CLOCK_MONOTONIC
#define SRT_SYNC_CLOCK_WINQPC             2
#define SRT_SYNC_CLOCK_MACH_ABSTIME       3
#define SRT_SYNC_CLOCK_POSIX_GETTIMEOFDAY 4
#define SRT_SYNC_CLOCK_AMD64_RDTSC        5
#define SRT_SYNC_CLOCK_IA32_RDTSC         6
#define SRT_SYNC_CLOCK_IA64_ITC           7

SRT_API int srt_clock_type(void);

// SRT Socket Groups API (ENABLE_BONDING)

typedef enum SRT_GROUP_TYPE
{
    SRT_GTYPE_UNDEFINED,
    SRT_GTYPE_BROADCAST,
    SRT_GTYPE_BACKUP,
    // ...
    SRT_GTYPE_E_END
} SRT_GROUP_TYPE;

// Free-form flags for groups
// Flags may be type-specific!
static const uint32_t SRT_GFLAG_SYNCONMSG = 1;

typedef enum SRT_MemberStatus
{
    SRT_GST_PENDING,  // The socket is created correctly, but not yet ready for getting data.
    SRT_GST_IDLE,     // The socket is ready to be activated
    SRT_GST_RUNNING,  // The socket was already activated and is in use
    SRT_GST_BROKEN    // The last operation broke the socket, it should be closed.
} SRT_MEMBERSTATUS;

struct SRT_SocketGroupData_
{
    SRTSOCKET id;
    struct sockaddr_storage peeraddr; // Don't want to expose sockaddr_any to public API
    SRT_SOCKSTATUS sockstate;
    uint16_t weight;
    SRT_MEMBERSTATUS memberstate;
    int result;
    int token;
};

typedef struct SRT_SocketOptionObject SRT_SOCKOPT_CONFIG;

typedef struct SRT_GroupMemberConfig_
{
    SRTSOCKET id;
    struct sockaddr_storage srcaddr;
    struct sockaddr_storage peeraddr; // Don't want to expose sockaddr_any to public API
    uint16_t weight;
    SRT_SOCKOPT_CONFIG* config;
    int errorcode;
    int token;
} SRT_SOCKGROUPCONFIG;

SRT_API SRTSOCKET srt_create_group(SRT_GROUP_TYPE);
SRT_API SRTSOCKET srt_groupof(SRTSOCKET socket);
SRT_API       int srt_group_data(SRTSOCKET socketgroup, SRT_SOCKGROUPDATA* output, size_t* inoutlen);

SRT_API SRT_SOCKOPT_CONFIG* srt_create_config(void);
SRT_API void srt_delete_config(SRT_SOCKOPT_CONFIG* config /*nullable*/);
SRT_API int srt_config_add(SRT_SOCKOPT_CONFIG* config, SRT_SOCKOPT option, const void* contents, int len);

SRT_API SRT_SOCKGROUPCONFIG srt_prepare_endpoint(const struct sockaddr* src /*nullable*/, const struct sockaddr* adr, int namelen);
SRT_API       int srt_connect_group(SRTSOCKET group, SRT_SOCKGROUPCONFIG name[], int arraysize);

#ifdef __cplusplus
}
#endif

#endif
