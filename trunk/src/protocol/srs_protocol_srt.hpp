//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#ifndef SRS_PROTOCOL_SRT_HPP
#define SRS_PROTOCOL_SRT_HPP

#include <srs_core.hpp>
#include <srs_protocol_st.hpp>

#include <map>
#include <vector>

class SrsSrtSocket;

extern srs_error_t srs_srt_log_initialize();

typedef int srs_srt_t;
extern srs_srt_t srs_srt_socket_invalid();

// Create srt socket only, with libsrt's default option.
extern srs_error_t srs_srt_socket(srs_srt_t* pfd);
extern srs_error_t srs_srt_close(srs_srt_t fd);

// Create srt socket with srs recommend default option(tsbpdmode=false,tlpktdrop=false,latency=0,sndsyn=0,rcvsyn=0)
extern srs_error_t srs_srt_socket_with_default_option(srs_srt_t* pfd);

// For server, listen at SRT endpoint.
extern srs_error_t srs_srt_listen(srs_srt_t srt_fd, std::string ip, int port);

// Set read/write no block.
extern srs_error_t srs_srt_nonblock(srs_srt_t srt_fd);

// Set SRT options.
extern srs_error_t srs_srt_set_maxbw(srs_srt_t srt_fd, int64_t maxbw);
extern srs_error_t srs_srt_set_mss(srs_srt_t srt_fd, int mss);
extern srs_error_t srs_srt_set_payload_size(srs_srt_t srt_fd, int payload_size);
extern srs_error_t srs_srt_set_connect_timeout(srs_srt_t srt_fd, int timeout);
extern srs_error_t srs_srt_set_peer_idle_timeout(srs_srt_t srt_fd, int timeout);
extern srs_error_t srs_srt_set_tsbpdmode(srs_srt_t srt_fd, bool tsbpdmode);
extern srs_error_t srs_srt_set_sndbuf(srs_srt_t srt_fd, int sndbuf);
extern srs_error_t srs_srt_set_rcvbuf(srs_srt_t srt_fd, int rcvbuf);
extern srs_error_t srs_srt_set_tlpktdrop(srs_srt_t srt_fd, bool tlpktdrop);
extern srs_error_t srs_srt_set_latency(srs_srt_t srt_fd, int latency);
extern srs_error_t srs_srt_set_rcv_latency(srs_srt_t srt_fd, int rcv_latency);
extern srs_error_t srs_srt_set_peer_latency(srs_srt_t srt_fd, int peer_latency);
extern srs_error_t srs_srt_set_streamid(srs_srt_t srt_fd, const std::string& streamid);
extern srs_error_t srs_srt_set_passphrase(srs_srt_t srt_fd, const std::string& passphrase);
extern srs_error_t srs_srt_set_pbkeylen(srs_srt_t srt_fd, int pbkeylen);

// Get SRT options.
extern srs_error_t srs_srt_get_maxbw(srs_srt_t srt_fd, int64_t& maxbw);
extern srs_error_t srs_srt_get_mss(srs_srt_t srt_fd, int& mss);
extern srs_error_t srs_srt_get_payload_size(srs_srt_t srt_fd, int& payload_size);
extern srs_error_t srs_srt_get_connect_timeout(srs_srt_t srt_fd, int& timeout);
extern srs_error_t srs_srt_get_peer_idle_timeout(srs_srt_t srt_fd, int& timeout);
extern srs_error_t srs_srt_get_tsbpdmode(srs_srt_t srt_fd, bool& tsbpdmode);
extern srs_error_t srs_srt_get_sndbuf(srs_srt_t srt_fd, int& sndbuf);
extern srs_error_t srs_srt_get_rcvbuf(srs_srt_t srt_fd, int& rcvbuf);
extern srs_error_t srs_srt_get_tlpktdrop(srs_srt_t srt_fd, bool& tlpktdrop);
extern srs_error_t srs_srt_get_latency(srs_srt_t srt_fd, int& latency);
extern srs_error_t srs_srt_get_rcv_latency(srs_srt_t srt_fd, int& rcv_latency);
extern srs_error_t srs_srt_get_peer_latency(srs_srt_t srt_fd, int& peer_latency);
extern srs_error_t srs_srt_get_streamid(srs_srt_t srt_fd, std::string& streamid);

// Get SRT socket info.
extern srs_error_t srs_srt_get_local_ip_port(srs_srt_t srt_fd, std::string& ip, int& port);
extern srs_error_t srs_srt_get_remote_ip_port(srs_srt_t srt_fd, std::string& ip, int& port);

// Get SRT stats.
class SrsSrtStat
{
private:
    void* stat_;
public:
    SrsSrtStat();
    virtual ~SrsSrtStat();
public:
    int64_t pktRecv();
    int pktRcvLoss();
    int pktRcvRetrans();
    int pktRcvDrop();
public:
    int64_t pktSent();
    int pktSndLoss();
    int pktRetrans();
    int pktSndDrop();
public:
    srs_error_t fetch(srs_srt_t srt_fd, bool clear);
};

// Srt poller, subscribe/unsubscribed events and wait them fired.
class ISrsSrtPoller
{
public:
    ISrsSrtPoller();
    virtual ~ISrsSrtPoller();
public:
    virtual srs_error_t initialize() = 0;
    virtual srs_error_t add_socket(SrsSrtSocket* srt_skt) = 0;
    virtual srs_error_t mod_socket(SrsSrtSocket* srt_skt) = 0;
    virtual srs_error_t del_socket(SrsSrtSocket* srt_skt) = 0;
    // Wait for the fds in its epoll to be fired in specified timeout_ms, where the pn_fds is the number of active fds.
    // Note that for ST, please always use timeout_ms(0) and switch coroutine by yourself.
    virtual srs_error_t wait(int timeout_ms, int* pn_fds) = 0;
public:
    virtual int size() = 0;
};
ISrsSrtPoller* srs_srt_poller_new();

// Srt ST socket, wrap SRT io and make it adapt to ST-thread.
class SrsSrtSocket
{
public:
    SrsSrtSocket(ISrsSrtPoller* srt_poller, srs_srt_t srt_fd);
    virtual ~SrsSrtSocket();
public: // IO API
    srs_error_t connect(const std::string& ip, int port);
    srs_error_t accept(srs_srt_t* client_srt_fd);
    srs_error_t recvmsg(void* buf, size_t size, ssize_t* nread);
    srs_error_t sendmsg(void* buf, size_t size, ssize_t* nwrite);
public:
    srs_srt_t fd() const { return srt_fd_; }
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
    srs_srt_t srt_fd_;
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
    ISrsSrtPoller* srt_poller_;
};

#endif

