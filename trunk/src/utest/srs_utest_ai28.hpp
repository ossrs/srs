//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_UTEST_AI28_HPP
#define SRS_UTEST_AI28_HPP

/*
#include <srs_utest_ai28.hpp>
*/
#include <srs_utest.hpp>

#include <srs_app_factory.hpp>
#include <srs_app_srt_listener.hpp>
#include <srs_protocol_srt.hpp>
#include <srs_utest_manual_mock.hpp>

#include <string>
#include <vector>

// Every SRT option the acceptor reads, set to a value that differs from its default.
class MockAppConfigForSrtAcceptor : public MockAppConfig
{
public:
    int64_t maxbw_;
    int mss_;
    bool tsbpdmode_;
    int latency_;
    int recv_latency_;
    int peer_latency_;
    bool tlpktdrop_;
    srs_utime_t conntimeout_;
    srs_utime_t peeridletimeout_;
    int sendbuf_;
    int recvbuf_;
    int payloadsize_;
    std::string passphrase_;
    int pbkeylen_;

public:
    MockAppConfigForSrtAcceptor();
    virtual ~MockAppConfigForSrtAcceptor();

public:
    virtual int64_t get_srto_maxbw() { return maxbw_; }
    virtual int get_srto_mss() { return mss_; }
    virtual bool get_srto_tsbpdmode() { return tsbpdmode_; }
    virtual int get_srto_latency() { return latency_; }
    virtual int get_srto_recv_latency() { return recv_latency_; }
    virtual int get_srto_peer_latency() { return peer_latency_; }
    virtual bool get_srto_tlpktdrop() { return tlpktdrop_; }
    virtual srs_utime_t get_srto_conntimeout() { return conntimeout_; }
    virtual srs_utime_t get_srto_peeridletimeout() { return peeridletimeout_; }
    virtual int get_srto_sendbuf() { return sendbuf_; }
    virtual int get_srto_recvbuf() { return recvbuf_; }
    virtual int get_srto_payloadsize() { return payloadsize_; }
    virtual std::string get_srto_passphrase() { return passphrase_; }
    virtual int get_srto_pbkeylen() { return pbkeylen_; }
};

// Records create_socket and listen into a call log shared with the options mock, so a test sees their order.
class MockSrtListenerForSrtAcceptor : public ISrsSrtListener
{
public:
    std::vector<std::string> *calls_;
    srs_srt_t fd_;
    srs_error_t create_socket_error_;
    srs_error_t listen_error_;

public:
    MockSrtListenerForSrtAcceptor(std::vector<std::string> *calls);
    virtual ~MockSrtListenerForSrtAcceptor();

public:
    virtual srs_srt_t fd();
    virtual srs_error_t create_socket();
    virtual srs_error_t listen();
};

// Records each option as "name=value" into the shared call log, and fails the option named by fail_option_.
class MockSrtOptionsForSrtAcceptor : public ISrsSrtOptions
{
public:
    std::vector<std::string> *calls_;
    // The fd each option was set on, which must be the listener's fd.
    std::vector<srs_srt_t> fds_;
    std::string fail_option_;

public:
    MockSrtOptionsForSrtAcceptor(std::vector<std::string> *calls);
    virtual ~MockSrtOptionsForSrtAcceptor();

public:
    virtual srs_error_t set_maxbw(srs_srt_t srt_fd, int64_t maxbw);
    virtual srs_error_t set_mss(srs_srt_t srt_fd, int mss);
    virtual srs_error_t set_payload_size(srs_srt_t srt_fd, int payload_size);
    virtual srs_error_t set_connect_timeout(srs_srt_t srt_fd, int timeout);
    virtual srs_error_t set_peer_idle_timeout(srs_srt_t srt_fd, int timeout);
    virtual srs_error_t set_tsbpdmode(srs_srt_t srt_fd, bool tsbpdmode);
    virtual srs_error_t set_sndbuf(srs_srt_t srt_fd, int sndbuf);
    virtual srs_error_t set_rcvbuf(srs_srt_t srt_fd, int rcvbuf);
    virtual srs_error_t set_tlpktdrop(srs_srt_t srt_fd, bool tlpktdrop);
    virtual srs_error_t set_latency(srs_srt_t srt_fd, int latency);
    virtual srs_error_t set_rcv_latency(srs_srt_t srt_fd, int rcv_latency);
    virtual srs_error_t set_peer_latency(srs_srt_t srt_fd, int peer_latency);
    virtual srs_error_t set_passphrase(srs_srt_t srt_fd, const std::string &passphrase);
    virtual srs_error_t set_pbkeylen(srs_srt_t srt_fd, int pbkeylen);

private:
    srs_error_t record(srs_srt_t srt_fd, std::string name, std::string value);
};

// Hands out one mock listener and records the arguments it was created with. The acceptor owns the listener once
// created; the factory frees it only when the acceptor never asked for it.
class MockAppFactoryForSrtAcceptor : public SrsAppFactory
{
public:
    MockSrtListenerForSrtAcceptor *listener_;
    bool listener_created_;
    int create_srt_listener_count_;
    ISrsSrtHandler *handler_;
    std::string ip_;
    int port_;

public:
    MockAppFactoryForSrtAcceptor(MockSrtListenerForSrtAcceptor *listener);
    virtual ~MockAppFactoryForSrtAcceptor();

public:
    virtual ISrsSrtListener *create_srt_listener(ISrsSrtHandler *handler, std::string ip, int port);
};

#endif
