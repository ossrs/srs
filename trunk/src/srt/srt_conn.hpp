#ifndef SRT_CONN_H
#define SRT_CONN_H
#include <srt/srt.h>
#include <thread>
#include <memory>
#include <string>
#include <srs_kernel_log.hpp>
#include <srs_kernel_error.hpp>
#include <srs_app_rtmp_conn.hpp>
#include <srs_app_config.hpp>

#define PULL_SRT_MODE 0x01
#define PUSH_SRT_MODE 0x02

inline void get_streamid_info(const std::string& streamid, int& mode, std::string& url_subpash) {
    std::string real_streamip;
    
    size_t pos = streamid.find("?");
    if (pos == std::string::npos) {
        mode = PULL_SRT_MODE;
        url_subpash = streamid;
    } else {
        std::string mode_str = streamid.substr(pos+1);

        url_subpash = streamid.substr(0, pos);

        size_t mode_pos = mode_str.find("m=");
        if (mode_pos == std::string::npos) {
            mode = PULL_SRT_MODE;
        } else {
            mode_str = mode_str.substr(mode_pos+2);
            if (mode_str == "push") {
                mode = PUSH_SRT_MODE;
            } else {
                mode = PULL_SRT_MODE;
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