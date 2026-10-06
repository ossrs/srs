/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2013-2026 The SRS Authors */

/*
 * Load: many loopback TCP connections open at once, each with its own client
 * and server coroutine exchanging echoed messages; repeated create and exit
 * cycles of many coroutines with different stack sizes, so stacks are
 * reused; and many coroutines contending for one mutex and one cond.
 *
 * Usage: ./stress [count]
 * count is the number of connections and of coroutines in each cycle and in
 * the contention, 200 by default so a run fits in 200 ms. A larger count is
 * for manual runs; it needs about two descriptors per connection.
 */

#include "tool.h"

/* On native Windows, st.h brings TCP_NODELAY and struct iovec from Winsock 2, and there is no rlimit. */
#ifndef _WIN32
#include <netinet/tcp.h>
#include <sys/resource.h>
#include <sys/uio.h>
#endif

/* Long enough that a coroutine still blocked here is a failure, not a slow run. */
#define BLOCK_US (5 * 1000 * 1000)

#define DEFAULT_COUNT 200

/* Messages per connection, and the largest payload. */
#define MESSAGES 8
#define MAX_PAYLOAD 4096
#define HEADER 4

/* Create and exit cycles, and the bytes each coroutine writes on its stack. */
#define CYCLES 20
#define STACK_FILL 4096

/* Rounds through the mutex, and passes of the token around the cond. */
#define LOCK_ROUNDS 10
#define RING_ROUNDS 3

/* A coroutine fails by returning the line it failed at. */
#define FAIL() return (void *)(long)__LINE__

/* Join t, and check it returned want; a failed coroutine prints its line. */
static int join(st_thread_t t, void *want)
{
    void *ret = (void *)-1;
    CHECK(st_thread_join(t, &ret) == 0);
    if (ret != want) {
        printf("ST: FAILED coroutine returned %ld, want %ld\n", (long)ret, (long)want);
        return 1;
    }
    return 0;
}

static int count = DEFAULT_COUNT;

/* The payload of message seq on connection id: its size and its bytes. */
static int payload_size(int id, int seq)
{
    return 1 + (id * 7919 + seq * 3571) % MAX_PAYLOAD;
}

static void payload_fill(int id, int seq, char *buf, int size)
{
    for (int i = 0; i < size; i++) {
        buf[i] = (char)(id * 31 + seq * 7 + i);
    }
}

static int nodelay(st_netfd_t fd)
{
    int one = 1;
    return setsockopt(st_netfd_fileno(fd), IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
}

static struct {
    st_netfd_t listener;
    int port;
    st_thread_t *handlers;
    int accepted;
    st_cond_t accept_cond;
    /* Clients wait on start_cond until every connection is up. */
    int connected;
    st_cond_t start_cond;
    int open;
    int peak;
    int server_msgs;
    long server_bytes;
    int client_msgs;
    long client_bytes;
} c;

/* Echo each message, a 4-byte size and its payload, until the client closes. */
static void *handler(void *arg)
{
    st_netfd_t conn = (st_netfd_t)arg;
    char buf[HEADER + MAX_PAYLOAD];
    if (++c.open > c.peak) {
        c.peak = c.open;
    }

    for (;;) {
        ssize_t n = st_read_fully(conn, buf, HEADER, BLOCK_US);
        if (n == 0) {
            break; /* The client closed. */
        }
        if (n != HEADER) {
            FAIL();
        }
        uint32_t v;
        memcpy(&v, buf, HEADER);
        int size = (int)ntohl(v);
        if (size <= 0 || size > MAX_PAYLOAD) {
            FAIL();
        }
        if (st_read_fully(conn, buf + HEADER, size, BLOCK_US) != size) {
            FAIL();
        }
        if (st_write(conn, buf, HEADER + size, BLOCK_US) != HEADER + size) {
            FAIL();
        }
        c.server_msgs++;
        c.server_bytes += HEADER + size;
    }

    c.open--;
    if (st_netfd_close(conn) != 0) {
        FAIL();
    }
    return NULL;
}

/* Accept every client, each connection in its own coroutine. */
static void *acceptor(void *arg)
{
    while (c.accepted < count) {
        st_netfd_t conn = st_accept(c.listener, NULL, NULL, BLOCK_US);
        if (!conn || nodelay(conn) != 0) {
            FAIL();
        }
        st_thread_t t = st_thread_create(handler, conn, 1, 0);
        if (!t) {
            FAIL();
        }
        c.handlers[c.accepted++] = t;
        st_cond_signal(c.accept_cond);
    }
    return NULL;
}

static void *client(void *arg)
{
    int id = (int)(long)arg;
    char sent[HEADER + MAX_PAYLOAD], got[HEADER + MAX_PAYLOAD];

    st_netfd_t fd = tool_connect(AF_INET, c.port, BLOCK_US);
    if (!fd || nodelay(fd) != 0) {
        FAIL();
    }

    /* Exchange nothing until every connection is up, so all are open at once. */
    if (++c.connected == count) {
        st_cond_broadcast(c.start_cond);
    }
    while (c.connected < count) {
        if (st_cond_timedwait(c.start_cond, BLOCK_US) != 0) {
            FAIL();
        }
    }

    for (int seq = 0; seq < MESSAGES; seq++) {
        int size = payload_size(id, seq);
        uint32_t v = htonl((uint32_t)size);
        memcpy(sent, &v, HEADER);
        payload_fill(id, seq, sent + HEADER, size);

        /* The header and the payload go out as two iovecs in one write. */
        struct iovec iov[2] = {{sent, HEADER}, {sent + HEADER, size}};
        if (st_writev(fd, iov, 2, BLOCK_US) != HEADER + size) {
            FAIL();
        }
        memset(got, 0, HEADER + size);
        if (st_read_fully(fd, got, HEADER + size, BLOCK_US) != HEADER + size) {
            FAIL();
        }
        if (memcmp(sent, got, HEADER + size) != 0) {
            FAIL();
        }
        c.client_msgs++;
        c.client_bytes += HEADER + size;
    }

    if (st_netfd_close(fd) != 0) {
        FAIL();
    }
    return NULL;
}

static int connections(void)
{
    memset(&c, 0, sizeof(c));
    CHECK((c.handlers = calloc(count, sizeof(st_thread_t))) != NULL);
    CHECK((c.accept_cond = st_cond_new()) != NULL);
    CHECK((c.start_cond = st_cond_new()) != NULL);
    CHECK((c.listener = tool_listen(AF_INET, count, &c.port)) != NULL);

    st_thread_t a, *clients;
    CHECK((clients = calloc(count, sizeof(st_thread_t))) != NULL);
    CHECK((a = st_thread_create(acceptor, NULL, 1, 0)) != NULL);

    /*
     * Start the clients in batches the accept queue holds, waiting for each
     * batch to be accepted, so no connect waits for a dropped SYN to retry.
     */
    for (int i = 0; i < count; i++) {
        CHECK((clients[i] = st_thread_create(client, (void *)(long)i, 1, 0)) != NULL);
        if ((i + 1) % 64 == 0 || i == count - 1) {
            while (c.accepted < i + 1) {
                CHECK(st_cond_timedwait(c.accept_cond, BLOCK_US) == 0);
            }
        }
    }

    CHECK(join(a, NULL) == 0);
    for (int i = 0; i < count; i++) {
        CHECK(join(clients[i], NULL) == 0);
    }
    for (int i = 0; i < count; i++) {
        CHECK(join(c.handlers[i], NULL) == 0);
    }
    CHECK(st_netfd_close(c.listener) == 0);
    CHECK(st_cond_destroy(c.accept_cond) == 0);
    CHECK(st_cond_destroy(c.start_cond) == 0);
    free(clients);
    free(c.handlers);

    /* Every connection was open at once, and exchanged all its data. */
    CHECK(c.accepted == count);
    CHECK(c.peak == count);
    CHECK(c.open == 0);
    CHECK(c.client_msgs == count * MESSAGES);
    CHECK(c.server_msgs == count * MESSAGES);
    long bytes = 0;
    for (int id = 0; id < count; id++) {
        for (int seq = 0; seq < MESSAGES; seq++) {
            bytes += HEADER + payload_size(id, seq);
        }
    }
    CHECK(c.client_bytes == bytes);
    CHECK(c.server_bytes == bytes);
    return 0;
}

static int detached_done;

/*
 * Fill STACK_FILL bytes of this coroutine's stack with its own pattern,
 * yield so the others run, and check the pattern is still there.
 */
static void *cycler(void *arg)
{
    long id = (long)arg;
    volatile char buf[STACK_FILL];
    for (int i = 0; i < STACK_FILL; i++) {
        buf[i] = (char)(id + i);
    }
    st_thread_yield();
    for (int i = 0; i < STACK_FILL; i++) {
        if (buf[i] != (char)(id + i)) {
            FAIL();
        }
    }
    /* A detached coroutine has no joiner, so it counts itself. */
    if (id % 4 == 3) {
        detached_done++;
    }
    /* Every other one exits with st_thread_exit instead of returning. */
    if (id % 2) {
        st_thread_exit((void *)(id + 1000));
    }
    return (void *)(id + 1000);
}

static int cycles(void)
{
    /* The default stack, and two custom sizes. */
    static const int sizes[] = {0, 32 * 1024, 128 * 1024};
    st_thread_t *threads;
    CHECK((threads = calloc(count, sizeof(st_thread_t))) != NULL);

    for (int cycle = 0; cycle < CYCLES; cycle++) {
        detached_done = 0;
        int detached = 0;
        for (long id = 0; id < count; id++) {
            int joinable = (id % 4 != 3);
            int size = sizes[(id + cycle) % 3];
            CHECK((threads[id] = st_thread_create(cycler, (void *)id, joinable, size)) != NULL);
            detached += !joinable;
        }
        for (long id = 0; id < count; id++) {
            if (id % 4 != 3) {
                CHECK(join(threads[id], (void *)(id + 1000)) == 0);
            }
        }
        /* The joins switch enough for every detached coroutine to finish. */
        while (detached_done < detached) {
            st_thread_yield();
        }
        CHECK(detached_done == detached);
    }

    free(threads);
    return 0;
}

static struct {
    st_mutex_t mutex;
    st_cond_t cond;
    int inside;
    int counter;
    int turn;
    int passes;
    int last;
} k;

/*
 * Take the mutex LOCK_ROUNDS times, yielding inside it, then pass a token
 * around every coroutine through one cond, RING_ROUNDS times.
 */
static void *contender(void *arg)
{
    int id = (int)(long)arg;

    for (int i = 0; i < LOCK_ROUNDS; i++) {
        if (st_mutex_lock(k.mutex) != 0) {
            FAIL();
        }
        if (++k.inside != 1) {
            FAIL();
        }
        int v = k.counter;
        st_thread_yield();
        k.counter = v + 1;
        k.inside--;
        if (st_mutex_unlock(k.mutex) != 0) {
            FAIL();
        }
    }

    for (int i = 0; i < RING_ROUNDS; i++) {
        while (k.turn != id) {
            if (st_cond_timedwait(k.cond, BLOCK_US) != 0) {
                FAIL();
            }
        }
        /* The token comes from the coroutine before this one. */
        if (k.last != (id + count - 1) % count) {
            FAIL();
        }
        k.last = id;
        k.passes++;
        k.turn = (id + 1) % count;
        if (st_cond_broadcast(k.cond) != 0) {
            FAIL();
        }
    }
    return NULL;
}

static int contention(void)
{
    memset(&k, 0, sizeof(k));
    k.last = count - 1;
    CHECK((k.mutex = st_mutex_new()) != NULL);
    CHECK((k.cond = st_cond_new()) != NULL);

    st_thread_t *threads;
    CHECK((threads = calloc(count, sizeof(st_thread_t))) != NULL);
    for (long id = 0; id < count; id++) {
        CHECK((threads[id] = st_thread_create(contender, (void *)id, 1, 0)) != NULL);
    }
    for (int id = 0; id < count; id++) {
        CHECK(join(threads[id], NULL) == 0);
    }
    free(threads);

    CHECK(k.counter == count * LOCK_ROUNDS);
    CHECK(k.inside == 0);
    CHECK(k.passes == count * RING_ROUNDS);
    CHECK(k.turn == 0);
    CHECK(st_mutex_destroy(k.mutex) == 0);
    CHECK(st_cond_destroy(k.cond) == 0);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc > 1) {
        count = atoi(argv[1]);
    }
    CHECK(count >= 2);

    /*
     * Each connection has a client and a server descriptor. Raise the soft
     * limit for a large count before st_init, which reads it.
     */
    int need = 2 * count + 32;
#ifndef _WIN32
    /* Windows does not support this: it has no rlimit, and ST allows 2^24 sockets there. */
    struct rlimit rl;
    CHECK(getrlimit(RLIMIT_NOFILE, &rl) == 0);
    if (rl.rlim_cur < (rlim_t)need) {
        rl.rlim_cur = need;
        setrlimit(RLIMIT_NOFILE, &rl);
    }
#endif

    CHECK(tool_init() == 0);
    if (st_getfdlimit() < need) {
        printf("ST: FAILED count %d needs %d descriptors, the limit is %d\n", count, need, st_getfdlimit());
        return 1;
    }

    CHECK(connections() == 0);
    CHECK(cycles() == 0);
    CHECK(contention() == 0);

    printf("stress OK\n");
    return 0;
}
