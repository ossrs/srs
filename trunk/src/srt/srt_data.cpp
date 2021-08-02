//
// Copyright (c) 2013-2021 Runner365
//
// SPDX-License-Identifier: MIT
//

#include "srt_data.hpp"
#include <string.h>

SRT_DATA_MSG::SRT_DATA_MSG(const std::string& path, unsigned int msg_type):_msg_type(msg_type)
    ,_len(0)
    ,_data_p(nullptr)
    ,_key_path(path) {

}

SRT_DATA_MSG::SRT_DATA_MSG(unsigned int len, const std::string& path, unsigned int msg_type):_msg_type(msg_type)
    ,_len(len)
    ,_key_path(path) {
    _data_p = new unsigned char[len];
    memset(_data_p, 0, len);
}

SRT_DATA_MSG::SRT_DATA_MSG(unsigned char* data_p, unsigned int len, const std::string& path, unsigned int msg_type):_msg_type(msg_type)
    ,_len(len)
    ,_key_path(path)
{
    _data_p = new unsigned char[len];
    memcpy(_data_p, data_p, len);
}

SRT_DATA_MSG::SRT_DATA_MSG(LOGGER_LEVEL log_level, const std::string& log_content): _msg_type(SRT_MSG_LOG_TYPE)
    ,_log_content(log_content)
    ,_log_level(log_level)
{

}

SRT_DATA_MSG::~SRT_DATA_MSG() {
    if (_data_p && (_len > 0)) {
        delete[] _data_p;
    }
}

unsigned int SRT_DATA_MSG::msg_type() {
    return _msg_type;
}

void SRT_DATA_MSG::set_msg_type(unsigned int msg_type) {
    _msg_type = msg_type;
}

std::string SRT_DATA_MSG::get_path() {
    return _key_path;
}

unsigned int SRT_DATA_MSG::data_len() {
    return _len;
}

unsigned char* SRT_DATA_MSG::get_data() {
    return _data_p;
}

LOGGER_LEVEL SRT_DATA_MSG::get_log_level() {
    return _log_level;
}

const char* SRT_DATA_MSG::get_log_string() {
    return _log_content.c_str();
}
