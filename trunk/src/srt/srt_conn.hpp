#ifndef SRT_CONN_H
#define SRT_CONN_H
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

inline bool is_streamid_valid(const std::string& streamid) {
    bool ret = false;
    if (streamid.empty()) {
        return ret;
    }
    //check whether subpath is valid, eg. live/test or srs.srt.com.cn/live/test
    std::vector<std::string> ret_vec;
    string_split(streamid, "/", ret_vec);
    if (ret_vec.size() < 2) {
        return ret;
    }

    //check whether mode is valid, eg. live/test?m=push or live/test?m=pull
    size_t pos = streamid.find("?");
    if (pos == streamid.npos) {
        return ret;
    }

    std::string mode = streamid.substr(pos+1);
    string_split(mode, "=", ret_vec);
    if (ret_vec.size() != 2) {
        return ret;
    }
    //it must be m=push or m=pull
    std::string mode_oper = string_lower(ret_vec[1]);
    if ((ret_vec[0] != "m") || ((mode_oper != "push") && (mode_oper != "pull"))) {
        return ret;
    }
    ret = true;
    return ret;
}

inline void get_streamid_info(const std::string& streamid, int& mode, std::string& url_subpash) {
    std::string real_streamip;
    
    size_t pos = streamid.find("?");
    if (pos == std::string::npos) {
        mode = ERR_SRT_MODE;
        url_subpash = streamid;
    } else {
        std::string mode_str = streamid.substr(pos+1);

        url_subpash = streamid.substr(0, pos);

        size_t mode_pos = mode_str.find("m=");
        if (mode_pos == std::string::npos) {
            mode = PULL_SRT_MODE;
        } else {
            mode_str = mode_str.substr(mode_pos+2);
            mode_str = string_lower(mode_str);
            if (mode_str == "push") {
                mode = PUSH_SRT_MODE;
            } else if(mode_str == "pull"){
                mode = PULL_SRT_MODE;
            } else {
                mode = ERR_SRT_MODE;
            }
        }
    }
}

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