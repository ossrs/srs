/* SPDX-License-Identifier: MPL-1.1 OR GPL-2.0-or-later */

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
 * 
 * Portions created by SGI are Copyright (C) 2000-2001 Silicon
 * Graphics, Inc.  All Rights Reserved.
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

/*
 * This file is derived directly from Netscape Communications Corporation,
 * and consists of extensive modifications made during the year(s) 1999-2000.
 */

#include <stdlib.h>
#include <time.h>
#include <errno.h>
#include "common.h"


extern __thread time_t _st_curr_time;
extern __thread st_utime_t _st_last_tset;
extern __thread int _st_active_count;

static st_utime_t (*_st_utime)(void) = NULL;


/*****************************************
 * Time functions
 */

st_utime_t st_utime(void)
{
    if (_st_utime == NULL) {
#ifdef MD_GET_UTIME
        MD_GET_UTIME();
#else
#error Unknown OS
#endif
    }
    
    return (*_st_utime)();
}


int st_set_utime_function(st_utime_t (*func)(void))
{
    if (_st_active_count) {
        errno = EINVAL;
        return -1;
    }
    
    _st_utime = func;
    
    return 0;
}


st_utime_t st_utime_last_clock(void)
{
    return _ST_LAST_CLOCK;
}


int st_timecache_set(int on)
{
    int wason = (_st_curr_time) ? 1 : 0;
    
    if (on) {
        _st_curr_time = time(NULL);
        _st_last_tset = st_utime();
    } else
        _st_curr_time = 0;
    
    return wason;
}


time_t st_time(void)
{
    if (_st_curr_time)
        return _st_curr_time;
    
    return time(NULL);
}


int st_usleep(st_utime_t usecs)
{
    _st_thread_t *me = _ST_CURRENT_THREAD();
    
    if (me->flags & _ST_FL_INTERRUPT) {
        me->flags &= ~_ST_FL_INTERRUPT;
        errno = EINTR;
        return -1;
    }
    
    if (usecs != ST_UTIME_NO_TIMEOUT) {
        me->state = _ST_ST_SLEEPING;
        _ST_ADD_SLEEPQ(me, usecs);
    } else
        me->state = _ST_ST_SUSPENDED;
    
    _ST_SWITCH_CONTEXT(me);
    
    if (me->flags & _ST_FL_INTERRUPT) {
        me->flags &= ~_ST_FL_INTERRUPT;
        errno = EINTR;
        return -1;
    }
    
    return 0;
}


int st_sleep(int secs)
{
    return st_usleep((secs >= 0) ? secs * (st_utime_t) 1000000LL : ST_UTIME_NO_TIMEOUT);
}


/*****************************************
 * Condition variable functions
 */

_st_cond_t *st_cond_new(void)
{
    _st_cond_t *cvar;
    
    cvar = (_st_cond_t *) calloc(1, sizeof(_st_cond_t));
    if (cvar) {
        ST_INIT_CLIST(&cvar->wait_q);
    }
    
    return cvar;
}


int st_cond_destroy(_st_cond_t *cvar)
{
    if (cvar->wait_q.next != &cvar->wait_q) {
        errno = EBUSY;
        return -1;
    }
    
    free(cvar);
    
    return 0;
}


int st_cond_timedwait(_st_cond_t *cvar, st_utime_t timeout)
{
    _st_thread_t *me = _ST_CURRENT_THREAD();
    int rv;
    
    if (me->flags & _ST_FL_INTERRUPT) {
        me->flags &= ~_ST_FL_INTERRUPT;
        errno = EINTR;
        return -1;
    }
    
    /* Put caller thread on the condition variable's wait queue */
    me->state = _ST_ST_COND_WAIT;
    ST_APPEND_LINK(&me->wait_links, &cvar->wait_q);
    
    if (timeout != ST_UTIME_NO_TIMEOUT)
        _ST_ADD_SLEEPQ(me, timeout);
    
    _ST_SWITCH_CONTEXT(me);
    
    ST_REMOVE_LINK(&me->wait_links);
    rv = 0;
    
    if (me->flags & _ST_FL_TIMEDOUT) {
        me->flags &= ~_ST_FL_TIMEDOUT;
        errno = ETIME;
        rv = -1;
    }
    if (me->flags & _ST_FL_INTERRUPT) {
        me->flags &= ~_ST_FL_INTERRUPT;
        errno = EINTR;
        rv = -1;
    }
    
    return rv;
}


int st_cond_wait(_st_cond_t *cvar)
{
    return st_cond_timedwait(cvar, ST_UTIME_NO_TIMEOUT);
}


static int _st_cond_signal(_st_cond_t *cvar, int broadcast)
{
    _st_thread_t *thread;
    _st_clist_t *q;
    
    for (q = cvar->wait_q.next; q != &cvar->wait_q; q = q->next) {
        thread = _ST_THREAD_WAITQ_PTR(q);
        if (thread->state == _ST_ST_COND_WAIT) {
            if (thread->flags & _ST_FL_ON_SLEEPQ)
                _ST_DEL_SLEEPQ(thread);
            
            /* Make thread runnable */
            thread->state = _ST_ST_RUNNABLE;
            _ST_ADD_RUNQ(thread);
            if (!broadcast)
                break;
        }
    }
    
    return 0;
}


int st_cond_signal(_st_cond_t *cvar)
{
    return _st_cond_signal(cvar, 0);
}


int st_cond_broadcast(_st_cond_t *cvar)
{
    return _st_cond_signal(cvar, 1);
}


/*****************************************
 * Mutex functions
 */

_st_mutex_t *st_mutex_new(void)
{
    _st_mutex_t *lock;
    
    lock = (_st_mutex_t *) calloc(1, sizeof(_st_mutex_t));
    if (lock) {
        ST_INIT_CLIST(&lock->wait_q);
        lock->owner = NULL;
    }
    
    return lock;
}


int st_mutex_destroy(_st_mutex_t *lock)
{
    if (lock->owner != NULL || lock->wait_q.next != &lock->wait_q) {
        errno = EBUSY;
        return -1;
    }
    
    free(lock);
    
    return 0;
}


int st_mutex_lock(_st_mutex_t *lock)
{
    _st_thread_t *me = _ST_CURRENT_THREAD();
    
    if (me->flags & _ST_FL_INTERRUPT) {
        me->flags &= ~_ST_FL_INTERRUPT;
        errno = EINTR;
        return -1;
    }
    
    if (lock->owner == NULL) {
        /* Got the mutex */
        lock->owner = me;
        return 0;
    }
    
    if (lock->owner == me) {
        errno = EDEADLK;
        return -1;
    }
    
    /* Put caller thread on the mutex's wait queue */
    me->state = _ST_ST_LOCK_WAIT;
    ST_APPEND_LINK(&me->wait_links, &lock->wait_q);
    
    _ST_SWITCH_CONTEXT(me);
    
    ST_REMOVE_LINK(&me->wait_links);
    
    if ((me->flags & _ST_FL_INTERRUPT) && lock->owner != me) {
        me->flags &= ~_ST_FL_INTERRUPT;
        errno = EINTR;
        return -1;
    }
    
    return 0;
}


int st_mutex_unlock(_st_mutex_t *lock)
{
    _st_thread_t *thread;
    _st_clist_t *q;
    
    if (lock->owner != _ST_CURRENT_THREAD()) {
        errno = EPERM;
        return -1;
    }
    
    for (q = lock->wait_q.next; q != &lock->wait_q; q = q->next) {
        thread = _ST_THREAD_WAITQ_PTR(q);
        if (thread->state == _ST_ST_LOCK_WAIT) {
            lock->owner = thread;
            /* Make thread runnable */
            thread->state = _ST_ST_RUNNABLE;
            _ST_ADD_RUNQ(thread);
            return 0;
        }
    }
    
    /* No threads waiting on this mutex */
    lock->owner = NULL;
    
    return 0;
}


int st_mutex_trylock(_st_mutex_t *lock)
{
    if (lock->owner != NULL) {
        errno = EBUSY;
        return -1;
    }
    
    /* Got the mutex */
    lock->owner = _ST_CURRENT_THREAD();
    
    return 0;
}

