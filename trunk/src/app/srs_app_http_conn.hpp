/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2020 Winlin
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef SRS_APP_HTTP_CONN_HPP
#define SRS_APP_HTTP_CONN_HPP

#include <srs_core.hpp>

#include <map>
#include <string>
#include <vector>

#include <srs_service_http_conn.hpp>
#include <srs_app_reload.hpp>
#include <srs_kernel_file.hpp>
#include <srs_app_thread.hpp>
#include <srs_app_conn.hpp>
#include <srs_app_source.hpp>

class SrsServer;
class SrsSource;
class SrsRequest;
class SrsConsumer;
class SrsStSocket;
class SrsHttpParser;
class ISrsHttpMessage;
class SrsHttpHandler;
class SrsMessageQueue;
class SrsSharedPtrMessage;
class SrsRequest;
class SrsFastStream;
class SrsHttpUri;
class SrsConnection;
class SrsHttpMessage;
class SrsHttpStreamServer;
class SrsHttpStaticServer;

// The http connection which request the static or stream content.
class SrsHttpConn : public SrsConnection
{
protected:
    SrsHttpParser* parser;
    ISrsHttpServeMux* http_mux;
    SrsHttpCorsMux* cors;
public:
    SrsHttpConn(IConnectionManager* cm, srs_netfd_t fd, ISrsHttpServeMux* m, std::string cip);
    virtual ~SrsHttpConn();
// Interface ISrsKbpsDelta
public:
    virtual void remark(int64_t* in, int64_t* out);
protected:
    virtual srs_error_t do_cycle();
protected:
    // When got http message,
    // for the static service or api, discard any body.
    // for the stream caster, for instance, http flv streaming, may discard the flv header or not.
    virtual srs_error_t on_got_http_message(ISrsHttpMessage* msg) = 0;
private:
    virtual srs_error_t process_request(ISrsHttpResponseWriter* w, ISrsHttpMessage* r);
    // When the connection disconnect, call this method.
    // e.g. log msg of connection and report to other system.
    // @param request: request which is converted by the last http message.
    virtual srs_error_t on_disconnect(SrsRequest* req);
// Interface ISrsReloadHandler
public:
    virtual srs_error_t on_reload_http_stream_crossdomain();
};

// Drop body of request, only process the response.
class SrsResponseOnlyHttpConn : public SrsHttpConn
{
public:
    SrsResponseOnlyHttpConn(IConnectionManager* cm, srs_netfd_t fd, ISrsHttpServeMux* m, std::string cip);
    virtual ~SrsResponseOnlyHttpConn();
public:
    // Directly read a HTTP request message.
    // It's exported for HTTP stream, such as HTTP FLV, only need to write to client when
    // serving it, but we need to start a thread to read message to detect whether FD is closed.
    // @see https://github.com/ossrs/srs/issues/636#issuecomment-298208427
    // @remark Should only used in HTTP-FLV streaming connection.
    virtual srs_error_t pop_message(ISrsHttpMessage** preq);
public:
    virtual srs_error_t on_got_http_message(ISrsHttpMessage* msg);
public:
    // Set connection to expired.
    virtual void expire();
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
    virtual srs_error_t http_mount(SrsSource* s, SrsRequest* r);
    virtual void http_unmount(SrsSource* s, SrsRequest* r);
};

#endif

