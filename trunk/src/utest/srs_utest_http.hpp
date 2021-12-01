//
// Copyright (c) 2013-2021 Winlin
//
// SPDX-License-Identifier: MIT
//

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

class MockResponseWriter : public ISrsHttpResponseWriter, public ISrsHttpHeaderFilter
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
bool is_string_contain(string substr, string str);

#define __MOCK_HTTP_EXPECT_STREQ(status, text, w) \
        EXPECT_STREQ(mock_http_response(status, text).c_str(), HELPER_BUFFER2STR(&w.io.out_buffer).c_str())

#define __MOCK_HTTP_EXPECT_STREQ2(status, text, w) \
        EXPECT_STREQ(mock_http_response2(status, text).c_str(), HELPER_BUFFER2STR(&w.io.out_buffer).c_str())

#define __MOCK_HTTP_EXPECT_STRCT(status, text, w) \
        EXPECT_PRED2(is_string_contain, text, HELPER_BUFFER2STR(&w.io.out_buffer).c_str())

#endif

