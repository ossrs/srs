//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_UTEST_AI34_HPP
#define SRS_UTEST_AI34_HPP

/*
#include <srs_utest_ai34.hpp>
*/
#include <srs_utest.hpp>

#include <srs_app_factory.hpp>
#include <srs_app_forward.hpp>
#include <srs_app_st.hpp>
#include <srs_kernel_packet.hpp>
#include <srs_protocol_rtmp_conn.hpp>
#include <srs_utest_manual_mock.hpp>

#include <string>
#include <vector>

// One packet the forwarder sent to the destination, kept after the packet is freed.
struct MockForwarderSentPacket {
    SrsFrameType type_;
    int64_t timestamp_;
    std::string payload_;
};

// Mock ISrsBasicRtmpClient for SrsForwarder: records every packet sent, in order, and each
// batch size of send_and_free_messages().
class MockRtmpClientForForwarder : public ISrsBasicRtmpClient
{
public:
    std::string url_;
    int connect_count_;
    int publish_count_;
    int close_count_;
    int recv_count_;
    int send_count_;
    // The 1-based send call that fails with send_error_; 0 means every send fails with it.
    int fail_send_at_;
    srs_utime_t recv_timeout_;
    srs_error_t connect_error_;
    srs_error_t publish_error_;
    srs_error_t send_error_;
    srs_error_t recv_error_;
    std::vector<MockForwarderSentPacket> sent_;
    std::vector<int> batches_;

public:
    MockRtmpClientForForwarder();
    virtual ~MockRtmpClientForForwarder();

public:
    virtual srs_error_t connect();
    virtual void close();
    virtual srs_error_t publish(int chunk_size, bool with_vhost = true, std::string *pstream = NULL);
    virtual srs_error_t play(int chunk_size, bool with_vhost = true, std::string *pstream = NULL);
    virtual void kbps_sample(const char *label, srs_utime_t age);
    virtual void kbps_sample(const char *label, srs_utime_t age, int msgs);
    virtual int sid();
    virtual srs_error_t recv_message(SrsRtmpCommonMessage **pmsg);
    virtual srs_error_t decode_message(SrsRtmpCommonMessage *msg, SrsRtmpCommand **ppacket);
    virtual srs_error_t send_and_free_messages(SrsMediaPacket **msgs, int nb_msgs);
    virtual srs_error_t send_and_free_message(SrsMediaPacket *msg);
    virtual void set_recv_timeout(srs_utime_t timeout);

private:
    void record(SrsMediaPacket *msg);
    srs_error_t send_result();
};

// Mock ISrsCoroutine for SrsForwarder: pull() succeeds nb_pulls_ times, then fails, so a test
// runs a fixed number of forward() iterations.
class MockCoroutineForForwarder : public ISrsCoroutine
{
public:
    int nb_pulls_;
    int pull_count_;
    int stop_count_;
    SrsContextId cid_;

public:
    MockCoroutineForForwarder(int nb_pulls);
    virtual ~MockCoroutineForForwarder();

public:
    virtual srs_error_t start();
    virtual void stop();
    virtual void interrupt();
    virtual srs_error_t pull();
    virtual const SrsContextId &cid();
    virtual void set_cid(const SrsContextId &cid);
};

// Mock ISrsAppFactory for SrsForwarder::do_cycle(): hands out a recording RTMP client and keeps
// the url and timeouts of every create_rtmp_client() call.
class MockAppFactoryForForwarderCycle : public SrsAppFactory
{
public:
    // The client for the next create_rtmp_client(), so a test can set its errors first; a new
    // one is made when it is NULL. The forwarder owns every client it gets.
    MockRtmpClientForForwarder *next_;
    std::vector<MockRtmpClientForForwarder *> clients_;
    std::vector<std::string> urls_;
    srs_utime_t cto_;
    srs_utime_t sto_;

public:
    MockAppFactoryForForwarderCycle();
    virtual ~MockAppFactoryForForwarderCycle();

public:
    virtual ISrsBasicRtmpClient *create_rtmp_client(std::string url, srs_utime_t cto, srs_utime_t sto);
};

// Mock ISrsOriginHub whose on_forwarder_start() feeds its cached metadata, video and audio
// sequence headers into the forwarder, in the same order as SrsOriginHub.
class MockOriginHubForForwarderStart : public MockOriginHub
{
public:
    SrsMediaPacket *metadata_;
    SrsMediaPacket *vsh_;
    SrsMediaPacket *ash_;

public:
    MockOriginHubForForwarderStart();
    virtual ~MockOriginHubForForwarderStart();

public:
    virtual srs_error_t on_forwarder_start(SrsForwarder *forwarder);
};

#endif
