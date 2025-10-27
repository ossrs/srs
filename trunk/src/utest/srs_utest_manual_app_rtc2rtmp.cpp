//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_utest_manual_app_rtc2rtmp.hpp>

#include <srs_app_rtc_source.hpp>
#include <srs_app_stream_bridge.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_codec.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_packet.hpp>
#include <srs_kernel_rtc_rtp.hpp>
#include <srs_protocol_rtmp_stack.hpp>

#include <srs_utest_ai09.hpp>
#include <srs_utest_manual_service.hpp>

#include <vector>
using namespace std;

MockRtc2RtmpFrameTarget::MockRtc2RtmpFrameTarget()
{
    on_frame_count_ = 0;
    last_frame_ = NULL;
    frame_error_ = srs_success;
}

MockRtc2RtmpFrameTarget::~MockRtc2RtmpFrameTarget()
{
    reset();
}

srs_error_t MockRtc2RtmpFrameTarget::on_frame(SrsMediaPacket *frame)
{
    on_frame_count_++;

    // Store a copy of the frame for verification
    srs_freep(last_frame_);
    if (frame) {
        last_frame_ = frame->copy();
    }

    return srs_error_copy(frame_error_);
}

void MockRtc2RtmpFrameTarget::reset()
{
    on_frame_count_ = 0;
    srs_freep(last_frame_);
    srs_freep(frame_error_);
}

void MockRtc2RtmpFrameTarget::set_frame_error(srs_error_t err)
{
    srs_freep(frame_error_);
    frame_error_ = srs_error_copy(err);
}

MockRtc2RtmpRequest::MockRtc2RtmpRequest(std::string vhost, std::string app, std::string stream)
{
    vhost_ = vhost;
    app_ = app;
    stream_ = stream;
    host_ = "127.0.0.1";
}

MockRtc2RtmpRequest::~MockRtc2RtmpRequest()
{
}

ISrsRequest *MockRtc2RtmpRequest::copy()
{
    MockRtc2RtmpRequest *cp = new MockRtc2RtmpRequest();
    cp->vhost_ = vhost_;
    cp->app_ = app_;
    cp->stream_ = stream_;
    cp->host_ = host_;
    return cp;
}

std::string MockRtc2RtmpRequest::get_stream_url()
{
    return "rtmp://" + host_ + "/" + app_ + "/" + stream_ + "?vhost=" + vhost_;
}

void MockRtc2RtmpRequest::update_auth(ISrsRequest *req)
{
}

void MockRtc2RtmpRequest::strip()
{
}

ISrsRequest *MockRtc2RtmpRequest::as_http()
{
    return copy();
}

// Helper function to create a mock NALU sample
SrsNaluSample *mock_create_nalu_sample(const uint8_t *data, int size)
{
    SrsNaluSample *sample = new SrsNaluSample();
    sample->bytes_ = new char[size];
    memcpy(sample->bytes_, data, size);
    sample->size_ = size;
    return sample;
}

// Helper function to create STAP-A RTP packet with SPS and PPS
SrsRtpPacket *mock_create_stap_packet_with_sps_pps(uint16_t seq, uint32_t ts)
{
    SrsRtpPacket *pkt = new SrsRtpPacket();
    pkt->header_.set_ssrc(12345);
    pkt->header_.set_sequence(seq);
    pkt->header_.set_timestamp(ts);
    pkt->frame_type_ = SrsFrameTypeVideo;
    pkt->set_avsync_time(1000);
    pkt->nalu_type_ = kStapA;

    // Create STAP-A payload with SPS and PPS
    SrsRtpSTAPPayload *stap = new SrsRtpSTAPPayload();

    // Mock SPS data (H.264 SPS NALU)
    uint8_t sps_data[] = {0x67, 0x42, 0x00, 0x1e, 0x9a, 0x66, 0x02, 0x80};
    SrsNaluSample *sps = mock_create_nalu_sample(sps_data, sizeof(sps_data));
    stap->nalus_.push_back(sps);

    // Mock PPS data (H.264 PPS NALU)
    uint8_t pps_data[] = {0x68, 0xce, 0x3c, 0x80};
    SrsNaluSample *pps = mock_create_nalu_sample(pps_data, sizeof(pps_data));
    stap->nalus_.push_back(pps);

    pkt->set_payload(stap, SrsRtpPacketPayloadTypeSTAP);
    return pkt;
}

// Helper function to create STAP-A RTP packet with SPS, PPS and small IDR frame
SrsRtpPacket *mock_create_stap_packet_with_sps_pps_idr(uint16_t seq, uint32_t ts)
{
    SrsRtpPacket *pkt = new SrsRtpPacket();
    pkt->header_.set_ssrc(12345);
    pkt->header_.set_sequence(seq);
    pkt->header_.set_timestamp(ts);
    pkt->frame_type_ = SrsFrameTypeVideo;
    pkt->set_avsync_time(1000);
    pkt->nalu_type_ = kStapA;
    pkt->header_.set_marker(true); // Single packet contains complete frame

    // Create STAP-A payload with SPS, PPS and small IDR
    SrsRtpSTAPPayload *stap = new SrsRtpSTAPPayload();

    // Mock SPS data (H.264 SPS NALU)
    uint8_t sps_data[] = {0x67, 0x42, 0x00, 0x1e, 0x9a, 0x66, 0x02, 0x80};
    SrsNaluSample *sps = mock_create_nalu_sample(sps_data, sizeof(sps_data));
    stap->nalus_.push_back(sps);

    // Mock PPS data (H.264 PPS NALU)
    uint8_t pps_data[] = {0x68, 0xce, 0x3c, 0x80};
    SrsNaluSample *pps = mock_create_nalu_sample(pps_data, sizeof(pps_data));
    stap->nalus_.push_back(pps);

    // Mock small IDR data (~200 bytes)
    uint8_t idr_data[200];
    idr_data[0] = 0x65; // IDR NALU header
    for (int i = 1; i < 200; i++) {
        idr_data[i] = (uint8_t)(0x10 + (i % 128));
    }
    SrsNaluSample *idr = mock_create_nalu_sample(idr_data, sizeof(idr_data));
    stap->nalus_.push_back(idr);

    pkt->set_payload(stap, SrsRtpPacketPayloadTypeSTAP);
    return pkt;
}

// Helper function to create IDR frame RTP packet
SrsRtpPacket *mock_create_idr_packet(uint16_t seq, uint32_t ts, bool marker)
{
    SrsRtpPacket *pkt = new SrsRtpPacket();
    pkt->header_.set_ssrc(12345);
    pkt->header_.set_sequence(seq);
    pkt->header_.set_timestamp(ts);
    pkt->frame_type_ = SrsFrameTypeVideo;
    pkt->set_avsync_time(1000);
    pkt->nalu_type_ = SrsAvcNaluTypeIDR;

    // Set marker bit if last packet for a frame.
    pkt->header_.set_marker(marker);

    // Create raw payload with IDR data
    SrsRtpRawPayload *raw = new SrsRtpRawPayload();
    char *payload_data = pkt->wrap(200);

    // Fill with mock IDR data
    payload_data[0] = 0x65; // IDR NALU header
    for (int i = 1; i < 200; i++) {
        payload_data[i] = (char)(0x10 + (i % 128));
    }

    raw->payload_ = payload_data;
    raw->nn_payload_ = 200;
    pkt->set_payload(raw, SrsRtpPacketPayloadTypeRaw);

    return pkt;
}

// Helper function to create P frame RTP packet
SrsRtpPacket *mock_create_p_frame_packet(uint16_t seq, uint32_t ts, bool marker)
{
    SrsRtpPacket *pkt = new SrsRtpPacket();
    pkt->header_.set_ssrc(12345);
    pkt->header_.set_sequence(seq);
    pkt->header_.set_timestamp(ts);
    pkt->frame_type_ = SrsFrameTypeVideo;
    pkt->set_avsync_time(2000); // Different timestamp for P frame
    pkt->nalu_type_ = SrsAvcNaluTypeNonIDR;

    // Set marker bit for single packet frame
    pkt->header_.set_marker(marker);

    // Create raw payload with P frame data
    SrsRtpRawPayload *raw = new SrsRtpRawPayload();
    char *payload_data = pkt->wrap(150);

    // Fill with mock P frame data
    payload_data[0] = 0x41; // P frame NALU header
    for (int i = 1; i < 150; i++) {
        payload_data[i] = (char)(0x20 + (i % 100));
    }

    raw->payload_ = payload_data;
    raw->nn_payload_ = 150;
    pkt->set_payload(raw, SrsRtpPacketPayloadTypeRaw);

    return pkt;
}

// Helper function to create empty IDR frame RTP packet
SrsRtpPacket *mock_create_empty_idr_packet(uint16_t seq, uint32_t ts, bool marker)
{
    SrsRtpPacket *pkt = new SrsRtpPacket();
    pkt->header_.set_ssrc(12345);
    pkt->header_.set_sequence(seq);
    pkt->header_.set_timestamp(ts);
    pkt->frame_type_ = SrsFrameTypeVideo;
    pkt->set_avsync_time(1000);
    pkt->nalu_type_ = SrsAvcNaluTypeIDR;

    // Set marker bit if last packet for a frame.
    pkt->header_.set_marker(marker);

    // Create raw payload with empty IDR data (only NALU header)
    SrsRtpRawPayload *raw = new SrsRtpRawPayload();
    char *payload_data = pkt->wrap(1);

    // Only IDR NALU header, no payload data
    payload_data[0] = 0x65; // IDR NALU header

    raw->payload_ = payload_data;
    raw->nn_payload_ = 1;
    pkt->set_payload(raw, SrsRtpPacketPayloadTypeRaw);

    return pkt;
}

// Helper function to create empty IDR frame RTP packet using FU-A format
SrsRtpPacket *mock_create_empty_idr_packet_fua(uint16_t seq, uint32_t ts)
{
    SrsRtpPacket *pkt = new SrsRtpPacket();
    pkt->header_.set_ssrc(12345);
    pkt->header_.set_sequence(seq);
    pkt->header_.set_timestamp(ts);
    pkt->frame_type_ = SrsFrameTypeVideo;
    pkt->set_avsync_time(1000);
    pkt->nalu_type_ = kFuA;

    // Set marker bit if last packet for a frame.
    pkt->header_.set_marker(true);

    // Create FU-A payload with empty IDR data
    SrsRtpFUAPayload2 *fua = new SrsRtpFUAPayload2();
    fua->start_ = true;                  // This is a complete single fragment
    fua->end_ = true;                    // This is a complete single fragment
    fua->nri_ = SrsAvcNaluTypeIDR;       // NRI bits from original IDR NALU header (0x65)
    fua->nalu_type_ = SrsAvcNaluTypeIDR; // IDR NALU type

    // Empty payload - no actual IDR data, just the fragmentation headers
    fua->payload_ = NULL;
    fua->size_ = 0;

    pkt->set_payload(fua, SrsRtpPacketPayloadTypeFUA2);

    return pkt;
}

// Helper function to create audio RTP packet
SrsRtpPacket *mock_create_audio_packet(uint16_t seq, uint32_t ts, uint32_t avsync_time)
{
    SrsRtpPacket *pkt = new SrsRtpPacket();
    pkt->header_.set_ssrc(12345);
    pkt->header_.set_sequence(seq);
    pkt->header_.set_timestamp(ts);
    pkt->frame_type_ = SrsFrameTypeAudio;
    pkt->set_avsync_time(avsync_time);

    SrsRtpRawPayload *raw = new SrsRtpRawPayload();
    char audio_data[64];
    memset(audio_data, seq & 0xFF, sizeof(audio_data));
    raw->payload_ = pkt->wrap(sizeof(audio_data));
    memcpy(raw->payload_, audio_data, sizeof(audio_data));
    raw->nn_payload_ = sizeof(audio_data);
    pkt->set_payload(raw, SrsRtpPacketPayloadTypeRaw);

    return pkt;
}

// Test SrsRtcFrameBuilder::packet_video with the simplest video sequence:
// 1. STAP-A packet with SPS/PPS
// 2. IDR frame (2 RTP packets), with same timestamp to SPS/PPS.
// 3. P frame (1 RTP packet)
// No packet loss scenario
//
// This is the most typical packet sequence to Chrome. Although SPS/PPS is in the next
// RTP packet to IDR, but they share the same timestamp, because they should be in the
// same RTP packet, but just for IDR is huge so Chrome put it in the next packet.
//
// Altough SPS/PPS and IDR is in two RTP packets, and they share the same timestamp, but
// we should only packet the IDR to frame. That is the second frame contains only IDR,
// this can be verified by the frame size.
VOID TEST(Rtc2RtmpConvertTest, PacketVideo_SameKeyframeTimestampVideoSequence)
{
    srs_error_t err;

    MockRtc2RtmpFrameTarget target;
    SrsRtcFrameBuilder builder(_srs_app_factory, &target);

    // Initialize the builder with AVC codec
    SrsUniquePtr<MockRtc2RtmpRequest> req(new MockRtc2RtmpRequest());
    HELPER_EXPECT_SUCCESS(builder.initialize(req.get(), SrsAudioCodecIdAAC, SrsVideoCodecIdAVC));

    // Reset frame count before test
    target.reset();

    // 1. Send STAP-A packet containing SPS/PPS (sequence header)
    SrsUniquePtr<SrsRtpPacket> stap_pkt(mock_create_stap_packet_with_sps_pps(100, 90000));
    HELPER_EXPECT_SUCCESS(builder.packet_video(stap_pkt.get()));

    // Should generate one frame for sequence header
    EXPECT_EQ(1, target.on_frame_count_);
    EXPECT_TRUE(target.last_frame_ != NULL);
    if (target.last_frame_) {
        EXPECT_EQ(SrsFrameTypeVideo, target.last_frame_->message_type_);
    }

    // 2. Send IDR frame as 2 RTP packets
    // First packet of IDR frame (marker bit = false)
    SrsUniquePtr<SrsRtpPacket> idr_pkt1(mock_create_idr_packet(101, 90000, false));
    HELPER_EXPECT_SUCCESS(builder.packet_video(idr_pkt1.get()));

    // Should not generate frame yet (waiting for complete frame)
    EXPECT_EQ(1, target.on_frame_count_);

    // Second packet of IDR frame (marker bit = true, completes frame)
    SrsUniquePtr<SrsRtpPacket> idr_pkt2(mock_create_idr_packet(102, 90000, true));
    HELPER_EXPECT_SUCCESS(builder.packet_video(idr_pkt2.get()));

    // Should generate second frame for IDR
    EXPECT_EQ(2, target.on_frame_count_);
    EXPECT_TRUE(target.last_frame_ != NULL);
    if (target.last_frame_) {
        EXPECT_EQ(SrsFrameTypeVideo, target.last_frame_->message_type_);
        // As each IDR is about 200 bytes, SPS/PPS is about 20 bytes, the frame
        // contains 2xIDR should be around 400 bytes
        EXPECT_GT(target.last_frame_->payload_->size(), 400);
        EXPECT_LT(target.last_frame_->payload_->size(), 420);
    }

    // 3. Send P frame as single RTP packet (marker bit = true)
    SrsUniquePtr<SrsRtpPacket> p_pkt(mock_create_p_frame_packet(103, 180000, true));
    HELPER_EXPECT_SUCCESS(builder.packet_video(p_pkt.get()));

    // Should generate third frame for P frame
    EXPECT_EQ(3, target.on_frame_count_);
    EXPECT_TRUE(target.last_frame_ != NULL);
    if (target.last_frame_) {
        EXPECT_EQ(SrsFrameTypeVideo, target.last_frame_->message_type_);
    }

    // Verify no errors occurred during processing
    EXPECT_TRUE(target.frame_error_ == srs_success);
}

// Test SrsRtcFrameBuilder::packet_video with STAP-A containing SPS/PPS/IDR:
// 1. STAP-A packet with SPS/PPS/IDR (small ~200 bytes IDR frame)
// 2. P frame (1 RTP packet)
// No packet loss scenario
//
// This tests the case where SPS/PPS/IDR are all in the same RTP packet,
// with a small IDR frame that fits within the STAP-A packet.
VOID TEST(Rtc2RtmpConvertTest, PacketVideo_SameStapForIdrVideoSequence)
{
    srs_error_t err;

    MockRtc2RtmpFrameTarget target;
    SrsRtcFrameBuilder builder(_srs_app_factory, &target);

    // Initialize the builder with AVC codec
    SrsUniquePtr<MockRtc2RtmpRequest> req(new MockRtc2RtmpRequest());
    HELPER_EXPECT_SUCCESS(builder.initialize(req.get(), SrsAudioCodecIdAAC, SrsVideoCodecIdAVC));

    // Reset frame count before test
    target.reset();

    // 1. Send STAP-A packet containing SPS/PPS/IDR (sequence header + keyframe)
    SrsUniquePtr<SrsRtpPacket> stap_pkt(mock_create_stap_packet_with_sps_pps_idr(100, 90000));
    HELPER_EXPECT_SUCCESS(builder.packet_video(stap_pkt.get()));

    // For single packet IDR frame, we don't know whether it's really a single packet or
    // reordering packet, for example, it may be one of following cases:
    //      Single packet of a IDR frame:
    //          IDR (marker bit = true) seq=100
    //      Two packets of a IDR frame, this is the reordering case:
    //          IDR (marker bit = true)  seq=100
    //          IDR (marker bit = false) seq=99
    // Should be triggered by next frame.
    EXPECT_EQ(1, target.on_frame_count_);

    // 2. Send P frame as single RTP packet (marker bit = true)
    SrsUniquePtr<SrsRtpPacket> p_pkt(mock_create_p_frame_packet(101, 180000, true));
    HELPER_EXPECT_SUCCESS(builder.packet_video(p_pkt.get()));

    // Should generate 2 frames for previous IDR and this P frame
    EXPECT_EQ(3, target.on_frame_count_);
    EXPECT_TRUE(target.last_frame_ != NULL);
    if (target.last_frame_) {
        EXPECT_EQ(SrsFrameTypeVideo, target.last_frame_->message_type_);
    }

    // Verify no errors occurred during processing
    EXPECT_TRUE(target.frame_error_ == srs_success);
}

// Test SrsRtcFrameBuilder::packet_video with the simplest video sequence:
// 1. STAP-A packet with SPS/PPS
// 2. IDR frame (2 RTP packets), with different timestamp to SPS/PPS.
// 3. P frame (1 RTP packet)
// No packet loss scenario
//
// If the IDR is a single RTP packet with different timestamp, after SPS/PPS be packeted
// as a frame, IDR will also be packeted as another frame. This can be verified by the
// frame size.
VOID TEST(Rtc2RtmpConvertTest, PacketVideo_DifferentKeyframeTimestampVideoSequence)
{
    srs_error_t err;

    MockRtc2RtmpFrameTarget target;
    SrsRtcFrameBuilder builder(_srs_app_factory, &target);

    // Initialize the builder with AVC codec
    SrsUniquePtr<MockRtc2RtmpRequest> req(new MockRtc2RtmpRequest());
    HELPER_EXPECT_SUCCESS(builder.initialize(req.get(), SrsAudioCodecIdAAC, SrsVideoCodecIdAVC));

    // Reset frame count before test
    target.reset();

    // 1. Send STAP-A packet containing SPS/PPS (sequence header)
    SrsUniquePtr<SrsRtpPacket> stap_pkt(mock_create_stap_packet_with_sps_pps(100, 90000));
    HELPER_EXPECT_SUCCESS(builder.packet_video(stap_pkt.get()));

    // Should generate one frame for sequence header
    EXPECT_EQ(1, target.on_frame_count_);
    EXPECT_TRUE(target.last_frame_ != NULL);
    if (target.last_frame_) {
        EXPECT_EQ(SrsFrameTypeVideo, target.last_frame_->message_type_);
    }

    // 2. Send IDR frame as 2 RTP packets
    // First packet of IDR frame (marker bit = false)
    SrsUniquePtr<SrsRtpPacket> idr_pkt1(mock_create_idr_packet(101, 180000, false));
    HELPER_EXPECT_SUCCESS(builder.packet_video(idr_pkt1.get()));

    // Should not generate frame yet (waiting for complete frame)
    EXPECT_EQ(1, target.on_frame_count_);

    // Second packet of IDR frame (marker bit = true, completes frame)
    SrsUniquePtr<SrsRtpPacket> idr_pkt2(mock_create_idr_packet(102, 180000, true));
    HELPER_EXPECT_SUCCESS(builder.packet_video(idr_pkt2.get()));

    // Should generate second frame for IDR
    EXPECT_EQ(2, target.on_frame_count_);
    EXPECT_TRUE(target.last_frame_ != NULL);
    if (target.last_frame_) {
        EXPECT_EQ(SrsFrameTypeVideo, target.last_frame_->message_type_);
        // As each IDR is about 200 bytes, SPS/PPS is about 20 bytes, the frame contains
        // 2xIDR should be around 400 bytes.
        EXPECT_GT(target.last_frame_->payload_->size(), 400);
        EXPECT_LT(target.last_frame_->payload_->size(), 420);
    }

    // 3. Send P frame as single RTP packet (marker bit = true)
    SrsUniquePtr<SrsRtpPacket> p_pkt(mock_create_p_frame_packet(103, 270000, true));
    HELPER_EXPECT_SUCCESS(builder.packet_video(p_pkt.get()));

    // Should generate third frame for P frame
    EXPECT_EQ(3, target.on_frame_count_);
    EXPECT_TRUE(target.last_frame_ != NULL);
    if (target.last_frame_) {
        EXPECT_EQ(SrsFrameTypeVideo, target.last_frame_->message_type_);
    }

    // Verify no errors occurred during processing
    EXPECT_TRUE(target.frame_error_ == srs_success);
}

// Test SrsRtcFrameBuilder::packet_video with typical video sequence:
// 1. STAP-A packet with SPS/PPS
// 2. IDR frame (3 RTP packets), with same timestamp to SPS/PPS.
// 3. P frame (1 RTP packet)
// No packet loss scenario
VOID TEST(Rtc2RtmpConvertTest, PacketVideo_TypicalVideoSequence)
{
    srs_error_t err;

    MockRtc2RtmpFrameTarget target;
    SrsRtcFrameBuilder builder(_srs_app_factory, &target);

    // Initialize the builder with AVC codec
    SrsUniquePtr<MockRtc2RtmpRequest> req(new MockRtc2RtmpRequest());
    HELPER_EXPECT_SUCCESS(builder.initialize(req.get(), SrsAudioCodecIdAAC, SrsVideoCodecIdAVC));

    // Reset frame count before test
    target.reset();

    // 1. Send STAP-A packet containing SPS/PPS (sequence header)
    SrsUniquePtr<SrsRtpPacket> stap_pkt(mock_create_stap_packet_with_sps_pps(100, 90000));
    HELPER_EXPECT_SUCCESS(builder.packet_video(stap_pkt.get()));

    // Should generate one frame for sequence header
    EXPECT_EQ(1, target.on_frame_count_);
    EXPECT_TRUE(target.last_frame_ != NULL);
    if (target.last_frame_) {
        EXPECT_EQ(SrsFrameTypeVideo, target.last_frame_->message_type_);
    }

    // 2. Send IDR frame as 3 RTP packets
    // First packet of IDR frame (marker bit = false)
    SrsUniquePtr<SrsRtpPacket> idr_pkt1(mock_create_idr_packet(101, 90000, false));
    HELPER_EXPECT_SUCCESS(builder.packet_video(idr_pkt1.get()));

    // Should not generate frame yet (waiting for complete frame)
    EXPECT_EQ(1, target.on_frame_count_);

    // Second packet of IDR frame (marker bit = true, completes frame)
    SrsUniquePtr<SrsRtpPacket> idr_pkt2(mock_create_idr_packet(102, 90000, false));
    HELPER_EXPECT_SUCCESS(builder.packet_video(idr_pkt2.get()));

    // Should not generate frame yet (waiting for complete frame)
    EXPECT_EQ(1, target.on_frame_count_);

    // Second packet of IDR frame (marker bit = true, completes frame)
    SrsUniquePtr<SrsRtpPacket> idr_pkt3(mock_create_idr_packet(103, 90000, true));
    HELPER_EXPECT_SUCCESS(builder.packet_video(idr_pkt3.get()));

    // Should generate second frame for IDR
    EXPECT_EQ(2, target.on_frame_count_);
    EXPECT_TRUE(target.last_frame_ != NULL);
    if (target.last_frame_) {
        EXPECT_EQ(SrsFrameTypeVideo, target.last_frame_->message_type_);
    }

    // 3. Send P frame as single RTP packet (marker bit = true)
    SrsUniquePtr<SrsRtpPacket> p_pkt(mock_create_p_frame_packet(104, 180000, true));
    HELPER_EXPECT_SUCCESS(builder.packet_video(p_pkt.get()));

    // Should generate third frame for P frame
    EXPECT_EQ(3, target.on_frame_count_);
    EXPECT_TRUE(target.last_frame_ != NULL);
    if (target.last_frame_) {
        EXPECT_EQ(SrsFrameTypeVideo, target.last_frame_->message_type_);
    }

    // Verify no errors occurred during processing
    EXPECT_TRUE(target.frame_error_ == srs_success);
}

// Test SrsRtcFrameBuilder::packet_video with the simplest video sequence:
// 1. STAP-A packet with SPS/PPS
// 2. IDR frame (1 RTP packets), with same timestamp to SPS/PPS.
// 3. P frame (1 RTP packet)
// No packet loss scenario
VOID TEST(Rtc2RtmpConvertTest, PacketVideo_SingleVideoSequence)
{
    srs_error_t err;

    MockRtc2RtmpFrameTarget target;
    SrsRtcFrameBuilder builder(_srs_app_factory, &target);

    // Initialize the builder with AVC codec
    SrsUniquePtr<MockRtc2RtmpRequest> req(new MockRtc2RtmpRequest());
    HELPER_EXPECT_SUCCESS(builder.initialize(req.get(), SrsAudioCodecIdAAC, SrsVideoCodecIdAVC));

    // Reset frame count before test
    target.reset();

    // 1. Send STAP-A packet containing SPS/PPS (sequence header)
    SrsUniquePtr<SrsRtpPacket> stap_pkt(mock_create_stap_packet_with_sps_pps(100, 90000));
    HELPER_EXPECT_SUCCESS(builder.packet_video(stap_pkt.get()));

    // Should generate one frame for sequence header
    EXPECT_EQ(1, target.on_frame_count_);
    EXPECT_TRUE(target.last_frame_ != NULL);
    if (target.last_frame_) {
        EXPECT_EQ(SrsFrameTypeVideo, target.last_frame_->message_type_);
    }

    // 2. Send IDR frame as 1 RTP packets
    // First packet of IDR frame (marker bit = false)
    SrsUniquePtr<SrsRtpPacket> idr_pkt1(mock_create_idr_packet(101, 90000, true));
    HELPER_EXPECT_SUCCESS(builder.packet_video(idr_pkt1.get()));

    // Should generate second frame for IDR
    EXPECT_EQ(2, target.on_frame_count_);
    EXPECT_TRUE(target.last_frame_ != NULL);
    if (target.last_frame_) {
        EXPECT_EQ(SrsFrameTypeVideo, target.last_frame_->message_type_);
    }

    // 3. Send P frame as single RTP packet (marker bit = true)
    SrsUniquePtr<SrsRtpPacket> p_pkt(mock_create_p_frame_packet(102, 180000, true));
    HELPER_EXPECT_SUCCESS(builder.packet_video(p_pkt.get()));

    // Should generate third frame for P frame
    EXPECT_EQ(3, target.on_frame_count_);
    EXPECT_TRUE(target.last_frame_ != NULL);
    if (target.last_frame_) {
        EXPECT_EQ(SrsFrameTypeVideo, target.last_frame_->message_type_);
    }

    // Verify no errors occurred during processing
    EXPECT_TRUE(target.frame_error_ == srs_success);
}

// Test SrsRtcFrameBuilder::packet_video with the simplest video sequence but large P frame:
// 1. STAP-A packet with SPS/PPS
// 2. IDR frame (1 RTP packets), with same timestamp to SPS/PPS.
// 3. P frame (3 RTP packets) - large P frame requiring multiple packets
// No packet loss scenario
VOID TEST(Rtc2RtmpConvertTest, PacketVideo_SingleVideoSequence_LargePFrame)
{
    srs_error_t err;

    MockRtc2RtmpFrameTarget target;
    SrsRtcFrameBuilder builder(_srs_app_factory, &target);

    // Initialize the builder with AVC codec
    SrsUniquePtr<MockRtc2RtmpRequest> req(new MockRtc2RtmpRequest());
    HELPER_EXPECT_SUCCESS(builder.initialize(req.get(), SrsAudioCodecIdAAC, SrsVideoCodecIdAVC));

    // Reset frame count before test
    target.reset();

    // 1. Send STAP-A packet containing SPS/PPS (sequence header)
    SrsUniquePtr<SrsRtpPacket> stap_pkt(mock_create_stap_packet_with_sps_pps(100, 90000));
    HELPER_EXPECT_SUCCESS(builder.packet_video(stap_pkt.get()));

    // Should generate one frame for sequence header
    EXPECT_EQ(1, target.on_frame_count_);
    EXPECT_TRUE(target.last_frame_ != NULL);
    if (target.last_frame_) {
        EXPECT_EQ(SrsFrameTypeVideo, target.last_frame_->message_type_);
    }

    // 2. Send IDR frame as 1 RTP packets
    // First packet of IDR frame (marker bit = true)
    SrsUniquePtr<SrsRtpPacket> idr_pkt1(mock_create_idr_packet(101, 90000, true));
    HELPER_EXPECT_SUCCESS(builder.packet_video(idr_pkt1.get()));

    // Should generate second frame for IDR
    EXPECT_EQ(2, target.on_frame_count_);
    EXPECT_TRUE(target.last_frame_ != NULL);
    if (target.last_frame_) {
        EXPECT_EQ(SrsFrameTypeVideo, target.last_frame_->message_type_);
    }

    // 3. Send P frame as 3 RTP packets (large P frame)
    // First packet of P frame (marker bit = false)
    SrsUniquePtr<SrsRtpPacket> p_pkt1(mock_create_p_frame_packet(102, 180000, false));
    HELPER_EXPECT_SUCCESS(builder.packet_video(p_pkt1.get()));

    // Should not generate frame yet (waiting for complete frame)
    EXPECT_EQ(2, target.on_frame_count_);

    // Second packet of P frame (marker bit = false)
    SrsUniquePtr<SrsRtpPacket> p_pkt2(mock_create_p_frame_packet(103, 180000, false));
    HELPER_EXPECT_SUCCESS(builder.packet_video(p_pkt2.get()));

    // Should not generate frame yet (waiting for complete frame)
    EXPECT_EQ(2, target.on_frame_count_);

    // Third packet of P frame (marker bit = true, completes frame)
    SrsUniquePtr<SrsRtpPacket> p_pkt3(mock_create_p_frame_packet(104, 180000, true));
    HELPER_EXPECT_SUCCESS(builder.packet_video(p_pkt3.get()));

    // Should generate third frame for P frame
    EXPECT_EQ(3, target.on_frame_count_);
    EXPECT_TRUE(target.last_frame_ != NULL);
    if (target.last_frame_) {
        EXPECT_EQ(SrsFrameTypeVideo, target.last_frame_->message_type_);
    }

    // Verify no errors occurred during processing
    EXPECT_TRUE(target.frame_error_ == srs_success);
}

// Test SrsRtcFrameBuilder::packet_video with the simplest video sequence:
// 1. STAP-A packet with SPS/PPS
// 2. IDR frame (1 RTP packets)
// 3. P frame (1 RTP packet)
// No packet loss scenario
//
// Normally the SPS/PPS should be the same timestamp as the IDR frame, but this is not
// required. It's acceptable if the IDR RTP packet has a different timestamp. This is
// useful when testing some empty NALU case, without the SPS/PPS, only IDR packet.
VOID TEST(Rtc2RtmpConvertTest, PacketVideo_AcceptableVideoSequence)
{
    srs_error_t err;

    MockRtc2RtmpFrameTarget target;
    SrsRtcFrameBuilder builder(_srs_app_factory, &target);

    // Initialize the builder with AVC codec
    SrsUniquePtr<MockRtc2RtmpRequest> req(new MockRtc2RtmpRequest());
    HELPER_EXPECT_SUCCESS(builder.initialize(req.get(), SrsAudioCodecIdAAC, SrsVideoCodecIdAVC));

    // Reset frame count before test
    target.reset();

    // 1. Send STAP-A packet containing SPS/PPS (sequence header)
    SrsUniquePtr<SrsRtpPacket> stap_pkt(mock_create_stap_packet_with_sps_pps(100, 90000));
    HELPER_EXPECT_SUCCESS(builder.packet_video(stap_pkt.get()));

    // Should generate one frame for sequence header
    EXPECT_EQ(1, target.on_frame_count_);
    EXPECT_TRUE(target.last_frame_ != NULL);
    if (target.last_frame_) {
        EXPECT_EQ(SrsFrameTypeVideo, target.last_frame_->message_type_);
    }

    // 2. Send IDR frame as 1 RTP packets
    // First packet of IDR frame (marker bit = false)
    SrsUniquePtr<SrsRtpPacket> idr_pkt1(mock_create_idr_packet(101, 180000, true));
    HELPER_EXPECT_SUCCESS(builder.packet_video(idr_pkt1.get()));

    // For single packet IDR frame, we don't know whether it's really a single packet or
    // reordering packet, for example, it may be one of following cases:
    //      Single packet of a IDR frame:
    //          IDR (marker bit = true) seq=100
    //      Two packets of a IDR frame, this is the reordering case:
    //          IDR (marker bit = true)  seq=100
    //          IDR (marker bit = false) seq=99
    // Should be triggered by next frame.
    EXPECT_EQ(1, target.on_frame_count_);

    // 3. Send P frame as single RTP packet (marker bit = true)
    SrsUniquePtr<SrsRtpPacket> p_pkt(mock_create_p_frame_packet(102, 270000, true));
    HELPER_EXPECT_SUCCESS(builder.packet_video(p_pkt.get()));

    // Should generate 2 frames for previous IDR and this P frame
    EXPECT_EQ(3, target.on_frame_count_);
    EXPECT_TRUE(target.last_frame_ != NULL);
    if (target.last_frame_) {
        EXPECT_EQ(SrsFrameTypeVideo, target.last_frame_->message_type_);
    }

    // Verify no errors occurred during processing
    EXPECT_TRUE(target.frame_error_ == srs_success);
}

// Test SrsRtcFrameBuilder::packet_video with empty IDR packet before normal frames:
// 1. STAP-A packet with SPS/PPS
// 2. Empty IDR frame (1 RTP packet with only NALU header) in RAW payload.
// 3. Normal IDR frame (1 RTP packet)
// 4. P frame (1 RTP packet)
// No packet loss scenario
//
// This tests the case where an empty IDR packet is received before the actual
// IDR frame, which can happen in some streaming scenarios.
VOID TEST(Rtc2RtmpConvertTest, PacketVideo_EmptyIDRBeforeNormalFrames)
{
    srs_error_t err;

    MockRtc2RtmpFrameTarget target;
    SrsRtcFrameBuilder builder(_srs_app_factory, &target);

    // Initialize the builder with AVC codec
    SrsUniquePtr<MockRtc2RtmpRequest> req(new MockRtc2RtmpRequest());
    HELPER_EXPECT_SUCCESS(builder.initialize(req.get(), SrsAudioCodecIdAAC, SrsVideoCodecIdAVC));

    // Reset frame count before test
    target.reset();

    // 1. Send STAP-A packet containing SPS/PPS (sequence header)
    SrsUniquePtr<SrsRtpPacket> stap_pkt(mock_create_stap_packet_with_sps_pps(100, 90000));
    HELPER_EXPECT_SUCCESS(builder.packet_video(stap_pkt.get()));

    // Should generate one frame for sequence header
    EXPECT_EQ(1, target.on_frame_count_);
    EXPECT_TRUE(target.last_frame_ != NULL);
    if (target.last_frame_) {
        EXPECT_EQ(SrsFrameTypeVideo, target.last_frame_->message_type_);
    }

    // 2. Send empty IDR frame (only NALU header, no payload data)
    SrsUniquePtr<SrsRtpPacket> empty_idr_pkt(mock_create_empty_idr_packet(101, 180000, true));
    HELPER_EXPECT_SUCCESS(builder.packet_video(empty_idr_pkt.get()));

    // Should not generate frame yet (ignore empty frame)
    EXPECT_EQ(1, target.on_frame_count_);

    // 3. Send normal IDR frame as single RTP packet
    SrsUniquePtr<SrsRtpPacket> idr_pkt(mock_create_idr_packet(102, 270000, true));
    HELPER_EXPECT_SUCCESS(builder.packet_video(idr_pkt.get()));

    // Should be triggered by next frame.
    EXPECT_EQ(1, target.on_frame_count_);

    // 4. Send P frame as single RTP packet
    SrsUniquePtr<SrsRtpPacket> p_pkt(mock_create_p_frame_packet(103, 360000, true));
    HELPER_EXPECT_SUCCESS(builder.packet_video(p_pkt.get()));

    // Should generate 2 frames for previous IDR and this P frame
    EXPECT_EQ(3, target.on_frame_count_);
    EXPECT_TRUE(target.last_frame_ != NULL);
    if (target.last_frame_) {
        EXPECT_EQ(SrsFrameTypeVideo, target.last_frame_->message_type_);
    }

    // Verify no errors occurred during processing
    EXPECT_TRUE(target.frame_error_ == srs_success);
}

// Test SrsRtcFrameBuilder::packet_video with empty IDR packet before normal frames:
// 1. STAP-A packet with SPS/PPS
// 2. Empty IDR frame (1 RTP packet with only NALU header) in FU-A payload.
// 3. Normal IDR frame (1 RTP packet)
// 4. P frame (1 RTP packet)
// No packet loss scenario
//
// This tests the case where an empty IDR packet is received before the actual
// IDR frame, which can happen in some streaming scenarios.
VOID TEST(Rtc2RtmpConvertTest, PacketVideo_EmptyIDRBeforeNormalFrames_FUA)
{
    srs_error_t err;

    MockRtc2RtmpFrameTarget target;
    SrsRtcFrameBuilder builder(_srs_app_factory, &target);

    // Initialize the builder with AVC codec
    SrsUniquePtr<MockRtc2RtmpRequest> req(new MockRtc2RtmpRequest());
    HELPER_EXPECT_SUCCESS(builder.initialize(req.get(), SrsAudioCodecIdAAC, SrsVideoCodecIdAVC));

    // Reset frame count before test
    target.reset();

    // 1. Send STAP-A packet containing SPS/PPS (sequence header)
    SrsUniquePtr<SrsRtpPacket> stap_pkt(mock_create_stap_packet_with_sps_pps(100, 90000));
    HELPER_EXPECT_SUCCESS(builder.packet_video(stap_pkt.get()));

    // Should generate one frame for sequence header
    EXPECT_EQ(1, target.on_frame_count_);
    EXPECT_TRUE(target.last_frame_ != NULL);
    if (target.last_frame_) {
        EXPECT_EQ(SrsFrameTypeVideo, target.last_frame_->message_type_);
    }

    // 2. Send empty IDR frame (only NALU header, no payload data)
    SrsUniquePtr<SrsRtpPacket> empty_idr_pkt(mock_create_empty_idr_packet_fua(101, 180000));
    HELPER_EXPECT_SUCCESS(builder.packet_video(empty_idr_pkt.get()));

    // Should not generate frame yet (ignore empty frame)
    EXPECT_EQ(1, target.on_frame_count_);

    // 3. Send normal IDR frame as single RTP packet
    SrsUniquePtr<SrsRtpPacket> idr_pkt(mock_create_idr_packet(102, 270000, true));
    HELPER_EXPECT_SUCCESS(builder.packet_video(idr_pkt.get()));

    // Should be triggered by next frame.
    EXPECT_EQ(1, target.on_frame_count_);

    // 4. Send P frame as single RTP packet
    SrsUniquePtr<SrsRtpPacket> p_pkt(mock_create_p_frame_packet(103, 360000, true));
    HELPER_EXPECT_SUCCESS(builder.packet_video(p_pkt.get()));

    // Should generate 2 frames for previous IDR and this P frame
    EXPECT_EQ(3, target.on_frame_count_);
    EXPECT_TRUE(target.last_frame_ != NULL);
    if (target.last_frame_) {
        EXPECT_EQ(SrsFrameTypeVideo, target.last_frame_->message_type_);
    }

    // Verify no errors occurred during processing
    EXPECT_TRUE(target.frame_error_ == srs_success);
}

// Test SrsRtcFrameBuilder::packet_video with the packet-reordering video sequence:
// 1. STAP-A packet with SPS/PPS
// 2. IDR frame (2 RTP packets)
// 3. P frame (2 RTP packets)
// No packet loss scenario
//
// This is the baseline case, for IDR with 2 RTP packets, and P frame with 2 RTP packets.
// This is useful to conduct the packet reordering or lost tests.
VOID TEST(Rtc2RtmpConvertTest, PacketVideo_BaselinePackets)
{
    srs_error_t err;

    MockRtc2RtmpFrameTarget target;
    SrsRtcFrameBuilder builder(_srs_app_factory, &target);

    // Initialize the builder with AVC codec
    SrsUniquePtr<MockRtc2RtmpRequest> req(new MockRtc2RtmpRequest());
    HELPER_EXPECT_SUCCESS(builder.initialize(req.get(), SrsAudioCodecIdAAC, SrsVideoCodecIdAVC));

    // Reset frame count before test
    target.reset();

    // 1. Send STAP-A packet containing SPS/PPS (sequence header)
    SrsUniquePtr<SrsRtpPacket> stap_pkt(mock_create_stap_packet_with_sps_pps(100, 90000));
    HELPER_EXPECT_SUCCESS(builder.packet_video(stap_pkt.get()));

    // Should generate one frame for sequence header
    EXPECT_EQ(1, target.on_frame_count_);
    EXPECT_TRUE(target.last_frame_ != NULL);
    if (target.last_frame_) {
        EXPECT_EQ(SrsFrameTypeVideo, target.last_frame_->message_type_);
    }

    // 2. Send IDR frame as 2 RTP packets
    // First packet of IDR frame (marker bit = false)
    SrsUniquePtr<SrsRtpPacket> idr_pkt1(mock_create_idr_packet(101, 180000, false));
    HELPER_EXPECT_SUCCESS(builder.packet_video(idr_pkt1.get()));

    // Should not generate frame yet (waiting for complete frame)
    EXPECT_EQ(1, target.on_frame_count_);

    // Second packet of IDR frame (marker bit = true, completes frame)
    SrsUniquePtr<SrsRtpPacket> idr_pkt2(mock_create_idr_packet(102, 180000, true));
    HELPER_EXPECT_SUCCESS(builder.packet_video(idr_pkt2.get()));

    // First packet of I frame.
    EXPECT_EQ(2, target.on_frame_count_);

    // 3. Send P frame as single RTP packet (marker bit = true)
    SrsUniquePtr<SrsRtpPacket> p_pkt(mock_create_p_frame_packet(103, 270000, false));
    HELPER_EXPECT_SUCCESS(builder.packet_video(p_pkt.get()));

    // Should not generate frame yet (waiting for complete frame)
    EXPECT_EQ(2, target.on_frame_count_);

    // Second packet of P frame.
    SrsUniquePtr<SrsRtpPacket> p_pkt2(mock_create_p_frame_packet(104, 270000, true));
    HELPER_EXPECT_SUCCESS(builder.packet_video(p_pkt2.get()));

    // Should generate P frame.
    EXPECT_EQ(3, target.on_frame_count_);
    EXPECT_TRUE(target.last_frame_ != NULL);
    if (target.last_frame_) {
        EXPECT_EQ(SrsFrameTypeVideo, target.last_frame_->message_type_);
    }

    // Verify no errors occurred during processing
    EXPECT_TRUE(target.frame_error_ == srs_success);
}

// Test SrsRtcFrameBuilder::packet_video with the packet-reordering video sequence:
// 1. IDR frame (2 RTP packets)
// 2. P frame (2 RTP packets)
// No packet loss scenario
//
// This is the bad case, no SPS/PPS, only IDR frame and P frame.
VOID TEST(Rtc2RtmpConvertTest, PacketVideo_NoSpsPpsPackets)
{
    srs_error_t err;

    MockRtc2RtmpFrameTarget target;
    SrsRtcFrameBuilder builder(_srs_app_factory, &target);

    // Initialize the builder with AVC codec
    SrsUniquePtr<MockRtc2RtmpRequest> req(new MockRtc2RtmpRequest());
    HELPER_EXPECT_SUCCESS(builder.initialize(req.get(), SrsAudioCodecIdAAC, SrsVideoCodecIdAVC));

    // Reset frame count before test
    target.reset();

    // 1. Send IDR frame as 2 RTP packets
    // First packet of IDR frame (marker bit = false)
    SrsUniquePtr<SrsRtpPacket> idr_pkt1(mock_create_idr_packet(101, 180000, false));
    HELPER_EXPECT_SUCCESS(builder.packet_video(idr_pkt1.get()));

    // Should not generate frame yet (waiting for complete frame)
    EXPECT_EQ(0, target.on_frame_count_);

    // Second packet of IDR frame (marker bit = true, completes frame)
    SrsUniquePtr<SrsRtpPacket> idr_pkt2(mock_create_idr_packet(102, 180000, true));
    HELPER_EXPECT_SUCCESS(builder.packet_video(idr_pkt2.get()));

    // First packet of I frame.
    EXPECT_EQ(1, target.on_frame_count_);

    // 2. Send P frame as single RTP packet (marker bit = true)
    SrsUniquePtr<SrsRtpPacket> p_pkt(mock_create_p_frame_packet(103, 270000, false));
    HELPER_EXPECT_SUCCESS(builder.packet_video(p_pkt.get()));

    // Should not generate frame yet (waiting for complete frame)
    EXPECT_EQ(1, target.on_frame_count_);

    // Second packet of P frame.
    SrsUniquePtr<SrsRtpPacket> p_pkt2(mock_create_p_frame_packet(104, 270000, true));
    HELPER_EXPECT_SUCCESS(builder.packet_video(p_pkt2.get()));

    // Should generate P frame.
    EXPECT_EQ(2, target.on_frame_count_);
    EXPECT_TRUE(target.last_frame_ != NULL);
    if (target.last_frame_) {
        EXPECT_EQ(SrsFrameTypeVideo, target.last_frame_->message_type_);
    }

    // Verify no errors occurred during processing
    EXPECT_TRUE(target.frame_error_ == srs_success);
}

// Test SrsRtcFrameBuilder::packet_video with the packet-reordering video sequence:
// 1. STAP-A packet with SPS/PPS
// 2. IDR frame (2 RTP packets)
// 3. P frame (2 RTP packets)
// No packet loss scenario
//
// For the 2 RTP packets of IDR frame, they are in reordering, so the second packet
// comes before the first packet.
VOID TEST(Rtc2RtmpConvertTest, PacketVideo_ReorderingIdrPackets)
{
    srs_error_t err;

    MockRtc2RtmpFrameTarget target;
    SrsRtcFrameBuilder builder(_srs_app_factory, &target);

    // Initialize the builder with AVC codec
    SrsUniquePtr<MockRtc2RtmpRequest> req(new MockRtc2RtmpRequest());
    HELPER_EXPECT_SUCCESS(builder.initialize(req.get(), SrsAudioCodecIdAAC, SrsVideoCodecIdAVC));

    // Reset frame count before test
    target.reset();

    // 1. Send STAP-A packet containing SPS/PPS (sequence header)
    SrsUniquePtr<SrsRtpPacket> stap_pkt(mock_create_stap_packet_with_sps_pps(100, 90000));
    HELPER_EXPECT_SUCCESS(builder.packet_video(stap_pkt.get()));

    // Should generate one frame for sequence header
    EXPECT_EQ(1, target.on_frame_count_);
    EXPECT_TRUE(target.last_frame_ != NULL);
    if (target.last_frame_) {
        EXPECT_EQ(SrsFrameTypeVideo, target.last_frame_->message_type_);
    }

    // 2. Send IDR frame as 2 RTP packets
    // First packet of IDR frame (marker bit = true, completes frame)
    SrsUniquePtr<SrsRtpPacket> idr_pkt1(mock_create_idr_packet(102, 180000, true));
    HELPER_EXPECT_SUCCESS(builder.packet_video(idr_pkt1.get()));

    // Should not generate frame yet (waiting for complete frame)
    EXPECT_EQ(1, target.on_frame_count_);

    // Second packet of IDR frame (marker bit = false)
    SrsUniquePtr<SrsRtpPacket> idr_pkt2(mock_create_idr_packet(101, 180000, false));
    HELPER_EXPECT_SUCCESS(builder.packet_video(idr_pkt2.get()));

    // Should not generate frame yet (waiting for complete frame)
    EXPECT_EQ(1, target.on_frame_count_);

    // 3. Send P frame as single RTP packet (marker bit = true)
    SrsUniquePtr<SrsRtpPacket> p_pkt(mock_create_p_frame_packet(103, 270000, false));
    HELPER_EXPECT_SUCCESS(builder.packet_video(p_pkt.get()));

    // Should have triggered the previous IDR frame.
    EXPECT_EQ(2, target.on_frame_count_);

    // Second packet of P frame
    SrsUniquePtr<SrsRtpPacket> p_pkt2(mock_create_p_frame_packet(104, 270000, true));
    HELPER_EXPECT_SUCCESS(builder.packet_video(p_pkt2.get()));

    // Should generate P frame.
    EXPECT_EQ(3, target.on_frame_count_);
    EXPECT_TRUE(target.last_frame_ != NULL);
    if (target.last_frame_) {
        EXPECT_EQ(SrsFrameTypeVideo, target.last_frame_->message_type_);
    }

    // Verify no errors occurred during processing
    EXPECT_TRUE(target.frame_error_ == srs_success);
}

// Test SrsRtcFrameBuilder::packet_video with the packet-reordering video sequence:
// 1. STAP-A packet with SPS/PPS
// 2. P frame (2 RTP packets)
// 3. IDR frame (2 RTP packets)
// No packet loss scenario
//
// This case makes the P frame before IDR frame. It should work in normal state.
VOID TEST(Rtc2RtmpConvertTest, PacketVideo_PframeBeforeIdrPackets)
{
    srs_error_t err;

    MockRtc2RtmpFrameTarget target;
    SrsRtcFrameBuilder builder(_srs_app_factory, &target);

    // Initialize the builder with AVC codec
    SrsUniquePtr<MockRtc2RtmpRequest> req(new MockRtc2RtmpRequest());
    HELPER_EXPECT_SUCCESS(builder.initialize(req.get(), SrsAudioCodecIdAAC, SrsVideoCodecIdAVC));

    // Reset frame count before test
    target.reset();

    // 1. Send STAP-A packet containing SPS/PPS (sequence header)
    SrsUniquePtr<SrsRtpPacket> stap_pkt(mock_create_stap_packet_with_sps_pps(100, 90000));
    HELPER_EXPECT_SUCCESS(builder.packet_video(stap_pkt.get()));

    // Should generate one frame for sequence header
    EXPECT_EQ(1, target.on_frame_count_);
    EXPECT_TRUE(target.last_frame_ != NULL);
    if (target.last_frame_) {
        EXPECT_EQ(SrsFrameTypeVideo, target.last_frame_->message_type_);
    }

    // 2. Send P frame as single RTP packet (marker bit = true)
    SrsUniquePtr<SrsRtpPacket> p_pkt(mock_create_p_frame_packet(101, 180000, false));
    HELPER_EXPECT_SUCCESS(builder.packet_video(p_pkt.get()));

    // Should not generate frame yet (waiting for complete frame)
    EXPECT_EQ(1, target.on_frame_count_);

    // Second packet of P frame.
    SrsUniquePtr<SrsRtpPacket> p_pkt2(mock_create_p_frame_packet(102, 180000, true));
    HELPER_EXPECT_SUCCESS(builder.packet_video(p_pkt2.get()));

    // Should generate P frame.
    EXPECT_EQ(2, target.on_frame_count_);
    EXPECT_TRUE(target.last_frame_ != NULL);
    if (target.last_frame_) {
        EXPECT_EQ(SrsFrameTypeVideo, target.last_frame_->message_type_);
    }

    // 3. Send IDR frame as 2 RTP packets
    // First packet of IDR frame (marker bit = false)
    SrsUniquePtr<SrsRtpPacket> idr_pkt1(mock_create_idr_packet(103, 270000, false));
    HELPER_EXPECT_SUCCESS(builder.packet_video(idr_pkt1.get()));

    // Should not generate frame yet (waiting for complete frame)
    EXPECT_EQ(2, target.on_frame_count_);

    // Second packet of IDR frame (marker bit = true, completes frame)
    SrsUniquePtr<SrsRtpPacket> idr_pkt2(mock_create_idr_packet(104, 270000, true));
    HELPER_EXPECT_SUCCESS(builder.packet_video(idr_pkt2.get()));

    // Should generate I frame.
    EXPECT_EQ(3, target.on_frame_count_);

    // Verify no errors occurred during processing
    EXPECT_TRUE(target.frame_error_ == srs_success);
}

// Test SrsRtcFrameBuilder::packet_video with the packet-reordering video sequence:
// 1. STAP-A packet with SPS/PPS
// 2. P frame (2 RTP packets), the second packet arrive after IDR frame.
// 3. IDR frame (2 RTP packets)
// No packet loss scenario
//
// If P frame is before IDR frame, and the P frame is reordering, we should get both
// IDR and P frame. But in fact, the IDR frame will clear the P frame.
//      RTP(100, SPS/PPS)               // got_frame=1
//      RTP(101, P frame, marker=0).    // got_frame=1
//      RTP(103, IDR frame, marker=0)   // reordering, got_frame=1
//      RTP(102, P frame, marker=1)     // reordering, got_frame=1, should be 2.
//      RTP(104, IDR frame, marker=1)   // got_frame=2, should be 3.
VOID TEST(Rtc2RtmpConvertTest, PacketVideo_PframeReorderingPackets)
{
    srs_error_t err;

    MockRtc2RtmpFrameTarget target;
    SrsRtcFrameBuilder builder(_srs_app_factory, &target);

    // Initialize the builder with AVC codec
    SrsUniquePtr<MockRtc2RtmpRequest> req(new MockRtc2RtmpRequest());
    HELPER_EXPECT_SUCCESS(builder.initialize(req.get(), SrsAudioCodecIdAAC, SrsVideoCodecIdAVC));

    // Reset frame count before test
    target.reset();

    // 1. Send STAP-A packet containing SPS/PPS (sequence header)
    SrsUniquePtr<SrsRtpPacket> stap_pkt(mock_create_stap_packet_with_sps_pps(100, 90000));
    HELPER_EXPECT_SUCCESS(builder.packet_video(stap_pkt.get()));

    // Should generate one frame for sequence header
    EXPECT_EQ(1, target.on_frame_count_);
    EXPECT_TRUE(target.last_frame_ != NULL);
    if (target.last_frame_) {
        EXPECT_EQ(SrsFrameTypeVideo, target.last_frame_->message_type_);
    }

    // 2. Send P frame as single RTP packet (marker bit = true)
    SrsUniquePtr<SrsRtpPacket> p_pkt(mock_create_p_frame_packet(101, 180000, false));
    HELPER_EXPECT_SUCCESS(builder.packet_video(p_pkt.get()));

    // Should not generate frame yet (waiting for complete frame)
    EXPECT_EQ(1, target.on_frame_count_);

    // 3. Send IDR frame as 2 RTP packets
    // First packet of IDR frame (marker bit = false)
    // This is the reordering packet, arrive before the previous P frame.
    SrsUniquePtr<SrsRtpPacket> idr_pkt1(mock_create_idr_packet(103, 270000, false));
    HELPER_EXPECT_SUCCESS(builder.packet_video(idr_pkt1.get()));

    // Should not generate frame yet (waiting for complete frame)
    EXPECT_EQ(1, target.on_frame_count_);

    // ---> Reordering P frame arrived, the second packet of P frame.
    SrsUniquePtr<SrsRtpPacket> p_pkt2(mock_create_p_frame_packet(102, 180000, true));
    HELPER_EXPECT_SUCCESS(builder.packet_video(p_pkt2.get()));

    // Should generate P frame, but right now it's cleared by IDR.
    EXPECT_EQ(1, target.on_frame_count_);

    // Second packet of IDR frame (marker bit = true, completes frame)
    SrsUniquePtr<SrsRtpPacket> idr_pkt2(mock_create_idr_packet(104, 270000, true));
    HELPER_EXPECT_SUCCESS(builder.packet_video(idr_pkt2.get()));

    // Should generate I frame.
    EXPECT_EQ(2, target.on_frame_count_);

    // Verify no errors occurred during processing
    EXPECT_TRUE(target.frame_error_ == srs_success);
}

// Test SrsRtcFrameBuilder::packet_video with the packet-reordering video sequence:
// 1. STAP-A packet with SPS/PPS
// 2. P frame (2 RTP packets), the second packet arrive after IDR frame.
// 3. IDR frame (2 RTP packets)
// 4. P frame (1 RTP packet), used to trigger the IDR frame.
// No packet loss scenario
//
// If P frame is before IDR frame, and the P frame is reordering, we should get both
// IDR and P frame. But in fact, the IDR frame will clear the P frame.
//      RTP(100, SPS/PPS)               // got_frame=1
//      RTP(101, P frame, marker=0).    // got_frame=1
//      RTP(104, IDR frame, marker=1)   // reordering, got_frame=1.
//      RTP(102, P frame, marker=1)     // reordering, got_frame=1, should be 2.
//      RTP(103, IDR frame, marker=0)   // reordering, got_frame=1, should be 2.
//      RTP(105, P frame, marker=1)     // trigger, got_frame=3, should be 4.
VOID TEST(Rtc2RtmpConvertTest, PacketVideo_IdrPframeReorderingPackets)
{
    srs_error_t err;

    MockRtc2RtmpFrameTarget target;
    SrsRtcFrameBuilder builder(_srs_app_factory, &target);

    // Initialize the builder with AVC codec
    SrsUniquePtr<MockRtc2RtmpRequest> req(new MockRtc2RtmpRequest());
    HELPER_EXPECT_SUCCESS(builder.initialize(req.get(), SrsAudioCodecIdAAC, SrsVideoCodecIdAVC));

    // Reset frame count before test
    target.reset();

    // 1. Send STAP-A packet containing SPS/PPS (sequence header)
    SrsUniquePtr<SrsRtpPacket> stap_pkt(mock_create_stap_packet_with_sps_pps(100, 90000));
    HELPER_EXPECT_SUCCESS(builder.packet_video(stap_pkt.get()));

    // Should generate one frame for sequence header
    EXPECT_EQ(1, target.on_frame_count_);
    EXPECT_TRUE(target.last_frame_ != NULL);
    if (target.last_frame_) {
        EXPECT_EQ(SrsFrameTypeVideo, target.last_frame_->message_type_);
    }

    // 2. Send P frame as single RTP packet (marker bit = true)
    SrsUniquePtr<SrsRtpPacket> p_pkt(mock_create_p_frame_packet(101, 180000, false));
    HELPER_EXPECT_SUCCESS(builder.packet_video(p_pkt.get()));

    // Should not generate frame yet (waiting for complete frame)
    EXPECT_EQ(1, target.on_frame_count_);

    // 3. Send IDR frame as 2 RTP packets
    // First packet of IDR frame (marker bit = true, completes frame)
    // This is the reordering packet, arrive before the previous P frame.
    SrsUniquePtr<SrsRtpPacket> idr_pkt1(mock_create_idr_packet(104, 270000, true));
    HELPER_EXPECT_SUCCESS(builder.packet_video(idr_pkt1.get()));

    // Should not generate frame yet (waiting for complete frame)
    EXPECT_EQ(1, target.on_frame_count_);

    // ---> Reordering P frame arrived, the second packet of P frame.
    SrsUniquePtr<SrsRtpPacket> p_pkt2(mock_create_p_frame_packet(102, 180000, true));
    HELPER_EXPECT_SUCCESS(builder.packet_video(p_pkt2.get()));

    // Should generate P frame, but right now it's cleared by IDR.
    EXPECT_EQ(1, target.on_frame_count_);

    // Second packet of IDR frame (marker bit = false)
    SrsUniquePtr<SrsRtpPacket> idr_pkt2(mock_create_idr_packet(103, 270000, false));
    HELPER_EXPECT_SUCCESS(builder.packet_video(idr_pkt2.get()));

    // Should generate I frame.
    EXPECT_EQ(1, target.on_frame_count_);

    // 4. Send P frame as single RTP packet
    SrsUniquePtr<SrsRtpPacket> p2_pkt(mock_create_p_frame_packet(105, 360000, true));
    HELPER_EXPECT_SUCCESS(builder.packet_video(p2_pkt.get()));

    // Should generate 2 frames for previous IDR and this P frame
    EXPECT_EQ(3, target.on_frame_count_);

    // Verify no errors occurred during processing
    EXPECT_TRUE(target.frame_error_ == srs_success);
}

// Test SrsRtcFrameBuilder::packet_video with the packet-reordering video sequence:
// 1. STAP-A packet with SPS/PPS
// 2. P frame (2 RTP packets), the second packet arrive after IDR frame.
// 3. IDR frame (2 RTP packets)
// No packet loss scenario
//
// If P frame is before IDR frame, and the P frame is lost, we should get both
// IDR and P frame (NACK). But in fact, the IDR frame will clear the P frame.
//      RTP(100, SPS/PPS)               // got_frame=1
//      RTP(101, P frame, marker=0).    // got_frame=1
//      RTP(102, P frame, marker=1)     // <---- LOST
//      RTP(103, IDR frame, marker=0)   // got_frame=1, should trigger NACK.
//      RTP(104, IDR frame, marker=1)   // got_frame=2, should be 3.
VOID TEST(Rtc2RtmpConvertTest, PacketVideo_PframeLostPackets)
{
    srs_error_t err;

    MockRtc2RtmpFrameTarget target;
    SrsRtcFrameBuilder builder(_srs_app_factory, &target);

    // Initialize the builder with AVC codec
    SrsUniquePtr<MockRtc2RtmpRequest> req(new MockRtc2RtmpRequest());
    HELPER_EXPECT_SUCCESS(builder.initialize(req.get(), SrsAudioCodecIdAAC, SrsVideoCodecIdAVC));

    // Reset frame count before test
    target.reset();

    // 1. Send STAP-A packet containing SPS/PPS (sequence header)
    SrsUniquePtr<SrsRtpPacket> stap_pkt(mock_create_stap_packet_with_sps_pps(100, 90000));
    HELPER_EXPECT_SUCCESS(builder.packet_video(stap_pkt.get()));

    // Should generate one frame for sequence header
    EXPECT_EQ(1, target.on_frame_count_);
    EXPECT_TRUE(target.last_frame_ != NULL);
    if (target.last_frame_) {
        EXPECT_EQ(SrsFrameTypeVideo, target.last_frame_->message_type_);
    }

    // 2. Send P frame as single RTP packet (marker bit = true)
    SrsUniquePtr<SrsRtpPacket> p_pkt(mock_create_p_frame_packet(101, 180000, false));
    HELPER_EXPECT_SUCCESS(builder.packet_video(p_pkt.get()));

    // Should not generate frame yet (waiting for complete frame)
    EXPECT_EQ(1, target.on_frame_count_);

    // 3. Send IDR frame as 2 RTP packets
    // First packet of IDR frame (marker bit = false)
    // This is the reordering packet, arrive before the previous P frame.
    SrsUniquePtr<SrsRtpPacket> idr_pkt1(mock_create_idr_packet(103, 270000, false));
    HELPER_EXPECT_SUCCESS(builder.packet_video(idr_pkt1.get()));

    // Should not generate frame yet (waiting for complete frame)
    EXPECT_EQ(1, target.on_frame_count_);

    // Second packet of IDR frame (marker bit = true, completes frame)
    SrsUniquePtr<SrsRtpPacket> idr_pkt2(mock_create_idr_packet(104, 270000, true));
    HELPER_EXPECT_SUCCESS(builder.packet_video(idr_pkt2.get()));

    // Should generate I frame.
    EXPECT_EQ(2, target.on_frame_count_);

    // Verify no errors occurred during processing
    EXPECT_TRUE(target.frame_error_ == srs_success);
}

// Test SrsRtcFrameBuilder::packet_audio with 3 audio RTP packets:
// 1. First audio packet
// 2. Second audio packet
// 3. Third audio packet
// No packet loss scenario
//
// This tests the basic audio packet processing through the audio cache
// and transcoding pipeline. All packets should be processed successfully
// using a mock audio transcoder.
VOID TEST(Rtc2RtmpConvertTest, PacketAudio_ThreeAudioPackets)
{
    srs_error_t err;

    MockRtc2RtmpFrameTarget target;
    SrsRtcFrameBuilder builder(_srs_app_factory, &target);

    // Initialize the builder with Opus codec (typical for WebRTC)
    SrsUniquePtr<MockRtc2RtmpRequest> req(new MockRtc2RtmpRequest());
    HELPER_EXPECT_SUCCESS(builder.initialize(req.get(), SrsAudioCodecIdOpus, SrsVideoCodecIdAVC));

    // Replace the audio transcoder with our mock to avoid FFmpeg issues
    MockAudioTranscoderForUtest *mock_transcoder = new MockAudioTranscoderForUtest();
    mock_transcoder->set_output_packets(1); // Each input packet produces 1 output packet

    // Access private member through friendship (utests have access to private members)
    builder.audio_transcoder_ = mock_transcoder;

    // Reset frame count before test
    target.reset();

    // 1. Send first audio packet
    SrsUniquePtr<SrsRtpPacket> audio_pkt1(mock_create_audio_packet(100, 48000, 1000));
    HELPER_EXPECT_SUCCESS(builder.packet_audio(audio_pkt1.get()));
    // Should generate at least one frame (sequence header + data frame)
    EXPECT_EQ(target.on_frame_count_, 2);

    // 2. Send second audio packet
    SrsUniquePtr<SrsRtpPacket> audio_pkt2(mock_create_audio_packet(101, 48960, 1020));
    HELPER_EXPECT_SUCCESS(builder.packet_audio(audio_pkt2.get()));
    // Should generate at least one frame (data frame)
    EXPECT_EQ(target.on_frame_count_, 3);

    // 3. Send third audio packet
    SrsUniquePtr<SrsRtpPacket> audio_pkt3(mock_create_audio_packet(102, 49920, 1040));
    HELPER_EXPECT_SUCCESS(builder.packet_audio(audio_pkt3.get()));
    // Should generate at least one frame (data frame)
    EXPECT_EQ(target.on_frame_count_, 4);

    // Verify no errors occurred during processing
    EXPECT_TRUE(target.frame_error_ == srs_success);
}

// Test SrsRtcFrameBuilder::packet_audio with 3 audio RTP packets:
// 1. First audio packet
// 2. Third audio packet (reordering)
// 3. Second audio packet
// No packet loss scenario
//
// This tests the audio packet reordering through the audio cache.
VOID TEST(Rtc2RtmpConvertTest, PacketAudio_ReorderingAudioPackets)
{
    srs_error_t err;

    MockRtc2RtmpFrameTarget target;
    SrsRtcFrameBuilder builder(_srs_app_factory, &target);

    // Initialize the builder with Opus codec (typical for WebRTC)
    SrsUniquePtr<MockRtc2RtmpRequest> req(new MockRtc2RtmpRequest());
    HELPER_EXPECT_SUCCESS(builder.initialize(req.get(), SrsAudioCodecIdOpus, SrsVideoCodecIdAVC));

    // Replace the audio transcoder with our mock to avoid FFmpeg issues
    MockAudioTranscoderForUtest *mock_transcoder = new MockAudioTranscoderForUtest();
    mock_transcoder->set_output_packets(1); // Each input packet produces 1 output packet

    // Access private member through friendship (utests have access to private members)
    builder.audio_transcoder_ = mock_transcoder;

    // Reset frame count before test
    target.reset();

    // 1. Send first audio packet
    SrsUniquePtr<SrsRtpPacket> audio_pkt1(mock_create_audio_packet(100, 48000, 1000));
    HELPER_EXPECT_SUCCESS(builder.packet_audio(audio_pkt1.get()));
    // Should generate at least one frame (sequence header + data frame)
    EXPECT_EQ(target.on_frame_count_, 2);

    // 2. Send third audio packet
    SrsUniquePtr<SrsRtpPacket> audio_pkt3(mock_create_audio_packet(102, 49920, 1040));
    HELPER_EXPECT_SUCCESS(builder.packet_audio(audio_pkt3.get()));
    // Should generate at least one frame (data frame)
    EXPECT_EQ(target.on_frame_count_, 2);

    // 3. Send second audio packet
    SrsUniquePtr<SrsRtpPacket> audio_pkt2(mock_create_audio_packet(101, 48960, 1020));
    HELPER_EXPECT_SUCCESS(builder.packet_audio(audio_pkt2.get()));
    // Should generate at least one frame (data frame)
    EXPECT_EQ(target.on_frame_count_, 4);

    // Verify no errors occurred during processing
    EXPECT_TRUE(target.frame_error_ == srs_success);
}
