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

#include <srs_utest_workflow_rtmp_conn.hpp>

#include <srs_app_rtmp_conn.hpp>
#include <srs_protocol_conn.hpp>
#include <srs_protocol_io.hpp>
#include <srs_protocol_rtmp_stack.hpp>
#include <srs_protocol_st.hpp>
#include <srs_utest_ai11.hpp>
#include <srs_utest_ai15.hpp>
#include <srs_utest_ai18.hpp>
#include <srs_utest_ai22.hpp>
#include <srs_utest_manual_mock.hpp>
#include <srs_utest_manual_service.hpp>

#include <sys/socket.h>
#include <unistd.h>

// This test is used to verify the basic workflow of the RTMP connection.
// It's finished with the help of AI, but each step is manually designed
// and verified. So this is not dominated by AI, but by humanbeing.
VOID TEST(BasicWorkflowRtmpConnTest, ManuallyVerifyForPublisher)
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
#ifdef SRS_RTSP
    SrsUniquePtr<MockRtspSourceManager> mock_rtsp_sources(new MockRtspSourceManager());
#endif
    MockRtmpServer *mock_rtmp_server = new MockRtmpServer();
    MockSecurity *mock_security = new MockSecurity();

    mock_config->default_vhost_ = new SrsConfDirective();
    mock_config->default_vhost_->name_ = "vhost";
    mock_config->default_vhost_->args_.push_back("__defaultVhost__");

    mock_config->mw_msgs_ = 0;  // Handle each RTMP message, no merging write.
    mock_config->mw_sleep_ = 0; // Handle each RTMP message, no merging write.

    mock_rtmp_server->set_request(SrsRtmpConnFMLEPublish, "192.168.1.100", "utest.ossrs.io", "utest", "livestream", "rtmp://127.0.0.1/utest", "rtmp", 1935, "127.0.0.1");

    // Create SrsRtmpConn - it takes ownership of transport
    ISrsRtmpTransport *transport = new MockRtmpTransport();
    SrsUniquePtr<SrsRtmpConn> conn(new SrsRtmpConn(transport, "192.168.1.100", 1935));

    conn->config_ = mock_config.get();
    conn->manager_ = mock_manager.get();
    conn->live_sources_ = mock_sources.get();
    conn->stream_publish_tokens_ = mock_tokens.get();
    conn->stat_ = mock_stat.get();
    conn->hooks_ = mock_hooks.get();
    conn->rtc_sources_ = mock_rtc_sources.get();
    conn->srt_sources_ = mock_srt_sources.get();
#ifdef SRS_RTSP
    conn->rtsp_sources_ = mock_rtsp_sources.get();
#endif
    srs_freep(conn->rtmp_);
    conn->rtmp_ = mock_rtmp_server;
    srs_freep(conn->security_);
    conn->security_ = mock_security;

    // Start the RTMP connection.
    if (true) {
        // Mock the client type to be a player
        HELPER_EXPECT_SUCCESS(conn->start());

        // Wait for coroutine to start.
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);

        // Verify the req should be parsed.
        ISrsRequest *req = conn->info_->req_;
        EXPECT_STREQ("192.168.1.100", req->ip_.c_str());
        EXPECT_STREQ("rtmp://127.0.0.1/utest", req->tcUrl_.c_str());
        EXPECT_STREQ("rtmp", req->schema_.c_str());
        EXPECT_STREQ("__defaultVhost__", req->vhost_.c_str());
        EXPECT_STREQ("127.0.0.1", req->host_.c_str());
        EXPECT_EQ(1935, req->port_);
        EXPECT_STREQ("utest", req->app_.c_str());
        EXPECT_STREQ("livestream", req->stream_.c_str());
        EXPECT_EQ(0, req->duration_);
        EXPECT_TRUE(NULL == req->args_);
        EXPECT_STREQ("rtmp", req->protocol_.c_str());
        EXPECT_FALSE(conn->info_->edge_);
    }

    // Create an RTMP audio message to feed consumer.
    MockLiveSource *mock_source = dynamic_cast<MockLiveSource *>(mock_sources->mock_source_.get());
    if (true) {
        // Create a real AAC audio message with proper format.
        // AAC audio format in RTMP/FLV:
        // Byte 0: (SoundFormat << 4) | (SoundRate << 2) | (SoundSize << 1) | SoundType
        //         SoundFormat=10 (AAC), SoundRate=3 (44kHz), SoundSize=1 (16-bit), SoundType=1 (stereo)
        //         = 0xAF
        // Byte 1: AACPacketType (0=sequence header, 1=raw data)
        // Remaining bytes: AAC data
        int payload_size = 10;
        SrsRtmpCommonMessage *msg = new SrsRtmpCommonMessage();
        msg->header_.initialize_audio(payload_size, 0, 1);
        msg->create_payload(payload_size);

        // Fill in AAC audio data
        SrsBuffer stream(msg->payload(), payload_size);
        // Audio format byte: AAC(10), 44kHz(3), 16-bit(1), stereo(1) = 0xAF
        stream.write_1bytes(0xAF);
        // AAC packet type: 1 = AAC raw data
        stream.write_1bytes(0x01);
        // AAC raw data (8 bytes of dummy audio data)
        for (int i = 0; i < 8; i++) {
            stream.write_1bytes(0x00);
        }

        // Feed audio to rtmp server.
        mock_rtmp_server->recv_msgs_.push_back(msg);
        mock_rtmp_server->cond_->signal();

        // Wait for consumer to process the message.
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);

        // Verify that the message is sent to the client.
        EXPECT_EQ(1, mock_source->on_audio_count_);
    }

    // Create an RTMP video message to feed consumer.
    if (true) {
        // Create a real H.264 video message with proper format.
        // H.264 video format in RTMP/FLV:
        // Byte 0: (FrameType << 4) | CodecID (CodecID=7 for H.264)
        //         FrameType=1 (key frame), CodecID=7 (H.264) = 0x17
        // Byte 1: AVCPacketType (0=sequence header, 1=NALU, 2=end of sequence)
        // Byte 2-4: CompositionTime (3bytes little-endian int24)
        // Remaining bytes: H.264 data
        int payload_size = 10;
        SrsRtmpCommonMessage *msg = new SrsRtmpCommonMessage();
        msg->header_.initialize_video(payload_size, 0, 1);
        msg->create_payload(payload_size);

        // Fill in H.264 video data
        SrsBuffer stream(msg->payload(), payload_size);
        // Frame type & Codec ID: Key frame (1) + H.264 (7) = 0x17
        stream.write_1bytes(0x17);
        // AVC packet type: 1 = NALU
        stream.write_1bytes(0x01);
        // Composition time: 0 (3bytes little-endian int24)
        stream.write_3bytes(0x000000);
        // H.264 raw data (5 bytes of dummy video data)
        for (int i = 0; i < 5; i++) {
            stream.write_1bytes(0x00);
        }

        // Feed audio to rtmp server.
        mock_rtmp_server->recv_msgs_.push_back(msg);
        mock_rtmp_server->cond_->signal();

        // Wait for consumer to process the message.
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);

        // Verify that the message is sent to the client.
        EXPECT_EQ(1, mock_source->on_video_count_);
    }

    // Simulate client quit event, the receive thread will get this error.
    if (true) {
        mock_rtmp_server->recv_err_ = srs_error_new(ERROR_SOCKET_READ, "mock client quit");
        mock_rtmp_server->cond_->signal();

        // Wait for coroutine to stop.
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);
    }

    // Stop the RTMP connection.
    conn->stop();
}

// This test is used to verify the basic workflow of the RTMP connection.
// It's finished with the help of AI, but each step is manually designed
// and verified. So this is not dominated by AI, but by humanbeing.
VOID TEST(BasicWorkflowRtmpConnTest, ManuallyVerifyForPlayer)
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
#ifdef SRS_RTSP
    SrsUniquePtr<MockRtspSourceManager> mock_rtsp_sources(new MockRtspSourceManager());
#endif
    MockRtmpServer *mock_rtmp_server = new MockRtmpServer();
    MockSecurity *mock_security = new MockSecurity();

    mock_config->default_vhost_ = new SrsConfDirective();
    mock_config->default_vhost_->name_ = "vhost";
    mock_config->default_vhost_->args_.push_back("__defaultVhost__");

    mock_config->mw_msgs_ = 0;  // Handle each RTMP message, no merging write.
    mock_config->mw_sleep_ = 0; // Handle each RTMP message, no merging write.

    mock_rtmp_server->type_ = SrsRtmpConnPlay;
    mock_rtmp_server->ip_ = "192.168.1.100";
    mock_rtmp_server->vhost_ = "utest.ossrs.io";
    mock_rtmp_server->app_ = "utest";
    mock_rtmp_server->stream_ = "livestream";
    mock_rtmp_server->tcUrl_ = "rtmp://127.0.0.1/utest";
    mock_rtmp_server->schema_ = "rtmp";
    mock_rtmp_server->port_ = 1935;
    mock_rtmp_server->host_ = "127.0.0.1";

    // Create SrsRtmpConn - it takes ownership of transport
    ISrsRtmpTransport *transport = new MockRtmpTransport();
    SrsUniquePtr<SrsRtmpConn> conn(new SrsRtmpConn(transport, "192.168.1.100", 1935));

    conn->config_ = mock_config.get();
    conn->manager_ = mock_manager.get();
    conn->live_sources_ = mock_sources.get();
    conn->stream_publish_tokens_ = mock_tokens.get();
    conn->stat_ = mock_stat.get();
    conn->hooks_ = mock_hooks.get();
    conn->rtc_sources_ = mock_rtc_sources.get();
    conn->srt_sources_ = mock_srt_sources.get();
#ifdef SRS_RTSP
    conn->rtsp_sources_ = mock_rtsp_sources.get();
#endif
    srs_freep(conn->rtmp_);
    conn->rtmp_ = mock_rtmp_server;
    srs_freep(conn->security_);
    conn->security_ = mock_security;

    // Start the RTMP connection.
    if (true) {
        // Mock the client type to be a player
        HELPER_EXPECT_SUCCESS(conn->start());

        // Wait for coroutine to start.
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);

        // Verify the req should be parsed.
        ISrsRequest *req = conn->info_->req_;
        EXPECT_STREQ("192.168.1.100", req->ip_.c_str());
        EXPECT_STREQ("rtmp://127.0.0.1/utest", req->tcUrl_.c_str());
        EXPECT_STREQ("rtmp", req->schema_.c_str());
        EXPECT_STREQ("__defaultVhost__", req->vhost_.c_str());
        EXPECT_STREQ("127.0.0.1", req->host_.c_str());
        EXPECT_EQ(1935, req->port_);
        EXPECT_STREQ("utest", req->app_.c_str());
        EXPECT_STREQ("livestream", req->stream_.c_str());
        EXPECT_EQ(0, req->duration_);
        EXPECT_TRUE(NULL == req->args_);
        EXPECT_STREQ("rtmp", req->protocol_.c_str());
        EXPECT_FALSE(conn->info_->edge_);
    }

    // Create an RTMP audio message to feed consumer.
    if (true) {
        // Create a real AAC audio message with proper format.
        // AAC audio format in RTMP/FLV:
        // Byte 0: (SoundFormat << 4) | (SoundRate << 2) | (SoundSize << 1) | SoundType
        //         SoundFormat=10 (AAC), SoundRate=3 (44kHz), SoundSize=1 (16-bit), SoundType=1 (stereo)
        //         = 0xAF
        // Byte 1: AACPacketType (0=sequence header, 1=raw data)
        // Remaining bytes: AAC data
        int payload_size = 10;
        SrsRtmpCommonMessage *msg = new SrsRtmpCommonMessage();
        msg->header_.initialize_audio(payload_size, 0, 1);
        msg->create_payload(payload_size);

        // Fill in AAC audio data
        SrsBuffer stream(msg->payload(), payload_size);
        // Audio format byte: AAC(10), 44kHz(3), 16-bit(1), stereo(1) = 0xAF
        stream.write_1bytes(0xAF);
        // AAC packet type: 1 = AAC raw data
        stream.write_1bytes(0x01);
        // AAC raw data (8 bytes of dummy audio data)
        for (int i = 0; i < 8; i++) {
            stream.write_1bytes(0x00);
        }

        // Feed audio to source.
        SrsLiveSource *source = mock_sources->mock_source_.get();
        HELPER_EXPECT_SUCCESS(source->on_audio(msg));

        // Wait for consumer to process the message.
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);
        // Sleep again because player coroutine yield control, so we need to wait for
        // it to run again.
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);

        // Verify that the message is sent to the client.
        EXPECT_EQ(1, mock_rtmp_server->nb_sent_messages_);
    }

    // Create an RTMP video message to feed consumer.
    if (true) {
        // Create a real H.264 video message with proper format.
        // H.264 video format in RTMP/FLV:
        // Byte 0: (FrameType << 4) | CodecID (CodecID=7 for H.264)
        //         FrameType=1 (key frame), CodecID=7 (H.264) = 0x17
        // Byte 1: AVCPacketType (0=sequence header, 1=NALU, 2=end of sequence)
        // Byte 2-4: CompositionTime (3bytes little-endian int24)
        // Remaining bytes: H.264 data
        int payload_size = 10;
        SrsRtmpCommonMessage *msg = new SrsRtmpCommonMessage();
        msg->header_.initialize_video(payload_size, 0, 1);
        msg->create_payload(payload_size);

        // Fill in H.264 video data
        SrsBuffer stream(msg->payload(), payload_size);
        // Frame type & Codec ID: Key frame (1) + H.264 (7) = 0x17
        stream.write_1bytes(0x17);
        // AVC packet type: 1 = NALU
        stream.write_1bytes(0x01);
        // Composition time: 0 (3bytes little-endian int24)
        stream.write_3bytes(0x000000);
        // H.264 raw data (5 bytes of dummy video data)
        for (int i = 0; i < 5; i++) {
            stream.write_1bytes(0x00);
        }

        // Feed video to source.
        SrsLiveSource *source = mock_sources->mock_source_.get();
        HELPER_EXPECT_SUCCESS(source->on_video(msg));

        // Wait for consumer to process the message.
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);
        // Sleep again because player coroutine yield control, so we need to wait for
        // it to run again.
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);

        // Verify that the message is sent to the client.
        EXPECT_EQ(2, mock_rtmp_server->nb_sent_messages_);
    }

    // Simulate client quit event, the receive thread will get this error.
    if (true) {
        mock_rtmp_server->recv_err_ = srs_error_new(ERROR_SOCKET_READ, "mock client quit");
        mock_rtmp_server->cond_->signal();

        // Wait for coroutine to stop.
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);
    }

    // Stop the RTMP connection.
    conn->stop();
}
