//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//
#include <srs_utest_ai42.hpp>

#include <srs_app_srt_server.hpp>
#include <srs_app_st.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_srt.hpp>
#include <srs_utest_ai15.hpp>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <string>
#include <vector>
using namespace std;

#include <srt/srt.h>

extern SrsSrtEventLoop *_srs_srt_eventloop;

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
