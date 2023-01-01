//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
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
#include <srs_protocol_http_stack.hpp>
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

class SrsGoApiMetrics : public ISrsHttpHandler
{
private:
    bool enabled_;
    std::string label_;
    std::string tag_;
public:
    SrsGoApiMetrics();
    virtual ~SrsGoApiMetrics();
public:
    virtual srs_error_t serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r);
};

#endif

