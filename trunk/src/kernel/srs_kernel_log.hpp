//
// Copyright (c) 2013-2021 Winlin
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_KERNEL_LOG_HPP
#define SRS_KERNEL_LOG_HPP

#include <srs_core.hpp>

#include <stdio.h>

#include <errno.h>
#include <string.h>
#include <string>

#include <srs_kernel_consts.hpp>

// The log level, for example:
//      if specified Debug level, all level messages will be logged.
//      if specified Warn level, only Warn/Error/Fatal level messages will be logged.
enum SrsLogLevel
{
    SrsLogLevelForbidden = 0x00,
    // Only used for very verbose debug, generally,
    // we compile without this level for high performance.
    SrsLogLevelVerbose = 0x01,
    SrsLogLevelInfo = 0x02,
    SrsLogLevelTrace = 0x04,
    SrsLogLevelWarn = 0x08,
    SrsLogLevelError = 0x10,
    SrsLogLevelDisabled = 0x20,
};

// The log interface provides method to write log.
// but we provides some macro, which enable us to disable the log when compile.
// @see also SmtDebug/SmtTrace/SmtWarn/SmtError which is corresponding to Debug/Trace/Warn/Fatal.
class ISrsLog
{
public:
    ISrsLog();
    virtual ~ISrsLog();
public:
    // Initialize log utilities.
    virtual srs_error_t initialize() = 0;
    // Reopen the log file for log rotate.
    virtual void reopen() = 0;
public:
    // The log for verbose, very verbose information.
    virtual void verbose(const char* tag, SrsContextId context_id, const char* fmt, ...) = 0;
    // The log for debug, detail information.
    virtual void info(const char* tag, SrsContextId context_id, const char* fmt, ...) = 0;
    // The log for trace, important information.
    virtual void trace(const char* tag, SrsContextId context_id, const char* fmt, ...) = 0;
    // The log for warn, warn is something should take attention, but not a error.
    virtual void warn(const char* tag, SrsContextId context_id, const char* fmt, ...) = 0;
    // The log for error, something error occur, do something about the error, ie. close the connection,
    // but we will donot abort the program.
    virtual void error(const char* tag, SrsContextId context_id, const char* fmt, ...) = 0;
};

// The logic context, for example, a RTMP connection, or RTC Session, etc.
// We can grep the context id to identify the logic unit, for debugging.
// For example:
//      SrsContextId cid = _srs_context->get_id(); // Get current context id.
//      SrsContextId new_cid = _srs_context->generate_id(); // Generate a new context id.
//      SrsContextId old_cid = _srs_context->set_id(new_cid); // Change the context id.
class ISrsContext
{
public:
    ISrsContext();
    virtual ~ISrsContext();
public:
    // Generate a new context id.
    // @remark We do not set to current thread, user should do this.
    virtual SrsContextId generate_id() = 0;
    // Get the context id of current thread.
    virtual const SrsContextId& get_id() = 0;
    // Set the context id of current thread.
    // @return the current context id.
    virtual const SrsContextId& set_id(const SrsContextId& v) = 0;
};

// @global User must provides a log object
extern ISrsLog* _srs_log;

// @global User must implements the LogContext and define a global instance.
extern ISrsContext* _srs_context;

// Log style.
// Use __FUNCTION__ to print c method
// Use __PRETTY_FUNCTION__ to print c++ class:method
#define srs_verbose(msg, ...) _srs_log->verbose(NULL, _srs_context->get_id(), msg, ##__VA_ARGS__)
#define srs_info(msg, ...)    _srs_log->info(NULL, _srs_context->get_id(), msg, ##__VA_ARGS__)
#define srs_trace(msg, ...)   _srs_log->trace(NULL, _srs_context->get_id(), msg, ##__VA_ARGS__)
#define srs_warn(msg, ...)    _srs_log->warn(NULL, _srs_context->get_id(), msg, ##__VA_ARGS__)
#define srs_error(msg, ...)   _srs_log->error(NULL, _srs_context->get_id(), msg, ##__VA_ARGS__)
// With tag.
#define srs_verbose2(tag, msg, ...) _srs_log->verbose(tag, _srs_context->get_id(), msg, ##__VA_ARGS__)
#define srs_info2(tag, msg, ...)    _srs_log->info(tag, _srs_context->get_id(), msg, ##__VA_ARGS__)
#define srs_trace2(tag, msg, ...)   _srs_log->trace(tag, _srs_context->get_id(), msg, ##__VA_ARGS__)
#define srs_warn2(tag, msg, ...)    _srs_log->warn(tag, _srs_context->get_id(), msg, ##__VA_ARGS__)
#define srs_error2(tag, msg, ...)   _srs_log->error(tag, _srs_context->get_id(), msg, ##__VA_ARGS__)

// TODO: FIXME: Add more verbose and info logs.
#ifndef SRS_VERBOSE
    #undef srs_verbose
    #define srs_verbose(msg, ...) (void)0
#endif
#ifndef SRS_INFO
    #undef srs_info
    #define srs_info(msg, ...) (void)0
#endif
#ifndef SRS_TRACE
    #undef srs_trace
    #define srs_trace(msg, ...) (void)0
#endif

#endif

