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

#include <srs_app_server.hpp>

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <algorithm>

#include <srs_kernel_log.hpp>
#include <srs_kernel_error.hpp>
#include <srs_app_rtmp_conn.hpp>
#include <srs_app_config.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_app_http_api.hpp>
#include <srs_app_http_conn.hpp>
#include <srs_app_http.hpp>
#ifdef SRS_INGEST
#include <srs_app_ingest.hpp>
#endif

#define SERVER_LISTEN_BACKLOG 512
#define SRS_TIME_RESOLUTION_MS 500

SrsListener::SrsListener(SrsServer* _server, SrsListenerType _type)
{
    fd = -1;
    stfd = NULL;
    
    port = 0;
    server = _server;
    type = _type;

    pthread = new SrsThread(this, 0);
}

SrsListener::~SrsListener()
{
    srs_close_stfd(stfd);
    
    pthread->stop();
    srs_freep(pthread);
    
    // st does not close it sometimes, 
    // close it manually.
    close(fd);
}

int SrsListener::listen(int _port)
{
    int ret = ERROR_SUCCESS;
    
    port = _port;
    
    if ((fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        ret = ERROR_SOCKET_CREATE;
        srs_error("create linux socket error. ret=%d", ret);
        return ret;
    }
    srs_verbose("create linux socket success. fd=%d", fd);
    
    int reuse_socket = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse_socket, sizeof(int)) == -1) {
        ret = ERROR_SOCKET_SETREUSE;
        srs_error("setsockopt reuse-addr error. ret=%d", ret);
        return ret;
    }
    srs_verbose("setsockopt reuse-addr success. fd=%d", fd);
    
    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(fd, (const sockaddr*)&addr, sizeof(sockaddr_in)) == -1) {
        ret = ERROR_SOCKET_BIND;
        srs_error("bind socket error. ret=%d", ret);
        return ret;
    }
    srs_verbose("bind socket success. fd=%d", fd);
    
    if (::listen(fd, SERVER_LISTEN_BACKLOG) == -1) {
        ret = ERROR_SOCKET_LISTEN;
        srs_error("listen socket error. ret=%d", ret);
        return ret;
    }
    srs_verbose("listen socket success. fd=%d", fd);
    
    if ((stfd = st_netfd_open_socket(fd)) == NULL){
        ret = ERROR_ST_OPEN_SOCKET;
        srs_error("st_netfd_open_socket open socket failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("st open socket success. fd=%d", fd);
    
    if ((ret = pthread->start()) != ERROR_SUCCESS) {
        srs_error("st_thread_create listen thread error. ret=%d", ret);
        return ret;
    }
    srs_verbose("create st listen thread success.");
    
    srs_trace("server started, listen at port=%d, type=%d, fd=%d", port, type, fd);
    
    return ret;
}

void SrsListener::on_thread_start()
{
    srs_trace("listen cycle start, port=%d, type=%d, fd=%d", port, type, fd);
}

int SrsListener::cycle()
{
    int ret = ERROR_SUCCESS;
    
    st_netfd_t client_stfd = st_accept(stfd, NULL, NULL, ST_UTIME_NO_TIMEOUT);
    
    if(client_stfd == NULL){
        // ignore error.
        srs_warn("ignore accept thread stoppped for accept client error");
        return ret;
    }
    srs_verbose("get a client. fd=%d", st_netfd_fileno(client_stfd));
    
    if ((ret = server->accept_client(type, client_stfd)) != ERROR_SUCCESS) {
        srs_warn("accept client error. ret=%d", ret);
        return ret;
    }
    
    return ret;
}

SrsServer::SrsServer()
{
    signal_reload = false;
    signal_gmc_stop = false;
    
    srs_assert(_srs_config);
    _srs_config->subscribe(this);
    
#ifdef SRS_HTTP_API
    http_api_handler = NULL;
#endif
#ifdef SRS_HTTP_SERVER
    http_stream_handler = NULL;
#endif
#ifdef SRS_INGEST
    ingester = NULL;
#endif
}

SrsServer::~SrsServer()
{
    _srs_config->unsubscribe(this);
    
    if (true) {
        std::vector<SrsConnection*>::iterator it;
        for (it = conns.begin(); it != conns.end(); ++it) {
            SrsConnection* conn = *it;
            srs_freep(conn);
        }
        conns.clear();
    }
    
    close_listeners();
    
#ifdef SRS_HTTP_API
    srs_freep(http_api_handler);
#endif

#ifdef SRS_HTTP_SERVER
    srs_freep(http_stream_handler);
#endif
#ifdef SRS_INGEST
    srs_freep(ingester);
#endif
}

int SrsServer::initialize()
{
    int ret = ERROR_SUCCESS;
    
#ifdef SRS_HTTP_API
    srs_assert(!http_api_handler);
    http_api_handler = SrsHttpHandler::create_http_api();
#endif
#ifdef SRS_HTTP_SERVER
    srs_assert(!http_stream_handler);
    http_stream_handler = SrsHttpHandler::create_http_stream();
#endif
#ifdef SRS_INGEST
    srs_assert(!ingester);
    ingester = new SrsIngester();
#endif
    
#ifdef SRS_HTTP_API
    if ((ret = http_api_handler->initialize()) != ERROR_SUCCESS) {
        return ret;
    }
#endif

#ifdef SRS_HTTP_SERVER
    if ((ret = http_stream_handler->initialize()) != ERROR_SUCCESS) {
        return ret;
    }
#endif

    return ret;
}

int SrsServer::acquire_pid_file()
{
    int ret = ERROR_SUCCESS;
    
    std::string pid_file = _srs_config->get_pid_file();
    
    // -rw-r--r-- 
    // 644
    int mode = S_IRUSR | S_IWUSR |  S_IRGRP | S_IROTH;
    
    int fd;
    // open pid file
    if ((fd = ::open(pid_file.c_str(), O_WRONLY | O_CREAT, mode)) < 0) {
        ret = ERROR_SYSTEM_PID_ACQUIRE;
        srs_error("open pid file %s error, ret=%#x", pid_file.c_str(), ret);
        return ret;
    }
    
    // require write lock
    flock lock;

    lock.l_type = F_WRLCK; // F_RDLCK, F_WRLCK, F_UNLCK
    lock.l_start = 0; // type offset, relative to l_whence
    lock.l_whence = SEEK_SET;  // SEEK_SET, SEEK_CUR, SEEK_END
    lock.l_len = 0;
    
    if (fcntl(fd, F_SETLK, &lock) < 0) {
        if(errno == EACCES || errno == EAGAIN) {
            ret = ERROR_SYSTEM_PID_ALREADY_RUNNING;
            srs_error("srs is already running! ret=%#x", ret);
            return ret;
        }
        
        ret = ERROR_SYSTEM_PID_LOCK;
        srs_error("require lock for file %s error! ret=%#x", pid_file.c_str(), ret);
        return ret;
    }

    // truncate file
    if (ftruncate(fd, 0) < 0) {
        ret = ERROR_SYSTEM_PID_TRUNCATE_FILE;
        srs_error("truncate pid file %s error! ret=%#x", pid_file.c_str(), ret);
        return ret;
    }

    int pid = (int)getpid();
    
    // write the pid
    char buf[512];
    snprintf(buf, sizeof(buf), "%d", pid);
    if (write(fd, buf, strlen(buf)) != (int)strlen(buf)) {
        ret = ERROR_SYSTEM_PID_WRITE_FILE;
        srs_error("write our pid error! pid=%d file=%s ret=%#x", pid, pid_file.c_str(), ret);
        return ret;
    }

    // auto close when fork child process.
    int val;
    if ((val = fcntl(fd, F_GETFD, 0)) < 0) {
        ret = ERROR_SYSTEM_PID_GET_FILE_INFO;
        srs_error("fnctl F_GETFD error! file=%s ret=%#x", pid_file.c_str(), ret);
        return ret;
    }
    val |= FD_CLOEXEC;
    if (fcntl(fd, F_SETFD, val) < 0) {
        ret = ERROR_SYSTEM_PID_SET_FILE_INFO;
        srs_error("fcntl F_SETFD error! file=%s ret=%#x", pid_file.c_str(), ret);
        return ret;
    }
    
    srs_trace("write pid=%d to %s success!", pid, pid_file.c_str());
    
    return ret;
}

int SrsServer::initialize_st()
{
    int ret = ERROR_SUCCESS;
    
    // use linux epoll.
    if (st_set_eventsys(ST_EVENTSYS_ALT) == -1) {
        ret = ERROR_ST_SET_EPOLL;
        srs_error("st_set_eventsys use linux epoll failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("st_set_eventsys use linux epoll success");
    
    if(st_init() != 0){
        ret = ERROR_ST_INITIALIZE;
        srs_error("st_init failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("st_init success");
    
    // set current log id.
    _srs_context->generate_id();
    srs_info("log set id success");
    
    return ret;
}

int SrsServer::listen()
{
    int ret = ERROR_SUCCESS;
    
    SrsConfDirective* conf = NULL;
    
    // stream service port.
    conf = _srs_config->get_listen();
    srs_assert(conf);
    
    close_listeners();
    
    for (int i = 0; i < (int)conf->args.size(); i++) {
        SrsListener* listener = new SrsListener(this, SrsListenerRtmpStream);
        listeners.push_back(listener);
        
        int port = ::atoi(conf->args.at(i).c_str());
        if ((ret = listener->listen(port)) != ERROR_SUCCESS) {
            srs_error("RTMP stream listen at port %d failed. ret=%d", port, ret);
            return ret;
        }
    }

#ifdef SRS_HTTP_API
    if (_srs_config->get_http_api_enabled()) {
        SrsListener* listener = new SrsListener(this, SrsListenerHttpApi);
        listeners.push_back(listener);
        
        int port = _srs_config->get_http_api_listen();
        if ((ret = listener->listen(port)) != ERROR_SUCCESS) {
            srs_error("HTTP api listen at port %d failed. ret=%d", port, ret);
            return ret;
        }
    }
#endif
    
#ifdef SRS_HTTP_SERVER
    if (_srs_config->get_http_stream_enabled()) {
        SrsListener* listener = new SrsListener(this, SrsListenerHttpStream);
        listeners.push_back(listener);
        
        int port = _srs_config->get_http_stream_listen();
        if ((ret = listener->listen(port)) != ERROR_SUCCESS) {
            srs_error("HTTP stream listen at port %d failed. ret=%d", port, ret);
            return ret;
        }
    }
#endif
    
    return ret;
}

int SrsServer::ingest()
{
    int ret = ERROR_SUCCESS;
    
#ifdef SRS_INGEST
    if ((ret = ingester->start()) != ERROR_SUCCESS) {
        srs_error("start ingest streams failed. ret=%d", ret);
        return ret;
    }
#endif

    return ret;
}

int SrsServer::cycle()
{
    int ret = ERROR_SUCCESS;
    
    // the deamon thread, update the time cache
    while (true) {
        st_usleep(SRS_TIME_RESOLUTION_MS * 1000);
        srs_update_system_time_ms();
        
// for gperf heap checker,
// @see: research/gperftools/heap-checker/heap_checker.cc
// if user interrupt the program, exit to check mem leak.
// but, if gperf, use reload to ensure main return normally,
// because directly exit will cause core-dump.
#ifdef SRS_GPERF_MC
        if (signal_gmc_stop) {
            break;
        }
#endif
        
        if (signal_reload) {
            signal_reload = false;
            srs_info("get signal reload, to reload the config.");
            
            if ((ret = _srs_config->reload()) != ERROR_SUCCESS) {
                srs_error("reload config failed. ret=%d", ret);
                return ret;
            }
            srs_trace("reload config success.");
        }
    }

#ifdef SRS_INGEST
    ingester->stop();
#endif
    
    return ret;
}

void SrsServer::remove(SrsConnection* conn)
{
    std::vector<SrsConnection*>::iterator it = std::find(conns.begin(), conns.end(), conn);
    
    if (it != conns.end()) {
        conns.erase(it);
    }
    
    srs_info("conn removed. conns=%d", (int)conns.size());
    
    // all connections are created by server,
    // so we free it here.
    srs_freep(conn);
}

void SrsServer::on_signal(int signo)
{
    if (signo == SIGNAL_RELOAD) {
        signal_reload = true;
        return;
    }
    
    if (signo == SIGINT) {
#ifdef SRS_GPERF_MC
        srs_trace("gmc is on, main cycle will terminate normally.");
        signal_gmc_stop = true;
#else
        srs_trace("user terminate program");
        exit(0);
#endif
        return;
    }
    
    if (signo == SIGTERM) {
        srs_trace("user terminate program");
        exit(0);
        return;
    }
}

void SrsServer::close_listeners()
{
    std::vector<SrsListener*>::iterator it;
    for (it = listeners.begin(); it != listeners.end(); ++it) {
        SrsListener* listener = *it;
        srs_freep(listener);
    }
    listeners.clear();
}

int SrsServer::accept_client(SrsListenerType type, st_netfd_t client_stfd)
{
    int ret = ERROR_SUCCESS;
    
    int max_connections = _srs_config->get_max_connections();
    if ((int)conns.size() >= max_connections) {
        int fd = st_netfd_fileno(client_stfd);
        
        srs_error("exceed the max connections, drop client: "
            "clients=%d, max=%d, fd=%d", (int)conns.size(), max_connections, fd);
            
        srs_close_stfd(client_stfd);
        
        return ret;
    }
    
    SrsConnection* conn = NULL;
    if (type == SrsListenerRtmpStream) {
        conn = new SrsRtmpConn(this, client_stfd);
    } else if (type == SrsListenerHttpApi) {
#ifdef SRS_HTTP_API
        conn = new SrsHttpApi(this, client_stfd, http_api_handler);
#else
        srs_warn("close http client for server not support http-api");
        srs_close_stfd(client_stfd);
        return ret;
#endif
    } else if (type == SrsListenerHttpStream) {
#ifdef SRS_HTTP_SERVER
        conn = new SrsHttpConn(this, client_stfd, http_stream_handler);
#else
        srs_warn("close http client for server not support http-server");
        srs_close_stfd(client_stfd);
        return ret;
#endif
    } else {
        // TODO: FIXME: handler others
    }
    srs_assert(conn);
    
    // directly enqueue, the cycle thread will remove the client.
    conns.push_back(conn);
    srs_verbose("add conn to vector.");
    
    // cycle will start process thread and when finished remove the client.
    if ((ret = conn->start()) != ERROR_SUCCESS) {
        return ret;
    }
    srs_verbose("conn started success   .");

    srs_verbose("accept client finished. conns=%d, ret=%d", (int)conns.size(), ret);
    
    return ret;
}

int SrsServer::on_reload_listen()
{
    return listen();
}
