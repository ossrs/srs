//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_UTEST_AI48_HPP
#define SRS_UTEST_AI48_HPP

/*
#include <srs_utest_ai48.hpp>
*/
#include <srs_utest.hpp>

#include <srs_core_platform.hpp>
#include <srs_protocol_srt.hpp>

#ifndef _WIN32
#include <pthread.h>
#endif

#include <string>
#include <vector>

#include <srt/srt.h>

// A UDP relay between one SRT client and an SRT listener, both ways, on a thread of its own, so a test
// can lose packets or cut the link while libsrt runs in the test's coroutines. The client sends to
// port(); the relay forwards to the listener from a socket of its own, and the listener's replies back
// to the address the client last sent from. Loss is picked by a random number from the seed, so a
// failure replays with the same seed.
class SrtUtestUdpRelay
{
public:
    // The direction of a packet: up from the client to the listener, down from the listener back.
    enum Direction {
        Up = 0,
        Down = 1,
    };

private:
    std::string target_ip_;
    int target_port_;
    uint32_t seed_;
    int port_;
    srs_socket_t client_fd_;
    srs_socket_t server_fd_;
    // Set by the test, read by the relay thread.
    int loss_[2];
    int blackhole_;
    int forged_group_;
    int stop_;
    // Set by the relay thread, read by the test.
    int forwarded_[2];
    int lost_[2];
    int blackholed_[2];
    bool started_;
#ifdef _WIN32
    HANDLE thread_;
#else
    pthread_t thread_;
#endif

public:
    SrtUtestUdpRelay(std::string target_ip, int target_port, uint32_t seed = 1);
    virtual ~SrtUtestUdpRelay();

public:
    // Bind a free port of the unit tests' range on the target IP for the client, and start the thread.
    srs_error_t start();
    // The port the client sends to.
    int port();
    uint32_t seed();
    // Lose this percent of the packets, 0 to 100, both ways or one way, from now on.
    void set_loss(int percent);
    void set_loss(Direction dir, int percent);
    // Drop every packet both ways, as a network that went down, until restore(); the loss stays as set.
    void blackhole();
    void restore();
    // Send a packet to the listener from the relay's socket, as the client would on its connection,
    // whatever the loss and the blackhole.
    void inject(const char *buf, int size);
    // Write the group ID into the group block of each handshake up to the listener, as a caller that claims a
    // link of that group, such as one that guessed the ID; a listener finds a group by the ID alone.
    void forge_group(SRTSOCKET group);
    // The packets of the direction forwarded, lost by the loss percent, and dropped in a blackhole.
    int forwarded(Direction dir);
    int lost(Direction dir);
    int blackholed(Direction dir);

private:
#ifdef _WIN32
    static DWORD WINAPI win_cycle(LPVOID arg);
#else
    static void *posix_cycle(void *arg);
#endif
    void cycle();
};

// A plain libsrt listener with the options SRS sets on every socket, and a caller connected through the
// relay, with the accepted socket.
class SrtUtestRelayedPair
{
public:
    int port_;
    srs_srt_t lfd_;
    srs_srt_t cfd_;
    srs_srt_t afd_;
    SrsSrtSocket *listener_;
    SrsSrtSocket *client_;
    SrsSrtSocket *accepted_;
    SrtUtestUdpRelay *relay_;

public:
    SrtUtestRelayedPair();
    ~SrtUtestRelayedPair();

public:
    srs_error_t connect();
};

// A socket group caller, bonding, on SRS's libsrt: one connection over several links, each through a relay of
// its own to the listener, so a test can cut one link while the group goes on over the others. It uses libsrt's
// blocking calls, which wait in libsrt's threads, not in a coroutine.
class SrtUtestBondCaller
{
private:
    SRT_GROUP_TYPE type_;
    SRTSOCKET group_;
    std::string passphrase_;
    std::vector<SrtUtestUdpRelay *> relays_;
    std::vector<std::string> ips_;
    std::vector<int> ports_;

public:
    SrtUtestBondCaller(SRT_GROUP_TYPE type);
    virtual ~SrtUtestBondCaller();

public:
    // Encrypt every link with the passphrase, set before connect.
    void set_passphrase(std::string passphrase);
    // Add a link to the listener at ip and port, through a relay of its own.
    srs_error_t add_link(std::string ip, int port);
    // Connect the group over every link, with the stream ID and the latency, and wait until each link has
    // connected or failed. libsrt returns once the first link connects, the others go on in the background.
    srs_error_t connect(std::string streamid, srs_utime_t latency);
    // Add a link to the connected group, as an encoder that brings up another network later, and wait until it
    // has connected or failed.
    srs_error_t join(std::string ip, int port);
    // Close every link but keep the group, as an encoder whose networks went down gives up its links. A link
    // whose network is down tells the listener nothing.
    void drop_links();
    // Dial the group again over a new link to each listener it had, as an encoder whose networks came back, and
    // wait until each has connected or failed. The group, so its ID and stream ID, is the same.
    srs_error_t redial();
    // The links that are connected.
    int connected_links();
    // Send one message to the listener over the group.
    srs_error_t send(const void *buf, int size);
    SrtUtestUdpRelay *relay(int i);
    // The relay of the link that carried the most packets up, which is the active link of a backup group.
    SrtUtestUdpRelay *busiest();
    SRTSOCKET group();
    // Close the group and every link, telling the listener.
    void close();

private:
    // Connect the links from the index on, and wait until none is still connecting.
    srs_error_t connect_links(int from);
};

// The links of a socket group, the caller's or the listener's, as libsrt reports them.
extern std::vector<SRT_SOCKGROUPDATA> srt_utest_group_members(SRTSOCKET group);

#endif
