#ifndef SRT_HANDLE_H
#define SRT_HANDLE_H
#include <srt/srt.h>

#include <thread>
#include <memory>
#include <unordered_map>
#include <queue>
#include <string.h>
#include <mutex>

#include "srt_conn.hpp"

typedef struct {
    SRT_CONN_PTR conn_ptr;
    int events;
} request_message_t;

class srt_handle {
public:
    srt_handle();
    ~srt_handle();

    int start();//create srt epoll and create epoll thread
    void stop();//close srt epoll and end epoll thread

    void insert_message_queue(request_message_t msg);
    bool get_message_from_queue(request_message_t& msg);

private:
    //add new srt connection into epoll event
    void add_newconn(SRT_CONN_PTR conn_ptr, int events);
    //get srt conn object by srt socket
    SRT_CONN_PTR get_srt_conn(SRTSOCKET conn_srt_socket);
    //get srt connect mode: push or pull
    int get_srt_mode(SRTSOCKET conn_srt_socket);

    void onwork();//epoll thread loop
    //handle recv/send srt socket
    void handle_srt_socket(SRT_SOCKSTATUS status, SRTSOCKET conn_fd);
    void handle_push_data(SRT_SOCKSTATUS status, const std::string& subpath, SRTSOCKET conn_fd);
    void handle_pull_data(SRT_SOCKSTATUS status, const std::string& subpath, SRTSOCKET conn_fd);

    //add new puller into puller list and conn_map
    void add_new_puller(SRT_CONN_PTR, std::string stream_id);
    //remove pull srt from play list
    void close_pull_conn(SRTSOCKET srtsocket, std::string stream_id);

    //add new pusher into pusher map: <socket fd, pusher conn ptr>
    bool add_new_pusher(SRT_CONN_PTR conn_ptr);
    //remove push connection and remove epoll
    void close_push_conn(SRTSOCKET srtsocket);
    
    //check srt connection whether it's still alive.
    void check_alive();

    //debug statics
    void debug_statics(SRTSOCKET srtsocket, const std::string& streamid);

private:
    int _handle_pollid;

    std::unordered_map<SRTSOCKET, SRT_CONN_PTR> _conn_map;
    std::shared_ptr<std::thread> _work_thread_ptr;

    //streamid, play map<SRTSOCKET, SRT_CONN_PTR>
    std::unordered_map<std::string, std::unordered_map<SRTSOCKET, SRT_CONN_PTR>> _streamid_map;
    
    std::mutex _queue_mutex;
    std::queue<request_message_t> _message_queue;

    long long _last_timestamp;
    long long _last_check_alive_ts;
};

#endif //SRT_HANDLE_H