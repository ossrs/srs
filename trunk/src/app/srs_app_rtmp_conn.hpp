/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2017 OSSRS(winlin)
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

#ifndef SRS_APP_RTMP_CONN_HPP
#define SRS_APP_RTMP_CONN_HPP

#include <srs_core.hpp>

#include <string>

#include <srs_app_st.hpp>
#include <srs_app_conn.hpp>
#include <srs_app_reload.hpp>
#include <srs_rtmp_stack.hpp>
#include <srs_service_rtmp_conn.hpp>

class SrsServer;
class SrsRtmpServer;
class SrsRequest;
class SrsResponse;
class SrsSource;
class SrsRefer;
class SrsConsumer;
class SrsCommonMessage;
class SrsStSocket;
class SrsHttpHooks;
class SrsBandwidth;
class SrsKbps;
class SrsRtmpClient;
class SrsSharedPtrMessage;
class SrsQueueRecvThread;
class SrsPublishRecvThread;
class SrsSecurity;
class ISrsWakable;
class SrsCommonMessage;
class SrsPacket;
#ifdef SRS_AUTO_KAFKA
class ISrsKafkaCluster;
#endif

/**
 * The simple rtmp client for SRS.
 */
class SrsSimpleRtmpClient : public SrsBasicRtmpClient
{
public:
    SrsSimpleRtmpClient(std::string u, int64_t ctm, int64_t stm);
    virtual ~SrsSimpleRtmpClient();
protected:
    virtual int connect_app();
};

/**
 * Some information of client.
 */
class SrsClientInfo
{
public:
    // The type of client, play or publish.
    SrsRtmpConnType type;
    // Whether the client connected at the edge server.
    bool edge;
    // Original request object from client.
    SrsRequest* req;
    // Response object to client.
    SrsResponse* res;
public:
    SrsClientInfo();
    virtual ~SrsClientInfo();
};

/**
 * the client provides the main logic control for RTMP clients.
 */
class SrsRtmpConn : public virtual SrsConnection, public virtual ISrsReloadHandler
{
    // for the thread to directly access any field of connection.
    friend class SrsPublishRecvThread;
private:
    SrsServer* server;
    SrsRtmpServer* rtmp;
    SrsRefer* refer;
    SrsBandwidth* bandwidth;
    SrsSecurity* security;
    // the wakable handler, maybe NULL.
    ISrsWakable* wakable;
    // elapse duration in ms
    // for live play duration, for instance, rtmpdump to record.
    // @see https://github.com/ossrs/srs/issues/47
    int64_t duration;
    SrsKbps* kbps;
    // the MR(merged-write) sleep time in ms.
    int mw_sleep;
    // the MR(merged-write) only enabled for play.
    int mw_enabled;
    // for realtime
    // @see https://github.com/ossrs/srs/issues/257
    bool realtime;
    // the minimal interval in ms for delivery stream.
    double send_min_interval;
    // publish 1st packet timeout in ms
    int publish_1stpkt_timeout;
    // publish normal packet timeout in ms
    int publish_normal_timeout;
    // whether enable the tcp_nodelay.
    bool tcp_nodelay;
    // About the rtmp client.
    SrsClientInfo* info;
public:
    SrsRtmpConn(SrsServer* svr, srs_netfd_t c, std::string cip);
    virtual ~SrsRtmpConn();
public:
    virtual void dispose();
protected:
    virtual int do_cycle();
// interface ISrsReloadHandler
public:
    virtual int on_reload_vhost_removed(std::string vhost);
    virtual int on_reload_vhost_play(std::string vhost);
    virtual int on_reload_vhost_tcp_nodelay(std::string vhost);
    virtual int on_reload_vhost_realtime(std::string vhost);
    virtual int on_reload_vhost_publish(std::string vhost);
// interface IKbpsDelta
public:
    virtual void resample();
    virtual int64_t get_send_bytes_delta();
    virtual int64_t get_recv_bytes_delta();
    virtual void cleanup();
private:
    // when valid and connected to vhost/app, service the client.
    virtual int service_cycle();
    // stream(play/publish) service cycle, identify client first.
    virtual int stream_service_cycle();
    virtual int check_vhost(bool try_default_vhost);
    virtual int playing(SrsSource* source);
    virtual int do_playing(SrsSource* source, SrsConsumer* consumer, SrsQueueRecvThread* trd);
    virtual int publishing(SrsSource* source);
    virtual int do_publishing(SrsSource* source, SrsPublishRecvThread* trd);
    virtual int acquire_publish(SrsSource* source);
    virtual void release_publish(SrsSource* source);
    virtual int handle_publish_message(SrsSource* source, SrsCommonMessage* msg);
    virtual int process_publish_message(SrsSource* source, SrsCommonMessage* msg);
    virtual int process_play_control_msg(SrsConsumer* consumer, SrsCommonMessage* msg);
    virtual void change_mw_sleep(int sleep_ms);
    virtual void set_sock_options();
private:
    virtual int check_edge_token_traverse_auth();
    virtual int do_token_traverse_auth(SrsRtmpClient* client);
private:
    /**
     * when the connection disconnect, call this method.
     * e.g. log msg of connection and report to other system.
     */
    virtual int on_disconnect();
private:
    virtual int http_hooks_on_connect();
    virtual void http_hooks_on_close();
    virtual int http_hooks_on_publish();
    virtual void http_hooks_on_unpublish();
    virtual int http_hooks_on_play();
    virtual void http_hooks_on_stop();
};

#endif

