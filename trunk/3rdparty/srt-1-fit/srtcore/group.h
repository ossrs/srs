/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2020 Haivision Systems Inc.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 */

/*****************************************************************************
Written by
   Haivision Systems Inc.
*****************************************************************************/

#ifndef INC_SRT_GROUP_H
#define INC_SRT_GROUP_H

#include "srt.h"
#include "common.h"
#include "packet.h"
#include "group_common.h"
#include "group_backup.h"

namespace srt
{

#if ENABLE_HEAVY_LOGGING
const char* const srt_log_grp_state[] = {"PENDING", "IDLE", "RUNNING", "BROKEN"};
#endif


class CUDTGroup
{
    friend class CUDTUnited;

    typedef sync::steady_clock::time_point time_point;
    typedef sync::steady_clock::duration   duration;
    typedef sync::steady_clock             steady_clock;
    typedef groups::SocketData SocketData;
    typedef groups::SendBackupCtx SendBackupCtx;
    typedef groups::BackupMemberState BackupMemberState;

public:
    typedef SRT_MEMBERSTATUS GroupState;

    // Note that the use of states may differ in particular group types:
    //
    // Broadcast: links that are freshly connected become PENDING and then IDLE only
    // for a short moment to be activated immediately at the nearest sending operation.
    //
    // Balancing: like with broadcast, just that the link activation gets its shared percentage
    // of traffic balancing
    //
    // Multicast: The link is never idle. The data are always sent over the UDP multicast link
    // and the receiver simply gets subscribed and reads packets once it's ready.
    //
    // Backup: The link stays idle until it's activated, and the activation can only happen
    // at the moment when the currently active link is "suspected of being likely broken"
    // (the current active link fails to receive ACK in a time when two ACKs should already
    // be received). After a while when the current active link is confirmed broken, it turns
    // into broken state.

    static const char* StateStr(GroupState);

    static int32_t s_tokenGen;
    static int32_t genToken() { ++s_tokenGen; if (s_tokenGen < 0) s_tokenGen = 0; return s_tokenGen;}

    struct ConfigItem
    {
        SRT_SOCKOPT                so;
        std::vector<unsigned char> value;

        template <class T>
        bool get(T& refr)
        {
            if (sizeof(T) > value.size())
                return false;
            refr = *(T*)&value[0];
            return true;
        }

        ConfigItem(SRT_SOCKOPT o, const void* val, int size)
            : so(o)
        {
            value.resize(size);
            unsigned char* begin = (unsigned char*)val;
            std::copy(begin, begin + size, value.begin());
        }

        struct OfType
        {
            SRT_SOCKOPT so;
            OfType(SRT_SOCKOPT soso)
                : so(soso)
            {
            }
            bool operator()(ConfigItem& ci) { return ci.so == so; }
        };
    };

    typedef std::list<SocketData> group_t;
    typedef group_t::iterator     gli_t;
    typedef std::vector< std::pair<SRTSOCKET, srt::CUDTSocket*> > sendable_t;

    struct Sendstate
    {
        SRTSOCKET id;
        SocketData* mb;
        int   stat;
        int   code;
    };

    CUDTGroup(SRT_GROUP_TYPE);
    ~CUDTGroup();

    SocketData* add(SocketData data);

    struct HaveID
    {
        SRTSOCKET id;
        HaveID(SRTSOCKET sid)
            : id(sid)
        {
        }
        bool operator()(const SocketData& s) { return s.id == id; }
    };

    bool contains(SRTSOCKET id, SocketData*& w_f)
    {
        srt::sync::ScopedLock g(m_GroupLock);
        gli_t f = std::find_if(m_Group.begin(), m_Group.end(), HaveID(id));
        if (f == m_Group.end())
        {
            w_f = NULL;
            return false;
        }
        w_f = &*f;
        return true;
    }

    // NEED LOCKING
    gli_t begin() { return m_Group.begin(); }
    gli_t end() { return m_Group.end(); }

    /// Remove the socket from the group container.
    /// REMEMBER: the group spec should be taken from the socket
    /// (set m_GroupOf and m_GroupMemberData to NULL
    /// PRIOR TO calling this function.
    /// @param id Socket ID to look for in the container to remove
    /// @return true if the container still contains any sockets after the operation
    bool remove(SRTSOCKET id)
    {
        using srt_logging::gmlog;
        srt::sync::ScopedLock g(m_GroupLock);

        bool empty = false;
        LOGC(gmlog.Note, log << "group/remove: removing member @" << id << " from group $" << m_GroupID);

        gli_t f = std::find_if(m_Group.begin(), m_Group.end(), HaveID(id));
        if (f != m_Group.end())
        {
            m_Group.erase(f);

            // Reset sequence numbers on a dead group so that they are
            // initialized anew with the new alive connection within
            // the group.
            // XXX The problem is that this should be done after the
            // socket is considered DISCONNECTED, not when it's being
            // closed. After being disconnected, the sequence numbers
            // are no longer valid, and will be reinitialized when the
            // socket is connected again. This may stay as is for now
            // as in SRT it's not predicted to do anything with the socket
            // that was disconnected other than immediately closing it.
            if (m_Group.empty())
            {
                // When the group is empty, there's no danger that this
                // number will collide with any ISN provided by a socket.
                // Also since now every socket will derive this ISN.
                m_iLastSchedSeqNo = generateISN();
                resetInitialRxSequence();
                empty = true;
            }
        }
        else
        {
            HLOGC(gmlog.Debug, log << "group/remove: IPE: id @" << id << " NOT FOUND");
            empty = true; // not exactly true, but this is to cause error on group in the APP
        }

        if (m_Group.empty())
        {
            m_bOpened    = false;
            m_bConnected = false;
        }

        return !empty;
    }

    bool groupEmpty()
    {
        srt::sync::ScopedLock g(m_GroupLock);
        return m_Group.empty();
    }

    void setGroupConnected();

    int            send(const char* buf, int len, SRT_MSGCTRL& w_mc);
    int            sendBroadcast(const char* buf, int len, SRT_MSGCTRL& w_mc);
    int            sendBackup(const char* buf, int len, SRT_MSGCTRL& w_mc);
    static int32_t generateISN();

private:
    // For Backup, sending all previous packet
    int sendBackupRexmit(srt::CUDT& core, SRT_MSGCTRL& w_mc);

    // Support functions for sendBackup and sendBroadcast
    /// Check if group member is idle.
    /// @param d group member
    /// @param[in,out] w_wipeme array of sockets to remove from group
    /// @param[in,out] w_pendingLinks array of sockets pending for connection
    /// @returns true if d is idle (standby), false otherwise
    bool send_CheckIdle(const gli_t d, std::vector<SRTSOCKET>& w_wipeme, std::vector<SRTSOCKET>& w_pendingLinks);


    /// This function checks if the member has just become idle (check if sender buffer is empty) to send a KEEPALIVE immidiatelly.
    /// @todo Check it is some abandoned logic.
    void sendBackup_CheckIdleTime(gli_t w_d);
    
    /// Qualify states of member links.
    /// [[using locked(this->m_GroupLock, m_pGlobal->m_GlobControlLock)]]
    /// @param[out] w_sendBackupCtx  the context will be updated with state qualifications
    /// @param[in] currtime          current timestamp
    void sendBackup_QualifyMemberStates(SendBackupCtx& w_sendBackupCtx, const steady_clock::time_point& currtime);

    void sendBackup_AssignBackupState(srt::CUDT& socket, BackupMemberState state, const steady_clock::time_point& currtime);

    /// Qualify the state of the active link: fresh, stable, unstable, wary.
    /// @retval active backup member state: fresh, stable, unstable, wary.
    BackupMemberState sendBackup_QualifyActiveState(const gli_t d, const time_point currtime);

    BackupMemberState sendBackup_QualifyIfStandBy(const gli_t d);

    /// Sends the same payload over all active members.
    /// @param[in] buf payload
    /// @param[in] len payload length in bytes
    /// @param[in,out] w_mc message control
    /// @param[in] currtime current time
    /// @param[in] currseq current packet sequence number
    /// @param[out] w_nsuccessful number of members with successfull sending.
    /// @param[in,out] maxActiveWeight
    /// @param[in,out] sendBackupCtx context
    /// @param[in,out] w_cx error
    /// @return group send result: -1 if sending over all members has failed; number of bytes sent overwise.
    int sendBackup_SendOverActive(const char* buf, int len, SRT_MSGCTRL& w_mc, const steady_clock::time_point& currtime, int32_t& w_curseq,
        size_t& w_nsuccessful, uint16_t& w_maxActiveWeight, SendBackupCtx& w_sendBackupCtx, CUDTException& w_cx);
    
    /// Check link sending status
    /// @param[in]  currtime       Current time (logging only)
    /// @param[in]  send_status    Result of sending over the socket
    /// @param[in]  lastseq        Last sent sequence number before the current sending operation
    /// @param[in]  pktseq         Packet sequence number currently tried to be sent
    /// @param[out] w_u            CUDT unit of the current member (to allow calling overrideSndSeqNo)
    /// @param[out] w_curseq       Group's current sequence number (either -1 or the value used already for other links)
    /// @param[out] w_final_stat   w_final_stat = send_status if sending succeeded.
    ///
    /// @returns true if the sending operation result (submitted in stat) is a success, false otherwise.
    bool sendBackup_CheckSendStatus(const time_point&   currtime,
                                    const int           send_status,
                                    const int32_t       lastseq,
                                    const int32_t       pktseq,
                                    CUDT&               w_u,
                                    int32_t&            w_curseq,
                                    int&                w_final_stat);
    void sendBackup_Buffering(const char* buf, const int len, int32_t& curseq, SRT_MSGCTRL& w_mc);

    size_t sendBackup_TryActivateStandbyIfNeeded(
        const char* buf,
        const int   len,
        bool& w_none_succeeded,
        SRT_MSGCTRL& w_mc,
        int32_t& w_curseq,
        int32_t& w_final_stat,
        SendBackupCtx& w_sendBackupCtx,
        CUDTException& w_cx,
        const steady_clock::time_point& currtime);

    /// Check if pending sockets are to be qualified as broken.
    /// This qualification later results in removing the socket from a group and closing it.
    /// @param[in,out]  a context with a list of member sockets, some pending might qualified broken
    void sendBackup_CheckPendingSockets(SendBackupCtx& w_sendBackupCtx, const steady_clock::time_point& currtime);

    /// Check if unstable sockets are to be qualified as broken.
    /// The main reason for such qualification is if a socket is unstable for too long.
    /// This qualification later results in removing the socket from a group and closing it.
    /// @param[in,out]  a context with a list of member sockets, some pending might qualified broken
    void sendBackup_CheckUnstableSockets(SendBackupCtx& w_sendBackupCtx, const steady_clock::time_point& currtime);

    /// @brief Marks broken sockets as closed. Used in broadcast sending.
    /// @param w_wipeme a list of sockets to close
    void send_CloseBrokenSockets(std::vector<SRTSOCKET>& w_wipeme);

    /// @brief Marks broken sockets as closed. Used in backup sending.
    /// @param w_sendBackupCtx the context with a list of broken sockets
    void sendBackup_CloseBrokenSockets(SendBackupCtx& w_sendBackupCtx);

    void sendBackup_RetryWaitBlocked(SendBackupCtx& w_sendBackupCtx,
                                     int&                      w_final_stat,
                                     bool&                     w_none_succeeded,
                                     SRT_MSGCTRL&              w_mc,
                                     CUDTException&            w_cx);
    void sendBackup_SilenceRedundantLinks(SendBackupCtx& w_sendBackupCtx, const steady_clock::time_point& currtime);

    void send_CheckValidSockets();

public:
    int recv(char* buf, int len, SRT_MSGCTRL& w_mc);

    void close();

    void setOpt(SRT_SOCKOPT optname, const void* optval, int optlen);
    void getOpt(SRT_SOCKOPT optName, void* optval, int& w_optlen);
    void deriveSettings(srt::CUDT* source);
    bool applyFlags(uint32_t flags, HandshakeSide);

    SRT_SOCKSTATUS getStatus();

    void debugMasterData(SRTSOCKET slave);

    bool isGroupReceiver()
    {
        // XXX add here also other group types, which
        // predict group receiving.
        return m_type == SRT_GTYPE_BROADCAST;
    }

    sync::Mutex* exp_groupLock() { return &m_GroupLock; }
    void         addEPoll(int eid);
    void         removeEPollEvents(const int eid);
    void         removeEPollID(const int eid);

    /// @brief Update read-ready state.
    /// @param sock member socket ID (unused)
    /// @param sequence the latest packet sequence number available for reading.
    void         updateReadState(SRTSOCKET sock, int32_t sequence);

    void         updateWriteState();
    void         updateFailedLink();
    void         activateUpdateEvent(bool still_have_items);
    int32_t      getRcvBaseSeqNo();

    /// Update the in-group array of packet providers per sequence number.
    /// Also basing on the information already provided by possibly other sockets,
    /// report the real status of packet loss, including packets maybe lost
    /// by the caller provider, but already received from elsewhere. Note that
    /// these packets are not ready for extraction until ACK-ed.
    ///
    /// @param exp_sequence The previously received sequence at this socket
    /// @param sequence The sequence of this packet
    /// @param provider The core of the socket for which the packet was dispatched
    /// @param time TSBPD time of this packet
    /// @return The bitmap that marks by 'false' packets lost since next to exp_sequence
    std::vector<bool> providePacket(int32_t exp_sequence, int32_t sequence, srt::CUDT* provider, uint64_t time);

    /// This is called from the ACK action by particular socket, which
    /// actually signs off the packet for extraction.
    ///
    /// @param core The socket core for which the ACK was sent
    /// @param ack The past-the-last-received ACK sequence number
    void readyPackets(srt::CUDT* core, int32_t ack);

    void syncWithSocket(const srt::CUDT& core, const HandshakeSide side);
    int  getGroupData(SRT_SOCKGROUPDATA* pdata, size_t* psize);
    int  getGroupData_LOCKED(SRT_SOCKGROUPDATA* pdata, size_t* psize);

    /// Predicted to be called from the reading function to fill
    /// the group data array as requested.
    void fillGroupData(SRT_MSGCTRL&       w_out, //< MSGCTRL to be written
                       const SRT_MSGCTRL& in     //< MSGCTRL read from the data-providing socket
    );

    void copyGroupData(const CUDTGroup::SocketData& source, SRT_SOCKGROUPDATA& w_target);

#if ENABLE_HEAVY_LOGGING
    void debugGroup();
#else
    void debugGroup() {}
#endif

    void ackMessage(int32_t msgno);
    void processKeepalive(SocketData*);
    void internalKeepalive(SocketData*);

private:
    // Check if there's at least one connected socket.
    // If so, grab the status of all member sockets.
    void getGroupCount(size_t& w_size, bool& w_still_alive);

    srt::CUDTUnited&  m_Global;
    srt::sync::Mutex  m_GroupLock;

    SRTSOCKET m_GroupID;
    SRTSOCKET m_PeerGroupID;
    struct GroupContainer
    {
    private:
        std::list<SocketData>  m_List;
        sync::atomic<size_t>   m_SizeCache;

        /// This field is used only by some types of groups that need
        /// to keep track as to which link was lately used. Note that
        /// by removal of a node from the m_List container, this link
        /// must be appropriately reset.
        gli_t m_LastActiveLink;

    public:

        GroupContainer()
            : m_SizeCache(0)
            , m_LastActiveLink(m_List.end())
        {
        }

        // Property<gli_t> active = { m_LastActiveLink; }
        SRTU_PROPERTY_RW(gli_t, active, m_LastActiveLink);

        gli_t        begin() { return m_List.begin(); }
        gli_t        end() { return m_List.end(); }
        bool         empty() { return m_List.empty(); }
        void         push_back(const SocketData& data) { m_List.push_back(data); ++m_SizeCache; }
        void         clear()
        {
            m_LastActiveLink = end();
            m_List.clear();
            m_SizeCache = 0;
        }
        size_t size() { return m_SizeCache; }

        void erase(gli_t it);
    };
    GroupContainer m_Group;
    SRT_GROUP_TYPE m_type;
    CUDTSocket*    m_listener; // A "group" can only have one listener.
    srt::sync::atomic<int> m_iBusy;
    CallbackHolder<srt_connect_callback_fn> m_cbConnectHook;
    void installConnectHook(srt_connect_callback_fn* hook, void* opaq)
    {
        m_cbConnectHook.set(opaq, hook);
    }

public:
    void apiAcquire() { ++m_iBusy; }
    void apiRelease() { --m_iBusy; }

    // A normal cycle of the send/recv functions is the following:
    // - [Initial API call for a group]
    // - GroupKeeper - ctor
    //    - LOCK: GlobControlLock
    //       - Find the group ID in the group container (break if not found)
    //       - LOCK: GroupLock of that group
    //           - Set BUSY flag
    //       - UNLOCK GroupLock
    //    - UNLOCK GlobControlLock
    // - [Call the sending function (sendBroadcast/sendBackup)]
    //    - LOCK GroupLock
    //       - Preparation activities
    //       - Loop over group members
    //       - Send over a single socket
    //       - Check send status and conditions
    //       - Exit, if nothing else to be done
    //       - Check links to send extra
    //           - UNLOCK GroupLock
    //               - Wait for first ready link
    //           - LOCK GroupLock
    //       - Check status and find sendable link
    //       - Send over a single socket
    //       - Check status and update data
    //    - UNLOCK GroupLock, Exit
    // - GroupKeeper - dtor
    // - LOCK GroupLock
    //    - Clear BUSY flag
    // - UNLOCK GroupLock
    // END.
    //
    // The possibility for isStillBusy to go on is only the following:
    // 1. Before calling the API function. As GlobControlLock is locked,
    //    the nearest lock on GlobControlLock by GroupKeeper can happen:
    //    - before the group is moved to ClosedGroups (this allows it to be found)
    //    - after the group is moved to ClosedGroups (this makes the group not found)
    //    - NOT after the group was deleted, as it could not be found and occupied.
    //    
    // 2. Before release of GlobControlLock (acquired by GC), but before the
    //    API function locks GroupLock:
    //    - the GC call to isStillBusy locks GroupLock, but BUSY flag is already set
    //    - GC then avoids deletion of the group
    //
    // 3. In any further place up to the exit of the API implementation function,
    // the BUSY flag is still set.
    // 
    // 4. After exit of GroupKeeper destructor and unlock of GroupLock
    //    - the group is no longer being accessed and can be freely deleted.
    //    - the group also can no longer be found by ID.

    bool isStillBusy()
    {
        sync::ScopedLock glk(m_GroupLock);
        return m_iBusy || !m_Group.empty();
    }

    struct BufferedMessageStorage
    {
        size_t             blocksize;
        size_t             maxstorage;
        std::vector<char*> storage;

        BufferedMessageStorage(size_t blk, size_t max = 0)
            : blocksize(blk)
            , maxstorage(max)
            , storage()
        {
        }

        char* get()
        {
            if (storage.empty())
                return new char[blocksize];

            // Get the element from the end
            char* block = storage.back();
            storage.pop_back();
            return block;
        }

        void put(char* block)
        {
            if (storage.size() >= maxstorage)
            {
                // Simply delete
                delete[] block;
                return;
            }

            // Put the block into the spare buffer
            storage.push_back(block);
        }

        ~BufferedMessageStorage()
        {
            for (size_t i = 0; i < storage.size(); ++i)
                delete[] storage[i];
        }
    };

    struct BufferedMessage
    {
        static BufferedMessageStorage storage;

        SRT_MSGCTRL   mc;
        mutable char* data;
        size_t        size;

        BufferedMessage()
            : data()
            , size()
        {
        }
        ~BufferedMessage()
        {
            if (data)
                storage.put(data);
        }

        // NOTE: size 's' must be checked against SRT_LIVE_MAX_PLSIZE
        // before calling
        void copy(const char* buf, size_t s)
        {
            size = s;
            data = storage.get();
            memcpy(data, buf, s);
        }

        BufferedMessage(const BufferedMessage& foreign)
            : mc(foreign.mc)
            , data(foreign.data)
            , size(foreign.size)
        {
            foreign.data = 0;
        }

        BufferedMessage& operator=(const BufferedMessage& foreign)
        {
            data = foreign.data;
            size = foreign.size;
            mc = foreign.mc;

            foreign.data = 0;
            return *this;
        }

    private:
        void swap_with(BufferedMessage& b)
        {
            std::swap(this->mc, b.mc);
            std::swap(this->data, b.data);
            std::swap(this->size, b.size);
        }
    };

    typedef std::deque<BufferedMessage> senderBuffer_t;
    // typedef StaticBuffer<BufferedMessage, 1000> senderBuffer_t;

private:
    // Fields required for SRT_GTYPE_BACKUP groups.
    senderBuffer_t        m_SenderBuffer;
    int32_t               m_iSndOldestMsgNo; // oldest position in the sender buffer
    sync::atomic<int32_t> m_iSndAckedMsgNo;
    uint32_t              m_uOPT_MinStabilityTimeout_us;

    // THIS function must be called only in a function for a group type
    // that does use sender buffer.
    int32_t addMessageToBuffer(const char* buf, size_t len, SRT_MSGCTRL& w_mc);

    std::set<int>      m_sPollID; // set of epoll ID to trigger
    int                m_iMaxPayloadSize;
    int                m_iAvgPayloadSize;
    bool               m_bSynRecving;
    bool               m_bSynSending;
    bool               m_bTsbPd;
    bool               m_bTLPktDrop;
    int64_t            m_iTsbPdDelay_us;
    int                m_RcvEID;
    class CEPollDesc*  m_RcvEpolld;
    int                m_SndEID;
    class CEPollDesc*  m_SndEpolld;

    int m_iSndTimeOut; // sending timeout in milliseconds
    int m_iRcvTimeOut; // receiving timeout in milliseconds

    // Start times for TsbPd. These times shall be synchronized
    // between all sockets in the group. The first connected one
    // defines it, others shall derive it. The value 0 decides if
    // this has been already set.
    time_point m_tsStartTime;
    time_point m_tsRcvPeerStartTime;

    void recv_CollectAliveAndBroken(std::vector<srt::CUDTSocket*>& w_alive, std::set<srt::CUDTSocket*>& w_broken);

    /// The function polls alive member sockets and retrieves a list of read-ready.
    /// [acquires lock for CUDT::uglobal()->m_GlobControlLock]
    /// [[using locked(m_GroupLock)]] temporally unlocks-locks internally
    ///
    /// @returns list of read-ready sockets
    /// @throws CUDTException(MJ_CONNECTION, MN_NOCONN, 0)
    /// @throws CUDTException(MJ_AGAIN, MN_RDAVAIL, 0)
    std::vector<srt::CUDTSocket*> recv_WaitForReadReady(const std::vector<srt::CUDTSocket*>& aliveMembers, std::set<srt::CUDTSocket*>& w_broken);

    // This is the sequence number of a packet that has been previously
    // delivered. Initially it should be set to SRT_SEQNO_NONE so that the sequence read
    // from the first delivering socket will be taken as a good deal.
    sync::atomic<int32_t> m_RcvBaseSeqNo;

    bool m_bOpened;    // Set to true when at least one link is at least pending
    bool m_bConnected; // Set to true on first link confirmed connected
    bool m_bClosing;

    // There's no simple way of transforming config
    // items that are predicted to be used on socket.
    // Use some options for yourself, store the others
    // for setting later on a socket.
    std::vector<ConfigItem> m_config;

    // Signal for the blocking user thread that the packet
    // is ready to deliver.
    sync::Condition       m_RcvDataCond;
    sync::Mutex           m_RcvDataLock;
    sync::atomic<int32_t> m_iLastSchedSeqNo; // represetnts the value of CUDT::m_iSndNextSeqNo for each running socket
    sync::atomic<int32_t> m_iLastSchedMsgNo;
    // Statistics

    struct Stats
    {
        // Stats state
        time_point tsActivateTime;   // Time when this group sent or received the first data packet
        time_point tsLastSampleTime; // Time reset when clearing stats

        stats::Metric<stats::BytesPackets> sent; // number of packets sent from the application
        stats::Metric<stats::BytesPackets> recv; // number of packets delivered from the group to the application
        stats::Metric<stats::BytesPackets> recvDrop; // number of packets dropped by the group receiver (not received from any member)
        stats::Metric<stats::BytesPackets> recvDiscard; // number of packets discarded as already delivered

        void init()
        {
            tsActivateTime = srt::sync::steady_clock::time_point();
            tsLastSampleTime = srt::sync::steady_clock::now();
            sent.reset();
            recv.reset();
            recvDrop.reset();
            recvDiscard.reset();
        }

        void reset()
        {
            tsLastSampleTime = srt::sync::steady_clock::now();

            sent.resetTrace();
            recv.resetTrace();
            recvDrop.resetTrace();
            recvDiscard.resetTrace();
        }
    } m_stats;

    void updateAvgPayloadSize(int size)
    {
        if (m_iAvgPayloadSize == -1)
            m_iAvgPayloadSize = size;
        else
            m_iAvgPayloadSize = avg_iir<4>(m_iAvgPayloadSize, size);
    }

    int avgRcvPacketSize()
    {
        // In case when no packet has been received yet, but already notified
        // a dropped packet, its size will be SRT_LIVE_DEF_PLSIZE. It will be
        // the value most matching in the typical uses, although no matter what
        // value would be used here, each one would be wrong from some points
        // of view. This one is simply the best choice for typical uses of groups
        // provided that they are to be ued only for live mode.
        return m_iAvgPayloadSize == -1 ? SRT_LIVE_DEF_PLSIZE : m_iAvgPayloadSize;
    }

public:
    void bstatsSocket(CBytePerfMon* perf, bool clear);

    // Required after the call on newGroup on the listener side.
    // On the listener side the group is lazily created just before
    // accepting a new socket and therefore always open.
    void setOpen() { m_bOpened = true; }

    std::string CONID() const
    {
#if ENABLE_LOGGING
        std::ostringstream os;
        os << "@" << m_GroupID << ":";
        return os.str();
#else
        return "";
#endif
    }

    void resetInitialRxSequence()
    {
        // The app-reader doesn't care about the real sequence number.
        // The first provided one will be taken as a good deal; even if
        // this is going to be past the ISN, at worst it will be caused
        // by TLPKTDROP.
        m_RcvBaseSeqNo = SRT_SEQNO_NONE;
    }

    bool applyGroupTime(time_point& w_start_time, time_point& w_peer_start_time)
    {
        using srt::sync::is_zero;
        using srt_logging::gmlog;

        if (is_zero(m_tsStartTime))
        {
            // The first socket, defines the group time for the whole group.
            m_tsStartTime        = w_start_time;
            m_tsRcvPeerStartTime = w_peer_start_time;
            return true;
        }

        // Sanity check. This should never happen, fix the bug if found!
        if (is_zero(m_tsRcvPeerStartTime))
        {
            LOGC(gmlog.Error, log << "IPE: only StartTime is set, RcvPeerStartTime still 0!");
            // Kinda fallback, but that's not too safe.
            m_tsRcvPeerStartTime = w_peer_start_time;
        }

        // The redundant connection, derive the times
        w_start_time      = m_tsStartTime;
        w_peer_start_time = m_tsRcvPeerStartTime;

        return false;
    }

    // Live state synchronization
    bool getBufferTimeBase(srt::CUDT* forthesakeof, time_point& w_tb, bool& w_wp, duration& w_dr);
    bool applyGroupSequences(SRTSOCKET, int32_t& w_snd_isn, int32_t& w_rcv_isn);

    /// @brief Synchronize TSBPD base time and clock drift among members using the @a srcMember as a reference.
    /// @param srcMember a reference for synchronization.
    void synchronizeDrift(const srt::CUDT* srcMember);

    void updateLatestRcv(srt::CUDTSocket*);

    // Property accessors
    SRTU_PROPERTY_RW_CHAIN(CUDTGroup, SRTSOCKET, id, m_GroupID);
    SRTU_PROPERTY_RW_CHAIN(CUDTGroup, SRTSOCKET, peerid, m_PeerGroupID);
    SRTU_PROPERTY_RW_CHAIN(CUDTGroup, SRT_GROUP_TYPE, type, m_type);
    SRTU_PROPERTY_RW_CHAIN(CUDTGroup, int32_t, currentSchedSequence, m_iLastSchedSeqNo);
    SRTU_PROPERTY_RRW(std::set<int>&, epollset, m_sPollID);
    SRTU_PROPERTY_RW_CHAIN(CUDTGroup, int64_t, latency, m_iTsbPdDelay_us);
    SRTU_PROPERTY_RO(bool, closing, m_bClosing);
};

} // namespace srt

#endif // INC_SRT_GROUP_H
