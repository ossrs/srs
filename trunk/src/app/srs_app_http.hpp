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

#ifndef SRS_APP_HTTP_HPP
#define SRS_APP_HTTP_HPP

/*
#include <srs_app_http.hpp>
*/
#include <srs_core.hpp>

#ifdef SRS_HTTP_PARSER

#include <string>
#include <vector>
#include <sstream>

#include <http_parser.h>

#include <srs_app_st.hpp>

class SrsBuffer;
class SrsRequest;
class SrsSocket;
class SrsHttpMessage;

// http specification
// CR             = <US-ASCII CR, carriage return (13)>
#define __CR "\r" // 0x0D
// LF             = <US-ASCII LF, linefeed (10)>
#define __LF "\n" // 0x0A
// SP             = <US-ASCII SP, space (32)>
#define __SP " " // 0x20
// HT             = <US-ASCII HT, horizontal-tab (9)>
#define __HT "\x09" // 0x09

// HTTP/1.1 defines the sequence CR LF as the end-of-line marker for all
// protocol elements except the entity-body (see appendix 19.3 for
// tolerant applications). 
#define __CRLF "\r\n" // 0x0D0A
#define __CRLFCRLF "\r\n\r\n" // 0x0D0A0D0A

// linux path seprator
#define __PATH_SEP '/'

// state of message
enum SrsHttpParseState {
    SrsHttpParseStateInit = 0, 
    SrsHttpParseStateStart, 
    SrsHttpParseStateComplete
};

/**
* resource handler for HTTP RESTful api.
*/
class SrsHttpHandler
{
protected:
    /**
    * we use handler chain to process request.
    */
    std::vector<SrsHttpHandler*> handlers;
public:
    SrsHttpHandler();
    virtual ~SrsHttpHandler();
public:
    /**
    * initialize the handler.
    */
    virtual int initialize();
    /**
    * whether current handler can handle the specified path.
    * @pnext_path set the next path, if needed.
    */
    virtual bool can_handle(const char* path, int length, const char** pnext_path);
    /**
    * use the handler to process the request.
    */
    virtual int process_request(SrsSocket* skt, SrsHttpMessage* req, const char* path, int length);
public:
    /**
    * find the best matched handler
    */
    virtual int best_match(const char* path, int length, SrsHttpHandler** phandler, const char** pstart, int* plength);
public:
    virtual SrsHttpHandler* res_status_line(std::stringstream& ss);
    virtual SrsHttpHandler* res_content_type(std::stringstream& ss);
    virtual SrsHttpHandler* res_content_length(std::stringstream& ss, int64_t length);
    virtual SrsHttpHandler* res_enable_crossdomain(std::stringstream& ss);
    virtual SrsHttpHandler* res_header_eof(std::stringstream& ss);
    virtual SrsHttpHandler* res_body(std::stringstream& ss, std::string body);
    virtual int res_flush(SrsSocket* skt, std::stringstream& ss);
public:
    virtual int res_options(SrsSocket* skt);
    virtual int res_text(SrsSocket* skt, std::string body);
public:
    /**
    * create http api resource handler.
    */
    static SrsHttpHandler* create_http_api();
    /**
    * create http stream resource handler.
    */
    static SrsHttpHandler* create_http_stream();
};

/**
* the http message, request or response.
*/
class SrsHttpMessage
{
private:
    /**
    * parsed url.
    */
    std::string _url;
    /**
    * parsed http header.
    */
    http_parser _header;
    /**
    * body object, in bytes.
    * @remark, user can get body in string by get_body().
    */
    SrsBuffer* _body;
    /**
    * parser state
    * @remark, user can use is_complete() to determine the state.
    */
    SrsHttpParseState _state;
    
public:
    SrsHttpMessage();
    virtual ~SrsHttpMessage();
    
public:
    virtual void reset();
public:
    virtual bool is_complete();
    virtual u_int8_t method();
    virtual std::string url();
    virtual std::string body();
    virtual int64_t body_size();
    virtual int64_t content_length();
    virtual void set_url(std::string url);
    virtual void set_state(SrsHttpParseState state);
    virtual void set_header(http_parser* header);
    virtual void append_body(const char* body, int length);
};

/**
* wrapper for http-parser, 
* provides HTTP message originted service.
*/
class SrsHttpParser
{
private:
    http_parser_settings settings;
    http_parser parser;
    SrsHttpMessage* msg;
public:
    SrsHttpParser();
    virtual ~SrsHttpParser();
public:
    /**
    * initialize the http parser with specified type,
    * one parser can only parse request or response messages.
    */
    virtual int initialize(enum http_parser_type type);
    /**
    * always parse a http message,
    * that is, the *ppmsg always NOT-NULL when return success.
    * or error and *ppmsg must be NULL.
    * @remark, if success, *ppmsg always NOT-NULL, *ppmsg always is_complete().
    */
    virtual int parse_message(SrsSocket* skt, SrsHttpMessage** ppmsg);
private:
    /**
    * parse the HTTP message to member field: msg.
    */
    virtual int parse_message_imp(SrsSocket* skt);
private:
    static int on_message_begin(http_parser* parser);
    static int on_headers_complete(http_parser* parser);
    static int on_message_complete(http_parser* parser);
    static int on_url(http_parser* parser, const char* at, size_t length);
    static int on_header_field(http_parser* parser, const char* at, size_t length);
    static int on_header_value(http_parser* parser, const char* at, size_t length);
    static int on_body(http_parser* parser, const char* at, size_t length);
};

/**
* used to resolve the http uri.
*/
class SrsHttpUri
{
private:
    std::string url;
    std::string schema;
    std::string host;
    int port;
    std::string path;
public:
    SrsHttpUri();
    virtual ~SrsHttpUri();
public:
    /**
    * initialize the http uri.
    */
    virtual int initialize(std::string _url);
public:
    virtual const char* get_url();
    virtual const char* get_schema();
    virtual const char* get_host();
    virtual int get_port();
    virtual const char* get_path();
private:
    /**
    * get the parsed url field.
    * @return return empty string if not set.
    */
    virtual std::string get_uri_field(std::string uri, http_parser_url* hp_u, http_parser_url_fields field);
};

#endif

#endif