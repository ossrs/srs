/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2013-2026 The SRS Authors */

/*
 * UDP, as SRS serves WebRTC and SRT: one server coroutine on one socket
 * echoes datagrams from many clients, and tells the peers apart by their
 * address. The echo runs once with st_recvfrom and st_sendto, and once with
 * st_recvmsg and st_sendmsg over several iovecs. Datagram boundaries,
 * truncation, and receive timeouts are checked too, over IPv4 and IPv6.
 */

#include "tool.h"

#include <sys/uio.h>

/* Long enough that a coroutine still blocked here is a failure, not a slow run. */
#define BLOCK_US (5 * 1000 * 1000)

/* A timeout that is expected to expire. */
#define SHORT_US (5 * 1000)

/* The echo workload: clients, datagrams per client, and the largest payload. */
#define CLIENTS 8
#define MESSAGES 32
#define MAX_PAYLOAD 4000
#define HEADER 8
/* The most iovecs for one datagram. */
#define MAX_IOV 6

/* Which calls the echo uses. */
#define MODE_TO 0
#define MODE_MSG 1

/* A UDP socket bound to the loopback address of family at port 0. */
static st_netfd_t udp_bind(int family, struct sockaddr_storage *addr, int *addrlen)
{
    socklen_t len = tool_loopback(family, 0, addr);
    int fd = socket(family, SOCK_DGRAM, 0);
    if (fd < 0) {
        return NULL;
    }

    if (bind(fd, (struct sockaddr *)addr, len) < 0 || getsockname(fd, (struct sockaddr *)addr, &len) < 0) {
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
    *addrlen = (int)len;
    return stfd;
}

/* Whether two addresses are the same family, address and port. */
static int same_addr(const struct sockaddr_storage *a, const struct sockaddr_storage *b)
{
    if (a->ss_family != b->ss_family) {
        return 0;
    }
    if (a->ss_family == AF_INET6) {
        const struct sockaddr_in6 *x = (const struct sockaddr_in6 *)a;
        const struct sockaddr_in6 *y = (const struct sockaddr_in6 *)b;
        return x->sin6_port == y->sin6_port && !memcmp(&x->sin6_addr, &y->sin6_addr, sizeof(x->sin6_addr));
    }
    const struct sockaddr_in *x = (const struct sockaddr_in *)a;
    const struct sockaddr_in *y = (const struct sockaddr_in *)b;
    return x->sin_port == y->sin_port && x->sin_addr.s_addr == y->sin_addr.s_addr;
}

/* The sockaddr size of family. */
static int addr_size(int family)
{
    return family == AF_INET6 ? (int)sizeof(struct sockaddr_in6) : (int)sizeof(struct sockaddr_in);
}

/* The payload of datagram seq from client id: its size and its bytes. */
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

static int payload_check(int id, int seq, const char *buf, int size)
{
    for (int i = 0; i < size; i++) {
        if (buf[i] != (char)(id * 31 + seq * 7 + i)) {
            return -1;
        }
    }
    return 0;
}

static void header_fill(char *h, int id, int seq)
{
    uint32_t v = htonl((uint32_t)id);
    memcpy(h, &v, 4);
    v = htonl((uint32_t)seq);
    memcpy(h + 4, &v, 4);
}

static void header_parse(const char *h, int *id, int *seq)
{
    uint32_t v;
    memcpy(&v, h, 4);
    *id = (int)ntohl(v);
    memcpy(&v, h + 4, 4);
    *seq = (int)ntohl(v);
}

/* Split buf into n iovecs, as even as the size allows. */
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

/* A peer of the server, known only by its address, as SRS's UDP mux does. */
struct peer {
    struct sockaddr_storage addr;
    int id;
    int msgs;
    long bytes;
};

static struct {
    int family;
    int mode;
    st_netfd_t server;
    struct sockaddr_storage server_addr;
    int server_addrlen;
    struct peer peers[CLIENTS];
    int npeers;
    int server_msgs;
    long server_bytes;
    int client_msgs;
    long client_bytes;
} e;

/* Find the peer with addr, or add it as client id. NULL if a peer is wrong. */
static struct peer *peer_of(const struct sockaddr_storage *addr, int id)
{
    for (int i = 0; i < e.npeers; i++) {
        if (same_addr(&e.peers[i].addr, addr)) {
            /* The address alone tells which client sent it. */
            return e.peers[i].id == id ? &e.peers[i] : NULL;
        }
    }
    if (e.npeers == CLIENTS) {
        return NULL;
    }
    struct peer *p = &e.peers[e.npeers++];
    memset(p, 0, sizeof(*p));
    p->addr = *addr;
    p->id = id;
    return p;
}

/* Receive one datagram into buf, and its sender into from. */
static int server_recv(char *buf, struct sockaddr_storage *from, int *fromlen)
{
    *fromlen = sizeof(*from);
    if (e.mode == MODE_TO) {
        return st_recvfrom(e.server, buf, HEADER + MAX_PAYLOAD, (struct sockaddr *)from, fromlen, BLOCK_US);
    }

    /* The header and the payload in separate iovecs, the payload in three. */
    struct iovec iov[4];
    iov[0].iov_base = buf;
    iov[0].iov_len = HEADER;
    split(iov + 1, buf + HEADER, MAX_PAYLOAD, 3);
    struct msghdr msg;
    memset(&msg, 0, sizeof(msg));
    msg.msg_name = from;
    msg.msg_namelen = sizeof(*from);
    msg.msg_iov = iov;
    msg.msg_iovlen = 4;
    int n = st_recvmsg(e.server, &msg, 0, BLOCK_US);
    *fromlen = (int)msg.msg_namelen;
    if (n >= 0 && (msg.msg_flags & MSG_TRUNC)) {
        errno = EMSGSIZE;
        return -1;
    }
    return n;
}

/* Echo n bytes of buf back to from. */
static int server_send(char *buf, int n, int seq, const struct sockaddr_storage *from, int fromlen)
{
    if (e.mode == MODE_TO) {
        return st_sendto(e.server, buf, n, (const struct sockaddr *)from, fromlen, BLOCK_US);
    }

    struct iovec iov[MAX_IOV];
    struct msghdr msg;
    memset(&msg, 0, sizeof(msg));
    msg.msg_name = (void *)from;
    msg.msg_namelen = fromlen;
    msg.msg_iov = iov;
    msg.msg_iovlen = split(iov, buf, n, 1 + seq % MAX_IOV);
    return st_sendmsg(e.server, &msg, 0, BLOCK_US);
}

/* Echo every datagram to its sender, until it is interrupted. */
static void *server(void *arg)
{
    char *buf = malloc(HEADER + MAX_PAYLOAD);
    void *ret = NULL;
    if (!buf) {
        return (void *)1;
    }

    for (;;) {
        struct sockaddr_storage from;
        int fromlen;
        int n = server_recv(buf, &from, &fromlen);
        if (n < 0) {
            ret = (errno == EINTR && e.server_msgs == CLIENTS * MESSAGES) ? NULL : (void *)2;
            break;
        }
        if (n < HEADER || fromlen != addr_size(e.family) || from.ss_family != e.family) {
            ret = (void *)3;
            break;
        }

        int id, seq;
        header_parse(buf, &id, &seq);
        struct peer *p = peer_of(&from, id);
        if (!p || n != HEADER + payload_size(id, seq) || payload_check(id, seq, buf + HEADER, n - HEADER) != 0) {
            ret = (void *)4;
            break;
        }
        if (server_send(buf, n, seq, &from, fromlen) != n) {
            ret = (void *)5;
            break;
        }
        p->msgs++;
        p->bytes += n;
        e.server_msgs++;
        e.server_bytes += n;
    }

    free(buf);
    return ret;
}

/* Send one datagram to the server, the header and payload in several iovecs. */
static int client_send(st_netfd_t fd, char *h, char *buf, int size, int seq)
{
    if (e.mode == MODE_TO) {
        char *d = malloc(HEADER + size);
        if (!d) {
            return -1;
        }
        memcpy(d, h, HEADER);
        memcpy(d + HEADER, buf, size);
        int n = st_sendto(fd, d, HEADER + size, (struct sockaddr *)&e.server_addr, e.server_addrlen, BLOCK_US);
        free(d);
        return n == HEADER + size ? 0 : -1;
    }

    struct iovec iov[1 + MAX_IOV];
    iov[0].iov_base = h;
    iov[0].iov_len = HEADER;
    struct msghdr msg;
    memset(&msg, 0, sizeof(msg));
    msg.msg_name = &e.server_addr;
    msg.msg_namelen = e.server_addrlen;
    msg.msg_iov = iov;
    msg.msg_iovlen = 1 + split(iov + 1, buf, size, 1 + (seq * 5) % MAX_IOV);
    return st_sendmsg(fd, &msg, 0, BLOCK_US) == HEADER + size ? 0 : -1;
}

/* Receive the echo, and check it came from the server. */
static int client_recv(st_netfd_t fd, char *h, char *buf, int size)
{
    struct sockaddr_storage from;
    int fromlen = sizeof(from);
    int n;

    if (e.mode == MODE_TO) {
        char *d = malloc(HEADER + MAX_PAYLOAD);
        if (!d) {
            return -1;
        }
        n = st_recvfrom(fd, d, HEADER + MAX_PAYLOAD, (struct sockaddr *)&from, &fromlen, BLOCK_US);
        if (n == HEADER + size) {
            memcpy(h, d, HEADER);
            memcpy(buf, d + HEADER, size);
        }
        free(d);
    } else {
        /* Exactly the expected size, in two iovecs. */
        struct iovec iov[3];
        iov[0].iov_base = h;
        iov[0].iov_len = HEADER;
        int cnt = 1 + split(iov + 1, buf, size, 2);
        struct msghdr msg;
        memset(&msg, 0, sizeof(msg));
        msg.msg_name = &from;
        msg.msg_namelen = sizeof(from);
        msg.msg_iov = iov;
        msg.msg_iovlen = cnt;
        n = st_recvmsg(fd, &msg, 0, BLOCK_US);
        fromlen = (int)msg.msg_namelen;
        if (msg.msg_flags & MSG_TRUNC) {
            return -1;
        }
    }

    if (n != HEADER + size || fromlen != e.server_addrlen || !same_addr(&from, &e.server_addr)) {
        return -1;
    }
    return 0;
}

static void *client(void *arg)
{
    int id = (int)(long)arg;
    char *sent = malloc(MAX_PAYLOAD);
    char *got = malloc(MAX_PAYLOAD);
    void *ret = NULL;
    st_netfd_t fd = NULL;
    struct sockaddr_storage addr;
    int addrlen;
    if (!sent || !got) {
        ret = (void *)1;
        goto out;
    }
    /* Each client has its own socket, so the server sees its own address. */
    if (!(fd = udp_bind(e.family, &addr, &addrlen))) {
        ret = (void *)2;
        goto out;
    }

    for (int seq = 0; seq < MESSAGES; seq++) {
        int size = payload_size(id, seq);
        char h[HEADER], gh[HEADER];
        header_fill(h, id, seq);
        payload_fill(id, seq, sent, size);
        memset(got, 0, size);

        if (client_send(fd, h, sent, size, seq) != 0) {
            ret = (void *)3;
            break;
        }
        if (client_recv(fd, gh, got, size) != 0) {
            ret = (void *)4;
            break;
        }
        if (memcmp(h, gh, HEADER) != 0 || memcmp(sent, got, size) != 0) {
            ret = (void *)5;
            break;
        }
        e.client_msgs++;
        e.client_bytes += HEADER + size;
        /* Let the other clients interleave with this one. */
        if (seq % 3 == id % 3) {
            st_thread_yield();
        }
    }

out:
    if (fd && st_netfd_close(fd) != 0) {
        ret = (void *)6;
    }
    free(sent);
    free(got);
    return ret;
}

static int echo(int family, int mode)
{
    memset(&e, 0, sizeof(e));
    e.family = family;
    e.mode = mode;
    CHECK((e.server = udp_bind(family, &e.server_addr, &e.server_addrlen)) != NULL);
    CHECK(e.server_addrlen == addr_size(family));

    st_thread_t srv, clients[CLIENTS];
    CHECK((srv = st_thread_create(server, NULL, 1, 0)) != NULL);
    for (int i = 0; i < CLIENTS; i++) {
        CHECK((clients[i] = st_thread_create(client, (void *)(long)i, 1, 0)) != NULL);
    }

    void *ret;
    for (int i = 0; i < CLIENTS; i++) {
        ret = (void *)-1;
        CHECK(st_thread_join(clients[i], &ret) == 0);
        CHECK(ret == NULL);
    }

    /* The server waits in its receive call, and is stopped as SRS stops it. */
    st_thread_interrupt(srv);
    ret = (void *)-1;
    CHECK(st_thread_join(srv, &ret) == 0);
    CHECK(ret == NULL);
    CHECK(st_netfd_close(e.server) == 0);

    /* Every datagram and byte on both sides, and one peer per client. */
    CHECK(e.client_msgs == CLIENTS * MESSAGES);
    CHECK(e.server_msgs == CLIENTS * MESSAGES);
    CHECK(e.server_bytes == e.client_bytes);
    CHECK(e.npeers == CLIENTS);
    long bytes = 0;
    for (int id = 0; id < CLIENTS; id++) {
        long peer_bytes = 0;
        for (int seq = 0; seq < MESSAGES; seq++) {
            peer_bytes += HEADER + payload_size(id, seq);
        }
        int found = 0;
        for (int i = 0; i < e.npeers; i++) {
            if (e.peers[i].id == id) {
                found++;
                CHECK(e.peers[i].msgs == MESSAGES);
                CHECK(e.peers[i].bytes == peer_bytes);
            }
        }
        CHECK(found == 1);
        bytes += peer_bytes;
    }
    CHECK(e.client_bytes == bytes);
    return 0;
}

/* Each receive gets one whole datagram, and a short buffer truncates it. */
static int boundaries(int family)
{
    struct sockaddr_storage a_addr, b_addr, from;
    int a_len, b_len, fromlen;
    st_netfd_t a, b;
    char buf[400], big[300];
    CHECK((a = udp_bind(family, &a_addr, &a_len)) != NULL);
    CHECK((b = udp_bind(family, &b_addr, &b_len)) != NULL);
    memset(big, 'x', sizeof(big));

    /* Three datagrams queued before any is read keep their sizes. */
    CHECK(st_sendto(a, "hello", 5, (struct sockaddr *)&b_addr, b_len, BLOCK_US) == 5);
    CHECK(st_sendto(a, "!", 1, (struct sockaddr *)&b_addr, b_len, BLOCK_US) == 1);
    CHECK(st_sendto(a, big, sizeof(big), (struct sockaddr *)&b_addr, b_len, BLOCK_US) == (int)sizeof(big));
    fromlen = sizeof(from);
    CHECK(st_recvfrom(b, buf, sizeof(buf), (struct sockaddr *)&from, &fromlen, BLOCK_US) == 5);
    CHECK(!memcmp(buf, "hello", 5) && same_addr(&from, &a_addr));
    fromlen = sizeof(from);
    CHECK(st_recvfrom(b, buf, sizeof(buf), (struct sockaddr *)&from, &fromlen, BLOCK_US) == 1);
    CHECK(buf[0] == '!' && same_addr(&from, &a_addr));
    /* A NULL sender is allowed, as for a connected socket. */
    CHECK(st_recvfrom(b, buf, sizeof(buf), NULL, NULL, BLOCK_US) == (int)sizeof(big));
    CHECK(!memcmp(buf, big, sizeof(big)));

    /* A buffer shorter than the datagram gets its start; the rest is dropped. */
    CHECK(st_sendto(a, big, sizeof(big), (struct sockaddr *)&b_addr, b_len, BLOCK_US) == (int)sizeof(big));
    CHECK(st_sendto(a, "next", 4, (struct sockaddr *)&b_addr, b_len, BLOCK_US) == 4);
    fromlen = sizeof(from);
    CHECK(st_recvfrom(b, buf, 10, (struct sockaddr *)&from, &fromlen, BLOCK_US) == 10);
    CHECK(!memcmp(buf, big, 10));
    fromlen = sizeof(from);
    CHECK(st_recvfrom(b, buf, sizeof(buf), (struct sockaddr *)&from, &fromlen, BLOCK_US) == 4);
    CHECK(!memcmp(buf, "next", 4));

    /* st_recvmsg reports the truncation in msg_flags. */
    CHECK(st_sendto(a, big, sizeof(big), (struct sockaddr *)&b_addr, b_len, BLOCK_US) == (int)sizeof(big));
    struct iovec iov[2] = {{buf, 4}, {buf + 4, 6}};
    struct msghdr msg;
    memset(&msg, 0, sizeof(msg));
    msg.msg_name = &from;
    msg.msg_namelen = sizeof(from);
    msg.msg_iov = iov;
    msg.msg_iovlen = 2;
    CHECK(st_recvmsg(b, &msg, 0, BLOCK_US) == 10);
    CHECK((msg.msg_flags & MSG_TRUNC) != 0);
    CHECK(!memcmp(buf, big, 10) && same_addr(&from, &a_addr));

    /* Nothing is left. */
    errno = 0;
    CHECK(st_recvfrom(b, buf, sizeof(buf), NULL, NULL, ST_UTIME_NO_WAIT) == -1 && errno == ETIME);
    CHECK(st_netfd_close(a) == 0);
    CHECK(st_netfd_close(b) == 0);
    return 0;
}

static void *recv_forever(void *arg)
{
    st_netfd_t fd = (st_netfd_t)arg;
    char buf[16];
    if (st_recvfrom(fd, buf, sizeof(buf), NULL, NULL, BLOCK_US) != -1 || errno != EINTR) {
        return (void *)1;
    }
    return NULL;
}

/* With no sender, the receive calls time out with ETIME, or are interrupted. */
static int timeouts(int family)
{
    struct sockaddr_storage addr, from;
    int addrlen, fromlen = sizeof(from);
    st_netfd_t fd;
    char buf[16];
    CHECK((fd = udp_bind(family, &addr, &addrlen)) != NULL);

    st_utime_t start = st_utime();
    errno = 0;
    CHECK(st_recvfrom(fd, buf, sizeof(buf), (struct sockaddr *)&from, &fromlen, SHORT_US) == -1 && errno == ETIME);
    CHECK(st_utime() - start >= SHORT_US);

    struct iovec iov = {buf, sizeof(buf)};
    struct msghdr msg;
    memset(&msg, 0, sizeof(msg));
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    start = st_utime();
    errno = 0;
    CHECK(st_recvmsg(fd, &msg, 0, SHORT_US) == -1 && errno == ETIME);
    CHECK(st_utime() - start >= SHORT_US);

    st_thread_t t;
    void *ret = (void *)-1;
    CHECK((t = st_thread_create(recv_forever, fd, 1, 0)) != NULL);
    st_usleep(1000);
    st_thread_interrupt(t);
    CHECK(st_thread_join(t, &ret) == 0);
    CHECK(ret == NULL);

    CHECK(st_netfd_close(fd) == 0);
    return 0;
}

static int run(int family)
{
    CHECK(echo(family, MODE_TO) == 0);
    CHECK(echo(family, MODE_MSG) == 0);
    CHECK(boundaries(family) == 0);
    CHECK(timeouts(family) == 0);
    return 0;
}

int main(int argc, char **argv)
{
    CHECK(tool_init() == 0);

    CHECK(run(AF_INET) == 0);

    /* IPv6, unless ::1 cannot be bound here. */
    struct sockaddr_storage addr;
    int addrlen;
    st_netfd_t fd = udp_bind(AF_INET6, &addr, &addrlen);
    if (!fd && (errno == EADDRNOTAVAIL || errno == EAFNOSUPPORT)) {
        printf("SKIP ipv6\n");
    } else {
        CHECK(fd != NULL);
        CHECK(st_netfd_close(fd) == 0);
        CHECK(run(AF_INET6) == 0);
    }

    printf("udp OK\n");
    return 0;
}
