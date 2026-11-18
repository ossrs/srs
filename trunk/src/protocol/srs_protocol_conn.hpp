//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_PROTOCOL_CONN_HPP
#define SRS_PROTOCOL_CONN_HPP

#include <srs_core.hpp>

#include <string>
#include <vector>

#include <openssl/err.h>
#include <openssl/ssl.h>

#include <srs_kernel_kbps.hpp>
#include <srs_kernel_resource.hpp>
#include <srs_protocol_io.hpp>
#include <srs_protocol_st.hpp>

class SrsBuffer;

class ISrsConnection : public ISrsResource
{
public:
    ISrsConnection();
    virtual ~ISrsConnection();

public:
    virtual std::string remote_ip() = 0;
};

// The HTTP API uses it to kick off a client.
class ISrsExpire
{
public:
    ISrsExpire();
    virtual ~ISrsExpire();

public:
    virtual void expire() = 0;
};

class SrsTcpConnection : public ISrsProtocolReadWriter
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srs_netfd_t stfd_;
    ISrsProtocolReadWriter *skt_;

public:
    SrsTcpConnection(srs_netfd_t c);
    virtual ~SrsTcpConnection();

public:
    virtual srs_error_t set_tcp_nodelay(bool v);
    virtual srs_error_t set_socket_buffer(srs_utime_t buffer_v);
public:
    virtual void set_recv_timeout(srs_utime_t tm);
    virtual srs_utime_t get_recv_timeout();
    virtual srs_error_t read_fully(void *buf, size_t size, ssize_t *nread);
    virtual int64_t get_recv_bytes();
    virtual int64_t get_send_bytes();
    virtual srs_error_t read(void *buf, size_t size, ssize_t *nread);
    virtual void set_send_timeout(srs_utime_t tm);
    virtual srs_utime_t get_send_timeout();
    virtual srs_error_t write(void *buf, size_t size, ssize_t *nwrite);
    virtual srs_error_t writev(const iovec *iov, int iov_size, ssize_t *nwrite);
};

// Buffers the first bytes, so the protocol can be detected by a peek.
class SrsBufferedReadWriter : public ISrsProtocolReadWriter
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // Owned and freed with this object.
    ISrsProtocolReadWriter *io_;
    // Keep it small enough for every protocol, since a peek fills it fully.
    char cache_[16];
    SrsBuffer *buf_;

public:
    SrsBufferedReadWriter(ISrsProtocolReadWriter *io);
    virtual ~SrsBufferedReadWriter();

public:
    srs_error_t peek(char *buf, int *size);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srs_error_t reload_buffer();
public:
    virtual srs_error_t read(void *buf, size_t size, ssize_t *nread);
    virtual srs_error_t read_fully(void *buf, size_t size, ssize_t *nread);
    virtual void set_recv_timeout(srs_utime_t tm);
    virtual srs_utime_t get_recv_timeout();
    virtual int64_t get_recv_bytes();
    virtual int64_t get_send_bytes();
    virtual void set_send_timeout(srs_utime_t tm);
    virtual srs_utime_t get_send_timeout();
    virtual srs_error_t write(void *buf, size_t size, ssize_t *nwrite);
    virtual srs_error_t writev(const iovec *iov, int iov_size, ssize_t *nwrite);
};

class ISrsSslConnection : public ISrsProtocolReadWriter
{
public:
    ISrsSslConnection();
    virtual ~ISrsSslConnection();

public:
    virtual srs_error_t handshake(std::string key_file, std::string crt_file) = 0;
};

// TLS in server mode.
class SrsSslConnection : public ISrsSslConnection
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
    SrsSslConnection(ISrsProtocolReadWriter *c);
    virtual ~SrsSslConnection();

public:
    virtual srs_error_t handshake(std::string key_file, std::string crt_file);
public:
    virtual void set_recv_timeout(srs_utime_t tm);
    virtual srs_utime_t get_recv_timeout();
    virtual srs_error_t read_fully(void *buf, size_t size, ssize_t *nread);
    virtual int64_t get_recv_bytes();
    virtual int64_t get_send_bytes();
    virtual srs_error_t read(void *buf, size_t size, ssize_t *nread);
    virtual void set_send_timeout(srs_utime_t tm);
    virtual srs_utime_t get_send_timeout();
    virtual srs_error_t write(void *buf, size_t size, ssize_t *nwrite);
    virtual srs_error_t writev(const iovec *iov, int iov_size, ssize_t *nwrite);
};

#endif
