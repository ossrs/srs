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

#include <srs_utest_workflow_forward.hpp>

#include <srs_app_utility.hpp>
#include <srs_kernel_error.hpp>

// Mock ISrsAppFactory implementation
MockAppFactoryForForwarder::MockAppFactoryForForwarder()
{
    mock_rtmp_client_ = NULL;
}

MockAppFactoryForForwarder::~MockAppFactoryForForwarder()
{
    // Don't free mock_rtmp_client_ - it's managed by the test
}

ISrsBasicRtmpClient *MockAppFactoryForForwarder::create_rtmp_client(std::string url, srs_utime_t cto, srs_utime_t sto)
{
    if (mock_rtmp_client_) {
        mock_rtmp_client_->set_url(url);
    }
    return mock_rtmp_client_;
}

// This test is used to verify the basic workflow of the forwarding.
// It's finished with the help of AI, but each step is manually designed
// and verified. So this is not dominated by AI, but by humanbeing.
VOID TEST(BasicWorkflowForwardTest, ManuallyVerifyForwardingHostport)
{
    srs_error_t err;

    // Create mock objects
    SrsUniquePtr<MockRequest> req(new MockRequest("test.vhost", "live", "stream1"));
    MockRtmpClient *mock_sdk = new MockRtmpClient();
    SrsUniquePtr<MockAppFactoryForForwarder> mock_factory(new MockAppFactoryForForwarder());
    mock_factory->mock_rtmp_client_ = mock_sdk;
    SrsUniquePtr<MockAppConfig> mock_config(new MockAppConfig());

    // Create forwarder
    SrsUniquePtr<MockOriginHub> mock_hub(new MockOriginHub());
    SrsUniquePtr<SrsForwarder> forwarder(new SrsForwarder(mock_hub.get()));

    forwarder->app_factory_ = mock_factory.get();
    forwarder->config_ = mock_config.get();

    // Configure destination with traditional host:port format
    std::string destination = "127.0.0.1:19350";

    // Step 1: Initialize forwarder with destination
    HELPER_EXPECT_SUCCESS(forwarder->initialize(req.get(), destination));
    EXPECT_STREQ("127.0.0.1:19350", forwarder->ep_forward_.c_str());

    // Generate a video sequenece header message.
    if (true) {
        // Create a real H.264 video message with proper format.
        // H.264 video format in RTMP/FLV:
        // Byte 0: (FrameType << 4) | CodecID (CodecID=7 for H.264)
        //         FrameType=5 (disposable inter frame), CodecID=7 (H.264) = 0x57
        // Byte 1: AVCPacketType (0=sequence header, 1=NALU, 2=end of sequence)
        // Byte 2-4: CompositionTime (3bytes little-endian int24)
        // Remaining bytes: H.264 data
        int payload_size = 10;
        SrsUniquePtr<SrsRtmpCommonMessage> msg(new SrsRtmpCommonMessage());
        msg->header_.initialize_video(payload_size, 0, 1);
        msg->create_payload(payload_size);

        // Fill in H.264 video data
        SrsBuffer stream(msg->payload(), payload_size);
        // Frame type & Codec ID: Disposable inter frame (5) + H.264 (7) = 0x57
        stream.write_1bytes(0x57);
        // AVC packet type: 0 = sequence header
        stream.write_1bytes(0x00);
        // Composition time: 0 (3bytes little-endian int24)
        stream.write_3bytes(0x000000);
        // H.264 raw data (5 bytes of dummy video data) - SPS and PPS
        for (int i = 0; i < 5; i++) {
            stream.write_1bytes(0x00);
        }

        // Convert to SrsMediaPacket
        SrsMediaPacket *pkt = new SrsMediaPacket();
        msg->to_msg(pkt);
        forwarder->sh_video_ = pkt;
    }

    // Generate the audio sequence header.
    if (true) {
        // Create a real AAC audio message with proper format.
        // AAC audio format in RTMP/FLV:
        // Byte 0: (SoundFormat << 4) | (SoundRate << 2) | (SoundSize << 1) | SoundType
        //         SoundFormat=10 (AAC), SoundRate=3 (44kHz), SoundSize=1 (16-bit), SoundType=1 (stereo)
        //         = 0xAF
        // Byte 1: AACPacketType (0=sequence header, 1=raw data)
        // Remaining bytes: AAC data
        int payload_size = 10;
        SrsUniquePtr<SrsRtmpCommonMessage> msg(new SrsRtmpCommonMessage());
        msg->header_.initialize_audio(payload_size, 0, 1);
        msg->create_payload(payload_size);

        // Fill in AAC audio data
        SrsBuffer stream(msg->payload(), payload_size);
        // Audio format byte: AAC(10), 44kHz(3), 16-bit(1), stereo(1) = 0xAF
        stream.write_1bytes(0xAF);
        // AAC packet type: 0 = sequence header
        stream.write_1bytes(0x00);
        // AAC sequence header data (8 bytes of dummy audio data)
        for (int i = 0; i < 8; i++) {
            stream.write_1bytes(0x00);
        }

        // Convert to SrsMediaPacket
        SrsMediaPacket *pkt = new SrsMediaPacket();
        msg->to_msg(pkt);
        forwarder->sh_audio_ = pkt;
    }

    // Step 2: Call on_publish to start forwarding
    HELPER_EXPECT_SUCCESS(forwarder->on_publish());

    // Wait for forwarder to start
    srs_usleep(1 * SRS_UTIME_MILLISECONDS);

    // Verify the forwarder.
    EXPECT_STREQ("rtmp://127.0.0.1:19350/live/stream1?vhost=test.vhost", mock_sdk->url_.c_str());
    EXPECT_EQ(2, mock_sdk->send_message_count_);

    // Generate an audio message.
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

        // Convert to SrsMediaPacket
        SrsUniquePtr<SrsMediaPacket> pkt(new SrsMediaPacket());
        msg->to_msg(pkt.get());
        HELPER_EXPECT_SUCCESS(forwarder->on_video(pkt.get()));

        // Use this message to wakeup the forwarder coroutine.
        mock_sdk->recv_msgs_.push_back(msg);
        mock_sdk->cond_->signal();

        // Wait for forwarder to process the message
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);

        // Verify that the message is sent to the server.
        EXPECT_EQ(1, mock_sdk->send_and_free_messages_count_);
    }

    // Notify forwarder to quit.
    mock_sdk->recv_err_ = srs_error_new(ERROR_SOCKET_READ, "mock client quit");
    mock_sdk->cond_->signal();

    // Stop the forwarder coroutine.
    forwarder->on_unpublish();

    // Wait for forwarder to stop
    srs_usleep(1 * SRS_UTIME_MILLISECONDS);
}

// This test is used to verify the forwarding with query parameters (tokens).
// It's finished with the help of AI, but each step is manually designed
// and verified. So this is not dominated by AI, but by humanbeing.
VOID TEST(BasicWorkflowForwardTest, ManuallyVerifyForwardingWithToken)
{
    srs_error_t err;

    // Create mock objects with query parameters in the request
    SrsUniquePtr<MockRequest> req(new MockRequest("test.vhost", "live", "stream1"));
    // Set query parameters that should be forwarded (e.g., authentication tokens)
    req->param_ = "?sdkappid=1007&userid=5fe6e61e&usersig=eJyToken123";

    MockRtmpClient *mock_sdk = new MockRtmpClient();
    SrsUniquePtr<MockAppFactoryForForwarder> mock_factory(new MockAppFactoryForForwarder());
    mock_factory->mock_rtmp_client_ = mock_sdk;
    SrsUniquePtr<MockAppConfig> mock_config(new MockAppConfig());

    // Create forwarder
    SrsUniquePtr<MockOriginHub> mock_hub(new MockOriginHub());
    SrsUniquePtr<SrsForwarder> forwarder(new SrsForwarder(mock_hub.get()));

    forwarder->app_factory_ = mock_factory.get();
    forwarder->config_ = mock_config.get();

    // Configure destination with traditional host:port format
    std::string destination = "127.0.0.1:19350";

    // Step 1: Initialize forwarder with destination
    HELPER_EXPECT_SUCCESS(forwarder->initialize(req.get(), destination));
    EXPECT_STREQ("127.0.0.1:19350", forwarder->ep_forward_.c_str());

    // Step 2: Call on_publish to start forwarding
    HELPER_EXPECT_SUCCESS(forwarder->on_publish());

    // Wait for forwarder to start
    srs_usleep(1 * SRS_UTIME_MILLISECONDS);

    // Verify the forwarder URL includes the query parameters from the original request
    // The expected URL should be: rtmp://127.0.0.1:19350/live/stream1?sdkappid=1007&userid=5fe6e61e&usersig=eJyToken123&vhost=test.vhost
    EXPECT_STREQ("rtmp://127.0.0.1:19350/live/stream1?sdkappid=1007&userid=5fe6e61e&usersig=eJyToken123&vhost=test.vhost", mock_sdk->url_.c_str());

    // Notify forwarder to quit.
    mock_sdk->recv_err_ = srs_error_new(ERROR_SOCKET_READ, "mock client quit");
    mock_sdk->cond_->signal();

    // Stop the forwarder coroutine.
    forwarder->on_unpublish();

    // Wait for forwarder to stop
    srs_usleep(1 * SRS_UTIME_MILLISECONDS);
}
