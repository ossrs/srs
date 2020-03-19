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

#ifndef SRS_UTEST_PROTO_STACK_HPP
#define SRS_UTEST_PROTO_STACK_HPP

/*
#include <srs_utest_http.hpp>
*/
#include <srs_utest.hpp>

#include <srs_utest_protocol.hpp>
#include <srs_http_stack.hpp>
#include <srs_service_http_conn.hpp>

#include <string>
using namespace std;

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

string mock_http_response(int status, string content);
string mock_http_response2(int status, string content);

#define __MOCK_HTTP_EXPECT_STREQ(status, text, w) \
        EXPECT_STREQ(mock_http_response(status, text).c_str(), HELPER_BUFFER2STR(&w.io.out_buffer).c_str())

#define __MOCK_HTTP_EXPECT_STREQ2(status, text, w) \
        EXPECT_STREQ(mock_http_response2(status, text).c_str(), HELPER_BUFFER2STR(&w.io.out_buffer).c_str())

#endif

