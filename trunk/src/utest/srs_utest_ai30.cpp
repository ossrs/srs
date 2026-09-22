//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//
#include <srs_utest_ai30.hpp>

#include <fcntl.h>
#include <unistd.h>

#include <srs_app_log.hpp>
#include <srs_kernel_error.hpp>

using namespace std;

MockAppConfigForFileLog::MockAppConfigForFileLog()
{
    get_log_file_count_ = 0;
}

MockAppConfigForFileLog::~MockAppConfigForFileLog()
{
}

string MockAppConfigForFileLog::get_log_file()
{
    get_log_file_count_++;
    return log_file_;
}

// Whether fd is still an open descriptor of this process.
static bool is_fd_open(int fd)
{
    return ::fcntl(fd, F_GETFD) != -1;
}

// The file logger reopens its log file on SIGUSR1, for log rotation. Every path out of reopen() must leave the
// descriptor member consistent with what the process actually holds, because write_log() opens a new file only when
// the member is negative. A closed descriptor left in the member is worse than a leaked one: the number is free for
// the next socket or file the process opens, so the next log write, and the close of the next reopen(), land on
// whatever took it over.

// Reopening while logging to the console has no file to reopen, so the descriptor must be released and cleared.
VOID TEST(FileLogTest, ReopenClearsTheDescriptorWhenTheTankIsConsole)
{
    SrsFileLog log;

    // The logger holds an open log file, as it does after a start with the file tank.
    log.fd_ = ::open("/dev/null", O_RDWR);
    ASSERT_TRUE(log.fd_ > 0);
    int previous = log.fd_;

    // A reload moved the logs to the console, then SIGUSR1 asked for a reopen.
    log.log_to_file_tank_ = false;
    log.reopen();

    // GOAL: the descriptor is closed and the member no longer names it.
    EXPECT_FALSE(is_fd_open(previous));
    EXPECT_EQ(-1, log.fd_);
}

// Reopening with no log file configured cannot open one, so the descriptor must be released and cleared.
VOID TEST(FileLogTest, ReopenClearsTheDescriptorWhenNoLogFileIsConfigured)
{
    MockAppConfigForFileLog config;
    SrsFileLog log;

    // The logger holds an open log file, and keeps the file tank.
    log.fd_ = ::open("/dev/null", O_RDWR);
    ASSERT_TRUE(log.fd_ > 0);
    int previous = log.fd_;

    log.config_ = &config;
    log.log_to_file_tank_ = true;
    config.log_file_ = "";

    log.reopen();

    // GOAL: the file name comes from the injected config, and finding none leaves no descriptor behind.
    EXPECT_EQ(1, config.get_log_file_count_);
    EXPECT_FALSE(is_fd_open(previous));
    EXPECT_EQ(-1, log.fd_);

    log.config_ = NULL;
}

// Reopening with the file tank must open the configured file, which is what log rotation depends on.
VOID TEST(FileLogTest, ReopenOpensTheConfiguredLogFile)
{
    MockAppConfigForFileLog config;
    SrsFileLog log;

    string path = "./objs/srs_utest_ai30.log";
    ::unlink(path.c_str());

    log.fd_ = ::open("/dev/null", O_RDWR);
    ASSERT_TRUE(log.fd_ > 0);
    int previous = log.fd_;

    log.config_ = &config;
    log.log_to_file_tank_ = true;
    config.log_file_ = path;

    log.reopen();

    // GOAL: the previous descriptor is released, and the file the injected config names is open.
    EXPECT_EQ(1, config.get_log_file_count_);
    EXPECT_TRUE(log.fd_ > 0);
    EXPECT_TRUE(is_fd_open(log.fd_));
    EXPECT_EQ(0, ::access(path.c_str(), F_OK));

    // The reopened descriptor may reuse the released number, so only assert the release when it was not reused.
    if (log.fd_ != previous) {
        EXPECT_FALSE(is_fd_open(previous));
    }

    log.config_ = NULL;
    ::unlink(path.c_str());
}
