/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2013-2026 The SRS Authors */

#include <st_utest.hpp>

#include <st.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>
#include <string>
#include <vector>
#include <algorithm>

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <sys/resource.h>
#include <sys/un.h>
#include <stddef.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define ST_UTIME_MILLISECONDS 1000
#define ST_UTEST_TIMEOUT (100 * ST_UTIME_MILLISECONDS)

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The utest for reading a TCP stream, the way SRS reads every RTMP chunk header and payload with st_read_fully.
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// A connected TCP pair on loopback: the server side SRS reads from, and the client side the test writes from.
struct IoTestTcpPair {
    st_netfd_t server_;
    st_netfd_t client_;
    IoTestTcpPair() : server_(NULL), client_(NULL) {
    }
    ~IoTestTcpPair() {
        close_client();
        if (server_) st_netfd_close(server_);
    }
    void close_client() {
        if (client_) st_netfd_close(client_);
        client_ = NULL;
    }
    // Close the client with SO_LINGER 0, so the kernel resets the connection instead of a normal close: the network
    // of a publisher dies.
    void reset_client() {
        struct linger lg;
        lg.l_onoff = 1;
        lg.l_linger = 0;
        setsockopt(st_netfd_fileno(client_), SOL_SOCKET, SO_LINGER, &lg, sizeof(lg));
        close_client();
    }
};

// Accept the connection from client cfd, closing any other one queued on the listener, such as a stray connection from
// another program to a port it used before.
static int io_accept_from(int lfd, int cfd)
{
    struct sockaddr_in local, peer;
    socklen_t locallen = sizeof(local);
    if (getsockname(cfd, (sockaddr*)&local, &locallen) < 0) return -1;

    for (;;) {
        socklen_t peerlen = sizeof(peer);
        int sfd = ::accept(lfd, (sockaddr*)&peer, &peerlen);
        if (sfd < 0 || (peer.sin_port == local.sin_port && peer.sin_addr.s_addr == local.sin_addr.s_addr)) {
            return sfd;
        }
        ::close(sfd);
    }
}

// Listen on an ephemeral loopback port, then connect and accept. Loopback completes the handshake in the kernel, so
// the plain blocking calls return at once.
static bool io_tcp_pair(IoTestTcpPair& pair)
{
    int lfd = socket(AF_INET, SOCK_STREAM, 0);
    if (lfd < 0) return false;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;

    socklen_t addrlen = sizeof(addr);
    if (::bind(lfd, (sockaddr*)&addr, sizeof(addr)) < 0 || ::listen(lfd, 1) < 0
        || getsockname(lfd, (sockaddr*)&addr, &addrlen) < 0) {
        ::close(lfd);
        return false;
    }

    int cfd = socket(AF_INET, SOCK_STREAM, 0);
    if (cfd < 0 || ::connect(cfd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        if (cfd >= 0) ::close(cfd);
        ::close(lfd);
        return false;
    }

    int sfd = io_accept_from(lfd, cfd);
    ::close(lfd);
    if (sfd < 0) {
        ::close(cfd);
        return false;
    }

    pair.server_ = st_netfd_open_socket(sfd);
    pair.client_ = st_netfd_open_socket(cfd);
    return pair.server_ && pair.client_;
}

// The client sends each segment as its own write, sleeping between them so the reader drains one segment at a time
// and has to wait for the next, like a chunk spread over several TCP segments. Then it closes, unless asked to stay
// connected.
struct IoTestSegmentWriter {
    IoTestTcpPair* pair_;
    std::string segments_[4];
    int nn_segments_;
    bool close_after_;
};

static void* io_segment_writer(void* arg)
{
    IoTestSegmentWriter* w = (IoTestSegmentWriter*)arg;
    for (int i = 0; i < w->nn_segments_; i++) {
        st_usleep(10 * ST_UTIME_MILLISECONDS);
        st_write(w->pair_->client_, w->segments_[i].data(), w->segments_[i].size(), ST_UTEST_TIMEOUT);
    }
    if (w->close_after_) {
        st_usleep(10 * ST_UTIME_MILLISECONDS);
        w->pair_->close_client();
    }
    return NULL;
}

static st_thread_t io_start_writer(IoTestSegmentWriter& w, IoTestTcpPair& pair, bool close_after,
    const char* s0, const char* s1 = NULL, const char* s2 = NULL)
{
    w.pair_ = &pair;
    w.close_after_ = close_after;
    w.nn_segments_ = 0;
    const char* segments[] = {s0, s1, s2};
    for (int i = 0; i < 3 && segments[i]; i++) {
        w.segments_[w.nn_segments_++] = segments[i];
    }
    return st_thread_create(io_segment_writer, &w, 1, 0);
}

// An RTMP basic and message header of 12 bytes arrives as three TCP segments. st_read_fully waits for each one and
// returns only when all 12 bytes are in the buffer, so SRS never parses half a header. Locks in current behavior.
VOID TEST(IoReadTest, ReadFullyWaitsForEverySegment)
{
    IoTestTcpPair pair;
    ASSERT_TRUE(io_tcp_pair(pair));

    IoTestSegmentWriter w;
    st_thread_t writer = io_start_writer(w, pair, false, "0123", "4567", "89ab");
    ASSERT_TRUE(writer != NULL);

    char buf[13] = {0};
    EXPECT_EQ(12, st_read_fully(pair.server_, buf, 12, ST_UTIME_NO_TIMEOUT));
    EXPECT_STREQ("0123456789ab", buf);

    st_thread_join(writer, NULL);
}

// The publisher disconnects after sending only part of a chunk. st_read_fully returns the short count, not -1, so the
// caller must compare the count with what it asked for: SRS turns a short count into ECONNRESET. Locks in current
// behavior.
VOID TEST(IoReadTest, ReadFullyReturnsShortCountWhenPeerClosesHalfway)
{
    IoTestTcpPair pair;
    ASSERT_TRUE(io_tcp_pair(pair));

    IoTestSegmentWriter w;
    st_thread_t writer = io_start_writer(w, pair, true, "01234");
    ASSERT_TRUE(writer != NULL);

    char buf[13] = {0};
    EXPECT_EQ(5, st_read_fully(pair.server_, buf, 12, ST_UTEST_TIMEOUT));
    EXPECT_STREQ("01234", buf);

    st_thread_join(writer, NULL);
}

// The publisher stalls after sending part of a chunk. st_read_fully fails with ETIME once the timeout passes without
// new data; SRS reports it as a socket timeout. Locks in current behavior.
VOID TEST(IoReadTest, ReadFullyTimesOutHalfway)
{
    IoTestTcpPair pair;
    ASSERT_TRUE(io_tcp_pair(pair));

    IoTestSegmentWriter w;
    st_thread_t writer = io_start_writer(w, pair, false, "01234");
    ASSERT_TRUE(writer != NULL);

    char buf[13] = {0};
    errno = 0;
    EXPECT_EQ(-1, st_read_fully(pair.server_, buf, 12, 50 * ST_UTIME_MILLISECONDS));
    EXPECT_EQ(ETIME, errno);
    EXPECT_STREQ("01234", buf);

    st_thread_join(writer, NULL);
}

struct IoTestBlockedReader {
    IoTestTcpPair* pair_;
    char buf_[13];
    ssize_t nread_;
    int errno_;
};

static void* io_read_fully_coroutine(void* arg)
{
    IoTestBlockedReader* r = (IoTestBlockedReader*)arg;
    errno = 0;
    r->nread_ = st_read_fully(r->pair_->server_, r->buf_, 12, ST_UTIME_NO_TIMEOUT);
    r->errno_ = errno;
    return NULL;
}

// SRS stops a connection while its coroutine waits, with no timeout, for the rest of a chunk. The interrupt wakes it
// and st_read_fully fails with EINTR, which is how every SRS connection coroutine shuts down. Locks in current
// behavior.
VOID TEST(IoReadTest, ReadFullyInterruptedHalfway)
{
    IoTestTcpPair pair;
    ASSERT_TRUE(io_tcp_pair(pair));

    IoTestBlockedReader r;
    memset(&r, 0, sizeof(r));
    r.pair_ = &pair;
    st_thread_t reader = st_thread_create(io_read_fully_coroutine, &r, 1, 0);
    ASSERT_TRUE(reader != NULL);

    // Let the reader take part of the chunk and block for the rest. It reads straight into its buffer, then waits
    // without switching in between, so once the bytes are there it is waiting.
    ASSERT_EQ(5, st_write(pair.client_, "01234", 5, ST_UTEST_TIMEOUT));
    for (int i = 0; i < 100 && r.buf_[4] == 0; i++) {
        st_usleep(1 * ST_UTIME_MILLISECONDS);
    }
    ASSERT_EQ('4', r.buf_[4]);

    st_thread_interrupt(reader);
    st_thread_join(reader, NULL);

    EXPECT_EQ(-1, r.nread_);
    EXPECT_EQ(EINTR, r.errno_);
    EXPECT_STREQ("01234", r.buf_);
}

// st_read_resid reads into the buffer until it is full or the peer closes, and reports how many bytes are still
// missing, so a caller can tell a complete read from a truncated one without doing the arithmetic. Locks in current
// behavior.
VOID TEST(IoReadTest, ReadResidReportsMissingBytes)
{
    IoTestTcpPair pair;
    ASSERT_TRUE(io_tcp_pair(pair));

    IoTestSegmentWriter w;
    st_thread_t writer = io_start_writer(w, pair, true, "0123", "45678");
    ASSERT_TRUE(writer != NULL);

    // The first 4 bytes complete the first read, and nothing is missing.
    char buf[16] = {0};
    size_t resid = 4;
    EXPECT_EQ(0, st_read_resid(pair.server_, buf, &resid, ST_UTEST_TIMEOUT));
    EXPECT_EQ(0, (int)resid);
    EXPECT_STREQ("0123", buf);

    // The peer closes after 5 more bytes, so 7 of the 12 are missing. The call still succeeds.
    memset(buf, 0, sizeof(buf));
    resid = 12;
    EXPECT_EQ(0, st_read_resid(pair.server_, buf, &resid, ST_UTEST_TIMEOUT));
    EXPECT_EQ(7, (int)resid);
    EXPECT_STREQ("45678", buf);

    st_thread_join(writer, NULL);
}

// A message is read straight into separate header and payload buffers with st_readv_resid. The data arrives in
// segments that end exactly at a buffer boundary, in the middle of a buffer, and across the last buffer, so the
// call advances the caller's iovec array past every buffer it fills and resumes mid-buffer after a partial read.
// When it returns, the array is used up. Locks in current behavior.
VOID TEST(IoReadTest, ReadvResidFillsHeaderAndPayloadBuffers)
{
    IoTestTcpPair pair;
    ASSERT_TRUE(io_tcp_pair(pair));

    IoTestSegmentWriter w;
    st_thread_t writer = io_start_writer(w, pair, false, "hdr", "pa", "yload");
    ASSERT_TRUE(writer != NULL);

    char header[4] = {0};
    char payload1[5] = {0};
    char payload2[4] = {0};
    struct iovec iovs[3];
    iovs[0].iov_base = header;
    iovs[0].iov_len = 3;
    iovs[1].iov_base = payload1;
    iovs[1].iov_len = 4;
    iovs[2].iov_base = payload2;
    iovs[2].iov_len = 3;

    struct iovec* iov = iovs;
    int iov_size = 3;
    EXPECT_EQ(0, st_readv_resid(pair.server_, &iov, &iov_size, ST_UTEST_TIMEOUT));
    EXPECT_EQ(0, iov_size);
    EXPECT_TRUE(iov == iovs + 3);
    EXPECT_STREQ("hdr", header);
    EXPECT_STREQ("payl", payload1);
    EXPECT_STREQ("oad", payload2);

    st_thread_join(writer, NULL);
}

// When the peer closes before every buffer is full, st_readv_resid stops and succeeds, and the iovec array points at
// what is still missing: the rest of the partly filled buffer, then the untouched ones. Locks in current behavior.
VOID TEST(IoReadTest, ReadvResidLeavesMissingBuffersWhenPeerCloses)
{
    IoTestTcpPair pair;
    ASSERT_TRUE(io_tcp_pair(pair));

    IoTestSegmentWriter w;
    st_thread_t writer = io_start_writer(w, pair, true, "hdrpa");
    ASSERT_TRUE(writer != NULL);

    char header[4] = {0};
    char payload1[5] = {0};
    char payload2[4] = {0};
    struct iovec iovs[3];
    iovs[0].iov_base = header;
    iovs[0].iov_len = 3;
    iovs[1].iov_base = payload1;
    iovs[1].iov_len = 4;
    iovs[2].iov_base = payload2;
    iovs[2].iov_len = 3;

    struct iovec* iov = iovs;
    int iov_size = 3;
    EXPECT_EQ(0, st_readv_resid(pair.server_, &iov, &iov_size, ST_UTEST_TIMEOUT));
    EXPECT_EQ(2, iov_size);
    EXPECT_TRUE(iov == iovs + 1);
    EXPECT_TRUE(iov[0].iov_base == payload1 + 2);
    EXPECT_EQ(2, (int)iov[0].iov_len);
    EXPECT_EQ(3, (int)iov[1].iov_len);
    EXPECT_STREQ("hdr", header);
    EXPECT_STREQ("pa", payload1);

    st_thread_join(writer, NULL);
}

// st_readv is the scatter form of st_read: it waits until data is queued, then returns whatever is there, spread over
// the buffers in order, without waiting to fill them. On EOF it returns 0. Locks in current behavior.
VOID TEST(IoReadTest, ReadvReturnsWhatIsQueued)
{
    IoTestTcpPair pair;
    ASSERT_TRUE(io_tcp_pair(pair));

    IoTestSegmentWriter w;
    st_thread_t writer = io_start_writer(w, pair, true, "hdrpa");
    ASSERT_TRUE(writer != NULL);

    char header[4] = {0};
    char payload[8] = {0};
    struct iovec iovs[2];
    iovs[0].iov_base = header;
    iovs[0].iov_len = 3;
    iovs[1].iov_base = payload;
    iovs[1].iov_len = 7;

    EXPECT_EQ(5, st_readv(pair.server_, iovs, 2, ST_UTEST_TIMEOUT));
    EXPECT_STREQ("hdr", header);
    EXPECT_STREQ("pa", payload);

    EXPECT_EQ(0, st_readv(pair.server_, iovs, 2, ST_UTEST_TIMEOUT));

    st_thread_join(writer, NULL);
}

// With nothing queued, st_readv waits and fails with ETIME when the timeout passes. Locks in current behavior.
VOID TEST(IoReadTest, ReadvTimesOut)
{
    IoTestTcpPair pair;
    ASSERT_TRUE(io_tcp_pair(pair));

    char buf[4];
    struct iovec iov;
    iov.iov_base = buf;
    iov.iov_len = sizeof(buf);

    errno = 0;
    EXPECT_EQ(-1, st_readv(pair.server_, &iov, 1, 10 * ST_UTIME_MILLISECONDS));
    EXPECT_EQ(ETIME, errno);
}

// A message whose buffers are already full, such as an RTMP message with no payload left to read, comes back to
// st_readv_resid with nothing to read. It returns 0 at once, without reading or waiting, and the data queued on the
// connection stays for the next read. Locks in current behavior.
VOID TEST(IoReadTest, ReadvResidWithNothingToReadReturnsAtOnce)
{
    IoTestTcpPair pair;
    ASSERT_TRUE(io_tcp_pair(pair));
    ASSERT_EQ(1, st_write(pair.client_, "x", 1, ST_UTEST_TIMEOUT));

    char header[4] = {0};
    struct iovec iovs[1];
    iovs[0].iov_base = header;
    iovs[0].iov_len = 3;

    struct iovec* iov = iovs + 1;
    int iov_size = 0;
    st_utime_t starttime = st_utime();
    EXPECT_EQ(0, st_readv_resid(pair.server_, &iov, &iov_size, 1000 * ST_UTIME_MILLISECONDS));
    EXPECT_LT(st_utime() - starttime, 50 * ST_UTIME_MILLISECONDS);
    EXPECT_EQ(0, iov_size);
    EXPECT_TRUE(iov == iovs + 1);

    char c = 0;
    EXPECT_EQ(1, st_read(pair.server_, &c, 1, ST_UTEST_TIMEOUT));
    EXPECT_EQ('x', c);
}

// The network of a publisher dies and the connection is reset. Every read API fails with -1 and ECONNRESET instead of
// waiting; st_read_fully fails too, rather than returning a short count as it does on a normal close. Each call gets
// its own connection, because only the first read after a reset reports the error. Locks in current behavior.
VOID TEST(IoReadTest, ReadOnResetConnectionFails)
{
    char buf[12];
    struct iovec iovs[2];
    iovs[0].iov_base = buf;
    iovs[0].iov_len = 4;
    iovs[1].iov_base = buf + 4;
    iovs[1].iov_len = 8;

    if (true) {
        IoTestTcpPair pair;
        ASSERT_TRUE(io_tcp_pair(pair));
        pair.reset_client();

        errno = 0;
        EXPECT_EQ(-1, st_read(pair.server_, buf, sizeof(buf), ST_UTEST_TIMEOUT));
        EXPECT_EQ(ECONNRESET, errno);
    }

    if (true) {
        IoTestTcpPair pair;
        ASSERT_TRUE(io_tcp_pair(pair));
        pair.reset_client();

        errno = 0;
        EXPECT_EQ(-1, st_read_fully(pair.server_, buf, sizeof(buf), ST_UTEST_TIMEOUT));
        EXPECT_EQ(ECONNRESET, errno);
    }

    if (true) {
        IoTestTcpPair pair;
        ASSERT_TRUE(io_tcp_pair(pair));
        pair.reset_client();

        errno = 0;
        EXPECT_EQ(-1, st_readv(pair.server_, iovs, 2, ST_UTEST_TIMEOUT));
        EXPECT_EQ(ECONNRESET, errno);
    }

    if (true) {
        IoTestTcpPair pair;
        ASSERT_TRUE(io_tcp_pair(pair));
        pair.reset_client();

        struct iovec* iov = iovs;
        int iov_size = 2;
        errno = 0;
        EXPECT_EQ(-1, st_readv_resid(pair.server_, &iov, &iov_size, ST_UTEST_TIMEOUT));
        EXPECT_EQ(ECONNRESET, errno);
        EXPECT_EQ(2, iov_size);
    }
}

static volatile sig_atomic_t io_read_signals = 0;

static void io_read_on_signal(int signo)
{
    io_read_signals++;
}

// Another thread of the program signals the reading thread a few times, then the peer sends a message in one write.
struct IoTestSignaler {
    pthread_t target_;
    int peer_fd_;
    const char* msg_;
};

static void* io_signaler_thread(void* arg)
{
    IoTestSignaler* s = (IoTestSignaler*)arg;
    for (int i = 0; i < 3; i++) {
        usleep(5 * ST_UTIME_MILLISECONDS);
        pthread_kill(s->target_, SIGUSR1);
    }
    usleep(5 * ST_UTIME_MILLISECONDS);
    size_t size = strlen(s->msg_);
    if (::write(s->peer_fd_, s->msg_, size) != (ssize_t)size) return NULL;
    return NULL;
}

// A signal interrupts read while it waits in the kernel, and the handler was installed without SA_RESTART, so read
// fails with EINTR. st_read retries it instead of failing, because only st_thread_interrupt means the reader should
// stop, and the retry returns the byte the peer sends later. ST's sockets are non-blocking, so read returns at once and
// a signal almost never lands in it; read waits in the kernel only when the descriptor lost O_NONBLOCK, for example
// because another process sharing it after a fork cleared the flag. The test clears the flag so read waits, and signals
// it on purpose. Locks in current behavior.
VOID TEST(IoReadTest, ReadRetriesWhenSignalInterruptsSystemCall)
{
    IoTestTcpPair pair;
    ASSERT_TRUE(io_tcp_pair(pair));

    int fd = st_netfd_fileno(pair.server_);
    int flags = fcntl(fd, F_GETFL);
    ASSERT_NE(-1, flags);
    ASSERT_NE(-1, fcntl(fd, F_SETFL, flags & ~O_NONBLOCK));

    struct sigaction sa, old_sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = io_read_on_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    ASSERT_EQ(0, sigaction(SIGUSR1, &sa, &old_sa));

    IoTestSignaler s;
    s.target_ = pthread_self();
    s.peer_fd_ = st_netfd_fileno(pair.client_);
    s.msg_ = "x";
    io_read_signals = 0;
    pthread_t signaler;
    ASSERT_EQ(0, pthread_create(&signaler, NULL, io_signaler_thread, &s));

    char c = 0;
    errno = 0;
    ssize_t nread = st_read(pair.server_, &c, 1, ST_UTEST_TIMEOUT);
    int err = errno;

    pthread_join(signaler, NULL);
    sigaction(SIGUSR1, &old_sa, NULL);
    fcntl(fd, F_SETFL, flags);

    EXPECT_EQ(3, (int)io_read_signals);
    EXPECT_EQ(1, nread) << "errno=" << err;
    EXPECT_EQ('x', c);
}

// The same signal interrupts readv, the scatter read into a header and a payload buffer. st_readv retries it, and the
// retry returns the byte the peer sends later in the first buffer. The test clears O_NONBLOCK so readv waits in the
// kernel, as for st_read. Locks in current behavior.
VOID TEST(IoReadTest, ReadvRetriesWhenSignalInterruptsSystemCall)
{
    IoTestTcpPair pair;
    ASSERT_TRUE(io_tcp_pair(pair));

    int fd = st_netfd_fileno(pair.server_);
    int flags = fcntl(fd, F_GETFL);
    ASSERT_NE(-1, flags);
    ASSERT_NE(-1, fcntl(fd, F_SETFL, flags & ~O_NONBLOCK));

    struct sigaction sa, old_sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = io_read_on_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    ASSERT_EQ(0, sigaction(SIGUSR1, &sa, &old_sa));

    IoTestSignaler s;
    s.target_ = pthread_self();
    s.peer_fd_ = st_netfd_fileno(pair.client_);
    s.msg_ = "x";
    io_read_signals = 0;
    pthread_t signaler;
    ASSERT_EQ(0, pthread_create(&signaler, NULL, io_signaler_thread, &s));

    char header[4] = {0};
    char payload[8] = {0};
    struct iovec iovs[2];
    iovs[0].iov_base = header;
    iovs[0].iov_len = 3;
    iovs[1].iov_base = payload;
    iovs[1].iov_len = 7;

    errno = 0;
    ssize_t nread = st_readv(pair.server_, iovs, 2, ST_UTEST_TIMEOUT);
    int err = errno;

    pthread_join(signaler, NULL);
    sigaction(SIGUSR1, &old_sa, NULL);
    fcntl(fd, F_SETFL, flags);

    EXPECT_EQ(3, (int)io_read_signals);
    EXPECT_EQ(1, nread) << "errno=" << err;
    EXPECT_STREQ("x", header);
    EXPECT_STREQ("", payload);
}

// The same signal interrupts readv in st_readv_resid, which reads a whole message into a header and a payload buffer.
// st_readv_resid retries it, and the retry fills both buffers with the message the peer sends later in one write, so
// the iovec array is used up. The test clears O_NONBLOCK so readv waits in the kernel, as for st_read. Locks in current
// behavior.
VOID TEST(IoReadTest, ReadvResidRetriesWhenSignalInterruptsSystemCall)
{
    IoTestTcpPair pair;
    ASSERT_TRUE(io_tcp_pair(pair));

    int fd = st_netfd_fileno(pair.server_);
    int flags = fcntl(fd, F_GETFL);
    ASSERT_NE(-1, flags);
    ASSERT_NE(-1, fcntl(fd, F_SETFL, flags & ~O_NONBLOCK));

    struct sigaction sa, old_sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = io_read_on_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    ASSERT_EQ(0, sigaction(SIGUSR1, &sa, &old_sa));

    IoTestSignaler s;
    s.target_ = pthread_self();
    s.peer_fd_ = st_netfd_fileno(pair.client_);
    s.msg_ = "hdrpayload";
    io_read_signals = 0;
    pthread_t signaler;
    ASSERT_EQ(0, pthread_create(&signaler, NULL, io_signaler_thread, &s));

    char header[4] = {0};
    char payload[8] = {0};
    struct iovec iovs[2];
    iovs[0].iov_base = header;
    iovs[0].iov_len = 3;
    iovs[1].iov_base = payload;
    iovs[1].iov_len = 7;

    struct iovec* iov = iovs;
    int iov_size = 2;
    errno = 0;
    int r0 = st_readv_resid(pair.server_, &iov, &iov_size, ST_UTEST_TIMEOUT);
    int err = errno;

    pthread_join(signaler, NULL);
    sigaction(SIGUSR1, &old_sa, NULL);
    fcntl(fd, F_SETFL, flags);

    EXPECT_EQ(3, (int)io_read_signals);
    EXPECT_EQ(0, r0) << "errno=" << err;
    EXPECT_EQ(0, iov_size);
    EXPECT_TRUE(iov == iovs + 2);
    EXPECT_STREQ("hdr", header);
    EXPECT_STREQ("payload", payload);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The utest for accepting and connecting TCP, the way an SRS listener accepts clients and an SRS edge or forwarder
// connects to its origin.
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Listen on an ephemeral loopback port with the given backlog, and report the address clients connect to.
static st_netfd_t io_tcp_listen(struct sockaddr_in& addr, int backlog)
{
    int lfd = socket(AF_INET, SOCK_STREAM, 0);
    if (lfd < 0) return NULL;

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;

    socklen_t addrlen = sizeof(addr);
    if (::bind(lfd, (sockaddr*)&addr, sizeof(addr)) < 0 || ::listen(lfd, backlog) < 0
        || getsockname(lfd, (sockaddr*)&addr, &addrlen) < 0) {
        ::close(lfd);
        return NULL;
    }

    st_netfd_t stfd = st_netfd_open_socket(lfd);
    if (!stfd) ::close(lfd);
    return stfd;
}

// A client coroutine that connects with st_connect after the listener is already waiting.
struct IoTestConnector {
    struct sockaddr_in addr_;
    st_netfd_t stfd_;
    int r0_;
    int errno_;
};

static void* io_connect_coroutine(void* arg)
{
    IoTestConnector* c = (IoTestConnector*)arg;
    st_usleep(10 * ST_UTIME_MILLISECONDS);

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    c->stfd_ = (fd < 0) ? NULL : st_netfd_open_socket(fd);
    if (!c->stfd_) {
        if (fd >= 0) ::close(fd);
        return NULL;
    }

    errno = 0;
    c->r0_ = st_connect(c->stfd_, (sockaddr*)&c->addr_, sizeof(c->addr_), ST_UTEST_TIMEOUT);
    c->errno_ = errno;
    return NULL;
}

// An SRS listener waits for clients with no timeout. A client connects later: st_accept wakes, reports the client's
// address, and returns a non-blocking socket that is ready for ST I/O, whether or not the OS lets an accepted socket
// inherit O_NONBLOCK from the listener. On the client side, st_connect waits for the handshake and succeeds. Locks in
// current behavior.
VOID TEST(IoAcceptTest, AcceptWaitsForClient)
{
    struct sockaddr_in addr;
    st_netfd_t listener = io_tcp_listen(addr, 8);
    ASSERT_TRUE(listener != NULL);
    StStfdCleanup(listener);

    IoTestConnector c;
    memset(&c, 0, sizeof(c));
    c.addr_ = addr;
    st_thread_t connector = st_thread_create(io_connect_coroutine, &c, 1, 0);
    ASSERT_TRUE(connector != NULL);

    struct sockaddr_in peer;
    memset(&peer, 0, sizeof(peer));
    int peerlen = sizeof(peer);
    st_netfd_t client = st_accept(listener, (sockaddr*)&peer, &peerlen, ST_UTIME_NO_TIMEOUT);
    StStfdCleanup(client);
    st_thread_join(connector, NULL);
    st_netfd_t connected = c.stfd_;
    StStfdCleanup(connected);

    ASSERT_TRUE(client != NULL);
    ASSERT_TRUE(connected != NULL);
    EXPECT_EQ(0, c.r0_);

    struct sockaddr_in local;
    socklen_t locallen = sizeof(local);
    ASSERT_EQ(0, getsockname(st_netfd_fileno(connected), (sockaddr*)&local, &locallen));
    EXPECT_EQ((int)sizeof(peer), peerlen);
    EXPECT_EQ(AF_INET, peer.sin_family);
    EXPECT_EQ(local.sin_port, peer.sin_port);
    EXPECT_EQ(htonl(INADDR_LOOPBACK), peer.sin_addr.s_addr);

    EXPECT_TRUE(fcntl(st_netfd_fileno(client), F_GETFL) & O_NONBLOCK);

    char buf[6] = {0};
    EXPECT_EQ(5, st_write(connected, "hello", 5, ST_UTEST_TIMEOUT));
    EXPECT_EQ(5, st_read_fully(client, buf, 5, ST_UTEST_TIMEOUT));
    EXPECT_STREQ("hello", buf);
}

// No client arrives before the timeout, so st_accept fails with ETIME. Locks in current behavior.
VOID TEST(IoAcceptTest, AcceptTimesOut)
{
    struct sockaddr_in addr;
    st_netfd_t listener = io_tcp_listen(addr, 8);
    ASSERT_TRUE(listener != NULL);
    StStfdCleanup(listener);

    errno = 0;
    EXPECT_TRUE(st_accept(listener, NULL, NULL, 10 * ST_UTIME_MILLISECONDS) == NULL);
    EXPECT_EQ(ETIME, errno);
}

struct IoTestAcceptor {
    st_netfd_t listener_;
    st_netfd_t client_;
    int errno_;
};

static void* io_accept_coroutine(void* arg)
{
    IoTestAcceptor* a = (IoTestAcceptor*)arg;
    errno = 0;
    a->client_ = st_accept(a->listener_, NULL, NULL, ST_UTIME_NO_TIMEOUT);
    a->errno_ = errno;
    return NULL;
}

// SRS stops a listener on reload or shutdown by interrupting the coroutine that waits in st_accept with no timeout.
// st_accept fails with EINTR, and the listener is no longer waited on, so SRS can close it. Locks in current behavior.
VOID TEST(IoAcceptTest, AcceptInterruptedWhenListenerStops)
{
    struct sockaddr_in addr;
    IoTestAcceptor a;
    a.listener_ = io_tcp_listen(addr, 8);
    a.client_ = NULL;
    a.errno_ = 0;
    ASSERT_TRUE(a.listener_ != NULL);

    st_thread_t acceptor = st_thread_create(io_accept_coroutine, &a, 1, 0);
    ASSERT_TRUE(acceptor != NULL);

    st_usleep(10 * ST_UTIME_MILLISECONDS);
    st_thread_interrupt(acceptor);
    st_thread_join(acceptor, NULL);

    EXPECT_TRUE(a.client_ == NULL);
    EXPECT_EQ(EINTR, a.errno_);
    EXPECT_EQ(0, st_netfd_close(a.listener_));
}

// Accepting on a socket that is not listening is a hard error: st_accept fails at once with EINVAL instead of waiting
// for a client that can never arrive. Locks in current behavior.
VOID TEST(IoAcceptTest, AcceptOnSocketNotListeningFails)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_GE(fd, 0);
    st_netfd_t stfd = st_netfd_open_socket(fd);
    ASSERT_TRUE(stfd != NULL);
    StStfdCleanup(stfd);

    errno = 0;
    EXPECT_TRUE(st_accept(stfd, NULL, NULL, ST_UTIME_NO_TIMEOUT) == NULL);
    EXPECT_EQ(EINVAL, errno);
}

// Another thread of the program signals the accepting thread a few times, then a client connects.
struct IoTestAcceptSignaler {
    pthread_t target_;
    struct sockaddr_in addr_;
    int client_fd_;
};

static void* io_accept_signaler_thread(void* arg)
{
    IoTestAcceptSignaler* s = (IoTestAcceptSignaler*)arg;
    for (int i = 0; i < 3; i++) {
        usleep(20 * ST_UTIME_MILLISECONDS);
        pthread_kill(s->target_, SIGUSR1);
    }
    usleep(20 * ST_UTIME_MILLISECONDS);
    s->client_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (s->client_fd_ >= 0 && ::connect(s->client_fd_, (sockaddr*)&s->addr_, sizeof(s->addr_)) < 0) {
        ::close(s->client_fd_);
        s->client_fd_ = -1;
    }
    return NULL;
}

// A signal interrupts accept while it waits in the kernel, and the handler was installed without SA_RESTART, so accept
// fails with EINTR. st_accept retries it instead of failing, because only st_thread_interrupt means the listener should
// stop, and the retry accepts the client that connects later. ST's listener is non-blocking, so accept returns at once
// and a signal almost never lands in it; accept waits in the kernel only when the listener lost O_NONBLOCK. The test
// clears the flag so accept waits, and signals it on purpose. Locks in current behavior.
VOID TEST(IoAcceptTest, AcceptRetriesWhenSignalInterruptsSystemCall)
{
    struct sockaddr_in addr;
    st_netfd_t listener = io_tcp_listen(addr, 8);
    ASSERT_TRUE(listener != NULL);
    StStfdCleanup(listener);

    int fd = st_netfd_fileno(listener);
    int flags = fcntl(fd, F_GETFL);
    ASSERT_NE(-1, flags);
    ASSERT_NE(-1, fcntl(fd, F_SETFL, flags & ~O_NONBLOCK));

    struct sigaction sa, old_sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = io_read_on_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    ASSERT_EQ(0, sigaction(SIGUSR1, &sa, &old_sa));

    IoTestAcceptSignaler s;
    s.target_ = pthread_self();
    s.addr_ = addr;
    s.client_fd_ = -1;
    io_read_signals = 0;
    pthread_t signaler;
    ASSERT_EQ(0, pthread_create(&signaler, NULL, io_accept_signaler_thread, &s));

    errno = 0;
    st_netfd_t client = st_accept(listener, NULL, NULL, ST_UTEST_TIMEOUT);
    int err = errno;
    StStfdCleanup(client);

    pthread_join(signaler, NULL);
    sigaction(SIGUSR1, &old_sa, NULL);
    fcntl(fd, F_SETFL, flags);
    int cfd = s.client_fd_;
    st_netfd_t cstfd = NULL;
    StFdCleanup(cfd, cstfd);

    EXPECT_EQ(3, (int)io_read_signals);
    ASSERT_TRUE(client != NULL) << "errno=" << err;
    ASSERT_NE(-1, cfd);

    char buf[6] = {0};
    ASSERT_EQ(5, ::write(cfd, "hello", 5));
    EXPECT_EQ(5, st_read_fully(client, buf, 5, ST_UTEST_TIMEOUT));
    EXPECT_STREQ("hello", buf);
}

// Open a TCP socket for st_connect.
static st_netfd_t io_tcp_socket(int family)
{
    int fd = socket(family, SOCK_STREAM, 0);
    if (fd < 0) return NULL;
    st_netfd_t stfd = st_netfd_open_socket(fd);
    if (!stfd) ::close(fd);
    return stfd;
}

// An SRS edge pulls from an origin that is down, or a forwarder uses the wrong port: nothing listens on the port, so
// st_connect fails with ECONNREFUSED. The kernel may refuse at once or after the handshake starts, and both ways
// report the same error. The test takes a free port by opening a listener and closing it. Locks in current behavior.
VOID TEST(IoConnectTest, ConnectRefusedWhenNobodyListens)
{
    struct sockaddr_in addr;
    st_netfd_t listener = io_tcp_listen(addr, 8);
    ASSERT_TRUE(listener != NULL);
    ASSERT_EQ(0, st_netfd_close(listener));

    st_netfd_t stfd = io_tcp_socket(AF_INET);
    ASSERT_TRUE(stfd != NULL);
    StStfdCleanup(stfd);

    errno = 0;
    EXPECT_EQ(-1, st_connect(stfd, (sockaddr*)&addr, sizeof(addr), ST_UTEST_TIMEOUT));
    EXPECT_EQ(ECONNREFUSED, errno);
}

// An SRS forwarder connects to an origin that is too busy to answer. Nobody accepts, so the origin's accept queue
// fills up and the kernel drops the next SYN; st_connect fails with ETIME once the timeout passes, and
// srs_tcp_connect reports it as a connect error. How many clients fit in the queue depends on the OS (one on macOS,
// two on Linux for a backlog of 1), so clients connect until one gets no answer. Locks in current behavior.
VOID TEST(IoConnectTest, ConnectTimesOutWhenServerDoesNotAnswer)
{
    struct sockaddr_in addr;
    st_netfd_t listener = io_tcp_listen(addr, 1);
    ASSERT_TRUE(listener != NULL);
    StStfdCleanup(listener);

    st_netfd_t clients[4] = {NULL, NULL, NULL, NULL};
    int nn_queued = 0;
    int r0 = 0;
    int err = 0;
    for (int i = 0; i < 4 && r0 == 0; i++) {
        clients[i] = io_tcp_socket(AF_INET);
        if (!clients[i]) break;

        errno = 0;
        r0 = st_connect(clients[i], (sockaddr*)&addr, sizeof(addr), 50 * ST_UTIME_MILLISECONDS);
        err = errno;
        if (r0 == 0) nn_queued++;
    }
    for (int i = 0; i < 4; i++) {
        if (clients[i]) st_netfd_close(clients[i]);
    }

    EXPECT_GE(nn_queued, 1);
    EXPECT_EQ(-1, r0);
    EXPECT_EQ(ETIME, err);
}

// Connecting an IPv4 socket to an IPv6 address is a caller bug, not a network failure: st_connect fails at once with
// EAFNOSUPPORT instead of waiting. srs_tcp_connect avoids it by creating the socket from the resolved address.
// Locks in current behavior.
VOID TEST(IoConnectTest, ConnectToAddressOfAnotherFamilyFails)
{
    st_netfd_t stfd = io_tcp_socket(AF_INET);
    ASSERT_TRUE(stfd != NULL);
    StStfdCleanup(stfd);

    struct sockaddr_in6 addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin6_family = AF_INET6;
    addr.sin6_addr = in6addr_loopback;
    addr.sin6_port = htons(1935);

    errno = 0;
    EXPECT_EQ(-1, st_connect(stfd, (sockaddr*)&addr, sizeof(addr), ST_UTIME_NO_TIMEOUT));
    EXPECT_EQ(EAFNOSUPPORT, errno);
}

#if defined(__linux__)
// Another thread of the program signals the connecting thread a few times, then accepts the client already queued on
// the listener, which makes room for the connecting one.
struct IoTestConnectSignaler {
    pthread_t target_;
    int lfd_;
    int accepted_;
};

static void* io_connect_signaler_thread(void* arg)
{
    IoTestConnectSignaler* s = (IoTestConnectSignaler*)arg;
    for (int i = 0; i < 3; i++) {
        usleep(5 * ST_UTIME_MILLISECONDS);
        pthread_kill(s->target_, SIGUSR1);
    }
    usleep(5 * ST_UTIME_MILLISECONDS);
    s->accepted_ = ::accept(s->lfd_, NULL, NULL);
    return NULL;
}

// A signal interrupts connect while it waits in the kernel, and the handler was installed without SA_RESTART, so
// connect fails with EINTR. st_connect retries it instead of failing, and the retry connects once the listener has
// room. ST's sockets are non-blocking, so connect returns at once and a signal never lands in it; connect waits in the
// kernel only when the socket lost O_NONBLOCK. The test clears the flag and connects to a Unix socket listener whose
// queue is full, where Linux makes a blocking connect wait for room, and signals it on purpose. macOS does not wait
// there. Locks in current behavior.
VOID TEST(IoConnectTest, ConnectRetriesWhenSignalInterruptsSystemCall)
{
    int lfd = socket(AF_UNIX, SOCK_STREAM, 0);
    ASSERT_NE(-1, lfd);
    st_netfd_t lstfd = NULL;
    StFdCleanup(lfd, lstfd);

    // An abstract address, so no file is left behind.
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path + 1, sizeof(addr.sun_path) - 1, "st-utest-connect-%d", (int)getpid());
    socklen_t addrlen = offsetof(struct sockaddr_un, sun_path) + 1 + strlen(addr.sun_path + 1);
    ASSERT_EQ(0, ::bind(lfd, (sockaddr*)&addr, addrlen));
    ASSERT_EQ(0, ::listen(lfd, 0));

    // With a backlog of 0, one queued client fills the queue.
    int queued = socket(AF_UNIX, SOCK_STREAM, 0);
    ASSERT_NE(-1, queued);
    st_netfd_t qstfd = NULL;
    StFdCleanup(queued, qstfd);
    ASSERT_EQ(0, ::connect(queued, (sockaddr*)&addr, addrlen));

    int cfd = socket(AF_UNIX, SOCK_STREAM, 0);
    ASSERT_NE(-1, cfd);
    st_netfd_t client = st_netfd_open_socket(cfd);
    StFdCleanup(cfd, client);
    ASSERT_TRUE(client != NULL);

    int flags = fcntl(cfd, F_GETFL);
    ASSERT_NE(-1, flags);
    ASSERT_NE(-1, fcntl(cfd, F_SETFL, flags & ~O_NONBLOCK));

    struct sigaction sa, old_sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = io_read_on_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    ASSERT_EQ(0, sigaction(SIGUSR1, &sa, &old_sa));

    IoTestConnectSignaler s;
    s.target_ = pthread_self();
    s.lfd_ = lfd;
    s.accepted_ = -1;
    io_read_signals = 0;
    pthread_t signaler;
    ASSERT_EQ(0, pthread_create(&signaler, NULL, io_connect_signaler_thread, &s));

    errno = 0;
    int r0 = st_connect(client, (sockaddr*)&addr, addrlen, ST_UTEST_TIMEOUT);
    int err = errno;

    pthread_join(signaler, NULL);
    sigaction(SIGUSR1, &old_sa, NULL);
    fcntl(cfd, F_SETFL, flags);
    int afd = s.accepted_;
    st_netfd_t astfd = NULL;
    StFdCleanup(afd, astfd);

    EXPECT_EQ(3, (int)io_read_signals);
    EXPECT_EQ(0, r0) << "errno=" << err;
    EXPECT_NE(-1, afd);
}
#endif

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The utest for writing a TCP stream, the way SRS sends every RTMP and HTTP-FLV message to a player with st_writev.
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// A slow player: shrink the server's send buffer, so a write of about 1 MB fills it and the player's receive buffer,
// and the server has to wait for the player to read. The receive buffer keeps its default size: on Linux a tiny
// receive window makes TCP fall back to zero-window probes hundreds of milliseconds apart.
static bool io_shrink_send_buffer(IoTestTcpPair& pair)
{
    int size = 4096;
    return setsockopt(st_netfd_fileno(pair.server_), SOL_SOCKET, SO_SNDBUF, &size, sizeof(size)) == 0;
}

// Messages as SRS sends them: each one is a 12-byte header iovec and a payload iovec. Both point into one byte
// pattern that doesn't repeat at buffer boundaries, so the player can compare what it got with data_, and a byte that
// is lost, sent twice or sent out of order shows up.
struct IoTestMessages {
    std::string data_;
    std::vector<struct iovec> iovs_;
    IoTestMessages(int nn_msgs, size_t payload) {
        size_t size = 12 + payload;
        data_.resize(nn_msgs * size);
        for (size_t i = 0; i < data_.size(); i++) {
            data_[i] = (char)(i % 251);
        }
        iovs_.resize(2 * nn_msgs);
        for (int i = 0; i < nn_msgs; i++) {
            iovs_[2 * i].iov_base = &data_[i * size];
            iovs_[2 * i].iov_len = 12;
            iovs_[2 * i + 1].iov_base = &data_[i * size + 12];
            iovs_[2 * i + 1].iov_len = payload;
        }
    }
};

// A player that starts reading after a delay, then reads until it has everything it expects or the stream ends.
struct IoTestPlayer {
    IoTestTcpPair* pair_;
    st_utime_t delay_;
    size_t expect_;
    std::string received_;
};

static void* io_player_coroutine(void* arg)
{
    IoTestPlayer* p = (IoTestPlayer*)arg;
    st_usleep(p->delay_);

    char buf[4096];
    while (p->received_.size() < p->expect_) {
        size_t size = std::min(sizeof(buf), p->expect_ - p->received_.size());
        ssize_t n = st_read(p->pair_->client_, buf, size, ST_UTEST_TIMEOUT);
        if (n <= 0) break;
        p->received_.append(buf, n);
    }
    return NULL;
}

static st_thread_t io_start_player(IoTestPlayer& p, IoTestTcpPair& pair, st_utime_t delay, size_t expect)
{
    p.pair_ = &pair;
    p.delay_ = delay;
    p.expect_ = expect;
    return st_thread_create(io_player_coroutine, &p, 1, 0);
}

// Read whatever the player has queued, until nothing more arrives.
static size_t io_drain(st_netfd_t stfd)
{
    size_t nn = 0;
    char buf[4096];
    ssize_t n;
    while ((n = st_read(stfd, buf, sizeof(buf), 10 * ST_UTIME_MILLISECONDS)) > 0) {
        nn += n;
    }
    return nn;
}

// On a healthy connection the kernel takes a whole message, header and payload, in one writev, and st_writev returns
// the total size without waiting. This is the common case for every message SRS sends. Locks in current behavior.
VOID TEST(IoWriteTest, WritevSendsMessageInOneCall)
{
    IoTestTcpPair pair;
    ASSERT_TRUE(io_tcp_pair(pair));

    IoTestMessages msgs(1, 1000);
    EXPECT_EQ((ssize_t)msgs.data_.size(), st_writev(pair.server_, &msgs.iovs_[0], 2, ST_UTEST_TIMEOUT));

    std::string buf(msgs.data_.size(), 0);
    EXPECT_EQ((ssize_t)buf.size(), st_read_fully(pair.client_, &buf[0], buf.size(), ST_UTEST_TIMEOUT));
    EXPECT_TRUE(buf == msgs.data_);
}

// SRS sends a few large video frames to a slow player. The socket send buffer fills, so the kernel takes only part of
// the frames: st_writev resumes from the middle of a buffer, waits until the player reads, and returns only when every
// byte is sent, in order. The player has to start reading before st_writev can return. Locks in current behavior.
VOID TEST(IoWriteTest, WritevWaitsForSlowPlayer)
{
    IoTestTcpPair pair;
    ASSERT_TRUE(io_tcp_pair(pair));
    ASSERT_TRUE(io_shrink_send_buffer(pair));

    IoTestMessages msgs(4, 256 * 1024);
    IoTestPlayer p;
    st_thread_t player = io_start_player(p, pair, 10 * ST_UTIME_MILLISECONDS, msgs.data_.size());
    ASSERT_TRUE(player != NULL);

    EXPECT_EQ((ssize_t)msgs.data_.size(), st_writev(pair.server_, &msgs.iovs_[0], msgs.iovs_.size(), ST_UTEST_TIMEOUT));
    EXPECT_GT((int)p.received_.size(), 0);

    st_thread_join(player, NULL);
    EXPECT_TRUE(p.received_ == msgs.data_);
}

// SRS merges many messages into one write to save syscalls: 128 messages are 256 iovecs. When the slow player makes
// the write partial, more iovecs remain than st_writev keeps on its stack, so it copies the rest into allocated memory
// and frees it when done. Every byte still arrives in order. Locks in current behavior.
VOID TEST(IoWriteTest, WritevWaitsForSlowPlayerWithMergedWrite)
{
    IoTestTcpPair pair;
    ASSERT_TRUE(io_tcp_pair(pair));
    ASSERT_TRUE(io_shrink_send_buffer(pair));

    IoTestMessages msgs(128, 8192);
    IoTestPlayer p;
    st_thread_t player = io_start_player(p, pair, 10 * ST_UTIME_MILLISECONDS, msgs.data_.size());
    ASSERT_TRUE(player != NULL);

    EXPECT_EQ((ssize_t)msgs.data_.size(), st_writev(pair.server_, &msgs.iovs_[0], msgs.iovs_.size(), ST_UTEST_TIMEOUT));
    EXPECT_GT((int)p.received_.size(), 0);

    st_thread_join(player, NULL);
    EXPECT_TRUE(p.received_ == msgs.data_);
}

struct IoTestBlockedWriter {
    IoTestTcpPair* pair_;
    IoTestMessages* msgs_;
    ssize_t nwrite_;
    int errno_;
};

static void* io_writev_coroutine(void* arg)
{
    IoTestBlockedWriter* w = (IoTestBlockedWriter*)arg;
    errno = 0;
    w->nwrite_ = st_writev(w->pair_->server_, &w->msgs_->iovs_[0], w->msgs_->iovs_.size(), ST_UTIME_NO_TIMEOUT);
    w->errno_ = errno;
    return NULL;
}

// A player closes its browser tab while SRS waits, with no timeout, for room to send the rest of the frames. The
// connection is reset, which wakes the writer, and st_writev fails with EPIPE or ECONNRESET, depending on the OS,
// instead of waiting forever. SIGPIPE is ignored by ST, so the server process survives. Locks in current behavior.
VOID TEST(IoWriteTest, WritevFailsWhenPlayerResetsMidStream)
{
    IoTestTcpPair pair;
    ASSERT_TRUE(io_tcp_pair(pair));
    ASSERT_TRUE(io_shrink_send_buffer(pair));

    IoTestMessages msgs(4, 256 * 1024);
    IoTestBlockedWriter w;
    w.pair_ = &pair;
    w.msgs_ = &msgs;
    w.nwrite_ = 0;
    w.errno_ = 0;
    st_thread_t writer = st_thread_create(io_writev_coroutine, &w, 1, 0);
    ASSERT_TRUE(writer != NULL);

    st_usleep(10 * ST_UTIME_MILLISECONDS);
    pair.reset_client();
    st_thread_join(writer, NULL);

    EXPECT_EQ(-1, w.nwrite_);
    EXPECT_TRUE(w.errno_ == EPIPE || w.errno_ == ECONNRESET) << "errno=" << w.errno_;
}

// The player stalls and never reads while SRS sends it a large keyframe. st_writev sends the header and part of the
// payload, then fails with ETIME, and SRS drops the connection. The -1 hides how many bytes were sent, although the
// player can still read some of them: a caller that needs the count uses st_writev_resid instead. Locks in current
// behavior.
VOID TEST(IoWriteTest, WritevTimesOutAndLosesTheSentCount)
{
    IoTestTcpPair pair;
    ASSERT_TRUE(io_tcp_pair(pair));
    ASSERT_TRUE(io_shrink_send_buffer(pair));

    IoTestMessages msgs(1, 1024 * 1024);
    errno = 0;
    EXPECT_EQ(-1, st_writev(pair.server_, &msgs.iovs_[0], msgs.iovs_.size(), 50 * ST_UTIME_MILLISECONDS));
    EXPECT_EQ(ETIME, errno);

    size_t sent = io_drain(pair.client_);
    EXPECT_GT((int)sent, 0);
    EXPECT_LT(sent, msgs.data_.size());
}

// SRS stops a connection while its coroutine waits, with no timeout, to send the rest of the frames to a stalled
// player. The interrupt wakes it and st_writev fails with EINTR. Locks in current behavior.
VOID TEST(IoWriteTest, WritevInterruptedWhenConnectionStops)
{
    IoTestTcpPair pair;
    ASSERT_TRUE(io_tcp_pair(pair));
    ASSERT_TRUE(io_shrink_send_buffer(pair));

    IoTestMessages msgs(4, 256 * 1024);
    IoTestBlockedWriter w;
    w.pair_ = &pair;
    w.msgs_ = &msgs;
    w.nwrite_ = 0;
    w.errno_ = 0;
    st_thread_t writer = st_thread_create(io_writev_coroutine, &w, 1, 0);
    ASSERT_TRUE(writer != NULL);

    st_usleep(10 * ST_UTIME_MILLISECONDS);
    st_thread_interrupt(writer);
    st_thread_join(writer, NULL);

    EXPECT_EQ(-1, w.nwrite_);
    EXPECT_EQ(EINTR, w.errno_);
}

// Another thread of the program signals the writing thread a few times, then reads everything as the player.
struct IoTestWriteSignaler {
    pthread_t target_;
    int player_fd_;
    size_t expect_;
    std::string received_;
};

static void* io_write_signaler_thread(void* arg)
{
    IoTestWriteSignaler* s = (IoTestWriteSignaler*)arg;
    for (int i = 0; i < 3; i++) {
        usleep(5 * ST_UTIME_MILLISECONDS);
        pthread_kill(s->target_, SIGUSR1);
    }
    usleep(5 * ST_UTIME_MILLISECONDS);

    char buf[4096];
    while (s->received_.size() < s->expect_) {
        struct pollfd pfd;
        pfd.fd = s->player_fd_;
        pfd.events = POLLIN;
        if (::poll(&pfd, 1, 1000) <= 0) break;
        ssize_t n = ::read(s->player_fd_, buf, sizeof(buf));
        if (n < 0 && errno == EAGAIN) continue;
        if (n <= 0) break;
        s->received_.append(buf, n);
    }
    return NULL;
}

// Write to the non-blocking socket until the send buffer and the player's receive buffer are full and the kernel takes
// no more, and keep what was written. The kernel moves the send buffer to the player in the background, so the first
// EAGAIN may leave room a moment later: on macOS each round moves only one send buffer, and on Linux a delayed ACK
// frees room up to 40 ms later. So keep writing after a short pause while room keeps appearing, and stop only when a
// pause longer than a delayed ACK frees no more. Fill the last bytes one at a time, or a later write that fits in the
// room left would be sent without waiting.
static bool io_fill_buffers(int fd, std::string& filled)
{
    char chunk[4096];
    memset(chunk, 'f', sizeof(chunk));
    bool settling = false;
    for (;;) {
        size_t size = filled.size();
        size_t sizes[] = {sizeof(chunk), 1};
        for (int i = 0; i < 2; i++) {
            ssize_t n;
            while ((n = st_utest_send(fd, chunk, sizes[i])) > 0) {
                filled.append(chunk, n);
            }
            if (n == 0 || !st_utest_would_block()) return false;
        }
        if (filled.size() == size && settling) return true;
        settling = filled.size() == size;
        usleep((settling ? 50 : 1) * ST_UTIME_MILLISECONDS);
    }
}

// A signal interrupts writev while it waits in the kernel for a stalled player, and the handler was installed without
// SA_RESTART, so writev fails with EINTR. st_writev retries it instead of failing, because only st_thread_interrupt
// means the writer should stop, and every byte arrives once the player reads. A blocking writev fails with EINTR only
// if it sent nothing yet, so the test first fills the send and receive buffers while the socket is non-blocking, then
// clears O_NONBLOCK so writev waits in the kernel, and signals it on purpose. Locks in current behavior.
VOID TEST(IoWriteTest, WritevRetriesWhenSignalInterruptsSystemCall)
{
    IoTestTcpPair pair;
    ASSERT_TRUE(io_tcp_pair(pair));
    ASSERT_TRUE(io_shrink_send_buffer(pair));

    int fd = st_netfd_fileno(pair.server_);
    std::string filled;
    ASSERT_TRUE(io_fill_buffers(fd, filled));

    int flags = fcntl(fd, F_GETFL);
    ASSERT_NE(-1, flags);
    ASSERT_NE(-1, fcntl(fd, F_SETFL, flags & ~O_NONBLOCK));

    struct sigaction sa, old_sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = io_read_on_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    ASSERT_EQ(0, sigaction(SIGUSR1, &sa, &old_sa));

    IoTestMessages msgs(1, 1000);
    IoTestWriteSignaler s;
    s.target_ = pthread_self();
    s.player_fd_ = st_netfd_fileno(pair.client_);
    s.expect_ = filled.size() + msgs.data_.size();
    io_read_signals = 0;
    pthread_t signaler;
    ASSERT_EQ(0, pthread_create(&signaler, NULL, io_write_signaler_thread, &s));

    errno = 0;
    ssize_t nwrite = st_writev(pair.server_, &msgs.iovs_[0], msgs.iovs_.size(), ST_UTEST_TIMEOUT);
    int err = errno;

    pthread_join(signaler, NULL);
    sigaction(SIGUSR1, &old_sa, NULL);
    fcntl(fd, F_SETFL, flags);

    EXPECT_EQ(3, (int)io_read_signals);
    EXPECT_EQ((ssize_t)msgs.data_.size(), nwrite) << "errno=" << err;
    ASSERT_EQ(s.expect_, s.received_.size());
    EXPECT_TRUE(s.received_.substr(0, filled.size()) == filled);
    EXPECT_TRUE(s.received_.substr(filled.size()) == msgs.data_);
}

// The same signal interrupts writev in st_writev_resid while the player is stalled. st_writev_resid retries it, sends
// the whole message once the player reads, and leaves the caller's iovec array used up. The test fills the buffers and
// clears O_NONBLOCK, as for st_writev. Locks in current behavior.
VOID TEST(IoWriteTest, WritevResidRetriesWhenSignalInterruptsSystemCall)
{
    IoTestTcpPair pair;
    ASSERT_TRUE(io_tcp_pair(pair));
    ASSERT_TRUE(io_shrink_send_buffer(pair));

    int fd = st_netfd_fileno(pair.server_);
    std::string filled;
    ASSERT_TRUE(io_fill_buffers(fd, filled));

    int flags = fcntl(fd, F_GETFL);
    ASSERT_NE(-1, flags);
    ASSERT_NE(-1, fcntl(fd, F_SETFL, flags & ~O_NONBLOCK));

    struct sigaction sa, old_sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = io_read_on_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    ASSERT_EQ(0, sigaction(SIGUSR1, &sa, &old_sa));

    IoTestMessages msgs(1, 1000);
    IoTestWriteSignaler s;
    s.target_ = pthread_self();
    s.player_fd_ = st_netfd_fileno(pair.client_);
    s.expect_ = filled.size() + msgs.data_.size();
    io_read_signals = 0;
    pthread_t signaler;
    ASSERT_EQ(0, pthread_create(&signaler, NULL, io_write_signaler_thread, &s));

    struct iovec* iov = &msgs.iovs_[0];
    int iov_size = (int)msgs.iovs_.size();
    errno = 0;
    int r0 = st_writev_resid(pair.server_, &iov, &iov_size, ST_UTEST_TIMEOUT);
    int err = errno;

    pthread_join(signaler, NULL);
    sigaction(SIGUSR1, &old_sa, NULL);
    fcntl(fd, F_SETFL, flags);

    EXPECT_EQ(3, (int)io_read_signals);
    EXPECT_EQ(0, r0) << "errno=" << err;
    EXPECT_EQ(0, iov_size);
    ASSERT_EQ(s.expect_, s.received_.size());
    EXPECT_TRUE(s.received_.substr(0, filled.size()) == filled);
    EXPECT_TRUE(s.received_.substr(filled.size()) == msgs.data_);
}

// The same signal interrupts sendto while it waits for a stalled peer. st_sendto retries it, and the message arrives
// once the peer reads. The test sends on the connected TCP pair with no address, because a datagram socket can't
// wait for room on every OS: macOS fails a full local datagram queue with ENOBUFS even when the socket blocks. It
// fills the buffers and clears O_NONBLOCK, as for st_writev. Locks in current behavior.
VOID TEST(IoWriteTest, SendtoRetriesWhenSignalInterruptsSystemCall)
{
    IoTestTcpPair pair;
    ASSERT_TRUE(io_tcp_pair(pair));
    ASSERT_TRUE(io_shrink_send_buffer(pair));

    int fd = st_netfd_fileno(pair.server_);
    std::string filled;
    ASSERT_TRUE(io_fill_buffers(fd, filled));

    int flags = fcntl(fd, F_GETFL);
    ASSERT_NE(-1, flags);
    ASSERT_NE(-1, fcntl(fd, F_SETFL, flags & ~O_NONBLOCK));

    struct sigaction sa, old_sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = io_read_on_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    ASSERT_EQ(0, sigaction(SIGUSR1, &sa, &old_sa));

    std::string data(1000, 'd');
    IoTestWriteSignaler s;
    s.target_ = pthread_self();
    s.player_fd_ = st_netfd_fileno(pair.client_);
    s.expect_ = filled.size() + data.size();
    io_read_signals = 0;
    pthread_t signaler;
    ASSERT_EQ(0, pthread_create(&signaler, NULL, io_write_signaler_thread, &s));

    errno = 0;
    int nwrite = st_sendto(pair.server_, data.data(), (int)data.size(), NULL, 0, ST_UTEST_TIMEOUT);
    int err = errno;

    pthread_join(signaler, NULL);
    sigaction(SIGUSR1, &old_sa, NULL);
    fcntl(fd, F_SETFL, flags);

    EXPECT_EQ(3, (int)io_read_signals);
    EXPECT_EQ((int)data.size(), nwrite) << "errno=" << err;
    ASSERT_EQ(s.expect_, s.received_.size());
    EXPECT_TRUE(s.received_.substr(0, filled.size()) == filled);
    EXPECT_TRUE(s.received_.substr(filled.size()) == data);
}

// The same signal interrupts sendmsg while it waits for a stalled peer. st_sendmsg retries it, and the header and
// payload buffers arrive as one message once the peer reads. Like st_sendto, the test uses the connected TCP pair,
// because macOS never makes a datagram send wait. Locks in current behavior.
VOID TEST(IoWriteTest, SendmsgRetriesWhenSignalInterruptsSystemCall)
{
    IoTestTcpPair pair;
    ASSERT_TRUE(io_tcp_pair(pair));
    ASSERT_TRUE(io_shrink_send_buffer(pair));

    int fd = st_netfd_fileno(pair.server_);
    std::string filled;
    ASSERT_TRUE(io_fill_buffers(fd, filled));

    int flags = fcntl(fd, F_GETFL);
    ASSERT_NE(-1, flags);
    ASSERT_NE(-1, fcntl(fd, F_SETFL, flags & ~O_NONBLOCK));

    struct sigaction sa, old_sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = io_read_on_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    ASSERT_EQ(0, sigaction(SIGUSR1, &sa, &old_sa));

    IoTestMessages msgs(1, 1000);
    struct msghdr msg;
    memset(&msg, 0, sizeof(msg));
    msg.msg_iov = &msgs.iovs_[0];
    msg.msg_iovlen = (int)msgs.iovs_.size();

    IoTestWriteSignaler s;
    s.target_ = pthread_self();
    s.player_fd_ = st_netfd_fileno(pair.client_);
    s.expect_ = filled.size() + msgs.data_.size();
    io_read_signals = 0;
    pthread_t signaler;
    ASSERT_EQ(0, pthread_create(&signaler, NULL, io_write_signaler_thread, &s));

    errno = 0;
    int nwrite = st_sendmsg(pair.server_, &msg, 0, ST_UTEST_TIMEOUT);
    int err = errno;

    pthread_join(signaler, NULL);
    sigaction(SIGUSR1, &old_sa, NULL);
    fcntl(fd, F_SETFL, flags);

    EXPECT_EQ(3, (int)io_read_signals);
    EXPECT_EQ((int)msgs.data_.size(), nwrite) << "errno=" << err;
    ASSERT_EQ(s.expect_, s.received_.size());
    EXPECT_TRUE(s.received_.substr(0, filled.size()) == filled);
    EXPECT_TRUE(s.received_.substr(filled.size()) == msgs.data_);
}

// st_writev_resid keeps the count that st_writev loses. When the stalled player makes it time out, the caller's iovec
// array points at what is still unsent: the rest of a partly sent buffer, then the untouched ones. When the player
// reads again, calling it with the same array sends exactly the rest, so the player gets the whole stream once and in
// order. Locks in current behavior.
VOID TEST(IoWriteTest, WritevResidResumesAfterTimeout)
{
    IoTestTcpPair pair;
    ASSERT_TRUE(io_tcp_pair(pair));
    ASSERT_TRUE(io_shrink_send_buffer(pair));

    IoTestMessages msgs(4, 256 * 1024);
    struct iovec* iov = &msgs.iovs_[0];
    int iov_size = (int)msgs.iovs_.size();

    errno = 0;
    EXPECT_EQ(-1, st_writev_resid(pair.server_, &iov, &iov_size, 50 * ST_UTIME_MILLISECONDS));
    EXPECT_EQ(ETIME, errno);
    ASSERT_GT(iov_size, 0);

    // The unsent bytes are a tail of the stream, starting inside a buffer or at its start.
    size_t unsent = 0;
    for (int i = 0; i < iov_size; i++) {
        unsent += iov[i].iov_len;
    }
    EXPECT_GT((int)unsent, 0);
    EXPECT_LT(unsent, msgs.data_.size());
    EXPECT_TRUE((char*)iov[0].iov_base == &msgs.data_[msgs.data_.size() - unsent]);

    IoTestPlayer p;
    st_thread_t player = io_start_player(p, pair, 0, msgs.data_.size());
    ASSERT_TRUE(player != NULL);

    EXPECT_EQ(0, st_writev_resid(pair.server_, &iov, &iov_size, ST_UTEST_TIMEOUT));
    EXPECT_EQ(0, iov_size);

    st_thread_join(player, NULL);
    EXPECT_TRUE(p.received_ == msgs.data_);
}

// SRS has nothing to send to a stalled player whose send and receive buffers are full: a message whose header and
// payload are both empty, no messages at all, or the empty tail st_writev_resid leaves once everything is sent. Each
// call returns 0 at once instead of waiting for the player, and the player receives only what was queued before.
// Locks in current behavior.
VOID TEST(IoWriteTest, WritevWithNothingToSendReturnsAtOnce)
{
    IoTestTcpPair pair;
    ASSERT_TRUE(io_tcp_pair(pair));
    ASSERT_TRUE(io_shrink_send_buffer(pair));
    std::string filled;
    ASSERT_TRUE(io_fill_buffers(st_netfd_fileno(pair.server_), filled));

    IoTestMessages msgs(1, 0);
    msgs.iovs_[0].iov_len = 0;
    st_utime_t timeout = 1000 * ST_UTIME_MILLISECONDS;

    st_utime_t starttime = st_utime();
    EXPECT_EQ(0, st_writev(pair.server_, &msgs.iovs_[0], 2, timeout));
    EXPECT_EQ(0, st_writev(pair.server_, &msgs.iovs_[0], 0, timeout));

    struct iovec* iov = &msgs.iovs_[0] + 2;
    int iov_size = 0;
    EXPECT_EQ(0, st_writev_resid(pair.server_, &iov, &iov_size, timeout));
    EXPECT_LT(st_utime() - starttime, 50 * ST_UTIME_MILLISECONDS);
    EXPECT_EQ(0, iov_size);
    EXPECT_TRUE(iov == &msgs.iovs_[0] + 2);

    EXPECT_EQ(filled.size(), io_drain(pair.client_));
}

// SRS writes a large buffer, such as an HTTP response body, with st_write. To a slow player the write is partial:
// st_write waits until the player reads, resumes from where the kernel stopped, and returns the full size. Locks in
// current behavior.
VOID TEST(IoWriteTest, WriteWaitsForSlowPlayer)
{
    IoTestTcpPair pair;
    ASSERT_TRUE(io_tcp_pair(pair));
    ASSERT_TRUE(io_shrink_send_buffer(pair));

    IoTestMessages msgs(1, 1024 * 1024);
    IoTestPlayer p;
    st_thread_t player = io_start_player(p, pair, 10 * ST_UTIME_MILLISECONDS, msgs.data_.size());
    ASSERT_TRUE(player != NULL);

    EXPECT_EQ((ssize_t)msgs.data_.size(), st_write(pair.server_, msgs.data_.data(), msgs.data_.size(), ST_UTEST_TIMEOUT));
    EXPECT_GT((int)p.received_.size(), 0);

    st_thread_join(player, NULL);
    EXPECT_TRUE(p.received_ == msgs.data_);
}

static void* io_write_coroutine(void* arg)
{
    IoTestBlockedWriter* w = (IoTestBlockedWriter*)arg;
    errno = 0;
    w->nwrite_ = st_write(w->pair_->server_, w->msgs_->data_.data(), w->msgs_->data_.size(), ST_UTIME_NO_TIMEOUT);
    w->errno_ = errno;
    return NULL;
}

// A player closes its browser tab while SRS waits, with no timeout, to send the rest of a large response with
// st_write. The reset wakes the writer, and st_write fails with EPIPE or ECONNRESET, depending on the OS. Locks in
// current behavior.
VOID TEST(IoWriteTest, WriteFailsWhenPlayerResetsMidStream)
{
    IoTestTcpPair pair;
    ASSERT_TRUE(io_tcp_pair(pair));
    ASSERT_TRUE(io_shrink_send_buffer(pair));

    IoTestMessages msgs(1, 1024 * 1024);
    IoTestBlockedWriter w;
    w.pair_ = &pair;
    w.msgs_ = &msgs;
    w.nwrite_ = 0;
    w.errno_ = 0;
    st_thread_t writer = st_thread_create(io_write_coroutine, &w, 1, 0);
    ASSERT_TRUE(writer != NULL);

    st_usleep(10 * ST_UTIME_MILLISECONDS);
    pair.reset_client();
    st_thread_join(writer, NULL);

    EXPECT_EQ(-1, w.nwrite_);
    EXPECT_TRUE(w.errno_ == EPIPE || w.errno_ == ECONNRESET) << "errno=" << w.errno_;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The utest for descriptor objects: how a descriptor is wrapped, waited on, closed and recycled, including the pipe
// SRS uses to turn a signal into an event for a coroutine.
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// A pipe whose read end is wrapped with st_netfd_open, the way SRS wraps its signal pipe. The write end stays a plain
// descriptor, like the one a signal handler writes to.
struct IoTestPipe {
    st_netfd_t reader_;
    int writer_;
    IoTestPipe() : reader_(NULL), writer_(-1) {
    }
    ~IoTestPipe() {
        if (reader_) st_netfd_close(reader_);
        if (writer_ >= 0) ::close(writer_);
    }
};

static bool io_pipe(IoTestPipe& p)
{
    int fds[2];
    if (pipe(fds) < 0) return false;

    p.writer_ = fds[1];
    if ((p.reader_ = st_netfd_open(fds[0])) == NULL) {
        ::close(fds[0]);
        return false;
    }
    return true;
}

struct IoTestSignalReader {
    st_netfd_t stfd_;
    int signo_;
    ssize_t nread_;
    int errno_;
};

static void* io_signal_reader_coroutine(void* arg)
{
    IoTestSignalReader* r = (IoTestSignalReader*)arg;
    errno = 0;
    r->nread_ = st_read(r->stfd_, &r->signo_, sizeof(r->signo_), ST_UTIME_NO_TIMEOUT);
    r->errno_ = errno;
    return NULL;
}

// SRS turns signals into events with a pipe: the handler writes the signal number to the write end, and a coroutine
// waits on the read end, wrapped with st_netfd_open. A pipe is not a socket, so ST makes it non-blocking with fcntl,
// and the coroutine waits instead of blocking the whole process. Locks in current behavior.
VOID TEST(IoNetfdTest, SignalPipeWakesWaitingCoroutine)
{
    IoTestPipe p;
    ASSERT_TRUE(io_pipe(p));
    EXPECT_TRUE(fcntl(st_netfd_fileno(p.reader_), F_GETFL) & O_NONBLOCK);

    IoTestSignalReader r;
    memset(&r, 0, sizeof(r));
    r.stfd_ = p.reader_;
    st_thread_t reader = st_thread_create(io_signal_reader_coroutine, &r, 1, 0);
    ASSERT_TRUE(reader != NULL);

    // The reader waits while the scheduler keeps running other coroutines.
    st_usleep(10 * ST_UTIME_MILLISECONDS);
    EXPECT_EQ(0, r.nread_);

    // A signal handler writes the signal number with a plain write.
    int signo = SIGHUP;
    ASSERT_EQ((ssize_t)sizeof(signo), ::write(p.writer_, &signo, sizeof(signo)));
    st_thread_join(reader, NULL);

    EXPECT_EQ((ssize_t)sizeof(signo), r.nread_);
    EXPECT_EQ(SIGHUP, r.signo_);
}

// Closing a descriptor while another coroutine still waits on it fails with EBUSY, and leaves the descriptor open and
// the waiter undisturbed, so the event system never holds a stale descriptor. SRS treats this as a bug and asserts
// (srs_close_stfd), so it must stop the waiter first; once the waiter is gone, the close succeeds. Locks in current
// behavior.
VOID TEST(IoNetfdTest, CloseWhileAnotherCoroutineWaitsIsRefused)
{
    StUtestPair p;
    ASSERT_TRUE(st_utest_pair_open(p));

    IoTestSignalReader r;
    memset(&r, 0, sizeof(r));
    r.stfd_ = p.stfd_;
    st_thread_t reader = st_thread_create(io_signal_reader_coroutine, &r, 1, 0);
    ASSERT_TRUE(reader != NULL);
    st_usleep(10 * ST_UTIME_MILLISECONDS);

    errno = 0;
    EXPECT_EQ(-1, st_netfd_close(p.stfd_));
    EXPECT_EQ(EBUSY, errno);

    // The descriptor is still open and the waiter still gets its data.
    int signo = SIGTERM;
    ASSERT_EQ((ssize_t)sizeof(signo), st_utest_send(p.peer_, &signo, sizeof(signo)));
    st_thread_join(reader, NULL);
    EXPECT_EQ((ssize_t)sizeof(signo), r.nread_);
    EXPECT_EQ(SIGTERM, r.signo_);

    EXPECT_EQ(0, st_netfd_close(p.stfd_));
    p.stfd_ = NULL;
}

// A server opens and closes connections all day, so ST recycles descriptor objects: a freed object is handed to the
// next st_netfd_open. Freeing an object twice, as an error path might, is ignored, so the object is recycled only once
// and two later descriptors never share it. Locks in current behavior.
VOID TEST(IoNetfdTest, FreedObjectIsRecycledOnce)
{
    int fds[2];
    ASSERT_EQ(0, st_utest_stream_pair(fds));

    st_netfd_t first = st_netfd_open_socket(fds[0]);
    ASSERT_TRUE(first != NULL);
    st_netfd_free(first);
    st_netfd_free(first);

    st_netfd_t reader = st_netfd_open_socket(fds[0]);
    st_netfd_t writer = st_netfd_open_socket(fds[1]);
    ASSERT_TRUE(reader != NULL);
    ASSERT_TRUE(writer != NULL);
    EXPECT_TRUE(reader == first);
    EXPECT_TRUE(writer != reader);

    EXPECT_EQ(0, st_netfd_close(reader));
    EXPECT_EQ(0, st_netfd_close(writer));
}

// Opening a descriptor that is already closed, as a caller with a stale fd might, fails with EBADF when ST makes it
// non-blocking, whether it is opened as a file or as a socket. The descriptor object goes back to the free list, so
// the next st_netfd_open reuses it. Locks in current behavior.
VOID TEST(IoNetfdTest, OpenClosedDescriptorFails)
{
    int fds[2];
    ASSERT_EQ(0, st_utest_stream_pair(fds));
    st_netfd_t first = st_netfd_open_socket(fds[0]);
    ASSERT_TRUE(first != NULL);
    st_netfd_free(first);

    int closed[2];
    ASSERT_EQ(0, st_utest_stream_pair(closed));
    ::close(closed[0]);
    ::close(closed[1]);

    errno = 0;
    EXPECT_TRUE(st_netfd_open(closed[0]) == NULL);
    EXPECT_EQ(EBADF, errno);

    errno = 0;
    EXPECT_TRUE(st_netfd_open_socket(closed[1]) == NULL);
    EXPECT_EQ(EBADF, errno);

    st_netfd_t reader = st_netfd_open_socket(fds[0]);
    ASSERT_TRUE(reader != NULL);
    EXPECT_TRUE(reader == first);

    EXPECT_EQ(0, st_netfd_close(reader));
    ::close(fds[1]);
}

static std::vector<std::string> _io_freed_specifics;

static void io_specific_destructor(void* arg)
{
    std::string* v = (std::string*)arg;
    _io_freed_specifics.push_back(*v);
    delete v;
}

// A server keeps per-connection state on the descriptor with st_netfd_setspecific. Setting the same value again keeps
// it, replacing it frees the old value, and closing the descriptor frees the last one, so the state never leaks and is
// never freed while in use. Locks in current behavior.
VOID TEST(IoNetfdTest, DescriptorDataIsFreedWhenReplacedOrClosed)
{
    _io_freed_specifics.clear();

    StUtestPair p;
    ASSERT_TRUE(st_utest_pair_open(p));

    std::string* first = new std::string("first");
    st_netfd_setspecific(p.stfd_, first, io_specific_destructor);
    st_netfd_setspecific(p.stfd_, first, io_specific_destructor);
    EXPECT_TRUE(_io_freed_specifics.empty());

    st_netfd_setspecific(p.stfd_, new std::string("second"), io_specific_destructor);
    ASSERT_EQ(1, (int)_io_freed_specifics.size());
    EXPECT_EQ("first", _io_freed_specifics[0]);
    EXPECT_EQ("second", *(std::string*)st_netfd_getspecific(p.stfd_));

    EXPECT_EQ(0, st_netfd_close(p.stfd_));
    p.stfd_ = NULL;
    ASSERT_EQ(2, (int)_io_freed_specifics.size());
    EXPECT_EQ("second", _io_freed_specifics[1]);
}

// st_init raises the soft limit of open descriptors as far as it may, so a server can hold many connections, and
// st_getfdlimit reports that limit. On Linux the soft limit becomes the hard limit. On macOS an unlimited hard limit
// reads as negative, so ST keeps the soft limit. Locks in current behavior.
VOID TEST(IoNetfdTest, FdLimitIsRaisedAtInit)
{
    struct rlimit rlim;
    ASSERT_EQ(0, getrlimit(RLIMIT_NOFILE, &rlim));

    EXPECT_GT(st_getfdlimit(), 0);
    EXPECT_EQ((int)rlim.rlim_cur, st_getfdlimit());
#ifdef __linux__
    EXPECT_EQ(rlim.rlim_max, rlim.rlim_cur);
#endif
}

// The original ST serialized accept across processes; this fork dropped that, so st_netfd_serialize_accept is a no-op
// kept for source compatibility. It succeeds and the listener accepts as before. Locks in current behavior.
VOID TEST(IoNetfdTest, SerializeAcceptIsNoOp)
{
    struct sockaddr_in addr;
    st_netfd_t listener = io_tcp_listen(addr, 8);
    ASSERT_TRUE(listener != NULL);
    EXPECT_EQ(0, st_netfd_serialize_accept(listener));

    int cfd = socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_GE(cfd, 0);
    ASSERT_EQ(0, ::connect(cfd, (sockaddr*)&addr, sizeof(addr)));

    st_netfd_t client = st_accept(listener, NULL, NULL, ST_UTEST_TIMEOUT);
    EXPECT_TRUE(client != NULL);
    if (client) st_netfd_close(client);
    ::close(cfd);
    EXPECT_EQ(0, st_netfd_close(listener));
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The utest for st_open: a FIFO or another special file opened non-blocking, so a coroutine waits on it like on a
// socket. For example, a server that takes commands from a named pipe, or reads a stream another program writes into
// a FIFO.
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// A fresh temporary directory; every path made with path() is removed with it at the end.
struct IoTestDir {
    char dir_[64];
    std::vector<std::string> paths_;
    IoTestDir() {
        dir_[0] = 0;
    }
    ~IoTestDir() {
        for (size_t i = 0; i < paths_.size(); i++) ::unlink(paths_[i].c_str());
        if (dir_[0]) ::rmdir(dir_);
    }
    std::string path(const char* name) {
        paths_.push_back(std::string(dir_) + "/" + name);
        return paths_.back();
    }
};

static bool io_tmpdir(IoTestDir& d)
{
    strcpy(d.dir_, "/tmp/st-utest-XXXXXX");
    if (!mkdtemp(d.dir_)) {
        d.dir_[0] = 0;
        return false;
    }
    return true;
}

static bool io_fifo(IoTestDir& d, std::string& path)
{
    if (!io_tmpdir(d)) return false;
    path = d.path("fifo");
    return mkfifo(path.c_str(), 0600) == 0;
}

struct IoTestFifoReader {
    st_netfd_t fd_;
    st_utime_t timeout_;
    char buf_[64];
    ssize_t nread_;
    int errno_;
    bool done_;
};

static void* io_fifo_reader_coroutine(void* arg)
{
    IoTestFifoReader* r = (IoTestFifoReader*)arg;
    errno = 0;
    r->nread_ = st_read(r->fd_, r->buf_, sizeof(r->buf_), r->timeout_);
    r->errno_ = errno;
    r->done_ = true;
    return NULL;
}

// A server takes commands from a named pipe: it opens the FIFO for reading and a coroutine waits for a command while
// the scheduler keeps running. Opening the read end does not wait for a writer, because st_open adds O_NONBLOCK. When a
// tool opens the write end and writes a command, the reader wakes. When the tool closes the write end, a waiting reader
// wakes with 0, end of file, on Linux. On macOS the kernel reports no readiness for that end of file, neither to kqueue
// nor to poll, so the reader waits until its timeout, and only a read after that returns 0. A reader on macOS must
// wait with a timeout. Locks in current behavior.
VOID TEST(IoOpenTest, FifoReaderWaitsForCommandThenSeesEof)
{
    IoTestDir d;
    std::string path;
    ASSERT_TRUE(io_fifo(d, path));

    st_netfd_t reader = st_open(path.c_str(), O_RDONLY, 0);
    ASSERT_TRUE(reader != NULL);
    StStfdCleanup(reader);
    EXPECT_TRUE(fcntl(st_netfd_fileno(reader), F_GETFL) & O_NONBLOCK);

    st_netfd_t writer = st_open(path.c_str(), O_WRONLY, 0);
    ASSERT_TRUE(writer != NULL);

    IoTestFifoReader r;
    memset(&r, 0, sizeof(r));
    r.fd_ = reader;
    r.timeout_ = ST_UTIME_NO_TIMEOUT;
    st_thread_t trd = st_thread_create(io_fifo_reader_coroutine, &r, 1, 0);
    ASSERT_TRUE(trd != NULL);

    // The reader waits for a command.
    st_usleep(10 * ST_UTIME_MILLISECONDS);
    EXPECT_FALSE(r.done_);

    EXPECT_EQ(6, st_write(writer, "reload", 6, ST_UTEST_TIMEOUT));
    st_thread_join(trd, NULL);
    EXPECT_EQ(6, r.nread_);
    EXPECT_EQ("reload", std::string(r.buf_, 6));

    // The reader waits for the next command, then the tool closes the write end.
    memset(&r, 0, sizeof(r));
    r.fd_ = reader;
    r.timeout_ = ST_UTEST_TIMEOUT;
    trd = st_thread_create(io_fifo_reader_coroutine, &r, 1, 0);
    ASSERT_TRUE(trd != NULL);
    st_usleep(10 * ST_UTIME_MILLISECONDS);
    EXPECT_FALSE(r.done_);

    EXPECT_EQ(0, st_netfd_close(writer));
    st_thread_join(trd, NULL);
#ifdef __APPLE__
    EXPECT_EQ(-1, r.nread_);
    EXPECT_EQ(ETIME, r.errno_);

    char buf[16];
    EXPECT_EQ(0, st_read(reader, buf, sizeof(buf), ST_UTEST_TIMEOUT));
#else
    EXPECT_EQ(0, r.nread_);
#endif
}

// Before any writer has opened the FIFO, a read does not wait: it returns 0, end of file, at once. So a reader can't
// tell "no writer yet" from "the writer is gone", and must not take this 0 as the end of the stream. Locks in current
// behavior.
VOID TEST(IoOpenTest, FifoReaderSeesEofBeforeAnyWriter)
{
    IoTestDir d;
    std::string path;
    ASSERT_TRUE(io_fifo(d, path));

    st_netfd_t reader = st_open(path.c_str(), O_RDONLY, 0);
    ASSERT_TRUE(reader != NULL);
    StStfdCleanup(reader);

    char buf[16];
    st_utime_t starttime = st_utime();
    EXPECT_EQ(0, st_read(reader, buf, sizeof(buf), ST_UTEST_TIMEOUT));
    EXPECT_LT(st_utime() - starttime, ST_UTEST_TIMEOUT);
}

// Opening the write end before any reader fails at once with ENXIO, because st_open adds O_NONBLOCK; a blocking open
// would wait for a reader instead. A program that feeds a FIFO must open it after the reader, or retry. Locks in
// current behavior.
VOID TEST(IoOpenTest, FifoWriterWithoutReaderFails)
{
    IoTestDir d;
    std::string path;
    ASSERT_TRUE(io_fifo(d, path));

    errno = 0;
    EXPECT_TRUE(st_open(path.c_str(), O_WRONLY, 0) == NULL);
    EXPECT_EQ(ENXIO, errno);
}

// With a writer open but quiet, a read waits for data and fails with ETIME when the timeout expires. Locks in current
// behavior.
VOID TEST(IoOpenTest, FifoReadTimesOut)
{
    IoTestDir d;
    std::string path;
    ASSERT_TRUE(io_fifo(d, path));

    st_netfd_t reader = st_open(path.c_str(), O_RDONLY, 0);
    ASSERT_TRUE(reader != NULL);
    StStfdCleanup(reader);
    st_netfd_t writer = st_open(path.c_str(), O_WRONLY, 0);
    ASSERT_TRUE(writer != NULL);
    StStfdCleanup(writer);

    char buf[16];
    errno = 0;
    EXPECT_EQ(-1, st_read(reader, buf, sizeof(buf), 10 * ST_UTIME_MILLISECONDS));
    EXPECT_EQ(ETIME, errno);
}

struct IoTestFifoConsumer {
    st_netfd_t fd_;
    std::string data_;
};

// A consumer that starts late and reads in small pieces, until end of file, or on macOS until the timeout, because
// macOS does not wake a FIFO reader when the writer closes.
static void* io_fifo_consumer_coroutine(void* arg)
{
    IoTestFifoConsumer* c = (IoTestFifoConsumer*)arg;
    st_usleep(10 * ST_UTIME_MILLISECONDS);

    char buf[4096];
    ssize_t n;
    while ((n = st_read(c->fd_, buf, sizeof(buf), ST_UTEST_TIMEOUT)) > 0) {
        c->data_.append(buf, n);
    }
    return NULL;
}

// A producer writes a stream into a FIFO faster than its consumer reads, like a transcoder feeding a slow reader. The
// FIFO holds far less than 1 MB, so st_write waits for the consumer to drain it, coroutine by coroutine, instead of
// blocking the process, and every byte arrives in order. Locks in current behavior.
VOID TEST(IoOpenTest, FifoWriterWaitsForSlowReader)
{
    IoTestDir d;
    std::string path;
    ASSERT_TRUE(io_fifo(d, path));

    st_netfd_t reader = st_open(path.c_str(), O_RDONLY, 0);
    ASSERT_TRUE(reader != NULL);
    StStfdCleanup(reader);
    st_netfd_t writer = st_open(path.c_str(), O_WRONLY, 0);
    ASSERT_TRUE(writer != NULL);

    IoTestFifoConsumer c;
    c.fd_ = reader;
    st_thread_t trd = st_thread_create(io_fifo_consumer_coroutine, &c, 1, 0);
    ASSERT_TRUE(trd != NULL);

    std::string stream(1024 * 1024, 0);
    for (size_t i = 0; i < stream.size(); i++) stream[i] = (char)(i % 251);
    EXPECT_EQ((ssize_t)stream.size(), st_write(writer, stream.data(), stream.size(), ST_UTEST_TIMEOUT));

    EXPECT_EQ(0, st_netfd_close(writer));
    st_thread_join(trd, NULL);
    EXPECT_EQ(stream.size(), c.data_.size());
    EXPECT_TRUE(stream == c.data_);
}

// Opening a path that does not exist fails with ENOENT and returns NULL, like open. Locks in current behavior.
VOID TEST(IoOpenTest, OpenMissingPathFails)
{
    IoTestDir d;
    ASSERT_TRUE(io_tmpdir(d));

    errno = 0;
    EXPECT_TRUE(st_open(d.path("missing").c_str(), O_RDONLY, 0) == NULL);
    EXPECT_EQ(ENOENT, errno);
}

// A regular file is always ready, so st_open on it works but never waits: writes and reads complete at once, and a
// read at the end returns 0 instead of waiting for more data. ST can't make disk I/O non-blocking, and a slow disk
// still blocks the whole process; st_open is meant for FIFOs, devices and ttys. Locks in current behavior.
VOID TEST(IoOpenTest, RegularFileNeverWaits)
{
    IoTestDir d;
    ASSERT_TRUE(io_tmpdir(d));
    std::string path = d.path("file");

    st_netfd_t fd = st_open(path.c_str(), O_RDWR | O_CREAT | O_EXCL, 0600);
    ASSERT_TRUE(fd != NULL);
    StStfdCleanup(fd);

    EXPECT_EQ(5, st_write(fd, "hello", 5, ST_UTEST_TIMEOUT));
    ASSERT_EQ(0, lseek(st_netfd_fileno(fd), 0, SEEK_SET));

    char buf[16];
    EXPECT_EQ(5, st_read(fd, buf, sizeof(buf), ST_UTEST_TIMEOUT));
    EXPECT_EQ("hello", std::string(buf, 5));

    st_utime_t starttime = st_utime();
    EXPECT_EQ(0, st_read(fd, buf, sizeof(buf), ST_UTEST_TIMEOUT));
    EXPECT_LT(st_utime() - starttime, ST_UTEST_TIMEOUT);
}
