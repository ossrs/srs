//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_utest_ai48.hpp>

#include <srs_app_srt_server.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_srt.hpp>

#include <string.h>

#include <string>
#include <vector>
using namespace std;

#include <srt/srt.h>

extern SrsSrtEventLoop *_srs_srt_eventloop;

// The relay thread and the test share the settings and counters, read and written whole.
static int srt_utest_atomic_load(int *v)
{
#ifdef _WIN32
    return InterlockedCompareExchange((volatile LONG *)v, 0, 0);
#else
    return __atomic_load_n(v, __ATOMIC_SEQ_CST);
#endif
}

static void srt_utest_atomic_store(int *v, int value)
{
#ifdef _WIN32
    InterlockedExchange((volatile LONG *)v, value);
#else
    __atomic_store_n(v, value, __ATOMIC_SEQ_CST);
#endif
}

static void srt_utest_atomic_increase(int *v)
{
#ifdef _WIN32
    InterlockedIncrement((volatile LONG *)v);
#else
    __atomic_add_fetch(v, 1, __ATOMIC_SEQ_CST);
#endif
}

// The address of an IPv4 or IPv6 literal and a port.
static srs_error_t srt_utest_relay_addr(const string &ip, int port, sockaddr_storage *addr, socklen_t *len)
{
    memset(addr, 0, sizeof(*addr));

    sockaddr_in *addr4 = (sockaddr_in *)addr;
    sockaddr_in6 *addr6 = (sockaddr_in6 *)addr;
    if (inet_pton(AF_INET, ip.c_str(), &addr4->sin_addr) == 1) {
        addr4->sin_family = AF_INET;
        addr4->sin_port = htons(port);
        *len = sizeof(sockaddr_in);
        return srs_success;
    }
    if (inet_pton(AF_INET6, ip.c_str(), &addr6->sin6_addr) == 1) {
        addr6->sin6_family = AF_INET6;
        addr6->sin6_port = htons(port);
        *len = sizeof(sockaddr_in6);
        return srs_success;
    }

    return srs_error_new(ERROR_SOCKET_BIND, "not an ip %s", ip.c_str());
}

SrtUtestUdpRelay::SrtUtestUdpRelay(string target_ip, int target_port, uint32_t seed)
{
    target_ip_ = target_ip;
    target_port_ = target_port;
    seed_ = seed ? seed : 1;
    port_ = 0;
    client_fd_ = SRS_INVALID_SOCKET;
    server_fd_ = SRS_INVALID_SOCKET;
    for (int i = 0; i < 2; i++) {
        loss_[i] = 0;
        forwarded_[i] = 0;
        lost_[i] = 0;
        blackholed_[i] = 0;
    }
    blackhole_ = 0;
    stop_ = 0;
    started_ = false;
}

SrtUtestUdpRelay::~SrtUtestUdpRelay()
{
    if (started_) {
        srt_utest_atomic_store(&stop_, 1);
#ifdef _WIN32
        WaitForSingleObject(thread_, INFINITE);
        CloseHandle(thread_);
#else
        pthread_join(thread_, NULL);
#endif
    }

    if (client_fd_ != SRS_INVALID_SOCKET) {
        srs_close_socket(client_fd_);
    }
    if (server_fd_ != SRS_INVALID_SOCKET) {
        srs_close_socket(server_fd_);
    }
}

srs_error_t SrtUtestUdpRelay::start()
{
    srs_error_t err = srs_success;

    sockaddr_storage target;
    socklen_t target_len = 0;
    if ((err = srt_utest_relay_addr(target_ip_, target_port_, &target, &target_len)) != srs_success) {
        return srs_error_wrap(err, "target");
    }

    // The client's side, on a random port of the unit tests' range that no socket holds.
    if ((client_fd_ = ::socket(target.ss_family, SOCK_DGRAM, 0)) == SRS_INVALID_SOCKET) {
        return srs_error_new(ERROR_SOCKET_CREATE, "client socket");
    }
    srs_socket_ignore_unreachable(client_fd_);
    for (int i = 0; i < 1024 && !port_; i++) {
        int candidate = srs_utest_random_port();
        sockaddr_storage addr;
        socklen_t addr_len = 0;
        if ((err = srt_utest_relay_addr(target_ip_, candidate, &addr, &addr_len)) != srs_success) {
            return srs_error_wrap(err, "client addr");
        }
        if (srs_bind(client_fd_, (sockaddr *)&addr, addr_len) == 0) {
            port_ = candidate;
        }
    }
    if (!port_) {
        return srs_error_new(ERROR_SOCKET_BIND, "no free udp port on %s", target_ip_.c_str());
    }

    // The listener's side, from a port the system picks.
    if ((server_fd_ = ::socket(target.ss_family, SOCK_DGRAM, 0)) == SRS_INVALID_SOCKET) {
        return srs_error_new(ERROR_SOCKET_CREATE, "server socket");
    }
    srs_socket_ignore_unreachable(server_fd_);
    if (::connect(server_fd_, (sockaddr *)&target, target_len) != 0) {
        return srs_error_new(ERROR_SOCKET_CONNECT, "connect %s:%d", target_ip_.c_str(), target_port_);
    }

#ifdef _WIN32
    thread_ = CreateThread(NULL, 0, win_cycle, this, 0, NULL);
    if (!thread_) {
#else
    if (pthread_create(&thread_, NULL, posix_cycle, this) != 0) {
#endif
        return srs_error_new(ERROR_ST_CREATE_CYCLE_THREAD, "create relay");
    }
    started_ = true;

    return err;
}

int SrtUtestUdpRelay::port()
{
    return port_;
}

uint32_t SrtUtestUdpRelay::seed()
{
    return seed_;
}

void SrtUtestUdpRelay::set_loss(int percent)
{
    set_loss(Up, percent);
    set_loss(Down, percent);
}

void SrtUtestUdpRelay::set_loss(Direction dir, int percent)
{
    srt_utest_atomic_store(&loss_[dir], percent);
}

void SrtUtestUdpRelay::blackhole()
{
    srt_utest_atomic_store(&blackhole_, 1);
}

void SrtUtestUdpRelay::restore()
{
    srt_utest_atomic_store(&blackhole_, 0);
}

void SrtUtestUdpRelay::inject(const char *buf, int size)
{
    ::send(server_fd_, buf, size, 0);
}

int SrtUtestUdpRelay::forwarded(Direction dir)
{
    return srt_utest_atomic_load(&forwarded_[dir]);
}

int SrtUtestUdpRelay::lost(Direction dir)
{
    return srt_utest_atomic_load(&lost_[dir]);
}

int SrtUtestUdpRelay::blackholed(Direction dir)
{
    return srt_utest_atomic_load(&blackholed_[dir]);
}

#ifdef _WIN32
DWORD WINAPI SrtUtestUdpRelay::win_cycle(LPVOID arg)
{
    ((SrtUtestUdpRelay *)arg)->cycle();
    return 0;
}
#else
void *SrtUtestUdpRelay::posix_cycle(void *arg)
{
    ((SrtUtestUdpRelay *)arg)->cycle();
    return NULL;
}
#endif

void SrtUtestUdpRelay::cycle()
{
    // A xorshift generator of the seed, one number for each packet in the order the relay reads them.
    uint32_t random = seed_;

    // The address the client last sent from, where the listener's replies go.
    sockaddr_storage client;
    socklen_t client_len = 0;

    char buf[2048];
    while (!srt_utest_atomic_load(&stop_)) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(client_fd_, &fds);
        FD_SET(server_fd_, &fds);
        timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 10 * 1000;
        int max_fd = (int)(client_fd_ > server_fd_ ? client_fd_ : server_fd_);
        if (::select(max_fd + 1, &fds, NULL, NULL, &tv) <= 0) {
            continue;
        }

        for (int dir = Up; dir <= Down; dir++) {
            srs_socket_t from_fd = (dir == Up) ? client_fd_ : server_fd_;
            if (!FD_ISSET(from_fd, &fds)) {
                continue;
            }

            sockaddr_storage from;
            socklen_t from_len = sizeof(from);
            int nn = (int)::recvfrom(from_fd, buf, sizeof(buf), 0, (sockaddr *)&from, &from_len);
            if (nn <= 0) {
                continue;
            }
            if (dir == Up) {
                client = from;
                client_len = from_len;
            } else if (!client_len) {
                continue;
            }

            random ^= random << 13;
            random ^= random >> 17;
            random ^= random << 5;

            if (srt_utest_atomic_load(&blackhole_)) {
                srt_utest_atomic_increase(&blackholed_[dir]);
                continue;
            }
            if ((int)(random % 100) < srt_utest_atomic_load(&loss_[dir])) {
                srt_utest_atomic_increase(&lost_[dir]);
                continue;
            }

            if (dir == Up) {
                ::send(server_fd_, buf, nn, 0);
            } else {
                ::sendto(client_fd_, buf, nn, 0, (sockaddr *)&client, client_len);
            }
            srt_utest_atomic_increase(&forwarded_[dir]);
        }
    }
}

// A message of the size SRS sends, 7 TS packets, that starts with its sequence number.
static string srt_utest_relay_message(int seq)
{
    string msg(1316, (char)0x47);
    memcpy((char *)msg.data(), &seq, sizeof(seq));
    return msg;
}

static int srt_utest_relay_seq(const char *buf)
{
    int seq = 0;
    memcpy(&seq, buf, sizeof(seq));
    return seq;
}

SrtUtestRelayedPair::SrtUtestRelayedPair()
{
    port_ = 0;
    lfd_ = cfd_ = afd_ = srs_srt_socket_invalid();
    listener_ = client_ = accepted_ = NULL;
    relay_ = NULL;
}

SrtUtestRelayedPair::~SrtUtestRelayedPair()
{
    // The sockets first, so the relay thread is not stopped under a handshake or a shutdown.
    srs_freep(accepted_);
    srs_freep(client_);
    srs_freep(listener_);
    srs_freep(relay_);
}

srs_error_t SrtUtestRelayedPair::connect()
{
    srs_error_t err = srs_success;

    if ((err = srs_utest_free_port(SOCK_DGRAM, &port_)) != srs_success) {
        return srs_error_wrap(err, "free port");
    }

    if ((err = srs_srt_socket_with_default_option(&lfd_)) != srs_success) {
        return srs_error_wrap(err, "listener socket");
    }
    listener_ = new SrsSrtSocket(_srs_srt_eventloop->poller(), lfd_);
    if ((err = srs_srt_listen(lfd_, "127.0.0.1", port_)) != srs_success) {
        return srs_error_wrap(err, "listen");
    }
    listener_->set_recv_timeout(3 * SRS_UTIME_SECONDS);

    relay_ = new SrtUtestUdpRelay("127.0.0.1", port_);
    if ((err = relay_->start()) != srs_success) {
        return srs_error_wrap(err, "relay");
    }

    if ((err = srs_srt_socket_with_default_option(&cfd_)) != srs_success) {
        return srs_error_wrap(err, "client socket");
    }
    client_ = new SrsSrtSocket(_srs_srt_eventloop->poller(), cfd_);
    client_->set_send_timeout(3 * SRS_UTIME_SECONDS);
    client_->set_recv_timeout(3 * SRS_UTIME_SECONDS);
    if ((err = client_->connect("127.0.0.1", relay_->port())) != srs_success) {
        return srs_error_wrap(err, "connect");
    }

    if ((err = listener_->accept(&afd_)) != srs_success) {
        return srs_error_wrap(err, "accept");
    }
    accepted_ = new SrsSrtSocket(_srs_srt_eventloop->poller(), afd_);
    accepted_->set_send_timeout(3 * SRS_UTIME_SECONDS);
    accepted_->set_recv_timeout(3 * SRS_UTIME_SECONDS);

    return err;
}

// A relay with no loss carries the handshake and the messages both ways, and the listener sees the relay
// as its peer, not the caller, so every packet went through it.
VOID TEST(SrtUtestUdpRelayTest, CleanLink_CarriesHandshakeAndMessagesBothWays)
{
    srs_error_t err;

    SrtUtestRelayedPair pair;
    HELPER_ASSERT_SUCCESS(pair.connect());
    SrtUtestUdpRelay *relay = pair.relay_;

    char buf[1500];
    ssize_t nn = 0;

    for (int i = 0; i < 10; i++) {
        string msg = srt_utest_relay_message(i);
        HELPER_ASSERT_SUCCESS(pair.client_->sendmsg((void *)msg.data(), msg.size(), &nn));
    }
    for (int i = 0; i < 10; i++) {
        HELPER_ASSERT_SUCCESS(pair.accepted_->recvmsg(buf, sizeof(buf), &nn));
        EXPECT_EQ(1316, nn);
        EXPECT_EQ(i, srt_utest_relay_seq(buf));
    }

    for (int i = 0; i < 5; i++) {
        string msg = srt_utest_relay_message(100 + i);
        HELPER_ASSERT_SUCCESS(pair.accepted_->sendmsg((void *)msg.data(), msg.size(), &nn));
    }
    for (int i = 0; i < 5; i++) {
        HELPER_ASSERT_SUCCESS(pair.client_->recvmsg(buf, sizeof(buf), &nn));
        EXPECT_EQ(100 + i, srt_utest_relay_seq(buf));
    }

    // The handshake and the data, with the ACKs back each way.
    EXPECT_GE(relay->forwarded(SrtUtestUdpRelay::Up), 2 + 10);
    EXPECT_GE(relay->forwarded(SrtUtestUdpRelay::Down), 2 + 5);
    EXPECT_EQ(0, relay->lost(SrtUtestUdpRelay::Up));
    EXPECT_EQ(0, relay->lost(SrtUtestUdpRelay::Down));
    EXPECT_EQ(0, relay->blackholed(SrtUtestUdpRelay::Up));
    EXPECT_EQ(0, relay->blackholed(SrtUtestUdpRelay::Down));

    sockaddr_storage local;
    int local_len = sizeof(local);
    ASSERT_EQ(0, srt_getsockname(pair.cfd_, (sockaddr *)&local, &local_len));
    int client_port = ntohs(((sockaddr_in *)&local)->sin_port);

    string peer_ip;
    int peer_port = 0;
    HELPER_ASSERT_SUCCESS(srs_srt_get_remote_ip_port(pair.afd_, peer_ip, peer_port));
    EXPECT_STREQ("127.0.0.1", peer_ip.c_str());
    EXPECT_NE(client_port, peer_port);
    EXPECT_NE(relay->port(), peer_port);
}

// Send messages 0 to n-1 and read until all of them are in seqs, at most 3 seconds. A live sender learns
// of a lost packet only from a later one, so the sender sends a filler message, of sequence -1, while the
// receiver waits, as a publisher's next packets would.
static srs_error_t srt_utest_relay_deliver(SrsSrtSocket *sender, SrsSrtSocket *receiver, int n, vector<int> &seqs)
{
    srs_error_t err = srs_success;

    char buf[1500];
    ssize_t nn = 0;

    for (int i = 0; i < n; i++) {
        string msg = srt_utest_relay_message(i);
        if ((err = sender->sendmsg((void *)msg.data(), msg.size(), &nn)) != srs_success) {
            return srs_error_wrap(err, "send %d", i);
        }
    }

    srs_utime_t timeout = receiver->get_recv_timeout();
    receiver->set_recv_timeout(20 * SRS_UTIME_MILLISECONDS);
    srs_utime_t deadline = srs_time_now_realtime() + 3 * SRS_UTIME_SECONDS;
    while ((int)seqs.size() < n && srs_time_now_realtime() < deadline) {
        if ((err = receiver->recvmsg(buf, sizeof(buf), &nn)) != srs_success) {
            if (srs_error_code(err) != ERROR_SRT_TIMEOUT) {
                return srs_error_wrap(err, "recv");
            }
            srs_freep(err);

            string filler = srt_utest_relay_message(-1);
            if ((err = sender->sendmsg((void *)filler.data(), filler.size(), &nn)) != srs_success) {
                return srs_error_wrap(err, "send filler");
            }
            continue;
        }

        int seq = srt_utest_relay_seq(buf);
        if (seq >= 0) {
            seqs.push_back(seq);
        }
    }
    receiver->set_recv_timeout(timeout);

    return err;
}

// At 10% loss each way, libsrt's ARQ recovers every lost data packet, its NAKs and ACKs lost too, so each
// end reads every message the other sent once and in order, and its stats count the loss. SRS's options
// turn TSBPD and the too-late drop off, so nothing is given up.
VOID TEST(SrtUtestUdpRelayTest, LossBothWays_ArqDeliversEveryMessageInOrder)
{
    srs_error_t err;

    SrtUtestRelayedPair pair;
    HELPER_ASSERT_SUCCESS(pair.connect());
    SrtUtestUdpRelay *relay = pair.relay_;
    relay->set_loss(10);

    const int nn_messages = 200;
    vector<int> up;
    HELPER_ASSERT_SUCCESS(srt_utest_relay_deliver(pair.client_, pair.accepted_, nn_messages, up));
    vector<int> down;
    HELPER_ASSERT_SUCCESS(srt_utest_relay_deliver(pair.accepted_, pair.client_, nn_messages, down));

    ASSERT_EQ(nn_messages, (int)up.size()) << "seed=" << relay->seed();
    ASSERT_EQ(nn_messages, (int)down.size()) << "seed=" << relay->seed();
    for (int i = 0; i < nn_messages; i++) {
        EXPECT_EQ(i, up[i]);
        EXPECT_EQ(i, down[i]);
    }

    EXPECT_GT(relay->lost(SrtUtestUdpRelay::Up), 0);
    EXPECT_GT(relay->lost(SrtUtestUdpRelay::Down), 0);

    srs_srt_t receivers[] = {pair.afd_, pair.cfd_};
    for (int i = 0; i < 2; i++) {
        SrsSrtStat s;
        HELPER_ASSERT_SUCCESS(s.fetch(receivers[i], false));
        EXPECT_GT(s.pktRcvLoss(), 0);
        EXPECT_GT(s.pktRcvRetrans(), 0);
        EXPECT_EQ(0, s.pktRcvDrop());
    }
}

// A blackhole drops every packet both ways, so the listener reads nothing; restored before the peer idle
// timeout, the connection lives on and the messages sent into the blackhole arrive by ARQ, in order.
VOID TEST(SrtUtestUdpRelayTest, Blackhole_NothingPassesUntilRestored)
{
    srs_error_t err;

    SrtUtestRelayedPair pair;
    HELPER_ASSERT_SUCCESS(pair.connect());
    SrtUtestUdpRelay *relay = pair.relay_;

    char buf[1500];
    ssize_t nn = 0;

    string msg = srt_utest_relay_message(0);
    HELPER_ASSERT_SUCCESS(pair.client_->sendmsg((void *)msg.data(), msg.size(), &nn));
    HELPER_ASSERT_SUCCESS(pair.accepted_->recvmsg(buf, sizeof(buf), &nn));
    EXPECT_EQ(0, srt_utest_relay_seq(buf));

    relay->blackhole();
    int up = relay->forwarded(SrtUtestUdpRelay::Up);
    int down = relay->forwarded(SrtUtestUdpRelay::Down);

    for (int i = 1; i <= 5; i++) {
        msg = srt_utest_relay_message(i);
        HELPER_ASSERT_SUCCESS(pair.client_->sendmsg((void *)msg.data(), msg.size(), &nn));
    }
    pair.accepted_->set_recv_timeout(200 * SRS_UTIME_MILLISECONDS);
    err = pair.accepted_->recvmsg(buf, sizeof(buf), &nn);
    EXPECT_EQ(ERROR_SRT_TIMEOUT, srs_error_code(err));
    srs_freep(err);

    EXPECT_EQ(up, relay->forwarded(SrtUtestUdpRelay::Up));
    EXPECT_EQ(down, relay->forwarded(SrtUtestUdpRelay::Down));
    EXPECT_GE(relay->blackholed(SrtUtestUdpRelay::Up), 5);
    EXPECT_EQ(0, relay->lost(SrtUtestUdpRelay::Up));

    // The next message shows the listener the gap, and the caller sends the rest again.
    relay->restore();
    msg = srt_utest_relay_message(6);
    HELPER_ASSERT_SUCCESS(pair.client_->sendmsg((void *)msg.data(), msg.size(), &nn));

    pair.accepted_->set_recv_timeout(3 * SRS_UTIME_SECONDS);
    for (int i = 1; i <= 6; i++) {
        HELPER_ASSERT_SUCCESS(pair.accepted_->recvmsg(buf, sizeof(buf), &nn));
        EXPECT_EQ(i, srt_utest_relay_seq(buf));
    }
    EXPECT_GT(relay->forwarded(SrtUtestUdpRelay::Up), up);
}

// Plain UDP packets to a sink through a relay of a seed: the loss percent drops about that share, and a
// relay of the same seed drops the same packets, so a failure replays.
VOID TEST(SrtUtestUdpRelayTest, LossOfSeed_DropsThatShareAndReplays)
{
    srs_error_t err;

    int sink_port = 0;
    HELPER_ASSERT_SUCCESS(srs_utest_free_port(SOCK_DGRAM, &sink_port));
    srs_socket_t sink = ::socket(AF_INET, SOCK_DGRAM, 0);
    ASSERT_NE(SRS_INVALID_SOCKET, sink);
    sockaddr_in sink_addr;
    memset(&sink_addr, 0, sizeof(sink_addr));
    sink_addr.sin_family = AF_INET;
    sink_addr.sin_port = htons(sink_port);
    inet_pton(AF_INET, "127.0.0.1", &sink_addr.sin_addr);
    ASSERT_EQ(0, srs_bind(sink, (sockaddr *)&sink_addr, sizeof(sink_addr)));

    vector<int> lost;
    for (int round = 0; round < 2; round++) {
        SrtUtestUdpRelay relay("127.0.0.1", sink_port, 7);
        HELPER_ASSERT_SUCCESS(relay.start());
        relay.set_loss(SrtUtestUdpRelay::Up, 30);

        srs_socket_t client = ::socket(AF_INET, SOCK_DGRAM, 0);
        ASSERT_NE(SRS_INVALID_SOCKET, client);
        sockaddr_in relay_addr = sink_addr;
        relay_addr.sin_port = htons(relay.port());

        // Paced, so the relay's receive buffer never overflows and every packet gets a random number.
        const int nn_packets = 500;
        char packet[100];
        memset(packet, 0, sizeof(packet));
        for (int i = 0; i < nn_packets; i++) {
            ::sendto(client, packet, sizeof(packet), 0, (sockaddr *)&relay_addr, sizeof(relay_addr));
            if ((i % 50) == 49) {
                srs_usleep(1 * SRS_UTIME_MILLISECONDS);
            }
        }
        for (int i = 0; i < 300; i++) {
            if (relay.forwarded(SrtUtestUdpRelay::Up) + relay.lost(SrtUtestUdpRelay::Up) >= nn_packets) {
                break;
            }
            srs_usleep(10 * SRS_UTIME_MILLISECONDS);
        }
        srs_close_socket(client);

        EXPECT_EQ(nn_packets, relay.forwarded(SrtUtestUdpRelay::Up) + relay.lost(SrtUtestUdpRelay::Up));
        EXPECT_GT(relay.lost(SrtUtestUdpRelay::Up), nn_packets * 20 / 100);
        EXPECT_LT(relay.lost(SrtUtestUdpRelay::Up), nn_packets * 40 / 100);
        EXPECT_EQ(0, relay.lost(SrtUtestUdpRelay::Down));
        lost.push_back(relay.lost(SrtUtestUdpRelay::Up));
    }
    srs_close_socket(sink);

    EXPECT_EQ(lost[0], lost[1]);
}
