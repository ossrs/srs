/*
The MIT License (MIT)

Copyright (c) 2013-2017 SRS(ossrs)

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
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

int SrsHttpHooks::on_connect(string url, SrsRequest* req)
{
    int ret = ERROR_SUCCESS;
    
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
    if ((ret = do_post(&http, url, data, status_code, res)) != ERROR_SUCCESS) {
        srs_error("http post on_connect uri failed. "
            "client_id=%d, url=%s, request=%s, response=%s, code=%d, ret=%d",
            client_id, url.c_str(), data.c_str(), res.c_str(), status_code, ret);
        return ret;
    }
    
    srs_trace("http hook on_connect success. "
        "client_id=%d, url=%s, request=%s, response=%s, ret=%d",
        client_id, url.c_str(), data.c_str(), res.c_str(), ret);
    
    return ret;
}

void SrsHttpHooks::on_close(string url, SrsRequest* req, int64_t send_bytes, int64_t recv_bytes)
{
    int ret = ERROR_SUCCESS;
    
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
    if ((ret = do_post(&http, url, data, status_code, res)) != ERROR_SUCCESS) {
        srs_warn("http post on_close uri failed, ignored. "
            "client_id=%d, url=%s, request=%s, response=%s, code=%d, ret=%d",
            client_id, url.c_str(), data.c_str(), res.c_str(), status_code, ret);
        return;
    }
    
    srs_trace("http hook on_close success. "
        "client_id=%d, url=%s, request=%s, response=%s, ret=%d",
        client_id, url.c_str(), data.c_str(), res.c_str(), ret);
    
    return;
}

int SrsHttpHooks::on_publish(string url, SrsRequest* req)
{
    int ret = ERROR_SUCCESS;
    
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
    if ((ret = do_post(&http, url, data, status_code, res)) != ERROR_SUCCESS) {
        srs_error("http post on_publish uri failed. "
            "client_id=%d, url=%s, request=%s, response=%s, code=%d, ret=%d",
            client_id, url.c_str(), data.c_str(), res.c_str(), status_code, ret);
        return ret;
    }
    
    srs_trace("http hook on_publish success. "
        "client_id=%d, url=%s, request=%s, response=%s, ret=%d",
        client_id, url.c_str(), data.c_str(), res.c_str(), ret);
    
    return ret;
}

void SrsHttpHooks::on_unpublish(string url, SrsRequest* req)
{
    int ret = ERROR_SUCCESS;
    
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
    if ((ret = do_post(&http, url, data, status_code, res)) != ERROR_SUCCESS) {
        srs_warn("http post on_unpublish uri failed, ignored. "
            "client_id=%d, url=%s, request=%s, response=%s, code=%d, ret=%d",
            client_id, url.c_str(), data.c_str(), res.c_str(), status_code, ret);
        return;
    }
    
    srs_trace("http hook on_unpublish success. "
        "client_id=%d, url=%s, request=%s, response=%s, ret=%d",
        client_id, url.c_str(), data.c_str(), res.c_str(), ret);
    
    return;
}

int SrsHttpHooks::on_play(string url, SrsRequest* req)
{
    int ret = ERROR_SUCCESS;
    
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
    if ((ret = do_post(&http, url, data, status_code, res)) != ERROR_SUCCESS) {
        srs_error("http post on_play uri failed. "
            "client_id=%d, url=%s, request=%s, response=%s, code=%d, ret=%d",
            client_id, url.c_str(), data.c_str(), res.c_str(), status_code, ret);
        return ret;
    }
    
    srs_trace("http hook on_play success. "
        "client_id=%d, url=%s, request=%s, response=%s, ret=%d",
        client_id, url.c_str(), data.c_str(), res.c_str(), ret);
    
    return ret;
}

void SrsHttpHooks::on_stop(string url, SrsRequest* req)
{
    int ret = ERROR_SUCCESS;
    
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
    if ((ret = do_post(&http, url, data, status_code, res)) != ERROR_SUCCESS) {
        srs_warn("http post on_stop uri failed, ignored. "
            "client_id=%d, url=%s, request=%s, response=%s, code=%d, ret=%d",
            client_id, url.c_str(), data.c_str(), res.c_str(), status_code, ret);
        return;
    }
    
    srs_trace("http hook on_stop success. "
        "client_id=%d, url=%s, request=%s, response=%s, ret=%d",
        client_id, url.c_str(), data.c_str(), res.c_str(), ret);
    
    return;
}

int SrsHttpHooks::on_dvr(int cid, string url, SrsRequest* req, string file)
{
    int ret = ERROR_SUCCESS;
    
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
    if ((ret = do_post(&http, url, data, status_code, res)) != ERROR_SUCCESS) {
        srs_error("http post on_dvr uri failed, ignored. "
            "client_id=%d, url=%s, request=%s, response=%s, code=%d, ret=%d",
            client_id, url.c_str(), data.c_str(), res.c_str(), status_code, ret);
        return ret;
    }
    
    srs_trace("http hook on_dvr success. "
        "client_id=%d, url=%s, request=%s, response=%s, ret=%d",
        client_id, url.c_str(), data.c_str(), res.c_str(), ret);
    
    return ret;
}

int SrsHttpHooks::on_hls(int cid, string url, SrsRequest* req, string file, string ts_url, string m3u8, string m3u8_url, int sn, double duration)
{
    int ret = ERROR_SUCCESS;
    
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
    if ((ret = do_post(&http, url, data, status_code, res)) != ERROR_SUCCESS) {
        srs_error("http post on_hls uri failed, ignored. "
            "client_id=%d, url=%s, request=%s, response=%s, code=%d, ret=%d",
            client_id, url.c_str(), data.c_str(), res.c_str(), status_code, ret);
        return ret;
    }
    
    srs_trace("http hook on_hls success. "
        "client_id=%d, url=%s, request=%s, response=%s, ret=%d",
        client_id, url.c_str(), data.c_str(), res.c_str(), ret);
    
    return ret;
}

int SrsHttpHooks::on_hls_notify(int cid, std::string url, SrsRequest* req, std::string ts_url, int nb_notify)
{
    int ret = ERROR_SUCCESS;
    
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
    if ((ret = uri.initialize(url)) != ERROR_SUCCESS) {
        srs_error("http: post failed. url=%s, ret=%d", url.c_str(), ret);
        return ret;
    }
    
    SrsHttpClient http;
    if ((ret = http.initialize(uri.get_host(), uri.get_port(), SRS_HLS_NOTIFY_TMMS)) != ERROR_SUCCESS) {
        return ret;
    }
    
    std::string path = uri.get_query();
    if (path.empty()) {
        path = uri.get_path();
    } else {
        path = uri.get_path();
        path += "?";
        path += uri.get_query();
    }
    srs_warn("GET %s", path.c_str());
    
    ISrsHttpMessage* msg = NULL;
    if ((ret = http.get(path.c_str(), "", &msg)) != ERROR_SUCCESS) {
        return ret;
    }
    SrsAutoFree(ISrsHttpMessage, msg);
    
    int nb_buf = srs_min(nb_notify, SRS_HTTP_READ_BUFFER);
    char* buf = new char[nb_buf];
    SrsAutoFreeA(char, buf);
    
    int nb_read = 0;
    ISrsHttpResponseReader* br = msg->body_reader();
    while (nb_read < nb_notify && !br->eof()) {
        int nb_bytes = 0;
        if ((ret = br->read(buf, nb_buf, &nb_bytes)) != ERROR_SUCCESS) {
            break;
        }
        nb_read += nb_bytes;
    }
    
    int spenttime = (int)(srs_update_system_time_ms() - starttime);
    srs_trace("http hook on_hls_notify success. client_id=%d, url=%s, code=%d, spent=%dms, read=%dB, ret=%d",
        client_id, url.c_str(), msg->status_code(), spenttime, nb_read, ret);
    
    // ignore any error for on_hls_notify.
    ret = ERROR_SUCCESS;
    
    return ret;
}

int SrsHttpHooks::do_post(SrsHttpClient* hc, std::string url, std::string req, int& code, string& res)
{
    int ret = ERROR_SUCCESS;
    
    SrsHttpUri uri;
    if ((ret = uri.initialize(url)) != ERROR_SUCCESS) {
        srs_error("http: post failed. url=%s, ret=%d", url.c_str(), ret);
        return ret;
    }
    
    if ((ret = hc->initialize(uri.get_host(), uri.get_port())) != ERROR_SUCCESS) {
        return ret;
    }
    
    ISrsHttpMessage* msg = NULL;
    if ((ret = hc->post(uri.get_path(), req, &msg)) != ERROR_SUCCESS) {
        return ret;
    }
    SrsAutoFree(ISrsHttpMessage, msg);
    
    code = msg->status_code();
    if ((ret = msg->body_read_all(res)) != ERROR_SUCCESS) {
        return ret;
    }
    
    // ensure the http status is ok.
    // https://github.com/ossrs/srs/issues/158
    if (code != SRS_CONSTS_HTTP_OK && code != SRS_CONSTS_HTTP_Created) {
        ret = ERROR_HTTP_STATUS_INVALID;
        srs_error("invalid response status=%d. ret=%d", code, ret);
        return ret;
    }
    
    // should never be empty.
    if (res.empty()) {
        ret = ERROR_HTTP_DATA_INVALID;
        srs_error("invalid empty response. ret=%d", ret);
        return ret;
    }
    
    // parse string res to json.
    SrsJsonAny* info = SrsJsonAny::loads((char*)res.c_str());
    if (!info) {
        ret = ERROR_HTTP_DATA_INVALID;
        srs_error("invalid response %s. ret=%d", res.c_str(), ret);
        return ret;
    }
    SrsAutoFree(SrsJsonAny, info);
    
    // response error code in string.
    if (!info->is_object()) {
        if (res != SRS_HTTP_RESPONSE_OK) {
            ret = ERROR_HTTP_DATA_INVALID;
            srs_error("invalid response number %s. ret=%d", res.c_str(), ret);
            return ret;
        }
        return ret;
    }
    
    // response standard object, format in json: {"code": 0, "data": ""}
    SrsJsonObject* res_info = info->to_object();
    SrsJsonAny* res_code = NULL;
    if ((res_code = res_info->ensure_property_integer("code")) == NULL) {
        ret = ERROR_RESPONSE_CODE;
        srs_error("invalid response without code, ret=%d", ret);
        return ret;
    }
    
    if ((res_code->to_integer()) != ERROR_SUCCESS) {
        ret = ERROR_RESPONSE_CODE;
        srs_error("error response code=%d. ret=%d", res_code->to_integer(), ret);
        return ret;
    }
    
    return ret;
}
