/*
The MIT License (MIT)

Copyright (c) 2013-2015 winlin

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
#include <srs_protocol_rtmp.hpp>
#include <srs_app_st_socket.hpp>
#include <srs_app_http.hpp>
#include <srs_app_json.hpp>
#include <srs_app_dvr.hpp>
#include <srs_app_http_client.hpp>

#define SRS_HTTP_RESPONSE_OK "0"

#define SRS_HTTP_HEADER_BUFFER        1024
#define SRS_HTTP_BODY_BUFFER        32 * 1024

SrsHttpHooks::SrsHttpHooks()
{
}

SrsHttpHooks::~SrsHttpHooks()
{
}

int SrsHttpHooks::on_connect(string url, int client_id, string ip, SrsRequest* req)
{
    int ret = ERROR_SUCCESS;
    
    SrsHttpUri uri;
    if ((ret = uri.initialize(url)) != ERROR_SUCCESS) {
        srs_error("http uri parse on_connect url failed. "
            "client_id=%d, url=%s, ret=%d", client_id, url.c_str(), ret);
        return ret;
    }
    
    std::stringstream ss;
    ss << __SRS_JOBJECT_START
        << __SRS_JFIELD_STR("action", "on_connect") << __SRS_JFIELD_CONT
        << __SRS_JFIELD_ORG("client_id", client_id) << __SRS_JFIELD_CONT
        << __SRS_JFIELD_STR("ip", ip) << __SRS_JFIELD_CONT
        << __SRS_JFIELD_STR("vhost", req->vhost) << __SRS_JFIELD_CONT
        << __SRS_JFIELD_STR("app", req->app) << __SRS_JFIELD_CONT
        << __SRS_JFIELD_STR("tcUrl", req->tcUrl) << __SRS_JFIELD_CONT
        << __SRS_JFIELD_STR("pageUrl", req->pageUrl)
        << __SRS_JOBJECT_END;
    std::string data = ss.str();
    std::string res;
    
    SrsHttpClient http;
    if ((ret = http.post(&uri, data, res)) != ERROR_SUCCESS) {
        srs_error("http post on_connect uri failed. "
            "client_id=%d, url=%s, request=%s, response=%s, ret=%d",
            client_id, url.c_str(), data.c_str(), res.c_str(), ret);
        return ret;
    }
    
    if (res.empty() || res != SRS_HTTP_RESPONSE_OK) {
        ret = ERROR_HTTP_DATA_INVLIAD;
        srs_error("http hook on_connect validate failed. "
            "client_id=%d, res=%s, ret=%d", client_id, res.c_str(), ret);
        return ret;
    }
    
    srs_trace("http hook on_connect success. "
        "client_id=%d, url=%s, request=%s, response=%s, ret=%d",
        client_id, url.c_str(), data.c_str(), res.c_str(), ret);
    
    return ret;
}

void SrsHttpHooks::on_close(string url, int client_id, string ip, SrsRequest* req)
{
    int ret = ERROR_SUCCESS;
    
    SrsHttpUri uri;
    if ((ret = uri.initialize(url)) != ERROR_SUCCESS) {
        srs_warn("http uri parse on_close url failed, ignored. "
            "client_id=%d, url=%s, ret=%d", client_id, url.c_str(), ret);
        return;
    }
    
    std::stringstream ss;
    ss << __SRS_JOBJECT_START
        << __SRS_JFIELD_STR("action", "on_close") << __SRS_JFIELD_CONT
        << __SRS_JFIELD_ORG("client_id", client_id) << __SRS_JFIELD_CONT
        << __SRS_JFIELD_STR("ip", ip) << __SRS_JFIELD_CONT
        << __SRS_JFIELD_STR("vhost", req->vhost) << __SRS_JFIELD_CONT
        << __SRS_JFIELD_STR("app", req->app) << __SRS_JFIELD_CONT
        << __SRS_JFIELD_STR("pageUrl", req->pageUrl)
        << __SRS_JOBJECT_END;
    std::string data = ss.str();
    std::string res;
    
    SrsHttpClient http;
    if ((ret = http.post(&uri, data, res)) != ERROR_SUCCESS) {
        srs_warn("http post on_close uri failed, ignored. "
            "client_id=%d, url=%s, request=%s, response=%s, ret=%d",
            client_id, url.c_str(), data.c_str(), res.c_str(), ret);
        return;
    }
    
    if (res.empty() || res != SRS_HTTP_RESPONSE_OK) {
        ret = ERROR_HTTP_DATA_INVLIAD;
        srs_warn("http hook on_close validate failed, ignored. "
            "client_id=%d, res=%s, ret=%d", client_id, res.c_str(), ret);
        return;
    }
    
    srs_trace("http hook on_close success. "
        "client_id=%d, url=%s, request=%s, response=%s, ret=%d",
        client_id, url.c_str(), data.c_str(), res.c_str(), ret);
    
    return;
}

int SrsHttpHooks::on_publish(string url, int client_id, string ip, SrsRequest* req)
{
    int ret = ERROR_SUCCESS;
    
    SrsHttpUri uri;
    if ((ret = uri.initialize(url)) != ERROR_SUCCESS) {
        srs_error("http uri parse on_publish url failed. "
            "client_id=%d, url=%s, ret=%d", client_id, url.c_str(), ret);
        return ret;
    }
    
    std::stringstream ss;
    ss << __SRS_JOBJECT_START
        << __SRS_JFIELD_STR("action", "on_publish") << __SRS_JFIELD_CONT
        << __SRS_JFIELD_ORG("client_id", client_id) << __SRS_JFIELD_CONT
        << __SRS_JFIELD_STR("ip", ip) << __SRS_JFIELD_CONT
        << __SRS_JFIELD_STR("vhost", req->vhost) << __SRS_JFIELD_CONT
        << __SRS_JFIELD_STR("app", req->app) << __SRS_JFIELD_CONT
        << __SRS_JFIELD_STR("pageUrl", req->pageUrl) << __SRS_JFIELD_CONT
        << __SRS_JFIELD_STR("stream", req->stream)
        << __SRS_JOBJECT_END;
    std::string data = ss.str();
    std::string res;
    
    SrsHttpClient http;
    if ((ret = http.post(&uri, data, res)) != ERROR_SUCCESS) {
        srs_error("http post on_publish uri failed. "
            "client_id=%d, url=%s, request=%s, response=%s, ret=%d",
            client_id, url.c_str(), data.c_str(), res.c_str(), ret);
        return ret;
    }
    
    if (res.empty() || res != SRS_HTTP_RESPONSE_OK) {
        ret = ERROR_HTTP_DATA_INVLIAD;
        srs_error("http hook on_publish validate failed. "
            "client_id=%d, res=%s, ret=%d", client_id, res.c_str(), ret);
        return ret;
    }
    
    srs_trace("http hook on_publish success. "
        "client_id=%d, url=%s, request=%s, response=%s, ret=%d",
        client_id, url.c_str(), data.c_str(), res.c_str(), ret);
    
    return ret;
}

void SrsHttpHooks::on_unpublish(string url, int client_id, string ip, SrsRequest* req)
{
    int ret = ERROR_SUCCESS;
    
    SrsHttpUri uri;
    if ((ret = uri.initialize(url)) != ERROR_SUCCESS) {
        srs_warn("http uri parse on_unpublish url failed, ignored. "
            "client_id=%d, url=%s, ret=%d", client_id, url.c_str(), ret);
        return;
    }
    
    std::stringstream ss;
    ss << __SRS_JOBJECT_START
        << __SRS_JFIELD_STR("action", "on_unpublish") << __SRS_JFIELD_CONT
        << __SRS_JFIELD_ORG("client_id", client_id) << __SRS_JFIELD_CONT
        << __SRS_JFIELD_STR("ip", ip) << __SRS_JFIELD_CONT
        << __SRS_JFIELD_STR("vhost", req->vhost) << __SRS_JFIELD_CONT
        << __SRS_JFIELD_STR("app", req->app) << __SRS_JFIELD_CONT
        << __SRS_JFIELD_STR("pageUrl", req->pageUrl) << __SRS_JFIELD_CONT
        << __SRS_JFIELD_STR("stream", req->stream)
        << __SRS_JOBJECT_END;
    std::string data = ss.str();
    std::string res;
    
    SrsHttpClient http;
    if ((ret = http.post(&uri, data, res)) != ERROR_SUCCESS) {
        srs_warn("http post on_unpublish uri failed, ignored. "
            "client_id=%d, url=%s, request=%s, response=%s, ret=%d",
            client_id, url.c_str(), data.c_str(), res.c_str(), ret);
        return;
    }
    
    if (res.empty() || res != SRS_HTTP_RESPONSE_OK) {
        ret = ERROR_HTTP_DATA_INVLIAD;
        srs_warn("http hook on_unpublish validate failed, ignored. "
            "client_id=%d, res=%s, ret=%d", client_id, res.c_str(), ret);
        return;
    }
    
    srs_trace("http hook on_unpublish success. "
        "client_id=%d, url=%s, request=%s, response=%s, ret=%d",
        client_id, url.c_str(), data.c_str(), res.c_str(), ret);
    
    return;
}

int SrsHttpHooks::on_play(string url, int client_id, string ip, SrsRequest* req)
{
    int ret = ERROR_SUCCESS;
    
    SrsHttpUri uri;
    if ((ret = uri.initialize(url)) != ERROR_SUCCESS) {
        srs_error("http uri parse on_play url failed. "
            "client_id=%d, url=%s, ret=%d", client_id, url.c_str(), ret);
        return ret;
    }
    
    std::stringstream ss;
    ss << __SRS_JOBJECT_START
        << __SRS_JFIELD_STR("action", "on_play") << __SRS_JFIELD_CONT
        << __SRS_JFIELD_ORG("client_id", client_id) << __SRS_JFIELD_CONT
        << __SRS_JFIELD_STR("ip", ip) << __SRS_JFIELD_CONT
        << __SRS_JFIELD_STR("vhost", req->vhost) << __SRS_JFIELD_CONT
        << __SRS_JFIELD_STR("app", req->app) << __SRS_JFIELD_CONT
        << __SRS_JFIELD_STR("pageUrl", req->pageUrl) << __SRS_JFIELD_CONT
        << __SRS_JFIELD_STR("stream", req->stream)
        << __SRS_JOBJECT_END;
    std::string data = ss.str();
    std::string res;
    
    SrsHttpClient http;
    if ((ret = http.post(&uri, data, res)) != ERROR_SUCCESS) {
        srs_error("http post on_play uri failed. "
            "client_id=%d, url=%s, request=%s, response=%s, ret=%d",
            client_id, url.c_str(), data.c_str(), res.c_str(), ret);
        return ret;
    }
    
    if (res.empty() || res != SRS_HTTP_RESPONSE_OK) {
        ret = ERROR_HTTP_DATA_INVLIAD;
        srs_error("http hook on_play validate failed. "
            "client_id=%d, res=%s, ret=%d", client_id, res.c_str(), ret);
        return ret;
    }
    
    srs_trace("http hook on_play success. "
        "client_id=%d, url=%s, request=%s, response=%s, ret=%d",
        client_id, url.c_str(), data.c_str(), res.c_str(), ret);
    
    return ret;
}

void SrsHttpHooks::on_stop(string url, int client_id, string ip, SrsRequest* req)
{
    int ret = ERROR_SUCCESS;
    
    SrsHttpUri uri;
    if ((ret = uri.initialize(url)) != ERROR_SUCCESS) {
        srs_warn("http uri parse on_stop url failed, ignored. "
            "client_id=%d, url=%s, ret=%d", client_id, url.c_str(), ret);
        return;
    }
    
    std::stringstream ss;
    ss << __SRS_JOBJECT_START
        << __SRS_JFIELD_STR("action", "on_stop") << __SRS_JFIELD_CONT
        << __SRS_JFIELD_ORG("client_id", client_id) << __SRS_JFIELD_CONT
        << __SRS_JFIELD_STR("ip", ip) << __SRS_JFIELD_CONT
        << __SRS_JFIELD_STR("vhost", req->vhost) << __SRS_JFIELD_CONT
        << __SRS_JFIELD_STR("app", req->app) << __SRS_JFIELD_CONT
        << __SRS_JFIELD_STR("pageUrl", req->pageUrl) << __SRS_JFIELD_CONT
        << __SRS_JFIELD_STR("stream", req->stream)
        << __SRS_JOBJECT_END;
    std::string data = ss.str();
    std::string res;
    
    SrsHttpClient http;
    if ((ret = http.post(&uri, data, res)) != ERROR_SUCCESS) {
        srs_warn("http post on_stop uri failed, ignored. "
            "client_id=%d, url=%s, request=%s, response=%s, ret=%d",
            client_id, url.c_str(), data.c_str(), res.c_str(), ret);
        return;
    }
    
    if (res.empty() || res != SRS_HTTP_RESPONSE_OK) {
        ret = ERROR_HTTP_DATA_INVLIAD;
        srs_warn("http hook on_stop validate failed, ignored. "
            "client_id=%d, res=%s, ret=%d", client_id, res.c_str(), ret);
        return;
    }
    
    srs_trace("http hook on_stop success. "
        "client_id=%d, url=%s, request=%s, response=%s, ret=%d",
        client_id, url.c_str(), data.c_str(), res.c_str(), ret);
    
    return;
}

#endif

