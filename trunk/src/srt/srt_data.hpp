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
 
#ifndef SRT_DATA_H
#define SRT_DATA_H

#include <srs_core.hpp>

#include <string>
#include <memory>

#define SRT_MSG_DATA_TYPE  0x01
#define SRT_MSG_CLOSE_TYPE 0x02

class SRT_DATA_MSG {
public:
    SRT_DATA_MSG(const std::string& path, unsigned int msg_type=SRT_MSG_DATA_TYPE);
    SRT_DATA_MSG(unsigned int len, const std::string& path, unsigned int msg_type=SRT_MSG_DATA_TYPE);
    SRT_DATA_MSG(unsigned char* data_p, unsigned int len, const std::string& path, unsigned int msg_type=SRT_MSG_DATA_TYPE);
    ~SRT_DATA_MSG();

    unsigned int msg_type();
    unsigned int data_len();
    unsigned char* get_data();
    std::string get_path();

private:
    unsigned int   _msg_type;
    unsigned int   _len;
    unsigned char* _data_p;
    std::string _key_path;
};

typedef std::shared_ptr<SRT_DATA_MSG> SRT_DATA_MSG_PTR;

#endif