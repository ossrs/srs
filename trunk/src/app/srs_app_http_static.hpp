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

// HLS virtual connection, build on query string ctx of hls stream.
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
    // Interface ISrsExpire.
public:
    virtual void expire();
};

// Server HLS streaming.
class SrsHlsStream : public ISrsFastTimerHandler
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // The period of validity of the ctx
    std::map<std::string, SrsHlsVirtualConn *>
        map_ctx_info_;

public:
    SrsHlsStream();
    virtual ~SrsHlsStream();

public:
    void assemble(); // Construct object, to avoid call function in constructor.

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
    // interface ISrsFastTimerHandler
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
};

// The Vod streaming, like FLV, MP4 or HLS streaming.
class SrsVodStream : public SrsHttpFileServer
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    SrsHlsStream hls_;

public:
    SrsVodStream(std::string root_dir);
    virtual ~SrsVodStream();

public:
    void assemble(); // Construct object, to avoid call function in constructor.

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    // The flv vod stream supports flv?start=offset-bytes.
    // For example, http://server/file.flv?start=10240
    // server will write flv header and sequence header,
    // then seek(10240) and response flv tag data.
    virtual srs_error_t serve_flv_stream(ISrsHttpResponseWriter *w, ISrsHttpMessage *r, std::string fullpath, int64_t offset);
    // Support mp4 with start and offset in query string.
    virtual srs_error_t serve_mp4_stream(ISrsHttpResponseWriter *w, ISrsHttpMessage *r, std::string fullpath, int64_t start, int64_t end);
    // Support HLS streaming with pseudo session id.
    virtual srs_error_t serve_m3u8_ctx(ISrsHttpResponseWriter *w, ISrsHttpMessage *r, std::string fullpath);
    // the ts file including: .ts .m4s init.mp4
    virtual srs_error_t serve_ts_ctx(ISrsHttpResponseWriter *w, ISrsHttpMessage *r, std::string fullpath);
};

// The http static server instance,
class ISrsHttpStaticServer : public ISrsHttpHandler
{
public:
    ISrsHttpStaticServer();
    virtual ~ISrsHttpStaticServer();

public:
    virtual srs_error_t initialize() = 0;
    virtual ISrsHttpServeMux *mux() = 0;
};

// The http static server instance,
// serve http static file and flv/mp4 vod stream.
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

    // Interface ISrsHttpHandler
public:
    virtual srs_error_t serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t mount_vhost(std::string vhost, std::string &pmount);
};

#endif
