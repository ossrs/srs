//
// Copyright (c) 2013-2021 Runner365
//
// SPDX-License-Identifier: MIT
//
 
#ifndef SRT_DATA_H
#define SRT_DATA_H

#include "srt_log.hpp"
#include <srs_core.hpp>

#include <string>
#include <memory>

#define SRT_MSG_DATA_TYPE  0x01
#define SRT_MSG_CLOSE_TYPE 0x02
#define SRT_MSG_LOG_TYPE   0x03

class SRT_DATA_MSG {
public:
    SRT_DATA_MSG(const std::string& path, unsigned int msg_type=SRT_MSG_DATA_TYPE);
    SRT_DATA_MSG(unsigned int len, const std::string& path, unsigned int msg_type=SRT_MSG_DATA_TYPE);
    SRT_DATA_MSG(unsigned char* data_p, unsigned int len, const std::string& path, unsigned int msg_type=SRT_MSG_DATA_TYPE);
    SRT_DATA_MSG(LOGGER_LEVEL log_level, const std::string& log_content);
    ~SRT_DATA_MSG();

    unsigned int msg_type();
    unsigned int data_len();
    unsigned char* get_data();
    std::string get_path();
    LOGGER_LEVEL get_log_level();
    const char* get_log_string();

    void set_msg_type(unsigned int msg_type);

private:
    unsigned int   _msg_type;
    unsigned int   _len    = 0;
    unsigned char* _data_p = nullptr;
    std::string _key_path;
    std::string _log_content;
    LOGGER_LEVEL _log_level = SRT_LOGGER_TRACE_LEVEL;
};

typedef std::shared_ptr<SRT_DATA_MSG> SRT_DATA_MSG_PTR;

#endif
