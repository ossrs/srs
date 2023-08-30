//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#include <srs_app_server.hpp>

#include <sys/types.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <algorithm>
#if !defined(SRS_OSX) && !defined(SRS_CYGWIN64)
#include <sys/inotify.h>
#endif
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
#include <srs_app_statistic.hpp>
#include <srs_app_caster_flv.hpp>
#include <srs_kernel_consts.hpp>
#include <srs_app_coworkers.hpp>
#include <srs_protocol_log.hpp>
#include <srs_app_latest_version.hpp>
#include <srs_app_conn.hpp>
#ifdef SRS_RTC
#include <srs_app_rtc_network.hpp>
#endif
#ifdef SRS_GB28181
#include <srs_app_gb28181.hpp>
#endif

SrsSignalManager* SrsSignalManager::instance = NULL;

SrsSignalManager::SrsSignalManager(SrsServer* s)
{
    SrsSignalManager::instance = this;
    
    server = s;
    sig_pipe[0] = sig_pipe[1] = -1;
    trd = new SrsSTCoroutine("signal", this, _srs_context->get_id());
    signal_read_stfd = NULL;
}

SrsSignalManager::~SrsSignalManager()
{
    srs_freep(trd);

    srs_close_stfd(signal_read_stfd);
    
    if (sig_pipe[0] > 0) {
        ::close(sig_pipe[0]);
    }
    if (sig_pipe[1] > 0) {
        ::close(sig_pipe[1]);
    }
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
    sigaction(SRS_SIGNAL_FAST_QUIT, &sa, NULL);

    sa.sa_handler = SrsSignalManager::sig_catcher;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SRS_SIGNAL_GRACEFULLY_QUIT, &sa, NULL);

    sa.sa_handler = SrsSignalManager::sig_catcher;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SRS_SIGNAL_ASSERT_ABORT, &sa, NULL);
    
    sa.sa_handler = SrsSignalManager::sig_catcher;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    
    sa.sa_handler = SrsSignalManager::sig_catcher;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SRS_SIGNAL_REOPEN_LOG, &sa, NULL);
    
    srs_trace("signal installed, reload=%d, reopen=%d, fast_quit=%d, grace_quit=%d",
              SRS_SIGNAL_RELOAD, SRS_SIGNAL_REOPEN_LOG, SRS_SIGNAL_FAST_QUIT, SRS_SIGNAL_GRACEFULLY_QUIT);
    
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

// Whether we are in docker, defined in main module.
extern bool _srs_in_docker;

SrsInotifyWorker::SrsInotifyWorker(SrsServer* s)
{
    server = s;
    trd = new SrsSTCoroutine("inotify", this);
    inotify_fd = NULL;
}

SrsInotifyWorker::~SrsInotifyWorker()
{
    srs_freep(trd);
    srs_close_stfd(inotify_fd);
}

srs_error_t SrsInotifyWorker::start()
{
    srs_error_t err = srs_success;

#if !defined(SRS_OSX) && !defined(SRS_CYGWIN64)
    // Whether enable auto reload config.
    bool auto_reload = _srs_config->inotify_auto_reload();
    if (!auto_reload && _srs_in_docker && _srs_config->auto_reload_for_docker()) {
        srs_warn("enable auto reload for docker");
        auto_reload = true;
    }

    if (!auto_reload) {
        return err;
    }

    // Create inotify to watch config file.
    int fd = ::inotify_init1(IN_NONBLOCK);
    if (fd < 0) {
        return srs_error_new(ERROR_INOTIFY_CREATE, "create inotify");
    }

    // Open as stfd to read by ST.
    if ((inotify_fd = srs_netfd_open(fd)) == NULL) {
        ::close(fd);
        return srs_error_new(ERROR_INOTIFY_OPENFD, "open fd=%d", fd);
    }

    if (((err = srs_fd_closeexec(fd))) != srs_success) {
        return srs_error_wrap(err, "closeexec fd=%d", fd);
    }

    // /* the following are legal, implemented events that user-space can watch for */
    // #define IN_ACCESS               0x00000001      /* File was accessed */
    // #define IN_MODIFY               0x00000002      /* File was modified */
    // #define IN_ATTRIB               0x00000004      /* Metadata changed */
    // #define IN_CLOSE_WRITE          0x00000008      /* Writtable file was closed */
    // #define IN_CLOSE_NOWRITE        0x00000010      /* Unwrittable file closed */
    // #define IN_OPEN                 0x00000020      /* File was opened */
    // #define IN_MOVED_FROM           0x00000040      /* File was moved from X */
    // #define IN_MOVED_TO             0x00000080      /* File was moved to Y */
    // #define IN_CREATE               0x00000100      /* Subfile was created */
    // #define IN_DELETE               0x00000200      /* Subfile was deleted */
    // #define IN_DELETE_SELF          0x00000400      /* Self was deleted */
    // #define IN_MOVE_SELF            0x00000800      /* Self was moved */
    //
    // /* the following are legal events.  they are sent as needed to any watch */
    // #define IN_UNMOUNT              0x00002000      /* Backing fs was unmounted */
    // #define IN_Q_OVERFLOW           0x00004000      /* Event queued overflowed */
    // #define IN_IGNORED              0x00008000      /* File was ignored */
    //
    // /* helper events */
    // #define IN_CLOSE                (IN_CLOSE_WRITE | IN_CLOSE_NOWRITE) /* close */
    // #define IN_MOVE                 (IN_MOVED_FROM | IN_MOVED_TO) /* moves */
    //
    // /* special flags */
    // #define IN_ONLYDIR              0x01000000      /* only watch the path if it is a directory */
    // #define IN_DONT_FOLLOW          0x02000000      /* don't follow a sym link */
    // #define IN_EXCL_UNLINK          0x04000000      /* exclude events on unlinked objects */
    // #define IN_MASK_ADD             0x20000000      /* add to the mask of an already existing watch */
    // #define IN_ISDIR                0x40000000      /* event occurred against dir */
    // #define IN_ONESHOT              0x80000000      /* only send event once */

    // Watch the config directory events.
    string config_dir = srs_path_dirname(_srs_config->config());
    uint32_t mask = IN_MODIFY | IN_CREATE | IN_MOVED_TO; int watch_conf = 0;
    if ((watch_conf = ::inotify_add_watch(fd, config_dir.c_str(), mask)) < 0) {
        return srs_error_new(ERROR_INOTIFY_WATCH, "watch file=%s, fd=%d, watch=%d, mask=%#x",
            config_dir.c_str(), fd, watch_conf, mask);
    }
    srs_trace("auto reload watching fd=%d, watch=%d, file=%s", fd, watch_conf, config_dir.c_str());

    if ((err = trd->start()) != srs_success) {
        return srs_error_wrap(err, "inotify");
    }
#endif

    return err;
}

srs_error_t SrsInotifyWorker::cycle()
{
    srs_error_t err = srs_success;

#if !defined(SRS_OSX) && !defined(SRS_CYGWIN64)
    string config_path = _srs_config->config();
    string config_file = srs_path_basename(config_path);
    string k8s_file = "..data";

    while (true) {
        char buf[4096];
        ssize_t nn = srs_read(inotify_fd, buf, (size_t)sizeof(buf), SRS_UTIME_NO_TIMEOUT);
        if (nn < 0) {
            srs_warn("inotify ignore read failed, nn=%d", (int)nn);
            break;
        }

        // Whether config file changed.
        bool do_reload = false;

        // Parse all inotify events.
        inotify_event* ie = NULL;
        for (char* ptr = buf; ptr < buf + nn; ptr += sizeof(inotify_event) + ie->len) {
            ie = (inotify_event*)ptr;

            if (!ie->len || !ie->name) {
                continue;
            }

            string name = ie->name;
            if ((name == k8s_file || name == config_file) && ie->mask & (IN_MODIFY|IN_CREATE|IN_MOVED_TO)) {
                do_reload = true;
            }

            srs_trace("inotify event wd=%d, mask=%#x, len=%d, name=%s, reload=%d", ie->wd, ie->mask, ie->len, ie->name, do_reload);
        }

        // Notify server to do reload.
        if (do_reload && srs_path_exists(config_path)) {
            server->on_signal(SRS_SIGNAL_RELOAD);
        }

        srs_usleep(3000 * SRS_UTIME_MILLISECONDS);
    }
#endif

    return err;
}

SrsServer::SrsServer()
{
    signal_reload = false;
    signal_persistence_config = false;
    signal_gmc_stop = false;
    signal_fast_quit = false;
    signal_gracefully_quit = false;
    pid_fd = -1;
    
    signal_manager = new SrsSignalManager(this);
    conn_manager = new SrsResourceManager("TCP", true);
    latest_version_ = new SrsLatestVersion();
    ppid = ::getppid();

    rtmp_listener_ = new SrsMultipleTcpListeners(this);
    api_listener_ = new SrsTcpListener(this);
    apis_listener_ = new SrsTcpListener(this);
    http_listener_ = new SrsTcpListener(this);
    https_listener_ = new SrsTcpListener(this);
    webrtc_listener_ = new SrsTcpListener(this);
    stream_caster_flv_listener_ = new SrsHttpFlvListener();
    stream_caster_mpegts_ = new SrsUdpCasterListener();
    exporter_listener_ = new SrsTcpListener(this);
#ifdef SRS_GB28181
    stream_caster_gb28181_ = new SrsGbListener();
#endif

    // donot new object in constructor,
    // for some global instance is not ready now,
    // new these objects in initialize instead.
    http_api_mux = new SrsHttpServeMux();
    http_server = new SrsHttpServer(this);
    reuse_api_over_server_ = false;
    reuse_rtc_over_server_ = false;

    http_heartbeat = new SrsHttpHeartbeat();
    ingester = new SrsIngester();
    trd_ = new SrsSTCoroutine("srs", this, _srs_context->get_id());
    timer_ = NULL;
    wg_ = NULL;
}

SrsServer::~SrsServer()
{
    destroy();
}

void SrsServer::destroy()
{
    srs_warn("start destroy server");

    srs_freep(trd_);
    srs_freep(timer_);

    dispose();

    // If api reuse the same port of server, they're the same object.
    if (!reuse_api_over_server_) {
        srs_freep(http_api_mux);
    }
    srs_freep(http_server);

    srs_freep(http_heartbeat);
    srs_freep(ingester);
    
    if (pid_fd > 0) {
        ::close(pid_fd);
        pid_fd = -1;
    }
    
    srs_freep(signal_manager);
    srs_freep(latest_version_);
    srs_freep(conn_manager);
    srs_freep(rtmp_listener_);
    srs_freep(api_listener_);
    srs_freep(apis_listener_);
    srs_freep(http_listener_);
    srs_freep(https_listener_);
    srs_freep(webrtc_listener_);
    srs_freep(stream_caster_flv_listener_);
    srs_freep(stream_caster_mpegts_);
    srs_freep(exporter_listener_);
#ifdef SRS_GB28181
    srs_freep(stream_caster_gb28181_);
#endif
}

void SrsServer::dispose()
{
    _srs_config->unsubscribe(this);
    
    // Destroy all listeners.
    rtmp_listener_->close();
    api_listener_->close();
    apis_listener_->close();
    http_listener_->close();
    https_listener_->close();
    webrtc_listener_->close();
    stream_caster_flv_listener_->close();
    stream_caster_mpegts_->close();
    exporter_listener_->close();
#ifdef SRS_GB28181
    stream_caster_gb28181_->close();
#endif

    // Fast stop to notify FFMPEG to quit, wait for a while then fast kill.
    ingester->dispose();
    
    // dispose the source for hls and dvr.
    _srs_sources->dispose();
    
    // @remark don't dispose all connections, for too slow.
}

void SrsServer::gracefully_dispose()
{
    _srs_config->unsubscribe(this);

    // Always wait for a while to start.
    srs_usleep(_srs_config->get_grace_start_wait());
    srs_trace("start wait for %dms", srsu2msi(_srs_config->get_grace_start_wait()));

    // Destroy all listeners.
    rtmp_listener_->close();
    api_listener_->close();
    apis_listener_->close();
    http_listener_->close();
    https_listener_->close();
    webrtc_listener_->close();
    stream_caster_flv_listener_->close();
    stream_caster_mpegts_->close();
    exporter_listener_->close();
#ifdef SRS_GB28181
    stream_caster_gb28181_->close();
#endif
    srs_trace("listeners closed");

    // Fast stop to notify FFMPEG to quit, wait for a while then fast kill.
    ingester->stop();
    srs_trace("ingesters stopped");

    // Wait for connections to quit.
    // While gracefully quiting, user can requires SRS to fast quit.
    int wait_step = 1;
    while (!conn_manager->empty() && !signal_fast_quit) {
        for (int i = 0; i < wait_step && !conn_manager->empty() && !signal_fast_quit; i++) {
            srs_usleep(1000 * SRS_UTIME_MILLISECONDS);
        }

        wait_step = (wait_step * 2) % 33;
        srs_trace("wait for %d conns to quit", (int)conn_manager->size());
    }

    // dispose the source for hls and dvr.
    _srs_sources->dispose();
    srs_trace("source disposed");

    srs_usleep(_srs_config->get_grace_final_wait());
    srs_trace("final wait for %dms", srsu2msi(_srs_config->get_grace_final_wait()));
}

srs_error_t SrsServer::initialize()
{
    srs_error_t err = srs_success;
    
    // for the main objects(server, config, log, context),
    // never subscribe handler in constructor,
    // instead, subscribe handler in initialize method.
    srs_assert(_srs_config);
    _srs_config->subscribe(this);

    bool stream = _srs_config->get_http_stream_enabled();
    string http_listen = _srs_config->get_http_stream_listen();
    string https_listen = _srs_config->get_https_stream_listen();

#ifdef SRS_RTC
    bool rtc = _srs_config->get_rtc_server_enabled();
    bool rtc_tcp = _srs_config->get_rtc_server_tcp_enabled();
    string rtc_listen = srs_int2str(_srs_config->get_rtc_server_tcp_listen());
    // If enabled and listen is the same value, resue port for WebRTC over TCP.
    if (stream && rtc && rtc_tcp && http_listen == rtc_listen) {
        srs_trace("WebRTC tcp=%s reuses http=%s server", rtc_listen.c_str(), http_listen.c_str());
        reuse_rtc_over_server_ = true;
    }
    if (stream && rtc && rtc_tcp && https_listen == rtc_listen) {
        srs_trace("WebRTC tcp=%s reuses https=%s server", rtc_listen.c_str(), https_listen.c_str());
        reuse_rtc_over_server_ = true;
    }
#endif

    // If enabled and the listen is the same value, reuse port.
    bool api = _srs_config->get_http_api_enabled();
    string api_listen = _srs_config->get_http_api_listen();
    string apis_listen = _srs_config->get_https_api_listen();
    if (stream && api && api_listen == http_listen && apis_listen == https_listen) {
        srs_trace("API reuses http=%s and https=%s server", http_listen.c_str(), https_listen.c_str());
        reuse_api_over_server_ = true;
    }

    // Only init HTTP API when not reusing HTTP server.
    if (!reuse_api_over_server_) {
        SrsHttpServeMux *api = dynamic_cast<SrsHttpServeMux*>(http_api_mux);
        srs_assert(api);

        if ((err = api->initialize()) != srs_success) {
            return srs_error_wrap(err, "http api initialize");
        }
    } else {
        srs_freep(http_api_mux);
        http_api_mux = http_server;
    }

    if ((err = http_server->initialize()) != srs_success) {
        return srs_error_wrap(err, "http server initialize");
    }
    
    return err;
}

srs_error_t SrsServer::initialize_st()
{
    srs_error_t err = srs_success;

    // check asprocess.
    bool asprocess = _srs_config->get_asprocess();
    if (asprocess && ppid == 1) {
        return srs_error_new(ERROR_SYSTEM_ASSERT_FAILED, "ppid=%d illegal for asprocess", ppid);
    }
    
    srs_trace("server main cid=%s, pid=%d, ppid=%d, asprocess=%d",
        _srs_context->get_id().c_str(), ::getpid(), ppid, asprocess);
    
    return err;
}

srs_error_t SrsServer::initialize_signal()
{
    srs_error_t err = srs_success;

    if ((err = signal_manager->initialize()) != srs_success) {
        return srs_error_wrap(err, "init signal manager");
    }

    // Start the version query coroutine.
    if ((err = latest_version_->start()) != srs_success) {
        return srs_error_wrap(err, "start version query");
    }

    return err;
}

srs_error_t SrsServer::listen()
{
    srs_error_t err = srs_success;

    // Create RTMP listeners.
    rtmp_listener_->add(_srs_config->get_listens())->set_label("RTMP");
    if ((err = rtmp_listener_->listen()) != srs_success) {
        return srs_error_wrap(err, "rtmp listen");
    }

    // Create HTTP API listener.
    if (_srs_config->get_http_api_enabled()) {
        if (reuse_api_over_server_) {
            srs_trace("HTTP-API: Reuse listen to http server %s", _srs_config->get_http_stream_listen().c_str());
        } else {
            api_listener_->set_endpoint(_srs_config->get_http_api_listen())->set_label("HTTP-API");
            if ((err = api_listener_->listen()) != srs_success) {
                return srs_error_wrap(err, "http api listen");
            }
        }
    }

    // Create HTTPS API listener.
    if (_srs_config->get_https_api_enabled()) {
        if (reuse_api_over_server_) {
            srs_trace("HTTPS-API: Reuse listen to http server %s", _srs_config->get_http_stream_listen().c_str());
        } else {
            apis_listener_->set_endpoint(_srs_config->get_https_api_listen())->set_label("HTTPS-API");
            if ((err = apis_listener_->listen()) != srs_success) {
                return srs_error_wrap(err, "https api listen");
            }
        }
    }

    // Create HTTP server listener.
    if (_srs_config->get_http_stream_enabled()) {
        http_listener_->set_endpoint(_srs_config->get_http_stream_listen())->set_label("HTTP-Server");
        if ((err = http_listener_->listen()) != srs_success) {
            return srs_error_wrap(err, "http server listen");
        }
    }

    // Create HTTPS server listener.
    if (_srs_config->get_https_stream_enabled()) {
        https_listener_->set_endpoint(_srs_config->get_https_stream_listen())->set_label("HTTPS-Server");
        if ((err = https_listener_->listen()) != srs_success) {
            return srs_error_wrap(err, "https server listen");
        }
    }

    // Start WebRTC over TCP listener.
#ifdef SRS_RTC
    if (!reuse_rtc_over_server_ && _srs_config->get_rtc_server_tcp_enabled()) {
        webrtc_listener_->set_endpoint(srs_int2str(_srs_config->get_rtc_server_tcp_listen()))->set_label("WebRTC");
        if ((err = webrtc_listener_->listen()) != srs_success) {
            return srs_error_wrap(err, "webrtc tcp listen");
        }
    }
#endif

    // Start all listeners for stream caster.
    std::vector<SrsConfDirective*> confs = _srs_config->get_stream_casters();
    for (vector<SrsConfDirective*>::iterator it = confs.begin(); it != confs.end(); ++it) {
        SrsConfDirective* conf = *it;
        if (!_srs_config->get_stream_caster_enabled(conf)) {
            continue;
        }

        ISrsListener* listener = NULL;
        std::string caster = _srs_config->get_stream_caster_engine(conf);
        if (srs_stream_caster_is_udp(caster)) {
            listener = stream_caster_mpegts_;
            if ((err = stream_caster_mpegts_->initialize(conf)) != srs_success) {
                return srs_error_wrap(err, "initialize");
            }
        } else if (srs_stream_caster_is_flv(caster)) {
            listener = stream_caster_flv_listener_;
            if ((err = stream_caster_flv_listener_->initialize(conf)) != srs_success) {
                return srs_error_wrap(err, "initialize");
            }
        } else if (srs_stream_caster_is_gb28181(caster)) {
        #ifdef SRS_GB28181
            listener = stream_caster_gb28181_;
            if ((err = stream_caster_gb28181_->initialize(conf)) != srs_success) {
                return srs_error_wrap(err, "initialize");
            }
        #else
            return srs_error_new(ERROR_STREAM_CASTER_ENGINE, "Please enable GB by: ./configure --gb28181=on");
        #endif
        } else {
            return srs_error_new(ERROR_STREAM_CASTER_ENGINE, "invalid caster %s", caster.c_str());
        }

        srs_assert(listener);
        if ((err = listener->listen()) != srs_success) {
            return srs_error_wrap(err, "listen");
        }
    }

    // Create exporter server listener.
    if (_srs_config->get_exporter_enabled()) {
        exporter_listener_->set_endpoint(_srs_config->get_exporter_listen())->set_label("Exporter-Server");
        if ((err = exporter_listener_->listen()) != srs_success) {
            return srs_error_wrap(err, "exporter server listen");
        }
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

    // Ignore / and /api/v1/versions for already handled by HTTP server.
    if (!reuse_api_over_server_) {
        if ((err = http_api_mux->handle("/", new SrsGoApiRoot())) != srs_success) {
            return srs_error_wrap(err, "handle /");
        }
        if ((err = http_api_mux->handle("/api/v1/versions", new SrsGoApiVersion())) != srs_success) {
            return srs_error_wrap(err, "handle versions");
        }
    }

    if ((err = http_api_mux->handle("/api/", new SrsGoApiApi())) != srs_success) {
        return srs_error_wrap(err, "handle api");
    }
    if ((err = http_api_mux->handle("/api/v1/", new SrsGoApiV1())) != srs_success) {
        return srs_error_wrap(err, "handle v1");
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
        return srs_error_wrap(err, "handle clusters");
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

#ifdef SRS_GPERF
    // The test api for get tcmalloc stats.
    // @see Memory Introspection in https://gperftools.github.io/gperftools/tcmalloc.html
    if ((err = http_api_mux->handle("/api/v1/tcmalloc", new SrsGoApiTcmalloc())) != srs_success) {
        return srs_error_wrap(err, "handle tests errors");
    }
#endif
    // metrics by prometheus
    if ((err = http_api_mux->handle("/metrics", new SrsGoApiMetrics())) != srs_success) {
        return srs_error_wrap(err, "handle tests errors");
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

srs_error_t SrsServer::start(SrsWaitGroup* wg)
{
    srs_error_t err = srs_success;

    if ((err = _srs_sources->initialize()) != srs_success) {
        return srs_error_wrap(err, "sources");
    }

    if ((err = trd_->start()) != srs_success) {
        return srs_error_wrap(err, "start");
    }

    if ((err = setup_ticks()) != srs_success) {
        return srs_error_wrap(err, "tick");
    }

    // OK, we start SRS server.
    wg_ = wg;
    wg->add(1);

    return err;
}

void SrsServer::stop()
{
#ifdef SRS_GPERF_MC
    dispose();

    // remark, for gmc, never invoke the exit().
    srs_warn("sleep a long time for system st-threads to cleanup.");
    srs_usleep(3 * 1000 * 1000);
    srs_warn("system quit");

    // For GCM, cleanup done.
    return;
#endif

    // quit normally.
    srs_warn("main cycle terminated, system quit normally.");

    // fast quit, do some essential cleanup.
    if (signal_fast_quit) {
        dispose(); // TODO: FIXME: Rename to essential_dispose.
        srs_trace("srs disposed");
    }

    // gracefully quit, do carefully cleanup.
    if (signal_gracefully_quit) {
        gracefully_dispose();
        srs_trace("srs gracefully quit");
    }

    srs_trace("srs terminated");

    // for valgrind to detect.
    srs_freep(_srs_config);
    srs_freep(_srs_log);
}

srs_error_t SrsServer::cycle()
{
    srs_error_t err = srs_success;

    // Start the inotify auto reload by watching config file.
    SrsInotifyWorker inotify(this);
    if ((err = inotify.start()) != srs_success) {
        return srs_error_wrap(err, "start inotify");
    }

    // Do server main cycle.
    if ((err = do_cycle()) != srs_success) {
        srs_error("server err %s", srs_error_desc(err).c_str());
    }

    // OK, SRS server is done.
    wg_->done();

    return err;
}

void SrsServer::on_signal(int signo)
{
    // For signal to quit with coredump.
    if (signo == SRS_SIGNAL_ASSERT_ABORT) {
        srs_trace("abort with coredump, signo=%d", signo);
        srs_assert(false);
        return;
    }

    if (signo == SRS_SIGNAL_RELOAD) {
        srs_trace("reload config, signo=%d", signo);
        signal_reload = true;
        return;
    }
    
#ifndef SRS_GPERF_MC
    if (signo == SRS_SIGNAL_REOPEN_LOG) {
        _srs_log->reopen();

        srs_warn("reopen log file, signo=%d", signo);
        return;
    }
#endif
    
#ifdef SRS_GPERF_MC
    if (signo == SRS_SIGNAL_REOPEN_LOG) {
        signal_gmc_stop = true;
        srs_warn("for gmc, the SIGUSR1 used as SIGINT, signo=%d", signo);
        return;
    }
#endif
    
    if (signo == SRS_SIGNAL_PERSISTENCE_CONFIG) {
        signal_persistence_config = true;
        return;
    }
    
    if (signo == SIGINT) {
#ifdef SRS_GPERF_MC
        srs_trace("gmc is on, main cycle will terminate normally, signo=%d", signo);
        signal_gmc_stop = true;
#endif
    }

    // For K8S, force to gracefully quit for gray release or canary.
    // @see https://github.com/ossrs/srs/issues/1595#issuecomment-587473037
    if (signo == SRS_SIGNAL_FAST_QUIT && _srs_config->is_force_grace_quit()) {
        srs_trace("force gracefully quit, signo=%d", signo);
        signo = SRS_SIGNAL_GRACEFULLY_QUIT;
    }

    if ((signo == SIGINT || signo == SRS_SIGNAL_FAST_QUIT) && !signal_fast_quit) {
        srs_trace("sig=%d, user terminate program, fast quit", signo);
        signal_fast_quit = true;
        return;
    }

    if (signo == SRS_SIGNAL_GRACEFULLY_QUIT && !signal_gracefully_quit) {
        srs_trace("sig=%d, user start gracefully quit", signo);
        signal_gracefully_quit = true;
        return;
    }
}

srs_error_t _srs_reload_err;
SrsReloadState _srs_reload_state;
std::string _srs_reload_id;

srs_error_t SrsServer::do_cycle()
{
    srs_error_t err = srs_success;
    
    // for asprocess.
    bool asprocess = _srs_config->get_asprocess();

    while (true) {
        if ((err = trd_->pull()) != srs_success) {
            return srs_error_wrap(err, "pull");
        }
            
        // asprocess check.
        if (asprocess && ::getppid() != ppid) {
            return srs_error_new(ERROR_ASPROCESS_PPID, "asprocess ppid changed from %d to %d", ppid, ::getppid());
        }

        // gracefully quit for SIGINT or SIGTERM or SIGQUIT.
        if (signal_fast_quit || signal_gracefully_quit) {
            srs_trace("cleanup for quit signal fast=%d, grace=%d", signal_fast_quit, signal_gracefully_quit);
            return err;
        }

        // for gperf heap checker,
        // @see: research/gperftools/heap-checker/heap_checker.cc
        // if user interrupt the program, exit to check mem leak.
        // but, if gperf, use reload to ensure main return normally,
        // because directly exit will cause core-dump.
#ifdef SRS_GPERF_MC
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
            srs_trace("starting reload config.");

            SrsReloadState state = SrsReloadStateInit;
            _srs_reload_state = SrsReloadStateInit; srs_freep(_srs_reload_err); _srs_reload_id = srs_random_str(7);
            err = _srs_config->reload(&state);
            _srs_reload_state = state; _srs_reload_err = srs_error_copy(err);
            if (err != srs_success) {
                // If the parsing and transformation of the configuration fail, we can tolerate it by simply
                // ignoring the new configuration and continuing to use the current one. However, if the
                // application of the new configuration fails, some configurations may be applied while
                // others may not. For instance, the listening port may be closed when the configuration
                // is set to listen on an unavailable port. In such cases, we should terminate the service.
                if (state == SrsReloadStateApplying) {
                    return srs_error_wrap(err, "reload fatal error state=%d", state);
                }

                srs_warn("reload failed, state=%d, err %s", state, srs_error_desc(err).c_str());
                srs_freep(err);
            } else {
                srs_trace("reload config success, state=%d.", state);
            }
        }

        srs_usleep(1 * SRS_UTIME_SECONDS);
    }
    
    return err;
}

srs_error_t SrsServer::setup_ticks()
{
    srs_error_t err = srs_success;

    srs_freep(timer_);
    timer_ = new SrsHourGlass("srs", this, 1 * SRS_UTIME_SECONDS);

    if (_srs_config->get_stats_enabled()) {
        if ((err = timer_->tick(2, 3 * SRS_UTIME_SECONDS)) != srs_success) {
            return srs_error_wrap(err, "tick");
        }
        if ((err = timer_->tick(4, 6 * SRS_UTIME_SECONDS)) != srs_success) {
            return srs_error_wrap(err, "tick");
        }
        if ((err = timer_->tick(5, 6 * SRS_UTIME_SECONDS)) != srs_success) {
            return srs_error_wrap(err, "tick");
        }
        if ((err = timer_->tick(6, 9 * SRS_UTIME_SECONDS)) != srs_success) {
            return srs_error_wrap(err, "tick");
        }
        if ((err = timer_->tick(7, 9 * SRS_UTIME_SECONDS)) != srs_success) {
            return srs_error_wrap(err, "tick");
        }

        if ((err = timer_->tick(8, 3 * SRS_UTIME_SECONDS)) != srs_success) {
            return srs_error_wrap(err, "tick");
        }

        if ((err = timer_->tick(10, 9 * SRS_UTIME_SECONDS)) != srs_success) {
            return srs_error_wrap(err, "tick");
        }
    }

    if (_srs_config->get_heartbeat_enabled()) {
        if ((err = timer_->tick(9, _srs_config->get_heartbeat_interval())) != srs_success) {
            return srs_error_wrap(err, "tick");
        }
    }

    if ((err = timer_->start()) != srs_success) {
        return srs_error_wrap(err, "timer");
    }

    return err;
}

srs_error_t SrsServer::notify(int event, srs_utime_t interval, srs_utime_t tick)
{
    srs_error_t err = srs_success;

    switch (event) {
        case 2: srs_update_system_rusage(); break;
        case 4: srs_update_disk_stat(); break;
        case 5: srs_update_meminfo(); break;
        case 6: srs_update_platform_info(); break;
        case 7: srs_update_network_devices(); break;
        case 8: resample_kbps(); break;
        case 9: http_heartbeat->heartbeat(); break;
        case 10: srs_update_udp_snmp_statistic(); break;
    }

    return err;
}

void SrsServer::resample_kbps()
{
    SrsStatistic* stat = SrsStatistic::instance();
    
    // collect delta from all clients.
    for (int i = 0; i < (int)conn_manager->size(); i++) {
        ISrsResource* c = conn_manager->at(i);

        SrsRtmpConn* rtmp = dynamic_cast<SrsRtmpConn*>(c);
        if (rtmp) {
            stat->kbps_add_delta(c->get_id().c_str(), rtmp->delta());
            continue;
        }

        SrsHttpxConn* httpx = dynamic_cast<SrsHttpxConn*>(c);
        if (httpx) {
            stat->kbps_add_delta(c->get_id().c_str(), httpx->delta());
            continue;
        }

#ifdef SRS_RTC
        SrsRtcTcpConn* tcp = dynamic_cast<SrsRtcTcpConn*>(c);
        if (tcp) {
            stat->kbps_add_delta(c->get_id().c_str(), tcp->delta());
            continue;
        }
#endif

        // Impossible path, because we only create these connections above.
        srs_assert(false);
    }
    
    // Update the global server level statistics.
    stat->kbps_sample();
}

ISrsHttpServeMux* SrsServer::api_server()
{
    return http_api_mux;
}

srs_error_t SrsServer::on_tcp_client(ISrsListener* listener, srs_netfd_t stfd)
{
    srs_error_t err = do_on_tcp_client(listener, stfd);

    // We always try to close the stfd, because it should be NULL if it has been handled or closed.
    srs_close_stfd(stfd);

    return err;
}

srs_error_t SrsServer::do_on_tcp_client(ISrsListener* listener, srs_netfd_t& stfd)
{
    srs_error_t err = srs_success;

    int fd = srs_netfd_fileno(stfd);
    string ip = srs_get_peer_ip(fd);
    int port = srs_get_peer_port(fd);

    // Ignore if ip is empty, for example, load balancer keepalive.
    if (ip.empty()) {
        if (_srs_config->empty_ip_ok()) return err;
        return srs_error_new(ERROR_SOCKET_GET_PEER_IP, "ignore empty ip, fd=%d", fd);
    }

    // Security or system flow control check.
    if ((err = on_before_connection(stfd, ip, port)) != srs_success) {
        return srs_error_wrap(err, "check");
    }

    // Covert handler to resource.
    ISrsResource* resource = NULL;

    // The context id may change during creating the bellow objects.
    SrsContextRestore(_srs_context->get_id());

    // From now on, we always handle the stfd, so we set the original one to NULL.
    srs_netfd_t stfd2 = stfd;
    stfd = NULL;

#ifdef SRS_RTC
    // If reuse HTTP server with WebRTC TCP, peek to detect the client.
    if (reuse_rtc_over_server_ && (listener == http_listener_ || listener == https_listener_)) {
        SrsTcpConnection* skt = new SrsTcpConnection(stfd2);
        SrsBufferedReadWriter* io = new SrsBufferedReadWriter(skt);

        // Peek first N bytes to finger out the real client type.
        uint8_t b[10]; int nn = sizeof(b);
        if ((err = io->peek((char*)b, &nn)) != srs_success) {
            srs_freep(io); srs_freep(skt);
            return srs_error_wrap(err, "peek");
        }

        // If first message is BindingRequest(00 01), prefixed with length(2B), it's WebRTC client. Generally, the frame
        // length minus message length should be 20, that is the header size of STUN is 20 bytes. For example:
        //      00 6c # Frame length: 0x006c = 108
        //      00 01 # Message Type: Binding Request(0x0001)
        //      00 58 # Message Length: 0x005 = 88
        //      21 12 a4 42 # Message Cookie: 0x2112a442
        //      48 32 6c 61 6b 42 35 71 42 35 4a 71 # Message Transaction ID: 12 bytes
        if (nn == 10 && b[0] == 0 && b[2] == 0 && b[3] == 1 && b[1] - b[5] == 20
            && b[6] == 0x21 && b[7] == 0x12 && b[8] == 0xa4 && b[9] == 0x42
        ) {
            // TODO: FIXME: Should manage this connection by _srs_rtc_manager
            resource = new SrsRtcTcpConn(io, ip, port, this);
        } else {
            resource = new SrsHttpxConn(listener == http_listener_, this, io, http_server, ip, port);
        }
    }
#endif

    // Create resource by normal listeners.
    if (!resource) {
        if (listener == rtmp_listener_) {
            resource = new SrsRtmpConn(this, stfd2, ip, port);
        } else if (listener == api_listener_ || listener == apis_listener_) {
            bool is_https = listener == apis_listener_;
            resource = new SrsHttpxConn(is_https, this, new SrsTcpConnection(stfd2), http_api_mux, ip, port);
        } else if (listener == http_listener_ || listener == https_listener_) {
            bool is_https = listener == https_listener_;
            resource = new SrsHttpxConn(is_https, this, new SrsTcpConnection(stfd2), http_server, ip, port);
#ifdef SRS_RTC
        } else if (listener == webrtc_listener_) {
            // TODO: FIXME: Should manage this connection by _srs_rtc_manager
            resource = new SrsRtcTcpConn(new SrsTcpConnection(stfd2), ip, port, this);
#endif
        } else if (listener == exporter_listener_) {
            // TODO: FIXME: Maybe should support https metrics.
            bool is_https = false;
            resource = new SrsHttpxConn(is_https, this, new SrsTcpConnection(stfd2), http_api_mux, ip, port);
        } else {
            srs_close_stfd(stfd2);
            srs_warn("Close for invalid fd=%d, ip=%s:%d", fd, ip.c_str(), port);
            return err;
        }
    }

    // Use connection manager to manage all the resources.
    conn_manager->add(resource);

    // If connection is a resource to start, start a coroutine to handle it.
    ISrsStartable* conn = dynamic_cast<ISrsStartable*>(resource);
    if ((err = conn->start()) != srs_success) {
        return srs_error_wrap(err, "start conn coroutine");
    }

    return err;
}

srs_error_t SrsServer::on_before_connection(srs_netfd_t& stfd, const std::string& ip, int port)
{
    srs_error_t err = srs_success;

    int fd = srs_netfd_fileno(stfd);

    // Failed if exceed the connection limitation.
    int max_connections = _srs_config->get_max_connections();

    if ((int)conn_manager->size() >= max_connections) {
        return srs_error_new(ERROR_EXCEED_CONNECTIONS, "drop fd=%d, ip=%s:%d, max=%d, cur=%d for exceed connection limits",
            fd, ip.c_str(), port, max_connections, (int)conn_manager->size());
    }

    // Set to close the fd when forking, to avoid fd leak when start a process.
    // See https://github.com/ossrs/srs/issues/518
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

    return err;
}

void SrsServer::remove(ISrsResource* c)
{
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

srs_error_t SrsServer::on_publish(SrsLiveSource* s, SrsRequest* r)
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

void SrsServer::on_unpublish(SrsLiveSource* s, SrsRequest* r)
{
    http_server->http_unmount(s, r);
    
    SrsCoWorkers* coworkers = SrsCoWorkers::instance();
    coworkers->on_unpublish(s, r);
}

SrsServerAdapter::SrsServerAdapter()
{
    srs = new SrsServer();
}

SrsServerAdapter::~SrsServerAdapter()
{
    srs_freep(srs);
}

srs_error_t SrsServerAdapter::initialize()
{
    srs_error_t err = srs_success;
    return err;
}

srs_error_t SrsServerAdapter::run(SrsWaitGroup* wg)
{
    srs_error_t err = srs_success;

    // Initialize the whole system, set hooks to handle server level events.
    if ((err = srs->initialize()) != srs_success) {
        return srs_error_wrap(err, "server initialize");
    }

    if ((err = srs->initialize_st()) != srs_success) {
        return srs_error_wrap(err, "initialize st");
    }

    if ((err = srs->initialize_signal()) != srs_success) {
        return srs_error_wrap(err, "initialize signal");
    }

    if ((err = srs->listen()) != srs_success) {
        return srs_error_wrap(err, "listen");
    }

    if ((err = srs->register_signal()) != srs_success) {
        return srs_error_wrap(err, "register signal");
    }

    if ((err = srs->http_handle()) != srs_success) {
        return srs_error_wrap(err, "http handle");
    }

    if ((err = srs->ingest()) != srs_success) {
        return srs_error_wrap(err, "ingest");
    }

    if ((err = srs->start(wg)) != srs_success) {
        return srs_error_wrap(err, "start");
    }

#ifdef SRS_GB28181
    if ((err = _srs_gb_manager->start()) != srs_success) {
        return srs_error_wrap(err, "start manager");
    }
#endif

    SrsLazySweepGc* gc = dynamic_cast<SrsLazySweepGc*>(_srs_gc);
    if ((err = gc->start()) != srs_success) {
        return srs_error_wrap(err, "start gc");
    }

    return err;
}

void SrsServerAdapter::stop()
{
}

SrsServer* SrsServerAdapter::instance()
{
    return srs;
}

