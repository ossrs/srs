/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2018 Winlin
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
#include <netdb.h>
using namespace std;

#include <srs_core_autofree.hpp>
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

srs_error_t ISrsUdpHandler::on_stfd_change(srs_netfd_t /*fd*/)
{
    return srs_success;
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

srs_error_t SrsUdpListener::listen()
{
    srs_error_t err = srs_success;
    
    char sport[8];
    snprintf(sport, sizeof(sport), "%d", port);
    
    addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags    = AI_NUMERICHOST;
    
    addrinfo* r  = NULL;
    SrsAutoFree(addrinfo, r);
    if(getaddrinfo(ip.c_str(), sport, (const addrinfo*)&hints, &r)) {
        return srs_error_new(ERROR_SYSTEM_IP_INVALID, "get address info");
    }
    
    if ((_fd = socket(r->ai_family, r->ai_socktype, r->ai_protocol)) == -1) {
        return srs_error_new(ERROR_SOCKET_CREATE, "create socket. ip=%s, port=%d", ip.c_str(), port);
    }

    srs_fd_close_exec(_fd);
    srs_socket_reuse_addr(_fd);
    
    if (bind(_fd, r->ai_addr, r->ai_addrlen) == -1) {
        return srs_error_new(ERROR_SOCKET_BIND, "bind socket. ep=%s:%d", ip.c_str(), port);;
    }
    
    if ((_stfd = srs_netfd_open_socket(_fd)) == NULL){
        return srs_error_new(ERROR_ST_OPEN_SOCKET, "st open socket");
    }
    
    srs_freep(trd);
    trd = new SrsSTCoroutine("udp", this);
    if ((err = trd->start()) != srs_success) {
        return srs_error_wrap(err, "start thread");
    }
    
    return err;
}

srs_error_t SrsUdpListener::cycle()
{
    srs_error_t err = srs_success;
    
    while (true) {
        if ((err = trd->pull()) != srs_success) {
            return srs_error_wrap(err, "udp listener");
        }
        
        sockaddr_storage from;
        int nb_from = sizeof(from);
        int nread = 0;
        
        if ((nread = srs_recvfrom(_stfd, buf, nb_buf, (sockaddr*)&from, &nb_from, SRS_UTIME_NO_TIMEOUT)) <= 0) {
            return srs_error_new(ERROR_SOCKET_READ, "udp read, nread=%d", nread);
        }
        
        if ((err = handler->on_udp_packet((const sockaddr*)&from, nb_from, buf, nread)) != srs_success) {
            return srs_error_wrap(err, "handle packet %d bytes", nread);
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

srs_error_t SrsTcpListener::listen()
{
    srs_error_t err = srs_success;
    
    char sport[8];
    snprintf(sport, sizeof(sport), "%d", port);
    
    addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags    = AI_NUMERICHOST;
    
    addrinfo* r = NULL;
    SrsAutoFree(addrinfo, r);
    if(getaddrinfo(ip.c_str(), sport, (const addrinfo*)&hints, &r)) {
        return srs_error_new(ERROR_SYSTEM_IP_INVALID, "get address info");
    }
    
    if ((_fd = socket(r->ai_family, r->ai_socktype, r->ai_protocol)) == -1) {
        return srs_error_new(ERROR_SOCKET_CREATE, "create socket. ip=%s, port=%d", ip.c_str(), port);
    }
    
    // Detect alive for TCP connection.
    // @see https://github.com/ossrs/srs/issues/1044
#ifdef SO_KEEPALIVE
    int tcp_keepalive = 1;
    if (setsockopt(_fd, SOL_SOCKET, SO_KEEPALIVE, &tcp_keepalive, sizeof(int)) == -1) {
        return srs_error_new(ERROR_SOCKET_SETKEEPALIVE, "setsockopt SO_KEEPALIVE[%d]error. port=%d", tcp_keepalive, port);
    }
#endif

    srs_fd_close_exec(_fd);
    srs_socket_reuse_addr(_fd);
    
    if (bind(_fd, r->ai_addr, r->ai_addrlen) == -1) {
        return srs_error_new(ERROR_SOCKET_BIND, "bind socket. ep=%s:%d", ip.c_str(), port);;
    }

    if (::listen(_fd, SERVER_LISTEN_BACKLOG) == -1) {
        return srs_error_new(ERROR_SOCKET_LISTEN, "listen socket");
    }
    
    if ((_stfd = srs_netfd_open_socket(_fd)) == NULL){
        return srs_error_new(ERROR_ST_OPEN_SOCKET, "st open socket");
    }
    
    srs_freep(trd);
    trd = new SrsSTCoroutine("tcp", this);
    if ((err = trd->start()) != srs_success) {
        return srs_error_wrap(err, "start coroutine");
    }
    
    return err;
}

srs_error_t SrsTcpListener::cycle()
{
    srs_error_t err = srs_success;
    
    while (true) {
        if ((err = trd->pull()) != srs_success) {
            return srs_error_wrap(err, "tcp listener");
        }
        
        srs_netfd_t cstfd = srs_accept(_stfd, NULL, NULL, SRS_UTIME_NO_TIMEOUT);
        if(cstfd == NULL){
            return srs_error_new(ERROR_SOCKET_CREATE, "accept failed");
        }
        
        int cfd = srs_netfd_fileno(cstfd);
        srs_fd_close_exec(cfd);
        
        if ((err = handler->on_tcp_client(cstfd)) != srs_success) {
            return srs_error_wrap(err, "handle fd=%d", cfd);
        }
    }
    
    return err;
}

