//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#include <srs_core_time.hpp>

srs_utime_t srs_duration(srs_utime_t start, srs_utime_t end)
{
    if (start == 0 || end == 0) {
        return 0;
    }

    return end - start;
}

