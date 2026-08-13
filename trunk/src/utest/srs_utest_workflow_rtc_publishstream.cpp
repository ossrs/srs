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

#include <srs_utest_workflow_rtc_publishstream.hpp>

#include <srs_app_rtc_conn.hpp>
#include <srs_app_rtc_source.hpp>
#include <srs_kernel_error.hpp>
#include <srs_utest_ai11.hpp>
#include <srs_utest_manual_mock.hpp>

namespace
{

class MockRtcFrameStatistic : public MockAppStatistic
{
public:
    int video_frame_calls_;
    int audio_frame_calls_;
    int video_frames_;
    int audio_frames_;

public:
    MockRtcFrameStatistic()
    {
        video_frame_calls_ = 0;
        audio_frame_calls_ = 0;
        video_frames_ = 0;
        audio_frames_ = 0;
    }

    virtual srs_error_t on_video_frames(ISrsRequest *req, int nb_frames)
    {
        ++video_frame_calls_;
        video_frames_ += nb_frames;
        return srs_success;
    }

    virtual srs_error_t on_audio_frames(ISrsRequest *req, int nb_frames)
    {
        ++audio_frame_calls_;
        audio_frames_ += nb_frames;
        return srs_success;
    }
};

srs_error_t send_rtp_packet(SrsRtcPublishStream *publish_stream, uint32_t ssrc, uint16_t sequence, uint32_t timestamp, uint8_t payload_type, bool marker)
{
    SrsRtpPacket pkt;
    pkt.header_.set_ssrc(ssrc);
    pkt.header_.set_sequence(sequence);
    pkt.header_.set_timestamp(timestamp);
    pkt.header_.set_payload_type(payload_type);
    pkt.header_.set_marker(marker);

    char data[1500];
    SrsBuffer buf(data, sizeof(data));
    srs_error_t err = pkt.encode(&buf);
    if (err != srs_success) {
        return srs_error_wrap(err, "encode RTP packet");
    }

    return publish_stream->on_rtp_plaintext(data, buf.pos());
}

} // namespace

// This test is used to verify the basic workflow of the RTC publish stream.
// It's finished with the help of AI, but each step is manually designed
// and verified. So this is not dominated by AI, but by humanbeing.
VOID TEST(BasicWorkflowRtcPublishStreamTest, ManuallyVerify)
{
    srs_error_t err;

    // Create mock objects for dependencies
    MockAppConfig mock_config;
    MockRtcSourceManager mock_rtc_sources;
    MockAppStatistic mock_stat;
    MockRtcAsyncCallRequest mock_request("test.vhost", "live", "stream1");
    MockRtcAsyncTaskExecutor mock_exec;
    MockExpire mock_expire;
    MockRtcPacketReceiver mock_receiver;
    MockRtcTrackDescriptionFactory track_factory;
    MockRtcFormat mock_format;
    SrsContextId cid;
    cid.set_value("test-publish-stream-cid");

    // Create RTC publish stream - use real pli_worker_
    SrsUniquePtr<SrsRtcPublishStream> publish_stream(new SrsRtcPublishStream(&mock_exec, &mock_expire, &mock_receiver, cid));

    // Mock the publish stream object
    if (true) {
        // Inject mock dependencies
        publish_stream->config_ = &mock_config;
        publish_stream->rtc_sources_ = &mock_rtc_sources;
        publish_stream->stat_ = &mock_stat;

        // Replace the real format_ with mock format
        srs_freep(publish_stream->format_);
        publish_stream->format_ = &mock_format;
    }

    // Create stream description with audio and video tracks
    if (true) {
        SrsUniquePtr<SrsRtcSourceDescription> stream_desc(track_factory.create_stream_description());

        // Initialize the publish stream (it will take ownership of track descriptions)
        HELPER_EXPECT_SUCCESS(publish_stream->initialize(&mock_request, stream_desc.get()));

        // Check the tracks, should be one audio track
        EXPECT_EQ(publish_stream->audio_tracks_.size(), 1);
        // Check the tracks, should be one video track
        EXPECT_EQ(publish_stream->video_tracks_.size(), 1);

        // Test: First call to start() should succeed
        HELPER_EXPECT_SUCCESS(publish_stream->start());

        // Verify is_sender_started_ flag is set
        EXPECT_TRUE(publish_stream->is_sender_started_);

        // Wait for coroutine to start. Normally it should be ready and stopped at wait
        // for PLI requests.
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);
    }

    // Request a PLI about the video ssrc to the publisher.
    if (true) {
        uint32_t video_ssrc = track_factory.video_ssrc_;
        publish_stream->request_keyframe(video_ssrc, cid);

        // Wait for coroutine to process the request
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);

        // Verify the PLI is sent out
        EXPECT_EQ(mock_receiver.send_rtcp_fb_pli_count_, 1);
    }

    // Stop the publish stream
    publish_stream->stop();

    // Before destroying publish_stream, set format_ to NULL to prevent double-free
    // since mock_format is a stack variable
    publish_stream->format_ = NULL;
}

// Verify frame statistics from the RTP behavior exposed by a publish stream:
// audio uses primary RTP packets, while video uses the marker that closes a frame.
// Auxiliary RTX and FEC streams do not represent additional media frames.
VOID TEST(BasicWorkflowRtcPublishStreamTest, CountPrimaryRtcMediaFrames)
{
    srs_error_t err;

    MockAppConfig mock_config;
    MockRtcSourceManager mock_rtc_sources;
    MockRtcFrameStatistic mock_stat;
    MockRtcAsyncCallRequest mock_request("test.vhost", "live", "frame-counting");
    MockRtcAsyncTaskExecutor mock_exec;
    MockExpire mock_expire;
    MockRtcPacketReceiver mock_receiver;
    MockRtcTrackDescriptionFactory track_factory;
    MockRtcFormat mock_format;
    MockCircuitBreaker mock_circuit_breaker;
    SrsContextId cid;
    cid.set_value("test-frame-counting-cid");

    // Keep transport recovery outside this behavioral test. Duplicate primary
    // packets are intentionally accepted as additional observations below.
    mock_config.set_rtc_nack_enabled(false);

    SrsUniquePtr<SrsRtcPublishStream> publish_stream(new SrsRtcPublishStream(&mock_exec, &mock_expire, &mock_receiver, cid));
    publish_stream->config_ = &mock_config;
    publish_stream->rtc_sources_ = &mock_rtc_sources;
    publish_stream->stat_ = &mock_stat;
    publish_stream->circuit_breaker_ = &mock_circuit_breaker;
    srs_freep(publish_stream->format_);
    publish_stream->format_ = &mock_format;

    uint32_t audio_rtx_ssrc = track_factory.audio_ssrc_ + 1;
    uint32_t audio_fec_ssrc = track_factory.audio_ssrc_ + 2;
    uint32_t video_rtx_ssrc = track_factory.video_ssrc_ + 1;
    uint32_t video_fec_ssrc = track_factory.video_ssrc_ + 2;

    SrsUniquePtr<SrsRtcSourceDescription> stream_desc(track_factory.create_stream_description());
    stream_desc->audio_track_desc_->set_rtx_ssrc(audio_rtx_ssrc);
    stream_desc->audio_track_desc_->set_fec_ssrc(audio_fec_ssrc);
    stream_desc->video_track_descs_.at(0)->set_rtx_ssrc(video_rtx_ssrc);
    stream_desc->video_track_descs_.at(0)->set_fec_ssrc(video_fec_ssrc);
    HELPER_EXPECT_SUCCESS(publish_stream->initialize(&mock_request, stream_desc.get()));

    // Send complete three-packet video frames until statistics are reported.
    // The reported value must equal the number of primary marker packets, not
    // the number of RTP packets used to carry those frames.
    int primary_video_frames = 0;
    uint16_t video_sequence = 100;
    uint32_t video_timestamp = 90000;
    for (; primary_video_frames < 1000 && mock_stat.video_frame_calls_ == 0; ++primary_video_frames) {
        HELPER_EXPECT_SUCCESS(send_rtp_packet(publish_stream.get(), track_factory.video_ssrc_, video_sequence++, video_timestamp, track_factory.video_pt_, false));
        HELPER_EXPECT_SUCCESS(send_rtp_packet(publish_stream.get(), track_factory.video_ssrc_, video_sequence++, video_timestamp, track_factory.video_pt_, false));
        HELPER_EXPECT_SUCCESS(send_rtp_packet(publish_stream.get(), track_factory.video_ssrc_, video_sequence++, video_timestamp, track_factory.video_pt_, true));
        video_timestamp += 3000;
    }
    EXPECT_GT(mock_stat.video_frame_calls_, 0);
    EXPECT_EQ(primary_video_frames, mock_stat.video_frames_);

    // Auxiliary marker packets do not describe new source frames and must not
    // cause another video statistics report.
    int video_calls_before_auxiliary = mock_stat.video_frame_calls_;
    int video_frames_before_auxiliary = mock_stat.video_frames_;
    for (int i = 0; i < 400; ++i) {
        uint32_t ssrc = i % 2 ? video_rtx_ssrc : video_fec_ssrc;
        HELPER_EXPECT_SUCCESS(send_rtp_packet(publish_stream.get(), ssrc, video_sequence++, video_timestamp, track_factory.video_pt_, true));
    }
    EXPECT_EQ(video_calls_before_auxiliary, mock_stat.video_frame_calls_);
    EXPECT_EQ(video_frames_before_auxiliary, mock_stat.video_frames_);

    // A duplicate primary marker is acceptable for these approximate statistics.
    // Count every observation until the next report without deduplicating it.
    int duplicate_video_packets = 0;
    int video_calls_before_duplicates = mock_stat.video_frame_calls_;
    int video_frames_before_duplicates = mock_stat.video_frames_;
    for (; duplicate_video_packets < 1000 && mock_stat.video_frame_calls_ == video_calls_before_duplicates; ++duplicate_video_packets) {
        HELPER_EXPECT_SUCCESS(send_rtp_packet(publish_stream.get(), track_factory.video_ssrc_, 65000, video_timestamp, track_factory.video_pt_, true));
    }
    EXPECT_GT(mock_stat.video_frame_calls_, video_calls_before_duplicates);
    EXPECT_EQ(video_frames_before_duplicates + duplicate_video_packets, mock_stat.video_frames_);

    // Each primary audio RTP packet is an audio frame observation.
    int primary_audio_frames = 0;
    uint16_t audio_sequence = 1000;
    uint32_t audio_timestamp = 48000;
    for (; primary_audio_frames < 1000 && mock_stat.audio_frame_calls_ == 0; ++primary_audio_frames) {
        HELPER_EXPECT_SUCCESS(send_rtp_packet(publish_stream.get(), track_factory.audio_ssrc_, audio_sequence++, audio_timestamp, track_factory.audio_pt_, false));
        audio_timestamp += 960;
    }
    EXPECT_GT(mock_stat.audio_frame_calls_, 0);
    EXPECT_EQ(primary_audio_frames, mock_stat.audio_frames_);

    // RTX and FEC audio packets are transport data rather than media frames.
    int audio_calls_before_auxiliary = mock_stat.audio_frame_calls_;
    int audio_frames_before_auxiliary = mock_stat.audio_frames_;
    for (int i = 0; i < 400; ++i) {
        uint32_t ssrc = i % 2 ? audio_rtx_ssrc : audio_fec_ssrc;
        HELPER_EXPECT_SUCCESS(send_rtp_packet(publish_stream.get(), ssrc, audio_sequence++, audio_timestamp, track_factory.audio_pt_, false));
    }
    EXPECT_EQ(audio_calls_before_auxiliary, mock_stat.audio_frame_calls_);
    EXPECT_EQ(audio_frames_before_auxiliary, mock_stat.audio_frames_);

    // mock_format is stack-owned.
    publish_stream->format_ = NULL;
}
