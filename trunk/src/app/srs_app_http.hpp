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

#ifdef SRS_AUTO_HTTP_PARSER

#include <string>
#include <vector>
#include <sstream>

#include <http_parser.h>

#include <srs_app_st.hpp>

class SrsBuffer;
class SrsRequest;
class SrsSocket;
class SrsHttpUri;
class SrsHttpMessage;
class SrsHttpHandler;

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

// 6.1.1 Status Code and Reason Phrase
#define SRS_CONSTS_HTTP_Continue                       100
#define SRS_CONSTS_HTTP_SwitchingProtocols             101
#define SRS_CONSTS_HTTP_OK                             200
#define SRS_CONSTS_HTTP_Created                        201
#define SRS_CONSTS_HTTP_Accepted                       202
#define SRS_CONSTS_HTTP_NonAuthoritativeInformation    203
#define SRS_CONSTS_HTTP_NoContent                      204
#define SRS_CONSTS_HTTP_ResetContent                   205
#define SRS_CONSTS_HTTP_PartialContent                 206
#define SRS_CONSTS_HTTP_MultipleChoices                300
#define SRS_CONSTS_HTTP_MovedPermanently               301
#define SRS_CONSTS_HTTP_Found                          302
#define SRS_CONSTS_HTTP_SeeOther                       303
#define SRS_CONSTS_HTTP_NotModified                    304
#define SRS_CONSTS_HTTP_UseProxy                       305
#define SRS_CONSTS_HTTP_TemporaryRedirect              307
#define SRS_CONSTS_HTTP_BadRequest                     400
#define SRS_CONSTS_HTTP_Unauthorized                   401
#define SRS_CONSTS_HTTP_PaymentRequired                402
#define SRS_CONSTS_HTTP_Forbidden                      403
#define SRS_CONSTS_HTTP_NotFound                       404
#define SRS_CONSTS_HTTP_MethodNotAllowed               405
#define SRS_CONSTS_HTTP_NotAcceptable                  406
#define SRS_CONSTS_HTTP_ProxyAuthenticationRequired    407
#define SRS_CONSTS_HTTP_RequestTimeout                 408
#define SRS_CONSTS_HTTP_Conflict                       409
#define SRS_CONSTS_HTTP_Gone                           410
#define SRS_CONSTS_HTTP_LengthRequired                 411
#define SRS_CONSTS_HTTP_PreconditionFailed             412
#define SRS_CONSTS_HTTP_RequestEntityTooLarge          413
#define SRS_CONSTS_HTTP_RequestURITooLarge             414
#define SRS_CONSTS_HTTP_UnsupportedMediaType           415
#define SRS_CONSTS_HTTP_RequestedRangeNotSatisfiable   416
#define SRS_CONSTS_HTTP_ExpectationFailed              417
#define SRS_CONSTS_HTTP_InternalServerError            500
#define SRS_CONSTS_HTTP_NotImplemented                 501
#define SRS_CONSTS_HTTP_BadGateway                     502
#define SRS_CONSTS_HTTP_ServiceUnavailable             503
#define SRS_CONSTS_HTTP_GatewayTimeout                 504
#define SRS_CONSTS_HTTP_HTTPVersionNotSupported        505

#define SRS_CONSTS_HTTP_Continue_str                           "Continue"
#define SRS_CONSTS_HTTP_SwitchingProtocols_str                 "Switching Protocols"
#define SRS_CONSTS_HTTP_OK_str                                 "OK"
#define SRS_CONSTS_HTTP_Created_str                            "Created "
#define SRS_CONSTS_HTTP_Accepted_str                           "Accepted"
#define SRS_CONSTS_HTTP_NonAuthoritativeInformation_str        "Non Authoritative Information "
#define SRS_CONSTS_HTTP_NoContent_str                          "No Content "
#define SRS_CONSTS_HTTP_ResetContent_str                       "Reset Content"
#define SRS_CONSTS_HTTP_PartialContent_str                     "Partial Content"
#define SRS_CONSTS_HTTP_MultipleChoices_str                    "Multiple Choices "
#define SRS_CONSTS_HTTP_MovedPermanently_str                   "Moved Permanently"
#define SRS_CONSTS_HTTP_Found_str                              "Found"
#define SRS_CONSTS_HTTP_SeeOther_str                           "See Other"
#define SRS_CONSTS_HTTP_NotModified_str                        "Not Modified "
#define SRS_CONSTS_HTTP_UseProxy_str                           "Use Proxy"
#define SRS_CONSTS_HTTP_TemporaryRedirect_str                  "Temporary Redirect "
#define SRS_CONSTS_HTTP_BadRequest_str                         "Bad Request"
#define SRS_CONSTS_HTTP_Unauthorized_str                       "Unauthorized"
#define SRS_CONSTS_HTTP_PaymentRequired_str                    "Payment Required "
#define SRS_CONSTS_HTTP_Forbidden_str                          "Forbidden "
#define SRS_CONSTS_HTTP_NotFound_str                           "Not Found"
#define SRS_CONSTS_HTTP_MethodNotAllowed_str                   "Method Not Allowed"
#define SRS_CONSTS_HTTP_NotAcceptable_str                      "Not Acceptable "
#define SRS_CONSTS_HTTP_ProxyAuthenticationRequired_str        "Proxy Authentication Required "
#define SRS_CONSTS_HTTP_RequestTimeout_str                     "Request Timeout"
#define SRS_CONSTS_HTTP_Conflict_str                           "Conflict"
#define SRS_CONSTS_HTTP_Gone_str                               "Gone"
#define SRS_CONSTS_HTTP_LengthRequired_str                     "Length Required"
#define SRS_CONSTS_HTTP_PreconditionFailed_str                 "Precondition Failed"
#define SRS_CONSTS_HTTP_RequestEntityTooLarge_str              "Request Entity Too Large "
#define SRS_CONSTS_HTTP_RequestURITooLarge_str                 "Request URI Too Large"
#define SRS_CONSTS_HTTP_UnsupportedMediaType_str               "Unsupported Media Type"
#define SRS_CONSTS_HTTP_RequestedRangeNotSatisfiable_str       "Requested Range Not Satisfiable"
#define SRS_CONSTS_HTTP_ExpectationFailed_str                  "Expectation Failed "
#define SRS_CONSTS_HTTP_InternalServerError_str                "Internal Server Error "
#define SRS_CONSTS_HTTP_NotImplemented_str                     "Not Implemented"
#define SRS_CONSTS_HTTP_BadGateway_str                         "Bad Gateway"
#define SRS_CONSTS_HTTP_ServiceUnavailable_str                 "Service Unavailable"
#define SRS_CONSTS_HTTP_GatewayTimeout_str                     "Gateway Timeout"
#define SRS_CONSTS_HTTP_HTTPVersionNotSupported_str            "HTTP Version Not Supported"

// @see SrsHttpMessage._http_ts_send_buffer
#define SRS_CONSTS_HTTP_TS_SEND_BUFFER_SIZE 4096

// linux path seprator
#define __PATH_SEP '/'
// query string seprator
#define __QUERY_SEP '?'

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
    virtual int process_request(SrsSocket* skt, SrsHttpMessage* req);
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
    virtual int do_process_request(SrsSocket* skt, SrsHttpMessage* req);
    /**
    * response error, format as, for example:
    * {"code":100, "desc":"description"}
    */
    virtual int response_error(SrsSocket* skt, SrsHttpMessage* req, int code, std::string desc);
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
    virtual int res_flush(SrsSocket* skt, std::stringstream& ss);
public:
    virtual int res_options(SrsSocket* skt);
    virtual int res_text(SrsSocket* skt, SrsHttpMessage* req, std::string body);
    virtual int res_xml(SrsSocket* skt, SrsHttpMessage* req, std::string body);
    virtual int res_javascript(SrsSocket* skt, SrsHttpMessage* req, std::string body);
    virtual int res_swf(SrsSocket* skt, SrsHttpMessage* req, std::string body);
    virtual int res_css(SrsSocket* skt, SrsHttpMessage* req, std::string body);
    virtual int res_ico(SrsSocket* skt, SrsHttpMessage* req, std::string body);
    virtual int res_m3u8(SrsSocket* skt, SrsHttpMessage* req, std::string body);
    virtual int res_mpegts(SrsSocket* skt, SrsHttpMessage* req, std::string body);
    virtual int res_json(SrsSocket* skt, SrsHttpMessage* req, std::string json);
    virtual int res_error(SrsSocket* skt, SrsHttpMessage* req, int code, std::string reason_phrase, std::string body);
// object creator
public:
    /**
    * create http api resource handler.
    */
#ifdef SRS_AUTO_HTTP_API
    static SrsHttpHandler* create_http_api();
#endif
    /**
    * create http stream resource handler.
    */
#ifdef SRS_AUTO_HTTP_SERVER
    static SrsHttpHandler* create_http_stream();
#endif
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
public:
    SrsHttpMessage();
    virtual ~SrsHttpMessage();
public:
    virtual char* http_ts_send_buffer();
    virtual void reset();
    virtual int parse_uri();
public:
    virtual bool is_complete();
    virtual u_int8_t method();
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
    virtual std::string query();
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
public:
    /**
    * get the param in query string,
    * for instance, query is "start=100&end=200",
    * then query_get("start") is "100", and query_get("end") is "200"
    */
    virtual std::string query_get(std::string key);
public:
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