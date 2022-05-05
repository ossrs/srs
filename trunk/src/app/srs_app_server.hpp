//
// Copyright (c) 2013-2021 Winlin
//
// SPDX-License-Identifier: MIT
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
#include <srs_service_st.hpp>
#include <srs_app_hourglass.hpp>

class SrsServer;
class SrsHttpServeMux;
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

// The listener type for server to identify the connection,
// that is, use different type to process the connection.
enum SrsListenerType
{
    // RTMP client,
    SrsListenerRtmpStream = 0,
    // HTTP api,
    SrsListenerHttpApi = 1,
    // HTTP stream, HDS/HLS/DASH
    SrsListenerHttpStream = 2,
    // UDP stream, MPEG-TS over udp.
    SrsListenerMpegTsOverUdp = 3,
    // TCP stream, FLV stream over HTTP.
    SrsListenerFlv = 5,
    // HTTPS api,
    SrsListenerHttpsApi = 8,
    // HTTPS stream,
    SrsListenerHttpsStream = 9,
};

// A common tcp listener, for RTMP/HTTP server.
class SrsListener
{
protected:
    SrsListenerType type;
protected:
    std::string ip;
    int port;
    SrsServer* server;
public:
    SrsListener(SrsServer* svr, SrsListenerType t);
    virtual ~SrsListener();
public:
    virtual SrsListenerType listen_type();
    virtual srs_error_t listen(std::string i, int p) = 0;
};

// A buffered TCP listener.
class SrsBufferListener : public SrsListener, public ISrsTcpHandler
{
private:
    SrsTcpListener* listener;
public:
    SrsBufferListener(SrsServer* server, SrsListenerType type);
    virtual ~SrsBufferListener();
public:
    virtual srs_error_t listen(std::string ip, int port);
// Interface ISrsTcpHandler
public:
    virtual srs_error_t on_tcp_client(srs_netfd_t stfd);
};

// A TCP listener, for flv stream server.
class SrsHttpFlvListener : public SrsListener, public ISrsTcpHandler
{
private:
    SrsTcpListener* listener;
    SrsAppCasterFlv* caster;
public:
    SrsHttpFlvListener(SrsServer* svr, SrsListenerType t, SrsConfDirective* c);
    virtual ~SrsHttpFlvListener();
public:
    virtual srs_error_t listen(std::string i, int p);
// Interface ISrsTcpHandler
public:
    virtual srs_error_t on_tcp_client(srs_netfd_t stfd);
};

// A UDP listener, for udp server.
class SrsUdpStreamListener : public SrsListener
{
protected:
    SrsUdpListener* listener;
    ISrsUdpHandler* caster;
public:
    SrsUdpStreamListener(SrsServer* svr, SrsListenerType t, ISrsUdpHandler* c);
    virtual ~SrsUdpStreamListener();
public:
    virtual srs_error_t listen(std::string i, int p);
};

// A UDP listener, for udp stream caster server.
class SrsUdpCasterListener : public SrsUdpStreamListener
{
public:
    SrsUdpCasterListener(SrsServer* svr, SrsListenerType t, SrsConfDirective* c);
    virtual ~SrsUdpCasterListener();
};

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

// A handler to the handle cycle in SRS RTMP server.
class ISrsServerCycle
{
public:
    ISrsServerCycle();
    virtual ~ISrsServerCycle();
public:
    // Initialize the cycle handler.
    virtual srs_error_t initialize() = 0;
    // Do on_cycle while server doing cycle.
    virtual srs_error_t on_cycle() = 0;
    // Callback the handler when got client.
    virtual srs_error_t on_accept_client(int max, int cur) = 0;
    // Callback for logrotate.
    virtual void on_logrotate() = 0;
};

// TODO: FIXME: Rename to SrsLiveServer.
// SRS RTMP server, initialize and listen, start connection service thread, destroy client.
class SrsServer : public ISrsReloadHandler, public ISrsLiveSourceHandler
    , public ISrsResourceManager, public ISrsCoroutineHandler
    , public ISrsHourGlass
{
private:
    // TODO: FIXME: Extract an HttpApiServer.
    SrsHttpServeMux* http_api_mux;
    SrsHttpServer* http_server;
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
    // All listners, listener manager.
    std::vector<SrsListener*> listeners;
    // Signal manager which convert gignal to io message.
    SrsSignalManager* signal_manager;
    // To query the latest available version of SRS.
    SrsLatestVersion* latest_version_;
    // Handle in server cycle.
    ISrsServerCycle* handler;
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
    virtual srs_error_t initialize(ISrsServerCycle* ch);
    virtual srs_error_t initialize_st();
    virtual srs_error_t initialize_signal();
    virtual srs_error_t acquire_pid_file();
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
    // listen at specified protocol.
    virtual srs_error_t listen_rtmp();
    virtual srs_error_t listen_http_api();
    virtual srs_error_t listen_https_api();
    virtual srs_error_t listen_http_stream();
    virtual srs_error_t listen_https_stream();
    virtual srs_error_t listen_stream_caster();
    // Close the listeners for specified type,
    // Remove the listen object from manager.
    virtual void close_listeners(SrsListenerType type);
    // Resample the server kbs.
    virtual void resample_kbps();
// For internal only
public:
    // When listener got a fd, notice server to accept it.
    // @param type, the client type, used to create concrete connection,
    //       for instance RTMP connection to serve client.
    // @param stfd, the client fd in st boxed, the underlayer fd.
    virtual srs_error_t accept_client(SrsListenerType type, srs_netfd_t stfd);
    // TODO: FIXME: Fetch from hybrid server manager.
    virtual SrsHttpServeMux* api_server();
private:
    virtual srs_error_t fd_to_resource(SrsListenerType type, srs_netfd_t stfd, ISrsStartableConneciton** pr);
// Interface ISrsResourceManager
public:
    // A callback for connection to remove itself.
    // When connection thread cycle terminated, callback this to delete connection.
    // @see SrsTcpConnection.on_thread_stop().
    virtual void remove(ISrsResource* c);
// Interface ISrsReloadHandler.
public:
    virtual srs_error_t on_reload_listen();
    virtual srs_error_t on_reload_pid();
    virtual srs_error_t on_reload_vhost_added(std::string vhost);
    virtual srs_error_t on_reload_vhost_removed(std::string vhost);
    virtual srs_error_t on_reload_http_api_enabled();
    virtual srs_error_t on_reload_http_api_disabled();
    virtual srs_error_t on_reload_http_stream_enabled();
    virtual srs_error_t on_reload_http_stream_disabled();
    virtual srs_error_t on_reload_http_stream_updated();
// Interface ISrsLiveSourceHandler
public:
    virtual srs_error_t on_publish(SrsLiveSource* s, SrsRequest* r);
    virtual void on_unpublish(SrsLiveSource* s, SrsRequest* r);
};

#endif

