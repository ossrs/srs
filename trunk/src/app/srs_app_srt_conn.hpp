//
// Copyright (c) 2013-2026 The SRS Authors
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
class ISrsAppFactory;
class ISrsContext;
class SrsSrtStat;
class ISrsSrtEventLoop;

class ISrsSrtConnection : public ISrsProtocolReadWriter
{
public:
    ISrsSrtConnection();
    virtual ~ISrsSrtConnection();

public:
    virtual void assemble() = 0;
    virtual srs_srt_t srtfd() = 0;
    virtual srs_error_t get_streamid(std::string &streamid) = 0;
    virtual srs_error_t get_stats(SrsSrtStat &stat) = 0;
};

class SrsSrtConnection : public ISrsSrtConnection
{
public:
    SrsSrtConnection(srs_srt_t srt_fd);
    virtual ~SrsSrtConnection();

public:
    virtual srs_error_t initialize();

public:
    virtual void assemble(); // Not in the constructor, to avoid calling functions there.
    virtual srs_srt_t srtfd();
    virtual srs_error_t get_streamid(std::string &streamid);
    virtual srs_error_t get_stats(SrsSrtStat &stat);

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
    srs_srt_t srt_fd_;
    ISrsSrtSocket *srt_skt_;
    ISrsSrtEventLoop *srt_eventloop_;
};

class ISrsSrtRecvThread : public ISrsCoroutineHandler
{
public:
    ISrsSrtRecvThread();
    virtual ~ISrsSrtRecvThread();

public:
};

class SrsSrtRecvThread : public ISrsSrtRecvThread
{
public:
    SrsSrtRecvThread(ISrsProtocolReadWriter *srt_conn);
    void assemble(); // Not in the constructor, to avoid calling functions there.
    ~SrsSrtRecvThread();

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
    ISrsAppFactory *app_factory_;
    ISrsContext *context_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsProtocolReadWriter *srt_conn_;
    ISrsCoroutine *trd_;
    srs_error_t recv_err_;
};

class ISrsMpegtsSrtConnection : public ISrsConnection,
                                public ISrsStartable,
                                public ISrsCoroutineHandler,
                                public ISrsExpire
{
public:
    ISrsMpegtsSrtConnection();
    virtual ~ISrsMpegtsSrtConnection();

public:
};

class SrsMpegtsSrtConn : public ISrsMpegtsSrtConnection
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsAppFactory *app_factory_;
    ISrsContext *context_;
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
    void assemble(); // Not in the constructor, to avoid calling functions there.

public:
    virtual std::string desc();

public:
    ISrsKbpsDelta *delta();

public:
    virtual void expire();

public:
    virtual srs_error_t start();
    virtual void stop();

public:
    virtual std::string remote_ip();
    virtual const SrsContextId &get_id();

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
    // For a socket group: log the links that joined or left since the last call, then the stats of the group and of
    // each link, as libsrt's stats of a group count neither per link nor anything but the unique packets.
    void update_group_links();
    void print_group_stats();

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
    ISrsSrtConnection *srt_conn_;
    ISrsNetworkDelta *delta_;
    SrsNetworkKbps *kbps_;
    std::string ip_;
    int port_;
    ISrsCoroutine *trd_;

    ISrsRequest *req_;
    SrsSharedPtr<SrsSrtSource> srt_source_;
    ISrsSecurity *security_;
    // The links of the socket group at the last update, none for a plain socket.
    std::vector<SrsSrtGroupLink> group_links_;
};

#endif
