/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2017 OSSRS(winlin)
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

#include <srs_app_http_static.hpp>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>

#include <sstream>
using namespace std;

#include <srs_protocol_stream.hpp>
#include <srs_protocol_utility.hpp>
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

SrsVodStream::SrsVodStream(string root_dir)
: SrsHttpFileServer(root_dir)
{
}

SrsVodStream::~SrsVodStream()
{
}

srs_error_t SrsVodStream::serve_flv_stream(ISrsHttpResponseWriter* w, ISrsHttpMessage* r, string fullpath, int offset)
{
    int ret = ERROR_SUCCESS;
    srs_error_t err = srs_success;
    
    SrsFileReader fs;
    
    // open flv file
    if ((ret = fs.open(fullpath)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "open file");
    }
    
    if (offset > fs.filesize()) {
        return srs_error_new(ERROR_HTTP_REMUX_OFFSET_OVERFLOW, "http flv streaming %s overflow. size=%" PRId64 ", offset=%d",
            fullpath.c_str(), fs.filesize(), offset);
    }
    
    SrsFlvVodStreamDecoder ffd;
    
    // open fast decoder
    if ((ret = ffd.initialize(&fs)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "init ffd");
    }
    
    // save header, send later.
    char flv_header[13];
    
    // send flv header
    if ((ret = ffd.read_header_ext(flv_header)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "ffd read header");
    }
    
    // save sequence header, send later
    char* sh_data = NULL;
    int sh_size = 0;
    
    if (true) {
        // send sequence header
        int64_t start = 0;
        if ((ret = ffd.read_sequence_header_summary(&start, &sh_size)) != ERROR_SUCCESS) {
            return srs_error_new(ret, "ffd read sps");
        }
        if (sh_size <= 0) {
            return srs_error_new(ERROR_HTTP_REMUX_SEQUENCE_HEADER, "no sequence, size=%d", sh_size);
        }
    }
    sh_data = new char[sh_size];
    SrsAutoFreeA(char, sh_data);
    if ((ret = fs.read(sh_data, sh_size, NULL)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "fs read");
    }
    
    // seek to data offset
    int64_t left = fs.filesize() - offset;
    
    // write http header for ts.
    w->header()->set_content_length((int)(sizeof(flv_header) + sh_size + left));
    w->header()->set_content_type("video/x-flv");
    
    // write flv header and sequence header.
    if ((ret = w->write(flv_header, sizeof(flv_header))) != ERROR_SUCCESS) {
        return srs_error_new(ret, "write flv header");
    }
    if (sh_size > 0 && (ret = w->write(sh_data, sh_size)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "write sequence");
    }
    
    // write body.
    if ((ret = ffd.seek2(offset)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "ffd seek");
    }
    
    // send data
    if ((err = copy(w, &fs, r, (int)left)) != srs_success) {
        return srs_error_wrap(err, "read flv=%s size=%d", fullpath.c_str(), left);
    }
    
    return err;
}

srs_error_t SrsVodStream::serve_mp4_stream(ISrsHttpResponseWriter* w, ISrsHttpMessage* r, string fullpath, int start, int end)
{
    int ret = ERROR_SUCCESS;
    srs_error_t err = srs_success;
    
    srs_assert(start >= 0);
    srs_assert(end == -1 || end >= 0);
    
    SrsFileReader fs;
    
    // open flv file
    if ((ret = fs.open(fullpath)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "fs open");
    }
    
    // parse -1 to whole file.
    if (end == -1) {
        end = (int)fs.filesize();
    }
    
    if (end > fs.filesize() || start > end) {
        return srs_error_new(ERROR_HTTP_REMUX_OFFSET_OVERFLOW, "http mp4 streaming %s overflow. size=%" PRId64 ", offset=%d",
            fullpath.c_str(), fs.filesize(), start);
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
    fs.seek2(start);
    
    // send data
    if ((err = copy(w, &fs, r, (int)left)) != srs_success) {
        return srs_error_wrap(err, "read mp4=%s size=%d", fullpath.c_str(), left);
    }
    
    return err;
}

SrsHttpStaticServer::SrsHttpStaticServer(SrsServer* svr)
{
    server = svr;
    _srs_config->subscribe(this);
}

SrsHttpStaticServer::~SrsHttpStaticServer()
{
    _srs_config->unsubscribe(this);
}

srs_error_t SrsHttpStaticServer::initialize()
{
    int ret = ERROR_SUCCESS;
    srs_error_t err = srs_success;
    
    bool default_root_exists = false;
    
    // http static file and flv vod stream mount for each vhost.
    SrsConfDirective* root = _srs_config->get_root();
    for (int i = 0; i < (int)root->directives.size(); i++) {
        SrsConfDirective* conf = root->at(i);
        
        if (!conf->is_vhost()) {
            continue;
        }
        
        string pmount;
        string vhost = conf->arg0();
        if ((ret = mount_vhost(vhost, pmount)) != ERROR_SUCCESS) {
            return srs_error_new(ret, "mount vhost");
        }
        
        if (pmount == "/") {
            default_root_exists = true;
            std::string dir = _srs_config->get_vhost_http_dir(vhost);
            srs_warn("http: root mount to %s", dir.c_str());
        }
    }
    
    if (!default_root_exists) {
        // add root
        std::string dir = _srs_config->get_http_stream_dir();
        if ((err = mux.handle("/", new SrsVodStream(dir))) != srs_success) {
            return srs_error_wrap(err, "mount root dir=%s", dir.c_str());
        }
        srs_trace("http: root mount to %s", dir.c_str());
    }
    
    return err;
}

int SrsHttpStaticServer::mount_vhost(string vhost, string& pmount)
{
    int ret = ERROR_SUCCESS;
    srs_error_t err = srs_success;
    
    // when vhost disabled, ignore.
    if (!_srs_config->get_vhost_enabled(vhost)) {
        return ret;
    }
    
    // when vhost http_static disabled, ignore.
    if (!_srs_config->get_vhost_http_enabled(vhost)) {
        return ret;
    }
    
    std::string mount = _srs_config->get_vhost_http_mount(vhost);
    std::string dir = _srs_config->get_vhost_http_dir(vhost);
    
    // replace the vhost variable
    mount = srs_string_replace(mount, "[vhost]", vhost);
    dir = srs_string_replace(dir, "[vhost]", vhost);
    
    // remove the default vhost mount
    mount = srs_string_replace(mount, SRS_CONSTS_RTMP_DEFAULT_VHOST"/", "/");
    
    // the dir mount must always ends with "/"
    if (mount != "/" && !srs_string_ends_with(mount, "/")) {
        mount += "/";
    }
    
    // mount the http of vhost.
    if ((err = mux.handle(mount, new SrsVodStream(dir))) != srs_success) {
        // TODO: FIXME: Use error.
        ret = srs_error_code(err);
        srs_freep(err);
        
        srs_error("http: mount dir=%s for vhost=%s failed. ret=%d", dir.c_str(), vhost.c_str(), ret);
        return ret;
    }
    srs_trace("http: vhost=%s mount to %s at %s", vhost.c_str(), mount.c_str(), dir.c_str());
    
    pmount = mount;
    
    return ret;
}

int SrsHttpStaticServer::on_reload_vhost_added(string vhost)
{
    int ret = ERROR_SUCCESS;
    
    string pmount;
    if ((ret = mount_vhost(vhost, pmount)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int SrsHttpStaticServer::on_reload_vhost_http_updated()
{
    int ret = ERROR_SUCCESS;
    // TODO: FIXME: implements it.
    return ret;
}

