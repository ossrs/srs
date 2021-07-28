/* 
 * The contents of this file are subject to the Mozilla Public
 * License Version 1.1 (the "License"); you may not use this file
 * except in compliance with the License. You may obtain a copy of
 * the License at http://www.mozilla.org/MPL/
 * 
 * Software distributed under the License is distributed on an "AS
 * IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or
 * implied. See the License for the specific language governing
 * rights and limitations under the License.
 * 
 * The Original Code is the Netscape Portable Runtime library.
 * 
 * The Initial Developer of the Original Code is Netscape
 * Communications Corporation.  Portions created by Netscape are 
 * Copyright (C) 1994-2000 Netscape Communications Corporation.  All
 * Rights Reserved.
 * 
 * Contributor(s):  Silicon Graphics, Inc.
 *                  Yahoo! Inc.
 *
 * Alternatively, the contents of this file may be used under the
 * terms of the GNU General Public License Version 2 or later (the
 * "GPL"), in which case the provisions of the GPL are applicable 
 * instead of those above.  If you wish to allow use of your 
 * version of this file only under the terms of the GPL and not to
 * allow others to use your version of this file under the MPL,
 * indicate your decision by deleting the provisions above and
 * replace them with the notice and other provisions required by
 * the GPL.  If you do not delete the provisions above, a recipient
 * may use your version of this file under either the MPL or the
 * GPL.
 */

#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include "common.h"

#ifdef MD_HAVE_KQUEUE
#include <sys/event.h>
#endif
#ifdef MD_HAVE_EPOLL
#include <sys/epoll.h>
#endif

// Global stat.
#if defined(DEBUG) && defined(DEBUG_STATS)
unsigned long long _st_stat_epoll = 0;
unsigned long long _st_stat_epoll_zero = 0;
unsigned long long _st_stat_epoll_shake = 0;
unsigned long long _st_stat_epoll_spin = 0;
#endif

#if !defined(MD_HAVE_KQUEUE) && !defined(MD_HAVE_EPOLL) && !defined(MD_HAVE_SELECT)
    #error Only support epoll(for Linux), kqueue(for Darwin) or select(for Cygwin)
#endif


#ifdef MD_HAVE_SELECT
static struct _st_seldata {
    fd_set fd_read_set, fd_write_set, fd_exception_set;
    int fd_ref_cnts[FD_SETSIZE][3];
    int maxfd;
} *_st_select_data;

#define _ST_SELECT_MAX_OSFD      (_st_select_data->maxfd)
#define _ST_SELECT_READ_SET      (_st_select_data->fd_read_set)
#define _ST_SELECT_WRITE_SET     (_st_select_data->fd_write_set)
#define _ST_SELECT_EXCEP_SET     (_st_select_data->fd_exception_set)
#define _ST_SELECT_READ_CNT(fd)  (_st_select_data->fd_ref_cnts[fd][0])
#define _ST_SELECT_WRITE_CNT(fd) (_st_select_data->fd_ref_cnts[fd][1])
#define _ST_SELECT_EXCEP_CNT(fd) (_st_select_data->fd_ref_cnts[fd][2])
#endif


#ifdef MD_HAVE_KQUEUE
typedef struct _kq_fd_data {
    int rd_ref_cnt;
    int wr_ref_cnt;
    int revents;
} _kq_fd_data_t;

static struct _st_kqdata {
    _kq_fd_data_t *fd_data;
    struct kevent *evtlist;
    struct kevent *addlist;
    struct kevent *dellist;
    int fd_data_size;
    int evtlist_size;
    int addlist_size;
    int addlist_cnt;
    int dellist_size;
    int dellist_cnt;
    int kq;
} *_st_kq_data;

#ifndef ST_KQ_MIN_EVTLIST_SIZE
#define ST_KQ_MIN_EVTLIST_SIZE 64
#endif

#define _ST_KQ_READ_CNT(fd)      (_st_kq_data->fd_data[fd].rd_ref_cnt)
#define _ST_KQ_WRITE_CNT(fd)     (_st_kq_data->fd_data[fd].wr_ref_cnt)
#define _ST_KQ_REVENTS(fd)       (_st_kq_data->fd_data[fd].revents)
#endif  /* MD_HAVE_KQUEUE */


#ifdef MD_HAVE_EPOLL
typedef struct _epoll_fd_data {
    int rd_ref_cnt;
    int wr_ref_cnt;
    int ex_ref_cnt;
    int revents;
} _epoll_fd_data_t;

static struct _st_epolldata {
    _epoll_fd_data_t *fd_data;
    struct epoll_event *evtlist;
    int fd_data_size;
    int evtlist_size;
    int evtlist_cnt;
    int fd_hint;
    int epfd;
} *_st_epoll_data;

#ifndef ST_EPOLL_EVTLIST_SIZE
    /* Not a limit, just a hint */
    #define ST_EPOLL_EVTLIST_SIZE 4096
#endif

#define _ST_EPOLL_READ_CNT(fd)   (_st_epoll_data->fd_data[fd].rd_ref_cnt)
#define _ST_EPOLL_WRITE_CNT(fd)  (_st_epoll_data->fd_data[fd].wr_ref_cnt)
#define _ST_EPOLL_EXCEP_CNT(fd)  (_st_epoll_data->fd_data[fd].ex_ref_cnt)
#define _ST_EPOLL_REVENTS(fd)    (_st_epoll_data->fd_data[fd].revents)

#define _ST_EPOLL_READ_BIT(fd)   (_ST_EPOLL_READ_CNT(fd) ? EPOLLIN : 0)
#define _ST_EPOLL_WRITE_BIT(fd)  (_ST_EPOLL_WRITE_CNT(fd) ? EPOLLOUT : 0)
#define _ST_EPOLL_EXCEP_BIT(fd)  (_ST_EPOLL_EXCEP_CNT(fd) ? EPOLLPRI : 0)
#define _ST_EPOLL_EVENTS(fd) \
    (_ST_EPOLL_READ_BIT(fd)|_ST_EPOLL_WRITE_BIT(fd)|_ST_EPOLL_EXCEP_BIT(fd))

#endif  /* MD_HAVE_EPOLL */

_st_eventsys_t *_st_eventsys = NULL;


#ifdef MD_HAVE_SELECT
/*****************************************
 * select event system
 */

ST_HIDDEN int _st_select_init(void)
{
    _st_select_data = (struct _st_seldata *) malloc(sizeof(*_st_select_data));
    if (!_st_select_data)
        return -1;

    memset(_st_select_data, 0, sizeof(*_st_select_data));
    _st_select_data->maxfd = -1;

    return 0;
}

ST_HIDDEN int _st_select_pollset_add(struct pollfd *pds, int npds)
{
    struct pollfd *pd;
    struct pollfd *epd = pds + npds;

    /* Do checks up front */
    for (pd = pds; pd < epd; pd++) {
        if (pd->fd < 0 || pd->fd >= FD_SETSIZE || !pd->events ||
            (pd->events & ~(POLLIN | POLLOUT | POLLPRI))) {
            errno = EINVAL;
            return -1;
        }
    }

    for (pd = pds; pd < epd; pd++) {
        if (pd->events & POLLIN) {
            FD_SET(pd->fd, &_ST_SELECT_READ_SET);
            _ST_SELECT_READ_CNT(pd->fd)++;
        }
        if (pd->events & POLLOUT) {
            FD_SET(pd->fd, &_ST_SELECT_WRITE_SET);
            _ST_SELECT_WRITE_CNT(pd->fd)++;
        }
        if (pd->events & POLLPRI) {
            FD_SET(pd->fd, &_ST_SELECT_EXCEP_SET);
            _ST_SELECT_EXCEP_CNT(pd->fd)++;
        }
        if (_ST_SELECT_MAX_OSFD < pd->fd)
            _ST_SELECT_MAX_OSFD = pd->fd;
    }

    return 0;
}

ST_HIDDEN void _st_select_pollset_del(struct pollfd *pds, int npds)
{
    struct pollfd *pd;
    struct pollfd *epd = pds + npds;

    for (pd = pds; pd < epd; pd++) {
        if (pd->events & POLLIN) {
            if (--_ST_SELECT_READ_CNT(pd->fd) == 0)
                FD_CLR(pd->fd, &_ST_SELECT_READ_SET);
        }
        if (pd->events & POLLOUT) {
            if (--_ST_SELECT_WRITE_CNT(pd->fd) == 0)
                FD_CLR(pd->fd, &_ST_SELECT_WRITE_SET);
        }
        if (pd->events & POLLPRI) {
            if (--_ST_SELECT_EXCEP_CNT(pd->fd) == 0)
                FD_CLR(pd->fd, &_ST_SELECT_EXCEP_SET);
        }
    }
}

ST_HIDDEN void _st_select_find_bad_fd(void)
{
    _st_clist_t *q;
    _st_pollq_t *pq;
    int notify;
    struct pollfd *pds, *epds;
    int pq_max_osfd, osfd;
    short events;

    _ST_SELECT_MAX_OSFD = -1;

    for (q = _ST_IOQ.next; q != &_ST_IOQ; q = q->next) {
        pq = _ST_POLLQUEUE_PTR(q);
        notify = 0;
        epds = pq->pds + pq->npds;
        pq_max_osfd = -1;

        for (pds = pq->pds; pds < epds; pds++) {
            osfd = pds->fd;
            pds->revents = 0;
            if (pds->events == 0)
                continue;
            if (fcntl(osfd, F_GETFL, 0) < 0) {
                pds->revents = POLLNVAL;
                notify = 1;
            }
            if (osfd > pq_max_osfd) {
                pq_max_osfd = osfd;
            }
        }

        if (notify) {
            ST_REMOVE_LINK(&pq->links);
            pq->on_ioq = 0;
            /*
             * Decrement the count of descriptors for each descriptor/event
             * because this I/O request is being removed from the ioq
             */
            for (pds = pq->pds; pds < epds; pds++) {
                osfd = pds->fd;
                events = pds->events;
                if (events & POLLIN) {
                    if (--_ST_SELECT_READ_CNT(osfd) == 0) {
                        FD_CLR(osfd, &_ST_SELECT_READ_SET);
                    }
                }
                if (events & POLLOUT) {
                    if (--_ST_SELECT_WRITE_CNT(osfd) == 0) {
                        FD_CLR(osfd, &_ST_SELECT_WRITE_SET);
                    }
                }
                if (events & POLLPRI) {
                    if (--_ST_SELECT_EXCEP_CNT(osfd) == 0) {
                        FD_CLR(osfd, &_ST_SELECT_EXCEP_SET);
                    }
                }
            }

            if (pq->thread->flags & _ST_FL_ON_SLEEPQ)
                _ST_DEL_SLEEPQ(pq->thread);
            pq->thread->state = _ST_ST_RUNNABLE;
            _ST_ADD_RUNQ(pq->thread);
        } else {
            if (_ST_SELECT_MAX_OSFD < pq_max_osfd)
                _ST_SELECT_MAX_OSFD = pq_max_osfd;
        }
    }
}

ST_HIDDEN void _st_select_dispatch(void)
{
    struct timeval timeout, *tvp;
    fd_set r, w, e;
    fd_set *rp, *wp, *ep;
    int nfd, pq_max_osfd, osfd;
    _st_clist_t *q;
    st_utime_t min_timeout;
    _st_pollq_t *pq;
    int notify;
    struct pollfd *pds, *epds;
    short events, revents;

    /*
     * Assignment of fd_sets
     */
    r = _ST_SELECT_READ_SET;
    w = _ST_SELECT_WRITE_SET;
    e = _ST_SELECT_EXCEP_SET;

    rp = &r;
    wp = &w;
    ep = &e;

    if (_ST_SLEEPQ == NULL) {
        tvp = NULL;
    } else {
        min_timeout = (_ST_SLEEPQ->due <= _ST_LAST_CLOCK) ? 0 :
                      (_ST_SLEEPQ->due - _ST_LAST_CLOCK);
        timeout.tv_sec  = (int) (min_timeout / 1000000);
        timeout.tv_usec = (int) (min_timeout % 1000000);
        tvp = &timeout;
    }

    /* Check for I/O operations */
    nfd = select(_ST_SELECT_MAX_OSFD + 1, rp, wp, ep, tvp);

    /* Notify threads that are associated with the selected descriptors */
    if (nfd > 0) {
        _ST_SELECT_MAX_OSFD = -1;
        for (q = _ST_IOQ.next; q != &_ST_IOQ; q = q->next) {
            pq = _ST_POLLQUEUE_PTR(q);
            notify = 0;
            epds = pq->pds + pq->npds;
            pq_max_osfd = -1;

            for (pds = pq->pds; pds < epds; pds++) {
                osfd = pds->fd;
                events = pds->events;
                revents = 0;
                if ((events & POLLIN) && FD_ISSET(osfd, rp)) {
                    revents |= POLLIN;
                }
                if ((events & POLLOUT) && FD_ISSET(osfd, wp)) {
                    revents |= POLLOUT;
                }
                if ((events & POLLPRI) && FD_ISSET(osfd, ep)) {
                    revents |= POLLPRI;
                }
                pds->revents = revents;
                if (revents) {
                    notify = 1;
                }
                if (osfd > pq_max_osfd) {
                    pq_max_osfd = osfd;
                }
            }
            if (notify) {
                ST_REMOVE_LINK(&pq->links);
                pq->on_ioq = 0;
                /*
                 * Decrement the count of descriptors for each descriptor/event
                 * because this I/O request is being removed from the ioq
                 */
                for (pds = pq->pds; pds < epds; pds++) {
                    osfd = pds->fd;
                    events = pds->events;
                    if (events & POLLIN) {
                        if (--_ST_SELECT_READ_CNT(osfd) == 0) {
                            FD_CLR(osfd, &_ST_SELECT_READ_SET);
                        }
                    }
                    if (events & POLLOUT) {
                        if (--_ST_SELECT_WRITE_CNT(osfd) == 0) {
                            FD_CLR(osfd, &_ST_SELECT_WRITE_SET);
                        }
                    }
                    if (events & POLLPRI) {
                        if (--_ST_SELECT_EXCEP_CNT(osfd) == 0) {
                            FD_CLR(osfd, &_ST_SELECT_EXCEP_SET);
                        }
                    }
                }

                if (pq->thread->flags & _ST_FL_ON_SLEEPQ)
                    _ST_DEL_SLEEPQ(pq->thread);
                pq->thread->state = _ST_ST_RUNNABLE;
                _ST_ADD_RUNQ(pq->thread);
            } else {
                if (_ST_SELECT_MAX_OSFD < pq_max_osfd)
                    _ST_SELECT_MAX_OSFD = pq_max_osfd;
            }
        }
    } else if (nfd < 0) {
        /*
         * It can happen when a thread closes file descriptor
         * that is being used by some other thread -- BAD!
         */
        if (errno == EBADF)
            _st_select_find_bad_fd();
    }
}

ST_HIDDEN int _st_select_fd_new(int osfd)
{
    if (osfd >= FD_SETSIZE) {
        errno = EMFILE;
        return -1;
    }

    return 0;
}

ST_HIDDEN int _st_select_fd_close(int osfd)
{
    if (_ST_SELECT_READ_CNT(osfd) || _ST_SELECT_WRITE_CNT(osfd) ||
        _ST_SELECT_EXCEP_CNT(osfd)) {
        errno = EBUSY;
        return -1;
    }

    return 0;
}

ST_HIDDEN int _st_select_fd_getlimit(void)
{
    return FD_SETSIZE;
}

static _st_eventsys_t _st_select_eventsys = {
        "select",
        ST_EVENTSYS_SELECT,
        _st_select_init,
        _st_select_dispatch,
        _st_select_pollset_add,
        _st_select_pollset_del,
        _st_select_fd_new,
        _st_select_fd_close,
        _st_select_fd_getlimit
};
#endif


#ifdef MD_HAVE_KQUEUE
/*****************************************
 * kqueue event system
 */
                    
ST_HIDDEN int _st_kq_init(void)
{
    int err = 0;
    int rv = 0;

    _st_kq_data = (struct _st_kqdata *) calloc(1, sizeof(*_st_kq_data));
    if (!_st_kq_data)
        return -1;

    if ((_st_kq_data->kq = kqueue()) < 0) {
        err = errno;
        rv = -1;
        goto cleanup_kq;
    }
    fcntl(_st_kq_data->kq, F_SETFD, FD_CLOEXEC);

    /*
     * Allocate file descriptor data array.
     * FD_SETSIZE looks like good initial size.
     */
    _st_kq_data->fd_data_size = FD_SETSIZE;
    _st_kq_data->fd_data = (_kq_fd_data_t *)calloc(_st_kq_data->fd_data_size, sizeof(_kq_fd_data_t));
    if (!_st_kq_data->fd_data) {
        err = errno;
        rv = -1;
        goto cleanup_kq;
    }

    /* Allocate event lists */
    _st_kq_data->evtlist_size = ST_KQ_MIN_EVTLIST_SIZE;
    _st_kq_data->evtlist = (struct kevent *)malloc(_st_kq_data->evtlist_size * sizeof(struct kevent));
    _st_kq_data->addlist_size = ST_KQ_MIN_EVTLIST_SIZE;
    _st_kq_data->addlist = (struct kevent *)malloc(_st_kq_data->addlist_size * sizeof(struct kevent));
    _st_kq_data->dellist_size = ST_KQ_MIN_EVTLIST_SIZE;
    _st_kq_data->dellist = (struct kevent *)malloc(_st_kq_data->dellist_size * sizeof(struct kevent));
    if (!_st_kq_data->evtlist || !_st_kq_data->addlist ||
        !_st_kq_data->dellist) {
        err = ENOMEM;
        rv = -1;
    }

 cleanup_kq:
    if (rv < 0) {
        if (_st_kq_data->kq >= 0)
            close(_st_kq_data->kq);
        free(_st_kq_data->fd_data);
        free(_st_kq_data->evtlist);
        free(_st_kq_data->addlist);
        free(_st_kq_data->dellist);
        free(_st_kq_data);
        _st_kq_data = NULL;
        errno = err;
    }

    return rv;
}

ST_HIDDEN int _st_kq_fd_data_expand(int maxfd)
{
    _kq_fd_data_t *ptr;
    int n = _st_kq_data->fd_data_size;

    while (maxfd >= n)
        n <<= 1;

    ptr = (_kq_fd_data_t *)realloc(_st_kq_data->fd_data, n * sizeof(_kq_fd_data_t));
    if (!ptr)
        return -1;

    memset(ptr + _st_kq_data->fd_data_size, 0, (n - _st_kq_data->fd_data_size) * sizeof(_kq_fd_data_t));

    _st_kq_data->fd_data = ptr;
    _st_kq_data->fd_data_size = n;

    return 0;
}

ST_HIDDEN int _st_kq_addlist_expand(int avail)
{
    struct kevent *ptr;
    int n = _st_kq_data->addlist_size;

    while (avail > n - _st_kq_data->addlist_cnt)
        n <<= 1;

    ptr = (struct kevent *)realloc(_st_kq_data->addlist, n * sizeof(struct kevent));
    if (!ptr)
        return -1;

    _st_kq_data->addlist = ptr;
    _st_kq_data->addlist_size = n;

    /*
     * Try to expand the result event list too
     * (although we don't have to do it).
     */
    ptr = (struct kevent *)realloc(_st_kq_data->evtlist, n * sizeof(struct kevent));
    if (ptr) {
        _st_kq_data->evtlist = ptr;
        _st_kq_data->evtlist_size = n;
    }

    return 0;
}

ST_HIDDEN void _st_kq_addlist_add(const struct kevent *kev)
{
    ST_ASSERT(_st_kq_data->addlist_cnt < _st_kq_data->addlist_size);
    memcpy(_st_kq_data->addlist + _st_kq_data->addlist_cnt, kev, sizeof(struct kevent));
    _st_kq_data->addlist_cnt++;
}

ST_HIDDEN void _st_kq_dellist_add(const struct kevent *kev)
{
    int n = _st_kq_data->dellist_size;

    if (_st_kq_data->dellist_cnt >= n) {
        struct kevent *ptr;

        n <<= 1;
        ptr = (struct kevent *)realloc(_st_kq_data->dellist, n * sizeof(struct kevent));
        if (!ptr) {
            /* See comment in _st_kq_pollset_del() */
            return;
        }

        _st_kq_data->dellist = ptr;
        _st_kq_data->dellist_size = n;
    }

    memcpy(_st_kq_data->dellist + _st_kq_data->dellist_cnt, kev, sizeof(struct kevent));
    _st_kq_data->dellist_cnt++;
}

ST_HIDDEN int _st_kq_pollset_add(struct pollfd *pds, int npds)
{
    struct kevent kev;
    struct pollfd *pd;
    struct pollfd *epd = pds + npds;

    /*
     * Pollset adding is "atomic". That is, either it succeeded for
     * all descriptors in the set or it failed. It means that we
     * need to do all the checks up front so we don't have to
     * "unwind" if adding of one of the descriptors failed.
     */
    for (pd = pds; pd < epd; pd++) {
        /* POLLIN and/or POLLOUT must be set, but nothing else */
        if (pd->fd < 0 || !pd->events || (pd->events & ~(POLLIN | POLLOUT))) {
            errno = EINVAL;
            return -1;
        }
        if (pd->fd >= _st_kq_data->fd_data_size &&
            _st_kq_fd_data_expand(pd->fd) < 0)
            return -1;
    }

    /*
     * Make sure we have enough room in the addlist for twice as many
     * descriptors as in the pollset (for both READ and WRITE filters).
     */
    npds <<= 1;
    if (npds > _st_kq_data->addlist_size - _st_kq_data->addlist_cnt && _st_kq_addlist_expand(npds) < 0)
        return -1;

    for (pd = pds; pd < epd; pd++) {
        if ((pd->events & POLLIN) && (_ST_KQ_READ_CNT(pd->fd)++ == 0)) {
            memset(&kev, 0, sizeof(kev));
            kev.ident = pd->fd;
            kev.filter = EVFILT_READ;
#ifdef NOTE_EOF
            /* Make it behave like select() and poll() */
            kev.fflags = NOTE_EOF;
#endif
            kev.flags = (EV_ADD | EV_ONESHOT);
            _st_kq_addlist_add(&kev);
        }
        if ((pd->events & POLLOUT) && (_ST_KQ_WRITE_CNT(pd->fd)++ == 0)) {
            memset(&kev, 0, sizeof(kev));
            kev.ident = pd->fd;
            kev.filter = EVFILT_WRITE;
            kev.flags = (EV_ADD | EV_ONESHOT);
            _st_kq_addlist_add(&kev);
        }
    }

    return 0;
}

ST_HIDDEN void _st_kq_pollset_del(struct pollfd *pds, int npds)
{
    struct kevent kev;
    struct pollfd *pd;
    struct pollfd *epd = pds + npds;

    /*
     * It's OK if deleting fails because a descriptor will either be
     * closed or fire only once (we set EV_ONESHOT flag).
     */
    _st_kq_data->dellist_cnt = 0;
    for (pd = pds; pd < epd; pd++) {
        if ((pd->events & POLLIN) && (--_ST_KQ_READ_CNT(pd->fd) == 0)) {
            memset(&kev, 0, sizeof(kev));
            kev.ident = pd->fd;
            kev.filter = EVFILT_READ;
            kev.flags = EV_DELETE;
            _st_kq_dellist_add(&kev);
        }
        if ((pd->events & POLLOUT) && (--_ST_KQ_WRITE_CNT(pd->fd) == 0)) {
            memset(&kev, 0, sizeof(kev));
            kev.ident = pd->fd;
            kev.filter = EVFILT_WRITE;
            kev.flags = EV_DELETE;
            _st_kq_dellist_add(&kev);
        }
    }

    if (_st_kq_data->dellist_cnt > 0) {
        /*
         * We do "synchronous" kqueue deletes to avoid deleting
         * closed descriptors and other possible problems.
         */
        int rv;
        do {
            /* This kevent() won't block since result list size is 0 */
            rv = kevent(_st_kq_data->kq, _st_kq_data->dellist, _st_kq_data->dellist_cnt, NULL, 0, NULL);
        } while (rv < 0 && errno == EINTR);
    }
}

ST_HIDDEN void _st_kq_dispatch(void)
{
    struct timespec timeout, *tsp;
    struct kevent kev;
    st_utime_t min_timeout;
    _st_clist_t *q;
    _st_pollq_t *pq;
    struct pollfd *pds, *epds;
    int nfd, i, osfd, notify, filter;
    short events, revents;

    if (_ST_SLEEPQ == NULL) {
        tsp = NULL;
    } else {
        min_timeout = (_ST_SLEEPQ->due <= _ST_LAST_CLOCK) ? 0 : (_ST_SLEEPQ->due - _ST_LAST_CLOCK);
        timeout.tv_sec  = (time_t) (min_timeout / 1000000);
        timeout.tv_nsec = (long) ((min_timeout % 1000000) * 1000);
        tsp = &timeout;
    }

    /* Check for I/O operations */
    nfd = kevent(_st_kq_data->kq,
                 _st_kq_data->addlist, _st_kq_data->addlist_cnt,
                 _st_kq_data->evtlist, _st_kq_data->evtlist_size, tsp);

    _st_kq_data->addlist_cnt = 0;

    if (nfd > 0) {
        for (i = 0; i < nfd; i++) {
            osfd = _st_kq_data->evtlist[i].ident;
            filter = _st_kq_data->evtlist[i].filter;

            if (filter == EVFILT_READ) {
                _ST_KQ_REVENTS(osfd) |= POLLIN;
            } else if (filter == EVFILT_WRITE) {
                _ST_KQ_REVENTS(osfd) |= POLLOUT;
            }
            if (_st_kq_data->evtlist[i].flags & EV_ERROR) {
                if (_st_kq_data->evtlist[i].data == EBADF) {
                    _ST_KQ_REVENTS(osfd) |= POLLNVAL;
                } else {
                    _ST_KQ_REVENTS(osfd) |= POLLERR;
                }
            }
        }

        _st_kq_data->dellist_cnt = 0;

        for (q = _ST_IOQ.next; q != &_ST_IOQ; q = q->next) {
            pq = _ST_POLLQUEUE_PTR(q);
            notify = 0;
            epds = pq->pds + pq->npds;
                     
            for (pds = pq->pds; pds < epds; pds++) {
                osfd = pds->fd;
                events = pds->events;
                revents = (short)(_ST_KQ_REVENTS(osfd) & ~(POLLIN | POLLOUT));
                if ((events & POLLIN) && (_ST_KQ_REVENTS(osfd) & POLLIN)) {
                    revents |= POLLIN;
                }
                if ((events & POLLOUT) && (_ST_KQ_REVENTS(osfd) & POLLOUT)) {
                    revents |= POLLOUT;
                }
                pds->revents = revents;
                if (revents) {
                    notify = 1;
                }
            }
            if (notify) {
                ST_REMOVE_LINK(&pq->links);
                pq->on_ioq = 0;
                for (pds = pq->pds; pds < epds; pds++) {
                    osfd = pds->fd;
                    events = pds->events;
                    /*
                     * We set EV_ONESHOT flag so we only need to delete
                     * descriptor if it didn't fire.
                     */
                    if ((events & POLLIN) && (--_ST_KQ_READ_CNT(osfd) == 0) && ((_ST_KQ_REVENTS(osfd) & POLLIN) == 0)) {
                        memset(&kev, 0, sizeof(kev));
                        kev.ident = osfd;
                        kev.filter = EVFILT_READ;
                        kev.flags = EV_DELETE;
                        _st_kq_dellist_add(&kev);
                    }
                    if ((events & POLLOUT) && (--_ST_KQ_WRITE_CNT(osfd) == 0) && ((_ST_KQ_REVENTS(osfd) & POLLOUT) == 0)) {
                        memset(&kev, 0, sizeof(kev));
                        kev.ident = osfd;
                        kev.filter = EVFILT_WRITE;
                        kev.flags = EV_DELETE;
                        _st_kq_dellist_add(&kev);
                    }
                }

                if (pq->thread->flags & _ST_FL_ON_SLEEPQ)
                    _ST_DEL_SLEEPQ(pq->thread);
                pq->thread->state = _ST_ST_RUNNABLE;
                _ST_ADD_RUNQ(pq->thread);
            }
        }

        if (_st_kq_data->dellist_cnt > 0) {
            int rv;
            do {
                /* This kevent() won't block since result list size is 0 */
                rv = kevent(_st_kq_data->kq, _st_kq_data->dellist, _st_kq_data->dellist_cnt, NULL, 0, NULL);
            } while (rv < 0 && errno == EINTR);
        }

        for (i = 0; i < nfd; i++) {
            osfd = _st_kq_data->evtlist[i].ident;
            _ST_KQ_REVENTS(osfd) = 0;
        }
    }
}

ST_HIDDEN int _st_kq_fd_new(int osfd)
{
    if (osfd >= _st_kq_data->fd_data_size && _st_kq_fd_data_expand(osfd) < 0)
        return -1;

    return 0;
}

ST_HIDDEN int _st_kq_fd_close(int osfd)
{
    if (_ST_KQ_READ_CNT(osfd) || _ST_KQ_WRITE_CNT(osfd)) {
        errno = EBUSY;
        return -1;
    }

    return 0;
}

ST_HIDDEN int _st_kq_fd_getlimit(void)
{
    /* zero means no specific limit */
    return 0;
}

static _st_eventsys_t _st_kq_eventsys = {
    "kqueue",
    ST_EVENTSYS_ALT,
    _st_kq_init,
    _st_kq_dispatch,
    _st_kq_pollset_add,
    _st_kq_pollset_del,
    _st_kq_fd_new,
    _st_kq_fd_close,  
    _st_kq_fd_getlimit
};
#endif  /* MD_HAVE_KQUEUE */


#ifdef MD_HAVE_EPOLL
/*****************************************
 * epoll event system
 */

ST_HIDDEN int _st_epoll_init(void)
{
    int fdlim;
    int err = 0;
    int rv = 0;

    _st_epoll_data = (struct _st_epolldata *) calloc(1, sizeof(*_st_epoll_data));
    if (!_st_epoll_data)
        return -1;

    fdlim = st_getfdlimit();
    _st_epoll_data->fd_hint = (fdlim > 0 && fdlim < ST_EPOLL_EVTLIST_SIZE) ? fdlim : ST_EPOLL_EVTLIST_SIZE;

    if ((_st_epoll_data->epfd = epoll_create(_st_epoll_data->fd_hint)) < 0) {
        err = errno;
        rv = -1;
        goto cleanup_epoll;
    }
    fcntl(_st_epoll_data->epfd, F_SETFD, FD_CLOEXEC);

    /* Allocate file descriptor data array */
    _st_epoll_data->fd_data_size = _st_epoll_data->fd_hint;
    _st_epoll_data->fd_data = (_epoll_fd_data_t *)calloc(_st_epoll_data->fd_data_size, sizeof(_epoll_fd_data_t));
    if (!_st_epoll_data->fd_data) {
        err = errno;
        rv = -1;
        goto cleanup_epoll;
    }

    /* Allocate event lists */
    _st_epoll_data->evtlist_size = _st_epoll_data->fd_hint;
    _st_epoll_data->evtlist = (struct epoll_event *)malloc(_st_epoll_data->evtlist_size * sizeof(struct epoll_event));
    if (!_st_epoll_data->evtlist) {
        err = errno;
        rv = -1;
    }

 cleanup_epoll:
    if (rv < 0) {
        if (_st_epoll_data->epfd >= 0)
            close(_st_epoll_data->epfd);
        free(_st_epoll_data->fd_data);
        free(_st_epoll_data->evtlist);
        free(_st_epoll_data);
        _st_epoll_data = NULL;
        errno = err;
    }

    return rv;
}

ST_HIDDEN int _st_epoll_fd_data_expand(int maxfd)
{
    _epoll_fd_data_t *ptr;
    int n = _st_epoll_data->fd_data_size;

    while (maxfd >= n)
        n <<= 1;

    ptr = (_epoll_fd_data_t *)realloc(_st_epoll_data->fd_data, n * sizeof(_epoll_fd_data_t));
    if (!ptr)
        return -1;

    memset(ptr + _st_epoll_data->fd_data_size, 0, (n - _st_epoll_data->fd_data_size) * sizeof(_epoll_fd_data_t));

    _st_epoll_data->fd_data = ptr;
    _st_epoll_data->fd_data_size = n;

    return 0;
}

ST_HIDDEN void _st_epoll_evtlist_expand(void)
{
    struct epoll_event *ptr;
    int n = _st_epoll_data->evtlist_size;

    while (_st_epoll_data->evtlist_cnt > n)
        n <<= 1;

    ptr = (struct epoll_event *)realloc(_st_epoll_data->evtlist, n * sizeof(struct epoll_event));
    if (ptr) {
        _st_epoll_data->evtlist = ptr;
        _st_epoll_data->evtlist_size = n;
    }
}

ST_HIDDEN void _st_epoll_pollset_del(struct pollfd *pds, int npds)
{
    struct epoll_event ev;
    struct pollfd *pd;
    struct pollfd *epd = pds + npds;
    int old_events, events, op;

    /*
     * It's more or less OK if deleting fails because a descriptor
     * will either be closed or deleted in dispatch function after
     * it fires.
     */
    for (pd = pds; pd < epd; pd++) {
        old_events = _ST_EPOLL_EVENTS(pd->fd);

        if (pd->events & POLLIN)
            _ST_EPOLL_READ_CNT(pd->fd)--;
        if (pd->events & POLLOUT)
            _ST_EPOLL_WRITE_CNT(pd->fd)--;
        if (pd->events & POLLPRI)
            _ST_EPOLL_EXCEP_CNT(pd->fd)--;

        events = _ST_EPOLL_EVENTS(pd->fd);
        /*
         * The _ST_EPOLL_REVENTS check below is needed so we can use
         * this function inside dispatch(). Outside of dispatch()
         * _ST_EPOLL_REVENTS is always zero for all descriptors.
         */
        if (events != old_events && _ST_EPOLL_REVENTS(pd->fd) == 0) {
            op = events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
            ev.events = events;
            ev.data.fd = pd->fd;
            if (epoll_ctl(_st_epoll_data->epfd, op, pd->fd, &ev) == 0 && op == EPOLL_CTL_DEL) {
                _st_epoll_data->evtlist_cnt--;
            }
        }
    }
}

ST_HIDDEN int _st_epoll_pollset_add(struct pollfd *pds, int npds)
{
    struct epoll_event ev;
    int i, fd;
    int old_events, events, op;

    /* Do as many checks as possible up front */
    for (i = 0; i < npds; i++) {
        fd = pds[i].fd;
        if (fd < 0 || !pds[i].events ||
            (pds[i].events & ~(POLLIN | POLLOUT | POLLPRI))) {
            errno = EINVAL;
            return -1;
        }
        if (fd >= _st_epoll_data->fd_data_size && _st_epoll_fd_data_expand(fd) < 0)
            return -1;
    }

    for (i = 0; i < npds; i++) {
        fd = pds[i].fd;
        old_events = _ST_EPOLL_EVENTS(fd);

        if (pds[i].events & POLLIN)
            _ST_EPOLL_READ_CNT(fd)++;
        if (pds[i].events & POLLOUT)
            _ST_EPOLL_WRITE_CNT(fd)++;
        if (pds[i].events & POLLPRI)
            _ST_EPOLL_EXCEP_CNT(fd)++;

        events = _ST_EPOLL_EVENTS(fd);
        if (events != old_events) {
            op = old_events ? EPOLL_CTL_MOD : EPOLL_CTL_ADD;
            ev.events = events;
            ev.data.fd = fd;
            if (epoll_ctl(_st_epoll_data->epfd, op, fd, &ev) < 0 && (op != EPOLL_CTL_ADD || errno != EEXIST))
                break;
            if (op == EPOLL_CTL_ADD) {
                _st_epoll_data->evtlist_cnt++;
                if (_st_epoll_data->evtlist_cnt > _st_epoll_data->evtlist_size)
                    _st_epoll_evtlist_expand();
            }
        }
    }

    if (i < npds) {
        /* Error */
        int err = errno;
        /* Unroll the state */
        _st_epoll_pollset_del(pds, i + 1);
        errno = err;
        return -1;
    }

    return 0;
}

ST_HIDDEN void _st_epoll_dispatch(void)
{
    st_utime_t min_timeout;
    _st_clist_t *q;
    _st_pollq_t *pq;
    struct pollfd *pds, *epds;
    struct epoll_event ev;
    int timeout, nfd, i, osfd, notify;
    int events, op;
    short revents;

    #if defined(DEBUG) && defined(DEBUG_STATS)
    ++_st_stat_epoll;
    #endif

    if (_ST_SLEEPQ == NULL) {
        timeout = -1;
    } else {
        min_timeout = (_ST_SLEEPQ->due <= _ST_LAST_CLOCK) ? 0 : (_ST_SLEEPQ->due - _ST_LAST_CLOCK);
        timeout = (int) (min_timeout / 1000);

        // At least wait 1ms when <1ms, to avoid epoll_wait spin loop.
        if (timeout == 0) {
            #if defined(DEBUG) && defined(DEBUG_STATS)
            ++_st_stat_epoll_zero;
            #endif

            if (min_timeout > 0) {
                #if defined(DEBUG) && defined(DEBUG_STATS)
                ++_st_stat_epoll_shake;
                #endif

                timeout = 1;
            }
        }
    }

    /* Check for I/O operations */
    nfd = epoll_wait(_st_epoll_data->epfd, _st_epoll_data->evtlist, _st_epoll_data->evtlist_size, timeout);

    #if defined(DEBUG) && defined(DEBUG_STATS)
    if (nfd <= 0) {
        ++_st_stat_epoll_spin;
    }
    #endif

    if (nfd > 0) {
        for (i = 0; i < nfd; i++) {
            osfd = _st_epoll_data->evtlist[i].data.fd;
            _ST_EPOLL_REVENTS(osfd) = _st_epoll_data->evtlist[i].events;
            if (_ST_EPOLL_REVENTS(osfd) & (EPOLLERR | EPOLLHUP)) {
                /* Also set I/O bits on error */
                _ST_EPOLL_REVENTS(osfd) |= _ST_EPOLL_EVENTS(osfd);
            }
        }

        for (q = _ST_IOQ.next; q != &_ST_IOQ; q = q->next) {
            pq = _ST_POLLQUEUE_PTR(q);
            notify = 0;
            epds = pq->pds + pq->npds;

            for (pds = pq->pds; pds < epds; pds++) {
                if (_ST_EPOLL_REVENTS(pds->fd) == 0) {
                    pds->revents = 0;
                    continue;
                }
                osfd = pds->fd;
                events = pds->events;
                revents = 0;
                if ((events & POLLIN) && (_ST_EPOLL_REVENTS(osfd) & EPOLLIN))
                    revents |= POLLIN;
                if ((events & POLLOUT) && (_ST_EPOLL_REVENTS(osfd) & EPOLLOUT))
                    revents |= POLLOUT;
                if ((events & POLLPRI) && (_ST_EPOLL_REVENTS(osfd) & EPOLLPRI))
                    revents |= POLLPRI;
                if (_ST_EPOLL_REVENTS(osfd) & EPOLLERR)
                    revents |= POLLERR;
                if (_ST_EPOLL_REVENTS(osfd) & EPOLLHUP)
                    revents |= POLLHUP;

                pds->revents = revents;
                if (revents) {
                    notify = 1;
                }
            }
            if (notify) {
                ST_REMOVE_LINK(&pq->links);
                pq->on_ioq = 0;
                /*
                 * Here we will only delete/modify descriptors that
                 * didn't fire (see comments in _st_epoll_pollset_del()).
                 */
                _st_epoll_pollset_del(pq->pds, pq->npds);

                if (pq->thread->flags & _ST_FL_ON_SLEEPQ)
                    _ST_DEL_SLEEPQ(pq->thread);
                pq->thread->state = _ST_ST_RUNNABLE;
                _ST_ADD_RUNQ(pq->thread);
            }
        }

        for (i = 0; i < nfd; i++) {
            /* Delete/modify descriptors that fired */
            osfd = _st_epoll_data->evtlist[i].data.fd;
            _ST_EPOLL_REVENTS(osfd) = 0;
            events = _ST_EPOLL_EVENTS(osfd);
            op = events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
            ev.events = events;
            ev.data.fd = osfd;
            if (epoll_ctl(_st_epoll_data->epfd, op, osfd, &ev) == 0 && op == EPOLL_CTL_DEL) {
                _st_epoll_data->evtlist_cnt--;
            }
        }
    }
}

ST_HIDDEN int _st_epoll_fd_new(int osfd)
{
    if (osfd >= _st_epoll_data->fd_data_size && _st_epoll_fd_data_expand(osfd) < 0)
        return -1;

    return 0;   
}

ST_HIDDEN int _st_epoll_fd_close(int osfd)
{
    if (_ST_EPOLL_READ_CNT(osfd) || _ST_EPOLL_WRITE_CNT(osfd) || _ST_EPOLL_EXCEP_CNT(osfd)) {
        errno = EBUSY;
        return -1;
    }

    return 0;
}

ST_HIDDEN int _st_epoll_fd_getlimit(void)
{
    /* zero means no specific limit */
    return 0;
}

/*
 * Check if epoll functions are just stubs.
 */
ST_HIDDEN int _st_epoll_is_supported(void)
{
    struct epoll_event ev;

    ev.events = EPOLLIN;
    ev.data.ptr = NULL;
    /* Guaranteed to fail */
    epoll_ctl(-1, EPOLL_CTL_ADD, -1, &ev);

    return (errno != ENOSYS);
}

static _st_eventsys_t _st_epoll_eventsys = {
    "epoll",
    ST_EVENTSYS_ALT,
    _st_epoll_init,
    _st_epoll_dispatch,
    _st_epoll_pollset_add,
    _st_epoll_pollset_del,
    _st_epoll_fd_new,
    _st_epoll_fd_close,
    _st_epoll_fd_getlimit
};
#endif  /* MD_HAVE_EPOLL */


/*****************************************
 * Public functions
 */

int st_set_eventsys(int eventsys)
{
    if (_st_eventsys) {
        errno = EBUSY;
        return -1;
    }

    if (eventsys == ST_EVENTSYS_SELECT) {
#if defined (MD_HAVE_SELECT)
        _st_eventsys = &_st_select_eventsys;
        return 0;
#endif
    }

    if (eventsys == ST_EVENTSYS_ALT) {
#if defined (MD_HAVE_KQUEUE)
        _st_eventsys = &_st_kq_eventsys;
        return 0;
#elif defined (MD_HAVE_EPOLL)
        if (_st_epoll_is_supported()) {
            _st_eventsys = &_st_epoll_eventsys;
            return 0;
        }
#endif
    }

    errno = EINVAL;
    return -1;
}

int st_get_eventsys(void)
{
    return _st_eventsys ? _st_eventsys->val : -1;
}

const char *st_get_eventsys_name(void)
{
    return _st_eventsys ? _st_eventsys->name : "";
}

