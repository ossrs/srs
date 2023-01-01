//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

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
#include <srs_app_threads.hpp>

// the max size of a line of log.
#define LOG_MAX_SIZE 8192

// the tail append to each log.
#define LOG_TAIL '\n'
// reserved for the end of log data, it must be strlen(LOG_TAIL)
#define LOG_TAIL_SIZE 1

SrsFileLog::SrsFileLog()
{
    level_ = SrsLogLevelTrace;
    log_data = new char[LOG_MAX_SIZE];
    
    fd = -1;
    log_to_file_tank = false;
    utc = false;

    mutex_ = new SrsThreadMutex();
}

SrsFileLog::~SrsFileLog()
{
    srs_freepa(log_data);
    
    if (fd > 0) {
        ::close(fd);
        fd = -1;
    }
    
    if (_srs_config) {
        _srs_config->unsubscribe(this);
    }

    srs_freep(mutex_);
}

srs_error_t SrsFileLog::initialize()
{
    if (_srs_config) {
        _srs_config->subscribe(this);
        
        log_to_file_tank = _srs_config->get_log_tank_file();
        utc = _srs_config->get_utc_time();

        std::string level = _srs_config->get_log_level();
        std::string level_v2 = _srs_config->get_log_level_v2();
        level_ = level_v2.empty() ? srs_get_log_level(level) : srs_get_log_level_v2(level_v2);
    }
    
    return srs_success;
}

void SrsFileLog::reopen()
{
    if (fd > 0) {
        ::close(fd);
    }
    
    if (!log_to_file_tank) {
        return;
    }
    
    open_log_file();
}

void SrsFileLog::log(SrsLogLevel level, const char* tag, const SrsContextId& context_id, const char* fmt, va_list args)
{
    if (level < level_ || level >= SrsLogLevelDisabled) {
        return;
    }

    SrsThreadLocker(mutex_);

    int size = 0;
    bool header_ok = srs_log_header(
        log_data, LOG_MAX_SIZE, utc, level >= SrsLogLevelWarn, tag, context_id, srs_log_level_strings[level], &size
    );
    if (!header_ok) {
        return;
    }

    // Something not expected, drop the log.
    int r0 = vsnprintf(log_data + size, LOG_MAX_SIZE - size, fmt, args);
    if (r0 <= 0 || r0 >= LOG_MAX_SIZE - size) {
        return;
    }
    size += r0;

    // Add errno and strerror() if error. Check size to avoid security issue https://github.com/ossrs/srs/issues/1229
    if (level == SrsLogLevelError && errno != 0 && size < LOG_MAX_SIZE) {
        r0 = snprintf(log_data + size, LOG_MAX_SIZE - size, "(%s)", strerror(errno));

        // Something not expected, drop the log.
        if (r0 <= 0 || r0 >= LOG_MAX_SIZE - size) {
            return;
        }
        size += r0;
    }

    write_log(fd, log_data, size, level);
}

void SrsFileLog::write_log(int& fd, char *str_log, int size, int level)
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

void SrsFileLog::open_log_file()
{
    if (!_srs_config) {
        return;
    }
    
    std::string filename = _srs_config->get_log_file();
    
    if (filename.empty()) {
        return;
    }

    fd = ::open(filename.c_str(),
        O_RDWR | O_CREAT | O_APPEND,
        S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH
    );
}

