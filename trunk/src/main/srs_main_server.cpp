//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_core.hpp>

// LCOV_EXCL_START
#include <stdlib.h>
#include <sys/types.h>

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

using namespace std;

#include <srs_app_circuit_breaker.hpp>
#include <srs_app_config.hpp>
#include <srs_app_factory.hpp>
#include <srs_app_log.hpp>
#include <srs_app_rtc_codec.hpp>
#include <srs_app_rtc_conn.hpp>
#include <srs_app_server.hpp>
#include <srs_app_srt_server.hpp>
#include <srs_app_utility.hpp>
#include <srs_core_autofree.hpp>
#include <srs_core_performance.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_file.hpp>
#include <srs_kernel_kbps.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_srt.hpp>

srs_error_t run_directly_or_daemon();
srs_error_t run_srs_server();
void show_macro_features();

ISrsLog *_srs_log = NULL;
// Each ST thread keeps its own context id, in ST thread-specific data.
ISrsContext *_srs_context = NULL;
SrsConfig *_srs_config = NULL;

ISrsAppFactory *_srs_app_factory = new SrsAppFactory();
ISrsKernelFactory *_srs_kernel_factory = _srs_app_factory;

#ifdef SRS_FFMPEG_FIT
// Its constructor registers the FFmpeg log callback.
SrsFFmpegLogHelper _srs_ffmpeg_log_helper;
#endif

// Starts with the keyword "XCORE", to grep the version from the binary.
extern const char *_srs_version;

// See https://github.com/ossrs/srs/issues/2277
bool _srs_config_by_env = false;

const char *_srs_binary = NULL;

bool _srs_in_docker = false;

#ifdef SRS_SANITIZER_LOG
extern void asan_report_callback(const char *str);
#endif

srs_error_t do_main(int argc, char **argv, char **envp)
{
    srs_error_t err = srs_success;

    // TODO: Might fail if change working directory.
    _srs_binary = argv[0];

#if defined(SRS_OSX) && defined(SRS_SANITIZER)
    if (!getenv("MallocNanoZone")) {
        fprintf(stderr, "Asan: Please setup the env MallocNanoZone=0 to disable the warning, see https://stackoverflow.com/a/70209891/17679565\n");
    }
#endif

    // Create these before any other global object.
    _srs_log = new SrsFileLog();
    _srs_context = new SrsThreadContext();
    _srs_config = new SrsConfig();

    // Before the global init, this sets the default context id.
    _srs_context->set_id(_srs_context->generate_id());

    // TODO: support both little and big endian.
    srs_assert(srs_is_little_endian());

    // The gperf gmp and gcp profilers hurt performance, so build them in only when needed.
#ifdef SRS_GPERF_MP
    HeapProfilerStart("gperf.srs.gmp");
#endif
#ifdef SRS_GPERF_CP
    ProfilerStart("gperf.srs.gcp");
#endif

#ifdef SRS_GPERF_MP
#warning "gmp is not used for memory leak, please use gmc instead."
#endif

    if (srs_socket_startup() != 0) {
        return srs_error_new(ERROR_SOCKET_CREATE, "socket startup");
    }

    // Do not use the SRS log, such as srs_trace or srs_error, before the options are parsed,
    // which load the log config.
    if ((err = _srs_config->parse_options(argc, argv)) != srs_success) {
        return srs_error_wrap(err, "config parse options");
    }

    int r0 = 0;
    string cwd = _srs_config->get_work_dir();
    if (!cwd.empty() && cwd != "./" && (r0 = chdir(cwd.c_str())) == -1) {
        return srs_error_new(-1, "chdir to %s, r0=%d", cwd.c_str(), r0);
    }
    if ((err = _srs_config->initialize_cwd()) != srs_success) {
        return srs_error_wrap(err, "config cwd");
    }

    if ((err = _srs_log->initialize()) != srs_success) {
        return srs_error_wrap(err, "log initialize");
    }

    for (char **pp = envp; *pp; pp++) {
        char *p = *pp;
        if (p[0] == 'S' && p[1] == 'R' && p[2] == 'S' && p[3] == '_') {
            _srs_config_by_env = true;
            break;
        }
    }

    srs_trace("%s, %s", RTMP_SIG_SRS_SERVER, RTMP_SIG_SRS_LICENSE);
    srs_trace("authors: %sand %s", RTMP_SIG_SRS_AUTHORS, SRS_CONSTRIBUTORS);
    srs_trace("cwd=%s, work_dir=%s, build: %s, configure: %s, uname: %s, osx: %d, env: %d, pkg: %s",
              _srs_config->cwd().c_str(), cwd.c_str(), SRS_BUILD_DATE, SRS_USER_CONFIGURE, SRS_UNAME, SRS_OSX_BOOL,
              _srs_config_by_env, SRS_PACKAGER);
    srs_trace("configure detail: " SRS_CONFIGURE);
#ifdef SRS_EMBEDED_TOOL_CHAIN
    srs_trace("crossbuild tool chain: " SRS_EMBEDED_TOOL_CHAIN);
#endif

    if (true) {
        stringstream ss;

#ifdef SRS_PERF_GLIBC_MEMORY_CHECK
        // Make glibc write fatal errors to stderr.
        string lfsov = srs_getenv("LIBC_FATAL_STDERR_");
        setenv("LIBC_FATAL_STDERR_", "1", 1);
        string lfsnv = srs_getenv("LIBC_FATAL_STDERR_");

        // Make glibc check allocations.
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
        char *TCMALLOC_PAGE_FENCE = getenv("TCMALLOC_PAGE_FENCE");
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

    if ((err = _srs_config->check_config()) != srs_success) {
        return srs_error_wrap(err, "check config");
    }

    show_macro_features();

#ifdef SRS_GPERF
    // For tcmalloc, use a slower release rate.
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

    if ((err = run_directly_or_daemon()) != srs_success) {
        return srs_error_wrap(err, "run");
    }

    return err;
}

int main(int argc, char **argv, char **envp)
{
#ifdef SRS_SANITIZER
    // The address of a local is not the exact stack top, but close enough for ST.
    void *p = NULL;
    srs_set_primordial_stack(&p);
#endif

    srs_error_t err = do_main(argc, argv, envp);

    if (err != srs_success) {
        srs_error("Failed, %s", srs_error_desc(err).c_str());
    }

    int ret = srs_error_code(err);
    srs_freep(err);
    return ret;
}

void show_macro_features()
{
    if (true) {
        stringstream ss;

        ss << "features";

        // rch(rtmp complex handshake)
        ss << ", rch:" << srs_strconv_format_bool(true);
        ss << ", dash:" << "on";
        ss << ", hls:" << srs_strconv_format_bool(true);
        ss << ", srt:" << srs_strconv_format_bool(true);
        // hc(http callback)
        ss << ", hc:" << srs_strconv_format_bool(true);
        // ha(http api)
        ss << ", ha:" << srs_strconv_format_bool(true);
        // hs(http server)
        ss << ", hs:" << srs_strconv_format_bool(true);
        // hp(http parser)
        ss << ", hp:" << srs_strconv_format_bool(true);
        ss << ", dvr:" << srs_strconv_format_bool(true);
        // trans(transcode)
        ss << ", trans:" << srs_strconv_format_bool(true);
        // inge(ingest)
        ss << ", inge:" << srs_strconv_format_bool(true);
        ss << ", stat:" << srs_strconv_format_bool(true);
        // sc(stream-caster)
        ss << ", sc:" << srs_strconv_format_bool(true);
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
           << ", writev:" << srs_iov_max() << ", encoding:" << (srs_is_little_endian() ? "little-endian" : "big-endian")
           << ", HZ:" << srs_clock_ticks();

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
        ss << "gc:" << srs_strconv_format_bool(SRS_PERF_GOP_CACHE);
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

        // ss(SO_SNDBUF)
        ss << ", ss:";
#ifdef SRS_PERF_SO_SNDBUF_SIZE
        ss << SRS_PERF_SO_SNDBUF_SIZE;
#else
        ss << "auto(guess by merged write)";
#endif

        srs_trace("%s", ss.str().c_str());
    }

    int possible_mr_latency = 0;
#ifdef SRS_PERF_MERGED_READ
    possible_mr_latency = srsu2msi(SRS_PERF_MR_SLEEP);
#endif
    srs_trace("system default latency(ms): mw(0-%d) + mr(0-%d) + play-queue(0-%d)",
              srsu2msi(SRS_PERF_MW_SLEEP), possible_mr_latency, srsu2msi(SRS_PERF_PLAY_QUEUE));

#if VERSION_MAJOR > VERSION_STABLE
#ifdef _MSC_VER
#pragma message("Current branch is not stable.")
#else
#warning "Current branch is not stable."
#endif
    srs_warn("%s/%s is not stable", RTMP_SIG_SRS_KEY, RTMP_SIG_SRS_VERSION);
#endif

#if defined(SRS_PERF_SO_SNDBUF_SIZE) && !defined(SRS_PERF_MW_SO_SNDBUF)
#error "SRS_PERF_SO_SNDBUF_SIZE depends on SRS_PERF_MW_SO_SNDBUF"
#endif
}

srs_error_t run_directly_or_daemon()
{
    srs_error_t err = srs_success;

    // @see https://github.com/ossrs/srs/issues/1594
    bool run_as_daemon = _srs_config->get_daemon();
    if (run_as_daemon && _srs_in_docker && _srs_config->disable_daemon_for_docker()) {
        srs_warn("disable daemon for docker");
        run_as_daemon = false;
    }

    if (!run_as_daemon) {
        if ((err = run_srs_server()) != srs_success) {
            return srs_error_wrap(err, "run server");
        }
        return srs_success;
    }

    srs_trace("start daemon mode...");

    int pid = srs_fork();

    if (pid < 0) {
        return srs_error_new(-1, "fork father process");
    }

    if (pid > 0) {
        int status = 0;
        srs_waitpid(pid, &status, 0);
        srs_trace("grandpa process exit.");
        exit(0);
    }

    pid = srs_fork();

    if (pid < 0) {
        return srs_error_new(-1, "fork child process");
    }

    if (pid > 0) {
        srs_trace("father process exit");
        exit(0);
    }

    srs_trace("son(daemon) process running.");

    if ((err = run_srs_server()) != srs_success) {
        return srs_error_wrap(err, "daemon run server");
    }

    return err;
}

srs_error_t run_srs_server()
{
    srs_error_t err = srs_success;

    if ((err = srs_global_initialize()) != srs_success) {
        return srs_error_wrap(err, "global init");
    }

    // The primordial thread shares the default cid.
    _srs_context->set_id(_srs_context->get_id());

    _srs_server = new SrsServer();
    _srs_server->assemble();

    if ((err = _srs_server->initialize()) != srs_success) {
        return srs_error_wrap(err, "hybrid initialize");
    }

    if ((err = _srs_server->run()) != srs_success) {
        return srs_error_wrap(err, "hybrid run");
    }

    _srs_server->stop();

    return err;
}
// LCOV_EXCL_STOP
