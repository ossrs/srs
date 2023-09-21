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


#ifndef INC_SRT_CORE_H
#define INC_SRT_CORE_H

#include <deque>
#include <sstream>
#include "srt.h"
#include "common.h"
#include "list.h"
#include "buffer_snd.h"
#include "buffer_rcv.h"
#include "window.h"
#include "packet.h"
#include "channel.h"
#include "cache.h"
#include "queue.h"
#include "handshake.h"
#include "congctl.h"
#include "packetfilter.h"
#include "socketconfig.h"
#include "utilities.h"
#include "logger_defs.h"

#include "stats.h"

#include <haicrypt.h>


// TODO: Utility function - to be moved to utilities.h?
template <class T>
inline T CountIIR(T base, T newval, double factor)
{
    if ( base == 0.0 )
        return newval;

    T diff = newval - base;
    return base+T(diff*factor);
}

// TODO: Probably a better rework for that can be done - this can be
// turned into a serializable structure, just like it's done for CHandShake.
enum AckDataItem
{
    ACKD_RCVLASTACK       = 0,
    ACKD_RTT              = 1,
    ACKD_RTTVAR           = 2,
    ACKD_BUFFERLEFT       = 3,
    ACKD_TOTAL_SIZE_SMALL = 4,  // Size of the Small ACK, packet length = 16.

    // Extra fields for Full ACK.
    ACKD_RCVSPEED           = 4,
    ACKD_BANDWIDTH          = 5,
    ACKD_TOTAL_SIZE_UDTBASE = 6,  // Packet length = 24.

    // Extra stats since SRT v1.0.1.
    ACKD_RCVRATE           = 6,
    ACKD_TOTAL_SIZE_VER101 = 7,  // Packet length = 28.

    // Only in SRT v1.0.2.
    ACKD_XMRATE_VER102_ONLY     = 7,
    ACKD_TOTAL_SIZE_VER102_ONLY = 8,  // Packet length = 32.

    ACKD_TOTAL_SIZE = ACKD_TOTAL_SIZE_VER102_ONLY  // The maximum known ACK length is 32 bytes.
};
const size_t ACKD_FIELD_SIZE = sizeof(int32_t);

static const size_t SRT_SOCKOPT_NPOST = 12;
extern const SRT_SOCKOPT srt_post_opt_list [];

enum GroupDataItem
{
    GRPD_GROUPID,
    GRPD_GROUPDATA,

    GRPD_E_SIZE
};

const size_t GRPD_MIN_SIZE = 2; // ID and GROUPTYPE as backward compat

const size_t GRPD_FIELD_SIZE = sizeof(int32_t);

// For HSv4 legacy handshake
#define SRT_MAX_HSRETRY     10          /* Maximum SRT handshake retry */

enum SeqPairItems
{
    SEQ_BEGIN = 0, SEQ_END = 1, SEQ_SIZE = 2
};


// Extended SRT Congestion control class - only an incomplete definition required
class CCryptoControl;

namespace srt {
class CUDTUnited;
class CUDTSocket;
#if ENABLE_BONDING
class CUDTGroup;
#endif

// XXX REFACTOR: The 'CUDT' class is to be merged with 'CUDTSocket'.
// There's no reason for separating them, there's no case of having them
// anyhow managed separately. After this is done, with a small help with
// separating the internal abnormal path management (exceptions) from the
// API (return values), through CUDTUnited, this class may become in future
// an officially exposed C++ API.
class CUDT
{
    friend class CUDTSocket;
    friend class CUDTUnited;
    friend class CCC;
    friend struct CUDTComp;
    friend class CCache<CInfoBlock>;
    friend class CRendezvousQueue;
    friend class CSndQueue;
    friend class CRcvQueue;
    friend class CSndUList;
    friend class CRcvUList;
    friend class PacketFilter;
    friend class CUDTGroup;
    friend class TestMockCUDT; // unit tests

    typedef sync::steady_clock::time_point time_point;
    typedef sync::steady_clock::duration duration;
    typedef sync::AtomicClock<sync::steady_clock> atomic_time_point;
    typedef sync::AtomicDuration<sync::steady_clock> atomic_duration;

private: // constructor and desctructor
    void construct();
    void clearData();
    CUDT(CUDTSocket* parent);
    CUDT(CUDTSocket* parent, const CUDT& ancestor);
    const CUDT& operator=(const CUDT&) {return *this;} // = delete ?
    ~CUDT();

public: //API
    static int startup();
    static int cleanup();
    static SRTSOCKET socket();
#if ENABLE_BONDING
    static SRTSOCKET createGroup(SRT_GROUP_TYPE);
    static SRTSOCKET getGroupOfSocket(SRTSOCKET socket);
    static int getGroupData(SRTSOCKET groupid, SRT_SOCKGROUPDATA* pdata, size_t* psize);
    static bool isgroup(SRTSOCKET sock) { return (sock & SRTGROUP_MASK) != 0; }
#endif
    static int bind(SRTSOCKET u, const sockaddr* name, int namelen);
    static int bind(SRTSOCKET u, UDPSOCKET udpsock);
    static int listen(SRTSOCKET u, int backlog);
    static SRTSOCKET accept(SRTSOCKET u, sockaddr* addr, int* addrlen);
    static SRTSOCKET accept_bond(const SRTSOCKET listeners [], int lsize, int64_t msTimeOut);
    static int connect(SRTSOCKET u, const sockaddr* name, int namelen, int32_t forced_isn);
    static int connect(SRTSOCKET u, const sockaddr* name, const sockaddr* tname, int namelen);
#if ENABLE_BONDING
    static int connectLinks(SRTSOCKET grp, SRT_SOCKGROUPCONFIG links [], int arraysize);
#endif
    static int close(SRTSOCKET u);
    static int getpeername(SRTSOCKET u, sockaddr* name, int* namelen);
    static int getsockname(SRTSOCKET u, sockaddr* name, int* namelen);
    static int getsockopt(SRTSOCKET u, int level, SRT_SOCKOPT optname, void* optval, int* optlen);
    static int setsockopt(SRTSOCKET u, int level, SRT_SOCKOPT optname, const void* optval, int optlen);
    static int send(SRTSOCKET u, const char* buf, int len, int flags);
    static int recv(SRTSOCKET u, char* buf, int len, int flags);
    static int sendmsg(SRTSOCKET u, const char* buf, int len, int ttl = SRT_MSGTTL_INF, bool inorder = false, int64_t srctime = 0);
    static int recvmsg(SRTSOCKET u, char* buf, int len, int64_t& srctime);
    static int sendmsg2(SRTSOCKET u, const char* buf, int len, SRT_MSGCTRL& mctrl);
    static int recvmsg2(SRTSOCKET u, char* buf, int len, SRT_MSGCTRL& w_mctrl);
    static int64_t sendfile(SRTSOCKET u, std::fstream& ifs, int64_t& offset, int64_t size, int block = SRT_DEFAULT_SENDFILE_BLOCK);
    static int64_t recvfile(SRTSOCKET u, std::fstream& ofs, int64_t& offset, int64_t size, int block = SRT_DEFAULT_RECVFILE_BLOCK);
    static int select(int nfds, UDT::UDSET* readfds, UDT::UDSET* writefds, UDT::UDSET* exceptfds, const timeval* timeout);
    static int selectEx(const std::vector<SRTSOCKET>& fds, std::vector<SRTSOCKET>* readfds, std::vector<SRTSOCKET>* writefds, std::vector<SRTSOCKET>* exceptfds, int64_t msTimeOut);
    static int epoll_create();
    static int epoll_clear_usocks(int eid);
    static int epoll_add_usock(const int eid, const SRTSOCKET u, const int* events = NULL);
    static int epoll_add_ssock(const int eid, const SYSSOCKET s, const int* events = NULL);
    static int epoll_remove_usock(const int eid, const SRTSOCKET u);
    static int epoll_remove_ssock(const int eid, const SYSSOCKET s);
    static int epoll_update_usock(const int eid, const SRTSOCKET u, const int* events = NULL);
    static int epoll_update_ssock(const int eid, const SYSSOCKET s, const int* events = NULL);
    static int epoll_wait(const int eid, std::set<SRTSOCKET>* readfds, std::set<SRTSOCKET>* writefds,
            int64_t msTimeOut, std::set<SYSSOCKET>* lrfds = NULL, std::set<SYSSOCKET>* wrfds = NULL);
    static int epoll_uwait(const int eid, SRT_EPOLL_EVENT* fdsSet, int fdsSize, int64_t msTimeOut);
    static int32_t epoll_set(const int eid, int32_t flags);
    static int epoll_release(const int eid);
    static CUDTException& getlasterror();
    static int bstats(SRTSOCKET u, CBytePerfMon* perf, bool clear = true, bool instantaneous = false);
#if ENABLE_BONDING
    static int groupsockbstats(SRTSOCKET u, CBytePerfMon* perf, bool clear = true);
#endif
    static SRT_SOCKSTATUS getsockstate(SRTSOCKET u);
    static bool setstreamid(SRTSOCKET u, const std::string& sid);
    static std::string getstreamid(SRTSOCKET u);
    static int getsndbuffer(SRTSOCKET u, size_t* blocks, size_t* bytes);
    static int rejectReason(SRTSOCKET s);
    static int rejectReason(SRTSOCKET s, int value);
    static int64_t socketStartTime(SRTSOCKET s);

public: // internal API
    // This is public so that it can be used directly in API implementation functions.
    struct APIError
    {
        APIError(const CUDTException&);
        APIError(CodeMajor, CodeMinor, int = 0);

        operator int() const
        {
            return SRT_ERROR;
        }
    };

    static const       SRTSOCKET INVALID_SOCK   = -1;           // Invalid socket descriptor
    static const int   ERROR                    = -1;           // Socket api error returned value

    static const int HS_VERSION_UDT4 = 4;
    static const int HS_VERSION_SRT1 = 5;

    // Parameters
    //
    // NOTE: Use notation with X*1000*1000*... instead of
    // million zeros in a row.
    static const int       COMM_RESPONSE_MAX_EXP                 = 16;
    static const int       SRT_TLPKTDROP_MINTHRESHOLD_MS         = 1000;
    static const uint64_t  COMM_KEEPALIVE_PERIOD_US              = 1*1000*1000;
    static const int32_t   COMM_SYN_INTERVAL_US                  = 10*1000;
    static const int       COMM_CLOSE_BROKEN_LISTENER_TIMEOUT_MS = 3000;
    static const uint16_t  MAX_WEIGHT                            = 32767;
    static const size_t    ACK_WND_SIZE                          = 1024;
    static const int       INITIAL_RTT                           = 10 * COMM_SYN_INTERVAL_US;
    static const int       INITIAL_RTTVAR                        = INITIAL_RTT / 2;

    int handshakeVersion()
    {
        return m_ConnRes.m_iVersion;
    }

    std::string CONID() const
    {
#if ENABLE_LOGGING
        std::ostringstream os;
        os << "@" << m_SocketID << ": ";
        return os.str();
#else
        return "";
#endif
    }

    SRTSOCKET socketID() const { return m_SocketID; }

    static CUDT*                    getUDTHandle(SRTSOCKET u);
    static std::vector<SRTSOCKET>   existingSockets();

    void addressAndSend(CPacket& pkt);

    SRT_ATTR_REQUIRES(m_ConnectionLock)
    void sendSrtMsg(int cmd, uint32_t *srtdata_in = NULL, size_t srtlen_in = 0);

    bool        isOPT_TsbPd()                   const { return m_config.bTSBPD; }
    int         SRTT()                          const { return m_iSRTT; }
    int         RTTVar()                        const { return m_iRTTVar; }
    int32_t     sndSeqNo()                      const { return m_iSndCurrSeqNo; }
    int32_t     schedSeqNo()                    const { return m_iSndNextSeqNo; }
    bool        overrideSndSeqNo(int32_t seq);

#if ENABLE_BONDING
    sync::steady_clock::time_point   lastRspTime()          const { return m_tsLastRspTime.load(); }
    sync::steady_clock::time_point   freshActivationStart() const { return m_tsFreshActivation; }
#endif

    int32_t     rcvSeqNo()          const { return m_iRcvCurrSeqNo; }
    int         flowWindowSize()    const { return m_iFlowWindowSize; }
    int32_t     deliveryRate()      const { return m_iDeliveryRate; }
    int         bandwidth()         const { return m_iBandwidth; }
    int64_t     maxBandwidth()      const { return m_config.llMaxBW; }
    int         MSS()               const { return m_config.iMSS; }

    uint32_t        peerLatency_us()        const { return m_iPeerTsbPdDelay_ms * 1000; }
    int             peerIdleTimeout_ms()    const { return m_config.iPeerIdleTimeout_ms; }
    size_t          maxPayloadSize()        const { return m_iMaxSRTPayloadSize; }
    size_t          OPT_PayloadSize()       const { return m_config.zExpPayloadSize; }
    int             sndLossLength()               { return m_pSndLossList->getLossLength(); }
    int32_t         ISN()                   const { return m_iISN; }
    int32_t         peerISN()               const { return m_iPeerISN; }
    duration        minNAKInterval()        const { return m_tdMinNakInterval; }
    sockaddr_any    peerAddr()              const { return m_PeerAddr; }

    /// Returns the number of packets in flight (sent, but not yet acknowledged).
    /// @param lastack is the sequence number of the first unacknowledged packet.
    /// @param curseq is the sequence number of the latest original packet sent
    ///
    /// @note When there are no packets in flight, lastack = incseq(curseq).
    ///
    /// @returns The number of packets in flight belonging to the interval [0; ...)
    static int32_t getFlightSpan(int32_t lastack, int32_t curseq)
    {
        // Packets sent:
        // | 1 | 2 | 3 | 4 | 5 |
        //   ^               ^
        //   |               |
        // lastack           |
        //                curseq
        //
        // In Flight: [lastack; curseq]
        //
        // Normally 'lastack' should be PAST the 'curseq',
        // however in a case when the sending stopped and all packets were
        // ACKed, the 'lastack' is one sequence ahead of 'curseq'.
        // Therefore we increase 'curseq' by 1 forward and then
        // get the distance towards the last ACK. This way this value may
        // be only positive as seqlen() includes endpoints.
        // Finally, we subtract 1 to exclude the increment added earlier.

        return CSeqNo::seqlen(lastack, CSeqNo::incseq(curseq)) - 1;
    }

    /// Returns the number of packets in flight (sent, but not yet acknowledged).
    /// @returns The number of packets in flight belonging to the interval [0; ...)
    int32_t getFlightSpan() const
    {
        return getFlightSpan(m_iSndLastAck, m_iSndCurrSeqNo);
    }

    int minSndSize(int len = 0) const
    {
        const int ps = (int) maxPayloadSize();
        if (len == 0) // weird, can't use non-static data member as default argument!
            len = ps;
        return m_config.bMessageAPI ? (len+ps-1)/ps : 1;
    }

    static int32_t makeTS(const time_point& from_time, const time_point& tsStartTime)
    {
        // NOTE:
        // - This calculates first the time difference towards start time.
        // - This difference value is also CUT OFF THE SEGMENT information
        //   (a multiple of MAX_TIMESTAMP+1)
        // So, this can be simply defined as: TS = (RTS - STS) % (MAX_TIMESTAMP+1)
        SRT_ASSERT(from_time >= tsStartTime);
        return (int32_t) sync::count_microseconds(from_time - tsStartTime);
    }

    /// @brief Set the timestamp field of the packet using the provided value (no check)
    /// @param p the packet structure to set the timestamp on.
    /// @param ts timestamp to use as a source for packet timestamp.
    SRT_ATTR_EXCLUDES(m_StatsLock)
    void setPacketTS(CPacket& p, const time_point& ts);

    /// @brief Set the timestamp field of the packet according the TSBPD mode.
    /// Also checks the connection start time (m_tsStartTime).
    /// @param p the packet structure to set the timestamp on.
    /// @param ts timestamp to use as a source for packet timestamp. Ignored if m_bPeerTsbPd is false.
    SRT_ATTR_EXCLUDES(m_StatsLock)
    void setDataPacketTS(CPacket& p, const time_point& ts);

    // Utility used for closing a listening socket
    // immediately to free the socket
    void notListening()
    {
        sync::ScopedLock cg(m_ConnectionLock);
        m_bListening = false;
        m_pRcvQueue->removeListener(this);
    }

    static int32_t generateISN()
    {
        using namespace sync;
        return genRandomInt(0, CSeqNo::m_iMaxSeqNo);
    }

    static CUDTUnited& uglobal();                      // UDT global management base

    std::set<int>& pollset() { return m_sPollID; }

    CSrtConfig m_config;

    SRTU_PROPERTY_RO(SRTSOCKET, id, m_SocketID);
    SRTU_PROPERTY_RO(bool, isClosing, m_bClosing);
    SRTU_PROPERTY_RO(srt::CRcvBuffer*, rcvBuffer, m_pRcvBuffer);
    SRTU_PROPERTY_RO(bool, isTLPktDrop, m_bTLPktDrop);
    SRTU_PROPERTY_RO(bool, isSynReceiving, m_config.bSynRecving);
    SRTU_PROPERTY_RR(sync::Condition*, recvDataCond, &m_RecvDataCond);
    SRTU_PROPERTY_RR(sync::Condition*, recvTsbPdCond, &m_RcvTsbPdCond);

    /// @brief  Request a socket to be broken due to too long instability (normally by a group).
    void breakAsUnstable() { m_bBreakAsUnstable = true; }

    void ConnectSignal(ETransmissionEvent tev, EventSlot sl);
    void DisconnectSignal(ETransmissionEvent tev);

    // This is in public section so prospective overriding it can be
    // done by directly assigning to a field.

    typedef std::vector< std::pair<int32_t, int32_t> > loss_seqs_t;
    typedef loss_seqs_t packetArrival_cb(void*, CPacket&);
    CallbackHolder<packetArrival_cb> m_cbPacketArrival;

private:
    /// initialize a UDT entity and bind to a local address.
    void open();

    /// Start listening to any connection request.
    void setListenState();

    /// Connect to a UDT entity listening at address "peer".
    /// @param peer [in] The address of the listening UDT entity.
    void startConnect(const sockaddr_any& peer, int32_t forced_isn);

    /// Process the response handshake packet. Failure reasons can be:
    /// * Socket is not in connecting state
    /// * Response @a pkt is not a handshake control message
    /// * Rendezvous socket has once processed a regular handshake
    /// @param pkt [in] handshake packet.
    /// @retval 0 Connection successful
    /// @retval 1 Connection in progress (m_ConnReq turned into RESPONSE)
    /// @retval -1 Connection failed
    SRT_ATR_NODISCARD SRT_ATTR_REQUIRES(m_ConnectionLock)
    EConnectStatus processConnectResponse(const CPacket& pkt, CUDTException* eout) ATR_NOEXCEPT;

    // This function works in case of HSv5 rendezvous. It changes the state
    // according to the present state and received message type, as well as the
    // INITIATOR/RESPONDER side resolved through cookieContest().
    // The resulting data are:
    // - rsptype: handshake message type that should be sent back to the peer (nothing if URQ_DONE)
    // - needs_extension: the HSREQ/KMREQ or HSRSP/KMRSP extensions should be attached to the handshake message.
    // - RETURNED VALUE: if true, it means a URQ_CONCLUSION message was received with HSRSP/KMRSP extensions and needs HSRSP/KMRSP.
    void rendezvousSwitchState(UDTRequestType& rsptype, bool& needs_extension, bool& needs_hsrsp);
    void cookieContest();

    /// Interpret the incoming handshake packet in order to perform appropriate
    /// rendezvous FSM state transition if needed, and craft the response, serialized
    /// into the packet to be next sent.
    /// @param reqpkt Packet to be written with handshake data
    /// @param response incoming handshake response packet to be interpreted
    /// @param serv_addr incoming packet's address
    /// @param rst Current read status to know if the HS packet was freshly received from the peer, or this is only a periodic update (RST_AGAIN)
    SRT_ATR_NODISCARD SRT_ATTR_REQUIRES(m_ConnectionLock)
    EConnectStatus processRendezvous(const CPacket* response, const sockaddr_any& serv_addr, EReadStatus, CPacket& reqpkt);
    void sendRendezvousRejection(const sockaddr_any& serv_addr, CPacket& request);

    /// Create the CryptoControl object based on the HS packet.
    SRT_ATR_NODISCARD SRT_ATTR_REQUIRES(m_ConnectionLock)
    bool prepareConnectionObjects(const CHandShake &hs, HandshakeSide hsd, CUDTException* eout);

    /// Allocates sender and receiver buffers and loss lists.
    SRT_ATR_NODISCARD SRT_ATTR_REQUIRES(m_ConnectionLock)
    bool prepareBuffers(CUDTException* eout);

    SRT_ATR_NODISCARD SRT_ATTR_REQUIRES(m_ConnectionLock)
    EConnectStatus postConnect(const CPacket* response, bool rendezvous, CUDTException* eout) ATR_NOEXCEPT;

    SRT_ATR_NODISCARD bool applyResponseSettings(const CPacket* hspkt /*[[nullable]]*/) ATR_NOEXCEPT;
    SRT_ATR_NODISCARD EConnectStatus processAsyncConnectResponse(const CPacket& pkt) ATR_NOEXCEPT;
    SRT_ATR_NODISCARD bool processAsyncConnectRequest(EReadStatus rst, EConnectStatus cst, const CPacket* response, const sockaddr_any& serv_addr);
    SRT_ATR_NODISCARD EConnectStatus craftKmResponse(uint32_t* aw_kmdata, size_t& w_kmdatasize);

    void checkUpdateCryptoKeyLen(const char* loghdr, int32_t typefield);

    SRT_ATR_NODISCARD size_t fillSrtHandshake_HSREQ(uint32_t* srtdata, size_t srtlen, int hs_version);
    SRT_ATR_NODISCARD size_t fillSrtHandshake_HSRSP(uint32_t* srtdata, size_t srtlen, int hs_version);
    SRT_ATR_NODISCARD size_t fillSrtHandshake(uint32_t* srtdata, size_t srtlen, int msgtype, int hs_version);

    SRT_ATR_NODISCARD SRT_ATTR_REQUIRES(m_ConnectionLock)
    bool createSrtHandshake(int srths_cmd, int srtkm_cmd, const uint32_t* data, size_t datalen,
            CPacket& w_reqpkt, CHandShake& w_hs);

    SRT_ATR_NODISCARD size_t fillHsExtConfigString(uint32_t *pcmdspec, int cmd, const std::string &str);
#if ENABLE_BONDING
    SRT_ATR_NODISCARD size_t fillHsExtGroup(uint32_t *pcmdspec);
#endif
    SRT_ATR_NODISCARD SRT_ATTR_REQUIRES(m_ConnectionLock)
    size_t fillHsExtKMREQ(uint32_t *pcmdspec, size_t ki);

    SRT_ATR_NODISCARD size_t fillHsExtKMRSP(uint32_t *pcmdspec, const uint32_t *kmdata, size_t kmdata_wordsize);

    SRT_ATR_NODISCARD size_t prepareSrtHsMsg(int cmd, uint32_t* srtdata, size_t size);

    SRT_ATR_NODISCARD bool processSrtMsg(const CPacket *ctrlpkt);
    SRT_ATR_NODISCARD int processSrtMsg_HSREQ(const uint32_t* srtdata, size_t bytelen, uint32_t ts, int hsv);
    SRT_ATR_NODISCARD int processSrtMsg_HSRSP(const uint32_t* srtdata, size_t bytelen, uint32_t ts, int hsv);
    SRT_ATR_NODISCARD bool interpretSrtHandshake(const CHandShake& hs, const CPacket& hspkt, uint32_t* out_data, size_t* out_len);
    SRT_ATR_NODISCARD bool checkApplyFilterConfig(const std::string& cs);

#if ENABLE_BONDING
    static CUDTGroup& newGroup(const int); // defined EXCEPTIONALLY in api.cpp for convenience reasons
    // Note: This is an "interpret" function, which should treat the tp as
    // "possibly group type" that might be out of the existing values.
    SRT_ATR_NODISCARD bool interpretGroup(const int32_t grpdata[], size_t data_size, int hsreq_type_cmd);
    SRT_ATR_NODISCARD SRTSOCKET makeMePeerOf(SRTSOCKET peergroup, SRT_GROUP_TYPE tp, uint32_t link_flags);
    void synchronizeWithGroup(CUDTGroup* grp);
#endif

    void updateAfterSrtHandshake(int hsv);

    void updateSrtRcvSettings();
    void updateSrtSndSettings();

    void updateIdleLinkFrom(CUDT* source);

    /// @brief Drop packets too late to be delivered if any.
    /// @returns the number of packets actually dropped.
    SRT_ATTR_REQUIRES2(m_RecvAckLock, m_StatsLock)
    int sndDropTooLate();

    /// @bried Allow packet retransmission.
    /// Depending on the configuration mode (live / file), retransmission
    /// can be blocked if e.g. there are original packets pending to be sent.
    /// @return true if retransmission is allowed; false otherwise.
    bool isRetransmissionAllowed(const time_point& tnow);

    /// Connect to a UDT entity as per hs request. This will update
    /// required data in the entity, then update them also in the hs structure,
    /// and then send the response back to the caller.
    /// @param agent [in] The address to which the UDT entity is bound.
    /// @param peer [in] The address of the listening UDT entity.
    /// @param hspkt [in] The original packet that brought the handshake.
    /// @param hs [in/out] The handshake information sent by the peer side (in), negotiated value (out).
    void acceptAndRespond(const sockaddr_any& agent, const sockaddr_any& peer, const CPacket& hspkt, CHandShake& hs);

    /// Write back to the hs structure the data after they have been
    /// negotiated by acceptAndRespond.
    void rewriteHandshakeData(const sockaddr_any& peer, CHandShake& w_hs);
    bool runAcceptHook(CUDT* acore, const sockaddr* peer, const CHandShake& hs, const CPacket& hspkt);

    /// Close the opened UDT entity.

    bool closeInternal();
    void updateBrokenConnection();
    void completeBrokenConnectionDependencies(int errorcode);

    /// Request UDT to send out a data block "data" with size of "len".
    /// @param data [in] The address of the application data to be sent.
    /// @param len [in] The size of the data block.
    /// @return Actual size of data sent.

    SRT_ATR_NODISCARD int send(const char* data, int len)
    {
        return sendmsg(data, len, SRT_MSGTTL_INF, false, 0);
    }

    /// Request UDT to receive data to a memory block "data" with size of "len".
    /// @param data [out] data received.
    /// @param len [in] The desired size of data to be received.
    /// @return Actual size of data received.

    SRT_ATR_NODISCARD int recv(char* data, int len);

    /// send a message of a memory block "data" with size of "len".
    /// @param data [out] data received.
    /// @param len [in] The desired size of data to be received.
    /// @param ttl [in] the time-to-live of the message.
    /// @param inorder [in] if the message should be delivered in order.
    /// @param srctime [in] Time when the data were ready to send.
    /// @return Actual size of data sent.

    SRT_ATR_NODISCARD int sendmsg(const char* data, int len, int ttl, bool inorder, int64_t srctime);
    /// Receive a message to buffer "data".
    /// @param data [out] data received.
    /// @param len [in] size of the buffer.
    /// @return Actual size of data received.

    SRT_ATR_NODISCARD int sendmsg2(const char* data, int len, SRT_MSGCTRL& w_m);

    SRT_ATR_NODISCARD int recvmsg(char* data, int len, int64_t& srctime);
    SRT_ATR_NODISCARD int recvmsg2(char* data, int len, SRT_MSGCTRL& w_m);
    SRT_ATR_NODISCARD int receiveMessage(char* data, int len, SRT_MSGCTRL& w_m, int erh = 1 /*throw exception*/);
    SRT_ATR_NODISCARD int receiveBuffer(char* data, int len);

    size_t dropMessage(int32_t seqtoskip);

    /// Request UDT to send out a file described as "fd", starting from "offset", with size of "size".
    /// @param ifs [in] The input file stream.
    /// @param offset [in, out] From where to read and send data; output is the new offset when the call returns.
    /// @param size [in] How many data to be sent.
    /// @param block [in] size of block per read from disk
    /// @return Actual size of data sent.

    SRT_ATR_NODISCARD int64_t sendfile(std::fstream& ifs, int64_t& offset, int64_t size, int block = 366000);

    /// Request UDT to receive data into a file described as "fd", starting from "offset", with expected size of "size".
    /// @param ofs [out] The output file stream.
    /// @param offset [in, out] From where to write data; output is the new offset when the call returns.
    /// @param size [in] How many data to be received.
    /// @param block [in] size of block per write to disk
    /// @return Actual size of data received.

    SRT_ATR_NODISCARD int64_t recvfile(std::fstream& ofs, int64_t& offset, int64_t size, int block = 7320000);

    /// Configure UDT options.
    /// @param optName [in] The enum name of a UDT option.
    /// @param optval [in] The value to be set.
    /// @param optlen [in] size of "optval".

    void setOpt(SRT_SOCKOPT optName, const void* optval, int optlen);

    /// Read UDT options.
    /// @param optName [in] The enum name of a UDT option.
    /// @param optval [in] The value to be returned.
    /// @param optlen [out] size of "optval".

    void getOpt(SRT_SOCKOPT optName, void* optval, int& w_optlen);

#if ENABLE_BONDING
    /// Applies the configuration set on the socket.
    /// Any errors in this process are reported by exception.
    SRT_ERRNO applyMemberConfigObject(const SRT_SocketOptionObject& opt);
#endif

    /// read the performance data with bytes counters since bstats() 
    ///  
    /// @param perf [in, out] pointer to a CPerfMon structure to record the performance data.
    /// @param clear [in] flag to decide if the local performance trace should be cleared. 
    /// @param instantaneous [in] flag to request instantaneous data 
    /// instead of moving averages.
    void bstats(CBytePerfMon* perf, bool clear = true, bool instantaneous = false);

    /// Mark sequence contained in the given packet as not lost. This
    /// removes the loss record from both current receiver loss list and
    /// the receiver fresh loss list.
    void unlose(const CPacket& oldpacket);
    void dropFromLossLists(int32_t from, int32_t to);
    bool getFirstNoncontSequence(int32_t& w_seq, std::string& w_log_reason);

    SRT_ATTR_EXCLUDES(m_ConnectionLock)
    void checkSndTimers();
    
    /// @brief Check and perform KM refresh if needed.
    void checkSndKMRefresh();

    void handshakeDone()
    {
        m_iSndHsRetryCnt = 0;
    }

    int64_t withOverhead(int64_t basebw)
    {
        return (basebw * (100 + m_config.iOverheadBW))/100;
    }

    static double Bps2Mbps(int64_t basebw)
    {
        return double(basebw) * 8.0/1000000.0;
    }

    bool stillConnected()
    {
        // Still connected is when:
        // - no "broken" condition appeared (security, protocol error, response timeout)
        return !m_bBroken
            // - still connected (no one called srt_close())
            && m_bConnected
            // - isn't currently closing (srt_close() called, response timeout, shutdown)
            && !m_bClosing;
    }

    int sndSpaceLeft()
    {
        return static_cast<int>(sndBuffersLeft() * maxPayloadSize());
    }

    int sndBuffersLeft()
    {
        return m_config.iSndBufSize - m_pSndBuffer->getCurrBufSize();
    }

    time_point socketStartTime()
    {
        return m_stats.tsStartTime;
    }

    SRT_ATTR_EXCLUDES(m_RcvBufferLock)
    bool isRcvBufferReady() const;

    SRT_ATTR_REQUIRES(m_RcvBufferLock)
    bool isRcvBufferReadyNoLock() const;

    // TSBPD thread main function.
    static void* tsbpd(void* param);

    /// Drop too late packets (receiver side). Update loss lists and ACK positions.
    /// The @a seqno packet itself is not dropped.
    /// @param seqno [in] The sequence number of the first packets following those to be dropped.
    /// @return The number of packets dropped.
    int rcvDropTooLateUpTo(int seqno);

    static loss_seqs_t defaultPacketArrival(void* vself, CPacket& pkt);
    static loss_seqs_t groupPacketArrival(void* vself, CPacket& pkt);

    CRateEstimator getRateEstimator() const
    {
        if (!m_pSndBuffer)
            return CRateEstimator();
        return m_pSndBuffer->getRateEstimator();
    }

    void setRateEstimator(const CRateEstimator& rate)
    {
        if (!m_pSndBuffer)
            return;

        m_pSndBuffer->setRateEstimator(rate);
        updateCC(TEV_SYNC, EventVariant(0));
    }


private: // Identification
    CUDTSocket* const   m_parent;                       // Temporary, until the CUDTSocket class is merged with CUDT
    SRTSOCKET           m_SocketID;                     // UDT socket number
    SRTSOCKET           m_PeerID;                       // Peer ID, for multiplexer

    // HSv4 (legacy handshake) support)
    time_point  m_tsSndHsLastTime;                      // Last SRT handshake request time
    int         m_iSndHsRetryCnt;                       // SRT handshake retries left

#if ENABLE_BONDING
    SRT_GROUP_TYPE m_HSGroupType;   // Group type about-to-be-set in the handshake
#endif

private:
    int                       m_iMaxSRTPayloadSize;     // Maximum/regular payload size, in bytes
    int                       m_iTsbPdDelay_ms;         // Rx delay to absorb burst, in milliseconds
    int                       m_iPeerTsbPdDelay_ms;     // Tx delay that the peer uses to absorb burst, in milliseconds
    bool                      m_bTLPktDrop;             // Enable Too-late Packet Drop
    SRT_ATTR_PT_GUARDED_BY(m_ConnectionLock)
    UniquePtr<CCryptoControl> m_pCryptoControl;         // Crypto control module
    CCache<CInfoBlock>*       m_pCache;                 // Network information cache

    // Congestion control
    std::vector<EventSlot> m_Slots[TEV_E_SIZE];
    SrtCongestion          m_CongCtl;

    // Packet filtering
    PacketFilter m_PacketFilter;
    SRT_ARQLevel m_PktFilterRexmitLevel;
    std::string  m_sPeerPktFilterConfigString;

    // Attached tool function
    void EmitSignal(ETransmissionEvent tev, EventVariant var);

    // Internal state
    sync::atomic<bool> m_bListening;             // If the UDT entity is listening to connection
    sync::atomic<bool> m_bConnecting;            // The short phase when connect() is called but not yet completed
    sync::atomic<bool> m_bConnected;             // Whether the connection is on or off
    sync::atomic<bool> m_bClosing;               // If the UDT entity is closing
    sync::atomic<bool> m_bShutdown;              // If the peer side has shutdown the connection
    sync::atomic<bool> m_bBroken;                // If the connection has been broken
    sync::atomic<bool> m_bBreakAsUnstable;       // A flag indicating that the socket should become broken because it has been unstable for too long.
    sync::atomic<bool> m_bPeerHealth;            // If the peer status is normal
    sync::atomic<int> m_RejectReason;
    bool m_bOpened;                              // If the UDT entity has been opened
                                                 // A counter (number of GC checks happening every 1s) to let the GC tag this socket as closed.   
    sync::atomic<int> m_iBrokenCounter;          // If a broken socket still has data in the receiver buffer, it is not marked closed until the counter is 0.

    int m_iEXPCount;                             // Expiration counter
    sync::atomic<int> m_iBandwidth;              // Estimated bandwidth, number of packets per second
    sync::atomic<int> m_iSRTT;                   // Smoothed RTT (an exponentially-weighted moving average (EWMA)
                                                 // of an endpoint's RTT samples), in microseconds
    sync::atomic<int> m_iRTTVar;                 // The variation in the RTT samples (RTT variance), in microseconds
    sync::atomic<bool> m_bIsFirstRTTReceived;    // True if the first RTT sample was obtained from the ACK/ACKACK pair
                                                 // at the receiver side or received by the sender from an ACK packet.
                                                 // It's used to reset the initial value of smoothed RTT (m_iSRTT)
                                                 // at the beginning of transmission (including the one taken from
                                                 // cache). False by default.
    sync::atomic<int> m_iDeliveryRate;           // Packet arrival rate at the receiver side
    sync::atomic<int> m_iByteDeliveryRate;       // Byte arrival rate at the receiver side

    CHandShake m_ConnReq;                        // Connection request
    CHandShake m_ConnRes;                        // Connection response
    CHandShake::RendezvousState m_RdvState;      // HSv5 rendezvous state
    HandshakeSide m_SrtHsSide;                   // HSv5 rendezvous handshake side resolved from cookie contest (DRAW if not yet resolved)

private: // Sending related data
    CSndBuffer* m_pSndBuffer;                    // Sender buffer
    CSndLossList* m_pSndLossList;                // Sender loss list
    CPktTimeWindow<16, 16> m_SndTimeWindow;      // Packet sending time window
#ifdef ENABLE_MAXREXMITBW
    CSndRateEstimator      m_SndRexmitRate;      // Retransmission rate estimation.
#endif

    atomic_duration m_tdSendInterval;            // Inter-packet time, in CPU clock cycles

    atomic_duration m_tdSendTimeDiff;            // Aggregate difference in inter-packet sending time

    SRT_ATTR_GUARDED_BY(m_RecvAckLock)
    sync::atomic<int> m_iFlowWindowSize;         // Flow control window size
    double m_dCongestionWindow;                  // Congestion window size

private: // Timers
    atomic_time_point m_tsNextACKTime;           // Next ACK time, in CPU clock cycles, same below
    atomic_time_point m_tsNextNAKTime;           // Next NAK time

    duration   m_tdACKInterval;                  // ACK interval
    duration   m_tdNAKInterval;                  // NAK interval

    SRT_ATTR_GUARDED_BY(m_RecvAckLock)
    atomic_time_point m_tsLastRspTime;           // Timestamp of last response from the peer
    time_point m_tsLastRspAckTime;               // (SND) Timestamp of last ACK from the peer
    atomic_time_point m_tsLastSndTime;           // Timestamp of last data/ctrl sent (in system ticks)
    time_point m_tsLastWarningTime;              // Last time that a warning message is sent
    atomic_time_point m_tsLastReqTime;           // last time when a connection request is sent
    time_point m_tsRcvPeerStartTime;
    time_point m_tsLingerExpiration;             // Linger expiration time (for GC to close a socket with data in sending buffer)
    time_point m_tsLastAckTime;                  // (RCV) Timestamp of last ACK
    duration m_tdMinNakInterval;                 // NAK timeout lower bound; too small value can cause unnecessary retransmission
    duration m_tdMinExpInterval;                 // Timeout lower bound threshold: too small timeout can cause problem

    int m_iPktCount;                             // Packet counter for ACK
    int m_iLightACKCount;                        // Light ACK counter

    time_point m_tsNextSendTime;                 // Scheduled time of next packet sending

    sync::atomic<int32_t> m_iSndLastFullAck;     // Last full ACK received
    SRT_ATTR_GUARDED_BY(m_RecvAckLock)
    sync::atomic<int32_t> m_iSndLastAck;         // Last ACK received

    // NOTE: m_iSndLastDataAck is the value strictly bound to the CSndBufer object (m_pSndBuffer)
    // and this is the sequence number that refers to the block at position [0]. Upon acknowledgement,
    // this value is shifted to the acknowledged position, and the blocks are removed from the
    // m_pSndBuffer buffer up to excluding this sequence number.
    // XXX CONSIDER removing this field and giving up the maintenance of this sequence number
    // to the sending buffer. This way, extraction of an old packet for retransmission should
    // require only the lost sequence number, and how to find the packet with this sequence
    // will be up to the sending buffer.
    sync::atomic<int32_t> m_iSndLastDataAck;     // The real last ACK that updates the sender buffer and loss list
    sync::atomic<int32_t> m_iSndCurrSeqNo;       // The largest sequence number that HAS BEEN SENT
    sync::atomic<int32_t> m_iSndNextSeqNo;       // The sequence number predicted to be placed at the currently scheduled packet

    // Note important differences between Curr and Next fields:
    // - m_iSndCurrSeqNo: this is used by SRT:SndQ:worker thread and it's operated from CUDT::packData
    //   function only. This value represents the sequence number that has been stamped on a packet directly
    //   before it is sent over the network.
    // - m_iSndNextSeqNo: this is used by the user's thread and it's operated from CUDT::sendmsg2
    //   function only. This value represents the sequence number that is PREDICTED to be stamped on the
    //   first block out of the block series that will be scheduled for later sending over the network
    //   out of the data passed in this function. For a special case when the length of the data is
    //   short enough to be passed in one UDP packet (always the case for live mode), this value is
    //   always increased by one in this call, otherwise it will be increased by the number of blocks
    //   scheduled for sending.

    int32_t m_iSndLastAck2;                      // Last ACK2 sent back
    time_point m_SndLastAck2Time;                // The time when last ACK2 was sent back
    void setInitialSndSeq(int32_t isn)
    {
        m_iSndLastAck = isn;
        m_iSndLastDataAck = isn;
        m_iSndLastFullAck = isn;
        m_iSndCurrSeqNo = CSeqNo::decseq(isn);
        m_iSndNextSeqNo = isn;
        m_iSndLastAck2 = isn;
    }

    void setInitialRcvSeq(int32_t isn);

    int32_t m_iISN;                              // Initial Sequence Number
    bool m_bPeerTsbPd;                           // Peer accept TimeStamp-Based Rx mode
    bool m_bPeerTLPktDrop;                       // Enable sender late packet dropping
    bool m_bPeerNakReport;                       // Sender's peer (receiver) issues Periodic NAK Reports
    bool m_bPeerRexmitFlag;                      // Receiver supports rexmit flag in payload packets

    SRT_ATTR_GUARDED_BY(m_RecvAckLock)
    int32_t m_iReXmitCount;                      // Re-Transmit Count since last ACK

    time_point m_tsLogSlowDown;                  // The last time a log message from the "slow down" group was shown.
                                                 // The "slow down" group of logs are those that can be printed too often otherwise, but can't be turned off (warnings and errors).
                                                 // Currently only used by decryption failure message, therefore no mutex protection needed.

    /// @brief Check if a frequent log can be shown.
    /// @param tnow current time
    /// @return true if it is ok to print a frequent log message.
    bool frequentLogAllowed(const time_point& tnow) const;

private: // Receiving related data
    CRcvBuffer* m_pRcvBuffer;                    //< Receiver buffer
    SRT_ATTR_GUARDED_BY(m_RcvLossLock)
    CRcvLossList* m_pRcvLossList;                //< Receiver loss list
    SRT_ATTR_GUARDED_BY(m_RcvLossLock)
    std::deque<CRcvFreshLoss> m_FreshLoss;       //< Lost sequence already added to m_pRcvLossList, but not yet sent UMSG_LOSSREPORT for.

    int m_iReorderTolerance;                     //< Current value of dynamic reorder tolerance
    int m_iConsecEarlyDelivery;                  //< Increases with every OOO packet that came <TTL-2 time, resets with every increased reorder tolerance
    int m_iConsecOrderedDelivery;                //< Increases with every packet coming in order or retransmitted, resets with every out-of-order packet

    CACKWindow<ACK_WND_SIZE> m_ACKWindow;        // ACK history window
    CPktTimeWindow<16, 64> m_RcvTimeWindow;      // Packet arrival time window

    int32_t m_iRcvLastAck;                       // First unacknowledged packet seqno sent in the latest ACK.
#ifdef ENABLE_LOGGING
    int32_t m_iDebugPrevLastAck;
#endif
    int32_t m_iRcvLastAckAck;                    // (RCV) Latest packet seqno in a sent ACK acknowledged by ACKACK. RcvQTh (sendCtrlAck {r}, processCtrlAckAck {r}, processCtrlAck {r}, connection {w}).
    int32_t m_iAckSeqNo;                         // Last ACK sequence number
    sync::atomic<int32_t> m_iRcvCurrSeqNo;       // (RCV) Largest received sequence number. RcvQTh, TSBPDTh.
    int32_t m_iRcvCurrPhySeqNo;                  // Same as m_iRcvCurrSeqNo, but physical only (disregarding a filter)
    bool m_bBufferWasFull;                        // Indicate that RX buffer was full last time a ack was sent
    int32_t m_iPeerISN;                          // Initial Sequence Number of the peer side

    uint32_t m_uPeerSrtVersion;
    uint32_t m_uPeerSrtFlags;

    bool m_bTsbPd;                               // Peer sends TimeStamp-Based Packet Delivery Packets 
    bool m_bGroupTsbPd;                          // TSBPD should be used for GROUP RECEIVER instead

    sync::CThread m_RcvTsbPdThread;              // Rcv TsbPD Thread handle
    sync::Condition m_RcvTsbPdCond;              // TSBPD signals if reading is ready. Use together with m_RecvLock
    bool m_bTsbPdAckWakeup;                      // Signal TsbPd thread on Ack sent
    sync::Mutex m_RcvTsbPdStartupLock;           // Protects TSBPD thread creating and joining

    CallbackHolder<srt_listen_callback_fn> m_cbAcceptHook;
    CallbackHolder<srt_connect_callback_fn> m_cbConnectHook;

    // FORWARDER
public:
    static int installAcceptHook(SRTSOCKET lsn, srt_listen_callback_fn* hook, void* opaq);
    static int installConnectHook(SRTSOCKET lsn, srt_connect_callback_fn* hook, void* opaq);
private:
    void installAcceptHook(srt_listen_callback_fn* hook, void* opaq)
    {
        m_cbAcceptHook.set(opaq, hook);
    }

    void installConnectHook(srt_connect_callback_fn* hook, void* opaq)
    {
        m_cbConnectHook.set(opaq, hook);
    }


private: // synchronization: mutexes and conditions
    sync::Mutex m_ConnectionLock;                // used to synchronize connection operation

    sync::Condition m_SendBlockCond;             // used to block "send" call
    sync::Mutex m_SendBlockLock;                 // lock associated to m_SendBlockCond

    mutable sync::Mutex m_RcvBufferLock;         // Protects the state of the m_pRcvBuffer
    // Protects access to m_iSndCurrSeqNo, m_iSndLastAck
    sync::Mutex m_RecvAckLock;                   // Protects the state changes while processing incoming ACK (SRT_EPOLL_OUT)

    sync::Condition m_RecvDataCond;              // used to block "srt_recv*" when there is no data. Use together with m_RecvLock
    sync::Mutex m_RecvLock;                      // used to synchronize "srt_recv*" call, protects TSBPD drift updates (CRcvBuffer::isRcvDataReady())

    sync::Mutex m_SendLock;                      // used to synchronize "send" call
    sync::Mutex m_RcvLossLock;                   // Protects the receiver loss list (access: CRcvQueue::worker, CUDT::tsbpd)
    mutable sync::Mutex m_StatsLock;             // used to synchronize access to trace statistics

    void initSynch();
    void destroySynch();
    void releaseSynch();

private: // Common connection Congestion Control setup
    // This can fail only when it failed to create a congctl
    // which only may happen when the congctl list is extended 
    // with user-supplied congctl modules, not a case so far.
    SRT_ATR_NODISCARD
    SRT_REJECT_REASON setupCC();

    // for updateCC it's ok to discard the value. This returns false only if
    // the congctl isn't created, and this can be prevented from.
    bool updateCC(ETransmissionEvent, const EventVariant arg);

    // Failure to create the crypter means that an encrypted
    // connection should be rejected if ENFORCEDENCRYPTION is on.
    SRT_ATR_NODISCARD SRT_ATTR_REQUIRES(m_ConnectionLock)
    bool createCrypter(HandshakeSide side, bool bidi);

private: // Generation and processing of packets
    void sendCtrl(UDTMessageType pkttype, const int32_t* lparam = NULL, void* rparam = NULL, int size = 0);

    /// Forms and sends ACK packet
    /// @note Assumes @ctrlpkt already has a timestamp.
    ///
    /// @param ctrlpkt  A control packet structure to fill. It must have a timestemp already set.
    /// @param size     Sends lite ACK if size is SEND_LITE_ACK, Full ACK otherwise
    ///
    /// @returns the nmber of packets sent.
    int  sendCtrlAck(CPacket& ctrlpkt, int size);
    void sendLossReport(const std::vector< std::pair<int32_t, int32_t> >& losslist);

    void processCtrl(const CPacket& ctrlpkt);
    
    /// @brief Process incoming control ACK packet.
    /// @param ctrlpkt incoming ACK packet
    /// @param currtime current clock time
    void processCtrlAck(const CPacket& ctrlpkt, const time_point& currtime);

    /// @brief Process incoming control ACKACK packet.
    /// @param ctrlpkt incoming ACKACK packet
    /// @param tsArrival time when packet has arrived (used to calculate RTT)
    void processCtrlAckAck(const CPacket& ctrlpkt, const time_point& tsArrival);

    /// @brief Process incoming loss report (NAK) packet.
    /// @param ctrlpkt incoming NAK packet
    void processCtrlLossReport(const CPacket& ctrlpkt);

    /// @brief Process incoming handshake control packet
    /// @param ctrlpkt incoming HS packet
    void processCtrlHS(const CPacket& ctrlpkt);

    /// @brief Process incoming drop request control packet
    /// @param ctrlpkt incoming drop request packet
    void processCtrlDropReq(const CPacket& ctrlpkt);

    /// @brief Process incoming shutdown control packet
    void processCtrlShutdown();
    /// @brief Process incoming user defined control packet
    /// @param ctrlpkt incoming user defined packet
    void processCtrlUserDefined(const CPacket& ctrlpkt);

    /// @brief Update sender's loss list on an incoming acknowledgement.
    /// @param ackdata_seqno    sequence number of a data packet being acknowledged
    void updateSndLossListOnACK(int32_t ackdata_seqno);

    /// Pack a packet from a list of lost packets.
    /// @param packet [in, out] a packet structure to fill
    /// @return payload size on success, <=0 on failure
    int packLostData(CPacket &packet);

    /// Pack a unique data packet (never sent so far) in CPacket for sending.
    /// @param packet [in, out] a CPacket structure to fill.
    ///
    /// @return true if a packet has been packets; false otherwise.
    bool packUniqueData(CPacket& packet);

    /// Pack in CPacket the next data to be send.
    ///
    /// @param packet [out] a CPacket structure to fill
    /// @param nexttime [out] Time when this socket should be next time picked up for processing.
    /// @param src_addr [out] Source address to pass to channel's sendto
    ///
    /// @retval true A packet was extracted for sending, the socket should be rechecked at @a nexttime
    /// @retval false Nothing was extracted for sending, @a nexttime should be ignored
    bool packData(CPacket& packet, time_point& nexttime, sockaddr_any& src_addr);

    int processData(CUnit* unit);

    /// This function passes the incoming packet to the initial processing
    /// (like packet filter) and is about to store it effectively to the
    /// receiver buffer and do some postprocessing (decryption) if necessary
    /// and report the status thereof.
    ///
    /// @param incoming [in] The packet coming from the network medium
    /// @param w_new_inserted [out] Set false, if the packet already exists, otherwise true (packet added)
    /// @param w_was_sent_in_order [out] Set false, if the packet was belated, but had no R flag set.
    /// @param w_srt_loss_seqs [out] Gets inserted a loss, if this function has detected it.
    ///
    /// @return 0 The call was successful (regardless if the packet was accepted or not).
    /// @return -1 The call has failed: no space left in the buffer.
    /// @return -2 The incoming packet exceeds the expected sequence by more than a length of the buffer (irrepairable discrepancy).
    int handleSocketPacketReception(const std::vector<CUnit*>& incoming, bool& w_new_inserted, bool& w_was_sent_in_order, CUDT::loss_seqs_t& w_srt_loss_seqs);

    /// Get the packet's TSBPD time.
    /// The @a grp passed by void* is not used yet
    /// and shall not be used when ENABLE_BONDING=0.
    time_point getPktTsbPdTime(void* grp, const CPacket& packet);

    /// Checks and spawns the TSBPD thread if required.
    int checkLazySpawnTsbPdThread();
    void processClose();

    /// Process the request after receiving the handshake from caller.
    /// The @a packet param is passed here as non-const because this function
    /// will need to make a temporary back-and-forth endian swap; it doesn't intend to
    /// modify the object permanently.
    /// @param addr source address from where the request came
    /// @param packet contents of the packet
    /// @return URQ code, possibly containing reject reason
    int processConnectRequest(const sockaddr_any& addr, CPacket& packet);
    static void addLossRecord(std::vector<int32_t>& lossrecord, int32_t lo, int32_t hi);
    int32_t bake(const sockaddr_any& addr, int32_t previous_cookie = 0, int correction = 0);

#if ENABLE_BONDING
    /// @brief Drop packets in the recv buffer behind group_recv_base.
    /// Updates m_iRcvLastSkipAck if it's behind group_recv_base.
    void dropToGroupRecvBase();
#endif

    void processKeepalive(const CPacket& ctrlpkt, const time_point& tsArrival);


    /// Retrieves the available size of the receiver buffer.
    /// Expects that m_RcvBufferLock is locked.
    SRT_ATTR_REQUIRES(m_RcvBufferLock)
    size_t getAvailRcvBufferSizeNoLock() const;

private: // Trace
    struct CoreStats
    {
        time_point tsStartTime;             // timestamp when the UDT entity is started
        stats::Sender sndr;                 // sender statistics
        stats::Receiver rcvr;               // receiver statistics

        int64_t m_sndDurationTotal;         // total real time for sending

        time_point tsLastSampleTime;        // last performance sample time
        int traceReorderDistance;
        double traceBelatedTime;
        
        int64_t sndDuration;                // real time for sending
        time_point sndDurationCounter;      // timers to record the sending Duration
    } m_stats;

public:
    static const int SELF_CLOCK_INTERVAL = 64;  // ACK interval for self-clocking
    static const int SEND_LITE_ACK = sizeof(int32_t); // special size for ack containing only ack seq
    static const int PACKETPAIR_MASK = 0xF;

private: // Timers functions
#if ENABLE_BONDING
    time_point m_tsFreshActivation; // GROUPS: time of fresh activation of the link, or 0 if past the activation phase or idle
    time_point m_tsUnstableSince;   // GROUPS: time since unexpected ACK delay experienced, or 0 if link seems healthy
    time_point m_tsWarySince;       // GROUPS: time since an unstable link has first some response
#endif

    static const int BECAUSE_NO_REASON = 0, // NO BITS
                     BECAUSE_ACK       = 1 << 0,
                     BECAUSE_LITEACK   = 1 << 1,
                     BECAUSE_NAKREPORT = 1 << 2,
                     LAST_BECAUSE_BIT  =      3;

    void checkTimers();
    void considerLegacySrtHandshake(const time_point &timebase);
    int checkACKTimer (const time_point& currtime);
    int checkNAKTimer(const time_point& currtime);
    bool checkExpTimer (const time_point& currtime, int check_reason);  // returns true if the connection is expired
    void checkRexmitTimer(const time_point& currtime);


private: // for UDP multiplexer
    CSndQueue* m_pSndQueue;    // packet sending queue
    CRcvQueue* m_pRcvQueue;    // packet receiving queue
    sockaddr_any m_PeerAddr;   // peer address
    sockaddr_any m_SourceAddr; // override UDP source address with this one when sending
    uint32_t m_piSelfIP[4];    // local UDP IP address
    CSNode* m_pSNode;          // node information for UDT list used in snd queue
    CRNode* m_pRNode;          // node information for UDT list used in rcv queue

public: // For SrtCongestion
    const CSndQueue* sndQueue() { return m_pSndQueue; }
    const CRcvQueue* rcvQueue() { return m_pRcvQueue; }

private: // for epoll
    std::set<int> m_sPollID;                     // set of epoll ID to trigger
    void addEPoll(const int eid);
    void removeEPollEvents(const int eid);
    void removeEPollID(const int eid);
};

} // namespace srt

#endif
