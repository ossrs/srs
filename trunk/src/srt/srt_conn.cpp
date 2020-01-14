#include "srt_conn.hpp"
#include "time_help.h"
#include "stringex.hpp"
#include <vector>

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