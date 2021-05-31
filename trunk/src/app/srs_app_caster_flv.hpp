//
// Copyright (c) 2013-2021 Winlin
//
// SPDX-License-Identifier: MIT
//

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

#include <srs_app_st.hpp>
#include <srs_app_listener.hpp>
#include <srs_app_conn.hpp>
#include <srs_app_http_conn.hpp>
#include <srs_kernel_file.hpp>

// The stream caster for flv stream over HTTP POST.
class SrsAppCasterFlv : public ISrsTcpHandler, public ISrsResourceManager, public ISrsHttpHandler
{
private:
    std::string output;
    SrsHttpServeMux* http_mux;
    std::vector<ISrsStartableConneciton*> conns;
    SrsResourceManager* manager;
public:
    SrsAppCasterFlv(SrsConfDirective* c);
    virtual ~SrsAppCasterFlv();
public:
    virtual srs_error_t initialize();
// Interface ISrsTcpHandler
public:
    virtual srs_error_t on_tcp_client(srs_netfd_t stfd);
// Interface ISrsResourceManager
public:
    virtual void remove(ISrsResource* c);
// Interface ISrsHttpHandler
public:
    virtual srs_error_t serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r);
};

// The dynamic http connection, never drop the body.
class SrsDynamicHttpConn : public ISrsStartableConneciton, public ISrsHttpConnOwner, public ISrsReloadHandler
{
private:
    // The manager object to manage the connection.
    ISrsResourceManager* manager;
    std::string output;
    SrsPithyPrint* pprint;
    SrsSimpleRtmpClient* sdk;
    SrsTcpConnection* skt;
    SrsHttpConn* conn;
private:
    // The ip and port of client.
    std::string ip;
    int port;
public:
    SrsDynamicHttpConn(ISrsResourceManager* cm, srs_netfd_t fd, SrsHttpServeMux* m, std::string cip, int port);
    virtual ~SrsDynamicHttpConn();
public:
    virtual srs_error_t proxy(ISrsHttpResponseWriter* w, ISrsHttpMessage* r, std::string o);
private:
    virtual srs_error_t do_proxy(ISrsHttpResponseReader* rr, SrsFlvDecoder* dec);
// Extract APIs from SrsTcpConnection.
// Interface ISrsReloadHandler
public:
    virtual srs_error_t on_reload_http_stream_crossdomain();
// Interface ISrsHttpConnOwner.
public:
    virtual srs_error_t on_start();
    virtual srs_error_t on_http_message(ISrsHttpMessage* r, SrsHttpResponseWriter* w);
    virtual srs_error_t on_message_done(ISrsHttpMessage* r, SrsHttpResponseWriter* w);
    virtual srs_error_t on_conn_done(srs_error_t r0);
// Interface ISrsResource.
public:
    virtual std::string desc();
// Interface ISrsConnection.
public:
    virtual std::string remote_ip();
    virtual const SrsContextId& get_id();
// Interface ISrsStartable
public:
    virtual srs_error_t start();
// Interface ISrsKbpsDelta
public:
    virtual void remark(int64_t* in, int64_t* out);
};

// The http wrapper for file reader, to read http post stream like a file.
class SrsHttpFileReader : public SrsFileReader
{
private:
    ISrsHttpResponseReader* http;
public:
    SrsHttpFileReader(ISrsHttpResponseReader* h);
    virtual ~SrsHttpFileReader();
public:
    // Open file reader, can open then close then open...
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

