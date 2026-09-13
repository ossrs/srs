//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_APP_HTTP_CONN_HPP
#define SRS_APP_HTTP_CONN_HPP

#include <srs_core.hpp>

#include <map>
#include <string>
#include <vector>

#include <srs_app_reload.hpp>
#include <srs_app_rtmp_source.hpp>
#include <srs_app_st.hpp>
#include <srs_kernel_file.hpp>
#include <srs_protocol_conn.hpp>
#include <srs_protocol_http_conn.hpp>

class SrsServer;
class SrsLiveSource;
class ISrsRequest;
class SrsLiveConsumer;
class SrsStSocket;
class SrsHttpParser;
class ISrsHttpParser;
class ISrsHttpMessage;
class SrsHttpHandler;
class SrsMessageQueue;
class SrsMediaPacket;
class SrsFastStream;
class SrsHttpUri;
class SrsHttpMessage;
class SrsHttpStreamServer;
class ISrsHttpStreamServer;
class SrsHttpStaticServer;
class ISrsHttpStaticServer;
class SrsNetworkDelta;
class ISrsNetworkDelta;
class SrsHttpCorsMux;
class ISrsHttpCorsMux;
class SrsHttpAuthMux;
class ISrsHttpAuthMux;
class SrsSslConnection;
class ISrsSslConnection;
class ISrsHttpConn;
class ISrsAppConfig;
class ISrsStatistic;
class ISrsAppFactory;

// The root error handler of HTTP streaming, which converts an error into an HTTP status for the
// viewer. Without it the connection is simply closed, which the viewer cannot tell apart from a
// crash or a network failure, and which a player retries because it looks like a transport error.
//
// Call it from the root of a streaming handler, never from the branch that produced the error, so
// that every error gets a status instead of only the ones somebody remembered to handle. This is
// the same pattern SrsGoApiRtcWhip::serve_http() uses for the WHIP API.
//
// It does nothing when err is success, or when the response header is already sent, because the
// viewer is already being served and the status can no longer be changed.
//
// Note that it never sends the error description to the viewer, which may carry the HTTP callback
// URL or the response of the callback backend. Only the error code and its short name are sent.
extern void srs_http_stream_serve_error(ISrsHttpResponseWriter *w, srs_error_t err);

// The owner of HTTP connection.
class ISrsHttpConnOwner
{
public:
    ISrsHttpConnOwner();
    virtual ~ISrsHttpConnOwner();

public:
    // When start the coroutine to process connection.
    virtual srs_error_t on_start() = 0;
    // Handle the HTTP message r, which may be parsed partially.
    // For the static service or api, discard any body.
    // For the stream caster, for instance, http flv streaming, may discard the flv header or not.
    virtual srs_error_t on_http_message(ISrsHttpMessage *r, ISrsHttpResponseWriter *w) = 0;
    // When message is processed, we may need to do more things.
    virtual srs_error_t on_message_done(ISrsHttpMessage *r, ISrsHttpResponseWriter *w) = 0;
    // When connection is destroy, should use manager to dispose it.
    // The r0 is the original error, we will use the returned new error.
    virtual srs_error_t on_conn_done(srs_error_t r0) = 0;
};

// The HTTP connection, for HTTP stream or static file.
class ISrsHttpConn : public ISrsConnection, public ISrsStartable, public ISrsCoroutineHandler, public ISrsExpire
{
public:
    ISrsHttpConn();
    virtual ~ISrsHttpConn();

public:
    // Get the delta object for statistics.
    virtual ISrsKbpsDelta *delta() = 0;
    // Whether the connection coroutine is error or terminated.
    virtual srs_error_t pull() = 0;
    // Whether enable the CORS(cross-domain).
    virtual srs_error_t set_crossdomain_enabled(bool v) = 0;
    // Whether enable the Auth.
    virtual srs_error_t set_auth_enabled(bool auth_enabled) = 0;
    // Whether enable the JSONP.
    virtual srs_error_t set_jsonp(bool v) = 0;
};

// TODO: FIXME: Should rename to roundtrip or responder, not connection.
// The http connection which request the static or stream content.
class SrsHttpConn : public ISrsHttpConn
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsAppConfig *config_;
    ISrsAppFactory *app_factory_;

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    ISrsHttpParser *parser_;
    ISrsCommonHttpHandler *http_mux_;
    ISrsHttpCorsMux *cors_;
    ISrsHttpAuthMux *auth_;
    ISrsHttpConnOwner *handler_;

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    ISrsProtocolReadWriter *skt_;
    // Each connection start a green thread,
    // when thread stop, the connection will be delete by server.
    ISrsCoroutine *trd_;
    // The ip and port of client.
    std::string ip_;
    int port_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // The delta for statistic.
    ISrsNetworkDelta *delta_;
    // The create time in microseconds.
    // for current connection to log self create time and calculate the living time.
    srs_utime_t create_time_;

public:
    SrsHttpConn(ISrsHttpConnOwner *handler, ISrsProtocolReadWriter *fd, ISrsCommonHttpHandler *m, std::string cip, int port);
    virtual ~SrsHttpConn();
    // Interface ISrsResource.
public:
    virtual std::string desc();

public:
    ISrsKbpsDelta *delta();
    // Interface ISrsStartable
public:
    virtual srs_error_t start();
    // Interface ISrsCoroutineHandler
public:
    virtual srs_error_t cycle();

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t do_cycle();
    virtual srs_error_t process_requests(ISrsRequest **preq);
    virtual srs_error_t process_request(ISrsHttpResponseWriter *w, ISrsHttpMessage *r, int rid);
    // When the connection disconnect, call this method.
    // e.g. log msg of connection and report to other system.
    // @param request: request which is converted by the last http message.
    virtual srs_error_t on_disconnect(ISrsRequest *req);

public:
    // Get the HTTP message handler.
    virtual ISrsHttpConnOwner *handler();
    // Whether the connection coroutine is error or terminated.
    virtual srs_error_t pull();
    // Whether enable the CORS(cross-domain).
    virtual srs_error_t set_crossdomain_enabled(bool v);
    // Whether enable the Auth.
    virtual srs_error_t set_auth_enabled(bool auth_enabled);
    // Whether enable the JSONP.
    virtual srs_error_t set_jsonp(bool v);
    // Interface ISrsConnection.
public:
    virtual std::string remote_ip();
    virtual const SrsContextId &get_id();
    // Interface ISrsExpire.
public:
    virtual void expire();
};

// The HTTP connection manager.
class ISrsHttpxConn : public ISrsConnection, public ISrsStartable, public ISrsHttpConnOwner
{
public:
    ISrsHttpxConn();
    virtual ~ISrsHttpxConn();

public:
};

// Drop body of request, only process the response.
class SrsHttpxConn : public ISrsHttpxConn
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsAppConfig *config_;
    ISrsStatistic *stat_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // The manager object to manage the connection.
    ISrsResourceManager *manager_;
    ISrsProtocolReadWriter *io_;
    ISrsSslConnection *ssl_;
    ISrsHttpConn *conn_;
    // We should never enable the stat, unless HTTP stream connection requires.
    bool enable_stat_;
    // ssl key & cert file
    const std::string ssl_key_file_;
    const std::string ssl_cert_file_;

public:
    SrsHttpxConn(ISrsResourceManager *cm, ISrsProtocolReadWriter *io, ISrsCommonHttpHandler *m, std::string cip, int port, std::string key, std::string cert);
    virtual ~SrsHttpxConn();

public:
    // Require statistic about HTTP connection, for HTTP streaming clients only.
    void set_enable_stat(bool v);
    // Directly read a HTTP request message.
    // It's exported for HTTP stream, such as HTTP FLV, only need to write to client when
    // serving it, but we need to start a thread to read message to detect whether FD is closed.
    // @see https://github.com/ossrs/srs/issues/636#issuecomment-298208427
    // @remark Should only used in HTTP-FLV streaming connection.
    virtual srs_error_t pop_message(ISrsHttpMessage **preq);
    // Interface ISrsHttpConnOwner.
public:
    virtual srs_error_t on_start();
    virtual srs_error_t on_http_message(ISrsHttpMessage *r, ISrsHttpResponseWriter *w);
    virtual srs_error_t on_message_done(ISrsHttpMessage *r, ISrsHttpResponseWriter *w);
    virtual srs_error_t on_conn_done(srs_error_t r0);
    // Interface ISrsResource.
public:
    virtual std::string desc();
    // Interface ISrsConnection.
public:
    virtual std::string remote_ip();
    virtual const SrsContextId &get_id();
    // Interface ISrsStartable
public:
    virtual srs_error_t start();

public:
    ISrsKbpsDelta *delta();
};

// The http server, use http stream or static server to serve requests.
class ISrsHttpServer : public ISrsCommonHttpHandler
{
public:
    ISrsHttpServer();
    virtual ~ISrsHttpServer();

public:
};

// The http server, use http stream or static server to serve requests.
class SrsHttpServer : public ISrsHttpServer
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsHttpStaticServer *http_static_;
    ISrsHttpStreamServer *http_stream_;

public:
    SrsHttpServer();
    virtual ~SrsHttpServer();

public:
    virtual srs_error_t initialize();
    // Interface ISrsCommonHttpHandler
public:
    virtual srs_error_t handle(std::string pattern, ISrsHttpHandler *handler);
    // Interface ISrsHttpHandler
public:
    virtual srs_error_t serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r);

public:
    virtual srs_error_t http_mount(ISrsRequest *r);
    virtual void http_unmount(ISrsRequest *r);
};

#endif
