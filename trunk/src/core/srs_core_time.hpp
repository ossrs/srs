//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#ifndef SRS_CORE_TIME_HPP
#define SRS_CORE_TIME_HPP

// Time and duration unit, in us.
#if defined(_WIN32) && !defined(__MINGW32__) && (!defined(_MSC_VER) || _MSC_VER<1600) && !defined(__WINE__)
#include <BaseTsd.h>
typedef __int64 srs_utime_t;
#else
#include <stdint.h>
typedef int64_t srs_utime_t;
#endif

// The time unit in ms, for example 100 * SRS_UTIME_MILLISECONDS means 100ms.
#define SRS_UTIME_MILLISECONDS 1000

// Convert srs_utime_t as ms.
#define srsu2ms(us) ((us) / SRS_UTIME_MILLISECONDS)
#define srsu2msi(us) int((us) / SRS_UTIME_MILLISECONDS)

// Convert srs_utime_t as second.
#define srsu2s(us) ((us) / SRS_UTIME_SECONDS)
#define srsu2si(us) ((us) / SRS_UTIME_SECONDS)

// Them time duration = end - start. return 0, if start or end is 0.
srs_utime_t srs_duration(srs_utime_t start, srs_utime_t end);

// The time unit in ms, for example 120 * SRS_UTIME_SECONDS means 120s.
#define SRS_UTIME_SECONDS 1000000LL

// The time unit in minutes, for example 3 * SRS_UTIME_MINUTES means 3m.
#define SRS_UTIME_MINUTES 60000000LL

// The time unit in hours, for example 2 * SRS_UTIME_HOURS means 2h.
#define SRS_UTIME_HOURS 3600000000LL

// Never timeout.
#define SRS_UTIME_NO_TIMEOUT ((srs_utime_t) -1LL)

#endif

