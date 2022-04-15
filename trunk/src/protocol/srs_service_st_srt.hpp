//
// Copyright (c) 2013-2021 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#ifndef SRS_SERVICE_ST_SRT_HPP
#define SRS_SERVICE_ST_SRT_HPP

#include <srs_core.hpp>
#include <srs_service_st.hpp>

#include <map>
#include <vector>

#include <srt/srt.h>

// Create srt socket only, with libsrt's default option.
extern srs_error_t srs_srt_socket(SRTSOCKET* pfd);

// Create srt socket with srs recommend default option(tsbpdmode=false,tlpktdrop=false,latency=0,sndsyn=0,rcvsyn=0)
extern srs_error_t srs_srt_socket_with_default_option(SRTSOCKET* pfd);

// For server, listen at SRT endpoint.
extern srs_error_t srs_srt_listen(SRTSOCKET srt_fd, std::string ip, int port);

// Set read/write no block.
extern srs_error_t srs_srt_nonblock(SRTSOCKET srt_fd);

// Set SRT options.
extern srs_error_t srs_srt_set_maxbw(SRTSOCKET srt_fd, int maxbw);
extern srs_error_t srs_srt_set_mss(SRTSOCKET srt_fd, int mss);
extern srs_error_t srs_srt_set_payload_size(SRTSOCKET srt_fd, int payload_size);
extern srs_error_t srs_srt_set_connect_timeout(SRTSOCKET srt_fd, int timeout);
extern srs_error_t srs_srt_set_peer_idle_timeout(SRTSOCKET srt_fd, int timeout);
extern srs_error_t srs_srt_set_tsbpdmode(SRTSOCKET srt_fd, bool tsbpdmode);
extern srs_error_t srs_srt_set_sndbuf(SRTSOCKET srt_fd, int sndbuf);
extern srs_error_t srs_srt_set_rcvbuf(SRTSOCKET srt_fd, int rcvbuf);
extern srs_error_t srs_srt_set_tlpktdrop(SRTSOCKET srt_fd, bool tlpktdrop);
extern srs_error_t srs_srt_set_latency(SRTSOCKET srt_fd, int latency);
extern srs_error_t srs_srt_set_rcv_latency(SRTSOCKET srt_fd, int rcv_latency);
extern srs_error_t srs_srt_set_peer_latency(SRTSOCKET srt_fd, int peer_latency);
extern srs_error_t srs_srt_set_streamid(SRTSOCKET srt_fd, const std::string& streamid);

// Get SRT options.
extern srs_error_t srs_srt_get_maxbw(SRTSOCKET srt_fd, int& maxbw);
extern srs_error_t srs_srt_get_mss(SRTSOCKET srt_fd, int& mss);
extern srs_error_t srs_srt_get_payload_size(SRTSOCKET srt_fd, int& payload_size);
extern srs_error_t srs_srt_get_connect_timeout(SRTSOCKET srt_fd, int& timeout);
extern srs_error_t srs_srt_get_peer_idle_timeout(SRTSOCKET srt_fd, int& timeout);
extern srs_error_t srs_srt_get_tsbpdmode(SRTSOCKET srt_fd, bool& tsbpdmode);
extern srs_error_t srs_srt_get_sndbuf(SRTSOCKET srt_fd, int& sndbuf);
extern srs_error_t srs_srt_get_rcvbuf(SRTSOCKET srt_fd, int& rcvbuf);
extern srs_error_t srs_srt_get_tlpktdrop(SRTSOCKET srt_fd, bool& tlpktdrop);
extern srs_error_t srs_srt_get_latency(SRTSOCKET srt_fd, int& latency);
extern srs_error_t srs_srt_get_rcv_latency(SRTSOCKET srt_fd, int& rcv_latency);
extern srs_error_t srs_srt_get_peer_latency(SRTSOCKET srt_fd, int& peer_latency);
extern srs_error_t srs_srt_get_streamid(SRTSOCKET srt_fd, std::string& streamid);

// Get SRT socket info.
extern srs_error_t srs_srt_get_local_ip_port(SRTSOCKET srt_fd, std::string& ip, int& port);
extern srs_error_t srs_srt_get_remote_ip_port(SRTSOCKET srt_fd, std::string& ip, int& port);

class SrsSrtSocket;

// Srt poller, subscribe/unsubscribed events and wait them fired.
class SrsSrtPoller 
{
public:
    SrsSrtPoller();
    virtual ~SrsSrtPoller();
public:
    srs_error_t initialize();
    srs_error_t add_socket(SrsSrtSocket* srt_skt);
    srs_error_t mod_socket(SrsSrtSocket* srt_skt);
    srs_error_t del_socket(SrsSrtSocket* srt_skt);
    srs_error_t wait(int timeout_ms);
private:
    // Find SrsSrtSocket* context by SRTSOCKET.
    std::map<SRTSOCKET, SrsSrtSocket*> fd_sockets_;
    int srt_epoller_fd_;
    std::vector<SRT_EPOLL_EVENT> events_;
};

// Srt ST socket, wrap SRT io and make it adapt to ST-thread.
class SrsSrtSocket
{
public:
    SrsSrtSocket(SrsSrtPoller* srt_poller, SRTSOCKET srt_fd);
    virtual ~SrsSrtSocket();
public: // IO API
    srs_error_t connect(const std::string& ip, int port);
    srs_error_t accept(SRTSOCKET* client_srt_fd);
    srs_error_t recvmsg(void* buf, size_t size, ssize_t* nread);
    srs_error_t sendmsg(void* buf, size_t size, ssize_t* nwrite);
public:
    SRTSOCKET fd() const { return srt_fd_; }
    int events() const { return events_; }
public:
    void set_recv_timeout(srs_utime_t tm) { recv_timeout_ = tm; }
    void set_send_timeout(srs_utime_t tm) { send_timeout_ = tm; }
    srs_utime_t get_send_timeout() { return send_timeout_; }
    srs_utime_t get_recv_timeout() { return recv_timeout_; }
    int64_t get_send_bytes() { return send_bytes_; }
    int64_t get_recv_bytes() { return recv_bytes_; }
    // Yiled coroutine and wait this socket readable.
    srs_error_t wait_readable();
    // Yiled coroutine and wait this socket writeable.
    srs_error_t wait_writeable();
    // Notify this socket readable, and resume coroutine later.
    void notify_readable();
    // Notify this socket writeable, and resume coroutine later.
    void notify_writeable();
    // Notify this socket error, resume coroutine later and error will return in all the operator of this socket.
    void notify_error();
public:
    // Subscribed IN/ERR event to srt poller.
    srs_error_t enable_read();
    // Unsubscribed IN event to srt poller.
    srs_error_t disable_read();
    // Subscribed OUT/ERR event to srt poller.
    srs_error_t enable_write();
    // Unsubscribed OUT event to srt poller.
    srs_error_t disable_write();
private:
    srs_error_t enable_event(int event);
    srs_error_t disable_event(int event);
    srs_error_t check_error();

private:
    SRTSOCKET srt_fd_;
    // Mark if some error occured in srt socket.
    bool has_error_;
    // When read operator like recvmsg/accept would block, wait this condition timeout or notified, 
    // and the coroutine itself will be yiled and resume when condition be notified.
    srs_cond_t read_cond_;
    // When write operator like sendmsg/connectt would block, wait this condition timeout or notified, 
    // and the coroutine itself will be yiled and resume when condition be notified.
    srs_cond_t write_cond_;

    srs_utime_t recv_timeout_;
    srs_utime_t send_timeout_;

    int64_t recv_bytes_;
    int64_t send_bytes_;

    // Event of this socket subscribed.
    int events_;
    // Srt poller which this socket attach to.
    SrsSrtPoller* srt_poller_;
};

#endif

