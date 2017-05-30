/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2017 OSSRS(winlin)
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

SrsHttpConn::SrsHttpConn(IConnectionManager* cm, srs_netfd_t fd, ISrsHttpServeMux* m, string cip)
: SrsConnection(cm, fd, cip)
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

void SrsHttpConn::resample()
{
    // TODO: FIXME: implements it
}

int64_t SrsHttpConn::get_send_bytes_delta()
{
    // TODO: FIXME: implements it
    return 0;
}

int64_t SrsHttpConn::get_recv_bytes_delta()
{
    // TODO: FIXME: implements it
    return 0;
}

void SrsHttpConn::cleanup()
{
    // TODO: FIXME: implements it
}

int SrsHttpConn::do_cycle()
{
    int ret = ERROR_SUCCESS;
    
    srs_trace("HTTP client ip=%s", ip.c_str());
    
    // initialize parser
    if ((ret = parser->initialize(HTTP_REQUEST, false)) != ERROR_SUCCESS) {
        srs_error("http initialize http parser failed. ret=%d", ret);
        return ret;
    }
    
    // set the recv timeout, for some clients never disconnect the connection.
    // @see https://github.com/ossrs/srs/issues/398
    skt->set_recv_timeout(SRS_HTTP_RECV_TMMS);
    
    SrsRequest* last_req = NULL;
    SrsAutoFree(SrsRequest, last_req);
    
    // initialize the cors, which will proxy to mux.
    bool crossdomain_enabled = _srs_config->get_http_stream_crossdomain();
    if ((ret = cors->initialize(http_mux, crossdomain_enabled)) != ERROR_SUCCESS) {
        return ret;
    }
    
    // process http messages.
    while (!trd->pull()) {
        ISrsHttpMessage* req = NULL;
        
        // get a http message
        if ((ret = parser->parse_message(skt, this, &req)) != ERROR_SUCCESS) {
            break;
        }
        
        // if SUCCESS, always NOT-NULL.
        srs_assert(req);
        
        // always free it in this scope.
        SrsAutoFree(ISrsHttpMessage, req);
        
        // get the last request, for report the info of request on connection disconnect.
        delete last_req;
        SrsHttpMessage* hreq = dynamic_cast<SrsHttpMessage*>(req);
        last_req = hreq->to_request(hreq->host());
        
        // may should discard the body.
        if ((ret = on_got_http_message(req)) != ERROR_SUCCESS) {
            break;
        }
        
        // ok, handle http request.
        SrsHttpResponseWriter writer(skt);
        if ((ret = process_request(&writer, req)) != ERROR_SUCCESS) {
            break;
        }
        
        // donot keep alive, disconnect it.
        // @see https://github.com/ossrs/srs/issues/399
        if (!req->is_keep_alive()) {
            break;
        }
    }
    
    int disc_ret = ERROR_SUCCESS;
    if ((disc_ret = on_disconnect(last_req)) != ERROR_SUCCESS) {
        srs_warn("connection on disconnect peer failed, but ignore this error. disc_ret=%d, ret=%d", disc_ret, ret);
    }
    
    return ret;
}

int SrsHttpConn::process_request(ISrsHttpResponseWriter* w, ISrsHttpMessage* r)
{
    int ret = ERROR_SUCCESS;
    
    srs_trace("HTTP %s %s, content-length=%" PRId64 "",
              r->method_str().c_str(), r->url().c_str(), r->content_length());
    
    // use cors server mux to serve http request, which will proxy to http_remux.
    if ((ret = cors->serve_http(w, r)) != ERROR_SUCCESS) {
        if (!srs_is_client_gracefully_close(ret)) {
            srs_error("serve http msg failed. ret=%d", ret);
        }
        return ret;
    }
    
    return ret;
}

int SrsHttpConn::on_disconnect(SrsRequest* req)
{
    int ret = ERROR_SUCCESS;
    // TODO: implements it.s
    return ret;
}

int SrsHttpConn::on_reload_http_stream_crossdomain()
{
    int ret = ERROR_SUCCESS;
    
    // initialize the cors, which will proxy to mux.
    bool crossdomain_enabled = _srs_config->get_http_stream_crossdomain();
    if ((ret = cors->initialize(http_mux, crossdomain_enabled)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

SrsResponseOnlyHttpConn::SrsResponseOnlyHttpConn(IConnectionManager* cm, srs_netfd_t fd, ISrsHttpServeMux* m, string cip)
: SrsHttpConn(cm, fd, m, cip)
{
}

SrsResponseOnlyHttpConn::~SrsResponseOnlyHttpConn()
{
}

int SrsResponseOnlyHttpConn::pop_message(ISrsHttpMessage** preq)
{
    int ret = ERROR_SUCCESS;
    
    SrsStSocket skt;
    
    if ((ret = skt.initialize(stfd)) != ERROR_SUCCESS) {
        return ret;
    }
    
    if ((ret = parser->parse_message(&skt, this, preq)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int SrsResponseOnlyHttpConn::on_got_http_message(ISrsHttpMessage* msg)
{
    int ret = ERROR_SUCCESS;
    
    ISrsHttpResponseReader* br = msg->body_reader();
    
    // when not specified the content length, ignore.
    if (msg->content_length() == -1) {
        return ret;
    }
    
    // drop all request body.
    while (!br->eof()) {
        char body[4096];
        if ((ret = br->read(body, 4096, NULL)) != ERROR_SUCCESS) {
            return ret;
        }
    }
    
    return ret;
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

int SrsHttpServer::initialize()
{
    int ret = ERROR_SUCCESS;
    
    // for SRS go-sharp to detect the status of HTTP server of SRS HTTP FLV Cluster.
    if ((ret = http_static->mux.handle("/api/v1/versions", new SrsGoApiVersion())) != ERROR_SUCCESS) {
        return ret;
    }
    
    if ((ret = http_stream->initialize()) != ERROR_SUCCESS) {
        return ret;
    }
    
    if ((ret = http_static->initialize()) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int SrsHttpServer::serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r)
{
    // try http stream first.
    if (http_stream->mux.can_serve(r)) {
        return http_stream->mux.serve_http(w, r);
    }
    
    return http_static->mux.serve_http(w, r);
}

int SrsHttpServer::http_mount(SrsSource* s, SrsRequest* r)
{
    return http_stream->http_mount(s, r);
}

void SrsHttpServer::http_unmount(SrsSource* s, SrsRequest* r)
{
    http_stream->http_unmount(s, r);
}

