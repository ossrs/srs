/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2019 Winlin
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef SRS_KERNEL_LOG_HPP
#define SRS_KERNEL_LOG_HPP

#include <srs_core.hpp>

#include <stdio.h>

#include <errno.h>
#include <string.h>

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
    virtual srs_error_t initialize();
    // Reopen the log file for log rotate.
    virtual void reopen();
public:
    // The log for verbose, very verbose information.
    virtual void verbose(const char* tag, int context_id, const char* fmt, ...);
    // The log for debug, detail information.
    virtual void info(const char* tag, int context_id, const char* fmt, ...);
    // The log for trace, important information.
    virtual void trace(const char* tag, int context_id, const char* fmt, ...);
    // The log for warn, warn is something should take attention, but not a error.
    virtual void warn(const char* tag, int context_id, const char* fmt, ...);
    // The log for error, something error occur, do something about the error, ie. close the connection,
    // but we will donot abort the program.
    virtual void error(const char* tag, int context_id, const char* fmt, ...);
};

// The context id manager to identify context, for instance, the green-thread.
// Usage:
//      _srs_context->generate_id(); // when thread start.
//      _srs_context->get_id(); // get current generated id.
//      int old_id = _srs_context->set_id(1000); // set context id if need to merge thread context.
// The context for multiple clients.
class ISrsThreadContext
{
public:
    ISrsThreadContext();
    virtual ~ISrsThreadContext();
public:
    // Generate the id for current context.
    virtual int generate_id();
    // Get the generated id of current context.
    virtual int get_id();
    // Set the id of current context.
    // @return the previous id value; 0 if no context.
    virtual int set_id(int v);
};

// @global User must provides a log object
extern ISrsLog* _srs_log;

// @global User must implements the LogContext and define a global instance.
extern ISrsThreadContext* _srs_context;

// Log style.
// Use __FUNCTION__ to print c method
// Use __PRETTY_FUNCTION__ to print c++ class:method
#if 1
    #define srs_verbose(msg, ...) _srs_log->verbose(NULL, _srs_context->get_id(), msg, ##__VA_ARGS__)
    #define srs_info(msg, ...)    _srs_log->info(NULL, _srs_context->get_id(), msg, ##__VA_ARGS__)
    #define srs_trace(msg, ...)   _srs_log->trace(NULL, _srs_context->get_id(), msg, ##__VA_ARGS__)
    #define srs_warn(msg, ...)    _srs_log->warn(NULL, _srs_context->get_id(), msg, ##__VA_ARGS__)
    #define srs_error(msg, ...)   _srs_log->error(NULL, _srs_context->get_id(), msg, ##__VA_ARGS__)
#endif
#if 0
    #define srs_verbose(msg, ...) _srs_log->verbose(__FUNCTION__, _srs_context->get_id(), msg, ##__VA_ARGS__)
    #define srs_info(msg, ...)    _srs_log->info(__FUNCTION__, _srs_context->get_id(), msg, ##__VA_ARGS__)
    #define srs_trace(msg, ...)   _srs_log->trace(__FUNCTION__, _srs_context->get_id(), msg, ##__VA_ARGS__)
    #define srs_warn(msg, ...)    _srs_log->warn(__FUNCTION__, _srs_context->get_id(), msg, ##__VA_ARGS__)
    #define srs_error(msg, ...)   _srs_log->error(__FUNCTION__, _srs_context->get_id(), msg, ##__VA_ARGS__)
#endif
#if 0
    #define srs_verbose(msg, ...) _srs_log->verbose(__PRETTY_FUNCTION__, _srs_context->get_id(), msg, ##__VA_ARGS__)
    #define srs_info(msg, ...)    _srs_log->info(__PRETTY_FUNCTION__, _srs_context->get_id(), msg, ##__VA_ARGS__)
    #define srs_trace(msg, ...)   _srs_log->trace(__PRETTY_FUNCTION__, _srs_context->get_id(), msg, ##__VA_ARGS__)
    #define srs_warn(msg, ...)    _srs_log->warn(__PRETTY_FUNCTION__, _srs_context->get_id(), msg, ##__VA_ARGS__)
    #define srs_error(msg, ...)   _srs_log->error(__PRETTY_FUNCTION__, _srs_context->get_id(), msg, ##__VA_ARGS__)
#endif

// TODO: FIXME: Add more verbose and info logs.
#ifndef SRS_AUTO_VERBOSE
    #undef srs_verbose
    #define srs_verbose(msg, ...) (void)0
#endif
#ifndef SRS_AUTO_INFO
    #undef srs_info
    #define srs_info(msg, ...) (void)0
#endif
#ifndef SRS_AUTO_TRACE
    #undef srs_trace
    #define srs_trace(msg, ...) (void)0
#endif

#endif

