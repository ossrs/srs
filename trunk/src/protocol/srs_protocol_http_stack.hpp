//
// Copyright (c) 2013-2024 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_PROTOCOL_HTTP_HPP
#define SRS_PROTOCOL_HTTP_HPP

#include <srs_core.hpp>

#include <srs_kernel_io.hpp>

#include <map>
#include <string>
#include <vector>

// For srs-librtmp, @see https://github.com/ossrs/srs/issues/213
#ifndef _WIN32
#include <sys/uio.h>
#endif

class SrsFileReader;
class SrsHttpHeader;
class ISrsHttpMessage;
class SrsHttpMuxEntry;
class ISrsHttpResponseWriter;
class SrsJsonObject;
class ISrsFileReaderFactory;

// From http specification
// CR             = <US-ASCII CR, carriage return (13)>
#define SRS_HTTP_CR SRS_CONSTS_CR // 0x0D
// LF             = <US-ASCII LF, linefeed (10)>
#define SRS_HTTP_LF SRS_CONSTS_LF // 0x0A
// SP             = <US-ASCII SP, space (32)>
#define SRS_HTTP_SP ' ' // 0x20
// HT             = <US-ASCII HT, horizontal-tab (9)>
#define SRS_HTTP_HT '\x09' // 0x09

// HTTP/1.1 defines the sequence CR LF as the end-of-line marker for all
// protocol elements except the entity-body (see appendix 19.3 for
// tolerant applications).
#define SRS_HTTP_CRLF "\r\n" // 0x0D0A
#define SRS_HTTP_CRLFCRLF "\r\n\r\n" // 0x0D0A0D0A

// For ead all of http body, read each time.
#define SRS_HTTP_READ_CACHE_BYTES 4096

// For http parser macros
#define SRS_CONSTS_HTTP_OPTIONS HTTP_OPTIONS
#define SRS_CONSTS_HTTP_GET HTTP_GET
#define SRS_CONSTS_HTTP_POST HTTP_POST
#define SRS_CONSTS_HTTP_PUT HTTP_PUT
#define SRS_CONSTS_HTTP_DELETE HTTP_DELETE

// Error replies to the request with the specified error message and HTTP code.
// The error message should be plain text.
extern srs_error_t srs_go_http_error(ISrsHttpResponseWriter* w, int code);
extern srs_error_t srs_go_http_error(ISrsHttpResponseWriter* w, int code, std::string error);

// Get the status text of code.
extern std::string srs_generate_http_status_text(int status);

// It reports whether a given response status code
// permits a body.  See RFC2616, section 4.4.
extern bool srs_go_http_body_allowd(int status);

// DetectContentType implements the algorithm described
// at http://mimesniff.spec.whatwg.org/ to determine the
// Content-Type of the given data.  It considers at most the
// first 512 bytes of data.  DetectContentType always returns
// a valid MIME type: if it cannot determine a more specific one, it
// returns "application/octet-stream".
extern std::string srs_go_http_detect(char* data, int size);

// The state of HTTP message
enum SrsHttpParseState {
    SrsHttpParseStateInit = 0,
    SrsHttpParseStateStart,
    SrsHttpParseStateHeaderComplete,
    SrsHttpParseStateBody,
    SrsHttpParseStateMessageComplete
};

// A Header represents the key-value pairs in an HTTP header.
class SrsHttpHeader
{
private:
    // The order in which header fields with differing field names are
    // received is not significant. However, it is "good practice" to send
    // general-header fields first, followed by request-header or response-
    // header fields, and ending with the entity-header fields.
    // @doc https://tools.ietf.org/html/rfc2616#section-4.2
    std::map<std::string, std::string> headers;
    // Store keys to keep fields in order.
    std::vector<std::string> keys_;
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
    // Delete the http header indicated by key.
    // Return the removed header field.
    virtual void del(std::string);
    // Get the count of headers.
    virtual int count();
public:
    // Dumps to a JSON object.
    virtual void dumps(SrsJsonObject* o);
public:
    // Get the content length. -1 if not set.
    virtual int64_t content_length();
    // set the content length by header "Content-Length"
    virtual void set_content_length(int64_t size);
public:
    // Get the content type. empty string if not set.
    virtual std::string content_type();
    // set the content type by header "Content-Type"
    virtual void set_content_type(std::string ct);
public:
    // write all headers to string stream.
    virtual void write(std::stringstream& ss);
};

// A ResponseWriter interface is used by an HTTP handler to
// construct an HTTP response.
// Usage 0, response with a message once:
//      ISrsHttpResponseWriter* w; // create or get response.
//      std::string msg = "Hello, HTTP!";
//      w->write((char*)msg.data(), (int)msg.length());
// Usage 1, response with specified length content, same to #0:
//      ISrsHttpResponseWriter* w; // create or get response.
//      std::string msg = "Hello, HTTP!";
//      w->header()->set_content_type("text/plain; charset=utf-8");
//      w->header()->set_content_length(msg.length());
//      w->write_header(SRS_CONSTS_HTTP_OK);
//      w->write((char*)msg.data(), (int)msg.length()); // write N times, N>0
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
    // When chunked mode,
    // final the request to complete the chunked encoding.
    // For no-chunked mode,
    // final to send request, for example, content-length is 0.
    virtual srs_error_t final_request() = 0;
    
    // Header returns the header map that will be sent by WriteHeader.
    // Changing the header after a call to WriteHeader (or Write) has
    // no effect.
    virtual SrsHttpHeader* header() = 0;
    
    // Write writes the data to the connection as part of an HTTP reply.
    // If WriteHeader has not yet been called, Write calls WriteHeader(http.StatusOK)
    // before writing the data.  If the Header does not contain a
    // Content-Type line, Write adds a Content-Type set to the result of passing
    // The initial 512 bytes of written data to DetectContentType.
    // @param data, the data to send. NULL to flush header only.
    virtual srs_error_t write(char* data, int size) = 0;
    // for the HTTP FLV, to writev to improve performance.
    virtual srs_error_t writev(const iovec* iov, int iovcnt, ssize_t* pnwrite) = 0;
    
    // WriteHeader sends an HTTP response header with status code.
    // If WriteHeader is not called explicitly, the first call to Write
    // will trigger an implicit WriteHeader(http.StatusOK).
    // Thus explicit calls to WriteHeader are mainly used to
    // send error codes.
    // @remark, user must set header then write or write_header.
    virtual void write_header(int code) = 0;
};

// The reader interface for http response.
class ISrsHttpResponseReader : public ISrsReader
{
public:
    ISrsHttpResponseReader();
    virtual ~ISrsHttpResponseReader();
public:
    // Whether response read EOF.
    virtual bool eof() = 0;
};

// A RequestWriter interface is used by an HTTP handler to
// construct an HTTP request.
// Usage 0, request with a message once:
//      ISrsHttpRequestWriter* w; // create or get request.
//      std::string msg = "Hello, HTTP!";
//      w->write((char*)msg.data(), (int)msg.length());
// Usage 1, request with specified length content, same to #0:
//      ISrsHttpRequestWriter* w; // create or get request.
//      std::string msg = "Hello, HTTP!";
//      w->header()->set_content_type("text/plain; charset=utf-8");
//      w->header()->set_content_length(msg.length());
//      w->write_header("POST", "/");
//      w->write((char*)msg.data(), (int)msg.length()); // write N times, N>0
//      w->final_request(); // optional flush.
// Usage 2, request with HTTP code only, zero content length.
//      ISrsHttpRequestWriter* w; // create or get request.
//      w->header()->set_content_length(0);
//      w->write_header("GET", "/");
//      w->final_request();
// Usage 3, request in chunked encoding.
//      ISrsHttpRequestWriter* w; // create or get request.
//      std::string msg = "Hello, HTTP!";
//      w->header()->set_content_type("application/octet-stream");
//      w->write_header("POST", "/");
//      w->write((char*)msg.data(), (int)msg.length());
//      w->write((char*)msg.data(), (int)msg.length());
//      w->write((char*)msg.data(), (int)msg.length());
//      w->write((char*)msg.data(), (int)msg.length());
//      w->final_request(); // required to end the chunked and flush.
class ISrsHttpRequestWriter
{
public:
    ISrsHttpRequestWriter();
    virtual ~ISrsHttpRequestWriter();
public:
    // When chunked mode,
    // final the request to complete the chunked encoding.
    // For no-chunked mode,
    // final to send request, for example, content-length is 0.
    virtual srs_error_t final_request() = 0;

    // Header returns the header map that will be sent by WriteHeader.
    // Changing the header after a call to WriteHeader (or Write) has
    // no effect.
    virtual SrsHttpHeader* header() = 0;

    // Write writes the data to the connection as part of an HTTP reply.
    // If WriteHeader has not yet been called, Write calls WriteHeader(http.StatusOK)
    // before writing the data.  If the Header does not contain a
    // Content-Type line, Write adds a Content-Type set to the result of passing
    // The initial 512 bytes of written data to DetectContentType.
    // @param data, the data to send. NULL to flush header only.
    virtual srs_error_t write(char* data, int size) = 0;
    // for the HTTP FLV, to writev to improve performance.
    virtual srs_error_t writev(const iovec* iov, int iovcnt, ssize_t* pnwrite) = 0;

    // WriteHeader sends an HTTP request header with status code.
    // If WriteHeader is not called explicitly, the first call to Write
    // will trigger an implicit WriteHeader(http.StatusOK).
    // Thus explicit calls to WriteHeader are mainly used to
    // send error codes.
    // @remark, user must set header then write or write_header.
    virtual void write_header(const std::string& method, const std::string& path) = 0;
};

// Objects implementing the Handler interface can be
// registered to serve a particular path or subtree
// in the HTTP server.
//
// ServeHTTP should write reply headers and data to the ResponseWriter
// and then return.  Returning signals that the request is finished
// and that the HTTP server can move on to the next request on
// The connection.
class ISrsHttpHandler
{
public:
    SrsHttpMuxEntry* entry;
public:
    ISrsHttpHandler();
    virtual ~ISrsHttpHandler();
public:
    virtual bool is_not_found();
    virtual srs_error_t serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r) = 0;
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
    virtual srs_error_t serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r);
};

// NotFound replies to the request with an HTTP 404 not found error.
class SrsHttpNotFoundHandler : public ISrsHttpHandler
{
public:
    SrsHttpNotFoundHandler();
    virtual ~SrsHttpNotFoundHandler();
public:
    virtual bool is_not_found();
    virtual srs_error_t serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r);
};

// For utest to mock it.
typedef bool (*_pfn_srs_path_exists)(std::string path);

// Build the file path from request r.
extern std::string srs_http_fs_fullpath(std::string dir, std::string pattern, std::string upath);

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
protected:
    ISrsFileReaderFactory* fs_factory;
    _pfn_srs_path_exists _srs_path_exists;
public:
    SrsHttpFileServer(std::string root_dir);
    virtual ~SrsHttpFileServer();
private:
    // For utest to mock the fs.
    virtual void set_fs_factory(ISrsFileReaderFactory* v);
    // For utest to mock the path check function.
    virtual void set_path_check(_pfn_srs_path_exists pfn);
public:
    virtual srs_error_t serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r);
private:
    // Serve the file by specified path
    virtual srs_error_t serve_file(ISrsHttpResponseWriter* w, ISrsHttpMessage* r, std::string fullpath);
    virtual srs_error_t serve_flv_file(ISrsHttpResponseWriter* w, ISrsHttpMessage* r, std::string fullpath);
    virtual srs_error_t serve_mp4_file(ISrsHttpResponseWriter* w, ISrsHttpMessage* r, std::string fullpath);
    virtual srs_error_t serve_m3u8_file(ISrsHttpResponseWriter* w, ISrsHttpMessage* r, std::string fullpath);
    // the ts file including: .ts .m4s init.mp4
    virtual srs_error_t serve_ts_file(ISrsHttpResponseWriter* w, ISrsHttpMessage* r, std::string fullpath);
protected:
    // When access flv file with x.flv?start=xxx
    virtual srs_error_t serve_flv_stream(ISrsHttpResponseWriter* w, ISrsHttpMessage* r, std::string fullpath, int64_t offset);
    // When access mp4 file with x.mp4?range=start-end
    // @param start the start offset in bytes.
    // @param end the end offset in bytes. -1 to end of file.
    // @remark response data in [start, end].
    virtual srs_error_t serve_mp4_stream(ISrsHttpResponseWriter* w, ISrsHttpMessage* r, std::string fullpath, int64_t start, int64_t end);
    // For HLS protocol.
    // When the request url, like as "http://127.0.0.1:8080/live/livestream.m3u8", 
    // returns the response like as "http://127.0.0.1:8080/live/livestream.m3u8?hls_ctx=12345678" .
    // SRS use "hls_ctx" to keep track of subsequent requests that is short-connection.
    // Remark 1: 
    //           Fill the parameter "hls_ctx" by yourself in the first request is allowed, SRS will use it.
    //           And MUST make sure it is unique.
    // Remark 2:
    //           If use two same "hls_ctx" in different requests, SRS cannot detect so that they will be treated as one.
    virtual srs_error_t serve_m3u8_ctx(ISrsHttpResponseWriter* w, ISrsHttpMessage* r, std::string fullpath);
    // the ts file including: .ts .m4s init.mp4
    virtual srs_error_t serve_ts_ctx(ISrsHttpResponseWriter* w, ISrsHttpMessage* r, std::string fullpath);
protected:
    // Copy the fs to response writer in size bytes.
    virtual srs_error_t copy(ISrsHttpResponseWriter* w, SrsFileReader* fs, ISrsHttpMessage* r, int64_t size);
};

// The mux entry for server mux.
// The matcher info, for example, the pattern and handler.
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

// The hijacker for http pattern match.
class ISrsHttpMatchHijacker
{
public:
    ISrsHttpMatchHijacker();
    virtual ~ISrsHttpMatchHijacker();
public:
    // When match the request failed, no handler to process request.
    // @param request the http request message to match the handler.
    // @param ph the already matched handler, hijack can rewrite it.
    virtual srs_error_t hijack(ISrsHttpMessage* request, ISrsHttpHandler** ph) = 0;
};

// The server mux, all http server should implements it.
class ISrsHttpServeMux : public ISrsHttpHandler
{
public:
    ISrsHttpServeMux();
    virtual ~ISrsHttpServeMux();
public:
    // Register HTTP handler to mux.
    virtual srs_error_t handle(std::string pattern, ISrsHttpHandler* handler) = 0;
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
// The pattern "/" matches all paths not matched by other registered
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
class SrsHttpServeMux : public ISrsHttpServeMux
{
private:
    // The pattern handler, to handle the http request.
    std::map<std::string, SrsHttpMuxEntry*> entries;
    // The vhost handler.
    // When find the handler to process the request,
    // append the matched vhost when pattern not starts with /,
    // For example, for pattern /live/livestream.flv of vhost ossrs.net,
    // The path will rewrite to ossrs.net/live/livestream.flv
    std::map<std::string, ISrsHttpHandler*> vhosts;
    // all hijackers for http match.
    // For example, the hstrs(http stream trigger rtmp source)
    // can hijack and install handler when request incoming and no handler.
    std::vector<ISrsHttpMatchHijacker*> hijackers;
public:
    SrsHttpServeMux();
    virtual ~SrsHttpServeMux();
public:
    // Initialize the http serve mux.
    virtual srs_error_t initialize();
    // hijack the http match.
    virtual void hijack(ISrsHttpMatchHijacker* h);
    virtual void unhijack(ISrsHttpMatchHijacker* h);
public:
    // Handle registers the handler for the given pattern.
    // If a handler already exists for pattern, Handle panics.
    virtual srs_error_t handle(std::string pattern, ISrsHttpHandler* handler);
    // Remove the handler for pattern. Note that this will not free the handler.
    void unhandle(std::string pattern, ISrsHttpHandler* handler);
// Interface ISrsHttpServeMux
public:
    virtual srs_error_t serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r);
public:
    virtual srs_error_t find_handler(ISrsHttpMessage* r, ISrsHttpHandler** ph);
private:
    virtual srs_error_t match(ISrsHttpMessage* r, ISrsHttpHandler** ph);
    virtual bool path_match(std::string pattern, std::string path);
};

// The filter http mux, directly serve the http CORS requests
class SrsHttpCorsMux : public ISrsHttpHandler
{
private:
    bool required;
    bool enabled;
    ISrsHttpHandler* next_;
public:
    SrsHttpCorsMux(ISrsHttpHandler* h);
    virtual ~SrsHttpCorsMux();
public:
    virtual srs_error_t initialize(bool cros_enabled);
// Interface ISrsHttpServeMux
public:
    virtual srs_error_t serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r);
};

// The filter http mux, directly serve the http AUTH requests,
// while proxy to the worker mux for services.
// @see https://www.rfc-editor.org/rfc/rfc7617
// @see https://developer.mozilla.org/en-US/docs/Web/HTTP/Headers/WWW-Authenticate
class SrsHttpAuthMux : public ISrsHttpHandler
{
private:
    bool enabled_;
    std::string username_;
    std::string password_;
    ISrsHttpHandler* next_;
public:
    SrsHttpAuthMux(ISrsHttpHandler* h);
    virtual ~SrsHttpAuthMux();
public:
    virtual srs_error_t initialize(bool enabled, std::string username, std::string password);
// Interface ISrsHttpServeMux
public:
    virtual srs_error_t serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r);
private:
    virtual srs_error_t do_auth(ISrsHttpResponseWriter* w, ISrsHttpMessage* r);
};

// A Request represents an HTTP request received by a server
// or to be sent by a client.
//
// The field semantics differ slightly between client and server
// usage. In addition to the notes on the fields below, see the
// documentation for Request.Write and RoundTripper.
//
// There are some modes to determine the length of body:
//      1. content-length and chunked.
//      2. infinite chunked.
//      3. no body.
// For example:
//      ISrsHttpMessage* r = ...;
//      while (!r->eof()) r->read(); // Read in mode 1 or 3.
// @rmark for mode 2, the infinite chunked, all left data is body.
class ISrsHttpMessage
{
public:
    ISrsHttpMessage();
    virtual ~ISrsHttpMessage();
public:
    virtual uint8_t message_type() = 0;
    virtual uint8_t method() = 0;
    virtual uint16_t status_code() = 0;
    // Method helpers.
    virtual std::string method_str() = 0;
    virtual bool is_http_get() = 0;
    virtual bool is_http_put() = 0;
    virtual bool is_http_post() = 0;
    virtual bool is_http_delete() = 0;
    virtual bool is_http_options() = 0;
public:
    // Whether should keep the connection alive.
    virtual bool is_keep_alive() = 0;
    // The uri contains the host and path.
    virtual std::string uri() = 0;
    // The url maybe the path.
    virtual std::string url() = 0;
    virtual std::string host() = 0;
    virtual std::string path() = 0;
    virtual std::string query() = 0;
    virtual std::string ext() = 0;
    // Get the RESTful id, in string,
    // for example, pattern is /api/v1/streams, path is /api/v1/streams/100,
    // then the rest id is 100.
    // @param pattern the handler pattern which will serve the request.
    // @return the REST id; "" if not matched.
    virtual std::string parse_rest_id(std::string pattern) = 0;
public:
    // Read body to string.
    // @remark for small http body.
    virtual srs_error_t body_read_all(std::string& body) = 0;
    // Get the body reader, to read one by one.
    // @remark when body is very large, or chunked, use this.
    virtual ISrsHttpResponseReader* body_reader() = 0;
    // The content length, -1 for chunked or not set.
    virtual int64_t content_length() = 0;
public:
    // Get the param in query string,
    // for instance, query is "start=100&end=200",
    // then query_get("start") is "100", and query_get("end") is "200"
    virtual std::string query_get(std::string key) = 0;
    // Get the headers.
    virtual SrsHttpHeader* header() = 0;
public:
    // Whether the current request is JSONP,
    // which has a "callback=xxx" in QueryString.
    virtual bool is_jsonp() = 0;
};

// Used to resolve the http uri.
class SrsHttpUri
{
private:
    std::string url_;
    std::string schema;
    std::string host;
    int port;
    std::string path;
    std::string query;
    std::string fragment_;
    std::string username_;
    std::string password_;
    std::map<std::string, std::string> query_values_;
public:
    SrsHttpUri();
    virtual ~SrsHttpUri();
public:
    // Initialize the http uri.
    virtual srs_error_t initialize(std::string _url);
    // After parsed the message, set the schema to https.
    virtual void set_schema(std::string v);
public:
    virtual std::string get_url();
    virtual std::string get_schema();
    virtual std::string get_host();
    virtual int get_port();
    virtual std::string get_path();
    virtual std::string get_query();
    virtual std::string get_query_by_key(std::string key);
    virtual std::string get_fragment();
    virtual std::string username();
    virtual std::string password();
private:
    // Get the parsed url field.
    // @return return empty string if not set.
    virtual std::string get_uri_field(const std::string& uri, void* hp_u, int field);
    srs_error_t parse_query();
public:
    static std::string query_escape(std::string s);
    static std::string path_escape(std::string s);
    static srs_error_t query_unescape(std::string s, std::string& value);
    static srs_error_t path_unescape(std::string s, std::string& value);
};

// For #ifndef SRS_PROTOCOL_HTTP_HPP
#endif

// The http-parser is license under MIT at https://github.com/nodejs/http-parser/blob/master/LICENSE-MIT
// Version: 2.9.2 from https://github.com/nodejs/http-parser/releases/tag/v2.9.2
// File: https://raw.githubusercontent.com/nodejs/http-parser/5c17dad400e45c5a442a63f250fff2638d144682/http_parser.h

/////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////

/* Copyright Joyent, Inc. and other Node contributors. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */
#ifndef http_parser_h
#define http_parser_h
#ifdef __cplusplus
extern "C" {
#endif

/* Also update SONAME in the Makefile whenever you change these. */
#define HTTP_PARSER_VERSION_MAJOR 2
#define HTTP_PARSER_VERSION_MINOR 9
#define HTTP_PARSER_VERSION_PATCH 2

#include <stddef.h>
#if defined(_WIN32) && !defined(__MINGW32__) && \
  (!defined(_MSC_VER) || _MSC_VER<1600) && !defined(__WINE__)
#include <BaseTsd.h>
typedef __int8 int8_t;
typedef unsigned __int8 uint8_t;
typedef __int16 int16_t;
typedef unsigned __int16 uint16_t;
typedef __int32 int32_t;
typedef unsigned __int32 uint32_t;
typedef __int64 int64_t;
typedef unsigned __int64 uint64_t;
#else
#include <stdint.h>
#endif

/* Compile with -DHTTP_PARSER_STRICT=0 to make less checks, but run
 * faster
 */
#ifndef HTTP_PARSER_STRICT
# define HTTP_PARSER_STRICT 1
#endif

/* Maximium header size allowed. If the macro is not defined
 * before including this header then the default is used. To
 * change the maximum header size, define the macro in the build
 * environment (e.g. -DHTTP_MAX_HEADER_SIZE=<value>). To remove
 * the effective limit on the size of the header, define the macro
 * to a very large number (e.g. -DHTTP_MAX_HEADER_SIZE=0x7fffffff)
 */
#ifndef HTTP_MAX_HEADER_SIZE
# define HTTP_MAX_HEADER_SIZE (80*1024)
#endif

typedef struct http_parser http_parser;
typedef struct http_parser_settings http_parser_settings;


/* Callbacks should return non-zero to indicate an error. The parser will
 * then halt execution.
 *
 * The one exception is on_headers_complete. In a HTTP_RESPONSE parser
 * returning '1' from on_headers_complete will tell the parser that it
 * should not expect a body. This is used when receiving a response to a
 * HEAD request which may contain 'Content-Length' or 'Transfer-Encoding:
 * chunked' headers that indicate the presence of a body.
 *
 * Returning `2` from on_headers_complete will tell parser that it should not
 * expect neither a body nor any futher responses on this connection. This is
 * useful for handling responses to a CONNECT request which may not contain
 * `Upgrade` or `Connection: upgrade` headers.
 *
 * http_data_cb does not return data chunks. It will be called arbitrarily
 * many times for each string. E.G. you might get 10 callbacks for "on_url"
 * each providing just a few characters more data.
 */
typedef int (*http_data_cb) (http_parser*, const char *at, size_t length);
typedef int (*http_cb) (http_parser*);


/* Status Codes */
#define HTTP_STATUS_MAP(XX)                                                 \
  XX(100, CONTINUE,                        Continue)                        \
  XX(101, SWITCHING_PROTOCOLS,             Switching Protocols)             \
  XX(102, PROCESSING,                      Processing)                      \
  XX(200, OK,                              OK)                              \
  XX(201, CREATED,                         Created)                         \
  XX(202, ACCEPTED,                        Accepted)                        \
  XX(203, NON_AUTHORITATIVE_INFORMATION,   Non-Authoritative Information)   \
  XX(204, NO_CONTENT,                      No Content)                      \
  XX(205, RESET_CONTENT,                   Reset Content)                   \
  XX(206, PARTIAL_CONTENT,                 Partial Content)                 \
  XX(207, MULTI_STATUS,                    Multi-Status)                    \
  XX(208, ALREADY_REPORTED,                Already Reported)                \
  XX(226, IM_USED,                         IM Used)                         \
  XX(300, MULTIPLE_CHOICES,                Multiple Choices)                \
  XX(301, MOVED_PERMANENTLY,               Moved Permanently)               \
  XX(302, FOUND,                           Found)                           \
  XX(303, SEE_OTHER,                       See Other)                       \
  XX(304, NOT_MODIFIED,                    Not Modified)                    \
  XX(305, USE_PROXY,                       Use Proxy)                       \
  XX(307, TEMPORARY_REDIRECT,              Temporary Redirect)              \
  XX(308, PERMANENT_REDIRECT,              Permanent Redirect)              \
  XX(400, BAD_REQUEST,                     Bad Request)                     \
  XX(401, UNAUTHORIZED,                    Unauthorized)                    \
  XX(402, PAYMENT_REQUIRED,                Payment Required)                \
  XX(403, FORBIDDEN,                       Forbidden)                       \
  XX(404, NOT_FOUND,                       Not Found)                       \
  XX(405, METHOD_NOT_ALLOWED,              Method Not Allowed)              \
  XX(406, NOT_ACCEPTABLE,                  Not Acceptable)                  \
  XX(407, PROXY_AUTHENTICATION_REQUIRED,   Proxy Authentication Required)   \
  XX(408, REQUEST_TIMEOUT,                 Request Timeout)                 \
  XX(409, CONFLICT,                        Conflict)                        \
  XX(410, GONE,                            Gone)                            \
  XX(411, LENGTH_REQUIRED,                 Length Required)                 \
  XX(412, PRECONDITION_FAILED,             Precondition Failed)             \
  XX(413, PAYLOAD_TOO_LARGE,               Payload Too Large)               \
  XX(414, URI_TOO_LONG,                    URI Too Long)                    \
  XX(415, UNSUPPORTED_MEDIA_TYPE,          Unsupported Media Type)          \
  XX(416, RANGE_NOT_SATISFIABLE,           Range Not Satisfiable)           \
  XX(417, EXPECTATION_FAILED,              Expectation Failed)              \
  XX(421, MISDIRECTED_REQUEST,             Misdirected Request)             \
  XX(422, UNPROCESSABLE_ENTITY,            Unprocessable Entity)            \
  XX(423, LOCKED,                          Locked)                          \
  XX(424, FAILED_DEPENDENCY,               Failed Dependency)               \
  XX(426, UPGRADE_REQUIRED,                Upgrade Required)                \
  XX(428, PRECONDITION_REQUIRED,           Precondition Required)           \
  XX(429, TOO_MANY_REQUESTS,               Too Many Requests)               \
  XX(431, REQUEST_HEADER_FIELDS_TOO_LARGE, Request Header Fields Too Large) \
  XX(451, UNAVAILABLE_FOR_LEGAL_REASONS,   Unavailable For Legal Reasons)   \
  XX(500, INTERNAL_SERVER_ERROR,           Internal Server Error)           \
  XX(501, NOT_IMPLEMENTED,                 Not Implemented)                 \
  XX(502, BAD_GATEWAY,                     Bad Gateway)                     \
  XX(503, SERVICE_UNAVAILABLE,             Service Unavailable)             \
  XX(504, GATEWAY_TIMEOUT,                 Gateway Timeout)                 \
  XX(505, HTTP_VERSION_NOT_SUPPORTED,      HTTP Version Not Supported)      \
  XX(506, VARIANT_ALSO_NEGOTIATES,         Variant Also Negotiates)         \
  XX(507, INSUFFICIENT_STORAGE,            Insufficient Storage)            \
  XX(508, LOOP_DETECTED,                   Loop Detected)                   \
  XX(510, NOT_EXTENDED,                    Not Extended)                    \
  XX(511, NETWORK_AUTHENTICATION_REQUIRED, Network Authentication Required) \

enum http_status
  {
#define XX(num, name, string) HTTP_STATUS_##name = num,
  HTTP_STATUS_MAP(XX)
#undef XX
  };


/* Request Methods */
#define HTTP_METHOD_MAP(XX)         \
  XX(0,  DELETE,      DELETE)       \
  XX(1,  GET,         GET)          \
  XX(2,  HEAD,        HEAD)         \
  XX(3,  POST,        POST)         \
  XX(4,  PUT,         PUT)          \
  /* pathological */                \
  XX(5,  CONNECT,     CONNECT)      \
  XX(6,  OPTIONS,     OPTIONS)      \
  XX(7,  TRACE,       TRACE)        \
  /* WebDAV */                      \
  XX(8,  COPY,        COPY)         \
  XX(9,  LOCK,        LOCK)         \
  XX(10, MKCOL,       MKCOL)        \
  XX(11, MOVE,        MOVE)         \
  XX(12, PROPFIND,    PROPFIND)     \
  XX(13, PROPPATCH,   PROPPATCH)    \
  XX(14, SEARCH,      SEARCH)       \
  XX(15, UNLOCK,      UNLOCK)       \
  XX(16, BIND,        BIND)         \
  XX(17, REBIND,      REBIND)       \
  XX(18, UNBIND,      UNBIND)       \
  XX(19, ACL,         ACL)          \
  /* subversion */                  \
  XX(20, REPORT,      REPORT)       \
  XX(21, MKACTIVITY,  MKACTIVITY)   \
  XX(22, CHECKOUT,    CHECKOUT)     \
  XX(23, MERGE,       MERGE)        \
  /* upnp */                        \
  XX(24, MSEARCH,     M-SEARCH)     \
  XX(25, NOTIFY,      NOTIFY)       \
  XX(26, SUBSCRIBE,   SUBSCRIBE)    \
  XX(27, UNSUBSCRIBE, UNSUBSCRIBE)  \
  /* RFC-5789 */                    \
  XX(28, PATCH,       PATCH)        \
  XX(29, PURGE,       PURGE)        \
  /* CalDAV */                      \
  XX(30, MKCALENDAR,  MKCALENDAR)   \
  /* RFC-2068, section 19.6.1.2 */  \
  XX(31, LINK,        LINK)         \
  XX(32, UNLINK,      UNLINK)       \
  /* icecast */                     \
  XX(33, SOURCE,      SOURCE)       \
  /* SIP https://www.ietf.org/rfc/rfc3261.html */ \
  XX(34, REGISTER,    REGISTER)     \
  XX(35, INVITE,      INVITE)       \
  XX(36, ACK,         ACK)          \
  XX(37, MESSAGE,     MESSAGE)      \
  XX(38, BYE,         BYE)          \

enum http_method
  {
#define XX(num, name, string) HTTP_##name = num,
  HTTP_METHOD_MAP(XX)
#undef XX
  };


enum http_parser_type { HTTP_REQUEST, HTTP_RESPONSE, HTTP_BOTH };


/* Flag values for http_parser.flags field */
enum flags
  { F_CHUNKED               = 1 << 0
  , F_CONNECTION_KEEP_ALIVE = 1 << 1
  , F_CONNECTION_CLOSE      = 1 << 2
  , F_CONNECTION_UPGRADE    = 1 << 3
  , F_TRAILING              = 1 << 4
  , F_UPGRADE               = 1 << 5
  , F_SKIPBODY              = 1 << 6
  , F_CONTENTLENGTH         = 1 << 7
  };


/* Map for errno-related constants
 *
 * The provided argument should be a macro that takes 2 arguments.
 */
#define HTTP_ERRNO_MAP(XX)                                           \
  /* No error */                                                     \
  XX(OK, "success")                                                  \
                                                                     \
  /* Callback-related errors */                                      \
  XX(CB_message_begin, "the on_message_begin callback failed")       \
  XX(CB_url, "the on_url callback failed")                           \
  XX(CB_header_field, "the on_header_field callback failed")         \
  XX(CB_header_value, "the on_header_value callback failed")         \
  XX(CB_headers_complete, "the on_headers_complete callback failed") \
  XX(CB_body, "the on_body callback failed")                         \
  XX(CB_message_complete, "the on_message_complete callback failed") \
  XX(CB_status, "the on_status callback failed")                     \
  XX(CB_chunk_header, "the on_chunk_header callback failed")         \
  XX(CB_chunk_complete, "the on_chunk_complete callback failed")     \
                                                                     \
  /* Parsing-related errors */                                       \
  XX(INVALID_EOF_STATE, "stream ended at an unexpected time")        \
  XX(HEADER_OVERFLOW,                                                \
     "too many header bytes seen; overflow detected")                \
  XX(CLOSED_CONNECTION,                                              \
     "data received after completed connection: close message")      \
  XX(INVALID_VERSION, "invalid HTTP version")                        \
  XX(INVALID_STATUS, "invalid HTTP status code")                     \
  XX(INVALID_METHOD, "invalid HTTP method")                          \
  XX(INVALID_URL, "invalid URL")                                     \
  XX(INVALID_HOST, "invalid host")                                   \
  XX(INVALID_PORT, "invalid port")                                   \
  XX(INVALID_PATH, "invalid path")                                   \
  XX(INVALID_QUERY_STRING, "invalid query string")                   \
  XX(INVALID_FRAGMENT, "invalid fragment")                           \
  XX(LF_EXPECTED, "LF character expected")                           \
  XX(INVALID_HEADER_TOKEN, "invalid character in header")            \
  XX(INVALID_CONTENT_LENGTH,                                         \
     "invalid character in content-length header")                   \
  XX(UNEXPECTED_CONTENT_LENGTH,                                      \
     "unexpected content-length header")                             \
  XX(INVALID_CHUNK_SIZE,                                             \
     "invalid character in chunk size header")                       \
  XX(INVALID_CONSTANT, "invalid constant string")                    \
  XX(INVALID_INTERNAL_STATE, "encountered unexpected internal state")\
  XX(STRICT, "strict mode assertion failed")                         \
  XX(PAUSED, "parser is paused")                                     \
  XX(UNKNOWN, "an unknown error occurred")


/* Define HPE_* values for each errno value above */
#define HTTP_ERRNO_GEN(n, s) HPE_##n,
enum http_errno {
  HTTP_ERRNO_MAP(HTTP_ERRNO_GEN)
};
#undef HTTP_ERRNO_GEN


/* Get an http_errno value from an http_parser */
#define HTTP_PARSER_ERRNO(p)            ((enum http_errno) (p)->http_errno)


struct http_parser {
  /** PRIVATE **/
  unsigned int type : 2;         /* enum http_parser_type */
  unsigned int flags : 8;        /* F_* values from 'flags' enum; semi-public */
  unsigned int state : 7;        /* enum state from http_parser.c */
  unsigned int header_state : 7; /* enum header_state from http_parser.c */
  unsigned int index : 7;        /* index into current matcher */
  unsigned int lenient_http_headers : 1;

  uint32_t nread;          /* # bytes read in various scenarios */
  uint64_t content_length; /* # bytes in body (0 if no Content-Length header) */

  /** READ-ONLY **/
  unsigned short http_major;
  unsigned short http_minor;
  unsigned int status_code : 16; /* responses only */
  unsigned int method : 8;       /* requests only */
  unsigned int http_errno : 7;

  /* 1 = Upgrade header was present and the parser has exited because of that.
   * 0 = No upgrade header present.
   * Should be checked when http_parser_execute() returns in addition to
   * error checking.
   */
  unsigned int upgrade : 1;

  /** PUBLIC **/
  void *data; /* A pointer to get hook to the "connection" or "socket" object */
};


struct http_parser_settings {
  http_cb      on_message_begin;
  http_data_cb on_url;
  http_data_cb on_status;
  http_data_cb on_header_field;
  http_data_cb on_header_value;
  http_cb      on_headers_complete;
  http_data_cb on_body;
  http_cb      on_message_complete;
  /* When on_chunk_header is called, the current chunk length is stored
   * in parser->content_length.
   */
  http_cb      on_chunk_header;
  http_cb      on_chunk_complete;
};


enum http_parser_url_fields
  { UF_SCHEMA           = 0
  , UF_HOST             = 1
  , UF_PORT             = 2
  , UF_PATH             = 3
  , UF_QUERY            = 4
  , UF_FRAGMENT         = 5
  , UF_USERINFO         = 6
  , UF_MAX              = 7
  };


/* Result structure for http_parser_parse_url().
 *
 * Callers should index into field_data[] with UF_* values iff field_set
 * has the relevant (1 << UF_*) bit set. As a courtesy to clients (and
 * because we probably have padding left over), we convert any port to
 * a uint16_t.
 */
struct http_parser_url {
  uint16_t field_set;           /* Bitmask of (1 << UF_*) values */
  uint16_t port;                /* Converted UF_PORT string */

  struct {
    uint16_t off;               /* Offset into buffer in which field starts */
    uint16_t len;               /* Length of run in buffer */
  } field_data[UF_MAX];
};


/* Returns the library version. Bits 16-23 contain the major version number,
 * bits 8-15 the minor version number and bits 0-7 the patch level.
 * Usage example:
 *
 *   unsigned long version = http_parser_version();
 *   unsigned major = (version >> 16) & 255;
 *   unsigned minor = (version >> 8) & 255;
 *   unsigned patch = version & 255;
 *   printf("http_parser v%u.%u.%u\n", major, minor, patch);
 */
unsigned long http_parser_version(void);

void http_parser_init(http_parser *parser, enum http_parser_type type);


/* Initialize http_parser_settings members to 0
 */
void http_parser_settings_init(http_parser_settings *settings);


/* Executes the parser. Returns number of parsed bytes. Sets
 * `parser->http_errno` on error. */
size_t http_parser_execute(http_parser *parser,
                           const http_parser_settings *settings,
                           const char *data,
                           size_t len);


/* If http_should_keep_alive() in the on_headers_complete or
 * on_message_complete callback returns 0, then this should be
 * the last message on the connection.
 * If you are the server, respond with the "Connection: close" header.
 * If you are the client, close the connection.
 */
int http_should_keep_alive(const http_parser *parser);

/* Returns a string version of the HTTP method. */
const char *http_method_str(enum http_method m);

/* Returns a string version of the HTTP status code. */
const char *http_status_str(enum http_status s);

/* Return a string name of the given error */
const char *http_errno_name(enum http_errno err);

/* Return a string description of the given error */
const char *http_errno_description(enum http_errno err);

/* Initialize all http_parser_url members to 0 */
void http_parser_url_init(struct http_parser_url *u);

/* Parse a URL; return nonzero on failure */
int http_parser_parse_url(const char *buf, size_t buflen,
                          int is_connect,
                          struct http_parser_url *u);

/* Pause or un-pause the parser; a nonzero value pauses */
void http_parser_pause(http_parser *parser, int paused);

/* Checks if this is the final chunk of the body. */
int http_body_is_final(const http_parser *parser);

/* Change the maximum header size provided at compile time. */
void http_parser_set_max_header_size(uint32_t size);

#ifdef __cplusplus
}
#endif
#endif

