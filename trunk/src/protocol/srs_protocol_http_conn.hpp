//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_PROTOCOL_HTTP_CONN_HPP
#define SRS_PROTOCOL_HTTP_CONN_HPP

#include <srs_core.hpp>

#include <sstream>
#include <string>

#include <srs_protocol_http_stack.hpp>

class ISrsConnection;
class SrsFastStream;
class ISrsRequest;
class ISrsReader;
class SrsHttpResponseReader;
class ISrsProtocolReadWriter;
class SrsProtocolUtility;

class ISrsHttpParser
{
public:
    ISrsHttpParser();
    virtual ~ISrsHttpParser();

public:
    virtual srs_error_t initialize(enum llhttp_type type) = 0;
    // A JSONP request gives its HTTP method in the query string.
    virtual void set_jsonp(bool allow_jsonp) = 0;
    // The caller frees *ppmsg.
    virtual srs_error_t parse_message(ISrsReader *reader, ISrsHttpMessage **ppmsg) = 0;
};

class SrsHttpParser : public ISrsHttpParser
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    llhttp_settings_t settings_;
    llhttp_t parser_;
    SrsFastStream *buffer_;
    bool jsonp_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    std::string field_name_;
    std::string field_value_;
    SrsHttpParseState state_;
    llhttp_t hp_header_;
    std::string url_;
    SrsHttpHeader *header_;
    enum llhttp_type type_;
    enum llhttp_type parsed_type_;

public:
    SrsHttpParser();
    virtual ~SrsHttpParser();

public:
    virtual srs_error_t initialize(enum llhttp_type type);
    virtual void set_jsonp(bool allow_jsonp);
    virtual srs_error_t parse_message(ISrsReader *reader, ISrsHttpMessage **ppmsg);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t parse_message_imp(ISrsReader *reader);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    static int on_message_begin(llhttp_t *parser);
    static int on_headers_complete(llhttp_t *parser);
    static int on_message_complete(llhttp_t *parser);
    static int on_url(llhttp_t *parser, const char *at, size_t length);
    static int on_header_field(llhttp_t *parser, const char *at, size_t length);
    static int on_header_value(llhttp_t *parser, const char *at, size_t length);
    static int on_body(llhttp_t *parser, const char *at, size_t length);
};

class ISrsHttpMessageOwnerSetter : public ISrsHttpMessage
{
public:
    ISrsHttpMessageOwnerSetter();
    virtual ~ISrsHttpMessageOwnerSetter();

public:
    virtual void set_connection(ISrsConnection *conn) = 0;
    virtual ISrsRequest *to_request(std::string vhost) = 0;
};

class SrsHttpMessage : public ISrsHttpMessageOwnerSetter
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    SrsHttpResponseReader *body_;
    ISrsConnection *owner_conn_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // An llhttp_type value.
    uint8_t type_;
    llhttp_method_t method_;
    llhttp_status_t status_;
    int64_t content_length_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    SrsHttpHeader header_;
    bool keep_alive_;
    bool chunked_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    std::string schema_;
    std::string url_;
    // Such as .flv.
    std::string ext_;
    SrsHttpUri *uri_;
    std::map<std::string, std::string> query_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    bool jsonp_;
    std::string jsonp_method_;

public:
    SrsHttpMessage(ISrsReader *reader = NULL, SrsFastStream *buffer = NULL);
    virtual ~SrsHttpMessage();

public:
    // Call before set_header, which overwrites content_length from the header.
    virtual void set_basic(uint8_t type, llhttp_method_t method, llhttp_status_t status, int64_t content_length);
    // Call before set_url, which uses the Host header.
    virtual void set_header(SrsHttpHeader *header, bool keep_alive);
    virtual srs_error_t set_url(std::string url, bool allow_jsonp);
    // Call after parsing the message.
    virtual void set_https(bool v);

public:
    // It can be NULL.
    virtual ISrsConnection *connection();
    virtual void set_connection(ISrsConnection *conn);

public:
    virtual std::string schema();
    virtual uint8_t message_type();
    virtual uint8_t method();
    virtual uint16_t status_code();
    virtual std::string method_str();
    virtual bool is_http_get();
    virtual bool is_http_put();
    virtual bool is_http_post();
    virtual bool is_http_delete();
    virtual bool is_http_options();
    virtual bool is_chunked();
    virtual bool is_keep_alive();
    // The URI the client asked for, such as http://host:port/path?query.
    virtual std::string uri();
    virtual std::string url();
    virtual std::string host();
    virtual int port();
    virtual std::string path();
    virtual std::string query();
    virtual std::string ext();
    virtual std::string parse_rest_id(std::string pattern);

public:
    // For a small body only.
    virtual srs_error_t body_read_all(std::string &body);
    // For a large or chunked body.
    virtual ISrsHttpResponseReader *body_reader();
    // -1 when chunked or not set.
    virtual int64_t content_length();
    virtual std::string query_get(std::string key);
    virtual SrsHttpHeader *header();

public:
    // The caller frees the returned request.
    virtual ISrsRequest *to_request(std::string vhost);
    // The URL of the app on the HTTP server the client asked, such as http://127.0.0.1:8080/live: the
    // schema, the Host header as sent (or host() without one), and app. An HTTP client's tcUrl.
    std::string app_url(std::string app);

public:
    virtual bool is_jsonp();
};

// Holds the hex size line of one chunk.
#define SRS_HTTP_HEADER_CACHE_SIZE 64

class ISrsHttpHeaderFilter
{
public:
    ISrsHttpHeaderFilter();
    virtual ~ISrsHttpHeaderFilter();

public:
    virtual srs_error_t filter(SrsHttpHeader *h) = 0;
};

class ISrsHttpFirstLineWriter
{
public:
    ISrsHttpFirstLineWriter();
    virtual ~ISrsHttpFirstLineWriter();

public:
    // The data is the body to write, maybe NULL; it is used to detect the Content-Type.
    virtual srs_error_t build_first_line(std::stringstream &ss, char *data, int size) = 0;
    virtual void write_default_header() = 0;
};

// An HTTP request and response differ only in the first line: the request line or the status line.
class SrsHttpMessageWriter
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsProtocolReadWriter *skt_;
    SrsHttpHeader *hdr_;
    ISrsHttpHeaderFilter *hf_;
    ISrsHttpFirstLineWriter *flw_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    char header_cache_[SRS_HTTP_HEADER_CACHE_SIZE];
    iovec *iovss_cache_;
    int nb_iovss_cache_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // The header is written logically, not yet sent.
    bool header_wrote_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // The Content-Length set in the header, or -1.
    int64_t content_length_;
    int64_t written_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // The header is sent on the socket, unlike header_wrote_.
    bool header_sent_;
    SrsProtocolUtility *protocol_utility_;

public:
    SrsHttpMessageWriter(ISrsProtocolReadWriter *io, ISrsHttpFirstLineWriter *flw);
    virtual ~SrsHttpMessageWriter();

public:
    virtual srs_error_t final_request();
    virtual SrsHttpHeader *header();
    virtual srs_error_t write(char *data, int size);
    virtual srs_error_t writev(const iovec *iov, int iovcnt, ssize_t *pnwrite);
    virtual void write_header();
    virtual srs_error_t send_header(char *data, int size);

public:
    bool header_wrote();
    void set_header_filter(ISrsHttpHeaderFilter *hf);
};

class SrsHttpResponseWriter : public ISrsHttpResponseWriter, public ISrsHttpFirstLineWriter
{
// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    SrsHttpMessageWriter *writer_;
    int status_;

public:
    SrsHttpResponseWriter(ISrsProtocolReadWriter *io);
    virtual ~SrsHttpResponseWriter();

public:
    void set_header_filter(ISrsHttpHeaderFilter *hf);

public:
    virtual srs_error_t final_request();
    virtual SrsHttpHeader *header();
    virtual srs_error_t write(char *data, int size);
    virtual srs_error_t writev(const iovec *iov, int iovcnt, ssize_t *pnwrite);
    virtual void write_header(int code);
    virtual bool header_wrote();

public:
    virtual srs_error_t build_first_line(std::stringstream &ss, char *data, int size);
    virtual void write_default_header();
};

class SrsHttpRequestWriter : public ISrsHttpRequestWriter, public ISrsHttpFirstLineWriter
{
// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    SrsHttpMessageWriter *writer_;
    std::string method_;
    std::string path_;

public:
    SrsHttpRequestWriter(ISrsProtocolReadWriter *io);
    virtual ~SrsHttpRequestWriter();

public:
    virtual srs_error_t final_request();
    virtual SrsHttpHeader *header();
    virtual srs_error_t write(char *data, int size);
    virtual srs_error_t writev(const iovec *iov, int iovcnt, ssize_t *pnwrite);
    virtual void write_header(const std::string &method, const std::string &path);

public:
    virtual srs_error_t build_first_line(std::stringstream &ss, char *data, int size);
    virtual void write_default_header();
};

class SrsHttpResponseReader : public ISrsHttpResponseReader
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsReader *skt_;
    SrsHttpMessage *owner_;
    SrsFastStream *buffer_;
    bool is_eof_;
    size_t nb_left_chunk_;
    size_t nb_chunk_;
    int64_t nb_total_read_;

public:
    // The buffer may already hold body bytes read from the reader with the header.
    SrsHttpResponseReader(SrsHttpMessage *msg, ISrsReader *reader, SrsFastStream *buffer);
    virtual ~SrsHttpResponseReader();

public:
    // Marks EOF for a message with no body, such as an OPTIONS request with no Content-Length and not chunked.
    void close();

public:
    virtual bool eof();
    virtual srs_error_t read(void *buf, size_t size, ssize_t *nread);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t read_chunked(void *buf, size_t size, ssize_t *nread);
    virtual srs_error_t read_specified(void *buf, size_t size, ssize_t *nread);
};

#endif
