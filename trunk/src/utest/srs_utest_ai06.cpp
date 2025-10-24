//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_utest_ai06.hpp>

using namespace std;

#include <srs_app_st.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_codec.hpp>
#include <srs_kernel_rtc_rtp.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_amf0.hpp>
#include <srs_protocol_conn.hpp>
#include <srs_protocol_http_client.hpp>
#include <srs_protocol_rtmp_conn.hpp>
#include <srs_protocol_rtp.hpp>
#include <srs_protocol_st.hpp>
#include <srs_utest_manual_http.hpp>

VOID TEST(HTTPClientTest, HTTPClientUtility)
{
    srs_error_t err;

    // Typical HTTP POST.
    if (true) {
        SrsHttpTestServer server("OK");
        HELPER_ASSERT_SUCCESS(server.start());

        SrsHttpClient client;
        HELPER_ASSERT_SUCCESS(client.initialize("http", "127.0.0.1", server.get_port(), 1 * SRS_UTIME_SECONDS));

        ISrsHttpMessage *res = NULL;
        HELPER_ASSERT_SUCCESS(client.post("/api/v1", "", &res));
        SrsUniquePtr<ISrsHttpMessage> res_uptr(res);

        ISrsHttpResponseReader *br = res->body_reader();
        ASSERT_FALSE(br->eof());

        ssize_t nn = 0;
        char buf[1024];
        HELPER_ARRAY_INIT(buf, sizeof(buf), 0);
        HELPER_ASSERT_SUCCESS(br->read(buf, sizeof(buf), &nn));
        ASSERT_EQ(2, nn);
        EXPECT_STREQ("OK", buf);
    }

    // Typical HTTP GET.
    if (true) {
        SrsHttpTestServer server("OK");
        HELPER_ASSERT_SUCCESS(server.start());

        SrsHttpClient client;
        HELPER_ASSERT_SUCCESS(client.initialize("http", "127.0.0.1", server.get_port(), 1 * SRS_UTIME_SECONDS));

        ISrsHttpMessage *res = NULL;
        HELPER_ASSERT_SUCCESS(client.get("/api/v1", "", &res));
        SrsUniquePtr<ISrsHttpMessage> res_uptr(res);

        ISrsHttpResponseReader *br = res->body_reader();
        ASSERT_FALSE(br->eof());

        ssize_t nn = 0;
        char buf[1024];
        HELPER_ARRAY_INIT(buf, sizeof(buf), 0);
        HELPER_ASSERT_SUCCESS(br->read(buf, sizeof(buf), &nn));
        ASSERT_EQ(2, nn);
        EXPECT_STREQ("OK", buf);
    }

    // Set receive timeout and Kbps sample.
    if (true) {
        SrsHttpTestServer server("OK");
        HELPER_ASSERT_SUCCESS(server.start());

        SrsHttpClient client;
        HELPER_ASSERT_SUCCESS(client.initialize("http", "127.0.0.1", server.get_port(), 1 * SRS_UTIME_SECONDS));
        client.set_recv_timeout(1 * SRS_UTIME_SECONDS);
        client.set_header("agent", "srs");

        ISrsHttpMessage *res = NULL;
        HELPER_ASSERT_SUCCESS(client.get("/api/v1", "", &res));
        SrsUniquePtr<ISrsHttpMessage> res_uptr(res);

        ISrsHttpResponseReader *br = res->body_reader();
        ASSERT_FALSE(br->eof());

        ssize_t nn = 0;
        char buf[1024];
        HELPER_ARRAY_INIT(buf, sizeof(buf), 0);
        HELPER_ASSERT_SUCCESS(br->read(buf, sizeof(buf), &nn));
        ASSERT_EQ(2, nn);
        EXPECT_STREQ("OK", buf);

        client.kbps_sample("SRS", 0);
    }
}

VOID TEST(HTTPSClientTest, HTTPSClientPost)
{
    srs_error_t err;

    // Test HTTPS POST request
    if (true) {
        SrsHttpsTestServer server("HTTPS OK");
        HELPER_ASSERT_SUCCESS(server.start());

        // Give server time to start
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);

        SrsHttpClient client;
        HELPER_ASSERT_SUCCESS(client.initialize("https", "127.0.0.1", server.get_port(), 5 * SRS_UTIME_SECONDS));

        string post_data = "{\"test\": \"data\"}";
        ISrsHttpMessage *res = NULL;
        HELPER_ASSERT_SUCCESS(client.post("/api/test", post_data, &res));
        SrsUniquePtr<ISrsHttpMessage> res_uptr(res);

        ISrsHttpResponseReader *br = res->body_reader();
        ASSERT_FALSE(br->eof());

        ssize_t nn = 0;
        char buf[1024];
        HELPER_ARRAY_INIT(buf, sizeof(buf), 0);
        HELPER_ASSERT_SUCCESS(br->read(buf, sizeof(buf), &nn));
        ASSERT_EQ(8, nn);
        EXPECT_STREQ("HTTPS OK", buf);
    }

    // Test HTTPS GET request
    if (true) {
        SrsHttpsTestServer server("HTTPS OK");
        HELPER_ASSERT_SUCCESS(server.start());

        // Give server time to start
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);

        SrsHttpClient client;
        HELPER_ASSERT_SUCCESS(client.initialize("https", "127.0.0.1", server.get_port(), 5 * SRS_UTIME_SECONDS));

        ISrsHttpMessage *res = NULL;
        HELPER_ASSERT_SUCCESS(client.get("/api/test", "", &res));
        SrsUniquePtr<ISrsHttpMessage> res_uptr(res);

        ISrsHttpResponseReader *br = res->body_reader();
        ASSERT_FALSE(br->eof());

        ssize_t nn = 0;
        char buf[1024];
        HELPER_ARRAY_INIT(buf, sizeof(buf), 0);
        HELPER_ASSERT_SUCCESS(br->read(buf, sizeof(buf), &nn));
        ASSERT_EQ(8, nn);
        EXPECT_STREQ("HTTPS OK", buf);
    }
}

VOID TEST(HTTPClientTest, HTTPClientHeaders)
{
    srs_error_t err;

    // Test custom headers
    if (true) {
        SrsHttpTestServer server("Header Test");
        HELPER_ASSERT_SUCCESS(server.start());

        SrsHttpClient client;
        HELPER_ASSERT_SUCCESS(client.initialize("http", "127.0.0.1", server.get_port(), 1 * SRS_UTIME_SECONDS));

        // Set custom headers
        client.set_header("X-Custom-Header", "test-value");
        client.set_header("Authorization", "Bearer token123");
        client.set_header("Content-Type", "application/xml");

        ISrsHttpMessage *res = NULL;
        HELPER_ASSERT_SUCCESS(client.post("/api/headers", "<xml>test</xml>", &res));
        SrsUniquePtr<ISrsHttpMessage> res_uptr(res);

        ISrsHttpResponseReader *br = res->body_reader();
        ASSERT_FALSE(br->eof());

        ssize_t nn = 0;
        char buf[1024];
        HELPER_ARRAY_INIT(buf, sizeof(buf), 0);
        HELPER_ASSERT_SUCCESS(br->read(buf, sizeof(buf), &nn));
        ASSERT_EQ(11, nn);
        EXPECT_STREQ("Header Test", buf);
    }

    // Test header chaining
    if (true) {
        SrsHttpTestServer server("Chain Test");
        HELPER_ASSERT_SUCCESS(server.start());

        SrsHttpClient client;
        HELPER_ASSERT_SUCCESS(client.initialize("http", "127.0.0.1", server.get_port(), 1 * SRS_UTIME_SECONDS));

        // Chain header setting
        client.set_header("X-Test-1", "value1")->set_header("X-Test-2", "value2");

        ISrsHttpMessage *res = NULL;
        HELPER_ASSERT_SUCCESS(client.get("/api/chain", "", &res));
        SrsUniquePtr<ISrsHttpMessage> res_uptr(res);

        ISrsHttpResponseReader *br = res->body_reader();
        ASSERT_FALSE(br->eof());

        ssize_t nn = 0;
        char buf[1024];
        HELPER_ARRAY_INIT(buf, sizeof(buf), 0);
        HELPER_ASSERT_SUCCESS(br->read(buf, sizeof(buf), &nn));
        ASSERT_EQ(10, nn);
        EXPECT_STREQ("Chain Test", buf);
    }
}

VOID TEST(HTTPClientTest, HTTPClientErrorHandling)
{
    srs_error_t err;

    // Test connection to non-existent server
    if (true) {
        SrsHttpClient client;
        HELPER_ASSERT_SUCCESS(client.initialize("http", "127.0.0.1", 1, 100 * SRS_UTIME_MILLISECONDS));

        ISrsHttpMessage *res = NULL;
        HELPER_EXPECT_FAILED(client.post("/api/test", "data", &res));
        EXPECT_TRUE(res == NULL);
    }

    // Test empty path handling
    if (true) {
        SrsHttpTestServer server("Empty Path");
        HELPER_ASSERT_SUCCESS(server.start());

        SrsHttpClient client;
        HELPER_ASSERT_SUCCESS(client.initialize("http", "127.0.0.1", server.get_port(), 1 * SRS_UTIME_SECONDS));

        ISrsHttpMessage *res = NULL;
        HELPER_ASSERT_SUCCESS(client.get("", "", &res));
        SrsUniquePtr<ISrsHttpMessage> res_uptr(res);

        ISrsHttpResponseReader *br = res->body_reader();
        ASSERT_FALSE(br->eof());

        ssize_t nn = 0;
        char buf[1024];
        HELPER_ARRAY_INIT(buf, sizeof(buf), 0);
        HELPER_ASSERT_SUCCESS(br->read(buf, sizeof(buf), &nn));
        ASSERT_EQ(10, nn);
        EXPECT_STREQ("Empty Path", buf);
    }
}

VOID TEST(HTTPClientTest, HTTPClientLargeData)
{
    srs_error_t err;

    // Test large POST data
    if (true) {
        SrsHttpTestServer server("Large Data OK");
        HELPER_ASSERT_SUCCESS(server.start());

        SrsHttpClient client;
        HELPER_ASSERT_SUCCESS(client.initialize("http", "127.0.0.1", server.get_port(), 5 * SRS_UTIME_SECONDS));

        // Create large data (1KB)
        string large_data(1024, 'A');

        ISrsHttpMessage *res = NULL;
        HELPER_ASSERT_SUCCESS(client.post("/api/large", large_data, &res));
        SrsUniquePtr<ISrsHttpMessage> res_uptr(res);

        ISrsHttpResponseReader *br = res->body_reader();
        ASSERT_FALSE(br->eof());

        ssize_t nn = 0;
        char buf[1024];
        HELPER_ARRAY_INIT(buf, sizeof(buf), 0);
        HELPER_ASSERT_SUCCESS(br->read(buf, sizeof(buf), &nn));
        ASSERT_EQ(13, nn);
        EXPECT_STREQ("Large Data OK", buf);
    }

    // Test empty POST data
    if (true) {
        SrsHttpTestServer server("Empty Data OK");
        HELPER_ASSERT_SUCCESS(server.start());

        SrsHttpClient client;
        HELPER_ASSERT_SUCCESS(client.initialize("http", "127.0.0.1", server.get_port(), 1 * SRS_UTIME_SECONDS));

        ISrsHttpMessage *res = NULL;
        HELPER_ASSERT_SUCCESS(client.post("/api/empty", "", &res));
        SrsUniquePtr<ISrsHttpMessage> res_uptr(res);

        ISrsHttpResponseReader *br = res->body_reader();
        ASSERT_FALSE(br->eof());

        ssize_t nn = 0;
        char buf[1024];
        HELPER_ARRAY_INIT(buf, sizeof(buf), 0);
        HELPER_ASSERT_SUCCESS(br->read(buf, sizeof(buf), &nn));
        ASSERT_EQ(13, nn);
        EXPECT_STREQ("Empty Data OK", buf);
    }
}

VOID TEST(HTTPSClientTest, HTTPSClientErrorHandling)
{
    srs_error_t err;

    // Test HTTPS connection to HTTP server (should fail)
    if (true) {
        SrsHttpTestServer server("HTTP Server");
        HELPER_ASSERT_SUCCESS(server.start());

        SrsHttpClient client;
        HELPER_ASSERT_SUCCESS(client.initialize("https", "127.0.0.1", server.get_port(), 1 * SRS_UTIME_SECONDS));

        ISrsHttpMessage *res = NULL;
        HELPER_EXPECT_FAILED(client.get("/api/test", "", &res));
        EXPECT_TRUE(res == NULL);
    }

    // Test HTTPS with invalid port
    if (true) {
        SrsHttpClient client;
        HELPER_ASSERT_SUCCESS(client.initialize("https", "127.0.0.1", 1, 100 * SRS_UTIME_MILLISECONDS));

        ISrsHttpMessage *res = NULL;
        HELPER_EXPECT_FAILED(client.post("/api/test", "data", &res));
        EXPECT_TRUE(res == NULL);
    }
}

VOID TEST(HTTPSClientTest, HTTPSClientHeaders)
{
    srs_error_t err;

    // Test HTTPS with custom headers
    if (true) {
        SrsHttpsTestServer server("HTTPS Headers");
        HELPER_ASSERT_SUCCESS(server.start());

        // Give server time to start
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);

        SrsHttpClient client;
        HELPER_ASSERT_SUCCESS(client.initialize("https", "127.0.0.1", server.get_port(), 5 * SRS_UTIME_SECONDS));

        // Set custom headers for HTTPS
        client.set_header("X-SSL-Test", "secure-value");
        client.set_header("Authorization", "Bearer ssl-token");
        client.set_recv_timeout(2 * SRS_UTIME_SECONDS);

        string post_data = "{\"ssl\": \"test\"}";
        ISrsHttpMessage *res = NULL;
        HELPER_ASSERT_SUCCESS(client.post("/api/ssl-headers", post_data, &res));
        SrsUniquePtr<ISrsHttpMessage> res_uptr(res);

        ISrsHttpResponseReader *br = res->body_reader();
        ASSERT_FALSE(br->eof());

        ssize_t nn = 0;
        char buf[1024];
        HELPER_ARRAY_INIT(buf, sizeof(buf), 0);
        HELPER_ASSERT_SUCCESS(br->read(buf, sizeof(buf), &nn));
        ASSERT_EQ(13, nn);
        EXPECT_STREQ("HTTPS Headers", buf);
    }
}

VOID TEST(HTTPClientTest, HTTPClientConnectionReuse)
{
    srs_error_t err;

    // Test connection reuse for multiple requests
    if (true) {
        SrsHttpTestServer server("Reuse Test");
        HELPER_ASSERT_SUCCESS(server.start());

        SrsHttpClient client;
        HELPER_ASSERT_SUCCESS(client.initialize("http", "127.0.0.1", server.get_port(), 2 * SRS_UTIME_SECONDS));

        // First request
        ISrsHttpMessage *res1 = NULL;
        HELPER_ASSERT_SUCCESS(client.get("/api/first", "", &res1));
        SrsUniquePtr<ISrsHttpMessage> res1_uptr(res1);

        ISrsHttpResponseReader *br1 = res1->body_reader();
        ssize_t nn1 = 0;
        char buf1[1024];
        HELPER_ARRAY_INIT(buf1, sizeof(buf1), 0);
        HELPER_ASSERT_SUCCESS(br1->read(buf1, sizeof(buf1), &nn1));
        ASSERT_EQ(10, nn1);
        EXPECT_STREQ("Reuse Test", buf1);

        // Second request on same connection
        ISrsHttpMessage *res2 = NULL;
        HELPER_ASSERT_SUCCESS(client.post("/api/second", "data", &res2));
        SrsUniquePtr<ISrsHttpMessage> res2_uptr(res2);

        ISrsHttpResponseReader *br2 = res2->body_reader();
        ssize_t nn2 = 0;
        char buf2[1024];
        HELPER_ARRAY_INIT(buf2, sizeof(buf2), 0);
        HELPER_ASSERT_SUCCESS(br2->read(buf2, sizeof(buf2), &nn2));
        ASSERT_EQ(10, nn2);
        EXPECT_STREQ("Reuse Test", buf2);

        // Third request with different headers
        client.set_header("X-Request-Count", "3");
        ISrsHttpMessage *res3 = NULL;
        HELPER_ASSERT_SUCCESS(client.get("/api/third", "", &res3));
        SrsUniquePtr<ISrsHttpMessage> res3_uptr(res3);

        ISrsHttpResponseReader *br3 = res3->body_reader();
        ssize_t nn3 = 0;
        char buf3[1024];
        HELPER_ARRAY_INIT(buf3, sizeof(buf3), 0);
        HELPER_ASSERT_SUCCESS(br3->read(buf3, sizeof(buf3), &nn3));
        ASSERT_EQ(10, nn3);
        EXPECT_STREQ("Reuse Test", buf3);
    }
}

VOID TEST(HTTPClientTest, HTTPClientSpecialCases)
{
    srs_error_t err;

    // Test client reinitialization
    if (true) {
        SrsHttpTestServer server1("Server 1");
        HELPER_ASSERT_SUCCESS(server1.start());

        SrsHttpTestServer server2("Server 2");
        HELPER_ASSERT_SUCCESS(server2.start());

        SrsHttpClient client;

        // Initialize with first server
        HELPER_ASSERT_SUCCESS(client.initialize("http", "127.0.0.1", server1.get_port(), 1 * SRS_UTIME_SECONDS));

        ISrsHttpMessage *res1 = NULL;
        HELPER_ASSERT_SUCCESS(client.get("/api/test", "", &res1));
        SrsUniquePtr<ISrsHttpMessage> res1_uptr(res1);

        ISrsHttpResponseReader *br1 = res1->body_reader();
        ssize_t nn1 = 0;
        char buf1[1024];
        HELPER_ARRAY_INIT(buf1, sizeof(buf1), 0);
        HELPER_ASSERT_SUCCESS(br1->read(buf1, sizeof(buf1), &nn1));
        ASSERT_EQ(8, nn1);
        EXPECT_STREQ("Server 1", buf1);

        // Reinitialize with second server
        HELPER_ASSERT_SUCCESS(client.initialize("http", "127.0.0.1", server2.get_port(), 1 * SRS_UTIME_SECONDS));

        ISrsHttpMessage *res2 = NULL;
        HELPER_ASSERT_SUCCESS(client.get("/api/test", "", &res2));
        SrsUniquePtr<ISrsHttpMessage> res2_uptr(res2);

        ISrsHttpResponseReader *br2 = res2->body_reader();
        ssize_t nn2 = 0;
        char buf2[1024];
        HELPER_ARRAY_INIT(buf2, sizeof(buf2), 0);
        HELPER_ASSERT_SUCCESS(br2->read(buf2, sizeof(buf2), &nn2));
        ASSERT_EQ(8, nn2);
        EXPECT_STREQ("Server 2", buf2);
    }

    // Test header override
    if (true) {
        SrsHttpTestServer server("Header Override");
        HELPER_ASSERT_SUCCESS(server.start());

        SrsHttpClient client;
        HELPER_ASSERT_SUCCESS(client.initialize("http", "127.0.0.1", server.get_port(), 1 * SRS_UTIME_SECONDS));

        // Set initial header
        client.set_header("X-Test", "initial");

        // Override the same header
        client.set_header("X-Test", "overridden");

        // Override default headers
        client.set_header("User-Agent", "Custom-Agent/1.0");
        client.set_header("Content-Type", "text/plain");

        ISrsHttpMessage *res = NULL;
        HELPER_ASSERT_SUCCESS(client.post("/api/override", "test data", &res));
        SrsUniquePtr<ISrsHttpMessage> res_uptr(res);

        ISrsHttpResponseReader *br = res->body_reader();
        ssize_t nn = 0;
        char buf[1024];
        HELPER_ARRAY_INIT(buf, sizeof(buf), 0);
        HELPER_ASSERT_SUCCESS(br->read(buf, sizeof(buf), &nn));
        ASSERT_EQ(15, nn);
        EXPECT_STREQ("Header Override", buf);
    }
}

VOID TEST(HTTPSClientTest, HTTPSClientLargeData)
{
    srs_error_t err;

    // Test HTTPS with large data transfer
    if (true) {
        SrsHttpsTestServer server("HTTPS Large");
        HELPER_ASSERT_SUCCESS(server.start());

        // Give server time to start
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);

        SrsHttpClient client;
        HELPER_ASSERT_SUCCESS(client.initialize("https", "127.0.0.1", server.get_port(), 10 * SRS_UTIME_SECONDS));
        client.set_recv_timeout(5 * SRS_UTIME_SECONDS);

        // Create large data (2KB)
        string large_data(2048, 'S');

        ISrsHttpMessage *res = NULL;
        HELPER_ASSERT_SUCCESS(client.post("/api/ssl-large", large_data, &res));
        SrsUniquePtr<ISrsHttpMessage> res_uptr(res);

        ISrsHttpResponseReader *br = res->body_reader();
        ASSERT_FALSE(br->eof());

        ssize_t nn = 0;
        char buf[1024];
        HELPER_ARRAY_INIT(buf, sizeof(buf), 0);
        HELPER_ASSERT_SUCCESS(br->read(buf, sizeof(buf), &nn));
        ASSERT_EQ(11, nn);
        EXPECT_STREQ("HTTPS Large", buf);

        // Test kbps sampling after large transfer
        client.kbps_sample("HTTPS-LARGE", 500 * SRS_UTIME_MILLISECONDS);
    }
}

VOID TEST(RTMPClientTest, RTMPClientConnect)
{
    srs_error_t err;

    // Test basic RTMP connection
    if (true) {
        SrsRtmpTestServer server("live", "test");
        HELPER_ASSERT_SUCCESS(server.start());

        // Give server time to start
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);

        SrsBasicRtmpClient client(server.url(), 5 * SRS_UTIME_SECONDS, 5 * SRS_UTIME_SECONDS);
        HELPER_ASSERT_SUCCESS(client.connect());

        // Test that connection is established by checking stream ID
        EXPECT_TRUE(client.sid() > 0);
    }

    // Test connection with custom timeout
    if (true) {
        SrsRtmpTestServer server("app", "stream");
        HELPER_ASSERT_SUCCESS(server.start());

        // Give server time to start
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);

        SrsBasicRtmpClient client(server.url(), 1 * SRS_UTIME_SECONDS, 1 * SRS_UTIME_SECONDS);
        HELPER_ASSERT_SUCCESS(client.connect());

        EXPECT_TRUE(client.sid() > 0);
    }
}

VOID TEST(RTMPClientTest, RTMPClientPublish)
{
    srs_error_t err;

    // Test RTMP publish
    if (true) {
        SrsRtmpTestServer server("live", "publish_test");
        server.enable_publish(true);
        server.enable_play(false);
        HELPER_ASSERT_SUCCESS(server.start());

        // Give server time to start
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);

        SrsBasicRtmpClient client(server.url(), 5 * SRS_UTIME_SECONDS, 5 * SRS_UTIME_SECONDS);
        HELPER_ASSERT_SUCCESS(client.connect());
        HELPER_ASSERT_SUCCESS(client.publish(4096));

        EXPECT_TRUE(client.sid() > 0);
    }

    // Test publish with different chunk size
    if (true) {
        SrsRtmpTestServer server("live", "publish_chunk");
        server.enable_publish(true);
        HELPER_ASSERT_SUCCESS(server.start());

        // Give server time to start
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);

        SrsBasicRtmpClient client(server.url(), 2 * SRS_UTIME_SECONDS, 2 * SRS_UTIME_SECONDS);
        HELPER_ASSERT_SUCCESS(client.connect());
        HELPER_ASSERT_SUCCESS(client.publish(8192));

        EXPECT_TRUE(client.sid() > 0);
    }
}

VOID TEST(RTMPClientTest, RTMPClientPlay)
{
    srs_error_t err;

    // Test RTMP play
    if (true) {
        SrsRtmpTestServer server("live", "play_test");
        server.enable_publish(false);
        server.enable_play(true);
        HELPER_ASSERT_SUCCESS(server.start());

        // Give server time to start
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);

        SrsBasicRtmpClient client(server.url(), 5 * SRS_UTIME_SECONDS, 5 * SRS_UTIME_SECONDS);
        HELPER_ASSERT_SUCCESS(client.connect());
        HELPER_ASSERT_SUCCESS(client.play(4096));

        EXPECT_TRUE(client.sid() > 0);
    }

    // Test play with different chunk size
    if (true) {
        SrsRtmpTestServer server("live", "play_chunk");
        server.enable_play(true);
        HELPER_ASSERT_SUCCESS(server.start());

        // Give server time to start
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);

        SrsBasicRtmpClient client(server.url(), 2 * SRS_UTIME_SECONDS, 2 * SRS_UTIME_SECONDS);
        HELPER_ASSERT_SUCCESS(client.connect());
        HELPER_ASSERT_SUCCESS(client.play(8192));

        EXPECT_TRUE(client.sid() > 0);
    }
}

VOID TEST(RTMPClientTest, RTMPClientSendMessages)
{
    srs_error_t err;

    // Test send_and_free_message and send_and_free_messages
    if (true) {
        SrsRtmpTestServer server("live", "send_test");
        HELPER_ASSERT_SUCCESS(server.start());

        // Give server time to start
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);

        SrsBasicRtmpClient client(server.url(), 5 * SRS_UTIME_SECONDS, 5 * SRS_UTIME_SECONDS);
        HELPER_ASSERT_SUCCESS(client.connect());
        HELPER_ASSERT_SUCCESS(client.publish(4096));

        // Test send_and_free_message with video data
        if (true) {
            SrsMediaPacket *video_msg = new SrsMediaPacket();
            video_msg->timestamp_ = 1000;
            video_msg->message_type_ = SrsFrameTypeVideo;
            video_msg->stream_id_ = client.sid();

            char *video_data = new char[10];
            memset(video_data, 0x17, 10); // Video keyframe marker
            video_msg->wrap(video_data, 10);

            HELPER_ASSERT_SUCCESS(client.send_and_free_message(video_msg));
        }

        // Test send_and_free_message with audio data
        if (true) {
            SrsMediaPacket *audio_msg = new SrsMediaPacket();
            audio_msg->timestamp_ = 1100;
            audio_msg->message_type_ = SrsFrameTypeAudio;
            audio_msg->stream_id_ = client.sid();

            char *audio_data = new char[8];
            memset(audio_data, 0xAF, 8); // Audio AAC marker
            audio_msg->wrap(audio_data, 8);

            HELPER_ASSERT_SUCCESS(client.send_and_free_message(audio_msg));
        }

        // Test send_and_free_messages with multiple messages
        if (true) {
            const int nb_msgs = 3;
            SrsMediaPacket **msgs = new SrsMediaPacket *[nb_msgs];

            // Create video message
            msgs[0] = new SrsMediaPacket();
            msgs[0]->timestamp_ = 2000;
            msgs[0]->message_type_ = SrsFrameTypeVideo;
            msgs[0]->stream_id_ = client.sid();
            char *video_data = new char[12];
            memset(video_data, 0x27, 12); // Video inter frame marker
            msgs[0]->wrap(video_data, 12);

            // Create audio message
            msgs[1] = new SrsMediaPacket();
            msgs[1]->timestamp_ = 2050;
            msgs[1]->message_type_ = SrsFrameTypeAudio;
            msgs[1]->stream_id_ = client.sid();
            char *audio_data = new char[6];
            memset(audio_data, 0xAF, 6);
            msgs[1]->wrap(audio_data, 6);

            // Create script message
            msgs[2] = new SrsMediaPacket();
            msgs[2]->timestamp_ = 2100;
            msgs[2]->message_type_ = SrsFrameTypeScript;
            msgs[2]->stream_id_ = client.sid();
            char *script_data = new char[4];
            memcpy(script_data, "test", 4);
            msgs[2]->wrap(script_data, 4);

            HELPER_ASSERT_SUCCESS(client.send_and_free_messages(msgs, nb_msgs));

            // Messages are freed by send_and_free_messages, just free the array
            delete[] msgs;
        }
    }
}

VOID TEST(RTMPClientTest, RTMPClientMessageEdgeCases)
{
    srs_error_t err;

    // Test edge cases for message handling
    if (true) {
        SrsRtmpTestServer server("live", "edge_test");
        HELPER_ASSERT_SUCCESS(server.start());

        // Give server time to start
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);

        SrsBasicRtmpClient client(server.url(), 5 * SRS_UTIME_SECONDS, 5 * SRS_UTIME_SECONDS);
        HELPER_ASSERT_SUCCESS(client.connect());
        HELPER_ASSERT_SUCCESS(client.publish(4096));

        // Test send_and_free_message with small message
        if (true) {
            SrsMediaPacket *small_msg = new SrsMediaPacket();
            small_msg->timestamp_ = 3000;
            small_msg->message_type_ = SrsFrameTypeVideo;
            small_msg->stream_id_ = client.sid();

            // Create a small payload
            char *small_data = new char[1];
            small_data[0] = 0x17; // Video keyframe marker
            small_msg->wrap(small_data, 1);

            HELPER_ASSERT_SUCCESS(client.send_and_free_message(small_msg));
        }

        // Test send_and_free_messages with single message
        if (true) {
            const int nb_msgs = 1;
            SrsMediaPacket **msgs = new SrsMediaPacket *[nb_msgs];

            msgs[0] = new SrsMediaPacket();
            msgs[0]->timestamp_ = 4000;
            msgs[0]->message_type_ = SrsFrameTypeAudio;
            msgs[0]->stream_id_ = client.sid();
            char *audio_data = new char[2];
            memset(audio_data, 0xAF, 2);
            msgs[0]->wrap(audio_data, 2);

            HELPER_ASSERT_SUCCESS(client.send_and_free_messages(msgs, nb_msgs));

            // Messages are freed by send_and_free_messages, just free the array
            delete[] msgs;
        }
    }
}

VOID TEST(TCPConnectionTest, TCPConnectionNodelay)
{
    srs_error_t err;

    // Test TCP nodelay functionality
    if (true) {
        SrsTestTcpServer server("127.0.0.1");
        HELPER_ASSERT_SUCCESS(server.start());

        // Give server time to start
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);

        SrsTestTcpClient client("127.0.0.1", server.get_port());
        HELPER_ASSERT_SUCCESS(client.connect());

        // Give time for connection to be established
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);

        SrsTcpConnection *server_conn = server.get_connection();
        SrsTcpConnection *client_conn = client.get_connection();

        ASSERT_TRUE(server_conn != NULL);
        ASSERT_TRUE(client_conn != NULL);

        // Test set_tcp_nodelay on server connection
        HELPER_ASSERT_SUCCESS(server_conn->set_tcp_nodelay(true));
        HELPER_ASSERT_SUCCESS(server_conn->set_tcp_nodelay(false));

        // Test set_tcp_nodelay on client connection
        HELPER_ASSERT_SUCCESS(client_conn->set_tcp_nodelay(true));
        HELPER_ASSERT_SUCCESS(client_conn->set_tcp_nodelay(false));

        // Test multiple calls with same value
        HELPER_ASSERT_SUCCESS(server_conn->set_tcp_nodelay(true));
        HELPER_ASSERT_SUCCESS(server_conn->set_tcp_nodelay(true));
    }
}

VOID TEST(TCPConnectionTest, TCPConnectionSocketBuffer)
{
    srs_error_t err;

    // Test socket buffer functionality
    if (true) {
        SrsTestTcpServer server("127.0.0.1");
        HELPER_ASSERT_SUCCESS(server.start());

        // Give server time to start
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);

        SrsTestTcpClient client("127.0.0.1", server.get_port());
        HELPER_ASSERT_SUCCESS(client.connect());

        // Give time for connection to be established
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);

        SrsTcpConnection *server_conn = server.get_connection();
        SrsTcpConnection *client_conn = client.get_connection();

        ASSERT_TRUE(server_conn != NULL);
        ASSERT_TRUE(client_conn != NULL);

        // Test set_socket_buffer on server connection with different buffer sizes
        HELPER_ASSERT_SUCCESS(server_conn->set_socket_buffer(1 * SRS_UTIME_SECONDS));
        HELPER_ASSERT_SUCCESS(server_conn->set_socket_buffer(2 * SRS_UTIME_SECONDS));
        HELPER_ASSERT_SUCCESS(server_conn->set_socket_buffer(5 * SRS_UTIME_SECONDS));

        // Test set_socket_buffer on client connection
        HELPER_ASSERT_SUCCESS(client_conn->set_socket_buffer(1 * SRS_UTIME_SECONDS));
        HELPER_ASSERT_SUCCESS(client_conn->set_socket_buffer(3 * SRS_UTIME_SECONDS));

        // Test with smaller buffer sizes
        HELPER_ASSERT_SUCCESS(server_conn->set_socket_buffer(500 * SRS_UTIME_MILLISECONDS));
        HELPER_ASSERT_SUCCESS(client_conn->set_socket_buffer(100 * SRS_UTIME_MILLISECONDS));
    }
}

VOID TEST(TCPConnectionTest, TCPConnectionPingPong)
{
    srs_error_t err;

    // Test ping-pong communication between client and server
    if (true) {
        SrsTestTcpServer server("127.0.0.1");
        HELPER_ASSERT_SUCCESS(server.start());

        // Give server time to start
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);

        SrsTestTcpClient client("127.0.0.1", server.get_port());
        HELPER_ASSERT_SUCCESS(client.connect());

        // Give time for connection to be established
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);

        SrsTcpConnection *server_conn = server.get_connection();
        SrsTcpConnection *client_conn = client.get_connection();

        ASSERT_TRUE(server_conn != NULL);
        ASSERT_TRUE(client_conn != NULL);

        // Client sends "ping" to server
        string ping_msg = "ping";
        ssize_t nwrite = 0;
        HELPER_ASSERT_SUCCESS(client_conn->write((void *)ping_msg.data(), ping_msg.length(), &nwrite));
        EXPECT_EQ(4, nwrite);

        // Server receives "ping"
        char recv_buf[1024];
        ssize_t nread = 0;
        HELPER_ASSERT_SUCCESS(server_conn->read(recv_buf, sizeof(recv_buf), &nread));
        EXPECT_EQ(4, nread);
        EXPECT_STREQ("ping", string(recv_buf, nread).c_str());

        // Server sends "pong" back to client
        string pong_msg = "pong";
        ssize_t nwrite2 = 0;
        HELPER_ASSERT_SUCCESS(server_conn->write((void *)pong_msg.data(), pong_msg.length(), &nwrite2));
        EXPECT_EQ(4, nwrite2);

        // Client receives "pong"
        char recv_buf2[1024];
        ssize_t nread2 = 0;
        HELPER_ASSERT_SUCCESS(client_conn->read(recv_buf2, sizeof(recv_buf2), &nread2));
        EXPECT_EQ(4, nread2);
        EXPECT_STREQ("pong", string(recv_buf2, nread2).c_str());
    }

    // Test multiple ping-pong exchanges
    if (true) {
        SrsTestTcpServer server("127.0.0.1");
        HELPER_ASSERT_SUCCESS(server.start());

        // Give server time to start
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);

        SrsTestTcpClient client("127.0.0.1", server.get_port());
        HELPER_ASSERT_SUCCESS(client.connect());

        // Give time for connection to be established
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);

        SrsTcpConnection *server_conn = server.get_connection();
        SrsTcpConnection *client_conn = client.get_connection();

        ASSERT_TRUE(server_conn != NULL);
        ASSERT_TRUE(client_conn != NULL);

        // Perform multiple ping-pong exchanges
        for (int i = 0; i < 3; i++) {
            // Client sends ping with sequence number
            string ping_msg = "ping" + srs_strconv_format_int(i);
            ssize_t nwrite = 0;
            HELPER_ASSERT_SUCCESS(client_conn->write((void *)ping_msg.data(), ping_msg.length(), &nwrite));
            EXPECT_EQ((ssize_t)ping_msg.length(), nwrite);

            // Server receives ping
            char recv_buf[1024];
            ssize_t nread = 0;
            HELPER_ASSERT_SUCCESS(server_conn->read(recv_buf, sizeof(recv_buf), &nread));
            EXPECT_EQ((ssize_t)ping_msg.length(), nread);
            EXPECT_STREQ(ping_msg.c_str(), string(recv_buf, nread).c_str());

            // Server sends pong with sequence number
            string pong_msg = "pong" + srs_strconv_format_int(i);
            ssize_t nwrite2 = 0;
            HELPER_ASSERT_SUCCESS(server_conn->write((void *)pong_msg.data(), pong_msg.length(), &nwrite2));
            EXPECT_EQ((ssize_t)pong_msg.length(), nwrite2);

            // Client receives pong
            char recv_buf2[1024];
            ssize_t nread2 = 0;
            HELPER_ASSERT_SUCCESS(client_conn->read(recv_buf2, sizeof(recv_buf2), &nread2));
            EXPECT_EQ((ssize_t)pong_msg.length(), nread2);
            EXPECT_STREQ(pong_msg.c_str(), string(recv_buf2, nread2).c_str());
        }
    }
}

VOID TEST(TCPConnectionTest, BufferedReadWriterReadFully)
{
    srs_error_t err;

    // Test SrsBufferedReadWriter::read_fully when buf_ is not empty
    if (true) {
        SrsTestTcpServer server("127.0.0.1");
        HELPER_ASSERT_SUCCESS(server.start());

        // Give server time to start
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);

        SrsTestTcpClient client("127.0.0.1", server.get_port());
        HELPER_ASSERT_SUCCESS(client.connect());

        // Give time for connection to be established
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);

        SrsTcpConnection *server_conn = server.get_connection();
        SrsTcpConnection *client_conn = client.get_connection();

        ASSERT_TRUE(server_conn != NULL);
        ASSERT_TRUE(client_conn != NULL);

        // Create SrsBufferedReadWriter wrapping the server connection
        SrsBufferedReadWriter buffered_reader(server_conn);

        // Client sends enough data to fill the cache (16 bytes) and more
        string test_msg = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"; // 36 bytes
        ssize_t nwrite = 0;
        HELPER_ASSERT_SUCCESS(client_conn->write((void *)test_msg.data(), test_msg.length(), &nwrite));
        EXPECT_EQ((ssize_t)test_msg.length(), nwrite);

        // First, use peek() to populate the internal buffer (buf_)
        // This will trigger reload_buffer() and fill the internal cache_[16] buffer
        char peek_buf[4];
        int peek_size = sizeof(peek_buf);
        HELPER_ASSERT_SUCCESS(buffered_reader.peek(peek_buf, &peek_size));
        EXPECT_EQ(4, peek_size);
        EXPECT_STREQ("0123", string(peek_buf, peek_size).c_str());

        // Now call read_fully() which should use the buffered data first
        // Since buf_ is not empty after peek(), it should read from buffer first
        char read_buf[20];
        ssize_t nread = 0;
        HELPER_ASSERT_SUCCESS(buffered_reader.read_fully(read_buf, sizeof(read_buf), &nread));
        EXPECT_EQ(20, nread);
        // The first 16 bytes should come from the buffer, the rest from underlying io
        EXPECT_STREQ("0123456789ABCDEFGHIJ", string(read_buf, nread).c_str());

        // Read the remaining data to verify the full message
        char remaining_buf[16];
        ssize_t nread2 = 0;
        HELPER_ASSERT_SUCCESS(buffered_reader.read_fully(remaining_buf, sizeof(remaining_buf), &nread2));
        EXPECT_EQ(16, nread2);
        EXPECT_STREQ("KLMNOPQRSTUVWXYZ", string(remaining_buf, nread2).c_str());
    }
}

VOID TEST(TCPConnectionTest, BufferedReadWriterTimeoutAndStats)
{
    srs_error_t err;

    // Test SrsBufferedReadWriter timeout and statistics methods
    if (true) {
        SrsTestTcpServer server("127.0.0.1");
        HELPER_ASSERT_SUCCESS(server.start());

        // Give server time to start
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);

        SrsTestTcpClient client("127.0.0.1", server.get_port());
        HELPER_ASSERT_SUCCESS(client.connect());

        // Give time for connection to be established
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);

        SrsTcpConnection *server_conn = server.get_connection();
        SrsTcpConnection *client_conn = client.get_connection();

        ASSERT_TRUE(server_conn != NULL);
        ASSERT_TRUE(client_conn != NULL);

        // Create SrsBufferedReadWriter wrapping the server connection
        SrsBufferedReadWriter buffered_reader(server_conn);

        // Test timeout methods
        srs_utime_t original_recv_timeout = buffered_reader.get_recv_timeout();
        srs_utime_t original_send_timeout = buffered_reader.get_send_timeout();

        // Set new timeouts
        srs_utime_t new_recv_timeout = 5 * SRS_UTIME_SECONDS;
        srs_utime_t new_send_timeout = 3 * SRS_UTIME_SECONDS;

        buffered_reader.set_recv_timeout(new_recv_timeout);
        buffered_reader.set_send_timeout(new_send_timeout);

        // Verify timeouts were set correctly
        EXPECT_EQ(new_recv_timeout, buffered_reader.get_recv_timeout());
        EXPECT_EQ(new_send_timeout, buffered_reader.get_send_timeout());

        // Test statistics methods - initial values
        int64_t initial_recv_bytes = buffered_reader.get_recv_bytes();

        // Client sends data to server to test recv statistics
        string test_msg = "Hello Statistics Test!";
        ssize_t nwrite = 0;
        HELPER_ASSERT_SUCCESS(client_conn->write((void *)test_msg.data(), test_msg.length(), &nwrite));
        EXPECT_EQ((ssize_t)test_msg.length(), nwrite);

        // Server reads data using buffered reader
        char read_buf[32];
        ssize_t nread = 0;
        HELPER_ASSERT_SUCCESS(buffered_reader.read_fully(read_buf, test_msg.length(), &nread));
        EXPECT_EQ((ssize_t)test_msg.length(), nread);
        EXPECT_STREQ(test_msg.c_str(), string(read_buf, nread).c_str());

        // Check that recv bytes increased
        int64_t final_recv_bytes = buffered_reader.get_recv_bytes();
        EXPECT_GT(final_recv_bytes, initial_recv_bytes);

        // Restore original timeouts
        buffered_reader.set_recv_timeout(original_recv_timeout);
        buffered_reader.set_send_timeout(original_send_timeout);
    }
}

VOID TEST(TCPConnectionTest, BufferedReadWriterWriteMethods)
{
    srs_error_t err;

    // Test SrsBufferedReadWriter write and writev methods
    if (true) {
        SrsTestTcpServer server("127.0.0.1");
        HELPER_ASSERT_SUCCESS(server.start());

        // Give server time to start
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);

        SrsTestTcpClient client("127.0.0.1", server.get_port());
        HELPER_ASSERT_SUCCESS(client.connect());

        // Give time for connection to be established
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);

        SrsTcpConnection *server_conn = server.get_connection();
        SrsTcpConnection *client_conn = client.get_connection();

        ASSERT_TRUE(server_conn != NULL);
        ASSERT_TRUE(client_conn != NULL);

        // Create SrsBufferedReadWriter wrapping the server connection
        SrsBufferedReadWriter buffered_writer(server_conn);

        // Test write() method
        string test_msg1 = "Hello Write Test!";
        ssize_t nwrite = 0;
        HELPER_ASSERT_SUCCESS(buffered_writer.write((void *)test_msg1.data(), test_msg1.length(), &nwrite));
        EXPECT_EQ((ssize_t)test_msg1.length(), nwrite);

        // Client reads the data to verify write worked
        char read_buf1[32];
        ssize_t nread1 = 0;
        HELPER_ASSERT_SUCCESS(client_conn->read_fully(read_buf1, test_msg1.length(), &nread1));
        EXPECT_EQ((ssize_t)test_msg1.length(), nread1);
        EXPECT_STREQ(test_msg1.c_str(), string(read_buf1, nread1).c_str());

        // Test writev() method with multiple buffers
        string part1 = "Hello ";
        string part2 = "Writev ";
        string part3 = "Test!";

        struct iovec iov[3];
        iov[0].iov_base = (void *)part1.data();
        iov[0].iov_len = part1.length();
        iov[1].iov_base = (void *)part2.data();
        iov[1].iov_len = part2.length();
        iov[2].iov_base = (void *)part3.data();
        iov[2].iov_len = part3.length();

        ssize_t nwrite_vec = 0;
        HELPER_ASSERT_SUCCESS(buffered_writer.writev(iov, 3, &nwrite_vec));
        size_t expected_total = part1.length() + part2.length() + part3.length();
        EXPECT_EQ((ssize_t)expected_total, nwrite_vec);

        // Client reads the vectored data to verify writev worked
        char read_buf2[32];
        ssize_t nread2 = 0;
        HELPER_ASSERT_SUCCESS(client_conn->read_fully(read_buf2, expected_total, &nread2));
        EXPECT_EQ((ssize_t)expected_total, nread2);
        string expected_result = part1 + part2 + part3;
        EXPECT_STREQ(expected_result.c_str(), string(read_buf2, nread2).c_str());

        // Test send bytes statistics after write operations
        int64_t send_bytes = buffered_writer.get_send_bytes();
        EXPECT_GT(send_bytes, 0);
    }
}

VOID TEST(TCPConnectionTest, TcpConnectionReadFully)
{
    srs_error_t err;

    // Test SrsTcpConnection::read_fully method with various scenarios
    if (true) {
        SrsTestTcpServer server("127.0.0.1");
        HELPER_ASSERT_SUCCESS(server.start());

        // Give server time to start
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);

        SrsTestTcpClient client("127.0.0.1", server.get_port());
        HELPER_ASSERT_SUCCESS(client.connect());

        // Give time for connection to be established
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);

        SrsTcpConnection *server_conn = server.get_connection();
        SrsTcpConnection *client_conn = client.get_connection();

        ASSERT_TRUE(server_conn != NULL);
        ASSERT_TRUE(client_conn != NULL);

        // Test read_fully with exact size
        string test_msg1 = "Exact size test message!";
        ssize_t nwrite1 = 0;
        HELPER_ASSERT_SUCCESS(client_conn->write((void *)test_msg1.data(), test_msg1.length(), &nwrite1));
        EXPECT_EQ((ssize_t)test_msg1.length(), nwrite1);

        char read_buf1[32];
        ssize_t nread1 = 0;
        HELPER_ASSERT_SUCCESS(server_conn->read_fully(read_buf1, test_msg1.length(), &nread1));
        EXPECT_EQ((ssize_t)test_msg1.length(), nread1);
        EXPECT_STREQ(test_msg1.c_str(), string(read_buf1, nread1).c_str());

        // Test read_fully with larger buffer
        string test_msg2 = "Large buffer test!";
        ssize_t nwrite2 = 0;
        HELPER_ASSERT_SUCCESS(client_conn->write((void *)test_msg2.data(), test_msg2.length(), &nwrite2));
        EXPECT_EQ((ssize_t)test_msg2.length(), nwrite2);

        char read_buf2[64]; // Larger buffer than needed
        ssize_t nread2 = 0;
        HELPER_ASSERT_SUCCESS(server_conn->read_fully(read_buf2, test_msg2.length(), &nread2));
        EXPECT_EQ((ssize_t)test_msg2.length(), nread2);
        EXPECT_STREQ(test_msg2.c_str(), string(read_buf2, nread2).c_str());

        // Test read_fully with multiple small reads
        string part1 = "Part1";
        string part2 = "Part2";
        string part3 = "Part3";

        // Send parts separately
        ssize_t nwrite_p1 = 0, nwrite_p2 = 0, nwrite_p3 = 0;
        HELPER_ASSERT_SUCCESS(client_conn->write((void *)part1.data(), part1.length(), &nwrite_p1));
        HELPER_ASSERT_SUCCESS(client_conn->write((void *)part2.data(), part2.length(), &nwrite_p2));
        HELPER_ASSERT_SUCCESS(client_conn->write((void *)part3.data(), part3.length(), &nwrite_p3));

        // Read all parts in one read_fully call
        size_t total_size = part1.length() + part2.length() + part3.length();
        char read_buf3[32];
        ssize_t nread3 = 0;
        HELPER_ASSERT_SUCCESS(server_conn->read_fully(read_buf3, total_size, &nread3));
        EXPECT_EQ((ssize_t)total_size, nread3);

        string expected_combined = part1 + part2 + part3;
        EXPECT_STREQ(expected_combined.c_str(), string(read_buf3, nread3).c_str());
    }
}

VOID TEST(UDPConnectionTest, UdpConnectionPingPong)
{
    srs_error_t err;

    // Test UDP ping-pong communication using SrsUdpTestServer and SrsUdpTestClient
    if (true) {
        SrsUdpTestServer server("127.0.0.1");
        HELPER_ASSERT_SUCCESS(server.start());

        // Give server time to start
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);

        SrsUdpTestClient client("127.0.0.1", server.get_port());
        HELPER_ASSERT_SUCCESS(client.connect());

        // Give time for connection to be established
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);

        SrsStSocket *server_socket = server.get_socket();
        SrsStSocket *client_socket = client.get_socket();

        ASSERT_TRUE(server_socket != NULL);
        ASSERT_TRUE(client_socket != NULL);

        // Test basic ping-pong
        string ping_msg = "ping";
        ssize_t nwrite = 0;
        HELPER_ASSERT_SUCCESS(client.sendto((void *)ping_msg.data(), ping_msg.length(), &nwrite));
        EXPECT_EQ((ssize_t)ping_msg.length(), nwrite);

        // Give time for server to process and echo back
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);

        // Client receives the echoed message
        char pong_buf[32];
        ssize_t nread = 0;
        HELPER_ASSERT_SUCCESS(client.recvfrom(pong_buf, sizeof(pong_buf), &nread));
        EXPECT_EQ((ssize_t)ping_msg.length(), nread);
        EXPECT_STREQ(ping_msg.c_str(), string(pong_buf, nread).c_str());

        // Test multiple ping-pong exchanges
        for (int i = 0; i < 3; i++) {
            string ping_msg_seq = "ping" + srs_strconv_format_int(i);
            ssize_t nwrite_seq = 0;
            HELPER_ASSERT_SUCCESS(client.sendto((void *)ping_msg_seq.data(), ping_msg_seq.length(), &nwrite_seq));
            EXPECT_EQ((ssize_t)ping_msg_seq.length(), nwrite_seq);

            // Give time for server to process and echo back
            srs_usleep(1 * SRS_UTIME_MILLISECONDS);

            // Client receives the echoed message
            char pong_buf_seq[32];
            ssize_t nread_seq = 0;
            HELPER_ASSERT_SUCCESS(client.recvfrom(pong_buf_seq, sizeof(pong_buf_seq), &nread_seq));
            EXPECT_EQ((ssize_t)ping_msg_seq.length(), nread_seq);
            EXPECT_STREQ(ping_msg_seq.c_str(), string(pong_buf_seq, nread_seq).c_str());
        }

        // Test larger message
        string large_ping = "This is a larger ping message for UDP testing!";
        ssize_t nwrite_large = 0;
        HELPER_ASSERT_SUCCESS(client.sendto((void *)large_ping.data(), large_ping.length(), &nwrite_large));
        EXPECT_EQ((ssize_t)large_ping.length(), nwrite_large);

        // Give time for server to process and echo back
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);

        // Client receives the echoed large message
        char large_pong_buf[128];
        ssize_t nread_large = 0;
        HELPER_ASSERT_SUCCESS(client.recvfrom(large_pong_buf, sizeof(large_pong_buf), &nread_large));
        EXPECT_EQ((ssize_t)large_ping.length(), nread_large);
        EXPECT_STREQ(large_ping.c_str(), string(large_pong_buf, nread_large).c_str());

        // Stop server
        server.stop();
    }
}

VOID TEST(RTPVideoBuilderTest, PackageStapAHevc)
{
    srs_error_t err;

    // Test HEVC STAP-A packaging with VPS/SPS/PPS NALUs
    if (true) {
        // Create mock SrsRtpVideoBuilder
        SrsRtpVideoBuilder builder;
        builder.video_payload_type_ = 96;
        builder.video_ssrc_ = 0x12345678;
        builder.video_sequence_ = 100;

        // Create mock SrsFormat with HEVC codec
        SrsFormat format;
        format.vcodec_ = new SrsVideoCodecConfig();
        format.vcodec_->id_ = SrsVideoCodecIdHEVC;

        // Create HEVC decoder configuration record with VPS/SPS/PPS NALUs
        SrsHevcHvccNalu vps_nalu;
        vps_nalu.nal_unit_type_ = SrsHevcNaluType_VPS;
        vps_nalu.num_nalus_ = 1;

        SrsHevcNalData vps_data;
        vps_data.nal_unit_length_ = 24;
        // Mock VPS data (24 bytes)
        uint8_t vps_bytes[] = {
            0x40, 0x01, 0x0c, 0x01, 0xff, 0xff, 0x01, 0x60,
            0x00, 0x00, 0x03, 0x00, 0x90, 0x00, 0x00, 0x03,
            0x00, 0x00, 0x03, 0x00, 0x5d, 0x95, 0x98, 0x09};
        vps_data.nal_unit_data_.assign(vps_bytes, vps_bytes + 24);
        vps_nalu.nal_data_vec_.push_back(vps_data);

        SrsHevcHvccNalu sps_nalu;
        sps_nalu.nal_unit_type_ = SrsHevcNaluType_SPS;
        sps_nalu.num_nalus_ = 1;

        SrsHevcNalData sps_data;
        sps_data.nal_unit_length_ = 40;
        // Mock SPS data (40 bytes)
        uint8_t sps_bytes[] = {
            0x42, 0x01, 0x01, 0x01, 0x60, 0x00, 0x00, 0x03,
            0x00, 0x90, 0x00, 0x00, 0x03, 0x00, 0x00, 0x03,
            0x00, 0x5d, 0xa0, 0x02, 0x80, 0x80, 0x2d, 0x16,
            0x59, 0x59, 0xa4, 0x93, 0x2b, 0xc0, 0x40, 0x40,
            0x00, 0x00, 0x03, 0x00, 0x40, 0x00, 0x00, 0x07};
        sps_data.nal_unit_data_.assign(sps_bytes, sps_bytes + 40);
        sps_nalu.nal_data_vec_.push_back(sps_data);

        SrsHevcHvccNalu pps_nalu;
        pps_nalu.nal_unit_type_ = SrsHevcNaluType_PPS;
        pps_nalu.num_nalus_ = 1;

        SrsHevcNalData pps_data;
        pps_data.nal_unit_length_ = 8;
        // Mock PPS data (8 bytes)
        uint8_t pps_bytes[] = {
            0x44, 0x01, 0xc1, 0x72, 0xb4, 0x62, 0x40, 0x00};
        pps_data.nal_unit_data_.assign(pps_bytes, pps_bytes + 8);
        pps_nalu.nal_data_vec_.push_back(pps_data);

        // Add NALUs to decoder configuration record
        format.vcodec_->hevc_dec_conf_record_.nalu_vec_.push_back(vps_nalu);
        format.vcodec_->hevc_dec_conf_record_.nalu_vec_.push_back(sps_nalu);
        format.vcodec_->hevc_dec_conf_record_.nalu_vec_.push_back(pps_nalu);

        // Set format in builder
        builder.format_ = &format;

        // Create mock media packet
        SrsMediaPacket msg;
        msg.timestamp_ = 1000;

        // Create RTP packet for STAP-A
        SrsRtpPacket pkt;

        // Test package_stap_a method
        HELPER_ASSERT_SUCCESS(builder.package_stap_a(&msg, &pkt));

        // Verify RTP header settings
        EXPECT_EQ(96, pkt.header_.get_payload_type());
        EXPECT_EQ(0x12345678, pkt.header_.get_ssrc());
        EXPECT_EQ(SrsFrameTypeVideo, pkt.frame_type_);
        EXPECT_FALSE(pkt.header_.get_marker());
        EXPECT_EQ(100, pkt.header_.get_sequence());
        EXPECT_EQ(90000, pkt.header_.get_timestamp()); // 1000 * 90

        // Verify NALU type is set to HEVC STAP
        EXPECT_EQ(kStapHevc, pkt.nalu_type_);

        // Verify payload type is HEVC STAP
        EXPECT_EQ(SrsRtpPacketPayloadTypeSTAPHevc, pkt.payload_type_);

        // Verify payload is SrsRtpSTAPPayloadHevc
        ASSERT_TRUE(pkt.payload_ != NULL);
        SrsRtpSTAPPayloadHevc *stap_payload = static_cast<SrsRtpSTAPPayloadHevc *>(pkt.payload_);

        // Verify NALUs are correctly added
        EXPECT_EQ(3, (int)stap_payload->nalus_.size());

        // Verify VPS NALU
        SrsNaluSample *vps_sample = stap_payload->get_vps();
        ASSERT_TRUE(vps_sample != NULL);
        EXPECT_EQ(24, vps_sample->size_);
        EXPECT_EQ(SrsHevcNaluType_VPS, SrsHevcNaluTypeParse(vps_sample->bytes_[0]));

        // Verify SPS NALU
        SrsNaluSample *sps_sample = stap_payload->get_sps();
        ASSERT_TRUE(sps_sample != NULL);
        EXPECT_EQ(40, sps_sample->size_);
        EXPECT_EQ(SrsHevcNaluType_SPS, SrsHevcNaluTypeParse(sps_sample->bytes_[0]));

        // Verify PPS NALU
        SrsNaluSample *pps_sample = stap_payload->get_pps();
        ASSERT_TRUE(pps_sample != NULL);
        EXPECT_EQ(8, pps_sample->size_);
        EXPECT_EQ(SrsHevcNaluType_PPS, SrsHevcNaluTypeParse(pps_sample->bytes_[0]));

        // Verify data integrity - check first few bytes of each NALU
        EXPECT_EQ(0x40, (uint8_t)vps_sample->bytes_[0]); // VPS header
        EXPECT_EQ(0x42, (uint8_t)sps_sample->bytes_[0]); // SPS header
        EXPECT_EQ(0x44, (uint8_t)pps_sample->bytes_[0]); // PPS header

        // Cleanup
        srs_freep(format.vcodec_);
    }
}

VOID TEST(RTPVideoBuilderTest, PackageNalusWithEmptySamples)
{
    srs_error_t err;

    // Test package_nalus method with empty samples to cover the continue path
    if (true) {
        // Create mock SrsRtpVideoBuilder
        SrsRtpVideoBuilder builder;
        builder.video_payload_type_ = 96;
        builder.video_ssrc_ = 0x12345678;
        builder.video_sequence_ = 200;

        // Create mock SrsFormat with H.264 codec
        SrsFormat format;
        format.vcodec_ = new SrsVideoCodecConfig();
        format.vcodec_->id_ = SrsVideoCodecIdAVC;

        // Set format in builder
        builder.format_ = &format;

        // Create mock media packet
        SrsMediaPacket msg;
        msg.timestamp_ = 2000;

        // Create vector of NALU samples with some empty samples
        vector<SrsNaluSample *> samples;

        // Create first empty sample (size = 0) - this should be skipped
        SrsNaluSample *empty_sample1 = new SrsNaluSample();
        empty_sample1->size_ = 0;
        empty_sample1->bytes_ = NULL;
        samples.push_back(empty_sample1);

        // Create valid IDR sample
        SrsNaluSample *idr_sample = new SrsNaluSample();
        idr_sample->size_ = 20;
        uint8_t idr_bytes[] = {
            0x65, 0x88, 0x84, 0x00, 0x33, 0x56, 0x7e, 0x00,
            0x43, 0x8a, 0x02, 0x09, 0x96, 0xe5, 0x93, 0x2b,
            0x80, 0x40, 0x45, 0x02};
        idr_sample->bytes_ = (char *)malloc(idr_sample->size_);
        memcpy(idr_sample->bytes_, idr_bytes, idr_sample->size_);
        samples.push_back(idr_sample);

        // Create second empty sample (size = 0) - this should also be skipped
        SrsNaluSample *empty_sample2 = new SrsNaluSample();
        empty_sample2->size_ = 0;
        empty_sample2->bytes_ = NULL;
        samples.push_back(empty_sample2);

        // Create valid P-frame sample
        SrsNaluSample *p_sample = new SrsNaluSample();
        p_sample->size_ = 15;
        uint8_t p_bytes[] = {
            0x41, 0x9a, 0x24, 0x66, 0x54, 0x66, 0x90, 0x80,
            0x00, 0x47, 0xbe, 0x7c, 0x05, 0x5a, 0x80};
        p_sample->bytes_ = (char *)malloc(p_sample->size_);
        memcpy(p_sample->bytes_, p_bytes, p_sample->size_);
        samples.push_back(p_sample);

        // Create third empty sample (size = 0) - this should also be skipped
        SrsNaluSample *empty_sample3 = new SrsNaluSample();
        empty_sample3->size_ = 0;
        empty_sample3->bytes_ = NULL;
        samples.push_back(empty_sample3);

        // Create RTP packets vector
        vector<SrsRtpPacket *> pkts;

        // Test package_nalus method - should skip empty samples and package only valid ones
        HELPER_ASSERT_SUCCESS(builder.package_nalus(&msg, samples, pkts));

        // Verify that only one RTP packet was created (combining the two valid NALUs)
        EXPECT_EQ(1, (int)pkts.size());

        if (!pkts.empty()) {
            SrsRtpPacket *pkt = pkts[0];

            // Verify RTP header settings
            EXPECT_EQ(96, pkt->header_.get_payload_type());
            EXPECT_EQ(0x12345678, pkt->header_.get_ssrc());
            EXPECT_EQ(SrsFrameTypeVideo, pkt->frame_type_);
            EXPECT_EQ(200, pkt->header_.get_sequence());
            EXPECT_EQ(180000, pkt->header_.get_timestamp()); // 2000 * 90

            // Verify NALU type is set to first valid NALU type (IDR)
            EXPECT_EQ(SrsAvcNaluTypeIDR, pkt->nalu_type_);

            // Verify payload type is NALU
            EXPECT_EQ(SrsRtpPacketPayloadTypeNALU, pkt->payload_type_);

            // Verify payload exists
            ASSERT_TRUE(pkt->payload_ != NULL);
            SrsRtpRawNALUs *raw_nalus = static_cast<SrsRtpRawNALUs *>(pkt->payload_);

            // Verify that 3 NALUs are in the payload (2 valid NALUs + 1 separator)
            // SrsRtpRawNALUs adds separator bytes (\0\0\1) between NALUs
            EXPECT_EQ(3, (int)raw_nalus->nalus_.size());

            // Verify total bytes (should be sum of valid samples + separator)
            EXPECT_EQ(38, (int)raw_nalus->nb_bytes()); // 20 + 15 + 3 separator = 38 bytes
        }

        // Cleanup RTP packets
        for (size_t i = 0; i < pkts.size(); i++) {
            srs_freep(pkts[i]);
        }

        // Cleanup samples
        for (size_t i = 0; i < samples.size(); i++) {
            SrsNaluSample *sample = samples[i];
            if (sample->bytes_) {
                free(sample->bytes_);
            }
            srs_freep(sample);
        }

        // Cleanup format
        srs_freep(format.vcodec_);
    }
}

VOID TEST(RTPVideoBuilderTest, PackageNalusAllEmptySamples)
{
    srs_error_t err;

    // Test package_nalus method with all empty samples - should create no RTP packets
    if (true) {
        // Create mock SrsRtpVideoBuilder
        SrsRtpVideoBuilder builder;
        builder.video_payload_type_ = 96;
        builder.video_ssrc_ = 0x12345678;
        builder.video_sequence_ = 300;

        // Create mock SrsFormat with HEVC codec
        SrsFormat format;
        format.vcodec_ = new SrsVideoCodecConfig();
        format.vcodec_->id_ = SrsVideoCodecIdHEVC;

        // Set format in builder
        builder.format_ = &format;

        // Create mock media packet
        SrsMediaPacket msg;
        msg.timestamp_ = 3000;

        // Create vector of NALU samples with ALL empty samples
        vector<SrsNaluSample *> samples;

        // Create multiple empty samples
        for (int i = 0; i < 5; i++) {
            SrsNaluSample *empty_sample = new SrsNaluSample();
            empty_sample->size_ = 0;
            empty_sample->bytes_ = NULL;
            samples.push_back(empty_sample);
        }

        // Create RTP packets vector
        vector<SrsRtpPacket *> pkts;

        // Test package_nalus method - should skip all empty samples and create no packets
        HELPER_ASSERT_SUCCESS(builder.package_nalus(&msg, samples, pkts));

        // Verify that no RTP packets were created (all samples were empty)
        EXPECT_EQ(0, (int)pkts.size());

        // Cleanup samples
        for (size_t i = 0; i < samples.size(); i++) {
            srs_freep(samples[i]);
        }

        // Cleanup format
        srs_freep(format.vcodec_);
    }
}

VOID TEST(RTPVideoBuilderTest, PackageNalusLargePayload)
{
    srs_error_t err;

    // Test package_nalus method with large payload that exceeds kRtpMaxPayloadSize (1200 bytes)
    // This should trigger FU-A fragmentation path
    if (true) {
        // Create mock SrsRtpVideoBuilder
        SrsRtpVideoBuilder builder;
        builder.video_payload_type_ = 96;
        builder.video_ssrc_ = 0x87654321;
        builder.video_sequence_ = 400;

        // Create mock SrsFormat with H.264 codec
        SrsFormat format;
        format.vcodec_ = new SrsVideoCodecConfig();
        format.vcodec_->id_ = SrsVideoCodecIdAVC;

        // Set format in builder
        builder.format_ = &format;

        // Create mock media packet
        SrsMediaPacket msg;
        msg.timestamp_ = 4000;

        // Create vector with single large IDR NALU sample > kRtpMaxPayloadSize (1200 bytes)
        vector<SrsNaluSample *> samples;

        // Create single large IDR sample (10240 bytes) that will be fragmented
        SrsNaluSample *large_idr_sample = new SrsNaluSample();
        large_idr_sample->size_ = 10240;
        large_idr_sample->bytes_ = (char *)malloc(large_idr_sample->size_);
        // Fill with IDR NALU header and dummy data
        large_idr_sample->bytes_[0] = 0x65; // IDR NALU type
        for (int i = 1; i < large_idr_sample->size_; i++) {
            large_idr_sample->bytes_[i] = (char)(0x80 + (i % 128));
        }
        samples.push_back(large_idr_sample);

        // Total size: 10240 bytes >> kRtpMaxPayloadSize (1200)

        // Create RTP packets vector
        vector<SrsRtpPacket *> pkts;

        // Test package_nalus method - should create multiple FU-A packets due to large size
        HELPER_ASSERT_SUCCESS(builder.package_nalus(&msg, samples, pkts));

        // Verify that multiple RTP packets were created (FU-A fragmentation)
        EXPECT_GT((int)pkts.size(), 1);

        // Verify all packets have correct RTP header settings
        for (size_t i = 0; i < pkts.size(); i++) {
            SrsRtpPacket *pkt = pkts[i];

            // Verify RTP header settings
            EXPECT_EQ(96, pkt->header_.get_payload_type());
            EXPECT_EQ(0x87654321, pkt->header_.get_ssrc());
            EXPECT_EQ(SrsFrameTypeVideo, pkt->frame_type_);
            EXPECT_EQ(400 + i, pkt->header_.get_sequence()); // Sequence increments
            EXPECT_EQ(360000, pkt->header_.get_timestamp()); // 4000 * 90

            // Verify NALU type is FU-A for fragmented packets
            EXPECT_EQ(kFuA, pkt->nalu_type_);

            // Verify payload type is FUA
            EXPECT_EQ(SrsRtpPacketPayloadTypeFUA, pkt->payload_type_);

            // Verify payload exists
            ASSERT_TRUE(pkt->payload_ != NULL);
            SrsRtpFUAPayload *fua_payload = static_cast<SrsRtpFUAPayload *>(pkt->payload_);

            // Verify FU-A header settings
            EXPECT_EQ(SrsAvcNaluTypeIDR, fua_payload->nalu_type_); // First NALU type

            // First packet should have start=true, end=false
            // Last packet should have start=false, end=true
            // Middle packets should have start=false, end=false
            if (i == 0) {
                EXPECT_TRUE(fua_payload->start_);
                EXPECT_FALSE(fua_payload->end_);
            } else if (i == pkts.size() - 1) {
                EXPECT_FALSE(fua_payload->start_);
                EXPECT_TRUE(fua_payload->end_);
            } else {
                EXPECT_FALSE(fua_payload->start_);
                EXPECT_FALSE(fua_payload->end_);
            }

            // Verify payload has NALU samples
            EXPECT_GT((int)fua_payload->nalus_.size(), 0);
        }

        // Cleanup RTP packets
        for (size_t i = 0; i < pkts.size(); i++) {
            srs_freep(pkts[i]);
        }

        // Cleanup sample
        for (size_t i = 0; i < samples.size(); i++) {
            SrsNaluSample *sample = samples[i];
            if (sample->bytes_) {
                free(sample->bytes_);
            }
            srs_freep(sample);
        }

        // Cleanup format
        srs_freep(format.vcodec_);
    }
}

VOID TEST(RTPVideoBuilderTest, PackageNalusLargePayloadHevc)
{
    srs_error_t err;

    // Test package_nalus method with large HEVC payload that exceeds kRtpMaxPayloadSize (1200 bytes)
    // This should trigger HEVC FU-A fragmentation path
    if (true) {
        // Create mock SrsRtpVideoBuilder
        SrsRtpVideoBuilder builder;
        builder.video_payload_type_ = 96;
        builder.video_ssrc_ = 0x11223344;
        builder.video_sequence_ = 500;

        // Create mock SrsFormat with HEVC codec
        SrsFormat format;
        format.vcodec_ = new SrsVideoCodecConfig();
        format.vcodec_->id_ = SrsVideoCodecIdHEVC;

        // Set format in builder
        builder.format_ = &format;

        // Create mock media packet
        SrsMediaPacket msg;
        msg.timestamp_ = 5000;

        // Create vector with single large HEVC IDR NALU sample > kRtpMaxPayloadSize (1200 bytes)
        vector<SrsNaluSample *> samples;

        // Create single large HEVC IDR sample (10240 bytes) that will be fragmented
        SrsNaluSample *large_hevc_idr_sample = new SrsNaluSample();
        large_hevc_idr_sample->size_ = 10240;
        large_hevc_idr_sample->bytes_ = (char *)malloc(large_hevc_idr_sample->size_);
        // Fill with HEVC IDR NALU header and dummy data
        // HEVC IDR NALU type is 19 (0x13), with layer_id=0, temporal_id=1
        large_hevc_idr_sample->bytes_[0] = (19 << 1) | 0; // NALU type 19, layer_id bit 0
        large_hevc_idr_sample->bytes_[1] = 1;             // layer_id bits + temporal_id
        for (int i = 2; i < large_hevc_idr_sample->size_; i++) {
            large_hevc_idr_sample->bytes_[i] = (char)(0x90 + (i % 128));
        }
        samples.push_back(large_hevc_idr_sample);

        // Total size: 10240 bytes >> kRtpMaxPayloadSize (1200)

        // Create RTP packets vector
        vector<SrsRtpPacket *> pkts;

        // Test package_nalus method - should create multiple HEVC FU-A packets due to large size
        HELPER_ASSERT_SUCCESS(builder.package_nalus(&msg, samples, pkts));

        // Verify that multiple RTP packets were created (HEVC FU-A fragmentation)
        EXPECT_GT((int)pkts.size(), 1);

        // Verify all packets have correct RTP header settings
        for (size_t i = 0; i < pkts.size(); i++) {
            SrsRtpPacket *pkt = pkts[i];

            // Verify RTP header settings
            EXPECT_EQ(96, pkt->header_.get_payload_type());
            EXPECT_EQ(0x11223344, pkt->header_.get_ssrc());
            EXPECT_EQ(SrsFrameTypeVideo, pkt->frame_type_);
            EXPECT_EQ(500 + i, pkt->header_.get_sequence()); // Sequence increments
            EXPECT_EQ(450000, pkt->header_.get_timestamp()); // 5000 * 90

            // Verify NALU type is FU-A for fragmented packets
            EXPECT_EQ(kFuA, pkt->nalu_type_);

            // Verify payload type is HEVC FUA
            EXPECT_EQ(SrsRtpPacketPayloadTypeFUAHevc, pkt->payload_type_);

            // Verify payload exists
            ASSERT_TRUE(pkt->payload_ != NULL);
            SrsRtpFUAPayloadHevc *fua_payload = static_cast<SrsRtpFUAPayloadHevc *>(pkt->payload_);

            // Verify HEVC FU-A header settings
            EXPECT_EQ(SrsHevcNaluType_CODED_SLICE_IDR, fua_payload->nalu_type_); // First NALU type (19)

            // First packet should have start=true, end=false
            // Last packet should have start=false, end=true
            // Middle packets should have start=false, end=false
            if (i == 0) {
                EXPECT_TRUE(fua_payload->start_);
                EXPECT_FALSE(fua_payload->end_);
            } else if (i == pkts.size() - 1) {
                EXPECT_FALSE(fua_payload->start_);
                EXPECT_TRUE(fua_payload->end_);
            } else {
                EXPECT_FALSE(fua_payload->start_);
                EXPECT_FALSE(fua_payload->end_);
            }

            // Verify payload has NALU samples
            EXPECT_GT((int)fua_payload->nalus_.size(), 0);
        }

        // Cleanup RTP packets
        for (size_t i = 0; i < pkts.size(); i++) {
            srs_freep(pkts[i]);
        }

        // Cleanup sample
        for (size_t i = 0; i < samples.size(); i++) {
            SrsNaluSample *sample = samples[i];
            if (sample->bytes_) {
                free(sample->bytes_);
            }
            srs_freep(sample);
        }

        // Cleanup format
        srs_freep(format.vcodec_);
    }
}

VOID TEST(RTPVideoBuilderTest, PackageNalusMultipleSamplesLargePayload)
{
    srs_error_t err;

    // Test package_nalus method with multiple IDR NALU samples in one frame that exceed kRtpMaxPayloadSize
    // This simulates a real IDR frame with multiple IDR NALU samples
    if (true) {
        // Create mock SrsRtpVideoBuilder
        SrsRtpVideoBuilder builder;
        builder.video_payload_type_ = 96;
        builder.video_ssrc_ = 0xAABBCCDD;
        builder.video_sequence_ = 600;

        // Create mock SrsFormat with H.264 codec
        SrsFormat format;
        format.vcodec_ = new SrsVideoCodecConfig();
        format.vcodec_->id_ = SrsVideoCodecIdAVC;

        // Set format in builder
        builder.format_ = &format;

        // Create mock media packet
        SrsMediaPacket msg;
        msg.timestamp_ = 6000;

        // Create vector with multiple IDR NALU samples that together exceed kRtpMaxPayloadSize
        vector<SrsNaluSample *> samples;

        // Create first IDR NALU sample (3000 bytes)
        SrsNaluSample *idr_sample1 = new SrsNaluSample();
        idr_sample1->size_ = 3000;
        idr_sample1->bytes_ = (char *)malloc(idr_sample1->size_);
        idr_sample1->bytes_[0] = 0x65; // IDR NALU type
        for (int i = 1; i < idr_sample1->size_; i++) {
            idr_sample1->bytes_[i] = (char)(0x10 + (i % 128));
        }
        samples.push_back(idr_sample1);

        // Create second IDR NALU sample (2500 bytes)
        SrsNaluSample *idr_sample2 = new SrsNaluSample();
        idr_sample2->size_ = 2500;
        idr_sample2->bytes_ = (char *)malloc(idr_sample2->size_);
        idr_sample2->bytes_[0] = 0x65; // IDR NALU type
        for (int i = 1; i < idr_sample2->size_; i++) {
            idr_sample2->bytes_[i] = (char)(0x20 + (i % 128));
        }
        samples.push_back(idr_sample2);

        // Create third IDR NALU sample (4000 bytes)
        SrsNaluSample *idr_sample3 = new SrsNaluSample();
        idr_sample3->size_ = 4000;
        idr_sample3->bytes_ = (char *)malloc(idr_sample3->size_);
        idr_sample3->bytes_[0] = 0x65; // IDR NALU type
        for (int i = 1; i < idr_sample3->size_; i++) {
            idr_sample3->bytes_[i] = (char)(0x30 + (i % 128));
        }
        samples.push_back(idr_sample3);

        // Total size: 3000 + 2500 + 4000 + 6 separators = 9506 bytes >> kRtpMaxPayloadSize (1200)

        // Create RTP packets vector
        vector<SrsRtpPacket *> pkts;

        // Test package_nalus method - should create multiple FU-A packets due to large total size
        HELPER_ASSERT_SUCCESS(builder.package_nalus(&msg, samples, pkts));

        // Verify that multiple RTP packets were created (FU-A fragmentation)
        EXPECT_GT((int)pkts.size(), 1);

        // Verify all packets have correct RTP header settings
        for (size_t i = 0; i < pkts.size(); i++) {
            SrsRtpPacket *pkt = pkts[i];

            // Verify RTP header settings
            EXPECT_EQ(96, pkt->header_.get_payload_type());
            EXPECT_EQ(0xAABBCCDD, pkt->header_.get_ssrc());
            EXPECT_EQ(SrsFrameTypeVideo, pkt->frame_type_);
            EXPECT_EQ(600 + i, pkt->header_.get_sequence()); // Sequence increments
            EXPECT_EQ(540000, pkt->header_.get_timestamp()); // 6000 * 90

            // Verify NALU type is FU-A for fragmented packets
            EXPECT_EQ(kFuA, pkt->nalu_type_);

            // Verify payload type is FUA
            EXPECT_EQ(SrsRtpPacketPayloadTypeFUA, pkt->payload_type_);

            // Verify payload exists
            ASSERT_TRUE(pkt->payload_ != NULL);
            SrsRtpFUAPayload *fua_payload = static_cast<SrsRtpFUAPayload *>(pkt->payload_);

            // Verify FU-A header settings - NALU type should be from first NALU (IDR)
            EXPECT_EQ(SrsAvcNaluTypeIDR, fua_payload->nalu_type_); // First NALU type

            // First packet should have start=true, end=false
            // Last packet should have start=false, end=true
            // Middle packets should have start=false, end=false
            if (i == 0) {
                EXPECT_TRUE(fua_payload->start_);
                EXPECT_FALSE(fua_payload->end_);
            } else if (i == pkts.size() - 1) {
                EXPECT_FALSE(fua_payload->start_);
                EXPECT_TRUE(fua_payload->end_);
            } else {
                EXPECT_FALSE(fua_payload->start_);
                EXPECT_FALSE(fua_payload->end_);
            }

            // Verify payload has NALU samples
            EXPECT_GT((int)fua_payload->nalus_.size(), 0);
        }

        // Cleanup RTP packets
        for (size_t i = 0; i < pkts.size(); i++) {
            srs_freep(pkts[i]);
        }

        // Cleanup samples
        for (size_t i = 0; i < samples.size(); i++) {
            SrsNaluSample *sample = samples[i];
            if (sample->bytes_) {
                free(sample->bytes_);
            }
            srs_freep(sample);
        }

        // Cleanup format
        srs_freep(format.vcodec_);
    }
}

VOID TEST(RTPVideoBuilderTest, PackageNalusMultipleSamplesLargePayloadHevc)
{
    srs_error_t err;

    // Test package_nalus method with multiple HEVC IDR NALU samples in one frame that exceed kRtpMaxPayloadSize
    // This simulates a real HEVC IDR frame with multiple IDR NALU samples
    if (true) {
        // Create mock SrsRtpVideoBuilder
        SrsRtpVideoBuilder builder;
        builder.video_payload_type_ = 96;
        builder.video_ssrc_ = 0x12345678;
        builder.video_sequence_ = 700;

        // Create mock SrsFormat with HEVC codec
        SrsFormat format;
        format.vcodec_ = new SrsVideoCodecConfig();
        format.vcodec_->id_ = SrsVideoCodecIdHEVC;

        // Set format in builder
        builder.format_ = &format;

        // Create mock media packet
        SrsMediaPacket msg;
        msg.timestamp_ = 7000;

        // Create vector with multiple HEVC IDR NALU samples that together exceed kRtpMaxPayloadSize
        vector<SrsNaluSample *> samples;

        // Create first HEVC IDR NALU sample (2800 bytes)
        SrsNaluSample *idr_sample1 = new SrsNaluSample();
        idr_sample1->size_ = 2800;
        idr_sample1->bytes_ = (char *)malloc(idr_sample1->size_);
        // HEVC IDR NALU type is 19, with layer_id=0, temporal_id=1
        idr_sample1->bytes_[0] = (19 << 1) | 0; // NALU type 19, layer_id bit 0
        idr_sample1->bytes_[1] = 1;             // layer_id bits + temporal_id
        for (int i = 2; i < idr_sample1->size_; i++) {
            idr_sample1->bytes_[i] = (char)(0x10 + (i % 128));
        }
        samples.push_back(idr_sample1);

        // Create second HEVC IDR NALU sample (3200 bytes)
        SrsNaluSample *idr_sample2 = new SrsNaluSample();
        idr_sample2->size_ = 3200;
        idr_sample2->bytes_ = (char *)malloc(idr_sample2->size_);
        // HEVC IDR NALU type is 19, with layer_id=0, temporal_id=1
        idr_sample2->bytes_[0] = (19 << 1) | 0; // NALU type 19, layer_id bit 0
        idr_sample2->bytes_[1] = 1;             // layer_id bits + temporal_id
        for (int i = 2; i < idr_sample2->size_; i++) {
            idr_sample2->bytes_[i] = (char)(0x20 + (i % 128));
        }
        samples.push_back(idr_sample2);

        // Create third HEVC IDR NALU sample (2600 bytes)
        SrsNaluSample *idr_sample3 = new SrsNaluSample();
        idr_sample3->size_ = 2600;
        idr_sample3->bytes_ = (char *)malloc(idr_sample3->size_);
        // HEVC IDR NALU type is 19, with layer_id=0, temporal_id=1
        idr_sample3->bytes_[0] = (19 << 1) | 0; // NALU type 19, layer_id bit 0
        idr_sample3->bytes_[1] = 1;             // layer_id bits + temporal_id
        for (int i = 2; i < idr_sample3->size_; i++) {
            idr_sample3->bytes_[i] = (char)(0x30 + (i % 128));
        }
        samples.push_back(idr_sample3);

        // Total size: 2800 + 3200 + 2600 + 6 separators = 8606 bytes >> kRtpMaxPayloadSize (1200)

        // Create RTP packets vector
        vector<SrsRtpPacket *> pkts;

        // Test package_nalus method - should create multiple HEVC FU-A packets due to large total size
        HELPER_ASSERT_SUCCESS(builder.package_nalus(&msg, samples, pkts));

        // Verify that multiple RTP packets were created (HEVC FU-A fragmentation)
        EXPECT_GT((int)pkts.size(), 1);

        // Verify all packets have correct RTP header settings
        for (size_t i = 0; i < pkts.size(); i++) {
            SrsRtpPacket *pkt = pkts[i];

            // Verify RTP header settings
            EXPECT_EQ(96, pkt->header_.get_payload_type());
            EXPECT_EQ(0x12345678, pkt->header_.get_ssrc());
            EXPECT_EQ(SrsFrameTypeVideo, pkt->frame_type_);
            EXPECT_EQ(700 + i, pkt->header_.get_sequence()); // Sequence increments
            EXPECT_EQ(630000, pkt->header_.get_timestamp()); // 7000 * 90

            // Verify NALU type is FU-A for fragmented packets
            EXPECT_EQ(kFuA, pkt->nalu_type_);

            // Verify payload type is HEVC FUA
            EXPECT_EQ(SrsRtpPacketPayloadTypeFUAHevc, pkt->payload_type_);

            // Verify payload exists
            ASSERT_TRUE(pkt->payload_ != NULL);
            SrsRtpFUAPayloadHevc *fua_payload = static_cast<SrsRtpFUAPayloadHevc *>(pkt->payload_);

            // Verify HEVC FU-A header settings - NALU type should be from first NALU (IDR)
            EXPECT_EQ(SrsHevcNaluType_CODED_SLICE_IDR, fua_payload->nalu_type_); // First NALU type (19)

            // First packet should have start=true, end=false
            // Last packet should have start=false, end=true
            // Middle packets should have start=false, end=false
            if (i == 0) {
                EXPECT_TRUE(fua_payload->start_);
                EXPECT_FALSE(fua_payload->end_);
            } else if (i == pkts.size() - 1) {
                EXPECT_FALSE(fua_payload->start_);
                EXPECT_TRUE(fua_payload->end_);
            } else {
                EXPECT_FALSE(fua_payload->start_);
                EXPECT_FALSE(fua_payload->end_);
            }

            // Verify payload has NALU samples
            EXPECT_GT((int)fua_payload->nalus_.size(), 0);
        }

        // Cleanup RTP packets
        for (size_t i = 0; i < pkts.size(); i++) {
            srs_freep(pkts[i]);
        }

        // Cleanup samples
        for (size_t i = 0; i < samples.size(); i++) {
            SrsNaluSample *sample = samples[i];
            if (sample->bytes_) {
                free(sample->bytes_);
            }
            srs_freep(sample);
        }

        // Cleanup format
        srs_freep(format.vcodec_);
    }
}

VOID TEST(RTPVideoBuilderTest, PackageFuAHevc)
{
    srs_error_t err;

    // Test package_fu_a method with HEVC codec - covers SrsRtpFUAPayloadHevc2 code path
    if (true) {
        // Create mock SrsRtpVideoBuilder
        SrsRtpVideoBuilder builder;
        builder.video_payload_type_ = 96;
        builder.video_ssrc_ = 0x11223344;
        builder.video_sequence_ = 800;

        // Create mock SrsFormat with HEVC codec
        SrsFormat format;
        format.vcodec_ = new SrsVideoCodecConfig();
        format.vcodec_->id_ = SrsVideoCodecIdHEVC;

        // Set format in builder
        builder.format_ = &format;

        // Create mock media packet
        SrsMediaPacket msg;
        msg.timestamp_ = 8000;

        // Create large HEVC IDR NALU sample that will be fragmented (3000 bytes)
        SrsNaluSample *sample = new SrsNaluSample();
        sample->size_ = 3000;
        sample->bytes_ = (char *)malloc(sample->size_);
        // HEVC IDR NALU type is 19, with layer_id=0, temporal_id=1
        sample->bytes_[0] = (19 << 1) | 0; // NALU type 19, layer_id bit 0
        sample->bytes_[1] = 1;             // layer_id bits + temporal_id
        for (int i = 2; i < sample->size_; i++) {
            sample->bytes_[i] = (char)(0x50 + (i % 128));
        }

        // Create RTP packets vector
        vector<SrsRtpPacket *> pkts;

        // Test package_fu_a method with payload size that will create multiple fragments
        int fu_payload_size = 1000; // Smaller than sample size to force fragmentation
        HELPER_ASSERT_SUCCESS(builder.package_fu_a(&msg, sample, fu_payload_size, pkts));

        // Verify that multiple RTP packets were created (FU-A fragmentation)
        EXPECT_GT((int)pkts.size(), 1);
        int expected_packets = 1 + (sample->size_ - SrsHevcNaluHeaderSize - 1) / fu_payload_size;
        EXPECT_EQ(expected_packets, (int)pkts.size());

        // Verify all packets have correct RTP header settings and HEVC FU-A payload
        for (size_t i = 0; i < pkts.size(); i++) {
            SrsRtpPacket *pkt = pkts[i];

            // Verify RTP header settings
            EXPECT_EQ(96, pkt->header_.get_payload_type());
            EXPECT_EQ(0x11223344, pkt->header_.get_ssrc());
            EXPECT_EQ(SrsFrameTypeVideo, pkt->frame_type_);
            EXPECT_EQ(800 + i, pkt->header_.get_sequence()); // Sequence increments
            EXPECT_EQ(720000, pkt->header_.get_timestamp()); // 8000 * 90

            // Verify NALU type is HEVC FU-A for fragmented packets
            EXPECT_EQ(kFuHevc, pkt->nalu_type_);

            // Verify payload type is HEVC FUA2
            EXPECT_EQ(SrsRtpPacketPayloadTypeFUAHevc2, pkt->payload_type_);

            // Verify payload exists and is SrsRtpFUAPayloadHevc2
            ASSERT_TRUE(pkt->payload_ != NULL);
            SrsRtpFUAPayloadHevc2 *fua_payload = static_cast<SrsRtpFUAPayloadHevc2 *>(pkt->payload_);

            // Verify HEVC FU-A header settings - NALU type should be IDR (19)
            EXPECT_EQ(SrsHevcNaluType_CODED_SLICE_IDR, fua_payload->nalu_type_);

            // First packet should have start=true, end=false
            // Last packet should have start=false, end=true
            // Middle packets should have start=false, end=false
            if (i == 0) {
                EXPECT_TRUE(fua_payload->start_);
                EXPECT_FALSE(fua_payload->end_);
            } else if (i == pkts.size() - 1) {
                EXPECT_FALSE(fua_payload->start_);
                EXPECT_TRUE(fua_payload->end_);
            } else {
                EXPECT_FALSE(fua_payload->start_);
                EXPECT_FALSE(fua_payload->end_);
            }

            // Verify payload points to correct data and has correct size
            EXPECT_TRUE(fua_payload->payload_ != NULL);
            EXPECT_GT(fua_payload->size_, 0);
            EXPECT_LE(fua_payload->size_, fu_payload_size);

            // For last packet, verify it contains remaining data
            if (i == pkts.size() - 1) {
                int remaining_size = sample->size_ - SrsHevcNaluHeaderSize - (int)i * fu_payload_size;
                EXPECT_EQ(remaining_size, fua_payload->size_);
            } else {
                EXPECT_EQ(fu_payload_size, fua_payload->size_);
            }
        }

        // Cleanup RTP packets
        for (size_t i = 0; i < pkts.size(); i++) {
            srs_freep(pkts[i]);
        }

        // Cleanup sample
        if (sample->bytes_) {
            free(sample->bytes_);
        }
        srs_freep(sample);

        // Cleanup format
        srs_freep(format.vcodec_);
    }
}

VOID TEST(RTPVideoBuilderTest, PackageFuAH264)
{
    srs_error_t err;

    // Test package_fu_a method with H.264 codec - covers SrsRtpFUAPayload2 code path
    if (true) {
        // Create mock SrsRtpVideoBuilder
        SrsRtpVideoBuilder builder;
        builder.video_payload_type_ = 96;
        builder.video_ssrc_ = 0x55667788;
        builder.video_sequence_ = 900;

        // Create mock SrsFormat with H.264 codec
        SrsFormat format;
        format.vcodec_ = new SrsVideoCodecConfig();
        format.vcodec_->id_ = SrsVideoCodecIdAVC;

        // Set format in builder
        builder.format_ = &format;

        // Create mock media packet
        SrsMediaPacket msg;
        msg.timestamp_ = 9000;

        // Create large H.264 IDR NALU sample that will be fragmented (2500 bytes)
        SrsNaluSample *sample = new SrsNaluSample();
        sample->size_ = 2500;
        sample->bytes_ = (char *)malloc(sample->size_);
        sample->bytes_[0] = 0x65; // H.264 IDR NALU type
        for (int i = 1; i < sample->size_; i++) {
            sample->bytes_[i] = (char)(0x60 + (i % 128));
        }

        // Create RTP packets vector
        vector<SrsRtpPacket *> pkts;

        // Test package_fu_a method with payload size that will create multiple fragments
        int fu_payload_size = 800; // Smaller than sample size to force fragmentation
        HELPER_ASSERT_SUCCESS(builder.package_fu_a(&msg, sample, fu_payload_size, pkts));

        // Verify that multiple RTP packets were created (FU-A fragmentation)
        EXPECT_GT((int)pkts.size(), 1);
        int expected_packets = 1 + (sample->size_ - SrsAvcNaluHeaderSize - 1) / fu_payload_size;
        EXPECT_EQ(expected_packets, (int)pkts.size());

        // Verify all packets have correct RTP header settings and H.264 FU-A payload
        for (size_t i = 0; i < pkts.size(); i++) {
            SrsRtpPacket *pkt = pkts[i];

            // Verify RTP header settings
            EXPECT_EQ(96, pkt->header_.get_payload_type());
            EXPECT_EQ(0x55667788, pkt->header_.get_ssrc());
            EXPECT_EQ(SrsFrameTypeVideo, pkt->frame_type_);
            EXPECT_EQ(900 + i, pkt->header_.get_sequence()); // Sequence increments
            EXPECT_EQ(810000, pkt->header_.get_timestamp()); // 9000 * 90

            // Verify NALU type is H.264 FU-A for fragmented packets
            EXPECT_EQ(kFuA, pkt->nalu_type_);

            // Verify payload type is H.264 FUA2
            EXPECT_EQ(SrsRtpPacketPayloadTypeFUA2, pkt->payload_type_);

            // Verify payload exists and is SrsRtpFUAPayload2
            ASSERT_TRUE(pkt->payload_ != NULL);
            SrsRtpFUAPayload2 *fua_payload = static_cast<SrsRtpFUAPayload2 *>(pkt->payload_);

            // Verify H.264 FU-A header settings - NALU type should be IDR (5)
            EXPECT_EQ(SrsAvcNaluTypeIDR, fua_payload->nalu_type_);
            EXPECT_EQ((SrsAvcNaluType)0x65, fua_payload->nri_); // NRI from original header

            // First packet should have start=true, end=false
            // Last packet should have start=false, end=true
            // Middle packets should have start=false, end=false
            if (i == 0) {
                EXPECT_TRUE(fua_payload->start_);
                EXPECT_FALSE(fua_payload->end_);
            } else if (i == pkts.size() - 1) {
                EXPECT_FALSE(fua_payload->start_);
                EXPECT_TRUE(fua_payload->end_);
            } else {
                EXPECT_FALSE(fua_payload->start_);
                EXPECT_FALSE(fua_payload->end_);
            }

            // Verify payload points to correct data and has correct size
            EXPECT_TRUE(fua_payload->payload_ != NULL);
            EXPECT_GT(fua_payload->size_, 0);
            EXPECT_LE(fua_payload->size_, fu_payload_size);

            // For last packet, verify it contains remaining data
            if (i == pkts.size() - 1) {
                int remaining_size = sample->size_ - SrsAvcNaluHeaderSize - (int)i * fu_payload_size;
                EXPECT_EQ(remaining_size, fua_payload->size_);
            } else {
                EXPECT_EQ(fu_payload_size, fua_payload->size_);
            }
        }

        // Cleanup RTP packets
        for (size_t i = 0; i < pkts.size(); i++) {
            srs_freep(pkts[i]);
        }

        // Cleanup sample
        if (sample->bytes_) {
            free(sample->bytes_);
        }
        srs_freep(sample);

        // Cleanup format
        srs_freep(format.vcodec_);
    }
}
