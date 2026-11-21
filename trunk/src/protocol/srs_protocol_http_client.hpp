//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_PROTOCOL_HTTP_CLIENT_HPP
#define SRS_PROTOCOL_HTTP_CLIENT_HPP

#include <srs_core.hpp>

#include <map>
#include <string>

#include <openssl/err.h>
#include <openssl/ssl.h>

#include <srs_protocol_http_stack.hpp>
#include <srs_protocol_st.hpp>

class SrsHttpUri;
class SrsHttpParser;
class ISrsHttpMessage;
class SrsStSocket;
class SrsNetworkKbps;
class SrsWallClock;
class SrsTcpClient;

#define SRS_HTTP_CLIENT_TIMEOUT (30 * SRS_UTIME_SECONDS)

class SrsSslClient : public ISrsReader, public ISrsStreamWriter
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsProtocolReadWriter *transport_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    SSL_CTX *ssl_ctx_;
    SSL *ssl_;
    BIO *bio_in_;
    BIO *bio_out_;

public:
    SrsSslClient(ISrsProtocolReadWriter *transport);
    virtual ~SrsSslClient();

public:
    virtual srs_error_t handshake(const std::string &host);

public:
    virtual srs_error_t read(void *buf, size_t size, ssize_t *nread);
    virtual srs_error_t write(void *buf, size_t size, ssize_t *nwrite);
};

class ISrsHttpClient
{
public:
    ISrsHttpClient();
    virtual ~ISrsHttpClient();

public:
    virtual srs_error_t initialize(std::string schema, std::string h, int p, srs_utime_t tm = SRS_HTTP_CLIENT_TIMEOUT) = 0;
    virtual ISrsHttpClient *set_header(std::string k, std::string v);
    virtual srs_error_t get(std::string path, std::string req, ISrsHttpMessage **ppmsg) = 0;
    virtual srs_error_t post(std::string path, std::string req, ISrsHttpMessage **ppmsg) = 0;
    virtual void set_recv_timeout(srs_utime_t tm) = 0;
    virtual void kbps_sample(const char *label, srs_utime_t age) = 0;
};

class SrsHttpClient : public ISrsHttpClient
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    SrsTcpClient *transport_;
    SrsHttpParser *parser_;
    std::map<std::string, std::string> headers_;
    SrsNetworkKbps *kbps_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srs_utime_t timeout_;
    srs_utime_t recv_timeout_;
    std::string schema_;
    std::string host_;
    int port_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    SrsSslClient *ssl_transport_;

public:
    SrsHttpClient();
    virtual ~SrsHttpClient();

public:
    virtual srs_error_t initialize(std::string schema, std::string h, int p, srs_utime_t tm = SRS_HTTP_CLIENT_TIMEOUT);
    virtual SrsHttpClient *set_header(std::string k, std::string v);

public:
    // The caller frees the *ppmsg response.
    virtual srs_error_t post(std::string path, std::string req, ISrsHttpMessage **ppmsg);
    // The caller frees the *ppmsg response.
    virtual srs_error_t get(std::string path, std::string req, ISrsHttpMessage **ppmsg);

public:
    virtual void set_recv_timeout(srs_utime_t tm);

public:
    virtual void kbps_sample(const char *label, srs_utime_t age);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual void disconnect();
    virtual srs_error_t connect();
    ISrsStreamWriter *writer();
    ISrsReader *reader();
};

#endif
