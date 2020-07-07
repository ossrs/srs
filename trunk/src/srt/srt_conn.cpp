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

#include "srt_conn.hpp"
#include "time_help.h"
#include "stringex.hpp"
#include <vector>

#include <srs_app_config.hpp>

bool is_streamid_valid(const std::string& streamid) {
    if (streamid.empty()) {
        return false;
    }

    size_t pos = streamid.find(" ");
    if (pos != streamid.npos) {
        return false;
    }

    int mode;
    std::string subpath;

    bool ret = get_streamid_info(streamid, mode, subpath);
    if (!ret) {
        return false;
    }
    
    if ((mode != PUSH_SRT_MODE) && (mode != PULL_SRT_MODE)) {
        return false;
    }

    std::vector<std::string> info_vec;
    string_split(subpath, "/", info_vec);

    if (info_vec.size() < 2) {//it must be appname/stream at least.
        return false;
    }

    for (auto item : info_vec) {
        if (item.empty()) {
            return false;
        }
        pos = item.find(" ");
        if (pos != item.npos) {
            return false;
        }
    }
    return true;
}

bool get_key_value(const std::string& info, std::string& key, std::string& value) {
    size_t pos = info.find("=");

    if (pos == info.npos) {
        return false;
    }

    key = info.substr(0, pos);
    value = info.substr(pos+1);

    if (key.empty() || value.empty()) {
        return false;
    }
    return true;
}

//eg. streamid=#!::h:live/livestream,m:publish
bool get_streamid_info(const std::string& streamid, int& mode, std::string& url_subpath) {
    std::vector<std::string> info_vec;
    std::string real_streamid;

    mode = PUSH_SRT_MODE;

    size_t pos = streamid.find("#!::");
    if (pos != 0) {
        pos = streamid.find("/");
        if (pos == streamid.npos) {
            url_subpath = _srs_config->get_default_app_name() + "/" + streamid;
            return true;
        }
        url_subpath = streamid;
        return true;
    }
    real_streamid = streamid.substr(4);

    string_split(real_streamid, ",", info_vec);
    if (info_vec.size() < 2) {
        return false;
    }

    for (size_t index = 0; index < info_vec.size(); index++) {
        std::string key;
        std::string value;

        bool ret = get_key_value(info_vec[index], key, value);
        if (!ret) {
            continue;
        }
        
        if (key == "h") {
            url_subpath = value;//eg. h=live/stream
        } else if (key == "m") {
            std::string mode_str = string_lower(value);//m=publish or m=request
            if (mode_str == "publish") {
                mode = PUSH_SRT_MODE;
            } else if (mode_str == "request") {
                mode = PULL_SRT_MODE;
            } else {
                mode = PUSH_SRT_MODE;
            }
        } else {//not suport
            continue;
        }
    }

    return true;
}

srt_conn::srt_conn(SRTSOCKET conn_fd, const std::string& streamid):_conn_fd(conn_fd),
    _streamid(streamid) {
    get_streamid_info(streamid, _mode, _url_subpath);
    
    _update_timestamp = now_ms();
    
    std::vector<std::string> path_vec;
    
    string_split(_url_subpath, "/", path_vec);
    if (path_vec.size() >= 3) {
        _vhost = path_vec[0];
    } else {
        _vhost = "__default_host__";
    }
    srs_trace("srt connect construct streamid:%s, mode:%d, subpath:%s, vhost:%s", 
        streamid.c_str(), _mode, _url_subpath.c_str(), _vhost.c_str());
}

srt_conn::~srt_conn() {
    close();
}

std::string srt_conn::get_vhost() {
    return _vhost;
}

void srt_conn::update_timestamp(long long now_ts) {
    _update_timestamp = now_ts;
}

long long srt_conn::get_last_ts() {
    return _update_timestamp;
}

void srt_conn::close() {
    if (_conn_fd == SRT_INVALID_SOCK) {
        return;
    }
    srt_close(_conn_fd);
    _conn_fd = SRT_INVALID_SOCK;
}

SRTSOCKET srt_conn::get_conn() {
    return _conn_fd;
}
int srt_conn::get_mode() {
    return _mode;
}

std::string srt_conn::get_streamid() {
    return _streamid;
}

std::string srt_conn::get_subpath() {
    return _url_subpath;
}

int srt_conn::read(unsigned char* data, int len) {
    int ret = 0;

    ret = srt_recv(_conn_fd, (char*)data, len);
    if (ret <= 0) {
        srs_error("srt read error:%d, socket fd:%d", ret, _conn_fd);
        return ret;
    }
    return ret;
}

int srt_conn::write(unsigned char* data, int len) {
    int ret = 0;

    ret = srt_send(_conn_fd, (char*)data, len);
    if (ret <= 0) {
        srs_error("srt write error:%d, socket fd:%d", ret, _conn_fd);
        return ret;
    }
    return ret;
}