//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//
#include <srs_utest_ai38.hpp>

#include <srs_app_dash.hpp>
#include <srs_app_edge.hpp>
#include <srs_app_encoder.hpp>
#include <srs_app_ffmpeg.hpp>
#include <srs_app_fragment.hpp>
#include <srs_app_hls.hpp>
#include <srs_app_ingest.hpp>
#include <srs_app_mpegts_udp.hpp>
#include <srs_app_ng_exec.hpp>
#include <srs_app_process.hpp>
#include <srs_app_rtmp_source.hpp>
#include <srs_kernel_balance.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_ts.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_amf0.hpp>
#include <srs_protocol_rtmp_stack.hpp>
#include <srs_utest_ai13.hpp>
#include <srs_utest_ai14.hpp>
#include <srs_utest_ai17.hpp>
#include <srs_utest_ai18.hpp>
#include <srs_utest_ai34.hpp>
#include <srs_utest_manual_coworkers.hpp>

using namespace std;

MockAppConfigForEdgeIngester::MockAppConfigForEdgeIngester()
{
    edge_protocol_ = "rtmp";
    follow_client_ = false;
    origin_ = NULL;
}

MockAppConfigForEdgeIngester::~MockAppConfigForEdgeIngester()
{
    srs_freep(origin_);
}

std::string MockAppConfigForEdgeIngester::get_vhost_edge_protocol(std::string vhost)
{
    return edge_protocol_;
}

bool MockAppConfigForEdgeIngester::get_vhost_edge_follow_client(std::string vhost)
{
    return follow_client_;
}

SrsConfDirective *MockAppConfigForEdgeIngester::get_vhost_edge_origin(std::string vhost)
{
    return origin_;
}

MockPlayEdgeForIngesterCycle::MockPlayEdgeForIngesterCycle()
{
    on_ingest_play_count_ = 0;
    on_ingest_play_error_ = srs_success;
}

MockPlayEdgeForIngesterCycle::~MockPlayEdgeForIngesterCycle()
{
    srs_freep(on_ingest_play_error_);
}

srs_error_t MockPlayEdgeForIngesterCycle::on_ingest_play()
{
    on_ingest_play_count_++;
    return srs_error_copy(on_ingest_play_error_);
}

MockEdgeUpstreamForIngesterCycle::MockEdgeUpstreamForIngesterCycle()
{
    factory_ = NULL;
    connect_error_ = srs_success;
    recv_error_ = srs_success;
}

MockEdgeUpstreamForIngesterCycle::~MockEdgeUpstreamForIngesterCycle()
{
    srs_freep(connect_error_);
    srs_freep(recv_error_);
    for (int i = 0; i < (int)msgs_.size(); i++) {
        srs_freep(msgs_[i]);
    }
    for (int i = 0; i < (int)packets_.size(); i++) {
        srs_freep(packets_[i]);
    }
}

srs_error_t MockEdgeUpstreamForIngesterCycle::connect(ISrsRequest *r, ISrsLbRoundRobin *lb)
{
    factory_->connect_count_++;
    return srs_error_copy(connect_error_);
}

srs_error_t MockEdgeUpstreamForIngesterCycle::recv_message(SrsRtmpCommonMessage **pmsg)
{
    if (msgs_.empty()) {
        if (recv_error_ != srs_success) {
            return srs_error_copy(recv_error_);
        }
        return srs_error_new(ERROR_SOCKET_READ, "mock origin closed");
    }

    *pmsg = msgs_.front();
    msgs_.erase(msgs_.begin());
    return srs_success;
}

srs_error_t MockEdgeUpstreamForIngesterCycle::decode_message(SrsRtmpCommonMessage *msg, SrsRtmpCommand **ppacket)
{
    if (packets_.empty()) {
        return srs_error_new(ERROR_RTMP_AMF0_DECODE, "mock decode");
    }

    *ppacket = packets_.front();
    packets_.erase(packets_.begin());
    return srs_success;
}

void MockEdgeUpstreamForIngesterCycle::close()
{
    if (factory_) {
        factory_->close_count_++;
    }
}

void MockEdgeUpstreamForIngesterCycle::selected(std::string &server, int &port)
{
    server = "127.0.0.1";
    port = 1935;
}

void MockEdgeUpstreamForIngesterCycle::set_recv_timeout(srs_utime_t tm)
{
    factory_->recv_timeout_ = tm;
}

void MockEdgeUpstreamForIngesterCycle::kbps_sample(const char *label, srs_utime_t age)
{
}

MockHttpMessageForEdgeRedirect::MockHttpMessageForEdgeRedirect(int status, std::string location)
{
    status_code_ = status;
    if (!location.empty()) {
        header_.set("Location", location);
    }
}

MockHttpMessageForEdgeRedirect::~MockHttpMessageForEdgeRedirect()
{
}

SrsHttpHeader *MockHttpMessageForEdgeRedirect::header()
{
    return &header_;
}

MockHttpClientForEdgeRedirect::MockHttpClientForEdgeRedirect(MockAppFactoryForEdgeIngester *factory)
{
    factory_ = factory;
}

MockHttpClientForEdgeRedirect::~MockHttpClientForEdgeRedirect()
{
}

srs_error_t MockHttpClientForEdgeRedirect::initialize(std::string schema, std::string h, int p, srs_utime_t tm)
{
    factory_->http_schemas_.push_back(schema);
    factory_->http_hosts_.push_back(h);
    factory_->http_ports_.push_back(p);
    return srs_success;
}

srs_error_t MockHttpClientForEdgeRedirect::get(std::string path, std::string req, ISrsHttpMessage **ppmsg)
{
    factory_->http_paths_.push_back(path);
    *ppmsg = new MockHttpMessageForEdgeRedirect(302, factory_->location_);
    return srs_success;
}

srs_error_t MockHttpClientForEdgeRedirect::post(std::string path, std::string req, ISrsHttpMessage **ppmsg)
{
    return srs_success;
}

void MockHttpClientForEdgeRedirect::set_recv_timeout(srs_utime_t tm)
{
}

void MockHttpClientForEdgeRedirect::kbps_sample(const char *label, srs_utime_t age)
{
}

MockAppFactoryForEdgeIngester::MockAppFactoryForEdgeIngester()
{
    coroutine_ = NULL;
    create_coroutine_count_ = 0;
    connect_count_ = 0;
    close_count_ = 0;
    recv_timeout_ = 0;
}

MockAppFactoryForEdgeIngester::~MockAppFactoryForEdgeIngester()
{
    for (int i = 0; i < (int)upstreams_.size(); i++) {
        srs_freep(upstreams_[i]);
    }
    srs_freep(coroutine_);
}

ISrsCoroutine *MockAppFactoryForEdgeIngester::create_coroutine(const std::string &name, ISrsCoroutineHandler *handler, SrsContextId cid)
{
    create_coroutine_count_++;
    coroutine_name_ = name;
    coroutine_cid_ = cid;

    ISrsCoroutine *trd = coroutine_;
    coroutine_ = NULL;
    return trd ? trd : new MockCoroutineForForwarder(0);
}

ISrsEdgeUpstream *MockAppFactoryForEdgeIngester::create_edge_flv_upstream(std::string schema)
{
    flv_schemas_.push_back(schema);
    return next_upstream();
}

ISrsEdgeUpstream *MockAppFactoryForEdgeIngester::create_edge_rtmp_upstream(std::string redirect)
{
    rtmp_redirects_.push_back(redirect);
    return next_upstream();
}

MockEdgeUpstreamForIngesterCycle *MockAppFactoryForEdgeIngester::next_upstream()
{
    MockEdgeUpstreamForIngesterCycle *upstream = NULL;
    if (upstreams_.empty()) {
        upstream = new MockEdgeUpstreamForIngesterCycle();
    } else {
        upstream = upstreams_.front();
        upstreams_.erase(upstreams_.begin());
    }
    upstream->factory_ = this;
    return upstream;
}

ISrsHttpClient *MockAppFactoryForEdgeIngester::create_http_client()
{
    return new MockHttpClientForEdgeRedirect(this);
}

// A message of the type, with a payload of size bytes of the value.
static SrsRtmpCommonMessage *mock_edge_message(int type, int size, char value)
{
    SrsRtmpCommonMessage *msg = new SrsRtmpCommonMessage();
    msg->header_.message_type_ = type;
    msg->header_.payload_length_ = size;
    msg->create_payload(size);
    memset(msg->payload(), value, size);
    return msg;
}

// The call an origin sends for an RTMP 302: an error status, with the next URL in ex.
static SrsCallPacket *mock_edge_redirect_call(std::string level, std::string key, std::string url)
{
    SrsCallPacket *call = new SrsCallPacket();
    SrsAmf0Object *args = SrsAmf0Any::object();
    args->set("level", SrsAmf0Any::str(level.c_str()));
    SrsAmf0Object *ex = SrsAmf0Any::object();
    if (!key.empty()) {
        ex->set(key, SrsAmf0Any::str(url.c_str()));
    }
    args->set("ex", ex);
    call->arguments_ = args;
    return call;
}

// The ingester, wired to the mocks of a test.
class MockEdgeIngesterFixture
{
public:
    MockLiveSourceForOriginHub source_;
    MockPlayEdgeForIngesterCycle edge_;
    MockAppConfigForEdgeIngester config_;
    MockAppFactoryForEdgeIngester factory_;
    MockSrsRequest req_;
    SrsEdgeIngester *ingester_;

public:
    MockEdgeIngesterFixture(int nb_pulls) : req_("test.vhost", "live", "livestream")
    {
        ingester_ = new SrsEdgeIngester();
        ingester_->source_ = &source_;
        ingester_->edge_ = &edge_;
        ingester_->req_ = &req_;
        ingester_->config_ = &config_;
        ingester_->app_factory_ = &factory_;
        srs_freep(ingester_->trd_);
        ingester_->trd_ = new MockCoroutineForForwarder(nb_pulls);
    }
    virtual ~MockEdgeIngesterFixture()
    {
        srs_freep(ingester_);
    }
    MockCoroutineForForwarder *trd()
    {
        return dynamic_cast<MockCoroutineForForwarder *>(ingester_->trd_);
    }
};

// Start publishes the source, then pulls in a new coroutine, which takes its own cid.
VOID TEST(EdgeIngesterCycleTest, StartPublishesSourceAndStartsCoroutine)
{
    srs_error_t err;

    MockEdgeIngesterFixture f(0);
    MockCoroutineForForwarder *trd = new MockCoroutineForForwarder(0);
    f.factory_.coroutine_ = trd;

    HELPER_EXPECT_SUCCESS(f.ingester_->start());
    EXPECT_EQ(1, f.source_.on_publish_count_);
    EXPECT_EQ(1, f.factory_.create_coroutine_count_);
    EXPECT_EQ("edge-igs", f.factory_.coroutine_name_);
    EXPECT_TRUE(f.factory_.coroutine_cid_.empty());
    EXPECT_TRUE(f.ingester_->trd_ == trd);
    EXPECT_EQ(1, trd->start_count_);

    // Stop stops the coroutine, closes the upstream and unpublishes the source.
    f.ingester_->stop();
    EXPECT_EQ(1, trd->stop_count_);
    EXPECT_EQ(1, f.source_.on_unpublish_count_);
}

// A source that refuses the publish starts no coroutine.
VOID TEST(EdgeIngesterCycleTest, StartFailsWhenSourceRefusesPublish)
{
    MockEdgeIngesterFixture f(0);
    f.source_.on_publish_error_ = srs_error_new(ERROR_SYSTEM_STREAM_BUSY, "mock busy");

    srs_error_t err = f.ingester_->start();
    EXPECT_EQ(ERROR_SYSTEM_STREAM_BUSY, srs_error_code(err));
    srs_freep(err);
    EXPECT_EQ(0, f.factory_.create_coroutine_count_);
}

VOID TEST(EdgeIngesterCycleTest, StartFailsWhenCoroutineFails)
{
    MockEdgeIngesterFixture f(0);
    MockCoroutineForForwarder *trd = new MockCoroutineForForwarder(0);
    trd->start_error_ = srs_error_new(ERROR_ST_CREATE_CYCLE_THREAD, "mock start");
    f.factory_.coroutine_ = trd;

    srs_error_t err = f.ingester_->start();
    EXPECT_EQ(ERROR_ST_CREATE_CYCLE_THREAD, srs_error_code(err));
    srs_freep(err);
    EXPECT_EQ(1, f.source_.on_publish_count_);
}

// The cycle quits as soon as the coroutine is stopped, before or after a pull from the origin.
VOID TEST(EdgeIngesterCycleTest, CycleQuitsWhenCoroutineStops)
{
    if (true) {
        MockEdgeIngesterFixture f(0);
        srs_error_t err = f.ingester_->cycle();
        EXPECT_EQ(ERROR_THREAD_DUMMY, srs_error_code(err));
        srs_freep(err);
        EXPECT_EQ(1, f.trd()->pull_count_);
        EXPECT_EQ(0, f.factory_.connect_count_);
    }

    // The pull fails at its first check, which the cycle ignores, then the cycle sees the stop.
    if (true) {
        MockEdgeIngesterFixture f(1);
        srs_error_t err = f.ingester_->cycle();
        EXPECT_EQ(ERROR_THREAD_DUMMY, srs_error_code(err));
        srs_freep(err);
        EXPECT_EQ(3, f.trd()->pull_count_);
        EXPECT_EQ(0, f.factory_.connect_count_);
    }
}

// An RTMP pull connects, tells the edge, then feeds the source every message of the origin, until
// the origin closes, which is a retry, not an error.
VOID TEST(EdgeIngesterCycleTest, DoCycleIngestsFromRtmpOrigin)
{
    srs_error_t err;

    MockEdgeIngesterFixture f(100);
    MockEdgeUpstreamForIngesterCycle *upstream = new MockEdgeUpstreamForIngesterCycle();
    upstream->msgs_.push_back(mock_edge_message(RTMP_MSG_AudioMessage, 4, (char)0xaf));
    upstream->msgs_.push_back(mock_edge_message(RTMP_MSG_VideoMessage, 4, 0x17));
    upstream->msgs_.push_back(mock_edge_message(RTMP_MSG_AMF0DataMessage, 4, 0x02));
    upstream->packets_.push_back(new SrsOnMetaDataPacket());
    upstream->msgs_.push_back(mock_edge_message(RTMP_MSG_AMF0DataMessage, 4, 0x02));
    upstream->packets_.push_back(new SrsCallPacket());
    f.factory_.upstreams_.push_back(upstream);

    HELPER_EXPECT_SUCCESS(f.ingester_->do_cycle());

    ASSERT_EQ(1, (int)f.factory_.rtmp_redirects_.size());
    EXPECT_EQ("", f.factory_.rtmp_redirects_.at(0));
    EXPECT_EQ(0, (int)f.factory_.flv_schemas_.size());
    EXPECT_EQ(1, f.source_.on_source_id_changed_count_);
    EXPECT_EQ(1, f.factory_.connect_count_);
    EXPECT_EQ(1, f.edge_.on_ingest_play_count_);
    EXPECT_EQ(5 * SRS_UTIME_SECONDS, f.factory_.recv_timeout_);
    EXPECT_EQ(1, f.source_.on_audio_count_);
    EXPECT_EQ(1, f.source_.on_video_count_);
    EXPECT_EQ(1, f.source_.on_meta_data_count_);
}

// The edge pulls HTTP-FLV when the vhost says flv or flvs, or when it follows a client that plays so.
VOID TEST(EdgeIngesterCycleTest, DoCycleChoosesFlvUpstream)
{
    const char *protocols[] = {"flv", "flvs"};
    const char *schemas[] = {"http", "https"};
    for (int i = 0; i < 2; i++) {
        MockEdgeIngesterFixture f(100);
        f.config_.edge_protocol_ = protocols[i];
        MockEdgeUpstreamForIngesterCycle *upstream = new MockEdgeUpstreamForIngesterCycle();
        upstream->connect_error_ = srs_error_new(ERROR_SOCKET_CONNECT, "mock connect");
        f.factory_.upstreams_.push_back(upstream);

        srs_error_t err = f.ingester_->do_cycle();
        EXPECT_EQ(ERROR_SOCKET_CONNECT, srs_error_code(err)) << i;
        srs_freep(err);
        ASSERT_EQ(1, (int)f.factory_.flv_schemas_.size()) << i;
        EXPECT_EQ(schemas[i], f.factory_.flv_schemas_.at(0)) << i;
        EXPECT_EQ(0, f.edge_.on_ingest_play_count_) << i;
    }

    if (true) {
        MockEdgeIngesterFixture f(100);
        f.config_.follow_client_ = true;
        f.req_.protocol_ = "flvs";
        MockEdgeUpstreamForIngesterCycle *upstream = new MockEdgeUpstreamForIngesterCycle();
        upstream->connect_error_ = srs_error_new(ERROR_SOCKET_CONNECT, "mock connect");
        f.factory_.upstreams_.push_back(upstream);

        srs_error_t err = f.ingester_->do_cycle();
        EXPECT_EQ(ERROR_SOCKET_CONNECT, srs_error_code(err));
        srs_freep(err);
        ASSERT_EQ(1, (int)f.factory_.flv_schemas_.size());
        EXPECT_EQ("https", f.factory_.flv_schemas_.at(0));
    }

    // A client of no protocol keeps the protocol of the vhost.
    if (true) {
        MockEdgeIngesterFixture f(100);
        f.config_.follow_client_ = true;
        MockEdgeUpstreamForIngesterCycle *upstream = new MockEdgeUpstreamForIngesterCycle();
        upstream->connect_error_ = srs_error_new(ERROR_SOCKET_CONNECT, "mock connect");
        f.factory_.upstreams_.push_back(upstream);

        srs_error_t err = f.ingester_->do_cycle();
        EXPECT_EQ(ERROR_SOCKET_CONNECT, srs_error_code(err));
        srs_freep(err);
        EXPECT_EQ(0, (int)f.factory_.flv_schemas_.size());
        EXPECT_EQ(1, (int)f.factory_.rtmp_redirects_.size());
    }
}

// An RTMP 302 of the origin connects again at once, to the URL of the redirect.
VOID TEST(EdgeIngesterCycleTest, DoCycleFollowsRtmpRedirect)
{
    MockEdgeIngesterFixture f(100);

    MockEdgeUpstreamForIngesterCycle *first = new MockEdgeUpstreamForIngesterCycle();
    first->msgs_.push_back(mock_edge_message(RTMP_MSG_AMF0CommandMessage, 4, 0x02));
    first->packets_.push_back(mock_edge_redirect_call(StatusLevelError, "redirect2", "rtmp://10.0.0.2/live/livestream"));
    f.factory_.upstreams_.push_back(first);

    MockEdgeUpstreamForIngesterCycle *second = new MockEdgeUpstreamForIngesterCycle();
    second->connect_error_ = srs_error_new(ERROR_SOCKET_CONNECT, "mock connect");
    f.factory_.upstreams_.push_back(second);

    srs_error_t err = f.ingester_->do_cycle();
    EXPECT_EQ(ERROR_SOCKET_CONNECT, srs_error_code(err));
    srs_freep(err);

    ASSERT_EQ(2, (int)f.factory_.rtmp_redirects_.size());
    EXPECT_EQ("", f.factory_.rtmp_redirects_.at(0));
    EXPECT_EQ("rtmp://10.0.0.2/live/livestream", f.factory_.rtmp_redirects_.at(1));
    EXPECT_EQ(2, f.factory_.connect_count_);
    EXPECT_EQ(1, f.edge_.on_ingest_play_count_);
}

// A failure that is not the origin closing ends the pull with the error, for the cycle to retry.
VOID TEST(EdgeIngesterCycleTest, DoCycleReturnsIngestError)
{
    MockEdgeIngesterFixture f(100);
    f.source_.on_audio_error_ = srs_error_new(ERROR_RTMP_MESSAGE_DECODE, "mock audio");
    MockEdgeUpstreamForIngesterCycle *upstream = new MockEdgeUpstreamForIngesterCycle();
    upstream->msgs_.push_back(mock_edge_message(RTMP_MSG_AudioMessage, 4, (char)0xaf));
    f.factory_.upstreams_.push_back(upstream);

    srs_error_t err = f.ingester_->do_cycle();
    EXPECT_EQ(ERROR_RTMP_MESSAGE_DECODE, srs_error_code(err));
    srs_freep(err);
    EXPECT_EQ(1, f.source_.on_audio_count_);
}

VOID TEST(EdgeIngesterCycleTest, DoCycleFailsBeforeIngest)
{
    // The coroutine is stopped.
    if (true) {
        MockEdgeIngesterFixture f(0);
        srs_error_t err = f.ingester_->do_cycle();
        EXPECT_EQ(ERROR_THREAD_DUMMY, srs_error_code(err));
        srs_freep(err);
        EXPECT_EQ(0, f.factory_.connect_count_);
    }

    // The source fails to take the new source id.
    if (true) {
        MockEdgeIngesterFixture f(100);
        f.source_.on_source_id_changed_error_ = srs_error_new(ERROR_SYSTEM_STREAM_BUSY, "mock id");
        srs_error_t err = f.ingester_->do_cycle();
        EXPECT_EQ(ERROR_SYSTEM_STREAM_BUSY, srs_error_code(err));
        srs_freep(err);
        EXPECT_EQ(0, f.factory_.connect_count_);
    }

    // The edge fails to start playing.
    if (true) {
        MockEdgeIngesterFixture f(100);
        f.edge_.on_ingest_play_error_ = srs_error_new(ERROR_RTMP_EDGE_PLAY_STATE, "mock play");
        srs_error_t err = f.ingester_->do_cycle();
        EXPECT_EQ(ERROR_RTMP_EDGE_PLAY_STATE, srs_error_code(err));
        srs_freep(err);
        EXPECT_EQ(1, f.factory_.connect_count_);
        EXPECT_EQ(0, f.factory_.recv_timeout_);
    }

    // The coroutine stops while ingesting.
    if (true) {
        MockEdgeIngesterFixture f(1);
        srs_error_t err = f.ingester_->do_cycle();
        EXPECT_EQ(ERROR_THREAD_DUMMY, srs_error_code(err));
        srs_freep(err);
        EXPECT_EQ(1, f.edge_.on_ingest_play_count_);
    }
}

// Only an error status with a redirect in ex is an RTMP 302; any other call is ignored.
VOID TEST(EdgeIngesterCycleTest, ProcessCallIgnoresAllButRedirect)
{
    srs_error_t err;

    MockEdgeIngesterFixture f(100);
    MockEdgeUpstreamForIngesterCycle *upstream = new MockEdgeUpstreamForIngesterCycle();
    upstream->factory_ = &f.factory_;
    srs_freep(f.ingester_->upstream_);
    f.ingester_->upstream_ = upstream;

    SrsUniquePtr<SrsRtmpCommonMessage> msg(mock_edge_message(RTMP_MSG_AMF0CommandMessage, 4, 0x02));

    // Arguments that are no object.
    SrsCallPacket *call = new SrsCallPacket();
    call->arguments_ = SrsAmf0Any::str("redirect");
    upstream->packets_.push_back(call);
    // No level.
    call = new SrsCallPacket();
    call->arguments_ = SrsAmf0Any::object();
    upstream->packets_.push_back(call);
    // A level of no error.
    upstream->packets_.push_back(mock_edge_redirect_call("status", "redirect2", "rtmp://10.0.0.2/live/livestream"));
    // No ex.
    call = new SrsCallPacket();
    SrsAmf0Object *args = SrsAmf0Any::object();
    args->set("level", SrsAmf0Any::str(StatusLevelError));
    call->arguments_ = args;
    upstream->packets_.push_back(call);
    // An ex of no object.
    call = new SrsCallPacket();
    args = SrsAmf0Any::object();
    args->set("level", SrsAmf0Any::str(StatusLevelError));
    args->set("ex", SrsAmf0Any::str("rtmp://10.0.0.2/live/livestream"));
    call->arguments_ = args;
    upstream->packets_.push_back(call);
    // An ex without a redirect.
    upstream->packets_.push_back(mock_edge_redirect_call(StatusLevelError, "", ""));
    // A command of no call.
    upstream->packets_.push_back(new SrsOnMetaDataPacket());

    for (int i = 0; i < 7; i++) {
        std::string redirect;
        HELPER_EXPECT_SUCCESS(f.ingester_->process_publish_message(msg.get(), redirect));
        EXPECT_TRUE(redirect.empty()) << i;
    }
    EXPECT_TRUE(upstream->packets_.empty());

    // The redirect of the early SRS3 releases, a tcUrl.
    upstream->packets_.push_back(mock_edge_redirect_call(StatusLevelError, "redirect", "rtmp://10.0.0.2/live"));
    std::string redirect;
    err = f.ingester_->process_publish_message(msg.get(), redirect);
    EXPECT_EQ(ERROR_CONTROL_REDIRECT, srs_error_code(err));
    srs_freep(err);
    EXPECT_EQ("rtmp://10.0.0.2/live", redirect);

    // A command that fails to decode.
    err = f.ingester_->process_publish_message(msg.get(), redirect);
    EXPECT_EQ(ERROR_RTMP_AMF0_DECODE, srs_error_code(err));
    srs_freep(err);
}

VOID TEST(EdgeIngesterCycleTest, ProcessDataMessage)
{
    srs_error_t err;

    MockEdgeIngesterFixture f(100);
    MockEdgeUpstreamForIngesterCycle *upstream = new MockEdgeUpstreamForIngesterCycle();
    upstream->factory_ = &f.factory_;
    srs_freep(f.ingester_->upstream_);
    f.ingester_->upstream_ = upstream;

    SrsUniquePtr<SrsRtmpCommonMessage> msg(mock_edge_message(RTMP_MSG_AMF0DataMessage, 4, 0x02));
    std::string redirect;

    // Data that fails to decode.
    err = f.ingester_->process_publish_message(msg.get(), redirect);
    EXPECT_EQ(ERROR_RTMP_AMF0_DECODE, srs_error_code(err));
    srs_freep(err);
    EXPECT_EQ(0, f.source_.on_meta_data_count_);

    // Metadata the source refuses.
    f.source_.on_meta_data_error_ = srs_error_new(ERROR_RTMP_MESSAGE_DECODE, "mock metadata");
    upstream->packets_.push_back(new SrsOnMetaDataPacket());
    err = f.ingester_->process_publish_message(msg.get(), redirect);
    EXPECT_EQ(ERROR_RTMP_MESSAGE_DECODE, srs_error_code(err));
    srs_freep(err);
    EXPECT_EQ(1, f.source_.on_meta_data_count_);
}

// The edge forwarder sends what the publisher queued, and quits once the coroutine stops.
VOID TEST(EdgeForwarderCycleTest, DoCycleSendsQueuedMessages)
{
    srs_error_t err;

    SrsUniquePtr<SrsEdgeForwarder> forwarder(new SrsEdgeForwarder());
    MockRtmpClientForForwarder *sdk = new MockRtmpClientForForwarder();
    forwarder->sdk_ = sdk;
    srs_freep(forwarder->trd_);
    // Two loops: one sends the queue, one finds it empty.
    MockCoroutineForForwarder *trd = new MockCoroutineForForwarder(2);
    forwarder->trd_ = trd;

    for (int i = 0; i < 2; i++) {
        SrsUniquePtr<SrsRtmpCommonMessage> msg(mock_edge_message(RTMP_MSG_VideoMessage, 8, (char)(0x17 + i)));
        msg->header_.timestamp_ = 40 * i;
        HELPER_EXPECT_SUCCESS(forwarder->proxy(msg.get()));
    }

    err = forwarder->do_cycle();
    EXPECT_EQ(ERROR_THREAD_DUMMY, srs_error_code(err));
    srs_freep(err);

    EXPECT_EQ(SRS_CONSTS_RTMP_PULSE, sdk->recv_timeout_);
    EXPECT_EQ(2, sdk->recv_count_);
    ASSERT_EQ(1, (int)sdk->batches_.size());
    EXPECT_EQ(2, sdk->batches_.at(0));
    ASSERT_EQ(2, (int)sdk->sent_.size());
    EXPECT_EQ(0, sdk->sent_.at(0).timestamp_);
    EXPECT_EQ(40, sdk->sent_.at(1).timestamp_);
    EXPECT_EQ(0, forwarder->send_error_code_);
}

// An origin that fails the connection stops the push: the forwarder keeps the error for the next
// proxy to refuse the publisher, and sends nothing more.
VOID TEST(EdgeForwarderCycleTest, DoCycleKeepsOriginError)
{
    srs_error_t err;

    SrsUniquePtr<SrsEdgeForwarder> forwarder(new SrsEdgeForwarder());
    MockRtmpClientForForwarder *sdk = new MockRtmpClientForForwarder();
    sdk->recv_error_ = srs_error_new(ERROR_SOCKET_READ, "mock origin closed");
    forwarder->sdk_ = sdk;
    srs_freep(forwarder->trd_);
    forwarder->trd_ = new MockCoroutineForForwarder(2);

    SrsUniquePtr<SrsRtmpCommonMessage> msg(mock_edge_message(RTMP_MSG_VideoMessage, 8, 0x17));
    HELPER_EXPECT_SUCCESS(forwarder->proxy(msg.get()));

    err = forwarder->do_cycle();
    EXPECT_EQ(ERROR_THREAD_DUMMY, srs_error_code(err));
    srs_freep(err);

    EXPECT_EQ(ERROR_SOCKET_READ, forwarder->send_error_code_);
    EXPECT_EQ(1, sdk->recv_count_);
    EXPECT_EQ(0, sdk->send_count_);

    err = forwarder->proxy(msg.get());
    EXPECT_EQ(ERROR_SOCKET_READ, srs_error_code(err));
    srs_freep(err);
}

// A send that fails ends the cycle, which keeps the error for the next proxy.
VOID TEST(EdgeForwarderCycleTest, CycleKeepsSendError)
{
    srs_error_t err;

    SrsUniquePtr<SrsEdgeForwarder> forwarder(new SrsEdgeForwarder());
    MockRtmpClientForForwarder *sdk = new MockRtmpClientForForwarder();
    sdk->send_error_ = srs_error_new(ERROR_SOCKET_WRITE, "mock send");
    forwarder->sdk_ = sdk;
    srs_freep(forwarder->trd_);
    forwarder->trd_ = new MockCoroutineForForwarder(100);

    SrsUniquePtr<SrsRtmpCommonMessage> msg(mock_edge_message(RTMP_MSG_VideoMessage, 8, 0x17));
    HELPER_EXPECT_SUCCESS(forwarder->proxy(msg.get()));

    err = forwarder->cycle();
    EXPECT_EQ(ERROR_SOCKET_WRITE, srs_error_code(err));
    srs_freep(err);
    EXPECT_EQ(ERROR_SOCKET_WRITE, forwarder->send_error_code_);
    EXPECT_EQ(1, sdk->send_count_);

    // A stopped coroutine ends the cycle at once.
    srs_freep(forwarder->trd_);
    MockCoroutineForForwarder *trd = new MockCoroutineForForwarder(0);
    forwarder->trd_ = trd;
    err = forwarder->cycle();
    EXPECT_EQ(ERROR_THREAD_DUMMY, srs_error_code(err));
    srs_freep(err);
    EXPECT_EQ(1, trd->pull_count_);
    EXPECT_EQ(1, sdk->send_count_);
}

// An HTTP-FLV origin may redirect the edge to another origin, at most three times in a row.
VOID TEST(EdgeFlvUpstreamTest, ConnectFollowsHttpRedirectThreeTimes)
{
    MockAppConfigForEdgeIngester config;
    config.origin_ = new SrsConfDirective();
    config.origin_->name_ = "origin";
    config.origin_->args_.push_back("127.0.0.1:8080");
    MockAppFactoryForEdgeIngester factory;
    factory.location_ = "http://10.0.0.2:8081/live2/stream2.flv";

    SrsUniquePtr<SrsEdgeFlvUpstream> upstream(new SrsEdgeFlvUpstream("http"));
    upstream->config_ = &config;
    upstream->app_factory_ = &factory;

    MockSrsRequest req("test.vhost", "live", "livestream");
    SrsLbRoundRobin lb;
    srs_error_t err = upstream->connect(&req, &lb);
    EXPECT_EQ(ERROR_HTTP_302_INVALID, srs_error_code(err));
    srs_freep(err);

    ASSERT_EQ(4, (int)factory.http_hosts_.size());
    EXPECT_EQ("http", factory.http_schemas_.at(0));
    EXPECT_EQ("127.0.0.1", factory.http_hosts_.at(0));
    EXPECT_EQ(8080, factory.http_ports_.at(0));
    EXPECT_EQ("/live/livestream.flv", factory.http_paths_.at(0));
    for (int i = 1; i < 4; i++) {
        EXPECT_EQ("http", factory.http_schemas_.at(i)) << i;
        EXPECT_EQ("10.0.0.2", factory.http_hosts_.at(i)) << i;
        EXPECT_EQ(8081, factory.http_ports_.at(i)) << i;
        EXPECT_EQ("/live2/stream2.flv", factory.http_paths_.at(i)) << i;
    }

    // The upstream connected to the last origin it was redirected to.
    std::string server;
    int port = 0;
    upstream->selected(server, port);
    EXPECT_EQ("10.0.0.2", server);
    EXPECT_EQ(8081, port);

    // The request of the client is left as is.
    EXPECT_EQ("live", req.app_);
    EXPECT_EQ("livestream", req.stream_);

    upstream->config_ = NULL;
    upstream->app_factory_ = NULL;
}

// The HTTP-FLV body is a stream, so the file reader is always open, at its start, and seeks nowhere.
VOID TEST(EdgeHttpFileReaderTest, StreamHasNoFilePosition)
{
    srs_error_t err;

    MockHttpResponseReaderForHttpFileReader http;
    SrsUniquePtr<SrsHttpFileReader> reader(new SrsHttpFileReader(&http));

    HELPER_EXPECT_SUCCESS(reader->open("any.flv"));
    EXPECT_TRUE(reader->is_open());
    EXPECT_EQ(0, reader->tellg());
    reader->skip(100);
    EXPECT_EQ(0, reader->tellg());
    EXPECT_EQ(100, reader->seek2(100));
    EXPECT_EQ(0, reader->filesize());
    reader->close();
    EXPECT_TRUE(reader->is_open());
}

MockProcessForFFMPEG::MockProcessForFFMPEG()
{
    started_ = false;
    initialize_count_ = 0;
    start_count_ = 0;
    cycle_count_ = 0;
    stop_count_ = 0;
    fast_stop_count_ = 0;
    fast_kill_count_ = 0;
    initialize_error_ = srs_success;
    start_error_ = srs_success;
    cycle_error_ = srs_success;
}

MockProcessForFFMPEG::~MockProcessForFFMPEG()
{
    srs_freep(initialize_error_);
    srs_freep(start_error_);
    srs_freep(cycle_error_);
}

int MockProcessForFFMPEG::get_pid()
{
    return started_ ? 100 : -1;
}

bool MockProcessForFFMPEG::started()
{
    return started_;
}

srs_error_t MockProcessForFFMPEG::initialize(std::string binary, std::vector<std::string> argv)
{
    initialize_count_++;
    binary_ = binary;
    argv_ = argv;
    return srs_error_copy(initialize_error_);
}

srs_error_t MockProcessForFFMPEG::start()
{
    start_count_++;
    if (start_error_ != srs_success) {
        return srs_error_copy(start_error_);
    }
    started_ = true;
    return srs_success;
}

srs_error_t MockProcessForFFMPEG::cycle()
{
    cycle_count_++;
    return srs_error_copy(cycle_error_);
}

void MockProcessForFFMPEG::stop()
{
    stop_count_++;
    started_ = false;
}

void MockProcessForFFMPEG::fast_stop()
{
    fast_stop_count_++;
}

void MockProcessForFFMPEG::fast_kill()
{
    fast_kill_count_++;
}

// The FFmpeg with a mock process, freed with it.
static SrsFFMPEG *mock_ffmpeg_with_process(MockProcessForFFMPEG **pprocess)
{
    SrsFFMPEG *ffmpeg = new SrsFFMPEG("/usr/local/bin/ffmpeg");
    srs_freep(ffmpeg->process_);
    *pprocess = new MockProcessForFFMPEG();
    ffmpeg->process_ = *pprocess;
    return ffmpeg;
}

static std::string mock_join(std::vector<std::string> args)
{
    return srs_strings_join(args, " ");
}

// A transcode passes every option of the engine to FFmpeg, in the order FFmpeg reads them, skips the
// empty ones, and writes both outputs of FFmpeg to the log file.
VOID TEST(FFMPEGStartTest, StartBuildsTranscodeArguments)
{
    srs_error_t err;

    MockProcessForFFMPEG *process = NULL;
    SrsUniquePtr<SrsFFMPEG> ffmpeg(mock_ffmpeg_with_process(&process));
    HELPER_EXPECT_SUCCESS(ffmpeg->initialize("rtmp://127.0.0.1/live/livestream", "rtmp://127.0.0.1/live/livestream_hd", "/var/log/ffmpeg.log"));

    ffmpeg->append_iparam("-loglevel");
    ffmpeg->append_iparam("info");
    ffmpeg->append_iparam("");
    ffmpeg->perfile_.push_back("-re");
    ffmpeg->perfile_.push_back("");
    ffmpeg->iformat_ = "flv";
    ffmpeg->vfilter_.push_back("-vf");
    ffmpeg->vfilter_.push_back("scale=1280:720");
    ffmpeg->vfilter_.push_back("");
    ffmpeg->vcodec_ = "libx264";
    ffmpeg->vbitrate_ = 800;
    ffmpeg->vfps_ = 25;
    ffmpeg->vwidth_ = 1280;
    ffmpeg->vheight_ = 720;
    ffmpeg->vthreads_ = 4;
    ffmpeg->vprofile_ = "main";
    ffmpeg->vpreset_ = "medium";
    ffmpeg->vparams_.push_back("-g");
    ffmpeg->vparams_.push_back("50");
    ffmpeg->vparams_.push_back("");
    ffmpeg->acodec_ = "libfdk_aac";
    ffmpeg->abitrate_ = 64;
    ffmpeg->asample_rate_ = 44100;
    ffmpeg->achannels_ = 2;
    ffmpeg->aparams_.push_back("-profile:a");
    ffmpeg->aparams_.push_back("aac_he");
    ffmpeg->aparams_.push_back("");
    ffmpeg->set_oformat("flv");

    HELPER_EXPECT_SUCCESS(ffmpeg->start());

    EXPECT_EQ(1, process->initialize_count_);
    EXPECT_EQ(1, process->start_count_);
    EXPECT_EQ("/usr/local/bin/ffmpeg", process->binary_);
    EXPECT_EQ("/usr/local/bin/ffmpeg -loglevel info -re -f flv -i rtmp://127.0.0.1/live/livestream"
              " -vf scale=1280:720 -vcodec libx264 -b:v 800000 -r 25.00 -s 1280x720 -aspect 1280:720"
              " -threads 4 -profile:v main -preset medium -g 50"
              " -acodec libfdk_aac -b:a 64000 -ar 44100 -ac 2 -profile:a aac_he"
              " -f flv -y rtmp://127.0.0.1/live/livestream_hd"
              " 1 > /var/log/ffmpeg.log 2 > /var/log/ffmpeg.log",
              mock_join(process->argv_));

    // A started FFmpeg starts no other process.
    HELPER_EXPECT_SUCCESS(ffmpeg->start());
    EXPECT_EQ(1, process->initialize_count_);
    EXPECT_EQ(1, process->start_count_);
}

// A copy passes no encoder option, and of the audio params only the bitstream filter that turns
// ADTS into the AAC of FLV, the one a copy needs.
VOID TEST(FFMPEGStartTest, StartBuildsCopyArguments)
{
    srs_error_t err;

    MockProcessForFFMPEG *process = NULL;
    SrsUniquePtr<SrsFFMPEG> ffmpeg(mock_ffmpeg_with_process(&process));
    HELPER_EXPECT_SUCCESS(ffmpeg->initialize("udp://0.0.0.0:2222", "rtmp://127.0.0.1/live/copy", ""));
    HELPER_EXPECT_SUCCESS(ffmpeg->initialize_copy());

    ffmpeg->iformat_ = "off";
    ffmpeg->vbitrate_ = 800;
    ffmpeg->vprofile_ = "main";
    ffmpeg->abitrate_ = 64;
    ffmpeg->aparams_.push_back("-bsf:a");
    ffmpeg->aparams_.push_back("aac_adtstoasc");
    ffmpeg->aparams_.push_back("-bsf:a");
    ffmpeg->aparams_.push_back("other");
    ffmpeg->aparams_.push_back("-ar");
    ffmpeg->aparams_.push_back("-bsf:a");
    ffmpeg->set_oformat("off");

    HELPER_EXPECT_SUCCESS(ffmpeg->start());
    EXPECT_EQ("/usr/local/bin/ffmpeg -i udp://0.0.0.0:2222 -vcodec copy -acodec copy -bsf:a aac_adtstoasc"
              " -y rtmp://127.0.0.1/live/copy",
              mock_join(process->argv_));
}

// An engine without video or without audio tells FFmpeg to drop it.
VOID TEST(FFMPEGStartTest, StartDropsDisabledStreams)
{
    srs_error_t err;

    MockProcessForFFMPEG *process = NULL;
    SrsUniquePtr<SrsFFMPEG> ffmpeg(mock_ffmpeg_with_process(&process));
    HELPER_EXPECT_SUCCESS(ffmpeg->initialize("rtmp://127.0.0.1/live/livestream", "rtmp://127.0.0.1/live/audio", ""));
    ffmpeg->vcodec_ = "vn";
    ffmpeg->vbitrate_ = 800;
    ffmpeg->acodec_ = "aac";

    HELPER_EXPECT_SUCCESS(ffmpeg->start());
    EXPECT_EQ("/usr/local/bin/ffmpeg -i rtmp://127.0.0.1/live/livestream -vn -acodec aac -y rtmp://127.0.0.1/live/audio",
              mock_join(process->argv_));

    MockProcessForFFMPEG *process2 = NULL;
    SrsUniquePtr<SrsFFMPEG> ffmpeg2(mock_ffmpeg_with_process(&process2));
    HELPER_EXPECT_SUCCESS(ffmpeg2->initialize("rtmp://127.0.0.1/live/livestream", "rtmp://127.0.0.1/live/video", ""));
    ffmpeg2->vcodec_ = "copy";
    ffmpeg2->acodec_ = "an";
    ffmpeg2->abitrate_ = 64;

    HELPER_EXPECT_SUCCESS(ffmpeg2->start());
    EXPECT_EQ("/usr/local/bin/ffmpeg -i rtmp://127.0.0.1/live/livestream -vcodec copy -an -y rtmp://127.0.0.1/live/video",
              mock_join(process2->argv_));
}

VOID TEST(FFMPEGStartTest, StartFailsWhenProcessFails)
{
    srs_error_t err;

    MockProcessForFFMPEG *process = NULL;
    SrsUniquePtr<SrsFFMPEG> ffmpeg(mock_ffmpeg_with_process(&process));
    HELPER_EXPECT_SUCCESS(ffmpeg->initialize("rtmp://127.0.0.1/live/livestream", "rtmp://127.0.0.1/live/copy", ""));
    HELPER_EXPECT_SUCCESS(ffmpeg->initialize_copy());

    process->initialize_error_ = srs_error_new(ERROR_ENCODER_FORK, "mock init");
    err = ffmpeg->start();
    EXPECT_EQ(ERROR_ENCODER_FORK, srs_error_code(err));
    srs_freep(err);
    EXPECT_EQ(0, process->start_count_);

    srs_freep(process->initialize_error_);
    process->start_error_ = srs_error_new(ERROR_ENCODER_FORK, "mock fork");
    err = ffmpeg->start();
    EXPECT_EQ(ERROR_ENCODER_FORK, srs_error_code(err));
    srs_freep(err);
    EXPECT_EQ(1, process->start_count_);
    EXPECT_FALSE(process->started_);
}

// The FFmpeg runs as its process: it checks, stops and kills that process.
VOID TEST(FFMPEGStartTest, ControlsGoToProcess)
{
    srs_error_t err;

    MockProcessForFFMPEG *process = NULL;
    SrsUniquePtr<SrsFFMPEG> ffmpeg(mock_ffmpeg_with_process(&process));

    HELPER_EXPECT_SUCCESS(ffmpeg->cycle());
    EXPECT_EQ(1, process->cycle_count_);

    process->cycle_error_ = srs_error_new(ERROR_SYSTEM_WAITPID, "mock waitpid");
    err = ffmpeg->cycle();
    EXPECT_EQ(ERROR_SYSTEM_WAITPID, srs_error_code(err));
    srs_freep(err);

    ffmpeg->fast_stop();
    EXPECT_EQ(1, process->fast_stop_count_);
    ffmpeg->fast_kill();
    EXPECT_EQ(1, process->fast_kill_count_);
    ffmpeg->stop();
    EXPECT_EQ(1, process->stop_count_);
}

// An engine directive with the options of the args, each a "name value" pair.
static SrsConfDirective *mock_engine(const char *args[], int nb_args)
{
    SrsConfDirective *engine = new SrsConfDirective();
    engine->name_ = "engine";
    for (int i = 0; i + 1 < nb_args; i += 2) {
        SrsConfDirective *d = new SrsConfDirective();
        d->name_ = args[i];
        d->args_.push_back(args[i + 1]);
        engine->directives_.push_back(d);
    }
    return engine;
}

// A transcode engine with an invalid option is refused, naming the option.
VOID TEST(FFMPEGStartTest, InitializeTranscodeRefusesInvalidOptions)
{
    struct {
        const char *args[8];
        int nb_args;
        int code;
    } cases[] = {
        {{"vcodec", "vn", "acodec", "an"}, 4, ERROR_ENCODER_VCODEC},
        {{"vcodec", "libx264", "vbitrate", "-1"}, 4, ERROR_ENCODER_VBITRATE},
        {{"vcodec", "libx264", "vfps", "-1"}, 4, ERROR_ENCODER_VFPS},
        {{"vcodec", "libx264", "vwidth", "-2"}, 4, ERROR_ENCODER_VWIDTH},
        {{"vcodec", "libx264", "vheight", "-2"}, 4, ERROR_ENCODER_VHEIGHT},
        {{"vcodec", "libx264", "vthreads", "-1"}, 4, ERROR_ENCODER_VTHREADS},
        {{"vcodec", "libx264"}, 2, ERROR_ENCODER_VPROFILE},
        {{"vcodec", "libx264", "vprofile", "main"}, 4, ERROR_ENCODER_VPRESET},
        {{"vcodec", "copy", "acodec", "libaacplus", "abitrate", "15"}, 6, ERROR_ENCODER_ABITRATE},
        {{"vcodec", "copy", "acodec", "libaacplus", "abitrate", "73"}, 6, ERROR_ENCODER_ABITRATE},
    };

    MockAppConfigForIngester config;
    for (int i = 0; i < (int)(sizeof(cases) / sizeof(cases[0])); i++) {
        SrsUniquePtr<SrsConfDirective> engine(mock_engine(cases[i].args, cases[i].nb_args));
        SrsUniquePtr<SrsFFMPEG> ffmpeg(new SrsFFMPEG("/usr/local/bin/ffmpeg"));
        ffmpeg->config_ = &config;
        srs_error_t err = ffmpeg->initialize("rtmp://127.0.0.1/live/livestream", "rtmp://127.0.0.1/live/out", "");
        HELPER_EXPECT_SUCCESS(err);

        err = ffmpeg->initialize_transcode(engine.get());
        EXPECT_EQ(cases[i].code, srs_error_code(err)) << i;
        srs_freep(err);
        ffmpeg->config_ = NULL;
    }
}

// The libaacplus encoder takes a bitrate in [16, 72], or none.
VOID TEST(FFMPEGStartTest, InitializeTranscodeAcceptsAacplusBitrate)
{
    srs_error_t err;

    const char *bitrates[] = {"0", "16", "72"};
    MockAppConfigForIngester config;
    for (int i = 0; i < 3; i++) {
        const char *args[] = {"vcodec", "copy", "acodec", "libaacplus", "abitrate", bitrates[i]};
        SrsUniquePtr<SrsConfDirective> engine(mock_engine(args, 6));
        SrsUniquePtr<SrsFFMPEG> ffmpeg(new SrsFFMPEG("/usr/local/bin/ffmpeg"));
        ffmpeg->config_ = &config;
        HELPER_EXPECT_SUCCESS(ffmpeg->initialize("rtmp://127.0.0.1/live/livestream", "rtmp://127.0.0.1/live/out", ""));
        HELPER_EXPECT_SUCCESS(ffmpeg->initialize_transcode(engine.get()));
        ffmpeg->config_ = NULL;
    }
}

// The encoder with the mocks of a test, with one engine of output live/livestream_hd.
class MockEncoderFixture
{
public:
    SrsConfDirective engine_;
    MockAppConfigForEncoder config_;
    MockAppFactoryForEncoder factory_;
    std::vector<std::string> transcoded_urls_;
    MockSrsRequest req_;
    SrsEncoder *encoder_;

public:
    MockEncoderFixture() : req_("test.vhost", "live", "livestream")
    {
        engine_.name_ = "engine";
        engine_.args_.push_back("hd");
        config_.transcode_directive_ = &engine_;
        config_.transcode_engines_.push_back(&engine_);
        config_.engine_output_ = "rtmp://127.0.0.1:[port]/[app]/[stream]_[engine]?vhost=[vhost]";
        req_.port_ = 1935;

        encoder_ = new SrsEncoder();
        encoder_->config_ = &config_;
        encoder_->app_factory_ = &factory_;
        encoder_->transcoded_urls_ = &transcoded_urls_;
    }
    virtual ~MockEncoderFixture()
    {
        srs_freep(encoder_);
    }
};

// A publish of a stream with transcode engines runs them in a new coroutine.
VOID TEST(EncoderCycleTest, OnPublishStartsCoroutineForEngines)
{
    srs_error_t err;

    MockEncoderFixture f;
    MockFFMPEGForEncoder *ffmpeg = new MockFFMPEGForEncoder();
    f.factory_.mock_ffmpeg_ = ffmpeg;
    MockCoroutineForForwarder *trd = new MockCoroutineForForwarder(0);
    f.factory_.coroutine_ = trd;

    HELPER_EXPECT_SUCCESS(f.encoder_->on_publish(&f.req_));
    EXPECT_EQ(1, (int)f.encoder_->ffmpegs_.size());
    EXPECT_EQ(1, f.factory_.create_coroutine_count_);
    EXPECT_TRUE(f.encoder_->trd_ == trd);
    EXPECT_EQ(1, trd->start_count_);

    // Unpublish stops the coroutine and frees the engines.
    f.encoder_->on_unpublish();
    EXPECT_EQ(1, trd->stop_count_);
    EXPECT_EQ(0, (int)f.encoder_->ffmpegs_.size());
    EXPECT_EQ(0, (int)f.transcoded_urls_.size());
}

VOID TEST(EncoderCycleTest, OnPublishFailsWhenCoroutineFails)
{
    MockEncoderFixture f;
    f.factory_.mock_ffmpeg_ = new MockFFMPEGForEncoder();
    MockCoroutineForForwarder *trd = new MockCoroutineForForwarder(0);
    trd->start_error_ = srs_error_new(ERROR_ST_CREATE_CYCLE_THREAD, "mock start");
    f.factory_.coroutine_ = trd;

    srs_error_t err = f.encoder_->on_publish(&f.req_);
    EXPECT_EQ(ERROR_ST_CREATE_CYCLE_THREAD, srs_error_code(err));
    srs_freep(err);
}

// A publish of a stream that an encoder outputs is not transcoded again, and is no error.
VOID TEST(EncoderCycleTest, OnPublishIgnoresTranscodedStream)
{
    srs_error_t err;

    MockEncoderFixture f;
    f.factory_.mock_ffmpeg_ = new MockFFMPEGForEncoder();
    f.transcoded_urls_.push_back("rtmp://127.0.0.1:1935/live/livestream?vhost=test.vhost");

    HELPER_EXPECT_SUCCESS(f.encoder_->on_publish(&f.req_));
    EXPECT_EQ(0, (int)f.encoder_->ffmpegs_.size());
    EXPECT_EQ(0, f.factory_.create_coroutine_count_);
    ASSERT_EQ(1, (int)f.transcoded_urls_.size());
    EXPECT_EQ("rtmp://127.0.0.1:1935/live/livestream?vhost=test.vhost", f.transcoded_urls_.at(0));
}

// Each loop starts every engine that is not running and checks it; an engine that fails stops the
// loop, for the next one to retry.
VOID TEST(EncoderCycleTest, DoCycleStartsAndChecksEngines)
{
    srs_error_t err;

    MockEncoderFixture f;
    f.encoder_->assemble();
    MockFFMPEGForEncoder *a = new MockFFMPEGForEncoder();
    MockFFMPEGForEncoder *b = new MockFFMPEGForEncoder();
    f.encoder_->ffmpegs_.push_back(a);
    f.encoder_->ffmpegs_.push_back(b);

    HELPER_EXPECT_SUCCESS(f.encoder_->do_cycle());
    EXPECT_TRUE(a->start_called_);
    EXPECT_EQ(1, a->cycle_count_);
    EXPECT_TRUE(b->start_called_);
    EXPECT_EQ(1, b->cycle_count_);

    a->cycle_error_ = srs_error_new(ERROR_SYSTEM_WAITPID, "mock waitpid");
    b->start_called_ = false;
    err = f.encoder_->do_cycle();
    EXPECT_EQ(ERROR_SYSTEM_WAITPID, srs_error_code(err));
    srs_freep(err);
    EXPECT_FALSE(b->start_called_);

    srs_freep(a->cycle_error_);
    a->start_error_ = srs_error_new(ERROR_ENCODER_FORK, "mock fork");
    err = f.encoder_->do_cycle();
    EXPECT_EQ(ERROR_ENCODER_FORK, srs_error_code(err));
    srs_freep(err);
    EXPECT_EQ(2, a->cycle_count_);
    EXPECT_FALSE(b->start_called_);
}

// A stopped coroutine ends the cycle, which stops every engine.
VOID TEST(EncoderCycleTest, CycleStopsEnginesWhenCoroutineStops)
{
    MockEncoderFixture f;
    MockFFMPEGForEncoder *a = new MockFFMPEGForEncoder();
    MockFFMPEGForEncoder *b = new MockFFMPEGForEncoder();
    f.encoder_->ffmpegs_.push_back(a);
    f.encoder_->ffmpegs_.push_back(b);
    srs_freep(f.encoder_->trd_);
    f.encoder_->trd_ = new MockCoroutineForForwarder(0);

    srs_error_t err = f.encoder_->cycle();
    EXPECT_EQ(ERROR_THREAD_DUMMY, srs_error_code(err));
    srs_freep(err);
    EXPECT_EQ(1, a->stop_count_);
    EXPECT_EQ(1, b->stop_count_);
    EXPECT_FALSE(a->start_called_);
}

// With the FFmpeg log on, each engine logs to a file of its stream and engine in the log dir.
VOID TEST(EncoderCycleTest, InitializeFFmpegLogsToFileOfEngine)
{
    srs_error_t err;

    MockEncoderFixture f;
    f.config_.ff_log_enabled_ = true;
    f.config_.ff_log_dir_ = "./objs/logs";

    MockFFMPEGForEncoder ffmpeg;
    HELPER_EXPECT_SUCCESS(f.encoder_->initialize_ffmpeg(&ffmpeg, &f.req_, &f.engine_));
    EXPECT_EQ("./objs/logs/ffmpeg-encoder-test.vhost-live-livestream-hd.log", ffmpeg.log_);

    // An engine of no name logs to the file of its stream.
    SrsConfDirective unnamed;
    unnamed.name_ = "engine";
    MockFFMPEGForEncoder ffmpeg2;
    f.config_.engine_output_ = "rtmp://127.0.0.1:[port]/[app]/[stream]_sd?vhost=[vhost]";
    HELPER_EXPECT_SUCCESS(f.encoder_->initialize_ffmpeg(&ffmpeg2, &f.req_, &unnamed));
    EXPECT_EQ("./objs/logs/ffmpeg-encoder-test.vhost-live-livestream.log", ffmpeg2.log_);

    // With the log off, FFmpeg logs to the null device.
    f.config_.ff_log_enabled_ = false;
    MockFFMPEGForEncoder ffmpeg3;
    f.config_.engine_output_ = "rtmp://127.0.0.1:[port]/[app]/[stream]_ld?vhost=[vhost]";
    HELPER_EXPECT_SUCCESS(f.encoder_->initialize_ffmpeg(&ffmpeg3, &f.req_, &f.engine_));
    EXPECT_EQ(SRS_CONSTS_NULL_FILE, ffmpeg3.log_);
}

MockAppConfigForIngesterLog::MockAppConfigForIngesterLog()
{
    ff_log_enabled_ = false;
}

MockAppConfigForIngesterLog::~MockAppConfigForIngesterLog()
{
}

std::string MockAppConfigForIngesterLog::get_engine_output(SrsConfDirective *conf)
{
    if (!conf) {
        return default_output_;
    }
    return MockAppConfigForIngester::get_engine_output(conf);
}

bool MockAppConfigForIngesterLog::get_ff_log_enabled()
{
    return ff_log_enabled_;
}

std::string MockAppConfigForIngesterLog::get_ff_log_dir()
{
    return ff_log_dir_;
}

MockAppFactoryForIngesterSetup::MockAppFactoryForIngesterSetup()
{
}

MockAppFactoryForIngesterSetup::~MockAppFactoryForIngesterSetup()
{
}

ISrsFFMPEG *MockAppFactoryForIngesterSetup::create_ffmpeg(std::string ffmpeg_bin)
{
    MockFFMPEGForEncoder *ffmpeg = new MockFFMPEGForEncoder();
    ffmpegs_.push_back(ffmpeg);
    ffmpeg_bins_.push_back(ffmpeg_bin);
    return ffmpeg;
}

// A directive of the name and arg, with the children.
static SrsConfDirective *mock_directive(std::string name, std::string arg)
{
    SrsConfDirective *d = new SrsConfDirective();
    d->name_ = name;
    if (!arg.empty()) {
        d->args_.push_back(arg);
    }
    return d;
}

// A vhost of one enabled ingest of the input, with no engine.
static SrsConfDirective *mock_ingest_vhost(std::string ffmpeg, std::string type, std::string url)
{
    SrsConfDirective *vhost = mock_directive("vhost", "test.vhost");
    SrsConfDirective *ingest = mock_directive("ingest", "livestream");
    vhost->directives_.push_back(ingest);
    ingest->directives_.push_back(mock_directive("enabled", "on"));
    if (!ffmpeg.empty()) {
        ingest->directives_.push_back(mock_directive("ffmpeg", ffmpeg));
    }
    SrsConfDirective *input = mock_directive("input", "");
    ingest->directives_.push_back(input);
    input->directives_.push_back(mock_directive("type", type));
    if (!url.empty()) {
        input->directives_.push_back(mock_directive("url", url));
    }
    return vhost;
}

// An ingest without engines copies the stream: FFmpeg reads the input at its native rate and
// publishes it as FLV, logging to the file of the stream when the log is on.
VOID TEST(IngesterSetupTest, IngestWithoutEngineCopiesStream)
{
    srs_error_t err;

    SrsUniquePtr<SrsConfDirective> vhost(mock_ingest_vhost("/usr/local/bin/ffmpeg", "stream", "http://10.0.0.2/live/livestream.m3u8"));
    MockAppConfigForIngesterLog config;
    config.add_vhost(vhost.get());
    config.default_output_ = "rtmp://127.0.0.1:[port]/live/livestream?vhost=[vhost]";
    config.ff_log_enabled_ = true;
    config.ff_log_dir_ = "./objs/logs";
    MockAppFactoryForIngesterSetup factory;

    SrsUniquePtr<SrsIngester> ingester(new SrsIngester());
    ingester->config_ = &config;
    ingester->app_factory_ = &factory;

    HELPER_EXPECT_SUCCESS(ingester->parse());
    ASSERT_EQ(1, (int)ingester->ingesters_.size());
    EXPECT_EQ("test.vhost/livestream", ingester->ingesters_.at(0)->uri());

    ASSERT_EQ(1, (int)factory.ffmpegs_.size());
    EXPECT_EQ("/usr/local/bin/ffmpeg", factory.ffmpeg_bins_.at(0));
    MockFFMPEGForEncoder *ffmpeg = factory.ffmpegs_.at(0);
    EXPECT_EQ("http://10.0.0.2/live/livestream.m3u8", ffmpeg->input_);
    EXPECT_EQ("rtmp://127.0.0.1:1935/live/livestream?vhost=test.vhost", ffmpeg->output_);
    EXPECT_EQ("./objs/logs/ffmpeg-ingest-test.vhost-live-livestream.log", ffmpeg->log_);
    ASSERT_EQ(1, (int)ffmpeg->iparams_.size());
    EXPECT_EQ("-re", ffmpeg->iparams_.at(0));
    EXPECT_EQ("flv", ffmpeg->oformat_);
    EXPECT_EQ(1, ffmpeg->initialize_copy_count_);

    ingester->clear_engines();
    ingester->config_ = NULL;
    ingester->app_factory_ = NULL;
}

// A stream input without a URL, or an FFmpeg that fails to take the input, makes no ingester.
VOID TEST(IngesterSetupTest, StreamInputRequiresUrl)
{
    srs_error_t err;

    MockAppConfigForIngesterLog config;
    config.default_output_ = "rtmp://127.0.0.1:[port]/live/livestream";
    SrsUniquePtr<SrsConfDirective> vhost(mock_ingest_vhost("/usr/local/bin/ffmpeg", "stream", ""));
    SrsConfDirective *ingest = vhost->get("ingest");

    SrsUniquePtr<SrsIngester> ingester(new SrsIngester());
    ingester->config_ = &config;

    MockFFMPEGForEncoder ffmpeg;
    err = ingester->initialize_ffmpeg(&ffmpeg, vhost.get(), ingest, NULL);
    EXPECT_EQ(ERROR_ENCODER_NO_INPUT, srs_error_code(err));
    srs_freep(err);
    EXPECT_FALSE(ffmpeg.initialize_called_);

    ingest->get("input")->directives_.push_back(mock_directive("url", "udp://0.0.0.0:2222"));
    MockFFMPEGForEncoder ffmpeg2;
    ffmpeg2.initialize_error_ = srs_error_new(ERROR_ENCODER_OUTPUT, "mock init");
    err = ingester->initialize_ffmpeg(&ffmpeg2, vhost.get(), ingest, NULL);
    EXPECT_EQ(ERROR_ENCODER_OUTPUT, srs_error_code(err));
    srs_freep(err);
    EXPECT_TRUE(ffmpeg2.initialize_called_);

    // An ingest without engines whose FFmpeg fails to set up makes no ingester.
    MockAppFactoryForIngester factory;
    ingester->app_factory_ = &factory;
    config.default_output_ = "";
    config.add_vhost(vhost.get());
    err = ingester->parse();
    EXPECT_EQ(ERROR_ENCODER_NO_OUTPUT, srs_error_code(err));
    srs_freep(err);
    EXPECT_EQ(0, (int)ingester->ingesters_.size());

    ingester->config_ = NULL;
    ingester->app_factory_ = NULL;
}

// When the config is reloaded, the ingester stops every FFmpeg, parses the ingests again, and starts
// the FFmpegs of the new config.
VOID TEST(IngesterSetupTest, ExpiredIngesterParsesAgain)
{
    srs_error_t err;

    MockAppConfigForIngesterLog config;
    config.default_output_ = "rtmp://127.0.0.1:[port]/live/livestream";
    MockAppFactoryForIngesterSetup factory;
    SrsUniquePtr<SrsIngester> ingester(new SrsIngester());
    ingester->assemble();
    ingester->config_ = &config;
    ingester->app_factory_ = &factory;

    ingester->ingesters_.push_back(new MockIngesterFFMPEG());
    ingester->expired_ = true;

    SrsUniquePtr<SrsConfDirective> vhost(mock_ingest_vhost("/usr/local/bin/ffmpeg", "file", "./doc/source.flv"));
    config.add_vhost(vhost.get());

    HELPER_EXPECT_SUCCESS(ingester->do_cycle());
    EXPECT_FALSE(ingester->expired_);
    ASSERT_EQ(1, (int)ingester->ingesters_.size());
    EXPECT_TRUE(dynamic_cast<SrsIngesterFFMPEG *>(ingester->ingesters_.at(0)) != NULL);
    ASSERT_EQ(1, (int)factory.ffmpegs_.size());
    EXPECT_TRUE(factory.ffmpegs_.at(0)->start_called_);
    EXPECT_EQ(1, factory.ffmpegs_.at(0)->cycle_count_);

    // A config that fails to parse leaves no ingester.
    SrsUniquePtr<SrsConfDirective> bad(mock_ingest_vhost("", "file", "./doc/source.flv"));
    config.clear_vhosts();
    config.add_vhost(bad.get());
    ingester->expired_ = true;
    err = ingester->do_cycle();
    EXPECT_EQ(ERROR_ENCODER_PARSE, srs_error_code(err));
    srs_freep(err);
    EXPECT_FALSE(ingester->expired_);
    EXPECT_EQ(0, (int)ingester->ingesters_.size());

    config.clear_vhosts();
    ingester->config_ = NULL;
    ingester->app_factory_ = NULL;
}

// The output of FFmpeg goes to files given as shell redirects, with or without a space after the >.
VOID TEST(ProcessArgsTest, InitializeParsesSpacedRedirects)
{
    srs_error_t err;

    if (true) {
        vector<string> argv;
        argv.push_back("ffmpeg");
        argv.push_back("-i");
        argv.push_back("rtmp://127.0.0.1/live/livestream");
        argv.push_back("1");
        argv.push_back(">");
        argv.push_back("out.log");
        argv.push_back("2");
        argv.push_back(">");
        argv.push_back("err.log");

        SrsProcess process;
        HELPER_EXPECT_SUCCESS(process.initialize("ffmpeg", argv));
        EXPECT_EQ("out.log", process.stdout_file_);
        EXPECT_EQ("err.log", process.stderr_file_);
        EXPECT_EQ("ffmpeg -i rtmp://127.0.0.1/live/livestream", process.actual_cli_);
        EXPECT_EQ("ffmpeg -i rtmp://127.0.0.1/live/livestream 1 > out.log 2 > err.log", process.cli_);
    }

    if (true) {
        vector<string> argv;
        argv.push_back("ffmpeg");
        argv.push_back("1");
        argv.push_back(">out.log");
        argv.push_back("2");
        argv.push_back(">err.log");
        argv.push_back("-y");

        SrsProcess process;
        HELPER_EXPECT_SUCCESS(process.initialize("ffmpeg", argv));
        EXPECT_EQ("out.log", process.stdout_file_);
        EXPECT_EQ("err.log", process.stderr_file_);
        EXPECT_EQ("ffmpeg -y", process.actual_cli_);
    }

    // A > at the end names no file, so the output is not redirected.
    if (true) {
        vector<string> argv;
        argv.push_back("ffmpeg");
        argv.push_back("2");
        argv.push_back(">");

        SrsProcess process;
        HELPER_EXPECT_SUCCESS(process.initialize("ffmpeg", argv));
        EXPECT_EQ("", process.stderr_file_);
        EXPECT_EQ("ffmpeg", process.actual_cli_);

        vector<string> argv2;
        argv2.push_back("ffmpeg");
        argv2.push_back("1");
        argv2.push_back(">");

        SrsProcess process2;
        HELPER_EXPECT_SUCCESS(process2.initialize("ffmpeg", argv2));
        EXPECT_EQ("", process2.stdout_file_);
        EXPECT_EQ("ffmpeg", process2.actual_cli_);
    }
}

// A process that is not running has nothing to check, stop or kill.
VOID TEST(ProcessArgsTest, NotStartedProcessIgnoresControls)
{
    srs_error_t err;

    SrsProcess process;
    HELPER_EXPECT_SUCCESS(process.cycle());
    process.fast_stop();
    EXPECT_FALSE(process.fast_stopped_);
    process.fast_kill();
    process.stop();
    EXPECT_FALSE(process.started());

    // Nor has a process that got no pid.
    process.is_started_ = true;
    process.pid_ = -1;
    process.fast_stop();
    EXPECT_FALSE(process.fast_stopped_);
    process.fast_kill();
    process.is_started_ = false;
}

#ifndef _WIN32 // POSIX only: children run /bin/sh and /bin/sleep
// A fast stop sends the child SIGTERM and lets it quit on its own: the process is not checked any
// more, and a fast kill ends it.
VOID TEST(ProcessArgsTest, FastStopThenFastKill)
{
    srs_error_t err;

    vector<string> argv;
    argv.push_back("/bin/sleep");
    argv.push_back("10");
    argv.push_back(string("1>") + SRS_UTEST_NULL_FILE);

    SrsProcess process;
    HELPER_ASSERT_SUCCESS(process.initialize("/bin/sleep", argv));
    HELPER_ASSERT_SUCCESS(process.start());
    pid_t pid = process.get_pid();

    process.fast_stop();
    EXPECT_TRUE(process.fast_stopped_);
    HELPER_EXPECT_SUCCESS(process.cycle());
    EXPECT_TRUE(process.started());

    // The child ignores SIGTERM, so it is still there for the fast kill.
    process.fast_kill();
    int status = 0;
    pid_t r = srs_waitpid(pid, &status, 0);
    EXPECT_TRUE(r == pid || (r == -1 && errno == ECHILD));

    // The child is gone, so a stop finds no process to kill and leaves it be.
    process.stop();
    EXPECT_EQ(pid, process.get_pid());
}

// A child reaped by someone else can be neither checked, signaled nor killed.
VOID TEST(ProcessArgsTest, ReapedChildFailsControls)
{
    srs_error_t err;

    vector<string> argv;
    argv.push_back("/bin/sleep");
    argv.push_back("10");
    argv.push_back(string("1>") + SRS_UTEST_NULL_FILE);

    SrsProcess process;
    HELPER_ASSERT_SUCCESS(process.initialize("/bin/sleep", argv));
    HELPER_ASSERT_SUCCESS(process.start());
    pid_t pid = process.get_pid();

    EXPECT_EQ(0, srs_kill(pid, SIGKILL));
    int status = 0;
    EXPECT_EQ(pid, srs_waitpid(pid, &status, 0));

    err = process.cycle();
    EXPECT_EQ(ERROR_SYSTEM_WAITPID, srs_error_code(err));
    srs_freep(err);

    process.fast_stop();
    EXPECT_FALSE(process.fast_stopped_);
    process.fast_kill();
    process.is_started_ = false;
}

// The exec of a publish starts each process and checks it on each loop, and stops them all when
// the coroutine stops.
VOID TEST(NgExecCycleTest, DoCycleRunsExecProcesses)
{
    srs_error_t err;

    SrsUniquePtr<SrsNgExec> exec(new SrsNgExec());
    exec->assemble();

    // No exec for the publish.
    HELPER_EXPECT_SUCCESS(exec->do_cycle());

    vector<string> argv;
    argv.push_back("/bin/sh");
    argv.push_back("-c");
    argv.push_back("exit 0");
    argv.push_back(string("1>") + SRS_UTEST_NULL_FILE);
    SrsProcess *process = new SrsProcess();
    HELPER_ASSERT_SUCCESS(process->initialize("/bin/sh", argv));
    exec->exec_publishs_.push_back(process);

    HELPER_EXPECT_SUCCESS(exec->do_cycle());
    EXPECT_GT(process->get_pid(), 0);

    srs_freep(exec->trd_);
    exec->trd_ = new MockCoroutineForForwarder(0);
    err = exec->cycle();
    EXPECT_EQ(ERROR_THREAD_DUMMY, srs_error_code(err));
    srs_freep(err);
    EXPECT_FALSE(process->started());
}

// A process that can not be checked fails the loop, for the next loop to retry.
VOID TEST(NgExecCycleTest, DoCycleFailsWhenProcessCheckFails)
{
    srs_error_t err;

    SrsUniquePtr<SrsNgExec> exec(new SrsNgExec());
    exec->assemble();

    vector<string> argv;
    argv.push_back("/bin/sleep");
    argv.push_back("10");
    argv.push_back(string("1>") + SRS_UTEST_NULL_FILE);
    SrsProcess *process = new SrsProcess();
    HELPER_ASSERT_SUCCESS(process->initialize("/bin/sleep", argv));
    HELPER_ASSERT_SUCCESS(process->start());
    exec->exec_publishs_.push_back(process);

    pid_t pid = process->get_pid();
    EXPECT_EQ(0, srs_kill(pid, SIGKILL));
    int status = 0;
    EXPECT_EQ(pid, srs_waitpid(pid, &status, 0));

    err = exec->do_cycle();
    EXPECT_EQ(ERROR_SYSTEM_WAITPID, srs_error_code(err));
    srs_freep(err);
    process->is_started_ = false;
}
#endif

// An ADTS frame of AAC LC, stereo, at the sampling frequency index sfi, with size bytes of payload.
static std::string mock_adts_frame(int sfi, int size)
{
    std::string frame;
    int frame_length = 7 + size;
    frame.push_back((char)0xff);
    frame.push_back((char)0xf1);
    frame.push_back((char)((1 << 6) | (sfi << 2) | (2 >> 2)));
    frame.push_back((char)(((2 & 0x03) << 6) | ((frame_length >> 11) & 0x03)));
    frame.push_back((char)((frame_length >> 3) & 0xff));
    frame.push_back((char)(((frame_length & 0x07) << 5) | 0x1f));
    frame.push_back((char)0xfc);
    for (int i = 0; i < size; i++) {
        frame.push_back((char)(0x21 + i));
    }
    return frame;
}

// A TS message of the channel, with the PES stream id and payload.
class MockTsMessageFixture
{
public:
    SrsTsChannel channel_;
    SrsTsPacket packet_;
    SrsTsMessage *msg_;

public:
    MockTsMessageFixture(SrsTsPidApply apply, SrsTsStream stream, SrsTsPESStreamId sid, std::string payload) : packet_(NULL)
    {
        channel_.apply_ = apply;
        channel_.stream_ = stream;
        msg_ = new SrsTsMessage(&channel_, &packet_);
        msg_->sid_ = sid;
        msg_->dts_ = msg_->pts_ = 90000;
        msg_->payload_->append(payload.data(), (int)payload.length());
    }
    virtual ~MockTsMessageFixture()
    {
        srs_freep(msg_);
    }
};

static SrsMpegtsQueue *mock_caster_queue_of(SrsMpegtsOverUdp *caster)
{
    return dynamic_cast<SrsMpegtsQueue *>(caster->queue_);
}

// Queue a packet of the type and time in the caster, as one it got before.
static void mock_caster_queue(SrsMpegtsOverUdp *caster, SrsFrameType type, int64_t timestamp)
{
    SrsMediaPacket *msg = new SrsMediaPacket();
    msg->message_type_ = type;
    msg->timestamp_ = timestamp;
    char *payload = new char[1];
    payload[0] = (char)type;
    msg->wrap(payload, 1);
    srs_error_t err = caster->queue_->push(msg);
    srs_freep(err);
}

// The audio of a private stream 1, as some encoders send AAC, is taken as common audio and published.
VOID TEST(MpegtsOverUdpTsTest, OnTsMessagePublishesPrivateStreamAudio)
{
    srs_error_t err;

    SrsUniquePtr<SrsMpegtsOverUdp> caster(new SrsMpegtsOverUdp());
    caster->assemble();
    caster->sdk_ = new MockRtmpClientForForwarder();

    MockTsMessageFixture f(SrsTsPidApplyAudio, SrsTsStreamAudioAAC, SrsTsPESStreamIdPrivateStream1, mock_adts_frame(4, 8));
    HELPER_EXPECT_SUCCESS(caster->on_ts_message(f.msg_));
    EXPECT_EQ(SrsTsPESStreamIdAudioCommon, f.msg_->sid_);

    // The sequence header, then the frame.
    EXPECT_EQ(2, (int)mock_caster_queue_of(caster.get())->msgs_.size());
    EXPECT_EQ(2, mock_caster_queue_of(caster.get())->nb_audios_);
    EXPECT_FALSE(caster->aac_specific_config_.empty());
}

// Only the first audio or video stream of a PES, of AAC or H.264, is published.
VOID TEST(MpegtsOverUdpTsTest, OnTsMessageRefusesUnsupportedStreams)
{
    SrsUniquePtr<SrsMpegtsOverUdp> caster(new SrsMpegtsOverUdp());
    caster->assemble();
    caster->sdk_ = new MockRtmpClientForForwarder();

    // The second video stream.
    if (true) {
        MockTsMessageFixture f(SrsTsPidApplyVideo, SrsTsStreamVideoH264, (SrsTsPESStreamId)0xe1, std::string("\x00\x00\x00\x01\x09", 5));
        srs_error_t err = caster->on_ts_message(f.msg_);
        EXPECT_EQ(ERROR_STREAM_CASTER_TS_ES, srs_error_code(err));
        srs_freep(err);
    }

    // A stream of neither audio nor video.
    if (true) {
        MockTsMessageFixture f(SrsTsPidApplyVideo, SrsTsStreamVideoH264, SrsTsPESStreamIdPaddingStream, "");
        srs_error_t err = caster->on_ts_message(f.msg_);
        EXPECT_EQ(ERROR_STREAM_CASTER_TS_ES, srs_error_code(err));
        srs_freep(err);
    }

    // HEVC.
    if (true) {
        MockTsMessageFixture f(SrsTsPidApplyVideo, SrsTsStreamVideoHEVC, SrsTsPESStreamIdVideoCommon, std::string("\x00\x00\x00\x01\x40", 5));
        srs_error_t err = caster->on_ts_message(f.msg_);
        EXPECT_EQ(ERROR_STREAM_CASTER_TS_CODEC, srs_error_code(err));
        srs_freep(err);
    }

    EXPECT_EQ(0, (int)mock_caster_queue_of(caster.get())->msgs_.size());
}

// A PES that fails to demux fails the message.
VOID TEST(MpegtsOverUdpTsTest, OnTsMessageFailsForBadPayload)
{
    SrsUniquePtr<SrsMpegtsOverUdp> caster(new SrsMpegtsOverUdp());
    caster->assemble();
    caster->sdk_ = new MockRtmpClientForForwarder();

    if (true) {
        MockTsMessageFixture f(SrsTsPidApplyAudio, SrsTsStreamAudioAAC, SrsTsPESStreamIdAudioCommon, "not adts");
        srs_error_t err = caster->on_ts_message(f.msg_);
        EXPECT_EQ(ERROR_AAC_REQUIRED_ADTS, srs_error_code(err));
        srs_freep(err);
    }

    if (true) {
        MockMpegtsRawH264Stream *avc = new MockMpegtsRawH264Stream();
        avc->annexb_demux_error_ = srs_error_new(ERROR_H264_API_NO_PREFIXED, "mock annexb");
        srs_freep(caster->avc_);
        caster->avc_ = avc;

        MockTsMessageFixture f(SrsTsPidApplyVideo, SrsTsStreamVideoH264, SrsTsPESStreamIdVideoCommon, std::string("\x00\x00\x00\x01\x65", 5));
        srs_error_t err = caster->on_ts_message(f.msg_);
        EXPECT_EQ(ERROR_H264_API_NO_PREFIXED, srs_error_code(err));
        srs_freep(err);
    }
}

// An RTMP output that refuses the connection drops the media, and the next media connects again.
VOID TEST(MpegtsOverUdpTsTest, OnTsMediaFailsWhenOutputRefuses)
{
    srs_error_t err;

    SrsUniquePtr<SrsMpegtsOverUdp> caster(new SrsMpegtsOverUdp());
    caster->assemble();
    MockAppFactoryForMpegtsOverUdp factory;
    caster->app_factory_ = &factory;

    for (int i = 0; i < 2; i++) {
        // The caster frees the client it fails to connect.
        MockRtmpClient *sdk = new MockRtmpClient();
        sdk->connect_error_ = srs_error_new(ERROR_SOCKET_CONNECT, "mock connect");
        factory.mock_rtmp_client_ = sdk;
        factory.create_rtmp_client_called_ = false;

        std::string payload = (i == 0) ? std::string("\x00\x00\x00\x01\x65", 5) : mock_adts_frame(4, 8);
        SrsBuffer avs((char *)payload.data(), (int)payload.length());
        SrsTsMessage msg(NULL, NULL);
        err = (i == 0) ? caster->on_ts_video(&msg, &avs) : caster->on_ts_audio(&msg, &avs);
        EXPECT_EQ(ERROR_SOCKET_CONNECT, srs_error_code(err)) << i;
        srs_freep(err);
        EXPECT_TRUE(factory.create_rtmp_client_called_) << i;
        EXPECT_TRUE(caster->sdk_ == NULL) << i;
    }

    caster->app_factory_ = NULL;
}

// The video demuxer, with the mock H.264 of a test that returns the frame of the NALU type.
class MockCasterVideoFixture
{
public:
    SrsMpegtsOverUdp *caster_;
    MockMpegtsRawH264Stream *avc_;
    char frame_[2];
    std::string payload_;

public:
    MockCasterVideoFixture(char nalu)
    {
        caster_ = new SrsMpegtsOverUdp();
        caster_->assemble();
        caster_->sdk_ = new MockRtmpClientForForwarder();
        avc_ = new MockMpegtsRawH264Stream();
        srs_freep(caster_->avc_);
        caster_->avc_ = avc_;

        frame_[0] = nalu;
        frame_[1] = 0x01;
        payload_ = std::string(frame_, 2);
        avc_->demux_frame_ = frame_;
        avc_->demux_frame_size_ = 2;
        avc_->sps_output_ = "sps";
        avc_->pps_output_ = "pps";
    }
    virtual ~MockCasterVideoFixture()
    {
        srs_freep(caster_);
    }
    srs_error_t on_ts_video()
    {
        SrsTsMessage msg(NULL, NULL);
        msg.dts_ = msg.pts_ = 90000;
        SrsBuffer avs((char *)payload_.data(), (int)payload_.length());
        return caster_->on_ts_video(&msg, &avs);
    }
};

// An SPS or PPS that the caster has is not sent again; an access unit delimiter is dropped.
VOID TEST(MpegtsOverUdpTsTest, OnTsVideoSkipsKnownParameterSets)
{
    srs_error_t err;

    if (true) {
        MockCasterVideoFixture f(0x67);
        f.avc_->is_sps_result_ = true;
        f.caster_->h264_sps_ = "sps";
        HELPER_EXPECT_SUCCESS(f.on_ts_video());
        EXPECT_FALSE(f.caster_->h264_sps_changed_);
        EXPECT_EQ(0, (int)mock_caster_queue_of(f.caster_)->msgs_.size());
    }

    if (true) {
        MockCasterVideoFixture f(0x68);
        f.avc_->is_pps_result_ = true;
        f.caster_->h264_pps_ = "pps";
        HELPER_EXPECT_SUCCESS(f.on_ts_video());
        EXPECT_FALSE(f.caster_->h264_pps_changed_);
        EXPECT_EQ(0, (int)mock_caster_queue_of(f.caster_)->msgs_.size());
    }

    if (true) {
        MockCasterVideoFixture f(0x09);
        HELPER_EXPECT_SUCCESS(f.on_ts_video());
        EXPECT_FALSE(f.avc_->is_sps_called_);
        EXPECT_EQ(0, (int)mock_caster_queue_of(f.caster_)->msgs_.size());
    }
}

VOID TEST(MpegtsOverUdpTsTest, OnTsVideoFailsForBadParameterSets)
{
    if (true) {
        MockCasterVideoFixture f(0x67);
        f.avc_->is_sps_result_ = true;
        f.avc_->sps_demux_error_ = srs_error_new(ERROR_STREAM_CASTER_AVC_SPS, "mock sps");
        srs_error_t err = f.on_ts_video();
        EXPECT_EQ(ERROR_STREAM_CASTER_AVC_SPS, srs_error_code(err));
        srs_freep(err);
    }

    if (true) {
        MockCasterVideoFixture f(0x68);
        f.avc_->is_pps_result_ = true;
        f.avc_->pps_demux_error_ = srs_error_new(ERROR_STREAM_CASTER_AVC_PPS, "mock pps");
        srs_error_t err = f.on_ts_video();
        EXPECT_EQ(ERROR_STREAM_CASTER_AVC_PPS, srs_error_code(err));
        srs_freep(err);
    }

    if (true) {
        MockCasterVideoFixture f(0x65);
        f.avc_->mux_ipb_frame_error_ = srs_error_new(ERROR_H264_API_NO_PREFIXED, "mock ipb");
        srs_error_t err = f.on_ts_video();
        EXPECT_EQ(ERROR_H264_API_NO_PREFIXED, srs_error_code(err));
        srs_freep(err);
    }

    // A frame before the SPS and PPS is dropped.
    if (true) {
        MockCasterVideoFixture f(0x65);
        srs_error_t err = f.on_ts_video();
        EXPECT_EQ(ERROR_H264_DROP_BEFORE_SPS_PPS, srs_error_code(err));
        srs_freep(err);
        EXPECT_EQ(0, (int)mock_caster_queue_of(f.caster_)->msgs_.size());
    }
}

// An output that fails to take the sequence header closes, and starts over from the next SPS and PPS.
VOID TEST(MpegtsOverUdpTsTest, OnTsVideoClosesWhenSequenceHeaderSendFails)
{
    for (int i = 0; i < 2; i++) {
        bool sps = (i == 0);
        MockCasterVideoFixture f(sps ? 0x67 : 0x68);
        f.avc_->is_sps_result_ = sps;
        f.avc_->is_pps_result_ = !sps;
        if (sps) {
            f.caster_->h264_pps_ = "pps";
            f.caster_->h264_pps_changed_ = true;
        } else {
            f.caster_->h264_sps_ = "sps";
            f.caster_->h264_sps_changed_ = true;
        }
        MockRtmpClientForForwarder *sdk = dynamic_cast<MockRtmpClientForForwarder *>(f.caster_->sdk_);
        sdk->send_error_ = srs_error_new(ERROR_SOCKET_WRITE, "mock send");

        // The queue holds enough to send the sequence header at once.
        mock_caster_queue(f.caster_, SrsFrameTypeAudio, 0);
        mock_caster_queue(f.caster_, SrsFrameTypeAudio, 20);
        mock_caster_queue(f.caster_, SrsFrameTypeVideo, 10);

        srs_error_t err = f.on_ts_video();
        EXPECT_EQ(ERROR_SOCKET_WRITE, srs_error_code(err)) << i;
        srs_freep(err);
        EXPECT_TRUE(f.caster_->sdk_ == NULL) << i;
        EXPECT_TRUE(f.caster_->h264_sps_.empty()) << i;
        EXPECT_TRUE(f.caster_->h264_pps_.empty()) << i;
    }
}

// An output that fails to take the audio closes; an empty ADTS frame is skipped.
VOID TEST(MpegtsOverUdpTsTest, OnTsAudioClosesWhenSendFails)
{
    // The sequence header fails to send.
    if (true) {
        SrsUniquePtr<SrsMpegtsOverUdp> caster(new SrsMpegtsOverUdp());
        caster->assemble();
        MockRtmpClientForForwarder *sdk = new MockRtmpClientForForwarder();
        sdk->send_error_ = srs_error_new(ERROR_SOCKET_WRITE, "mock send");
        caster->sdk_ = sdk;
        mock_caster_queue(caster.get(), SrsFrameTypeVideo, 0);
        mock_caster_queue(caster.get(), SrsFrameTypeVideo, 40);
        mock_caster_queue(caster.get(), SrsFrameTypeAudio, 10);

        std::string payload = mock_adts_frame(4, 8);
        SrsBuffer avs((char *)payload.data(), (int)payload.length());
        SrsTsMessage msg(NULL, NULL);
        srs_error_t err = caster->on_ts_audio(&msg, &avs);
        EXPECT_EQ(ERROR_SOCKET_WRITE, srs_error_code(err));
        srs_freep(err);
        EXPECT_TRUE(caster->sdk_ == NULL);
    }

    // The frame fails to send, after the sequence header is sent.
    if (true) {
        SrsUniquePtr<SrsMpegtsOverUdp> caster(new SrsMpegtsOverUdp());
        caster->assemble();
        MockRtmpClientForForwarder *sdk = new MockRtmpClientForForwarder();
        sdk->send_error_ = srs_error_new(ERROR_SOCKET_WRITE, "mock send");
        caster->sdk_ = sdk;
        caster->aac_specific_config_ = "sh";
        mock_caster_queue(caster.get(), SrsFrameTypeVideo, 0);
        mock_caster_queue(caster.get(), SrsFrameTypeVideo, 40);
        mock_caster_queue(caster.get(), SrsFrameTypeAudio, 10);

        std::string payload = mock_adts_frame(4, 8);
        SrsBuffer avs((char *)payload.data(), (int)payload.length());
        SrsTsMessage msg(NULL, NULL);
        srs_error_t err = caster->on_ts_audio(&msg, &avs);
        EXPECT_EQ(ERROR_SOCKET_WRITE, srs_error_code(err));
        srs_freep(err);
        EXPECT_TRUE(caster->sdk_ == NULL);
    }

    // An ADTS frame of no data has nothing to publish.
    if (true) {
        SrsUniquePtr<SrsMpegtsOverUdp> caster(new SrsMpegtsOverUdp());
        caster->assemble();
        caster->sdk_ = new MockRtmpClientForForwarder();

        std::string payload = mock_adts_frame(4, 0);
        SrsBuffer avs((char *)payload.data(), (int)payload.length());
        SrsTsMessage msg(NULL, NULL);
        srs_error_t err = caster->on_ts_audio(&msg, &avs);
        HELPER_EXPECT_SUCCESS(err);
        EXPECT_EQ(0, (int)mock_caster_queue_of(caster.get())->msgs_.size());
        EXPECT_TRUE(caster->aac_specific_config_.empty());
    }
}

// The caster publishes to the output of its stream caster config.
VOID TEST(MpegtsOverUdpTsTest, InitializeTakesOutputOfConfig)
{
    srs_error_t err;

    MockAppConfig config;
    SrsUniquePtr<SrsMpegtsOverUdp> caster(new SrsMpegtsOverUdp());
    caster->config_ = &config;

    SrsConfDirective conf;
    HELPER_EXPECT_SUCCESS(caster->initialize(&conf));
    EXPECT_EQ("", caster->output_);

    caster->config_ = NULL;
}

MockEdgeForwarderForLiveSource::MockEdgeForwarderForLiveSource()
{
    start_count_ = 0;
    stop_count_ = 0;
    proxy_count_ = 0;
    start_error_ = srs_success;
}

MockEdgeForwarderForLiveSource::~MockEdgeForwarderForLiveSource()
{
    srs_freep(start_error_);
}

void MockEdgeForwarderForLiveSource::set_queue_size(srs_utime_t queue_size)
{
}

srs_error_t MockEdgeForwarderForLiveSource::initialize(SrsSharedPtr<SrsLiveSource> s, ISrsPublishEdge *e, ISrsRequest *r)
{
    return srs_success;
}

srs_error_t MockEdgeForwarderForLiveSource::start()
{
    start_count_++;
    return srs_error_copy(start_error_);
}

void MockEdgeForwarderForLiveSource::stop()
{
    stop_count_++;
}

srs_error_t MockEdgeForwarderForLiveSource::proxy(SrsRtmpCommonMessage *msg)
{
    proxy_count_++;
    return srs_success;
}

// A publish to an edge source goes to its publish edge: the publish starts the push to the origin,
// each message is proxied, and the unpublish stops the push.
VOID TEST(LiveSourceEdgeTest, EdgePublishGoesToPublishEdge)
{
    srs_error_t err;

    SrsUniquePtr<SrsLiveSource> source(new SrsLiveSource());
    MockEdgeForwarderForLiveSource *forwarder = new MockEdgeForwarderForLiveSource();
    srs_freep(source->publish_edge_->forwarder_);
    source->publish_edge_->forwarder_ = forwarder;

    HELPER_EXPECT_SUCCESS(source->on_edge_start_publish());
    EXPECT_EQ(1, forwarder->start_count_);
    EXPECT_EQ(SrsEdgeStatePublish, source->publish_edge_->state_);

    // A second publisher is refused while the first one publishes.
    err = source->on_edge_start_publish();
    EXPECT_EQ(ERROR_RTMP_EDGE_PUBLISH_STATE, srs_error_code(err));
    srs_freep(err);
    EXPECT_EQ(1, forwarder->start_count_);

    SrsUniquePtr<SrsRtmpCommonMessage> msg(mock_edge_message(RTMP_MSG_VideoMessage, 8, 0x17));
    HELPER_EXPECT_SUCCESS(source->on_edge_proxy_publish(msg.get()));
    EXPECT_EQ(1, forwarder->proxy_count_);

    source->on_edge_proxy_unpublish();
    EXPECT_EQ(1, forwarder->stop_count_);
    EXPECT_EQ(SrsEdgeStateInit, source->publish_edge_->state_);
}

// The source plays with the jitter algorithm of its vhost, off until it is configured.
VOID TEST(LiveSourceEdgeTest, JitterIsOffByDefault)
{
    SrsUniquePtr<SrsLiveSource> source(new SrsLiveSource());
    EXPECT_EQ(SrsRtmpJitterAlgorithmOFF, source->jitter());

    source->jitter_algorithm_ = SrsRtmpJitterAlgorithmZERO;
    EXPECT_EQ(SrsRtmpJitterAlgorithmZERO, source->jitter());
}

// The GOP cache starts at the time of its first packet, or 0 when it is empty.
VOID TEST(LiveSourceEdgeTest, GopCacheStartsAtFirstPacket)
{
    SrsUniquePtr<SrsGopCache> cache(new SrsGopCache());
    EXPECT_EQ(0, cache->start_time());

    SrsMediaPacket *first = new SrsMediaPacket();
    first->message_type_ = SrsFrameTypeVideo;
    first->timestamp_ = 1200;
    cache->gop_cache_.push_back(first);
    SrsMediaPacket *second = new SrsMediaPacket();
    second->message_type_ = SrsFrameTypeAudio;
    second->timestamp_ = 1240;
    cache->gop_cache_.push_back(second);

    EXPECT_EQ(1200 * SRS_UTIME_MILLISECONDS, cache->start_time());
}

extern bool srs_hls_can_continue(int ret, SrsMediaPacket *sh, SrsMediaPacket *msg);

// HLS goes on after a decode error only for a packet of the size of the sequence header, which
// is taken as a copy of the sequence header.
VOID TEST(LiveSourceEdgeTest, HlsContinuesOnlyForSequenceHeaderCopy)
{
    SrsMediaPacket sh;
    sh.wrap(new char[4], 4);
    SrsMediaPacket same;
    same.wrap(new char[4], 4);
    SrsMediaPacket other;
    other.wrap(new char[8], 8);

    EXPECT_TRUE(srs_hls_can_continue(ERROR_HLS_DECODE_ERROR, &sh, &same));
    EXPECT_FALSE(srs_hls_can_continue(ERROR_HLS_DECODE_ERROR, &sh, &other));
    EXPECT_FALSE(srs_hls_can_continue(ERROR_HLS_DECODE_ERROR, NULL, &same));
    EXPECT_FALSE(srs_hls_can_continue(ERROR_HLS_WRITE_FAILED, &sh, &same));
}

// A fragment of the path, of duration ms long.
static SrsFragment *mock_fragment(std::string path, int64_t duration)
{
    SrsFragment *fragment = new SrsFragment();
    fragment->set_path(path);
    fragment->append(1000);
    fragment->append(1000 + duration);
    return fragment;
}

static void mock_touch_file(std::string path)
{
    FILE *fp = ::fopen(path.c_str(), "wb");
    if (fp) {
        ::fputs("ts", fp);
        ::fclose(fp);
    }
}

static bool mock_file_exists(std::string path)
{
    FILE *fp = ::fopen(path.c_str(), "rb");
    if (!fp) {
        return false;
    }
    ::fclose(fp);
    return true;
}

// The window keeps the newest fragments that fill it; the older ones expire, and are deleted from
// the disk only when asked.
VOID TEST(FragmentWindowCleanupTest, ShrinkExpiresOldFragments)
{
    string a = _srs_tmp_file_prefix + "window-a.ts";
    string b = _srs_tmp_file_prefix + "window-b.ts";
    string c = _srs_tmp_file_prefix + "window-c.ts";
    mock_touch_file(a);
    mock_touch_file(b);
    mock_touch_file(c);

    SrsUniquePtr<SrsFragmentWindow> window(new SrsFragmentWindow());
    window->append(mock_fragment(a, 4000));
    window->append(mock_fragment(b, 4000));
    window->append(mock_fragment(c, 4000));

    // A window of 5s keeps the last two fragments, which hold 8s, more than the window.
    window->shrink(5 * SRS_UTIME_SECONDS);
    EXPECT_EQ(2, (int)window->fragments_.size());
    EXPECT_EQ(1, (int)window->expired_fragments_.size());
    EXPECT_EQ(b, dynamic_cast<SrsFragment *>(window->first())->fullpath());

    // Clear the expired, keeping the files.
    window->clear_expired(false);
    EXPECT_EQ(0, (int)window->expired_fragments_.size());
    EXPECT_TRUE(mock_file_exists(a));

    // A window of 1s keeps the last fragment, and the expired file is deleted.
    window->shrink(1 * SRS_UTIME_SECONDS);
    EXPECT_EQ(1, (int)window->fragments_.size());
    window->clear_expired(true);
    EXPECT_FALSE(mock_file_exists(b));
    EXPECT_TRUE(mock_file_exists(c));

    ::unlink(a.c_str());
    ::unlink(c.c_str());
}

// Disposing the window deletes the files of every fragment, expired or not, and goes on when a file
// is already gone.
VOID TEST(FragmentWindowCleanupTest, DisposeDeletesAllFiles)
{
    string a = _srs_tmp_file_prefix + "dispose-a.ts";
    string b = _srs_tmp_file_prefix + "dispose-b.ts";
    string gone = _srs_tmp_file_prefix + "dispose-gone.ts";
    mock_touch_file(a);
    mock_touch_file(b);
    ::unlink(gone.c_str());

    SrsUniquePtr<SrsFragmentWindow> window(new SrsFragmentWindow());
    window->append(mock_fragment(a, 4000));
    window->append(mock_fragment(gone, 4000));
    window->append(mock_fragment(b, 4000));
    window->append(mock_fragment(gone, 4000));
    window->shrink(5 * SRS_UTIME_SECONDS);
    ASSERT_EQ(2, (int)window->expired_fragments_.size());

    window->dispose();
    EXPECT_TRUE(window->empty());
    EXPECT_EQ(0, (int)window->expired_fragments_.size());
    EXPECT_FALSE(mock_file_exists(a));
    EXPECT_FALSE(mock_file_exists(b));

    // The window frees the expired fragments it still holds.
    SrsUniquePtr<SrsFragmentWindow> window2(new SrsFragmentWindow());
    window2->append(mock_fragment(a, 4000));
    window2->append(mock_fragment(b, 4000));
    window2->shrink(1 * SRS_UTIME_SECONDS);
    EXPECT_EQ(1, (int)window2->expired_fragments_.size());
}

// A fragment fails to unlink a missing file, to create a dir under a file, and to rename a missing
// temporary file.
VOID TEST(FragmentWindowCleanupTest, FragmentFileErrors)
{
    srs_error_t err;

    string file = _srs_tmp_file_prefix + "fragment-file";
    mock_touch_file(file);

    SrsUniquePtr<SrsFragment> fragment(mock_fragment(_srs_tmp_file_prefix + "fragment-gone.ts", 4000));
    err = fragment->unlink_file();
    EXPECT_EQ(ERROR_SYSTEM_FILE_UNLINK, srs_error_code(err));
    srs_freep(err);

    err = fragment->rename();
    EXPECT_EQ(ERROR_SYSTEM_FRAGMENT_RENAME, srs_error_code(err));
    srs_freep(err);

    fragment->set_path(file + "/live/livestream-0.ts");
    err = fragment->create_dir();
    HELPER_EXPECT_FAILED(err);

    ::unlink(file.c_str());
}

static std::string mock_read_file(std::string path)
{
    std::string data;
    FILE *fp = ::fopen(path.c_str(), "rb");
    if (!fp) {
        return data;
    }
    char buf[1024];
    size_t n = 0;
    while ((n = ::fread(buf, 1, sizeof(buf), fp)) > 0) {
        data.append(buf, n);
    }
    ::fclose(fp);
    return data;
}

// A segment named by its duration gets the duration in its file and its URI once it is closed.
VOID TEST(HlsFilesTest, SegmentRenameFillsDuration)
{
    srs_error_t err;

    SrsTsContext context;
    MockSrsFileWriter writer;
    SrsUniquePtr<SrsHlsSegment> segment(new SrsHlsSegment(&context, SrsAudioCodecIdAAC, SrsVideoCodecIdAVC, &writer));
    string path = _srs_tmp_file_prefix + "hls-[duration].ts";
    string final_path = _srs_tmp_file_prefix + "hls-4000.ts";
    segment->set_path(path);
    segment->uri_ = "livestream-[duration].ts";
    segment->append(1000);
    segment->append(5000);
    mock_touch_file(segment->tmppath());
    ::unlink(final_path.c_str());

    HELPER_EXPECT_SUCCESS(segment->rename());
    EXPECT_EQ("livestream-4000.ts", segment->uri_);
    EXPECT_EQ(final_path, segment->fullpath());
    EXPECT_TRUE(mock_file_exists(final_path));

    ::unlink(final_path.c_str());
}

// The muxer writes the m3u8 to a temporary file, then renames it over the m3u8, so a player never
// reads a partial one.
VOID TEST(HlsFilesTest, MuxerRefreshReplacesM3u8)
{
    srs_error_t err;

    MockHlsRequest req("test.vhost", "live", "livestream");
    SrsUniquePtr<SrsHlsMuxer> muxer(new SrsHlsMuxer());
    muxer->req_ = req.copy();
    muxer->m3u8_ = _srs_tmp_file_prefix + "refresh.m3u8";
    muxer->max_td_ = 10 * SRS_UTIME_SECONDS;
    ::unlink(muxer->m3u8_.c_str());

    // No segment, no m3u8.
    HELPER_EXPECT_SUCCESS(muxer->refresh_m3u8());
    EXPECT_FALSE(mock_file_exists(muxer->m3u8_));

    MockSrsFileWriter writer;
    SrsHlsSegment *segment = new SrsHlsSegment(muxer->context_, SrsAudioCodecIdAAC, SrsVideoCodecIdAVC, &writer);
    segment->sequence_no_ = 7;
    segment->uri_ = "livestream-7.ts";
    segment->append(0);
    segment->append(6000);
    muxer->segments_->append(segment);

    HELPER_EXPECT_SUCCESS(muxer->refresh_m3u8());
    string m3u8 = mock_read_file(muxer->m3u8_);
    EXPECT_TRUE(m3u8.find("#EXTM3U") == 0) << m3u8;
    EXPECT_TRUE(m3u8.find("#EXT-X-MEDIA-SEQUENCE:7") != string::npos) << m3u8;
    EXPECT_TRUE(m3u8.find("livestream-7.ts") != string::npos) << m3u8;
    EXPECT_FALSE(mock_file_exists(muxer->m3u8_ + ".temp"));

    ::unlink(muxer->m3u8_.c_str());
}

// Disposing goes on when the temporary segment or the m3u8 can not be deleted.
VOID TEST(HlsFilesTest, MuxerDisposeIgnoresUndeletableFiles)
{
    SrsUniquePtr<SrsHlsMuxer> muxer(new SrsHlsMuxer());
    MockSrsFileWriter *writer = new MockSrsFileWriter();
    muxer->writer_ = writer;

    // The m3u8 is a dir, which unlink does not delete.
    string dir = _srs_tmp_file_prefix + "dispose-m3u8-dir";
    SrsPath path;
    srs_error_t err = path.mkdir_all(dir);
    HELPER_EXPECT_SUCCESS(err);
    muxer->m3u8_ = dir;

    // The segment has no temporary file.
    muxer->current_ = new SrsHlsSegment(muxer->context_, SrsAudioCodecIdAAC, SrsVideoCodecIdAVC, writer);
    muxer->current_->set_path(_srs_tmp_file_prefix + "dispose-gone.ts");

    muxer->dispose();
    EXPECT_TRUE(muxer->current_ == NULL);
    EXPECT_TRUE(path.exists(dir));

    ::rmdir(dir.c_str());
}

// The muxers fail to publish when the dir of the m3u8 or of the keys can not be created.
VOID TEST(HlsFilesTest, CreateDirectoriesFailsUnderFile)
{
    string file = _srs_tmp_file_prefix + "hls-dirs-file";
    mock_touch_file(file);
    MockHlsRequest req("test.vhost", "live", "livestream");

    if (true) {
        SrsUniquePtr<SrsHlsMuxer> muxer(new SrsHlsMuxer());
        muxer->m3u8_ = file + "/live/livestream.m3u8";
        srs_error_t err = muxer->create_directories();
        HELPER_EXPECT_FAILED(err);

        muxer->m3u8_ = _srs_tmp_file_prefix + "hls-dirs.m3u8";
        muxer->req_ = req.copy();
        muxer->hls_keys_ = true;
        muxer->hls_path_ = srs_utest_tmp_dir();
        muxer->hls_key_file_path_ = file;
        muxer->hls_key_file_ = "[app]/[stream]-[seq].key";
        err = muxer->create_directories();
        HELPER_EXPECT_FAILED(err);
    }

    if (true) {
        SrsUniquePtr<SrsHlsFmp4Muxer> muxer(new SrsHlsFmp4Muxer());
        muxer->m3u8_ = file + "/live/livestream.m3u8";
        srs_error_t err = muxer->create_directories();
        HELPER_EXPECT_FAILED(err);

        muxer->m3u8_ = _srs_tmp_file_prefix + "hls-dirs.m3u8";
        muxer->req_ = req.copy();
        muxer->hls_keys_ = true;
        muxer->hls_path_ = srs_utest_tmp_dir();
        muxer->hls_key_file_path_ = file;
        muxer->hls_key_file_ = "[app]/[stream]-[seq].key";
        err = muxer->create_directories();
        HELPER_EXPECT_FAILED(err);

        // No segment, no m3u8.
        HELPER_EXPECT_SUCCESS(muxer->refresh_m3u8());
        EXPECT_FALSE(mock_file_exists(muxer->m3u8_));
    }

    ::unlink(file.c_str());
}

// A new sequence header in a stream marks the open segment, for a discontinuity in the m3u8.
VOID TEST(HlsFilesTest, ControllerMarksSequenceHeaderOnSegment)
{
    srs_error_t err;

    SrsUniquePtr<SrsHlsController> controller(new SrsHlsController());
    SrsHlsMuxer *muxer = dynamic_cast<SrsHlsMuxer *>(controller->muxer_);
    ASSERT_TRUE(muxer != NULL);

    // No open segment: nothing to mark.
    HELPER_EXPECT_SUCCESS(controller->on_sequence_header(NULL, NULL));

    MockSrsFileWriter writer;
    muxer->current_ = new SrsHlsSegment(muxer->context_, SrsAudioCodecIdAAC, SrsVideoCodecIdAVC, &writer);
    EXPECT_FALSE(muxer->current_->is_sequence_header());
    HELPER_EXPECT_SUCCESS(controller->on_sequence_header(NULL, NULL));
    EXPECT_TRUE(muxer->current_->is_sequence_header());

    srs_freep(muxer->current_);
}

// The MPD writer of a test, publishing to home.
class MockMpdWriterFixture
{
public:
    MockAppConfig config_;
    MockSrsRequest req_;
    SrsMpdWriter *writer_;
    SrsFragmentWindow afragments_;
    SrsFragmentWindow vfragments_;
    SrsFormat format_;

public:
    MockMpdWriterFixture(std::string home) : req_("test.vhost", "live", "livestream")
    {
        writer_ = new SrsMpdWriter();
        writer_->config_ = &config_;
        srs_error_t err = writer_->initialize(&req_);
        srs_freep(err);
        err = writer_->on_publish();
        srs_freep(err);
        writer_->home_ = home;

        SrsFragment *fragment = new SrsFragment();
        fragment->append(0);
        fragment->append(2000);
        vfragments_.append(fragment);
        format_.vcodec_ = new SrsVideoCodecConfig();
        format_.vcodec_->id_ = SrsVideoCodecIdAVC;
    }
    virtual ~MockMpdWriterFixture()
    {
        writer_->config_ = NULL;
        writer_->app_factory_ = NULL;
        srs_freep(writer_);
    }
    srs_error_t write()
    {
        return writer_->write(&format_, &afragments_, &vfragments_);
    }
};

// The MPD is not written when its dir can not be made, its temporary file can not be opened or
// written, or the temporary file can not be renamed to the MPD.
VOID TEST(DashFilesTest, MpdWriteFailures)
{
    srs_error_t err;

    string file = _srs_tmp_file_prefix + "dash-file";
    mock_touch_file(file);
    string home = _srs_tmp_file_prefix + "dash-home";
    string mpd = home + "/livestream.mpd";
    SrsPath path;

    // The home is under a file.
    if (true) {
        MockMpdWriterFixture f(file + "/dash");
        err = f.write();
        HELPER_EXPECT_FAILED(err);
        EXPECT_TRUE(f.writer_->mpd_path_.empty());
    }

    // The temporary file is a dir.
    if (true) {
        MockMpdWriterFixture f(home);
        HELPER_EXPECT_SUCCESS(path.mkdir_all(mpd + ".tmp"));
        err = f.write();
        HELPER_EXPECT_FAILED(err);
        EXPECT_TRUE(f.writer_->mpd_path_.empty());
        ::rmdir((mpd + ".tmp").c_str());
    }

    // The temporary file fails to write.
    if (true) {
        MockMpdWriterFixture f(home);
        MockAppFactory factory;
        factory.writer_error_offset_ = 0;
        f.writer_->app_factory_ = &factory;
        err = f.write();
        HELPER_EXPECT_FAILED(err);
        EXPECT_EQ(1, factory.create_file_writer_count_);
        EXPECT_TRUE(f.writer_->mpd_path_.empty());
    }

    // The MPD is a dir, which the temporary file can not replace.
    if (true) {
        MockMpdWriterFixture f(home);
        HELPER_EXPECT_SUCCESS(path.mkdir_all(mpd));
        err = f.write();
        EXPECT_EQ(ERROR_DASH_WRITE_FAILED, srs_error_code(err));
        srs_freep(err);
        EXPECT_TRUE(f.writer_->mpd_path_.empty());
        ::rmdir(mpd.c_str());
        ::unlink((mpd + ".tmp").c_str());
    }

    ::rmdir(home.c_str());
    ::unlink(file.c_str());
}

// Disposing an MPD that is already gone forgets it; the unpublish leaves the MPD to the dispose.
VOID TEST(DashFilesTest, MpdDisposeForgetsMissingMpd)
{
    srs_error_t err;

    string home = _srs_tmp_file_prefix + "dash-dispose-home";
    string mpd = home + "/livestream.mpd";
    MockMpdWriterFixture f(home);
    HELPER_EXPECT_SUCCESS(f.write());
    EXPECT_EQ(mpd, f.writer_->mpd_path_);

    f.writer_->on_unpublish();
    EXPECT_TRUE(mock_file_exists(mpd));

    ::unlink(mpd.c_str());
    f.writer_->dispose();
    EXPECT_TRUE(f.writer_->mpd_path_.empty());

    ::rmdir(home.c_str());
}

// The unpublish keeps the segments that fail to reap and goes on when the MPD fails to refresh.
VOID TEST(DashFilesTest, ControllerUnpublishIgnoresErrors)
{
    srs_error_t err;

    SrsUniquePtr<SrsDashController> controller(new SrsDashController());
    MockAppConfig config;
    MockAppFactory factory;
    MockSrsRequest req("test.vhost", "live", "livestream");
    HELPER_EXPECT_SUCCESS(controller->initialize(&req));
    controller->config_ = &config;
    controller->app_factory_ = &factory;
    MockMpdWriter *mpd = new MockMpdWriter();
    mpd->write_error_ = srs_error_new(ERROR_DASH_WRITE_FAILED, "mock mpd");
    srs_freep(controller->mpd_);
    controller->mpd_ = mpd;
    HELPER_EXPECT_SUCCESS(controller->on_publish());

    MockFragmentedMp4 *vcurrent = new MockFragmentedMp4();
    vcurrent->reap_error_ = srs_error_new(ERROR_SYSTEM_FRAGMENT_RENAME, "mock reap");
    vcurrent->duration_ = 1 * SRS_UTIME_SECONDS;
    MockFragmentedMp4 *acurrent = new MockFragmentedMp4();
    acurrent->reap_error_ = srs_error_new(ERROR_SYSTEM_FRAGMENT_RENAME, "mock reap");
    acurrent->duration_ = 1 * SRS_UTIME_SECONDS;
    controller->vcurrent_ = vcurrent;
    controller->acurrent_ = acurrent;
    SrsFormat format;
    controller->format_ = &format;

    controller->on_unpublish();
    EXPECT_TRUE(controller->vcurrent_ == NULL);
    EXPECT_TRUE(controller->acurrent_ == NULL);
    EXPECT_EQ(1, controller->vfragments_->size());
    EXPECT_EQ(1, controller->afragments_->size());
    EXPECT_EQ(1, mpd->write_count_);

    controller->format_ = NULL;
    controller->config_ = NULL;
    controller->app_factory_ = NULL;
}

// The dispose goes on when the temporary segments or the init files can not be deleted.
VOID TEST(DashFilesTest, ControllerDisposeIgnoresUndeletableFiles)
{
    srs_error_t err;

    SrsUniquePtr<SrsDashController> controller(new SrsDashController());
    MockSrsRequest req("test.vhost", "live", "livestream");
    HELPER_EXPECT_SUCCESS(controller->initialize(&req));

    MockFragmentedMp4 *vcurrent = new MockFragmentedMp4();
    vcurrent->unlink_tmpfile_error_ = srs_error_new(ERROR_SYSTEM_FILE_UNLINK, "mock unlink");
    MockFragmentedMp4 *acurrent = new MockFragmentedMp4();
    acurrent->unlink_tmpfile_error_ = srs_error_new(ERROR_SYSTEM_FILE_UNLINK, "mock unlink");
    controller->vcurrent_ = vcurrent;
    controller->acurrent_ = acurrent;

    // The init file is a dir, which unlink does not delete.
    string dir = _srs_tmp_file_prefix + "dash-init-dir";
    SrsPath path;
    HELPER_EXPECT_SUCCESS(path.mkdir_all(dir));
    controller->video_init_file_ = dir;

    controller->dispose();
    EXPECT_TRUE(vcurrent->unlink_tmpfile_called_);
    EXPECT_TRUE(acurrent->unlink_tmpfile_called_);
    EXPECT_TRUE(controller->video_init_file_.empty());
    EXPECT_TRUE(path.exists(dir));

    ::rmdir(dir.c_str());
}
