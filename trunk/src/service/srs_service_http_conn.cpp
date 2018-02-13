/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2018 Winlin
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

SrsHttpParser::SrsHttpParser()
{
    buffer = new SrsFastStream();
}

SrsHttpParser::~SrsHttpParser()
{
    srs_freep(buffer);
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

srs_error_t SrsHttpParser::parse_message(ISrsProtocolReaderWriter* io, SrsConnection* conn, ISrsHttpMessage** ppmsg)
{
    *ppmsg = NULL;
    
    srs_error_t err = srs_success;
    
    // reset request data.
    field_name = "";
    field_value = "";
    expect_field_name = true;
    state = SrsHttpParseStateInit;
    header = http_parser();
    url = "";
    headers.clear();
    header_parsed = 0;
    
    // do parse
    if ((err = parse_message_imp(io)) != srs_success) {
        return srs_error_wrap(err, "parse message");
    }
    
    // create msg
    SrsHttpMessage* msg = new SrsHttpMessage(io, conn);
    
    // initalize http msg, parse url.
    if ((err = msg->update(url, jsonp, &header, buffer, headers)) != srs_success) {
        srs_freep(msg);
        return srs_error_wrap(err, "update message");
    }
    
    // parse ok, return the msg.
    *ppmsg = msg;
    
    return err;
}

srs_error_t SrsHttpParser::parse_message_imp(ISrsProtocolReaderWriter* io)
{
    srs_error_t err = srs_success;
    
    while (true) {
        ssize_t nparsed = 0;
        
        // when got entire http header, parse it.
        // @see https://github.com/ossrs/srs/issues/400
        char* start = buffer->bytes();
        char* end = start + buffer->size();
        for (char* p = start; p <= end - 4; p++) {
            // SRS_HTTP_CRLFCRLF "\r\n\r\n" // 0x0D0A0D0A
            if (p[0] == SRS_CONSTS_CR && p[1] == SRS_CONSTS_LF && p[2] == SRS_CONSTS_CR && p[3] == SRS_CONSTS_LF) {
                nparsed = http_parser_execute(&parser, &settings, buffer->bytes(), buffer->size());
                srs_info("buffer=%d, nparsed=%d, header=%d", buffer->size(), (int)nparsed, header_parsed);
                break;
            }
        }
        
        // consume the parsed bytes.
        if (nparsed && header_parsed) {
            buffer->read_slice(header_parsed);
        }
        
        // ok atleast header completed,
        // never wait for body completed, for maybe chunked.
        if (state == SrsHttpParseStateHeaderComplete || state == SrsHttpParseStateMessageComplete) {
            break;
        }
        
        // when nothing parsed, read more to parse.
        if (nparsed == 0) {
            // when requires more, only grow 1bytes, but the buffer will cache more.
            if ((err = buffer->grow(io, buffer->size() + 1)) != srs_success) {
                return srs_error_wrap(err, "grow buffer");
            }
        }
    }
    
    // parse last header.
    if (!field_name.empty() && !field_value.empty()) {
        headers.push_back(std::make_pair(field_name, field_value));
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
    
    obj->header = *parser;
    // save the parser when header parse completed.
    obj->state = SrsHttpParseStateHeaderComplete;
    obj->header_parsed = (int)parser->nread;
    
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
        obj->url.append(at, (int)length);
    }
    
    srs_info("Method: %d, Url: %.*s", parser->method, (int)length, at);
    
    return 0;
}

int SrsHttpParser::on_header_field(http_parser* parser, const char* at, size_t length)
{
    SrsHttpParser* obj = (SrsHttpParser*)parser->data;
    srs_assert(obj);
    
    // field value=>name, reap the field.
    if (!obj->expect_field_name) {
        obj->headers.push_back(std::make_pair(obj->field_name, obj->field_value));
        
        // reset the field name when parsed.
        obj->field_name = "";
        obj->field_value = "";
    }
    obj->expect_field_name = true;
    
    if (length > 0) {
        obj->field_name.append(at, (int)length);
    }
    
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
    obj->expect_field_name = false;
    
    srs_info("Header value(%d bytes): %.*s", (int)length, (int)length, at);
    return 0;
}

int SrsHttpParser::on_body(http_parser* parser, const char* at, size_t length)
{
    SrsHttpParser* obj = (SrsHttpParser*)parser->data;
    srs_assert(obj);
    
    srs_info("Body: %.*s", (int)length, at);
    
    return 0;
}

SrsHttpMessage::SrsHttpMessage(ISrsProtocolReaderWriter* io, SrsConnection* c) : ISrsHttpMessage()
{
    conn = c;
    chunked = false;
    infinite_chunked = false;
    keep_alive = true;
    _uri = new SrsHttpUri();
    _body = new SrsHttpResponseReader(this, io);
    _http_ts_send_buffer = new char[SRS_HTTP_TS_SEND_BUFFER_SIZE];
    jsonp = false;
}

SrsHttpMessage::~SrsHttpMessage()
{
    srs_freep(_body);
    srs_freep(_uri);
    srs_freepa(_http_ts_send_buffer);
}

srs_error_t SrsHttpMessage::update(string url, bool allow_jsonp, http_parser* header, SrsFastStream* body, vector<SrsHttpHeaderField>& headers)
{
    srs_error_t err = srs_success;
    
    _url = url;
    _header = *header;
    _headers = headers;
    
    // whether chunked.
    std::string transfer_encoding = get_request_header("Transfer-Encoding");
    chunked = (transfer_encoding == "chunked");
    
    // whether keep alive.
    keep_alive = http_should_keep_alive(header);
    
    // set the buffer.
    if ((err = _body->initialize(body)) != srs_success) {
        return srs_error_wrap(err, "init body");
    }
    
    // parse uri from url.
    std::string host = get_request_header("Host");
    
    // use server public ip when no host specified.
    // to make telnet happy.
    if (host.empty()) {
        host= srs_get_public_internet_address();
    }
    
    // parse uri to schema/server:port/path?query
    std::string uri = "http://" + host + _url;
    if ((err = _uri->initialize(uri)) != srs_success) {
        return srs_error_wrap(err, "init uri");
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

SrsConnection* SrsHttpMessage::connection()
{
    return conn;
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
    
    return (uint8_t)_header.method;
}

uint16_t SrsHttpMessage::status_code()
{
    return (uint16_t)_header.status_code;
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
    return _header.method == SRS_CONSTS_HTTP_OPTIONS;
}

bool SrsHttpMessage::is_chunked()
{
    return chunked;
}

bool SrsHttpMessage::is_keep_alive()
{
    return keep_alive;
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
    return _uri->get_host();
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
        int nb_read = 0;
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
    return _header.content_length;
}

string SrsHttpMessage::query_get(string key)
{
    std::string v;
    
    if (_query.find(key) != _query.end()) {
        v = _query[key];
    }
    
    return v;
}

int SrsHttpMessage::request_header_count()
{
    return (int)_headers.size();
}

string SrsHttpMessage::request_header_key_at(int index)
{
    srs_assert(index < request_header_count());
    SrsHttpHeaderField item = _headers[index];
    return item.first;
}

string SrsHttpMessage::request_header_value_at(int index)
{
    srs_assert(index < request_header_count());
    SrsHttpHeaderField item = _headers[index];
    return item.second;
}

string SrsHttpMessage::get_request_header(string name)
{
    std::vector<SrsHttpHeaderField>::iterator it;
    
    for (it = _headers.begin(); it != _headers.end(); ++it) {
        SrsHttpHeaderField& elem = *it;
        std::string key = elem.first;
        std::string value = elem.second;
        if (key == name) {
            return value;
        }
    }
    
    return "";
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
    req->pageUrl = get_request_header("Referer");
    req->objectEncoding = 0;
    
    srs_discovery_tc_url(req->tcUrl, req->schema, req->host, req->vhost, req->app, req->stream, req->port, req->param);
    req->strip();
    
    // reset the host to http request host.
    if (req->host == SRS_CONSTS_RTMP_DEFAULT_VHOST) {
        req->host = _uri->get_host();
    }
    
    return req;
}

bool SrsHttpMessage::is_jsonp()
{
    return jsonp;
}

SrsHttpResponseWriter::SrsHttpResponseWriter(SrsStSocket* io)
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
}

SrsHttpResponseWriter::~SrsHttpResponseWriter()
{
    srs_freep(hdr);
    srs_freepa(iovss_cache);
}

srs_error_t SrsHttpResponseWriter::final_request()
{
    // write the header data in memory.
    if (!header_wrote) {
        write_header(SRS_CONSTS_HTTP_OK);
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
    if (!data) {
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
            const iovec* piovc = iov + i;
            nwrite += piovc->iov_len;
            if ((err = write((char*)piovc->iov_base, (int)piovc->iov_len)) != srs_success) {
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
    
    // send in chunked encoding.
    int nb_iovss = 3 + iovcnt;
    iovec* iovss = iovss_cache;
    if (nb_iovss_cache < nb_iovss) {
        srs_freepa(iovss_cache);
        nb_iovss_cache = nb_iovss;
        iovss = iovss_cache = new iovec[nb_iovss];
    }
    
    // send in chunked encoding.
    
    // chunk size.
    int size = 0;
    for (int i = 0; i < iovcnt; i++) {
        const iovec* data_iov = iov + i;
        size += data_iov->iov_len;
    }
    written += size;
    
    // chunk header
    int nb_size = snprintf(header_cache, SRS_HTTP_HEADER_CACHE_SIZE, "%x", size);
    iovec* iovs = iovss;
    iovs[0].iov_base = (char*)header_cache;
    iovs[0].iov_len = (int)nb_size;
    iovs++;
    
    // chunk header eof.
    iovs[0].iov_base = (char*)SRS_HTTP_CRLF;
    iovs[0].iov_len = 2;
    iovs++;
    
    // chunk body.
    for (int i = 0; i < iovcnt; i++) {
        const iovec* data_iov = iov + i;
        iovs[0].iov_base = (char*)data_iov->iov_base;
        iovs[0].iov_len = (int)data_iov->iov_len;
        iovs++;
    }
    
    // chunk body eof.
    iovs[0].iov_base = (char*)SRS_HTTP_CRLF;
    iovs[0].iov_len = 2;
    iovs++;
    
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
        if (hdr->content_type().empty()) {
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
    hdr->set("Connection", "Keep-Alive");
    
    // write headers
    hdr->write(ss);
    
    // header_eof
    ss << SRS_HTTP_CRLF;
    
    std::string buf = ss.str();
    return skt->write((void*)buf.c_str(), buf.length(), NULL);
}

SrsHttpResponseReader::SrsHttpResponseReader(SrsHttpMessage* msg, ISrsProtocolReaderWriter* io)
{
    skt = io;
    owner = msg;
    is_eof = false;
    nb_total_read = 0;
    nb_left_chunk = 0;
    buffer = NULL;
}

SrsHttpResponseReader::~SrsHttpResponseReader()
{
}

srs_error_t SrsHttpResponseReader::initialize(SrsFastStream* body)
{
    srs_error_t err = srs_success;
    
    nb_chunk = 0;
    nb_left_chunk = 0;
    nb_total_read = 0;
    buffer = body;
    
    return err;
}

bool SrsHttpResponseReader::eof()
{
    return is_eof;
}

srs_error_t SrsHttpResponseReader::read(char* data, int nb_data, int* nb_read)
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
        int max = (int)owner->content_length() - (int)nb_total_read;
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

srs_error_t SrsHttpResponseReader::read_chunked(char* data, int nb_data, int* nb_read)
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
        int ilength = (int)::strtol(at, NULL, 16);
        if (ilength < 0) {
            return srs_error_new(ERROR_HTTP_INVALID_CHUNK_HEADER, "invalid length=%d", ilength);
        }
        
        // all bytes in chunk is left now.
        nb_chunk = nb_left_chunk = ilength;
    }
    
    if (nb_chunk <= 0) {
        // for the last chunk, eof.
        is_eof = true;
    } else {
        // for not the last chunk, there must always exists bytes.
        // left bytes in chunk, read some.
        srs_assert(nb_left_chunk);
        
        int nb_bytes = srs_min(nb_left_chunk, nb_data);
        err = read_specified(data, nb_bytes, &nb_bytes);
        
        // the nb_bytes used for output already read size of bytes.
        if (nb_read) {
            *nb_read = nb_bytes;
        }
        nb_left_chunk -= nb_bytes;
        
        // error or still left bytes in chunk, ignore and read in future.
        if (err != srs_success) {
            return srs_error_wrap(err, "read specified");
        }
        if (nb_left_chunk > 0) {
            return srs_error_new(ERROR_HTTP_INVALID_CHUNK_HEADER, "read specified left=%d", nb_left_chunk);
        }
    }
    
    // for both the last or not, the CRLF of chunk payload end.
    if ((err = buffer->grow(skt, 2)) != srs_success) {
        return srs_error_wrap(err, "grow buffer");
    }
    buffer->read_slice(2);
    
    return err;
}

srs_error_t SrsHttpResponseReader::read_specified(char* data, int nb_data, int* nb_read)
{
    srs_error_t err = srs_success;
    
    if (buffer->size() <= 0) {
        // when empty, only grow 1bytes, but the buffer will cache more.
        if ((err = buffer->grow(skt, 1)) != srs_success) {
            return srs_error_wrap(err, "grow buffer");
        }
    }
    
    int nb_bytes = srs_min(nb_data, buffer->size());
    
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

