//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#ifndef SRS_APP_SRT_CONN_HPP
#define SRS_APP_SRT_CONN_HPP

#include <srs_core.hpp>

#include <string>
#include <vector>

#include <srs_protocol_srt.hpp>
#include <srs_app_st.hpp>
#include <srs_app_conn.hpp>
#include <srs_app_srt_utility.hpp>

class SrsBuffer;
class SrsLiveSource;
class SrsSrtSource;
class SrsSrtServer;
class SrsNetworkDelta;

// The basic connection of SRS, for SRT based protocols,
// all srt connections accept from srt listener must extends from this base class,
// srt server will add the connection to manager, and delete it when remove.
class SrsSrtConnection : public ISrsProtocolReadWriter
{
public:
    SrsSrtConnection(srs_srt_t srt_fd);
    virtual ~SrsSrtConnection();
public:
    virtual srs_error_t initialize();
// Interface ISrsProtocolReadWriter
public:
    virtual void set_recv_timeout(srs_utime_t tm);
    virtual srs_utime_t get_recv_timeout();
    virtual srs_error_t read_fully(void* buf, size_t size, ssize_t* nread);
    virtual int64_t get_recv_bytes();
    virtual int64_t get_send_bytes();
    virtual srs_error_t read(void* buf, size_t size, ssize_t* nread);
    virtual void set_send_timeout(srs_utime_t tm);
    virtual srs_utime_t get_send_timeout();
    virtual srs_error_t write(void* buf, size_t size, ssize_t* nwrite);
    virtual srs_error_t writev(const iovec *iov, int iov_size, ssize_t* nwrite);
private:
    // The underlayer srt fd handler.
    srs_srt_t srt_fd_;
    // The underlayer srt socket.
    SrsSrtSocket* srt_skt_;
};

class SrsSrtRecvThread : public ISrsCoroutineHandler
{
public:
    SrsSrtRecvThread(SrsSrtConnection* srt_conn);
    ~SrsSrtRecvThread();
// Interface ISrsCoroutineHandler
public:
    virtual srs_error_t cycle();
private:
    srs_error_t do_cycle();
public:
    srs_error_t start();
    srs_error_t get_recv_err();
private:
    SrsSrtConnection* srt_conn_;
    SrsCoroutine* trd_;
    srs_error_t recv_err_;
};

class SrsMpegtsSrtConn : public ISrsConnection, public ISrsStartable, public ISrsCoroutineHandler, public ISrsExpire
{
public:
    SrsMpegtsSrtConn(SrsSrtServer* srt_server, srs_srt_t srt_fd, std::string ip, int port);
    virtual ~SrsMpegtsSrtConn();
// Interface ISrsResource.
public:
    virtual std::string desc();
public:
    ISrsKbpsDelta* delta();
// Interface ISrsExpire
public:
    virtual void expire();
public:
    virtual srs_error_t start();
// Interface ISrsConnection.
public:
    virtual std::string remote_ip();
    virtual const SrsContextId& get_id();
// Interface ISrsCoroutineHandler
public:
    virtual srs_error_t cycle();
protected:
    virtual srs_error_t do_cycle();
private:
    srs_error_t publishing();
    srs_error_t playing();
    srs_error_t acquire_publish();
    void release_publish();
    srs_error_t do_publishing();
    srs_error_t do_playing();
private:
    srs_error_t on_srt_packet(char* buf, int nb_buf);
private:
    srs_error_t http_hooks_on_connect();
    void http_hooks_on_close();
    srs_error_t http_hooks_on_publish();
    void http_hooks_on_unpublish();
    srs_error_t http_hooks_on_play();
    void http_hooks_on_stop();
private:
    SrsSrtServer* srt_server_;
    srs_srt_t srt_fd_;
    SrsSrtConnection* srt_conn_;
    SrsNetworkDelta* delta_;
    SrsNetworkKbps* kbps_;
    std::string ip_;
    int port_;
    SrsCoroutine* trd_;

    SrsRequest* req_;
    SrsSrtSource* srt_source_;
};

#endif

