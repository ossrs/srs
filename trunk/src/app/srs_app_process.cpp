//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_app_process.hpp>

#include <signal.h>
#include <stdlib.h>
#include <sys/types.h>

using namespace std;

#include <srs_app_config.hpp>
#include <srs_app_utility.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_utility.hpp>

ISrsProcess::ISrsProcess()
{
}

ISrsProcess::~ISrsProcess()
{
}

SrsProcess::SrsProcess()
{
    is_started_ = false;
    fast_stopped_ = false;
    pid_ = -1;
}

SrsProcess::~SrsProcess()
{
}

int SrsProcess::get_pid()
{
    return pid_;
}

bool SrsProcess::started()
{
    return is_started_;
}

srs_error_t SrsProcess::initialize(string binary, vector<string> argv)
{
    srs_error_t err = srs_success;

    bin_ = binary;
    cli_ = "";
    actual_cli_ = "";
    params_.clear();

    for (int i = 0; i < (int)argv.size(); i++) {
        std::string ffp = argv[i];
        std::string nffp = (i < (int)argv.size() - 1) ? argv[i + 1] : "";
        std::string nnffp = (i < (int)argv.size() - 2) ? argv[i + 2] : "";

        if (srs_strings_starts_with(ffp, ">")) {
            stdout_file_ = ffp.substr(1);
            continue;
        }

        if (srs_strings_starts_with(ffp, "1>")) {
            stdout_file_ = ffp.substr(2);
            continue;
        }

        if (srs_strings_starts_with(ffp, "2>")) {
            stderr_file_ = ffp.substr(2);
            continue;
        }

        if (ffp == "1" && srs_strings_starts_with(nffp, ">")) {
            if (nffp == ">") {
                if (!nnffp.empty()) {
                    stdout_file_ = nnffp;
                    i++;
                }
            } else {
                stdout_file_ = srs_strings_trim_start(nffp, ">");
            }
            i++;
            continue;
        }

        if (ffp == "2" && srs_strings_starts_with(nffp, ">")) {
            if (nffp == ">") {
                if (!nnffp.empty()) {
                    stderr_file_ = nnffp;
                    i++;
                }
            } else {
                stderr_file_ = srs_strings_trim_start(nffp, ">");
            }
            i++;
            continue;
        }

        params_.push_back(ffp);
    }

    actual_cli_ = srs_strings_join(params_, " ");
    cli_ = srs_strings_join(argv, " ");

    return err;
}

srs_error_t SrsProcess::start()
{
    srs_error_t err = srs_success;

    if (is_started_) {
        return err;
    }

    srs_info("fork process: %s", cli_.c_str());

    std::vector<const char *> argv;
    for (int i = 0; i < (int)params_.size(); i++) {
        argv.push_back(params_[i].c_str());
    }
    argv.push_back(NULL);

    std::string info = "process binary=" + bin_ + ", cli: " + cli_ + "\n";
    info += "process actual cli: " + actual_cli_ + "\n";

    SrsContextId cid = _srs_context->get_id();
    pid_ = srs_process_spawn(bin_.c_str(), &argv[0], stdout_file_.c_str(), stderr_file_.c_str(), cid.c_str(), info.c_str());
    if (pid_ < 0) {
        return srs_error_new(ERROR_ENCODER_FORK, "vfork process failed, cli=%s", cli_.c_str());
    }

    // Wait a while for the process to really start.
    // @see https://github.com/ossrs/srs/issues/1634#issuecomment-597568840
    srs_usleep(10 * SRS_UTIME_MILLISECONDS);

    is_started_ = true;
    srs_trace("forked process, pid=%d, bin=%s, stdout=%s, stderr=%s, argv=%s",
              pid_, bin_.c_str(), stdout_file_.c_str(), stderr_file_.c_str(), actual_cli_.c_str());
    return err;
}

srs_error_t SrsProcess::cycle()
{
    srs_error_t err = srs_success;

    if (!is_started_) {
        return err;
    }

    if (fast_stopped_) {
        return err;
    }

    int status = 0;
    pid_t p = srs_waitpid(pid_, &status, WNOHANG);

    if (p < 0) {
        return srs_error_new(ERROR_SYSTEM_WAITPID, "process waitpid failed, pid=%d", pid_);
    }

    if (p == 0) {
        srs_info("process process pid=%d is running.", pid_);
        return err;
    }

    srs_trace("process pid=%d terminate, please restart it.", pid_);
    is_started_ = false;

    return err;
}

void SrsProcess::stop()
{
    if (!is_started_) {
        return;
    }

    SrsAppUtility utility;
    srs_error_t err = utility.kill(pid_);
    if (err != srs_success) {
        srs_warn("ignore kill the process failed, pid=%d. err=%s", pid_, srs_error_desc(err).c_str());
        srs_freep(err);
        return;
    }

    is_started_ = false;
}

void SrsProcess::fast_stop()
{
    int ret = ERROR_SUCCESS;

    if (!is_started_) {
        return;
    }

    if (pid_ <= 0) {
        return;
    }

    if (srs_kill(pid_, SIGTERM) < 0) {
        ret = ERROR_SYSTEM_KILL;
        srs_warn("ignore fast stop process failed, pid=%d. ret=%d", pid_, ret);
        return;
    }

    fast_stopped_ = true;
    return;
}

void SrsProcess::fast_kill()
{
    int ret = ERROR_SUCCESS;

    if (!is_started_) {
        return;
    }

    if (pid_ <= 0) {
        return;
    }

    if (srs_kill(pid_, SIGKILL) < 0) {
        ret = ERROR_SYSTEM_KILL;
        srs_warn("ignore fast kill process failed, pid=%d. ret=%d", pid_, ret);
        return;
    }

    // Try to reap the child, so it does not stay a zombie.
    int status = 0;
    srs_waitpid(pid_, &status, WNOHANG);

    return;
}
