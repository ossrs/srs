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

#include <srs_app_http.hpp>

#ifdef SRS_HTTP_PARSER

#include <stdlib.h>

using namespace std;

#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_app_socket.hpp>

#define SRS_DEFAULT_HTTP_PORT 80

#define SRS_HTTP_HEADER_BUFFER        1024

SrsHttpMessage::SrsHttpMessage()
{
    _body = new SrsBuffer();
    _state = SrsHttpParseStateInit;
}

SrsHttpMessage::~SrsHttpMessage()
{
    srs_freep(_body);
}

void SrsHttpMessage::reset()
{
    _state = SrsHttpParseStateInit;
    _body->clear();
    _url = "";
}

bool SrsHttpMessage::is_complete()
{
    return _state == SrsHttpParseStateComplete;
}

u_int8_t SrsHttpMessage::method()
{
    return (u_int8_t)_header.method;
}

string SrsHttpMessage::url()
{
    return _url;
}

string SrsHttpMessage::body()
{
    std::string b;
    
    if (_body && !_body->empty()) {
        b.append(_body->bytes(), _body->size());
    }
    
    return b;
}

int64_t SrsHttpMessage::body_size()
{
    return (int64_t)_body->size();
}

int64_t SrsHttpMessage::content_length()
{
    return _header.content_length;
}

void SrsHttpMessage::set_url(std::string url)
{
    _url = url;
}

void SrsHttpMessage::set_state(SrsHttpParseState state)
{
    _state = state;
}

void SrsHttpMessage::set_header(http_parser* header)
{
    memcpy(&_header, header, sizeof(http_parser));
}

void SrsHttpMessage::append_body(const char* body, int length)
{
    _body->append(body, length);
}

SrsHttpParser::SrsHttpParser()
{
    msg = NULL;
}

SrsHttpParser::~SrsHttpParser()
{
    srs_freep(msg);
}

int SrsHttpParser::initialize(enum http_parser_type type)
{
    int ret = ERROR_SUCCESS;
    
    memset(&settings, 0, sizeof(settings));
    settings.on_message_begin = on_message_begin;
    settings.on_url = on_url;
    settings.on_header_field = on_header_field;
    settings.on_header_value = on_header_value;
    settings.on_headers_complete = on_headers_complete;
    settings.on_body = on_body;
    settings.on_message_complete = on_message_complete;
    
    http_parser_init(&parser, type);
    // callback object ptr.
    parser.data = (void*)this;
    
    return ret;
}

int SrsHttpParser::parse_message(SrsSocket* skt, SrsHttpMessage** ppmsg)
{
    *ppmsg = NULL;
    
    int ret = ERROR_SUCCESS;

    // the msg must be always NULL
    srs_assert(msg == NULL);
    msg = new SrsHttpMessage();
    
    // reset response header.
    msg->reset();
    
    // do parse
    if ((ret = parse_message_imp(skt)) != ERROR_SUCCESS) {
        if (!srs_is_client_gracefully_close(ret)) {
            srs_error("parse http msg failed. ret=%d", ret);
        }
        srs_freep(msg);
        return ret;
    }
    
    // parse ok, return the msg.
    *ppmsg = msg;
    msg = NULL;
    
    return ret;
}

int SrsHttpParser::parse_message_imp(SrsSocket* skt)
{
    int ret = ERROR_SUCCESS;
    
    // the msg should never be NULL
    srs_assert(msg != NULL);
    
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
        
        ssize_t nparsed = http_parser_execute(&parser, &settings, buf, nread);
        srs_info("read_size=%d, nparsed=%d", (int)nread, (int)nparsed);

        // check header size.
        if (msg->is_complete()) {
            srs_trace("http request parsed, method=%d, url=%s, content-length=%"PRId64"", 
                msg->method(), msg->url().c_str(), msg->content_length());
            
            return ret;
        }
        
        if (nparsed != nread) {
            ret = ERROR_HTTP_PARSE_HEADER;
            srs_error("parse response error, parsed(%d)!=read(%d), ret=%d", (int)nparsed, (int)nread, ret);
            return ret;
        }
    }

    return ret;
}

int SrsHttpParser::on_message_begin(http_parser* parser)
{
    SrsHttpParser* obj = (SrsHttpParser*)parser->data;
    obj->msg->set_state(SrsHttpParseStateStart);
    
    srs_info("***MESSAGE BEGIN***");
    
    return 0;
}

int SrsHttpParser::on_headers_complete(http_parser* parser)
{
    SrsHttpParser* obj = (SrsHttpParser*)parser->data;
    obj->msg->set_header(parser);
    
    srs_info("***HEADERS COMPLETE***");
    
    // see http_parser.c:1570, return 1 to skip body.
    return 0;
}

int SrsHttpParser::on_message_complete(http_parser* parser)
{
    SrsHttpParser* obj = (SrsHttpParser*)parser->data;
    // save the parser when header parse completed.
    obj->msg->set_state(SrsHttpParseStateComplete);
    
    srs_info("***MESSAGE COMPLETE***\n");
    
    return 0;
}

int SrsHttpParser::on_url(http_parser* parser, const char* at, size_t length)
{
    SrsHttpParser* obj = (SrsHttpParser*)parser->data;
    
    if (length > 0) {
        std::string url;
        
        url.append(at, (int)length);
        
        obj->msg->set_url(url);
    }
    
    srs_info("Method: %d, Url: %.*s", parser->method, (int)length, at);
    
    return 0;
}

int SrsHttpParser::on_header_field(http_parser* /*parser*/, const char* at, size_t length)
{
    srs_info("Header field: %.*s", (int)length, at);
    return 0;
}

int SrsHttpParser::on_header_value(http_parser* /*parser*/, const char* at, size_t length)
{
    srs_info("Header value: %.*s", (int)length, at);
    return 0;
}

int SrsHttpParser::on_body(http_parser* parser, const char* at, size_t length)
{
    SrsHttpParser* obj = (SrsHttpParser*)parser->data;
    
    if (length > 0) {
        srs_assert(obj);
        srs_assert(obj->msg);
        
        obj->msg->append_body(at, (int)length);
    }
    
    srs_info("Body: %.*s", (int)length, at);

    return 0;
}

SrsHttpUri::SrsHttpUri()
{
    port = SRS_DEFAULT_HTTP_PORT;
}

SrsHttpUri::~SrsHttpUri()
{
}

int SrsHttpUri::initialize(std::string _url)
{
    int ret = ERROR_SUCCESS;
    
    url = _url;
    const char* purl = url.c_str();
    
    http_parser_url hp_u;
    if((ret = http_parser_parse_url(purl, url.length(), 0, &hp_u)) != 0){
        int code = ret;
        ret = ERROR_HTTP_PARSE_URI;
        
        srs_error("parse url %s failed, code=%d, ret=%d", purl, code, ret);
        return ret;
    }
    
    std::string field = get_uri_field(url, &hp_u, UF_SCHEMA);
    if(!field.empty()){
        schema = field;
    }
    
    host = get_uri_field(url, &hp_u, UF_HOST);
    
    field = get_uri_field(url, &hp_u, UF_PORT);
    if(!field.empty()){
        port = atoi(field.c_str());
    }
    
    path = get_uri_field(url, &hp_u, UF_PATH);
    srs_info("parse url %s success", purl);
    
    return ret;
}

const char* SrsHttpUri::get_url()
{
    return url.c_str();
}

const char* SrsHttpUri::get_schema()
{
    return schema.c_str();
}

const char* SrsHttpUri::get_host()
{
    return host.c_str();
}

int SrsHttpUri::get_port()
{
    return port;
}

const char* SrsHttpUri::get_path()
{
    return path.c_str();
}

std::string SrsHttpUri::get_uri_field(std::string uri, http_parser_url* hp_u, http_parser_url_fields field)
{
    if((hp_u->field_set & (1 << field)) == 0){
        return "";
    }
    
    srs_verbose("uri field matched, off=%d, len=%d, value=%.*s", 
        hp_u->field_data[field].off, 
        hp_u->field_data[field].len, 
        hp_u->field_data[field].len, 
        uri.c_str() + hp_u->field_data[field].off);
    
    int offset = hp_u->field_data[field].off;
    int len = hp_u->field_data[field].len;
    
    return uri.substr(offset, len);
}

#endif
