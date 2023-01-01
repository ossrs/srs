//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#ifndef SRS_UTEST_PROTOCOL_HPP
#define SRS_UTEST_PROTOCOL_HPP

/*
#include <srs_utest_protocol.hpp>
*/
#include <srs_utest.hpp>

#include <string>
#include <srs_protocol_utility.hpp>

#include <srs_protocol_rtmp_stack.hpp>
#include <srs_protocol_rtmp_handshake.hpp>
#include <srs_protocol_stream.hpp>
#include <srs_protocol_kbps.hpp>

using namespace srs_internal;

#include <srs_protocol_io.hpp>

class MockEmptyIO : public ISrsProtocolReadWriter
{
public:
    MockEmptyIO();
    virtual ~MockEmptyIO();
// for handshake.
public:
    virtual srs_error_t read_fully(void* buf, size_t size, ssize_t* nread);
    virtual srs_error_t write(void* buf, size_t size, ssize_t* nwrite);
// for protocol
public:
    virtual void set_recv_timeout(srs_utime_t tm);
    virtual srs_utime_t get_recv_timeout();
    virtual int64_t get_recv_bytes();
// for protocol
public:
    virtual void set_send_timeout(srs_utime_t tm);
    virtual srs_utime_t get_send_timeout();
    virtual int64_t get_send_bytes();
    virtual srs_error_t writev(const iovec *iov, int iov_size, ssize_t* nwrite);
// for protocol/amf0/msg-codec
public:
    virtual srs_error_t read(void* buf, size_t size, ssize_t* nread);
};

class MockBufferIO : public ISrsProtocolReadWriter
{
public:
    // The send/recv timeout in srs_utime_t.
    srs_utime_t rtm;
    srs_utime_t stm;
    // The send/recv data in bytes.
    int64_t rbytes;
    int64_t sbytes;
    // data source for socket read.
    SrsSimpleStream in_buffer;
    // data buffer for socket send.
    SrsSimpleStream out_buffer;
public:
    // Mock error for io.
    srs_error_t in_err;
    srs_error_t out_err;
public:
    MockBufferIO();
    virtual ~MockBufferIO();
public:
    virtual int length();
    virtual MockBufferIO* append(std::string data);
    virtual MockBufferIO* append(MockBufferIO* data);
    virtual MockBufferIO* append(uint8_t* data, int size);
public:
    virtual int out_length();
    virtual MockBufferIO* out_append(std::string data);
    virtual MockBufferIO* out_append(MockBufferIO* data);
    virtual MockBufferIO* out_append(uint8_t* data, int size);
// for handshake.
public:
    virtual srs_error_t read_fully(void* buf, size_t size, ssize_t* nread);
    virtual srs_error_t write(void* buf, size_t size, ssize_t* nwrite);
// for protocol
public:
    virtual void set_recv_timeout(srs_utime_t tm);
    virtual srs_utime_t get_recv_timeout();
    virtual int64_t get_recv_bytes();
// for protocol
public:
    virtual void set_send_timeout(srs_utime_t tm);
    virtual srs_utime_t get_send_timeout();
    virtual int64_t get_send_bytes();
    virtual srs_error_t writev(const iovec *iov, int iov_size, ssize_t* nwrite);
// for protocol/amf0/msg-codec
public:
    virtual srs_error_t read(void* buf, size_t size, ssize_t* nread);
};

class MockStatistic : public ISrsProtocolStatistic
{
private:
    int64_t in;
    int64_t out;
public:
    MockStatistic();
    virtual ~MockStatistic();
public:
    virtual int64_t get_recv_bytes();
    virtual int64_t get_send_bytes();
public:
    MockStatistic* set_in(int64_t v);
    MockStatistic* set_out(int64_t v);
    MockStatistic* add_in(int64_t v);
    MockStatistic* add_out(int64_t v);
};

class MockWallClock : public SrsWallClock
{
private:
    int64_t clock;
public:
    MockWallClock();
    virtual ~MockWallClock();
public:
    virtual srs_utime_t now();
public:
    virtual MockWallClock* set_clock(srs_utime_t v);
};

#endif

