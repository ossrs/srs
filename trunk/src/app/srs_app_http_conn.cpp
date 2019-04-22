/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2019 Winlin
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

#include <srs_app_http_conn.hpp>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>

#include <sstream>
using namespace std;

#include <srs_protocol_stream.hpp>
#include <srs_protocol_utility.hpp>
#include <srs_kernel_log.hpp>
#include <srs_kernel_error.hpp>
#include <srs_app_st.hpp>
#include <srs_core_autofree.hpp>
#include <srs_app_config.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_kernel_file.hpp>
#include <srs_kernel_flv.hpp>
#include <srs_rtmp_stack.hpp>
#include <srs_app_source.hpp>
#include <srs_rtmp_msg_array.hpp>
#include <srs_kernel_aac.hpp>
#include <srs_kernel_mp3.hpp>
#include <srs_kernel_ts.hpp>
#include <srs_app_pithy_print.hpp>
#include <srs_app_source.hpp>
#include <srs_app_server.hpp>
#include <srs_app_http_static.hpp>
#include <srs_app_http_stream.hpp>
#include <srs_app_http_api.hpp>
#include <srs_protocol_json.hpp>
#include <srs_app_http_hooks.hpp>
#include <srs_protocol_amf0.hpp>
#include <srs_app_utility.hpp>
#include <srs_app_st.hpp>

SrsHttpConn::SrsHttpConn(IConnectionManager* cm, srs_netfd_t fd, ISrsHttpServeMux* m, string cip) : SrsConnection(cm, fd, cip)
{
    parser = new SrsHttpParser();
    cors = new SrsHttpCorsMux();
    http_mux = m;
}

SrsHttpConn::~SrsHttpConn()
{
    srs_freep(parser);
    srs_freep(cors);
}

void SrsHttpConn::remark(int64_t* in, int64_t* out)
{
    // TODO: FIXME: implements it
}

srs_error_t SrsHttpConn::do_cycle()
{
    srs_error_t err = srs_success;
    
    srs_trace("HTTP client ip=%s", ip.c_str());
    
    // initialize parser
    if ((err = parser->initialize(HTTP_REQUEST, false)) != srs_success) {
        return srs_error_wrap(err, "init parser");
    }
    
    // set the recv timeout, for some clients never disconnect the connection.
    // @see https://github.com/ossrs/srs/issues/398
    skt->set_recv_timeout(SRS_HTTP_RECV_TIMEOUT);
    
    SrsRequest* last_req = NULL;
    SrsAutoFree(SrsRequest, last_req);
    
    // initialize the cors, which will proxy to mux.
    bool crossdomain_enabled = _srs_config->get_http_stream_crossdomain();
    if ((err = cors->initialize(http_mux, crossdomain_enabled)) != srs_success) {
        return srs_error_wrap(err, "init cors");
    }
    
    // process http messages.
    while ((err = trd->pull()) == srs_success) {
        ISrsHttpMessage* req = NULL;
        
        // get a http message
        if ((err = parser->parse_message(skt, &req)) != srs_success) {
            break;
        }
        
        // if SUCCESS, always NOT-NULL.
        // always free it in this scope.
        srs_assert(req);
        SrsAutoFree(ISrsHttpMessage, req);
        
        // Attach owner connection to message.
        SrsHttpMessage* hreq = (SrsHttpMessage*)req;
        hreq->set_connection(this);
        
        // copy request to last request object.
        srs_freep(last_req);
        last_req = hreq->to_request(hreq->host());
        
        // may should discard the body.
        if ((err = on_got_http_message(req)) != srs_success) {
            break;
        }
        
        // ok, handle http request.
        SrsHttpResponseWriter writer(skt);
        if ((err = process_request(&writer, req)) != srs_success) {
            break;
        }
        
        // donot keep alive, disconnect it.
        // @see https://github.com/ossrs/srs/issues/399
        if (!req->is_keep_alive()) {
            break;
        }
    }
    
    srs_error_t r0 = srs_success;
    if ((r0 = on_disconnect(last_req)) != srs_success) {
        err = srs_error_wrap(err, "on disconnect %s", srs_error_desc(r0).c_str());
        srs_freep(r0);
    }
    
    return err;
}

srs_error_t SrsHttpConn::process_request(ISrsHttpResponseWriter* w, ISrsHttpMessage* r)
{
    srs_error_t err = srs_success;
    
    srs_trace("HTTP %s %s, content-length=%" PRId64 "",
        r->method_str().c_str(), r->url().c_str(), r->content_length());
    
    // use cors server mux to serve http request, which will proxy to http_remux.
    if ((err = cors->serve_http(w, r)) != srs_success) {
        return srs_error_wrap(err, "mux serve");
    }
    
    return err;
}

srs_error_t SrsHttpConn::on_disconnect(SrsRequest* req)
{
    // TODO: FIXME: Implements it.
    return srs_success;
}

srs_error_t SrsHttpConn::on_reload_http_stream_crossdomain()
{
    srs_error_t err = srs_success;
    
    // initialize the cors, which will proxy to mux.
    bool crossdomain_enabled = _srs_config->get_http_stream_crossdomain();
    if ((err = cors->initialize(http_mux, crossdomain_enabled)) != srs_success) {
        return srs_error_wrap(err, "init mux");
    }
    
    return err;
}

SrsResponseOnlyHttpConn::SrsResponseOnlyHttpConn(IConnectionManager* cm, srs_netfd_t fd, ISrsHttpServeMux* m, string cip)
: SrsHttpConn(cm, fd, m, cip)
{
}

SrsResponseOnlyHttpConn::~SrsResponseOnlyHttpConn()
{
}

srs_error_t SrsResponseOnlyHttpConn::pop_message(ISrsHttpMessage** preq)
{
    srs_error_t err = srs_success;
    
    SrsStSocket skt;
    
    if ((err = skt.initialize(stfd)) != srs_success) {
        return srs_error_wrap(err, "init socket");
    }
    
    if ((err = parser->parse_message(&skt, preq)) != srs_success) {
        return srs_error_wrap(err, "parse message");
    }
    
    // Attach owner connection to message.
    SrsHttpMessage* hreq = (SrsHttpMessage*)(*preq);
    hreq->set_connection(this);
    
    return err;
}

srs_error_t SrsResponseOnlyHttpConn::on_got_http_message(ISrsHttpMessage* msg)
{
    srs_error_t err = srs_success;
    
    ISrsHttpResponseReader* br = msg->body_reader();
    
    // when not specified the content length, ignore.
    if (msg->content_length() == -1) {
        return err;
    }
    
    // drop all request body.
    char body[4096];
    while (!br->eof()) {
        if ((err = br->read(body, 4096, NULL)) != srs_success) {
            return srs_error_wrap(err, "read response");
        }
    }
    
    return err;
}

SrsHttpServer::SrsHttpServer(SrsServer* svr)
{
    server = svr;
    http_stream = new SrsHttpStreamServer(svr);
    http_static = new SrsHttpStaticServer(svr);
}

SrsHttpServer::~SrsHttpServer()
{
    srs_freep(http_stream);
    srs_freep(http_static);
}

srs_error_t SrsHttpServer::initialize()
{
    srs_error_t err = srs_success;
    
    // for SRS go-sharp to detect the status of HTTP server of SRS HTTP FLV Cluster.
    if ((err = http_static->mux.handle("/api/v1/versions", new SrsGoApiVersion())) != srs_success) {
        return srs_error_wrap(err, "handle versin");
    }
    
    if ((err = http_stream->initialize()) != srs_success) {
        return srs_error_wrap(err, "http stream");
    }
    
    if ((err = http_static->initialize()) != srs_success) {
        return srs_error_wrap(err, "http static");
    }
    
    return err;
}

srs_error_t SrsHttpServer::serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r)
{
    srs_error_t err = srs_success;
    
    // try http stream first.
    ISrsHttpHandler* h = NULL;
    if ((err = http_stream->mux.find_handler(r, &h)) != srs_success) {
        return srs_error_wrap(err, "find handler");
    }
    if (!h->is_not_found()) {
        return http_stream->mux.serve_http(w, r);
    }
    
    return http_static->mux.serve_http(w, r);
}

srs_error_t SrsHttpServer::http_mount(SrsSource* s, SrsRequest* r)
{
    return http_stream->http_mount(s, r);
}

void SrsHttpServer::http_unmount(SrsSource* s, SrsRequest* r)
{
    http_stream->http_unmount(s, r);
}

