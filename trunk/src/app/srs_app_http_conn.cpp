/*
The MIT License (MIT)

Copyright (c) 2013-2015 winlin

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

#ifdef SRS_AUTO_HTTP_SERVER

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>

#include <sstream>
using namespace std;

#include <srs_kernel_log.hpp>
#include <srs_kernel_error.hpp>
#include <srs_app_st_socket.hpp>
#include <srs_core_autofree.hpp>
#include <srs_app_config.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_kernel_file.hpp>
#include <srs_kernel_flv.hpp>

SrsVodStream::SrsVodStream(string root_dir)
    : SrsGoHttpFileServer(root_dir)
{
}

SrsVodStream::~SrsVodStream()
{
}

int SrsVodStream::serve_flv_stream(ISrsGoHttpResponseWriter* w, SrsHttpMessage* r, string fullpath, int offset)
{
    int ret = ERROR_SUCCESS;
    
    SrsFileReader fs;
    
    // open flv file
    if ((ret = fs.open(fullpath)) != ERROR_SUCCESS) {
        return ret;
    }
    
    if (offset > fs.filesize()) {
        ret = ERROR_HTTP_FLV_OFFSET_OVERFLOW;
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
            ret = ERROR_HTTP_FLV_SEQUENCE_HEADER;
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
    if ((ret = copy(w, &fs, r, left)) != ERROR_SUCCESS) {
        srs_warn("read flv=%s size=%d failed, ret=%d", fullpath.c_str(), left, ret);
        return ret;
    }
    
    return ret;
}

SrsHttpServer::SrsHttpServer()
{
}

SrsHttpServer::~SrsHttpServer()
{
}

int SrsHttpServer::initialize()
{
    int ret = ERROR_SUCCESS;
    
    bool default_root_exists = false;
    
    // add other virtual path
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

int SrsHttpServer::on_reload_vhost_http_updated()
{
    int ret = ERROR_SUCCESS;
    // TODO: FIXME: implements it.
    return ret;
}

SrsHttpConn::SrsHttpConn(SrsServer* svr, st_netfd_t fd, SrsHttpServer* m) 
    : SrsConnection(svr, fd)
{
    parser = new SrsHttpParser();
    mux = m;
}

SrsHttpConn::~SrsHttpConn()
{
    srs_freep(parser);
}

void SrsHttpConn::kbps_resample()
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
    
    // process http messages.
    for (;;) {
        SrsHttpMessage* req = NULL;
        
        // get a http message
        if ((ret = parser->parse_message(&skt, &req)) != ERROR_SUCCESS) {
            return ret;
        }

        // if SUCCESS, always NOT-NULL and completed message.
        srs_assert(req);
        srs_assert(req->is_complete());
        
        // always free it in this scope.
        SrsAutoFree(SrsHttpMessage, req);
        
        // ok, handle http request.
        SrsGoHttpResponseWriter writer(&skt);
        if ((ret = process_request(&writer, req)) != ERROR_SUCCESS) {
            return ret;
        }
    }
        
    return ret;
}

int SrsHttpConn::process_request(ISrsGoHttpResponseWriter* w, SrsHttpMessage* r) 
{
    int ret = ERROR_SUCCESS;
    
    srs_trace("HTTP %s %s, content-length=%"PRId64"", 
        r->method_str().c_str(), r->url().c_str(), r->content_length());
    
    // use default server mux to serve http request.
    if ((ret = mux->mux.serve_http(w, r)) != ERROR_SUCCESS) {
        if (!srs_is_client_gracefully_close(ret)) {
            srs_error("serve http msg failed. ret=%d", ret);
        }
        return ret;
    }
    
    return ret;
}

#endif

