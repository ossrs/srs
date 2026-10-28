//
// Copyright (c) 2013-2026 The SRS Authors
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
#include <srs_kernel_pithy_print.hpp>
#include <srs_kernel_ps.hpp>
#include <srs_kernel_ts.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_json.hpp>
#include <srs_protocol_raw_avc.hpp>
#include <srs_protocol_rtc_stun.hpp>
#include <srs_protocol_rtmp_conn.hpp>
#include <srs_protocol_sdp.hpp>
#include <srs_utest_ai15.hpp>
#include <srs_utest_ai15.hpp>
#include <srs_utest_ai16.hpp>
#include <srs_utest_ai19.hpp>
#include <srs_utest_ai34.hpp>
#include <srs_utest_manual_http.hpp>
#include <srs_utest_manual_kernel.hpp>
#include <srs_utest_manual_protocol.hpp>

#ifdef SRS_GB28181
// Mock ISrsGbMuxer implementation
MockGbMuxer::MockGbMuxer()
{
    setup_called_ = false;
    setup_output_ = "";
    on_ts_message_called_ = false;
    on_ts_message_error_ = srs_success;
    assemble_count_ = 0;
}

MockGbMuxer::~MockGbMuxer()
{
}

void MockGbMuxer::assemble()
{
    assemble_count_++;
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
    media_connect_timeout_ = 10 * SRS_UTIME_SECONDS;
}

MockAppConfigForGbSession::~MockAppConfigForGbSession()
{
}

std::string MockAppConfigForGbSession::get_stream_caster_output(SrsConfDirective *conf)
{
    return stream_caster_output_;
}

srs_utime_t MockAppConfigForGbSession::get_stream_caster_media_connect_timeout(SrsConfDirective *conf)
{
    return media_connect_timeout_;
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
    session->assemble();

    // Inject mock dependencies
    session->config_ = mock_config.get();
    session->muxer_ = mock_muxer;

    // Setup mock config to return test output
    mock_config->set_stream_caster_output("rtmp://127.0.0.1/live/test_stream");
    mock_config->media_connect_timeout_ = 500 * SRS_UTIME_MILLISECONDS;

    EXPECT_EQ(0, session->media_connect_timeout_);

    // Test setup() method
    SrsConfDirective *conf = NULL;
    session->setup(conf);

    // Verify muxer->setup() was called with correct output
    EXPECT_TRUE(mock_muxer->setup_called_);
    EXPECT_STREQ("rtmp://127.0.0.1/live/test_stream", mock_muxer->setup_output_.c_str());
    EXPECT_EQ(500 * SRS_UTIME_MILLISECONDS, session->media_connect_timeout_);

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
    session->assemble();

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
    assemble_count_ = 0;
    set_cid_called_ = false;
    is_connected_ = false;
    interrupt_called_ = false;
}

MockGbMediaTcpConn::~MockGbMediaTcpConn()
{
}

void MockGbMediaTcpConn::assemble()
{
    assemble_count_++;
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
    interrupt_called_ = true;
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
    interrupt_called_ = false;
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
    session->assemble();

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
    EXPECT_EQ(0, session->connecting_starttime_);

    // Clean up - set to NULL to avoid double-free
    session->muxer_ = NULL;
    srs_freep(mock_muxer);
}

// Test that an API-created session expires while waiting for its media TCP
// connection, but stops using that timeout as soon as a transport binds.
VOID TEST(GB28181Test, SessionMediaConnectionTimeout)
{
    srs_error_t err = srs_success;
    SrsUniquePtr<MockAppConfigForGbSession> mock_config(new MockAppConfigForGbSession());
    MockGbMuxer *mock_muxer = new MockGbMuxer();
    SrsUniquePtr<SrsGbSession> session(new SrsGbSession());
    session->assemble();

    session->config_ = mock_config.get();
    session->muxer_ = mock_muxer;
    mock_config->media_connect_timeout_ = 500 * SRS_UTIME_MILLISECONDS;
    session->setup(NULL);

    session->connecting_starttime_ = srs_time_now_realtime();
    HELPER_EXPECT_SUCCESS(session->drive_state());

    session->connecting_starttime_ = srs_time_now_realtime() - 10 * SRS_UTIME_SECONDS;
    err = session->drive_state();
    EXPECT_TRUE(err != srs_success);
    EXPECT_EQ(ERROR_SUCCESS, srs_error_code(err));
    srs_freep(err);

    MockGbMediaTcpConn *media = new MockGbMediaTcpConn();
    SrsSharedResource<ISrsGbMediaTcpConn> media_resource(media);
    session->on_media_transport(media_resource);
    HELPER_EXPECT_SUCCESS(session->drive_state());

    session->muxer_ = NULL;
    srs_freep(mock_muxer);
}

// Test that a bound session never ends by the media connect timeout, whatever value the config
// gives it: zero, negative, or a value that is not a number, which parses to zero.
VOID TEST(GB28181Test, SessionBoundIgnoresNonPositiveTimeout)
{
    srs_error_t err = srs_success;

    srs_utime_t timeouts[] = {0, -1 * SRS_UTIME_SECONDS};
    for (int i = 0; i < (int)(sizeof(timeouts) / sizeof(timeouts[0])); i++) {
        SrsUniquePtr<MockAppConfigForGbSession> mock_config(new MockAppConfigForGbSession());
        MockGbMuxer *mock_muxer = new MockGbMuxer();
        SrsUniquePtr<SrsGbSession> session(new SrsGbSession());
        session->assemble();

        session->config_ = mock_config.get();
        session->muxer_ = mock_muxer;
        mock_config->media_connect_timeout_ = timeouts[i];
        session->setup(NULL);

        MockGbMediaTcpConn *media = new MockGbMediaTcpConn();
        media->is_connected_ = true;
        SrsSharedResource<ISrsGbMediaTcpConn> media_resource(media);
        session->on_media_transport(media_resource);

        // The media keeps flowing, so every drive keeps the session.
        for (int j = 0; j < 3; j++) {
            HELPER_EXPECT_SUCCESS(session->drive_state());
        }

        session->muxer_ = NULL;
        srs_freep(mock_muxer);
    }
}

// Test SrsGbSession::on_media_disconnected - only the current transport may
// terminate the session, while a stale transport must be ignored.
VOID TEST(GB28181Test, SessionOnMediaDisconnected)
{
    SrsUniquePtr<SrsGbSession> session(new SrsGbSession());
    session->assemble();
    SrsUniquePtr<MockInterruptableForRtcTcpConn> owner(new MockInterruptableForRtcTcpConn());

    MockGbMediaTcpConn *current = new MockGbMediaTcpConn();
    MockGbMediaTcpConn *stale = new MockGbMediaTcpConn();
    SrsSharedResource<ISrsGbMediaTcpConn> current_resource(current);
    SrsSharedResource<ISrsGbMediaTcpConn> stale_resource(stale);

    session->setup_owner(NULL, owner.get(), NULL);
    session->on_media_transport(current_resource);

    session->on_media_disconnected(stale);
    EXPECT_FALSE(owner->interrupt_called_);

    session->on_media_disconnected(current);
    EXPECT_TRUE(owner->interrupt_called_);
}

// Test SrsGbSession::is_ended - a session ends with its cycle, when its executor is done.
VOID TEST(GB28181Test, SessionEndsWithItsCycle)
{
    SrsUniquePtr<SrsGbSession> session(new SrsGbSession());
    session->assemble();
    SrsUniquePtr<MockInterruptableForRtcTcpConn> owner(new MockInterruptableForRtcTcpConn());

    session->setup_owner(NULL, owner.get(), NULL);
    EXPECT_FALSE(session->is_ended());

    session->on_executor_done(owner.get());
    EXPECT_TRUE(session->is_ended());
}

// Test that a session interrupts every media connection that bound it when it is freed, which cuts
// their pointers to the session, so no connection points at the freed session.
VOID TEST(GB28181Test, SessionFreedInterruptsEveryMedia)
{
    SrsGbSession *session = new SrsGbSession();
    session->assemble();

    MockGbMediaTcpConn *first = new MockGbMediaTcpConn();
    MockGbMediaTcpConn *second = new MockGbMediaTcpConn();
    SrsSharedResource<ISrsGbMediaTcpConn> first_resource(first);
    SrsSharedResource<ISrsGbMediaTcpConn> second_resource(second);

    session->on_media_transport(first_resource);
    session->on_media_transport(second_resource);
    EXPECT_EQ(second, session->media_.get());
    EXPECT_FALSE(first->interrupt_called_);
    EXPECT_FALSE(second->interrupt_called_);

    srs_freep(session);
    EXPECT_TRUE(first->interrupt_called_);
    EXPECT_TRUE(second->interrupt_called_);
}

// Test that a session forgets a media connection that ended, so it holds and interrupts only the
// connections that still run.
VOID TEST(GB28181Test, SessionForgetsDisconnectedMedia)
{
    SrsGbSession *session = new SrsGbSession();
    session->assemble();
    SrsUniquePtr<MockInterruptableForRtcTcpConn> owner(new MockInterruptableForRtcTcpConn());
    session->setup_owner(NULL, owner.get(), NULL);

    MockGbMediaTcpConn *first = new MockGbMediaTcpConn();
    MockGbMediaTcpConn *second = new MockGbMediaTcpConn();
    SrsSharedResource<ISrsGbMediaTcpConn> first_resource(first);
    SrsSharedResource<ISrsGbMediaTcpConn> second_resource(second);

    session->on_media_transport(first_resource);
    session->on_media_transport(second_resource);
    EXPECT_EQ(2u, session->medias_.size());

    // The stale connection ends: forgotten, and the session goes on.
    session->on_media_disconnected(first);
    EXPECT_EQ(1u, session->medias_.size());
    EXPECT_FALSE(owner->interrupt_called_);

    srs_freep(session);
    EXPECT_FALSE(first->interrupt_called_);
    EXPECT_TRUE(second->interrupt_called_);
}

// Test that a session's cycle ends only after its media connections quit, so the
// session and its muxer, freed after the cycle, are never freed under a connection
// in the muxer, where a connect or send of the RTMP output yields. The interrupt
// ends only the I/O it finds, so the connection must not mux the rest of its pack.
VOID TEST(GB28181Test, SessionCycleWaitsForMediaInMuxer)
{
    srs_error_t err = srs_success;

    SrsUniquePtr<SrsTsMessage> audio(new SrsTsMessage());
    audio->sid_ = SrsTsPESStreamIdAudioCommon;
    audio->payload_->append("audio_data", 10);
    SrsUniquePtr<SrsTsMessage> audio2(new SrsTsMessage());
    audio2->sid_ = SrsTsPESStreamIdAudioCommon;
    audio2->payload_->append("audio_data", 10);

    // The connection's coroutine outlives the session, which interrupts it when freed.
    MockGbMediaCoroutine media(NULL, audio.get());
    media.msgs_.push_back(audio2.get());
    SrsSTCoroutine trd("gb-media", &media);

    SrsUniquePtr<SrsGbSession> session(new SrsGbSession());
    session->assemble();
    MockGbYieldingMuxer *muxer = new MockGbYieldingMuxer(10 * SRS_UTIME_SECONDS);
    srs_freep(session->muxer_);
    session->muxer_ = muxer;
    media.session_ = session.get();

    SrsUniquePtr<MockInterruptableForRtcTcpConn> owner(new MockInterruptableForRtcTcpConn());
    owner->pull_error_ = srs_error_new(ERROR_THREAD_INTERRUPED, "mock interrupted");
    SrsUniquePtr<MockContextIdSetterForRtcTcpConn> owner_cid(new MockContextIdSetterForRtcTcpConn());
    session->setup_owner(NULL, owner.get(), owner_cid.get());

    // A connection that never sent a pack, so it was never in the muxer.
    MockGbMediaTcpConn *idle = new MockGbMediaTcpConn();
    session->on_media_transport(SrsSharedResource<ISrsGbMediaTcpConn>(idle));
    MockGbMediaTcpConnInMuxer *conn = new MockGbMediaTcpConnInMuxer(&media, &trd);
    session->on_media_transport(SrsSharedResource<ISrsGbMediaTcpConn>(conn));

    // The connection's coroutine enters the muxer with the first message of its pack, and yields there.
    HELPER_EXPECT_SUCCESS(trd.start());
    srs_usleep(10 * SRS_UTIME_MILLISECONDS);
    EXPECT_EQ(1, muxer->nn_messages_);
    EXPECT_FALSE(muxer->left_);
    EXPECT_TRUE(conn->is_connected());

    // The session's cycle ends and interrupts its connections. The interrupted one leaves the muxer
    // without muxing the second message, and quits; the cycle returns only then, and no connection
    // binds the session after it.
    EXPECT_FALSE(session->is_ended());
    srs_utime_t starttime = srs_time_now_realtime();
    HELPER_EXPECT_SUCCESS(session->cycle());
    EXPECT_LT(srs_time_now_realtime() - starttime, 1 * SRS_UTIME_SECONDS);
    EXPECT_TRUE(idle->interrupt_called_);
    EXPECT_TRUE(conn->interrupt_called_);
    EXPECT_TRUE(muxer->left_);
    EXPECT_EQ(1, muxer->nn_messages_);
    EXPECT_FALSE(conn->is_connected());
    EXPECT_TRUE(session->is_ended());

    trd.stop();
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
    session->assemble();

    // Inject mock dependencies
    session->config_ = mock_config.get();
    session->muxer_ = mock_muxer;
    session->media_ = SrsSharedResource<ISrsGbMediaTcpConn>(mock_media);
    session->device_id_ = "test-device-123";
    session->setup(NULL);

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
    SrsUniquePtr<SrsGbListener> listener(new SrsGbListener(NULL));

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
    SrsUniquePtr<SrsGbListener> listener(new SrsGbListener(NULL));

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

VOID TEST(GB28181Test, GlobalResourceManagerIsAssembled)
{
    ASSERT_TRUE(_srs_gb_manager != NULL);

    EXPECT_TRUE(_srs_kernel_factory == _srs_gb_manager->factory_);
    EXPECT_TRUE(NULL != _srs_gb_manager->cond_);
}

// The listener keeps the API server owner it is constructed with.
VOID TEST(GB28181Test, ListenerConstructionCapturesApiServerOwner)
{
    MockApiServerOwnerForGbListener owner;
    SrsUniquePtr<SrsGbListener> listener(new SrsGbListener(&owner));

    EXPECT_TRUE(listener->api_server_owner_ == &owner);
}

// initialize() keeps the injected owner instead of looking up the global server, so listen()
// registers the GB publish API on the owner's mux.
VOID TEST(GB28181Test, ListenerInitializeKeepsApiServerOwnerForListen)
{
    srs_error_t err;

    MockAppConfigForGbListener config;
    config.stream_caster_listen_port_ = 9000;
    MockHttpServeMuxForGbListener mux;
    MockApiServerOwnerForGbListener owner;
    owner.mux_ = &mux;
    MockIpListenerForGbListen *media_listener = new MockIpListenerForGbListen();

    SrsUniquePtr<SrsGbListener> listener(new SrsGbListener(&owner));
    listener->config_ = &config;
    srs_freep(listener->media_listener_);
    listener->media_listener_ = media_listener;

    SrsUniquePtr<SrsConfDirective> conf(new SrsConfDirective());
    conf->name_ = "stream_caster";
    conf->args_.push_back("gb28181");

    HELPER_EXPECT_SUCCESS(listener->initialize(conf.get()));
    // Stop if the owner was replaced, so listen() never registers on the real server's mux.
    ASSERT_TRUE(listener->api_server_owner_ == &owner);

    HELPER_EXPECT_SUCCESS(listener->listen());
    EXPECT_TRUE(media_listener->listen_called_);
    EXPECT_TRUE(mux.handle_called_);
    EXPECT_STREQ("/gb/v1/publish/", mux.handle_pattern_.c_str());

    srs_freep(mux.handle_handler_);
    listener->config_ = NULL;
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

// The constructor only captures the context global; it reads no context id, so a test can
// inject the context before assemble() runs.
VOID TEST(GbMediaTcpConnTest, ConstructorCapturesContextWithoutReadingId)
{
    SrsUniquePtr<SrsGbMediaTcpConn> conn(new SrsGbMediaTcpConn());

    EXPECT_TRUE(conn->context_ == _srs_context);
    EXPECT_TRUE(conn->cid_.empty());
}

// assemble() takes the connection's id from the injected context.
VOID TEST(GbMediaTcpConnTest, AssembleReadsIdThroughContext)
{
    SrsUniquePtr<SrsGbMediaTcpConn> conn(new SrsGbMediaTcpConn());

    MockContextForRtmpConn context;
    context.id_.set_value("gb-media-cid");
    conn->context_ = &context;
    conn->assemble();

    EXPECT_STREQ("gb-media-cid", conn->get_id().c_str());

    conn->context_ = NULL;
}

// The factory returns an assembled connection, which carries the id of the accepting coroutine.
VOID TEST(GbMediaTcpConnTest, FactoryCreatesAssembledConn)
{
    SrsContextId before = _srs_context->get_id();
    SrsContextId cid;
    _srs_context->set_id(cid.set_value("gb-accept-cid"));

    SrsAppFactory factory;
    ISrsGbMediaTcpConn *conn = factory.create_gb_media_tcp_conn();
    EXPECT_STREQ("gb-accept-cid", conn->get_id().c_str());
    srs_freep(conn);

    _srs_context->set_id(before);
}

// The constructor only captures the context global; it neither switches the coroutine's id,
// nor stamps the start time, nor assembles its media placeholder, so a test can inject first.
VOID TEST(GbSessionTest, ConstructorCapturesContextWithoutWork)
{
    std::string before = _srs_context->get_id().c_str();

    SrsUniquePtr<SrsGbSession> session(new SrsGbSession());

    EXPECT_STREQ(before.c_str(), _srs_context->get_id().c_str());
    EXPECT_TRUE(session->context_ == _srs_context);
    EXPECT_TRUE(session->cid_.empty());
    EXPECT_EQ(0, session->startime_);
    EXPECT_TRUE(session->media_->get_id().empty());
}

// assemble() switches to a new session id through the injected context, and stamps the start times.
VOID TEST(GbSessionTest, AssembleSetsNewIdThroughContext)
{
    SrsUniquePtr<SrsGbSession> session(new SrsGbSession());

    MockContextForRtmpConn context;
    context.id_.set_value("gb-session-cid");
    session->context_ = &context;
    session->assemble();

    EXPECT_EQ(1, context.generate_id_count_);
    EXPECT_EQ(1, context.set_id_count_);
    EXPECT_STREQ("gb-session-cid", session->get_id().c_str());
    EXPECT_GT(session->startime_, 0);
    EXPECT_EQ(session->startime_, session->connecting_starttime_);
    EXPECT_EQ(session->startime_, session->media_starttime_);

    session->context_ = NULL;
}

// The session assembles its placeholder media connection before it switches to its own id,
// so the placeholder carries the id of the creating coroutine.
VOID TEST(GbSessionTest, AssembleAssemblesMediaWithCreatorId)
{
    SrsContextId before = _srs_context->get_id();
    SrsContextId cid;
    _srs_context->set_id(cid.set_value("gb-creator-cid"));

    SrsUniquePtr<SrsGbSession> session(new SrsGbSession());
    session->assemble();
    EXPECT_STREQ("gb-creator-cid", session->media_->get_id().c_str());
    EXPECT_STRNE("gb-creator-cid", session->get_id().c_str());
    EXPECT_STREQ(session->get_id().c_str(), _srs_context->get_id().c_str());

    _srs_context->set_id(before);
}

// The factory returns an assembled session, which has switched the creating coroutine to its id.
VOID TEST(GbSessionTest, FactoryCreatesAssembledSession)
{
    SrsContextId before = _srs_context->get_id();

    SrsAppFactory factory;
    ISrsGbSession *session = factory.create_gb_session();
    EXPECT_FALSE(session->get_id().empty());
    EXPECT_STREQ(session->get_id().c_str(), _srs_context->get_id().c_str());
    srs_freep(session);

    _srs_context->set_id(before);
}

// The session's assemble() assembles the injected muxer.
VOID TEST(GbSessionTest, AssembleAssemblesMuxer)
{
    SrsContextId before = _srs_context->get_id();

    SrsUniquePtr<SrsGbSession> session(new SrsGbSession());
    MockGbMuxer *mock_muxer = new MockGbMuxer();
    srs_freep(session->muxer_);
    session->muxer_ = mock_muxer;

    session->assemble();

    EXPECT_EQ(1, mock_muxer->assemble_count_);

    _srs_context->set_id(before);
}

// The factory is the production construction site: the session it returns has a
// muxer with its pithy print created.
VOID TEST(GbSessionTest, FactoryCreatesSessionWithAssembledMuxer)
{
    SrsContextId before = _srs_context->get_id();

    SrsAppFactory factory;
    SrsUniquePtr<ISrsGbSession> isession(factory.create_gb_session());

    SrsGbSession *session = dynamic_cast<SrsGbSession *>(isession.get());
    ASSERT_TRUE(session != NULL);
    SrsGbMuxer *muxer = dynamic_cast<SrsGbMuxer *>(session->muxer_);
    ASSERT_TRUE(muxer != NULL);
    EXPECT_TRUE(muxer->pprint_ != NULL);

    _srs_context->set_id(before);
}

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
    conn->assemble();

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
    on_media_disconnected_called_ = false;
    ended_ = false;
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

void MockGbSessionForMediaConn::on_media_disconnected(ISrsGbMediaTcpConn *media)
{
    on_media_disconnected_called_ = true;
}

bool MockGbSessionForMediaConn::is_ended()
{
    return ended_;
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
    on_media_disconnected_called_ = false;
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
    conn->assemble();

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
    // Note: conn destructor will free pack_, so we set it to NULL; the session is the test's to free.
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
    conn->assemble();
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

// Test that the session cuts a media connection's pointer to it by interrupt(), before the
// connection's coroutine is interrupted, so the connection never uses the session again, not even
// to tell it that the connection closed.
VOID TEST(GbMediaTcpConnTest, InterruptDetachesSession)
{
    srs_error_t err = srs_success;

    MockGbSessionForMediaConn *mock_session = new MockGbSessionForMediaConn();
    SrsUniquePtr<SrsSharedResource<ISrsGbSession> > session_wrapper(new SrsSharedResource<ISrsGbSession>(mock_session));
    SrsUniquePtr<MockInterruptableForRtcTcpConn> owner(new MockInterruptableForRtcTcpConn());

    SrsGbMediaTcpConn *conn = new SrsGbMediaTcpConn();
    conn->assemble();
    SrsUniquePtr<SrsSharedResource<ISrsGbMediaTcpConn> > wrapper(new SrsSharedResource<ISrsGbMediaTcpConn>(conn));
    conn->setup_owner(wrapper.get(), owner.get(), NULL);
    conn->session_ = mock_session;

    conn->interrupt();
    EXPECT_TRUE(conn->session_ == NULL);
    EXPECT_TRUE(owner->interrupt_called_);

    // The cycle ends at once with no owner coroutine, and leaves the session alone.
    conn->owner_coroutine_ = NULL;
    HELPER_EXPECT_SUCCESS(conn->cycle());
    EXPECT_FALSE(mock_session->on_media_disconnected_called_);
}

// Test that a media connection the session dropped while it was in the muxer drops the packs left
// in its buffer, rather than use the session; its coroutine ends at its next pull.
VOID TEST(GbMediaTcpConnTest, OnPsPackDroppedWhenDetached)
{
    srs_error_t err = srs_success;

    SrsGbMediaTcpConn *conn = new SrsGbMediaTcpConn();
    conn->assemble();
    SrsUniquePtr<SrsSharedResource<ISrsGbMediaTcpConn> > wrapper(new SrsSharedResource<ISrsGbMediaTcpConn>(conn));

    SrsUniquePtr<SrsTsMessage> video(new SrsTsMessage());
    video->sid_ = SrsTsPESStreamIdVideoCommon;
    video->payload_->append("video_data", 10);
    std::vector<SrsTsMessage *> msgs;
    msgs.push_back(video.get());

    HELPER_EXPECT_SUCCESS(conn->on_ps_pack(NULL, msgs));
    EXPECT_FALSE(conn->is_connected());
}

// Test that a media connection doesn't bind a session whose cycle ended, which the manager still
// finds by SSRC until it frees it, and binds the next session of the SSRC.
VOID TEST(GbMediaTcpConnTest, BindSessionRefusesEndedSession)
{
    srs_error_t err = srs_success;

    MockGbSessionForMediaConn *ended = new MockGbSessionForMediaConn();
    ended->ended_ = true;
    SrsUniquePtr<SrsSharedResource<ISrsGbSession> > ended_wrapper(new SrsSharedResource<ISrsGbSession>(ended));

    SrsUniquePtr<MockResourceManagerForBindSession> mock_manager(new MockResourceManagerForBindSession());
    mock_manager->session_to_return_ = ended_wrapper.get();

    SrsGbMediaTcpConn *conn = new SrsGbMediaTcpConn();
    conn->assemble();
    SrsUniquePtr<SrsSharedResource<ISrsGbMediaTcpConn> > wrapper(new SrsSharedResource<ISrsGbMediaTcpConn>(conn));
    conn->gb_manager_ = mock_manager.get();
    conn->wrapper_ = wrapper.get();

    HELPER_EXPECT_SUCCESS(conn->bind_session(12345, &conn->session_));
    EXPECT_TRUE(conn->session_ == NULL);
    EXPECT_FALSE(ended->on_media_transport_called_);

    // The next session of the SSRC, created after the manager freed the ended one.
    MockGbSessionForMediaConn *next = new MockGbSessionForMediaConn();
    SrsUniquePtr<SrsSharedResource<ISrsGbSession> > next_wrapper(new SrsSharedResource<ISrsGbSession>(next));
    mock_manager->session_to_return_ = next_wrapper.get();

    HELPER_EXPECT_SUCCESS(conn->bind_session(12345, &conn->session_));
    EXPECT_EQ(next, conn->session_);
    EXPECT_TRUE(next->on_media_transport_called_);

    conn->session_ = NULL;
    conn->gb_manager_ = NULL;
    conn->wrapper_ = NULL;
}

// Test that a media connection which another connection replaced on the same SSRC never uses the
// session after the current connection drops and the manager frees the session; the stale one's
// next pack and its own disconnect come later.
VOID TEST(GbMediaTcpConnTest, StaleMediaOutlivesSession)
{
    srs_error_t err = srs_success;

    SrsGbSession *session = new SrsGbSession();
    session->assemble();
    SrsUniquePtr<MockInterruptableForRtcTcpConn> session_owner(new MockInterruptableForRtcTcpConn());
    session->setup_owner(NULL, session_owner.get(), NULL);
    SrsSharedResource<ISrsGbSession> *session_wrapper = new SrsSharedResource<ISrsGbSession>(session);

    SrsUniquePtr<MockResourceManagerForBindSession> mock_manager(new MockResourceManagerForBindSession());
    mock_manager->session_to_return_ = session_wrapper;

    SrsUniquePtr<MockInterruptableForRtcTcpConn> stale_owner(new MockInterruptableForRtcTcpConn());
    SrsGbMediaTcpConn *stale = new SrsGbMediaTcpConn();
    stale->assemble();
    SrsUniquePtr<SrsSharedResource<ISrsGbMediaTcpConn> > stale_wrapper(new SrsSharedResource<ISrsGbMediaTcpConn>(stale));
    stale->setup_owner(stale_wrapper.get(), stale_owner.get(), NULL);
    stale->gb_manager_ = mock_manager.get();

    SrsUniquePtr<MockInterruptableForRtcTcpConn> current_owner(new MockInterruptableForRtcTcpConn());
    SrsGbMediaTcpConn *current = new SrsGbMediaTcpConn();
    current->assemble();
    SrsUniquePtr<SrsSharedResource<ISrsGbMediaTcpConn> > current_wrapper(new SrsSharedResource<ISrsGbMediaTcpConn>(current));
    current->setup_owner(current_wrapper.get(), current_owner.get(), NULL);
    current->gb_manager_ = mock_manager.get();

    // Both connections bind the session by the same SSRC, the second one replaces the first.
    HELPER_EXPECT_SUCCESS(stale->bind_session(12345, &stale->session_));
    HELPER_EXPECT_SUCCESS(current->bind_session(12345, &current->session_));
    EXPECT_EQ(session, stale->session_);
    EXPECT_EQ(session, current->session_);
    EXPECT_EQ(current, session->media_.get());

    // The current connection drops, which ends the session.
    session->on_media_disconnected(current);
    current->session_ = NULL;
    EXPECT_TRUE(session_owner->interrupt_called_);
    EXPECT_FALSE(stale_owner->interrupt_called_);

    // The manager frees the ended session, which must cut the stale connection off it.
    mock_manager->session_to_return_ = NULL;
    srs_freep(session_wrapper);
    EXPECT_TRUE(stale->session_ == NULL);
    EXPECT_TRUE(stale_owner->interrupt_called_);

    // The stale connection's next pack and its disconnect leave the freed session alone.
    SrsUniquePtr<SrsTsMessage> video(new SrsTsMessage());
    video->sid_ = SrsTsPESStreamIdVideoCommon;
    video->payload_->append("video_data", 10);
    std::vector<SrsTsMessage *> msgs;
    msgs.push_back(video.get());
    HELPER_EXPECT_SUCCESS(stale->on_ps_pack(NULL, msgs));

    stale->owner_coroutine_ = NULL;
    HELPER_EXPECT_SUCCESS(stale->cycle());

    stale->gb_manager_ = NULL;
    current->gb_manager_ = NULL;
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

static SrsMediaPacket *mock_mpegps_queue_packet(SrsFrameType type, int64_t timestamp)
{
    SrsMediaPacket *msg = new SrsMediaPacket();
    msg->timestamp_ = timestamp;
    msg->message_type_ = type;
    char *data = new char[4];
    memset(data, 0, 4);
    msg->wrap(data, 4);
    return msg;
}

// A GB stream with no audio, as a device with its audio off sends it, is held
// like one with audio, by one frame: the queue doesn't wait for a track that
// never came.
VOID TEST(MpegpsQueueTest, DequeueVideoOnlyHoldsOneFrame)
{
    srs_error_t err = srs_success;

    SrsUniquePtr<SrsMpegpsQueue> queue(new SrsMpegpsQueue());

    // The first video message alone is held.
    HELPER_EXPECT_SUCCESS(queue->push(mock_mpegps_queue_packet(SrsFrameTypeVideo, 0)));
    EXPECT_TRUE(queue->dequeue() == NULL);

    // Each next video message sends the one before it, in time order.
    for (int i = 1; i <= 10; i++) {
        HELPER_EXPECT_SUCCESS(queue->push(mock_mpegps_queue_packet(SrsFrameTypeVideo, i * 40)));

        SrsUniquePtr<SrsMediaPacket> msg(queue->dequeue());
        ASSERT_TRUE(msg.get() != NULL);
        EXPECT_EQ((i - 1) * 40, msg->timestamp_);
        EXPECT_TRUE(msg->is_video());

        EXPECT_TRUE(queue->dequeue() == NULL);
        EXPECT_EQ(1, queue->nb_videos_);
        EXPECT_EQ(1u, queue->msgs_.size());
    }
}

// A GB stream with no video is held by one frame too.
VOID TEST(MpegpsQueueTest, DequeueAudioOnlyHoldsOneFrame)
{
    srs_error_t err = srs_success;

    SrsUniquePtr<SrsMpegpsQueue> queue(new SrsMpegpsQueue());

    HELPER_EXPECT_SUCCESS(queue->push(mock_mpegps_queue_packet(SrsFrameTypeAudio, 0)));
    EXPECT_TRUE(queue->dequeue() == NULL);

    for (int i = 1; i <= 10; i++) {
        HELPER_EXPECT_SUCCESS(queue->push(mock_mpegps_queue_packet(SrsFrameTypeAudio, i * 23)));

        SrsUniquePtr<SrsMediaPacket> msg(queue->dequeue());
        ASSERT_TRUE(msg.get() != NULL);
        EXPECT_EQ((i - 1) * 23, msg->timestamp_);
        EXPECT_TRUE(msg->is_audio());

        EXPECT_TRUE(queue->dequeue() == NULL);
        EXPECT_EQ(1, queue->nb_audios_);
    }
}

// Once the other track has come, the queue waits for 2 messages of each again,
// so the tracks go out in time order.
VOID TEST(MpegpsQueueTest, DequeueWaitsForTrackOnceItCame)
{
    srs_error_t err = srs_success;

    SrsUniquePtr<SrsMpegpsQueue> queue(new SrsMpegpsQueue());

    // Two video messages before any audio: the first goes.
    HELPER_EXPECT_SUCCESS(queue->push(mock_mpegps_queue_packet(SrsFrameTypeVideo, 0)));
    EXPECT_TRUE(queue->dequeue() == NULL);
    HELPER_EXPECT_SUCCESS(queue->push(mock_mpegps_queue_packet(SrsFrameTypeVideo, 1)));
    if (true) {
        SrsUniquePtr<SrsMediaPacket> msg(queue->dequeue());
        ASSERT_TRUE(msg.get() != NULL);
        EXPECT_EQ(0, msg->timestamp_);
    }
    EXPECT_TRUE(queue->dequeue() == NULL);

    // The audio comes: the queue now holds 1 video and 1 audio, and waits.
    HELPER_EXPECT_SUCCESS(queue->push(mock_mpegps_queue_packet(SrsFrameTypeAudio, 20)));
    EXPECT_TRUE(queue->dequeue() == NULL);

    // A second video isn't enough without a second audio.
    HELPER_EXPECT_SUCCESS(queue->push(mock_mpegps_queue_packet(SrsFrameTypeVideo, 40)));
    EXPECT_TRUE(queue->dequeue() == NULL);
    HELPER_EXPECT_SUCCESS(queue->push(mock_mpegps_queue_packet(SrsFrameTypeVideo, 80)));
    EXPECT_TRUE(queue->dequeue() == NULL);
    EXPECT_EQ(3, queue->nb_videos_);
    EXPECT_EQ(1, queue->nb_audios_);

    // A second audio sends the oldest ones in time order, while 2 of each are
    // held.
    HELPER_EXPECT_SUCCESS(queue->push(mock_mpegps_queue_packet(SrsFrameTypeAudio, 60)));
    int64_t want[] = {1, 20};
    for (int i = 0; i < 2; i++) {
        SrsUniquePtr<SrsMediaPacket> msg(queue->dequeue());
        ASSERT_TRUE(msg.get() != NULL);
        EXPECT_EQ(want[i], msg->timestamp_);
    }
    EXPECT_TRUE(queue->dequeue() == NULL);
    EXPECT_EQ(2, queue->nb_videos_);
    EXPECT_EQ(1, queue->nb_audios_);
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
    payloads_.push_back(std::string(msg->payload(), msg->size()));
    srs_freep(msg);

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
    payloads_.clear();
    srs_freep(push_error_);
}

// Mock ISrsGbSession implementation
MockGbSessionForMuxer::MockGbSessionForMuxer()
{
    device_id_ = "34020000001320000001";
    ended_ = false;
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

void MockGbSessionForMuxer::on_media_disconnected(ISrsGbMediaTcpConn *media)
{
}

bool MockGbSessionForMuxer::is_ended()
{
    return ended_;
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

MockGbConnectingRtmpClient::MockGbConnectingRtmpClient(SrsGbMuxer *muxer, SrsTsMessage *msg)
{
    muxer_ = muxer;
    msg_ = msg;
    reentry_error_ = srs_success;
    sdk_set_while_connecting_ = false;
}

MockGbConnectingRtmpClient::~MockGbConnectingRtmpClient()
{
    srs_freep(reentry_error_);
}

srs_error_t MockGbConnectingRtmpClient::connect()
{
    sdk_set_while_connecting_ = (muxer_->sdk_ != NULL);
    if (msg_) {
        reentry_error_ = muxer_->on_ts_message(msg_);
    } else {
        reentry_error_ = muxer_->connect();
    }
    return MockRtmpClient::connect();
}

MockGbSendingRtmpClient::MockGbSendingRtmpClient(SrsGbMuxer *muxer, int *sends, srs_error_t *reentry_error)
{
    muxer_ = muxer;
    fail_at_ = 0;
    sends_ = sends;
    reentry_error_ = reentry_error;
}

MockGbSendingRtmpClient::~MockGbSendingRtmpClient()
{
}

srs_error_t MockGbSendingRtmpClient::send_and_free_message(SrsMediaPacket *msg)
{
    srs_freep(msg);
    int nn = ++(*sends_);

    if (nn == 1) {
        char *data = new char[4];
        memset(data, 0, 4);
        *reentry_error_ = muxer_->rtmp_write_packet(SrsFrameTypeVideo, 80, data, 4);
    }

    // Use this client after the muxer was fed, as a send that yields does.
    send_message_count_++;

    if (nn == fail_at_) {
        return srs_error_new(ERROR_SOCKET_WRITE, "mock send failed");
    }
    return srs_success;
}

MockGbEndingRtmpClient::MockGbEndingRtmpClient(MockGbSessionForMuxer *session, int end_at, int *sends)
{
    session_ = session;
    end_at_ = end_at;
    sends_ = sends;
}

MockGbEndingRtmpClient::~MockGbEndingRtmpClient()
{
}

srs_error_t MockGbEndingRtmpClient::send_and_free_message(SrsMediaPacket *msg)
{
    srs_freep(msg);
    int nn = ++(*sends_);

    if (nn == end_at_) {
        session_->ended_ = true;
        return srs_error_new(ERROR_SOCKET_WRITE, "mock send interrupted");
    }

    send_message_count_++;
    return srs_success;
}

MockGbYieldingMuxer::MockGbYieldingMuxer(srs_utime_t delay)
{
    delay_ = delay;
    nn_messages_ = 0;
    left_ = false;
}

MockGbYieldingMuxer::~MockGbYieldingMuxer()
{
}

srs_error_t MockGbYieldingMuxer::on_ts_message(SrsTsMessage *msg)
{
    on_ts_message_called_ = true;
    nn_messages_++;
    // As a send, the sleep yields, and an interrupt ends it.
    srs_usleep(delay_);
    left_ = true;
    return srs_success;
}

MockGbMediaCoroutine::MockGbMediaCoroutine(SrsGbSession *session, SrsTsMessage *msg)
{
    session_ = session;
    msgs_.push_back(msg);
    done_ = false;
}

MockGbMediaCoroutine::~MockGbMediaCoroutine()
{
}

srs_error_t MockGbMediaCoroutine::cycle()
{
    session_->on_ps_pack(&ctx_, NULL, msgs_);
    done_ = true;
    return srs_success;
}

MockGbMediaTcpConnInMuxer::MockGbMediaTcpConnInMuxer(MockGbMediaCoroutine *media, SrsSTCoroutine *trd)
{
    media_ = media;
    trd_ = trd;
}

MockGbMediaTcpConnInMuxer::~MockGbMediaTcpConnInMuxer()
{
}

bool MockGbMediaTcpConnInMuxer::is_connected()
{
    return !media_->done_;
}

void MockGbMediaTcpConnInMuxer::interrupt()
{
    interrupt_called_ = true;
    trd_->interrupt();
}

MockAppFactoryForGbMuxer::MockAppFactoryForGbMuxer()
{
    created_ = 0;
    cto_ = sto_ = 0;
}

MockAppFactoryForGbMuxer::~MockAppFactoryForGbMuxer()
{
    for (int i = created_; i < (int)clients_.size(); i++) {
        srs_freep(clients_[i]);
    }
}

ISrsBasicRtmpClient *MockAppFactoryForGbMuxer::create_rtmp_client(std::string url, srs_utime_t cto, srs_utime_t sto)
{
    ISrsBasicRtmpClient *client = created_ < (int)clients_.size() ? clients_[created_] : new MockRtmpClient();
    created_++;
    cto_ = cto;
    sto_ = sto;
    return client;
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

srs_error_t MockRtcConnectionForTcpConn::on_rtp_cipher(char *data, int nb_data, bool *dropped)
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

// The constructor does not create the pithy print, which would enter the caster
// stage of the global stage manager before a test could inject anything.
VOID TEST(GbMuxerTest, ConstructorLeavesPithyPrintUnset)
{
    SrsUniquePtr<MockGbSessionForMuxer> mock_session(new MockGbSessionForMuxer());
    SrsUniquePtr<SrsGbMuxer> muxer(new SrsGbMuxer(mock_session.get()));

    EXPECT_TRUE(muxer->pprint_ == NULL);
}

// assemble() creates the pithy print for the caster stage.
VOID TEST(GbMuxerTest, AssembleCreatesCasterPithyPrint)
{
    SrsUniquePtr<MockGbSessionForMuxer> mock_session(new MockGbSessionForMuxer());
    SrsUniquePtr<SrsGbMuxer> muxer(new SrsGbMuxer(mock_session.get()));
    srs_freep(muxer->pprint_);

    muxer->assemble();

    SrsPithyPrint *pprint = dynamic_cast<SrsPithyPrint *>(muxer->pprint_);
    ASSERT_TRUE(pprint != NULL);

    SrsUniquePtr<SrsPithyPrint> expected(SrsPithyPrint::create_caster());
    EXPECT_EQ(expected->stage_id_, pprint->stage_id_);
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

// HEVC goes to RTMP in enhanced RTMP, as from SRT and WebRTC: the sequence
// header as SequenceStart, a frame without a composition time as
// CodedFramesX, and one with it as CodedFrames with the composition time.
VOID TEST(GbMuxerTest, HevcInEnhancedRtmp)
{
    srs_error_t err = srs_success;

    SrsUniquePtr<MockGbSessionForMuxer> mock_session(new MockGbSessionForMuxer());
    SrsUniquePtr<SrsGbMuxer> muxer(new SrsGbMuxer(mock_session.get()));
    muxer->setup("rtmp://127.0.0.1/live/[stream]");

    MockGbMpegpsQueue *mock_queue = new MockGbMpegpsQueue();
    srs_freep(muxer->queue_);
    muxer->queue_ = mock_queue;
    MockRtmpClient *mock_sdk = new MockRtmpClient();
    muxer->sdk_ = mock_sdk;

    unsigned char vps[] = {0x40, 0x01, 0x0C, 0x01, 0xFF, 0xFF, 0x01, 0x60, 0x00, 0x00, 0x03, 0x00, 0x90, 0x00, 0x00, 0x03, 0x00, 0x00, 0x03, 0x00, 0x5D, 0xAC, 0x09};
    unsigned char sps[] = {0x42, 0x01, 0x01, 0x01, 0x60, 0x00, 0x00, 0x03, 0x00, 0x90, 0x00, 0x00, 0x03, 0x00, 0x00, 0x03, 0x00, 0x5D,
                           0xA0, 0x02, 0x80, 0x80, 0x2D, 0x16, 0x59, 0x59, 0xA4, 0x93, 0x2B, 0xC0, 0x5A, 0x70, 0x80};
    unsigned char pps[] = {0x44, 0x01, 0xC1, 0x73, 0xD1, 0x89, 0x00};
    muxer->h265_vps_ = std::string((char *)vps, sizeof(vps));
    muxer->h265_sps_ = std::string((char *)sps, sizeof(sps));
    muxer->h265_pps_ = std::string((char *)pps, sizeof(pps));
    muxer->vps_sps_pps_change_ = true;
    HELPER_EXPECT_SUCCESS(muxer->write_h265_vps_sps_pps(1000, 1000));

    // An IDR (type 19) with pts equal to dts, then a frame (type 1) shown 80ms
    // after it is decoded.
    char idr[] = {0x26, 0x01, (char)0xaf, 0x06};
    std::string idr_ipb;
    HELPER_EXPECT_SUCCESS(muxer->hevc_->mux_ipb_frame(idr, sizeof(idr), idr_ipb));
    HELPER_EXPECT_SUCCESS(muxer->write_h265_ipb_frame(idr_ipb, true, 1000, 1000));
    char b[] = {0x02, 0x01, (char)0xd0, (char)0x80};
    std::string b_ipb;
    HELPER_EXPECT_SUCCESS(muxer->hevc_->mux_ipb_frame(b, sizeof(b), b_ipb));
    HELPER_EXPECT_SUCCESS(muxer->write_h265_ipb_frame(b_ipb, false, 1040, 1120));

    ASSERT_EQ(3, (int)mock_queue->payloads_.size());

    // The sequence header: ex header, keyframe, SequenceStart, then 'hvc1'.
    std::string sh = mock_queue->payloads_[0];
    ASSERT_GT((int)sh.size(), 5);
    EXPECT_EQ(0x90, (uint8_t)sh[0]);
    EXPECT_EQ("hvc1", sh.substr(1, 4));

    // The IDR: keyframe, CodedFramesX, 'hvc1', then the NALU with its size.
    std::string key = mock_queue->payloads_[1];
    ASSERT_EQ(5 + 4 + (int)sizeof(idr), (int)key.size());
    EXPECT_EQ(0x93, (uint8_t)key[0]);
    EXPECT_EQ("hvc1", key.substr(1, 4));
    EXPECT_EQ(std::string("\x00\x00\x00\x04", 4), key.substr(5, 4));
    EXPECT_EQ(std::string(idr, sizeof(idr)), key.substr(9));

    // The other frame: inter frame, CodedFrames, 'hvc1', the composition time
    // of 80ms as SI24, then the NALU with its size.
    std::string inter = mock_queue->payloads_[2];
    ASSERT_EQ(5 + 3 + 4 + (int)sizeof(b), (int)inter.size());
    EXPECT_EQ(0xa1, (uint8_t)inter[0]);
    EXPECT_EQ("hvc1", inter.substr(1, 4));
    EXPECT_EQ(std::string("\x00\x00\x50", 3), inter.substr(5, 3));
    EXPECT_EQ(std::string(b, sizeof(b)), inter.substr(12));

    muxer->sdk_ = NULL;
    srs_freep(mock_sdk);
}

// Appends a NALU to an Annex B video frame, after a 4-byte start code.
static void mock_gb_append_annexb(std::string &frame, const char *nalu, int size)
{
    frame.append("\x00\x00\x00\x01", 4);
    frame.append(nalu, size);
}

// A GB28181 PS pack's video is one access unit: every slice of an H.264 frame
// goes in one RTMP message, a keyframe when its slices are IDR. The AUD and
// SEI are left out, and the SPS and PPS go as the sequence header.
VOID TEST(GbMuxerTest, H264AccessUnitAsOneMessage)
{
    srs_error_t err = srs_success;

    SrsUniquePtr<MockGbSessionForMuxer> mock_session(new MockGbSessionForMuxer());
    SrsUniquePtr<SrsGbMuxer> muxer(new SrsGbMuxer(mock_session.get()));
    muxer->setup("rtmp://127.0.0.1/live/[stream]");

    MockGbMpegpsQueue *mock_queue = new MockGbMpegpsQueue();
    srs_freep(muxer->queue_);
    muxer->queue_ = mock_queue;
    MockRtmpClient *mock_sdk = new MockRtmpClient();
    muxer->sdk_ = mock_sdk;

    const char aud[] = {0x09, (char)0xf0};
    const char sps[] = {0x67, 0x42, 0x00, 0x1f, (char)0xac, (char)0xd9};
    const char pps[] = {0x68, (char)0xeb, (char)0xe3, (char)0xcb};
    const char sei[] = {0x06, 0x05, 0x02, 0x11, 0x22, (char)0x80};
    const char idr0[] = {0x65, (char)0x88, (char)0x84, 0x01};
    const char idr1[] = {0x65, 0x00, 0x22, 0x02};
    const char p0[] = {0x41, (char)0x9a, 0x11, 0x03};
    const char p1[] = {0x41, 0x00, 0x33, 0x04};

    // The first frame at 1000ms: AUD, SPS, PPS, SEI and an IDR of 2 slices.
    if (true) {
        std::string frame;
        mock_gb_append_annexb(frame, aud, sizeof(aud));
        mock_gb_append_annexb(frame, sps, sizeof(sps));
        mock_gb_append_annexb(frame, pps, sizeof(pps));
        mock_gb_append_annexb(frame, sei, sizeof(sei));
        mock_gb_append_annexb(frame, idr0, sizeof(idr0));
        mock_gb_append_annexb(frame, idr1, sizeof(idr1));

        SrsUniquePtr<SrsTsMessage> msg(new SrsTsMessage());
        msg->dts_ = msg->pts_ = 90000;
        SrsBuffer avs((char *)frame.data(), (int)frame.size());
        HELPER_EXPECT_SUCCESS(muxer->mux_h264(msg.get(), &avs));
        EXPECT_TRUE(avs.empty());
    }

    // The next frame at 1040ms: AUD and a P-frame of 2 slices.
    if (true) {
        std::string frame;
        mock_gb_append_annexb(frame, aud, sizeof(aud));
        mock_gb_append_annexb(frame, p0, sizeof(p0));
        mock_gb_append_annexb(frame, p1, sizeof(p1));

        SrsUniquePtr<SrsTsMessage> msg(new SrsTsMessage());
        msg->dts_ = msg->pts_ = 93600;
        SrsBuffer avs((char *)frame.data(), (int)frame.size());
        HELPER_EXPECT_SUCCESS(muxer->mux_h264(msg.get(), &avs));
    }

    // The sequence header, then one message for each frame.
    ASSERT_EQ(3, (int)mock_queue->payloads_.size());

    std::string sh = mock_queue->payloads_[0];
    ASSERT_GE((int)sh.size(), 2);
    EXPECT_EQ(0x17, (uint8_t)sh[0]);
    EXPECT_EQ(SrsVideoAvcFrameTraitSequenceHeader, sh[1]);

    // The keyframe: its 2 slices in the order they came, each with its 4-byte
    // size.
    if (true) {
        std::string want;
        want.append("\x17\x01\x00\x00\x00", 5);
        want.append("\x00\x00\x00\x04", 4);
        want.append(idr0, sizeof(idr0));
        want.append("\x00\x00\x00\x04", 4);
        want.append(idr1, sizeof(idr1));
        EXPECT_EQ(want, mock_queue->payloads_[1]);
    }

    // The inter frame of its 2 slices.
    if (true) {
        std::string want;
        want.append("\x27\x01\x00\x00\x00", 5);
        want.append("\x00\x00\x00\x04", 4);
        want.append(p0, sizeof(p0));
        want.append("\x00\x00\x00\x04", 4);
        want.append(p1, sizeof(p1));
        EXPECT_EQ(want, mock_queue->payloads_[2]);
    }

    muxer->sdk_ = NULL;
    srs_freep(mock_sdk);
}

// Every slice of an HEVC frame goes in one RTMP message too, a keyframe when
// its slices are IRAP. The AUD and SEI are left out, and the VPS, SPS and PPS
// go as the sequence header.
VOID TEST(GbMuxerTest, HevcAccessUnitAsOneMessage)
{
    srs_error_t err = srs_success;

    SrsUniquePtr<MockGbSessionForMuxer> mock_session(new MockGbSessionForMuxer());
    SrsUniquePtr<SrsGbMuxer> muxer(new SrsGbMuxer(mock_session.get()));
    muxer->setup("rtmp://127.0.0.1/live/[stream]");

    MockGbMpegpsQueue *mock_queue = new MockGbMpegpsQueue();
    srs_freep(muxer->queue_);
    muxer->queue_ = mock_queue;
    MockRtmpClient *mock_sdk = new MockRtmpClient();
    muxer->sdk_ = mock_sdk;

    const char aud[] = {0x46, 0x01, 0x50};
    const char vps[] = {0x40, 0x01, 0x0C, 0x01, (char)0xFF, (char)0xFF, 0x01, 0x60, 0x00, 0x00, 0x03, 0x00, (char)0x90, 0x00,
                        0x00, 0x03, 0x00, 0x00, 0x03, 0x00, 0x5D, (char)0xAC, 0x09};
    const char sps[] = {0x42, 0x01, 0x01, 0x01, 0x60, 0x00, 0x00, 0x03, 0x00, (char)0x90, 0x00, 0x00, 0x03, 0x00, 0x00, 0x03, 0x00,
                        0x5D, (char)0xA0, 0x02, (char)0x80, (char)0x80, 0x2D, 0x16, 0x59, 0x59, (char)0xA4, (char)0x93, 0x2B,
                        (char)0xC0, 0x5A, 0x70, (char)0x80};
    const char pps[] = {0x44, 0x01, (char)0xC1, 0x73, (char)0xD1, (char)0x89, 0x00};
    const char sei[] = {0x4e, 0x01, 0x05, 0x02, 0x11, 0x22, (char)0x80};
    const char idr0[] = {0x26, 0x01, (char)0xaf, 0x06};
    const char idr1[] = {0x26, 0x01, 0x22, 0x07};
    const char p0[] = {0x02, 0x01, (char)0xd0, 0x08};
    const char p1[] = {0x02, 0x01, 0x33, 0x09};

    // The first frame at 1000ms: AUD, VPS, SPS, PPS, SEI and an IDR of 2
    // slices.
    if (true) {
        std::string frame;
        mock_gb_append_annexb(frame, aud, sizeof(aud));
        mock_gb_append_annexb(frame, vps, sizeof(vps));
        mock_gb_append_annexb(frame, sps, sizeof(sps));
        mock_gb_append_annexb(frame, pps, sizeof(pps));
        mock_gb_append_annexb(frame, sei, sizeof(sei));
        mock_gb_append_annexb(frame, idr0, sizeof(idr0));
        mock_gb_append_annexb(frame, idr1, sizeof(idr1));

        SrsUniquePtr<SrsTsMessage> msg(new SrsTsMessage());
        msg->dts_ = msg->pts_ = 90000;
        SrsBuffer avs((char *)frame.data(), (int)frame.size());
        HELPER_EXPECT_SUCCESS(muxer->mux_h265(msg.get(), &avs));
        EXPECT_TRUE(avs.empty());
    }

    // The next frame at 1040ms: AUD and a trailing frame of 2 slices.
    if (true) {
        std::string frame;
        mock_gb_append_annexb(frame, aud, sizeof(aud));
        mock_gb_append_annexb(frame, p0, sizeof(p0));
        mock_gb_append_annexb(frame, p1, sizeof(p1));

        SrsUniquePtr<SrsTsMessage> msg(new SrsTsMessage());
        msg->dts_ = msg->pts_ = 93600;
        SrsBuffer avs((char *)frame.data(), (int)frame.size());
        HELPER_EXPECT_SUCCESS(muxer->mux_h265(msg.get(), &avs));
    }

    // The sequence header, then one message for each frame.
    ASSERT_EQ(3, (int)mock_queue->payloads_.size());

    std::string sh = mock_queue->payloads_[0];
    ASSERT_GT((int)sh.size(), 5);
    EXPECT_EQ(0x90, (uint8_t)sh[0]);
    EXPECT_EQ("hvc1", sh.substr(1, 4));

    // The keyframe, CodedFramesX: its 2 slices in the order they came, each
    // with its 4-byte size.
    if (true) {
        std::string want;
        want.append("\x93hvc1", 5);
        want.append("\x00\x00\x00\x04", 4);
        want.append(idr0, sizeof(idr0));
        want.append("\x00\x00\x00\x04", 4);
        want.append(idr1, sizeof(idr1));
        EXPECT_EQ(want, mock_queue->payloads_[1]);
    }

    // The inter frame of its 2 slices.
    if (true) {
        std::string want;
        want.append("\xa3hvc1", 5);
        want.append("\x00\x00\x00\x04", 4);
        want.append(p0, sizeof(p0));
        want.append("\x00\x00\x00\x04", 4);
        want.append(p1, sizeof(p1));
        EXPECT_EQ(want, mock_queue->payloads_[2]);
    }

    muxer->sdk_ = NULL;
    srs_freep(mock_sdk);
}

// Muxes the video of a PS pack from a heap buffer of its exact size, as a
// read past the end of the pack is then caught by the sanitizer.
static srs_error_t mock_gb_mux_video(SrsGbMuxer *muxer, bool hevc, const std::string &frame, int64_t dts)
{
    SrsUniquePtr<char[]> data(new char[frame.size()]);
    memcpy(data.get(), frame.data(), frame.size());

    SrsUniquePtr<SrsTsMessage> msg(new SrsTsMessage());
    msg->dts_ = msg->pts_ = dts;
    SrsBuffer avs(data.get(), (int)frame.size());
    return hevc ? muxer->mux_h265(msg.get(), &avs) : muxer->mux_h264(msg.get(), &avs);
}

// A device's H.264 SPS cut under 4 bytes, then a new PPS, after the stream
// started. The cut SPS has no profile or level for a sequence header, so it is
// skipped and the SPS of the stream is kept; the frames go on.
VOID TEST(GbMuxerTest, H264CutSpsThenNewPps_CutSpsSkipped)
{
    srs_error_t err = srs_success;

    SrsUniquePtr<MockGbSessionForMuxer> mock_session(new MockGbSessionForMuxer());
    SrsUniquePtr<SrsGbMuxer> muxer(new SrsGbMuxer(mock_session.get()));
    muxer->setup("rtmp://127.0.0.1/live/[stream]");

    MockGbMpegpsQueue *mock_queue = new MockGbMpegpsQueue();
    srs_freep(muxer->queue_);
    muxer->queue_ = mock_queue;
    MockRtmpClient *mock_sdk = new MockRtmpClient();
    muxer->sdk_ = mock_sdk;

    const char sps[] = {0x67, 0x42, 0x00, 0x1f, (char)0xac, (char)0xd9};
    const char pps[] = {0x68, (char)0xeb, (char)0xe3, (char)0xcb};
    const char idr[] = {0x65, (char)0x88, (char)0x84, 0x01};
    const char cut_sps[] = {0x67, 0x42, 0x00};
    const char new_pps[] = {0x68, (char)0xce, 0x3c, (char)0x80};
    const char p[] = {0x41, (char)0x9a, 0x11, 0x03};

    if (true) {
        std::string frame;
        mock_gb_append_annexb(frame, sps, sizeof(sps));
        mock_gb_append_annexb(frame, pps, sizeof(pps));
        mock_gb_append_annexb(frame, idr, sizeof(idr));
        HELPER_EXPECT_SUCCESS(mock_gb_mux_video(muxer.get(), false, frame, 90000));
    }

    if (true) {
        std::string frame;
        mock_gb_append_annexb(frame, cut_sps, sizeof(cut_sps));
        mock_gb_append_annexb(frame, new_pps, sizeof(new_pps));
        mock_gb_append_annexb(frame, p, sizeof(p));
        HELPER_EXPECT_SUCCESS(mock_gb_mux_video(muxer.get(), false, frame, 93600));
    }

    EXPECT_EQ(std::string(sps, sizeof(sps)), muxer->h264_sps_);

    // The sequence header, the IDR and the P-frame.
    ASSERT_EQ(3, (int)mock_queue->payloads_.size());
    std::string want("\x27\x01\x00\x00\x00", 5);
    want.append("\x00\x00\x00\x04", 4);
    want.append(p, sizeof(p));
    EXPECT_EQ(want, mock_queue->payloads_[2]);

    muxer->sdk_ = NULL;
    srs_freep(mock_sdk);
}

// A device's H.264 PS pack that ends with a start code, then one with two
// start codes in a row. Each gives an empty NALU, which is skipped, and the
// slices around it go as before.
VOID TEST(GbMuxerTest, H264EmptyNalu_Skipped)
{
    srs_error_t err = srs_success;

    SrsUniquePtr<MockGbSessionForMuxer> mock_session(new MockGbSessionForMuxer());
    SrsUniquePtr<SrsGbMuxer> muxer(new SrsGbMuxer(mock_session.get()));
    muxer->setup("rtmp://127.0.0.1/live/[stream]");

    MockGbMpegpsQueue *mock_queue = new MockGbMpegpsQueue();
    srs_freep(muxer->queue_);
    muxer->queue_ = mock_queue;
    MockRtmpClient *mock_sdk = new MockRtmpClient();
    muxer->sdk_ = mock_sdk;

    const char sps[] = {0x67, 0x42, 0x00, 0x1f, (char)0xac, (char)0xd9};
    const char pps[] = {0x68, (char)0xeb, (char)0xe3, (char)0xcb};
    const char idr[] = {0x65, (char)0x88, (char)0x84, 0x01};
    const char p[] = {0x41, (char)0x9a, 0x11, 0x03};

    if (true) {
        std::string frame;
        mock_gb_append_annexb(frame, sps, sizeof(sps));
        mock_gb_append_annexb(frame, pps, sizeof(pps));
        mock_gb_append_annexb(frame, idr, sizeof(idr));
        frame.append("\x00\x00\x00\x01", 4);
        HELPER_EXPECT_SUCCESS(mock_gb_mux_video(muxer.get(), false, frame, 90000));
    }

    if (true) {
        std::string frame("\x00\x00\x00\x01", 4);
        mock_gb_append_annexb(frame, p, sizeof(p));
        HELPER_EXPECT_SUCCESS(mock_gb_mux_video(muxer.get(), false, frame, 93600));
    }

    // The sequence header, the IDR and the P-frame.
    ASSERT_EQ(3, (int)mock_queue->payloads_.size());
    if (true) {
        std::string want("\x17\x01\x00\x00\x00", 5);
        want.append("\x00\x00\x00\x04", 4);
        want.append(idr, sizeof(idr));
        EXPECT_EQ(want, mock_queue->payloads_[1]);
    }
    if (true) {
        std::string want("\x27\x01\x00\x00\x00", 5);
        want.append("\x00\x00\x00\x04", 4);
        want.append(p, sizeof(p));
        EXPECT_EQ(want, mock_queue->payloads_[2]);
    }

    muxer->sdk_ = NULL;
    srs_freep(mock_sdk);
}

// A device's HEVC PS pack that ends with a start code, then one with two
// start codes in a row. Each gives an empty NALU, which is skipped, and the
// slices around it go as before.
VOID TEST(GbMuxerTest, HevcEmptyNalu_Skipped)
{
    srs_error_t err = srs_success;

    SrsUniquePtr<MockGbSessionForMuxer> mock_session(new MockGbSessionForMuxer());
    SrsUniquePtr<SrsGbMuxer> muxer(new SrsGbMuxer(mock_session.get()));
    muxer->setup("rtmp://127.0.0.1/live/[stream]");

    MockGbMpegpsQueue *mock_queue = new MockGbMpegpsQueue();
    srs_freep(muxer->queue_);
    muxer->queue_ = mock_queue;
    MockRtmpClient *mock_sdk = new MockRtmpClient();
    muxer->sdk_ = mock_sdk;

    const char vps[] = {0x40, 0x01, 0x0C, 0x01, (char)0xFF, (char)0xFF, 0x01, 0x60, 0x00, 0x00, 0x03, 0x00, (char)0x90, 0x00,
                        0x00, 0x03, 0x00, 0x00, 0x03, 0x00, 0x5D, (char)0xAC, 0x09};
    const char sps[] = {0x42, 0x01, 0x01, 0x01, 0x60, 0x00, 0x00, 0x03, 0x00, (char)0x90, 0x00, 0x00, 0x03, 0x00, 0x00, 0x03, 0x00,
                        0x5D, (char)0xA0, 0x02, (char)0x80, (char)0x80, 0x2D, 0x16, 0x59, 0x59, (char)0xA4, (char)0x93, 0x2B,
                        (char)0xC0, 0x5A, 0x70, (char)0x80};
    const char pps[] = {0x44, 0x01, (char)0xC1, 0x73, (char)0xD1, (char)0x89, 0x00};
    const char idr[] = {0x26, 0x01, (char)0xaf, 0x06};
    const char p0[] = {0x02, 0x01, (char)0xd0, 0x08};

    if (true) {
        std::string frame;
        mock_gb_append_annexb(frame, vps, sizeof(vps));
        mock_gb_append_annexb(frame, sps, sizeof(sps));
        mock_gb_append_annexb(frame, pps, sizeof(pps));
        mock_gb_append_annexb(frame, idr, sizeof(idr));
        frame.append("\x00\x00\x00\x01", 4);
        HELPER_EXPECT_SUCCESS(mock_gb_mux_video(muxer.get(), true, frame, 90000));
    }

    if (true) {
        std::string frame("\x00\x00\x00\x01", 4);
        mock_gb_append_annexb(frame, p0, sizeof(p0));
        HELPER_EXPECT_SUCCESS(mock_gb_mux_video(muxer.get(), true, frame, 93600));
    }

    // The sequence header, the IDR and the trailing frame.
    ASSERT_EQ(3, (int)mock_queue->payloads_.size());
    if (true) {
        std::string want("\x93hvc1", 5);
        want.append("\x00\x00\x00\x04", 4);
        want.append(idr, sizeof(idr));
        EXPECT_EQ(want, mock_queue->payloads_[1]);
    }
    if (true) {
        std::string want("\xa3hvc1", 5);
        want.append("\x00\x00\x00\x04", 4);
        want.append(p0, sizeof(p0));
        EXPECT_EQ(want, mock_queue->payloads_[2]);
    }

    muxer->sdk_ = NULL;
    srs_freep(mock_sdk);
}

// A device's HEVC SPS cut under 4 bytes, then a new PPS, after the stream
// started. The cut SPS is skipped and the SPS of the stream kept, so the new
// PPS goes in a new sequence header with it.
VOID TEST(GbMuxerTest, HevcCutSpsThenNewPps_CutSpsSkipped)
{
    srs_error_t err = srs_success;

    SrsUniquePtr<MockGbSessionForMuxer> mock_session(new MockGbSessionForMuxer());
    SrsUniquePtr<SrsGbMuxer> muxer(new SrsGbMuxer(mock_session.get()));
    muxer->setup("rtmp://127.0.0.1/live/[stream]");

    MockGbMpegpsQueue *mock_queue = new MockGbMpegpsQueue();
    srs_freep(muxer->queue_);
    muxer->queue_ = mock_queue;
    MockRtmpClient *mock_sdk = new MockRtmpClient();
    muxer->sdk_ = mock_sdk;

    const char vps[] = {0x40, 0x01, 0x0C, 0x01, (char)0xFF, (char)0xFF, 0x01, 0x60, 0x00, 0x00, 0x03, 0x00, (char)0x90, 0x00,
                        0x00, 0x03, 0x00, 0x00, 0x03, 0x00, 0x5D, (char)0xAC, 0x09};
    const char sps[] = {0x42, 0x01, 0x01, 0x01, 0x60, 0x00, 0x00, 0x03, 0x00, (char)0x90, 0x00, 0x00, 0x03, 0x00, 0x00, 0x03, 0x00,
                        0x5D, (char)0xA0, 0x02, (char)0x80, (char)0x80, 0x2D, 0x16, 0x59, 0x59, (char)0xA4, (char)0x93, 0x2B,
                        (char)0xC0, 0x5A, 0x70, (char)0x80};
    const char pps[] = {0x44, 0x01, (char)0xC1, 0x73, (char)0xD1, (char)0x89, 0x00};
    const char idr[] = {0x26, 0x01, (char)0xaf, 0x06};
    const char cut_sps[] = {0x42, 0x01, 0x01};
    const char new_pps[] = {0x44, 0x01, (char)0xC1, 0x72, (char)0xB4, 0x62, 0x40};
    const char p[] = {0x02, 0x01, 0x33, 0x09};

    if (true) {
        std::string frame;
        mock_gb_append_annexb(frame, vps, sizeof(vps));
        mock_gb_append_annexb(frame, sps, sizeof(sps));
        mock_gb_append_annexb(frame, pps, sizeof(pps));
        mock_gb_append_annexb(frame, idr, sizeof(idr));
        HELPER_EXPECT_SUCCESS(mock_gb_mux_video(muxer.get(), true, frame, 90000));
    }

    if (true) {
        std::string frame;
        mock_gb_append_annexb(frame, cut_sps, sizeof(cut_sps));
        mock_gb_append_annexb(frame, new_pps, sizeof(new_pps));
        mock_gb_append_annexb(frame, p, sizeof(p));
        HELPER_EXPECT_SUCCESS(mock_gb_mux_video(muxer.get(), true, frame, 93600));
    }

    EXPECT_EQ(std::string(sps, sizeof(sps)), muxer->h265_sps_);
    EXPECT_EQ(std::string(new_pps, sizeof(new_pps)), muxer->h265_pps_);

    // The sequence header, the IDR, the new sequence header and the frame.
    ASSERT_EQ(4, (int)mock_queue->payloads_.size());
    if (true) {
        std::string sh = mock_queue->payloads_[2];
        ASSERT_GT((int)sh.size(), 5);
        EXPECT_EQ(0x90, (uint8_t)sh[0]);
        EXPECT_EQ("hvc1", sh.substr(1, 4));
        EXPECT_NE(std::string::npos, sh.find(std::string(sps, sizeof(sps))));
        EXPECT_NE(std::string::npos, sh.find(std::string(new_pps, sizeof(new_pps))));
    }
    if (true) {
        std::string want("\xa3hvc1", 5);
        want.append("\x00\x00\x00\x04", 4);
        want.append(p, sizeof(p));
        EXPECT_EQ(want, mock_queue->payloads_[3]);
    }

    muxer->sdk_ = NULL;
    srs_freep(mock_sdk);
}

// A GB28181 H.264 frame shown after it is decoded, as a B-frame or a frame
// before one, keeps its PES's pts minus dts as its composition time; a frame
// whose pts is its dts, or before it, has none, and so has the sequence header.
VOID TEST(GbMuxerTest, H264KeepsCompositionTime)
{
    srs_error_t err = srs_success;

    SrsUniquePtr<MockGbSessionForMuxer> mock_session(new MockGbSessionForMuxer());
    SrsUniquePtr<SrsGbMuxer> muxer(new SrsGbMuxer(mock_session.get()));
    muxer->setup("rtmp://127.0.0.1/live/[stream]");

    MockGbMpegpsQueue *mock_queue = new MockGbMpegpsQueue();
    srs_freep(muxer->queue_);
    muxer->queue_ = mock_queue;
    MockRtmpClient *mock_sdk = new MockRtmpClient();
    muxer->sdk_ = mock_sdk;

    const char sps[] = {0x67, 0x64, 0x00, 0x1f, (char)0xac, (char)0xd9};
    const char pps[] = {0x68, (char)0xeb, (char)0xe3, (char)0xcb};
    const char idr[] = {0x65, (char)0x88, (char)0x84, 0x01};
    const char p[] = {0x41, (char)0x9a, 0x11, 0x03};
    const char b[] = {0x01, (char)0x9e, 0x22, 0x04};

    // The keyframe decoded at 1000ms, shown at 1080ms; a P-frame decoded at
    // 1040ms, shown at 1160ms; a B-frame decoded and shown at 1080ms; a frame
    // whose pts is 40ms before its dts.
    struct {
        const char *nalu;
        int size;
        int64_t dts;
        int64_t pts;
    } frames[] = {
        {idr, (int)sizeof(idr), 90000, 97200},
        {p, (int)sizeof(p), 93600, 104400},
        {b, (int)sizeof(b), 97200, 97200},
        {p, (int)sizeof(p), 100800, 97200},
    };
    for (int i = 0; i < (int)(sizeof(frames) / sizeof(frames[0])); i++) {
        std::string frame;
        if (i == 0) {
            mock_gb_append_annexb(frame, sps, sizeof(sps));
            mock_gb_append_annexb(frame, pps, sizeof(pps));
        }
        mock_gb_append_annexb(frame, frames[i].nalu, frames[i].size);

        SrsUniquePtr<SrsTsMessage> msg(new SrsTsMessage());
        msg->dts_ = frames[i].dts;
        msg->pts_ = frames[i].pts;
        SrsBuffer avs((char *)frame.data(), (int)frame.size());
        HELPER_EXPECT_SUCCESS(muxer->mux_h264(msg.get(), &avs));
    }

    // The sequence header, then each frame: the frame type, the AVC packet
    // type and the 24-bit composition time.
    ASSERT_EQ(5, (int)mock_queue->payloads_.size());
    EXPECT_EQ(std::string("\x17\x00\x00\x00\x00", 5), mock_queue->payloads_[0].substr(0, 5));
    EXPECT_EQ(std::string("\x17\x01\x00\x00\x50", 5), mock_queue->payloads_[1].substr(0, 5));
    EXPECT_EQ(std::string("\x27\x01\x00\x00\x78", 5), mock_queue->payloads_[2].substr(0, 5));
    EXPECT_EQ(std::string("\x27\x01\x00\x00\x00", 5), mock_queue->payloads_[3].substr(0, 5));
    EXPECT_EQ(std::string("\x27\x01\x00\x00\x00", 5), mock_queue->payloads_[4].substr(0, 5));

    muxer->sdk_ = NULL;
    srs_freep(mock_sdk);
}

// A GB28181 HEVC frame shown after it is decoded goes as an enhanced-RTMP
// CodedFrames tag with its composition time after the fourcc; a frame whose
// pts is its dts goes as CodedFramesX, with none.
VOID TEST(GbMuxerTest, HevcKeepsCompositionTime)
{
    srs_error_t err = srs_success;

    SrsUniquePtr<MockGbSessionForMuxer> mock_session(new MockGbSessionForMuxer());
    SrsUniquePtr<SrsGbMuxer> muxer(new SrsGbMuxer(mock_session.get()));
    muxer->setup("rtmp://127.0.0.1/live/[stream]");

    MockGbMpegpsQueue *mock_queue = new MockGbMpegpsQueue();
    srs_freep(muxer->queue_);
    muxer->queue_ = mock_queue;
    MockRtmpClient *mock_sdk = new MockRtmpClient();
    muxer->sdk_ = mock_sdk;

    const char vps[] = {0x40, 0x01, 0x0C, 0x01, (char)0xFF, (char)0xFF, 0x01, 0x60, 0x00, 0x00, 0x03, 0x00, (char)0x90, 0x00,
                        0x00, 0x03, 0x00, 0x00, 0x03, 0x00, 0x5D, (char)0xAC, 0x09};
    const char sps[] = {0x42, 0x01, 0x01, 0x01, 0x60, 0x00, 0x00, 0x03, 0x00, (char)0x90, 0x00, 0x00, 0x03, 0x00, 0x00, 0x03, 0x00,
                        0x5D, (char)0xA0, 0x02, (char)0x80, (char)0x80, 0x2D, 0x16, 0x59, 0x59, (char)0xA4, (char)0x93, 0x2B,
                        (char)0xC0, 0x5A, 0x70, (char)0x80};
    const char pps[] = {0x44, 0x01, (char)0xC1, 0x73, (char)0xD1, (char)0x89, 0x00};
    const char idr[] = {0x26, 0x01, (char)0xaf, 0x06};
    const char p[] = {0x02, 0x01, (char)0xd0, 0x08};

    // The keyframe decoded at 1000ms, shown at 1080ms; then a frame decoded
    // and shown at 1040ms.
    if (true) {
        std::string frame;
        mock_gb_append_annexb(frame, vps, sizeof(vps));
        mock_gb_append_annexb(frame, sps, sizeof(sps));
        mock_gb_append_annexb(frame, pps, sizeof(pps));
        mock_gb_append_annexb(frame, idr, sizeof(idr));

        SrsUniquePtr<SrsTsMessage> msg(new SrsTsMessage());
        msg->dts_ = 90000;
        msg->pts_ = 97200;
        SrsBuffer avs((char *)frame.data(), (int)frame.size());
        HELPER_EXPECT_SUCCESS(muxer->mux_h265(msg.get(), &avs));
    }
    if (true) {
        std::string frame;
        mock_gb_append_annexb(frame, p, sizeof(p));

        SrsUniquePtr<SrsTsMessage> msg(new SrsTsMessage());
        msg->dts_ = msg->pts_ = 93600;
        SrsBuffer avs((char *)frame.data(), (int)frame.size());
        HELPER_EXPECT_SUCCESS(muxer->mux_h265(msg.get(), &avs));
    }

    // The sequence header, SequenceStart, then the frames.
    ASSERT_EQ(3, (int)mock_queue->payloads_.size());
    EXPECT_EQ(std::string("\x90hvc1", 5), mock_queue->payloads_[0].substr(0, 5));

    if (true) {
        std::string want;
        want.append("\x91hvc1", 5);
        want.append("\x00\x00\x50", 3);
        want.append("\x00\x00\x00\x04", 4);
        want.append(idr, sizeof(idr));
        EXPECT_EQ(want, mock_queue->payloads_[1]);
    }

    if (true) {
        std::string want;
        want.append("\xa3hvc1", 5);
        want.append("\x00\x00\x00\x04", 4);
        want.append(p, sizeof(p));
        EXPECT_EQ(want, mock_queue->payloads_[2]);
    }

    muxer->sdk_ = NULL;
    srs_freep(mock_sdk);
}

// Media that comes while the RTMP output connects, from a second media
// connection of the session, is dropped before the muxer parses it: it isn't
// written through the client that hasn't connected, and leaves the muxer's
// state as it was. Once connected, the next media is written.
VOID TEST(GbMuxerTest, MediaWhileConnectingDropped)
{
    srs_error_t err = srs_success;

    MockAppFactoryForGbMuxer factory;
    SrsUniquePtr<MockGbSessionForMuxer> mock_session(new MockGbSessionForMuxer());
    SrsUniquePtr<SrsGbMuxer> muxer(new SrsGbMuxer(mock_session.get()));
    muxer->setup("rtmp://127.0.0.1/live/[stream]");
    muxer->app_factory_ = &factory;

    MockGbRawAacStream *mock_aac = new MockGbRawAacStream();
    srs_freep(muxer->aac_);
    muxer->aac_ = mock_aac;
    MockGbMpegpsQueue *mock_queue = new MockGbMpegpsQueue();
    srs_freep(muxer->queue_);
    muxer->queue_ = mock_queue;

    SrsUniquePtr<SrsTsMessage> audio(new SrsTsMessage());
    audio->sid_ = SrsTsPESStreamIdAudioCommon;
    audio->dts_ = 90000;
    char adts[100];
    memset(adts, 0, sizeof(adts));
    audio->payload_->append(adts, sizeof(adts));

    MockGbConnectingRtmpClient *client = new MockGbConnectingRtmpClient(muxer.get(), audio.get());
    factory.clients_.push_back(client);

    HELPER_EXPECT_SUCCESS(muxer->connect());

    // While connecting: the client isn't the muxer's yet, and the media is
    // dropped untouched.
    EXPECT_FALSE(client->sdk_set_while_connecting_);
    EXPECT_EQ(ERROR_GB_RTMP_CONNECTING, srs_error_code(client->reentry_error_));
    EXPECT_FALSE(mock_aac->adts_demux_called_);
    EXPECT_TRUE(muxer->aac_specific_config_.empty());
    EXPECT_EQ(0, mock_queue->push_count_);

    // Connected: the client is the muxer's, and media is written.
    EXPECT_TRUE(muxer->sdk_ == client);
    EXPECT_EQ(1, factory.created_);
    HELPER_EXPECT_SUCCESS(muxer->on_ts_message(audio.get()));
    EXPECT_TRUE(mock_aac->adts_demux_called_);
    EXPECT_GE(mock_queue->push_count_, 2);
}

// The RTMP output waits for its own RTMP server as the forwarder does, not
// only the pulse: the server reads its media only once the on_publish hook
// accepts, so a send may wait as long as the hook.
VOID TEST(GbMuxerTest, RtmpOutputWaitsForHook)
{
    srs_error_t err = srs_success;

    MockAppFactoryForGbMuxer factory;
    SrsUniquePtr<MockGbSessionForMuxer> mock_session(new MockGbSessionForMuxer());
    SrsUniquePtr<SrsGbMuxer> muxer(new SrsGbMuxer(mock_session.get()));
    muxer->setup("rtmp://127.0.0.1/live/[stream]");
    muxer->app_factory_ = &factory;

    MockGbConnectingRtmpClient *client = new MockGbConnectingRtmpClient(muxer.get(), NULL);
    factory.clients_.push_back(client);

    HELPER_EXPECT_SUCCESS(muxer->connect());
    EXPECT_TRUE(muxer->sdk_ == client);
    EXPECT_EQ(SRS_CONSTS_RTMP_TIMEOUT, factory.cto_);
    EXPECT_EQ(SRS_CONSTS_RTMP_TIMEOUT, factory.sto_);
}

// A connect while another one is in progress is refused and makes no second
// client. A connect that fails leaves no client, and the next media connects
// again.
VOID TEST(GbMuxerTest, ConnectWhileConnectingRefused)
{
    srs_error_t err = srs_success;

    MockAppFactoryForGbMuxer factory;
    SrsUniquePtr<MockGbSessionForMuxer> mock_session(new MockGbSessionForMuxer());
    SrsUniquePtr<SrsGbMuxer> muxer(new SrsGbMuxer(mock_session.get()));
    muxer->setup("rtmp://127.0.0.1/live/[stream]");
    muxer->app_factory_ = &factory;

    MockGbConnectingRtmpClient *failing = new MockGbConnectingRtmpClient(muxer.get(), NULL);
    failing->connect_error_ = srs_error_new(ERROR_SOCKET_CONNECT, "mock connect failed");
    MockGbConnectingRtmpClient *client = new MockGbConnectingRtmpClient(muxer.get(), NULL);
    factory.clients_.push_back(failing);
    factory.clients_.push_back(client);

    // The first connect fails; the one tried while it connects is refused.
    // The failed client is freed by the muxer, so read it before the next.
    HELPER_EXPECT_FAILED(muxer->connect());
    EXPECT_TRUE(muxer->sdk_ == NULL);
    EXPECT_EQ(1, factory.created_);

    // The next connect makes a new client.
    HELPER_EXPECT_SUCCESS(muxer->connect());
    EXPECT_EQ(ERROR_GB_RTMP_CONNECTING, srs_error_code(client->reentry_error_));
    EXPECT_FALSE(client->sdk_set_while_connecting_);
    EXPECT_EQ(2, factory.created_);
    EXPECT_TRUE(muxer->sdk_ == client);

    // Connected: a connect does nothing.
    HELPER_EXPECT_SUCCESS(muxer->connect());
    EXPECT_EQ(2, factory.created_);
}

// A message that a second media connection of the session writes while the
// first one's send yields is queued, and the first one sends it in turn, so
// only one coroutine sends through the RTMP client. When that send fails, the
// client is freed by the coroutine that used it, never under another one.
VOID TEST(GbMuxerTest, WriteWhileSendingQueued)
{
    srs_error_t err = srs_success;

    MockAppFactoryForGbMuxer factory;
    SrsUniquePtr<MockGbSessionForMuxer> mock_session(new MockGbSessionForMuxer());
    SrsUniquePtr<SrsGbMuxer> muxer(new SrsGbMuxer(mock_session.get()));
    muxer->assemble();
    muxer->setup("rtmp://127.0.0.1/live/[stream]");
    muxer->app_factory_ = &factory;

    int sends = 0;
    srs_error_t reentry_error = srs_success;
    MockGbSendingRtmpClient *client = new MockGbSendingRtmpClient(muxer.get(), &sends, &reentry_error);
    client->fail_at_ = 2;
    factory.clients_.push_back(client);
    HELPER_EXPECT_SUCCESS(muxer->connect());
    EXPECT_TRUE(muxer->sdk_ == client);

    // The queue holds one video frame, and sends a frame once it has two.
    char *data = new char[4];
    memset(data, 0, 4);
    HELPER_EXPECT_SUCCESS(muxer->rtmp_write_packet(SrsFrameTypeVideo, 0, data, 4));
    EXPECT_EQ(0, sends);

    // The first send writes the next frame meanwhile, which is queued, not sent,
    // and the frame after the first is sent next, whose send fails.
    data = new char[4];
    memset(data, 0, 4);
    HELPER_EXPECT_FAILED(muxer->rtmp_write_packet(SrsFrameTypeVideo, 40, data, 4));
    HELPER_EXPECT_SUCCESS(reentry_error);
    EXPECT_EQ(2, sends);
    EXPECT_TRUE(muxer->sdk_ == NULL);
}

// A session's end interrupts its media connection in a send of the RTMP
// output, which fails and frees the client. The rest of that connection's
// pack, and any media after it, makes no new client: the ended session never
// publishes again, and its cycle's wait for the connection to leave the muxer
// ends.
VOID TEST(GbMuxerTest, NoConnectAfterSessionEnded)
{
    srs_error_t err = srs_success;

    MockAppFactoryForGbMuxer factory;
    SrsUniquePtr<MockGbSessionForMuxer> mock_session(new MockGbSessionForMuxer());
    SrsUniquePtr<SrsGbMuxer> muxer(new SrsGbMuxer(mock_session.get()));
    muxer->assemble();
    muxer->setup("rtmp://127.0.0.1/live/[stream]");
    muxer->app_factory_ = &factory;

    int sends = 0;
    factory.clients_.push_back(new MockGbEndingRtmpClient(mock_session.get(), 2, &sends));

    // While the session lives, the first audio connects, and the queue sends
    // a frame once it has two.
    char *data = new char[4];
    memset(data, 0, 4);
    HELPER_EXPECT_SUCCESS(muxer->rtmp_write_packet(SrsFrameTypeAudio, 0, data, 4));
    EXPECT_EQ(1, factory.created_);
    EXPECT_TRUE(muxer->sdk_ != NULL);
    EXPECT_EQ(0, sends);

    data = new char[4];
    memset(data, 0, 4);
    HELPER_EXPECT_SUCCESS(muxer->rtmp_write_packet(SrsFrameTypeAudio, 23, data, 4));
    EXPECT_EQ(1, sends);
    EXPECT_FALSE(mock_session->is_ended());

    // The session ends in the next send, which fails and frees the client.
    data = new char[4];
    memset(data, 0, 4);
    HELPER_EXPECT_FAILED(muxer->rtmp_write_packet(SrsFrameTypeAudio, 46, data, 4));
    EXPECT_EQ(2, sends);
    EXPECT_TRUE(mock_session->is_ended());
    EXPECT_TRUE(muxer->sdk_ == NULL);

    // The video of the same pack, and the media after it, connect no more.
    data = new char[4];
    memset(data, 0, 4);
    err = muxer->rtmp_write_packet(SrsFrameTypeVideo, 50, data, 4);
    EXPECT_EQ(ERROR_GB_SESSION_ENDED, srs_error_code(err));
    srs_freep(err);

    data = new char[4];
    memset(data, 0, 4);
    err = muxer->rtmp_write_packet(SrsFrameTypeAudio, 69, data, 4);
    EXPECT_EQ(ERROR_GB_SESSION_ENDED, srs_error_code(err));
    srs_freep(err);

    EXPECT_EQ(1, factory.created_);
    EXPECT_TRUE(muxer->sdk_ == NULL);
    EXPECT_EQ(2, sends);
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
    body_error_ = srs_success;
}

MockHttpMessageForGbPublish::~MockHttpMessageForGbPublish()
{
    srs_freep(mock_conn_);
    srs_freep(body_error_);
}

srs_error_t MockHttpMessageForGbPublish::body_read_all(std::string &body)
{
    if (body_error_ != srs_success) {
        return srs_error_copy(body_error_);
    }
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

void MockGbSessionForApiPublish::on_media_disconnected(ISrsGbMediaTcpConn *media)
{
}

bool MockGbSessionForApiPublish::is_ended()
{
    return false;
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

// Test SrsGoApiGbPublish::do_serve_http - an SSRC that is not a decimal number
// of 1 to 2^32-1 is refused, and makes no session
VOID TEST(GB28181Test, GoApiGbPublishRefusesBadSsrc)
{
    srs_error_t err;

    const char *ssrcs[] = {
        "camera", "", "12a", "a12", "-1", "+1", " 1", "1 ", "0x10", "1.5",
        "0", "0000000000", "4294967296", "99999999999999999999999",
    };

    for (size_t i = 0; i < sizeof(ssrcs) / sizeof(ssrcs[0]); i++) {
        SrsUniquePtr<MockResponseWriter> mock_writer(new MockResponseWriter());
        SrsUniquePtr<MockHttpMessageForGbPublish> mock_request(new MockHttpMessageForGbPublish());
        SrsUniquePtr<MockResourceManagerForGbPublish> mock_manager(new MockResourceManagerForGbPublish());
        SrsUniquePtr<MockAppFactoryForGbPublish> mock_factory(new MockAppFactoryForGbPublish());
        SrsUniquePtr<MockAppConfigForGbListener> mock_config(new MockAppConfigForGbListener());

        SrsConfDirective *conf = new SrsConfDirective();
        conf->name_ = "stream_caster";
        SrsUniquePtr<SrsGoApiGbPublish> api(new SrsGoApiGbPublish(conf));
        api->config_ = mock_config.get();
        api->gb_manager_ = mock_manager.get();
        api->app_factory_ = mock_factory.get();

        mock_request->body_content_ = std::string("{\"id\":\"34020000001320000001\",\"ssrc\":\"") + ssrcs[i] + "\"}";

        SrsUniquePtr<SrsJsonObject> res(SrsJsonAny::object());
        err = api->do_serve_http(mock_writer.get(), mock_request.get(), res.get());
        EXPECT_TRUE(err != srs_success) << "ssrc=" << ssrcs[i];
        EXPECT_EQ(ERROR_HTTP_DATA_INVALID, srs_error_code(err)) << "ssrc=" << ssrcs[i];
        srs_freep(err);

        EXPECT_TRUE(mock_manager->empty()) << "ssrc=" << ssrcs[i];
        EXPECT_TRUE(res->get_property("port") == NULL) << "ssrc=" << ssrcs[i];

        api->config_ = NULL;
        api->gb_manager_ = NULL;
        api->app_factory_ = NULL;
        srs_freep(conf);
    }
}

// Test SrsGoApiGbPublish::do_serve_http - an SSRC takes the whole 32 bits of an
// RTP SSRC, with leading zeros as GB28181 writes it
VOID TEST(GB28181Test, GoApiGbPublishSsrcOf32Bits)
{
    srs_error_t err;

    struct {
        const char *str;
        uint64_t value;
    } ssrcs[] = {
        {"0100000001", 100000001},
        {"1", 1},
        {"2147483648", 2147483648ULL},
        {"3000000000", 3000000000ULL},
        {"4294967295", 4294967295ULL},
        {"00004294967295", 4294967295ULL},
    };

    for (size_t i = 0; i < sizeof(ssrcs) / sizeof(ssrcs[0]); i++) {
        SrsUniquePtr<MockResponseWriter> mock_writer(new MockResponseWriter());
        SrsUniquePtr<MockHttpMessageForGbPublish> mock_request(new MockHttpMessageForGbPublish());
        SrsUniquePtr<MockResourceManagerForGbPublish> mock_manager(new MockResourceManagerForGbPublish());
        SrsUniquePtr<MockAppFactoryForGbPublish> mock_factory(new MockAppFactoryForGbPublish());
        SrsUniquePtr<MockAppConfigForGbListener> mock_config(new MockAppConfigForGbListener());

        SrsConfDirective *conf = new SrsConfDirective();
        conf->name_ = "stream_caster";
        SrsUniquePtr<SrsGoApiGbPublish> api(new SrsGoApiGbPublish(conf));
        api->config_ = mock_config.get();
        api->gb_manager_ = mock_manager.get();
        api->app_factory_ = mock_factory.get();

        mock_request->body_content_ = std::string("{\"id\":\"34020000001320000001\",\"ssrc\":\"") + ssrcs[i].str + "\"}";

        SrsUniquePtr<SrsJsonObject> res(SrsJsonAny::object());
        err = api->do_serve_http(mock_writer.get(), mock_request.get(), res.get());
        HELPER_EXPECT_SUCCESS(err);

        ISrsResource *session = mock_manager->find_by_id("34020000001320000001");
        EXPECT_TRUE(session != NULL) << "ssrc=" << ssrcs[i].str;
        EXPECT_EQ(session, mock_manager->find_by_fast_id(ssrcs[i].value)) << "ssrc=" << ssrcs[i].str;

        // Stop the session's coroutine, as the success test does.
        SrsSharedResource<ISrsGbSession> *wrapper = dynamic_cast<SrsSharedResource<ISrsGbSession> *>(session);
        MockGbSessionForApiPublish *mock_session = wrapper ? dynamic_cast<MockGbSessionForApiPublish *>(wrapper->get()) : NULL;
        if (mock_session && mock_session->owner_coroutine_) {
            mock_session->owner_coroutine_->interrupt();
        }
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);

        api->config_ = NULL;
        api->gb_manager_ = NULL;
        api->app_factory_ = NULL;
        srs_freep(conf);
    }
}

MockGbListenerForServer::MockGbListenerForServer() : SrsGbListener(NULL)
{
    initialize_count_ = 0;
    initialize_conf_ = NULL;
    listen_count_ = 0;
    close_count_ = 0;
}

MockGbListenerForServer::~MockGbListenerForServer()
{
}

srs_error_t MockGbListenerForServer::initialize(SrsConfDirective *conf)
{
    initialize_count_++;
    initialize_conf_ = conf;
    return srs_success;
}

srs_error_t MockGbListenerForServer::listen()
{
    listen_count_++;
    return srs_success;
}

void MockGbListenerForServer::close()
{
    close_count_++;
}

VOID TEST(ServerTest, ListenStartsGbStreamCasterThroughInjectedListener)
{
    srs_error_t err;

    SrsUniquePtr<SrsServer> server(new SrsServer());
    server->assemble();

    SrsUniquePtr<SrsConfDirective> gb_conf(mock_server_stream_caster_conf("gb28181", true));

    MockAppConfigForServerListen config;
    config.stream_casters_.push_back(gb_conf.get());
    server->config_ = &config;

    MockGbListenerForServer gb;
    ISrsListener *original = server->stream_caster_gb28181_;
    server->stream_caster_gb28181_ = &gb;

    HELPER_EXPECT_SUCCESS(server->listen());

    // The GB caster is initialized with its own directive, then started.
    EXPECT_EQ(1, gb.initialize_count_);
    EXPECT_TRUE(gb.initialize_conf_ == gb_conf.get());
    EXPECT_EQ(1, gb.listen_count_);

    server->stream_caster_gb28181_ = dynamic_cast<SrsGbListener *>(original);
    server->config_ = NULL;
}

VOID TEST(ServerTest, DisposeClosesInjectedGbStreamCaster)
{
    SrsUniquePtr<SrsServer> server(new SrsServer());
    server->assemble();

    MockGbListenerForServer gb;
    ISrsListener *original = server->stream_caster_gb28181_;
    server->stream_caster_gb28181_ = &gb;

    server->dispose();
    EXPECT_EQ(1, gb.close_count_);

    server->stream_caster_gb28181_ = dynamic_cast<SrsGbListener *>(original);
}

VOID TEST(ServerTest, ConstructionPassesItselfToGbStreamCaster)
{
    SrsUniquePtr<SrsServer> server(new SrsServer());

    // The GB caster registers its API on this server's mux, not on the global server's.
    SrsGbListener *gb = dynamic_cast<SrsGbListener *>(server->stream_caster_gb28181_);
    ASSERT_TRUE(gb != NULL);
    EXPECT_TRUE(gb->api_server_owner_ == static_cast<ISrsApiServerOwner *>(server.get()));
}
#endif

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
    rtp_cipher_dropped_ = false;
    binding_request_error_ = srs_success;
    binding_requests_ = 0;
    udp_network_ = NULL;
}

MockRtcConnectionForUdpNetwork::~MockRtcConnectionForUdpNetwork()
{
    srs_freep(on_dtls_alert_error_);
    srs_freep(binding_request_error_);
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

srs_error_t MockRtcConnectionForUdpNetwork::on_rtp_cipher(char *data, int nb_data, bool *dropped)
{
    on_rtp_cipher_called_ = true;
    *dropped = rtp_cipher_dropped_;
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
    binding_requests_++;
    return srs_error_copy(binding_request_error_);
}

ISrsRtcNetwork *MockRtcConnectionForUdpNetwork::udp()
{
    return udp_network_;
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
    rtp_cipher_dropped_ = false;
    srs_freep(binding_request_error_);
    binding_requests_ = 0;
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
    std::map<std::string, ISrsResource *>::iterator it = name_map_.find(name);
    if (it != name_map_.end()) {
        return it->second;
    }
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
    name_map_.clear();
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

MockRtcBlackholeForUdpNetwork::MockRtcBlackholeForUdpNetwork()
{
    sendto_count_ = 0;
    last_data_ = NULL;
    last_len_ = 0;
}

MockRtcBlackholeForUdpNetwork::~MockRtcBlackholeForUdpNetwork()
{
}

srs_error_t MockRtcBlackholeForUdpNetwork::initialize()
{
    return srs_success;
}

void MockRtcBlackholeForUdpNetwork::sendto(void *data, int len)
{
    sendto_count_++;
    last_data_ = data;
    last_len_ = len;
}

// The UDP network captures the black hole at construction, so a test can replace it before any packet arrives.
VOID TEST(RtcUdpNetworkTest, ConstructionCapturesBlackhole)
{
    MockRtcConnectionForUdpNetwork conn;
    MockEphemeralDelta delta;
    SrsRtcUdpNetwork udp_network(&conn, &delta);

    EXPECT_TRUE(udp_network.blackhole_ != NULL);
    EXPECT_EQ((ISrsRtcBlackhole *)_srs_blackhole, udp_network.blackhole_);
}

// A received RTP packet goes to the black hole after it is decrypted, with its plaintext size, and is then delivered.
// A packet the cipher hook drops, or one that fails to decrypt, never reaches the black hole.
VOID TEST(RtcUdpNetworkTest, OnRtpWritesPlaintextToInjectedBlackhole)
{
    srs_error_t err;

    MockRtcConnectionForUdpNetwork conn;
    MockEphemeralDelta delta;
    MockRtcTransportForUdpNetwork transport;
    MockRtcBlackholeForUdpNetwork blackhole;

    SrsRtcUdpNetwork udp_network(&conn, &delta);
    srs_freep(udp_network.transport_);
    udp_network.transport_ = &transport;
    udp_network.blackhole_ = &blackhole;

    char data[200];
    memset(data, 0x80, sizeof(data));

    HELPER_EXPECT_SUCCESS(udp_network.on_rtp(data, sizeof(data)));
    EXPECT_EQ(1, blackhole.sendto_count_);
    EXPECT_EQ((void *)data, blackhole.last_data_);
    // The mock transport strips 10 bytes of SRTP overhead.
    EXPECT_EQ(190, blackhole.last_len_);
    EXPECT_TRUE(conn.on_rtp_plaintext_called_);

    conn.reset();
    transport.reset();
    conn.rtp_cipher_dropped_ = true;
    HELPER_EXPECT_SUCCESS(udp_network.on_rtp(data, sizeof(data)));
    EXPECT_FALSE(transport.unprotect_rtp_called_);
    EXPECT_FALSE(conn.on_rtp_plaintext_called_);
    EXPECT_EQ(1, blackhole.sendto_count_);

    conn.reset();
    transport.reset();
    srs_error_t unprotect_err = srs_error_new(ERROR_RTC_SRTP_UNPROTECT, "mock unprotect error");
    transport.set_unprotect_rtp_error(unprotect_err);
    srs_freep(unprotect_err);
    HELPER_EXPECT_FAILED(udp_network.on_rtp(data, sizeof(data)));
    EXPECT_FALSE(conn.on_rtp_plaintext_called_);
    EXPECT_EQ(1, blackhole.sendto_count_);

    transport.reset();
    udp_network.transport_ = NULL;
    udp_network.blackhole_ = NULL;
}

// A received RTCP packet goes to the black hole after it is decrypted, with its plaintext size, and is then delivered.
// A packet that fails to decrypt never reaches the black hole.
VOID TEST(RtcUdpNetworkTest, OnRtcpWritesPlaintextToInjectedBlackhole)
{
    srs_error_t err;

    MockRtcConnectionForUdpNetwork conn;
    MockEphemeralDelta delta;
    MockRtcTransportForUdpNetwork transport;
    MockRtcBlackholeForUdpNetwork blackhole;

    SrsRtcUdpNetwork udp_network(&conn, &delta);
    srs_freep(udp_network.transport_);
    udp_network.transport_ = &transport;
    udp_network.blackhole_ = &blackhole;

    char data[100];
    memset(data, 0xC8, sizeof(data));

    HELPER_EXPECT_SUCCESS(udp_network.on_rtcp(data, sizeof(data)));
    EXPECT_EQ(1, blackhole.sendto_count_);
    EXPECT_EQ((void *)data, blackhole.last_data_);
    // The mock transport strips 14 bytes of SRTCP overhead.
    EXPECT_EQ(86, blackhole.last_len_);
    EXPECT_TRUE(conn.on_rtcp_called_);

    conn.reset();
    transport.reset();
    srs_error_t unprotect_err = srs_error_new(ERROR_RTC_SRTP_UNPROTECT, "mock unprotect error");
    transport.set_unprotect_rtcp_error(unprotect_err);
    srs_freep(unprotect_err);
    HELPER_EXPECT_FAILED(udp_network.on_rtcp(data, sizeof(data)));
    EXPECT_FALSE(conn.on_rtcp_called_);
    EXPECT_EQ(1, blackhole.sendto_count_);

    transport.reset();
    udp_network.transport_ = NULL;
    udp_network.blackhole_ = NULL;
}

// Every received STUN packet goes to the black hole as it arrived, and the binding response goes there too, but only
// after it was sent to the peer.
VOID TEST(RtcUdpNetworkTest, OnStunWritesStunAndResponseToInjectedBlackhole)
{
    srs_error_t err;

    MockRtcConnectionForUdpNetwork conn;
    MockEphemeralDelta delta;
    MockUdpMuxSocket socket;
    MockRtcBlackholeForUdpNetwork blackhole;

    SrsRtcUdpNetwork udp_network(&conn, &delta);
    udp_network.sendonly_skt_ = &socket;
    udp_network.blackhole_ = &blackhole;

    char data[100];
    memset(data, 0, sizeof(data));

    // Not a binding request, so only the packet itself.
    SrsStunPacket response;
    response.set_message_type(BindingResponse);
    HELPER_EXPECT_SUCCESS(udp_network.on_stun(&response, data, sizeof(data)));
    EXPECT_EQ(1, blackhole.sendto_count_);
    EXPECT_EQ((void *)data, blackhole.last_data_);
    EXPECT_EQ(100, blackhole.last_len_);
    EXPECT_EQ(0, socket.sendto_called_count_);

    // A binding request, then the response that was sent for it.
    SrsStunPacket request;
    request.set_message_type(BindingRequest);
    request.set_local_ufrag("local_user");
    request.set_remote_ufrag("remote_user");
    request.set_transcation_id("transaction123");
    HELPER_EXPECT_SUCCESS(udp_network.on_stun(&request, data, sizeof(data)));
    EXPECT_EQ(1, socket.sendto_called_count_);
    EXPECT_EQ(3, blackhole.sendto_count_);
    EXPECT_EQ(socket.last_sendto_size_, blackhole.last_len_);

    // The response fails to send, so only the request.
    socket.reset();
    srs_error_t sendto_err = srs_error_new(ERROR_SOCKET_WRITE, "mock sendto error");
    socket.set_sendto_error(sendto_err);
    srs_freep(sendto_err);
    HELPER_EXPECT_FAILED(udp_network.on_stun(&request, data, sizeof(data)));
    EXPECT_EQ(1, socket.sendto_called_count_);
    EXPECT_EQ(4, blackhole.sendto_count_);
    EXPECT_EQ((void *)data, blackhole.last_data_);
    EXPECT_EQ(100, blackhole.last_len_);

    udp_network.sendonly_skt_ = NULL;
    udp_network.blackhole_ = NULL;
}

// A binding request switches the session to its peer address only after the connection accepted it, so a request the
// connection rejects, such as one without a valid MESSAGE-INTEGRITY, neither redirects the session's traffic nor adds
// an address to it.
VOID TEST(RtcUdpNetworkTest, OnStunFromPeerSwitchesAddressOnlyWhenAccepted)
{
    srs_error_t err;

    MockRtcConnectionForUdpNetwork conn;
    MockEphemeralDelta delta;
    MockResourceManagerForUdpNetwork manager;
    MockUdpMuxSocket socket;
    socket.peer_ip_ = "192.168.1.100";
    socket.peer_port_ = 5000;
    socket.peer_id_ = "192.168.1.100:5000";

    SrsRtcUdpNetwork udp_network(&conn, &delta);
    udp_network.conn_manager_ = &manager;

    SrsStunPacket request;
    request.set_message_type(BindingRequest);
    request.set_local_ufrag("local_user");
    request.set_remote_ufrag("remote_user");
    request.set_transcation_id("transaction123");

    char data[100];
    memset(data, 0, sizeof(data));

    srs_error_t rejected = srs_error_new(ERROR_RTC_STUN, "mock integrity error");
    conn.binding_request_error_ = srs_error_copy(rejected);
    srs_freep(rejected);
    HELPER_EXPECT_FAILED(udp_network.on_stun(&socket, &request, data, sizeof(data)));
    EXPECT_TRUE(udp_network.sendonly_skt_ == NULL);
    EXPECT_TRUE(udp_network.peer_addresses_.empty());
    EXPECT_TRUE(manager.id_map_.empty());
    EXPECT_EQ(0, socket.sendto_called_count_);

    srs_freep(conn.binding_request_error_);
    HELPER_EXPECT_SUCCESS(udp_network.on_stun(&socket, &request, data, sizeof(data)));
    EXPECT_EQ((ISrsUdpMuxSocket *)&socket, udp_network.sendonly_skt_);
    EXPECT_EQ(1u, udp_network.peer_addresses_.size());
    EXPECT_TRUE(manager.id_map_.find("192.168.1.100:5000") != manager.id_map_.end());
    EXPECT_EQ(1, socket.sendto_called_count_);

    // The mock socket copies itself as the cached address, so it must not be freed by the network.
    udp_network.peer_addresses_.clear();
    udp_network.sendonly_skt_ = NULL;
    udp_network.conn_manager_ = NULL;
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
    if (write_error_ != srs_success) {
        return srs_error_copy(write_error_);
    }

    // Store the data of all buffers as one write.
    std::string data;
    for (int i = 0; i < iov_size; i++) {
        data.append((char *)iov[i].iov_base, iov[i].iov_len);
    }
    written_data_.push_back(data);
    send_bytes_ += data.size();

    if (nwrite) {
        *nwrite = data.size();
    }

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

    // Verify that the STUN binding response was written in one write, after its 2-byte length prefix.
    EXPECT_EQ(1, (int)mock_io->written_data_.size());
    EXPECT_TRUE(mock_io->written_data_[0].size() > 2);

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
    tcp_conn->assemble();

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

srs_error_t MockRtcConnectionForTcpConnHandshake::on_rtp_cipher(char *data, int nb_data, bool *dropped)
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

static std::string build_rtc_tcp_stun_binding_request(std::string username)
{
    char stun_buf[128];
    memset(stun_buf, 0, sizeof(stun_buf));

    int username_len = (int)username.size();
    int username_padded = (username_len + 3) / 4 * 4;
    int message_size = 4 + username_padded;
    int stun_size = 20 + message_size;

    // STUN header: BindingRequest, message length, magic cookie and transaction ID.
    stun_buf[0] = 0x00;
    stun_buf[1] = 0x01;
    stun_buf[2] = (message_size >> 8) & 0xff;
    stun_buf[3] = message_size & 0xff;
    stun_buf[4] = 0x21;
    stun_buf[5] = 0x12;
    stun_buf[6] = 0xa4;
    stun_buf[7] = 0x42;
    for (int i = 8; i < 20; ++i) {
        stun_buf[i] = i - 8;
    }

    // USERNAME attribute.
    stun_buf[20] = 0x00;
    stun_buf[21] = 0x06;
    stun_buf[22] = (username_len >> 8) & 0xff;
    stun_buf[23] = username_len & 0xff;
    memcpy(stun_buf + 24, username.data(), username_len);

    // RFC4571 frame: two-byte packet length followed by the STUN packet.
    std::string frame;
    frame.push_back((stun_size >> 8) & 0xff);
    frame.push_back(stun_size & 0xff);
    frame.append(stun_buf, stun_size);
    return frame;
}

// Test SrsRtcTcpConn::handshake - major use scenario
VOID TEST(RtcTcpConnTest, HandshakeWithStunBindingRequest)
{
    srs_error_t err;

    std::string read_data = build_rtc_tcp_stun_binding_request("test:session");

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
    tcp_conn->assemble();

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

// Issue #4642: A second TCP connection for the same RTC session must not
// displace the first owner, otherwise the first connection keeps a stale
// session pointer after the session interrupts only the recorded owner.
VOID TEST(ReproduceIssue4642, RejectSecondTcpConnForSameRtcSession)
{
    srs_error_t err;
    std::string read_data = build_rtc_tcp_stun_binding_request("test:session");

    SrsUniquePtr<MockResourceManagerForTcpConnHandshake> mock_conn_manager(new MockResourceManagerForTcpConnHandshake());
    SrsUniquePtr<MockRtcConnectionForTcpConnHandshake> mock_session(new MockRtcConnectionForTcpConnHandshake());
    SrsUniquePtr<MockEphemeralDelta> mock_delta(new MockEphemeralDelta());
    SrsUniquePtr<SrsRtcTcpNetwork> tcp_network(new SrsRtcTcpNetwork(mock_session.get(), mock_delta.get()));

    mock_session->tcp_network_ = tcp_network.get();
    mock_conn_manager->session_to_return_ = mock_session.get();

    SrsUniquePtr<MockProtocolReadWriterForTcpNetwork> io_a(new MockProtocolReadWriterForTcpNetwork());
    SrsUniquePtr<MockProtocolReadWriterForTcpNetwork> io_b(new MockProtocolReadWriterForTcpNetwork());
    io_a->set_read_data(read_data);
    io_b->set_read_data(read_data);

    SrsRtcTcpConn *raw_a = new SrsRtcTcpConn(io_a.get(), "192.168.1.10", 10000);
    raw_a->assemble();
    SrsRtcTcpConn *raw_b = new SrsRtcTcpConn(io_b.get(), "192.168.1.11", 10001);
    raw_b->assemble();
    SrsUniquePtr<SrsSharedResource<ISrsRtcTcpConn> > wrapper_a(new SrsSharedResource<ISrsRtcTcpConn>(raw_a));
    SrsUniquePtr<SrsSharedResource<ISrsRtcTcpConn> > wrapper_b(new SrsSharedResource<ISrsRtcTcpConn>(raw_b));

    raw_a->conn_manager_ = mock_conn_manager.get();
    raw_a->wrapper_ = wrapper_a.get();
    raw_b->conn_manager_ = mock_conn_manager.get();
    raw_b->wrapper_ = wrapper_b.get();

    HELPER_EXPECT_SUCCESS(raw_a->handshake());
    EXPECT_EQ(raw_a, tcp_network->owner().get());
    EXPECT_EQ(mock_session.get(), raw_a->session_);

    err = raw_b->handshake();
    EXPECT_TRUE(err != srs_success);
    if (err != srs_success) {
        EXPECT_EQ(ERROR_RTC_TCP_UNIQUE, srs_error_code(err));
        srs_freep(err);
    }

    EXPECT_EQ(raw_a, tcp_network->owner().get());
    EXPECT_EQ(mock_session.get(), raw_a->session_);
    EXPECT_TRUE(raw_b->session_ == NULL);

    // Prevent the connection and mock socket owners from deleting the same sockets.
    raw_a->skt_ = NULL;
    raw_b->skt_ = NULL;
    raw_a->conn_manager_ = NULL;
    raw_b->conn_manager_ = NULL;
    raw_a->wrapper_ = NULL;
    raw_b->wrapper_ = NULL;
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
    tcp_conn->assemble();

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
    tcp_conn->assemble();

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

// The WebRTC over TCP connection is created for every TCP client, so a test has to be able to
// replace its context and socket before anything uses them. Construction must therefore take no
// context id and wire no bandwidth counter.
VOID TEST(RtcTcpConnTest, ConstructionReachesNoCollaborator)
{
    SrsUniquePtr<MockProtocolReadWriterForTcpNetwork> io(new MockProtocolReadWriterForTcpNetwork());
    SrsUniquePtr<SrsRtcTcpConn> conn(new SrsRtcTcpConn(io.get(), "192.168.1.100", 8000));

    // GOAL: construction only captures the process context, and uses it later.
    EXPECT_TRUE(_srs_context == conn->context_);
    EXPECT_TRUE(conn->cid_.empty());

    SrsNetworkDelta *delta = dynamic_cast<SrsNetworkDelta *>(conn->delta_);
    EXPECT_TRUE(NULL == delta->in_);
    EXPECT_TRUE(NULL == delta->out_);

    // The mock socket is owned by the test.
    conn->skt_ = NULL;
}

VOID TEST(RtcTcpConnTest, AssembleWiresInjectedContextAndSocket)
{
    SrsUniquePtr<MockProtocolReadWriterForTcpNetwork> io(new MockProtocolReadWriterForTcpNetwork());
    SrsUniquePtr<SrsRtcTcpConn> conn(new SrsRtcTcpConn(io.get(), "192.168.1.100", 8000));

    MockContextForRtmpConn context;
    context.id_ = SrsContextId().set_value("rtc-tcp-cid");
    conn->context_ = &context;

    // Replace the socket before assemble(), so the counters must measure the replacement.
    SrsUniquePtr<MockProtocolReadWriterForTcpNetwork> io2(new MockProtocolReadWriterForTcpNetwork());
    conn->skt_ = io2.get();
    conn->assemble();

    // GOAL: the connection takes the id of the injected context, without generating or switching one.
    EXPECT_STREQ("rtc-tcp-cid", conn->get_id().c_str());
    EXPECT_EQ(0, context.generate_id_count_);
    EXPECT_EQ(0, context.set_id_count_);

    // GOAL: the bandwidth counters measure the socket present at assemble().
    SrsNetworkDelta *delta = dynamic_cast<SrsNetworkDelta *>(conn->delta_);
    EXPECT_TRUE(io2.get() == delta->in_);
    EXPECT_TRUE(io2.get() == delta->out_);

    conn->skt_ = NULL;
    conn->context_ = NULL;
}

VOID TEST(RtcTcpConnTest, DoCycleSwitchesInjectedContext)
{
    srs_error_t err;

    // No data to read, so the handshake fails right after the context switch.
    SrsUniquePtr<MockProtocolReadWriterForTcpNetwork> io(new MockProtocolReadWriterForTcpNetwork());
    SrsUniquePtr<SrsRtcTcpConn> conn(new SrsRtcTcpConn(io.get(), "192.168.1.100", 8000));

    MockContextForRtmpConn context;
    context.id_ = SrsContextId().set_value("rtc-tcp-cid");
    conn->context_ = &context;
    conn->assemble();

    MockInterruptableForRtcTcpConn coroutine;
    MockContextIdSetterForRtcTcpConn cid_setter;
    conn->setup_owner(NULL, &coroutine, &cid_setter);

    // Assemble took the id, so change the mock to observe what do_cycle() switches to.
    context.id_ = SrsContextId().set_value("other-cid");

    HELPER_EXPECT_FAILED(conn->do_cycle());

    // GOAL: the connection switches the injected context, not the process one, to its own id.
    EXPECT_EQ(1, context.set_id_count_);
    EXPECT_STREQ("rtc-tcp-cid", context.id_.c_str());
    EXPECT_TRUE(cid_setter.set_cid_called_);
    EXPECT_STREQ("rtc-tcp-cid", cid_setter.received_cid_.c_str());

    conn->skt_ = NULL;
    conn->context_ = NULL;
}

MockRtcTcpNetworkForTcpConn::MockRtcTcpNetworkForTcpConn()
{
    set_owner_count_ = 0;
    sendonly_skt_ = NULL;
    peer_port_ = 0;
    on_stun_count_ = 0;
}

MockRtcTcpNetworkForTcpConn::~MockRtcTcpNetworkForTcpConn()
{
}

void MockRtcTcpNetworkForTcpConn::set_owner(SrsSharedResource<ISrsRtcTcpConn> v)
{
    set_owner_count_++;
    owner_ = v;
}

SrsSharedResource<ISrsRtcTcpConn> MockRtcTcpNetworkForTcpConn::owner()
{
    return owner_;
}

void MockRtcTcpNetworkForTcpConn::update_sendonly_socket(ISrsProtocolReadWriter *skt)
{
    sendonly_skt_ = skt;
}

void MockRtcTcpNetworkForTcpConn::set_peer_id(const std::string &ip, int port)
{
    peer_ip_ = ip;
    peer_port_ = port;
}

srs_error_t MockRtcTcpNetworkForTcpConn::initialize(SrsSessionConfig *cfg, bool dtls, bool srtp)
{
    return srs_success;
}

void MockRtcTcpNetworkForTcpConn::set_state(SrsRtcNetworkState state)
{
}

srs_error_t MockRtcTcpNetworkForTcpConn::on_dtls_handshake_done()
{
    return srs_success;
}

srs_error_t MockRtcTcpNetworkForTcpConn::on_dtls_alert(std::string type, std::string desc)
{
    return srs_success;
}

srs_error_t MockRtcTcpNetworkForTcpConn::on_dtls(char *data, int nb_data)
{
    return srs_success;
}

srs_error_t MockRtcTcpNetworkForTcpConn::protect_rtp(void *packet, int *nb_cipher)
{
    return srs_success;
}

srs_error_t MockRtcTcpNetworkForTcpConn::protect_rtcp(void *packet, int *nb_cipher)
{
    return srs_success;
}

srs_error_t MockRtcTcpNetworkForTcpConn::on_stun(SrsStunPacket *r, char *data, int nb_data)
{
    on_stun_count_++;
    return srs_success;
}

srs_error_t MockRtcTcpNetworkForTcpConn::on_rtp(char *data, int nb_data)
{
    return srs_success;
}

srs_error_t MockRtcTcpNetworkForTcpConn::on_rtcp(char *data, int nb_data)
{
    return srs_success;
}

bool MockRtcTcpNetworkForTcpConn::is_establelished()
{
    return false;
}

srs_error_t MockRtcTcpNetworkForTcpConn::write(void *buf, size_t size, ssize_t *nwrite)
{
    return srs_success;
}

// The handshake binds the TCP connection to the session's TCP network, so a test must be able to
// replace that network to observe the binding.
VOID TEST(RtcTcpConnTest, HandshakeBindsInjectedTcpNetwork)
{
    srs_error_t err;

    SrsUniquePtr<MockProtocolReadWriterForTcpNetwork> io(new MockProtocolReadWriterForTcpNetwork());
    io->set_read_data(build_rtc_tcp_stun_binding_request("test:session"));

    MockRtcTcpNetworkForTcpConn network;
    MockRtcConnectionForTcpConnHandshake session;
    session.tcp_network_ = &network;
    MockResourceManagerForTcpConnHandshake conn_manager;
    conn_manager.session_to_return_ = &session;

    SrsRtcTcpConn *conn = new SrsRtcTcpConn(io.get(), "192.168.1.100", 8000);
    conn->assemble();
    SrsUniquePtr<SrsSharedResource<ISrsRtcTcpConn> > wrapper(new SrsSharedResource<ISrsRtcTcpConn>(conn));
    conn->conn_manager_ = &conn_manager;
    conn->wrapper_ = wrapper.get();

    HELPER_EXPECT_SUCCESS(conn->handshake());

    // GOAL: the connection owns the injected network, which sends to its socket and handles the STUN.
    EXPECT_EQ(1, network.set_owner_count_);
    EXPECT_TRUE(conn == network.owner().get());
    EXPECT_TRUE(io.get() == network.sendonly_skt_);
    EXPECT_STREQ("192.168.1.100", network.peer_ip_.c_str());
    EXPECT_EQ(8000, network.peer_port_);
    EXPECT_EQ(1, network.on_stun_count_);
    EXPECT_TRUE(&session == conn->session_);

    conn->skt_ = NULL;
    conn->conn_manager_ = NULL;
    conn->wrapper_ = NULL;
    session.tcp_network_ = NULL;
    conn_manager.session_to_return_ = NULL;
    network.owner_ = SrsSharedResource<ISrsRtcTcpConn>();
}

VOID TEST(RtcTcpConnTest, HandshakeRejectsInjectedTcpNetworkAlreadyOwned)
{
    srs_error_t err;

    SrsUniquePtr<MockProtocolReadWriterForTcpNetwork> io(new MockProtocolReadWriterForTcpNetwork());
    io->set_read_data(build_rtc_tcp_stun_binding_request("test:session"));

    SrsUniquePtr<MockProtocolReadWriterForTcpNetwork> other_io(new MockProtocolReadWriterForTcpNetwork());
    SrsRtcTcpConn *other = new SrsRtcTcpConn(other_io.get(), "192.168.1.10", 10000);
    other->assemble();
    other->skt_ = NULL;

    MockRtcTcpNetworkForTcpConn network;
    network.owner_ = SrsSharedResource<ISrsRtcTcpConn>(other);
    MockRtcConnectionForTcpConnHandshake session;
    session.tcp_network_ = &network;
    MockResourceManagerForTcpConnHandshake conn_manager;
    conn_manager.session_to_return_ = &session;

    SrsRtcTcpConn *conn = new SrsRtcTcpConn(io.get(), "192.168.1.100", 8000);
    conn->assemble();
    SrsUniquePtr<SrsSharedResource<ISrsRtcTcpConn> > wrapper(new SrsSharedResource<ISrsRtcTcpConn>(conn));
    conn->conn_manager_ = &conn_manager;
    conn->wrapper_ = wrapper.get();

    // GOAL: a network that already has an owner rejects a second TCP connection, and keeps its owner.
    err = conn->handshake();
    EXPECT_EQ(ERROR_RTC_TCP_UNIQUE, srs_error_code(err));
    srs_freep(err);

    EXPECT_EQ(0, network.set_owner_count_);
    EXPECT_TRUE(other == network.owner().get());
    EXPECT_TRUE(NULL == network.sendonly_skt_);
    EXPECT_EQ(0, network.on_stun_count_);
    EXPECT_TRUE(NULL == conn->session_);

    conn->skt_ = NULL;
    conn->conn_manager_ = NULL;
    conn->wrapper_ = NULL;
    session.tcp_network_ = NULL;
    conn_manager.session_to_return_ = NULL;
}

// A client may close its TCP connection after the STUN handshake but before DTLS completes, and then
// reconnect over TCP for the same session. The closed connection must release the session's TCP
// network, otherwise the network keeps the closed socket and rejects the reconnection.
VOID TEST(RtcTcpConnTest, CycleReleasesNetworkWhenClosedBeforeEstablished)
{
    srs_error_t err;
    std::string read_data = build_rtc_tcp_stun_binding_request("test:session");

    MockResourceManagerForTcpConnHandshake conn_manager;
    MockRtcConnectionForTcpConnHandshake session;
    MockEphemeralDelta delta;
    SrsUniquePtr<SrsRtcTcpNetwork> network(new SrsRtcTcpNetwork(&session, &delta));
    session.tcp_network_ = network.get();
    conn_manager.session_to_return_ = &session;

    // The first connection completes the STUN handshake, then reads nothing more, so cycle() returns.
    SrsUniquePtr<MockProtocolReadWriterForTcpNetwork> io_a(new MockProtocolReadWriterForTcpNetwork());
    io_a->set_read_data(read_data);
    SrsRtcTcpConn *raw_a = new SrsRtcTcpConn(io_a.get(), "192.168.1.10", 10000);
    MockContextForRtmpConn context;
    raw_a->context_ = &context;
    raw_a->assemble();
    SrsSharedResource<ISrsRtcTcpConn> *wrapper_a = new SrsSharedResource<ISrsRtcTcpConn>(raw_a);
    raw_a->conn_manager_ = &conn_manager;
    MockInterruptableForRtcTcpConn coroutine;
    MockContextIdSetterForRtcTcpConn cid_setter;
    raw_a->setup_owner(wrapper_a, &coroutine, &cid_setter);

    HELPER_EXPECT_SUCCESS(raw_a->cycle());
    EXPECT_FALSE(network->is_establelished());

    // GOAL: the closed connection no longer owns the network, and its socket is no longer used to send.
    EXPECT_TRUE(NULL == network->owner().get());
    EXPECT_TRUE(NULL == network->sendonly_skt_);

    // The executor then frees its wrapper, which frees the connection and its socket.
    raw_a->on_executor_done(&coroutine);
    raw_a->skt_ = NULL;
    raw_a->context_ = NULL;
    srs_freep(wrapper_a);

    // GOAL: the same client reconnects over TCP for the same session.
    SrsUniquePtr<MockProtocolReadWriterForTcpNetwork> io_b(new MockProtocolReadWriterForTcpNetwork());
    io_b->set_read_data(read_data);
    SrsRtcTcpConn *raw_b = new SrsRtcTcpConn(io_b.get(), "192.168.1.10", 10001);
    raw_b->assemble();
    SrsUniquePtr<SrsSharedResource<ISrsRtcTcpConn> > wrapper_b(new SrsSharedResource<ISrsRtcTcpConn>(raw_b));
    raw_b->conn_manager_ = &conn_manager;
    raw_b->wrapper_ = wrapper_b.get();

    HELPER_EXPECT_SUCCESS(raw_b->handshake());
    EXPECT_TRUE(raw_b == network->owner().get());
    EXPECT_TRUE(io_b.get() == network->sendonly_skt_);

    raw_b->skt_ = NULL;
    raw_b->conn_manager_ = NULL;
    raw_b->wrapper_ = NULL;
    network->sendonly_skt_ = NULL;
    network->set_owner(SrsSharedResource<ISrsRtcTcpConn>());
    session.tcp_network_ = NULL;
    conn_manager.session_to_return_ = NULL;
}

// A released network has no socket, but DTLS may still try to send through it.
VOID TEST(RtcTcpNetworkTest, WriteWithoutSocketFails)
{
    srs_error_t err;

    MockRtcConnectionForTcpConnHandshake session;
    MockEphemeralDelta delta;
    SrsUniquePtr<SrsRtcTcpNetwork> network(new SrsRtcTcpNetwork(&session, &delta));

    // GOAL: the write fails with an error instead of crashing.
    char data[] = "dtls";
    HELPER_EXPECT_FAILED(network->write(data, sizeof(data), NULL));
}

// The TCP network captures the black hole at construction, so a test can replace it before any packet arrives.
VOID TEST(RtcTcpNetworkTest, ConstructionCapturesBlackhole)
{
    MockRtcConnectionForUdpNetwork conn;
    MockEphemeralDelta delta;
    SrsRtcTcpNetwork tcp_network(&conn, &delta);

    EXPECT_TRUE(tcp_network.blackhole_ != NULL);
    EXPECT_EQ((ISrsRtcBlackhole *)_srs_blackhole, tcp_network.blackhole_);
}

// A received RTP packet goes to the black hole after it is decrypted, with its plaintext size, and is then delivered.
// A packet the cipher hook drops, or one that fails to decrypt, never reaches the black hole.
VOID TEST(RtcTcpNetworkTest, OnRtpWritesPlaintextToInjectedBlackhole)
{
    srs_error_t err;

    MockRtcConnectionForUdpNetwork conn;
    MockEphemeralDelta delta;
    MockRtcTransportForUdpNetwork transport;
    MockRtcBlackholeForUdpNetwork blackhole;

    SrsRtcTcpNetwork tcp_network(&conn, &delta);
    srs_freep(tcp_network.transport_);
    tcp_network.transport_ = &transport;
    tcp_network.blackhole_ = &blackhole;

    char data[200];
    memset(data, 0x80, sizeof(data));

    HELPER_EXPECT_SUCCESS(tcp_network.on_rtp(data, sizeof(data)));
    EXPECT_EQ(1, blackhole.sendto_count_);
    EXPECT_EQ((void *)data, blackhole.last_data_);
    // The mock transport strips 10 bytes of SRTP overhead.
    EXPECT_EQ(190, blackhole.last_len_);
    EXPECT_TRUE(conn.on_rtp_plaintext_called_);

    conn.reset();
    transport.reset();
    conn.rtp_cipher_dropped_ = true;
    HELPER_EXPECT_SUCCESS(tcp_network.on_rtp(data, sizeof(data)));
    EXPECT_FALSE(transport.unprotect_rtp_called_);
    EXPECT_FALSE(conn.on_rtp_plaintext_called_);
    EXPECT_EQ(1, blackhole.sendto_count_);

    conn.reset();
    transport.reset();
    srs_error_t unprotect_err = srs_error_new(ERROR_RTC_SRTP_UNPROTECT, "mock unprotect error");
    transport.set_unprotect_rtp_error(unprotect_err);
    srs_freep(unprotect_err);
    HELPER_EXPECT_FAILED(tcp_network.on_rtp(data, sizeof(data)));
    EXPECT_FALSE(conn.on_rtp_plaintext_called_);
    EXPECT_EQ(1, blackhole.sendto_count_);

    transport.reset();
    tcp_network.transport_ = NULL;
    tcp_network.blackhole_ = NULL;
}

// A received RTCP packet goes to the black hole after it is decrypted, with its plaintext size, and is then delivered.
// A packet that fails to decrypt never reaches the black hole.
VOID TEST(RtcTcpNetworkTest, OnRtcpWritesPlaintextToInjectedBlackhole)
{
    srs_error_t err;

    MockRtcConnectionForUdpNetwork conn;
    MockEphemeralDelta delta;
    MockRtcTransportForUdpNetwork transport;
    MockRtcBlackholeForUdpNetwork blackhole;

    SrsRtcTcpNetwork tcp_network(&conn, &delta);
    srs_freep(tcp_network.transport_);
    tcp_network.transport_ = &transport;
    tcp_network.blackhole_ = &blackhole;

    char data[100];
    memset(data, 0xC8, sizeof(data));

    HELPER_EXPECT_SUCCESS(tcp_network.on_rtcp(data, sizeof(data)));
    EXPECT_EQ(1, blackhole.sendto_count_);
    EXPECT_EQ((void *)data, blackhole.last_data_);
    // The mock transport strips 14 bytes of SRTCP overhead.
    EXPECT_EQ(86, blackhole.last_len_);
    EXPECT_TRUE(conn.on_rtcp_called_);

    conn.reset();
    transport.reset();
    srs_error_t unprotect_err = srs_error_new(ERROR_RTC_SRTP_UNPROTECT, "mock unprotect error");
    transport.set_unprotect_rtcp_error(unprotect_err);
    srs_freep(unprotect_err);
    HELPER_EXPECT_FAILED(tcp_network.on_rtcp(data, sizeof(data)));
    EXPECT_FALSE(conn.on_rtcp_called_);
    EXPECT_EQ(1, blackhole.sendto_count_);

    transport.reset();
    tcp_network.transport_ = NULL;
    tcp_network.blackhole_ = NULL;
}

// Every received STUN packet goes to the black hole as it arrived, and the binding response goes there too, but only
// after it was sent to the peer.
VOID TEST(RtcTcpNetworkTest, OnStunWritesStunAndResponseToInjectedBlackhole)
{
    srs_error_t err;

    MockRtcConnectionForUdpNetwork conn;
    MockEphemeralDelta delta;
    MockProtocolReadWriterForTcpNetwork io;
    MockRtcBlackholeForUdpNetwork blackhole;

    SrsRtcTcpNetwork tcp_network(&conn, &delta);
    tcp_network.update_sendonly_socket(&io);
    tcp_network.set_peer_id("192.168.1.100", 5000);
    tcp_network.blackhole_ = &blackhole;

    char data[100];
    memset(data, 0, sizeof(data));

    // Not a binding request, so only the packet itself.
    SrsStunPacket response;
    response.set_message_type(BindingResponse);
    HELPER_EXPECT_SUCCESS(tcp_network.on_stun(&response, data, sizeof(data)));
    EXPECT_EQ(1, blackhole.sendto_count_);
    EXPECT_EQ((void *)data, blackhole.last_data_);
    EXPECT_EQ(100, blackhole.last_len_);
    EXPECT_TRUE(io.written_data_.empty());

    // A binding request, then the response that was sent for it, without the 2-byte length prefix.
    SrsStunPacket request;
    request.set_message_type(BindingRequest);
    request.set_local_ufrag("local_user");
    request.set_remote_ufrag("remote_user");
    request.set_transcation_id("transaction123");
    HELPER_EXPECT_SUCCESS(tcp_network.on_stun(&request, data, sizeof(data)));
    EXPECT_EQ(1, (int)io.written_data_.size());
    EXPECT_EQ(3, blackhole.sendto_count_);
    EXPECT_EQ((int)io.written_data_[0].size() - 2, blackhole.last_len_);

    // The response fails to send, so only the request.
    io.reset();
    srs_error_t write_err = srs_error_new(ERROR_SOCKET_WRITE, "mock write error");
    io.set_write_error(write_err);
    srs_freep(write_err);
    HELPER_EXPECT_FAILED(tcp_network.on_stun(&request, data, sizeof(data)));
    EXPECT_EQ(4, blackhole.sendto_count_);
    EXPECT_EQ((void *)data, blackhole.last_data_);
    EXPECT_EQ(100, blackhole.last_len_);

    tcp_network.sendonly_skt_ = NULL;
    tcp_network.blackhole_ = NULL;
}

// A client that closed its socket without a DTLS alert leaves its session at its address until the session times out,
// and a new client may get the same port. Its binding request goes to the session its username names, which binds and
// takes the address, not to the old session, which would reject it for the other password.
VOID TEST(RtcSessionManagerTest, BindingRequestFromOldSessionAddressGoesToNamedSession)
{
    srs_error_t err;

    MockResourceManagerForUdpNetwork manager;
    MockEphemeralDelta delta;
    MockRtcBlackholeForUdpNetwork blackhole;

    MockRtcConnectionForUdpNetwork old_conn;
    SrsRtcUdpNetwork old_network(&old_conn, &delta);
    old_network.blackhole_ = &blackhole;
    old_network.conn_manager_ = &manager;
    old_conn.udp_network_ = &old_network;
    old_conn.binding_request_error_ = srs_error_new(ERROR_RTC_STUN, "mock integrity mismatch");

    MockRtcConnectionForUdpNetwork conn;
    SrsRtcUdpNetwork network(&conn, &delta);
    network.blackhole_ = &blackhole;
    network.conn_manager_ = &manager;
    conn.udp_network_ = &network;

    MockUdpMuxSocket socket;
    socket.peer_ip_ = "127.0.0.1";
    socket.peer_port_ = 50000;
    socket.peer_id_ = "127.0.0.1:50000";
    socket.fast_id_ = 12345;
    manager.fast_id_map_[socket.fast_id_] = &old_conn;
    manager.id_map_[socket.peer_id_] = &old_conn;
    manager.name_map_["srvufrag:cliufrag"] = &conn;

    // The same STUN packet as over TCP, without the 2-byte length of RFC 4571.
    std::string stun = build_rtc_tcp_stun_binding_request("srvufrag:cliufrag").substr(2);
    socket.data_ = (char *)stun.data();
    socket.size_ = (int)stun.size();

    SrsRtcSessionManager session_manager;
    session_manager.conn_manager_ = &manager;
    HELPER_EXPECT_SUCCESS(session_manager.on_udp_packet(&socket));

    EXPECT_EQ(0, old_conn.binding_requests_);
    EXPECT_EQ(1, conn.binding_requests_);
    EXPECT_EQ(1, socket.sendto_called_count_);
    EXPECT_EQ((ISrsUdpMuxSocket *)&socket, network.sendonly_skt_);
    EXPECT_TRUE(old_network.sendonly_skt_ == NULL);

    // The address is the new session's now, so its next packets, such as DTLS, go there.
    EXPECT_EQ((ISrsResource *)&conn, manager.fast_id_map_[socket.fast_id_]);
    EXPECT_EQ((ISrsResource *)&conn, manager.id_map_[socket.peer_id_]);

    // The mock socket copies itself as the cached address, so it must not be freed by the network.
    session_manager.conn_manager_ = NULL;
    network.peer_addresses_.clear();
    network.sendonly_skt_ = NULL;
    network.conn_manager_ = NULL;
    network.blackhole_ = NULL;
    old_network.conn_manager_ = NULL;
    old_network.blackhole_ = NULL;
    socket.data_ = NULL;
}

// A session with no owner left has nothing to drive, and its cycle ends with no error.
VOID TEST(GbSessionCycleTest, CycleEndsWithoutOwner)
{
    srs_error_t err;

    SrsUniquePtr<SrsGbSession> session(new SrsGbSession());
    session->assemble();
    MockContextIdSetterForRtcTcpConn owner_cid;
    session->setup_owner(NULL, NULL, &owner_cid);

    HELPER_EXPECT_SUCCESS(session->cycle());
    EXPECT_TRUE(owner_cid.set_cid_called_);
    EXPECT_TRUE(session->is_ended());
}

// A session that ends for a closed client, a closed server or a success with a message ends with no
// error.
VOID TEST(GbSessionCycleTest, CycleSwallowsCloseErrors)
{
    srs_error_t err;

    int codes[] = {ERROR_SOCKET_READ, ERROR_HTTP_STREAM_EOF, ERROR_SUCCESS};
    for (int i = 0; i < 3; i++) {
        SrsUniquePtr<SrsGbSession> session(new SrsGbSession());
        session->assemble();
        MockInterruptableForRtcTcpConn owner;
        owner.pull_error_ = srs_error_new(codes[i], "mock close");
        MockContextIdSetterForRtcTcpConn owner_cid;
        session->setup_owner(NULL, &owner, &owner_cid);

        HELPER_EXPECT_SUCCESS(session->cycle());
        EXPECT_TRUE(session->is_ended()) << i;
    }
}

// The session drives its state at a fixed interval, until the publisher's media does not come in time.
VOID TEST(GbSessionCycleTest, DoCycleDrivesStateAtInterval)
{
    srs_error_t err;

    // One drive, then the coroutine stops.
    if (true) {
        SrsUniquePtr<SrsGbSession> session(new SrsGbSession());
        session->assemble();
        MockCoroutineForForwarder owner(1);
        session->setup_owner(NULL, &owner, &owner);
        // The media connected, so no wait for it times out.
        session->connecting_starttime_ = 0;

        srs_utime_t starttime = srs_time_now_realtime();
        err = session->do_cycle();
        EXPECT_EQ(ERROR_THREAD_DUMMY, srs_error_code(err));
        srs_freep(err);
        EXPECT_GE(srs_time_now_realtime() - starttime, 250 * SRS_UTIME_MILLISECONDS);
        EXPECT_EQ(2, owner.pull_count_);
        EXPECT_EQ(SrsGbSessionStateInit, session->state_);
    }

    // The media connection times out at the first drive, which ends the session with no error.
    if (true) {
        SrsUniquePtr<SrsGbSession> session(new SrsGbSession());
        session->assemble();
        MockCoroutineForForwarder owner(100);
        session->setup_owner(NULL, &owner, &owner);
        session->connecting_starttime_ = srs_time_now_realtime() - 10 * SRS_UTIME_SECONDS;
        session->media_connect_timeout_ = 1 * SRS_UTIME_SECONDS;

        HELPER_EXPECT_SUCCESS(session->cycle());
        EXPECT_EQ(1, owner.pull_count_);
        EXPECT_TRUE(session->is_ended());
    }
}

// An RTP packet of RFC4571 framing: the 2 bytes of length, then the bytes.
static void mock_gb_append_framed(MockBufferIO *io, const std::string &packet)
{
    uint8_t length[2];
    length[0] = (uint8_t)(packet.size() >> 8);
    length[1] = (uint8_t)(packet.size() & 0xff);
    io->append(length, 2);
    io->append((uint8_t *)packet.data(), (int)packet.size());
}

// The media connection of a test, reading from the io.
class MockGbMediaCycleFixture
{
public:
    MockResourceManagerForBindSession manager_;
    MockCoroutineForForwarder owner_;
    MockBufferIO *io_;
    SrsGbMediaTcpConn *conn_;
    SrsSharedResource<ISrsGbMediaTcpConn> *wrapper_;

public:
    MockGbMediaCycleFixture() : owner_(100)
    {
        conn_ = new SrsGbMediaTcpConn();
        conn_->assemble();
        wrapper_ = new SrsSharedResource<ISrsGbMediaTcpConn>(conn_);
        conn_->gb_manager_ = &manager_;
        conn_->wrapper_ = wrapper_;
        conn_->owner_coroutine_ = &owner_;
        io_ = new MockBufferIO();
        conn_->conn_ = io_;
    }
    virtual ~MockGbMediaCycleFixture()
    {
        conn_->owner_coroutine_ = NULL;
        conn_->gb_manager_ = NULL;
        srs_freep(wrapper_);
    }
};

// The media connection drops RTCP, packets that are no RTP and packets of no session, and ends with
// no error when the device closes it.
VOID TEST(GbMediaCycleTest, CycleDropsUnusablePackets)
{
    srs_error_t err;

    MockGbMediaCycleFixture f;

    // An RTCP sender report.
    std::string rtcp("\x80\xc8\x00\x06\x00\x00\x04\xd2\x00\x00\x00\x00", 12);
    mock_gb_append_framed(f.io_, rtcp);
    // A large RTCP packet, above the size of an MTU.
    std::string large = rtcp + std::string(1600 - rtcp.size(), '\x00');
    mock_gb_append_framed(f.io_, large);
    // Bytes that are no RTP packet.
    mock_gb_append_framed(f.io_, std::string("\x80\x60", 2));
    // An RTP packet of an SSRC that no session reserved.
    mock_gb_append_framed(f.io_, std::string("\x80\x60\x00\x01\x00\x00\x00\x01\x00\x00\x04\xd2\x00", 13));

    HELPER_EXPECT_SUCCESS(f.conn_->cycle());
    EXPECT_EQ(2u, f.conn_->nn_rtcp_);
    EXPECT_TRUE(f.conn_->session_ == NULL);
    EXPECT_TRUE(f.conn_->conn_ == NULL);
    EXPECT_FALSE(f.conn_->is_connected());
    EXPECT_EQ(5, f.owner_.pull_count_);
}

// The media connection ends, with no error, for a frame of no length, a stopped owner, a server
// that closes, or an end with a message; it tells its session that the media is gone.
VOID TEST(GbMediaCycleTest, CycleEndsAndLeavesSession)
{
    srs_error_t err;

    // A frame of no length.
    if (true) {
        MockGbMediaCycleFixture f;
        f.io_->append(std::string("\x00\x00", 2));
        err = f.conn_->do_cycle();
        EXPECT_EQ(ERROR_GB_PS_MEDIA, srs_error_code(err));
        srs_freep(err);
    }

    // No owner.
    if (true) {
        MockGbMediaCycleFixture f;
        f.conn_->owner_coroutine_ = NULL;
        HELPER_EXPECT_SUCCESS(f.conn_->cycle());
    }

    // The server closes, while the connection serves a session.
    if (true) {
        MockGbMediaCycleFixture f;
        f.io_->in_err = srs_error_new(ERROR_HTTP_STREAM_EOF, "mock eof");
        MockGbSessionForMediaConn session;
        f.conn_->session_ = &session;
        HELPER_EXPECT_SUCCESS(f.conn_->cycle());
        EXPECT_TRUE(session.on_media_disconnected_called_);
        EXPECT_TRUE(f.conn_->session_ == NULL);
    }

    // An end with a message, and a stopped coroutine.
    if (true) {
        MockGbMediaCycleFixture f;
        MockInterruptableForRtcTcpConn owner;
        owner.pull_error_ = srs_error_new(ERROR_SUCCESS, "mock done");
        f.conn_->owner_coroutine_ = &owner;
        HELPER_EXPECT_SUCCESS(f.conn_->cycle());
        f.conn_->owner_coroutine_ = NULL;
    }
}

// The listener serves only the media listener, and closes any other TCP client.
VOID TEST(GbMediaCycleTest, ListenerIgnoresOtherTcpClient)
{
    srs_error_t err;

    SrsUniquePtr<SrsGbListener> listener(new SrsGbListener(NULL));
    MockIpListener other;
    HELPER_EXPECT_SUCCESS(listener->on_tcp_client(&other, NULL));
}

// The PS message of the video codec, for SrsGbMuxer::on_ts_video.
class MockGbVideoFixture
{
public:
    SrsPsContext ctx_;
    SrsPsPacket ps_;
    SrsPsDecodeHelper helper_;
    SrsTsMessage msg_;
    MockGbSessionForMuxer session_;
    SrsGbMuxer *muxer_;
    MockGbMpegpsQueue *queue_;

public:
    MockGbVideoFixture(SrsTsStream codec) : ps_(&ctx_)
    {
        ctx_.video_stream_type_ = codec;
        helper_.ctx_ = &ctx_;
        helper_.ps_ = &ps_;
        msg_.ps_helper_ = &helper_;
        msg_.dts_ = msg_.pts_ = 90000;

        muxer_ = new SrsGbMuxer(&session_);
        muxer_->setup("rtmp://127.0.0.1/live/[stream]");
        queue_ = new MockGbMpegpsQueue();
        srs_freep(muxer_->queue_);
        muxer_->queue_ = queue_;
        muxer_->sdk_ = new MockRtmpClient();
    }
    virtual ~MockGbVideoFixture()
    {
        srs_freep(muxer_);
    }
    srs_error_t on_ts_video(const std::string &frame)
    {
        SrsBuffer avs((char *)frame.data(), (int)frame.size());
        return muxer_->on_ts_video(&msg_, &avs);
    }
};

static const char gb_h264_sps[] = {0x67, 0x42, 0x00, 0x1f, (char)0xac, (char)0xd9};
static const char gb_h264_pps[] = {0x68, (char)0xeb, (char)0xe3, (char)0xcb};
static const char gb_h264_idr[] = {0x65, (char)0x88, (char)0x84, 0x01};
static const char gb_h265_aud[] = {0x46, 0x01, 0x50};
static const char gb_h265_vps[] = {0x40, 0x01, 0x0C, 0x01, (char)0xFF, (char)0xFF, 0x01, 0x60, 0x00, 0x00, 0x03, 0x00, (char)0x90, 0x00,
                                   0x00, 0x03, 0x00, 0x00, 0x03, 0x00, 0x5D, (char)0xAC, 0x09};
static const char gb_h265_sps[] = {0x42, 0x01, 0x01, 0x01, 0x60, 0x00, 0x00, 0x03, 0x00, (char)0x90, 0x00, 0x00, 0x03, 0x00, 0x00, 0x03, 0x00,
                                   0x5D, (char)0xA0, 0x02, (char)0x80, (char)0x80, 0x2D, 0x16, 0x59, 0x59, (char)0xA4, (char)0x93, 0x2B,
                                   (char)0xC0, 0x5A, 0x70, (char)0x80};
static const char gb_h265_pps[] = {0x44, 0x01, (char)0xC1, 0x73, (char)0xD1, (char)0x89, 0x00};
static const char gb_h265_idr[] = {0x26, 0x01, (char)0xaf, 0x06};

// The video of a PS pack is muxed by its codec, H.264 or HEVC; any other codec is refused.
VOID TEST(GbMuxerVideoTest, OnTsVideoDispatchesByCodec)
{
    srs_error_t err;

    if (true) {
        MockGbVideoFixture f(SrsTsStreamVideoH264);
        std::string frame;
        mock_gb_append_annexb(frame, gb_h264_sps, sizeof(gb_h264_sps));
        mock_gb_append_annexb(frame, gb_h264_pps, sizeof(gb_h264_pps));
        mock_gb_append_annexb(frame, gb_h264_idr, sizeof(gb_h264_idr));
        HELPER_EXPECT_SUCCESS(f.on_ts_video(frame));
        ASSERT_EQ(2, (int)f.queue_->payloads_.size());
        EXPECT_EQ(0x17, (uint8_t)f.queue_->payloads_[0][0]);
        EXPECT_EQ(SrsVideoAvcFrameTraitSequenceHeader, f.queue_->payloads_[0][1]);
    }

    if (true) {
        MockGbVideoFixture f(SrsTsStreamVideoHEVC);
        std::string frame;
        mock_gb_append_annexb(frame, gb_h265_vps, sizeof(gb_h265_vps));
        mock_gb_append_annexb(frame, gb_h265_sps, sizeof(gb_h265_sps));
        mock_gb_append_annexb(frame, gb_h265_pps, sizeof(gb_h265_pps));
        mock_gb_append_annexb(frame, gb_h265_idr, sizeof(gb_h265_idr));
        HELPER_EXPECT_SUCCESS(f.on_ts_video(frame));
        EXPECT_EQ(2, (int)f.queue_->payloads_.size());
    }

    if (true) {
        MockGbVideoFixture f(SrsTsStreamVideoMpeg4);
        std::string frame;
        mock_gb_append_annexb(frame, gb_h264_idr, sizeof(gb_h264_idr));
        err = f.on_ts_video(frame);
        EXPECT_EQ(ERROR_STREAM_CASTER_TS_CODEC, srs_error_code(err));
        srs_freep(err);
        EXPECT_EQ(0, (int)f.queue_->payloads_.size());
    }

    // The session ended: no connect, no media.
    if (true) {
        MockGbVideoFixture f(SrsTsStreamVideoH264);
        srs_freep(f.muxer_->sdk_);
        f.session_.ended_ = true;
        std::string frame;
        mock_gb_append_annexb(frame, gb_h264_idr, sizeof(gb_h264_idr));
        err = f.on_ts_video(frame);
        EXPECT_EQ(ERROR_GB_SESSION_ENDED, srs_error_code(err));
        srs_freep(err);
    }
}

// The H.264 of a pack: bytes of no start code fail, a NALU of an unknown type ends the pack, a known
// SPS or PPS is not sent again, and a frame before them is dropped.
VOID TEST(GbMuxerVideoTest, MuxH264DropsAndSkips)
{
    srs_error_t err;

    MockGbVideoFixture f(SrsTsStreamVideoH264);

    err = f.on_ts_video(std::string("\x65\x88\x84\x01", 4));
    HELPER_EXPECT_FAILED(err);

    // A filler NALU, of type 12, ends the pack.
    std::string filler;
    mock_gb_append_annexb(filler, "\x0c\xff\xff", 3);
    mock_gb_append_annexb(filler, gb_h264_idr, sizeof(gb_h264_idr));
    HELPER_EXPECT_SUCCESS(f.on_ts_video(filler));
    EXPECT_EQ(0, (int)f.queue_->payloads_.size());

    // A frame before the SPS and PPS.
    std::string idr;
    mock_gb_append_annexb(idr, gb_h264_idr, sizeof(gb_h264_idr));
    err = f.on_ts_video(idr);
    EXPECT_EQ(ERROR_H264_DROP_BEFORE_SPS_PPS, srs_error_code(err));
    srs_freep(err);

    // The SPS and PPS go once.
    std::string sh;
    mock_gb_append_annexb(sh, gb_h264_sps, sizeof(gb_h264_sps));
    mock_gb_append_annexb(sh, gb_h264_pps, sizeof(gb_h264_pps));
    HELPER_EXPECT_SUCCESS(f.on_ts_video(sh));
    HELPER_EXPECT_SUCCESS(f.on_ts_video(sh));
    EXPECT_EQ(1, (int)f.queue_->payloads_.size());
}

// The H.264 parameter sets that fail to demux or to queue fail the pack.
VOID TEST(GbMuxerVideoTest, MuxH264Failures)
{
    srs_error_t err;

    // The sequence header fails to queue, at its SPS or at its PPS.
    for (int i = 0; i < 2; i++) {
        MockGbVideoFixture f(SrsTsStreamVideoH264);
        f.queue_->push_error_ = srs_error_new(ERROR_SOCKET_WRITE, "mock push");
        std::string frame;
        if (i == 0) {
            f.muxer_->h264_pps_ = std::string(gb_h264_pps, sizeof(gb_h264_pps));
            f.muxer_->h264_pps_changed_ = true;
        } else {
            mock_gb_append_annexb(frame, gb_h264_sps, sizeof(gb_h264_sps));
        }
        mock_gb_append_annexb(frame, i == 0 ? gb_h264_sps : gb_h264_pps, i == 0 ? sizeof(gb_h264_sps) : sizeof(gb_h264_pps));
        err = f.on_ts_video(frame);
        EXPECT_EQ(ERROR_SOCKET_WRITE, srs_error_code(err)) << i;
        srs_freep(err);
    }

    // The demuxer fails on the SPS, the PPS, and the frame.
    for (int i = 0; i < 3; i++) {
        MockGbVideoFixture f(SrsTsStreamVideoH264);
        MockMpegtsRawH264Stream *avc = new MockMpegtsRawH264Stream();
        char nalu[2] = {(char)(i == 0 ? 0x67 : (i == 1 ? 0x68 : 0x65)), 0x01};
        avc->demux_frame_ = nalu;
        avc->demux_frame_size_ = 2;
        avc->is_sps_result_ = (i == 0);
        avc->is_pps_result_ = (i == 1);
        if (i == 0) {
            avc->sps_demux_error_ = srs_error_new(ERROR_STREAM_CASTER_AVC_SPS, "mock sps");
        } else if (i == 1) {
            avc->pps_demux_error_ = srs_error_new(ERROR_STREAM_CASTER_AVC_PPS, "mock pps");
        } else {
            avc->mux_ipb_frame_error_ = srs_error_new(ERROR_H264_API_NO_PREFIXED, "mock ipb");
        }
        srs_freep(f.muxer_->avc_);
        f.muxer_->avc_ = avc;

        err = f.on_ts_video(std::string(nalu, 2));
        HELPER_EXPECT_FAILED(err);
        EXPECT_EQ(0, (int)f.queue_->payloads_.size()) << i;
    }
}

// The HEVC of a pack: an AUD alone is nothing to send, a known VPS, SPS or PPS is not sent again,
// a frame before them is dropped, and bytes of no start code fail.
VOID TEST(GbMuxerVideoTest, MuxH265DropsAndSkips)
{
    srs_error_t err;

    MockGbVideoFixture f(SrsTsStreamVideoHEVC);

    err = f.on_ts_video(std::string(gb_h265_idr, sizeof(gb_h265_idr)));
    HELPER_EXPECT_FAILED(err);

    std::string aud;
    mock_gb_append_annexb(aud, gb_h265_aud, sizeof(gb_h265_aud));
    HELPER_EXPECT_SUCCESS(f.on_ts_video(aud));
    EXPECT_EQ(0, (int)f.queue_->payloads_.size());

    std::string idr;
    mock_gb_append_annexb(idr, gb_h265_idr, sizeof(gb_h265_idr));
    err = f.on_ts_video(idr);
    EXPECT_EQ(ERROR_H264_DROP_BEFORE_SPS_PPS, srs_error_code(err));
    srs_freep(err);

    std::string sh;
    mock_gb_append_annexb(sh, gb_h265_vps, sizeof(gb_h265_vps));
    mock_gb_append_annexb(sh, gb_h265_sps, sizeof(gb_h265_sps));
    mock_gb_append_annexb(sh, gb_h265_pps, sizeof(gb_h265_pps));
    HELPER_EXPECT_SUCCESS(f.on_ts_video(sh));
    HELPER_EXPECT_SUCCESS(f.on_ts_video(sh));
    EXPECT_EQ(1, (int)f.queue_->payloads_.size());

    HELPER_EXPECT_SUCCESS(f.on_ts_video(idr));
    EXPECT_EQ(2, (int)f.queue_->payloads_.size());
}

// The HEVC sequence header that fails to queue fails the pack, at its VPS, SPS or PPS.
VOID TEST(GbMuxerVideoTest, MuxH265QueueFailures)
{
    srs_error_t err;

    const char *nalus[] = {gb_h265_vps, gb_h265_sps, gb_h265_pps};
    int sizes[] = {(int)sizeof(gb_h265_vps), (int)sizeof(gb_h265_sps), (int)sizeof(gb_h265_pps)};
    for (int i = 0; i < 3; i++) {
        MockGbVideoFixture f(SrsTsStreamVideoHEVC);
        // The other parameter sets are known already.
        for (int j = 0; j < 3; j++) {
            std::string v(nalus[j], sizes[j]);
            if (j == 0 && i != 0) {
                f.muxer_->h265_vps_ = v;
            } else if (j == 1 && i != 1) {
                f.muxer_->h265_sps_ = v;
            } else if (j == 2 && i != 2) {
                f.muxer_->h265_pps_ = v;
            }
        }
        f.queue_->push_error_ = srs_error_new(ERROR_SOCKET_WRITE, "mock push");

        std::string frame;
        mock_gb_append_annexb(frame, nalus[i], sizes[i]);
        err = f.on_ts_video(frame);
        EXPECT_EQ(ERROR_SOCKET_WRITE, srs_error_code(err)) << i;
        srs_freep(err);
    }

    // The frame fails to queue.
    if (true) {
        MockGbVideoFixture f(SrsTsStreamVideoHEVC);
        std::string sh;
        mock_gb_append_annexb(sh, gb_h265_vps, sizeof(gb_h265_vps));
        mock_gb_append_annexb(sh, gb_h265_sps, sizeof(gb_h265_sps));
        mock_gb_append_annexb(sh, gb_h265_pps, sizeof(gb_h265_pps));
        HELPER_EXPECT_SUCCESS(f.on_ts_video(sh));

        f.queue_->push_error_ = srs_error_new(ERROR_SOCKET_WRITE, "mock push");
        std::string idr;
        mock_gb_append_annexb(idr, gb_h265_idr, sizeof(gb_h265_idr));
        err = f.on_ts_video(idr);
        EXPECT_EQ(ERROR_SOCKET_WRITE, srs_error_code(err));
        srs_freep(err);
    }
}

// An RTMP server that accepts the connection but refuses the publish leaves the muxer unconnected.
VOID TEST(GbMuxerVideoTest, ConnectFailsWhenPublishRefused)
{
    MockAppFactoryForGbMuxer factory;
    SrsUniquePtr<MockGbSessionForMuxer> session(new MockGbSessionForMuxer());
    SrsUniquePtr<SrsGbMuxer> muxer(new SrsGbMuxer(session.get()));
    muxer->setup("rtmp://127.0.0.1/live/[stream]");
    muxer->app_factory_ = &factory;

    MockRtmpClient *client = new MockRtmpClient();
    client->publish_error_ = srs_error_new(ERROR_SYSTEM_STREAM_BUSY, "mock busy");
    factory.clients_.push_back(client);

    srs_error_t err = muxer->connect();
    EXPECT_EQ(ERROR_SYSTEM_STREAM_BUSY, srs_error_code(err));
    srs_freep(err);
    EXPECT_TRUE(muxer->sdk_ == NULL);
    EXPECT_FALSE(muxer->connecting_);

    muxer->app_factory_ = NULL;
}

// The publish API answers a refused request with the code of the error, and a good one with code 0.
VOID TEST(GbMuxerVideoTest, GoApiGbPublishAnswersCode)
{
    srs_error_t err;

    for (int i = 0; i < 2; i++) {
        bool good = (i == 1);
        MockResponseWriter writer;
        MockHttpMessageForGbPublish request;
        MockResourceManagerForGbPublish manager;
        MockAppFactoryForGbPublish factory;
        MockAppConfigForGbListener config;
        config.stream_caster_listen_port_ = 9000;

        SrsConfDirective *conf = new SrsConfDirective();
        conf->name_ = "stream_caster";
        SrsUniquePtr<SrsGoApiGbPublish> api(new SrsGoApiGbPublish(conf));
        api->config_ = &config;
        api->gb_manager_ = &manager;
        api->app_factory_ = &factory;

        request.body_content_ = good ? "{\"id\":\"34020000001320000001\",\"ssrc\":\"1234567890\"}" : "{\"id\":\"34020000001320000001\",\"ssrc\":\"camera\"}";
        HELPER_EXPECT_SUCCESS(api->serve_http(&writer, &request));

        std::string body(writer.io.out_buffer.bytes(), writer.io.out_buffer.length());
        if (good) {
            EXPECT_TRUE(body.find("\"code\":0") != std::string::npos) << body;
            EXPECT_TRUE(body.find("\"port\":9000") != std::string::npos) << body;
        } else {
            EXPECT_TRUE(body.find("\"code\":" + srs_strconv_format_int(ERROR_HTTP_DATA_INVALID)) != std::string::npos) << body;
            EXPECT_TRUE(body.find("\"port\"") == std::string::npos) << body;
        }

        api->config_ = NULL;
        api->gb_manager_ = NULL;
        api->app_factory_ = NULL;
        srs_freep(conf);
    }
}
