/*
The MIT License (MIT)

Copyright (c) 2013-2015 winlin

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

#ifdef SRS_AUTO_HTTP_PARSER

#include <map>
#include <string>
#include <vector>
#include <sstream>

#include <http_parser.h>

#include <srs_app_st.hpp>

class SrsSimpleBuffer;
class SrsRequest;
class SrsStSocket;
class SrsHttpUri;
class SrsHttpMessage;
class SrsHttpHandler;
class ISrsGoHttpResponseWriter;

// http specification
// CR             = <US-ASCII CR, carriage return (13)>
#define __SRS_CR "\r" // 0x0D
// LF             = <US-ASCII LF, linefeed (10)>
#define __SRS_LF "\n" // 0x0A
// SP             = <US-ASCII SP, space (32)>
#define __SRS_SP " " // 0x20
// HT             = <US-ASCII HT, horizontal-tab (9)>
#define __SRS_HT "\x09" // 0x09

// HTTP/1.1 defines the sequence CR LF as the end-of-line marker for all
// protocol elements except the entity-body (see appendix 19.3 for
// tolerant applications). 
#define __SRS_CRLF "\r\n" // 0x0D0A
#define __SRS_CRLFCRLF "\r\n\r\n" // 0x0D0A0D0A

// @see SrsHttpMessage._http_ts_send_buffer
#define __SRS_HTTP_TS_SEND_BUFFER_SIZE 4096

// helper function: response in json format.
extern int srs_go_http_response_json(ISrsGoHttpResponseWriter* w, std::string data);

// compare the path.
// full compare, extractly match.
// used for api match.
extern bool srs_path_equals(const char* expect, const char* path, int nb_path);
// compare the path use like,
// used for http stream to match,
// if the path like the requires
extern bool srs_path_like(const char* expect, const char* path, int nb_path);

// state of message
enum SrsHttpParseState {
    SrsHttpParseStateInit = 0, 
    SrsHttpParseStateStart, 
    SrsHttpParseStateComplete
};

// A Header represents the key-value pairs in an HTTP header.
class SrsGoHttpHeader
{
private:
    std::map<std::string, std::string> headers;
public:
    SrsGoHttpHeader();
    virtual ~SrsGoHttpHeader();
public:
    // Add adds the key, value pair to the header.
    // It appends to any existing values associated with key.
    virtual void set(std::string key, std::string value);
    // Get gets the first value associated with the given key.
    // If there are no values associated with the key, Get returns "".
    // To access multiple values of a key, access the map directly
    // with CanonicalHeaderKey.
    virtual std::string get(std::string key);
public:
    /**
    * get the content length. -1 if not set.
    */
    virtual int64_t content_length();
    /**
    * set the content length by header "Content-Length"
    */
    virtual void set_content_length(int64_t size);
public:
    /**
    * get the content type. empty string if not set.
    */
    virtual std::string content_type();
    /**
    * set the content type by header "Content-Type"
    */
    virtual void set_content_type(std::string ct);
public:
    /**
    * write all headers to string stream.
    */
    virtual void write(std::stringstream& ss);
};

// A ResponseWriter interface is used by an HTTP handler to
// construct an HTTP response.
class ISrsGoHttpResponseWriter
{
public:
    ISrsGoHttpResponseWriter();
    virtual ~ISrsGoHttpResponseWriter();
public:
    // Header returns the header map that will be sent by WriteHeader.
    // Changing the header after a call to WriteHeader (or Write) has
    // no effect.
    virtual SrsGoHttpHeader* header() = 0;
    
    // Write writes the data to the connection as part of an HTTP reply.
    // If WriteHeader has not yet been called, Write calls WriteHeader(http.StatusOK)
    // before writing the data.  If the Header does not contain a
    // Content-Type line, Write adds a Content-Type set to the result of passing
    // the initial 512 bytes of written data to DetectContentType.
    virtual int write(char* data, int size) = 0;
    
    // WriteHeader sends an HTTP response header with status code.
    // If WriteHeader is not called explicitly, the first call to Write
    // will trigger an implicit WriteHeader(http.StatusOK).
    // Thus explicit calls to WriteHeader are mainly used to
    // send error codes.
    virtual void write_header(int code) = 0;
};

// Objects implementing the Handler interface can be
// registered to serve a particular path or subtree
// in the HTTP server.
//
// ServeHTTP should write reply headers and data to the ResponseWriter
// and then return.  Returning signals that the request is finished
// and that the HTTP server can move on to the next request on
// the connection.
class ISrsGoHttpHandler
{
public:
    ISrsGoHttpHandler();
    virtual ~ISrsGoHttpHandler();
public:
    virtual int serve_http(ISrsGoHttpResponseWriter* w, SrsHttpMessage* r) = 0;
};

// Redirect to a fixed URL
class SrsGoHttpRedirectHandler : public ISrsGoHttpHandler
{
private:
    std::string url;
    int code;
public:
    SrsGoHttpRedirectHandler(std::string u, int c);
    virtual ~SrsGoHttpRedirectHandler();
public:
    virtual int serve_http(ISrsGoHttpResponseWriter* w, SrsHttpMessage* r);
};

// NotFound replies to the request with an HTTP 404 not found error.
class SrsGoHttpNotFoundHandler : public ISrsGoHttpHandler
{
public:
    SrsGoHttpNotFoundHandler();
    virtual ~SrsGoHttpNotFoundHandler();
public:
    virtual int serve_http(ISrsGoHttpResponseWriter* w, SrsHttpMessage* r);
};

// the mux entry for server mux.
class SrsGoHttpMuxEntry
{
public:
    bool explicit_match;
    ISrsGoHttpHandler* handler;
    std::string pattern;
public:
    SrsGoHttpMuxEntry();
    virtual ~SrsGoHttpMuxEntry();
};

// ServeMux is an HTTP request multiplexer.
// It matches the URL of each incoming request against a list of registered
// patterns and calls the handler for the pattern that
// most closely matches the URL.
//
// Patterns name fixed, rooted paths, like "/favicon.ico",
// or rooted subtrees, like "/images/" (note the trailing slash).
// Longer patterns take precedence over shorter ones, so that
// if there are handlers registered for both "/images/"
// and "/images/thumbnails/", the latter handler will be
// called for paths beginning "/images/thumbnails/" and the
// former will receive requests for any other paths in the
// "/images/" subtree.
//
// Note that since a pattern ending in a slash names a rooted subtree,
// the pattern "/" matches all paths not matched by other registered
// patterns, not just the URL with Path == "/".
//
// Patterns may optionally begin with a host name, restricting matches to
// URLs on that host only.  Host-specific patterns take precedence over
// general patterns, so that a handler might register for the two patterns
// "/codesearch" and "codesearch.google.com/" without also taking over
// requests for "http://www.google.com/".
//
// ServeMux also takes care of sanitizing the URL request path,
// redirecting any request containing . or .. elements to an
// equivalent .- and ..-free URL.
class SrsGoHttpServeMux
{
private:
    std::map<std::string, SrsGoHttpMuxEntry*> entries;
public:
    SrsGoHttpServeMux();
    virtual ~SrsGoHttpServeMux();
public:
    /**
    * initialize the http serve mux.
    */
    virtual int initialize();
public:
    // Handle registers the handler for the given pattern.
    // If a handler already exists for pattern, Handle panics.
    virtual int handle(std::string pattern, ISrsGoHttpHandler* handler);
// interface ISrsGoHttpHandler
public:
    virtual int serve_http(ISrsGoHttpResponseWriter* w, SrsHttpMessage* r);
private:
    virtual int find_handler(SrsHttpMessage* r, ISrsGoHttpHandler** ph);
    virtual int match(SrsHttpMessage* r, ISrsGoHttpHandler** ph);
    virtual bool path_match(std::string pattern, std::string path);
};

/**
* response writer use st socket
*/
class SrsGoHttpResponseWriter : public ISrsGoHttpResponseWriter
{
private:
    SrsStSocket* skt;
    SrsGoHttpHeader* hdr;
private:
    // reply header has been (logically) written
    bool header_wrote;
     // status code passed to WriteHeader
    int status;
private:
    // explicitly-declared Content-Length; or -1
    int64_t content_length;
    // number of bytes written in body
    int64_t written;
private:
    // wroteHeader tells whether the header's been written to "the
    // wire" (or rather: w.conn.buf). this is unlike
    // (*response).wroteHeader, which tells only whether it was
    // logically written.
    bool header_sent;
public:
    SrsGoHttpResponseWriter(SrsStSocket* io);
    virtual ~SrsGoHttpResponseWriter();
public:
    virtual SrsGoHttpHeader* header();
    virtual int write(char* data, int size);
    virtual void write_header(int code);
    virtual int send_header(char* data, int size);
};

/**
* the matched handler info.
*/
class SrsHttpHandlerMatch
{
public:
    SrsHttpHandler* handler;
    std::string matched_url;
    std::string unmatched_url;
public:
    SrsHttpHandlerMatch();
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
    * @pchild set the next child path, if needed.
    *       for example, the root handler will reset pchild to path,
    *       to reparse the path use child handlers.
    */
    virtual bool can_handle(const char* path, int length, const char** pchild);
    /**
    * use the handler to process the request.
    * @remark sub classes should override the do_process_request.
    */
    virtual int process_request(SrsStSocket* skt, SrsHttpMessage* req);
public:
    /**
    * find the best matched handler
    */
    virtual int best_match(const char* path, int length, SrsHttpHandlerMatch** ppmatch);
// factory methods
protected:
    /**
    * check whether the handler is valid.
    * for example, user access /apis, actually it's not found,
    * we will find the root handler to process it.
    * @remark user can override this method, and should invoke it first.
    * @see SrsApiRoot::is_handler_valid
    */
    virtual bool is_handler_valid(SrsHttpMessage* req, int& status_code, std::string& reason_phrase);
    /**
    * do the actual process of request., format as, for example:
    * {"code":0, "data":{}}
    */
    virtual int do_process_request(SrsStSocket* skt, SrsHttpMessage* req);
    /**
    * response error, format as, for example:
    * {"code":100, "desc":"description"}
    */
    virtual int response_error(SrsStSocket* skt, SrsHttpMessage* req, int code, std::string desc);
// response writer
public:
    virtual SrsHttpHandler* res_status_line(std::stringstream& ss);
    virtual SrsHttpHandler* res_status_line_error(std::stringstream& ss, int code, std::string reason_phrase);
    virtual SrsHttpHandler* res_content_type(std::stringstream& ss);
    virtual SrsHttpHandler* res_content_type_xml(std::stringstream& ss);
    virtual SrsHttpHandler* res_content_type_javascript(std::stringstream& ss);
    virtual SrsHttpHandler* res_content_type_swf(std::stringstream& ss);
    virtual SrsHttpHandler* res_content_type_css(std::stringstream& ss);
    virtual SrsHttpHandler* res_content_type_ico(std::stringstream& ss);
    virtual SrsHttpHandler* res_content_type_json(std::stringstream& ss);
    virtual SrsHttpHandler* res_content_type_m3u8(std::stringstream& ss);
    virtual SrsHttpHandler* res_content_type_mpegts(std::stringstream& ss);
    virtual SrsHttpHandler* res_content_type_flv(std::stringstream& ss);
    virtual SrsHttpHandler* res_content_length(std::stringstream& ss, int64_t length);
    virtual SrsHttpHandler* res_enable_crossdomain(std::stringstream& ss);
    virtual SrsHttpHandler* res_header_eof(std::stringstream& ss);
    virtual SrsHttpHandler* res_body(std::stringstream& ss, std::string body);
    virtual int res_flush(SrsStSocket* skt, std::stringstream& ss);
public:
    virtual int res_options(SrsStSocket* skt);
    virtual int res_text(SrsStSocket* skt, SrsHttpMessage* req, std::string body);
    virtual int res_xml(SrsStSocket* skt, SrsHttpMessage* req, std::string body);
    virtual int res_javascript(SrsStSocket* skt, SrsHttpMessage* req, std::string body);
    virtual int res_swf(SrsStSocket* skt, SrsHttpMessage* req, std::string body);
    virtual int res_css(SrsStSocket* skt, SrsHttpMessage* req, std::string body);
    virtual int res_ico(SrsStSocket* skt, SrsHttpMessage* req, std::string body);
    virtual int res_m3u8(SrsStSocket* skt, SrsHttpMessage* req, std::string body);
    virtual int res_mpegts(SrsStSocket* skt, SrsHttpMessage* req, std::string body);
    virtual int res_json(SrsStSocket* skt, SrsHttpMessage* req, std::string json);
    virtual int res_error(SrsStSocket* skt, SrsHttpMessage* req, int code, std::string reason_phrase, std::string body);
// object creator
public:
    /**
    * create http stream resource handler.
    */
#ifdef SRS_AUTO_HTTP_SERVER
    static SrsHttpHandler* create_http_stream();
#endif
};

// A Request represents an HTTP request received by a server
// or to be sent by a client.
//
// The field semantics differ slightly between client and server
// usage. In addition to the notes on the fields below, see the
// documentation for Request.Write and RoundTripper.
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
    SrsSimpleBuffer* _body;
    /**
    * parser state
    * @remark, user can use is_complete() to determine the state.
    */
    SrsHttpParseState _state;
    /**
    * uri parser
    */
    SrsHttpUri* _uri;
    /**
    * best matched handler.
    */
    SrsHttpHandlerMatch* _match;
    /**
    * whether the message requires crossdomain.
    */
    bool _requires_crossdomain;
    /**
    * use a buffer to read and send ts file.
    */
    char* _http_ts_send_buffer;
    // http headers
    typedef std::pair<std::string, std::string> SrsHttpHeaderField;
    std::vector<SrsHttpHeaderField> headers;
    // the query map
    std::map<std::string, std::string> _query;
public:
    SrsHttpMessage();
    virtual ~SrsHttpMessage();
public:
    virtual int initialize();
public:
    virtual char* http_ts_send_buffer();
    virtual void reset();
public:
    virtual bool is_complete();
    virtual u_int8_t method();
    virtual u_int16_t status_code();
    virtual std::string method_str();
    virtual bool is_http_get();
    virtual bool is_http_put();
    virtual bool is_http_post();
    virtual bool is_http_delete();
    virtual bool is_http_options();
    virtual std::string uri();
    virtual std::string url();
    virtual std::string host();
    virtual std::string path();
public:
    virtual std::string body();
    virtual char* body_raw();
    virtual int64_t body_size();
    virtual int64_t content_length();
    virtual SrsHttpHandlerMatch* match();
    virtual bool requires_crossdomain();
    virtual void set_url(std::string url);
    virtual void set_state(SrsHttpParseState state);
    virtual void set_header(http_parser* header);
    virtual void set_match(SrsHttpHandlerMatch* match);
    virtual void set_requires_crossdomain(bool requires_crossdomain);
    virtual void append_body(const char* body, int length);
    /**
    * get the param in query string,
    * for instance, query is "start=100&end=200",
    * then query_get("start") is "100", and query_get("end") is "200"
    */
    virtual std::string query_get(std::string key);
    virtual int request_header_count();
    virtual std::string request_header_key_at(int index);
    virtual std::string request_header_value_at(int index);
    virtual void set_request_header(std::string key, std::string value);
    virtual std::string get_request_header(std::string name);
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
    std::string filed_name;
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
    virtual int parse_message(SrsStSocket* skt, SrsHttpMessage** ppmsg);
private:
    /**
    * parse the HTTP message to member field: msg.
    */
    virtual int parse_message_imp(SrsStSocket* skt);
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
    std::string query;
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
    virtual const char* get_query();
private:
    /**
    * get the parsed url field.
    * @return return empty string if not set.
    */
    virtual std::string get_uri_field(std::string uri, http_parser_url* hp_u, http_parser_url_fields field);
};

#endif

#endif
