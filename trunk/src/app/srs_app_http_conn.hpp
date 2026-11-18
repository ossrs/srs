//
// Copyright (c) 2013-2026 The SRS Authors
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
class ISrsContext;

// A viewer whose connection is simply closed cannot tell it apart from a crash or a network
// failure, and a player retries it as a transport error.
//
// Call it from the root of a streaming handler, never from the branch that produced the error, so
// that every error gets a status. SrsGoApiRtcWhip::serve_http() does the same for the WHIP API.
extern void srs_http_stream_serve_error(ISrsHttpResponseWriter *w, srs_error_t err);

class ISrsHttpConnOwner
{
public:
    ISrsHttpConnOwner();
    virtual ~ISrsHttpConnOwner();

public:
    virtual srs_error_t on_start() = 0;
    virtual srs_error_t on_http_message(ISrsHttpMessage *r, ISrsHttpResponseWriter *w) = 0;
    virtual srs_error_t on_message_done(ISrsHttpMessage *r, ISrsHttpResponseWriter *w) = 0;
    // The owner removes the connection through its manager. It takes r0, and the returned error
    // replaces it.
    virtual srs_error_t on_conn_done(srs_error_t r0) = 0;
};

class ISrsHttpConn : public ISrsConnection, public ISrsStartable, public ISrsCoroutineHandler, public ISrsExpire
{
public:
    ISrsHttpConn();
    virtual ~ISrsHttpConn();

public:
    virtual ISrsKbpsDelta *delta() = 0;
    virtual srs_error_t pull() = 0;
    virtual srs_error_t set_crossdomain_enabled(bool v) = 0;
    virtual srs_error_t set_auth_enabled(bool auth_enabled) = 0;
    virtual srs_error_t set_jsonp(bool v) = 0;
};

// TODO: FIXME: Should rename to roundtrip or responder, not connection.
class SrsHttpConn : public ISrsHttpConn
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsAppConfig *config_;
    ISrsAppFactory *app_factory_;
    ISrsContext *context_;

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
    ISrsCoroutine *trd_;
    std::string ip_;
    int port_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsNetworkDelta *delta_;
    srs_utime_t create_time_;

public:
    SrsHttpConn(ISrsHttpConnOwner *handler, ISrsProtocolReadWriter *fd, ISrsCommonHttpHandler *m, std::string cip, int port);
    void assemble(); // Creates the members, to avoid calling functions in the constructor.
    virtual ~SrsHttpConn();

public:
    virtual std::string desc();

public:
    ISrsKbpsDelta *delta();

public:
    virtual srs_error_t start();

public:
    virtual srs_error_t cycle();

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t do_cycle();
    virtual srs_error_t process_requests(ISrsRequest **preq);
    virtual srs_error_t process_request(ISrsHttpResponseWriter *w, ISrsHttpMessage *r, int rid);
    virtual srs_error_t on_disconnect(ISrsRequest *req);

public:
    virtual ISrsHttpConnOwner *handler();
    virtual srs_error_t pull();
    virtual srs_error_t set_crossdomain_enabled(bool v);
    virtual srs_error_t set_auth_enabled(bool auth_enabled);
    virtual srs_error_t set_jsonp(bool v);

public:
    virtual std::string remote_ip();
    virtual const SrsContextId &get_id();

public:
    virtual void expire();
};

class ISrsHttpxConn : public ISrsConnection, public ISrsStartable, public ISrsHttpConnOwner
{
public:
    ISrsHttpxConn();
    virtual ~ISrsHttpxConn();

public:
};

class SrsHttpxConn : public ISrsHttpxConn
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsAppConfig *config_;
    ISrsStatistic *stat_;
    ISrsContext *context_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsResourceManager *manager_;
    ISrsProtocolReadWriter *io_;
    ISrsSslConnection *ssl_;
    ISrsHttpConn *conn_;
    ISrsCommonHttpHandler *http_mux_;
    std::string ip_;
    int port_;
    // Only HTTP streaming clients go into the statistics.
    bool enable_stat_;
    bool is_api_listener_;
    const std::string ssl_key_file_;
    const std::string ssl_cert_file_;

public:
    SrsHttpxConn(ISrsResourceManager *cm, ISrsProtocolReadWriter *io, ISrsCommonHttpHandler *m, std::string cip, int port, std::string key, std::string cert);
    void assemble(); // Creates the members, to avoid calling functions in the constructor.
    virtual ~SrsHttpxConn();

public:
    void set_enable_stat(bool v);
    void set_api_listener(bool v);
    // An HTTP stream, such as HTTP-FLV, only writes to the client, so another coroutine reads and
    // drops what the client sends, to detect when the client closes the connection.
    // @see https://github.com/ossrs/srs/issues/636#issuecomment-298208427
    virtual srs_error_t pop_message(ISrsHttpMessage **preq);

public:
    virtual srs_error_t on_start();
    virtual srs_error_t on_http_message(ISrsHttpMessage *r, ISrsHttpResponseWriter *w);
    virtual srs_error_t on_message_done(ISrsHttpMessage *r, ISrsHttpResponseWriter *w);
    virtual srs_error_t on_conn_done(srs_error_t r0);

public:
    virtual std::string desc();

public:
    virtual std::string remote_ip();
    virtual const SrsContextId &get_id();

public:
    virtual srs_error_t start();

public:
    ISrsKbpsDelta *delta();
};

class ISrsHttpServer : public ISrsCommonHttpHandler
{
public:
    ISrsHttpServer();
    virtual ~ISrsHttpServer();

public:
    virtual void assemble() = 0;
    virtual srs_error_t initialize() = 0;
    virtual srs_error_t http_mount(ISrsRequest *r) = 0;
    virtual void http_unmount(ISrsRequest *r) = 0;
};

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
    void assemble(); // Not in the constructor, to avoid calling functions there.
    virtual srs_error_t initialize();

public:
    virtual srs_error_t handle(std::string pattern, ISrsHttpHandler *handler);

public:
    virtual srs_error_t serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r);

public:
    virtual srs_error_t http_mount(ISrsRequest *r);
    virtual void http_unmount(ISrsRequest *r);
};

#endif
