/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2013-2026 The SRS Authors */

/*
 * Polling: st_poll on several descriptors at once, pipes, a Unix socketpair
 * and a TCP connection, where only the ready ones are reported; a worker
 * that waits on all of them in a loop; st_netfd_poll for POLLIN and POLLOUT;
 * and both calls timing out and being interrupted.
 */

#include "tool.h"

/* On native Windows, st.h brings struct pollfd from Winsock 2, and fcntl is only for a check skipped there. */
#ifndef _WIN32
#include <fcntl.h>
#include <poll.h>
#endif
#include <sys/types.h>

/* Long enough that a coroutine still blocked here is a failure, not a slow run. */
#define BLOCK_US (5 * 1000 * 1000)

/* A timeout that is expected to expire. */
#define SHORT_US (5 * 1000)

/* A channel: the end that is polled, and the peer that writes to it. */
struct channel {
    st_netfd_t rd;
    st_netfd_t wr;
};

/* The channels polled together: two pipes, a Unix socketpair and a TCP connection. */
#define NB_CHANNELS 4
static struct channel channels[NB_CHANNELS];

static int open_pipe(struct channel *c)
{
    int fds[2];
    CHECK(tool_pipe(fds) == 0);
    CHECK((c->rd = st_netfd_open(fds[0])) != NULL);
    CHECK((c->wr = st_netfd_open(fds[1])) != NULL);
    return 0;
}

static int open_socketpair(struct channel *c)
{
    int fds[2];
    CHECK(tool_socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
    CHECK((c->rd = st_netfd_open_socket(fds[0])) != NULL);
    CHECK((c->wr = st_netfd_open_socket(fds[1])) != NULL);
    return 0;
}

static int open_tcp(struct channel *c)
{
    int port = 0;
    st_netfd_t lfd;
    CHECK((lfd = tool_listen(AF_INET, 16, &port)) != NULL);
    CHECK((c->wr = tool_connect(AF_INET, port, BLOCK_US)) != NULL);
    CHECK((c->rd = st_accept(lfd, NULL, NULL, BLOCK_US)) != NULL);
    CHECK(st_netfd_close(lfd) == 0);
    return 0;
}

static int open_channels(void)
{
    CHECK(open_pipe(&channels[0]) == 0);
    CHECK(open_pipe(&channels[1]) == 0);
    CHECK(open_socketpair(&channels[2]) == 0);
    CHECK(open_tcp(&channels[3]) == 0);
    return 0;
}

/* Close every channel; nothing is left registered, so each close succeeds. */
static int close_channels(void)
{
    for (int i = 0; i < NB_CHANNELS; i++) {
        CHECK(st_netfd_close(channels[i].rd) == 0);
        CHECK(st_netfd_close(channels[i].wr) == 0);
    }
    return 0;
}

/* Fill pds with the read end of every channel, for POLLIN. */
static void poll_all(struct pollfd *pds)
{
    for (int i = 0; i < NB_CHANNELS; i++) {
        pds[i].fd = st_netfd_fileno(channels[i].rd);
        pds[i].events = POLLIN;
        pds[i].revents = 0;
    }
}

/* Read the one byte c waits with on channel i. */
static int read_byte(int i, char c)
{
    char ch = 0;
    CHECK(st_read(channels[i].rd, &ch, 1, BLOCK_US) == 1);
    CHECK(ch == c);
    return 0;
}

/*
 * With data on some channels already, st_poll returns at once, counts only
 * the ready descriptors, and sets revents on each entry: POLLIN where data
 * waits, POLLOUT where there is room, and 0 on the quiet ones.
 */
static int ready_only(void)
{
    CHECK(st_write(channels[1].wr, "b", 1, BLOCK_US) == 1);
    CHECK(st_write(channels[3].wr, "d", 1, BLOCK_US) == 1);

    /*
     * A pipe or socketpair write is readable at once, but loopback TCP may
     * deliver it a little later, as on macOS; wait until it has arrived.
     */
    CHECK(st_netfd_poll(channels[3].rd, POLLIN, BLOCK_US) == 0);

    struct pollfd pds[NB_CHANNELS + 2];
    poll_all(pds);
    /* A pipe write end with room. */
    pds[4].fd = st_netfd_fileno(channels[0].wr);
    pds[4].events = POLLOUT;
    pds[4].revents = 0;
    /* The TCP client end, with room. */
    pds[5].fd = st_netfd_fileno(channels[3].wr);
    pds[5].events = POLLOUT;
    pds[5].revents = 0;

    st_utime_t start = st_utime();
    CHECK(st_poll(pds, NB_CHANNELS + 2, BLOCK_US) == 4);
    CHECK(st_utime() - start < BLOCK_US / 2);
    CHECK(pds[0].revents == 0);
    CHECK(pds[1].revents == POLLIN);
    CHECK(pds[2].revents == 0);
    CHECK(pds[3].revents == POLLIN);
    CHECK(pds[4].revents == POLLOUT);
    CHECK(pds[5].revents == POLLOUT);

    /* The same TCP socket asked for both: data waits and there is room. */
    struct pollfd pd = {st_netfd_fileno(channels[3].rd), POLLIN | POLLOUT, 0};
    CHECK(st_poll(&pd, 1, BLOCK_US) == 1);
    CHECK(pd.revents == (POLLIN | POLLOUT));

    CHECK(read_byte(1, 'b') == 0);
    CHECK(read_byte(3, 'd') == 0);

    /* Drained, only the room is left. */
    pd.revents = 0;
    CHECK(st_poll(&pd, 1, BLOCK_US) == 1);
    CHECK(pd.revents == POLLOUT);
    return 0;
}

/*
 * A worker that waits on every channel at once, with no timeout, and keeps
 * one pollfd array for its whole loop. It reads one byte from each channel
 * that is ready, and quits after the byte 'q'.
 */
#define NB_MESSAGES 16
struct worker {
    int polls;
    int ready[NB_MESSAGES + 1];
    int order[NB_MESSAGES + 1];
    char bytes[NB_MESSAGES + 1];
    int nb_messages;
};

static void *worker_cycle(void *arg)
{
    struct worker *w = (struct worker *)arg;
    struct pollfd pds[NB_CHANNELS];
    poll_all(pds);

    while (w->polls <= NB_MESSAGES) {
        int n = st_poll(pds, NB_CHANNELS, ST_UTIME_NO_TIMEOUT);
        if (n <= 0) {
            return (void *)1;
        }
        w->ready[w->polls++] = n;

        for (int i = 0; i < NB_CHANNELS; i++) {
            if (!pds[i].revents) {
                continue;
            }
            if (pds[i].revents != POLLIN || w->nb_messages > NB_MESSAGES) {
                return (void *)2;
            }
            char ch;
            if (st_read(channels[i].rd, &ch, 1, BLOCK_US) != 1) {
                return (void *)3;
            }
            w->order[w->nb_messages] = i;
            w->bytes[w->nb_messages++] = ch;
            if (ch == 'q') {
                return NULL;
            }
        }
    }
    return (void *)4;
}

/* The worker wakes for each byte, on the channel it came on, in order. */
static int worker(void)
{
    struct worker w;
    memset(&w, 0, sizeof(w));
    st_thread_t t;
    CHECK((t = st_thread_create(worker_cycle, &w, 1, 0)) != NULL);

    /* With nothing to do, the worker waits. */
    CHECK(st_usleep(1000) == 0);
    CHECK(w.polls == 0);

    for (int k = 0; k < NB_MESSAGES; k++) {
        int i = (k * 3 + k / NB_CHANNELS) % NB_CHANNELS;
        char ch = (char)('a' + k);
        CHECK(st_write(channels[i].wr, &ch, 1, BLOCK_US) == 1);
        /* Wait until the worker reads it, so one byte is pending at a time. */
        for (int n = 0; n < 1000 && w.nb_messages < k + 1; n++) {
            CHECK(st_usleep(100) == 0);
        }
        CHECK(w.nb_messages == k + 1);
        CHECK(w.order[k] == i && w.bytes[k] == ch);
    }

    CHECK(st_write(channels[2].wr, "q", 1, BLOCK_US) == 1);
    void *ret = (void *)-1;
    CHECK(st_thread_join(t, &ret) == 0);
    CHECK(ret == NULL);
    CHECK(w.polls == NB_MESSAGES + 1 && w.nb_messages == NB_MESSAGES + 1);
    for (int k = 0; k < w.polls; k++) {
        CHECK(w.ready[k] == 1);
    }
    CHECK(w.order[NB_MESSAGES] == 2 && w.bytes[NB_MESSAGES] == 'q');
    return 0;
}

/*
 * With nothing ready, st_poll returns 0 after the timeout and leaves errno
 * alone, as poll(2) does (sched.c:115). Only st_netfd_poll, built on it,
 * turns that 0 into -1 with ETIME (io.c:253).
 */
static int timeout(void)
{
    struct pollfd pds[NB_CHANNELS];
    poll_all(pds);

    /* The timeout counts from the clock reading this yield takes. */
    st_utime_t start = st_utime();
    st_thread_yield();
    errno = 0;
    CHECK(st_poll(pds, NB_CHANNELS, SHORT_US) == 0);
    CHECK(errno == 0);
    CHECK(st_utime() - start >= SHORT_US);

    start = st_utime();
    st_thread_yield();
    errno = 0;
    CHECK(st_netfd_poll(channels[2].rd, POLLIN, SHORT_US) == -1 && errno == ETIME);
    CHECK(st_utime() - start >= SHORT_US);
    return 0;
}

/* A coroutine that waits in st_poll on every channel, or in st_netfd_poll on one. */
struct poller {
    int netfd;
    int r0;
    int err;
    /* The result of a second st_poll with a short timeout, or 1 if not run. */
    int r1;
    int again;
};

static void *poller_cycle(void *arg)
{
    struct poller *p = (struct poller *)arg;
    struct pollfd pds[NB_CHANNELS];
    poll_all(pds);

    errno = 0;
    if (p->netfd) {
        p->r0 = st_netfd_poll(channels[3].rd, POLLIN, BLOCK_US);
    } else {
        p->r0 = st_poll(pds, NB_CHANNELS, BLOCK_US);
    }
    p->err = errno;

    p->r1 = 1;
    if (p->again) {
        p->r1 = st_poll(pds, NB_CHANNELS, SHORT_US);
    }
    return NULL;
}

/* Both calls, waiting, are interrupted with EINTR, as a coroutine stop does. */
static int interrupt(void)
{
    for (int netfd = 0; netfd < 2; netfd++) {
        struct poller p;
        memset(&p, 0, sizeof(p));
        p.netfd = netfd;
        st_thread_t t;
        CHECK((t = st_thread_create(poller_cycle, &p, 1, 0)) != NULL);
        CHECK(st_usleep(1000) == 0);

        st_utime_t start = st_utime();
        st_thread_interrupt(t);
        CHECK(st_thread_join(t, NULL) == 0);
        CHECK(st_utime() - start < BLOCK_US / 2);
        CHECK(p.r0 == -1 && p.err == EINTR);
    }

    /*
     * Interrupted before it runs, st_poll fails with EINTR at once, without
     * waiting; the interrupt is used up, so the next st_poll times out.
     */
    struct poller p;
    memset(&p, 0, sizeof(p));
    p.again = 1;
    st_thread_t t;
    CHECK((t = st_thread_create(poller_cycle, &p, 1, 0)) != NULL);
    st_thread_interrupt(t);
    CHECK(st_thread_join(t, NULL) == 0);
    CHECK(p.r0 == -1 && p.err == EINTR);
    CHECK(p.r1 == 0);
    return 0;
}

/* A coroutine that writes one byte to a channel after a short sleep. */
struct late_writer {
    st_netfd_t fd;
    st_utime_t delay;
};

static void *late_writer_cycle(void *arg)
{
    struct late_writer *w = (struct late_writer *)arg;
    if (st_usleep(w->delay) != 0) {
        return (void *)1;
    }
    if (st_write(w->fd, "x", 1, BLOCK_US) != 1) {
        return (void *)2;
    }
    return NULL;
}

/* A coroutine that reads total bytes from a pipe it drains. */
struct drainer {
    st_netfd_t fd;
    int total;
    int got;
};

static void *drainer_cycle(void *arg)
{
    struct drainer *d = (struct drainer *)arg;
    char buf[8192];
    while (d->got < d->total) {
        ssize_t n = st_read(d->fd, buf, sizeof(buf), BLOCK_US);
        if (n <= 0) {
            return (void *)1;
        }
        d->got += (int)n;
    }
    return NULL;
}

/* st_netfd_poll waits for one descriptor to be readable or writable. */
static int netfd_poll(void)
{
    /* POLLIN wakes when a byte arrives later, and the read then gets it. */
    for (int i = 0; i < NB_CHANNELS; i++) {
        struct late_writer w = {channels[i].wr, 1000};
        st_thread_t t;
        CHECK((t = st_thread_create(late_writer_cycle, &w, 1, 0)) != NULL);
        st_utime_t start = st_utime();
        CHECK(st_netfd_poll(channels[i].rd, POLLIN, BLOCK_US) == 0);
        CHECK(st_utime() - start < BLOCK_US / 2);
        void *ret = (void *)-1;
        CHECK(st_thread_join(t, &ret) == 0);
        CHECK(ret == NULL);
        CHECK(read_byte(i, 'x') == 0);
    }

    /* POLLOUT on sockets and a pipe with room returns at once. */
    CHECK(st_netfd_poll(channels[0].wr, POLLOUT, SHORT_US) == 0);
    CHECK(st_netfd_poll(channels[2].wr, POLLOUT, SHORT_US) == 0);
    CHECK(st_netfd_poll(channels[3].rd, POLLOUT, SHORT_US) == 0);
    CHECK(st_netfd_poll(channels[3].rd, POLLIN | POLLOUT, SHORT_US) == 0);

    /* A full pipe is not writable; it is again once a reader drains it. */
    struct channel c;
    CHECK(open_pipe(&c) == 0);
    int wfd = st_netfd_fileno(c.wr);
#ifndef _WIN32
    /* Windows does not support this check: it cannot read back whether a socket is non-blocking. */
    CHECK(fcntl(wfd, F_GETFL) & O_NONBLOCK);
#endif
    char buf[4096];
    memset(buf, 'f', sizeof(buf));
    int filled = 0;
    for (;;) {
        ssize_t n = tool_write(wfd, buf, sizeof(buf));
        if (n < 0) {
            CHECK(errno == EAGAIN || errno == EWOULDBLOCK);
            break;
        }
        filled += (int)n;
    }
    CHECK(filled > 0);

    st_utime_t start = st_utime();
    st_thread_yield();
    errno = 0;
    CHECK(st_netfd_poll(c.wr, POLLOUT, SHORT_US) == -1 && errno == ETIME);
    CHECK(st_utime() - start >= SHORT_US);

    struct drainer d = {c.rd, filled, 0};
    st_thread_t t;
    CHECK((t = st_thread_create(drainer_cycle, &d, 1, 0)) != NULL);
    CHECK(st_netfd_poll(c.wr, POLLOUT, BLOCK_US) == 0);
    void *ret = (void *)-1;
    CHECK(st_thread_join(t, &ret) == 0);
    CHECK(ret == NULL && d.got == filled);

    /* At EOF the read end is readable, and the read returns 0. */
    CHECK(st_netfd_close(c.wr) == 0);
    CHECK(st_netfd_poll(c.rd, POLLIN, BLOCK_US) == 0);
    CHECK(st_read(c.rd, buf, sizeof(buf), BLOCK_US) == 0);
    CHECK(st_netfd_close(c.rd) == 0);
    return 0;
}

static int run(void)
{
    CHECK(open_channels() == 0);
    CHECK(ready_only() == 0);
    CHECK(worker() == 0);
    CHECK(timeout() == 0);
    CHECK(interrupt() == 0);
    CHECK(netfd_poll() == 0);
    CHECK(close_channels() == 0);
    return 0;
}

int main(int argc, char **argv)
{
    CHECK(tool_init() == 0);
    CHECK(run() == 0);

    printf("poll OK\n");
    return 0;
}
