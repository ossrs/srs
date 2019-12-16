/*
The MIT License (MIT)

Copyright (c) 2013-2019 Winlin

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
#include <srs_utest_http.hpp>

#include <sstream>
using namespace std;

#include <srs_http_stack.hpp>
#include <srs_service_http_conn.hpp>
#include <srs_utest_protocol.hpp>

class MockResponseWriter : virtual public ISrsHttpResponseWriter, virtual public ISrsHttpHeaderFilter
{
public:
    SrsHttpResponseWriter* w;
    MockBufferIO io;
public:
    MockResponseWriter();
    virtual ~MockResponseWriter();
public:
    virtual srs_error_t final_request();
    virtual SrsHttpHeader* header();
    virtual srs_error_t write(char* data, int size);
    virtual srs_error_t writev(const iovec* iov, int iovcnt, ssize_t* pnwrite);
    virtual void write_header(int code);
public:
    virtual srs_error_t filter(SrsHttpHeader* h);
};

MockResponseWriter::MockResponseWriter()
{
    w = new SrsHttpResponseWriter(&io);
    w->hf = this;
}

MockResponseWriter::~MockResponseWriter()
{
    srs_freep(w);
}

srs_error_t MockResponseWriter::final_request()
{
    return w->final_request();
}

SrsHttpHeader* MockResponseWriter::header()
{
    return w->header();
}

srs_error_t MockResponseWriter::write(char* data, int size)
{
    return w->write(data, size);
}

srs_error_t MockResponseWriter::writev(const iovec* iov, int iovcnt, ssize_t* pnwrite)
{
    return w->writev(iov, iovcnt, pnwrite);
}

void MockResponseWriter::write_header(int code)
{
    w->write_header(code);
}

srs_error_t MockResponseWriter::filter(SrsHttpHeader* h)
{
    h->del("Content-Type");
    h->del("Server");
    h->del("Connection");
    return srs_success;
}

string mock_http_response(int status, string content)
{
    stringstream ss;
    ss << "HTTP/1.1 " << status << " " << srs_generate_http_status_text(status) << "\r\n"
        << "Content-Length: " << content.length() << "\r\n"
        << "\r\n"
        << content;
    return ss.str();
}

VOID TEST(ProtocolHTTPTest, StatusCode2Text)
{
    EXPECT_STREQ(SRS_CONSTS_HTTP_OK_str, srs_generate_http_status_text(SRS_CONSTS_HTTP_OK).c_str());
    EXPECT_STREQ("Status Unknown", srs_generate_http_status_text(999).c_str());

    EXPECT_FALSE(srs_go_http_body_allowd(SRS_CONSTS_HTTP_Continue));
    EXPECT_FALSE(srs_go_http_body_allowd(SRS_CONSTS_HTTP_OK-1));
    EXPECT_FALSE(srs_go_http_body_allowd(SRS_CONSTS_HTTP_NoContent));
    EXPECT_FALSE(srs_go_http_body_allowd(SRS_CONSTS_HTTP_NotModified));
    EXPECT_TRUE(srs_go_http_body_allowd(SRS_CONSTS_HTTP_OK));
}

VOID TEST(ProtocolHTTPTest, ResponseDetect)
{
    EXPECT_STREQ("application/octet-stream", srs_go_http_detect(NULL, 0).c_str());
}

VOID TEST(ProtocolHTTPTest, ResponseHTTPError)
{
    srs_error_t err;

    if (true) {
        MockResponseWriter w;
        HELPER_EXPECT_SUCCESS(srs_go_http_error(&w, SRS_CONSTS_HTTP_Found));
        EXPECT_STREQ(mock_http_response(302,"Found").c_str(), HELPER_BUFFER2STR(&w.io.out_buffer).c_str());
    }
}

VOID TEST(ProtocolHTTPTest, HTTPHeader)
{
    SrsHttpHeader h;
    h.set("Server", "SRS");
    EXPECT_STREQ("SRS", h.get("Server").c_str());

    stringstream ss;
    h.write(ss);
    EXPECT_STREQ("Server: SRS\r\n", ss.str().c_str());

    h.del("Server");
    EXPECT_TRUE(h.get("Server").empty());

    EXPECT_EQ(-1, h.content_length());

    h.set_content_length(0);
    EXPECT_EQ(0, h.content_length());

    h.set_content_length(1024);
    EXPECT_EQ(1024, h.content_length());

    h.set_content_type("text/plain");
    EXPECT_STREQ("text/plain", h.content_type().c_str());
}

VOID TEST(ProtocolHTTPTest, HTTPCommonHandler)
{
}
