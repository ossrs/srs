//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_utest_workflow_rtc_playstream.hpp>

#include <srs_app_rtc_conn.hpp>
#include <srs_app_rtc_source.hpp>
#include <srs_kernel_error.hpp>
#include <srs_utest_ai11.hpp>
#include <srs_utest_manual_app.hpp>
#include <srs_utest_manual_mock.hpp>

// This test is used to verify the basic workflow of the RTC play stream.
// It's finished with the help of AI, but each step is manually designed
// and verified. So this is not dominated by AI, but by humanbeing.
VOID TEST(BasicWorkflowRtcPlayStreamTest, ManuallyVerify)
{
    srs_error_t err;

    // Create mock objects for dependencies
    MockAppConfig mock_config;
    MockRtcSourceManager mock_rtc_sources;
    MockAppStatistic mock_stat;
    MockRtcAsyncCallRequest mock_request("test.vhost", "live", "stream1");
    MockRtcAsyncTaskExecutor mock_exec;
    MockExpire mock_expire;
    MockRtcPacketSender mock_sender;
    MockRtcTrackDescriptionFactory track_factory;
    SrsContextId cid;
    cid.set_value("test-play-stream-start-cid");

    // Create RTC play stream - uses real app_factory_ and real pli_worker_
    SrsUniquePtr<SrsRtcPlayStream> play_stream(new SrsRtcPlayStream(&mock_exec, &mock_expire, &mock_sender, cid));

    // Mock the play stream object.
    if (true) {
        // Inject mock dependencies
        play_stream->config_ = &mock_config;
        play_stream->rtc_sources_ = &mock_rtc_sources;
        play_stream->stat_ = &mock_stat;

        // Set mw_msgs to 0 to make the consumer block until we push a packet
        mock_config.mw_msgs_ = 0;
    }

    // Create track descriptions using factory
    if (true) {
        std::map<uint32_t, SrsRtcTrackDescription *> sub_relations = track_factory.create_audio_video_tracks();

        // Initialize the play stream (it will take ownership of track descriptions)
        HELPER_EXPECT_SUCCESS(play_stream->initialize(&mock_request, sub_relations));

        // Check the tracks, should be two video tracks.
        EXPECT_EQ(play_stream->video_tracks_.size(), 2);
        // Check the tracks, should be a audio track.
        EXPECT_EQ(play_stream->audio_tracks_.size(), 1);

        // Test: First call to start() should succeed
        HELPER_EXPECT_SUCCESS(play_stream->start());

        // Verify is_started_ flag is set
        EXPECT_TRUE(play_stream->is_started_);

        // Wait for coroutine to start and create consumer. Normally it should be ready
        // and stopped at wait for RTP packets from consumer.
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);
    }

    // Push a video packet to the source to feed the consumer.
    if (true) {
        SrsUniquePtr<SrsRtpPacket> test_pkt(new SrsRtpPacket());
        test_pkt->frame_type_ = SrsFrameTypeVideo;
        test_pkt->header_.set_sequence(1000);
        test_pkt->header_.set_timestamp(5000);
        test_pkt->header_.set_ssrc(track_factory.video_ssrc_);

        // Push packet to source - this will feed all consumers including the play stream's consumer
        HELPER_EXPECT_SUCCESS(play_stream->source_->on_rtp(test_pkt.get()));
    }

    // Verify the video packet is sent out
    if (true) {
        // Wait for coroutine to process the packet
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);

        // Check sender should have received the packet
        EXPECT_EQ(mock_sender.send_packet_count_, 1);
        // The packet should create a cached track for this ssrc.
        EXPECT_EQ(play_stream->cache_ssrc0_, track_factory.video_ssrc_);
        EXPECT_TRUE(play_stream->cache_track0_ != NULL);
        // The packet should be in the nack ring buffer.
        SrsRtpPacket *pkt = play_stream->cache_track0_->rtp_queue_->at(1000);
        EXPECT_TRUE(pkt != NULL);
        EXPECT_EQ(pkt->header_.get_ssrc(), track_factory.video_ssrc_);
    }

    // Push a audio packet to the source to feed the consumer.
    if (true) {
        SrsUniquePtr<SrsRtpPacket> test_pkt(new SrsRtpPacket());
        test_pkt->frame_type_ = SrsFrameTypeAudio;
        test_pkt->header_.set_sequence(1000);
        test_pkt->header_.set_timestamp(5000);
        test_pkt->header_.set_ssrc(track_factory.audio_ssrc_);

        // Push packet to source - this will feed all consumers including the play stream's consumer
        HELPER_EXPECT_SUCCESS(play_stream->source_->on_rtp(test_pkt.get()));
    }

    // Verify the audio packet is sent out
    if (true) {
        // Wait for coroutine to process the packet
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);

        // Check sender should have received the packet
        EXPECT_EQ(mock_sender.send_packet_count_, 2);
        // The packet should create a cached track for this ssrc.
        EXPECT_EQ(play_stream->cache_ssrc1_, track_factory.audio_ssrc_);
        EXPECT_TRUE(play_stream->cache_track1_ != NULL);
        // The packet should be in the nack ring buffer.
        SrsRtpPacket *pkt = play_stream->cache_track1_->rtp_queue_->at(1000);
        EXPECT_TRUE(pkt != NULL);
        EXPECT_EQ(pkt->header_.get_ssrc(), track_factory.audio_ssrc_);
    }

    // Push a screen share packet to the source to feed the consumer.
    if (true) {
        SrsUniquePtr<SrsRtpPacket> test_pkt(new SrsRtpPacket());
        test_pkt->frame_type_ = SrsFrameTypeVideo;
        test_pkt->header_.set_sequence(1000);
        test_pkt->header_.set_timestamp(5000);
        test_pkt->header_.set_ssrc(track_factory.screen_ssrc_);

        // Push packet to source - this will feed all consumers including the play stream's consumer
        HELPER_EXPECT_SUCCESS(play_stream->source_->on_rtp(test_pkt.get()));
    }

    // Verify the screen share packet is sent out
    if (true) {
        // Wait for coroutine to process the packet
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);

        // Check sender should have received the packet
        EXPECT_EQ(mock_sender.send_packet_count_, 3);
        // The packet should create a cached track for this ssrc.
        EXPECT_EQ(play_stream->cache_ssrc2_, track_factory.screen_ssrc_);
        EXPECT_TRUE(play_stream->cache_track2_ != NULL);
        // The packet should be in the nack ring buffer.
        SrsRtpPacket *pkt = play_stream->cache_track2_->rtp_queue_->at(1000);
        EXPECT_TRUE(pkt != NULL);
        EXPECT_EQ(pkt->header_.get_ssrc(), track_factory.screen_ssrc_);
    }

    // Receive a video packet again, to verify the hit cache track.
    if (true) {
        SrsUniquePtr<SrsRtpPacket> test_pkt(new SrsRtpPacket());
        test_pkt->frame_type_ = SrsFrameTypeVideo;
        test_pkt->header_.set_sequence(1001);
        test_pkt->header_.set_timestamp(5001);
        test_pkt->header_.set_ssrc(track_factory.video_ssrc_);

        // Push packet to source - this will feed all consumers including the play stream's consumer
        HELPER_EXPECT_SUCCESS(play_stream->source_->on_rtp(test_pkt.get()));

        // Wait for coroutine to process the packet
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);

        // Check sender should have received the packet
        EXPECT_EQ(mock_sender.send_packet_count_, 4);
        // Check NACK ring buffer.
        SrsRtpPacket *pkt = play_stream->cache_track0_->rtp_queue_->at(1001);
        EXPECT_TRUE(pkt != NULL);
        EXPECT_EQ(pkt->header_.get_ssrc(), track_factory.video_ssrc_);
    }

    // Receive a audio packet again, to verify the hit cache track.
    if (true) {
        SrsUniquePtr<SrsRtpPacket> test_pkt(new SrsRtpPacket());
        test_pkt->frame_type_ = SrsFrameTypeAudio;
        test_pkt->header_.set_sequence(1001);
        test_pkt->header_.set_timestamp(5001);
        test_pkt->header_.set_ssrc(track_factory.audio_ssrc_);

        // Push packet to source - this will feed all consumers including the play stream's consumer
        HELPER_EXPECT_SUCCESS(play_stream->source_->on_rtp(test_pkt.get()));

        // Wait for coroutine to process the packet
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);

        // Check sender should have received the packet
        EXPECT_EQ(mock_sender.send_packet_count_, 5);
        // Check NACK ring buffer.
        SrsRtpPacket *pkt = play_stream->cache_track1_->rtp_queue_->at(1001);
        EXPECT_TRUE(pkt != NULL);
        EXPECT_EQ(pkt->header_.get_ssrc(), track_factory.audio_ssrc_);
    }

    // Receive a screen share packet again, to verify the hit cache track.
    if (true) {
        SrsUniquePtr<SrsRtpPacket> test_pkt(new SrsRtpPacket());
        test_pkt->frame_type_ = SrsFrameTypeVideo;
        test_pkt->header_.set_sequence(1001);
        test_pkt->header_.set_timestamp(5001);
        test_pkt->header_.set_ssrc(track_factory.screen_ssrc_);

        // Push packet to source - this will feed all consumers including the play stream's consumer
        HELPER_EXPECT_SUCCESS(play_stream->source_->on_rtp(test_pkt.get()));

        // Wait for coroutine to process the packet
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);

        // Check sender should have received the packet
        EXPECT_EQ(mock_sender.send_packet_count_, 6);
        // Check NACK ring buffer.
        SrsRtpPacket *pkt = play_stream->cache_track2_->rtp_queue_->at(1001);
        EXPECT_TRUE(pkt != NULL);
        EXPECT_EQ(pkt->header_.get_ssrc(), track_factory.screen_ssrc_);
    }

    // Stop the play stream
    play_stream->stop();
}
