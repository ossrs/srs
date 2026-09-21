//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//
#include <srs_utest_ai27.hpp>

#include <srs_app_rtc_source.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_buffer.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_kbps.hpp>
#include <srs_kernel_rtc_rtp.hpp>
#include <srs_protocol_sdp.hpp>
#include <srs_utest_manual_mock.hpp>

#include <sstream>
#include <string.h>
using namespace std;

// The RFC 4588 RTX payloader is checked byte for byte: an original RTP packet on ssrc=200001 pt=102 and the RTX packet
// that wraps it with ssrc=200002 pt=103 seq=7. The uplink unwrap lives in the publish stream's decode hook and is
// covered by the workflow tests in srs_utest_workflow_rtc_conn.cpp.
static const uint32_t kMediaSsrc = 200001; // 0x00030D41
static const uint32_t kRtxSsrc = 200002;   // 0x00030D42
static const uint8_t kMediaPt = 102;       // 0x66
static const uint8_t kRtxPt = 103;         // 0x67
static const uint16_t kRtxSeq = 7;

static void expect_bytes_eq(const uint8_t *expected, const char *actual, int size)
{
    for (int i = 0; i < size; i++) {
        EXPECT_EQ((int)expected[i], (int)(uint8_t)actual[i]) << "byte " << i;
    }
}

// A payload the RTX payload owns, which the RTX payload must free. It references the media bytes without owning them,
// as every payloader in SRS references its packet's buffer.
static SrsRtpRawPayload *mock_raw_payload(uint8_t *bytes, int size)
{
    SrsRtpRawPayload *raw = new SrsRtpRawPayload();
    raw->payload_ = (char *)bytes;
    raw->nn_payload_ = size;
    return raw;
}

// Records its own destruction, so a test can tell whether the RTX payload freed the payload it holds.
class MockRtpPayloaderForRtx : public ISrsRtpPayloader
{
public:
    bool *destroyed_;

public:
    MockRtpPayloaderForRtx(bool *destroyed)
    {
        destroyed_ = destroyed;
    }
    virtual ~MockRtpPayloaderForRtx()
    {
        *destroyed_ = true;
    }

public:
    virtual uint64_t nb_bytes()
    {
        return 0;
    }
    virtual srs_error_t encode(SrsBuffer *buf)
    {
        return srs_success;
    }
    virtual srs_error_t decode(SrsBuffer *buf)
    {
        return srs_success;
    }
    virtual ISrsRtpPayloader *copy()
    {
        return new MockRtpPayloaderForRtx(destroyed_);
    }
};

VOID TEST(KernelRtcRtxTest, RtxPayloadEncodesOsnThenPayload)
{
    srs_error_t err = srs_success;

    uint8_t bytes[] = {0xAA, 0xBB, 0xCC};

    // The payloader owns the payload it wraps and prefixes the OSN.
    if (true) {
        SrsRtpRtxPayload rtx;
        rtx.osn_ = 1001;
        rtx.payload_ = mock_raw_payload(bytes, sizeof(bytes));
        EXPECT_EQ(2 + (int)sizeof(bytes), (int)rtx.nb_bytes());

        char out[5];
        memset(out, 0, sizeof(out));
        SrsBuffer buf(out, sizeof(out));
        HELPER_ASSERT_SUCCESS(rtx.encode(&buf));
        EXPECT_EQ((int)sizeof(out), buf.pos());

        uint8_t expected[] = {0x03, 0xE9, 0xAA, 0xBB, 0xCC};
        expect_bytes_eq(expected, out, sizeof(expected));

        // The media bytes are not touched.
        SrsRtpRawPayload *raw = dynamic_cast<SrsRtpRawPayload *>(rtx.payload_);
        ASSERT_TRUE(raw != NULL);
        EXPECT_EQ((char *)bytes, raw->payload_);
        EXPECT_EQ((int)sizeof(bytes), raw->nn_payload_);
    }

    // Without an original payload only the OSN is written.
    if (true) {
        SrsRtpRtxPayload rtx;
        rtx.osn_ = 0xFFFF;
        EXPECT_EQ(2, (int)rtx.nb_bytes());

        char out[2];
        memset(out, 0, sizeof(out));
        SrsBuffer buf(out, sizeof(out));
        HELPER_ASSERT_SUCCESS(rtx.encode(&buf));
        EXPECT_EQ(2, buf.pos());

        uint8_t expected[] = {0xFF, 0xFF};
        expect_bytes_eq(expected, out, sizeof(expected));
    }

    // Too small a buffer is rejected.
    if (true) {
        SrsRtpRtxPayload rtx;
        rtx.osn_ = 1001;
        rtx.payload_ = mock_raw_payload(bytes, sizeof(bytes));

        char out[4];
        SrsBuffer buf(out, sizeof(out));
        HELPER_EXPECT_FAILED(rtx.encode(&buf));
    }
}

// decode is the inverse of encode, RFC 4588 section 4: it reads the two-byte OSN and borrows the rest as a raw payload.
// A raw payload references the bytes without consuming them, so the buffer is left right after the OSN, where the media
// track's decode hook picks the payloader for the original payload.
VOID TEST(KernelRtcRtxTest, RtxPayloadDecodesOsnThenRawPayload)
{
    srs_error_t err = srs_success;

    if (true) {
        uint8_t bytes[] = {0x03, 0xE9, 0xAA, 0xBB, 0xCC};
        SrsBuffer buf((char *)bytes, sizeof(bytes));

        SrsRtpRtxPayload rtx;
        HELPER_ASSERT_SUCCESS(rtx.decode(&buf));
        EXPECT_EQ(1001, rtx.osn_);
        EXPECT_EQ(2, buf.pos());
        EXPECT_EQ(3, buf.left());

        SrsRtpRawPayload *raw = dynamic_cast<SrsRtpRawPayload *>(rtx.payload_);
        ASSERT_TRUE(raw != NULL);
        EXPECT_EQ((char *)bytes + 2, raw->payload_);
        EXPECT_EQ(3, raw->nn_payload_);
        EXPECT_EQ(5, (int)rtx.nb_bytes());
    }

    // An OSN with nothing behind it decodes to an empty payload, which the publish stream treats as a probe.
    if (true) {
        uint8_t bytes[] = {0xFF, 0xFE};
        SrsBuffer buf((char *)bytes, sizeof(bytes));

        SrsRtpRtxPayload rtx;
        HELPER_ASSERT_SUCCESS(rtx.decode(&buf));
        EXPECT_EQ(0xFFFE, rtx.osn_);
        EXPECT_EQ(2, buf.pos());
        ASSERT_TRUE(rtx.payload_ != NULL);
        EXPECT_EQ(0, (int)rtx.payload_->nb_bytes());
        EXPECT_EQ(2, (int)rtx.nb_bytes());
    }

    // Less than an OSN fails without consuming anything or creating a payload.
    for (int size = 0; size < 2; size++) {
        uint8_t bytes[] = {0x03};
        SrsBuffer buf((char *)bytes, size);

        SrsRtpRtxPayload rtx;
        HELPER_EXPECT_FAILED(rtx.decode(&buf));
        EXPECT_EQ(0, buf.pos()) << "size=" << size;
        EXPECT_TRUE(rtx.payload_ == NULL) << "size=" << size;
    }
}

// Encoding then decoding restores the OSN and the payload bytes, and the decoded payload encodes to the same bytes.
VOID TEST(KernelRtcRtxTest, RtxPayloadDecodeInvertsEncode)
{
    srs_error_t err = srs_success;

    uint8_t bytes[] = {0x65, 0x88, 0x84, 0x00};
    SrsRtpRtxPayload rtx;
    rtx.osn_ = 0xABCD;
    rtx.payload_ = mock_raw_payload(bytes, sizeof(bytes));

    char out[6];
    SrsBuffer buf(out, sizeof(out));
    HELPER_ASSERT_SUCCESS(rtx.encode(&buf));

    SrsBuffer in(out, sizeof(out));
    SrsRtpRtxPayload decoded;
    HELPER_ASSERT_SUCCESS(decoded.decode(&in));
    EXPECT_EQ(0xABCD, decoded.osn_);

    SrsRtpRawPayload *raw = dynamic_cast<SrsRtpRawPayload *>(decoded.payload_);
    ASSERT_TRUE(raw != NULL);
    ASSERT_EQ((int)sizeof(bytes), raw->nn_payload_);
    EXPECT_EQ(0, memcmp(bytes, raw->payload_, sizeof(bytes)));

    char again[6];
    SrsBuffer buf2(again, sizeof(again));
    HELPER_ASSERT_SUCCESS(decoded.encode(&buf2));
    EXPECT_EQ(0, memcmp(out, again, sizeof(out)));
}

// The RTX payload owns the payload it holds and frees it with itself.
VOID TEST(KernelRtcRtxTest, RtxPayloadFreesItsPayload)
{
    bool destroyed = false;
    MockRtpPayloaderForRtx *payload = new MockRtpPayloaderForRtx(&destroyed);

    SrsRtpRtxPayload *rtx = new SrsRtpRtxPayload();
    rtx->payload_ = payload;
    srs_freep(rtx);
    EXPECT_TRUE(destroyed);

    // Free it here only when the RTX payload did not, so a failing run does not leak it.
    if (!destroyed) {
        srs_freep(payload);
    }
}

// A copy holds a copy of the payload, so it stays usable after the original is freed. The media bytes are shared.
VOID TEST(KernelRtcRtxTest, RtxPayloadCopyOwnsItsOwnPayload)
{
    srs_error_t err = srs_success;

    uint8_t bytes[] = {0xAA, 0xBB, 0xCC};
    SrsRtpRtxPayload *rtx = new SrsRtpRtxPayload();
    rtx->osn_ = 1001;
    rtx->payload_ = mock_raw_payload(bytes, sizeof(bytes));

    SrsRtpRtxPayload *cp = dynamic_cast<SrsRtpRtxPayload *>(rtx->copy());
    ASSERT_TRUE(cp != NULL);
    EXPECT_EQ(1001, cp->osn_);
    ASSERT_TRUE(cp->payload_ != NULL);
    EXPECT_NE(rtx->payload_, cp->payload_);
    srs_freep(rtx);

    SrsRtpRawPayload *raw = dynamic_cast<SrsRtpRawPayload *>(cp->payload_);
    ASSERT_TRUE(raw != NULL);
    EXPECT_EQ((char *)bytes, raw->payload_);
    EXPECT_EQ((int)sizeof(bytes), raw->nn_payload_);

    char out[5];
    SrsBuffer buf(out, sizeof(out));
    HELPER_ASSERT_SUCCESS(cp->encode(&buf));
    uint8_t expected[] = {0x03, 0xE9, 0xAA, 0xBB, 0xCC};
    expect_bytes_eq(expected, out, sizeof(expected));
    srs_freep(cp);
}

VOID TEST(KernelRtcRtxTest, RtxPayloadEncodesThroughRtpPacket)
{
    srs_error_t err = srs_success;

    // A header copy with the RTX fields, attached to the payloader that owns a copy of the cached payload, encodes as
    // RFC 4588 section 4: the RTX header, the two-byte OSN, then the original payload.
    uint8_t bytes[] = {0xAA, 0xBB, 0xCC};
    SrsRtpRtxPayload *rtx = new SrsRtpRtxPayload();
    rtx->osn_ = 1001;
    rtx->payload_ = mock_raw_payload(bytes, sizeof(bytes));

    SrsRtpPacket pkt;
    pkt.header_.set_payload_type(kRtxPt);
    pkt.header_.set_sequence(kRtxSeq);
    pkt.header_.set_timestamp(90000);
    pkt.header_.set_ssrc(kRtxSsrc);
    pkt.set_payload(rtx, SrsRtpPacketPayloadTypeUnknown);

    uint8_t expected[] = {0x80, 0x67, 0x00, 0x07, 0x00, 0x01, 0x5F, 0x90, 0x00, 0x03, 0x0D, 0x42, 0x03, 0xE9, 0xAA, 0xBB, 0xCC};
    ASSERT_EQ((int)sizeof(expected), (int)pkt.nb_bytes());

    char out[sizeof(expected)];
    memset(out, 0, sizeof(out));
    SrsBuffer buf(out, sizeof(out));
    HELPER_ASSERT_SUCCESS(pkt.encode(&buf));
    EXPECT_EQ((int)sizeof(expected), buf.pos());
    expect_bytes_eq(expected, out, sizeof(expected));
}

VOID TEST(KernelRtcRtxTest, RtxPayloadDesCarriesClockRate)
{
    // Constructed for a 90000 Hz video codec, the descriptor must generate rtx/90000 with apt=<media pt>, never the
    // hardcoded 8000 that would make browsers ignore the RTX line.
    SrsRtxPayloadDes des(kRtxPt, kMediaPt, 90000);

    SrsMediaPayloadType t = des.generate_media_payload_type();
    EXPECT_EQ((int)kRtxPt, t.payload_type_);
    EXPECT_STREQ("rtx", t.encoding_name_.c_str());
    EXPECT_EQ(90000, t.clock_rate_);
    // The SDP encoder writes "a=fmtp:<pt> " itself, so the value is only the parameter, as for every other codec.
    EXPECT_STREQ("apt=102", t.format_specific_param_.c_str());

    SrsUniquePtr<SrsRtxPayloadDes> cp(des.copy());
    EXPECT_EQ(90000, cp->sample_);
    EXPECT_EQ((int)kMediaPt, (int)cp->apt_);
}

VOID TEST(KernelRtcRtxTest, TrackDescriptionCreatesRtxFromOffer)
{
    // The offer carries a=rtpmap:103 rtx/90000 and a=fmtp:103 apt=102. The parser keeps the fmtp word in
    // format_specific_param_, so the descriptor must take apt from there and the clock rate from the rtpmap.
    SrsMediaPayloadType rtx(kRtxPt);
    rtx.encoding_name_ = "rtx";
    rtx.clock_rate_ = 90000;
    rtx.format_specific_param_ = "apt=102";

    std::vector<SrsMediaPayloadType> payloads;
    payloads.push_back(rtx);

    SrsRtcTrackDescription track;
    track.create_auxiliary_payload(payloads);

    SrsRtxPayloadDes *des = dynamic_cast<SrsRtxPayloadDes *>(track.rtx_);
    ASSERT_TRUE(des != NULL);
    EXPECT_EQ((int)kRtxPt, (int)des->pt_);
    EXPECT_EQ((int)kMediaPt, (int)des->apt_);
    EXPECT_EQ(90000, des->sample_);
}

VOID TEST(KernelRtcRtxTest, TrackDescriptionCopyKeepsRtx)
{
    // copy() already deep-copies the rtx payload and the RTX SSRC, so this passes from the start; it locks in what the
    // play negotiation relies on when it copies the source track for each player.
    SrsRtcTrackDescription track;
    track.type_ = "video";
    track.rtx_ = new SrsRtxPayloadDes(kRtxPt, kMediaPt, 90000);
    track.rtx_ssrc_ = 42;

    SrsUniquePtr<SrsRtcTrackDescription> cp(track.copy());
    ASSERT_TRUE(cp->rtx_ != NULL);
    EXPECT_NE(track.rtx_, cp->rtx_);
    EXPECT_EQ(42u, cp->rtx_ssrc_);

    SrsRtxPayloadDes *rtx = dynamic_cast<SrsRtxPayloadDes *>(cp->rtx_);
    ASSERT_TRUE(rtx != NULL);
    EXPECT_EQ((int)kRtxPt, (int)rtx->pt_);
    EXPECT_EQ((int)kMediaPt, (int)rtx->apt_);
    EXPECT_EQ(90000, rtx->sample_);
}

VOID TEST(KernelRtcRtxTest, MediaDescEncodesFidGroup)
{
    srs_error_t err = srs_success;

    // A video media description with a FID group and its two SSRCs encodes the group line before the ssrc lines, as
    // browsers write it, RFC 5576 section 4.2.
    SrsMediaDesc desc("video");
    desc.port_ = 9;
    desc.protos_ = "UDP/TLS/RTP/SAVPF";
    desc.mid_ = "1";
    desc.sendonly_ = true;
    desc.payload_types_.push_back(SrsMediaPayloadType(102));
    desc.payload_types_.back().encoding_name_ = "H264";
    desc.payload_types_.back().clock_rate_ = 90000;

    std::vector<uint32_t> ssrcs;
    ssrcs.push_back(kMediaSsrc);
    ssrcs.push_back(kRtxSsrc);
    desc.ssrc_groups_.push_back(SrsSSRCGroup("FID", ssrcs));
    desc.ssrc_infos_.push_back(SrsSSRCInfo(kMediaSsrc, "stream", "live/livestream", "video"));
    desc.ssrc_infos_.push_back(SrsSSRCInfo(kRtxSsrc, "stream", "live/livestream", "video"));

    std::ostringstream os;
    HELPER_ASSERT_SUCCESS(desc.encode(os));
    std::string sdp = os.str();

    size_t group = sdp.find("a=ssrc-group:FID 200001 200002\r\n");
    size_t media = sdp.find("a=ssrc:200001 cname:stream\r\n");
    size_t rtx = sdp.find("a=ssrc:200002 cname:stream\r\n");
    EXPECT_TRUE(group != std::string::npos) << sdp;
    EXPECT_TRUE(media != std::string::npos) << sdp;
    EXPECT_TRUE(rtx != std::string::npos) << sdp;
    EXPECT_TRUE(group < media && media < rtx) << sdp;
}

// A video send track on the mock sender, active, with RTX negotiated when rtx_pt is not 0. The track copies the
// description, so the local one is freed with its payloads when this returns.
static SrsRtcVideoSendTrack *mock_video_send_track(MockRtcPacketSender *sender, uint8_t rtx_pt, uint32_t rtx_ssrc)
{
    SrsRtcTrackDescription desc;
    desc.type_ = "video";
    desc.id_ = "video-track";
    desc.ssrc_ = kMediaSsrc;
    desc.is_active_ = true;
    desc.direction_ = "sendonly";
    desc.media_ = new SrsVideoPayload(kMediaPt, "H264", 90000);
    if (rtx_pt) {
        desc.rtx_ = new SrsRtxPayloadDes(rtx_pt, kMediaPt, 90000);
        desc.rtx_ssrc_ = rtx_ssrc;
    }
    return new SrsRtcVideoSendTrack(sender, &desc);
}

// A marked video packet on the media SSRC with the payload, as the send track caches it after sending it.
static SrsRtpPacket *mock_cached_video_packet(uint16_t seq, uint8_t *payload, int nn_payload)
{
    SrsRtpPacket *pkt = new SrsRtpPacket();
    pkt->frame_type_ = SrsFrameTypeVideo;
    pkt->header_.set_ssrc(kMediaSsrc);
    pkt->header_.set_payload_type(kMediaPt);
    pkt->header_.set_sequence(seq);
    pkt->header_.set_timestamp(90000);
    pkt->header_.set_marker(true);

    SrsRtpRawPayload *raw = new SrsRtpRawPayload();
    raw->payload_ = (char *)payload;
    raw->nn_payload_ = nn_payload;
    pkt->set_payload(raw, SrsRtpPacketPayloadTypeRaw);

    return pkt;
}

static std::string encode_rtp_packet(SrsRtpPacket *pkt)
{
    char buf[kRtpPacketSize];
    SrsBuffer stream(buf, sizeof(buf));
    srs_error_t err = pkt->encode(&stream);
    srs_freep(err);
    return std::string(buf, stream.pos());
}

// With RTX negotiated, on_recv_nack answers from the cache with an RTX packet: RTX SSRC and payload type, an RTX
// sequence that increments per packet sent, the original timestamp and marker, and the OSN before the original
// payload. The cached packet is byte-identical afterwards in both cache modes.
VOID TEST(KernelRtcRtxTest, SendTrackAnswersNackWithRtx)
{
    srs_error_t err = srs_success;
    uint8_t payload[] = {0x65, 0x88, 0x84, 0x00};

    for (int mode = 0; mode < 2; mode++) {
        bool nack_no_copy = (mode == 1);

        MockRtcPacketSender sender;
        SrsUniquePtr<SrsRtcVideoSendTrack> track(mock_video_send_track(&sender, kRtxPt, kRtxSsrc));
        track->set_nack_no_copy(nack_no_copy);

        // Cache the packet as on_rtp does after sending: copied, or handed over in no-copy mode.
        SrsRtpPacket *pkt = mock_cached_video_packet(1000, payload, sizeof(payload));
        std::string original = encode_rtp_packet(pkt);
        HELPER_ASSERT_SUCCESS(track->on_nack(&pkt));
        srs_freep(pkt);

        std::vector<uint16_t> seqs;
        seqs.push_back(1000);
        HELPER_ASSERT_SUCCESS(track->on_recv_nack(seqs));
        HELPER_ASSERT_SUCCESS(track->on_recv_nack(seqs));
        ASSERT_EQ(2, (int)sender.sent_packets_.size()) << "no_copy=" << nack_no_copy;

        const std::string &rtx = sender.sent_packets_[0];
        const std::string &rtx2 = sender.sent_packets_[1];
        EXPECT_EQ(kRtxSsrc, srs_rtp_fast_parse_ssrc((char *)rtx.data(), (int)rtx.size())) << "no_copy=" << nack_no_copy;
        EXPECT_EQ((int)kRtxPt, (int)srs_rtp_fast_parse_pt((char *)rtx.data(), (int)rtx.size()));
        ASSERT_EQ(original.size() + 2, rtx.size());
        EXPECT_EQ(0, memcmp(original.data() + 4, rtx.data() + 4, 4));
        EXPECT_EQ((uint8_t)original[1] & 0x80, (uint8_t)rtx[1] & 0x80);
        EXPECT_EQ(0x03, (int)(uint8_t)rtx[12]);
        EXPECT_EQ(0xE8, (int)(uint8_t)rtx[13]);
        EXPECT_EQ(0, memcmp(original.data() + 12, rtx.data() + 14, original.size() - 12));

        uint16_t seq1 = srs_rtp_fast_parse_seq((char *)rtx.data(), (int)rtx.size());
        uint16_t seq2 = srs_rtp_fast_parse_seq((char *)rtx2.data(), (int)rtx2.size());
        EXPECT_EQ((uint16_t)(seq1 + 1), seq2);
        EXPECT_EQ(0, memcmp(rtx.data() + 4, rtx2.data() + 4, rtx.size() - 4));

        SrsRtpPacket *cached = track->rtp_queue_->at(1000);
        ASSERT_TRUE(cached != NULL);
        EXPECT_EQ(original, encode_rtp_packet(cached)) << "no_copy=" << nack_no_copy;
    }
}

// Without RTX on the track, or with an rtx payload but no RTX SSRC, the cached packet is resent unchanged. Both pass
// from the start and lock in plain retransmission as the accepted fallback.
VOID TEST(KernelRtcRtxTest, SendTrackAnswersNackPlainWithoutRtx)
{
    srs_error_t err = srs_success;
    uint8_t payload[] = {0x65, 0x88, 0x84, 0x00};

    for (int mode = 0; mode < 2; mode++) {
        MockRtcPacketSender sender;
        SrsUniquePtr<SrsRtcVideoSendTrack> track(mock_video_send_track(&sender, mode == 0 ? 0 : kRtxPt, 0));

        SrsRtpPacket *pkt = mock_cached_video_packet(1000, payload, sizeof(payload));
        std::string original = encode_rtp_packet(pkt);
        HELPER_ASSERT_SUCCESS(track->on_nack(&pkt));
        srs_freep(pkt);

        std::vector<uint16_t> seqs;
        seqs.push_back(1000);
        HELPER_ASSERT_SUCCESS(track->on_recv_nack(seqs));
        ASSERT_EQ(1, (int)sender.sent_packets_.size());
        EXPECT_EQ(original, sender.sent_packets_[0]) << "mode=" << mode;
    }
}
