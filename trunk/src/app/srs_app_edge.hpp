/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2019 Winlin
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

#ifndef SRS_APP_EDGE_HPP
#define SRS_APP_EDGE_HPP

#include <srs_core.hpp>

#include <srs_app_st.hpp>
#include <srs_app_thread.hpp>

#include <string>

class SrsStSocket;
class SrsRtmpServer;
class SrsSource;
class SrsRequest;
class SrsPlayEdge;
class SrsPublishEdge;
class SrsRtmpClient;
class SrsCommonMessage;
class SrsMessageQueue;
class ISrsProtocolReadWriter;
class SrsKbps;
class SrsLbRoundRobin;
class SrsTcpClient;
class SrsSimpleRtmpClient;
class SrsPacket;

// The state of edge, auto machine
enum SrsEdgeState
{
    SrsEdgeStateInit = 0,
    
    // For play edge
    SrsEdgeStatePlay = 100,
    // play stream from origin, ingest stream
    SrsEdgeStateIngestConnected = 101,
    
    // For publish edge
    SrsEdgeStatePublish = 200,
};

// The state of edge from user, manual machine
enum SrsEdgeUserState
{
    SrsEdgeUserStateInit = 0,
    SrsEdgeUserStateReloading = 100,
};

// The upstream of edge, can be rtmp or http.
class SrsEdgeUpstream
{
public:
    SrsEdgeUpstream();
    virtual ~SrsEdgeUpstream();
public:
    virtual srs_error_t connect(SrsRequest* r, SrsLbRoundRobin* lb) = 0;
    virtual srs_error_t recv_message(SrsCommonMessage** pmsg) = 0;
    virtual srs_error_t decode_message(SrsCommonMessage* msg, SrsPacket** ppacket) = 0;
    virtual void close() = 0;
public:
    virtual void set_recv_timeout(srs_utime_t tm) = 0;
    virtual void kbps_sample(const char* label, int64_t age) = 0;
};

class SrsEdgeRtmpUpstream : public SrsEdgeUpstream
{
private:
    // For RTMP 302, if not empty,
    // use this <ip[:port]> as upstream.
    std::string redirect;
    SrsSimpleRtmpClient* sdk;
public:
    // @param rediect, override the server. ignore if empty.
    SrsEdgeRtmpUpstream(std::string r);
    virtual ~SrsEdgeRtmpUpstream();
public:
    virtual srs_error_t connect(SrsRequest* r, SrsLbRoundRobin* lb);
    virtual srs_error_t recv_message(SrsCommonMessage** pmsg);
    virtual srs_error_t decode_message(SrsCommonMessage* msg, SrsPacket** ppacket);
    virtual void close();
public:
    virtual void set_recv_timeout(srs_utime_t tm);
    virtual void kbps_sample(const char* label, int64_t age);
};

// The edge used to ingest stream from origin.
class SrsEdgeIngester : public ISrsCoroutineHandler
{
private:
    SrsSource* source;
    SrsPlayEdge* edge;
    SrsRequest* req;
    SrsCoroutine* trd;
    SrsLbRoundRobin* lb;
    SrsEdgeUpstream* upstream;
    // For RTMP 302 redirect.
    std::string redirect;
public:
    SrsEdgeIngester();
    virtual ~SrsEdgeIngester();
public:
    virtual srs_error_t initialize(SrsSource* s, SrsPlayEdge* e, SrsRequest* r);
    virtual srs_error_t start();
    virtual void stop();
    virtual std::string get_curr_origin();
// Interface ISrsReusableThread2Handler
public:
    virtual srs_error_t cycle();
private:
    virtual srs_error_t do_cycle();
private:
    virtual srs_error_t ingest();
    virtual srs_error_t process_publish_message(SrsCommonMessage* msg);
};

// The edge used to forward stream to origin.
class SrsEdgeForwarder : public ISrsCoroutineHandler
{
private:
    SrsSource* source;
    SrsPublishEdge* edge;
    SrsRequest* req;
    SrsCoroutine* trd;
    SrsSimpleRtmpClient* sdk;
    SrsLbRoundRobin* lb;
    // we must ensure one thread one fd principle,
    // that is, a fd must be write/read by the one thread.
    // The publish service thread will proxy(msg), and the edge forward thread
    // will cycle(), so we use queue for cycle to send the msg of proxy.
    SrsMessageQueue* queue;
    // error code of send, for edge proxy thread to query.
    int send_error_code;
public:
    SrsEdgeForwarder();
    virtual ~SrsEdgeForwarder();
public:
    virtual void set_queue_size(srs_utime_t queue_size);
public:
    virtual srs_error_t initialize(SrsSource* s, SrsPublishEdge* e, SrsRequest* r);
    virtual srs_error_t start();
    virtual void stop();
// Interface ISrsReusableThread2Handler
public:
    virtual srs_error_t cycle();
private:
    virtual srs_error_t do_cycle();
public:
    virtual srs_error_t proxy(SrsCommonMessage* msg);
};

// The play edge control service.
class SrsPlayEdge
{
private:
    SrsEdgeState state;
    SrsEdgeIngester* ingester;
public:
    SrsPlayEdge();
    virtual ~SrsPlayEdge();
public:
    // Always use the req of source,
    // For we assume all client to edge is invalid,
    // if auth open, edge must valid it from origin, then service it.
    virtual srs_error_t initialize(SrsSource* source, SrsRequest* req);
    // When client play stream on edge.
    virtual srs_error_t on_client_play();
    // When all client stopped play, disconnect to origin.
    virtual void on_all_client_stop();
    virtual std::string get_curr_origin();
public:
    // When ingester start to play stream.
    virtual srs_error_t on_ingest_play();
};

// The publish edge control service.
class SrsPublishEdge
{
private:
    SrsEdgeState state;
    SrsEdgeForwarder* forwarder;
public:
    SrsPublishEdge();
    virtual ~SrsPublishEdge();
public:
    virtual void set_queue_size(srs_utime_t queue_size);
public:
    virtual srs_error_t initialize(SrsSource* source, SrsRequest* req);
    virtual bool can_publish();
    // When client publish stream on edge.
    virtual srs_error_t on_client_publish();
    // Proxy publish stream to edge
    virtual srs_error_t on_proxy_publish(SrsCommonMessage* msg);
    // Proxy unpublish stream to edge.
    virtual void on_proxy_unpublish();
};

#endif

