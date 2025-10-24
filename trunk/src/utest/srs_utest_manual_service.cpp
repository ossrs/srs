//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//
#include <srs_utest_manual_service.hpp>

using namespace std;

#include <srs_app_http_conn.hpp>
#include <srs_app_listener.hpp>
#include <srs_core_deprecated.hpp>
#include <srs_kernel_error.hpp>
#include <srs_protocol_http_stack.hpp>
#include <srs_protocol_st.hpp>
#include <srs_protocol_utility.hpp>

#include <netdb.h>
#include <srs_core_autofree.hpp>
#include <srs_protocol_conn.hpp>
#include <srs_protocol_http_client.hpp>
#include <srs_protocol_http_conn.hpp>
#include <srs_protocol_rtmp_conn.hpp>
#include <srs_protocol_rtmp_stack.hpp>
#include <srs_protocol_st.hpp>
#include <srs_protocol_utility.hpp>
#include <srs_utest_manual_http.hpp>
#include <srs_utest_manual_protocol.hpp>
#include <st.h>
#include <sys/socket.h>

MockSrsConnection::MockSrsConnection()
{
    do_switch = false;
}

MockSrsConnection::~MockSrsConnection()
{
    if (do_switch) {
        srs_usleep(0);
    }
}

const SrsContextId &MockSrsConnection::get_id()
{
    return _srs_context->get_id();
}

std::string MockSrsConnection::desc()
{
    return "Mock";
}

std::string MockSrsConnection::remote_ip()
{
    return "127.0.0.1";
}

VOID TEST(ServiceTimeTest, TimeUnit)
{
    EXPECT_EQ(1000, SRS_UTIME_MILLISECONDS);
    EXPECT_EQ(1000 * 1000, SRS_UTIME_SECONDS);
    EXPECT_EQ(60 * 1000 * 1000, SRS_UTIME_MINUTES);
    EXPECT_EQ(3600 * 1000 * 1000LL, SRS_UTIME_HOURS);

    EXPECT_TRUE(srs_is_never_timeout(SRS_UTIME_NO_TIMEOUT));
    EXPECT_FALSE(srs_is_never_timeout(0));
}

MockTcpHandler::MockTcpHandler()
{
    fd = NULL;
}

MockTcpHandler::~MockTcpHandler()
{
    srs_close_stfd(fd);
}

srs_error_t MockTcpHandler::on_tcp_client(ISrsListener *listener, srs_netfd_t stfd)
{
    fd = stfd;
    return srs_success;
}

VOID TEST(TCPServerTest, PingPong)
{
    srs_error_t err;
    if (true) {
        MockTcpHandler h;
        SrsTcpListener l(&h);
        l.set_endpoint(_srs_tmp_host, _srs_tmp_port);

        HELPER_EXPECT_SUCCESS(l.listen());
        EXPECT_TRUE(srs_netfd_fileno(l.lfd_) > 0);
    }

    if (true) {
        MockTcpHandler h;
        SrsTcpListener l(&h);
        l.set_endpoint(_srs_tmp_host, _srs_tmp_port);
        HELPER_EXPECT_SUCCESS(l.listen());
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);

        SrsTcpClient c(_srs_tmp_host, _srs_tmp_port, _srs_tmp_timeout);
        HELPER_EXPECT_SUCCESS(c.connect());

        srs_usleep(1 * SRS_UTIME_MILLISECONDS);
        EXPECT_TRUE(h.fd != NULL);
    }

    if (true) {
        MockTcpHandler h;
        SrsTcpListener l(&h);
        l.set_endpoint(_srs_tmp_host, _srs_tmp_port);
        HELPER_EXPECT_SUCCESS(l.listen());
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);

        SrsTcpClient c(_srs_tmp_host, _srs_tmp_port, _srs_tmp_timeout);
        HELPER_EXPECT_SUCCESS(c.connect());

        srs_usleep(1 * SRS_UTIME_MILLISECONDS);
#ifdef SRS_OSX
        ASSERT_TRUE(h.fd != NULL);
#endif
        SrsStSocket skt(h.fd);

        HELPER_EXPECT_SUCCESS(c.write((void *)"Hello", 5, NULL));

        char buf[16] = {0};
        HELPER_EXPECT_SUCCESS(skt.read(buf, 5, NULL));
        EXPECT_STREQ(buf, "Hello");
    }

    if (true) {
        MockTcpHandler h;
        SrsTcpListener l(&h);
        l.set_endpoint(_srs_tmp_host, _srs_tmp_port);
        HELPER_EXPECT_SUCCESS(l.listen());
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);

        SrsTcpClient c(_srs_tmp_host, _srs_tmp_port, _srs_tmp_timeout);
        HELPER_EXPECT_SUCCESS(c.connect());

        srs_usleep(1 * SRS_UTIME_MILLISECONDS);
#ifdef SRS_OSX
        ASSERT_TRUE(h.fd != NULL);
#endif
        SrsStSocket skt(h.fd);

        HELPER_EXPECT_SUCCESS(c.write((void *)"Hello", 5, NULL));
        HELPER_EXPECT_SUCCESS(c.write((void *)" ", 1, NULL));
        HELPER_EXPECT_SUCCESS(c.write((void *)"SRS", 3, NULL));

        char buf[16] = {0};
        HELPER_EXPECT_SUCCESS(skt.read_fully(buf, 9, NULL));
        EXPECT_STREQ(buf, "Hello SRS");
    }

    if (true) {
        MockTcpHandler h;
        SrsTcpListener l(&h);
        l.set_endpoint(_srs_tmp_host, _srs_tmp_port);
        HELPER_EXPECT_SUCCESS(l.listen());
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);

        SrsTcpClient c(_srs_tmp_host, _srs_tmp_port, _srs_tmp_timeout);
        HELPER_EXPECT_SUCCESS(c.connect());

        srs_usleep(1 * SRS_UTIME_MILLISECONDS);
#ifdef SRS_OSX
        ASSERT_TRUE(h.fd != NULL);
#endif
        SrsStSocket skt(h.fd);

        HELPER_EXPECT_SUCCESS(c.write((void *)"Hello SRS", 9, NULL));
        EXPECT_EQ(9, c.get_send_bytes());
        EXPECT_EQ(0, c.get_recv_bytes());
        EXPECT_TRUE(SRS_UTIME_NO_TIMEOUT == c.get_send_timeout());
        EXPECT_TRUE(SRS_UTIME_NO_TIMEOUT == c.get_recv_timeout());

        char buf[16] = {0};
        HELPER_EXPECT_SUCCESS(skt.read(buf, 9, NULL));
        EXPECT_STREQ(buf, "Hello SRS");
        EXPECT_EQ(0, skt.get_send_bytes());
        EXPECT_EQ(9, skt.get_recv_bytes());
        EXPECT_TRUE(SRS_UTIME_NO_TIMEOUT == skt.get_send_timeout());
        EXPECT_TRUE(SRS_UTIME_NO_TIMEOUT == skt.get_recv_timeout());
    }
}

VOID TEST(TCPServerTest, PingPongWithTimeout)
{
    srs_error_t err;

    if (true) {
        MockTcpHandler h;
        SrsTcpListener l(&h);
        l.set_endpoint(_srs_tmp_host, _srs_tmp_port);
        HELPER_EXPECT_SUCCESS(l.listen());
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);

        SrsTcpClient c(_srs_tmp_host, _srs_tmp_port, _srs_tmp_timeout);
        HELPER_EXPECT_SUCCESS(c.connect());

        srs_usleep(1 * SRS_UTIME_MILLISECONDS);
#ifdef SRS_OSX
        ASSERT_TRUE(h.fd != NULL);
#endif
        SrsStSocket skt(h.fd);
        skt.set_recv_timeout(1 * SRS_UTIME_MILLISECONDS);

        char buf[16] = {0};
        HELPER_EXPECT_FAILED(skt.read(buf, 9, NULL));
        EXPECT_TRUE(SRS_UTIME_NO_TIMEOUT == skt.get_send_timeout());
        EXPECT_TRUE(1 * SRS_UTIME_MILLISECONDS == skt.get_recv_timeout());
    }

    if (true) {
        MockTcpHandler h;
        SrsTcpListener l(&h);
        l.set_endpoint(_srs_tmp_host, _srs_tmp_port);
        HELPER_EXPECT_SUCCESS(l.listen());
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);

        SrsTcpClient c(_srs_tmp_host, _srs_tmp_port, _srs_tmp_timeout);
        HELPER_EXPECT_SUCCESS(c.connect());

        srs_usleep(1 * SRS_UTIME_MILLISECONDS);
#ifdef SRS_OSX
        ASSERT_TRUE(h.fd != NULL);
#endif
        SrsStSocket skt(h.fd);
        skt.set_recv_timeout(1 * SRS_UTIME_MILLISECONDS);

        char buf[16] = {0};
        HELPER_EXPECT_FAILED(skt.read_fully(buf, 9, NULL));
        EXPECT_TRUE(SRS_UTIME_NO_TIMEOUT == skt.get_send_timeout());
        EXPECT_TRUE(1 * SRS_UTIME_MILLISECONDS == skt.get_recv_timeout());
    }

    if (true) {
        MockTcpHandler h;
        SrsTcpListener l(&h);
        l.set_endpoint(_srs_tmp_host, _srs_tmp_port);
        HELPER_EXPECT_SUCCESS(l.listen());
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);

        SrsTcpClient c(_srs_tmp_host, _srs_tmp_port, _srs_tmp_timeout);
        HELPER_EXPECT_SUCCESS(c.connect());

        srs_usleep(1 * SRS_UTIME_MILLISECONDS);
#ifdef SRS_OSX
        ASSERT_TRUE(h.fd != NULL);
#endif
        SrsStSocket skt(h.fd);
        skt.set_recv_timeout(1 * SRS_UTIME_MILLISECONDS);

        HELPER_EXPECT_SUCCESS(c.write((void *)"Hello", 5, NULL));

        char buf[16] = {0};
        HELPER_EXPECT_FAILED(skt.read_fully(buf, 9, NULL));
        EXPECT_TRUE(SRS_UTIME_NO_TIMEOUT == skt.get_send_timeout());
        EXPECT_TRUE(1 * SRS_UTIME_MILLISECONDS == skt.get_recv_timeout());
    }
}

VOID TEST(TCPServerTest, StringIsDigital)
{
    EXPECT_EQ(0, ::atoi("0"));
    EXPECT_EQ(0, ::atoi("0000000000"));
    EXPECT_EQ(1, ::atoi("01"));
    EXPECT_EQ(12, ::atoi("012"));
    EXPECT_EQ(1234567890L, ::atol("1234567890"));
    EXPECT_EQ(123456789L, ::atol("0123456789"));
    EXPECT_EQ(1234567890, ::atoi("1234567890a"));
    EXPECT_EQ(10, ::atoi("10e3"));
    EXPECT_EQ(0, ::atoi("!1234567890"));
    EXPECT_EQ(0, ::atoi(""));

    EXPECT_TRUE(srs_is_digit_number("0"));
    EXPECT_TRUE(srs_is_digit_number("0000000000"));
    EXPECT_TRUE(srs_is_digit_number("1234567890"));
    EXPECT_TRUE(srs_is_digit_number("0123456789"));
    EXPECT_FALSE(srs_is_digit_number("1234567890a"));
    EXPECT_FALSE(srs_is_digit_number("a1234567890"));
    EXPECT_FALSE(srs_is_digit_number("10e3"));
    EXPECT_FALSE(srs_is_digit_number("!1234567890"));
    EXPECT_FALSE(srs_is_digit_number(""));
}

VOID TEST(TCPServerTest, StringIsHex)
{
    if (true) {
        char *str = (char *)"0";
        char *parsed = str;
        errno = 0;
        EXPECT_EQ(0x0, ::strtol(str, &parsed, 16));
        EXPECT_EQ(0, errno);
        EXPECT_EQ(str + 1, parsed);
    }

    if (true) {
        char *str = (char *)"0";
        char *parsed = str;
        errno = 0;
        EXPECT_EQ(0x0, ::strtol(str, &parsed, 16));
        EXPECT_EQ(0, errno);
        EXPECT_EQ(str + 1, parsed);
    }

    if (true) {
        char *str = (char *)"0000000000";
        char *parsed = str;
        errno = 0;
        EXPECT_EQ(0x0, ::strtol(str, &parsed, 16));
        EXPECT_EQ(0, errno);
        EXPECT_EQ(str + 10, parsed);
    }

    if (true) {
        char *str = (char *)"01";
        char *parsed = str;
        errno = 0;
        EXPECT_EQ(0x1, ::strtol(str, &parsed, 16));
        EXPECT_EQ(0, errno);
        EXPECT_EQ(str + 2, parsed);
    }

    if (true) {
        char *str = (char *)"012";
        char *parsed = str;
        errno = 0;
        EXPECT_EQ(0x12, ::strtol(str, &parsed, 16));
        EXPECT_EQ(0, errno);
        EXPECT_EQ(str + 3, parsed);
    }

    if (true) {
        char *str = (char *)"1234567890";
        char *parsed = str;
        errno = 0;
        EXPECT_EQ(0x1234567890L, ::strtol(str, &parsed, 16));
        EXPECT_EQ(0, errno);
        EXPECT_EQ(str + 10, parsed);
    }

    if (true) {
        char *str = (char *)"0123456789";
        char *parsed = str;
        errno = 0;
        EXPECT_EQ(0x123456789L, ::strtol(str, &parsed, 16));
        EXPECT_EQ(0, errno);
        EXPECT_EQ(str + 10, parsed);
    }

    if (true) {
        char *str = (char *)"1234567890a";
        char *parsed = str;
        errno = 0;
        EXPECT_EQ(0x1234567890a, ::strtol(str, &parsed, 16));
        EXPECT_EQ(0, errno);
        EXPECT_EQ(str + 11, parsed);
    }

    if (true) {
        char *str = (char *)"0x1234567890a";
        char *parsed = str;
        errno = 0;
        EXPECT_EQ(0x1234567890a, ::strtol(str, &parsed, 16));
        EXPECT_EQ(0, errno);
        EXPECT_EQ(str + 13, parsed);
    }

    if (true) {
        char *str = (char *)"1234567890f";
        char *parsed = str;
        errno = 0;
        EXPECT_EQ(0x1234567890f, ::strtol(str, &parsed, 16));
        EXPECT_EQ(0, errno);
        EXPECT_EQ(str + 11, parsed);
    }

    if (true) {
        char *str = (char *)"10e3";
        char *parsed = str;
        errno = 0;
        EXPECT_EQ(0x10e3, ::strtol(str, &parsed, 16));
        EXPECT_EQ(0, errno);
        EXPECT_EQ(str + 4, parsed);
    }

    if (true) {
        char *str = (char *)"!1234567890";
        char *parsed = str;
        errno = 0;
        EXPECT_EQ(0x0, ::strtol(str, &parsed, 16));
#ifndef SRS_OSX
        EXPECT_EQ(0, errno);
#endif
        EXPECT_EQ(str, parsed);
    }

    if (true) {
        char *str = (char *)"1234567890g";
        char *parsed = str;
        errno = 0;
        EXPECT_EQ(0x1234567890, ::strtol(str, &parsed, 16));
        EXPECT_EQ(0, errno);
        EXPECT_EQ(str + 10, parsed);
    }

    if (true) {
        char *str = (char *)"";
        char *parsed = str;
        errno = 0;
        EXPECT_EQ(0x0, ::strtol(str, &parsed, 16));
#ifndef SRS_OSX
        EXPECT_EQ(0, errno);
#endif
        EXPECT_EQ(str, parsed);
    }

    if (true) {
        char *str = (char *)"1fffffffffffffffffffffffffffff";
        char *parsed = str;
        errno = 0;
        EXPECT_EQ(0x7fffffffffffffff, ::strtol(str, &parsed, 16));
        EXPECT_NE(0, errno);
        EXPECT_EQ(str + 30, parsed);
    }
}

VOID TEST(TCPServerTest, WritevIOVC)
{
    srs_error_t err;

    if (true) {
        MockTcpHandler h;
        SrsTcpListener l(&h);
        l.set_endpoint(_srs_tmp_host, _srs_tmp_port);
        HELPER_EXPECT_SUCCESS(l.listen());

        SrsTcpClient c(_srs_tmp_host, _srs_tmp_port, _srs_tmp_timeout);
        HELPER_EXPECT_SUCCESS(c.connect());

        srs_usleep(1 * SRS_UTIME_MILLISECONDS);
#ifdef SRS_OSX
        ASSERT_TRUE(h.fd != NULL);
#endif
        SrsStSocket skt(h.fd);

        iovec iovs[3];
        iovs[0].iov_base = (void *)"H";
        iovs[0].iov_len = 1;
        iovs[1].iov_base = (void *)"e";
        iovs[1].iov_len = 1;
        iovs[2].iov_base = (void *)"llo";
        iovs[2].iov_len = 3;

        HELPER_EXPECT_SUCCESS(c.writev(iovs, 3, NULL));

        char buf[16] = {0};
        HELPER_EXPECT_SUCCESS(skt.read(buf, 5, NULL));
        EXPECT_STREQ(buf, "Hello");
    }

    if (true) {
        MockTcpHandler h;
        SrsTcpListener l(&h);
        l.set_endpoint(_srs_tmp_host, _srs_tmp_port);
        HELPER_EXPECT_SUCCESS(l.listen());

        SrsTcpClient c(_srs_tmp_host, _srs_tmp_port, _srs_tmp_timeout);
        HELPER_EXPECT_SUCCESS(c.connect());

        srs_usleep(1 * SRS_UTIME_MILLISECONDS);
#ifdef SRS_OSX
        ASSERT_TRUE(h.fd != NULL);
#endif
        SrsStSocket skt(h.fd);

        iovec iovs[3];
        iovs[0].iov_base = (void *)"H";
        iovs[0].iov_len = 1;
        iovs[1].iov_base = (void *)NULL;
        iovs[1].iov_len = 0;
        iovs[2].iov_base = (void *)"llo";
        iovs[2].iov_len = 3;

        HELPER_EXPECT_SUCCESS(c.writev(iovs, 3, NULL));

        char buf[16] = {0};
        HELPER_EXPECT_SUCCESS(skt.read(buf, 4, NULL));
        EXPECT_STREQ(buf, "Hllo");
    }
}

VOID TEST(HTTPServerTest, MessageConnection)
{
    srs_error_t err;

    if (true) {
        MockSrsConnection conn;
        SrsHttpMessage m;
        m.set_connection(&conn);
        EXPECT_TRUE(&conn == m.connection());
        EXPECT_FALSE(m.is_jsonp());
    }

    if (true) {
        SrsHttpMessage m;
        HELPER_EXPECT_SUCCESS(m.set_url("http://127.0.0.1/live/livestream.flv?callback=fn&method=POST", true));
        EXPECT_TRUE(m.jsonp_);
        EXPECT_STREQ("POST", m.jsonp_method_.c_str());
        EXPECT_TRUE(m.is_jsonp());
    }

    if (true) {
        SrsHttpMessage m;
        HELPER_EXPECT_SUCCESS(m.set_url("http://127.0.0.1/live/livestream.flv?callback=fn&method=GET", true));
        EXPECT_EQ(SRS_CONSTS_HTTP_GET, m.method());
        EXPECT_STREQ("GET", m.method_str().c_str());
        EXPECT_TRUE(m.is_jsonp());
    }

    if (true) {
        SrsHttpMessage m;
        HELPER_EXPECT_SUCCESS(m.set_url("http://127.0.0.1/live/livestream.flv?callback=fn&method=PUT", true));
        EXPECT_EQ(SRS_CONSTS_HTTP_PUT, m.method());
        EXPECT_STREQ("PUT", m.method_str().c_str());
        EXPECT_TRUE(m.is_jsonp());
    }

    if (true) {
        SrsHttpMessage m;
        HELPER_EXPECT_SUCCESS(m.set_url("http://127.0.0.1/live/livestream.flv?callback=fn&method=POST", true));
        EXPECT_EQ(SRS_CONSTS_HTTP_POST, m.method());
        EXPECT_STREQ("POST", m.method_str().c_str());
        EXPECT_TRUE(m.is_jsonp());
    }

    if (true) {
        SrsHttpMessage m;
        HELPER_EXPECT_SUCCESS(m.set_url("http://127.0.0.1/live/livestream.flv?callback=fn&method=DELETE", true));
        EXPECT_EQ(SRS_CONSTS_HTTP_DELETE, m.method());
        EXPECT_STREQ("DELETE", m.method_str().c_str());
        EXPECT_TRUE(m.is_jsonp());
    }

    if (true) {
        SrsHttpMessage m;
        m.set_basic(HTTP_REQUEST, SRS_CONSTS_HTTP_GET, (llhttp_status_t)0, 0);
        EXPECT_EQ(SRS_CONSTS_HTTP_GET, m.method());
        EXPECT_STREQ("GET", m.method_str().c_str());
        m.set_basic(HTTP_REQUEST, SRS_CONSTS_HTTP_PUT, (llhttp_status_t)0, 0);
        EXPECT_EQ(SRS_CONSTS_HTTP_PUT, m.method());
        EXPECT_STREQ("PUT", m.method_str().c_str());
        m.set_basic(HTTP_REQUEST, SRS_CONSTS_HTTP_POST, (llhttp_status_t)0, 0);
        EXPECT_EQ(SRS_CONSTS_HTTP_POST, m.method());
        EXPECT_STREQ("POST", m.method_str().c_str());
        m.set_basic(HTTP_REQUEST, SRS_CONSTS_HTTP_DELETE, (llhttp_status_t)0, 0);
        EXPECT_EQ(SRS_CONSTS_HTTP_DELETE, m.method());
        EXPECT_STREQ("DELETE", m.method_str().c_str());
        m.set_basic(HTTP_REQUEST, SRS_CONSTS_HTTP_OPTIONS, (llhttp_status_t)0, 0);
        EXPECT_EQ(SRS_CONSTS_HTTP_OPTIONS, m.method());
        EXPECT_STREQ("OPTIONS", m.method_str().c_str());
    }

    if (true) {
        SrsHttpMessage m;
        EXPECT_TRUE(m.is_keep_alive());
    }

    if (true) {
        SrsHttpMessage m;
        HELPER_EXPECT_SUCCESS(m.set_url("http://127.0.0.1/live/livestream.flv", false));
        EXPECT_STREQ("http://127.0.0.1/live/livestream.flv", m.uri().c_str());
        EXPECT_FALSE(m.is_jsonp());
    }

    if (true) {
        SrsHttpMessage m;
        HELPER_EXPECT_SUCCESS(m.set_url("http://127.0.0.1/live/livestream.flv?domain=ossrs.net", false));
        EXPECT_STREQ("ossrs.net", m.host().c_str());
        EXPECT_FALSE(m.is_jsonp());
    }

    if (true) {
        SrsHttpMessage m;
        HELPER_EXPECT_SUCCESS(m.set_url("http://127.0.0.1/live/livestream.flv?vhost=ossrs.net", false));
        EXPECT_STREQ("ossrs.net", m.host().c_str());
        EXPECT_FALSE(m.is_jsonp());
    }

    if (true) {
        SrsHttpMessage m;
        HELPER_EXPECT_SUCCESS(m.set_url("http://127.0.0.1/live/livestream.flv?domain=ossrs.net&token=xxx", false));
        EXPECT_STREQ("ossrs.net", m.host().c_str());
        EXPECT_FALSE(m.is_jsonp());
    }

    if (true) {
        SrsHttpMessage m;
        HELPER_EXPECT_SUCCESS(m.set_url("http://127.0.0.1/live/livestream.flv?token=xxx&domain=ossrs.net", false));
        EXPECT_STREQ("ossrs.net", m.host().c_str());
        EXPECT_FALSE(m.is_jsonp());
    }

    if (true) {
        SrsHttpMessage m;
        HELPER_EXPECT_SUCCESS(m.set_url("http://127.0.0.1/live/livestream.flv", false));
        EXPECT_STREQ(".flv", m.ext().c_str());
        EXPECT_FALSE(m.is_jsonp());
    }

    if (true) {
        SrsHttpMessage m;
        HELPER_EXPECT_SUCCESS(m.set_url("http://127.0.0.1/v1/streams/100", false));
        EXPECT_STREQ("100", m.parse_rest_id("/v1/streams/").c_str());
        EXPECT_FALSE(m.is_jsonp());
    }

    if (true) {
        SrsHttpMessage m;
        HELPER_EXPECT_SUCCESS(m.set_url("http://127.0.0.1/v1/streams/abc", false));
        EXPECT_STREQ("abc", m.parse_rest_id("/v1/streams/").c_str());
        EXPECT_FALSE(m.is_jsonp());
    }
}

VOID TEST(HTTPServerTest, MessageTurnRequest)
{
    srs_error_t err;

    if (true) {
        SrsHttpMessage m;
        HELPER_ASSERT_SUCCESS(m.set_url("http://127.0.0.1/live/livestream.flv", false));
        ISrsRequest *r = m.to_request("ossrs.net");
        EXPECT_STREQ("live", r->app_.c_str());
        EXPECT_STREQ("livestream", r->stream_.c_str());
        EXPECT_STREQ("rtmp://ossrs.net/live", r->tcUrl_.c_str());
        srs_freep(r);
    }

    if (true) {
        SrsHttpMessage m;
        HELPER_ASSERT_SUCCESS(m.set_url("http://127.0.0.1/live/livestream.flv?token=key", false));
        ISrsRequest *r = m.to_request("ossrs.net");
        EXPECT_STREQ("rtmp://ossrs.net/live", r->tcUrl_.c_str());
        EXPECT_STREQ("?token=key", r->param_.c_str());
        srs_freep(r);
    }

    if (true) {
        MockSrsConnection conn;
        SrsHttpMessage m;
        m.set_connection(&conn);

        ISrsRequest *r = m.to_request("ossrs.net");
        EXPECT_STREQ("127.0.0.1", r->ip_.c_str());
        srs_freep(r);
    }

    if (true) {
        MockSrsConnection conn;
        SrsHttpMessage m;
        m.set_connection(&conn);

        SrsHttpHeader hdr;
        hdr.set("X-Real-IP", "10.11.12.13");
        m.set_header(&hdr, false);

        ISrsRequest *r = m.to_request("ossrs.net");
        EXPECT_STREQ("10.11.12.13", r->ip_.c_str());
        srs_freep(r);
    }
}

VOID TEST(HTTPServerTest, ContentLength)
{
    srs_error_t err;

    if (true) {
        MockBufferIO io;
        io.append("HTTP/1.1 200 OK\r\nContent-Length: 11\r\n\r\n");

        SrsHttpParser hp;
        HELPER_ASSERT_SUCCESS(hp.initialize(HTTP_RESPONSE));
        ISrsHttpMessage *msg_raw = NULL;
        HELPER_ASSERT_SUCCESS(hp.parse_message(&io, &msg_raw));
        SrsUniquePtr<ISrsHttpMessage> msg(msg_raw);

        char buf[32];
        ssize_t nread = 0;
        ISrsHttpResponseReader *r = msg->body_reader();

        io.append("Hello");
        HELPER_ARRAY_INIT(buf, sizeof(buf), 0);
        HELPER_ASSERT_SUCCESS(r->read(buf, 5, &nread));
        EXPECT_EQ(5, nread);
        EXPECT_STREQ("Hello", buf);

        io.append("World!");
        HELPER_ARRAY_INIT(buf, sizeof(buf), 0);
        HELPER_ASSERT_SUCCESS(r->read(buf, 6, &nread));
        EXPECT_EQ(6, nread);
        EXPECT_STREQ("World!", buf);
    }
}

VOID TEST(HTTPServerTest, HTTPChunked)
{
    srs_error_t err;

    if (true) {
        MockBufferIO io;
        io.append("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n");

        SrsHttpParser hp;
        HELPER_ASSERT_SUCCESS(hp.initialize(HTTP_RESPONSE));
        ISrsHttpMessage *msg_raw = NULL;
        HELPER_ASSERT_SUCCESS(hp.parse_message(&io, &msg_raw));
        SrsUniquePtr<ISrsHttpMessage> msg(msg_raw);

        char buf[32];
        ssize_t nread = 0;
        ISrsHttpResponseReader *r = msg->body_reader();

        io.append("5\r\nHello\r\n");
        HELPER_ARRAY_INIT(buf, sizeof(buf), 0);
        HELPER_ASSERT_SUCCESS(r->read(buf, 5, &nread));
        EXPECT_EQ(5, nread);
        EXPECT_STREQ("Hello", buf);

        io.append("6\r\nWorld!\r\n");
        HELPER_ARRAY_INIT(buf, sizeof(buf), 0);
        HELPER_ASSERT_SUCCESS(r->read(buf, 6, &nread));
        EXPECT_EQ(6, nread);
        EXPECT_STREQ("World!", buf);
    }
}

VOID TEST(HTTPServerTest, InfiniteChunked)
{
    srs_error_t err;

    // For infinite chunked mode, all data is content.
    if (true) {
        MockBufferIO io;
        io.append("HTTP/1.1 200 OK\r\n\r\n");

        SrsHttpParser hp;
        HELPER_ASSERT_SUCCESS(hp.initialize(HTTP_RESPONSE));
        ISrsHttpMessage *msg_raw = NULL;
        HELPER_ASSERT_SUCCESS(hp.parse_message(&io, &msg_raw));
        SrsUniquePtr<ISrsHttpMessage> msg(msg_raw);

        char buf[32];
        ssize_t nread = 0;
        ISrsHttpResponseReader *r = msg->body_reader();

        io.append("Hello");
        HELPER_ARRAY_INIT(buf, sizeof(buf), 0);
        HELPER_ASSERT_SUCCESS(r->read(buf, 5, &nread));
        EXPECT_EQ(5, nread);
        EXPECT_STREQ("Hello", buf);

        io.append("\r\nWorld!");
        HELPER_ARRAY_INIT(buf, sizeof(buf), 0);
        HELPER_ASSERT_SUCCESS(r->read(buf, 8, &nread));
        EXPECT_EQ(8, nread);
        EXPECT_STREQ("\r\nWorld!", buf);

        EXPECT_FALSE(r->eof());
    }

    // If read error, it's EOF.
    if (true) {
        MockBufferIO io;
        io.append("HTTP/1.1 200 OK\r\n\r\n");

        SrsHttpParser hp;
        HELPER_ASSERT_SUCCESS(hp.initialize(HTTP_RESPONSE));
        ISrsHttpMessage *msg_raw = NULL;
        HELPER_ASSERT_SUCCESS(hp.parse_message(&io, &msg_raw));
        SrsUniquePtr<ISrsHttpMessage> msg(msg_raw);

        char buf[32];
        ssize_t nread = 0;
        ISrsHttpResponseReader *r = msg->body_reader();

        io.append("Hello");
        HELPER_ARRAY_INIT(buf, sizeof(buf), 0);
        HELPER_ASSERT_SUCCESS(r->read(buf, 10, &nread));
        EXPECT_EQ(5, nread);
        EXPECT_STREQ("Hello", buf);

        io.in_err = srs_error_new(ERROR_SOCKET_READ, "EOF");
        HELPER_ASSERT_SUCCESS(r->read(buf, 10, &nread));
        EXPECT_TRUE(r->eof());
    }
}

VOID TEST(HTTPServerTest, OPTIONSRead)
{
    srs_error_t err;

    // If request, it has no content-length, not chunked, it's not infinite chunked,
    // actually, it has no body.
    if (true) {
        MockBufferIO io;
        io.append("OPTIONS /rtc/v1/play HTTP/1.1\r\n\r\n");

        SrsHttpParser hp;
        HELPER_ASSERT_SUCCESS(hp.initialize(HTTP_REQUEST));
        ISrsHttpMessage *req_raw = NULL;
        HELPER_ASSERT_SUCCESS(hp.parse_message(&io, &req_raw));
        SrsUniquePtr<ISrsHttpMessage> req(req_raw);

        ISrsHttpResponseReader *br = req->body_reader();
        EXPECT_TRUE(br->eof());
    }

    // If response, it has no content-length, not chunked, it's infinite chunked,
    if (true) {
        MockBufferIO io;
        io.append("HTTP/1.1 200 OK\r\n\r\n");

        SrsHttpParser hp;
        HELPER_ASSERT_SUCCESS(hp.initialize(HTTP_RESPONSE));
        ISrsHttpMessage *req_raw = NULL;
        HELPER_ASSERT_SUCCESS(hp.parse_message(&io, &req_raw));
        SrsUniquePtr<ISrsHttpMessage> req(req_raw);

        ISrsHttpResponseReader *br = req->body_reader();
        EXPECT_FALSE(br->eof());
    }

    // So if OPTIONS has body, with chunked or content-length, it's ok to parsing it.
    if (true) {
        MockBufferIO io;
        io.append("OPTIONS /rtc/v1/play HTTP/1.1\r\nContent-Length: 5\r\n\r\nHello");

        SrsHttpParser hp;
        HELPER_ASSERT_SUCCESS(hp.initialize(HTTP_REQUEST));
        ISrsHttpMessage *req_raw = NULL;
        HELPER_ASSERT_SUCCESS(hp.parse_message(&io, &req_raw));
        SrsUniquePtr<ISrsHttpMessage> req(req_raw);

        ISrsHttpResponseReader *br = req->body_reader();
        EXPECT_FALSE(br->eof());

        string b;
        HELPER_ASSERT_SUCCESS(req->body_read_all(b));
        EXPECT_STREQ("Hello", b.c_str());

        // The body will use as next HTTP request message.
        io.append("GET /rtc/v1/play HTTP/1.1\r\n\r\n");
        ISrsHttpMessage *req2_raw = NULL;
        HELPER_ASSERT_SUCCESS(hp.parse_message(&io, &req2_raw));
        SrsUniquePtr<ISrsHttpMessage> req2(req2_raw);
    }

    // So if OPTIONS has body, but not specified the size, we think it has no body,
    // and the body is parsed fail as the next parsing.
    if (true) {
        MockBufferIO io;
        io.append("OPTIONS /rtc/v1/play HTTP/1.1\r\n\r\n");

        SrsHttpParser hp;
        HELPER_ASSERT_SUCCESS(hp.initialize(HTTP_REQUEST));
        ISrsHttpMessage *req_raw = NULL;
        HELPER_ASSERT_SUCCESS(hp.parse_message(&io, &req_raw));
        SrsUniquePtr<ISrsHttpMessage> req(req_raw);

        ISrsHttpResponseReader *br = req->body_reader();
        EXPECT_TRUE(br->eof());

        // The body will use as next HTTP request message.
        io.append("Hello");
        ISrsHttpMessage *req2_raw = NULL;
        HELPER_ASSERT_FAILED(hp.parse_message(&io, &req2_raw));
        SrsUniquePtr<ISrsHttpMessage> req2(req2_raw);
    }
}

VOID TEST(HTTPServerTest, MessageWritev)
{
    srs_error_t err;

    // Directly writev, merge to one chunk.
    if (true) {
        MockResponseWriter w;
        w.write_header(SRS_CONSTS_HTTP_OK);

        iovec iovs[] = {
            {(char *)"Hello", 5},
            {(char *)"World", 5},
            {(char *)"!", 1},
        };
        HELPER_ASSERT_SUCCESS(w.writev(iovs, 3, NULL));

        __MOCK_HTTP_EXPECT_STREQ2(200, "b\r\nHelloWorld!\r\n", w);
    }

    // Use writev to send one iov, should also be ok.
    if (true) {
        MockResponseWriter w;

        char data[] = "Hello, world!";
        iovec iovs[] = {{(char *)data, (int)(sizeof(data) - 1)}};
        HELPER_ASSERT_SUCCESS(w.writev(iovs, 1, NULL));

        __MOCK_HTTP_EXPECT_STREQ(200, "Hello, world!", w);
    }

    // Write header multiple times, should be ok.
    if (true) {
        MockResponseWriter w;
        w.write_header(SRS_CONSTS_HTTP_OK);
        w.write_header(SRS_CONSTS_HTTP_OK);
    }
}

VOID TEST(TCPServerTest, TCPListen)
{
    srs_error_t err;

    // Failed for invalid ip.
    if (true) {
        srs_netfd_t pfd = NULL;
        HELPER_EXPECT_FAILED(srs_tcp_listen("10.0.0.abc", 1935, &pfd));
        srs_close_stfd(pfd);
    }

    // If listen multiple times, should success for we already set the REUSEPORT.
    if (true) {
        srs_netfd_t pfd = NULL;
        HELPER_ASSERT_SUCCESS(srs_tcp_listen("127.0.0.1", 1935, &pfd));

        srs_netfd_t pfd2 = NULL;
        srs_error_t err2 = srs_tcp_listen("127.0.0.1", 1935, &pfd2);

        srs_close_stfd(pfd);
        srs_close_stfd(pfd2);
#ifdef SRS_CYGWIN64
        // Should failed because cygwin does not support REUSE_PORT.
        HELPER_EXPECT_FAILED(err2);
#else
        HELPER_EXPECT_SUCCESS(err2);
#endif
    }

    // Typical listen.
    if (true) {
        srs_netfd_t pfd = NULL;
        HELPER_ASSERT_SUCCESS(srs_tcp_listen("127.0.0.1", 1935, &pfd));
        srs_close_stfd(pfd);
    }
}

VOID TEST(TCPServerTest, UDPListen)
{
    srs_error_t err;

    // Failed for invalid ip.
    if (true) {
        srs_netfd_t pfd = NULL;
        HELPER_EXPECT_FAILED(srs_udp_listen("10.0.0.abc", 1935, &pfd));
        srs_close_stfd(pfd);
    }

    // If listen multiple times, should success for we already set the REUSEPORT.
    if (true) {
        srs_netfd_t pfd = NULL;
        HELPER_ASSERT_SUCCESS(srs_udp_listen("127.0.0.1", 1935, &pfd));

        srs_netfd_t pfd2 = NULL;
        srs_error_t err2 = srs_udp_listen("127.0.0.1", 1935, &pfd2);

        srs_close_stfd(pfd);
        srs_close_stfd(pfd2);
        HELPER_EXPECT_SUCCESS(err2);
    }

    // Typical listen.
    if (true) {
        srs_netfd_t pfd = NULL;
        HELPER_ASSERT_SUCCESS(srs_udp_listen("127.0.0.1", 1935, &pfd));
        srs_close_stfd(pfd);
    }
}

MockOnCycleThread::MockOnCycleThread() : trd("mock", this)
{
    cond = srs_cond_new();
}

MockOnCycleThread::~MockOnCycleThread()
{
    srs_cond_destroy(cond);
}

srs_error_t MockOnCycleThread::cycle()
{
    srs_error_t err = srs_success;

    for (;;) {
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);
        srs_cond_signal(cond);
        // If no one waiting on the cond, directly return event signal more than one time.
        // If someone waiting, signal them more than one time.
        srs_cond_signal(cond);

        if ((err = trd.pull()) != srs_success) {
            return err;
        }
    }

    return err;
}

srs_error_t MockOnCycleThread::start()
{
    return trd.start();
}

void MockOnCycleThread::stop()
{
    trd.stop();
}

VOID TEST(TCPServerTest, ThreadCondWait)
{
    MockOnCycleThread trd;
    trd.trd.start();

    srs_usleep(1 * SRS_UTIME_MILLISECONDS);
    srs_cond_wait(trd.cond);
    trd.trd.stop();
}

MockOnCycleThread2::MockOnCycleThread2() : trd("mock", this)
{
    lock = srs_mutex_new();
}

MockOnCycleThread2::~MockOnCycleThread2()
{
    srs_mutex_destroy(lock);
}

srs_error_t MockOnCycleThread2::cycle()
{
    srs_error_t err = srs_success;

    for (;;) {
        srs_mutex_lock(lock);
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);
        srs_mutex_unlock(lock);

        srs_error_t err = trd.pull();
        if (err != srs_success) {
            return err;
        }
    }

    return err;
}

srs_error_t MockOnCycleThread2::start()
{
    return trd.start();
}

void MockOnCycleThread2::stop()
{
    trd.stop();
}

VOID TEST(TCPServerTest, ThreadMutexWait)
{
    MockOnCycleThread2 trd;
    trd.trd.start();

    srs_usleep(1 * SRS_UTIME_MILLISECONDS);

    srs_mutex_lock(trd.lock);
    trd.trd.stop();
    srs_mutex_unlock(trd.lock);
}

MockOnCycleThread3::MockOnCycleThread3() : trd("mock", this)
{
    fd = NULL;
}

MockOnCycleThread3::~MockOnCycleThread3()
{
    trd.stop();
    srs_close_stfd(fd);
}

srs_error_t MockOnCycleThread3::start(std::string ip, int port)
{
    srs_error_t err = srs_success;
    if ((err = srs_tcp_listen(ip, port, &fd)) != srs_success) {
        return err;
    }

    return trd.start();
}

srs_error_t MockOnCycleThread3::do_cycle(srs_netfd_t cfd)
{
    srs_error_t err = srs_success;

    SrsStSocket skt(cfd);
    skt.set_recv_timeout(1 * SRS_UTIME_SECONDS);
    skt.set_send_timeout(1 * SRS_UTIME_SECONDS);

    while (true) {
        if ((err = trd.pull()) != srs_success) {
            return err;
        }

        char buf[5];
        if ((err = skt.read_fully(buf, 5, NULL)) != srs_success) {
            return err;
        }
        if ((err = skt.write(buf, 5, NULL)) != srs_success) {
            return err;
        }
    }

    return err;
}

srs_error_t MockOnCycleThread3::cycle()
{
    srs_error_t err = srs_success;

    srs_netfd_t cfd = srs_accept(fd, NULL, NULL, SRS_UTIME_NO_TIMEOUT);
    if (cfd == NULL) {
        return err;
    }

    err = do_cycle(cfd);
    srs_close_stfd(cfd);
    srs_freep(err);

    return err;
}

void MockOnCycleThread3::stop()
{
    trd.stop();
}

VOID TEST(TCPServerTest, TCPClientServer)
{
    srs_error_t err;

    MockOnCycleThread3 trd;
    HELPER_ASSERT_SUCCESS(trd.start("127.0.0.1", 1935));

    SrsTcpClient c("127.0.0.1", 1935, 1 * SRS_UTIME_SECONDS);
    HELPER_ASSERT_SUCCESS(c.connect());

    c.set_recv_timeout(1 * SRS_UTIME_SECONDS);
    c.set_send_timeout(1 * SRS_UTIME_SECONDS);

    if (true) {
        HELPER_ASSERT_SUCCESS(c.write((void *)"Hello", 5, NULL));

        char buf[6];
        HELPER_ARRAY_INIT(buf, 6, 0);
        HELPER_ASSERT_SUCCESS(c.read(buf, 5, NULL));
        EXPECT_STREQ("Hello", buf);
    }

    if (true) {
        HELPER_ASSERT_SUCCESS(c.write((void *)"Hello", 5, NULL));

        char buf[6];
        HELPER_ARRAY_INIT(buf, 6, 0);
        HELPER_ASSERT_SUCCESS(c.read_fully(buf, 5, NULL));
        EXPECT_STREQ("Hello", buf);
    }

    if (true) {
        HELPER_ASSERT_SUCCESS(c.write((void *)"Hello", 5, NULL));

        char buf[6];
        HELPER_ARRAY_INIT(buf, 6, 0);
        ASSERT_EQ(5, srs_read(c.stfd_, buf, 5, 1 * SRS_UTIME_SECONDS));
        EXPECT_STREQ("Hello", buf);
    }
}

VOID TEST(TCPServerTest, CoverUtility)
{
    EXPECT_TRUE(srs_net_url_is_http("http://"));
    EXPECT_TRUE(srs_net_url_is_http("https://"));
    EXPECT_TRUE(srs_net_url_is_http("http://localhost"));
    EXPECT_TRUE(srs_net_url_is_http("https://localhost"));
    EXPECT_FALSE(srs_net_url_is_http("ftp://"));
    EXPECT_FALSE(srs_net_url_is_http("ftps://"));
    EXPECT_FALSE(srs_net_url_is_http("http:"));
    EXPECT_FALSE(srs_net_url_is_http("https:"));
    EXPECT_TRUE(srs_net_url_is_rtmp("rtmp://"));
    EXPECT_TRUE(srs_net_url_is_rtmp("rtmp://localhost"));
    EXPECT_FALSE(srs_net_url_is_rtmp("http://"));
    EXPECT_FALSE(srs_net_url_is_rtmp("rtmp:"));

    SrsProtocolUtility utility;

    // ipv4 loopback
    if (true) {
        addrinfo hints;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET;

        addrinfo *r_raw = NULL;
        ASSERT_TRUE(!getaddrinfo("127.0.0.1", NULL, &hints, &r_raw));
        SrsUniquePtr<addrinfo> r(r_raw, freeaddrinfo);

        EXPECT_FALSE(utility.is_internet((sockaddr *)r->ai_addr));
    }

    // ipv4 intranet
    if (true) {
        addrinfo hints;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET;

        addrinfo *r_raw = NULL;
        ASSERT_TRUE(!getaddrinfo("192.168.0.1", NULL, &hints, &r_raw));
        SrsUniquePtr<addrinfo> r(r_raw, freeaddrinfo);

        EXPECT_FALSE(utility.is_internet((sockaddr *)r->ai_addr));
    }

    if (true) {
        sockaddr_in addr;
        addr.sin_family = AF_INET;

        addr.sin_addr.s_addr = htonl(0x12000000);
        EXPECT_TRUE(utility.is_internet((sockaddr *)&addr));

        addr.sin_addr.s_addr = htonl(0x7f000000);
        EXPECT_FALSE(utility.is_internet((sockaddr *)&addr));

        addr.sin_addr.s_addr = htonl(0x7f000001);
        EXPECT_FALSE(utility.is_internet((sockaddr *)&addr));

        addr.sin_addr.s_addr = htonl(0x0a000000);
        EXPECT_FALSE(utility.is_internet((sockaddr *)&addr));

        addr.sin_addr.s_addr = htonl(0x0a000001);
        EXPECT_FALSE(utility.is_internet((sockaddr *)&addr));

        addr.sin_addr.s_addr = htonl(0x0affffff);
        EXPECT_FALSE(utility.is_internet((sockaddr *)&addr));

        addr.sin_addr.s_addr = htonl(0xc0a80000);
        EXPECT_FALSE(utility.is_internet((sockaddr *)&addr));

        addr.sin_addr.s_addr = htonl(0xc0a80001);
        EXPECT_FALSE(utility.is_internet((sockaddr *)&addr));

        addr.sin_addr.s_addr = htonl(0xc0a8ffff);
        EXPECT_FALSE(utility.is_internet((sockaddr *)&addr));
    }

    // Normal ipv6 address.
    if (true) {
        addrinfo hints;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET6;

        addrinfo *r_raw = NULL;
        ASSERT_TRUE(!getaddrinfo("2001:da8:6000:291:21f:d0ff:fed4:928c", NULL, &hints, &r_raw));
        SrsUniquePtr<addrinfo> r(r_raw, freeaddrinfo);

        EXPECT_TRUE(utility.is_internet((sockaddr *)r->ai_addr));
    }
    if (true) {
        addrinfo hints;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET6;

        addrinfo *r_raw = NULL;
        ASSERT_TRUE(!getaddrinfo("3ffe:dead:beef::1", NULL, &hints, &r_raw));
        SrsUniquePtr<addrinfo> r(r_raw, freeaddrinfo);

        EXPECT_TRUE(utility.is_internet((sockaddr *)r->ai_addr));
    }

    // IN6_IS_ADDR_UNSPECIFIED
    if (true) {
        addrinfo hints;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET6;

        addrinfo *r_raw = NULL;
        ASSERT_TRUE(!getaddrinfo("::", NULL, &hints, &r_raw));
        SrsUniquePtr<addrinfo> r(r_raw, freeaddrinfo);

        EXPECT_FALSE(utility.is_internet((sockaddr *)r->ai_addr));
    }

    // IN6_IS_ADDR_SITELOCAL
    if (true) {
        addrinfo hints;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET6;

        addrinfo *r_raw = NULL;
        ASSERT_TRUE(!getaddrinfo("fec0::", NULL, &hints, &r_raw));
        SrsUniquePtr<addrinfo> r(r_raw, freeaddrinfo);

        EXPECT_FALSE(utility.is_internet((sockaddr *)r->ai_addr));
    }

    // IN6_IS_ADDR_LINKLOCAL
    if (true) {
        addrinfo hints;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET6;

        addrinfo *r_raw = NULL;
        ASSERT_TRUE(!getaddrinfo("FE80::", NULL, &hints, &r_raw));
        SrsUniquePtr<addrinfo> r(r_raw, freeaddrinfo);

        EXPECT_FALSE(utility.is_internet((sockaddr *)r->ai_addr));
    }

    // IN6_IS_ADDR_LINKLOCAL
    if (true) {
        addrinfo hints;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET6;

        addrinfo *r_raw = NULL;
        ASSERT_TRUE(!getaddrinfo("::1", NULL, &hints, &r_raw));
        SrsUniquePtr<addrinfo> r(r_raw, freeaddrinfo);

        EXPECT_FALSE(utility.is_internet((sockaddr *)r->ai_addr));
    }
}

MockConnectionManager::MockConnectionManager()
{
}

MockConnectionManager::~MockConnectionManager()
{
}

srs_error_t MockConnectionManager::start()
{
    return srs_success;
}

bool MockConnectionManager::empty()
{
    return true;
}

size_t MockConnectionManager::size()
{
    return 0;
}

void MockConnectionManager::add(ISrsResource * /*conn*/, bool * /*exists*/)
{
}

void MockConnectionManager::add_with_id(const std::string & /*id*/, ISrsResource * /*conn*/)
{
}

void MockConnectionManager::add_with_fast_id(uint64_t /*id*/, ISrsResource * /*conn*/)
{
}

void MockConnectionManager::add_with_name(const std::string & /*name*/, ISrsResource * /*conn*/)
{
}

ISrsResource *MockConnectionManager::at(int /*index*/)
{
    return NULL;
}

ISrsResource *MockConnectionManager::find_by_id(std::string /*id*/)
{
    return NULL;
}

ISrsResource *MockConnectionManager::find_by_fast_id(uint64_t /*id*/)
{
    return NULL;
}

ISrsResource *MockConnectionManager::find_by_name(std::string /*name*/)
{
    return NULL;
}

void MockConnectionManager::remove(ISrsResource * /*c*/)
{
}

void MockConnectionManager::subscribe(ISrsDisposingHandler * /*h*/)
{
}

void MockConnectionManager::unsubscribe(ISrsDisposingHandler * /*h*/)
{
}

void MockConnectionManager::dispose()
{
}

VOID TEST(TCPServerTest, ContextUtility)
{
    if (true) {
        SrsThreadContext ctx;

        if (true) {
            SrsContextId cid;
            EXPECT_TRUE(!ctx.set_id(cid.set_value("100")).compare(cid));
        }
        if (true) {
            SrsContextId cid;
            EXPECT_TRUE(!ctx.set_id(cid.set_value("1000")).compare(cid));
        }
        if (true) {
            SrsContextId cid;
            EXPECT_TRUE(!ctx.get_id().compare(cid.set_value("1000")));
        }

        ctx.clear_cid();
        if (true) {
            SrsContextId cid;
            EXPECT_TRUE(!ctx.set_id(cid.set_value("100")).compare(cid));
        }
    }

    SrsContextId cid;
    cid.set_value("100");

    int base_size = 0;
    if (true) {
        errno = 0;
        int size = 0;
        char buf[1024];
        HELPER_ARRAY_INIT(buf, 1024, 0);
        ASSERT_TRUE(srs_log_header(buf, 1024, true, true, "SRS", cid, "Trace", &size));
        base_size = size;
        EXPECT_TRUE(base_size > 0);
    }

    if (true) {
        errno = 0;
        int size = 0;
        char buf[1024];
        HELPER_ARRAY_INIT(buf, 1024, 0);
        ASSERT_TRUE(srs_log_header(buf, 1024, false, true, "SRS", cid, "Trace", &size));
        EXPECT_EQ(base_size, size);
    }

    if (true) {
        errno = 0;
        int size = 0;
        char buf[1024];
        HELPER_ARRAY_INIT(buf, 1024, 0);
        ASSERT_TRUE(srs_log_header(buf, 1024, false, true, NULL, cid, "Trace", &size));
        EXPECT_EQ(base_size - 5, size);
    }

    if (true) {
        errno = 0;
        int size = 0;
        char buf[1024];
        HELPER_ARRAY_INIT(buf, 1024, 0);
        ASSERT_TRUE(srs_log_header(buf, 1024, false, false, NULL, cid, "Trace", &size));
        EXPECT_EQ(base_size - 8, size);
    }

    if (true) {
        MockConnectionManager cm;
        cm.remove(NULL);
    }

    if (true) {
        srs_utime_t to = 1 * SRS_UTIME_SECONDS;
        SrsBasicRtmpClient rc("rtmp://127.0.0.1/live/livestream", to, to);
        rc.close();
    }
}

MockStopSelfThread::MockStopSelfThread() : r0(0), r1(0), trd("mock", this)
{
}

MockStopSelfThread::~MockStopSelfThread()
{
}

srs_error_t MockStopSelfThread::start()
{
    return trd.start();
}

void MockStopSelfThread::stop()
{
    trd.stop();
}

srs_error_t MockStopSelfThread::cycle()
{
    r0 = st_thread_join((st_thread_t)trd.trd_, NULL);
    r1 = errno;
    return srs_success;
}

VOID TEST(ThreadCriticalTest, ShouldFailWhenStopSelf)
{
    srs_error_t err;
    MockStopSelfThread trd;
    HELPER_EXPECT_SUCCESS(trd.start());

    // Switch to thread cycle, should fail.
    srs_usleep(0);
    EXPECT_EQ(-1, trd.r0);
    EXPECT_EQ(EDEADLK, trd.r1);
}

MockAsyncReaderThread::MockAsyncReaderThread(srs_netfd_t v) : trd("mock", this), fd(v)
{
}

MockAsyncReaderThread::~MockAsyncReaderThread()
{
}

srs_error_t MockAsyncReaderThread::start()
{
    return trd.start();
}

void MockAsyncReaderThread::stop()
{
    trd.stop();
}

srs_error_t MockAsyncReaderThread::cycle()
{
    srs_error_t err = srs_success;
    while (true) {
        if ((err = trd.pull()) != srs_success) {
            return err;
        }
        char buf[16] = {0};
        if (st_read((st_netfd_t)fd, buf, sizeof(buf), SRS_UTIME_NO_TIMEOUT) <= 0) {
            break;
        }
    }
    return err;
}

VOID TEST(ThreadCriticalTest, FailIfCloseActiveFD)
{
    srs_error_t err;

    MockTcpHandler h;
    SrsTcpListener l(&h);
    l.set_endpoint(_srs_tmp_host, _srs_tmp_port);
    HELPER_EXPECT_SUCCESS(l.listen());

    SrsTcpClient c0(_srs_tmp_host, _srs_tmp_port, _srs_tmp_timeout);
    HELPER_EXPECT_SUCCESS(c0.connect());

    srs_usleep(1 * SRS_UTIME_MILLISECONDS);
    EXPECT_TRUE(h.fd != NULL);

    MockAsyncReaderThread trd0(h.fd);
    HELPER_EXPECT_SUCCESS(trd0.start());

    MockAsyncReaderThread trd1(h.fd);
    HELPER_EXPECT_SUCCESS(trd1.start());

    // Wait for all threads to run.
    srs_usleep(1 * SRS_UTIME_MILLISECONDS);

    // Should fail when close, because there is 2 threads reading fd.
    int r0 = st_netfd_close((st_netfd_t)h.fd);
    EXPECT_EQ(-1, r0);
    EXPECT_EQ(EBUSY, errno);

    // Stop thread1, still fail because thread0 is reading fd.
    trd1.stop();
    r0 = st_netfd_close((st_netfd_t)h.fd);
    EXPECT_EQ(-1, r0);
    EXPECT_EQ(EBUSY, errno);

    // Stop thread0, should success, no threads is reading fd.
    trd0.stop();
    r0 = st_netfd_close((st_netfd_t)h.fd);
    EXPECT_EQ(0, r0);

    // Set fd to NULL to avoid close fail for EBADF.
    h.fd = NULL;
}
