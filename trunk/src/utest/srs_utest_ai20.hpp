//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_UTEST_AI20_HPP
#define SRS_UTEST_AI20_HPP

/*
#include <srs_utest_ai20.hpp>
*/
#include <srs_utest.hpp>

#include <srs_app_server.hpp>

// Mock ISrsSignalHandler implementation for testing SrsSignalManager
class MockSignalHandlerForManager : public ISrsSignalHandler
{
public:
    int signal_reload_count_;
    int signal_fast_quit_count_;
    int signal_gracefully_quit_count_;
    int signal_reopen_log_count_;
    int signal_persistence_config_count_;
    int signal_assert_abort_count_;
    int last_signo_;

public:
    MockSignalHandlerForManager();
    virtual ~MockSignalHandlerForManager();

public:
    virtual void on_signal(int signo);
    void reset();
};

#endif
