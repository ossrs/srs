//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_UTEST_AI30_HPP
#define SRS_UTEST_AI30_HPP

/*
#include <srs_utest_ai30.hpp>
*/
#include <srs_utest.hpp>

#include <srs_utest_manual_mock.hpp>

#include <string>

// The log file the file logger opens, and whether it was asked for one.
class MockAppConfigForFileLog : public MockAppConfig
{
public:
    // The log file to open. Empty means no log file is configured.
    std::string log_file_;
    // How many times the logger asked for the log file.
    int get_log_file_count_;

public:
    MockAppConfigForFileLog();
    virtual ~MockAppConfigForFileLog();

public:
    virtual std::string get_log_file();
};

#endif
