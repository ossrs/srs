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
   Yunhong Gu, last updated 01/01/2011
modified by
   Haivision Systems Inc.
*****************************************************************************/

#define SRT_IMPORT_EVENT
#include "platform_sys.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <iterator>

#if defined(__FreeBSD_kernel__)
#include <sys/event.h>
#endif

#include "common.h"
#include "epoll.h"
#include "logging.h"
#include "udt.h"

using namespace std;
using namespace srt::sync;

#if ENABLE_HEAVY_LOGGING
namespace srt {
static ostream& PrintEpollEvent(ostream& os, int events, int et_events = 0);
}
#endif

namespace srt_logging
{
    extern Logger eilog, ealog;
}

using namespace srt_logging;

#if ENABLE_HEAVY_LOGGING
#define IF_DIRNAME(tested, flag, name) (tested & flag ? name : "")
#endif

srt::CEPoll::CEPoll():
m_iIDSeed(0)
{
   // Exception -> CUDTUnited ctor.
   setupMutex(m_EPollLock, "EPoll");
}

srt::CEPoll::~CEPoll()
{
   releaseMutex(m_EPollLock);
}

int srt::CEPoll::create(CEPollDesc** pout)
{
   ScopedLock pg(m_EPollLock);

   if (++ m_iIDSeed >= 0x7FFFFFFF)
      m_iIDSeed = 0;

   // Check if an item already exists. Should not ever happen.
   if (m_mPolls.find(m_iIDSeed) != m_mPolls.end())
       throw CUDTException(MJ_SETUP, MN_NONE);

   int localid = 0;

   #ifdef LINUX

   // NOTE: epoll_create1() and EPOLL_CLOEXEC were introduced in GLIBC-2.9.
   //    So earlier versions of GLIBC, must use epoll_create() and set
   //       FD_CLOEXEC on the file descriptor returned by it after the fact.
   #if defined(EPOLL_CLOEXEC)
      int flags = 0;
      #if ENABLE_SOCK_CLOEXEC
      flags |= EPOLL_CLOEXEC;
      #endif
      localid = epoll_create1(flags);
   #else
      localid = epoll_create(1);
      #if ENABLE_SOCK_CLOEXEC
      if (localid != -1)
      {
         int fdFlags = fcntl(localid, F_GETFD);
         if (fdFlags != -1)
         {
            fdFlags |= FD_CLOEXEC;
            fcntl(localid, F_SETFD, fdFlags);
         }
      }
      #endif
   #endif

   /* Possible reasons of -1 error:
EMFILE: The per-user limit on the number of epoll instances imposed by /proc/sys/fs/epoll/max_user_instances was encountered.
ENFILE: The system limit on the total number of open files has been reached.
ENOMEM: There was insufficient memory to create the kernel object.
       */
   if (localid < 0)
      throw CUDTException(MJ_SETUP, MN_NONE, errno);
   #elif defined(BSD) || TARGET_OS_MAC
   localid = kqueue();
   if (localid < 0)
      throw CUDTException(MJ_SETUP, MN_NONE, errno);
   #else
   // TODO: Solaris, use port_getn()
   //    https://docs.oracle.com/cd/E86824_01/html/E54766/port-get-3c.html
   // on Windows, select
   #endif

   pair<map<int, CEPollDesc>::iterator, bool> res = m_mPolls.insert(make_pair(m_iIDSeed, CEPollDesc(m_iIDSeed, localid)));
   if (!res.second)  // Insertion failed (no memory?)
       throw CUDTException(MJ_SETUP, MN_NONE);
   if (pout)
       *pout = &res.first->second;

   return m_iIDSeed;
}

int srt::CEPoll::clear_usocks(int eid)
{
    // This should remove all SRT sockets from given eid.
   ScopedLock pg (m_EPollLock);

   map<int, CEPollDesc>::iterator p = m_mPolls.find(eid);
   if (p == m_mPolls.end())
      throw CUDTException(MJ_NOTSUP, MN_EIDINVAL);

   CEPollDesc& d = p->second;

   d.clearAll();

   return 0;
}


void srt::CEPoll::clear_ready_usocks(CEPollDesc& d, int direction)
{
    if ((direction & ~SRT_EPOLL_EVENTTYPES) != 0)
    {
        // This is internal function, so simply report an IPE on incorrect usage.
        LOGC(eilog.Error, log << "CEPoll::clear_ready_usocks: IPE, event flags exceed event types: " << direction);
        return;
    }
    ScopedLock pg (m_EPollLock);

    vector<SRTSOCKET> cleared;

    CEPollDesc::enotice_t::iterator i = d.enotice_begin();
    while (i != d.enotice_end())
    {
        IF_HEAVY_LOGGING(SRTSOCKET subsock = i->fd);
        SRTSOCKET rs = d.clearEventSub(i++, direction);
        // This function returns:
        // - a valid socket - if there are no other subscription after 'direction' was cleared
        // - SRT_INVALID_SOCK otherwise
        // Valid sockets should be collected as sockets that no longer
        // have a subscribed event should be deleted from subscriptions.
        if (rs != SRT_INVALID_SOCK)
        {
            HLOGC(eilog.Debug, log << "CEPoll::clear_ready_usocks: @" << rs << " got all subscription cleared");
            cleared.push_back(rs);
        }
        else
        {
            HLOGC(eilog.Debug, log << "CEPoll::clear_ready_usocks: @" << subsock << " is still subscribed");
        }
    }

    for (size_t i = 0; i < cleared.size(); ++i)
        d.removeSubscription(cleared[i]);
}

int srt::CEPoll::add_ssock(const int eid, const SYSSOCKET& s, const int* events)
{
   ScopedLock pg(m_EPollLock);

   map<int, CEPollDesc>::iterator p = m_mPolls.find(eid);
   if (p == m_mPolls.end())
      throw CUDTException(MJ_NOTSUP, MN_EIDINVAL);

#ifdef LINUX
   epoll_event ev;
   memset(&ev, 0, sizeof(epoll_event));

   if (NULL == events)
      ev.events = EPOLLIN | EPOLLOUT | EPOLLERR;
   else
   {
      ev.events = 0;
      if (*events & SRT_EPOLL_IN)
         ev.events |= EPOLLIN;
      if (*events & SRT_EPOLL_OUT)
         ev.events |= EPOLLOUT;
      if (*events & SRT_EPOLL_ERR)
         ev.events |= EPOLLERR;
   }

   ev.data.fd = s;
   if (::epoll_ctl(p->second.m_iLocalID, EPOLL_CTL_ADD, s, &ev) < 0)
      throw CUDTException();
#elif defined(BSD) || TARGET_OS_MAC
   struct kevent ke[2];
   int num = 0;

   if (NULL == events)
   {
      EV_SET(&ke[num++], s, EVFILT_READ, EV_ADD, 0, 0, NULL);
      EV_SET(&ke[num++], s, EVFILT_WRITE, EV_ADD, 0, 0, NULL);
   }
   else
   {
      if (*events & SRT_EPOLL_IN)
      {
         EV_SET(&ke[num++], s, EVFILT_READ, EV_ADD, 0, 0, NULL);
      }
      if (*events & SRT_EPOLL_OUT)
      {
         EV_SET(&ke[num++], s, EVFILT_WRITE, EV_ADD, 0, 0, NULL);
      }
   }
   if (kevent(p->second.m_iLocalID, ke, num, NULL, 0, NULL) < 0)
      throw CUDTException();
#else

   // fake use 'events' to prevent warning. Remove when implemented.
   (void)events;
   (void)s;

#ifdef _MSC_VER
// Microsoft Visual Studio doesn't support the #warning directive - nonstandard anyway.
// Use #pragma message with the same text.
// All other compilers should be ok :)
#pragma message("WARNING: Unsupported system for epoll. The epoll_add_ssock() API call won't work on this platform.")
#else
#warning "Unsupported system for epoll. The epoll_add_ssock() API call won't work on this platform."
#endif

#endif

   p->second.m_sLocals.insert(s);

   return 0;
}

int srt::CEPoll::remove_ssock(const int eid, const SYSSOCKET& s)
{
   ScopedLock pg(m_EPollLock);

   map<int, CEPollDesc>::iterator p = m_mPolls.find(eid);
   if (p == m_mPolls.end())
      throw CUDTException(MJ_NOTSUP, MN_EIDINVAL);

#ifdef LINUX
   epoll_event ev;  // ev is ignored, for compatibility with old Linux kernel only.
   if (::epoll_ctl(p->second.m_iLocalID, EPOLL_CTL_DEL, s, &ev) < 0)
      throw CUDTException();
#elif defined(BSD) || TARGET_OS_MAC
   struct kevent ke;

   //
   // Since I don't know what was set before
   // Just clear out both read and write
   //
   EV_SET(&ke, s, EVFILT_READ, EV_DELETE, 0, 0, NULL);
   kevent(p->second.m_iLocalID, &ke, 1, NULL, 0, NULL);
   EV_SET(&ke, s, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
   kevent(p->second.m_iLocalID, &ke, 1, NULL, 0, NULL);
#endif

   p->second.m_sLocals.erase(s);

   return 0;
}

// Need this to atomically modify polled events (ex: remove write/keep read)
int srt::CEPoll::update_usock(const int eid, const SRTSOCKET& u, const int* events)
{
    ScopedLock pg(m_EPollLock);
    IF_HEAVY_LOGGING(ostringstream evd);

    map<int, CEPollDesc>::iterator p = m_mPolls.find(eid);
    if (p == m_mPolls.end())
        throw CUDTException(MJ_NOTSUP, MN_EIDINVAL);

    CEPollDesc& d = p->second;

    int32_t evts = events ? *events : uint32_t(SRT_EPOLL_IN | SRT_EPOLL_OUT | SRT_EPOLL_ERR);
    bool edgeTriggered = evts & SRT_EPOLL_ET;
    evts &= ~SRT_EPOLL_ET;

    // et_evts = all events, if SRT_EPOLL_ET, or only those that are always ET otherwise.
    int32_t et_evts = edgeTriggered ? evts : evts & SRT_EPOLL_ETONLY;
    if (evts)
    {
        pair<CEPollDesc::ewatch_t::iterator, bool> iter_new = d.addWatch(u, evts, et_evts);
        CEPollDesc::Wait& wait = iter_new.first->second;
        if (!iter_new.second)
        {
            // The object exists. We only are certain about the `u`
            // parameter, but others are probably unchanged. Change them
            // forcefully and take out notices that are no longer valid.
            const int removable = wait.watch & ~evts;
            IF_HEAVY_LOGGING(PrintEpollEvent(evd, evts & (~wait.watch)));

            // Check if there are any events that would be removed.
            // If there are no removed events watched (for example, when
            // only new events are being added to existing socket),
            // there's nothing to remove, but might be something to update.
            if (removable)
            {
                d.removeExcessEvents(wait, evts);
            }

            // Update the watch configuration, including edge
            wait.watch = evts;
            wait.edge = et_evts;

            // Now it should look exactly like newly added
            // and the state is also updated
            HLOGC(ealog.Debug, log << "srt_epoll_update_usock: UPDATED E" << eid << " for @" << u << " +" << evd.str());
        }
        else
        {
            IF_HEAVY_LOGGING(PrintEpollEvent(evd, evts));
            HLOGC(ealog.Debug, log << "srt_epoll_update_usock: ADDED E" << eid << " for @" << u << " " << evd.str());
        }

        const int newstate = wait.watch & wait.state;
        if (newstate)
        {
            d.addEventNotice(wait, u, newstate);
        }
    }
    else if (edgeTriggered)
    {
        LOGC(ealog.Error, log << "srt_epoll_update_usock: Specified only SRT_EPOLL_ET flag, but no event flag. Error.");
        throw CUDTException(MJ_NOTSUP, MN_INVAL);
    }
    else
    {
        // Update with no events means to remove subscription
        HLOGC(ealog.Debug, log << "srt_epoll_update_usock: REMOVED E" << eid << " socket @" << u);
        d.removeSubscription(u);
    }
    return 0;
}

int srt::CEPoll::update_ssock(const int eid, const SYSSOCKET& s, const int* events)
{
   ScopedLock pg(m_EPollLock);

   map<int, CEPollDesc>::iterator p = m_mPolls.find(eid);
   if (p == m_mPolls.end())
      throw CUDTException(MJ_NOTSUP, MN_EIDINVAL);

#ifdef LINUX
   epoll_event ev;
   memset(&ev, 0, sizeof(epoll_event));

   if (NULL == events)
      ev.events = EPOLLIN | EPOLLOUT | EPOLLERR;
   else
   {
      ev.events = 0;
      if (*events & SRT_EPOLL_IN)
         ev.events |= EPOLLIN;
      if (*events & SRT_EPOLL_OUT)
         ev.events |= EPOLLOUT;
      if (*events & SRT_EPOLL_ERR)
         ev.events |= EPOLLERR;
   }

   ev.data.fd = s;
   if (::epoll_ctl(p->second.m_iLocalID, EPOLL_CTL_MOD, s, &ev) < 0)
      throw CUDTException();
#elif defined(BSD) || TARGET_OS_MAC
   struct kevent ke[2];
   int num = 0;

   //
   // Since I don't know what was set before
   // Just clear out both read and write
   //
   EV_SET(&ke[0], s, EVFILT_READ, EV_DELETE, 0, 0, NULL);
   kevent(p->second.m_iLocalID, ke, 1, NULL, 0, NULL);
   EV_SET(&ke[0], s, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
   kevent(p->second.m_iLocalID, ke, 1, NULL, 0, NULL);
   if (NULL == events)
   {
      EV_SET(&ke[num++], s, EVFILT_READ, EV_ADD, 0, 0, NULL);
      EV_SET(&ke[num++], s, EVFILT_WRITE, EV_ADD, 0, 0, NULL);
   }
   else
   {
      if (*events & SRT_EPOLL_IN)
      {
         EV_SET(&ke[num++], s, EVFILT_READ, EV_ADD, 0, 0, NULL);
      }
      if (*events & SRT_EPOLL_OUT)
      {
         EV_SET(&ke[num++], s, EVFILT_WRITE, EV_ADD, 0, 0, NULL);
      }
   }
   if (kevent(p->second.m_iLocalID, ke, num, NULL, 0, NULL) < 0)
      throw CUDTException();
#else

   // fake use 'events' to prevent warning. Remove when implemented.
   (void)events;
   (void)s;

#endif
// Assuming add is used if not inserted
//   p->second.m_sLocals.insert(s);

   return 0;
}

int srt::CEPoll::setflags(const int eid, int32_t flags)
{
    ScopedLock pg(m_EPollLock);
    map<int, CEPollDesc>::iterator p = m_mPolls.find(eid);
    if (p == m_mPolls.end())
        throw CUDTException(MJ_NOTSUP, MN_EIDINVAL);
    CEPollDesc& ed = p->second;

    int32_t oflags = ed.flags();

    if (flags == -1)
        return oflags;

    if (flags == 0)
    {
        ed.clr_flags(~int32_t());
    }
    else
    {
        ed.set_flags(flags);
    }

    return oflags;
}

int srt::CEPoll::uwait(const int eid, SRT_EPOLL_EVENT* fdsSet, int fdsSize, int64_t msTimeOut)
{
    // It is allowed to call this function witn fdsSize == 0
    // and therefore also NULL fdsSet. This will then only report
    // the number of ready sockets, just without information which.
    if (fdsSize < 0 || (fdsSize > 0 && !fdsSet))
        throw CUDTException(MJ_NOTSUP, MN_INVAL);

    steady_clock::time_point entertime = steady_clock::now();

    while (true)
    {
        {
            ScopedLock pg(m_EPollLock);
            map<int, CEPollDesc>::iterator p = m_mPolls.find(eid);
            if (p == m_mPolls.end())
                throw CUDTException(MJ_NOTSUP, MN_EIDINVAL);
            CEPollDesc& ed = p->second;

            if (!ed.flags(SRT_EPOLL_ENABLE_EMPTY) && ed.watch_empty())
            {
                // Empty EID is not allowed, report error.
                throw CUDTException(MJ_NOTSUP, MN_EEMPTY);
            }

            if (ed.flags(SRT_EPOLL_ENABLE_OUTPUTCHECK) && (fdsSet == NULL || fdsSize == 0))
            {
                // Empty EID is not allowed, report error.
                throw CUDTException(MJ_NOTSUP, MN_INVAL);
            }

            if (!ed.m_sLocals.empty())
            {
                // XXX Add error log
                // uwait should not be used with EIDs subscribed to system sockets
                throw CUDTException(MJ_NOTSUP, MN_INVAL);
            }

            int total = 0; // This is a list, so count it during iteration
            CEPollDesc::enotice_t::iterator i = ed.enotice_begin();
            while (i != ed.enotice_end())
            {
                int pos = total; // previous past-the-end position
                ++total;

                if (total > fdsSize)
                    break;

                fdsSet[pos] = *i;

                ed.checkEdge(i++); // NOTE: potentially deletes `i`
            }
            if (total)
                return total;
        }

        if ((msTimeOut >= 0) && (count_microseconds(srt::sync::steady_clock::now() - entertime) >= msTimeOut * int64_t(1000)))
            break; // official wait does: throw CUDTException(MJ_AGAIN, MN_XMTIMEOUT, 0);

        CGlobEvent::waitForEvent();
    }

    return 0;
}

int srt::CEPoll::wait(const int eid, set<SRTSOCKET>* readfds, set<SRTSOCKET>* writefds, int64_t msTimeOut, set<SYSSOCKET>* lrfds, set<SYSSOCKET>* lwfds)
{
    // if all fields is NULL and waiting time is infinite, then this would be a deadlock
    if (!readfds && !writefds && !lrfds && !lwfds && (msTimeOut < 0))
        throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);

    // Clear these sets in case the app forget to do it.
    if (readfds) readfds->clear();
    if (writefds) writefds->clear();
    if (lrfds) lrfds->clear();
    if (lwfds) lwfds->clear();

    int total = 0;

    srt::sync::steady_clock::time_point entertime = srt::sync::steady_clock::now();
    while (true)
    {
        {
            ScopedLock epollock(m_EPollLock);

            map<int, CEPollDesc>::iterator p = m_mPolls.find(eid);
            if (p == m_mPolls.end())
            {
                LOGC(ealog.Error, log << "EID:" << eid << " INVALID.");
                throw CUDTException(MJ_NOTSUP, MN_EIDINVAL);
            }

            CEPollDesc& ed = p->second;

            if (!ed.flags(SRT_EPOLL_ENABLE_EMPTY) && ed.watch_empty() && ed.m_sLocals.empty())
            {
                // Empty EID is not allowed, report error.
                //throw CUDTException(MJ_NOTSUP, MN_INVAL);
                LOGC(ealog.Error, log << "EID:" << eid << " no sockets to check, this would deadlock");
                throw CUDTException(MJ_NOTSUP, MN_EEMPTY, 0);
            }

            if (ed.flags(SRT_EPOLL_ENABLE_OUTPUTCHECK))
            {
                // Empty report is not allowed, report error.
                if (!ed.m_sLocals.empty() && (!lrfds || !lwfds))
                    throw CUDTException(MJ_NOTSUP, MN_INVAL);

                if (!ed.watch_empty() && (!readfds || !writefds))
                    throw CUDTException(MJ_NOTSUP, MN_INVAL);
            }

            IF_HEAVY_LOGGING(int total_noticed = 0);
            IF_HEAVY_LOGGING(ostringstream debug_sockets);
            // Sockets with exceptions are returned to both read and write sets.
            for (CEPollDesc::enotice_t::iterator it = ed.enotice_begin(), it_next = it; it != ed.enotice_end(); it = it_next)
            {
                ++it_next;
                IF_HEAVY_LOGGING(++total_noticed);
                if (readfds && ((it->events & SRT_EPOLL_IN) || (it->events & SRT_EPOLL_ERR)))
                {
                    if (readfds->insert(it->fd).second)
                        ++total;
                }

                if (writefds && ((it->events & SRT_EPOLL_OUT) || (it->events & SRT_EPOLL_ERR)))
                {
                    if (writefds->insert(it->fd).second)
                        ++total;
                }

                IF_HEAVY_LOGGING(debug_sockets << " " << it->fd << ":"
                        << IF_DIRNAME(it->events, SRT_EPOLL_IN, "R")
                        << IF_DIRNAME(it->events, SRT_EPOLL_OUT, "W")
                        << IF_DIRNAME(it->events, SRT_EPOLL_ERR, "E"));

                if (ed.checkEdge(it)) // NOTE: potentially erases 'it'.
                {
                    IF_HEAVY_LOGGING(debug_sockets << "!");
                }
            }

            HLOGC(ealog.Debug, log << "CEPoll::wait: REPORTED " << total << "/" << total_noticed
                    << debug_sockets.str());

            if ((lrfds || lwfds) && !ed.m_sLocals.empty())
            {
#ifdef LINUX
                const int max_events = ed.m_sLocals.size();
                SRT_ASSERT(max_events > 0);
                epoll_event ev[max_events];
                int nfds = ::epoll_wait(ed.m_iLocalID, ev, max_events, 0);

                IF_HEAVY_LOGGING(const int prev_total = total);
                for (int i = 0; i < nfds; ++ i)
                {
                    if ((NULL != lrfds) && (ev[i].events & EPOLLIN))
                    {
                        lrfds->insert(ev[i].data.fd);
                        ++ total;
                    }
                    if ((NULL != lwfds) && (ev[i].events & EPOLLOUT))
                    {
                        lwfds->insert(ev[i].data.fd);
                        ++ total;
                    }
                }
                HLOGC(ealog.Debug, log << "CEPoll::wait: LINUX: picking up " << (total - prev_total)  << " ready fds.");

#elif defined(BSD) || TARGET_OS_MAC
                struct timespec tmout = {0, 0};
                const int max_events = ed.m_sLocals.size();
                SRT_ASSERT(max_events > 0);
                struct kevent ke[max_events];

                int nfds = kevent(ed.m_iLocalID, NULL, 0, ke, max_events, &tmout);
                IF_HEAVY_LOGGING(const int prev_total = total);

                for (int i = 0; i < nfds; ++ i)
                {
                    if ((NULL != lrfds) && (ke[i].filter == EVFILT_READ))
                    {
                        lrfds->insert(ke[i].ident);
                        ++ total;
                    }
                    if ((NULL != lwfds) && (ke[i].filter == EVFILT_WRITE))
                    {
                        lwfds->insert(ke[i].ident);
                        ++ total;
                    }
                }

                HLOGC(ealog.Debug, log << "CEPoll::wait: Darwin/BSD: picking up " << (total - prev_total)  << " ready fds.");

#else
                //currently "select" is used for all non-Linux platforms.
                //faster approaches can be applied for specific systems in the future.

                //"select" has a limitation on the number of sockets
                int max_fd = 0;

                fd_set rqreadfds;
                fd_set rqwritefds;
                FD_ZERO(&rqreadfds);
                FD_ZERO(&rqwritefds);

                for (set<SYSSOCKET>::const_iterator i = ed.m_sLocals.begin(); i != ed.m_sLocals.end(); ++ i)
                {
                    if (lrfds)
                        FD_SET(*i, &rqreadfds);
                    if (lwfds)
                        FD_SET(*i, &rqwritefds);
                    if ((int)*i > max_fd)
                        max_fd = *i;
                }

                IF_HEAVY_LOGGING(const int prev_total = total);
                timeval tv;
                tv.tv_sec = 0;
                tv.tv_usec = 0;
                if (::select(max_fd + 1, &rqreadfds, &rqwritefds, NULL, &tv) > 0)
                {
                    for (set<SYSSOCKET>::const_iterator i = ed.m_sLocals.begin(); i != ed.m_sLocals.end(); ++ i)
                    {
                        if (lrfds && FD_ISSET(*i, &rqreadfds))
                        {
                            lrfds->insert(*i);
                            ++ total;
                        }
                        if (lwfds && FD_ISSET(*i, &rqwritefds))
                        {
                            lwfds->insert(*i);
                            ++ total;
                        }
                    }
                }

                HLOGC(ealog.Debug, log << "CEPoll::wait: select(otherSYS): picking up " << (total - prev_total)  << " ready fds.");
#endif
            }

        } // END-LOCK: m_EPollLock

        HLOGC(ealog.Debug, log << "CEPoll::wait: Total of " << total << " READY SOCKETS");

        if (total > 0)
            return total;

        if ((msTimeOut >= 0) && (count_microseconds(srt::sync::steady_clock::now() - entertime) >= msTimeOut * int64_t(1000)))
        {
            HLOGC(ealog.Debug, log << "EID:" << eid << ": TIMEOUT.");
            throw CUDTException(MJ_AGAIN, MN_XMTIMEOUT, 0);
        }

        const bool wait_signaled SRT_ATR_UNUSED = CGlobEvent::waitForEvent();
        HLOGC(ealog.Debug, log << "CEPoll::wait: EVENT WAITING: "
            << (wait_signaled ? "TRIGGERED" : "CHECKPOINT"));
    }

    return 0;
}

int srt::CEPoll::swait(CEPollDesc& d, map<SRTSOCKET, int>& st, int64_t msTimeOut, bool report_by_exception)
{
    {
        ScopedLock lg (m_EPollLock);
        if (!d.flags(SRT_EPOLL_ENABLE_EMPTY) && d.watch_empty() && msTimeOut < 0)
        {
            // no socket is being monitored, this may be a deadlock
            LOGC(ealog.Error, log << "EID:" << d.m_iID << " no sockets to check, this would deadlock");
            if (report_by_exception)
                throw CUDTException(MJ_NOTSUP, MN_EEMPTY, 0);
            return -1;
        }
    }

    st.clear();

    steady_clock::time_point entertime = steady_clock::now();
    while (true)
    {
        {
            // Not extracting separately because this function is
            // for internal use only and we state that the eid could
            // not be deleted or changed the target CEPollDesc in the
            // meantime.

            // Here we only prevent the pollset be updated simultaneously
            // with unstable reading. 
            ScopedLock lg (m_EPollLock);

            if (!d.flags(SRT_EPOLL_ENABLE_EMPTY) && d.watch_empty())
            {
                // Empty EID is not allowed, report error.
                throw CUDTException(MJ_NOTSUP, MN_EEMPTY);
            }

            if (!d.m_sLocals.empty())
            {
                // XXX Add error log
                // uwait should not be used with EIDs subscribed to system sockets
                throw CUDTException(MJ_NOTSUP, MN_INVAL);
            }

            bool empty = d.enotice_empty();

            if (!empty || msTimeOut == 0)
            {
                IF_HEAVY_LOGGING(ostringstream singles);
                // If msTimeOut == 0, it means that we need the information
                // immediately, we don't want to wait. Therefore in this case
                // report also when none is ready.
                int total = 0; // This is a list, so count it during iteration
                CEPollDesc::enotice_t::iterator i = d.enotice_begin();
                while (i != d.enotice_end())
                {
                    ++total;
                    st[i->fd] = i->events;
                    IF_HEAVY_LOGGING(singles << "@" << i->fd << ":");
                    IF_HEAVY_LOGGING(PrintEpollEvent(singles, i->events, i->parent->edgeOnly()));
                    const bool edged SRT_ATR_UNUSED = d.checkEdge(i++); // NOTE: potentially deletes `i`
                    IF_HEAVY_LOGGING(singles << (edged ? "<^> " : " "));
                }

                // Logging into 'singles' because it notifies as to whether
                // the edge-triggered event has been cleared
                HLOGC(ealog.Debug, log << "E" << d.m_iID << " rdy=" << total << ": "
                        << singles.str()
                        << " TRACKED: " << d.DisplayEpollWatch());
                return total;
            }
            // Don't report any updates because this check happens
            // extremely often.
        }

        if ((msTimeOut >= 0) && ((steady_clock::now() - entertime) >= microseconds_from(msTimeOut * int64_t(1000))))
        {
            HLOGC(ealog.Debug, log << "EID:" << d.m_iID << ": TIMEOUT.");
            if (report_by_exception)
                throw CUDTException(MJ_AGAIN, MN_XMTIMEOUT, 0);
            return 0; // meaning "none is ready"
        }

        CGlobEvent::waitForEvent();
    }

    return 0;
}

bool srt::CEPoll::empty(const CEPollDesc& d) const
{
    ScopedLock lg (m_EPollLock);
    return d.watch_empty();
}

int srt::CEPoll::release(const int eid)
{
   ScopedLock pg(m_EPollLock);

   map<int, CEPollDesc>::iterator i = m_mPolls.find(eid);
   if (i == m_mPolls.end())
      throw CUDTException(MJ_NOTSUP, MN_EIDINVAL);

   #ifdef LINUX
   // release local/system epoll descriptor
   ::close(i->second.m_iLocalID);
   #elif defined(BSD) || TARGET_OS_MAC
   ::close(i->second.m_iLocalID);
   #endif

   m_mPolls.erase(i);

   return 0;
}


int srt::CEPoll::update_events(const SRTSOCKET& uid, std::set<int>& eids, const int events, const bool enable)
{
    // As event flags no longer contain only event types, check now.
    if ((events & ~SRT_EPOLL_EVENTTYPES) != 0)
    {
        LOGC(eilog.Fatal, log << "epoll/update: IPE: 'events' parameter shall not contain special flags!");
        return -1; // still, ignored.
    }

    int nupdated = 0;
    vector<int> lost;

    IF_HEAVY_LOGGING(ostringstream debug);
    IF_HEAVY_LOGGING(debug << "epoll/update: @" << uid << " " << (enable ? "+" : "-"));
    IF_HEAVY_LOGGING(PrintEpollEvent(debug, events));

    ScopedLock pg (m_EPollLock);
    for (set<int>::iterator i = eids.begin(); i != eids.end(); ++ i)
    {
        map<int, CEPollDesc>::iterator p = m_mPolls.find(*i);
        if (p == m_mPolls.end())
        {
            HLOGC(eilog.Note, log << "epoll/update: E" << *i << " was deleted in the meantime");
            // EID invalid, though still present in the socket's subscriber list
            // (dangling in the socket). Postpone to fix the subscruption and continue.
            lost.push_back(*i);
            continue;
        }

        CEPollDesc& ed = p->second;

        // Check if this EID is subscribed for this socket.
        CEPollDesc::Wait* pwait = ed.watch_find(uid);
        if (!pwait)
        {
            // As this is mapped in the socket's data, it should be impossible.
            LOGC(eilog.Error, log << "epoll/update: IPE: update struck E"
                    << (*i) << " which is NOT SUBSCRIBED to @" << uid);
            continue;
        }

        IF_HEAVY_LOGGING(string tracking = " TRACKING: " + ed.DisplayEpollWatch());
        // compute new states

        // New state to be set into the permanent state
        const int newstate = enable ? pwait->state | events // SET event bits if enable
                              : pwait->state & (~events); // CLEAR event bits

        // compute states changes!
        int changes = pwait->state ^ newstate; // oldState XOR newState
        if (!changes)
        {
            HLOGC(eilog.Debug, log << debug.str() << ": E" << (*i)
                    << tracking << " NOT updated: no changes");
            continue; // no changes!
        }
        // assign new state
        pwait->state = newstate;
        // filter change relating what is watching
        changes &= pwait->watch;
        if (!changes)
        {
            HLOGC(eilog.Debug, log << debug.str() << ": E" << (*i)
                    << tracking << " NOT updated: not subscribed");
            continue; // no change watching
        }
        // set events changes!

        // This function will update the notice object associated with
        // the given events, that is:
        // - if enable, it will set event flags, possibly in a new notice object
        // - if !enable, it will clear event flags, possibly remove notice if resulted in 0
        ed.updateEventNotice(*pwait, uid, events, enable);
        ++nupdated;

        HLOGC(eilog.Debug, log << debug.str() << ": E" << (*i)
                << " TRACKING: " << ed.DisplayEpollWatch());
    }

    for (vector<int>::iterator i = lost.begin(); i != lost.end(); ++ i)
        eids.erase(*i);

    return nupdated;
}

// Debug use only.
#if ENABLE_HEAVY_LOGGING
namespace srt
{

static ostream& PrintEpollEvent(ostream& os, int events, int et_events)
{
    static pair<int, const char*> const namemap [] = {
        make_pair(SRT_EPOLL_IN, "R"),
        make_pair(SRT_EPOLL_OUT, "W"),
        make_pair(SRT_EPOLL_ERR, "E"),
        make_pair(SRT_EPOLL_UPDATE, "U")
    };

    int N = Size(namemap);

    for (int i = 0; i < N; ++i)
    {
        if (events & namemap[i].first)
        {
            os << "[";
            if (et_events & namemap[i].first)
                os << "^";
            os << namemap[i].second << "]";
        }
    }

    return os;
}

string DisplayEpollResults(const std::map<SRTSOCKET, int>& sockset)
{
    typedef map<SRTSOCKET, int> fmap_t;
    ostringstream os;
    for (fmap_t::const_iterator i = sockset.begin(); i != sockset.end(); ++i)
    {
        os << "@" << i->first << ":";
        PrintEpollEvent(os, i->second);
        os << " ";
    }

    return os.str();
}

string CEPollDesc::DisplayEpollWatch()
{
    ostringstream os;
    for (ewatch_t::const_iterator i = m_USockWatchState.begin(); i != m_USockWatchState.end(); ++i)
    {
        os << "@" << i->first << ":";
        PrintEpollEvent(os, i->second.watch, i->second.edge);
        os << " ";
    }

    return os.str();
}

} // namespace srt

#endif
