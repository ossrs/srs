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

#include <srs_app_log.hpp>

#include <stdarg.h>
#include <sys/time.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <srs_app_config.hpp>
#include <srs_kernel_error.hpp>
#include <srs_app_utility.hpp>
#include <srs_kernel_utility.hpp>

// the max size of a line of log.
#define LOG_MAX_SIZE 4096

// the tail append to each log.
#define LOG_TAIL '\n'
// reserved for the end of log data, it must be strlen(LOG_TAIL)
#define LOG_TAIL_SIZE 1

SrsFastLog::SrsFastLog()
{
    level = SrsLogLevelTrace;
    log_data = new char[LOG_MAX_SIZE];
    
    fd = -1;
    log_to_file_tank = false;
    utc = false;
}

SrsFastLog::~SrsFastLog()
{
    srs_freepa(log_data);
    
    if (fd > 0) {
        ::close(fd);
        fd = -1;
    }
    
    if (_srs_config) {
        _srs_config->unsubscribe(this);
    }
}

srs_error_t SrsFastLog::initialize()
{
    if (_srs_config) {
        _srs_config->subscribe(this);
        
        log_to_file_tank = _srs_config->get_log_tank_file();
        level = srs_get_log_level(_srs_config->get_log_level());
        utc = _srs_config->get_utc_time();
    }
    
    return srs_success;
}

void SrsFastLog::reopen()
{
    if (fd > 0) {
        ::close(fd);
    }
    
    if (!log_to_file_tank) {
        return;
    }
    
    open_log_file();
}

void SrsFastLog::verbose(const char* tag, int context_id, const char* fmt, ...)
{
    if (level > SrsLogLevelVerbose) {
        return;
    }
    
    int size = 0;
    if (!srs_log_header(log_data, LOG_MAX_SIZE, utc, false, tag, context_id, "Verb", &size)) {
        return;
    }
    
    va_list ap;
    va_start(ap, fmt);
    // we reserved 1 bytes for the new line.
    size += vsnprintf(log_data + size, LOG_MAX_SIZE - size, fmt, ap);
    va_end(ap);
    
    write_log(fd, log_data, size, SrsLogLevelVerbose);
}

void SrsFastLog::info(const char* tag, int context_id, const char* fmt, ...)
{
    if (level > SrsLogLevelInfo) {
        return;
    }
    
    int size = 0;
    if (!srs_log_header(log_data, LOG_MAX_SIZE, utc, false, tag, context_id, "Debug", &size)) {
        return;
    }
    
    va_list ap;
    va_start(ap, fmt);
    // we reserved 1 bytes for the new line.
    size += vsnprintf(log_data + size, LOG_MAX_SIZE - size, fmt, ap);
    va_end(ap);
    
    write_log(fd, log_data, size, SrsLogLevelInfo);
}

void SrsFastLog::trace(const char* tag, int context_id, const char* fmt, ...)
{
    if (level > SrsLogLevelTrace) {
        return;
    }
    
    int size = 0;
    if (!srs_log_header(log_data, LOG_MAX_SIZE, utc, false, tag, context_id, "Trace", &size)) {
        return;
    }
    
    va_list ap;
    va_start(ap, fmt);
    // we reserved 1 bytes for the new line.
    size += vsnprintf(log_data + size, LOG_MAX_SIZE - size, fmt, ap);
    va_end(ap);
    
    write_log(fd, log_data, size, SrsLogLevelTrace);
}

void SrsFastLog::warn(const char* tag, int context_id, const char* fmt, ...)
{
    if (level > SrsLogLevelWarn) {
        return;
    }
    
    int size = 0;
    if (!srs_log_header(log_data, LOG_MAX_SIZE, utc, true, tag, context_id, "Warn", &size)) {
        return;
    }
    
    va_list ap;
    va_start(ap, fmt);
    // we reserved 1 bytes for the new line.
    size += vsnprintf(log_data + size, LOG_MAX_SIZE - size, fmt, ap);
    va_end(ap);
    
    write_log(fd, log_data, size, SrsLogLevelWarn);
}

void SrsFastLog::error(const char* tag, int context_id, const char* fmt, ...)
{
    if (level > SrsLogLevelError) {
        return;
    }
    
    int size = 0;
    if (!srs_log_header(log_data, LOG_MAX_SIZE, utc, true, tag, context_id, "Error", &size)) {
        return;
    }
    
    va_list ap;
    va_start(ap, fmt);
    // we reserved 1 bytes for the new line.
    size += vsnprintf(log_data + size, LOG_MAX_SIZE - size, fmt, ap);
    va_end(ap);
    
    // add strerror() to error msg.
    if (errno != 0) {
        size += snprintf(log_data + size, LOG_MAX_SIZE - size, "(%s)", strerror(errno));
    }
    
    write_log(fd, log_data, size, SrsLogLevelError);
}

int SrsFastLog::on_reload_utc_time()
{
    utc = _srs_config->get_utc_time();
    
    return ERROR_SUCCESS;
}

int SrsFastLog::on_reload_log_tank()
{
    int ret = ERROR_SUCCESS;
    
    if (!_srs_config) {
        return ret;
    }
    
    bool tank = log_to_file_tank;
    log_to_file_tank = _srs_config->get_log_tank_file();
    
    if (tank) {
        return ret;
    }
    
    if (!log_to_file_tank) {
        return ret;
    }
    
    if (fd > 0) {
        ::close(fd);
    }
    open_log_file();
    
    return ret;
}

int SrsFastLog::on_reload_log_level()
{
    int ret = ERROR_SUCCESS;
    
    if (!_srs_config) {
        return ret;
    }
    
    level = srs_get_log_level(_srs_config->get_log_level());
    
    return ret;
}

int SrsFastLog::on_reload_log_file()
{
    int ret = ERROR_SUCCESS;
    
    if (!_srs_config) {
        return ret;
    }
    
    if (!log_to_file_tank) {
        return ret;
    }
    
    if (fd > 0) {
        ::close(fd);
    }
    open_log_file();
    
    return ret;
}

void SrsFastLog::write_log(int& fd, char *str_log, int size, int level)
{
    // ensure the tail and EOF of string
    //      LOG_TAIL_SIZE for the TAIL char.
    //      1 for the last char(0).
    size = srs_min(LOG_MAX_SIZE - 1 - LOG_TAIL_SIZE, size);
    
    // add some to the end of char.
    str_log[size++] = LOG_TAIL;
    
    // if not to file, to console and return.
    if (!log_to_file_tank) {
        // if is error msg, then print color msg.
        // \033[31m : red text code in shell
        // \033[32m : green text code in shell
        // \033[33m : yellow text code in shell
        // \033[0m : normal text code
        if (level <= SrsLogLevelTrace) {
            printf("%.*s", size, str_log);
        } else if (level == SrsLogLevelWarn) {
            printf("\033[33m%.*s\033[0m", size, str_log);
        } else{
            printf("\033[31m%.*s\033[0m", size, str_log);
        }
        fflush(stdout);
        
        return;
    }
    
    // open log file. if specified
    if (fd < 0) {
        open_log_file();
    }
    
    // write log to file.
    if (fd > 0) {
        ::write(fd, str_log, size);
    }
}

void SrsFastLog::open_log_file()
{
    if (!_srs_config) {
        return;
    }
    
    std::string filename = _srs_config->get_log_file();
    
    if (filename.empty()) {
        return;
    }
    
    fd = ::open(filename.c_str(), O_RDWR | O_APPEND);
    
    if(fd == -1 && errno == ENOENT) {
        fd = open(filename.c_str(), O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH);
    }
}

