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
#include <srs_protocol_http_client.hpp>
#include <srs_protocol_rtmp_conn.hpp>
#include <srs_utest_manual_mock.hpp>

#include <string>
#include <vector>

class SrsOriginHub;
class MockLiveSourceForOriginHub;
class MockAppConfig;

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
    // A send that carries a packet with this marker fails with send_error_; 0 means none.
    char fail_marker_;
    // The packets the publisher sends while this client is connected: the Nth recv_message()
    // feeds live_[N-1] to live_forwarder_, or through live_hub_ when it is set, then frees them.
    SrsForwarder *live_forwarder_;
    SrsOriginHub *live_hub_;
    std::vector<std::vector<SrsMediaPacket *> > live_;
    int live_errors_;

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
    bool record(SrsMediaPacket *msg);
    srs_error_t send_result(bool marked);
    void feed_live();
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

// Mock config for the forward parts of SrsOriginHub: the test sets whether forward is on and the
// queue length; the destinations and backend come from MockAppConfig.
class MockAppConfigForForwardHub : public MockAppConfig
{
public:
    bool forward_enabled_;
    srs_utime_t queue_length_;

public:
    MockAppConfigForForwardHub();
    virtual ~MockAppConfigForForwardHub();

public:
    virtual bool get_forward_enabled(std::string vhost);
    virtual srs_utime_t get_queue_length(std::string vhost);
};

// Mock ISrsHttpHooks for SrsOriginHub::create_backend_forwarders(): returns urls_ or error_, and
// keeps the backend url and request of each call.
class MockHttpHooksForForwardHub : public MockHttpHooks
{
public:
    int count_;
    std::string url_;
    ISrsRequest *req_;
    std::vector<std::string> urls_;
    srs_error_t error_;

public:
    MockHttpHooksForForwardHub();
    virtual ~MockHttpHooksForForwardHub();

public:
    virtual srs_error_t on_forward_backend(std::string url, ISrsRequest *req, std::vector<std::string> &rtmp_urls);
};

// Mock ISrsForwarder for the SrsOriginHub fan-out: appends "<name>:<event>" to a log shared by
// every forwarder, where the event is the packet marker, "unpublish" or "freed".
class MockForwarderForFanOut : public ISrsForwarder
{
public:
    std::string name_;
    std::vector<std::string> *log_;
    std::vector<SrsMediaPacket *> packets_;
    srs_error_t meta_error_;
    srs_error_t audio_error_;
    srs_error_t video_error_;

public:
    MockForwarderForFanOut(std::string name, std::vector<std::string> *log);
    virtual ~MockForwarderForFanOut();

public:
    virtual srs_error_t initialize(ISrsRequest *r, std::string ep);
    virtual void set_queue_size(srs_utime_t queue_size);
    virtual srs_error_t on_publish();
    virtual void on_unpublish();
    virtual srs_error_t on_meta_data(SrsMediaPacket *shared_metadata);
    virtual srs_error_t on_audio(SrsMediaPacket *shared_audio);
    virtual srs_error_t on_video(SrsMediaPacket *shared_video);

private:
    srs_error_t record(SrsMediaPacket *pkt, srs_error_t err);
};

// What SrsHttpHooks::on_forward_backend() sent to the backend and what the backend answers. It
// outlives the HTTP client, which the hook frees before it returns.
struct MockForwardBackendExchange {
    int create_count_;
    int post_count_;
    std::string schema_;
    std::string host_;
    int port_;
    std::string path_;
    std::string body_;
    // The answer of the backend: the HTTP status and body, or a post error with no response.
    int status_code_;
    std::string response_;
    srs_error_t post_error_;

    MockForwardBackendExchange();
    ~MockForwardBackendExchange();
};

// Mock ISrsHttpClient for SrsHttpHooks::on_forward_backend(): records the request into the shared
// exchange and answers with its status and body.
class MockHttpClientForForwardBackend : public ISrsHttpClient
{
public:
    MockForwardBackendExchange *exchange_;

public:
    MockHttpClientForForwardBackend(MockForwardBackendExchange *exchange);
    virtual ~MockHttpClientForForwardBackend();

public:
    virtual srs_error_t initialize(std::string schema, std::string h, int p, srs_utime_t tm);
    virtual srs_error_t get(std::string path, std::string req, ISrsHttpMessage **ppmsg);
    virtual srs_error_t post(std::string path, std::string req, ISrsHttpMessage **ppmsg);
    virtual void set_recv_timeout(srs_utime_t tm);
    virtual void kbps_sample(const char *label, srs_utime_t age);
};

// Mock ISrsAppFactory for SrsHttpHooks: each create_http_client() makes a new recording client on
// the same exchange.
class MockAppFactoryForForwardBackend : public SrsAppFactory
{
public:
    MockForwardBackendExchange exchange_;

public:
    MockAppFactoryForForwardBackend();
    virtual ~MockAppFactoryForForwardBackend();

public:
    virtual ISrsHttpClient *create_http_client();
};

// Forward test packets: FLV tags whose last byte is a marker to tell them apart.
extern SrsMediaPacket *utest_forward_video_sh(int64_t ts, char marker);
extern SrsMediaPacket *utest_forward_video_keyframe(int64_t ts, char marker);
extern SrsMediaPacket *utest_forward_video_inter(int64_t ts, char marker);
extern SrsMediaPacket *utest_forward_audio_sh(int64_t ts, char marker);
extern SrsMediaPacket *utest_forward_audio_raw(int64_t ts, char marker);
extern SrsMediaPacket *utest_forward_metadata(int64_t ts, char marker);
extern char utest_forward_marker(SrsMediaPacket *pkt);
extern char utest_forward_marker(const MockForwarderSentPacket &sent);

// The non-NULL packets, in order, as one batch of MockRtmpClientForForwarder::live_.
extern std::vector<SrsMediaPacket *> utest_forward_batch(SrsMediaPacket *p0, SrsMediaPacket *p1 = NULL, SrsMediaPacket *p2 = NULL,
                                                         SrsMediaPacket *p3 = NULL, SrsMediaPacket *p4 = NULL);

// The markers of the sent packets, in order, as a string.
extern std::string utest_forward_markers(const std::vector<MockForwarderSentPacket> &sent);

// Wire a forwarder to the mock factory and config, with a coroutine that allows nb_pulls pulls.
extern MockCoroutineForForwarder *utest_forward_wire(SrsForwarder *forwarder, MockAppFactoryForForwarderCycle *factory,
                                                     MockAppConfig *config, int nb_pulls);

// An origin hub whose HLS, DASH, DVR, encoder, exec and HDS are mocks, so only its forward parts do real work.
extern SrsOriginHub *utest_forward_hub(MockLiveSourceForOriginHub *source, ISrsRequest *req, ISrsAppConfig *config, ISrsHttpHooks *hooks);

#endif
