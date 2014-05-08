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

#include <arpa/inet.h>

#include <srs_kernel_error.hpp>
#include <srs_protocol_rtmp.hpp>
#include <srs_kernel_log.hpp>
#include <srs_app_socket.hpp>
#include <srs_app_http.hpp>
#include <srs_app_json.hpp>
#include <srs_app_dvr.hpp>
#include <srs_app_config.hpp>

#define SRS_HTTP_RESPONSE_OK "0"

#define SRS_HTTP_HEADER_BUFFER        1024
#define SRS_HTTP_BODY_BUFFER        32 * 1024

SrsHttpClient::SrsHttpClient()
{
    connected = false;
    stfd = NULL;
    parser = NULL;
}

SrsHttpClient::~SrsHttpClient()
{
    disconnect();
    srs_freep(parser);
}

int SrsHttpClient::post(SrsHttpUri* uri, string req, string& res)
{
    res = "";
    
    int ret = ERROR_SUCCESS;
    
    if (!parser) {
        parser = new SrsHttpParser();
        
        if ((ret = parser->initialize(HTTP_RESPONSE)) != ERROR_SUCCESS) {
            srs_error("initialize parser failed. ret=%d", ret);
            return ret;
        }
    }
    
    if ((ret = connect(uri)) != ERROR_SUCCESS) {
        srs_error("http connect server failed. ret=%d", ret);
        return ret;
    }
    
    // send POST request to uri
    // POST %s HTTP/1.1\r\nHost: %s\r\nContent-Length: %d\r\n\r\n%s
    std::stringstream ss;
    ss << "POST " << uri->get_path() << " "
        << "HTTP/1.1" << __CRLF
        << "Host: " << uri->get_host() << __CRLF
        << "Connection: Keep-Alive" << __CRLF
        << "Content-Length: " << std::dec << req.length() << __CRLF
        << "User-Agent: " << RTMP_SIG_SRS_NAME << RTMP_SIG_SRS_VERSION << __CRLF
        << "Content-Type: text/html" << __CRLF
        << __CRLF
        << req;
    
    SrsSocket skt(stfd);
    
    std::string data = ss.str();
    if ((ret = skt.write(data.c_str(), data.length(), NULL)) != ERROR_SUCCESS) {
        // disconnect when error.
        disconnect();
        
        srs_error("write http post failed. ret=%d", ret);
        return ret;
    }
    
    SrsHttpMessage* msg = NULL;
    if ((ret = parser->parse_message(&skt, &msg)) != ERROR_SUCCESS) {
        srs_error("parse http post response failed. ret=%d", ret);
        return ret;
    }

    srs_assert(msg);
    srs_assert(msg->is_complete());
    
    // get response body.
    if (msg->body_size() > 0) {
        res = msg->body();
    }
    srs_info("parse http post response success.");
    
    return ret;
}

void SrsHttpClient::disconnect()
{
    connected = false;
    
    srs_close_stfd(stfd);
}

int SrsHttpClient::connect(SrsHttpUri* uri)
{
    int ret = ERROR_SUCCESS;
    
    if (connected) {
        return ret;
    }
    
    disconnect();
    
    std::string ip = srs_dns_resolve(uri->get_host());
    if (ip.empty()) {
        ret = ERROR_SYSTEM_IP_INVALID;
        srs_error("dns resolve server error, ip empty. ret=%d", ret);
        return ret;
    }

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if(sock == -1){
        ret = ERROR_SOCKET_CREATE;
        srs_error("create socket error. ret=%d", ret);
        return ret;
    }
    
    stfd = st_netfd_open_socket(sock);
    if(stfd == NULL){
        ret = ERROR_ST_OPEN_SOCKET;
        srs_error("st_netfd_open_socket failed. ret=%d", ret);
        return ret;
    }
    
    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(uri->get_port());
    addr.sin_addr.s_addr = inet_addr(ip.c_str());
    
    if (st_connect(stfd, (const struct sockaddr*)&addr, sizeof(sockaddr_in), ST_UTIME_NO_TIMEOUT) == -1){
        ret = ERROR_ST_CONNECT;
        srs_error("connect to server error. "
            "ip=%s, port=%d, ret=%d", ip.c_str(), uri->get_port(), ret);
        return ret;
    }
    srs_info("connect to server success. "
        "http url=%s, server=%s, ip=%s, port=%d", 
        uri->get_url(), uri->get_host(), ip.c_str(), uri->get_port());
    
    connected = true;
    
    return ret;
}

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
