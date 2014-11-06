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
#include <sys/mman.h>
#include "common.h"

/* How much space to leave between the stacks, at each end */
#define REDZONE    _ST_PAGE_SIZE

_st_clist_t _st_free_stacks = ST_INIT_STATIC_CLIST(&_st_free_stacks);
int _st_num_free_stacks = 0;
int _st_randomize_stacks = 0;

static char *_st_new_stk_segment(int size);

_st_stack_t *_st_stack_new(int stack_size)
{
    _st_clist_t *qp;
    _st_stack_t *ts;
    int extra;
    
    for (qp = _st_free_stacks.next; qp != &_st_free_stacks; qp = qp->next) {
        ts = _ST_THREAD_STACK_PTR(qp);
        if (ts->stk_size >= stack_size) {
            /* Found a stack that is big enough */
            ST_REMOVE_LINK(&ts->links);
            _st_num_free_stacks--;
            ts->links.next = NULL;
            ts->links.prev = NULL;
            return ts;
        }
    }
    
    /* Make a new thread stack object. */
    if ((ts = (_st_stack_t *)calloc(1, sizeof(_st_stack_t))) == NULL) {
        return NULL;
    }
    extra = _st_randomize_stacks ? _ST_PAGE_SIZE : 0;
    ts->vaddr_size = stack_size + 2*REDZONE + extra;
    ts->vaddr = _st_new_stk_segment(ts->vaddr_size);
    if (!ts->vaddr) {
        free(ts);
        return NULL;
    }
    ts->stk_size = stack_size;
    ts->stk_bottom = ts->vaddr + REDZONE;
    ts->stk_top = ts->stk_bottom + stack_size;
    
#ifdef DEBUG
    mprotect(ts->vaddr, REDZONE, PROT_NONE);
    mprotect(ts->stk_top + extra, REDZONE, PROT_NONE);
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
    if (!ts) {
        return;
    }
    
    /* Put the stack on the free list */
    ST_APPEND_LINK(&ts->links, _st_free_stacks.prev);
    _st_num_free_stacks++;
}

static char *_st_new_stk_segment(int size)
{
#ifdef MALLOC_STACK
    void *vaddr = malloc(size);
#else
    static int zero_fd = -1;
    int mmap_flags = MAP_PRIVATE;
    void *vaddr;
    
    #if defined (MD_USE_SYSV_ANON_MMAP)
    if (zero_fd < 0) {
        if ((zero_fd = open("/dev/zero", O_RDWR, 0)) < 0) {
            return NULL;
        }
        fcntl(zero_fd, F_SETFD, FD_CLOEXEC);
    }
    #elif defined (MD_USE_BSD_ANON_MMAP)
    mmap_flags |= MAP_ANON;
    #else
        #error Unknown OS
    #endif
    
    vaddr = mmap(NULL, size, PROT_READ | PROT_WRITE, mmap_flags, zero_fd, 0);
    if (vaddr == (void *)MAP_FAILED) {
        return NULL;
    }
    
#endif
    
    return (char *)vaddr;
}

/* Not used */
#if 0
void _st_delete_stk_segment(char *vaddr, int size)
{
#ifdef MALLOC_STACK
    free(vaddr);
#else
    (void) munmap(vaddr, size);
#endif
}
#endif

int st_randomize_stacks(int on)
{
    int wason = _st_randomize_stacks;
    
    _st_randomize_stacks = on;
    if (on) {
        srandom((unsigned int) st_utime());
    }
    
    return wason;
}
