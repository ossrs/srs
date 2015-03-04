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
#include <srs_kernel_consts.hpp>

class SrsRequest;
class SrsStSocket;
class SrsHttpUri;
class SrsHttpMessage;
class SrsFileReader;
class SrsSimpleBuffer;
class SrsHttpMuxEntry;
class ISrsHttpResponseWriter;

// http specification
// CR             = <US-ASCII CR, carriage return (13)>
#define __SRS_HTTP_CR SRS_CONSTS_CR // 0x0D
// LF             = <US-ASCII LF, linefeed (10)>
#define __SRS_HTTP_LF SRS_CONSTS_LF // 0x0A
// SP             = <US-ASCII SP, space (32)>
#define __SRS_HTTP_SP ' ' // 0x20
// HT             = <US-ASCII HT, horizontal-tab (9)>
#define __SRS_HTTP_HT '\x09' // 0x09

// HTTP/1.1 defines the sequence CR LF as the end-of-line marker for all
// protocol elements except the entity-body (see appendix 19.3 for
// tolerant applications). 
#define __SRS_HTTP_CRLF "\r\n" // 0x0D0A
#define __SRS_HTTP_CRLFCRLF "\r\n\r\n" // 0x0D0A0D0A

// @see SrsHttpMessage._http_ts_send_buffer
#define __SRS_HTTP_TS_SEND_BUFFER_SIZE 4096

// helper function: response in json format.
extern int srs_go_http_response_json(ISrsHttpResponseWriter* w, std::string data);

// state of message
enum SrsHttpParseState {
    SrsHttpParseStateInit = 0, 
    SrsHttpParseStateStart, 
    SrsHttpParseStateComplete
};

// A Header represents the key-value pairs in an HTTP header.
class SrsHttpHeader
{
private:
    std::map<std::string, std::string> headers;
public:
    SrsHttpHeader();
    virtual ~SrsHttpHeader();
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
// Usage 1, response with specified length content:
//      ISrsHttpResponseWriter* w; // create or get response.
//      std::string msg = "Hello, HTTP!";
//      w->header()->set_content_type("text/plain; charset=utf-8");
//      w->header()->set_content_length(msg.length());
//      w->write_header(SRS_CONSTS_HTTP_OK);
//      w->write((char*)msg.data(), (int)msg.length());
//      w->final_request(); // optional flush.
// Usage 2, response with HTTP code only, zero content length.
//      ISrsHttpResponseWriter* w; // create or get response.
//      w->header()->set_content_length(0);
//      w->write_header(SRS_CONSTS_HTTP_OK);
//      w->final_request();
// Usage 3, response in chunked encoding.
//      ISrsHttpResponseWriter* w; // create or get response.
//      std::string msg = "Hello, HTTP!";
//      w->header()->set_content_type("application/octet-stream");
//      w->write_header(SRS_CONSTS_HTTP_OK);
//      w->write((char*)msg.data(), (int)msg.length());
//      w->write((char*)msg.data(), (int)msg.length());
//      w->write((char*)msg.data(), (int)msg.length());
//      w->write((char*)msg.data(), (int)msg.length());
//      w->final_request(); // required to end the chunked and flush.
class ISrsHttpResponseWriter
{
public:
    ISrsHttpResponseWriter();
    virtual ~ISrsHttpResponseWriter();
public:
    // when chunked mode, 
    // final the request to complete the chunked encoding.
    virtual int final_request() = 0;
    
    // Header returns the header map that will be sent by WriteHeader.
    // Changing the header after a call to WriteHeader (or Write) has
    // no effect.
    virtual SrsHttpHeader* header() = 0;
    
    // Write writes the data to the connection as part of an HTTP reply.
    // If WriteHeader has not yet been called, Write calls WriteHeader(http.StatusOK)
    // before writing the data.  If the Header does not contain a
    // Content-Type line, Write adds a Content-Type set to the result of passing
    // the initial 512 bytes of written data to DetectContentType.
    // @param data, the data to send. NULL to flush header only.
    virtual int write(char* data, int size) = 0;
    
    // WriteHeader sends an HTTP response header with status code.
    // If WriteHeader is not called explicitly, the first call to Write
    // will trigger an implicit WriteHeader(http.StatusOK).
    // Thus explicit calls to WriteHeader are mainly used to
    // send error codes.
    // @remark, user must set header then write or write_header.
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
class ISrsHttpHandler
{
public:
    SrsHttpMuxEntry* entry;
public:
    ISrsHttpHandler();
    virtual ~ISrsHttpHandler();
public:
    virtual int serve_http(ISrsHttpResponseWriter* w, SrsHttpMessage* r) = 0;
};

// Redirect to a fixed URL
class SrsHttpRedirectHandler : public ISrsHttpHandler
{
private:
    std::string url;
    int code;
public:
    SrsHttpRedirectHandler(std::string u, int c);
    virtual ~SrsHttpRedirectHandler();
public:
    virtual int serve_http(ISrsHttpResponseWriter* w, SrsHttpMessage* r);
};

// NotFound replies to the request with an HTTP 404 not found error.
class SrsHttpNotFoundHandler : public ISrsHttpHandler
{
public:
    SrsHttpNotFoundHandler();
    virtual ~SrsHttpNotFoundHandler();
public:
    virtual int serve_http(ISrsHttpResponseWriter* w, SrsHttpMessage* r);
};

// FileServer returns a handler that serves HTTP requests
// with the contents of the file system rooted at root.
//
// To use the operating system's file system implementation,
// use http.Dir:
//
//     http.Handle("/", SrsHttpFileServer("/tmp"))
//     http.Handle("/", SrsHttpFileServer("static-dir"))
class SrsHttpFileServer : public ISrsHttpHandler
{
protected:
    std::string dir;
public:
    SrsHttpFileServer(std::string root_dir);
    virtual ~SrsHttpFileServer();
public:
    virtual int serve_http(ISrsHttpResponseWriter* w, SrsHttpMessage* r);
private:
    /**
    * serve the file by specified path
    */
    virtual int serve_file(ISrsHttpResponseWriter* w, SrsHttpMessage* r, std::string fullpath);
    virtual int serve_flv_file(ISrsHttpResponseWriter* w, SrsHttpMessage* r, std::string fullpath);
    virtual int serve_mp4_file(ISrsHttpResponseWriter* w, SrsHttpMessage* r, std::string fullpath);
protected:
    /**
    * when access flv file with x.flv?start=xxx
    */
    virtual int serve_flv_stream(ISrsHttpResponseWriter* w, SrsHttpMessage* r, std::string fullpath, int offset);
    /**
    * when access mp4 file with x.mp4?range=start-end
    * @param start the start offset in bytes.
    * @param end the end offset in bytes. -1 to end of file.
    * @remark response data in [start, end].
    */
    virtual int serve_mp4_stream(ISrsHttpResponseWriter* w, SrsHttpMessage* r, std::string fullpath, int start, int end);
protected:
    /**
    * copy the fs to response writer in size bytes.
    */
    virtual int copy(ISrsHttpResponseWriter* w, SrsFileReader* fs, SrsHttpMessage* r, int size);
};

// the mux entry for server mux.
class SrsHttpMuxEntry
{
public:
    bool explicit_match;
    ISrsHttpHandler* handler;
    std::string pattern;
    bool enabled;
public:
    SrsHttpMuxEntry();
    virtual ~SrsHttpMuxEntry();
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
class SrsHttpServeMux
{
private:
    // the pattern handler.
    std::map<std::string, SrsHttpMuxEntry*> entries;
    // the vhost handler.
    std::map<std::string, ISrsHttpHandler*> vhosts;
public:
    SrsHttpServeMux();
    virtual ~SrsHttpServeMux();
public:
    /**
    * initialize the http serve mux.
    */
    virtual int initialize();
public:
    // Handle registers the handler for the given pattern.
    // If a handler already exists for pattern, Handle panics.
    virtual int handle(std::string pattern, ISrsHttpHandler* handler);
// interface ISrsHttpHandler
public:
    virtual int serve_http(ISrsHttpResponseWriter* w, SrsHttpMessage* r);
private:
    virtual int find_handler(SrsHttpMessage* r, ISrsHttpHandler** ph);
    virtual int match(SrsHttpMessage* r, ISrsHttpHandler** ph);
    virtual bool path_match(std::string pattern, std::string path);
};

/**
* response writer use st socket
*/
class SrsHttpResponseWriter : public ISrsHttpResponseWriter
{
private:
    SrsStSocket* skt;
    SrsHttpHeader* hdr;
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
    SrsHttpResponseWriter(SrsStSocket* io);
    virtual ~SrsHttpResponseWriter();
public:
    virtual int final_request();
    virtual SrsHttpHeader* header();
    virtual int write(char* data, int size);
    virtual void write_header(int code);
    virtual int send_header(char* data, int size);
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
    virtual void set_url(std::string url);
    virtual void set_state(SrsHttpParseState state);
    virtual void set_header(http_parser* header);
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
