//
// Copyright (c) 2013-2026 The SRS Authors
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

    llhttp_settings_init(&settings_);
    settings_.on_message_begin = on_message_begin;
    settings_.on_url = on_url;
    settings_.on_header_field = on_header_field;
    settings_.on_header_value = on_header_value;
    settings_.on_headers_complete = on_headers_complete;
    settings_.on_body = on_body;
    settings_.on_message_complete = on_message_complete;

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

    state_ = SrsHttpParseStateInit;
    memset(&hp_header_, 0, sizeof(llhttp_t));
    // A partial value may be left from on_header_value.
    field_name_ = field_value_ = "";
    url_ = "";
    srs_freep(header_);
    header_ = new SrsHttpHeader();

    // A large message may leave llhttp in the body state, with the header and part of the body read,
    // so it would parse the whole next message as that body. See utest ProtocolHTTPTest.ParsingLargeMessages.
    llhttp_reset(&parser_);

    parsed_type_ = HTTP_BOTH;
    parser_.data = (void *)this;
    // Parse only the header; the body may be large or chunked and is read later.
    parser_.flags |= F_SKIPBODY;

    if ((err = parse_message_imp(reader)) != srs_success) {
        return srs_error_wrap(err, "parse message");
    }

    SrsHttpMessage *msg = new SrsHttpMessage(reader, buffer_);

    msg->set_basic(hp_header_.type, (llhttp_method_t)hp_header_.method, (llhttp_status_t)hp_header_.status_code, hp_header_.content_length);
    msg->set_header(header_, llhttp_should_keep_alive(&hp_header_));
    // An HTTP response has no URL.
    if (parsed_type_ != HTTP_RESPONSE && (err = msg->set_url(url_, jsonp_)) != srs_success) {
        srs_freep(msg);
        return srs_error_wrap(err, "set url=%s, jsonp=%d", url_.c_str(), jsonp_);
    }

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
                // llhttp consumed the whole buffer.
                consumed = buffer_->size();
            } else if (code == HPE_PAUSED) {
                // Paused at the end of the header, so consume only the header.
                const char *error_pos = llhttp_get_error_pos(&parser_);
                if (error_pos && error_pos < data_start) {
                    return srs_error_new(ERROR_HTTP_PARSE_HEADER, "llhttp error_pos=%p < data_start=%p", error_pos, data_start);
                }

                if (error_pos && error_pos >= data_start) {
                    consumed = error_pos - data_start;
                }
            }

            if (code != HPE_OK && code != HPE_PAUSED) {
                return srs_error_new(ERROR_HTTP_PARSE_HEADER, "parse %dB, nparsed=%d, err=%d/%s %s",
                                     buffer_->size(), (int)consumed, code, llhttp_errno_name(code),
                                     llhttp_get_error_reason(&parser_) ? llhttp_get_error_reason(&parser_) : "");
            }

            srs_info("size=%d, nparsed=%d", buffer_->size(), (int)consumed);

            if (consumed > 0) {
                buffer_->read_slice(consumed);
            }

            // Never wait for the body, since it may be chunked.
            if (state_ >= SrsHttpParseStateHeaderComplete) {
                break;
            }
        }

        // Ask for 1 more byte; the buffer reads and caches more.
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

    obj->state_ = SrsHttpParseStateStart;

    // With HTTP_BOTH, llhttp detects the type.
    obj->parsed_type_ = (llhttp_type)parser->type;

    srs_info("***MESSAGE BEGIN***");

    return 0;
}

int SrsHttpParser::on_headers_complete(llhttp_t *parser)
{
    SrsHttpParser *obj = (SrsHttpParser *)parser->data;
    srs_assert(obj);

    obj->hp_header_ = *parser;
    obj->state_ = SrsHttpParseStateHeaderComplete;

    srs_info("***HEADERS COMPLETE***");

    // HPE_PAUSED makes llhttp pause until llhttp_resume(), which skips the body.
    return HPE_PAUSED;
}

int SrsHttpParser::on_message_complete(llhttp_t *parser)
{
    SrsHttpParser *obj = (SrsHttpParser *)parser->data;
    srs_assert(obj);

    // Never called, since on_headers_complete always returns HPE_PAUSED.
    obj->state_ = SrsHttpParseStateMessageComplete;

    srs_info("***MESSAGE COMPLETE***\n");

    return 0;
}

int SrsHttpParser::on_url(llhttp_t *parser, const char *at, size_t length)
{
    SrsHttpParser *obj = (SrsHttpParser *)parser->data;
    srs_assert(obj);

    if (length > 0) {
        // llhttp may call this several times, each with a piece of the URL.
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

    obj->state_ = SrsHttpParseStateBody;

    srs_info("Body: %.*s", (int)length, at);

    return 0;
}

ISrsHttpMessageOwnerSetter::ISrsHttpMessageOwnerSetter()
{
}

ISrsHttpMessageOwnerSetter::~ISrsHttpMessageOwnerSetter()
{
}

SrsHttpMessage::SrsHttpMessage(ISrsReader *reader, SrsFastStream *buffer)
{
    owner_conn_ = NULL;
    chunked_ = false;
    uri_ = new SrsHttpUri();
    body_ = new SrsHttpResponseReader(this, reader, buffer);

    jsonp_ = false;

    // Method 0 is DELETE, so default to GET.
    method_ = (llhttp_method_t)SRS_CONSTS_HTTP_GET;
    status_ = (llhttp_status_t)SRS_CONSTS_HTTP_OK;
    content_length_ = -1;
    // HTTP/1.1 defaults to keep-alive.
    keep_alive_ = true;
    type_ = 0;

    schema_ = "http";
}

SrsHttpMessage::~SrsHttpMessage()
{
    srs_freep(body_);
    srs_freep(uri_);
}

void SrsHttpMessage::set_basic(uint8_t type, llhttp_method_t method, llhttp_status_t status, int64_t content_length)
{
    type_ = type;
    method_ = method;
    status_ = status;

    // llhttp uses 0 for no Content-Length, where this class uses -1.
    if (content_length_ == -1 && content_length) {
        content_length_ = content_length;
    }
}

void SrsHttpMessage::set_header(SrsHttpHeader *header, bool keep_alive)
{
    header_ = *header;
    keep_alive_ = keep_alive;

    chunked_ = (header->get("Transfer-Encoding") == "chunked");

    string clv = header->get("Content-Length");
    if (!clv.empty()) {
        content_length_ = ::atoll(clv.c_str());
    }

    // With neither Content-Length nor chunked encoding, a response body runs until the socket
    // closes, but a request has no body.
    if (!chunked_ && content_length_ == -1) {
        if (type_ == HTTP_REQUEST) {
            body_->close();
        }
    }
}

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

    url_ = url;

    std::string uri = url_;

    // Only a schema before any path or query makes an absolute URL. A query may hold a URL too,
    // such as the WHEP player page's ?url=http://host:1985/rtc/v1/whep/.
    size_t schema_end = uri.find("://");
    bool has_schema = schema_end != std::string::npos && schema_end < uri.find_first_of("/?");

    if (!has_schema) {
        std::string host = header_.get("Host");

        // Fall back to the server IP when there is no Host header, to make telnet happy.
        if (host.empty()) {
            SrsProtocolUtility utility;
            host = utility.public_internet_address(true);
        }

        // A URL with no schema may not start with a slash, such as a SIP request line.
        if (!host.empty() && !srs_strings_starts_with(url_, "/")) {
            host += "/";
        }

        if (!host.empty()) {
            uri = "http://" + host + url_;
        }
    }

    if ((err = uri_->initialize(uri)) != srs_success) {
        return srs_error_wrap(err, "init uri %s", uri.c_str());
    }

    SrsPath path;
    ext_ = path.filepath_ext(uri_->get_path());

    srs_net_url_parse_query(uri_->get_query(), query_);

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
    uri_->set_schema(schema_);
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

    return method_;
}

uint16_t SrsHttpMessage::status_code()
{
    return status_;
}

string SrsHttpMessage::method_str()
{
    if (jsonp_ && !jsonp_method_.empty()) {
        return jsonp_method_;
    }

    return llhttp_method_name((llhttp_method_t)method_);
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
    return method_ == SRS_CONSTS_HTTP_OPTIONS;
}

bool SrsHttpMessage::is_chunked()
{
    return chunked_;
}

bool SrsHttpMessage::is_keep_alive()
{
    return keep_alive_;
}

string SrsHttpMessage::uri()
{
    // The URI the client asked for: the schema, the Host with its port, the
    // path and the query, not the vhost of a vhost or domain query.
    return uri_->get_url();
}

string SrsHttpMessage::url()
{
    return uri_->get_url();
}

string SrsHttpMessage::host()
{
    std::map<string, string>::iterator it = query_.find("vhost");
    if (it != query_.end() && !it->second.empty()) {
        return it->second;
    }

    it = query_.find("domain");
    if (it != query_.end() && !it->second.empty()) {
        return it->second;
    }

    return uri_->get_host();
}

int SrsHttpMessage::port()
{
    return uri_->get_port();
}

string SrsHttpMessage::path()
{
    return uri_->get_path();
}

string SrsHttpMessage::query()
{
    return uri_->get_query();
}

string SrsHttpMessage::ext()
{
    return ext_;
}

std::string SrsHttpMessage::parse_rest_id(string pattern)
{
    string p = uri_->get_path();
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
    return srs_io_readall(body_, body);
}

ISrsHttpResponseReader *SrsHttpMessage::body_reader()
{
    return body_;
}

int64_t SrsHttpMessage::content_length()
{
    return content_length_;
}

string SrsHttpMessage::query_get(string key)
{
    std::string v;

    if (query_.find(key) != query_.end()) {
        v = query_[key];
    }

    return v;
}

SrsHttpHeader *SrsHttpMessage::header()
{
    return &header_;
}

ISrsRequest *SrsHttpMessage::to_request(string vhost)
{
    ISrsRequest *req = new SrsRequest();

    srs_net_url_parse_http_path(uri_->get_path(), req->app_, req->stream_);

    req->tcUrl_ = "rtmp://" + vhost + "/" + req->app_;
    req->pageUrl_ = header_.get("Referer");
    req->objectEncoding_ = 0;

    std::string query = uri_->get_query();
    if (!query.empty()) {
        req->param_ = "?" + query;
    }

    srs_net_url_parse_tcurl(req->tcUrl_, req->schema_, req->host_, req->vhost_, req->app_, req->stream_, req->port_, req->param_);
    req->strip();

    if (req->host_ == SRS_CONSTS_RTMP_DEFAULT_VHOST) {
        req->host_ = uri_->get_host();
    }

    if (owner_conn_) {
        req->ip_ = owner_conn_->remote_ip();
    }

    // Prefer the client IP a proxy forwards.
    string oip = srs_get_original_ip(this);
    if (!oip.empty()) {
        req->ip_ = oip;
    }

    req->protocol_ = (schema_ == "http") ? "flv" : "flvs";

    return req;
}

string SrsHttpMessage::app_url(string app)
{
    // The Host header keeps the port and IPv6 brackets as asked, which the URI may not: it defaults a
    // missing port to 80, even for HTTPS.
    string host = header_.get("Host");
    if (host.empty()) {
        host = this->host();
    }
    return schema() + "://" + host + "/" + app;
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

    if (!header_wrote_) {
        flw_->write_default_header();
    }

    if ((err = send_header(NULL, 0)) != srs_success) {
        return srs_error_wrap(err, "send header");
    }

    // A zero-size chunk ends a chunked body.
    if (content_length_ == -1) {
        std::stringstream ss;
        ss << 0 << SRS_HTTP_CRLF << SRS_HTTP_CRLF;
        std::string ch = ss.str();
        return skt_->write((void *)ch.data(), (int)ch.length(), NULL);
    }

    return write(NULL, 0);
}

SrsHttpHeader *SrsHttpMessageWriter::header()
{
    return hdr_;
}

srs_error_t SrsHttpMessageWriter::write(char *data, int size)
{
    srs_error_t err = srs_success;

    if (!header_wrote_) {
        if (hdr_->content_type().empty()) {
            hdr_->set_content_type("text/plain; charset=utf-8");
        }
        if (hdr_->content_length() == -1) {
            hdr_->set_content_length(size);
        }
        flw_->write_default_header();
    }

    if ((err = send_header(data, size)) != srs_success) {
        return srs_error_wrap(err, "send header");
    }

    written_ += size;
    if (content_length_ != -1 && written_ > content_length_) {
        return srs_error_new(ERROR_HTTP_CONTENT_LENGTH, "overflow writen=%" PRId64 ", max=%" PRId64, written_, content_length_);
    }

    if (!data || size <= 0) {
        return err;
    }

    if (content_length_ != -1) {
        return skt_->write((void *)data, size, NULL);
    }

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

    if (iovcnt <= 0) {
        return err;
    }

    if ((err = send_header(NULL, 0)) != srs_success) {
        return srs_error_wrap(err, "send header");
    }

    int nb_iovss = 3 + iovcnt;
    iovec *iovss = iovss_cache_;
    if (nb_iovss_cache_ < nb_iovss) {
        srs_freepa(iovss_cache_);
        nb_iovss_cache_ = nb_iovss;
        iovss = iovss_cache_ = new iovec[nb_iovss];
    }

    int size = 0;
    for (int i = 0; i < iovcnt; i++) {
        const iovec *data_iov = iov + i;
        size += data_iov->iov_len;
    }
    written_ += size;

    int nb_size = snprintf(header_cache_, SRS_HTTP_HEADER_CACHE_SIZE, "%x", size);
    if (nb_size <= 0 || nb_size >= SRS_HTTP_HEADER_CACHE_SIZE) {
        return srs_error_new(ERROR_HTTP_CONTENT_LENGTH, "overflow size=%d, expect=%d", size, nb_size);
    }
    iovss[0].iov_base = (char *)header_cache_;
    iovss[0].iov_len = (int)nb_size;

    iovss[1].iov_base = (char *)SRS_HTTP_CRLF;
    iovss[1].iov_len = 2;

    for (int i = 0; i < iovcnt; i++) {
        iovss[2 + i].iov_base = (char *)iov[i].iov_base;
        iovss[2 + i].iov_len = (int)iov[i].iov_len;
    }

    iovss[2 + iovcnt].iov_base = (char *)SRS_HTTP_CRLF;
    iovss[2 + iovcnt].iov_len = 2;

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

    if ((err = flw_->build_first_line(ss, data, size)) != srs_success) {
        return srs_error_wrap(err, "first line");
    }

    if (hdr_->get("Server").empty()) {
        hdr_->set("Server", RTMP_SIG_SRS_SERVER);
    }

    if (content_length_ == -1) {
        hdr_->set("Transfer-Encoding", "chunked");
    }

    // Keep-Alive to make VLC happy.
    if (hdr_->get("Connection").empty()) {
        hdr_->set("Connection", "Keep-Alive");
    }

    if (hf_ && ((err = hf_->filter(hdr_)) != srs_success)) {
        return srs_error_wrap(err, "filter header");
    }

    hdr_->write(ss);

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

bool SrsHttpResponseWriter::header_wrote()
{
    return writer_->header_wrote();
}

srs_error_t SrsHttpResponseWriter::build_first_line(std::stringstream &ss, char *data, int size)
{
    srs_error_t err = srs_success;

    ss << "HTTP/1.1 " << status_ << " " << srs_generate_http_status_text(status_) << SRS_HTTP_CRLF;

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

    ss << method_ << " " << path_ << " HTTP/1.1" << SRS_HTTP_CRLF;

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

    if (owner_->is_chunked()) {
        return read_chunked(data, nb_data, nb_read);
    }

    if (owner_->content_length() != -1) {
        size_t max = (size_t)owner_->content_length() - (size_t)nb_total_read_;
        if (max <= 0) {
            is_eof_ = true;
            return err;
        }

        nb_data = srs_min(nb_data, max);
        return read_specified(data, nb_data, nb_read);
    }

    // With neither chunked encoding nor Content-Length, the body runs until the socket closes.
    if ((err = read_specified(data, nb_data, nb_read)) != srs_success) {
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

    if (nb_left_chunk_ <= 0) {
        char *at = NULL;
        int length = 0;
        while (!at) {
            char *start = buffer_->bytes();
            char *end = start + buffer_->size();

            for (char *p = start; p < end - 1; p++) {
                if (p[0] == SRS_HTTP_CR && p[1] == SRS_HTTP_LF) {
                    if (p == start) {
                        return srs_error_new(ERROR_HTTP_INVALID_CHUNK_HEADER, "chunk header");
                    }
                    length = (int)(p - start + 2);
                    at = buffer_->read_slice(length);
                    break;
                }
            }

            if (at) {
                break;
            }

            // Ask for 1 more byte; the buffer reads and caches more.
            if ((err = buffer_->grow(skt_, buffer_->size() + 1)) != srs_success) {
                return srs_error_wrap(err, "grow buffer");
            }
        }
        srs_assert(length >= 3);

        // These are the CRLF, so zeroing them ends the size string.
        at[length - 1] = 0;
        at[length - 2] = 0;

        // The size is in hex, and does not count the size line or the CRLF after the data, see
        // https://developer.mozilla.org/en-US/docs/Web/HTTP/Headers/Transfer-Encoding#Directives
        // When nothing converts, strtol returns 0 and may set errno to EINVAL.
        // The size may be followed by chunk extensions, such as "5;name=value", which are ignored, see RFC 9112 7.1.1.
        char *at_parsed = at;
        errno = 0;
        int ilength = (int)::strtol(at, &at_parsed, 16);
        bool has_ext = at_parsed > at && *at_parsed == ';';
        if (ilength < 0 || errno != 0 || (!has_ext && at_parsed - at != length - 2)) {
            return srs_error_new(ERROR_HTTP_INVALID_CHUNK_HEADER, "invalid length %s as %d, parsed=%.*s, errno=%d",
                                 at, ilength, (int)(at_parsed - at), at, errno);
        }

        nb_chunk_ = nb_left_chunk_ = (size_t)ilength;
    }

    if (nb_chunk_ <= 0) {
        // A zero-size chunk is the last one.
        is_eof_ = true;
        if (nb_read) {
            *nb_read = 0;
        }
    } else {
        srs_assert(nb_left_chunk_);

        size_t nb_bytes = srs_min(nb_left_chunk_, nb_data);
        err = read_specified(data, nb_bytes, (ssize_t *)&nb_bytes);

        if (nb_read) {
            *nb_read = nb_bytes;
        }
        nb_left_chunk_ -= nb_bytes;

        if (err != srs_success) {
            return srs_error_wrap(err, "read specified");
        }

        if (nb_left_chunk_ > 0) {
            return err;
        }
    }

    // Every chunk, the last one too, ends with a CRLF.
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
        // Ask for 1 byte; the buffer reads and caches more.
        if ((err = buffer_->grow(skt_, 1)) != srs_success) {
            return srs_error_wrap(err, "grow buffer");
        }
    }

    size_t nb_bytes = srs_min(nb_data, (size_t)buffer_->size());

    srs_assert(nb_bytes);
    char *p = buffer_->read_slice(nb_bytes);
    memcpy(data, p, nb_bytes);
    if (nb_read) {
        *nb_read = nb_bytes;
    }

    nb_total_read_ += nb_bytes;

    if (!owner_->is_chunked() && owner_->content_length() != -1) {
        if (nb_total_read_ >= (int)owner_->content_length()) {
            is_eof_ = true;
        }
    }

    return err;
}
