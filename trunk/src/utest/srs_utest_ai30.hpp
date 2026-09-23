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

#include <srs_app_log.hpp>
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

// What the file logger wrote, captured instead of a log file and a console.
class MockLogWriterForFileLog : public ISrsLogWriter
{
public:
    // The path of the last open_file(), and how many times it was asked to open one.
    std::string opened_path_;
    int open_file_count_;
    // The descriptor open_file() returns. Negative means the open failed.
    int open_fd_;
    // The descriptor of the last close_file(), and how many times it was asked to close one.
    int closed_fd_;
    int close_file_count_;
    // When set, close_file() also records the descriptor here, so a test can observe a close that happens while the
    // logger is being destroyed and owns this writer.
    int *closed_fd_out_;
    // The descriptor and bytes of the last write_file(), and how many times it was called.
    int written_fd_;
    std::string written_;
    int write_file_count_;
    // The color and bytes of the last write_console(), and how many times it was called.
    std::string console_color_;
    std::string console_;
    int write_console_count_;

public:
    MockLogWriterForFileLog();
    virtual ~MockLogWriterForFileLog();
    // Interface ISrsLogWriter
public:
    virtual int open_file(const std::string &path);
    virtual void close_file(int fd);
    virtual void write_file(int fd, const char *str_log, int size);
    virtual void write_console(const char *color, const char *str_log, int size);
};

#endif
