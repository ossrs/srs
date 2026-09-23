//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_app_log.hpp>

#include <stdarg.h>
#include <sys/time.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <srs_app_config.hpp>
#include <srs_app_utility.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_utility.hpp>

// the max size of a line of log.
#define LOG_MAX_SIZE 65536 // 64 KB

// the tail append to each log.
#define LOG_TAIL '\n'
// reserved for the end of log data, it must be strlen(LOG_TAIL)
#define LOG_TAIL_SIZE 1

ISrsLogWriter::ISrsLogWriter()
{
}

ISrsLogWriter::~ISrsLogWriter()
{
}

SrsLogWriter::SrsLogWriter()
{
}

SrsLogWriter::~SrsLogWriter()
{
}

int SrsLogWriter::open_file(const std::string &path)
{
    return ::open(path.c_str(),
                  O_RDWR | O_CREAT | O_APPEND,
                  S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH);
}

void SrsLogWriter::close_file(int fd)
{
    ::close(fd);
}

void SrsLogWriter::write_file(int fd, const char *str_log, int size)
{
    ::write(fd, str_log, size);
}

void SrsLogWriter::write_console(const char *color, const char *str_log, int size)
{
    if (!color || !*color) {
        printf("%.*s", size, str_log);
    } else {
        printf("%s%.*s\033[0m", color, size, str_log);
    }
    fflush(stdout);
}

SrsFileLog::SrsFileLog()
{
    level_ = SrsLogLevelTrace;
    log_data_ = new char[LOG_MAX_SIZE];

    fd_ = -1;
    log_to_file_tank_ = false;
    utc_ = false;

    // The config global does not exist yet when the logger is created, so it is captured by initialize().
    config_ = NULL;

    writer_ = new SrsLogWriter();
}

SrsFileLog::~SrsFileLog()
{
    srs_freepa(log_data_);

    if (writer_ && fd_ > 0) {
        writer_->close_file(fd_);
    }
    fd_ = -1;

    if (config_) {
        config_->unsubscribe(this);
    }

    config_ = NULL;
    srs_freep(writer_);
}

// LCOV_EXCL_START
srs_error_t SrsFileLog::initialize()
{
    // Capture the config here rather than in the constructor: the logger is one of the first objects created, before
    // the config global exists.
    config_ = _srs_config;

    if (config_) {
        config_->subscribe(this);

        log_to_file_tank_ = config_->get_log_tank_file();
        utc_ = config_->get_utc_time();

        std::string level = config_->get_log_level();
        std::string level_v2 = config_->get_log_level_v2();
        level_ = level_v2.empty() ? srs_get_log_level(level) : srs_get_log_level_v2(level_v2);
    }

    return srs_success;
}
// LCOV_EXCL_STOP

void SrsFileLog::reopen()
{
    // Clear the descriptor with the close. Every path below may leave without opening a new file, and write_log()
    // opens one only when the descriptor is negative, so a closed descriptor left here would be written to after the
    // number has been handed to another socket or file.
    if (fd_ > 0) {
        writer_->close_file(fd_);
        fd_ = -1;
    }

    if (!log_to_file_tank_) {
        return;
    }

    open_log_file();
}

void SrsFileLog::log(SrsLogLevel level, const char *tag, const SrsContextId &context_id, const char *fmt, va_list args)
{
    if (level < level_ || level >= SrsLogLevelDisabled) {
        return;
    }

    int size = 0;
    bool header_ok = srs_log_header(
        log_data_, LOG_MAX_SIZE, utc_, level >= SrsLogLevelWarn, tag, context_id, srs_log_level_strings[level], &size);
    if (!header_ok) {
        return;
    }

    // Something not expected, drop the log.
    int r0 = vsnprintf(log_data_ + size, LOG_MAX_SIZE - size, fmt, args);
    if (r0 <= 0 || r0 >= LOG_MAX_SIZE - size) {
        return;
    }
    size += r0;

    // Add errno and strerror() if error. Check size to avoid security issue https://github.com/ossrs/srs/issues/1229
    if (level == SrsLogLevelError && errno != 0 && size < LOG_MAX_SIZE) {
        r0 = snprintf(log_data_ + size, LOG_MAX_SIZE - size, "(%s)", strerror(errno));

        // Something not expected, drop the log.
        if (r0 <= 0 || r0 >= LOG_MAX_SIZE - size) {
            return;
        }
        size += r0;
    }

    write_log(fd_, log_data_, size, level);
}

void SrsFileLog::write_log(int &fd, char *str_log, int size, int level)
{
    // ensure the tail and EOF of string
    //      LOG_TAIL_SIZE for the TAIL char.
    //      1 for the last char(0).
    size = srs_min(LOG_MAX_SIZE - 1 - LOG_TAIL_SIZE, size);

    // add some to the end of char.
    str_log[size++] = LOG_TAIL;

    // if not to file, to console and return.
    if (!log_to_file_tank_) {
        // if is error msg, then print color msg.
        // \033[31m : red text code in shell
        // \033[32m : green text code in shell
        // \033[33m : yellow text code in shell
        // \033[0m : normal text code
        if (level <= SrsLogLevelTrace) {
            writer_->write_console("", str_log, size);
        } else if (level == SrsLogLevelWarn) {
            writer_->write_console("\033[33m", str_log, size);
        } else {
            writer_->write_console("\033[31m", str_log, size);
        }

        return;
    }

    // open log file. if specified
    if (fd < 0) {
        open_log_file();
    }

    // write log to file.
    if (fd > 0) {
        writer_->write_file(fd, str_log, size);
    }
}

void SrsFileLog::open_log_file()
{
    if (!config_) {
        return;
    }

    std::string filename = config_->get_log_file();

    if (filename.empty()) {
        return;
    }

    fd_ = writer_->open_file(filename);
}
