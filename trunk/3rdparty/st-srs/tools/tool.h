/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2013-2026 The SRS Authors */

/*
 * Shared helpers for the integration tools. Each tool includes this file
 * with -I.., and links obj/libst.a through st.h, as SRS does.
 */

#ifndef ST_TOOL_H
#define ST_TOOL_H

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif

/* On native Windows, st.h brings the Winsock 2 headers. */
#include <st.h>
#if defined(_WIN32)
#include <afunix.h>
#endif

/* Print the failing check and its line, then return 1 from the caller. */
#define CHECK(cond) do { \
    if (!(cond)) { \
        printf("ST: FAILED %s at line %d, errno=%d\n", #cond, __LINE__, errno); \
        return 1; \
    } \
} while (0)

/*
 * The event system the runner asks for in ST_TOOL_EVENTSYS: select (the
 * default when unset) or alt. Returns -1 for any other value.
 */
static inline int tool_eventsys(void)
{
#ifdef _MSC_VER
#pragma warning(suppress: 4996) /* getenv is not deprecated for this use. */
#endif
    const char *v = getenv("ST_TOOL_EVENTSYS");
    if (!v || !strcmp(v, "select")) {
        return ST_EVENTSYS_SELECT;
    }
    if (!strcmp(v, "alt")) {
        return ST_EVENTSYS_ALT;
    }
    return -1;
}

/* The name st_get_eventsys_name() reports for an event system. */
static inline const char *tool_eventsys_name(int eventsys)
{
#if defined(_WIN32)
    /* Both event systems are WSAPoll on native Windows. */
    (void)eventsys;
    return "wsapoll";
#else
    if (eventsys == ST_EVENTSYS_SELECT) {
        return "select";
    }
#if defined(__APPLE__)
    return "kqueue";
#else
    return "epoll";
#endif
#endif
}

/*
 * Set the event system from ST_TOOL_EVENTSYS, then st_init, and check that
 * ST reports the event system that was asked for.
 */
static inline int tool_init(void)
{
    int eventsys = tool_eventsys();
    CHECK(eventsys != -1);
    CHECK(st_set_eventsys(eventsys) == 0);
    CHECK(st_init() == 0);
    CHECK(st_get_eventsys() == eventsys);
    CHECK(!strcmp(st_get_eventsys_name(), tool_eventsys_name(eventsys)));
    return 0;
}

/* Close a socket that no st_netfd_t owns: closesocket on native Windows, close elsewhere. */
static inline void tool_close_socket(int fd)
{
#if defined(_WIN32)
    closesocket(fd);
#else
    close(fd);
#endif
}

/* Fill addr with the loopback address of family, 127.0.0.1 or ::1, and port. */
static inline socklen_t tool_loopback(int family, int port, struct sockaddr_storage *addr)
{
    memset(addr, 0, sizeof(*addr));
    if (family == AF_INET6) {
        struct sockaddr_in6 *a = (struct sockaddr_in6 *)addr;
        a->sin6_family = AF_INET6;
        a->sin6_port = htons((unsigned short)port);
        a->sin6_addr = in6addr_loopback;
        return sizeof(*a);
    }

    struct sockaddr_in *a = (struct sockaddr_in *)addr;
    a->sin_family = AF_INET;
    a->sin_port = htons((unsigned short)port);
    a->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    return sizeof(*a);
}

/*
 * A TCP listener on the loopback address of family, AF_INET or AF_INET6, at
 * port 0. Sets *port to the port the OS chose. Returns NULL with errno set on
 * failure; for ::1, EADDRNOTAVAIL or EAFNOSUPPORT means no IPv6 here.
 */
static inline st_netfd_t tool_listen(int family, int backlog, int *port)
{
    struct sockaddr_storage addr;
    socklen_t len = tool_loopback(family, 0, &addr);

    int fd = (int)socket(family, SOCK_STREAM, 0);
    if (fd < 0) {
        return NULL;
    }

    if (bind(fd, (struct sockaddr *)&addr, len) < 0 || listen(fd, backlog) < 0
        || getsockname(fd, (struct sockaddr *)&addr, &len) < 0) {
        int err = errno;
        tool_close_socket(fd);
        errno = err;
        return NULL;
    }

    st_netfd_t stfd = st_netfd_open_socket(fd);
    if (!stfd) {
        int err = errno;
        tool_close_socket(fd);
        errno = err;
        return NULL;
    }

    if (family == AF_INET6) {
        *port = ntohs(((struct sockaddr_in6 *)&addr)->sin6_port);
    } else {
        *port = ntohs(((struct sockaddr_in *)&addr)->sin_port);
    }
    return stfd;
}

/*
 * A TCP client connected to the loopback address of family at port, with
 * st_connect and timeout. Returns NULL with errno set on failure.
 */
static inline st_netfd_t tool_connect(int family, int port, st_utime_t timeout)
{
    struct sockaddr_storage addr;
    socklen_t len = tool_loopback(family, port, &addr);

    int fd = (int)socket(family, SOCK_STREAM, 0);
    if (fd < 0) {
        return NULL;
    }

    st_netfd_t stfd = st_netfd_open_socket(fd);
    if (!stfd) {
        int err = errno;
        tool_close_socket(fd);
        errno = err;
        return NULL;
    }

    if (st_connect(stfd, (struct sockaddr *)&addr, len, timeout) < 0) {
        int err = errno;
        st_netfd_close(stfd);
        errno = err;
        return NULL;
    }
    return stfd;
}

#if defined(_WIN32)
/*
 * A connected pair of stream sockets of family, AF_INET over 127.0.0.1 or
 * AF_UNIX at a path in the temp folder, made with bind, listen, connect and
 * accept. fds[0] is the accepted end and fds[1] the client. Returns -1 with
 * errno set on failure.
 */
static inline int tool_stream_pair(int family, int fds[2])
{
    struct sockaddr_storage addr;
    int len;
    if (family == AF_UNIX) {
        static int seq = 0;
        char dir[MAX_PATH];
        struct sockaddr_un *a = (struct sockaddr_un *)&addr;
        memset(&addr, 0, sizeof(addr));
        a->sun_family = AF_UNIX;
        if (!GetTempPathA(sizeof(dir), dir)) {
            errno = ENOENT;
            return -1;
        }
        snprintf(a->sun_path, sizeof(a->sun_path), "%sst-tool-%lu-%d.sock", dir, GetCurrentProcessId(), seq++);
        DeleteFileA(a->sun_path);
        len = (int)sizeof(*a);
    } else {
        len = (int)tool_loopback(AF_INET, 0, &addr);
    }

    SOCKET l = INVALID_SOCKET, c = INVALID_SOCKET, s = INVALID_SOCKET;
    if ((l = socket(family, SOCK_STREAM, 0)) == INVALID_SOCKET
        || bind(l, (struct sockaddr *)&addr, len) || listen(l, 1)
        || getsockname(l, (struct sockaddr *)&addr, &len)
        || (c = socket(family, SOCK_STREAM, 0)) == INVALID_SOCKET
        || connect(c, (struct sockaddr *)&addr, len)
        || (s = accept(l, NULL, NULL)) == INVALID_SOCKET) {
        if (c != INVALID_SOCKET) {
            closesocket(c);
        }
        s = INVALID_SOCKET;
    }
    if (l != INVALID_SOCKET) {
        closesocket(l);
    }
    if (family == AF_UNIX) {
        DeleteFileA(((struct sockaddr_un *)&addr)->sun_path);
    }
    if (s == INVALID_SOCKET) {
        errno = EIO;
        return -1;
    }

    fds[0] = (int)s;
    fds[1] = (int)c;
    return 0;
}
#endif

/*
 * A one-way channel, fds[0] to read and fds[1] to write: pipe(2), or on
 * native Windows, where ST takes sockets only, a loopback TCP pair (D26).
 */
static inline int tool_pipe(int fds[2])
{
#if defined(_WIN32)
    return tool_stream_pair(AF_INET, fds);
#else
    return pipe(fds);
#endif
}

#if defined(_WIN32)
/*
 * A pair of UDP sockets on 127.0.0.1, each connected to the other, so send
 * and recv carry datagrams between them. Returns -1 with errno set on failure.
 */
static inline int tool_dgram_pair(int fds[2])
{
    struct sockaddr_storage a0, a1;
    int l0 = (int)tool_loopback(AF_INET, 0, &a0), l1 = (int)tool_loopback(AF_INET, 0, &a1);
    SOCKET s0 = socket(AF_INET, SOCK_DGRAM, 0), s1 = socket(AF_INET, SOCK_DGRAM, 0);
    if (s0 == INVALID_SOCKET || s1 == INVALID_SOCKET
        || bind(s0, (struct sockaddr *)&a0, l0) || bind(s1, (struct sockaddr *)&a1, l1)
        || getsockname(s0, (struct sockaddr *)&a0, &l0) || getsockname(s1, (struct sockaddr *)&a1, &l1)
        || connect(s0, (struct sockaddr *)&a1, l1) || connect(s1, (struct sockaddr *)&a0, l0)) {
        if (s0 != INVALID_SOCKET) {
            closesocket(s0);
        }
        if (s1 != INVALID_SOCKET) {
            closesocket(s1);
        }
        errno = EIO;
        return -1;
    }

    fds[0] = (int)s0;
    fds[1] = (int)s1;
    return 0;
}
#endif

/*
 * socketpair(2). Native Windows has none, and its AF_UNIX is stream only, so
 * there an AF_UNIX stream pair is connected through a path in the temp folder
 * (D26), and a datagram pair is a connected loopback UDP pair (D28).
 */
static inline int tool_socketpair(int domain, int type, int protocol, int fds[2])
{
#if defined(_WIN32)
    if (domain != AF_UNIX || (type != SOCK_STREAM && type != SOCK_DGRAM) || protocol != 0) {
        errno = EINVAL;
        return -1;
    }
    if (type == SOCK_DGRAM) {
        return tool_dgram_pair(fds);
    }
    return tool_stream_pair(AF_UNIX, fds);
#else
    return socketpair(domain, type, protocol, fds);
#endif
}

/*
 * write(2) to a descriptor from tool_pipe or tool_socketpair. On native
 * Windows it is a socket: send, with EAGAIN when it would block.
 */
static inline ssize_t tool_write(int fd, const void *buf, size_t n)
{
#if defined(_WIN32)
    int r = send((SOCKET)fd, (const char *)buf, (int)n, 0);
    if (r == SOCKET_ERROR) {
        errno = (WSAGetLastError() == WSAEWOULDBLOCK) ? EAGAIN : EIO;
        return -1;
    }
    return r;
#else
    return write(fd, buf, n);
#endif
}

#endif /* ST_TOOL_H */
