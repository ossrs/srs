/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2020 Runner365
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

SRT_DATA_MSG::~SRT_DATA_MSG() {
    if (_data_p && (_len > 0)) {
        delete _data_p;
    }
}

unsigned int SRT_DATA_MSG::msg_type() {
    return _msg_type;
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
