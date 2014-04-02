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

#include <srs_app_http_api.hpp>

#ifdef SRS_HTTP_API

#include <sstream>
using namespace std;

#include <srs_kernel_log.hpp>
#include <srs_kernel_error.hpp>
#include <srs_app_http.hpp>
#include <srs_app_socket.hpp>
#include <srs_core_autofree.hpp>

SrsApiRoot::SrsApiRoot()
{
    handlers.push_back(new SrsApiApi());
}

SrsApiRoot::~SrsApiRoot()
{
}

bool SrsApiRoot::can_handle(const char* path, int length, const char** pnext_path)
{
    // reset the next path for child to parse.
    *pnext_path = path;
    
    return true;
}

int SrsApiRoot::process_request(SrsSocket* skt, SrsHttpMessage* req, const char* /*path*/, int /*length*/)
{
    if (req->method() == HTTP_OPTIONS) {
        return res_options(skt);
    } else {
        std::string body = "hello, root";
        return res_text(skt, body);
    }

    return ERROR_SUCCESS;
}

SrsApiApi::SrsApiApi()
{
}

SrsApiApi::~SrsApiApi()
{
}

bool SrsApiApi::can_handle(const char* path, int length, const char** /*pnext_path*/)
{
    return !memcmp("/api", path, length);
}

int SrsApiApi::process_request(SrsSocket* skt, SrsHttpMessage* req, const char* /*path*/, int /*length*/)
{
    if (req->method() == HTTP_OPTIONS) {
        return res_options(skt);
    } else {
        std::string body = "hello, api";
        return res_text(skt, body);
    }

    return ERROR_SUCCESS;
}

SrsHttpApi::SrsHttpApi(SrsServer* srs_server, st_netfd_t client_stfd, SrsHttpHandler* _handler) 
    : SrsConnection(srs_server, client_stfd)
{
    parser = new SrsHttpParser();
    handler = _handler;
}

SrsHttpApi::~SrsHttpApi()
{
    srs_freep(parser);
}

int SrsHttpApi::do_cycle()
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = get_peer_ip()) != ERROR_SUCCESS) {
        srs_error("get peer ip failed. ret=%d", ret);
        return ret;
    }
    srs_trace("api get peer ip success. ip=%s", ip);
    
    // initialize parser
    if ((ret = parser->initialize(HTTP_REQUEST)) != ERROR_SUCCESS) {
        srs_error("api initialize http parser failed. ret=%d", ret);
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

int SrsHttpApi::process_request(SrsSocket* skt, SrsHttpMessage* req) 
{
    int ret = ERROR_SUCCESS;
    
    // TODO: maybe need to parse the url.
    std::string uri = req->url();
    
    int length = 0;
    const char* start = NULL;
    SrsHttpHandler* p = NULL;
    if ((ret = handler->best_match(uri.data(), uri.length(), &p, &start, &length)) != ERROR_SUCCESS) {
        srs_warn("failed to find the best match handler for url. ret=%d", ret);
        return ret;
    }
    
    // if success, p and pstart should be valid.
    srs_assert(p);
    srs_assert(start);
    srs_assert(length <= (int)uri.length());
    
    // use handler to process request.
    if ((ret = p->process_request(skt, req, start, length)) != ERROR_SUCCESS) {
        srs_warn("handler failed to process http request. ret=%d", ret);
        return ret;
    }
    
    return ret;
}

#endif
