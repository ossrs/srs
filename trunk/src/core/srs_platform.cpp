#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include "srs_platform.hpp"


#if defined(_WIN32) && !defined(__CYGWIN__)

int socket_setup()
{
    WORD wVersionRequested;
    WSADATA wsaData;
    int err;

	/* Use the MAKEWORD(lowbyte, highbyte) macro declared in Windef.h */
    wVersionRequested = MAKEWORD(2, 2);

    err = WSAStartup(wVersionRequested, &wsaData);
    if (err != 0) {
        /* Tell the user that we could not find a usable */
        /* Winsock DLL.                                  */
        //printf("WSAStartup failed with error: %d\n", err);
        return -1;
    }
	return 0;
}

int socket_cleanup()
{
	WSACleanup();
	return 0;
}

    int gettimeofday(struct timeval* tv, struct timezone* tz)
    {  
        time_t clock;
        struct tm tm;
        SYSTEMTIME win_time;
    
        GetLocalTime(&win_time);
    
        tm.tm_year = win_time.wYear - 1900;
        tm.tm_mon = win_time.wMonth - 1;
        tm.tm_mday = win_time.wDay;
        tm.tm_hour = win_time.wHour;
        tm.tm_min = win_time.wMinute;
        tm.tm_sec = win_time.wSecond;
        tm.tm_isdst = -1;
    
        clock = mktime(&tm);
    
        tv->tv_sec = (long)clock;
        tv->tv_usec = win_time.wMilliseconds * 1000;
    
        return 0;
    }
	
    pid_t getpid(void)
    {
        return (pid_t)GetCurrentProcessId();
    }
    
    int usleep(useconds_t usec)
    {
        Sleep((DWORD)(usec / 1000));
        return 0;
    }
    
    ssize_t writev(int fd, const struct iovec *iov, int iovcnt)
    {
        ssize_t nwrite = 0;
        for (int i = 0; i < iovcnt; i++) {
            const struct iovec* current = iov + i;
    
            int nsent = ::send(fd, (char*)current->iov_base, current->iov_len, 0);
            if (nsent < 0) {
                return nsent;
            }
    
            nwrite += nsent;
            if (nsent == 0) {
                return nwrite;
            }
        }
        return nwrite;
    }

    ////////////////////////   strlcpy.c (modified) //////////////////////////
    
    /*    $OpenBSD: strlcpy.c,v 1.11 2006/05/05 15:27:38 millert Exp $    */
    
    /*-
     * Copyright (c) 1998 Todd C. Miller <Todd.Miller@courtesan.com>
     *
     * Permission to use, copy, modify, and distribute this software for any
     * purpose with or without fee is hereby granted, provided that the above
     * copyright notice and this permission notice appear in all copies.
     *
     * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
     * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
     * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
     * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
     * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
     * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
     * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
     */
    
    //#include <sys/cdefs.h> // ****
    //#include <cstddef> // ****
    // __FBSDID("$FreeBSD: stable/9/sys/libkern/strlcpy.c 243811 2012-12-03 18:08:44Z delphij $"); // ****
    
    // #include <sys/types.h> // ****
    // #include <sys/libkern.h> // ****
    
    /*
     * Copy src to string dst of size siz.  At most siz-1 characters
     * will be copied.  Always NUL terminates (unless siz == 0).
     * Returns strlen(src); if retval >= siz, truncation occurred.
     */
    
    //#define __restrict // ****
    
    size_t strlcpy(char * __restrict dst, const char * __restrict src, size_t siz)
    {
        char *d = dst;
        const char *s = src;
        size_t n = siz;
    
        /* Copy as many bytes as will fit */
        if (n != 0) {
            while (--n != 0) {
                if ((*d++ = *s++) == '\0')
                    break;
            }
        }
    
        /* Not enough room in dst, add NUL and traverse rest of src */
        if (n == 0) {
            if (siz != 0)
                *d = '\0';        /* NUL-terminate dst */
            while (*s++)
                ;
        }
    
        return(s - src - 1);    /* count does not include NUL */
    }
    
    // http://www.cplusplus.com/forum/general/141779/////////////////////////   inet_ntop.c (modified) //////////////////////////
    /*
     * Copyright (c) 2004 by Internet Systems Consortium, Inc. ("ISC")
     * Copyright (c) 1996-1999 by Internet Software Consortium.
     *
     * Permission to use, copy, modify, and distribute this software for any
     * purpose with or without fee is hereby granted, provided that the above
     * copyright notice and this permission notice appear in all copies.
     *
     * THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES
     * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
     * MERCHANTABILITY AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR
     * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
     * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
     * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT
     * OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
     */
    
    // #if defined(LIBC_SCCS) && !defined(lint) // ****
    //static const char rcsid[] = "$Id: inet_ntop.c,v 1.3.18.2 2005/11/03 23:02:22 marka Exp $";
    // #endif /* LIBC_SCCS and not lint */ // ****
    // #include <sys/cdefs.h> // ****
    // __FBSDID("$FreeBSD: stable/9/sys/libkern/inet_ntop.c 213103 2010-09-24 15:01:45Z attilio $"); // ****
    
    //#define _WIN32_WINNT _WIN32_WINNT_WIN8 // ****
    //#include <Ws2tcpip.h> // ****
    //#pragma comment(lib, "Ws2_32.lib") // ****
    //#include <cstdio> // ****
    
    // #include <sys/param.h> // ****
    // #include <sys/socket.h> // ****
    // #include <sys/systm.h> // ****
    
    // #include <netinet/in.h> // ****
    
    /*%
     * WARNING: Don't even consider trying to compile this on a system where
     * sizeof(int) < 4.  sizeof(int) > 4 is fine; all the world's not a VAX.
     */
    
    static char *inet_ntop4(const u_char *src, char *dst, socklen_t size);
    static char *inet_ntop6(const u_char *src, char *dst, socklen_t size);
    
    /* char *
     * inet_ntop(af, src, dst, size)
     *    convert a network format address to presentation format.
     * return:
     *    pointer to presentation format address (`dst'), or NULL (see errno).
     * author:
     *    Paul Vixie, 1996.
     */
    const char* inet_ntop(int af, const void *src, char *dst, socklen_t size)
    {
        switch (af) {
        case AF_INET:
            return (inet_ntop4( (unsigned char*)src, (char*)dst, size)); // ****
    #ifdef AF_INET6
        //#error "IPv6 not supported"
        //case AF_INET6:
        //    return (char*)(inet_ntop6( (unsigned char*)src, (char*)dst, size)); // ****
    #endif
        default:
            // return (NULL); // ****
            return 0 ; // ****
        }
        /* NOTREACHED */
    }
    
    /* const char *
     * inet_ntop4(src, dst, size)
     *    format an IPv4 address
     * return:
     *    `dst' (as a const)
     * notes:
     *    (1) uses no statics
     *    (2) takes a u_char* not an in_addr as input
     * author:
     *    Paul Vixie, 1996.
     */
    static char * inet_ntop4(const u_char *src, char *dst, socklen_t size)
    {
        static const char fmt[128] = "%u.%u.%u.%u";
        char tmp[sizeof "255.255.255.255"];
        int l;
    
        l = snprintf(tmp, sizeof(tmp), fmt, src[0], src[1], src[2], src[3]); // ****
        if (l <= 0 || (socklen_t) l >= size) {
            return (NULL);
        }
        strlcpy(dst, tmp, size);
        return (dst);
    }
    
    /* const char *
     * inet_ntop6(src, dst, size)
     *    convert IPv6 binary address into presentation (printable) format
     * author:
     *    Paul Vixie, 1996.
     */
    static char * inet_ntop6(const u_char *src, char *dst, socklen_t size)
    {
        /*
         * Note that int32_t and int16_t need only be "at least" large enough
         * to contain a value of the specified size.  On some systems, like
         * Crays, there is no such thing as an integer variable with 16 bits.
         * Keep this in mind if you think this function should have been coded
         * to use pointer overlays.  All the world's not a VAX.
         */
        char tmp[sizeof "ffff:ffff:ffff:ffff:ffff:ffff:255.255.255.255"], *tp;
        struct { int base, len; } best, cur;
    #define NS_IN6ADDRSZ 16
    #define NS_INT16SZ 2
        u_int words[NS_IN6ADDRSZ / NS_INT16SZ];
        int i;
    
        /*
         * Preprocess:
         *    Copy the input (bytewise) array into a wordwise array.
         *    Find the longest run of 0x00's in src[] for :: shorthanding.
         */
        memset(words, '\0', sizeof words);
        for (i = 0; i < NS_IN6ADDRSZ; i++)
            words[i / 2] |= (src[i] << ((1 - (i % 2)) << 3));
        best.base = -1;
        best.len = 0;
        cur.base = -1;
        cur.len = 0;
        for (i = 0; i < (NS_IN6ADDRSZ / NS_INT16SZ); i++) {
            if (words[i] == 0) {
                if (cur.base == -1)
                    cur.base = i, cur.len = 1;
                else
                    cur.len++;
            } else {
                if (cur.base != -1) {
                    if (best.base == -1 || cur.len > best.len)
                        best = cur;
                    cur.base = -1;
                }
            }
        }
        if (cur.base != -1) {
            if (best.base == -1 || cur.len > best.len)
                best = cur;
        }
        if (best.base != -1 && best.len < 2)
            best.base = -1;
    
        /*
         * Format the result.
         */
        tp = tmp;
        for (i = 0; i < (NS_IN6ADDRSZ / NS_INT16SZ); i++) {
            /* Are we inside the best run of 0x00's? */
            if (best.base != -1 && i >= best.base &&
                i < (best.base + best.len)) {
                if (i == best.base)
                    *tp++ = ':';
                continue;
            }
            /* Are we following an initial run of 0x00s or any real hex? */
            if (i != 0)
                *tp++ = ':';
            /* Is this address an encapsulated IPv4? */
            if (i == 6 && best.base == 0 && (best.len == 6 ||
                (best.len == 7 && words[7] != 0x0001) ||
                (best.len == 5 && words[5] == 0xffff))) {
                if (!inet_ntop4(src+12, tp, sizeof tmp - (tp - tmp)))
                    return (NULL);
                tp += strlen(tp);
                break;
            }
            tp += sprintf(tp, "%x", words[i]); // ****
        }
        /* Was it a trailing run of 0x00's? */
        if (best.base != -1 && (best.base + best.len) == 
            (NS_IN6ADDRSZ / NS_INT16SZ))
            *tp++ = ':';
        *tp++ = '\0';
    
        /*
         * Check for overflow, copy, and we're done.
         */
        if ((socklen_t)(tp - tmp) > size) {
            return (NULL);
        }
        strcpy(dst, tmp);
        return (dst);
    }

#define Set_errno(num) SetLastError((num))

/* INVALID_SOCKET == INVALID_HANDLE_VALUE == (unsigned int)(~0) */
/* SOCKET_ERROR == -1 */
//#define ENOTSOCK WSAENOTSOCK
//#define EINTR    WSAEINTR
//#define ENOBUFS  WSAENOBUFS
//#define EWOULDBLOCK           WSAEWOULDBLOCK
#define EAFNOSUPPORT  WSAEAFNOSUPPORT
/* from public\sdk\inc\crt\errno.h */
#define ENOSPC          28

/*
 *
 */
/*
#ifndef INET_ADDRSTRLEN
#define INET_ADDRSTRLEN    16
#endif

static const char *
inet_ntop_v4 (const void *src, char *dst, size_t size)
{
    const char digits[] = "0123456789";
    int i;
    struct in_addr *addr = (struct in_addr *)src;
    u_long a = ntohl(addr->s_addr);
    const char *orig_dst = dst;

    if (size < INET_ADDRSTRLEN) {
      Set_errno(ENOSPC);
      return NULL;
    }
    for (i = 0; i < 4; ++i) {
	int n = (a >> (24 - i * 8)) & 0xFF;
	int non_zerop = 0;

	if (non_zerop || n / 100 > 0) {
	    *dst++ = digits[n / 100];
	    n %= 100;
	    non_zerop = 1;
	}
	if (non_zerop || n / 10 > 0) {
	    *dst++ = digits[n / 10];
	    n %= 10;
	    non_zerop = 1;
	}
	*dst++ = digits[n];
	if (i != 3)
	    *dst++ = '.';
    }
    *dst++ = '\0';
    return orig_dst;
}

const char *
inet_ntop(int af, const void *src, char *dst, size_t size)
{
    switch (af) {
    case AF_INET :
	return inet_ntop_v4 (src, dst, size);
    default :
      Set_errno(EAFNOSUPPORT);
      return NULL;
    }
}

*/
#endif

