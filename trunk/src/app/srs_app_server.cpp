/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2018 Winlin
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

#include <srs_app_server.hpp>

#include <sys/types.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
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
#include <srs_app_kafka.hpp>
#include <srs_app_thread.hpp>
#include <srs_app_coworkers.hpp>

// system interval in ms,
// all resolution times should be times togother,
// for example, system-interval is x=1s(1000ms),
// then rusage can be 3*x, for instance, 3*1=3s,
// the meminfo canbe 6*x, for instance, 6*1=6s,
// for performance refine, @see: https://github.com/ossrs/srs/issues/194
// @remark, recomment to 1000ms.
#define SRS_SYS_CYCLE_INTERVAL 1000

// update time interval:
//      SRS_SYS_CYCLE_INTERVAL * SRS_SYS_TIME_RESOLUTION_MS_TIMES
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

SrsBufferListener::SrsBufferListener(SrsServer* svr, SrsListenerType t) : SrsListener(svr, t)
{
    listener = NULL;
}

SrsBufferListener::~SrsBufferListener()
{
    srs_freep(listener);
}

srs_error_t SrsBufferListener::listen(string i, int p)
{
    srs_error_t err = srs_success;
    
    ip = i;
    port = p;
    
    srs_freep(listener);
    listener = new SrsTcpListener(this, ip, port);
    
    if ((err = listener->listen()) != srs_success) {
        return srs_error_wrap(err, "buffered tcp listen");
    }
    
    string v = srs_listener_type2string(type);
    srs_trace("%s listen at tcp://%s:%d, fd=%d", v.c_str(), ip.c_str(), port, listener->fd());
    
    return err;
}

srs_error_t SrsBufferListener::on_tcp_client(srs_netfd_t stfd)
{
    srs_error_t err = server->accept_client(type, stfd);
    if (err != srs_success) {
        srs_warn("accept client failed, err is %s", srs_error_desc(err).c_str());
        srs_freep(err);
    }
    
    return srs_success;
}

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

srs_error_t SrsRtspListener::listen(string i, int p)
{
    srs_error_t err = srs_success;
    
    // the caller already ensure the type is ok,
    // we just assert here for unknown stream caster.
    srs_assert(type == SrsListenerRtsp);
    
    ip = i;
    port = p;
    
    srs_freep(listener);
    listener = new SrsTcpListener(this, ip, port);
    
    if ((err = listener->listen()) != srs_success) {
        return srs_error_wrap(err, "rtsp listen %s:%d", ip.c_str(), port);
    }
    
    string v = srs_listener_type2string(type);
    srs_trace("%s listen at tcp://%s:%d, fd=%d", v.c_str(), ip.c_str(), port, listener->fd());
    
    return err;
}

srs_error_t SrsRtspListener::on_tcp_client(srs_netfd_t stfd)
{
    srs_error_t err = caster->on_tcp_client(stfd);
    if (err != srs_success) {
        srs_warn("accept client failed, err is %s", srs_error_desc(err).c_str());
        srs_freep(err);
    }
    
    return srs_success;
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

srs_error_t SrsHttpFlvListener::listen(string i, int p)
{
    srs_error_t err = srs_success;
    
    // the caller already ensure the type is ok,
    // we just assert here for unknown stream caster.
    srs_assert(type == SrsListenerFlv);
    
    ip = i;
    port = p;
    
    if ((err = caster->initialize()) != srs_success) {
        return srs_error_wrap(err, "init caster %s:%d", ip.c_str(), port);
    }
    
    srs_freep(listener);
    listener = new SrsTcpListener(this, ip, port);
    
    if ((err = listener->listen()) != srs_success) {
        return srs_error_wrap(err, "listen");
    }
    
    string v = srs_listener_type2string(type);
    srs_trace("%s listen at tcp://%s:%d, fd=%d", v.c_str(), ip.c_str(), port, listener->fd());
    
    return err;
}

srs_error_t SrsHttpFlvListener::on_tcp_client(srs_netfd_t stfd)
{
    srs_error_t err = caster->on_tcp_client(stfd);
    if (err != srs_success) {
        srs_warn("accept client failed, err is %s", srs_error_desc(err).c_str());
        srs_freep(err);
    }
    
    return err;
}

SrsUdpStreamListener::SrsUdpStreamListener(SrsServer* svr, SrsListenerType t, ISrsUdpHandler* c) : SrsListener(svr, t)
{
    listener = NULL;
    caster = c;
}

SrsUdpStreamListener::~SrsUdpStreamListener()
{
    srs_freep(listener);
}

srs_error_t SrsUdpStreamListener::listen(string i, int p)
{
    srs_error_t err = srs_success;
    
    // the caller already ensure the type is ok,
    // we just assert here for unknown stream caster.
    srs_assert(type == SrsListenerMpegTsOverUdp);
    
    ip = i;
    port = p;
    
    srs_freep(listener);
    listener = new SrsUdpListener(caster, ip, port);
    
    if ((err = listener->listen()) != srs_success) {
        return srs_error_wrap(err, "listen %s:%d", ip.c_str(), port);
    }
    
    // notify the handler the fd changed.
    if ((err = caster->on_stfd_change(listener->stfd())) != srs_success) {
        return srs_error_wrap(err, "notify fd change failed");
    }
    
    string v = srs_listener_type2string(type);
    srs_trace("%s listen at udp://%s:%d, fd=%d", v.c_str(), ip.c_str(), port, listener->fd());
    
    return err;
}

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

SrsSignalManager* SrsSignalManager::instance = NULL;

SrsSignalManager::SrsSignalManager(SrsServer* s)
{
    SrsSignalManager::instance = this;
    
    server = s;
    sig_pipe[0] = sig_pipe[1] = -1;
    trd = new SrsSTCoroutine("signal", this);
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
    
    srs_freep(trd);
}

srs_error_t SrsSignalManager::initialize()
{
    /* Create signal pipe */
    if (pipe(sig_pipe) < 0) {
        return srs_error_new(ERROR_SYSTEM_CREATE_PIPE, "create pipe");
    }
    
    if ((signal_read_stfd = srs_netfd_open(sig_pipe[0])) == NULL) {
        return srs_error_new(ERROR_SYSTEM_CREATE_PIPE, "open pipe");
    }
    
    return srs_success;
}

srs_error_t SrsSignalManager::start()
{
    srs_error_t err = srs_success;
    
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
    sigaction(SRS_SIGNAL_REOPEN_LOG, &sa, NULL);
    
    srs_trace("signal installed, reload=%d, reopen=%d, grace_quit=%d",
              SRS_SIGNAL_RELOAD, SRS_SIGNAL_REOPEN_LOG, SRS_SIGNAL_GRACEFULLY_QUIT);
    
    if ((err = trd->start()) != srs_success) {
        return srs_error_wrap(err, "signal manager");
    }
    
    return err;
}

srs_error_t SrsSignalManager::cycle()
{
    srs_error_t err = srs_success;
    
    while (true) {
        if ((err = trd->pull()) != srs_success) {
            return srs_error_wrap(err, "signal manager");
        }
        
        int signo;
        
        /* Read the next signal from the pipe */
        srs_read(signal_read_stfd, &signo, sizeof(int), SRS_UTIME_NO_TIMEOUT);
        
        /* Process signal synchronously */
        server->on_signal(signo);
    }
    
    return err;
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
    
    signal_manager = new SrsSignalManager(this);
    conn_manager = new SrsCoroutineManager();
    
    handler = NULL;
    ppid = ::getppid();
    
    // donot new object in constructor,
    // for some global instance is not ready now,
    // new these objects in initialize instead.
    http_api_mux = new SrsHttpServeMux();
    http_server = new SrsHttpServer(this);
    http_heartbeat = new SrsHttpHeartbeat();
    ingester = new SrsIngester();
}

SrsServer::~SrsServer()
{
    destroy();
}

void SrsServer::destroy()
{
    srs_warn("start destroy server");
    
    dispose();
    
    srs_freep(http_api_mux);
    srs_freep(http_server);
    srs_freep(http_heartbeat);
    srs_freep(ingester);
    
    if (pid_fd > 0) {
        ::close(pid_fd);
        pid_fd = -1;
    }
    
    srs_freep(signal_manager);
    srs_freep(conn_manager);
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
    
    // @remark don't dispose ingesters, for too slow.
    
#ifdef SRS_AUTO_KAFKA
    srs_dispose_kafka();
#endif
    
    // dispose the source for hls and dvr.
    SrsSource::dispose_all();
    
    // @remark don't dispose all connections, for too slow.
    
#ifdef SRS_AUTO_MEM_WATCH
    srs_memory_report();
#endif
}

srs_error_t SrsServer::initialize(ISrsServerCycle* ch)
{
    srs_error_t err = srs_success;
    
    // ensure the time is ok.
    srs_update_system_time_ms();
    
    // for the main objects(server, config, log, context),
    // never subscribe handler in constructor,
    // instead, subscribe handler in initialize method.
    srs_assert(_srs_config);
    _srs_config->subscribe(this);
    
    handler = ch;
    if(handler && (err = handler->initialize()) != srs_success){
        return srs_error_wrap(err, "handler initialize");
    }
    
    if ((err = http_api_mux->initialize()) != srs_success) {
        return srs_error_wrap(err, "http api initialize");
    }
    
    if ((err = http_server->initialize()) != srs_success) {
        return srs_error_wrap(err, "http server initialize");
    }
    
    return err;
}

srs_error_t SrsServer::initialize_st()
{
    srs_error_t err = srs_success;
    
    // init st
    if ((err = srs_st_init()) != srs_success) {
        return srs_error_wrap(err, "initialize st failed");
    }
    
    // @remark, st alloc segment use mmap, which only support 32757 threads,
    // if need to support more, for instance, 100k threads, define the macro MALLOC_STACK.
    // TODO: FIXME: maybe can use "sysctl vm.max_map_count" to refine.
#define __MMAP_MAX_CONNECTIONS 32756
    if (_srs_config->get_max_connections() > __MMAP_MAX_CONNECTIONS) {
        srs_error("st mmap for stack allocation must <= %d threads, "
                  "@see Makefile of st for MALLOC_STACK, please build st manually by "
                  "\"make EXTRA_CFLAGS=-DMALLOC_STACK linux-debug\"", __MMAP_MAX_CONNECTIONS);
        return srs_error_new(ERROR_ST_EXCEED_THREADS, "%d exceed max %d threads",
            _srs_config->get_max_connections(), __MMAP_MAX_CONNECTIONS);
    }
    
    // set current log id.
    _srs_context->generate_id();
    
    // initialize the conponents that depends on st.
#ifdef SRS_AUTO_KAFKA
    if ((err = srs_initialize_kafka()) != srs_success) {
        return srs_error_wrap(err, "initialize kafka");
    }
#endif
    
    // check asprocess.
    bool asprocess = _srs_config->get_asprocess();
    if (asprocess && ppid == 1) {
        return srs_error_new(ERROR_SYSTEM_ASSERT_FAILED, "ppid=%d illegal for asprocess", ppid);
    }
    
    srs_trace("server main cid=%d, pid=%d, ppid=%d, asprocess=%d",
        _srs_context->get_id(), ::getpid(), ppid, asprocess);
    
    return err;
}

srs_error_t SrsServer::initialize_signal()
{
    return signal_manager->initialize();
}

srs_error_t SrsServer::acquire_pid_file()
{
    // when srs in dolphin mode, no need the pid file.
    if (_srs_config->is_dolphin()) {
        return srs_success;
    }
    
    std::string pid_file = _srs_config->get_pid_file();
    
    // -rw-r--r--
    // 644
    int mode = S_IRUSR | S_IWUSR |  S_IRGRP | S_IROTH;
    
    int fd;
    // open pid file
    if ((fd = ::open(pid_file.c_str(), O_WRONLY | O_CREAT, mode)) == -1) {
        return srs_error_new(ERROR_SYSTEM_PID_ACQUIRE, "open pid file=%s", pid_file.c_str());
    }
    
    // require write lock
    struct flock lock;
    
    lock.l_type = F_WRLCK; // F_RDLCK, F_WRLCK, F_UNLCK
    lock.l_start = 0; // type offset, relative to l_whence
    lock.l_whence = SEEK_SET;  // SEEK_SET, SEEK_CUR, SEEK_END
    lock.l_len = 0;
    
    if (fcntl(fd, F_SETLK, &lock) == -1) {
        if(errno == EACCES || errno == EAGAIN) {
            ::close(fd);
            srs_error("srs is already running!");
            return srs_error_new(ERROR_SYSTEM_PID_ALREADY_RUNNING, "srs is already running");
        }
        return srs_error_new(ERROR_SYSTEM_PID_LOCK, "access to pid=%s", pid_file.c_str());
    }
    
    // truncate file
    if (ftruncate(fd, 0) != 0) {
        return srs_error_new(ERROR_SYSTEM_PID_TRUNCATE_FILE, "truncate pid file=%s", pid_file.c_str());
    }
    
    // write the pid
    string pid = srs_int2str(getpid());
    if (write(fd, pid.c_str(), pid.length()) != (int)pid.length()) {
        return srs_error_new(ERROR_SYSTEM_PID_WRITE_FILE, "write pid=%d to file=%s", pid.c_str(), pid_file.c_str());
    }
    
    // auto close when fork child process.
    int val;
    if ((val = fcntl(fd, F_GETFD, 0)) < 0) {
        return srs_error_new(ERROR_SYSTEM_PID_GET_FILE_INFO, "fcntl fd=%d", fd);
    }
    val |= FD_CLOEXEC;
    if (fcntl(fd, F_SETFD, val) < 0) {
        return srs_error_new(ERROR_SYSTEM_PID_SET_FILE_INFO, "lock file=%s fd=%d", pid_file.c_str(), fd);
    }
    
    srs_trace("write pid=%s to %s success!", pid.c_str(), pid_file.c_str());
    pid_fd = fd;
    
    return srs_success;
}

srs_error_t SrsServer::listen()
{
    srs_error_t err = srs_success;
    
    if ((err = listen_rtmp()) != srs_success) {
        return srs_error_wrap(err, "rtmp listen");
    }
    
    if ((err = listen_http_api()) != srs_success) {
        return srs_error_wrap(err, "http api listen");
    }
    
    if ((err = listen_http_stream()) != srs_success) {
        return srs_error_wrap(err, "http stream listen");
    }
    
    if ((err = listen_stream_caster()) != srs_success) {
        return srs_error_wrap(err, "stream caster listen");
    }
    
    if ((err = conn_manager->start()) != srs_success) {
        return srs_error_wrap(err, "connection manager");
    }
    
    return err;
}

srs_error_t SrsServer::register_signal()
{
    srs_error_t err = srs_success;
    
    if ((err = signal_manager->start()) != srs_success) {
        return srs_error_wrap(err, "signal manager start");
    }
    
    return err;
}

srs_error_t SrsServer::http_handle()
{
    srs_error_t err = srs_success;
    
    if ((err = http_api_mux->handle("/", new SrsHttpNotFoundHandler())) != srs_success) {
        return srs_error_wrap(err, "handle not found");
    }
    if ((err = http_api_mux->handle("/api/", new SrsGoApiApi())) != srs_success) {
        return srs_error_wrap(err, "handle api");
    }
    if ((err = http_api_mux->handle("/api/v1/", new SrsGoApiV1())) != srs_success) {
        return srs_error_wrap(err, "handle v1");
    }
    if ((err = http_api_mux->handle("/api/v1/versions", new SrsGoApiVersion())) != srs_success) {
        return srs_error_wrap(err, "handle versions");
    }
    if ((err = http_api_mux->handle("/api/v1/summaries", new SrsGoApiSummaries())) != srs_success) {
        return srs_error_wrap(err, "handle summaries");
    }
    if ((err = http_api_mux->handle("/api/v1/rusages", new SrsGoApiRusages())) != srs_success) {
        return srs_error_wrap(err, "handle rusages");
    }
    if ((err = http_api_mux->handle("/api/v1/self_proc_stats", new SrsGoApiSelfProcStats())) != srs_success) {
        return srs_error_wrap(err, "handle self proc stats");
    }
    if ((err = http_api_mux->handle("/api/v1/system_proc_stats", new SrsGoApiSystemProcStats())) != srs_success) {
        return srs_error_wrap(err, "handle system proc stats");
    }
    if ((err = http_api_mux->handle("/api/v1/meminfos", new SrsGoApiMemInfos())) != srs_success) {
        return srs_error_wrap(err, "handle meminfos");
    }
    if ((err = http_api_mux->handle("/api/v1/authors", new SrsGoApiAuthors())) != srs_success) {
        return srs_error_wrap(err, "handle authors");
    }
    if ((err = http_api_mux->handle("/api/v1/features", new SrsGoApiFeatures())) != srs_success) {
        return srs_error_wrap(err, "handle features");
    }
    if ((err = http_api_mux->handle("/api/v1/vhosts/", new SrsGoApiVhosts())) != srs_success) {
        return srs_error_wrap(err, "handle vhosts");
    }
    if ((err = http_api_mux->handle("/api/v1/streams/", new SrsGoApiStreams())) != srs_success) {
        return srs_error_wrap(err, "handle streams");
    }
    if ((err = http_api_mux->handle("/api/v1/clients/", new SrsGoApiClients())) != srs_success) {
        return srs_error_wrap(err, "handle clients");
    }
    if ((err = http_api_mux->handle("/api/v1/raw", new SrsGoApiRaw(this))) != srs_success) {
        return srs_error_wrap(err, "handle raw");
    }
    if ((err = http_api_mux->handle("/api/v1/clusters", new SrsGoApiClusters())) != srs_success) {
        return srs_error_wrap(err, "handle raw");
    }
    
    // test the request info.
    if ((err = http_api_mux->handle("/api/v1/tests/requests", new SrsGoApiRequests())) != srs_success) {
        return srs_error_wrap(err, "handle tests requests");
    }
    // test the error code response.
    if ((err = http_api_mux->handle("/api/v1/tests/errors", new SrsGoApiError())) != srs_success) {
        return srs_error_wrap(err, "handle tests errors");
    }
    // test the redirect mechenism.
    if ((err = http_api_mux->handle("/api/v1/tests/redirects", new SrsHttpRedirectHandler("/api/v1/tests/errors", SRS_CONSTS_HTTP_MovedPermanently))) != srs_success) {
        return srs_error_wrap(err, "handle tests redirects");
    }
    // test the http vhost.
    if ((err = http_api_mux->handle("error.srs.com/api/v1/tests/errors", new SrsGoApiError())) != srs_success) {
        return srs_error_wrap(err, "handle tests errors for error.srs.com");
    }
    
    // TODO: FIXME: for console.
    // TODO: FIXME: support reload.
    std::string dir = _srs_config->get_http_stream_dir() + "/console";
    if ((err = http_api_mux->handle("/console/", new SrsHttpFileServer(dir))) != srs_success) {
        return srs_error_wrap(err, "handle console at %s", dir.c_str());
    }
    srs_trace("http: api mount /console to %s", dir.c_str());
    
    return err;
}

srs_error_t SrsServer::ingest()
{
    srs_error_t err = srs_success;
    
    if ((err = ingester->start()) != srs_success) {
        return srs_error_wrap(err, "ingest start");
    }
    
    return err;
}

srs_error_t SrsServer::cycle()
{
    srs_error_t err = do_cycle();
    
#ifdef SRS_AUTO_GPERF_MC
    destroy();
    
    // remark, for gmc, never invoke the exit().
    srs_warn("sleep a long time for system st-threads to cleanup.");
    srs_usleep(3 * 1000 * 1000);
    srs_warn("system quit");
#else
    // normally quit with neccessary cleanup by dispose().
    srs_warn("main cycle terminated, system quit normally.");
    dispose();
    srs_trace("srs terminated");
    
    // for valgrind to detect.
    srs_freep(_srs_config);
    srs_freep(_srs_log);
    
    exit(0);
#endif
    
    return err;
}


void SrsServer::on_signal(int signo)
{
    if (signo == SRS_SIGNAL_RELOAD) {
        signal_reload = true;
        return;
    }
    
#ifndef SRS_AUTO_GPERF_MC
    if (signo == SRS_SIGNAL_REOPEN_LOG) {
        _srs_log->reopen();
        srs_warn("reopen log file");
        return;
    }
#endif
    
#ifdef SRS_AUTO_GPERF_MC
    if (signo == SRS_SIGNAL_REOPEN_LOG) {
        signal_gmc_stop = true;
        srs_warn("for gmc, the SIGUSR1 used as SIGINT");
        return;
    }
#endif
    
    if (signo == SRS_SIGNAL_PERSISTENCE_CONFIG) {
        signal_persistence_config = true;
        return;
    }
    
    if (signo == SIGINT) {
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

srs_error_t SrsServer::do_cycle()
{
    srs_error_t err = srs_success;
    
    // find the max loop
    int max = srs_max(0, SRS_SYS_TIME_RESOLUTION_MS_TIMES);
    
    max = srs_max(max, SRS_SYS_RUSAGE_RESOLUTION_TIMES);
    max = srs_max(max, SRS_SYS_CPU_STAT_RESOLUTION_TIMES);
    max = srs_max(max, SRS_SYS_DISK_STAT_RESOLUTION_TIMES);
    max = srs_max(max, SRS_SYS_MEMINFO_RESOLUTION_TIMES);
    max = srs_max(max, SRS_SYS_PLATFORM_INFO_RESOLUTION_TIMES);
    max = srs_max(max, SRS_SYS_NETWORK_DEVICE_RESOLUTION_TIMES);
    max = srs_max(max, SRS_SYS_NETWORK_RTMP_SERVER_RESOLUTION_TIMES);
    
    // for asprocess.
    bool asprocess = _srs_config->get_asprocess();
    
    // the deamon thread, update the time cache
    // TODO: FIXME: use SrsHourGlass.
    while (true) {
        if (handler && (err = handler->on_cycle()) != srs_success) {
            return srs_error_wrap(err, "handle callback");
        }
        
        // the interval in config.
        int heartbeat_max_resolution = (int)(_srs_config->get_heartbeat_interval() / SRS_SYS_CYCLE_INTERVAL);
        
        // dynamic fetch the max.
        int dynamic_max = srs_max(max, heartbeat_max_resolution);
        
        for (int i = 0; i < dynamic_max; i++) {
            srs_usleep(SRS_SYS_CYCLE_INTERVAL * 1000);
            
            // asprocess check.
            if (asprocess && ::getppid() != ppid) {
                return srs_error_new(ERROR_ASPROCESS_PPID, "asprocess ppid changed from %d to %d", ppid, ::getppid());
            }
            
            // gracefully quit for SIGINT or SIGTERM.
            if (signal_gracefully_quit) {
                srs_trace("cleanup for gracefully terminate.");
                return err;
            }
            
            // for gperf heap checker,
            // @see: research/gperftools/heap-checker/heap_checker.cc
            // if user interrupt the program, exit to check mem leak.
            // but, if gperf, use reload to ensure main return normally,
            // because directly exit will cause core-dump.
#ifdef SRS_AUTO_GPERF_MC
            if (signal_gmc_stop) {
                srs_warn("gmc got singal to stop server.");
                return err;
            }
#endif
            
            // do persistence config to file.
            if (signal_persistence_config) {
                signal_persistence_config = false;
                srs_info("get signal to persistence config to file.");
                
                if ((err = _srs_config->persistence()) != srs_success) {
                    return srs_error_wrap(err, "config persistence to file");
                }
                srs_trace("persistence config to file success.");
            }
            
            // do reload the config.
            if (signal_reload) {
                signal_reload = false;
                srs_info("get signal to reload the config.");
                
                if ((err = _srs_config->reload()) != srs_success) {
                    return srs_error_wrap(err, "config reload");
                }
                srs_trace("reload config success.");
            }
            
            // notice the stream sources to cycle.
            if ((err = SrsSource::cycle_all()) != srs_success) {
                return srs_error_wrap(err, "source cycle");
            }
            
            // update the cache time
            if ((i % SRS_SYS_TIME_RESOLUTION_MS_TIMES) == 0) {
                srs_info("update current time cache.");
                srs_update_system_time_ms();
            }
            
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
            if (_srs_config->get_heartbeat_enabled()) {
                if ((i % heartbeat_max_resolution) == 0) {
                    srs_info("do http heartbeat, for internal server to report.");
                    http_heartbeat->heartbeat();
                }
            }
            
            srs_info("server main thread loop");
        }
    }
    
    return err;
}

srs_error_t SrsServer::listen_rtmp()
{
    srs_error_t err = srs_success;
    
    // stream service port.
    std::vector<std::string> ip_ports = _srs_config->get_listens();
    srs_assert((int)ip_ports.size() > 0);
    
    close_listeners(SrsListenerRtmpStream);
    
    for (int i = 0; i < (int)ip_ports.size(); i++) {
        SrsListener* listener = new SrsBufferListener(this, SrsListenerRtmpStream);
        listeners.push_back(listener);
        
        std::string ip;
        int port;
        srs_parse_endpoint(ip_ports[i], ip, port);
        
        if ((err = listener->listen(ip, port)) != srs_success) {
            srs_error_wrap(err, "rtmp listen %s:%d", ip.c_str(), port);
        }
    }
    
    return err;
}

srs_error_t SrsServer::listen_http_api()
{
    srs_error_t err = srs_success;
    
    close_listeners(SrsListenerHttpApi);
    if (_srs_config->get_http_api_enabled()) {
        SrsListener* listener = new SrsBufferListener(this, SrsListenerHttpApi);
        listeners.push_back(listener);
        
        std::string ep = _srs_config->get_http_api_listen();
        
        std::string ip;
        int port;
        srs_parse_endpoint(ep, ip, port);
        
        if ((err = listener->listen(ip, port)) != srs_success) {
            return srs_error_wrap(err, "http api listen %s:%d", ip.c_str(), port);
        }
    }
    
    return err;
}

srs_error_t SrsServer::listen_http_stream()
{
    srs_error_t err = srs_success;
    
    close_listeners(SrsListenerHttpStream);
    if (_srs_config->get_http_stream_enabled()) {
        SrsListener* listener = new SrsBufferListener(this, SrsListenerHttpStream);
        listeners.push_back(listener);
        
        std::string ep = _srs_config->get_http_stream_listen();
        
        std::string ip;
        int port;
        srs_parse_endpoint(ep, ip, port);
        
        if ((err = listener->listen(ip, port)) != srs_success) {
            return srs_error_wrap(err, "http stream listen %s:%d", ip.c_str(), port);
        }
    }
    
    return err;
}

srs_error_t SrsServer::listen_stream_caster()
{
    srs_error_t err = srs_success;
    
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
            return srs_error_new(ERROR_STREAM_CASTER_ENGINE, "invalid caster %s", caster.c_str());
        }
        srs_assert(listener != NULL);
        
        listeners.push_back(listener);
        
        int port = _srs_config->get_stream_caster_listen(stream_caster);
        if (port <= 0) {
            return srs_error_new(ERROR_STREAM_CASTER_PORT, "invalid port=%d", port);
        }
        
        // TODO: support listen at <[ip:]port>
        if ((err = listener->listen(srs_any_address4listener(), port)) != srs_success) {
            return srs_error_wrap(err, "listen at %d", port);
        }
    }
    
    return err;
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

srs_error_t SrsServer::accept_client(SrsListenerType type, srs_netfd_t stfd)
{
    srs_error_t err = srs_success;
    
    SrsConnection* conn = NULL;
    
    if ((err = fd2conn(type, stfd, &conn)) != srs_success) {
        return srs_error_wrap(err, "fd2conn");
    }
    srs_assert(conn);
    
    // directly enqueue, the cycle thread will remove the client.
    conns.push_back(conn);
    
    // cycle will start process thread and when finished remove the client.
    // @remark never use the conn, for it maybe destroyed.
    if ((err = conn->start()) != srs_success) {
        return srs_error_wrap(err, "start conn coroutine");
    }
    
    return err;
}

srs_error_t SrsServer::fd2conn(SrsListenerType type, srs_netfd_t stfd, SrsConnection** pconn)
{
    srs_error_t err = srs_success;
    
    int fd = srs_netfd_fileno(stfd);
    string ip = srs_get_peer_ip(fd);
    
    // for some keep alive application, for example, the keepalived,
    // will send some tcp packet which we cann't got the ip,
    // we just ignore it.
    if (ip.empty()) {
        return srs_error_new(ERROR_SOCKET_GET_PEER_IP, "ignore empty ip, fd=%d", fd);
    }
    
    // check connection limitation.
    int max_connections = _srs_config->get_max_connections();
    if (handler && (err = handler->on_accept_client(max_connections, (int)conns.size())) != srs_success) {
        return srs_error_wrap(err, "drop client fd=%d, max=%d, cur=%d for err: %s",
            fd, max_connections, (int)conns.size(), srs_error_desc(err).c_str());
    }
    if ((int)conns.size() >= max_connections) {
        return srs_error_new(ERROR_EXCEED_CONNECTIONS,
            "drop fd=%d, max=%d, cur=%d for exceed connection limits",
            fd, max_connections, (int)conns.size());
    }
    
    // avoid fd leak when fork.
    // @see https://github.com/ossrs/srs/issues/518
    if (true) {
        int val;
        if ((val = fcntl(fd, F_GETFD, 0)) < 0) {
            return srs_error_new(ERROR_SYSTEM_PID_GET_FILE_INFO, "fnctl F_GETFD error! fd=%d", fd);
        }
        val |= FD_CLOEXEC;
        if (fcntl(fd, F_SETFD, val) < 0) {
            return srs_error_new(ERROR_SYSTEM_PID_SET_FILE_INFO, "fcntl F_SETFD error! fd=%d", fd);
        }
    }
    
    if (type == SrsListenerRtmpStream) {
        *pconn = new SrsRtmpConn(this, stfd, ip);
    } else if (type == SrsListenerHttpApi) {
        *pconn = new SrsHttpApi(this, stfd, http_api_mux, ip);
    } else if (type == SrsListenerHttpStream) {
        *pconn = new SrsResponseOnlyHttpConn(this, stfd, http_server, ip);
    } else {
        srs_warn("close for no service handler. fd=%d, ip=%s", fd, ip.c_str());
        srs_close_stfd(stfd);
        return err;
    }
    
    return err;
}

void SrsServer::remove(ISrsConnection* c)
{
    SrsConnection* conn = dynamic_cast<SrsConnection*>(c);
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
    
    // use manager to free it async.
    conn_manager->remove(c);
}

srs_error_t SrsServer::on_reload_listen()
{
    srs_error_t err = srs_success;
    
    if ((err = listen()) != srs_success) {
        return srs_error_wrap(err, "reload listen");
    }
    
    return err;
}

srs_error_t SrsServer::on_reload_pid()
{
    srs_error_t err = srs_success;
    
    if (pid_fd > 0) {
        ::close(pid_fd);
        pid_fd = -1;
    }
    
    if ((err = acquire_pid_file()) != srs_success) {
        return srs_error_wrap(err, "reload pid");
    }
    
    return err;
}

srs_error_t SrsServer::on_reload_vhost_added(std::string vhost)
{
    srs_error_t err = srs_success;
    
    if (!_srs_config->get_vhost_http_enabled(vhost)) {
        return err;
    }
    
    // TODO: FIXME: should handle the event in SrsHttpStaticServer
    if ((err = on_reload_vhost_http_updated()) != srs_success) {
        return srs_error_wrap(err, "reload vhost added");
    }
    
    return err;
}

srs_error_t SrsServer::on_reload_vhost_removed(std::string /*vhost*/)
{
    srs_error_t err = srs_success;
    
    // TODO: FIXME: should handle the event in SrsHttpStaticServer
    if ((err = on_reload_vhost_http_updated()) != srs_success) {
        return srs_error_wrap(err, "reload vhost removed");
    }
    
    return err;
}

srs_error_t SrsServer::on_reload_http_api_enabled()
{
    srs_error_t err = srs_success;
    
    if ((err = listen_http_api()) != srs_success) {
        return srs_error_wrap(err, "reload http_api");
    }
    
    return err;
}

srs_error_t SrsServer::on_reload_http_api_disabled()
{
    close_listeners(SrsListenerHttpApi);
    return srs_success;
}

srs_error_t SrsServer::on_reload_http_stream_enabled()
{
    srs_error_t err = srs_success;
    
    if ((err = listen_http_stream()) != srs_success) {
        return srs_error_wrap(err, "reload http_stream enabled");
    }
    
    return err;
}

srs_error_t SrsServer::on_reload_http_stream_disabled()
{
    close_listeners(SrsListenerHttpStream);
    return srs_success;
}

// TODO: FIXME: rename to http_remux
srs_error_t SrsServer::on_reload_http_stream_updated()
{
    srs_error_t err = srs_success;
    
    if ((err = on_reload_http_stream_enabled()) != srs_success) {
        return srs_error_wrap(err, "reload http_stream updated");
    }
    
    // TODO: FIXME: should handle the event in SrsHttpStaticServer
    if ((err = on_reload_vhost_http_updated()) != srs_success) {
        return srs_error_wrap(err, "reload http_stream updated");
    }
    
    return err;
}

srs_error_t SrsServer::on_publish(SrsSource* s, SrsRequest* r)
{
    srs_error_t err = srs_success;
    
    if ((err = http_server->http_mount(s, r)) != srs_success) {
        return srs_error_wrap(err, "http mount");
    }
    
    SrsCoWorkers* coworkers = SrsCoWorkers::instance();
    if ((err = coworkers->on_publish(s, r)) != srs_success) {
        return srs_error_wrap(err, "coworkers");
    }
    
    return err;
}

void SrsServer::on_unpublish(SrsSource* s, SrsRequest* r)
{
    http_server->http_unmount(s, r);
    
    SrsCoWorkers* coworkers = SrsCoWorkers::instance();
    coworkers->on_unpublish(s, r);
}

