//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#ifndef SRS_APP_SERVER_HPP
#define SRS_APP_SERVER_HPP

#include <srs_core.hpp>

#include <vector>
#include <string>

#include <srs_app_st.hpp>
#include <srs_app_reload.hpp>
#include <srs_app_source.hpp>
#include <srs_app_hls.hpp>
#include <srs_app_listener.hpp>
#include <srs_app_conn.hpp>
#include <srs_protocol_st.hpp>
#include <srs_app_hourglass.hpp>
#include <srs_app_hybrid.hpp>

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
class SrsWaitGroup;
class SrsMultipleTcpListeners;
class SrsHttpFlvListener;
class SrsUdpCasterListener;
class SrsGbListener;

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
    SrsServer* server;
    SrsCoroutine* trd;
public:
    SrsSignalManager(SrsServer* s);
    virtual ~SrsSignalManager();
public:
    virtual srs_error_t initialize();
    virtual srs_error_t start();
// Interface ISrsEndlessThreadHandler.
public:
    virtual srs_error_t cycle();
private:
    // Global singleton instance
    static SrsSignalManager* instance;
    // Signal catching function.
    // Converts signal event to I/O event.
    static void sig_catcher(int signo);
};

// Auto reload by inotify.
// @see https://github.com/ossrs/srs/issues/1635
class SrsInotifyWorker : public ISrsCoroutineHandler
{
private:
    SrsServer* server;
    SrsCoroutine* trd;
    srs_netfd_t inotify_fd;
public:
    SrsInotifyWorker(SrsServer* s);
    virtual ~SrsInotifyWorker();
public:
    virtual srs_error_t start();
// Interface ISrsEndlessThreadHandler.
public:
    virtual srs_error_t cycle();
};

// TODO: FIXME: Rename to SrsLiveServer.
// SRS RTMP server, initialize and listen, start connection service thread, destroy client.
class SrsServer : public ISrsReloadHandler, public ISrsLiveSourceHandler, public ISrsTcpHandler
    , public ISrsResourceManager, public ISrsCoroutineHandler, public ISrsHourGlass
{
private:
    // TODO: FIXME: Extract an HttpApiServer.
    ISrsHttpServeMux* http_api_mux;
    SrsHttpServer* http_server;
private:
    SrsHttpHeartbeat* http_heartbeat;
    SrsIngester* ingester;
    SrsResourceManager* conn_manager;
    SrsCoroutine* trd_;
    SrsHourGlass* timer_;
    SrsWaitGroup* wg_;
private:
    // The pid file fd, lock the file write when server is running.
    // @remark the init.d script should cleanup the pid file, when stop service,
    //       for the server never delete the file; when system startup, the pid in pid file
    //       maybe valid but the process is not SRS, the init.d script will never start server.
    int pid_fd;
private:
    // If reusing, HTTP API use the same port of HTTP server.
    bool reuse_api_over_server_;
    // If reusing, WebRTC TCP use the same port of HTTP server.
    bool reuse_rtc_over_server_;
    // RTMP stream listeners, over TCP.
    SrsMultipleTcpListeners* rtmp_listener_;
    // HTTP API listener, over TCP. Please note that it might reuse with stream listener.
    SrsTcpListener* api_listener_;
    // HTTPS API listener, over TCP. Please note that it might reuse with stream listener.
    SrsTcpListener* apis_listener_;
    // HTTP server listener, over TCP. Please note that request of both HTTP static and stream are served by this
    // listener, and it might be reused by HTTP API and WebRTC TCP.
    SrsTcpListener* http_listener_;
    // HTTPS server listener, over TCP. Please note that request of both HTTP static and stream are served by this
    // listener, and it might be reused by HTTP API and WebRTC TCP.
    SrsTcpListener* https_listener_;
    // WebRTC over TCP listener. Please note that there is always a UDP listener by RTC server.
    SrsTcpListener* webrtc_listener_;
    // Stream Caster for push over HTTP-FLV.
    SrsHttpFlvListener* stream_caster_flv_listener_;
    // Stream Caster for push over MPEGTS-UDP
    SrsUdpCasterListener* stream_caster_mpegts_;
    // Exporter server listener, over TCP. Please note that metrics request of HTTP is served by this
    // listener, and it might be reused by HTTP API.
    SrsTcpListener* exporter_listener_;
#ifdef SRS_GB28181
    // Stream Caster for GB28181.
    SrsGbListener* stream_caster_gb28181_;
#endif
private:
    // Signal manager which convert gignal to io message.
    SrsSignalManager* signal_manager;
    // To query the latest available version of SRS.
    SrsLatestVersion* latest_version_;
    // User send the signal, convert to variable.
    bool signal_reload;
    bool signal_persistence_config;
    bool signal_gmc_stop;
    bool signal_fast_quit;
    bool signal_gracefully_quit;
    // Parent pid for asprocess.
    int ppid;
public:
    SrsServer();
    virtual ~SrsServer();
private:
    // The destroy is for gmc to analysis the memory leak,
    // if not destroy global/static data, the gmc will warning memory leak.
    // In service, server never destroy, directly exit when restart.
    virtual void destroy();
    // When SIGTERM, SRS should do cleanup, for example,
    // to stop all ingesters, cleanup HLS and dvr.
    virtual void dispose();
    // Close listener to stop accepting new connections,
    // then wait and quit when all connections finished.
    virtual void gracefully_dispose();
// server startup workflow, @see run_master()
public:
    // Initialize server with callback handler ch.
    // @remark user must free the handler.
    virtual srs_error_t initialize();
    virtual srs_error_t initialize_st();
    virtual srs_error_t initialize_signal();
    virtual srs_error_t listen();
    virtual srs_error_t register_signal();
    virtual srs_error_t http_handle();
    virtual srs_error_t ingest();
public:
    virtual srs_error_t start(SrsWaitGroup* wg);
    void stop();
// interface ISrsCoroutineHandler
public:
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
// For internal only
public:
    // TODO: FIXME: Fetch from hybrid server manager.
    virtual ISrsHttpServeMux* api_server();
// Interface ISrsTcpHandler
public:
    virtual srs_error_t on_tcp_client(ISrsListener* listener, srs_netfd_t stfd);
private:
    virtual srs_error_t do_on_tcp_client(ISrsListener* listener, srs_netfd_t& stfd);
    virtual srs_error_t on_before_connection(srs_netfd_t& stfd, const std::string& ip, int port);
// Interface ISrsResourceManager
public:
    // A callback for connection to remove itself.
    // When connection thread cycle terminated, callback this to delete connection.
    // @see SrsTcpConnection.on_thread_stop().
    virtual void remove(ISrsResource* c);
// Interface ISrsReloadHandler.
public:
    virtual srs_error_t on_reload_listen();
// Interface ISrsLiveSourceHandler
public:
    virtual srs_error_t on_publish(SrsLiveSource* s, SrsRequest* r);
    virtual void on_unpublish(SrsLiveSource* s, SrsRequest* r);
};

// The SRS server adapter, the master server.
class SrsServerAdapter : public ISrsHybridServer
{
private:
    SrsServer* srs;
public:
    SrsServerAdapter();
    virtual ~SrsServerAdapter();
public:
    virtual srs_error_t initialize();
    virtual srs_error_t run(SrsWaitGroup* wg);
    virtual void stop();
public:
    virtual SrsServer* instance();
};

#endif

