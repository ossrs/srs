/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2013-2026 The SRS Authors */

/*
 * TCP, as SRS serves and connects: two coroutines accept on one listener,
 * each connection gets its own coroutine, and clients send framed messages,
 * a header and a payload, that the server reads with st_read_fully and echoes
 * with st_writev. Socket buffers are small, so writes are partial, and some
 * writes have more than 16 iovecs. The other read and write calls, the
 * residual counts at EOF and on a timeout, connect and read timeouts, the
 * netfd specific data, st_netfd_free and st_netfd_close run over IPv4 and
 * IPv6.
 */

#include "tool.h"

#include <fcntl.h>
#include <netinet/tcp.h>
#include <sys/uio.h>

/* Long enough that a coroutine still blocked here is a failure, not a slow run. */
#define BLOCK_US (5 * 1000 * 1000)

/* A timeout that is expected to expire. */
#define SHORT_US (5 * 1000)

/*
 * A small socket buffer. In the echo only the send buffers are small, so a
 * message does not fit and writes are partial; a small receive buffer there
 * would stall loopback TCP, whose segments are much bigger than the window.
 */
#define SMALL_BUF 4096

/* The echo workload: clients, messages per client, and the largest payload. */
#define CLIENTS 8
#define MESSAGES 24
#define MAX_PAYLOAD 24000
/* The most iovecs in one write, more than ST's 16 local ones. */
#define MAX_IOV 40
#define HEADER 8

/* Set SO_SNDBUF or SO_RCVBUF to size. */
static int set_buf(int fd, int opt, int size)
{
    return setsockopt(fd, SOL_SOCKET, opt, &size, sizeof(size));
}

/*
 * An echo socket: a small send buffer, and TCP_NODELAY as SRS sets with
 * tcp_nodelay, so each echo goes out without waiting for a delayed ACK.
 */
static int tune(int fd)
{
    int one = 1;
    if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one)) < 0) {
        return -1;
    }
    return set_buf(fd, SO_SNDBUF, SMALL_BUF);
}

/* The payload of message seq from client id: its size and its bytes. */
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

static void header_fill(char *h, int size, int seq)
{
    uint32_t v = htonl((uint32_t)size);
    memcpy(h, &v, 4);
    v = htonl((uint32_t)seq);
    memcpy(h + 4, &v, 4);
}

static void header_parse(const char *h, int *size, int *seq)
{
    uint32_t v;
    memcpy(&v, h, 4);
    *size = (int)ntohl(v);
    memcpy(&v, h + 4, 4);
    *seq = (int)ntohl(v);
}

/* Split buf into n iovecs after iov[0], as even as the size allows. */
static int split(struct iovec *iov, char *buf, int size, int n)
{
    if (n > size) {
        n = size;
    }
    int off = 0;
    for (int i = 0; i < n; i++) {
        int len = (i == n - 1) ? size - off : size / n;
        iov[i].iov_base = buf + off;
        iov[i].iov_len = len;
        off += len;
    }
    return n;
}

/* The server side of one connection, kept as the netfd specific data. */
struct conn_ctx {
    int msgs;
    long bytes;
};

static struct {
    st_netfd_t listener;
    int accepted;
    int accepts[2];
    st_thread_t handlers[CLIENTS];
    int family;
    int port;
    /* Added by the connection destructor, so they count only closed ones. */
    int freed;
    int server_msgs;
    long server_bytes;
    int client_msgs;
    long client_bytes;
} e;

static void conn_ctx_free(void *arg)
{
    struct conn_ctx *ctx = (struct conn_ctx *)arg;
    e.freed++;
    e.server_msgs += ctx->msgs;
    e.server_bytes += ctx->bytes;
    free(ctx);
}

/* Read each framed message with st_read_fully and echo it with st_writev. */
static void *handler(void *arg)
{
    st_netfd_t conn = (st_netfd_t)arg;
    struct conn_ctx *ctx = (struct conn_ctx *)st_netfd_getspecific(conn);
    char *buf = malloc(MAX_PAYLOAD);
    void *ret = NULL;
    if (!buf) {
        return (void *)1;
    }

    for (;;) {
        char h[HEADER];
        ssize_t n = st_read_fully(conn, h, HEADER, BLOCK_US);
        if (n == 0) {
            break; /* The client closed. */
        }
        int size, seq;
        if (n != HEADER) {
            ret = (void *)2;
            break;
        }
        header_parse(h, &size, &seq);
        if (size <= 0 || size > MAX_PAYLOAD || st_read_fully(conn, buf, size, BLOCK_US) != size) {
            ret = (void *)3;
            break;
        }

        struct iovec iov[1 + MAX_IOV];
        iov[0].iov_base = h;
        iov[0].iov_len = HEADER;
        int cnt = 1 + split(iov + 1, buf, size, 1 + seq % MAX_IOV);
        if (st_writev(conn, iov, cnt, BLOCK_US) != HEADER + size) {
            ret = (void *)4;
            break;
        }
        ctx->msgs++;
        ctx->bytes += HEADER + size;
    }

    free(buf);
    if (st_netfd_getspecific(conn) != ctx) {
        ret = (void *)5;
    }
    /* Closing runs the destructor, which adds this connection's counts. */
    if (st_netfd_close(conn) != 0) {
        ret = (void *)6;
    }
    return ret;
}

/* Accept until every client is in; the other acceptor is then interrupted. */
static void *acceptor(void *arg)
{
    int id = (int)(long)arg;
    while (e.accepted < CLIENTS) {
        struct sockaddr_storage addr;
        int addrlen = sizeof(addr);
        st_netfd_t conn = st_accept(e.listener, (struct sockaddr *)&addr, &addrlen, BLOCK_US);
        if (!conn) {
            return (errno == EINTR && e.accepted == CLIENTS) ? NULL : (void *)1;
        }
        if (addr.ss_family != e.family) {
            return (void *)2;
        }
        /* An accepted connection is tuned through its descriptor. */
        if (tune(st_netfd_fileno(conn)) != 0) {
            return (void *)3;
        }

        struct conn_ctx *ctx = calloc(1, sizeof(*ctx));
        if (!ctx) {
            return (void *)4;
        }
        st_netfd_setspecific(conn, ctx, conn_ctx_free);

        st_thread_t t = st_thread_create(handler, conn, 1, 0);
        if (!t) {
            return (void *)5;
        }
        e.handlers[e.accepted++] = t;
        e.accepts[id]++;
    }
    return NULL;
}

/* Read n bytes into iov with st_readv, moving past what each call read. */
static int readv_all(st_netfd_t fd, struct iovec *iov, int cnt)
{
    while (cnt > 0) {
        ssize_t n = st_readv(fd, iov, cnt, BLOCK_US);
        if (n <= 0) {
            return -1;
        }
        while (cnt > 0 && (size_t)n >= iov->iov_len) {
            n -= iov->iov_len;
            iov++;
            cnt--;
        }
        if (cnt > 0) {
            iov->iov_base = (char *)iov->iov_base + n;
            iov->iov_len -= n;
        }
    }
    return 0;
}

/* Send one message with a write call chosen by seq. */
static int client_send(st_netfd_t fd, char *h, char *buf, int size, int seq)
{
    struct iovec iovs[1 + MAX_IOV];
    iovs[0].iov_base = h;
    iovs[0].iov_len = HEADER;
    int cnt = 1 + split(iovs + 1, buf, size, 1 + (seq * 5) % MAX_IOV);

    if (seq % 4 == 2) {
        /* The header with st_write, the payload with st_write_resid. */
        if (st_write(fd, h, HEADER, BLOCK_US) != HEADER) {
            return -1;
        }
        size_t resid = size;
        if (st_write_resid(fd, buf, &resid, BLOCK_US) != 0 || resid != 0) {
            return -1;
        }
        return 0;
    }
    if (seq % 4 == 3) {
        struct iovec *iov = iovs;
        int iov_size = cnt;
        if (st_writev_resid(fd, &iov, &iov_size, BLOCK_US) != 0 || iov_size != 0 || iov != iovs + cnt) {
            return -1;
        }
        return 0;
    }
    return st_writev(fd, iovs, cnt, BLOCK_US) == HEADER + size ? 0 : -1;
}

/* Read the echo of one message with a read call chosen by seq. */
static int client_recv(st_netfd_t fd, char *h, char *buf, int size, int seq)
{
    switch (seq % 4) {
    case 0:
        if (st_read_fully(fd, h, HEADER, BLOCK_US) != HEADER || st_read_fully(fd, buf, size, BLOCK_US) != size) {
            return -1;
        }
        return 0;
    case 1: {
        if (st_read_fully(fd, h, HEADER, BLOCK_US) != HEADER) {
            return -1;
        }
        for (int off = 0; off < size;) {
            ssize_t n = st_read(fd, buf + off, size - off, BLOCK_US);
            if (n <= 0) {
                return -1;
            }
            off += n;
        }
        return 0;
    }
    case 2: {
        size_t resid = HEADER;
        if (st_read_resid(fd, h, &resid, BLOCK_US) != 0 || resid != 0) {
            return -1;
        }
        resid = size;
        if (st_read_resid(fd, buf, &resid, BLOCK_US) != 0 || resid != 0) {
            return -1;
        }
        return 0;
    }
    default: {
        /* The header and payload in one call each of st_readv_resid and st_readv. */
        struct iovec iovs[3];
        iovs[0].iov_base = h;
        iovs[0].iov_len = HEADER;
        int cnt = 1 + split(iovs + 1, buf, size, 2);
        if (seq % 8 == 3) {
            struct iovec *iov = iovs;
            int iov_size = cnt;
            if (st_readv_resid(fd, &iov, &iov_size, BLOCK_US) != 0 || iov_size != 0 || iov != iovs + cnt) {
                return -1;
            }
            return 0;
        }
        return readv_all(fd, iovs, cnt);
    }
    }
}

static void *client(void *arg)
{
    int id = (int)(long)arg;
    char *sent = malloc(MAX_PAYLOAD);
    char *got = malloc(MAX_PAYLOAD);
    void *ret = NULL;
    st_netfd_t fd = NULL;
    if (!sent || !got) {
        ret = (void *)1;
        goto out;
    }
    if (!(fd = tool_connect(e.family, e.port, BLOCK_US))) {
        ret = (void *)2;
        goto out;
    }
    if (tune(st_netfd_fileno(fd)) != 0) {
        ret = (void *)3;
        goto out;
    }

    for (int seq = 0; seq < MESSAGES; seq++) {
        int size = payload_size(id, seq);
        char h[HEADER], gh[HEADER];
        header_fill(h, size, seq);
        payload_fill(id, seq, sent, size);
        memset(got, 0, size);

        if (client_send(fd, h, sent, size, seq) != 0) {
            ret = (void *)4;
            break;
        }
        if (client_recv(fd, gh, got, size, seq) != 0) {
            ret = (void *)5;
            break;
        }
        if (memcmp(h, gh, HEADER) != 0 || memcmp(sent, got, size) != 0) {
            ret = (void *)6;
            break;
        }
        e.client_msgs++;
        e.client_bytes += HEADER + size;
    }

out:
    if (fd && st_netfd_close(fd) != 0) {
        ret = (void *)7;
    }
    free(sent);
    free(got);
    return ret;
}

static int echo(void)
{
    int family = e.family;
    memset(&e, 0, sizeof(e));
    e.family = family;
    CHECK((e.listener = tool_listen(family, 128, &e.port)) != NULL);
    /* A no-op on Linux and macOS, so two acceptors still share the listener. */
    CHECK(st_netfd_serialize_accept(e.listener) == 0);

    st_thread_t acceptors[2], clients[CLIENTS];
    for (int i = 0; i < 2; i++) {
        CHECK((acceptors[i] = st_thread_create(acceptor, (void *)(long)i, 1, 0)) != NULL);
    }
    for (int i = 0; i < CLIENTS; i++) {
        CHECK((clients[i] = st_thread_create(client, (void *)(long)i, 1, 0)) != NULL);
    }

    void *ret;
    for (int i = 0; i < CLIENTS; i++) {
        ret = (void *)-1;
        CHECK(st_thread_join(clients[i], &ret) == 0);
        CHECK(ret == NULL);
    }
    CHECK(e.accepted == CLIENTS);
    CHECK(e.accepts[0] + e.accepts[1] == CLIENTS);
    for (int i = 0; i < CLIENTS; i++) {
        ret = (void *)-1;
        CHECK(st_thread_join(e.handlers[i], &ret) == 0);
        CHECK(ret == NULL);
    }

    /* One acceptor still waits in st_accept, the other may have returned. */
    for (int i = 0; i < 2; i++) {
        st_thread_interrupt(acceptors[i]);
        ret = (void *)-1;
        CHECK(st_thread_join(acceptors[i], &ret) == 0);
        CHECK(ret == NULL);
    }
    CHECK(st_netfd_close(e.listener) == 0);

    /* Every message and byte on both sides, and every connection closed. */
    CHECK(e.client_msgs == CLIENTS * MESSAGES);
    CHECK(e.freed == CLIENTS);
    CHECK(e.server_msgs == CLIENTS * MESSAGES);
    CHECK(e.server_bytes == e.client_bytes);
    long bytes = 0;
    for (int id = 0; id < CLIENTS; id++) {
        for (int seq = 0; seq < MESSAGES; seq++) {
            bytes += HEADER + payload_size(id, seq);
        }
    }
    CHECK(e.client_bytes == bytes);
    return 0;
}

/*
 * A connected pair on loopback: a client with a small send buffer, and the
 * server side accepted with the small receive buffer of the listener.
 */
static int pair(int family, st_netfd_t *c, st_netfd_t *s)
{
    int port;
    st_netfd_t l;
    CHECK((l = tool_listen(family, 8, &port)) != NULL);
    CHECK(set_buf(st_netfd_fileno(l), SO_RCVBUF, SMALL_BUF) == 0);
    CHECK((*c = tool_connect(family, port, BLOCK_US)) != NULL);
    CHECK(set_buf(st_netfd_fileno(*c), SO_SNDBUF, SMALL_BUF) == 0);
    CHECK((*s = st_accept(l, NULL, NULL, BLOCK_US)) != NULL);
    CHECK(st_netfd_close(l) == 0);
    return 0;
}

/* Read exactly n bytes, and then nothing more is there. */
static int drain(st_netfd_t fd, size_t n)
{
    char *buf = malloc(n + 1);
    CHECK(buf != NULL);
    ssize_t got = st_read_fully(fd, buf, n, BLOCK_US);
    char extra;
    errno = 0;
    ssize_t more = st_read(fd, &extra, 1, ST_UTIME_NO_WAIT);
    int err = errno;
    free(buf);
    CHECK(got == (ssize_t)n);
    CHECK(more == -1 && err == ETIME);
    return 0;
}

/* The residual counts: short reads at EOF, and partial writes on a timeout. */
static int resid(int family)
{
    st_netfd_t c, s;
    char buf[100];

    /* At EOF, st_read_fully is short and st_read_resid keeps what is left. */
    CHECK(pair(family, &c, &s) == 0);
    CHECK(st_write(c, "0123456789", 10, BLOCK_US) == 10);
    CHECK(st_netfd_close(c) == 0);
    CHECK(st_read_fully(s, buf, 6, BLOCK_US) == 6);
    CHECK(!memcmp(buf, "012345", 6));
    size_t left = sizeof(buf);
    CHECK(st_read_resid(s, buf, &left, BLOCK_US) == 0);
    CHECK(left == sizeof(buf) - 4 && !memcmp(buf, "6789", 4));
    CHECK(st_read_fully(s, buf, sizeof(buf), BLOCK_US) == 0);
    CHECK(st_read(s, buf, sizeof(buf), BLOCK_US) == 0);
    CHECK(st_netfd_close(s) == 0);

    /* At EOF, st_readv_resid moves past the filled iovecs into the partial one. */
    CHECK(pair(family, &c, &s) == 0);
    CHECK(st_write(c, "0123456789", 10, BLOCK_US) == 10);
    CHECK(st_netfd_close(c) == 0);
    struct iovec iovs[3] = {{buf, 4}, {buf + 4, 4}, {buf + 8, 92}};
    struct iovec *iov = iovs;
    int iov_size = 3;
    CHECK(st_readv_resid(s, &iov, &iov_size, BLOCK_US) == 0);
    CHECK(iov_size == 1 && iov == iovs + 2);
    CHECK(iovs[0].iov_len == 0 && iovs[1].iov_len == 0);
    CHECK(iovs[2].iov_len == 90 && iovs[2].iov_base == buf + 10);
    CHECK(!memcmp(buf, "0123456789", 10));
    CHECK(st_netfd_close(s) == 0);

    /* The peer does not read, so a big write times out part way. */
    size_t big = 1024 * 1024;
    char *data = calloc(1, big);
    CHECK(data != NULL);
    CHECK(pair(family, &c, &s) == 0);
    left = big;
    errno = 0;
    int r = st_write_resid(c, data, &left, SHORT_US);
    int err = errno;
    CHECK(r == -1 && err == ETIME);
    CHECK(left > 0 && left < big);
    CHECK(drain(s, big - left) == 0);

    /* The same with st_writev_resid and more than 16 iovecs. */
    struct iovec wiovs[MAX_IOV];
    size_t chunk = SMALL_BUF;
    for (int i = 0; i < MAX_IOV; i++) {
        wiovs[i].iov_base = data + i * chunk;
        wiovs[i].iov_len = chunk;
    }
    iov = wiovs;
    iov_size = MAX_IOV;
    errno = 0;
    r = st_writev_resid(c, &iov, &iov_size, SHORT_US);
    err = errno;
    CHECK(r == -1 && err == ETIME);
    CHECK(iov_size > 0 && iov_size <= MAX_IOV && iov == wiovs + MAX_IOV - iov_size);
    size_t unsent = 0;
    for (int i = 0; i < iov_size; i++) {
        unsent += iov[i].iov_len;
    }
    CHECK(unsent > 0 && unsent < chunk * MAX_IOV && unsent <= chunk * iov_size);
    CHECK(drain(s, chunk * MAX_IOV - unsent) == 0);

    /* st_write and st_writev fail as a whole on a timeout. */
    errno = 0;
    CHECK(st_write(c, data, big, SHORT_US) == -1 && errno == ETIME);
    free(data);
    CHECK(st_netfd_close(c) == 0);
    CHECK(st_netfd_close(s) == 0);
    return 0;
}

/* st_read and st_connect time out with ETIME. */
static int timeouts(int family)
{
    st_netfd_t c, s;
    char buf[16];
    CHECK(pair(family, &c, &s) == 0);
    st_utime_t start = st_utime();
    errno = 0;
    CHECK(st_read(s, buf, sizeof(buf), SHORT_US) == -1 && errno == ETIME);
    CHECK(st_utime() - start >= SHORT_US);
    CHECK(st_netfd_close(c) == 0);
    CHECK(st_netfd_close(s) == 0);

    /*
     * A listener with backlog 1 that never accepts: clients fill its queue,
     * and the next connect gets no answer. How many fit depends on the OS.
     */
    int port;
    st_netfd_t l;
    CHECK((l = tool_listen(family, 1, &port)) != NULL);
    st_netfd_t clients[16];
    int n = 0, queued = 0, err = 0;
    for (; n < 16; n++) {
        struct sockaddr_storage addr;
        socklen_t len = tool_loopback(family, port, &addr);
        int fd = socket(family, SOCK_STREAM, 0);
        CHECK(fd >= 0);
        CHECK((clients[n] = st_netfd_open_socket(fd)) != NULL);
        errno = 0;
        if (st_connect(clients[n], (struct sockaddr *)&addr, len, 2 * SHORT_US) != 0) {
            err = errno;
            n++;
            break;
        }
        queued++;
    }
    for (int i = 0; i < n; i++) {
        CHECK(st_netfd_close(clients[i]) == 0);
    }
    CHECK(st_netfd_close(l) == 0);
    CHECK(queued >= 1 && queued < 16);
    CHECK(err == ETIME);
    return 0;
}

static int dtor_calls;
static void *dtor_last;

static void dtor(void *v)
{
    dtor_calls++;
    dtor_last = v;
}

static int is_open(int fd)
{
    return fcntl(fd, F_GETFD) != -1;
}

/* The netfd specific data, and st_netfd_free against st_netfd_close. */
static int netfd(int family)
{
    st_netfd_t c, s;
    int v1, v2, v3;
    CHECK(pair(family, &c, &s) == 0);

    dtor_calls = 0;
    CHECK(st_netfd_getspecific(s) == NULL);
    st_netfd_setspecific(s, &v1, dtor);
    CHECK(st_netfd_getspecific(s) == &v1);
    /* The same value again is not freed. */
    st_netfd_setspecific(s, &v1, dtor);
    CHECK(dtor_calls == 0);
    /* A new value frees the old one, and NULL frees the last. */
    st_netfd_setspecific(s, &v2, dtor);
    CHECK(dtor_calls == 1 && dtor_last == &v1);
    CHECK(st_netfd_getspecific(s) == &v2);
    st_netfd_setspecific(s, NULL, NULL);
    CHECK(dtor_calls == 2 && dtor_last == &v2);
    CHECK(st_netfd_getspecific(s) == NULL);

    /* st_netfd_free runs the destructor and keeps the OS descriptor open. */
    int osfd = st_netfd_fileno(s);
    CHECK(osfd >= 0 && is_open(osfd));
    st_netfd_setspecific(s, &v3, dtor);
    st_netfd_free(s);
    CHECK(dtor_calls == 3 && dtor_last == &v3);
    CHECK(is_open(osfd));

    /* The descriptor still works when it is opened again. */
    CHECK((s = st_netfd_open_socket(osfd)) != NULL);
    CHECK(st_netfd_fileno(s) == osfd);
    CHECK(st_netfd_getspecific(s) == NULL);
    char buf[4];
    CHECK(st_write(c, "ping", 4, BLOCK_US) == 4);
    CHECK(st_read_fully(s, buf, 4, BLOCK_US) == 4 && !memcmp(buf, "ping", 4));

    /* st_netfd_close runs the destructor and closes it, so the peer reads EOF. */
    st_netfd_setspecific(s, &v1, dtor);
    CHECK(st_netfd_close(s) == 0);
    CHECK(dtor_calls == 4 && dtor_last == &v1);
    CHECK(!is_open(osfd) && errno == EBADF);
    CHECK(st_read(c, buf, sizeof(buf), BLOCK_US) == 0);
    CHECK(st_netfd_close(c) == 0);
    return 0;
}

static int run(int family)
{
    e.family = family;
    CHECK(echo() == 0);
    CHECK(resid(family) == 0);
    CHECK(timeouts(family) == 0);
    CHECK(netfd(family) == 0);
    return 0;
}

int main(int argc, char **argv)
{
    CHECK(tool_init() == 0);

    CHECK(run(AF_INET) == 0);

    /* IPv6, unless ::1 cannot be bound here. */
    int port;
    st_netfd_t l = tool_listen(AF_INET6, 1, &port);
    if (!l && (errno == EADDRNOTAVAIL || errno == EAFNOSUPPORT)) {
        printf("SKIP ipv6\n");
    } else {
        CHECK(l != NULL);
        CHECK(st_netfd_close(l) == 0);
        CHECK(run(AF_INET6) == 0);
    }

    printf("tcp OK\n");
    return 0;
}
