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

#ifndef SRT_CONN_H
#define SRT_CONN_H

#include <srs_core.hpp>

#include "stringex.hpp"
#include <srt/srt.h>
#include <thread>
#include <memory>
#include <string>
#include <vector>
#include <srs_kernel_log.hpp>
#include <srs_kernel_error.hpp>
#include <srs_app_rtmp_conn.hpp>
#include <srs_app_config.hpp>

#define ERR_SRT_MODE  0x00
#define PULL_SRT_MODE 0x01
#define PUSH_SRT_MODE 0x02

bool is_streamid_valid(const std::string& streamid);
bool get_key_value(const std::string& info, std::string& key, std::string& value);
bool get_streamid_info(const std::string& streamid, int& mode, std::string& url_subpash);

class srt_conn {
public:
    srt_conn(SRTSOCKET conn_fd, const std::string& streamid);
    ~srt_conn();

    void close();
    SRTSOCKET get_conn();
    int get_mode();
    std::string get_streamid();
    std::string get_subpath();
    std::string get_vhost();
    int read(unsigned char* data, int len);
    int write(unsigned char* data, int len);

    void update_timestamp(long long now_ts);
    long long get_last_ts();

private:
    SRTSOCKET _conn_fd;
    std::string _streamid;
    std::string _url_subpath;
    std::string _vhost;
    int _mode;
    long long _update_timestamp;
};

typedef std::shared_ptr<srt_conn> SRT_CONN_PTR;

#endif //SRT_CONN_H