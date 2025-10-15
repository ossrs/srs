//
// Copyright (c) 2013-2025 The SRS Authors
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

// Use memory/disk cache and donot flush when write log.
// it's ok to use it without config, which will log to console, and default trace level.
// when you want to use different level, override this classs, set the protected _level.
class SrsFileLog : public ISrsLog, public ISrsReloadHandler
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // Defined in SrsLogLevel.
    SrsLogLevel level_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    char *log_data_;
    // Log to file if specified srs_log_file
    int fd_;
    // Whether log to file tank
    bool log_to_file_tank_;
    // Whether use utc time.
    bool utc_;

public:
    SrsFileLog();
    virtual ~SrsFileLog();
    // Interface ISrsLog
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
