/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2021-2022 The SRS Authors */

#include "common.h"

void st_clist_init(_st_clist_t *l)
{
    l->next = l;
    l->prev = l;
}

void st_clist_remove(_st_clist_t *e)
{
    e->prev->next = e->next;
    e->next->prev = e->prev;
}

void st_clist_insert_before(_st_clist_t *e, _st_clist_t *l)
{
    e->next = l;
    e->prev = l->prev;
    l->prev->next = e;
    l->prev = e;
}

void st_clist_insert_after(_st_clist_t *e, _st_clist_t *l)
{
    e->next = l->next;
    e->prev = l;
    l->next->prev = e;
    l->next = e;
}

void _st_switch_context(_st_thread_t *thread)
{
    ST_SWITCH_OUT_CB(thread);

    if (!MD_SETJMP((thread)->context)) {
        _st_vp_schedule();
    }

    ST_DEBUG_ITERATE_THREADS();
    ST_SWITCH_IN_CB(thread);
}

void _st_restore_context(_st_thread_t *thread)
{
    _st_this_thread = thread;
    MD_LONGJMP(thread->context, 1);
}

