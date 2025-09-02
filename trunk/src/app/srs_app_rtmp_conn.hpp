//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_APP_RTMP_CONN_HPP
#define SRS_APP_RTMP_CONN_HPP

#include <srs_core.hpp>

#include <string>

#include <srs_app_conn.hpp>
#include <srs_app_reload.hpp>
#include <srs_app_st.hpp>
#include <srs_core_autofree.hpp>
#include <srs_protocol_rtmp_conn.hpp>
#include <srs_protocol_rtmp_stack.hpp>

class SrsServer;
class SrsRtmpServer;
class ISrsRequest;
class SrsResponse;
class SrsLiveSource;
class SrsRefer;
class SrsLiveConsumer;
class SrsRtmpCommonMessage;
class SrsStSocket;
class SrsHttpHooks;
class SrsBandwidth;
class SrsKbps;
class SrsRtmpClient;
class SrsMediaPacket;
class SrsQueueRecvThread;
class SrsPublishRecvThread;
class SrsSecurity;
class ISrsWakable;
class SrsRtmpCommonMessage;
class SrsRtmpCommand;
class SrsNetworkDelta;

class SrsSslConnection;

// The simple rtmp client for SRS.
class SrsSimpleRtmpClient : public SrsBasicRtmpClient
{
public:
    SrsSimpleRtmpClient(std::string u, srs_utime_t ctm, srs_utime_t stm);
    virtual ~SrsSimpleRtmpClient();

protected:
    virtual srs_error_t connect_app();
};

// Some information of client.
class SrsClientInfo
{
public:
    // The type of client, play or publish.
    SrsRtmpConnType type;
    // Whether the client connected at the edge server.
    bool edge;
    // Original request object from client.
    ISrsRequest *req;
    // Response object to client.
    SrsResponse *res;

public:
    SrsClientInfo();
    virtual ~SrsClientInfo();
};

// The base transport layer for RTMP connections over plain TCP.
class SrsRtmpTransport
{
protected:
    srs_netfd_t stfd_;
    SrsTcpConnection *skt_;

public:
    SrsRtmpTransport(srs_netfd_t c);
    virtual ~SrsRtmpTransport();

public:
    // Get the file descriptor for logging and identification
    virtual srs_netfd_t fd();
    // Get the appropriate I/O interface (TCP)
    virtual ISrsProtocolReadWriter *io();
    // Perform handshake (no-op for plain RTMP)
    virtual srs_error_t handshake();
    // Get transport type description for logging
    virtual const char *transport_type();
    // Set socket buffer size
    virtual srs_error_t set_socket_buffer(srs_utime_t buffer_v);
    // Set TCP nodelay option
    virtual srs_error_t set_tcp_nodelay(bool v);
    // Get network statistics
    virtual int64_t get_recv_bytes();
    virtual int64_t get_send_bytes();
};

// The SSL/TLS transport layer for RTMPS connections.
class SrsRtmpsTransport : public SrsRtmpTransport
{
private:
    SrsSslConnection *ssl_;

public:
    SrsRtmpsTransport(srs_netfd_t c);
    virtual ~SrsRtmpsTransport();

public:
    // Get the appropriate I/O interface (SSL)
    virtual ISrsProtocolReadWriter *io();
    // Perform SSL handshake
    virtual srs_error_t handshake();
    // Get transport type description for logging
    virtual const char *transport_type();
};

class SrsRtmpConn : public ISrsConnection, public ISrsStartable, public ISrsReloadHandler, public ISrsCoroutineHandler, public ISrsExpire
{
    // For the thread to directly access any field of connection.
    friend class SrsPublishRecvThread;

private:
    SrsServer *server;
    SrsRtmpServer *rtmp;
    SrsRefer *refer;
    SrsBandwidth *bandwidth;
    SrsSecurity *security;
    // The wakable handler, maybe NULL.
    // TODO: FIXME: Should refine the state for receiving thread.
    ISrsWakable *wakable;
    // The elapsed duration in srs_utime_t
    // For live play duration, for instance, rtmpdump to record.
    srs_utime_t duration;
    // The MR(merged-write) sleep time in srs_utime_t.
    srs_utime_t mw_sleep;
    int mw_msgs;
    // For realtime
    // @see https://github.com/ossrs/srs/issues/257
    bool realtime;
    // The minimal interval in srs_utime_t for delivery stream.
    srs_utime_t send_min_interval;
    // The publish 1st packet timeout in srs_utime_t
    srs_utime_t publish_1stpkt_timeout;
    // The publish normal packet timeout in srs_utime_t
    srs_utime_t publish_normal_timeout;
    // Whether enable the tcp_nodelay.
    bool tcp_nodelay;
    // About the rtmp client.
    SrsClientInfo *info;

private:
    SrsRtmpTransport *transport_;
    // Each connection start a green thread,
    // when thread stop, the connection will be delete by server.
    SrsCoroutine *trd;
    // The manager object to manage the connection.
    ISrsResourceManager *manager;
    // The ip and port of client.
    std::string ip;
    int port;
    // The delta for statistic.
    SrsNetworkDelta *delta_;
    SrsNetworkKbps *kbps;
    // The create time in milliseconds.
    // for current connection to log self create time and calculate the living time.
    int64_t create_time;

public:
    SrsRtmpConn(SrsServer *svr, SrsRtmpTransport *transport, std::string cip, int port);
    virtual ~SrsRtmpConn();
    // Interface ISrsResource.
public:
    virtual std::string desc();

protected:
    virtual srs_error_t do_cycle();

public:
    virtual ISrsKbpsDelta *delta();

private:
    // When valid and connected to vhost/app, service the client.
    virtual srs_error_t service_cycle();
    // The stream(play/publish) service cycle, identify client first.
    virtual srs_error_t stream_service_cycle();
    virtual srs_error_t check_vhost(bool try_default_vhost);
    virtual srs_error_t playing(SrsSharedPtr<SrsLiveSource> source);
    virtual srs_error_t do_playing(SrsSharedPtr<SrsLiveSource> source, SrsLiveConsumer *consumer, SrsQueueRecvThread *trd);
    virtual srs_error_t publishing(SrsSharedPtr<SrsLiveSource> source);
    virtual srs_error_t do_publishing(SrsSharedPtr<SrsLiveSource> source, SrsPublishRecvThread *trd);
    virtual srs_error_t acquire_publish(SrsSharedPtr<SrsLiveSource> source);
    virtual void release_publish(SrsSharedPtr<SrsLiveSource> source);
    virtual srs_error_t handle_publish_message(SrsSharedPtr<SrsLiveSource> &source, SrsRtmpCommonMessage *msg);
    virtual srs_error_t process_publish_message(SrsSharedPtr<SrsLiveSource> &source, SrsRtmpCommonMessage *msg);
    virtual srs_error_t process_play_control_msg(SrsLiveConsumer *consumer, SrsRtmpCommonMessage *msg);
    virtual void set_sock_options();

private:
    virtual srs_error_t check_edge_token_traverse_auth();
    virtual srs_error_t do_token_traverse_auth(SrsRtmpClient *client);

private:
    // When the connection disconnect, call this method.
    // e.g. log msg of connection and report to other system.
    virtual srs_error_t on_disconnect();

private:
    virtual srs_error_t http_hooks_on_connect();
    virtual void http_hooks_on_close();
    virtual srs_error_t http_hooks_on_publish();
    virtual void http_hooks_on_unpublish();
    virtual srs_error_t http_hooks_on_play();
    virtual void http_hooks_on_stop();
    // Extract APIs from SrsTcpConnection.
    // Interface ISrsStartable
public:
    // Start the client green thread.
    // when server get a client from listener,
    // 1. server will create an concrete connection(for instance, RTMP connection),
    // 2. then add connection to its connection manager,
    // 3. start the client thread by invoke this start()
    // when client cycle thread stop, invoke the on_thread_stop(), which will use server
    // To remove the client by server->remove(this).
    virtual srs_error_t start();
    // Interface ISrsCoroutineHandler
public:
    // The thread cycle function,
    // when serve connection completed, terminate the loop which will terminate the thread,
    // thread will invoke the on_thread_stop() when it terminated.
    virtual srs_error_t cycle();
    // Interface ISrsConnection.
public:
    virtual std::string remote_ip();
    virtual const SrsContextId &get_id();
    // Interface ISrsExpire.
public:
    virtual void expire();
};

#endif
