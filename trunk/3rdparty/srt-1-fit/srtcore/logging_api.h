/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2018 Haivision Systems Inc.
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 * 
 */

/*****************************************************************************
written by
   Haivision Systems Inc.
 *****************************************************************************/

#ifndef INC_SRT_LOGGING_API_H
#define INC_SRT_LOGGING_API_H

// These are required for access functions:
// - adding FA (requires set)
// - setting a log stream (requires iostream)
#ifdef __cplusplus
#include <set>
#include <iostream>
#endif

#ifdef _WIN32
#include "win/syslog_defs.h"
#else
#include <syslog.h>
#endif

// Syslog is included so that it provides log level names.
// Haivision log standard requires the same names plus extra one:
#ifndef LOG_DEBUG_TRACE
#define LOG_DEBUG_TRACE 8
#endif
// It's unused anyway, just for the record.
#define SRT_LOG_LEVEL_MIN LOG_CRIT
#define SRT_LOG_LEVEL_MAX LOG_DEBUG

// Flags
#define SRT_LOGF_DISABLE_TIME 1
#define SRT_LOGF_DISABLE_THREADNAME 2
#define SRT_LOGF_DISABLE_SEVERITY 4
#define SRT_LOGF_DISABLE_EOL 8

// Handler type.
typedef void SRT_LOG_HANDLER_FN(void* opaque, int level, const char* file, int line, const char* area, const char* message);

#ifdef __cplusplus
namespace srt_logging
{


struct LogFA
{
private:
    int value;
public:
    operator int() const { return value; }

    LogFA(int v): value(v)
    {
        // Generally this was what it has to be used for.
        // Unfortunately it couldn't be agreed with the
        //logging_fa_all.insert(v);
    }
};

const LogFA LOGFA_GENERAL = 0;



namespace LogLevel
{
    // There are 3 general levels:

    // A. fatal - this means the application WILL crash.
    // B. unexpected:
    //    - error: this was unexpected for the library
    //    - warning: this was expected by the library, but may be harmful for the application
    // C. expected:
    //    - note: a significant, but rarely occurring event
    //    - debug: may occur even very often and enabling it can harm performance

    enum type
    {
        fatal = LOG_CRIT,
        // Fatal vs. Error: with Error, you can still continue.
        error = LOG_ERR,
        // Error vs. Warning: Warning isn't considered a problem for the library.
        warning = LOG_WARNING,
        // Warning vs. Note: Note means something unusual, but completely correct behavior.
        note = LOG_NOTICE,
        // Note vs. Debug: Debug may occur even multiple times in a millisecond.
        // (Well, worth noting that Error and Warning potentially also can).
        debug = LOG_DEBUG
    };
}

class Logger;

}
#endif

#endif
