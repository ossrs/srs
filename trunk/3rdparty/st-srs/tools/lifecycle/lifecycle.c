/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2013-2026 The SRS Authors */

/*
 * The ST lifecycle, as SRS runs it: choose the event system, set the
 * primordial stack, st_init, check the descriptor limit, interrupt and join
 * every coroutine blocked in I/O, a condition and a sleep, then st_destroy.
 */

/* For pthread_getattr_np on Linux. */
#define _GNU_SOURCE

#include <pthread.h>
#include <sys/resource.h>
#include <sys/select.h>

#include "tool.h"

/* Long enough that a coroutine still blocked here is a failure, not a slow run. */
#define BLOCK_US (5 * 1000 * 1000)

static int started;

/* Return errno of a call that failed, or 0 when it did not fail. */
static void *result(int r0)
{
    return (void *)(long)(r0 < 0 ? errno : 0);
}

static void *do_accept(void *arg)
{
    started++;
    st_netfd_t client = st_accept((st_netfd_t)arg, NULL, NULL, BLOCK_US);
    if (client) {
        st_netfd_close(client);
        return (void *)0;
    }
    return (void *)(long)errno;
}

static void *do_read(void *arg)
{
    char buf[16];
    started++;
    return result((int)st_read((st_netfd_t)arg, buf, sizeof(buf), BLOCK_US));
}

static void *do_cond(void *arg)
{
    started++;
    return result(st_cond_timedwait((st_cond_t)arg, BLOCK_US));
}

static void *do_sleep(void *arg)
{
    started++;
    return result(st_usleep(BLOCK_US));
}

/*
 * Set the primordial stack to the main thread's real stack. SRS takes the whole
 * RLIMIT_STACK below a local, which is mostly unmapped; with leak detection on,
 * as here and unlike SRS, LeakSanitizer reads that range at exit and crashes.
 */
static int set_primordial_stack(void)
{
    char *top = NULL;
    size_t size = 0;
#ifdef __APPLE__
    pthread_t self = pthread_self();
    top = (char *)pthread_get_stackaddr_np(self);
    size = pthread_get_stacksize_np(self);
#else
    void *addr = NULL;
    pthread_attr_t attr;
    CHECK(pthread_getattr_np(pthread_self(), &attr) == 0);
    CHECK(pthread_attr_getstack(&attr, &addr, &size) == 0);
    pthread_attr_destroy(&attr);
    top = (char *)addr + size;
#endif
    CHECK(top && size > 0);
    st_set_primordial_stack(top, top - size);
    return 0;
}

/* The descriptor limit _st_io_init sets from the limit before st_init. */
static int check_fdlimit(int eventsys, const struct rlimit *before)
{
    struct rlimit after;
    CHECK(getrlimit(RLIMIT_NOFILE, &after) == 0);

    int limit = st_getfdlimit();
    CHECK(limit > 0);

    if (eventsys == ST_EVENTSYS_SELECT) {
        /* select caps the limit at FD_SETSIZE, and raises the soft limit to it. */
        int expected = before->rlim_max > FD_SETSIZE ? FD_SETSIZE : (int)before->rlim_max;
        CHECK(limit == expected);
        CHECK(after.rlim_cur == (rlim_t)expected && after.rlim_max == (rlim_t)expected);
    } else if ((int)before->rlim_max < 0) {
        /* kqueue and epoll have no limit; an infinite rlim_max, as on macOS, uses rlim_cur. */
        CHECK(limit == (int)before->rlim_cur);
        CHECK(after.rlim_cur == before->rlim_cur && after.rlim_max == before->rlim_max);
    } else {
        /* Otherwise the soft limit is raised to rlim_max. */
        CHECK(limit == (int)before->rlim_max);
        CHECK(after.rlim_cur == before->rlim_max && after.rlim_max == before->rlim_max);
    }

    printf("ST: fdlimit=%d, before cur=%llu max=%llu\n", limit,
        (unsigned long long)before->rlim_cur, (unsigned long long)before->rlim_max);
    return 0;
}

/* Block four coroutines, then interrupt and join them; each must fail with EINTR at once. */
static int interrupt_all(void)
{
    int port = 0;
    st_netfd_t lfd = tool_listen(AF_INET, 16, &port);
    CHECK(lfd);

    /* Accept the client here, so the accepting coroutine has nothing pending. */
    st_netfd_t client = tool_connect(AF_INET, port, BLOCK_US);
    CHECK(client);
    st_netfd_t server = st_accept(lfd, NULL, NULL, BLOCK_US);
    CHECK(server);

    st_cond_t cond = st_cond_new();
    CHECK(cond);

    st_thread_t trds[4];
    trds[0] = st_thread_create(do_accept, lfd, 1, 0);
    trds[1] = st_thread_create(do_read, client, 1, 0);
    trds[2] = st_thread_create(do_cond, cond, 1, 0);
    trds[3] = st_thread_create(do_sleep, NULL, 1, 0);
    for (int i = 0; i < 4; i++) {
        CHECK(trds[i]);
    }

    /* Let every coroutine run until it blocks. */
    st_utime_t starttime = st_utime();
    CHECK(st_usleep(1000) == 0);
    CHECK(started == 4);

    for (int i = 0; i < 4; i++) {
        st_thread_interrupt(trds[i]);
    }
    for (int i = 0; i < 4; i++) {
        void *retval = NULL;
        CHECK(st_thread_join(trds[i], &retval) == 0);
        CHECK((long)retval == EINTR);
    }
    CHECK(st_utime() - starttime < BLOCK_US / 10);

    CHECK(st_cond_destroy(cond) == 0);
    CHECK(st_netfd_close(server) == 0);
    CHECK(st_netfd_close(client) == 0);
    CHECK(st_netfd_close(lfd) == 0);
    return 0;
}

int main(int argc, char **argv)
{
    int eventsys = tool_eventsys();
    CHECK(eventsys != -1);

    /* Nothing is chosen before st_set_eventsys. */
    CHECK(st_get_eventsys() == -1);
    CHECK(!strcmp(st_get_eventsys_name(), ""));

    errno = 0;
    CHECK(st_set_eventsys(2) == -1 && errno == EINVAL);
    CHECK(st_get_eventsys() == -1);

    CHECK(st_set_eventsys(eventsys) == 0);
    CHECK(st_get_eventsys() == eventsys);
    CHECK(!strcmp(st_get_eventsys_name(), tool_eventsys_name(eventsys)));

    /* The event system is chosen once per process. */
    errno = 0;
    CHECK(st_set_eventsys(eventsys) == -1 && errno == EBUSY);

    CHECK(set_primordial_stack() == 0);

    struct rlimit before;
    CHECK(getrlimit(RLIMIT_NOFILE, &before) == 0);

    CHECK(st_init() == 0);
    printf("ST: init ok, eventsys=%s\n", st_get_eventsys_name());

    /* After st_init, setting the event system still fails, and nothing changes. */
    errno = 0;
    CHECK(st_set_eventsys(ST_EVENTSYS_DEFAULT) == -1 && errno == EBUSY);
    CHECK(st_get_eventsys() == eventsys);
    CHECK(!strcmp(st_get_eventsys_name(), tool_eventsys_name(eventsys)));

    /* A second st_init is a no-op. */
    CHECK(st_init() == 0);
    CHECK(st_get_eventsys() == eventsys);

    CHECK(check_fdlimit(eventsys, &before) == 0);
    CHECK(interrupt_all() == 0);

    st_destroy();
    printf("lifecycle OK\n");
    return 0;
}
