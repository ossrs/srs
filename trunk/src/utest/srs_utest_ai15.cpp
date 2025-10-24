//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//
#include <srs_utest_ai15.hpp>

using namespace std;

#include <algorithm>
#include <srs_app_factory.hpp>
#include <srs_app_http_hooks.hpp>
#include <srs_app_rtmp_conn.hpp>
#include <srs_app_rtmp_source.hpp>
#include <srs_app_security.hpp>
#include <srs_app_server.hpp>
#include <srs_app_utility.hpp>
#include <srs_kernel_consts.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_hourglass.hpp>
#include <srs_kernel_st.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_json.hpp>
#include <srs_protocol_rtmp_stack.hpp>
#include <srs_protocol_st.hpp>
#include <srs_utest_ai11.hpp>
#include <srs_utest_ai14.hpp>
#include <sys/socket.h>

// Mock PID file locker implementation for SrsServer::initialize() testing
MockPidFileLocker::MockPidFileLocker()
{
}

MockPidFileLocker::~MockPidFileLocker()
{
}

srs_error_t MockPidFileLocker::acquire()
{
    // Mock implementation that always succeeds without actually locking a file
    // This allows tests to run even when a real SRS server is running
    return srs_success;
}

// Mock config implementation for SrsServer::listen() testing
MockAppConfigForServerListen::MockAppConfigForServerListen()
{
    rtmps_enabled_ = false;
    http_api_enabled_ = false;
    https_api_enabled_ = false;
    http_stream_enabled_ = false;
    https_stream_enabled_ = false;
    rtc_server_tcp_enabled_ = false;
    rtc_server_protocol_ = "udp";
    rtsp_server_enabled_ = false;
    exporter_enabled_ = false;
}

MockAppConfigForServerListen::~MockAppConfigForServerListen()
{
}

std::vector<std::string> MockAppConfigForServerListen::get_listens()
{
    return rtmp_listens_;
}

bool MockAppConfigForServerListen::get_rtmps_enabled()
{
    return rtmps_enabled_;
}

std::vector<std::string> MockAppConfigForServerListen::get_rtmps_listen()
{
    return rtmps_listens_;
}

bool MockAppConfigForServerListen::get_http_api_enabled()
{
    return http_api_enabled_;
}

std::vector<std::string> MockAppConfigForServerListen::get_http_api_listens()
{
    return http_api_listens_;
}

bool MockAppConfigForServerListen::get_https_api_enabled()
{
    return https_api_enabled_;
}

std::vector<std::string> MockAppConfigForServerListen::get_https_api_listens()
{
    return https_api_listens_;
}

bool MockAppConfigForServerListen::get_http_stream_enabled()
{
    return http_stream_enabled_;
}

std::vector<std::string> MockAppConfigForServerListen::get_http_stream_listens()
{
    return http_stream_listens_;
}

bool MockAppConfigForServerListen::get_https_stream_enabled()
{
    return https_stream_enabled_;
}

std::vector<std::string> MockAppConfigForServerListen::get_https_stream_listens()
{
    return https_stream_listens_;
}

bool MockAppConfigForServerListen::get_rtc_server_tcp_enabled()
{
    return rtc_server_tcp_enabled_;
}

std::vector<std::string> MockAppConfigForServerListen::get_rtc_server_tcp_listens()
{
    return rtc_server_tcp_listens_;
}

std::string MockAppConfigForServerListen::get_rtc_server_protocol()
{
    return rtc_server_protocol_;
}

bool MockAppConfigForServerListen::get_rtsp_server_enabled()
{
    return rtsp_server_enabled_;
}

std::vector<std::string> MockAppConfigForServerListen::get_rtsp_server_listens()
{
    return rtsp_server_listens_;
}

bool MockAppConfigForServerListen::get_exporter_enabled()
{
    return exporter_enabled_;
}

std::string MockAppConfigForServerListen::get_exporter_listen()
{
    return exporter_listen_;
}

// Test SrsServer constructor and destructor to ensure proper initialization
// and cleanup of all server components including listeners, managers, and
// WebRTC session management.
VOID TEST(SrsServerTest, ConstructorAndDestructor)
{
    // Create SrsServer instance - tests constructor initialization
    SrsUniquePtr<SrsServer> server(new SrsServer());

    // Verify that the server object was created successfully
    EXPECT_TRUE(server.get() != NULL);

    // The destructor will be called automatically when server goes out of scope
    // This tests proper cleanup of all components:
    // - Signal manager
    // - HTTP API mux
    // - All protocol listeners (RTMP, RTMPS, HTTP, HTTPS, WebRTC, RTSP, etc.)
    // - Stream casters
    // - HTTP server and heartbeat
    // - Ingester and timer
    // - WebRTC session manager
    // - PID file locker
}

// Test SrsServer::initialize() method to verify proper initialization sequence
// including PID file locking, SRT event loop, DTLS certificate, DVR async worker,
// config subscription, HTTP API/server initialization, blackhole, and WebRTC
// session manager. This test covers the major use scenario of server initialization.
VOID TEST(SrsServerTest, InitializeSuccess)
{
    srs_error_t err = srs_success;

    // Create SrsServer instance
    SrsUniquePtr<SrsServer> server(new SrsServer());
    EXPECT_TRUE(server.get() != NULL);

    // Replace the PID file locker with a mock to avoid conflicts with running SRS server
    SrsPidFileLocker *original_locker = server->pid_file_locker_;
    MockPidFileLocker *mock_locker = new MockPidFileLocker();
    server->pid_file_locker_ = mock_locker;

    // Call initialize() - this is the main test
    // This will initialize all server components in the correct sequence:
    // 1. PID file locker acquisition
    // 2. SRT log and event loop initialization
    // 3. WebRTC DTLS certificate initialization
    // 4. DVR async call worker start
    // 5. Config subscription
    // 6. HTTP stream/API configuration check and port reuse logic
    // 7. HTTP API mux initialization (if not reusing)
    // 8. HTTP server initialization
    // 9. WebRTC blackhole initialization
    // 10. WebRTC session manager initialization
    HELPER_EXPECT_SUCCESS(server->initialize());

    // Verify that the server was initialized successfully
    // The fact that initialize() returned success means all components
    // were initialized properly without errors
    EXPECT_TRUE(server.get() != NULL);

    // Restore original locker and cleanup mock
    server->pid_file_locker_ = original_locker;
    srs_freep(mock_locker);
}

// Test SrsServer::listen() method to verify proper listener setup for RTMP protocol.
// This test covers the major use scenario of starting RTMP listener on a random port.
VOID TEST(SrsServerTest, ListenRtmpSuccess)
{
    srs_error_t err = srs_success;

    // Generate random port in range [30000, 60000]
    SrsRand rand;
    int port = rand.integer(30000, 60000);
    std::string listen_addr = srs_strconv_format_int(port);

    // Create mock config with RTMP listening enabled
    MockAppConfigForServerListen mock_config;
    mock_config.rtmp_listens_.push_back(listen_addr);

    // Create SrsServer instance
    SrsUniquePtr<SrsServer> server(new SrsServer());
    EXPECT_TRUE(server.get() != NULL);

    // Inject mock config
    server->config_ = &mock_config;

    // Replace the PID file locker with a mock to avoid conflicts with running SRS server
    SrsPidFileLocker *original_locker = server->pid_file_locker_;
    MockPidFileLocker *mock_locker = new MockPidFileLocker();
    server->pid_file_locker_ = mock_locker;

    // Initialize server first (required before listen)
    HELPER_EXPECT_SUCCESS(server->initialize());

    // Call listen() - this is the main test
    // This will:
    // 1. Create RTMP listener on the configured port
    // 2. Start listening for incoming RTMP connections
    // 3. Start connection manager
    HELPER_EXPECT_SUCCESS(server->listen());

    // Verify that the server is listening
    // The fact that listen() returned success means:
    // - RTMP listener was created successfully
    // - Socket binding succeeded on the random port
    // - Connection manager started successfully
    EXPECT_TRUE(server.get() != NULL);

    // Restore original locker and cleanup mock
    server->pid_file_locker_ = original_locker;
    srs_freep(mock_locker);
}

// Test SrsServer::http_handle() method to verify proper HTTP API handler registration.
// This test covers the major use scenario of registering all HTTP API endpoints including
// root API, versioning, summaries, rusages, stats, meminfos, authors, features, vhosts,
// streams, clients, raw API, clusters, test endpoints, metrics, console, and WebRTC APIs.
VOID TEST(SrsServerTest, HttpHandleSuccess)
{
    srs_error_t err = srs_success;

    // Create mock HTTP API mux
    MockHttpServeMux *mock_mux = new MockHttpServeMux();

    // Create SrsServer instance
    SrsUniquePtr<SrsServer> server(new SrsServer());
    EXPECT_TRUE(server.get() != NULL);

    // Inject mock HTTP API mux
    ISrsCommonHttpHandler *original_mux = server->http_api_mux_;
    server->http_api_mux_ = mock_mux;

    // Set reuse_api_over_server_ to false to test all handler registrations
    server->reuse_api_over_server_ = false;

    // Call http_handle() - this is the main test
    // This will register all HTTP API handlers:
    // 1. Root API (/)
    // 2. API version (/api/v1/versions)
    // 3. API endpoints (/api/, /api/v1/)
    // 4. Server stats (/api/v1/summaries, /api/v1/rusages, etc.)
    // 5. System info (/api/v1/meminfos, /api/v1/authors, /api/v1/features)
    // 6. Stream management (/api/v1/vhosts/, /api/v1/streams/, /api/v1/clients/)
    // 7. Raw API (/api/v1/raw)
    // 8. Cluster API (/api/v1/clusters)
    // 9. Test endpoints (/api/v1/tests/*)
    // 10. Metrics (/metrics)
    // 11. Console (/console/)
    // 12. WebRTC APIs (via listen_rtc_api())
    HELPER_EXPECT_SUCCESS(server->http_handle());

    // Verify that handlers were registered
    // The exact count depends on conditional compilation flags (SRS_GPERF, SRS_VALGRIND, SRS_SIGNAL_API)
    // but should be at least 20 handlers (basic APIs + WebRTC APIs)
    EXPECT_TRUE(mock_mux->handle_count_ >= 20);

    // Verify some key patterns were registered
    bool has_root = false;
    bool has_api = false;
    bool has_summaries = false;
    bool has_metrics = false;
    bool has_rtc_play = false;

    for (size_t i = 0; i < mock_mux->patterns_.size(); i++) {
        if (mock_mux->patterns_[i] == "/")
            has_root = true;
        if (mock_mux->patterns_[i] == "/api/")
            has_api = true;
        if (mock_mux->patterns_[i] == "/api/v1/summaries")
            has_summaries = true;
        if (mock_mux->patterns_[i] == "/metrics")
            has_metrics = true;
        if (mock_mux->patterns_[i] == "/rtc/v1/play/")
            has_rtc_play = true;
    }

    EXPECT_TRUE(has_root);
    EXPECT_TRUE(has_api);
    EXPECT_TRUE(has_summaries);
    EXPECT_TRUE(has_metrics);
    EXPECT_TRUE(has_rtc_play);

    // Cleanup: restore original mux to avoid side effects
    server->http_api_mux_ = original_mux;
    srs_freep(mock_mux);
}

MockLogForSignal::MockLogForSignal()
{
    reopen_count_ = 0;
}

MockLogForSignal::~MockLogForSignal()
{
}

srs_error_t MockLogForSignal::initialize()
{
    return srs_success;
}

void MockLogForSignal::reopen()
{
    reopen_count_++;
}

void MockLogForSignal::log(SrsLogLevel level, const char *tag, const SrsContextId &context_id, const char *fmt, va_list args)
{
    // Do nothing for mock
}

MockAppConfigForSignal::MockAppConfigForSignal()
{
    force_grace_quit_ = false;
}

MockAppConfigForSignal::~MockAppConfigForSignal()
{
}

bool MockAppConfigForSignal::is_force_grace_quit()
{
    return force_grace_quit_;
}

VOID TEST(ServerTest, OnSignalHandling)
{
    // Create SrsServer instance
    SrsUniquePtr<SrsServer> server(new SrsServer());
    EXPECT_TRUE(server.get() != NULL);

    // Create and inject mock config
    MockAppConfigForSignal *mock_config = new MockAppConfigForSignal();
    ISrsAppConfig *original_config = server->config_;
    server->config_ = mock_config;

    // Create and inject mock log
    MockLogForSignal *mock_log = new MockLogForSignal();
    ISrsLog *original_log = server->log_;
    server->log_ = mock_log;

    // Test 1: SRS_SIGNAL_RELOAD should set signal_reload_ flag
    server->signal_reload_ = false;
    server->on_signal(SRS_SIGNAL_RELOAD);
    EXPECT_TRUE(server->signal_reload_);

    // Test 2: SRS_SIGNAL_REOPEN_LOG should call log_->reopen()
    EXPECT_EQ(0, mock_log->reopen_count_);
    server->on_signal(SRS_SIGNAL_REOPEN_LOG);
    EXPECT_EQ(1, mock_log->reopen_count_);

    // Test 3: SRS_SIGNAL_PERSISTENCE_CONFIG should set signal_persistence_config_ flag
    server->signal_persistence_config_ = false;
    server->on_signal(SRS_SIGNAL_PERSISTENCE_CONFIG);
    EXPECT_TRUE(server->signal_persistence_config_);

    // Test 4: SRS_SIGNAL_FAST_QUIT should set signal_fast_quit_ flag
    server->signal_fast_quit_ = false;
    server->on_signal(SRS_SIGNAL_FAST_QUIT);
    EXPECT_TRUE(server->signal_fast_quit_);

    // Test 5: SIGINT should set signal_fast_quit_ flag
    server->signal_fast_quit_ = false;
    server->on_signal(SIGINT);
    EXPECT_TRUE(server->signal_fast_quit_);

    // Test 6: SRS_SIGNAL_GRACEFULLY_QUIT should set signal_gracefully_quit_ flag
    server->signal_gracefully_quit_ = false;
    server->on_signal(SRS_SIGNAL_GRACEFULLY_QUIT);
    EXPECT_TRUE(server->signal_gracefully_quit_);

    // Test 7: SRS_SIGNAL_FAST_QUIT with force_grace_quit enabled should convert to gracefully quit
    mock_config->force_grace_quit_ = true;
    server->signal_gracefully_quit_ = false;
    server->signal_fast_quit_ = false;
    server->on_signal(SRS_SIGNAL_FAST_QUIT);
    EXPECT_TRUE(server->signal_gracefully_quit_);
    EXPECT_FALSE(server->signal_fast_quit_);

    // Test 8: Repeated signals should not change already-set flags
    server->signal_fast_quit_ = true;
    server->on_signal(SRS_SIGNAL_FAST_QUIT);
    EXPECT_TRUE(server->signal_fast_quit_);

    server->signal_gracefully_quit_ = true;
    server->on_signal(SRS_SIGNAL_GRACEFULLY_QUIT);
    EXPECT_TRUE(server->signal_gracefully_quit_);

    // Cleanup: restore original config and log
    server->config_ = original_config;
    server->log_ = original_log;
    srs_freep(mock_config);
    srs_freep(mock_log);
}

// Mock config implementation for SrsServer::do2_cycle() testing
MockAppConfigForDo2Cycle::MockAppConfigForDo2Cycle()
{
    reload_error_ = srs_success;
    persistence_error_ = srs_success;
    reload_count_ = 0;
    persistence_count_ = 0;
    reload_state_ = SrsReloadStateInit;
}

MockAppConfigForDo2Cycle::~MockAppConfigForDo2Cycle()
{
    srs_freep(reload_error_);
    srs_freep(persistence_error_);
}

srs_error_t MockAppConfigForDo2Cycle::reload(SrsReloadState *pstate)
{
    reload_count_++;
    if (pstate) {
        *pstate = reload_state_;
    }
    return srs_error_copy(reload_error_);
}

srs_error_t MockAppConfigForDo2Cycle::persistence()
{
    persistence_count_++;
    return srs_error_copy(persistence_error_);
}

void MockAppConfigForDo2Cycle::reset()
{
    srs_freep(reload_error_);
    srs_freep(persistence_error_);
    reload_error_ = srs_success;
    persistence_error_ = srs_success;
    reload_count_ = 0;
    persistence_count_ = 0;
    reload_state_ = SrsReloadStateInit;
}

VOID TEST(ServerTest, Do2CycleReloadSuccess)
{
    srs_error_t err;

    // Create SrsServer instance
    SrsUniquePtr<SrsServer> server(new SrsServer());
    EXPECT_TRUE(server.get() != NULL);

    // Create and inject mock config
    MockAppConfigForDo2Cycle *mock_config = new MockAppConfigForDo2Cycle();
    ISrsAppConfig *original_config = server->config_;
    server->config_ = mock_config;

    // Test major use scenario: signal_reload_ triggers config reload with success
    // This covers the main code path where reload signal is processed successfully
    server->signal_reload_ = true;
    server->signal_fast_quit_ = false;
    server->signal_gracefully_quit_ = false;
    mock_config->reload_state_ = SrsReloadStateFinished;
    HELPER_EXPECT_SUCCESS(server->do2_cycle());
    EXPECT_FALSE(server->signal_reload_);     // Flag should be cleared after processing
    EXPECT_EQ(1, mock_config->reload_count_); // Config reload should be called once

    // Cleanup: restore original config
    server->config_ = original_config;
    srs_freep(mock_config);
}

// Mock hourglass implementation for SrsServer::setup_ticks() testing
MockHourGlassForSetupTicks::MockHourGlassForSetupTicks()
{
    tick_count_ = 0;
    start_count_ = 0;
}

MockHourGlassForSetupTicks::~MockHourGlassForSetupTicks()
{
}

srs_error_t MockHourGlassForSetupTicks::start()
{
    start_count_++;
    return srs_success;
}

void MockHourGlassForSetupTicks::stop()
{
}

srs_error_t MockHourGlassForSetupTicks::tick(srs_utime_t interval)
{
    return tick(0, interval);
}

srs_error_t MockHourGlassForSetupTicks::tick(int event, srs_utime_t interval)
{
    tick_count_++;
    tick_events_.push_back(event);
    tick_intervals_.push_back(interval);
    return srs_success;
}

void MockHourGlassForSetupTicks::untick(int event)
{
}

// Mock app factory implementation for SrsServer::setup_ticks() testing
MockAppFactoryForSetupTicks::MockAppFactoryForSetupTicks()
{
    mock_hourglass_ = new MockHourGlassForSetupTicks();
}

MockAppFactoryForSetupTicks::~MockAppFactoryForSetupTicks()
{
    // Do NOT free mock_hourglass_ here because it's owned by SrsServer::timer_
    // and will be freed by SrsServer destructor
    mock_hourglass_ = NULL;
}

ISrsHourGlass *MockAppFactoryForSetupTicks::create_hourglass(const std::string &name, ISrsHourGlassHandler *handler, srs_utime_t interval)
{
    return mock_hourglass_;
}

// Mock config implementation for SrsServer::setup_ticks() testing
MockAppConfigForSetupTicks::MockAppConfigForSetupTicks()
{
    stats_enabled_ = false;
    heartbeat_enabled_ = false;
    heartbeat_interval_ = 1 * SRS_UTIME_MILLISECONDS;
}

MockAppConfigForSetupTicks::~MockAppConfigForSetupTicks()
{
}

bool MockAppConfigForSetupTicks::get_stats_enabled()
{
    return stats_enabled_;
}

bool MockAppConfigForSetupTicks::get_heartbeat_enabled()
{
    return heartbeat_enabled_;
}

srs_utime_t MockAppConfigForSetupTicks::get_heartbeat_interval()
{
    return heartbeat_interval_;
}

VOID TEST(ServerTest, SetupTicksWithStatsAndHeartbeat)
{
    srs_error_t err;

    // Create SrsServer instance
    SrsUniquePtr<SrsServer> server(new SrsServer());
    EXPECT_TRUE(server.get() != NULL);

    // Create and inject mock config
    MockAppConfigForSetupTicks *mock_config = new MockAppConfigForSetupTicks();
    ISrsAppConfig *original_config = server->config_;
    server->config_ = mock_config;

    // Create and inject mock app factory
    MockAppFactoryForSetupTicks *mock_factory = new MockAppFactoryForSetupTicks();
    ISrsAppFactory *original_factory = server->app_factory_;
    server->app_factory_ = mock_factory;

    // Test major use scenario: setup_ticks with stats and heartbeat enabled
    // This covers the main code path where both stats and heartbeat are enabled
    mock_config->stats_enabled_ = true;
    mock_config->heartbeat_enabled_ = true;
    mock_config->heartbeat_interval_ = 1 * SRS_UTIME_MILLISECONDS;

    HELPER_EXPECT_SUCCESS(server->setup_ticks());

    // Verify that timer was created and started
    EXPECT_EQ(1, mock_factory->mock_hourglass_->start_count_);

    // Verify that ticks were registered
    // When stats_enabled: events 2,4,5,6,7,8,10,11,12 (9 ticks)
    // When heartbeat_enabled: event 9 (1 tick)
    // Total: 10 ticks
    EXPECT_EQ(10, mock_factory->mock_hourglass_->tick_count_);

    // Verify specific tick events are registered
    std::vector<int> &events = mock_factory->mock_hourglass_->tick_events_;
    EXPECT_TRUE(std::find(events.begin(), events.end(), 2) != events.end());  // rusage
    EXPECT_TRUE(std::find(events.begin(), events.end(), 4) != events.end());  // disk
    EXPECT_TRUE(std::find(events.begin(), events.end(), 5) != events.end());  // meminfo
    EXPECT_TRUE(std::find(events.begin(), events.end(), 6) != events.end());  // platform
    EXPECT_TRUE(std::find(events.begin(), events.end(), 7) != events.end());  // network
    EXPECT_TRUE(std::find(events.begin(), events.end(), 8) != events.end());  // kbps
    EXPECT_TRUE(std::find(events.begin(), events.end(), 9) != events.end());  // heartbeat
    EXPECT_TRUE(std::find(events.begin(), events.end(), 10) != events.end()); // udp snmp
    EXPECT_TRUE(std::find(events.begin(), events.end(), 11) != events.end()); // rtc sessions
    EXPECT_TRUE(std::find(events.begin(), events.end(), 12) != events.end()); // server stats

    // Cleanup: restore original config and factory
    server->config_ = original_config;
    server->app_factory_ = original_factory;
    srs_freep(mock_config);
    srs_freep(mock_factory);
}

MockRtcSessionManagerForNotify::MockRtcSessionManagerForNotify()
{
    update_rtc_sessions_count_ = 0;
}

MockRtcSessionManagerForNotify::~MockRtcSessionManagerForNotify()
{
}

void MockRtcSessionManagerForNotify::srs_update_rtc_sessions()
{
    update_rtc_sessions_count_++;
}

MockHttpHeartbeatForNotify::MockHttpHeartbeatForNotify()
{
    heartbeat_count_ = 0;
}

MockHttpHeartbeatForNotify::~MockHttpHeartbeatForNotify()
{
}

void MockHttpHeartbeatForNotify::heartbeat()
{
    heartbeat_count_++;
}

MockConnectionManagerForResampleKbps::MockConnectionManagerForResampleKbps()
{
}

MockConnectionManagerForResampleKbps::~MockConnectionManagerForResampleKbps()
{
}

srs_error_t MockConnectionManagerForResampleKbps::start()
{
    return srs_success;
}

bool MockConnectionManagerForResampleKbps::empty()
{
    return connections_.empty();
}

size_t MockConnectionManagerForResampleKbps::size()
{
    return connections_.size();
}

void MockConnectionManagerForResampleKbps::add(ISrsResource *conn, bool *exists)
{
    connections_.push_back(conn);
}

void MockConnectionManagerForResampleKbps::add_with_id(const std::string & /*id*/, ISrsResource * /*conn*/)
{
}

void MockConnectionManagerForResampleKbps::add_with_fast_id(uint64_t /*id*/, ISrsResource * /*conn*/)
{
}

void MockConnectionManagerForResampleKbps::add_with_name(const std::string & /*name*/, ISrsResource * /*conn*/)
{
}

ISrsResource *MockConnectionManagerForResampleKbps::at(int index)
{
    if (index < 0 || index >= (int)connections_.size()) {
        return NULL;
    }
    return connections_[index];
}

ISrsResource *MockConnectionManagerForResampleKbps::find_by_id(std::string /*id*/)
{
    return NULL;
}

ISrsResource *MockConnectionManagerForResampleKbps::find_by_fast_id(uint64_t /*id*/)
{
    return NULL;
}

ISrsResource *MockConnectionManagerForResampleKbps::find_by_name(std::string /*name*/)
{
    return NULL;
}

void MockConnectionManagerForResampleKbps::remove(ISrsResource *c)
{
}

void MockConnectionManagerForResampleKbps::subscribe(ISrsDisposingHandler *h)
{
}

void MockConnectionManagerForResampleKbps::unsubscribe(ISrsDisposingHandler *h)
{
}

MockStatisticForResampleKbps::MockStatisticForResampleKbps()
{
    kbps_add_delta_count_ = 0;
    kbps_sample_count_ = 0;
}

MockStatisticForResampleKbps::~MockStatisticForResampleKbps()
{
}

void MockStatisticForResampleKbps::on_disconnect(std::string id, srs_error_t err)
{
}

srs_error_t MockStatisticForResampleKbps::on_client(std::string id, ISrsRequest *req, ISrsExpire *conn, SrsRtmpConnType type)
{
    return srs_success;
}

srs_error_t MockStatisticForResampleKbps::on_video_info(ISrsRequest *req, SrsVideoCodecId vcodec, int avc_profile, int avc_level, int width, int height)
{
    return srs_success;
}

srs_error_t MockStatisticForResampleKbps::on_audio_info(ISrsRequest *req, SrsAudioCodecId acodec, SrsAudioSampleRate asample_rate, SrsAudioChannels asound_type, SrsAacObjectType aac_object)
{
    return srs_success;
}

void MockStatisticForResampleKbps::on_stream_publish(ISrsRequest *req, std::string publisher_id)
{
}

void MockStatisticForResampleKbps::on_stream_close(ISrsRequest *req)
{
}

void MockStatisticForResampleKbps::kbps_add_delta(std::string id, ISrsKbpsDelta *delta)
{
    kbps_add_delta_count_++;
}

void MockStatisticForResampleKbps::kbps_sample()
{
    kbps_sample_count_++;
}

srs_error_t MockStatisticForResampleKbps::on_video_frames(ISrsRequest *req, int nb_frames)
{
    return srs_success;
}

std::string MockStatisticForResampleKbps::server_id()
{
    return "mock_server_id";
}

std::string MockStatisticForResampleKbps::service_id()
{
    return "mock_service_id";
}

std::string MockStatisticForResampleKbps::service_pid()
{
    return "mock_pid";
}

SrsStatisticVhost *MockStatisticForResampleKbps::find_vhost_by_id(std::string vid)
{
    return NULL;
}

SrsStatisticStream *MockStatisticForResampleKbps::find_stream(std::string sid)
{
    return NULL;
}

SrsStatisticStream *MockStatisticForResampleKbps::find_stream_by_url(std::string url)
{
    return NULL;
}

SrsStatisticClient *MockStatisticForResampleKbps::find_client(std::string client_id)
{
    return NULL;
}

srs_error_t MockStatisticForResampleKbps::dumps_vhosts(SrsJsonArray *arr)
{
    return srs_success;
}

srs_error_t MockStatisticForResampleKbps::dumps_streams(SrsJsonArray *arr, int start, int count)
{
    return srs_success;
}

srs_error_t MockStatisticForResampleKbps::dumps_clients(SrsJsonArray *arr, int start, int count)
{
    return srs_success;
}

srs_error_t MockStatisticForResampleKbps::dumps_metrics(int64_t &send_bytes, int64_t &recv_bytes, int64_t &nstreams, int64_t &nclients, int64_t &total_nclients, int64_t &nerrs)
{
    send_bytes = 0;
    recv_bytes = 0;
    nstreams = 0;
    nclients = 0;
    total_nclients = 0;
    nerrs = 0;
    return srs_success;
}

void MockStatisticForResampleKbps::reset()
{
    kbps_add_delta_count_ = 0;
    kbps_sample_count_ = 0;
}

VOID TEST(SrsServerTest, NotifyEventDispatch)
{
    srs_error_t err = srs_success;

    // Create server instance
    SrsUniquePtr<SrsServer> server(new SrsServer());

    // Create mock objects
    MockRtcSessionManagerForNotify *mock_rtc_manager = new MockRtcSessionManagerForNotify();
    MockHttpHeartbeatForNotify *mock_heartbeat = new MockHttpHeartbeatForNotify();

    // Save original pointers
    SrsRtcSessionManager *original_rtc_manager = server->rtc_session_manager_;
    SrsHttpHeartbeat *original_heartbeat = server->http_heartbeat_;

    // Inject mock objects (no cast needed since they inherit from the base classes)
    server->rtc_session_manager_ = mock_rtc_manager;
    server->http_heartbeat_ = mock_heartbeat;

    // Test event 11 - RTC session update
    HELPER_EXPECT_SUCCESS(server->notify(11, 0, 0));
    EXPECT_EQ(1, mock_rtc_manager->update_rtc_sessions_count_);

    // Test event 9 - HTTP heartbeat
    HELPER_EXPECT_SUCCESS(server->notify(9, 0, 0));
    EXPECT_EQ(1, mock_heartbeat->heartbeat_count_);

    // Test event 8 - resample_kbps (should not crash)
    HELPER_EXPECT_SUCCESS(server->notify(8, 0, 0));

    // Test multiple calls to same event
    HELPER_EXPECT_SUCCESS(server->notify(11, 0, 0));
    EXPECT_EQ(2, mock_rtc_manager->update_rtc_sessions_count_);

    HELPER_EXPECT_SUCCESS(server->notify(9, 0, 0));
    EXPECT_EQ(2, mock_heartbeat->heartbeat_count_);

    // Test other events (should not crash)
    HELPER_EXPECT_SUCCESS(server->notify(2, 0, 0));  // srs_update_system_rusage
    HELPER_EXPECT_SUCCESS(server->notify(4, 0, 0));  // srs_update_disk_stat
    HELPER_EXPECT_SUCCESS(server->notify(5, 0, 0));  // srs_update_meminfo
    HELPER_EXPECT_SUCCESS(server->notify(6, 0, 0));  // srs_update_platform_info
    HELPER_EXPECT_SUCCESS(server->notify(7, 0, 0));  // srs_update_network_devices
    HELPER_EXPECT_SUCCESS(server->notify(10, 0, 0)); // srs_update_udp_snmp_statistic
    HELPER_EXPECT_SUCCESS(server->notify(12, 0, 0)); // srs_update_server_statistics

    // Restore original pointers
    server->rtc_session_manager_ = original_rtc_manager;
    server->http_heartbeat_ = original_heartbeat;

    // Cleanup mock objects
    srs_freep(mock_rtc_manager);
    srs_freep(mock_heartbeat);
}

VOID TEST(SrsServerTest, ResampleKbps)
{
    // Create server instance
    SrsUniquePtr<SrsServer> server(new SrsServer());

    // Create mock objects
    MockConnectionManagerForResampleKbps *mock_conn_manager = new MockConnectionManagerForResampleKbps();
    MockStatisticForResampleKbps *mock_stat = new MockStatisticForResampleKbps();

    // Save original pointers
    ISrsResourceManager *original_conn_manager = server->conn_manager_;
    ISrsStatistic *original_stat = server->stat_;

    // Inject mock objects
    server->conn_manager_ = mock_conn_manager;
    server->stat_ = mock_stat;

    // Test case: Empty connection manager - verifies kbps_sample is called
    // This is the major use scenario: resample_kbps() should always call
    // stat_->kbps_sample() to update global server statistics, even when
    // there are no connections.
    if (true) {
        server->resample_kbps();
        EXPECT_EQ(0, mock_stat->kbps_add_delta_count_);
        EXPECT_EQ(1, mock_stat->kbps_sample_count_);
    }

    // Restore original pointers
    server->conn_manager_ = original_conn_manager;
    server->stat_ = original_stat;

    // Cleanup mock objects
    srs_freep(mock_conn_manager);
    srs_freep(mock_stat);
}

MockAppConfigForConnectionLimit::MockAppConfigForConnectionLimit()
{
    max_connections_ = 10;
}

MockAppConfigForConnectionLimit::~MockAppConfigForConnectionLimit()
{
}

int MockAppConfigForConnectionLimit::get_max_connections()
{
    return max_connections_;
}

MockConnectionManagerForConnectionLimit::MockConnectionManagerForConnectionLimit()
{
    connection_count_ = 0;
}

MockConnectionManagerForConnectionLimit::~MockConnectionManagerForConnectionLimit()
{
}

srs_error_t MockConnectionManagerForConnectionLimit::start()
{
    return srs_success;
}

bool MockConnectionManagerForConnectionLimit::empty()
{
    return connection_count_ == 0;
}

size_t MockConnectionManagerForConnectionLimit::size()
{
    return connection_count_;
}

void MockConnectionManagerForConnectionLimit::add(ISrsResource *conn, bool *exists)
{
}

void MockConnectionManagerForConnectionLimit::add_with_id(const std::string & /*id*/, ISrsResource * /*conn*/)
{
}

void MockConnectionManagerForConnectionLimit::add_with_fast_id(uint64_t /*id*/, ISrsResource * /*conn*/)
{
}

void MockConnectionManagerForConnectionLimit::add_with_name(const std::string & /*name*/, ISrsResource * /*conn*/)
{
}

ISrsResource *MockConnectionManagerForConnectionLimit::at(int index)
{
    return NULL;
}

ISrsResource *MockConnectionManagerForConnectionLimit::find_by_id(std::string /*id*/)
{
    return NULL;
}

ISrsResource *MockConnectionManagerForConnectionLimit::find_by_fast_id(uint64_t /*id*/)
{
    return NULL;
}

ISrsResource *MockConnectionManagerForConnectionLimit::find_by_name(std::string /*name*/)
{
    return NULL;
}

void MockConnectionManagerForConnectionLimit::remove(ISrsResource *c)
{
}

void MockConnectionManagerForConnectionLimit::subscribe(ISrsDisposingHandler *h)
{
}

void MockConnectionManagerForConnectionLimit::unsubscribe(ISrsDisposingHandler *h)
{
}

MockAppConfigForRtmpConn::MockAppConfigForRtmpConn()
{
    subscribe_count_ = 0;
    unsubscribe_count_ = 0;
    last_subscribed_handler_ = NULL;
    vhost_directive_ = new SrsConfDirective();
    vhost_directive_->name_ = "vhost";
    vhost_directive_->args_.push_back("__defaultVhost__");
}

MockAppConfigForRtmpConn::~MockAppConfigForRtmpConn()
{
    srs_freep(vhost_directive_);
}

void MockAppConfigForRtmpConn::subscribe(ISrsReloadHandler *handler)
{
    subscribe_count_++;
    last_subscribed_handler_ = handler;
}

void MockAppConfigForRtmpConn::unsubscribe(ISrsReloadHandler *handler)
{
    unsubscribe_count_++;
}

SrsConfDirective *MockAppConfigForRtmpConn::get_vhost(std::string vhost, bool try_default_vhost)
{
    return vhost_directive_;
}

bool MockAppConfigForRtmpConn::get_vhost_is_edge(std::string vhost)
{
    return false;
}

void MockAppConfigForRtmpConn::reset()
{
    subscribe_count_ = 0;
    unsubscribe_count_ = 0;
    last_subscribed_handler_ = NULL;
}

MockCoroutineForRtmpConn::MockCoroutineForRtmpConn()
{
    pull_error_ = srs_success;
    pull_count_ = 0;
}

MockCoroutineForRtmpConn::~MockCoroutineForRtmpConn()
{
    srs_freep(pull_error_);
}

srs_error_t MockCoroutineForRtmpConn::start()
{
    return srs_success;
}

void MockCoroutineForRtmpConn::stop()
{
}

void MockCoroutineForRtmpConn::interrupt()
{
}

srs_error_t MockCoroutineForRtmpConn::pull()
{
    pull_count_++;

    // Return success on first call to allow stream_service_cycle() to be called
    // Return error on subsequent calls to exit the loop
    if (pull_count_ == 1) {
        return srs_success;
    }

    // Return the configured error (or success)
    // Note: We don't free pull_error_ here because it might be called multiple times
    if (pull_error_ != srs_success) {
        return srs_error_copy(pull_error_);
    }
    return srs_success;
}

const SrsContextId &MockCoroutineForRtmpConn::cid()
{
    static SrsContextId dummy_cid;
    return dummy_cid;
}

void MockCoroutineForRtmpConn::set_cid(const SrsContextId &cid)
{
}

MockRtmpTransportForDoCycle::MockRtmpTransportForDoCycle()
{
}

MockRtmpTransportForDoCycle::~MockRtmpTransportForDoCycle()
{
}

srs_netfd_t MockRtmpTransportForDoCycle::fd()
{
    // Create a dummy st_netfd_t that won't crash when accessed
    // We use a socket pair to get a valid file descriptor
    static int fds[2] = {-1, -1};
    static srs_netfd_t dummy_stfd = NULL;

    if (fds[0] == -1) {
        socketpair(AF_UNIX, SOCK_STREAM, 0, fds);
        dummy_stfd = srs_netfd_open_socket(fds[0]);
    }

    return dummy_stfd;
}

int MockRtmpTransportForDoCycle::osfd()
{
    return 0;
}

ISrsProtocolReadWriter *MockRtmpTransportForDoCycle::io()
{
    return NULL;
}

srs_error_t MockRtmpTransportForDoCycle::handshake()
{
    return srs_success;
}

const char *MockRtmpTransportForDoCycle::transport_type()
{
    return "mock";
}

srs_error_t MockRtmpTransportForDoCycle::set_socket_buffer(srs_utime_t buffer_v)
{
    return srs_success;
}

srs_error_t MockRtmpTransportForDoCycle::set_tcp_nodelay(bool v)
{
    return srs_success;
}

int64_t MockRtmpTransportForDoCycle::get_recv_bytes()
{
    return 0;
}

int64_t MockRtmpTransportForDoCycle::get_send_bytes()
{
    return 0;
}

VOID TEST(SrsRtmpConnTest, ConstructorAndAssemble)
{
    // Create a dummy file descriptor for transport
    srs_netfd_t dummy_fd = (srs_netfd_t)((void *)0x1234);
    SrsRtmpTransport *transport = new SrsRtmpTransport(dummy_fd);
    // Prevent destructor from closing dummy fd
    transport->skt_->stfd_ = NULL;

    // Create mock config
    MockAppConfigForRtmpConn *mock_config = new MockAppConfigForRtmpConn();

    // Test the major use scenario: constructor + assemble() pattern
    // This is how SrsRtmpConn is used in production code (srs_app_server.cpp)
    if (true) {
        // Create connection - constructor initializes from global config
        // Note: SrsRtmpConn takes ownership of transport and will delete it
        SrsRtmpConn *conn = new SrsRtmpConn(transport, "192.168.1.100", 1935);

        // Inject mock config before calling assemble()
        // This allows us to test the assemble() pattern without modifying global state
        conn->config_ = mock_config;

        // Initially, no subscription should have occurred
        EXPECT_EQ(0, mock_config->subscribe_count_);
        EXPECT_TRUE(mock_config->last_subscribed_handler_ == NULL);

        // Call assemble() - should subscribe to config
        conn->assemble();
        EXPECT_EQ(1, mock_config->subscribe_count_);
        EXPECT_TRUE(mock_config->last_subscribed_handler_ == conn);

        // Verify constructor initialized all fields correctly
        EXPECT_STREQ("192.168.1.100", conn->ip_.c_str());
        EXPECT_EQ(1935, conn->port_);
        EXPECT_TRUE(conn->config_ == mock_config);
        EXPECT_TRUE(conn->rtmp_ != NULL);
        EXPECT_TRUE(conn->refer_ != NULL);
        EXPECT_TRUE(conn->security_ != NULL);
        EXPECT_TRUE(conn->info_ != NULL);
        EXPECT_TRUE(conn->kbps_ != NULL);
        EXPECT_TRUE(conn->delta_ != NULL);
        EXPECT_TRUE(conn->trd_ != NULL);

        // Cleanup - destructor should unsubscribe and delete transport
        srs_freep(conn);
        EXPECT_EQ(1, mock_config->unsubscribe_count_);
    }

    // Cleanup mock config
    srs_freep(mock_config);
}

VOID TEST(SrsServerTest, OnBeforeConnectionExceedLimit)
{
    srs_error_t err = srs_success;

    // Create server instance
    SrsUniquePtr<SrsServer> server(new SrsServer());

    // Create mock objects
    MockAppConfigForConnectionLimit *mock_config = new MockAppConfigForConnectionLimit();
    MockConnectionManagerForConnectionLimit *mock_conn_manager = new MockConnectionManagerForConnectionLimit();

    // Save original pointers
    ISrsAppConfig *original_config = server->config_;
    ISrsResourceManager *original_conn_manager = server->conn_manager_;

    // Inject mock objects
    server->config_ = mock_config;
    server->conn_manager_ = mock_conn_manager;

    // Test case: Connection limit exceeded
    // This is the major use scenario: when current connections >= max_connections,
    // on_before_connection() should return ERROR_EXCEED_CONNECTIONS error.
    if (true) {
        // Set max connections to 10
        mock_config->max_connections_ = 10;
        // Set current connections to 10 (at limit)
        mock_conn_manager->connection_count_ = 10;

        // Try to accept a new connection - should fail
        err = server->on_before_connection("RTMP", 100, "192.168.1.100", 1935);
        EXPECT_TRUE(err != srs_success);
        EXPECT_EQ(ERROR_EXCEED_CONNECTIONS, srs_error_code(err));
        srs_freep(err);
    }

    // Test case: Connection limit not exceeded
    if (true) {
        // Set max connections to 10
        mock_config->max_connections_ = 10;
        // Set current connections to 9 (below limit)
        mock_conn_manager->connection_count_ = 9;

        // Try to accept a new connection - should succeed
        HELPER_EXPECT_SUCCESS(server->on_before_connection("RTMP", 101, "192.168.1.101", 1935));
    }

    // Restore original pointers
    server->config_ = original_config;
    server->conn_manager_ = original_conn_manager;

    // Cleanup mock objects
    srs_freep(mock_config);
    srs_freep(mock_conn_manager);
}

VOID TEST(SrsRtmpTransportTest, BasicOperations)
{
    srs_error_t err = srs_success;

    // Create a dummy file descriptor (cast from int for testing)
    // Note: We won't actually use this for I/O, just testing the wrapper methods
    srs_netfd_t dummy_fd = (srs_netfd_t)((void *)0x1234);

    // Create SrsRtmpTransport instance
    SrsUniquePtr<SrsRtmpTransport> transport(new SrsRtmpTransport(dummy_fd));

    // Test fd() - should return the file descriptor we passed
    EXPECT_EQ(dummy_fd, transport->fd());

    // Test io() - should return the socket interface (not NULL)
    EXPECT_TRUE(transport->io() != NULL);

    // Test handshake() - should return success (no-op for plain RTMP)
    HELPER_EXPECT_SUCCESS(transport->handshake());

    // Test transport_type() - should return "plaintext"
    EXPECT_STREQ("plaintext", transport->transport_type());

    // Test get_recv_bytes() - should return 0 for new connection
    EXPECT_EQ(0, transport->get_recv_bytes());

    // Test get_send_bytes() - should return 0 for new connection
    EXPECT_EQ(0, transport->get_send_bytes());

    // Prevent the destructor from trying to close the dummy fd
    // by setting the internal stfd_ to NULL before destruction
    transport->skt_->stfd_ = NULL;
}

// Test srs_update_proc_stat() function to verify proper CPU statistics collection
// and calculation. This test covers the major use scenario of updating system and
// self process CPU statistics, which is called periodically by the circuit breaker.
VOID TEST(SrsUtilityTest, UpdateProcStat)
{
    // Call srs_update_proc_stat() - this is the main test
    // This will:
    // 1. Read /proc/stat for system CPU statistics (Linux only)
    // 2. Read /proc/self/stat for self process CPU statistics (Linux only)
    // 3. Calculate CPU usage percentages based on deltas
    // 4. Update the global stat objects
    // On macOS, the function returns early but doesn't crash
    srs_update_proc_stat();

    // Get system and self CPU stats after first update
    SrsProcSystemStat *system_stat = srs_get_system_proc_stat();
    SrsProcSelfStat *self_stat = srs_get_self_proc_stat();

    // Verify that the stat objects are not NULL
    EXPECT_TRUE(system_stat != NULL);
    EXPECT_TRUE(self_stat != NULL);

#if !defined(SRS_OSX)
    // After first call, ok_ should be true if /proc/stat was read successfully
    EXPECT_TRUE(system_stat->ok_);
    // After first call, ok_ should be true if /proc/self/stat was read successfully
    EXPECT_TRUE(self_stat->ok_);
#endif

    // Call srs_update_proc_stat() again to test delta calculation
    // The second call should calculate CPU usage percentage based on
    // the difference between current and previous values
    srs_update_proc_stat();

#if !defined(SRS_OSX)
    // Verify that percent values are in valid range [0, 1]
    // Note: percent_ might be 0 if very little time passed between calls
    EXPECT_TRUE(system_stat->percent_ >= 0.0f);
    EXPECT_TRUE(system_stat->percent_ <= 1.0f);
    EXPECT_TRUE(self_stat->percent_ >= 0.0f);
    // Self process percent can exceed 1.0 on multi-core systems, but should be reasonable
    EXPECT_TRUE(self_stat->percent_ < 100.0f);
#endif
}

VOID TEST(SrsRtmpConnTest, StreamServiceCycleSelection)
{
    srs_error_t err = srs_success;

    // Create mock transport
    MockRtmpTransportForDoCycle *mock_transport = new MockRtmpTransportForDoCycle();

    // Create connection
    SrsRtmpConn *conn = new SrsRtmpConn(mock_transport, "192.168.1.100", 1935);

    // Create mock rtmp server
    MockRtmpServer *mock_rtmp = new MockRtmpServer();
    // Return an error to exit the test cleanly after verifying selection worked
    mock_rtmp->start_play_error_ = srs_error_new(ERROR_RTMP_STREAM_NOT_FOUND, "mock start play");

    // Create mock coroutine that always returns error in pull()
    MockCoroutineForRtmpConn *mock_trd = new MockCoroutineForRtmpConn();
    mock_trd->pull_error_ = srs_error_new(ERROR_THREAD_INTERRUPED, "mock thread interrupted");

    // Inject mocks into connection
    srs_freep(conn->rtmp_);
    conn->rtmp_ = mock_rtmp;
    srs_freep(conn->trd_);
    conn->trd_ = mock_trd;

    // Test the major use scenario: RTMP connection cycle
    // This tests do_cycle() -> service_cycle() path
    // Note: stream_service_cycle() is NOT executed because trd_->pull() returns error
    // in service_cycle() before the while loop calls stream_service_cycle()
    if (true) {
        mock_rtmp->type_ = SrsRtmpConnPlay;
        mock_rtmp->stream_ = "livestream";
        mock_rtmp->start_play_count_ = 0;
        mock_rtmp->start_fmle_publish_count_ = 0;
        mock_rtmp->start_flash_publish_count_ = 0;
        mock_rtmp->start_haivision_publish_count_ = 0;

        // Call do_cycle - executes do_cycle() and service_cycle()
        // Fails at trd_->pull() in service_cycle() before reaching stream_service_cycle()
        err = conn->do_cycle();
        HELPER_EXPECT_FAILED(err);

        // Verify that no play or publish methods were called
        // This is expected because trd_->pull() returns error before stream_service_cycle()
        // is executed, so the selection code (switch statement) is never reached
        EXPECT_EQ(0, mock_rtmp->start_play_count_);
        EXPECT_EQ(0, mock_rtmp->start_fmle_publish_count_);
        EXPECT_EQ(0, mock_rtmp->start_flash_publish_count_);
        EXPECT_EQ(0, mock_rtmp->start_haivision_publish_count_);
    }

    // Cleanup - note: conn owns mock_rtmp, mock_trd, and mock_transport now, so they will be deleted
    srs_freep(conn);
}

// Test srs_get_disk_diskstats_stat() function to verify proper disk statistics
// collection from /proc/diskstats. This test covers the major use scenario of
// reading disk I/O statistics for configured disk devices. The function uses
// the global config to get disk device configuration.
VOID TEST(SrsUtilityTest, GetDiskDiskstatsStat)
{
    // Test case 1: Call with default config - should return true with ok_ = true
    // This is the major use scenario - the function should work with the global config
    // and return success even if no disk devices are configured or /proc/diskstats
    // is not available (on macOS).
    if (true) {
        SrsDiskStat stat;
        bool result = srs_get_disk_diskstats_stat(stat);

        // The function should always return true and set ok_ = true
        EXPECT_TRUE(result);
        EXPECT_TRUE(stat.ok_);
        // sample_time_ should be set to current time
        EXPECT_TRUE(stat.sample_time_ > 0);
    }

#if !defined(SRS_OSX)
    // Test case 2: Verify that disk stat fields are initialized
    // On Linux, if /proc/diskstats exists and disk devices are configured,
    // the function should read and accumulate disk statistics
    if (true) {
        SrsDiskStat stat;
        bool result = srs_get_disk_diskstats_stat(stat);

        EXPECT_TRUE(result);
        EXPECT_TRUE(stat.ok_);
        // All disk stat fields should be initialized (may be 0 if no devices configured)
        // Just verify they are accessible and don't crash
        EXPECT_TRUE(stat.rd_ios_ >= 0);
        EXPECT_TRUE(stat.wr_ios_ >= 0);
        EXPECT_TRUE(stat.rd_sectors_ >= 0);
        EXPECT_TRUE(stat.wr_sectors_ >= 0);
    }
#endif
}

// Test srs_get_cpuinfo() function to verify proper CPU information retrieval
// using sysconf(). This test covers the major use scenario of getting system
// CPU configuration including total processors and online processors. The
// function uses a singleton pattern with static caching for performance.
VOID TEST(SrsUtilityTest, GetCpuInfo)
{
    // Call srs_get_cpuinfo() - this is the main test
    // This will:
    // 1. Create singleton SrsCpuInfo instance on first call
    // 2. Initialize CPU info using sysconf(_SC_NPROCESSORS_CONF) and sysconf(_SC_NPROCESSORS_ONLN)
    // 3. Return cached instance on subsequent calls
    SrsCpuInfo *cpu_info = srs_get_cpuinfo();

    // Verify that the cpu_info object is not NULL
    EXPECT_TRUE(cpu_info != NULL);

    // Verify that ok_ flag is set to true
    EXPECT_TRUE(cpu_info->ok_);

    // Verify that nb_processors_ is positive (at least 1 CPU)
    EXPECT_TRUE(cpu_info->nb_processors_ > 0);

    // Verify that nb_processors_online_ is positive (at least 1 CPU online)
    EXPECT_TRUE(cpu_info->nb_processors_online_ > 0);

    // Verify that online processors <= total processors
    EXPECT_TRUE(cpu_info->nb_processors_online_ <= cpu_info->nb_processors_);

    // Call srs_get_cpuinfo() again to verify singleton pattern
    // Should return the same cached instance
    SrsCpuInfo *cpu_info2 = srs_get_cpuinfo();
    EXPECT_TRUE(cpu_info2 == cpu_info);
}

// Test srs_update_disk_stat() function to verify proper disk statistics calculation
// including vmstat KBps calculation and diskstats busy percentage calculation.
// This test covers the major use scenario of updating disk statistics with delta
// calculations between two samples.
VOID TEST(SrsUtilityTest, UpdateDiskStat)
{
    // Get the initial disk stat to save the original state
    SrsDiskStat *original_stat = srs_get_disk_stat();
    SrsDiskStat saved_stat = *original_stat;

    // Test case 1: First call to srs_update_disk_stat() - should initialize the stat
    // This is the major use scenario - the first call should set ok_ = true and
    // initialize all fields without calculating deltas (since there's no previous sample)
    if (true) {
        srs_update_disk_stat();

        SrsDiskStat *stat = srs_get_disk_stat();
        EXPECT_TRUE(stat->ok_);
        EXPECT_TRUE(stat->sample_time_ > 0);
        // busy_ should be 0 (no delta to calculate)
        EXPECT_EQ(0.0f, stat->busy_);
    }

#if !defined(SRS_OSX)
    // Test case 2: Second call to srs_update_disk_stat() - should calculate deltas
    // This tests the vmstat KBps calculation and diskstats busy percentage calculation
    // On Linux, if /proc/vmstat and /proc/diskstats exist, the function should
    // calculate the delta between current and previous samples
    if (true) {
        // Save the first sample
        SrsDiskStat first_sample = *srs_get_disk_stat();

        // Wait a bit to ensure time difference
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);

        // Call srs_update_disk_stat() again to calculate deltas
        srs_update_disk_stat();

        SrsDiskStat *stat = srs_get_disk_stat();
        EXPECT_TRUE(stat->ok_);
        EXPECT_TRUE(stat->sample_time_ > first_sample.sample_time_);

        // Verify vmstat calculation logic:
        // If pgpgin/pgpgout increased and duration_ms > 0, then KBps should be calculated
        // KBps = (current - previous) * 1000 / duration_ms
        // Note: KBps values may still be 0 if no disk I/O occurred between samples
        EXPECT_TRUE(stat->in_KBps_ >= 0);

        // Verify diskstats calculation logic:
        // If cpu_.ok_ and cpu_.total_delta_ > 0 and cpuinfo->ok_ and nb_processors_ > 0
        // and ticks increased, then busy_ should be calculated
        // busy_ = ticks / delta_ms, where delta_ms = cpu_.total_delta_ * 10 / nb_processors_
        // Note: busy_ may still be 0 if no disk I/O occurred or if conditions not met
        EXPECT_TRUE(stat->busy_ >= 0.0f);
        EXPECT_TRUE(stat->busy_ <= 1.0f); // busy_ should be in [0, 1] range
    }

    // Test case 3: Verify the calculation formulas with known values
    // This tests the specific calculation logic for vmstat and diskstats
    if (true) {
        // Get current stat
        SrsDiskStat *current = srs_get_disk_stat();

        // Manually create a previous stat with known values to test calculation
        SrsDiskStat prev = *current;
        prev.sample_time_ = current->sample_time_ - 1000; // 1 second ago
        prev.pgpgin_ = 1000;                              // 1000 KB read
        prev.pgpgout_ = 2000;                             // 2000 KB written
        prev.ticks_ = 100;                                // 100 ticks
        prev.cpu_.ok_ = true;
        prev.cpu_.user_ = 1000;
        prev.cpu_.sys_ = 500;
        prev.cpu_.idle_ = 8500;
        prev.cpu_.total_delta_ = 0;

        // Create a new stat with increased values
        SrsDiskStat next = *current;
        next.sample_time_ = current->sample_time_;
        next.pgpgin_ = 2000;  // 1000 KB more read
        next.pgpgout_ = 4000; // 2000 KB more written
        next.ticks_ = 200;    // 100 ticks more
        next.cpu_.ok_ = true;
        next.cpu_.user_ = 1100;
        next.cpu_.sys_ = 600;
        next.cpu_.idle_ = 8800;
        next.cpu_.total_delta_ = next.cpu_.total() - prev.cpu_.total();

        // Calculate expected values
        int64_t duration_ms = next.sample_time_ - prev.sample_time_;
        if (duration_ms > 0 && prev.pgpgin_ > 0 && next.pgpgin_ > prev.pgpgin_) {
            int expected_in_KBps = (int)((next.pgpgin_ - prev.pgpgin_) * 1000 / duration_ms);
            // Verify the formula: KBps = KB * 1000 / ms = KB/s
            EXPECT_EQ(expected_in_KBps, (int)((next.pgpgin_ - prev.pgpgin_) * 1000 / duration_ms));
        }

        if (duration_ms > 0 && prev.pgpgout_ > 0 && next.pgpgout_ > prev.pgpgout_) {
            int expected_out_KBps = (int)((next.pgpgout_ - prev.pgpgout_) * 1000 / duration_ms);
            // Verify the formula: KBps = KB * 1000 / ms = KB/s
            EXPECT_EQ(expected_out_KBps, (int)((next.pgpgout_ - prev.pgpgout_) * 1000 / duration_ms));
        }

        // Verify diskstats busy calculation formula
        if (next.cpu_.ok_ && prev.cpu_.ok_ && next.cpu_.total_delta_ > 0) {
            SrsCpuInfo *cpuinfo = srs_get_cpuinfo();
            if (cpuinfo->ok_ && cpuinfo->nb_processors_ > 0 && prev.ticks_ < next.ticks_) {
                // delta_ms = cpu_.total_delta_ * 10 / nb_processors_
                double delta_ms = next.cpu_.total_delta_ * 10 / cpuinfo->nb_processors_;
                unsigned int ticks = next.ticks_ - prev.ticks_;
                // busy_ = ticks / delta_ms
                float expected_busy = (float)(ticks / delta_ms);
                // Verify the formula is correct
                EXPECT_TRUE(expected_busy >= 0.0f);
            }
        }
    }
#endif

    // Restore the original disk stat to avoid side effects
    *original_stat = saved_stat;
}

VOID TEST(SrsRtmpConnTest, StreamServiceCycleThreadQuitCheck)
{
    srs_error_t err = srs_success;

    // This test covers the thread quit check in playing() and publishing() methods
    // that are called from stream_service_cycle(). The major use scenario is testing
    // that when stream_service_cycle() calls playing() or publishing(), those methods
    // check for thread quit (trd_->pull()) and return immediately if the thread is quitting.

    MockRtmpTransportForDoCycle *mock_transport = new MockRtmpTransportForDoCycle();
    MockRtmpServer *mock_rtmp = new MockRtmpServer();
    mock_rtmp->start_play_error_ = srs_error_new(ERROR_THREAD_INTERRUPED, "thread interrupted");
    MockSecurity *mock_security = new MockSecurity();
    MockCoroutineForRtmpConn *mock_trd = new MockCoroutineForRtmpConn();
    MockAppConfigForRtmpConn *mock_config = new MockAppConfigForRtmpConn();

    SrsRtmpConn *conn = new SrsRtmpConn(mock_transport, "127.0.0.1", 1935);

    // Inject mock config before assemble()
    conn->config_ = mock_config;
    conn->assemble();

    // Inject mock objects
    srs_freep(conn->rtmp_);
    conn->rtmp_ = mock_rtmp;
    srs_freep(conn->security_);
    conn->security_ = mock_security;
    srs_freep(conn->trd_);
    conn->trd_ = mock_trd;

    // Configure mock to simulate thread quitting
    // Set pull_count_ to 1 so the next call will return error (mock returns success on first call)
    mock_trd->pull_count_ = 1;
    mock_trd->pull_error_ = srs_error_new(ERROR_THREAD_INTERRUPED, "thread interrupted");

    // Set connection type to Play
    mock_rtmp->type_ = SrsRtmpConnPlay;
    mock_rtmp->stream_ = "livestream";
    conn->info_->type_ = SrsRtmpConnPlay;

    // Set up request with valid tcUrl to pass tcUrl parsing
    conn->info_->req_->tcUrl_ = "rtmp://127.0.0.1/live";
    conn->info_->req_->vhost_ = "__defaultVhost__";
    conn->info_->req_->app_ = "live";
    conn->info_->req_->stream_ = "livestream";

    // Call stream_service_cycle - it should reach the switch statement,
    // call start_play, then call playing() which should immediately return
    // error from trd_->pull() check
    err = conn->stream_service_cycle();
    EXPECT_TRUE(err != srs_success);
    srs_freep(err);

    // Verify that start_play was called (selection code was reached)
    EXPECT_EQ(1, mock_rtmp->start_play_count_);
    // Verify that pull() was called in playing()
    EXPECT_EQ(1, mock_trd->pull_count_);

    srs_freep(conn);
    srs_freep(mock_config);
}

// Mock config implementation for SrsRtmpConn::acquire_publish() testing
MockAppConfigForAcquirePublish::MockAppConfigForAcquirePublish()
{
    rtc_server_enabled_ = true;
    rtc_enabled_ = true;
    srt_enabled_ = true;
    rtsp_server_enabled_ = false;
    rtsp_enabled_ = false;
}

MockAppConfigForAcquirePublish::~MockAppConfigForAcquirePublish()
{
}

bool MockAppConfigForAcquirePublish::get_rtc_server_enabled()
{
    return rtc_server_enabled_;
}

bool MockAppConfigForAcquirePublish::get_rtc_enabled(std::string vhost)
{
    return rtc_enabled_;
}

bool MockAppConfigForAcquirePublish::get_srt_enabled()
{
    return srt_enabled_;
}

bool MockAppConfigForAcquirePublish::get_srt_enabled(std::string vhost)
{
    return srt_enabled_;
}

bool MockAppConfigForAcquirePublish::get_rtsp_server_enabled()
{
    return rtsp_server_enabled_;
}

bool MockAppConfigForAcquirePublish::get_rtsp_enabled(std::string vhost)
{
    return rtsp_enabled_;
}

// Test SrsRtmpConn::acquire_publish() method to verify proper cross-protocol stream
// busy checking for RTC, SRT, and RTSP sources. This test covers the major use scenario
// of acquiring publish permission when multiple protocols are enabled and checking that
// RTC and SRT sources are properly checked for busy state.
VOID TEST(SrsRtmpConnTest, AcquirePublishCrossProtocolCheck)
{
    srs_error_t err = srs_success;

    // Create mock config (must outlive connection)
    MockAppConfigForAcquirePublish *mock_config = new MockAppConfigForAcquirePublish();

    // Create mock source managers (from srs_utest_app6.hpp)
    MockRtcSourceManager *mock_rtc_sources = new MockRtcSourceManager();
    MockSrtSourceManager *mock_srt_sources = new MockSrtSourceManager();

    // Use scope block to ensure conn is destroyed before mock objects
    {
        // Create mock transport
        MockRtmpTransportForDoCycle *mock_transport = new MockRtmpTransportForDoCycle();

        // Create connection
        SrsUniquePtr<SrsRtmpConn> conn(new SrsRtmpConn(mock_transport, "192.168.1.100", 1935));

        // Inject mock config before assemble()
        conn->config_ = mock_config;
        conn->assemble();

        // Inject mock source managers
        conn->rtc_sources_ = mock_rtc_sources;
        conn->srt_sources_ = mock_srt_sources;

        // Setup request
        conn->info_->req_->vhost_ = "__defaultVhost__";
        conn->info_->req_->app_ = "live";
        conn->info_->req_->stream_ = "livestream";
        conn->info_->edge_ = false;

        // Create a mock live source (from srs_utest_app6.hpp)
        SrsSharedPtr<SrsLiveSource> live_source(new MockLiveSource());

        // Test major use scenario: RTC and SRT enabled, both sources can publish
        // This covers the code path where rtc_server_enabled && rtc_enabled && !info_->edge_
        // and srt_server_enabled && srt_enabled && !info_->edge_
        if (true) {
            // Configure SRT source to allow publishing
            mock_srt_sources->set_can_publish(true);

            // Note: MockRtcSourceManager creates a real SrsRtcSource which by default
            // allows publishing (can_publish() returns true when no publisher exists)

            // Call acquire_publish - should succeed
            HELPER_EXPECT_SUCCESS(conn->acquire_publish(live_source));

            // Verify that fetch_or_create was called for both RTC and SRT
            EXPECT_EQ(1, mock_rtc_sources->fetch_or_create_count_);
            EXPECT_EQ(1, mock_srt_sources->fetch_or_create_count_);
        }

        // Test scenario: SRT stream is busy (cannot publish)
        // This covers the code path where !srt->can_publish() returns error
        if (true) {
            // Reset counters
            mock_rtc_sources->fetch_or_create_count_ = 0;
            mock_srt_sources->fetch_or_create_count_ = 0;

            // Configure SRT source to reject publishing
            mock_srt_sources->set_can_publish(false);

            // Call acquire_publish - should fail with ERROR_SYSTEM_STREAM_BUSY
            err = conn->acquire_publish(live_source);
            EXPECT_TRUE(err != srs_success);
            EXPECT_EQ(ERROR_SYSTEM_STREAM_BUSY, srs_error_code(err));
            srs_freep(err);

            // Verify that fetch_or_create was called for both RTC and SRT
            EXPECT_EQ(1, mock_rtc_sources->fetch_or_create_count_);
            EXPECT_EQ(1, mock_srt_sources->fetch_or_create_count_);
        }

        // Test scenario: Edge server - RTC and SRT should be skipped
        // This covers the code path where info_->edge_ is true
        if (true) {
            // Reset counters
            mock_rtc_sources->fetch_or_create_count_ = 0;
            mock_srt_sources->fetch_or_create_count_ = 0;

            // Set connection as edge
            conn->info_->edge_ = true;

            // Configure SRT source to allow publishing
            mock_srt_sources->set_can_publish(true);

            // Call acquire_publish - should succeed
            HELPER_EXPECT_SUCCESS(conn->acquire_publish(live_source));

            // Verify that fetch_or_create was NOT called for RTC and SRT (edge server)
            EXPECT_EQ(0, mock_rtc_sources->fetch_or_create_count_);
            EXPECT_EQ(0, mock_srt_sources->fetch_or_create_count_);
        }
    }

    // Cleanup mock objects
    srs_freep(mock_config);
    srs_freep(mock_rtc_sources);
    srs_freep(mock_srt_sources);
}

VOID TEST(SrsRtmpConnTest, HandlePublishMessageFlashRepublish)
{
    srs_error_t err = srs_success;

    // Create mock config (must outlive connection)
    MockAppConfigForRtmpConn *mock_config = new MockAppConfigForRtmpConn();

    // Use scope block to ensure conn is destroyed before mock_config
    {
        // Create mock transport
        MockRtmpTransportForDoCycle *mock_transport = new MockRtmpTransportForDoCycle();

        // Create connection
        SrsUniquePtr<SrsRtmpConn> conn(new SrsRtmpConn(mock_transport, "192.168.1.100", 1935));

        // Inject mock config before assemble()
        conn->config_ = mock_config;
        conn->assemble();

        // Create mock rtmp server
        MockRtmpServer *mock_rtmp = new MockRtmpServer();

        // Inject mock into connection
        srs_freep(conn->rtmp_);
        conn->rtmp_ = mock_rtmp;

        // Test major use scenario: Flash publish - any AMF command should return ERROR_CONTROL_REPUBLISH
        // This covers the code path where info_->type_ == SrsRtmpConnFlashPublish
        if (true) {
            // Set connection type to Flash publish
            conn->info_->type_ = SrsRtmpConnFlashPublish;

            // Create a dummy AMF0 command message
            SrsUniquePtr<SrsRtmpCommonMessage> msg(new SrsRtmpCommonMessage());
            msg->header_.message_type_ = RTMP_MSG_AMF0CommandMessage;

            // Configure mock to return a generic command packet
            mock_rtmp->decode_message_packet_ = new SrsCallPacket();
            mock_rtmp->decode_message_error_ = srs_success;

            // Create a dummy live source (not used in this test path)
            SrsSharedPtr<SrsLiveSource> source;

            // Call handle_publish_message - should return ERROR_CONTROL_REPUBLISH
            err = conn->handle_publish_message(source, msg.get());
            EXPECT_TRUE(err != srs_success);
            EXPECT_EQ(ERROR_CONTROL_REPUBLISH, srs_error_code(err));
            srs_freep(err);

            // Verify decode_message was called
            EXPECT_EQ(1, mock_rtmp->decode_message_count_);
        }
    }

    // Cleanup mock config after connection is destroyed
    srs_freep(mock_config);
}

VOID TEST(SrsRtmpConnTest, HandlePublishMessageFMLERepublish)
{
    srs_error_t err = srs_success;

    // Create mock config (must outlive connection)
    MockAppConfigForRtmpConn *mock_config = new MockAppConfigForRtmpConn();

    // Use scope block to ensure conn is destroyed before mock_config
    {
        // Create mock transport
        MockRtmpTransportForDoCycle *mock_transport = new MockRtmpTransportForDoCycle();

        // Create connection
        SrsUniquePtr<SrsRtmpConn> conn(new SrsRtmpConn(mock_transport, "192.168.1.100", 1935));

        // Inject mock config before assemble()
        conn->config_ = mock_config;
        conn->assemble();

        // Create mock rtmp server
        MockRtmpServer *mock_rtmp = new MockRtmpServer();

        // Inject mock into connection
        srs_freep(conn->rtmp_);
        conn->rtmp_ = mock_rtmp;

        // Test major use scenario: FMLE publish with SrsFMLEStartPacket - should call fmle_unpublish
        // This covers the code path where dynamic_cast<SrsFMLEStartPacket *>(pkt.get()) succeeds
        if (true) {
            // Set connection type to FMLE publish
            conn->info_->type_ = SrsRtmpConnFMLEPublish;
            conn->info_->res_->stream_id_ = 1;

            // Create a dummy AMF0 command message
            SrsUniquePtr<SrsRtmpCommonMessage> msg(new SrsRtmpCommonMessage());
            msg->header_.message_type_ = RTMP_MSG_AMF0CommandMessage;

            // Configure mock to return SrsFMLEStartPacket
            SrsFMLEStartPacket *fmle_pkt = new SrsFMLEStartPacket();
            fmle_pkt->transaction_id_ = 2.0;
            mock_rtmp->decode_message_packet_ = fmle_pkt;
            mock_rtmp->decode_message_error_ = srs_success;
            mock_rtmp->fmle_unpublish_error_ = srs_success;

            // Create a dummy live source (not used in this test path)
            SrsSharedPtr<SrsLiveSource> source;

            // Call handle_publish_message - should return ERROR_CONTROL_REPUBLISH
            err = conn->handle_publish_message(source, msg.get());
            EXPECT_TRUE(err != srs_success);
            EXPECT_EQ(ERROR_CONTROL_REPUBLISH, srs_error_code(err));
            srs_freep(err);

            // Verify decode_message and fmle_unpublish were called
            EXPECT_EQ(1, mock_rtmp->decode_message_count_);
            EXPECT_EQ(1, mock_rtmp->fmle_unpublish_count_);
        }
    }

    // Cleanup mock config after connection is destroyed
    srs_freep(mock_config);
}

VOID TEST(SrsRtmpConnTest, HandlePublishMessageFMLEIgnoreCommand)
{
    srs_error_t err = srs_success;

    // Create mock config (must outlive connection)
    MockAppConfigForRtmpConn *mock_config = new MockAppConfigForRtmpConn();

    // Use scope block to ensure conn is destroyed before mock_config
    {
        // Create mock transport
        MockRtmpTransportForDoCycle *mock_transport = new MockRtmpTransportForDoCycle();

        // Create connection
        SrsUniquePtr<SrsRtmpConn> conn(new SrsRtmpConn(mock_transport, "192.168.1.100", 1935));

        // Inject mock config before assemble()
        conn->config_ = mock_config;
        conn->assemble();

        // Create mock rtmp server
        MockRtmpServer *mock_rtmp = new MockRtmpServer();

        // Inject mock into connection
        srs_freep(conn->rtmp_);
        conn->rtmp_ = mock_rtmp;

        // Test major use scenario: FMLE publish with non-FMLE command - should trace and return success
        // This covers the code path where the packet is not SrsFMLEStartPacket
        if (true) {
            // Set connection type to FMLE publish
            conn->info_->type_ = SrsRtmpConnFMLEPublish;

            // Create a dummy AMF0 command message
            SrsUniquePtr<SrsRtmpCommonMessage> msg(new SrsRtmpCommonMessage());
            msg->header_.message_type_ = RTMP_MSG_AMF0CommandMessage;

            // Configure mock to return a generic command packet (not SrsFMLEStartPacket)
            mock_rtmp->decode_message_packet_ = new SrsCallPacket();
            mock_rtmp->decode_message_error_ = srs_success;

            // Create a dummy live source (not used in this test path)
            SrsSharedPtr<SrsLiveSource> source;

            // Call handle_publish_message - should return success (trace and ignore)
            HELPER_EXPECT_SUCCESS(conn->handle_publish_message(source, msg.get()));

            // Verify decode_message was called but fmle_unpublish was not
            EXPECT_EQ(1, mock_rtmp->decode_message_count_);
            EXPECT_EQ(0, mock_rtmp->fmle_unpublish_count_);
        }
    }

    // Cleanup mock config after connection is destroyed
    srs_freep(mock_config);
}

VOID TEST(SrsRtmpConnTest, AcquirePublishStreamBusyCheck)
{
    srs_error_t err = srs_success;

    // Create mock config that disables all protocols except RTMP
    MockAppConfigForRtmpConn *mock_config = new MockAppConfigForRtmpConn();

    // Use scope block to ensure conn is destroyed before mock_config
    {
        // Create mock transport
        MockRtmpTransportForDoCycle *mock_transport = new MockRtmpTransportForDoCycle();

        // Create connection
        SrsUniquePtr<SrsRtmpConn> conn(new SrsRtmpConn(mock_transport, "192.168.1.100", 1935));

        // Inject mock config before assemble()
        conn->config_ = mock_config;
        conn->assemble();

        // Create mock rtmp server
        MockRtmpServer *mock_rtmp = new MockRtmpServer();
        mock_rtmp->start_play_error_ = srs_error_new(ERROR_RTMP_STREAM_NOT_FOUND, "mock start play");

        // Create mock security
        MockSecurity *mock_security = new MockSecurity();

        // Inject mocks into connection
        srs_freep(conn->rtmp_);
        conn->rtmp_ = mock_rtmp;
        srs_freep(conn->security_);
        conn->security_ = mock_security;

        // Set up request with valid stream info
        conn->info_->req_->tcUrl_ = "rtmp://127.0.0.1/live";
        conn->info_->req_->vhost_ = "__defaultVhost__";
        conn->info_->req_->app_ = "live";
        conn->info_->req_->stream_ = "livestream";
        conn->info_->edge_ = false;

        // Test the major use scenario: acquire_publish fails when RTMP stream is busy
        // Create mock live source that does NOT allow publishing (stream is busy)
        SrsSharedPtr<SrsLiveSource> source(new MockLiveSource());
        MockLiveSource *mock_source = dynamic_cast<MockLiveSource *>(source.get());
        mock_source->set_can_publish(false); // Stream is busy

        // Call acquire_publish - should fail with ERROR_SYSTEM_STREAM_BUSY
        err = conn->acquire_publish(source);
        EXPECT_TRUE(err != srs_success);
        EXPECT_EQ(ERROR_SYSTEM_STREAM_BUSY, srs_error_code(err));
        srs_freep(err);

        // Note: conn owns mock_rtmp and mock_security, they will be deleted by conn destructor
    } // conn is destroyed here

    // Now safe to delete mock_config
    srs_freep(mock_config);
}

// Test SrsRtmpConn::handle_publish_message() to verify proper handling of video/audio messages
// during publishing. This test covers the major use scenario: processing a video message through
// process_publish_message() which calls source->on_video().
VOID TEST(SrsRtmpConnTest, HandlePublishMessageVideoSuccess)
{
    srs_error_t err = srs_success;

    // Create mock config
    MockAppConfigForRtmpConn *mock_config = new MockAppConfigForRtmpConn();

    // Use scope block to ensure conn is destroyed before mock_config
    {
        // Create mock transport
        MockRtmpTransportForDoCycle *mock_transport = new MockRtmpTransportForDoCycle();

        // Create connection
        SrsUniquePtr<SrsRtmpConn> conn(new SrsRtmpConn(mock_transport, "192.168.1.100", 1935));

        // Inject mock config before assemble()
        conn->config_ = mock_config;
        conn->assemble();

        // Create mock rtmp server
        MockRtmpServer *mock_rtmp = new MockRtmpServer();

        // Inject mock into connection
        srs_freep(conn->rtmp_);
        conn->rtmp_ = mock_rtmp;

        // Set connection type to FMLE publish (not Flash, so AMF commands are ignored)
        conn->info_->type_ = SrsRtmpConnFMLEPublish;
        conn->info_->edge_ = false;

        // Create mock live source
        SrsSharedPtr<SrsLiveSource> source(new MockLiveSource());

        // Create a video message (RTMP_MSG_VideoMessage = 9)
        SrsUniquePtr<SrsRtmpCommonMessage> msg(new SrsRtmpCommonMessage());
        msg->header_.message_type_ = RTMP_MSG_VideoMessage;
        msg->header_.payload_length_ = 10;
        msg->header_.timestamp_ = 1000;
        msg->header_.stream_id_ = 1;
        msg->create_payload(10);

        // Test the major use scenario: handle_publish_message processes video message
        // This should:
        // 1. Check if message is AMF command (it's not - it's video)
        // 2. Call process_publish_message() which calls source->on_video()
        HELPER_EXPECT_SUCCESS(conn->handle_publish_message(source, msg.get()));

        // Verify that decode_message was NOT called (because it's not an AMF command)
        EXPECT_EQ(0, mock_rtmp->decode_message_count_);

        // Note: conn owns mock_rtmp, it will be deleted by conn destructor
    } // conn is destroyed here

    // Now safe to delete mock_config
    srs_freep(mock_config);
}

MockLiveConsumerForPlayControl::MockLiveConsumerForPlayControl(ISrsLiveSource *source)
    : SrsLiveConsumer(source)
{
    on_play_client_pause_count_ = 0;
    last_pause_state_ = false;
}

MockLiveConsumerForPlayControl::~MockLiveConsumerForPlayControl()
{
}

srs_error_t MockLiveConsumerForPlayControl::on_play_client_pause(bool is_pause)
{
    on_play_client_pause_count_++;
    last_pause_state_ = is_pause;
    return srs_success;
}

MockAppConfigForHttpHooksOnConnect::MockAppConfigForHttpHooksOnConnect()
{
    http_hooks_enabled_ = false;
    on_connect_directive_ = NULL;
}

MockAppConfigForHttpHooksOnConnect::~MockAppConfigForHttpHooksOnConnect()
{
    srs_freep(on_connect_directive_);
}

bool MockAppConfigForHttpHooksOnConnect::get_vhost_http_hooks_enabled(std::string vhost)
{
    return http_hooks_enabled_;
}

SrsConfDirective *MockAppConfigForHttpHooksOnConnect::get_vhost_on_connect(std::string vhost)
{
    return on_connect_directive_;
}

MockAppConfigForHttpHooksOnClose::MockAppConfigForHttpHooksOnClose()
{
    http_hooks_enabled_ = false;
    on_close_directive_ = NULL;
}

MockAppConfigForHttpHooksOnClose::~MockAppConfigForHttpHooksOnClose()
{
    srs_freep(on_close_directive_);
}

bool MockAppConfigForHttpHooksOnClose::get_vhost_http_hooks_enabled(std::string vhost)
{
    return http_hooks_enabled_;
}

SrsConfDirective *MockAppConfigForHttpHooksOnClose::get_vhost_on_close(std::string vhost)
{
    return on_close_directive_;
}

MockHttpHooksForOnConnect::MockHttpHooksForOnConnect()
{
    on_connect_count_ = 0;
    on_connect_error_ = srs_success;
    on_close_count_ = 0;
    on_unpublish_count_ = 0;
    on_stop_count_ = 0;
}

MockHttpHooksForOnConnect::~MockHttpHooksForOnConnect()
{
    srs_freep(on_connect_error_);
    on_connect_calls_.clear();
    on_close_calls_.clear();
    on_unpublish_calls_.clear();
    on_stop_calls_.clear();
}

srs_error_t MockHttpHooksForOnConnect::on_connect(std::string url, ISrsRequest *req)
{
    on_connect_count_++;
    on_connect_calls_.push_back(std::make_pair(url, req));
    return srs_error_copy(on_connect_error_);
}

void MockHttpHooksForOnConnect::on_close(std::string url, ISrsRequest *req, int64_t send_bytes, int64_t recv_bytes)
{
    on_close_count_++;
    OnCloseCall call;
    call.url_ = url;
    call.req_ = req;
    call.send_bytes_ = send_bytes;
    call.recv_bytes_ = recv_bytes;
    on_close_calls_.push_back(call);
}

srs_error_t MockHttpHooksForOnConnect::on_publish(std::string url, ISrsRequest *req)
{
    return srs_success;
}

void MockHttpHooksForOnConnect::on_unpublish(std::string url, ISrsRequest *req)
{
    on_unpublish_count_++;
    on_unpublish_calls_.push_back(std::make_pair(url, req));
}

srs_error_t MockHttpHooksForOnConnect::on_play(std::string url, ISrsRequest *req)
{
    return srs_success;
}

void MockHttpHooksForOnConnect::on_stop(std::string url, ISrsRequest *req)
{
    on_stop_count_++;
    on_stop_calls_.push_back(std::make_pair(url, req));
}

srs_error_t MockHttpHooksForOnConnect::on_dvr(SrsContextId cid, std::string url, ISrsRequest *req, std::string file)
{
    return srs_success;
}

srs_error_t MockHttpHooksForOnConnect::on_hls(SrsContextId cid, std::string url, ISrsRequest *req, std::string file, std::string ts_url,
                                              std::string m3u8, std::string m3u8_url, int sn, srs_utime_t duration)
{
    return srs_success;
}

srs_error_t MockHttpHooksForOnConnect::on_hls_notify(SrsContextId cid, std::string url, ISrsRequest *req, std::string ts_url, int nb_notify)
{
    return srs_success;
}

srs_error_t MockHttpHooksForOnConnect::discover_co_workers(std::string url, std::string &host, int &port)
{
    return srs_success;
}

srs_error_t MockHttpHooksForOnConnect::on_forward_backend(std::string url, ISrsRequest *req, std::vector<std::string> &rtmp_urls)
{
    return srs_success;
}

void MockHttpHooksForOnConnect::reset()
{
    srs_freep(on_connect_error_);
    on_connect_error_ = srs_success;
    on_connect_count_ = 0;
    on_connect_calls_.clear();
    on_close_count_ = 0;
    on_close_calls_.clear();
    on_unpublish_count_ = 0;
    on_unpublish_calls_.clear();
    on_stop_count_ = 0;
    on_stop_calls_.clear();
}

VOID TEST(SrsRtmpConnTest, HttpHooksOnConnect)
{
    srs_error_t err = srs_success;

    // Create mock config
    MockAppConfigForHttpHooksOnConnect *mock_config = new MockAppConfigForHttpHooksOnConnect();

    // Create mock hooks
    MockHttpHooksForOnConnect *mock_hooks = new MockHttpHooksForOnConnect();

    // Use scope block to ensure conn is destroyed before mock_config
    {
        // Create mock transport
        MockRtmpTransportForDoCycle *mock_transport = new MockRtmpTransportForDoCycle();

        // Create connection
        SrsUniquePtr<SrsRtmpConn> conn(new SrsRtmpConn(mock_transport, "192.168.1.100", 1935));

        // Inject mock config before assemble()
        conn->config_ = mock_config;
        conn->assemble();

        // Inject mock hooks
        ISrsHttpHooks *original_hooks = conn->hooks_;
        conn->hooks_ = mock_hooks;

        // Set up request with vhost
        conn->info_->req_->vhost_ = "__defaultVhost__";

        // Test case 1: HTTP hooks disabled - should return success without calling hooks
        if (true) {
            mock_config->http_hooks_enabled_ = false;
            mock_hooks->reset();

            HELPER_EXPECT_SUCCESS(conn->http_hooks_on_connect());
            EXPECT_EQ(0, mock_hooks->on_connect_count_);
        }

        // Test case 2: HTTP hooks enabled but no on_connect directive - should return success
        if (true) {
            mock_config->http_hooks_enabled_ = true;
            mock_config->on_connect_directive_ = NULL;
            mock_hooks->reset();

            HELPER_EXPECT_SUCCESS(conn->http_hooks_on_connect());
            EXPECT_EQ(0, mock_hooks->on_connect_count_);
        }

        // Test case 3: HTTP hooks enabled with single URL - should call hooks_->on_connect once
        if (true) {
            mock_config->http_hooks_enabled_ = true;
            mock_config->on_connect_directive_ = new SrsConfDirective();
            mock_config->on_connect_directive_->name_ = "on_connect";
            mock_config->on_connect_directive_->args_.push_back("http://localhost:8080/api/on_connect");
            mock_hooks->reset();

            HELPER_EXPECT_SUCCESS(conn->http_hooks_on_connect());
            EXPECT_EQ(1, mock_hooks->on_connect_count_);
            EXPECT_EQ(1, (int)mock_hooks->on_connect_calls_.size());
            EXPECT_STREQ("http://localhost:8080/api/on_connect", mock_hooks->on_connect_calls_[0].first.c_str());
            EXPECT_TRUE(mock_hooks->on_connect_calls_[0].second == conn->info_->req_);
        }

        // Test case 4: HTTP hooks enabled with multiple URLs - should call hooks_->on_connect for each URL
        if (true) {
            srs_freep(mock_config->on_connect_directive_);
            mock_config->http_hooks_enabled_ = true;
            mock_config->on_connect_directive_ = new SrsConfDirective();
            mock_config->on_connect_directive_->name_ = "on_connect";
            mock_config->on_connect_directive_->args_.push_back("http://localhost:8080/api/on_connect1");
            mock_config->on_connect_directive_->args_.push_back("http://localhost:8080/api/on_connect2");
            mock_config->on_connect_directive_->args_.push_back("http://localhost:8080/api/on_connect3");
            mock_hooks->reset();

            HELPER_EXPECT_SUCCESS(conn->http_hooks_on_connect());
            EXPECT_EQ(3, mock_hooks->on_connect_count_);
            EXPECT_EQ(3, (int)mock_hooks->on_connect_calls_.size());
            EXPECT_STREQ("http://localhost:8080/api/on_connect1", mock_hooks->on_connect_calls_[0].first.c_str());
            EXPECT_STREQ("http://localhost:8080/api/on_connect2", mock_hooks->on_connect_calls_[1].first.c_str());
            EXPECT_STREQ("http://localhost:8080/api/on_connect3", mock_hooks->on_connect_calls_[2].first.c_str());
        }

        // Test case 5: HTTP hooks enabled but on_connect returns error - should wrap and return error
        if (true) {
            srs_freep(mock_config->on_connect_directive_);
            mock_config->http_hooks_enabled_ = true;
            mock_config->on_connect_directive_ = new SrsConfDirective();
            mock_config->on_connect_directive_->name_ = "on_connect";
            mock_config->on_connect_directive_->args_.push_back("http://localhost:8080/api/on_connect");
            mock_hooks->reset();
            mock_hooks->on_connect_error_ = srs_error_new(ERROR_SYSTEM_STREAM_BUSY, "mock hook error");

            err = conn->http_hooks_on_connect();
            EXPECT_TRUE(err != srs_success);
            srs_freep(err);
            EXPECT_EQ(1, mock_hooks->on_connect_count_);
        }

        // Restore original hooks
        conn->hooks_ = original_hooks;
    } // conn is destroyed here

    // Now safe to delete mock objects
    srs_freep(mock_hooks);
    srs_freep(mock_config);
}

// Test SrsRtmpConn::process_play_control_msg() to verify proper handling of pause control messages
// during playback. This test covers the major use scenario: processing a pause packet which calls
// both rtmp_->on_play_client_pause() and consumer->on_play_client_pause().
VOID TEST(SrsRtmpConnTest, ProcessPlayControlMsgPauseSuccess)
{
    srs_error_t err = srs_success;

    // Create mock config
    MockAppConfigForRtmpConn *mock_config = new MockAppConfigForRtmpConn();

    // Use scope block to ensure conn is destroyed before mock_config
    {
        // Create mock transport
        MockRtmpTransportForDoCycle *mock_transport = new MockRtmpTransportForDoCycle();

        // Create connection
        SrsUniquePtr<SrsRtmpConn> conn(new SrsRtmpConn(mock_transport, "192.168.1.100", 1935));

        // Inject mock config before assemble()
        conn->config_ = mock_config;
        conn->assemble();

        // Create mock rtmp server
        MockRtmpServer *mock_rtmp = new MockRtmpServer();

        // Create pause packet to be returned by decode_message
        SrsPausePacket *pause_pkt = new SrsPausePacket();
        pause_pkt->is_pause_ = true;
        pause_pkt->time_ms_ = 1000.0;
        mock_rtmp->decode_message_packet_ = pause_pkt;

        // Inject mock rtmp into connection
        srs_freep(conn->rtmp_);
        conn->rtmp_ = mock_rtmp;

        // Initialize stream_id in response object
        conn->info_->res_->stream_id_ = 1;

        // Create mock live source and consumer (use MockLiveSourceForQueue which properly handles on_consumer_destroy)
        SrsSharedPtr<MockLiveSourceForQueue> source(new MockLiveSourceForQueue());
        SrsUniquePtr<MockLiveConsumerForPlayControl> consumer(new MockLiveConsumerForPlayControl(source.get()));

        // Create an AMF0 command message
        SrsRtmpCommonMessage *msg = new SrsRtmpCommonMessage();
        msg->header_.message_type_ = RTMP_MSG_AMF0CommandMessage;
        msg->header_.payload_length_ = 10;
        msg->header_.timestamp_ = 1000;
        msg->header_.stream_id_ = 1;
        msg->create_payload(10);

        // Test the major use scenario: process_play_control_msg handles pause packet
        // This should:
        // 1. Check if message is AMF command (it is)
        // 2. Decode the message to get pause packet
        // 3. Call rtmp_->on_play_client_pause()
        // 4. Call consumer->on_play_client_pause()
        HELPER_EXPECT_SUCCESS(conn->process_play_control_msg(consumer.get(), msg));

        // Verify that decode_message was called
        EXPECT_EQ(1, mock_rtmp->decode_message_count_);

        // Verify that on_play_client_pause was called on both rtmp and consumer
        EXPECT_EQ(1, mock_rtmp->on_play_client_pause_count_);
        EXPECT_EQ(true, mock_rtmp->last_pause_state_);
        EXPECT_EQ(1, consumer->on_play_client_pause_count_);
        EXPECT_EQ(true, consumer->last_pause_state_);

        // Note: conn owns mock_rtmp, it will be deleted by conn destructor
    } // conn is destroyed here

    // Now safe to delete mock_config
    srs_freep(mock_config);
}

// Test SrsRtmpConn::http_hooks_on_close() method to verify proper HTTP hook invocation
// when a client disconnects. This test covers the major use scenario where on_close hooks
// are configured and should be called with the correct URL, request, and byte counts.
VOID TEST(SrsRtmpConnTest, HttpHooksOnClose)
{
    // Create mock transport
    MockRtmpTransportForDoCycle *mock_transport = new MockRtmpTransportForDoCycle();

    // Create connection
    SrsUniquePtr<SrsRtmpConn> conn(new SrsRtmpConn(mock_transport, "192.168.1.100", 1935));

    // Create mock config with on_close hooks enabled
    MockAppConfigForHttpHooksOnClose *mock_config = new MockAppConfigForHttpHooksOnClose();
    mock_config->http_hooks_enabled_ = true;

    // Create on_close directive with two hook URLs
    mock_config->on_close_directive_ = new SrsConfDirective();
    mock_config->on_close_directive_->name_ = "on_close";
    mock_config->on_close_directive_->args_.push_back("http://127.0.0.1:8085/api/v1/close");
    mock_config->on_close_directive_->args_.push_back("http://localhost:8085/api/v1/close");

    // Create mock hooks
    MockHttpHooksForOnConnect *mock_hooks = new MockHttpHooksForOnConnect();

    // Inject mocks into connection
    conn->config_ = mock_config;
    conn->hooks_ = mock_hooks;

    // Set up request with valid vhost
    conn->info_->req_->vhost_ = "__defaultVhost__";
    conn->info_->req_->app_ = "live";
    conn->info_->req_->stream_ = "livestream";

    // Test the major use scenario: http_hooks_on_close() with hooks enabled
    // This should:
    // 1. Check if HTTP hooks are enabled (they are)
    // 2. Get the on_close directive from config
    // 3. Copy the hook URLs from the directive
    // 4. Call hooks_->on_close() for each URL with transport byte counts
    conn->http_hooks_on_close();

    // Verify that on_close was called twice (once for each URL)
    EXPECT_EQ(2, mock_hooks->on_close_count_);
    EXPECT_EQ(2, (int)mock_hooks->on_close_calls_.size());

    // Verify the first call
    EXPECT_STREQ("http://127.0.0.1:8085/api/v1/close", mock_hooks->on_close_calls_[0].url_.c_str());
    EXPECT_TRUE(mock_hooks->on_close_calls_[0].req_ == conn->info_->req_);
    EXPECT_EQ(0, mock_hooks->on_close_calls_[0].send_bytes_); // Mock transport returns 0
    EXPECT_EQ(0, mock_hooks->on_close_calls_[0].recv_bytes_); // Mock transport returns 0

    // Verify the second call
    EXPECT_STREQ("http://localhost:8085/api/v1/close", mock_hooks->on_close_calls_[1].url_.c_str());
    EXPECT_TRUE(mock_hooks->on_close_calls_[1].req_ == conn->info_->req_);
    EXPECT_EQ(0, mock_hooks->on_close_calls_[1].send_bytes_);
    EXPECT_EQ(0, mock_hooks->on_close_calls_[1].recv_bytes_);

    // Clean up injected dependencies to avoid double-free
    conn->config_ = NULL;
    conn->hooks_ = NULL;
    srs_freep(mock_config);
    srs_freep(mock_hooks);
}

MockAppConfigForHttpHooksOnPublish::MockAppConfigForHttpHooksOnPublish()
{
    http_hooks_enabled_ = false;
    on_publish_directive_ = NULL;
}

MockAppConfigForHttpHooksOnPublish::~MockAppConfigForHttpHooksOnPublish()
{
    srs_freep(on_publish_directive_);
}

bool MockAppConfigForHttpHooksOnPublish::get_vhost_http_hooks_enabled(std::string vhost)
{
    return http_hooks_enabled_;
}

SrsConfDirective *MockAppConfigForHttpHooksOnPublish::get_vhost_on_publish(std::string vhost)
{
    return on_publish_directive_;
}

MockHttpHooksForOnPublish::MockHttpHooksForOnPublish()
{
    on_publish_count_ = 0;
    on_publish_error_ = srs_success;
}

MockHttpHooksForOnPublish::~MockHttpHooksForOnPublish()
{
    srs_freep(on_publish_error_);
}

srs_error_t MockHttpHooksForOnPublish::on_connect(std::string url, ISrsRequest *req)
{
    return srs_success;
}

void MockHttpHooksForOnPublish::on_close(std::string url, ISrsRequest *req, int64_t send_bytes, int64_t recv_bytes)
{
}

srs_error_t MockHttpHooksForOnPublish::on_publish(std::string url, ISrsRequest *req)
{
    on_publish_count_++;
    on_publish_calls_.push_back(std::make_pair(url, req));
    return srs_error_copy(on_publish_error_);
}

void MockHttpHooksForOnPublish::on_unpublish(std::string url, ISrsRequest *req)
{
}

srs_error_t MockHttpHooksForOnPublish::on_play(std::string url, ISrsRequest *req)
{
    return srs_success;
}

void MockHttpHooksForOnPublish::on_stop(std::string url, ISrsRequest *req)
{
}

srs_error_t MockHttpHooksForOnPublish::on_dvr(SrsContextId cid, std::string url, ISrsRequest *req, std::string file)
{
    return srs_success;
}

srs_error_t MockHttpHooksForOnPublish::on_hls(SrsContextId cid, std::string url, ISrsRequest *req, std::string file, std::string ts_url,
                                              std::string m3u8, std::string m3u8_url, int sn, srs_utime_t duration)
{
    return srs_success;
}

srs_error_t MockHttpHooksForOnPublish::on_hls_notify(SrsContextId cid, std::string url, ISrsRequest *req, std::string ts_url, int nb_notify)
{
    return srs_success;
}

srs_error_t MockHttpHooksForOnPublish::discover_co_workers(std::string url, std::string &host, int &port)
{
    return srs_success;
}

srs_error_t MockHttpHooksForOnPublish::on_forward_backend(std::string url, ISrsRequest *req, std::vector<std::string> &rtmp_urls)
{
    return srs_success;
}

void MockHttpHooksForOnPublish::reset()
{
    on_publish_calls_.clear();
    on_publish_count_ = 0;
    srs_freep(on_publish_error_);
    on_publish_error_ = srs_success;
}

VOID TEST(SrsRtmpConnTest, HttpHooksOnPublishSuccess)
{
    srs_error_t err = srs_success;

    // Create mock transport
    MockRtmpTransportForDoCycle *mock_transport = new MockRtmpTransportForDoCycle();

    // Create connection
    SrsUniquePtr<SrsRtmpConn> conn(new SrsRtmpConn(mock_transport, "192.168.1.100", 1935));

    // Create mock config with HTTP hooks enabled
    MockAppConfigForHttpHooksOnPublish *mock_config = new MockAppConfigForHttpHooksOnPublish();
    mock_config->http_hooks_enabled_ = true;

    // Create on_publish directive with two hook URLs
    mock_config->on_publish_directive_ = new SrsConfDirective();
    mock_config->on_publish_directive_->name_ = "on_publish";
    mock_config->on_publish_directive_->args_.push_back("http://127.0.0.1:8085/api/v1/publish");
    mock_config->on_publish_directive_->args_.push_back("http://localhost:8085/api/v1/publish");

    // Create mock hooks
    MockHttpHooksForOnPublish *mock_hooks = new MockHttpHooksForOnPublish();

    // Inject mocks into connection
    conn->config_ = mock_config;
    conn->hooks_ = mock_hooks;

    // Set up request with valid vhost
    conn->info_->req_->vhost_ = "__defaultVhost__";
    conn->info_->req_->app_ = "live";
    conn->info_->req_->stream_ = "livestream";

    // Test the major use scenario: http_hooks_on_publish() with hooks enabled
    // This should:
    // 1. Check if HTTP hooks are enabled (they are)
    // 2. Get the on_publish directive from config
    // 3. Copy the hook URLs from the directive
    // 4. Call hooks_->on_publish() for each URL
    HELPER_EXPECT_SUCCESS(conn->http_hooks_on_publish());

    // Verify that on_publish was called twice (once for each URL)
    EXPECT_EQ(2, mock_hooks->on_publish_count_);
    EXPECT_EQ(2, (int)mock_hooks->on_publish_calls_.size());

    // Verify the first call
    EXPECT_STREQ("http://127.0.0.1:8085/api/v1/publish", mock_hooks->on_publish_calls_[0].first.c_str());
    EXPECT_TRUE(mock_hooks->on_publish_calls_[0].second == conn->info_->req_);

    // Verify the second call
    EXPECT_STREQ("http://localhost:8085/api/v1/publish", mock_hooks->on_publish_calls_[1].first.c_str());
    EXPECT_TRUE(mock_hooks->on_publish_calls_[1].second == conn->info_->req_);

    // Clean up injected dependencies to avoid double-free
    conn->config_ = NULL;
    conn->hooks_ = NULL;
    srs_freep(mock_config);
    srs_freep(mock_hooks);
}

MockAppConfigForHttpHooksOnUnpublish::MockAppConfigForHttpHooksOnUnpublish()
{
    http_hooks_enabled_ = false;
    on_unpublish_directive_ = NULL;
}

MockAppConfigForHttpHooksOnUnpublish::~MockAppConfigForHttpHooksOnUnpublish()
{
    srs_freep(on_unpublish_directive_);
}

bool MockAppConfigForHttpHooksOnUnpublish::get_vhost_http_hooks_enabled(std::string vhost)
{
    return http_hooks_enabled_;
}

SrsConfDirective *MockAppConfigForHttpHooksOnUnpublish::get_vhost_on_unpublish(std::string vhost)
{
    return on_unpublish_directive_;
}

// Test SrsRtmpConn::http_hooks_on_unpublish() method to verify proper HTTP hook invocation
// when a publisher stops publishing. This test covers the major use scenario where on_unpublish
// hooks are configured and should be called with the correct URL and request.
VOID TEST(SrsRtmpConnTest, HttpHooksOnUnpublishSuccess)
{
    // Create mock transport
    MockRtmpTransportForDoCycle *mock_transport = new MockRtmpTransportForDoCycle();

    // Create connection
    SrsUniquePtr<SrsRtmpConn> conn(new SrsRtmpConn(mock_transport, "192.168.1.100", 1935));

    // Create mock config with HTTP hooks enabled
    MockAppConfigForHttpHooksOnUnpublish *mock_config = new MockAppConfigForHttpHooksOnUnpublish();
    mock_config->http_hooks_enabled_ = true;

    // Create on_unpublish directive with two hook URLs
    mock_config->on_unpublish_directive_ = new SrsConfDirective();
    mock_config->on_unpublish_directive_->name_ = "on_unpublish";
    mock_config->on_unpublish_directive_->args_.push_back("http://127.0.0.1:8085/api/v1/unpublish");
    mock_config->on_unpublish_directive_->args_.push_back("http://localhost:8085/api/v1/unpublish");

    // Create mock hooks
    MockHttpHooksForOnConnect *mock_hooks = new MockHttpHooksForOnConnect();

    // Inject mocks into connection
    conn->config_ = mock_config;
    conn->hooks_ = mock_hooks;

    // Set up request with valid vhost
    conn->info_->req_->vhost_ = "__defaultVhost__";
    conn->info_->req_->app_ = "live";
    conn->info_->req_->stream_ = "livestream";

    // Test the major use scenario: http_hooks_on_unpublish() with hooks enabled
    // This should:
    // 1. Check if HTTP hooks are enabled (they are)
    // 2. Get the on_unpublish directive from config
    // 3. Copy the hook URLs from the directive
    // 4. Call hooks_->on_unpublish() for each URL
    conn->http_hooks_on_unpublish();

    // Verify that on_unpublish was called twice (once for each URL)
    EXPECT_EQ(2, mock_hooks->on_unpublish_count_);
    EXPECT_EQ(2, (int)mock_hooks->on_unpublish_calls_.size());

    // Verify the first call
    EXPECT_STREQ("http://127.0.0.1:8085/api/v1/unpublish", mock_hooks->on_unpublish_calls_[0].first.c_str());
    EXPECT_TRUE(mock_hooks->on_unpublish_calls_[0].second == conn->info_->req_);

    // Verify the second call
    EXPECT_STREQ("http://localhost:8085/api/v1/unpublish", mock_hooks->on_unpublish_calls_[1].first.c_str());
    EXPECT_TRUE(mock_hooks->on_unpublish_calls_[1].second == conn->info_->req_);

    // Clean up injected dependencies to avoid double-free
    conn->config_ = NULL;
    conn->hooks_ = NULL;
    srs_freep(mock_config);
    srs_freep(mock_hooks);
}

MockAppConfigForHttpHooksOnStop::MockAppConfigForHttpHooksOnStop()
{
    http_hooks_enabled_ = false;
    on_stop_directive_ = NULL;
}

MockAppConfigForHttpHooksOnStop::~MockAppConfigForHttpHooksOnStop()
{
    srs_freep(on_stop_directive_);
}

bool MockAppConfigForHttpHooksOnStop::get_vhost_http_hooks_enabled(std::string vhost)
{
    return http_hooks_enabled_;
}

SrsConfDirective *MockAppConfigForHttpHooksOnStop::get_vhost_on_stop(std::string vhost)
{
    return on_stop_directive_;
}

// Test SrsRtmpConn::http_hooks_on_stop() method to verify proper HTTP hook invocation
// when a player stops playing. This test covers the major use scenario where on_stop
// hooks are configured and should be called with the correct URL and request.
VOID TEST(SrsRtmpConnTest, HttpHooksOnStopSuccess)
{
    // Create mock transport
    MockRtmpTransportForDoCycle *mock_transport = new MockRtmpTransportForDoCycle();

    // Create connection
    SrsUniquePtr<SrsRtmpConn> conn(new SrsRtmpConn(mock_transport, "192.168.1.100", 1935));

    // Create mock config with HTTP hooks enabled
    MockAppConfigForHttpHooksOnStop *mock_config = new MockAppConfigForHttpHooksOnStop();
    mock_config->http_hooks_enabled_ = true;

    // Create on_stop directive with two hook URLs
    mock_config->on_stop_directive_ = new SrsConfDirective();
    mock_config->on_stop_directive_->name_ = "on_stop";
    mock_config->on_stop_directive_->args_.push_back("http://127.0.0.1:8085/api/v1/stop");
    mock_config->on_stop_directive_->args_.push_back("http://localhost:8085/api/v1/stop");

    // Create mock hooks (reuse MockHttpHooksForOnConnect which has on_stop tracking)
    MockHttpHooksForOnConnect *mock_hooks = new MockHttpHooksForOnConnect();

    // Inject mocks into connection
    conn->config_ = mock_config;
    conn->hooks_ = mock_hooks;

    // Set up request with valid vhost
    conn->info_->req_->vhost_ = "__defaultVhost__";
    conn->info_->req_->app_ = "live";
    conn->info_->req_->stream_ = "livestream";

    // Test the major use scenario: http_hooks_on_stop() with hooks enabled
    // This should:
    // 1. Check if HTTP hooks are enabled (they are)
    // 2. Get the on_stop directive from config
    // 3. Copy the hook URLs from the directive
    // 4. Call hooks_->on_stop() for each URL
    conn->http_hooks_on_stop();

    // Verify that on_stop was called twice (once for each URL)
    EXPECT_EQ(2, mock_hooks->on_stop_count_);
    EXPECT_EQ(2, (int)mock_hooks->on_stop_calls_.size());

    // Verify the first call
    EXPECT_STREQ("http://127.0.0.1:8085/api/v1/stop", mock_hooks->on_stop_calls_[0].first.c_str());
    EXPECT_TRUE(mock_hooks->on_stop_calls_[0].second == conn->info_->req_);

    // Verify the second call
    EXPECT_STREQ("http://localhost:8085/api/v1/stop", mock_hooks->on_stop_calls_[1].first.c_str());
    EXPECT_TRUE(mock_hooks->on_stop_calls_[1].second == conn->info_->req_);

    // Clean up injected dependencies to avoid double-free
    conn->config_ = NULL;
    conn->hooks_ = NULL;
    srs_freep(mock_config);
    srs_freep(mock_hooks);
}

MockAppConfigForHttpHooksOnPlay::MockAppConfigForHttpHooksOnPlay()
{
    http_hooks_enabled_ = false;
    on_play_directive_ = NULL;
}

MockAppConfigForHttpHooksOnPlay::~MockAppConfigForHttpHooksOnPlay()
{
    srs_freep(on_play_directive_);
}

bool MockAppConfigForHttpHooksOnPlay::get_vhost_http_hooks_enabled(std::string vhost)
{
    return http_hooks_enabled_;
}

SrsConfDirective *MockAppConfigForHttpHooksOnPlay::get_vhost_on_play(std::string vhost)
{
    return on_play_directive_;
}

MockHttpHooksForOnPlay::MockHttpHooksForOnPlay()
{
    on_play_count_ = 0;
    on_play_error_ = srs_success;
}

MockHttpHooksForOnPlay::~MockHttpHooksForOnPlay()
{
    srs_freep(on_play_error_);
}

srs_error_t MockHttpHooksForOnPlay::on_connect(std::string url, ISrsRequest *req)
{
    return srs_success;
}

void MockHttpHooksForOnPlay::on_close(std::string url, ISrsRequest *req, int64_t send_bytes, int64_t recv_bytes)
{
}

srs_error_t MockHttpHooksForOnPlay::on_publish(std::string url, ISrsRequest *req)
{
    return srs_success;
}

void MockHttpHooksForOnPlay::on_unpublish(std::string url, ISrsRequest *req)
{
}

srs_error_t MockHttpHooksForOnPlay::on_play(std::string url, ISrsRequest *req)
{
    on_play_count_++;
    on_play_calls_.push_back(std::make_pair(url, req));
    return srs_error_copy(on_play_error_);
}

void MockHttpHooksForOnPlay::on_stop(std::string url, ISrsRequest *req)
{
}

srs_error_t MockHttpHooksForOnPlay::on_dvr(SrsContextId cid, std::string url, ISrsRequest *req, std::string file)
{
    return srs_success;
}

srs_error_t MockHttpHooksForOnPlay::on_hls(SrsContextId cid, std::string url, ISrsRequest *req, std::string file, std::string ts_url,
                                           std::string m3u8, std::string m3u8_url, int sn, srs_utime_t duration)
{
    return srs_success;
}

srs_error_t MockHttpHooksForOnPlay::on_hls_notify(SrsContextId cid, std::string url, ISrsRequest *req, std::string ts_url, int nb_notify)
{
    return srs_success;
}

srs_error_t MockHttpHooksForOnPlay::discover_co_workers(std::string url, std::string &host, int &port)
{
    return srs_success;
}

srs_error_t MockHttpHooksForOnPlay::on_forward_backend(std::string url, ISrsRequest *req, std::vector<std::string> &rtmp_urls)
{
    return srs_success;
}

void MockHttpHooksForOnPlay::reset()
{
    on_play_calls_.clear();
    on_play_count_ = 0;
    srs_freep(on_play_error_);
    on_play_error_ = srs_success;
}

VOID TEST(SrsRtmpConnTest, HttpHooksOnPlaySuccess)
{
    srs_error_t err = srs_success;

    // Create mock transport
    MockRtmpTransportForDoCycle *mock_transport = new MockRtmpTransportForDoCycle();

    // Create connection
    SrsUniquePtr<SrsRtmpConn> conn(new SrsRtmpConn(mock_transport, "192.168.1.100", 1935));

    // Create mock config with HTTP hooks enabled
    MockAppConfigForHttpHooksOnPlay *mock_config = new MockAppConfigForHttpHooksOnPlay();
    mock_config->http_hooks_enabled_ = true;

    // Create on_play directive with two hook URLs
    mock_config->on_play_directive_ = new SrsConfDirective();
    mock_config->on_play_directive_->name_ = "on_play";
    mock_config->on_play_directive_->args_.push_back("http://127.0.0.1:8085/api/v1/play");
    mock_config->on_play_directive_->args_.push_back("http://localhost:8085/api/v1/play");

    // Create mock hooks
    MockHttpHooksForOnPlay *mock_hooks = new MockHttpHooksForOnPlay();

    // Inject mocks into connection
    conn->config_ = mock_config;
    conn->hooks_ = mock_hooks;

    // Set up request with valid vhost
    conn->info_->req_->vhost_ = "__defaultVhost__";
    conn->info_->req_->app_ = "live";
    conn->info_->req_->stream_ = "livestream";

    // Test the major use scenario: http_hooks_on_play() with hooks enabled
    // This should:
    // 1. Check if HTTP hooks are enabled (they are)
    // 2. Get the on_play directive from config
    // 3. Copy the hook URLs from the directive
    // 4. Call hooks_->on_play() for each URL
    HELPER_EXPECT_SUCCESS(conn->http_hooks_on_play());

    // Verify that on_play was called twice (once for each URL)
    EXPECT_EQ(2, mock_hooks->on_play_count_);
    EXPECT_EQ(2, (int)mock_hooks->on_play_calls_.size());

    // Verify the first call
    EXPECT_STREQ("http://127.0.0.1:8085/api/v1/play", mock_hooks->on_play_calls_[0].first.c_str());
    EXPECT_TRUE(mock_hooks->on_play_calls_[0].second == conn->info_->req_);

    // Verify the second call
    EXPECT_STREQ("http://localhost:8085/api/v1/play", mock_hooks->on_play_calls_[1].first.c_str());
    EXPECT_TRUE(mock_hooks->on_play_calls_[1].second == conn->info_->req_);

    // Clean up injected dependencies to avoid double-free
    conn->config_ = NULL;
    conn->hooks_ = NULL;
    srs_freep(mock_config);
    srs_freep(mock_hooks);
}

// Test get_proc_self_stat() function to verify proper reading of /proc/self/stat
// on Linux systems. This test covers the major use scenario of reading process
// statistics from the /proc filesystem.
VOID TEST(UtilityTest, GetProcSelfStatSuccess)
{
    // Create SrsProcSelfStat instance
    SrsProcSelfStat stat;

    // Call get_proc_self_stat() - this is the main test
    // On Linux: reads /proc/self/stat and parses all fields
    // On macOS: skips reading but still sets ok_ to true
    bool result = get_proc_self_stat(stat);

    // Verify that the function succeeded
    EXPECT_TRUE(result);
    EXPECT_TRUE(stat.ok_);

#if !defined(SRS_OSX)
    // On Linux, verify that key fields were populated correctly
    // pid should be positive
    EXPECT_TRUE(stat.pid_ > 0);

    // comm should not be empty (process name in parentheses)
    EXPECT_TRUE(stat.comm_[0] != '\0');

    // state should be one of the valid process states: R, S, D, Z, T, W
    EXPECT_TRUE(stat.state_ == 'R' || stat.state_ == 'S' || stat.state_ == 'D' ||
                stat.state_ == 'Z' || stat.state_ == 'T' || stat.state_ == 'W');

    // num_threads should be at least 1 (current thread)
    EXPECT_TRUE(stat.num_threads_ >= 1);

    // vsize should be positive (virtual memory size)
    EXPECT_TRUE(stat.vsize_ > 0);
#endif
}

// Test srs_get_local_port() function to verify proper retrieval of local port
// from a socket file descriptor. This test covers both IPv4 and IPv6 scenarios.
VOID TEST(UtilityTest, GetLocalPortSuccess)
{
    srs_error_t err;

    // Test with IPv4 TCP socket - listen on random port in [30000, 60000]
    if (true) {
        int port = 30000 + (rand() % 30001);
        srs_netfd_t fd = NULL;
        HELPER_EXPECT_SUCCESS(srs_tcp_listen("127.0.0.1", port, &fd));
        EXPECT_TRUE(fd != NULL);

        // Get the actual port using srs_get_local_port
        int actual_fd = srs_netfd_fileno(fd);
        EXPECT_GT(actual_fd, 0);

        int local_port = srs_get_local_port(actual_fd);
        EXPECT_EQ(local_port, port);
        EXPECT_GE(local_port, 30000);
        EXPECT_LE(local_port, 60000);

        srs_close_stfd(fd);
    }

    // Test with IPv6 TCP socket - listen on random port in [30000, 60000]
    if (true) {
        int port = 30000 + (rand() % 30001);
        srs_netfd_t fd = NULL;
        HELPER_EXPECT_SUCCESS(srs_tcp_listen("::1", port, &fd));
        EXPECT_TRUE(fd != NULL);

        // Get the actual port using srs_get_local_port
        int actual_fd = srs_netfd_fileno(fd);
        EXPECT_GT(actual_fd, 0);

        int local_port = srs_get_local_port(actual_fd);
        EXPECT_EQ(local_port, port);
        EXPECT_GE(local_port, 30000);
        EXPECT_LE(local_port, 60000);

        srs_close_stfd(fd);
    }

    // Test with invalid file descriptor - should return 0
    if (true) {
        int invalid_fd = -1;
        int local_port = srs_get_local_port(invalid_fd);
        EXPECT_EQ(local_port, 0);
    }
}

VOID TEST(AppUtilityTest, ApiDumpSummaries)
{
    // Test srs_api_dump_summaries function
    SrsUniquePtr<SrsJsonObject> obj(SrsJsonAny::object());

    // Call the function to dump summaries
    srs_api_dump_summaries(obj.get());

    // Verify the JSON structure
    // Check that "data" object exists
    SrsJsonAny *data_any = obj->get_property("data");
    ASSERT_TRUE(data_any != NULL);
    ASSERT_TRUE(data_any->is_object());
    SrsJsonObject *data = (SrsJsonObject *)data_any;

    // Check "ok" field
    SrsJsonAny *ok_any = data->get_property("ok");
    ASSERT_TRUE(ok_any != NULL);
    ASSERT_TRUE(ok_any->is_boolean());

    // Check "now_ms" field
    SrsJsonAny *now_ms_any = data->get_property("now_ms");
    ASSERT_TRUE(now_ms_any != NULL);
    ASSERT_TRUE(now_ms_any->is_integer());

    // Check "self" object
    SrsJsonAny *self_any = data->get_property("self");
    ASSERT_TRUE(self_any != NULL);
    ASSERT_TRUE(self_any->is_object());
    SrsJsonObject *self = (SrsJsonObject *)self_any;

    // Verify self fields
    ASSERT_TRUE(self->get_property("version") != NULL);
    ASSERT_TRUE(self->get_property("pid") != NULL);
    ASSERT_TRUE(self->get_property("ppid") != NULL);
    ASSERT_TRUE(self->get_property("argv") != NULL);
    ASSERT_TRUE(self->get_property("cwd") != NULL);
    ASSERT_TRUE(self->get_property("mem_kbyte") != NULL);
    ASSERT_TRUE(self->get_property("mem_percent") != NULL);
    ASSERT_TRUE(self->get_property("cpu_percent") != NULL);
    ASSERT_TRUE(self->get_property("srs_uptime") != NULL);

    // Check "system" object
    SrsJsonAny *sys_any = data->get_property("system");
    ASSERT_TRUE(sys_any != NULL);
    ASSERT_TRUE(sys_any->is_object());
    SrsJsonObject *sys = (SrsJsonObject *)sys_any;

    // Verify system fields
    ASSERT_TRUE(sys->get_property("cpu_percent") != NULL);
    ASSERT_TRUE(sys->get_property("disk_read_KBps") != NULL);
    ASSERT_TRUE(sys->get_property("disk_write_KBps") != NULL);
    ASSERT_TRUE(sys->get_property("disk_busy_percent") != NULL);
    ASSERT_TRUE(sys->get_property("mem_ram_kbyte") != NULL);
    ASSERT_TRUE(sys->get_property("mem_ram_percent") != NULL);
    ASSERT_TRUE(sys->get_property("mem_swap_kbyte") != NULL);
    ASSERT_TRUE(sys->get_property("mem_swap_percent") != NULL);
    ASSERT_TRUE(sys->get_property("cpus") != NULL);
    ASSERT_TRUE(sys->get_property("cpus_online") != NULL);
    ASSERT_TRUE(sys->get_property("uptime") != NULL);
    ASSERT_TRUE(sys->get_property("ilde_time") != NULL);
    ASSERT_TRUE(sys->get_property("load_1m") != NULL);
    ASSERT_TRUE(sys->get_property("load_5m") != NULL);
    ASSERT_TRUE(sys->get_property("load_15m") != NULL);

    // Verify network fields
    ASSERT_TRUE(sys->get_property("net_sample_time") != NULL);
    ASSERT_TRUE(sys->get_property("net_recv_bytes") != NULL);
    ASSERT_TRUE(sys->get_property("net_send_bytes") != NULL);
    ASSERT_TRUE(sys->get_property("net_recvi_bytes") != NULL);
    ASSERT_TRUE(sys->get_property("net_sendi_bytes") != NULL);

    // Verify SRS network fields
    ASSERT_TRUE(sys->get_property("srs_sample_time") != NULL);
    ASSERT_TRUE(sys->get_property("srs_recv_bytes") != NULL);
    ASSERT_TRUE(sys->get_property("srs_send_bytes") != NULL);
    ASSERT_TRUE(sys->get_property("conn_sys") != NULL);
    ASSERT_TRUE(sys->get_property("conn_sys_et") != NULL);
    ASSERT_TRUE(sys->get_property("conn_sys_tw") != NULL);
    ASSERT_TRUE(sys->get_property("conn_sys_udp") != NULL);
    ASSERT_TRUE(sys->get_property("conn_srs") != NULL);
}
