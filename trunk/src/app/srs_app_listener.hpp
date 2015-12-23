/*
The MIT License (MIT)

Copyright (c) 2013-2016 SRS(ossrs)

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#ifndef SRS_APP_LISTENER_HPP
#define SRS_APP_LISTENER_HPP

/*
#include <srs_app_listener.hpp>
*/

#include <srs_core.hpp>

#include <string>

#include <srs_app_st.hpp>
#include <srs_app_thread.hpp>

struct sockaddr_in;

/**
* the udp packet handler.
*/
class ISrsUdpHandler
{
public:
    ISrsUdpHandler();
    virtual ~ISrsUdpHandler();
public:
    /**
     * when fd changed, for instance, reload the listen port,
     * notify the handler and user can do something.
     */
    virtual int on_stfd_change(st_netfd_t fd);
public:
    /**
    * when udp listener got a udp packet, notice server to process it.
    * @param type, the client type, used to create concrete connection,
    *       for instance RTMP connection to serve client.
    * @param from, the udp packet from address.
    * @param buf, the udp packet bytes, user should copy if need to use.
    * @param nb_buf, the size of udp packet bytes.
    * @remark user should never use the buf, for it's a shared memory bytes.
    */
    virtual int on_udp_packet(sockaddr_in* from, char* buf, int nb_buf) = 0;
};

/**
* the tcp connection handler.
*/
class ISrsTcpHandler
{
public:
    ISrsTcpHandler();
    virtual ~ISrsTcpHandler();
public:
    /**
    * when got tcp client.
    */
    virtual int on_tcp_client(st_netfd_t stfd) = 0;
};

/**
* bind udp port, start thread to recv packet and handler it.
*/
class SrsUdpListener : public ISrsReusableThreadHandler
{
private:
    int _fd;
    st_netfd_t _stfd;
    SrsReusableThread* pthread;
private:
    char* buf;
    int nb_buf;
private:
    ISrsUdpHandler* handler;
    std::string ip;
    int port;
public:
    SrsUdpListener(ISrsUdpHandler* h, std::string i, int p);
    virtual ~SrsUdpListener();
public:
    virtual int fd();
    virtual st_netfd_t stfd();
public:
    virtual int listen();
// interface ISrsReusableThreadHandler.
public:
    virtual int cycle();
};

/**
* bind and listen tcp port, use handler to process the client.
*/
class SrsTcpListener : public ISrsReusableThreadHandler
{
private:
    int _fd;
    st_netfd_t _stfd;
    SrsReusableThread* pthread;
private:
    ISrsTcpHandler* handler;
    std::string ip;
    int port;
public:
    SrsTcpListener(ISrsTcpHandler* h, std::string i, int p);
    virtual ~SrsTcpListener();
public:
    virtual int fd();
public:
    virtual int listen();
// interface ISrsReusableThreadHandler.
public:
    virtual int cycle();
};

#endif
