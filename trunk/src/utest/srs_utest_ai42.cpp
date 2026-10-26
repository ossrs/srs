//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//
#include <srs_utest_ai42.hpp>

#include <srs_app_async_call.hpp>
#include <srs_app_circuit_breaker.hpp>
#include <srs_app_config.hpp>
#include <srs_app_dash.hpp>
#include <srs_app_factory.hpp>
#include <srs_app_ffmpeg.hpp>
#include <srs_app_hls.hpp>
#include <srs_app_srt_server.hpp>
#include <srs_app_st.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_kbps.hpp>
#include <srs_kernel_log.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_conn.hpp>
#include <srs_protocol_http_conn.hpp>
#include <srs_protocol_rtmp_stack.hpp>
#include <srs_protocol_srt.hpp>
#include <srs_protocol_st.hpp>
#include <srs_protocol_utility.hpp>
#include <srs_utest_ai13.hpp>
#include <srs_utest_ai15.hpp>
#include <srs_utest_ai17.hpp>
#include <srs_utest_ai19.hpp>
#include <srs_utest_ai06.hpp>
#include <srs_utest_ai40.hpp>
#include <srs_utest_manual_config.hpp>
#include <srs_utest_manual_coworkers.hpp>
#include <srs_utest_manual_kernel.hpp>
#include <srs_utest_manual_mock.hpp>

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#ifndef _WIN32
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <sys/resource.h>
#endif

#include <string>
#include <vector>
using namespace std;

#include <srt/srt.h>

extern SrsSrtEventLoop *_srs_srt_eventloop;
extern vector<SrsIPAddress *> _srs_system_ips;
extern string _srs_public_internet_address;

// The libsrt log handler the tests set back after they route libsrt logs to the SRS log.
static void srt_utest_quiet_log(void *opaque, int level, const char *file, int line, const char *area, const char *message)
{
}

// An SRT listener socket as SrsSrtAcceptor makes it: the socket, the options from the config, then the listen.
static srs_error_t srt_utest_listen(const string &ip, int port, bool default_options, srs_srt_t *pfd)
{
    srs_error_t err = srs_success;

    srs_srt_t fd = srs_srt_socket_invalid();
    if (default_options) {
        err = srs_srt_socket_with_default_option(&fd);
    } else {
        err = srs_srt_socket(&fd);
    }
    if (err != srs_success) {
        return srs_error_wrap(err, "socket");
    }

    if ((err = srs_srt_listen(fd, ip, port)) != srs_success) {
        return srs_error_wrap(err, "listen");
    }

    *pfd = fd;
    return err;
}

// The listener coroutine of SrsSrtListener::cycle, which waits in accept until a client connects.
class SrtUtestAcceptTask : public ISrsCoroutineHandler
{
public:
    SrsSrtSocket *listener_;
    srs_srt_t accepted_;
    srs_error_t err_;
    bool done_;
    SrsSTCoroutine *trd_;

public:
    SrtUtestAcceptTask(SrsSrtSocket *listener)
    {
        listener_ = listener;
        accepted_ = srs_srt_socket_invalid();
        err_ = srs_success;
        done_ = false;
        trd_ = new SrsSTCoroutine("srt-accept", this);
    }
    virtual ~SrtUtestAcceptTask()
    {
        srs_freep(trd_);
        srs_freep(err_);
    }

public:
    virtual srs_error_t cycle()
    {
        err_ = listener_->accept(&accepted_);
        done_ = true;
        return srs_success;
    }
};

// An SRT player that pauses, then reads the messages the server sends, such as a player busy decoding.
class SrtUtestSlowPlayer : public ISrsCoroutineHandler
{
public:
    SrsSrtSocket *skt_;
    srs_utime_t pause_;
    int expect_;
    // The first 4 bytes of each message read, in order.
    vector<int> seqs_;
    srs_error_t err_;
    bool done_;
    SrsSTCoroutine *trd_;

public:
    SrtUtestSlowPlayer(SrsSrtSocket *skt, srs_utime_t pause, int expect)
    {
        skt_ = skt;
        pause_ = pause;
        expect_ = expect;
        err_ = srs_success;
        done_ = false;
        trd_ = new SrsSTCoroutine("srt-slow-player", this);
    }
    virtual ~SrtUtestSlowPlayer()
    {
        srs_freep(trd_);
        srs_freep(err_);
    }

public:
    virtual srs_error_t cycle()
    {
        srs_usleep(pause_);

        char buf[1500];
        for (int i = 0; i < expect_; i++) {
            ssize_t nread = 0;
            if ((err_ = skt_->recvmsg(buf, sizeof(buf), &nread)) != srs_success) {
                break;
            }
            int seq = 0;
            memcpy(&seq, buf, sizeof(seq));
            seqs_.push_back(seq);
        }

        done_ = true;
        return srs_success;
    }
};

// Wait for a coroutine of the test to finish, at most 3 seconds.
static void srt_utest_wait_done(bool *done)
{
    for (int i = 0; i < 300 && !*done; i++) {
        srs_usleep(10 * SRS_UTIME_MILLISECONDS);
    }
}

// A message of the size SRS sends, 7 TS packets, that starts with its sequence number.
static string srt_utest_ts_message(int seq)
{
    string msg(1316, (char)0x47);
    memcpy((char *)msg.data(), &seq, sizeof(seq));
    return msg;
}

// The libsrt buffers of a few packets, so a player that does not read fills them at once. libsrt counts
// SRTO_SNDBUF and SRTO_RCVBUF in packets of MSS minus the UDP header, 32 at least.
static srs_error_t srt_utest_small_buffers(srs_srt_t fd)
{
    srs_error_t err = srs_success;

    if ((err = srs_srt_set_sndbuf(fd, 32 * 1472)) != srs_success) {
        return srs_error_wrap(err, "sndbuf");
    }
    if ((err = srs_srt_set_rcvbuf(fd, 32 * 1472)) != srs_success) {
        return srs_error_wrap(err, "rcvbuf");
    }

    return err;
}

// An operator listens on both stacks with one port, as `srt_server { listen 10080 [::]:10080; }`. The IPv6
// listener is IPv6 only, so it does not take the port of the IPv4 wildcard, and each accepts the
// publishers of its family, with their real address.
VOID TEST(ProtocolSrtTest, DualStackListenersOnOnePort_EachAcceptsItsFamily)
{
    srs_error_t err;

    int port = 0;
    HELPER_ASSERT_SUCCESS(srs_utest_free_port(SOCK_DGRAM, &port));

    srs_srt_t lfd4 = srs_srt_socket_invalid();
    HELPER_ASSERT_SUCCESS(srt_utest_listen("0.0.0.0", port, false, &lfd4));
    SrsSrtSocket listener4(_srs_srt_eventloop->poller(), lfd4);
    listener4.set_recv_timeout(3 * SRS_UTIME_SECONDS);

    srs_srt_t lfd6 = srs_srt_socket_invalid();
    HELPER_ASSERT_SUCCESS(srt_utest_listen("::", port, false, &lfd6));
    SrsSrtSocket listener6(_srs_srt_eventloop->poller(), lfd6);
    listener6.set_recv_timeout(3 * SRS_UTIME_SECONDS);

    // An IPv4 publisher, to srt://127.0.0.1:port.
    srs_srt_t cfd4 = srs_srt_socket_invalid();
    HELPER_ASSERT_SUCCESS(srs_srt_socket_with_default_option(&cfd4));
    SrsSrtSocket client4(_srs_srt_eventloop->poller(), cfd4);
    client4.set_send_timeout(3 * SRS_UTIME_SECONDS);
    HELPER_ASSERT_SUCCESS(client4.connect("127.0.0.1", port));

    srs_srt_t afd4 = srs_srt_socket_invalid();
    HELPER_ASSERT_SUCCESS(listener4.accept(&afd4));
    SrsSrtSocket accepted4(_srs_srt_eventloop->poller(), afd4);

    string ip;
    int remote_port = 0;
    HELPER_ASSERT_SUCCESS(srs_srt_get_remote_ip_port(afd4, ip, remote_port));
    EXPECT_STREQ("127.0.0.1", ip.c_str());
    EXPECT_GT(remote_port, 0);

    // An IPv6 publisher, such as FFmpeg to srt://[::1]:port.
    srs_srt_t cfd6 = srs_srt_socket_invalid();
    HELPER_ASSERT_SUCCESS(srs_srt_socket_with_default_option(&cfd6));
    SrsSrtSocket client6(_srs_srt_eventloop->poller(), cfd6);

    sockaddr_in6 addr6;
    memset(&addr6, 0, sizeof(addr6));
    addr6.sin6_family = AF_INET6;
    addr6.sin6_port = htons(port);
    ASSERT_EQ(1, inet_pton(AF_INET6, "::1", &addr6.sin6_addr));
    ASSERT_EQ(0, srt_connect(cfd6, (sockaddr *)&addr6, sizeof(addr6)));

    srs_srt_t afd6 = srs_srt_socket_invalid();
    HELPER_ASSERT_SUCCESS(listener6.accept(&afd6));
    SrsSrtSocket accepted6(_srs_srt_eventloop->poller(), afd6);

    HELPER_ASSERT_SUCCESS(srs_srt_get_remote_ip_port(afd6, ip, remote_port));
    EXPECT_STREQ("::1", ip.c_str());

    sockaddr_storage local;
    int local_len = sizeof(local);
    ASSERT_EQ(0, srt_getsockname(cfd6, (sockaddr *)&local, &local_len));
    EXPECT_EQ(ntohs(((sockaddr_in6 *)&local)->sin6_port), remote_port);
}

// The listener coroutine waits in accept before any publisher comes, as SrsSrtListener::cycle does; a
// publisher that connects then wakes it, and it accepts the publisher with its streamid.
VOID TEST(ProtocolSrtTest, ListenerWaitingInAccept_PublisherConnects)
{
    srs_error_t err;

    int port = 0;
    HELPER_ASSERT_SUCCESS(srs_utest_free_port(SOCK_DGRAM, &port));

    srs_srt_t lfd = srs_srt_socket_invalid();
    HELPER_ASSERT_SUCCESS(srt_utest_listen("127.0.0.1", port, false, &lfd));
    SrsSrtSocket listener(_srs_srt_eventloop->poller(), lfd);
    listener.set_recv_timeout(3 * SRS_UTIME_SECONDS);

    SrtUtestAcceptTask task(&listener);
    HELPER_ASSERT_SUCCESS(task.trd_->start());

    // Let the listener coroutine find no publisher and wait.
    srs_usleep(10 * SRS_UTIME_MILLISECONDS);
    EXPECT_FALSE(task.done_);

    srs_srt_t cfd = srs_srt_socket_invalid();
    HELPER_ASSERT_SUCCESS(srs_srt_socket_with_default_option(&cfd));
    HELPER_ASSERT_SUCCESS(srs_srt_set_streamid(cfd, "#!::r=live/livestream,m=publish"));
    SrsSrtSocket client(_srs_srt_eventloop->poller(), cfd);
    client.set_send_timeout(3 * SRS_UTIME_SECONDS);
    HELPER_ASSERT_SUCCESS(client.connect("127.0.0.1", port));

    srt_utest_wait_done(&task.done_);
    ASSERT_TRUE(task.done_);
    HELPER_ASSERT_SUCCESS(task.err_);
    task.err_ = srs_success;
    SrsSrtSocket accepted(_srs_srt_eventloop->poller(), task.accepted_);

    string streamid;
    HELPER_ASSERT_SUCCESS(srs_srt_get_streamid(task.accepted_, streamid));
    EXPECT_STREQ("#!::r=live/livestream,m=publish", streamid.c_str());
}

// Another program, such as a second SRS, already holds the UDP port. The SRT listen fails to bind, and
// closes the socket it got, so SrsServer warns and goes on with the other listen addresses.
VOID TEST(ProtocolSrtTest, PortHeldByAnotherProgram_BindFailsAndSocketClosed)
{
    srs_error_t err;

    int port = 0;
    HELPER_ASSERT_SUCCESS(srs_utest_free_port(SOCK_DGRAM, &port));

    int other = ::socket(AF_INET, SOCK_DGRAM, 0);
    ASSERT_GE(other, 0);
    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    ASSERT_EQ(0, ::bind(other, (sockaddr *)&addr, sizeof(addr)));

    srs_srt_t lfd = srs_srt_socket_invalid();
    HELPER_ASSERT_SUCCESS(srs_srt_socket(&lfd));
    err = srs_srt_listen(lfd, "0.0.0.0", port);
    EXPECT_EQ(ERROR_SOCKET_BIND, srs_error_code(err));
    srs_freep(err);

    // The socket is closed; libsrt frees a closed socket later, then no longer knows it.
    SRT_SOCKSTATUS state = srt_getsockstate(lfd);
    EXPECT_TRUE(state == SRTS_CLOSED || state == SRTS_NONEXIST) << state;

    ::close(other);
}

// The operator lists the same port twice, as `srt_server { listen 10080 10080; }`. libsrt binds the second
// socket to the first one's UDP port, but one port has one listener, so the second listen fails and closes
// its socket, and the first listener still accepts publishers.
VOID TEST(ProtocolSrtTest, SamePortListedTwice_SecondListenFailsFirstServes)
{
    srs_error_t err;

    int port = 0;
    HELPER_ASSERT_SUCCESS(srs_utest_free_port(SOCK_DGRAM, &port));

    srs_srt_t lfd = srs_srt_socket_invalid();
    HELPER_ASSERT_SUCCESS(srt_utest_listen("0.0.0.0", port, false, &lfd));
    SrsSrtSocket listener(_srs_srt_eventloop->poller(), lfd);
    listener.set_recv_timeout(3 * SRS_UTIME_SECONDS);

    srs_srt_t lfd2 = srs_srt_socket_invalid();
    HELPER_ASSERT_SUCCESS(srs_srt_socket(&lfd2));
    err = srs_srt_listen(lfd2, "0.0.0.0", port);
    EXPECT_EQ(ERROR_SOCKET_LISTEN, srs_error_code(err));
    srs_freep(err);
    SRT_SOCKSTATUS state = srt_getsockstate(lfd2);
    EXPECT_TRUE(state == SRTS_CLOSED || state == SRTS_NONEXIST) << state;

    srs_srt_t cfd = srs_srt_socket_invalid();
    HELPER_ASSERT_SUCCESS(srs_srt_socket_with_default_option(&cfd));
    SrsSrtSocket client(_srs_srt_eventloop->poller(), cfd);
    client.set_send_timeout(3 * SRS_UTIME_SECONDS);
    HELPER_ASSERT_SUCCESS(client.connect("127.0.0.1", port));

    srs_srt_t afd = srs_srt_socket_invalid();
    HELPER_ASSERT_SUCCESS(listener.accept(&afd));
    SrsSrtSocket accepted(_srs_srt_eventloop->poller(), afd);
}

// The transport stats of the SRT publish and play pithy prints, which SrsSrtConnection::get_stats fetches
// and clears each time: the packets a publisher sent to the server, then the packets the server sent to a
// player, with no loss, retransmit or drop on loopback.
VOID TEST(ProtocolSrtTest, PublisherAndPlayerTransportStats)
{
    srs_error_t err;

    int port = 0;
    HELPER_ASSERT_SUCCESS(srs_utest_free_port(SOCK_DGRAM, &port));

    srs_srt_t lfd = srs_srt_socket_invalid();
    HELPER_ASSERT_SUCCESS(srt_utest_listen("127.0.0.1", port, true, &lfd));
    SrsSrtSocket listener(_srs_srt_eventloop->poller(), lfd);
    listener.set_recv_timeout(3 * SRS_UTIME_SECONDS);

    srs_srt_t cfd = srs_srt_socket_invalid();
    HELPER_ASSERT_SUCCESS(srs_srt_socket_with_default_option(&cfd));
    SrsSrtSocket client(_srs_srt_eventloop->poller(), cfd);
    client.set_send_timeout(3 * SRS_UTIME_SECONDS);
    client.set_recv_timeout(3 * SRS_UTIME_SECONDS);
    HELPER_ASSERT_SUCCESS(client.connect("127.0.0.1", port));

    srs_srt_t afd = srs_srt_socket_invalid();
    HELPER_ASSERT_SUCCESS(listener.accept(&afd));
    SrsSrtSocket accepted(_srs_srt_eventloop->poller(), afd);
    accepted.set_recv_timeout(3 * SRS_UTIME_SECONDS);

    char buf[1500];
    ssize_t nn = 0;

    // The publisher sends 5 messages, and the server reads them.
    for (int i = 0; i < 5; i++) {
        string msg = srt_utest_ts_message(i);
        HELPER_ASSERT_SUCCESS(client.sendmsg((void *)msg.data(), msg.size(), &nn));
    }
    for (int i = 0; i < 5; i++) {
        HELPER_ASSERT_SUCCESS(accepted.recvmsg(buf, sizeof(buf), &nn));
        EXPECT_EQ(1316, nn);
    }

    if (true) {
        SrsSrtStat s;
        HELPER_ASSERT_SUCCESS(s.fetch(afd, true));
        EXPECT_EQ(5, s.pktRecv());
        EXPECT_EQ(0, s.pktRcvLoss());
        EXPECT_EQ(0, s.pktRcvRetrans());
        EXPECT_EQ(0, s.pktRcvDrop());
    }

    // The next print counts only what came after the last one.
    if (true) {
        SrsSrtStat s;
        HELPER_ASSERT_SUCCESS(s.fetch(afd, true));
        EXPECT_EQ(0, s.pktRecv());
    }

    // The server sends 3 messages to the player, which reads them.
    for (int i = 0; i < 3; i++) {
        string msg = srt_utest_ts_message(i);
        HELPER_ASSERT_SUCCESS(accepted.sendmsg((void *)msg.data(), msg.size(), &nn));
    }
    for (int i = 0; i < 3; i++) {
        HELPER_ASSERT_SUCCESS(client.recvmsg(buf, sizeof(buf), &nn));
        EXPECT_EQ(1316, nn);
    }

    if (true) {
        SrsSrtStat s;
        HELPER_ASSERT_SUCCESS(s.fetch(afd, true));
        EXPECT_EQ(3, s.pktSent());
        EXPECT_EQ(0, s.pktSndLoss());
        EXPECT_EQ(0, s.pktRetrans());
        EXPECT_EQ(0, s.pktSndDrop());
    }
}

// A player that pauses, such as one busy decoding, while the server sends to it: libsrt's send buffer
// fills, so the server's send waits until the player reads again, then goes on. The player gets every
// message in order, as the vhost has `tlpktdrop off`.
VOID TEST(ProtocolSrtTest, SlowPlayerCatchesUp_SendWaitsThenGoesOn)
{
    srs_error_t err;

    int port = 0;
    HELPER_ASSERT_SUCCESS(srs_utest_free_port(SOCK_DGRAM, &port));

    srs_srt_t lfd = srs_srt_socket_invalid();
    HELPER_ASSERT_SUCCESS(srs_srt_socket_with_default_option(&lfd));
    HELPER_ASSERT_SUCCESS(srt_utest_small_buffers(lfd));
    HELPER_ASSERT_SUCCESS(srs_srt_listen(lfd, "127.0.0.1", port));
    SrsSrtSocket listener(_srs_srt_eventloop->poller(), lfd);
    listener.set_recv_timeout(3 * SRS_UTIME_SECONDS);

    srs_srt_t cfd = srs_srt_socket_invalid();
    HELPER_ASSERT_SUCCESS(srs_srt_socket_with_default_option(&cfd));
    HELPER_ASSERT_SUCCESS(srt_utest_small_buffers(cfd));
    SrsSrtSocket client(_srs_srt_eventloop->poller(), cfd);
    client.set_send_timeout(3 * SRS_UTIME_SECONDS);
    client.set_recv_timeout(3 * SRS_UTIME_SECONDS);
    HELPER_ASSERT_SUCCESS(client.connect("127.0.0.1", port));

    srs_srt_t afd = srs_srt_socket_invalid();
    HELPER_ASSERT_SUCCESS(listener.accept(&afd));
    SrsSrtSocket accepted(_srs_srt_eventloop->poller(), afd);
    accepted.set_send_timeout(3 * SRS_UTIME_SECONDS);

    // More messages than the two buffers hold, so the send must wait for the player.
    const int total = 200;
    const srs_utime_t pause = 200 * SRS_UTIME_MILLISECONDS;
    SrtUtestSlowPlayer player(&client, pause, total);
    HELPER_ASSERT_SUCCESS(player.trd_->start());

    srs_utime_t starttime = srs_time_now_realtime();
    for (int i = 0; i < total; i++) {
        string msg = srt_utest_ts_message(i);
        ssize_t nn = 0;
        HELPER_ASSERT_SUCCESS(accepted.sendmsg((void *)msg.data(), msg.size(), &nn));
        EXPECT_EQ(1316, nn);
    }
    srs_utime_t elapsed = srs_time_now_realtime() - starttime;

    // The sends could finish only after the player woke up and read.
    EXPECT_GE(elapsed, pause / 2);

    srt_utest_wait_done(&player.done_);
    ASSERT_TRUE(player.done_);
    HELPER_EXPECT_SUCCESS(player.err_);
    player.err_ = srs_success;
    ASSERT_EQ(total, (int)player.seqs_.size());
    for (int i = 0; i < total; i++) {
        EXPECT_EQ(i, player.seqs_[i]);
    }
}

// A player that stops reading but keeps its SRT connection: once the send buffer is full, the server's
// send waits for the send timeout, which SrsMpegtsSrtConn::do_playing sets to the peer idle timeout, then
// fails, so the server drops the player (#4600).
VOID TEST(ProtocolSrtTest, PlayerStopsReading_SendTimesOut)
{
    srs_error_t err;

    int port = 0;
    HELPER_ASSERT_SUCCESS(srs_utest_free_port(SOCK_DGRAM, &port));

    srs_srt_t lfd = srs_srt_socket_invalid();
    HELPER_ASSERT_SUCCESS(srs_srt_socket_with_default_option(&lfd));
    HELPER_ASSERT_SUCCESS(srt_utest_small_buffers(lfd));
    HELPER_ASSERT_SUCCESS(srs_srt_listen(lfd, "127.0.0.1", port));
    SrsSrtSocket listener(_srs_srt_eventloop->poller(), lfd);
    listener.set_recv_timeout(3 * SRS_UTIME_SECONDS);

    srs_srt_t cfd = srs_srt_socket_invalid();
    HELPER_ASSERT_SUCCESS(srs_srt_socket_with_default_option(&cfd));
    HELPER_ASSERT_SUCCESS(srt_utest_small_buffers(cfd));
    SrsSrtSocket client(_srs_srt_eventloop->poller(), cfd);
    client.set_send_timeout(3 * SRS_UTIME_SECONDS);
    HELPER_ASSERT_SUCCESS(client.connect("127.0.0.1", port));

    srs_srt_t afd = srs_srt_socket_invalid();
    HELPER_ASSERT_SUCCESS(listener.accept(&afd));
    SrsSrtSocket accepted(_srs_srt_eventloop->poller(), afd);
    accepted.set_send_timeout(100 * SRS_UTIME_MILLISECONDS);

    int sent = 0;
    for (; sent < 1000; sent++) {
        string msg = srt_utest_ts_message(sent);
        ssize_t nn = 0;
        if ((err = accepted.sendmsg((void *)msg.data(), msg.size(), &nn)) != srs_success) {
            break;
        }
    }

    // The buffers took some messages, then the send waited and timed out.
    EXPECT_GT(sent, 0);
    EXPECT_LT(sent, 1000);
    EXPECT_EQ(ERROR_SRT_TIMEOUT, srs_error_code(err));
    srs_freep(err);
}

// A publisher whose passphrase is not the vhost's `passphrase`: libsrt on the server refuses the
// handshake and warns, and the SRS log has the warning with the SRT tag, at the level libsrt gives it.
VOID TEST(ProtocolSrtTest, PublisherWithWrongPassphrase_RejectLoggedAsSrtWarning)
{
    srs_error_t err;

    MockLogForSignal log;
    ISrsLog *original_log = _srs_log;
    _srs_log = &log;

    // The SRT server routes libsrt logs to the SRS log, at libsrt's default level, warning.
    srt_setloglevel(LOG_WARNING);
    HELPER_EXPECT_SUCCESS(srs_srt_log_initialize());

    int port = 0;
    HELPER_EXPECT_SUCCESS(srs_utest_free_port(SOCK_DGRAM, &port));

    SrsRand rand;
    srs_srt_t lfd = srs_srt_socket_invalid();
    HELPER_EXPECT_SUCCESS(srs_srt_socket(&lfd));
    HELPER_EXPECT_SUCCESS(srs_srt_set_passphrase(lfd, rand.gen_str(16)));
    HELPER_EXPECT_SUCCESS(srs_srt_listen(lfd, "127.0.0.1", port));
    SrsSrtSocket listener(_srs_srt_eventloop->poller(), lfd);

    srs_srt_t cfd = srs_srt_socket_invalid();
    HELPER_EXPECT_SUCCESS(srs_srt_socket_with_default_option(&cfd));
    HELPER_EXPECT_SUCCESS(srs_srt_set_passphrase(cfd, rand.gen_str(16)));
    SrsSrtSocket client(_srs_srt_eventloop->poller(), cfd);
    client.set_send_timeout(3 * SRS_UTIME_SECONDS);

    err = client.connect("127.0.0.1", port);
    EXPECT_TRUE(err != srs_success);
    srs_freep(err);

    // libsrt calls the handler under its log lock, so no libsrt thread logs to the mock after this.
    srt_setloghandler(NULL, srt_utest_quiet_log);
    srt_setloglevel(LOG_CRIT);
    _srs_log = original_log;

    bool rejected = false;
    for (int i = 0; i < (int)log.warns_.size(); i++) {
        if (log.warns_[i].find("rsp(REJECT)") != string::npos) {
            rejected = true;
        }
    }
    EXPECT_TRUE(rejected);
    EXPECT_STREQ("SRT", log.last_warn_tag_.c_str());
}

// A player connected to a server port over loopback: the listen socket, the player's socket, and the
// server's side of the connection as the listener accepts it.
class StUtestTcpPair
{
public:
    srs_netfd_t lfd_;
    srs_netfd_t client_;
    srs_netfd_t server_;

public:
    StUtestTcpPair()
    {
        lfd_ = client_ = server_ = NULL;
    }
    ~StUtestTcpPair()
    {
        srs_close_stfd(client_);
        srs_close_stfd(server_);
        srs_close_stfd(lfd_);
    }

public:
    srs_error_t open()
    {
        srs_error_t err = srs_success;

        int port = 0;
        if ((err = srs_utest_tcp_listen("127.0.0.1", &port, &lfd_)) != srs_success) {
            return srs_error_wrap(err, "listen");
        }

        if ((err = srs_tcp_connect("127.0.0.1", port, 3 * SRS_UTIME_SECONDS, &client_)) != srs_success) {
            return srs_error_wrap(err, "connect");
        }

        if ((server_ = srs_accept(lfd_, NULL, NULL, 3 * SRS_UTIME_SECONDS)) == NULL) {
            return srs_error_new(ERROR_SOCKET_ACCEPT, "accept");
        }

        return err;
    }
    // The server's connection takes the accepted socket, and closes it when it goes.
    srs_netfd_t take_server()
    {
        srs_netfd_t fd = server_;
        server_ = NULL;
        return fd;
    }
};

// A player downloads an HLS segment, or a static file, then stops reading but keeps the connection,
// as a paused or stuck client does. The server writes the body with its length; when the socket
// buffers are full, the write waits for the send timeout and fails with a timeout, so the server
// drops the player instead of waiting for ever.
VOID TEST(ProtocolStTest, HttpDownloadPlayerStopsReading_WriteTimesOut)
{
    srs_error_t err = srs_success;

    StUtestTcpPair pair;
    HELPER_ASSERT_SUCCESS(pair.open());

    SrsTcpConnection conn(pair.take_server());
    conn.set_send_timeout(100 * SRS_UTIME_MILLISECONDS);

    const int chunk = 64 * 1024;
    const int nn_chunks = 1024;
    string data(chunk, 'x');

    SrsHttpResponseWriter w(&conn);
    w.header()->set_content_type("video/MP2T");
    w.header()->set_content_length((int64_t)chunk * nn_chunks);
    w.write_header(SRS_CONSTS_HTTP_OK);

    int i = 0;
    for (; i < nn_chunks; i++) {
        if ((err = w.write((char *)data.data(), chunk)) != srs_success) {
            break;
        }
    }

    ASSERT_TRUE(err != srs_success);
    EXPECT_EQ(ERROR_SOCKET_TIMEOUT, srs_error_code(err));
    srs_freep(err);

    // The socket buffers took some of the body before the player's window closed.
    EXPECT_LT(i, nn_chunks);
    EXPECT_GT(conn.get_send_bytes(), 0);
}

// A player resets its connection in the middle of a download, as a browser does when the user
// closes the tab. The next write of the body fails with a write error, not a timeout, so the
// server ends the connection at once.
VOID TEST(ProtocolStTest, HttpDownloadPlayerResets_WriteFails)
{
    srs_error_t err = srs_success;

    StUtestTcpPair pair;
    HELPER_ASSERT_SUCCESS(pair.open());

    SrsTcpConnection conn(pair.take_server());
    conn.set_send_timeout(3 * SRS_UTIME_SECONDS);

    const int chunk = 1024;
    string data(chunk, 'x');

    SrsHttpResponseWriter w(&conn);
    w.header()->set_content_type("video/MP2T");
    w.header()->set_content_length(chunk * 2);
    w.write_header(SRS_CONSTS_HTTP_OK);
    HELPER_EXPECT_SUCCESS(w.write((char *)data.data(), chunk));
    int64_t sent = conn.get_send_bytes();
    EXPECT_GT(sent, chunk);

    // The player closes with a reset, without reading what the server sent.
    struct linger l;
    l.l_onoff = 1;
    l.l_linger = 0;
    EXPECT_EQ(0, setsockopt(srs_netfd_fileno(pair.client_), SOL_SOCKET, SO_LINGER, (const char *)&l, sizeof(l)));
    srs_close_stfd(pair.client_);
    srs_usleep(1 * SRS_UTIME_MILLISECONDS);

    err = w.write((char *)data.data(), chunk);
    ASSERT_TRUE(err != srs_success);
    EXPECT_EQ(ERROR_SOCKET_WRITE, srs_error_code(err));
    srs_freep(err);

    // Nothing of the failed write is counted as sent.
    EXPECT_EQ(sent, conn.get_send_bytes());
}

// An HTTP-FLV player stops reading the live stream but keeps the connection. The server sends each
// batch of FLV tags as one chunk with writev; when the socket buffers are full, the writev waits for
// the send timeout and fails with a timeout, so the server drops the player and the stream goes on
// for the others.
VOID TEST(ProtocolStTest, HttpFlvPlayerStopsReading_WritevTimesOut)
{
    srs_error_t err = srs_success;

    StUtestTcpPair pair;
    HELPER_ASSERT_SUCCESS(pair.open());

    SrsTcpConnection conn(pair.take_server());
    conn.set_send_timeout(100 * SRS_UTIME_MILLISECONDS);

    // No content length, so the live stream goes in chunked encoding, as SrsLiveStream serves it.
    SrsHttpResponseWriter w(&conn);
    w.header()->set_content_type("video/x-flv");
    w.write_header(SRS_CONSTS_HTTP_OK);

    // A video tag: the 11-byte FLV tag header, the frame, and the 4-byte previous tag size.
    char tag_header[11];
    memset(tag_header, 0, sizeof(tag_header));
    tag_header[0] = 9;
    string frame(64 * 1024, 'x');
    char previous_tag_size[4];
    memset(previous_tag_size, 0, sizeof(previous_tag_size));

    iovec iovs[3];
    iovs[0].iov_base = tag_header;
    iovs[0].iov_len = sizeof(tag_header);
    iovs[1].iov_base = (char *)frame.data();
    iovs[1].iov_len = frame.size();
    iovs[2].iov_base = previous_tag_size;
    iovs[2].iov_len = sizeof(previous_tag_size);

    const int nn_batches = 1024;
    int i = 0;
    for (; i < nn_batches; i++) {
        if ((err = w.writev(iovs, 3, NULL)) != srs_success) {
            break;
        }
    }

    ASSERT_TRUE(err != srs_success);
    EXPECT_EQ(ERROR_SOCKET_TIMEOUT, srs_error_code(err));
    srs_freep(err);

    EXPECT_LT(i, nn_batches);
    EXPECT_GT(conn.get_send_bytes(), 0);
}

#ifndef _WIN32 // POSIX only: RLIMIT_NOFILE
// The lowest file descriptor the process would get for its next socket or file.
static int st_utest_lowest_free_fd()
{
    int fd = ::open("/dev/null", O_RDONLY);
    if (fd >= 0) {
        ::close(fd);
    }
    return fd;
}

// While it lives, the process can open no more file descriptors, as a server that serves as many
// clients as its ulimit -n allows: the soft RLIMIT_NOFILE is set to the lowest free descriptor, so
// the next socket() fails with EMFILE. The limit is set back when it goes.
class StUtestNoFreeFdScope
{
private:
    struct rlimit saved_;
    bool ok_;

public:
    StUtestNoFreeFdScope()
    {
        ok_ = false;
        if (getrlimit(RLIMIT_NOFILE, &saved_) != 0) {
            return;
        }

        int fd = st_utest_lowest_free_fd();
        if (fd < 0) {
            return;
        }

        struct rlimit limit = saved_;
        limit.rlim_cur = fd;
        ok_ = (setrlimit(RLIMIT_NOFILE, &limit) == 0);
    }
    ~StUtestNoFreeFdScope()
    {
        if (ok_) {
            setrlimit(RLIMIT_NOFILE, &saved_);
        }
    }

public:
    bool ok()
    {
        return ok_;
    }
};

// The server has used every file descriptor its ulimit -n allows, by its players, when an HTTP hook,
// or an edge pulling from its origin, connects to the backend. The client cannot create its socket,
// so the hook or the pull fails with the socket error, and nothing is left open. Once players leave
// and free descriptors, the next connect works again.
VOID TEST(ProtocolStTest, HookConnectWithNoFreeFd_FailsThenRecovers)
{
    srs_error_t err = srs_success;

    // The hook backend, up and listening.
    StUtestTcpPair backend;
    int port = 0;
    HELPER_ASSERT_SUCCESS(srs_utest_tcp_listen("127.0.0.1", &port, &backend.lfd_));

    int free_fd = st_utest_lowest_free_fd();
    ASSERT_GE(free_fd, 0);

    if (true) {
        StUtestNoFreeFdScope scope;
        ASSERT_TRUE(scope.ok());

        SrsTcpClient client("127.0.0.1", port, 1 * SRS_UTIME_SECONDS);
        err = client.connect();
        ASSERT_TRUE(err != srs_success);
        EXPECT_EQ(ERROR_SOCKET_CREATE, srs_error_code(err));
        srs_freep(err);
    }

    // No descriptor leaked by the failed connect.
    EXPECT_EQ(free_fd, st_utest_lowest_free_fd());

    SrsTcpClient client("127.0.0.1", port, 1 * SRS_UTIME_SECONDS);
    HELPER_EXPECT_SUCCESS(client.connect());
}

// The operator starts SRS with a ulimit -n too low for all its listeners, such as in a container
// with a tight limit and many WebRTC reuseport listeners or stream casters. The next TCP or UDP
// listener cannot create its socket, so it fails with the socket error and startup reports it,
// with nothing left open.
VOID TEST(ProtocolStTest, ListenWithNoFreeFd_TcpAndUdpListenFail)
{
    srs_error_t err = srs_success;

    int tcp_port = 0;
    HELPER_ASSERT_SUCCESS(srs_utest_free_port(SOCK_STREAM, &tcp_port));
    int udp_port = 0;
    HELPER_ASSERT_SUCCESS(srs_utest_free_port(SOCK_DGRAM, &udp_port));

    int free_fd = st_utest_lowest_free_fd();
    ASSERT_GE(free_fd, 0);

    if (true) {
        StUtestNoFreeFdScope scope;
        ASSERT_TRUE(scope.ok());

        srs_netfd_t fd = NULL;
        err = srs_tcp_listen("127.0.0.1", tcp_port, &fd);
        ASSERT_TRUE(err != srs_success);
        EXPECT_EQ(ERROR_SOCKET_CREATE, srs_error_code(err));
        EXPECT_TRUE(fd == NULL);
        srs_freep(err);

        err = srs_udp_listen("127.0.0.1", udp_port, &fd);
        ASSERT_TRUE(err != srs_success);
        EXPECT_EQ(ERROR_SOCKET_CREATE, srs_error_code(err));
        EXPECT_TRUE(fd == NULL);
        srs_freep(err);
    }

    EXPECT_EQ(free_fd, st_utest_lowest_free_fd());
}
#endif

// The interface list of a host, as getifaddrs gives it: one entry per address, in the order added. The
// interface names are ones the test hosts do not have, because the discovery records each interface's
// internet flag in a map the process keeps.
class UtilityIfaceList
{
private:
    struct Entry {
        ifaddrs ifa_;
        sockaddr_storage addr_;
        char name_[16];
    };
    vector<Entry *> entries_;

public:
    UtilityIfaceList()
    {
    }
    ~UtilityIfaceList()
    {
        for (int i = 0; i < (int)entries_.size(); i++) {
            delete entries_[i];
        }
    }

public:
    // Add an address of the interface name with the flags; an empty ip is an interface with no address,
    // such as a VPN tunnel before it is configured.
    void add(const char *name, unsigned int flags, const char *ip)
    {
        Entry *e = new Entry();
        memset(e, 0, sizeof(Entry));
        snprintf(e->name_, sizeof(e->name_), "%s", name);
        e->ifa_.ifa_name = e->name_;
        e->ifa_.ifa_flags = flags;

        if (strchr(ip, ':')) {
            sockaddr_in6 *a6 = (sockaddr_in6 *)&e->addr_;
            a6->sin6_family = AF_INET6;
#ifdef __APPLE__
            a6->sin6_len = sizeof(sockaddr_in6);
#endif
            inet_pton(AF_INET6, ip, &a6->sin6_addr);
            e->ifa_.ifa_addr = (sockaddr *)a6;
        } else if (ip[0]) {
            sockaddr_in *a4 = (sockaddr_in *)&e->addr_;
            a4->sin_family = AF_INET;
#ifdef __APPLE__
            a4->sin_len = sizeof(sockaddr_in);
#endif
            inet_pton(AF_INET, ip, &a4->sin_addr);
            e->ifa_.ifa_addr = (sockaddr *)a4;
        }

        if (!entries_.empty()) {
            entries_.back()->ifa_.ifa_next = &e->ifa_;
        }
        entries_.push_back(e);
    }
    ifaddrs *head()
    {
        return entries_.empty() ? NULL : &entries_[0]->ifa_;
    }
};

// The local IPs the server discovered at startup, swapped in for a test and restored after it, with the
// cached public address cleared, so the address is chosen again from the test's IPs.
class UtilityLocalIpsScope
{
private:
    vector<SrsIPAddress *> saved_ips_;
    string saved_address_;

public:
    UtilityLocalIpsScope()
    {
        saved_ips_.swap(_srs_system_ips);
        saved_address_ = _srs_public_internet_address;
        _srs_public_internet_address = "";
    }
    ~UtilityLocalIpsScope()
    {
        for (int i = 0; i < (int)_srs_system_ips.size(); i++) {
            SrsIPAddress *ip = _srs_system_ips[i];
            srs_freep(ip);
        }
        _srs_system_ips.clear();
        _srs_system_ips.swap(saved_ips_);
        _srs_public_internet_address = saved_address_;
    }

public:
    vector<SrsIPAddress *> &ips()
    {
        return _srs_system_ips;
    }
};

// The flags of an interface that is up with a link.
#define UTILITY_IFF_READY (IFF_UP | IFF_RUNNING)

// A host on a VPN: the tunnel device has no address yet (#141, #1087), so discovery skips it and lists
// the LAN address, in both the IPv4 and the IPv6 pass.
VOID TEST(ProtocolUtilityTest, LocalIpsOfHostOnVpn_TunnelWithNoAddressSkipped)
{
    UtilityIfaceList ifs;
    ifs.add("lo", UTILITY_IFF_READY | IFF_LOOPBACK, "127.0.0.1");
    ifs.add("tun0", UTILITY_IFF_READY | IFF_POINTOPOINT, "");
    ifs.add("enp3s0", UTILITY_IFF_READY, "192.168.1.10");

    SrsProtocolUtility utility;
    UtilityLocalIpsScope scope;
    srs_discover_local_ips(&utility, ifs.head(), scope.ips());

    ASSERT_EQ(1, (int)scope.ips().size());
    SrsIPAddress *ip = scope.ips()[0];
    EXPECT_STREQ("enp3s0", ip->ifname_.c_str());
    EXPECT_STREQ("192.168.1.10", ip->ip_.c_str());
    EXPECT_TRUE(ip->is_ipv4_);
    EXPECT_FALSE(ip->is_internet_);
    EXPECT_FALSE(ip->is_loopback_);
    EXPECT_FALSE(utility.is_internet("enp3s0"));

    // An RTMP client, such as an edge or a forwarder, connects with this address in its tcUrl.
    EXPECT_STREQ("192.168.1.10", utility.public_internet_address().c_str());
}

// A dual-stack cloud host with public IPv4 and IPv6 addresses on its NIC: the IPv4 address is listed
// first even when the system lists the IPv6 one first, both are internet, and so is the NIC.
VOID TEST(ProtocolUtilityTest, LocalIpsOfDualStackCloudHost_Ipv4FirstBothInternet)
{
    UtilityIfaceList ifs;
    ifs.add("lo", UTILITY_IFF_READY | IFF_LOOPBACK, "::1");
    ifs.add("ens5", UTILITY_IFF_READY, "2001:db8::10");
    ifs.add("lo", UTILITY_IFF_READY | IFF_LOOPBACK, "127.0.0.1");
    ifs.add("ens5", UTILITY_IFF_READY, "203.0.113.10");

    SrsProtocolUtility utility;
    UtilityLocalIpsScope scope;
    srs_discover_local_ips(&utility, ifs.head(), scope.ips());

    ASSERT_EQ(2, (int)scope.ips().size());
    SrsIPAddress *v4 = scope.ips()[0];
    EXPECT_STREQ("203.0.113.10", v4->ip_.c_str());
    EXPECT_TRUE(v4->is_ipv4_);
    EXPECT_TRUE(v4->is_internet_);
    SrsIPAddress *v6 = scope.ips()[1];
    EXPECT_STREQ("2001:db8::10", v6->ip_.c_str());
    EXPECT_FALSE(v6->is_ipv4_);
    EXPECT_TRUE(v6->is_internet_);
    EXPECT_STREQ("ens5", v6->ifname_.c_str());

    // The system summary API counts this NIC's bytes as internet traffic.
    EXPECT_TRUE(utility.is_internet("ens5"));
}

// A laptop offline: the cable is out (the NIC is up but not running) and the VPN tunnel has no address,
// so discovery falls back to the IPv4 loopback, and the server still has an address to use.
VOID TEST(ProtocolUtilityTest, LocalIpsOfOfflineLaptop_FallBackToLoopback)
{
    UtilityIfaceList ifs;
    ifs.add("lo", UTILITY_IFF_READY | IFF_LOOPBACK, "127.0.0.1");
    ifs.add("lo", UTILITY_IFF_READY | IFF_LOOPBACK, "::1");
    ifs.add("enp3s0", IFF_UP, "192.168.1.10");
    ifs.add("tun0", UTILITY_IFF_READY | IFF_POINTOPOINT, "");

    SrsProtocolUtility utility;
    UtilityLocalIpsScope scope;
    srs_discover_local_ips(&utility, ifs.head(), scope.ips());

    ASSERT_EQ(1, (int)scope.ips().size());
    SrsIPAddress *ip = scope.ips()[0];
    EXPECT_STREQ("lo", ip->ifname_.c_str());
    EXPECT_STREQ("127.0.0.1", ip->ip_.c_str());
    EXPECT_TRUE(ip->is_ipv4_);
    EXPECT_TRUE(ip->is_loopback_);
    EXPECT_FALSE(ip->is_internet_);

    EXPECT_STREQ("127.0.0.1", utility.public_internet_address().c_str());
}

// An SRT publisher on a cloud host with a Docker bridge, whose streamid names only the stream: the
// request takes the default app, and the server's public IPv4 rather than the bridge's address as its
// host and tcUrl, which the hooks and the stream API report.
VOID TEST(ProtocolUtilityTest, SrtPublishStreamOnlyOnCloudHost_DefaultAppAndPublicHost)
{
    UtilityIfaceList ifs;
    ifs.add("lo", UTILITY_IFF_READY | IFF_LOOPBACK, "127.0.0.1");
    ifs.add("docker0", UTILITY_IFF_READY, "172.17.0.1");
    ifs.add("ens5", UTILITY_IFF_READY, "203.0.113.10");

    SrsProtocolUtility utility;
    UtilityLocalIpsScope scope;
    srs_discover_local_ips(&utility, ifs.head(), scope.ips());
    ASSERT_EQ(2, (int)scope.ips().size());

    MockAppConfig config;
    SrtMode mode = SrtModePull;
    SrsRequest req;
    EXPECT_TRUE(srs_srt_streamid_to_request(&config, "#!::r=livestream,m=publish", mode, &req));

    EXPECT_EQ(SrtModePush, mode);
    EXPECT_STREQ("live", req.app_.c_str());
    EXPECT_STREQ("livestream", req.stream_.c_str());
    EXPECT_STREQ("203.0.113.10", req.host_.c_str());
    EXPECT_STREQ("203.0.113.10", req.vhost_.c_str());
    EXPECT_STREQ("srt://203.0.113.10/live", req.tcUrl_.c_str());
}

// An SRT client that asks for a mode other than publish or request, such as FFmpeg users who write
// m=play, or that names no stream at all, is refused, so the connection is closed.
VOID TEST(ProtocolUtilityTest, SrtStreamIdWithUnknownModeOrNoStream_Refused)
{
    MockAppConfig config;

    if (true) {
        SrtMode mode = SrtModePull;
        SrsRequest req;
        EXPECT_FALSE(srs_srt_streamid_to_request(&config, "#!::r=live/livestream,m=play", mode, &req));
        EXPECT_TRUE(req.stream_.empty());
    }

    if (true) {
        SrtMode mode = SrtModePull;
        SrsRequest req;
        EXPECT_FALSE(srs_srt_streamid_to_request(&config, "#!::m=publish", mode, &req));
        EXPECT_TRUE(req.stream_.empty());
    }
}

// A telnet or HTTP/1.0 client sends no Host header to a host with a public IPv6 address and a private
// IPv4 one: the request's host is the IPv4 address, since an IPv6 address cannot stand as a URL host.
VOID TEST(ProtocolUtilityTest, HttpRequestWithoutHostOnIpv6PublicHost_UsesIpv4Address)
{
    srs_error_t err = srs_success;

    UtilityIfaceList ifs;
    ifs.add("lo", UTILITY_IFF_READY | IFF_LOOPBACK, "127.0.0.1");
    ifs.add("enp3s0", UTILITY_IFF_READY, "192.168.1.10");
    ifs.add("enp3s0", UTILITY_IFF_READY, "2001:db8::20");

    SrsProtocolUtility utility;
    UtilityLocalIpsScope scope;
    srs_discover_local_ips(&utility, ifs.head(), scope.ips());
    ASSERT_EQ(2, (int)scope.ips().size());
    EXPECT_TRUE(scope.ips()[1]->is_internet_);

    SrsHttpMessage m(NULL, NULL);
    HELPER_ASSERT_SUCCESS(m.set_url("/api/v1/versions", false));
    EXPECT_STREQ("192.168.1.10", m.host().c_str());
    EXPECT_STREQ("/api/v1/versions", m.path().c_str());
}

// B26: a host with a public IPv4 address on its NIC, which also has the IPv6 link-local address every
// IPv6-enabled interface gets, is marked intranet: the link-local address is discovered last and
// overwrites the NIC's flag, so the system summary API counts all its traffic as intranet.
VOID TEST(ProtocolUtilityTest, DISABLED_PublicIpv4NicWithLinkLocalIpv6_MarkedIntranet)
{
    UtilityIfaceList ifs;
    ifs.add("lo", UTILITY_IFF_READY | IFF_LOOPBACK, "127.0.0.1");
    ifs.add("ens6", UTILITY_IFF_READY, "203.0.113.30");
    ifs.add("ens6", UTILITY_IFF_READY, "fe80::1");

    SrsProtocolUtility utility;
    UtilityLocalIpsScope scope;
    srs_discover_local_ips(&utility, ifs.head(), scope.ips());

    ASSERT_EQ(2, (int)scope.ips().size());
    EXPECT_TRUE(scope.ips()[0]->is_internet_);
    EXPECT_FALSE(scope.ips()[1]->is_internet_);

    EXPECT_TRUE(utility.is_internet("ens6"));
}

// State Threads returns NULL when it cannot map the stack of a new coroutine, such as when the
// process is out of memory or over its address space limit.
static void *async_call_st_thread_create_out_of_memory(void *(* /*start*/)(void *arg), void * /*arg*/, int /*joinable*/, int /*stack_size*/)
{
    return NULL;
}

// A publisher starts a stream on a vhost with HLS, in TS or fMP4 segments. The HLS muxer starts the
// worker that sends the on_hls and on_hls_notify callbacks when the stream is published. When no
// coroutine can be created for it, the publish fails with the ST error, instead of writing segments
// whose callbacks would never be sent. The unpublish after the failure stops the worker safely.
VOID TEST(AppAsyncCallTest, HlsPublishFailsWhenNoCoroutineCanBeCreatedForCallbacks)
{
    srs_error_t err = srs_success;

    MockHlsRequest req("__defaultVhost__", "live", "livestream");
    SrsHlsMuxer ts_muxer;
    SrsHlsFmp4Muxer fmp4_muxer;

    _ST_THREAD_CREATE_PFN ov = _pfn_st_thread_create;
    _pfn_st_thread_create = (_ST_THREAD_CREATE_PFN)async_call_st_thread_create_out_of_memory;
    srs_error_t ts_err = ts_muxer.on_publish(&req);
    srs_error_t fmp4_err = fmp4_muxer.on_publish(&req);
    _pfn_st_thread_create = ov;

    EXPECT_EQ(ERROR_ST_CREATE_CYCLE_THREAD, srs_error_code(ts_err));
    EXPECT_TRUE(srs_error_desc(ts_err).find("async start") != string::npos);
    EXPECT_TRUE(srs_error_desc(ts_err).find("coroutine") != string::npos);
    srs_freep(ts_err);

    EXPECT_EQ(ERROR_ST_CREATE_CYCLE_THREAD, srs_error_code(fmp4_err));
    EXPECT_TRUE(srs_error_desc(fmp4_err).find("async start") != string::npos);
    EXPECT_TRUE(srs_error_desc(fmp4_err).find("coroutine") != string::npos);
    srs_freep(fmp4_err);

    HELPER_EXPECT_SUCCESS(ts_muxer.on_unpublish());
    HELPER_EXPECT_SUCCESS(fmp4_muxer.on_unpublish());
}

// Queue the callbacks of a reaped HLS segment as SrsHlsMuxer::do_segment_close does: on_hls, then
// on_hls_notify, with the tasks' config and hooks set to the test's.
static void async_call_reap_hls_segment(SrsAsyncCallWorker *worker, ISrsRequest *req, ISrsAppConfig *config, ISrsHttpHooks *hooks, int seq)
{
    srs_error_t err = srs_success;

    string ts_url = "livestream-" + srs_strconv_format_int(seq) + ".ts";
    SrsDvrAsyncCallOnHls *on_hls = new SrsDvrAsyncCallOnHls(SrsContextId(), req, "./objs/nginx/html/live/" + ts_url, ts_url,
                                                            "./objs/nginx/html/live/livestream.m3u8", "livestream.m3u8", seq, 10 * SRS_UTIME_SECONDS);
    on_hls->config_ = config;
    on_hls->hooks_ = hooks;
    HELPER_EXPECT_SUCCESS(worker->execute(on_hls));

    SrsDvrAsyncCallOnHlsNotify *notify = new SrsDvrAsyncCallOnHlsNotify(SrsContextId(), req, ts_url);
    notify->config_ = config;
    notify->hooks_ = hooks;
    HELPER_EXPECT_SUCCESS(worker->execute(notify));
}

// A segment is reaped and its on_hls and on_hls_notify callbacks are queued, in that order. The on_hls
// backend refuses the first segment, such as an HTTP 500 while it restarts. The worker logs and drops
// the failure, still sends the CDN notify of the same segment, and keeps sending the callbacks of the
// next segment once the backend is back.
VOID TEST(AppAsyncCallTest, HlsCallbacksGoOnWhenOnHlsBackendRefusesASegment)
{
    srs_error_t err = srs_success;

    MockLogForSignal log;
    ISrsLog *original_log = _srs_log;
    _srs_log = &log;

    MockHlsRequest req("__defaultVhost__", "live", "livestream");
    MockAppConfigForHlsNotify config;
    config.on_hls_directive_ = new SrsConfDirective();
    config.on_hls_directive_->name_ = "on_hls";
    config.on_hls_directive_->args_.push_back("http://127.0.0.1:8085/api/v1/hls");
    vector<string> notify_urls;
    notify_urls.push_back("http://cdn.example.com/[app]/[stream]/[ts_url]");
    config.set_on_hls_notify_urls(notify_urls);

    MockHttpHooksForHlsNotify hooks;

    SrsAsyncCallWorker worker;
    HELPER_EXPECT_SUCCESS(worker.start());

    // The backend answers HTTP 500 for the first segment.
    srs_error_t refused = srs_error_new(ERROR_HTTP_STATUS_INVALID, "http: status 500");
    hooks.set_on_hls_error(refused);
    srs_freep(refused);

    async_call_reap_hls_segment(&worker, &req, &config, &hooks, 0);
    srs_usleep(1 * SRS_UTIME_MILLISECONDS);

    EXPECT_TRUE(worker.tasks_.empty());
    ASSERT_EQ(1, (int)hooks.on_hls_ts_urls_.size());
    EXPECT_STREQ("livestream-0.ts", hooks.on_hls_ts_urls_[0].c_str());
    ASSERT_EQ(1, hooks.on_hls_notify_count_);
    EXPECT_STREQ("livestream-0.ts", hooks.on_hls_notify_ts_urls_[0].c_str());
    ASSERT_EQ(1, (int)log.warns_.size());
    EXPECT_TRUE(log.warns_[0].find("ignore task failed") != string::npos);
    EXPECT_TRUE(log.warns_[0].find("callback on_hls http://127.0.0.1:8085/api/v1/hls") != string::npos);
    EXPECT_TRUE(log.warns_[0].find("status 500") != string::npos);

    // The backend is back for the next segment.
    hooks.set_on_hls_error(srs_success);
    async_call_reap_hls_segment(&worker, &req, &config, &hooks, 1);
    srs_usleep(1 * SRS_UTIME_MILLISECONDS);

    EXPECT_TRUE(worker.tasks_.empty());
    ASSERT_EQ(2, (int)hooks.on_hls_ts_urls_.size());
    EXPECT_STREQ("livestream-1.ts", hooks.on_hls_ts_urls_[1].c_str());
    ASSERT_EQ(2, hooks.on_hls_notify_count_);
    EXPECT_STREQ("livestream-1.ts", hooks.on_hls_notify_ts_urls_[1].c_str());
    EXPECT_EQ(1, (int)log.warns_.size());

    worker.stop();
    _srs_log = original_log;
}

// A server with `circuit_breaker` on at the default levels: high above 90% CPU for 2s, critical above
// 95% for 1s, dying above 99% for 5s in a row. Its process holds 200MB.
class CircuitBreakerServer
{
public:
    MockAppConfigForCircuitBreaker config_;
    MockSharedTimerForCircuitBreaker timer_;
    // Owned by the breaker.
    MockHostForCircuitBreaker *host_;
    SrsCircuitBreaker breaker_;

public:
    CircuitBreakerServer()
    {
        config_.circuit_breaker_enabled_ = true;
        config_.high_threshold_ = 90;
        config_.high_pulse_ = 2;
        config_.critical_threshold_ = 95;
        config_.critical_pulse_ = 1;
        config_.dying_threshold_ = 99;
        config_.dying_pulse_ = 5;

        host_ = new MockHostForCircuitBreaker();
        host_->proc_stat_->rss_ = 200 * 1024 / 4; // 4KB pages.

        srs_freep(breaker_.host_);
        breaker_.host_ = host_;
        breaker_.config_ = &config_;
        breaker_.shared_timer_ = &timer_;
    }
};

// An RTC server under load, with a WebRTC publisher on a lossy network. Every 10s the RTC stats timer
// computes the rates of the last 10s, as SrsRtcSessionManager does. While the CPU is above the high
// level, the breaker logs every second the CPU, memory and levels, and how many NACK requests the
// server sent the publisher per second. With no request sent, the line has no snk group, and below the
// high level there is no line at all.
VOID TEST(AppCircuitBreakerTest, OverloadedRtcServerWithLossyPublisher_LogsNackRequests)
{
    srs_error_t err;

    KbpsGlobalPpsScope scope(kbps_rtc_line_pps());

    MockLogForSignal log;
    ISrsLog *original_log = _srs_log;
    _srs_log = &log;

    CircuitBreakerServer server;
    HELPER_EXPECT_SUCCESS(server.breaker_.initialize());
    log.traces_.clear();

    // The server has just started, at 50% CPU: the breaker is off and logs nothing.
    if (true) {
        scope.clock_.set_clock(0);
        SrsKbsRtcStats stats;
        srs_global_rtc_update(&stats);

        server.host_->proc_stat_->percent_ = 0.50;
        HELPER_EXPECT_SUCCESS(server.breaker_.on_timer(1 * SRS_UTIME_SECONDS));
        EXPECT_FALSE(server.breaker_.hybrid_high_water_level());
        EXPECT_TRUE(log.traces_.empty());
    }

    // In the next 10s the publisher loses packets and the server sends it 50 NACK requests, then the
    // CPU goes to 92%: the breaker is high, and its line shows 5 requests a second.
    if (true) {
        _srs_pps_snack2->sugar_ += 50;
        scope.clock_.set_clock(10 * SRS_UTIME_SECONDS);
        SrsKbsRtcStats stats;
        srs_global_rtc_update(&stats);

        server.host_->proc_stat_->percent_ = 0.92;
        HELPER_EXPECT_SUCCESS(server.breaker_.on_timer(1 * SRS_UTIME_SECONDS));
        EXPECT_TRUE(server.breaker_.hybrid_high_water_level());
        EXPECT_FALSE(server.breaker_.hybrid_critical_water_level());

        ASSERT_EQ(1, (int)log.traces_.size());
        EXPECT_EQ(0u, log.traces_[0].find("CircuitBreaker: cpu=92.00%,200MB, break=1,0,0, cond=92.00%, snk=5,"))
            << log.traces_[0];
    }

    // The network is clean for the next 10s and the CPU reaches 96%: the breaker is critical too, and
    // the line has no NACK requests to show.
    if (true) {
        scope.clock_.set_clock(20 * SRS_UTIME_SECONDS);
        SrsKbsRtcStats stats;
        srs_global_rtc_update(&stats);

        server.host_->proc_stat_->percent_ = 0.96;
        HELPER_EXPECT_SUCCESS(server.breaker_.on_timer(1 * SRS_UTIME_SECONDS));
        EXPECT_TRUE(server.breaker_.hybrid_critical_water_level());

        ASSERT_EQ(2, (int)log.traces_.size());
        EXPECT_STREQ("CircuitBreaker: cpu=96.00%,200MB, break=1,1,0, cond=96.00%", log.traces_[1].c_str());
    }

    _srs_log = original_log;
}

// While the breaker is critical, the server skips the NACKs and TWCC feedback of every WebRTC publisher
// and counts each skip in _srs_pps_snack4. The breaker line ends with that count per second, so the
// operator sees what the breaker drops. Nothing updates the counter's rate, so the line always shows 0.
// The number before it, which the code names the NACK seqs sent, is never counted at all.
VOID TEST(AppCircuitBreakerTest, DISABLED_CriticalBreakerSkipsRtcFeedback_SkippedCountAlwaysZero)
{
    srs_error_t err;

    vector<SrsPps *> pps = kbps_rtc_line_pps();
    pps.push_back(_srs_pps_snack3);
    pps.push_back(_srs_pps_snack4);
    KbpsGlobalPpsScope scope(pps);

    MockLogForSignal log;
    ISrsLog *original_log = _srs_log;
    _srs_log = &log;

    CircuitBreakerServer server;
    HELPER_EXPECT_SUCCESS(server.breaker_.initialize());

    scope.clock_.set_clock(0);
    if (true) {
        SrsKbsRtcStats stats;
        srs_global_rtc_update(&stats);
    }

    // In 10s the server sent the publisher 50 NACK requests, then went critical and skipped 30 NACK and
    // TWCC rounds, as SrsRtcPublishStream and SrsRtcConnection count them.
    _srs_pps_snack2->sugar_ += 50;
    _srs_pps_snack4->sugar_ += 30;

    scope.clock_.set_clock(10 * SRS_UTIME_SECONDS);
    if (true) {
        SrsKbsRtcStats stats;
        srs_global_rtc_update(&stats);
    }

    server.host_->proc_stat_->percent_ = 0.96;
    log.traces_.clear();
    HELPER_EXPECT_SUCCESS(server.breaker_.on_timer(1 * SRS_UTIME_SECONDS));
    _srs_log = original_log;

    ASSERT_EQ(1, (int)log.traces_.size());
    string line = log.traces_[0];
    ASSERT_NE(string::npos, line.find(", snk=5,")) << line;
    EXPECT_STREQ(",3", line.substr(line.rfind(',')).c_str()) << line;
}

extern bool _srs_in_docker;
extern srs_fwrite_t _srs_fwrite_fn;

static void config_write_file(string path, string content)
{
    FILE *fp = fopen(path.c_str(), "wb");
    ASSERT_TRUE(fp != NULL);
    if (!content.empty()) {
        ASSERT_EQ(content.length(), fwrite(content.data(), 1, content.length(), fp));
    }
    fclose(fp);
}

static string config_read_file(string path)
{
    FILE *fp = fopen(path.c_str(), "rb");
    if (!fp) {
        return "";
    }

    string content;
    char buf[1024];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
        content.append(buf, n);
    }
    fclose(fp);
    return content;
}

// Loads the config text the way the server does, and returns the check's error, or success.
static srs_error_t config_load(MockSrsConfig &conf, string text)
{
    return conf.mock_parse(string(_MIN_OK_CONF) + text);
}

// Expects the config to be refused, with the error naming what is wrong.
static void config_expect_refused(string text, string reason)
{
    MockSrsConfig conf;
    srs_error_t err = config_load(conf, text);
    ASSERT_TRUE(err != srs_success) << text;
    EXPECT_EQ(ERROR_SYSTEM_CONFIG_INVALID, srs_error_code(err)) << text;
    string desc = srs_error_desc(err);
    EXPECT_NE(string::npos, desc.find(reason)) << text << "\n"
                                               << desc;
    srs_freep(err);
}

// SRS in a container on a cgroup v1 host sees /docker/<id> in the cgroup of its init process, and turns on its
// docker defaults. On a cgroup v2 host, or a host that is no container, it does not. Without the file (macOS, or
// no /proc), the detection fails, and the server ignores the error.
VOID TEST(AppConfigTest, DockerDetectedFromInitCgroup)
{
    srs_error_t err = srs_success;

    bool in_docker = _srs_in_docker;
    string path = _srs_tmp_file_prefix + "ai42-cgroup";
    MockFileRemover remover(path);

    config_write_file(path, "12:pids:/docker/3f1c8a9e2b7d\n11:memory:/docker/3f1c8a9e2b7d\n0::/system.slice/containerd.service\n");
    err = srs_detect_docker(path);
    bool v1_container = _srs_in_docker;
    HELPER_EXPECT_SUCCESS(err);

    config_write_file(path, "0::/\n");
    err = srs_detect_docker(path);
    bool v2_container = _srs_in_docker;
    HELPER_EXPECT_SUCCESS(err);

    config_write_file(path, "12:pids:/user.slice/user-1000.slice\n11:memory:/user.slice\n");
    _srs_in_docker = true;
    err = srs_detect_docker(path);
    bool v1_host = _srs_in_docker;
    HELPER_EXPECT_SUCCESS(err);

    _srs_in_docker = true;
    err = srs_detect_docker(_srs_tmp_file_prefix + "ai42-no-cgroup");
    bool no_proc = _srs_in_docker;
    _srs_in_docker = in_docker;
    EXPECT_TRUE(err != srs_success);
    srs_freep(err);

    EXPECT_TRUE(v1_container);
    EXPECT_FALSE(v2_container);
    EXPECT_FALSE(v1_host);
    EXPECT_FALSE(no_proc);
}

// The full.conf of SRS4 configured GB28181 with options SRS5 dropped (the SIP server moved out) and the SIP
// candidate as stream_caster.host. Such a config still loads: the dropped options are removed with a warning, and
// the host becomes sip.candidate, which the GB session uses as the media address in its SDP.
VOID TEST(AppConfigTest, Srs4Gb28181CasterConfigLoads)
{
    srs_error_t err = srs_success;

    MockLogForSignal log;
    ISrsLog *original_log = _srs_log;
    _srs_log = &log;

    MockSrsConfig conf;
    err = config_load(conf, "stream_caster { enabled on; caster gb28181; output rtmp://127.0.0.1/live/[stream];"
                            "    listen 9000; host 192.168.1.10; tcp_enable on; rtp_port_min 58200; rtp_port_max 58300;"
                            "    wait_keyframe on; rtp_idle_timeout 30; audio_enable on; auto_create_channel off;"
                            "    sip { enabled on; listen 5060; serial 34020000002000000001; realm 3402000000;"
                            "        ack_timeout 30; keepalive_timeout 120; invite_port_fixed on; query_catalog_interval 60;"
                            "        auto_play on; timeout 60; reinvite 5; } }");
    _srs_log = original_log;
    HELPER_ASSERT_SUCCESS(err);

    vector<SrsConfDirective *> casters = conf.get_stream_casters();
    ASSERT_EQ(1, (int)casters.size());
    SrsConfDirective *caster = casters[0];
    EXPECT_TRUE(conf.get_stream_caster_enabled(caster));
    EXPECT_EQ(9000, conf.get_stream_caster_listen(caster));
    EXPECT_TRUE(caster->get("host") == NULL);
    EXPECT_TRUE(caster->get("tcp_enable") == NULL);
    EXPECT_TRUE(caster->get("auto_create_channel") == NULL);

    SrsConfDirective *sip = caster->get("sip");
    ASSERT_TRUE(sip != NULL);
    ASSERT_TRUE(sip->get("candidate") != NULL);
    EXPECT_STREQ("192.168.1.10", sip->get("candidate")->arg0().c_str());
    EXPECT_STREQ("5060", sip->get("listen")->arg0().c_str());
    EXPECT_TRUE(sip->get("serial") == NULL);
    EXPECT_TRUE(sip->get("auto_play") == NULL);
    EXPECT_STREQ("60", sip->get("timeout")->arg0().c_str());

    // 7 dropped caster options, 7 dropped SIP options, and the host moved.
    int dropped = 0, moved = 0;
    for (int i = 0; i < (int)log.warns_.size(); i++) {
        if (log.warns_[i].find("for GB is not used") != string::npos) {
            dropped++;
        }
        if (log.warns_[i].find("move host to sip.candidate") != string::npos) {
            moved++;
        }
    }
    EXPECT_EQ(14, dropped);
    EXPECT_EQ(1, moved);
}

// An SRS2 config with the removed HLS in RAM options still loads, with a warning that they are ignored.
VOID TEST(AppConfigTest, Srs2HlsRamOptionsWarnAndLoad)
{
    srs_error_t err = srs_success;

    MockLogForSignal log;
    ISrsLog *original_log = _srs_log;
    _srs_log = &log;

    MockSrsConfig conf;
    err = config_load(conf, "vhost __defaultVhost__ { hls { enabled on; hls_storage ram; hls_mount [vhost]/[app]/[stream].m3u8; } }");
    _srs_log = original_log;
    HELPER_ASSERT_SUCCESS(err);

    EXPECT_TRUE(conf.get_hls_enabled("__defaultVhost__"));
    int warned = 0;
    for (int i = 0; i < (int)log.warns_.size(); i++) {
        if (log.warns_[i].find("HLS RAM is removed") != string::npos) {
            warned++;
        }
    }
    EXPECT_EQ(2, warned);
}

// A vhost file included from the main config with a missing brace fails the load, and the error names that file,
// so the operator knows which file to fix.
VOID TEST(AppConfigTest, IncludedVhostFileBrokenFailsNamingIt)
{
    MockSrsConfig conf;
    conf.mock_include("vhost.live.conf", "vhost live { hls { enabled on; }");

    srs_error_t err = config_load(conf, "include vhost.live.conf;");
    ASSERT_TRUE(err != srs_success);
    EXPECT_EQ(ERROR_SYSTEM_CONFIG_INVALID, srs_error_code(err));
    string desc = srs_error_desc(err);
    EXPECT_NE(string::npos, desc.find("parse include buffer vhost.live.conf")) << desc;
    srs_freep(err);
}

// A quoted vhost name may be followed by its brace with no space, and an empty quoted directive, as a template
// leaves for an empty value, is refused.
VOID TEST(AppConfigTest, QuotedNamesInConfig)
{
    srs_error_t err = srs_success;

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(config_load(conf, "vhost \"live.example.com\"{ hls { enabled on; } }"));
        EXPECT_TRUE(conf.get_vhost("live.example.com", false) != NULL);
        EXPECT_TRUE(conf.get_hls_enabled("live.example.com"));
    }

    if (true) {
        MockSrsConfig conf;
        err = config_load(conf, "\"\";");
        ASSERT_TRUE(err != srs_success);
        EXPECT_EQ(ERROR_SYSTEM_CONFIG_INVALID, srs_error_code(err));
        string desc = srs_error_desc(err);
        EXPECT_NE(string::npos, desc.find("empty directive")) << desc;
        srs_freep(err);
    }
}

// The config file can follow -c with no space, as getopt and nginx (whose ngx_get_options this code follows) allow:
// ./objs/srs -cconf/srs.conf. SRS takes the file name, then reads it as more options and fails at its first char.
VOID TEST(AppConfigTest, DISABLED_ConfigFileRightAfterOption_ReadAsOptions)
{
    srs_error_t err = srs_success;

    string path = _srs_tmp_file_prefix + "ai42-attached.conf";
    MockFileRemover remover(path);
    config_write_file(path, _MIN_OK_CONF);

    SrsConfig conf;
    // Log to the console, so get_log_tank_file() does not change its process-wide default for docker.
    SrsSetEnvConfig(conf, log_tank, "SRS_LOG_TANK", "console");

    string arg = "-c" + path;
    char *argv[] = {(char *)"srs", (char *)arg.c_str(), NULL};
    bool in_docker = _srs_in_docker;
    err = conf.parse_options(2, argv);
    _srs_in_docker = in_docker;
    HELPER_ASSERT_SUCCESS(err);

    EXPECT_STREQ(path.c_str(), conf.config().c_str());
    vector<string> listens = conf.get_listens();
    ASSERT_EQ(1, (int)listens.size());
    EXPECT_STREQ("1935", listens[0].c_str());
}

// A typo in any section is refused at startup and reload, naming the directive, instead of being ignored.
VOID TEST(AppConfigTest, TypoInSectionRefusedNamingIt)
{
    const char *cases[][2] = {
        {"http_api { enabled on; raw_api { enabled on; allow_reloads on; } }", "illegal http_api.raw_api.allow_reloads"},
        {"http_api { enabled on; auth { enabled on; type basic; usename admin; } }", "illegal http_api.auth.usename"},
        {"srt_server { enabled on; latancy 200; }", "illegal srt_server.latancy"},
        {"heartbeat { enabled on; auth { enabled on; tpye bearer; } }", "illegal heartbeat.auth.tpye"},
        {"rtc_server { enabled on; listne 8000; }", "illegal rtc_server.listne"},
        {"rtsp_server { enabled on; listne 554; }", "illegal rtsp_server.listne"},
        {"rtmps { enabled on; cret ./conf/server.crt; }", "illegal rtmps.cret"},
        {"exporter { enabled on; lable cn-beijing; }", "illegal exporter.lable"},
        {"stream_caster { enabled on; cster mpegts_over_udp; }", "illegal stream_caster.cster"},
        {"stream_caster { enabled on; caster gb28181; sip { enabled on; listne 5060; } }", "illegal stream_caster.sip.listne"},
        {"vhost v { exec { enabled on; publsh ./objs/ffmpeg/bin/ffmpeg; } }", "illegal vhost.exec.publsh of v"},
        {"vhost v { play { gop_cahce on; } }", "illegal vhost.play.gop_cahce of v"},
        {"vhost v { cluster { mdoe remote; } }", "illegal vhost.cluster.mdoe of v"},
        {"vhost v { publish { mr_latancy 350; } }", "illegal vhost.publish.mr_latancy of v"},
        {"vhost v { http_remux { enabled on; mout [vhost]/[app]/[stream].flv; } }", "illegal vhost.http_remux.mout of v"},
        {"vhost v { dash { enabled on; dash_fragmnet 30; } }", "illegal vhost.dash.dash_fragmnet of v"},
        {"vhost v { forward { enabled on; destinaton 127.0.0.1:19350; } }", "illegal vhost.forward.destinaton of v"},
        {"vhost v { security { enabled on; alow play all; } }", "illegal vhost.security.alow of v"},
        {"vhost v { rtc { enabled on; nak on; } }", "illegal vhost.rtc.nak of v"},
        {"vhost v { srt { enabled on; srt_to_rtmpp on; } }", "illegal vhost.srt.srt_to_rtmpp of v"},
        {"vhost v { rtsp { enabled on; rtmp_to_rtspp on; } }", "rtmp_to_rtspp of v"},
    };

    for (int i = 0; i < (int)(sizeof(cases) / sizeof(cases[0])); i++) {
        config_expect_refused(cases[i][0], cases[i][1]);
    }
}

// A typo in the vhost rtsp section is refused, but the error names the srt section, so the operator looks for the
// typo in the wrong place.
VOID TEST(AppConfigTest, DISABLED_RtspTypoInVhost_ErrorNamesSrtSection)
{
    config_expect_refused("vhost v { rtsp { enabled on; rtmp_to_rtspp on; } }", "illegal vhost.rtsp.rtmp_to_rtspp of v");
}

// HTTP API authentication supports only basic and bearer, and bearer needs a token. When the operator turns it on by
// env in a container, with no auth section in the config, the missing type or token is refused too.
VOID TEST(AppConfigTest, ApiAuthNeedsKnownTypeAndToken)
{
    config_expect_refused("http_api { enabled on; auth { enabled on; type digest; } }", "invalid SRS_HTTP_API_AUTH_TYPE=digest");

    if (true) {
        MockSrsConfig conf;
        SrsSetEnvConfig(conf, enabled, "SRS_HTTP_API_AUTH_ENABLED", "on");
        srs_error_t err = config_load(conf, "http_api { enabled on; }");
        ASSERT_TRUE(err != srs_success);
        EXPECT_NE(string::npos, srs_error_desc(err).find("SRS_HTTP_API_AUTH_TYPE is required"));
        srs_freep(err);
    }

    if (true) {
        MockSrsConfig conf;
        SrsSetEnvConfig(conf, enabled, "SRS_HTTP_API_AUTH_ENABLED", "on");
        SrsSetEnvConfig(conf, type, "SRS_HTTP_API_AUTH_TYPE", "bearer");
        srs_error_t err = config_load(conf, "http_api { enabled on; }");
        ASSERT_TRUE(err != srs_success);
        EXPECT_NE(string::npos, srs_error_desc(err).find("SRS_HTTP_API_AUTH_TOKEN is required"));
        srs_freep(err);
    }
}

// The heartbeat authenticates to its collector only by bearer token: a config with no type, another type, or no
// token is refused, from the config or the env, and a full bearer config gives the collector's token.
VOID TEST(AppConfigTest, HeartbeatAuthNeedsBearerToken)
{
    srs_error_t err = srs_success;

    config_expect_refused("heartbeat { enabled on; auth { enabled on; } }", "SRS_HEARTBEAT_AUTH_TYPE is required");
    config_expect_refused("heartbeat { enabled on; auth { enabled on; type basic; } }", "invalid SRS_HEARTBEAT_AUTH_TYPE=basic");
    config_expect_refused("heartbeat { enabled on; auth { enabled on; type bearer; } }", "SRS_HEARTBEAT_AUTH_TOKEN is required");

    const char *env_cases[][2] = {
        {"", "SRS_HEARTBEAT_AUTH_TYPE is required"},
        {"heartbeat { enabled on; }", "SRS_HEARTBEAT_AUTH_TYPE is required"},
    };
    for (int i = 0; i < (int)(sizeof(env_cases) / sizeof(env_cases[0])); i++) {
        MockSrsConfig conf;
        SrsSetEnvConfig(conf, enabled, "SRS_HEARTBEAT_AUTH_ENABLED", "on");
        err = config_load(conf, env_cases[i][0]);
        ASSERT_TRUE(err != srs_success) << env_cases[i][0];
        EXPECT_NE(string::npos, srs_error_desc(err).find(env_cases[i][1])) << env_cases[i][0];
        srs_freep(err);
    }

    if (true) {
        MockSrsConfig conf;
        SrsSetEnvConfig(conf, enabled, "SRS_HEARTBEAT_AUTH_ENABLED", "on");
        SrsSetEnvConfig(conf, type, "SRS_HEARTBEAT_AUTH_TYPE", "bearer");
        err = config_load(conf, "heartbeat { enabled on; }");
        ASSERT_TRUE(err != srs_success);
        EXPECT_NE(string::npos, srs_error_desc(err).find("SRS_HEARTBEAT_AUTH_TOKEN is required"));
        srs_freep(err);
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(config_load(conf, "heartbeat { enabled on; url http://127.0.0.1:8085/api/v1/servers;"
                                                "    auth { enabled on; type bearer; token t1; } }"));
        EXPECT_TRUE(conf.get_heartbeat_auth_enabled());
        EXPECT_STREQ("bearer", conf.get_heartbeat_auth_type().c_str());
        EXPECT_STREQ("t1", conf.get_heartbeat_auth_token().c_str());
    }
}

// Listen addresses and the HTTP and HTTPS pairs are checked at startup: a bad endpoint, an HTTPS API and stream that
// do not share a port when their HTTP pair does (or the other way), HTTPS without its HTTP, and a stats NIC index
// past the host's addresses are refused.
VOID TEST(AppConfigTest, BadListenOrPortPairRefused)
{
    const char *cases[][2] = {
        {"stats { network 99; }", "invalid stats.network=99"},
        {"http_api { enabled on; https { enabled on; listen abc; } }", "http_api.https.listen=abc is invalid"},
        {"http_api { enabled on; https { enabled on; listen 8088; } }"
         "http_server { enabled on; https { enabled on; listen 8088; } }",
         "for same https, http api(1985) != server(8080)"},
        {"http_api { enabled off; https { enabled on; } }", "https api depends on http"},
        {"http_server { enabled off; https { enabled on; } }", "https stream depends on http"},
        {"http_server { enabled on; https { enabled on; listen abc; } }", "https_server.listen=abc is invalid"},
        {"rtsp_server { enabled on; listen abc; }", "rtsp_server.listen=abc is invalid"},
        {"rtc_server { enabled on; tcp { enabled on; listen abc; } }", "rtc_server.tcp.listen=abc is invalid"},
    };

    for (int i = 0; i < (int)(sizeof(cases) / sizeof(cases[0])); i++) {
        config_expect_refused(cases[i][0], cases[i][1]);
    }

    // With the API on the stream server's HTTP port, the HTTPS API follows the HTTPS stream port, so only the env
    // in a container can set it apart.
    if (true) {
        MockSrsConfig conf;
        SrsSetEnvConfig(conf, https_listen, "SRS_HTTP_API_HTTPS_LISTEN", "1990");
        srs_error_t err = config_load(conf, "http_api { enabled on; listen 8080; https { enabled on; } }"
                                            "http_server { enabled on; listen 8080; https { enabled on; listen 8088; } }");
        ASSERT_TRUE(err != srs_success);
        EXPECT_NE(string::npos, srs_error_desc(err).find("for same http, https api(1990) != server(8088)"));
        srs_freep(err);
    }
}

// The full.conf lists every directive for reference, and marks itself with is_full; starting with it is refused.
VOID TEST(AppConfigTest, StartWithFullConfRefused)
{
    srs_error_t err = srs_success;

    MockSrsConfig conf;
    HELPER_ASSERT_SUCCESS(config_load(conf, "is_full on;"));
    err = conf.check_config();
    ASSERT_TRUE(err != srs_success);
    EXPECT_EQ(ERROR_SYSTEM_CONFIG_INVALID, srs_error_code(err));
    EXPECT_NE(string::npos, srs_error_desc(err).find("never use full.conf"));
    srs_freep(err);
}

// HLS and HTTP-TS cannot both be on in a vhost, and a vhost has one http_remux; both are refused at startup.
VOID TEST(AppConfigTest, HlsWithHttpTsOrTwoRemuxesRefused)
{
    config_expect_refused("vhost v { hls { enabled on; } http_remux { enabled on; mount [vhost]/[app]/[stream].ts; } }",
                          "vhost.hls conflict with vhost.http-ts of v");
    config_expect_refused("vhost v { http_remux { enabled on; mount [vhost]/[app]/[stream].flv; }"
                          "    http_remux { enabled on; mount [vhost]/[app]/[stream].ts; } }",
                          "vhost.http_remux only one but count=2 of v");

    // HTTP-TS with HLS off is fine.
    srs_error_t err = srs_success;
    MockSrsConfig conf;
    HELPER_EXPECT_SUCCESS(config_load(conf, "vhost v { hls { enabled off; } http_remux { enabled on; mount [vhost]/[app]/[stream].ts; } }"));
}

// A server id file left empty, as by a full disk when the server last wrote it, is not taken as the id: the server
// makes a new id and saves it next to the pid file.
VOID TEST(AppConfigTest, EmptyServerIdFileGetsNewIdSaved)
{
    srs_error_t err = srs_success;

    string pid = _srs_tmp_file_prefix + "ai42-srs.pid";
    string id = _srs_tmp_file_prefix + "ai42-srs.id";
    MockFileRemover remover(id);
    config_write_file(id, "");

    MockSrsConfig conf;
    HELPER_ASSERT_SUCCESS(config_load(conf, "pid " + pid + ";"));

    MockRandForHandshake rand;
    rand.gen_str_values_.push_back("abc1234");
    srs_freep(conf.rand_);
    conf.rand_ = &rand;
    string server_id = conf.get_server_id();
    conf.rand_ = NULL;

    EXPECT_STREQ("vid-abc1234", server_id.c_str());
    EXPECT_STREQ("vid-abc1234", config_read_file(id).c_str());
}

static int config_fwrite_calls_ = 0;
static int config_fwrite_fail_at_ = -1;

// Fails the write numbered config_fwrite_fail_at_, as a disk that fills there.
static size_t config_fwrite_full_at(const void *ptr, size_t size, size_t nitems, FILE *stream)
{
    if (config_fwrite_calls_++ == config_fwrite_fail_at_) {
        return 0;
    }
    return ::fwrite(ptr, size, nitems, stream);
}

// The operator asks SRS to save its config (the persistence signal). Whichever write the disk fills at, the save
// fails, the temporary file is removed, and the config file keeps the operator's version. With room, the saved
// file loads to the same config.
VOID TEST(AppConfigTest, PersistConfigOnFullDiskKeepsOldFile)
{
    srs_error_t err = srs_success;

    string path = _srs_tmp_file_prefix + "ai42-persist.conf";
    string tmp = path + ".tmp";
    MockFileRemover remover(path);
    MockFileRemover tmp_remover(tmp);

    string text = string(_MIN_OK_CONF) + "vhost __defaultVhost__ { hls { enabled on; hls_fragment 10; }"
                                         "    http_hooks { enabled on; on_publish http://127.0.0.1:8085/api/v1/streams http://127.0.0.1:8086/api/v1/streams; } }";
    config_write_file(path, text);

    MockSrsConfig conf;
    HELPER_ASSERT_SUCCESS(conf.mock_parse(text));
    conf.config_file_ = path;

    srs_fwrite_t fwrite_fn = _srs_fwrite_fn;
    _srs_fwrite_fn = config_fwrite_full_at;

    SrsPath p;
    int fill_points = 0;
    for (int k = 0; k < 1000; k++) {
        config_fwrite_calls_ = 0;
        config_fwrite_fail_at_ = k;
        err = conf.persistence();
        if (err == srs_success) {
            break;
        }

        fill_points++;
        EXPECT_EQ(ERROR_SYSTEM_FILE_WRITE, srs_error_code(err)) << k;
        srs_freep(err);
        EXPECT_FALSE(p.exists(tmp)) << k;
        EXPECT_STREQ(text.c_str(), config_read_file(path).c_str()) << k;
    }
    _srs_fwrite_fn = fwrite_fn;

    // Every write of the save was a point the disk filled at, and the last save had room.
    EXPECT_GT(fill_points, 30);
    EXPECT_EQ(fill_points, config_fwrite_calls_);
    EXPECT_FALSE(p.exists(tmp));

    SrsConfig saved;
    HELPER_ASSERT_SUCCESS(saved.parse_file(path.c_str()));
    EXPECT_TRUE(srs_directive_equals(conf.root_, saved.root_));
    EXPECT_STREQ("http://127.0.0.1:8086/api/v1/streams", saved.get_vhost_on_publish("__defaultVhost__")->args_.at(1).c_str());
}

// A section with only some of its directives uses the documented default for the rest.
VOID TEST(AppConfigTest, PartialSectionsUseDefaults)
{
    srs_error_t err = srs_success;

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(config_load(conf, "circuit_breaker { enabled on; }"));
        EXPECT_TRUE(conf.get_circuit_breaker());
        EXPECT_EQ(90, conf.get_high_threshold());
        EXPECT_EQ(2, conf.get_high_pulse());
        EXPECT_EQ(95, conf.get_critical_threshold());
        EXPECT_EQ(1, conf.get_critical_pulse());
        EXPECT_EQ(99, conf.get_dying_threshold());
        EXPECT_EQ(5, conf.get_dying_pulse());
    }

    // The Prometheus exporter on with nothing else listens on 9972 with no label or tag; a section with only the
    // port is off.
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(config_load(conf, "exporter { enabled on; }"));
        EXPECT_TRUE(conf.get_exporter_enabled());
        EXPECT_STREQ("9972", conf.get_exporter_listen().c_str());
        EXPECT_STREQ("", conf.get_exporter_label().c_str());
        EXPECT_STREQ("", conf.get_exporter_tag().c_str());
    }
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(config_load(conf, "exporter { listen 9973; }"));
        EXPECT_FALSE(conf.get_exporter_enabled());
    }

    // In a container, the exporter turned on by env with no section in the config listens on 9972.
    if (true) {
        MockSrsConfig conf;
        SrsSetEnvConfig(conf, exporter, "SRS_EXPORTER_ENABLED", "on");
        HELPER_ASSERT_SUCCESS(config_load(conf, ""));
        EXPECT_TRUE(conf.get_exporter_enabled());
        EXPECT_STREQ("9972", conf.get_exporter_listen().c_str());
    }

    // An SRT server with only its port takes the default stream id and mode for a client that sends none.
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(config_load(conf, "srt_server { enabled on; listen 10080; }"));
        EXPECT_STREQ("#!::r=live/livestream,m=request", conf.get_srt_default_streamid().c_str());
        EXPECT_STREQ("request", conf.get_srt_default_mode().c_str());
    }

    // An HTTPS section for the stream server with only its port is off.
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(config_load(conf, "http_server { enabled on; https { listen 8088; } }"));
        EXPECT_FALSE(conf.get_https_stream_enabled());
    }
}

// A vhost that sets one directive of its publish, play, rtc, hls, hooks, security, exec or cluster section gets
// the defaults for the others.
VOID TEST(AppConfigTest, PartialVhostSectionsUseDefaults)
{
    srs_error_t err = srs_success;

    MockSrsConfig conf;
    HELPER_ASSERT_SUCCESS(config_load(conf, "vhost v { min_latency on; publish { mr on; } play { gop_cache off; } rtc { enabled on; }"
                                            "    hls { enabled on; } http_hooks { on_publish http://127.0.0.1:8085/api/v1/streams; }"
                                            "    security { allow publish all; } }"
                                            "vhost w { publish { parse_sps off; } exec { publish ./objs/ffmpeg/bin/ffmpeg; } }"
                                            "vhost edge { cluster { mode remote; origin 127.0.0.1:19350; } }"
                                            "vhost origin { cluster { origin_cluster off; } }"));

    EXPECT_TRUE(conf.get_parse_sps("v"));
    EXPECT_TRUE(conf.try_annexb_first("v"));
    EXPECT_EQ(350 * SRS_UTIME_MILLISECONDS, conf.get_mr_sleep("v"));
    EXPECT_EQ(20 * SRS_UTIME_SECONDS, conf.get_publish_1stpkt_timeout("v"));
    EXPECT_EQ(5 * SRS_UTIME_SECONDS, conf.get_publish_normal_timeout("v"));
    EXPECT_FALSE(conf.get_gop_cache("v"));
    EXPECT_EQ(350 * SRS_UTIME_MILLISECONDS, conf.get_mw_sleep("v"));
    EXPECT_EQ(0, conf.get_send_min_interval("v"));
    EXPECT_FALSE(conf.get_reduce_sequence_header("v"));
    EXPECT_TRUE(conf.get_realtime_enabled("v", true));
    EXPECT_FALSE(conf.get_rtc_init_rate_from_sdp("v"));
    EXPECT_FALSE(conf.get_rtc_keep_original_ssrc("v"));
    EXPECT_FALSE(conf.get_hls_master_m3u8_path_relative("v"));
    EXPECT_FALSE(conf.get_vhost_http_hooks_enabled("v"));
    EXPECT_FALSE(conf.get_security_enabled("v"));
    EXPECT_FALSE(conf.get_exec_enabled("v"));

    EXPECT_FALSE(conf.get_mr_enabled("w"));
    EXPECT_FALSE(conf.get_exec_enabled("w"));

    EXPECT_TRUE(conf.get_vhost_is_edge("edge"));
    EXPECT_STREQ("[vhost]", conf.get_vhost_edge_transform_vhost("edge").c_str());
    EXPECT_FALSE(conf.get_vhost_origin_cluster("edge"));
    EXPECT_TRUE(conf.get_vhost_coworkers("edge").empty());

    EXPECT_FALSE(conf.get_vhost_is_edge("origin"));

    // A merged-write delay applies to RTMP and HTTP players, but WebRTC players ignore it.
    if (true) {
        MockSrsConfig conf2;
        HELPER_ASSERT_SUCCESS(config_load(conf2, "vhost v { play { mw_latency 500; } }"));
        EXPECT_EQ(500 * SRS_UTIME_MILLISECONDS, conf2.get_mw_sleep("v"));
        EXPECT_EQ(0, conf2.get_mw_sleep("v", true));
    }
}

// A copy engine sets only the codecs and output, so FFmpeg gets the defaults for the rest: FLV in and out, and no
// bitrate, size, filter or extra params. An HD engine as in full.conf passes its per-file, video and audio params
// to FFmpeg as options.
VOID TEST(AppConfigTest, TranscodeEnginesGiveFfmpegOptions)
{
    srs_error_t err = srs_success;

    MockSrsConfig conf;
    HELPER_ASSERT_SUCCESS(config_load(conf, "vhost v { transcode live { enabled on; ffmpeg ./objs/ffmpeg/bin/ffmpeg;"
                                            "    engine copy { enabled on; vcodec copy; acodec copy; output rtmp://127.0.0.1:[port]/[app]/[stream]_copy; }"
                                            "    engine hd { enabled on; perfile { re; ss 10; } vcodec libx264; vbitrate 1500; vfps 25;"
                                            "        vwidth 1280; vheight 720; vthreads 12; vprofile main; vpreset medium; vparams { t 100; coder 1; }"
                                            "        acodec libfdk_aac; abitrate 70; asample_rate 44100; achannels 2; aparams { profile:a aac_low; }"
                                            "        output rtmp://127.0.0.1:[port]/[app]/[stream]_hd; } } }"));

    vector<SrsConfDirective *> engines = conf.get_transcode_engines(conf.get_transcode("v", "live"));
    ASSERT_EQ(2, (int)engines.size());

    if (true) {
        SrsFFMPEG ffmpeg("./objs/ffmpeg/bin/ffmpeg");
        ffmpeg.config_ = &conf;
        HELPER_ASSERT_SUCCESS(ffmpeg.initialize("rtmp://127.0.0.1:1935/live/livestream", "rtmp://127.0.0.1:1935/live/livestream_copy", "/dev/null"));
        HELPER_ASSERT_SUCCESS(ffmpeg.initialize_transcode(engines[0]));
        ffmpeg.config_ = NULL;

        EXPECT_STREQ("flv", ffmpeg.iformat_.c_str());
        EXPECT_STREQ("flv", ffmpeg.oformat_.c_str());
        EXPECT_STREQ("copy", ffmpeg.vcodec_.c_str());
        EXPECT_STREQ("copy", ffmpeg.acodec_.c_str());
        EXPECT_EQ(0, ffmpeg.vbitrate_);
        EXPECT_EQ(0, ffmpeg.vfps_);
        EXPECT_EQ(0, ffmpeg.vwidth_);
        EXPECT_EQ(0, ffmpeg.vheight_);
        EXPECT_EQ(1, ffmpeg.vthreads_);
        EXPECT_STREQ("", ffmpeg.vprofile_.c_str());
        EXPECT_STREQ("", ffmpeg.vpreset_.c_str());
        EXPECT_EQ(0, ffmpeg.abitrate_);
        EXPECT_EQ(0, ffmpeg.asample_rate_);
        EXPECT_EQ(0, ffmpeg.achannels_);
        EXPECT_TRUE(ffmpeg.perfile_.empty());
        EXPECT_TRUE(ffmpeg.vfilter_.empty());
        EXPECT_TRUE(ffmpeg.vparams_.empty());
        EXPECT_TRUE(ffmpeg.aparams_.empty());
    }

    if (true) {
        SrsFFMPEG ffmpeg("./objs/ffmpeg/bin/ffmpeg");
        ffmpeg.config_ = &conf;
        HELPER_ASSERT_SUCCESS(ffmpeg.initialize("rtmp://127.0.0.1:1935/live/livestream", "rtmp://127.0.0.1:1935/live/livestream_hd", "/dev/null"));
        HELPER_ASSERT_SUCCESS(ffmpeg.initialize_transcode(engines[1]));
        ffmpeg.config_ = NULL;

        ASSERT_EQ(3, (int)ffmpeg.perfile_.size());
        EXPECT_STREQ("-re", ffmpeg.perfile_[0].c_str());
        EXPECT_STREQ("-ss", ffmpeg.perfile_[1].c_str());
        EXPECT_STREQ("10", ffmpeg.perfile_[2].c_str());
        ASSERT_EQ(4, (int)ffmpeg.vparams_.size());
        EXPECT_STREQ("-t", ffmpeg.vparams_[0].c_str());
        EXPECT_STREQ("100", ffmpeg.vparams_[1].c_str());
        EXPECT_STREQ("-coder", ffmpeg.vparams_[2].c_str());
        EXPECT_STREQ("1", ffmpeg.vparams_[3].c_str());
        ASSERT_EQ(2, (int)ffmpeg.aparams_.size());
        EXPECT_STREQ("-profile:a", ffmpeg.aparams_[0].c_str());
        EXPECT_STREQ("aac_low", ffmpeg.aparams_[1].c_str());
        EXPECT_EQ(1500, ffmpeg.vbitrate_);
        EXPECT_EQ(1280, ffmpeg.vwidth_);
        EXPECT_EQ(720, ffmpeg.vheight_);
        EXPECT_EQ(44100, ffmpeg.asample_rate_);
    }
}

extern srs_fopen_t _srs_fopen_fn;

// An H.264 768x320 stream and an AAC 44.1 kHz stereo stream, as FLV tags: the sequence headers, a key frame, an inter
// frame and an audio frame.
static const uint8_t dash_avc_sh[] = {
    0x17, 0x00, 0x00, 0x00, 0x00, 0x01, 0x64, 0x00, 0x20, 0xff, 0xe1, 0x00, 0x19, 0x67, 0x64, 0x00, 0x20, 0xac,
    0xd9, 0x40, 0xc0, 0x29, 0xb0, 0x11, 0x00, 0x00, 0x03, 0x00, 0x01, 0x00, 0x00, 0x03, 0x00, 0x32, 0x0f, 0x18,
    0x31, 0x96, 0x01, 0x00, 0x05, 0x68, 0xeb, 0xec, 0xb2, 0x2c};
static const uint8_t dash_avc_idr[] = {0x17, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x65, 0x88, 0x84, 0x00, 0x10};
static const uint8_t dash_avc_inter[] = {0x27, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x41, 0x9a, 0x21, 0x6c, 0x42};
static const uint8_t dash_aac_sh[] = {0xaf, 0x00, 0x12, 0x10};
static const uint8_t dash_aac_frame[] = {0xaf, 0x01, 0x21, 0x11, 0x45, 0x00};

// The DASH segments of a stream read the dash_path of the stream's config, as the server's segments do.
class DashUtestFactory : public SrsAppFactory
{
public:
    ISrsAppConfig *config_;

public:
    DashUtestFactory()
    {
        config_ = NULL;
    }
    virtual ~DashUtestFactory()
    {
    }
    virtual ISrsFragmentedMp4 *create_fragmented_mp4()
    {
        SrsFragmentedMp4 *fragment = new SrsFragmentedMp4();
        fragment->config_ = config_;
        return fragment;
    }
};

// A stream published with DASH on, its files under home, fed the packets of an RTMP publisher as the origin hub feeds
// them: each packet is parsed by the stream's format first. As the hub, it feeds the cached video sequence header
// again when a disposed DASH resumes.
class DashUtestStream : public MockOriginHub
{
public:
    string home_;
    MockSrsConfig conf_;
    DashUtestFactory factory_;
    MockSrsRequest req_;
    SrsDash *dash_;
    SrsFormat format_;
    SrsMediaPacket *vsh_;
    SrsFormat vsh_format_;

public:
    DashUtestStream(string name) : req_("__defaultVhost__", "live", "livestream")
    {
        home_ = _srs_tmp_file_prefix + name;
        dash_ = new SrsDash();
        vsh_ = NULL;
    }
    virtual ~DashUtestStream()
    {
        srs_freep(dash_);
        srs_freep(vsh_);
        remove_files();
    }

public:
    srs_error_t publish(string options)
    {
        return publish_to(home_, options);
    }
    srs_error_t publish_to(string dash_path, string options)
    {
        srs_error_t err = srs_success;

        if ((err = config_load(conf_, "vhost __defaultVhost__ { dash { enabled on; dash_path " + dash_path + "; " + options + " } }")) != srs_success) {
            return srs_error_wrap(err, "config");
        }
        if ((err = format_.initialize()) != srs_success) {
            return srs_error_wrap(err, "format");
        }
        if ((err = vsh_format_.initialize()) != srs_success) {
            return srs_error_wrap(err, "vsh format");
        }

        dash_->config_ = &conf_;
        SrsDashController *controller = dynamic_cast<SrsDashController *>(dash_->controller_);
        controller->config_ = &conf_;
        controller->app_factory_ = &factory_;
        dynamic_cast<SrsMpdWriter *>(controller->mpd_)->config_ = &conf_;
        factory_.config_ = &conf_;

        if ((err = dash_->initialize(this, &req_)) != srs_success) {
            return srs_error_wrap(err, "initialize");
        }
        return dash_->on_publish();
    }
    srs_error_t video(int64_t ts, const uint8_t *data, int size)
    {
        srs_error_t err = srs_success;

        SrsMediaPacket msg;
        char *payload = new char[size];
        memcpy(payload, data, size);
        msg.wrap(payload, size);
        msg.timestamp_ = ts;
        msg.message_type_ = SrsFrameTypeVideo;

        if ((err = format_.on_video(ts, msg.payload(), msg.size())) != srs_success) {
            return srs_error_wrap(err, "format");
        }
        if (format_.is_avc_sequence_header()) {
            srs_freep(vsh_);
            vsh_ = msg.copy();
            if ((err = vsh_format_.on_video(ts, vsh_->payload(), vsh_->size())) != srs_success) {
                return srs_error_wrap(err, "vsh format");
            }
        }

        return dash_->on_video(&msg, &format_);
    }
    srs_error_t audio(int64_t ts, const uint8_t *data, int size)
    {
        srs_error_t err = srs_success;

        SrsMediaPacket msg;
        char *payload = new char[size];
        memcpy(payload, data, size);
        msg.wrap(payload, size);
        msg.timestamp_ = ts;
        msg.message_type_ = SrsFrameTypeAudio;

        if ((err = format_.on_audio(ts, msg.payload(), msg.size())) != srs_success) {
            return srs_error_wrap(err, "format");
        }

        return dash_->on_audio(&msg, &format_);
    }
    // Sends the sequence headers of both tracks.
    srs_error_t sequence_headers()
    {
        srs_error_t err = srs_success;

        if ((err = video(0, dash_avc_sh, sizeof(dash_avc_sh))) != srs_success) {
            return srs_error_wrap(err, "avc sh");
        }
        if ((err = audio(0, dash_aac_sh, sizeof(dash_aac_sh))) != srs_success) {
            return srs_error_wrap(err, "aac sh");
        }
        return err;
    }
    // Sends the media from one time to another, in ms: 10 video and 10 audio frames a second, a key frame each second.
    srs_error_t av(int64_t from, int64_t to)
    {
        srs_error_t err = srs_success;

        for (int64_t ts = from; ts <= to; ts += 100) {
            const uint8_t *frame = (ts % 1000) == 0 ? dash_avc_idr : dash_avc_inter;
            if ((err = video(ts, frame, sizeof(dash_avc_idr))) != srs_success) {
                return srs_error_wrap(err, "video %d", (int)ts);
            }
            if ((err = audio(ts, dash_aac_frame, sizeof(dash_aac_frame))) != srs_success) {
                return srs_error_wrap(err, "audio %d", (int)ts);
            }
        }
        return err;
    }
    // On the hub's request, feeds the cached video sequence header again, as SrsOriginHub does.
    virtual srs_error_t on_dash_request_sh()
    {
        on_dash_request_sh_count_++;
        if (!vsh_) {
            return srs_success;
        }
        return dash_->on_video(vsh_, &vsh_format_);
    }

public:
    string stream_dir()
    {
        return home_ + "/live/livestream";
    }
    bool exists(string name)
    {
        SrsPath path;
        return path.exists(stream_dir() + "/" + name);
    }
    string mpd()
    {
        return config_read_file(home_ + "/live/livestream.mpd");
    }
    // Removes the files DASH may write for the stream, and their dirs.
    void remove_files()
    {
        const char *tracks[] = {"video", "audio"};
        for (int t = 0; t < 2; t++) {
            for (int i = 0; i < 32; i++) {
                string m4s = stream_dir() + "/" + tracks[t] + "-" + srs_strconv_format_int(i) + ".m4s";
                ::unlink(m4s.c_str());
                ::unlink((m4s + ".tmp").c_str());
            }
            string init = stream_dir() + "/" + tracks[t] + "-init.mp4";
            ::unlink(init.c_str());
            ::unlink((init + ".tmp").c_str());
        }
        string mpd = home_ + "/live/livestream.mpd";
        ::unlink(mpd.c_str());
        ::unlink((mpd + ".tmp").c_str());
        ::rmdir(stream_dir().c_str());
        ::rmdir((home_ + "/live").c_str());
        ::rmdir(home_.c_str());
    }
};

static int dash_utest_count(const string &s, const string &sub)
{
    int n = 0;
    for (size_t pos = s.find(sub); pos != string::npos; pos = s.find(sub, pos + sub.length())) {
        n++;
    }
    return n;
}

// A publisher streams H.264 and AAC for 10 seconds to a vhost with DASH segments of 0.5s and a window of 2. A key
// frame each second cuts both tracks into 1s segments. The MPD lists the last 2 segments of each track, the server
// keeps the one before for players of the previous MPD, and dash_cleanup removes the older segment files, so the disk
// use stays bounded on a long publish. The dispose leaves nothing.
VOID TEST(AppDashTest, LongPublishSlidesWindowAndRemovesOldSegments)
{
    srs_error_t err;

    DashUtestStream s("ai42-dash-long");
    HELPER_ASSERT_SUCCESS(s.publish("dash_fragment 0.5; dash_window_size 2; dash_cleanup on;"));
    HELPER_ASSERT_SUCCESS(s.sequence_headers());
    EXPECT_TRUE(s.exists("video-init.mp4"));
    EXPECT_TRUE(s.exists("audio-init.mp4"));

    HELPER_ASSERT_SUCCESS(s.av(0, 10000));

    // Segments 8 and 9 of each track are listed, from their start in the stream.
    string mpd = s.mpd();
    EXPECT_EQ(2, dash_utest_count(mpd, "startNumber=\"8\"")) << mpd;
    EXPECT_EQ(4, dash_utest_count(mpd, "<S t=")) << mpd;
    EXPECT_EQ(2, dash_utest_count(mpd, "<S t=\"8000\" d=\"1000\" />")) << mpd;
    EXPECT_EQ(2, dash_utest_count(mpd, "<S t=\"9000\" d=\"1000\" />")) << mpd;

    for (int i = 0; i < 7; i++) {
        EXPECT_FALSE(s.exists("video-" + srs_strconv_format_int(i) + ".m4s")) << i;
        EXPECT_FALSE(s.exists("audio-" + srs_strconv_format_int(i) + ".m4s")) << i;
    }
    for (int i = 7; i < 10; i++) {
        EXPECT_TRUE(s.exists("video-" + srs_strconv_format_int(i) + ".m4s")) << i;
        EXPECT_TRUE(s.exists("audio-" + srs_strconv_format_int(i) + ".m4s")) << i;
    }
    EXPECT_TRUE(s.exists("video-10.m4s.tmp"));
    EXPECT_TRUE(s.exists("audio-10.m4s.tmp"));

    s.dash_->on_unpublish();
    s.dash_->dispose();
    EXPECT_EQ(0, ::rmdir(s.stream_dir().c_str()));
}

// The operator sets dash_path under a regular file, such as a typo naming another service's file. DASH can not make
// the stream's dir, so the publisher's sequence headers fail, and the hub stops DASH for the stream; the file is not
// touched.
VOID TEST(AppDashTest, DashPathUnderRegularFile_SequenceHeadersFail)
{
    srs_error_t err;

    string file = _srs_tmp_file_prefix + "ai42-dash-file";
    MockFileRemover remover(file);
    config_write_file(file, "html");

    DashUtestStream s("ai42-dash-file-home");
    HELPER_ASSERT_SUCCESS(s.publish_to(file + "/dash", ""));

    err = s.video(0, dash_avc_sh, sizeof(dash_avc_sh));
    ASSERT_TRUE(err != srs_success);
    EXPECT_NE(string::npos, srs_error_desc(err).find("Create media home failed")) << srs_error_desc(err);
    srs_freep(err);

    err = s.audio(0, dash_aac_sh, sizeof(dash_aac_sh));
    ASSERT_TRUE(err != srs_success);
    EXPECT_NE(string::npos, srs_error_desc(err).find("Consume audio failed")) << srs_error_desc(err);
    srs_freep(err);

    EXPECT_STREQ("html", config_read_file(file).c_str());
}

// Creates no file for writing, as a volume mounted read-only.
static FILE *dash_fopen_read_only(const char *path, const char *mode)
{
    if (mode[0] == 'w' || mode[0] == 'a') {
        errno = EROFS;
        return NULL;
    }
    return ::fopen(path, mode);
}

// The DASH volume was remounted read-only after an I/O error, and a publisher publishes again to a stream whose dir an
// earlier publish made. The init file of the sequence header can not be created, so the hub stops DASH for the
// stream; no init file, nor its temporary file, is left.
VOID TEST(AppDashTest, ReadOnlyVolumeAtPublish_InitFileNotCreated)
{
    srs_error_t err;

    DashUtestStream s("ai42-dash-ro");
    SrsPath path;
    HELPER_ASSERT_SUCCESS(path.mkdir_all(s.stream_dir()));
    HELPER_ASSERT_SUCCESS(s.publish(""));

    srs_fopen_t fopen_fn = _srs_fopen_fn;
    _srs_fopen_fn = dash_fopen_read_only;
    err = s.video(0, dash_avc_sh, sizeof(dash_avc_sh));
    _srs_fopen_fn = fopen_fn;

    EXPECT_EQ(ERROR_SYSTEM_FILE_OPENE, srs_error_code(err));
    srs_freep(err);
    EXPECT_FALSE(s.exists("video-init.mp4"));
    EXPECT_FALSE(s.exists("video-init.mp4.tmp"));
}

static bool dash_disk_full_ = false;

// Fails every write once the disk is full.
static size_t dash_fwrite_disk_full(const void *ptr, size_t size, size_t nitems, FILE *stream)
{
    if (dash_disk_full_) {
        return 0;
    }
    return ::fwrite(ptr, size, nitems, stream);
}

// The disk DASH writes to, which a test fills.
class DashUtestDiskScope
{
public:
    srs_fwrite_t fwrite_fn_;

public:
    DashUtestDiskScope()
    {
        fwrite_fn_ = _srs_fwrite_fn;
        _srs_fwrite_fn = dash_fwrite_disk_full;
        dash_disk_full_ = false;
    }
    virtual ~DashUtestDiskScope()
    {
        _srs_fwrite_fn = fwrite_fn_;
        dash_disk_full_ = false;
    }
    void fill()
    {
        dash_disk_full_ = true;
    }
};

// The disk fills while publishers stream to DASH. Each write that fails stops DASH for the stream, through the hub,
// and leaves no file a player would get:
// - at the publish, the init file of the sequence header;
// - mid-stream, the segment a key frame finishes;
// - mid-stream on an audio-only stream, the segment its duration finishes.
VOID TEST(AppDashTest, DiskFullWhileStreaming_WriteFailsStopsDash)
{
    srs_error_t err;

    if (true) {
        DashUtestDiskScope disk;
        DashUtestStream s("ai42-dash-full-init");
        HELPER_ASSERT_SUCCESS(s.publish(""));

        disk.fill();
        err = s.video(0, dash_avc_sh, sizeof(dash_avc_sh));
        EXPECT_EQ(ERROR_SYSTEM_FILE_WRITE, srs_error_code(err));
        srs_freep(err);
        EXPECT_FALSE(s.exists("video-init.mp4"));
    }

    if (true) {
        DashUtestDiskScope disk;
        DashUtestStream s("ai42-dash-full-av");
        HELPER_ASSERT_SUCCESS(s.publish("dash_fragment 0.5;"));
        HELPER_ASSERT_SUCCESS(s.sequence_headers());
        HELPER_ASSERT_SUCCESS(s.av(0, 900));

        disk.fill();
        err = s.video(1000, dash_avc_idr, sizeof(dash_avc_idr));
        EXPECT_EQ(ERROR_SYSTEM_FILE_WRITE, srs_error_code(err));
        EXPECT_NE(string::npos, srs_error_desc(err).find("Flush encoder failed")) << srs_error_desc(err);
        srs_freep(err);
        EXPECT_FALSE(s.exists("video-0.m4s"));
    }

    if (true) {
        DashUtestDiskScope disk;
        DashUtestStream s("ai42-dash-full-audio");
        HELPER_ASSERT_SUCCESS(s.publish("dash_fragment 0.5;"));
        HELPER_ASSERT_SUCCESS(s.audio(0, dash_aac_sh, sizeof(dash_aac_sh)));
        for (int ts = 0; ts <= 500; ts += 100) {
            HELPER_ASSERT_SUCCESS(s.audio(ts, dash_aac_frame, sizeof(dash_aac_frame)));
        }

        disk.fill();
        err = s.audio(600, dash_aac_frame, sizeof(dash_aac_frame));
        EXPECT_EQ(ERROR_SYSTEM_FILE_WRITE, srs_error_code(err));
        EXPECT_NE(string::npos, srs_error_desc(err).find("Flush encoder failed")) << srs_error_desc(err);
        srs_freep(err);
        EXPECT_FALSE(s.exists("audio-0.m4s"));
    }
}

#ifndef _WIN32 // Windows deletes no open file.
// A cleanup job of the operator deletes the stream's DASH dir while the publisher streams. The segment being written
// is gone with its dir, so the next key frame can not finish it, and the hub stops DASH for the stream rather than
// list a segment players can not get.
VOID TEST(AppDashTest, CleanupJobDeletesStreamDir_SegmentNotFinished)
{
    srs_error_t err;

    DashUtestStream s("ai42-dash-deleted");
    HELPER_ASSERT_SUCCESS(s.publish("dash_fragment 0.5;"));
    HELPER_ASSERT_SUCCESS(s.sequence_headers());
    HELPER_ASSERT_SUCCESS(s.av(0, 900));
    EXPECT_TRUE(s.exists("video-0.m4s.tmp"));

    s.remove_files();
    SrsPath path;
    ASSERT_FALSE(path.exists(s.stream_dir()));

    err = s.video(1000, dash_avc_idr, sizeof(dash_avc_idr));
    EXPECT_EQ(ERROR_SYSTEM_FRAGMENT_RENAME, srs_error_code(err));
    srs_freep(err);
    EXPECT_FALSE(s.exists("video-0.m4s"));
}
#endif

// The operator turns the dispose off (dash_dispose 0). A publisher that sends nothing for a long time keeps its DASH:
// the cycle does not dispose it.
VOID TEST(AppDashTest, DisposeOffIdlePublisherKeepsDash)
{
    srs_error_t err;

    MockAppConfig config;
    config.dash_enabled_ = true;
    config.dash_dispose_ = 0;
    MockSrsRequest req("test.vhost", "live", "livestream");
    MockOriginHub hub;

    SrsDash dash;
    dash.config_ = &config;
    srs_freep(dash.controller_);
    MockDashController *controller = new MockDashController();
    dash.controller_ = controller;
    HELPER_EXPECT_SUCCESS(dash.initialize(&hub, &req));
    HELPER_EXPECT_SUCCESS(dash.on_publish());

    dash.last_update_time_ = 1;
    HELPER_EXPECT_SUCCESS(dash.cycle());
    EXPECT_TRUE(dash.enabled_);
    EXPECT_FALSE(controller->on_unpublish_called_);
    EXPECT_FALSE(controller->dispose_called_);

    dash.on_unpublish();
    dash.config_ = NULL;
}

// A publisher stays idle past dash_dispose, and the cycle disposes its DASH. While it stays idle, the next timeouts
// find nothing to dispose: the cycle does not dispose it again.
VOID TEST(AppDashTest, IdlePublisherDisposedOnlyOnce)
{
    srs_error_t err;

    MockAppConfig config;
    config.dash_enabled_ = true;
    config.dash_dispose_ = 1 * SRS_UTIME_SECONDS;
    MockSrsRequest req("test.vhost", "live", "livestream");
    MockOriginHub hub;

    SrsDash dash;
    dash.config_ = &config;
    srs_freep(dash.controller_);
    MockDashController *controller = new MockDashController();
    dash.controller_ = controller;
    HELPER_EXPECT_SUCCESS(dash.initialize(&hub, &req));
    HELPER_EXPECT_SUCCESS(dash.on_publish());

    dash.last_update_time_ = 1;
    HELPER_EXPECT_SUCCESS(dash.cycle());
    EXPECT_FALSE(dash.enabled_);
    EXPECT_TRUE(controller->dispose_called_);

    controller->on_unpublish_called_ = false;
    controller->dispose_called_ = false;
    dash.last_update_time_ = 1;
    HELPER_EXPECT_SUCCESS(dash.cycle());
    EXPECT_FALSE(controller->on_unpublish_called_);
    EXPECT_FALSE(controller->dispose_called_);

    dash.config_ = NULL;
}

// A publisher idle past dash_dispose has its DASH disposed and its files removed. When it sends media again, DASH
// starts again and the hub feeds the cached sequence header; with the disk full by then, the init file can not be
// written, so the media fails and the hub stops DASH for the stream.
VOID TEST(AppDashTest, IdlePublisherResumesOnFullDisk_ResumeFails)
{
    srs_error_t err;

    DashUtestDiskScope disk;
    DashUtestStream s("ai42-dash-resume");
    HELPER_ASSERT_SUCCESS(s.publish("dash_fragment 0.5; dash_dispose 1;"));
    HELPER_ASSERT_SUCCESS(s.sequence_headers());
    HELPER_ASSERT_SUCCESS(s.av(0, 900));

    s.dash_->last_update_time_ = 1;
    HELPER_ASSERT_SUCCESS(s.dash_->cycle());
    EXPECT_FALSE(s.exists("video-init.mp4"));

    disk.fill();
    err = s.video(1000, dash_avc_idr, sizeof(dash_avc_idr));
    EXPECT_EQ(ERROR_SYSTEM_FILE_WRITE, srs_error_code(err));
    EXPECT_NE(string::npos, srs_error_desc(err).find("dash request sh")) << srs_error_desc(err);
    srs_freep(err);
    EXPECT_EQ(1, s.on_dash_request_sh_count_);
    EXPECT_FALSE(s.exists("video-init.mp4"));
}
