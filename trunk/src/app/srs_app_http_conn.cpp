//
// Copyright (c) 2013-2021 Winlin
//
// SPDX-License-Identifier: MIT
//

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

ISrsHttpConnOwner::ISrsHttpConnOwner()
{
}

ISrsHttpConnOwner::~ISrsHttpConnOwner()
{
}

SrsHttpConn::SrsHttpConn(ISrsHttpConnOwner* handler, ISrsProtocolReadWriter* fd, ISrsHttpServeMux* m, string cip, int cport)
{
    parser = new SrsHttpParser();
    cors = new SrsHttpCorsMux();
    http_mux = m;
    handler_ = handler;

    skt = fd;
    ip = cip;
    port = cport;
    create_time = srsu2ms(srs_get_system_time());
    clk = new SrsWallClock();
    kbps = new SrsKbps(clk);
    kbps->set_io(skt, skt);
    trd = new SrsSTCoroutine("http", this, _srs_context->get_id());
}

SrsHttpConn::~SrsHttpConn()
{
    trd->interrupt();
    srs_freep(trd);

    srs_freep(parser);
    srs_freep(cors);

    srs_freep(kbps);
    srs_freep(clk);
}

std::string SrsHttpConn::desc()
{
    return "HttpConn";
}

void SrsHttpConn::remark(int64_t* in, int64_t* out)
{
    kbps->remark(in, out);
}

srs_error_t SrsHttpConn::start()
{
    srs_error_t err = srs_success;

    if ((err = trd->start()) != srs_success) {
        return srs_error_wrap(err, "coroutine");
    }

    return err;
}

srs_error_t SrsHttpConn::cycle()
{
    srs_error_t err = do_cycle();

    // Notify handler to handle it.
    // @remark The error may be transformed by handler.
    err = handler_->on_conn_done(err);

    // success.
    if (err == srs_success) {
        srs_trace("client finished.");
        return err;
    }

    // It maybe success with message.
    if (srs_error_code(err) == ERROR_SUCCESS) {
        srs_trace("client finished%s.", srs_error_summary(err).c_str());
        srs_freep(err);
        return err;
    }

    // client close peer.
    // TODO: FIXME: Only reset the error when client closed it.
    if (srs_is_client_gracefully_close(err)) {
        srs_warn("client disconnect peer. ret=%d", srs_error_code(err));
    } else if (srs_is_server_gracefully_close(err)) {
        srs_warn("server disconnect. ret=%d", srs_error_code(err));
    } else {
        srs_error("serve error %s", srs_error_desc(err).c_str());
    }

    srs_freep(err);
    return srs_success;
}

srs_error_t SrsHttpConn::do_cycle()
{
    srs_error_t err = srs_success;
    
    // set the recv timeout, for some clients never disconnect the connection.
    // @see https://github.com/ossrs/srs/issues/398
    skt->set_recv_timeout(SRS_HTTP_RECV_TIMEOUT);

    // initialize parser
    if ((err = parser->initialize(HTTP_REQUEST)) != srs_success) {
        return srs_error_wrap(err, "init parser for %s", ip.c_str());
    }

    // Notify the handler that we are starting to process the connection.
    if ((err = handler_->on_start()) != srs_success) {
        return srs_error_wrap(err, "start");
    }

    SrsRequest* last_req = NULL;
    SrsAutoFree(SrsRequest, last_req);

    // process all http messages.
    err = process_requests(&last_req);
    
    srs_error_t r0 = srs_success;
    if ((r0 = on_disconnect(last_req)) != srs_success) {
        err = srs_error_wrap(err, "on disconnect %s", srs_error_desc(r0).c_str());
        srs_freep(r0);
    }
    
    return err;
}

srs_error_t SrsHttpConn::process_requests(SrsRequest** preq)
{
    srs_error_t err = srs_success;

    for (int req_id = 0; ; req_id++) {
        if ((err = trd->pull()) != srs_success) {
            return srs_error_wrap(err, "pull");
        }

        // get a http message
        ISrsHttpMessage* req = NULL;
        if ((err = parser->parse_message(skt, &req)) != srs_success) {
            return srs_error_wrap(err, "parse message");
        }

        // if SUCCESS, always NOT-NULL.
        // always free it in this scope.
        srs_assert(req);
        SrsAutoFree(ISrsHttpMessage, req);

        // Attach owner connection to message.
        SrsHttpMessage* hreq = (SrsHttpMessage*)req;
        hreq->set_connection(this);

        // copy request to last request object.
        srs_freep(*preq);
        *preq = hreq->to_request(hreq->host());

        // may should discard the body.
        SrsHttpResponseWriter writer(skt);
        if ((err = handler_->on_http_message(req, &writer)) != srs_success) {
            return srs_error_wrap(err, "on http message");
        }

        // ok, handle http request.
        if ((err = process_request(&writer, req, req_id)) != srs_success) {
            return srs_error_wrap(err, "process request=%d", req_id);
        }

        // After the request is processed.
        if ((err = handler_->on_message_done(req, &writer)) != srs_success) {
            return srs_error_wrap(err, "on message done");
        }

        // donot keep alive, disconnect it.
        // @see https://github.com/ossrs/srs/issues/399
        if (!req->is_keep_alive()) {
            break;
        }
    }

    return err;
}

srs_error_t SrsHttpConn::process_request(ISrsHttpResponseWriter* w, ISrsHttpMessage* r, int rid)
{
    srs_error_t err = srs_success;
    
    srs_trace("HTTP #%d %s:%d %s %s, content-length=%" PRId64 "", rid, ip.c_str(), port,
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

ISrsHttpConnOwner* SrsHttpConn::handler()
{
    return handler_;
}

srs_error_t SrsHttpConn::pull()
{
    return trd->pull();
}

srs_error_t SrsHttpConn::set_crossdomain_enabled(bool v)
{
    srs_error_t err = srs_success;

    // initialize the cors, which will proxy to mux.
    if ((err = cors->initialize(http_mux, v)) != srs_success) {
        return srs_error_wrap(err, "init cors");
    }

    return err;
}

srs_error_t SrsHttpConn::set_jsonp(bool v)
{
    parser->set_jsonp(v);
    return srs_success;
}

string SrsHttpConn::remote_ip()
{
    return ip;
}

const SrsContextId& SrsHttpConn::get_id()
{
    return trd->cid();
}

void SrsHttpConn::expire()
{
    trd->interrupt();
}

SrsResponseOnlyHttpConn::SrsResponseOnlyHttpConn(bool https, ISrsResourceManager* cm, srs_netfd_t fd, ISrsHttpServeMux* m, string cip, int port)
{
    // Create a identify for this client.
    _srs_context->set_id(_srs_context->generate_id());

    manager = cm;
    skt = new SrsTcpConnection(fd);

    if (https) {
        ssl = new SrsSslConnection(skt);
        conn = new SrsHttpConn(this, ssl, m, cip, port);
    } else {
        ssl = NULL;
        conn = new SrsHttpConn(this, skt, m, cip, port);
    }

    _srs_config->subscribe(this);
}

SrsResponseOnlyHttpConn::~SrsResponseOnlyHttpConn()
{
    _srs_config->unsubscribe(this);

    srs_freep(conn);
    srs_freep(ssl);
    srs_freep(skt);
}

srs_error_t SrsResponseOnlyHttpConn::pop_message(ISrsHttpMessage** preq)
{
    srs_error_t err = srs_success;

    ISrsProtocolReadWriter* io = skt;
    if (ssl) {
        io = ssl;
    }

    // Check user interrupt by interval.
    io->set_recv_timeout(3 * SRS_UTIME_SECONDS);

    // We start a socket to read the stfd, which is writing by conn.
    // It's ok, because conn never read it after processing the HTTP request.
    // drop all request body.
    char body[4096];
    while (true) {
        if ((err = conn->pull()) != srs_success) {
            return srs_error_wrap(err, "timeout");
        }

        if ((err = io->read(body, 4096, NULL)) != srs_success) {
            // Because we use timeout to check trd state, so we should ignore any timeout.
            if (srs_error_code(err) == ERROR_SOCKET_TIMEOUT) {
                srs_freep(err);
                continue;
            }

            return srs_error_wrap(err, "read response");
        }
    }
    
    return err;
}

srs_error_t SrsResponseOnlyHttpConn::on_reload_http_stream_crossdomain()
{
    bool v = _srs_config->get_http_stream_crossdomain();
    return conn->set_crossdomain_enabled(v);
}

srs_error_t SrsResponseOnlyHttpConn::on_start()
{
    srs_error_t err = srs_success;

    if (ssl)  {
        srs_utime_t starttime = srs_update_system_time();
        string crt_file = _srs_config->get_https_stream_ssl_cert();
        string key_file = _srs_config->get_https_stream_ssl_key();
        if ((err = ssl->handshake(key_file, crt_file)) != srs_success) {
            return srs_error_wrap(err, "handshake");
        }

        int cost = srsu2msi(srs_update_system_time() - starttime);
        srs_trace("https: stream server done, use key %s and cert %s, cost=%dms",
            key_file.c_str(), crt_file.c_str(), cost);
    }

    return err;
}

srs_error_t SrsResponseOnlyHttpConn::on_http_message(ISrsHttpMessage* r, SrsHttpResponseWriter* w)
{
    srs_error_t err = srs_success;

    // After parsed the message, set the schema to https.
    if (ssl) {
        SrsHttpMessage* hm = dynamic_cast<SrsHttpMessage*>(r);
        hm->set_https(true);
    }
    
    ISrsHttpResponseReader* br = r->body_reader();

    // when not specified the content length, ignore.
    if (r->content_length() == -1) {
        return err;
    }

    // Drop all request body.
    // TODO: Should we set timeout for max reading?
    char body[4096];
    while (!br->eof()) {
        if ((err = br->read(body, 4096, NULL)) != srs_success) {
            return srs_error_wrap(err, "read response");
        }
    }
    
    return err;
}

srs_error_t SrsResponseOnlyHttpConn::on_message_done(ISrsHttpMessage* r, SrsHttpResponseWriter* w)
{
    return srs_success;
}

srs_error_t SrsResponseOnlyHttpConn::on_conn_done(srs_error_t r0)
{
    // Because we use manager to manage this object,
    // not the http connection object, so we must remove it here.
    manager->remove(this);

    return r0;
}

srs_error_t SrsResponseOnlyHttpConn::set_tcp_nodelay(bool v)
{
    return skt->set_tcp_nodelay(v);
}

srs_error_t SrsResponseOnlyHttpConn::set_socket_buffer(srs_utime_t buffer_v)
{
    return skt->set_socket_buffer(buffer_v);
}

std::string SrsResponseOnlyHttpConn::desc()
{
    if (ssl) {
        return "HttpsStream";
    }
    return "HttpStream";
}

std::string SrsResponseOnlyHttpConn::remote_ip()
{
    return conn->remote_ip();
}

const SrsContextId& SrsResponseOnlyHttpConn::get_id()
{
    return conn->get_id();
}

srs_error_t SrsResponseOnlyHttpConn::start()
{
    srs_error_t err = srs_success;

    bool v = _srs_config->get_http_stream_crossdomain();
    if ((err = conn->set_crossdomain_enabled(v)) != srs_success) {
        return srs_error_wrap(err, "set cors=%d", v);
    }

    if ((err = skt->initialize()) != srs_success) {
        return srs_error_wrap(err, "init socket");
    }

    return conn->start();
}

void SrsResponseOnlyHttpConn::remark(int64_t* in, int64_t* out)
{
    conn->remark(in, out);
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
        return srs_error_wrap(err, "handle versions");
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

srs_error_t SrsHttpServer::http_mount(SrsLiveSource* s, SrsRequest* r)
{
    return http_stream->http_mount(s, r);
}

void SrsHttpServer::http_unmount(SrsLiveSource* s, SrsRequest* r)
{
    http_stream->http_unmount(s, r);
}

