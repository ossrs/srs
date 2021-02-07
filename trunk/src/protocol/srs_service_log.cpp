/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2020 Winlin
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

#include <srs_service_log.hpp>

#include <stdarg.h>
#include <sys/time.h>
#include <unistd.h>
#include <sstream>
using namespace std;

#include <srs_kernel_error.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_utility.hpp>

#include <srs_protocol_kbps.hpp>

SrsPps* _srs_pps_cids_get = new SrsPps(_srs_clock);
SrsPps* _srs_pps_cids_set = new SrsPps(_srs_clock);

#define SRS_BASIC_LOG_SIZE 8192

SrsThreadContext::SrsThreadContext()
{
}

SrsThreadContext::~SrsThreadContext()
{
}

SrsContextId SrsThreadContext::generate_id()
{
    SrsContextId cid = SrsContextId();
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
    ++_srs_pps_cids_set->sugar;

    if (!srs_thread_self()) {
        _srs_context_default = v;
        return v;
    }

    SrsContextId* cid = new SrsContextId();
    *cid = v;

    if (_srs_context_key < 0) {
        int r0 = srs_key_create(&_srs_context_key, _srs_context_destructor);
        srs_assert(r0 == 0);
    }

    int r0 = srs_thread_setspecific(_srs_context_key, cid);
    srs_assert(r0 == 0);

    return v;
}

void SrsThreadContext::clear_cid()
{
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
    level = l;
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

void SrsConsoleLog::verbose(const char* tag, SrsContextId context_id, const char* fmt, ...)
{
    if (level > SrsLogLevelVerbose) {
        return;
    }
    
    int size = 0;
    if (!srs_log_header(buffer, SRS_BASIC_LOG_SIZE, utc, false, tag, context_id, "Verb", &size)) {
        return;
    }
    
    va_list ap;
    va_start(ap, fmt);
    // we reserved 1 bytes for the new line.
    size += vsnprintf(buffer + size, SRS_BASIC_LOG_SIZE - size, fmt, ap);
    va_end(ap);
    
    fprintf(stdout, "%s\n", buffer);
}

void SrsConsoleLog::info(const char* tag, SrsContextId context_id, const char* fmt, ...)
{
    if (level > SrsLogLevelInfo) {
        return;
    }
    
    int size = 0;
    if (!srs_log_header(buffer, SRS_BASIC_LOG_SIZE, utc, false, tag, context_id, "Debug", &size)) {
        return;
    }
    
    va_list ap;
    va_start(ap, fmt);
    // we reserved 1 bytes for the new line.
    size += vsnprintf(buffer + size, SRS_BASIC_LOG_SIZE - size, fmt, ap);
    va_end(ap);
    
    fprintf(stdout, "%s\n", buffer);
}

void SrsConsoleLog::trace(const char* tag, SrsContextId context_id, const char* fmt, ...)
{
    if (level > SrsLogLevelTrace) {
        return;
    }
    
    int size = 0;
    if (!srs_log_header(buffer, SRS_BASIC_LOG_SIZE, utc, false, tag, context_id, "Trace", &size)) {
        return;
    }
    
    va_list ap;
    va_start(ap, fmt);
    // we reserved 1 bytes for the new line.
    size += vsnprintf(buffer + size, SRS_BASIC_LOG_SIZE - size, fmt, ap);
    va_end(ap);
    
    fprintf(stdout, "%s\n", buffer);
}

void SrsConsoleLog::warn(const char* tag, SrsContextId context_id, const char* fmt, ...)
{
    if (level > SrsLogLevelWarn) {
        return;
    }
    
    int size = 0;
    if (!srs_log_header(buffer, SRS_BASIC_LOG_SIZE, utc, true, tag, context_id, "Warn", &size)) {
        return;
    }
    
    va_list ap;
    va_start(ap, fmt);
    // we reserved 1 bytes for the new line.
    size += vsnprintf(buffer + size, SRS_BASIC_LOG_SIZE - size, fmt, ap);
    va_end(ap);
    
    fprintf(stderr, "%s\n", buffer);
}

void SrsConsoleLog::error(const char* tag, SrsContextId context_id, const char* fmt, ...)
{
    if (level > SrsLogLevelError) {
        return;
    }
    
    int size = 0;
    if (!srs_log_header(buffer, SRS_BASIC_LOG_SIZE, utc, true, tag, context_id, "Error", &size)) {
        return;
    }
    
    va_list ap;
    va_start(ap, fmt);
    // we reserved 1 bytes for the new line.
    size += vsnprintf(buffer + size, SRS_BASIC_LOG_SIZE - size, fmt, ap);
    va_end(ap);
    
    // add strerror() to error msg.
    if (errno != 0) {
        size += snprintf(buffer + size, SRS_BASIC_LOG_SIZE - size, "(%s)", strerror(errno));
    }
    
    fprintf(stderr, "%s\n", buffer);
}
// LCOV_EXCL_STOP

bool srs_log_header(char* buffer, int size, bool utc, bool dangerous, const char* tag, SrsContextId cid, const char* level, int* psize)
{
    // clock time
    timeval tv;
    if (gettimeofday(&tv, NULL) == -1) {
        return false;
    }
    
    // to calendar time
    struct tm* tm;
    if (utc) {
        if ((tm = gmtime(&tv.tv_sec)) == NULL) {
            return false;
        }
    } else {
        if ((tm = localtime(&tv.tv_sec)) == NULL) {
            return false;
        }
    }
    
    int written = -1;
    if (dangerous) {
        if (tag) {
            written = snprintf(buffer, size,
                "[%d-%02d-%02d %02d:%02d:%02d.%03d][%s][%d][%s][%d][%s] ",
                1900 + tm->tm_year, 1 + tm->tm_mon, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec, (int)(tv.tv_usec / 1000),
                level, getpid(), cid.c_str(), errno, tag);
        } else {
            written = snprintf(buffer, size,
                "[%d-%02d-%02d %02d:%02d:%02d.%03d][%s][%d][%s][%d] ",
                1900 + tm->tm_year, 1 + tm->tm_mon, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec, (int)(tv.tv_usec / 1000),
                level, getpid(), cid.c_str(), errno);
        }
    } else {
        if (tag) {
            written = snprintf(buffer, size,
                "[%d-%02d-%02d %02d:%02d:%02d.%03d][%s][%d][%s][%s] ",
                1900 + tm->tm_year, 1 + tm->tm_mon, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec, (int)(tv.tv_usec / 1000),
                level, getpid(), cid.c_str(), tag);
        } else {
            written = snprintf(buffer, size,
                "[%d-%02d-%02d %02d:%02d:%02d.%03d][%s][%d][%s] ",
                1900 + tm->tm_year, 1 + tm->tm_mon, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec, (int)(tv.tv_usec / 1000),
                level, getpid(), cid.c_str());
        }
    }

    // Exceed the size, ignore this log.
    // Check size to avoid security issue https://github.com/ossrs/srs/issues/1229
    if (written >= size) {
        return false;
    }
    
    if (written == -1) {
        return false;
    }
    
    // write the header size.
    *psize = written;
    
    return true;
}

