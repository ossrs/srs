//
// Copyright (c) 2013-2025 The SRS Authors
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

// The connection interface for all HTTP/RTMP/RTSP object.
class ISrsConnection : public ISrsResource
{
public:
    ISrsConnection();
    virtual ~ISrsConnection();

public:
    // Get remote ip address.
    virtual std::string remote_ip() = 0;
};

// If a connection is able be expired, user can use HTTP-API to kick-off it.
class ISrsExpire
{
public:
    ISrsExpire();
    virtual ~ISrsExpire();

public:
    // Set connection to expired to kick-off it.
    virtual void expire() = 0;
};

// The basic connection of SRS, for TCP based protocols,
// all connections accept from listener must extends from this base class,
// server will add the connection to manager, and delete it when remove.
class SrsTcpConnection : public ISrsProtocolReadWriter
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // The underlayer st fd handler.
    srs_netfd_t stfd_;
    // The underlayer socket.
    ISrsProtocolReadWriter *skt_;

public:
    SrsTcpConnection(srs_netfd_t c);
    virtual ~SrsTcpConnection();

public:
    // Set socket option TCP_NODELAY.
    virtual srs_error_t set_tcp_nodelay(bool v);
    // Set socket option SO_SNDBUF in srs_utime_t.
    virtual srs_error_t set_socket_buffer(srs_utime_t buffer_v);
    // Interface ISrsProtocolReadWriter
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

// With a small fast read buffer, to support peek for protocol detecting. Note that directly write to io without any
// cache or buffer.
class SrsBufferedReadWriter : public ISrsProtocolReadWriter
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // The under-layer transport.
    ISrsProtocolReadWriter *io_;
    // Fixed, small and fast buffer. Note that it must be very small piece of cache, make sure matches all protocols,
    // because we will full fill it when peeking.
    char cache_[16];
    // Current reading position.
    SrsBuffer *buf_;

public:
    SrsBufferedReadWriter(ISrsProtocolReadWriter *io);
    virtual ~SrsBufferedReadWriter();

public:
    // Peek the head of cache to buf in size of bytes.
    srs_error_t peek(char *buf, int *size);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srs_error_t reload_buffer();
    // Interface ISrsProtocolReadWriter
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

// The interface for SSL connection.
class ISrsSslConnection : public ISrsProtocolReadWriter
{
public:
    ISrsSslConnection();
    virtual ~ISrsSslConnection();

public:
    virtual srs_error_t handshake(std::string key_file, std::string crt_file) = 0;
};

// The SSL connection over TCP transport, in server mode.
class SrsSslConnection : public ISrsSslConnection
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // The under-layer plaintext transport.
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
    // Interface ISrsProtocolReadWriter
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
