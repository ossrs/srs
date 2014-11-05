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

/*****************************************
 * Platform specifics
 */
#if defined (AIX)
    #define MD_STACK_GROWS_DOWN
    #define MD_USE_SYSV_ANON_MMAP
    #define MD_ACCEPT_NB_INHERITED
    #define MD_ALWAYS_UNSERIALIZED_ACCEPT
    
    #ifndef MD_HAVE_SOCKLEN_T
        #define MD_HAVE_SOCKLEN_T
        #define socklen_t unsigned long
    #endif
    
    #define MD_SETJMP(env) _setjmp(env)
    #define MD_LONGJMP(env, val) _longjmp(env, val)
    
    #define MD_INIT_CONTEXT(_thread, _sp, _main) \
        ST_BEGIN_MACRO                             \
        if (MD_SETJMP((_thread)->context))         \
        _main();                                 \
        (_thread)->context[3] = (long) (_sp);      \
        ST_END_MACRO
    
    #define MD_GET_UTIME()                        \
        timebasestruct_t rt;                        \
        (void) read_real_time(&rt, TIMEBASE_SZ);    \
        (void) time_base_to_time(&rt, TIMEBASE_SZ); \
        return (rt.tb_high * 1000000LL + rt.tb_low / 1000)

#elif defined (CYGWIN)
    #define MD_STACK_GROWS_DOWN
    #define MD_USE_BSD_ANON_MMAP
    #define MD_ACCEPT_NB_NOT_INHERITED
    #define MD_ALWAYS_UNSERIALIZED_ACCEPT
    
    #define MD_SETJMP(env) setjmp(env)
    #define MD_LONGJMP(env, val) longjmp(env, val)
    
    #define MD_JB_SP  7
    
    #define MD_GET_SP(_t) (_t)->context[MD_JB_SP]

    #define MD_INIT_CONTEXT(_thread, _sp, _main) \
        ST_BEGIN_MACRO                             \
        if (MD_SETJMP((_thread)->context))         \
        _main();                                 \
        MD_GET_SP(_thread) = (long) (_sp);         \
        ST_END_MACRO

    #define MD_GET_UTIME()            \
        struct timeval tv;              \
        (void) gettimeofday(&tv, NULL); \
        return (tv.tv_sec * 1000000LL + tv.tv_usec)

#elif defined (DARWIN)
    #define MD_STACK_GROWS_DOWN
    #define MD_USE_BSD_ANON_MMAP
    #define MD_ACCEPT_NB_INHERITED
    #define MD_ALWAYS_UNSERIALIZED_ACCEPT
    #define MD_HAVE_SOCKLEN_T
    
    #define MD_SETJMP(env) _setjmp(env)
    #define MD_LONGJMP(env, val) _longjmp(env, val)
    
    #if defined(__ppc__)
        #define MD_JB_SP  0
    #elif defined(__i386__)
        #define MD_JB_SP  9
    #elif defined(__x86_64__)
        #define MD_JB_SP  4
    #else
        #error Unknown CPU architecture
    #endif

    #define MD_INIT_CONTEXT(_thread, _sp, _main)   \
        ST_BEGIN_MACRO                               \
        if (MD_SETJMP((_thread)->context))           \
        _main();                                   \
        *((long *)&((_thread)->context[MD_JB_SP])) = (long) (_sp); \
        ST_END_MACRO

    #define MD_GET_UTIME()            \
        struct timeval tv;              \
        (void) gettimeofday(&tv, NULL); \
        return (tv.tv_sec * 1000000LL + tv.tv_usec)

#elif defined (FREEBSD)
    #define MD_STACK_GROWS_DOWN
    #define MD_USE_BSD_ANON_MMAP
    #define MD_ACCEPT_NB_INHERITED
    #define MD_ALWAYS_UNSERIALIZED_ACCEPT
    
    #define MD_SETJMP(env) _setjmp(env)
    #define MD_LONGJMP(env, val) _longjmp(env, val)

    #if defined(__i386__)
        #define MD_JB_SP   2
    #elif defined(__alpha__)
        #define MD_JB_SP  34
    #elif defined(__amd64__)
        #define MD_JB_SP   2
    #else
        #error Unknown CPU architecture
    #endif

    #define MD_INIT_CONTEXT(_thread, _sp, _main)          \
        ST_BEGIN_MACRO                                      \
        if (MD_SETJMP((_thread)->context))                  \
        _main();                                          \
        (_thread)->context[0]._jb[MD_JB_SP] = (long) (_sp); \
        ST_END_MACRO

    #define MD_GET_UTIME()            \
        struct timeval tv;              \
        (void) gettimeofday(&tv, NULL); \
        return (tv.tv_sec * 1000000LL + tv.tv_usec)

#elif defined (HPUX)
    #define MD_STACK_GROWS_UP
    #define MD_USE_BSD_ANON_MMAP
    #define MD_ACCEPT_NB_INHERITED
    #define MD_ALWAYS_UNSERIALIZED_ACCEPT
    
    #define MD_SETJMP(env) _setjmp(env)
    #define MD_LONGJMP(env, val) _longjmp(env, val)
    
    #ifndef __LP64__
        /* 32-bit mode (ILP32 data model) */
        #define MD_INIT_CONTEXT(_thread, _sp, _main)        \
            ST_BEGIN_MACRO                                    \
            if (MD_SETJMP((_thread)->context))                \
            _main();                                        \
            ((long *)((_thread)->context))[1] = (long) (_sp); \
            ST_END_MACRO
    #else
        /* 64-bit mode (LP64 data model) */
        #define MD_STACK_PAD_SIZE 256
        /* Last stack frame must be preserved */
        #define MD_INIT_CONTEXT(_thread, _sp, _main)                     \
            ST_BEGIN_MACRO                                                 \
            if (MD_SETJMP((_thread)->context))                             \
            _main();                                                     \
            memcpy((char *)(_sp) - MD_STACK_PAD_SIZE,                      \
                 ((char **)((_thread)->context))[1] - MD_STACK_PAD_SIZE, \
                 MD_STACK_PAD_SIZE);                                     \
            ((long *)((_thread)->context))[1] = (long) (_sp);              \
            ST_END_MACRO
    #endif /* !__LP64__ */

    #define MD_GET_UTIME()            \
        struct timeval tv;              \
        (void) gettimeofday(&tv, NULL); \
        return (tv.tv_sec * 1000000LL + tv.tv_usec)

#elif defined (IRIX)
    #include <sys/syssgi.h>
    
    #define MD_STACK_GROWS_DOWN
    #define MD_USE_SYSV_ANON_MMAP
    #define MD_ACCEPT_NB_INHERITED
    #define MD_ALWAYS_UNSERIALIZED_ACCEPT
    
    #define MD_SETJMP(env) setjmp(env)
    #define MD_LONGJMP(env, val) longjmp(env, val)

    #define MD_INIT_CONTEXT(_thread, _sp, _main) \
        ST_BEGIN_MACRO                             \
        (void) MD_SETJMP((_thread)->context);      \
        (_thread)->context[JB_SP] = (long) (_sp);  \
        (_thread)->context[JB_PC] = (long) _main;  \
        ST_END_MACRO

    #define MD_GET_UTIME()                         \
        static int inited = 0;                       \
        static clockid_t clock_id = CLOCK_SGI_CYCLE; \
        struct timespec ts;                          \
        if (!inited) {                               \
        if (syssgi(SGI_CYCLECNTR_SIZE) < 64)       \
          clock_id = CLOCK_REALTIME;               \
        inited = 1;                                \
        }                                            \
        (void) clock_gettime(clock_id, &ts);         \
        return (ts.tv_sec * 1000000LL + ts.tv_nsec / 1000)
    
    /*
     * Cap the stack by zeroing out the saved return address register
     * value. This allows libexc, used by SpeedShop, to know when to stop
     * backtracing since it won't find main, start, or any other known
     * stack root function in a state thread's stack. Without this libexc
     * traces right off the stack and crashes.
     * The function preamble stores ra at 8(sp), this stores zero there.
     * N.B. This macro is compiler/ABI dependent. It must change if ANY more
     * automatic variables are added to the _st_thread_main() routine, because
     * the address where ra is stored will change.
     */
    #if !defined(__GNUC__) && defined(_MIPS_SIM) && _MIPS_SIM != _ABIO32
        #define MD_CAP_STACK(var_addr) \
            (((volatile __uint64_t *)(var_addr))[1] = 0)
    #endif

#elif defined (LINUX)
    /*
     * These are properties of the linux kernel and are the same on every
     * flavor and architecture.
     */
    #define MD_USE_BSD_ANON_MMAP
    #define MD_ACCEPT_NB_NOT_INHERITED
    #define MD_ALWAYS_UNSERIALIZED_ACCEPT
    /*
     * Modern GNU/Linux is Posix.1g compliant.
     */
    #define MD_HAVE_SOCKLEN_T
    
    /*
     * All architectures and flavors of linux have the gettimeofday
     * function but if you know of a faster way, use it.
     */
    #define MD_GET_UTIME()            \
        struct timeval tv;              \
        (void) gettimeofday(&tv, NULL); \
        return (tv.tv_sec * 1000000LL + tv.tv_usec)

    #if defined(__ia64__)
        #define MD_STACK_GROWS_DOWN
        
        /*
         * IA-64 architecture.  Besides traditional memory call stack, IA-64
         * uses general register stack.  Thus each thread needs a backing store
         * for register stack in addition to memory stack.  Standard
         * setjmp()/longjmp() cannot be used for thread context switching
         * because their implementation implicitly assumes that only one
         * register stack exists.
         */
        #ifdef USE_LIBC_SETJMP
            #undef USE_LIBC_SETJMP
        #endif
        #define MD_USE_BUILTIN_SETJMP
    
        #define MD_STACK_PAD_SIZE 128
        /* Last register stack frame must be preserved */
        #define MD_INIT_CONTEXT(_thread, _sp, _bsp, _main)                       \
            ST_BEGIN_MACRO                                                         \
            if (MD_SETJMP((_thread)->context))                                     \
            _main();                                                             \
            memcpy((char *)(_bsp) - MD_STACK_PAD_SIZE,                             \
                 (char *)(_thread)->context[0].__jmpbuf[17] - MD_STACK_PAD_SIZE, \
                 MD_STACK_PAD_SIZE);                                             \
            (_thread)->context[0].__jmpbuf[0]  = (long) (_sp);                     \
            (_thread)->context[0].__jmpbuf[17] = (long) (_bsp);                    \
            ST_END_MACRO
    #elif defined(__mips__)
        #define MD_STACK_GROWS_DOWN
        
        #define MD_INIT_CONTEXT(_thread, _sp, _main)               \
            ST_BEGIN_MACRO                                           \
            MD_SETJMP((_thread)->context);                           \
            _thread->context[0].__jmpbuf[0].__pc = (__ptr_t) _main;  \
            _thread->context[0].__jmpbuf[0].__sp = _sp;              \
            ST_END_MACRO
    #else /* Not IA-64 or mips */
        /*
         * On linux, there are a few styles of jmpbuf format.  These vary based
         * on architecture/glibc combination.
         *
         * Most of the glibc based toggles were lifted from:
         * mozilla/nsprpub/pr/include/md/_linux.h
         */
        /*
         * Starting with glibc 2.4, JB_SP definitions are not public anymore.
         * They, however, can still be found in glibc source tree in
         * architecture-specific "jmpbuf-offsets.h" files.
         * Most importantly, the content of jmp_buf is mangled by setjmp to make
         * it completely opaque (the mangling can be disabled by setting the
         * LD_POINTER_GUARD environment variable before application execution).
         * Therefore we will use built-in _st_md_cxt_save/_st_md_cxt_restore
         * functions as a setjmp/longjmp replacement wherever they are available
         * unless USE_LIBC_SETJMP is defined.
         */
        #if defined(__powerpc__)
            #define MD_STACK_GROWS_DOWN
        
            #if (__GLIBC__ > 2) || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 1)
                #ifndef JB_GPR1
                    #define JB_GPR1 0
                #endif
                #define MD_GET_SP(_t) (_t)->context[0].__jmpbuf[JB_GPR1]   
            #else
                /* not an error but certainly cause for caution */
                #error "Untested use of old glibc on powerpc"
                #define MD_GET_SP(_t) (_t)->context[0].__jmpbuf[0].__misc[0]
            #endif /* glibc 2.1 or later */
        #elif defined(__alpha)
            #define MD_STACK_GROWS_DOWN
        
            #if defined(__GLIBC__) && __GLIBC__ >= 2
                #ifndef JB_SP
                    #define JB_SP 8
                #endif
                #define MD_GET_SP(_t) (_t)->context[0].__jmpbuf[JB_SP]
            #else
                /* not an error but certainly cause for caution */
                #error "Untested use of old glibc on alpha"
                #define MD_GET_SP(_t) (_t)->context[0].__jmpbuf[0].__sp
            #endif
        #elif defined(__mc68000__)
            #define MD_STACK_GROWS_DOWN
            
            /* m68k still uses old style sigjmp_buf */
            #define MD_GET_SP(_t) (_t)->context[0].__jmpbuf[0].__sp
        #elif defined(__sparc__)
            #define MD_STACK_GROWS_DOWN
        
            #if defined(__GLIBC__) && __GLIBC__ >= 2
                #ifndef JB_SP
                    #define JB_SP 0
                #endif
                #define MD_GET_SP(_t) (_t)->context[0].__jmpbuf[JB_SP]
            #else
                /* not an error but certainly cause for caution */
                #error "Untested use of old glic on sparc -- also using odd mozilla derived __fp"
                #define MD_GET_SP(_t) (_t)->context[0].__jmpbuf[0].__fp
            #endif
        #elif defined(__i386__)
            #define MD_STACK_GROWS_DOWN
            #define MD_USE_BUILTIN_SETJMP
            
            #if defined(__GLIBC__) && __GLIBC__ >= 2
                #ifndef JB_SP
                    #define JB_SP 4
                #endif
                #define MD_GET_SP(_t) (_t)->context[0].__jmpbuf[JB_SP]
            #else
                /* not an error but certainly cause for caution */
                #error "Untested use of old glibc on i386"
                #define MD_GET_SP(_t) (_t)->context[0].__jmpbuf[0].__sp
            #endif
        #elif defined(__amd64__) || defined(__x86_64__)
            #define MD_STACK_GROWS_DOWN
            #define MD_USE_BUILTIN_SETJMP
            
            #ifndef JB_RSP
                #define JB_RSP 6
            #endif
            #define MD_GET_SP(_t) (_t)->context[0].__jmpbuf[JB_RSP]
        #elif defined(__arm__)
            #define MD_STACK_GROWS_DOWN
            
            #if defined(__GLIBC__) && __GLIBC__ >= 2
                #define MD_GET_SP(_t) (_t)->context[0].__jmpbuf[8]
            #else
                #error "ARM/Linux pre-glibc2 not supported yet"
            #endif /* defined(__GLIBC__) && __GLIBC__ >= 2 */
        #elif defined(__s390__)
            #define MD_STACK_GROWS_DOWN
            
            /* There is no JB_SP in glibc at this time. (glibc 2.2.5)
             */
            #define MD_GET_SP(_t) (_t)->context[0].__jmpbuf[0].__gregs[9]
        #elif defined(__hppa__)
            #define MD_STACK_GROWS_UP
            
            /* yes, this is gross, unfortunately at the moment (2002/08/01) there is
             * a bug in hppa's glibc header definition for JB_SP, so we can't
             * use that...
             */
            #define MD_GET_SP(_t) (*(long *)(((char *)&(_t)->context[0].__jmpbuf[0]) + 76))
        #else
            #error "Unknown CPU architecture"
        #endif /* Cases with common MD_INIT_CONTEXT and different SP locations */
        
        #define MD_INIT_CONTEXT(_thread, _sp, _main) \
            ST_BEGIN_MACRO                             \
            if (MD_SETJMP((_thread)->context))         \
            _main();                                 \
            MD_GET_SP(_thread) = (long) (_sp);         \
            ST_END_MACRO
    #endif /* Cases with different MD_INIT_CONTEXT */
    
    #if defined(MD_USE_BUILTIN_SETJMP) && !defined(USE_LIBC_SETJMP)
        #define MD_SETJMP(env) _st_md_cxt_save(env)
        #define MD_LONGJMP(env, val) _st_md_cxt_restore(env, val)
        
        extern int _st_md_cxt_save(jmp_buf env);
        extern void _st_md_cxt_restore(jmp_buf env, int val);
    #else
        #define MD_SETJMP(env) setjmp(env)
        #define MD_LONGJMP(env, val) longjmp(env, val)
    #endif

#elif defined (NETBSD)
    #define MD_STACK_GROWS_DOWN
    #define MD_USE_BSD_ANON_MMAP
    #define MD_ACCEPT_NB_INHERITED
    #define MD_ALWAYS_UNSERIALIZED_ACCEPT
    #define MD_HAVE_SOCKLEN_T
    
    #define MD_SETJMP(env) _setjmp(env)
    #define MD_LONGJMP(env, val) _longjmp(env, val)
    
    #if defined(__i386__)
        #define MD_JB_SP   2
    #elif defined(__alpha__)
        #define MD_JB_SP  34
    #elif defined(__sparc__)
        #define MD_JB_SP   0
    #elif defined(__vax__)
        #define MD_JB_SP   2
    #else
        #error Unknown CPU architecture
    #endif
    
    #define MD_INIT_CONTEXT(_thread, _sp, _main)   \
        ST_BEGIN_MACRO                               \
        if (MD_SETJMP((_thread)->context))           \
        _main();                                   \
        (_thread)->context[MD_JB_SP] = (long) (_sp); \
        ST_END_MACRO
    
    #define MD_GET_UTIME()            \
        struct timeval tv;              \
        (void) gettimeofday(&tv, NULL); \
        return (tv.tv_sec * 1000000LL + tv.tv_usec)

#elif defined (OPENBSD)
    #define MD_STACK_GROWS_DOWN
    #define MD_USE_BSD_ANON_MMAP
    #define MD_ACCEPT_NB_INHERITED
    #define MD_ALWAYS_UNSERIALIZED_ACCEPT
    
    #define MD_SETJMP(env) _setjmp(env)
    #define MD_LONGJMP(env, val) _longjmp(env, val)
    
    #if defined(__i386__)
        #define MD_JB_SP   2
    #elif defined(__alpha__)
        #define MD_JB_SP  34
    #elif defined(__sparc__)
        #define MD_JB_SP   0
    #elif defined(__amd64__)
        #define MD_JB_SP   6
    #else
        #error Unknown CPU architecture
    #endif
    
    #define MD_INIT_CONTEXT(_thread, _sp, _main)   \
        ST_BEGIN_MACRO                               \
        if (MD_SETJMP((_thread)->context))           \
        _main();                                   \
        (_thread)->context[MD_JB_SP] = (long) (_sp); \
        ST_END_MACRO
    
    #define MD_GET_UTIME()            \
        struct timeval tv;              \
        (void) gettimeofday(&tv, NULL); \
        return (tv.tv_sec * 1000000LL + tv.tv_usec)

#elif defined (OSF1)
    #include <signal.h>
    
    #define MD_STACK_GROWS_DOWN
    #define MD_USE_SYSV_ANON_MMAP
    #define MD_ACCEPT_NB_NOT_INHERITED
    #define MD_ALWAYS_UNSERIALIZED_ACCEPT
    
    #define MD_SETJMP(env) _setjmp(env)
    #define MD_LONGJMP(env, val) _longjmp(env, val)
    
    #define MD_INIT_CONTEXT(_thread, _sp, _main)                         \
        ST_BEGIN_MACRO                                                     \
        if (MD_SETJMP((_thread)->context))                                 \
        _main();                                                         \
        ((struct sigcontext *)((_thread)->context))->sc_sp = (long) (_sp); \
        ST_END_MACRO
    
    #define MD_GET_UTIME()            \
        struct timeval tv;              \
        (void) gettimeofday(&tv, NULL); \
        return (tv.tv_sec * 1000000LL + tv.tv_usec)

#elif defined (SOLARIS)
    #include <sys/filio.h>
    extern int getpagesize(void);
    
    #define MD_STACK_GROWS_DOWN
    #define MD_USE_SYSV_ANON_MMAP
    #define MD_ACCEPT_NB_NOT_INHERITED
    
    #define MD_SETJMP(env) setjmp(env)
    #define MD_LONGJMP(env, val) longjmp(env, val)
    
    #if defined(sparc) || defined(__sparc)
        #ifdef _LP64
            #define MD_STACK_PAD_SIZE 4095
        #endif
        #define MD_INIT_CONTEXT(_thread, _sp, _main) \
            ST_BEGIN_MACRO                             \
            (void) MD_SETJMP((_thread)->context);      \
            (_thread)->context[1] = (long) (_sp);      \
            (_thread)->context[2] = (long) _main;      \
            ST_END_MACRO
    #elif defined(i386) || defined(__i386)
        #define MD_INIT_CONTEXT(_thread, _sp, _main) \
            ST_BEGIN_MACRO                             \
            (void) MD_SETJMP((_thread)->context);      \
            (_thread)->context[4] = (long) (_sp);      \
            (_thread)->context[5] = (long) _main;      \
            ST_END_MACRO
    #elif defined(__amd64__)
        #define MD_INIT_CONTEXT(_thread, _sp, _main)   \
            ST_BEGIN_MACRO                               \
            if (MD_SETJMP((_thread)->context))           \
            _main();                                   \
            (_thread)->context[6] = (long) (_sp); \
            ST_END_MACRO
    #else
        #error Unknown CPU architecture
    #endif
    
    #define MD_GET_UTIME() \
        return (gethrtime() / 1000)

#else
    #error Unknown OS
#endif /* OS */

/*****************************************
 * Other defines
 */
#if !defined(MD_HAVE_POLL) && !defined(MD_DONT_HAVE_POLL)
    #define MD_HAVE_POLL
#endif

#ifndef MD_STACK_PAD_SIZE
    #define MD_STACK_PAD_SIZE 128
#endif

#if !defined(MD_HAVE_SOCKLEN_T) && !defined(socklen_t)
    #define socklen_t int
#endif

#ifndef MD_CAP_STACK
    #define MD_CAP_STACK(var_addr)
#endif

#endif /* !__ST_MD_H__ */

