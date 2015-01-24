/*
The MIT License (MIT)

Copyright (c) 2013-2015 winlin

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
#include <srs_app_ingest.hpp>
#include <srs_app_source.hpp>
#include <srs_app_utility.hpp>
#include <srs_app_heartbeat.hpp>
#include <srs_app_mpegts_udp.hpp>

// signal defines.
#define SIGNAL_RELOAD SIGHUP

// nginx also set to 512
#define SERVER_LISTEN_BACKLOG 512

// sleep in ms for udp recv packet.
#define SRS_UDP_PACKET_RECV_CYCLE_INTERVAL_MS 0

// set the max packet size.
#define SRS_UDP_MAX_PACKET_SIZE 65535

// system interval in ms,
// all resolution times should be times togother,
// for example, system-interval is x=1s(1000ms),
// then rusage can be 3*x, for instance, 3*1=3s,
// the meminfo canbe 6*x, for instance, 6*1=6s,
// for performance refine, @see: https://github.com/winlinvip/simple-rtmp-server/issues/194
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

std::string __srs_listener_type2string(SrsListenerType type) 
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
    default:
        return "UNKONWN";
    }
}

SrsListener::SrsListener(SrsServer* server, SrsListenerType type)
{
    fd = -1;
    stfd = NULL;
    
    _port = 0;
    _server = server;
    _type = type;

    pthread = new SrsThread("listen", this, 0, true);
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

SrsListenerType SrsListener::type()
{
    return _type;
}

int SrsListener::listen(int port)
{
    int ret = ERROR_SUCCESS;
    
    _port = port;
    
    if ((fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        ret = ERROR_SOCKET_CREATE;
        srs_error("create linux socket error. port=%d, ret=%d", port, ret);
        return ret;
    }
    srs_verbose("create linux socket success. port=%d, fd=%d", port, fd);
    
    int reuse_socket = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse_socket, sizeof(int)) == -1) {
        ret = ERROR_SOCKET_SETREUSE;
        srs_error("setsockopt reuse-addr error. port=%d, ret=%d", port, ret);
        return ret;
    }
    srs_verbose("setsockopt reuse-addr success. port=%d, fd=%d", port, fd);
    
    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(_port);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(fd, (const sockaddr*)&addr, sizeof(sockaddr_in)) == -1) {
        ret = ERROR_SOCKET_BIND;
        srs_error("bind socket error. port=%d, ret=%d", port, ret);
        return ret;
    }
    srs_verbose("bind socket success. port=%d, fd=%d", port, fd);
    
    if (::listen(fd, SERVER_LISTEN_BACKLOG) == -1) {
        ret = ERROR_SOCKET_LISTEN;
        srs_error("listen socket error. port=%d, ret=%d", port, ret);
        return ret;
    }
    srs_verbose("listen socket success. port=%d, fd=%d", port, fd);
    
    if ((stfd = st_netfd_open_socket(fd)) == NULL){
        ret = ERROR_ST_OPEN_SOCKET;
        srs_error("st_netfd_open_socket open socket failed. port=%d, ret=%d", port, ret);
        return ret;
    }
    srs_verbose("st open socket success. port=%d, fd=%d", port, fd);
    
    if ((ret = pthread->start()) != ERROR_SUCCESS) {
        srs_error("st_thread_create listen thread error. port=%d, ret=%d", port, ret);
        return ret;
    }
    srs_verbose("create st listen thread success, port=%d", port);
    
    srs_info("listen thread cid=%d, current_cid=%d, "
        "listen at port=%d, type=%d, fd=%d started success, port=%d", 
        pthread->cid(), _srs_context->get_id(), _port, _type, fd, port);

    srs_trace("%s listen at tcp://%d, fd=%d", __srs_listener_type2string(_type).c_str(), _port, fd);
    
    return ret;
}

int SrsListener::cycle()
{
    int ret = ERROR_SUCCESS;
    
    st_netfd_t client_stfd = st_accept(stfd, NULL, NULL, ST_UTIME_NO_TIMEOUT);
    
    if(client_stfd == NULL){
        // ignore error.
        srs_error("ignore accept thread stoppped for accept client error");
        return ret;
    }
    srs_verbose("get a client. fd=%d", st_netfd_fileno(client_stfd));
    
    if ((ret = _server->accept_client(_type, client_stfd)) != ERROR_SUCCESS) {
        srs_warn("accept client error. ret=%d", ret);
        return ret;
    }
    
    return ret;
}

#ifdef SRS_AUTO_STREAM_CASTER
SrsUdpListener::SrsUdpListener(SrsServer* server, SrsListenerType type, SrsConfDirective* c) : SrsListener(server, type)
{
    _type = type;
    nb_buf = SRS_UDP_MAX_PACKET_SIZE;
    buf = new char[nb_buf];

    // the caller already ensure the type is ok,
    // we just assert here for unknown stream caster.
    srs_assert(_type == SrsListenerMpegTsOverUdp);
    if (_type == SrsListenerMpegTsOverUdp) {
        caster = new SrsMpegtsOverUdp(c);
    }
}

SrsUdpListener::~SrsUdpListener()
{
    srs_freep(caster);
    srs_freep(buf);
}

int SrsUdpListener::listen(int port)
{
    int ret = ERROR_SUCCESS;

    // the caller already ensure the type is ok,
    // we just assert here for unknown stream caster.
    srs_assert(_type == SrsListenerMpegTsOverUdp);
    
    _port = port;
    
    if ((fd = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
        ret = ERROR_SOCKET_CREATE;
        srs_error("create linux socket error. port=%d, ret=%d", port, ret);
        return ret;
    }
    srs_verbose("create linux socket success. port=%d, fd=%d", port, fd);
    
    int reuse_socket = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse_socket, sizeof(int)) == -1) {
        ret = ERROR_SOCKET_SETREUSE;
        srs_error("setsockopt reuse-addr error. port=%d, ret=%d", port, ret);
        return ret;
    }
    srs_verbose("setsockopt reuse-addr success. port=%d, fd=%d", port, fd);
    
    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(_port);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(fd, (const sockaddr*)&addr, sizeof(sockaddr_in)) == -1) {
        ret = ERROR_SOCKET_BIND;
        srs_error("bind socket error. port=%d, ret=%d", port, ret);
        return ret;
    }
    srs_verbose("bind socket success. port=%d, fd=%d", port, fd);
    
    if ((stfd = st_netfd_open_socket(fd)) == NULL){
        ret = ERROR_ST_OPEN_SOCKET;
        srs_error("st_netfd_open_socket open socket failed. port=%d, ret=%d", port, ret);
        return ret;
    }
    srs_verbose("st open socket success. port=%d, fd=%d", port, fd);
    
    if ((ret = pthread->start()) != ERROR_SUCCESS) {
        srs_error("st_thread_create listen thread error. port=%d, ret=%d", port, ret);
        return ret;
    }
    srs_verbose("create st listen thread success, port=%d", port);
    
    srs_info("listen thread cid=%d, current_cid=%d, "
        "listen at port=%d, type=%d, fd=%d started success, port=%d", 
        pthread->cid(), _srs_context->get_id(), _port, _type, fd, port);

    srs_trace("%s listen at udp://%d, fd=%d", __srs_listener_type2string(_type).c_str(), _port, fd);

    return ret;
}

int SrsUdpListener::cycle()
{
    int ret = ERROR_SUCCESS;

    // the caller already ensure the type is ok,
    // we just assert here for unknown stream caster.
    srs_assert(_type == SrsListenerMpegTsOverUdp);

    for (;;) {
        // TODO: FIXME: support ipv6, @see man 7 ipv6
        sockaddr_in from;
        int nb_from = sizeof(sockaddr_in);
        int nread = 0;

        if ((nread = st_recvfrom(stfd, buf, nb_buf, (sockaddr*)&from, &nb_from, ST_UTIME_NO_TIMEOUT)) <= 0) {
            srs_warn("ignore recv udp packet failed, nread=%d", nread);
            continue;
        }
        
        if ((ret = caster->on_udp_packet(&from, buf, nread)) != ERROR_SUCCESS) {
            srs_warn("handle udp packet failed. ret=%d", ret);
            continue;
        }

        if (SRS_UDP_PACKET_RECV_CYCLE_INTERVAL_MS > 0) {
            st_usleep(SRS_UDP_PACKET_RECV_CYCLE_INTERVAL_MS * 1000);
        }
    }

    // TODO: FIXME: recv udp packet.
    st_sleep(1);

    return ret;
}
#endif

SrsSignalManager* SrsSignalManager::instance = NULL;

SrsSignalManager::SrsSignalManager(SrsServer* server)
{
    SrsSignalManager::instance = this;
    
    _server = server;
    sig_pipe[0] = sig_pipe[1] = -1;
    pthread = new SrsThread("signal", this, 0, true);
    signal_read_stfd = NULL;
}

SrsSignalManager::~SrsSignalManager()
{
    pthread->stop();
    srs_freep(pthread);
    
    srs_close_stfd(signal_read_stfd);
    
    if (sig_pipe[0] > 0) {
        ::close(sig_pipe[0]);
    }
    if (sig_pipe[1] > 0) {
        ::close(sig_pipe[1]);
    }
}

int SrsSignalManager::initialize()
{
    int ret = ERROR_SUCCESS;
    return ret;
}

int SrsSignalManager::start()
{
    int ret = ERROR_SUCCESS;
    
    /**
    * Note that if multiple processes are used (see below), 
    * the signal pipe should be initialized after the fork(2) call 
    * so that each process has its own private pipe.
    */
    struct sigaction sa;
    
    /* Create signal pipe */
    if (pipe(sig_pipe) < 0) {
        ret = ERROR_SYSTEM_CREATE_PIPE;
        srs_error("create signal manager pipe failed. ret=%d", ret);
        return ret;
    }
    
    /* Install sig_catcher() as a signal handler */
    sa.sa_handler = SrsSignalManager::sig_catcher;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGNAL_RELOAD, &sa, NULL);
    
    sa.sa_handler = SrsSignalManager::sig_catcher;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, NULL);
    
    sa.sa_handler = SrsSignalManager::sig_catcher;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    
    sa.sa_handler = SrsSignalManager::sig_catcher;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGUSR2, &sa, NULL);
    
    srs_trace("signal installed");
    
    return pthread->start();
}

int SrsSignalManager::cycle()
{
    int ret = ERROR_SUCCESS;
    
    if (signal_read_stfd == NULL) {
        signal_read_stfd = st_netfd_open(sig_pipe[0]);
    }

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

SrsServer::SrsServer()
{
    signal_reload = false;
    signal_gmc_stop = false;
    pid_fd = -1;
    
    signal_manager = NULL;
    kbps = NULL;
    
    // donot new object in constructor,
    // for some global instance is not ready now,
    // new these objects in initialize instead.
#ifdef SRS_AUTO_HTTP_API
    http_api_mux = new SrsGoHttpServeMux();
#endif
#ifdef SRS_AUTO_HTTP_SERVER
    http_stream_mux = new SrsHttpServer();
#endif
#ifdef SRS_AUTO_HTTP_PARSER
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
    
    _srs_config->unsubscribe(this);
    
    close_listeners(SrsListenerRtmpStream);
    close_listeners(SrsListenerHttpApi);
    close_listeners(SrsListenerHttpStream);

#ifdef SRS_AUTO_INGEST
    ingester->stop();
#endif
    
#ifdef SRS_AUTO_HTTP_API
    srs_freep(http_api_mux);
#endif

#ifdef SRS_AUTO_HTTP_SERVER
    srs_freep(http_stream_mux);
#endif

#ifdef SRS_AUTO_HTTP_PARSER
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
    srs_freep(kbps);
    
    // @remark never destroy the connections, 
    // for it's still alive.

    // @remark never destroy the source, 
    // when we free all sources, the fmle publish may retry
    // and segment fault.
}

int SrsServer::initialize()
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
    
    srs_assert(!kbps);
    kbps = new SrsKbps();
    kbps->set_io(NULL, NULL);
    
#ifdef SRS_AUTO_HTTP_API
    if ((ret = http_api_mux->initialize()) != ERROR_SUCCESS) {
        return ret;
    }
#endif
    
#ifdef SRS_AUTO_HTTP_API
    srs_assert(http_api_mux);
    if ((ret = http_api_mux->handle("/", new SrsGoApiRoot())) != ERROR_SUCCESS) {
        return ret;
    }
    if ((ret = http_api_mux->handle("/api", new SrsGoApiApi())) != ERROR_SUCCESS) {
        return ret;
    }
    if ((ret = http_api_mux->handle("/api/v1", new SrsGoApiV1())) != ERROR_SUCCESS) {
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
    if ((ret = http_api_mux->handle("/api/v1/requests", new SrsGoApiRequests())) != ERROR_SUCCESS) {
        return ret;
    }
    if ((ret = http_api_mux->handle("/api/v1/vhosts", new SrsGoApiVhosts())) != ERROR_SUCCESS) {
        return ret;
    }
    if ((ret = http_api_mux->handle("/api/v1/streams", new SrsGoApiStreams())) != ERROR_SUCCESS) {
        return ret;
    }
#endif

#ifdef SRS_AUTO_HTTP_SERVER
    srs_assert(http_stream_mux);
    if ((ret = http_stream_mux->initialize()) != ERROR_SUCCESS) {
        return ret;
    }
#endif

#ifdef SRS_AUTO_HTTP_PARSER
    srs_assert(!http_heartbeat);
    http_heartbeat = new SrsHttpHeartbeat();
#endif

#ifdef SRS_AUTO_INGEST
    srs_assert(!ingester);
    ingester = new SrsIngester();
#endif

    return ret;
}

int SrsServer::initialize_signal()
{
    return signal_manager->initialize();
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

int SrsServer::initialize_st()
{
    int ret = ERROR_SUCCESS;
    
    // init st
    if ((ret = srs_init_st()) != ERROR_SUCCESS) {
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
    
    // resample the resource of specified connection.
    resample_kbps(conn);
    
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
    
    if (signo == SIGINT || signo == SIGUSR2) {
#ifdef SRS_AUTO_GPERF_MC
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
        // the interval in config.
        int heartbeat_max_resolution = (int)(_srs_config->get_heartbeat_interval() / SRS_SYS_CYCLE_INTERVAL);
        
        // dynamic fetch the max.
        int __max = max;
        __max = srs_max(__max, heartbeat_max_resolution);
        
        for (int i = 0; i < __max; i++) {
            st_usleep(SRS_SYS_CYCLE_INTERVAL * 1000);
        
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
        
            if (signal_reload) {
                signal_reload = false;
                srs_info("get signal reload, to reload the config.");
                
                if ((ret = _srs_config->reload()) != ERROR_SUCCESS) {
                    srs_error("reload config failed. ret=%d", ret);
                    return ret;
                }
                srs_trace("reload config success.");
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
                srs_info("update network rtmp server info.");
                resample_kbps(NULL);
                srs_update_rtmp_server((int)conns.size(), kbps);
            }
    #ifdef SRS_AUTO_HTTP_PARSER
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
    std::vector<std::string> ports = _srs_config->get_listen();
    srs_assert((int)ports.size() > 0);
    
    close_listeners(SrsListenerRtmpStream);
    
    for (int i = 0; i < (int)ports.size(); i++) {
        SrsListener* listener = new SrsListener(this, SrsListenerRtmpStream);
        listeners.push_back(listener);
        
        int port = ::atoi(ports[i].c_str());
        if ((ret = listener->listen(port)) != ERROR_SUCCESS) {
            srs_error("RTMP stream listen at port %d failed. ret=%d", port, ret);
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
        SrsListener* listener = new SrsListener(this, SrsListenerHttpApi);
        listeners.push_back(listener);
        
        int port = _srs_config->get_http_api_listen();
        if ((ret = listener->listen(port)) != ERROR_SUCCESS) {
            srs_error("HTTP api listen at port %d failed. ret=%d", port, ret);
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

        SrsUdpListener* listener = NULL;

        std::string caster = _srs_config->get_stream_caster_engine(stream_caster);
        if (caster == SRS_CONF_DEFAULT_STREAM_CASTER_MPEGTS_OVER_UDP) {
            listener = new SrsUdpListener(this, SrsListenerMpegTsOverUdp, stream_caster);
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

        if ((ret = listener->listen(port)) != ERROR_SUCCESS) {
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
        
        if (listener->type() != type) {
            ++it;
            continue;
        }
        
        srs_freep(listener);
        it = listeners.erase(it);
    }
}

void SrsServer::resample_kbps(SrsConnection* conn, bool do_resample)
{
    // resample all when conn is NULL.
    if (!conn) {
        for (std::vector<SrsConnection*>::iterator it = conns.begin(); it != conns.end(); ++it) {
            SrsConnection* client = *it;
            srs_assert(client);
            
            // only resample, do resample when all finished.
            resample_kbps(client, false);
        }
        
        kbps->sample();
        return;
    }
    
    // resample for connection.
    conn->kbps_resample();
    
    kbps->add_delta(conn);
    
    // resample for server.
    if (do_resample) {
        kbps->sample();
    }
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
        conn = new SrsHttpConn(this, client_stfd, http_stream_mux);
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

int SrsServer::on_reload_http_stream_updated()
{
    int ret = ERROR_SUCCESS;
    
#ifdef SRS_AUTO_HTTP_SERVER
    if ((ret = on_reload_http_stream_enabled()) != ERROR_SUCCESS) {
        return ret;
    }
    
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
    if ((ret = http_stream_mux->mount(s, r)) != ERROR_SUCCESS) {
        return ret;
    }
#endif
    
    return ret;
}

void SrsServer::on_unpublish(SrsSource* s, SrsRequest* r)
{
#ifdef SRS_AUTO_HTTP_SERVER
    http_stream_mux->unmount(s, r);
#endif
}

