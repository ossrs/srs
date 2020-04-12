/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2020 Winlin
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
#include <stdlib.h>
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

// sleep in srs_utime_t for udp recv packet.
#define SrsUdpPacketRecvCycleInterval 0

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

ISrsUdpMuxHandler::ISrsUdpMuxHandler()
{
}

ISrsUdpMuxHandler::~ISrsUdpMuxHandler()
{
}

srs_error_t ISrsUdpMuxHandler::on_stfd_change(srs_netfd_t /*fd*/)
{
    return srs_success;
}

void ISrsUdpHandler::set_stfd(srs_netfd_t /*fd*/)
{
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
    lfd = NULL;
    
    nb_buf = SRS_UDP_MAX_PACKET_SIZE;
    buf = new char[nb_buf];
    
    trd = new SrsDummyCoroutine();
}

SrsUdpListener::~SrsUdpListener()
{
    srs_freep(trd);
    srs_close_stfd(lfd);
    srs_freepa(buf);
}

int SrsUdpListener::fd()
{
    return srs_netfd_fileno(lfd);
}

srs_netfd_t SrsUdpListener::stfd()
{
    return lfd;
}

srs_error_t SrsUdpListener::listen()
{
    srs_error_t err = srs_success;

    if ((err = srs_udp_listen(ip, port, &lfd)) != srs_success) {
        return srs_error_wrap(err, "listen %s:%d", ip.c_str(), port);
    }

    handler->set_stfd(lfd);
    
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

        int nread = 0;
        sockaddr_storage from;
        int nb_from = sizeof(from);
        if ((nread = srs_recvfrom(lfd, buf, nb_buf, (sockaddr*)&from, &nb_from, SRS_UTIME_NO_TIMEOUT)) <= 0) {
            return srs_error_new(ERROR_SOCKET_READ, "udp read, nread=%d", nread);
        }

        // Drop UDP health check packet of Aliyun SLB.
        //      Healthcheck udp check
        // @see https://help.aliyun.com/document_detail/27595.html
        if (nread == 21 && buf[0] == 0x48 && buf[1] == 0x65 && buf[2] == 0x61 && buf[3] == 0x6c
            && buf[19] == 0x63 && buf[20] == 0x6b) {
            continue;
        }
        
        if ((err = handler->on_udp_packet((const sockaddr*)&from, nb_from, buf, nread)) != srs_success) {
            return srs_error_wrap(err, "handle packet %d bytes", nread);
        }
        
        if (SrsUdpPacketRecvCycleInterval > 0) {
            srs_usleep(SrsUdpPacketRecvCycleInterval);
        }
    }
    
    return err;
}

SrsTcpListener::SrsTcpListener(ISrsTcpHandler* h, string i, int p)
{
    handler = h;
    ip = i;
    port = p;

    lfd = NULL;
    
    trd = new SrsDummyCoroutine();
}

SrsTcpListener::~SrsTcpListener()
{
    srs_freep(trd);
    srs_close_stfd(lfd);
}

int SrsTcpListener::fd()
{
    return srs_netfd_fileno(lfd);;
}

srs_error_t SrsTcpListener::listen()
{
    srs_error_t err = srs_success;

    if ((err = srs_tcp_listen(ip, port, &lfd)) != srs_success) {
        return srs_error_wrap(err, "listen at %s:%d", ip.c_str(), port);
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
        
        srs_netfd_t fd = srs_accept(lfd, NULL, NULL, SRS_UTIME_NO_TIMEOUT);
        if(fd == NULL){
            return srs_error_new(ERROR_SOCKET_ACCEPT, "accept at fd=%d", srs_netfd_fileno(lfd));
        }
        
        if ((err = srs_fd_closeexec(srs_netfd_fileno(fd))) != srs_success) {
            return srs_error_wrap(err, "set closeexec");
        }
        
        if ((err = handler->on_tcp_client(fd)) != srs_success) {
            return srs_error_wrap(err, "handle fd=%d", srs_netfd_fileno(fd));
        }
    }
    
    return err;
}

SrsUdpMuxSocket::SrsUdpMuxSocket(srs_netfd_t fd)
{
    nb_buf = SRS_UDP_MAX_PACKET_SIZE;
    buf = new char[nb_buf];
    nread = 0;

    lfd = fd;

    fromlen = 0;
}

SrsUdpMuxSocket::~SrsUdpMuxSocket()
{
    srs_freepa(buf);
}

SrsUdpMuxSocket* SrsUdpMuxSocket::copy_sendonly()
{
    SrsUdpMuxSocket* sendonly = new SrsUdpMuxSocket(lfd);

    // Don't copy buffer
    srs_freepa(sendonly->buf);
    sendonly->nb_buf    = 0;
    sendonly->nread     = 0;
    sendonly->lfd       = lfd;
    sendonly->from      = from;
    sendonly->fromlen   = fromlen;
    sendonly->peer_ip   = peer_ip;
    sendonly->peer_port = peer_port;

    return sendonly;
}

int SrsUdpMuxSocket::recvfrom(srs_utime_t timeout)
{
    fromlen = sizeof(from);
    nread = srs_recvfrom(lfd, buf, nb_buf, (sockaddr*)&from, &fromlen, timeout);

    // Drop UDP health check packet of Aliyun SLB.
    //      Healthcheck udp check
    // @see https://help.aliyun.com/document_detail/27595.html
    if (nread == 21 && buf[0] == 0x48 && buf[1] == 0x65 && buf[2] == 0x61 && buf[3] == 0x6c
        && buf[19] == 0x63 && buf[20] == 0x6b) {
        return 0;
    }

    if (nread > 0) {
        // TODO: FIXME: Maybe we should not covert to string for each packet.
        char address_string[64];
        char port_string[16];
        if (getnameinfo((sockaddr*)&from, fromlen, 
                       (char*)&address_string, sizeof(address_string),
                       (char*)&port_string, sizeof(port_string),
                       NI_NUMERICHOST|NI_NUMERICSERV)) {
            return -1;
        }

        peer_ip = std::string(address_string);
        peer_port = atoi(port_string);    
    }

    return nread;
}

srs_error_t SrsUdpMuxSocket::sendto(void* data, int size, srs_utime_t timeout)
{
    srs_error_t err = srs_success;

    int nb_write = srs_sendto(lfd, data, size, (sockaddr*)&from, fromlen, timeout);

    if (nb_write <= 0) {
        if (nb_write < 0 && errno == ETIME) {
            return srs_error_new(ERROR_SOCKET_TIMEOUT, "sendto timeout %d ms", srsu2msi(timeout));
        }   
    
        return srs_error_new(ERROR_SOCKET_WRITE, "sendto");
    }   

    return err;
}

srs_netfd_t SrsUdpMuxSocket::stfd()
{
    return lfd;
}

sockaddr_in* SrsUdpMuxSocket::peer_addr()
{
    return (sockaddr_in*)&from;
}

socklen_t SrsUdpMuxSocket::peer_addrlen()
{
    return (socklen_t)fromlen;
}

std::string SrsUdpMuxSocket::get_peer_id()
{
    char id_buf[1024];
    int len = snprintf(id_buf, sizeof(id_buf), "%s:%d", peer_ip.c_str(), peer_port);

    return string(id_buf, len);
}

SrsUdpMuxListener::SrsUdpMuxListener(ISrsUdpMuxHandler* h, std::string i, int p)
{
    handler = h;
    ip = i;
    port = p;
    lfd = NULL;
    
    nb_buf = SRS_UDP_MAX_PACKET_SIZE;
    buf = new char[nb_buf];
    
    trd = new SrsDummyCoroutine();
}

SrsUdpMuxListener::~SrsUdpMuxListener()
{
    srs_freep(trd);
    srs_close_stfd(lfd);
    srs_freepa(buf);
}

int SrsUdpMuxListener::fd()
{
    return srs_netfd_fileno(lfd);
}

srs_netfd_t SrsUdpMuxListener::stfd()
{
    return lfd;
}

srs_error_t SrsUdpMuxListener::listen()
{
    srs_error_t err = srs_success;

    if ((err = srs_udp_listen(ip, port, &lfd)) != srs_success) {
        return srs_error_wrap(err, "listen %s:%d", ip.c_str(), port);
    }

    set_socket_buffer();
    
    srs_freep(trd);
    trd = new SrsSTCoroutine("udp", this);
    if ((err = trd->start()) != srs_success) {
        return srs_error_wrap(err, "start thread");
    }
    
    return err;
}

void SrsUdpMuxListener::set_socket_buffer()
{
    int sndbuf_size = 0;
    socklen_t opt_len = sizeof(sndbuf_size);
    getsockopt(fd(), SOL_SOCKET, SO_SNDBUF, (void*)&sndbuf_size, &opt_len);
    srs_trace("default udp remux socket sndbuf=%d", sndbuf_size);

    sndbuf_size = 1024*1024*10; // 10M
    if (setsockopt(fd(), SOL_SOCKET, SO_SNDBUF, (void*)&sndbuf_size, sizeof(sndbuf_size)) < 0) {
        srs_warn("set sock opt SO_SNDBUFFORCE failed");
    }

    opt_len = sizeof(sndbuf_size);
    getsockopt(fd(), SOL_SOCKET, SO_SNDBUF, (void*)&sndbuf_size, &opt_len);
    srs_trace("udp remux socket sndbuf=%d", sndbuf_size);

    int rcvbuf_size = 0;
    opt_len = sizeof(rcvbuf_size);
    getsockopt(fd(), SOL_SOCKET, SO_RCVBUF, (void*)&rcvbuf_size, &opt_len);
    srs_trace("default udp remux socket rcvbuf=%d", rcvbuf_size);

    rcvbuf_size = 1024*1024*10; // 10M
    if (setsockopt(fd(), SOL_SOCKET, SO_RCVBUF, (void*)&rcvbuf_size, sizeof(rcvbuf_size)) < 0) {
        srs_warn("set sock opt SO_RCVBUFFORCE failed");
    }

    opt_len = sizeof(rcvbuf_size);
    getsockopt(fd(), SOL_SOCKET, SO_RCVBUF, (void*)&rcvbuf_size, &opt_len);
    srs_trace("udp remux socket rcvbuf=%d", rcvbuf_size);
}

srs_error_t SrsUdpMuxListener::cycle()
{
    srs_error_t err = srs_success;
    
    while (true) {
        if ((err = trd->pull()) != srs_success) {
            return srs_error_wrap(err, "udp listener");
        }   

        SrsUdpMuxSocket skt(lfd);

        int nread = skt.recvfrom(SRS_UTIME_NO_TIMEOUT);
        if (nread <= 0) {
            if (nread < 0) {
                srs_warn("udp recv error");
            }
            // remux udp never return
            continue;
        }   
    
        if ((err = handler->on_udp_packet(&skt)) != srs_success) {
            // remux udp never return
            srs_warn("udp packet handler error:%s", srs_error_desc(err).c_str());
            srs_error_reset(err);
            continue;
        }   
    
        if (SrsUdpPacketRecvCycleInterval > 0) {
            srs_usleep(SrsUdpPacketRecvCycleInterval);
        }   
    }   
    
    return err;
}

