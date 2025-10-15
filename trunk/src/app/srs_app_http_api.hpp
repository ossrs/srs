//
// Copyright (c) 2013-2025 The SRS Authors
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
class ISrsRequest;
class ISrsHttpResponseWriter;
class SrsHttpConn;
class ISrsSignalHandler;
class ISrsStatistic;
class ISrsAppConfig;

#include <string>

#include <srs_app_http_conn.hpp>
#include <srs_app_reload.hpp>
#include <srs_app_st.hpp>
#include <srs_protocol_conn.hpp>
#include <srs_protocol_http_stack.hpp>

extern srs_error_t srs_api_response(ISrsHttpResponseWriter *w, ISrsHttpMessage *r, std::string json);
extern srs_error_t srs_api_response_code(ISrsHttpResponseWriter *w, ISrsHttpMessage *r, int code);
extern srs_error_t srs_api_response_code(ISrsHttpResponseWriter *w, ISrsHttpMessage *r, srs_error_t code);

// For http root.
class SrsGoApiRoot : public ISrsHttpHandler
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsStatistic *stat_;

public:
    SrsGoApiRoot();
    virtual ~SrsGoApiRoot();

public:
    virtual srs_error_t serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r);
};

class SrsGoApiApi : public ISrsHttpHandler
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsStatistic *stat_;

public:
    SrsGoApiApi();
    virtual ~SrsGoApiApi();

public:
    virtual srs_error_t serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r);
};

class SrsGoApiV1 : public ISrsHttpHandler
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsStatistic *stat_;

public:
    SrsGoApiV1();
    virtual ~SrsGoApiV1();

public:
    virtual srs_error_t serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r);
};

class SrsGoApiVersion : public ISrsHttpHandler
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsStatistic *stat_;

public:
    SrsGoApiVersion();
    virtual ~SrsGoApiVersion();

public:
    virtual srs_error_t serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r);
};

class SrsGoApiSummaries : public ISrsHttpHandler
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsStatistic *stat_;

public:
    SrsGoApiSummaries();
    virtual ~SrsGoApiSummaries();

public:
    virtual srs_error_t serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r);
};

class SrsGoApiRusages : public ISrsHttpHandler
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsStatistic *stat_;

public:
    SrsGoApiRusages();
    virtual ~SrsGoApiRusages();

public:
    virtual srs_error_t serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r);
};

class SrsGoApiSelfProcStats : public ISrsHttpHandler
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsStatistic *stat_;

public:
    SrsGoApiSelfProcStats();
    virtual ~SrsGoApiSelfProcStats();

public:
    virtual srs_error_t serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r);
};

class SrsGoApiSystemProcStats : public ISrsHttpHandler
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsStatistic *stat_;

public:
    SrsGoApiSystemProcStats();
    virtual ~SrsGoApiSystemProcStats();

public:
    virtual srs_error_t serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r);
};

class SrsGoApiMemInfos : public ISrsHttpHandler
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsStatistic *stat_;

public:
    SrsGoApiMemInfos();
    virtual ~SrsGoApiMemInfos();

public:
    virtual srs_error_t serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r);
};

class SrsGoApiAuthors : public ISrsHttpHandler
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsStatistic *stat_;

public:
    SrsGoApiAuthors();
    virtual ~SrsGoApiAuthors();

public:
    virtual srs_error_t serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r);
};

class SrsGoApiFeatures : public ISrsHttpHandler
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsStatistic *stat_;

public:
    SrsGoApiFeatures();
    virtual ~SrsGoApiFeatures();

public:
    virtual srs_error_t serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r);
};

class SrsGoApiRequests : public ISrsHttpHandler
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsStatistic *stat_;

public:
    SrsGoApiRequests();
    virtual ~SrsGoApiRequests();

public:
    virtual srs_error_t serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r);
};

class SrsGoApiVhosts : public ISrsHttpHandler
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsStatistic *stat_;

public:
    SrsGoApiVhosts();
    virtual ~SrsGoApiVhosts();

public:
    virtual srs_error_t serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r);
};

class SrsGoApiStreams : public ISrsHttpHandler
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsStatistic *stat_;

public:
    SrsGoApiStreams();
    virtual ~SrsGoApiStreams();

public:
    virtual srs_error_t serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r);
};

class SrsGoApiClients : public ISrsHttpHandler
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsStatistic *stat_;

public:
    SrsGoApiClients();
    virtual ~SrsGoApiClients();

public:
    virtual srs_error_t serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r);
};

class SrsGoApiRaw : public ISrsHttpHandler, public ISrsReloadHandler
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsStatistic *stat_;
    ISrsAppConfig *config_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsSignalHandler *handler_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    bool raw_api_;
    bool allow_reload_;
    bool allow_query_;
    bool allow_update_;

public:
    SrsGoApiRaw(ISrsSignalHandler *handler);
    void assemble();
    virtual ~SrsGoApiRaw();

public:
    virtual srs_error_t serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r);
};

class SrsGoApiClusters : public ISrsHttpHandler
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsStatistic *stat_;

public:
    SrsGoApiClusters();
    virtual ~SrsGoApiClusters();

public:
    virtual srs_error_t serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r);
};

class SrsGoApiError : public ISrsHttpHandler
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsStatistic *stat_;

public:
    SrsGoApiError();
    virtual ~SrsGoApiError();

public:
    virtual srs_error_t serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r);
};

#ifdef SRS_GPERF
class SrsGoApiTcmalloc : public ISrsHttpHandler
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsStatistic *stat_;

public:
    SrsGoApiTcmalloc();
    virtual ~SrsGoApiTcmalloc();

public:
    virtual srs_error_t serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r);
};
#endif

#ifdef SRS_VALGRIND
class SrsGoApiValgrind : public ISrsHttpHandler, public ISrsCoroutineHandler
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsStatistic *stat_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsCoroutine *trd_;
    std::string task_;

public:
    SrsGoApiValgrind();
    virtual ~SrsGoApiValgrind();

public:
    virtual srs_error_t serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r);
    // Interface ISrsCoroutineHandler
public:
    virtual srs_error_t cycle();
};
#endif

#ifdef SRS_SIGNAL_API
class SrsGoApiSignal : public ISrsHttpHandler
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsStatistic *stat_;

public:
    SrsGoApiSignal();
    virtual ~SrsGoApiSignal();

public:
    virtual srs_error_t serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r);
};
#endif

class SrsGoApiMetrics : public ISrsHttpHandler
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsStatistic *stat_;
    ISrsAppConfig *config_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    bool enabled_;
    std::string label_;
    std::string tag_;

public:
    SrsGoApiMetrics();
    void assemble();
    virtual ~SrsGoApiMetrics();

public:
    virtual srs_error_t serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r);
};

#endif
