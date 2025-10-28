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

#include <srs_utest_workflow_rtmp2rtc.hpp>

#include <srs_app_factory.hpp>
#include <srs_app_rtc_codec.hpp>
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

// Mock app factory for RTMP to RTC workflow testing
class MockAppFactoryForRtmp2Rtc : public SrsAppFactory
{
public:
    MockAudioTranscoder *last_created_audio_transcoder_;

public:
    MockAppFactoryForRtmp2Rtc()
    {
        last_created_audio_transcoder_ = NULL;
    }

    virtual ~MockAppFactoryForRtmp2Rtc()
    {
        // Don't delete last_created_audio_transcoder_ here,
        // it is managed by RTP builder
    }

#ifdef SRS_FFMPEG_FIT
    virtual ISrsAudioTranscoder *create_audio_transcoder()
    {
        // Create a new mock audio transcoder each time
        MockAudioTranscoder *transcoder = new MockAudioTranscoder();
        last_created_audio_transcoder_ = transcoder;
        return transcoder;
    }
#endif
};

// This test is used to verify the basic workflow of the RTMP connection.
// It's finished with the help of AI, but each step is manually designed
// and verified. So this is not dominated by AI, but by humanbeing.
VOID TEST(BasicWorkflowRtmp2RtcTest, ManuallyVerifyTypicalScenario)
{
    srs_error_t err;

    // Mock all interface dependencies
    SrsUniquePtr<MockAppFactoryForRtmp2Rtc> mock_factory(new MockAppFactoryForRtmp2Rtc());
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

    mock_config->rtc_server_enabled_ = true;
    mock_config->rtc_enabled_ = true;
    mock_config->rtc_from_rtmp_ = true;

    mock_rtmp_server->set_request(SrsRtmpConnFMLEPublish, "192.168.1.100", "utest.ossrs.io", "utest", "livestream", "rtmp://127.0.0.1/utest", "rtmp", 1935, "127.0.0.1");

    // Create SrsRtmpConn - it takes ownership of transport
    ISrsRtmpTransport *transport = new MockRtmpTransport();
    SrsUniquePtr<SrsRtmpConn> conn(new SrsRtmpConn(transport, "192.168.1.100", 1935));

    conn->app_factory_ = mock_factory.get();
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
    MockLiveSource *mock_source = dynamic_cast<MockLiveSource *>(mock_sources->mock_source_.get());
    SrsRtmpBridge *bridge = NULL;
    SrsRtcRtpBuilder *builder = NULL;
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

        // Bridge must be created.
        bridge = dynamic_cast<SrsRtmpBridge *>(mock_source->rtmp_bridge_);
        EXPECT_TRUE(bridge != NULL);
        builder = bridge->rtp_builder_;
        EXPECT_TRUE(builder != NULL);

#ifdef SRS_FFMPEG_FIT
        // Verify that the mock factory created the audio transcoder
        EXPECT_TRUE(mock_factory->last_created_audio_transcoder_ != NULL);
#endif
    }

    // Send AAC sequence header first (required before raw data).
    MockRtcSource *mock_rtc_source = dynamic_cast<MockRtcSource *>(mock_rtc_sources->mock_source_.get());
    if (true) {
        // Create AAC sequence header message.
        // AAC audio format in RTMP/FLV:
        // Byte 0: (SoundFormat << 4) | (SoundRate << 2) | (SoundSize << 1) | SoundType
        //         SoundFormat=10 (AAC), SoundRate=3 (44kHz), SoundSize=1 (16-bit), SoundType=1 (stereo)
        //         = 0xAF
        // Byte 1: AACPacketType (0=sequence header, 1=raw data)
        // Remaining bytes: AudioSpecificConfig (ASC) data
        int payload_size = 4;
        SrsRtmpCommonMessage *msg = new SrsRtmpCommonMessage();
        msg->header_.initialize_audio(payload_size, 0, 1);
        msg->create_payload(payload_size);

        // Fill in AAC sequence header
        SrsBuffer stream(msg->payload(), payload_size);
        // Audio format byte: AAC(10), 44kHz(3), 16-bit(1), stereo(1) = 0xAF
        stream.write_1bytes(0xAF);
        // AAC packet type: 0 = AAC sequence header
        stream.write_1bytes(0x00);
        // AudioSpecificConfig (ASC) data: 0x12 0x10
        // This represents AAC-LC profile, 44.1kHz sample rate, 2 channels
        stream.write_1bytes(0x12);
        stream.write_1bytes(0x10);

        // Feed sequence header to rtmp server.
        mock_rtmp_server->recv_msgs_.push_back(msg);
        mock_rtmp_server->cond_->signal();

        // Wait for consumer to process the message.
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);

        // Verify that the sequence header is sent to the RTC source.
        EXPECT_EQ(1, mock_source->on_audio_count_);
        EXPECT_EQ(0, mock_rtc_source->on_rtp_count_);
    }

    // Send AAC raw data frame.
    if (true) {
        // Create AAC raw data message.
        // Byte 0: Audio format byte (0xAF)
        // Byte 1: AACPacketType (1=raw data)
        // Remaining bytes: AAC raw frame data
        int payload_size = 10;
        SrsRtmpCommonMessage *msg = new SrsRtmpCommonMessage();
        msg->header_.initialize_audio(payload_size, 23, 1); // 23ms timestamp for second frame
        msg->create_payload(payload_size);

        // Fill in AAC raw data
        SrsBuffer stream(msg->payload(), payload_size);
        // Audio format byte: AAC(10), 44kHz(3), 16-bit(1), stereo(1) = 0xAF
        stream.write_1bytes(0xAF);
        // AAC packet type: 1 = AAC raw data
        stream.write_1bytes(0x01);
        // AAC raw frame data (8 bytes of dummy audio data)
        for (int i = 0; i < 8; i++) {
            stream.write_1bytes(0x00);
        }

        // Feed raw data to rtmp server.
        mock_rtmp_server->recv_msgs_.push_back(msg);
        mock_rtmp_server->cond_->signal();

        // Wait for consumer to process the message.
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);

        // Verify that the raw data is sent to the client.
        EXPECT_EQ(2, mock_source->on_audio_count_);
        EXPECT_EQ(1, mock_rtc_source->on_rtp_count_);
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
