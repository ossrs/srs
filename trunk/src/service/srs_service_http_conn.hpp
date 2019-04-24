/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2019 Winlin
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef SRS_SERVICE_HTTP_CONN_HPP
#define SRS_SERVICE_HTTP_CONN_HPP

#include <srs_core.hpp>

#include <string>

#include <srs_http_stack.hpp>

class SrsConnection;
class SrsFastStream;
class SrsRequest;
class ISrsReader;
class SrsHttpResponseReader;
class SrsStSocket;

/**
 * wrapper for http-parser,
 * provides HTTP message originted service.
 */
class SrsHttpParser
{
private:
    http_parser_settings settings;
    http_parser parser;
    // the global parse buffer.
    SrsFastStream* buffer;
    // whether allow jsonp parse.
    bool jsonp;
private:
    // http parse data, reset before parse message.
    bool expect_field_name;
    std::string field_name;
    std::string field_value;
    SrsHttpParseState state;
    http_parser header;
    std::string url;
    std::vector<SrsHttpHeaderField> headers;
    int header_parsed;
public:
    SrsHttpParser();
    virtual ~SrsHttpParser();
public:
    /**
     * initialize the http parser with specified type,
     * one parser can only parse request or response messages.
     * @param allow_jsonp whether allow jsonp parser, which indicates the method in query string.
     */
    virtual srs_error_t initialize(enum http_parser_type type, bool allow_jsonp = false);
    /**
     * always parse a http message,
     * that is, the *ppmsg always NOT-NULL when return success.
     * or error and *ppmsg must be NULL.
     * @remark, if success, *ppmsg always NOT-NULL, *ppmsg always is_complete().
     * @remark user must free the ppmsg if not NULL.
     */
    virtual srs_error_t parse_message(ISrsReader* reader, ISrsHttpMessage** ppmsg);
private:
    /**
     * parse the HTTP message to member field: msg.
     */
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

// for http header.
typedef std::pair<std::string, std::string> SrsHttpHeaderField;

// A Request represents an HTTP request received by a server
// or to be sent by a client.
//
// The field semantics differ slightly between client and server
// usage. In addition to the notes on the fields below, see the
// documentation for Request.Write and RoundTripper.
class SrsHttpMessage : public ISrsHttpMessage
{
private:
    /**
     * parsed url.
     */
    std::string _url;
    /**
     * the extension of file, for example, .flv
     */
    std::string _ext;
    /**
     * parsed http header.
     */
    http_parser _header;
    /**
     * body object, reader object.
     * @remark, user can get body in string by get_body().
     */
    SrsHttpResponseReader* _body;
    /**
     * whether the body is chunked.
     */
    bool chunked;
    /**
     * whether the body is infinite chunked.
     */
    bool infinite_chunked;
    /**
     * whether the request indicates should keep alive
     * for the http connection.
     */
    bool keep_alive;
    /**
     * uri parser
     */
    SrsHttpUri* _uri;
    /**
     * use a buffer to read and send ts file.
     */
    // TODO: FIXME: remove it.
    char* _http_ts_send_buffer;
    // http headers
    std::vector<SrsHttpHeaderField> _headers;
    // the query map
    std::map<std::string, std::string> _query;
    // the transport connection, can be NULL.
    SrsConnection* owner_conn;
    // whether request is jsonp.
    bool jsonp;
    // the method in QueryString will override the HTTP method.
    std::string jsonp_method;
public:
    SrsHttpMessage(ISrsReader* io);
    virtual ~SrsHttpMessage();
public:
    /**
     * set the original messages, then update the message.
     */
    virtual srs_error_t update(std::string url, bool allow_jsonp, http_parser* header, SrsFastStream* body, std::vector<SrsHttpHeaderField>& headers);
public:
    // Get the owner connection, maybe NULL.
    virtual SrsConnection* connection();
    virtual void set_connection(SrsConnection* conn);
public:
    virtual uint8_t method();
    virtual uint16_t status_code();
    /**
     * method helpers.
     */
    virtual std::string method_str();
    virtual bool is_http_get();
    virtual bool is_http_put();
    virtual bool is_http_post();
    virtual bool is_http_delete();
    virtual bool is_http_options();
    /**
     * whether body is chunked encoding, for reader only.
     */
    virtual bool is_chunked();
    /**
     * whether body is infinite chunked encoding.
     * @remark set by enter_infinite_chunked.
     */
    virtual bool is_infinite_chunked();
    /**
     * whether should keep the connection alive.
     */
    virtual bool is_keep_alive();
    /**
     * the uri contains the host and path.
     */
    virtual std::string uri();
    /**
     * the url maybe the path.
     */
    virtual std::string url();
    virtual std::string host();
    virtual std::string path();
    virtual std::string query();
    virtual std::string ext();
    /**
     * get the RESTful matched id.
     */
    virtual int parse_rest_id(std::string pattern);
public:
    virtual srs_error_t enter_infinite_chunked();
public:
    /**
     * read body to string.
     * @remark for small http body.
     */
    virtual srs_error_t body_read_all(std::string& body);
    /**
     * get the body reader, to read one by one.
     * @remark when body is very large, or chunked, use this.
     */
    virtual ISrsHttpResponseReader* body_reader();
    /**
     * the content length, -1 for chunked or not set.
     */
    virtual int64_t content_length();
    /**
     * get the param in query string,
     * for instance, query is "start=100&end=200",
     * then query_get("start") is "100", and query_get("end") is "200"
     */
    virtual std::string query_get(std::string key);
    /**
     * get the headers.
     */
    virtual int request_header_count();
    virtual std::string request_header_key_at(int index);
    virtual std::string request_header_value_at(int index);
    virtual std::string get_request_header(std::string name);
public:
    /**
     * convert the http message to a request.
     * @remark user must free the return request.
     */
    virtual SrsRequest* to_request(std::string vhost);
public:
    virtual bool is_jsonp();
};

// the http chunked header size,
// for writev, there always one chunk to send it.
#define SRS_HTTP_HEADER_CACHE_SIZE 64

/**
 * response writer use st socket
 */
class SrsHttpResponseWriter : public ISrsHttpResponseWriter
{
private:
    SrsStSocket* skt;
    SrsHttpHeader* hdr;
private:
    char header_cache[SRS_HTTP_HEADER_CACHE_SIZE];
    iovec* iovss_cache;
    int nb_iovss_cache;
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
    virtual srs_error_t final_request();
    virtual SrsHttpHeader* header();
    virtual srs_error_t write(char* data, int size);
    virtual srs_error_t writev(const iovec* iov, int iovcnt, ssize_t* pnwrite);
    virtual void write_header(int code);
    virtual srs_error_t send_header(char* data, int size);
};

/**
 * response reader use st socket.
 */
class SrsHttpResponseReader : virtual public ISrsHttpResponseReader
{
private:
    ISrsReader* skt;
    SrsHttpMessage* owner;
    SrsFastStream* buffer;
    bool is_eof;
    // the left bytes in chunk.
    int nb_left_chunk;
    // the number of bytes of current chunk.
    int nb_chunk;
    // already read total bytes.
    int64_t nb_total_read;
public:
    SrsHttpResponseReader(SrsHttpMessage* msg, ISrsReader* reader);
    virtual ~SrsHttpResponseReader();
public:
    /**
     * initialize the response reader with buffer.
     */
    virtual srs_error_t initialize(SrsFastStream* buffer);
    // interface ISrsHttpResponseReader
public:
    virtual bool eof();
    virtual srs_error_t read(char* data, int nb_data, int* nb_read);
private:
    virtual srs_error_t read_chunked(char* data, int nb_data, int* nb_read);
    virtual srs_error_t read_specified(char* data, int nb_data, int* nb_read);
};

#endif

