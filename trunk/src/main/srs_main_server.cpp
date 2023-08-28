//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#include <srs_core.hpp>

#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <sstream>
using namespace std;

#ifdef SRS_GPERF_MP
#include <gperftools/heap-profiler.h>
#endif
#ifdef SRS_GPERF_CP
#include <gperftools/profiler.h>
#endif

#ifdef SRS_GPERF
#include <gperftools/malloc_extension.h>
#endif

#ifdef SRS_SANITIZER_LOG
#include <sanitizer/asan_interface.h>
#endif

#include <unistd.h>
using namespace std;

#include <srs_kernel_error.hpp>
#include <srs_app_server.hpp>
#include <srs_app_config.hpp>
#include <srs_app_log.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_core_performance.hpp>
#include <srs_app_utility.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_file.hpp>
#include <srs_app_hybrid.hpp>
#include <srs_app_threads.hpp>
#include <srs_kernel_error.hpp>

#ifdef SRS_RTC
#include <srs_app_rtc_conn.hpp>
#include <srs_app_rtc_server.hpp>
#endif

#ifdef SRS_SRT
#include <srs_protocol_srt.hpp>
#include <srs_app_srt_server.hpp>
#endif

// pre-declare
srs_error_t run_directly_or_daemon();
srs_error_t run_in_thread_pool();
srs_error_t srs_detect_docker();
void show_macro_features();

// @global log and context.
ISrsLog* _srs_log = NULL;
// It SHOULD be thread-safe, because it use thread-local thread private data.
ISrsContext* _srs_context = NULL;
// @global config object for app module.
SrsConfig* _srs_config = NULL;

// @global version of srs, which can grep keyword "XCORE"
extern const char* _srs_version;

// @global main SRS server, for debugging
SrsServer* _srs_server = NULL;

// Whether setup config by environment variables, see https://github.com/ossrs/srs/issues/2277
bool _srs_config_by_env = false;

// The binary name of SRS.
const char* _srs_binary = NULL;

// Free global data, for address sanitizer.
extern void srs_free_global_system_ips();

#ifdef SRS_SANITIZER_LOG
extern void asan_report_callback(const char* str);
#endif

/**
 * main entrance.
 */
srs_error_t do_main(int argc, char** argv, char** envp)
{
    srs_error_t err = srs_success;

    // TODO: Might fail if change working directory.
    _srs_binary = argv[0];

    // For sanitizer on macOS, to avoid the warning on startup.
#if defined(SRS_OSX) && defined(SRS_SANITIZER)
    if (!getenv("MallocNanoZone")) {
        fprintf(stderr, "Asan: Please setup the env MallocNanoZone=0 to disable the warning, see https://stackoverflow.com/a/70209891/17679565\n");
    }
#endif

    // Initialize global and thread-local variables.
    if ((err = srs_global_initialize()) != srs_success) {
        return srs_error_wrap(err, "global init");
    }

    if ((err = SrsThreadPool::setup_thread_locals()) != srs_success) {
        return srs_error_wrap(err, "thread init");
    }

    // For background context id.
    _srs_context->set_id(_srs_context->generate_id());

    // TODO: support both little and big endian.
    srs_assert(srs_is_little_endian());
    
    // for gperf gmp or gcp,
    // should never enable it when not enabled for performance issue.
#ifdef SRS_GPERF_MP
    HeapProfilerStart("gperf.srs.gmp");
#endif
#ifdef SRS_GPERF_CP
    ProfilerStart("gperf.srs.gcp");
#endif
    
    // never use gmp to check memory leak.
#ifdef SRS_GPERF_MP
#warning "gmp is not used for memory leak, please use gmc instead."
#endif

    // Ignore any error while detecting docker.
    if ((err = srs_detect_docker()) != srs_success) {
        srs_error_reset(err);
    }
    
    // never use srs log(srs_trace, srs_error, etc) before config parse the option,
    // which will load the log config and apply it.
    if ((err = _srs_config->parse_options(argc, argv)) != srs_success) {
        return srs_error_wrap(err, "config parse options");
    }
    
    // change the work dir and set cwd.
    int r0 = 0;
    string cwd = _srs_config->get_work_dir();
    if (!cwd.empty() && cwd != "./" && (r0 = chdir(cwd.c_str())) == -1) {
        return srs_error_new(-1, "chdir to %s, r0=%d", cwd.c_str(), r0);
    }
    if ((err = _srs_config->initialize_cwd()) != srs_success) {
        return srs_error_wrap(err, "config cwd");
    }
    
    // config parsed, initialize log.
    if ((err = _srs_log->initialize()) != srs_success) {
        return srs_error_wrap(err, "log initialize");
    }

    // Detect whether set SRS config by envrionment variables.
    for (char** pp = envp; *pp; pp++) {
        char* p = *pp;
        if (p[0] == 'S' && p[1] == 'R' && p[2] == 'S' && p[3] == '_') {
            _srs_config_by_env = true;
            break;
        }
    }
    
    // config already applied to log.
    srs_trace("%s, %s", RTMP_SIG_SRS_SERVER, RTMP_SIG_SRS_LICENSE);
    srs_trace("authors: %sand %s", RTMP_SIG_SRS_AUTHORS, SRS_CONSTRIBUTORS);
    srs_trace("cwd=%s, work_dir=%s, build: %s, configure: %s, uname: %s, osx: %d, env: %d, pkg: %s",
        _srs_config->cwd().c_str(), cwd.c_str(), SRS_BUILD_DATE, SRS_USER_CONFIGURE, SRS_UNAME, SRS_OSX_BOOL,
        _srs_config_by_env, SRS_PACKAGER);
    srs_trace("configure detail: " SRS_CONFIGURE);
#ifdef SRS_EMBEDED_TOOL_CHAIN
    srs_trace("crossbuild tool chain: " SRS_EMBEDED_TOOL_CHAIN);
#endif

    // for memory check or detect.
    if (true) {
        stringstream ss;
        
#ifdef SRS_PERF_GLIBC_MEMORY_CHECK
        // ensure glibc write error to stderr.
        string lfsov = srs_getenv("LIBC_FATAL_STDERR_");
        setenv("LIBC_FATAL_STDERR_", "1", 1);
        string lfsnv = srs_getenv("LIBC_FATAL_STDERR_");
        //
        // ensure glibc to do alloc check.
        string mcov = srs_getenv("MALLOC_CHECK_");
        setenv("MALLOC_CHECK_", "1", 1);
        string mcnv = srs_getenv("MALLOC_CHECK_");
        ss << "glic mem-check env MALLOC_CHECK_ " << mcov << "=>" << mcnv << ", LIBC_FATAL_STDERR_ " << lfsov << "=>" << lfsnv << ".";
#endif
        
#ifdef SRS_GPERF_MC
        string hcov = srs_getenv("HEAPCHECK");
        if (hcov.empty()) {
            string cpath = _srs_config->config();
            srs_warn("gmc HEAPCHECK is required, for example: env HEAPCHECK=normal ./objs/srs -c %s", cpath.c_str());
        } else {
            ss << "gmc env HEAPCHECK=" << hcov << ".";
        }
#endif
        
#ifdef SRS_GPERF_MD
        char* TCMALLOC_PAGE_FENCE = getenv("TCMALLOC_PAGE_FENCE");
        if (!TCMALLOC_PAGE_FENCE || strcmp(TCMALLOC_PAGE_FENCE, "1")) {
            srs_warn("gmd enabled without env TCMALLOC_PAGE_FENCE=1");
        } else {
            ss << "gmd env TCMALLOC_PAGE_FENCE=" << TCMALLOC_PAGE_FENCE << ".";
        }
#endif
        
        string sss = ss.str();
        if (!sss.empty()) {
            srs_trace(sss.c_str());
        }
    }
    
    // we check the config when the log initialized.
    if ((err = _srs_config->check_config()) != srs_success) {
        return srs_error_wrap(err, "check config");
    }
    
    // features
    show_macro_features();

#ifdef SRS_GPERF
    // For tcmalloc, use slower release rate.
    if (true) {
        double trr = _srs_config->tcmalloc_release_rate();
        double otrr = MallocExtension::instance()->GetMemoryReleaseRate();
        MallocExtension::instance()->SetMemoryReleaseRate(trr);
        srs_trace("tcmalloc: set release-rate %.2f=>%.2f", otrr, trr);
    }
#endif

#ifdef SRS_SANITIZER_LOG
    __asan_set_error_report_callback(asan_report_callback);
#endif

    err = run_directly_or_daemon();
    srs_free_global_system_ips();
    if (err != srs_success) {
        return srs_error_wrap(err, "run");
    }

    return err;
}

int main(int argc, char** argv, char** envp)
{
    srs_error_t err = do_main(argc, argv, envp);

    if (err != srs_success) {
        srs_error("Failed, %s", srs_error_desc(err).c_str());
    }
    
    int ret = srs_error_code(err);
    srs_freep(err);
    return ret;
}

/**
 * show the features by macro, the actual macro values.
 */
void show_macro_features()
{
    if (true) {
        stringstream ss;
        
        ss << "features";
        
        // rch(rtmp complex handshake)
        ss << ", rch:" << srs_bool2switch(true);
        ss << ", dash:" << "on";
        ss << ", hls:" << srs_bool2switch(true);
        ss << ", hds:" << srs_bool2switch(SRS_HDS_BOOL);
        ss << ", srt:" << srs_bool2switch(SRS_SRT_BOOL);
        // hc(http callback)
        ss << ", hc:" << srs_bool2switch(true);
        // ha(http api)
        ss << ", ha:" << srs_bool2switch(true);
        // hs(http server)
        ss << ", hs:" << srs_bool2switch(true);
        // hp(http parser)
        ss << ", hp:" << srs_bool2switch(true);
        ss << ", dvr:" << srs_bool2switch(true);
        // trans(transcode)
        ss << ", trans:" << srs_bool2switch(true);
        // inge(ingest)
        ss << ", inge:" << srs_bool2switch(true);
        ss << ", stat:" << srs_bool2switch(true);
        // sc(stream-caster)
        ss << ", sc:" << srs_bool2switch(true);
        srs_trace("%s", ss.str().c_str());
    }
    
    if (true) {
        stringstream ss;
        ss << "SRS on";
#if defined(__amd64__)
        ss << " amd64";
#endif
#if defined(__x86_64__)
        ss << " x86_64";
#endif
#if defined(__i386__)
        ss << " i386";
#endif
#if defined(__arm__)
        ss << " arm";
#endif
#if defined(__aarch64__)
        ss << " aarch64";
#endif
#if defined(SRS_CROSSBUILD)
        ss << "(crossbuild)";
#endif
        
        ss << ", conf:" << _srs_config->config() << ", limit:" << _srs_config->get_max_connections()
        << ", writev:" << sysconf(_SC_IOV_MAX) << ", encoding:" << (srs_is_little_endian()? "little-endian":"big-endian")
        << ", HZ:" << (int)sysconf(_SC_CLK_TCK);
        
        srs_trace("%s", ss.str().c_str());
    }
    
    if (true) {
        stringstream ss;
        
        // mw(merged-write)
        ss << "mw sleep:" << srsu2msi(SRS_PERF_MW_SLEEP) << "ms";
        
        // mr(merged-read)
        ss << ". mr ";
#ifdef SRS_PERF_MERGED_READ
        ss << "enabled:on";
#else
        ss << "enabled:off";
#endif
        ss << ", default:" << SRS_PERF_MR_ENABLED << ", sleep:" << srsu2msi(SRS_PERF_MR_SLEEP) << "ms";
        
        srs_trace("%s", ss.str().c_str());
    }
    
    if (true) {
        stringstream ss;
        
        // gc(gop-cache)
        ss << "gc:" << srs_bool2switch(SRS_PERF_GOP_CACHE);
        // pq(play-queue)
        ss << ", pq:" << srsu2msi(SRS_PERF_PLAY_QUEUE) << "ms";
        // cscc(chunk stream cache cid)
        ss << ", cscc:[0," << SRS_PERF_CHUNK_STREAM_CACHE << ")";
        // csa(complex send algorithm)
        ss << ", csa:";
#ifndef SRS_PERF_COMPLEX_SEND
        ss << "off";
#else
        ss << "on";
#endif
        
        // tn(TCP_NODELAY)
        ss << ", tn:";
#ifdef SRS_PERF_TCP_NODELAY
        ss << "on(may hurts performance)";
#else
        ss << "off";
#endif
        
        // ss(SO_SENDBUF)
        ss << ", ss:";
#ifdef SRS_PERF_SO_SNDBUF_SIZE
        ss << SRS_PERF_SO_SNDBUF_SIZE;
#else
        ss << "auto(guess by merged write)";
#endif
        
        srs_trace("%s", ss.str().c_str());
    }
    
    // others
    int possible_mr_latency = 0;
#ifdef SRS_PERF_MERGED_READ
    possible_mr_latency = srsu2msi(SRS_PERF_MR_SLEEP);
#endif
    srs_trace("system default latency(ms): mw(0-%d) + mr(0-%d) + play-queue(0-%d)",
              srsu2msi(SRS_PERF_MW_SLEEP), possible_mr_latency, srsu2msi(SRS_PERF_PLAY_QUEUE));
    
#if VERSION_MAJOR > VERSION_STABLE
    #warning "Current branch is not stable."
    srs_warn("%s/%s is not stable", RTMP_SIG_SRS_KEY, RTMP_SIG_SRS_VERSION);
#endif
    
#if defined(SRS_PERF_SO_SNDBUF_SIZE) && !defined(SRS_PERF_MW_SO_SNDBUF)
#error "SRS_PERF_SO_SNDBUF_SIZE depends on SRS_PERF_MW_SO_SNDBUF"
#endif
}

// Detect docker by https://stackoverflow.com/a/41559867
bool _srs_in_docker = false;
srs_error_t srs_detect_docker()
{
    srs_error_t err = srs_success;

    _srs_in_docker = false;

    SrsFileReader fr;
    if ((err = fr.open("/proc/1/cgroup")) != srs_success) {
        return err;
    }

    ssize_t nn;
    char buf[1024];
    if ((err = fr.read(buf, sizeof(buf), &nn)) != srs_success) {
        return err;
    }

    if (nn <= 0) {
        return err;
    }

    string s(buf, nn);
    if (srs_string_contains(s, "/docker")) {
        _srs_in_docker = true;
    }

    return err;
}

srs_error_t run_directly_or_daemon()
{
    srs_error_t err = srs_success;

    // Try to load the config if docker detect failed.
    if (!_srs_in_docker) {
        _srs_in_docker = _srs_config->get_in_docker();
        if (_srs_in_docker) {
            srs_trace("enable in_docker by config");
        }
    }

    // Load daemon from config, disable it for docker.
    // @see https://github.com/ossrs/srs/issues/1594
    bool run_as_daemon = _srs_config->get_daemon();
    if (run_as_daemon && _srs_in_docker && _srs_config->disable_daemon_for_docker()) {
        srs_warn("disable daemon for docker");
        run_as_daemon = false;
    }
    
    // If not daemon, directly run hybrid server.
    if (!run_as_daemon) {
        if ((err = run_in_thread_pool()) != srs_success) {
            return srs_error_wrap(err, "run thread pool");
        }
        return srs_success;
    }
    
    srs_trace("start daemon mode...");
    
    int pid = fork();
    
    if(pid < 0) {
        return srs_error_new(-1, "fork father process");
    }
    
    // grandpa
    if(pid > 0) {
        int status = 0;
        waitpid(pid, &status, 0);
        srs_trace("grandpa process exit.");
        srs_free_global_system_ips();
        exit(0);
    }
    
    // father
    pid = fork();
    
    if(pid < 0) {
        return srs_error_new(-1, "fork child process");
    }
    
    if(pid > 0) {
        srs_trace("father process exit");
        srs_free_global_system_ips();
        exit(0);
    }
    
    // son
    srs_trace("son(daemon) process running.");
    
    if ((err = run_in_thread_pool()) != srs_success) {
        return srs_error_wrap(err, "daemon run thread pool");
    }
    
    return err;
}

srs_error_t run_hybrid_server(void* arg);
srs_error_t run_in_thread_pool()
{
#ifdef SRS_SINGLE_THREAD
    srs_trace("Run in single thread mode");
    return run_hybrid_server(NULL);
#else
    srs_error_t err = srs_success;

    // Initialize the thread pool.
    if ((err = _srs_thread_pool->initialize()) != srs_success) {
        return srs_error_wrap(err, "init thread pool");
    }

    // Start the hybrid service worker thread, for RTMP and RTC server, etc.
    if ((err = _srs_thread_pool->execute("hybrid", run_hybrid_server, (void*)NULL)) != srs_success) {
        return srs_error_wrap(err, "start hybrid server thread");
    }

    srs_trace("Pool: Start threads primordial=1, hybrids=1 ok");

    return _srs_thread_pool->run();
#endif
}

#include <srs_app_tencentcloud.hpp>
srs_error_t run_hybrid_server(void* /*arg*/)
{
    srs_error_t err = srs_success;

    // Create servers and register them.
    _srs_hybrid->register_server(new SrsServerAdapter());

#ifdef SRS_SRT
    _srs_hybrid->register_server(new SrsSrtServerAdapter());
#endif

#ifdef SRS_RTC
    _srs_hybrid->register_server(new RtcServerAdapter());
#endif

    // Do some system initialize.
    if ((err = _srs_hybrid->initialize()) != srs_success) {
        return srs_error_wrap(err, "hybrid initialize");
    }

    // Circuit breaker to protect server, which depends on hybrid.
    if ((err = _srs_circuit_breaker->initialize()) != srs_success) {
        return srs_error_wrap(err, "init circuit breaker");
    }

#ifdef SRS_APM
    // When startup, create a span for server information.
    ISrsApmSpan* span = _srs_apm->span("main")->set_kind(SrsApmKindServer);
    srs_freep(span);
#endif

    // Should run util hybrid servers all done.
    if ((err = _srs_hybrid->run()) != srs_success) {
        return srs_error_wrap(err, "hybrid run");
    }

    // After all done, stop and cleanup.
    _srs_hybrid->stop();

    return err;
}

