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
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include "common.h"

/* merge from https://github.com/toffaletti/state-threads/commit/7f57fc9acc05e657bca1223f1e5b9b1a45ed929b */
#ifdef MD_VALGRIND
#include <valgrind/valgrind.h>
#endif

// Global stat.
#if defined(DEBUG) && defined(DEBUG_STATS)
__thread unsigned long long _st_stat_sched_15ms = 0;
__thread unsigned long long _st_stat_sched_20ms = 0;
__thread unsigned long long _st_stat_sched_25ms = 0;
__thread unsigned long long _st_stat_sched_30ms = 0;
__thread unsigned long long _st_stat_sched_35ms = 0;
__thread unsigned long long _st_stat_sched_40ms = 0;
__thread unsigned long long _st_stat_sched_80ms = 0;
__thread unsigned long long _st_stat_sched_160ms = 0;
__thread unsigned long long _st_stat_sched_s = 0;

__thread unsigned long long _st_stat_thread_run = 0;
__thread unsigned long long _st_stat_thread_idle = 0;
__thread unsigned long long _st_stat_thread_yield = 0;
__thread unsigned long long _st_stat_thread_yield2 = 0;
#endif


/* Global data */
__thread _st_vp_t _st_this_vp;           /* This VP */
__thread _st_thread_t *_st_this_thread;  /* Current thread */
__thread int _st_active_count = 0;       /* Active thread count */

__thread time_t _st_curr_time = 0;       /* Current time as returned by time(2) */
__thread st_utime_t _st_last_tset;       /* Last time it was fetched */

// We should initialize the thread-local variable in st_init().
extern __thread _st_clist_t _st_free_stacks;

int st_poll(struct pollfd *pds, int npds, st_utime_t timeout)
{
    struct pollfd *pd;
    struct pollfd *epd = pds + npds;
    _st_pollq_t pq;
    _st_thread_t *me = _st_this_thread;
    int n;
    
    if (me->flags & _ST_FL_INTERRUPT) {
        me->flags &= ~_ST_FL_INTERRUPT;
        errno = EINTR;
        return -1;
    }
    
    if ((*_st_eventsys->pollset_add)(pds, npds) < 0)
        return -1;
    
    pq.pds = pds;
    pq.npds = npds;
    pq.thread = me;
    pq.on_ioq = 1;
    st_clist_insert_before(&pq.links, &_st_this_vp.io_q);
    if (timeout != ST_UTIME_NO_TIMEOUT)
        _st_add_sleep_q(me, timeout);
    me->state = _ST_ST_IO_WAIT;
    
    _st_switch_context(me);
    
    n = 0;
    if (pq.on_ioq) {
        /* If we timed out, the pollq might still be on the ioq. Remove it */
        st_clist_remove(&pq.links);
        (*_st_eventsys->pollset_del)(pds, npds);
    } else {
        /* Count the number of ready descriptors */
        for (pd = pds; pd < epd; pd++) {
            if (pd->revents)
                n++;
        }
    }
    
    if (me->flags & _ST_FL_INTERRUPT) {
        me->flags &= ~_ST_FL_INTERRUPT;
        errno = EINTR;
        return -1;
    }
    
    return n;
}


void _st_vp_schedule(_st_thread_t *from)
{
    _st_thread_t *thread;
    
    if (_st_this_vp.run_q.next != &_st_this_vp.run_q) {
        #if defined(DEBUG) && defined(DEBUG_STATS)
        ++_st_stat_thread_run;
        #endif

        /* Pull thread off of the run queue */
        thread = _ST_THREAD_PTR(_st_this_vp.run_q.next);
        st_clist_remove(&thread->links);
    } else {
        #if defined(DEBUG) && defined(DEBUG_STATS)
        ++_st_stat_thread_idle;
        #endif

        /* If there are no threads to run, switch to the idle thread */
        thread = _st_this_vp.idle_thread;
    }
    ST_ASSERT(thread->state == _ST_ST_RUNNABLE);
    
    /* Resume the thread */
    thread->state = _ST_ST_RUNNING;
    _st_restore_context(from, thread);
}


/*
 * Initialize this Virtual Processor
 */
int st_init(void)
{
    _st_thread_t *thread;

    if (_st_active_count) {
        /* Already initialized */
        return 0;
    }
    
    /* We can ignore return value here */
    st_set_eventsys(ST_EVENTSYS_DEFAULT);
    
    if (_st_io_init() < 0)
        return -1;

    // Initialize the thread-local variables.
    st_clist_init(&_st_free_stacks);

    // Initialize ST.
    memset(&_st_this_vp, 0, sizeof(_st_vp_t));
    
    st_clist_init(&_st_this_vp.run_q);
    st_clist_init(&_st_this_vp.io_q);
    st_clist_init(&_st_this_vp.zombie_q);
#ifdef DEBUG
    st_clist_init(&_st_this_vp.thread_q);
#endif
    
    if ((*_st_eventsys->init)() < 0)
        return -1;
    
    _st_this_vp.pagesize = getpagesize();
    _st_this_vp.last_clock = st_utime();
    
    /*
     * Create idle thread
     */
    _st_this_vp.idle_thread = st_thread_create(_st_idle_thread_start, NULL, 0, 0);
    if (!_st_this_vp.idle_thread)
        return -1;
    _st_this_vp.idle_thread->flags = _ST_FL_IDLE_THREAD;
    _st_active_count--;
    st_clist_remove(&_st_this_vp.idle_thread->links);
    
    /*
     * Initialize primordial thread
     */
    thread = (_st_thread_t *) calloc(1, sizeof(_st_thread_t) + (ST_KEYS_MAX * sizeof(void *)));
    if (!thread)
        return -1;
    thread->private_data = (void **) (thread + 1);
    thread->state = _ST_ST_RUNNING;
    thread->flags = _ST_FL_PRIMORDIAL;
    _st_this_thread = thread;
    _st_active_count++;
#ifdef DEBUG
    st_clist_insert_before(&thread->tlink, &_st_this_vp.thread_q);
#endif
    
    return 0;
}


/*
 * Destroy this Virtual Processor
 */
void st_destroy(void)
{
    (*_st_eventsys->destroy)();
}


#ifdef ST_SWITCH_CB
st_switch_cb_t st_set_switch_in_cb(st_switch_cb_t cb)
{
    st_switch_cb_t ocb = _st_this_vp.switch_in_cb;
    _st_this_vp.switch_in_cb = cb;
    return ocb;
}

st_switch_cb_t st_set_switch_out_cb(st_switch_cb_t cb)
{
    st_switch_cb_t ocb = _st_this_vp.switch_out_cb;
    _st_this_vp.switch_out_cb = cb;
    return ocb;
}
#endif


/*
 * Start function for the idle thread
 */
/* ARGSUSED */
void *_st_idle_thread_start(void *arg)
{
    _st_thread_t *me = _st_this_thread;
    
    while (_st_active_count > 0) {
        /* Idle vp till I/O is ready or the smallest timeout expired */
        (*_st_eventsys->dispatch)();
        
        /* Check sleep queue for expired threads */
        _st_vp_check_clock();
        
        me->state = _ST_ST_RUNNABLE;
        _st_switch_context(me);
    }
    
    /* No more threads */
    exit(0);
    
    /* NOTREACHED */
    return NULL;
}


void st_thread_exit(void *retval)
{
    _st_thread_t *thread = _st_this_thread;
    
    thread->retval = retval;
    _st_thread_cleanup(thread);
    _st_active_count--;
    if (thread->term) {
        /* Put thread on the zombie queue */
        thread->state = _ST_ST_ZOMBIE;
        st_clist_insert_before(&thread->links, &_st_this_vp.zombie_q);
        
        /* Notify on our termination condition variable */
        st_cond_signal(thread->term);
        
        /* Switch context and come back later */
        _st_switch_context(thread);
        
        /* Continue the cleanup */
        st_cond_destroy(thread->term);
        thread->term = NULL;
    }
    
#ifdef DEBUG
    st_clist_remove(&thread->tlink);
#endif
    
    /* merge from https://github.com/toffaletti/state-threads/commit/7f57fc9acc05e657bca1223f1e5b9b1a45ed929b */
#ifdef MD_VALGRIND
    if (!(thread->flags & _ST_FL_PRIMORDIAL)) {
        VALGRIND_STACK_DEREGISTER(thread->stack->valgrind_stack_id);
    }
#endif
    
    if (!(thread->flags & _ST_FL_PRIMORDIAL))
        _st_stack_free(thread->stack);
    
    /* Find another thread to run */
    _st_switch_context(thread);
    /* Not going to land here */
}


int st_thread_join(_st_thread_t *thread, void **retvalp)
{
    _st_cond_t *term = thread->term;
    
    /* Can't join a non-joinable thread */
    if (term == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (_st_this_thread == thread) {
        errno = EDEADLK;
        return -1;
    }
    
    /* Multiple threads can't wait on the same joinable thread */
    if (term->wait_q.next != &term->wait_q) {
        errno = EINVAL;
        return -1;
    }
    
    while (thread->state != _ST_ST_ZOMBIE) {
        if (st_cond_timedwait(term, ST_UTIME_NO_TIMEOUT) != 0)
            return -1;
    }
    
    if (retvalp)
        *retvalp = thread->retval;
    
    /*
     * Remove target thread from the zombie queue and make it runnable.
     * When it gets scheduled later, it will do the clean up.
     */
    thread->state = _ST_ST_RUNNABLE;
    st_clist_remove(&thread->links);
    st_clist_insert_before(&thread->links, &_st_this_vp.run_q);
    
    return 0;
}


void _st_thread_main(void)
{
    _st_thread_t *thread = _st_this_thread;

#ifdef MD_ASAN
    /* Switch from other thread to this new created thread. */
    _st_asan_finish_switch(thread);
#endif
    
    /*
     * Cap the stack by zeroing out the saved return address register
     * value. This allows some debugging/profiling tools to know when
     * to stop unwinding the stack. It's a no-op on most platforms.
     */
    MD_CAP_STACK(&thread);
    
    /* Run thread main */
    thread->retval = (*thread->start)(thread->arg);
    
    /* All done, time to go away */
    st_thread_exit(thread->retval);
}


/*
 * Insert "thread" into the timeout heap, in the position
 * specified by thread->heap_index.  See docs/timeout_heap.txt
 * for details about the timeout heap.
 */
static _st_thread_t **heap_insert(_st_thread_t *thread) {
    int target = thread->heap_index;
    int s = target;
    _st_thread_t **p = &_st_this_vp.sleep_q;
    int bits = 0;
    int bit;
    int index = 1;
    
    while (s) {
        s >>= 1;
        bits++;
    }
    for (bit = bits - 2; bit >= 0; bit--) {
        if (thread->due < (*p)->due) {
            _st_thread_t *t = *p;
            thread->left = t->left;
            thread->right = t->right;
            *p = thread;
            thread->heap_index = index;
            thread = t;
        }
        index <<= 1;
        if (target & (1 << bit)) {
            p = &((*p)->right);
            index |= 1;
        } else {
            p = &((*p)->left);
        }
    }
    thread->heap_index = index;
    *p = thread;
    thread->left = thread->right = NULL;
    return p;
}


/*
 * Delete "thread" from the timeout heap.
 */
static void heap_delete(_st_thread_t *thread) {
    _st_thread_t *t, **p;
    int bits = 0;
    int s, bit;
    
    /* First find and unlink the last heap element */
    p = &_st_this_vp.sleep_q;
    s = _st_this_vp.sleepq_size;
    while (s) {
        s >>= 1;
        bits++;
    }
    for (bit = bits - 2; bit >= 0; bit--) {
        if (_st_this_vp.sleepq_size & (1 << bit)) {
            p = &((*p)->right);
        } else {
            p = &((*p)->left);
        }
    }
    t = *p;
    *p = NULL;
    --_st_this_vp.sleepq_size;
    if (t != thread) {
        /*
         * Insert the unlinked last element in place of the element we are deleting
         */
        t->heap_index = thread->heap_index;
        p = heap_insert(t);
        t = *p;
        t->left = thread->left;
        t->right = thread->right;
        
        /*
         * Reestablish the heap invariant.
         */
        for (;;) {
            _st_thread_t *y; /* The younger child */
            int index_tmp;
            if (t->left == NULL)
                break;
            else if (t->right == NULL)
                y = t->left;
            else if (t->left->due < t->right->due)
                y = t->left;
            else
                y = t->right;
            if (t->due > y->due) {
                _st_thread_t *tl = y->left;
                _st_thread_t *tr = y->right;
                *p = y;
                if (y == t->left) {
                    y->left = t;
                    y->right = t->right;
                    p = &y->left;
                } else {
                    y->left = t->left;
                    y->right = t;
                    p = &y->right;
                }
                t->left = tl;
                t->right = tr;
                index_tmp = t->heap_index;
                t->heap_index = y->heap_index;
                y->heap_index = index_tmp;
            } else {
                break;
            }
        }
    }
    thread->left = thread->right = NULL;
}


void _st_add_sleep_q(_st_thread_t *thread, st_utime_t timeout)
{
    thread->due = _st_this_vp.last_clock + timeout;
    thread->flags |= _ST_FL_ON_SLEEPQ;
    thread->heap_index = ++_st_this_vp.sleepq_size;
    heap_insert(thread);
}


void _st_del_sleep_q(_st_thread_t *thread)
{
    heap_delete(thread);
    thread->flags &= ~_ST_FL_ON_SLEEPQ;
}


void _st_vp_check_clock(void)
{
    _st_thread_t *thread;
    st_utime_t now;
#if defined(DEBUG) && defined(DEBUG_STATS)
    st_utime_t elapsed;
#endif

    now = st_utime();
#if defined(DEBUG) && defined(DEBUG_STATS)
    elapsed = now < _st_this_vp.last_clock? 0 : now - _st_this_vp.last_clock; // Might step back.
#endif
    _st_this_vp.last_clock = now;

    #if defined(DEBUG) && defined(DEBUG_STATS)
    if (elapsed <= 10000) {
        ++_st_stat_sched_15ms;
    } else if (elapsed <= 21000) {
        ++_st_stat_sched_20ms;
    } else if (elapsed <= 25000) {
        ++_st_stat_sched_25ms;
    } else if (elapsed <= 30000) {
        ++_st_stat_sched_30ms;
    } else if (elapsed <= 35000) {
        ++_st_stat_sched_35ms;
    } else if (elapsed <= 40000) {
        ++_st_stat_sched_40ms;
    } else if (elapsed <= 80000) {
        ++_st_stat_sched_80ms;
    } else if (elapsed <= 160000) {
        ++_st_stat_sched_160ms;
    } else {
        ++_st_stat_sched_s;
    }
    #endif
    
    if (_st_curr_time && now - _st_last_tset > 999000) {
        _st_curr_time = time(NULL);
        _st_last_tset = now;
    }
    
    while (_st_this_vp.sleep_q != NULL) {
        thread = _st_this_vp.sleep_q;
        ST_ASSERT(thread->flags & _ST_FL_ON_SLEEPQ);
        if (thread->due > now)
            break;
        _st_del_sleep_q(thread);
        
        /* If thread is waiting on condition variable, set the time out flag */
        if (thread->state == _ST_ST_COND_WAIT)
            thread->flags |= _ST_FL_TIMEDOUT;
        
        /* Make thread runnable */
        ST_ASSERT(!(thread->flags & _ST_FL_IDLE_THREAD));
        thread->state = _ST_ST_RUNNABLE;
        /* Insert at the head of RunQ, to execute timer first. */
        st_clist_insert_after(&thread->links, &_st_this_vp.run_q);
    }
}


void st_thread_yield()
{
    _st_thread_t *me = _st_this_thread;

    #if defined(DEBUG) && defined(DEBUG_STATS)
    ++_st_stat_thread_yield;
    #endif

    /* Check sleep queue for expired threads */
    _st_vp_check_clock();

    /* If not thread in RunQ to yield to, ignore and continue to run. */
    if (_st_this_vp.run_q.next == &_st_this_vp.run_q) {
        return;
    }

    #if defined(DEBUG) && defined(DEBUG_STATS)
    ++_st_stat_thread_yield2;
    #endif

    /* Append thread to the tail of RunQ, we will back after all threads executed. */
    me->state = _ST_ST_RUNNABLE;
    st_clist_insert_before(&me->links, &_st_this_vp.run_q);

    /* Yield to other threads in the RunQ. */
    _st_switch_context(me);
}


void st_thread_interrupt(_st_thread_t *thread)
{
    /* If thread is already dead */
    if (thread->state == _ST_ST_ZOMBIE)
        return;
    
    thread->flags |= _ST_FL_INTERRUPT;
    
    if (thread->state == _ST_ST_RUNNING || thread->state == _ST_ST_RUNNABLE)
        return;
    
    if (thread->flags & _ST_FL_ON_SLEEPQ)
        _st_del_sleep_q(thread);
    
    /* Make thread runnable */
    thread->state = _ST_ST_RUNNABLE;
    st_clist_insert_before(&thread->links, &_st_this_vp.run_q);
}


_st_thread_t *st_thread_create(void *(*start)(void *arg), void *arg, int joinable, int stk_size)
{
    _st_thread_t *thread;
    _st_stack_t *stack;
    void **ptds;
    char *sp;
    
    /* Adjust stack size */
    if (stk_size == 0)
        stk_size = ST_DEFAULT_STACK_SIZE;
    stk_size = ((stk_size + _st_this_vp.pagesize - 1) / _st_this_vp.pagesize) * _st_this_vp.pagesize;
    stack = _st_stack_new(stk_size);
    if (!stack)
        return NULL;
    
    /* Allocate thread object and per-thread data off the stack */
    sp = stack->stk_top;
    sp = sp - (ST_KEYS_MAX * sizeof(void *));
    ptds = (void **) sp;
    sp = sp - sizeof(_st_thread_t);
    thread = (_st_thread_t *) sp;
    
    /* Make stack 64-byte aligned */
    if ((unsigned long)sp & 0x3f)
        sp = sp - ((unsigned long)sp & 0x3f);
    stack->sp = sp - _ST_STACK_PAD_SIZE;

    memset(thread, 0, sizeof(_st_thread_t));
    memset(ptds, 0, ST_KEYS_MAX * sizeof(void *));
    
    /* Initialize thread */
    thread->private_data = ptds;
    thread->stack = stack;
    thread->start = start;
    thread->arg = arg;

    /* Note that we must directly call rather than call any functions. */
    if (_st_md_cxt_save(thread->context)) {
        _st_thread_main();
    }
    MD_GET_SP(thread) = (long)(stack->sp);

    /* If thread is joinable, allocate a termination condition variable */
    if (joinable) {
        thread->term = st_cond_new();
        if (thread->term == NULL) {
            _st_stack_free(thread->stack);
            return NULL;
        }
    }
    
    /* Make thread runnable */
    thread->state = _ST_ST_RUNNABLE;
    _st_active_count++;
    st_clist_insert_before(&thread->links, &_st_this_vp.run_q);
#ifdef DEBUG
    st_clist_insert_before(&thread->tlink, &_st_this_vp.thread_q);
#endif
    
    /* merge from https://github.com/toffaletti/state-threads/commit/7f57fc9acc05e657bca1223f1e5b9b1a45ed929b */
#ifdef MD_VALGRIND
    if (!(thread->flags & _ST_FL_PRIMORDIAL)) {
        thread->stack->valgrind_stack_id = VALGRIND_STACK_REGISTER(thread->stack->stk_top, thread->stack->stk_bottom);
    }
#endif
    
    return thread;
}


_st_thread_t *st_thread_self(void)
{
    return _st_this_thread;
}

#ifdef DEBUG
/* ARGSUSED */
void _st_show_thread_stack(_st_thread_t *thread, const char *messg)
{
    
}

/* To be set from debugger */
int _st_iterate_threads_flag = 0;

void _st_iterate_threads(void)
{
    static __thread _st_thread_t *thread = NULL;
    static __thread _st_jmp_buf_t orig_jb, save_jb;
    _st_clist_t *q;
    
    if (!_st_iterate_threads_flag) {
        if (thread) {
            memcpy(thread->context, save_jb, sizeof(_st_jmp_buf_t));
            _st_md_cxt_restore(orig_jb, 1);
        }
        return;
    }
    
    if (thread) {
        memcpy(thread->context, save_jb, sizeof(_st_jmp_buf_t));
        _st_show_thread_stack(thread, NULL);
    } else {
        if (_st_md_cxt_save(orig_jb)) {
            _st_iterate_threads_flag = 0;
            thread = NULL;
            _st_show_thread_stack(thread, "Iteration completed");
            return;
        }
        thread = _st_this_thread;
        _st_show_thread_stack(thread, "Iteration started");
    }
    
    q = thread->tlink.next;
    if (q == &_st_this_vp.thread_q)
        q = q->next;
    ST_ASSERT(q != &_st_this_vp.thread_q);
    thread = _ST_THREAD_THREADQ_PTR(q);
    if (thread == _st_this_thread)
        _st_md_cxt_restore(orig_jb, 1);
    memcpy(save_jb, thread->context, sizeof(_st_jmp_buf_t));
    _st_md_cxt_restore(thread->context, 1);
}
#endif /* DEBUG */

