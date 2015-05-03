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

#include <srs_app_caster_flv.hpp>

#ifdef SRS_AUTO_STREAM_CASTER

#include <algorithm>
using namespace std;

#include <srs_app_config.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_app_config.hpp>
#include <srs_app_pithy_print.hpp>
#include <srs_app_http.hpp>
#include <srs_app_http_conn.hpp>
#include <srs_core_autofree.hpp>

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
    
    SrsHttpConn* conn = new SrsHttpConn(this, stfd, http_mux);
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
#define SRS_HTTP_FLV_STREAM_BUFFER 4096
int SrsAppCasterFlv::serve_http(ISrsHttpResponseWriter* w, SrsHttpMessage* r)
{
    int ret = ERROR_SUCCESS;
    
    srs_info("flv: handle request at %s", r->path().c_str());
    
    char* buffer = new char[SRS_HTTP_FLV_STREAM_BUFFER];
    SrsAutoFree(char, buffer);
    
    ISrsHttpResponseReader* rr = r->body_reader();
    while (!rr->eof()) {
        int nb_read = 0;
        if ((ret = rr->read(buffer, SRS_HTTP_FLV_STREAM_BUFFER, &nb_read)) != ERROR_SUCCESS) {
            return ret;
        }
        srs_trace("flv: read %dB from %s", nb_read, r->path().c_str());
    }
    
    return ret;
}

#endif
