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

// Create a mock audio cache ISrsRtcFrameBuilderAudioPacketCache
class MockAudioCache : public ISrsRtcFrameBuilderAudioPacketCache
{
public:
    int process_packet_count_;

public:
    MockAudioCache();
    virtual ~MockAudioCache();

public:
    virtual srs_error_t process_packet(SrsRtpPacket *src, std::vector<SrsRtpPacket *> &ready_packets);
    virtual void clear_all();
};

MockAudioCache::MockAudioCache()
{
    process_packet_count_ = 0;
}

MockAudioCache::~MockAudioCache()
{
}

srs_error_t MockAudioCache::process_packet(SrsRtpPacket *src, std::vector<SrsRtpPacket *> &ready_packets)
{
    process_packet_count_++;

    // Copy the packet.
    SrsRtpPacket *copy = src->copy();
    ready_packets.push_back(copy);

    return srs_success;
}

void MockAudioCache::clear_all()
{
}

// Mock the audio transcoder ISrsAudioTranscoder.
class MockAudioTranscoderForRtc2Rtmp : public ISrsAudioTranscoder
{
public:
    int transcode_count_;
    std::vector<SrsParsedAudioPacket *> output_packets_;
    std::string aac_header_;

public:
    MockAudioTranscoderForRtc2Rtmp();
    virtual ~MockAudioTranscoderForRtc2Rtmp();

public:
    virtual srs_error_t initialize(SrsAudioCodecId from, SrsAudioCodecId to, int channels, int sample_rate, int bit_rate);
    virtual srs_error_t transcode(SrsParsedAudioPacket *in, std::vector<SrsParsedAudioPacket *> &outs);
    virtual void free_frames(std::vector<SrsParsedAudioPacket *> &frames);
    virtual void aac_codec_header(uint8_t **data, int *len);
};

MockAudioTranscoderForRtc2Rtmp::MockAudioTranscoderForRtc2Rtmp()
{
    transcode_count_ = 0;
}

MockAudioTranscoderForRtc2Rtmp::~MockAudioTranscoderForRtc2Rtmp()
{
}

srs_error_t MockAudioTranscoderForRtc2Rtmp::initialize(SrsAudioCodecId from, SrsAudioCodecId to, int channels, int sample_rate, int bit_rate)
{
    return srs_success;
}

srs_error_t MockAudioTranscoderForRtc2Rtmp::transcode(SrsParsedAudioPacket *in, std::vector<SrsParsedAudioPacket *> &outs)
{
    transcode_count_++;

    SrsParsedAudioPacket *out = in->copy();
    output_packets_.push_back(out);
    outs.push_back(out);

    return srs_success;
}

void MockAudioTranscoderForRtc2Rtmp::free_frames(std::vector<SrsParsedAudioPacket *> &frames)
{
}

void MockAudioTranscoderForRtc2Rtmp::aac_codec_header(uint8_t **data, int *len)
{
    int size = aac_header_.size();
    uint8_t *copy = new uint8_t[size];
    memcpy(copy, aac_header_.data(), size);
    *data = copy;
    *len = size;
}

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
    MockAudioCache *mock_audio_cache = new MockAudioCache();
    MockAudioTranscoderForRtc2Rtmp *mock_audio_transcoder = new MockAudioTranscoderForRtc2Rtmp();

    mock_audio_transcoder->aac_header_ = std::string("\xAF\x00\x12\x10", 4); // AAC sequence header.
    mock_config->rtc_to_rtmp_ = true;

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
    }

    // Initialize publish stream, rtc2rtmp bridge should be created
    SrsRtcBridge *bridge = NULL;
    SrsLiveSource *live_source = NULL;
    SrsRtcFrameBuilder *frame_builder = NULL;
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

        frame_builder = bridge->frame_builder_;
        EXPECT_TRUE(frame_builder != NULL);
    }

    // Start the publish stream.
    if (true) {
        // Test: First call to start() should succeed
        HELPER_EXPECT_SUCCESS(publish_stream->start());

        // Verify is_sender_started_ flag is set
        EXPECT_TRUE(publish_stream->is_sender_started_);

        // When starting the publish stream, the frame builder should be recreated
        EXPECT_TRUE(frame_builder != bridge->frame_builder_);
        frame_builder = bridge->frame_builder_;
        EXPECT_TRUE(frame_builder != NULL);

        // Mock the frame builder object
        srs_freep(frame_builder->audio_cache_);
        frame_builder->audio_cache_ = mock_audio_cache;
        srs_freep(frame_builder->audio_transcoder_);
        frame_builder->audio_transcoder_ = mock_audio_transcoder;
    }

    // Got a RTP audio packet.
    MockLiveSource *mock_source = dynamic_cast<MockLiveSource *>(mock_sources->mock_source_.get());
    if (true) {
        SrsRtpPacket pkt;
        pkt.header_.set_ssrc(track_factory->audio_ssrc_);
        pkt.header_.set_sequence(100);
        pkt.header_.set_timestamp(1000);
        pkt.header_.set_payload_type(track_factory->audio_pt_);

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
