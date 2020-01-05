/*
The MIT License (MIT)

Copyright (c) 2013-2020 Winlin

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
#include <srs_utest_service.hpp>

using namespace std;

#include <srs_kernel_error.hpp>
#include <srs_app_listener.hpp>
#include <srs_service_st.hpp>
#include <srs_service_utility.hpp>

#include <srs_service_st.hpp>
#include <srs_service_http_conn.hpp>
#include <srs_rtmp_stack.hpp>
#include <srs_core_autofree.hpp>
#include <srs_utest_protocol.hpp>
#include <srs_utest_http.hpp>

class MockSrsConnection : public ISrsConnection
{
public:
    MockSrsConnection() {
    }
    virtual ~MockSrsConnection() {
    }
    virtual std::string remote_ip() {
        return "127.0.0.1";
    }
};

VOID TEST(ServiceTimeTest, TimeUnit)
{
    EXPECT_EQ(1000, SRS_UTIME_MILLISECONDS);
    EXPECT_EQ(1000*1000, SRS_UTIME_SECONDS);
    EXPECT_EQ(60*1000*1000, SRS_UTIME_MINUTES);
    EXPECT_EQ(3600*1000*1000LL, SRS_UTIME_HOURS);

    EXPECT_TRUE(srs_is_never_timeout(SRS_UTIME_NO_TIMEOUT));
    EXPECT_FALSE(srs_is_never_timeout(0));
}

class MockTcpHandler : public ISrsTcpHandler
{
private:
	srs_netfd_t fd;
public:
	MockTcpHandler();
	virtual ~MockTcpHandler();
public:
    virtual srs_error_t on_tcp_client(srs_netfd_t stfd);
};

MockTcpHandler::MockTcpHandler()
{
	fd = NULL;
}

MockTcpHandler::~MockTcpHandler()
{
	srs_close_stfd(fd);
}

srs_error_t MockTcpHandler::on_tcp_client(srs_netfd_t stfd)
{
	fd = stfd;
	return srs_success;
}

VOID TEST(TCPServerTest, PingPong)
{
	srs_error_t err;
	if (true) {
		MockTcpHandler h;
		SrsTcpListener l(&h, _srs_tmp_host, _srs_tmp_port);

		HELPER_EXPECT_SUCCESS(l.listen());
		EXPECT_TRUE(l.fd() > 0);
	}

	if (true) {
		MockTcpHandler h;
		SrsTcpListener l(&h, _srs_tmp_host, _srs_tmp_port);
		HELPER_EXPECT_SUCCESS(l.listen());

		SrsTcpClient c(_srs_tmp_host, _srs_tmp_port, _srs_tmp_timeout);
		HELPER_EXPECT_SUCCESS(c.connect());

		EXPECT_TRUE(h.fd != NULL);
	}

	if (true) {
		MockTcpHandler h;
		SrsTcpListener l(&h, _srs_tmp_host, _srs_tmp_port);
		HELPER_EXPECT_SUCCESS(l.listen());

		SrsTcpClient c(_srs_tmp_host, _srs_tmp_port, _srs_tmp_timeout);
		HELPER_EXPECT_SUCCESS(c.connect());

		SrsStSocket skt;
		ASSERT_TRUE(h.fd != NULL);
		HELPER_EXPECT_SUCCESS(skt.initialize(h.fd));

		HELPER_EXPECT_SUCCESS(c.write((void*)"Hello", 5, NULL));

		char buf[16] = {0};
		HELPER_EXPECT_SUCCESS(skt.read(buf, 5, NULL));
		EXPECT_STREQ(buf, "Hello");
	}

	if (true) {
		MockTcpHandler h;
		SrsTcpListener l(&h, _srs_tmp_host, _srs_tmp_port);
		HELPER_EXPECT_SUCCESS(l.listen());

		SrsTcpClient c(_srs_tmp_host, _srs_tmp_port, _srs_tmp_timeout);
		HELPER_EXPECT_SUCCESS(c.connect());

		SrsStSocket skt;
		ASSERT_TRUE(h.fd != NULL);
		HELPER_EXPECT_SUCCESS(skt.initialize(h.fd));

		HELPER_EXPECT_SUCCESS(c.write((void*)"Hello", 5, NULL));
		HELPER_EXPECT_SUCCESS(c.write((void*)" ", 1, NULL));
		HELPER_EXPECT_SUCCESS(c.write((void*)"SRS", 3, NULL));

		char buf[16] = {0};
		HELPER_EXPECT_SUCCESS(skt.read(buf, 9, NULL));
		EXPECT_STREQ(buf, "Hello SRS");
	}

	if (true) {
		MockTcpHandler h;
		SrsTcpListener l(&h, _srs_tmp_host, _srs_tmp_port);
		HELPER_EXPECT_SUCCESS(l.listen());

		SrsTcpClient c(_srs_tmp_host, _srs_tmp_port, _srs_tmp_timeout);
		HELPER_EXPECT_SUCCESS(c.connect());

		SrsStSocket skt;
		ASSERT_TRUE(h.fd != NULL);
		HELPER_EXPECT_SUCCESS(skt.initialize(h.fd));

		HELPER_EXPECT_SUCCESS(c.write((void*)"Hello SRS", 9, NULL));
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
		SrsTcpListener l(&h, _srs_tmp_host, _srs_tmp_port);
		HELPER_EXPECT_SUCCESS(l.listen());

		SrsTcpClient c(_srs_tmp_host, _srs_tmp_port, _srs_tmp_timeout);
		HELPER_EXPECT_SUCCESS(c.connect());

		SrsStSocket skt;
		ASSERT_TRUE(h.fd != NULL);
		HELPER_EXPECT_SUCCESS(skt.initialize(h.fd));
		skt.set_recv_timeout(1 * SRS_UTIME_MILLISECONDS);

		char buf[16] = {0};
		HELPER_EXPECT_FAILED(skt.read(buf, 9, NULL));
		EXPECT_TRUE(SRS_UTIME_NO_TIMEOUT == skt.get_send_timeout());
		EXPECT_TRUE(1*SRS_UTIME_MILLISECONDS == skt.get_recv_timeout());
	}

	if (true) {
		MockTcpHandler h;
		SrsTcpListener l(&h, _srs_tmp_host, _srs_tmp_port);
		HELPER_EXPECT_SUCCESS(l.listen());

		SrsTcpClient c(_srs_tmp_host, _srs_tmp_port, _srs_tmp_timeout);
		HELPER_EXPECT_SUCCESS(c.connect());

		SrsStSocket skt;
		ASSERT_TRUE(h.fd != NULL);
		HELPER_EXPECT_SUCCESS(skt.initialize(h.fd));
		skt.set_recv_timeout(1 * SRS_UTIME_MILLISECONDS);

		char buf[16] = {0};
		HELPER_EXPECT_FAILED(skt.read_fully(buf, 9, NULL));
		EXPECT_TRUE(SRS_UTIME_NO_TIMEOUT == skt.get_send_timeout());
		EXPECT_TRUE(1*SRS_UTIME_MILLISECONDS == skt.get_recv_timeout());
	}

	if (true) {
		MockTcpHandler h;
		SrsTcpListener l(&h, _srs_tmp_host, _srs_tmp_port);
		HELPER_EXPECT_SUCCESS(l.listen());

		SrsTcpClient c(_srs_tmp_host, _srs_tmp_port, _srs_tmp_timeout);
		HELPER_EXPECT_SUCCESS(c.connect());

		SrsStSocket skt;
		ASSERT_TRUE(h.fd != NULL);
		HELPER_EXPECT_SUCCESS(skt.initialize(h.fd));
		skt.set_recv_timeout(1 * SRS_UTIME_MILLISECONDS);

		HELPER_EXPECT_SUCCESS(c.write((void*)"Hello", 5, NULL));

		char buf[16] = {0};
		HELPER_EXPECT_FAILED(skt.read_fully(buf, 9, NULL));
		EXPECT_TRUE(SRS_UTIME_NO_TIMEOUT == skt.get_send_timeout());
		EXPECT_TRUE(1*SRS_UTIME_MILLISECONDS == skt.get_recv_timeout());
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
        char* str = (char*)"0";
        char* parsed = str; errno = 0;
        EXPECT_EQ(0x0, ::strtol(str, &parsed, 16));
        EXPECT_EQ(0, errno);
        EXPECT_EQ(str + 1, parsed);
    }

    if (true) {
        char* str = (char*)"0";
        char* parsed = str; errno = 0;
        EXPECT_EQ(0x0, ::strtol(str, &parsed, 16));
        EXPECT_EQ(0, errno);
        EXPECT_EQ(str + 1, parsed);
    }

    if (true) {
        char* str = (char*)"0000000000";
        char* parsed = str; errno = 0;
        EXPECT_EQ(0x0, ::strtol(str, &parsed, 16));
        EXPECT_EQ(0, errno);
        EXPECT_EQ(str + 10, parsed);
    }

    if (true) {
        char* str = (char*)"01";
        char* parsed = str; errno = 0;
        EXPECT_EQ(0x1, ::strtol(str, &parsed, 16));
        EXPECT_EQ(0, errno);
        EXPECT_EQ(str + 2, parsed);
    }

    if (true) {
        char* str = (char*)"012";
        char* parsed = str; errno = 0;
        EXPECT_EQ(0x12, ::strtol(str, &parsed, 16));
        EXPECT_EQ(0, errno);
        EXPECT_EQ(str + 3, parsed);
    }

    if (true) {
        char* str = (char*)"1234567890";
        char* parsed = str; errno = 0;
        EXPECT_EQ(0x1234567890L, ::strtol(str, &parsed, 16));
        EXPECT_EQ(0, errno);
        EXPECT_EQ(str + 10, parsed);
    }

    if (true) {
        char* str = (char*)"0123456789";
        char* parsed = str; errno = 0;
        EXPECT_EQ(0x123456789L, ::strtol(str, &parsed, 16));
        EXPECT_EQ(0, errno);
        EXPECT_EQ(str + 10, parsed);
    }

    if (true) {
        char* str = (char*)"1234567890a";
        char* parsed = str; errno = 0;
        EXPECT_EQ(0x1234567890a, ::strtol(str, &parsed, 16));
        EXPECT_EQ(0, errno);
        EXPECT_EQ(str + 11, parsed);
    }

    if (true) {
        char* str = (char*)"0x1234567890a";
        char* parsed = str; errno = 0;
        EXPECT_EQ(0x1234567890a, ::strtol(str, &parsed, 16));
        EXPECT_EQ(0, errno);
        EXPECT_EQ(str + 13, parsed);
    }

    if (true) {
        char* str = (char*)"1234567890f";
        char* parsed = str; errno = 0;
        EXPECT_EQ(0x1234567890f, ::strtol(str, &parsed, 16));
        EXPECT_EQ(0, errno);
        EXPECT_EQ(str + 11, parsed);
    }

    if (true) {
        char* str = (char*)"10e3";
        char* parsed = str; errno = 0;
        EXPECT_EQ(0x10e3, ::strtol(str, &parsed, 16));
        EXPECT_EQ(0, errno);
        EXPECT_EQ(str + 4, parsed);
    }

    if (true) {
        char* str = (char*)"!1234567890";
        char* parsed = str; errno = 0;
        EXPECT_EQ(0x0, ::strtol(str, &parsed, 16));
        EXPECT_EQ(0, errno);
        EXPECT_EQ(str, parsed);
    }

    if (true) {
        char* str = (char*)"1234567890g";
        char* parsed = str; errno = 0;
        EXPECT_EQ(0x1234567890, ::strtol(str, &parsed, 16));
        EXPECT_EQ(0, errno);
        EXPECT_EQ(str + 10, parsed);
    }

    if (true) {
        char* str = (char*)"";
        char* parsed = str; errno = 0;
        EXPECT_EQ(0x0, ::strtol(str, &parsed, 16));
        EXPECT_EQ(0, errno);
        EXPECT_EQ(str, parsed);
    }

    if (true) {
        char* str = (char*)"1fffffffffffffffffffffffffffff";
        char* parsed = str; errno = 0;
        EXPECT_EQ(0x7fffffffffffffff, ::strtol(str, &parsed, 16));
        EXPECT_NE(0, errno);
        EXPECT_EQ(str+30, parsed);
    }
}

VOID TEST(TCPServerTest, WritevIOVC)
{
	srs_error_t err;

	if (true) {
		MockTcpHandler h;
		SrsTcpListener l(&h, _srs_tmp_host, _srs_tmp_port);
		HELPER_EXPECT_SUCCESS(l.listen());

		SrsTcpClient c(_srs_tmp_host, _srs_tmp_port, _srs_tmp_timeout);
		HELPER_EXPECT_SUCCESS(c.connect());

		SrsStSocket skt;
		ASSERT_TRUE(h.fd != NULL);
		HELPER_EXPECT_SUCCESS(skt.initialize(h.fd));

		iovec iovs[3];
		iovs[0].iov_base = (void*)"H";
		iovs[0].iov_len = 1;
		iovs[1].iov_base = (void*)"e";
		iovs[1].iov_len = 1;
		iovs[2].iov_base = (void*)"llo";
		iovs[2].iov_len = 3;

		HELPER_EXPECT_SUCCESS(c.writev(iovs, 3, NULL));

		char buf[16] = {0};
		HELPER_EXPECT_SUCCESS(skt.read(buf, 5, NULL));
		EXPECT_STREQ(buf, "Hello");
	}

	if (true) {
		MockTcpHandler h;
		SrsTcpListener l(&h, _srs_tmp_host, _srs_tmp_port);
		HELPER_EXPECT_SUCCESS(l.listen());

		SrsTcpClient c(_srs_tmp_host, _srs_tmp_port, _srs_tmp_timeout);
		HELPER_EXPECT_SUCCESS(c.connect());

		SrsStSocket skt;
		ASSERT_TRUE(h.fd != NULL);
		HELPER_EXPECT_SUCCESS(skt.initialize(h.fd));

		iovec iovs[3];
		iovs[0].iov_base = (void*)"H";
		iovs[0].iov_len = 1;
		iovs[1].iov_base = (void*)NULL;
		iovs[1].iov_len = 0;
		iovs[2].iov_base = (void*)"llo";
		iovs[2].iov_len = 3;

		HELPER_EXPECT_SUCCESS(c.writev(iovs, 3, NULL));

		char buf[16] = {0};
		HELPER_EXPECT_SUCCESS(skt.read(buf, 4, NULL));
		EXPECT_STREQ(buf, "Hllo");
	}
}

VOID TEST(TCPServerTest, MessageConnection)
{
    srs_error_t err;

	if (true) {
	    MockSrsConnection conn;
	    SrsHttpMessage m;
	    m.set_connection(&conn);
	    EXPECT_TRUE(&conn == m.connection()); EXPECT_FALSE(m.is_jsonp());
	}

	if (true) {
	    SrsHttpMessage m;
	    HELPER_EXPECT_SUCCESS(m.set_url("http://127.0.0.1/live/livestream.flv?callback=fn&method=POST", true));
	    EXPECT_TRUE(m.jsonp); EXPECT_STREQ("POST", m.jsonp_method.c_str()); EXPECT_TRUE(m.is_jsonp());
	}

	if (true) {
	    SrsHttpMessage m;
	    HELPER_EXPECT_SUCCESS(m.set_url("http://127.0.0.1/live/livestream.flv?callback=fn&method=GET", true));
	    EXPECT_EQ(SRS_CONSTS_HTTP_GET, m.method()); EXPECT_STREQ("GET", m.method_str().c_str()); EXPECT_TRUE(m.is_jsonp());
	}

	if (true) {
	    SrsHttpMessage m;
	    HELPER_EXPECT_SUCCESS(m.set_url("http://127.0.0.1/live/livestream.flv?callback=fn&method=PUT", true));
	    EXPECT_EQ(SRS_CONSTS_HTTP_PUT, m.method()); EXPECT_STREQ("PUT", m.method_str().c_str()); EXPECT_TRUE(m.is_jsonp());
	}

	if (true) {
	    SrsHttpMessage m;
	    HELPER_EXPECT_SUCCESS(m.set_url("http://127.0.0.1/live/livestream.flv?callback=fn&method=POST", true));
	    EXPECT_EQ(SRS_CONSTS_HTTP_POST, m.method()); EXPECT_STREQ("POST", m.method_str().c_str()); EXPECT_TRUE(m.is_jsonp());
	}

	if (true) {
	    SrsHttpMessage m;
	    HELPER_EXPECT_SUCCESS(m.set_url("http://127.0.0.1/live/livestream.flv?callback=fn&method=DELETE", true));
	    EXPECT_EQ(SRS_CONSTS_HTTP_DELETE, m.method()); EXPECT_STREQ("DELETE", m.method_str().c_str()); EXPECT_TRUE(m.is_jsonp());
	}

	if (true) {
	    SrsHttpMessage m;
	    m.set_basic(100, 0, 0); EXPECT_STREQ("OTHER", m.method_str().c_str());
	    m.set_basic(SRS_CONSTS_HTTP_GET, 0, 0); EXPECT_EQ(SRS_CONSTS_HTTP_GET, m.method()); EXPECT_STREQ("GET", m.method_str().c_str());
	    m.set_basic(SRS_CONSTS_HTTP_PUT, 0, 0); EXPECT_EQ(SRS_CONSTS_HTTP_PUT, m.method()); EXPECT_STREQ("PUT", m.method_str().c_str());
	    m.set_basic(SRS_CONSTS_HTTP_POST, 0, 0); EXPECT_EQ(SRS_CONSTS_HTTP_POST, m.method()); EXPECT_STREQ("POST", m.method_str().c_str());
	    m.set_basic(SRS_CONSTS_HTTP_DELETE, 0, 0); EXPECT_EQ(SRS_CONSTS_HTTP_DELETE, m.method()); EXPECT_STREQ("DELETE", m.method_str().c_str());
	    m.set_basic(SRS_CONSTS_HTTP_OPTIONS, 0, 0); EXPECT_EQ(SRS_CONSTS_HTTP_OPTIONS, m.method()); EXPECT_STREQ("OPTIONS", m.method_str().c_str());
	}

	if (true) {
	    SrsHttpMessage m;
	    EXPECT_TRUE(m.is_keep_alive());
	    EXPECT_FALSE(m.is_infinite_chunked());
	}

	if (true) {
	    SrsHttpMessage m;
	    HELPER_EXPECT_SUCCESS(m.set_url("http://127.0.0.1/live/livestream.flv", false));
	    EXPECT_STREQ("http://127.0.0.1/live/livestream.flv", m.uri().c_str()); EXPECT_FALSE(m.is_jsonp());
	}

	if (true) {
	    SrsHttpMessage m;
	    HELPER_EXPECT_SUCCESS(m.set_url("http://127.0.0.1/live/livestream.flv?domain=ossrs.net", false));
	    EXPECT_STREQ("ossrs.net", m.host().c_str()); EXPECT_FALSE(m.is_jsonp());
	}

	if (true) {
	    SrsHttpMessage m;
	    HELPER_EXPECT_SUCCESS(m.set_url("http://127.0.0.1/live/livestream.flv", false));
	    EXPECT_STREQ(".flv", m.ext().c_str()); EXPECT_FALSE(m.is_jsonp());
	}

	if (true) {
	    SrsHttpMessage m;
	    HELPER_EXPECT_SUCCESS(m.set_url("http://127.0.0.1/v1/streams/100", false));
	    EXPECT_EQ(100, m.parse_rest_id("/v1/streams/")); EXPECT_FALSE(m.is_jsonp());
	}
}

VOID TEST(TCPServerTest, MessageInfinityChunked)
{
    srs_error_t err;

	if (true) {
	    SrsHttpMessage m;
	    EXPECT_FALSE(m.is_infinite_chunked());
	    HELPER_EXPECT_SUCCESS(m.enter_infinite_chunked());
	    EXPECT_TRUE(m.is_infinite_chunked());
	}

	if (true) {
	    SrsHttpMessage m;
	    HELPER_EXPECT_SUCCESS(m.enter_infinite_chunked());
	    HELPER_EXPECT_SUCCESS(m.enter_infinite_chunked());
	    EXPECT_TRUE(m.is_infinite_chunked());
	}

	if (true) {
	    SrsHttpMessage m;
	    SrsHttpHeader hdr;
	    hdr.set("Transfer-Encoding", "chunked");
	    m.set_header(&hdr, false);
	    HELPER_EXPECT_FAILED(m.enter_infinite_chunked());
	}

	if (true) {
	    SrsHttpMessage m;
	    SrsHttpHeader hdr;
	    hdr.set("Content-Length", "100");
	    m.set_header(&hdr, false);
	    HELPER_EXPECT_FAILED(m.enter_infinite_chunked());
	}
}

VOID TEST(TCPServerTest, MessageTurnRequest)
{
    srs_error_t err;

	if (true) {
	    SrsHttpMessage m;
	    HELPER_ASSERT_SUCCESS(m.set_url("http://127.0.0.1/live/livestream.flv", false));
	    SrsRequest* r = m.to_request("ossrs.net");
	    EXPECT_STREQ("live", r->app.c_str());
	    EXPECT_STREQ("livestream", r->stream.c_str());
	    EXPECT_STREQ("rtmp://ossrs.net/live", r->tcUrl.c_str());
	    srs_freep(r);
	}

	if (true) {
	    SrsHttpMessage m;
	    HELPER_ASSERT_SUCCESS(m.set_url("http://127.0.0.1/live/livestream.flv?token=key", false));
	    SrsRequest* r = m.to_request("ossrs.net");
	    EXPECT_STREQ("rtmp://ossrs.net/live", r->tcUrl.c_str());
	    EXPECT_STREQ("?token=key", r->param.c_str());
	    srs_freep(r);
	}

	if (true) {
	    MockSrsConnection conn;
	    SrsHttpMessage m;
	    m.set_connection(&conn);

	    SrsRequest* r = m.to_request("ossrs.net");
	    EXPECT_STREQ("127.0.0.1", r->ip.c_str());
	    srs_freep(r);
	}

	if (true) {
	    MockSrsConnection conn;
	    SrsHttpMessage m;
	    m.set_connection(&conn);

	    SrsHttpHeader hdr;
	    hdr.set("X-Real-IP", "10.11.12.13");
	    m.set_header(&hdr, false);

	    SrsRequest* r = m.to_request("ossrs.net");
	    EXPECT_STREQ("10.11.12.13", r->ip.c_str());
	    srs_freep(r);
	}
}

VOID TEST(TCPServerTest, MessageWritev)
{
    srs_error_t err;

    // For infinite chunked mode, all data is content.
    if (true) {
        MockBufferIO io;
        io.append("HTTP/1.1 200 OK\r\n\r\n");

        SrsHttpParser hp; HELPER_ASSERT_SUCCESS(hp.initialize(HTTP_RESPONSE, false));
        ISrsHttpMessage* msg = NULL; HELPER_ASSERT_SUCCESS(hp.parse_message(&io, &msg));

        if (true) {
            SrsHttpMessage* hm = dynamic_cast<SrsHttpMessage*>(msg);
            ASSERT_TRUE(hm != NULL);
            hm->enter_infinite_chunked();
        }

        char buf[32]; ssize_t nread = 0;
        ISrsHttpResponseReader* r = msg->body_reader();

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
    }

    // Directly writev, merge to one chunk.
    if (true) {
        MockResponseWriter w;
        w.write_header(SRS_CONSTS_HTTP_OK);

        iovec iovs[] = {
            {(char*)"Hello", 5},
            {(char*)"World", 5},
            {(char*)"!", 1},
        };
        HELPER_ASSERT_SUCCESS(w.writev(iovs, 3, NULL));

        __MOCK_HTTP_EXPECT_STREQ2(200, "b\r\nHelloWorld!\r\n", w);
    }

    // Use writev to send one iov, should also be ok.
    if (true) {
        MockResponseWriter w;

        char data[] = "Hello, world!";
        iovec iovs[] = {{(char*)data, (int)(sizeof(data) - 1)}};
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
        HELPER_EXPECT_SUCCESS(err2);
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

class MockOnCycleThread : public ISrsCoroutineHandler
{
public:
    SrsSTCoroutine trd;
    srs_cond_t cond;
    MockOnCycleThread() : trd("mock", this, 0) {
        cond = srs_cond_new();
    };
    virtual ~MockOnCycleThread() {
        srs_cond_destroy(cond);
    }
    virtual srs_error_t cycle() {
        srs_error_t err = srs_success;

        for (;;) {
            srs_usleep(10 * SRS_UTIME_MILLISECONDS);
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
};

VOID TEST(TCPServerTest, ThreadCondWait)
{
    MockOnCycleThread trd;
    trd.trd.start();

    srs_usleep(20 * SRS_UTIME_MILLISECONDS);
    srs_cond_wait(trd.cond);
    trd.trd.stop();
}

class MockOnCycleThread2 : public ISrsCoroutineHandler
{
public:
    SrsSTCoroutine trd;
    srs_mutex_t lock;
    MockOnCycleThread2() : trd("mock", this, 0) {
        lock = srs_mutex_new();
    };
    virtual ~MockOnCycleThread2() {
        srs_mutex_destroy(lock);
    }
    virtual srs_error_t cycle() {
        srs_error_t err = srs_success;

        for (;;) {
            srs_mutex_lock(lock);
            srs_usleep(10 * SRS_UTIME_MILLISECONDS);
            srs_mutex_unlock(lock);

            srs_error_t err = trd.pull();
            if (err != srs_success) {
                return err;
            }
        }

        return err;
    }
};

VOID TEST(TCPServerTest, ThreadMutexWait)
{
    MockOnCycleThread2 trd;
    trd.trd.start();

    srs_usleep(20 * SRS_UTIME_MILLISECONDS);

    srs_mutex_lock(trd.lock);
    trd.trd.stop();
    srs_mutex_unlock(trd.lock);
}

class MockOnCycleThread3 : public ISrsCoroutineHandler
{
public:
    SrsSTCoroutine trd;
    srs_netfd_t fd;
    MockOnCycleThread3() : trd("mock", this, 0) {
    };
    virtual ~MockOnCycleThread3() {
        trd.stop();
        srs_close_stfd(fd);
    }
    virtual srs_error_t start(string ip, int port) {
        srs_error_t err = srs_success;
        if ((err = srs_tcp_listen(ip, port, &fd)) != srs_success) {
            return err;
        }

        return trd.start();
    }
    virtual srs_error_t do_cycle(srs_netfd_t cfd) {
        srs_error_t err = srs_success;

        SrsStSocket skt;
        if ((err = skt.initialize(cfd)) != srs_success) {
            return err;
        }

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
    virtual srs_error_t cycle() {
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
};

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
        HELPER_ASSERT_SUCCESS(c.write((void*)"Hello", 5, NULL));

        char buf[6]; HELPER_ARRAY_INIT(buf, 6, 0);
        HELPER_ASSERT_SUCCESS(c.read(buf, 5, NULL));
        EXPECT_STREQ("Hello", buf);
    }

    if (true) {
        HELPER_ASSERT_SUCCESS(c.write((void*)"Hello", 5, NULL));

        char buf[6]; HELPER_ARRAY_INIT(buf, 6, 0);
        HELPER_ASSERT_SUCCESS(c.read_fully(buf, 5, NULL));
        EXPECT_STREQ("Hello", buf);
    }
}

