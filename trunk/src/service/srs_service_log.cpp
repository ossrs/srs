/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2017 OSSRS(winlin)
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
using namespace std;

#include <srs_kernel_error.hpp>
#include <srs_kernel_utility.hpp>

#define SRS_BASIC_LOG_SIZE 1024

SrsThreadContext::SrsThreadContext()
{
}

SrsThreadContext::~SrsThreadContext()
{
}

int SrsThreadContext::generate_id()
{
    static int id = 100;
    
    int gid = id++;
    cache[st_thread_self()] = gid;
    return gid;
}

int SrsThreadContext::get_id()
{
    return cache[st_thread_self()];
}

int SrsThreadContext::set_id(int v)
{
    st_thread_t self = st_thread_self();
    
    int ov = 0;
    if (cache.find(self) != cache.end()) {
        ov = cache[self];
    }
    
    cache[self] = v;
    
    return ov;
}

void SrsThreadContext::clear_cid()
{
    st_thread_t self = st_thread_self();
    std::map<st_thread_t, int>::iterator it = cache.find(self);
    if (it != cache.end()) {
        cache.erase(it);
    }
}

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

int SrsConsoleLog::initialize()
{
    return ERROR_SUCCESS;
}

void SrsConsoleLog::reopen()
{
}

void SrsConsoleLog::verbose(const char* tag, int context_id, const char* fmt, ...)
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
    
    fprintf(stdout, "%s", buffer);
}

void SrsConsoleLog::info(const char* tag, int context_id, const char* fmt, ...)
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
    
    fprintf(stdout, "%s", buffer);
}

void SrsConsoleLog::trace(const char* tag, int context_id, const char* fmt, ...)
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
    
    fprintf(stdout, "%s", buffer);
}

void SrsConsoleLog::warn(const char* tag, int context_id, const char* fmt, ...)
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
    
    fprintf(stderr, "%s", buffer);
}

void SrsConsoleLog::error(const char* tag, int context_id, const char* fmt, ...)
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
    
    fprintf(stderr, "%s", buffer);
}

bool srs_log_header(char* buffer, int size, bool utc, bool dangerous, const char* tag, int cid, const char* level, int* psize)
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
    
    int nb_header = -1;
    if (dangerous) {
        if (tag) {
            nb_header = snprintf(buffer, size,
                "[%d-%02d-%02d %02d:%02d:%02d.%03d][%s][%s][%d][%d][%d] ",
                1900 + tm->tm_year, 1 + tm->tm_mon, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec, (int)(tv.tv_usec / 1000),
                level, tag, getpid(), cid, errno);
        } else {
            nb_header = snprintf(buffer, size,
                "[%d-%02d-%02d %02d:%02d:%02d.%03d][%s][%d][%d][%d] ",
                1900 + tm->tm_year, 1 + tm->tm_mon, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec, (int)(tv.tv_usec / 1000),
                level, getpid(), cid, errno);
        }
    } else {
        if (tag) {
            nb_header = snprintf(buffer, size,
                "[%d-%02d-%02d %02d:%02d:%02d.%03d][%s][%s][%d][%d] ",
                1900 + tm->tm_year, 1 + tm->tm_mon, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec, (int)(tv.tv_usec / 1000),
                level, tag, getpid(), cid);
        } else {
            nb_header = snprintf(buffer, size,
                "[%d-%02d-%02d %02d:%02d:%02d.%03d][%s][%d][%d] ",
                1900 + tm->tm_year, 1 + tm->tm_mon, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec, (int)(tv.tv_usec / 1000),
                level, getpid(), cid);
        }
    }
    
    if (nb_header == -1) {
        return false;
    }
    
    // write the header size.
    *psize = srs_min(size - 1, nb_header);
    
    return true;
}

