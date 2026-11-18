//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_APP_PROCESS_HPP
#define SRS_APP_PROCESS_HPP

#include <srs_core.hpp>

#include <string>
#include <vector>

class ISrsProcess
{
public:
    ISrsProcess();
    virtual ~ISrsProcess();

public:
    virtual int get_pid() = 0;
    virtual bool started() = 0;
    virtual srs_error_t initialize(std::string binary, std::vector<std::string> argv) = 0;

public:
    virtual srs_error_t start() = 0;
    virtual srs_error_t cycle() = 0;
    virtual void stop() = 0;

public:
    virtual void fast_stop() = 0;
    virtual void fast_kill() = 0;
};

class SrsProcess : public ISrsProcess
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    bool is_started_;
    bool fast_stopped_;
    pid_t pid_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    std::string bin_;
    std::string stdout_file_;
    std::string stderr_file_;
    std::vector<std::string> params_;
    std::string cli_;
    std::string actual_cli_;

public:
    SrsProcess();
    virtual ~SrsProcess();

public:
    virtual int get_pid();
    virtual bool started();
    // By execv() convention, argv[0] must be the binary.
    virtual srs_error_t initialize(std::string binary, std::vector<std::string> argv);

public:
    virtual srs_error_t start();
    virtual srs_error_t cycle();
    // Waits up to SRS_PROCESS_QUIT_TIMEOUT_MS for the process to quit after SIGTERM.
    virtual void stop();

public:
    // To stop N processes, call fast_stop on all of them, then stop on each. They then quit
    // at the same time, in about one SRS_PROCESS_QUIT_TIMEOUT_MS, while stop alone on each
    // takes up to N times that. The caller must still call stop to make sure the process quits.
    virtual void fast_stop();
    // Use it only when the server quits.
    virtual void fast_kill();
};

#endif
