//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_CORE_TIME_HPP
#define SRS_CORE_TIME_HPP

// Time and duration, in microseconds.
#include <stdint.h>
typedef int64_t srs_utime_t;

#define SRS_UTIME_MILLISECONDS 1000

#define srsu2ms(us) ((us) / SRS_UTIME_MILLISECONDS)
#define srsu2msi(us) int((us) / SRS_UTIME_MILLISECONDS)

#define srsu2s(us) ((us) / SRS_UTIME_SECONDS)
#define srsu2si(us) int((us) / SRS_UTIME_SECONDS)

srs_utime_t srs_time_since(srs_utime_t start, srs_utime_t end);

#define SRS_UTIME_SECONDS 1000000LL

#define SRS_UTIME_MINUTES 60000000LL

#define SRS_UTIME_HOURS 3600000000LL

#define SRS_UTIME_NO_TIMEOUT ((srs_utime_t) - 1LL)

// Cached for performance. A daemon st-thread refreshes the cache by calling srs_time_now_realtime().
extern srs_utime_t srs_time_now_cached();
extern srs_utime_t srs_time_since_startup();
extern srs_utime_t srs_time_now_realtime();

class ISrsTime
{
public:
    ISrsTime();
    virtual ~ISrsTime();

public:
    virtual void usleep(srs_utime_t duration) = 0;
};

#endif
