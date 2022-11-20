//
// libuuid BSD License @see https://sourceforge.net/projects/libuuid/
//
// SPDX-License-Identifier: BSD-3-Clause
//

#include <srs_app_uuid.hpp>

#if defined(SRS_CYGWIN64)
#define HAVE_LOFF_T
#endif

#include <unistd.h>
#include <stdio.h>
#include <sys/file.h>
#define HAVE_USLEEP
////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////

/*
 * Fundamental C definitions.
 */

#ifndef UTIL_LINUX_C_H
#define UTIL_LINUX_C_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <limits.h>
#include <stddef.h>

#ifdef HAVE_STDINT_H
#include <stdint.h>
#else
# ifdef HAVE_INTTYPES_H
# include <inttypes.h>
# endif
#endif

#include <stdio.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#ifdef HAVE_ERR_H
# include <err.h>
#endif

#ifndef HAVE_USLEEP
# include <time.h>
#endif

/*
 * Compiler specific stuff
 */
#ifndef __GNUC_PREREQ
# if defined __GNUC__ && defined __GNUC_MINOR__
#  define __GNUC_PREREQ(maj, min) \
	((__GNUC__ << 16) + __GNUC_MINOR__ >= ((maj) << 16) + (min))
# else
#  define __GNUC_PREREQ(maj, min) 0
# endif
#endif

#ifdef __GNUC__

/* &a[0] degrades to a pointer: a different type from an array */
# define __must_be_array(a) \
	UL_BUILD_BUG_ON_ZERO(__builtin_types_compatible_p(__typeof__(a), __typeof__(&a[0])))

# define ignore_result(x) ({ \
	__typeof__(x) __dummy __attribute__((__unused__)) = (x); (void) __dummy; \
})

#else /* !__GNUC__ */
# define __must_be_array(a)	0
# define __attribute__(_arg_)
# define ignore_result(x) ((void) (x))
#endif /* !__GNUC__ */

/*
 * Function attributes
 */
#ifndef __ul_alloc_size
# if __GNUC_PREREQ (4, 3)
#  define __ul_alloc_size(s) __attribute__((alloc_size(s)))
# else
#  define __ul_alloc_size(s)
# endif
#endif

#ifndef __ul_calloc_size
# if __GNUC_PREREQ (4, 3)
#  define __ul_calloc_size(n, s) __attribute__((alloc_size(n, s)))
# else
#  define __ul_calloc_size(n, s)
# endif
#endif

/* Force a compilation error if condition is true, but also produce a
 * result (of value 0 and type size_t), so the expression can be used
 * e.g. in a structure initializer (or where-ever else comma expressions
 * aren't permitted).
 */
#define UL_BUILD_BUG_ON_ZERO(e) (sizeof(struct { int:-!!(e); }))
#define BUILD_BUG_ON_NULL(e) ((void *)sizeof(struct { int:-!!(e); }))

#ifndef ARRAY_SIZE
# define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]) + __must_be_array(arr))
#endif

#ifndef PATH_MAX
# define PATH_MAX 4096
#endif

#ifndef TRUE
# define TRUE 1
#endif

#ifndef FALSE
# define FALSE 0
#endif

#ifndef min
# define min(x, y) ({				\
	__typeof__(x) _min1 = (x);		\
	__typeof__(y) _min2 = (y);		\
	(void) (&_min1 == &_min2);		\
	_min1 < _min2 ? _min1 : _min2; })
#endif

#ifndef max
# define max(x, y) ({				\
	__typeof__(x) _max1 = (x);		\
	__typeof__(y) _max2 = (y);		\
	(void) (&_max1 == &_max2);		\
	_max1 > _max2 ? _max1 : _max2; })
#endif

#ifndef offsetof
#define offsetof(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)
#endif

#ifndef container_of
#define container_of(ptr, type, member) ({                       \
	const __typeof__( ((type *)0)->member ) *__mptr = (ptr); \
	(type *)( (char *)__mptr - offsetof(type,member) );})
#endif

#if 0
#ifndef HAVE_PROGRAM_INVOCATION_SHORT_NAME
# ifdef HAVE___PROGNAME
extern char *__progname;
#  define program_invocation_short_name __progname
# else
#  ifdef HAVE_GETEXECNAME
#   define program_invocation_short_name \
		prog_inv_sh_nm_from_file(getexecname(), 0)
#  else
#   define program_invocation_short_name \
		prog_inv_sh_nm_from_file((char*)__FILE__, 1)
#  endif
static char prog_inv_sh_nm_buf[256];
static inline char *
prog_inv_sh_nm_from_file(char *f, char stripext)
{
    char *t;

    if ((t = strrchr(f, '/')) != NULL)
        t++;
    else
        t = f;

    strncpy(prog_inv_sh_nm_buf, t, sizeof(prog_inv_sh_nm_buf) - 1);
    prog_inv_sh_nm_buf[sizeof(prog_inv_sh_nm_buf) - 1] = '\0';

    if (stripext && (t = strrchr(prog_inv_sh_nm_buf, '.')) != NULL)
        *t = '\0';

    return prog_inv_sh_nm_buf;
}
# endif
#endif
#endif

#ifndef HAVE_ERR_H
#if 0
static inline void
errmsg(char doexit, int excode, char adderr, const char *fmt, ...)
{
    fprintf(stderr, "%s: ", program_invocation_short_name);
    if (fmt != NULL) {
        va_list argp;
        va_start(argp, fmt);
        vfprintf(stderr, fmt, argp);
        va_end(argp);
        if (adderr)
            fprintf(stderr, ": ");
    }
    if (adderr)
        fprintf(stderr, "%m");
    fprintf(stderr, "\n");
    if (doexit)
        exit(excode);
}
#endif

#ifndef HAVE_ERR
# define err(E, FMT...) errmsg(1, E, 1, FMT)
#endif

#ifndef HAVE_ERRX
# define errx(E, FMT...) errmsg(1, E, 0, FMT)
#endif

#ifndef HAVE_WARN
# define warn(FMT...) errmsg(0, 0, 1, FMT)
#endif

#ifndef HAVE_WARNX
# define warnx(FMT...) errmsg(0, 0, 0, FMT)
#endif
#endif /* !HAVE_ERR_H */


#if 0
static inline __attribute__((const)) int is_power_of_2(unsigned long num)
{
    return (num != 0 && ((num & (num - 1)) == 0));
}
#endif

#ifndef HAVE_LOFF_T
typedef int64_t loff_t;
#endif

#if !defined(HAVE_DIRFD) && (!defined(HAVE_DECL_DIRFD) || HAVE_DECL_DIRFD == 0) && defined(HAVE_DIR_DD_FD)
#include <sys/types.h>
#include <dirent.h>
static inline int dirfd(DIR *d)
{
	return d->dd_fd;
}
#endif

/*
 * Fallback defines for old versions of glibc
 */
#include <fcntl.h>

#ifdef O_CLOEXEC
#define UL_CLOEXECSTR	"e"
#else
#define UL_CLOEXECSTR	""
#endif

#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif


#ifndef AI_ADDRCONFIG
#define AI_ADDRCONFIG 0x0020
#endif

#ifndef IUTF8
#define IUTF8 0040000
#endif

#if 0
/*
 * MAXHOSTNAMELEN replacement
 */
static inline size_t get_hostname_max(void)
{
#if HAVE_DECL__SC_HOST_NAME_MAX
    long len = sysconf(_SC_HOST_NAME_MAX);

	if (0 < len)
		return len;
#endif

#ifdef MAXHOSTNAMELEN
    return MAXHOSTNAMELEN;
#elif HOST_NAME_MAX
    return HOST_NAME_MAX;
#endif
    return 64;
}
#endif

#ifndef HAVE_USLEEP
/*
 * This function is marked obsolete in POSIX.1-2001 and removed in
 * POSIX.1-2008. It is replaced with nanosleep().
 */
static inline int usleep(useconds_t usec)
{
    struct timespec waittime = {
            .tv_sec   =  usec / 1000000L,
            .tv_nsec  = (usec % 1000000L) * 1000
    };
    return nanosleep(&waittime, NULL);
}
#endif

/*
 * Constant strings for usage() functions. For more info see
 * Documentation/howto-usage-function.txt and disk-utils/delpart.c
 */
#define USAGE_HEADER     _("\nUsage:\n")
#define USAGE_OPTIONS    _("\nOptions:\n")
#define USAGE_SEPARATOR  _("\n")
#define USAGE_HELP       _(" -h, --help     display this help and exit\n")
#define USAGE_VERSION    _(" -V, --version  output version information and exit\n")
#define USAGE_MAN_TAIL(_man)   _("\nFor more details see %s.\n"), _man

#define UTIL_LINUX_VERSION _("%s from %s\n"), program_invocation_short_name, PACKAGE_STRING

/*
 * scanf modifiers for "strings allocation"
 */
#ifdef HAVE_SCANF_MS_MODIFIER
#define UL_SCNsA	"%ms"
#elif defined(HAVE_SCANF_AS_MODIFIER)
#define UL_SCNsA	"%as"
#endif

/*
 * seek stuff
 */
#ifndef SEEK_DATA
# define SEEK_DATA	3
#endif
#ifndef SEEK_HOLE
# define SEEK_HOLE	4
#endif

#endif /* UTIL_LINUX_C_H */

/*
 * Definitions used by the uuidd daemon
 *
 * Copyright (C) 2007 Theodore Ts'o.
 *
 * %Begin-Header%
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, and the entire permission notice in its entirety,
 *    including the disclaimer of warranties.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote
 *    products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE, ALL OF
 * WHICH ARE HEREBY DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OF THIS SOFTWARE, EVEN IF NOT ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 * %End-Header%
 */

#ifndef _UUID_UUIDD_H
#define _UUID_UUIDD_H

#define UUIDD_DIR		_PATH_LOCALSTATEDIR "/uuidd"
#define UUIDD_SOCKET_PATH	UUIDD_DIR "/request"
#define UUIDD_PIDFILE_PATH	UUIDD_DIR "/uuidd.pid"
#define UUIDD_PATH		"/usr/sbin/uuidd"

#define UUIDD_OP_GETPID			0
#define UUIDD_OP_GET_MAXOP		1
#define UUIDD_OP_TIME_UUID		2
#define UUIDD_OP_RANDOM_UUID		3
#define UUIDD_OP_BULK_TIME_UUID		4
#define UUIDD_OP_BULK_RANDOM_UUID	5
#define UUIDD_MAX_OP			UUIDD_OP_BULK_RANDOM_UUID

extern int __uuid_generate_time(uuid_t out, int *num);
extern void __uuid_generate_random(uuid_t out, int *num);

#endif /* _UUID_UUID_H */

#ifndef UTIL_LINUX_RANDUTILS
#define UTIL_LINUX_RANDUTILS

#ifdef HAVE_SRANDOM
#define srand(x)	srandom(x)
#define rand()		random()
#endif

extern int random_get_fd(void);
extern void random_get_bytes(void *buf, size_t nbytes);

#endif

/*
 * uuid.h -- private header file for uuids
 *
 * Copyright (C) 1996, 1997 Theodore Ts'o.
 *
 * %Begin-Header%
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, and the entire permission notice in its entirety,
 *    including the disclaimer of warranties.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote
 *    products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE, ALL OF
 * WHICH ARE HEREBY DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OF THIS SOFTWARE, EVEN IF NOT ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 * %End-Header%
 */

#include <inttypes.h>
#include <sys/types.h>

//#include "uuid.h"

#define LIBUUID_CLOCK_FILE	"/var/lib/libuuid/clock.txt"

/*
 * Offset between 15-Oct-1582 and 1-Jan-70
 */
#define TIME_OFFSET_HIGH 0x01B21DD2
#define TIME_OFFSET_LOW  0x13814000

struct uuid {
    uint32_t	time_low;
    uint16_t	time_mid;
    uint16_t	time_hi_and_version;
    uint16_t	clock_seq;
    uint8_t	node[6];
};


/*
 * prototypes
 */
void uuid_pack(const struct uuid *uu, uuid_t ptr);
void uuid_unpack(const uuid_t in, struct uuid *uu);

/*
 * gen_uuid.c --- generate a DCE-compatible uuid
 *
 * Copyright (C) 1996, 1997, 1998, 1999 Theodore Ts'o.
 *
 * %Begin-Header%
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, and the entire permission notice in its entirety,
 *    including the disclaimer of warranties.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote
 *    products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE, ALL OF
 * WHICH ARE HEREBY DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OF THIS SOFTWARE, EVEN IF NOT ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 * %End-Header%
 */

/*
 * Force inclusion of SVID stuff since we need it if we're compiling in
 * gcc-wall wall mode
 */
#ifndef _SVID_SOURCE
#define _SVID_SOURCE
#endif

#ifdef _WIN32
#define _WIN32_WINNT 0x0500
#include <windows.h>
#define UUID MYUUID
#endif
#include <stdio.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include <sys/types.h>
#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif
#include <sys/stat.h>
#ifdef HAVE_SYS_FILE_H
#include <sys/file.h>
#endif
#ifdef HAVE_SYS_IOCTL_H
#include <sys/ioctl.h>
#endif
#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
#ifdef HAVE_SYS_UN_H
#include <sys/un.h>
#endif
#ifdef HAVE_SYS_SOCKIO_H
#include <sys/sockio.h>
#endif
#ifdef HAVE_NET_IF_H
#include <net/if.h>
#endif
#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif
#ifdef HAVE_NET_IF_DL_H
#include <net/if_dl.h>
#endif
#if defined(__linux__) && defined(HAVE_SYS_SYSCALL_H)
#include <sys/syscall.h>
#endif

//#include "all-io.h"
//#include "uuidP.h"
//#include "uuidd.h"
//#include "randutils.h"
//#include "c.h"

#ifdef HAVE_TLS
#define THREAD_LOCAL static __thread
#else
#define THREAD_LOCAL static
#endif

#ifndef LOCK_EX
/* flock() replacement */
#define LOCK_EX 1
#define LOCK_SH 2
#define LOCK_UN 3
#define LOCK_NB 4

static int flock(int fd, int op)
{
    int rc = 0;

#if defined(F_SETLK) && defined(F_SETLKW)
    struct flock fl = {0};

    switch (op & (LOCK_EX|LOCK_SH|LOCK_UN)) {
    case LOCK_EX:
        fl.l_type = F_WRLCK;
        break;

    case LOCK_SH:
        fl.l_type = F_RDLCK;
        break;

    case LOCK_UN:
        fl.l_type = F_UNLCK;
        break;

    default:
        errno = EINVAL;
        return -1;
    }

    fl.l_whence = SEEK_SET;
    rc = fcntl (fd, op & LOCK_NB ? F_SETLK : F_SETLKW, &fl);

    if (rc && (errno == EAGAIN))
        errno = EWOULDBLOCK;
#endif /* defined(F_SETLK) && defined(F_SETLKW)  */

    return rc;
}

#endif /* LOCK_EX */

#ifdef _WIN32
static void gettimeofday (struct timeval *tv, void *dummy)
{
	FILETIME	ftime;
	uint64_t	n;

	GetSystemTimeAsFileTime (&ftime);
	n = (((uint64_t) ftime.dwHighDateTime << 32)
	     + (uint64_t) ftime.dwLowDateTime);
	if (n) {
		n /= 10;
		n -= ((369 * 365 + 89) * (uint64_t) 86400) * 1000000;
	}

	tv->tv_sec = n / 1000000;
	tv->tv_usec = n % 1000000;
}

static int getuid (void)
{
	return 1;
}
#endif

/*
 * Get the ethernet hardware address, if we can find it...
 *
 * XXX for a windows version, probably should use GetAdaptersInfo:
 * http://www.codeguru.com/cpp/i-n/network/networkinformation/article.php/c5451
 * commenting out get_node_id just to get gen_uuid to compile under windows
 * is not the right way to go!
 */
static int get_node_id(unsigned char *node_id)
{
#ifdef HAVE_NET_IF_H
    int		sd;
	struct ifreq	ifr, *ifrp;
	struct ifconf	ifc;
	char buf[1024];
	int		n, i;
	unsigned char	*a;
#ifdef HAVE_NET_IF_DL_H
	struct sockaddr_dl *sdlp;
#endif

/*
 * BSD 4.4 defines the size of an ifreq to be
 * max(sizeof(ifreq), sizeof(ifreq.ifr_name)+ifreq.ifr_addr.sa_len
 * However, under earlier systems, sa_len isn't present, so the size is
 * just sizeof(struct ifreq)
 */
#ifdef HAVE_SA_LEN
#define ifreq_size(i) max(sizeof(struct ifreq),\
     sizeof((i).ifr_name)+(i).ifr_addr.sa_len)
#else
#define ifreq_size(i) sizeof(struct ifreq)
#endif /* HAVE_SA_LEN */

	sd = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
	if (sd < 0) {
		return -1;
	}
	memset(buf, 0, sizeof(buf));
	ifc.ifc_len = sizeof(buf);
	ifc.ifc_buf = buf;
	if (ioctl (sd, SIOCGIFCONF, (char *)&ifc) < 0) {
		close(sd);
		return -1;
	}
	n = ifc.ifc_len;
	for (i = 0; i < n; i+= ifreq_size(*ifrp) ) {
		ifrp = (struct ifreq *)((char *) ifc.ifc_buf+i);
		strncpy(ifr.ifr_name, ifrp->ifr_name, IFNAMSIZ);
#ifdef SIOCGIFHWADDR
		if (ioctl(sd, SIOCGIFHWADDR, &ifr) < 0)
			continue;
		a = (unsigned char *) &ifr.ifr_hwaddr.sa_data;
#else
#ifdef SIOCGENADDR
		if (ioctl(sd, SIOCGENADDR, &ifr) < 0)
			continue;
		a = (unsigned char *) ifr.ifr_enaddr;
#else
#ifdef HAVE_NET_IF_DL_H
		sdlp = (struct sockaddr_dl *) &ifrp->ifr_addr;
		if ((sdlp->sdl_family != AF_LINK) || (sdlp->sdl_alen != 6))
			continue;
		a = (unsigned char *) &sdlp->sdl_data[sdlp->sdl_nlen];
#else
		/*
		 * XXX we don't have a way of getting the hardware
		 * address
		 */
		close(sd);
		return 0;
#endif /* HAVE_NET_IF_DL_H */
#endif /* SIOCGENADDR */
#endif /* SIOCGIFHWADDR */
		if (!a[0] && !a[1] && !a[2] && !a[3] && !a[4] && !a[5])
			continue;
		if (node_id) {
			memcpy(node_id, a, 6);
			close(sd);
			return 1;
		}
	}
	close(sd);
#endif
    return 0;
}

/* Assume that the gettimeofday() has microsecond granularity */
#define MAX_ADJUSTMENT 10

/*
 * Get clock from global sequence clock counter.
 *
 * Return -1 if the clock counter could not be opened/locked (in this case
 * pseudorandom value is returned in @ret_clock_seq), otherwise return 0.
 */
static int get_clock(uint32_t *clock_high, uint32_t *clock_low,
                     uint16_t *ret_clock_seq, int *num)
{
    THREAD_LOCAL int		adjustment = 0;
    THREAD_LOCAL struct timeval	last = {0, 0};
    THREAD_LOCAL int		state_fd = -2;
    THREAD_LOCAL FILE		*state_f;
    THREAD_LOCAL uint16_t		clock_seq;
    struct timeval			tv;
    uint64_t			clock_reg;
    mode_t				save_umask;
    int				len;
    int				ret = 0;

    if (state_fd == -2) {
        save_umask = umask(0);
        state_fd = open(LIBUUID_CLOCK_FILE, O_RDWR|O_CREAT|O_CLOEXEC, 0660);
        (void) umask(save_umask);
        if (state_fd != -1) {
            state_f = fdopen(state_fd, "r+" UL_CLOEXECSTR);
            if (!state_f) {
                close(state_fd);
                state_fd = -1;
                ret = -1;
            }
        }
        else
            ret = -1;
    }
    if (state_fd >= 0) {
        rewind(state_f);
        while (flock(state_fd, LOCK_EX) < 0) {
            if ((errno == EAGAIN) || (errno == EINTR))
                continue;
            fclose(state_f);
            close(state_fd);
            state_fd = -1;
            ret = -1;
            break;
        }
    }
    if (state_fd >= 0) {
        unsigned int cl;
        unsigned long tv1, tv2;
        int a;

        if (fscanf(state_f, "clock: %04x tv: %lu %lu adj: %d\n",
                   &cl, &tv1, &tv2, &a) == 4) {
            clock_seq = cl & 0x3FFF;
            last.tv_sec = tv1;
            last.tv_usec = tv2;
            adjustment = a;
        }
    }

    if ((last.tv_sec == 0) && (last.tv_usec == 0)) {
        random_get_bytes(&clock_seq, sizeof(clock_seq));
        clock_seq &= 0x3FFF;
        gettimeofday(&last, 0);
        last.tv_sec--;
    }

    try_again:
    gettimeofday(&tv, 0);
    if ((tv.tv_sec < last.tv_sec) ||
        ((tv.tv_sec == last.tv_sec) &&
         (tv.tv_usec < last.tv_usec))) {
        clock_seq = (clock_seq+1) & 0x3FFF;
        adjustment = 0;
        last = tv;
    } else if ((tv.tv_sec == last.tv_sec) &&
               (tv.tv_usec == last.tv_usec)) {
        if (adjustment >= MAX_ADJUSTMENT)
            goto try_again;
        adjustment++;
    } else {
        adjustment = 0;
        last = tv;
    }

    clock_reg = tv.tv_usec*10 + adjustment;
    clock_reg += ((uint64_t) tv.tv_sec)*10000000;
    clock_reg += (((uint64_t) 0x01B21DD2) << 32) + 0x13814000;

    if (num && (*num > 1)) {
        adjustment += *num - 1;
        last.tv_usec += adjustment / 10;
        adjustment = adjustment % 10;
        last.tv_sec += last.tv_usec / 1000000;
        last.tv_usec = last.tv_usec % 1000000;
    }

    if (state_fd >= 0) {
        rewind(state_f);
        len = fprintf(state_f,
                      "clock: %04x tv: %016lu %08lu adj: %08d\n",
                      clock_seq, last.tv_sec, (unsigned long)last.tv_usec, adjustment);
        fflush(state_f);
        if (ftruncate(state_fd, len) < 0) {
            fprintf(state_f, "                   \n");
            fflush(state_f);
        }
        rewind(state_f);
        flock(state_fd, LOCK_UN);
    }

    *clock_high = clock_reg >> 32;
    *clock_low = clock_reg;
    *ret_clock_seq = clock_seq;
    return ret;
}

#if defined(HAVE_UUIDD) && defined(HAVE_SYS_UN_H)
/*
 * Try using the uuidd daemon to generate the UUID
 *
 * Returns 0 on success, non-zero on failure.
 */
static int get_uuid_via_daemon(int op, uuid_t out, int *num)
{
	char op_buf[64];
	int op_len;
	int s;
	ssize_t ret;
	int32_t reply_len = 0, expected = 16;
	struct sockaddr_un srv_addr;

	if ((s = socket(AF_UNIX, SOCK_STREAM, 0)) < 0)
		return -1;

	srv_addr.sun_family = AF_UNIX;
	strcpy(srv_addr.sun_path, UUIDD_SOCKET_PATH);

	if (connect(s, (const struct sockaddr *) &srv_addr,
		    sizeof(struct sockaddr_un)) < 0)
		goto fail;

	op_buf[0] = op;
	op_len = 1;
	if (op == UUIDD_OP_BULK_TIME_UUID) {
		memcpy(op_buf+1, num, sizeof(*num));
		op_len += sizeof(*num);
		expected += sizeof(*num);
	}

	ret = write(s, op_buf, op_len);
	if (ret < 1)
		goto fail;

	ret = read_all(s, (char *) &reply_len, sizeof(reply_len));
	if (ret < 0)
		goto fail;

	if (reply_len != expected)
		goto fail;

	ret = read_all(s, op_buf, reply_len);

	if (op == UUIDD_OP_BULK_TIME_UUID)
		memcpy(op_buf+16, num, sizeof(int));

	memcpy(out, op_buf, 16);

	close(s);
	return ((ret == expected) ? 0 : -1);

fail:
	close(s);
	return -1;
}

#else /* !defined(HAVE_UUIDD) && defined(HAVE_SYS_UN_H) */
static int get_uuid_via_daemon(int op, uuid_t out, int *num)
{
    return -1;
}
#endif

int __uuid_generate_time(uuid_t out, int *num)
{
    static unsigned char node_id[6];
    static int has_init = 0;
    struct uuid uu;
    uint32_t	clock_mid;
    int ret;

    if (!has_init) {
        if (get_node_id(node_id) <= 0) {
            random_get_bytes(node_id, 6);
            /*
             * Set multicast bit, to prevent conflicts
             * with IEEE 802 addresses obtained from
             * network cards
             */
            node_id[0] |= 0x01;
        }
        has_init = 1;
    }
    ret = get_clock(&clock_mid, &uu.time_low, &uu.clock_seq, num);
    uu.clock_seq |= 0x8000;
    uu.time_mid = (uint16_t) clock_mid;
    uu.time_hi_and_version = ((clock_mid >> 16) & 0x0FFF) | 0x1000;
    memcpy(uu.node, node_id, 6);
    uuid_pack(&uu, out);
    return ret;
}

/*
 * Generate time-based UUID and store it to @out
 *
 * Tries to guarantee uniqueness of the generated UUIDs by obtaining them from the uuidd daemon,
 * or, if uuidd is not usable, by using the global clock state counter (see get_clock()).
 * If neither of these is possible (e.g. because of insufficient permissions), it generates
 * the UUID anyway, but returns -1. Otherwise, returns 0.
 */
static int uuid_generate_time_generic(uuid_t out) {
#ifdef HAVE_TLS
    THREAD_LOCAL int		num = 0;
	THREAD_LOCAL struct uuid	uu;
	THREAD_LOCAL time_t		last_time = 0;
	time_t				now;

	if (num > 0) {
		now = time(0);
		if (now > last_time+1)
			num = 0;
	}
	if (num <= 0) {
		num = 1000;
		if (get_uuid_via_daemon(UUIDD_OP_BULK_TIME_UUID,
					out, &num) == 0) {
			last_time = time(0);
			uuid_unpack(out, &uu);
			num--;
			return 0;
		}
		num = 0;
	}
	if (num > 0) {
		uu.time_low++;
		if (uu.time_low == 0) {
			uu.time_mid++;
			if (uu.time_mid == 0)
				uu.time_hi_and_version++;
		}
		num--;
		uuid_pack(&uu, out);
		return 0;
	}
#else
    if (get_uuid_via_daemon(UUIDD_OP_TIME_UUID, out, 0) == 0)
        return 0;
#endif

    return __uuid_generate_time(out, 0);
}

/*
 * Generate time-based UUID and store it to @out.
 *
 * Discards return value from uuid_generate_time_generic()
 */
void uuid_generate_time(uuid_t out)
{
    (void)uuid_generate_time_generic(out);
}


int uuid_generate_time_safe(uuid_t out)
{
    return uuid_generate_time_generic(out);
}


void __uuid_generate_random(uuid_t out, int *num)
{
    uuid_t	buf;
    struct uuid uu;
    int i, n;

    if (!num || !*num)
        n = 1;
    else
        n = *num;

    for (i = 0; i < n; i++) {
        random_get_bytes(buf, sizeof(buf));
        uuid_unpack(buf, &uu);

        uu.clock_seq = (uu.clock_seq & 0x3FFF) | 0x8000;
        uu.time_hi_and_version = (uu.time_hi_and_version & 0x0FFF)
                                 | 0x4000;
        uuid_pack(&uu, out);
        out += sizeof(uuid_t);
    }
}

void uuid_generate_random(uuid_t out)
{
    int	num = 1;
    /* No real reason to use the daemon for random uuid's -- yet */

    __uuid_generate_random(out, &num);
}

/*
 * Check whether good random source (/dev/random or /dev/urandom)
 * is available.
 */
static int have_random_source(void)
{
    struct stat s;

    return (!stat("/dev/random", &s) || !stat("/dev/urandom", &s));
}


/*
 * This is the generic front-end to uuid_generate_random and
 * uuid_generate_time.  It uses uuid_generate_random only if
 * /dev/urandom is available, since otherwise we won't have
 * high-quality randomness.
 */
void uuid_generate(uuid_t out)
{
    if (have_random_source())
        uuid_generate_random(out);
    else
        uuid_generate_time(out);
}

/*
 * General purpose random utilities
 *
 * Based on libuuid code.
 *
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License.
 */
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#if defined(__linux__) && defined(HAVE_SYS_SYSCALL_H)
#include <sys/syscall.h>
#endif

//#include "randutils.h"

#ifdef HAVE_TLS
#define THREAD_LOCAL static __thread
#else
#define THREAD_LOCAL static
#endif

#if defined(__linux__) && defined(__NR_gettid) && defined(HAVE_JRAND48)
#define DO_JRAND_MIX
THREAD_LOCAL unsigned short ul_jrand_seed[3];
#endif

int random_get_fd(void)
{
    int i, fd;
    struct timeval	tv;

    gettimeofday(&tv, 0);
    fd = open("/dev/urandom", O_RDONLY);
    if (fd == -1)
        fd = open("/dev/random", O_RDONLY | O_NONBLOCK);
    if (fd >= 0) {
        i = fcntl(fd, F_GETFD);
        if (i >= 0)
            fcntl(fd, F_SETFD, i | FD_CLOEXEC);
    }
    srand((getpid() << 16) ^ getuid() ^ tv.tv_sec ^ tv.tv_usec);

#ifdef DO_JRAND_MIX
    ul_jrand_seed[0] = getpid() ^ (tv.tv_sec & 0xFFFF);
	ul_jrand_seed[1] = getppid() ^ (tv.tv_usec & 0xFFFF);
	ul_jrand_seed[2] = (tv.tv_sec ^ tv.tv_usec) >> 16;
#endif
    /* Crank the random number generator a few times */
    gettimeofday(&tv, 0);
    for (i = (tv.tv_sec ^ tv.tv_usec) & 0x1F; i > 0; i--)
        rand();
    return fd;
}


/*
 * Generate a stream of random nbytes into buf.
 * Use /dev/urandom if possible, and if not,
 * use glibc pseudo-random functions.
 */
void random_get_bytes(void *buf, size_t nbytes)
{
    size_t i, n = nbytes;
    int fd = random_get_fd();
    int lose_counter = 0;
    unsigned char *cp = (unsigned char *) buf;

    if (fd >= 0) {
        while (n > 0) {
            ssize_t x = read(fd, cp, n);
            if (x <= 0) {
                if (lose_counter++ > 16)
                    break;
                continue;
            }
            n -= x;
            cp += x;
            lose_counter = 0;
        }

        close(fd);
    }

    /*
     * We do this all the time, but this is the only source of
     * randomness if /dev/random/urandom is out to lunch.
     */
    for (cp = (unsigned char *)buf, i = 0; i < nbytes; i++)
        *cp++ ^= (rand() >> 7) & 0xFF;

#ifdef DO_JRAND_MIX
    {
		unsigned short tmp_seed[3];

		memcpy(tmp_seed, ul_jrand_seed, sizeof(tmp_seed));
		ul_jrand_seed[2] = ul_jrand_seed[2] ^ syscall(__NR_gettid);
		for (cp = buf, i = 0; i < nbytes; i++)
			*cp++ ^= (jrand48(tmp_seed) >> 7) & 0xFF;
		memcpy(ul_jrand_seed, tmp_seed,
		       sizeof(ul_jrand_seed)-sizeof(unsigned short));
	}
#endif

    return;
}

#ifdef TEST_PROGRAM
int main(int argc __attribute__ ((__unused__)),
         char *argv[] __attribute__ ((__unused__)))
{
	unsigned int v, i;

	/* generate and print 10 random numbers */
	for (i = 0; i < 10; i++) {
		random_get_bytes(&v, sizeof(v));
		printf("%d\n", v);
	}

	return EXIT_SUCCESS;
}
#endif /* TEST_PROGRAM */

/*
 * Internal routine for packing UUIDs
 *
 * Copyright (C) 1996, 1997 Theodore Ts'o.
 *
 * %Begin-Header%
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, and the entire permission notice in its entirety,
 *    including the disclaimer of warranties.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote
 *    products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE, ALL OF
 * WHICH ARE HEREBY DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OF THIS SOFTWARE, EVEN IF NOT ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 * %End-Header%
 */

#include <string.h>
//#include "uuidP.h"

void uuid_pack(const struct uuid *uu, uuid_t ptr)
{
    uint32_t	tmp;
    unsigned char	*out = ptr;

    tmp = uu->time_low;
    out[3] = (unsigned char) tmp;
    tmp >>= 8;
    out[2] = (unsigned char) tmp;
    tmp >>= 8;
    out[1] = (unsigned char) tmp;
    tmp >>= 8;
    out[0] = (unsigned char) tmp;

    tmp = uu->time_mid;
    out[5] = (unsigned char) tmp;
    tmp >>= 8;
    out[4] = (unsigned char) tmp;

    tmp = uu->time_hi_and_version;
    out[7] = (unsigned char) tmp;
    tmp >>= 8;
    out[6] = (unsigned char) tmp;

    tmp = uu->clock_seq;
    out[9] = (unsigned char) tmp;
    tmp >>= 8;
    out[8] = (unsigned char) tmp;

    memcpy(out+10, uu->node, 6);
}

/*
 * Internal routine for unpacking UUID
 *
 * Copyright (C) 1996, 1997 Theodore Ts'o.
 *
 * %Begin-Header%
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, and the entire permission notice in its entirety,
 *    including the disclaimer of warranties.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote
 *    products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE, ALL OF
 * WHICH ARE HEREBY DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OF THIS SOFTWARE, EVEN IF NOT ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 * %End-Header%
 */

#include <string.h>
//#include "uuidP.h"

void uuid_unpack(const uuid_t in, struct uuid *uu)
{
    const uint8_t	*ptr = in;
    uint32_t		tmp;

    tmp = *ptr++;
    tmp = (tmp << 8) | *ptr++;
    tmp = (tmp << 8) | *ptr++;
    tmp = (tmp << 8) | *ptr++;
    uu->time_low = tmp;

    tmp = *ptr++;
    tmp = (tmp << 8) | *ptr++;
    uu->time_mid = tmp;

    tmp = *ptr++;
    tmp = (tmp << 8) | *ptr++;
    uu->time_hi_and_version = tmp;

    tmp = *ptr++;
    tmp = (tmp << 8) | *ptr++;
    uu->clock_seq = tmp;

    memcpy(uu->node, ptr, 6);
}

