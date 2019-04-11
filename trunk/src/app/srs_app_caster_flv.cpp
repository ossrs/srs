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

#include <srs_app_caster_flv.hpp>

#include <algorithm>
using namespace std;

#include <srs_app_config.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_app_config.hpp>
#include <srs_app_pithy_print.hpp>
#include <srs_app_http_conn.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_flv.hpp>
#include <srs_rtmp_stack.hpp>
#include <srs_protocol_utility.hpp>
#include <srs_app_st.hpp>
#include <srs_app_utility.hpp>
#include <srs_protocol_amf0.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_app_rtmp_conn.hpp>
#include <srs_protocol_utility.hpp>

#define SRS_HTTP_FLV_STREAM_BUFFER 4096

SrsAppCasterFlv::SrsAppCasterFlv(SrsConfDirective* c)
{
    http_mux = new SrsHttpServeMux();
    output = _srs_config->get_stream_caster_output(c);
    manager = new SrsCoroutineManager();
}

SrsAppCasterFlv::~SrsAppCasterFlv()
{
    srs_freep(http_mux);
    srs_freep(manager);
}

srs_error_t SrsAppCasterFlv::initialize()
{
    srs_error_t err = srs_success;
    
    if ((err = http_mux->handle("/", this)) != srs_success) {
        return srs_error_wrap(err, "handle root");
    }
    
    if ((err = manager->start()) != srs_success) {
        return srs_error_wrap(err, "start manager");
    }
    
    return err;
}

srs_error_t SrsAppCasterFlv::on_tcp_client(srs_netfd_t stfd)
{
    srs_error_t err = srs_success;
    
    string ip = srs_get_peer_ip(srs_netfd_fileno(stfd));
    SrsHttpConn* conn = new SrsDynamicHttpConn(this, stfd, http_mux, ip);
    conns.push_back(conn);
    
    if ((err = conn->start()) != srs_success) {
        return srs_error_wrap(err, "start tcp listener");
    }
    
    return err;
}

void SrsAppCasterFlv::remove(ISrsConnection* c)
{
    SrsConnection* conn = dynamic_cast<SrsConnection*>(c);
    
    std::vector<SrsHttpConn*>::iterator it;
    if ((it = std::find(conns.begin(), conns.end(), conn)) != conns.end()) {
        conns.erase(it);
    }
    
    // fixbug: SrsHttpConn for CasterFlv is not freed, which could cause memory leak
    // so, free conn which is not managed by SrsServer->conns;
    // @see: https://github.com/ossrs/srs/issues/826
    manager->remove(c);
}

srs_error_t SrsAppCasterFlv::serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r)
{
    SrsHttpMessage* msg = dynamic_cast<SrsHttpMessage*>(r);
    SrsDynamicHttpConn* conn = dynamic_cast<SrsDynamicHttpConn*>(msg->connection());
    srs_assert(conn);
    
    std::string app = srs_path_dirname(r->path());
    app = srs_string_trim_start(app, "/");
    
    std::string stream = srs_path_basename(r->path());
    stream = srs_string_trim_start(stream, "/");
    
    std::string o = output;
    if (!app.empty() && app != "/") {
        o = srs_string_replace(o, "[app]", app);
    }
    o = srs_string_replace(o, "[stream]", stream);
    
    // remove the extension.
    if (srs_string_ends_with(o, ".flv")) {
        o = o.substr(0, o.length() - 4);
    }
    
    srs_error_t err = conn->proxy(w, r, o);
    if (err != srs_success) {
        return srs_error_wrap(err, "proxy");
    }
    
    return err;
}

SrsDynamicHttpConn::SrsDynamicHttpConn(IConnectionManager* cm, srs_netfd_t fd, SrsHttpServeMux* m, string cip)
: SrsHttpConn(cm, fd, m, cip)
{
    sdk = NULL;
    pprint = SrsPithyPrint::create_caster();
}

SrsDynamicHttpConn::~SrsDynamicHttpConn()
{
    srs_freep(sdk);
    srs_freep(pprint);
}

srs_error_t SrsDynamicHttpConn::on_got_http_message(ISrsHttpMessage* msg)
{
    return srs_success;
}

srs_error_t SrsDynamicHttpConn::proxy(ISrsHttpResponseWriter* w, ISrsHttpMessage* r, std::string o)
{
    srs_error_t err = srs_success;
    
    output = o;
    srs_trace("flv: proxy %s to %s", r->uri().c_str(), output.c_str());
    
    char* buffer = new char[SRS_HTTP_FLV_STREAM_BUFFER];
    SrsAutoFreeA(char, buffer);
    
    ISrsHttpResponseReader* rr = r->body_reader();
    SrsHttpFileReader reader(rr);
    SrsFlvDecoder dec;
    
    if ((err = dec.initialize(&reader)) != srs_success) {
        return srs_error_wrap(err, "init decoder");
    }
    
    char header[9];
    if ((err = dec.read_header(header)) != srs_success) {
        return srs_error_wrap(err, "read header");
    }
    
    char pps[4];
    if ((err = dec.read_previous_tag_size(pps)) != srs_success) {
        return srs_error_wrap(err, "read pts");
    }
    
    err = do_proxy(rr, &dec);
    sdk->close();
    
    return err;
}

srs_error_t SrsDynamicHttpConn::do_proxy(ISrsHttpResponseReader* rr, SrsFlvDecoder* dec)
{
    srs_error_t err = srs_success;
    
    srs_freep(sdk);
    
    int64_t cto = srsu2ms(SRS_CONSTS_RTMP_TIMEOUT);
    int64_t sto = srsu2ms(SRS_CONSTS_RTMP_PULSE);
    sdk = new SrsSimpleRtmpClient(output, cto, sto);
    
    if ((err = sdk->connect()) != srs_success) {
        return srs_error_wrap(err, "connect %s failed, cto=%" PRId64 ", sto=%" PRId64, output.c_str(), cto, sto);
    }
    
    if ((err = sdk->publish(SRS_CONSTS_RTMP_PROTOCOL_CHUNK_SIZE)) != srs_success) {
        return srs_error_wrap(err, "publish");
    }
    
    char pps[4];
    while (!rr->eof()) {
        pprint->elapse();
        
        char type;
        int32_t size;
        uint32_t time;
        if ((err = dec->read_tag_header(&type, &size, &time)) != srs_success) {
            return srs_error_wrap(err, "read tag header");
        }
        
        char* data = new char[size];
        if ((err = dec->read_tag_data(data, size)) != srs_success) {
            srs_freepa(data);
            return srs_error_wrap(err, "read tag data");
        }
        
        SrsSharedPtrMessage* msg = NULL;
        if ((err = srs_rtmp_create_msg(type, time, data, size, sdk->sid(), &msg)) != srs_success) {
            return srs_error_wrap(err, "create message");
        }
        
        // TODO: FIXME: for post flv, reconnect when error.
        if ((err = sdk->send_and_free_message(msg)) != srs_success) {
            return srs_error_wrap(err, "send message");
        }
        
        if (pprint->can_print()) {
            srs_trace("flv: send msg %d age=%d, dts=%d, size=%d", type, pprint->age(), time, size);
        }
        
        if ((err = dec->read_previous_tag_size(pps)) != srs_success) {
            return srs_error_wrap(err, "read pts");
        }
    }
    
    return err;
}

SrsHttpFileReader::SrsHttpFileReader(ISrsHttpResponseReader* h)
{
    http = h;
}

SrsHttpFileReader::~SrsHttpFileReader()
{
}

srs_error_t SrsHttpFileReader::open(std::string /*file*/)
{
    return srs_success;
}

void SrsHttpFileReader::close()
{
}

bool SrsHttpFileReader::is_open()
{
    return true;
}

int64_t SrsHttpFileReader::tellg()
{
    return 0;
}

void SrsHttpFileReader::skip(int64_t /*size*/)
{
}

int64_t SrsHttpFileReader::seek2(int64_t offset)
{
    return offset;
}

int64_t SrsHttpFileReader::filesize()
{
    return 0;
}

srs_error_t SrsHttpFileReader::read(void* buf, size_t count, ssize_t* pnread)
{
    srs_error_t err = srs_success;
    
    if (http->eof()) {
        return srs_error_new(ERROR_HTTP_REQUEST_EOF, "EOF");
    }
    
    int total_read = 0;
    while (total_read < (int)count) {
        int nread = 0;
        if ((err = http->read((char*)buf + total_read, (int)(count - total_read), &nread)) != srs_success) {
            return srs_error_wrap(err, "read");
        }
        
        if (nread == 0) {
            err = srs_error_new(ERROR_HTTP_REQUEST_EOF, "EOF");
            break;
        }
        
        srs_assert(nread);
        total_read += nread;
    }
    
    if (pnread) {
        *pnread = total_read;
    }
    
    return err;
}

srs_error_t SrsHttpFileReader::lseek(off_t offset, int whence, off_t* seeked)
{
    // TODO: FIXME: Use HTTP range for seek.
    return srs_success;
}

