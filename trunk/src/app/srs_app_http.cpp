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
#include <srs_app_http_api.hpp>
#include <srs_app_http_conn.hpp>
#include <srs_app_json.hpp>

#define SRS_DEFAULT_HTTP_PORT 80

#define SRS_HTTP_HEADER_BUFFER 1024

bool srs_path_equals(const char* expect, const char* path, int nb_path)
{
    int size = strlen(expect);
    
    if (size != nb_path) {
        return false;
    }
    
    return !memcmp(expect, path, size);
}

SrsHttpHandlerMatch::SrsHttpHandlerMatch()
{
    handler = NULL;
}

SrsHttpHandler::SrsHttpHandler()
{
}

SrsHttpHandler::~SrsHttpHandler()
{
    std::vector<SrsHttpHandler*>::iterator it;
    for (it = handlers.begin(); it != handlers.end(); ++it) {
        SrsHttpHandler* handler = *it;
        srs_freep(handler);
    }
    handlers.clear();
}

int SrsHttpHandler::initialize()
{
    int ret = ERROR_SUCCESS;
    return ret;
}

bool SrsHttpHandler::can_handle(const char* /*path*/, int /*length*/, const char** /*pchild*/)
{
    return false;
}

int SrsHttpHandler::process_request(SrsSocket* skt, SrsHttpMessage* req)
{
    if (req->method() == HTTP_OPTIONS) {
        req->set_requires_crossdomain(true);
        return res_options(skt);
    }

    int status_code;
    std::string reason_phrase;
    if (!is_handler_valid(req, status_code, reason_phrase)) {
        std::stringstream ss;
        
        ss << JOBJECT_START
            << JFIELD_ERROR(ERROR_HTTP_HANDLER_INVALID) << JFIELD_CONT
            << JFIELD_ORG("data", JOBJECT_START)
                << JFIELD_ORG("status_code", status_code) << JFIELD_CONT
                << JFIELD_STR("reason_phrase", reason_phrase) << JFIELD_CONT
                << JFIELD_STR("url", req->url())
            << JOBJECT_END
            << JOBJECT_END;
        
        return res_error(skt, req, status_code, reason_phrase, ss.str());
    }
    
    return do_process_request(skt, req);
}

bool SrsHttpHandler::is_handler_valid(SrsHttpMessage* req, int& status_code, std::string& reason_phrase) 
{
    if (!req->match()->unmatched_url.empty()) {
        status_code = HTTP_NotFound;
        reason_phrase = HTTP_NotFound_str;
        
        return false;
    }
    
    return true;
}

int SrsHttpHandler::do_process_request(SrsSocket* /*skt*/, SrsHttpMessage* /*req*/)
{
    int ret = ERROR_SUCCESS;
    return ret;
}

int SrsHttpHandler::best_match(const char* path, int length, SrsHttpHandlerMatch** ppmatch)
{
    int ret = ERROR_SUCCESS;
    
    SrsHttpHandler* handler = NULL;
    const char* match_start = NULL;
    int match_length = 0;
    
    for (;;) {
        // ensure cur is not NULL.
        // ensure p not NULL and has bytes to parse.
        if (!path || length <= 0) {
            break;
        }
        
        const char* p = NULL;
        for (p = path + 1; p - path < length && *p != __PATH_SEP; p++) {
        }
        
        // whether the handler can handler the node.
        const char* pchild = p;
        if (!can_handle(path, p - path, &pchild)) {
            break;
        }
        
        // save current handler, it's ok for current handler atleast.
        handler = this;
        match_start = path;
        match_length = p - path;
        
        // find the best matched child handler.
        std::vector<SrsHttpHandler*>::iterator it;
        for (it = handlers.begin(); it != handlers.end(); ++it) {
            SrsHttpHandler* h = *it;
            
            // matched, donot search more.
            if (h->best_match(pchild, length - (pchild - path), ppmatch) == ERROR_SUCCESS) {
                break;
            }
        }
        
        // whatever, donot loop.
        break;
    }
    
    // if already matched by child, return.
    if (*ppmatch) {
        return ret;
    }
    
    // not matched, error.
    if (handler == NULL) {
        ret = ERROR_HTTP_HANDLER_MATCH_URL;
        return ret;
    }
    
    // matched by this handler.
    *ppmatch = new SrsHttpHandlerMatch();
    (*ppmatch)->handler = handler;
    (*ppmatch)->matched_url.append(match_start, match_length);
    
    int unmatch_length = length - match_length;
    if (unmatch_length > 0) {
        (*ppmatch)->unmatched_url.append(match_start + match_length, unmatch_length);
    }
    
    return ret;
}

SrsHttpHandler* SrsHttpHandler::res_status_line(std::stringstream& ss)
{
    ss << "HTTP/1.1 200 OK " << __CRLF
       << "Server: SRS/"RTMP_SIG_SRS_VERSION"" << __CRLF;
    return this;
}

SrsHttpHandler* SrsHttpHandler::res_status_line_error(std::stringstream& ss, int code, std::string reason_phrase)
{
    ss << "HTTP/1.1 " << code << " " << reason_phrase << __CRLF
       << "Server: SRS/"RTMP_SIG_SRS_VERSION"" << __CRLF;
    return this;
}

SrsHttpHandler* SrsHttpHandler::res_content_type(std::stringstream& ss)
{
    ss << "Content-Type: text/html;charset=utf-8" << __CRLF
        << "Allow: DELETE, GET, HEAD, OPTIONS, POST, PUT" << __CRLF;
    return this;
}

SrsHttpHandler* SrsHttpHandler::res_content_type_json(std::stringstream& ss)
{
    ss << "Content-Type: application/json;charset=utf-8" << __CRLF
        << "Allow: DELETE, GET, HEAD, OPTIONS, POST, PUT" << __CRLF;
    return this;
}

SrsHttpHandler* SrsHttpHandler::res_content_length(std::stringstream& ss, int64_t length)
{
    ss << "Content-Length: "<< length << __CRLF;
    return this;
}

SrsHttpHandler* SrsHttpHandler::res_enable_crossdomain(std::stringstream& ss)
{
    ss << "Access-Control-Allow-Origin: *" << __CRLF
        << "Access-Control-Allow-Methods: "
        << "GET, POST, HEAD, PUT, DELETE" << __CRLF
        << "Access-Control-Allow-Headers: "
        << "Cache-Control,X-Proxy-Authorization,X-Requested-With,Content-Type" << __CRLF;
    return this;
}

SrsHttpHandler* SrsHttpHandler::res_header_eof(std::stringstream& ss)
{
    ss << __CRLF;
    return this;
}

SrsHttpHandler* SrsHttpHandler::res_body(std::stringstream& ss, std::string body)
{
    ss << body;
    return this;
}

int SrsHttpHandler::res_flush(SrsSocket* skt, std::stringstream& ss)
{
    return skt->write(ss.str().c_str(), ss.str().length(), NULL);
}

int SrsHttpHandler::res_options(SrsSocket* skt)
{
    std::stringstream ss;
    
    res_status_line(ss)->res_content_type(ss)
        ->res_content_length(ss, 0)->res_enable_crossdomain(ss)
        ->res_header_eof(ss);
    
    return res_flush(skt, ss);
}

int SrsHttpHandler::res_text(SrsSocket* skt, SrsHttpMessage* req, std::string body)
{
    std::stringstream ss;
    
    res_status_line(ss)->res_content_type(ss)
        ->res_content_length(ss, (int)body.length());
        
    if (req->requires_crossdomain()) {
        res_enable_crossdomain(ss);
    }
    
    res_header_eof(ss)
        ->res_body(ss, body);
    
    return res_flush(skt, ss);
}

int SrsHttpHandler::res_json(SrsSocket* skt, SrsHttpMessage* req, std::string json)
{
    std::stringstream ss;
    
    res_status_line(ss)->res_content_type_json(ss)
        ->res_content_length(ss, (int)json.length());
        
    if (req->requires_crossdomain()) {
        res_enable_crossdomain(ss);
    }
    
    res_header_eof(ss)
        ->res_body(ss, json);
    
    return res_flush(skt, ss);
}

int SrsHttpHandler::res_error(SrsSocket* skt, SrsHttpMessage* req, int code, std::string reason_phrase, std::string body)
{
    std::stringstream ss;

    res_status_line_error(ss, code, reason_phrase)->res_content_type_json(ss)
        ->res_content_length(ss, (int)body.length());
        
    if (req->requires_crossdomain()) {
        res_enable_crossdomain(ss);
    }
    
    res_header_eof(ss)
        ->res_body(ss, body);
    
    return res_flush(skt, ss);
}

SrsHttpHandler* SrsHttpHandler::create_http_api()
{
    return new SrsApiRoot();
}

SrsHttpHandler* SrsHttpHandler::create_http_stream()
{
    // TODO: FIXME: use http stream handler instead.
    return new SrsHttpHandler();
}

SrsHttpMessage::SrsHttpMessage()
{
    _body = new SrsBuffer();
    _state = SrsHttpParseStateInit;
    _uri = new SrsHttpUri();
    _match = NULL;
    _requires_crossdomain = false;
}

SrsHttpMessage::~SrsHttpMessage()
{
    srs_freep(_body);
    srs_freep(_uri);
    srs_freep(_match);
}

void SrsHttpMessage::reset()
{
    _state = SrsHttpParseStateInit;
    _body->clear();
    _url = "";
}

int SrsHttpMessage::parse_uri()
{
    // filter url according to HTTP specification.
    
    // remove the duplicated slash.
    std::string filtered_url = srs_string_replace(_url, "//", "/");
    
    // remove the last / to match resource.
    filtered_url = srs_string_trim_end(filtered_url, "/");
    
    // if empty, use root.
    if (filtered_url.empty()) {
        filtered_url = "/";
    }
    
    return _uri->initialize(filtered_url);
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
    return _uri->get_url();
}

string SrsHttpMessage::path()
{
    return _uri->get_path();
}

string SrsHttpMessage::query()
{
    return _uri->get_query();
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

SrsHttpHandlerMatch* SrsHttpMessage::match()
{
    return _match;
}

bool SrsHttpMessage::requires_crossdomain()
{
    return _requires_crossdomain;
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

void SrsHttpMessage::set_match(SrsHttpHandlerMatch* match)
{
    srs_freep(_match);
    _match = match;
}

void SrsHttpMessage::set_requires_crossdomain(bool requires_crossdomain)
{
    _requires_crossdomain = requires_crossdomain;
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
    
    query = get_uri_field(url, &hp_u, UF_QUERY);
    srs_info("parse query %s success", query.c_str());
    
    return ret;
}

const char* SrsHttpUri::get_url()
{
    return url.data();
}

const char* SrsHttpUri::get_schema()
{
    return schema.data();
}

const char* SrsHttpUri::get_host()
{
    return host.data();
}

int SrsHttpUri::get_port()
{
    return port;
}

const char* SrsHttpUri::get_path()
{
    return path.data();
}

const char* SrsHttpUri::get_query()
{
    return path.data();
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
