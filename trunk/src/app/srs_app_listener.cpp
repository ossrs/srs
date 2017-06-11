/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2017 OSSRS(winlin)
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

#include <srs_app_listener.hpp>

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
using namespace std;

#include <srs_kernel_log.hpp>
#include <srs_kernel_error.hpp>
#include <srs_app_server.hpp>
#include <srs_app_utility.hpp>

// set the max packet size.
#define SRS_UDP_MAX_PACKET_SIZE 65535

// sleep in ms for udp recv packet.
#define SrsUdpPacketRecvCycleMS 0

// nginx also set to 512
#define SERVER_LISTEN_BACKLOG 512

ISrsUdpHandler::ISrsUdpHandler()
{
}

ISrsUdpHandler::~ISrsUdpHandler()
{
}

int ISrsUdpHandler::on_stfd_change(srs_netfd_t /*fd*/)
{
    return ERROR_SUCCESS;
}

ISrsTcpHandler::ISrsTcpHandler()
{
}

ISrsTcpHandler::~ISrsTcpHandler()
{
}

SrsUdpListener::SrsUdpListener(ISrsUdpHandler* h, string i, int p)
{
    handler = h;
    ip = i;
    port = p;
    
    _fd = -1;
    _stfd = NULL;
    
    nb_buf = SRS_UDP_MAX_PACKET_SIZE;
    buf = new char[nb_buf];
    
    trd = new SrsDummyCoroutine();
}

SrsUdpListener::~SrsUdpListener()
{
    // close the stfd to trigger thread to interrupted.
    srs_close_stfd(_stfd);
    
    srs_freep(trd);
    
    // st does not close it sometimes,
    // close it manually.
    close(_fd);
    
    srs_freepa(buf);
}

int SrsUdpListener::fd()
{
    return _fd;
}

srs_netfd_t SrsUdpListener::stfd()
{
    return _stfd;
}

int SrsUdpListener::listen()
{
    int ret = ERROR_SUCCESS;
    srs_error_t err = srs_success;
    
    if ((_fd = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
        ret = ERROR_SOCKET_CREATE;
        srs_error("create linux socket error. ip=%s, port=%d, ret=%d", ip.c_str(), port, ret);
        return ret;
    }
    srs_verbose("create linux socket success. ip=%s, port=%d, fd=%d", ip.c_str(), port, _fd);
    
    srs_fd_close_exec(_fd);
    srs_socket_reuse_addr(_fd);
    
    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(ip.c_str());
    if (bind(_fd, (const sockaddr*)&addr, sizeof(sockaddr_in)) == -1) {
        ret = ERROR_SOCKET_BIND;
        srs_error("bind socket error. ep=%s:%d, ret=%d", ip.c_str(), port, ret);
        return ret;
    }
    srs_verbose("bind socket success. ep=%s:%d, fd=%d", ip.c_str(), port, _fd);
    
    if ((_stfd = srs_netfd_open_socket(_fd)) == NULL){
        ret = ERROR_ST_OPEN_SOCKET;
        srs_error("st_netfd_open_socket open socket failed. ep=%s:%d, ret=%d", ip.c_str(), port, ret);
        return ret;
    }
    srs_verbose("st open socket success. ep=%s:%d, fd=%d", ip.c_str(), port, _fd);
    
    srs_freep(trd);
    trd = new SrsSTCoroutine("udp", this);
    if ((err = trd->start()) != srs_success) {
        // TODO: FIXME: Use error
        ret = srs_error_code(err);
        srs_freep(err);

        srs_error("st_thread_create listen thread error. ep=%s:%d, ret=%d", ip.c_str(), port, ret);
        return ret;
    }
    srs_verbose("create st listen thread success, ep=%s:%d", ip.c_str(), port);
    
    return ret;
}

srs_error_t SrsUdpListener::cycle()
{
    int ret = ERROR_SUCCESS;
    srs_error_t err = srs_success;
    
    while (!trd->pull()) {
        // TODO: FIXME: support ipv6, @see man 7 ipv6
        sockaddr_in from;
        int nb_from = sizeof(sockaddr_in);
        int nread = 0;
        
        if ((nread = srs_recvfrom(_stfd, buf, nb_buf, (sockaddr*)&from, &nb_from, SRS_UTIME_NO_TIMEOUT)) <= 0) {
            return srs_error_new(ERROR_SOCKET_READ, "udp read, nread=%d", nread);
        }
        
        if ((ret = handler->on_udp_packet(&from, buf, nread)) != ERROR_SUCCESS) {
            return srs_error_new(ret, "handle packet %d bytes", nread);
        }
        
        if (SrsUdpPacketRecvCycleMS > 0) {
            srs_usleep(SrsUdpPacketRecvCycleMS * 1000);
        }
    }
    
    return err;
}

SrsTcpListener::SrsTcpListener(ISrsTcpHandler* h, string i, int p)
{
    handler = h;
    ip = i;
    port = p;
    
    _fd = -1;
    _stfd = NULL;
    
    trd = new SrsDummyCoroutine();
}

SrsTcpListener::~SrsTcpListener()
{
    srs_freep(trd);
    
    srs_close_stfd(_stfd);
}

int SrsTcpListener::fd()
{
    return _fd;
}

int SrsTcpListener::listen()
{
    int ret = ERROR_SUCCESS;
    srs_error_t err = srs_success;
    
    if ((_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        ret = ERROR_SOCKET_CREATE;
        srs_error("create linux socket error. port=%d, ret=%d", port, ret);
        return ret;
    }
    srs_verbose("create linux socket success. port=%d, fd=%d", port, _fd);
    
    srs_fd_close_exec(_fd);
    srs_socket_reuse_addr(_fd);
    
    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(ip.c_str());
    if (bind(_fd, (const sockaddr*)&addr, sizeof(sockaddr_in)) == -1) {
        ret = ERROR_SOCKET_BIND;
        srs_error("bind socket error. ep=%s:%d, ret=%d", ip.c_str(), port, ret);
        return ret;
    }
    srs_verbose("bind socket success. ep=%s:%d, fd=%d", ip.c_str(), port, _fd);
    
    if (::listen(_fd, SERVER_LISTEN_BACKLOG) == -1) {
        ret = ERROR_SOCKET_LISTEN;
        srs_error("listen socket error. ep=%s:%d, ret=%d", ip.c_str(), port, ret);
        return ret;
    }
    srs_verbose("listen socket success. ep=%s:%d, fd=%d", ip.c_str(), port, _fd);
    
    if ((_stfd = srs_netfd_open_socket(_fd)) == NULL){
        ret = ERROR_ST_OPEN_SOCKET;
        srs_error("st_netfd_open_socket open socket failed. ep=%s:%d, ret=%d", ip.c_str(), port, ret);
        return ret;
    }
    srs_verbose("st open socket success. ep=%s:%d, fd=%d", ip.c_str(), port, _fd);
    
    srs_freep(trd);
    trd = new SrsSTCoroutine("tcp", this);
    if ((err = trd->start()) != srs_success) {
        // TODO: FIXME: Use error
        ret = srs_error_code(err);
        srs_freep(err);

        srs_error("st_thread_create listen thread error. ep=%s:%d, ret=%d", ip.c_str(), port, ret);
        return ret;
    }
    srs_verbose("create st listen thread success, ep=%s:%d", ip.c_str(), port);
    
    return ret;
}

srs_error_t SrsTcpListener::cycle()
{
    int ret = ERROR_SUCCESS;
    srs_error_t err = srs_success;
    
    while (!trd->pull()) {
        srs_netfd_t stfd = srs_accept(_stfd, NULL, NULL, SRS_UTIME_NO_TIMEOUT);
        int fd = srs_netfd_fileno(stfd);
        
        srs_fd_close_exec(fd);
        
        if(stfd == NULL){
            return err;
        }
        srs_verbose("get a client. fd=%d", fd);
        
        if ((ret = handler->on_tcp_client(stfd)) != ERROR_SUCCESS) {
            return srs_error_new(ret, "handle fd=%d", fd);
        }
    }
    
    return err;
}

