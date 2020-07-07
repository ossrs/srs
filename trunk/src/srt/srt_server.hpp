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

#ifndef SRT_SERVER_H
#define SRT_SERVER_H

#include <srs_core.hpp>

#include <srt/srt.h>

#include <thread>
#include <memory>

#include <srs_app_hybrid.hpp>

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
    int init_srt_parameter();
    
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
    virtual srs_error_t run();
    virtual void stop();
};

#endif//SRT_SERVER_H