/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2021-2022 The SRS Authors */

#include "common.h"

void _st_switch_context(_st_thread_t *thread)
{
    ST_SWITCH_OUT_CB(thread);

    if (!_st_md_cxt_save(thread->context)) {
        _st_vp_schedule();
    }

    ST_DEBUG_ITERATE_THREADS();
    ST_SWITCH_IN_CB(thread);
}

void _st_restore_context(_st_thread_t *thread)
{
    _st_this_thread = thread;
    _st_md_cxt_restore(thread->context, 1);
}

