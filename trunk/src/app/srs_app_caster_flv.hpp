//
// Copyright (c) 2013-2025 The SRS Authors
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
class ISrsRequest;
class SrsPithyPrint;
class ISrsHttpResponseReader;
class SrsFlvDecoder;
class SrsTcpClient;
class SrsSimpleRtmpClient;
class SrsAppCasterFlv;
class ISrsAppCasterFlv;
class ISrsAppConfig;
class ISrsAppFactory;
class ISrsBasicRtmpClient;
class ISrsProtocolReadWriter;
class ISrsHttpConn;

#include <srs_app_http_conn.hpp>
#include <srs_app_listener.hpp>
#include <srs_app_st.hpp>
#include <srs_kernel_file.hpp>
#include <srs_protocol_conn.hpp>

// The http flv listener.
class ISrsHttpFlvListener : public ISrsTcpHandler, public ISrsListener
{
public:
    ISrsHttpFlvListener();
    virtual ~ISrsHttpFlvListener();

public:
};

// A TCP listener, for flv stream server.
class SrsHttpFlvListener : public ISrsHttpFlvListener
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsAppConfig *config_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    SrsTcpListener *listener_;
    ISrsAppCasterFlv *caster_;

public:
    SrsHttpFlvListener();
    virtual ~SrsHttpFlvListener();

public:
    srs_error_t initialize(SrsConfDirective *c);
    virtual srs_error_t listen();
    void close();
    // Interface ISrsTcpHandler
public:
    virtual srs_error_t on_tcp_client(ISrsListener *listener, srs_netfd_t stfd);
};

// The http flv caster interface.
class ISrsAppCasterFlv : public ISrsTcpHandler, public ISrsResourceManager, public ISrsHttpHandler
{
public:
    ISrsAppCasterFlv();
    virtual ~ISrsAppCasterFlv();

public:
    virtual srs_error_t initialize(SrsConfDirective *c) = 0;
};

// The stream caster for flv stream over HTTP POST.
class SrsAppCasterFlv : public ISrsAppCasterFlv
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsAppConfig *config_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    std::string output_;
    SrsHttpServeMux *http_mux_;
    std::vector<ISrsConnection *> conns_;
    SrsResourceManager *manager_;

public:
    SrsAppCasterFlv();
    virtual ~SrsAppCasterFlv();

public:
    virtual srs_error_t initialize(SrsConfDirective *c);
    // Interface ISrsTcpHandler
public:
    virtual srs_error_t on_tcp_client(ISrsListener *listener, srs_netfd_t stfd);
    // Interface ISrsResourceManager
public:
    virtual srs_error_t start();
    virtual bool empty();
    virtual size_t size();
    virtual void add(ISrsResource *conn, bool *exists = NULL);
    virtual void add_with_id(const std::string &id, ISrsResource *conn);
    virtual void add_with_fast_id(uint64_t id, ISrsResource *conn);
    virtual void add_with_name(const std::string &name, ISrsResource *conn);
    virtual ISrsResource *at(int index);
    virtual ISrsResource *find_by_id(std::string id);
    virtual ISrsResource *find_by_fast_id(uint64_t id);
    virtual ISrsResource *find_by_name(std::string name);
    virtual void remove(ISrsResource *c);
    virtual void subscribe(ISrsDisposingHandler *h);
    virtual void unsubscribe(ISrsDisposingHandler *h);
    // Interface ISrsHttpHandler
public:
    virtual srs_error_t serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r);
};

// The dynamic http connection, never drop the body.
class ISrsDynamicHttpConn : public ISrsConnection, public ISrsStartable, public ISrsHttpConnOwner
{
public:
    ISrsDynamicHttpConn();
    virtual ~ISrsDynamicHttpConn();

public:
};

// The dynamic http connection, never drop the body.
class SrsDynamicHttpConn : public ISrsDynamicHttpConn
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsAppConfig *config_;
    ISrsAppFactory *app_factory_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // The manager object to manage the connection.
    ISrsResourceManager *manager_;
    std::string output_;
    SrsPithyPrint *pprint_;
    ISrsBasicRtmpClient *sdk_;
    ISrsProtocolReadWriter *skt_;
    ISrsHttpConn *conn_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // The ip and port of client.
    std::string ip_;
    int port_;

public:
    SrsDynamicHttpConn(ISrsResourceManager *cm, srs_netfd_t fd, SrsHttpServeMux *m, std::string cip, int port);
    virtual ~SrsDynamicHttpConn();

public:
    virtual srs_error_t proxy(ISrsHttpResponseWriter *w, ISrsHttpMessage *r, std::string o);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t do_proxy(ISrsHttpResponseReader *rr, SrsFlvDecoder *dec);
    // Extract APIs from SrsTcpConnection.
    // Interface ISrsHttpConnOwner.
public:
    virtual srs_error_t on_start();
    virtual srs_error_t on_http_message(ISrsHttpMessage *r, ISrsHttpResponseWriter *w);
    virtual srs_error_t on_message_done(ISrsHttpMessage *r, ISrsHttpResponseWriter *w);
    virtual srs_error_t on_conn_done(srs_error_t r0);
    // Interface ISrsResource.
public:
    virtual std::string desc();
    // Interface ISrsConnection.
public:
    virtual std::string remote_ip();
    virtual const SrsContextId &get_id();
    // Interface ISrsStartable
public:
    virtual srs_error_t start();
};

// The http wrapper for file reader, to read http post stream like a file.
class ISrsHttpFileReader : public ISrsFileReader
{
public:
    ISrsHttpFileReader();
    virtual ~ISrsHttpFileReader();

public:
};

// The http wrapper for file reader, to read http post stream like a file.
class SrsHttpFileReader : public ISrsHttpFileReader
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsHttpResponseReader *http_;
    SrsFileReader *file_reader_;

public:
    SrsHttpFileReader(ISrsHttpResponseReader *h);
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
    virtual srs_error_t read(void *buf, size_t count, ssize_t *pnread);
    virtual srs_error_t lseek(off_t offset, int whence, off_t *seeked);
};

#endif
