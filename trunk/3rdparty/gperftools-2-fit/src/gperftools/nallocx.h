#ifndef _NALLOCX_H_
#define _NALLOCX_H_
#include <stddef.h>

#ifndef PERFTOOLS_DLL_DECL
# ifdef _WIN32
#  define PERFTOOLS_DLL_DECL  __declspec(dllimport)
# else
#  define PERFTOOLS_DLL_DECL
# endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define MALLOCX_LG_ALIGN(la) ((int)(la))

/*
 * The nallocx function allocates no memory, but it performs the same size
 * computation as the malloc function, and returns the real size of the
 * allocation that would result from the equivalent malloc function call.
 * nallocx is a malloc extension originally implemented by jemalloc:
 * http://www.unix.com/man-page/freebsd/3/nallocx/
 *
 * Note, we only support MALLOCX_LG_ALIGN flag and nothing else.
 */
PERFTOOLS_DLL_DECL size_t nallocx(size_t size, int flags);

/* same as above but never weak */
PERFTOOLS_DLL_DECL size_t tc_nallocx(size_t size, int flags);

#ifdef __cplusplus
}   /* extern "C" */
#endif

#endif /* _NALLOCX_H_ */
