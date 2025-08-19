/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2021-2022 The SRS Authors */

#include "common.h"

void *_st_primordial_stack_bottom = NULL;
size_t _st_primordial_stack_size = 0;

void st_set_primordial_stack(void *top, void *bottom)
{
    _st_primordial_stack_bottom = bottom;
    _st_primordial_stack_size = (char *)top - (char *)bottom;
}

