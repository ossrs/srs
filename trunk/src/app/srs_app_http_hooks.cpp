/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2018 Winlin
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

#include <srs_app_http_hooks.hpp>

#include <sstream>
using namespace std;

#include <srs_kernel_error.hpp>
#include <srs_rtmp_stack.hpp>
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

#define SRS_HTTP_RESPONSE_OK    SRS_XSTR(ERROR_SUCCESS)

#define SRS_HTTP_HEADER_BUFFER 1024
#define SRS_HTTP_READ_BUFFER 4096
#define SRS_HTTP_BODY_BUFFER (32 * 1024)

// the timeout for hls notify, in ms.
#define SRS_HLS_NOTIFY_TMMS (10 * 1000)

SrsHttpHooks::SrsHttpHooks()
{
}

SrsHttpHooks::~SrsHttpHooks()
{
}

srs_error_t SrsHttpHooks::on_connect(string url, SrsRequest* req)
{
    srs_error_t err = srs_success;
    
    int client_id = _srs_context->get_id();
    
    SrsJsonObject* obj = SrsJsonAny::object();
    SrsAutoFree(SrsJsonObject, obj);
    
    obj->set("action", SrsJsonAny::str("on_connect"));
    obj->set("client_id", SrsJsonAny::integer(client_id));
    obj->set("ip", SrsJsonAny::str(req->ip.c_str()));
    obj->set("vhost", SrsJsonAny::str(req->vhost.c_str()));
    obj->set("app", SrsJsonAny::str(req->app.c_str()));
    obj->set("tcUrl", SrsJsonAny::str(req->tcUrl.c_str()));
    obj->set("pageUrl", SrsJsonAny::str(req->pageUrl.c_str()));
    
    std::string data = obj->dumps();
    std::string res;
    int status_code;
    
    SrsHttpClient http;
    if ((err = do_post(&http, url, data, status_code, res)) != srs_success) {
        return srs_error_wrap(err, "http: on_connect failed, client_id=%d, url=%s, request=%s, response=%s, code=%d",
            client_id, url.c_str(), data.c_str(), res.c_str(), status_code);
    }
    
    srs_trace("http: on_connect ok, client_id=%d, url=%s, request=%s, response=%s",
              client_id, url.c_str(), data.c_str(), res.c_str());
    
    return err;
}

void SrsHttpHooks::on_close(string url, SrsRequest* req, int64_t send_bytes, int64_t recv_bytes)
{
    srs_error_t err = srs_success;
    
    int client_id = _srs_context->get_id();
    
    SrsJsonObject* obj = SrsJsonAny::object();
    SrsAutoFree(SrsJsonObject, obj);
    
    obj->set("action", SrsJsonAny::str("on_close"));
    obj->set("client_id", SrsJsonAny::integer(client_id));
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
        srs_warn("http: ignore on_close failed, client_id=%d, url=%s, request=%s, response=%s, code=%d, ret=%d",
            client_id, url.c_str(), data.c_str(), res.c_str(), status_code, ret);
        return;
    }
    
    srs_trace("http: on_close ok, client_id=%d, url=%s, request=%s, response=%s",
        client_id, url.c_str(), data.c_str(), res.c_str());
    
    return;
}

srs_error_t SrsHttpHooks::on_publish(string url, SrsRequest* req)
{
    srs_error_t err = srs_success;
    
    int client_id = _srs_context->get_id();
    
    SrsJsonObject* obj = SrsJsonAny::object();
    SrsAutoFree(SrsJsonObject, obj);
    
    obj->set("action", SrsJsonAny::str("on_publish"));
    obj->set("client_id", SrsJsonAny::integer(client_id));
    obj->set("ip", SrsJsonAny::str(req->ip.c_str()));
    obj->set("vhost", SrsJsonAny::str(req->vhost.c_str()));
    obj->set("app", SrsJsonAny::str(req->app.c_str()));
    obj->set("tcUrl", SrsJsonAny::str(req->tcUrl.c_str()));
    obj->set("stream", SrsJsonAny::str(req->stream.c_str()));
    
    std::string data = obj->dumps();
    std::string res;
    int status_code;
    
    SrsHttpClient http;
    if ((err = do_post(&http, url, data, status_code, res)) != srs_success) {
        return srs_error_wrap(err, "http: on_publish failed, client_id=%d, url=%s, request=%s, response=%s, code=%d",
            client_id, url.c_str(), data.c_str(), res.c_str(), status_code);
    }
    
    srs_trace("http: on_publish ok, client_id=%d, url=%s, request=%s, response=%s",
        client_id, url.c_str(), data.c_str(), res.c_str());
    
    return err;
}

void SrsHttpHooks::on_unpublish(string url, SrsRequest* req)
{
    srs_error_t err = srs_success;
    
    int client_id = _srs_context->get_id();
    
    SrsJsonObject* obj = SrsJsonAny::object();
    SrsAutoFree(SrsJsonObject, obj);
    
    obj->set("action", SrsJsonAny::str("on_unpublish"));
    obj->set("client_id", SrsJsonAny::integer(client_id));
    obj->set("ip", SrsJsonAny::str(req->ip.c_str()));
    obj->set("vhost", SrsJsonAny::str(req->vhost.c_str()));
    obj->set("app", SrsJsonAny::str(req->app.c_str()));
    obj->set("stream", SrsJsonAny::str(req->stream.c_str()));
    
    std::string data = obj->dumps();
    std::string res;
    int status_code;
    
    SrsHttpClient http;
    if ((err = do_post(&http, url, data, status_code, res)) != srs_success) {
        int ret = srs_error_code(err);
        srs_freep(err);
        srs_warn("http: ignore on_unpublish failed, client_id=%d, url=%s, request=%s, response=%s, status=%d, ret=%d",
            client_id, url.c_str(), data.c_str(), res.c_str(), status_code, ret);
        return;
    }
    
    srs_trace("http: on_unpublish ok, client_id=%d, url=%s, request=%s, response=%s",
        client_id, url.c_str(), data.c_str(), res.c_str());
    
    return;
}

srs_error_t SrsHttpHooks::on_play(string url, SrsRequest* req)
{
    srs_error_t err = srs_success;
    
    int client_id = _srs_context->get_id();
    
    SrsJsonObject* obj = SrsJsonAny::object();
    SrsAutoFree(SrsJsonObject, obj);
    
    obj->set("action", SrsJsonAny::str("on_play"));
    obj->set("client_id", SrsJsonAny::integer(client_id));
    obj->set("ip", SrsJsonAny::str(req->ip.c_str()));
    obj->set("vhost", SrsJsonAny::str(req->vhost.c_str()));
    obj->set("app", SrsJsonAny::str(req->app.c_str()));
    obj->set("stream", SrsJsonAny::str(req->stream.c_str()));
    obj->set("pageUrl", SrsJsonAny::str(req->pageUrl.c_str()));
    
    std::string data = obj->dumps();
    std::string res;
    int status_code;
    
    SrsHttpClient http;
    if ((err = do_post(&http, url, data, status_code, res)) != srs_success) {
        return srs_error_wrap(err, "http: on_play failed, client_id=%d, url=%s, request=%s, response=%s, status=%d",
            client_id, url.c_str(), data.c_str(), res.c_str(), status_code);
    }
    
    srs_trace("http: on_play ok, client_id=%d, url=%s, request=%s, response=%s",
        client_id, url.c_str(), data.c_str(), res.c_str());
    
    return err;
}

void SrsHttpHooks::on_stop(string url, SrsRequest* req)
{
    srs_error_t err = srs_success;
    
    int client_id = _srs_context->get_id();
    
    SrsJsonObject* obj = SrsJsonAny::object();
    SrsAutoFree(SrsJsonObject, obj);
    
    obj->set("action", SrsJsonAny::str("on_stop"));
    obj->set("client_id", SrsJsonAny::integer(client_id));
    obj->set("ip", SrsJsonAny::str(req->ip.c_str()));
    obj->set("vhost", SrsJsonAny::str(req->vhost.c_str()));
    obj->set("app", SrsJsonAny::str(req->app.c_str()));
    obj->set("stream", SrsJsonAny::str(req->stream.c_str()));
    
    std::string data = obj->dumps();
    std::string res;
    int status_code;
    
    SrsHttpClient http;
    if ((err = do_post(&http, url, data, status_code, res)) != srs_success) {
        int ret = srs_error_code(err);
        srs_freep(err);
        srs_warn("http: ignore on_stop failed, client_id=%d, url=%s, request=%s, response=%s, code=%d, ret=%d",
            client_id, url.c_str(), data.c_str(), res.c_str(), status_code, ret);
        return;
    }
    
    srs_trace("http: on_stop ok, client_id=%d, url=%s, request=%s, response=%s",
        client_id, url.c_str(), data.c_str(), res.c_str());
    
    return;
}

srs_error_t SrsHttpHooks::on_dvr(int cid, string url, SrsRequest* req, string file)
{
    srs_error_t err = srs_success;
    
    int client_id = cid;
    std::string cwd = _srs_config->cwd();
    
    SrsJsonObject* obj = SrsJsonAny::object();
    SrsAutoFree(SrsJsonObject, obj);
    
    obj->set("action", SrsJsonAny::str("on_dvr"));
    obj->set("client_id", SrsJsonAny::integer(client_id));
    obj->set("ip", SrsJsonAny::str(req->ip.c_str()));
    obj->set("vhost", SrsJsonAny::str(req->vhost.c_str()));
    obj->set("app", SrsJsonAny::str(req->app.c_str()));
    obj->set("stream", SrsJsonAny::str(req->stream.c_str()));
    obj->set("cwd", SrsJsonAny::str(cwd.c_str()));
    obj->set("file", SrsJsonAny::str(file.c_str()));
    
    std::string data = obj->dumps();
    std::string res;
    int status_code;
    
    SrsHttpClient http;
    if ((err = do_post(&http, url, data, status_code, res)) != srs_success) {
        return srs_error_wrap(err, "http post on_dvr uri failed, client_id=%d, url=%s, request=%s, response=%s, code=%d",
            client_id, url.c_str(), data.c_str(), res.c_str(), status_code);
    }
    
    srs_trace("http hook on_dvr success. client_id=%d, url=%s, request=%s, response=%s",
        client_id, url.c_str(), data.c_str(), res.c_str());
    
    return err;
}

srs_error_t SrsHttpHooks::on_hls(int cid, string url, SrsRequest* req, string file, string ts_url, string m3u8, string m3u8_url, int sn, double duration)
{
    srs_error_t err = srs_success;
    
    int client_id = cid;
    std::string cwd = _srs_config->cwd();
    
    // the ts_url is under the same dir of m3u8_url.
    string prefix = srs_path_dirname(m3u8_url);
    if (!prefix.empty() && !srs_string_is_http(ts_url)) {
        ts_url = prefix + "/" + ts_url;
    }
    
    SrsJsonObject* obj = SrsJsonAny::object();
    SrsAutoFree(SrsJsonObject, obj);
    
    obj->set("action", SrsJsonAny::str("on_hls"));
    obj->set("client_id", SrsJsonAny::integer(client_id));
    obj->set("ip", SrsJsonAny::str(req->ip.c_str()));
    obj->set("vhost", SrsJsonAny::str(req->vhost.c_str()));
    obj->set("app", SrsJsonAny::str(req->app.c_str()));
    obj->set("stream", SrsJsonAny::str(req->stream.c_str()));
    obj->set("duration", SrsJsonAny::number(duration));
    obj->set("cwd", SrsJsonAny::str(cwd.c_str()));
    obj->set("file", SrsJsonAny::str(file.c_str()));
    obj->set("url", SrsJsonAny::str(ts_url.c_str()));
    obj->set("m3u8", SrsJsonAny::str(m3u8.c_str()));
    obj->set("m3u8_url", SrsJsonAny::str(m3u8_url.c_str()));
    obj->set("seq_no", SrsJsonAny::integer(sn));
    
    std::string data = obj->dumps();
    std::string res;
    int status_code;
    
    SrsHttpClient http;
    if ((err = do_post(&http, url, data, status_code, res)) != srs_success) {
        return srs_error_wrap(err, "http: post %s with %s, status=%d, res=%s", url.c_str(), data.c_str(), status_code, res.c_str());
    }
    
    srs_trace("http: on_hls ok, client_id=%d, url=%s, request=%s, response=%s",
        client_id, url.c_str(), data.c_str(), res.c_str());
    
    return err;
}

srs_error_t SrsHttpHooks::on_hls_notify(int cid, std::string url, SrsRequest* req, std::string ts_url, int nb_notify)
{
    srs_error_t err = srs_success;
    
    int client_id = cid;
    std::string cwd = _srs_config->cwd();
    
    if (srs_string_is_http(ts_url)) {
        url = ts_url;
    }
    
    url = srs_string_replace(url, "[app]", req->app);
    url = srs_string_replace(url, "[stream]", req->stream);
    url = srs_string_replace(url, "[ts_url]", ts_url);
    
    int64_t starttime = srs_update_system_time_ms();
    
    SrsHttpUri uri;
    if ((err = uri.initialize(url)) != srs_success) {
        return srs_error_wrap(err, "http: init url=%s", url.c_str());
    }
    
    SrsHttpClient http;
    if ((err = http.initialize(uri.get_host(), uri.get_port(), SRS_HLS_NOTIFY_TMMS)) != srs_success) {
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
    
    ISrsHttpMessage* msg = NULL;
    if ((err = http.get(path.c_str(), "", &msg)) != srs_success) {
        return srs_error_wrap(err, "http: get %s", url.c_str());
    }
    SrsAutoFree(ISrsHttpMessage, msg);
    
    int nb_buf = srs_min(nb_notify, SRS_HTTP_READ_BUFFER);
    char* buf = new char[nb_buf];
    SrsAutoFreeA(char, buf);
    
    int nb_read = 0;
    ISrsHttpResponseReader* br = msg->body_reader();
    while (nb_read < nb_notify && !br->eof()) {
        int nb_bytes = 0;
        if ((err = br->read(buf, nb_buf, &nb_bytes)) != srs_success) {
            break;
        }
        nb_read += nb_bytes;
    }
    
    int spenttime = (int)(srs_update_system_time_ms() - starttime);
    srs_trace("http hook on_hls_notify success. client_id=%d, url=%s, code=%d, spent=%dms, read=%dB, err=%s",
        client_id, url.c_str(), msg->status_code(), spenttime, nb_read, srs_error_desc(err).c_str());
    
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
    
    SrsJsonObject* robj = NULL;
    SrsAutoFree(SrsJsonObject, robj);
    
    if (true) {
        SrsJsonAny* jr = NULL;
        if ((jr = SrsJsonAny::loads(res)) == NULL) {
            return srs_error_new(ERROR_OCLUSTER_DISCOVER, "load json from %s", res.c_str());
        }
        
        if (!jr->is_object()) {
            srs_freep(jr);
            return srs_error_new(ERROR_OCLUSTER_DISCOVER, "response %s", res.c_str());
        }
        
        robj = jr->to_object();
    }
    
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
    
    srs_trace("http: on_hls ok, url=%s, response=%s", url.c_str(), res.c_str());
    
    return err;
}

srs_error_t SrsHttpHooks::do_post(SrsHttpClient* hc, std::string url, std::string req, int& code, string& res)
{
    srs_error_t err = srs_success;
    
    SrsHttpUri uri;
    if ((err = uri.initialize(url)) != srs_success) {
        return srs_error_wrap(err, "http: post failed. url=%s", url.c_str());
    }
    
    if ((err = hc->initialize(uri.get_host(), uri.get_port())) != srs_success) {
        return srs_error_wrap(err, "http: init client");
    }
    
    string path = uri.get_path();
    if (!uri.get_query().empty()) {
        path += "?" + uri.get_query();
    }
    
    ISrsHttpMessage* msg = NULL;
    if ((err = hc->post(path, req, &msg)) != srs_success) {
        return srs_error_wrap(err, "http: client post");
    }
    SrsAutoFree(ISrsHttpMessage, msg);
    
    code = msg->status_code();
    if ((err = msg->body_read_all(res)) != srs_success) {
        return srs_error_wrap(err, "http: body read");
    }
    
    // ensure the http status is ok.
    // https://github.com/ossrs/srs/issues/158
    if (code != SRS_CONSTS_HTTP_OK && code != SRS_CONSTS_HTTP_Created) {
        return srs_error_new(ERROR_HTTP_STATUS_INVALID, "http: status %d", code);
    }
    
    // should never be empty.
    if (res.empty()) {
        return srs_error_new(ERROR_HTTP_DATA_INVALID, "http: empty response");
    }
    
    // parse string res to json.
    SrsJsonAny* info = SrsJsonAny::loads(res);
    if (!info) {
        return srs_error_new(ERROR_HTTP_DATA_INVALID, "http: not json %s", res.c_str());
    }
    SrsAutoFree(SrsJsonAny, info);
    
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
        return srs_error_new(ERROR_RESPONSE_CODE, "http: response object code %d %s", res_code->to_integer(), res.c_str());
    }
    
    return err;
}
