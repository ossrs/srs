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

#ifndef SRS_APP_HTTP_API_HPP
#define SRS_APP_HTTP_API_HPP

/*
#include <srs_app_http_api.hpp>
*/

#include <srs_core.hpp>

#ifdef SRS_AUTO_HTTP_API

class SrsStSocket;
class SrsHttpMessage;
class SrsHttpParser;
class SrsHttpHandler;

#include <srs_app_st.hpp>
#include <srs_app_conn.hpp>
#include <srs_app_http.hpp>

// for http root.
class SrsGoApiRoot : public ISrsHttpHandler
{
public:
    SrsGoApiRoot();
    virtual ~SrsGoApiRoot();
public:
    virtual int serve_http(ISrsHttpResponseWriter* w, SrsHttpMessage* r);
};

class SrsGoApiApi : public ISrsHttpHandler
{
public:
    SrsGoApiApi();
    virtual ~SrsGoApiApi();
public:
    virtual int serve_http(ISrsHttpResponseWriter* w, SrsHttpMessage* r);
};

class SrsGoApiV1 : public ISrsHttpHandler
{
public:
    SrsGoApiV1();
    virtual ~SrsGoApiV1();
public:
    virtual int serve_http(ISrsHttpResponseWriter* w, SrsHttpMessage* r);
};

class SrsGoApiVersion : public ISrsHttpHandler
{
public:
    SrsGoApiVersion();
    virtual ~SrsGoApiVersion();
public:
    virtual int serve_http(ISrsHttpResponseWriter* w, SrsHttpMessage* r);
};

class SrsGoApiSummaries : public ISrsHttpHandler
{
public:
    SrsGoApiSummaries();
    virtual ~SrsGoApiSummaries();
public:
    virtual int serve_http(ISrsHttpResponseWriter* w, SrsHttpMessage* r);
};

class SrsGoApiRusages : public ISrsHttpHandler
{
public:
    SrsGoApiRusages();
    virtual ~SrsGoApiRusages();
public:
    virtual int serve_http(ISrsHttpResponseWriter* w, SrsHttpMessage* r);
};

class SrsGoApiSelfProcStats : public ISrsHttpHandler
{
public:
    SrsGoApiSelfProcStats();
    virtual ~SrsGoApiSelfProcStats();
public:
    virtual int serve_http(ISrsHttpResponseWriter* w, SrsHttpMessage* r);
};

class SrsGoApiSystemProcStats : public ISrsHttpHandler
{
public:
    SrsGoApiSystemProcStats();
    virtual ~SrsGoApiSystemProcStats();
public:
    virtual int serve_http(ISrsHttpResponseWriter* w, SrsHttpMessage* r);
};

class SrsGoApiMemInfos : public ISrsHttpHandler
{
public:
    SrsGoApiMemInfos();
    virtual ~SrsGoApiMemInfos();
public:
    virtual int serve_http(ISrsHttpResponseWriter* w, SrsHttpMessage* r);
};

class SrsGoApiAuthors : public ISrsHttpHandler
{
public:
    SrsGoApiAuthors();
    virtual ~SrsGoApiAuthors();
public:
    virtual int serve_http(ISrsHttpResponseWriter* w, SrsHttpMessage* r);
};

class SrsGoApiRequests : public ISrsHttpHandler
{
public:
    SrsGoApiRequests();
    virtual ~SrsGoApiRequests();
public:
    virtual int serve_http(ISrsHttpResponseWriter* w, SrsHttpMessage* r);
};

class SrsGoApiVhosts : public ISrsHttpHandler
{
public:
    SrsGoApiVhosts();
    virtual ~SrsGoApiVhosts();
public:
    virtual int serve_http(ISrsHttpResponseWriter* w, SrsHttpMessage* r);
};

class SrsGoApiStreams : public ISrsHttpHandler
{
public:
    SrsGoApiStreams();
    virtual ~SrsGoApiStreams();
public:
    virtual int serve_http(ISrsHttpResponseWriter* w, SrsHttpMessage* r);
};

class SrsHttpApi : public SrsConnection
{
private:
    SrsHttpParser* parser;
    SrsHttpServeMux* mux;
    bool crossdomain_required;
public:
    SrsHttpApi(SrsServer* svr, st_netfd_t fd, SrsHttpServeMux* m);
    virtual ~SrsHttpApi();
// interface IKbpsDelta
public:
    virtual void resample();
    virtual int64_t get_send_bytes_delta();
    virtual int64_t get_recv_bytes_delta();
    virtual void cleanup();
protected:
    virtual int do_cycle();
private:
    virtual int process_request(ISrsHttpResponseWriter* w, SrsHttpMessage* r);
};

#endif

#endif

