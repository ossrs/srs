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
#include <srs_app_json.hpp>

SrsApiRoot::SrsApiRoot()
{
    handlers.push_back(new SrsApiApi());
}

SrsApiRoot::~SrsApiRoot()
{
}

bool SrsApiRoot::can_handle(const char* path, int length, const char** pchild)
{
    // reset the child path to path,
    // for child to reparse the path.
    *pchild = path;
    
    // only compare the first char.
    return srs_path_equals("/", path, 1);
}

int SrsApiRoot::do_process_request(SrsSocket* skt, SrsHttpMessage* req)
{
    std::stringstream ss;
    
    ss << JOBJECT_START
        << JFIELD_ERROR(ERROR_SUCCESS) << JFIELD_CONT
        << JFIELD_ORG("urls", JOBJECT_START)
            << JFIELD_STR("api", "the api root")
        << JOBJECT_END
        << JOBJECT_END;
    
    return res_json(skt, ss.str());
}

SrsApiApi::SrsApiApi()
{
    handlers.push_back(new SrsApiV1());
}

SrsApiApi::~SrsApiApi()
{
}

bool SrsApiApi::can_handle(const char* path, int length, const char** /*pchild*/)
{
    return srs_path_equals("/api", path, length);
}

int SrsApiApi::do_process_request(SrsSocket* skt, SrsHttpMessage* req)
{
    std::stringstream ss;
    
    ss << JOBJECT_START
        << JFIELD_ERROR(ERROR_SUCCESS) << JFIELD_CONT
        << JFIELD_ORG("urls", JOBJECT_START)
            << JFIELD_STR("v1", "the api version 1.0")
        << JOBJECT_END
        << JOBJECT_END;
    
    return res_json(skt, ss.str());
}

SrsApiV1::SrsApiV1()
{
    handlers.push_back(new SrsApiVersion());
    handlers.push_back(new SrsApiAuthors());
}

SrsApiV1::~SrsApiV1()
{
}

bool SrsApiV1::can_handle(const char* path, int length, const char** /*pchild*/)
{
    return srs_path_equals("/v1", path, length);
}

int SrsApiV1::do_process_request(SrsSocket* skt, SrsHttpMessage* req)
{
    std::stringstream ss;
    
    ss << JOBJECT_START
        << JFIELD_ERROR(ERROR_SUCCESS) << JFIELD_CONT
        << JFIELD_ORG("urls", JOBJECT_START)
            << JFIELD_STR("version", "the version of SRS") << JFIELD_CONT
            << JFIELD_STR("authors", "the primary authors and contributors")
        << JOBJECT_END
        << JOBJECT_END;
    
    return res_json(skt, ss.str());
}

SrsApiVersion::SrsApiVersion()
{
}

SrsApiVersion::~SrsApiVersion()
{
}

bool SrsApiVersion::can_handle(const char* path, int length, const char** /*pchild*/)
{
    return srs_path_equals("/version", path, length);
}

int SrsApiVersion::do_process_request(SrsSocket* skt, SrsHttpMessage* req)
{
    std::stringstream ss;
    
    ss << JOBJECT_START
        << JFIELD_ERROR(ERROR_SUCCESS) << JFIELD_CONT
        << JFIELD_ORG("data", JOBJECT_START)
            << JFIELD_ORG("major", VERSION_MAJOR) << JFIELD_CONT
            << JFIELD_ORG("minor", VERSION_MINOR) << JFIELD_CONT
            << JFIELD_ORG("revision", VERSION_REVISION) << JFIELD_CONT
            << JFIELD_STR("version", RTMP_SIG_SRS_VERSION)
        << JOBJECT_END
        << JOBJECT_END;
    
    return res_json(skt, ss.str());
}

SrsApiAuthors::SrsApiAuthors()
{
}

SrsApiAuthors::~SrsApiAuthors()
{
}

bool SrsApiAuthors::can_handle(const char* path, int length, const char** /*pchild*/)
{
    return srs_path_equals("/authors", path, length);
}

int SrsApiAuthors::do_process_request(SrsSocket* skt, SrsHttpMessage* req)
{
    std::stringstream ss;
    
    ss << JOBJECT_START
        << JFIELD_ERROR(ERROR_SUCCESS) << JFIELD_CONT
        << JFIELD_ORG("data", JOBJECT_START)
            << JFIELD_STR("primary_authors", RTMP_SIG_SRS_PRIMARY_AUTHROS) << JFIELD_CONT
            << JFIELD_STR("contributors_link", RTMP_SIG_SRS_CONTRIBUTORS_URL) << JFIELD_CONT
            << JFIELD_STR("contributors", SRS_CONSTRIBUTORS)
        << JOBJECT_END
        << JOBJECT_END;
    
    return res_json(skt, ss.str());
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
    
    // use handler to process request.
    if ((ret = p->handler->process_request(skt, req)) != ERROR_SUCCESS) {
        srs_warn("handler failed to process http request. ret=%d", ret);
        return ret;
    }
    
    return ret;
}

#endif
