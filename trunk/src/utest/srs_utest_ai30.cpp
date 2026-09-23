//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//
#include <srs_utest_ai30.hpp>

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>

#include <srs_app_log.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>

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

MockLogWriterForFileLog::MockLogWriterForFileLog()
{
    open_file_count_ = 0;
    open_fd_ = -1;
    closed_fd_ = -1;
    close_file_count_ = 0;
    written_fd_ = -1;
    write_file_count_ = 0;
    write_console_count_ = 0;
}

MockLogWriterForFileLog::~MockLogWriterForFileLog()
{
}

int MockLogWriterForFileLog::open_file(const string &path)
{
    open_file_count_++;
    opened_path_ = path;
    return open_fd_;
}

void MockLogWriterForFileLog::close_file(int fd)
{
    close_file_count_++;
    closed_fd_ = fd;
}

void MockLogWriterForFileLog::write_file(int fd, const char *str_log, int size)
{
    write_file_count_++;
    written_fd_ = fd;
    written_ = string(str_log, size);
}

void MockLogWriterForFileLog::write_console(const char *color, const char *str_log, int size)
{
    write_console_count_++;
    console_color_ = color;
    console_ = string(str_log, size);
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

// The logger writes through ISrsLogWriter, so these tests read back the exact bytes and the exact descriptor the
// logger handed to the system, without creating a log file or printing to the terminal.

// Call the logger the way the SRS log macros do, through a va_list.
static void mock_log(SrsFileLog &log, SrsLogLevel level, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    log.log(level, NULL, SrsContextId(), fmt, args);
    va_end(args);
}

// Every log line is terminated, and the console tank prints the line with no color below the warn level.
VOID TEST(FileLogTest, WriteLogEndsTheConsoleLineWithTheTail)
{
    MockLogWriterForFileLog writer;
    SrsFileLog log;

    srs_freep(log.writer_);
    log.writer_ = &writer;
    log.log_to_file_tank_ = false;

    char str_log[16] = "hello";
    log.write_log(log.fd_, str_log, 5, SrsLogLevelTrace);

    // GOAL: the console receives the line with its tail appended, and no color wraps it.
    EXPECT_EQ(1, writer.write_console_count_);
    EXPECT_STREQ("", writer.console_color_.c_str());
    EXPECT_STREQ("hello\n", writer.console_.c_str());
    EXPECT_EQ(0, writer.write_file_count_);

    log.writer_ = NULL;
}

// A warning is yellow and an error is red, which is how an operator spots them in a terminal.
VOID TEST(FileLogTest, WriteLogColorsTheConsoleByLevel)
{
    MockLogWriterForFileLog writer;
    SrsFileLog log;

    srs_freep(log.writer_);
    log.writer_ = &writer;
    log.log_to_file_tank_ = false;

    char warn_log[16] = "warn";
    log.write_log(log.fd_, warn_log, 4, SrsLogLevelWarn);

    // GOAL: the warn line is wrapped in the yellow code.
    EXPECT_STREQ("\033[33m", writer.console_color_.c_str());
    EXPECT_STREQ("warn\n", writer.console_.c_str());

    char error_log[16] = "error";
    log.write_log(log.fd_, error_log, 5, SrsLogLevelError);

    // GOAL: the error line is wrapped in the red code.
    EXPECT_STREQ("\033[31m", writer.console_color_.c_str());
    EXPECT_STREQ("error\n", writer.console_.c_str());

    EXPECT_EQ(2, writer.write_console_count_);

    log.writer_ = NULL;
}

// A line longer than the log buffer is truncated, leaving room for the tail inside the buffer.
VOID TEST(FileLogTest, WriteLogTruncatesAnOversizedLine)
{
    MockLogWriterForFileLog writer;
    SrsFileLog log;

    srs_freep(log.writer_);
    log.writer_ = &writer;
    log.log_to_file_tank_ = false;

    // LOG_MAX_SIZE is 64KB, and one byte each is reserved for the tail and the terminating zero.
    int capacity = 65536;
    char *str_log = new char[capacity];
    memset(str_log, 'a', capacity);

    log.write_log(log.fd_, str_log, capacity, SrsLogLevelTrace);

    // GOAL: the console receives 65534 bytes of payload plus the tail, never more than the buffer holds.
    EXPECT_EQ(65535, (int)writer.console_.length());
    EXPECT_EQ('\n', writer.console_[writer.console_.length() - 1]);
    EXPECT_EQ('a', writer.console_[writer.console_.length() - 2]);

    srs_freepa(str_log);
    log.writer_ = NULL;
}

// With the file tank and an open descriptor, the line goes to that descriptor and no file is opened again.
VOID TEST(FileLogTest, WriteLogWritesToTheOpenDescriptor)
{
    MockAppConfigForFileLog config;
    MockLogWriterForFileLog writer;
    SrsFileLog log;

    srs_freep(log.writer_);
    log.writer_ = &writer;
    log.config_ = &config;
    log.log_to_file_tank_ = true;
    log.fd_ = 7;

    char str_log[16] = "hello";
    log.write_log(log.fd_, str_log, 5, SrsLogLevelTrace);

    // GOAL: the bytes reach the descriptor the logger already holds.
    EXPECT_EQ(1, writer.write_file_count_);
    EXPECT_EQ(7, writer.written_fd_);
    EXPECT_STREQ("hello\n", writer.written_.c_str());
    EXPECT_EQ(0, writer.open_file_count_);
    EXPECT_EQ(0, writer.write_console_count_);

    log.fd_ = -1;
    log.config_ = NULL;
    log.writer_ = NULL;
}

// With the file tank and no descriptor, as after a reopen, the configured file is opened and then written.
VOID TEST(FileLogTest, WriteLogOpensTheLogFileWhenNoDescriptorIsHeld)
{
    MockAppConfigForFileLog config;
    MockLogWriterForFileLog writer;
    SrsFileLog log;

    srs_freep(log.writer_);
    log.writer_ = &writer;
    log.config_ = &config;
    log.log_to_file_tank_ = true;
    log.fd_ = -1;
    config.log_file_ = "./objs/srs_utest_ai30_write.log";
    writer.open_fd_ = 9;

    char str_log[16] = "hello";
    log.write_log(log.fd_, str_log, 5, SrsLogLevelTrace);

    // GOAL: the file the injected config names is opened once, and the line goes to the descriptor it returned.
    EXPECT_EQ(1, writer.open_file_count_);
    EXPECT_STREQ("./objs/srs_utest_ai30_write.log", writer.opened_path_.c_str());
    EXPECT_EQ(9, log.fd_);
    EXPECT_EQ(1, writer.write_file_count_);
    EXPECT_EQ(9, writer.written_fd_);
    EXPECT_STREQ("hello\n", writer.written_.c_str());

    log.fd_ = -1;
    log.config_ = NULL;
    log.writer_ = NULL;
}

// A log file that cannot be opened, such as a path that is not writable, must drop the line rather than write to a
// negative descriptor.
VOID TEST(FileLogTest, WriteLogDropsTheLineWhenTheLogFileCannotBeOpened)
{
    MockAppConfigForFileLog config;
    MockLogWriterForFileLog writer;
    SrsFileLog log;

    srs_freep(log.writer_);
    log.writer_ = &writer;
    log.config_ = &config;
    log.log_to_file_tank_ = true;
    log.fd_ = -1;
    config.log_file_ = "./objs/srs_utest_ai30_write.log";
    writer.open_fd_ = -1;

    char str_log[16] = "hello";
    log.write_log(log.fd_, str_log, 5, SrsLogLevelTrace);

    // GOAL: the open is attempted, and nothing is written.
    EXPECT_EQ(1, writer.open_file_count_);
    EXPECT_EQ(-1, log.fd_);
    EXPECT_EQ(0, writer.write_file_count_);

    log.fd_ = -1;
    log.config_ = NULL;
    log.writer_ = NULL;
}

// A level below the configured one is dropped, which is what the log level setting buys.
VOID TEST(FileLogTest, LogDropsALevelBelowTheConfiguredOne)
{
    MockLogWriterForFileLog writer;
    SrsFileLog log;

    srs_freep(log.writer_);
    log.writer_ = &writer;
    log.log_to_file_tank_ = false;
    log.level_ = SrsLogLevelTrace;

    mock_log(log, SrsLogLevelInfo, "dropped");

    // GOAL: nothing reaches the output.
    EXPECT_EQ(0, writer.write_console_count_);
    EXPECT_EQ(0, writer.write_file_count_);

    // GOAL: the configured level itself still passes.
    mock_log(log, SrsLogLevelTrace, "kept");
    EXPECT_EQ(1, writer.write_console_count_);

    log.writer_ = NULL;
}

// The disabled level turns the logger off, whatever the configured level is.
VOID TEST(FileLogTest, LogDropsTheDisabledLevel)
{
    MockLogWriterForFileLog writer;
    SrsFileLog log;

    srs_freep(log.writer_);
    log.writer_ = &writer;
    log.log_to_file_tank_ = false;
    log.level_ = SrsLogLevelVerbose;

    mock_log(log, SrsLogLevelDisabled, "dropped");

    // GOAL: nothing reaches the output, even though the level is above the configured one.
    EXPECT_EQ(0, writer.write_console_count_);
    EXPECT_EQ(0, writer.write_file_count_);

    // GOAL: the highest level that is not disabled still passes.
    mock_log(log, SrsLogLevelError, "kept");
    EXPECT_EQ(1, writer.write_console_count_);

    log.writer_ = NULL;
}

// The message the caller formatted reaches the output, after the header the logger builds.
VOID TEST(FileLogTest, LogWritesTheFormattedMessage)
{
    MockLogWriterForFileLog writer;
    SrsFileLog log;

    srs_freep(log.writer_);
    log.writer_ = &writer;
    log.log_to_file_tank_ = false;
    log.level_ = SrsLogLevelTrace;

    mock_log(log, SrsLogLevelTrace, "stream=%s, clients=%d", "live/livestream", 7);

    // GOAL: the formatted arguments arrive, behind a header and in front of the tail. The header spelling is not
    // asserted, because the level strings differ between the v1 and v2 log level builds.
    EXPECT_EQ(1, writer.write_console_count_);
    size_t at = writer.console_.find("stream=live/livestream, clients=7\n");
    ASSERT_TRUE(at != string::npos);
    EXPECT_TRUE(at > 0);
    EXPECT_EQ('[', writer.console_[0]);

    log.writer_ = NULL;
}

// An error line carries the system error text, so the reason is in the log without the caller passing it.
VOID TEST(FileLogTest, LogAppendsTheSystemErrorToAnErrorLine)
{
    MockLogWriterForFileLog writer;
    SrsFileLog log;

    srs_freep(log.writer_);
    log.writer_ = &writer;
    log.log_to_file_tank_ = false;
    log.level_ = SrsLogLevelTrace;

    errno = EACCES;
    mock_log(log, SrsLogLevelError, "open failed");
    errno = 0;

    // GOAL: strerror() of the pending errno is appended in parentheses.
    string expect = string("open failed(") + strerror(EACCES) + ")\n";
    EXPECT_TRUE(writer.console_.find(expect) != string::npos);

    // GOAL: a trace line carries no system error.
    errno = EACCES;
    mock_log(log, SrsLogLevelTrace, "no error here");
    errno = 0;
    EXPECT_TRUE(writer.console_.find("no error here\n") != string::npos);

    log.writer_ = NULL;
}
