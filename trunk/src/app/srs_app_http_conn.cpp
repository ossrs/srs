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

#include <srs_kernel_log.hpp>
#include <srs_kernel_error.hpp>
#include <srs_app_socket.hpp>

#define SRS_HTTP_HEADER_BUFFER        1024

SrsHttpConn::SrsHttpConn(SrsServer* srs_server, st_netfd_t client_stfd) 
    : SrsConnection(srs_server, client_stfd)
{
}

SrsHttpConn::~SrsHttpConn() 
{
}

int SrsHttpConn::do_cycle() 
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = get_peer_ip()) != ERROR_SUCCESS) {
        srs_error("get peer ip failed. ret=%d", ret);
        return ret;
    }
    srs_trace("http get peer ip success. ip=%s", ip);

    // setup http parser
    http_parser_settings settings;
    
    memset(&settings, 0, sizeof(settings));
    settings.on_message_begin = on_message_begin;
    settings.on_url = on_url;
    settings.on_header_field = on_header_field;
    settings.on_header_value = on_header_value;
    settings.on_headers_complete = on_headers_complete;
    settings.on_body = on_body;
    settings.on_message_complete = on_message_complete;
    
    http_parser parser;
    http_parser_init(&parser, HTTP_REQUEST);
    // callback object ptr.
    parser.data = (void*)this;
    
    // underlayer socket
    SrsSocket skt(stfd);
    
    for (;;) {
        if ((ret = process_request(&skt, &parser, &settings)) != ERROR_SUCCESS) {
            if (!srs_is_client_gracefully_close(ret)) {
                srs_error("http client cycle failed. ret=%d", ret);
            }
            return ret;
        }
    }
        
    return ret;
}

int SrsHttpConn::process_request(SrsSocket* skt, http_parser* parser, http_parser_settings* settings)
{
    int ret = ERROR_SUCCESS;

    // reset response header.
    http_header = NULL;
    
    // parser header.
    char buf[SRS_HTTP_HEADER_BUFFER];
    for (;;) {
        ssize_t nread;
        if ((ret = skt->read(buf, (size_t)sizeof(buf), &nread)) != ERROR_SUCCESS) {
            if (!srs_is_client_gracefully_close(ret)) {
                srs_error("read body from server failed. ret=%d", ret);
            }
            return ret;
        }
        
        ssize_t nparsed = http_parser_execute(parser, settings, buf, nread);
        srs_info("read_size=%d, nparsed=%d", (int)nread, (int)nparsed);

        // check header size.
        if (http_header) {
            int nb_body = nread - nparsed;
            
            srs_info("http header parsed, size=%d, content-length=%"PRId64", nb_body=%d", 
                http_header->nread, http_header->content_length, nb_body);
            
            return complete_header(skt, http_header, buf + nparsed, nb_body);
        }
        
        if (nparsed != nread) {
            ret = ERROR_HTTP_PARSE_HEADER;
            srs_error("parse response error, parsed(%d)!=read(%d), ret=%d", (int)nparsed, (int)nread, ret);
            return ret;
        }
    }
    
    return ret;
}

int SrsHttpConn::complete_header(SrsSocket* skt, http_parser* header, char* body, int nb_body) 
{
    int ret = ERROR_SUCCESS;
    
    char data[] = "HTTP/1.1 200 OK\r\n"
        "Server: SRS/"RTMP_SIG_SRS_VERSION"\r\n"
        "Content-Length: 15\r\n"
        "Content-Type: text/html;charset=utf-8\r\n\r\n"
        "hello http/1.1~";
        
    skt->write(data, sizeof(data), NULL);
    
    return ret;
}

int SrsHttpConn::on_message_begin(http_parser* parser)
{
    SrsHttpConn* obj = (SrsHttpConn*)parser->data;
    (void)obj;
    srs_trace("***MESSAGE BEGIN***");
    return 0;
}

int SrsHttpConn::on_headers_complete(http_parser* parser)
{
    SrsHttpConn* obj = (SrsHttpConn*)parser->data;
    (void)obj;
    srs_trace("***HEADERS COMPLETE***");
    
    // see http_parser.c:1570, return 1 to skip body.
    return 0;
}

int SrsHttpConn::on_message_complete(http_parser* parser)
{
    SrsHttpConn* obj = (SrsHttpConn*)parser->data;
    srs_trace("***MESSAGE COMPLETE***\n");
    
    // save the parser when header parse completed.
    obj->http_header = parser;
    return 0;
}

int SrsHttpConn::on_url(http_parser* parser, const char* at, size_t length)
{
    SrsHttpConn* obj = (SrsHttpConn*)parser->data;
    (void)obj;
    srs_trace("Method: %d, Url: %.*s", parser->method, (int)length, at);
    return 0;
}

int SrsHttpConn::on_header_field(http_parser* parser, const char* at, size_t length)
{
    SrsHttpConn* obj = (SrsHttpConn*)parser->data;
    (void)obj;
    srs_trace("Header field: %.*s", (int)length, at);
    return 0;
}

int SrsHttpConn::on_header_value(http_parser* parser, const char* at, size_t length)
{
    SrsHttpConn* obj = (SrsHttpConn*)parser->data;
    (void)obj;
    srs_trace("Header value: %.*s", (int)length, at);
    return 0;
}

int SrsHttpConn::on_body(http_parser* parser, const char* at, size_t length)
{
    SrsHttpConn* obj = (SrsHttpConn*)parser->data;
    (void)obj;
    srs_trace("Body: %.*s", (int)length, at);
    return 0;
}
