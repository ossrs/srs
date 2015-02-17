/*
The MIT License (MIT)

Copyright (c) 2013-2015 winlin

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

#include <srs_app_listener.hpp>

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <srs_kernel_log.hpp>
#include <srs_kernel_error.hpp>
#include <srs_app_server.hpp>
#include <srs_app_utility.hpp>

// set the max packet size.
#define SRS_UDP_MAX_PACKET_SIZE 65535

// sleep in ms for udp recv packet.
#define SRS_UDP_PACKET_RECV_CYCLE_INTERVAL_MS 0

// nginx also set to 512
#define SERVER_LISTEN_BACKLOG 512

ISrsUdpHandler::ISrsUdpHandler()
{
}

ISrsUdpHandler::~ISrsUdpHandler()
{
}

ISrsTcpHandler::ISrsTcpHandler()
{
}

ISrsTcpHandler::~ISrsTcpHandler()
{
}

SrsUdpListener::SrsUdpListener(ISrsUdpHandler* h, int p)
{
    handler = h;
    port = p;

    _fd = -1;
    stfd = NULL;

    nb_buf = SRS_UDP_MAX_PACKET_SIZE;
    buf = new char[nb_buf];

    pthread = new SrsThread("udp", this, 0, true);
}

SrsUdpListener::~SrsUdpListener()
{
    srs_close_stfd(stfd);
    
    pthread->stop();
    srs_freep(pthread);
    
    // st does not close it sometimes, 
    // close it manually.
    close(_fd);

    srs_freep(buf);
}

int SrsUdpListener::fd()
{
    return _fd;
}

int SrsUdpListener::listen()
{
    int ret = ERROR_SUCCESS;
    
    if ((_fd = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
        ret = ERROR_SOCKET_CREATE;
        srs_error("create linux socket error. port=%d, ret=%d", port, ret);
        return ret;
    }
    srs_verbose("create linux socket success. port=%d, fd=%d", port, _fd);
    
    int reuse_socket = 1;
    if (setsockopt(_fd, SOL_SOCKET, SO_REUSEADDR, &reuse_socket, sizeof(int)) == -1) {
        ret = ERROR_SOCKET_SETREUSE;
        srs_error("setsockopt reuse-addr error. port=%d, ret=%d", port, ret);
        return ret;
    }
    srs_verbose("setsockopt reuse-addr success. port=%d, fd=%d", port, _fd);
    
    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(_fd, (const sockaddr*)&addr, sizeof(sockaddr_in)) == -1) {
        ret = ERROR_SOCKET_BIND;
        srs_error("bind socket error. port=%d, ret=%d", port, ret);
        return ret;
    }
    srs_verbose("bind socket success. port=%d, fd=%d", port, _fd);
    
    if ((stfd = st_netfd_open_socket(_fd)) == NULL){
        ret = ERROR_ST_OPEN_SOCKET;
        srs_error("st_netfd_open_socket open socket failed. port=%d, ret=%d", port, ret);
        return ret;
    }
    srs_verbose("st open socket success. port=%d, fd=%d", port, _fd);
    
    if ((ret = pthread->start()) != ERROR_SUCCESS) {
        srs_error("st_thread_create listen thread error. port=%d, ret=%d", port, ret);
        return ret;
    }
    srs_verbose("create st listen thread success, port=%d", port);

    return ret;
}

int SrsUdpListener::cycle()
{
    int ret = ERROR_SUCCESS;

    for (;;) {
        // TODO: FIXME: support ipv6, @see man 7 ipv6
        sockaddr_in from;
        int nb_from = sizeof(sockaddr_in);
        int nread = 0;

        if ((nread = st_recvfrom(stfd, buf, nb_buf, (sockaddr*)&from, &nb_from, ST_UTIME_NO_TIMEOUT)) <= 0) {
            srs_warn("ignore recv udp packet failed, nread=%d", nread);
            continue;
        }
        
        if ((ret = handler->on_udp_packet(&from, buf, nread)) != ERROR_SUCCESS) {
            srs_warn("handle udp packet failed. ret=%d", ret);
            continue;
        }

        if (SRS_UDP_PACKET_RECV_CYCLE_INTERVAL_MS > 0) {
            st_usleep(SRS_UDP_PACKET_RECV_CYCLE_INTERVAL_MS * 1000);
        }
    }

    return ret;
}

SrsTcpListener::SrsTcpListener(ISrsTcpHandler* h, int p)
{
    handler = h;
    port = p;

    _fd = -1;
    stfd = NULL;

    pthread = new SrsThread("tcp", this, 0, true);
}

SrsTcpListener::~SrsTcpListener()
{
    srs_close_stfd(stfd);
    
    pthread->stop();
    srs_freep(pthread);
    
    // st does not close it sometimes, 
    // close it manually.
    close(_fd);
}

int SrsTcpListener::fd()
{
    return _fd;
}

int SrsTcpListener::listen()
{
    int ret = ERROR_SUCCESS;
    
    if ((_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        ret = ERROR_SOCKET_CREATE;
        srs_error("create linux socket error. port=%d, ret=%d", port, ret);
        return ret;
    }
    srs_verbose("create linux socket success. port=%d, fd=%d", port, _fd);
    
    int reuse_socket = 1;
    if (setsockopt(_fd, SOL_SOCKET, SO_REUSEADDR, &reuse_socket, sizeof(int)) == -1) {
        ret = ERROR_SOCKET_SETREUSE;
        srs_error("setsockopt reuse-addr error. port=%d, ret=%d", port, ret);
        return ret;
    }
    srs_verbose("setsockopt reuse-addr success. port=%d, fd=%d", port, _fd);
    
    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(_fd, (const sockaddr*)&addr, sizeof(sockaddr_in)) == -1) {
        ret = ERROR_SOCKET_BIND;
        srs_error("bind socket error. port=%d, ret=%d", port, ret);
        return ret;
    }
    srs_verbose("bind socket success. port=%d, fd=%d", port, _fd);
    
    if (::listen(_fd, SERVER_LISTEN_BACKLOG) == -1) {
        ret = ERROR_SOCKET_LISTEN;
        srs_error("listen socket error. port=%d, ret=%d", port, ret);
        return ret;
    }
    srs_verbose("listen socket success. port=%d, fd=%d", port, _fd);
    
    if ((stfd = st_netfd_open_socket(_fd)) == NULL){
        ret = ERROR_ST_OPEN_SOCKET;
        srs_error("st_netfd_open_socket open socket failed. port=%d, ret=%d", port, ret);
        return ret;
    }
    srs_verbose("st open socket success. port=%d, fd=%d", port, _fd);
    
    if ((ret = pthread->start()) != ERROR_SUCCESS) {
        srs_error("st_thread_create listen thread error. port=%d, ret=%d", port, ret);
        return ret;
    }
    srs_verbose("create st listen thread success, port=%d", port);
    
    return ret;
}

int SrsTcpListener::cycle()
{
    int ret = ERROR_SUCCESS;
    
    st_netfd_t client_stfd = st_accept(stfd, NULL, NULL, ST_UTIME_NO_TIMEOUT);
    
    if(client_stfd == NULL){
        // ignore error.
        srs_error("ignore accept thread stoppped for accept client error");
        return ret;
    }
    srs_verbose("get a client. fd=%d", st_netfd_fileno(client_stfd));
    
    if ((ret = handler->on_tcp_client(client_stfd)) != ERROR_SUCCESS) {
        srs_warn("accept client error. ret=%d", ret);
        return ret;
    }
    
    return ret;
}

