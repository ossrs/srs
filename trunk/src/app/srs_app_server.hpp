//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_APP_SERVER_HPP
#define SRS_APP_SERVER_HPP

#include <srs_core.hpp>

#include <string>
#include <vector>

#include <srs_app_conn.hpp>
#include <srs_app_hls.hpp>
#include <srs_app_hourglass.hpp>
#include <srs_app_listener.hpp>
#include <srs_app_reload.hpp>
#include <srs_app_source.hpp>
#include <srs_app_srt_listener.hpp>
#include <srs_app_srt_server.hpp>
#include <srs_app_st.hpp>
#include <srs_protocol_srt.hpp>
#include <srs_protocol_st.hpp>

class SrsAsyncCallWorker;
class SrsUdpMuxListener;
class SrsUdpMuxSocket;
class SrsRtcUserConfig;
class SrsSdp;
class SrsRtcConnection;
class ISrsAsyncCallTask;
class SrsSignalManager;
class SrsServer;
class ISrsHttpServeMux;
class SrsHttpServer;
class SrsIngester;
class SrsHttpHeartbeat;
class SrsKbps;
class SrsConfDirective;
class ISrsTcpHandler;
class ISrsUdpHandler;
class SrsUdpListener;
class SrsTcpListener;
class SrsAppCasterFlv;
class SrsResourceManager;
class SrsLatestVersion;
class SrsMultipleTcpListeners;
class SrsHttpFlvListener;
class SrsUdpCasterListener;
class SrsGbListener;
class SrsRtmpTransport;
class SrsRtmpsTransport;
class SrsSrtAcceptor;
class SrsSrtEventLoop;
class SrsRtcSessionManager;
class SrsPidFileLocker;

// Initialize global shared variables cross all threads.
extern srs_error_t srs_global_initialize();

// SrsServer is the main server class of SRS (Simple Realtime Server) that provides comprehensive
// streaming media server functionality. It serves as the central orchestrator for all streaming
// protocols and services in a single-threaded, coroutine-based architecture.
class SrsServer : public ISrsReloadHandler, // Reload framework for permormance optimization.
                  public ISrsLiveSourceHandler,
                  public ISrsTcpHandler,
                  public ISrsHourGlass,
                  public ISrsSrtClientHandler,
                  public ISrsUdpMuxHandler
{
private:
    ISrsHttpServeMux *http_api_mux_;
    SrsHttpServer *http_server_;

private:
    SrsHttpHeartbeat *http_heartbeat_;
    SrsIngester *ingester_;
    SrsHourGlass *timer_;

private:
    // PID file manager for process identification and locking.
    SrsPidFileLocker *pid_file_locker_;

private:
    // If reusing, HTTP API use the same port of HTTP server.
    bool reuse_api_over_server_;
    // If reusing, WebRTC TCP use the same port of HTTP server.
    bool reuse_rtc_over_server_;
    // RTMP stream listeners, over TCP.
    SrsMultipleTcpListeners *rtmp_listener_;
    // RTMPS stream listeners, over TCP.
    SrsMultipleTcpListeners *rtmps_listener_;
    // HTTP API listener, over TCP. Please note that it might reuse with stream listener.
    SrsMultipleTcpListeners *api_listener_;
    // HTTPS API listener, over TCP. Please note that it might reuse with stream listener.
    SrsMultipleTcpListeners *apis_listener_;
    // HTTP server listener, over TCP. Please note that request of both HTTP static and stream are served by this
    // listener, and it might be reused by HTTP API and WebRTC TCP.
    SrsMultipleTcpListeners *http_listener_;
    // HTTPS server listener, over TCP. Please note that request of both HTTP static and stream are served by this
    // listener, and it might be reused by HTTP API and WebRTC TCP.
    SrsMultipleTcpListeners *https_listener_;
    // WebRTC over TCP listener. Please note that there is always a UDP listener by RTC server.
    SrsMultipleTcpListeners *webrtc_listener_;
#ifdef SRS_RTSP
    // RTSP listener, over TCP.
    SrsMultipleTcpListeners *rtsp_listener_;
#endif
    // Stream Caster for push over HTTP-FLV.
    SrsHttpFlvListener *stream_caster_flv_listener_;
    // Stream Caster for push over MPEGTS-UDP
    SrsUdpCasterListener *stream_caster_mpegts_;
    // Exporter server listener, over TCP. Please note that metrics request of HTTP is served by this
    // listener, and it might be reused by HTTP API.
    SrsTcpListener *exporter_listener_;
#ifdef SRS_GB28181
    // Stream Caster for GB28181.
    SrsGbListener *stream_caster_gb28181_;
#endif

private:
    // SRT acceptors for MPEG-TS over SRT.
    std::vector<SrsSrtAcceptor *> srt_acceptors_;

private:
    // WebRTC UDP listeners for RTC server functionality.
    std::vector<SrsUdpMuxListener *> rtc_listeners_;
    // WebRTC session manager.
    SrsRtcSessionManager *rtc_session_manager_;

private:
    // Signal manager which convert gignal to io message.
    SrsSignalManager *signal_manager_;
    // To query the latest available version of SRS.
    SrsLatestVersion *latest_version_;
    // User send the signal, convert to variable.
    bool signal_reload_;
    bool signal_persistence_config_;
    bool signal_gmc_stop_;
    bool signal_fast_quit_;
    bool signal_gracefully_quit_;
    // Parent pid for asprocess.
    int ppid_;

public:
    SrsServer();
    virtual ~SrsServer();

private:
    // When SIGTERM, SRS should do cleanup, for example,
    // to stop all ingesters, cleanup HLS and dvr.
    virtual void dispose();
    // Close listener to stop accepting new connections,
    // then wait and quit when all connections finished.
    virtual void gracefully_dispose();

public:
    // Get the HTTP API server mux.
    ISrsHttpServeMux *api_server();

    // server startup workflow, @see run_master()
public:
    // Initialize server with callback handler ch.
    // @remark user must free the handler.
    virtual srs_error_t initialize();

public:
    srs_error_t run();

private:
    virtual srs_error_t initialize_st();
    virtual srs_error_t initialize_signal();
    virtual srs_error_t listen();
    virtual srs_error_t register_signal();
    virtual srs_error_t http_handle();
    virtual srs_error_t ingest();

public:
    void stop();

    // interface ISrsCoroutineHandler
private:
    virtual srs_error_t cycle();

    // server utilities.
public:
    // The callback for signal manager got a signal.
    // The signal manager convert signal to io message,
    // whatever, we will got the signo like the orignal signal(int signo) handler.
    // @param signo the signal number from user, where:
    //      SRS_SIGNAL_FAST_QUIT, the SIGTERM, do essential dispose then quit.
    //      SRS_SIGNAL_GRACEFULLY_QUIT, the SIGQUIT, do careful dispose then quit.
    //      SRS_SIGNAL_REOPEN_LOG, the SIGUSR1, reopen the log file.
    //      SRS_SIGNAL_RELOAD, the SIGHUP, reload the config.
    //      SRS_SIGNAL_PERSISTENCE_CONFIG, application level signal, persistence config to file.
    // @remark, for SIGINT:
    //       no gmc, fast quit, do essential dispose then quit.
    //       for gmc, set the variable signal_gmc_stop, the cycle will return and cleanup for gmc.
    // @remark, maybe the HTTP RAW API will trigger the on_signal() also.
    virtual void on_signal(int signo);

private:
    // The server thread main cycle,
    // update the global static data, for instance, the current time,
    // the cpu/mem/network statistic.
    virtual srs_error_t do_cycle();

    // interface ISrsHourGlass
private:
    virtual srs_error_t setup_ticks();
    virtual srs_error_t notify(int event, srs_utime_t interval, srs_utime_t tick);

private:
    // Resample the server kbs.
    virtual void resample_kbps();

    // SRT-related methods
    virtual srs_error_t listen_srt_mpegts();
    virtual void close_srt_listeners();
    virtual srs_error_t accept_srt_client(srs_srt_t srt_fd);
    virtual srs_error_t srt_fd_to_resource(srs_srt_t srt_fd, ISrsResource **pr);

private:
    // WebRTC-related methods
    virtual srs_error_t listen_rtc_udp();

    // Interface ISrsUdpMuxHandler
public:
    virtual srs_error_t on_udp_packet(SrsUdpMuxSocket *skt);

private:
    virtual srs_error_t listen_rtc_api();

public:
    virtual SrsRtcConnection *find_rtc_session_by_username(const std::string &ufrag);
    virtual srs_error_t create_rtc_session(SrsRtcUserConfig *ruc, SrsSdp &local_sdp, SrsRtcConnection **psession);

private:
    virtual srs_error_t srs_update_server_statistics();

    // Interface ISrsTcpHandler
public:
    virtual srs_error_t on_tcp_client(ISrsListener *listener, srs_netfd_t stfd);

private:
    virtual srs_error_t do_on_tcp_client(ISrsListener *listener, srs_netfd_t &stfd);
    virtual srs_error_t on_before_connection(const char *label, int fd, const std::string &ip, int port);

    // Interface ISrsLiveSourceHandler
public:
    virtual srs_error_t on_publish(ISrsRequest *r);
    virtual void on_unpublish(ISrsRequest *r);
};

// @global main SRS server, for debugging
extern SrsServer *_srs_server;

// Convert signal to io,
// @see: st-1.9/docs/notes.html
class SrsSignalManager : public ISrsCoroutineHandler
{
private:
    // Per-process pipe which is used as a signal queue.
    // Up to PIPE_BUF/sizeof(int) signals can be queued up.
    int sig_pipe[2];
    srs_netfd_t signal_read_stfd;

private:
    SrsServer *server;
    SrsCoroutine *trd;

public:
    SrsSignalManager(SrsServer *s);
    virtual ~SrsSignalManager();

public:
    virtual srs_error_t initialize();
    virtual srs_error_t start();
    // Interface ISrsEndlessThreadHandler.
public:
    virtual srs_error_t cycle();

private:
    // Global singleton instance
    static SrsSignalManager *instance;
    // Signal catching function.
    // Converts signal event to I/O event.
    static void sig_catcher(int signo);
};

// Auto reload by inotify.
// @see https://github.com/ossrs/srs/issues/1635
class SrsInotifyWorker : public ISrsCoroutineHandler
{
private:
    SrsServer *server;
    SrsCoroutine *trd;
    srs_netfd_t inotify_fd;

public:
    SrsInotifyWorker(SrsServer *s);
    virtual ~SrsInotifyWorker();

public:
    virtual srs_error_t start();
    // Interface ISrsEndlessThreadHandler.
public:
    virtual srs_error_t cycle();
};

// PID file manager for process identification and locking.
class SrsPidFileLocker
{
private:
    int pid_fd_;
    std::string pid_file_;

public:
    SrsPidFileLocker();
    virtual ~SrsPidFileLocker();

public:
    // Acquire the PID file for the whole process.
    virtual srs_error_t acquire();

private:
    // Close the PID file descriptor.
    virtual void close();
};

#endif
