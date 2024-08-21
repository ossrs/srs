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

#ifndef __ST_COMMON_H__
#define __ST_COMMON_H__

#include <stddef.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>
#include <setjmp.h>

/* Enable assertions only if DEBUG is defined */
#ifndef DEBUG
    #define NDEBUG
#endif
#include <assert.h>
#define ST_ASSERT(expr) assert(expr)

#define ST_BEGIN_MACRO  {
#define ST_END_MACRO    }

#ifdef DEBUG
    #define ST_HIDDEN   /*nothing*/
#else
    #define    ST_HIDDEN   static
#endif

#include "public.h"
#include "md.h"

#ifdef __cplusplus
extern "C" {
#endif


/*****************************************
 * Circular linked list definitions
 */

typedef struct _st_clist {
    struct _st_clist *next;
    struct _st_clist *prev;
} _st_clist_t;

/* Initialize a circular list */
static inline void st_clist_init(_st_clist_t *l)
{
    l->next = l;
    l->prev = l;
}

/* Remove the element "_e" from it's circular list */
static inline void st_clist_remove(_st_clist_t *e)
{
    e->prev->next = e->next;
    e->next->prev = e->prev;
}

/* Insert element "_e" into the list, before "_l" */
static inline void st_clist_insert_before(_st_clist_t *e, _st_clist_t *l)
{
    e->next = l;
    e->prev = l->prev;
    l->prev->next = e;
    l->prev = e;
}

/* Insert element "_e" into the list, after "_l" */
static inline void st_clist_insert_after(_st_clist_t *e, _st_clist_t *l)
{
    e->next = l->next;
    e->prev = l;
    l->next->prev = e;
    l->next = e;
}


/*****************************************
 * Basic types definitions
 */

typedef void  (*_st_destructor_t)(void *);


typedef struct _st_stack {
    _st_clist_t links;
    char *vaddr;                /* Base of stack's allocated memory */
    int  vaddr_size;            /* Size of stack's allocated memory */
    int  stk_size;              /* Size of usable portion of the stack */
    char *stk_bottom;           /* Lowest address of stack's usable portion */
    char *stk_top;              /* Highest address of stack's usable portion */
    void *sp;                   /* Stack pointer from C's point of view */
    /* merge from https://github.com/toffaletti/state-threads/commit/7f57fc9acc05e657bca1223f1e5b9b1a45ed929b */
#ifdef MD_VALGRIND
    /* id returned by VALGRIND_STACK_REGISTER */
    /* http://valgrind.org/docs/manual/manual-core-adv.html */
    unsigned long valgrind_stack_id;
#endif
} _st_stack_t;


typedef struct _st_cond {
    _st_clist_t wait_q;          /* Condition variable wait queue */
} _st_cond_t;

typedef struct _st_thread _st_thread_t;

struct _st_thread {
    int state;                  /* Thread's state */
    int flags;                  /* Thread's flags */

    void *(*start)(void *arg);  /* The start function of the thread */
    void *arg;                  /* Argument of the start function */
    void *retval;               /* Return value of the start function */

    _st_stack_t *stack;          /* Info about thread's stack */

    _st_clist_t links;          /* For putting on run/sleep/zombie queue */
    _st_clist_t wait_links;     /* For putting on mutex/condvar wait queue */
#ifdef DEBUG
    _st_clist_t tlink;          /* For putting on thread queue */
#endif

    st_utime_t due;             /* Wakeup time when thread is sleeping */
    _st_thread_t *left;         /* For putting in timeout heap */
    _st_thread_t *right;          /* -- see docs/timeout_heap.txt for details */
    int heap_index;

    void **private_data;        /* Per thread private data */

    _st_cond_t *term;           /* Termination condition variable for join */

    _st_jmp_buf_t context;            /* Thread's context */
};


typedef struct _st_mutex {
    _st_thread_t *owner;        /* Current mutex owner */
    _st_clist_t  wait_q;        /* Mutex wait queue */
} _st_mutex_t;


typedef struct _st_pollq {
    _st_clist_t links;          /* For putting on io queue */
    _st_thread_t  *thread;      /* Polling thread */
    struct pollfd *pds;         /* Array of poll descriptors */
    int npds;                   /* Length of the array */
    int on_ioq;                 /* Is it on ioq? */
} _st_pollq_t;


typedef struct _st_eventsys_ops {
    const char *name;                          /* Name of this event system */
    int  val;                                  /* Type of this event system */
    int  (*init)(void);                        /* Initialization */
    void (*dispatch)(void);                    /* Dispatch function */
    int  (*pollset_add)(struct pollfd *, int); /* Add descriptor set */
    void (*pollset_del)(struct pollfd *, int); /* Delete descriptor set */
    int  (*fd_new)(int);                       /* New descriptor allocated */
    int  (*fd_close)(int);                     /* Descriptor closed */
    int  (*fd_getlimit)(void);                 /* Descriptor hard limit */
    void (*destroy)(void);                     /* Destroy the event object */
} _st_eventsys_t;


typedef struct _st_vp {
    _st_thread_t *idle_thread;  /* Idle thread for this vp */
    st_utime_t last_clock;      /* The last time we went into vp_check_clock() */

    _st_clist_t run_q;          /* run queue for this vp */
    _st_clist_t io_q;           /* io queue for this vp */
    _st_clist_t zombie_q;       /* zombie queue for this vp */
#ifdef DEBUG
    _st_clist_t thread_q;       /* all threads of this vp */
#endif
    int pagesize;

    _st_thread_t *sleep_q;      /* sleep queue for this vp */
    int sleepq_size;          /* number of threads on sleep queue */

#ifdef ST_SWITCH_CB
    st_switch_cb_t switch_out_cb;    /* called when a thread is switched out */
    st_switch_cb_t switch_in_cb;    /* called when a thread is switched in */
#endif
} _st_vp_t;


typedef struct _st_netfd {
    int osfd;                   /* Underlying OS file descriptor */
    int inuse;                  /* In-use flag */
    void *private_data;         /* Per descriptor private data */
    _st_destructor_t destructor; /* Private data destructor function */
    void *aux_data;             /* Auxiliary data for internal use */
    struct _st_netfd *next;     /* For putting on the free list */
} _st_netfd_t;


/*****************************************
 * Current vp, thread, and event system
 */

extern __thread _st_vp_t        _st_this_vp;
extern __thread _st_thread_t *_st_this_thread;
extern __thread _st_eventsys_t *_st_eventsys;


/*****************************************
 * Thread states and flags
 */

#define _ST_ST_RUNNING      0 
#define _ST_ST_RUNNABLE     1
#define _ST_ST_IO_WAIT      2
#define _ST_ST_LOCK_WAIT    3
#define _ST_ST_COND_WAIT    4
#define _ST_ST_SLEEPING     5
#define _ST_ST_ZOMBIE       6
#define _ST_ST_SUSPENDED    7

#define _ST_FL_PRIMORDIAL   0x01
#define _ST_FL_IDLE_THREAD  0x02
#define _ST_FL_ON_SLEEPQ    0x04
#define _ST_FL_INTERRUPT    0x08
#define _ST_FL_TIMEDOUT     0x10


/*****************************************
 * Pointer conversion
 */

#ifndef offsetof
    #define offsetof(type, identifier) ((size_t)&(((type *)0)->identifier))
#endif

#define _ST_THREAD_PTR(_qp)         \
    ((_st_thread_t *)((char *)(_qp) - offsetof(_st_thread_t, links)))

#define _ST_THREAD_WAITQ_PTR(_qp)   \
    ((_st_thread_t *)((char *)(_qp) - offsetof(_st_thread_t, wait_links)))

#define _ST_THREAD_STACK_PTR(_qp)   \
    ((_st_stack_t *)((char*)(_qp) - offsetof(_st_stack_t, links)))

#define _ST_POLLQUEUE_PTR(_qp)      \
    ((_st_pollq_t *)((char *)(_qp) - offsetof(_st_pollq_t, links)))

#ifdef DEBUG
    #define _ST_THREAD_THREADQ_PTR(_qp) \
        ((_st_thread_t *)((char *)(_qp) - offsetof(_st_thread_t, tlink)))
#endif


/*****************************************
 * Constants
 */

#ifndef ST_UTIME_NO_TIMEOUT
    #define ST_UTIME_NO_TIMEOUT ((st_utime_t) -1LL)
#endif

#define ST_DEFAULT_STACK_SIZE (128*1024)  /* Includes register stack size */

#ifndef ST_KEYS_MAX
    #define ST_KEYS_MAX 16
#endif

#ifndef ST_MIN_POLLFDS_SIZE
    #define ST_MIN_POLLFDS_SIZE 64
#endif


/*****************************************
 * Threads context switching
 */

#ifdef DEBUG
    void _st_iterate_threads(void);
    #define ST_DEBUG_ITERATE_THREADS() _st_iterate_threads()
#else
    #define ST_DEBUG_ITERATE_THREADS()
#endif

#ifdef ST_SWITCH_CB
    #define ST_SWITCH_OUT_CB(_thread)        \
        if (_st_this_vp.switch_out_cb != NULL &&    \
            _thread != _st_this_vp.idle_thread &&    \
            _thread->state != _ST_ST_ZOMBIE) {    \
          _st_this_vp.switch_out_cb();        \
        }
    #define ST_SWITCH_IN_CB(_thread)        \
        if (_st_this_vp.switch_in_cb != NULL &&    \
        _thread != _st_this_vp.idle_thread &&    \
        _thread->state != _ST_ST_ZOMBIE) {    \
          _st_this_vp.switch_in_cb();        \
        }
#else
    #define ST_SWITCH_OUT_CB(_thread)
    #define ST_SWITCH_IN_CB(_thread)
#endif

/*
 * Switch away from the current thread context by saving its state and
 * calling the thread scheduler
 */
void _st_switch_context(_st_thread_t *thread);

/*
 * Restore a thread context that was saved by _st_switch_context or
 * initialized by _ST_INIT_CONTEXT
 */
void _st_restore_context(_st_thread_t *thread);

/*
 * Number of bytes reserved under the stack "bottom"
 */
#define _ST_STACK_PAD_SIZE 128


/*****************************************
 * Forward declarations
 */

void _st_vp_schedule(void);
void _st_vp_check_clock(void);
void *_st_idle_thread_start(void *arg);
void _st_thread_main(void);
void _st_thread_cleanup(_st_thread_t *thread);
void _st_add_sleep_q(_st_thread_t *thread, st_utime_t timeout);
void _st_del_sleep_q(_st_thread_t *thread);
_st_stack_t *_st_stack_new(int stack_size);
void _st_stack_free(_st_stack_t *ts);
int _st_io_init(void);

st_utime_t st_utime(void);
_st_cond_t *st_cond_new(void);
int st_cond_destroy(_st_cond_t *cvar);
int st_cond_timedwait(_st_cond_t *cvar, st_utime_t timeout);
int st_cond_signal(_st_cond_t *cvar);
ssize_t st_read(_st_netfd_t *fd, void *buf, size_t nbyte, st_utime_t timeout);
ssize_t st_write(_st_netfd_t *fd, const void *buf, size_t nbyte, st_utime_t timeout);
int st_poll(struct pollfd *pds, int npds, st_utime_t timeout);
_st_thread_t *st_thread_create(void *(*start)(void *arg), void *arg, int joinable, int stk_size);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* !__ST_COMMON_H__ */

