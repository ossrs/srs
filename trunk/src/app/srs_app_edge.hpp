//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#ifndef SRS_APP_EDGE_HPP
#define SRS_APP_EDGE_HPP

#include <srs_core.hpp>

#include <srs_app_st.hpp>

#include <string>

class SrsStSocket;
class SrsRtmpServer;
class SrsLiveSource;
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
class SrsHttpClient;
class ISrsHttpMessage;
class SrsHttpFileReader;
class SrsFlvDecoder;
class ISrsApmSpan;

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

    // We are stopping edge ingesting.
    SrsEdgeStateIngestStopping = 300,
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
    virtual void selected(std::string& server, int& port) = 0;
    virtual void set_recv_timeout(srs_utime_t tm) = 0;
    virtual void kbps_sample(const char* label, srs_utime_t age) = 0;
};

class SrsEdgeRtmpUpstream : public SrsEdgeUpstream
{
private:
    // For RTMP 302, if not empty,
    // use this <ip[:port]> as upstream.
    std::string redirect;
    SrsSimpleRtmpClient* sdk;
private:
    // Current selected server, the ip:port.
    std::string selected_ip;
    int selected_port;
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
    virtual void selected(std::string& server, int& port);
    virtual void set_recv_timeout(srs_utime_t tm);
    virtual void kbps_sample(const char* label, srs_utime_t age);
};

class SrsEdgeFlvUpstream : public SrsEdgeUpstream
{
private:
    std::string schema_;
    SrsHttpClient* sdk_;
    ISrsHttpMessage* hr_;
private:
    SrsHttpFileReader* reader_;
    SrsFlvDecoder* decoder_;
private:
    // We might modify the request by HTTP redirect.
    SrsRequest* req_;
    // Current selected server, the ip:port.
    std::string selected_ip;
    int selected_port;
public:
    SrsEdgeFlvUpstream(std::string schema);
    virtual ~SrsEdgeFlvUpstream();
public:
    virtual srs_error_t connect(SrsRequest* r, SrsLbRoundRobin* lb);
private:
    virtual srs_error_t do_connect(SrsRequest* r, SrsLbRoundRobin* lb, int redirect_depth);
public:
    virtual srs_error_t recv_message(SrsCommonMessage** pmsg);
    virtual srs_error_t decode_message(SrsCommonMessage* msg, SrsPacket** ppacket);
    virtual void close();
public:
    virtual void selected(std::string& server, int& port);
    virtual void set_recv_timeout(srs_utime_t tm);
    virtual void kbps_sample(const char* label, srs_utime_t age);
};

// The edge used to ingest stream from origin.
class SrsEdgeIngester : public ISrsCoroutineHandler
{
private:
    SrsLiveSource* source;
    SrsPlayEdge* edge;
    SrsRequest* req;
    SrsCoroutine* trd;
    SrsLbRoundRobin* lb;
    SrsEdgeUpstream* upstream;
#ifdef SRS_APM
    ISrsApmSpan* span_main_;
#endif
public:
    SrsEdgeIngester();
    virtual ~SrsEdgeIngester();
public:
    virtual srs_error_t initialize(SrsLiveSource* s, SrsPlayEdge* e, SrsRequest* r);
    virtual srs_error_t start();
    virtual void stop();
    virtual std::string get_curr_origin();
#ifdef SRS_APM
    // Get the current main span. Note that it might be NULL.
    ISrsApmSpan* span();
#endif
// Interface ISrsReusableThread2Handler
public:
    virtual srs_error_t cycle();
private:
    virtual srs_error_t do_cycle();
private:
    virtual srs_error_t ingest(std::string& redirect);
    virtual srs_error_t process_publish_message(SrsCommonMessage* msg, std::string& redirect);
};

// The edge used to forward stream to origin.
class SrsEdgeForwarder : public ISrsCoroutineHandler
{
private:
    SrsLiveSource* source;
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
    virtual srs_error_t initialize(SrsLiveSource* s, SrsPublishEdge* e, SrsRequest* r);
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
    virtual srs_error_t initialize(SrsLiveSource* source, SrsRequest* req);
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
    virtual srs_error_t initialize(SrsLiveSource* source, SrsRequest* req);
    virtual bool can_publish();
    // When client publish stream on edge.
    virtual srs_error_t on_client_publish();
    // Proxy publish stream to edge
    virtual srs_error_t on_proxy_publish(SrsCommonMessage* msg);
    // Proxy unpublish stream to edge.
    virtual void on_proxy_unpublish();
};

#endif

