//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_PROTOCOL_HTTP_HPP
#define SRS_PROTOCOL_HTTP_HPP

#include <srs_core.hpp>

#include <srs_kernel_io.hpp>
#include <srs_protocol_http_stack_llhttp.hpp>

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
#define SRS_HTTP_CRLF "\r\n"         // 0x0D0A
#define SRS_HTTP_CRLFCRLF "\r\n\r\n" // 0x0D0A0D0A

// For http parser macros
#define SRS_CONSTS_HTTP_OPTIONS HTTP_OPTIONS
#define SRS_CONSTS_HTTP_GET HTTP_GET
#define SRS_CONSTS_HTTP_POST HTTP_POST
#define SRS_CONSTS_HTTP_PUT HTTP_PUT
#define SRS_CONSTS_HTTP_DELETE HTTP_DELETE

// Error replies to the request with the specified error message and HTTP code.
// The error message should be plain text.
extern srs_error_t srs_go_http_error(ISrsHttpResponseWriter *w, int code);
extern srs_error_t srs_go_http_error(ISrsHttpResponseWriter *w, int code, std::string error);

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
extern std::string srs_go_http_detect(char *data, int size);

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
    virtual void dumps(SrsJsonObject *o);

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
    virtual void write(std::stringstream &ss);
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
    virtual SrsHttpHeader *header() = 0;

    // Write writes the data to the connection as part of an HTTP reply.
    // If WriteHeader has not yet been called, Write calls WriteHeader(http.StatusOK)
    // before writing the data.  If the Header does not contain a
    // Content-Type line, Write adds a Content-Type set to the result of passing
    // The initial 512 bytes of written data to DetectContentType.
    // @param data, the data to send. NULL to flush header only.
    virtual srs_error_t write(char *data, int size) = 0;
    // for the HTTP FLV, to writev to improve performance.
    virtual srs_error_t writev(const iovec *iov, int iovcnt, ssize_t *pnwrite) = 0;

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
    virtual SrsHttpHeader *header() = 0;

    // Write writes the data to the connection as part of an HTTP reply.
    // If WriteHeader has not yet been called, Write calls WriteHeader(http.StatusOK)
    // before writing the data.  If the Header does not contain a
    // Content-Type line, Write adds a Content-Type set to the result of passing
    // The initial 512 bytes of written data to DetectContentType.
    // @param data, the data to send. NULL to flush header only.
    virtual srs_error_t write(char *data, int size) = 0;
    // for the HTTP FLV, to writev to improve performance.
    virtual srs_error_t writev(const iovec *iov, int iovcnt, ssize_t *pnwrite) = 0;

    // WriteHeader sends an HTTP request header with status code.
    // If WriteHeader is not called explicitly, the first call to Write
    // will trigger an implicit WriteHeader(http.StatusOK).
    // Thus explicit calls to WriteHeader are mainly used to
    // send error codes.
    // @remark, user must set header then write or write_header.
    virtual void write_header(const std::string &method, const std::string &path) = 0;
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
    SrsHttpMuxEntry *entry;

public:
    ISrsHttpHandler();
    virtual ~ISrsHttpHandler();

public:
    virtual bool is_not_found();
    virtual srs_error_t serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r) = 0;
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
    virtual srs_error_t serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r);
};

// NotFound replies to the request with an HTTP 404 not found error.
class SrsHttpNotFoundHandler : public ISrsHttpHandler
{
public:
    SrsHttpNotFoundHandler();
    virtual ~SrsHttpNotFoundHandler();

public:
    virtual bool is_not_found();
    virtual srs_error_t serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r);
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
    ISrsFileReaderFactory *fs_factory;
    _pfn_srs_path_exists _srs_path_exists;

public:
    SrsHttpFileServer(std::string root_dir);
    virtual ~SrsHttpFileServer();

private:
    // For utest to mock the fs.
    virtual void set_fs_factory(ISrsFileReaderFactory *v);
    // For utest to mock the path check function.
    virtual void set_path_check(_pfn_srs_path_exists pfn);

public:
    virtual srs_error_t serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r);

private:
    // Serve the file by specified path
    virtual srs_error_t serve_file(ISrsHttpResponseWriter *w, ISrsHttpMessage *r, std::string fullpath);
    virtual srs_error_t serve_flv_file(ISrsHttpResponseWriter *w, ISrsHttpMessage *r, std::string fullpath);
    virtual srs_error_t serve_mp4_file(ISrsHttpResponseWriter *w, ISrsHttpMessage *r, std::string fullpath);

protected:
    // When access flv file with x.flv?start=xxx
    virtual srs_error_t serve_flv_stream(ISrsHttpResponseWriter *w, ISrsHttpMessage *r, std::string fullpath, int64_t offset);
    // When access mp4 file with x.mp4?range=start-end
    // @param start the start offset in bytes.
    // @param end the end offset in bytes. -1 to end of file.
    // @remark response data in [start, end].
    virtual srs_error_t serve_mp4_stream(ISrsHttpResponseWriter *w, ISrsHttpMessage *r, std::string fullpath, int64_t start, int64_t end);
    // For HLS protocol.
    // When the request url, like as "http://127.0.0.1:8080/live/livestream.m3u8",
    // returns the response like as "http://127.0.0.1:8080/live/livestream.m3u8?hls_ctx=12345678" .
    // SRS use "hls_ctx" to keep track of subsequent requests that is short-connection.
    // Remark 1:
    //           Fill the parameter "hls_ctx" by yourself in the first request is allowed, SRS will use it.
    //           And MUST make sure it is unique.
    // Remark 2:
    //           If use two same "hls_ctx" in different requests, SRS cannot detect so that they will be treated as one.
    virtual srs_error_t serve_m3u8_ctx(ISrsHttpResponseWriter *w, ISrsHttpMessage *r, std::string fullpath);
    // the ts file including: .ts .m4s init.mp4
    virtual srs_error_t serve_ts_ctx(ISrsHttpResponseWriter *w, ISrsHttpMessage *r, std::string fullpath);

protected:
    // Copy the fs to response writer in size bytes.
    virtual srs_error_t copy(ISrsHttpResponseWriter *w, SrsFileReader *fs, ISrsHttpMessage *r, int64_t size);
};

// The mux entry for server mux.
// The matcher info, for example, the pattern and handler.
class SrsHttpMuxEntry
{
public:
    bool explicit_match;
    ISrsHttpHandler *handler;
    std::string pattern;
    bool enabled;

public:
    SrsHttpMuxEntry();
    virtual ~SrsHttpMuxEntry();
};

// The dynamic matcher for http pattern match.
class ISrsHttpDynamicMatcher
{
public:
    ISrsHttpDynamicMatcher();
    virtual ~ISrsHttpDynamicMatcher();

public:
    // When match the request failed, no handler to process request.
    // @param request the http request message to match the handler.
    // @param ph the already matched handler, dynamic matcher can rewrite it.
    virtual srs_error_t dynamic_match(ISrsHttpMessage *request, ISrsHttpHandler **ph) = 0;
};

// The server mux, all http server should implements it.
class ISrsHttpServeMux : public ISrsHttpHandler
{
public:
    ISrsHttpServeMux();
    virtual ~ISrsHttpServeMux();

public:
    // Register HTTP handler to mux.
    virtual srs_error_t handle(std::string pattern, ISrsHttpHandler *handler) = 0;
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
    std::map<std::string, SrsHttpMuxEntry *> static_matchers_;
    // The vhost handler.
    // When find the handler to process the request,
    // append the matched vhost when pattern not starts with /,
    // For example, for pattern /live/livestream.flv of vhost ossrs.net,
    // The path will rewrite to ossrs.net/live/livestream.flv
    std::map<std::string, ISrsHttpHandler *> vhosts_;

private:
    // all dynamic matcher for http match.
    // For example, the hstrs(http stream trigger rtmp source)
    // can dynamic match and install handler when request incoming and no handler.
    std::vector<ISrsHttpDynamicMatcher *> dynamic_matchers_;

public:
    SrsHttpServeMux();
    virtual ~SrsHttpServeMux();

public:
    // Initialize the http serve mux.
    virtual srs_error_t initialize();

public:
    // Add a dynamic matcher for the http match.
    virtual void add_dynamic_matcher(ISrsHttpDynamicMatcher *h);
    virtual void remove_dynamic_matcher(ISrsHttpDynamicMatcher *h);

public:
    // Handle registers the handler for the given pattern.
    // If a handler already exists for pattern, Handle panics.
    virtual srs_error_t handle(std::string pattern, ISrsHttpHandler *handler);
    // Remove the handler for pattern. Note that this will not free the handler.
    void unhandle(std::string pattern, ISrsHttpHandler *handler);
    // Interface ISrsHttpServeMux
public:
    virtual srs_error_t serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r);

public:
    virtual srs_error_t find_handler(ISrsHttpMessage *r, ISrsHttpHandler **ph);

private:
    virtual srs_error_t match(ISrsHttpMessage *r, ISrsHttpHandler **ph);
    virtual bool path_match(std::string pattern, std::string path);
};

// The filter http mux, directly serve the http CORS requests
class SrsHttpCorsMux : public ISrsHttpHandler
{
private:
    bool required;
    bool enabled;
    ISrsHttpHandler *next_;

public:
    SrsHttpCorsMux(ISrsHttpHandler *h);
    virtual ~SrsHttpCorsMux();

public:
    virtual srs_error_t initialize(bool cros_enabled);
    // Interface ISrsHttpServeMux
public:
    virtual srs_error_t serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r);
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
    ISrsHttpHandler *next_;

public:
    SrsHttpAuthMux(ISrsHttpHandler *h);
    virtual ~SrsHttpAuthMux();

public:
    virtual srs_error_t initialize(bool enabled, std::string username, std::string password);
    // Interface ISrsHttpServeMux
public:
    virtual srs_error_t serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r);

private:
    virtual srs_error_t do_auth(ISrsHttpResponseWriter *w, ISrsHttpMessage *r);
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
    virtual srs_error_t body_read_all(std::string &body) = 0;
    // Get the body reader, to read one by one.
    // @remark when body is very large, or chunked, use this.
    virtual ISrsHttpResponseReader *body_reader() = 0;
    // The content length, -1 for chunked or not set.
    virtual int64_t content_length() = 0;

public:
    // Get the param in query string,
    // for instance, query is "start=100&end=200",
    // then query_get("start") is "100", and query_get("end") is "200"
    virtual std::string query_get(std::string key) = 0;
    // Get the headers.
    virtual SrsHttpHeader *header() = 0;

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
    // Simple URL parser to replace http-parser URL parsing
    virtual srs_error_t parse_url_simple(const std::string &url, std::string &schema, std::string &host, int &port,
                                         std::string &path, std::string &query, std::string &fragment,
                                         std::string &username, std::string &password);
    srs_error_t parse_query();

public:
    static std::string query_escape(std::string s);
    static std::string path_escape(std::string s);
    static srs_error_t query_unescape(std::string s, std::string &value);
    static srs_error_t path_unescape(std::string s, std::string &value);
};

// Decode a base64-encoded string.
extern srs_error_t srs_av_base64_decode(std::string cipher, std::string &plaintext);
// Encode a plaintext to  base64-encoded string.
extern srs_error_t srs_av_base64_encode(std::string plaintext, std::string &cipher);

// For #ifndef SRS_PROTOCOL_HTTP_HPP
#endif
