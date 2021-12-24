//
// Copyright (c) 2013-2021 Winlin
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_APP_HTTP_STATIC_HPP
#define SRS_APP_HTTP_STATIC_HPP

#include <srs_core.hpp>

#include <srs_app_http_conn.hpp>

struct SrsM3u8CtxInfo
{
    srs_utime_t request_time;
    SrsRequest* req;
};

// The flv vod stream supports flv?start=offset-bytes.
// For example, http://server/file.flv?start=10240
// server will write flv header and sequence header,
// then seek(10240) and response flv tag data.
class SrsVodStream : public SrsHttpFileServer, public ISrsFastTimer
{
private:
    // The period of validity of the ctx
    std::map<std::string, SrsM3u8CtxInfo> map_ctx_info_;
public:
    SrsVodStream(std::string root_dir);
    virtual ~SrsVodStream();
protected:
    virtual srs_error_t serve_flv_stream(ISrsHttpResponseWriter* w, ISrsHttpMessage* r, std::string fullpath, int64_t offset);
    virtual srs_error_t serve_mp4_stream(ISrsHttpResponseWriter* w, ISrsHttpMessage* r, std::string fullpath, int64_t start, int64_t end);
    virtual srs_error_t serve_m3u8_ctx(ISrsHttpResponseWriter* w, ISrsHttpMessage* r, std::string fullpath);
private:
    virtual bool ctx_is_exist(std::string ctx);
    virtual void alive(std::string ctx, SrsRequest* req);
    virtual srs_error_t http_hooks_on_play(SrsRequest* req);
    virtual void http_hooks_on_stop(SrsRequest* req);
// interface ISrsFastTimer
private:
    srs_error_t on_timer(srs_utime_t interval);
};

// The http static server instance,
// serve http static file and flv/mp4 vod stream.
class SrsHttpStaticServer : public ISrsReloadHandler
{
private:
    SrsServer* server;
public:
    SrsHttpServeMux mux;
public:
    SrsHttpStaticServer(SrsServer* svr);
    virtual ~SrsHttpStaticServer();
public:
    virtual srs_error_t initialize();
private:
    virtual srs_error_t mount_vhost(std::string vhost, std::string& pmount);
// Interface ISrsReloadHandler.
public:
    virtual srs_error_t on_reload_vhost_added(std::string vhost);
    virtual srs_error_t on_reload_vhost_http_updated();
};

#endif

