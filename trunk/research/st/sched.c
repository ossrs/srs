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

/* Global data */
_st_vp_t _st_this_vp;           /* This VP */
_st_thread_t *_st_this_thread;  /* Current thread */
int _st_active_count = 0;       /* Active thread count */

time_t _st_curr_time = 0;       /* Current time as returned by time(2) */
st_utime_t _st_last_tset;       /* Last time it was fetched */

int st_poll(struct pollfd *pds, int npds, st_utime_t timeout)
{
    struct pollfd *pd;
    struct pollfd *epd = pds + npds;
    _st_pollq_t pq;
    _st_thread_t *me = _ST_CURRENT_THREAD();
    int n;
    
    if (me->flags & _ST_FL_INTERRUPT) {
        me->flags &= ~_ST_FL_INTERRUPT;
        errno = EINTR;
        return -1;
    }
    
    if ((*_st_eventsys->pollset_add)(pds, npds) < 0) {
        return -1;
    }
    
    pq.pds = pds;
    pq.npds = npds;
    pq.thread = me;
    pq.on_ioq = 1;
    _ST_ADD_IOQ(pq);
    if (timeout != ST_UTIME_NO_TIMEOUT) {
        _ST_ADD_SLEEPQ(me, timeout);
    }
    me->state = _ST_ST_IO_WAIT;
    
    _ST_SWITCH_CONTEXT(me);
    
    n = 0;
    if (pq.on_ioq) {
        /* If we timed out, the pollq might still be on the ioq. Remove it */
        _ST_DEL_IOQ(pq);
        (*_st_eventsys->pollset_del)(pds, npds);
    } else {
        /* Count the number of ready descriptors */
        for (pd = pds; pd < epd; pd++) {
            if (pd->revents) {
                n++;
            }
        }
    }
    
    if (me->flags & _ST_FL_INTERRUPT) {
        me->flags &= ~_ST_FL_INTERRUPT;
        errno = EINTR;
        return -1;
    }
    
    return n;
}

void _st_vp_schedule(void)
{
    _st_thread_t *trd;
    
    if (_ST_RUNQ.next != &_ST_RUNQ) {
        /* Pull thread off of the run queue */
        trd = _ST_THREAD_PTR(_ST_RUNQ.next);
        _ST_DEL_RUNQ(trd);
    } else {
        /* If there are no threads to run, switch to the idle thread */
        trd = _st_this_vp.idle_thread;
    }
    ST_ASSERT(trd->state == _ST_ST_RUNNABLE);
    
    /* Resume the thread */
    trd->state = _ST_ST_RUNNING;
    _ST_RESTORE_CONTEXT(trd);
}

/*
 * Initialize this Virtual Processor
 */
int st_init(void)
{
    _st_thread_t *trd;
    
    if (_st_active_count) {
        /* Already initialized */
        return 0;
    }
    
    /* We can ignore return value here */
    st_set_eventsys(ST_EVENTSYS_DEFAULT);
    
    if (_st_io_init() < 0) {
        return -1;
    }
    
    memset(&_st_this_vp, 0, sizeof(_st_vp_t));
    
    ST_INIT_CLIST(&_ST_RUNQ);
    ST_INIT_CLIST(&_ST_IOQ);
    ST_INIT_CLIST(&_ST_ZOMBIEQ);
#ifdef DEBUG
    ST_INIT_CLIST(&_ST_THREADQ);
#endif
    
    if ((*_st_eventsys->init)() < 0) {
        return -1;
    }
    
    _st_this_vp.pagesize = getpagesize();
    _st_this_vp.last_clock = st_utime();
    
    /*
    * Create idle thread
    */
    _st_this_vp.idle_thread = st_thread_create(_st_idle_thread_start, NULL, 0, 0);
    if (!_st_this_vp.idle_thread) {
        return -1;
    }
    _st_this_vp.idle_thread->flags = _ST_FL_IDLE_THREAD;
    _st_active_count--;
    _ST_DEL_RUNQ(_st_this_vp.idle_thread);
    
    /*
    * Initialize primordial thread
    */
    trd = (_st_thread_t *) calloc(1, sizeof(_st_thread_t) +
    (ST_KEYS_MAX * sizeof(void *)));
    if (!trd) {
        return -1;
    }
    trd->private_data = (void **) (trd + 1);
    trd->state = _ST_ST_RUNNING;
    trd->flags = _ST_FL_PRIMORDIAL;
    _ST_SET_CURRENT_THREAD(trd);
    _st_active_count++;
#ifdef DEBUG
    _ST_ADD_THREADQ(trd);
#endif
    
    return 0;
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
    _st_thread_t *me = _ST_CURRENT_THREAD();
    
    while (_st_active_count > 0) {
        /* Idle vp till I/O is ready or the smallest timeout expired */
        _ST_VP_IDLE();
        
        /* Check sleep queue for expired threads */
        _st_vp_check_clock();
        
        me->state = _ST_ST_RUNNABLE;
        _ST_SWITCH_CONTEXT(me);
    }
    
    /* No more threads */
    exit(0);
    
    /* NOTREACHED */
    return NULL;
}

void st_thread_exit(void *retval)
{
    _st_thread_t *trd = _ST_CURRENT_THREAD();
    
    trd->retval = retval;
    _st_thread_cleanup(trd);
    _st_active_count--;
    if (trd->term) {
        /* Put thread on the zombie queue */
        trd->state = _ST_ST_ZOMBIE;
        _ST_ADD_ZOMBIEQ(trd);
        
        /* Notify on our termination condition variable */
        st_cond_signal(trd->term);
        
        /* Switch context and come back later */
        _ST_SWITCH_CONTEXT(trd);
        
        /* Continue the cleanup */
        st_cond_destroy(trd->term);
        trd->term = NULL;
    }
    
#ifdef DEBUG
    _ST_DEL_THREADQ(trd);
#endif
    
    if (!(trd->flags & _ST_FL_PRIMORDIAL)) {
        _st_stack_free(trd->stack);
    }
    
    /* Find another thread to run */
    _ST_SWITCH_CONTEXT(trd);
    /* Not going to land here */
}

int st_thread_join(_st_thread_t *trd, void **retvalp)
{
    _st_cond_t *term = trd->term;
    
    /* Can't join a non-joinable thread */
    if (term == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (_ST_CURRENT_THREAD() == trd) {
        errno = EDEADLK;
        return -1;
    }
    
    /* Multiple threads can't wait on the same joinable thread */
    if (term->wait_q.next != &term->wait_q) {
        errno = EINVAL;
        return -1;
    }
    
    while (trd->state != _ST_ST_ZOMBIE) {
        if (st_cond_timedwait(term, ST_UTIME_NO_TIMEOUT) != 0) {
            return -1;
        }
    }
    
    if (retvalp) {
        *retvalp = trd->retval;
    }
    
    /*
    * Remove target thread from the zombie queue and make it runnable.
    * When it gets scheduled later, it will do the clean up.
    */
    trd->state = _ST_ST_RUNNABLE;
    _ST_DEL_ZOMBIEQ(trd);
    _ST_ADD_RUNQ(trd);
    
    return 0;
}

void _st_thread_main(void)
{
    _st_thread_t *trd = _ST_CURRENT_THREAD();
    
    /*
    * Cap the stack by zeroing out the saved return address register
    * value. This allows some debugging/profiling tools to know when
    * to stop unwinding the stack. It's a no-op on most platforms.
    */
    MD_CAP_STACK(&trd);
    
    /* Run thread main */
    trd->retval = (*trd->start)(trd->arg);
    
    /* All done, time to go away */
    st_thread_exit(trd->retval);
}

/*
 * Insert "thread" into the timeout heap, in the position
 * specified by thread->heap_index.  See docs/timeout_heap.txt
 * for details about the timeout heap.
 */
static _st_thread_t **heap_insert(_st_thread_t *trd)
{
    int target = trd->heap_index;
    int s = target;
    _st_thread_t **p = &_ST_SLEEPQ;
    int bits = 0;
    int bit;
    int index = 1;
    
    while (s) {
        s >>= 1;
        bits++;
    }
    
    for (bit = bits - 2; bit >= 0; bit--) {
        if (trd->due < (*p)->due) {
            _st_thread_t *t = *p;
            trd->left = t->left;
            trd->right = t->right;
            *p = trd;
            trd->heap_index = index;
            trd = t;
        }
        index <<= 1;
        if (target & (1 << bit)) {
            p = &((*p)->right);
            index |= 1;
        } else {
            p = &((*p)->left);
        }
    }
    
    trd->heap_index = index;
    *p = trd;
    trd->left = trd->right = NULL;
    
    return p;
}

/*
 * Delete "thread" from the timeout heap.
 */
static void heap_delete(_st_thread_t *trd) 
{
    _st_thread_t *t, **p;
    int bits = 0;
    int s, bit;
    
    /* First find and unlink the last heap element */
    p = &_ST_SLEEPQ;
    s = _ST_SLEEPQ_SIZE;
    while (s) {
        s >>= 1;
        bits++;
    }
    
    for (bit = bits - 2; bit >= 0; bit--) {
        if (_ST_SLEEPQ_SIZE & (1 << bit)) {
            p = &((*p)->right);
        } else {
            p = &((*p)->left);
        }
    }
    
    t = *p;
    *p = NULL;
    --_ST_SLEEPQ_SIZE;
    if (t != trd) {
        /*
        * Insert the unlinked last element in place of the element we are deleting
        */
        t->heap_index = trd->heap_index;
        p = heap_insert(t);
        t = *p;
        t->left = trd->left;
        t->right = trd->right;
        
        /*
        * Reestablish the heap invariant.
        */
        for (;;) {
            _st_thread_t *y; /* The younger child */
            int index_tmp;
            
            if (t->left == NULL) {
                break;
            } else if (t->right == NULL) {
                y = t->left;
            } else if (t->left->due < t->right->due) {
                y = t->left;
            } else {
                y = t->right;
            }
            
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
    
    trd->left = trd->right = NULL;
}

void _st_add_sleep_q(_st_thread_t *trd, st_utime_t timeout)
{
    trd->due = _ST_LAST_CLOCK + timeout;
    trd->flags |= _ST_FL_ON_SLEEPQ;
    trd->heap_index = ++_ST_SLEEPQ_SIZE;
    heap_insert(trd);
}

void _st_del_sleep_q(_st_thread_t *trd)
{
    heap_delete(trd);
    trd->flags &= ~_ST_FL_ON_SLEEPQ;
}

void _st_vp_check_clock(void)
{
    _st_thread_t *trd;
    st_utime_t elapsed, now;
    
    now = st_utime();
    elapsed = now - _ST_LAST_CLOCK;
    _ST_LAST_CLOCK = now;
    
    if (_st_curr_time && now - _st_last_tset > 999000) {
        _st_curr_time = time(NULL);
        _st_last_tset = now;
    }
    
    while (_ST_SLEEPQ != NULL) {
        trd = _ST_SLEEPQ;
        ST_ASSERT(trd->flags & _ST_FL_ON_SLEEPQ);
        if (trd->due > now) {
            break;
        }
        _ST_DEL_SLEEPQ(trd);
        
        /* If thread is waiting on condition variable, set the time out flag */
        if (trd->state == _ST_ST_COND_WAIT) {
            trd->flags |= _ST_FL_TIMEDOUT;
        }
        
        /* Make thread runnable */
        ST_ASSERT(!(trd->flags & _ST_FL_IDLE_THREAD));
        trd->state = _ST_ST_RUNNABLE;
        _ST_ADD_RUNQ(trd);
    }
}

void st_thread_interrupt(_st_thread_t* trd)
{
    /* If thread is already dead */
    if (trd->state == _ST_ST_ZOMBIE) {
        return;
    }
    
    trd->flags |= _ST_FL_INTERRUPT;
    
    if (trd->state == _ST_ST_RUNNING || trd->state == _ST_ST_RUNNABLE) {
        return;
    }
    
    if (trd->flags & _ST_FL_ON_SLEEPQ) {
        _ST_DEL_SLEEPQ(trd);
    }
    
    /* Make thread runnable */
    trd->state = _ST_ST_RUNNABLE;
    _ST_ADD_RUNQ(trd);
}

_st_thread_t *st_thread_create(void *(*start)(void *arg), void *arg, int joinable, int stk_size)
{
    _st_thread_t *trd;
    _st_stack_t *stack;
    void **ptds;
    char *sp;
    
    /* Adjust stack size */
    if (stk_size == 0) {
        stk_size = ST_DEFAULT_STACK_SIZE;
    }
    stk_size = ((stk_size + _ST_PAGE_SIZE - 1) / _ST_PAGE_SIZE) * _ST_PAGE_SIZE;
    stack = _st_stack_new(stk_size);
    if (!stack) {
        return NULL;
    }
    
    /* Allocate thread object and per-thread data off the stack */
#if defined (MD_STACK_GROWS_DOWN)
    sp = stack->stk_top;
    /*
    * The stack segment is split in the middle. The upper half is used
    * as backing store for the register stack which grows upward.
    * The lower half is used for the traditional memory stack which
    * grows downward. Both stacks start in the middle and grow outward
    * from each other.
    */
    /**
    The below comments is by winlin:
    The Stack public structure:
        +--------------------------------------------------------------+
        |                         stack                                |
        +--------------------------------------------------------------+
       bottom                                                         top
    The code bellow use the stack as:
        +-----------------+-----------------+-------------+------------+
        | stack of thread |pad+align(128B+) |thread(336B) | keys(128B) |
        +-----------------+-----------------+-------------+------------+
       bottom            sp                trd           ptds         top
               (context[0].__jmpbuf.sp)             (private_data)
    */
    sp = sp - (ST_KEYS_MAX * sizeof(void *));
    ptds = (void **) sp;
    sp = sp - sizeof(_st_thread_t);
    trd = (_st_thread_t *) sp;
    
    /* Make stack 64-byte aligned */
    if ((unsigned long)sp & 0x3f) {
        sp = sp - ((unsigned long)sp & 0x3f);
    }
    stack->sp = sp - _ST_STACK_PAD_SIZE;
#else
    #error "Only Supports Stack Grown Down"
#endif
    
    memset(trd, 0, sizeof(_st_thread_t));
    memset(ptds, 0, ST_KEYS_MAX * sizeof(void *));
    
    /* Initialize thread */
    trd->private_data = ptds;
    trd->stack = stack;
    trd->start = start;
    trd->arg = arg;

// by winlin, expand macro MD_INIT_CONTEXT
#if defined(__mips__)
    MD_SETJMP((trd)->context);
    trd->context[0].__jmpbuf[0].__pc = (__ptr_t) _st_thread_main;
    trd->context[0].__jmpbuf[0].__sp = stack->sp;
#else
    int ret_setjmp = 0;
    if ((ret_setjmp = MD_SETJMP((trd)->context)) != 0) {
        _st_thread_main();
    }
    MD_GET_SP(trd) = (long) (stack->sp);
#endif
    
    /* If thread is joinable, allocate a termination condition variable */
    if (joinable) {
        trd->term = st_cond_new();
        if (trd->term == NULL) {
            _st_stack_free(trd->stack);
            return NULL;
        }
    }
    
    /* Make thread runnable */
    trd->state = _ST_ST_RUNNABLE;
    _st_active_count++;
    _ST_ADD_RUNQ(trd);
#ifdef DEBUG
    _ST_ADD_THREADQ(trd);
#endif
    
    return trd;
}

_st_thread_t *st_thread_self(void)
{
    return _ST_CURRENT_THREAD();
}

#ifdef DEBUG
/* ARGSUSED */
void _st_show_thread_stack(_st_thread_t *trd, const char *messg)
{
}

/* To be set from debugger */
int _st_iterate_threads_flag = 0;

void _st_iterate_threads(void)
{
    static _st_thread_t *trd = NULL;
    static jmp_buf orig_jb, save_jb;
    _st_clist_t *q;
    
    if (!_st_iterate_threads_flag) {
        if (trd) {
            memcpy(trd->context, save_jb, sizeof(jmp_buf));
            MD_LONGJMP(orig_jb, 1);
        }
        return;
    }
    
    if (trd) {
        memcpy(trd->context, save_jb, sizeof(jmp_buf));
        _st_show_thread_stack(trd, NULL);
    } else {
        if (MD_SETJMP(orig_jb)) {
            _st_iterate_threads_flag = 0;
            trd = NULL;
            _st_show_thread_stack(trd, "Iteration completed");
            return;
        }
        trd = _ST_CURRENT_THREAD();
        _st_show_thread_stack(trd, "Iteration started");
    }
    
    q = trd->tlink.next;
    if (q == &_ST_THREADQ) {
        q = q->next;
    }
    ST_ASSERT(q != &_ST_THREADQ);
    trd = _ST_THREAD_THREADQ_PTR(q);
    if (trd == _ST_CURRENT_THREAD()) {
        MD_LONGJMP(orig_jb, 1);
    }
    memcpy(save_jb, trd->context, sizeof(jmp_buf));
    MD_LONGJMP(trd->context, 1);
}
#endif /* DEBUG */

