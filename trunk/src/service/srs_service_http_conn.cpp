/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2020 Winlin
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

#include <srs_service_http_conn.hpp>

#include <stdlib.h>
#include <sstream>
using namespace std;

#include <srs_kernel_log.hpp>
#include <srs_kernel_error.hpp>
#include <srs_protocol_stream.hpp>
#include <srs_service_utility.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_utility.hpp>
#include <srs_core_autofree.hpp>
#include <srs_rtmp_stack.hpp>
#include <srs_service_conn.hpp>

SrsHttpParser::SrsHttpParser()
{
    buffer = new SrsFastStream();
    header = NULL;

    p_body_start = p_header_tail = NULL;
}

SrsHttpParser::~SrsHttpParser()
{
    srs_freep(buffer);
    srs_freep(header);
}

srs_error_t SrsHttpParser::initialize(enum http_parser_type type, bool allow_jsonp)
{
    srs_error_t err = srs_success;
    
    jsonp = allow_jsonp;
    
    memset(&settings, 0, sizeof(settings));
    settings.on_message_begin = on_message_begin;
    settings.on_url = on_url;
    settings.on_header_field = on_header_field;
    settings.on_header_value = on_header_value;
    settings.on_headers_complete = on_headers_complete;
    settings.on_body = on_body;
    settings.on_message_complete = on_message_complete;
    
    http_parser_init(&parser, type);
    // callback object ptr.
    parser.data = (void*)this;
    
    return err;
}

srs_error_t SrsHttpParser::parse_message(ISrsReader* reader, ISrsHttpMessage** ppmsg)
{
    srs_error_t err = srs_success;

    *ppmsg = NULL;
    
    // Reset request data.
    state = SrsHttpParseStateInit;
    hp_header = http_parser();
    // The body that we have read from cache.
    p_body_start = p_header_tail = NULL;
    // We must reset the field name and value, because we may get a partial value in on_header_value.
    field_name = field_value = "";
    // The header of the request.
    srs_freep(header);
    header = new SrsHttpHeader();
    
    // do parse
    if ((err = parse_message_imp(reader)) != srs_success) {
        return srs_error_wrap(err, "parse message");
    }
    
    // create msg
    SrsHttpMessage* msg = new SrsHttpMessage(reader, buffer);

    // Initialize the basic information.
    msg->set_basic(hp_header.method, hp_header.status_code, hp_header.content_length);
    msg->set_header(header, http_should_keep_alive(&hp_header));
    if ((err = msg->set_url(url, jsonp)) != srs_success) {
        srs_freep(msg);
        return srs_error_wrap(err, "set url=%s, jsonp=%d", url.c_str(), jsonp);
    }
    
    // parse ok, return the msg.
    *ppmsg = msg;
    
    return err;
}

srs_error_t SrsHttpParser::parse_message_imp(ISrsReader* reader)
{
    srs_error_t err = srs_success;
    
    while (true) {
        if (buffer->size() > 0) {
            ssize_t consumed = http_parser_execute(&parser, &settings, buffer->bytes(), buffer->size());

            // The error is set in http_errno.
            enum http_errno code;
	        if ((code = HTTP_PARSER_ERRNO(&parser)) != HPE_OK) {
	            return srs_error_new(ERROR_HTTP_PARSE_HEADER, "parse %dB, nparsed=%d, err=%d/%s %s",
	                buffer->size(), consumed, code, http_errno_name(code), http_errno_description(code));
	        }

            // When buffer consumed these bytes, it's dropped so the new ptr is actually the HTTP body. But http-parser
            // doesn't indicate the specific sizeof header, so we must finger it out.
            // @remark We shouldn't use on_body, because it only works for normal case, and losts the chunk header and length.
            // @see https://github.com/ossrs/srs/issues/1508
	        if (p_header_tail && buffer->bytes() < p_body_start) {
	            for (const char* p = p_header_tail; p <= p_body_start - 4; p++) {
	                if (p[0] == SRS_CONSTS_CR && p[1] == SRS_CONSTS_LF && p[2] == SRS_CONSTS_CR && p[3] == SRS_CONSTS_LF) {
	                    consumed = p + 4 - buffer->bytes();
	                    break;
	                }
	            }
	        }
            
            srs_info("size=%d, nparsed=%d", buffer->size(), (int)consumed);

	        // Only consume the header bytes.
            buffer->read_slice(consumed);

	        // Done when header completed, never wait for body completed, because it maybe chunked.
	        if (state >= SrsHttpParseStateHeaderComplete) {
	            break;
	        }
        }
        
        // when nothing parsed, read more to parse.
        // when requires more, only grow 1bytes, but the buffer will cache more.
        if ((err = buffer->grow(reader, buffer->size() + 1)) != srs_success) {
            return srs_error_wrap(err, "grow buffer");
        }
    }

    SrsHttpParser* obj = this;
    if (!obj->field_value.empty()) {
        obj->header->set(obj->field_name, obj->field_value);
    }
    
    return err;
}

int SrsHttpParser::on_message_begin(http_parser* parser)
{
    SrsHttpParser* obj = (SrsHttpParser*)parser->data;
    srs_assert(obj);
    
    obj->state = SrsHttpParseStateStart;
    
    srs_info("***MESSAGE BEGIN***");
    
    return 0;
}

int SrsHttpParser::on_headers_complete(http_parser* parser)
{
    SrsHttpParser* obj = (SrsHttpParser*)parser->data;
    srs_assert(obj);
    
    obj->hp_header = *parser;
    // save the parser when header parse completed.
    obj->state = SrsHttpParseStateHeaderComplete;

    // We must update the body start when header complete, because sometimes we only got header.
    // When we got the body start event, we will update it to much precious position.
    obj->p_body_start = obj->buffer->bytes() + obj->buffer->size();

    srs_info("***HEADERS COMPLETE***");
    
    // see http_parser.c:1570, return 1 to skip body.
    return 0;
}

int SrsHttpParser::on_message_complete(http_parser* parser)
{
    SrsHttpParser* obj = (SrsHttpParser*)parser->data;
    srs_assert(obj);
    
    // save the parser when body parse completed.
    obj->state = SrsHttpParseStateMessageComplete;
    
    srs_info("***MESSAGE COMPLETE***\n");
    
    return 0;
}

int SrsHttpParser::on_url(http_parser* parser, const char* at, size_t length)
{
    SrsHttpParser* obj = (SrsHttpParser*)parser->data;
    srs_assert(obj);
    
    if (length > 0) {
        obj->url = string(at, (int)length);
    }

    // When header parsed, we must save the position of start for body,
    // because we have to consume the header in buffer.
    // @see https://github.com/ossrs/srs/issues/1508
    obj->p_header_tail = at;
    
    srs_info("Method: %d, Url: %.*s", parser->method, (int)length, at);
    
    return 0;
}

int SrsHttpParser::on_header_field(http_parser* parser, const char* at, size_t length)
{
    SrsHttpParser* obj = (SrsHttpParser*)parser->data;
    srs_assert(obj);

    if (!obj->field_value.empty()) {
        obj->header->set(obj->field_name, obj->field_value);
        obj->field_name = obj->field_value = "";
    }
    
    if (length > 0) {
        obj->field_name.append(at, (int)length);
    }

    // When header parsed, we must save the position of start for body,
    // because we have to consume the header in buffer.
    // @see https://github.com/ossrs/srs/issues/1508
    obj->p_header_tail = at;
    
    srs_info("Header field(%d bytes): %.*s", (int)length, (int)length, at);
    return 0;
}

int SrsHttpParser::on_header_value(http_parser* parser, const char* at, size_t length)
{
    SrsHttpParser* obj = (SrsHttpParser*)parser->data;
    srs_assert(obj);
    
    if (length > 0) {
        obj->field_value.append(at, (int)length);
    }

    // When header parsed, we must save the position of start for body,
    // because we have to consume the header in buffer.
    // @see https://github.com/ossrs/srs/issues/1508
    obj->p_header_tail = at;
    
    srs_info("Header value(%d bytes): %.*s", (int)length, (int)length, at);
    return 0;
}

int SrsHttpParser::on_body(http_parser* parser, const char* at, size_t length)
{
    SrsHttpParser* obj = (SrsHttpParser*)parser->data;
    srs_assert(obj);

    // save the parser when body parsed.
    obj->state = SrsHttpParseStateBody;

    // Used to discover the header length.
    // @see https://github.com/ossrs/srs/issues/1508
    obj->p_body_start = at;

    srs_info("Body: %.*s", (int)length, at);
    
    return 0;
}

SrsHttpMessage::SrsHttpMessage(ISrsReader* reader, SrsFastStream* buffer) : ISrsHttpMessage()
{
    owner_conn = NULL;
    chunked = false;
    infinite_chunked = false;
    _uri = new SrsHttpUri();
    _body = new SrsHttpResponseReader(this, reader, buffer);

    jsonp = false;

    // As 0 is DELETE, so we use GET as default.
    _method = SRS_CONSTS_HTTP_GET;
    // 200 is ok.
    _status = SRS_CONSTS_HTTP_OK;
    // -1 means infinity chunked mode.
    _content_length = -1;
    // From HTTP/1.1, default to keep alive.
    _keep_alive = true;
}

SrsHttpMessage::~SrsHttpMessage()
{
    srs_freep(_body);
    srs_freep(_uri);
}

void SrsHttpMessage::set_basic(uint8_t method, uint16_t status, int64_t content_length)
{
    _method = method;
    _status = status;
    if (_content_length == -1) {
        _content_length = content_length;
    }
}

void SrsHttpMessage::set_header(SrsHttpHeader* header, bool keep_alive)
{
    _header = *header;
    _keep_alive = keep_alive;

    // whether chunked.
    chunked = (header->get("Transfer-Encoding") == "chunked");

    // Update the content-length in header.
    string clv = header->get("Content-Length");
    if (!clv.empty()) {
        _content_length = ::atoll(clv.c_str());
    }
}

srs_error_t SrsHttpMessage::set_url(string url, bool allow_jsonp)
{
    srs_error_t err = srs_success;
    
    _url = url;

    // parse uri from schema/server:port/path?query
    std::string uri = _url;

    if (!srs_string_contains(uri, "://")) {
        // use server public ip when host not specified.
        // to make telnet happy.
        std::string host = _header.get("Host");
        if (host.empty()) {
            host= srs_get_public_internet_address();
        }
        if (!host.empty()) {
            uri = "http://" + host + _url;
        }
    }

    if ((err = _uri->initialize(uri)) != srs_success) {
        return srs_error_wrap(err, "init uri %s", uri.c_str());
    }
    
    // parse ext.
    _ext = srs_path_filext(_uri->get_path());
    
    // parse query string.
    srs_parse_query_string(_uri->get_query(), _query);
    
    // parse jsonp request message.
    if (allow_jsonp) {
        if (!query_get("callback").empty()) {
            jsonp = true;
        }
        if (jsonp) {
            jsonp_method = query_get("method");
        }
    }
    
    return err;
}

ISrsConnection* SrsHttpMessage::connection()
{
    return owner_conn;
}

void SrsHttpMessage::set_connection(ISrsConnection* conn)
{
    owner_conn = conn;
}

uint8_t SrsHttpMessage::method()
{
    if (jsonp && !jsonp_method.empty()) {
        if (jsonp_method == "GET") {
            return SRS_CONSTS_HTTP_GET;
        } else if (jsonp_method == "PUT") {
            return SRS_CONSTS_HTTP_PUT;
        } else if (jsonp_method == "POST") {
            return SRS_CONSTS_HTTP_POST;
        } else if (jsonp_method == "DELETE") {
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
    if (jsonp && !jsonp_method.empty()) {
        return jsonp_method;
    }
    
    if (is_http_get()) {
        return "GET";
    }
    if (is_http_put()) {
        return "PUT";
    }
    if (is_http_post()) {
        return "POST";
    }
    if (is_http_delete()) {
        return "DELETE";
    }
    if (is_http_options()) {
        return "OPTIONS";
    }
    
    return "OTHER";
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
    return chunked;
}

bool SrsHttpMessage::is_keep_alive()
{
    return _keep_alive;
}

bool SrsHttpMessage::is_infinite_chunked()
{
    return infinite_chunked;
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

int SrsHttpMessage::parse_rest_id(string pattern)
{
    string p = _uri->get_path();
    if (p.length() <= pattern.length()) {
        return -1;
    }
    
    string id = p.substr((int)pattern.length());
    if (!id.empty()) {
        return ::atoi(id.c_str());
    }
    
    return -1;
}

srs_error_t SrsHttpMessage::enter_infinite_chunked()
{
    srs_error_t err = srs_success;
    
    if (infinite_chunked) {
        return err;
    }
    
    if (is_chunked() || content_length() != -1) {
        return srs_error_new(ERROR_HTTP_DATA_INVALID, "not infinited chunked");
    }
    
    infinite_chunked = true;
    
    return err;
}

srs_error_t SrsHttpMessage::body_read_all(string& body)
{
    srs_error_t err = srs_success;
    
    // cache to read.
    char* buf = new char[SRS_HTTP_READ_CACHE_BYTES];
    SrsAutoFreeA(char, buf);
    
    // whatever, read util EOF.
    while (!_body->eof()) {
        ssize_t nb_read = 0;
        if ((err = _body->read(buf, SRS_HTTP_READ_CACHE_BYTES, &nb_read)) != srs_success) {
            return srs_error_wrap(err, "read body");
        }
        
        if (nb_read > 0) {
            body.append(buf, nb_read);
        }
    }
    
    return err;
}

ISrsHttpResponseReader* SrsHttpMessage::body_reader()
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

SrsHttpHeader* SrsHttpMessage::header()
{
    return &_header;
}

SrsRequest* SrsHttpMessage::to_request(string vhost)
{
    SrsRequest* req = new SrsRequest();
    
    // http path, for instance, /live/livestream.flv, parse to
    //      app: /live
    //      stream: livestream.flv
    srs_parse_rtmp_url(_uri->get_path(), req->app, req->stream);
    
    // trim the start slash, for instance, /live to live
    req->app = srs_string_trim_start(req->app, "/");
    
    // remove the extension, for instance, livestream.flv to livestream
    req->stream = srs_path_filename(req->stream);
    
    // generate others.
    req->tcUrl = "rtmp://" + vhost + "/" + req->app;
    req->pageUrl = _header.get("Referer");
    req->objectEncoding = 0;

    std::string query = _uri->get_query();
    if (!query.empty()) {
        req->param = "?" + query;
    }
    
    srs_discovery_tc_url(req->tcUrl, req->schema, req->host, req->vhost, req->app, req->stream, req->port, req->param);
    req->strip();
    
    // reset the host to http request host.
    if (req->host == SRS_CONSTS_RTMP_DEFAULT_VHOST) {
        req->host = _uri->get_host();
    }

    // Set ip by remote ip of connection.
    if (owner_conn) {
        req->ip = owner_conn->remote_ip();
    }

    // Overwrite by ip from proxy.
    string oip = srs_get_original_ip(this);
    if (!oip.empty()) {
        req->ip = oip;
    }
    
    return req;
}

bool SrsHttpMessage::is_jsonp()
{
    return jsonp;
}

ISrsHttpHeaderFilter::ISrsHttpHeaderFilter()
{
}

ISrsHttpHeaderFilter::~ISrsHttpHeaderFilter()
{
}

SrsHttpResponseWriter::SrsHttpResponseWriter(ISrsProtocolReadWriter* io)
{
    skt = io;
    hdr = new SrsHttpHeader();
    header_wrote = false;
    status = SRS_CONSTS_HTTP_OK;
    content_length = -1;
    written = 0;
    header_sent = false;
    nb_iovss_cache = 0;
    iovss_cache = NULL;
    hf = NULL;
}

SrsHttpResponseWriter::~SrsHttpResponseWriter()
{
    srs_freep(hdr);
    srs_freepa(iovss_cache);
}

srs_error_t SrsHttpResponseWriter::final_request()
{
    srs_error_t err = srs_success;

    // write the header data in memory.
    if (!header_wrote) {
        write_header(SRS_CONSTS_HTTP_OK);
    }

    // whatever header is wrote, we should try to send header.
    if ((err = send_header(NULL, 0)) != srs_success) {
        return srs_error_wrap(err, "send header");
    }
    
    // complete the chunked encoding.
    if (content_length == -1) {
        std::stringstream ss;
        ss << 0 << SRS_HTTP_CRLF << SRS_HTTP_CRLF;
        std::string ch = ss.str();
        return skt->write((void*)ch.data(), (int)ch.length(), NULL);
    }
    
    // flush when send with content length
    return write(NULL, 0);
}

SrsHttpHeader* SrsHttpResponseWriter::header()
{
    return hdr;
}

srs_error_t SrsHttpResponseWriter::write(char* data, int size)
{
    srs_error_t err = srs_success;
    
    // write the header data in memory.
    if (!header_wrote) {
        if (hdr->content_type().empty()) {
            hdr->set_content_type("text/plain; charset=utf-8");
        }
        if (hdr->content_length() == -1) {
            hdr->set_content_length(size);
        }
        write_header(SRS_CONSTS_HTTP_OK);
    }
    
    // whatever header is wrote, we should try to send header.
    if ((err = send_header(data, size)) != srs_success) {
        return srs_error_wrap(err, "send header");
    }
    
    // check the bytes send and content length.
    written += size;
    if (content_length != -1 && written > content_length) {
        return srs_error_new(ERROR_HTTP_CONTENT_LENGTH, "overflow writen=%d, max=%d", (int)written, (int)content_length);
    }
    
    // ignore NULL content.
    if (!data || size <= 0) {
        return err;
    }
    
    // directly send with content length
    if (content_length != -1) {
        return skt->write((void*)data, size, NULL);
    }
    
    // send in chunked encoding.
    int nb_size = snprintf(header_cache, SRS_HTTP_HEADER_CACHE_SIZE, "%x", size);
    
    iovec iovs[4];
    iovs[0].iov_base = (char*)header_cache;
    iovs[0].iov_len = (int)nb_size;
    iovs[1].iov_base = (char*)SRS_HTTP_CRLF;
    iovs[1].iov_len = 2;
    iovs[2].iov_base = (char*)data;
    iovs[2].iov_len = size;
    iovs[3].iov_base = (char*)SRS_HTTP_CRLF;
    iovs[3].iov_len = 2;
    
    ssize_t nwrite;
    if ((err = skt->writev(iovs, 4, &nwrite)) != srs_success) {
        return srs_error_wrap(err, "write chunk");
    }
    
    return err;
}

srs_error_t SrsHttpResponseWriter::writev(const iovec* iov, int iovcnt, ssize_t* pnwrite)
{
    srs_error_t err = srs_success;
    
    // when header not ready, or not chunked, send one by one.
    if (!header_wrote || content_length != -1) {
        ssize_t nwrite = 0;
        for (int i = 0; i < iovcnt; i++) {
            nwrite += iov[i].iov_len;
            if ((err = write((char*)iov[i].iov_base, (int)iov[i].iov_len)) != srs_success) {
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
    iovec* iovss = iovss_cache;
    if (nb_iovss_cache < nb_iovss) {
        srs_freepa(iovss_cache);
        nb_iovss_cache = nb_iovss;
        iovss = iovss_cache = new iovec[nb_iovss];
    }
    
    // Send all iovs in one chunk, the size is the total size of iovs.
    int size = 0;
    for (int i = 0; i < iovcnt; i++) {
        const iovec* data_iov = iov + i;
        size += data_iov->iov_len;
    }
    written += size;
    
    // chunk header
    int nb_size = snprintf(header_cache, SRS_HTTP_HEADER_CACHE_SIZE, "%x", size);
    iovss[0].iov_base = (char*)header_cache;
    iovss[0].iov_len = (int)nb_size;

    // chunk header eof.
    iovss[1].iov_base = (char*)SRS_HTTP_CRLF;
    iovss[1].iov_len = 2;

    // chunk body.
    for (int i = 0; i < iovcnt; i++) {
        iovss[2+i].iov_base = (char*)iov[i].iov_base;
        iovss[2+i].iov_len = (int)iov[i].iov_len;
    }
    
    // chunk body eof.
    iovss[2+iovcnt].iov_base = (char*)SRS_HTTP_CRLF;
    iovss[2+iovcnt].iov_len = 2;

    // sendout all ioves.
    ssize_t nwrite;
    if ((err = srs_write_large_iovs(skt, iovss, nb_iovss, &nwrite)) != srs_success) {
        return srs_error_wrap(err, "writev large iovs");
    }
    
    if (pnwrite) {
        *pnwrite = nwrite;
    }
    
    return err;
}

void SrsHttpResponseWriter::write_header(int code)
{
    if (header_wrote) {
        srs_warn("http: multiple write_header calls, code=%d", code);
        return;
    }
    
    header_wrote = true;
    status = code;
    
    // parse the content length from header.
    content_length = hdr->content_length();
}

srs_error_t SrsHttpResponseWriter::send_header(char* data, int size)
{
    srs_error_t err = srs_success;
    
    if (header_sent) {
        return err;
    }
    header_sent = true;
    
    std::stringstream ss;
    
    // status_line
    ss << "HTTP/1.1 " << status << " "
    << srs_generate_http_status_text(status) << SRS_HTTP_CRLF;
    
    // detect content type
    if (srs_go_http_body_allowd(status)) {
        if (data && hdr->content_type().empty()) {
            hdr->set_content_type(srs_go_http_detect(data, size));
        }
    }
    
    // set server if not set.
    if (hdr->get("Server").empty()) {
        hdr->set("Server", RTMP_SIG_SRS_SERVER);
    }
    
    // chunked encoding
    if (content_length == -1) {
        hdr->set("Transfer-Encoding", "chunked");
    }
    
    // keep alive to make vlc happy.
    if (hdr->get("Connection").empty()) {
        hdr->set("Connection", "Keep-Alive");
    }

    // Filter the header before writing it.
    if (hf && ((err = hf->filter(hdr)) != srs_success)) {
        return srs_error_wrap(err, "filter header");
    }
    
    // write header
    hdr->write(ss);
    
    // header_eof
    ss << SRS_HTTP_CRLF;
    
    std::string buf = ss.str();
    return skt->write((void*)buf.c_str(), buf.length(), NULL);
}

SrsHttpResponseReader::SrsHttpResponseReader(SrsHttpMessage* msg, ISrsReader* reader, SrsFastStream* body)
{
    skt = reader;
    owner = msg;
    is_eof = false;
    nb_total_read = 0;
    nb_left_chunk = 0;
    buffer = body;
}

SrsHttpResponseReader::~SrsHttpResponseReader()
{
}

bool SrsHttpResponseReader::eof()
{
    return is_eof;
}

srs_error_t SrsHttpResponseReader::read(void* data, size_t nb_data, ssize_t* nb_read)
{
    srs_error_t err = srs_success;
    
    if (is_eof) {
        return srs_error_new(ERROR_HTTP_RESPONSE_EOF, "EOF");
    }
    
    // chunked encoding.
    if (owner->is_chunked()) {
        return read_chunked(data, nb_data, nb_read);
    }
    
    // read by specified content-length
    if (owner->content_length() != -1) {
        size_t max = (size_t)owner->content_length() - (size_t)nb_total_read;
        if (max <= 0) {
            is_eof = true;
            return err;
        }
        
        // change the max to read.
        nb_data = srs_min(nb_data, max);
        return read_specified(data, nb_data, nb_read);
    }
    
    // infinite chunked mode, directly read.
    if (owner->is_infinite_chunked()) {
        srs_assert(!owner->is_chunked() && owner->content_length() == -1);
        return read_specified(data, nb_data, nb_read);
    }
    
    // infinite chunked mode, but user not set it,
    // we think there is no data left.
    is_eof = true;
    
    return err;
}

srs_error_t SrsHttpResponseReader::read_chunked(void* data, size_t nb_data, ssize_t* nb_read)
{
    srs_error_t err = srs_success;
    
    // when no bytes left in chunk,
    // parse the chunk length first.
    if (nb_left_chunk <= 0) {
        char* at = NULL;
        int length = 0;
        while (!at) {
            // find the CRLF of chunk header end.
            char* start = buffer->bytes();
            char* end = start + buffer->size();
            for (char* p = start; p < end - 1; p++) {
                if (p[0] == SRS_HTTP_CR && p[1] == SRS_HTTP_LF) {
                    // invalid chunk, ignore.
                    if (p == start) {
                        return srs_error_new(ERROR_HTTP_INVALID_CHUNK_HEADER, "chunk header");
                    }
                    length = (int)(p - start + 2);
                    at = buffer->read_slice(length);
                    break;
                }
            }
            
            // got at, ok.
            if (at) {
                break;
            }
            
            // when empty, only grow 1bytes, but the buffer will cache more.
            if ((err = buffer->grow(skt, buffer->size() + 1)) != srs_success) {
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
        char* at_parsed = at; errno = 0;
        int ilength = (int)::strtol(at, &at_parsed, 16);
        if (ilength < 0 || errno != 0 || at_parsed - at != length - 2) {
            return srs_error_new(ERROR_HTTP_INVALID_CHUNK_HEADER, "invalid length %s as %d, parsed=%.*s, errno=%d",
                at, ilength, (int)(at_parsed-at), at, errno);
        }
        
        // all bytes in chunk is left now.
        nb_chunk = nb_left_chunk = (size_t)ilength;
    }
    
    if (nb_chunk <= 0) {
        // for the last chunk, eof.
        is_eof = true;
        *nb_read = 0;
    } else {
        // for not the last chunk, there must always exists bytes.
        // left bytes in chunk, read some.
        srs_assert(nb_left_chunk);
        
        size_t nb_bytes = srs_min(nb_left_chunk, nb_data);
        err = read_specified(data, nb_bytes, (ssize_t*)&nb_bytes);
        
        // the nb_bytes used for output already read size of bytes.
        if (nb_read) {
            *nb_read = nb_bytes;
        }
        nb_left_chunk -= nb_bytes;

        if (err != srs_success) {
            return srs_error_wrap(err, "read specified");
        }

        // If still left bytes in chunk, ignore and read in future.
        if (nb_left_chunk > 0) {
            return err;
        }
    }
    
    // for both the last or not, the CRLF of chunk payload end.
    if ((err = buffer->grow(skt, 2)) != srs_success) {
        return srs_error_wrap(err, "grow buffer");
    }
    buffer->read_slice(2);
    
    return err;
}

srs_error_t SrsHttpResponseReader::read_specified(void* data, size_t nb_data, ssize_t* nb_read)
{
    srs_error_t err = srs_success;
    
    if (buffer->size() <= 0) {
        // when empty, only grow 1bytes, but the buffer will cache more.
        if ((err = buffer->grow(skt, 1)) != srs_success) {
            return srs_error_wrap(err, "grow buffer");
        }
    }
    
    size_t nb_bytes = srs_min(nb_data, (size_t)buffer->size());
    
    // read data to buffer.
    srs_assert(nb_bytes);
    char* p = buffer->read_slice(nb_bytes);
    memcpy(data, p, nb_bytes);
    if (nb_read) {
        *nb_read = nb_bytes;
    }
    
    // increase the total read to determine whether EOF.
    nb_total_read += nb_bytes;
    
    // for not chunked and specified content length.
    if (!owner->is_chunked() && owner->content_length() != -1) {
        // when read completed, eof.
        if (nb_total_read >= (int)owner->content_length()) {
            is_eof = true;
        }
    }
    
    return err;
}

