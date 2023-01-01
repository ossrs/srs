//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#ifndef SRS_PROTOCOL_HTTP_CONN_HPP
#define SRS_PROTOCOL_HTTP_CONN_HPP

#include <srs_core.hpp>

#include <string>
#include <sstream>

#include <srs_protocol_http_stack.hpp>

class ISrsConnection;
class SrsFastStream;
class SrsRequest;
class ISrsReader;
class SrsHttpResponseReader;
class ISrsProtocolReadWriter;

// A wrapper for http-parser,
// provides HTTP message originted service.
class SrsHttpParser
{
private:
    http_parser_settings settings;
    http_parser parser;
    // The global parse buffer.
    SrsFastStream* buffer;
    // Whether allow jsonp parse.
    bool jsonp;
private:
    std::string field_name;
    std::string field_value;
    SrsHttpParseState state;
    http_parser hp_header;
    std::string url;
    SrsHttpHeader* header;
    enum http_parser_type type_;
    enum http_parser_type parsed_type_;
public:
    SrsHttpParser();
    virtual ~SrsHttpParser();
public:
    // initialize the http parser with specified type,
    // one parser can only parse request or response messages.
    virtual srs_error_t initialize(enum http_parser_type type);
    // Whether allow jsonp parser, which indicates the method in query string.
    virtual void set_jsonp(bool allow_jsonp);
    // always parse a http message,
    // that is, the *ppmsg always NOT-NULL when return success.
    // or error and *ppmsg must be NULL.
    // @remark, if success, *ppmsg always NOT-NULL, *ppmsg always is_complete().
    // @remark user must free the ppmsg if not NULL.
    virtual srs_error_t parse_message(ISrsReader* reader, ISrsHttpMessage** ppmsg);
private:
    // parse the HTTP message to member field: msg.
    virtual srs_error_t parse_message_imp(ISrsReader* reader);
private:
    static int on_message_begin(http_parser* parser);
    static int on_headers_complete(http_parser* parser);
    static int on_message_complete(http_parser* parser);
    static int on_url(http_parser* parser, const char* at, size_t length);
    static int on_header_field(http_parser* parser, const char* at, size_t length);
    static int on_header_value(http_parser* parser, const char* at, size_t length);
    static int on_body(http_parser* parser, const char* at, size_t length);
};

// A Request represents an HTTP request received by a server
// or to be sent by a client.
//
// The field semantics differ slightly between client and server
// usage. In addition to the notes on the fields below, see the
// documentation for Request.Write and RoundTripper.
class SrsHttpMessage : public ISrsHttpMessage
{
private:
    // The body object, reader object.
    // @remark, user can get body in string by get_body().
    SrsHttpResponseReader* _body;
    // Use a buffer to read and send ts file.
    // The transport connection, can be NULL.
    ISrsConnection* owner_conn;
private:
    // The request type defined as
    //      enum http_parser_type { HTTP_REQUEST, HTTP_RESPONSE, HTTP_BOTH };
    uint8_t type_;
    // The HTTP method defined by HTTP_METHOD_MAP
    http_method _method;
    http_status _status;
    int64_t _content_length;
private:
    // The http headers
    SrsHttpHeader _header;
    // Whether the request indicates should keep alive for the http connection.
    bool _keep_alive;
    // Whether the body is chunked.
    bool chunked;
private:
    std::string schema_;
    // The parsed url.
    std::string _url;
    // The extension of file, for example, .flv
    std::string _ext;
    // The uri parser
    SrsHttpUri* _uri;
    // The query map
    std::map<std::string, std::string> _query;
private:
    // Whether request is jsonp.
    bool jsonp;
    // The method in QueryString will override the HTTP method.
    std::string jsonp_method;
public:
    SrsHttpMessage(ISrsReader* reader = NULL, SrsFastStream* buffer = NULL);
    virtual ~SrsHttpMessage();
public:
    // Set the basic information for HTTP request.
    // @remark User must call set_basic before set_header, because the content_length will be overwrite by header.
    virtual void set_basic(uint8_t type, http_method method, http_status status, int64_t content_length);
    // Set HTTP header and whether the request require keep alive.
    // @remark User must call set_header before set_url, because the Host in header is used for url.
    virtual void set_header(SrsHttpHeader* header, bool keep_alive);
    // set the original messages, then update the message.
    virtual srs_error_t set_url(std::string url, bool allow_jsonp);
    // After parsed the message, set the schema to https.
    virtual void set_https(bool v);
public:
    // Get the owner connection, maybe NULL.
    virtual ISrsConnection* connection();
    virtual void set_connection(ISrsConnection* conn);
public:
    // The schema, http or https.
    virtual std::string schema();
    virtual uint8_t message_type();
    virtual uint8_t method();
    virtual uint16_t status_code();
    // The method helpers.
    virtual std::string method_str();
    virtual bool is_http_get();
    virtual bool is_http_put();
    virtual bool is_http_post();
    virtual bool is_http_delete();
    virtual bool is_http_options();
    // Whether body is chunked encoding, for reader only.
    virtual bool is_chunked();
    // Whether should keep the connection alive.
    virtual bool is_keep_alive();
    // The uri contains the host and path.
    virtual std::string uri();
    // The url maybe the path.
    virtual std::string url();
    virtual std::string host();
    virtual int port();
    virtual std::string path();
    virtual std::string query();
    virtual std::string ext();
    // Get the RESTful matched id.
    virtual std::string parse_rest_id(std::string pattern);
public:
    // Read body to string.
    // @remark for small http body.
    virtual srs_error_t body_read_all(std::string& body);
    // Get the body reader, to read one by one.
    // @remark when body is very large, or chunked, use this.
    virtual ISrsHttpResponseReader* body_reader();
    // The content length, -1 for chunked or not set.
    virtual int64_t content_length();
    // Get the param in query string, for instance, query is "start=100&end=200",
    // then query_get("start") is "100", and query_get("end") is "200"
    virtual std::string query_get(std::string key);
    // Get the headers.
    virtual SrsHttpHeader* header();
public:
    // Convert the http message to a request.
    // @remark user must free the return request.
    virtual SrsRequest* to_request(std::string vhost);
public:
    virtual bool is_jsonp();
};

// The http chunked header size,
// for writev, there always one chunk to send it.
#define SRS_HTTP_HEADER_CACHE_SIZE 64

class ISrsHttpHeaderFilter
{
public:
    ISrsHttpHeaderFilter();
    virtual ~ISrsHttpHeaderFilter();
public:
    // Filter the HTTP header h.
    virtual srs_error_t filter(SrsHttpHeader* h) = 0;
};

class ISrsHttpFirstLineWriter
{
public:
    ISrsHttpFirstLineWriter();
    virtual ~ISrsHttpFirstLineWriter();
public:
    // Build first line of HTTP message to ss. Note that data with size of bytes is the body to write, which enables us
    // to setup the header by detecting the body, and it might be NULL.
    virtual srs_error_t build_first_line(std::stringstream& ss, char* data, int size) = 0;
    // Write a default header line if user does not specify one.
    virtual void write_default_header() = 0;
};

// Message writer use st socket, for writing HTTP request or response, which is only different at the first line. For
// HTTP request, the first line is RequestLine. While for HTTP response, it's StatusLine.
class SrsHttpMessageWriter
{
private:
    ISrsProtocolReadWriter* skt;
    SrsHttpHeader* hdr;
    // Before writing header, there is a chance to filter it,
    // such as remove some headers or inject new.
    ISrsHttpHeaderFilter* hf_;
    // The first line writer.
    ISrsHttpFirstLineWriter* flw_;
private:
    char header_cache[SRS_HTTP_HEADER_CACHE_SIZE];
    iovec* iovss_cache;
    int nb_iovss_cache;
private:
    // Reply header has been (logically) written
    bool header_wrote_;
private:
    // The explicitly-declared Content-Length; or -1
    int64_t content_length;
    // The number of bytes written in body
    int64_t written;
private:
    // The wroteHeader tells whether the header's been written to "the
    // wire" (or rather: w.conn.buf). this is unlike
    // (*response).wroteHeader, which tells only whether it was
    // logically written.
    bool header_sent;
public:
    SrsHttpMessageWriter(ISrsProtocolReadWriter* io, ISrsHttpFirstLineWriter* flw);
    virtual ~SrsHttpMessageWriter();
public:
    virtual srs_error_t final_request();
    virtual SrsHttpHeader* header();
    virtual srs_error_t write(char* data, int size);
    virtual srs_error_t writev(const iovec* iov, int iovcnt, ssize_t* pnwrite);
    virtual void write_header();
    virtual srs_error_t send_header(char* data, int size);
public:
    bool header_wrote();
    void set_header_filter(ISrsHttpHeaderFilter* hf);
};

// Response writer use st socket
class SrsHttpResponseWriter : public ISrsHttpResponseWriter, public ISrsHttpFirstLineWriter
{
protected:
    SrsHttpMessageWriter* writer_;
    // The status code passed to WriteHeader, for response only.
    int status;
public:
    SrsHttpResponseWriter(ISrsProtocolReadWriter* io);
    virtual ~SrsHttpResponseWriter();
public:
    void set_header_filter(ISrsHttpHeaderFilter* hf);
// Interface ISrsHttpResponseWriter
public:
    virtual srs_error_t final_request();
    virtual SrsHttpHeader* header();
    virtual srs_error_t write(char* data, int size);
    virtual srs_error_t writev(const iovec* iov, int iovcnt, ssize_t* pnwrite);
    virtual void write_header(int code);
// Interface ISrsHttpFirstLineWriter
public:
    virtual srs_error_t build_first_line(std::stringstream& ss, char* data, int size);
    virtual void write_default_header();
};

// Request writer use st socket
class SrsHttpRequestWriter : public ISrsHttpRequestWriter, public ISrsHttpFirstLineWriter
{
protected:
    SrsHttpMessageWriter* writer_;
    // The method and path passed to WriteHeader, for request only.
    std::string method_;
    std::string path_;
public:
    SrsHttpRequestWriter(ISrsProtocolReadWriter* io);
    virtual ~SrsHttpRequestWriter();
// Interface ISrsHttpResponseWriter
public:
    virtual srs_error_t final_request();
    virtual SrsHttpHeader* header();
    virtual srs_error_t write(char* data, int size);
    virtual srs_error_t writev(const iovec* iov, int iovcnt, ssize_t* pnwrite);
    virtual void write_header(const std::string& method, const std::string& path);
// Interface ISrsHttpFirstLineWriter
public:
    virtual srs_error_t build_first_line(std::stringstream& ss, char* data, int size);
    virtual void write_default_header();
};

// Response reader use st socket.
class SrsHttpResponseReader : public ISrsHttpResponseReader
{
private:
    ISrsReader* skt;
    SrsHttpMessage* owner;
    SrsFastStream* buffer;
    bool is_eof;
    // The left bytes in chunk.
    size_t nb_left_chunk;
    // The number of bytes of current chunk.
    size_t nb_chunk;
    // Already read total bytes.
    int64_t nb_total_read;
public:
    // Generally the reader is the under-layer io such as socket,
    // while buffer is a fast cache which may have cached some data from reader.
    SrsHttpResponseReader(SrsHttpMessage* msg, ISrsReader* reader, SrsFastStream* buffer);
    virtual ~SrsHttpResponseReader();
public:
    // User close the HTTP response reader.
    // For example, OPTIONS has no body, no content-length and not chunked,
    // so we must close it(set to eof) to avoid reading the response body.
    void close();
// Interface ISrsHttpResponseReader
public:
    virtual bool eof();
    virtual srs_error_t read(void* buf, size_t size, ssize_t* nread);
private:
    virtual srs_error_t read_chunked(void* buf, size_t size, ssize_t* nread);
    virtual srs_error_t read_specified(void* buf, size_t size, ssize_t* nread);
};

#endif

