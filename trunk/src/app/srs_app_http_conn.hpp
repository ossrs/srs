//
// Copyright (c) 2013-2021 Winlin
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_APP_HTTP_CONN_HPP
#define SRS_APP_HTTP_CONN_HPP

#include <srs_core.hpp>

#include <map>
#include <string>
#include <vector>

#include <srs_service_http_conn.hpp>
#include <srs_app_reload.hpp>
#include <srs_kernel_file.hpp>
#include <srs_app_st.hpp>
#include <srs_app_conn.hpp>
#include <srs_app_source.hpp>

class SrsServer;
class SrsLiveSource;
class SrsRequest;
class SrsLiveConsumer;
class SrsStSocket;
class SrsHttpParser;
class ISrsHttpMessage;
class SrsHttpHandler;
class SrsMessageQueue;
class SrsSharedPtrMessage;
class SrsRequest;
class SrsFastStream;
class SrsHttpUri;
class SrsHttpMessage;
class SrsHttpStreamServer;
class SrsHttpStaticServer;

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
    virtual srs_error_t on_http_message(ISrsHttpMessage* r, SrsHttpResponseWriter* w) = 0;
    // When message is processed, we may need to do more things.
    virtual srs_error_t on_message_done(ISrsHttpMessage* r, SrsHttpResponseWriter* w) = 0;
    // When connection is destroy, should use manager to dispose it.
    // The r0 is the original error, we will use the returned new error.
    virtual srs_error_t on_conn_done(srs_error_t r0) = 0;
};

// The http connection which request the static or stream content.
class SrsHttpConn : public ISrsStartableConneciton, public ISrsCoroutineHandler
    , public ISrsExpire
{
protected:
    SrsHttpParser* parser;
    ISrsHttpServeMux* http_mux;
    SrsHttpCorsMux* cors;
    ISrsHttpConnOwner* handler_;
protected:
    ISrsProtocolReadWriter* skt;
    // Each connection start a green thread,
    // when thread stop, the connection will be delete by server.
    SrsCoroutine* trd;
    // The ip and port of client.
    std::string ip;
    int port;
private:
    // The connection total kbps.
    // not only the rtmp or http connection, all type of connection are
    // need to statistic the kbps of io.
    // The SrsStatistic will use it indirectly to statistic the bytes delta of current connection.
    SrsKbps* kbps;
    SrsWallClock* clk;
    // The create time in milliseconds.
    // for current connection to log self create time and calculate the living time.
    int64_t create_time;
public:
    SrsHttpConn(ISrsHttpConnOwner* handler, ISrsProtocolReadWriter* fd, ISrsHttpServeMux* m, std::string cip, int port);
    virtual ~SrsHttpConn();
// Interface ISrsResource.
public:
    virtual std::string desc();
// Interface ISrsKbpsDelta
public:
    virtual void remark(int64_t* in, int64_t* out);
// Interface ISrsStartable
public:
    virtual srs_error_t start();
// Interface ISrsOneCycleThreadHandler
public:
    virtual srs_error_t cycle();
private:
    virtual srs_error_t do_cycle();
    virtual srs_error_t process_requests(SrsRequest** preq);
    virtual srs_error_t process_request(ISrsHttpResponseWriter* w, ISrsHttpMessage* r, int rid);
    // When the connection disconnect, call this method.
    // e.g. log msg of connection and report to other system.
    // @param request: request which is converted by the last http message.
    virtual srs_error_t on_disconnect(SrsRequest* req);
public:
    // Get the HTTP message handler.
    virtual ISrsHttpConnOwner* handler();
    // Whether the connection coroutine is error or terminated.
    virtual srs_error_t pull();
    // Whether enable the CORS(cross-domain).
    virtual srs_error_t set_crossdomain_enabled(bool v);
    // Whether enable the JSONP.
    virtual srs_error_t set_jsonp(bool v);
// Interface ISrsConnection.
public:
    virtual std::string remote_ip();
    virtual const SrsContextId& get_id();
// Interface ISrsExpire.
public:
    virtual void expire();
};

// Drop body of request, only process the response.
class SrsResponseOnlyHttpConn : public ISrsStartableConneciton, public ISrsHttpConnOwner
    , public ISrsReloadHandler
{
private:
    // The manager object to manage the connection.
    ISrsResourceManager* manager;
    SrsTcpConnection* skt;
    SrsSslConnection* ssl;
    SrsHttpConn* conn;
public:
    SrsResponseOnlyHttpConn(bool https, ISrsResourceManager* cm, srs_netfd_t fd, ISrsHttpServeMux* m, std::string cip, int port);
    virtual ~SrsResponseOnlyHttpConn();
public:
    // Directly read a HTTP request message.
    // It's exported for HTTP stream, such as HTTP FLV, only need to write to client when
    // serving it, but we need to start a thread to read message to detect whether FD is closed.
    // @see https://github.com/ossrs/srs/issues/636#issuecomment-298208427
    // @remark Should only used in HTTP-FLV streaming connection.
    virtual srs_error_t pop_message(ISrsHttpMessage** preq);
// Interface ISrsReloadHandler
public:
    virtual srs_error_t on_reload_http_stream_crossdomain();
// Interface ISrsHttpConnOwner.
public:
    virtual srs_error_t on_start();
    virtual srs_error_t on_http_message(ISrsHttpMessage* r, SrsHttpResponseWriter* w);
    virtual srs_error_t on_message_done(ISrsHttpMessage* r, SrsHttpResponseWriter* w);
    virtual srs_error_t on_conn_done(srs_error_t r0);
// Extract APIs from SrsTcpConnection.
public:
    // Set socket option TCP_NODELAY.
    virtual srs_error_t set_tcp_nodelay(bool v);
    // Set socket option SO_SNDBUF in srs_utime_t.
    virtual srs_error_t set_socket_buffer(srs_utime_t buffer_v);
// Interface ISrsResource.
public:
    virtual std::string desc();
// Interface ISrsConnection.
public:
    virtual std::string remote_ip();
    virtual const SrsContextId& get_id();
// Interface ISrsStartable
public:
    virtual srs_error_t start();
// Interface ISrsKbpsDelta
public:
    virtual void remark(int64_t* in, int64_t* out);
};

// The http server, use http stream or static server to serve requests.
class SrsHttpServer : public ISrsHttpServeMux
{
private:
    SrsServer* server;
    SrsHttpStaticServer* http_static;
    SrsHttpStreamServer* http_stream;
public:
    SrsHttpServer(SrsServer* svr);
    virtual ~SrsHttpServer();
public:
    virtual srs_error_t initialize();
// Interface ISrsHttpServeMux
public:
    virtual srs_error_t serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r);
public:
    virtual srs_error_t http_mount(SrsLiveSource* s, SrsRequest* r);
    virtual void http_unmount(SrsLiveSource* s, SrsRequest* r);
};

#endif

