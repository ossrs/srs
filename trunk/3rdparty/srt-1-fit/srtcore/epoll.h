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
   Yunhong Gu, last updated 08/20/2010
modified by
   Haivision Systems Inc.
*****************************************************************************/

#ifndef INC_SRT_EPOLL_H
#define INC_SRT_EPOLL_H


#include <map>
#include <set>
#include <list>
#include "udt.h"

namespace srt
{

class CUDT;
class CRendezvousQueue;
class CUDTGroup;


class CEPollDesc
{
#ifdef __GNUG__
   const int m_iID;                                // epoll ID
#else
   const int m_iID SRT_ATR_UNUSED;                 // epoll ID
#endif
   struct Wait;

   struct Notice: public SRT_EPOLL_EVENT
   {
       Wait* parent;

       Notice(Wait* p, SRTSOCKET sock, int ev): parent(p)
       {
           fd = sock;
           events = ev;
       }
   };

   /// The type for `m_USockEventNotice`, the pair contains:
   /// * The back-pointer to the subscriber object for which this event notice serves
   /// * The events currently being on
   typedef std::list<Notice> enotice_t;

   struct Wait
   {
       /// Events the subscriber is interested with. Only those will be
       /// regarded when updating event flags.
       int32_t watch;

       /// Which events should be edge-triggered. When the event isn't
       /// mentioned in `watch`, this bit flag is disregarded. Otherwise
       /// it means that the event is to be waited for persistent state
       /// if this flag is not present here, and for edge trigger, if
       /// the flag is present here.
       int32_t edge;

       /// The current persistent state. This is usually duplicated in
       /// a dedicated state object in `m_USockEventNotice`, however the state
       /// here will stay forever as is, regardless of the edge/persistent
       /// subscription mode for the event.
       int32_t state;

       /// The iterator to `m_USockEventNotice` container that contains the
       /// event notice object for this subscription, or the value from
       /// `nullNotice()` if there is no such object.
       enotice_t::iterator notit;

       Wait(explicit_t<int32_t> sub, explicit_t<int32_t> etr, enotice_t::iterator i)
           :watch(sub)
           ,edge(etr)
           ,state(0)
           ,notit(i)
       {
       }

       int edgeOnly() { return edge & watch; }

       /// Clear all flags for given direction from the notices
       /// and subscriptions, and checks if this made the event list
       /// for this watch completely empty.
       /// @param direction event type that has to be cleared
       /// @return true, if this cleared the last event (the caller
       /// want to remove the subscription for this socket)
       bool clear(int32_t direction)
       {
           if (watch & direction)
           {
               watch &= ~direction;
               edge &= ~direction;
               state &= ~direction;

               return watch == 0;
           }

           return false;
       }
   };

   typedef std::map<SRTSOCKET, Wait> ewatch_t;

#if ENABLE_HEAVY_LOGGING
std::string DisplayEpollWatch();
#endif

   /// Sockets that are subscribed for events in this eid.
   ewatch_t m_USockWatchState;

   /// Objects representing changes in SRT sockets.
   /// Objects are removed from here when an event is registerred as edge-triggered.
   /// Otherwise it is removed only when all events as per subscription
   /// are no longer on.
   enotice_t m_USockEventNotice;

   // Special behavior
   int32_t m_Flags;

   enotice_t::iterator nullNotice() { return m_USockEventNotice.end(); }

   // Only CEPoll class should have access to it.
   // Guarding private access to the class is not necessary
   // within the epoll module.
   friend class CEPoll;

   CEPollDesc(int id, int localID)
       : m_iID(id)
       , m_Flags(0)
       , m_iLocalID(localID)
    {
    }

   static const int32_t EF_NOCHECK_EMPTY = 1 << 0;
   static const int32_t EF_CHECK_REP = 1 << 1;

   int32_t flags() const { return m_Flags; }
   bool flags(int32_t f) const { return (m_Flags & f) != 0; }
   void set_flags(int32_t flg) { m_Flags |= flg; }
   void clr_flags(int32_t flg) { m_Flags &= ~flg; }

   // Container accessors for ewatch_t.
   bool watch_empty() const { return m_USockWatchState.empty(); }
   Wait* watch_find(SRTSOCKET sock)
   {
       ewatch_t::iterator i = m_USockWatchState.find(sock);
       if (i == m_USockWatchState.end())
           return NULL;
       return &i->second;
   }

   // Container accessors for enotice_t.
   enotice_t::iterator enotice_begin() { return m_USockEventNotice.begin(); }
   enotice_t::iterator enotice_end() { return m_USockEventNotice.end(); }
   enotice_t::const_iterator enotice_begin() const { return m_USockEventNotice.begin(); }
   enotice_t::const_iterator enotice_end() const { return m_USockEventNotice.end(); }
   bool enotice_empty() const { return m_USockEventNotice.empty(); }

   const int m_iLocalID;                           // local system epoll ID
   std::set<SYSSOCKET> m_sLocals;            // set of local (non-UDT) descriptors

   std::pair<ewatch_t::iterator, bool> addWatch(SRTSOCKET sock, explicit_t<int32_t> events, explicit_t<int32_t> et_events)
   {
        return m_USockWatchState.insert(std::make_pair(sock, Wait(events, et_events, nullNotice())));
   }

   void addEventNotice(Wait& wait, SRTSOCKET sock, int events)
   {
       // `events` contains bits to be set, so:
       //
       // 1. If no notice object exists, add it exactly with `events`.
       // 2. If it exists, only set the bits from `events`.
       // ASSUME: 'events' is not 0, that is, we have some readiness

       if (wait.notit == nullNotice()) // No notice object
       {
           // Add new event notice and bind to the wait object.
           m_USockEventNotice.push_back(Notice(&wait, sock, events));
           wait.notit = --m_USockEventNotice.end();

           return;
       }

       // We have an existing event notice, so update it
       wait.notit->events |= events;
   }

   // This function only updates the corresponding event notice object
   // according to the change in the events.
   void updateEventNotice(Wait& wait, SRTSOCKET sock, int events, bool enable)
   {
       if (enable)
       {
           addEventNotice(wait, sock, events);
       }
       else
       {
           removeExcessEvents(wait, ~events);
       }
   }

   void removeSubscription(SRTSOCKET u)
   {
       std::map<SRTSOCKET, Wait>::iterator i = m_USockWatchState.find(u);
       if (i == m_USockWatchState.end())
           return;

       if (i->second.notit != nullNotice())
       {
           m_USockEventNotice.erase(i->second.notit);
           // NOTE: no need to update the Wait::notit field
           // because the Wait object is about to be removed anyway.
       }
       m_USockWatchState.erase(i);
   }

   void clearAll()
   {
       m_USockEventNotice.clear();
       m_USockWatchState.clear();
   }

   void removeExistingNotices(Wait& wait)
   {
       m_USockEventNotice.erase(wait.notit);
       wait.notit = nullNotice();
   }

   void removeEvents(Wait& wait)
   {
       if (wait.notit == nullNotice())
           return;
       removeExistingNotices(wait);
   }

   // This function removes notices referring to
   // events that are NOT present in @a nevts, but
   // may be among subscriptions and therefore potentially
   // have an associated notice.
   void removeExcessEvents(Wait& wait, int nevts)
   {
       // Update the event notice, should it exist
       // If the watch points to a null notice, there's simply
       // no notice there, so nothing to update or prospectively
       // remove - but may be something to add.
       if (wait.notit == nullNotice())
           return;

       // `events` contains bits to be cleared.
       // 1. If there is no notice event, do nothing - clear already.
       // 2. If there is a notice event, update by clearing the bits
       // 2.1. If this made resulting state to be 0, also remove the notice.

       const int newstate = wait.notit->events & nevts;
       if (newstate)
       {
           wait.notit->events = newstate;
       }
       else
       {
           // If the new state is full 0 (no events),
           // then remove the corresponding notice object
           removeExistingNotices(wait);
       }
   }

   bool checkEdge(enotice_t::iterator i)
   {
       // This function should check if this event was subscribed
       // as edge-triggered, and if so, clear the event from the notice.
       // Update events and check edge mode at the subscriber
       i->events &= ~i->parent->edgeOnly();
       if(!i->events)
       {
           removeExistingNotices(*i->parent);
           return true;
       }
       return false;
   }

   /// This should work in a loop around the notice container of
   /// the given eid container and clear out the notice for
   /// particular event type. If this has cleared effectively the
   /// last existing event, it should return the socket id
   /// so that the caller knows to remove it also from subscribers.
   ///
   /// @param i iterator in the notice container
   /// @param event event type to be cleared
   /// @retval (socket) Socket to be removed from subscriptions
   /// @retval SRT_INVALID_SOCK Nothing to be done (associated socket
   ///         still has other subscriptions)
   SRTSOCKET clearEventSub(enotice_t::iterator i, int event)
   {
       // We need to remove the notice and subscription
       // for this event. The 'i' iterator is safe to
       // delete, even indirectly.

       // This works merely like checkEdge, just on request to clear the
       // identified event, if found.
       if (i->events & event)
       {
           // The notice has a readiness flag on this event.
           // This means that there exists also a subscription.
           Wait* w = i->parent;
           if (w->clear(event))
               return i->fd;
       }

       return SRT_INVALID_SOCK;
   }
};

class CEPoll
{
friend class srt::CUDT;
friend class srt::CUDTGroup;
friend class srt::CRendezvousQueue;

public:
   CEPoll();
   ~CEPoll();

public: // for CUDTUnited API

   /// create a new EPoll.
   /// @return new EPoll ID if success, otherwise an error number.
   int create(CEPollDesc** ppd = 0);


   /// delete all user sockets (SRT sockets) from an EPoll
   /// @param [in] eid EPoll ID.
   /// @return 0 
   int clear_usocks(int eid);

   /// add a system socket to an EPoll.
   /// @param [in] eid EPoll ID.
   /// @param [in] s system Socket ID.
   /// @param [in] events events to watch.
   /// @return 0 if success, otherwise an error number.

   int add_ssock(const int eid, const SYSSOCKET& s, const int* events = NULL);

   /// remove a system socket event from an EPoll; socket will be removed if no events to watch.
   /// @param [in] eid EPoll ID.
   /// @param [in] s system socket ID.
   /// @return 0 if success, otherwise an error number.

   int remove_ssock(const int eid, const SYSSOCKET& s);
   /// update a UDT socket events from an EPoll.
   /// @param [in] eid EPoll ID.
   /// @param [in] u UDT socket ID.
   /// @param [in] events events to watch.
   /// @return 0 if success, otherwise an error number.

   int update_usock(const int eid, const SRTSOCKET& u, const int* events);

   /// update a system socket events from an EPoll.
   /// @param [in] eid EPoll ID.
   /// @param [in] u UDT socket ID.
   /// @param [in] events events to watch.
   /// @return 0 if success, otherwise an error number.

   int update_ssock(const int eid, const SYSSOCKET& s, const int* events = NULL);

   /// wait for EPoll events or timeout.
   /// @param [in] eid EPoll ID.
   /// @param [out] readfds UDT sockets available for reading.
   /// @param [out] writefds UDT sockets available for writing.
   /// @param [in] msTimeOut timeout threshold, in milliseconds.
   /// @param [out] lrfds system file descriptors for reading.
   /// @param [out] lwfds system file descriptors for writing.
   /// @return number of sockets available for IO.

   int wait(const int eid, std::set<SRTSOCKET>* readfds, std::set<SRTSOCKET>* writefds, int64_t msTimeOut, std::set<SYSSOCKET>* lrfds, std::set<SYSSOCKET>* lwfds);

   typedef std::map<SRTSOCKET, int> fmap_t;

   /// Lightweit and more internal-reaching version of `uwait` for internal use only.
   /// This function wait for sockets to be ready and reports them in `st` map.
   ///
   /// @param d the internal structure of the epoll container
   /// @param st output container for the results: { socket_type, event }
   /// @param msTimeOut timeout after which return with empty output is allowed
   /// @param report_by_exception if true, errors will result in exception intead of returning -1
   /// @retval -1 error occurred
   /// @retval >=0 number of ready sockets (actually size of `st`)
   int swait(CEPollDesc& d, fmap_t& st, int64_t msTimeOut, bool report_by_exception = true);

   /// Empty subscription check - for internal use only.
   bool empty(const CEPollDesc& d) const;

   /// Reports which events are ready on the given socket.
   /// @param mp socket event map retirned by `swait`
   /// @param sock which socket to ask
   /// @return event flags for given socket, or 0 if none
   static int ready(const fmap_t& mp, SRTSOCKET sock)
   {
       fmap_t::const_iterator y = mp.find(sock);
       if (y == mp.end())
           return 0;
       return y->second;
   }

   /// Reports whether socket is ready for given event.
   /// @param mp socket event map retirned by `swait`
   /// @param sock which socket to ask
   /// @param event which events it should be ready for
   /// @return true if the given socket is ready for given event
   static bool isready(const fmap_t& mp, SRTSOCKET sock, SRT_EPOLL_OPT event)
   {
       return (ready(mp, sock) & event) != 0;
   }

   // Could be a template directly, but it's now hidden in the imp file.
   void clear_ready_usocks(CEPollDesc& d, int direction);

   /// wait for EPoll events or timeout optimized with explicit EPOLL_ERR event and the edge mode option.
   /// @param [in] eid EPoll ID.
   /// @param [out] fdsSet array of user socket events (SRT_EPOLL_IN | SRT_EPOLL_OUT | SRT_EPOLL_ERR).
   /// @param [int] fdsSize of fds array
   /// @param [in] msTimeOut timeout threshold, in milliseconds.
   /// @return total of available events in the epoll system (can be greater than fdsSize)

   int uwait(const int eid, SRT_EPOLL_EVENT* fdsSet, int fdsSize, int64_t msTimeOut);

   /// close and release an EPoll.
   /// @param [in] eid EPoll ID.
   /// @return 0 if success, otherwise an error number.

   int release(const int eid);

public: // for CUDT to acknowledge IO status

   /// Update events available for a UDT socket. At the end this function
   /// counts the number of updated EIDs with given events.
   /// @param [in] uid UDT socket ID.
   /// @param [in] eids EPoll IDs to be set
   /// @param [in] events Combination of events to update
   /// @param [in] enable true -> enable, otherwise disable
   /// @return -1 if invalid events, otherwise the number of changes

   int update_events(const SRTSOCKET& uid, std::set<int>& eids, int events, bool enable);

   int setflags(const int eid, int32_t flags);

private:
   int m_iIDSeed;                            // seed to generate a new ID
   srt::sync::Mutex m_SeedLock;

   std::map<int, CEPollDesc> m_mPolls;       // all epolls
   mutable srt::sync::Mutex m_EPollLock;
};

#if ENABLE_HEAVY_LOGGING
std::string DisplayEpollResults(const std::map<SRTSOCKET, int>& sockset);
#endif

} // namespace srt


#endif
