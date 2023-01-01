//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#ifndef SRS_CORE_PLATFORM_HPP
#define SRS_CORE_PLATFORM_HPP

// For 32bit os, 2G big file limit for unistd io,
// ie. read/write/lseek to use 64bits size for huge file.
#ifndef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 64
#endif

// For int64_t print using PRId64 format.
#ifndef __STDC_FORMAT_MACROS
#define __STDC_FORMAT_MACROS
#endif

// For RTC/FFMPEG build.
#if defined(SRS_RTC) && !defined(__STDC_CONSTANT_MACROS)
#define __STDC_CONSTANT_MACROS
#endif

// For srs-librtmp, @see https://github.com/ossrs/srs/issues/213
#ifndef _WIN32
#include <inttypes.h>
#endif

#include <stddef.h>
#include <sys/types.h>

// For CentOS 6 or C++98, @see https://github.com/ossrs/srs/issues/2815
#ifndef UINT32_MAX
#define UINT32_MAX (4294967295U)
#endif

#endif

