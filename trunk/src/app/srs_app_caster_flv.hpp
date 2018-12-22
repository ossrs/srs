/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2018 Winlin
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

#ifndef SRS_APP_CASTER_FLV_HPP
#define SRS_APP_CASTER_FLV_HPP

#include <srs_core.hpp>

#include <string>
#include <vector>

class SrsConfDirective;
class SrsHttpServeMux;
class SrsRtmpClient;
class SrsStSocket;
class SrsRequest;
class SrsPithyPrint;
class ISrsHttpResponseReader;
class SrsFlvDecoder;
class SrsTcpClient;
class SrsSimpleRtmpClient;

#include <srs_app_thread.hpp>
#include <srs_app_listener.hpp>
#include <srs_app_conn.hpp>
#include <srs_app_http_conn.hpp>
#include <srs_kernel_file.hpp>

/**
 * the stream caster for flv stream over HTTP POST.
 */
class SrsAppCasterFlv : virtual public ISrsTcpHandler
    , virtual public IConnectionManager, virtual public ISrsHttpHandler
{
private:
    std::string output;
    SrsHttpServeMux* http_mux;
    std::vector<SrsHttpConn*> conns;
    SrsCoroutineManager* manager;
public:
    SrsAppCasterFlv(SrsConfDirective* c);
    virtual ~SrsAppCasterFlv();
public:
    virtual srs_error_t initialize();
// ISrsTcpHandler
public:
    virtual srs_error_t on_tcp_client(srs_netfd_t stfd);
// IConnectionManager
public:
    virtual void remove(ISrsConnection* c);
// ISrsHttpHandler
public:
    virtual srs_error_t serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r);
};

/**
 * the dynamic http connection, never drop the body.
 */
class SrsDynamicHttpConn : public SrsHttpConn
{
private:
    std::string output;
    SrsPithyPrint* pprint;
    SrsSimpleRtmpClient* sdk;
public:
    SrsDynamicHttpConn(IConnectionManager* cm, srs_netfd_t fd, SrsHttpServeMux* m, std::string cip);
    virtual ~SrsDynamicHttpConn();
public:
    virtual srs_error_t on_got_http_message(ISrsHttpMessage* msg);
public:
    virtual srs_error_t proxy(ISrsHttpResponseWriter* w, ISrsHttpMessage* r, std::string o);
private:
    virtual srs_error_t do_proxy(ISrsHttpResponseReader* rr, SrsFlvDecoder* dec);
};

/**
 * the http wrapper for file reader,
 * to read http post stream like a file.
 */
class SrsHttpFileReader : public SrsFileReader
{
private:
    ISrsHttpResponseReader* http;
public:
    SrsHttpFileReader(ISrsHttpResponseReader* h);
    virtual ~SrsHttpFileReader();
public:
    /**
     * open file reader, can open then close then open...
     */
    virtual srs_error_t open(std::string file);
    virtual void close();
public:
    // TODO: FIXME: extract interface.
    virtual bool is_open();
    virtual int64_t tellg();
    virtual void skip(int64_t size);
    virtual int64_t seek2(int64_t offset);
    virtual int64_t filesize();
public:
    virtual srs_error_t read(void* buf, size_t count, ssize_t* pnread);
    virtual srs_error_t lseek(off_t offset, int whence, off_t* seeked);
};

#endif

