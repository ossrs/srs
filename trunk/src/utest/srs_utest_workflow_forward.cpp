/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2026 Winlin
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

#include <srs_app_rtmp_conn.hpp>
#include <srs_app_utility.hpp>
#include <srs_kernel_error.hpp>
#include <srs_utest_ai14.hpp>
#include <srs_utest_ai34.hpp>

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

MockHttpHooksForForwardBackendFailure::MockHttpHooksForForwardBackendFailure()
{
    on_forward_backend_count_ = 0;
    on_forward_backend_error_ = srs_success;
}

MockHttpHooksForForwardBackendFailure::~MockHttpHooksForForwardBackendFailure()
{
    srs_freep(on_forward_backend_error_);
}

srs_error_t MockHttpHooksForForwardBackendFailure::on_forward_backend(std::string url, ISrsRequest *req, std::vector<std::string> &rtmp_urls)
{
    on_forward_backend_count_++;
    return srs_error_copy(on_forward_backend_error_);
}

void MockHttpHooksForForwardBackendFailure::set_on_forward_backend_error(srs_error_t err)
{
    srs_freep(on_forward_backend_error_);
    on_forward_backend_error_ = srs_error_copy(err);
}

// A dynamic-forward backend is an external dependency. If it fails, SRS should reject the
// current publish attempt and roll back the partially acquired live source so the publisher
// can retry. This regression test defines the required rollback behavior.
VOID TEST(BasicWorkflowForwardTest, ForwardBackendFailureRollsBackPublishState)
{
    srs_error_t err;

    SrsUniquePtr<MockAppConfig> mock_config(new MockAppConfig());
    SrsUniquePtr<MockAppStatistic> mock_stat(new MockAppStatistic());
    SrsUniquePtr<MockHttpHooksForForwardBackendFailure> mock_hooks(new MockHttpHooksForForwardBackendFailure());
    SrsUniquePtr<MockRequest> req(new MockRequest("__defaultVhost__", "live", "stream1"));

    mock_config->set_forward_backend("http://127.0.0.1:8085/api/v1/forward");
    mock_hooks->set_on_forward_backend_error(srs_error_new(ERROR_HTTP_STATUS_INVALID, "mock HTTP 500"));

    SrsLiveSource *raw_source = new SrsLiveSource();
    raw_source->config_ = mock_config.get();
    raw_source->stat_ = mock_stat.get();
    raw_source->req_ = req->copy();
    raw_source->assemble();
    SrsSharedPtr<SrsLiveSource> source(raw_source);

    SrsOriginHub *hub = new SrsOriginHub();
    hub->config_ = mock_config.get();
    hub->stat_ = mock_stat.get();
    hub->hooks_ = mock_hooks.get();
    HELPER_EXPECT_SUCCESS(hub->initialize(source, raw_source->req_));
    raw_source->hub_ = hub;

    SrsUniquePtr<SrsRtmpConn> conn(new SrsRtmpConn(new MockRtmpTransport(), "127.0.0.1", 1935));
    conn->config_ = mock_config.get();
    conn->stat_ = mock_stat.get();
    conn->hooks_ = mock_hooks.get();
    conn->assemble();

    conn->info_->req_->vhost_ = req->vhost_;
    conn->info_->req_->app_ = req->app_;
    conn->info_->req_->stream_ = req->stream_;
    conn->info_->edge_ = false;

    // Each attempt should reach the backend and fail with its original error. Most importantly,
    // the failed attempt must leave the live source available for the next publisher.
    for (int i = 0; i < 2; i++) {
        err = conn->publishing(source);
        EXPECT_TRUE(err != srs_success);
        EXPECT_EQ(ERROR_HTTP_STATUS_INVALID, srs_error_code(err));
        srs_freep(err);

        EXPECT_TRUE(source->can_publish(false));
    }
    EXPECT_EQ(2, mock_hooks->on_forward_backend_count_);
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

// The publisher sends one packet through the origin hub, which fans it out to the forwarders.
static srs_error_t utest_workflow_forward_publish(SrsOriginHub *hub, SrsMediaPacket *pkt)
{
    srs_error_t err = pkt->is_audio() ? hub->on_audio(pkt) : hub->on_video(pkt, false);
    srs_freep(pkt);
    return err;
}

// Run the hub's only forwarder through a stream: the publisher streams before the destination is
// connected; the forwarder connects, then loses the destination mid-stream; the publisher streams on,
// a keyframe among it; the forwarder reconnects mid-GOP. The packets the first connect sent are in
// first, the second client is the last of the factory.
static void utest_workflow_forward_reconnect(SrsOriginHub *hub, MockAppFactoryForForwarderCycle *factory, MockAppConfig *config,
                                             std::vector<MockForwarderSentPacket> &first)
{
    srs_error_t err;

    ASSERT_EQ(1, (int)hub->forwarders_.size());
    SrsForwarder *forwarder = dynamic_cast<SrsForwarder *>(hub->forwarders_.at(0));
    ASSERT_TRUE(forwarder != NULL);

    // Stop the coroutine the hub started; the test runs the connects itself.
    forwarder->on_unpublish();

    HELPER_EXPECT_SUCCESS(utest_workflow_forward_publish(hub, utest_forward_video_keyframe(100, 'k')));
    HELPER_EXPECT_SUCCESS(utest_workflow_forward_publish(hub, utest_forward_audio_raw(110, 'a')));
    HELPER_EXPECT_SUCCESS(utest_workflow_forward_publish(hub, utest_forward_video_inter(200, 'p')));

    // Connect; the destination is lost when the send of inter frame x fails.
    if (true) {
        utest_forward_wire(forwarder, factory, config, 10);
        MockRtmpClientForForwarder *sdk = new MockRtmpClientForForwarder();
        sdk->live_hub_ = hub;
        sdk->send_error_ = srs_error_new(ERROR_SOCKET_WRITE, "mock destination lost");
        sdk->fail_marker_ = 'x';
        sdk->live_.push_back(utest_forward_batch(utest_forward_video_keyframe(300, 'K'), utest_forward_audio_raw(310, 'b')));
        sdk->live_.push_back(utest_forward_batch(utest_forward_video_inter(400, 'x'), utest_forward_audio_raw(410, 'c')));
        factory->next_ = sdk;

        err = forwarder->do_cycle();
        EXPECT_EQ(ERROR_SOCKET_WRITE, srs_error_code(err));
        srs_freep(err);
        EXPECT_EQ(0, sdk->live_errors_);

        // The reconnect frees this client.
        first = sdk->sent_;
    }

    HELPER_EXPECT_SUCCESS(utest_workflow_forward_publish(hub, utest_forward_video_inter(500, 'q')));
    HELPER_EXPECT_SUCCESS(utest_workflow_forward_publish(hub, utest_forward_audio_raw(510, 'd')));
    HELPER_EXPECT_SUCCESS(utest_workflow_forward_publish(hub, utest_forward_video_keyframe(600, 'L')));
    HELPER_EXPECT_SUCCESS(utest_workflow_forward_publish(hub, utest_forward_video_inter(700, 'r')));

    // Reconnect while the publisher is in the middle of a GOP, then the next keyframe comes.
    if (true) {
        utest_forward_wire(forwarder, factory, config, 2);
        MockRtmpClientForForwarder *sdk = new MockRtmpClientForForwarder();
        sdk->live_hub_ = hub;
        sdk->live_.push_back(utest_forward_batch(utest_forward_video_inter(800, 's'), utest_forward_audio_raw(810, 'e')));
        sdk->live_.push_back(utest_forward_batch(utest_forward_video_keyframe(900, 'N'), utest_forward_video_inter(1000, 't'),
                                                 utest_forward_audio_raw(1010, 'f')));
        factory->next_ = sdk;

        err = forwarder->do_cycle();
        EXPECT_EQ(ERROR_THREAD_DUMMY, srs_error_code(err));
        srs_freep(err);
        EXPECT_EQ(0, sdk->live_errors_);
    }
}

// Check what a forward that reconnects mid-stream sent on each connect: the source's metadata and
// sequence headers once, then live from a keyframe, with no stale burst and no timestamp before the
// connect except the headers.
//
// Each letter is the marker of a packet fed above, and its builder sets the kind, for example
// utest_forward_video_keyframe() makes a keyframe:
//   - First connect: metadata M, sequence headers W and B, keyframe K at 300, audio b, inter x
//     whose send fails, and audio c. The media queued before the connect, k a p, is dropped.
//   - Second connect: M W B again, audio e at 810, keyframe N at 900, inter t, audio f. The inter
//     frame s at 800 comes before the keyframe and is dropped, so is the media queued while the
//     destination was away, q d L r at 500 to 700.
// So the first video after the headers is a keyframe: K at 300, then N at 900.
static void utest_workflow_forward_check_restart(const std::vector<MockForwarderSentPacket> &first, MockRtmpClientForForwarder *second)
{
    EXPECT_STREQ("MWBKbxc", utest_forward_markers(first).c_str());
    EXPECT_STREQ("MWBeNtf", utest_forward_markers(second->sent_).c_str());

    // M, W, B, audio e, keyframe N at 900, inter t, audio f.
    const int64_t timestamps[] = {0, 0, 0, 810, 900, 1000, 1010};
    ASSERT_EQ(7, (int)second->sent_.size());
    for (int i = 0; i < 7; i++) {
        EXPECT_EQ(timestamps[i], second->sent_[i].timestamp_) << "#" << i;
    }
}

// A forward.destination forward whose destination is lost mid-stream: after the reconnect, the
// destination starts again at a keyframe, with no burst of the media queued while it was away.
VOID TEST(BasicWorkflowForwardTest, DestinationReconnectStartsAtKeyframe)
{
    srs_error_t err;

    std::vector<MockForwarderSentPacket> first;
    SrsUniquePtr<MockAppConfigForForwardHub> config(new MockAppConfigForForwardHub());
    SrsUniquePtr<MockHttpHooksForForwardHub> hooks(new MockHttpHooksForForwardHub());
    SrsUniquePtr<MockAppFactoryForForwarderCycle> factory(new MockAppFactoryForForwarderCycle());
    SrsUniquePtr<MockLiveSourceForOriginHub> source(new MockLiveSourceForOriginHub());
    SrsUniquePtr<MockRequest> req(new MockRequest("__defaultVhost__", "live", "livestream"));

    // The source has the stream's metadata and sequence headers.
    source->meta()->meta_ = utest_forward_metadata(0, 'M');
    source->meta()->video_ = utest_forward_video_sh(0, 'W');
    source->meta()->audio_ = utest_forward_audio_sh(0, 'B');

    std::vector<std::string> destinations;
    destinations.push_back("127.0.0.1:19350");
    config->set_forward_destinations(destinations);

    SrsUniquePtr<SrsOriginHub> hub(utest_forward_hub(source.get(), req.get(), config.get(), hooks.get()));
    HELPER_ASSERT_SUCCESS(hub->create_forwarders());

    utest_workflow_forward_reconnect(hub.get(), factory.get(), config.get(), first);

    ASSERT_EQ(2, (int)factory->urls_.size());
    EXPECT_STREQ("rtmp://127.0.0.1:19350/live/livestream", factory->urls_[0].c_str());
    EXPECT_STREQ("rtmp://127.0.0.1:19350/live/livestream", factory->urls_[1].c_str());
    utest_workflow_forward_check_restart(first, factory->clients_[1]);
}

// A forward.backend forward, to the backend's url with its own app, stream and token, whose
// destination is lost mid-stream: after the reconnect, the destination starts again at a keyframe,
// with no burst of the media queued while it was away.
VOID TEST(BasicWorkflowForwardTest, BackendReconnectStartsAtKeyframe)
{
    srs_error_t err;

    std::vector<MockForwarderSentPacket> first;
    SrsUniquePtr<MockAppConfigForForwardHub> config(new MockAppConfigForForwardHub());
    SrsUniquePtr<MockHttpHooksForForwardHub> hooks(new MockHttpHooksForForwardHub());
    SrsUniquePtr<MockAppFactoryForForwarderCycle> factory(new MockAppFactoryForForwarderCycle());
    SrsUniquePtr<MockLiveSourceForOriginHub> source(new MockLiveSourceForOriginHub());
    SrsUniquePtr<MockRequest> req(new MockRequest("__defaultVhost__", "live", "livestream"));

    source->meta()->meta_ = utest_forward_metadata(0, 'M');
    source->meta()->video_ = utest_forward_video_sh(0, 'W');
    source->meta()->audio_ = utest_forward_audio_sh(0, 'B');

    // The backend decides the destination; the configured destination is ignored.
    std::vector<std::string> destinations;
    destinations.push_back("127.0.0.1:19350");
    config->set_forward_destinations(destinations);
    config->set_forward_backend("http://127.0.0.1:8085/api/v1/forward");
    hooks->urls_.push_back("rtmp://127.0.0.1:19351/fwd/livestream-dst?token=abc");

    SrsUniquePtr<SrsOriginHub> hub(utest_forward_hub(source.get(), req.get(), config.get(), hooks.get()));
    HELPER_ASSERT_SUCCESS(hub->create_forwarders());
    EXPECT_EQ(1, hooks->count_);

    utest_workflow_forward_reconnect(hub.get(), factory.get(), config.get(), first);

    ASSERT_EQ(2, (int)factory->urls_.size());
    EXPECT_STREQ("rtmp://127.0.0.1:19351/fwd/livestream-dst?token=abc&vhost=127.0.0.1", factory->urls_[0].c_str());
    EXPECT_STREQ("rtmp://127.0.0.1:19351/fwd/livestream-dst?token=abc&vhost=127.0.0.1", factory->urls_[1].c_str());
    utest_workflow_forward_check_restart(first, factory->clients_[1]);
}
