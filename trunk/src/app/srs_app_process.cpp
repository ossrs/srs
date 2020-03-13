/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2020 Winlin
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

srs_error_t SrsProcess::initialize(string binary, vector<string> argv)
{
    srs_error_t err = srs_success;
    
    bin = binary;
    cli = "";
    actual_cli = "";
    params.clear();
    
    for (int i = 0; i < (int)argv.size(); i++) {
        std::string ffp = argv[i];
        std::string nffp = (i < (int)argv.size() - 1)? argv[i + 1] : "";
        std::string nnffp = (i < (int)argv.size() - 2)? argv[i + 2] : "";
        
        // >file
        if (srs_string_starts_with(ffp, ">")) {
            stdout_file = ffp.substr(1);
            continue;
        }
        
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
    
    return err;
}

srs_error_t srs_redirect_output(string from_file, int to_fd)
{
    srs_error_t err = srs_success;
    
    // use default output.
    if (from_file.empty()) {
        return err;
    }
    
    // redirect the fd to file.
    int fd = -1;
    int flags = O_CREAT|O_RDWR|O_APPEND;
    mode_t mode = S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH;
    
    if ((fd = ::open(from_file.c_str(), flags, mode)) < 0) {
        return srs_error_new(ERROR_FORK_OPEN_LOG, "open process %d %s failed", to_fd, from_file.c_str());
    }
    
    if (dup2(fd, to_fd) < 0) {
        return srs_error_new(ERROR_FORK_DUP2_LOG, "dup2 process %d failed", to_fd);
    }
    
    ::close(fd);
    
    return err;
}

srs_error_t SrsProcess::start()
{
    srs_error_t err = srs_success;
    
    if (is_started) {
        return err;
    }
    
    // generate the argv of process.
    srs_info("fork process: %s", cli.c_str());
    
    // for log
    int cid = _srs_context->get_id();
    int ppid = getpid();
    
    // TODO: fork or vfork?
    if ((pid = fork()) < 0) {
        return srs_error_new(ERROR_ENCODER_FORK, "vfork process failed, cli=%s", cli.c_str());
    }
    
    // for osx(lldb) to debug the child process.
    // user can use "lldb -p <pid>" to resume the parent or child process.
    //kill(0, SIGSTOP);
    
    // child process: ffmpeg encoder engine.
    if (pid == 0) {
        // ignore the SIGINT and SIGTERM
        signal(SIGINT, SIG_IGN);
        signal(SIGTERM, SIG_IGN);

        // for the stdout, ignore when not specified.
        // redirect stdout to file if possible.
        if ((err = srs_redirect_output(stdout_file, STDOUT_FILENO)) != srs_success) {
            return srs_error_wrap(err, "redirect output");
        }
        
        // for the stderr, ignore when not specified.
        // redirect stderr to file if possible.
        if ((err = srs_redirect_output(stderr_file, STDERR_FILENO)) != srs_success) {
            return srs_error_wrap(err, "redirect output");
        }

        // No stdin for process, @bug https://github.com/ossrs/srs/issues/1592
        if ((err = srs_redirect_output("/dev/null", STDIN_FILENO)) != srs_success) {
            return srs_error_wrap(err, "redirect input");
        }

        // should never close the fd 3+, for it myabe used.
        // for fd should close at exec, use fnctl to set it.
        
        // log basic info to stderr.
        if (true) {
            fprintf(stdout, "\n");
            fprintf(stdout, "process ppid=%d, cid=%d, pid=%d, in=%d, out=%d, err=%d\n",
                ppid, cid, getpid(), STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO);
            fprintf(stdout, "process binary=%s, cli: %s\n", bin.c_str(), cli.c_str());
            fprintf(stdout, "process actual cli: %s\n", actual_cli.c_str());
        }
        
        // memory leak in child process, it's ok.
        char** argv = new char*[params.size() + 1];
        for (int i = 0; i < (int)params.size(); i++) {
            std::string& p = params[i];
            
            // memory leak in child process, it's ok.
            char* v = new char[p.length() + 1];
            argv[i] = strcpy(v, p.data());
        }
        argv[params.size()] = NULL;
        
        // use execv to start the program.
        int r0 = execv(bin.c_str(), argv);
        if (r0 < 0) {
            fprintf(stderr, "fork process failed, errno=%d(%s)", errno, strerror(errno));
        }
        exit(r0);
    }
    
    // parent.
    if (pid > 0) {
        // Wait for a while for process to really started.
        // @see https://github.com/ossrs/srs/issues/1634#issuecomment-597568840
        srs_usleep(10 * SRS_UTIME_MILLISECONDS);

        is_started = true;
        srs_trace("fored process, pid=%d, bin=%s, stdout=%s, stderr=%s, argv=%s",
                  pid, bin.c_str(), stdout_file.c_str(), stderr_file.c_str(), actual_cli.c_str());
        return err;
    }
    
    return err;
}

srs_error_t SrsProcess::cycle()
{
    srs_error_t err = srs_success;
    
    if (!is_started) {
        return err;
    }
    
    // ffmpeg is prepare to stop, donot cycle.
    if (fast_stopped) {
        return err;
    }
    
    int status = 0;
    pid_t p = waitpid(pid, &status, WNOHANG);
    
    if (p < 0) {
        return srs_error_new(ERROR_SYSTEM_WAITPID, "process waitpid failed, pid=%d", pid);
    }
    
    if (p == 0) {
        srs_info("process process pid=%d is running.", pid);
        return err;
    }
    
    srs_trace("process pid=%d terminate, please restart it.", pid);
    is_started = false;
    
    return err;
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
    srs_error_t err = srs_kill_forced(pid);
    if (err != srs_success) {
        srs_warn("ignore kill the process failed, pid=%d. err=%s", pid, srs_error_desc(err).c_str());
        srs_freep(err);
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

void SrsProcess::fast_kill()
{
    int ret = ERROR_SUCCESS;

    if (!is_started) {
        return;
    }

    if (pid <= 0) {
        return;
    }

    if (kill(pid, SIGKILL) < 0) {
        ret = ERROR_SYSTEM_KILL;
        srs_warn("ignore fast kill process failed, pid=%d. ret=%d", pid, ret);
        return;
    }

    // Try to wait pid to avoid zombie FFMEPG.
    int status = 0;
    waitpid(pid, &status, WNOHANG);

    return;
}

