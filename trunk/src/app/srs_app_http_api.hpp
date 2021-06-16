//
// Copyright (c) 2013-2021 Winlin
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_APP_HTTP_API_HPP
#define SRS_APP_HTTP_API_HPP

#include <srs_core.hpp>

class ISrsHttpMessage;
class SrsHttpParser;
class SrsHttpHandler;
class SrsServer;
class SrsRtcServer;
class SrsJsonObject;
class SrsSdp;
class SrsRequest;
class ISrsHttpResponseWriter;
class SrsHttpConn;

#include <string>

#include <srs_app_st.hpp>
#include <srs_app_conn.hpp>
#include <srs_http_stack.hpp>
#include <srs_app_reload.hpp>
#include <srs_app_http_conn.hpp>

extern srs_error_t srs_api_response(ISrsHttpResponseWriter* w, ISrsHttpMessage* r, std::string json);
extern srs_error_t srs_api_response_code(ISrsHttpResponseWriter* w, ISrsHttpMessage* r, int code);
extern srs_error_t srs_api_response_code(ISrsHttpResponseWriter* w, ISrsHttpMessage* r, srs_error_t code);

// For http root.
class SrsGoApiRoot : public ISrsHttpHandler
{
public:
    SrsGoApiRoot();
    virtual ~SrsGoApiRoot();
public:
    virtual srs_error_t serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r);
};

class SrsGoApiApi : public ISrsHttpHandler
{
public:
    SrsGoApiApi();
    virtual ~SrsGoApiApi();
public:
    virtual srs_error_t serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r);
};

class SrsGoApiV1 : public ISrsHttpHandler
{
public:
    SrsGoApiV1();
    virtual ~SrsGoApiV1();
public:
    virtual srs_error_t serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r);
};

class SrsGoApiVersion : public ISrsHttpHandler
{
public:
    SrsGoApiVersion();
    virtual ~SrsGoApiVersion();
public:
    virtual srs_error_t serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r);
};

class SrsGoApiSummaries : public ISrsHttpHandler
{
public:
    SrsGoApiSummaries();
    virtual ~SrsGoApiSummaries();
public:
    virtual srs_error_t serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r);
};

class SrsGoApiRusages : public ISrsHttpHandler
{
public:
    SrsGoApiRusages();
    virtual ~SrsGoApiRusages();
public:
    virtual srs_error_t serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r);
};

class SrsGoApiSelfProcStats : public ISrsHttpHandler
{
public:
    SrsGoApiSelfProcStats();
    virtual ~SrsGoApiSelfProcStats();
public:
    virtual srs_error_t serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r);
};

class SrsGoApiSystemProcStats : public ISrsHttpHandler
{
public:
    SrsGoApiSystemProcStats();
    virtual ~SrsGoApiSystemProcStats();
public:
    virtual srs_error_t serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r);
};

class SrsGoApiMemInfos : public ISrsHttpHandler
{
public:
    SrsGoApiMemInfos();
    virtual ~SrsGoApiMemInfos();
public:
    virtual srs_error_t serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r);
};

class SrsGoApiAuthors : public ISrsHttpHandler
{
public:
    SrsGoApiAuthors();
    virtual ~SrsGoApiAuthors();
public:
    virtual srs_error_t serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r);
};

class SrsGoApiFeatures : public ISrsHttpHandler
{
public:
    SrsGoApiFeatures();
    virtual ~SrsGoApiFeatures();
public:
    virtual srs_error_t serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r);
};

class SrsGoApiRequests : public ISrsHttpHandler
{
public:
    SrsGoApiRequests();
    virtual ~SrsGoApiRequests();
public:
    virtual srs_error_t serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r);
};

class SrsGoApiVhosts : public ISrsHttpHandler
{
public:
    SrsGoApiVhosts();
    virtual ~SrsGoApiVhosts();
public:
    virtual srs_error_t serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r);
};

class SrsGoApiStreams : public ISrsHttpHandler
{
public:
    SrsGoApiStreams();
    virtual ~SrsGoApiStreams();
public:
    virtual srs_error_t serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r);
};

class SrsGoApiClients : public ISrsHttpHandler
{
public:
    SrsGoApiClients();
    virtual ~SrsGoApiClients();
public:
    virtual srs_error_t serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r);
};

class SrsGoApiRaw : public ISrsHttpHandler, public ISrsReloadHandler
{
private:
    SrsServer* server;
private:
    bool raw_api;
    bool allow_reload;
    bool allow_query;
    bool allow_update;
public:
    SrsGoApiRaw(SrsServer* svr);
    virtual ~SrsGoApiRaw();
public:
    virtual srs_error_t serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r);
// Interface ISrsReloadHandler
public:
    virtual srs_error_t on_reload_http_api_raw_api();
};

class SrsGoApiClusters : public ISrsHttpHandler
{
public:
    SrsGoApiClusters();
    virtual ~SrsGoApiClusters();
public:
    virtual srs_error_t serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r);
};

class SrsGoApiError : public ISrsHttpHandler
{
public:
    SrsGoApiError();
    virtual ~SrsGoApiError();
public:
    virtual srs_error_t serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r);
};

#ifdef SRS_GPERF
class SrsGoApiTcmalloc : public ISrsHttpHandler
{
public:
    SrsGoApiTcmalloc();
    virtual ~SrsGoApiTcmalloc();
public:
    virtual srs_error_t serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r);
};
#endif

// Handle the HTTP API request.
class SrsHttpApi : public ISrsStartableConneciton, public ISrsHttpConnOwner
    , public ISrsReloadHandler
{
private:
    // The manager object to manage the connection.
    ISrsResourceManager* manager;
    SrsTcpConnection* skt;
    SrsSslConnection* ssl;
    SrsHttpConn* conn;
public:
    SrsHttpApi(bool https, ISrsResourceManager* cm, srs_netfd_t fd, SrsHttpServeMux* m, std::string cip, int port);
    virtual ~SrsHttpApi();
// Interface ISrsHttpConnOwner.
public:
    virtual srs_error_t on_start();
    virtual srs_error_t on_http_message(ISrsHttpMessage* r, SrsHttpResponseWriter* w);
    virtual srs_error_t on_message_done(ISrsHttpMessage* r, SrsHttpResponseWriter* w);
    virtual srs_error_t on_conn_done(srs_error_t r0);
// Interface ISrsResource.
public:
    virtual std::string desc();
// Interface ISrsKbpsDelta
public:
    virtual void remark(int64_t* in, int64_t* out);
// Interface ISrsReloadHandler
public:
    virtual srs_error_t on_reload_http_api_crossdomain();
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
// Interface ISrsConnection.
public:
    virtual std::string remote_ip();
    virtual const SrsContextId& get_id();
};

#endif

