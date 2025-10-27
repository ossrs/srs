/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2025 Winlin
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <srs_utest_workflow_srt_conn.hpp>

#include <srs_app_srt_conn.hpp>
#include <srs_protocol_conn.hpp>
#include <srs_protocol_io.hpp>
#include <srs_protocol_st.hpp>
#include <srs_utest_ai11.hpp>
#include <srs_utest_ai18.hpp>
#include <srs_utest_manual_mock.hpp>
#include <srs_utest_manual_service.hpp>

#include <sys/socket.h>
#include <unistd.h>

// This test is used to verify the basic workflow of the SRT connection.
// It's finished with the help of AI, but each step is manually designed
// and verified. So this is not dominated by AI, but by humanbeing.
VOID TEST(BasicWorkflowSrtConnTest, ManuallyVerifyForPublisher)
{
    srs_error_t err;

    // Mock all interface dependencies
    SrsUniquePtr<MockAppConfig> mock_config(new MockAppConfig());
    SrsUniquePtr<MockConnectionManager> mock_manager(new MockConnectionManager());
    SrsUniquePtr<MockLiveSourceManager> mock_sources(new MockLiveSourceManager());
    SrsUniquePtr<MockStreamPublishTokenManager> mock_tokens(new MockStreamPublishTokenManager());
    SrsUniquePtr<MockAppStatistic> mock_stat(new MockAppStatistic());
    SrsUniquePtr<MockHttpHooks> mock_hooks(new MockHttpHooks());
    SrsUniquePtr<MockRtcSourceManager> mock_rtc_sources(new MockRtcSourceManager());
    SrsUniquePtr<MockSrtSourceManager> mock_srt_sources(new MockSrtSourceManager());
    MockSrtConnection *mock_srt_conn = new MockSrtConnection();
    MockSecurity *mock_security = new MockSecurity();

    mock_config->default_vhost_ = new SrsConfDirective();
    mock_config->default_vhost_->name_ = "vhost";
    mock_config->default_vhost_->args_.push_back("__defaultVhost__");

    // Enable SRT in mock config
    mock_config->srt_enabled_ = true;
    mock_config->srt_to_rtmp_ = false;
    mock_config->rtc_from_rtmp_ = false;

    // Configure SRT connection mock
    mock_srt_conn->streamid_ = "#!::h=127.0.0.1,r=live/livestream,m=publish";
    mock_srt_conn->srt_fd_ = 100;

    // Create SrsMpegtsSrtConn - it takes ownership of srt_conn
    SrsUniquePtr<SrsMpegtsSrtConn> conn(new SrsMpegtsSrtConn(mock_manager.get(), 100, "192.168.1.100", 9000));

    conn->config_ = mock_config.get();
    conn->stat_ = mock_stat.get();
    conn->stream_publish_tokens_ = mock_tokens.get();
    conn->srt_sources_ = mock_srt_sources.get();
    conn->live_sources_ = mock_sources.get();
    conn->rtc_sources_ = mock_rtc_sources.get();
    conn->hooks_ = mock_hooks.get();
    srs_freep(conn->srt_conn_);
    conn->srt_conn_ = mock_srt_conn;
    srs_freep(conn->security_);
    conn->security_ = mock_security;

    // Start the SRT connection.
    if (true) {
        HELPER_EXPECT_SUCCESS(conn->start());

        // Wait for coroutine to start.
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);

        // Verify the req should be parsed.
        ISrsRequest *req = conn->req_;
        EXPECT_STREQ("192.168.1.100", req->ip_.c_str());
        EXPECT_STREQ("__defaultVhost__", req->vhost_.c_str());
        EXPECT_STREQ("live", req->app_.c_str());
        EXPECT_STREQ("livestream", req->stream_.c_str());
    }

    // Create MPEG-TS packets to feed the SRT source.
    MockSrtSource *mock_srt_source = dynamic_cast<MockSrtSource *>(mock_srt_sources->mock_source_.get());
    if (true) {
        // Create a simple MPEG-TS packet (188 bytes)
        // This is a minimal TS packet structure for testing
        char ts_packet[188];
        memset(ts_packet, 0, sizeof(ts_packet));

        // TS packet header: sync byte (0x47) + flags
        ts_packet[0] = 0x47; // Sync byte
        ts_packet[1] = 0x40; // Payload unit start indicator
        ts_packet[2] = 0x00; // PID (0x000 = PAT)
        ts_packet[3] = 0x10; // Continuity counter

        // Simulate receiving TS packet
        mock_srt_conn->recv_msgs_.push_back(std::string(ts_packet, sizeof(ts_packet)));
        mock_srt_conn->cond_->signal();

        // Wait for processing
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);

        // Check message should be read by SRT recv thread.
        EXPECT_EQ(1, mock_srt_conn->read_count_);
        EXPECT_EQ(1, mock_srt_source->on_packet_count_);
    }

    // Simulate client quit event
    if (true) {
        mock_srt_conn->read_error_ = srs_error_new(ERROR_SOCKET_READ, "mock client quit");

        // Wait for coroutine to stop.
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);
    }

    // Stop the SRT connection.
    conn->stop();
}

// This test is used to verify the basic workflow of the SRT connection.
// It's finished with the help of AI, but each step is manually designed
// and verified. So this is not dominated by AI, but by humanbeing.
VOID TEST(BasicWorkflowSrtConnTest, ManuallyVerifyForPlayer)
{
    srs_error_t err;

    // Mock all interface dependencies
    SrsUniquePtr<MockAppConfig> mock_config(new MockAppConfig());
    SrsUniquePtr<MockConnectionManager> mock_manager(new MockConnectionManager());
    SrsUniquePtr<MockLiveSourceManager> mock_sources(new MockLiveSourceManager());
    SrsUniquePtr<MockStreamPublishTokenManager> mock_tokens(new MockStreamPublishTokenManager());
    SrsUniquePtr<MockAppStatistic> mock_stat(new MockAppStatistic());
    SrsUniquePtr<MockHttpHooks> mock_hooks(new MockHttpHooks());
    SrsUniquePtr<MockRtcSourceManager> mock_rtc_sources(new MockRtcSourceManager());
    SrsUniquePtr<MockSrtSourceManager> mock_srt_sources(new MockSrtSourceManager());
    MockSrtConnection *mock_srt_conn = new MockSrtConnection();
    MockSecurity *mock_security = new MockSecurity();

    mock_config->default_vhost_ = new SrsConfDirective();
    mock_config->default_vhost_->name_ = "vhost";
    mock_config->default_vhost_->args_.push_back("__defaultVhost__");

    // Enable SRT in mock config
    mock_config->srt_enabled_ = true;
    mock_config->srt_to_rtmp_ = false;
    mock_config->rtc_from_rtmp_ = false;

    // Configure SRT connection mock for play mode (m=request)
    mock_srt_conn->streamid_ = "#!::h=127.0.0.1,r=live/livestream,m=request";
    mock_srt_conn->srt_fd_ = 100;

    // Create SrsMpegtsSrtConn - it takes ownership of srt_conn
    SrsUniquePtr<SrsMpegtsSrtConn> conn(new SrsMpegtsSrtConn(mock_manager.get(), 100, "192.168.1.100", 9000));

    conn->config_ = mock_config.get();
    conn->stat_ = mock_stat.get();
    conn->stream_publish_tokens_ = mock_tokens.get();
    conn->srt_sources_ = mock_srt_sources.get();
    conn->live_sources_ = mock_sources.get();
    conn->rtc_sources_ = mock_rtc_sources.get();
    conn->hooks_ = mock_hooks.get();
    srs_freep(conn->srt_conn_);
    conn->srt_conn_ = mock_srt_conn;
    srs_freep(conn->security_);
    conn->security_ = mock_security;

    // Start the SRT connection.
    MockSrtSource *srt_source = dynamic_cast<MockSrtSource *>(mock_srt_sources->mock_source_.get());
    if (true) {
        HELPER_EXPECT_SUCCESS(conn->start());

        // Wait for coroutine to start.
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);

        // Verify the req should be parsed.
        ISrsRequest *req = conn->req_;
        EXPECT_STREQ("192.168.1.100", req->ip_.c_str());
        EXPECT_STREQ("__defaultVhost__", req->vhost_.c_str());
        EXPECT_STREQ("live", req->app_.c_str());
        EXPECT_STREQ("livestream", req->stream_.c_str());
        EXPECT_EQ(1, (int)srt_source->consumers_.size());
    }

    // Feed TS packets to the SRT source consumer.
    // Note: The consumer waits for queue_.size() > mw_min_msgs_ (which is 1),
    // so we need to enqueue 2 packets to trigger the signal, or wait for timeout.
    if (true) {
        // Create first MPEG-TS packet (188 bytes)
        char ts_packet1[188];
        memset(ts_packet1, 0, sizeof(ts_packet1));
        ts_packet1[0] = 0x47; // Sync byte
        ts_packet1[1] = 0x40; // Payload unit start indicator
        ts_packet1[2] = 0x00; // PID (0x000 = PAT)
        ts_packet1[3] = 0x10; // Continuity counter

        SrsUniquePtr<SrsSrtPacket> packet1(new SrsSrtPacket());
        packet1->wrap(ts_packet1, sizeof(ts_packet1));
        HELPER_EXPECT_SUCCESS(srt_source->on_packet(packet1.get()));
        EXPECT_EQ(1, srt_source->on_packet_count_);

        // Create second MPEG-TS packet to trigger consumer signal
        char ts_packet2[188];
        memset(ts_packet2, 0, sizeof(ts_packet2));
        ts_packet2[0] = 0x47; // Sync byte
        ts_packet2[1] = 0x40; // Payload unit start indicator
        ts_packet2[2] = 0x00; // PID (0x000 = PAT)
        ts_packet2[3] = 0x11; // Continuity counter (incremented)

        SrsUniquePtr<SrsSrtPacket> packet2(new SrsSrtPacket());
        packet2->wrap(ts_packet2, sizeof(ts_packet2));
        HELPER_EXPECT_SUCCESS(srt_source->on_packet(packet2.get()));
        EXPECT_EQ(2, srt_source->on_packet_count_);

        // Wait for consumer to process the messages.
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);

        // Verify that both packets are sent to the client.
        EXPECT_EQ(376, mock_srt_conn->send_bytes_);
    }

    // Simulate client quit event, the receive thread will get this error.
    if (true) {
        mock_srt_conn->read_error_ = srs_error_new(ERROR_SOCKET_READ, "mock client quit");
        mock_srt_conn->cond_->signal();

        // Wait for coroutine to stop.
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);
    }

    // Stop the SRT connection.
    conn->stop();
}
