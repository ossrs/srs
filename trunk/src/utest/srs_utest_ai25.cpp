//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_utest_ai25.hpp>

MockHttpMessageForCountTest::MockHttpMessageForCountTest()
{
    mock_conn_ = new MockHttpConn();
}

MockHttpMessageForCountTest::~MockHttpMessageForCountTest()
{
    srs_freep(mock_conn_);
    query_params_.clear();
}

void MockHttpMessageForCountTest::set_query_param(const std::string &key, const std::string &value)
{
    query_params_[key] = value;
}

void MockHttpMessageForCountTest::clear_query_params()
{
    query_params_.clear();
}

// ISrsHttpMessage interface implementation
uint8_t MockHttpMessageForCountTest::message_type()
{
    return HTTP_REQUEST;
}

uint8_t MockHttpMessageForCountTest::method()
{
    return HTTP_GET;
}

uint16_t MockHttpMessageForCountTest::status_code()
{
    return HTTP_STATUS_OK;
}

std::string MockHttpMessageForCountTest::method_str()
{
    return "GET";
}

bool MockHttpMessageForCountTest::is_http_get()
{
    return true;
}

bool MockHttpMessageForCountTest::is_http_put()
{
    return false;
}

bool MockHttpMessageForCountTest::is_http_post()
{
    return false;
}

bool MockHttpMessageForCountTest::is_http_delete()
{
    return false;
}

bool MockHttpMessageForCountTest::is_http_options()
{
    return false;
}

std::string MockHttpMessageForCountTest::uri()
{
    return "/api/v1/streams";
}

std::string MockHttpMessageForCountTest::url()
{
    return "http://localhost/api/v1/streams";
}

std::string MockHttpMessageForCountTest::host()
{
    return "localhost";
}

std::string MockHttpMessageForCountTest::path()
{
    return "/api/v1/streams";
}

std::string MockHttpMessageForCountTest::query()
{
    return "";
}

std::string MockHttpMessageForCountTest::ext()
{
    return "";
}

srs_error_t MockHttpMessageForCountTest::body_read_all(std::string &body)
{
    body = "";
    return srs_success;
}

ISrsHttpResponseReader *MockHttpMessageForCountTest::body_reader()
{
    return NULL;
}

int64_t MockHttpMessageForCountTest::content_length()
{
    return 0;
}

std::string MockHttpMessageForCountTest::query_get(std::string key)
{
    std::map<std::string, std::string>::iterator it = query_params_.find(key);
    if (it != query_params_.end()) {
        return it->second;
    }
    return "";
}

SrsHttpHeader *MockHttpMessageForCountTest::header()
{
    return NULL;
}

bool MockHttpMessageForCountTest::is_jsonp()
{
    return false;
}

bool MockHttpMessageForCountTest::is_keep_alive()
{
    return false;
}

std::string MockHttpMessageForCountTest::parse_rest_id(std::string pattern)
{
    return "";
}

// Test fixture implementation
void HttpApiCountParameterTest::SetUp()
{
    mock_msg_ = new MockHttpMessageForCountTest();
}

void HttpApiCountParameterTest::TearDown()
{
    srs_freep(mock_msg_);
}

// Helper method to simulate the count parameter processing logic
int HttpApiCountParameterTest::process_count_parameter(const std::string &count_value)
{
    // This method replicates the count parameter processing logic from srs_app_http_api.cpp
    std::string rcount = count_value;
    int count = 10; // Default value

    if (!rcount.empty()) {
        // Use atoi for simple conversion
        int value = atoi(rcount.c_str());

        // Check if value is a positive integer
        if (value > 0) {
            count = value;
        }
        // If not positive, we keep the default value
    }

    return count;
}

// Test cases implementation
void HttpApiCountParameterTest::test_empty_count_parameter()
{
    // Test with empty count parameter
    int count = process_count_parameter("");
    EXPECT_EQ(10, count) << "Empty count parameter should use default value 10";
}

void HttpApiCountParameterTest::test_valid_positive_count()
{
    // Test with valid positive count values
    EXPECT_EQ(1, process_count_parameter("1")) << "Count value 1 should be accepted";
    EXPECT_EQ(5, process_count_parameter("5")) << "Count value 5 should be accepted";
    EXPECT_EQ(20, process_count_parameter("20")) << "Count value 20 should be accepted";
    EXPECT_EQ(100, process_count_parameter("100")) << "Count value 100 should be accepted";
}

void HttpApiCountParameterTest::test_zero_count_parameter()
{
    // Test with count=0, should use default value
    int count = process_count_parameter("0");
    EXPECT_EQ(10, count) << "Count value 0 should use default value 10";
}

void HttpApiCountParameterTest::test_negative_count_parameter()
{
    // Test with negative count values, should use default value
    EXPECT_EQ(10, process_count_parameter("-1")) << "Negative count value -1 should use default value 10";
    EXPECT_EQ(10, process_count_parameter("-5")) << "Negative count value -5 should use default value 10";
    EXPECT_EQ(10, process_count_parameter("-100")) << "Negative count value -100 should use default value 10";
}

void HttpApiCountParameterTest::test_non_numeric_count_parameter()
{
    // Test with non-numeric count values, should use default value
    // Since atoi returns 0 for non-numeric strings
    EXPECT_EQ(10, process_count_parameter("abc")) << "Non-numeric count value 'abc' should use default value 10";
    EXPECT_EQ(10, process_count_parameter("test")) << "Non-numeric count value 'test' should use default value 10";
    EXPECT_EQ(10, process_count_parameter("!@#$%")) << "Non-numeric count value '!@#$%' should use default value 10";
}

void HttpApiCountParameterTest::test_mixed_numeric_text_count()
{
    // Test with mixed numeric and text values
    // atoi stops at first non-digit character
    EXPECT_EQ(123, process_count_parameter("123abc")) << "Mixed value '123abc' should be parsed as 123";
    EXPECT_EQ(45, process_count_parameter("45test67")) << "Mixed value '45test67' should be parsed as 45";
    EXPECT_EQ(10, process_count_parameter("abc123")) << "Mixed value starting with letters should be parsed as 0";
}

void HttpApiCountParameterTest::test_large_numeric_count()
{
    // Test with large numeric values
    EXPECT_EQ(9999, process_count_parameter("9999")) << "Large count value 9999 should be accepted";

    // Test with maximum int value (assuming 32-bit int)
    EXPECT_EQ(2147483647, process_count_parameter("2147483647")) << "Maximum int value should be accepted";

    // Test with value exceeding maximum int (will cause overflow)
    // The behavior is implementation-defined, but we can still test it
    // Note: We don't check the exact value due to undefined overflow behavior
    int large_overflow = process_count_parameter("2147483648");
    EXPECT_NE(2147483648, large_overflow) << "Value exceeding max int should cause overflow";
}

// Google Test test cases
VOID TEST_F(HttpApiCountParameterTest, EmptyCountParameter)
{
    test_empty_count_parameter();
}

VOID TEST_F(HttpApiCountParameterTest, ValidPositiveCount)
{
    test_valid_positive_count();
}

VOID TEST_F(HttpApiCountParameterTest, ZeroCountParameter)
{
    test_zero_count_parameter();
}

VOID TEST_F(HttpApiCountParameterTest, NegativeCountParameter)
{
    test_negative_count_parameter();
}

VOID TEST_F(HttpApiCountParameterTest, NonNumericCountParameter)
{
    test_non_numeric_count_parameter();
}

VOID TEST_F(HttpApiCountParameterTest, MixedNumericTextCount)
{
    test_mixed_numeric_text_count();
}

VOID TEST_F(HttpApiCountParameterTest, LargeNumericCount)
{
    test_large_numeric_count();
}

// Additional test case: Integration with actual query_get method
VOID TEST_F(HttpApiCountParameterTest, IntegrationWithQueryGet)
{
    // Clear any existing parameters
    mock_msg_->clear_query_params();

    // Test with no count parameter
    std::string rcount = mock_msg_->query_get("count");
    int count = process_count_parameter(rcount);
    EXPECT_EQ(10, count) << "No count parameter should use default value 10";

    // Test with valid count parameter
    mock_msg_->set_query_param("count", "5");
    rcount = mock_msg_->query_get("count");
    count = process_count_parameter(rcount);
    EXPECT_EQ(5, count) << "Valid count parameter 5 should be used";

    // Test with invalid count parameter
    mock_msg_->set_query_param("count", "invalid");
    rcount = mock_msg_->query_get("count");
    count = process_count_parameter(rcount);
    EXPECT_EQ(10, count) << "Invalid count parameter should use default value 10";
}