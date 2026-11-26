//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_utest_ai49.hpp>

#include <srs_app_srt_listener.hpp>
#include <srs_app_srt_server.hpp>
#include <srs_kernel_error.hpp>
#include <srs_protocol_srt.hpp>
#include <srs_utest_ai37.hpp>
#include <srs_utest_ai48.hpp>
#include <srs_utest_manual_config.hpp>
#include <srs_utest_manual_mock.hpp>

#include <string.h>

#include <string>
#include <vector>
using namespace std;

#include <srt/srt.h>

extern SrsSrtEventLoop *_srs_srt_eventloop;

// The SRT control types, and the subtypes of the user-defined type, which carry the handshake extensions.
enum SrtUtestControl {
    SrtUtestHandshake = 0,
    SrtUtestKeepalive = 1,
    SrtUtestAck = 2,
    SrtUtestNak = 3,
    SrtUtestCongestionWarning = 4,
    SrtUtestShutdown = 5,
    SrtUtestAckAck = 6,
    SrtUtestDropReq = 7,
    SrtUtestPeerError = 8,
    SrtUtestUser = 0x7fff,
};

static void srt_utest_put32(string &b, size_t at, uint32_t v)
{
    b[at] = (char)(v >> 24);
    b[at + 1] = (char)(v >> 16);
    b[at + 2] = (char)(v >> 8);
    b[at + 3] = (char)v;
}

// A control packet as a peer sends it, with body after the 16 bytes of its header.
static string srt_utest_control(int type, int subtype, uint32_t dest, const string &body)
{
    string b(16, 0);
    srt_utest_put32(b, 0, 0x80000000 | ((uint32_t)type << 16) | (uint32_t)subtype);
    srt_utest_put32(b, 12, dest);
    return b + body;
}

// A data packet of one whole message, in order.
static string srt_utest_data(uint32_t seq, uint32_t dest, const string &payload)
{
    string b(16, 0);
    srt_utest_put32(b, 0, seq & 0x7fffffff);
    srt_utest_put32(b, 4, 0xe0000000 | 1);
    srt_utest_put32(b, 12, dest);
    return b + payload;
}

// A caller's first handshake packet, the induction: version 4, the UDT socket type DGRAM, the MTU and flow
// window libsrt sends, and the caller's socket.
static string srt_utest_induction(uint32_t socket_id)
{
    string cif(48, 0);
    srt_utest_put32(cif, 0, 4);
    srt_utest_put32(cif, 4, 2);
    srt_utest_put32(cif, 8, 1);
    srt_utest_put32(cif, 12, 1500);
    srt_utest_put32(cif, 16, 8192);
    srt_utest_put32(cif, 20, 1);
    srt_utest_put32(cif, 24, socket_id);
    return srt_utest_control(SrtUtestHandshake, 0, 0, cif);
}

static string srt_utest_random(uint32_t *random, int size)
{
    string b(size, 0);
    for (int i = 0; i < size; i++) {
        *random ^= *random << 13;
        *random ^= *random >> 17;
        *random ^= *random << 5;
        b[i] = (char)*random;
    }
    return b;
}

// A plain UDP socket, as a peer that writes SRT packets by hand.
class SrtUtestRawPeer
{
public:
    srs_socket_t fd_;

public:
    SrtUtestRawPeer()
    {
        fd_ = SRS_INVALID_SOCKET;
    }
    ~SrtUtestRawPeer()
    {
        if (fd_ != SRS_INVALID_SOCKET) {
            srs_close_socket(fd_);
        }
    }

public:
    srs_error_t connect(int port)
    {
        if ((fd_ = ::socket(AF_INET, SOCK_DGRAM, 0)) == SRS_INVALID_SOCKET) {
            return srs_error_new(ERROR_SOCKET_CREATE, "socket");
        }
        srs_socket_ignore_unreachable(fd_);
        sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
        if (::connect(fd_, (sockaddr *)&addr, sizeof(addr)) != 0) {
            return srs_error_new(ERROR_SOCKET_CONNECT, "connect %d", port);
        }
        return srs_success;
    }

    void send(const string &b)
    {
        ::send(fd_, b.data(), (int)b.size(), 0);
    }

    // The size of the packet the listener answered with within the timeout, or 0 for none.
    int recv(int timeout_ms)
    {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(fd_, &fds);
        timeval tv;
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        if (::select((int)fd_ + 1, &fds, NULL, NULL, &tv) <= 0) {
            return 0;
        }
        char buf[2048];
        int nn = (int)::recv(fd_, buf, sizeof(buf), 0);
        return nn > 0 ? nn : 0;
    }
};

// The server's SRT acceptor on loopback as `srt_server { listen }` makes it, handing each socket libsrt
// accepts to a server that keeps it.
class SrtUtestRobustServer
{
public:
    MockSrsConfig conf_;
    MockSrtClientHandlerForAcceptor server_;
    SrsSrtAcceptor *acceptor_;
    int port_;

public:
    SrtUtestRobustServer()
    {
        acceptor_ = new SrsSrtAcceptor(&server_);
        port_ = 0;
    }
    ~SrtUtestRobustServer()
    {
        srs_freep(acceptor_);
        for (int i = 0; i < (int)server_.fds_.size(); i++) {
            srs_srt_close(server_.fds_[i]);
        }
    }

public:
    srs_error_t listen()
    {
        srs_error_t err = srs_success;

        if ((err = srs_utest_free_port(SOCK_DGRAM, &port_)) != srs_success) {
            return srs_error_wrap(err, "port");
        }
        if ((err = conf_.mock_parse(_MIN_OK_CONF "srt_server { enabled on; listen 10080; }")) != srs_success) {
            return srs_error_wrap(err, "config");
        }
        acceptor_->config_ = &conf_;

        return acceptor_->listen("127.0.0.1", port_);
    }

    void wait_for_callers(int n)
    {
        for (int i = 0; i < 300 && (int)server_.fds_.size() < n; i++) {
            srs_usleep(10 * SRS_UTIME_MILLISECONDS);
        }
    }
};

// A libsrt caller with the options SRS sets on every socket and a stream id.
static srs_error_t srt_utest_robust_call(int port, const string &streamid, SrsSrtSocket **pskt)
{
    srs_error_t err = srs_success;

    srs_srt_t fd = srs_srt_socket_invalid();
    if ((err = srs_srt_socket_with_default_option(&fd)) != srs_success) {
        return srs_error_wrap(err, "socket");
    }
    SrsSrtSocket *skt = new SrsSrtSocket(_srs_srt_eventloop->poller(), fd);
    *pskt = skt;
    skt->set_recv_timeout(3 * SRS_UTIME_SECONDS);
    if ((err = srs_srt_set_streamid(fd, streamid)) != srs_success) {
        return srs_error_wrap(err, "streamid");
    }
    return skt->connect("127.0.0.1", port);
}

// A publish stream id padded by a parameter to size bytes.
static string srt_utest_padded_streamid(int size)
{
    string id = "#!::r=live/livestream,m=publish,pad=";
    return id + string(size - id.size(), 'x');
}

// Garbage and malformed SRT packets reach the server's SRT port before any handshake: random bytes of every
// size up to an Ethernet packet, an induction cut at every length, one over the 1500 bytes libsrt reads,
// data packets, and every control type to the listener. libsrt drops each of them, so the server is handed
// no socket, and a caller after them is accepted and handed as usual.
VOID TEST(SrtRobustTest, MalformedPacketsBeforeHandshake_NothingHandedThenCallerIs)
{
    srs_error_t err;

    SrtUtestRobustServer s;
    HELPER_ASSERT_SUCCESS(s.listen());

    SrtUtestRawPeer peer;
    HELPER_ASSERT_SUCCESS(peer.connect(s.port_));

    uint32_t random = 7;
    for (int i = 0; i < 200; i++) {
        peer.send(srt_utest_random(&random, 1 + (int)(random % 1500)));
    }

    string induction = srt_utest_induction(0x1234);
    for (int n = 1; n < (int)induction.size(); n++) {
        peer.send(induction.substr(0, n));
    }
    peer.send(induction + string(9000 - induction.size(), 0));

    for (uint32_t seq = 0; seq < 20; seq++) {
        peer.send(srt_utest_data(seq, 0, string(1316, 0x47)));
    }

    int types[] = {SrtUtestKeepalive, SrtUtestAck, SrtUtestNak, SrtUtestCongestionWarning, SrtUtestShutdown,
                   SrtUtestAckAck, SrtUtestDropReq, SrtUtestPeerError, SrtUtestUser, 0x1234};
    for (int i = 0; i < (int)(sizeof(types) / sizeof(types[0])); i++) {
        peer.send(srt_utest_control(types[i], 0, 0, ""));
        peer.send(srt_utest_control(types[i], 5, 0, srt_utest_random(&random, 1456)));
    }

    // None of them is answered, as libsrt answers only a whole handshake.
    EXPECT_EQ(0, peer.recv(50));
    srs_usleep(10 * SRS_UTIME_MILLISECONDS);
    EXPECT_EQ(0, (int)s.server_.fds_.size());

    // The whole induction is answered.
    peer.send(induction);
    EXPECT_EQ(64, peer.recv(1000));

    SrsSrtSocket *caller = NULL;
    HELPER_EXPECT_SUCCESS(srt_utest_robust_call(s.port_, "#!::r=live/livestream,m=publish", &caller));
    srs_freep(caller);
    s.wait_for_callers(1);
    EXPECT_EQ(1, (int)s.server_.fds_.size());
}

// Inductions from many ports at once, as a flood of callers: libsrt answers each with a cookie and keeps no
// state for it, so no socket is handed to the server and the next caller connects at once.
VOID TEST(SrtRobustTest, InductionsFromManyPorts_EachAnsweredNothingHanded)
{
    srs_error_t err;

    SrtUtestRobustServer s;
    HELPER_ASSERT_SUCCESS(s.listen());

    const int nn_peers = 100;
    vector<SrtUtestRawPeer *> peers;
    for (int i = 0; i < nn_peers; i++) {
        SrtUtestRawPeer *peer = new SrtUtestRawPeer();
        peers.push_back(peer);
        HELPER_EXPECT_SUCCESS(peer->connect(s.port_));
        peer->send(srt_utest_induction(0x1000 + i));
    }

    int answered = 0;
    for (int i = 0; i < nn_peers; i++) {
        if (peers[i]->recv(1000) >= 64) {
            answered++;
        }
        srs_freep(peers[i]);
    }
    EXPECT_EQ(nn_peers, answered);
    EXPECT_EQ(0, (int)s.server_.fds_.size());

    srs_utime_t start = srs_time_now_realtime();
    SrsSrtSocket *caller = NULL;
    HELPER_EXPECT_SUCCESS(srt_utest_robust_call(s.port_, "#!::r=live/livestream,m=publish", &caller));
    srs_freep(caller);
    s.wait_for_callers(1);
    EXPECT_EQ(1, (int)s.server_.fds_.size());
    EXPECT_LT(srs_time_now_realtime() - start, 500 * SRS_UTIME_MILLISECONDS);
}

// A caller whose stream id is one byte short of the 512 SRT carries, such as one with a long token: the
// server reads all of it.
VOID TEST(SrtRobustTest, StreamIdOf511Bytes_ServerReadsIt)
{
    srs_error_t err;

    SrtUtestRobustServer s;
    HELPER_ASSERT_SUCCESS(s.listen());

    string streamid = srt_utest_padded_streamid(511);
    SrsSrtSocket *caller = NULL;
    HELPER_EXPECT_SUCCESS(srt_utest_robust_call(s.port_, streamid, &caller));
    s.wait_for_callers(1);
    ASSERT_EQ(1, (int)s.server_.fds_.size());

    string got;
    HELPER_EXPECT_SUCCESS(srs_srt_get_streamid(s.server_.fds_[0], got));
    EXPECT_STREQ(streamid.c_str(), got.c_str());
    srs_freep(caller);
}

// F6: a stream id of 512 bytes, the most SRT carries, which libsrt accepts from the caller. The server reads
// it into a buffer of 512 bytes, but libsrt needs one more for the NUL, so the read fails and the server
// closes the caller. Disabled until it is fixed.
VOID TEST(SrtRobustTest, DISABLED_StreamIdOf512Bytes_ServerReadsIt)
{
    srs_error_t err;

    SrtUtestRobustServer s;
    HELPER_ASSERT_SUCCESS(s.listen());

    string streamid = srt_utest_padded_streamid(512);
    SrsSrtSocket *caller = NULL;
    HELPER_EXPECT_SUCCESS(srt_utest_robust_call(s.port_, streamid, &caller));
    s.wait_for_callers(1);
    ASSERT_EQ(1, (int)s.server_.fds_.size());

    string got;
    HELPER_EXPECT_SUCCESS(srs_srt_get_streamid(s.server_.fds_[0], got));
    EXPECT_STREQ(streamid.c_str(), got.c_str());
    srs_freep(caller);
}

// A message of 7 TS packets that starts with its sequence number.
static string srt_utest_robust_message(int seq)
{
    string msg(1316, (char)0x47);
    memcpy((char *)msg.data(), &seq, sizeof(seq));
    return msg;
}

static int srt_utest_robust_seq(const char *buf)
{
    int seq = 0;
    memcpy(&seq, buf, sizeof(seq));
    return seq;
}

// Connect a pair through the relay, and carry a first message from the caller, as an encoder starts with
// its media. libsrt drops a packet that reaches a socket just accepted before its receive queue takes it
// in, so a control packet injected at once may be lost.
static srs_error_t srt_utest_robust_connect(SrtUtestRelayedPair *pair)
{
    srs_error_t err = srs_success;

    if ((err = pair->connect()) != srs_success) {
        return srs_error_wrap(err, "connect");
    }

    string msg = srt_utest_robust_message(-1);
    ssize_t nn = 0;
    if ((err = pair->client_->sendmsg((void *)msg.data(), msg.size(), &nn)) != srs_success) {
        return srs_error_wrap(err, "send");
    }
    char buf[1500];
    if ((err = pair->accepted_->recvmsg(buf, sizeof(buf), &nn)) != srs_success) {
        return srs_error_wrap(err, "recv");
    }

    return err;
}

// The caller of a connection sends control packets of every type that leaves a stream as it was, with no
// body, a short one and a whole packet of random bytes, between its messages, on its own connection: a
// handshake, keepalive, congestion warning, ACKACK, peer error, each extension of the user-defined type but
// the key material, and types nobody defines. The accepted socket reads every message once and in order.
VOID TEST(SrtRobustTest, ControlPacketsOnConnection_MessagesGoOn)
{
    srs_error_t err;

    SrtUtestRelayedPair pair;
    HELPER_ASSERT_SUCCESS(srt_utest_robust_connect(&pair));

    struct {
        int type;
        int subtype;
    } controls[] = {
        {SrtUtestHandshake, 0}, {SrtUtestKeepalive, 0}, {SrtUtestCongestionWarning, 0}, {SrtUtestAckAck, 0},
        {SrtUtestPeerError, 0}, {SrtUtestUser, 1}, {SrtUtestUser, 2}, {SrtUtestUser, 5}, {SrtUtestUser, 6},
        {SrtUtestUser, 7}, {SrtUtestUser, 8}, {SrtUtestUser, 0x7777}, {0x1234, 0},
    };
    int nn_controls = (int)(sizeof(controls) / sizeof(controls[0]));

    uint32_t random = 11;
    ssize_t nn = 0;
    int seq = 0;
    for (int i = 0; i < nn_controls; i++) {
        string bodies[] = {"", string(4, 0), srt_utest_random(&random, 1456)};
        for (int j = 0; j < 3; j++) {
            string pkt = srt_utest_control(controls[i].type, controls[i].subtype, (uint32_t)pair.afd_, bodies[j]);
            pair.relay_->inject(pkt.data(), (int)pkt.size());
            string msg = srt_utest_robust_message(seq++);
            HELPER_ASSERT_SUCCESS(pair.client_->sendmsg((void *)msg.data(), msg.size(), &nn));
        }
    }

    char buf[1500];
    for (int i = 0; i < seq; i++) {
        HELPER_ASSERT_SUCCESS(pair.accepted_->recvmsg(buf, sizeof(buf), &nn));
        EXPECT_EQ(i, srt_utest_robust_seq(buf));
    }
}

// A shutdown from the caller, and an ACK or a NAK of a packet the accepted socket never sent, which libsrt
// takes for an attack or a bug, each end the connection: the accepted socket's read fails at once, not at
// its timeout.
VOID TEST(SrtRobustTest, ShutdownOrAckOrNakOfUnsentPacket_ConnectionEnds)
{
    srs_error_t err;

    int types[] = {SrtUtestShutdown, SrtUtestAck, SrtUtestNak};
    for (int i = 0; i < 3; i++) {
        SrtUtestRelayedPair pair;
        HELPER_ASSERT_SUCCESS(srt_utest_robust_connect(&pair));

        // The accepted socket has sent nothing, so its next packet would have its first sequence number.
        int32_t isn = 0;
        int isn_len = sizeof(isn);
        ASSERT_EQ(0, srt_getsockflag(pair.afd_, SRTO_ISN, &isn, &isn_len));
        uint32_t unsent = ((uint32_t)isn + 1000) & 0x7fffffff;

        // A full ACK of 7 words, a NAK of one sequence number.
        string body(types[i] == SrtUtestAck ? 28 : 4, 0);
        srt_utest_put32(body, 0, unsent);
        string pkt = srt_utest_control(types[i], 0, (uint32_t)pair.afd_, body);
        if (types[i] == SrtUtestAck) {
            srt_utest_put32(pkt, 4, 1);
        }
        pair.relay_->inject(pkt.data(), (int)pkt.size());
        // libsrt breaks the connection on its receive thread; on Linux a read already waiting is not woken by
        // an ACK or a NAK that breaks it, only by its timeout, so the read starts after the break.
        for (int j = 0; j < 100 && srt_getsockstate(pair.afd_) != SRTS_BROKEN; j++) {
            srs_usleep(1 * SRS_UTIME_MILLISECONDS);
        }
        EXPECT_EQ(SRTS_BROKEN, srt_getsockstate(pair.afd_)) << "type " << types[i];

        srs_utime_t start = srs_time_now_realtime();
        char buf[1500];
        ssize_t nn = 0;
        err = pair.accepted_->recvmsg(buf, sizeof(buf), &nn);
        EXPECT_TRUE(err != srs_success) << "type " << types[i];
        EXPECT_NE(ERROR_SRT_TIMEOUT, srs_error_code(err)) << "type " << types[i];
        srs_freep(err);
        EXPECT_LT(srs_time_now_realtime() - start, SRS_UTIME_SECONDS) << "type " << types[i];
    }
}
