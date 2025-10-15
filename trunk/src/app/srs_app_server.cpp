//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_app_server.hpp>

#include <algorithm>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#if !defined(SRS_OSX) && !defined(SRS_CYGWIN64)
#include <sys/inotify.h>
#endif
using namespace std;

#include <srs_app_async_call.hpp>
#include <srs_app_caster_flv.hpp>
#include <srs_app_circuit_breaker.hpp>
#include <srs_app_config.hpp>
#include <srs_app_coworkers.hpp>
#include <srs_app_heartbeat.hpp>
#include <srs_app_http_api.hpp>
#include <srs_app_http_conn.hpp>
#include <srs_app_http_hooks.hpp>
#include <srs_app_ingest.hpp>
#include <srs_app_latest_version.hpp>
#include <srs_app_log.hpp>
#include <srs_app_mpegts_udp.hpp>
#include <srs_app_reload.hpp>
#include <srs_app_rtc_api.hpp>
#include <srs_app_rtc_dtls.hpp>
#include <srs_app_rtc_network.hpp>
#include <srs_app_rtc_server.hpp>
#include <srs_app_rtc_source.hpp>
#include <srs_app_rtmp_conn.hpp>
#include <srs_app_rtmp_source.hpp>
#include <srs_app_statistic.hpp>
#include <srs_app_stream_token.hpp>
#include <srs_app_utility.hpp>
#include <srs_kernel_consts.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_hourglass.hpp>
#include <srs_kernel_kbps.hpp>
#include <srs_kernel_log.hpp>
#include <srs_kernel_pithy_print.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_conn.hpp>
#include <srs_protocol_log.hpp>
#include <srs_protocol_rtc_stun.hpp>
#include <srs_protocol_sdp.hpp>
#ifdef SRS_GB28181
#include <srs_app_gb28181.hpp>
#endif
#include <srs_app_srt_conn.hpp>
#include <srs_app_srt_server.hpp>
#include <srs_app_srt_source.hpp>
#ifdef SRS_RTSP
#include <srs_app_rtsp_conn.hpp>
#include <srs_app_rtsp_source.hpp>
#endif
#include <srs_app_factory.hpp>

SrsServer *_srs_server = NULL;

SrsAsyncCallWorker *_srs_dvr_async = NULL;

extern SrsStageManager *_srs_stages;

extern srs_error_t _srs_reload_err;
extern SrsReloadState _srs_reload_state;
extern std::string _srs_reload_id;

// External WebRTC global variables
extern SrsRtcBlackhole *_srs_blackhole;
extern SrsDtlsCertificate *_srs_rtc_dtls_certificate;

bool _srs_global_initialized = false;
srs_error_t srs_global_initialize()
{
    srs_error_t err = srs_success;

    // Initialize the global kbps statistics variables
    if ((err = srs_global_kbps_initialize()) != srs_success) {
        return srs_error_wrap(err, "global kbps initialize");
    }

    // Initialize ST, which depends on pps cids.
    if ((err = srs_st_init()) != srs_success) {
        return srs_error_wrap(err, "initialize st failed");
    }

    // Initialize global shared timer, which depends on ST
    _srs_shared_timer = new SrsSharedTimer();
    if ((err = _srs_shared_timer->initialize()) != srs_success) {
        return srs_error_wrap(err, "initialize shared timer");
    }

    // The global objects which depends on ST.
    // Initialize _srs_stages first as it's needed by SrsServer constructor
    _srs_stages = new SrsStageManager();
    _srs_sources = new SrsLiveSourceManager();
    _srs_circuit_breaker = new SrsCircuitBreaker();

    // Initialize global statistic instance before _srs_hooks, as SrsHttpHooks depends on it.
    _srs_stat = new SrsStatistic();

    _srs_hooks = new SrsHttpHooks();

    _srs_srt_sources = new SrsSrtSourceManager();

    _srs_rtc_sources = new SrsRtcSourceManager();
    _srs_blackhole = new SrsRtcBlackhole();

    // Initialize stream publish token manager
    _srs_stream_publish_tokens = new SrsStreamPublishTokenManager();

    _srs_conn_manager = new SrsResourceManager("RTC", true);
    _srs_rtc_dtls_certificate = new SrsDtlsCertificate();
#ifdef SRS_RTSP
    _srs_rtsp_sources = new SrsRtspSourceManager();
    _srs_rtsp_manager = new SrsResourceManager("RTSP", true);
#endif
#ifdef SRS_GB28181
    _srs_gb_manager = new SrsResourceManager("GB", true);
#endif

    // Create global async worker for DVR.
    _srs_dvr_async = new SrsAsyncCallWorker();

    _srs_reload_err = srs_success;
    _srs_reload_state = SrsReloadStateInit;
    SrsRand rand;
    _srs_reload_id = rand.gen_str(7);

    // Global initialization done
    _srs_global_initialized = true;

    return err;
}

ISrsSignalHandler::ISrsSignalHandler()
{
}

ISrsSignalHandler::~ISrsSignalHandler()
{
}

ISrsApiServerOwner::ISrsApiServerOwner()
{
}

ISrsApiServerOwner::~ISrsApiServerOwner()
{
}

ISrsRtcApiServer::ISrsRtcApiServer()
{
}

ISrsRtcApiServer::~ISrsRtcApiServer()
{
}

SrsServer::SrsServer()
{
    signal_reload_ = false;
    signal_persistence_config_ = false;
    signal_gmc_stop_ = false;
    signal_fast_quit_ = false;
    signal_gracefully_quit_ = false;

    pid_file_locker_ = new SrsPidFileLocker();

    signal_manager_ = new SrsSignalManager(this);
    latest_version_ = new SrsLatestVersion();
    ppid_ = ::getppid();

    http_api_mux_ = new SrsHttpServeMux();

    rtmp_listener_ = new SrsMultipleTcpListeners(this);
    rtmps_listener_ = new SrsMultipleTcpListeners(this);
    api_listener_ = new SrsMultipleTcpListeners(this);
    apis_listener_ = new SrsMultipleTcpListeners(this);
    http_listener_ = new SrsMultipleTcpListeners(this);
    https_listener_ = new SrsMultipleTcpListeners(this);
    webrtc_listener_ = new SrsMultipleTcpListeners(this);
#ifdef SRS_RTSP
    rtsp_listener_ = new SrsMultipleTcpListeners(this);
#endif
    stream_caster_flv_listener_ = new SrsHttpFlvListener();
    stream_caster_mpegts_ = new SrsUdpCasterListener();
    exporter_listener_ = new SrsTcpListener(this);
#ifdef SRS_GB28181
    stream_caster_gb28181_ = new SrsGbListener();
#endif

    http_server_ = new SrsHttpServer();
    reuse_api_over_server_ = false;
    reuse_rtc_over_server_ = false;

    http_heartbeat_ = new SrsHttpHeartbeat();
    ingester_ = new SrsIngester();
    timer_ = NULL;

    // Initialize WebRTC components
    rtc_session_manager_ = new SrsRtcSessionManager();

    config_ = _srs_config;
    live_sources_ = _srs_sources;
    conn_manager_ = _srs_conn_manager;
    rtc_dtls_certificate_ = _srs_rtc_dtls_certificate;
    dvr_async_ = _srs_dvr_async;
    circuit_breaker_ = _srs_circuit_breaker;
    srt_sources_ = _srs_srt_sources;
    rtc_sources_ = _srs_rtc_sources;
#ifdef SRS_RTSP
    rtsp_sources_ = _srs_rtsp_sources;
#endif
#ifdef SRS_GB28181
    gb_manager_ = _srs_gb_manager;
#endif
    log_ = _srs_log;
    stat_ = _srs_stat;
    app_factory_ = _srs_app_factory;
}

SrsServer::~SrsServer()
{
    srs_freep(timer_);

    dispose();

    // If api reuse the same port of server, they're the same object.
    if (!reuse_api_over_server_) {
        srs_freep(http_api_mux_);
    }
    srs_freep(http_server_);

    srs_freep(http_heartbeat_);
    srs_freep(ingester_);

    srs_freep(pid_file_locker_);

    srs_freep(signal_manager_);
    srs_freep(latest_version_);
    srs_freep(rtmp_listener_);
    srs_freep(rtmps_listener_);
    srs_freep(api_listener_);
    srs_freep(apis_listener_);
    srs_freep(http_listener_);
    srs_freep(https_listener_);
    srs_freep(webrtc_listener_);
#ifdef SRS_RTSP
    srs_freep(rtsp_listener_);
#endif
    srs_freep(stream_caster_flv_listener_);
    srs_freep(stream_caster_mpegts_);
    srs_freep(exporter_listener_);
#ifdef SRS_GB28181
    srs_freep(stream_caster_gb28181_);
#endif
    close_srt_listeners();

    // Cleanup WebRTC components
    if (true) {
        std::vector<SrsUdpMuxListener *>::iterator it;
        for (it = rtc_listeners_.begin(); it != rtc_listeners_.end(); ++it) {
            SrsUdpMuxListener *listener = *it;
            srs_freep(listener);
        }
        rtc_listeners_.clear();
    }

    srs_freep(rtc_session_manager_);

    config_ = NULL;
    live_sources_ = NULL;
    conn_manager_ = NULL;
    rtc_dtls_certificate_ = NULL;
    dvr_async_ = NULL;
    circuit_breaker_ = NULL;
    srt_sources_ = NULL;
    rtc_sources_ = NULL;
#ifdef SRS_RTSP
    rtsp_sources_ = NULL;
#endif
#ifdef SRS_GB28181
    gb_manager_ = NULL;
#endif
    log_ = NULL;
    stat_ = NULL;
    app_factory_ = NULL;
}

void SrsServer::dispose()
{
    config_->unsubscribe(this);

    // Destroy all listeners.
    rtmp_listener_->close();
    rtmps_listener_->close();
    api_listener_->close();
    apis_listener_->close();
    http_listener_->close();
    https_listener_->close();
    webrtc_listener_->close();
#ifdef SRS_RTSP
    rtsp_listener_->close();
#endif
    stream_caster_flv_listener_->close();
    stream_caster_mpegts_->close();
    exporter_listener_->close();
#ifdef SRS_GB28181
    stream_caster_gb28181_->close();
#endif
    close_srt_listeners();

    // Fast stop to notify FFMPEG to quit, wait for a while then fast kill.
    ingester_->dispose();

    // dispose the source for hls and dvr.
    live_sources_->dispose();

    // @remark don't dispose all connections, for too slow.
}

void SrsServer::gracefully_dispose()
{
    config_->unsubscribe(this);

    // Always wait for a while to start.
    srs_usleep(config_->get_grace_start_wait());
    srs_trace("start wait for %dms", srsu2msi(config_->get_grace_start_wait()));

    // Destroy all listeners.
    rtmp_listener_->close();
    rtmps_listener_->close();
    api_listener_->close();
    apis_listener_->close();
    http_listener_->close();
    https_listener_->close();
    webrtc_listener_->close();
#ifdef SRS_RTSP
    rtsp_listener_->close();
#endif
    stream_caster_flv_listener_->close();
    stream_caster_mpegts_->close();
    exporter_listener_->close();
#ifdef SRS_GB28181
    stream_caster_gb28181_->close();
#endif
    close_srt_listeners();
    srs_trace("listeners closed");

    // Fast stop to notify FFMPEG to quit, wait for a while then fast kill.
    ingester_->stop();
    srs_trace("ingesters stopped");

    // Wait for connections to quit.
    // While gracefully quiting, user can requires SRS to fast quit.
    int wait_step = 1;
    while (!conn_manager_->empty() && !signal_fast_quit_) {
        for (int i = 0; i < wait_step && !conn_manager_->empty() && !signal_fast_quit_; i++) {
            srs_usleep(1000 * SRS_UTIME_MILLISECONDS);
        }

        wait_step = (wait_step * 2) % 33;
        srs_trace("wait for %d conns to quit", (int)conn_manager_->size());
    }

    // dispose the source for hls and dvr.
    live_sources_->dispose();
    srs_trace("source disposed");

    srs_usleep(config_->get_grace_final_wait());
    srs_trace("final wait for %dms", srsu2msi(config_->get_grace_final_wait()));
}

ISrsCommonHttpHandler *SrsServer::api_server()
{
    return http_api_mux_;
}

srs_error_t SrsServer::initialize()
{
    srs_error_t err = srs_success;

    srs_trace("SRS server initialized in single thread mode");

    // Initialize the server.
    if ((err = pid_file_locker_->acquire()) != srs_success) {
        return srs_error_wrap(err, "init server");
    }

    if ((err = srs_srt_log_initialize()) != srs_success) {
        return srs_error_wrap(err, "srt log initialize");
    }

    _srt_eventloop = new SrsSrtEventLoop();

    if ((err = _srt_eventloop->initialize()) != srs_success) {
        return srs_error_wrap(err, "srt poller initialize");
    }

    if ((err = _srt_eventloop->start()) != srs_success) {
        return srs_error_wrap(err, "srt poller start");
    }

    // Initialize WebRTC DTLS certificate
    if ((err = rtc_dtls_certificate_->initialize()) != srs_success) {
        return srs_error_wrap(err, "rtc dtls certificate initialize");
    }

    // Start the DVR async call.
    if ((err = dvr_async_->start()) != srs_success) {
        return srs_error_wrap(err, "dvr async");
    }

    // for the main objects(server, config, log, context),
    // never subscribe handler in constructor,
    // instead, subscribe handler in initialize method.
    srs_assert(config_);
    config_->subscribe(this);

    bool stream = config_->get_http_stream_enabled();
    vector<string> http_listens = config_->get_http_stream_listens();
    vector<string> https_listens = config_->get_https_stream_listens();

    bool rtc = config_->get_rtc_server_enabled();
    bool rtc_tcp = config_->get_rtc_server_tcp_enabled();
    vector<string> rtc_listens = config_->get_rtc_server_tcp_listens();
    // If enabled and listen is the same value, resue port for WebRTC over TCP.
    if (stream && rtc && rtc_tcp && srs_strings_equal(http_listens, rtc_listens)) {
        srs_trace("WebRTC tcp=%s reuses http=%s server", srs_strings_join(rtc_listens, ",").c_str(), srs_strings_join(http_listens, ",").c_str());
        reuse_rtc_over_server_ = true;
    }
    if (stream && rtc && rtc_tcp && srs_strings_equal(https_listens, rtc_listens)) {
        srs_trace("WebRTC tcp=%s reuses https=%s server", srs_strings_join(rtc_listens, ",").c_str(), srs_strings_join(https_listens, ",").c_str());
        reuse_rtc_over_server_ = true;
    }

    // If enabled and the listen is the same value, reuse port.
    bool api = config_->get_http_api_enabled();
    vector<string> api_listens = config_->get_http_api_listens();
    vector<string> apis_listens = config_->get_https_api_listens();
    if (stream && api && srs_strings_equal(api_listens, http_listens) && srs_strings_equal(apis_listens, https_listens)) {
        srs_trace("API reuses http=%s and https=%s server", srs_strings_join(http_listens, ",").c_str(), srs_strings_join(https_listens, ",").c_str());
        reuse_api_over_server_ = true;
    }

    // Only init HTTP API when not reusing HTTP server.
    if (!reuse_api_over_server_) {
        SrsHttpServeMux *api = dynamic_cast<SrsHttpServeMux *>(http_api_mux_);
        srs_assert(api);

        if ((err = api->initialize()) != srs_success) {
            return srs_error_wrap(err, "http api initialize");
        }
    } else {
        srs_freep(http_api_mux_);
        http_api_mux_ = http_server_;
    }

    if ((err = http_server_->initialize()) != srs_success) {
        return srs_error_wrap(err, "http server initialize");
    }

    // Initialize the black hole.
    if ((err = _srs_blackhole->initialize()) != srs_success) {
        return srs_error_wrap(err, "black hole");
    }

    // Start WebRTC async worker
    if ((err = rtc_session_manager_->initialize()) != srs_success) {
        return srs_error_wrap(err, "rtc session manager");
    }

    return err;
}

srs_error_t SrsServer::run()
{
    srs_error_t err = srs_success;

    // Circuit breaker to protect server, which depends on server.
    if ((err = circuit_breaker_->initialize()) != srs_success) {
        return srs_error_wrap(err, "init circuit breaker");
    }

    // Initialize the whole system, set hooks to handle server level events.
    if ((err = initialize_st()) != srs_success) {
        return srs_error_wrap(err, "initialize st");
    }

    if ((err = initialize_signal()) != srs_success) {
        return srs_error_wrap(err, "initialize signal");
    }

    if ((err = listen()) != srs_success) {
        return srs_error_wrap(err, "listen");
    }

    if ((err = register_signal()) != srs_success) {
        return srs_error_wrap(err, "register signal");
    }

    if ((err = http_handle()) != srs_success) {
        return srs_error_wrap(err, "http handle");
    }

    if ((err = ingest()) != srs_success) {
        return srs_error_wrap(err, "ingest");
    }

    if ((err = live_sources_->initialize()) != srs_success) {
        return srs_error_wrap(err, "live sources");
    }

    if ((err = srt_sources_->initialize()) != srs_success) {
        return srs_error_wrap(err, "srt sources");
    }

    if ((err = rtc_sources_->initialize()) != srs_success) {
        return srs_error_wrap(err, "rtc sources");
    }

#ifdef SRS_RTSP
    if ((err = rtsp_sources_->initialize()) != srs_success) {
        return srs_error_wrap(err, "rtsp sources");
    }
#endif

    if ((err = setup_ticks()) != srs_success) {
        return srs_error_wrap(err, "tick");
    }

#ifdef SRS_GB28181
    if ((err = gb_manager_->start()) != srs_success) {
        return srs_error_wrap(err, "start manager");
    }
#endif

    return cycle();
}

srs_error_t SrsServer::initialize_st()
{
    srs_error_t err = srs_success;

    // check asprocess.
    bool asprocess = config_->get_asprocess();
    if (asprocess && ppid_ == 1) {
        return srs_error_new(ERROR_SYSTEM_ASSERT_FAILED, "ppid=%d illegal for asprocess", ppid_);
    }

    srs_trace("server main cid=%s, pid=%d, ppid=%d, asprocess=%d",
              _srs_context->get_id().c_str(), ::getpid(), ppid_, asprocess);

    return err;
}

srs_error_t SrsServer::initialize_signal()
{
    srs_error_t err = srs_success;

    if ((err = signal_manager_->initialize()) != srs_success) {
        return srs_error_wrap(err, "init signal manager");
    }

    // Start the version query coroutine.
    if ((err = latest_version_->start()) != srs_success) {
        return srs_error_wrap(err, "start version query");
    }

    return err;
}

srs_error_t SrsServer::listen()
{
    srs_error_t err = srs_success;

    // Create RTMP listeners.
    rtmp_listener_->add(config_->get_listens())->set_label("RTMP");
    if ((err = rtmp_listener_->listen()) != srs_success) {
        return srs_error_wrap(err, "rtmp listen");
    }

    // Create RTMPS listeners.
    if (config_->get_rtmps_enabled()) {
        rtmps_listener_->add(config_->get_rtmps_listen())->set_label("RTMPS");
        if ((err = rtmps_listener_->listen()) != srs_success) {
            return srs_error_wrap(err, "rtmps listen");
        }
    }

    // Create HTTP API listener.
    if (config_->get_http_api_enabled()) {
        if (reuse_api_over_server_) {
            vector<string> listens = config_->get_http_stream_listens();
            srs_trace("HTTP-API: Reuse listen to http server %s", srs_strings_join(listens, ",").c_str());
        } else {
            api_listener_->add(config_->get_http_api_listens())->set_label("HTTP-API");
            if ((err = api_listener_->listen()) != srs_success) {
                return srs_error_wrap(err, "http api listen");
            }
        }
    }

    // Create HTTPS API listener.
    if (config_->get_https_api_enabled()) {
        if (reuse_api_over_server_) {
            vector<string> listens = config_->get_https_stream_listens();
            srs_trace("HTTPS-API: Reuse listen to http server %s", srs_strings_join(listens, ",").c_str());
        } else {
            apis_listener_->add(config_->get_https_api_listens())->set_label("HTTPS-API");
            if ((err = apis_listener_->listen()) != srs_success) {
                return srs_error_wrap(err, "https api listen");
            }
        }
    }

    // Create HTTP server listener.
    if (config_->get_http_stream_enabled()) {
        http_listener_->add(config_->get_http_stream_listens())->set_label("HTTP-Server");
        if ((err = http_listener_->listen()) != srs_success) {
            return srs_error_wrap(err, "http server listen");
        }
    }

    // Create HTTPS server listener.
    if (config_->get_https_stream_enabled()) {
        https_listener_->add(config_->get_https_stream_listens())->set_label("HTTPS-Server");
        if ((err = https_listener_->listen()) != srs_success) {
            return srs_error_wrap(err, "https server listen");
        }
    }

    // Start WebRTC over TCP listener.
    string protocol = config_->get_rtc_server_protocol();
    if (!reuse_rtc_over_server_ && protocol != "udp" && config_->get_rtc_server_tcp_enabled()) {
        webrtc_listener_->add(config_->get_rtc_server_tcp_listens())->set_label("WebRTC");
        if ((err = webrtc_listener_->listen()) != srs_success) {
            return srs_error_wrap(err, "webrtc tcp listen");
        }
    }

#ifdef SRS_RTSP
    // Start RTSP listener. RTC is a critical dependency.
    if (config_->get_rtsp_server_enabled()) {
        rtsp_listener_->add(config_->get_rtsp_server_listens())->set_label("RTSP");
        if ((err = rtsp_listener_->listen()) != srs_success) {
            return srs_error_wrap(err, "rtsp listen");
        }
    }
#endif

    // Start all listeners for stream caster.
    std::vector<SrsConfDirective *> confs = config_->get_stream_casters();
    for (vector<SrsConfDirective *>::iterator it = confs.begin(); it != confs.end(); ++it) {
        SrsConfDirective *conf = *it;
        if (!config_->get_stream_caster_enabled(conf)) {
            continue;
        }

        ISrsListener *listener = NULL;
        std::string caster = config_->get_stream_caster_engine(conf);
        if (srs_stream_caster_is_udp(caster)) {
            listener = stream_caster_mpegts_;
            if ((err = stream_caster_mpegts_->initialize(conf)) != srs_success) {
                return srs_error_wrap(err, "initialize");
            }
        } else if (srs_stream_caster_is_flv(caster)) {
            listener = stream_caster_flv_listener_;
            if ((err = stream_caster_flv_listener_->initialize(conf)) != srs_success) {
                return srs_error_wrap(err, "initialize");
            }
        } else if (srs_stream_caster_is_gb28181(caster)) {
#ifdef SRS_GB28181
            listener = stream_caster_gb28181_;
            if ((err = stream_caster_gb28181_->initialize(conf)) != srs_success) {
                return srs_error_wrap(err, "initialize");
            }
#else
            return srs_error_new(ERROR_STREAM_CASTER_ENGINE, "Please enable GB by: ./configure --gb28181=on");
#endif
        } else {
            return srs_error_new(ERROR_STREAM_CASTER_ENGINE, "invalid caster %s", caster.c_str());
        }

        srs_assert(listener);
        if ((err = listener->listen()) != srs_success) {
            return srs_error_wrap(err, "listen");
        }
    }

    // Create exporter server listener.
    if (config_->get_exporter_enabled()) {
        exporter_listener_->set_endpoint(config_->get_exporter_listen());
        exporter_listener_->set_label("Exporter-Server");
        if ((err = exporter_listener_->listen()) != srs_success) {
            return srs_error_wrap(err, "exporter server listen");
        }
    }

    // Listen MPEG-TS over SRT.
    if ((err = listen_srt_mpegts()) != srs_success) {
        return srs_error_wrap(err, "srt mpegts listen");
    }

    // Listen WebRTC UDP.
    if ((err = listen_rtc_udp()) != srs_success) {
        return srs_error_wrap(err, "rtc udp listen");
    }

    if ((err = conn_manager_->start()) != srs_success) {
        return srs_error_wrap(err, "connection manager");
    }

    return err;
}

srs_error_t SrsServer::register_signal()
{
    srs_error_t err = srs_success;

    if ((err = signal_manager_->start()) != srs_success) {
        return srs_error_wrap(err, "signal manager start");
    }

    return err;
}

srs_error_t SrsServer::http_handle()
{
    srs_error_t err = srs_success;

    // Ignore / and /api/v1/versions for already handled by HTTP server.
    if (!reuse_api_over_server_) {
        if ((err = http_api_mux_->handle("/", new SrsGoApiRoot())) != srs_success) {
            return srs_error_wrap(err, "handle /");
        }
        if ((err = http_api_mux_->handle("/api/v1/versions", new SrsGoApiVersion())) != srs_success) {
            return srs_error_wrap(err, "handle versions");
        }
    }

    if ((err = http_api_mux_->handle("/api/", new SrsGoApiApi())) != srs_success) {
        return srs_error_wrap(err, "handle api");
    }
    if ((err = http_api_mux_->handle("/api/v1/", new SrsGoApiV1())) != srs_success) {
        return srs_error_wrap(err, "handle v1");
    }
    if ((err = http_api_mux_->handle("/api/v1/summaries", new SrsGoApiSummaries())) != srs_success) {
        return srs_error_wrap(err, "handle summaries");
    }
    if ((err = http_api_mux_->handle("/api/v1/rusages", new SrsGoApiRusages())) != srs_success) {
        return srs_error_wrap(err, "handle rusages");
    }
    if ((err = http_api_mux_->handle("/api/v1/self_proc_stats", new SrsGoApiSelfProcStats())) != srs_success) {
        return srs_error_wrap(err, "handle self proc stats");
    }
    if ((err = http_api_mux_->handle("/api/v1/system_proc_stats", new SrsGoApiSystemProcStats())) != srs_success) {
        return srs_error_wrap(err, "handle system proc stats");
    }
    if ((err = http_api_mux_->handle("/api/v1/meminfos", new SrsGoApiMemInfos())) != srs_success) {
        return srs_error_wrap(err, "handle meminfos");
    }
    if ((err = http_api_mux_->handle("/api/v1/authors", new SrsGoApiAuthors())) != srs_success) {
        return srs_error_wrap(err, "handle authors");
    }
    if ((err = http_api_mux_->handle("/api/v1/features", new SrsGoApiFeatures())) != srs_success) {
        return srs_error_wrap(err, "handle features");
    }
    if ((err = http_api_mux_->handle("/api/v1/vhosts/", new SrsGoApiVhosts())) != srs_success) {
        return srs_error_wrap(err, "handle vhosts");
    }
    if ((err = http_api_mux_->handle("/api/v1/streams/", new SrsGoApiStreams())) != srs_success) {
        return srs_error_wrap(err, "handle streams");
    }
    if ((err = http_api_mux_->handle("/api/v1/clients/", new SrsGoApiClients())) != srs_success) {
        return srs_error_wrap(err, "handle clients");
    }

    SrsGoApiRaw *raw_api = new SrsGoApiRaw(this);
    raw_api->assemble();
    if ((err = http_api_mux_->handle("/api/v1/raw", raw_api)) != srs_success) {
        return srs_error_wrap(err, "handle raw");
    }

    if ((err = http_api_mux_->handle("/api/v1/clusters", new SrsGoApiClusters())) != srs_success) {
        return srs_error_wrap(err, "handle clusters");
    }

    // test the request info.
    if ((err = http_api_mux_->handle("/api/v1/tests/requests", new SrsGoApiRequests())) != srs_success) {
        return srs_error_wrap(err, "handle tests requests");
    }
    // test the error code response.
    if ((err = http_api_mux_->handle("/api/v1/tests/errors", new SrsGoApiError())) != srs_success) {
        return srs_error_wrap(err, "handle tests errors");
    }
    // test the redirect mechenism.
    if ((err = http_api_mux_->handle("/api/v1/tests/redirects", new SrsHttpRedirectHandler("/api/v1/tests/errors", SRS_CONSTS_HTTP_MovedPermanently))) != srs_success) {
        return srs_error_wrap(err, "handle tests redirects");
    }
    // test the http vhost.
    if ((err = http_api_mux_->handle("error.srs.com/api/v1/tests/errors", new SrsGoApiError())) != srs_success) {
        return srs_error_wrap(err, "handle tests errors for error.srs.com");
    }

#ifdef SRS_GPERF
    // The test api for get tcmalloc stats.
    // @see Memory Introspection in https://gperftools.github.io/gperftools/tcmalloc.html
    if ((err = http_api_mux_->handle("/api/v1/tcmalloc", new SrsGoApiTcmalloc())) != srs_success) {
        return srs_error_wrap(err, "handle tcmalloc errors");
    }
#endif

#ifdef SRS_VALGRIND
    // The test api for valgrind. See VALGRIND_DO_LEAK_CHECK in https://valgrind.org/docs/manual/mc-manual.html
    if ((err = http_api_mux_->handle("/api/v1/valgrind", new SrsGoApiValgrind())) != srs_success) {
        return srs_error_wrap(err, "handle valgrind errors");
    }
#endif

#ifdef SRS_SIGNAL_API
    // Simulate the signal by HTTP API, for debug signal issues in CLion.
    if ((err = http_api_mux_->handle("/api/v1/signal", new SrsGoApiSignal())) != srs_success) {
        return srs_error_wrap(err, "handle signal errors");
    }
#endif

    // metrics by prometheus
    SrsGoApiMetrics *metrics = new SrsGoApiMetrics();
    metrics->assemble();
    if ((err = http_api_mux_->handle("/metrics", metrics)) != srs_success) {
        return srs_error_wrap(err, "handle tests errors");
    }

    // TODO: FIXME: for console.
    // TODO: FIXME: support reload.
    std::string dir = config_->get_http_stream_dir() + "/console";
    if ((err = http_api_mux_->handle("/console/", new SrsHttpFileServer(dir))) != srs_success) {
        return srs_error_wrap(err, "handle console at %s", dir.c_str());
    }
    srs_trace("http: api mount /console to %s", dir.c_str());

    // WebRTC API endpoints
    if ((err = listen_rtc_api()) != srs_success) {
        return srs_error_wrap(err, "rtc api");
    }

    return err;
}

srs_error_t SrsServer::ingest()
{
    srs_error_t err = srs_success;

    if ((err = ingester_->start()) != srs_success) {
        return srs_error_wrap(err, "ingest start");
    }

    return err;
}

void SrsServer::stop()
{
#ifdef SRS_GPERF_MC
    dispose();

    // remark, for gmc, never invoke the exit().
    srs_warn("sleep a long time for system st-threads to cleanup.");
    srs_usleep(3 * 1000 * 1000);
    srs_warn("system quit");

    // For GCM, cleanup done.
    return;
#endif

    // quit normally.
    srs_warn("main cycle terminated, system quit normally.");

    // fast quit, do some essential cleanup.
    if (signal_fast_quit_) {
        dispose(); // TODO: FIXME: Rename to essential_dispose.
        srs_trace("srs disposed");
    }

    // gracefully quit, do carefully cleanup.
    if (signal_gracefully_quit_) {
        gracefully_dispose();
        srs_trace("srs gracefully quit");
    }

    // This is the last line log of SRS.
    srs_trace("srs terminated");
}

srs_error_t SrsServer::cycle()
{
    srs_error_t err = srs_success;

    // Start the inotify auto reload by watching config file.
    SrsInotifyWorker inotify(this);
    if ((err = inotify.start()) != srs_success) {
        return srs_error_wrap(err, "start inotify");
    }

    // Do server main cycle.
    if ((err = do_cycle()) != srs_success) {
        srs_error("server err %s", srs_error_desc(err).c_str());
    }

    return err;
}

void SrsServer::on_signal(int signo)
{
    // For signal to quit with coredump.
    if (signo == SRS_SIGNAL_ASSERT_ABORT) {
        srs_trace("abort with coredump, signo=%d", signo);
        srs_assert(false);
        return;
    }

    if (signo == SRS_SIGNAL_RELOAD) {
        srs_trace("reload config, signo=%d", signo);
        signal_reload_ = true;
        return;
    }

#ifndef SRS_GPERF_MC
    if (signo == SRS_SIGNAL_REOPEN_LOG) {
        log_->reopen();

        srs_warn("reopen log file, signo=%d", signo);
        return;
    }
#endif

#ifdef SRS_GPERF_MC
    if (signo == SRS_SIGNAL_REOPEN_LOG) {
        signal_gmc_stop_ = true;
        srs_warn("for gmc, the SIGUSR1 used as SIGINT, signo=%d", signo);
        return;
    }
#endif

    if (signo == SRS_SIGNAL_PERSISTENCE_CONFIG) {
        signal_persistence_config_ = true;
        return;
    }

    if (signo == SIGINT) {
#ifdef SRS_GPERF_MC
        srs_trace("gmc is on, main cycle will terminate normally, signo=%d", signo);
        signal_gmc_stop_ = true;
#endif
    }

    // For K8S, force to gracefully quit for gray release or canary.
    // @see https://github.com/ossrs/srs/issues/1595#issuecomment-587473037
    if (signo == SRS_SIGNAL_FAST_QUIT && config_->is_force_grace_quit()) {
        srs_trace("force gracefully quit, signo=%d", signo);
        signo = SRS_SIGNAL_GRACEFULLY_QUIT;
    }

    if ((signo == SIGINT || signo == SRS_SIGNAL_FAST_QUIT) && !signal_fast_quit_) {
        srs_trace("sig=%d, user terminate program, fast quit", signo);
        signal_fast_quit_ = true;
        return;
    }

    if (signo == SRS_SIGNAL_GRACEFULLY_QUIT && !signal_gracefully_quit_) {
        srs_trace("sig=%d, user start gracefully quit", signo);
        signal_gracefully_quit_ = true;
        return;
    }
}

srs_error_t _srs_reload_err;
SrsReloadState _srs_reload_state;
std::string _srs_reload_id;

srs_error_t SrsServer::do2_cycle()
{
    srs_error_t err = srs_success;

    // gracefully quit for SIGINT or SIGTERM or SIGQUIT.
    if (signal_fast_quit_ || signal_gracefully_quit_) {
        srs_trace("cleanup for quit signal fast=%d, grace=%d", signal_fast_quit_, signal_gracefully_quit_);
        return err;
    }

    // for gperf heap checker,
    // @see: research/gperftools/heap-checker/heap_checker.cc
    // if user interrupt the program, exit to check mem leak.
    // but, if gperf, use reload to ensure main return normally,
    // because directly exit will cause core-dump.
#ifdef SRS_GPERF_MC
    if (signal_gmc_stop_) {
        srs_warn("gmc got singal to stop server.");
        return err;
    }
#endif

    // do persistence config to file.
    if (signal_persistence_config_) {
        signal_persistence_config_ = false;
        srs_info("get signal to persistence config to file.");

        if ((err = config_->persistence()) != srs_success) {
            return srs_error_wrap(err, "config persistence to file");
        }
        srs_trace("persistence config to file success.");
    }

    // do reload the config.
    if (signal_reload_) {
        signal_reload_ = false;
        srs_trace("starting reload config.");

        SrsReloadState state = SrsReloadStateInit;
        _srs_reload_state = SrsReloadStateInit;
        srs_freep(_srs_reload_err);
        SrsRand rand;
        _srs_reload_id = rand.gen_str(7);
        err = config_->reload(&state);
        _srs_reload_state = state;
        _srs_reload_err = srs_error_copy(err);
        if (err != srs_success) {
            // If the parsing and transformation of the configuration fail, we can tolerate it by simply
            // ignoring the new configuration and continuing to use the current one. However, if the
            // application of the new configuration fails, some configurations may be applied while
            // others may not. For instance, the listening port may be closed when the configuration
            // is set to listen on an unavailable port. In such cases, we should terminate the service.
            if (state == SrsReloadStateApplying) {
                return srs_error_wrap(err, "reload fatal error state=%d", state);
            }

            srs_warn("reload failed, state=%d, err %s", state, srs_error_desc(err).c_str());
            srs_freep(err);
        } else {
            srs_trace("reload config success, state=%d.", state);
        }
    }

    return err;
}

srs_error_t SrsServer::do_cycle()
{
    srs_error_t err = srs_success;

    // for asprocess.
    bool asprocess = config_->get_asprocess();

    while (true) {
        // asprocess check.
        if (asprocess && ::getppid() != ppid_) {
            return srs_error_new(ERROR_ASPROCESS_PPID, "asprocess ppid changed from %d to %d", ppid_, ::getppid());
        }

        if ((err = do2_cycle()) != srs_success) {
            return srs_error_wrap(err, "cycle");
        }

        // Break the loop when quit signals are set, otherwise we loop forever
        // printing "cleanup for quit signal" every second.
        if (signal_fast_quit_ || signal_gracefully_quit_) {
            break;
        }

        srs_usleep(1 * SRS_UTIME_SECONDS);
    }

    return err;
}

srs_error_t SrsServer::setup_ticks()
{
    srs_error_t err = srs_success;

    srs_freep(timer_);
    timer_ = app_factory_->create_hourglass("srs", this, 1 * SRS_UTIME_SECONDS);

    if (config_->get_stats_enabled()) {
        if ((err = timer_->tick(2, 3 * SRS_UTIME_SECONDS)) != srs_success) {
            return srs_error_wrap(err, "tick");
        }
        if ((err = timer_->tick(4, 6 * SRS_UTIME_SECONDS)) != srs_success) {
            return srs_error_wrap(err, "tick");
        }
        if ((err = timer_->tick(5, 6 * SRS_UTIME_SECONDS)) != srs_success) {
            return srs_error_wrap(err, "tick");
        }
        if ((err = timer_->tick(6, 9 * SRS_UTIME_SECONDS)) != srs_success) {
            return srs_error_wrap(err, "tick");
        }
        if ((err = timer_->tick(7, 9 * SRS_UTIME_SECONDS)) != srs_success) {
            return srs_error_wrap(err, "tick");
        }

        if ((err = timer_->tick(8, 3 * SRS_UTIME_SECONDS)) != srs_success) {
            return srs_error_wrap(err, "tick");
        }

        if ((err = timer_->tick(10, 9 * SRS_UTIME_SECONDS)) != srs_success) {
            return srs_error_wrap(err, "tick");
        }

        if ((err = timer_->tick(11, 5 * SRS_UTIME_SECONDS)) != srs_success) {
            return srs_error_wrap(err, "tick");
        }

        if ((err = timer_->tick(12, 5 * SRS_UTIME_SECONDS)) != srs_success) {
            return srs_error_wrap(err, "tick");
        }
    }

    if (config_->get_heartbeat_enabled()) {
        if ((err = timer_->tick(9, config_->get_heartbeat_interval())) != srs_success) {
            return srs_error_wrap(err, "tick");
        }
    }

    if ((err = timer_->start()) != srs_success) {
        return srs_error_wrap(err, "timer");
    }

    return err;
}

srs_error_t SrsServer::notify(int event, srs_utime_t interval, srs_utime_t tick)
{
    srs_error_t err = srs_success;

    switch (event) {
    case 2:
        srs_update_system_rusage();
        break;
    case 4:
        srs_update_disk_stat();
        break;
    case 5:
        srs_update_meminfo();
        break;
    case 6:
        srs_update_platform_info();
        break;
    case 7:
        srs_update_network_devices();
        break;
    case 8:
        resample_kbps();
        break;
    case 9:
        http_heartbeat_->heartbeat();
        break;
    case 10:
        srs_update_udp_snmp_statistic();
        break;
    case 11:
        rtc_session_manager_->srs_update_rtc_sessions();
        break;
    case 12:
        srs_update_server_statistics();
        break;
    }

    return err;
}

void SrsServer::resample_kbps()
{
    // collect delta from all clients.
    for (int i = 0; i < (int)conn_manager_->size(); i++) {
        ISrsResource *c = conn_manager_->at(i);

        SrsRtmpConn *rtmp = dynamic_cast<SrsRtmpConn *>(c);
        if (rtmp) {
            stat_->kbps_add_delta(c->get_id().c_str(), rtmp->delta());
            continue;
        }

        SrsHttpxConn *httpx = dynamic_cast<SrsHttpxConn *>(c);
        if (httpx) {
            stat_->kbps_add_delta(c->get_id().c_str(), httpx->delta());
            continue;
        }

#ifdef SRS_RTSP
        SrsRtspConnection *rtsp = dynamic_cast<SrsRtspConnection *>(c);
        if (rtsp) {
            stat_->kbps_add_delta(c->get_id().c_str(), rtsp->delta());
            continue;
        }
#endif

        SrsRtcTcpConn *tcp = dynamic_cast<SrsRtcTcpConn *>(c);
        if (tcp) {
            stat_->kbps_add_delta(c->get_id().c_str(), tcp->delta());
            continue;
        }

        SrsMpegtsSrtConn *srt = dynamic_cast<SrsMpegtsSrtConn *>(c);
        if (srt) {
            stat_->kbps_add_delta(c->get_id().c_str(), srt->delta());
            continue;
        }

        SrsRtcConnection *rtc = dynamic_cast<SrsRtcConnection *>(c);
        if (rtc) {
            stat_->kbps_add_delta(c->get_id().c_str(), rtc->delta());
            continue;
        }

        // Impossible path, because we only create these connections above.
        srs_assert(false);
    }

    // Update the global server level statistics.
    stat_->kbps_sample();
}

srs_error_t SrsServer::listen_srt_mpegts()
{
    srs_error_t err = srs_success;

    if (!config_->get_srt_enabled()) {
        return err;
    }

    // Close all listener for SRT if exists.
    close_srt_listeners();

    // Start listeners for SRT, support multiple addresses including IPv6.
    vector<string> srt_listens = config_->get_srt_listens();
    for (int i = 0; i < (int)srt_listens.size(); i++) {
        SrsSrtAcceptor *acceptor = new SrsSrtAcceptor(this);

        int port;
        string ip;
        srs_net_split_for_listener(srt_listens[i], ip, port);

        if ((err = acceptor->listen(ip, port)) != srs_success) {
            srs_freep(acceptor);
            srs_warn("srt listen %s:%d failed, err=%s", ip.c_str(), port, srs_error_desc(err).c_str());
            srs_freep(err);
            continue;
        }

        srt_acceptors_.push_back(acceptor);
    }

    // Check if at least one listener succeeded
    if (srt_acceptors_.empty()) {
        return srs_error_new(ERROR_SOCKET_LISTEN, "no srt listeners available");
    }

    return err;
}

void SrsServer::close_srt_listeners()
{
    std::vector<SrsSrtAcceptor *>::iterator it;
    for (it = srt_acceptors_.begin(); it != srt_acceptors_.end();) {
        SrsSrtAcceptor *acceptor = *it;
        srs_freep(acceptor);

        it = srt_acceptors_.erase(it);
    }
}

srs_error_t SrsServer::accept_srt_client(srs_srt_t srt_fd)
{
    srs_error_t err = srs_success;

    ISrsResource *resource = NULL;
    if ((err = srt_fd_to_resource(srt_fd, &resource)) != srs_success) {
        // close fd on conn error, otherwise will lead to fd leak
        srs_srt_close(srt_fd);
        return srs_error_wrap(err, "srt fd to resource");
    }
    srs_assert(resource);

    // directly enqueue, the cycle thread will remove the client.
    conn_manager_->add(resource);

    // Note that conn is managed by conn_manager_, so we don't need to free it.
    ISrsStartable *conn = dynamic_cast<ISrsStartable *>(resource);
    if ((err = conn->start()) != srs_success) {
        return srs_error_wrap(err, "start srt conn coroutine");
    }

    return err;
}

srs_error_t SrsServer::srt_fd_to_resource(srs_srt_t srt_fd, ISrsResource **pr)
{
    srs_error_t err = srs_success;

    string ip = "";
    int port = 0;
    if ((err = srs_srt_get_remote_ip_port(srt_fd, ip, port)) != srs_success) {
        return srs_error_wrap(err, "get srt ip port");
    }

    // Security or system flow control check.
    if ((err = on_before_connection("SRT", (int)srt_fd, ip, port)) != srs_success) {
        return srs_error_wrap(err, "check");
    }

    // The context id may change during creating the bellow objects.
    SrsContextRestore(_srs_context->get_id());

    // Convert to SRT connection.
    *pr = new SrsMpegtsSrtConn(conn_manager_, srt_fd, ip, port);

    return err;
}

srs_error_t SrsServer::listen_rtc_udp()
{
    srs_error_t err = srs_success;

    if (!config_->get_rtc_server_enabled()) {
        return err;
    }

    // Check protocol setting - only create UDP listeners if protocol allows UDP
    string protocol = config_->get_rtc_server_protocol();
    if (protocol == "tcp") {
        // When protocol is "tcp", don't create UDP listeners
        return err;
    }

    vector<string> rtc_listens = config_->get_rtc_server_listens();
    if (rtc_listens.empty()) {
        return srs_error_new(ERROR_RTC_PORT, "empty rtc listen");
    }

    // There should be no listeners before listening.
    srs_assert(rtc_listeners_.empty());

    // For each listen address, create multiple listeners based on reuseport setting
    int nn_listeners = config_->get_rtc_server_reuseport();
    for (int j = 0; j < (int)rtc_listens.size(); j++) {
        string ip;
        int port;
        srs_net_split_for_listener(rtc_listens[j], ip, port);

        if (port <= 0) {
            return srs_error_new(ERROR_RTC_PORT, "invalid port=%d", port);
        }

        for (int i = 0; i < nn_listeners; i++) {
            SrsUdpMuxListener *listener = new SrsUdpMuxListener(this, ip, port);

            if ((err = listener->listen()) != srs_success) {
                srs_freep(listener);
                return srs_error_wrap(err, "listen %s:%d", ip.c_str(), port);
            }

            srs_trace("WebRTC listen at udp://%s:%d, fd=%d", ip.c_str(), port, listener->fd());
            rtc_listeners_.push_back(listener);
        }
    }

    return err;
}

srs_error_t SrsServer::on_udp_packet(ISrsUdpMuxSocket *skt)
{
    return rtc_session_manager_->on_udp_packet(skt);
}

srs_error_t SrsServer::listen_rtc_api()
{
    srs_error_t err = srs_success;

    if ((err = http_api_mux_->handle("/rtc/v1/play/", new SrsGoApiRtcPlay(this))) != srs_success) {
        return srs_error_wrap(err, "handle play");
    }

    if ((err = http_api_mux_->handle("/rtc/v1/publish/", new SrsGoApiRtcPublish(this))) != srs_success) {
        return srs_error_wrap(err, "handle publish");
    }

    // Generally, WHIP is a publishing protocol, but it can be also used as playing.
    // See https://datatracker.ietf.org/doc/draft-ietf-wish-whep/
    if ((err = http_api_mux_->handle("/rtc/v1/whip/", new SrsGoApiRtcWhip(this))) != srs_success) {
        return srs_error_wrap(err, "handle whip");
    }

    // We create another mount, to support play with the same query string as publish.
    // See https://datatracker.ietf.org/doc/draft-murillo-whep/
    if ((err = http_api_mux_->handle("/rtc/v1/whip-play/", new SrsGoApiRtcWhip(this))) != srs_success) {
        return srs_error_wrap(err, "handle whep play");
    }
    if ((err = http_api_mux_->handle("/rtc/v1/whep/", new SrsGoApiRtcWhip(this))) != srs_success) {
        return srs_error_wrap(err, "handle whep play");
    }

#ifdef SRS_SIMULATOR
    if ((err = http_api_mux_->handle("/rtc/v1/nack/", new SrsGoApiRtcNACK(this))) != srs_success) {
        return srs_error_wrap(err, "handle nack");
    }
#endif

    return err;
}

ISrsRtcConnection *SrsServer::find_rtc_session_by_username(const std::string &username)
{
    return rtc_session_manager_->find_rtc_session_by_username(username);
}

srs_error_t SrsServer::create_rtc_session(SrsRtcUserConfig *ruc, SrsSdp &local_sdp, ISrsRtcConnection **psession)
{
    srs_error_t err = srs_success;

    ISrsRequest *req = ruc->req_;

    // Security or system flow control check. For WebRTC, use 0 as fd and port, because for
    // the WebRTC HTTP API, it's not useful information.
    if ((err = on_before_connection("RTC", (int)0, req->ip_, 0)) != srs_success) {
        return srs_error_wrap(err, "check");
    }

    return rtc_session_manager_->create_rtc_session(ruc, local_sdp, psession);
}

srs_error_t SrsServer::srs_update_server_statistics()
{
    srs_error_t err = srs_success;

    // Show statistics for RTC server.
    SrsProcSelfStat *u = srs_get_self_proc_stat();
    // Resident Set Size: number of pages the process has in real memory.
    int memory = (int)(u->rss_ * 4 / 1024);

    SrsKbpsStats stats;
    srs_global_kbps_update(&stats);

    srs_trace("SRS: cpu=%.2f%%,%dMB%s%s%s%s%s%s%s%s%s%s%s",
              u->percent_ * 100, memory,
              stats.cid_desc_.c_str(), stats.timer_desc_.c_str(),
              stats.recvfrom_desc_.c_str(), stats.io_desc_.c_str(), stats.msg_desc_.c_str(),
              stats.epoll_desc_.c_str(), stats.sched_desc_.c_str(), stats.clock_desc_.c_str(),
              stats.thread_desc_.c_str(), stats.free_desc_.c_str(), stats.objs_desc_.c_str());

    return err;
}

srs_error_t SrsServer::on_tcp_client(ISrsListener *listener, srs_netfd_t stfd)
{
    srs_error_t err = do_on_tcp_client(listener, stfd);

    // We always try to close the stfd, because it should be NULL if it has been handled or closed.
    srs_close_stfd(stfd);

    return err;
}

srs_error_t SrsServer::do_on_tcp_client(ISrsListener *listener, srs_netfd_t &stfd)
{
    srs_error_t err = srs_success;

    int fd = srs_netfd_fileno(stfd);
    string ip = srs_get_peer_ip(fd);
    int port = srs_get_peer_port(fd);

    // Ignore if ip is empty, for example, load balancer keepalive.
    if (ip.empty()) {
        if (config_->empty_ip_ok())
            return err;
        return srs_error_new(ERROR_SOCKET_GET_PEER_IP, "ignore empty ip, fd=%d", fd);
    }

    // Security or system flow control check.
    if ((err = on_before_connection("TCP", fd, ip, port)) != srs_success) {
        return srs_error_wrap(err, "check");
    }

    // Set to close the fd when forking, to avoid fd leak when start a process.
    // See https://github.com/ossrs/srs/issues/518
    if (true) {
        int val;
        if ((val = fcntl(fd, F_GETFD, 0)) < 0) {
            return srs_error_new(ERROR_SYSTEM_PID_GET_FILE_INFO, "fnctl F_GETFD error! fd=%d", fd);
        }
        val |= FD_CLOEXEC;
        if (fcntl(fd, F_SETFD, val) < 0) {
            return srs_error_new(ERROR_SYSTEM_PID_SET_FILE_INFO, "fcntl F_SETFD error! fd=%d", fd);
        }
    }

    // Covert handler to resource.
    ISrsResource *resource = NULL;

    // The context id may change during creating the bellow objects.
    SrsContextRestore(_srs_context->get_id());

    // From now on, we always handle the stfd, so we set the original one to NULL.
    srs_netfd_t stfd2 = stfd;
    stfd = NULL;

    // If reuse HTTP server with WebRTC TCP, peek to detect the client.
    if (reuse_rtc_over_server_ && (listener == http_listener_ || listener == https_listener_)) {
        SrsTcpConnection *skt = new SrsTcpConnection(stfd2);
        SrsBufferedReadWriter *io = new SrsBufferedReadWriter(skt);

        // Peek first N bytes to finger out the real client type.
        uint8_t b[10];
        int nn = sizeof(b);
        if ((err = io->peek((char *)b, &nn)) != srs_success) {
            srs_freep(io);
            srs_freep(skt);
            return srs_error_wrap(err, "peek");
        }

        // If first message is BindingRequest(00 01), prefixed with length(2B), it's WebRTC client. Generally, the frame
        // length minus message length should be 20, that is the header size of STUN is 20 bytes. For example:
        //      00 6c # Frame length: 0x006c = 108
        //      00 01 # Message Type: Binding Request(0x0001)
        //      00 58 # Message Length: 0x005 = 88
        //      21 12 a4 42 # Message Cookie: 0x2112a442
        //      48 32 6c 61 6b 42 35 71 42 35 4a 71 # Message Transaction ID: 12 bytes
        if (nn == 10 && b[0] == 0 && b[2] == 0 && b[3] == 1 && b[1] - b[5] == 20 && b[6] == 0x21 && b[7] == 0x12 && b[8] == 0xa4 && b[9] == 0x42) {
            resource = new SrsRtcTcpConn(io, ip, port);
        } else {
            string key = listener == https_listener_ ? config_->get_https_stream_ssl_key() : "";
            string cert = listener == https_listener_ ? config_->get_https_stream_ssl_cert() : "";
            resource = new SrsHttpxConn(conn_manager_, io, http_server_, ip, port, key, cert);
        }
    }

    // Create resource by normal listeners.
    if (!resource) {
        if (listener == rtmp_listener_) {
            SrsRtmpTransport *transport = new SrsRtmpTransport(stfd2);
            SrsRtmpConn *conn = new SrsRtmpConn(transport, ip, port);
            conn->assemble();
            resource = conn;
        } else if (listener == rtmps_listener_) {
            SrsRtmpTransport *transport = new SrsRtmpsTransport(stfd2);
            SrsRtmpConn *conn = new SrsRtmpConn(transport, ip, port);
            conn->assemble();
            resource = conn;
        } else if (listener == api_listener_ || listener == apis_listener_) {
            string key = listener == apis_listener_ ? config_->get_https_api_ssl_key() : "";
            string cert = listener == apis_listener_ ? config_->get_https_api_ssl_cert() : "";
            resource = new SrsHttpxConn(conn_manager_, new SrsTcpConnection(stfd2), http_api_mux_, ip, port, key, cert);
        } else if (listener == http_listener_ || listener == https_listener_) {
            string key = listener == https_listener_ ? config_->get_https_stream_ssl_key() : "";
            string cert = listener == https_listener_ ? config_->get_https_stream_ssl_cert() : "";
            resource = new SrsHttpxConn(conn_manager_, new SrsTcpConnection(stfd2), http_server_, ip, port, key, cert);
        } else if (listener == webrtc_listener_) {
            resource = new SrsRtcTcpConn(new SrsTcpConnection(stfd2), ip, port);
#ifdef SRS_RTSP
        } else if (listener == rtsp_listener_) {
            SrsRtspConnection *conn = new SrsRtspConnection(conn_manager_, new SrsTcpConnection(stfd2), ip, port);
            conn->assemble();
            resource = conn;
#endif
        } else if (listener == exporter_listener_) {
            // TODO: FIXME: Maybe should support https metrics.
            resource = new SrsHttpxConn(conn_manager_, new SrsTcpConnection(stfd2), http_api_mux_, ip, port, "", "");
        } else {
            srs_close_stfd(stfd2);
            srs_warn("Close for invalid fd=%d, ip=%s:%d", fd, ip.c_str(), port);
            return err;
        }
    }

    // For RTC TCP connection, use resource executor to manage the resource.
    SrsRtcTcpConn *raw_conn = dynamic_cast<SrsRtcTcpConn *>(resource);
    if (raw_conn) {
        SrsSharedResource<ISrsRtcTcpConn> *conn = new SrsSharedResource<ISrsRtcTcpConn>(raw_conn);
        SrsExecutorCoroutine *executor = new SrsExecutorCoroutine(conn_manager_, conn, raw_conn, raw_conn);
        raw_conn->setup_owner(conn, executor, executor);
        if ((err = executor->start()) != srs_success) {
            srs_freep(executor);
            return srs_error_wrap(err, "start executor");
        }
        return err;
    }

    // Use connection manager to manage all the resources.
    srs_assert(resource);
    conn_manager_->add(resource);

    // If connection is a resource to start, start a coroutine to handle it.
    // Note that conn is managed by conn_manager, so we don't need to free it.
    ISrsStartable *conn = dynamic_cast<ISrsStartable *>(resource);
    srs_assert(conn);
    if ((err = conn->start()) != srs_success) {
        return srs_error_wrap(err, "start conn coroutine");
    }

    return err;
}

srs_error_t SrsServer::on_before_connection(const char *label, int fd, const std::string &ip, int port)
{
    srs_error_t err = srs_success;

    // Failed if exceed the connection limitation.
    int max_connections = config_->get_max_connections();

    if ((int)conn_manager_->size() >= max_connections) {
        return srs_error_new(ERROR_EXCEED_CONNECTIONS, "drop %s fd=%d, ip=%s:%d, max=%d, cur=%d for exceed connection limits",
                             label, fd, ip.c_str(), port, max_connections, (int)conn_manager_->size());
    }

    return err;
}

srs_error_t SrsServer::on_publish(ISrsRequest *r)
{
    srs_error_t err = srs_success;

    if ((err = http_server_->http_mount(r)) != srs_success) {
        return srs_error_wrap(err, "http mount");
    }

    SrsCoWorkers *coworkers = SrsCoWorkers::instance();
    if ((err = coworkers->on_publish(r)) != srs_success) {
        return srs_error_wrap(err, "coworkers");
    }

    return err;
}

void SrsServer::on_unpublish(ISrsRequest *r)
{
    http_server_->http_unmount(r);

    SrsCoWorkers *coworkers = SrsCoWorkers::instance();
    coworkers->on_unpublish(r);
}

SrsSignalManager *SrsSignalManager::instance = NULL;

SrsSignalManager::SrsSignalManager(SrsServer *s)
{
    SrsSignalManager::instance = this;

    server_ = s;
    sig_pipe_[0] = sig_pipe_[1] = -1;
    trd_ = new SrsSTCoroutine("signal", this, _srs_context->get_id());
    signal_read_stfd_ = NULL;
}

SrsSignalManager::~SrsSignalManager()
{
    srs_freep(trd_);

    srs_close_stfd(signal_read_stfd_);

    if (sig_pipe_[0] > 0) {
        ::close(sig_pipe_[0]);
    }
    if (sig_pipe_[1] > 0) {
        ::close(sig_pipe_[1]);
    }
}

srs_error_t SrsSignalManager::initialize()
{
    /* Create signal pipe */
    if (pipe(sig_pipe_) < 0) {
        return srs_error_new(ERROR_SYSTEM_CREATE_PIPE, "create pipe");
    }

    if ((signal_read_stfd_ = srs_netfd_open(sig_pipe_[0])) == NULL) {
        return srs_error_new(ERROR_SYSTEM_CREATE_PIPE, "open pipe");
    }

    return srs_success;
}

srs_error_t SrsSignalManager::start()
{
    srs_error_t err = srs_success;

    /**
     * Note that if multiple processes are used (see below),
     * the signal pipe should be initialized after the fork(2) call
     * so that each process has its own private pipe.
     */
    struct sigaction sa;

    /* Install sig_catcher() as a signal handler */
    sa.sa_handler = SrsSignalManager::sig_catcher;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SRS_SIGNAL_RELOAD, &sa, NULL);

    sa.sa_handler = SrsSignalManager::sig_catcher;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SRS_SIGNAL_FAST_QUIT, &sa, NULL);

    sa.sa_handler = SrsSignalManager::sig_catcher;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SRS_SIGNAL_GRACEFULLY_QUIT, &sa, NULL);

    sa.sa_handler = SrsSignalManager::sig_catcher;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SRS_SIGNAL_ASSERT_ABORT, &sa, NULL);

    sa.sa_handler = SrsSignalManager::sig_catcher;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);

    sa.sa_handler = SrsSignalManager::sig_catcher;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SRS_SIGNAL_REOPEN_LOG, &sa, NULL);

    srs_trace("signal installed, reload=%d, reopen=%d, fast_quit=%d, grace_quit=%d",
              SRS_SIGNAL_RELOAD, SRS_SIGNAL_REOPEN_LOG, SRS_SIGNAL_FAST_QUIT, SRS_SIGNAL_GRACEFULLY_QUIT);

    if ((err = trd_->start()) != srs_success) {
        return srs_error_wrap(err, "signal manager");
    }

    return err;
}

srs_error_t SrsSignalManager::cycle()
{
    srs_error_t err = srs_success;

    while (true) {
        if ((err = trd_->pull()) != srs_success) {
            return srs_error_wrap(err, "signal manager");
        }

        int signo;

        /* Read the next signal from the pipe */
        srs_read(signal_read_stfd_, &signo, sizeof(int), SRS_UTIME_NO_TIMEOUT);

        /* Process signal synchronously */
        server_->on_signal(signo);
    }

    return err;
}

void SrsSignalManager::sig_catcher(int signo)
{
    int err;

    /* Save errno to restore it after the write() */
    err = errno;

    /* write() is reentrant/async-safe */
    int fd = SrsSignalManager::instance->sig_pipe_[1];
    write(fd, &signo, sizeof(int));

    errno = err;
}

// Whether we are in docker, defined in main module.
extern bool _srs_in_docker;

SrsInotifyWorker::SrsInotifyWorker(SrsServer *s)
{
    server_ = s;
    trd_ = new SrsSTCoroutine("inotify", this);
    inotify_fd_ = NULL;

    config_ = _srs_config;
}

SrsInotifyWorker::~SrsInotifyWorker()
{
    srs_freep(trd_);
    srs_close_stfd(inotify_fd_);

    config_ = NULL;
}

srs_error_t SrsInotifyWorker::start()
{
    srs_error_t err = srs_success;

#if !defined(SRS_OSX) && !defined(SRS_CYGWIN64)
    // Whether enable auto reload config.
    bool auto_reload = config_->inotify_auto_reload();
    if (!auto_reload && _srs_in_docker && config_->auto_reload_for_docker()) {
        srs_warn("enable auto reload for docker");
        auto_reload = true;
    }

    if (!auto_reload) {
        return err;
    }

    // Create inotify to watch config file.
    int fd = ::inotify_init1(IN_NONBLOCK);
    if (fd < 0) {
        return srs_error_new(ERROR_INOTIFY_CREATE, "create inotify");
    }

    // Open as stfd to read by ST.
    if ((inotify_fd_ = srs_netfd_open(fd)) == NULL) {
        ::close(fd);
        return srs_error_new(ERROR_INOTIFY_OPENFD, "open fd=%d", fd);
    }

    if (((err = srs_fd_closeexec(fd))) != srs_success) {
        return srs_error_wrap(err, "closeexec fd=%d", fd);
    }

    // /* the following are legal, implemented events that user-space can watch for */
    // #define IN_ACCESS               0x00000001      /* File was accessed */
    // #define IN_MODIFY               0x00000002      /* File was modified */
    // #define IN_ATTRIB               0x00000004      /* Metadata changed */
    // #define IN_CLOSE_WRITE          0x00000008      /* Writtable file was closed */
    // #define IN_CLOSE_NOWRITE        0x00000010      /* Unwrittable file closed */
    // #define IN_OPEN                 0x00000020      /* File was opened */
    // #define IN_MOVED_FROM           0x00000040      /* File was moved from X */
    // #define IN_MOVED_TO             0x00000080      /* File was moved to Y */
    // #define IN_CREATE               0x00000100      /* Subfile was created */
    // #define IN_DELETE               0x00000200      /* Subfile was deleted */
    // #define IN_DELETE_SELF          0x00000400      /* Self was deleted */
    // #define IN_MOVE_SELF            0x00000800      /* Self was moved */
    //
    // /* the following are legal events.  they are sent as needed to any watch */
    // #define IN_UNMOUNT              0x00002000      /* Backing fs was unmounted */
    // #define IN_Q_OVERFLOW           0x00004000      /* Event queued overflowed */
    // #define IN_IGNORED              0x00008000      /* File was ignored */
    //
    // /* helper events */
    // #define IN_CLOSE                (IN_CLOSE_WRITE | IN_CLOSE_NOWRITE) /* close */
    // #define IN_MOVE                 (IN_MOVED_FROM | IN_MOVED_TO) /* moves */
    //
    // /* special flags */
    // #define IN_ONLYDIR              0x01000000      /* only watch the path if it is a directory */
    // #define IN_DONT_FOLLOW          0x02000000      /* don't follow a sym link */
    // #define IN_EXCL_UNLINK          0x04000000      /* exclude events on unlinked objects */
    // #define IN_MASK_ADD             0x20000000      /* add to the mask of an already existing watch */
    // #define IN_ISDIR                0x40000000      /* event occurred against dir */
    // #define IN_ONESHOT              0x80000000      /* only send event once */

    // Watch the config directory events.
    SrsPath path_util;
    string config_dir = path_util.filepath_dir(config_->config());
    uint32_t mask = IN_MODIFY | IN_CREATE | IN_MOVED_TO;
    int watch_conf = 0;
    if ((watch_conf = ::inotify_add_watch(fd, config_dir.c_str(), mask)) < 0) {
        return srs_error_new(ERROR_INOTIFY_WATCH, "watch file=%s, fd=%d, watch=%d, mask=%#x",
                             config_dir.c_str(), fd, watch_conf, mask);
    }
    srs_trace("auto reload watching fd=%d, watch=%d, file=%s", fd, watch_conf, config_dir.c_str());

    if ((err = trd_->start()) != srs_success) {
        return srs_error_wrap(err, "inotify");
    }
#endif

    return err;
}

srs_error_t SrsInotifyWorker::cycle()
{
    srs_error_t err = srs_success;

#if !defined(SRS_OSX) && !defined(SRS_CYGWIN64)
    SrsPath path;
    string config_path = config_->config();
    string config_file = path.filepath_base(config_path);
    string k8s_file = "..data";

    while (true) {
        char buf[4096];
        ssize_t nn = srs_read(inotify_fd_, buf, (size_t)sizeof(buf), SRS_UTIME_NO_TIMEOUT);
        if (nn < 0) {
            srs_warn("inotify ignore read failed, nn=%d", (int)nn);
            break;
        }

        // Whether config file changed.
        bool do_reload = false;

        // Parse all inotify events.
        inotify_event *ie = NULL;
        for (char *ptr = buf; ptr < buf + nn; ptr += sizeof(inotify_event) + ie->len) {
            ie = (inotify_event *)ptr;

            if (!ie->len || !ie->name) {
                continue;
            }

            string name = ie->name;
            if ((name == k8s_file || name == config_file) && ie->mask & (IN_MODIFY | IN_CREATE | IN_MOVED_TO)) {
                do_reload = true;
            }

            srs_trace("inotify event wd=%d, mask=%#x, len=%d, name=%s, reload=%d", ie->wd, ie->mask, ie->len, ie->name, do_reload);
        }

        // Notify server to do reload.
        SrsPath path;
        if (do_reload && path.exists(config_path)) {
            server_->on_signal(SRS_SIGNAL_RELOAD);
        }

        srs_usleep(3000 * SRS_UTIME_MILLISECONDS);
    }
#endif

    return err;
}

SrsPidFileLocker::SrsPidFileLocker()
{
    pid_fd_ = -1;
    config_ = _srs_config;
}

SrsPidFileLocker::~SrsPidFileLocker()
{
    close();
    config_ = NULL;
}

srs_error_t SrsPidFileLocker::acquire()
{
    srs_error_t err = srs_success;

    pid_file_ = config_->get_pid_file();

    // -rw-r--r--
    // 644
    int mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;

    int fd;
    // open pid file
    if ((fd = ::open(pid_file_.c_str(), O_WRONLY | O_CREAT, mode)) == -1) {
        return srs_error_new(ERROR_SYSTEM_PID_ACQUIRE, "open pid file=%s", pid_file_.c_str());
    }

    // require write lock
    struct flock lock;

    lock.l_type = F_WRLCK;    // F_RDLCK, F_WRLCK, F_UNLCK
    lock.l_start = 0;         // type offset, relative to l_whence
    lock.l_whence = SEEK_SET; // SEEK_SET, SEEK_CUR, SEEK_END
    lock.l_len = 0;

    if (fcntl(fd, F_SETLK, &lock) == -1) {
        if (errno == EACCES || errno == EAGAIN) {
            ::close(fd);
            srs_error("srs is already running!");
            return srs_error_new(ERROR_SYSTEM_PID_ALREADY_RUNNING, "srs is already running");
        }
        return srs_error_new(ERROR_SYSTEM_PID_LOCK, "access to pid=%s", pid_file_.c_str());
    }

    // truncate file
    if (ftruncate(fd, 0) != 0) {
        return srs_error_new(ERROR_SYSTEM_PID_TRUNCATE_FILE, "truncate pid file=%s", pid_file_.c_str());
    }

    // write the pid
    std::string pid = srs_strconv_format_int(getpid());
    if (write(fd, pid.c_str(), pid.length()) != (int)pid.length()) {
        return srs_error_new(ERROR_SYSTEM_PID_WRITE_FILE, "write pid=%s to file=%s", pid.c_str(), pid_file_.c_str());
    }

    // auto close when fork child process.
    int val;
    if ((val = fcntl(fd, F_GETFD, 0)) < 0) {
        return srs_error_new(ERROR_SYSTEM_PID_GET_FILE_INFO, "fcntl fd=%d", fd);
    }
    val |= FD_CLOEXEC;
    if (fcntl(fd, F_SETFD, val) < 0) {
        return srs_error_new(ERROR_SYSTEM_PID_SET_FILE_INFO, "lock file=%s fd=%d", pid_file_.c_str(), fd);
    }

    srs_trace("write pid=%s to %s success!", pid.c_str(), pid_file_.c_str());
    pid_fd_ = fd;

    return err;
}

void SrsPidFileLocker::close()
{
    if (pid_fd_ > 0) {
        ::close(pid_fd_);
        pid_fd_ = -1;
    }
}
