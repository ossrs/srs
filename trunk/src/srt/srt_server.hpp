//
// Copyright (c) 2013-2021 Runner365
//
// SPDX-License-Identifier: MIT
//

#ifndef SRT_SERVER_H
#define SRT_SERVER_H

#include <srs_core.hpp>

#include <srt/srt.h>

#include <thread>
#include <memory>

#include <srs_app_hybrid.hpp>

class srt_handle;
class SrsWaitGroup;

class srt_server {
public:
    srt_server(unsigned short port);
    ~srt_server();

    int start();//init srt handl and create srt main thread loop
    void stop();//stop srt main thread loop

private:
    //init srt socket and srt epoll
    int init_srt();
    int init_srt_parameter();
    void init_srt_log();
    
    //srt main epoll loop
    void on_work();
    //accept new srt connection
    void srt_handle_connection(SRT_SOCKSTATUS status, SRTSOCKET input_fd, const std::string& dscr);
    //get srt data read/write
    void srt_handle_data(SRT_SOCKSTATUS status, SRTSOCKET input_fd, const std::string& dscr);

private:
    unsigned short _listen_port;
    SRTSOCKET _server_socket;
    int _pollid;
    bool run_flag;
    std::shared_ptr<std::thread> thread_run_ptr;
    std::shared_ptr<srt_handle> _handle_ptr;
};

typedef std::shared_ptr<srt_server> SRT_SERVER_PTR;

class SrtServerAdapter : public ISrsHybridServer
{
private:
    SRT_SERVER_PTR srt_ptr;
public:
    SrtServerAdapter();
    virtual ~SrtServerAdapter();
public:
    virtual srs_error_t initialize();
    virtual srs_error_t run(SrsWaitGroup* wg);
    virtual void stop();
};

#endif//SRT_SERVER_H
