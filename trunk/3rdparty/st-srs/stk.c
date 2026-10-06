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
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#if !defined(WIN64)
#include <sys/mman.h>
#endif
#include "common.h"

/* Protect the red zones with _st_mprotect, which is the system mprotect by default. */
#if defined(WIN64)
/* Windows has no mprotect: the red zones use VirtualProtect, on VirtualAlloc pages only. */
#define PROT_NONE  0x0
#define PROT_READ  0x1
#define PROT_WRITE 0x2
static int _st_win64_mprotect(void *addr, size_t len, int prot)
{
    DWORD old;
    return VirtualProtect(addr, len, prot ? PAGE_READWRITE : PAGE_NOACCESS, &old) ? 0 : -1;
}
#define _st_mprotect _st_win64_mprotect
#else
#define _st_mprotect mprotect
#endif


/* How much space to leave between the stacks, at each end */
#define REDZONE	_st_this_vp.pagesize

__thread _st_clist_t _st_free_stacks;
__thread int _st_num_free_stacks = 0;
__thread int _st_randomize_stacks = 0;

static char *_st_new_stk_segment(int size);
static void _st_delete_stk_segment(char *vaddr, int size);

_st_stack_t *_st_stack_new(int stack_size)
{
    _st_clist_t *qp;
    _st_stack_t *ts;
    int extra;

   /* If cache stack, we try to use stack from the cache list. */
#ifdef MD_CACHE_STACK
    for (qp = _st_free_stacks.next; qp != &_st_free_stacks; qp = qp->next) {
        ts = _ST_THREAD_STACK_PTR(qp);
        if (ts->stk_size >= stack_size) {
            /* Found a stack that is big enough */
            st_clist_remove(&ts->links);
            _st_num_free_stacks--;
            ts->links.next = NULL;
            ts->links.prev = NULL;
            return ts;
        }
    }
#endif

    extra = _st_randomize_stacks ? _st_this_vp.pagesize : 0;
    /* If not cache stack, we will free all stack in the list, which contains the stack to be freed.
     * Note that we should never directly free it at _st_stack_free, because it is still be used,
     * and will cause crash. */
#ifndef MD_CACHE_STACK
    for (qp = _st_free_stacks.next; qp != &_st_free_stacks;) {
        ts = _ST_THREAD_STACK_PTR(qp);
        /* Before qp is freed, move to next one, because the qp will be freed when free the ts. */
        qp = qp->next;

        st_clist_remove(&ts->links);
        _st_num_free_stacks--;

#if defined(DEBUG) && !defined(MD_NO_PROTECT)
        _st_mprotect(ts->vaddr, REDZONE, PROT_READ | PROT_WRITE);
        /* The upper red zone ends the segment; stk_top may be randomized, and extra may have changed since. */
        _st_mprotect(ts->vaddr + ts->vaddr_size - REDZONE, REDZONE, PROT_READ | PROT_WRITE);
#endif

        _st_delete_stk_segment(ts->vaddr, ts->vaddr_size);
        free(ts);
    }
#endif
    
    /* Make a new thread stack object. */
    if ((ts = (_st_stack_t *)calloc(1, sizeof(_st_stack_t))) == NULL) /* GCOVR_EXCL_BR_LINE */
        return NULL; /* GCOVR_EXCL_LINE */
    ts->vaddr_size = stack_size + 2*REDZONE + extra;
    ts->vaddr = _st_new_stk_segment(ts->vaddr_size);
    if (!ts->vaddr) { /* GCOVR_EXCL_BR_LINE */
        free(ts); /* GCOVR_EXCL_LINE */
        return NULL; /* GCOVR_EXCL_LINE */
    }
    ts->stk_size = stack_size;
    ts->stk_bottom = ts->vaddr + REDZONE;
    ts->stk_top = ts->stk_bottom + stack_size;

    /* For example, in OpenWRT, the memory at the begin minus 16B by mprotect is read-only. */
#if defined(DEBUG) && !defined(MD_NO_PROTECT)
    _st_mprotect(ts->vaddr, REDZONE, PROT_NONE);
    _st_mprotect(ts->stk_top + extra, REDZONE, PROT_NONE);
#endif
    
    if (extra) {
        long offset = (random() % extra) & ~0xf;
        
        ts->stk_bottom += offset;
        ts->stk_top += offset;
    }
    
    return ts;
}


/*
 * Free the stack for the current thread
 */
void _st_stack_free(_st_stack_t *ts)
{
    if (!ts)
        return; /* GCOVR_EXCL_LINE */

    /* Put the stack on the free list */
    st_clist_insert_before(&ts->links, _st_free_stacks.prev);
    _st_num_free_stacks++;
}


static char *_st_new_stk_segment(int size)
{
#ifdef MALLOC_STACK
    void *vaddr = malloc(size);
#elif defined(WIN64)
    void *vaddr = VirtualAlloc(NULL, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#else
    static int zero_fd = -1;
    int mmap_flags = MAP_PRIVATE;
    void *vaddr;
    
#if defined (MD_USE_SYSV_ANON_MMAP)
    if (zero_fd < 0) {
        if ((zero_fd = open("/dev/zero", O_RDWR, 0)) < 0)
            return NULL;
        fcntl(zero_fd, F_SETFD, FD_CLOEXEC);
    }
#elif defined (MD_USE_BSD_ANON_MMAP)
    mmap_flags |= MAP_ANON;
#else
#error Unknown OS
#endif
    
    vaddr = mmap(NULL, size, PROT_READ | PROT_WRITE, mmap_flags, zero_fd, 0);
    if (vaddr == (void *)MAP_FAILED) /* GCOVR_EXCL_BR_LINE */
        return NULL; /* GCOVR_EXCL_LINE */
    
#endif /* MALLOC_STACK */
    
    return (char *)vaddr;
}


void _st_delete_stk_segment(char *vaddr, int size)
{
#ifdef MALLOC_STACK
    free(vaddr);
#elif defined(WIN64)
    (void) size;
    (void) VirtualFree(vaddr, 0, MEM_RELEASE);
#else
    (void) munmap(vaddr, size);
#endif
}

int st_randomize_stacks(int on)
{
    int wason = _st_randomize_stacks;
    
    _st_randomize_stacks = on;
    if (on)
        srandom((unsigned int) st_utime());
    
    return wason;
}
