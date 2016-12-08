/*
The MIT License (MIT)

Copyright (c) 2013-2016 SRS(ossrs)

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

#include <sstream>
using namespace std;

#ifdef SRS_AUTO_GPERF_MP
    #include <gperftools/heap-profiler.h>
#endif
#ifdef SRS_AUTO_GPERF_CP
    #include <gperftools/profiler.h>
#endif

using namespace std;

#include <srs_kernel_error.hpp>
#include <srs_app_server.hpp>
#include <srs_app_config.hpp>
#include <srs_app_log.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_core_performance.hpp>
#include <srs_app_utility.hpp>
#include <srs_core_autofree.hpp>

// pre-declare
int run(SrsServer* svr);
int run_master(SrsServer* svr);

// @global log and context.
ISrsLog* _srs_log = new SrsFastLog();
ISrsThreadContext* _srs_context = new SrsThreadContext();
// @global config object for app module.
SrsConfig* _srs_config = new SrsConfig();

// @global version of srs, which can grep keyword "XCORE"
extern const char* _srs_version;

/**
* show the features by macro, the actual macro values.
*/
void show_macro_features()
{
    if (true) {
        stringstream ss;
        
        ss << "features";
        
        // rch(rtmp complex handshake)
        ss << ", rch:" << srs_bool2switch(SRS_AUTO_SSL_BOOL);
        ss << ", hls:" << srs_bool2switch(SRS_AUTO_HLS_BOOL);
        ss << ", hds:" << srs_bool2switch(SRS_AUTO_HDS_BOOL);
        // hc(http callback)
        ss << ", hc:" << srs_bool2switch(SRS_AUTO_HTTP_CALLBACK_BOOL);
        // ha(http api)
        ss << ", ha:" << srs_bool2switch(SRS_AUTO_HTTP_API_BOOL);
        // hs(http server)
        ss << ", hs:" << srs_bool2switch(SRS_AUTO_HTTP_SERVER_BOOL);
        // hp(http parser)
        ss << ", hp:" << srs_bool2switch(SRS_AUTO_HTTP_CORE_BOOL);
        ss << ", dvr:" << srs_bool2switch(SRS_AUTO_DVR_BOOL);
        // trans(transcode)
        ss << ", trans:" << srs_bool2switch(SRS_AUTO_TRANSCODE_BOOL);
        // inge(ingest)
        ss << ", inge:" << srs_bool2switch(SRS_AUTO_INGEST_BOOL);
        ss << ", kafka:" << srs_bool2switch(SRS_AUTO_KAFKA_BOOL);
        ss << ", stat:" << srs_bool2switch(SRS_AUTO_STAT_BOOL);
        ss << ", nginx:" << srs_bool2switch(SRS_AUTO_NGINX_BOOL);
        // ff(ffmpeg)
        ss << ", ff:" << srs_bool2switch(SRS_AUTO_FFMPEG_TOOL_BOOL);
        // sc(stream-caster)
        ss << ", sc:" << srs_bool2switch(SRS_AUTO_STREAM_CASTER_BOOL);
        srs_trace(ss.str().c_str());
    }
    
    if (true) {
        stringstream ss;
        ss << "SRS on ";
#ifdef SRS_OSX
        ss << "OSX";
#endif
#ifdef SRS_PI
        ss << "RespberryPi";
#endif
#ifdef SRS_CUBIE
        ss << "CubieBoard";
#endif
#ifdef SRS_ARM_UBUNTU12
        ss << "ARM(build on ubuntu)";
#endif
#ifdef SRS_MIPS_UBUNTU12
        ss << "MIPS(build on ubuntu)";
#endif
        
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
        ss << "arm";
#endif
        
#ifndef SRS_OSX
        ss << ", glibc" << (int)__GLIBC__ << "." <<  (int)__GLIBC_MINOR__;
#endif
        
        ss << ", conf:" << _srs_config->config() << ", limit:" << _srs_config->get_max_connections()
            << ", writev:" << sysconf(_SC_IOV_MAX) << ", encoding:" << (srs_is_little_endian()? "little-endian":"big-endian")
            << ", HZ:" << (int)sysconf(_SC_CLK_TCK);
        
        srs_trace(ss.str().c_str());
    }
    
    if (true) {
        stringstream ss;
        
        // mw(merged-write)
        ss << "mw sleep:" << SRS_PERF_MW_SLEEP << "ms";
        
        // mr(merged-read)
        ss << ". mr ";
#ifdef SRS_PERF_MERGED_READ
        ss << "enabled:on";
#else
        ss << "enabled:off";
#endif
        ss << ", default:" << SRS_PERF_MR_ENABLED << ", sleep:" << SRS_PERF_MR_SLEEP << "ms";
        ss << ", @see " << RTMP_SIG_SRS_ISSUES(241);
        
        srs_trace(ss.str().c_str());
    }
    
    if (true) {
        stringstream ss;
        
        // gc(gop-cache)
        ss << "gc:" << srs_bool2switch(SRS_PERF_GOP_CACHE);
        // pq(play-queue)
        ss << ", pq:" << SRS_PERF_PLAY_QUEUE << "s";
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
        
        srs_trace(ss.str().c_str());
    }
    
    // others
    int possible_mr_latency = 0;
#ifdef SRS_PERF_MERGED_READ
    possible_mr_latency = SRS_PERF_MR_SLEEP;
#endif
    srs_trace("system default latency in ms: mw(0-%d) + mr(0-%d) + play-queue(0-%d)",
              SRS_PERF_MW_SLEEP, possible_mr_latency, SRS_PERF_PLAY_QUEUE*1000);
    
#ifdef SRS_AUTO_MEM_WATCH
    #warning "srs memory watcher will hurts performance. user should kill by SIGTERM or init.d script."
    srs_warn("srs memory watcher will hurts performance. user should kill by SIGTERM or init.d script.");
#endif
    
#if defined(SRS_AUTO_STREAM_CASTER)
    #warning "stream caster is experiment feature."
    srs_warn("stream caster is experiment feature.");
#endif
    
#if VERSION_MAJOR > VERSION_STABLE
    #warning "current branch is not stable, please use stable branch instead."
    srs_warn("SRS %s is not stable, please use stable branch %s instead", RTMP_SIG_SRS_VERSION, VERSION_STABLE_BRANCH);
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
    
    // never use gmp to check memory leak.
#ifdef SRS_AUTO_GPERF_MP
    #warning "gmp is not used for memory leak, please use gmc instead."
#endif

    // never use srs log(srs_trace, srs_error, etc) before config parse the option,
    // which will load the log config and apply it.
    if ((ret = _srs_config->parse_options(argc, argv)) != ERROR_SUCCESS) {
        return ret;
    }

    // change the work dir and set cwd.
    string cwd = _srs_config->get_work_dir();
    if (!cwd.empty() && cwd != "./" && (ret = chdir(cwd.c_str())) != ERROR_SUCCESS) {
        srs_error("change cwd to %s failed. ret=%d", cwd.c_str(), ret);
        return ret;
    }
    if ((ret = _srs_config->initialize_cwd()) != ERROR_SUCCESS) {
        return ret;
    }

    // config parsed, initialize log.
    if ((ret = _srs_log->initialize()) != ERROR_SUCCESS) {
        return ret;
    }
    
    // config already applied to log.
    srs_trace(RTMP_SIG_SRS_SERVER", stable is "RTMP_SIG_SRS_PRIMARY);
    srs_trace("license: "RTMP_SIG_SRS_LICENSE", "RTMP_SIG_SRS_COPYRIGHT);
    srs_trace("authors: "RTMP_SIG_SRS_AUTHROS);
    srs_trace("contributors: "SRS_AUTO_CONSTRIBUTORS);
    srs_trace("build: %s, configure:%s, uname: %s", SRS_AUTO_BUILD_DATE, SRS_AUTO_USER_CONFIGURE, SRS_AUTO_UNAME);
    srs_trace("configure detail: "SRS_AUTO_CONFIGURE);
#ifdef SRS_AUTO_EMBEDED_TOOL_CHAIN
    srs_trace("crossbuild tool chain: "SRS_AUTO_EMBEDED_TOOL_CHAIN);
#endif
    srs_trace("cwd=%s, work_dir=%s", _srs_config->cwd().c_str(), cwd.c_str());
    
#ifdef SRS_PERF_GLIBC_MEMORY_CHECK
    // ensure glibc write error to stderr.
    setenv("LIBC_FATAL_STDERR_", "1", 1);
    // ensure glibc to do alloc check.
    setenv("MALLOC_CHECK_", "1", 1);
    srs_trace("env MALLOC_CHECK_=1 LIBC_FATAL_STDERR_=1");
#endif
    
#ifdef SRS_AUTO_GPERF_MD
    char* TCMALLOC_PAGE_FENCE = getenv("TCMALLOC_PAGE_FENCE");
    if (!TCMALLOC_PAGE_FENCE || strcmp(TCMALLOC_PAGE_FENCE, "1")) {
        srs_trace("gmd enabled without env TCMALLOC_PAGE_FENCE=1");
    } else {
        srs_trace("env TCMALLOC_PAGE_FENCE=1");
    }
#endif

    // we check the config when the log initialized.
    if ((ret = _srs_config->check_config()) != ERROR_SUCCESS) {
        return ret;
    }

    // features
    show_macro_features();
    
    SrsServer* svr = new SrsServer();
    SrsAutoFree(SrsServer, svr);
    
    /**
    * we do nothing in the constructor of server,
    * and use initialize to create members, set hooks for instance the reload handler,
    * all initialize will done in this stage.
    */
    if ((ret = svr->initialize(NULL)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return run(svr);
}

int run(SrsServer* svr)
{
    // if not deamon, directly run master.
    if (!_srs_config->get_deamon()) {
        return run_master(svr);
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
    
    return run_master(svr);
}

int run_master(SrsServer* svr)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = svr->initialize_st()) != ERROR_SUCCESS) {
        return ret;
    }

    if ((ret = svr->initialize_signal()) != ERROR_SUCCESS) {
        return ret;
    }
    
    if ((ret = svr->acquire_pid_file()) != ERROR_SUCCESS) {
        return ret;
    }
    
    if ((ret = svr->listen()) != ERROR_SUCCESS) {
        return ret;
    }
    
    if ((ret = svr->register_signal()) != ERROR_SUCCESS) {
        return ret;
    }
    
    if ((ret = svr->http_handle()) != ERROR_SUCCESS) {
        return ret;
    }
    
    if ((ret = svr->ingest()) != ERROR_SUCCESS) {
        return ret;
    }
    
    if ((ret = svr->cycle()) != ERROR_SUCCESS) {
        return ret;
    }
    
    return 0;
}

