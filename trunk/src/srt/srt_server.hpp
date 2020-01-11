#ifndef SRT_SERVER_H
#define SRT_SERVER_H
#include <thread>
#include <memory>

class srt_server {
public:
    srt_server(unsigned short port);
    ~srt_server();

    int start();
    void stop();

private:
    void on_work();

private:
    unsigned short listen_port;
    bool run_flag;
    std::shared_ptr<std::thread> thread_run_ptr;
};

typedef std::shared_ptr<srt_server> SRT_SERVER_PTR;

#endif//SRT_SERVER_H