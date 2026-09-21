//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_utest_workflow_rtc_playstream.hpp>

#include <string.h>

#include <srs_kernel_kbps.hpp>

#include <srs_kernel_rtc_rtcp.hpp>

#include <srs_kernel_rtc_rtp.hpp>

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

// Build a play stream on the mock sender with the factory's audio and video tracks, the video track carrying RTX as
// the Phase 2 negotiation produces it when rtx_pt is not 0: the rtx payload for the media payload type, on rtx_ssrc.
// Push one video packet with a payload through the source so the send track sends and caches it, then NACK it through
// the public on_rtcp and return the bytes the mock sender captured.
class MockRtcPlayStreamRtxScenario
{
public:
    MockAppConfig config_;
    MockRtcSourceManager rtc_sources_;
    MockAppStatistic stat_;
    MockRtcAsyncCallRequest request_;
    MockRtcAsyncTaskExecutor exec_;
    MockExpire expire_;
    MockRtcPacketSender sender_;
    MockRtcTrackDescriptionFactory track_factory_;
    SrsContextId cid_;
    SrsUniquePtr<SrsRtcPlayStream> play_stream_;
    uint8_t payload_[4];

public:
    MockRtcPlayStreamRtxScenario() : request_("test.vhost", "live", "stream1"), play_stream_(new SrsRtcPlayStream(&exec_, &expire_, &sender_, cid_))
    {
        play_stream_->config_ = &config_;
        play_stream_->rtc_sources_ = &rtc_sources_;
        play_stream_->stat_ = &stat_;
        // Make the consumer block until a packet is pushed.
        config_.mw_msgs_ = 0;

        payload_[0] = 0x65;
        payload_[1] = 0x88;
        payload_[2] = 0x84;
        payload_[3] = 0x00;
    }
    virtual ~MockRtcPlayStreamRtxScenario()
    {
        play_stream_->stop();
    }

public:
    srs_error_t start(uint8_t rtx_pt, uint32_t rtx_ssrc)
    {
        srs_error_t err = srs_success;

        std::map<uint32_t, SrsRtcTrackDescription *> sub_relations = track_factory_.create_audio_video_tracks();
        if (rtx_pt) {
            SrsRtcTrackDescription *video_desc = sub_relations[track_factory_.video_ssrc_];
            video_desc->rtx_ = new SrsRtxPayloadDes(rtx_pt, track_factory_.video_pt_, 90000);
            video_desc->rtx_ssrc_ = rtx_ssrc;
        }

        if ((err = play_stream_->initialize(&request_, sub_relations)) != srs_success) {
            return srs_error_wrap(err, "initialize");
        }
        if ((err = play_stream_->start()) != srs_success) {
            return srs_error_wrap(err, "start");
        }

        // Wait for the coroutine to start and block on the consumer.
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);
        return err;
    }
    // Push a marked video packet with the payload, and wait for the send track to send and cache it.
    srs_error_t push_video(uint16_t seq)
    {
        srs_error_t err = srs_success;

        SrsUniquePtr<SrsRtpPacket> pkt(new SrsRtpPacket());
        pkt->frame_type_ = SrsFrameTypeVideo;
        pkt->header_.set_sequence(seq);
        pkt->header_.set_timestamp(5000);
        pkt->header_.set_ssrc(track_factory_.video_ssrc_);
        pkt->header_.set_payload_type(track_factory_.video_pt_);
        pkt->header_.set_marker(true);

        SrsRtpRawPayload *raw = new SrsRtpRawPayload();
        raw->payload_ = (char *)payload_;
        raw->nn_payload_ = sizeof(payload_);
        pkt->set_payload(raw, SrsRtpPacketPayloadTypeRaw);

        if ((err = play_stream_->source_->on_rtp(pkt.get())) != srs_success) {
            return srs_error_wrap(err, "on rtp");
        }

        srs_usleep(1 * SRS_UTIME_MILLISECONDS);
        return err;
    }
    // NACK one sequence on the SSRC through the public RTCP entry of the play stream.
    srs_error_t nack(uint32_t ssrc, uint16_t seq)
    {
        SrsRtcpNack rtcp;
        rtcp.set_media_ssrc(ssrc);
        rtcp.add_lost_sn(seq);
        return play_stream_->on_rtcp(&rtcp);
    }
};

static uint32_t rtp_ssrc_of(const std::string &pkt) { return srs_rtp_fast_parse_ssrc((char *)pkt.data(), (int)pkt.size()); }
static uint16_t rtp_seq_of(const std::string &pkt) { return srs_rtp_fast_parse_seq((char *)pkt.data(), (int)pkt.size()); }
static uint8_t rtp_pt_of(const std::string &pkt) { return srs_rtp_fast_parse_pt((char *)pkt.data(), (int)pkt.size()); }

// With RTX negotiated on the video track, a player's NACK is answered on the RTX SSRC with the RTX payload type: the
// original timestamp and marker, then the two-byte OSN, then the original payload unchanged, RFC 4588 section 4.
VOID TEST(BasicWorkflowRtcPlayStreamTest, RtxRetransmitsOnNack)
{
    srs_error_t err;

    const uint32_t rtx_ssrc = 200002;
    const uint8_t rtx_pt = 107;

    MockRtcPlayStreamRtxScenario s;
    HELPER_ASSERT_SUCCESS(s.start(rtx_pt, rtx_ssrc));
    HELPER_ASSERT_SUCCESS(s.push_video(1000));

    ASSERT_EQ(1, (int)s.sender_.sent_packets_.size());
    std::string media = s.sender_.sent_packets_[0];
    EXPECT_EQ(s.track_factory_.video_ssrc_, rtp_ssrc_of(media));
    uint16_t media_seq = rtp_seq_of(media);

    HELPER_ASSERT_SUCCESS(s.nack(s.track_factory_.video_ssrc_, media_seq));
    ASSERT_EQ(2, (int)s.sender_.sent_packets_.size());
    std::string rtx = s.sender_.sent_packets_[1];

    EXPECT_EQ(rtx_ssrc, rtp_ssrc_of(rtx));
    EXPECT_EQ((int)rtx_pt, (int)rtp_pt_of(rtx));
    ASSERT_EQ(media.size() + 2, rtx.size());
    // Timestamp bytes 4..7 and the marker bit are the original's.
    EXPECT_EQ(0, memcmp(media.data() + 4, rtx.data() + 4, 4));
    EXPECT_EQ((uint8_t)media[1] & 0x80, (uint8_t)rtx[1] & 0x80);
    // The OSN is the media sequence, followed by the original payload.
    EXPECT_EQ((media_seq >> 8) & 0xFF, (int)(uint8_t)rtx[12]);
    EXPECT_EQ(media_seq & 0xFF, (int)(uint8_t)rtx[13]);
    EXPECT_EQ(0, memcmp(media.data() + 12, rtx.data() + 14, media.size() - 12));

    // A second NACK for the same sequence is answered again, on the next RTX sequence, from the untouched cache.
    HELPER_ASSERT_SUCCESS(s.nack(s.track_factory_.video_ssrc_, media_seq));
    ASSERT_EQ(3, (int)s.sender_.sent_packets_.size());
    std::string rtx2 = s.sender_.sent_packets_[2];
    EXPECT_EQ((uint16_t)(rtp_seq_of(rtx) + 1), rtp_seq_of(rtx2));
    EXPECT_EQ(0, memcmp(rtx.data() + 4, rtx2.data() + 4, rtx.size() - 4));

    // A NACK naming the RTX SSRC is ignored: nothing is sent and the media cache is not searched.
    int rmnack = _srs_pps_rmnack->sugar_;
    HELPER_ASSERT_SUCCESS(s.nack(rtx_ssrc, rtp_seq_of(rtx)));
    EXPECT_EQ(3, (int)s.sender_.sent_packets_.size());
    EXPECT_EQ(rmnack, (int)_srs_pps_rmnack->sugar_);
}

// Without rtx on the track, as negotiation leaves it for a player that offered no rtx or under nack_prefer_rtx off,
// the same NACK is answered with the cached packet unchanged: media SSRC, payload type and sequence, and no packet on
// any second SSRC. This passes from the start and locks in plain retransmission as the accepted fallback.
VOID TEST(BasicWorkflowRtcPlayStreamTest, PlainRetransmitsOnNackWithoutRtx)
{
    srs_error_t err;

    MockRtcPlayStreamRtxScenario s;
    HELPER_ASSERT_SUCCESS(s.start(0, 0));
    HELPER_ASSERT_SUCCESS(s.push_video(1000));

    ASSERT_EQ(1, (int)s.sender_.sent_packets_.size());
    std::string media = s.sender_.sent_packets_[0];

    HELPER_ASSERT_SUCCESS(s.nack(s.track_factory_.video_ssrc_, rtp_seq_of(media)));
    ASSERT_EQ(2, (int)s.sender_.sent_packets_.size());
    EXPECT_EQ(media, s.sender_.sent_packets_[1]);
}

#ifdef SRS_NACK_DEBUG_LOG_ENABLED
// The simulator build logs the play side packet by packet, so RTX is verified from the log alone: the NACK a player
// sends, with the media SSRC and the sequences, and the RTX packet that answers it, with the RTX SSRC, payload type
// and sequence, the OSN, and the media it repairs. A NACK naming the RTX SSRC is logged as ignored, and a
// sequence that is not cached is logged as a miss.
VOID TEST(BasicWorkflowRtcPlayStreamTest, RtxRetransmitsOnNackLogsDetail)
{
    srs_error_t err;

    const uint32_t rtx_ssrc = 200002;
    const uint8_t rtx_pt = 107;

    MockRtcPlayStreamRtxScenario s;
    HELPER_ASSERT_SUCCESS(s.start(rtx_pt, rtx_ssrc));
    HELPER_ASSERT_SUCCESS(s.push_video(1000));
    ASSERT_EQ(1, (int)s.sender_.sent_packets_.size());
    uint32_t media_ssrc = s.track_factory_.video_ssrc_;
    uint32_t media_pt = s.track_factory_.video_pt_;
    uint16_t media_seq = rtp_seq_of(s.sender_.sent_packets_[0]);

    MockLogForNack log;
    HELPER_ASSERT_SUCCESS(s.nack(media_ssrc, media_seq));
    ASSERT_EQ(2, (int)s.sender_.sent_packets_.size());
    std::string rtx = s.sender_.sent_packets_[1];

    char expected[256];
    snprintf(expected, sizeof(expected), "NACK: received ssrc=%u, seqs=[%u]", media_ssrc, media_seq);
    EXPECT_EQ(1, log.count(expected)) << log.find("NACK:");
    snprintf(expected, sizeof(expected), "NACK: resend RTX seq=%u, ssrc=%u, pt=%u, osn=%u, media ssrc=%u, pt=%u",
             rtp_seq_of(rtx), rtx_ssrc, (uint32_t)rtx_pt, media_seq, media_ssrc, media_pt);
    EXPECT_EQ(1, log.count(expected)) << log.find("NACK: resend");
    EXPECT_EQ(0, log.count("NACK: resend plain"));

    HELPER_ASSERT_SUCCESS(s.nack(rtx_ssrc, rtp_seq_of(rtx)));
    EXPECT_EQ(2, (int)s.sender_.sent_packets_.size());
    snprintf(expected, sizeof(expected), "NACK: received ssrc=%u is RTX, ignored", rtx_ssrc);
    EXPECT_EQ(1, log.count(expected)) << log.find("NACK: received");

    uint16_t missing = (uint16_t)(media_seq + 100);
    HELPER_ASSERT_SUCCESS(s.nack(media_ssrc, missing));
    EXPECT_EQ(2, (int)s.sender_.sent_packets_.size());
    snprintf(expected, sizeof(expected), "NACK: miss seq=%u, ssrc=%u", missing, media_ssrc);
    EXPECT_EQ(1, log.count(expected)) << log.find("NACK: miss");
}

// Without rtx on the track, the same NACK is logged as answered plain: the cached packet's own sequence, SSRC and
// payload type, and no RTX line, so the fallback is verified from the log the same way.
VOID TEST(BasicWorkflowRtcPlayStreamTest, PlainRetransmitsOnNackLogsDetail)
{
    srs_error_t err;

    MockRtcPlayStreamRtxScenario s;
    HELPER_ASSERT_SUCCESS(s.start(0, 0));
    HELPER_ASSERT_SUCCESS(s.push_video(1000));
    ASSERT_EQ(1, (int)s.sender_.sent_packets_.size());
    uint32_t media_ssrc = s.track_factory_.video_ssrc_;
    uint32_t media_pt = s.track_factory_.video_pt_;
    uint16_t media_seq = rtp_seq_of(s.sender_.sent_packets_[0]);

    MockLogForNack log;
    HELPER_ASSERT_SUCCESS(s.nack(media_ssrc, media_seq));
    ASSERT_EQ(2, (int)s.sender_.sent_packets_.size());

    char expected[256];
    snprintf(expected, sizeof(expected), "NACK: received ssrc=%u, seqs=[%u]", media_ssrc, media_seq);
    EXPECT_EQ(1, log.count(expected)) << log.find("NACK:");
    snprintf(expected, sizeof(expected), "NACK: resend plain seq=%u, ssrc=%u, pt=%u", media_seq, media_ssrc, media_pt);
    EXPECT_EQ(1, log.count(expected)) << log.find("NACK: resend");
    EXPECT_EQ(0, log.count("NACK: resend RTX"));
}
#endif
