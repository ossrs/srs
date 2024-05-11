//
// Copyright (c) 2013-2024 The SRS Authors
//
// SPDX-License-Identifier: MIT
//
#include <srs_utest_protocol3.hpp>

using namespace std;

#include <srs_kernel_error.hpp>
#include <srs_core_autofree.hpp>
#include <srs_protocol_utility.hpp>
#include <srs_protocol_rtmp_msg_array.hpp>
#include <srs_protocol_rtmp_stack.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_app_st.hpp>
#include <srs_protocol_amf0.hpp>
#include <srs_protocol_rtmp_stack.hpp>
#include <srs_protocol_http_conn.hpp>
#include <srs_protocol_protobuf.hpp>
#include <srs_kernel_buffer.hpp>

extern bool srs_is_valid_jsonp_callback(std::string callback);

VOID TEST(ProtocolHttpTest, JsonpCallbackName)
{
    EXPECT_TRUE(srs_is_valid_jsonp_callback(""));
    EXPECT_TRUE(srs_is_valid_jsonp_callback("callback"));
    EXPECT_TRUE(srs_is_valid_jsonp_callback("Callback"));
    EXPECT_TRUE(srs_is_valid_jsonp_callback("Callback1234567890"));
    EXPECT_TRUE(srs_is_valid_jsonp_callback("Callback-1234567890"));
    EXPECT_TRUE(srs_is_valid_jsonp_callback("Callback_1234567890"));
    EXPECT_TRUE(srs_is_valid_jsonp_callback("Callback.1234567890"));
    EXPECT_TRUE(srs_is_valid_jsonp_callback("Callback1234567890-_."));
    EXPECT_FALSE(srs_is_valid_jsonp_callback("callback()//"));
    EXPECT_FALSE(srs_is_valid_jsonp_callback("callback!"));
    EXPECT_FALSE(srs_is_valid_jsonp_callback("callback;"));
}

