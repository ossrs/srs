//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_APP_RTMP_CONN_HPP
#define SRS_APP_RTMP_CONN_HPP

#include <srs_core.hpp>

#include <string>

#include <srs_app_reload.hpp>
#include <srs_app_st.hpp>
#include <srs_core_autofree.hpp>
#include <srs_protocol_conn.hpp>
#include <srs_protocol_rtmp_conn.hpp>
#include <srs_protocol_rtmp_stack.hpp>

class SrsServer;
class SrsRtmpServer;
class ISrsRequest;
class SrsResponse;
class SrsLiveSource;
class SrsLiveConsumer;
class SrsRtmpCommonMessage;
class SrsStSocket;
class SrsHttpHooks;
class SrsBandwidth;
class SrsKbps;
class SrsMediaPacket;
class SrsQueueRecvThread;
class SrsPublishRecvThread;
class SrsSecurity;
class ISrsWakable;
class SrsRtmpCommonMessage;
class SrsRtmpCommand;
class SrsNetworkDelta;
class ISrsNetworkDelta;
class ISrsAppConfig;
class SrsSslConnection;
class ISrsResourceManager;
class ISrsStreamPublishTokenManager;
class ISrsLiveSourceManager;
class ISrsStatistic;
class ISrsHttpHooks;
class ISrsAppFactory;
class ISrsRtcSourceManager;
class ISrsSrtSourceManager;
class ISrsRtspSourceManager;
class ISrsRtmpServer;
class ISrsRtmpTransport;
class ISrsSecurity;
class ISrsContext;

class SrsSimpleRtmpClient : public SrsBasicRtmpClient
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsAppConfig *config_;

public:
    SrsSimpleRtmpClient(std::string u, srs_utime_t ctm, srs_utime_t stm);
    virtual ~SrsSimpleRtmpClient();

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual srs_error_t connect_app();
};

class SrsClientInfo
{
public:
    SrsRtmpConnType type_;
    bool edge_;
    ISrsRequest *req_;
    SrsResponse *res_;

public:
    SrsClientInfo();
    virtual ~SrsClientInfo();
};

class ISrsRtmpTransport
{
public:
    ISrsRtmpTransport();
    virtual ~ISrsRtmpTransport();

public:
    virtual srs_netfd_t fd() = 0;
    virtual int osfd() = 0;
    virtual ISrsProtocolReadWriter *io() = 0;
    virtual srs_error_t handshake() = 0;
    virtual const char *transport_type() = 0;
    virtual srs_error_t set_socket_buffer(srs_utime_t buffer_v) = 0;
    virtual srs_error_t set_tcp_nodelay(bool v) = 0;
    virtual int64_t get_recv_bytes() = 0;
    virtual int64_t get_send_bytes() = 0;
};

class SrsRtmpTransport : public ISrsRtmpTransport
{
// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    srs_netfd_t stfd_;
    SrsTcpConnection *skt_;

public:
    SrsRtmpTransport(srs_netfd_t c);
    virtual ~SrsRtmpTransport();

public:
    virtual srs_netfd_t fd();
    virtual int osfd();
    virtual ISrsProtocolReadWriter *io();
    virtual srs_error_t handshake();
    virtual const char *transport_type();
    virtual srs_error_t set_socket_buffer(srs_utime_t buffer_v);
    virtual srs_error_t set_tcp_nodelay(bool v);
    virtual int64_t get_recv_bytes();
    virtual int64_t get_send_bytes();
};

class SrsRtmpsTransport : public SrsRtmpTransport
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsAppConfig *config_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    SrsSslConnection *ssl_;

public:
    SrsRtmpsTransport(srs_netfd_t c);
    virtual ~SrsRtmpsTransport();

public:
    virtual ISrsProtocolReadWriter *io();
    virtual srs_error_t handshake();
    virtual const char *transport_type();
};

class SrsRtmpConn : public ISrsConnection,
                    public ISrsStartable,
                    public ISrsReloadHandler,
                    public ISrsCoroutineHandler,
                    public ISrsExpire
{
    friend class SrsPublishRecvThread;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsAppFactory *app_factory_;
    ISrsContext *context_;
    ISrsResourceManager *manager_;
    ISrsAppConfig *config_;
    ISrsStreamPublishTokenManager *stream_publish_tokens_;
    ISrsLiveSourceManager *live_sources_;
    ISrsStatistic *stat_;
    ISrsHttpHooks *hooks_;
    ISrsRtcSourceManager *rtc_sources_;
    ISrsSrtSourceManager *srt_sources_;
#ifdef SRS_RTSP
    ISrsRtspSourceManager *rtsp_sources_;
#endif

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsRtmpServer *rtmp_;
    SrsBandwidth *bandwidth_;
    ISrsSecurity *security_;
    // May be NULL.
    // TODO: FIXME: Should refine the state for receiving thread.
    ISrsWakable *wakable_;
    // The play duration so far, for a client that asks for one, such as rtmpdump to record.
    srs_utime_t duration_;
    // MW is merged-write.
    srs_utime_t mw_sleep_;
    int mw_msgs_;
    // @see https://github.com/ossrs/srs/issues/257
    bool realtime_;
    srs_utime_t send_min_interval_;
    srs_utime_t publish_1stpkt_timeout_;
    srs_utime_t publish_normal_timeout_;
    bool tcp_nodelay_;
    // Whether the on_connect hook accepted the client, so it gets the on_close hook when it closes.
    // A client that only connects, never playing or publishing, gets neither, and one the on_connect
    // hook refused gets no on_close, as one refused by on_play gets no on_stop.
    bool on_connect_checked_;
    SrsClientInfo *info_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsRtmpTransport *transport_;
    ISrsCoroutine *trd_;
    std::string ip_;
    int port_;
    ISrsNetworkDelta *delta_;
    SrsNetworkKbps *kbps_;
    // In milliseconds.
    int64_t create_time_;

public:
    SrsRtmpConn(ISrsRtmpTransport *transport, std::string cip, int port);
    void assemble(); // Not in the constructor, to avoid calling functions there.
    virtual ~SrsRtmpConn();

public:
    virtual std::string desc();

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual srs_error_t do_cycle();

public:
    virtual ISrsKbpsDelta *delta();

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t service_cycle();
    virtual srs_error_t stream_service_cycle();
    virtual srs_error_t check_vhost(bool try_default_vhost);
    virtual srs_error_t playing(SrsSharedPtr<SrsLiveSource> source);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t redirect_to_origin_cluster(SrsSharedPtr<SrsLiveSource> source);
    // Drain what the client sent, until it is idle, so closing the connection is a FIN, not a reset that
    // drops what SRS has not sent yet.
    virtual void drain_client_input();

public:
    virtual srs_error_t do_playing(SrsSharedPtr<SrsLiveSource> source, SrsLiveConsumer *consumer, SrsQueueRecvThread *trd);
    virtual srs_error_t authorize_publish();
    virtual srs_error_t start_publish();
    virtual srs_error_t publishing(SrsSharedPtr<SrsLiveSource> source);
    virtual srs_error_t do_publishing(SrsSharedPtr<SrsLiveSource> source, SrsPublishRecvThread *trd);
    virtual srs_error_t acquire_publish(SrsSharedPtr<SrsLiveSource> source);
    virtual void release_publish(SrsSharedPtr<SrsLiveSource> source);
    virtual srs_error_t handle_publish_message(SrsSharedPtr<SrsLiveSource> &source, SrsRtmpCommonMessage *msg);
    virtual srs_error_t process_publish_message(SrsSharedPtr<SrsLiveSource> &source, SrsRtmpCommonMessage *msg);
    virtual srs_error_t process_play_control_msg(SrsLiveConsumer *consumer, SrsRtmpCommonMessage *msg);
    virtual void set_sock_options();

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t on_disconnect();

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t http_hooks_on_connect();
    virtual void http_hooks_on_close();
    virtual srs_error_t http_hooks_on_publish();
    virtual void http_hooks_on_unpublish();
    virtual srs_error_t http_hooks_on_play();
    virtual void http_hooks_on_stop();

public:
    virtual srs_error_t start();
    virtual void stop();

public:
    virtual srs_error_t cycle();

public:
    virtual std::string remote_ip();
    virtual const SrsContextId &get_id();

public:
    virtual void expire();
};

#endif
