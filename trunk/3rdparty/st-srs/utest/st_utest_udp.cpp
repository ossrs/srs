/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2013-2026 The SRS Authors */

#include <st_utest.hpp>

#include <st.h>
#include <errno.h>
#include <fcntl.h>
#ifndef _WIN32
#include <pthread.h>
#endif
#include <signal.h>
#ifndef _WIN32
#include <unistd.h>
#endif
#include <string.h>
#include <string>

#ifndef _WIN32
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/uio.h>
#endif

#define ST_UTIME_MILLISECONDS 1000
#define ST_UTEST_TIMEOUT (100 * ST_UTIME_MILLISECONDS)

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The utest for UDP datagrams, the way SRS receives and sends WebRTC, SRT and GB28181 packets with st_recvfrom and
// st_sendto on one listening socket shared by many peers.
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// A UDP socket bound to an ephemeral loopback port, and the address it is bound to.
struct IoTestUdpSocket {
    st_netfd_t stfd_;
    struct sockaddr_in addr_;
    IoTestUdpSocket() : stfd_(NULL) {
        memset(&addr_, 0, sizeof(addr_));
    }
    ~IoTestUdpSocket() {
        if (stfd_) st_netfd_close(stfd_);
    }
};

static bool io_udp_socket(IoTestUdpSocket& s)
{
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return false;

    s.addr_.sin_family = AF_INET;
    s.addr_.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    s.addr_.sin_port = 0;

    socklen_t addrlen = sizeof(s.addr_);
    if (::bind(fd, (sockaddr*)&s.addr_, sizeof(s.addr_)) < 0 || getsockname(fd, (sockaddr*)&s.addr_, &addrlen) < 0) {
        st_utest_close(fd);
        return false;
    }

    s.stfd_ = st_netfd_open_socket(fd);
    if (!s.stfd_) st_utest_close(fd);
    return s.stfd_ != NULL;
}

static int io_udp_send(IoTestUdpSocket& from, IoTestUdpSocket& to, const void* data, int size)
{
    return st_sendto(from.stfd_, data, size, (sockaddr*)&to.addr_, sizeof(to.addr_), ST_UTEST_TIMEOUT);
}

// Two WebRTC players send to the same SRS UDP port. st_sendto sends each datagram whole, and st_recvfrom returns them
// one at a time, each with its own sender's address: SRS tells peers apart by that address. Locks in current behavior.
VOID TEST(IoUdpTest, RecvfromReportsEachSender)
{
    IoTestUdpSocket server, player1, player2;
    ASSERT_TRUE(io_udp_socket(server));
    ASSERT_TRUE(io_udp_socket(player1));
    ASSERT_TRUE(io_udp_socket(player2));

    EXPECT_EQ(5, io_udp_send(player1, server, "hello", 5));
    EXPECT_EQ(5, io_udp_send(player2, server, "world", 5));

    const IoTestUdpSocket* senders[] = {&player1, &player2};
    const char* payloads[] = {"hello", "world"};
    for (int i = 0; i < 2; i++) {
        char buf[64] = {0};
        struct sockaddr_in from;
        int fromlen = sizeof(from);
        EXPECT_EQ(5, st_recvfrom(server.stfd_, buf, sizeof(buf), (sockaddr*)&from, &fromlen, ST_UTEST_TIMEOUT));
        EXPECT_STREQ(payloads[i], buf);
        EXPECT_EQ((int)sizeof(from), fromlen);
        EXPECT_EQ(senders[i]->addr_.sin_port, from.sin_port);
        EXPECT_EQ(senders[i]->addr_.sin_addr.s_addr, from.sin_addr.s_addr);
    }
}

struct IoTestUdpReceiver {
    IoTestUdpSocket* server_;
    char buf_[64];
    int nread_;
    int errno_;
};

static void* io_recvfrom_coroutine(void* arg)
{
    IoTestUdpReceiver* r = (IoTestUdpReceiver*)arg;
    memset(r->buf_, 0, sizeof(r->buf_));
    errno = 0;
    r->nread_ = st_recvfrom(r->server_->stfd_, r->buf_, sizeof(r->buf_), NULL, NULL, ST_UTIME_NO_TIMEOUT);
    r->errno_ = errno;
    return NULL;
}

// The SRS UDP listener waits with no timeout until a packet arrives. st_recvfrom parks the coroutine, and the
// datagram sent while it waits wakes it with the data. Locks in current behavior.
VOID TEST(IoUdpTest, RecvfromWaitsForDatagram)
{
    IoTestUdpSocket server, player;
    ASSERT_TRUE(io_udp_socket(server));
    ASSERT_TRUE(io_udp_socket(player));

    IoTestUdpReceiver r;
    r.server_ = &server;
    st_thread_t receiver = st_thread_create(io_recvfrom_coroutine, &r, 1, 0);
    ASSERT_TRUE(receiver != NULL);

    // Let the receiver find the socket empty and wait.
    st_usleep(10 * ST_UTIME_MILLISECONDS);
    EXPECT_EQ(5, io_udp_send(player, server, "hello", 5));
    st_thread_join(receiver, NULL);

    EXPECT_EQ(5, r.nread_);
    EXPECT_STREQ("hello", r.buf_);
}

// No packet arrives before the timeout: st_recvfrom fails with ETIME, the way a WebRTC session detects a silent
// peer. Locks in current behavior.
VOID TEST(IoUdpTest, RecvfromTimesOut)
{
    IoTestUdpSocket server;
    ASSERT_TRUE(io_udp_socket(server));

    char buf[64];
    errno = 0;
    EXPECT_EQ(-1, st_recvfrom(server.stfd_, buf, sizeof(buf), NULL, NULL, 50 * ST_UTIME_MILLISECONDS));
    EXPECT_EQ(ETIME, errno);
}

// SRS stops a UDP listener on reload or shutdown by interrupting its coroutine, which waits in st_recvfrom with no
// timeout. st_recvfrom fails with EINTR, and the socket closes without EBUSY because no coroutine waits on it any
// more. Locks in current behavior.
VOID TEST(IoUdpTest, RecvfromInterruptedWhenListenerStops)
{
    IoTestUdpSocket server;
    ASSERT_TRUE(io_udp_socket(server));

    IoTestUdpReceiver r;
    r.server_ = &server;
    st_thread_t receiver = st_thread_create(io_recvfrom_coroutine, &r, 1, 0);
    ASSERT_TRUE(receiver != NULL);

    st_usleep(10 * ST_UTIME_MILLISECONDS);
    st_thread_interrupt(receiver);
    st_thread_join(receiver, NULL);

    EXPECT_EQ(-1, r.nread_);
    EXPECT_EQ(EINTR, r.errno_);
    EXPECT_EQ(0, st_netfd_close(server.stfd_));
    server.stfd_ = NULL;
}

// A datagram larger than the buffer is truncated to the buffer size and the rest is dropped, by UDP semantics: the
// next st_recvfrom returns the next datagram, not the tail of this one. SRS sizes its UDP buffer for the largest
// packet it accepts. Locks in current behavior.
VOID TEST(IoUdpTest, RecvfromTruncatesLargeDatagram)
{
    IoTestUdpSocket server, player;
    ASSERT_TRUE(io_udp_socket(server));
    ASSERT_TRUE(io_udp_socket(player));

    std::string large(1500, 'x');
    EXPECT_EQ(1500, io_udp_send(player, server, large.data(), large.size()));
    EXPECT_EQ(4, io_udp_send(player, server, "next", 4));

    char buf[100];
    EXPECT_EQ(100, st_recvfrom(server.stfd_, buf, sizeof(buf), NULL, NULL, ST_UTEST_TIMEOUT));
    EXPECT_EQ(std::string(100, 'x'), std::string(buf, 100));

    memset(buf, 0, sizeof(buf));
    EXPECT_EQ(4, st_recvfrom(server.stfd_, buf, sizeof(buf), NULL, NULL, ST_UTEST_TIMEOUT));
    EXPECT_STREQ("next", buf);
}

// An empty datagram is a valid UDP packet: st_recvfrom returns 0 for it, which is not an EOF as on TCP, and the
// socket keeps working. SRS's UDP listener logs it as a read error and goes on. Locks in current behavior.
VOID TEST(IoUdpTest, RecvfromReturnsZeroForEmptyDatagram)
{
    IoTestUdpSocket server, player;
    ASSERT_TRUE(io_udp_socket(server));
    ASSERT_TRUE(io_udp_socket(player));

    EXPECT_EQ(0, io_udp_send(player, server, "", 0));
    EXPECT_EQ(5, io_udp_send(player, server, "hello", 5));

    char buf[64] = {0};
    EXPECT_EQ(0, st_recvfrom(server.stfd_, buf, sizeof(buf), NULL, NULL, ST_UTEST_TIMEOUT));
    EXPECT_EQ(5, st_recvfrom(server.stfd_, buf, sizeof(buf), NULL, NULL, ST_UTEST_TIMEOUT));
    EXPECT_STREQ("hello", buf);
}

// A datagram larger than UDP allows is a hard error: st_sendto fails at once with EMSGSIZE instead of waiting for
// the socket to become writable. Locks in current behavior.
VOID TEST(IoUdpTest, SendtoOversizedDatagramFails)
{
    IoTestUdpSocket server, player;
    ASSERT_TRUE(io_udp_socket(server));
    ASSERT_TRUE(io_udp_socket(player));

    std::string huge(65536, 'x');
    errno = 0;
    EXPECT_EQ(-1, st_sendto(player.stfd_, huge.data(), huge.size(), (sockaddr*)&server.addr_, sizeof(server.addr_),
        ST_UTIME_NO_TIMEOUT));
    EXPECT_EQ(EMSGSIZE, errno);
}

// A connected UDP socket sends to a port where nothing listens, like a client whose server is down. The kernel
// reports the ICMP port unreachable as ECONNREFUSED: st_recvfrom, called for the reply, fails with it instead of
// waiting for the timeout. Locks in current behavior.
VOID TEST(IoUdpTest, RecvfromFailsWhenPeerRefuses)
{
    // Take a free port, then close it, so nothing listens there.
    IoTestUdpSocket gone;
    ASSERT_TRUE(io_udp_socket(gone));
    st_netfd_close(gone.stfd_);
    gone.stfd_ = NULL;

    IoTestUdpSocket client;
    ASSERT_TRUE(io_udp_socket(client));
    ASSERT_EQ(0, ::connect(st_netfd_fileno(client.stfd_), (sockaddr*)&gone.addr_, sizeof(gone.addr_)));

    EXPECT_EQ(5, st_sendto(client.stfd_, "hello", 5, NULL, 0, ST_UTEST_TIMEOUT));

    char buf[64];
    errno = 0;
    EXPECT_EQ(-1, st_recvfrom(client.stfd_, buf, sizeof(buf), NULL, NULL, ST_UTEST_TIMEOUT));
    EXPECT_EQ(ECONNREFUSED, errno);
}

#ifndef _WIN32 // POSIX only: signals, fcntl and pthread
static volatile sig_atomic_t io_udp_signals = 0;

static void io_udp_on_signal(int signo)
{
    io_udp_signals++;
}

// Another thread of the program signals the receiving thread a few times, then the player sends one datagram.
struct IoTestUdpSignaler {
    pthread_t target_;
    IoTestUdpSocket* player_;
    IoTestUdpSocket* server_;
};

static void* io_udp_signaler_thread(void* arg)
{
    IoTestUdpSignaler* s = (IoTestUdpSignaler*)arg;
    for (int i = 0; i < 3; i++) {
        usleep(5 * ST_UTIME_MILLISECONDS);
        pthread_kill(s->target_, SIGUSR1);
    }
    usleep(5 * ST_UTIME_MILLISECONDS);
    ::sendto(st_netfd_fileno(s->player_->stfd_), "hello", 5, 0, (sockaddr*)&s->server_->addr_, sizeof(s->server_->addr_));
    return NULL;
}

// A signal interrupts recvfrom while it waits in the kernel, and the handler was installed without SA_RESTART, so
// recvfrom fails with EINTR. st_recvfrom retries it instead of failing, because only st_thread_interrupt means the
// listener should stop, and the retry returns the datagram the player sends later, with the player's address. ST's
// sockets are non-blocking, so recvfrom returns at once and a signal almost never lands in it; recvfrom waits in the
// kernel only when the socket lost O_NONBLOCK. The test clears the flag so recvfrom waits, and signals it on purpose.
// Locks in current behavior.
VOID TEST(IoUdpTest, RecvfromRetriesWhenSignalInterruptsSystemCall)
{
    IoTestUdpSocket server, player;
    ASSERT_TRUE(io_udp_socket(server));
    ASSERT_TRUE(io_udp_socket(player));

    int fd = st_netfd_fileno(server.stfd_);
    int flags = fcntl(fd, F_GETFL);
    ASSERT_NE(-1, flags);
    ASSERT_NE(-1, fcntl(fd, F_SETFL, flags & ~O_NONBLOCK));

    struct sigaction sa, old_sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = io_udp_on_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    ASSERT_EQ(0, sigaction(SIGUSR1, &sa, &old_sa));

    IoTestUdpSignaler s;
    s.target_ = pthread_self();
    s.player_ = &player;
    s.server_ = &server;
    io_udp_signals = 0;
    pthread_t signaler;
    ASSERT_EQ(0, pthread_create(&signaler, NULL, io_udp_signaler_thread, &s));

    char buf[64] = {0};
    struct sockaddr_in from;
    int fromlen = sizeof(from);
    errno = 0;
    int nread = st_recvfrom(server.stfd_, buf, sizeof(buf), (sockaddr*)&from, &fromlen, ST_UTEST_TIMEOUT);
    int err = errno;

    pthread_join(signaler, NULL);
    sigaction(SIGUSR1, &old_sa, NULL);
    fcntl(fd, F_SETFL, flags);

    EXPECT_EQ(3, (int)io_udp_signals);
    EXPECT_EQ(5, nread) << "errno=" << err;
    EXPECT_STREQ("hello", buf);
    EXPECT_EQ(player.addr_.sin_port, from.sin_port);
}
#endif

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The utest for st_recvmsg and st_sendmsg: datagram I/O through a msghdr, which adds what st_recvfrom and st_sendto
// can't do: scatter and gather buffers, control messages, and the flags the kernel reports for each datagram.
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// A msghdr with one buffer and no address, for a socket that is connected or doesn't need the peer's address.
static void io_msghdr(struct msghdr& msg, struct iovec& iov, void* buf, size_t size)
{
    iov.iov_base = buf;
    iov.iov_len = size;
    memset(&msg, 0, sizeof(msg));
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
}

// An RTP packet built from two buffers, the fixed header and the payload, the way SRS once sent WebRTC media with
// srs_sendmsg. st_sendmsg gathers both into one datagram, and st_recvmsg scatters it back into a header buffer and a
// payload buffer, with the sender's address in msg_name. Locks in current behavior.
VOID TEST(IoUdpMsgTest, SendmsgGathersAndRecvmsgScatters)
{
    IoTestUdpSocket server, player;
    ASSERT_TRUE(io_udp_socket(server));
    ASSERT_TRUE(io_udp_socket(player));

    char header[12], payload[100];
    memset(header, 'h', sizeof(header));
    memset(payload, 'p', sizeof(payload));
    struct iovec out[2] = {{header, sizeof(header)}, {payload, sizeof(payload)}};
    struct msghdr sent;
    memset(&sent, 0, sizeof(sent));
    sent.msg_name = &server.addr_;
    sent.msg_namelen = sizeof(server.addr_);
    sent.msg_iov = out;
    sent.msg_iovlen = 2;
    EXPECT_EQ(112, st_sendmsg(player.stfd_, &sent, 0, ST_UTEST_TIMEOUT));

    char rheader[12] = {0}, rpayload[200] = {0};
    struct iovec in[2] = {{rheader, sizeof(rheader)}, {rpayload, sizeof(rpayload)}};
    struct sockaddr_in from;
    struct msghdr received;
    memset(&received, 0, sizeof(received));
    received.msg_name = &from;
    received.msg_namelen = sizeof(from);
    received.msg_iov = in;
    received.msg_iovlen = 2;
    EXPECT_EQ(112, st_recvmsg(server.stfd_, &received, 0, ST_UTEST_TIMEOUT));
    EXPECT_EQ(std::string(12, 'h'), std::string(rheader, 12));
    EXPECT_EQ(std::string(100, 'p'), std::string(rpayload, 100));
    EXPECT_EQ(0, received.msg_flags);
    EXPECT_EQ((socklen_t)sizeof(from), received.msg_namelen);
    EXPECT_EQ(player.addr_.sin_port, from.sin_port);
    EXPECT_EQ(player.addr_.sin_addr.s_addr, from.sin_addr.s_addr);
}

#ifndef _WIN32 // POSIX only: IP_PKTINFO control messages with CMSG
// A server bound to every interface must answer from the address the client sent to, or a multi-homed host replies
// from the wrong address. With IP_PKTINFO on, st_recvmsg delivers each datagram's destination address as a control
// message. Locks in current behavior.
VOID TEST(IoUdpMsgTest, RecvmsgReportsDestinationAddress)
{
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    ASSERT_GE(fd, 0);

    IoTestUdpSocket server, player;
    server.stfd_ = st_netfd_open_socket(fd);
    ASSERT_TRUE(server.stfd_ != NULL);

    int on = 1;
    ASSERT_EQ(0, setsockopt(fd, IPPROTO_IP, IP_PKTINFO, &on, sizeof(on)));
    server.addr_.sin_family = AF_INET;
    server.addr_.sin_addr.s_addr = htonl(INADDR_ANY);
    socklen_t addrlen = sizeof(server.addr_);
    ASSERT_EQ(0, ::bind(fd, (sockaddr*)&server.addr_, sizeof(server.addr_)));
    ASSERT_EQ(0, getsockname(fd, (sockaddr*)&server.addr_, &addrlen));
    ASSERT_TRUE(io_udp_socket(player));

    // The player sends to the server's port on the loopback address.
    struct sockaddr_in to = server.addr_;
    to.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    EXPECT_EQ(5, st_sendto(player.stfd_, "hello", 5, (sockaddr*)&to, sizeof(to), ST_UTEST_TIMEOUT));

    char buf[64] = {0};
    struct iovec iov;
    struct msghdr msg;
    io_msghdr(msg, iov, buf, sizeof(buf));
    union {
        char buf_[CMSG_SPACE(sizeof(struct in_pktinfo))];
        struct cmsghdr align_;
    } control;
    msg.msg_control = control.buf_;
    msg.msg_controllen = sizeof(control.buf_);
    EXPECT_EQ(5, st_recvmsg(server.stfd_, &msg, 0, ST_UTEST_TIMEOUT));
    EXPECT_STREQ("hello", buf);

    struct cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
    ASSERT_TRUE(cmsg != NULL);
    EXPECT_EQ(IPPROTO_IP, cmsg->cmsg_level);
    EXPECT_EQ(IP_PKTINFO, cmsg->cmsg_type);
    struct in_pktinfo info;
    memcpy(&info, CMSG_DATA(cmsg), sizeof(info));
    EXPECT_EQ(htonl(INADDR_LOOPBACK), info.ipi_addr.s_addr);
}
#endif

// st_recvfrom drops the tail of a datagram larger than the buffer without saying so. st_recvmsg says so: it returns
// the bytes that fit and sets MSG_TRUNC in msg_flags, so a server can count or reject oversized packets. Locks in
// current behavior.
VOID TEST(IoUdpMsgTest, RecvmsgFlagsTruncatedDatagram)
{
    IoTestUdpSocket server, player;
    ASSERT_TRUE(io_udp_socket(server));
    ASSERT_TRUE(io_udp_socket(player));

    std::string large(1500, 'x');
    EXPECT_EQ(1500, io_udp_send(player, server, large.data(), large.size()));

    char buf[100];
    struct iovec iov;
    struct msghdr msg;
    io_msghdr(msg, iov, buf, sizeof(buf));
    EXPECT_EQ(100, st_recvmsg(server.stfd_, &msg, 0, ST_UTEST_TIMEOUT));
    EXPECT_TRUE((msg.msg_flags & MSG_TRUNC) != 0);
}

// st_recvmsg passes its flags to the kernel. With MSG_PEEK, a UDP mux looks at the first byte to tell STUN, DTLS and
// RTP apart, and the datagram stays queued for the read that follows. Locks in current behavior.
VOID TEST(IoUdpMsgTest, RecvmsgPeekKeepsDatagram)
{
    IoTestUdpSocket server, player;
    ASSERT_TRUE(io_udp_socket(server));
    ASSERT_TRUE(io_udp_socket(player));

    EXPECT_EQ(5, io_udp_send(player, server, "hello", 5));

    char first = 0;
    struct iovec iov;
    struct msghdr msg;
    io_msghdr(msg, iov, &first, 1);
    EXPECT_EQ(1, st_recvmsg(server.stfd_, &msg, MSG_PEEK, ST_UTEST_TIMEOUT));
    EXPECT_EQ('h', first);

    char buf[64] = {0};
    io_msghdr(msg, iov, buf, sizeof(buf));
    EXPECT_EQ(5, st_recvmsg(server.stfd_, &msg, 0, ST_UTEST_TIMEOUT));
    EXPECT_STREQ("hello", buf);
}

static void* io_recvmsg_coroutine(void* arg)
{
    IoTestUdpReceiver* r = (IoTestUdpReceiver*)arg;
    memset(r->buf_, 0, sizeof(r->buf_));
    struct iovec iov;
    struct msghdr msg;
    io_msghdr(msg, iov, r->buf_, sizeof(r->buf_));
    errno = 0;
    r->nread_ = st_recvmsg(r->server_->stfd_, &msg, 0, ST_UTIME_NO_TIMEOUT);
    r->errno_ = errno;
    return NULL;
}

// A receive loop waits with no timeout: st_recvmsg parks the coroutine, and a datagram sent while it waits wakes it
// with the data. Locks in current behavior.
VOID TEST(IoUdpMsgTest, RecvmsgWaitsForDatagram)
{
    IoTestUdpSocket server, player;
    ASSERT_TRUE(io_udp_socket(server));
    ASSERT_TRUE(io_udp_socket(player));

    IoTestUdpReceiver r;
    r.server_ = &server;
    st_thread_t receiver = st_thread_create(io_recvmsg_coroutine, &r, 1, 0);
    ASSERT_TRUE(receiver != NULL);

    // Let the receiver find the socket empty and wait.
    st_usleep(10 * ST_UTIME_MILLISECONDS);
    EXPECT_EQ(5, io_udp_send(player, server, "hello", 5));
    st_thread_join(receiver, NULL);

    EXPECT_EQ(5, r.nread_);
    EXPECT_STREQ("hello", r.buf_);
}

// No datagram arrives before the timeout: st_recvmsg fails with ETIME. Locks in current behavior.
VOID TEST(IoUdpMsgTest, RecvmsgTimesOut)
{
    IoTestUdpSocket server;
    ASSERT_TRUE(io_udp_socket(server));

    char buf[64];
    struct iovec iov;
    struct msghdr msg;
    io_msghdr(msg, iov, buf, sizeof(buf));
    errno = 0;
    EXPECT_EQ(-1, st_recvmsg(server.stfd_, &msg, 0, 50 * ST_UTIME_MILLISECONDS));
    EXPECT_EQ(ETIME, errno);
}

// A connected UDP socket sends with st_sendmsg to a port where nothing listens. st_recvmsg, called for the reply,
// fails at once with the ECONNREFUSED the kernel got from the ICMP port unreachable, instead of waiting for the
// timeout. Locks in current behavior.
VOID TEST(IoUdpMsgTest, RecvmsgFailsWhenPeerRefuses)
{
    // Take a free port, then close it, so nothing listens there.
    IoTestUdpSocket gone;
    ASSERT_TRUE(io_udp_socket(gone));
    st_netfd_close(gone.stfd_);
    gone.stfd_ = NULL;

    IoTestUdpSocket client;
    ASSERT_TRUE(io_udp_socket(client));
    ASSERT_EQ(0, ::connect(st_netfd_fileno(client.stfd_), (sockaddr*)&gone.addr_, sizeof(gone.addr_)));

    char hello[] = "hello";
    struct iovec iov;
    struct msghdr msg;
    io_msghdr(msg, iov, hello, 5);
    EXPECT_EQ(5, st_sendmsg(client.stfd_, &msg, 0, ST_UTEST_TIMEOUT));

    char buf[64];
    io_msghdr(msg, iov, buf, sizeof(buf));
    errno = 0;
    EXPECT_EQ(-1, st_recvmsg(client.stfd_, &msg, 0, ST_UTEST_TIMEOUT));
    EXPECT_EQ(ECONNREFUSED, errno);
}

#ifndef _WIN32 // POSIX only: signals, fcntl and pthread
// The same signal interrupts recvmsg. st_recvmsg retries it, and the retry returns the datagram the player sends
// later, with the player's address in msg_name. The test clears O_NONBLOCK so recvmsg waits in the kernel, as for
// st_recvfrom. Locks in current behavior.
VOID TEST(IoUdpMsgTest, RecvmsgRetriesWhenSignalInterruptsSystemCall)
{
    IoTestUdpSocket server, player;
    ASSERT_TRUE(io_udp_socket(server));
    ASSERT_TRUE(io_udp_socket(player));

    int fd = st_netfd_fileno(server.stfd_);
    int flags = fcntl(fd, F_GETFL);
    ASSERT_NE(-1, flags);
    ASSERT_NE(-1, fcntl(fd, F_SETFL, flags & ~O_NONBLOCK));

    struct sigaction sa, old_sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = io_udp_on_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    ASSERT_EQ(0, sigaction(SIGUSR1, &sa, &old_sa));

    IoTestUdpSignaler s;
    s.target_ = pthread_self();
    s.player_ = &player;
    s.server_ = &server;
    io_udp_signals = 0;
    pthread_t signaler;
    ASSERT_EQ(0, pthread_create(&signaler, NULL, io_udp_signaler_thread, &s));

    char buf[64] = {0};
    struct iovec iov;
    struct msghdr msg;
    io_msghdr(msg, iov, buf, sizeof(buf));
    struct sockaddr_in from;
    msg.msg_name = &from;
    msg.msg_namelen = sizeof(from);
    errno = 0;
    int nread = st_recvmsg(server.stfd_, &msg, 0, ST_UTEST_TIMEOUT);
    int err = errno;

    pthread_join(signaler, NULL);
    sigaction(SIGUSR1, &old_sa, NULL);
    fcntl(fd, F_SETFL, flags);

    EXPECT_EQ(3, (int)io_udp_signals);
    EXPECT_EQ(5, nread) << "errno=" << err;
    EXPECT_STREQ("hello", buf);
    EXPECT_EQ(player.addr_.sin_port, from.sin_port);
}
#endif

// The buffers of one datagram add up to more than UDP allows: st_sendmsg fails at once with EMSGSIZE instead of
// waiting for the socket to become writable. Locks in current behavior.
VOID TEST(IoUdpMsgTest, SendmsgOversizedDatagramFails)
{
    IoTestUdpSocket server, player;
    ASSERT_TRUE(io_udp_socket(server));
    ASSERT_TRUE(io_udp_socket(player));

    std::string half(32768, 'x');
    struct iovec iov[2] = {{(void*)half.data(), half.size()}, {(void*)half.data(), half.size()}};
    struct msghdr msg;
    memset(&msg, 0, sizeof(msg));
    msg.msg_name = &server.addr_;
    msg.msg_namelen = sizeof(server.addr_);
    msg.msg_iov = iov;
    msg.msg_iovlen = 2;
    errno = 0;
    EXPECT_EQ(-1, st_sendmsg(player.stfd_, &msg, 0, ST_UTIME_NO_TIMEOUT));
    EXPECT_EQ(EMSGSIZE, errno);
}

#ifdef __linux__
struct IoTestMsgConsumer {
    st_netfd_t stfd_;
    int ndatagrams_;
};

// Wake after the producer starts waiting, then read every queued datagram.
static void* io_consume_all_coroutine(void* arg)
{
    IoTestMsgConsumer* c = (IoTestMsgConsumer*)arg;
    char buf[1000];
    st_usleep(10 * ST_UTIME_MILLISECONDS);
    while (st_read(c->stfd_, buf, sizeof(buf), 0) > 0) {
        c->ndatagrams_++;
    }
    return NULL;
}
#endif

#ifndef _WIN32 // POSIX only: Unix-domain datagram socket pairs
// Loopback UDP never runs out of send buffer, so this uses a local datagram socket pair, whose sender can't send
// while its queued datagrams fill the send buffer, like a producer that outruns its consumer. On Linux, the kernel
// reports the socket as not ready: st_sendmsg fails with ETIME when nobody reads, and waits until the consumer catches
// up. Linux reports the socket writable only after the queue drains well below the limit, not after one read, so the
// consumer reads everything. macOS fails with ENOBUFS at once instead, so st_sendmsg can't wait there. Locks in
// current behavior on each OS.
VOID TEST(IoUdpMsgTest, SendmsgWaitsForFullQueue)
{
    int fds[2];
    ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_DGRAM, 0, fds));
    IoTestUdpSocket producer, consumer;
    producer.stfd_ = st_netfd_open_socket(fds[0]);
    consumer.stfd_ = st_netfd_open_socket(fds[1]);
    ASSERT_TRUE(producer.stfd_ != NULL);
    ASSERT_TRUE(consumer.stfd_ != NULL);

    char data[1000];
    memset(data, 'x', sizeof(data));
    struct iovec iov;
    struct msghdr msg;
    io_msghdr(msg, iov, data, sizeof(data));

    // Fill the queue without waiting.
    int nsent = 0;
    while (st_sendmsg(producer.stfd_, &msg, 0, 0) > 0) {
        nsent++;
    }
    EXPECT_GT(nsent, 0);
#ifdef __linux__
    EXPECT_EQ(ETIME, errno);

    errno = 0;
    EXPECT_EQ(-1, st_sendmsg(producer.stfd_, &msg, 0, 20 * ST_UTIME_MILLISECONDS));
    EXPECT_EQ(ETIME, errno);

    IoTestMsgConsumer c;
    c.stfd_ = consumer.stfd_;
    c.ndatagrams_ = 0;
    st_thread_t reader = st_thread_create(io_consume_all_coroutine, &c, 1, 0);
    ASSERT_TRUE(reader != NULL);
    EXPECT_EQ(1000, st_sendmsg(producer.stfd_, &msg, 0, ST_UTEST_TIMEOUT));
    st_thread_join(reader, NULL);
    EXPECT_EQ(nsent, c.ndatagrams_);
#else
    EXPECT_EQ(ENOBUFS, errno);
#endif
}

// The same full queue for st_sendto, the call SRS sends every RTP and RTCP packet with: a burst outruns the consumer.
// The pair is connected, so the datagrams go without an address. On Linux, st_sendto fails with ETIME when nobody
// reads, and waits until the consumer catches up; macOS fails with ENOBUFS at once. Locks in current behavior on each
// OS.
VOID TEST(IoUdpTest, SendtoWaitsForFullQueue)
{
    int fds[2];
    ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_DGRAM, 0, fds));
    IoTestUdpSocket producer, consumer;
    producer.stfd_ = st_netfd_open_socket(fds[0]);
    consumer.stfd_ = st_netfd_open_socket(fds[1]);
    ASSERT_TRUE(producer.stfd_ != NULL);
    ASSERT_TRUE(consumer.stfd_ != NULL);

    char data[1000];
    memset(data, 'x', sizeof(data));

    // Fill the queue without waiting.
    int nsent = 0;
    while (st_sendto(producer.stfd_, data, sizeof(data), NULL, 0, 0) > 0) {
        nsent++;
    }
    EXPECT_GT(nsent, 0);
#ifdef __linux__
    EXPECT_EQ(ETIME, errno);

    errno = 0;
    EXPECT_EQ(-1, st_sendto(producer.stfd_, data, sizeof(data), NULL, 0, 20 * ST_UTIME_MILLISECONDS));
    EXPECT_EQ(ETIME, errno);

    IoTestMsgConsumer c;
    c.stfd_ = consumer.stfd_;
    c.ndatagrams_ = 0;
    st_thread_t reader = st_thread_create(io_consume_all_coroutine, &c, 1, 0);
    ASSERT_TRUE(reader != NULL);
    EXPECT_EQ(1000, st_sendto(producer.stfd_, data, sizeof(data), NULL, 0, ST_UTEST_TIMEOUT));
    st_thread_join(reader, NULL);
    EXPECT_EQ(nsent, c.ndatagrams_);
#else
    EXPECT_EQ(ENOBUFS, errno);
#endif
}
#endif
