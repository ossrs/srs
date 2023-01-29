//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#ifndef SRS_APP_HTTP_STATIC_HPP
#define SRS_APP_HTTP_STATIC_HPP

#include <srs_core.hpp>

#include <srs_app_http_conn.hpp>

class ISrsFileReaderFactory;

// HLS virtual connection, build on query string ctx of hls stream.
class SrsHlsVirtualConn: public ISrsExpire
{
public:
    srs_utime_t request_time;
    SrsRequest* req;
    std::string ctx;
    bool interrupt;
public:
    SrsHlsVirtualConn();
    virtual ~SrsHlsVirtualConn();
// Interface ISrsExpire.
public:
    virtual void expire();
};

// Server HLS streaming.
class SrsHlsStream : public ISrsFastTimer
{
private:
    // The period of validity of the ctx
    std::map<std::string, SrsHlsVirtualConn*> map_ctx_info_;
public:
    SrsHlsStream();
    virtual ~SrsHlsStream();
public:
    virtual srs_error_t serve_m3u8_ctx(ISrsHttpResponseWriter* w, ISrsHttpMessage* r, ISrsFileReaderFactory* factory, std::string fullpath, SrsRequest* req, bool* served);
    virtual void on_serve_ts_ctx(ISrsHttpResponseWriter* w, ISrsHttpMessage* r);
private:
    srs_error_t serve_new_session(ISrsHttpResponseWriter* w, ISrsHttpMessage* r, SrsRequest *req, std::string& ctx);
    srs_error_t serve_exists_session(ISrsHttpResponseWriter* w, ISrsHttpMessage* r, ISrsFileReaderFactory* factory, std::string fullpath);
    bool ctx_is_exist(std::string ctx);
    void alive(std::string ctx, SrsRequest* req);
    srs_error_t http_hooks_on_play(SrsRequest* req);
    void http_hooks_on_stop(SrsRequest* req);
    bool is_interrupt(std::string id);
// interface ISrsFastTimer
private:
    srs_error_t on_timer(srs_utime_t interval);
};

// The Vod streaming, like FLV, MP4 or HLS streaming.
class SrsVodStream : public SrsHttpFileServer
{
private:
    SrsHlsStream hls_;
public:
    SrsVodStream(std::string root_dir);
    virtual ~SrsVodStream();
protected:
    // The flv vod stream supports flv?start=offset-bytes.
    // For example, http://server/file.flv?start=10240
    // server will write flv header and sequence header,
    // then seek(10240) and response flv tag data.
    virtual srs_error_t serve_flv_stream(ISrsHttpResponseWriter* w, ISrsHttpMessage* r, std::string fullpath, int64_t offset);
    // Support mp4 with start and offset in query string.
    virtual srs_error_t serve_mp4_stream(ISrsHttpResponseWriter* w, ISrsHttpMessage* r, std::string fullpath, int64_t start, int64_t end);
    // Support HLS streaming with pseudo session id.
    virtual srs_error_t serve_m3u8_ctx(ISrsHttpResponseWriter* w, ISrsHttpMessage* r, std::string fullpath);
    virtual srs_error_t serve_ts_ctx(ISrsHttpResponseWriter* w, ISrsHttpMessage* r, std::string fullpath);
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
};

#endif

