#ifndef SRT_SERVER_H
#define SRT_SERVER_H
#include <srt/srt.h>

#include <thread>
#include <memory>

class srt_handle;

class srt_server {
public:
    srt_server(unsigned short port);
    ~srt_server();

    int start();//init srt handl and create srt main thread loop
    void stop();//stop srt main thread loop

private:
    //init srt socket and srt epoll
    int init_srt();
    //srt main epoll loop
    void on_work();
    //accept new srt connection
    void srt_handle_connection(SRT_SOCKSTATUS status, SRTSOCKET input_fd, const std::string& dscr);

private:
    unsigned short listen_port;
    SRTSOCKET server_socket;
    int _pollid;
    bool run_flag;
    std::shared_ptr<std::thread> thread_run_ptr;
    std::shared_ptr<srt_handle> handle_ptr;
};

typedef std::shared_ptr<srt_server> SRT_SERVER_PTR;

#endif//SRT_SERVER_H