/*
The MIT License (MIT)

Copyright (c) 2013-2014 winlin

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#ifndef SRS_APP_SERVER_HPP
#define SRS_APP_SERVER_HPP

/*
#include <srs_app_server.hpp>
*/

#include <srs_core.hpp>

#include <vector>

#include <srs_app_st.hpp>
#include <srs_app_reload.hpp>
#include <srs_app_thread.hpp>

class SrsServer;
class SrsConnection;
class SrsHttpHandler;
class SrsIngester;
class SrsHttpHeartbeat;

// listener type for server to identify the connection,
// that is, use different type to process the connection.
enum SrsListenerType 
{
    // RTMP client,
    SrsListenerRtmpStream   = 0,
    // HTTP api,
    SrsListenerHttpApi      = 1,
    // HTTP stream, HDS/HLS/DASH
    SrsListenerHttpStream   = 2
};

class SrsListener : public ISrsThreadHandler
{
public:
    SrsListenerType _type;
private:
    int fd;
    st_netfd_t stfd;
    int _port;
    SrsServer* _server;
    SrsThread* pthread;
public:
    SrsListener(SrsServer* server, SrsListenerType type);
    virtual ~SrsListener();
public:
    virtual SrsListenerType type();
    virtual int listen(int port);
// interface ISrsThreadHandler.
public:
    virtual void on_thread_start();
    virtual int cycle();
};

/**
* convert signal to io,
* @see: st-1.9/docs/notes.html
*/
class SrsSignalManager : public ISrsThreadHandler
{
private:
    /* Per-process pipe which is used as a signal queue. */
    /* Up to PIPE_BUF/sizeof(int) signals can be queued up. */
    int sig_pipe[2];
    st_netfd_t signal_read_stfd;
private:
    SrsServer* _server;
    SrsThread* pthread;
public:
    SrsSignalManager(SrsServer* server);
    virtual ~SrsSignalManager();
public:
    virtual int initialize();
    virtual int start();
// interface ISrsThreadHandler.
public:
    virtual int cycle();
private:
    // global singleton instance
    static SrsSignalManager* instance;
    /* Signal catching function. */
    /* Converts signal event to I/O event. */
    static void sig_catcher(int signo);
};

class SrsServer : public ISrsReloadHandler
{
private:
#ifdef SRS_AUTO_HTTP_API
    SrsHttpHandler* http_api_handler;
#endif
#ifdef SRS_AUTO_HTTP_SERVER
    SrsHttpHandler* http_stream_handler;
#endif
#ifdef SRS_AUTO_HTTP_PARSER
    SrsHttpHeartbeat* http_heartbeat;
#endif
#ifdef SRS_AUTO_INGEST
    SrsIngester* ingester;
#endif
private:
    int pid_fd;
    std::vector<SrsConnection*> conns;
    std::vector<SrsListener*> listeners;
    SrsSignalManager* signal_manager;
    bool signal_reload;
    bool signal_gmc_stop;
public:
    SrsServer();
    virtual ~SrsServer();
    virtual void destroy();
public:
    virtual int initialize();
    virtual int initialize_signal();
    virtual int acquire_pid_file();
    virtual int initialize_st();
    virtual int listen();
    virtual int register_signal();
    virtual int ingest();
    virtual int cycle();
    virtual void remove(SrsConnection* conn);
    virtual void on_signal(int signo);
private:
    virtual int do_cycle();
    virtual int listen_rtmp();
    virtual int listen_http_api();
    virtual int listen_http_stream();
    virtual void close_listeners(SrsListenerType type);
// internal only
public:
    virtual int accept_client(SrsListenerType type, st_netfd_t client_stfd);
// interface ISrsThreadHandler.
public:
    virtual int on_reload_listen();
    virtual int on_reload_pid();
    virtual int on_reload_vhost_added(std::string vhost);
    virtual int on_reload_vhost_removed(std::string vhost);
    virtual int on_reload_vhost_http_updated();
    virtual int on_reload_http_api_enabled();
    virtual int on_reload_http_api_disabled();
    virtual int on_reload_http_stream_enabled();
    virtual int on_reload_http_stream_disabled();
    virtual int on_reload_http_stream_updated();
};

#endif