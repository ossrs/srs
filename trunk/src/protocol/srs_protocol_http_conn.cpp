//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_protocol_http_conn.hpp>

#include <sstream>
#include <stdlib.h>
using namespace std;

#include <srs_core_autofree.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_conn.hpp>
#include <srs_protocol_http_stack.hpp>
#include <srs_protocol_rtmp_stack.hpp>
#include <srs_protocol_stream.hpp>
#include <srs_protocol_utility.hpp>

ISrsHttpParser::ISrsHttpParser()
{
}

ISrsHttpParser::~ISrsHttpParser()
{
}

SrsHttpParser::SrsHttpParser()
{
    buffer_ = new SrsFastStream();
    header_ = NULL;

    type_ = HTTP_REQUEST;
    parsed_type_ = HTTP_BOTH;
}

SrsHttpParser::~SrsHttpParser()
{
    srs_freep(buffer_);
    srs_freep(header_);
}

srs_error_t SrsHttpParser::initialize(enum llhttp_type type)
{
    srs_error_t err = srs_success;

    jsonp_ = false;
    type_ = type;

    // Initialize the settings first
    llhttp_settings_init(&settings_);
    settings_.on_message_begin = on_message_begin;
    settings_.on_url = on_url;
    settings_.on_header_field = on_header_field;
    settings_.on_header_value = on_header_value;
    settings_.on_headers_complete = on_headers_complete;
    settings_.on_body = on_body;
    settings_.on_message_complete = on_message_complete;

    // Initialize the parser with settings
    llhttp_init(&parser_, type_, &settings_);
    parser_.data = (void *)this;

    return err;
}

void SrsHttpParser::set_jsonp(bool allow_jsonp)
{
    jsonp_ = allow_jsonp;
}

srs_error_t SrsHttpParser::parse_message(ISrsReader *reader, ISrsHttpMessage **ppmsg)
{
    srs_error_t err = srs_success;

    *ppmsg = NULL;

    // Reset parser data and state.
    state_ = SrsHttpParseStateInit;
    memset(&hp_header_, 0, sizeof(llhttp_t));
    // We must reset the field name and value, because we may get a partial value in on_header_value.
    field_name_ = field_value_ = "";
    // Reset the url.
    url_ = "";
    // The header of the request.
    srs_freep(header_);
    header_ = new SrsHttpHeader();

    // Reset parser for each message.
    // If the request is large, such as the fifth message at @utest ProtocolHTTPTest.ParsingLargeMessages,
    // we got header and part of body, so the parser will stay at SrsHttpParseStateBody:
    //      ***MESSAGE BEGIN***
    //      ***HEADERS COMPLETE***
    //      Body: xxx
    // when got next message, the whole next message is parsed as the body of previous one,
    // and the message fail.
    // @note You can comment the bellow line, the utest will fail.
    llhttp_reset(&parser_);

    // Reset the parsed type.
    parsed_type_ = HTTP_BOTH;
    // callback object ptr.
    parser_.data = (void *)this;
    // Always skip body, because we only want to parse the header.
    parser_.flags |= F_SKIPBODY;

    // do parse
    if ((err = parse_message_imp(reader)) != srs_success) {
        return srs_error_wrap(err, "parse message");
    }

    // create msg
    SrsHttpMessage *msg = new SrsHttpMessage(reader, buffer_);

    // Initialize the basic information.
    msg->set_basic(hp_header_.type, (llhttp_method_t)hp_header_.method, (llhttp_status_t)hp_header_.status_code, hp_header_.content_length);
    msg->set_header(header_, llhttp_should_keep_alive(&hp_header_));
    // For HTTP response, no url.
    if (parsed_type_ != HTTP_RESPONSE && (err = msg->set_url(url_, jsonp_)) != srs_success) {
        srs_freep(msg);
        return srs_error_wrap(err, "set url=%s, jsonp=%d", url_.c_str(), jsonp_);
    }

    // parse ok, return the msg.
    *ppmsg = msg;

    return err;
}

srs_error_t SrsHttpParser::parse_message_imp(ISrsReader *reader)
{
    srs_error_t err = srs_success;

    while (true) {
        if (buffer_->size() > 0) {
            const char *data_start = buffer_->bytes();
            llhttp_errno_t code = llhttp_execute(&parser_, data_start, buffer_->size());

            ssize_t consumed = 0;
            if (code == HPE_OK) {
                // No problem, all buffer should be consumed.
                consumed = buffer_->size();
            } else if (code == HPE_PAUSED) {
                // We only consume the header, not message or body.
                const char *error_pos = llhttp_get_error_pos(&parser_);
                if (error_pos && error_pos < data_start) {
                    return srs_error_new(ERROR_HTTP_PARSE_HEADER, "llhttp error_pos=%p < data_start=%p", error_pos, data_start);
                }

                if (error_pos && error_pos >= data_start) {
                    consumed = error_pos - data_start;
                }
            }

            // Check for errors (but allow certain conditions that are normal)
            // HPE_OK: success
            // HPE_PAUSED: we use to skip body
            if (code != HPE_OK && code != HPE_PAUSED) {
                return srs_error_new(ERROR_HTTP_PARSE_HEADER, "parse %dB, nparsed=%d, err=%d/%s %s",
                                     buffer_->size(), (int)consumed, code, llhttp_errno_name(code),
                                     llhttp_get_error_reason(&parser_) ? llhttp_get_error_reason(&parser_) : "");
            }

            srs_info("size=%d, nparsed=%d", buffer_->size(), (int)consumed);

            // Only consume the header bytes.
            if (consumed > 0) {
                buffer_->read_slice(consumed);
            }

            // Done when header completed, never wait for body completed, because it maybe chunked.
            if (state_ >= SrsHttpParseStateHeaderComplete) {
                break;
            }
        }

        // when nothing parsed, read more to parse.
        // when requires more, only grow 1bytes, but the buffer will cache more.
        if ((err = buffer_->grow(reader, buffer_->size() + 1)) != srs_success) {
            return srs_error_wrap(err, "grow buffer");
        }
    }

    SrsHttpParser *obj = this;
    if (!obj->field_value_.empty()) {
        obj->header_->set(obj->field_name_, obj->field_value_);
    }

    return err;
}

int SrsHttpParser::on_message_begin(llhttp_t *parser)
{
    SrsHttpParser *obj = (SrsHttpParser *)parser->data;
    srs_assert(obj);

    // Now, we start to parse HTTP message.
    obj->state_ = SrsHttpParseStateStart;

    // If we set to HTTP_BOTH, the type is detected and speicifed by parser.
    obj->parsed_type_ = (llhttp_type)parser->type;

    srs_info("***MESSAGE BEGIN***");

    return 0;
}

int SrsHttpParser::on_headers_complete(llhttp_t *parser)
{
    SrsHttpParser *obj = (SrsHttpParser *)parser->data;
    srs_assert(obj);

    obj->hp_header_ = *parser;
    // save the parser when header parse completed.
    obj->state_ = SrsHttpParseStateHeaderComplete;

    srs_info("***HEADERS COMPLETE***");

    // The return code of this callback:
    //      `0`: Proceed normally.
    //      `1`: Assume that request/response has no body, and proceed to parsing the next message.
    //      `2`: Assume absence of body (as above) and make `llhttp_execute()` return `HPE_PAUSED_UPGRADE`.
    //      `-1`: Error
    //      `HPE_PAUSED`: Pause parsing and wait for user to call `llhttp_resume()`.
    // We use HPE_PAUSED to skip body.
    return HPE_PAUSED;
}

int SrsHttpParser::on_message_complete(llhttp_t *parser)
{
    SrsHttpParser *obj = (SrsHttpParser *)parser->data;
    srs_assert(obj);

    // Note that we should never get here, because we always return HPE_PAUSED in on_headers_complete.
    obj->state_ = SrsHttpParseStateMessageComplete;

    srs_info("***MESSAGE COMPLETE***\n");

    return 0;
}

int SrsHttpParser::on_url(llhttp_t *parser, const char *at, size_t length)
{
    SrsHttpParser *obj = (SrsHttpParser *)parser->data;
    srs_assert(obj);

    if (length > 0) {
        // Note that this function might be called for multiple times, and we got pieces of content.
        obj->url_.append(at, (int)length);
    }

    srs_info("Method: %d, Url: %.*s", parser->method, (int)length, at);

    return 0;
}

int SrsHttpParser::on_header_field(llhttp_t *parser, const char *at, size_t length)
{
    SrsHttpParser *obj = (SrsHttpParser *)parser->data;
    srs_assert(obj);

    if (!obj->field_value_.empty()) {
        obj->header_->set(obj->field_name_, obj->field_value_);
        obj->field_name_ = obj->field_value_ = "";
    }

    if (length > 0) {
        obj->field_name_.append(at, (int)length);
    }

    srs_info("Header field(%d bytes): %.*s", (int)length, (int)length, at);
    return 0;
}

int SrsHttpParser::on_header_value(llhttp_t *parser, const char *at, size_t length)
{
    SrsHttpParser *obj = (SrsHttpParser *)parser->data;
    srs_assert(obj);

    if (length > 0) {
        obj->field_value_.append(at, (int)length);
    }

    srs_info("Header value(%d bytes): %.*s", (int)length, (int)length, at);
    return 0;
}

int SrsHttpParser::on_body(llhttp_t *parser, const char *at, size_t length)
{
    SrsHttpParser *obj = (SrsHttpParser *)parser->data;
    srs_assert(obj);

    // save the parser when body parsed.
    obj->state_ = SrsHttpParseStateBody;

    srs_info("Body: %.*s", (int)length, at);

    return 0;
}

SrsHttpMessage::SrsHttpMessage(ISrsReader *reader, SrsFastStream *buffer)
{
    owner_conn_ = NULL;
    chunked_ = false;
    _uri = new SrsHttpUri();
    _body = new SrsHttpResponseReader(this, reader, buffer);

    jsonp_ = false;

    // As 0 is DELETE, so we use GET as default.
    _method = (llhttp_method_t)SRS_CONSTS_HTTP_GET;
    // 200 is ok.
    _status = (llhttp_status_t)SRS_CONSTS_HTTP_OK;
    // -1 means infinity chunked mode.
    _content_length = -1;
    // From HTTP/1.1, default to keep alive.
    _keep_alive = true;
    type_ = 0;

    schema_ = "http";
}

SrsHttpMessage::~SrsHttpMessage()
{
    srs_freep(_body);
    srs_freep(_uri);
}

void SrsHttpMessage::set_basic(uint8_t type, llhttp_method_t method, llhttp_status_t status, int64_t content_length)
{
    type_ = type;
    _method = method;
    _status = status;

    // We use -1 as uninitialized mode, while llhttp use 0, so we should only
    // update it when it's not 0 and the message is not initialized.
    if (_content_length == -1 && content_length) {
        _content_length = content_length;
    }
}

void SrsHttpMessage::set_header(SrsHttpHeader *header, bool keep_alive)
{
    _header = *header;
    _keep_alive = keep_alive;

    // whether chunked.
    chunked_ = (header->get("Transfer-Encoding") == "chunked");

    // Update the content-length in header.
    string clv = header->get("Content-Length");
    if (!clv.empty()) {
        _content_length = ::atoll(clv.c_str());
    }

    // If no size(content-length or chunked), it's infinite chunked,
    // it means there is no body, so we must close the body reader.
    if (!chunked_ && _content_length == -1) {
        // The infinite chunked is only enabled for HTTP_RESPONSE, so we close the body for request.
        if (type_ == HTTP_REQUEST) {
            _body->close();
        }
    }
}

// For callback function name, only allow [a-zA-Z0-9_-.] characters.
bool srs_is_valid_jsonp_callback(std::string callback)
{
    for (int i = 0; i < (int)callback.length(); i++) {
        char ch = callback.at(i);
        bool is_alpha_beta = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');
        bool is_number = (ch >= '0' && ch <= '9');
        if (!is_alpha_beta && !is_number && ch != '.' && ch != '_' && ch != '-') {
            return false;
        }
    }
    return true;
}

srs_error_t SrsHttpMessage::set_url(string url, bool allow_jsonp)
{
    srs_error_t err = srs_success;

    _url = url;

    // parse uri from schema/server:port/path?query
    std::string uri = _url;

    if (!srs_strings_contains(uri, "://")) {
        // use server public ip when host not specified.
        // to make telnet happy.
        std::string host = _header.get("Host");

        // If no host in header, we use local discovered IP, IPv4 first.
        if (host.empty()) {
            SrsProtocolUtility utility;
            host = utility.public_internet_address(true);
        }

        // The url must starts with slash if no schema. For example, SIP request line starts with "sip".
        if (!host.empty() && !srs_strings_starts_with(_url, "/")) {
            host += "/";
        }

        if (!host.empty()) {
            uri = "http://" + host + _url;
        }
    }

    if ((err = _uri->initialize(uri)) != srs_success) {
        return srs_error_wrap(err, "init uri %s", uri.c_str());
    }

    // parse ext.
    SrsPath path;
    _ext = path.filepath_ext(_uri->get_path());

    // parse query string.
    srs_net_url_parse_query(_uri->get_query(), _query);

    // parse jsonp request message.
    if (allow_jsonp) {
        string callback = query_get("callback");
        jsonp_ = !callback.empty();

        if (jsonp_) {
            jsonp_method_ = query_get("method");
        }

        if (!srs_is_valid_jsonp_callback(callback)) {
            return srs_error_new(ERROR_HTTP_JSONP, "invalid callback=%s", callback.c_str());
        }
    }

    return err;
}

void SrsHttpMessage::set_https(bool v)
{
    schema_ = v ? "https" : "http";
    _uri->set_schema(schema_);
}

ISrsConnection *SrsHttpMessage::connection()
{
    return owner_conn_;
}

void SrsHttpMessage::set_connection(ISrsConnection *conn)
{
    owner_conn_ = conn;
}

string SrsHttpMessage::schema()
{
    return schema_;
}

uint8_t SrsHttpMessage::message_type()
{
    return type_;
}

uint8_t SrsHttpMessage::method()
{
    if (jsonp_ && !jsonp_method_.empty()) {
        if (jsonp_method_ == "GET") {
            return SRS_CONSTS_HTTP_GET;
        } else if (jsonp_method_ == "PUT") {
            return SRS_CONSTS_HTTP_PUT;
        } else if (jsonp_method_ == "POST") {
            return SRS_CONSTS_HTTP_POST;
        } else if (jsonp_method_ == "DELETE") {
            return SRS_CONSTS_HTTP_DELETE;
        }
    }

    return _method;
}

uint16_t SrsHttpMessage::status_code()
{
    return _status;
}

string SrsHttpMessage::method_str()
{
    if (jsonp_ && !jsonp_method_.empty()) {
        return jsonp_method_;
    }

    return llhttp_method_name((llhttp_method_t)_method);
}

bool SrsHttpMessage::is_http_get()
{
    return method() == SRS_CONSTS_HTTP_GET;
}

bool SrsHttpMessage::is_http_put()
{
    return method() == SRS_CONSTS_HTTP_PUT;
}

bool SrsHttpMessage::is_http_post()
{
    return method() == SRS_CONSTS_HTTP_POST;
}

bool SrsHttpMessage::is_http_delete()
{
    return method() == SRS_CONSTS_HTTP_DELETE;
}

bool SrsHttpMessage::is_http_options()
{
    return _method == SRS_CONSTS_HTTP_OPTIONS;
}

bool SrsHttpMessage::is_chunked()
{
    return chunked_;
}

bool SrsHttpMessage::is_keep_alive()
{
    return _keep_alive;
}

string SrsHttpMessage::uri()
{
    std::string uri = _uri->get_schema();
    if (uri.empty()) {
        uri += "http";
    }
    uri += "://";

    uri += host();
    uri += path();

    return uri;
}

string SrsHttpMessage::url()
{
    return _uri->get_url();
}

string SrsHttpMessage::host()
{
    std::map<string, string>::iterator it = _query.find("vhost");
    if (it != _query.end() && !it->second.empty()) {
        return it->second;
    }

    it = _query.find("domain");
    if (it != _query.end() && !it->second.empty()) {
        return it->second;
    }

    return _uri->get_host();
}

int SrsHttpMessage::port()
{
    return _uri->get_port();
}

string SrsHttpMessage::path()
{
    return _uri->get_path();
}

string SrsHttpMessage::query()
{
    return _uri->get_query();
}

string SrsHttpMessage::ext()
{
    return _ext;
}

std::string SrsHttpMessage::parse_rest_id(string pattern)
{
    string p = _uri->get_path();
    if (p.length() <= pattern.length()) {
        return "";
    }

    string id = p.substr((int)pattern.length());
    if (!id.empty()) {
        return id;
    }

    return "";
}

srs_error_t SrsHttpMessage::body_read_all(string &body)
{
    return srs_io_readall(_body, body);
}

ISrsHttpResponseReader *SrsHttpMessage::body_reader()
{
    return _body;
}

int64_t SrsHttpMessage::content_length()
{
    return _content_length;
}

string SrsHttpMessage::query_get(string key)
{
    std::string v;

    if (_query.find(key) != _query.end()) {
        v = _query[key];
    }

    return v;
}

SrsHttpHeader *SrsHttpMessage::header()
{
    return &_header;
}

ISrsRequest *SrsHttpMessage::to_request(string vhost)
{
    ISrsRequest *req = new SrsRequest();

    // http path, for instance, /live/livestream.flv, parse to
    //      app: /live
    //      stream: livestream.flv
    srs_net_url_parse_rtmp_url(_uri->get_path(), req->app_, req->stream_);

    // trim the start slash, for instance, /live to live
    req->app_ = srs_strings_trim_start(req->app_, "/");

    // remove the extension, for instance, livestream.flv to livestream
    SrsPath path;
    req->stream_ = path.filepath_filename(req->stream_);

    // generate others.
    req->tcUrl_ = "rtmp://" + vhost + "/" + req->app_;
    req->pageUrl_ = _header.get("Referer");
    req->objectEncoding_ = 0;

    std::string query = _uri->get_query();
    if (!query.empty()) {
        req->param_ = "?" + query;
    }

    srs_net_url_parse_tcurl(req->tcUrl_, req->schema_, req->host_, req->vhost_, req->app_, req->stream_, req->port_, req->param_);
    req->strip();

    // reset the host to http request host.
    if (req->host_ == SRS_CONSTS_RTMP_DEFAULT_VHOST) {
        req->host_ = _uri->get_host();
    }

    // Set ip by remote ip of connection.
    if (owner_conn_) {
        req->ip_ = owner_conn_->remote_ip();
    }

    // Overwrite by ip from proxy.
    string oip = srs_get_original_ip(this);
    if (!oip.empty()) {
        req->ip_ = oip;
    }

    // The request streaming protocol.
    req->protocol_ = (schema_ == "http") ? "flv" : "flvs";

    return req;
}

bool SrsHttpMessage::is_jsonp()
{
    return jsonp_;
}

ISrsHttpHeaderFilter::ISrsHttpHeaderFilter()
{
}

ISrsHttpHeaderFilter::~ISrsHttpHeaderFilter()
{
}

ISrsHttpFirstLineWriter::ISrsHttpFirstLineWriter()
{
}

ISrsHttpFirstLineWriter::~ISrsHttpFirstLineWriter()
{
}

SrsHttpMessageWriter::SrsHttpMessageWriter(ISrsProtocolReadWriter *io, ISrsHttpFirstLineWriter *flw)
{
    skt_ = io;
    hdr_ = new SrsHttpHeader();
    header_wrote_ = false;
    content_length_ = -1;
    written_ = 0;
    header_sent_ = false;
    nb_iovss_cache_ = 0;
    iovss_cache_ = NULL;
    hf_ = NULL;
    flw_ = flw;
    protocol_utility_ = new SrsProtocolUtility();
}

SrsHttpMessageWriter::~SrsHttpMessageWriter()
{
    srs_freep(hdr_);
    srs_freepa(iovss_cache_);
    srs_freep(protocol_utility_);
}

srs_error_t SrsHttpMessageWriter::final_request()
{
    srs_error_t err = srs_success;

    // write the header data in memory.
    if (!header_wrote_) {
        flw_->write_default_header();
    }

    // whatever header is wrote, we should try to send header.
    if ((err = send_header(NULL, 0)) != srs_success) {
        return srs_error_wrap(err, "send header");
    }

    // complete the chunked encoding.
    if (content_length_ == -1) {
        std::stringstream ss;
        ss << 0 << SRS_HTTP_CRLF << SRS_HTTP_CRLF;
        std::string ch = ss.str();
        return skt_->write((void *)ch.data(), (int)ch.length(), NULL);
    }

    // flush when send with content length
    return write(NULL, 0);
}

SrsHttpHeader *SrsHttpMessageWriter::header()
{
    return hdr_;
}

srs_error_t SrsHttpMessageWriter::write(char *data, int size)
{
    srs_error_t err = srs_success;

    // write the header data in memory.
    if (!header_wrote_) {
        if (hdr_->content_type().empty()) {
            hdr_->set_content_type("text/plain; charset=utf-8");
        }
        if (hdr_->content_length() == -1) {
            hdr_->set_content_length(size);
        }
        flw_->write_default_header();
    }

    // whatever header is wrote, we should try to send header.
    if ((err = send_header(data, size)) != srs_success) {
        return srs_error_wrap(err, "send header");
    }

    // check the bytes send and content length.
    written_ += size;
    if (content_length_ != -1 && written_ > content_length_) {
        return srs_error_new(ERROR_HTTP_CONTENT_LENGTH, "overflow writen=%" PRId64 ", max=%" PRId64, written_, content_length_);
    }

    // ignore NULL content.
    if (!data || size <= 0) {
        return err;
    }

    // directly send with content length
    if (content_length_ != -1) {
        return skt_->write((void *)data, size, NULL);
    }

    // send in chunked encoding.
    int nb_size = snprintf(header_cache_, SRS_HTTP_HEADER_CACHE_SIZE, "%x", size);
    if (nb_size <= 0 || nb_size >= SRS_HTTP_HEADER_CACHE_SIZE) {
        return srs_error_new(ERROR_HTTP_CONTENT_LENGTH, "overflow size=%d, expect=%d", size, nb_size);
    }

    iovec iovs[4];
    iovs[0].iov_base = (char *)header_cache_;
    iovs[0].iov_len = (int)nb_size;
    iovs[1].iov_base = (char *)SRS_HTTP_CRLF;
    iovs[1].iov_len = 2;
    iovs[2].iov_base = (char *)data;
    iovs[2].iov_len = size;
    iovs[3].iov_base = (char *)SRS_HTTP_CRLF;
    iovs[3].iov_len = 2;

    ssize_t nwrite = 0;
    if ((err = skt_->writev(iovs, 4, &nwrite)) != srs_success) {
        return srs_error_wrap(err, "write chunk");
    }

    return err;
}

srs_error_t SrsHttpMessageWriter::writev(const iovec *iov, int iovcnt, ssize_t *pnwrite)
{
    srs_error_t err = srs_success;

    // when header not ready, or not chunked, send one by one.
    if (!header_wrote_ || content_length_ != -1) {
        ssize_t nwrite = 0;
        for (int i = 0; i < iovcnt; i++) {
            nwrite += iov[i].iov_len;
            if ((err = write((char *)iov[i].iov_base, (int)iov[i].iov_len)) != srs_success) {
                return srs_error_wrap(err, "writev");
            }
        }

        if (pnwrite) {
            *pnwrite = nwrite;
        }

        return err;
    }

    // ignore NULL content.
    if (iovcnt <= 0) {
        return err;
    }

    // whatever header is wrote, we should try to send header.
    if ((err = send_header(NULL, 0)) != srs_success) {
        return srs_error_wrap(err, "send header");
    }

    // send in chunked encoding.
    int nb_iovss = 3 + iovcnt;
    iovec *iovss = iovss_cache_;
    if (nb_iovss_cache_ < nb_iovss) {
        srs_freepa(iovss_cache_);
        nb_iovss_cache_ = nb_iovss;
        iovss = iovss_cache_ = new iovec[nb_iovss];
    }

    // Send all iovs in one chunk, the size is the total size of iovs.
    int size = 0;
    for (int i = 0; i < iovcnt; i++) {
        const iovec *data_iov = iov + i;
        size += data_iov->iov_len;
    }
    written_ += size;

    // chunk header
    int nb_size = snprintf(header_cache_, SRS_HTTP_HEADER_CACHE_SIZE, "%x", size);
    if (nb_size <= 0 || nb_size >= SRS_HTTP_HEADER_CACHE_SIZE) {
        return srs_error_new(ERROR_HTTP_CONTENT_LENGTH, "overflow size=%d, expect=%d", size, nb_size);
    }
    iovss[0].iov_base = (char *)header_cache_;
    iovss[0].iov_len = (int)nb_size;

    // chunk header eof.
    iovss[1].iov_base = (char *)SRS_HTTP_CRLF;
    iovss[1].iov_len = 2;

    // chunk body.
    for (int i = 0; i < iovcnt; i++) {
        iovss[2 + i].iov_base = (char *)iov[i].iov_base;
        iovss[2 + i].iov_len = (int)iov[i].iov_len;
    }

    // chunk body eof.
    iovss[2 + iovcnt].iov_base = (char *)SRS_HTTP_CRLF;
    iovss[2 + iovcnt].iov_len = 2;

    // sendout all ioves.
    ssize_t nwrite = 0;
    if ((err = protocol_utility_->write_iovs(skt_, iovss, nb_iovss, &nwrite)) != srs_success) {
        return srs_error_wrap(err, "writev large iovs");
    }

    if (pnwrite) {
        *pnwrite = nwrite;
    }

    return err;
}

void SrsHttpMessageWriter::write_header()
{
    if (header_wrote_)
        return;
    header_wrote_ = true;

    // parse the content length from header.
    content_length_ = hdr_->content_length();
}

srs_error_t SrsHttpMessageWriter::send_header(char *data, int size)
{
    srs_error_t err = srs_success;

    if (header_sent_) {
        return err;
    }
    header_sent_ = true;

    std::stringstream ss;

    // First line, the request line or status line.
    if ((err = flw_->build_first_line(ss, data, size)) != srs_success) {
        return srs_error_wrap(err, "first line");
    }

    // set server if not set.
    if (hdr_->get("Server").empty()) {
        hdr_->set("Server", RTMP_SIG_SRS_SERVER);
    }

    // chunked encoding
    if (content_length_ == -1) {
        hdr_->set("Transfer-Encoding", "chunked");
    }

    // keep alive to make vlc happy.
    if (hdr_->get("Connection").empty()) {
        hdr_->set("Connection", "Keep-Alive");
    }

    // Filter the header before writing it.
    if (hf_ && ((err = hf_->filter(hdr_)) != srs_success)) {
        return srs_error_wrap(err, "filter header");
    }

    // write header
    hdr_->write(ss);

    // header_eof
    ss << SRS_HTTP_CRLF;

    std::string buf = ss.str();
    return skt_->write((void *)buf.c_str(), buf.length(), NULL);
}

bool SrsHttpMessageWriter::header_wrote()
{
    return header_wrote_;
}

void SrsHttpMessageWriter::set_header_filter(ISrsHttpHeaderFilter *hf)
{
    hf_ = hf;
}

SrsHttpResponseWriter::SrsHttpResponseWriter(ISrsProtocolReadWriter *io)
{
    writer_ = new SrsHttpMessageWriter(io, this);
    status_ = SRS_CONSTS_HTTP_OK;
}

SrsHttpResponseWriter::~SrsHttpResponseWriter()
{
    srs_freep(writer_);
}

void SrsHttpResponseWriter::set_header_filter(ISrsHttpHeaderFilter *hf)
{
    writer_->set_header_filter(hf);
}

srs_error_t SrsHttpResponseWriter::final_request()
{
    return writer_->final_request();
}

SrsHttpHeader *SrsHttpResponseWriter::header()
{
    return writer_->header();
}

srs_error_t SrsHttpResponseWriter::write(char *data, int size)
{
    return writer_->write(data, size);
}

srs_error_t SrsHttpResponseWriter::writev(const iovec *iov, int iovcnt, ssize_t *pnwrite)
{
    return writer_->writev(iov, iovcnt, pnwrite);
}

void SrsHttpResponseWriter::write_header(int code)
{
    if (writer_->header_wrote()) {
        srs_warn("http: multiple write_header calls, status=%d, code=%d", status_, code);
        return;
    }

    status_ = code;
    return writer_->write_header();
}

srs_error_t SrsHttpResponseWriter::build_first_line(std::stringstream &ss, char *data, int size)
{
    srs_error_t err = srs_success;

    // Write status line for response.
    ss << "HTTP/1.1 " << status_ << " " << srs_generate_http_status_text(status_) << SRS_HTTP_CRLF;

    // Try to detect content type from response body data.
    SrsHttpHeader *hdr = writer_->header();
    if (srs_go_http_body_allowd(status_)) {
        if (data && hdr->content_type().empty()) {
            hdr->set_content_type(srs_go_http_detect(data, size));
        }
    }

    return err;
}

void SrsHttpResponseWriter::write_default_header()
{
    write_header(SRS_CONSTS_HTTP_OK);
}

SrsHttpRequestWriter::SrsHttpRequestWriter(ISrsProtocolReadWriter *io)
{
    writer_ = new SrsHttpMessageWriter(io, this);
}

SrsHttpRequestWriter::~SrsHttpRequestWriter()
{
    srs_freep(writer_);
}

srs_error_t SrsHttpRequestWriter::final_request()
{
    return writer_->final_request();
}

SrsHttpHeader *SrsHttpRequestWriter::header()
{
    return writer_->header();
}

srs_error_t SrsHttpRequestWriter::write(char *data, int size)
{
    return writer_->write(data, size);
}

srs_error_t SrsHttpRequestWriter::writev(const iovec *iov, int iovcnt, ssize_t *pnwrite)
{
    return writer_->writev(iov, iovcnt, pnwrite);
}

void SrsHttpRequestWriter::write_header(const std::string &method, const std::string &path)
{
    if (writer_->header_wrote()) {
        srs_warn("http: multiple write_header calls, current=%s(%s), now=%s(%s)", method_.c_str(), path_.c_str(), method.c_str(), path.c_str());
        return;
    }

    method_ = method;
    path_ = path;
    return writer_->write_header();
}

srs_error_t SrsHttpRequestWriter::build_first_line(std::stringstream &ss, char *data, int size)
{
    srs_error_t err = srs_success;

    // Write status line for response.
    ss << method_ << " " << path_ << " HTTP/1.1" << SRS_HTTP_CRLF;

    // Try to detect content type from request body data.
    SrsHttpHeader *hdr = writer_->header();
    if (data && hdr->content_type().empty()) {
        hdr->set_content_type(srs_go_http_detect(data, size));
    }

    return err;
}

void SrsHttpRequestWriter::write_default_header()
{
    write_header("GET", "/");
}

SrsHttpResponseReader::SrsHttpResponseReader(SrsHttpMessage *msg, ISrsReader *reader, SrsFastStream *body)
{
    skt_ = reader;
    owner_ = msg;
    is_eof_ = false;
    nb_total_read_ = 0;
    nb_left_chunk_ = 0;
    buffer_ = body;
    nb_chunk_ = 0;
}

SrsHttpResponseReader::~SrsHttpResponseReader()
{
}

void SrsHttpResponseReader::close()
{
    is_eof_ = true;
}

bool SrsHttpResponseReader::eof()
{
    return is_eof_;
}

srs_error_t SrsHttpResponseReader::read(void *data, size_t nb_data, ssize_t *nb_read)
{
    srs_error_t err = srs_success;

    if (is_eof_) {
        return srs_error_new(ERROR_HTTP_RESPONSE_EOF, "EOF");
    }

    // chunked encoding.
    if (owner_->is_chunked()) {
        return read_chunked(data, nb_data, nb_read);
    }

    // read by specified content-length
    if (owner_->content_length() != -1) {
        size_t max = (size_t)owner_->content_length() - (size_t)nb_total_read_;
        if (max <= 0) {
            is_eof_ = true;
            return err;
        }

        // change the max to read.
        nb_data = srs_min(nb_data, max);
        return read_specified(data, nb_data, nb_read);
    }

    // Infinite chunked mode.
    // If not chunked encoding, and no content-length, it's infinite chunked.
    // In this mode, all body is data and never EOF util socket closed.
    if ((err = read_specified(data, nb_data, nb_read)) != srs_success) {
        // For infinite chunked, the socket close event is EOF.
        if (srs_error_code(err) == ERROR_SOCKET_READ) {
            srs_freep(err);
            is_eof_ = true;
            return err;
        }
    }

    return err;
}

srs_error_t SrsHttpResponseReader::read_chunked(void *data, size_t nb_data, ssize_t *nb_read)
{
    srs_error_t err = srs_success;

    // when no bytes left in chunk,
    // parse the chunk length first.
    if (nb_left_chunk_ <= 0) {
        char *at = NULL;
        int length = 0;
        while (!at) {
            // find the CRLF of chunk header end.
            char *start = buffer_->bytes();
            char *end = start + buffer_->size();

            for (char *p = start; p < end - 1; p++) {
                if (p[0] == SRS_HTTP_CR && p[1] == SRS_HTTP_LF) {
                    // invalid chunk, ignore.
                    if (p == start) {
                        return srs_error_new(ERROR_HTTP_INVALID_CHUNK_HEADER, "chunk header");
                    }
                    length = (int)(p - start + 2);
                    at = buffer_->read_slice(length);
                    break;
                }
            }

            // got at, ok.
            if (at) {
                break;
            }

            // when empty, only grow 1bytes, but the buffer will cache more.
            if ((err = buffer_->grow(skt_, buffer_->size() + 1)) != srs_success) {
                return srs_error_wrap(err, "grow buffer");
            }
        }
        srs_assert(length >= 3);

        // it's ok to set the pos and pos+1 to NULL.
        at[length - 1] = 0;
        at[length - 2] = 0;

        // size is the bytes size, excludes the chunk header and end CRLF.
        // @remark It must be hex format, please read https://developer.mozilla.org/en-US/docs/Web/HTTP/Headers/Transfer-Encoding#Directives
        // @remark For strtol, note that: If no conversion could be performed, 0 is returned and the global variable errno is set to EINVAL.
        char *at_parsed = at;
        errno = 0;
        int ilength = (int)::strtol(at, &at_parsed, 16);
        if (ilength < 0 || errno != 0 || at_parsed - at != length - 2) {
            return srs_error_new(ERROR_HTTP_INVALID_CHUNK_HEADER, "invalid length %s as %d, parsed=%.*s, errno=%d",
                                 at, ilength, (int)(at_parsed - at), at, errno);
        }

        // all bytes in chunk is left now.
        nb_chunk_ = nb_left_chunk_ = (size_t)ilength;
    }

    if (nb_chunk_ <= 0) {
        // for the last chunk, eof.
        is_eof_ = true;
        if (nb_read) {
            *nb_read = 0;
        }
    } else {
        // for not the last chunk, there must always exists bytes.
        // left bytes in chunk, read some.
        srs_assert(nb_left_chunk_);

        size_t nb_bytes = srs_min(nb_left_chunk_, nb_data);
        err = read_specified(data, nb_bytes, (ssize_t *)&nb_bytes);

        // the nb_bytes used for output already read size of bytes.
        if (nb_read) {
            *nb_read = nb_bytes;
        }
        nb_left_chunk_ -= nb_bytes;

        if (err != srs_success) {
            return srs_error_wrap(err, "read specified");
        }

        // If still left bytes in chunk, ignore and read in future.
        if (nb_left_chunk_ > 0) {
            return err;
        }
    }

    // for both the last or not, the CRLF of chunk payload end.
    if ((err = buffer_->grow(skt_, 2)) != srs_success) {
        return srs_error_wrap(err, "grow buffer");
    }
    buffer_->read_slice(2);

    return err;
}

srs_error_t SrsHttpResponseReader::read_specified(void *data, size_t nb_data, ssize_t *nb_read)
{
    srs_error_t err = srs_success;

    if (buffer_->size() <= 0) {
        // when empty, only grow 1bytes, but the buffer will cache more.
        if ((err = buffer_->grow(skt_, 1)) != srs_success) {
            return srs_error_wrap(err, "grow buffer");
        }
    }

    size_t nb_bytes = srs_min(nb_data, (size_t)buffer_->size());

    // read data to buffer.
    srs_assert(nb_bytes);
    char *p = buffer_->read_slice(nb_bytes);
    memcpy(data, p, nb_bytes);
    if (nb_read) {
        *nb_read = nb_bytes;
    }

    // increase the total read to determine whether EOF.
    nb_total_read_ += nb_bytes;

    // for not chunked and specified content length.
    if (!owner_->is_chunked() && owner_->content_length() != -1) {
        // when read completed, eof.
        if (nb_total_read_ >= (int)owner_->content_length()) {
            is_eof_ = true;
        }
    }

    return err;
}
