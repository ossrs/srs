/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2017 OSSRS(winlin)
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

#include <srs_app_http_client.hpp>

#include <arpa/inet.h>

using namespace std;

#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_app_st.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_app_utility.hpp>
#include <srs_core_autofree.hpp>
#include <srs_app_http_conn.hpp>
#include <srs_protocol_kbps.hpp>

SrsHttpClient::SrsHttpClient()
{
    transport = NULL;
    kbps = new SrsKbps();
    parser = NULL;
    timeout = SRS_CONSTS_NO_TMMS;
    port = 0;
}

SrsHttpClient::~SrsHttpClient()
{
    disconnect();
    
    srs_freep(kbps);
    srs_freep(parser);
}

// TODO: FIXME: use ms for timeout.
int SrsHttpClient::initialize(string h, int p, int64_t tm)
{
    int ret = ERROR_SUCCESS;
    
    srs_freep(parser);
    parser = new SrsHttpParser();
    
    if ((ret = parser->initialize(HTTP_RESPONSE, false)) != ERROR_SUCCESS) {
        srs_error("initialize parser failed. ret=%d", ret);
        return ret;
    }
    
    // Always disconnect the transport.
    host = h;
    port = p;
    timeout = tm;
    disconnect();
    
    // ep used for host in header.
    string ep = host;
    if (port > 0 && port != SRS_CONSTS_HTTP_DEFAULT_PORT) {
        ep += ":" + srs_int2str(port);
    }
    
    // Set default value for headers.
    headers["Host"] = ep;
    headers["Connection"] = "Keep-Alive";
    headers["User-Agent"] = RTMP_SIG_SRS_SERVER;
    headers["Content-Type"] = "application/json";
    
    return ret;
}

SrsHttpClient* SrsHttpClient::set_header(string k, string v)
{
    headers[k] = v;
    
    return this;
}

int SrsHttpClient::post(string path, string req, ISrsHttpMessage** ppmsg)
{
    *ppmsg = NULL;
    
    int ret = ERROR_SUCCESS;
    
    // always set the content length.
    headers["Content-Length"] = srs_int2str(req.length());
    
    if ((ret = connect()) != ERROR_SUCCESS) {
        srs_warn("http connect server failed. ret=%d", ret);
        return ret;
    }
    
    // send POST request to uri
    // POST %s HTTP/1.1\r\nHost: %s\r\nContent-Length: %d\r\n\r\n%s
    std::stringstream ss;
    ss << "POST " << path << " " << "HTTP/1.1" << SRS_HTTP_CRLF;
    for (map<string, string>::iterator it = headers.begin(); it != headers.end(); ++it) {
        string key = it->first;
        string value = it->second;
        ss << key << ": " << value << SRS_HTTP_CRLF;
    }
    ss << SRS_HTTP_CRLF << req;
    
    std::string data = ss.str();
    if ((ret = transport->write((void*)data.c_str(), data.length(), NULL)) != ERROR_SUCCESS) {
        // Disconnect the transport when channel error, reconnect for next operation.
        disconnect();
        srs_error("write http post failed. ret=%d", ret);
        return ret;
    }
    
    ISrsHttpMessage* msg = NULL;
    if ((ret = parser->parse_message(transport, NULL, &msg)) != ERROR_SUCCESS) {
        srs_error("parse http post response failed. ret=%d", ret);
        return ret;
    }
    srs_assert(msg);
    
    if (ppmsg) {
        *ppmsg = msg;
    } else {
        srs_freep(msg);
    }
    srs_info("parse http post response success.");
    
    return ret;
}

int SrsHttpClient::get(string path, string req, ISrsHttpMessage** ppmsg)
{
    *ppmsg = NULL;
    
    int ret = ERROR_SUCCESS;
    
    // always set the content length.
    headers["Content-Length"] = srs_int2str(req.length());
    
    if ((ret = connect()) != ERROR_SUCCESS) {
        srs_warn("http connect server failed. ret=%d", ret);
        return ret;
    }
    
    // send POST request to uri
    // GET %s HTTP/1.1\r\nHost: %s\r\nContent-Length: %d\r\n\r\n%s
    std::stringstream ss;
    ss << "GET " << path << " " << "HTTP/1.1" << SRS_HTTP_CRLF;
    for (map<string, string>::iterator it = headers.begin(); it != headers.end(); ++it) {
        string key = it->first;
        string value = it->second;
        ss << key << ": " << value << SRS_HTTP_CRLF;
    }
    ss << SRS_HTTP_CRLF << req;
    
    std::string data = ss.str();
    if ((ret = transport->write((void*)data.c_str(), data.length(), NULL)) != ERROR_SUCCESS) {
        // Disconnect the transport when channel error, reconnect for next operation.
        disconnect();
        srs_error("write http get failed. ret=%d", ret);
        return ret;
    }
    
    ISrsHttpMessage* msg = NULL;
    if ((ret = parser->parse_message(transport, NULL, &msg)) != ERROR_SUCCESS) {
        srs_error("parse http post response failed. ret=%d", ret);
        return ret;
    }
    srs_assert(msg);
    
    if (ppmsg) {
        *ppmsg = msg;
    } else {
        srs_freep(msg);
    }
    srs_info("parse http get response success.");
    
    return ret;
}

void SrsHttpClient::set_recv_timeout(int64_t tm)
{
    transport->set_recv_timeout(tm);
}

void SrsHttpClient::kbps_sample(const char* label, int64_t age)
{
    kbps->sample();
    
    int sr = kbps->get_send_kbps();
    int sr30s = kbps->get_send_kbps_30s();
    int sr5m = kbps->get_send_kbps_5m();
    int rr = kbps->get_recv_kbps();
    int rr30s = kbps->get_recv_kbps_30s();
    int rr5m = kbps->get_recv_kbps_5m();
    
    srs_trace("<- %s time=%"PRId64", okbps=%d,%d,%d, ikbps=%d,%d,%d", label, age, sr, sr30s, sr5m, rr, rr30s, rr5m);
}

void SrsHttpClient::disconnect()
{
    kbps->set_io(NULL, NULL);
    srs_freep(transport);
}

int SrsHttpClient::connect()
{
    int ret = ERROR_SUCCESS;
    
    // When transport connected, ignore.
    if (transport) {
        return ret;
    }
    
    transport = new SrsTcpClient(host, port, timeout);
    if ((ret = transport->connect()) != ERROR_SUCCESS) {
        disconnect();
        srs_warn("http client failed, server=%s, port=%d, timeout=%"PRId64", ret=%d", host.c_str(), port, timeout, ret);
        return ret;
    }
    srs_info("connect to server success. server=%s, port=%d", host.c_str(), port);
    
    // Set the recv/send timeout in ms.
    transport->set_recv_timeout(timeout);
    transport->set_send_timeout(timeout);
    
    kbps->set_io(transport, transport);
    
    return ret;
}

