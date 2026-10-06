/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2013-2026 The SRS Authors */

/*
 * Unix domain sockets: a stream server on a bound path echoes framed
 * messages from many clients, with st_read, st_write and st_writev; a
 * datagram server on a bound path echoes to clients on their own paths, with
 * st_recvfrom and st_sendto; and socketpairs used through
 * st_netfd_open_socket carry a stream and datagrams between two coroutines.
 */

#include "tool.h"

#include <stddef.h>
/* On native Windows, st.h brings struct iovec and tool.h struct sockaddr_un from afunix.h. */
#ifndef _WIN32
#include <sys/uio.h>
#include <sys/un.h>
#else
#include <io.h>
/* The POSIX names of the MSVC CRT calls, for the socket paths. */
#define access _access
#define unlink _unlink
#define F_OK 0
#endif

/* Long enough that a coroutine still blocked here is a failure, not a slow run. */
#define BLOCK_US (5 * 1000 * 1000)

/* A timeout that is expected to expire. */
#define SHORT_US (5 * 1000)

#ifdef _WIN32
/*
 * Windows has no /tmp, so the stream paths are in the temp folder, set by
 * temp_paths before use. Its AF_UNIX is stream only, with no datagram paths.
 */
static char stream_path[MAX_PATH], missing_path[MAX_PATH];
#define STREAM_PATH stream_path
#define MISSING_PATH missing_path

static int temp_paths(void)
{
    char dir[MAX_PATH];
    CHECK(GetTempPathA(sizeof(dir), dir) != 0);
    snprintf(stream_path, sizeof(stream_path), "%sst-tool-unix.sock", dir);
    snprintf(missing_path, sizeof(missing_path), "%sst-tool-unix-missing.sock", dir);
    return 0;
}
#else
/* The paths, removed by these literal paths before and after use. */
#define STREAM_PATH "/tmp/st-tool-unix.sock"
#define DGRAM_PATH "/tmp/st-tool-unix-dgram.sock"
#define MISSING_PATH "/tmp/st-tool-unix-missing.sock"
#define DGRAM_CLIENTS 4
static const char *dgram_client_paths[DGRAM_CLIENTS] = {
    "/tmp/st-tool-unix-dgram-0.sock",
    "/tmp/st-tool-unix-dgram-1.sock",
    "/tmp/st-tool-unix-dgram-2.sock",
    "/tmp/st-tool-unix-dgram-3.sock",
};
#endif

/* The stream echo: clients, messages per client, and the largest payload. */
#define CLIENTS 8
#define MESSAGES 24
#define MAX_PAYLOAD 24000
#define HEADER 8
/* The most iovecs a client sends one message with. */
#define MAX_IOV 8

/* The datagram echo: datagrams per client and the largest payload. macOS caps
 * a Unix datagram at 2048 bytes by default (net.local.dgram.maxdgram). */
#define DGRAMS 32
#define MAX_DGRAM 2000

static void remove_paths(void)
{
    unlink(STREAM_PATH);
    unlink(MISSING_PATH);
#ifndef _WIN32
    unlink(DGRAM_PATH);
    unlink("/tmp/st-tool-unix-dgram-0.sock");
    unlink("/tmp/st-tool-unix-dgram-1.sock");
    unlink("/tmp/st-tool-unix-dgram-2.sock");
    unlink("/tmp/st-tool-unix-dgram-3.sock");
#endif
}

static socklen_t unix_addr(const char *path, struct sockaddr_un *addr)
{
    memset(addr, 0, sizeof(*addr));
    addr->sun_family = AF_UNIX;
    strncpy(addr->sun_path, path, sizeof(addr->sun_path) - 1);
    return (socklen_t)sizeof(*addr);
}

/* A socket of type bound to path, or NULL with errno set. */
static st_netfd_t unix_bind(int type, const char *path)
{
    struct sockaddr_un addr;
    socklen_t len = unix_addr(path, &addr);
    int fd = (int)socket(AF_UNIX, type, 0);
    if (fd < 0) {
        return NULL;
    }

    /*
     * macOS gives a Unix datagram socket 4096 bytes to receive into
     * (net.local.dgram.recvspace), and a send to a full one fails with
     * ENOBUFS instead of waiting; make room for every client in flight.
     */
    int rcvbuf = 64 * 1024;
    if ((type == SOCK_DGRAM && setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf)) < 0)
        || bind(fd, (struct sockaddr *)&addr, len) < 0 || (type == SOCK_STREAM && listen(fd, CLIENTS) < 0)) {
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
    }
    return stfd;
}

/* A stream client connected to path with st_connect, or NULL with errno set. */
static st_netfd_t unix_connect(const char *path)
{
    struct sockaddr_un addr;
    socklen_t len = unix_addr(path, &addr);
    int fd = (int)socket(AF_UNIX, SOCK_STREAM, 0);
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

    if (st_connect(stfd, (struct sockaddr *)&addr, len, BLOCK_US) < 0) {
        int err = errno;
        st_netfd_close(stfd);
        errno = err;
        return NULL;
    }
    return stfd;
}

/* The payload of message seq from client id: its size and its bytes. */
static int payload_size(int id, int seq, int max)
{
    return 1 + (id * 7919 + seq * 3571) % max;
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

/* The header: the client id, the sequence number, and the payload size. */
static void header_fill(char *h, int id, int seq, int size)
{
    uint16_t v = htons((uint16_t)id);
    memcpy(h, &v, 2);
    v = htons((uint16_t)seq);
    memcpy(h + 2, &v, 2);
    uint32_t s = htonl((uint32_t)size);
    memcpy(h + 4, &s, 4);
}

static void header_parse(const char *h, int *id, int *seq, int *size)
{
    uint16_t v;
    memcpy(&v, h, 2);
    *id = ntohs(v);
    memcpy(&v, h + 2, 2);
    *seq = ntohs(v);
    uint32_t s;
    memcpy(&s, h + 4, 4);
    *size = (int)ntohl(s);
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

/*
 * Read exactly n bytes with st_read, which returns what is there. Returns n,
 * 0 at EOF before the first byte, or -1 (EOF part way sets EPIPE).
 */
static int read_all(st_netfd_t fd, char *buf, int n)
{
    int got = 0;
    while (got < n) {
        ssize_t r = st_read(fd, buf + got, n - got, BLOCK_US);
        if (r < 0) {
            return -1;
        }
        if (r == 0) {
            if (got == 0) {
                return 0;
            }
            errno = EPIPE;
            return -1;
        }
        got += (int)r;
    }
    return n;
}

static struct {
    st_netfd_t listener;
    int accepted;
    int conns_done;
    int server_msgs;
    long server_bytes;
    int client_msgs;
    long client_bytes;
} s;

/* Echo every message on one connection with st_write, until the client closes. */
static void *conn(void *arg)
{
    st_netfd_t fd = (st_netfd_t)arg;
    char *buf = malloc(HEADER + MAX_PAYLOAD);
    void *ret = NULL;
    if (!buf) {
        ret = (void *)1;
        goto out;
    }

    for (;;) {
        int r = read_all(fd, buf, HEADER);
        if (r == 0) {
            break;
        }
        if (r != HEADER) {
            ret = (void *)2;
            break;
        }

        int id, seq, size;
        header_parse(buf, &id, &seq, &size);
        if (id >= CLIENTS || seq >= MESSAGES || size != payload_size(id, seq, MAX_PAYLOAD)) {
            ret = (void *)3;
            break;
        }
        if (read_all(fd, buf + HEADER, size) != size || payload_check(id, seq, buf + HEADER, size) != 0) {
            ret = (void *)4;
            break;
        }
        if (st_write(fd, buf, HEADER + size, BLOCK_US) != HEADER + size) {
            ret = (void *)5;
            break;
        }
        s.server_msgs++;
        s.server_bytes += HEADER + size;
    }

out:
    if (st_netfd_close(fd) != 0 && !ret) {
        ret = (void *)6;
    }
    free(buf);
    if (!ret) {
        s.conns_done++;
    }
    return ret;
}

/* Accept every client, each in its own coroutine, until interrupted. */
static void *acceptor(void *arg)
{
    st_thread_t conns[CLIENTS];
    void *ret = NULL;
    int n = 0;

    for (;;) {
        struct sockaddr_un from;
        int fromlen = sizeof(from);
        st_netfd_t fd = st_accept(s.listener, (struct sockaddr *)&from, &fromlen, BLOCK_US);
        if (!fd) {
            if (errno != EINTR || n != CLIENTS) {
                ret = (void *)1;
            }
            break;
        }
        if (n == CLIENTS || fromlen > (int)sizeof(from)) {
            st_netfd_close(fd);
            ret = (void *)2;
            break;
        }
        if (!(conns[n] = st_thread_create(conn, fd, 1, 0))) {
            st_netfd_close(fd);
            ret = (void *)3;
            break;
        }
        n++;
        s.accepted++;
    }

    for (int i = 0; i < n; i++) {
        void *r = (void *)-1;
        if (st_thread_join(conns[i], &r) != 0 || r != NULL) {
            ret = (void *)4;
        }
    }
    return ret;
}

/* Send framed messages with st_writev, and read each echo back. */
static void *client(void *arg)
{
    int id = (int)(long)arg;
    char *sent = malloc(MAX_PAYLOAD);
    char *got = malloc(HEADER + MAX_PAYLOAD);
    void *ret = NULL;
    st_netfd_t fd = NULL;
    if (!sent || !got) {
        ret = (void *)1;
        goto out;
    }
    if (!(fd = unix_connect(STREAM_PATH))) {
        ret = (void *)2;
        goto out;
    }

    for (int seq = 0; seq < MESSAGES; seq++) {
        int size = payload_size(id, seq, MAX_PAYLOAD);
        char h[HEADER];
        header_fill(h, id, seq, size);
        payload_fill(id, seq, sent, size);

        /* The header in its own iovec, the payload in up to MAX_IOV - 1. */
        struct iovec iov[MAX_IOV];
        iov[0].iov_base = h;
        iov[0].iov_len = HEADER;
        int cnt = 1 + split(iov + 1, sent, size, 1 + seq % (MAX_IOV - 1));
        if (st_writev(fd, iov, cnt, BLOCK_US) != HEADER + size) {
            ret = (void *)3;
            break;
        }

        /* Odd clients read the echo with st_read, even ones with st_read_fully. */
        memset(got, 0, HEADER + size);
        int n = (id % 2) ? read_all(fd, got, HEADER + size) : (int)st_read_fully(fd, got, HEADER + size, BLOCK_US);
        if (n != HEADER + size || memcmp(got, h, HEADER) != 0 || memcmp(got + HEADER, sent, size) != 0) {
            ret = (void *)4;
            break;
        }
        s.client_msgs++;
        s.client_bytes += HEADER + size;
        /* Let the other clients interleave with this one. */
        if (seq % 3 == id % 3) {
            st_thread_yield();
        }
    }

out:
    if (fd && st_netfd_close(fd) != 0) {
        ret = (void *)5;
    }
    free(sent);
    free(got);
    return ret;
}

/* A stream server on a bound path, and clients that connect to it. */
static int stream_echo(void)
{
    memset(&s, 0, sizeof(s));
    unlink(STREAM_PATH);
    CHECK((s.listener = unix_bind(SOCK_STREAM, STREAM_PATH)) != NULL);
    CHECK(access(STREAM_PATH, F_OK) == 0);

    st_thread_t acc, clients[CLIENTS];
    CHECK((acc = st_thread_create(acceptor, NULL, 1, 0)) != NULL);
    for (int i = 0; i < CLIENTS; i++) {
        CHECK((clients[i] = st_thread_create(client, (void *)(long)i, 1, 0)) != NULL);
    }

    void *ret;
    for (int i = 0; i < CLIENTS; i++) {
        ret = (void *)-1;
        CHECK(st_thread_join(clients[i], &ret) == 0);
        CHECK(ret == NULL);
    }

    /* Every connection sees its client close; then the acceptor is stopped. */
    while (s.conns_done < CLIENTS) {
        CHECK(st_usleep(1000) == 0);
        CHECK(s.accepted == CLIENTS);
    }
    st_thread_interrupt(acc);
    ret = (void *)-1;
    CHECK(st_thread_join(acc, &ret) == 0);
    CHECK(ret == NULL);
    CHECK(st_netfd_close(s.listener) == 0);
    CHECK(unlink(STREAM_PATH) == 0);

    long bytes = 0;
    for (int id = 0; id < CLIENTS; id++) {
        for (int seq = 0; seq < MESSAGES; seq++) {
            bytes += HEADER + payload_size(id, seq, MAX_PAYLOAD);
        }
    }
    CHECK(s.accepted == CLIENTS);
    CHECK(s.client_msgs == CLIENTS * MESSAGES);
    CHECK(s.server_msgs == CLIENTS * MESSAGES);
    CHECK(s.client_bytes == bytes);
    CHECK(s.server_bytes == bytes);

    /* A path with no socket, and a path whose listener is gone. */
    st_netfd_t fd;
    unlink(MISSING_PATH);
#ifndef _WIN32
    /* Windows does not support this check: a connect to a missing path fails with ECONNREFUSED there. */
    errno = 0;
    CHECK(unix_connect(MISSING_PATH) == NULL && errno == ENOENT);
#endif
    CHECK((fd = unix_bind(SOCK_STREAM, MISSING_PATH)) != NULL);
    CHECK(st_netfd_close(fd) == 0);
    errno = 0;
    CHECK(unix_connect(MISSING_PATH) == NULL && errno == ECONNREFUSED);
    CHECK(unlink(MISSING_PATH) == 0);
    return 0;
}

#ifndef _WIN32
/* Windows does not support the datagram echo: its AF_UNIX is stream only, with no datagram paths. */
static struct {
    st_netfd_t server;
    int server_msgs;
    long server_bytes;
    int client_msgs;
    long client_bytes;
    int seen[DGRAM_CLIENTS];
} d;

/* Whether a sender address is path. */
static int from_path(const struct sockaddr_un *from, int fromlen, const char *path)
{
    if (fromlen <= (int)offsetof(struct sockaddr_un, sun_path) || from->sun_family != AF_UNIX) {
        return 0;
    }
    return !strncmp(from->sun_path, path, sizeof(from->sun_path));
}

/* Echo every datagram to the path it came from, until interrupted. */
static void *dgram_server(void *arg)
{
    char buf[MAX_DGRAM + 1];
    for (;;) {
        struct sockaddr_un from;
        int fromlen = sizeof(from);
        int n = st_recvfrom(d.server, buf, sizeof(buf), (struct sockaddr *)&from, &fromlen, BLOCK_US);
        if (n < 0) {
            return (errno == EINTR && d.server_msgs == DGRAM_CLIENTS * DGRAMS) ? NULL : (void *)1;
        }

        int id, seq, size;
        if (n < HEADER) {
            return (void *)2;
        }
        header_parse(buf, &id, &seq, &size);
        /* The sender is known by its bound path. */
        if (id >= DGRAM_CLIENTS || !from_path(&from, fromlen, dgram_client_paths[id])) {
            return (void *)3;
        }
        if (seq != d.seen[id] || n != HEADER + size || size != payload_size(id, seq, MAX_DGRAM - HEADER)
            || payload_check(id, seq, buf + HEADER, size) != 0) {
            return (void *)4;
        }
        if (st_sendto(d.server, buf, n, (struct sockaddr *)&from, fromlen, BLOCK_US) != n) {
            return (void *)5;
        }
        d.seen[id]++;
        d.server_msgs++;
        d.server_bytes += n;
    }
}

/* Send datagrams from its own bound path, and check each echo. */
static void *dgram_client(void *arg)
{
    int id = (int)(long)arg;
    void *ret = NULL;
    st_netfd_t fd;
    struct sockaddr_un to;
    socklen_t tolen = unix_addr(DGRAM_PATH, &to);
    if (!(fd = unix_bind(SOCK_DGRAM, dgram_client_paths[id]))) {
        return (void *)1;
    }

    for (int seq = 0; seq < DGRAMS; seq++) {
        char sent[MAX_DGRAM], got[MAX_DGRAM + 1];
        int size = payload_size(id, seq, MAX_DGRAM - HEADER);
        header_fill(sent, id, seq, size);
        payload_fill(id, seq, sent + HEADER, size);
        if (st_sendto(fd, sent, HEADER + size, (struct sockaddr *)&to, tolen, BLOCK_US) != HEADER + size) {
            ret = (void *)2;
            break;
        }

        struct sockaddr_un from;
        int fromlen = sizeof(from);
        int n = st_recvfrom(fd, got, sizeof(got), (struct sockaddr *)&from, &fromlen, BLOCK_US);
        if (n != HEADER + size || memcmp(got, sent, n) != 0 || !from_path(&from, fromlen, DGRAM_PATH)) {
            ret = (void *)3;
            break;
        }
        d.client_msgs++;
        d.client_bytes += n;
        if (seq % 2 == id % 2) {
            st_thread_yield();
        }
    }

    if (st_netfd_close(fd) != 0 && !ret) {
        ret = (void *)4;
    }
    unlink(dgram_client_paths[id]);
    return ret;
}

/* A datagram server on a bound path, and clients on their own paths. */
static int dgram_echo(void)
{
    memset(&d, 0, sizeof(d));
    unlink(DGRAM_PATH);
    for (int i = 0; i < DGRAM_CLIENTS; i++) {
        unlink(dgram_client_paths[i]);
    }
    CHECK((d.server = unix_bind(SOCK_DGRAM, DGRAM_PATH)) != NULL);

    st_thread_t srv, clients[DGRAM_CLIENTS];
    CHECK((srv = st_thread_create(dgram_server, NULL, 1, 0)) != NULL);
    for (int i = 0; i < DGRAM_CLIENTS; i++) {
        CHECK((clients[i] = st_thread_create(dgram_client, (void *)(long)i, 1, 0)) != NULL);
    }

    void *ret;
    for (int i = 0; i < DGRAM_CLIENTS; i++) {
        ret = (void *)-1;
        CHECK(st_thread_join(clients[i], &ret) == 0);
        CHECK(ret == NULL);
        CHECK(access(dgram_client_paths[i], F_OK) != 0);
    }
    st_thread_interrupt(srv);
    ret = (void *)-1;
    CHECK(st_thread_join(srv, &ret) == 0);
    CHECK(ret == NULL);

    long bytes = 0;
    for (int id = 0; id < DGRAM_CLIENTS; id++) {
        CHECK(d.seen[id] == DGRAMS);
        for (int seq = 0; seq < DGRAMS; seq++) {
            bytes += HEADER + payload_size(id, seq, MAX_DGRAM - HEADER);
        }
    }
    CHECK(d.client_msgs == DGRAM_CLIENTS * DGRAMS);
    CHECK(d.server_msgs == DGRAM_CLIENTS * DGRAMS);
    CHECK(d.client_bytes == bytes);
    CHECK(d.server_bytes == bytes);

    /* Queued datagrams keep their boundaries, and with none st_recvfrom times out. */
    st_netfd_t c;
    char buf[64];
    struct sockaddr_un to, from;
    socklen_t tolen = unix_addr(DGRAM_PATH, &to);
    int fromlen = sizeof(from);
    CHECK((c = unix_bind(SOCK_DGRAM, dgram_client_paths[0])) != NULL);
    CHECK(st_sendto(c, "hello", 5, (struct sockaddr *)&to, tolen, BLOCK_US) == 5);
    CHECK(st_sendto(c, "!", 1, (struct sockaddr *)&to, tolen, BLOCK_US) == 1);
    CHECK(st_recvfrom(d.server, buf, sizeof(buf), (struct sockaddr *)&from, &fromlen, BLOCK_US) == 5);
    CHECK(!memcmp(buf, "hello", 5) && from_path(&from, fromlen, dgram_client_paths[0]));
    CHECK(st_recvfrom(d.server, buf, sizeof(buf), NULL, NULL, BLOCK_US) == 1 && buf[0] == '!');
    st_utime_t start = st_utime();
    errno = 0;
    CHECK(st_recvfrom(d.server, buf, sizeof(buf), NULL, NULL, SHORT_US) == -1 && errno == ETIME);
    CHECK(st_utime() - start >= SHORT_US);

    CHECK(st_netfd_close(c) == 0);
    CHECK(st_netfd_close(d.server) == 0);
    CHECK(unlink(dgram_client_paths[0]) == 0);
    CHECK(unlink(DGRAM_PATH) == 0);
    return 0;
}
#endif

/* The socketpair bulk transfer: the size and the largest write. */
#define PAIR_BYTES (512 * 1024)
#define PAIR_CHUNK 30000

/* Write PAIR_BYTES in chunks, so the writer waits when the socket is full. */
static void *pair_writer(void *arg)
{
    st_netfd_t fd = (st_netfd_t)arg;
    char *buf = malloc(PAIR_CHUNK);
    if (!buf) {
        return (void *)1;
    }
    void *ret = NULL;
    for (int off = 0; off < PAIR_BYTES;) {
        int n = 1 + (off * 13) % PAIR_CHUNK;
        if (n > PAIR_BYTES - off) {
            n = PAIR_BYTES - off;
        }
        for (int i = 0; i < n; i++) {
            buf[i] = (char)((off + i) * 7);
        }
        if (st_write(fd, buf, n, BLOCK_US) != n) {
            ret = (void *)2;
            break;
        }
        off += n;
    }
    free(buf);
    return ret;
}

/* A stream socketpair through st_netfd_open_socket, both ways, then EOF. */
static int pair_stream(void)
{
    int sv[2];
    st_netfd_t a, b;
    CHECK(tool_socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    CHECK((a = st_netfd_open_socket(sv[0])) != NULL);
    CHECK((b = st_netfd_open_socket(sv[1])) != NULL);
    CHECK(st_netfd_fileno(a) == sv[0] && st_netfd_fileno(b) == sv[1]);

    /* Both ways, the reader reading with st_read while the writer fills it. */
    char *buf = malloc(PAIR_CHUNK);
    CHECK(buf != NULL);
    for (int dir = 0; dir < 2; dir++) {
        st_netfd_t w = dir ? b : a, r = dir ? a : b;
        st_thread_t t;
        CHECK((t = st_thread_create(pair_writer, w, 1, 0)) != NULL);
        int got = 0;
        while (got < PAIR_BYTES) {
            ssize_t n = st_read(r, buf, PAIR_CHUNK, BLOCK_US);
            CHECK(n > 0 && got + n <= PAIR_BYTES);
            for (int i = 0; i < n; i++) {
                CHECK(buf[i] == (char)((got + i) * 7));
            }
            got += (int)n;
        }
        void *ret = (void *)-1;
        CHECK(st_thread_join(t, &ret) == 0);
        CHECK(ret == NULL);
    }
    free(buf);

    /* With nothing to read, st_read times out. */
    char c;
    st_utime_t start = st_utime();
    errno = 0;
    CHECK(st_read(b, &c, 1, SHORT_US) == -1 && errno == ETIME);
    CHECK(st_utime() - start >= SHORT_US);

    /* st_writev of several iovecs arrives as one stream; then close gives EOF. */
    char h[3] = {'a', 'b', 'c'}, p[5] = {'d', 'e', 'f', 'g', 'h'}, got[8];
    struct iovec iov[2] = {{h, sizeof(h)}, {p, sizeof(p)}};
    CHECK(st_writev(a, iov, 2, BLOCK_US) == 8);
    CHECK(st_read_fully(b, got, sizeof(got), BLOCK_US) == 8);
    CHECK(!memcmp(got, "abcdefgh", 8));
    CHECK(st_netfd_close(a) == 0);
    CHECK(st_read(b, got, sizeof(got), BLOCK_US) == 0);
    CHECK(st_netfd_close(b) == 0);
    return 0;
}

static void *pair_read_forever(void *arg)
{
    char buf[16];
    if (st_read((st_netfd_t)arg, buf, sizeof(buf), BLOCK_US) != -1 || errno != EINTR) {
        return (void *)1;
    }
    return NULL;
}

/*
 * A datagram socketpair: st_write and st_read keep the boundaries. On native
 * Windows it is a connected loopback UDP pair (D28).
 */
static int pair_dgram(void)
{
    int sv[2];
    st_netfd_t a, b;
    CHECK(tool_socketpair(AF_UNIX, SOCK_DGRAM, 0, sv) == 0);
    CHECK((a = st_netfd_open_socket(sv[0])) != NULL);
    CHECK((b = st_netfd_open_socket(sv[1])) != NULL);

    char big[1000], buf[1200];
    memset(big, 'x', sizeof(big));
    CHECK(st_write(a, "hello", 5, BLOCK_US) == 5);
    CHECK(st_write(a, big, sizeof(big), BLOCK_US) == (int)sizeof(big));
    CHECK(st_write(b, "back", 4, BLOCK_US) == 4);
    CHECK(st_read(b, buf, sizeof(buf), BLOCK_US) == 5 && !memcmp(buf, "hello", 5));
    CHECK(st_read(b, buf, sizeof(buf), BLOCK_US) == (int)sizeof(big) && !memcmp(buf, big, sizeof(big)));
    CHECK(st_read(a, buf, sizeof(buf), BLOCK_US) == 4 && !memcmp(buf, "back", 4));

    /* A coroutine waiting in st_read is interrupted. */
    st_thread_t t;
    void *ret = (void *)-1;
    CHECK((t = st_thread_create(pair_read_forever, b, 1, 0)) != NULL);
    CHECK(st_usleep(1000) == 0);
    st_thread_interrupt(t);
    CHECK(st_thread_join(t, &ret) == 0);
    CHECK(ret == NULL);

    CHECK(st_netfd_close(a) == 0);
    CHECK(st_netfd_close(b) == 0);
    return 0;
}

static int run(void)
{
    CHECK(stream_echo() == 0);
#ifndef _WIN32
    /* Windows does not support it: its AF_UNIX is stream only. */
    CHECK(dgram_echo() == 0);
#endif
    CHECK(pair_stream() == 0);
    CHECK(pair_dgram() == 0);
    return 0;
}

int main(int argc, char **argv)
{
    CHECK(tool_init() == 0);

#ifdef _WIN32
    CHECK(temp_paths() == 0);
#endif
    remove_paths();
    int r = run();
    remove_paths();
    CHECK(r == 0);

    printf("unix OK\n");
    return 0;
}
