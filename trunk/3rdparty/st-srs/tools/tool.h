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
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <st.h>

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
    if (eventsys == ST_EVENTSYS_SELECT) {
        return "select";
    }
#if defined(__APPLE__)
    return "kqueue";
#else
    return "epoll";
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

/* Fill addr with the loopback address of family, 127.0.0.1 or ::1, and port. */
static inline socklen_t tool_loopback(int family, int port, struct sockaddr_storage *addr)
{
    memset(addr, 0, sizeof(*addr));
    if (family == AF_INET6) {
        struct sockaddr_in6 *a = (struct sockaddr_in6 *)addr;
        a->sin6_family = AF_INET6;
        a->sin6_port = htons(port);
        a->sin6_addr = in6addr_loopback;
        return sizeof(*a);
    }

    struct sockaddr_in *a = (struct sockaddr_in *)addr;
    a->sin_family = AF_INET;
    a->sin_port = htons(port);
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

    int fd = socket(family, SOCK_STREAM, 0);
    if (fd < 0) {
        return NULL;
    }

    if (bind(fd, (struct sockaddr *)&addr, len) < 0 || listen(fd, backlog) < 0
        || getsockname(fd, (struct sockaddr *)&addr, &len) < 0) {
        int err = errno;
        close(fd);
        errno = err;
        return NULL;
    }

    st_netfd_t stfd = st_netfd_open_socket(fd);
    if (!stfd) {
        int err = errno;
        close(fd);
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

    int fd = socket(family, SOCK_STREAM, 0);
    if (fd < 0) {
        return NULL;
    }

    st_netfd_t stfd = st_netfd_open_socket(fd);
    if (!stfd) {
        int err = errno;
        close(fd);
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

#endif /* ST_TOOL_H */
