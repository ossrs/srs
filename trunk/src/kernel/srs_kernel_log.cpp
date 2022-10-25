//
// Copyright (c) 2013-2022 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#include <srs_kernel_log.hpp>

#include <stdarg.h>

const char* srs_log_level_strings[] = {
        "Forbidden",
        "Verb",
        "Debug", NULL,
        "Trace", NULL, NULL, NULL,
        "Warn", NULL, NULL, NULL, NULL, NULL, NULL, NULL,
        "Error", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
        "Disabled",
};

ISrsLog::ISrsLog()
{
}

ISrsLog::~ISrsLog()
{
}

ISrsContext::ISrsContext()
{
}

ISrsContext::~ISrsContext()
{
}

void srs_logger_impl(SrsLogLevel level, const char* tag, const SrsContextId& context_id, const char* fmt, ...)
{
    if (!_srs_log) return;

    va_list args;
    va_start(args, fmt);
    _srs_log->log(level, tag, context_id, fmt, args);
    va_end(args);
}



