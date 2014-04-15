/*
The MIT License (MIT)

Copyright (c) 2013-2014 winlin

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

#include <sstream>
using namespace std;

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <srs_kernel_log.hpp>
#include <srs_kernel_error.hpp>
#include <srs_app_socket.hpp>
#include <srs_app_http.hpp>
#include <srs_core_autofree.hpp>
#include <srs_app_json.hpp>
#include <srs_app_config.hpp>

#define SRS_HTTP_DEFAULT_PAGE "index.html"

SrsHttpRoot::SrsHttpRoot()
{
    // TODO: FIXME: support reload vhosts.
}

SrsHttpRoot::~SrsHttpRoot()
{
}

int SrsHttpRoot::initialize()
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
        
        handlers.push_back(new SrsHttpVhost(vhost, mount, dir));
        
        if (mount == "/") {
            default_root_exists = true;
        }
    }
    
    if (!default_root_exists) {
        // add root
        handlers.push_back(new SrsHttpVhost(
            "__http__", "/", _srs_config->get_http_stream_dir()));
    }
    
    return ret;
}

int SrsHttpRoot::best_match(const char* path, int length, SrsHttpHandlerMatch** ppmatch)
{
    int ret = ERROR_SUCCESS;
        
    // find the best matched child handler.
    std::vector<SrsHttpHandler*>::iterator it;
    for (it = handlers.begin(); it != handlers.end(); ++it) {
        SrsHttpHandler* h = *it;
        
        // search all child handlers.
        h->best_match(path, length, ppmatch);
    }
    
    // if already matched by child, return.
    if (*ppmatch) {
        return ret;
    }
    
    // not matched, error.
    return ERROR_HTTP_HANDLER_MATCH_URL;
}

bool SrsHttpRoot::is_handler_valid(SrsHttpMessage* req, int& status_code, std::string& reason_phrase) 
{
    status_code = HTTP_InternalServerError;
    reason_phrase = HTTP_InternalServerError_str;
    
    return false;
}

int SrsHttpRoot::do_process_request(SrsSocket* skt, SrsHttpMessage* req)
{
    int ret = ERROR_SUCCESS;
    return ret;
}

SrsHttpVhost::SrsHttpVhost(std::string vhost, std::string mount, std::string dir)
{
    _vhost = vhost;
    _mount = mount;
    _dir = dir;
}

SrsHttpVhost::~SrsHttpVhost()
{
}

bool SrsHttpVhost::can_handle(const char* path, int length, const char** /*pchild*/)
{
    return srs_path_like(_mount.c_str(), path, length);
}

bool SrsHttpVhost::is_handler_valid(SrsHttpMessage* req, int& status_code, std::string& reason_phrase) 
{
    std::string fullpath = get_request_file(req);
    
    if (::access(fullpath.c_str(), F_OK | R_OK) < 0) {
        srs_warn("check file %s does not exists", fullpath.c_str());
        
        status_code = HTTP_NotFound;
        reason_phrase = HTTP_NotFound_str;
        return false;
    }
    
    return true;
}

int SrsHttpVhost::do_process_request(SrsSocket* skt, SrsHttpMessage* req)
{
    int ret = ERROR_SUCCESS;
    
    std::string fullpath = get_request_file(req);
    
    int fd = ::open(fullpath.c_str(), O_RDONLY);
    if (fd < 0) {
        ret = ERROR_HTTP_OPEN_FILE;
        srs_warn("open file %s failed, ret=%d", fullpath.c_str(), ret);
        return ret;
    }

    int64_t length = (int64_t)::lseek(fd, 0, SEEK_END);
    ::lseek(fd, 0, SEEK_SET);
    
    char* buf = new char[length];
    SrsAutoFree(char, buf, true);
    
    // TODO: FIXME: use st_read.
    if (::read(fd, buf, length) < 0) {
        ::close(fd);
        ret = ERROR_HTTP_READ_FILE;
        srs_warn("read file %s failed, ret=%d", fullpath.c_str(), ret);
        return ret;
    }
    ::close(fd);
    
    std::string str;
    str.append(buf, length);
    
    if (srs_string_ends_with(fullpath, ".ts")) {
        return res_mpegts(skt, req, str);
    } else if (srs_string_ends_with(fullpath, ".m3u8")) {
        return res_m3u8(skt, req, str);
    } else if (srs_string_ends_with(fullpath, ".xml")) {
        return res_xml(skt, req, str);
    } else if (srs_string_ends_with(fullpath, ".js")) {
        return res_javascript(skt, req, str);
    } else if (srs_string_ends_with(fullpath, ".json")) {
        return res_json(skt, req, str);
    } else if (srs_string_ends_with(fullpath, ".swf")) {
        return res_swf(skt, req, str);
    } else if (srs_string_ends_with(fullpath, ".css")) {
        return res_css(skt, req, str);
    } else if (srs_string_ends_with(fullpath, ".ico")) {
        return res_ico(skt, req, str);
    } else {
        return res_text(skt, req, str);
    }
    
    return ret;
}

string SrsHttpVhost::get_request_file(SrsHttpMessage* req)
{
    std::string fullpath = _dir + "/"; 
    
    // if root, directly use the matched url.
    if (_mount == "/") {
        // add the dir
        fullpath += req->match()->matched_url;
        // if file speicified, add the file.
        if (!req->match()->unmatched_url.empty()) {
            fullpath += "/" + req->match()->unmatched_url;
        }
    } else {
        // virtual path, ignore the virutal path.
        fullpath += req->match()->unmatched_url;
    }
    
    // add default pages.
    if (srs_string_ends_with(fullpath, "/")) {
        fullpath += SRS_HTTP_DEFAULT_PAGE;
    }
    
    return fullpath;
}

string SrsHttpVhost::vhost()
{
    return _vhost;
}

string SrsHttpVhost::mount()
{
    return _mount;
}

string SrsHttpVhost::dir()
{
    return _dir;
}

SrsHttpConn::SrsHttpConn(SrsServer* srs_server, st_netfd_t client_stfd, SrsHttpHandler* _handler) 
    : SrsConnection(srs_server, client_stfd)
{
    parser = new SrsHttpParser();
    handler = _handler;
    requires_crossdomain = false;
}

SrsHttpConn::~SrsHttpConn()
{
    srs_freep(parser);
}

int SrsHttpConn::do_cycle()
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = get_peer_ip()) != ERROR_SUCCESS) {
        srs_error("get peer ip failed. ret=%d", ret);
        return ret;
    }
    srs_trace("http get peer ip success. ip=%s", ip);
    
    // initialize parser
    if ((ret = parser->initialize(HTTP_REQUEST)) != ERROR_SUCCESS) {
        srs_error("http initialize http parser failed. ret=%d", ret);
        return ret;
    }
    
    // underlayer socket
    SrsSocket skt(stfd);
    
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
        SrsAutoFree(SrsHttpMessage, req, false);
        
        // ok, handle http request.
        if ((ret = process_request(&skt, req)) != ERROR_SUCCESS) {
            return ret;
        }
    }
        
    return ret;
}

int SrsHttpConn::process_request(SrsSocket* skt, SrsHttpMessage* req) 
{
    int ret = ERROR_SUCCESS;

    // parse uri to schema/server:port/path?query
    if ((ret = req->parse_uri()) != ERROR_SUCCESS) {
        return ret;
    }
    
    srs_trace("http request parsed, method=%d, url=%s, content-length=%"PRId64"", 
        req->method(), req->url().c_str(), req->content_length());
    
    // TODO: maybe need to parse the url.
    std::string url = req->path();
    
    SrsHttpHandlerMatch* p = NULL;
    if ((ret = handler->best_match(url.data(), url.length(), &p)) != ERROR_SUCCESS) {
        srs_warn("failed to find the best match handler for url. ret=%d", ret);
        return ret;
    }
    
    // if success, p and pstart should be valid.
    srs_assert(p);
    srs_assert(p->handler);
    srs_assert(p->matched_url.length() <= url.length());
    srs_info("best match handler, matched_url=%s", p->matched_url.c_str());
    
    req->set_match(p);
    req->set_requires_crossdomain(requires_crossdomain);
    
    // use handler to process request.
    if ((ret = p->handler->process_request(skt, req)) != ERROR_SUCCESS) {
        srs_warn("handler failed to process http request. ret=%d", ret);
        return ret;
    }
    
    if (req->requires_crossdomain()) {
        requires_crossdomain = true;
    }
    
    return ret;
}

#endif
