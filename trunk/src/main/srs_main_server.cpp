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

#include <srs_core.hpp>

#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>

#ifdef SRS_AUTO_GPERF_MP
    #include <gperftools/heap-profiler.h>
#endif
#ifdef SRS_AUTO_GPERF_CP
    #include <gperftools/profiler.h>
#endif

#include <srs_kernel_error.hpp>
#include <srs_app_server.hpp>
#include <srs_app_config.hpp>
#include <srs_app_log.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_core_performance.hpp>

// pre-declare
int run();
int run_master();

// for the main objects(server, config, log, context),
// never subscribe handler in constructor,
// instead, subscribe handler in initialize method.
// kernel module.
ISrsLog* _srs_log = new SrsFastLog();
ISrsThreadContext* _srs_context = new SrsThreadContext();
// app module.
SrsConfig* _srs_config = new SrsConfig();
SrsServer* _srs_server = new SrsServer();

/**
* show the features by macro, the actual macro values.
*/
void show_macro_features()
{
#ifdef SRS_AUTO_SSL
    srs_trace("check feature rtmp handshake: on");
#else
    srs_warn("check feature rtmp handshake: off");
#endif

#ifdef SRS_AUTO_HLS
    srs_trace("check feature hls: on");
#else
    srs_warn("check feature hls: off");
#endif

#ifdef SRS_AUTO_HDS
    srs_trace("check feature hds: on");
#else
    srs_warn("check feature hds: off");
#endif

#ifdef SRS_AUTO_HTTP_CALLBACK
    srs_trace("check feature http callback: on");
#else
    srs_warn("check feature http callback: off");
#endif

#ifdef SRS_AUTO_HTTP_API
    srs_trace("check feature http api: on");
#else
    srs_warn("check feature http api: off");
#endif

#ifdef SRS_AUTO_HTTP_SERVER
    srs_trace("check feature http server: on");
#else
    srs_warn("check feature http server: off");
#endif

#ifdef SRS_AUTO_HTTP_PARSER
    srs_trace("check feature http parser: on");
#else
    srs_warn("check feature http parser: off");
#endif

#ifdef SRS_AUTO_DVR
    srs_trace("check feature dvr: on");
#else
    srs_warn("check feature dvr: off");
#endif

#ifdef SRS_AUTO_TRANSCODE
    srs_trace("check feature transcode: on");
#else
    srs_warn("check feature transcode: off");
#endif

#ifdef SRS_AUTO_INGEST
    srs_trace("check feature ingest: on");
#else
    srs_warn("check feature ingest: off");
#endif

#ifdef SRS_AUTO_STAT
    srs_trace("check feature system stat: on");
#else
    srs_warn("check feature system stat: off");
#endif

#ifdef SRS_AUTO_NGINX
    srs_trace("check feature compile nginx: on");
#else
    srs_warn("check feature compile nginx: off");
#endif

#ifdef SRS_AUTO_FFMPEG_TOOL
    srs_trace("check feature compile ffmpeg: on");
#else
    srs_warn("check feature compile ffmpeg: off");
#endif

#ifdef SRS_AUTO_STREAM_CASTER
    srs_trace("stream caster: on");
#else
    srs_warn("stream caster: off");
#endif

#ifdef SRS_PERF_MERGED_READ
    srs_trace("MR(merged-read): on, @see %s", RTMP_SIG_SRS_ISSUES(241));
#else
    srs_warn("MR(merged-read): off, @see %s", RTMP_SIG_SRS_ISSUES(241));
#endif

    srs_trace("MR(merged-read) default %d sleep %d", SRS_PERF_MR_ENABLED, SRS_PERF_MR_SLEEP);
    srs_trace("MW(merged-write) default sleep %d", SRS_PERF_MW_SLEEP);
    srs_trace("read chunk stream cache cid [0, %d)", SRS_PERF_CHUNK_STREAM_CACHE);
    srs_trace("default gop cache %d, play queue %ds", SRS_PERF_GOP_CACHE, SRS_PERF_PLAY_QUEUE);
    
#ifndef SRS_PERF_COMPLEX_SEND
    srs_warn("complex send algorithm disabled.");
#else
    srs_trace("complex send algorithm enabled.");
#endif

#ifdef SRS_PERF_TCP_NODELAY
    srs_warn("TCP_NODELAY enabled, hurts performance.");
#else
    srs_trace("TCP_NODELAY disabled.");
#endif

#ifdef SRS_PERF_SO_SNDBUF_SIZE
    srs_warn("socket send buffer size %d", SRS_PERF_SO_SNDBUF_SIZE);
#else
    srs_trace("auto guess socket send buffer by merged write");
#endif

    int possible_mr_latency = 0;
#ifdef SRS_PERF_MERGED_READ
    possible_mr_latency = SRS_PERF_MR_SLEEP;
#endif
    srs_trace("system default latency in ms: mw(0-%d) + mr(0-%d) + play-queue(0-%d)",
        SRS_PERF_MW_SLEEP, possible_mr_latency, SRS_PERF_PLAY_QUEUE*1000);
}

void check_macro_features()
{
    // important preset.
#ifdef SRS_OSX
    srs_trace("SRS for OSX");
#endif
#ifdef SRS_PI
    srs_trace("SRS for pi");
#endif
#ifdef SRS_CUBIE
    srs_trace("SRS for cubieboard");
#endif
#ifdef SRS_ARM_UBUNTU12
    srs_trace("SRS for arm(build on ubuntu)");
#endif
#ifdef SRS_MIPS_UBUNTU12
    srs_trace("SRS for mips(build on ubuntu)");
#endif
    
    // for special features.
#ifndef SRS_PERF_MERGED_READ
    srs_warn("MR(merged-read) is disabled, hurts read performance. @see %s", RTMP_SIG_SRS_ISSUES(241));
#endif

    srs_trace("writev limits write %d iovs a time", sysconf(_SC_IOV_MAX));

#if VERSION_MAJOR > VERSION_STABLE
    #warning "current branch is not stable, please use stable branch instead."
    srs_warn("SRS %s is not stable, please use stable branch %s instead", RTMP_SIG_SRS_VERSION, VERSION_STABLE_BRANCH);
#endif

#if defined(SRS_AUTO_STREAM_CASTER)
    #warning "stream caster is experiment feature."
    srs_warn("stream caster is experiment feature.");
#endif

#if defined(SRS_PERF_SO_SNDBUF_SIZE) && !defined(SRS_PERF_MW_SO_SNDBUF)
    #error "SRS_PERF_SO_SNDBUF_SIZE depends on SRS_PERF_MW_SO_SNDBUF"
#endif
}

/**
* main entrance.
*/
int main(int argc, char** argv) 
{
    int ret = ERROR_SUCCESS;

    // TODO: support both little and big endian.
    srs_assert(srs_is_little_endian());

    // for gperf gmp or gcp, 
    // should never enable it when not enabled for performance issue.
#ifdef SRS_AUTO_GPERF_MP
    HeapProfilerStart("gperf.srs.gmp");
#endif
#ifdef SRS_AUTO_GPERF_CP
    ProfilerStart("gperf.srs.gcp");
#endif

    // directly compile error when these two macro defines.
#if defined(SRS_AUTO_GPERF_MC) && defined(SRS_AUTO_GPERF_MP)
    #error ("option --with-gmc confict with --with-gmp, "
        "@see: http://google-perftools.googlecode.com/svn/trunk/doc/heap_checker.html\n"
        "Note that since the heap-checker uses the heap-profiling framework internally, "
        "it is not possible to run both the heap-checker and heap profiler at the same time");
#endif
    
    // never use srs log(srs_trace, srs_error, etc) before config parse the option,
    // which will load the log config and apply it.
    if ((ret = _srs_config->parse_options(argc, argv)) != ERROR_SUCCESS) {
        return ret;
    }
    
    // config parsed, initialize log.
    if ((ret = _srs_log->initialize()) != ERROR_SUCCESS) {
        return ret;
    }

    // we check the config when the log initialized.
    if ((ret = _srs_config->check_config()) != ERROR_SUCCESS) {
        return ret;
    }
    
    srs_trace("srs(simple-rtmp-server) "RTMP_SIG_SRS_VERSION);
    srs_trace("license: "RTMP_SIG_SRS_LICENSE);
    srs_trace("primary: "RTMP_SIG_SRS_PRIMARY);
    srs_trace("authors: "RTMP_SIG_SRS_AUTHROS);
    srs_trace("contributors: "SRS_AUTO_CONSTRIBUTORS);
    srs_trace("uname: "SRS_AUTO_UNAME);
    srs_trace("build: %s, %s", SRS_AUTO_BUILD_DATE, srs_is_little_endian()? "little-endian":"big-endian");
    srs_trace("configure: "SRS_AUTO_USER_CONFIGURE);
    srs_trace("features: "SRS_AUTO_CONFIGURE);
#ifdef SRS_AUTO_ARM_UBUNTU12
    srs_trace("arm tool chain: "SRS_AUTO_EMBEDED_TOOL_CHAIN);
#endif
    srs_trace("conf: %s, limit: %d", _srs_config->config().c_str(), _srs_config->get_max_connections());
    
    // features
    check_macro_features();
    show_macro_features();
    
    /**
    * we do nothing in the constructor of server,
    * and use initialize to create members, set hooks for instance the reload handler,
    * all initialize will done in this stage.
    */
    if ((ret = _srs_server->initialize(NULL)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return run();
}

int run()
{
    // if not deamon, directly run master.
    if (!_srs_config->get_deamon()) {
        return run_master();
    }
    
    srs_trace("start deamon mode...");
    
    int pid = fork();
    
    if(pid < 0) {
        srs_error("create process error. ret=-1"); //ret=0
        return -1;
    }

    // grandpa
    if(pid > 0) {
        int status = 0;
        if(waitpid(pid, &status, 0) == -1) {
            srs_error("wait child process error! ret=-1"); //ret=0
        }
        srs_trace("grandpa process exit.");
        exit(0);
    }

    // father
    pid = fork();
    
    if(pid < 0) {
        srs_error("create process error. ret=0");
        return -1;
    }

    if(pid > 0) {
        srs_trace("father process exit. ret=0");
        exit(0);
    }

    // son
    srs_trace("son(deamon) process running.");
    
    return run_master();
}

int run_master()
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = _srs_server->initialize_st()) != ERROR_SUCCESS) {
        return ret;
    }
    
    if ((ret = _srs_server->initialize_signal()) != ERROR_SUCCESS) {
        return ret;
    }
    
    if ((ret = _srs_server->acquire_pid_file()) != ERROR_SUCCESS) {
        return ret;
    }
    
    if ((ret = _srs_server->listen()) != ERROR_SUCCESS) {
        return ret;
    }
    
    if ((ret = _srs_server->register_signal()) != ERROR_SUCCESS) {
        return ret;
    }
    
    if ((ret = _srs_server->http_handle()) != ERROR_SUCCESS) {
        return ret;
    }
    
    if ((ret = _srs_server->ingest()) != ERROR_SUCCESS) {
        return ret;
    }
    
    if ((ret = _srs_server->cycle()) != ERROR_SUCCESS) {
        return ret;
    }
    
    return 0;
}

