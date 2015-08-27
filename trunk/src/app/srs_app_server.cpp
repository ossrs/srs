/*
The MIT License (MIT)

Copyright (c) 2013-2015 SRS(simple-rtmp-server)

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
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <algorithm>
using namespace std;

#include <srs_kernel_log.hpp>
#include <srs_kernel_error.hpp>
#include <srs_app_rtmp_conn.hpp>
#include <srs_app_config.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_app_http_api.hpp>
#include <srs_app_http_conn.hpp>
#include <srs_app_ingest.hpp>
#include <srs_app_source.hpp>
#include <srs_app_utility.hpp>
#include <srs_app_heartbeat.hpp>
#include <srs_app_mpegts_udp.hpp>
#include <srs_app_rtsp.hpp>
#include <srs_app_statistic.hpp>
#include <srs_app_caster_flv.hpp>
#include <srs_core_mem_watch.hpp>
#include <srs_kernel_consts.hpp>

// system interval in ms,
// all resolution times should be times togother,
// for example, system-interval is x=1s(1000ms),
// then rusage can be 3*x, for instance, 3*1=3s,
// the meminfo canbe 6*x, for instance, 6*1=6s,
// for performance refine, @see: https://github.com/simple-rtmp-server/srs/issues/194
// @remark, recomment to 1000ms.
#define SRS_SYS_CYCLE_INTERVAL 1000

// update time interval:
//      SRS_SYS_CYCLE_INTERVAL * SRS_SYS_TIME_RESOLUTION_MS_TIMES
// @see SYS_TIME_RESOLUTION_US
#define SRS_SYS_TIME_RESOLUTION_MS_TIMES 1

// update rusage interval:
//      SRS_SYS_CYCLE_INTERVAL * SRS_SYS_RUSAGE_RESOLUTION_TIMES
#define SRS_SYS_RUSAGE_RESOLUTION_TIMES 3

// update network devices info interval:
//      SRS_SYS_CYCLE_INTERVAL * SRS_SYS_NETWORK_RTMP_SERVER_RESOLUTION_TIMES
#define SRS_SYS_NETWORK_RTMP_SERVER_RESOLUTION_TIMES 3

// update rusage interval:
//      SRS_SYS_CYCLE_INTERVAL * SRS_SYS_CPU_STAT_RESOLUTION_TIMES
#define SRS_SYS_CPU_STAT_RESOLUTION_TIMES 3

// update the disk iops interval:
//      SRS_SYS_CYCLE_INTERVAL * SRS_SYS_DISK_STAT_RESOLUTION_TIMES
#define SRS_SYS_DISK_STAT_RESOLUTION_TIMES 6

// update rusage interval:
//      SRS_SYS_CYCLE_INTERVAL * SRS_SYS_MEMINFO_RESOLUTION_TIMES
#define SRS_SYS_MEMINFO_RESOLUTION_TIMES 6

// update platform info interval:
//      SRS_SYS_CYCLE_INTERVAL * SRS_SYS_PLATFORM_INFO_RESOLUTION_TIMES
#define SRS_SYS_PLATFORM_INFO_RESOLUTION_TIMES 9

// update network devices info interval:
//      SRS_SYS_CYCLE_INTERVAL * SRS_SYS_NETWORK_DEVICE_RESOLUTION_TIMES
#define SRS_SYS_NETWORK_DEVICE_RESOLUTION_TIMES 9

std::string srs_listener_type2string(SrsListenerType type) 
{
    switch (type) {
    case SrsListenerRtmpStream:
        return "RTMP";
    case SrsListenerHttpApi:
        return "HTTP-API";
    case SrsListenerHttpStream:
        return "HTTP-Server";
    case SrsListenerMpegTsOverUdp:
        return "MPEG-TS over UDP";
    case SrsListenerRtsp:
        return "RTSP";
    case SrsListenerFlv:
        return "HTTP-FLV";
    default:
        return "UNKONWN";
    }
}

SrsListener::SrsListener(SrsServer* svr, SrsListenerType t)
{
    port = 0;
    server = svr;
    type = t;
}

SrsListener::~SrsListener()
{
}

SrsListenerType SrsListener::listen_type()
{
    return type;
}

SrsStreamListener::SrsStreamListener(SrsServer* svr, SrsListenerType t) : SrsListener(svr, t)
{
    listener = NULL;
}

SrsStreamListener::~SrsStreamListener()
{
    srs_freep(listener);
}

int SrsStreamListener::listen(string i, int p)
{
    int ret = ERROR_SUCCESS;
    
    ip = i;
    port = p;

    srs_freep(listener);
    listener = new SrsTcpListener(this, ip, port);

    if ((ret = listener->listen()) != ERROR_SUCCESS) {
        srs_error("tcp listen failed. ret=%d", ret);
        return ret;
    }
    
    srs_info("listen thread current_cid=%d, "
        "listen at port=%d, type=%d, fd=%d started success, ep=%s:%d",
        _srs_context->get_id(), p, type, listener->fd(), i.c_str(), p);

    srs_trace("%s listen at tcp://%s:%d, fd=%d", srs_listener_type2string(type).c_str(), ip.c_str(), port, listener->fd());

    return ret;
}

int SrsStreamListener::on_tcp_client(st_netfd_t stfd)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = server->accept_client(type, stfd)) != ERROR_SUCCESS) {
        srs_warn("accept client error. ret=%d", ret);
        return ret;
    }

    return ret;
}

#ifdef SRS_AUTO_STREAM_CASTER
SrsRtspListener::SrsRtspListener(SrsServer* svr, SrsListenerType t, SrsConfDirective* c) : SrsListener(svr, t)
{
    listener = NULL;

    // the caller already ensure the type is ok,
    // we just assert here for unknown stream caster.
    srs_assert(type == SrsListenerRtsp);
    if (type == SrsListenerRtsp) {
        caster = new SrsRtspCaster(c);
    }
}

SrsRtspListener::~SrsRtspListener()
{
    srs_freep(caster);
    srs_freep(listener);
}

int SrsRtspListener::listen(string i, int p)
{
    int ret = ERROR_SUCCESS;

    // the caller already ensure the type is ok,
    // we just assert here for unknown stream caster.
    srs_assert(type == SrsListenerRtsp);
    
    ip = i;
    port = p;

    srs_freep(listener);
    listener = new SrsTcpListener(this, ip, port);

    if ((ret = listener->listen()) != ERROR_SUCCESS) {
        srs_error("rtsp caster listen failed. ret=%d", ret);
        return ret;
    }
    
    srs_info("listen thread cid=%d, current_cid=%d, "
        "listen at port=%d, type=%d, fd=%d started success, ep=%s:%d",
        pthread->cid(), _srs_context->get_id(), port, type, fd, ip.c_str(), port);

    srs_trace("%s listen at tcp://%s:%d, fd=%d", srs_listener_type2string(type).c_str(), ip.c_str(), port, listener->fd());

    return ret;
}

int SrsRtspListener::on_tcp_client(st_netfd_t stfd)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = caster->on_tcp_client(stfd)) != ERROR_SUCCESS) {
        srs_warn("accept client error. ret=%d", ret);
        return ret;
    }

    return ret;
}

SrsHttpFlvListener::SrsHttpFlvListener(SrsServer* svr, SrsListenerType t, SrsConfDirective* c) : SrsListener(svr, t)
{
    listener = NULL;
    
    // the caller already ensure the type is ok,
    // we just assert here for unknown stream caster.
    srs_assert(type == SrsListenerFlv);
    if (type == SrsListenerFlv) {
        caster = new SrsAppCasterFlv(c);
    }
}

SrsHttpFlvListener::~SrsHttpFlvListener()
{
    srs_freep(caster);
    srs_freep(listener);
}

int SrsHttpFlvListener::listen(string i, int p)
{
    int ret = ERROR_SUCCESS;
    
    // the caller already ensure the type is ok,
    // we just assert here for unknown stream caster.
    srs_assert(type == SrsListenerFlv);
    
    ip = i;
    port = p;
    
    if ((ret = caster->initialize()) != ERROR_SUCCESS) {
        return ret;
    }
    
    srs_freep(listener);
    listener = new SrsTcpListener(this, ip, port);
    
    if ((ret = listener->listen()) != ERROR_SUCCESS) {
        srs_error("flv caster listen failed. ret=%d", ret);
        return ret;
    }
    
    srs_info("listen thread cid=%d, current_cid=%d, "
             "listen at port=%d, type=%d, fd=%d started success, ep=%s:%d",
             pthread->cid(), _srs_context->get_id(), port, type, fd, ip.c_str(), port);
    
    srs_trace("%s listen at tcp://%s:%d, fd=%d", srs_listener_type2string(type).c_str(), ip.c_str(), port, listener->fd());
    
    return ret;
}

int SrsHttpFlvListener::on_tcp_client(st_netfd_t stfd)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = caster->on_tcp_client(stfd)) != ERROR_SUCCESS) {
        srs_warn("accept client error. ret=%d", ret);
        return ret;
    }
    
    return ret;
}
#endif

SrsUdpStreamListener::SrsUdpStreamListener(SrsServer* svr, SrsListenerType t, ISrsUdpHandler* c) : SrsListener(svr, t)
{
    listener = NULL;
    caster = c;
}

SrsUdpStreamListener::~SrsUdpStreamListener()
{
    srs_freep(listener);
}

int SrsUdpStreamListener::listen(string i, int p)
{
    int ret = ERROR_SUCCESS;

    // the caller already ensure the type is ok,
    // we just assert here for unknown stream caster.
    srs_assert(type == SrsListenerMpegTsOverUdp);
    
    ip = i;
    port = p;

    srs_freep(listener);
    listener = new SrsUdpListener(caster, ip, port);

    if ((ret = listener->listen()) != ERROR_SUCCESS) {
        srs_error("udp caster listen failed. ret=%d", ret);
        return ret;
    }
    
    srs_info("listen thread current_cid=%d, "
        "listen at port=%d, type=%d, fd=%d started success, ep=%s:%d",
        _srs_context->get_id(), p, type, listener->fd(), i.c_str(), p);
    
    // notify the handler the fd changed.
    if ((ret = caster->on_stfd_change(listener->stfd())) != ERROR_SUCCESS) {
        srs_error("notify handler fd changed. ret=%d", ret);
        return ret;
    }

    srs_trace("%s listen at udp://%s:%d, fd=%d", srs_listener_type2string(type).c_str(), ip.c_str(), port, listener->fd());

    return ret;
}

#ifdef SRS_AUTO_STREAM_CASTER
SrsUdpCasterListener::SrsUdpCasterListener(SrsServer* svr, SrsListenerType t, SrsConfDirective* c) : SrsUdpStreamListener(svr, t, NULL)
{
    // the caller already ensure the type is ok,
    // we just assert here for unknown stream caster.
    srs_assert(type == SrsListenerMpegTsOverUdp);
    if (type == SrsListenerMpegTsOverUdp) {
        caster = new SrsMpegtsOverUdp(c);
    }
}

SrsUdpCasterListener::~SrsUdpCasterListener()
{
    srs_freep(caster);
}
#endif

SrsSignalManager* SrsSignalManager::instance = NULL;

SrsSignalManager::SrsSignalManager(SrsServer* server)
{
    SrsSignalManager::instance = this;
    
    _server = server;
    sig_pipe[0] = sig_pipe[1] = -1;
    pthread = new SrsEndlessThread("signal", this);
    signal_read_stfd = NULL;
}

SrsSignalManager::~SrsSignalManager()
{
    srs_close_stfd(signal_read_stfd);
    
    if (sig_pipe[0] > 0) {
        ::close(sig_pipe[0]);
    }
    if (sig_pipe[1] > 0) {
        ::close(sig_pipe[1]);
    }
    
    srs_freep(pthread);
}

int SrsSignalManager::initialize()
{
    int ret = ERROR_SUCCESS;
    
    /* Create signal pipe */
    if (pipe(sig_pipe) < 0) {
        ret = ERROR_SYSTEM_CREATE_PIPE;
        srs_error("create signal manager pipe failed. ret=%d", ret);
        return ret;
    }
    
    if ((signal_read_stfd = st_netfd_open(sig_pipe[0])) == NULL) {
        ret = ERROR_SYSTEM_CREATE_PIPE;
        srs_error("create signal manage st pipe failed. ret=%d", ret);
        return ret;
    }
    
    return ret;
}

int SrsSignalManager::start()
{
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
    sigaction(SRS_SIGNAL_GRACEFULLY_QUIT, &sa, NULL);
    
    sa.sa_handler = SrsSignalManager::sig_catcher;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    
    sa.sa_handler = SrsSignalManager::sig_catcher;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SRS_SIGNAL_DISPOSE, &sa, NULL);
    
    sa.sa_handler = SrsSignalManager::sig_catcher;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SRS_SIGNAL_PERSISTENCE_CONFIG, &sa, NULL);
    
    srs_trace("signal installed");
    
    return pthread->start();
}

int SrsSignalManager::cycle()
{
    int ret = ERROR_SUCCESS;

    int signo;
    
    /* Read the next signal from the pipe */
    st_read(signal_read_stfd, &signo, sizeof(int), ST_UTIME_NO_TIMEOUT);
    
    /* Process signal synchronously */
    _server->on_signal(signo);
    
    return ret;
}

void SrsSignalManager::sig_catcher(int signo)
{
    int err;
    
    /* Save errno to restore it after the write() */
    err = errno;
    
    /* write() is reentrant/async-safe */
    int fd = SrsSignalManager::instance->sig_pipe[1];
    write(fd, &signo, sizeof(int));
    
    errno = err;
}

ISrsServerCycle::ISrsServerCycle()
{
}

ISrsServerCycle::~ISrsServerCycle()
{
}

SrsServer::SrsServer()
{
    signal_reload = false;
    signal_persistence_config = false;
    signal_gmc_stop = false;
    signal_gracefully_quit = false;
    pid_fd = -1;
    
    signal_manager = NULL;
    
    handler = NULL;
    
    // donot new object in constructor,
    // for some global instance is not ready now,
    // new these objects in initialize instead.
#ifdef SRS_AUTO_HTTP_API
    http_api_mux = new SrsHttpServeMux();
#endif
#ifdef SRS_AUTO_HTTP_SERVER
    http_server = new SrsHttpServer(this);
#endif
#ifdef SRS_AUTO_HTTP_CORE
    http_heartbeat = NULL;
#endif
#ifdef SRS_AUTO_INGEST
    ingester = NULL;
#endif
}

SrsServer::~SrsServer()
{
    destroy();
}

void SrsServer::destroy()
{
    srs_warn("start destroy server");
    
    dispose();
    
#ifdef SRS_AUTO_HTTP_API
    srs_freep(http_api_mux);
#endif

#ifdef SRS_AUTO_HTTP_SERVER
    srs_freep(http_server);
#endif

#ifdef SRS_AUTO_HTTP_CORE
    srs_freep(http_heartbeat);
#endif

#ifdef SRS_AUTO_INGEST
    srs_freep(ingester);
#endif
    
    if (pid_fd > 0) {
        ::close(pid_fd);
        pid_fd = -1;
    }
    
    srs_freep(signal_manager);
}

void SrsServer::dispose()
{
    _srs_config->unsubscribe(this);
    
    // prevent fresh clients.
    close_listeners(SrsListenerRtmpStream);
    close_listeners(SrsListenerHttpApi);
    close_listeners(SrsListenerHttpStream);
    close_listeners(SrsListenerMpegTsOverUdp);
    close_listeners(SrsListenerRtsp);
    close_listeners(SrsListenerFlv);
    
#ifdef SRS_AUTO_INGEST
    ingester->dispose();
#endif
    
    SrsSource::dispose_all();
    
    while (!conns.empty()) {
        std::vector<SrsConnection*>::iterator it;
        for (it = conns.begin(); it != conns.end(); ++it) {
            SrsConnection* conn = *it;
            conn->dispose();
        }
        
        st_usleep(100 * 1000);
    }
    
#ifdef SRS_AUTO_MEM_WATCH
    srs_memory_report();
#endif
}

int SrsServer::initialize(ISrsServerCycle* cycle_handler)
{
    int ret = ERROR_SUCCESS;
    
    // ensure the time is ok.
    srs_update_system_time_ms();
    
    // for the main objects(server, config, log, context),
    // never subscribe handler in constructor,
    // instead, subscribe handler in initialize method.
    srs_assert(_srs_config);
    _srs_config->subscribe(this);
    
    srs_assert(!signal_manager);
    signal_manager = new SrsSignalManager(this);
    
    handler = cycle_handler;
    if(handler && (ret = handler->initialize()) != ERROR_SUCCESS){
        return ret;
    }
    
#ifdef SRS_AUTO_HTTP_API
    if ((ret = http_api_mux->initialize()) != ERROR_SUCCESS) {
        return ret;
    }
#endif

#ifdef SRS_AUTO_HTTP_SERVER
    srs_assert(http_server);
    if ((ret = http_server->initialize()) != ERROR_SUCCESS) {
        return ret;
    }
#endif

#ifdef SRS_AUTO_HTTP_CORE
    srs_assert(!http_heartbeat);
    http_heartbeat = new SrsHttpHeartbeat();
#endif

#ifdef SRS_AUTO_INGEST
    srs_assert(!ingester);
    ingester = new SrsIngester();
#endif

    return ret;
}

int SrsServer::initialize_st()
{
    int ret = ERROR_SUCCESS;
    
    // init st
    if ((ret = srs_st_init()) != ERROR_SUCCESS) {
        srs_error("init st failed. ret=%d", ret);
        return ret;
    }
    
    // @remark, st alloc segment use mmap, which only support 32757 threads,
    // if need to support more, for instance, 100k threads, define the macro MALLOC_STACK.
    // TODO: FIXME: maybe can use "sysctl vm.max_map_count" to refine.
    if (_srs_config->get_max_connections() > 32756) {
        ret = ERROR_ST_EXCEED_THREADS;
        srs_error("st mmap for stack allocation must <= %d threads, "
                  "@see Makefile of st for MALLOC_STACK, please build st manually by "
                  "\"make EXTRA_CFLAGS=-DMALLOC_STACK linux-debug\", ret=%d", ret);
        return ret;
    }
    
    // set current log id.
    _srs_context->generate_id();
    srs_trace("server main cid=%d", _srs_context->get_id());
    
    return ret;
}

int SrsServer::initialize_signal()
{
    return signal_manager->initialize();
}

int SrsServer::acquire_pid_file()
{
    int ret = ERROR_SUCCESS;
    
    // when srs in dolphin mode, no need the pid file.
    if (_srs_config->is_dolphin()) {
        return ret;
    }
    
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
    struct flock lock;

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
    pid_fd = fd;
    
    return ret;
}

int SrsServer::listen()
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = listen_rtmp()) != ERROR_SUCCESS) {
        return ret;
    }
    
    if ((ret = listen_http_api()) != ERROR_SUCCESS) {
        return ret;
    }
    
    if ((ret = listen_http_stream()) != ERROR_SUCCESS) {
        return ret;
    }
    
    if ((ret = listen_stream_caster()) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int SrsServer::register_signal()
{
    // start signal process thread.
    return signal_manager->start();
}

int SrsServer::http_handle()
{
    int ret = ERROR_SUCCESS;
    
#ifdef SRS_AUTO_HTTP_API
    srs_assert(http_api_mux);
    if ((ret = http_api_mux->handle("/", new SrsHttpNotFoundHandler())) != ERROR_SUCCESS) {
        return ret;
    }
    if ((ret = http_api_mux->handle("/api/", new SrsGoApiApi())) != ERROR_SUCCESS) {
        return ret;
    }
    if ((ret = http_api_mux->handle("/api/v1/", new SrsGoApiV1())) != ERROR_SUCCESS) {
        return ret;
    }
    if ((ret = http_api_mux->handle("/api/v1/versions", new SrsGoApiVersion())) != ERROR_SUCCESS) {
        return ret;
    }
    if ((ret = http_api_mux->handle("/api/v1/summaries", new SrsGoApiSummaries())) != ERROR_SUCCESS) {
        return ret;
    }
    if ((ret = http_api_mux->handle("/api/v1/rusages", new SrsGoApiRusages())) != ERROR_SUCCESS) {
        return ret;
    }
    if ((ret = http_api_mux->handle("/api/v1/self_proc_stats", new SrsGoApiSelfProcStats())) != ERROR_SUCCESS) {
        return ret;
    }
    if ((ret = http_api_mux->handle("/api/v1/system_proc_stats", new SrsGoApiSystemProcStats())) != ERROR_SUCCESS) {
        return ret;
    }
    if ((ret = http_api_mux->handle("/api/v1/meminfos", new SrsGoApiMemInfos())) != ERROR_SUCCESS) {
        return ret;
    }
    if ((ret = http_api_mux->handle("/api/v1/authors", new SrsGoApiAuthors())) != ERROR_SUCCESS) {
        return ret;
    }
    if ((ret = http_api_mux->handle("/api/v1/features", new SrsGoApiFeatures())) != ERROR_SUCCESS) {
        return ret;
    }
    if ((ret = http_api_mux->handle("/api/v1/vhosts/", new SrsGoApiVhosts())) != ERROR_SUCCESS) {
        return ret;
    }
    if ((ret = http_api_mux->handle("/api/v1/streams/", new SrsGoApiStreams())) != ERROR_SUCCESS) {
        return ret;
    }
    if ((ret = http_api_mux->handle("/api/v1/clients/", new SrsGoApiClients())) != ERROR_SUCCESS) {
        return ret;
    }
    if ((ret = http_api_mux->handle("/api/v1/raw", new SrsGoApiRaw(this))) != ERROR_SUCCESS) {
        return ret;
    }
    
    // test the request info.
    if ((ret = http_api_mux->handle("/api/v1/tests/requests", new SrsGoApiRequests())) != ERROR_SUCCESS) {
        return ret;
    }
    // test the error code response.
    if ((ret = http_api_mux->handle("/api/v1/tests/errors", new SrsGoApiError())) != ERROR_SUCCESS) {
        return ret;
    }
    // test the redirect mechenism.
    if ((ret = http_api_mux->handle("/api/v1/tests/redirects", new SrsHttpRedirectHandler("/api/v1/tests/errors", SRS_CONSTS_HTTP_MovedPermanently))) != ERROR_SUCCESS) {
        return ret;
    }
    // test the http vhost.
    if ((ret = http_api_mux->handle("error.srs.com/api/v1/tests/errors", new SrsGoApiError())) != ERROR_SUCCESS) {
        return ret;
    }
    
    // TODO: FIXME: for console.
    // TODO: FIXME: support reload.
    std::string dir = _srs_config->get_http_stream_dir() + "/console";
    if ((ret = http_api_mux->handle("/console/", new SrsHttpFileServer(dir))) != ERROR_SUCCESS) {
        srs_error("http: mount console dir=%s failed. ret=%d", dir.c_str(), ret);
        return ret;
    }
    srs_trace("http: api mount /console to %s", dir.c_str());
#endif

    return ret;
}

int SrsServer::ingest()
{
    int ret = ERROR_SUCCESS;
    
#ifdef SRS_AUTO_INGEST
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

    ret = do_cycle();

#ifdef SRS_AUTO_GPERF_MC
    destroy();
    
    // remark, for gmc, never invoke the exit().
    srs_warn("sleep a long time for system st-threads to cleanup.");
    st_usleep(3 * 1000 * 1000);
    srs_warn("system quit");
#else
    srs_warn("main cycle terminated, system quit normally.");
    dispose();
    srs_trace("srs terminated");
    exit(0);
#endif
    
    return ret;
}

void SrsServer::remove(SrsConnection* conn)
{
    std::vector<SrsConnection*>::iterator it = std::find(conns.begin(), conns.end(), conn);
    
    // removed by destroy, ignore.
    if (it == conns.end()) {
        srs_warn("server moved connection, ignore.");
        return;
    }
    
    conns.erase(it);
    
    srs_info("conn removed. conns=%d", (int)conns.size());
    
    SrsStatistic* stat = SrsStatistic::instance();
    stat->kbps_add_delta(conn);
    stat->on_disconnect(conn->srs_id());
    
    // all connections are created by server,
    // so we free it here.
    srs_freep(conn);
}

void SrsServer::on_signal(int signo)
{
    if (signo == SRS_SIGNAL_RELOAD) {
        signal_reload = true;
        return;
    }
    
    if (signo == SRS_SIGNAL_PERSISTENCE_CONFIG) {
        signal_persistence_config = true;
        return;
    }
    
    if (signo == SIGINT || signo == SRS_SIGNAL_DISPOSE) {
#ifdef SRS_AUTO_GPERF_MC
        srs_trace("gmc is on, main cycle will terminate normally.");
        signal_gmc_stop = true;
#else
        srs_trace("user terminate program");
#ifdef SRS_AUTO_MEM_WATCH
        srs_memory_report();
#endif
        exit(0);
#endif
        return;
    }
    
    if (signo == SRS_SIGNAL_GRACEFULLY_QUIT && !signal_gracefully_quit) {
        srs_trace("user terminate program, gracefully quit.");
        signal_gracefully_quit = true;
        return;
    }
}

int SrsServer::do_cycle()
{
    int ret = ERROR_SUCCESS;
    
    // find the max loop
    int max = srs_max(0, SRS_SYS_TIME_RESOLUTION_MS_TIMES);
    
#ifdef SRS_AUTO_STAT
    max = srs_max(max, SRS_SYS_RUSAGE_RESOLUTION_TIMES);
    max = srs_max(max, SRS_SYS_CPU_STAT_RESOLUTION_TIMES);
    max = srs_max(max, SRS_SYS_DISK_STAT_RESOLUTION_TIMES);
    max = srs_max(max, SRS_SYS_MEMINFO_RESOLUTION_TIMES);
    max = srs_max(max, SRS_SYS_PLATFORM_INFO_RESOLUTION_TIMES);
    max = srs_max(max, SRS_SYS_NETWORK_DEVICE_RESOLUTION_TIMES);
    max = srs_max(max, SRS_SYS_NETWORK_RTMP_SERVER_RESOLUTION_TIMES);
#endif
    
    // the deamon thread, update the time cache
    while (true) {
        if(handler && (ret = handler->on_cycle((int)conns.size())) != ERROR_SUCCESS){
            srs_error("cycle handle failed. ret=%d", ret);
            return ret;
        }
            
        // the interval in config.
        int heartbeat_max_resolution = (int)(_srs_config->get_heartbeat_interval() / SRS_SYS_CYCLE_INTERVAL);
        
        // dynamic fetch the max.
        int temp_max = max;
        temp_max = srs_max(temp_max, heartbeat_max_resolution);
        
        for (int i = 0; i < temp_max; i++) {
            st_usleep(SRS_SYS_CYCLE_INTERVAL * 1000);
            
            // gracefully quit for SIGINT or SIGTERM(SRS_SIGNAL_GRACEFULLY_QUIT).
            if (signal_gracefully_quit) {
                srs_trace("cleanup for gracefully terminate.");
                return ret;
            }
        
            // for gperf heap checker,
            // @see: research/gperftools/heap-checker/heap_checker.cc
            // if user interrupt the program, exit to check mem leak.
            // but, if gperf, use reload to ensure main return normally,
            // because directly exit will cause core-dump.
#ifdef SRS_AUTO_GPERF_MC
            if (signal_gmc_stop) {
                srs_warn("gmc got singal to stop server.");
                return ret;
            }
#endif
        
            // do reload the config.
            if (signal_reload) {
                signal_reload = false;
                srs_info("get signal to reload the config.");
                
                if ((ret = _srs_config->reload()) != ERROR_SUCCESS) {
                    srs_error("reload config failed. ret=%d", ret);
                    return ret;
                }
                srs_trace("reload config success.");
            }
            
            // do persistence config to file.
            if (signal_persistence_config) {
                signal_persistence_config = false;
                srs_info("get signal to persistence config to file.");
                
                if ((ret = _srs_config->persistence()) != ERROR_SUCCESS) {
                    srs_error("persistence config to file failed. ret=%d", ret);
                    return ret;
                }
                srs_trace("persistence config to file success.");
            }
            
            // notice the stream sources to cycle.
            if ((ret = SrsSource::cycle_all()) != ERROR_SUCCESS) {
                return ret;
            }
            
            // update the cache time
            if ((i % SRS_SYS_TIME_RESOLUTION_MS_TIMES) == 0) {
                srs_info("update current time cache.");
                srs_update_system_time_ms();
            }
            
#ifdef SRS_AUTO_STAT
            if ((i % SRS_SYS_RUSAGE_RESOLUTION_TIMES) == 0) {
                srs_info("update resource info, rss.");
                srs_update_system_rusage();
            }
            if ((i % SRS_SYS_CPU_STAT_RESOLUTION_TIMES) == 0) {
                srs_info("update cpu info, cpu usage.");
                srs_update_proc_stat();
            }
            if ((i % SRS_SYS_DISK_STAT_RESOLUTION_TIMES) == 0) {
                srs_info("update disk info, disk iops.");
                srs_update_disk_stat();
            }
            if ((i % SRS_SYS_MEMINFO_RESOLUTION_TIMES) == 0) {
                srs_info("update memory info, usage/free.");
                srs_update_meminfo();
            }
            if ((i % SRS_SYS_PLATFORM_INFO_RESOLUTION_TIMES) == 0) {
                srs_info("update platform info, uptime/load.");
                srs_update_platform_info();
            }
            if ((i % SRS_SYS_NETWORK_DEVICE_RESOLUTION_TIMES) == 0) {
                srs_info("update network devices info.");
                srs_update_network_devices();
            }
            if ((i % SRS_SYS_NETWORK_RTMP_SERVER_RESOLUTION_TIMES) == 0) {
                srs_info("update network server kbps info.");
                resample_kbps();
            }
    #ifdef SRS_AUTO_HTTP_CORE
            if (_srs_config->get_heartbeat_enabled()) {
                if ((i % heartbeat_max_resolution) == 0) {
                    srs_info("do http heartbeat, for internal server to report.");
                    http_heartbeat->heartbeat();
                }
            }
    #endif
#endif
            
            srs_info("server main thread loop");
        }
    }

    return ret;
}

int SrsServer::listen_rtmp()
{
    int ret = ERROR_SUCCESS;
    
    // stream service port.
    std::vector<std::string> ip_ports = _srs_config->get_listens();
    srs_assert((int)ip_ports.size() > 0);
    
    close_listeners(SrsListenerRtmpStream);
    
    for (int i = 0; i < (int)ip_ports.size(); i++) {
        SrsListener* listener = new SrsStreamListener(this, SrsListenerRtmpStream);
        listeners.push_back(listener);
        
        std::string ip;
        int port;
        srs_parse_endpoint(ip_ports[i], ip, port);
        
        if ((ret = listener->listen(ip, port)) != ERROR_SUCCESS) {
            srs_error("RTMP stream listen at %s:%d failed. ret=%d", ip.c_str(), port, ret);
            return ret;
        }
    }
    
    return ret;
}

int SrsServer::listen_http_api()
{
    int ret = ERROR_SUCCESS;
    
#ifdef SRS_AUTO_HTTP_API
    close_listeners(SrsListenerHttpApi);
    if (_srs_config->get_http_api_enabled()) {
        SrsListener* listener = new SrsStreamListener(this, SrsListenerHttpApi);
        listeners.push_back(listener);
        
        std::string ep = _srs_config->get_http_api_listen();
        
        std::string ip;
        int port;
        srs_parse_endpoint(ep, ip, port);
        
        if ((ret = listener->listen(ip, port)) != ERROR_SUCCESS) {
            srs_error("HTTP api listen at %s:%d failed. ret=%d", ip.c_str(), port, ret);
            return ret;
        }
    }
#endif
    
    return ret;
}

int SrsServer::listen_http_stream()
{
    int ret = ERROR_SUCCESS;
    
#ifdef SRS_AUTO_HTTP_SERVER
    close_listeners(SrsListenerHttpStream);
    if (_srs_config->get_http_stream_enabled()) {
        SrsListener* listener = new SrsStreamListener(this, SrsListenerHttpStream);
        listeners.push_back(listener);
        
        std::string ep = _srs_config->get_http_stream_listen();
        
        std::string ip;
        int port;
        srs_parse_endpoint(ep, ip, port);
        
        if ((ret = listener->listen(ip, port)) != ERROR_SUCCESS) {
            srs_error("HTTP stream listen at %s:%d failed. ret=%d", ip.c_str(), port, ret);
            return ret;
        }
    }
#endif
    
    return ret;
}

int SrsServer::listen_stream_caster()
{
    int ret = ERROR_SUCCESS;
    
#ifdef SRS_AUTO_STREAM_CASTER
    close_listeners(SrsListenerMpegTsOverUdp);
    
    std::vector<SrsConfDirective*>::iterator it;
    std::vector<SrsConfDirective*> stream_casters = _srs_config->get_stream_casters();

    for (it = stream_casters.begin(); it != stream_casters.end(); ++it) {
        SrsConfDirective* stream_caster = *it;
        if (!_srs_config->get_stream_caster_enabled(stream_caster)) {
            continue;
        }

        SrsListener* listener = NULL;

        std::string caster = _srs_config->get_stream_caster_engine(stream_caster);
        if (srs_stream_caster_is_udp(caster)) {
            listener = new SrsUdpCasterListener(this, SrsListenerMpegTsOverUdp, stream_caster);
        } else if (srs_stream_caster_is_rtsp(caster)) {
            listener = new SrsRtspListener(this, SrsListenerRtsp, stream_caster);
        } else if (srs_stream_caster_is_flv(caster)) {
            listener = new SrsHttpFlvListener(this, SrsListenerFlv, stream_caster);
        } else {
            ret = ERROR_STREAM_CASTER_ENGINE;
            srs_error("unsupported stream caster %s. ret=%d", caster.c_str(), ret);
            return ret;
        }
        srs_assert(listener != NULL);

        listeners.push_back(listener);
        
        int port = _srs_config->get_stream_caster_listen(stream_caster);
        if (port <= 0) {
            ret = ERROR_STREAM_CASTER_PORT;
            srs_error("invalid stream caster port %d. ret=%d", port, ret);
            return ret;
        }
        
        // TODO: support listen at <[ip:]port>
        if ((ret = listener->listen("0.0.0.0", port)) != ERROR_SUCCESS) {
            srs_error("StreamCaster listen at port %d failed. ret=%d", port, ret);
            return ret;
        }
    }
#endif
    
    return ret;
}

void SrsServer::close_listeners(SrsListenerType type)
{
    std::vector<SrsListener*>::iterator it;
    for (it = listeners.begin(); it != listeners.end();) {
        SrsListener* listener = *it;
        
        if (listener->listen_type() != type) {
            ++it;
            continue;
        }
        
        srs_freep(listener);
        it = listeners.erase(it);
    }
}

void SrsServer::resample_kbps()
{
    SrsStatistic* stat = SrsStatistic::instance();
    
    // collect delta from all clients.
    for (std::vector<SrsConnection*>::iterator it = conns.begin(); it != conns.end(); ++it) {
        SrsConnection* conn = *it;
        
        // add delta of connection to server kbps.,
        // for next sample() of server kbps can get the stat.
        stat->kbps_add_delta(conn);
    }
    
    // TODO: FXME: support all other connections.

    // sample the kbps, get the stat.
    SrsKbps* kbps = stat->kbps_sample();
    
    srs_update_rtmp_server((int)conns.size(), kbps);
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
#ifdef SRS_AUTO_HTTP_API
        conn = new SrsHttpApi(this, client_stfd, http_api_mux);
#else
        srs_warn("close http client for server not support http-api");
        srs_close_stfd(client_stfd);
        return ret;
#endif
    } else if (type == SrsListenerHttpStream) {
#ifdef SRS_AUTO_HTTP_SERVER
        conn = new SrsResponseOnlyHttpConn(this, client_stfd, http_server);
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
    // @remark never use the conn, for it maybe destroyed.
    if ((ret = conn->start()) != ERROR_SUCCESS) {
        return ret;
    }
    srs_verbose("conn started success.");

    srs_verbose("accept client finished. conns=%d, ret=%d", (int)conns.size(), ret);
    
    return ret;
}

int SrsServer::on_reload_listen()
{
    return listen();
}

int SrsServer::on_reload_pid()
{
    if (pid_fd > 0) {
        ::close(pid_fd);
        pid_fd = -1;
    }
    
    return acquire_pid_file();
}

int SrsServer::on_reload_vhost_added(std::string vhost)
{
    int ret = ERROR_SUCCESS;
    
#ifdef SRS_AUTO_HTTP_SERVER
    if (!_srs_config->get_vhost_http_enabled(vhost)) {
        return ret;
    }
    
    // TODO: FIXME: should handle the event in SrsHttpStaticServer
    if ((ret = on_reload_vhost_http_updated()) != ERROR_SUCCESS) {
        return ret;
    }
#endif

    return ret;
}

int SrsServer::on_reload_vhost_removed(std::string /*vhost*/)
{
    int ret = ERROR_SUCCESS;
    
#ifdef SRS_AUTO_HTTP_SERVER
    // TODO: FIXME: should handle the event in SrsHttpStaticServer
    if ((ret = on_reload_vhost_http_updated()) != ERROR_SUCCESS) {
        return ret;
    }
#endif

    return ret;
}

int SrsServer::on_reload_http_api_enabled()
{
    int ret = ERROR_SUCCESS;
    
#ifdef SRS_AUTO_HTTP_API
    ret = listen_http_api();
#endif
    
    return ret;
}

int SrsServer::on_reload_http_api_disabled()
{
    int ret = ERROR_SUCCESS;
    
#ifdef SRS_AUTO_HTTP_API
    close_listeners(SrsListenerHttpApi);
#endif
    
    return ret;
}

int SrsServer::on_reload_http_stream_enabled()
{
    int ret = ERROR_SUCCESS;
    
#ifdef SRS_AUTO_HTTP_SERVER
    ret = listen_http_stream();
#endif
    
    return ret;
}

int SrsServer::on_reload_http_stream_disabled()
{
    int ret = ERROR_SUCCESS;
    
#ifdef SRS_AUTO_HTTP_SERVER
    close_listeners(SrsListenerHttpStream);
#endif

    return ret;
}

// TODO: FIXME: rename to http_remux
int SrsServer::on_reload_http_stream_updated()
{
    int ret = ERROR_SUCCESS;
    
#ifdef SRS_AUTO_HTTP_SERVER
    if ((ret = on_reload_http_stream_enabled()) != ERROR_SUCCESS) {
        return ret;
    }
    
    // TODO: FIXME: should handle the event in SrsHttpStaticServer
    if ((ret = on_reload_vhost_http_updated()) != ERROR_SUCCESS) {
        return ret;
    }
#endif
    
    return ret;
}

int SrsServer::on_publish(SrsSource* s, SrsRequest* r)
{
    int ret = ERROR_SUCCESS;
    
#ifdef SRS_AUTO_HTTP_SERVER
    if ((ret = http_server->http_mount(s, r)) != ERROR_SUCCESS) {
        return ret;
    }
#endif
    
    return ret;
}

void SrsServer::on_unpublish(SrsSource* s, SrsRequest* r)
{
#ifdef SRS_AUTO_HTTP_SERVER
    http_server->http_unmount(s, r);
#endif
}

int SrsServer::on_hls_publish(SrsRequest* r)
{
    int ret = ERROR_SUCCESS;
    
#ifdef SRS_AUTO_HTTP_SERVER
    if ((ret = http_server->mount_hls(r)) != ERROR_SUCCESS) {
        return ret;
    }
#endif
    
    return ret;
}

int SrsServer::on_update_m3u8(SrsRequest* r, string m3u8)
{
    int ret = ERROR_SUCCESS;
    
#ifdef SRS_AUTO_HTTP_SERVER
    if ((ret = http_server->hls_update_m3u8(r, m3u8)) != ERROR_SUCCESS) {
        return ret;
    }
#endif
    
    return ret;
}

int SrsServer::on_update_ts(SrsRequest* r, string uri, string ts)
{
    int ret = ERROR_SUCCESS;
    
#ifdef SRS_AUTO_HTTP_SERVER
    if ((ret = http_server->hls_update_ts(r, uri, ts)) != ERROR_SUCCESS) {
        return ret;
    }
#endif
    
    return ret;
}


int SrsServer::on_remove_ts(SrsRequest* r, string uri)
{
    int ret = ERROR_SUCCESS;
    
#ifdef SRS_AUTO_HTTP_SERVER
    if ((ret = http_server->hls_remove_ts(r, uri)) != ERROR_SUCCESS) {
        return ret;
    }
#endif
    
    return ret;
}

int SrsServer::on_hls_unpublish(SrsRequest* r)
{
    int ret = ERROR_SUCCESS;
    
#ifdef SRS_AUTO_HTTP_SERVER
    http_server->unmount_hls(r);
#endif
    
    return ret;
}

