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

#include <srs_app_http_client.hpp>

#ifdef SRS_AUTO_HTTP_CORE

#include <arpa/inet.h>

using namespace std;

#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_app_st.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_app_utility.hpp>
#include <srs_core_autofree.hpp>
#include <srs_app_http_conn.hpp>

SrsHttpClient::SrsHttpClient()
{
    connected = false;
    stfd = NULL;
    skt = NULL;
    parser = NULL;
    timeout_us = 0;
    port = 0;
}

SrsHttpClient::~SrsHttpClient()
{
    disconnect();
    srs_freep(parser);
}

int SrsHttpClient::initialize(string h, int p, int64_t t_us)
{
    int ret = ERROR_SUCCESS;
    
    // disconnect first when h:p changed.
    if ((!host.empty() && host != h) || (port != 0 && port != p)) {
        disconnect();
    }
    
    srs_freep(parser);
    parser = new SrsHttpParser();
    
    if ((ret = parser->initialize(HTTP_RESPONSE, false)) != ERROR_SUCCESS) {
        srs_error("initialize parser failed. ret=%d", ret);
        return ret;
    }
    
    host = h;
    port = p;
    timeout_us = t_us;
    
    return ret;
}

int SrsHttpClient::post(string path, string req, ISrsHttpMessage** ppmsg)
{
    *ppmsg = NULL;
    
    int ret = ERROR_SUCCESS;
    
    if ((ret = connect()) != ERROR_SUCCESS) {
        srs_warn("http connect server failed. ret=%d", ret);
        return ret;
    }
    
    // send POST request to uri
    // POST %s HTTP/1.1\r\nHost: %s\r\nContent-Length: %d\r\n\r\n%s
    std::stringstream ss;
    ss << "POST " << path << " "
        << "HTTP/1.1" << SRS_HTTP_CRLF
        << "Host: " << host << SRS_HTTP_CRLF
        << "Connection: Keep-Alive" << SRS_HTTP_CRLF
        << "Content-Length: " << std::dec << req.length() << SRS_HTTP_CRLF
        << "User-Agent: " << RTMP_SIG_SRS_NAME << RTMP_SIG_SRS_VERSION << SRS_HTTP_CRLF
        << "Content-Type: application/json" << SRS_HTTP_CRLF
        << SRS_HTTP_CRLF
        << req;
    
    std::string data = ss.str();
    if ((ret = skt->write((void*)data.c_str(), data.length(), NULL)) != ERROR_SUCCESS) {
        // disconnect when error.
        disconnect();
        
        srs_error("write http post failed. ret=%d", ret);
        return ret;
    }
    
    ISrsHttpMessage* msg = NULL;
    if ((ret = parser->parse_message(skt, NULL, &msg)) != ERROR_SUCCESS) {
        srs_error("parse http post response failed. ret=%d", ret);
        return ret;
    }

    srs_assert(msg);
    *ppmsg = msg;
    srs_info("parse http post response success.");
    
    return ret;
}

int SrsHttpClient::get(string path, std::string req, ISrsHttpMessage** ppmsg)
{
    *ppmsg = NULL;

    int ret = ERROR_SUCCESS;

    if ((ret = connect()) != ERROR_SUCCESS) {
        srs_warn("http connect server failed. ret=%d", ret);
        return ret;
    }

    // send POST request to uri
    // GET %s HTTP/1.1\r\nHost: %s\r\nContent-Length: %d\r\n\r\n%s
    std::stringstream ss;
    ss << "GET " << path << " "
        << "HTTP/1.1" << SRS_HTTP_CRLF
        << "Host: " << host << SRS_HTTP_CRLF
        << "Connection: Keep-Alive" << SRS_HTTP_CRLF
        << "Content-Length: " << std::dec << req.length() << SRS_HTTP_CRLF
        << "User-Agent: " << RTMP_SIG_SRS_NAME << RTMP_SIG_SRS_VERSION << SRS_HTTP_CRLF
        << "Content-Type: application/json" << SRS_HTTP_CRLF
        << SRS_HTTP_CRLF
        << req;

    std::string data = ss.str();
    if ((ret = skt->write((void*)data.c_str(), data.length(), NULL)) != ERROR_SUCCESS) {
        // disconnect when error.
        disconnect();

        srs_error("write http get failed. ret=%d", ret);
        return ret;
    }

    ISrsHttpMessage* msg = NULL;
    if ((ret = parser->parse_message(skt, NULL, &msg)) != ERROR_SUCCESS) {
        srs_error("parse http post response failed. ret=%d", ret);
        return ret;
    }
    srs_assert(msg);

    *ppmsg = msg;
    srs_info("parse http get response success.");

    return ret;
}

void SrsHttpClient::disconnect()
{
    connected = false;
    
    srs_close_stfd(stfd);
    srs_freep(skt);
}

int SrsHttpClient::connect()
{
    int ret = ERROR_SUCCESS;
    
    if (connected) {
        return ret;
    }
    
    disconnect();
    
    // open socket.
    if ((ret = srs_socket_connect(host, port, timeout_us, &stfd)) != ERROR_SUCCESS) {
        srs_warn("http client failed, server=%s, port=%d, timeout=%"PRId64", ret=%d",
            host.c_str(), port, timeout_us, ret);
        return ret;
    }
    srs_info("connect to server success. server=%s, port=%d", host.c_str(), port);
    
    srs_assert(!skt);
    skt = new SrsStSocket(stfd);
    connected = true;
    
    // set the recv/send timeout in us.
    skt->set_recv_timeout(timeout_us);
    skt->set_send_timeout(timeout_us);
    
    return ret;
}

#endif

