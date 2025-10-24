//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//
#include <srs_utest_ai23.hpp>

using namespace std;

#include <srs_app_config.hpp>
#ifdef SRS_GB28181
#include <srs_app_gb28181.hpp>
#endif
#include <srs_app_listener.hpp>
#include <srs_app_rtc_conn.hpp>
#include <srs_app_rtc_network.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_kbps.hpp>
#include <srs_kernel_ps.hpp>
#include <srs_kernel_ts.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_json.hpp>
#include <srs_protocol_raw_avc.hpp>
#include <srs_protocol_rtc_stun.hpp>
#include <srs_protocol_rtmp_conn.hpp>
#include <srs_protocol_sdp.hpp>
#include <srs_utest_ai16.hpp>
#include <srs_utest_manual_http.hpp>
#include <srs_utest_manual_kernel.hpp>
#include <srs_utest_manual_protocol.hpp>

// Mock ISrsGbMuxer implementation
MockGbMuxer::MockGbMuxer()
{
    setup_called_ = false;
    setup_output_ = "";
    on_ts_message_called_ = false;
    on_ts_message_error_ = srs_success;
}

MockGbMuxer::~MockGbMuxer()
{
}

void MockGbMuxer::setup(std::string output)
{
    setup_called_ = true;
    setup_output_ = output;
}

srs_error_t MockGbMuxer::on_ts_message(SrsTsMessage *msg)
{
    on_ts_message_called_ = true;
    return srs_error_copy(on_ts_message_error_);
}

void MockGbMuxer::reset()
{
    setup_called_ = false;
    setup_output_ = "";
    on_ts_message_called_ = false;
    srs_freep(on_ts_message_error_);
}

// Mock ISrsAppConfig implementation
MockAppConfigForGbSession::MockAppConfigForGbSession()
{
    stream_caster_output_ = "";
}

MockAppConfigForGbSession::~MockAppConfigForGbSession()
{
}

std::string MockAppConfigForGbSession::get_stream_caster_output(SrsConfDirective *conf)
{
    return stream_caster_output_;
}

void MockAppConfigForGbSession::set_stream_caster_output(const std::string &output)
{
    stream_caster_output_ = output;
}

// Test GB28181 session state conversion functions
VOID TEST(GB28181Test, SessionStateConversion)
{
    // Test srs_gb_session_state() for all valid states
    EXPECT_STREQ("Init", srs_gb_session_state(SrsGbSessionStateInit).c_str());
    EXPECT_STREQ("Connecting", srs_gb_session_state(SrsGbSessionStateConnecting).c_str());
    EXPECT_STREQ("Established", srs_gb_session_state(SrsGbSessionStateEstablished).c_str());
    EXPECT_STREQ("Invalid", srs_gb_session_state((SrsGbSessionState)999).c_str());

    // Test srs_gb_state() for state transitions
    EXPECT_STREQ("Init->Connecting", srs_gb_state(SrsGbSessionStateInit, SrsGbSessionStateConnecting).c_str());
    EXPECT_STREQ("Connecting->Established", srs_gb_state(SrsGbSessionStateConnecting, SrsGbSessionStateEstablished).c_str());
    EXPECT_STREQ("Established->Init", srs_gb_state(SrsGbSessionStateEstablished, SrsGbSessionStateInit).c_str());
}

// Test SrsGbSession setup and setup_owner methods
VOID TEST(GB28181Test, SessionSetupAndOwner)
{
    // Create mock dependencies
    SrsUniquePtr<MockAppConfigForGbSession> mock_config(new MockAppConfigForGbSession());
    MockGbMuxer *mock_muxer = new MockGbMuxer();

    // Create session
    SrsUniquePtr<SrsGbSession> session(new SrsGbSession());

    // Inject mock dependencies
    session->config_ = mock_config.get();
    session->muxer_ = mock_muxer;

    // Setup mock config to return test output
    mock_config->set_stream_caster_output("rtmp://127.0.0.1/live/test_stream");

    // Test setup() method
    SrsConfDirective *conf = NULL;
    session->setup(conf);

    // Verify muxer->setup() was called with correct output
    EXPECT_TRUE(mock_muxer->setup_called_);
    EXPECT_STREQ("rtmp://127.0.0.1/live/test_stream", mock_muxer->setup_output_.c_str());

    // Test setup_owner() method
    SrsSharedResource<ISrsGbSession> *wrapper = NULL;
    ISrsInterruptable *owner_coroutine = (ISrsInterruptable *)0x1234;
    ISrsContextIdSetter *owner_cid = (ISrsContextIdSetter *)0x5678;

    session->setup_owner(wrapper, owner_coroutine, owner_cid);

    // Verify owner fields are set correctly
    EXPECT_EQ(wrapper, session->wrapper_);
    EXPECT_EQ(owner_coroutine, session->owner_coroutine_);
    EXPECT_EQ(owner_cid, session->owner_cid_);

    // Test on_executor_done() method
    session->on_executor_done(NULL);

    // Verify owner_coroutine_ is cleared
    EXPECT_EQ((ISrsInterruptable *)NULL, session->owner_coroutine_);

    // Clean up - set to NULL to avoid double-free
    session->muxer_ = NULL;
    srs_freep(mock_muxer);
}

// Mock ISrsPackContext implementation
MockPackContext::MockPackContext()
{
}

MockPackContext::~MockPackContext()
{
}

srs_error_t MockPackContext::on_ts_message(SrsTsMessage *msg)
{
    return srs_success;
}

void MockPackContext::on_recover_mode(int nn_recover)
{
}

// Test SrsGbSession::on_ps_pack method - major use scenario
VOID TEST(GB28181Test, SessionOnPsPack)
{
    // Create mock dependencies
    SrsUniquePtr<MockAppConfigForGbSession> mock_config(new MockAppConfigForGbSession());
    MockGbMuxer *mock_muxer = new MockGbMuxer();

    // Create session
    SrsUniquePtr<SrsGbSession> session(new SrsGbSession());

    // Inject mock dependencies
    session->config_ = mock_config.get();
    session->muxer_ = mock_muxer;

    // Create mock pack context
    SrsUniquePtr<MockPackContext> ctx(new MockPackContext());
    ctx->media_id_ = 1;
    ctx->media_startime_ = srs_time_now_realtime();
    ctx->media_nn_recovered_ = 5;
    ctx->media_nn_msgs_dropped_ = 3;
    ctx->media_reserved_ = 10;

    // Create test messages: 2 video messages and 1 audio message
    std::vector<SrsTsMessage *> msgs;

    // Create first video message
    SrsUniquePtr<SrsTsMessage> video1(new SrsTsMessage());
    video1->sid_ = SrsTsPESStreamIdVideoCommon;
    video1->dts_ = 90000;
    video1->pts_ = 90000;
    video1->payload_->append("video1", 6);
    msgs.push_back(video1.get());

    // Create audio message
    SrsUniquePtr<SrsTsMessage> audio(new SrsTsMessage());
    audio->sid_ = SrsTsPESStreamIdAudioCommon;
    audio->dts_ = 90000;
    audio->pts_ = 90000;
    audio->payload_->append("audio1", 6);
    msgs.push_back(audio.get());

    // Create second video message
    SrsUniquePtr<SrsTsMessage> video2(new SrsTsMessage());
    video2->sid_ = SrsTsPESStreamIdVideoCommon;
    video2->dts_ = 90000;
    video2->pts_ = 90000;
    video2->payload_->append("video2", 6);
    msgs.push_back(video2.get());

    // Call on_ps_pack
    session->on_ps_pack(ctx.get(), NULL, msgs);

    // Verify statistics are updated correctly
    EXPECT_EQ(1u, ctx->media_id_);
    EXPECT_EQ(1u, session->media_id_);
    EXPECT_EQ(1u, session->media_packs_);
    EXPECT_EQ(3u, session->media_msgs_);
    EXPECT_EQ(5u, session->media_recovered_);
    EXPECT_EQ(3u, session->media_msgs_dropped_);
    EXPECT_EQ(10u, session->media_reserved_);

    // Verify muxer was called (should be called twice: once for audio, once for grouped video)
    EXPECT_TRUE(mock_muxer->on_ts_message_called_);

    // Reset mock for second test
    mock_muxer->reset();

    // Test context change (new media transport)
    SrsUniquePtr<MockPackContext> ctx2(new MockPackContext());
    ctx2->media_id_ = 2; // Different media_id
    ctx2->media_startime_ = srs_time_now_realtime();
    ctx2->media_nn_recovered_ = 2;
    ctx2->media_nn_msgs_dropped_ = 1;
    ctx2->media_reserved_ = 5;

    // Create new messages
    std::vector<SrsTsMessage *> msgs2;
    SrsUniquePtr<SrsTsMessage> video3(new SrsTsMessage());
    video3->sid_ = SrsTsPESStreamIdVideoCommon;
    video3->dts_ = 180000;
    video3->pts_ = 180000;
    video3->payload_->append("video3", 6);
    msgs2.push_back(video3.get());

    // Call on_ps_pack with new context
    session->on_ps_pack(ctx2.get(), NULL, msgs2);

    // Verify statistics are accumulated for old context
    EXPECT_EQ(1u, session->total_packs_);
    EXPECT_EQ(3u, session->total_msgs_);
    EXPECT_EQ(5u, session->total_recovered_);
    EXPECT_EQ(3u, session->total_msgs_dropped_);
    EXPECT_EQ(10u, session->total_reserved_);

    // Verify new context statistics
    EXPECT_EQ(2u, session->media_id_);
    EXPECT_EQ(1u, session->media_packs_);
    EXPECT_EQ(1u, session->media_msgs_);
    EXPECT_EQ(2u, session->media_recovered_);
    EXPECT_EQ(1u, session->media_msgs_dropped_);
    EXPECT_EQ(5u, session->media_reserved_);

    // Clean up - set to NULL to avoid double-free
    session->muxer_ = NULL;
    srs_freep(mock_muxer);
}

// Mock ISrsGbMediaTcpConn implementation
MockGbMediaTcpConn::MockGbMediaTcpConn()
{
    set_cid_called_ = false;
    is_connected_ = false;
}

MockGbMediaTcpConn::~MockGbMediaTcpConn()
{
}

void MockGbMediaTcpConn::setup(srs_netfd_t stfd)
{
}

void MockGbMediaTcpConn::setup_owner(SrsSharedResource<ISrsGbMediaTcpConn> *wrapper, ISrsInterruptable *owner_coroutine, ISrsContextIdSetter *owner_cid)
{
}

bool MockGbMediaTcpConn::is_connected()
{
    return is_connected_;
}

void MockGbMediaTcpConn::interrupt()
{
}

void MockGbMediaTcpConn::set_cid(const SrsContextId &cid)
{
    set_cid_called_ = true;
    received_cid_ = cid;
}

const SrsContextId &MockGbMediaTcpConn::get_id()
{
    return received_cid_;
}

std::string MockGbMediaTcpConn::desc()
{
    return "MockGbMediaTcpConn";
}

srs_error_t MockGbMediaTcpConn::cycle()
{
    return srs_success;
}

void MockGbMediaTcpConn::on_executor_done(ISrsInterruptable *executor)
{
}

void MockGbMediaTcpConn::reset()
{
    set_cid_called_ = false;
    received_cid_ = SrsContextId();
}

// Test SrsGbSession::on_media_transport - covers the major use scenario:
// 1. Session receives a media transport connection
// 2. Session stores the media transport reference
// 3. Session propagates its context ID to the media transport via set_cid()
VOID TEST(GB28181Test, SessionOnMediaTransport)
{
    // Create mock dependencies
    SrsUniquePtr<MockAppConfigForGbSession> mock_config(new MockAppConfigForGbSession());
    MockGbMuxer *mock_muxer = new MockGbMuxer();

    // Create session
    SrsUniquePtr<SrsGbSession> session(new SrsGbSession());

    // Inject mock dependencies
    session->config_ = mock_config.get();
    session->muxer_ = mock_muxer;

    // Set a specific context ID for the session
    SrsContextId session_cid;
    session_cid.set_value("test-session-123");
    session->cid_ = session_cid;

    // Create mock media transport wrapped in SrsSharedResource
    MockGbMediaTcpConn *mock_media = new MockGbMediaTcpConn();
    SrsSharedResource<ISrsGbMediaTcpConn> media_resource(mock_media);

    // Call on_media_transport
    session->on_media_transport(media_resource);

    // Verify that set_cid was called on the media transport
    EXPECT_TRUE(mock_media->set_cid_called_);

    // Verify that the session's context ID was passed to the media transport
    EXPECT_EQ(0, mock_media->received_cid_.compare(session_cid));

    // Clean up - set to NULL to avoid double-free
    session->muxer_ = NULL;
    srs_freep(mock_muxer);
}

// Test SrsGbSession::drive_state - covers the major use scenario:
// 1. Session starts in Init state with media disconnected
// 2. When media connects, session transitions to Established state
// 3. When media disconnects, session transitions back to Init state
VOID TEST(GB28181Test, SessionDriveState)
{
    srs_error_t err;

    // Create mock dependencies
    SrsUniquePtr<MockAppConfigForGbSession> mock_config(new MockAppConfigForGbSession());
    MockGbMuxer *mock_muxer = new MockGbMuxer();
    MockGbMediaTcpConn *mock_media = new MockGbMediaTcpConn();

    // Create session
    SrsUniquePtr<SrsGbSession> session(new SrsGbSession());

    // Inject mock dependencies
    session->config_ = mock_config.get();
    session->muxer_ = mock_muxer;
    session->media_ = SrsSharedResource<ISrsGbMediaTcpConn>(mock_media);
    session->device_id_ = "test-device-123";

    // Verify initial state is Init
    EXPECT_EQ(SrsGbSessionStateInit, session->state_);

    // Test 1: Init state with media disconnected - should remain in Init
    mock_media->is_connected_ = false;
    HELPER_EXPECT_SUCCESS(session->drive_state());
    EXPECT_EQ(SrsGbSessionStateInit, session->state_);

    // Test 2: Init state with media connected - should transition to Established
    mock_media->is_connected_ = true;
    HELPER_EXPECT_SUCCESS(session->drive_state());
    EXPECT_EQ(SrsGbSessionStateEstablished, session->state_);

    // Test 3: Established state with media still connected - should remain in Established
    mock_media->is_connected_ = true;
    HELPER_EXPECT_SUCCESS(session->drive_state());
    EXPECT_EQ(SrsGbSessionStateEstablished, session->state_);

    // Test 4: Established state with media disconnected - should transition back to Init
    mock_media->is_connected_ = false;
    HELPER_EXPECT_SUCCESS(session->drive_state());
    EXPECT_EQ(SrsGbSessionStateInit, session->state_);

    // Clean up - set to NULL to avoid double-free
    session->muxer_ = NULL;
    srs_freep(mock_muxer);
}

// Mock ISrsIpListener implementation
MockIpListener::MockIpListener()
{
    endpoint_ip_ = "";
    endpoint_port_ = 0;
    label_ = "";
    set_endpoint_called_ = false;
    set_label_called_ = false;
}

MockIpListener::~MockIpListener()
{
}

ISrsListener *MockIpListener::set_endpoint(const std::string &i, int p)
{
    endpoint_ip_ = i;
    endpoint_port_ = p;
    set_endpoint_called_ = true;
    return this;
}

ISrsListener *MockIpListener::set_label(const std::string &label)
{
    label_ = label;
    set_label_called_ = true;
    return this;
}

srs_error_t MockIpListener::listen()
{
    return srs_success;
}

void MockIpListener::close()
{
}

void MockIpListener::reset()
{
    endpoint_ip_ = "";
    endpoint_port_ = 0;
    label_ = "";
    set_endpoint_called_ = false;
    set_label_called_ = false;
}

// Mock ISrsAppConfig for GbListener implementation
MockAppConfigForGbListener::MockAppConfigForGbListener()
{
    stream_caster_listen_port_ = 0;
}

MockAppConfigForGbListener::~MockAppConfigForGbListener()
{
}

int MockAppConfigForGbListener::get_stream_caster_listen(SrsConfDirective *conf)
{
    return stream_caster_listen_port_;
}

// Test SrsGbListener::initialize - covers the major use scenario:
// 1. Listener receives a configuration directive
// 2. Listener copies the configuration
// 3. Listener retrieves the listen port from config
// 4. Listener sets the endpoint (IP and port) on the media listener
// 5. Listener sets the label on the media listener
VOID TEST(GB28181Test, ListenerInitialize)
{
    srs_error_t err = srs_success;

    // Create mock dependencies
    SrsUniquePtr<MockAppConfigForGbListener> mock_config(new MockAppConfigForGbListener());
    MockIpListener *mock_listener = new MockIpListener();

    // Setup mock config to return test port
    mock_config->stream_caster_listen_port_ = 9000;

    // Create listener
    SrsUniquePtr<SrsGbListener> listener(new SrsGbListener());

    // Inject mock dependencies
    listener->config_ = mock_config.get();
    listener->media_listener_ = mock_listener;

    // Create test configuration directive
    SrsUniquePtr<SrsConfDirective> conf(new SrsConfDirective());
    conf->name_ = "stream_caster";
    conf->args_.push_back("gb28181");

    // Add listen directive
    SrsConfDirective *listen_directive = new SrsConfDirective();
    listen_directive->name_ = "listen";
    listen_directive->args_.push_back("9000");
    conf->directives_.push_back(listen_directive);

    // Test initialize() method
    HELPER_EXPECT_SUCCESS(listener->initialize(conf.get()));

    // Verify configuration was copied
    EXPECT_TRUE(listener->conf_ != NULL);
    EXPECT_STREQ("stream_caster", listener->conf_->name_.c_str());

    // Verify set_endpoint was called with correct parameters
    EXPECT_TRUE(mock_listener->set_endpoint_called_);
    EXPECT_STREQ("0.0.0.0", mock_listener->endpoint_ip_.c_str());
    EXPECT_EQ(9000, mock_listener->endpoint_port_);

    // Verify set_label was called with correct label
    EXPECT_TRUE(mock_listener->set_label_called_);
    EXPECT_STREQ("GB-TCP", mock_listener->label_.c_str());

    // Clean up - set to NULL to avoid double-free
    listener->media_listener_ = NULL;
    srs_freep(mock_listener);
}

// Mock ISrsCommonHttpHandler implementation
MockHttpServeMuxForGbListener::MockHttpServeMuxForGbListener()
{
    handle_called_ = false;
    handle_pattern_ = "";
    handle_handler_ = NULL;
}

MockHttpServeMuxForGbListener::~MockHttpServeMuxForGbListener()
{
}

srs_error_t MockHttpServeMuxForGbListener::handle(std::string pattern, ISrsHttpHandler *handler)
{
    handle_called_ = true;
    handle_pattern_ = pattern;
    handle_handler_ = handler;
    return srs_success;
}

srs_error_t MockHttpServeMuxForGbListener::serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r)
{
    return srs_success;
}

void MockHttpServeMuxForGbListener::reset()
{
    handle_called_ = false;
    handle_pattern_ = "";
    handle_handler_ = NULL;
}

// Mock ISrsApiServerOwner implementation
MockApiServerOwnerForGbListener::MockApiServerOwnerForGbListener()
{
    mux_ = NULL;
}

MockApiServerOwnerForGbListener::~MockApiServerOwnerForGbListener()
{
    mux_ = NULL;
}

ISrsCommonHttpHandler *MockApiServerOwnerForGbListener::api_server()
{
    return mux_;
}

// Mock ISrsIpListener implementation
MockIpListenerForGbListen::MockIpListenerForGbListen()
{
    listen_called_ = false;
}

MockIpListenerForGbListen::~MockIpListenerForGbListen()
{
}

ISrsListener *MockIpListenerForGbListen::set_endpoint(const std::string &i, int p)
{
    return this;
}

ISrsListener *MockIpListenerForGbListen::set_label(const std::string &label)
{
    return this;
}

srs_error_t MockIpListenerForGbListen::listen()
{
    listen_called_ = true;
    return srs_success;
}

void MockIpListenerForGbListen::close()
{
}

void MockIpListenerForGbListen::reset()
{
    listen_called_ = false;
}

// Test SrsGbListener::listen - covers the major use scenario:
// 1. Listener calls media_listener_->listen() to start listening for TCP connections
// 2. Listener calls listen_api() to register HTTP API handlers
// 3. listen_api() retrieves the HTTP mux from api_server_owner_
// 4. listen_api() registers the /gb/v1/publish/ endpoint with the mux
VOID TEST(GB28181Test, ListenerListen)
{
    srs_error_t err;

    // Create mock dependencies
    MockIpListenerForGbListen *mock_media_listener = new MockIpListenerForGbListen();
    SrsUniquePtr<MockHttpServeMuxForGbListener> mock_mux(new MockHttpServeMuxForGbListener());
    SrsUniquePtr<MockApiServerOwnerForGbListener> mock_api_owner(new MockApiServerOwnerForGbListener());

    // Setup mock api_server_owner to return mock mux
    mock_api_owner->mux_ = mock_mux.get();

    // Create listener
    SrsUniquePtr<SrsGbListener> listener(new SrsGbListener());

    // Inject mock dependencies
    listener->media_listener_ = mock_media_listener;
    listener->api_server_owner_ = mock_api_owner.get();

    // Create test configuration directive
    SrsUniquePtr<SrsConfDirective> conf(new SrsConfDirective());
    conf->name_ = "stream_caster";
    conf->args_.push_back("gb28181");
    listener->conf_ = conf->copy();

    // Test listen() method
    HELPER_EXPECT_SUCCESS(listener->listen());

    // Verify media_listener_->listen() was called
    EXPECT_TRUE(mock_media_listener->listen_called_);

    // Verify mux->handle() was called with correct pattern
    EXPECT_TRUE(mock_mux->handle_called_);
    EXPECT_STREQ("/gb/v1/publish/", mock_mux->handle_pattern_.c_str());
    EXPECT_TRUE(mock_mux->handle_handler_ != NULL);

    // Clean up - set to NULL to avoid double-free
    listener->media_listener_ = NULL;
    srs_freep(mock_media_listener);
}

// Mock ISrsInterruptable for testing SrsGbMediaTcpConn::setup_owner
class MockInterruptableForGbMediaTcpConn : public ISrsInterruptable
{
public:
    bool interrupt_called_;
    srs_error_t pull_error_;

public:
    MockInterruptableForGbMediaTcpConn()
    {
        interrupt_called_ = false;
        pull_error_ = srs_success;
    }
    virtual ~MockInterruptableForGbMediaTcpConn()
    {
        srs_freep(pull_error_);
    }

public:
    virtual void interrupt()
    {
        interrupt_called_ = true;
    }
    virtual srs_error_t pull()
    {
        return srs_error_copy(pull_error_);
    }
};

// Mock ISrsContextIdSetter for testing SrsGbMediaTcpConn::setup_owner
class MockContextIdSetterForGbMediaTcpConn : public ISrsContextIdSetter
{
public:
    bool set_cid_called_;
    SrsContextId received_cid_;

public:
    MockContextIdSetterForGbMediaTcpConn()
    {
        set_cid_called_ = false;
    }
    virtual ~MockContextIdSetterForGbMediaTcpConn()
    {
    }

public:
    virtual void set_cid(const SrsContextId &cid)
    {
        set_cid_called_ = true;
        received_cid_ = cid;
    }
};

// Test SrsGbMediaTcpConn::setup_owner, on_executor_done, is_connected, set_cid, get_id, desc
// This test covers the major use scenario:
// 1. Create SrsGbMediaTcpConn and setup owner with wrapper, coroutine, and cid setter
// 2. Verify is_connected() returns false initially
// 3. Call set_cid() and verify it propagates to owner_cid_
// 4. Verify get_id() returns the correct context id
// 5. Verify desc() returns correct description
// 6. Call on_executor_done() and verify owner_coroutine_ is cleared
VOID TEST(GbMediaTcpConnTest, SetupOwnerAndLifecycle)
{
    // Create mock dependencies
    SrsUniquePtr<MockInterruptableForGbMediaTcpConn> mock_coroutine(new MockInterruptableForGbMediaTcpConn());
    SrsUniquePtr<MockContextIdSetterForGbMediaTcpConn> mock_cid_setter(new MockContextIdSetterForGbMediaTcpConn());

    // Create SrsGbMediaTcpConn - wrapper will own it
    SrsGbMediaTcpConn *conn = new SrsGbMediaTcpConn();

    // Create a wrapper that owns the conn
    SrsUniquePtr<SrsSharedResource<ISrsGbMediaTcpConn> > wrapper(new SrsSharedResource<ISrsGbMediaTcpConn>(conn));

    // Test setup_owner
    conn->setup_owner(wrapper.get(), mock_coroutine.get(), mock_cid_setter.get());

    // Verify is_connected() returns false initially
    EXPECT_FALSE(conn->is_connected());

    // Test set_cid() - should propagate to owner_cid_
    SrsContextId test_cid;
    conn->set_cid(test_cid);

    // Verify owner_cid_->set_cid() was called
    EXPECT_TRUE(mock_cid_setter->set_cid_called_);
    EXPECT_EQ(0, test_cid.compare(mock_cid_setter->received_cid_));

    // Test get_id() - should return the cid we set
    const SrsContextId &returned_cid = conn->get_id();
    EXPECT_EQ(0, test_cid.compare(returned_cid));

    // Test desc() - should return "GB-Media-TCP"
    EXPECT_STREQ("GB-Media-TCP", conn->desc().c_str());

    // Test on_executor_done() - should clear owner_coroutine_
    conn->on_executor_done(mock_coroutine.get());
    // After on_executor_done, owner_coroutine_ should be NULL
    // We can't directly verify this, but we can verify the method doesn't crash

    // wrapper will automatically clean up conn when it goes out of scope
}

// Mock ISrsGbSession implementation
MockGbSessionForMediaConn::MockGbSessionForMediaConn()
{
    on_ps_pack_called_ = false;
    received_pack_ = NULL;
    received_ps_ = NULL;
    on_media_transport_called_ = false;
}

MockGbSessionForMediaConn::~MockGbSessionForMediaConn()
{
}

void MockGbSessionForMediaConn::setup(SrsConfDirective *conf)
{
}

void MockGbSessionForMediaConn::setup_owner(SrsSharedResource<ISrsGbSession> *wrapper, ISrsInterruptable *owner_coroutine, ISrsContextIdSetter *owner_cid)
{
}

void MockGbSessionForMediaConn::on_media_transport(SrsSharedResource<ISrsGbMediaTcpConn> media)
{
    on_media_transport_called_ = true;
    received_media_ = media;
}

void MockGbSessionForMediaConn::on_ps_pack(ISrsPackContext *ctx, SrsPsPacket *ps, const std::vector<SrsTsMessage *> &msgs)
{
    on_ps_pack_called_ = true;
    received_pack_ = ctx;
    received_ps_ = ps;
    received_msgs_ = msgs;
}

const SrsContextId &MockGbSessionForMediaConn::get_id()
{
    static SrsContextId cid;
    return cid;
}

std::string MockGbSessionForMediaConn::desc()
{
    return "MockGbSessionForMediaConn";
}

srs_error_t MockGbSessionForMediaConn::cycle()
{
    return srs_success;
}

void MockGbSessionForMediaConn::on_executor_done(ISrsInterruptable *executor)
{
}

void MockGbSessionForMediaConn::reset()
{
    on_ps_pack_called_ = false;
    received_pack_ = NULL;
    received_ps_ = NULL;
    received_msgs_.clear();
    on_media_transport_called_ = false;
}

// Test SrsGbMediaTcpConn::on_ps_pack - covers the major use scenario:
// 1. Media connection receives PS pack with messages
// 2. Connection state changes from disconnected to connected
// 3. Session is notified about the PS pack via on_ps_pack callback
VOID TEST(GbMediaTcpConnTest, OnPsPack)
{
    // Create mock dependencies
    MockGbSessionForMediaConn *mock_session = new MockGbSessionForMediaConn();
    MockPackContext *mock_pack = new MockPackContext();

    // Create SrsGbMediaTcpConn - wrapper will own it
    SrsGbMediaTcpConn *conn = new SrsGbMediaTcpConn();

    // Create a wrapper that owns the conn
    SrsUniquePtr<SrsSharedResource<ISrsGbMediaTcpConn> > wrapper(new SrsSharedResource<ISrsGbMediaTcpConn>(conn));

    // Inject mock dependencies
    conn->session_ = mock_session;
    conn->pack_ = mock_pack;

    // Verify initial state - not connected
    EXPECT_FALSE(conn->is_connected());

    // Create test messages: 1 video message and 1 audio message
    std::vector<SrsTsMessage *> msgs;

    // Create video message
    SrsUniquePtr<SrsTsMessage> video(new SrsTsMessage());
    video->sid_ = SrsTsPESStreamIdVideoCommon;
    video->dts_ = 90000;
    video->pts_ = 90000;
    video->payload_->append("video_data", 10);
    msgs.push_back(video.get());

    // Create audio message
    SrsUniquePtr<SrsTsMessage> audio(new SrsTsMessage());
    audio->sid_ = SrsTsPESStreamIdAudioCommon;
    audio->dts_ = 90000;
    audio->pts_ = 90000;
    audio->payload_->append("audio_data", 10);
    msgs.push_back(audio.get());

    // Call on_ps_pack
    srs_error_t err = conn->on_ps_pack(NULL, msgs);
    HELPER_EXPECT_SUCCESS(err);

    // Verify connection state changed to connected
    EXPECT_TRUE(conn->is_connected());

    // Verify session->on_ps_pack was called
    EXPECT_TRUE(mock_session->on_ps_pack_called_);
    EXPECT_EQ(mock_pack, mock_session->received_pack_);
    EXPECT_EQ(NULL, mock_session->received_ps_);
    EXPECT_EQ(2u, mock_session->received_msgs_.size());

    // Clean up - set to NULL to avoid double-free
    // Note: conn destructor will free pack_ and session_, so we set them to NULL
    conn->session_ = NULL;
    conn->pack_ = NULL;
    srs_freep(mock_session);
    srs_freep(mock_pack);
}

// Test SrsGbMediaTcpConn::bind_session - covers the major use scenario:
// 1. Media connection binds to an existing session by SSRC
// 2. Session is found in the resource manager by fast_id (SSRC)
// 3. Session's on_media_transport is called with the media connection wrapper
// 4. The session pointer is returned to the caller
VOID TEST(GbMediaTcpConnTest, BindSession)
{
    srs_error_t err = srs_success;

    // Create mock session wrapped in SrsSharedResource
    MockGbSessionForMediaConn *mock_session = new MockGbSessionForMediaConn();
    SrsUniquePtr<SrsSharedResource<ISrsGbSession> > session_wrapper(new SrsSharedResource<ISrsGbSession>(mock_session));

    // Create mock resource manager
    SrsUniquePtr<MockResourceManagerForBindSession> mock_manager(new MockResourceManagerForBindSession());
    mock_manager->session_to_return_ = session_wrapper.get();

    // Create SrsGbMediaTcpConn - wrapper will own it
    SrsGbMediaTcpConn *conn = new SrsGbMediaTcpConn();
    SrsUniquePtr<SrsSharedResource<ISrsGbMediaTcpConn> > wrapper(new SrsSharedResource<ISrsGbMediaTcpConn>(conn));

    // Inject mock dependencies
    conn->gb_manager_ = mock_manager.get();
    conn->wrapper_ = wrapper.get();

    // Test bind_session with valid SSRC
    uint32_t ssrc = 12345;
    ISrsGbSession *bound_session = NULL;
    err = conn->bind_session(ssrc, &bound_session);
    HELPER_EXPECT_SUCCESS(err);

    // Verify session was found and bound
    EXPECT_TRUE(bound_session != NULL);
    EXPECT_EQ(mock_session, bound_session);

    // Verify session's on_media_transport was called with the wrapper
    EXPECT_TRUE(mock_session->on_media_transport_called_);
    EXPECT_EQ(conn, mock_session->received_media_.get());

    // Test bind_session with zero SSRC (should return success but do nothing)
    mock_session->reset();
    bound_session = NULL;
    err = conn->bind_session(0, &bound_session);
    HELPER_EXPECT_SUCCESS(err);
    EXPECT_TRUE(bound_session == NULL);
    EXPECT_FALSE(mock_session->on_media_transport_called_);

    // Test bind_session when session not found (should return success but do nothing)
    mock_session->reset();
    mock_manager->session_to_return_ = NULL;
    bound_session = NULL;
    err = conn->bind_session(ssrc, &bound_session);
    HELPER_EXPECT_SUCCESS(err);
    EXPECT_TRUE(bound_session == NULL);
    EXPECT_FALSE(mock_session->on_media_transport_called_);

    // Clean up - set to NULL to avoid double-free
    conn->gb_manager_ = NULL;
    conn->wrapper_ = NULL;
}

// Test SrsMpegpsQueue::push - covers the major use scenario:
// 1. Push audio and video packets with unique timestamps
// 2. Handle timestamp collision by adjusting timestamp (+1ms)
// 3. Verify audio/video counters are updated correctly
VOID TEST(MpegpsQueueTest, PushMediaPackets)
{
    srs_error_t err = srs_success;

    // Create SrsMpegpsQueue
    SrsUniquePtr<SrsMpegpsQueue> queue(new SrsMpegpsQueue());

    // Test 1: Push video packet with unique timestamp
    SrsMediaPacket *video1 = new SrsMediaPacket();
    video1->timestamp_ = 1000;
    video1->message_type_ = SrsFrameTypeVideo;
    char *video_data1 = new char[10];
    memset(video_data1, 0x01, 10);
    video1->wrap(video_data1, 10);

    err = queue->push(video1);
    HELPER_EXPECT_SUCCESS(err);
    EXPECT_EQ(1, queue->nb_videos_);
    EXPECT_EQ(0, queue->nb_audios_);
    EXPECT_EQ(1u, queue->msgs_.size());
    EXPECT_EQ(video1, queue->msgs_[1000]);

    // Test 2: Push audio packet with unique timestamp
    SrsMediaPacket *audio1 = new SrsMediaPacket();
    audio1->timestamp_ = 1020;
    audio1->message_type_ = SrsFrameTypeAudio;
    char *audio_data1 = new char[8];
    memset(audio_data1, 0xAF, 8);
    audio1->wrap(audio_data1, 8);

    err = queue->push(audio1);
    HELPER_EXPECT_SUCCESS(err);
    EXPECT_EQ(1, queue->nb_videos_);
    EXPECT_EQ(1, queue->nb_audios_);
    EXPECT_EQ(2u, queue->msgs_.size());
    EXPECT_EQ(audio1, queue->msgs_[1020]);

    // Test 3: Push video packet with timestamp collision - should adjust to 1001
    SrsMediaPacket *video2 = new SrsMediaPacket();
    video2->timestamp_ = 1000; // Same as video1
    video2->message_type_ = SrsFrameTypeVideo;
    char *video_data2 = new char[10];
    memset(video_data2, 0x02, 10);
    video2->wrap(video_data2, 10);

    err = queue->push(video2);
    HELPER_EXPECT_SUCCESS(err);
    EXPECT_EQ(2, queue->nb_videos_);
    EXPECT_EQ(1, queue->nb_audios_);
    EXPECT_EQ(3u, queue->msgs_.size());
    // Timestamp should be adjusted to 1001
    EXPECT_EQ(1001, video2->timestamp_);
    EXPECT_EQ(video2, queue->msgs_[1001]);

    // Test 4: Push audio packet with timestamp collision - should adjust to 1021
    SrsMediaPacket *audio2 = new SrsMediaPacket();
    audio2->timestamp_ = 1020; // Same as audio1
    audio2->message_type_ = SrsFrameTypeAudio;
    char *audio_data2 = new char[8];
    memset(audio_data2, 0xAE, 8);
    audio2->wrap(audio_data2, 8);

    err = queue->push(audio2);
    HELPER_EXPECT_SUCCESS(err);
    EXPECT_EQ(2, queue->nb_videos_);
    EXPECT_EQ(2, queue->nb_audios_);
    EXPECT_EQ(4u, queue->msgs_.size());
    // Timestamp should be adjusted to 1021
    EXPECT_EQ(1021, audio2->timestamp_);
    EXPECT_EQ(audio2, queue->msgs_[1021]);

    // Test 5: Push video packet with multiple collisions - should adjust to 1002
    SrsMediaPacket *video3 = new SrsMediaPacket();
    video3->timestamp_ = 1000; // Will collide with 1000 and 1001
    video3->message_type_ = SrsFrameTypeVideo;
    char *video_data3 = new char[10];
    memset(video_data3, 0x03, 10);
    video3->wrap(video_data3, 10);

    err = queue->push(video3);
    HELPER_EXPECT_SUCCESS(err);
    EXPECT_EQ(3, queue->nb_videos_);
    EXPECT_EQ(2, queue->nb_audios_);
    EXPECT_EQ(5u, queue->msgs_.size());
    // Timestamp should be adjusted to 1002
    EXPECT_EQ(1002, video3->timestamp_);
    EXPECT_EQ(video3, queue->msgs_[1002]);

    // Verify all packets are in the queue with correct timestamps
    EXPECT_EQ(video1, queue->msgs_[1000]);
    EXPECT_EQ(video2, queue->msgs_[1001]);
    EXPECT_EQ(video3, queue->msgs_[1002]);
    EXPECT_EQ(audio1, queue->msgs_[1020]);
    EXPECT_EQ(audio2, queue->msgs_[1021]);
}

// Test SrsMpegpsQueue::dequeue - covers the major use scenario:
// 1. Dequeue returns NULL when there are insufficient packets (< 2 videos or < 2 audios)
// 2. Dequeue returns packets in timestamp order when there are 2+ videos and 2+ audios
// 3. Verify audio/video counters are decremented correctly
// 4. Verify packets are removed from the queue in correct order
VOID TEST(MpegpsQueueTest, DequeueMediaPackets)
{
    srs_error_t err = srs_success;

    // Create SrsMpegpsQueue
    SrsUniquePtr<SrsMpegpsQueue> queue(new SrsMpegpsQueue());

    // Test 1: Dequeue returns NULL when queue is empty
    SrsMediaPacket *msg = queue->dequeue();
    EXPECT_TRUE(msg == NULL);

    // Test 2: Push 1 video and 1 audio - should not dequeue (need 2+ of each)
    SrsMediaPacket *video1 = new SrsMediaPacket();
    video1->timestamp_ = 1000;
    video1->message_type_ = SrsFrameTypeVideo;
    char *video_data1 = new char[10];
    memset(video_data1, 0x01, 10);
    video1->wrap(video_data1, 10);
    err = queue->push(video1);
    HELPER_EXPECT_SUCCESS(err);

    SrsMediaPacket *audio1 = new SrsMediaPacket();
    audio1->timestamp_ = 1020;
    audio1->message_type_ = SrsFrameTypeAudio;
    char *audio_data1 = new char[8];
    memset(audio_data1, 0xAF, 8);
    audio1->wrap(audio_data1, 8);
    err = queue->push(audio1);
    HELPER_EXPECT_SUCCESS(err);

    // Should return NULL - need 2+ videos and 2+ audios
    msg = queue->dequeue();
    EXPECT_TRUE(msg == NULL);
    EXPECT_EQ(1, queue->nb_videos_);
    EXPECT_EQ(1, queue->nb_audios_);

    // Test 3: Push another video and audio - now should dequeue
    SrsMediaPacket *video2 = new SrsMediaPacket();
    video2->timestamp_ = 1040;
    video2->message_type_ = SrsFrameTypeVideo;
    char *video_data2 = new char[10];
    memset(video_data2, 0x02, 10);
    video2->wrap(video_data2, 10);
    err = queue->push(video2);
    HELPER_EXPECT_SUCCESS(err);

    SrsMediaPacket *audio2 = new SrsMediaPacket();
    audio2->timestamp_ = 1060;
    audio2->message_type_ = SrsFrameTypeAudio;
    char *audio_data2 = new char[8];
    memset(audio_data2, 0xAE, 8);
    audio2->wrap(audio_data2, 8);
    err = queue->push(audio2);
    HELPER_EXPECT_SUCCESS(err);

    // Now we have 2 videos and 2 audios - should dequeue in timestamp order
    EXPECT_EQ(2, queue->nb_videos_);
    EXPECT_EQ(2, queue->nb_audios_);
    EXPECT_EQ(4u, queue->msgs_.size());

    // Test 4: Dequeue first packet (video1 at timestamp 1000)
    msg = queue->dequeue();
    EXPECT_TRUE(msg != NULL);
    EXPECT_EQ(1000, msg->timestamp_);
    EXPECT_TRUE(msg->is_video());
    EXPECT_EQ(1, queue->nb_videos_);
    EXPECT_EQ(2, queue->nb_audios_);
    EXPECT_EQ(3u, queue->msgs_.size());
    srs_freep(msg);

    // Test 5: After first dequeue, we have 1 video and 2 audios - should return NULL
    msg = queue->dequeue();
    EXPECT_TRUE(msg == NULL);
    EXPECT_EQ(1, queue->nb_videos_);
    EXPECT_EQ(2, queue->nb_audios_);
    EXPECT_EQ(3u, queue->msgs_.size());

    // Test 6: Push more videos to reach 2+ videos again
    SrsMediaPacket *video3 = new SrsMediaPacket();
    video3->timestamp_ = 1080;
    video3->message_type_ = SrsFrameTypeVideo;
    char *video_data3 = new char[10];
    memset(video_data3, 0x03, 10);
    video3->wrap(video_data3, 10);
    err = queue->push(video3);
    HELPER_EXPECT_SUCCESS(err);

    // Now we have 2 videos and 2 audios again - should dequeue
    EXPECT_EQ(2, queue->nb_videos_);
    EXPECT_EQ(2, queue->nb_audios_);
    EXPECT_EQ(4u, queue->msgs_.size());

    // Dequeue second packet (audio1 at timestamp 1020)
    msg = queue->dequeue();
    EXPECT_TRUE(msg != NULL);
    EXPECT_EQ(1020, msg->timestamp_);
    EXPECT_TRUE(msg->is_audio());
    EXPECT_EQ(2, queue->nb_videos_);
    EXPECT_EQ(1, queue->nb_audios_);
    EXPECT_EQ(3u, queue->msgs_.size());
    srs_freep(msg);
}

// Mock ISrsRawAacStream implementation
MockGbRawAacStream::MockGbRawAacStream()
{
    adts_demux_called_ = false;
    mux_sequence_header_called_ = false;
    mux_aac2flv_called_ = false;
    adts_demux_error_ = srs_success;
    mux_sequence_header_error_ = srs_success;
    mux_aac2flv_error_ = srs_success;
    sequence_header_output_ = "\xAF\x00\x12\x10"; // AAC sequence header
    demux_frame_size_ = 100;
}

MockGbRawAacStream::~MockGbRawAacStream()
{
    srs_freep(adts_demux_error_);
    srs_freep(mux_sequence_header_error_);
    srs_freep(mux_aac2flv_error_);
}

srs_error_t MockGbRawAacStream::adts_demux(SrsBuffer *stream, char **pframe, int *pnb_frame, SrsRawAacStreamCodec &codec)
{
    adts_demux_called_ = true;

    if (adts_demux_error_ != srs_success) {
        return srs_error_copy(adts_demux_error_);
    }

    // Simulate demuxing AAC frame
    if (!stream->empty()) {
        *pframe = stream->data() + stream->pos();
        *pnb_frame = srs_min(demux_frame_size_, stream->left());
        stream->skip(*pnb_frame);

        // Set codec info
        codec.aac_object_ = SrsAacObjectTypeAacLC;
        codec.sampling_frequency_index_ = 4; // 44100Hz
        codec.channel_configuration_ = 2;    // Stereo
        codec.sound_format_ = 10;            // AAC
        codec.sound_rate_ = 3;               // 44kHz
        codec.sound_size_ = 1;               // 16-bit
        codec.sound_type_ = 1;               // Stereo
    } else {
        *pframe = NULL;
        *pnb_frame = 0;
    }

    return srs_success;
}

srs_error_t MockGbRawAacStream::mux_sequence_header(SrsRawAacStreamCodec *codec, std::string &sh)
{
    mux_sequence_header_called_ = true;

    if (mux_sequence_header_error_ != srs_success) {
        return srs_error_copy(mux_sequence_header_error_);
    }

    sh = sequence_header_output_;
    return srs_success;
}

srs_error_t MockGbRawAacStream::mux_aac2flv(char *frame, int nb_frame, SrsRawAacStreamCodec *codec, uint32_t dts, char **flv, int *nb_flv)
{
    mux_aac2flv_called_ = true;

    if (mux_aac2flv_error_ != srs_success) {
        return srs_error_copy(mux_aac2flv_error_);
    }

    // Simulate muxing AAC to FLV
    *nb_flv = nb_frame + 2; // 2 bytes for AAC header
    *flv = new char[*nb_flv];
    (*flv)[0] = 0xAF;                    // AAC, 44kHz, 16-bit, stereo
    (*flv)[1] = codec->aac_packet_type_; // 0 for sequence header, 1 for raw data
    if (nb_frame > 0) {
        memcpy(*flv + 2, frame, nb_frame);
    }

    return srs_success;
}

void MockGbRawAacStream::reset()
{
    adts_demux_called_ = false;
    mux_sequence_header_called_ = false;
    mux_aac2flv_called_ = false;
    srs_freep(adts_demux_error_);
    srs_freep(mux_sequence_header_error_);
    srs_freep(mux_aac2flv_error_);
}

// Mock ISrsMpegpsQueue implementation
MockGbMpegpsQueue::MockGbMpegpsQueue()
{
    push_called_ = false;
    push_error_ = srs_success;
    push_count_ = 0;
}

MockGbMpegpsQueue::~MockGbMpegpsQueue()
{
    srs_freep(push_error_);
}

srs_error_t MockGbMpegpsQueue::push(SrsMediaPacket *msg)
{
    push_called_ = true;
    push_count_++;

    if (push_error_ != srs_success) {
        return srs_error_copy(push_error_);
    }

    return srs_success;
}

SrsMediaPacket *MockGbMpegpsQueue::dequeue()
{
    return NULL;
}

void MockGbMpegpsQueue::reset()
{
    push_called_ = false;
    push_count_ = 0;
    srs_freep(push_error_);
}

// Mock ISrsGbSession implementation
MockGbSessionForMuxer::MockGbSessionForMuxer()
{
    device_id_ = "34020000001320000001";
}

MockGbSessionForMuxer::~MockGbSessionForMuxer()
{
}

void MockGbSessionForMuxer::setup(SrsConfDirective *conf)
{
}

void MockGbSessionForMuxer::setup_owner(SrsSharedResource<ISrsGbSession> *wrapper, ISrsInterruptable *owner_coroutine, ISrsContextIdSetter *owner_cid)
{
}

void MockGbSessionForMuxer::on_media_transport(SrsSharedResource<ISrsGbMediaTcpConn> media)
{
}

void MockGbSessionForMuxer::on_ps_pack(ISrsPackContext *ctx, SrsPsPacket *ps, const std::vector<SrsTsMessage *> &msgs)
{
}

const SrsContextId &MockGbSessionForMuxer::get_id()
{
    static SrsContextId cid;
    return cid;
}

std::string MockGbSessionForMuxer::desc()
{
    return "MockGbSessionForMuxer";
}

srs_error_t MockGbSessionForMuxer::cycle()
{
    return srs_success;
}

void MockGbSessionForMuxer::on_executor_done(ISrsInterruptable *executor)
{
}

MockInterruptableForRtcTcpConn::MockInterruptableForRtcTcpConn()
{
    interrupt_called_ = false;
    pull_error_ = srs_success;
}

MockInterruptableForRtcTcpConn::~MockInterruptableForRtcTcpConn()
{
    srs_freep(pull_error_);
}

void MockInterruptableForRtcTcpConn::interrupt()
{
    interrupt_called_ = true;
}

srs_error_t MockInterruptableForRtcTcpConn::pull()
{
    return srs_error_copy(pull_error_);
}

void MockInterruptableForRtcTcpConn::reset()
{
    interrupt_called_ = false;
    srs_freep(pull_error_);
}

MockContextIdSetterForRtcTcpConn::MockContextIdSetterForRtcTcpConn()
{
    set_cid_called_ = false;
}

MockContextIdSetterForRtcTcpConn::~MockContextIdSetterForRtcTcpConn()
{
}

void MockContextIdSetterForRtcTcpConn::set_cid(const SrsContextId &cid)
{
    set_cid_called_ = true;
    received_cid_ = cid;
}

void MockContextIdSetterForRtcTcpConn::reset()
{
    set_cid_called_ = false;
    received_cid_ = SrsContextId();
}

MockRtcConnectionForTcpConn::MockRtcConnectionForTcpConn()
{
}

MockRtcConnectionForTcpConn::~MockRtcConnectionForTcpConn()
{
}

const SrsContextId &MockRtcConnectionForTcpConn::get_id()
{
    static SrsContextId cid;
    return cid;
}

std::string MockRtcConnectionForTcpConn::desc()
{
    return "MockRtcConnectionForTcpConn";
}

void MockRtcConnectionForTcpConn::on_disposing(ISrsResource *c)
{
}

void MockRtcConnectionForTcpConn::on_before_dispose(ISrsResource *c)
{
}

void MockRtcConnectionForTcpConn::expire()
{
}

srs_error_t MockRtcConnectionForTcpConn::send_rtcp(char *data, int nb_data)
{
    return srs_success;
}

srs_error_t MockRtcConnectionForTcpConn::send_rtcp_rr(uint32_t ssrc, SrsRtpRingBuffer *rtp_queue, const uint64_t &last_send_systime, const SrsNtp &last_send_ntp)
{
    return srs_success;
}

srs_error_t MockRtcConnectionForTcpConn::send_rtcp_xr_rrtr(uint32_t ssrc)
{
    return srs_success;
}

void MockRtcConnectionForTcpConn::check_send_nacks(SrsRtpNackForReceiver *nack, uint32_t ssrc, uint32_t &sent_nacks, uint32_t &timeout_nacks)
{
}

srs_error_t MockRtcConnectionForTcpConn::send_rtcp_fb_pli(uint32_t ssrc, const SrsContextId &cid_of_subscriber)
{
    return srs_success;
}

srs_error_t MockRtcConnectionForTcpConn::do_send_packet(SrsRtpPacket *pkt)
{
    return srs_success;
}

srs_error_t MockRtcConnectionForTcpConn::do_check_send_nacks()
{
    return srs_success;
}

void MockRtcConnectionForTcpConn::on_connection_established()
{
}

srs_error_t MockRtcConnectionForTcpConn::on_dtls_alert(std::string type, std::string desc)
{
    return srs_success;
}

srs_error_t MockRtcConnectionForTcpConn::on_dtls_handshake_done()
{
    return srs_success;
}

srs_error_t MockRtcConnectionForTcpConn::on_dtls_application_data(const char *data, const int len)
{
    return srs_success;
}

srs_error_t MockRtcConnectionForTcpConn::on_rtp_cipher(char *data, int nb_data)
{
    return srs_success;
}

srs_error_t MockRtcConnectionForTcpConn::on_rtp_plaintext(char *buf, int nb_buf)
{
    return srs_success;
}

srs_error_t MockRtcConnectionForTcpConn::on_rtcp(char *buf, int nb_buf)
{
    return srs_success;
}

srs_error_t MockRtcConnectionForTcpConn::on_binding_request(SrsStunPacket *r, std::string &ice_pwd)
{
    return srs_success;
}

ISrsRtcNetwork *MockRtcConnectionForTcpConn::udp()
{
    return NULL;
}

ISrsRtcNetwork *MockRtcConnectionForTcpConn::tcp()
{
    return NULL;
}

void MockRtcConnectionForTcpConn::alive()
{
}

bool MockRtcConnectionForTcpConn::is_alive()
{
    return true;
}

bool MockRtcConnectionForTcpConn::is_disposing()
{
    return false;
}

void MockRtcConnectionForTcpConn::switch_to_context()
{
}

srs_error_t MockRtcConnectionForTcpConn::add_publisher(SrsRtcUserConfig * /*ruc*/, SrsSdp & /*local_sdp*/)
{
    return srs_success;
}

srs_error_t MockRtcConnectionForTcpConn::add_player(SrsRtcUserConfig * /*ruc*/, SrsSdp & /*local_sdp*/)
{
    return srs_success;
}

void MockRtcConnectionForTcpConn::set_all_tracks_status(std::string /*stream_uri*/, bool /*is_publish*/, bool /*status*/)
{
}

void MockRtcConnectionForTcpConn::set_remote_sdp(const SrsSdp & /*sdp*/)
{
}

void MockRtcConnectionForTcpConn::set_local_sdp(const SrsSdp & /*sdp*/)
{
}

void MockRtcConnectionForTcpConn::set_state_as_waiting_stun()
{
}

srs_error_t MockRtcConnectionForTcpConn::initialize(ISrsRequest * /*r*/, bool /*dtls*/, bool /*srtp*/, std::string /*username*/)
{
    return srs_success;
}

std::string MockRtcConnectionForTcpConn::username()
{
    return "";
}

std::string MockRtcConnectionForTcpConn::token()
{
    return "";
}

void MockRtcConnectionForTcpConn::set_publish_token(SrsSharedPtr<ISrsStreamPublishToken> /*publish_token*/)
{
}

void MockRtcConnectionForTcpConn::simulate_nack_drop(int /*nn*/)
{
}

srs_error_t MockRtcConnectionForTcpConn::generate_local_sdp(SrsRtcUserConfig * /*ruc*/, SrsSdp & /*local_sdp*/, std::string & /*username*/)
{
    return srs_success;
}

// Mock ISrsPsPackHandler implementation
MockPsPackHandler::MockPsPackHandler()
{
    on_ps_pack_called_ = false;
    on_ps_pack_count_ = 0;
    last_pack_id_ = 0;
    last_msgs_count_ = 0;
    on_ps_pack_error_ = srs_success;
}

MockPsPackHandler::~MockPsPackHandler()
{
    srs_freep(on_ps_pack_error_);
}

srs_error_t MockPsPackHandler::on_ps_pack(SrsPsPacket *ps, const std::vector<SrsTsMessage *> &msgs)
{
    on_ps_pack_called_ = true;
    on_ps_pack_count_++;
    last_pack_id_ = ps->id_;
    last_msgs_count_ = (int)msgs.size();

    if (on_ps_pack_error_ != srs_success) {
        return srs_error_copy(on_ps_pack_error_);
    }

    return srs_success;
}

void MockPsPackHandler::reset()
{
    on_ps_pack_called_ = false;
    on_ps_pack_count_ = 0;
    last_pack_id_ = 0;
    last_msgs_count_ = 0;
    srs_freep(on_ps_pack_error_);
}

// Test SrsGbMuxer::on_ts_message - covers the major use scenario:
// 1. Process audio TS message with AAC ADTS data
// 2. Connect to RTMP server on first message
// 3. Generate AAC sequence header on first audio frame
// 4. Mux audio frames to FLV and push to queue
VOID TEST(GbMuxerTest, OnTsMessageAudio)
{
    srs_error_t err = srs_success;

    // Create mock session
    SrsUniquePtr<MockGbSessionForMuxer> mock_session(new MockGbSessionForMuxer());
    mock_session->device_id_ = "34020000001320000001";

    // Create SrsGbMuxer
    SrsUniquePtr<SrsGbMuxer> muxer(new SrsGbMuxer(mock_session.get()));
    muxer->setup("rtmp://127.0.0.1/live/[stream]");

    // Create mock dependencies
    MockGbRawAacStream *mock_aac = new MockGbRawAacStream();
    MockGbMpegpsQueue *mock_queue = new MockGbMpegpsQueue();

    // Inject mocks into muxer (but not sdk_ yet - let connect() create it)
    muxer->aac_ = mock_aac;
    muxer->queue_ = mock_queue;

    // We need to mock the app_factory to return our mock SDK
    // For now, let's just test without RTMP connection by setting sdk_ to a mock
    // that's already "connected"
    MockRtmpClient *mock_sdk = new MockRtmpClient();
    muxer->sdk_ = mock_sdk; // Simulate already connected

    // Create TS message with audio data (AAC ADTS format)
    SrsUniquePtr<SrsTsMessage> ts_msg(new SrsTsMessage());
    ts_msg->sid_ = SrsTsPESStreamIdAudioCommon; // Audio stream
    ts_msg->dts_ = 90000;                       // 1 second in 90kHz timebase

    // Create AAC ADTS frame data (simplified)
    // ADTS header (7 bytes) + AAC raw data
    char adts_data[200];
    memset(adts_data, 0, sizeof(adts_data));
    // ADTS sync word (12 bits = 0xFFF)
    adts_data[0] = 0xFF;
    adts_data[1] = 0xF1; // MPEG-4, no CRC
    // Profile, sample rate, channel config (simplified)
    adts_data[2] = 0x50; // AAC LC, 44.1kHz
    adts_data[3] = 0x80;
    adts_data[4] = 0x00;
    adts_data[5] = 0x1F;
    adts_data[6] = 0xFC;
    // Fill with some audio data
    for (int i = 7; i < 200; i++) {
        adts_data[i] = (char)(i & 0xFF);
    }

    ts_msg->payload_->append(adts_data, 200);

    // Test: Process audio TS message
    err = muxer->on_ts_message(ts_msg.get());
    HELPER_EXPECT_SUCCESS(err);

    // Verify: AAC demux was called
    EXPECT_TRUE(mock_aac->adts_demux_called_);

    // Verify: AAC sequence header was generated
    EXPECT_TRUE(mock_aac->mux_sequence_header_called_);

    // Verify: AAC to FLV muxing was called (sequence header + raw data)
    EXPECT_TRUE(mock_aac->mux_aac2flv_called_);

    // Verify: Messages were pushed to queue (sequence header + raw data)
    EXPECT_TRUE(mock_queue->push_called_);
    EXPECT_GE(mock_queue->push_count_, 2); // At least sequence header + one raw frame

    // Clean up - set to NULL to avoid double-free
    muxer->sdk_ = NULL;
    muxer->aac_ = NULL;
    muxer->queue_ = NULL;
    srs_freep(mock_sdk);
    srs_freep(mock_aac);
    srs_freep(mock_queue);
}

// Test SrsPackContext::on_ts_message - covers the major use scenario:
// 1. Accumulate multiple TS messages in the same PS pack
// 2. Trigger on_ps_pack callback when a new pack ID is detected
// 3. Correct DTS/PTS timestamps when they are zero
VOID TEST(PackContextTest, OnTsMessageMultiplePacksWithTimestampCorrection)
{
    srs_error_t err = srs_success;

    // Create mock handler
    SrsUniquePtr<MockPsPackHandler> mock_handler(new MockPsPackHandler());

    // Create SrsPackContext
    SrsUniquePtr<SrsPackContext> ctx(new SrsPackContext(mock_handler.get()));

    // Create PS context and packet for first pack
    SrsUniquePtr<SrsPsContext> ps_ctx1(new SrsPsContext());
    SrsUniquePtr<SrsPsPacket> ps_packet1(new SrsPsPacket(ps_ctx1.get()));
    ps_packet1->id_ = 0x10000001; // First pack ID

    // Create first TS message (video) with valid timestamps
    SrsUniquePtr<SrsTsMessage> msg1(new SrsTsMessage());
    msg1->sid_ = SrsTsPESStreamIdVideoCommon;
    msg1->dts_ = 90000; // 1 second in 90kHz
    msg1->pts_ = 90000;
    msg1->ps_helper_ = &ps_ctx1->helper_;
    ps_ctx1->helper_.ctx_ = ps_ctx1.get();
    ps_ctx1->helper_.ps_ = ps_packet1.get();

    // Test: Process first message
    err = ctx->on_ts_message(msg1.get());
    HELPER_EXPECT_SUCCESS(err);

    // Verify: Handler not called yet (still accumulating messages in same pack)
    EXPECT_FALSE(mock_handler->on_ps_pack_called_);

    // Create second TS message (audio) in same pack with zero timestamps
    SrsUniquePtr<SrsTsMessage> msg2(new SrsTsMessage());
    msg2->sid_ = SrsTsPESStreamIdAudioCommon;
    msg2->dts_ = 0; // Zero timestamp - should be corrected
    msg2->pts_ = 0; // Zero timestamp - should be corrected
    msg2->ps_helper_ = &ps_ctx1->helper_;

    // Test: Process second message in same pack
    err = ctx->on_ts_message(msg2.get());
    HELPER_EXPECT_SUCCESS(err);

    // Verify: Handler still not called (same pack ID)
    EXPECT_FALSE(mock_handler->on_ps_pack_called_);

    // Create PS context and packet for second pack (different ID)
    SrsUniquePtr<SrsPsContext> ps_ctx2(new SrsPsContext());
    SrsUniquePtr<SrsPsPacket> ps_packet2(new SrsPsPacket(ps_ctx2.get()));
    ps_packet2->id_ = 0x10000002; // Different pack ID

    // Create third TS message (video) in new pack
    SrsUniquePtr<SrsTsMessage> msg3(new SrsTsMessage());
    msg3->sid_ = SrsTsPESStreamIdVideoCommon;
    msg3->dts_ = 180000; // 2 seconds in 90kHz
    msg3->pts_ = 180000;
    msg3->ps_helper_ = &ps_ctx2->helper_;
    ps_ctx2->helper_.ctx_ = ps_ctx2.get();
    ps_ctx2->helper_.ps_ = ps_packet2.get();

    // Test: Process third message with new pack ID
    err = ctx->on_ts_message(msg3.get());
    HELPER_EXPECT_SUCCESS(err);

    // Verify: Handler was called once (pack ID changed)
    EXPECT_TRUE(mock_handler->on_ps_pack_called_);
    EXPECT_EQ(1, mock_handler->on_ps_pack_count_);
    EXPECT_EQ(0x10000001u, mock_handler->last_pack_id_);
    EXPECT_EQ(2, mock_handler->last_msgs_count_); // msg1 and msg2
}

// Test SrsPackContext::on_recover_mode - covers recovery statistics and message dropping
VOID TEST(PackContextTest, OnRecoverMode)
{
    srs_error_t err;

    // Create mock handler
    SrsUniquePtr<MockPsPackHandler> mock_handler(new MockPsPackHandler());

    // Create SrsPackContext
    SrsUniquePtr<SrsPackContext> ctx(new SrsPackContext(mock_handler.get()));

    // Verify initial state
    EXPECT_EQ(0u, ctx->media_nn_recovered_);
    EXPECT_EQ(0u, ctx->media_nn_msgs_dropped_);

    // Test: First recovery with empty message queue (nn_recover = 1)
    ctx->on_recover_mode(1);

    // Verify: Recovery counter incremented, no messages dropped
    EXPECT_EQ(1u, ctx->media_nn_recovered_);
    EXPECT_EQ(0u, ctx->media_nn_msgs_dropped_);

    // Setup: Add messages to the queue
    SrsUniquePtr<SrsPsContext> ps_ctx(new SrsPsContext());
    SrsUniquePtr<SrsPsPacket> ps_packet(new SrsPsPacket(ps_ctx.get()));
    ps_packet->id_ = 0x10000001;

    // Create and add first message
    SrsUniquePtr<SrsTsMessage> msg1(new SrsTsMessage());
    msg1->sid_ = SrsTsPESStreamIdVideoCommon;
    msg1->dts_ = 90000;
    msg1->pts_ = 90000;
    msg1->ps_helper_ = &ps_ctx->helper_;
    ps_ctx->helper_.ctx_ = ps_ctx.get();
    ps_ctx->helper_.ps_ = ps_packet.get();

    err = ctx->on_ts_message(msg1.get());
    HELPER_EXPECT_SUCCESS(err);

    // Create and add second message
    SrsUniquePtr<SrsTsMessage> msg2(new SrsTsMessage());
    msg2->sid_ = SrsTsPESStreamIdAudioCommon;
    msg2->dts_ = 90000;
    msg2->pts_ = 90000;
    msg2->ps_helper_ = &ps_ctx->helper_;

    err = ctx->on_ts_message(msg2.get());
    HELPER_EXPECT_SUCCESS(err);

    // Test: Second recovery with messages in queue (nn_recover = 2)
    ctx->on_recover_mode(2);

    // Verify: Recovery counter NOT incremented (nn_recover > 1), but messages dropped
    EXPECT_EQ(1u, ctx->media_nn_recovered_);    // Still 1, not incremented
    EXPECT_EQ(2u, ctx->media_nn_msgs_dropped_); // 2 messages dropped

    // Add more messages to test another recovery
    SrsUniquePtr<SrsTsMessage> msg3(new SrsTsMessage());
    msg3->sid_ = SrsTsPESStreamIdVideoCommon;
    msg3->dts_ = 180000;
    msg3->pts_ = 180000;
    msg3->ps_helper_ = &ps_ctx->helper_;

    err = ctx->on_ts_message(msg3.get());
    HELPER_EXPECT_SUCCESS(err);

    // Test: Third recovery with one message (nn_recover = 0, should increment)
    ctx->on_recover_mode(0);

    // Verify: Recovery counter incremented (nn_recover <= 1), message dropped
    EXPECT_EQ(2u, ctx->media_nn_recovered_);    // Incremented to 2
    EXPECT_EQ(3u, ctx->media_nn_msgs_dropped_); // Total 3 messages dropped
}

// Test SrsRecoverablePsContext::decode_rtp with valid RTP packet containing PS data
VOID TEST(RecoverablePsContextTest, DecodeRtpWithValidPacket)
{
    srs_error_t err;

    // Create mock handler
    MockPsHandler handler;

    // Create context under test
    SrsUniquePtr<SrsRecoverablePsContext> context(new SrsRecoverablePsContext());

    // PT=DynamicRTP-Type-96, SSRC=0xBEBD135, Seq=31916, Time=95652000
    // This is a valid RTP packet containing PS data with audio PES packet
    string raw = string(
        "\x80\x60\x7c\xac\x05\xb3\x88\xa0\x0b\xeb\xd1\x35\x00\x00\x01\xc0"
        "\x00\x6e\x8c\x80\x07\x25\x8a\x6d\xa9\xfd\xff\xf8\xff\xf9\x50\x40"
        "\x0c\x9f\xfc\x01\x3a\x2e\x98\x28\x18\x0a\x09\x84\x81\x60\xc0\x50"
        "\x2a\x12\x13\x05\x02\x22\x00\x88\x4c\x40\x11\x09\x85\x02\x61\x10"
        "\xa8\x40\x00\x00\x00\x1f\xa6\x8d\xef\x03\xca\xf0\x63\x7f\x02\xe2"
        "\x1d\x7f\xbf\x3e\x22\xbe\x3d\xf7\xa2\x7c\xba\xe6\xc8\xfb\x35\x9f"
        "\xd1\xa2\xc4\xaa\xc5\x3d\xf6\x67\xfd\xc6\x39\x06\x9f\x9e\xdf\x9b"
        "\x10\xd7\x4f\x59\xfd\xef\xea\xee\xc8\x4c\x40\xe5\xd9\xed\x00\x1c",
        128);
    SrsUniquePtr<SrsBuffer> buffer(new SrsBuffer((char *)raw.data(), raw.length()));

    // Decode RTP packet with no reserved bytes
    HELPER_EXPECT_SUCCESS(context->decode_rtp(buffer.get(), 0, &handler));

    // Verify successful decoding - should get one audio message
    ASSERT_EQ((size_t)1, handler.msgs_.size());
    EXPECT_EQ(0, context->recover_);

    // Verify message properties
    SrsTsMessage *msg = handler.msgs_.front();
    EXPECT_EQ(SrsTsPESStreamIdAudioCommon, msg->sid_);
    EXPECT_EQ(100, msg->PES_packet_length_);

    // Verify RTP header fields were extracted correctly
    EXPECT_EQ(31916, context->ctx_->helper()->rtp_seq_);
    EXPECT_EQ(95652000, context->ctx_->helper()->rtp_ts_);
    EXPECT_EQ(96, context->ctx_->helper()->rtp_pt_);
}

// Test SrsRecoverablePsContext::enter_recover_mode with normal packet size
VOID TEST(RecoverablePsContextTest, EnterRecoverModeWithNormalPacket)
{
    srs_error_t err;

    // Create mock handler
    MockPsHandler handler;

    // Create context under test
    SrsUniquePtr<SrsRecoverablePsContext> context(new SrsRecoverablePsContext());

    // Create a buffer with normal packet size (less than SRS_GB_LARGE_PACKET=1500)
    char data[1000];
    memset(data, 0xFF, sizeof(data));
    SrsUniquePtr<SrsBuffer> stream(new SrsBuffer(data, sizeof(data)));

    // Initialize helper with some test values
    SrsPsDecodeHelper *h = context->ctx_->helper();
    h->rtp_seq_ = 12345;
    h->rtp_ts_ = 90000;
    h->rtp_pt_ = 96;
    h->pack_first_seq_ = 100;
    h->pack_nn_msgs_ = 5;
    h->pack_pre_msg_last_seq_ = 99;
    h->pack_id_ = 1;

    // Create a last message to be reaped
    SrsTsMessage *last = context->ctx_->last();
    last->PES_packet_length_ = 200;

    // Simulate an error that triggers recovery
    srs_error_t test_error = srs_error_new(ERROR_GB_PS_MEDIA, "test decode error");

    // Record initial state
    int initial_recover = context->recover_;
    int initial_pos = stream->pos();

    // Call enter_recover_mode
    err = context->enter_recover_mode(stream.get(), &handler, initial_pos, test_error);

    // Verify: Should succeed (return srs_success) because packet size is normal
    HELPER_EXPECT_SUCCESS(err);

    // Verify: Recovery counter incremented
    EXPECT_EQ(initial_recover + 1, context->recover_);

    // Verify: Stream buffer fully consumed (all bytes skipped)
    EXPECT_EQ(0, stream->left());

    // Verify: Handler's on_recover_mode was called
    // Note: MockPsHandler doesn't track this, but the method was invoked
}

// Test SrsRecoverablePsContext recovery flow: enter recover mode, skip to pack header, quit recover mode
VOID TEST(RecoverablePsContextTest, RecoverModeSkipToPackHeaderAndQuit)
{
    // Create mock handler
    MockPsHandler handler;

    // Create context under test
    SrsUniquePtr<SrsRecoverablePsContext> context(new SrsRecoverablePsContext());

    // Create a buffer with corrupted data followed by pack header (00 00 01 ba)
    // Simulate real scenario: garbage bytes, then pack start code
    char data[100];
    memset(data, 0xFF, sizeof(data)); // Fill with garbage

    // Place pack header at offset 50: 00 00 01 ba
    data[50] = 0x00;
    data[51] = 0x00;
    data[52] = 0x01;
    data[53] = 0xba;

    SrsUniquePtr<SrsBuffer> stream(new SrsBuffer(data, sizeof(data)));

    // Initialize helper with test values
    SrsPsDecodeHelper *h = context->ctx_->helper();
    h->rtp_seq_ = 54321;
    h->rtp_ts_ = 180000;
    h->rtp_pt_ = 96;

    // Simulate entering recover mode first
    context->recover_ = 1;

    // Record initial position
    int initial_pos = stream->pos();
    EXPECT_EQ(0, initial_pos);

    // Call srs_skip_util_pack to find pack header
    bool found = srs_skip_util_pack(stream.get());

    // Verify: Pack header was found
    EXPECT_TRUE(found);

    // Verify: Stream position advanced to pack header (offset 50)
    EXPECT_EQ(50, stream->pos());

    // Verify: Still in recover mode
    EXPECT_EQ(1, context->recover_);

    // Now call quit_recover_mode to exit recover mode
    context->quit_recover_mode(stream.get(), &handler);

    // Verify: Exited recover mode (recover_ reset to 0)
    EXPECT_EQ(0, context->recover_);

    // Verify: Stream position unchanged (still at pack header)
    EXPECT_EQ(50, stream->pos());

    // Verify: Remaining bytes available for decoding
    EXPECT_EQ(50, stream->left());
}

// Mock ISrsHttpMessage implementation for GB publish API
MockHttpMessageForGbPublish::MockHttpMessageForGbPublish()
{
    mock_conn_ = new MockHttpConn();
    set_connection(mock_conn_);
    body_content_ = "";
}

MockHttpMessageForGbPublish::~MockHttpMessageForGbPublish()
{
    srs_freep(mock_conn_);
}

srs_error_t MockHttpMessageForGbPublish::body_read_all(std::string &body)
{
    body = body_content_;
    return srs_success;
}

// Mock ISrsResourceManager implementation for GB publish API
MockResourceManagerForGbPublish::MockResourceManagerForGbPublish()
{
}

MockResourceManagerForGbPublish::~MockResourceManagerForGbPublish()
{
}

srs_error_t MockResourceManagerForGbPublish::start()
{
    return srs_success;
}

bool MockResourceManagerForGbPublish::empty()
{
    return id_map_.empty() && fast_id_map_.empty();
}

size_t MockResourceManagerForGbPublish::size()
{
    return id_map_.size();
}

void MockResourceManagerForGbPublish::add(ISrsResource *conn, bool *exists)
{
}

void MockResourceManagerForGbPublish::add_with_id(const std::string &id, ISrsResource *conn)
{
    id_map_[id] = conn;
}

void MockResourceManagerForGbPublish::add_with_fast_id(uint64_t id, ISrsResource *conn)
{
    fast_id_map_[id] = conn;
}

void MockResourceManagerForGbPublish::add_with_name(const std::string & /*name*/, ISrsResource * /*conn*/)
{
}

ISrsResource *MockResourceManagerForGbPublish::at(int index)
{
    return NULL;
}

ISrsResource *MockResourceManagerForGbPublish::find_by_id(std::string id)
{
    if (id_map_.find(id) != id_map_.end()) {
        return id_map_[id];
    }
    return NULL;
}

ISrsResource *MockResourceManagerForGbPublish::find_by_fast_id(uint64_t id)
{
    if (fast_id_map_.find(id) != fast_id_map_.end()) {
        return fast_id_map_[id];
    }
    return NULL;
}

ISrsResource *MockResourceManagerForGbPublish::find_by_name(std::string /*name*/)
{
    return NULL;
}

void MockResourceManagerForGbPublish::remove(ISrsResource *c)
{
}

void MockResourceManagerForGbPublish::subscribe(ISrsDisposingHandler *h)
{
}

void MockResourceManagerForGbPublish::unsubscribe(ISrsDisposingHandler *h)
{
}

void MockResourceManagerForGbPublish::reset()
{
    id_map_.clear();
    fast_id_map_.clear();
}

// Mock ISrsGbSession implementation for GB publish API
MockGbSessionForApiPublish::MockGbSessionForApiPublish()
{
    setup_called_ = false;
    setup_owner_called_ = false;
    setup_conf_ = NULL;
    owner_coroutine_ = NULL;
}

MockGbSessionForApiPublish::~MockGbSessionForApiPublish()
{
    owner_coroutine_ = NULL;
}

void MockGbSessionForApiPublish::setup(SrsConfDirective *conf)
{
    setup_called_ = true;
    setup_conf_ = conf;
}

void MockGbSessionForApiPublish::setup_owner(SrsSharedResource<ISrsGbSession> *wrapper, ISrsInterruptable *owner_coroutine, ISrsContextIdSetter *owner_cid)
{
    setup_owner_called_ = true;
    owner_coroutine_ = owner_coroutine;
}

void MockGbSessionForApiPublish::on_media_transport(SrsSharedResource<ISrsGbMediaTcpConn> media)
{
}

void MockGbSessionForApiPublish::on_ps_pack(ISrsPackContext *ctx, SrsPsPacket *ps, const std::vector<SrsTsMessage *> &msgs)
{
}

const SrsContextId &MockGbSessionForApiPublish::get_id()
{
    static SrsContextId cid;
    return cid;
}

std::string MockGbSessionForApiPublish::desc()
{
    return "MockGbSessionForApiPublish";
}

srs_error_t MockGbSessionForApiPublish::cycle()
{
    srs_error_t err = srs_success;

    // Block like a real session would, until interrupted
    while (true) {
        if (!owner_coroutine_) {
            return err;
        }
        if ((err = owner_coroutine_->pull()) != srs_success) {
            return srs_error_wrap(err, "pull");
        }

        // Sleep to simulate work and allow interruption
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);
    }

    return err;
}

void MockGbSessionForApiPublish::on_executor_done(ISrsInterruptable *executor)
{
    owner_coroutine_ = NULL;
}

void MockGbSessionForApiPublish::reset()
{
    setup_called_ = false;
    setup_owner_called_ = false;
    setup_conf_ = NULL;
    owner_coroutine_ = NULL;
}

// Mock ISrsAppFactory implementation for GB publish API
MockAppFactoryForGbPublish::MockAppFactoryForGbPublish()
{
    mock_gb_session_ = new MockGbSessionForApiPublish();
}

MockAppFactoryForGbPublish::~MockAppFactoryForGbPublish()
{
    srs_freep(mock_gb_session_);
}

#ifdef SRS_GB28181
ISrsGbSession *MockAppFactoryForGbPublish::create_gb_session()
{
    // Return the mock session (ownership transferred to caller)
    MockGbSessionForApiPublish *session = mock_gb_session_;
    mock_gb_session_ = new MockGbSessionForApiPublish();
    return session;
}
#endif

void MockAppFactoryForGbPublish::reset()
{
    srs_freep(mock_gb_session_);
    mock_gb_session_ = new MockGbSessionForApiPublish();
}

// Test SrsGoApiGbPublish::do_serve_http - successful GB session creation
VOID TEST(GB28181Test, GoApiGbPublishSuccess)
{
    srs_error_t err;

    // Create mock dependencies
    SrsUniquePtr<MockResponseWriter> mock_writer(new MockResponseWriter());
    SrsUniquePtr<MockHttpMessageForGbPublish> mock_request(new MockHttpMessageForGbPublish());
    SrsUniquePtr<MockResourceManagerForGbPublish> mock_manager(new MockResourceManagerForGbPublish());
    SrsUniquePtr<MockAppFactoryForGbPublish> mock_factory(new MockAppFactoryForGbPublish());
    SrsUniquePtr<MockAppConfigForGbListener> mock_config(new MockAppConfigForGbListener());

    // Set up test configuration
    SrsConfDirective *conf = new SrsConfDirective();
    conf->name_ = "stream_caster";
    mock_config->stream_caster_listen_port_ = 9000;

    // Create API handler
    SrsUniquePtr<SrsGoApiGbPublish> api(new SrsGoApiGbPublish(conf));

    // Inject mock dependencies
    api->config_ = mock_config.get();
    api->gb_manager_ = mock_manager.get();
    api->app_factory_ = mock_factory.get();

    // Set up request body with valid JSON
    mock_request->body_content_ = "{\"id\":\"34020000001320000001\",\"ssrc\":\"1234567890\"}";

    // Create response object
    SrsUniquePtr<SrsJsonObject> res(SrsJsonAny::object());

    // Call do_serve_http
    err = api->do_serve_http(mock_writer.get(), mock_request.get(), res.get());
    HELPER_EXPECT_SUCCESS(err);

    // Verify response contains expected fields
    SrsJsonAny *code = res->get_property("code");
    EXPECT_TRUE(code != NULL);
    EXPECT_TRUE(code->is_integer());
    EXPECT_EQ(ERROR_SUCCESS, code->to_integer());

    SrsJsonAny *port = res->get_property("port");
    EXPECT_TRUE(port != NULL);
    EXPECT_TRUE(port->is_integer());
    EXPECT_EQ(9000, port->to_integer());

    SrsJsonAny *is_tcp = res->get_property("is_tcp");
    EXPECT_TRUE(is_tcp != NULL);
    EXPECT_TRUE(is_tcp->is_boolean());
    EXPECT_TRUE(is_tcp->to_boolean());

    // Verify session was created and registered
    ISrsResource *session_by_id = mock_manager->find_by_id("34020000001320000001");
    EXPECT_TRUE(session_by_id != NULL);

    ISrsResource *session_by_ssrc = mock_manager->find_by_fast_id(1234567890);
    EXPECT_TRUE(session_by_ssrc != NULL);

    // Verify both lookups return the same session
    EXPECT_EQ(session_by_id, session_by_ssrc);

    // Verify Connection header was set to Close
    std::string connection_header = mock_writer->header()->get("Connection");
    EXPECT_STREQ("Close", connection_header.c_str());

    // Clean up - interrupt and remove the created session/executor
    // The session is wrapped in SrsSharedResource, and the executor manages it
    SrsSharedResource<ISrsGbSession> *session_wrapper = dynamic_cast<SrsSharedResource<ISrsGbSession> *>(session_by_id);
    if (session_wrapper) {
        ISrsGbSession *session = session_wrapper->get();
        MockGbSessionForApiPublish *mock_session = dynamic_cast<MockGbSessionForApiPublish *>(session);
        if (mock_session && mock_session->owner_coroutine_) {
            // Interrupt the executor coroutine to stop the cycle
            mock_session->owner_coroutine_->interrupt();
        }
    }

    // Wait a bit for the coroutine to finish
    srs_usleep(1 * SRS_UTIME_MILLISECONDS);

    // Clean up - set injected fields to NULL to avoid double-free
    api->config_ = NULL;
    api->gb_manager_ = NULL;
    api->app_factory_ = NULL;

    srs_freep(conf);
}

// Mock ISrsRtcNetwork implementation
MockRtcNetworkForNetworks::MockRtcNetworkForNetworks()
{
    initialize_error_ = srs_success;
    initialize_called_ = false;
    last_cfg_ = NULL;
    last_dtls_ = false;
    last_srtp_ = false;
    state_ = SrsRtcNetworkStateInit;
    is_established_ = false;
}

MockRtcNetworkForNetworks::~MockRtcNetworkForNetworks()
{
}

srs_error_t MockRtcNetworkForNetworks::initialize(SrsSessionConfig *cfg, bool dtls, bool srtp)
{
    initialize_called_ = true;
    last_cfg_ = cfg;
    last_dtls_ = dtls;
    last_srtp_ = srtp;
    return srs_error_copy(initialize_error_);
}

void MockRtcNetworkForNetworks::set_state(SrsRtcNetworkState state)
{
    state_ = state;
}

srs_error_t MockRtcNetworkForNetworks::on_dtls_handshake_done()
{
    return srs_success;
}

srs_error_t MockRtcNetworkForNetworks::on_dtls_alert(std::string type, std::string desc)
{
    return srs_success;
}

srs_error_t MockRtcNetworkForNetworks::on_dtls(char *data, int nb_data)
{
    return srs_success;
}

srs_error_t MockRtcNetworkForNetworks::protect_rtp(void *packet, int *nb_cipher)
{
    return srs_success;
}

srs_error_t MockRtcNetworkForNetworks::protect_rtcp(void *packet, int *nb_cipher)
{
    return srs_success;
}

srs_error_t MockRtcNetworkForNetworks::on_stun(SrsStunPacket *r, char *data, int nb_data)
{
    return srs_success;
}

srs_error_t MockRtcNetworkForNetworks::on_rtp(char *data, int nb_data)
{
    return srs_success;
}

srs_error_t MockRtcNetworkForNetworks::on_rtcp(char *data, int nb_data)
{
    return srs_success;
}

bool MockRtcNetworkForNetworks::is_establelished()
{
    return is_established_;
}

srs_error_t MockRtcNetworkForNetworks::write(void *buf, size_t size, ssize_t *nwrite)
{
    return srs_success;
}

void MockRtcNetworkForNetworks::reset()
{
    initialize_called_ = false;
    last_cfg_ = NULL;
    last_dtls_ = false;
    last_srtp_ = false;
    state_ = SrsRtcNetworkStateInit;
    is_established_ = false;
    srs_freep(initialize_error_);
}

void MockRtcNetworkForNetworks::set_initialize_error(srs_error_t err)
{
    srs_freep(initialize_error_);
    initialize_error_ = srs_error_copy(err);
}

// Test SrsRtcNetworks::initialize
VOID TEST(RtcNetworksTest, InitializeSuccess)
{
    srs_error_t err;

    // Create SrsRtcNetworks object with NULL connection (not used in initialize)
    SrsUniquePtr<SrsRtcNetworks> networks(new SrsRtcNetworks(NULL));

    // Create mock networks for UDP and TCP
    MockRtcNetworkForNetworks *mock_udp = new MockRtcNetworkForNetworks();
    MockRtcNetworkForNetworks *mock_tcp = new MockRtcNetworkForNetworks();

    // Inject mock networks into SrsRtcNetworks
    networks->udp_ = mock_udp;
    networks->tcp_ = mock_tcp;

    // Create session config
    SrsSessionConfig cfg;
    cfg.dtls_role_ = "passive";
    cfg.dtls_version_ = "auto";

    // Call initialize with dtls=true, srtp=true
    HELPER_EXPECT_SUCCESS(networks->initialize(&cfg, true, true));

    // Verify both UDP and TCP initialize were called
    EXPECT_TRUE(mock_udp->initialize_called_);
    EXPECT_TRUE(mock_tcp->initialize_called_);

    // Verify parameters were passed correctly
    EXPECT_EQ(&cfg, mock_udp->last_cfg_);
    EXPECT_TRUE(mock_udp->last_dtls_);
    EXPECT_TRUE(mock_udp->last_srtp_);

    EXPECT_EQ(&cfg, mock_tcp->last_cfg_);
    EXPECT_TRUE(mock_tcp->last_dtls_);
    EXPECT_TRUE(mock_tcp->last_srtp_);

    // Clean up - set to NULL to avoid double-free
    networks->udp_ = NULL;
    networks->tcp_ = NULL;
    srs_freep(mock_udp);
    srs_freep(mock_tcp);
}

// Test SrsRtcNetworks major use scenario: set_state, udp, tcp, available, delta
VOID TEST(RtcNetworksTest, MajorUseScenario)
{
    // Create SrsRtcNetworks object with NULL connection
    SrsUniquePtr<SrsRtcNetworks> networks(new SrsRtcNetworks(NULL));

    // Create mock networks for UDP and TCP
    MockRtcNetworkForNetworks *mock_udp = new MockRtcNetworkForNetworks();
    MockRtcNetworkForNetworks *mock_tcp = new MockRtcNetworkForNetworks();

    // Inject mock networks into SrsRtcNetworks
    networks->udp_ = mock_udp;
    networks->tcp_ = mock_tcp;

    // Test 1: udp() and tcp() methods return correct network objects
    EXPECT_EQ(mock_udp, networks->udp());
    EXPECT_EQ(mock_tcp, networks->tcp());

    // Test 2: available() returns dummy when neither UDP nor TCP is established
    ISrsRtcNetwork *available_network = networks->available();
    EXPECT_NE(mock_udp, available_network);
    EXPECT_NE(mock_tcp, available_network);
    EXPECT_NE((ISrsRtcNetwork *)NULL, available_network); // Should return dummy, not NULL

    // Test 3: available() returns UDP when UDP is established
    mock_udp->is_established_ = true;
    available_network = networks->available();
    EXPECT_EQ(mock_udp, available_network);

    // Test 4: available() returns UDP when both UDP and TCP are established (UDP has priority)
    mock_tcp->is_established_ = true;
    available_network = networks->available();
    EXPECT_EQ(mock_udp, available_network);

    // Test 5: available() returns TCP when only TCP is established
    mock_udp->is_established_ = false;
    available_network = networks->available();
    EXPECT_EQ(mock_tcp, available_network);

    // Test 6: set_state() propagates state to both UDP and TCP networks
    networks->set_state(SrsRtcNetworkStateEstablished);
    EXPECT_EQ(SrsRtcNetworkStateEstablished, mock_udp->state_);
    EXPECT_EQ(SrsRtcNetworkStateEstablished, mock_tcp->state_);

    networks->set_state(SrsRtcNetworkStateClosed);
    EXPECT_EQ(SrsRtcNetworkStateClosed, mock_udp->state_);
    EXPECT_EQ(SrsRtcNetworkStateClosed, mock_tcp->state_);

    // Test 7: delta() returns non-NULL delta object
    ISrsKbpsDelta *delta = networks->delta();
    EXPECT_NE((ISrsKbpsDelta *)NULL, delta);

    // Clean up - set to NULL to avoid double-free
    networks->udp_ = NULL;
    networks->tcp_ = NULL;
    srs_freep(mock_udp);
    srs_freep(mock_tcp);
}

VOID TEST(RtcDummyNetworkTest, AllMethodsReturnSuccess)
{
    srs_error_t err;

    // Create SrsRtcDummyNetwork instance
    SrsUniquePtr<SrsRtcDummyNetwork> dummy_network(new SrsRtcDummyNetwork());

    // Test initialize() - should return success
    HELPER_EXPECT_SUCCESS(dummy_network->initialize(NULL, true, true));

    // Test set_state() - should not crash
    dummy_network->set_state(SrsRtcNetworkStateEstablished);

    // Test is_establelished() - should always return true
    EXPECT_TRUE(dummy_network->is_establelished());

    // Test on_dtls_handshake_done() - should return success
    HELPER_EXPECT_SUCCESS(dummy_network->on_dtls_handshake_done());

    // Test on_dtls_alert() - should return success
    HELPER_EXPECT_SUCCESS(dummy_network->on_dtls_alert("warning", "close_notify"));

    // Test on_dtls() - should return success
    char dtls_data[100] = {0};
    HELPER_EXPECT_SUCCESS(dummy_network->on_dtls(dtls_data, sizeof(dtls_data)));

    // Test protect_rtp() - should return success
    char rtp_packet[1500] = {0};
    int rtp_cipher_size = sizeof(rtp_packet);
    HELPER_EXPECT_SUCCESS(dummy_network->protect_rtp(rtp_packet, &rtp_cipher_size));

    // Test protect_rtcp() - should return success
    char rtcp_packet[1500] = {0};
    int rtcp_cipher_size = sizeof(rtcp_packet);
    HELPER_EXPECT_SUCCESS(dummy_network->protect_rtcp(rtcp_packet, &rtcp_cipher_size));

    // Test on_stun() - should return success
    HELPER_EXPECT_SUCCESS(dummy_network->on_stun(NULL, dtls_data, sizeof(dtls_data)));

    // Test on_rtp() - should return success
    HELPER_EXPECT_SUCCESS(dummy_network->on_rtp(rtp_packet, sizeof(rtp_packet)));

    // Test on_rtcp() - should return success
    HELPER_EXPECT_SUCCESS(dummy_network->on_rtcp(rtcp_packet, sizeof(rtcp_packet)));

    // Test write() - should return success
    char write_buf[1500] = {0};
    ssize_t nwrite = 0;
    HELPER_EXPECT_SUCCESS(dummy_network->write(write_buf, sizeof(write_buf), &nwrite));
}

// Mock ISrsRtcConnection implementation
MockRtcConnectionForUdpNetwork::MockRtcConnectionForUdpNetwork()
{
    on_dtls_alert_error_ = srs_success;
    last_alert_type_ = "";
    last_alert_desc_ = "";
    on_rtp_cipher_called_ = false;
    on_rtp_plaintext_called_ = false;
    on_rtcp_called_ = false;
}

MockRtcConnectionForUdpNetwork::~MockRtcConnectionForUdpNetwork()
{
    srs_freep(on_dtls_alert_error_);
}

const SrsContextId &MockRtcConnectionForUdpNetwork::get_id()
{
    static SrsContextId cid;
    return cid;
}

std::string MockRtcConnectionForUdpNetwork::desc()
{
    return "MockRtcConnectionForUdpNetwork";
}

void MockRtcConnectionForUdpNetwork::on_disposing(ISrsResource *c)
{
}

void MockRtcConnectionForUdpNetwork::on_before_dispose(ISrsResource *c)
{
}

void MockRtcConnectionForUdpNetwork::expire()
{
}

srs_error_t MockRtcConnectionForUdpNetwork::send_rtcp(char *data, int nb_data)
{
    return srs_success;
}

srs_error_t MockRtcConnectionForUdpNetwork::do_send_packet(SrsRtpPacket *pkt)
{
    return srs_success;
}

srs_error_t MockRtcConnectionForUdpNetwork::send_rtcp_rr(uint32_t ssrc, SrsRtpRingBuffer *rtp_queue, const uint64_t &last_send_systime, const SrsNtp &last_send_ntp)
{
    return srs_success;
}

srs_error_t MockRtcConnectionForUdpNetwork::send_rtcp_xr_rrtr(uint32_t ssrc)
{
    return srs_success;
}

void MockRtcConnectionForUdpNetwork::check_send_nacks(SrsRtpNackForReceiver *nack, uint32_t ssrc, uint32_t &sent_nacks, uint32_t &timeout_nacks)
{
}

srs_error_t MockRtcConnectionForUdpNetwork::send_rtcp_fb_pli(uint32_t ssrc, const SrsContextId &cid_of_subscriber)
{
    return srs_success;
}

srs_error_t MockRtcConnectionForUdpNetwork::on_rtp(SrsRtpPacket *pkt)
{
    return srs_success;
}

srs_error_t MockRtcConnectionForUdpNetwork::on_rtcp(SrsRtcpCommon *rtcp)
{
    return srs_success;
}

srs_error_t MockRtcConnectionForUdpNetwork::do_check_send_nacks()
{
    return srs_success;
}

srs_error_t MockRtcConnectionForUdpNetwork::on_dtls_handshake_done()
{
    return srs_success;
}

srs_error_t MockRtcConnectionForUdpNetwork::on_dtls_alert(std::string type, std::string desc)
{
    last_alert_type_ = type;
    last_alert_desc_ = desc;
    return srs_error_copy(on_dtls_alert_error_);
}

srs_error_t MockRtcConnectionForUdpNetwork::on_rtp_cipher(char *data, int nb_data)
{
    on_rtp_cipher_called_ = true;
    return srs_success;
}

srs_error_t MockRtcConnectionForUdpNetwork::on_rtp_plaintext(char *data, int nb_data)
{
    on_rtp_plaintext_called_ = true;
    return srs_success;
}

srs_error_t MockRtcConnectionForUdpNetwork::on_rtcp(char *data, int nb_data)
{
    on_rtcp_called_ = true;
    return srs_success;
}

srs_error_t MockRtcConnectionForUdpNetwork::on_binding_request(SrsStunPacket *r, std::string &ice_pwd)
{
    return srs_success;
}

ISrsRtcNetwork *MockRtcConnectionForUdpNetwork::udp()
{
    return NULL;
}

ISrsRtcNetwork *MockRtcConnectionForUdpNetwork::tcp()
{
    return NULL;
}

void MockRtcConnectionForUdpNetwork::alive()
{
}

bool MockRtcConnectionForUdpNetwork::is_alive()
{
    return true;
}

bool MockRtcConnectionForUdpNetwork::is_disposing()
{
    return false;
}

void MockRtcConnectionForUdpNetwork::switch_to_context()
{
}

srs_error_t MockRtcConnectionForUdpNetwork::add_publisher(SrsRtcUserConfig * /*ruc*/, SrsSdp & /*local_sdp*/)
{
    return srs_success;
}

srs_error_t MockRtcConnectionForUdpNetwork::add_player(SrsRtcUserConfig * /*ruc*/, SrsSdp & /*local_sdp*/)
{
    return srs_success;
}

void MockRtcConnectionForUdpNetwork::set_all_tracks_status(std::string /*stream_uri*/, bool /*is_publish*/, bool /*status*/)
{
}

void MockRtcConnectionForUdpNetwork::set_remote_sdp(const SrsSdp & /*sdp*/)
{
}

void MockRtcConnectionForUdpNetwork::set_local_sdp(const SrsSdp & /*sdp*/)
{
}

void MockRtcConnectionForUdpNetwork::set_state_as_waiting_stun()
{
}

srs_error_t MockRtcConnectionForUdpNetwork::initialize(ISrsRequest * /*r*/, bool /*dtls*/, bool /*srtp*/, std::string /*username*/)
{
    return srs_success;
}

std::string MockRtcConnectionForUdpNetwork::username()
{
    return "";
}

std::string MockRtcConnectionForUdpNetwork::token()
{
    return "";
}

void MockRtcConnectionForUdpNetwork::set_publish_token(SrsSharedPtr<ISrsStreamPublishToken> /*publish_token*/)
{
}

void MockRtcConnectionForUdpNetwork::simulate_nack_drop(int /*nn*/)
{
}

srs_error_t MockRtcConnectionForUdpNetwork::generate_local_sdp(SrsRtcUserConfig * /*ruc*/, SrsSdp & /*local_sdp*/, std::string & /*username*/)
{
    return srs_success;
}

void MockRtcConnectionForUdpNetwork::set_on_dtls_alert_error(srs_error_t err)
{
    srs_freep(on_dtls_alert_error_);
    on_dtls_alert_error_ = srs_error_copy(err);
}

void MockRtcConnectionForUdpNetwork::reset()
{
    srs_freep(on_dtls_alert_error_);
    last_alert_type_ = "";
    last_alert_desc_ = "";
    on_rtp_cipher_called_ = false;
    on_rtp_plaintext_called_ = false;
    on_rtcp_called_ = false;
}

// Test SrsRtcUdpNetwork initialization and DTLS handling
VOID TEST(RtcUdpNetworkTest, InitializeAndHandleDtls)
{
    srs_error_t err;

    // Create mock dependencies
    SrsUniquePtr<MockRtcConnectionForUdpNetwork> mock_conn(new MockRtcConnectionForUdpNetwork());
    SrsUniquePtr<MockEphemeralDelta> mock_delta(new MockEphemeralDelta());

    // Create SrsRtcUdpNetwork with mock dependencies
    SrsUniquePtr<SrsRtcUdpNetwork> udp_network(new SrsRtcUdpNetwork(mock_conn.get(), mock_delta.get()));

    // Test 1: Initialize with DTLS but no SRTP (should use SrsSemiSecurityTransport)
    SrsSessionConfig cfg;
    cfg.dtls_role_ = "passive";
    cfg.dtls_version_ = "auto";
    HELPER_EXPECT_SUCCESS(udp_network->initialize(&cfg, true, false));

    // Test 2: Handle DTLS data - should update delta statistics
    char dtls_data[100] = {0x16, 0x03, 0x01, 0x00, 0x50}; // DTLS handshake packet
    int nb_dtls = 100;
    mock_delta->reset();
    // Note: on_dtls will fail because we don't have a real DTLS context, but it should update delta
    err = udp_network->on_dtls(dtls_data, nb_dtls);
    srs_freep(err); // Ignore error, we're testing delta update
    EXPECT_EQ(100, mock_delta->in_bytes_);

    // Test 3: Handle DTLS alert - should forward to connection
    mock_conn->reset();
    HELPER_EXPECT_SUCCESS(udp_network->on_dtls_alert("warning", "close_notify"));
    EXPECT_STREQ("warning", mock_conn->last_alert_type_.c_str());
    EXPECT_STREQ("close_notify", mock_conn->last_alert_desc_.c_str());

    // Test 4: Initialize with plaintext (no DTLS, no SRTP)
    SrsUniquePtr<SrsRtcUdpNetwork> plaintext_network(new SrsRtcUdpNetwork(mock_conn.get(), mock_delta.get()));
    HELPER_EXPECT_SUCCESS(plaintext_network->initialize(&cfg, false, false));
}

// Test SrsRtcUdpNetwork DTLS handshake completion and SRTP protection
VOID TEST(RtcUdpNetworkTest, DtlsHandshakeAndSrtpProtection)
{
    srs_error_t err;

    // Create mock dependencies
    SrsUniquePtr<MockRtcConnectionForUdpNetwork> mock_conn(new MockRtcConnectionForUdpNetwork());
    SrsUniquePtr<MockEphemeralDelta> mock_delta(new MockEphemeralDelta());

    // Create SrsRtcUdpNetwork with mock dependencies
    SrsUniquePtr<SrsRtcUdpNetwork> udp_network(new SrsRtcUdpNetwork(mock_conn.get(), mock_delta.get()));

    // Initialize with plaintext transport (no DTLS, no SRTP) for testing
    SrsSessionConfig cfg;
    cfg.dtls_role_ = "passive";
    cfg.dtls_version_ = "auto";
    HELPER_EXPECT_SUCCESS(udp_network->initialize(&cfg, false, false));

    // Verify initial state is not established
    EXPECT_FALSE(udp_network->is_establelished());

    // Test 1: First call to on_dtls_handshake_done should succeed and change state
    HELPER_EXPECT_SUCCESS(udp_network->on_dtls_handshake_done());
    EXPECT_TRUE(udp_network->is_establelished());

    // Test 2: Second call to on_dtls_handshake_done should be ignored (ARQ scenario)
    // The state is already established, so it should return success without calling conn_
    HELPER_EXPECT_SUCCESS(udp_network->on_dtls_handshake_done());
    EXPECT_TRUE(udp_network->is_establelished());

    // Test 3: protect_rtp should delegate to transport
    char rtp_packet[1500];
    int nb_cipher = 1500;
    // With plaintext transport, this should succeed without modification
    HELPER_EXPECT_SUCCESS(udp_network->protect_rtp(rtp_packet, &nb_cipher));

    // Test 4: protect_rtcp should delegate to transport
    char rtcp_packet[1500];
    nb_cipher = 1500;
    // With plaintext transport, this should succeed without modification
    HELPER_EXPECT_SUCCESS(udp_network->protect_rtcp(rtcp_packet, &nb_cipher));
}

// Mock ISrsRtcTransport implementation
MockRtcTransportForUdpNetwork::MockRtcTransportForUdpNetwork()
{
    unprotect_rtp_error_ = srs_success;
    unprotect_rtcp_error_ = srs_success;
    unprotect_rtp_called_ = false;
    unprotect_rtcp_called_ = false;
    unprotected_rtp_size_ = 0;
    unprotected_rtcp_size_ = 0;
}

MockRtcTransportForUdpNetwork::~MockRtcTransportForUdpNetwork()
{
}

srs_error_t MockRtcTransportForUdpNetwork::initialize(SrsSessionConfig *cfg)
{
    return srs_success;
}

srs_error_t MockRtcTransportForUdpNetwork::start_active_handshake()
{
    return srs_success;
}

srs_error_t MockRtcTransportForUdpNetwork::on_dtls(char *data, int nb_data)
{
    return srs_success;
}

srs_error_t MockRtcTransportForUdpNetwork::on_dtls_alert(std::string type, std::string desc)
{
    return srs_success;
}

srs_error_t MockRtcTransportForUdpNetwork::protect_rtp(void *packet, int *nb_cipher)
{
    return srs_success;
}

srs_error_t MockRtcTransportForUdpNetwork::protect_rtcp(void *packet, int *nb_cipher)
{
    return srs_success;
}

srs_error_t MockRtcTransportForUdpNetwork::unprotect_rtp(void *packet, int *nb_plaintext)
{
    unprotect_rtp_called_ = true;
    if (unprotect_rtp_error_ != srs_success) {
        return srs_error_copy(unprotect_rtp_error_);
    }
    // Simulate successful unprotection - reduce size slightly (remove SRTP overhead)
    unprotected_rtp_size_ = *nb_plaintext;
    *nb_plaintext = *nb_plaintext - 10; // Simulate SRTP overhead removal
    return srs_success;
}

srs_error_t MockRtcTransportForUdpNetwork::unprotect_rtcp(void *packet, int *nb_plaintext)
{
    unprotect_rtcp_called_ = true;
    if (unprotect_rtcp_error_ != srs_success) {
        return srs_error_copy(unprotect_rtcp_error_);
    }
    // Simulate successful unprotection - reduce size slightly (remove SRTCP overhead)
    unprotected_rtcp_size_ = *nb_plaintext;
    *nb_plaintext = *nb_plaintext - 14; // Simulate SRTCP overhead removal
    return srs_success;
}

srs_error_t MockRtcTransportForUdpNetwork::on_dtls_handshake_done()
{
    return srs_success;
}

srs_error_t MockRtcTransportForUdpNetwork::on_dtls_application_data(const char *data, const int len)
{
    return srs_success;
}

srs_error_t MockRtcTransportForUdpNetwork::write_dtls_data(void *data, int size)
{
    return srs_success;
}

void MockRtcTransportForUdpNetwork::reset()
{
    srs_freep(unprotect_rtp_error_);
    srs_freep(unprotect_rtcp_error_);
    unprotect_rtp_called_ = false;
    unprotect_rtcp_called_ = false;
    unprotected_rtp_size_ = 0;
    unprotected_rtcp_size_ = 0;
}

void MockRtcTransportForUdpNetwork::set_unprotect_rtp_error(srs_error_t err)
{
    srs_freep(unprotect_rtp_error_);
    unprotect_rtp_error_ = srs_error_copy(err);
}

void MockRtcTransportForUdpNetwork::set_unprotect_rtcp_error(srs_error_t err)
{
    srs_freep(unprotect_rtcp_error_);
    unprotect_rtcp_error_ = srs_error_copy(err);
}

// Test SrsRtcUdpNetwork RTP and RTCP packet handling
VOID TEST(RtcUdpNetworkTest, HandleRtpAndRtcpPackets)
{
    srs_error_t err;

    // Create mock dependencies
    SrsUniquePtr<MockRtcConnectionForUdpNetwork> mock_conn(new MockRtcConnectionForUdpNetwork());
    SrsUniquePtr<MockEphemeralDelta> mock_delta(new MockEphemeralDelta());
    MockRtcTransportForUdpNetwork *mock_transport = new MockRtcTransportForUdpNetwork();

    // Create SrsRtcUdpNetwork with mock dependencies
    SrsUniquePtr<SrsRtcUdpNetwork> udp_network(new SrsRtcUdpNetwork(mock_conn.get(), mock_delta.get()));

    // Initialize with plaintext transport
    SrsSessionConfig cfg;
    cfg.dtls_role_ = "passive";
    cfg.dtls_version_ = "auto";
    HELPER_EXPECT_SUCCESS(udp_network->initialize(&cfg, false, false));

    // Inject mock transport
    udp_network->transport_ = mock_transport;

    // Test 1: Handle RTP packet - should update delta, call on_rtp_cipher, unprotect, and on_rtp_plaintext
    char rtp_data[200];
    memset(rtp_data, 0x80, 200); // Fill with RTP-like data
    mock_delta->reset();
    mock_transport->reset();
    mock_conn->reset();

    HELPER_EXPECT_SUCCESS(udp_network->on_rtp(rtp_data, 200));

    // Verify delta was updated with received bytes
    EXPECT_EQ(200, mock_delta->in_bytes_);
    // Verify transport unprotect was called
    EXPECT_TRUE(mock_transport->unprotect_rtp_called_);
    EXPECT_EQ(200, mock_transport->unprotected_rtp_size_);
    // Verify connection callbacks were invoked
    EXPECT_TRUE(mock_conn->on_rtp_cipher_called_);
    EXPECT_TRUE(mock_conn->on_rtp_plaintext_called_);

    // Test 2: Handle RTCP packet - should update delta, unprotect, and call on_rtcp
    char rtcp_data[100];
    memset(rtcp_data, 0xC8, 100); // Fill with RTCP-like data
    mock_delta->reset();
    mock_transport->reset();
    mock_conn->reset();

    HELPER_EXPECT_SUCCESS(udp_network->on_rtcp(rtcp_data, 100));

    // Verify delta was updated with received bytes
    EXPECT_EQ(100, mock_delta->in_bytes_);
    // Verify transport unprotect was called
    EXPECT_TRUE(mock_transport->unprotect_rtcp_called_);
    EXPECT_EQ(100, mock_transport->unprotected_rtcp_size_);
    // Verify connection callback was invoked
    EXPECT_TRUE(mock_conn->on_rtcp_called_);

    // Test 3: Handle RTP unprotect failure - should return error
    mock_transport->reset();
    mock_transport->set_unprotect_rtp_error(srs_error_new(ERROR_RTC_SRTP_UNPROTECT, "mock unprotect error"));
    HELPER_EXPECT_FAILED(udp_network->on_rtp(rtp_data, 200));

    // Test 4: Handle RTCP unprotect failure - should return error
    mock_transport->reset();
    mock_transport->set_unprotect_rtcp_error(srs_error_new(ERROR_RTC_SRTP_UNPROTECT, "mock unprotect error"));
    HELPER_EXPECT_FAILED(udp_network->on_rtcp(rtcp_data, 100));

    // Clean up - set to NULL to avoid double-free
    udp_network->transport_ = NULL;
    srs_freep(mock_transport);
}

// Mock ISrsResourceManager implementation for testing SrsRtcUdpNetwork::update_sendonly_socket
MockResourceManagerForUdpNetwork::MockResourceManagerForUdpNetwork()
{
}

MockResourceManagerForUdpNetwork::~MockResourceManagerForUdpNetwork()
{
}

srs_error_t MockResourceManagerForUdpNetwork::start()
{
    return srs_success;
}

bool MockResourceManagerForUdpNetwork::empty()
{
    return id_map_.empty();
}

size_t MockResourceManagerForUdpNetwork::size()
{
    return id_map_.size();
}

void MockResourceManagerForUdpNetwork::add(ISrsResource *conn, bool *exists)
{
}

void MockResourceManagerForUdpNetwork::add_with_id(const std::string &id, ISrsResource *conn)
{
    id_map_[id] = conn;
}

void MockResourceManagerForUdpNetwork::add_with_fast_id(uint64_t id, ISrsResource *conn)
{
    fast_id_map_[id] = conn;
}

void MockResourceManagerForUdpNetwork::add_with_name(const std::string & /*name*/, ISrsResource * /*conn*/)
{
}

ISrsResource *MockResourceManagerForUdpNetwork::at(int index)
{
    return NULL;
}

ISrsResource *MockResourceManagerForUdpNetwork::find_by_id(std::string id)
{
    std::map<std::string, ISrsResource *>::iterator it = id_map_.find(id);
    if (it != id_map_.end()) {
        return it->second;
    }
    return NULL;
}

ISrsResource *MockResourceManagerForUdpNetwork::find_by_fast_id(uint64_t id)
{
    std::map<uint64_t, ISrsResource *>::iterator it = fast_id_map_.find(id);
    if (it != fast_id_map_.end()) {
        return it->second;
    }
    return NULL;
}

ISrsResource *MockResourceManagerForUdpNetwork::find_by_name(std::string name)
{
    return NULL;
}

void MockResourceManagerForUdpNetwork::remove(ISrsResource *c)
{
}

void MockResourceManagerForUdpNetwork::subscribe(ISrsDisposingHandler *h)
{
}

void MockResourceManagerForUdpNetwork::unsubscribe(ISrsDisposingHandler *h)
{
}

void MockResourceManagerForUdpNetwork::reset()
{
    id_map_.clear();
    fast_id_map_.clear();
}

// Test SrsRtcUdpNetwork STUN binding request handling
VOID TEST(RtcUdpNetworkTest, HandleStunBindingRequest)
{
    srs_error_t err;

    // Create mock dependencies
    SrsUniquePtr<MockRtcConnectionForUdpNetwork> mock_conn(new MockRtcConnectionForUdpNetwork());
    SrsUniquePtr<MockEphemeralDelta> mock_delta(new MockEphemeralDelta());
    MockUdpMuxSocket *mock_socket = new MockUdpMuxSocket();

    // Create UDP network
    SrsUniquePtr<SrsRtcUdpNetwork> udp_network(new SrsRtcUdpNetwork(mock_conn.get(), mock_delta.get()));

    // Initialize with plaintext transport for testing
    SrsSessionConfig cfg;
    cfg.dtls_role_ = "passive";
    cfg.dtls_version_ = "auto";
    HELPER_EXPECT_SUCCESS(udp_network->initialize(&cfg, false, false));

    // Set the mock socket for UDP network to use
    udp_network->sendonly_skt_ = mock_socket;

    // Create a STUN binding request packet
    SrsUniquePtr<SrsStunPacket> stun_request(new SrsStunPacket());
    stun_request->set_message_type(BindingRequest);
    stun_request->set_local_ufrag("local_user");
    stun_request->set_remote_ufrag("remote_user");
    stun_request->set_transcation_id("transaction123");

    // Create dummy STUN data buffer
    char request_buf[100];
    memset(request_buf, 0, sizeof(request_buf));

    // Test 1: Handle non-binding STUN request (should be ignored and return success)
    SrsUniquePtr<SrsStunPacket> stun_response(new SrsStunPacket());
    stun_response->set_message_type(BindingResponse);

    mock_delta->reset();
    mock_socket->reset();
    HELPER_EXPECT_SUCCESS(udp_network->on_stun(stun_response.get(), request_buf, sizeof(request_buf)));

    // Should not send any response for non-binding-request, so no delta change and no sendto calls
    EXPECT_EQ(0, mock_delta->out_bytes_);
    EXPECT_EQ(0, mock_socket->sendto_called_count_);

    // Test 2: Handle STUN binding request - should create and send response
    mock_delta->reset();
    mock_socket->reset();
    HELPER_EXPECT_SUCCESS(udp_network->on_stun(stun_request.get(), request_buf, sizeof(request_buf)));

    // Verify that:
    // 1. conn_->on_binding_request() was called (implicitly - no error returned)
    // 2. A STUN binding response was created and sent
    // 3. Delta was updated with sent bytes
    // 4. Socket sendto was called
    EXPECT_GT(mock_delta->out_bytes_, 0);
    EXPECT_EQ(1, mock_socket->sendto_called_count_);
    EXPECT_GT(mock_socket->last_sendto_size_, 0);

    // Test 3: Handle STUN binding request with sendto error
    mock_delta->reset();
    mock_socket->reset();
    mock_socket->set_sendto_error(srs_error_new(ERROR_SOCKET_WRITE, "mock sendto error"));

    err = udp_network->on_stun(stun_request.get(), request_buf, sizeof(request_buf));
    HELPER_EXPECT_FAILED(err);

    // Even though sendto failed, delta should still be updated (happens before write)
    EXPECT_GT(mock_delta->out_bytes_, 0);
    EXPECT_EQ(1, mock_socket->sendto_called_count_);

    // Clean up - set to NULL to avoid double-free
    udp_network->sendonly_skt_ = NULL;
    srs_freep(mock_socket);
}

// Test SrsRtcUdpNetwork update_sendonly_socket, get_peer_ip, and get_peer_port
VOID TEST(RtcUdpNetworkTest, UpdateSendonlySocketAndGetPeerInfo)
{
    // Create mock dependencies
    SrsUniquePtr<MockRtcConnectionForUdpNetwork> mock_conn(new MockRtcConnectionForUdpNetwork());
    SrsUniquePtr<MockEphemeralDelta> mock_delta(new MockEphemeralDelta());
    SrsUniquePtr<MockResourceManagerForUdpNetwork> mock_manager(new MockResourceManagerForUdpNetwork());

    // Create UDP network
    SrsUniquePtr<SrsRtcUdpNetwork> udp_network(new SrsRtcUdpNetwork(mock_conn.get(), mock_delta.get()));

    // Inject mock resource manager
    udp_network->conn_manager_ = mock_manager.get();

    // Create mock UDP socket with peer address 192.168.1.100:5000
    MockUdpMuxSocket *mock_socket1 = new MockUdpMuxSocket();
    mock_socket1->peer_ip_ = "192.168.1.100";
    mock_socket1->peer_port_ = 5000;
    mock_socket1->peer_id_ = "192.168.1.100:5000";
    mock_socket1->fast_id_ = 0x1388c0a80164ULL; // port 5000 << 48 | IP 192.168.1.100

    std::string peer_id1 = mock_socket1->peer_id();
    EXPECT_FALSE(peer_id1.empty());
    EXPECT_STREQ("192.168.1.100:5000", peer_id1.c_str());

    // Test 1: First update - should initialize sendonly_skt_
    udp_network->update_sendonly_socket(mock_socket1);

    // Verify sendonly_skt_ is set
    EXPECT_TRUE(udp_network->sendonly_skt_ != NULL);

    // Test 2: Verify get_peer_ip and get_peer_port return correct values
    std::string peer_ip = udp_network->get_peer_ip();
    int peer_port = udp_network->get_peer_port();
    EXPECT_STREQ("192.168.1.100", peer_ip.c_str());
    EXPECT_EQ(5000, peer_port);

    // Test 3: Verify peer address is cached
    EXPECT_EQ(1u, udp_network->peer_addresses_.size());

    // Test 4: Verify connection manager was called with peer_id
    EXPECT_TRUE(mock_manager->id_map_.find(peer_id1) != mock_manager->id_map_.end());
    EXPECT_EQ(mock_conn.get(), mock_manager->id_map_[peer_id1]);

    // Test 5: Verify fast_id was registered
    EXPECT_TRUE(mock_manager->fast_id_map_.find(mock_socket1->fast_id_) != mock_manager->fast_id_map_.end());

    // Test 6: Update with same address - should be ignored (no new cache entry)
    udp_network->update_sendonly_socket(mock_socket1);
    EXPECT_EQ(1u, udp_network->peer_addresses_.size());

    // Test 7: Update with different address - should create new cache entry
    MockUdpMuxSocket *mock_socket2 = new MockUdpMuxSocket();
    mock_socket2->peer_ip_ = "192.168.1.101";
    mock_socket2->peer_port_ = 6000;
    mock_socket2->peer_id_ = "192.168.1.101:6000";
    mock_socket2->fast_id_ = 0x1770c0a80165ULL; // port 6000 << 48 | IP 192.168.1.101

    std::string peer_id2 = mock_socket2->peer_id();
    EXPECT_FALSE(peer_id2.empty());
    EXPECT_STREQ("192.168.1.101:6000", peer_id2.c_str());
    EXPECT_NE(peer_id1, peer_id2);

    udp_network->update_sendonly_socket(mock_socket2);

    // Verify new peer info
    peer_ip = udp_network->get_peer_ip();
    peer_port = udp_network->get_peer_port();
    EXPECT_STREQ("192.168.1.101", peer_ip.c_str());
    EXPECT_EQ(6000, peer_port);

    // Verify both addresses are cached
    EXPECT_EQ(2u, udp_network->peer_addresses_.size());
    EXPECT_TRUE(mock_manager->id_map_.find(peer_id2) != mock_manager->id_map_.end());

    // Clean up - clear peer_addresses_ map before destroying udp_network
    // to avoid use-after-free when udp_network destructor tries to free the cached sockets
    udp_network->peer_addresses_.clear();
    udp_network->conn_manager_ = NULL;
    udp_network->sendonly_skt_ = NULL;

    // Now safe to free the mock sockets
    srs_freep(mock_socket1);
    srs_freep(mock_socket2);
}

// Test SrsRtcTcpNetwork major use scenario: DTLS handshake and RTP/RTCP protection
VOID TEST(RtcTcpNetworkTest, DtlsHandshakeAndPacketProtection)
{
    srs_error_t err;

    // Create mock dependencies
    SrsUniquePtr<MockRtcConnectionForUdpNetwork> mock_conn(new MockRtcConnectionForUdpNetwork());
    SrsUniquePtr<MockEphemeralDelta> mock_delta(new MockEphemeralDelta());
    MockRtcTransportForUdpNetwork *mock_transport = new MockRtcTransportForUdpNetwork();

    // Create SrsRtcTcpNetwork with mock dependencies
    SrsUniquePtr<SrsRtcTcpNetwork> tcp_network(new SrsRtcTcpNetwork(mock_conn.get(), mock_delta.get()));

    // Inject mock transport to replace the real transport
    srs_freep(tcp_network->transport_);
    tcp_network->transport_ = mock_transport;

    // Test 1: update_sendonly_socket - should update the sendonly socket reference
    SrsUniquePtr<MockEmptyIO> mock_io(new MockEmptyIO());
    tcp_network->update_sendonly_socket(mock_io.get());
    EXPECT_TRUE(tcp_network->sendonly_skt_ != NULL);

    // Test 2: on_dtls_handshake_done - first call should succeed and change state to Established
    HELPER_EXPECT_SUCCESS(tcp_network->on_dtls_handshake_done());
    EXPECT_EQ(SrsRtcNetworkStateEstablished, tcp_network->state_);

    // Test 3: on_dtls_handshake_done - second call should be ignored (ARQ scenario)
    // The state is already established, so it should return success without calling conn_
    HELPER_EXPECT_SUCCESS(tcp_network->on_dtls_handshake_done());
    EXPECT_EQ(SrsRtcNetworkStateEstablished, tcp_network->state_);

    // Test 4: on_dtls_alert - should forward to connection
    mock_conn->reset();
    HELPER_EXPECT_SUCCESS(tcp_network->on_dtls_alert("warning", "close_notify"));
    EXPECT_STREQ("warning", mock_conn->last_alert_type_.c_str());
    EXPECT_STREQ("close_notify", mock_conn->last_alert_desc_.c_str());

    // Test 5: protect_rtp - should delegate to transport
    char rtp_packet[1500];
    memset(rtp_packet, 0x80, sizeof(rtp_packet));
    int nb_cipher = 1500;
    HELPER_EXPECT_SUCCESS(tcp_network->protect_rtp(rtp_packet, &nb_cipher));

    // Test 6: protect_rtcp - should delegate to transport
    char rtcp_packet[1500];
    memset(rtcp_packet, 0xC8, sizeof(rtcp_packet));
    nb_cipher = 1500;
    HELPER_EXPECT_SUCCESS(tcp_network->protect_rtcp(rtcp_packet, &nb_cipher));

    // Clean up
    tcp_network->sendonly_skt_ = NULL;
}

// Mock ISrsProtocolReadWriter implementation
MockProtocolReadWriterForTcpNetwork::MockProtocolReadWriterForTcpNetwork()
{
    write_error_ = srs_success;
    send_bytes_ = 0;
    recv_bytes_ = 0;
    send_timeout_ = SRS_UTIME_NO_TIMEOUT;
    recv_timeout_ = SRS_UTIME_NO_TIMEOUT;
    read_pos_ = 0;
}

MockProtocolReadWriterForTcpNetwork::~MockProtocolReadWriterForTcpNetwork()
{
    srs_freep(write_error_);
}

srs_error_t MockProtocolReadWriterForTcpNetwork::read_fully(void *buf, size_t size, ssize_t *nread)
{
    if (read_pos_ + size > read_data_.size()) {
        return srs_error_new(ERROR_SOCKET_READ, "not enough data");
    }

    memcpy(buf, read_data_.data() + read_pos_, size);
    read_pos_ += size;
    if (nread)
        *nread = size;
    recv_bytes_ += size;

    return srs_success;
}

srs_error_t MockProtocolReadWriterForTcpNetwork::write(void *buf, size_t size, ssize_t *nwrite)
{
    if (write_error_ != srs_success) {
        return srs_error_copy(write_error_);
    }

    // Store written data for verification
    written_data_.push_back(std::string((char *)buf, size));
    send_bytes_ += size;

    if (nwrite) {
        *nwrite = size;
    }

    return srs_success;
}

void MockProtocolReadWriterForTcpNetwork::set_recv_timeout(srs_utime_t tm)
{
    recv_timeout_ = tm;
}

srs_utime_t MockProtocolReadWriterForTcpNetwork::get_recv_timeout()
{
    return recv_timeout_;
}

int64_t MockProtocolReadWriterForTcpNetwork::get_recv_bytes()
{
    return recv_bytes_;
}

void MockProtocolReadWriterForTcpNetwork::set_send_timeout(srs_utime_t tm)
{
    send_timeout_ = tm;
}

srs_utime_t MockProtocolReadWriterForTcpNetwork::get_send_timeout()
{
    return send_timeout_;
}

int64_t MockProtocolReadWriterForTcpNetwork::get_send_bytes()
{
    return send_bytes_;
}

srs_error_t MockProtocolReadWriterForTcpNetwork::writev(const iovec *iov, int iov_size, ssize_t *nwrite)
{
    return srs_success;
}

srs_error_t MockProtocolReadWriterForTcpNetwork::read(void *buf, size_t size, ssize_t *nread)
{
    return srs_success;
}

void MockProtocolReadWriterForTcpNetwork::reset()
{
    written_data_.clear();
    srs_freep(write_error_);
    send_bytes_ = 0;
    recv_bytes_ = 0;
    read_data_.clear();
    read_pos_ = 0;
}

void MockProtocolReadWriterForTcpNetwork::set_write_error(srs_error_t err)
{
    srs_freep(write_error_);
    write_error_ = srs_error_copy(err);
}

void MockProtocolReadWriterForTcpNetwork::set_read_data(const std::string &data)
{
    read_data_ = data;
    read_pos_ = 0;
}

// Test SrsRtcTcpNetwork initialize, on_dtls, on_rtp, on_rtcp - major use scenario
VOID TEST(RtcTcpNetworkTest, InitializeAndHandlePackets)
{
    srs_error_t err;

    // Create mock dependencies
    SrsUniquePtr<MockRtcConnectionForUdpNetwork> mock_conn(new MockRtcConnectionForUdpNetwork());
    SrsUniquePtr<MockEphemeralDelta> mock_delta(new MockEphemeralDelta());
    MockRtcTransportForUdpNetwork *mock_transport = new MockRtcTransportForUdpNetwork();

    // Create SrsRtcTcpNetwork with mock dependencies
    SrsUniquePtr<SrsRtcTcpNetwork> tcp_network(new SrsRtcTcpNetwork(mock_conn.get(), mock_delta.get()));

    // Test 1: Initialize with DTLS but no SRTP (should use SrsSemiSecurityTransport)
    SrsSessionConfig cfg;
    cfg.dtls_role_ = "passive";
    cfg.dtls_version_ = "auto";
    HELPER_EXPECT_SUCCESS(tcp_network->initialize(&cfg, true, false));

    // Replace transport with mock for testing
    srs_freep(tcp_network->transport_);
    tcp_network->transport_ = mock_transport;

    // Test 2: on_dtls - should update delta statistics and forward to transport
    char dtls_data[100];
    memset(dtls_data, 0x16, sizeof(dtls_data)); // DTLS handshake packet marker
    mock_delta->reset();
    mock_transport->reset();
    err = tcp_network->on_dtls(dtls_data, 100);
    srs_freep(err); // Ignore error from mock transport
    EXPECT_EQ(100, mock_delta->in_bytes_);

    // Test 3: on_rtp - should update delta, call on_rtp_cipher, unprotect, and on_rtp_plaintext
    char rtp_data[200];
    memset(rtp_data, 0x80, sizeof(rtp_data)); // RTP packet marker
    mock_delta->reset();
    mock_transport->reset();
    mock_conn->reset();
    mock_transport->unprotected_rtp_size_ = 200; // Mock unprotect to keep same size
    HELPER_EXPECT_SUCCESS(tcp_network->on_rtp(rtp_data, 200));
    EXPECT_EQ(200, mock_delta->in_bytes_);
    EXPECT_TRUE(mock_transport->unprotect_rtp_called_);
    EXPECT_TRUE(mock_conn->on_rtp_cipher_called_);
    EXPECT_TRUE(mock_conn->on_rtp_plaintext_called_);

    // Test 4: on_rtcp - should update delta, unprotect, and call on_rtcp
    char rtcp_data[100];
    memset(rtcp_data, 0xC8, sizeof(rtcp_data)); // RTCP packet marker
    mock_delta->reset();
    mock_transport->reset();
    mock_conn->reset();
    mock_transport->unprotected_rtcp_size_ = 100; // Mock unprotect to keep same size
    HELPER_EXPECT_SUCCESS(tcp_network->on_rtcp(rtcp_data, 100));
    EXPECT_EQ(100, mock_delta->in_bytes_);
    EXPECT_TRUE(mock_transport->unprotect_rtcp_called_);
    EXPECT_TRUE(mock_conn->on_rtcp_called_);

    // Test 5: set_state and is_establelished
    tcp_network->set_state(SrsRtcNetworkStateEstablished);
    EXPECT_TRUE(tcp_network->is_establelished());

    // Test 6: get_peer_ip and get_peer_port
    tcp_network->set_peer_id("192.168.1.100", 5000);
    EXPECT_STREQ("192.168.1.100", tcp_network->get_peer_ip().c_str());
    EXPECT_EQ(5000, tcp_network->get_peer_port());

    // Clean up
    tcp_network->transport_ = NULL;
    srs_freep(mock_transport);
}

// Test SrsRtcTcpNetwork::on_stun with STUN binding request
VOID TEST(RtcTcpNetworkTest, HandleStunBindingRequest)
{
    srs_error_t err;

    // Create mock dependencies
    SrsUniquePtr<MockRtcConnectionForUdpNetwork> mock_conn(new MockRtcConnectionForUdpNetwork());
    SrsUniquePtr<MockEphemeralDelta> mock_delta(new MockEphemeralDelta());
    MockRtcTransportForUdpNetwork *mock_transport = new MockRtcTransportForUdpNetwork();
    SrsUniquePtr<MockProtocolReadWriterForTcpNetwork> mock_io(new MockProtocolReadWriterForTcpNetwork());

    // Create SrsRtcTcpNetwork with mock dependencies
    SrsUniquePtr<SrsRtcTcpNetwork> tcp_network(new SrsRtcTcpNetwork(mock_conn.get(), mock_delta.get()));

    // Inject mock transport and socket
    srs_freep(tcp_network->transport_);
    tcp_network->transport_ = mock_transport;
    tcp_network->update_sendonly_socket(mock_io.get());
    tcp_network->set_peer_id("192.168.1.100", 5000);

    // Set initial state to WaitingStun
    tcp_network->set_state(SrsRtcNetworkStateWaitingStun);

    // Create a STUN binding request packet
    SrsUniquePtr<SrsStunPacket> stun_request(new SrsStunPacket());
    stun_request->set_message_type(BindingRequest);
    stun_request->set_local_ufrag("local_user");
    stun_request->set_remote_ufrag("remote_user");
    stun_request->set_transcation_id("transaction123");

    // Create dummy STUN request data (we don't need real encoded data for this test)
    char request_buf[100];
    memset(request_buf, 0, sizeof(request_buf));
    int request_size = 20; // Minimal STUN header size

    // Test: Handle STUN binding request
    // This should:
    // 1. Call conn_->on_binding_request to get ice_pwd
    // 2. Create and encode a STUN binding response
    // 3. Write the response via write()
    // 4. Transition state from WaitingStun to Dtls
    // 5. Start DTLS handshake
    mock_io->reset();
    HELPER_EXPECT_SUCCESS(tcp_network->on_stun(stun_request.get(), request_buf, request_size));

    // Verify state transitioned to Dtls
    EXPECT_EQ(SrsRtcNetworkStateDtls, tcp_network->state_);

    // Verify that data was written (STUN binding response)
    // The write() method is called with 2-byte length prefix + STUN response data
    EXPECT_TRUE(mock_io->written_data_.size() >= 2);

    // First write should be 2-byte length prefix
    EXPECT_EQ(2, (int)mock_io->written_data_[0].size());

    // Second write should be the STUN response data
    EXPECT_TRUE(mock_io->written_data_[1].size() > 0);

    // Clean up
    tcp_network->sendonly_skt_ = NULL;
}

// Test SrsRtcTcpConn setup_owner, delta, interrupt, desc, get_id, remote_ip, and on_executor_done
VOID TEST(RtcTcpConnTest, SetupOwnerAndBasicMethods)
{
    // Create mock dependencies
    SrsUniquePtr<MockInterruptableForRtcTcpConn> mock_coroutine(new MockInterruptableForRtcTcpConn());
    SrsUniquePtr<MockContextIdSetterForRtcTcpConn> mock_cid_setter(new MockContextIdSetterForRtcTcpConn());
    SrsUniquePtr<MockRtcConnectionForTcpConn> mock_session(new MockRtcConnectionForTcpConn());

    // Create SrsRtcTcpConn with IP and port
    std::string test_ip = "192.168.1.100";
    int test_port = 8080;
    SrsUniquePtr<SrsRtcTcpConn> tcp_conn(new SrsRtcTcpConn(NULL, test_ip, test_port));

    // Test 1: setup_owner - set wrapper, owner_coroutine, and owner_cid
    SrsSharedResource<ISrsRtcTcpConn> *mock_wrapper = NULL;
    tcp_conn->setup_owner(mock_wrapper, mock_coroutine.get(), mock_cid_setter.get());
    EXPECT_EQ(mock_wrapper, tcp_conn->wrapper_);
    EXPECT_EQ(mock_coroutine.get(), tcp_conn->owner_coroutine_);
    EXPECT_EQ(mock_cid_setter.get(), tcp_conn->owner_cid_);

    // Test 2: delta() - should return the delta object
    ISrsKbpsDelta *delta = tcp_conn->delta();
    EXPECT_TRUE(delta != NULL);

    // Test 3: desc() - should return "Tcp"
    EXPECT_STREQ("Tcp", tcp_conn->desc().c_str());

    // Test 4: get_id() - should return the context id
    const SrsContextId &cid = tcp_conn->get_id();
    EXPECT_TRUE(!cid.empty());

    // Test 5: remote_ip() - should return the IP address
    EXPECT_STREQ(test_ip.c_str(), tcp_conn->remote_ip().c_str());

    // Test 6: interrupt() - should set session to NULL and call owner_coroutine->interrupt()
    tcp_conn->session_ = mock_session.get();
    EXPECT_FALSE(mock_coroutine->interrupt_called_);
    tcp_conn->interrupt();
    EXPECT_TRUE(mock_coroutine->interrupt_called_);
    EXPECT_TRUE(tcp_conn->session_ == NULL);

    // Test 7: on_executor_done() - should set owner_coroutine to NULL
    tcp_conn->owner_coroutine_ = mock_coroutine.get();
    EXPECT_TRUE(tcp_conn->owner_coroutine_ != NULL);
    tcp_conn->on_executor_done(mock_coroutine.get());
    EXPECT_TRUE(tcp_conn->owner_coroutine_ == NULL);
}

// Mock ISrsResourceManager for testing SrsRtcTcpConn::handshake
MockResourceManagerForTcpConnHandshake::MockResourceManagerForTcpConnHandshake()
{
    session_to_return_ = NULL;
}

MockResourceManagerForTcpConnHandshake::~MockResourceManagerForTcpConnHandshake()
{
}

srs_error_t MockResourceManagerForTcpConnHandshake::start()
{
    return srs_success;
}

bool MockResourceManagerForTcpConnHandshake::empty()
{
    return true;
}

size_t MockResourceManagerForTcpConnHandshake::size()
{
    return 0;
}

void MockResourceManagerForTcpConnHandshake::add(ISrsResource *conn, bool *exists)
{
}

void MockResourceManagerForTcpConnHandshake::add_with_id(const std::string &id, ISrsResource *conn)
{
}

void MockResourceManagerForTcpConnHandshake::add_with_fast_id(uint64_t id, ISrsResource *conn)
{
}

void MockResourceManagerForTcpConnHandshake::add_with_name(const std::string & /*name*/, ISrsResource * /*conn*/)
{
}

ISrsResource *MockResourceManagerForTcpConnHandshake::at(int index)
{
    return NULL;
}

ISrsResource *MockResourceManagerForTcpConnHandshake::find_by_id(std::string id)
{
    return NULL;
}

ISrsResource *MockResourceManagerForTcpConnHandshake::find_by_fast_id(uint64_t id)
{
    return NULL;
}

ISrsResource *MockResourceManagerForTcpConnHandshake::find_by_name(std::string name)
{
    return session_to_return_;
}

void MockResourceManagerForTcpConnHandshake::remove(ISrsResource *c)
{
}

void MockResourceManagerForTcpConnHandshake::subscribe(ISrsDisposingHandler *h)
{
}

void MockResourceManagerForTcpConnHandshake::unsubscribe(ISrsDisposingHandler *h)
{
}

void MockResourceManagerForTcpConnHandshake::reset()
{
    session_to_return_ = NULL;
}

// Mock ISrsRtcConnection for testing SrsRtcTcpConn::handshake
MockRtcConnectionForTcpConnHandshake::MockRtcConnectionForTcpConnHandshake()
{
    tcp_network_ = NULL;
    ice_pwd_ = "test_ice_pwd";
    switch_to_context_called_ = false;
    on_binding_request_called_ = false;
}

MockRtcConnectionForTcpConnHandshake::~MockRtcConnectionForTcpConnHandshake()
{
}

const SrsContextId &MockRtcConnectionForTcpConnHandshake::get_id()
{
    static SrsContextId cid;
    return cid;
}

std::string MockRtcConnectionForTcpConnHandshake::desc()
{
    return "MockRtcConnectionForTcpConnHandshake";
}

void MockRtcConnectionForTcpConnHandshake::on_disposing(ISrsResource *c)
{
}

void MockRtcConnectionForTcpConnHandshake::on_before_dispose(ISrsResource *c)
{
}

void MockRtcConnectionForTcpConnHandshake::expire()
{
}

srs_error_t MockRtcConnectionForTcpConnHandshake::send_rtcp(char *data, int nb_data)
{
    return srs_success;
}

srs_error_t MockRtcConnectionForTcpConnHandshake::send_rtcp_rr(uint32_t ssrc, SrsRtpRingBuffer *rtp_queue, const uint64_t &last_send_systime, const SrsNtp &last_send_ntp)
{
    return srs_success;
}

srs_error_t MockRtcConnectionForTcpConnHandshake::send_rtcp_xr_rrtr(uint32_t ssrc)
{
    return srs_success;
}

void MockRtcConnectionForTcpConnHandshake::check_send_nacks(SrsRtpNackForReceiver *nack, uint32_t ssrc, uint32_t &sent_nacks, uint32_t &timeout_nacks)
{
}

srs_error_t MockRtcConnectionForTcpConnHandshake::send_rtcp_fb_pli(uint32_t ssrc, const SrsContextId &cid_of_subscriber)
{
    return srs_success;
}

srs_error_t MockRtcConnectionForTcpConnHandshake::do_send_packet(SrsRtpPacket *pkt)
{
    return srs_success;
}

srs_error_t MockRtcConnectionForTcpConnHandshake::do_check_send_nacks()
{
    return srs_success;
}

void MockRtcConnectionForTcpConnHandshake::on_connection_established()
{
}

srs_error_t MockRtcConnectionForTcpConnHandshake::on_dtls_alert(std::string type, std::string desc)
{
    return srs_success;
}

srs_error_t MockRtcConnectionForTcpConnHandshake::on_dtls_handshake_done()
{
    return srs_success;
}

srs_error_t MockRtcConnectionForTcpConnHandshake::on_dtls_application_data(const char *data, const int len)
{
    return srs_success;
}

srs_error_t MockRtcConnectionForTcpConnHandshake::on_rtp_cipher(char *data, int nb_data)
{
    return srs_success;
}

srs_error_t MockRtcConnectionForTcpConnHandshake::on_rtp_plaintext(char *buf, int nb_buf)
{
    return srs_success;
}

srs_error_t MockRtcConnectionForTcpConnHandshake::on_rtcp(char *buf, int nb_buf)
{
    return srs_success;
}

srs_error_t MockRtcConnectionForTcpConnHandshake::on_binding_request(SrsStunPacket *r, std::string &ice_pwd)
{
    on_binding_request_called_ = true;
    ice_pwd = ice_pwd_;
    return srs_success;
}

ISrsRtcNetwork *MockRtcConnectionForTcpConnHandshake::udp()
{
    return NULL;
}

ISrsRtcNetwork *MockRtcConnectionForTcpConnHandshake::tcp()
{
    return tcp_network_;
}

void MockRtcConnectionForTcpConnHandshake::alive()
{
}

bool MockRtcConnectionForTcpConnHandshake::is_alive()
{
    return true;
}

bool MockRtcConnectionForTcpConnHandshake::is_disposing()
{
    return false;
}

void MockRtcConnectionForTcpConnHandshake::switch_to_context()
{
    switch_to_context_called_ = true;
}

srs_error_t MockRtcConnectionForTcpConnHandshake::add_publisher(SrsRtcUserConfig * /*ruc*/, SrsSdp & /*local_sdp*/)
{
    return srs_success;
}

srs_error_t MockRtcConnectionForTcpConnHandshake::add_player(SrsRtcUserConfig * /*ruc*/, SrsSdp & /*local_sdp*/)
{
    return srs_success;
}

void MockRtcConnectionForTcpConnHandshake::set_all_tracks_status(std::string /*stream_uri*/, bool /*is_publish*/, bool /*status*/)
{
}

void MockRtcConnectionForTcpConnHandshake::set_remote_sdp(const SrsSdp & /*sdp*/)
{
}

void MockRtcConnectionForTcpConnHandshake::set_local_sdp(const SrsSdp & /*sdp*/)
{
}

void MockRtcConnectionForTcpConnHandshake::set_state_as_waiting_stun()
{
}

srs_error_t MockRtcConnectionForTcpConnHandshake::initialize(ISrsRequest * /*r*/, bool /*dtls*/, bool /*srtp*/, std::string /*username*/)
{
    return srs_success;
}

std::string MockRtcConnectionForTcpConnHandshake::username()
{
    return "";
}

std::string MockRtcConnectionForTcpConnHandshake::token()
{
    return "";
}

void MockRtcConnectionForTcpConnHandshake::set_publish_token(SrsSharedPtr<ISrsStreamPublishToken> /*publish_token*/)
{
}

void MockRtcConnectionForTcpConnHandshake::simulate_nack_drop(int /*nn*/)
{
}

srs_error_t MockRtcConnectionForTcpConnHandshake::generate_local_sdp(SrsRtcUserConfig * /*ruc*/, SrsSdp & /*local_sdp*/, std::string & /*username*/)
{
    return srs_success;
}

void MockRtcConnectionForTcpConnHandshake::reset()
{
    tcp_network_ = NULL;
    ice_pwd_ = "test_ice_pwd";
    switch_to_context_called_ = false;
    on_binding_request_called_ = false;
}

// Test SrsRtcTcpConn::handshake - major use scenario
VOID TEST(RtcTcpConnTest, HandshakeWithStunBindingRequest)
{
    srs_error_t err;

    // Create a STUN binding request packet manually
    // STUN header: message type (2) + message length (2) + magic cookie (4) + transaction ID (12) = 20 bytes
    // Plus username attribute: type (2) + length (2) + value (padded to 4-byte boundary)
    char stun_buf[128];
    memset(stun_buf, 0, sizeof(stun_buf));

    // STUN header
    stun_buf[0] = 0x00;
    stun_buf[1] = 0x01; // BindingRequest
    stun_buf[2] = 0x00;
    stun_buf[3] = 0x10; // message length = 16 (username attribute: 4 + 12)

    // Magic cookie (0x2112A442)
    stun_buf[4] = 0x21;
    stun_buf[5] = 0x12;
    stun_buf[6] = 0xA4;
    stun_buf[7] = 0x42;

    // Transaction ID (12 bytes)
    for (int i = 8; i < 20; i++) {
        stun_buf[i] = i - 8;
    }

    // Username attribute: type=0x0006, length=12, value="test:session"
    stun_buf[20] = 0x00;
    stun_buf[21] = 0x06; // Username attribute type
    stun_buf[22] = 0x00;
    stun_buf[23] = 0x0C; // Length = 12
    memcpy(stun_buf + 24, "test:session", 12);

    int stun_size = 36; // 20 (header) + 16 (username attribute)

    // Prepare read data: 2-byte length prefix + STUN packet
    std::string read_data;
    uint8_t len_prefix[2];
    len_prefix[0] = (stun_size >> 8) & 0xFF;
    len_prefix[1] = stun_size & 0xFF;
    read_data.append((char *)len_prefix, 2);
    read_data.append(stun_buf, stun_size);

    // Create mock socket with read data
    SrsUniquePtr<MockProtocolReadWriterForTcpNetwork> mock_io(new MockProtocolReadWriterForTcpNetwork());
    mock_io->set_read_data(read_data);

    // Create mock resource manager
    SrsUniquePtr<MockResourceManagerForTcpConnHandshake> mock_conn_manager(new MockResourceManagerForTcpConnHandshake());

    // Create mock RTC connection with TCP network
    SrsUniquePtr<MockRtcConnectionForTcpConnHandshake> mock_session(new MockRtcConnectionForTcpConnHandshake());
    SrsUniquePtr<MockEphemeralDelta> mock_delta(new MockEphemeralDelta());
    SrsUniquePtr<SrsRtcTcpNetwork> tcp_network(new SrsRtcTcpNetwork(mock_session.get(), mock_delta.get()));

    // Set up mock session to return the TCP network
    mock_session->tcp_network_ = tcp_network.get();

    // Set up mock resource manager to return the session by username
    mock_conn_manager->session_to_return_ = mock_session.get();

    // Create SrsRtcTcpConn with mock socket - wrapper will own it
    std::string test_ip = "192.168.1.100";
    int test_port = 8080;
    SrsRtcTcpConn *tcp_conn = new SrsRtcTcpConn(mock_io.get(), test_ip, test_port);

    // Create wrapper for shared resource
    SrsUniquePtr<SrsSharedResource<ISrsRtcTcpConn> > wrapper(new SrsSharedResource<ISrsRtcTcpConn>(tcp_conn));

    // Inject mock resource manager
    tcp_conn->conn_manager_ = mock_conn_manager.get();
    tcp_conn->wrapper_ = wrapper.get();

    // Test: Execute handshake
    // This should:
    // 1. Read STUN packet from socket
    // 2. Decode STUN packet
    // 3. Find session by username from conn_manager
    // 4. Call session->switch_to_context()
    // 5. Get TCP network from session
    // 6. Set owner on TCP network
    // 7. Update sendonly socket on TCP network
    // 8. Call network->on_stun() to handle the packet
    HELPER_EXPECT_SUCCESS(tcp_conn->handshake());

    // Verify session was found and context switched
    EXPECT_TRUE(mock_session->switch_to_context_called_);

    // Verify session was stored in tcp_conn
    EXPECT_EQ(mock_session.get(), tcp_conn->session_);

    // Verify TCP network owner was set
    EXPECT_EQ(tcp_conn, tcp_network->owner().get());

    // Verify sendonly socket was updated
    EXPECT_EQ(mock_io.get(), tcp_network->sendonly_skt_);

    // Verify peer ID was set
    EXPECT_STREQ(test_ip.c_str(), tcp_network->peer_ip_.c_str());
    EXPECT_EQ(test_port, tcp_network->peer_port_);

    // Clean up - prevent double free
    tcp_conn->skt_ = NULL;
    tcp_conn->conn_manager_ = NULL;
    tcp_conn->wrapper_ = NULL;
    tcp_network->sendonly_skt_ = NULL;
    mock_session->tcp_network_ = NULL;
    mock_conn_manager->session_to_return_ = NULL;
}

// Test SrsRtcTcpConn::read_packet - major use scenario
VOID TEST(RtcTcpConnTest, ReadPacketSuccess)
{
    srs_error_t err;

    // Create mock socket with test data
    SrsUniquePtr<MockProtocolReadWriterForTcpNetwork> mock_io(new MockProtocolReadWriterForTcpNetwork());

    // Prepare test packet data: 2-byte length header + packet body
    // Length = 100 bytes (0x0064 in big-endian)
    std::string test_data;
    test_data.push_back(0x00); // Length high byte
    test_data.push_back(0x64); // Length low byte (100 in decimal)

    // Add 100 bytes of packet data
    for (int i = 0; i < 100; i++) {
        test_data.push_back((char)(i % 256));
    }

    mock_io->set_read_data(test_data);

    // Create SrsRtcTcpConn with mock socket
    std::string test_ip = "192.168.1.100";
    int test_port = 8000;
    SrsRtcTcpConn *tcp_conn = new SrsRtcTcpConn(mock_io.get(), test_ip, test_port);

    // Prepare buffer for reading packet
    char pkt[1500];
    int nb_pkt = 1500;

    // Test: Read packet successfully
    HELPER_EXPECT_SUCCESS(tcp_conn->read_packet(pkt, &nb_pkt));

    // Verify packet length was correctly read
    EXPECT_EQ(100, nb_pkt);

    // Verify packet data was correctly read
    for (int i = 0; i < 100; i++) {
        EXPECT_EQ((char)(i % 256), pkt[i]);
    }

    // Verify total bytes read (2 bytes length + 100 bytes data)
    EXPECT_EQ(102, mock_io->get_recv_bytes());

    // Clean up - prevent double free
    tcp_conn->skt_ = NULL;
}

// Test SrsRtcTcpConn::on_tcp_pkt - major use scenario
// This test covers the main packet routing logic:
// 1. STUN packets are decoded and forwarded to session->tcp()->on_stun()
// 2. RTP packets are forwarded to session->tcp()->on_rtp()
// 3. RTCP packets are forwarded to session->tcp()->on_rtcp()
// 4. DTLS packets are forwarded to session->tcp()->on_dtls()
// 5. Unknown packets return error
// 6. When session is NULL, packets are ignored
VOID TEST(RtcTcpConnTest, OnTcpPktRouting)
{
    srs_error_t err;

    // Create mock RTC connection with TCP network
    SrsUniquePtr<MockRtcConnectionForTcpConnHandshake> mock_session(new MockRtcConnectionForTcpConnHandshake());
    SrsUniquePtr<MockRtcNetworkForNetworks> mock_tcp_network(new MockRtcNetworkForNetworks());
    mock_session->tcp_network_ = mock_tcp_network.get();

    // Create SrsRtcTcpConn
    SrsUniquePtr<MockProtocolReadWriterForTcpNetwork> mock_io(new MockProtocolReadWriterForTcpNetwork());
    std::string test_ip = "192.168.1.100";
    int test_port = 8000;
    SrsRtcTcpConn *tcp_conn = new SrsRtcTcpConn(mock_io.get(), test_ip, test_port);

    // Inject mock session
    tcp_conn->session_ = mock_session.get();

    // Test 1: STUN packet routing
    // Create valid STUN binding request packet
    // STUN header: message type (2) + message length (2) + magic cookie (4) + transaction ID (12) = 20 bytes
    char stun_pkt[20];
    memset(stun_pkt, 0, sizeof(stun_pkt));
    // Set message type to binding request (0x0001)
    stun_pkt[0] = 0x00;
    stun_pkt[1] = 0x01;
    // Set message length to 0 (no attributes)
    stun_pkt[2] = 0x00;
    stun_pkt[3] = 0x00;
    // Set magic cookie (0x2112A442 in network byte order)
    stun_pkt[4] = 0x21;
    stun_pkt[5] = 0x12;
    stun_pkt[6] = 0xA4;
    stun_pkt[7] = 0x42;
    // Set transaction ID (12 bytes)
    for (int i = 8; i < 20; i++) {
        stun_pkt[i] = i - 8;
    }

    HELPER_EXPECT_SUCCESS(tcp_conn->on_tcp_pkt(stun_pkt, 20));

    // Test 2: RTP packet routing
    // RTP packets have version bits (10) in first byte, and payload type < 64
    char rtp_pkt[100];
    memset(rtp_pkt, 0, sizeof(rtp_pkt));
    rtp_pkt[0] = 0x80; // Version 2 (10xxxxxx)
    rtp_pkt[1] = 0x08; // Payload type 8 (PCMA)

    HELPER_EXPECT_SUCCESS(tcp_conn->on_tcp_pkt(rtp_pkt, 100));

    // Test 3: RTCP packet routing
    // RTCP packets have version bits (10) and payload type in range [64, 95]
    char rtcp_pkt[100];
    memset(rtcp_pkt, 0, sizeof(rtcp_pkt));
    rtcp_pkt[0] = 0x80; // Version 2 (10xxxxxx)
    rtcp_pkt[1] = 0xC8; // Payload type 200 (SR - Sender Report)

    HELPER_EXPECT_SUCCESS(tcp_conn->on_tcp_pkt(rtcp_pkt, 100));

    // Test 4: DTLS packet routing
    // DTLS packets have content type in range [20, 63]
    char dtls_pkt[100];
    memset(dtls_pkt, 0, sizeof(dtls_pkt));
    dtls_pkt[0] = 0x16; // Content type: handshake (22)
    dtls_pkt[1] = 0xFE; // DTLS version 1.0 (0xFEFF)
    dtls_pkt[2] = 0xFF;

    HELPER_EXPECT_SUCCESS(tcp_conn->on_tcp_pkt(dtls_pkt, 100));

    // Test 5: Unknown packet returns error
    char unknown_pkt[100];
    memset(unknown_pkt, 0xFF, sizeof(unknown_pkt));

    HELPER_EXPECT_FAILED(tcp_conn->on_tcp_pkt(unknown_pkt, 100));

    // Test 6: When session is NULL, packets are ignored (no error)
    tcp_conn->session_ = NULL;
    HELPER_EXPECT_SUCCESS(tcp_conn->on_tcp_pkt(stun_pkt, 20));

    // Clean up - prevent double free
    tcp_conn->skt_ = NULL;
    tcp_conn->session_ = NULL;
    mock_session->tcp_network_ = NULL;
}
