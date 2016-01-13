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

#include <srs_app_process.hpp>

#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>

// for srs-librtmp, @see https://github.com/ossrs/srs/issues/213
#ifndef _WIN32
#include <unistd.h>
#endif

using namespace std;

#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_app_config.hpp>
#include <srs_app_utility.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_utility.hpp>
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

int SrsProcess::get_pid()
{
    return pid;
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
    actual_cli = "";
    params.clear();
    
    for (int i = 0; i < (int)argv.size(); i++) {
        std::string ffp = argv[i];
        std::string nffp = (i < (int)argv.size() - 1)? argv[i + 1] : "";
        std::string nnffp = (i < (int)argv.size() - 2)? argv[i + 2] : "";
        
        // 1>file
        if (srs_string_starts_with(ffp, "1>")) {
            stdout_file = ffp.substr(2);
            continue;
        }
        
        // 2>file
        if (srs_string_starts_with(ffp, "2>")) {
            stderr_file = ffp.substr(2);
            continue;
        }
        
        // 1 >X
        if (ffp == "1" && srs_string_starts_with(nffp, ">")) {
            if (nffp == ">") {
                // 1 > file
                if (!nnffp.empty()) {
                    stdout_file = nnffp;
                    i++;
                }
            } else {
                // 1 >file
                stdout_file = srs_string_trim_start(nffp, ">");
            }
            // skip the >
            i++;
            continue;
        }
        
        // 2 >X
        if (ffp == "2" && srs_string_starts_with(nffp, ">")) {
            if (nffp == ">") {
                // 2 > file
                if (!nnffp.empty()) {
                    stderr_file = nnffp;
                    i++;
                }
            } else {
                // 2 >file
                stderr_file = srs_string_trim_start(nffp, ">");
            }
            // skip the >
            i++;
            continue;
        }
        
        params.push_back(ffp);
    }
    
    actual_cli = srs_join_vector_string(params, " ");
    cli = srs_join_vector_string(argv, " ");
    
    return ret;
}

int SrsProcess::start()
{
    int ret = ERROR_SUCCESS;
    
    if (is_started) {
        return ret;
    }
    
    // generate the argv of process.
    srs_info("fork process: %s", cli.c_str());
    
    // for log
    int cid = _srs_context->get_id();
    int ppid = getpid();
    
    // TODO: fork or vfork?
    if ((pid = fork()) < 0) {
        ret = ERROR_ENCODER_FORK;
        srs_error("vfork process failed, cli=%s. ret=%d", cli.c_str(), ret);
        return ret;
    }
    
    // for osx(lldb) to debug the child process.
    // user can use "lldb -p <pid>" to resume the parent or child process.
    //kill(0, SIGSTOP);
    
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
                exit(ret);
            }
            
            if (dup2(stdout_fd, STDOUT_FILENO) < 0) {
                ret = ERROR_ENCODER_DUP2;
                srs_error("dup2 process stdout failed. ret=%d", ret);
                exit(ret);
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
                exit(ret);
            }
            
            if (dup2(stderr_fd, STDERR_FILENO) < 0) {
                ret = ERROR_ENCODER_DUP2;
                srs_error("dup2 process stderr failed. ret=%d", ret);
                exit(ret);
            }
        }
        
        // log basic info
        if (true) {
            fprintf(stderr, "\n");
            fprintf(stderr, "process ppid=%d, cid=%d, pid=%d\n", ppid, cid, getpid());
            fprintf(stderr, "process binary=%s\n", bin.c_str());
            fprintf(stderr, "process cli: %s\n", cli.c_str());
            fprintf(stderr, "process actual cli: %s\n", actual_cli.c_str());
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
        srs_trace("fored process, pid=%d, bin=%s, stdout=%s, stderr=%s, argv=%s",
            pid, bin.c_str(), stdout_file.c_str(), stdout_file.c_str(), actual_cli.c_str());
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
    
    srs_trace("process pid=%d terminate, please restart it.", pid);
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

