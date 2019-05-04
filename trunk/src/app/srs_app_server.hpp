/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2019 Winlin
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

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

class SrsServer;
class SrsConnection;
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
#ifdef SRS_AUTO_KAFKA
class SrsKafkaProducer;
#endif
class SrsCoroutineManager;

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
    // TCP stream, RTSP stream.
    SrsListenerRtsp = 4,
    // TCP stream, FLV stream over HTTP.
    SrsListenerFlv = 5,
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
class SrsBufferListener : virtual public SrsListener, virtual public ISrsTcpHandler
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

// A TCP listener, for rtsp server.
class SrsRtspListener : virtual public SrsListener, virtual public ISrsTcpHandler
{
private:
    SrsTcpListener* listener;
    ISrsTcpHandler* caster;
public:
    SrsRtspListener(SrsServer* svr, SrsListenerType t, SrsConfDirective* c);
    virtual ~SrsRtspListener();
public:
    virtual srs_error_t listen(std::string i, int p);
// Interface ISrsTcpHandler
public:
    virtual srs_error_t on_tcp_client(srs_netfd_t stfd);
};

// A TCP listener, for flv stream server.
class SrsHttpFlvListener : virtual public SrsListener, virtual public ISrsTcpHandler
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
};

// SRS RTMP server, initialize and listen, start connection service thread, destroy client.
class SrsServer : virtual public ISrsReloadHandler, virtual public ISrsSourceHandler, virtual public IConnectionManager
{
private:
    // TODO: FIXME: rename to http_api
    SrsHttpServeMux* http_api_mux;
    SrsHttpServer* http_server;
    SrsHttpHeartbeat* http_heartbeat;
    SrsIngester* ingester;
    SrsCoroutineManager* conn_manager;
private:
    // The pid file fd, lock the file write when server is running.
    // @remark the init.d script should cleanup the pid file, when stop service,
    //       for the server never delete the file; when system startup, the pid in pid file
    //       maybe valid but the process is not SRS, the init.d script will never start server.
    int pid_fd;
    // All connections, connection manager
    std::vector<SrsConnection*> conns;
    // All listners, listener manager.
    std::vector<SrsListener*> listeners;
    // Signal manager which convert gignal to io message.
    SrsSignalManager* signal_manager;
    // Handle in server cycle.
    ISrsServerCycle* handler;
    // User send the signal, convert to variable.
    bool signal_reload;
    bool signal_persistence_config;
    bool signal_gmc_stop;
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
    virtual srs_error_t cycle();
// server utilities.
public:
    // The callback for signal manager got a signal.
    // The signal manager convert signal to io message,
    // whatever, we will got the signo like the orignal signal(int signo) handler.
    // @param signo the signal number from user, where:
    //      SRS_SIGNAL_GRACEFULLY_QUIT, the SIGTERM, dispose then quit.
    //      SRS_SIGNAL_REOPEN_LOG, the SIGUSR1, reopen the log file.
    //      SRS_SIGNAL_RELOAD, the SIGHUP, reload the config.
    //      SRS_SIGNAL_PERSISTENCE_CONFIG, application level signal, persistence config to file.
    // @remark, for SIGINT:
    //       no gmc, directly exit.
    //       for gmc, set the variable signal_gmc_stop, the cycle will return and cleanup for gmc.
    // @remark, maybe the HTTP RAW API will trigger the on_signal() also.
    virtual void on_signal(int signo);
private:
    // The server thread main cycle,
    // update the global static data, for instance, the current time,
    // the cpu/mem/network statistic.
    virtual srs_error_t do_cycle();
    // listen at specified protocol.
    virtual srs_error_t listen_rtmp();
    virtual srs_error_t listen_http_api();
    virtual srs_error_t listen_http_stream();
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
private:
    virtual srs_error_t fd2conn(SrsListenerType type, srs_netfd_t stfd, SrsConnection** pconn);
// Interface IConnectionManager
public:
    // A callback for connection to remove itself.
    // When connection thread cycle terminated, callback this to delete connection.
    // @see SrsConnection.on_thread_stop().
    virtual void remove(ISrsConnection* c);
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
// Interface ISrsSourceHandler
public:
    virtual srs_error_t on_publish(SrsSource* s, SrsRequest* r);
    virtual void on_unpublish(SrsSource* s, SrsRequest* r);
};

#endif

