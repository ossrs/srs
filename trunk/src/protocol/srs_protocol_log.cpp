//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#include <srs_protocol_log.hpp>

#include <stdarg.h>
#include <sys/time.h>
#include <unistd.h>
#include <sstream>
using namespace std;

#include <srs_kernel_error.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_utility.hpp>

#include <srs_protocol_kbps.hpp>

SrsPps* _srs_pps_cids_get = NULL;
SrsPps* _srs_pps_cids_set = NULL;

#define SRS_BASIC_LOG_SIZE 8192

SrsThreadContext::SrsThreadContext()
{
}

SrsThreadContext::~SrsThreadContext()
{
}

SrsContextId SrsThreadContext::generate_id()
{
    SrsContextId cid;
    return cid.set_value(srs_random_str(8));
}

static SrsContextId _srs_context_default;
static int _srs_context_key = -1;
void _srs_context_destructor(void* arg)
{
    SrsContextId* cid = (SrsContextId*)arg;
    srs_freep(cid);
}

const SrsContextId& SrsThreadContext::get_id()
{
    ++_srs_pps_cids_get->sugar;

    if (!srs_thread_self()) {
        return _srs_context_default;
    }

    void* cid = srs_thread_getspecific(_srs_context_key);
    if (!cid) {
        return _srs_context_default;
    }

    return *(SrsContextId*)cid;
}

const SrsContextId& SrsThreadContext::set_id(const SrsContextId& v)
{
    return srs_context_set_cid_of(srs_thread_self(), v);
}

void SrsThreadContext::clear_cid()
{
}

const SrsContextId& srs_context_set_cid_of(srs_thread_t trd, const SrsContextId& v)
{
    ++_srs_pps_cids_set->sugar;

    if (!trd) {
        _srs_context_default = v;
        return v;
    }

    SrsContextId* cid = new SrsContextId();
    *cid = v;

    if (_srs_context_key < 0) {
        int r0 = srs_key_create(&_srs_context_key, _srs_context_destructor);
        srs_assert(r0 == 0);
    }

    int r0 = srs_thread_setspecific2(trd, _srs_context_key, cid);
    srs_assert(r0 == 0);

    return v;
}

impl_SrsContextRestore::impl_SrsContextRestore(SrsContextId cid)
{
    cid_ = cid;
}

impl_SrsContextRestore::~impl_SrsContextRestore()
{
    _srs_context->set_id(cid_);
}

// LCOV_EXCL_START
SrsConsoleLog::SrsConsoleLog(SrsLogLevel l, bool u)
{
    level_ = l;
    utc = u;
    
    buffer = new char[SRS_BASIC_LOG_SIZE];
}

SrsConsoleLog::~SrsConsoleLog()
{
    srs_freepa(buffer);
}

srs_error_t SrsConsoleLog::initialize()
{
    return srs_success;
}

void SrsConsoleLog::reopen()
{
}

void SrsConsoleLog::log(SrsLogLevel level, const char* tag, const SrsContextId& context_id, const char* fmt, va_list args)
{
    if (level < level_ || level >= SrsLogLevelDisabled) {
        return;
    }
    
    int size = 0;
    if (!srs_log_header(buffer, SRS_BASIC_LOG_SIZE, utc, level >= SrsLogLevelWarn, tag, context_id, srs_log_level_strings[level], &size)) {
        return;
    }

    // Something not expected, drop the log.
    int r0 = vsnprintf(buffer + size, SRS_BASIC_LOG_SIZE - size, fmt, args);
    if (r0 <= 0 || r0 >= SRS_BASIC_LOG_SIZE - size) {
        return;
    }
    size += r0;

    // Add errno and strerror() if error.
    if (level == SrsLogLevelError && errno != 0) {
        r0 = snprintf(buffer + size, SRS_BASIC_LOG_SIZE - size, "(%s)", strerror(errno));

        // Something not expected, drop the log.
        if (r0 <= 0 || r0 >= SRS_BASIC_LOG_SIZE - size) {
            return;
        }
        size += r0;
    }

    if (level >= SrsLogLevelWarn) {
        fprintf(stderr, "%s\n", buffer);
    } else {
        fprintf(stdout, "%s\n", buffer);
    }
}

bool srs_log_header(char* buffer, int size, bool utc, bool dangerous, const char* tag, SrsContextId cid, const char* level, int* psize)
{
    // clock time
    timeval tv;
    if (gettimeofday(&tv, NULL) == -1) {
        return false;
    }
    
    // to calendar time
    struct tm now;
    // Each of these functions returns NULL in case an error was detected. @see https://linux.die.net/man/3/localtime_r
    if (utc) {
        if (gmtime_r(&tv.tv_sec, &now) == NULL) {
            return false;
        }
    } else {
        if (localtime_r(&tv.tv_sec, &now) == NULL) {
            return false;
        }
    }
    
    int written = -1;
    if (dangerous) {
        if (tag) {
            written = snprintf(buffer, size,
                "[%d-%02d-%02d %02d:%02d:%02d.%03d][%s][%d][%s][%d][%s] ",
                1900 + now.tm_year, 1 + now.tm_mon, now.tm_mday, now.tm_hour, now.tm_min, now.tm_sec, (int)(tv.tv_usec / 1000),
                level, getpid(), cid.c_str(), errno, tag);
        } else {
            written = snprintf(buffer, size,
                "[%d-%02d-%02d %02d:%02d:%02d.%03d][%s][%d][%s][%d] ",
                1900 + now.tm_year, 1 + now.tm_mon, now.tm_mday, now.tm_hour, now.tm_min, now.tm_sec, (int)(tv.tv_usec / 1000),
                level, getpid(), cid.c_str(), errno);
        }
    } else {
        if (tag) {
            written = snprintf(buffer, size,
                "[%d-%02d-%02d %02d:%02d:%02d.%03d][%s][%d][%s][%s] ",
                1900 + now.tm_year, 1 + now.tm_mon, now.tm_mday, now.tm_hour, now.tm_min, now.tm_sec, (int)(tv.tv_usec / 1000),
                level, getpid(), cid.c_str(), tag);
        } else {
            written = snprintf(buffer, size,
                "[%d-%02d-%02d %02d:%02d:%02d.%03d][%s][%d][%s] ",
                1900 + now.tm_year, 1 + now.tm_mon, now.tm_mday, now.tm_hour, now.tm_min, now.tm_sec, (int)(tv.tv_usec / 1000),
                level, getpid(), cid.c_str());
        }
    }

    // Exceed the size, ignore this log.
    // Check size to avoid security issue https://github.com/ossrs/srs/issues/1229
    if (written <= 0 || written >= size) {
        return false;
    }
    
    // write the header size.
    *psize = written;
    
    return true;
}

