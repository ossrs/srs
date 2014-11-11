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

#ifdef USE_POLL
    #error "Not support USE_POLL"
#endif
#ifdef MD_HAVE_KQUEUE
    #error "Not support MD_HAVE_KQUEUE"
#endif
#ifdef MD_HAVE_POLL
    #error "Not support MD_HAVE_POLL"
#endif
#ifndef MD_HAVE_EPOLL
    #error "Only support MD_HAVE_EPOLL"
#endif

#include <sys/epoll.h>

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
    pid_t pid;
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

_st_eventsys_t *_st_eventsys = NULL;

/*****************************************
 * epoll event system
 */

ST_HIDDEN int _st_epoll_init(void)
{
    int fdlim;
    int err = 0;
    int rv = 0;

    _st_epoll_data = (struct _st_epolldata *) calloc(1, sizeof(*_st_epoll_data));
    if (!_st_epoll_data) {
        return -1;
    }

    fdlim = st_getfdlimit();
    _st_epoll_data->fd_hint = (fdlim > 0 && fdlim < ST_EPOLL_EVTLIST_SIZE) ? fdlim : ST_EPOLL_EVTLIST_SIZE;

    if ((_st_epoll_data->epfd = epoll_create(_st_epoll_data->fd_hint)) < 0) {
        err = errno;
        rv = -1;
        goto cleanup_epoll;
    }
    fcntl(_st_epoll_data->epfd, F_SETFD, FD_CLOEXEC);
    _st_epoll_data->pid = getpid();

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
        if (_st_epoll_data->epfd >= 0) {
            close(_st_epoll_data->epfd);
        }
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

    while (maxfd >= n) {
        n <<= 1;
    }

    ptr = (_epoll_fd_data_t *)realloc(_st_epoll_data->fd_data, n * sizeof(_epoll_fd_data_t));
    if (!ptr) {
        return -1;
    }

    memset(ptr + _st_epoll_data->fd_data_size, 0, (n - _st_epoll_data->fd_data_size) * sizeof(_epoll_fd_data_t));

    _st_epoll_data->fd_data = ptr;
    _st_epoll_data->fd_data_size = n;

    return 0;
}

ST_HIDDEN void _st_epoll_evtlist_expand(void)
{
    struct epoll_event *ptr;
    int n = _st_epoll_data->evtlist_size;

    while (_st_epoll_data->evtlist_cnt > n) {
        n <<= 1;
    }

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

        if (pd->events & POLLIN) {
            _ST_EPOLL_READ_CNT(pd->fd)--;
        }
        if (pd->events & POLLOUT) {
            _ST_EPOLL_WRITE_CNT(pd->fd)--;
        }
        if (pd->events & POLLPRI) {
            _ST_EPOLL_EXCEP_CNT(pd->fd)--;
        }

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
        if (fd < 0 || !pds[i].events || (pds[i].events & ~(POLLIN | POLLOUT | POLLPRI))) {
            errno = EINVAL;
            return -1;
        }
        if (fd >= _st_epoll_data->fd_data_size && _st_epoll_fd_data_expand(fd) < 0) {
            return -1;
        }
    }

    for (i = 0; i < npds; i++) {
        fd = pds[i].fd;
        old_events = _ST_EPOLL_EVENTS(fd);

        if (pds[i].events & POLLIN) {
            _ST_EPOLL_READ_CNT(fd)++;
        }
        if (pds[i].events & POLLOUT) {
            _ST_EPOLL_WRITE_CNT(fd)++;
        }
        if (pds[i].events & POLLPRI) {
            _ST_EPOLL_EXCEP_CNT(fd)++;
        }

        events = _ST_EPOLL_EVENTS(fd);
        if (events != old_events) {
            op = old_events ? EPOLL_CTL_MOD : EPOLL_CTL_ADD;
            ev.events = events;
            ev.data.fd = fd;
            if (epoll_ctl(_st_epoll_data->epfd, op, fd, &ev) < 0 && (op != EPOLL_CTL_ADD || errno != EEXIST)) {
                break;
            }
            if (op == EPOLL_CTL_ADD) {
                _st_epoll_data->evtlist_cnt++;
                if (_st_epoll_data->evtlist_cnt > _st_epoll_data->evtlist_size) {
                    _st_epoll_evtlist_expand();
                }
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

    if (_ST_SLEEPQ == NULL) {
        timeout = -1;
    } else {
        min_timeout = (_ST_SLEEPQ->due <= _ST_LAST_CLOCK) ? 0 : (_ST_SLEEPQ->due - _ST_LAST_CLOCK);
        timeout = (int) (min_timeout / 1000);
    }

    if (_st_epoll_data->pid != getpid()) {
        // WINLIN: remove it for bug introduced.
        // @see: https://github.com/winlinvip/simple-rtmp-server/issues/193
        exit(-1);
    }

    /* Check for I/O operations */
    nfd = epoll_wait(_st_epoll_data->epfd, _st_epoll_data->evtlist, _st_epoll_data->evtlist_size, timeout);

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
                if ((events & POLLIN) && (_ST_EPOLL_REVENTS(osfd) & EPOLLIN)) {
                    revents |= POLLIN;
                }
                if ((events & POLLOUT) && (_ST_EPOLL_REVENTS(osfd) & EPOLLOUT)) {
                    revents |= POLLOUT;
                }
                if ((events & POLLPRI) && (_ST_EPOLL_REVENTS(osfd) & EPOLLPRI)) {
                    revents |= POLLPRI;
                }
                if (_ST_EPOLL_REVENTS(osfd) & EPOLLERR) {
                    revents |= POLLERR;
                }
                if (_ST_EPOLL_REVENTS(osfd) & EPOLLHUP) {
                    revents |= POLLHUP;
                }

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

                if (pq->thread->flags & _ST_FL_ON_SLEEPQ) {
                    _ST_DEL_SLEEPQ(pq->thread);
                }
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
    if (osfd >= _st_epoll_data->fd_data_size && _st_epoll_fd_data_expand(osfd) < 0) {
        return -1;
    }

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

/*****************************************
 * Public functions
 */

int st_set_eventsys(int eventsys)
{
    if (_st_eventsys) {
        errno = EBUSY;
        return -1;
    }

    switch (eventsys) {
    case ST_EVENTSYS_DEFAULT:
    case ST_EVENTSYS_ALT:
    default:
        if (_st_epoll_is_supported()) {
            _st_eventsys = &_st_epoll_eventsys;
            break;
        }
        errno = EINVAL;
        return -1;
    }

    return 0;
}

int st_get_eventsys(void)
{
    return _st_eventsys ? _st_eventsys->val : -1;
}

const char *st_get_eventsys_name(void)
{
    return _st_eventsys ? _st_eventsys->name : "";
}

