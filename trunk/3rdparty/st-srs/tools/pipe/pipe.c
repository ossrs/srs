/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2013-2026 The SRS Authors */

/*
 * Pipes and files: the SRS signal pipe, where a signal handler writes the
 * signal number to a pipe that a coroutine reads through st_netfd_open; a
 * pipe between two coroutines whose writer fills it and waits; a FIFO and a
 * regular file opened with st_open.
 */

#include "tool.h"

#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/wait.h>

/* Long enough that a coroutine still blocked here is a failure, not a slow run. */
#define BLOCK_US (5 * 1000 * 1000)

/* A timeout that is expected to expire. */
#define SHORT_US (5 * 1000)

/* The paths, removed by these literal paths before and after use. */
#define FIFO_PATH "/tmp/st-tool-pipe.fifo"
#define FILE_PATH "/tmp/st-tool-pipe.txt"
#define MISSING_PATH "/tmp/st-tool-pipe-missing.txt"

/* Bytes sent through a pipe or FIFO, more than any pipe buffer holds. */
#define PIPE_BYTES (512 * 1024)
#define PIPE_CHUNK (32 * 1024)

static void remove_paths(void)
{
    unlink(FIFO_PATH);
    unlink(FILE_PATH);
    unlink(MISSING_PATH);
}

/* The byte at offset off of every stream this tool sends. */
static char stream_byte(int off)
{
    return (char)(off * 7 + off / 251);
}

/* The size of the chunk written at offset off, from 1 to PIPE_CHUNK. */
static int chunk_size(int off, int total)
{
    int n = 1 + (off * 13) % PIPE_CHUNK;
    return n < total - off ? n : total - off;
}

/* Read total bytes of the stream from fd, then EOF. Returns 0 on success. */
static int read_stream(st_netfd_t fd, int total)
{
    char *buf = malloc(PIPE_CHUNK);
    CHECK(buf != NULL);
    int got = 0;
    while (got < total) {
        ssize_t n = st_read(fd, buf, PIPE_CHUNK, BLOCK_US);
        CHECK(n > 0 && got + n <= total);
        for (int i = 0; i < n; i++) {
            CHECK(buf[i] == stream_byte(got + i));
        }
        got += (int)n;
    }
    CHECK(st_read(fd, buf, PIPE_CHUNK, BLOCK_US) == 0);
    free(buf);
    return 0;
}

/*
 * The SRS signal pipe, as SrsSignalManager: the handler writes the signal
 * number to the write end, and a coroutine reads it from the read end opened
 * with st_netfd_open, then handles it. SIGHUP reloads, SIGTERM quits.
 */
static int sig_pipe[2] = {-1, -1};

static void sig_catcher(int signo)
{
    int err = errno;
    if (write(sig_pipe[1], &signo, sizeof(int)) != sizeof(int)) {
        /* Nothing to do in a handler; the reader then misses this signal. */
    }
    errno = err;
}

struct signal_manager {
    st_netfd_t fd;
    /* Where to write one byte after each reload, or NULL. */
    st_netfd_t ack;
    int signals[8];
    int nb_signals;
    int reloads;
};

static void *signal_manager_cycle(void *arg)
{
    struct signal_manager *m = (struct signal_manager *)arg;
    while (m->nb_signals < (int)(sizeof(m->signals) / sizeof(m->signals[0]))) {
        int signo;
        if (st_read(m->fd, &signo, sizeof(int), BLOCK_US) != sizeof(int)) {
            return (void *)1;
        }
        m->signals[m->nb_signals++] = signo;

        if (signo == SIGHUP) {
            m->reloads++;
            if (m->ack && st_write(m->ack, "r", 1, BLOCK_US) != 1) {
                return (void *)2;
            }
        } else if (signo == SIGTERM) {
            return NULL;
        }
    }
    return (void *)3;
}

/* The manager got SIGHUP then SIGTERM, reloaded once, and quit. */
static int check_reload_then_quit(st_thread_t t, struct signal_manager *m)
{
    void *ret = (void *)-1;
    CHECK(st_thread_join(t, &ret) == 0);
    CHECK(ret == NULL);
    CHECK(m->nb_signals == 2);
    CHECK(m->signals[0] == SIGHUP && m->signals[1] == SIGTERM);
    CHECK(m->reloads == 1);
    return 0;
}

static int signal_pipe(void)
{
    struct sigaction sa, old_hup, old_term;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sig_catcher;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    CHECK(pipe(sig_pipe) == 0);
    struct signal_manager m;
    memset(&m, 0, sizeof(m));
    CHECK((m.fd = st_netfd_open(sig_pipe[0])) != NULL);
    CHECK(st_netfd_fileno(m.fd) == sig_pipe[0]);
    CHECK(fcntl(sig_pipe[0], F_GETFL) & O_NONBLOCK);
    CHECK(sigaction(SIGHUP, &sa, &old_hup) == 0);
    CHECK(sigaction(SIGTERM, &sa, &old_term) == 0);

    /*
     * The process signals itself. A signal sent to the calling process is
     * handled before kill returns, so they are written in the order sent.
     */
    st_thread_t t;
    CHECK((t = st_thread_create(signal_manager_cycle, &m, 1, 0)) != NULL);
    CHECK(st_usleep(1000) == 0);
    CHECK(m.nb_signals == 0);
    CHECK(kill(getpid(), SIGHUP) == 0);
    CHECK(kill(getpid(), SIGTERM) == 0);
    CHECK(check_reload_then_quit(t, &m) == 0);

    /*
     * Another process signals, as an operator does, while ST waits in its
     * event system with every coroutine blocked. It sends SIGTERM only after
     * the reload is acknowledged, so the order is kept.
     */
    int ack[2];
    CHECK(pipe(ack) == 0);
    pid_t parent = getpid();
    pid_t pid = fork();
    CHECK(pid >= 0);
    if (pid == 0) {
        char c;
        close(ack[1]);
        if (kill(parent, SIGHUP) != 0 || read(ack[0], &c, 1) != 1 || kill(parent, SIGTERM) != 0) {
            _exit(1);
        }
        _exit(0);
    }

    close(ack[0]);
    memset(&m.signals, 0, sizeof(m.signals));
    m.nb_signals = m.reloads = 0;
    CHECK((m.ack = st_netfd_open(ack[1])) != NULL);
    CHECK((t = st_thread_create(signal_manager_cycle, &m, 1, 0)) != NULL);
    CHECK(check_reload_then_quit(t, &m) == 0);

    int status = -1;
    CHECK(waitpid(pid, &status, 0) == pid);
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);

    CHECK(sigaction(SIGHUP, &old_hup, NULL) == 0);
    CHECK(sigaction(SIGTERM, &old_term, NULL) == 0);
    CHECK(st_netfd_close(m.ack) == 0);
    CHECK(st_netfd_close(m.fd) == 0);
    CHECK(close(sig_pipe[1]) == 0);
    sig_pipe[0] = sig_pipe[1] = -1;
    return 0;
}

/* A writer of the whole stream to fd, then it closes fd. */
struct writer {
    st_netfd_t fd;
    int total;
    int done;
};

static void *writer_cycle(void *arg)
{
    struct writer *w = (struct writer *)arg;
    char *buf = malloc(PIPE_CHUNK);
    if (!buf) {
        return (void *)1;
    }

    void *ret = NULL;
    for (int off = 0; off < w->total;) {
        int n = chunk_size(off, w->total);
        for (int i = 0; i < n; i++) {
            buf[i] = stream_byte(off + i);
        }
        if (st_write(w->fd, buf, n, BLOCK_US) != n) {
            ret = (void *)2;
            break;
        }
        off += n;
    }
    free(buf);

    w->done = 1;
    if (st_netfd_close(w->fd) != 0) {
        return (void *)3;
    }
    return ret;
}

/* A pipe between two coroutines: the writer fills it and waits for the reader. */
static int pipe_fill(void)
{
    int p[2];
    st_netfd_t r, wfd;
    CHECK(pipe(p) == 0);
    CHECK((r = st_netfd_open(p[0])) != NULL);
    CHECK((wfd = st_netfd_open(p[1])) != NULL);

    /* With nothing to read, st_read times out. */
    char c;
    st_utime_t start = st_utime();
    errno = 0;
    CHECK(st_read(r, &c, 1, SHORT_US) == -1 && errno == ETIME);
    CHECK(st_utime() - start >= SHORT_US);

    /* The writer fills the pipe and waits, until the reader reads it all and EOF. */
    struct writer w = {wfd, PIPE_BYTES, 0};
    st_thread_t t;
    CHECK((t = st_thread_create(writer_cycle, &w, 1, 0)) != NULL);
    CHECK(st_usleep(1000) == 0);
    CHECK(w.done == 0);
    CHECK(read_stream(r, PIPE_BYTES) == 0);
    void *ret = (void *)-1;
    CHECK(st_thread_join(t, &ret) == 0);
    CHECK(ret == NULL && w.done == 1);
    CHECK(st_netfd_close(r) == 0);

    /*
     * With no reader running, st_write_resid times out part way, and the
     * reader then gets exactly the bytes it reports as sent.
     */
    CHECK(pipe(p) == 0);
    CHECK((r = st_netfd_open(p[0])) != NULL);
    CHECK((wfd = st_netfd_open(p[1])) != NULL);
    char *buf = malloc(PIPE_BYTES);
    CHECK(buf != NULL);
    for (int i = 0; i < PIPE_BYTES; i++) {
        buf[i] = stream_byte(i);
    }
    size_t resid = PIPE_BYTES;
    errno = 0;
    CHECK(st_write_resid(wfd, buf, &resid, SHORT_US) == -1 && errno == ETIME);
    CHECK(resid > 0 && resid < PIPE_BYTES);
    int sent = PIPE_BYTES - (int)resid;
    int got = 0;
    for (;;) {
        ssize_t n = st_read(r, buf, PIPE_BYTES, SHORT_US);
        if (n < 0) {
            CHECK(errno == ETIME);
            break;
        }
        CHECK(n > 0 && got + n <= sent);
        for (int i = 0; i < n; i++) {
            CHECK(buf[i] == stream_byte(got + i));
        }
        got += (int)n;
    }
    CHECK(got == sent);
    free(buf);

    /* With the read end closed, a write fails with EPIPE, as SIGPIPE is ignored. */
    struct sigaction sa, old_pipe;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    CHECK(sigaction(SIGPIPE, &sa, &old_pipe) == 0);
    CHECK(st_netfd_close(r) == 0);
    errno = 0;
    CHECK(st_write(wfd, "x", 1, BLOCK_US) == -1 && errno == EPIPE);
    CHECK(sigaction(SIGPIPE, &old_pipe, NULL) == 0);
    CHECK(st_netfd_close(wfd) == 0);
    return 0;
}

/* A FIFO opened with st_open: a reader and a writer coroutine. */
static int fifo(void)
{
    CHECK(mkfifo(FIFO_PATH, 0600) == 0);

    /* st_open always opens non-blocking, so the write end needs a reader first. */
    errno = 0;
    CHECK(st_open(FIFO_PATH, O_WRONLY, 0) == NULL && errno == ENXIO);

    st_netfd_t r, wfd;
    CHECK((r = st_open(FIFO_PATH, O_RDONLY, 0)) != NULL);
    CHECK(fcntl(st_netfd_fileno(r), F_GETFL) & O_NONBLOCK);
    CHECK((wfd = st_open(FIFO_PATH, O_WRONLY, 0)) != NULL);

    struct writer w = {wfd, PIPE_BYTES, 0};
    st_thread_t t;
    CHECK((t = st_thread_create(writer_cycle, &w, 1, 0)) != NULL);
    CHECK(st_usleep(1000) == 0);
    CHECK(w.done == 0);
    CHECK(read_stream(r, PIPE_BYTES) == 0);
    void *ret = (void *)-1;
    CHECK(st_thread_join(t, &ret) == 0);
    CHECK(ret == NULL && w.done == 1);
    CHECK(st_netfd_close(r) == 0);

    CHECK(unlink(FIFO_PATH) == 0);
    return 0;
}

/* A regular file opened with st_open: a write, then a read of it back. */
static int regular_file(void)
{
    errno = 0;
    CHECK(st_open(MISSING_PATH, O_RDONLY, 0) == NULL && errno == ENOENT);

    st_netfd_t fd;
    CHECK((fd = st_open(FILE_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0600)) != NULL);
    CHECK(st_write(fd, "hello, ", 7, BLOCK_US) == 7);
    struct iovec iov[2] = {{"state ", 6}, {"threads", 7}};
    CHECK(st_writev(fd, iov, 2, BLOCK_US) == 13);
    CHECK(st_netfd_close(fd) == 0);

    struct stat st;
    CHECK(stat(FILE_PATH, &st) == 0 && st.st_size == 20);

    char buf[32];
    CHECK((fd = st_open(FILE_PATH, O_RDONLY, 0)) != NULL);
    CHECK(st_read_fully(fd, buf, 20, BLOCK_US) == 20);
    CHECK(!memcmp(buf, "hello, state threads", 20));
    CHECK(st_read(fd, buf, sizeof(buf), BLOCK_US) == 0);
    CHECK(st_netfd_close(fd) == 0);

    /* O_APPEND is kept: a second open appends after what is there. */
    CHECK((fd = st_open(FILE_PATH, O_WRONLY | O_APPEND, 0)) != NULL);
    CHECK(st_write(fd, "!", 1, BLOCK_US) == 1);
    CHECK(st_netfd_close(fd) == 0);
    CHECK(stat(FILE_PATH, &st) == 0 && st.st_size == 21);

    CHECK(unlink(FILE_PATH) == 0);
    return 0;
}

static int run(void)
{
    CHECK(signal_pipe() == 0);
    CHECK(pipe_fill() == 0);
    CHECK(fifo() == 0);
    CHECK(regular_file() == 0);
    return 0;
}

int main(int argc, char **argv)
{
    CHECK(tool_init() == 0);

    remove_paths();
    int r = run();
    remove_paths();
    CHECK(r == 0);

    printf("pipe OK\n");
    return 0;
}
