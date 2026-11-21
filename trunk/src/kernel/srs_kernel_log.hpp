//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_KERNEL_LOG_HPP
#define SRS_KERNEL_LOG_HPP

#include <srs_core.hpp>

#include <stdio.h>

#include <errno.h>
#include <stdarg.h>
#include <string.h>
#include <string>

#include <srs_kernel_consts.hpp>

#define TAG_MAIN "MAIN"
#define TAG_MAYBE "MAYBE"
#define TAG_DTLS_ALERT "DTLS_ALERT"
#define TAG_DTLS_HANG "DTLS_HANG"
#define TAG_RESOURCE_UNSUB "RESOURCE_UNSUB"
#define TAG_LARGE_TIMER "LARGE_TIMER"

// See https://github.com/apache/logging-log4j2/blob/release-2.x/log4j-api/src/main/java/org/apache/logging/log4j/Level.java
// The enum names do not always match the level strings, to stay compatible with older versions.
enum SrsLogLevel {
    SrsLogLevelForbidden = 0x00,

    // Usually compiled out, for performance.
    SrsLogLevelVerbose = 0x01,
    SrsLogLevelInfo = 0x02,
    SrsLogLevelTrace = 0x04,
    SrsLogLevelWarn = 0x08,
    SrsLogLevelError = 0x10,

    SrsLogLevelDisabled = 0x20,
};

extern const char *srs_log_level_strings[];

class ISrsLog
{
public:
    ISrsLog();
    virtual ~ISrsLog();

public:
    virtual srs_error_t initialize() = 0;
    // For log rotation.
    virtual void reopen() = 0;

public:
    // The tag may be NULL.
    virtual void log(SrsLogLevel level, const char *tag, const SrsContextId &context_id, const char *fmt, va_list args) = 0;
};

// The context ID of a logic unit, such as an RTMP connection or an RTC session, to grep its logs for debugging.
class ISrsContext
{
public:
    ISrsContext();
    virtual ~ISrsContext();

public:
    // Does not set the ID of the current thread; call set_id for that.
    virtual SrsContextId generate_id() = 0;
    // Of the current thread.
    virtual const SrsContextId &get_id() = 0;
    // Of the current thread. Returns the new ID, not the old one.
    virtual const SrsContextId &set_id(const SrsContextId &v) = 0;
};

#define SrsContextRestore(cid) impl_SrsContextRestore _context_restore_instance(cid)
class impl_SrsContextRestore
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    SrsContextId cid_;

public:
    impl_SrsContextRestore(SrsContextId cid);
    virtual ~impl_SrsContextRestore();
};

extern ISrsContext *_srs_context;
extern ISrsLog *_srs_log;

extern void srs_logger_impl(SrsLogLevel level, const char *tag, const SrsContextId &context_id, const char *fmt, ...);

#define srs_verbose(msg, ...) srs_logger_impl(SrsLogLevelVerbose, NULL, _srs_context->get_id(), msg, ##__VA_ARGS__)
#define srs_info(msg, ...) srs_logger_impl(SrsLogLevelInfo, NULL, _srs_context->get_id(), msg, ##__VA_ARGS__)
#define srs_trace(msg, ...) srs_logger_impl(SrsLogLevelTrace, NULL, _srs_context->get_id(), msg, ##__VA_ARGS__)
#define srs_warn(msg, ...) srs_logger_impl(SrsLogLevelWarn, NULL, _srs_context->get_id(), msg, ##__VA_ARGS__)
#define srs_error(msg, ...) srs_logger_impl(SrsLogLevelError, NULL, _srs_context->get_id(), msg, ##__VA_ARGS__)
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
