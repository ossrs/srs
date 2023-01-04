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

#ifndef INC_SRT_COMPAT_H
#define INC_SRT_COMPAT_H

#include <stddef.h>
#include <time.h>

#ifndef SRT_API
#ifdef _WIN32
   #ifndef __MINGW32__
      #ifdef SRT_DYNAMIC
         #ifdef SRT_EXPORTS
            #define SRT_API __declspec(dllexport)
         #else
            #define SRT_API __declspec(dllimport)
         #endif
      #else
         #define SRT_API
      #endif
   #else
      #define SRT_API
   #endif
#else
   #define SRT_API __attribute__ ((visibility("default")))
#endif
#endif

#ifdef _WIN32
   // https://msdn.microsoft.com/en-us/library/tcxf1dw6.aspx
   // printf() Format for ssize_t
   #if !defined(PRIzd)
      #define PRIzd "Id"
   #endif
   // printf() Format for size_t
   #if !defined(PRIzu)
      #define PRIzu "Iu"
   #endif
#else
   // http://www.gnu.org/software/libc/manual/html_node/Integer-Conversions.html
   // printf() Format for ssize_t
   #if !defined(PRIzd)
      #define PRIzd "zd"
   #endif
   // printf() Format for size_t
   #if !defined(PRIzu)
      #define PRIzu "zu"
   #endif
#endif


#ifdef __cplusplus
extern "C" {
#endif

/* Ensures that we store the error in the buffer and return the bufer. */
SRT_API const char * SysStrError(int errnum, char * buf, size_t buflen);

#ifdef __cplusplus
} // extern C


// Extra C++ stuff. Included only in C++ mode.


#include <string>
#include <cstring>
inline std::string SysStrError(int errnum)
{
    char buf[1024];
    return SysStrError(errnum, buf, 1024);
}

inline struct tm SysLocalTime(time_t tt)
{
    struct tm tms;
    memset(&tms, 0, sizeof tms);
#ifdef _WIN32
	errno_t rr = localtime_s(&tms, &tt);
	if (rr == 0)
		return tms;
#else

    // Ignore the error, state that if something
    // happened, you simply have a pre-cleared tms.
    localtime_r(&tt, &tms);
#endif

    return tms;
}


#endif // defined C++

#endif // INC_SRT_COMPAT_H
