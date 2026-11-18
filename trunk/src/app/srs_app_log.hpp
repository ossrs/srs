//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_APP_LOG_HPP
#define SRS_APP_LOG_HPP

#include <srs_core.hpp>

#include <string.h>
#include <string>

#include <srs_app_reload.hpp>
#include <srs_protocol_log.hpp>

class ISrsAppConfig;

// A seam over the system calls, so a test can read back what the logger wrote without opening a file or writing to the
// terminal.
class ISrsLogWriter
{
public:
    ISrsLogWriter();
    virtual ~ISrsLogWriter();

public:
    // Return a negative value when the file cannot be opened.
    virtual int open_file(const std::string &path) = 0;
    virtual void close_file(int fd) = 0;
    virtual void write_file(int fd, const char *str_log, int size) = 0;
    virtual void write_console(const char *color, const char *str_log, int size) = 0;
};

class SrsLogWriter : public ISrsLogWriter
{
public:
    SrsLogWriter();
    virtual ~SrsLogWriter();

public:
    virtual int open_file(const std::string &path);
    virtual void close_file(int fd);
    virtual void write_file(int fd, const char *str_log, int size);
    virtual void write_console(const char *color, const char *str_log, int size);
};

class SrsFileLog : public ISrsLog, public ISrsReloadHandler
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    SrsLogLevel level_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    char *log_data_;
    int fd_;
    bool log_to_file_tank_;
    bool utc_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsAppConfig *config_;
    ISrsLogWriter *writer_;

public:
    SrsFileLog();
    virtual ~SrsFileLog();

public:
    virtual srs_error_t initialize();
    virtual void reopen();
    virtual void log(SrsLogLevel level, const char *tag, const SrsContextId &context_id, const char *fmt, va_list args);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual void write_log(int &fd, char *str_log, int size, int level);
    virtual void open_log_file();
};

#endif
