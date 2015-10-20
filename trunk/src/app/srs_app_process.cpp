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

#include <srs_app_process.hpp>

#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>

// for srs-librtmp, @see https://github.com/simple-rtmp-server/srs/issues/213
#ifndef _WIN32
#include <unistd.h>
#endif

using namespace std;

#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_app_config.hpp>
#include <srs_app_utility.hpp>

SrsProcess::SrsProcess()
{
    is_started         = false;
    fast_stopped       = false;
    pid                = -1;
}

SrsProcess::~SrsProcess()
{
}

bool SrsProcess::started()
{
    return is_started;
}

int SrsProcess::initialize(string binary, vector<string> argv)
{
    int ret = ERROR_SUCCESS;
    
    bin = binary;
    cli = "";
    params.clear();
    
    for (int i = 0; i < (int)argv.size(); i++) {
        std::string ffp = argv[i];
        cli += ffp;
        if (i < (int)argv.size() - 1) {
            cli += " ";
        }
    }
    
    for (int i = 0; i < (int)argv.size(); i++) {
        std::string ffp = argv[i];
        std::string nffp = (i < (int)argv.size() -1)? argv[i + 1] : "";
        
        // remove the stdout and stderr.
        if (ffp == "1" && nffp == ">") {
            if (i + 2 < (int)argv.size()) {
                stdout_file = argv[i + 2];
                i += 2;
            }
            continue;
        } else if (ffp == "2" && nffp == ">") {
            if (i + 2 < (int)argv.size()) {
                stderr_file = argv[i + 2];
                i += 2;
            }
            continue;
        }
        
        // startup params.
        params.push_back(ffp);
    }
    
    return ret;
}

int SrsProcess::start()
{
    int ret = ERROR_SUCCESS;
    
    if (is_started) {
        return ret;
    }
    
    // generate the argv of process.
    srs_trace("fork process: %s", cli.c_str());
    
    // for log
    int cid = _srs_context->get_id();
    int ppid = getpid();
    
    // TODO: fork or vfork?
    if ((pid = fork()) < 0) {
        ret = ERROR_ENCODER_FORK;
        srs_error("vfork process failed. ret=%d", ret);
        return ret;
    }
    
    // child process: ffmpeg encoder engine.
    if (pid == 0) {
        // ignore the SIGINT and SIGTERM
        signal(SIGINT, SIG_IGN);
        signal(SIGTERM, SIG_IGN);
        
        // redirect stdout to file.
        if (!stdout_file.empty()) {
            int stdout_fd = -1;
            int flags = O_CREAT|O_WRONLY|O_APPEND;
            mode_t mode = S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH;
            
            if ((stdout_fd = ::open(stdout_file.c_str(), flags, mode)) < 0) {
                ret = ERROR_ENCODER_OPEN;
                fprintf(stderr, "open process stdout %s failed. ret=%d", stdout_file.c_str(), ret);
                return ret;
            }
            
            if (dup2(stdout_fd, STDOUT_FILENO) < 0) {
                ret = ERROR_ENCODER_DUP2;
                srs_error("dup2 process stdout failed. ret=%d", ret);
                return ret;
            }
        }
        
        // redirect stderr to file.
        if (!stderr_file.empty()) {
            int stderr_fd = -1;
            int flags = O_CREAT|O_WRONLY|O_APPEND;
            mode_t mode = S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH;
            
            if ((stderr_fd = ::open(stderr_file.c_str(), flags, mode)) < 0) {
                ret = ERROR_ENCODER_OPEN;
                fprintf(stderr, "open process stderr %s failed. ret=%d", stderr_file.c_str(), ret);
                return ret;
            }
            
            if (dup2(stderr_fd, STDERR_FILENO) < 0) {
                ret = ERROR_ENCODER_DUP2;
                srs_error("dup2 process stderr failed. ret=%d", ret);
                return ret;
            }
        }
        
        // log basic info
        if (true) {
            fprintf(stderr, "\n");
            fprintf(stderr, "process parent pid=%d\n", ppid);
            fprintf(stderr, "process parent cid=%d\n", cid);
            fprintf(stderr, "process binary=%s\n", bin.c_str());
            fprintf(stderr, "process cli: %s\n", cli.c_str());
        }
        
        // close other fds
        // TODO: do in right way.
        for (int i = 3; i < 1024; i++) {
            ::close(i);
        }
        
        // memory leak in child process, it's ok.
        char** charpv_params = new char*[params.size() + 1];
        for (int i = 0; i < (int)params.size(); i++) {
            std::string& p = params[i];
            charpv_params[i] = (char*)p.data();
        }
        // EOF: NULL
        charpv_params[params.size()] = NULL;
        
        // TODO: execv or execvp
        ret = execv(bin.c_str(), charpv_params);
        if (ret < 0) {
            fprintf(stderr, "fork process failed, errno=%d(%s)", errno, strerror(errno));
        }
        exit(ret);
    }
    
    // parent.
    if (pid > 0) {
        is_started = true;
        srs_trace("vfored process, pid=%d, bin=%s", pid, bin.c_str());
        return ret;
    }
    
    return ret;
}

int SrsProcess::cycle()
{
    int ret = ERROR_SUCCESS;
    
    if (!is_started) {
        return ret;
    }
    
    // ffmpeg is prepare to stop, donot cycle.
    if (fast_stopped) {
        return ret;
    }
    
    int status = 0;
    pid_t p = waitpid(pid, &status, WNOHANG);
    
    if (p < 0) {
        ret = ERROR_SYSTEM_WAITPID;
        srs_error("process waitpid failed, pid=%d, ret=%d", pid, ret);
        return ret;
    }
    
    if (p == 0) {
        srs_info("process process pid=%d is running.", pid);
        return ret;
    }
    
    srs_trace("process pid=%d terminate, restart it.", pid);
    is_started = false;
    
    return ret;
}

void SrsProcess::stop()
{
    if (!is_started) {
        return;
    }
    
    // kill the ffmpeg,
    // when rewind, upstream will stop publish(unpublish),
    // unpublish event will stop all ffmpeg encoders,
    // then publish will start all ffmpeg encoders.
    int ret = srs_kill_forced(pid);
    if (ret != ERROR_SUCCESS) {
        srs_warn("ignore kill the process failed, pid=%d. ret=%d", pid, ret);
        return;
    }
    
    // terminated, set started to false to stop the cycle.
    is_started = false;
}

void SrsProcess::fast_stop()
{
    int ret = ERROR_SUCCESS;
    
    if (!is_started) {
        return;
    }
    
    if (pid <= 0) {
        return;
    }
    
    if (kill(pid, SIGTERM) < 0) {
        ret = ERROR_SYSTEM_KILL;
        srs_warn("ignore fast stop process failed, pid=%d. ret=%d", pid, ret);
        return;
    }
    
    return;
}

