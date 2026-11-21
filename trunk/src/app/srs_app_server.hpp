//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_APP_SERVER_HPP
#define SRS_APP_SERVER_HPP

#include <srs_core.hpp>

#include <string>
#include <vector>

#include <srs_app_hls.hpp>
#include <srs_app_listener.hpp>
#include <srs_app_reload.hpp>
#include <srs_app_rtmp_source.hpp>
#include <srs_app_srt_listener.hpp>
#include <srs_app_srt_server.hpp>
#include <srs_app_st.hpp>
#include <srs_kernel_hourglass.hpp>
#include <srs_protocol_conn.hpp>
#include <srs_protocol_srt.hpp>
#include <srs_protocol_st.hpp>

class SrsAsyncCallWorker;
class ISrsUdpMuxListener;
class SrsUdpMuxSocket;
class SrsRtcUserConfig;
class SrsSdp;
class SrsRtcConnection;
class ISrsAsyncCallTask;
class ISrsSignalManager;
class SrsServer;
class ISrsCommonHttpHandler;
class ISrsHttpServer;
class ISrsIngester;
class ISrsHttpHeartbeat;
class SrsKbps;
class SrsConfDirective;
class ISrsTcpHandler;
class ISrsUdpHandler;
class SrsUdpListener;
class SrsTcpListener;
class SrsResourceManager;
class SrsMultipleTcpListeners;
class ISrsMultipleTcpListeners;
class ISrsIpListener;
class ISrsUdpCasterListener;
class ISrsGbListener;
class SrsRtmpTransport;
class SrsRtmpsTransport;
class ISrsSrtAcceptor;
class SrsSrtEventLoop;
class ISrsRtcSessionManager;
class ISrsPidFileLocker;
class ISrsCoWorkers;
class ISrsAppConfig;
class ISrsLiveSourceManager;
class ISrsResourceManager;
class ISrsDtlsCertificate;
class ISrsAsyncCallWorker;
class ISrsCircuitBreaker;
class ISrsSrtSourceManager;
class ISrsRtcSourceManager;
class ISrsRtspSourceManager;
class ISrsLog;
class ISrsStatistic;
class ISrsHourGlass;
class ISrsAppFactory;
class ISrsUdpMuxSocket;
class ISrsRtcBlackhole;
class ISrsSrtEventLoop;
class ISrsRtcConnection;
class ISrsReloadStatus;
class SrsReloadStatus;

extern srs_error_t srs_global_initialize();

extern SrsReloadStatus *_srs_reload_status;

class ISrsSignalHandler
{
public:
    ISrsSignalHandler();
    virtual ~ISrsSignalHandler();

public:
    virtual void on_signal(int signo) = 0;
};

class ISrsApiServerOwner
{
public:
    ISrsApiServerOwner();
    virtual ~ISrsApiServerOwner();

public:
    virtual ISrsCommonHttpHandler *api_server() = 0;
};

class ISrsRtcApiServer
{
public:
    ISrsRtcApiServer();
    virtual ~ISrsRtcApiServer();

public:
    virtual srs_error_t create_rtc_session(SrsRtcUserConfig *ruc, SrsSdp &local_sdp, ISrsRtcConnection **psession) = 0;
    virtual ISrsRtcConnection *find_rtc_session_by_username(const std::string &ufrag) = 0;
};

class SrsServer : public ISrsLiveSourceHandler,
                  public ISrsTcpHandler,
                  public ISrsHourGlassHandler,
                  public ISrsSrtClientHandler,
                  public ISrsUdpMuxHandler,
                  public ISrsSignalHandler,
                  public ISrsApiServerOwner,
                  public ISrsRtcApiServer
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsAppConfig *config_;
    ISrsLiveSourceManager *live_sources_;
    ISrsResourceManager *conn_manager_;
    ISrsDtlsCertificate *rtc_dtls_certificate_;
    ISrsAsyncCallWorker *dvr_async_;
    ISrsCircuitBreaker *circuit_breaker_;
    ISrsSrtSourceManager *srt_sources_;
    ISrsRtcSourceManager *rtc_sources_;
#ifdef SRS_RTSP
    ISrsRtspSourceManager *rtsp_sources_;
#endif
#ifdef SRS_GB28181
    ISrsResourceManager *gb_manager_;
#endif
    ISrsLog *log_;
    ISrsStatistic *stat_;
    ISrsAppFactory *app_factory_;
    ISrsReloadStatus *reload_status_;
    ISrsRtcBlackhole *blackhole_;
    ISrsSrtEventLoop *srt_eventloop_;
    ISrsCoWorkers *coworkers_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsCommonHttpHandler *http_api_mux_;
    ISrsHttpServer *http_server_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsHttpHeartbeat *http_heartbeat_;
    ISrsIngester *ingester_;
    ISrsHourGlass *timer_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsPidFileLocker *pid_file_locker_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    bool reuse_api_over_server_;
    bool reuse_rtc_over_server_;
    srs_utime_t peek_timeout_;
    ISrsMultipleTcpListeners *rtmp_listener_;
    ISrsMultipleTcpListeners *rtmps_listener_;
    ISrsMultipleTcpListeners *api_listener_;
    ISrsMultipleTcpListeners *apis_listener_;
    // Serves both HTTP static files and HTTP streams, and may be shared with the HTTP API and WebRTC over TCP.
    ISrsMultipleTcpListeners *http_listener_;
    // Serves both HTTPS static files and HTTPS streams, and may be shared with the HTTPS API and WebRTC over TCP.
    ISrsMultipleTcpListeners *https_listener_;
    ISrsMultipleTcpListeners *webrtc_listener_;
#ifdef SRS_RTSP
    ISrsMultipleTcpListeners *rtsp_listener_;
#endif
    ISrsUdpCasterListener *stream_caster_mpegts_;
    ISrsIpListener *exporter_listener_;
#ifdef SRS_GB28181
    ISrsGbListener *stream_caster_gb28181_;
#endif

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    std::vector<ISrsSrtAcceptor *>
        srt_acceptors_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    std::vector<ISrsUdpMuxListener *>
        rtc_listeners_;
    ISrsRtcSessionManager *rtc_session_manager_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsSignalManager *signal_manager_;
    bool signal_reload_;
    bool signal_persistence_config_;
    bool signal_gmc_stop_;
    bool signal_fast_quit_;
    bool signal_gracefully_quit_;

public:
    SrsServer();
    virtual ~SrsServer();

public:
    void assemble(); // Not in the constructor, to avoid calling functions there.

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual void dispose();
    virtual void gracefully_dispose();

public:
    ISrsCommonHttpHandler *api_server();

public:
    virtual srs_error_t initialize();

public:
    srs_error_t run();

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t initialize_st();
    virtual srs_error_t initialize_signal();
    virtual srs_error_t listen();
    virtual srs_error_t register_signal();
    virtual srs_error_t http_handle();
    virtual srs_error_t ingest();

public:
    void stop();

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t cycle();

public:
    // The HTTP RAW API calls it too, not only the signal manager.
    virtual void on_signal(int signo);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t do_cycle();
    virtual srs_error_t do2_cycle();

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t setup_ticks();
    virtual srs_error_t notify(int event, srs_utime_t interval, srs_utime_t tick);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual void resample_kbps();

    virtual srs_error_t listen_srt_mpegts();
    virtual void close_srt_listeners();
    virtual srs_error_t accept_srt_client(srs_srt_t srt_fd);
    virtual srs_error_t srt_fd_to_resource(srs_srt_t srt_fd, ISrsResource **pr);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t listen_rtc_udp();

public:
    virtual srs_error_t on_udp_packet(ISrsUdpMuxSocket *skt);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t listen_rtc_api();

public:
    virtual ISrsRtcConnection *find_rtc_session_by_username(const std::string &ufrag);
    virtual srs_error_t create_rtc_session(SrsRtcUserConfig *ruc, SrsSdp &local_sdp, ISrsRtcConnection **psession);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t srs_update_server_statistics();

public:
    virtual srs_error_t on_tcp_client(ISrsListener *listener, srs_netfd_t stfd);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t do_on_tcp_client(ISrsListener *listener, srs_netfd_t &stfd);
    virtual srs_error_t start_resource(ISrsResource *resource);
    virtual srs_error_t on_before_connection(const char *label, int fd, const std::string &ip, int port);

public:
    // Runs in the client's own coroutine, started by SrsAsyncClientPeeker.
    virtual srs_error_t on_peeked_client(ISrsListener *listener, srs_netfd_t stfd, const std::string &ip, int port);

public:
    virtual srs_error_t on_publish(ISrsRequest *r);
    virtual void on_unpublish(ISrsRequest *r);
};

extern SrsServer *_srs_server;

class SrsAsyncClientPeeker : public ISrsResource, public ISrsCoroutineHandler
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    SrsServer *server_;
    ISrsListener *listener_;
    srs_netfd_t stfd_;
    std::string ip_;
    int port_;
    SrsContextId cid_;

public:
    SrsAsyncClientPeeker(SrsServer *server, ISrsListener *listener, srs_netfd_t stfd, std::string ip, int port);
    virtual ~SrsAsyncClientPeeker();

public:
    virtual srs_error_t cycle();

public:
    virtual const SrsContextId &get_id();
    virtual std::string desc();
};

class ISrsSignalManager
{
public:
    ISrsSignalManager();
    virtual ~ISrsSignalManager();

public:
    virtual void assemble() = 0;
    virtual srs_error_t initialize() = 0;
    virtual srs_error_t start() = 0;
};

// Converts signals to I/O events, see st-1.9/docs/notes.html
class SrsSignalManager : public ISrsSignalManager, public ISrsCoroutineHandler
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // A signal queue of up to PIPE_BUF/sizeof(int) signals.
    int sig_pipe_[2];
    srs_netfd_t signal_read_stfd_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsSignalHandler *server_;
    ISrsCoroutine *trd_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsAppFactory *app_factory_;
    ISrsContext *context_;

public:
    SrsSignalManager(ISrsSignalHandler *s);
    virtual void assemble(); // Not in the constructor, to avoid calling functions there.
    virtual ~SrsSignalManager();

public:
    virtual srs_error_t initialize();
    virtual srs_error_t start();

public:
    virtual srs_error_t cycle();

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    static SrsSignalManager *instance;
    static void sig_catcher(int signo);
};

// Auto reload by inotify.
// @see https://github.com/ossrs/srs/issues/1635
class SrsInotifyWorker : public ISrsCoroutineHandler
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsAppConfig *config_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    SrsServer *server_;
    ISrsCoroutine *trd_;
    srs_netfd_t inotify_fd_;

public:
    SrsInotifyWorker(SrsServer *s);
    virtual ~SrsInotifyWorker();

public:
    virtual srs_error_t start();

public:
    virtual srs_error_t cycle();
};

class ISrsPidFileLocker
{
public:
    ISrsPidFileLocker();
    virtual ~ISrsPidFileLocker();

public:
    virtual srs_error_t acquire() = 0;
};

class SrsPidFileLocker : public ISrsPidFileLocker
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsAppConfig *config_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    int pid_fd_;
    std::string pid_file_;

public:
    SrsPidFileLocker();
    virtual ~SrsPidFileLocker();

public:
    virtual srs_error_t acquire();

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual void close();
};

#endif
