//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_UTEST_AI25_HPP
#define SRS_UTEST_AI25_HPP

/*
#include <srs_utest_ai25.hpp>
*/
#include <srs_utest.hpp>

#include <srs_app_http_api.hpp>
#include <srs_protocol_http_conn.hpp>
#include <srs_protocol_http_stack.hpp>

// Mock HTTP message for testing count parameter handling
class MockHttpMessageForCountTest : public ISrsHttpMessage
{
public:
    MockHttpConn *mock_conn_;
    std::map<std::string, std::string> query_params_;

public:
    MockHttpMessageForCountTest();
    virtual ~MockHttpMessageForCountTest();

    // Set a query parameter value
    void set_query_param(const std::string& key, const std::string& value);
    
    // Clear all query parameters
    void clear_query_params();

public:
    // ISrsHttpMessage interface implementation
    virtual uint8_t message_type();
    virtual uint8_t method();
    virtual uint16_t status_code();
    virtual std::string method_str();
    virtual bool is_http_get();
    virtual bool is_http_put();
    virtual bool is_http_post();
    virtual bool is_http_delete();
    virtual bool is_http_options();
    virtual std::string uri();
    virtual std::string url();
    virtual std::string host();
    virtual std::string path();
    virtual std::string query();
    virtual std::string ext();
    virtual srs_error_t body_read_all(std::string &body);
    virtual ISrsHttpResponseReader *body_reader();
    virtual int64_t content_length();
    virtual std::string query_get(std::string key);
    virtual SrsHttpHeader *header();
    virtual bool is_jsonp();
    virtual bool is_keep_alive();
    virtual std::string parse_rest_id(std::string pattern);
};

// Test fixture for HTTP API count parameter handling
class HttpApiCountParameterTest : public ::testing::Test
{
protected:
    void SetUp() override;
    void TearDown() override;
    
    // Helper method to test count parameter processing
    int process_count_parameter(const std::string& count_value);
    
    // Test different count parameter scenarios
    void test_empty_count_parameter();
    void test_valid_positive_count();
    void test_zero_count_parameter();
    void test_negative_count_parameter();
    void test_non_numeric_count_parameter();
    void test_mixed_numeric_text_count();
    void test_large_numeric_count();

protected:
    MockHttpMessageForCountTest *mock_msg_;
};

#endif // SRS_UTEST_AI25_HPP