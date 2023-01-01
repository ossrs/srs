//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#ifndef SRS_KERNEL_LOG_HPP
#define SRS_KERNEL_LOG_HPP

#include <srs_core.hpp>

#include <stdio.h>

#include <errno.h>
#include <string.h>
#include <string>
#include <stdarg.h>

#include <srs_kernel_consts.hpp>

// The log level, see https://github.com/apache/logging-log4j2/blob/release-2.x/log4j-api/src/main/java/org/apache/logging/log4j/Level.java
// Please note that the enum name might not be the string, to keep compatible with previous definition.
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

// Get the level in string.
extern const char* srs_log_level_strings[];

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
    // Write a application level log. All parameters are required except the tag.
    virtual void log(SrsLogLevel level, const char* tag, const SrsContextId& context_id, const char* fmt, va_list args) = 0;
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

// @global User must implements the LogContext and define a global instance.
extern ISrsContext* _srs_context;

// @global User must provides a log object
extern ISrsLog* _srs_log;

// Global log function implementation. Please use helper macros, for example, srs_trace or srs_error.
extern void srs_logger_impl(SrsLogLevel level, const char* tag, const SrsContextId& context_id, const char* fmt, ...);

// Log style.
// Use __FUNCTION__ to print c method
// Use __PRETTY_FUNCTION__ to print c++ class:method
#define srs_verbose(msg, ...) srs_logger_impl(SrsLogLevelVerbose, NULL, _srs_context->get_id(), msg, ##__VA_ARGS__)
#define srs_info(msg, ...) srs_logger_impl(SrsLogLevelInfo, NULL, _srs_context->get_id(), msg, ##__VA_ARGS__)
#define srs_trace(msg, ...) srs_logger_impl(SrsLogLevelTrace, NULL, _srs_context->get_id(), msg, ##__VA_ARGS__)
#define srs_warn(msg, ...) srs_logger_impl(SrsLogLevelWarn, NULL, _srs_context->get_id(), msg, ##__VA_ARGS__)
#define srs_error(msg, ...) srs_logger_impl(SrsLogLevelError, NULL, _srs_context->get_id(), msg, ##__VA_ARGS__)
// With tag.
#define srs_verbose2(tag, msg, ...) srs_logger_impl(SrsLogLevelVerbose, tag, _srs_context->get_id(), msg, ##__VA_ARGS__)
#define srs_info2(tag, msg, ...) srs_logger_impl(SrsLogLevelInfo, tag, _srs_context->get_id(), msg, ##__VA_ARGS__)
#define srs_trace2(tag, msg, ...) srs_logger_impl(SrsLogLevelTrace, tag, _srs_context->get_id(), msg, ##__VA_ARGS__)
#define srs_warn2(tag, msg, ...) srs_logger_impl(SrsLogLevelWarn, tag, _srs_context->get_id(), msg, ##__VA_ARGS__)
#define srs_error2(tag, msg, ...) srs_logger_impl(SrsLogLevelError, tag, _srs_context->get_id(), msg, ##__VA_ARGS__)

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

