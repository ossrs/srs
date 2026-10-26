//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//
#include <srs_utest_ai42.hpp>

#include <srs_app_async_call.hpp>
#include <srs_app_circuit_breaker.hpp>
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
#include <srs_utest_ai19.hpp>
#include <srs_utest_ai40.hpp>
#include <srs_utest_manual_mock.hpp>

#include <arpa/inet.h>
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
