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

#ifndef __UDT_EPOLL_H__
#define __UDT_EPOLL_H__


#include <map>
#include <set>
#include <list>
#include "udt.h"


struct CEPollDesc
{
   const int m_iID;                                // epoll ID

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
       int watch;

       /// Which events should be edge-triggered. When the event isn't
       /// mentioned in `watch`, this bit flag is disregarded. Otherwise
       /// it means that the event is to be waited for persistent state
       /// if this flag is not present here, and for edge trigger, if
       /// the flag is present here.
       int edge;

       /// The current persistent state. This is usually duplicated in
       /// a dedicated state object in `m_USockEventNotice`, however the state
       /// here will stay forever as is, regardless of the edge/persistent
       /// subscription mode for the event.
       int state;

       /// The iterator to `m_USockEventNotice` container that contains the
       /// event notice object for this subscription, or the value from
       /// `nullNotice()` if there is no such object.
       enotice_t::iterator notit;

       Wait(int sub, bool etr, enotice_t::iterator i)
           :watch(sub)
           ,edge(etr ? sub : 0)
           ,state(0)
           ,notit(i)
       {
       }

       int edgeOnly() { return edge & watch; }
   };

   typedef std::map<SRTSOCKET, Wait> ewatch_t;

private:

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

public:

   CEPollDesc(int id, int localID)
       : m_iID(id)
       , m_Flags(0)
       , m_iLocalID(localID)
    {
    }

   static const int32_t EF_NOCHECK_EMPTY = 1 << 0;
   static const int32_t EF_CHECK_REP = 1 << 1;

   int32_t flags() { return m_Flags; }
   bool flags(int32_t f) { return (m_Flags & f) != 0; }
   void set_flags(int32_t flg) { m_Flags |= flg; }
   void clr_flags(int32_t flg) { m_Flags &= ~flg; }

   // Container accessors for ewatch_t.
   bool watch_empty() { return m_USockWatchState.empty(); }
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

   const int m_iLocalID;                           // local system epoll ID
   std::set<SYSSOCKET> m_sLocals;            // set of local (non-UDT) descriptors

   std::pair<ewatch_t::iterator, bool> addWatch(SRTSOCKET sock, int32_t events, bool edgeTrg)
   {
        return m_USockWatchState.insert(std::make_pair(sock, Wait(events, edgeTrg, nullNotice())));
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
};

class CEPoll
{
friend class CUDT;
friend class CRendezvousQueue;

public:
   CEPoll();
   ~CEPoll();

public: // for CUDTUnited API

      /// create a new EPoll.
      /// @return new EPoll ID if success, otherwise an error number.

   int create();

      /// add a UDT socket to an EPoll.
      /// @param [in] eid EPoll ID.
      /// @param [in] u UDT Socket ID.
      /// @param [in] events events to watch.
      /// @return 0 if success, otherwise an error number.

   int add_usock(const int eid, const SRTSOCKET& u, const int* events = NULL) { return update_usock(eid, u, events); }

      /// add a system socket to an EPoll.
      /// @param [in] eid EPoll ID.
      /// @param [in] s system Socket ID.
      /// @param [in] events events to watch.
      /// @return 0 if success, otherwise an error number.

   int add_ssock(const int eid, const SYSSOCKET& s, const int* events = NULL);

      /// remove a UDT socket event from an EPoll; socket will be removed if no events to watch.
      /// @param [in] eid EPoll ID.
      /// @param [in] u UDT socket ID.
      /// @return 0 if success, otherwise an error number.

   int remove_usock(const int eid, const SRTSOCKET& u) { static const int Null(0); return update_usock(eid, u, &Null);}

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

      /// Update events available for a UDT socket.
      /// @param [in] uid UDT socket ID.
      /// @param [in] eids EPoll IDs to be set
      /// @param [in] events Combination of events to update
      /// @param [in] enable true -> enable, otherwise disable
      /// @return 0 if success, otherwise an error number

   int update_events(const SRTSOCKET& uid, std::set<int>& eids, int events, bool enable);

   int setflags(const int eid, int32_t flags);

private:
   int m_iIDSeed;                            // seed to generate a new ID
   pthread_mutex_t m_SeedLock;

   std::map<int, CEPollDesc> m_mPolls;       // all epolls
   pthread_mutex_t m_EPollLock;
};


#endif
