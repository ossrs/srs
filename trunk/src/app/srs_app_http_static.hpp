//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_APP_HTTP_STATIC_HPP
#define SRS_APP_HTTP_STATIC_HPP

#include <srs_app_http_conn.hpp>
#include <srs_app_security.hpp>
#include <srs_core.hpp>

class ISrsFileReaderFactory;
class ISrsCommonHttpHandler;
class ISrsHttpServeMux;
class ISrsAppConfig;
class ISrsStatistic;
class ISrsHttpHooks;
class ISrsSharedTimer;
class ISrsRand;

// An HLS viewer has no connection of its own, so the session named by the hls_ctx query stands for it.
class SrsHlsVirtualConn : public ISrsExpire
{
public:
    srs_utime_t request_time_;
    ISrsRequest *req_;
    std::string ctx_;
    bool interrupt_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsStatistic *stat_;

public:
    SrsHlsVirtualConn();
    virtual ~SrsHlsVirtualConn();

public:
    virtual void expire();
};

class SrsHlsStream : public ISrsFastTimerHandler
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    std::map<std::string, SrsHlsVirtualConn *>
        map_ctx_info_;

public:
    SrsHlsStream();
    virtual ~SrsHlsStream();

public:
    void assemble(); // Not in the constructor, to avoid calling functions there.

public:
    virtual srs_error_t serve_m3u8_ctx(ISrsHttpResponseWriter *w, ISrsHttpMessage *r, ISrsFileReaderFactory *factory, std::string fullpath, ISrsRequest *req, bool *served);
    virtual void on_serve_ts_ctx(ISrsHttpResponseWriter *w, ISrsHttpMessage *r);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srs_error_t serve_new_session(ISrsHttpResponseWriter *w, ISrsHttpMessage *r, ISrsRequest *req, std::string &ctx);
    srs_error_t serve_exists_session(ISrsHttpResponseWriter *w, ISrsHttpMessage *r, ISrsFileReaderFactory *factory, std::string fullpath);
    bool ctx_is_exist(std::string ctx);
    void alive(std::string ctx, ISrsRequest *req);
    srs_error_t http_hooks_on_play(ISrsRequest *req);
    void http_hooks_on_stop(ISrsRequest *req);
    bool is_interrupt(std::string id);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srs_error_t on_timer(srs_utime_t interval);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsSecurity *security_;
    ISrsAppConfig *config_;
    ISrsStatistic *stat_;
    ISrsHttpHooks *hooks_;
    ISrsSharedTimer *shared_timer_;
    ISrsRand *rand_;
};

class SrsVodStream : public SrsHttpFileServer
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    SrsHlsStream hls_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsAppConfig *config_;

public:
    SrsVodStream(std::string root_dir);
    virtual ~SrsVodStream();

public:
    void assemble(); // Not in the constructor, to avoid calling functions there.

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    // The start is a byte offset. For example, http://server/file.flv?start=10240 gets the FLV header and
    // sequence header, then the tags from byte 10240.
    virtual srs_error_t serve_flv_stream(ISrsHttpResponseWriter *w, ISrsHttpMessage *r, std::string fullpath, int64_t offset);
    virtual srs_error_t serve_mp4_stream(ISrsHttpResponseWriter *w, ISrsHttpMessage *r, std::string fullpath, int64_t start, int64_t end);
    virtual srs_error_t serve_m3u8_ctx(ISrsHttpResponseWriter *w, ISrsHttpMessage *r, std::string fullpath);
    // Also serves .m4s and init.mp4 files.
    virtual srs_error_t serve_ts_ctx(ISrsHttpResponseWriter *w, ISrsHttpMessage *r, std::string fullpath);
};

class ISrsHttpStaticServer : public ISrsHttpHandler
{
public:
    ISrsHttpStaticServer();
    virtual ~ISrsHttpStaticServer();

public:
    virtual srs_error_t initialize() = 0;
    virtual ISrsHttpServeMux *mux() = 0;
};

class SrsHttpStaticServer : public ISrsHttpStaticServer
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsHttpServeMux *mux_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsAppConfig *config_;

public:
    SrsHttpStaticServer();
    virtual ~SrsHttpStaticServer();

public:
    virtual srs_error_t initialize();
    virtual ISrsHttpServeMux *mux();

public:
    virtual srs_error_t serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t mount_vhost(std::string vhost, std::string &pmount);
};

#endif
