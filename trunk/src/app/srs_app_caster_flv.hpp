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

#ifndef SRS_APP_CASTER_FLV_HPP
#define SRS_APP_CASTER_FLV_HPP

/*
#include <srs_app_caster_flv.hpp>
*/

#include <srs_core.hpp>

#include <string>
#include <vector>

#ifdef SRS_AUTO_STREAM_CASTER

class SrsConfDirective;
class SrsHttpServeMux;
class SrsRtmpClient;
class SrsStSocket;
class SrsRequest;
class SrsPithyPrint;
class ISrsHttpResponseReader;
class SrsFlvDecoder;
class SrsTcpClient;

#include <srs_app_st.hpp>
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
public:
    SrsAppCasterFlv(SrsConfDirective* c);
    virtual ~SrsAppCasterFlv();
public:
    virtual int initialize();
// ISrsTcpHandler
public:
    virtual int on_tcp_client(st_netfd_t stfd);
// IConnectionManager
public:
    virtual void remove(SrsConnection* c);
// ISrsHttpHandler
public:
    virtual int serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r);
};

/**
 * the dynamic http connection, never drop the body.
 */
class SrsDynamicHttpConn : public SrsHttpConn
{
private:
    std::string output;
    SrsPithyPrint* pprint;
private:
    SrsRequest* req;
    SrsTcpClient* transport;
    SrsRtmpClient* client;
    int stream_id;
public:
    SrsDynamicHttpConn(IConnectionManager* cm, st_netfd_t fd, SrsHttpServeMux* m);
    virtual ~SrsDynamicHttpConn();
public:
    virtual int on_got_http_message(ISrsHttpMessage* msg);
public:
    virtual int proxy(ISrsHttpResponseWriter* w, ISrsHttpMessage* r, std::string o);
private:
    virtual int do_proxy(ISrsHttpResponseReader* rr, SrsFlvDecoder* dec);
    virtual int rtmp_write_packet(char type, u_int32_t timestamp, char* data, int size);
private:
    // connect to rtmp output url.
    // @remark ignore when not connected, reconnect when disconnected.
    virtual int connect();
    virtual int connect_app(std::string ep_server, int ep_port);
    // close the connected io and rtmp to ready to be re-connect.
    virtual void close();
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
    virtual int open(std::string file);
    virtual void close();
public:
    // TODO: FIXME: extract interface.
    virtual bool is_open();
    virtual int64_t tellg();
    virtual void skip(int64_t size);
    virtual int64_t lseek(int64_t offset);
    virtual int64_t filesize();
public:
    /**
     * read from file.
     * @param pnread the output nb_read, NULL to ignore.
     */
    virtual int read(void* buf, size_t count, ssize_t* pnread);
};

#endif

#endif
