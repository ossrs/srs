//
// Copyright (c) 2013-2026 The SRS Authors
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
#include <srs_app_factory.hpp>
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

void srs_http_stream_serve_error(ISrsHttpResponseWriter *w, srs_error_t err)
{
    // Once the response is on the wire, writing a status would corrupt the stream.
    if (err == srs_success || w->header_wrote()) {
        return;
    }

    // Add the status of a new error here rather than where it is raised, so that every status of
    // HTTP streaming is decided in one place.
    int code = srs_error_code(err);

    int status = 0;
    if (code == ERROR_SYSTEM_AUTH) {
        // Refused by the on_play HTTP callback.
        status = SRS_CONSTS_HTTP_Unauthorized;
    } else if (code == ERROR_SYSTEM_SECURITY_DENY || code == ERROR_SYSTEM_SECURITY_ALLOW || code == ERROR_SYSTEM_SECURITY) {
        // Refused by the security rules of the vhost, as RTSP answers its DESCRIBE.
        status = SRS_CONSTS_HTTP_Forbidden;
    }

    // SRS has always closed the connection on an error, so it still does for an error with no status
    // yet, rather than answering every failure with a misleading generic status.
    if (!status) {
        return;
    }

    // Never srs_error_desc, which may carry the callback URL or the response of the callback backend.
    string msg = srs_fmt_sprintf("%d: %s", code, srs_error_code_str(err).c_str());

    // The caller returns the error that explains the refusal, which matters more to the operator than
    // a viewer that already went away.
    srs_error_t r0 = srs_go_http_error(w, status, msg);
    srs_freep(r0);
}

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
    create_time_ = 0;
    delta_ = new SrsNetworkDelta();
    trd_ = NULL;

    config_ = _srs_config;
    app_factory_ = _srs_app_factory;
    context_ = _srs_context;
}

void SrsHttpConn::assemble()
{
    create_time_ = srs_time_now_cached();
    delta_->set_io(skt_, skt_);
    trd_ = app_factory_->create_coroutine("http", this, context_->get_id());
}

SrsHttpConn::~SrsHttpConn()
{
    if (trd_) {
        trd_->interrupt();
    }
    srs_freep(trd_);

    srs_freep(parser_);
    srs_freep(cors_);
    srs_freep(auth_);

    srs_freep(delta_);

    config_ = NULL;
    app_factory_ = NULL;
    context_ = NULL;
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

    err = handler_->on_conn_done(err);

    if (err == srs_success) {
        srs_trace("client finished.");
        return err;
    }

    if (srs_error_code(err) == ERROR_SUCCESS) {
        srs_trace("client finished%s.", srs_error_summary(err).c_str());
        srs_freep(err);
        return err;
    }

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

    // Some clients never close the connection.
    // @see https://github.com/ossrs/srs/issues/398
    skt_->set_recv_timeout(SRS_HTTP_RECV_TIMEOUT);

    if ((err = parser_->initialize(HTTP_REQUEST)) != srs_success) {
        return srs_error_wrap(err, "init parser for %s", ip_.c_str());
    }

    if ((err = handler_->on_start()) != srs_success) {
        return srs_error_wrap(err, "start");
    }

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

        ISrsHttpMessage *req_raw = NULL;
        if ((err = parser_->parse_message(skt_, &req_raw)) != srs_success) {
            // Never send the refused callback, which must not be echoed back as script.
            if (srs_error_code(err) == ERROR_HTTP_JSONP) {
                SrsUniquePtr<ISrsHttpResponseWriter> writer(app_factory_->create_http_response_writer(skt_));
                writer->header()->set("Connection", "Close");

                string msg = srs_fmt_sprintf("%d: %s", srs_error_code(err), srs_error_code_str(err).c_str());
                srs_error_t r0 = srs_go_http_error(writer.get(), SRS_CONSTS_HTTP_BadRequest, msg);
                srs_freep(r0);
            }

            return srs_error_wrap(err, "parse message");
        }

        srs_assert(req_raw);
        SrsUniquePtr<ISrsHttpMessage> req(req_raw);

        ISrsHttpMessageOwnerSetter *hreq = dynamic_cast<ISrsHttpMessageOwnerSetter *>(req.get());
        srs_assert(hreq);

        hreq->set_connection(this);

        srs_freep(*preq);
        *preq = hreq->to_request(hreq->host());

        SrsUniquePtr<ISrsHttpResponseWriter> writer(app_factory_->create_http_response_writer(skt_));
        if ((err = handler_->on_http_message(req.get(), writer.get())) != srs_success) {
            return srs_error_wrap(err, "on http message");
        }

        if ((err = process_request(writer.get(), req.get(), req_id)) != srs_success) {
            // The mux refuses a URL with ".." before any handler runs, so no handler sent a status.
            if (srs_error_code(err) == ERROR_HTTP_URL_NOT_CLEAN && !writer->header_wrote()) {
                writer->header()->set("Connection", "Close");

                string msg = srs_fmt_sprintf("%d: %s", srs_error_code(err), srs_error_code_str(err).c_str());
                srs_error_t r0 = srs_go_http_error(writer.get(), SRS_CONSTS_HTTP_BadRequest, msg);
                srs_freep(r0);
            }

            return srs_error_wrap(err, "process request=%d", req_id);
        }

        if ((err = handler_->on_message_done(req.get(), writer.get())) != srs_success) {
            return srs_error_wrap(err, "on message done");
        }

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

    // The CORS mux passes the request to the auth mux, which passes it to the HTTP mux.
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

    ISrsHttpAuthenticator *authenticator = NULL;
    if (auth_enabled) {
        string type = config_->get_http_api_auth_type();
        if (type == "basic") {
            authenticator = new SrsHttpBasicAuthenticator(
                config_->get_http_api_auth_username(), config_->get_http_api_auth_password());
        } else if (type == "bearer") {
            authenticator = new SrsHttpBearerAuthenticator(
                config_->get_http_api_auth_token(), config_->get_http_api_auth_rtc_bearer_enabled());
        } else {
            return srs_error_new(ERROR_SYSTEM_CONFIG_INVALID, "invalid http api auth type=%s", type.c_str());
        }
    }

    // The auth mux owns the authenticator.
    if ((err = auth_->initialize(authenticator)) != srs_success) {
        srs_freep(authenticator);
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

SrsHttpxConn::SrsHttpxConn(ISrsResourceManager *cm, ISrsProtocolReadWriter *io, ISrsCommonHttpHandler *m, string cip, int port, string key, string cert) : manager_(cm), io_(io), enable_stat_(false), is_api_listener_(false), ssl_key_file_(key), ssl_cert_file_(cert)
{
    ssl_ = NULL;
    conn_ = NULL;

    http_mux_ = m;
    ip_ = cip;
    port_ = port;

    config_ = _srs_config;
    stat_ = _srs_stat;
    context_ = _srs_context;
}

void SrsHttpxConn::assemble()
{
    context_->set_id(context_->generate_id());

    if (!ssl_key_file_.empty() &&
        !ssl_cert_file_.empty()) {
        ssl_ = new SrsSslConnection(io_);
        SrsHttpConn *conn = new SrsHttpConn(this, ssl_, http_mux_, ip_, port_);
        conn->assemble();
        conn_ = conn;
    } else {
        SrsHttpConn *conn = new SrsHttpConn(this, io_, http_mux_, ip_, port_);
        conn->assemble();
        conn_ = conn;
    }
}

SrsHttpxConn::~SrsHttpxConn()
{
    srs_freep(conn_);
    srs_freep(ssl_);
    srs_freep(io_);

    config_ = NULL;
    stat_ = NULL;
    context_ = NULL;
}

void SrsHttpxConn::set_enable_stat(bool v)
{
    enable_stat_ = v;
}

void SrsHttpxConn::set_api_listener(bool v)
{
    is_api_listener_ = v;
}

srs_error_t SrsHttpxConn::pop_message(ISrsHttpMessage **preq)
{
    srs_error_t err = srs_success;

    const int SRS_HTTP_READ_CACHE_BYTES = 4096;

    ISrsProtocolReadWriter *io = io_;
    if (ssl_) {
        io = ssl_;
    }

    // The read times out now and then, so the loop can check whether the connection coroutine stopped.
    io->set_recv_timeout(3 * SRS_UTIME_SECONDS);

    // The connection coroutine writes to the same fd, which is safe because it never reads the fd
    // after it processes the HTTP request.
    static char body[SRS_HTTP_READ_CACHE_BYTES];
    while (true) {
        if ((err = conn_->pull()) != srs_success) {
            return srs_error_wrap(err, "timeout");
        }

        if ((err = io->read(body, SRS_HTTP_READ_CACHE_BYTES, NULL)) != srs_success) {
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

    if ((err = conn_->set_jsonp(true)) != srs_success) {
        return srs_error_wrap(err, "set jsonp");
    }

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

    if (ssl_) {
        SrsHttpMessage *hm = dynamic_cast<SrsHttpMessage *>(r);
        hm->set_https(true);
    }

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
    if (enable_stat_) {
        stat_->on_disconnect(get_id().c_str(), r0);
        stat_->kbps_add_delta(get_id().c_str(), conn_->delta());
    }

    // The manager holds this object, not the HTTP connection, so it is removed here.
    manager_->remove(this);

    // An HTTP API connection may carry no request or response, so a timeout is a normal end.
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

    // The HTTP API's listeners answer CORS by the API's crossdomain, every other HTTP listener by the
    // HTTP server's, which also covers the API when it shares the HTTP server's port.
    bool v = is_api_listener_ ? config_->get_http_api_crossdomain() : config_->get_http_stream_crossdomain();
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
    http_static_ = new SrsHttpStaticServer();
}

void SrsHttpServer::assemble()
{
    http_stream_->assemble();
}

SrsHttpServer::~SrsHttpServer()
{
    srs_freep(http_stream_);
    srs_freep(http_static_);
}

srs_error_t SrsHttpServer::initialize()
{
    srs_error_t err = srs_success;

    // SRS go-sharp reads it to detect the status of the HTTP server in an SRS HTTP-FLV cluster.
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

    if (path.length() > 4 && p[0] == '/') {
        bool is_api = memcmp(p, "/api/", 5) == 0;
        bool is_console = path.length() > 8 && memcmp(p, "/console/", 9) == 0;
        if (is_api || is_console) {
            return http_static_->mux()->serve_http(w, r);
        }
    }

    ISrsHttpHandler *h = NULL;
    if ((err = http_stream_->mux()->find_handler(r, &h)) != srs_success) {
        return srs_error_wrap(err, "find handler");
    }
    if (!h->is_not_found()) {
        return http_stream_->mux()->serve_http(w, r);
    }

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
