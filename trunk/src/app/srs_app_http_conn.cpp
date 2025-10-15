//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_app_http_conn.hpp>

#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <sstream>
using namespace std;

#include <srs_app_config.hpp>
#include <srs_app_http_api.hpp>
#include <srs_app_http_hooks.hpp>
#include <srs_app_http_static.hpp>
#include <srs_app_http_stream.hpp>
#include <srs_app_rtmp_source.hpp>
#include <srs_app_server.hpp>
#include <srs_app_st.hpp>
#include <srs_app_statistic.hpp>
#include <srs_app_utility.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_aac.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_file.hpp>
#include <srs_kernel_flv.hpp>
#include <srs_kernel_log.hpp>
#include <srs_kernel_mp3.hpp>
#include <srs_kernel_pithy_print.hpp>
#include <srs_kernel_ts.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_amf0.hpp>
#include <srs_protocol_json.hpp>
#include <srs_protocol_rtmp_msg_array.hpp>
#include <srs_protocol_rtmp_stack.hpp>
#include <srs_protocol_stream.hpp>
#include <srs_protocol_utility.hpp>

ISrsHttpConnOwner::ISrsHttpConnOwner()
{
}

ISrsHttpConnOwner::~ISrsHttpConnOwner()
{
}

ISrsHttpConn::ISrsHttpConn()
{
}

ISrsHttpConn::~ISrsHttpConn()
{
}

SrsHttpConn::SrsHttpConn(ISrsHttpConnOwner *handler, ISrsProtocolReadWriter *fd, ISrsCommonHttpHandler *m, string cip, int cport)
{
    parser_ = new SrsHttpParser();
    auth_ = new SrsHttpAuthMux(m);
    cors_ = new SrsHttpCorsMux(auth_);

    http_mux_ = m;
    handler_ = handler;

    skt_ = fd;
    ip_ = cip;
    port_ = cport;
    create_time_ = srs_time_now_cached();
    delta_ = new SrsNetworkDelta();
    delta_->set_io(skt_, skt_);
    trd_ = new SrsSTCoroutine("http", this, _srs_context->get_id());

    config_ = _srs_config;
}

SrsHttpConn::~SrsHttpConn()
{
    trd_->interrupt();
    srs_freep(trd_);

    srs_freep(parser_);
    srs_freep(cors_);
    srs_freep(auth_);

    srs_freep(delta_);

    config_ = NULL;
}

std::string SrsHttpConn::desc()
{
    return "HttpConn";
}

ISrsKbpsDelta *SrsHttpConn::delta()
{
    return delta_;
}

srs_error_t SrsHttpConn::start()
{
    srs_error_t err = srs_success;

    if ((err = trd_->start()) != srs_success) {
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
    skt_->set_recv_timeout(SRS_HTTP_RECV_TIMEOUT);

    // initialize parser
    if ((err = parser_->initialize(HTTP_REQUEST)) != srs_success) {
        return srs_error_wrap(err, "init parser for %s", ip_.c_str());
    }

    // Notify the handler that we are starting to process the connection.
    if ((err = handler_->on_start()) != srs_success) {
        return srs_error_wrap(err, "start");
    }

    // process all http messages.
    ISrsRequest *last_req_raw = NULL;
    err = process_requests(&last_req_raw);
    SrsUniquePtr<ISrsRequest> last_req(last_req_raw);

    srs_error_t r0 = srs_success;
    if ((r0 = on_disconnect(last_req.get())) != srs_success) {
        err = srs_error_wrap(err, "on disconnect %s", srs_error_desc(r0).c_str());
        srs_freep(r0);
    }

    return err;
}

srs_error_t SrsHttpConn::process_requests(ISrsRequest **preq)
{
    srs_error_t err = srs_success;

    for (int req_id = 0;; req_id++) {
        if ((err = trd_->pull()) != srs_success) {
            return srs_error_wrap(err, "pull");
        }

        // get a http message
        ISrsHttpMessage *req_raw = NULL;
        if ((err = parser_->parse_message(skt_, &req_raw)) != srs_success) {
            return srs_error_wrap(err, "parse message");
        }

        // if SUCCESS, always NOT-NULL.
        // always free it in this scope.
        srs_assert(req_raw);
        SrsUniquePtr<ISrsHttpMessage> req(req_raw);

        // Attach owner connection to message.
        SrsHttpMessage *hreq = (SrsHttpMessage *)req.get();
        hreq->set_connection(this);

        // copy request to last request object.
        srs_freep(*preq);
        *preq = hreq->to_request(hreq->host());

        // may should discard the body.
        SrsHttpResponseWriter writer(skt_);
        if ((err = handler_->on_http_message(req.get(), &writer)) != srs_success) {
            return srs_error_wrap(err, "on http message");
        }

        // ok, handle http request.
        if ((err = process_request(&writer, req.get(), req_id)) != srs_success) {
            return srs_error_wrap(err, "process request=%d", req_id);
        }

        // After the request is processed.
        if ((err = handler_->on_message_done(req.get(), &writer)) != srs_success) {
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

srs_error_t SrsHttpConn::process_request(ISrsHttpResponseWriter *w, ISrsHttpMessage *r, int rid)
{
    srs_error_t err = srs_success;

    srs_trace("HTTP #%d %s:%d %s %s, content-length=%" PRId64 "", rid, ip_.c_str(), port_,
              r->method_str().c_str(), r->url().c_str(), r->content_length());

    // proxy to cors-->auth-->http_remux.
    if ((err = cors_->serve_http(w, r)) != srs_success) {
        return srs_error_wrap(err, "cors serve");
    }

    return err;
}

srs_error_t SrsHttpConn::on_disconnect(ISrsRequest *req)
{
    // TODO: FIXME: Implements it.
    return srs_success;
}

ISrsHttpConnOwner *SrsHttpConn::handler()
{
    return handler_;
}

srs_error_t SrsHttpConn::pull()
{
    return trd_->pull();
}

srs_error_t SrsHttpConn::set_crossdomain_enabled(bool v)
{
    srs_error_t err = srs_success;

    if ((err = cors_->initialize(v)) != srs_success) {
        return srs_error_wrap(err, "init cors");
    }

    return err;
}

srs_error_t SrsHttpConn::set_auth_enabled(bool auth_enabled)
{
    srs_error_t err = srs_success;

    // initialize the auth, which will proxy to mux.
    if ((err = auth_->initialize(auth_enabled,
                                 config_->get_http_api_auth_username(),
                                 config_->get_http_api_auth_password())) != srs_success) {
        return srs_error_wrap(err, "init auth");
    }

    return err;
}

srs_error_t SrsHttpConn::set_jsonp(bool v)
{
    parser_->set_jsonp(v);
    return srs_success;
}

string SrsHttpConn::remote_ip()
{
    return ip_;
}

const SrsContextId &SrsHttpConn::get_id()
{
    return trd_->cid();
}

void SrsHttpConn::expire()
{
    trd_->interrupt();
}

ISrsHttpxConn::ISrsHttpxConn()
{
}

ISrsHttpxConn::~ISrsHttpxConn()
{
}

SrsHttpxConn::SrsHttpxConn(ISrsResourceManager *cm, ISrsProtocolReadWriter *io, ISrsCommonHttpHandler *m, string cip, int port, string key, string cert) : manager_(cm), io_(io), enable_stat_(false), ssl_key_file_(key), ssl_cert_file_(cert)
{
    // Create a identify for this client.
    _srs_context->set_id(_srs_context->generate_id());

    if (!ssl_key_file_.empty() &&
        !ssl_cert_file_.empty()) {
        ssl_ = new SrsSslConnection(io_);
        conn_ = new SrsHttpConn(this, ssl_, m, cip, port);
    } else {
        ssl_ = NULL;
        conn_ = new SrsHttpConn(this, io_, m, cip, port);
    }

    config_ = _srs_config;
    stat_ = _srs_stat;
}

SrsHttpxConn::~SrsHttpxConn()
{
    srs_freep(conn_);
    srs_freep(ssl_);
    srs_freep(io_);

    config_ = NULL;
    stat_ = NULL;
}

void SrsHttpxConn::set_enable_stat(bool v)
{
    enable_stat_ = v;
}

srs_error_t SrsHttpxConn::pop_message(ISrsHttpMessage **preq)
{
    srs_error_t err = srs_success;

    const int SRS_HTTP_READ_CACHE_BYTES = 4096;

    ISrsProtocolReadWriter *io = io_;
    if (ssl_) {
        io = ssl_;
    }

    // Check user interrupt by interval.
    io->set_recv_timeout(3 * SRS_UTIME_SECONDS);

    // We start a socket to read the stfd, which is writing by conn.
    // It's ok, because conn never read it after processing the HTTP request.
    // drop all request body.
    static char body[SRS_HTTP_READ_CACHE_BYTES];
    while (true) {
        if ((err = conn_->pull()) != srs_success) {
            return srs_error_wrap(err, "timeout");
        }

        if ((err = io->read(body, SRS_HTTP_READ_CACHE_BYTES, NULL)) != srs_success) {
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

srs_error_t SrsHttpxConn::on_start()
{
    srs_error_t err = srs_success;

    // Enable JSONP for HTTP API.
    if ((err = conn_->set_jsonp(true)) != srs_success) {
        return srs_error_wrap(err, "set jsonp");
    }

    // Do SSL handshake if HTTPS.
    if (ssl_) {
        srs_utime_t starttime = srs_time_now_realtime();
        if ((err = ssl_->handshake(ssl_key_file_, ssl_cert_file_)) != srs_success) {
            return srs_error_wrap(err, "handshake");
        }

        int cost = srsu2msi(srs_time_now_realtime() - starttime);
        srs_trace("https: stream server done, use key %s and cert %s, cost=%dms",
                  ssl_key_file_.c_str(), ssl_cert_file_.c_str(), cost);
    }

    return err;
}

srs_error_t SrsHttpxConn::on_http_message(ISrsHttpMessage *r, ISrsHttpResponseWriter *w)
{
    srs_error_t err = srs_success;

    // After parsed the message, set the schema to https.
    if (ssl_) {
        SrsHttpMessage *hm = dynamic_cast<SrsHttpMessage *>(r);
        hm->set_https(true);
    }

    // For each session, we use short-term HTTP connection.
    SrsHttpHeader *hdr = w->header();
    hdr->set("Connection", "Close");

    return err;
}

srs_error_t SrsHttpxConn::on_message_done(ISrsHttpMessage *r, ISrsHttpResponseWriter *w)
{
    return srs_success;
}

srs_error_t SrsHttpxConn::on_conn_done(srs_error_t r0)
{
    // Only stat the HTTP streaming clients, ignore all API clients.
    if (enable_stat_) {
        stat_->on_disconnect(get_id().c_str(), r0);
        stat_->kbps_add_delta(get_id().c_str(), conn_->delta());
    }

    // Because we use manager to manage this object,
    // not the http connection object, so we must remove it here.
    manager_->remove(this);

    // For HTTP-API timeout, we think it's done successfully,
    // because there may be no request or response for HTTP-API.
    if (srs_error_code(r0) == ERROR_SOCKET_TIMEOUT) {
        srs_freep(r0);
        return srs_success;
    }

    return r0;
}

std::string SrsHttpxConn::desc()
{
    if (ssl_) {
        return "HttpsConn";
    }
    return "HttpConn";
}

std::string SrsHttpxConn::remote_ip()
{
    return conn_->remote_ip();
}

const SrsContextId &SrsHttpxConn::get_id()
{
    return conn_->get_id();
}

srs_error_t SrsHttpxConn::start()
{
    srs_error_t err = srs_success;

    bool v = config_->get_http_stream_crossdomain();
    if ((err = conn_->set_crossdomain_enabled(v)) != srs_success) {
        return srs_error_wrap(err, "set cors=%d", v);
    }

    bool auth_enabled = config_->get_http_api_auth_enabled();
    if ((err = conn_->set_auth_enabled(auth_enabled)) != srs_success) {
        return srs_error_wrap(err, "set auth");
    }

    return conn_->start();
}

ISrsKbpsDelta *SrsHttpxConn::delta()
{
    return conn_->delta();
}

ISrsHttpServer::ISrsHttpServer()
{
}

ISrsHttpServer::~ISrsHttpServer()
{
}

SrsHttpServer::SrsHttpServer()
{
    http_stream_ = new SrsHttpStreamServer();
    http_stream_->assemble();

    http_static_ = new SrsHttpStaticServer();
}

SrsHttpServer::~SrsHttpServer()
{
    srs_freep(http_stream_);
    srs_freep(http_static_);
}

srs_error_t SrsHttpServer::initialize()
{
    srs_error_t err = srs_success;

    // for SRS go-sharp to detect the status of HTTP server of SRS HTTP FLV Cluster.
    if ((err = http_static_->mux()->handle("/api/v1/versions", new SrsGoApiVersion())) != srs_success) {
        return srs_error_wrap(err, "handle versions");
    }

    if ((err = http_stream_->initialize()) != srs_success) {
        return srs_error_wrap(err, "http stream");
    }

    if ((err = http_static_->initialize()) != srs_success) {
        return srs_error_wrap(err, "http static");
    }

    return err;
}

srs_error_t SrsHttpServer::handle(std::string pattern, ISrsHttpHandler *handler)
{
    return http_static_->mux()->handle(pattern, handler);
}

srs_error_t SrsHttpServer::serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r)
{
    srs_error_t err = srs_success;

    string path = r->path();
    const char *p = path.data();

    // For /api/ or /console/, try static only.
    if (path.length() > 4 && p[0] == '/') {
        bool is_api = memcmp(p, "/api/", 5) == 0;
        bool is_console = path.length() > 8 && memcmp(p, "/console/", 9) == 0;
        if (is_api || is_console) {
            return http_static_->mux()->serve_http(w, r);
        }
    }

    // Try http stream first, then http static if not found.
    ISrsHttpHandler *h = NULL;
    if ((err = http_stream_->mux()->find_handler(r, &h)) != srs_success) {
        return srs_error_wrap(err, "find handler");
    }
    if (!h->is_not_found()) {
        return http_stream_->mux()->serve_http(w, r);
    }

    // Use http static as default server.
    return http_static_->mux()->serve_http(w, r);
}

srs_error_t SrsHttpServer::http_mount(ISrsRequest *r)
{
    return http_stream_->http_mount(r);
}

void SrsHttpServer::http_unmount(ISrsRequest *r)
{
    http_stream_->http_unmount(r);
}
