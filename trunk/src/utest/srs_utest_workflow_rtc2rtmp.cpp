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

#include <srs_utest_workflow_rtc2rtmp.hpp>

#include <srs_app_factory.hpp>
#include <srs_app_rtc_codec.hpp>
#include <srs_app_rtc_conn.hpp>
#include <srs_app_rtc_server.hpp>
#include <srs_app_rtc_source.hpp>
#include <srs_app_stream_token.hpp>
#include <srs_kernel_error.hpp>
#include <srs_protocol_rtmp_stack.hpp>
#include <srs_utest_ai11.hpp>
#include <srs_utest_manual_mock.hpp>
#include <srs_utest_manual_service.hpp>
#include <srs_utest_workflow_rtc_conn.hpp>

// Mock app factory for RTC to RTMP workflow testing
class MockAppFactoryForRtc2Rtmp : public SrsAppFactory
{
public:
    MockAudioCache *last_created_audio_cache_;
    MockAudioTranscoder *last_created_audio_transcoder_;

public:
    MockAppFactoryForRtc2Rtmp()
    {
        last_created_audio_cache_ = NULL;
        last_created_audio_transcoder_ = NULL;
    }

    virtual ~MockAppFactoryForRtc2Rtmp()
    {
        // Don't delete last_created_audio_cache_ or last_created_audio_transcoder_ here,
        // they are managed by frame builder
    }

#ifdef SRS_FFMPEG_FIT
    virtual ISrsRtcFrameBuilderAudioPacketCache *create_rtc_frame_builder_audio_packet_cache()
    {
        // Create a new mock audio cache each time
        MockAudioCache *cache = new MockAudioCache();
        last_created_audio_cache_ = cache;
        return cache;
    }

    virtual ISrsAudioTranscoder *create_audio_transcoder()
    {
        // Create a new mock audio transcoder each time
        MockAudioTranscoder *transcoder = new MockAudioTranscoder();
        last_created_audio_transcoder_ = transcoder;
        return transcoder;
    }
#endif
};

// This test is used to verify the basic workflow of the RTC connection.
// It's finished with the help of AI, but each step is manually designed
// and verified. So this is not dominated by AI, but by humanbeing.
VOID TEST(BasicWorkflowRtc2RtmpTest, ManuallyVerifyTypicalScenario)
{
    srs_error_t err;

    // Create mock objects for dependencies
    SrsUniquePtr<MockAppConfig> mock_config(new MockAppConfig());
    SrsUniquePtr<MockRtcSourceManager> mock_rtc_sources(new MockRtcSourceManager());
    SrsUniquePtr<MockAppStatistic> mock_stat(new MockAppStatistic());
    SrsUniquePtr<MockRtcAsyncCallRequest> mock_request(new MockRtcAsyncCallRequest("test.vhost", "live", "stream1"));
    SrsUniquePtr<MockRtcAsyncTaskExecutor> mock_exec(new MockRtcAsyncTaskExecutor());
    SrsUniquePtr<MockExpire> mock_expire(new MockExpire());
    SrsUniquePtr<MockRtcPacketReceiver> mock_receiver(new MockRtcPacketReceiver());
    SrsUniquePtr<MockRtcTrackDescriptionFactory> track_factory(new MockRtcTrackDescriptionFactory());
    SrsUniquePtr<MockLiveSourceManager> mock_sources(new MockLiveSourceManager());
    SrsUniquePtr<MockAppFactoryForRtc2Rtmp> mock_factory(new MockAppFactoryForRtc2Rtmp());

    mock_config->rtc_to_rtmp_ = true;
    mock_config->rtc_init_rate_from_sdp_ = true;

    // Create RTC publish stream - use real pli_worker_
    SrsContextId cid;
    cid.set_value("test-rtc2rtmp-workflow-typical-scenario");
    SrsUniquePtr<SrsRtcPublishStream> publish_stream(new SrsRtcPublishStream(mock_exec.get(), mock_expire.get(), mock_receiver.get(), cid));

    // Mock the publish stream object
    if (true) {
        // Inject mock dependencies
        publish_stream->config_ = mock_config.get();
        publish_stream->rtc_sources_ = mock_rtc_sources.get();
        publish_stream->live_sources_ = mock_sources.get();
        publish_stream->stat_ = mock_stat.get();
        publish_stream->app_factory_ = mock_factory.get();
    }

    // Initialize publish stream, rtc2rtmp bridge should be created
    SrsRtcBridge *bridge = NULL;
    SrsLiveSource *live_source = NULL;
    if (true) {
        SrsUniquePtr<SrsRtcSourceDescription> stream_desc(track_factory->create_stream_description());

        // Initialize the publish stream (it will take ownership of track descriptions)
        HELPER_EXPECT_SUCCESS(publish_stream->initialize(mock_request.get(), stream_desc.get()));

        // Check the tracks, should be one audio track
        EXPECT_EQ(publish_stream->audio_tracks_.size(), 1);
        // Check the tracks, should be one video track
        EXPECT_EQ(publish_stream->video_tracks_.size(), 1);

        // source bridge should be created
        bridge = dynamic_cast<SrsRtcBridge *>(publish_stream->source_->rtc_bridge_);
        EXPECT_TRUE(bridge != NULL);

        live_source = bridge->rtmp_target_.get();
        EXPECT_TRUE(live_source != NULL);
    }

    // Start the publish stream.
    SrsRtcFrameBuilder *frame_builder = NULL;
    if (true) {
        // Test: First call to start() should succeed
        HELPER_EXPECT_SUCCESS(publish_stream->start());

        // Wait for coroutine to start.
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);

        // Verify is_sender_started_ flag is set
        EXPECT_TRUE(publish_stream->is_sender_started_);

        // When starting the publish stream, the frame builder should be recreated
        frame_builder = bridge->frame_builder_;
        EXPECT_TRUE(frame_builder != NULL);

        // Verify the audio cache was created by the factory
        EXPECT_TRUE(mock_factory->last_created_audio_cache_ != NULL);
        EXPECT_EQ(frame_builder->audio_cache_, mock_factory->last_created_audio_cache_);

        // Verify the audio transcoder was created by the factory
        EXPECT_TRUE(mock_factory->last_created_audio_transcoder_ != NULL);
        EXPECT_EQ(frame_builder->audio_transcoder_, mock_factory->last_created_audio_transcoder_);
    }

    // Got a RTP audio packet.
    MockLiveSource *mock_source = dynamic_cast<MockLiveSource *>(mock_sources->mock_source_.get());
    if (true) {
        SrsRtpPacket pkt;
        pkt.header_.set_ssrc(track_factory->audio_ssrc_);
        pkt.header_.set_sequence(100);
        pkt.header_.set_timestamp(1000);
        pkt.header_.set_payload_type(track_factory->audio_pt_);

        // Create fake audio payload.
        SrsRtpRawPayload *raw = new SrsRtpRawPayload();
        pkt.set_payload(raw, SrsRtpPacketPayloadTypeRaw);
        raw->payload_ = pkt.wrap(100);
        raw->nn_payload_ = 100;

        SrsUniquePtr<char[]> data(new char[1500]);
        SrsBuffer buf(data.get(), 1500);
        HELPER_EXPECT_SUCCESS(pkt.encode(&buf));

        HELPER_EXPECT_SUCCESS(publish_stream->on_rtp_plaintext(data.get(), buf.pos()));

        // The live source should got 2 audio packets, one is sequence header, another is audio data.
        EXPECT_EQ(mock_source->on_audio_count_, 2);
        EXPECT_EQ(mock_source->on_frame_count_, 2);
        EXPECT_EQ(mock_source->on_video_count_, 0);
    }

    publish_stream->stop();
}
