/*
The MIT License (MIT)

Copyright (c) 2013-2015 SRS(ossrs)

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

#if defined(SRS_AUTO_HTTP_CORE)

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
#include <srs_app_st.hpp>
#include <srs_core_autofree.hpp>
#include <srs_app_config.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_kernel_file.hpp>
#include <srs_kernel_flv.hpp>
#include <srs_rtmp_stack.hpp>
#include <srs_app_source.hpp>
#include <srs_rtmp_msg_array.hpp>
#include <srs_kernel_aac.hpp>
#include <srs_kernel_mp3.hpp>
#include <srs_kernel_ts.hpp>
#include <srs_app_pithy_print.hpp>
#include <srs_app_source.hpp>
#include <srs_app_server.hpp>
#include <srs_app_http_static.hpp>
#include <srs_app_http_stream.hpp>
#include <srs_app_http_api.hpp>
#include <srs_app_utility.hpp>

#endif

#ifdef SRS_AUTO_HTTP_CORE

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

int SrsHttpResponseWriter::final_request()
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

int SrsHttpResponseWriter::write(char* data, int size)
{
    int ret = ERROR_SUCCESS;
    
    // write the header data in memory.
    if (!header_wrote) {
        write_header(SRS_CONSTS_HTTP_OK);
    }
    
    // whatever header is wrote, we should try to send header.
    if ((ret = send_header(data, size)) != ERROR_SUCCESS) {
        srs_error("http: send header failed. ret=%d", ret);
        return ret;
    }
    
    // check the bytes send and content length.
    written += size;
    if (content_length != -1 && written > content_length) {
        ret = ERROR_HTTP_CONTENT_LENGTH;
        srs_error("http: exceed content length. ret=%d", ret);
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
    if ((ret = skt->writev(iovs, 4, &nwrite)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int SrsHttpResponseWriter::writev(iovec* iov, int iovcnt, ssize_t* pnwrite)
{
    int ret = ERROR_SUCCESS;
    
    // when header not ready, or not chunked, send one by one.
    if (!header_wrote || content_length != -1) {
        ssize_t nwrite = 0;
        for (int i = 0; i < iovcnt; i++) {
            iovec* piovc = iov + i;
            nwrite += piovc->iov_len;
            if ((ret = write((char*)piovc->iov_base, (int)piovc->iov_len)) != ERROR_SUCCESS) {
                return ret;
            }
        }
        
        if (pnwrite) {
            *pnwrite = nwrite;
        }
        
        return ret;
    }
    
    // ignore NULL content.
    if (iovcnt <= 0) {
        return ret;
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
        iovec* data_iov = iov + i;
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
        iovec* data_iov = iov + i;
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
    if ((ret = srs_write_large_iovs(skt, iovss, nb_iovss, &nwrite)) != ERROR_SUCCESS) {
        return ret;
    }
    
    if (pnwrite) {
        *pnwrite = nwrite;
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
    if (owner->content_length() != -1) {
        int max = (int)owner->content_length() - (int)nb_total_read;
        if (max <= 0) {
            is_eof = true;
            return ret;
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
    
    return ret;
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
    
    // for not chunked and specified content length.
    if (!owner->is_chunked() && owner->content_length() != -1) {
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
    
    // use server public ip when no host specified.
    // to make telnet happy.
    if (host.empty()) {
        host= srs_get_public_internet_address();
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
    
    // parse jsonp request message.
    if (!query_get("callback").empty()) {
        jsonp = true;
    }
    if (jsonp) {
        jsonp_method = query_get("method");
    }
    
    return ret;
}

SrsConnection* SrsHttpMessage::connection()
{
    return conn;
}

u_int8_t SrsHttpMessage::method()
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
    
    return (u_int8_t)_header.method;
}

u_int16_t SrsHttpMessage::status_code()
{
    return (u_int16_t)_header.status_code;
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

int SrsHttpMessage::enter_infinite_chunked()
{
    int ret = ERROR_SUCCESS;
    
    if (infinite_chunked) {
        return ret;
    }
    
    if (is_chunked() || content_length() != -1) {
        ret = ERROR_HTTP_DATA_INVALID;
        srs_error("infinite chunkted not supported in specified codec. ret=%d", ret);
        return ret;
    }
    
    infinite_chunked = true;
    
    return ret;
}

int SrsHttpMessage::body_read_all(string& body)
{
    int ret = ERROR_SUCCESS;
    
    // cache to read.
    char* buf = new char[SRS_HTTP_READ_CACHE_BYTES];
    SrsAutoFreeA(char, buf);
    
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
    
    srs_discovery_tc_url(req->tcUrl, req->schema, req->host, req->vhost, req->app, req->stream, req->port, req->param);
    req->as_http();
    
    return req;
}

bool SrsHttpMessage::is_jsonp()
{
    return jsonp;
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

SrsHttpConn::SrsHttpConn(IConnectionManager* cm, st_netfd_t fd, ISrsHttpServeMux* m)
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
    // @see https://github.com/ossrs/srs/issues/398
    skt.set_recv_timeout(SRS_HTTP_RECV_TIMEOUT_US);
    
    // process http messages.
    while (!disposed) {
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
        // @see https://github.com/ossrs/srs/issues/399
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

SrsResponseOnlyHttpConn::SrsResponseOnlyHttpConn(IConnectionManager* cm, st_netfd_t fd, ISrsHttpServeMux* m)
    : SrsHttpConn(cm, fd, m)
{
}

SrsResponseOnlyHttpConn::~SrsResponseOnlyHttpConn()
{
}

int SrsResponseOnlyHttpConn::pop_message(ISrsHttpMessage** preq)
{
    int ret = ERROR_SUCCESS;
    
    SrsStSocket skt(stfd);
    
    if ((ret = parser->parse_message(&skt, this, preq)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int SrsResponseOnlyHttpConn::on_got_http_message(ISrsHttpMessage* msg)
{
    int ret = ERROR_SUCCESS;
    
    ISrsHttpResponseReader* br = msg->body_reader();
    
    // drop all request body.
    while (!br->eof()) {
        char body[4096];
        if ((ret = br->read(body, 4096, NULL)) != ERROR_SUCCESS) {
            return ret;
        }
    }
    
    return ret;
}

SrsHttpServer::SrsHttpServer(SrsServer* svr)
{
    server = svr;
    http_stream = new SrsHttpStreamServer(svr);
    http_static = new SrsHttpStaticServer(svr);
}

SrsHttpServer::~SrsHttpServer()
{
    srs_freep(http_stream);
    srs_freep(http_static);
}

int SrsHttpServer::initialize()
{
    int ret = ERROR_SUCCESS;
    
#if defined(SRS_AUTO_HTTP_SERVER) && defined(SRS_AUTO_HTTP_API)
    // for SRS go-sharp to detect the status of HTTP server of SRS HTTP FLV Cluster.
    if ((ret = http_static->mux.handle("/api/v1/versions", new SrsGoApiVersion())) != ERROR_SUCCESS) {
        return ret;
    }
#endif
    
    if ((ret = http_stream->initialize()) != ERROR_SUCCESS) {
        return ret;
    }
    
    if ((ret = http_static->initialize()) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int SrsHttpServer::serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r)
{
    // try http stream first.
    if (http_stream->mux.can_serve(r)) {
        return http_stream->mux.serve_http(w, r);
    }
    
    return http_static->mux.serve_http(w, r);
}

int SrsHttpServer::http_mount(SrsSource* s, SrsRequest* r)
{
    return http_stream->http_mount(s, r);
}

void SrsHttpServer::http_unmount(SrsSource* s, SrsRequest* r)
{
    http_stream->http_unmount(s, r);
}

#endif

