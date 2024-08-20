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

