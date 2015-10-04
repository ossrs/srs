/*
The MIT License (MIT)

Copyright (c) 2013-2015 SRS(simple-rtmp-server)

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

#ifdef SRS_AUTO_HTTP_CALLBACK

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

#define SRS_HTTP_RESPONSE_OK    SRS_XSTR(ERROR_SUCCESS)

#define SRS_HTTP_HEADER_BUFFER        1024
#define SRS_HTTP_READ_BUFFER    4096
#define SRS_HTTP_BODY_BUFFER        32 * 1024

// the timeout for hls notify, in us.
#define SRS_HLS_NOTIFY_TIMEOUT_US (int64_t)(10*1000*1000LL)

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
    
    std::stringstream ss;
    ss << SRS_JOBJECT_START
        << SRS_JFIELD_STR("action", "on_connect") << SRS_JFIELD_CONT
        << SRS_JFIELD_ORG("client_id", client_id) << SRS_JFIELD_CONT
        << SRS_JFIELD_STR("ip", req->ip) << SRS_JFIELD_CONT
        << SRS_JFIELD_STR("vhost", req->vhost) << SRS_JFIELD_CONT
        << SRS_JFIELD_STR("app", req->app) << SRS_JFIELD_CONT
        << SRS_JFIELD_STR("tcUrl", req->tcUrl) << SRS_JFIELD_CONT
        << SRS_JFIELD_STR("pageUrl", req->pageUrl)
        << SRS_JOBJECT_END;
        
    std::string data = ss.str();
    std::string res;
    int status_code;
    if ((ret = do_post(url, data, status_code, res)) != ERROR_SUCCESS) {
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
    
    std::stringstream ss;
    ss << SRS_JOBJECT_START
        << SRS_JFIELD_STR("action", "on_close") << SRS_JFIELD_CONT
        << SRS_JFIELD_ORG("client_id", client_id) << SRS_JFIELD_CONT
        << SRS_JFIELD_STR("ip", req->ip) << SRS_JFIELD_CONT
        << SRS_JFIELD_STR("vhost", req->vhost) << SRS_JFIELD_CONT
        << SRS_JFIELD_ORG("send_bytes", send_bytes) << SRS_JFIELD_CONT
        << SRS_JFIELD_ORG("recv_bytes", recv_bytes) << SRS_JFIELD_CONT
        << SRS_JFIELD_STR("app", req->app)
        << SRS_JOBJECT_END;
        
    std::string data = ss.str();
    std::string res;
    int status_code;
    if ((ret = do_post(url, data, status_code, res)) != ERROR_SUCCESS) {
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
    
    std::stringstream ss;
    ss << SRS_JOBJECT_START
        << SRS_JFIELD_STR("action", "on_publish") << SRS_JFIELD_CONT
        << SRS_JFIELD_ORG("client_id", client_id) << SRS_JFIELD_CONT
        << SRS_JFIELD_STR("ip", req->ip) << SRS_JFIELD_CONT
        << SRS_JFIELD_STR("vhost", req->vhost) << SRS_JFIELD_CONT
        << SRS_JFIELD_STR("app", req->app) << SRS_JFIELD_CONT
        << SRS_JFIELD_STR("stream", req->stream)
        << SRS_JOBJECT_END;
        
    std::string data = ss.str();
    std::string res;
    int status_code;
    if ((ret = do_post(url, data, status_code, res)) != ERROR_SUCCESS) {
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
    
    std::stringstream ss;
    ss << SRS_JOBJECT_START
        << SRS_JFIELD_STR("action", "on_unpublish") << SRS_JFIELD_CONT
        << SRS_JFIELD_ORG("client_id", client_id) << SRS_JFIELD_CONT
        << SRS_JFIELD_STR("ip", req->ip) << SRS_JFIELD_CONT
        << SRS_JFIELD_STR("vhost", req->vhost) << SRS_JFIELD_CONT
        << SRS_JFIELD_STR("app", req->app) << SRS_JFIELD_CONT
        << SRS_JFIELD_STR("stream", req->stream)
        << SRS_JOBJECT_END;
        
    std::string data = ss.str();
    std::string res;
    int status_code;
    if ((ret = do_post(url, data, status_code, res)) != ERROR_SUCCESS) {
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
    
    std::stringstream ss;
    ss << SRS_JOBJECT_START
        << SRS_JFIELD_STR("action", "on_play") << SRS_JFIELD_CONT
        << SRS_JFIELD_ORG("client_id", client_id) << SRS_JFIELD_CONT
        << SRS_JFIELD_STR("ip", req->ip) << SRS_JFIELD_CONT
        << SRS_JFIELD_STR("vhost", req->vhost) << SRS_JFIELD_CONT
        << SRS_JFIELD_STR("app", req->app) << SRS_JFIELD_CONT
        << SRS_JFIELD_STR("stream", req->stream) << SRS_JFIELD_CONT
        << SRS_JFIELD_STR("pageUrl", req->pageUrl)
        << SRS_JOBJECT_END;
        
    std::string data = ss.str();
    std::string res;
    int status_code;
    if ((ret = do_post(url, data, status_code, res)) != ERROR_SUCCESS) {
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
    
    std::stringstream ss;
    ss << SRS_JOBJECT_START
        << SRS_JFIELD_STR("action", "on_stop") << SRS_JFIELD_CONT
        << SRS_JFIELD_ORG("client_id", client_id) << SRS_JFIELD_CONT
        << SRS_JFIELD_STR("ip", req->ip) << SRS_JFIELD_CONT
        << SRS_JFIELD_STR("vhost", req->vhost) << SRS_JFIELD_CONT
        << SRS_JFIELD_STR("app", req->app) << SRS_JFIELD_CONT
        << SRS_JFIELD_STR("stream", req->stream)
        << SRS_JOBJECT_END;
        
    std::string data = ss.str();
    std::string res;
    int status_code;
    if ((ret = do_post(url, data, status_code, res)) != ERROR_SUCCESS) {
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
    
    std::stringstream ss;
    ss << SRS_JOBJECT_START
        << SRS_JFIELD_STR("action", "on_dvr") << SRS_JFIELD_CONT
        << SRS_JFIELD_ORG("client_id", client_id) << SRS_JFIELD_CONT
        << SRS_JFIELD_STR("ip", req->ip) << SRS_JFIELD_CONT
        << SRS_JFIELD_STR("vhost", req->vhost) << SRS_JFIELD_CONT
        << SRS_JFIELD_STR("app", req->app) << SRS_JFIELD_CONT
        << SRS_JFIELD_STR("stream", req->stream) << SRS_JFIELD_CONT
        << SRS_JFIELD_STR("cwd", cwd) << SRS_JFIELD_CONT
        << SRS_JFIELD_STR("file", file)
        << SRS_JOBJECT_END;
        
    std::string data = ss.str();
    std::string res;
    int status_code;
    if ((ret = do_post(url, data, status_code, res)) != ERROR_SUCCESS) {
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
    
    std::stringstream ss;
    ss << SRS_JOBJECT_START
        << SRS_JFIELD_STR("action", "on_hls") << SRS_JFIELD_CONT
        << SRS_JFIELD_ORG("client_id", client_id) << SRS_JFIELD_CONT
        << SRS_JFIELD_STR("ip", req->ip) << SRS_JFIELD_CONT
        << SRS_JFIELD_STR("vhost", req->vhost) << SRS_JFIELD_CONT
        << SRS_JFIELD_STR("app", req->app) << SRS_JFIELD_CONT
        << SRS_JFIELD_STR("stream", req->stream) << SRS_JFIELD_CONT
        << SRS_JFIELD_ORG("duration", duration) << SRS_JFIELD_CONT
        << SRS_JFIELD_STR("cwd", cwd) << SRS_JFIELD_CONT
        << SRS_JFIELD_STR("file", file) << SRS_JFIELD_CONT
        << SRS_JFIELD_STR("url", ts_url) << SRS_JFIELD_CONT
        << SRS_JFIELD_STR("m3u8", m3u8) << SRS_JFIELD_CONT
        << SRS_JFIELD_STR("m3u8_url", m3u8_url) << SRS_JFIELD_CONT
        << SRS_JFIELD_ORG("seq_no", sn)
        << SRS_JOBJECT_END;
        
    std::string data = ss.str();
    std::string res;
    int status_code;
    if ((ret = do_post(url, data, status_code, res)) != ERROR_SUCCESS) {
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
    
    if (srs_string_starts_with(ts_url, "http://") || srs_string_starts_with(ts_url, "https://")) {
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
    if ((ret = http.initialize(uri.get_host(), uri.get_port(), SRS_HLS_NOTIFY_TIMEOUT_US)) != ERROR_SUCCESS) {
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
    SrsAutoFree(char, buf);
    
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

int SrsHttpHooks::do_post(std::string url, std::string req, int& code, string& res)
{
    int ret = ERROR_SUCCESS;
    
    SrsHttpUri uri;
    if ((ret = uri.initialize(url)) != ERROR_SUCCESS) {
        srs_error("http: post failed. url=%s, ret=%d", url.c_str(), ret);
        return ret;
    }
    
    SrsHttpClient http;
    if ((ret = http.initialize(uri.get_host(), uri.get_port())) != ERROR_SUCCESS) {
        return ret;
    }
    
    ISrsHttpMessage* msg = NULL;
    if ((ret = http.post(uri.get_path(), req, &msg)) != ERROR_SUCCESS) {
        return ret;
    }
    SrsAutoFree(ISrsHttpMessage, msg);
    
    code = msg->status_code();
    if ((ret = msg->body_read_all(res)) != ERROR_SUCCESS) {
        return ret;
    }
    
    // ensure the http status is ok.
    // https://github.com/simple-rtmp-server/srs/issues/158
    if (code != SRS_CONSTS_HTTP_OK) {
        ret = ERROR_HTTP_STATUS_INVLIAD;
        srs_error("invalid response status=%d. ret=%d", code, ret);
        return ret;
    }
    
    // should never be empty.
    if (res.empty()) {
        ret = ERROR_HTTP_DATA_INVLIAD;
        srs_error("invalid empty response. ret=%d", ret);
        return ret;
    }
    
    // parse string res to json.
    SrsJsonAny* info = SrsJsonAny::loads((char*)res.c_str());
    if (!info) {
        ret = ERROR_HTTP_DATA_INVLIAD;
        srs_error("invalid response %s. ret=%d", res.c_str(), ret);
        return ret;
    }
    SrsAutoFree(SrsJsonAny, info);
    
    // response error code in string.
    if (!info->is_object()) {
        if (res != SRS_HTTP_RESPONSE_OK) {
            ret = ERROR_HTTP_DATA_INVLIAD;
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

#endif
