//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_APP_SRT_CONN_HPP
#define SRS_APP_SRT_CONN_HPP

#include <srs_core.hpp>

#include <string>
#include <vector>

#include <srs_app_security.hpp>
#include <srs_app_st.hpp>
#include <srs_protocol_conn.hpp>
#include <srs_protocol_srt.hpp>
#include <srs_protocol_utility.hpp>

class SrsBuffer;
class SrsLiveSource;
class SrsSrtSource;
class SrsSrtServer;
class SrsNetworkDelta;
class ISrsNetworkDelta;
class ISrsSrtSocket;
class SrsNetworkKbps;
class ISrsSecurity;
class ISrsStatistic;
class ISrsAppConfig;
class ISrsStreamPublishTokenManager;
class ISrsSrtSourceManager;
class ISrsLiveSourceManager;
class ISrsRtcSourceManager;
class ISrsHttpHooks;

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
    virtual srs_error_t read_fully(void *buf, size_t size, ssize_t *nread);
    virtual int64_t get_recv_bytes();
    virtual int64_t get_send_bytes();
    virtual srs_error_t read(void *buf, size_t size, ssize_t *nread);
    virtual void set_send_timeout(srs_utime_t tm);
    virtual srs_utime_t get_send_timeout();
    virtual srs_error_t write(void *buf, size_t size, ssize_t *nwrite);
    virtual srs_error_t writev(const iovec *iov, int iov_size, ssize_t *nwrite);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // The underlayer srt fd handler.
    srs_srt_t srt_fd_;
    // The underlayer srt socket.
    ISrsSrtSocket *srt_skt_;
};

// The recv thread for SRT connection.
class ISrsSrtRecvThread : public ISrsCoroutineHandler
{
public:
    ISrsSrtRecvThread();
    virtual ~ISrsSrtRecvThread();

public:
};

// The recv thread for SRT connection.
class SrsSrtRecvThread : public ISrsSrtRecvThread
{
public:
    SrsSrtRecvThread(ISrsProtocolReadWriter *srt_conn);
    ~SrsSrtRecvThread();
    // Interface ISrsCoroutineHandler
public:
    virtual srs_error_t cycle();

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srs_error_t do_cycle();

public:
    srs_error_t start();
    srs_error_t get_recv_err();

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsProtocolReadWriter *srt_conn_;
    ISrsCoroutine *trd_;
    srs_error_t recv_err_;
};

// The SRT connection, for client to publish or play stream.
class ISrsMpegtsSrtConnection : public ISrsConnection, // It's a resource.
                                public ISrsStartable,
                                public ISrsCoroutineHandler,
                                public ISrsExpire
{
public:
    ISrsMpegtsSrtConnection();
    virtual ~ISrsMpegtsSrtConnection();

public:
};

// The SRT connection, for client to publish or play stream.
class SrsMpegtsSrtConn : public ISrsMpegtsSrtConnection
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsStatistic *stat_;
    ISrsAppConfig *config_;
    ISrsStreamPublishTokenManager *stream_publish_tokens_;
    ISrsSrtSourceManager *srt_sources_;
    ISrsLiveSourceManager *live_sources_;
    ISrsRtcSourceManager *rtc_sources_;
    ISrsHttpHooks *hooks_;

public:
    SrsMpegtsSrtConn(ISrsResourceManager *resource_manager, srs_srt_t srt_fd, std::string ip, int port);
    virtual ~SrsMpegtsSrtConn();
    // Interface ISrsResource.
public:
    virtual std::string desc();

public:
    ISrsKbpsDelta *delta();
    // Interface ISrsExpire
public:
    virtual void expire();

public:
    virtual srs_error_t start();
    // Interface ISrsConnection.
public:
    virtual std::string remote_ip();
    virtual const SrsContextId &get_id();
    // Interface ISrsCoroutineHandler
public:
    virtual srs_error_t cycle();

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual srs_error_t do_cycle();

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srs_error_t publishing();
    srs_error_t playing();
    srs_error_t acquire_publish();
    void release_publish();
    srs_error_t do_publishing();
    srs_error_t do_playing();

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srs_error_t on_srt_packet(char *buf, int nb_buf);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srs_error_t http_hooks_on_connect();
    void http_hooks_on_close();
    srs_error_t http_hooks_on_publish();
    void http_hooks_on_unpublish();
    srs_error_t http_hooks_on_play();
    void http_hooks_on_stop();

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsResourceManager *resource_manager_;
    srs_srt_t srt_fd_;
    ISrsProtocolReadWriter *srt_conn_;
    ISrsNetworkDelta *delta_;
    SrsNetworkKbps *kbps_;
    std::string ip_;
    int port_;
    ISrsCoroutine *trd_;

    ISrsRequest *req_;
    SrsSharedPtr<SrsSrtSource> srt_source_;
    ISrsSecurity *security_;
};

#endif
