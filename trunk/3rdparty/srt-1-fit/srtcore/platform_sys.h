/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2018 Haivision Systems Inc.
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 * 
 */
#ifndef INC_SRT_PLATFORM_SYS_H
#define INC_SRT_PLATFORM_SYS_H

// INFORMATION
//
// This file collects all required platform-specific declarations
// required to provide everything that the SRT library needs from system.
//
// There's also semi-modular system implemented using SRT_IMPORT_* macros.
// To require a module to be imported, #define SRT_IMPORT_* where * is
// the module name. Currently handled module macros:
//
// SRT_IMPORT_TIME   (mach time on Mac, portability gettimeofday on WIN32)
// SRT_IMPORT_EVENT  (includes kevent on Mac)


#ifdef _WIN32
   #include <winsock2.h>
   #include <ws2tcpip.h>
   #include <ws2ipdef.h>
   #include <windows.h>

#ifndef __MINGW32__
   #include <intrin.h>
#endif

   #ifdef SRT_IMPORT_TIME
   #include <win/wintime.h>
   #endif

   #include <stdint.h>
   #include <inttypes.h>
#else

#if defined(__APPLE__) && __APPLE__
// Warning: please keep this test as it is, do not make it
// "#if __APPLE__" or "#ifdef __APPLE__". In applications with
// a strict "no warning policy", "#if __APPLE__" triggers an "undef"
// error. With GCC, an old & never fixed bug prevents muting this
// warning (see https://gcc.gnu.org/bugzilla/show_bug.cgi?id=53431).
// Before this fix, the solution was to "#define __APPLE__ 0" before
// including srt.h. So, don't use "#ifdef __APPLE__" either.

// XXX Check if this condition doesn't require checking of
// also other macros, like TARGET_OS_IOS etc.

#include "TargetConditionals.h"
#define __APPLE_USE_RFC_3542 /* IPV6_PKTINFO */

#ifdef SRT_IMPORT_TIME
      #include <mach/mach_time.h>
#endif

#ifdef SRT_IMPORT_EVENT
   #include <sys/types.h>
   #include <sys/event.h>
   #include <sys/time.h>
   #include <unistd.h>
#endif

#endif

#ifdef BSD
#ifdef SRT_IMPORT_EVENT
   #include <sys/types.h>
   #include <sys/event.h>
   #include <sys/time.h>
   #include <unistd.h>
#endif
#endif

#ifdef LINUX

#ifdef SRT_IMPORT_EVENT
   #include <sys/epoll.h>
   #include <unistd.h>
#endif

#endif

#ifdef __ANDROID__

#ifdef SRT_IMPORT_EVENT
   #include <sys/select.h>
#endif

#endif

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>

#ifdef __cplusplus
// Headers for errno, string and stdlib are
// included indirectly correct C++ way.
#else
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#endif

#endif

#endif
