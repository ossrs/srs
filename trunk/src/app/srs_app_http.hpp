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

#include <http_parser.h>

#include <srs_app_st.hpp>

class SrsBuffer;
class SrsRequest;
class SrsSocket;

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

enum SrsHttpParseState {
    SrsHttpParseStateInit = 0, 
    SrsHttpParseStateStart, 
    SrsHttpParseStateComplete
};

/**
* the http message, request or response.
*/
class SrsHttpMessage
{
public:
    std::string url;
    http_parser header;
    SrsBuffer* body;
    SrsHttpParseState state;
    
    SrsHttpMessage();
    virtual ~SrsHttpMessage();
    
    virtual void reset();
    virtual bool is_complete();
};

/**
* wrapper for http-parser, 
* provides HTTP message originted service.
*/
class SrsHttpParser
{
public:
    SrsHttpParser();
    virtual ~SrsHttpParser();
public:
    //virtual int parse_requst(SrsHttpMessage** ppreq);
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