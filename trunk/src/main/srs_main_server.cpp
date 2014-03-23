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

#include <srs_kernel_log.hpp>
#include <srs_kernel_error.hpp>
#include <srs_app_server.hpp>
#include <srs_app_config.hpp>
#include <srs_app_log.hpp>

// kernel module.
ISrsLog* _srs_log = new SrsFastLog();
ISrsThreadContext* _srs_context = new SrsThreadContext();
// app module.
SrsConfig* _srs_config = new SrsConfig();
SrsServer* _srs_server = new SrsServer();

#include <stdlib.h>
#include <signal.h>

#include <sys/types.h>
#include <sys/wait.h>

#ifdef SRS_GPERF_MP
    #include <gperftools/heap-profiler.h>
#endif
#ifdef SRS_GPERF_CP
    #include <gperftools/profiler.h>
#endif

void handler(int signo)
{
    srs_trace("get a signal, signo=%d", signo);
    _srs_server->on_signal(signo);
}

int run_master() 
{
    int ret = ERROR_SUCCESS;
    
    signal(SIGNAL_RELOAD, handler);
    signal(SIGTERM, handler);
    signal(SIGINT, handler);
    
    if ((ret = _srs_server->initialize_st()) != ERROR_SUCCESS) {
        return ret;
    }
    
    if ((ret = _srs_server->listen()) != ERROR_SUCCESS) {
        return ret;
    }
    
    if ((ret = _srs_server->cycle()) != ERROR_SUCCESS) {
        return ret;
    }
    
    return 0;
}

int run() 
{
    // if not deamon, directly run master.
    if (!_srs_config->get_deamon()) {
        return run_master();
    }
    
    srs_trace("start deamon mode...");
    
    int pid = fork();
    
    if(pid == -1){
        srs_error("create process error. ret=-1"); //ret=0
        return -1;
    }

    // grandpa
    if(pid > 0){
        int status = 0;
        if(waitpid(pid, &status, 0) == -1){
            srs_error("wait child process error! ret=-1"); //ret=0
        }
        srs_trace("grandpa process exit.");
        exit(0);
        return 0;
    }

    // father
    pid = fork();
    
    if(pid == -1){
        srs_error("create process error. ret=-1");
        return -1;
    }

    if(pid > 0){
        srs_trace("father process exit. ret=-1");
        exit(0);
        return 0;
    }

    // son
    srs_trace("son(deamon) process running.");
    
    return run_master();
}

int main(int argc, char** argv) 
{
    int ret = ERROR_SUCCESS;
    
    srs_trace("srs(simple-rtmp-server)");
    
    // TODO: support both little and big endian.
    srs_assert(srs_is_little_endian());

#ifdef SRS_GPERF_MP
    HeapProfilerStart("gperf.srs.gmp");
#endif
#ifdef SRS_GPERF_CP
    ProfilerStart("gperf.srs.gcp");
#endif

#ifdef SRS_GPERF_MC
    #ifdef SRS_GPERF_MP
    srs_error("option --with-gmc confict with --with-gmp, "
        "@see: http://google-perftools.googlecode.com/svn/trunk/doc/heap_checker.html\n"
        "Note that since the heap-checker uses the heap-profiling framework internally, "
        "it is not possible to run both the heap-checker and heap profiler at the same time");
    return -1;
    #endif
#endif
    
    if ((ret = _srs_config->parse_options(argc, argv)) != ERROR_SUCCESS) {
        return ret;
    }

    srs_trace("uname: "SRS_UNAME);
    srs_trace("build: %s, %s", SRS_BUILD_DATE, srs_is_little_endian()? "little-endian":"big-endian");
    srs_trace("configure: "SRS_CONFIGURE);
    
    if ((ret = _srs_server->initialize()) != ERROR_SUCCESS) {
        return ret;
    }
    
    if ((ret = _srs_server->acquire_pid_file()) != ERROR_SUCCESS) {
        return ret;
    }
    
    return run();
}
