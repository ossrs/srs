//
// Copyright (c) 2013-2021 Winlin
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_APP_HTTP_STATIC_HPP
#define SRS_APP_HTTP_STATIC_HPP

#include <srs_core.hpp>

#include <srs_app_http_conn.hpp>

// The flv vod stream supports flv?start=offset-bytes.
// For example, http://server/file.flv?start=10240
// server will write flv header and sequence header,
// then seek(10240) and response flv tag data.
class SrsVodStream : public SrsHttpFileServer
{
public:
    SrsVodStream(std::string root_dir);
    virtual ~SrsVodStream();
protected:
    virtual srs_error_t serve_flv_stream(ISrsHttpResponseWriter* w, ISrsHttpMessage* r, std::string fullpath, int offset);
    virtual srs_error_t serve_mp4_stream(ISrsHttpResponseWriter* w, ISrsHttpMessage* r, std::string fullpath, int start, int end);
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

