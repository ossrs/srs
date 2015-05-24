/*
The MIT License (MIT)

Copyright (c) 2013-2015 SRS(simple-rtmp-server)

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

#include <srs_app_http_conn.hpp>

#if defined(SRS_AUTO_HTTP_PARSER) || defined(SRS_AUTO_HTTP_SERVER)

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>

#include <sstream>
using namespace std;

#include <srs_protocol_buffer.hpp>
#include <srs_rtmp_utility.hpp>
#include <srs_kernel_log.hpp>
#include <srs_kernel_error.hpp>
#include <srs_app_st_socket.hpp>
#include <srs_core_autofree.hpp>
#include <srs_app_config.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_kernel_file.hpp>
#include <srs_kernel_flv.hpp>
#include <srs_rtmp_sdk.hpp>
#include <srs_app_source.hpp>
#include <srs_rtmp_msg_array.hpp>
#include <srs_kernel_aac.hpp>
#include <srs_kernel_mp3.hpp>
#include <srs_kernel_ts.hpp>
#include <srs_app_pithy_print.hpp>
#include <srs_app_source.hpp>
#include <srs_app_server.hpp>

#endif

#ifdef SRS_AUTO_HTTP_PARSER

SrsHttpResponseWriter::SrsHttpResponseWriter(SrsStSocket* io)
{
    skt = io;
    hdr = new SrsHttpHeader();
    header_wrote = false;
    status = SRS_CONSTS_HTTP_OK;
    content_length = -1;
    written = 0;
    header_sent = false;
}

SrsHttpResponseWriter::~SrsHttpResponseWriter()
{
    srs_freep(hdr);
}

int SrsHttpResponseWriter::final_request()
{
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

int SrsHttpResponseWriter::write(char* data, int size)
{
    int ret = ERROR_SUCCESS;
    
    if (!header_wrote) {
        write_header(SRS_CONSTS_HTTP_OK);
    }
    
    written += size;
    if (content_length != -1 && written > content_length) {
        ret = ERROR_HTTP_CONTENT_LENGTH;
        srs_error("http: exceed content length. ret=%d", ret);
        return ret;
    }
    
    if ((ret = send_header(data, size)) != ERROR_SUCCESS) {
        srs_error("http: send header failed. ret=%d", ret);
        return ret;
    }
    
    // ignore NULL content.
    if (!data) {
        return ret;
    }
    
    // directly send with content length
    if (content_length != -1) {
        return skt->write((void*)data, size, NULL);
    }
    
    // send in chunked encoding.
    std::stringstream ss;
    ss << hex << size << SRS_HTTP_CRLF;
    std::string ch = ss.str();
    if ((ret = skt->write((void*)ch.data(), (int)ch.length(), NULL)) != ERROR_SUCCESS) {
        return ret;
    }
    if ((ret = skt->write((void*)data, size, NULL)) != ERROR_SUCCESS) {
        return ret;
    }
    if ((ret = skt->write((void*)SRS_HTTP_CRLF, 2, NULL)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
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

int SrsHttpResponseWriter::send_header(char* data, int size)
{
    int ret = ERROR_SUCCESS;
    
    if (header_sent) {
        return ret;
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
        hdr->set("Server", RTMP_SIG_SRS_KEY"/"RTMP_SIG_SRS_VERSION);
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

SrsHttpResponseReader::SrsHttpResponseReader(SrsHttpMessage* msg, SrsStSocket* io)
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

int SrsHttpResponseReader::initialize(SrsFastBuffer* body)
{
    int ret = ERROR_SUCCESS;
    
    nb_chunk = 0;
    nb_left_chunk = 0;
    nb_total_read = 0;
    buffer = body;
    
    return ret;
}

bool SrsHttpResponseReader::eof()
{
    return is_eof;
}

int SrsHttpResponseReader::read(char* data, int nb_data, int* nb_read)
{
    int ret = ERROR_SUCCESS;
    
    if (is_eof) {
        ret = ERROR_HTTP_RESPONSE_EOF;
        srs_error("http: response EOF. ret=%d", ret);
        return ret;
    }
    
    // chunked encoding.
    if (owner->is_chunked()) {
        return read_chunked(data, nb_data, nb_read);
    }
    
    // read by specified content-length
    int max = (int)owner->content_length() - (int)nb_total_read;
    if (max <= 0) {
        is_eof = true;
        return ret;
    }
    
    // change the max to read.
    nb_data = srs_min(nb_data, max);
    return read_specified(data, nb_data, nb_read);
}

int SrsHttpResponseReader::read_chunked(char* data, int nb_data, int* nb_read)
{
    int ret = ERROR_SUCCESS;
    
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
                        ret = ERROR_HTTP_INVALID_CHUNK_HEADER;
                        srs_error("chunk header start with CRLF. ret=%d", ret);
                        return ret;
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
            if ((ret = buffer->grow(skt, buffer->size() + 1)) != ERROR_SUCCESS) {
                if (!srs_is_client_gracefully_close(ret)) {
                    srs_error("read body from server failed. ret=%d", ret);
                }
                return ret;
            }
        }
        srs_assert(length >= 3);
        
        // it's ok to set the pos and pos+1 to NULL.
        at[length - 1] = 0;
        at[length - 2] = 0;
        
        // size is the bytes size, excludes the chunk header and end CRLF.
        int ilength = (int)::strtol(at, NULL, 16);
        if (ilength < 0) {
            ret = ERROR_HTTP_INVALID_CHUNK_HEADER;
            srs_error("chunk header negative, length=%d. ret=%d", ilength, ret);
            return ret;
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
        ret = read_specified(data, nb_bytes, &nb_bytes);
        
        // the nb_bytes used for output already read size of bytes.
        if (nb_read) {
            *nb_read = nb_bytes;
        }
        nb_left_chunk -= nb_bytes;
        srs_info("http: read %d bytes of chunk", nb_bytes);
        
        // error or still left bytes in chunk, ignore and read in future.
        if (nb_left_chunk > 0 || (ret != ERROR_SUCCESS)) {
            return ret;
        }
        srs_info("http: read total chunk %dB", nb_chunk);
    }
    
    // for both the last or not, the CRLF of chunk payload end.
    if ((ret = buffer->grow(skt, 2)) != ERROR_SUCCESS) {
        if (!srs_is_client_gracefully_close(ret)) {
            srs_error("read EOF of chunk from server failed. ret=%d", ret);
        }
        return ret;
    }
    buffer->read_slice(2);
    
    return ret;
}

int SrsHttpResponseReader::read_specified(char* data, int nb_data, int* nb_read)
{
    int ret = ERROR_SUCCESS;
    
    if (buffer->size() <= 0) {
        // when empty, only grow 1bytes, but the buffer will cache more.
        if ((ret = buffer->grow(skt, 1)) != ERROR_SUCCESS) {
            if (!srs_is_client_gracefully_close(ret)) {
                srs_error("read body from server failed. ret=%d", ret);
            }
            return ret;
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
    
    // for not chunked
    if (!owner->is_chunked()) {
        // when read completed, eof.
        if (nb_total_read >= (int)owner->content_length()) {
            is_eof = true;
        }
    }
    
    return ret;
}

SrsHttpMessage::SrsHttpMessage(SrsStSocket* io, SrsConnection* c) : ISrsHttpMessage()
{
    conn = c;
    chunked = false;
    keep_alive = true;
    _uri = new SrsHttpUri();
    _body = new SrsHttpResponseReader(this, io);
    _http_ts_send_buffer = new char[SRS_HTTP_TS_SEND_BUFFER_SIZE];
}

SrsHttpMessage::~SrsHttpMessage()
{
    srs_freep(_body);
    srs_freep(_uri);
    srs_freep(_http_ts_send_buffer);
}

int SrsHttpMessage::update(string url, http_parser* header, SrsFastBuffer* body, vector<SrsHttpHeaderField>& headers)
{
    int ret = ERROR_SUCCESS;
    
    _url = url;
    _header = *header;
    _headers = headers;
    
    // whether chunked.
    std::string transfer_encoding = get_request_header("Transfer-Encoding");
    chunked = (transfer_encoding == "chunked");
    
    // whether keep alive.
    keep_alive = http_should_keep_alive(header);
    
    // set the buffer.
    if ((ret = _body->initialize(body)) != ERROR_SUCCESS) {
        return ret;
    }
    
    // parse uri from url.
    std::string host = get_request_header("Host");
    
    // donot parse the empty host for uri,
    // for example, the response contains no host,
    // ignore it is ok.
    if (host.empty()) {
        return ret;
    }
    
    // parse uri to schema/server:port/path?query
    std::string uri = "http://" + host + _url;
    if ((ret = _uri->initialize(uri)) != ERROR_SUCCESS) {
        return ret;
    }
    
    // must format as key=value&...&keyN=valueN
    std::string q = _uri->get_query();
    size_t pos = string::npos;
    while (!q.empty()) {
        std::string k = q;
        if ((pos = q.find("=")) != string::npos) {
            k = q.substr(0, pos);
            q = q.substr(pos + 1);
        } else {
            q = "";
        }
        
        std::string v = q;
        if ((pos = q.find("&")) != string::npos) {
            v = q.substr(0, pos);
            q = q.substr(pos + 1);
        } else {
            q = "";
        }
        
        _query[k] = v;
    }
    
    // parse ext.
    _ext = _uri->get_path();
    if ((pos = _ext.rfind(".")) != string::npos) {
        _ext = _ext.substr(pos);
    } else {
        _ext = "";
    }
    
    return ret;
}

SrsConnection* SrsHttpMessage::connection()
{
    return conn;
}

u_int8_t SrsHttpMessage::method()
{
    return (u_int8_t)_header.method;
}

u_int16_t SrsHttpMessage::status_code()
{
    return (u_int16_t)_header.status_code;
}

string SrsHttpMessage::method_str()
{
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
    return _header.method == SRS_CONSTS_HTTP_GET;
}

bool SrsHttpMessage::is_http_put()
{
    return _header.method == SRS_CONSTS_HTTP_PUT;
}

bool SrsHttpMessage::is_http_post()
{
    return _header.method == SRS_CONSTS_HTTP_POST;
}

bool SrsHttpMessage::is_http_delete()
{
    return _header.method == SRS_CONSTS_HTTP_DELETE;
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

string SrsHttpMessage::ext()
{
    return _ext;
}

int SrsHttpMessage::body_read_all(string& body)
{
    int ret = ERROR_SUCCESS;
    
    // cache to read.
    char* buf = new char[SRS_HTTP_READ_CACHE_BYTES];
    SrsAutoFree(char, buf);
    
    // whatever, read util EOF.
    while (!_body->eof()) {
        int nb_read = 0;
        if ((ret = _body->read(buf, SRS_HTTP_READ_CACHE_BYTES, &nb_read)) != ERROR_SUCCESS) {
            return ret;
        }
        
        if (nb_read > 0) {
            body.append(buf, nb_read);
        }
    }
    
    return ret;
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
    
    req->app = _uri->get_path();
    size_t pos = string::npos;
    if ((pos = req->app.rfind("/")) != string::npos) {
        req->stream = req->app.substr(pos + 1);
        req->app = req->app.substr(0, pos);
    }
    if ((pos = req->stream.rfind(".")) != string::npos) {
        req->stream = req->stream.substr(0, pos);
    }
    
    req->tcUrl = "rtmp://" + vhost + req->app;
    req->pageUrl = get_request_header("Referer");
    req->objectEncoding = 0;
    
    srs_discovery_tc_url(req->tcUrl,
                         req->schema, req->host, req->vhost, req->app, req->port,
                         req->param);
    req->strip();
    
    return req;
}

SrsHttpParser::SrsHttpParser()
{
    buffer = new SrsFastBuffer();
}

SrsHttpParser::~SrsHttpParser()
{
    srs_freep(buffer);
}

int SrsHttpParser::initialize(enum http_parser_type type)
{
    int ret = ERROR_SUCCESS;
    
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
    
    return ret;
}

int SrsHttpParser::parse_message(SrsStSocket* skt, SrsConnection* conn, ISrsHttpMessage** ppmsg)
{
    *ppmsg = NULL;
    
    int ret = ERROR_SUCCESS;
    
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
    if ((ret = parse_message_imp(skt)) != ERROR_SUCCESS) {
        if (!srs_is_client_gracefully_close(ret)) {
            srs_error("parse http msg failed. ret=%d", ret);
        }
        return ret;
    }
    
    // create msg
    SrsHttpMessage* msg = new SrsHttpMessage(skt, conn);
    
    // initalize http msg, parse url.
    if ((ret = msg->update(url, &header, buffer, headers)) != ERROR_SUCCESS) {
        srs_error("initialize http msg failed. ret=%d", ret);
        srs_freep(msg);
        return ret;
    }
    
    // parse ok, return the msg.
    *ppmsg = msg;
    
    return ret;
}

int SrsHttpParser::parse_message_imp(SrsStSocket* skt)
{
    int ret = ERROR_SUCCESS;
    
    while (true) {
        ssize_t nparsed = 0;
        
        // when got entire http header, parse it.
        // @see https://github.com/simple-rtmp-server/srs/issues/400
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
            if ((ret = buffer->grow(skt, buffer->size() + 1)) != ERROR_SUCCESS) {
                if (!srs_is_client_gracefully_close(ret)) {
                    srs_error("read body from server failed. ret=%d", ret);
                }
                return ret;
            }
        }
    }
    
    // parse last header.
    if (!field_name.empty() && !field_value.empty()) {
        headers.push_back(std::make_pair(field_name, field_value));
    }
    
    return ret;
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

SrsHttpUri::SrsHttpUri()
{
    port = SRS_DEFAULT_HTTP_PORT;
}

SrsHttpUri::~SrsHttpUri()
{
}

int SrsHttpUri::initialize(string _url)
{
    int ret = ERROR_SUCCESS;
    
    url = _url;
    const char* purl = url.c_str();
    
    http_parser_url hp_u;
    if((ret = http_parser_parse_url(purl, url.length(), 0, &hp_u)) != 0){
        int code = ret;
        ret = ERROR_HTTP_PARSE_URI;
        
        srs_error("parse url %s failed, code=%d, ret=%d", purl, code, ret);
        return ret;
    }
    
    std::string field = get_uri_field(url, &hp_u, UF_SCHEMA);
    if(!field.empty()){
        schema = field;
    }
    
    host = get_uri_field(url, &hp_u, UF_HOST);
    
    field = get_uri_field(url, &hp_u, UF_PORT);
    if(!field.empty()){
        port = atoi(field.c_str());
    }
    
    path = get_uri_field(url, &hp_u, UF_PATH);
    srs_info("parse url %s success", purl);
    
    query = get_uri_field(url, &hp_u, UF_QUERY);
    srs_info("parse query %s success", query.c_str());
    
    return ret;
}

const char* SrsHttpUri::get_url()
{
    return url.data();
}

const char* SrsHttpUri::get_schema()
{
    return schema.data();
}

const char* SrsHttpUri::get_host()
{
    return host.data();
}

int SrsHttpUri::get_port()
{
    return port;
}

const char* SrsHttpUri::get_path()
{
    return path.data();
}

const char* SrsHttpUri::get_query()
{
    return query.data();
}

string SrsHttpUri::get_uri_field(string uri, http_parser_url* hp_u, http_parser_url_fields field)
{
    if((hp_u->field_set & (1 << field)) == 0){
        return "";
    }
    
    srs_verbose("uri field matched, off=%d, len=%d, value=%.*s",
                hp_u->field_data[field].off,
                hp_u->field_data[field].len,
                hp_u->field_data[field].len,
                uri.c_str() + hp_u->field_data[field].off);
    
    int offset = hp_u->field_data[field].off;
    int len = hp_u->field_data[field].len;
    
    return uri.substr(offset, len);
}

#endif

#ifdef SRS_AUTO_HTTP_SERVER

SrsVodStream::SrsVodStream(string root_dir)
    : SrsHttpFileServer(root_dir)
{
}

SrsVodStream::~SrsVodStream()
{
}

int SrsVodStream::serve_flv_stream(ISrsHttpResponseWriter* w, ISrsHttpMessage* r, string fullpath, int offset)
{
    int ret = ERROR_SUCCESS;
    
    SrsFileReader fs;
    
    // open flv file
    if ((ret = fs.open(fullpath)) != ERROR_SUCCESS) {
        return ret;
    }
    
    if (offset > fs.filesize()) {
        ret = ERROR_HTTP_REMUX_OFFSET_OVERFLOW;
        srs_warn("http flv streaming %s overflow. size=%"PRId64", offset=%d, ret=%d", 
            fullpath.c_str(), fs.filesize(), offset, ret);
        return ret;
    }
    
    SrsFlvVodStreamDecoder ffd;
    
    // open fast decoder
    if ((ret = ffd.initialize(&fs)) != ERROR_SUCCESS) {
        return ret;
    }
    
    // save header, send later.
    char flv_header[13];
    
    // send flv header
    if ((ret = ffd.read_header_ext(flv_header)) != ERROR_SUCCESS) {
        return ret;
    }
    
    // save sequence header, send later
    char* sh_data = NULL;
    int sh_size = 0;
    
    if (true) {
        // send sequence header
        int64_t start = 0;
        if ((ret = ffd.read_sequence_header_summary(&start, &sh_size)) != ERROR_SUCCESS) {
            return ret;
        }
        if (sh_size <= 0) {
            ret = ERROR_HTTP_REMUX_SEQUENCE_HEADER;
            srs_warn("http flv streaming no sequence header. size=%d, ret=%d", sh_size, ret);
            return ret;
        }
    }
    sh_data = new char[sh_size];
    SrsAutoFree(char, sh_data);
    if ((ret = fs.read(sh_data, sh_size, NULL)) != ERROR_SUCCESS) {
        return ret;
    }

    // seek to data offset
    int64_t left = fs.filesize() - offset;

    // write http header for ts.
    w->header()->set_content_length((int)(sizeof(flv_header) + sh_size + left));
    w->header()->set_content_type("video/x-flv");
    
    // write flv header and sequence header.
    if ((ret = w->write(flv_header, sizeof(flv_header))) != ERROR_SUCCESS) {
        return ret;
    }
    if (sh_size > 0 && (ret = w->write(sh_data, sh_size)) != ERROR_SUCCESS) {
        return ret;
    }
    
    // write body.
    if ((ret = ffd.lseek(offset)) != ERROR_SUCCESS) {
        return ret;
    }
    
    // send data
    if ((ret = copy(w, &fs, r, (int)left)) != ERROR_SUCCESS) {
        srs_warn("read flv=%s size=%d failed, ret=%d", fullpath.c_str(), left, ret);
        return ret;
    }
    
    return ret;
}

int SrsVodStream::serve_mp4_stream(ISrsHttpResponseWriter* w, ISrsHttpMessage* r, string fullpath, int start, int end)
{
    int ret = ERROR_SUCCESS;

    srs_assert(start >= 0);
    srs_assert(end == -1 || end >= 0);
    
    SrsFileReader fs;
    
    // open flv file
    if ((ret = fs.open(fullpath)) != ERROR_SUCCESS) {
        return ret;
    }

    // parse -1 to whole file.
    if (end == -1) {
        end = (int)fs.filesize();
    }
    
    if (end > fs.filesize() || start > end) {
        ret = ERROR_HTTP_REMUX_OFFSET_OVERFLOW;
        srs_warn("http mp4 streaming %s overflow. size=%"PRId64", offset=%d, ret=%d", 
            fullpath.c_str(), fs.filesize(), start, ret);
        return ret;
    }

    // seek to data offset, [start, end] for range.
    int64_t left = end - start + 1;

    // write http header for ts.
    w->header()->set_content_length(left);
    w->header()->set_content_type("video/mp4");

    // status code 206 to make dash.as happy.
    w->write_header(SRS_CONSTS_HTTP_PartialContent);

    // response the content range header.
    std::stringstream content_range;
    content_range << "bytes " << start << "-" << end << "/" << fs.filesize();
    w->header()->set("Content-Range", content_range.str());
    
    // write body.
    fs.lseek(start);
    
    // send data
    if ((ret = copy(w, &fs, r, (int)left)) != ERROR_SUCCESS) {
        srs_warn("read mp4=%s size=%d failed, ret=%d", fullpath.c_str(), left, ret);
        return ret;
    }
    
    return ret;
}

SrsStreamCache::SrsStreamCache(SrsSource* s, SrsRequest* r)
{
    req = r->copy();
    source = s;
    queue = new SrsMessageQueue(true);
    pthread = new SrsEndlessThread("http-stream", this);
}

SrsStreamCache::~SrsStreamCache()
{
    srs_freep(pthread);
    
    srs_freep(queue);
    srs_freep(req);
}

int SrsStreamCache::start()
{
    return pthread->start();
}

int SrsStreamCache::dump_cache(SrsConsumer* consumer)
{
    int ret = ERROR_SUCCESS;

    double fast_cache = _srs_config->get_vhost_http_remux_fast_cache(req->vhost);

    if (fast_cache <= 0) {
        srs_info("http: ignore dump fast cache.");
        return ret;
    }
    
    // TODO: FIXME: config it.
    if ((ret = queue->dump_packets(consumer, false, 0, 0, SrsRtmpJitterAlgorithmOFF)) != ERROR_SUCCESS) {
        return ret;
    }
    
    srs_trace("http: dump cache %d msgs, duration=%dms, cache=%.2fs", 
        queue->size(), queue->duration(), fast_cache);
    
    return ret;
}

int SrsStreamCache::cycle()
{
    int ret = ERROR_SUCCESS;
    
    SrsConsumer* consumer = NULL;
    if ((ret = source->create_consumer(consumer, false, false, true)) != ERROR_SUCCESS) {
        srs_error("http: create consumer failed. ret=%d", ret);
        return ret;
    }
    SrsAutoFree(SrsConsumer, consumer);

    SrsPithyPrint* pprint = SrsPithyPrint::create_http_stream_cache();
    SrsAutoFree(SrsPithyPrint, pprint);
    
    SrsMessageArray msgs(SRS_PERF_MW_MSGS);

    // TODO: FIXME: support reload.
    double fast_cache = _srs_config->get_vhost_http_remux_fast_cache(req->vhost);
    if (fast_cache > 0) {
        queue->set_queue_size(fast_cache);
    }
    
    while (true) {
        pprint->elapse();

        // get messages from consumer.
        // each msg in msgs.msgs must be free, for the SrsMessageArray never free them.
        int count = 0;
        if ((ret = consumer->dump_packets(&msgs, count)) != ERROR_SUCCESS) {
            srs_error("http: get messages from consumer failed. ret=%d", ret);
            return ret;
        }
        
        if (count <= 0) {
            srs_info("http: mw sleep %dms for no msg", mw_sleep);
            // directly use sleep, donot use consumer wait.
            st_usleep(SRS_CONSTS_RTMP_PULSE_TIMEOUT_US);
            
            // ignore when nothing got.
            continue;
        }

        if (pprint->can_print()) {
            srs_trace("-> "SRS_CONSTS_LOG_HTTP_STREAM_CACHE" http: got %d msgs, age=%d, min=%d, mw=%d", 
                count, pprint->age(), SRS_PERF_MW_MIN_MSGS, SRS_CONSTS_RTMP_PULSE_TIMEOUT_US / 1000);
        }
    
        // free the messages.
        for (int i = 0; i < count; i++) {
            SrsSharedPtrMessage* msg = msgs.msgs[i];
            if (fast_cache > 0) {
                queue->enqueue(msg);
            } else {
                srs_freep(msg);
            }
        }
    }
    
    return ret;
}

ISrsStreamEncoder::ISrsStreamEncoder()
{
}

ISrsStreamEncoder::~ISrsStreamEncoder()
{
}

SrsTsStreamEncoder::SrsTsStreamEncoder()
{
    enc = new SrsTsEncoder();
}

SrsTsStreamEncoder::~SrsTsStreamEncoder()
{
    srs_freep(enc);
}

int SrsTsStreamEncoder::initialize(SrsFileWriter* w, SrsStreamCache* /*c*/)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = enc->initialize(w)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int SrsTsStreamEncoder::write_audio(int64_t timestamp, char* data, int size)
{
    return enc->write_audio(timestamp, data, size);
}

int SrsTsStreamEncoder::write_video(int64_t timestamp, char* data, int size)
{
    return enc->write_video(timestamp, data, size);
}

int SrsTsStreamEncoder::write_metadata(int64_t /*timestamp*/, char* /*data*/, int /*size*/)
{
    return ERROR_SUCCESS;
}

bool SrsTsStreamEncoder::has_cache()
{
    // for ts stream, use gop cache of SrsSource is ok.
    return false;
}

int SrsTsStreamEncoder::dump_cache(SrsConsumer* /*consumer*/)
{
    // for ts stream, ignore cache.
    return ERROR_SUCCESS;
}

SrsFlvStreamEncoder::SrsFlvStreamEncoder()
{
    enc = new SrsFlvEncoder();
}

SrsFlvStreamEncoder::~SrsFlvStreamEncoder()
{
    srs_freep(enc);
}

int SrsFlvStreamEncoder::initialize(SrsFileWriter* w, SrsStreamCache* /*c*/)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = enc->initialize(w)) != ERROR_SUCCESS) {
        return ret;
    }
    
    // write flv header.
    if ((ret = enc->write_header())  != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int SrsFlvStreamEncoder::write_audio(int64_t timestamp, char* data, int size)
{
    return enc->write_audio(timestamp, data, size);
}

int SrsFlvStreamEncoder::write_video(int64_t timestamp, char* data, int size)
{
    return enc->write_video(timestamp, data, size);
}

int SrsFlvStreamEncoder::write_metadata(int64_t timestamp, char* data, int size)
{
    return enc->write_metadata(SrsCodecFlvTagScript, data, size);
}

bool SrsFlvStreamEncoder::has_cache()
{
    // for flv stream, use gop cache of SrsSource is ok.
    return false;
}

int SrsFlvStreamEncoder::dump_cache(SrsConsumer* /*consumer*/)
{
    // for flv stream, ignore cache.
    return ERROR_SUCCESS;
}

SrsAacStreamEncoder::SrsAacStreamEncoder()
{
    enc = new SrsAacEncoder();
    cache = NULL;
}

SrsAacStreamEncoder::~SrsAacStreamEncoder()
{
    srs_freep(enc);
}

int SrsAacStreamEncoder::initialize(SrsFileWriter* w, SrsStreamCache* c)
{
    int ret = ERROR_SUCCESS;
    
    cache = c;
    
    if ((ret = enc->initialize(w)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int SrsAacStreamEncoder::write_audio(int64_t timestamp, char* data, int size)
{
    return enc->write_audio(timestamp, data, size);
}

int SrsAacStreamEncoder::write_video(int64_t /*timestamp*/, char* /*data*/, int /*size*/)
{
    // aac ignore any flv video.
    return ERROR_SUCCESS;
}

int SrsAacStreamEncoder::write_metadata(int64_t /*timestamp*/, char* /*data*/, int /*size*/)
{
    // aac ignore any flv metadata.
    return ERROR_SUCCESS;
}

bool SrsAacStreamEncoder::has_cache()
{
    return true;
}

int SrsAacStreamEncoder::dump_cache(SrsConsumer* consumer)
{
    srs_assert(cache);
    return cache->dump_cache(consumer);
}

SrsMp3StreamEncoder::SrsMp3StreamEncoder()
{
    enc = new SrsMp3Encoder();
    cache = NULL;
}

SrsMp3StreamEncoder::~SrsMp3StreamEncoder()
{
    srs_freep(enc);
}

int SrsMp3StreamEncoder::initialize(SrsFileWriter* w, SrsStreamCache* c)
{
    int ret = ERROR_SUCCESS;
    
    cache = c;
    
    if ((ret = enc->initialize(w)) != ERROR_SUCCESS) {
        return ret;
    }
    
    if ((ret = enc->write_header()) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int SrsMp3StreamEncoder::write_audio(int64_t timestamp, char* data, int size)
{
    return enc->write_audio(timestamp, data, size);
}

int SrsMp3StreamEncoder::write_video(int64_t /*timestamp*/, char* /*data*/, int /*size*/)
{
    // mp3 ignore any flv video.
    return ERROR_SUCCESS;
}

int SrsMp3StreamEncoder::write_metadata(int64_t /*timestamp*/, char* /*data*/, int /*size*/)
{
    // mp3 ignore any flv metadata.
    return ERROR_SUCCESS;
}

bool SrsMp3StreamEncoder::has_cache()
{
    return true;
}

int SrsMp3StreamEncoder::dump_cache(SrsConsumer* consumer)
{
    srs_assert(cache);
    return cache->dump_cache(consumer);
}

SrsStreamWriter::SrsStreamWriter(ISrsHttpResponseWriter* w)
{
    writer = w;
}

SrsStreamWriter::~SrsStreamWriter()
{
}

int SrsStreamWriter::open(std::string /*file*/)
{
    return ERROR_SUCCESS;
}

void SrsStreamWriter::close()
{
}

bool SrsStreamWriter::is_open()
{
    return true;
}

int64_t SrsStreamWriter::tellg()
{
    return 0;
}

int SrsStreamWriter::write(void* buf, size_t count, ssize_t* pnwrite)
{
    if (pnwrite) {
        *pnwrite = count;
    }
    return writer->write((char*)buf, (int)count);
}

SrsLiveStream::SrsLiveStream(SrsSource* s, SrsRequest* r, SrsStreamCache* c)
{
    source = s;
    cache = c;
    req = r->copy();
}

SrsLiveStream::~SrsLiveStream()
{
    srs_freep(req);
}

int SrsLiveStream::serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r)
{
    int ret = ERROR_SUCCESS;
    
    ISrsStreamEncoder* enc = NULL;
    
    srs_assert(entry);
    if (srs_string_ends_with(entry->pattern, ".flv")) {
        w->header()->set_content_type("video/x-flv");
        enc = new SrsFlvStreamEncoder();
    } else if (srs_string_ends_with(entry->pattern, ".aac")) {
        w->header()->set_content_type("audio/x-aac");
        enc = new SrsAacStreamEncoder();
    } else if (srs_string_ends_with(entry->pattern, ".mp3")) {
        w->header()->set_content_type("audio/mpeg");
        enc = new SrsMp3StreamEncoder();
    } else if (srs_string_ends_with(entry->pattern, ".ts")) {
        w->header()->set_content_type("video/MP2T");
        enc = new SrsTsStreamEncoder();
    } else {
        ret = ERROR_HTTP_LIVE_STREAM_EXT;
        srs_error("http: unsupported pattern %s", entry->pattern.c_str());
        return ret;
    }
    SrsAutoFree(ISrsStreamEncoder, enc);
    
    // create consumer of souce, ignore gop cache, use the audio gop cache.
    SrsConsumer* consumer = NULL;
    if ((ret = source->create_consumer(consumer, true, true, !enc->has_cache())) != ERROR_SUCCESS) {
        srs_error("http: create consumer failed. ret=%d", ret);
        return ret;
    }
    SrsAutoFree(SrsConsumer, consumer);
    srs_verbose("http: consumer created success.");

    SrsPithyPrint* pprint = SrsPithyPrint::create_http_stream();
    SrsAutoFree(SrsPithyPrint, pprint);
    
    SrsMessageArray msgs(SRS_PERF_MW_MSGS);
    
    // the memory writer.
    SrsStreamWriter writer(w);
    if ((ret = enc->initialize(&writer, cache)) != ERROR_SUCCESS) {
        srs_error("http: initialize stream encoder failed. ret=%d", ret);
        return ret;
    }
    
    // if gop cache enabled for encoder, dump to consumer.
    if (enc->has_cache()) {
        if ((ret = enc->dump_cache(consumer)) != ERROR_SUCCESS) {
            srs_error("http: dump cache to consumer failed. ret=%d", ret);
            return ret;
        }
    }
    
    while (true) {
        pprint->elapse();

        // get messages from consumer.
        // each msg in msgs.msgs must be free, for the SrsMessageArray never free them.
        int count = 0;
        if ((ret = consumer->dump_packets(&msgs, count)) != ERROR_SUCCESS) {
            srs_error("http: get messages from consumer failed. ret=%d", ret);
            return ret;
        }
        
        if (count <= 0) {
            srs_info("http: mw sleep %dms for no msg", mw_sleep);
            // directly use sleep, donot use consumer wait.
            st_usleep(SRS_CONSTS_RTMP_PULSE_TIMEOUT_US);
            
            // ignore when nothing got.
            continue;
        }

        if (pprint->can_print()) {
            srs_info("-> "SRS_CONSTS_LOG_HTTP_STREAM" http: got %d msgs, age=%d, min=%d, mw=%d", 
                count, pprint->age(), SRS_PERF_MW_MIN_MSGS, SRS_CONSTS_RTMP_PULSE_TIMEOUT_US / 1000);
        }
        
        // sendout all messages.
        ret = streaming_send_messages(enc, msgs.msgs, count);
    
        // free the messages.
        for (int i = 0; i < count; i++) {
            SrsSharedPtrMessage* msg = msgs.msgs[i];
            srs_freep(msg);
        }
        
        // check send error code.
        if (ret != ERROR_SUCCESS) {
            if (!srs_is_client_gracefully_close(ret)) {
                srs_error("http: send messages to client failed. ret=%d", ret);
            }
            return ret;
        }
    }
    
    return ret;
}

int SrsLiveStream::streaming_send_messages(ISrsStreamEncoder* enc, SrsSharedPtrMessage** msgs, int nb_msgs)
{
    int ret = ERROR_SUCCESS;
    
    for (int i = 0; i < nb_msgs; i++) {
        SrsSharedPtrMessage* msg = msgs[i];
        
        if (msg->is_audio()) {
            ret = enc->write_audio(msg->timestamp, msg->payload, msg->size);
        } else if (msg->is_video()) {
            ret = enc->write_video(msg->timestamp, msg->payload, msg->size);
        } else {
            ret = enc->write_metadata(msg->timestamp, msg->payload, msg->size);
        }
        
        if (ret != ERROR_SUCCESS) {
            return ret;
        }
    }
    
    return ret;
}

SrsLiveEntry::SrsLiveEntry(std::string m, bool h)
{
    mount = m;
    hstrs = h;
    
    stream = NULL;
    cache = NULL;
    
    std::string ext;
    size_t pos = string::npos;
    if ((pos = m.rfind(".")) != string::npos) {
        ext = m.substr(pos);
    }
    _is_flv = (ext == ".flv");
    _is_ts = (ext == ".ts");
    _is_mp3 = (ext == ".mp3");
    _is_aac = (ext == ".aac");
}

bool SrsLiveEntry::is_flv()
{
    return _is_flv;
}

bool SrsLiveEntry::is_ts()
{
    return _is_ts;
}

bool SrsLiveEntry::is_aac()
{
    return _is_aac;
}

bool SrsLiveEntry::is_mp3()
{
    return _is_mp3;
}

SrsHlsM3u8Stream::SrsHlsM3u8Stream()
{
}

SrsHlsM3u8Stream::~SrsHlsM3u8Stream()
{
}

void SrsHlsM3u8Stream::set_m3u8(std::string v)
{
    m3u8 = v;
}

int SrsHlsM3u8Stream::serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r)
{
    int ret = ERROR_SUCCESS;
    
    std::string data = m3u8;
    
    w->header()->set_content_length((int)data.length());
    w->header()->set_content_type("application/x-mpegURL;charset=utf-8");

    if ((ret = w->write((char*)data.data(), (int)data.length())) != ERROR_SUCCESS) {
        if (!srs_is_client_gracefully_close(ret)) {
            srs_error("send m3u8 failed. ret=%d", ret);
        }
        return ret;
    }

    return ret;
}

SrsHlsTsStream::SrsHlsTsStream()
{
}

SrsHlsTsStream::~SrsHlsTsStream()
{
}

void SrsHlsTsStream::set_ts(std::string v)
{
    ts = v;
}

int SrsHlsTsStream::serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r)
{
    int ret = ERROR_SUCCESS;
    
    std::string data = ts;
    
    w->header()->set_content_length((int)data.length());
    w->header()->set_content_type("video/MP2T");

    if ((ret = w->write((char*)data.data(), (int)data.length())) != ERROR_SUCCESS) {
        if (!srs_is_client_gracefully_close(ret)) {
            srs_error("send ts failed. ret=%d", ret);
        }
        return ret;
    }

    return ret;
}

SrsHlsEntry::SrsHlsEntry()
{
}

SrsHttpServer::SrsHttpServer(SrsServer* svr)
{
    server = svr;
    
    mux.hijack(this);
}

SrsHttpServer::~SrsHttpServer()
{
    mux.unhijack(this);
    
    if (true) {
        std::map<std::string, SrsLiveEntry*>::iterator it;
        for (it = tflvs.begin(); it != tflvs.end(); ++it) {
            SrsLiveEntry* entry = it->second;
            srs_freep(entry);
        }
        tflvs.clear();
    }
    if (true) {
        std::map<std::string, SrsLiveEntry*>::iterator it;
        for (it = sflvs.begin(); it != sflvs.end(); ++it) {
            SrsLiveEntry* entry = it->second;
            srs_freep(entry);
        }
        sflvs.clear();
    }
    if (true) {
        std::map<std::string, SrsHlsEntry*>::iterator it;
        for (it = thls.begin(); it != thls.end(); ++it) {
            SrsHlsEntry* entry = it->second;
            srs_freep(entry);
        }
        thls.clear();
    }
    if (true) {
        std::map<std::string, SrsHlsEntry*>::iterator it;
        for (it = shls.begin(); it != shls.end(); ++it) {
            SrsHlsEntry* entry = it->second;
            srs_freep(entry);
        }
        shls.clear();
    }
}

int SrsHttpServer::initialize()
{
    int ret = ERROR_SUCCESS;
    
    // static file
    // flv vod streaming.
    if ((ret = initialize_static_file()) != ERROR_SUCCESS) {
        return ret;
    }
    
    // remux rtmp to flv live streaming
    if ((ret = initialize_flv_streaming()) != ERROR_SUCCESS) {
        return ret;
    }
    
    // remux rtmp to hls live streaming
    if ((ret = initialize_hls_streaming()) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int SrsHttpServer::http_mount(SrsSource* s, SrsRequest* r)
{
    int ret = ERROR_SUCCESS;
    
    // the id to identify stream.
    std::string sid = r->get_stream_url();
    SrsLiveEntry* entry = NULL;
    
    // create stream from template when not found.
    if (sflvs.find(sid) == sflvs.end()) {
        if (tflvs.find(r->vhost) == tflvs.end()) {
            srs_info("ignore mount flv stream for disabled");
            return ret;
        }
    
        SrsLiveEntry* tmpl = tflvs[r->vhost];

        std::string mount = tmpl->mount;
    
        // replace the vhost variable
        mount = srs_string_replace(mount, "[vhost]", r->vhost);
        mount = srs_string_replace(mount, "[app]", r->app);
        mount = srs_string_replace(mount, "[stream]", r->stream);
    
        // remove the default vhost mount
        mount = srs_string_replace(mount, SRS_CONSTS_RTMP_DEFAULT_VHOST"/", "/");
        
        entry = new SrsLiveEntry(mount, tmpl->hstrs);
    
        entry->cache = new SrsStreamCache(s, r);
        entry->stream = new SrsLiveStream(s, r, entry->cache);
        
        sflvs[sid] = entry;
        
        // mount the http flv stream.
        // we must register the handler, then start the thread,
        // for the thread will cause thread switch context.
        // @see https://github.com/simple-rtmp-server/srs/issues/404
        if ((ret = mux.handle(mount, entry->stream)) != ERROR_SUCCESS) {
            srs_error("http: mount flv stream for vhost=%s failed. ret=%d", sid.c_str(), ret);
            return ret;
        }
        
        // start http stream cache thread
        if ((ret = entry->cache->start()) != ERROR_SUCCESS) {
            srs_error("http: start stream cache failed. ret=%d", ret);
            return ret;
        }
        srs_trace("http: mount flv stream for vhost=%s, mount=%s", sid.c_str(), mount.c_str());
    } else {
        entry = sflvs[sid];
    }
    
    // TODO: FIXME: supports reload.
    if (entry->stream) {
        entry->stream->entry->enabled = true;
        return ret;
    }
    
    return ret;
}

void SrsHttpServer::http_unmount(SrsSource* s, SrsRequest* r)
{
    std::string sid = r->get_stream_url();
    
    if (sflvs.find(sid) == sflvs.end()) {
        srs_info("ignore unmount flv stream for disabled");
        return;
    }

    SrsLiveEntry* entry = sflvs[sid];
    entry->stream->entry->enabled = false;
}

int SrsHttpServer::mount_hls(SrsRequest* r)
{
    int ret = ERROR_SUCCESS;
    
    std::string sid = r->get_stream_url();
    
    if (shls.find(sid) == shls.end()) {
        srs_info("ignore mount hls stream for disabled");
        return ret;
    }
    
    SrsHlsEntry* entry = shls[sid];
    
    // TODO: FIXME: supports reload.
    std::map<std::string, ISrsHttpHandler*>::iterator it;
    for (it = entry->streams.begin(); it != entry->streams.end(); ++it) {
        ISrsHttpHandler* stream = it->second;
        stream->entry->enabled = true;
    }

    return ret;
}

int SrsHttpServer::hls_update_m3u8(SrsRequest* r, string m3u8)
{
    int ret = ERROR_SUCCESS;

    std::string mount;
    
    std::string sid = r->get_stream_url();
    SrsHlsEntry* entry = NULL;
    
    // create stream from template when not found.
    if (shls.find(sid) == shls.end()) {
        if (thls.find(r->vhost) == thls.end()) {
            srs_info("ignore mount hls stream for disabled");
            return ret;
        }
    
        SrsHlsEntry* tmpl = thls[r->vhost];
        
        entry = new SrsHlsEntry();
        mount = tmpl->mount;
        
        // replace the vhost variable
        mount = srs_string_replace(mount, "[vhost]", r->vhost);
        mount = srs_string_replace(mount, "[app]", r->app);
        mount = srs_string_replace(mount, "[stream]", r->stream);
    
        // remove the default vhost mount
        mount = srs_string_replace(mount, SRS_CONSTS_RTMP_DEFAULT_VHOST"/", "/");
        
        entry->mount = mount;
        shls[sid] = entry;
    
        if (entry->streams.find(mount) == entry->streams.end()) {
            ISrsHttpHandler* he = new SrsHlsM3u8Stream();
            entry->streams[mount] = he;
    
            if ((ret = mux.handle(mount, he)) != ERROR_SUCCESS) {
                srs_error("handle mount=%s failed. ret=%d", mount.c_str(), ret);
                return ret;
            }
        }
    } else {
        entry = shls[sid];
    }

    mount = entry->mount;

    // update the m3u8 stream.
    SrsHlsM3u8Stream* hms = dynamic_cast<SrsHlsM3u8Stream*>(entry->streams[mount]);
    if (hms) {
        hms->set_m3u8(m3u8);
    }
    srs_trace("hls update m3u8 ok, mount=%s", mount.c_str());

    return ret;
}

int SrsHttpServer::hls_update_ts(SrsRequest* r, string uri, string ts)
{
    int ret = ERROR_SUCCESS;
    
    std::string sid = r->get_stream_url();
    
    // when no hls mounted, ignore.
    if (shls.find(sid) == shls.end()) {
        return ret;
    }

    SrsHlsEntry* entry = shls[sid];
    srs_assert(entry);

    std::string mount = entry->mount;
    
    // the ts is relative from the m3u8, the same start dir.
    size_t pos = string::npos;
    if ((pos = mount.rfind("/")) != string::npos) {
        mount = mount.substr(0, pos);
    }

    // replace the vhost variable
    mount = srs_string_replace(mount, "[vhost]", r->vhost);
    mount = srs_string_replace(mount, "[app]", r->app);

    // remove the default vhost mount
    mount = srs_string_replace(mount, SRS_CONSTS_RTMP_DEFAULT_VHOST"/", "/");

    // mount with ts.
    mount += "/";
    mount += uri;

    if (entry->streams.find(mount) == entry->streams.end()) {
        ISrsHttpHandler* he = new SrsHlsTsStream();
        entry->streams[mount] = he;

        if ((ret = mux.handle(mount, he)) != ERROR_SUCCESS) {
            srs_error("handle mount=%s failed. ret=%d", mount.c_str(), ret);
            return ret;
        }
    }

    // update the ts stream.
    SrsHlsTsStream* hts = dynamic_cast<SrsHlsTsStream*>(entry->streams[mount]);
    if (hts) {
        hts->set_ts(ts);
    }
    srs_trace("hls update ts ok, mount=%s", mount.c_str());

    return ret;
}

void SrsHttpServer::unmount_hls(SrsRequest* r)
{
    std::string sid = r->get_stream_url();
    
    if (shls.find(sid) == shls.end()) {
        srs_info("ignore unmount hls stream for disabled");
        return;
    }

    SrsHlsEntry* entry = shls[sid];

    std::map<std::string, ISrsHttpHandler*>::iterator it;
    for (it = entry->streams.begin(); it != entry->streams.end(); ++it) {
        ISrsHttpHandler* stream = it->second;
        stream->entry->enabled = false;
    }
}

int SrsHttpServer::on_reload_vhost_http_updated()
{
    int ret = ERROR_SUCCESS;
    // TODO: FIXME: implements it.
    return ret;
}

int SrsHttpServer::on_reload_vhost_http_remux_updated()
{
    int ret = ERROR_SUCCESS;
    // TODO: FIXME: implements it.
    return ret;
}

int SrsHttpServer::on_reload_vhost_hls(string vhost)
{
    int ret = ERROR_SUCCESS;
    // TODO: FIXME: implements it.
    return ret;
}

int SrsHttpServer::hijack(ISrsHttpMessage* request, ISrsHttpHandler** ph)
{
    int ret = ERROR_SUCCESS;
    
    // when handler not the root, we think the handler is ok.
    ISrsHttpHandler* h = *ph? *ph : NULL;
    if (h && h->entry && h->entry->pattern != "/") {
        return ret;
    }
    
    // only hijack for http streaming, http-flv/ts/mp3/aac.
    std::string ext = request->ext();
    if (ext.empty()) {
        return ret;
    }
    
    // find the actually request vhost.
    SrsConfDirective* vhost = _srs_config->get_vhost(request->host());
    if (!vhost || !_srs_config->get_vhost_enabled(vhost)) {
        return ret;
    }
    
    // find the entry template for the stream.
    SrsLiveEntry* entry = NULL;
    if (true) {
        // no http streaming on vhost, ignore.
        std::map<std::string, SrsLiveEntry*>::iterator it = tflvs.find(vhost->arg0());
        if (it == tflvs.end()) {
            return ret;
        }
        
        // hstrs not enabled, ignore.
        entry = it->second;
        if (!entry->hstrs) {
            return ret;
        }

        // check entry and request extension.
        if (entry->is_flv()) {
            if (ext != ".flv") {
                return ret;
            }
        } else if (entry->is_ts()) {
            if (ext != ".ts") {
                return ret;
            }
        } else if (entry->is_mp3()) {
            if (ext != ".mp3") {
                return ret;
            }
        } else if (entry->is_aac()) {
            if (ext != ".aac") {
                return ret;
            }
        } else {
            return ret;
        }
    }
    
    // convert to concreate class.
    SrsHttpMessage* hreq = dynamic_cast<SrsHttpMessage*>(request);
    srs_assert(hreq);
    
    // hijack for entry.
    SrsRequest* r = hreq->to_request(vhost->arg0());
    SrsAutoFree(SrsRequest, r);
    SrsSource* s = SrsSource::fetch(r);
    if (!s) {
        if ((ret = SrsSource::create(r, server, server, &s)) != ERROR_SUCCESS) {
            return ret;
        }
    }
    srs_assert(s != NULL);
    
    // create http streaming handler.
    if ((ret = http_mount(s, r)) != ERROR_SUCCESS) {
        return ret;
    }
    
    // use the handler if exists.
    if (ph) {
        std::string sid = r->get_stream_url();
        if (sflvs.find(sid) != sflvs.end()) {
            entry = sflvs[sid];
            *ph = entry->stream;
        }
    }
    
    // trigger edge to fetch from origin.
    bool vhost_is_edge = _srs_config->get_vhost_is_edge(r->vhost);
    srs_trace("hstrs: source url=%s, is_edge=%d, source_id=%d[%d]",
        r->get_stream_url().c_str(), vhost_is_edge, s->source_id(), s->source_id());
    
    // TODO: FIXME: disconnect when all connection closed.
    if (vhost_is_edge) {
        // notice edge to start for the first client.
        if ((ret = s->on_edge_start_play()) != ERROR_SUCCESS) {
            srs_error("notice edge start play stream failed. ret=%d", ret);
            return ret;
        }
    }
    
    return ret;
}

int SrsHttpServer::initialize_static_file()
{
    int ret = ERROR_SUCCESS;
    
    bool default_root_exists = false;
    
    // http static file and flv vod stream mount for each vhost.
    SrsConfDirective* root = _srs_config->get_root();
    for (int i = 0; i < (int)root->directives.size(); i++) {
        SrsConfDirective* conf = root->at(i);
        
        if (!conf->is_vhost()) {
            continue;
        }
        
        std::string vhost = conf->arg0();
        if (!_srs_config->get_vhost_http_enabled(vhost)) {
            continue;
        }
        
        std::string mount = _srs_config->get_vhost_http_mount(vhost);
        std::string dir = _srs_config->get_vhost_http_dir(vhost);

        // replace the vhost variable
        mount = srs_string_replace(mount, "[vhost]", vhost);

        // remove the default vhost mount
        mount = srs_string_replace(mount, SRS_CONSTS_RTMP_DEFAULT_VHOST"/", "/");
        
        // the dir mount must always ends with "/"
        if (mount != "/" && mount.rfind("/") != mount.length() - 1) {
            mount += "/";
        }
        
        // mount the http of vhost.
        if ((ret = mux.handle(mount, new SrsVodStream(dir))) != ERROR_SUCCESS) {
            srs_error("http: mount dir=%s for vhost=%s failed. ret=%d", dir.c_str(), vhost.c_str(), ret);
            return ret;
        }
        
        if (mount == "/") {
            default_root_exists = true;
            srs_warn("http: root mount to %s", dir.c_str());
        }
        srs_trace("http: vhost=%s mount to %s", vhost.c_str(), mount.c_str());
    }
    
    if (!default_root_exists) {
        // add root
        std::string dir = _srs_config->get_http_stream_dir();
        if ((ret = mux.handle("/", new SrsVodStream(dir))) != ERROR_SUCCESS) {
            srs_error("http: mount root dir=%s failed. ret=%d", dir.c_str(), ret);
            return ret;
        }
        srs_trace("http: root mount to %s", dir.c_str());
    }
    
    return ret;
}

int SrsHttpServer::initialize_flv_streaming()
{
    int ret = ERROR_SUCCESS;
    
    // http flv live stream mount for each vhost.
    SrsConfDirective* root = _srs_config->get_root();
    for (int i = 0; i < (int)root->directives.size(); i++) {
        SrsConfDirective* conf = root->at(i);
        
        if (!conf->is_vhost()) {
            continue;
        }
        
        std::string vhost = conf->arg0();
        if (!_srs_config->get_vhost_http_remux_enabled(vhost)) {
            continue;
        }
        
        SrsLiveEntry* entry = new SrsLiveEntry(
            _srs_config->get_vhost_http_remux_mount(vhost),
            _srs_config->get_vhost_http_remux_hstrs(vhost)
        );
        tflvs[vhost] = entry;
        srs_trace("http flv live stream, vhost=%s, mount=%s", 
            vhost.c_str(), entry->mount.c_str());
    }
    
    return ret;
}

int SrsHttpServer::initialize_hls_streaming()
{
    int ret = ERROR_SUCCESS;
    
    // http hls live stream mount for each vhost.
    SrsConfDirective* root = _srs_config->get_root();
    for (int i = 0; i < (int)root->directives.size(); i++) {
        SrsConfDirective* conf = root->at(i);
        
        if (!conf->is_vhost()) {
            continue;
        }
        
        std::string vhost = conf->arg0();
        if (!_srs_config->get_hls_enabled(vhost)) {
            continue;
        }

        std::string storage = _srs_config->get_hls_storage(vhost);
        if (storage != "ram" && storage != "both") {
            continue;
        }
        
        SrsHlsEntry* entry = new SrsHlsEntry();
        entry->mount = _srs_config->get_hls_mount(vhost);
        thls[vhost] = entry;
        srs_trace("http hls live stream, vhost=%s, mount=%s", 
            vhost.c_str(), entry->mount.c_str());
    }
    
    return ret;
}

SrsHttpConn::SrsHttpConn(IConnectionManager* cm, st_netfd_t fd, SrsHttpServeMux* m)
    : SrsConnection(cm, fd)
{
    parser = new SrsHttpParser();
    http_mux = m;
}

SrsHttpConn::~SrsHttpConn()
{
    srs_freep(parser);
}

void SrsHttpConn::resample()
{
    // TODO: FIXME: implements it
}

int64_t SrsHttpConn::get_send_bytes_delta()
{
    // TODO: FIXME: implements it
    return 0;
}

int64_t SrsHttpConn::get_recv_bytes_delta()
{
    // TODO: FIXME: implements it
    return 0;
}

void SrsHttpConn::cleanup()
{
    // TODO: FIXME: implements it
}

int SrsHttpConn::do_cycle()
{
    int ret = ERROR_SUCCESS;
    
    srs_trace("HTTP client ip=%s", ip.c_str());
    
    // initialize parser
    if ((ret = parser->initialize(HTTP_REQUEST)) != ERROR_SUCCESS) {
        srs_error("http initialize http parser failed. ret=%d", ret);
        return ret;
    }
    
    // underlayer socket
    SrsStSocket skt(stfd);
    
    // set the recv timeout, for some clients never disconnect the connection.
    // @see https://github.com/simple-rtmp-server/srs/issues/398
    skt.set_recv_timeout(SRS_HTTP_RECV_TIMEOUT_US);
    
    // process http messages.
    for (;;) {
        ISrsHttpMessage* req = NULL;
        
        // get a http message
        if ((ret = parser->parse_message(&skt, this, &req)) != ERROR_SUCCESS) {
            return ret;
        }

        // if SUCCESS, always NOT-NULL.
        srs_assert(req);
        
        // always free it in this scope.
        SrsAutoFree(ISrsHttpMessage, req);
        
        // may should discard the body.
        if ((ret = on_got_http_message(req)) != ERROR_SUCCESS) {
            return ret;
        }
        
        // ok, handle http request.
        SrsHttpResponseWriter writer(&skt);
        if ((ret = process_request(&writer, req)) != ERROR_SUCCESS) {
            return ret;
        }
        
        // donot keep alive, disconnect it.
        // @see https://github.com/simple-rtmp-server/srs/issues/399
        if (!req->is_keep_alive()) {
            break;
        }
    }
        
    return ret;
}

int SrsHttpConn::process_request(ISrsHttpResponseWriter* w, ISrsHttpMessage* r) 
{
    int ret = ERROR_SUCCESS;
    
    srs_trace("HTTP %s %s, content-length=%"PRId64"", 
        r->method_str().c_str(), r->url().c_str(), r->content_length());
    
    // use default server mux to serve http request.
    if ((ret = http_mux->serve_http(w, r)) != ERROR_SUCCESS) {
        if (!srs_is_client_gracefully_close(ret)) {
            srs_error("serve http msg failed. ret=%d", ret);
        }
        return ret;
    }
    
    return ret;
}

SrsStaticHttpConn::SrsStaticHttpConn(IConnectionManager* cm, st_netfd_t fd, SrsHttpServeMux* m)
    : SrsHttpConn(cm, fd, m)
{
}

SrsStaticHttpConn::~SrsStaticHttpConn()
{
}

int SrsStaticHttpConn::on_got_http_message(ISrsHttpMessage* msg)
{
    int ret = ERROR_SUCCESS;
    
    // TODO: FIXME: use the post body.
    std::string res;
    
    // get response body.
    if ((ret = msg->body_read_all(res)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

#endif

