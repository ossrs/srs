/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2018 Haivision Systems Inc.
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 * 
 */

/*****************************************************************************
written by
   Haivision Systems Inc.
 *****************************************************************************/

#include "win/wintime.h"
#include <sys/timeb.h>

void SRTCompat_timeradd(struct timeval *a, struct timeval *b, struct timeval *result)
{
    result->tv_sec  = a->tv_sec + b->tv_sec;
    result->tv_usec = a->tv_usec + b->tv_usec;
    if (result->tv_usec >= 1000000)
    {
        result->tv_sec++;
        result->tv_usec -= 1000000;
    }
}

int SRTCompat_gettimeofday(struct timeval* tp, struct timezone*)
{
    struct timeb tb;
    ftime(&tb);
    tp->tv_sec  = (long)tb.time;
    tp->tv_usec = 1000*tb.millitm;
    return 0;
}
