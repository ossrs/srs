/*
The MIT License (MIT)

Copyright (c) 2013-2017 SRS(ossrs)

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

#include <srs_app_caster_flv.hpp>

#ifdef SRS_AUTO_STREAM_CASTER

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
}

SrsAppCasterFlv::~SrsAppCasterFlv()
{
}

int SrsAppCasterFlv::initialize()
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = http_mux->handle("/", this)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int SrsAppCasterFlv::on_tcp_client(st_netfd_t stfd)
{
    int ret = ERROR_SUCCESS;
    
    string ip = srs_get_peer_ip(st_netfd_fileno(stfd));
    SrsHttpConn* conn = new SrsDynamicHttpConn(this, stfd, http_mux, ip);
    conns.push_back(conn);
    
    if ((ret = conn->start()) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

void SrsAppCasterFlv::remove(SrsConnection* c)
{
    std::vector<SrsHttpConn*>::iterator it;
    if ((it = std::find(conns.begin(), conns.end(), c)) != conns.end()) {
        conns.erase(it);
    }
}

int SrsAppCasterFlv::serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r)
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
    
    return conn->proxy(w, r, o);
}

SrsDynamicHttpConn::SrsDynamicHttpConn(IConnectionManager* cm, st_netfd_t fd, SrsHttpServeMux* m, string cip)
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

int SrsDynamicHttpConn::on_got_http_message(ISrsHttpMessage* msg)
{
    int ret = ERROR_SUCCESS;
    return ret;
}

int SrsDynamicHttpConn::proxy(ISrsHttpResponseWriter* w, ISrsHttpMessage* r, std::string o)
{
    int ret = ERROR_SUCCESS;
    
    output = o;
    srs_trace("flv: proxy %s to %s", r->uri().c_str(), output.c_str());
    
    char* buffer = new char[SRS_HTTP_FLV_STREAM_BUFFER];
    SrsAutoFreeA(char, buffer);
    
    ISrsHttpResponseReader* rr = r->body_reader();
    SrsHttpFileReader reader(rr);
    SrsFlvDecoder dec;
    
    if ((ret = dec.initialize(&reader)) != ERROR_SUCCESS) {
        return ret;
    }
    
    char header[9];
    if ((ret = dec.read_header(header)) != ERROR_SUCCESS) {
        if (!srs_is_client_gracefully_close(ret)) {
            srs_error("flv: proxy flv header failed. ret=%d", ret);
        }
        return ret;
    }
    srs_trace("flv: proxy drop flv header.");
    
    char pps[4];
    if ((ret = dec.read_previous_tag_size(pps)) != ERROR_SUCCESS) {
        if (!srs_is_client_gracefully_close(ret)) {
            srs_error("flv: proxy flv header pps failed. ret=%d", ret);
        }
        return ret;
    }
    
    ret = do_proxy(rr, &dec);
    sdk->close();
    
    return ret;
}

int SrsDynamicHttpConn::do_proxy(ISrsHttpResponseReader* rr, SrsFlvDecoder* dec)
{
    int ret = ERROR_SUCCESS;
    
    srs_freep(sdk);
    
    int64_t cto = SRS_CONSTS_RTMP_TMMS;
    int64_t sto = SRS_CONSTS_RTMP_PULSE_TMMS;
    sdk = new SrsSimpleRtmpClient(output, cto, sto);
    
    if ((ret = sdk->connect()) != ERROR_SUCCESS) {
        srs_error("flv: connect %s failed, cto=%"PRId64", sto=%"PRId64". ret=%d", output.c_str(), cto, sto, ret);
        return ret;
    }
    
    if ((ret = sdk->publish()) != ERROR_SUCCESS) {
        srs_error("flv: publish failed. ret=%d", ret);
        return ret;
    }
    
    char pps[4];
    while (!rr->eof()) {
        pprint->elapse();
        
        char type;
        int32_t size;
        uint32_t time;
        if ((ret = dec->read_tag_header(&type, &size, &time)) != ERROR_SUCCESS) {
            if (!srs_is_client_gracefully_close(ret)) {
                srs_error("flv: proxy tag header failed. ret=%d", ret);
            }
            return ret;
        }
        
        char* data = new char[size];
        if ((ret = dec->read_tag_data(data, size)) != ERROR_SUCCESS) {
            srs_freepa(data);
            if (!srs_is_client_gracefully_close(ret)) {
                srs_error("flv: proxy tag data failed. ret=%d", ret);
            }
            return ret;
        }
        
        SrsSharedPtrMessage* msg = NULL;
        if ((ret = srs_rtmp_create_msg(type, time, data, size, sdk->sid(), &msg)) != ERROR_SUCCESS) {
            return ret;
        }
        
        // TODO: FIXME: for post flv, reconnect when error.
        if ((ret = sdk->send_and_free_message(msg)) != ERROR_SUCCESS) {
            if (!srs_is_client_gracefully_close(ret)) {
                srs_error("flv: proxy rtmp packet failed. ret=%d", ret);
            }
            return ret;
        }
        
        if (pprint->can_print()) {
            srs_trace("flv: send msg %d age=%d, dts=%d, size=%d", type, pprint->age(), time, size);
        }
        
        if ((ret = dec->read_previous_tag_size(pps)) != ERROR_SUCCESS) {
            if (!srs_is_client_gracefully_close(ret)) {
                srs_error("flv: proxy tag header pps failed. ret=%d", ret);
            }
            return ret;
        }
    }
    
    return ret;
}

SrsHttpFileReader::SrsHttpFileReader(ISrsHttpResponseReader* h)
{
    http = h;
}

SrsHttpFileReader::~SrsHttpFileReader()
{
}

int SrsHttpFileReader::open(std::string /*file*/)
{
    return ERROR_SUCCESS;
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

int SrsHttpFileReader::read(void* buf, size_t count, ssize_t* pnread)
{
    int ret = ERROR_SUCCESS;
    
    if (http->eof()) {
        ret = ERROR_HTTP_REQUEST_EOF;
        srs_error("flv: encoder EOF. ret=%d", ret);
        return ret;
    }
    
    int total_read = 0;
    while (total_read < (int)count) {
        int nread = 0;
        if ((ret = http->read((char*)buf + total_read, (int)(count - total_read), &nread)) != ERROR_SUCCESS) {
            return ret;
        }
        
        if (nread == 0) {
            ret = ERROR_HTTP_REQUEST_EOF;
            srs_warn("flv: encoder read EOF. ret=%d", ret);
            break;
        }
        
        srs_assert(nread);
        total_read += nread;
    }
    
    if (pnread) {
        *pnread = total_read;
    }
    
    return ret;
}

int SrsHttpFileReader::lseek(off_t offset, int whence, off_t* seeked)
{
    // TODO: FIXME: Use HTTP range for seek.
    return ERROR_SUCCESS;
}

#endif
