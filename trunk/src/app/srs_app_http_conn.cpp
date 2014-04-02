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

#include <srs_app_http_conn.hpp>

#ifdef SRS_HTTP_SERVER

#include <sstream>
using namespace std;

#include <srs_kernel_log.hpp>
#include <srs_kernel_error.hpp>
#include <srs_app_socket.hpp>
#include <srs_app_http.hpp>
#include <srs_kernel_buffer.hpp>
#include <srs_core_autofree.hpp>

#define SRS_HTTP_HEADER_BUFFER        1024

SrsHttpConn::SrsHttpConn(SrsServer* srs_server, st_netfd_t client_stfd) 
    : SrsConnection(srs_server, client_stfd)
{
    parser = new SrsHttpParser();
}

SrsHttpConn::~SrsHttpConn()
{
    srs_freep(parser);
}

int SrsHttpConn::do_cycle()
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = get_peer_ip()) != ERROR_SUCCESS) {
        srs_error("get peer ip failed. ret=%d", ret);
        return ret;
    }
    srs_trace("http get peer ip success. ip=%s", ip);
    
    // initialize parser
    if ((ret = parser->initialize(HTTP_REQUEST)) != ERROR_SUCCESS) {
        srs_error("http initialize http parser failed. ret=%d", ret);
        return ret;
    }
    
    // underlayer socket
    SrsSocket skt(stfd);
    
    // process http messages.
    for (;;) {
        SrsHttpMessage* req = NULL;
        
        // get a http message
        if ((ret = parser->parse_message(&skt, &req)) != ERROR_SUCCESS) {
            return ret;
        }

        // if SUCCESS, always NOT-NULL and completed message.
        srs_assert(req);
        srs_assert(req->is_complete());
        
        // always free it in this scope.
        SrsAutoFree(SrsHttpMessage, req, false);
        
        // ok, handle http request.
        if ((ret = process_request(&skt, req)) != ERROR_SUCCESS) {
            return ret;
        }
    }
        
    return ret;
}

int SrsHttpConn::process_request(SrsSocket* skt, SrsHttpMessage* req) 
{
    int ret = ERROR_SUCCESS;
    
    if (req->method() == HTTP_OPTIONS) {
        char data[] = "HTTP/1.1 200 OK" __CRLF
            "Content-Length: 0"__CRLF
            "Server: SRS/"RTMP_SIG_SRS_VERSION""__CRLF
            "Allow: DELETE, GET, HEAD, OPTIONS, POST, PUT"__CRLF
            "Access-Control-Allow-Origin: *"__CRLF
            "Access-Control-Allow-Methods: GET, POST, HEAD, PUT, DELETE"__CRLF
            "Access-Control-Allow-Headers: Cache-Control,X-Proxy-Authorization,X-Requested-With,Content-Type"__CRLF
            "Content-Type: text/html;charset=utf-8"__CRLFCRLF
            "";
        return skt->write(data, sizeof(data), NULL);
    } else {
        std::string tilte = "SRS/"RTMP_SIG_SRS_VERSION;
        tilte += " hello http/1.1 server~\n";
        
        std::stringstream ss;
        ss << "HTTP/1.1 200 OK " << __CRLF
            << "Content-Length: "<< tilte.length() + req->body_size() << __CRLF
            << "Server: SRS/"RTMP_SIG_SRS_VERSION"" << __CRLF
            << "Allow: DELETE, GET, HEAD, OPTIONS, POST, PUT" << __CRLF
            << "Access-Control-Allow-Origin: *" << __CRLF
            << "Access-Control-Allow-Methods: GET, POST, HEAD, PUT, DELETE" << __CRLF
            << "Access-Control-Allow-Headers: Cache-Control,X-Proxy-Authorization,X-Requested-With,Content-Type" << __CRLF
            << "Content-Type: text/html;charset=utf-8" << __CRLFCRLF
            << tilte << req->body().c_str()
            << "";
        return skt->write(ss.str().c_str(), ss.str().length(), NULL);
    }
    
    return ret;
}

#endif
