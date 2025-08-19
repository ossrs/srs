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
Copyright (c) 2001 - 2010, The Board of Trustees of the University of Illinois.
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
    Yunhong Gu, last updated 09/28/2010
modified by
    Haivision Systems Inc.
*****************************************************************************/

#ifndef INC_SRT_API_H
#define INC_SRT_API_H

#include <map>
#include <vector>
#include <string>
#include "netinet_any.h"
#include "udt.h"
#include "packet.h"
#include "queue.h"
#include "cache.h"
#include "epoll.h"
#include "handshake.h"
#include "core.h"
#if ENABLE_BONDING
#include "group.h"
#endif

// Please refer to structure and locking information provided in the
// docs/dev/low-level-info.md document.

namespace srt
{

class CUDT;

/// @brief Class CUDTSocket is a control layer on top of the CUDT core functionality layer.
/// CUDTSocket owns CUDT.
class CUDTSocket
{
public:
    CUDTSocket()
        : m_Status(SRTS_INIT)
        , m_SocketID(0)
        , m_ListenSocket(0)
        , m_PeerID(0)
#if ENABLE_BONDING
        , m_GroupMemberData()
        , m_GroupOf()
#endif
        , m_iISN(0)
        , m_UDT(this)
        , m_AcceptCond()
        , m_AcceptLock()
        , m_uiBackLog(0)
        , m_iMuxID(-1)
    {
        construct();
    }

    CUDTSocket(const CUDTSocket& ancestor)
        : m_Status(SRTS_INIT)
        , m_SocketID(0)
        , m_ListenSocket(0)
        , m_PeerID(0)
#if ENABLE_BONDING
        , m_GroupMemberData()
        , m_GroupOf()
#endif
        , m_iISN(0)
        , m_UDT(this, ancestor.m_UDT)
        , m_AcceptCond()
        , m_AcceptLock()
        , m_uiBackLog(0)
        , m_iMuxID(-1)
    {
        construct();
    }

    ~CUDTSocket();

    void construct();

    SRT_ATTR_GUARDED_BY(m_ControlLock)
    sync::atomic<SRT_SOCKSTATUS> m_Status; //< current socket state

    /// Time when the socket is closed.
    /// When the socket is closed, it is not removed immediately from the list
    /// of sockets in order to prevent other methods from accessing invalid address.
    /// A timer is started and the socket will be removed after approximately
    /// 1 second (see CUDTUnited::checkBrokenSockets()).
    sync::steady_clock::time_point m_tsClosureTimeStamp;

    sockaddr_any m_SelfAddr; //< local address of the socket
    sockaddr_any m_PeerAddr; //< peer address of the socket

    SRTSOCKET m_SocketID;     //< socket ID
    SRTSOCKET m_ListenSocket; //< ID of the listener socket; 0 means this is an independent socket

    SRTSOCKET m_PeerID; //< peer socket ID
#if ENABLE_BONDING
    groups::SocketData* m_GroupMemberData; //< Pointer to group member data, or NULL if not a group member
    CUDTGroup*          m_GroupOf;         //< Group this socket is a member of, or NULL if it isn't
#endif

    int32_t m_iISN; //< initial sequence number, used to tell different connection from same IP:port

private:
    CUDT m_UDT; //< internal SRT socket logic

public:
    std::set<SRTSOCKET> m_QueuedSockets; //< set of connections waiting for accept()

    sync::Condition m_AcceptCond; //< used to block "accept" call
    sync::Mutex     m_AcceptLock; //< mutex associated to m_AcceptCond

    unsigned int m_uiBackLog; //< maximum number of connections in queue

    // XXX A refactoring might be needed here.

    // There are no reasons found why the socket can't contain a list iterator to a
    // multiplexer INSTEAD of m_iMuxID. There's no danger in this solution because
    // the multiplexer is never deleted until there's at least one socket using it.
    //
    // The multiplexer may even physically be contained in the CUDTUnited object,
    // just track the multiple users of it (the listener and the accepted sockets).
    // When deleting, you simply "unsubscribe" yourself from the multiplexer, which
    // will unref it and remove the list element by the iterator kept by the
    // socket.
    int m_iMuxID; //< multiplexer ID

    sync::Mutex m_ControlLock; //< lock this socket exclusively for control APIs: bind/listen/connect

    CUDT&       core() { return m_UDT; }
    const CUDT& core() const { return m_UDT; }

    static int64_t getPeerSpec(SRTSOCKET id, int32_t isn) { return (int64_t(id) << 30) + isn; }
    int64_t        getPeerSpec() { return getPeerSpec(m_PeerID, m_iISN); }

    SRT_SOCKSTATUS getStatus();

    /// This function shall be called always wherever
    /// you'd like to call cudtsocket->m_pUDT->close(),
    /// from within the GC thread only (that is, only when
    /// the socket should be no longer visible in the
    /// connection, including for sending remaining data).
    void breakSocket_LOCKED();

    /// This makes the socket no longer capable of performing any transmission
    /// operation, but continues to be responsive in the connection in order
    /// to finish sending the data that were scheduled for sending so far.
    void setClosed();

    // This is necessary to be called from the group before the group clears
    // the connection with the socket. As for managed groups (and there are
    // currently no other group types), a socket disconnected from the group
    // is no longer usable.
    void setClosing()
    {
        core().m_bClosing = true;
    }

    /// This does the same as setClosed, plus sets the m_bBroken to true.
    /// Such a socket can still be read from so that remaining data from
    /// the receiver buffer can be read, but no longer sends anything.
    void setBrokenClosed();
    void removeFromGroup(bool broken);

    // Instrumentally used by select() and also required for non-blocking
    // mode check in groups
    bool readReady();
    bool writeReady() const;
    bool broken() const;

private:
    CUDTSocket& operator=(const CUDTSocket&);
};

////////////////////////////////////////////////////////////////////////////////

class CUDTUnited
{
    friend class CUDT;
    friend class CUDTGroup;
    friend class CRendezvousQueue;
    friend class CCryptoControl;

public:
    CUDTUnited();
    ~CUDTUnited();

    // Public constants
    static const int32_t MAX_SOCKET_VAL = SRTGROUP_MASK - 1; // maximum value for a regular socket

public:
    enum ErrorHandling
    {
        ERH_RETURN,
        ERH_THROW,
        ERH_ABORT
    };
    static std::string CONID(SRTSOCKET sock);

    /// initialize the UDT library.
    /// @return 0 if success, otherwise -1 is returned.
    int startup();

    /// release the UDT library.
    /// @return 0 if success, otherwise -1 is returned.
    int cleanup();

    /// Create a new UDT socket.
    /// @param [out] pps Variable (optional) to which the new socket will be written, if succeeded
    /// @return The new UDT socket ID, or INVALID_SOCK.
    SRTSOCKET newSocket(CUDTSocket** pps = NULL);

    /// Create (listener-side) a new socket associated with the incoming connection request.
    /// @param [in] listen the listening socket ID.
    /// @param [in] peer peer address.
    /// @param [in,out] hs handshake information from peer side (in), negotiated value (out);
    /// @param [out] w_error error code in case of failure.
    /// @param [out] w_acpu reference to the existing associated socket if already exists.
    /// @return  1: if the new connection was successfully created (accepted), @a w_acpu is NULL;
    ///          0: the connection already exists (reference to the corresponding socket is returned in @a w_acpu).
    ///         -1: The connection processing failed due to memory alloation error, exceeding listener's backlog,
    ///             any error propagated from CUDT::open and CUDT::acceptAndRespond.
    int newConnection(const SRTSOCKET     listen,
                      const sockaddr_any& peer,
                      const CPacket&      hspkt,
                      CHandShake&         w_hs,
                      int&                w_error,
                      CUDT*&              w_acpu);

    int installAcceptHook(const SRTSOCKET lsn, srt_listen_callback_fn* hook, void* opaq);
    int installConnectHook(const SRTSOCKET lsn, srt_connect_callback_fn* hook, void* opaq);

    /// Check the status of the UDT socket.
    /// @param [in] u the UDT socket ID.
    /// @return UDT socket status, or NONEXIST if not found.
    SRT_SOCKSTATUS getStatus(const SRTSOCKET u);

    // socket APIs

    int       bind(CUDTSocket* u, const sockaddr_any& name);
    int       bind(CUDTSocket* u, UDPSOCKET udpsock);
    int       listen(const SRTSOCKET u, int backlog);
    SRTSOCKET accept(const SRTSOCKET listen, sockaddr* addr, int* addrlen);
    SRTSOCKET accept_bond(const SRTSOCKET listeners[], int lsize, int64_t msTimeOut);
    int       connect(SRTSOCKET u, const sockaddr* srcname, const sockaddr* tarname, int tarlen);
    int       connect(const SRTSOCKET u, const sockaddr* name, int namelen, int32_t forced_isn);
    int       connectIn(CUDTSocket* s, const sockaddr_any& target, int32_t forced_isn);
#if ENABLE_BONDING
    int groupConnect(CUDTGroup* g, SRT_SOCKGROUPCONFIG targets[], int arraysize);
    int singleMemberConnect(CUDTGroup* g, SRT_SOCKGROUPCONFIG* target);
#endif
    int  close(const SRTSOCKET u);
    int  close(CUDTSocket* s);
    void getpeername(const SRTSOCKET u, sockaddr* name, int* namelen);
    void getsockname(const SRTSOCKET u, sockaddr* name, int* namelen);
    int  select(UDT::UDSET* readfds, UDT::UDSET* writefds, UDT::UDSET* exceptfds, const timeval* timeout);
    int  selectEx(const std::vector<SRTSOCKET>& fds,
                  std::vector<SRTSOCKET>*       readfds,
                  std::vector<SRTSOCKET>*       writefds,
                  std::vector<SRTSOCKET>*       exceptfds,
                  int64_t                       msTimeOut);
    int  epoll_create();
    int  epoll_clear_usocks(int eid);
    int  epoll_add_usock(const int eid, const SRTSOCKET u, const int* events = NULL);
    int  epoll_add_usock_INTERNAL(const int eid, CUDTSocket* s, const int* events);
    int  epoll_add_ssock(const int eid, const SYSSOCKET s, const int* events = NULL);
    int  epoll_remove_usock(const int eid, const SRTSOCKET u);
    template <class EntityType>
    int epoll_remove_entity(const int eid, EntityType* ent);
    int epoll_remove_socket_INTERNAL(const int eid, CUDTSocket* ent);
#if ENABLE_BONDING
    int epoll_remove_group_INTERNAL(const int eid, CUDTGroup* ent);
#endif
    int     epoll_remove_ssock(const int eid, const SYSSOCKET s);
    int     epoll_update_ssock(const int eid, const SYSSOCKET s, const int* events = NULL);
    int     epoll_uwait(const int eid, SRT_EPOLL_EVENT* fdsSet, int fdsSize, int64_t msTimeOut);
    int32_t epoll_set(const int eid, int32_t flags);
    int     epoll_release(const int eid);

#if ENABLE_BONDING
    // [[using locked(m_GlobControlLock)]]
    CUDTGroup& addGroup(SRTSOCKET id, SRT_GROUP_TYPE type)
    {
        // This only ensures that the element exists.
        // If the element was newly added, it will be NULL.
        CUDTGroup*& g = m_Groups[id];
        if (!g)
        {
            // This is a reference to the cell, so it will
            // rewrite it into the map.
            g = new CUDTGroup(type);
        }

        // Now we are sure that g is not NULL,
        // and persistence of this object is in the map.
        // The reference to the object can be safely returned here.
        return *g;
    }

    void deleteGroup(CUDTGroup* g);
    void deleteGroup_LOCKED(CUDTGroup* g);

    // [[using locked(m_GlobControlLock)]]
    CUDTGroup* findPeerGroup_LOCKED(SRTSOCKET peergroup)
    {
        for (groups_t::iterator i = m_Groups.begin(); i != m_Groups.end(); ++i)
        {
            if (i->second->peerid() == peergroup)
                return i->second;
        }
        return NULL;
    }
#endif

    CEPoll& epoll_ref() { return m_EPoll; }

private:
    /// Generates a new socket ID. This function starts from a randomly
    /// generated value (at initialization time) and goes backward with
    /// with next calls. The possible values come from the range without
    /// the SRTGROUP_MASK bit, and the group bit is set when the ID is
    /// generated for groups. It is also internally checked if the
    /// newly generated ID isn't already used by an existing socket or group.
    ///
    /// Socket ID value range.
    /// - [0]: reserved for handshake procedure. If the destination Socket ID is 0
    ///   (destination Socket ID unknown) the packet will be sent to the listening socket
    ///   or to a socket that is in the rendezvous connection phase.
    /// - [1; 2 ^ 30): single socket ID range.
    /// - (2 ^ 30; 2 ^ 31): group socket ID range. Effectively any positive number
    ///   from [1; 2 ^ 30) with bit 30 set to 1. Bit 31 is zero.
    /// The most significant bit 31 (sign bit) is left unused so that checking for a value <= 0 identifies an invalid
    /// socket ID.
    ///
    /// @param group The socket id should be for socket group.
    /// @return The new socket ID.
    /// @throw CUDTException if after rolling over all possible ID values nothing can be returned
    SRTSOCKET generateSocketID(bool group = false);

private:
    typedef std::map<SRTSOCKET, CUDTSocket*> sockets_t; // stores all the socket structures
    sockets_t                                m_Sockets;

#if ENABLE_BONDING
    typedef std::map<SRTSOCKET, CUDTGroup*> groups_t;
    groups_t                                m_Groups;
#endif

    sync::Mutex m_GlobControlLock; // used to synchronize UDT API

    sync::Mutex m_IDLock; // used to synchronize ID generation

    SRTSOCKET m_SocketIDGenerator;      // seed to generate a new unique socket ID
    SRTSOCKET m_SocketIDGenerator_init; // Keeps track of the very first one

    std::map<int64_t, std::set<SRTSOCKET> >
        m_PeerRec; // record sockets from peers to avoid repeated connection request, int64_t = (socker_id << 30) + isn

private:
    friend struct FLookupSocketWithEvent_LOCKED;

    CUDTSocket* locateSocket(SRTSOCKET u, ErrorHandling erh = ERH_RETURN);
    // This function does the same as locateSocket, except that:
    // - lock on m_GlobControlLock is expected (so that you don't unlock between finding and using)
    // - only return NULL if not found
    CUDTSocket* locateSocket_LOCKED(SRTSOCKET u);
    CUDTSocket* locatePeer(const sockaddr_any& peer, const SRTSOCKET id, int32_t isn);

#if ENABLE_BONDING
    CUDTGroup* locateAcquireGroup(SRTSOCKET u, ErrorHandling erh = ERH_RETURN);
    CUDTGroup* acquireSocketsGroup(CUDTSocket* s);

    struct GroupKeeper
    {
        CUDTGroup* group;

        // This is intended for API functions to lock the group's existence
        // for the lifetime of their call.
        GroupKeeper(CUDTUnited& glob, SRTSOCKET id, ErrorHandling erh) { group = glob.locateAcquireGroup(id, erh); }

        // This is intended for TSBPD thread that should lock the group's
        // existence until it exits.
        GroupKeeper(CUDTUnited& glob, CUDTSocket* s) { group = glob.acquireSocketsGroup(s); }

        ~GroupKeeper()
        {
            if (group)
            {
                // We have a guarantee that if `group` was set
                // as non-NULL here, it is also acquired and will not
                // be deleted until this busy flag is set back to false.
                sync::ScopedLock cgroup(*group->exp_groupLock());
                group->apiRelease();
                // Only now that the group lock is lifted, can the
                // group be now deleted and this pointer potentially dangling
            }
        }
    };

#endif
    void updateMux(CUDTSocket* s, const sockaddr_any& addr, const UDPSOCKET* = NULL);
    bool updateListenerMux(CUDTSocket* s, const CUDTSocket* ls);

    // Utility functions for updateMux
    void     configureMuxer(CMultiplexer& w_m, const CUDTSocket* s, int af);
    uint16_t installMuxer(CUDTSocket* w_s, CMultiplexer& sm);

    /// @brief Checks if channel configuration matches the socket configuration.
    /// @param cfgMuxer multiplexer configuration.
    /// @param cfgSocket socket configuration.
    /// @return tru if configurations match, false otherwise.
    static bool channelSettingsMatch(const CSrtMuxerConfig& cfgMuxer, const CSrtConfig& cfgSocket);
    static bool inet6SettingsCompat(const sockaddr_any& muxaddr, const CSrtMuxerConfig& cfgMuxer,
        const sockaddr_any& reqaddr, const CSrtMuxerConfig& cfgSocket);

private:
    std::map<int, CMultiplexer> m_mMultiplexer; // UDP multiplexer
    sync::Mutex                 m_MultiplexerLock;

private:
    CCache<CInfoBlock>* m_pCache; // UDT network information cache

private:
    srt::sync::atomic<bool> m_bClosing;
    sync::Mutex             m_GCStopLock;
    sync::Condition         m_GCStopCond;

    sync::Mutex m_InitLock;
    int         m_iInstanceCount; // number of startup() called by application
    bool        m_bGCStatus;      // if the GC thread is working (true)

    sync::CThread m_GCThread;
    static void*  garbageCollect(void*);

    sockets_t m_ClosedSockets; // temporarily store closed sockets
#if ENABLE_BONDING
    groups_t m_ClosedGroups;
#endif

    void checkBrokenSockets();
    void removeSocket(const SRTSOCKET u);

    CEPoll m_EPoll; // handling epoll data structures and events

private:
    CUDTUnited(const CUDTUnited&);
    CUDTUnited& operator=(const CUDTUnited&);
};

} // namespace srt

#endif
