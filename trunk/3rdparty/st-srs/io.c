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

#include <stdlib.h>
#if !defined(WIN64)
#include <unistd.h>
#endif
#include <sys/types.h>
#if !defined(WIN64)
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/uio.h>
#include <sys/time.h>
#include <sys/resource.h>
#endif
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include "common.h"

#if defined(WIN64)
/*
 * Native Windows implements the POSIX calls of this file with Winsock, for sockets
 * only, so the library brings ws2_32.lib. st_open (files and FIFOs) is not supported.
 */
#include <limits.h>
#include <mswsock.h>
#pragma comment(lib, "ws2_32.lib")

/*
 * Map a Winsock error to errno, which ST and its callers such as SRS check. For
 * connect (connecting is set), WSAEWOULDBLOCK means EINPROGRESS. An error with no
 * equivalent is EIO; WSAGetLastError() still has the Winsock error.
 */
int _st_win64_errno(int wsaerr, int connecting)
{
    switch (wsaerr) {
    case 0:                     return 0;
    case WSAEWOULDBLOCK:        return connecting ? EINPROGRESS : EAGAIN;
    case WSAEINPROGRESS:        return EINPROGRESS;
    case WSAEALREADY:           return EALREADY;
    case WSAEINTR:              return EINTR;
    case WSAETIMEDOUT:          return ETIMEDOUT;
    case WSAEBADF:              return EBADF;
    case WSAEACCES:             return EACCES;
    case WSAEFAULT:             return EFAULT;
    case WSAEINVAL:             return EINVAL;
    case WSAEMFILE:             return EMFILE;
    case WSAENOTSOCK:           return ENOTSOCK;
    case WSAEDESTADDRREQ:       return EDESTADDRREQ;
    case WSAEMSGSIZE:           return EMSGSIZE;
    case WSAEPROTOTYPE:         return EPROTOTYPE;
    case WSAENOPROTOOPT:        return ENOPROTOOPT;
    case WSAEPROTONOSUPPORT:    return EPROTONOSUPPORT;
    case WSAESOCKTNOSUPPORT:    return EPROTONOSUPPORT;
    case WSAEOPNOTSUPP:         return EOPNOTSUPP;
    case WSAEPFNOSUPPORT:       return EAFNOSUPPORT;
    case WSAEAFNOSUPPORT:       return EAFNOSUPPORT;
    case WSAEADDRINUSE:         return EADDRINUSE;
    case WSAEADDRNOTAVAIL:      return EADDRNOTAVAIL;
    case WSAENETDOWN:           return ENETDOWN;
    case WSAENETUNREACH:        return ENETUNREACH;
    case WSAENETRESET:          return ENETRESET;
    case WSAECONNABORTED:       return ECONNABORTED;
    case WSAECONNRESET:         return ECONNRESET;
    case WSAENOBUFS:            return ENOBUFS;
    case WSAEISCONN:            return EISCONN;
    case WSAENOTCONN:           return ENOTCONN;
    case WSAESHUTDOWN:          return EPIPE;
    case WSAECONNREFUSED:       return ECONNREFUSED;
    case WSAELOOP:              return ELOOP;
    case WSAENAMETOOLONG:       return ENAMETOOLONG;
    case WSAEHOSTDOWN:          return EHOSTUNREACH;
    case WSAEHOSTUNREACH:       return EHOSTUNREACH;
    case WSAENOTEMPTY:          return ENOTEMPTY;
    case WSA_INVALID_HANDLE:    return EBADF;
    case WSA_NOT_ENOUGH_MEMORY: return ENOMEM;
    case WSA_INVALID_PARAMETER: return EINVAL;
    case WSA_OPERATION_ABORTED: return ECANCELED;
    case WSANOTINITIALISED:     return EINVAL;
    default:                    return EIO;
    }
}

/* Set errno from the Winsock error of the failed call, and return -1. */
static int _st_win64_sock_fail(int connecting)
{
    errno = _st_win64_errno(WSAGetLastError(), connecting);
    return -1;
}

/*
 * The Winsock calls ST makes, with errno set on failure. A SOCKET fits in an int
 * (only 32 bits are significant), so it is truncated and sign-extended back.
 */
static int _st_win64_accept(int fd, struct sockaddr *addr, socklen_t *addrlen)
{
    SOCKET s = accept((SOCKET)fd, addr, addrlen);
    return (s == INVALID_SOCKET) ? _st_win64_sock_fail(0) : (int)s;
}

static int _st_win64_connect(int fd, const struct sockaddr *addr, int addrlen)
{
    return (connect((SOCKET)fd, addr, addrlen) == SOCKET_ERROR) ? _st_win64_sock_fail(1) : 0;
}

/* SO_ERROR, which st_connect reads after the wait, is a Winsock error, so it gives the errno instead. */
static int _st_win64_getsockopt(int fd, int level, int name, char *value, socklen_t *size)
{
    if (getsockopt((SOCKET)fd, level, name, value, size) == SOCKET_ERROR)
        return _st_win64_sock_fail(0);
    if (level == SOL_SOCKET && name == SO_ERROR && *size == sizeof(int) && *(int *)value)
        *(int *)value = _st_win64_errno(*(int *)value, 1);
    return 0;
}

/*
 * Set errno from the Winsock error of a failed receive, and return -1. Winsock reports
 * an ICMP port unreachable on a datagram socket as WSAECONNRESET, where POSIX gives
 * ECONNREFUSED.
 */
static int _st_win64_recv_sock_fail(int fd)
{
    int err = WSAGetLastError();
    int type = 0;
    socklen_t size = sizeof(type);

    if (err == WSAECONNRESET && getsockopt((SOCKET)fd, SOL_SOCKET, SO_TYPE, (char *)&type, &size) == 0 &&
        type == SOCK_DGRAM) {
        WSASetLastError(err);
        errno = ECONNREFUSED;
        return -1;
    }
    WSASetLastError(err);
    errno = _st_win64_errno(err, 0);
    return -1;
}

int _st_win64_recvfrom(int fd, void *buf, int len, int flags, struct sockaddr *from, socklen_t *fromlen)
{
    int n = recvfrom((SOCKET)fd, (char *)buf, len, flags, from, fromlen);
    if (n != SOCKET_ERROR)
        return n;
    /* A datagram larger than the buffer: POSIX returns the bytes that fit and drops the rest. */
    if (WSAGetLastError() == WSAEMSGSIZE)
        return len;
    return _st_win64_recv_sock_fail(fd);
}

static int _st_win64_sendto(int fd, const void *msg, int len, int flags, const struct sockaddr *to, int tolen)
{
    int n = sendto((SOCKET)fd, (const char *)msg, len, flags, to, tolen);
    return (n == SOCKET_ERROR) ? _st_win64_sock_fail(0) : n;
}

/* The fcntl commands and flag ST uses; the CRT has none for sockets. */
#define F_GETFL     3
#define F_SETFL     4
#define O_NONBLOCK  04000

static int _st_win64_enosys(void)
{
    errno = ENOSYS;
    return -1;
}

/* ioctl is ioctlsocket, for FIONBIO. */
int _st_win64_ioctl(int fd, unsigned long request, int *arg)
{
    u_long v = (u_long)*arg;
    if (ioctlsocket((SOCKET)fd, (long)request, &v) == SOCKET_ERROR)
        return _st_win64_sock_fail(0);
    *arg = (int)v;
    return 0;
}

/*
 * fcntl supports only sockets and only the non-blocking flag. Windows cannot read
 * the mode of a socket back, so F_GETFL checks that fd is a socket and gives 0.
 */
int _st_win64_fcntl(int fd, int cmd, int arg)
{
    u_long nonblock;
    int type;
    socklen_t size = sizeof(type);

    switch (cmd) {
    case F_GETFL:
        if (getsockopt((SOCKET)fd, SOL_SOCKET, SO_TYPE, (char *)&type, &size) == SOCKET_ERROR)
            return _st_win64_sock_fail(0);
        return 0;
    case F_SETFL:
        nonblock = (arg & O_NONBLOCK) ? 1 : 0;
        if (ioctlsocket((SOCKET)fd, FIONBIO, &nonblock) == SOCKET_ERROR)
            return _st_win64_sock_fail(0);
        return 0;
    default:
        errno = EINVAL;
        return -1;
    }
}

int _st_win64_close(int fd)
{
    return (closesocket((SOCKET)fd) == SOCKET_ERROR) ? _st_win64_sock_fail(0) : 0;
}

/* Winsock takes an int length; a larger request reads or writes less, as POSIX allows. */
static int _st_win64_len(size_t nbyte)
{
    return (nbyte > INT_MAX) ? INT_MAX : (int)nbyte;
}

ssize_t _st_win64_read(int fd, void *buf, size_t nbyte)
{
    int len = _st_win64_len(nbyte);
    int n = recv((SOCKET)fd, (char *)buf, len, 0);
    if (n != SOCKET_ERROR)
        return n;
    /* A datagram larger than the buffer: POSIX returns the bytes that fit and drops the rest. */
    if (WSAGetLastError() == WSAEMSGSIZE)
        return len;
    return _st_win64_recv_sock_fail(fd);
}

ssize_t _st_win64_write(int fd, const void *buf, size_t nbyte)
{
    int n = send((SOCKET)fd, (const char *)buf, _st_win64_len(nbyte), 0);
    return (n == SOCKET_ERROR) ? _st_win64_sock_fail(0) : n;
}

/* The WSABUFs kept on the stack; more are allocated. */
#define _ST_WIN64_LOCAL_BUFS 16

/*
 * Copy the iovecs to WSABUFs, which are not the same layout (length first, and a
 * 32-bit length), so never cast one to the other. Uses local when it is large
 * enough, or allocates the array, which the caller frees with _st_win64_bufs_free.
 * A vector longer than a WSABUF holds is cut, with the vectors after it, so the
 * call sends or receives less, as POSIX allows. Returns NULL with errno on error.
 */
static WSABUF *_st_win64_bufs(const struct iovec *iov, int iov_size, WSABUF *local, DWORD *count, size_t *total)
{
    WSABUF *bufs = local;
    int i;

    if (iov_size < 0 || (iov_size > 0 && !iov)) {
        errno = EINVAL;
        return NULL;
    }
    if (iov_size > _ST_WIN64_LOCAL_BUFS) {
        if ((bufs = (WSABUF *)malloc(iov_size * sizeof(WSABUF))) == NULL) {
            errno = ENOMEM;
            return NULL;
        }
    }

    *total = 0;
    for (i = 0; i < iov_size; i++) {
        int cut = iov[i].iov_len > ULONG_MAX;
        bufs[i].len = cut ? ULONG_MAX : (ULONG)iov[i].iov_len;
        bufs[i].buf = (char *)iov[i].iov_base;
        *total += bufs[i].len;
        if (cut) {
            i++;
            break;
        }
    }
    *count = (DWORD)i;
    return bufs;
}

static void _st_win64_bufs_free(WSABUF *bufs, WSABUF *local)
{
    if (bufs != local)
        free(bufs);
}

/* The result of a receive that failed: a truncated datagram is the bytes that fit. */
static ssize_t _st_win64_recv_fail(int fd, size_t total)
{
    if (WSAGetLastError() == WSAEMSGSIZE)
        return (ssize_t)total;
    return _st_win64_recv_sock_fail(fd);
}

ssize_t _st_win64_readv(int fd, const struct iovec *iov, int iov_size)
{
    WSABUF local[_ST_WIN64_LOCAL_BUFS], *bufs;
    DWORD count, nbytes = 0, flags = 0;
    size_t total;
    ssize_t rv;

    if (iov_size == 0)
        return 0;
    if ((bufs = _st_win64_bufs(iov, iov_size, local, &count, &total)) == NULL)
        return -1;
    if (WSARecv((SOCKET)fd, bufs, count, &nbytes, &flags, NULL, NULL) == 0)
        rv = (ssize_t)nbytes;
    else
        rv = _st_win64_recv_fail(fd, total);
    _st_win64_bufs_free(bufs, local);
    return rv;
}

ssize_t _st_win64_writev(int fd, const struct iovec *iov, int iov_size)
{
    WSABUF local[_ST_WIN64_LOCAL_BUFS], *bufs;
    DWORD count, nbytes = 0;
    size_t total;
    ssize_t rv;

    if (iov_size == 0)
        return 0;
    if ((bufs = _st_win64_bufs(iov, iov_size, local, &count, &total)) == NULL)
        return -1;
    if (WSASend((SOCKET)fd, bufs, count, &nbytes, 0, NULL, NULL) == 0)
        rv = (ssize_t)nbytes;
    else
        rv = _st_win64_sock_fail(0);
    _st_win64_bufs_free(bufs, local);
    return rv;
}

/*
 * WSARecvMsg is a Winsock extension, loaded once with WSAIoctl. The Microsoft
 * providers all return the same function, so one pointer serves every socket.
 */
static LPFN_WSARECVMSG _st_win64_wsarecvmsg = NULL;

static LPFN_WSARECVMSG _st_win64_load_wsarecvmsg(int fd)
{
    GUID guid = WSAID_WSARECVMSG;
    LPFN_WSARECVMSG fn = NULL;
    DWORD n = 0;

    if (_st_win64_wsarecvmsg)
        return _st_win64_wsarecvmsg;
    if (WSAIoctl((SOCKET)fd, SIO_GET_EXTENSION_FUNCTION_POINTER, &guid, sizeof(guid), &fn, sizeof(fn), &n,
        NULL, NULL) == SOCKET_ERROR) {
        _st_win64_sock_fail(0);
        return NULL;
    }
    _st_win64_wsarecvmsg = fn;
    return fn;
}

/*
 * WSARecvMsg and WSASendMsg take only datagram and raw sockets, while POSIX recvmsg
 * and sendmsg take streams too. After such a failure, this tells whether fd is a
 * stream, which then uses WSARecv or WSASend.
 */
static int _st_win64_is_stream(int fd, int wsaerr)
{
    int type = 0;
    socklen_t size = sizeof(type);

    if (wsaerr != WSAEINVAL && wsaerr != WSAEOPNOTSUPP)
        return 0;
    if (getsockopt((SOCKET)fd, SOL_SOCKET, SO_TYPE, (char *)&type, &size) == SOCKET_ERROR)
        return 0;
    WSASetLastError(wsaerr);
    return type == SOCK_STREAM;
}

/*
 * recvmsg with WSARecvMsg. The control data is Winsock's (WSA_CMSG_* macros, for
 * example IP_PKTINFO), not the POSIX layout. A truncated datagram returns the bytes
 * that fit with MSG_TRUNC, as on POSIX, where Winsock fails with WSAEMSGSIZE.
 */
int _st_win64_recvmsg(int fd, struct msghdr *msg, int flags)
{
    WSABUF local[_ST_WIN64_LOCAL_BUFS], *bufs;
    LPFN_WSARECVMSG fn;
    WSAMSG wmsg;
    DWORD count, nbytes = 0, dflags;
    size_t total;
    int rv, err;

    if ((fn = _st_win64_load_wsarecvmsg(fd)) == NULL)
        return -1;
    if ((bufs = _st_win64_bufs(msg->msg_iov, msg->msg_iovlen, local, &count, &total)) == NULL)
        return -1;

    wmsg.name = (LPSOCKADDR)msg->msg_name;
    wmsg.namelen = msg->msg_name ? msg->msg_namelen : 0;
    wmsg.lpBuffers = bufs;
    wmsg.dwBufferCount = count;
    wmsg.Control.buf = (char *)msg->msg_control;
    wmsg.Control.len = msg->msg_control ? (ULONG)msg->msg_controllen : 0;
    wmsg.dwFlags = (DWORD)flags;

    if ((*fn)((SOCKET)fd, &wmsg, &nbytes, NULL, NULL) == 0) {
        rv = (int)nbytes;
    } else if ((err = WSAGetLastError()) == WSAEMSGSIZE) {
        rv = (int)total;
        wmsg.dwFlags |= MSG_TRUNC;
    } else if (_st_win64_is_stream(fd, err)) {
        /* A stream has no sender address and no control data. */
        dflags = (DWORD)flags;
        rv = (WSARecv((SOCKET)fd, bufs, count, &nbytes, &dflags, NULL, NULL) == 0) ?
            (int)nbytes : _st_win64_sock_fail(0);
        wmsg.namelen = 0;
        wmsg.Control.len = 0;
        wmsg.dwFlags = 0;
    } else {
        rv = _st_win64_recv_sock_fail(fd);
    }

    if (rv >= 0) {
        msg->msg_namelen = wmsg.namelen;
        msg->msg_controllen = wmsg.Control.len;
        msg->msg_flags = (int)wmsg.dwFlags;
    }
    _st_win64_bufs_free(bufs, local);
    return rv;
}

/* sendmsg with WSASendMsg; the control data is Winsock's, as for recvmsg. */
int _st_win64_sendmsg(int fd, const struct msghdr *msg, int flags)
{
    WSABUF local[_ST_WIN64_LOCAL_BUFS], *bufs;
    WSAMSG wmsg;
    DWORD count, nbytes = 0;
    size_t total;
    int rv;

    if ((bufs = _st_win64_bufs(msg->msg_iov, msg->msg_iovlen, local, &count, &total)) == NULL)
        return -1;

    wmsg.name = (LPSOCKADDR)msg->msg_name;
    wmsg.namelen = msg->msg_name ? msg->msg_namelen : 0;
    wmsg.lpBuffers = bufs;
    wmsg.dwBufferCount = count;
    wmsg.Control.buf = (char *)msg->msg_control;
    wmsg.Control.len = msg->msg_control ? (ULONG)msg->msg_controllen : 0;
    wmsg.dwFlags = 0;

    if (WSASendMsg((SOCKET)fd, &wmsg, (DWORD)flags, &nbytes, NULL, NULL) == 0)
        rv = (int)nbytes;
    else if (_st_win64_is_stream(fd, WSAGetLastError()))
        rv = (WSASend((SOCKET)fd, bufs, count, &nbytes, (DWORD)flags, NULL, NULL) == 0) ?
            (int)nbytes : _st_win64_sock_fail(0);
    else
        rv = _st_win64_sock_fail(0);
    _st_win64_bufs_free(bufs, local);
    return rv;
}

#define ioctl       _st_win64_ioctl
#define fcntl       _st_win64_fcntl
#define close       _st_win64_close
#define read        _st_win64_read
#define write       _st_win64_write
#define readv       _st_win64_readv
#define writev      _st_win64_writev
#define recvmsg     _st_win64_recvmsg
#define sendmsg     _st_win64_sendmsg
#define accept      _st_win64_accept
#define connect     _st_win64_connect
#define getsockopt  _st_win64_getsockopt
#define recvfrom    _st_win64_recvfrom
#define sendto      _st_win64_sendto
#endif

// Global stat.
#if defined(DEBUG) && defined(DEBUG_STATS)
__thread unsigned long long _st_stat_recvfrom = 0;
__thread unsigned long long _st_stat_recvfrom_eagain = 0;
__thread unsigned long long _st_stat_sendto = 0;
__thread unsigned long long _st_stat_sendto_eagain = 0;
__thread unsigned long long _st_stat_read = 0;
__thread unsigned long long _st_stat_read_eagain = 0;
__thread unsigned long long _st_stat_readv = 0;
__thread unsigned long long _st_stat_readv_eagain = 0;
__thread unsigned long long _st_stat_writev = 0;
__thread unsigned long long _st_stat_writev_eagain = 0;
__thread unsigned long long _st_stat_recvmsg = 0;
__thread unsigned long long _st_stat_recvmsg_eagain = 0;
__thread unsigned long long _st_stat_sendmsg = 0;
__thread unsigned long long _st_stat_sendmsg_eagain = 0;
#endif

#if EAGAIN != EWOULDBLOCK
    #define _IO_NOT_READY_ERROR  ((errno == EAGAIN) || (errno == EWOULDBLOCK))
#else
    #define _IO_NOT_READY_ERROR  (errno == EAGAIN)
#endif

#define _LOCAL_MAXIOV  16

/* File descriptor object free list */
static __thread _st_netfd_t *_st_netfd_freelist = NULL;
/* Maximum number of file descriptors that the process can open */
static int _st_osfd_limit = -1;

static void _st_netfd_free_aux_data(_st_netfd_t *fd);

#if defined(WIN64)
/*
 * Windows has no SIGPIPE and no rlimit. A process may open up to 2^24 handles, sockets
 * included, so report that unless the event system has a lower limit.
 */
#define _ST_WIN64_OSFD_LIMIT (1 << 24)

/* Whether this thread's st_init started Winsock, which st_destroy cleans up. */
static __thread int _st_wsa_started = 0;

int _st_io_init(void)
{
    int fdlim;

    /*
     * Winsock needs WSAStartup before any socket call. It counts the starts, so each
     * thread's ST starts it once and st_destroy cleans up that start.
     */
    if (!_st_wsa_started) {
        WSADATA wsa;
        int err = WSAStartup(MAKEWORD(2, 2), &wsa);
        if (err != 0) {
            errno = _st_win64_errno(err, 0);
            return -1;
        }
        if (LOBYTE(wsa.wVersion) != 2 || HIBYTE(wsa.wVersion) != 2) {
            WSACleanup();
            errno = EINVAL;
            return -1;
        }
        _st_wsa_started = 1;
    }

    fdlim = (*_st_eventsys->fd_getlimit)();
    _st_osfd_limit = (fdlim > 0 && fdlim < _ST_WIN64_OSFD_LIMIT) ? fdlim : _ST_WIN64_OSFD_LIMIT;
    return 0;
}

void _st_io_destroy(void)
{
    if (_st_wsa_started) {
        WSACleanup();
        _st_wsa_started = 0;
    }
}
#else
int _st_io_init(void)
{
    struct sigaction sigact;
    struct rlimit rlim;
    int fdlim;

    /* Ignore SIGPIPE */
    sigact.sa_handler = SIG_IGN;
    sigemptyset(&sigact.sa_mask);
    sigact.sa_flags = 0;
    if (sigaction(SIGPIPE, &sigact, NULL) < 0) /* GCOVR_EXCL_BR_LINE */
        return -1; /* GCOVR_EXCL_LINE */

    /* Set maximum number of open file descriptors */
    if (getrlimit(RLIMIT_NOFILE, &rlim) < 0) /* GCOVR_EXCL_BR_LINE */
        return -1; /* GCOVR_EXCL_LINE */

    fdlim = (*_st_eventsys->fd_getlimit)();
    if (fdlim > 0 && rlim.rlim_max > (rlim_t) fdlim) {
        rlim.rlim_max = fdlim;
    }
    
    /**
     * by SRS, for osx.
     * when rlimit max is negative, for example, osx, use cur directly.
     * @see https://github.com/ossrs/srs/issues/336
     */
    if ((int)rlim.rlim_max < 0) { /* GCOVR_EXCL_BR_LINE */
        _st_osfd_limit = (int)(fdlim > 0? fdlim : rlim.rlim_cur); /* GCOVR_EXCL_LINE */
        return 0; /* GCOVR_EXCL_LINE */
    }
    
    rlim.rlim_cur = rlim.rlim_max;
    if (setrlimit(RLIMIT_NOFILE, &rlim) < 0) /* GCOVR_EXCL_BR_LINE */
        return -1; /* GCOVR_EXCL_LINE */
    _st_osfd_limit = (int) rlim.rlim_max;

    return 0;
}
#endif


int st_getfdlimit(void)
{
    return _st_osfd_limit;
}


void st_netfd_free(_st_netfd_t *fd)
{
    if (!fd->inuse)
        return;

    fd->inuse = 0;
    /* GCOVR_EXCL_START */
    if (fd->aux_data)
        _st_netfd_free_aux_data(fd);
    /* GCOVR_EXCL_STOP */
    if (fd->private_data && fd->destructor)
        (*(fd->destructor))(fd->private_data);
    fd->private_data = NULL;
    fd->destructor = NULL;
    fd->next = _st_netfd_freelist;
    _st_netfd_freelist = fd;
}


static _st_netfd_t *_st_netfd_new(int osfd, int nonblock, int is_socket)
{
    _st_netfd_t *fd;
    int flags = 1;

    if ((*_st_eventsys->fd_new)(osfd) < 0)
        return NULL;

    if (_st_netfd_freelist) {
        fd = _st_netfd_freelist;
        _st_netfd_freelist = _st_netfd_freelist->next;
    } else {
        fd = calloc(1, sizeof(_st_netfd_t));
        if (!fd) /* GCOVR_EXCL_BR_LINE */
            return NULL; /* GCOVR_EXCL_LINE */
    }

    fd->osfd = osfd;
    fd->inuse = 1;
    fd->next = NULL;
    
    if (nonblock) {
        /* Use just one system call */
        if (is_socket && ioctl(osfd, FIONBIO, &flags) != -1)
            return fd;
        /* Do it the Posix way */
        if ((flags = fcntl(osfd, F_GETFL, 0)) < 0 || /* GCOVR_EXCL_BR_LINE */
            fcntl(osfd, F_SETFL, flags | O_NONBLOCK) < 0) {
            st_netfd_free(fd);
            return NULL;
        }
    }

    return fd;
}


_st_netfd_t *st_netfd_open(int osfd)
{
    return _st_netfd_new(osfd, 1, 0);
}


_st_netfd_t *st_netfd_open_socket(int osfd)
{
    return _st_netfd_new(osfd, 1, 1);
}


int st_netfd_close(_st_netfd_t *fd)
{
    if ((*_st_eventsys->fd_close)(fd->osfd) < 0)
        return -1;
    
    st_netfd_free(fd);
    return close(fd->osfd);
}


int st_netfd_fileno(_st_netfd_t *fd)
{
    return (fd->osfd);
}


void st_netfd_setspecific(_st_netfd_t *fd, void *value, _st_destructor_t destructor)
{
  if (value != fd->private_data) {
    /* Free up previously set non-NULL data value */
    if (fd->private_data && fd->destructor)
      (*(fd->destructor))(fd->private_data);
  }
  fd->private_data = value;
  fd->destructor = destructor;
}


void *st_netfd_getspecific(_st_netfd_t *fd)
{
    return (fd->private_data);
}


/*
 * Wait for I/O on a single descriptor.
 */
int st_netfd_poll(_st_netfd_t *fd, int how, st_utime_t timeout)
{
    struct pollfd pd;
    int n;
    
    pd.fd = fd->osfd;
    pd.events = (short) how;
    pd.revents = 0;
    
    if ((n = st_poll(&pd, 1, timeout)) < 0)
        return -1;
    if (n == 0) {
        /* Timed out */
        errno = ETIME;
        return -1;
    }
    if (pd.revents & POLLNVAL) {
        errno = EBADF;
        return -1;
    }
    
    return 0;
}


/* No-op */
int st_netfd_serialize_accept(_st_netfd_t *fd)
{
    fd->aux_data = NULL;
    return 0;
}

/* No-op. */
/* GCOVR_EXCL_START */
static void _st_netfd_free_aux_data(_st_netfd_t *fd)
{
    fd->aux_data = NULL;
}
/* GCOVR_EXCL_STOP */

_st_netfd_t *st_accept(_st_netfd_t *fd, struct sockaddr *addr, int *addrlen, st_utime_t timeout)
{
    int osfd, err;
    _st_netfd_t *newfd;
    
    while ((osfd = accept(fd->osfd, addr, (socklen_t *)addrlen)) < 0) {
        if (errno == EINTR)
            continue;
        if (!_IO_NOT_READY_ERROR)
            return NULL;
        /* Wait until the socket becomes readable */
        if (st_netfd_poll(fd, POLLIN, timeout) < 0)
            return NULL;
    }
    
    /* On some platforms the new socket created by accept() inherits */
    /* the nonblocking attribute of the listening socket */
#if defined (MD_ACCEPT_NB_INHERITED)
    newfd = _st_netfd_new(osfd, 0, 1);
#elif defined (MD_ACCEPT_NB_NOT_INHERITED)
    newfd = _st_netfd_new(osfd, 1, 1);
#else
    #error Unknown OS
#endif
    
    /* GCOVR_EXCL_START */
    if (!newfd) {
        err = errno;
        close(osfd);
        errno = err;
    }
    /* GCOVR_EXCL_STOP */
    
    return newfd;
}


int st_connect(_st_netfd_t *fd, const struct sockaddr *addr, int addrlen, st_utime_t timeout)
{
    int n, err = 0;
    
    while (connect(fd->osfd, addr, addrlen) < 0) {
        if (errno != EINTR) {
            /*
             * On some platforms, if connect() is interrupted (errno == EINTR)
             * after the kernel binds the socket, a subsequent connect()
             * attempt will fail with errno == EADDRINUSE.  Ignore EADDRINUSE
             * iff connect() was previously interrupted.  See Rich Stevens'
             * "UNIX Network Programming," Vol. 1, 2nd edition, p. 413
             * ("Interrupted connect").
             */
            if (errno != EINPROGRESS && (errno != EADDRINUSE || err == 0))
                return -1;
            /* Wait until the socket becomes writable */
            if (st_netfd_poll(fd, POLLOUT, timeout) < 0)
                return -1;
            /* Try to find out whether the connection setup succeeded or failed */
            n = sizeof(int);
            if (getsockopt(fd->osfd, SOL_SOCKET, SO_ERROR, (char *)&err, (socklen_t *)&n) < 0) /* GCOVR_EXCL_BR_LINE */
                return -1; /* GCOVR_EXCL_LINE */
            if (err) {
                errno = err;
                return -1;
            }
            break;
        }
        err = 1;
    }
    
    return 0;
}


ssize_t st_read(_st_netfd_t *fd, void *buf, size_t nbyte, st_utime_t timeout)
{
    ssize_t n;

    #if defined(DEBUG) && defined(DEBUG_STATS)
    ++_st_stat_read;
    #endif
    
    while ((n = read(fd->osfd, buf, nbyte)) < 0) {
        if (errno == EINTR)
            continue;
        if (!_IO_NOT_READY_ERROR)
            return -1;

        #if defined(DEBUG) && defined(DEBUG_STATS)
        ++_st_stat_read_eagain;
        #endif

        /* Wait until the socket becomes readable */
        if (st_netfd_poll(fd, POLLIN, timeout) < 0)
            return -1;
    }
    
    return n;
}


int st_read_resid(_st_netfd_t *fd, void *buf, size_t *resid, st_utime_t timeout)
{
    struct iovec iov, *riov;
    int riov_size, rv;
    
    iov.iov_base = buf;
    iov.iov_len = *resid;
    riov = &iov;
    riov_size = 1;
    rv = st_readv_resid(fd, &riov, &riov_size, timeout);
    *resid = iov.iov_len;
    return rv;
}


ssize_t st_readv(_st_netfd_t *fd, const struct iovec *iov, int iov_size, st_utime_t timeout)
{
    ssize_t n;

    #if defined(DEBUG) && defined(DEBUG_STATS)
    ++_st_stat_readv;
    #endif
    
    while ((n = readv(fd->osfd, iov, iov_size)) < 0) {
        if (errno == EINTR)
            continue;
        if (!_IO_NOT_READY_ERROR)
            return -1;

        #if defined(DEBUG) && defined(DEBUG_STATS)
        ++_st_stat_readv_eagain;
        #endif

        /* Wait until the socket becomes readable */
        if (st_netfd_poll(fd, POLLIN, timeout) < 0)
            return -1;
    }
    
    return n;
}

int st_readv_resid(_st_netfd_t *fd, struct iovec **iov, int *iov_size, st_utime_t timeout)
{
    ssize_t n;
    
    while (*iov_size > 0) {
        if (*iov_size == 1)
            n = read(fd->osfd, (*iov)->iov_base, (*iov)->iov_len);
        else
            n = readv(fd->osfd, *iov, *iov_size);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            if (!_IO_NOT_READY_ERROR)
                return -1;
        } else if (n == 0)
            break;
        else {
            while ((size_t) n >= (*iov)->iov_len) {
                n -= (*iov)->iov_len;
                (*iov)->iov_base = (char *) (*iov)->iov_base + (*iov)->iov_len;
                (*iov)->iov_len = 0;
                (*iov)++;
                (*iov_size)--;
                if (n == 0)
                    break;
            }
            if (*iov_size == 0)
                break;
            (*iov)->iov_base = (char *) (*iov)->iov_base + n;
            (*iov)->iov_len -= n;
        }
        /* Wait until the socket becomes readable */
        if (st_netfd_poll(fd, POLLIN, timeout) < 0)
            return -1;
    }
    
    return 0;
}


ssize_t st_read_fully(_st_netfd_t *fd, void *buf, size_t nbyte, st_utime_t timeout)
{
    size_t resid = nbyte;
    return st_read_resid(fd, buf, &resid, timeout) == 0 ?
    (ssize_t) (nbyte - resid) : -1;
}


int st_write_resid(_st_netfd_t *fd, const void *buf, size_t *resid, st_utime_t timeout)
{
    struct iovec iov, *riov;
    int riov_size, rv;
    
    iov.iov_base = (void *) buf;        /* we promise not to modify buf */
    iov.iov_len = *resid;
    riov = &iov;
    riov_size = 1;
    rv = st_writev_resid(fd, &riov, &riov_size, timeout);
    *resid = iov.iov_len;
    return rv;
}


ssize_t st_write(_st_netfd_t *fd, const void *buf, size_t nbyte, st_utime_t timeout)
{
    size_t resid = nbyte;
    return st_write_resid(fd, buf, &resid, timeout) == 0 ?
    (ssize_t) (nbyte - resid) : -1;
}


ssize_t st_writev(_st_netfd_t *fd, const struct iovec *iov, int iov_size, st_utime_t timeout)
{
    ssize_t n, rv;
    size_t nleft, nbyte;
    int index, iov_cnt;
    struct iovec *tmp_iov;
    struct iovec local_iov[_LOCAL_MAXIOV];
    
    /* Calculate the total number of bytes to be sent */
    nbyte = 0;
    for (index = 0; index < iov_size; index++)
        nbyte += iov[index].iov_len;
    
    rv = (ssize_t)nbyte;
    nleft = nbyte;
    tmp_iov = (struct iovec *) iov;    /* we promise not to modify iov */
    iov_cnt = iov_size;

    #if defined(DEBUG) && defined(DEBUG_STATS)
    ++_st_stat_writev;
    #endif
    
    while (nleft > 0) {
        if (iov_cnt == 1) {
            if (st_write(fd, tmp_iov[0].iov_base, nleft, timeout) != (ssize_t) nleft)
                rv = -1;
            break;
        }
        if ((n = writev(fd->osfd, tmp_iov, iov_cnt)) < 0) {
            if (errno == EINTR)
                continue;
            if (!_IO_NOT_READY_ERROR) {
                rv = -1;
                break;
            }
        } else {
            if ((size_t) n == nleft)
                break;
            nleft -= n;
            /* Find the next unwritten vector */
            n = (ssize_t)(nbyte - nleft);
            for (index = 0; (size_t) n >= iov[index].iov_len; index++)
                n -= iov[index].iov_len;
            
            if (tmp_iov == iov) {
                /* Must copy iov's around */
                if (iov_size - index <= _LOCAL_MAXIOV) {
                    tmp_iov = local_iov;
                } else {
                    tmp_iov = calloc(1, (iov_size - index) * sizeof(struct iovec));
                    if (tmp_iov == NULL) /* GCOVR_EXCL_BR_LINE */
                        return -1; /* GCOVR_EXCL_LINE */
                }
            }
            
            /* Fill in the first partial read */
            tmp_iov[0].iov_base = &(((char *)iov[index].iov_base)[n]);
            tmp_iov[0].iov_len = iov[index].iov_len - n;
            index++;
            /* Copy the remaining vectors */
            for (iov_cnt = 1; index < iov_size; iov_cnt++, index++) {
                tmp_iov[iov_cnt].iov_base = iov[index].iov_base;
                tmp_iov[iov_cnt].iov_len = iov[index].iov_len;
            }
        }

        #if defined(DEBUG) && defined(DEBUG_STATS)
        ++_st_stat_writev_eagain;
        #endif

        /* Wait until the socket becomes writable */
        if (st_netfd_poll(fd, POLLOUT, timeout) < 0) {
            rv = -1;
            break;
        }
    }
    
    if (tmp_iov != iov && tmp_iov != local_iov)
        free(tmp_iov);
    
    return rv;
}


int st_writev_resid(_st_netfd_t *fd, struct iovec **iov, int *iov_size, st_utime_t timeout)
{
    ssize_t n;

    #if defined(DEBUG) && defined(DEBUG_STATS)
    ++_st_stat_writev;
    #endif
    
    while (*iov_size > 0) {
        if (*iov_size == 1)
            n = write(fd->osfd, (*iov)->iov_base, (*iov)->iov_len);
        else
            n = writev(fd->osfd, *iov, *iov_size);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            if (!_IO_NOT_READY_ERROR)
                return -1;
        } else {
            while ((size_t) n >= (*iov)->iov_len) {
                n -= (*iov)->iov_len;
                (*iov)->iov_base = (char *) (*iov)->iov_base + (*iov)->iov_len;
                (*iov)->iov_len = 0;
                (*iov)++;
                (*iov_size)--;
                if (n == 0)
                    break;
            }
            if (*iov_size == 0)
                break;
            (*iov)->iov_base = (char *) (*iov)->iov_base + n;
            (*iov)->iov_len -= n;
        }

        #if defined(DEBUG) && defined(DEBUG_STATS)
        ++_st_stat_writev_eagain;
        #endif

        /* Wait until the socket becomes writable */
        if (st_netfd_poll(fd, POLLOUT, timeout) < 0)
            return -1;
    }
    
    return 0;
}


/*
 * Simple I/O functions for UDP.
 */
int st_recvfrom(_st_netfd_t *fd, void *buf, int len, struct sockaddr *from, int *fromlen, st_utime_t timeout)
{
    int n;

    #if defined(DEBUG) && defined(DEBUG_STATS)
    ++_st_stat_recvfrom;
    #endif

    while ((n = recvfrom(fd->osfd, buf, len, 0, from, (socklen_t *)fromlen)) < 0) {
        if (errno == EINTR)
            continue;
        if (!_IO_NOT_READY_ERROR)
            return -1;

        #if defined(DEBUG) && defined(DEBUG_STATS)
        ++_st_stat_recvfrom_eagain;
        #endif

        /* Wait until the socket becomes readable */
        if (st_netfd_poll(fd, POLLIN, timeout) < 0)
            return -1;
    }
    
    return n;
}


int st_sendto(_st_netfd_t *fd, const void *msg, int len, const struct sockaddr *to, int tolen, st_utime_t timeout)
{
    int n;

    #if defined(DEBUG) && defined(DEBUG_STATS)
    ++_st_stat_sendto;
    #endif
    
    while ((n = sendto(fd->osfd, msg, len, 0, to, tolen)) < 0) {
        if (errno == EINTR)
            continue;
        if (!_IO_NOT_READY_ERROR)
            return -1;

        #if defined(DEBUG) && defined(DEBUG_STATS)
        ++_st_stat_sendto_eagain;
        #endif

        /* Wait until the socket becomes writable */
        if (st_netfd_poll(fd, POLLOUT, timeout) < 0)
            return -1;
    }
    
    return n;
}


int st_recvmsg(_st_netfd_t *fd, struct msghdr *msg, int flags, st_utime_t timeout)
{
    int n;

    #if defined(DEBUG) && defined(DEBUG_STATS)
    ++_st_stat_recvmsg;
    #endif
    
    while ((n = recvmsg(fd->osfd, msg, flags)) < 0) {
        if (errno == EINTR)
            continue;
        if (!_IO_NOT_READY_ERROR)
            return -1;

        #if defined(DEBUG) && defined(DEBUG_STATS)
        ++_st_stat_recvmsg_eagain;
        #endif

        /* Wait until the socket becomes readable */
        if (st_netfd_poll(fd, POLLIN, timeout) < 0)
            return -1;
    }
    
    return n;
}


int st_sendmsg(_st_netfd_t *fd, const struct msghdr *msg, int flags, st_utime_t timeout)
{
    int n;

    #if defined(DEBUG) && defined(DEBUG_STATS)
    ++_st_stat_sendmsg;
    #endif
    
    while ((n = sendmsg(fd->osfd, msg, flags)) < 0) {
        if (errno == EINTR)
            continue;
        if (!_IO_NOT_READY_ERROR)
            return -1;

        #if defined(DEBUG) && defined(DEBUG_STATS)
        ++_st_stat_sendmsg_eagain;
        #endif

        /* Wait until the socket becomes writable */
        if (st_netfd_poll(fd, POLLOUT, timeout) < 0)
            return -1;
    }
    
    return n;
}


/*
 * To open FIFOs or other special files.
 */
#if defined(WIN64)
/* Not supported on Windows yet. */
_st_netfd_t *st_open(const char *path, int oflags, mode_t mode)
{
    (void) path; (void) oflags; (void) mode;
    _st_win64_enosys();
    return NULL;
}
#else
_st_netfd_t *st_open(const char *path, int oflags, mode_t mode)
{
    int osfd, err;
    _st_netfd_t *newfd;
    
    while ((osfd = open(path, oflags | O_NONBLOCK, mode)) < 0) {
        if (errno != EINTR)
            return NULL;
    }
    
    newfd = _st_netfd_new(osfd, 0, 0);
    /* GCOVR_EXCL_START */
    if (!newfd) {
        err = errno;
        close(osfd);
        errno = err;
    }
    /* GCOVR_EXCL_STOP */
    
    return newfd;
}

#endif
