//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_PROTOCOL_HTTP_HPP
#define SRS_PROTOCOL_HTTP_HPP

#include <srs_core.hpp>

#include <srs_kernel_io.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_http_stack_llhttp.hpp>

#include <map>
#include <string>
#include <vector>

// For srs-librtmp, @see https://github.com/ossrs/srs/issues/213
#ifndef _WIN32
#endif

class SrsFileReader;
class SrsHttpHeader;
class ISrsHttpMessage;
class SrsHttpMuxEntry;
class ISrsHttpResponseWriter;
class SrsJsonObject;
class ISrsFileReaderFactory;

#define SRS_HTTP_CR SRS_CONSTS_CR
#define SRS_HTTP_LF SRS_CONSTS_LF
#define SRS_HTTP_SP ' '
#define SRS_HTTP_HT '\x09'

// HTTP/1.1 ends every protocol element except the entity-body with CR LF (RFC 2616 2.2).
#define SRS_HTTP_CRLF "\r\n"
#define SRS_HTTP_CRLFCRLF "\r\n\r\n"

#define SRS_CONSTS_HTTP_OPTIONS HTTP_OPTIONS
#define SRS_CONSTS_HTTP_GET HTTP_GET
#define SRS_CONSTS_HTTP_POST HTTP_POST
#define SRS_CONSTS_HTTP_PUT HTTP_PUT
#define SRS_CONSTS_HTTP_DELETE HTTP_DELETE

extern srs_error_t srs_go_http_error(ISrsHttpResponseWriter *w, int code);
extern srs_error_t srs_go_http_error(ISrsHttpResponseWriter *w, int code, std::string error);

extern std::string srs_generate_http_status_text(int status);

// See RFC 2616, section 4.4.
extern bool srs_go_http_body_allowd(int status);

// The MIME sniffing algorithm of http://mimesniff.spec.whatwg.org/ is not implemented, so it always returns
// application/octet-stream.
extern std::string srs_go_http_detect(char *data, int size);

enum SrsHttpParseState {
    SrsHttpParseStateInit = 0,
    SrsHttpParseStateStart,
    SrsHttpParseStateHeaderComplete,
    SrsHttpParseStateBody,
    SrsHttpParseStateMessageComplete
};

class SrsHttpHeader
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // The order of differing field names is not significant, but it is good practice to send the general
    // fields first, then the request or response fields, and the entity fields last.
    // @doc https://tools.ietf.org/html/rfc2616#section-4.2
    std::map<std::string, std::string>
        headers;
    // Keeps the fields in order.
    std::vector<std::string> keys_;

public:
    SrsHttpHeader();
    virtual ~SrsHttpHeader();

public:
    virtual void set(std::string key, std::string value);
    virtual std::string get(std::string key);
    virtual void del(std::string);
    virtual int count();

public:
    virtual void dumps(SrsJsonObject *o);

public:
    virtual int64_t content_length();
    virtual void set_content_length(int64_t size);

public:
    virtual std::string content_type();
    virtual void set_content_type(std::string ct);

public:
    virtual void write(std::stringstream &ss);
};

class ISrsHttpResponseWriter
{
public:
    ISrsHttpResponseWriter();
    virtual ~ISrsHttpResponseWriter();

public:
    virtual srs_error_t final_request() = 0;

    virtual SrsHttpHeader *header() = 0;

    virtual srs_error_t write(char *data, int size) = 0;
    // For the HTTP FLV, to improve performance.
    virtual srs_error_t writev(const iovec *iov, int iovcnt, ssize_t *pnwrite) = 0;

    // Set the Content-Length before this call; without it, the body is chunked, and final_request() must end it.
    virtual void write_header(int code) = 0;

    // Whether the response header is already sent to the client. Once sent, the status can never be
    // changed, so an error handler must not try to respond again.
    virtual bool header_wrote() = 0;
};

class ISrsHttpResponseReader : public ISrsReader
{
public:
    ISrsHttpResponseReader();
    virtual ~ISrsHttpResponseReader();

public:
    virtual bool eof() = 0;
};

class ISrsHttpRequestWriter
{
public:
    ISrsHttpRequestWriter();
    virtual ~ISrsHttpRequestWriter();

public:
    virtual srs_error_t final_request() = 0;

    virtual SrsHttpHeader *header() = 0;

    virtual srs_error_t write(char *data, int size) = 0;
    // For the HTTP FLV, to improve performance.
    virtual srs_error_t writev(const iovec *iov, int iovcnt, ssize_t *pnwrite) = 0;

    // Set the Content-Length before this call; without it, the body is chunked, and final_request() must end it.
    virtual void write_header(const std::string &method, const std::string &path) = 0;
};

class ISrsHttpHandler
{
public:
    SrsHttpMuxEntry *entry_;

public:
    ISrsHttpHandler();
    virtual ~ISrsHttpHandler();

public:
    virtual bool is_not_found();
    virtual srs_error_t serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r) = 0;
};

class SrsHttpRedirectHandler : public ISrsHttpHandler
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    std::string url;
    int code;

public:
    SrsHttpRedirectHandler(std::string u, int c);
    virtual ~SrsHttpRedirectHandler();

public:
    virtual srs_error_t serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r);
};

class SrsHttpNotFoundHandler : public ISrsHttpHandler
{
public:
    SrsHttpNotFoundHandler();
    virtual ~SrsHttpNotFoundHandler();

public:
    virtual bool is_not_found();
    virtual srs_error_t serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r);
};

extern std::string srs_http_fs_fullpath(std::string dir, std::string pattern, std::string upath);

class SrsHttpFileServer : public ISrsHttpHandler
{
// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    std::string dir;

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    ISrsFileReaderFactory *fs_factory;
    SrsPath *path_;

public:
    SrsHttpFileServer(std::string root_dir);
    virtual ~SrsHttpFileServer();

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // For utest to mock the fs.
    virtual void set_fs_factory(ISrsFileReaderFactory *v);
    // For utest to mock the path utility.
    virtual void set_path(SrsPath *v);

public:
    virtual srs_error_t serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t serve_file(ISrsHttpResponseWriter *w, ISrsHttpMessage *r, std::string fullpath);
    virtual srs_error_t serve_flv_file(ISrsHttpResponseWriter *w, ISrsHttpMessage *r, std::string fullpath);
    virtual srs_error_t serve_mp4_file(ISrsHttpResponseWriter *w, ISrsHttpMessage *r, std::string fullpath);

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual srs_error_t serve_flv_stream(ISrsHttpResponseWriter *w, ISrsHttpMessage *r, std::string fullpath, int64_t offset);
    // The bytes in [start, end] are sent; end is -1 for the end of file.
    virtual srs_error_t serve_mp4_stream(ISrsHttpResponseWriter *w, ISrsHttpMessage *r, std::string fullpath, int64_t start, int64_t end);
    // HLS requests are short connections, so SRS answers http://127.0.0.1:8080/live/livestream.m3u8 with
    // http://127.0.0.1:8080/live/livestream.m3u8?hls_ctx=12345678 and tracks the later requests of the client by
    // hls_ctx. A client may set its own hls_ctx in the first request, and it must be unique: requests with the same
    // hls_ctx are treated as one client.
    virtual srs_error_t serve_m3u8_ctx(ISrsHttpResponseWriter *w, ISrsHttpMessage *r, std::string fullpath);
    virtual srs_error_t serve_ts_ctx(ISrsHttpResponseWriter *w, ISrsHttpMessage *r, std::string fullpath);

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual srs_error_t copy(ISrsHttpResponseWriter *w, SrsFileReader *fs, ISrsHttpMessage *r, int64_t size);
};

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

class ISrsHttpDynamicMatcher
{
public:
    ISrsHttpDynamicMatcher();
    virtual ~ISrsHttpDynamicMatcher();

public:
    // Called for every request; ph is the handler the patterns matched, or NULL, and the matcher may replace it.
    virtual srs_error_t dynamic_match(ISrsHttpMessage *request, ISrsHttpHandler **ph) = 0;
};

class ISrsCommonHttpHandler : public ISrsHttpHandler
{
public:
    ISrsCommonHttpHandler();
    virtual ~ISrsCommonHttpHandler();

public:
    virtual srs_error_t handle(std::string pattern, ISrsHttpHandler *handler) = 0;
};

class ISrsHttpServeMux : public ISrsCommonHttpHandler
{
public:
    ISrsHttpServeMux();
    virtual ~ISrsHttpServeMux();

public:
    virtual srs_error_t find_handler(ISrsHttpMessage *r, ISrsHttpHandler **ph) = 0;
    virtual void unhandle(std::string pattern, ISrsHttpHandler *handler) = 0;
};

class SrsHttpServeMux : public ISrsHttpServeMux
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    std::map<std::string, SrsHttpMuxEntry *>
        static_matchers_;
    // The hosts of the patterns that start with a vhost, such as ossrs.net of ossrs.net/live/livestream.flv. A vhost
    // pattern matches only its host and takes precedence, so "codesearch.google.com/" next to "/codesearch" does not
    // take over requests for "http://www.google.com/".
    std::map<std::string, ISrsHttpHandler *> vhosts_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // Such as the HTTP stream server, which installs the handler of a stream for a request that has none.
    std::vector<ISrsHttpDynamicMatcher *>
        dynamic_matchers_;

public:
    SrsHttpServeMux();
    virtual ~SrsHttpServeMux();

public:
    virtual srs_error_t initialize();

public:
    virtual void add_dynamic_matcher(ISrsHttpDynamicMatcher *h);
    virtual void remove_dynamic_matcher(ISrsHttpDynamicMatcher *h);

public:
    virtual srs_error_t handle(std::string pattern, ISrsHttpHandler *handler);
    // It does not free the handler.
    void unhandle(std::string pattern, ISrsHttpHandler *handler);

public:
    virtual srs_error_t serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r);

public:
    virtual srs_error_t find_handler(ISrsHttpMessage *r, ISrsHttpHandler **ph);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t match(ISrsHttpMessage *r, ISrsHttpHandler **ph);
    virtual bool path_match(std::string pattern, std::string path);
};

class ISrsHttpCorsMux : public ISrsHttpHandler
{
public:
    ISrsHttpCorsMux();
    virtual ~ISrsHttpCorsMux();

public:
    virtual srs_error_t initialize(bool cros_enabled) = 0;
};

class SrsHttpCorsMux : public ISrsHttpCorsMux
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    bool required;
    bool enabled;
    ISrsHttpHandler *next_;

public:
    SrsHttpCorsMux(ISrsHttpHandler *h);
    virtual ~SrsHttpCorsMux();

public:
    virtual srs_error_t initialize(bool cros_enabled);

public:
    virtual srs_error_t serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r);
};

class ISrsHttpAuthenticator
{
public:
    ISrsHttpAuthenticator();
    virtual ~ISrsHttpAuthenticator();

public:
    virtual bool match(ISrsHttpMessage *r) = 0;
    virtual srs_error_t authenticate(ISrsHttpResponseWriter *w, ISrsHttpMessage *r) = 0;
};

// @see https://www.rfc-editor.org/rfc/rfc7617
class SrsHttpBasicAuthenticator : public ISrsHttpAuthenticator
{
private:
    std::string username_;
    std::string password_;

public:
    SrsHttpBasicAuthenticator(std::string username, std::string password);
    virtual ~SrsHttpBasicAuthenticator();

public:
    virtual bool match(ISrsHttpMessage *r);
    virtual srs_error_t authenticate(ISrsHttpResponseWriter *w, ISrsHttpMessage *r);

private:
    srs_error_t do_authenticate(ISrsHttpMessage *r);
};

// @see https://www.rfc-editor.org/rfc/rfc6750
class SrsHttpBearerAuthenticator : public ISrsHttpAuthenticator
{
private:
    std::string token_;
    bool rtc_bearer_enabled_;

public:
    SrsHttpBearerAuthenticator(std::string token, bool rtc_bearer_enabled);
    virtual ~SrsHttpBearerAuthenticator();

public:
    virtual bool match(ISrsHttpMessage *r);
    virtual srs_error_t authenticate(ISrsHttpResponseWriter *w, ISrsHttpMessage *r);
};

class ISrsHttpAuthMux : public ISrsHttpHandler
{
public:
    ISrsHttpAuthMux();
    virtual ~ISrsHttpAuthMux();

public:
    // The mux takes ownership of authenticator on success. Use NULL to disable authentication.
    virtual srs_error_t initialize(ISrsHttpAuthenticator *authenticator) = 0;
};

// @see https://developer.mozilla.org/en-US/docs/Web/HTTP/Headers/WWW-Authenticate
class SrsHttpAuthMux : public ISrsHttpAuthMux
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsHttpAuthenticator *authenticator_;
    ISrsHttpHandler *next_;

public:
    SrsHttpAuthMux(ISrsHttpHandler *h);
    virtual ~SrsHttpAuthMux();

public:
    virtual srs_error_t initialize(ISrsHttpAuthenticator *authenticator);

public:
    virtual srs_error_t serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t do_auth(ISrsHttpResponseWriter *w, ISrsHttpMessage *r);
};

class ISrsHttpMessage
{
public:
    ISrsHttpMessage();
    virtual ~ISrsHttpMessage();

public:
    virtual uint8_t message_type() = 0;
    virtual uint8_t method() = 0;
    virtual uint16_t status_code() = 0;
    virtual std::string method_str() = 0;
    virtual bool is_http_get() = 0;
    virtual bool is_http_put() = 0;
    virtual bool is_http_post() = 0;
    virtual bool is_http_delete() = 0;
    virtual bool is_http_options() = 0;

public:
    virtual bool is_keep_alive() = 0;
    // The uri the client asked for, such as http://host:port/path?query.
    virtual std::string uri() = 0;
    virtual std::string url() = 0;
    virtual std::string host() = 0;
    virtual std::string path() = 0;
    virtual std::string query() = 0;
    virtual std::string ext() = 0;
    // Such as 100 for pattern /api/v1/streams and path /api/v1/streams/100; empty if not matched.
    virtual std::string parse_rest_id(std::string pattern) = 0;

public:
    // For a small body; use body_reader() for a large or chunked one.
    virtual srs_error_t body_read_all(std::string &body) = 0;
    virtual ISrsHttpResponseReader *body_reader() = 0;
    // -1 for chunked or not set.
    virtual int64_t content_length() = 0;

public:
    virtual std::string query_get(std::string key) = 0;
    virtual SrsHttpHeader *header() = 0;

public:
    // A JSONP request has callback=xxx in the query string.
    virtual bool is_jsonp() = 0;
};

class SrsHttpUri
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    std::string url_;
    std::string schema_;
    std::string host_;
    int port_;
    std::string path_;
    std::string query_;
    std::string fragment_;
    std::string username_;
    std::string password_;
    std::map<std::string, std::string> query_values_;

public:
    SrsHttpUri();
    virtual ~SrsHttpUri();

public:
    virtual srs_error_t initialize(std::string url);
    virtual void set_schema(std::string v);

public:
    virtual std::string get_url();
    virtual std::string get_schema();
    virtual std::string get_host();
    virtual int get_port();
    virtual std::string get_path();
    virtual std::string get_query();
    // The path and the query to request the url with, such as /api?k=v; the
    // path is / when the url has none.
    virtual std::string get_request_target();
    virtual std::string get_query_by_key(std::string key);
    virtual std::string get_fragment();
    virtual std::string username();
    virtual std::string password();

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // Replaces the URL parsing of http-parser.
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

extern srs_error_t srs_av_base64_decode(std::string cipher, std::string &plaintext);
extern srs_error_t srs_av_base64_encode(std::string plaintext, std::string &cipher);

#endif
