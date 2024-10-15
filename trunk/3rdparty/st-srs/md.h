/* SPDX-License-Identifier: MPL-1.1 OR GPL-2.0-or-later */

/*
 * The contents of this file are subject to the Mozilla Public
 * License Version 1.1 (the "License"); you may not use this file
 * except in compliance with the License. You may obtain a copy of
 * the License at http://www.mozilla.org/MPL/
 * 
 * Software distributed under the License is distributed on an "AS
 * IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or
 * implied. See the License for the specific language governing
 * rights and limitations under the License.
 * 
 * The Original Code is the Netscape Portable Runtime library.
 * 
 * The Initial Developer of the Original Code is Netscape
 * Communications Corporation.  Portions created by Netscape are 
 * Copyright (C) 1994-2000 Netscape Communications Corporation.  All
 * Rights Reserved.
 * 
 * Contributor(s):  Silicon Graphics, Inc.
 * 
 * Portions created by SGI are Copyright (C) 2000-2001 Silicon
 * Graphics, Inc.  All Rights Reserved.
 * 
 * Alternatively, the contents of this file may be used under the
 * terms of the GNU General Public License Version 2 or later (the
 * "GPL"), in which case the provisions of the GPL are applicable 
 * instead of those above.  If you wish to allow use of your 
 * version of this file only under the terms of the GPL and not to
 * allow others to use your version of this file under the MPL,
 * indicate your decision by deleting the provisions above and
 * replace them with the notice and other provisions required by
 * the GPL.  If you do not delete the provisions above, a recipient
 * may use your version of this file under either the MPL or the
 * GPL.
 */

/*
 * This file is derived directly from Netscape Communications Corporation,
 * and consists of extensive modifications made during the year(s) 1999-2000.
 */

#ifndef __ST_MD_H__
#define __ST_MD_H__

#if defined(ETIMEDOUT) && !defined(ETIME)
    #define ETIME ETIMEDOUT
#endif

#if defined(MAP_ANONYMOUS) && !defined(MAP_ANON)
    #define MAP_ANON MAP_ANONYMOUS
#endif

#ifndef MAP_FAILED
    #define MAP_FAILED -1
#endif

/* We define the jmpbuf, because the system's is different in different OS */
typedef struct _st_jmp_buf {
    /*
     *   OS         CPU                  SIZE
     * Darwin   __amd64__/__x86_64__    long[8]
     * Darwin   __aarch64__             long[22]
     * Linux    __i386__                long[6]
     * Linux    __amd64__/__x86_64__    long[8]
     * Linux    __aarch64__             long[22]
     * Linux    __arm__                 long[16]
     * Linux    __mips__/__mips64       long[13]
     * Linux    __riscv                 long[14]
     * Linux    __loongarch64           long[12]
     * Cygwin64 __amd64__/__x86_64__    long[8]
     */
    long __jmpbuf[22];
} _st_jmp_buf_t[1];

/* Defined in *.S file and implemented by ASM. */
extern int _st_md_cxt_save(_st_jmp_buf_t env);
extern void _st_md_cxt_restore(_st_jmp_buf_t env, int val);

/* Always use builtin setjmp/longjmp, use asm code. */
#if defined(USE_LIBC_SETJMP)
#error The libc setjmp is not supported now
#endif

/*****************************************
 * Platform specifics
 */

#if defined (DARWIN)

    #define MD_USE_BSD_ANON_MMAP
    #define MD_ACCEPT_NB_INHERITED
    #define MD_HAVE_SOCKLEN_T

    #if defined(__amd64__) || defined(__x86_64__)
        #define MD_GET_SP(_t) *((long *)&((_t)->context[0].__jmpbuf[6]))
    #elif defined(__aarch64__)
        #define MD_GET_SP(_t) *((long *)&((_t)->context[0].__jmpbuf[13]))
    #else
        #error Unknown CPU architecture
    #endif

    #if defined (MD_OSX_NO_CLOCK_GETTIME)
        #define MD_GET_UTIME()                          \
            struct timeval tv;                          \
            (void) gettimeofday(&tv, NULL);             \
            return (tv.tv_sec * 1000000LL + tv.tv_usec)
    #else
        /*
         * https://github.com/ossrs/srs/issues/3978
         * use clock_gettime to get the timestamp in microseconds.
         */
        #define MD_GET_UTIME()                                 \
            struct timespec ts;                                \
            clock_gettime(CLOCK_MONOTONIC, &ts);               \
            return (ts.tv_sec * 1000000LL + ts.tv_nsec / 1000)
    #endif

#elif defined (LINUX)

    /*
     * These are properties of the linux kernel and are the same on every
     * flavor and architecture.
     */
    #define MD_USE_BSD_ANON_MMAP
    #define MD_ACCEPT_NB_NOT_INHERITED
    /*
     * Modern GNU/Linux is Posix.1g compliant.
     */
    #define MD_HAVE_SOCKLEN_T

    /*
     * https://github.com/ossrs/srs/issues/3978
     * use clock_gettime to get the timestamp in microseconds.
     */
    #define MD_GET_UTIME()                                 \
        struct timespec ts;                                \
        clock_gettime(CLOCK_MONOTONIC, &ts);               \
        return (ts.tv_sec * 1000000LL + ts.tv_nsec / 1000)

    #if defined(__i386__)
        #define MD_GET_SP(_t) *((long *)&((_t)->context[0].__jmpbuf[4]))
    #elif defined(__amd64__) || defined(__x86_64__)
        #define MD_GET_SP(_t) *((long *)&((_t)->context[0].__jmpbuf[6]))
    #elif defined(__aarch64__)
        /* https://github.com/ossrs/state-threads/issues/9 */
        #define MD_GET_SP(_t) *((long *)&((_t)->context[0].__jmpbuf[13]))
    #elif defined(__arm__)
        /* https://github.com/ossrs/state-threads/issues/1#issuecomment-244648573 */
        #define MD_GET_SP(_t) *((long *)&((_t)->context[0].__jmpbuf[8]))
    #elif defined(__mips64)
        /* https://github.com/ossrs/state-threads/issues/21 */
        #define MD_GET_SP(_t) *((long *)&((_t)->context[0].__jmpbuf[0]))
    #elif defined(__mips__)
        /* https://github.com/ossrs/state-threads/issues/21 */
        #define MD_GET_SP(_t) *((long *)&((_t)->context[0].__jmpbuf[0]))
    #elif defined(__riscv)
        /* https://github.com/ossrs/state-threads/pull/28 */
        #define MD_GET_SP(_t) *((long *)&((_t)->context[0].__jmpbuf[0]))
    #elif defined(__loongarch64)
        /* https://github.com/ossrs/state-threads/issues/24 */
        #define MD_GET_SP(_t) *((long *)&((_t)->context[0].__jmpbuf[0]))
    #else
        #error "Unknown CPU architecture"
    #endif

#elif defined (CYGWIN64)

    // For CYGWIN64, build SRS on Windows.
    #define MD_USE_BSD_ANON_MMAP
    #define MD_ACCEPT_NB_INHERITED
    #define MD_HAVE_SOCKLEN_T

    #if defined(__amd64__) || defined(__x86_64__)
        #define MD_GET_SP(_t) *((long *)&((_t)->context[0].__jmpbuf[6]))
    #else
        #error Unknown CPU architecture
    #endif

    #define MD_GET_UTIME()            \
        struct timeval tv;              \
        (void) gettimeofday(&tv, NULL); \
        return (tv.tv_sec * 1000000LL + tv.tv_usec)

#else
    #error Unknown OS
#endif /* OS */

#if !defined(MD_HAVE_SOCKLEN_T) && !defined(socklen_t)
    #define socklen_t int
#endif

#ifndef MD_CAP_STACK
    #define MD_CAP_STACK(var_addr)
#endif

#endif /* !__ST_MD_H__ */

