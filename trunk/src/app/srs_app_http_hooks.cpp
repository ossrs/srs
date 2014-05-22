/*
The MIT License (MIT)

Copyright (c) 2013-2014 winlin

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
#include <srs_kernel_log.hpp>
#include <srs_app_socket.hpp>
#include <srs_app_http.hpp>
#include <srs_app_json.hpp>
#include <srs_app_dvr.hpp>
#include <srs_app_config.hpp>
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
    ss << JOBJECT_START
        << JFIELD_STR("action", "on_connect") << JFIELD_CONT
        << JFIELD_ORG("client_id", client_id) << JFIELD_CONT
        << JFIELD_STR("ip", ip) << JFIELD_CONT
        << JFIELD_STR("vhost", req->vhost) << JFIELD_CONT
        << JFIELD_STR("app", req->app) << JFIELD_CONT
        << JFIELD_STR("tcUrl", req->tcUrl) << JFIELD_CONT
        << JFIELD_STR("pageUrl", req->pageUrl)
        << JOBJECT_END;
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
    ss << JOBJECT_START
        << JFIELD_STR("action", "on_close") << JFIELD_CONT
        << JFIELD_ORG("client_id", client_id) << JFIELD_CONT
        << JFIELD_STR("ip", ip) << JFIELD_CONT
        << JFIELD_STR("vhost", req->vhost) << JFIELD_CONT
        << JFIELD_STR("app", req->app) << JFIELD_CONT
        << JFIELD_STR("pageUrl", req->pageUrl)
        << JOBJECT_END;
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
    ss << JOBJECT_START
        << JFIELD_STR("action", "on_publish") << JFIELD_CONT
        << JFIELD_ORG("client_id", client_id) << JFIELD_CONT
        << JFIELD_STR("ip", ip) << JFIELD_CONT
        << JFIELD_STR("vhost", req->vhost) << JFIELD_CONT
        << JFIELD_STR("app", req->app) << JFIELD_CONT
        << JFIELD_STR("pageUrl", req->pageUrl) << JFIELD_CONT
        << JFIELD_STR("stream", req->stream)
        << JOBJECT_END;
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
    ss << JOBJECT_START
        << JFIELD_STR("action", "on_unpublish") << JFIELD_CONT
        << JFIELD_ORG("client_id", client_id) << JFIELD_CONT
        << JFIELD_STR("ip", ip) << JFIELD_CONT
        << JFIELD_STR("vhost", req->vhost) << JFIELD_CONT
        << JFIELD_STR("app", req->app) << JFIELD_CONT
        << JFIELD_STR("pageUrl", req->pageUrl) << JFIELD_CONT
        << JFIELD_STR("stream", req->stream)
        << JOBJECT_END;
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
    ss << JOBJECT_START
        << JFIELD_STR("action", "on_play") << JFIELD_CONT
        << JFIELD_ORG("client_id", client_id) << JFIELD_CONT
        << JFIELD_STR("ip", ip) << JFIELD_CONT
        << JFIELD_STR("vhost", req->vhost) << JFIELD_CONT
        << JFIELD_STR("app", req->app) << JFIELD_CONT
        << JFIELD_STR("pageUrl", req->pageUrl) << JFIELD_CONT
        << JFIELD_STR("stream", req->stream)
        << JOBJECT_END;
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
    ss << JOBJECT_START
        << JFIELD_STR("action", "on_stop") << JFIELD_CONT
        << JFIELD_ORG("client_id", client_id) << JFIELD_CONT
        << JFIELD_STR("ip", ip) << JFIELD_CONT
        << JFIELD_STR("vhost", req->vhost) << JFIELD_CONT
        << JFIELD_STR("app", req->app) << JFIELD_CONT
        << JFIELD_STR("pageUrl", req->pageUrl) << JFIELD_CONT
        << JFIELD_STR("stream", req->stream)
        << JOBJECT_END;
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

#ifdef SRS_AUTO_DVR

void SrsHttpHooks::on_dvr_hss_reap_flv_header(std::string url, SrsRequest* req, std::string header_file)
{
    int ret = ERROR_SUCCESS;
    
    srs_verbose("flv header reap, file=%s", header_file.c_str());
    
    SrsHttpUri uri;
    if ((ret = uri.initialize(url)) != ERROR_SUCCESS) {
        srs_warn("http uri parse on_dvr_hss_reap_flv_header url failed, ignored. "
            "url=%s, ret=%d", url.c_str(), ret);
        return;
    }
    
    std::stringstream ss;
    ss << JOBJECT_START
        << JFIELD_STR("action", "on_dvr_hss_reap_flv_header") << JFIELD_CONT
        << JFIELD_STR("vhost", req->vhost) << JFIELD_CONT
        << JFIELD_STR("app", req->app) << JFIELD_CONT
        << JFIELD_STR("stream", req->stream) << JFIELD_CONT
        << JFIELD_NAME("segment") << JOBJECT_START
            << JFIELD_STR("cwd", _srs_config->cwd()) << JFIELD_CONT
            << JFIELD_STR("path", header_file)
        << JOBJECT_END
        << JOBJECT_END;
    std::string data = ss.str();
    std::string res;
    
    SrsHttpClient http;
    if ((ret = http.post(&uri, data, res)) != ERROR_SUCCESS) {
        srs_warn("http post on_dvr_hss_reap_flv_header uri failed, ignored. "
            "url=%s, request=%s, response=%s, ret=%d",
            url.c_str(), data.c_str(), res.c_str(), ret);
        return;
    }
    
    if (res.empty() || res != SRS_HTTP_RESPONSE_OK) {
        ret = ERROR_HTTP_DATA_INVLIAD;
        srs_warn("http hook on_dvr_hss_reap_flv_header validate failed, ignored. "
            "res=%s, ret=%d", res.c_str(), ret);
        return;
    }
    
    srs_info("http hook on_dvr_hss_reap_flv_header success. "
        "url=%s, request=%s, response=%s, ret=%d",
        url.c_str(), data.c_str(), res.c_str(), ret);
    
    return;
}

void SrsHttpHooks::on_dvr_hss_reap_flv(string url, SrsRequest* req, SrsFlvSegment* segment)
{
    int ret = ERROR_SUCCESS;
    
    srs_assert(segment);
    srs_verbose("flv segment %s, atc_start=%"PRId64", "
        "has_key=%d, starttime=%"PRId64", duration=%d", 
        segment->path.c_str(), segment->stream_starttime,
        segment->has_keyframe, segment->starttime, (int)segment->duration);
    
    SrsHttpUri uri;
    if ((ret = uri.initialize(url)) != ERROR_SUCCESS) {
        srs_warn("http uri parse on_dvr_hss_reap_flv url failed, ignored. "
            "url=%s, ret=%d", url.c_str(), ret);
        return;
    }
    
    std::stringstream ss;
    ss << JOBJECT_START
        << JFIELD_STR("action", "on_dvr_hss_reap_flv") << JFIELD_CONT
        << JFIELD_STR("vhost", req->vhost) << JFIELD_CONT
        << JFIELD_STR("app", req->app) << JFIELD_CONT
        << JFIELD_STR("stream", req->stream) << JFIELD_CONT
        << JFIELD_NAME("segment") << JOBJECT_START
            << JFIELD_STR("cwd", _srs_config->cwd()) << JFIELD_CONT
            << JFIELD_STR("path", segment->path) << JFIELD_CONT
            << JFIELD_ORG("duration", segment->duration) << JFIELD_CONT
            << JFIELD_ORG("offset", segment->sequence_header_offset) << JFIELD_CONT
            << JFIELD_ORG("has_keyframe", (segment->has_keyframe? "true":"false")) << JFIELD_CONT
            << JFIELD_ORG("pts", segment->stream_starttime + segment->starttime)
        << JOBJECT_END
        << JOBJECT_END;
    std::string data = ss.str();
    std::string res;
    
    SrsHttpClient http;
    if ((ret = http.post(&uri, data, res)) != ERROR_SUCCESS) {
        srs_warn("http post on_dvr_hss_reap_flv uri failed, ignored. "
            "url=%s, request=%s, response=%s, ret=%d",
            url.c_str(), data.c_str(), res.c_str(), ret);
        return;
    }
    
    if (res.empty() || res != SRS_HTTP_RESPONSE_OK) {
        ret = ERROR_HTTP_DATA_INVLIAD;
        srs_warn("http hook on_dvr_hss_reap_flv validate failed, ignored. "
            "res=%s, ret=%d", res.c_str(), ret);
        return;
    }
    
    srs_info("http hook on_dvr_hss_reap_flv success. "
        "url=%s, request=%s, response=%s, ret=%d",
        url.c_str(), data.c_str(), res.c_str(), ret);
    
    return;
}

#endif

#endif
