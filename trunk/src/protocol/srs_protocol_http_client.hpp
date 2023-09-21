//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#ifndef SRS_PROTOCOL_HTTP_CLIENT_HPP
#define SRS_PROTOCOL_HTTP_CLIENT_HPP

#include <srs_core.hpp>

#include <string>
#include <map>

#include <openssl/ssl.h>
#include <openssl/err.h>

#include <srs_protocol_st.hpp>
#include <srs_protocol_http_stack.hpp>

class SrsHttpUri;
class SrsHttpParser;
class ISrsHttpMessage;
class SrsStSocket;
class SrsNetworkKbps;
class SrsWallClock;
class SrsTcpClient;

// The default timeout for http client.
#define SRS_HTTP_CLIENT_TIMEOUT (30 * SRS_UTIME_SECONDS)

// The SSL client over TCP transport.
class SrsSslClient : public ISrsReader, public ISrsStreamWriter
{
private:
    SrsTcpClient* transport;
private:
    SSL_CTX* ssl_ctx;
    SSL* ssl;
    BIO* bio_in;
    BIO* bio_out;
public:
    SrsSslClient(SrsTcpClient* tcp);
    virtual ~SrsSslClient();
public:
    virtual srs_error_t handshake(const std::string& host);
public:
    virtual srs_error_t read(void* buf, size_t size, ssize_t* nread);
    virtual srs_error_t write(void* buf, size_t size, ssize_t* nwrite);
};

// The client to GET/POST/PUT/DELETE over HTTP.
// @remark We will reuse the TCP transport until initialize or channel error,
//      such as send/recv failed.
// Usage:
//      SrsHttpClient hc;
//      hc.initialize("127.0.0.1", 80, 9000);
//      hc.post("/api/v1/version", "Hello world!", NULL);
class SrsHttpClient
{
private:
    // The underlayer TCP transport, set to NULL when disconnect, or never not NULL when connected.
    // We will disconnect transport when initialize or channel error, such as send/recv error.
    SrsTcpClient* transport;
    SrsHttpParser* parser;
    std::map<std::string, std::string> headers;
    SrsNetworkKbps* kbps;
private:
    // The timeout in srs_utime_t.
    srs_utime_t timeout;
    srs_utime_t recv_timeout;
    // The schema, host name or ip.
    std::string schema_;
    std::string host;
    int port;
private:
    SrsSslClient* ssl_transport;
public:
    SrsHttpClient();
    virtual ~SrsHttpClient();
public:
    // Initliaze the client, disconnect the transport, renew the HTTP parser.
    // @param schema Should be http or https.
    // @param tm The underlayer TCP transport timeout in srs_utime_t.
    // @remark we will set default values in headers, which can be override by set_header.
    virtual srs_error_t initialize(std::string schema, std::string h, int p, srs_utime_t tm = SRS_HTTP_CLIENT_TIMEOUT);
    // Set HTTP request header in header[k]=v.
    // @return the HTTP client itself.
    virtual SrsHttpClient* set_header(std::string k, std::string v);
public:
    // Post data to the uri.
    // @param the path to request on.
    // @param req the data post to uri. empty string to ignore.
    // @param ppmsg output the http message to read the response.
    // @remark user must free the ppmsg if not NULL.
    virtual srs_error_t post(std::string path, std::string req, ISrsHttpMessage** ppmsg);
    // Get data from the uri.
    // @param the path to request on.
    // @param req the data post to uri. empty string to ignore.
    // @param ppmsg output the http message to read the response.
    // @remark user must free the ppmsg if not NULL.
    virtual srs_error_t get(std::string path, std::string req, ISrsHttpMessage** ppmsg);
public:
    virtual void set_recv_timeout(srs_utime_t tm);
public:
    virtual void kbps_sample(const char* label, srs_utime_t age);
private:
    virtual void disconnect();
    virtual srs_error_t connect();
    ISrsStreamWriter* writer();
    ISrsReader* reader();
};

#endif

