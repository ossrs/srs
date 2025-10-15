//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_APP_EDGE_HPP
#define SRS_APP_EDGE_HPP

#include <srs_core.hpp>

#include <srs_app_st.hpp>
#include <srs_core_autofree.hpp>

#include <string>

class SrsStSocket;
class SrsRtmpServer;
class SrsLiveSource;
class ISrsRequest;
class SrsPlayEdge;
class SrsPublishEdge;
class SrsRtmpClient;
class SrsRtmpCommonMessage;
class SrsMessageQueue;
class ISrsProtocolReadWriter;
class SrsKbps;
class ISrsLbRoundRobin;
class SrsTcpClient;
class SrsSimpleRtmpClient;
class SrsRtmpCommand;
class SrsHttpClient;
class ISrsHttpMessage;
class SrsHttpFileReader;
class SrsFlvDecoder;
class ISrsAppConfig;
class ISrsBasicRtmpClient;
class ISrsHttpClient;
class ISrsFileReader;
class ISrsFlvDecoder;
class ISrsLiveSource;
class ISrsPlayEdge;
class ISrsPublishEdge;
class ISrsAppFactory;

// The state of edge, auto machine
enum SrsEdgeState {
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
enum SrsEdgeUserState {
    SrsEdgeUserStateInit = 0,
    SrsEdgeUserStateReloading = 100,
};

// The upstream of edge, can be rtmp or http.
class ISrsEdgeUpstream
{
public:
    ISrsEdgeUpstream();
    virtual ~ISrsEdgeUpstream();

public:
    virtual srs_error_t connect(ISrsRequest *r, ISrsLbRoundRobin *lb) = 0;
    virtual srs_error_t recv_message(SrsRtmpCommonMessage **pmsg) = 0;
    virtual srs_error_t decode_message(SrsRtmpCommonMessage *msg, SrsRtmpCommand **ppacket) = 0;
    virtual void close() = 0;

public:
    virtual void selected(std::string &server, int &port) = 0;
    virtual void set_recv_timeout(srs_utime_t tm) = 0;
    virtual void kbps_sample(const char *label, srs_utime_t age) = 0;
};

// The RTMP upstream of edge.
class SrsEdgeRtmpUpstream : public ISrsEdgeUpstream
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsAppConfig *config_;
    ISrsAppFactory *app_factory_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // For RTMP 302, if not empty,
    // use this <ip[:port]> as upstream.
    std::string redirect_;
    ISrsBasicRtmpClient *sdk_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // Current selected server, the ip:port.
    std::string selected_ip_;
    int selected_port_;

public:
    // @param rediect, override the server. ignore if empty.
    SrsEdgeRtmpUpstream(std::string r);
    virtual ~SrsEdgeRtmpUpstream();

public:
    virtual srs_error_t connect(ISrsRequest *r, ISrsLbRoundRobin *lb);
    virtual srs_error_t recv_message(SrsRtmpCommonMessage **pmsg);
    virtual srs_error_t decode_message(SrsRtmpCommonMessage *msg, SrsRtmpCommand **ppacket);
    virtual void close();

public:
    virtual void selected(std::string &server, int &port);
    virtual void set_recv_timeout(srs_utime_t tm);
    virtual void kbps_sample(const char *label, srs_utime_t age);
};

// The HTTP FLV upstream of edge.
class SrsEdgeFlvUpstream : public ISrsEdgeUpstream
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsAppConfig *config_;
    ISrsAppFactory *app_factory_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    std::string schema_;
    ISrsHttpClient *sdk_;
    ISrsHttpMessage *hr_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsFileReader *reader_;
    ISrsFlvDecoder *decoder_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // We might modify the request by HTTP redirect.
    ISrsRequest *req_;
    // Current selected server, the ip:port.
    std::string selected_ip_;
    int selected_port_;

public:
    SrsEdgeFlvUpstream(std::string schema);
    virtual ~SrsEdgeFlvUpstream();

public:
    virtual srs_error_t connect(ISrsRequest *r, ISrsLbRoundRobin *lb);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t do_connect(ISrsRequest *r, ISrsLbRoundRobin *lb, int redirect_depth);

public:
    virtual srs_error_t recv_message(SrsRtmpCommonMessage **pmsg);
    virtual srs_error_t decode_message(SrsRtmpCommonMessage *msg, SrsRtmpCommand **ppacket);
    virtual void close();

public:
    virtual void selected(std::string &server, int &port);
    virtual void set_recv_timeout(srs_utime_t tm);
    virtual void kbps_sample(const char *label, srs_utime_t age);
};

// The interface for edge ingester.
class ISrsEdgeIngester
{
public:
    ISrsEdgeIngester();
    virtual ~ISrsEdgeIngester();

public:
    // Initialize the ingester.
    virtual srs_error_t initialize(SrsSharedPtr<SrsLiveSource> s, ISrsPlayEdge *e, ISrsRequest *r) = 0;
    // Start the ingester.
    virtual srs_error_t start() = 0;
    // Stop the ingester.
    virtual void stop() = 0;
};

// The edge used to ingest stream from origin.
class SrsEdgeIngester : public ISrsCoroutineHandler, public ISrsEdgeIngester
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsAppConfig *config_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // Because source references to this object, so we should directly use the source ptr.
    ISrsLiveSource *source_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsPlayEdge *edge_;
    ISrsRequest *req_;
    ISrsCoroutine *trd_;
    ISrsLbRoundRobin *lb_;
    ISrsEdgeUpstream *upstream_;

public:
    SrsEdgeIngester();
    virtual ~SrsEdgeIngester();

public:
    virtual srs_error_t initialize(SrsSharedPtr<SrsLiveSource> s, ISrsPlayEdge *e, ISrsRequest *r);
    virtual srs_error_t start();
    virtual void stop();

    // Interface ISrsReusableThread2Handler
public:
    virtual srs_error_t cycle();

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t do_cycle();

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t ingest(std::string &redirect);
    virtual srs_error_t process_publish_message(SrsRtmpCommonMessage *msg, std::string &redirect);
};

// The interface for edge forwarder.
class ISrsEdgeForwarder
{
public:
    ISrsEdgeForwarder();
    virtual ~ISrsEdgeForwarder();

public:
    // Set the queue size.
    virtual void set_queue_size(srs_utime_t queue_size) = 0;
    // Initialize the forwarder.
    virtual srs_error_t initialize(SrsSharedPtr<SrsLiveSource> s, ISrsPublishEdge *e, ISrsRequest *r) = 0;
    // Start the forwarder.
    virtual srs_error_t start() = 0;
    // Stop the forwarder.
    virtual void stop() = 0;
    // Proxy publish stream to edge.
    virtual srs_error_t proxy(SrsRtmpCommonMessage *msg) = 0;
};

// The edge used to forward stream to origin.
class SrsEdgeForwarder : public ISrsCoroutineHandler, public ISrsEdgeForwarder
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsAppConfig *config_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // Because source references to this object, so we should directly use the source ptr.
    ISrsLiveSource *source_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsPublishEdge *edge_;
    ISrsRequest *req_;
    ISrsCoroutine *trd_;
    SrsSimpleRtmpClient *sdk_;
    ISrsLbRoundRobin *lb_;
    // we must ensure one thread one fd principle,
    // that is, a fd must be write/read by the one thread.
    // The publish service thread will proxy(msg), and the edge forward thread
    // will cycle(), so we use queue for cycle to send the msg of proxy.
    SrsMessageQueue *queue_;
    // error code of send, for edge proxy thread to query.
    int send_error_code_;

public:
    SrsEdgeForwarder();
    virtual ~SrsEdgeForwarder();

public:
    virtual void set_queue_size(srs_utime_t queue_size);

public:
    virtual srs_error_t initialize(SrsSharedPtr<SrsLiveSource> s, ISrsPublishEdge *e, ISrsRequest *r);
    virtual srs_error_t start();
    virtual void stop();
    // Interface ISrsReusableThread2Handler
public:
    virtual srs_error_t cycle();

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t do_cycle();

public:
    virtual srs_error_t proxy(SrsRtmpCommonMessage *msg);
};

// The interface for play edge.
class ISrsPlayEdge
{
public:
    ISrsPlayEdge();
    virtual ~ISrsPlayEdge();

public:
    // When ingester start to play stream.
    virtual srs_error_t on_ingest_play() = 0;
};

// The play edge control service.
class SrsPlayEdge : public ISrsPlayEdge
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    SrsEdgeState state_;
    ISrsEdgeIngester *ingester_;

public:
    SrsPlayEdge();
    virtual ~SrsPlayEdge();

public:
    // Always use the req of source,
    // For we assume all client to edge is invalid,
    // if auth open, edge must valid it from origin, then service it.
    virtual srs_error_t initialize(SrsSharedPtr<SrsLiveSource> source, ISrsRequest *req);
    // When client play stream on edge.
    virtual srs_error_t on_client_play();
    // When all client stopped play, disconnect to origin.
    virtual void on_all_client_stop();

public:
    // When ingester start to play stream.
    virtual srs_error_t on_ingest_play();
};

// The interface for publish edge.
class ISrsPublishEdge
{
public:
    ISrsPublishEdge();
    virtual ~ISrsPublishEdge();

public:
};

// The publish edge control service.
class SrsPublishEdge : public ISrsPublishEdge
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    SrsEdgeState state_;
    ISrsEdgeForwarder *forwarder_;

public:
    SrsPublishEdge();
    virtual ~SrsPublishEdge();

public:
    virtual void set_queue_size(srs_utime_t queue_size);

public:
    virtual srs_error_t initialize(SrsSharedPtr<SrsLiveSource> source, ISrsRequest *req);
    virtual bool can_publish();
    // When client publish stream on edge.
    virtual srs_error_t on_client_publish();
    // Proxy publish stream to edge
    virtual srs_error_t on_proxy_publish(SrsRtmpCommonMessage *msg);
    // Proxy unpublish stream to edge.
    virtual void on_proxy_unpublish();
};

#endif
