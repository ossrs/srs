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
    SrsListenerType type;
private:
    int fd;
    st_netfd_t stfd;
    int port;
    SrsServer* server;
    SrsThread* pthread;
public:
    SrsListener(SrsServer* _server, SrsListenerType _type);
    virtual ~SrsListener();
public:
    virtual int listen(int port);
// interface ISrsThreadHandler.
public:
    virtual void on_thread_start();
    virtual int cycle();
};

class SrsServer : public ISrsReloadHandler
{
    friend class SrsListener;
private:
#ifdef SRS_HTTP_API
    SrsHttpHandler* http_api_handler;
#endif
#ifdef SRS_HTTP_SERVER
    SrsHttpHandler* http_stream_handler;
#endif
#ifdef SRS_INGEST
    SrsIngester* ingester;
#endif
private:
    int pid_fd;
    std::vector<SrsConnection*> conns;
    std::vector<SrsListener*> listeners;
    bool signal_reload;
    bool signal_gmc_stop;
public:
    SrsServer();
    virtual ~SrsServer();
public:
    virtual int initialize();
    virtual int acquire_pid_file();
    virtual int initialize_st();
    virtual int listen();
    virtual int ingest();
    virtual int cycle();
    virtual void remove(SrsConnection* conn);
    virtual void on_signal(int signo);
private:
    virtual void close_listeners();
    virtual int accept_client(SrsListenerType type, st_netfd_t client_stfd);
// interface ISrsThreadHandler.
public:
    virtual int on_reload_listen();
    virtual int on_reload_pid();
};

#endif