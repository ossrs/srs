//
// Copyright (c) 2013-2021 Winlin
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_APP_LOG_HPP
#define SRS_APP_LOG_HPP

#include <srs_core.hpp>

#include <string.h>
#include <string>

#include <srs_app_reload.hpp>
#include <srs_service_log.hpp>

// For log TAGs.
#define TAG_MAIN "MAIN"
#define TAG_MAYBE "MAYBE"
#define TAG_DTLS_ALERT "DTLS_ALERT"
#define TAG_DTLS_HANG "DTLS_HANG"
#define TAG_RESOURCE_UNSUB "RESOURCE_UNSUB"
#define TAG_LARGE_TIMER "LARGE_TIMER"

// Use memory/disk cache and donot flush when write log.
// it's ok to use it without config, which will log to console, and default trace level.
// when you want to use different level, override this classs, set the protected _level.
class SrsFileLog : public ISrsLog, public ISrsReloadHandler
{
private:
    // Defined in SrsLogLevel.
    SrsLogLevel level;
private:
    char* log_data;
    // Log to file if specified srs_log_file
    int fd;
    // Whether log to file tank
    bool log_to_file_tank;
    // Whether use utc time.
    bool utc;
public:
    SrsFileLog();
    virtual ~SrsFileLog();
// Interface ISrsLog
public:
    virtual srs_error_t initialize();
    virtual void reopen();
    virtual void verbose(const char* tag, SrsContextId context_id, const char* fmt, ...);
    virtual void info(const char* tag, SrsContextId context_id, const char* fmt, ...);
    virtual void trace(const char* tag, SrsContextId context_id, const char* fmt, ...);
    virtual void warn(const char* tag, SrsContextId context_id, const char* fmt, ...);
    virtual void error(const char* tag, SrsContextId context_id, const char* fmt, ...);
// Interface ISrsReloadHandler.
public:
    virtual srs_error_t on_reload_utc_time();
    virtual srs_error_t on_reload_log_tank();
    virtual srs_error_t on_reload_log_level();
    virtual srs_error_t on_reload_log_file();
private:
    virtual void write_log(int& fd, char* str_log, int size, int level);
    virtual void open_log_file();
};

#endif

