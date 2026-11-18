//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_PROTOCOL_SRT_HPP
#define SRS_PROTOCOL_SRT_HPP

#include <srs_core.hpp>
#include <srs_protocol_st.hpp>

#include <map>
#include <vector>

class SrsSrtSocket;

extern srs_error_t srs_srt_log_initialize();

// Stop libsrt and join its worker threads, before the process destroys the libsrt global objects.
extern void srs_srt_cleanup();

extern srs_srt_t srs_srt_socket_invalid();

extern srs_error_t srs_srt_socket(srs_srt_t *pfd);
extern srs_error_t srs_srt_close(srs_srt_t fd);

extern srs_error_t srs_srt_socket_with_default_option(srs_srt_t *pfd);

extern srs_error_t srs_srt_listen(srs_srt_t srt_fd, std::string ip, int port);

extern srs_error_t srs_srt_nonblock(srs_srt_t srt_fd);

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
extern srs_error_t srs_srt_set_streamid(srs_srt_t srt_fd, const std::string &streamid);
extern srs_error_t srs_srt_set_passphrase(srs_srt_t srt_fd, const std::string &passphrase);
extern srs_error_t srs_srt_set_pbkeylen(srs_srt_t srt_fd, int pbkeylen);

extern srs_error_t srs_srt_get_maxbw(srs_srt_t srt_fd, int64_t &maxbw);
extern srs_error_t srs_srt_get_mss(srs_srt_t srt_fd, int &mss);
extern srs_error_t srs_srt_get_payload_size(srs_srt_t srt_fd, int &payload_size);
extern srs_error_t srs_srt_get_connect_timeout(srs_srt_t srt_fd, int &timeout);
extern srs_error_t srs_srt_get_peer_idle_timeout(srs_srt_t srt_fd, int &timeout);
extern srs_error_t srs_srt_get_tsbpdmode(srs_srt_t srt_fd, bool &tsbpdmode);
extern srs_error_t srs_srt_get_sndbuf(srs_srt_t srt_fd, int &sndbuf);
extern srs_error_t srs_srt_get_rcvbuf(srs_srt_t srt_fd, int &rcvbuf);
extern srs_error_t srs_srt_get_tlpktdrop(srs_srt_t srt_fd, bool &tlpktdrop);
extern srs_error_t srs_srt_get_latency(srs_srt_t srt_fd, int &latency);
extern srs_error_t srs_srt_get_rcv_latency(srs_srt_t srt_fd, int &rcv_latency);
extern srs_error_t srs_srt_get_peer_latency(srs_srt_t srt_fd, int &peer_latency);
extern srs_error_t srs_srt_get_streamid(srs_srt_t srt_fd, std::string &streamid);

extern srs_error_t srs_srt_get_local_ip_port(srs_srt_t srt_fd, std::string &ip, int &port);
extern srs_error_t srs_srt_get_remote_ip_port(srs_srt_t srt_fd, std::string &ip, int &port);

class SrsSrtStat
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    void *stat_;

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

class ISrsSrtPoller
{
public:
    ISrsSrtPoller();
    virtual ~ISrsSrtPoller();

public:
    virtual srs_error_t initialize() = 0;
    virtual srs_error_t add_socket(SrsSrtSocket *srt_skt) = 0;
    virtual srs_error_t mod_socket(SrsSrtSocket *srt_skt) = 0;
    virtual srs_error_t del_socket(SrsSrtSocket *srt_skt) = 0;
    // Under ST, always pass a timeout_ms of 0 and switch coroutines yourself.
    virtual srs_error_t wait(int timeout_ms, int *pn_fds) = 0;

public:
    virtual int size() = 0;
};
ISrsSrtPoller *srs_srt_poller_new();

class ISrsSrtOptions
{
public:
    ISrsSrtOptions();
    virtual ~ISrsSrtOptions();

public:
    virtual srs_error_t set_maxbw(srs_srt_t srt_fd, int64_t maxbw) = 0;
    virtual srs_error_t set_mss(srs_srt_t srt_fd, int mss) = 0;
    virtual srs_error_t set_payload_size(srs_srt_t srt_fd, int payload_size) = 0;
    virtual srs_error_t set_connect_timeout(srs_srt_t srt_fd, int timeout) = 0;
    virtual srs_error_t set_peer_idle_timeout(srs_srt_t srt_fd, int timeout) = 0;
    virtual srs_error_t set_tsbpdmode(srs_srt_t srt_fd, bool tsbpdmode) = 0;
    virtual srs_error_t set_sndbuf(srs_srt_t srt_fd, int sndbuf) = 0;
    virtual srs_error_t set_rcvbuf(srs_srt_t srt_fd, int rcvbuf) = 0;
    virtual srs_error_t set_tlpktdrop(srs_srt_t srt_fd, bool tlpktdrop) = 0;
    virtual srs_error_t set_latency(srs_srt_t srt_fd, int latency) = 0;
    virtual srs_error_t set_rcv_latency(srs_srt_t srt_fd, int rcv_latency) = 0;
    virtual srs_error_t set_peer_latency(srs_srt_t srt_fd, int peer_latency) = 0;
    virtual srs_error_t set_passphrase(srs_srt_t srt_fd, const std::string &passphrase) = 0;
    virtual srs_error_t set_pbkeylen(srs_srt_t srt_fd, int pbkeylen) = 0;
};

class SrsSrtOptions : public ISrsSrtOptions
{
public:
    SrsSrtOptions();
    virtual ~SrsSrtOptions();

public:
    virtual srs_error_t set_maxbw(srs_srt_t srt_fd, int64_t maxbw);
    virtual srs_error_t set_mss(srs_srt_t srt_fd, int mss);
    virtual srs_error_t set_payload_size(srs_srt_t srt_fd, int payload_size);
    virtual srs_error_t set_connect_timeout(srs_srt_t srt_fd, int timeout);
    virtual srs_error_t set_peer_idle_timeout(srs_srt_t srt_fd, int timeout);
    virtual srs_error_t set_tsbpdmode(srs_srt_t srt_fd, bool tsbpdmode);
    virtual srs_error_t set_sndbuf(srs_srt_t srt_fd, int sndbuf);
    virtual srs_error_t set_rcvbuf(srs_srt_t srt_fd, int rcvbuf);
    virtual srs_error_t set_tlpktdrop(srs_srt_t srt_fd, bool tlpktdrop);
    virtual srs_error_t set_latency(srs_srt_t srt_fd, int latency);
    virtual srs_error_t set_rcv_latency(srs_srt_t srt_fd, int rcv_latency);
    virtual srs_error_t set_peer_latency(srs_srt_t srt_fd, int peer_latency);
    virtual srs_error_t set_passphrase(srs_srt_t srt_fd, const std::string &passphrase);
    virtual srs_error_t set_pbkeylen(srs_srt_t srt_fd, int pbkeylen);
};

class ISrsSrtSocket
{
public:
    ISrsSrtSocket();
    virtual ~ISrsSrtSocket();

public:
    virtual srs_error_t recvmsg(void *buf, size_t size, ssize_t *nread) = 0;
    virtual srs_error_t sendmsg(void *buf, size_t size, ssize_t *nwrite) = 0;
    virtual void set_recv_timeout(srs_utime_t tm) = 0;
    virtual void set_send_timeout(srs_utime_t tm) = 0;
    virtual srs_utime_t get_send_timeout() = 0;
    virtual srs_utime_t get_recv_timeout() = 0;
    virtual int64_t get_send_bytes() = 0;
    virtual int64_t get_recv_bytes() = 0;
};

class SrsSrtSocket : public ISrsSrtSocket
{
public:
    SrsSrtSocket(ISrsSrtPoller *srt_poller, srs_srt_t srt_fd);
    virtual ~SrsSrtSocket();

public:
    srs_error_t connect(const std::string &ip, int port);
    srs_error_t accept(srs_srt_t *client_srt_fd);
    srs_error_t recvmsg(void *buf, size_t size, ssize_t *nread);
    srs_error_t sendmsg(void *buf, size_t size, ssize_t *nwrite);

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
    srs_error_t wait_readable();
    srs_error_t wait_writeable();
    void notify_readable();
    void notify_writeable();
    void notify_error();

public:
    srs_error_t enable_read();
    srs_error_t disable_read();
    srs_error_t enable_write();
    srs_error_t disable_write();

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srs_error_t enable_event(int event);
    srs_error_t disable_event(int event);
    srs_error_t check_error();

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srs_srt_t srt_fd_;
    bool has_error_;
    srs_cond_t read_cond_;
    srs_cond_t write_cond_;

    srs_utime_t recv_timeout_;
    srs_utime_t send_timeout_;

    int64_t recv_bytes_;
    int64_t send_bytes_;

    int events_;
    ISrsSrtPoller *srt_poller_;
};

#endif
