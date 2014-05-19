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

#include <srs_app_http_client.hpp>

#ifdef SRS_AUTO_HTTP_PARSER

#include <arpa/inet.h>

using namespace std;

#include <srs_app_http.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_app_socket.hpp>

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

#endif
