/*
The MIT License (MIT)

Copyright (c) 2013-2014 winlin

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include <srs_app_log.hpp>

#include <stdarg.h>
#include <sys/time.h>

SrsThreadContext::SrsThreadContext()
{
}

SrsThreadContext::~SrsThreadContext()
{
}

void SrsThreadContext::generate_id()
{
	static int id = 1;
    cache[st_thread_self()] = id++;
}

int SrsThreadContext::get_id()
{
    return cache[st_thread_self()];
}

// the max size of a line of log.
#define LOG_MAX_SIZE 4096

// the tail append to each log.
#define LOG_TAIL '\n'
// reserved for the end of log data, it must be strlen(LOG_TAIL)
#define LOG_TAIL_SIZE 1

SrsFastLog::SrsFastLog()
{
	level = SrsLogLevel::Trace;
    log_data = new char[LOG_MAX_SIZE];
}

SrsFastLog::~SrsFastLog()
{
    srs_freepa(log_data);
}

void SrsFastLog::verbose(const char* tag, int context_id, const char* fmt, ...)
{
    if (level > SrsLogLevel::Verbose) {
        return;
    }
    
    int size = 0;
    if (!generate_header(tag, context_id, "verb", &size)) {
        return;
    }
    
    va_list ap;
    va_start(ap, fmt);
    // we reserved 1 bytes for the new line.
    size += vsnprintf(log_data + size, LOG_MAX_SIZE - size, fmt, ap);
    va_end(ap);

    write_log(log_data, size, SrsLogLevel::Verbose);
}

void SrsFastLog::info(const char* tag, int context_id, const char* fmt, ...)
{
    if (level > SrsLogLevel::Info) {
        return;
    }
    
    int size = 0;
    if (!generate_header(tag, context_id, "debug", &size)) {
        return;
    }
    
    va_list ap;
    va_start(ap, fmt);
    // we reserved 1 bytes for the new line.
    size += vsnprintf(log_data + size, LOG_MAX_SIZE - size, fmt, ap);
    va_end(ap);

    write_log(log_data, size, SrsLogLevel::Info);
}

void SrsFastLog::trace(const char* tag, int context_id, const char* fmt, ...)
{
    if (level > SrsLogLevel::Trace) {
        return;
    }
    
    int size = 0;
    if (!generate_header(tag, context_id, "trace", &size)) {
        return;
    }
    
    va_list ap;
    va_start(ap, fmt);
    // we reserved 1 bytes for the new line.
    size += vsnprintf(log_data + size, LOG_MAX_SIZE - size, fmt, ap);
    va_end(ap);

    write_log(log_data, size, SrsLogLevel::Trace);
}

void SrsFastLog::warn(const char* tag, int context_id, const char* fmt, ...)
{
    if (level > SrsLogLevel::Warn) {
        return;
    }
    
    int size = 0;
    if (!generate_header(tag, context_id, "warn", &size)) {
        return;
    }
    
    va_list ap;
    va_start(ap, fmt);
    // we reserved 1 bytes for the new line.
    size += vsnprintf(log_data + size, LOG_MAX_SIZE - size, fmt, ap);
    va_end(ap);

    write_log(log_data, size, SrsLogLevel::Warn);
}

void SrsFastLog::error(const char* tag, int context_id, const char* fmt, ...)
{
    if (level > SrsLogLevel::Error) {
        return;
    }
    
    int size = 0;
    if (!generate_header(tag, context_id, "error", &size)) {
        return;
    }
    
    va_list ap;
    va_start(ap, fmt);
    // we reserved 1 bytes for the new line.
    size += vsnprintf(log_data + size, LOG_MAX_SIZE - size, fmt, ap);
    va_end(ap);

    // add strerror() to error msg.
    size += snprintf(log_data + size, LOG_MAX_SIZE - size, "(%s)", strerror(errno));

    write_log(log_data, size, SrsLogLevel::Error);
}

bool SrsFastLog::generate_header(const char* tag, int context_id, const char* level_name, int* header_size)
{
    // clock time
    timeval tv;
    if (gettimeofday(&tv, NULL) == -1) {
        return false;
    }
    
    // to calendar time
    struct tm* tm;
    if ((tm = localtime(&tv.tv_sec)) == NULL) {
        return false;
    }
    
    // write log header
    int log_header_size = -1;
    
    if (tag) {
	    log_header_size = snprintf(log_data, LOG_MAX_SIZE, 
	        "[%d-%02d-%02d %02d:%02d:%02d.%03d][%s][%s][%d][%d] ", 
	        1900 + tm->tm_year, 1 + tm->tm_mon, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec, (int)(tv.tv_usec / 1000), 
	        level_name, tag, context_id, errno);
    } else {
	    log_header_size = snprintf(log_data, LOG_MAX_SIZE, 
	        "[%d-%02d-%02d %02d:%02d:%02d.%03d][%s][%d][%d] ", 
	        1900 + tm->tm_year, 1 + tm->tm_mon, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec, (int)(tv.tv_usec / 1000), 
	        level_name, context_id, errno);
    }

    if (log_header_size == -1) {
        return false;
    }
    
    // write the header size.
    *header_size = srs_min(LOG_MAX_SIZE - 1, log_header_size);
    
    return true;
}

void SrsFastLog::write_log(char *str_log, int size, int _level)
{
    // ensure the tail and EOF of string
    //      LOG_TAIL_SIZE for the TAIL char.
    //      1 for the last char(0).
    size = srs_min(LOG_MAX_SIZE - 1 - LOG_TAIL_SIZE, size);
    
    // add some to the end of char.
    log_data[size++] = LOG_TAIL;
    log_data[size++] = 0;
    
    // if is error msg, then print color msg.
    // \033[1;31m : red text code in shell
    // \033[1;31m : normal text code
    if (_level == SrsLogLevel::Error) {
        printf("\033[1;31m%s\033[0m", str_log);
    } else {
        printf("%s", str_log);
    }
}
