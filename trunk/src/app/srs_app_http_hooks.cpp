//
// Copyright (c) 2013-2024 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_app_http_hooks.hpp>

#include <sstream>
using namespace std;

#include <srs_kernel_error.hpp>
#include <srs_protocol_rtmp_stack.hpp>
#include <srs_app_st.hpp>
#include <srs_protocol_json.hpp>
#include <srs_app_dvr.hpp>
#include <srs_app_http_client.hpp>
#include <srs_core_autofree.hpp>
#include <srs_app_config.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_app_http_conn.hpp>
#include <srs_protocol_amf0.hpp>
#include <srs_app_utility.hpp>
#include <srs_app_statistic.hpp>

// The HTTP response body should be "0", see https://github.com/ossrs/srs/issues/3215#issuecomment-1319991512
#define SRS_HTTP_RESPONSE_OK SRS_XSTR(0)

#define SRS_HTTP_HEADER_BUFFER 1024
#define SRS_HTTP_READ_BUFFER 4096
#define SRS_HTTP_BODY_BUFFER (32 * 1024)

// the timeout for hls notify, in srs_utime_t.
#define SRS_HLS_NOTIFY_TIMEOUT (10 * SRS_UTIME_SECONDS)

SrsHttpHooks::SrsHttpHooks()
{
}

SrsHttpHooks::~SrsHttpHooks()
{
}

srs_error_t SrsHttpHooks::on_connect(string url, SrsRequest* req)
{
    srs_error_t err = srs_success;
    
    SrsContextId cid = _srs_context->get_id();
    SrsStatistic* stat = SrsStatistic::instance();
    SrsUniquePtr<SrsJsonObject> obj(SrsJsonAny::object());

    obj->set("server_id", SrsJsonAny::str(stat->server_id().c_str()));
    obj->set("service_id", SrsJsonAny::str(stat->service_id().c_str()));
    obj->set("action", SrsJsonAny::str("on_connect"));
    obj->set("client_id", SrsJsonAny::str(cid.c_str()));
    obj->set("ip", SrsJsonAny::str(req->ip.c_str()));
    obj->set("vhost", SrsJsonAny::str(req->vhost.c_str()));
    obj->set("app", SrsJsonAny::str(req->app.c_str()));
    obj->set("stream", SrsJsonAny::str(req->stream.c_str()));
    obj->set("param", SrsJsonAny::str(req->param.c_str()));
    obj->set("tcUrl", SrsJsonAny::str(req->tcUrl.c_str()));
    obj->set("pageUrl", SrsJsonAny::str(req->pageUrl.c_str()));
    
    std::string data = obj->dumps();
    std::string res;
    int status_code;
    
    SrsHttpClient http;
    if ((err = do_post(&http, url, data, status_code, res)) != srs_success) {
        return srs_error_wrap(err, "http: on_connect failed, client_id=%s, url=%s, request=%s, response=%s, code=%d",
            cid.c_str(), url.c_str(), data.c_str(), res.c_str(), status_code);
    }
    
    srs_trace("http: on_connect ok, client_id=%s, url=%s, request=%s, response=%s",
              cid.c_str(), url.c_str(), data.c_str(), res.c_str());
    
    return err;
}

void SrsHttpHooks::on_close(string url, SrsRequest* req, int64_t send_bytes, int64_t recv_bytes)
{
    srs_error_t err = srs_success;
    
    SrsContextId cid = _srs_context->get_id();
    SrsStatistic* stat = SrsStatistic::instance();
    SrsUniquePtr<SrsJsonObject> obj(SrsJsonAny::object());
    
    obj->set("server_id", SrsJsonAny::str(stat->server_id().c_str()));
    obj->set("service_id", SrsJsonAny::str(stat->service_id().c_str()));
    obj->set("action", SrsJsonAny::str("on_close"));
    obj->set("client_id", SrsJsonAny::str(cid.c_str()));
    obj->set("ip", SrsJsonAny::str(req->ip.c_str()));
    obj->set("vhost", SrsJsonAny::str(req->vhost.c_str()));
    obj->set("app", SrsJsonAny::str(req->app.c_str()));
    obj->set("send_bytes", SrsJsonAny::integer(send_bytes));
    obj->set("recv_bytes", SrsJsonAny::integer(recv_bytes));
    
    std::string data = obj->dumps();
    std::string res;
    int status_code;
    
    SrsHttpClient http;
    if ((err = do_post(&http, url, data, status_code, res)) != srs_success) {
        int ret = srs_error_code(err);
        srs_freep(err);
        srs_warn("http: ignore on_close failed, client_id=%s, url=%s, request=%s, response=%s, code=%d, ret=%d",
            cid.c_str(), url.c_str(), data.c_str(), res.c_str(), status_code, ret);
        return;
    }
    
    srs_trace("http: on_close ok, client_id=%s, url=%s, request=%s, response=%s",
        cid.c_str(), url.c_str(), data.c_str(), res.c_str());
    
    return;
}

srs_error_t SrsHttpHooks::on_publish(string url, SrsRequest* req)
{
    srs_error_t err = srs_success;
    
    SrsContextId cid = _srs_context->get_id();
    SrsStatistic* stat = SrsStatistic::instance();
    SrsUniquePtr<SrsJsonObject> obj(SrsJsonAny::object());

    obj->set("server_id", SrsJsonAny::str(stat->server_id().c_str()));
    obj->set("service_id", SrsJsonAny::str(stat->service_id().c_str()));
    obj->set("action", SrsJsonAny::str("on_publish"));
    obj->set("client_id", SrsJsonAny::str(cid.c_str()));
    obj->set("ip", SrsJsonAny::str(req->ip.c_str()));
    obj->set("vhost", SrsJsonAny::str(req->vhost.c_str()));
    obj->set("app", SrsJsonAny::str(req->app.c_str()));
    obj->set("tcUrl", SrsJsonAny::str(req->tcUrl.c_str()));
    obj->set("stream", SrsJsonAny::str(req->stream.c_str()));
    obj->set("param", SrsJsonAny::str(req->param.c_str()));

    obj->set("stream_url", SrsJsonAny::str(req->get_stream_url().c_str()));
    SrsStatisticStream* stream = stat->find_stream_by_url(req->get_stream_url());
    if (stream) {
        obj->set("stream_id", SrsJsonAny::str(stream->id.c_str()));
    }
    
    std::string data = obj->dumps();
    std::string res;
    int status_code;
    
    SrsHttpClient http;
    if ((err = do_post(&http, url, data, status_code, res)) != srs_success) {
        return srs_error_wrap(err, "http: on_publish failed, client_id=%s, url=%s, request=%s, response=%s, code=%d",
            cid.c_str(), url.c_str(), data.c_str(), res.c_str(), status_code);
    }
    
    srs_trace("http: on_publish ok, client_id=%s, url=%s, request=%s, response=%s",
        cid.c_str(), url.c_str(), data.c_str(), res.c_str());
    
    return err;
}

void SrsHttpHooks::on_unpublish(string url, SrsRequest* req)
{
    srs_error_t err = srs_success;
    
    SrsContextId cid = _srs_context->get_id();
    SrsStatistic* stat = SrsStatistic::instance();
    SrsUniquePtr<SrsJsonObject> obj(SrsJsonAny::object());
    
    obj->set("server_id", SrsJsonAny::str(stat->server_id().c_str()));
    obj->set("service_id", SrsJsonAny::str(stat->service_id().c_str()));
    obj->set("action", SrsJsonAny::str("on_unpublish"));
    obj->set("client_id", SrsJsonAny::str(cid.c_str()));
    obj->set("ip", SrsJsonAny::str(req->ip.c_str()));
    obj->set("vhost", SrsJsonAny::str(req->vhost.c_str()));
    obj->set("app", SrsJsonAny::str(req->app.c_str()));
    obj->set("tcUrl", SrsJsonAny::str(req->tcUrl.c_str()));
    obj->set("stream", SrsJsonAny::str(req->stream.c_str()));
    obj->set("param", SrsJsonAny::str(req->param.c_str()));

    obj->set("stream_url", SrsJsonAny::str(req->get_stream_url().c_str()));
    SrsStatisticStream* stream = stat->find_stream_by_url(req->get_stream_url());
    if (stream) {
        obj->set("stream_id", SrsJsonAny::str(stream->id.c_str()));
    }
    
    std::string data = obj->dumps();
    std::string res;
    int status_code;
    
    SrsHttpClient http;
    if ((err = do_post(&http, url, data, status_code, res)) != srs_success) {
        int ret = srs_error_code(err);
        srs_freep(err);
        srs_warn("http: ignore on_unpublish failed, client_id=%s, url=%s, request=%s, response=%s, status=%d, ret=%d",
            cid.c_str(), url.c_str(), data.c_str(), res.c_str(), status_code, ret);
        return;
    }
    
    srs_trace("http: on_unpublish ok, client_id=%s, url=%s, request=%s, response=%s",
        cid.c_str(), url.c_str(), data.c_str(), res.c_str());
    
    return;
}

srs_error_t SrsHttpHooks::on_play(string url, SrsRequest* req)
{
    srs_error_t err = srs_success;
    
    SrsContextId cid = _srs_context->get_id();
    SrsStatistic* stat = SrsStatistic::instance();
    SrsUniquePtr<SrsJsonObject> obj(SrsJsonAny::object());
    
    obj->set("server_id", SrsJsonAny::str(stat->server_id().c_str()));
    obj->set("service_id", SrsJsonAny::str(stat->service_id().c_str()));
    obj->set("action", SrsJsonAny::str("on_play"));
    obj->set("client_id", SrsJsonAny::str(cid.c_str()));
    obj->set("ip", SrsJsonAny::str(req->ip.c_str()));
    obj->set("vhost", SrsJsonAny::str(req->vhost.c_str()));
    obj->set("app", SrsJsonAny::str(req->app.c_str()));
    obj->set("stream", SrsJsonAny::str(req->stream.c_str()));
    obj->set("tcUrl", SrsJsonAny::str(req->tcUrl.c_str()));
    obj->set("param", SrsJsonAny::str(req->param.c_str()));
    obj->set("pageUrl", SrsJsonAny::str(req->pageUrl.c_str()));

    obj->set("stream_url", SrsJsonAny::str(req->get_stream_url().c_str()));
    SrsStatisticStream* stream = stat->find_stream_by_url(req->get_stream_url());
    if (stream) {
        obj->set("stream_id", SrsJsonAny::str(stream->id.c_str()));
    }
    
    std::string data = obj->dumps();
    std::string res;
    int status_code;
    
    SrsHttpClient http;
    if ((err = do_post(&http, url, data, status_code, res)) != srs_success) {
        return srs_error_wrap(err, "http: on_play failed, client_id=%s, url=%s, request=%s, response=%s, status=%d",
            cid.c_str(), url.c_str(), data.c_str(), res.c_str(), status_code);
    }
    
    srs_trace("http: on_play ok, client_id=%s, url=%s, request=%s, response=%s",
        cid.c_str(), url.c_str(), data.c_str(), res.c_str());
    
    return err;
}

void SrsHttpHooks::on_stop(string url, SrsRequest* req)
{
    srs_error_t err = srs_success;
    
    SrsContextId cid = _srs_context->get_id();
    SrsStatistic* stat = SrsStatistic::instance();
    SrsUniquePtr<SrsJsonObject> obj(SrsJsonAny::object());
    
    obj->set("server_id", SrsJsonAny::str(stat->server_id().c_str()));
    obj->set("service_id", SrsJsonAny::str(stat->service_id().c_str()));
    obj->set("action", SrsJsonAny::str("on_stop"));
    obj->set("client_id", SrsJsonAny::str(cid.c_str()));
    obj->set("ip", SrsJsonAny::str(req->ip.c_str()));
    obj->set("vhost", SrsJsonAny::str(req->vhost.c_str()));
    obj->set("app", SrsJsonAny::str(req->app.c_str()));
    obj->set("tcUrl", SrsJsonAny::str(req->tcUrl.c_str()));
    obj->set("stream", SrsJsonAny::str(req->stream.c_str()));
    obj->set("param", SrsJsonAny::str(req->param.c_str()));

    obj->set("stream_url", SrsJsonAny::str(req->get_stream_url().c_str()));
    SrsStatisticStream* stream = stat->find_stream_by_url(req->get_stream_url());
    if (stream) {
        obj->set("stream_id", SrsJsonAny::str(stream->id.c_str()));
    }
    
    std::string data = obj->dumps();
    std::string res;
    int status_code;
    
    SrsHttpClient http;
    if ((err = do_post(&http, url, data, status_code, res)) != srs_success) {
        int ret = srs_error_code(err);
        srs_freep(err);
        srs_warn("http: ignore on_stop failed, client_id=%s, url=%s, request=%s, response=%s, code=%d, ret=%d",
            cid.c_str(), url.c_str(), data.c_str(), res.c_str(), status_code, ret);
        return;
    }
    
    srs_trace("http: on_stop ok, client_id=%s, url=%s, request=%s, response=%s",
        cid.c_str(), url.c_str(), data.c_str(), res.c_str());
    
    return;
}

srs_error_t SrsHttpHooks::on_dvr(SrsContextId c, string url, SrsRequest* req, string file)
{
    srs_error_t err = srs_success;
    
    SrsContextId cid = c;
    std::string cwd = _srs_config->cwd();
    
    SrsStatistic* stat = SrsStatistic::instance();
    SrsUniquePtr<SrsJsonObject> obj(SrsJsonAny::object());
    
    obj->set("server_id", SrsJsonAny::str(stat->server_id().c_str()));
    obj->set("service_id", SrsJsonAny::str(stat->service_id().c_str()));
    obj->set("action", SrsJsonAny::str("on_dvr"));
    obj->set("client_id", SrsJsonAny::str(cid.c_str()));
    obj->set("ip", SrsJsonAny::str(req->ip.c_str()));
    obj->set("vhost", SrsJsonAny::str(req->vhost.c_str()));
    obj->set("app", SrsJsonAny::str(req->app.c_str()));
    obj->set("tcUrl", SrsJsonAny::str(req->tcUrl.c_str()));
    obj->set("stream", SrsJsonAny::str(req->stream.c_str()));
    obj->set("param", SrsJsonAny::str(req->param.c_str()));
    obj->set("cwd", SrsJsonAny::str(cwd.c_str()));
    obj->set("file", SrsJsonAny::str(file.c_str()));

    obj->set("stream_url", SrsJsonAny::str(req->get_stream_url().c_str()));
    SrsStatisticStream* stream = stat->find_stream_by_url(req->get_stream_url());
    if (stream) {
        obj->set("stream_id", SrsJsonAny::str(stream->id.c_str()));
    }
    
    std::string data = obj->dumps();
    std::string res;
    int status_code;
    
    SrsHttpClient http;
    if ((err = do_post(&http, url, data, status_code, res)) != srs_success) {
        return srs_error_wrap(err, "http post on_dvr uri failed, client_id=%s, url=%s, request=%s, response=%s, code=%d",
            cid.c_str(), url.c_str(), data.c_str(), res.c_str(), status_code);
    }
    
    srs_trace("http hook on_dvr success. client_id=%s, url=%s, request=%s, response=%s",
        cid.c_str(), url.c_str(), data.c_str(), res.c_str());
    
    return err;
}

srs_error_t SrsHttpHooks::on_hls(SrsContextId c, string url, SrsRequest* req, string file, string ts_url, string m3u8, string m3u8_url, int sn, srs_utime_t duration)
{
    srs_error_t err = srs_success;
    
    SrsContextId cid = c;
    std::string cwd = _srs_config->cwd();
    
    // the ts_url is under the same dir of m3u8_url.
    string prefix = srs_path_dirname(m3u8_url);
    if (!prefix.empty() && !srs_string_is_http(ts_url)) {
        ts_url = prefix + "/" + ts_url;
    }
    
    SrsStatistic* stat = SrsStatistic::instance();
    SrsUniquePtr<SrsJsonObject> obj(SrsJsonAny::object());
    
    obj->set("server_id", SrsJsonAny::str(stat->server_id().c_str()));
    obj->set("service_id", SrsJsonAny::str(stat->service_id().c_str()));
    obj->set("action", SrsJsonAny::str("on_hls"));
    obj->set("client_id", SrsJsonAny::str(cid.c_str()));
    obj->set("ip", SrsJsonAny::str(req->ip.c_str()));
    obj->set("vhost", SrsJsonAny::str(req->vhost.c_str()));
    obj->set("app", SrsJsonAny::str(req->app.c_str()));
    obj->set("tcUrl", SrsJsonAny::str(req->tcUrl.c_str()));
    obj->set("stream", SrsJsonAny::str(req->stream.c_str()));
    obj->set("param", SrsJsonAny::str(req->param.c_str()));
    obj->set("duration", SrsJsonAny::number(srsu2ms(duration)/1000.0));
    obj->set("cwd", SrsJsonAny::str(cwd.c_str()));
    obj->set("file", SrsJsonAny::str(file.c_str()));
    obj->set("url", SrsJsonAny::str(ts_url.c_str()));
    obj->set("m3u8", SrsJsonAny::str(m3u8.c_str()));
    obj->set("m3u8_url", SrsJsonAny::str(m3u8_url.c_str()));
    obj->set("seq_no", SrsJsonAny::integer(sn));

    obj->set("stream_url", SrsJsonAny::str(req->get_stream_url().c_str()));
    SrsStatisticStream* stream = stat->find_stream_by_url(req->get_stream_url());
    if (stream) {
        obj->set("stream_id", SrsJsonAny::str(stream->id.c_str()));
    }
    
    std::string data = obj->dumps();
    std::string res;
    int status_code;
    
    SrsHttpClient http;
    if ((err = do_post(&http, url, data, status_code, res)) != srs_success) {
        return srs_error_wrap(err, "http: post %s with %s, status=%d, res=%s", url.c_str(), data.c_str(), status_code, res.c_str());
    }
    
    srs_trace("http: on_hls ok, client_id=%s, url=%s, request=%s, response=%s",
        cid.c_str(), url.c_str(), data.c_str(), res.c_str());
    
    return err;
}

srs_error_t SrsHttpHooks::on_hls_notify(SrsContextId c, std::string url, SrsRequest* req, std::string ts_url, int nb_notify)
{
    srs_error_t err = srs_success;
    
    SrsContextId cid = c;
    std::string cwd = _srs_config->cwd();
    
    if (srs_string_is_http(ts_url)) {
        url = ts_url;
    }
    
    SrsStatistic* stat = SrsStatistic::instance();

    url = srs_string_replace(url, "[server_id]", stat->server_id().c_str());
    url = srs_string_replace(url, "[service_id]", stat->service_id().c_str());
    url = srs_string_replace(url, "[app]", req->app);
    url = srs_string_replace(url, "[stream]", req->stream);
    url = srs_string_replace(url, "[ts_url]", ts_url);
    url = srs_string_replace(url, "[param]", req->param);
    
    int64_t starttime = srsu2ms(srs_update_system_time());
    
    SrsHttpUri uri;
    if ((err = uri.initialize(url)) != srs_success) {
        return srs_error_wrap(err, "http: init url=%s", url.c_str());
    }
    
    SrsHttpClient http;
    if ((err = http.initialize(uri.get_schema(), uri.get_host(), uri.get_port(), SRS_HLS_NOTIFY_TIMEOUT)) != srs_success) {
        return srs_error_wrap(err, "http: init client for %s", url.c_str());
    }
    
    std::string path = uri.get_query();
    if (path.empty()) {
        path = uri.get_path();
    } else {
        path = uri.get_path();
        path += "?";
        path += uri.get_query();
    }
    srs_info("GET %s", path.c_str());
    
    ISrsHttpMessage* msg_raw = NULL;
    if ((err = http.get(path.c_str(), "", &msg_raw)) != srs_success) {
        return srs_error_wrap(err, "http: get %s", url.c_str());
    }
    SrsUniquePtr<ISrsHttpMessage> msg(msg_raw);

    int nb_buf = srs_min(nb_notify, SRS_HTTP_READ_BUFFER);
    SrsUniquePtr<char[]> buf(new char[nb_buf]);

    int nb_read = 0;
    ISrsHttpResponseReader* br = msg->body_reader();
    while (nb_read < nb_notify && !br->eof()) {
        ssize_t nb_bytes = 0;
        if ((err = br->read(buf.get(), nb_buf, &nb_bytes)) != srs_success) {
            break;
        }
        nb_read += (int)nb_bytes;
    }
    
    int spenttime = (int)(srsu2ms(srs_update_system_time()) - starttime);
    srs_trace("http hook on_hls_notify success. client_id=%s, url=%s, code=%d, spent=%dms, read=%dB, err=%s",
        cid.c_str(), url.c_str(), msg->status_code(), spenttime, nb_read, srs_error_desc(err).c_str());
    
    // ignore any error for on_hls_notify.
    srs_error_reset(err);
    return srs_success;
}

srs_error_t SrsHttpHooks::discover_co_workers(string url, string& host, int& port)
{
    srs_error_t err = srs_success;
    
    std::string res;
    int status_code;
    
    SrsHttpClient http;
    if ((err = do_post(&http, url, "", status_code, res)) != srs_success) {
        return srs_error_wrap(err, "http: post %s, status=%d, res=%s", url.c_str(), status_code, res.c_str());
    }
    
    SrsJsonObject* robj_raw = NULL;
    if (true) {
        SrsJsonAny* jr = NULL;
        if ((jr = SrsJsonAny::loads(res)) == NULL) {
            return srs_error_new(ERROR_OCLUSTER_DISCOVER, "load json from %s", res.c_str());
        }
        
        if (!jr->is_object()) {
            srs_freep(jr);
            return srs_error_new(ERROR_OCLUSTER_DISCOVER, "response %s", res.c_str());
        }

        robj_raw = jr->to_object();
    }
    SrsUniquePtr<SrsJsonObject> robj(robj_raw);
    
    SrsJsonAny* prop = NULL;
    if ((prop = robj->ensure_property_object("data")) == NULL) {
        return srs_error_new(ERROR_OCLUSTER_DISCOVER, "parse data %s", res.c_str());
    }
    
    SrsJsonObject* p = prop->to_object();
    if ((prop = p->ensure_property_object("origin")) == NULL) {
        return srs_error_new(ERROR_OCLUSTER_DISCOVER, "parse data %s", res.c_str());
    }
    p = prop->to_object();
    
    if ((prop = p->ensure_property_string("ip")) == NULL) {
        return srs_error_new(ERROR_OCLUSTER_DISCOVER, "parse data %s", res.c_str());
    }
    host = prop->to_str();
    
    if ((prop = p->ensure_property_integer("port")) == NULL) {
        return srs_error_new(ERROR_OCLUSTER_DISCOVER, "parse data %s", res.c_str());
    }
    port = (int)prop->to_integer();
    
    srs_trace("http: cluster redirect %s:%d ok, url=%s, response=%s", host.c_str(), port, url.c_str(), res.c_str());
    
    return err;
}

srs_error_t SrsHttpHooks::on_forward_backend(string url, SrsRequest* req, std::vector<std::string>& rtmp_urls)
{
    srs_error_t err = srs_success;

    SrsContextId cid = _srs_context->get_id();

    SrsStatistic* stat = SrsStatistic::instance();
    SrsUniquePtr<SrsJsonObject> obj(SrsJsonAny::object());

    obj->set("action", SrsJsonAny::str("on_forward"));
    obj->set("server_id", SrsJsonAny::str(stat->server_id().c_str()));
    obj->set("service_id", SrsJsonAny::str(stat->service_id().c_str()));
    obj->set("client_id", SrsJsonAny::str(cid.c_str()));
    obj->set("ip", SrsJsonAny::str(req->ip.c_str()));
    obj->set("vhost", SrsJsonAny::str(req->vhost.c_str()));
    obj->set("app", SrsJsonAny::str(req->app.c_str()));
    obj->set("tcUrl", SrsJsonAny::str(req->tcUrl.c_str()));
    obj->set("stream", SrsJsonAny::str(req->stream.c_str()));
    obj->set("param", SrsJsonAny::str(req->param.c_str()));

    std::string data = obj->dumps();
    std::string res;
    int status_code;

    SrsHttpClient http;
    if ((err = do_post(&http, url, data, status_code, res)) != srs_success) {
        return srs_error_wrap(err, "http: on_forward_backend failed, client_id=%s, url=%s, request=%s, response=%s, code=%d",
            cid.c_str(), url.c_str(), data.c_str(), res.c_str(), status_code);
    }

    // parse string res to json.
    SrsUniquePtr<SrsJsonAny> info(SrsJsonAny::loads(res));
    if (!info.get()) {
        return srs_error_new(ERROR_SYSTEM_FORWARD_LOOP, "load json from %s", res.c_str());
    }

    // response error code in string.
    if (!info->is_object()) {
        return srs_error_new(ERROR_SYSTEM_FORWARD_LOOP, "response %s", res.c_str());
    }

    SrsJsonAny* prop = NULL;
    // response standard object, format in json: {}
    SrsJsonObject* res_info = info->to_object();
    if ((prop = res_info->ensure_property_object("data")) == NULL) {
        return srs_error_new(ERROR_SYSTEM_FORWARD_LOOP, "parse data %s", res.c_str());
    }

    SrsJsonObject* p = prop->to_object();
    if ((prop = p->ensure_property_array("urls")) == NULL) {
        return srs_error_new(ERROR_SYSTEM_FORWARD_LOOP, "parse urls %s", res.c_str());
    }

    SrsJsonArray* urls = prop->to_array();
    for (int i = 0; i < urls->count(); i++) {
        prop = urls->at(i);
        string rtmp_url = prop->to_str();
        if (!rtmp_url.empty()) {
            rtmp_urls.push_back(rtmp_url);
        }
    }

    srs_trace("http: on_forward_backend ok, client_id=%s, url=%s, request=%s, response=%s",
        cid.c_str(), url.c_str(), data.c_str(), res.c_str());

    return err;
}

srs_error_t SrsHttpHooks::do_post(SrsHttpClient* hc, std::string url, std::string req, int& code, string& res)
{
    srs_error_t err = srs_success;
    
    SrsHttpUri uri;
    if ((err = uri.initialize(url)) != srs_success) {
        return srs_error_wrap(err, "http: post failed. url=%s", url.c_str());
    }
    
    if ((err = hc->initialize(uri.get_schema(), uri.get_host(), uri.get_port())) != srs_success) {
        return srs_error_wrap(err, "http: init client");
    }
    
    string path = uri.get_path();
    if (!uri.get_query().empty()) {
        path += "?" + uri.get_query();
    }
    
    ISrsHttpMessage* msg_raw = NULL;
    if ((err = hc->post(path, req, &msg_raw)) != srs_success) {
        return srs_error_wrap(err, "http: client post");
    }
    SrsUniquePtr<ISrsHttpMessage> msg(msg_raw);

    code = msg->status_code();
    if ((err = msg->body_read_all(res)) != srs_success) {
        return srs_error_wrap(err, "http: body read");
    }
    
    // ensure the http status is ok.
    if (code != SRS_CONSTS_HTTP_OK && code != SRS_CONSTS_HTTP_Created) {
        return srs_error_new(ERROR_HTTP_STATUS_INVALID, "http: status %d", code);
    }
    
    // should never be empty.
    if (res.empty()) {
        return srs_error_new(ERROR_HTTP_DATA_INVALID, "http: empty response");
    }
    
    // parse string res to json.
    SrsUniquePtr<SrsJsonAny> info(SrsJsonAny::loads(res));
    if (!info.get()) {
        return srs_error_new(ERROR_HTTP_DATA_INVALID, "http: not json %s", res.c_str());
    }

    // response error code in string.
    if (!info->is_object()) {
        if (res == SRS_HTTP_RESPONSE_OK) {
            return err;
        }
        return srs_error_new(ERROR_HTTP_DATA_INVALID, "http: response number code %s", res.c_str());
    }
    
    // response standard object, format in json: {"code": 0, "data": ""}
    SrsJsonObject* res_info = info->to_object();
    SrsJsonAny* res_code = NULL;
    if ((res_code = res_info->ensure_property_integer("code")) == NULL) {
        return srs_error_new(ERROR_RESPONSE_CODE, "http: response object no code %s", res.c_str());
    }
    
    if ((res_code->to_integer()) != ERROR_SUCCESS) {
        return srs_error_new(ERROR_RESPONSE_CODE, "http: response object code %" PRId64 " %s", res_code->to_integer(), res.c_str());
    }
    
    return err;
}
