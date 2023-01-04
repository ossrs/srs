#ifndef INC_SRT_WIN_WINTIME
#define INC_SRT_WIN_WINTIME

#include <winsock2.h>
#include <windows.h>
// HACK: This include is a workaround for a bug in the MinGW headers
// where pthread.h, which defines _POSIX_THREAD_SAFE_FUNCTIONS,
// has to be included before time.h so that time.h defines
// localtime_r correctly
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

#if !defined(_MSC_VER)
   #define SRTCOMPAT_WINTIME_STATIC_INLINE_DECL static inline
#else
   // NOTE: MVC Does not like static inline for C functions in some versions.
   //    so just use static for MVC.
   #define SRTCOMPAT_WINTIME_STATIC_INLINE_DECL static
#endif

#ifndef _TIMEZONE_DEFINED /* also in sys/time.h */
#define _TIMEZONE_DEFINED
struct timezone 
{
    int tz_minuteswest; /* minutes W of Greenwich */
    int tz_dsttime;     /* type of dst correction */
};
#endif

void SRTCompat_timeradd(
      struct timeval *a, struct timeval *b, struct timeval *result);
SRTCOMPAT_WINTIME_STATIC_INLINE_DECL void timeradd(
      struct timeval *a, struct timeval *b, struct timeval *result)
{
   SRTCompat_timeradd(a, b, result);
}

int SRTCompat_gettimeofday(
      struct timeval* tp, struct timezone* tz);
SRTCOMPAT_WINTIME_STATIC_INLINE_DECL int gettimeofday(
      struct timeval* tp, struct timezone* tz)
{
   return SRTCompat_gettimeofday(tp, tz);
}

#undef SRTCOMPAT_WINTIME_STATIC_INLINE_DECL

#ifdef __cplusplus
}
#endif

#endif // INC_SRT_WIN_WINTIME
