/* -*- Mode: C++; c-basic-offset: 2; indent-tabs-mode: nil -*- */
/* A manual version of config.h fit for windows machines.
 *
 * Use of this source code is governed by a BSD-style license that can
 * be found in the LICENSE file.
 */

/* Sometimes we accidentally #include this config.h instead of the one
   in .. -- this is particularly true for msys/mingw, which uses the
   unix config.h but also runs code in the windows directory.
   */
#ifdef __MINGW32__
#include "../config.h"
#define GOOGLE_PERFTOOLS_WINDOWS_CONFIG_H_
#endif

#ifndef GOOGLE_PERFTOOLS_WINDOWS_CONFIG_H_
#define GOOGLE_PERFTOOLS_WINDOWS_CONFIG_H_
/* used by tcmalloc.h */
#define GPERFTOOLS_CONFIG_H_

/* Enable aggressive decommit by default */
/* #undef ENABLE_AGGRESSIVE_DECOMMIT_BY_DEFAULT */

/* Build new/delete operators for overaligned types */
/* #undef ENABLE_ALIGNED_NEW_DELETE */

/* Build runtime detection for sized delete */
/* #undef ENABLE_DYNAMIC_SIZED_DELETE */

/* Report large allocation */
/* #undef ENABLE_LARGE_ALLOC_REPORT */

/* Build sized deletion operators */
/* #undef ENABLE_SIZED_DELETE */

/* Define to 1 if you have the <asm/ptrace.h> header file. */
/* #undef HAVE_ASM_PTRACE_H */

/* Define to 1 if you have the <cygwin/signal.h> header file. */
/* #undef HAVE_CYGWIN_SIGNAL_H */

/* Define to 1 if you have the declaration of `backtrace', and to 0 if you
   don't. */
/* #undef HAVE_DECL_BACKTRACE */

/* Define to 1 if you have the declaration of `cfree', and to 0 if you don't.
   */
#define HAVE_DECL_CFREE 0

/* Define to 1 if you have the declaration of `memalign', and to 0 if you
   don't. */
#define HAVE_DECL_MEMALIGN 0

/* Define to 1 if you have the declaration of `nanosleep', and to 0 if you
   don't. */
#define HAVE_DECL_NANOSLEEP 0

/* Define to 1 if you have the declaration of `posix_memalign', and to 0 if
   you don't. */
#define HAVE_DECL_POSIX_MEMALIGN 0

/* Define to 1 if you have the declaration of `pvalloc', and to 0 if you
   don't. */
#define HAVE_DECL_PVALLOC 0

/* Define to 1 if you have the declaration of `sleep', and to 0 if you don't.
   */
#define HAVE_DECL_SLEEP 0

/* Define to 1 if you have the declaration of `valloc', and to 0 if you don't.
   */
#define HAVE_DECL_VALLOC 0

/* Define to 1 if you have the <dlfcn.h> header file. */
/* #undef HAVE_DLFCN_H */

/* Define to 1 if the system has the type `Elf32_Versym'. */
/* #undef HAVE_ELF32_VERSYM */

/* Define to 1 if you have the <execinfo.h> header file. */
/* #undef HAVE_EXECINFO_H */

/* Define to 1 if you have the <fcntl.h> header file. */
#define HAVE_FCNTL_H 1

/* Define to 1 if you have the <features.h> header file. */
/* #undef HAVE_FEATURES_H */

/* Define to 1 if you have the `fork' function. */
/* #undef HAVE_FORK */

/* Define to 1 if you have the `geteuid' function. */
/* #undef HAVE_GETEUID */

/* Define to 1 if you have the <glob.h> header file. */
/* #undef HAVE_GLOB_H */

/* Define to 1 if you have the <grp.h> header file. */
/* #undef HAVE_GRP_H */

/* Define to 1 if you have the <inttypes.h> header file. */
#if defined(_MSC_VER) && _MSC_VER >= 1900
#define HAVE_INTTYPES_H 1
#endif

/* Define to 1 if you have the <libunwind.h> header file. */
/* #undef HAVE_LIBUNWIND_H */

/* Define to 1 if you have the <linux/ptrace.h> header file. */
/* #undef HAVE_LINUX_PTRACE_H */

/* Define if this is Linux that has SIGEV_THREAD_ID */
/* #undef HAVE_LINUX_SIGEV_THREAD_ID */

/* Define to 1 if you have the <malloc.h> header file. */
#define HAVE_MALLOC_H 1

/* Define to 1 if you have the <memory.h> header file. */
#define HAVE_MEMORY_H 1

/* Define to 1 if you have a working `mmap' system call. */
/* #undef HAVE_MMAP */

/* Define to 1 if you have the <poll.h> header file. */
/* #undef HAVE_POLL_H */

/* define if libc has program_invocation_name */
/* #undef HAVE_PROGRAM_INVOCATION_NAME */

/* Define if you have POSIX threads libraries and header files. */
/* #undef HAVE_PTHREAD */

/* defined to 1 if pthread symbols are exposed even without include pthread.h
   */
/* #undef HAVE_PTHREAD_DESPITE_ASKING_FOR */

/* Define to 1 if you have the <pwd.h> header file. */
/* #undef HAVE_PWD_H */

/* Define to 1 if you have the `sbrk' function. */
/* #undef HAVE_SBRK */

/* Define to 1 if you have the <sched.h> header file. */
/* #undef HAVE_SCHED_H */

/* Define to 1 if you have the <stdint.h> header file. */
#define HAVE_STDINT_H 1

/* Define to 1 if you have the <stdlib.h> header file. */
#define HAVE_STDLIB_H 1

/* Define to 1 if you have the <strings.h> header file. */
/* #undef HAVE_STRINGS_H */

/* Define to 1 if you have the <string.h> header file. */
#define HAVE_STRING_H 1

/* Define to 1 if the system has the type `struct mallinfo'. */
/* #undef HAVE_STRUCT_MALLINFO */

/* Define to 1 if you have the <sys/cdefs.h> header file. */
/* #undef HAVE_SYS_CDEFS_H */

/* Define to 1 if you have the <sys/prctl.h> header file. */
/* #undef HAVE_SYS_PRCTL_H */

/* Define to 1 if you have the <sys/resource.h> header file. */
/* #undef HAVE_SYS_RESOURCE_H */

/* Define to 1 if you have the <sys/socket.h> header file. */
/* #undef HAVE_SYS_SOCKET_H */

/* Define to 1 if you have the <sys/stat.h> header file. */
#define HAVE_SYS_STAT_H 1

/* Define to 1 if you have the <sys/syscall.h> header file. */
/* #undef HAVE_SYS_SYSCALL_H */

/* Define to 1 if you have the <sys/types.h> header file. */
#define HAVE_SYS_TYPES_H 1

/* Define to 1 if you have the <sys/ucontext.h> header file. */
/* #undef HAVE_SYS_UCONTEXT_H */

/* Define to 1 if you have the <sys/wait.h> header file. */
/* #undef HAVE_SYS_WAIT_H */

/* Define to 1 if compiler supports __thread */
#define HAVE_TLS 1

/* Define to 1 if you have the <ucontext.h> header file. */
/* #undef HAVE_UCONTEXT_H */

/* Define to 1 if you have the <unistd.h> header file. */
/* #undef HAVE_UNISTD_H */

/* Whether <unwind.h> contains _Unwind_Backtrace */
/* #undef HAVE_UNWIND_BACKTRACE */

/* Define to 1 if you have the <unwind.h> header file. */
/* #undef HAVE_UNWIND_H */

/* define if your compiler has __attribute__ */
/* #undef HAVE___ATTRIBUTE__ */

/* define if your compiler supports alignment of functions */
/* #undef HAVE___ATTRIBUTE__ALIGNED_FN */

/* Define to 1 if compiler supports __environ */
/* #undef HAVE___ENVIRON */

/* Define to 1 if you have the `__sbrk' function. */
/* #undef HAVE___SBRK */

/* prefix where we look for installed files */
/* #undef INSTALL_PREFIX */

/* Define to 1 if int32_t is equivalent to intptr_t */
#ifndef _WIN64
#define INT32_EQUALS_INTPTR 1
#endif

/* Define to the sub-directory where libtool stores uninstalled libraries. */
/* #undef LT_OBJDIR */

/* Name of package */
#define PACKAGE "gperftools"

/* Define to the address where bug reports for this package should be sent. */
#define PACKAGE_BUGREPORT "gperftools@googlegroups.com"

/* Define to the full name of this package. */
#define PACKAGE_NAME "gperftools"

/* Define to the full name and version of this package. */
#define PACKAGE_STRING "gperftools 2.9.1"

/* Define to the one symbol short name of this package. */
#define PACKAGE_TARNAME "gperftools"

/* Define to the home page for this package. */
#define PACKAGE_URL ""

/* Define to the version of this package. */
#define PACKAGE_VERSION "2.9.1"

/* How to access the PC from a struct ucontext */
/* #undef PC_FROM_UCONTEXT */

/* Always the empty-string on non-windows systems. On windows, should be
   "__declspec(dllexport)". This way, when we compile the dll, we export our
   functions/classes. It's safe to define this here because config.h is only
   used internally, to compile the DLL, and every DLL source file #includes
   "config.h" before anything else. */
#ifndef PERFTOOLS_DLL_DECL
# define PERFTOOLS_IS_A_DLL 1   /* not set if you're statically linking */
# define PERFTOOLS_DLL_DECL __declspec(dllexport)
# define PERFTOOLS_DLL_DECL_FOR_UNITTESTS __declspec(dllimport)
#endif

/* Mark the systems where we know it's bad if pthreads runs too
   early before main (before threads are initialized, presumably).  */
#ifdef __FreeBSD__
#define PTHREADS_CRASHES_IF_RUN_TOO_EARLY 1
#endif

/* Define to necessary symbol if this constant uses a non-standard name on
   your system. */
/* #undef PTHREAD_CREATE_JOINABLE */

/* Define to 1 if you have the ANSI C header files. */
#define STDC_HEADERS 1

/* Define 8 bytes of allocation alignment for tcmalloc */
/* #undef TCMALLOC_ALIGN_8BYTES */

/* Define internal page size for tcmalloc as number of left bitshift */
/* #undef TCMALLOC_PAGE_SIZE_SHIFT */

/* Version number of package */
#define VERSION "2.9.1"

/* C99 says: define this to get the PRI... macros from stdint.h */
#ifndef __STDC_FORMAT_MACROS
# define __STDC_FORMAT_MACROS 1
#endif

// ---------------------------------------------------------------------
// Extra stuff not found in config.h.in

// This must be defined before the windows.h is included.  We need at
// least 0x0400 for mutex.h to have access to TryLock, and at least
// 0x0501 for patch_functions.cc to have access to GetModuleHandleEx.
// (This latter is an optimization we could take out if need be.)
#ifndef _WIN32_WINNT
# define _WIN32_WINNT 0x0501
#endif

// We want to make sure not to ever try to #include heap-checker.h
#define NO_HEAP_CHECK 1

// TODO(csilvers): include windows/port.h in every relevant source file instead?
#include "windows/port.h"

#endif  /* GOOGLE_PERFTOOLS_WINDOWS_CONFIG_H_ */
