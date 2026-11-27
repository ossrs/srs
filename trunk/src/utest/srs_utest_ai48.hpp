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

#endif
