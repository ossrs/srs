//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//
#include <srs_utest_ai24.hpp>

#include <srs_app_config.hpp>
#include <srs_app_fragment.hpp>
#include <srs_app_hls.hpp>
#include <srs_app_http_api.hpp>
#include <srs_app_http_hooks.hpp>
#include <srs_app_rtc_source.hpp>
#include <srs_app_server.hpp>
#include <srs_app_utility.hpp>
#include <srs_kernel_codec.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_mp4.hpp>
#include <srs_kernel_packet.hpp>
#include <srs_kernel_rtc_rtcp.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_sdp.hpp>
#include <srs_protocol_utility.hpp>
#include <srs_utest_ai16.hpp>
#include <srs_utest_manual_config.hpp>
#include <srs_utest_manual_fmp4.hpp>
#include <srs_utest_manual_kernel.hpp>
#include <srs_utest_manual_mock.hpp>

#ifdef SRS_FFMPEG_FIT
#include <srs_app_rtc_codec.hpp>

#ifdef __cplusplus
extern "C" {
#endif
#include <libavutil/log.h>
#ifdef __cplusplus
}
#endif
#endif

#include <fstream>
#include <math.h>
#include <new>
#include <sstream>

using namespace std;

// Mock class to access protected members of SrsRtcRecvTrack
class MockSrsRtcRecvTrackForAVSync : public SrsRtcRecvTrack
{
    SRS_DECLARE_PRIVATE:
    static SrsRtcTrackDescription* create_track_desc(const string& type, uint32_t ssrc, int sample_rate)
    {
        SrsRtcTrackDescription *desc = new SrsRtcTrackDescription();
        desc->type_ = type;
        desc->id_ = "test_track";
        desc->ssrc_ = ssrc;
        desc->is_active_ = true;

        // Create media description with sample rate
        desc->media_ = new SrsAudioPayload();
        desc->media_->sample_ = sample_rate;

        return desc;
    }

public:
    MockSrsRtcRecvTrackForAVSync(const string &type, uint32_t ssrc, int sample_rate, bool is_audio)
        : SrsRtcRecvTrack(NULL, create_track_desc(type, ssrc, sample_rate), is_audio, true)
    {
    }

    // Expose protected methods for testing
    double get_rate() const { return rate_; }

    void set_rate(double rate) { rate_ = rate; }

    int64_t test_cal_avsync_time(uint32_t rtp_time)
    {
        return cal_avsync_time(rtp_time);
    }

    void test_update_send_report_time(const SrsNtp &ntp, uint32_t rtp_time)
    {
        update_send_report_time(ntp, rtp_time);
    }

    // Implement pure virtual methods
    virtual srs_error_t on_rtp(SrsSharedPtr<SrsRtcSource> &source, SrsRtpPacket *pkt)
    {
        return srs_success;
    }

    virtual srs_error_t check_send_nacks()
    {
        return srs_success;
    }
};

// Test: Rate initialization from SDP for audio track (48kHz)
VOID TEST(RtcAVSyncTest, AudioRateInitFromSDP)
{
    MockSrsRtcRecvTrackForAVSync track("audio", 12345, 48000, true);

    // Rate should be initialized to 48 (48000 Hz / 1000 = 48 RTP units per ms)
    EXPECT_DOUBLE_EQ(48.0, track.get_rate());
}

// Test: Rate initialization from SDP for video track (90kHz)
VOID TEST(RtcAVSyncTest, VideoRateInitFromSDP)
{
    MockSrsRtcRecvTrackForAVSync track("video", 67890, 90000, false);

    // Rate should be initialized to 90 (90000 Hz / 1000 = 90 RTP units per ms)
    EXPECT_DOUBLE_EQ(90.0, track.get_rate());
}

// Test: cal_avsync_time with SDP rate (before receiving SR)
VOID TEST(RtcAVSyncTest, CalAVSyncTimeWithSDPRate)
{
    MockSrsRtcRecvTrackForAVSync track("audio", 12345, 48000, true);

    // Simulate first SR received
    SrsNtp ntp;
    ntp.system_ms_ = 1000;     // 1000 ms
    uint32_t rtp_time = 48000; // 48000 RTP units
    track.test_update_send_report_time(ntp, rtp_time);

    // Calculate avsync time for a later RTP packet
    // RTP time: 48000 + 4800 = 52800 (100ms later at 48kHz)
    // Expected avsync_time: 1000 + (52800 - 48000) / 48 = 1000 + 100 = 1100 ms
    int64_t avsync_time = track.test_cal_avsync_time(52800);
    EXPECT_EQ(1100, avsync_time);
}

// Test: cal_avsync_time returns -1 when rate is 0
VOID TEST(RtcAVSyncTest, CalAVSyncTimeWithZeroRate)
{
    MockSrsRtcRecvTrackForAVSync track("audio", 12345, 48000, true);

    // Manually set rate to 0
    track.set_rate(0.0);

    // Should return -1 when rate is too small
    int64_t avsync_time = track.test_cal_avsync_time(1000);
    EXPECT_EQ(-1, avsync_time);
}

// Test: Rate update after receiving 2nd SR (audio)
VOID TEST(RtcAVSyncTest, AudioRateUpdateAfter2ndSR)
{
    MockSrsRtcRecvTrackForAVSync track("audio", 12345, 48000, true);

    // Initial rate from SDP
    EXPECT_DOUBLE_EQ(48.0, track.get_rate());

    // First SR
    SrsNtp ntp1;
    ntp1.system_ms_ = 1000;
    uint32_t rtp_time1 = 48000;
    track.test_update_send_report_time(ntp1, rtp_time1);

    // Rate should still be 48 (from SDP)
    EXPECT_DOUBLE_EQ(48.0, track.get_rate());

    // Second SR (20ms later, RTP increased by 960)
    SrsNtp ntp2;
    ntp2.system_ms_ = 1020;     // 20ms later
    uint32_t rtp_time2 = 48960; // 960 RTP units later (48 * 20)
    track.test_update_send_report_time(ntp2, rtp_time2);

    // Rate should be updated to calculated value: 960 / 20 = 48
    EXPECT_DOUBLE_EQ(48.0, track.get_rate());
}

// Test: Rate update after receiving 2nd SR (video)
VOID TEST(RtcAVSyncTest, VideoRateUpdateAfter2ndSR)
{
    MockSrsRtcRecvTrackForAVSync track("video", 67890, 90000, false);

    // Initial rate from SDP
    EXPECT_DOUBLE_EQ(90.0, track.get_rate());

    // First SR
    SrsNtp ntp1;
    ntp1.system_ms_ = 2000;
    uint32_t rtp_time1 = 180000;
    track.test_update_send_report_time(ntp1, rtp_time1);

    // Rate should still be 90 (from SDP)
    EXPECT_DOUBLE_EQ(90.0, track.get_rate());

    // Second SR (100ms later, RTP increased by 9000)
    SrsNtp ntp2;
    ntp2.system_ms_ = 2100;      // 100ms later
    uint32_t rtp_time2 = 189000; // 9000 RTP units later (90 * 100)
    track.test_update_send_report_time(ntp2, rtp_time2);

    // Rate should be updated to calculated value: 9000 / 100 = 90
    EXPECT_DOUBLE_EQ(90.0, track.get_rate());
}

// Test: Rate calculation with clock drift (slightly off from SDP)
VOID TEST(RtcAVSyncTest, RateUpdateWithClockDrift)
{
    MockSrsRtcRecvTrackForAVSync track("video", 67890, 90000, false);

    // Initial rate from SDP
    EXPECT_DOUBLE_EQ(90.0, track.get_rate());

    // First SR
    SrsNtp ntp1;
    ntp1.system_ms_ = 1000;
    uint32_t rtp_time1 = 90000;
    track.test_update_send_report_time(ntp1, rtp_time1);

    // Second SR with slight clock drift
    // Expected: 100ms -> 9000 RTP units
    // Actual: 100ms -> 9010 RTP units (slight drift)
    SrsNtp ntp2;
    ntp2.system_ms_ = 1100;
    uint32_t rtp_time2 = 99010; // Slightly more than expected
    track.test_update_send_report_time(ntp2, rtp_time2);

    // Rate should be updated to: round(9010 / 100) = 90
    EXPECT_DOUBLE_EQ(90.0, track.get_rate());
}

// Test: Rate calculation with larger time interval
VOID TEST(RtcAVSyncTest, RateUpdateWithLargeInterval)
{
    MockSrsRtcRecvTrackForAVSync track("audio", 12345, 48000, true);

    // First SR
    SrsNtp ntp1;
    ntp1.system_ms_ = 5000;
    uint32_t rtp_time1 = 240000;
    track.test_update_send_report_time(ntp1, rtp_time1);

    // Second SR (1000ms later)
    SrsNtp ntp2;
    ntp2.system_ms_ = 6000;
    uint32_t rtp_time2 = 288000; // 48000 RTP units later (48 * 1000)
    track.test_update_send_report_time(ntp2, rtp_time2);

    // Rate should be: 48000 / 1000 = 48
    EXPECT_DOUBLE_EQ(48.0, track.get_rate());
}

// Test: cal_avsync_time with precise rate after 2nd SR
VOID TEST(RtcAVSyncTest, CalAVSyncTimeAfter2ndSR)
{
    MockSrsRtcRecvTrackForAVSync track("video", 67890, 90000, false);

    // First SR
    SrsNtp ntp1;
    ntp1.system_ms_ = 1000;
    uint32_t rtp_time1 = 90000;
    track.test_update_send_report_time(ntp1, rtp_time1);

    // Second SR
    SrsNtp ntp2;
    ntp2.system_ms_ = 1100;
    uint32_t rtp_time2 = 99000;
    track.test_update_send_report_time(ntp2, rtp_time2);

    // Now calculate avsync time for a packet
    // RTP time: 99000 + 4500 = 103500 (50ms later at 90kHz)
    // Expected: 1100 + (103500 - 99000) / 90 = 1100 + 50 = 1150 ms
    int64_t avsync_time = track.test_cal_avsync_time(103500);
    EXPECT_EQ(1150, avsync_time);
}

// Test: Immediate A/V sync availability (issue #4418 fix)
VOID TEST(RtcAVSyncTest, ImmediateAVSyncAvailability)
{
    MockSrsRtcRecvTrackForAVSync track("audio", 12345, 48000, true);

    // Before any SR, rate should be available from SDP
    EXPECT_DOUBLE_EQ(48.0, track.get_rate());

    // First SR received
    SrsNtp ntp1;
    ntp1.system_ms_ = 1000;
    uint32_t rtp_time1 = 48000;
    track.test_update_send_report_time(ntp1, rtp_time1);

    // Should be able to calculate avsync_time immediately (not -1)
    int64_t avsync_time = track.test_cal_avsync_time(48480); // 10ms later
    EXPECT_GT(avsync_time, 0);                               // Should be > 0, not -1
    EXPECT_EQ(1010, avsync_time);                            // Should be 1000 + 10 = 1010
}

// Test: with the SDP rate, a packet before its track's first SR has no A/V sync time, since its RTP time is on no
// clock yet and each track's starts at a random value; from the track's first SR on, the SDP rate maps it.
VOID TEST(RtcAVSyncTest, SDPRateWaitsForFirstSR)
{
    MockSrsRtcRecvTrackForAVSync audio("audio", 12345, 48000, true);
    MockSrsRtcRecvTrackForAVSync video("video", 67890, 90000, false);

    EXPECT_EQ(-1, audio.test_cal_avsync_time(52800));
    EXPECT_EQ(-1, video.test_cal_avsync_time(189000));

    SrsNtp ntp;
    ntp.system_ms_ = 1000;
    audio.test_update_send_report_time(ntp, 48000);
    EXPECT_EQ(1100, audio.test_cal_avsync_time(52800));
    EXPECT_EQ(-1, video.test_cal_avsync_time(189000));

    ntp.system_ms_ = 1050;
    video.test_update_send_report_time(ntp, 180000);
    EXPECT_EQ(1150, video.test_cal_avsync_time(189000));
}

// Test: a first SR at RTP time 0 counts as a report, so the 2nd SR gives the precise rate.
VOID TEST(RtcAVSyncTest, FirstSRAtRtpTimeZero)
{
    MockSrsRtcRecvTrackForAVSync track("audio", 12345, 48000, true);

    SrsNtp ntp1;
    ntp1.system_ms_ = 1000;
    track.test_update_send_report_time(ntp1, 0);
    EXPECT_EQ(1010, track.test_cal_avsync_time(480));

    // A sender whose clock runs at 49 units per ms.
    SrsNtp ntp2;
    ntp2.system_ms_ = 2000;
    track.test_update_send_report_time(ntp2, 49000);
    EXPECT_DOUBLE_EQ(49.0, track.get_rate());
}

// Test: RTP timestamp wraparound handling
VOID TEST(RtcAVSyncTest, RTPTimestampWraparound)
{
    MockSrsRtcRecvTrackForAVSync track("audio", 12345, 48000, true);

    // First SR near wraparound
    SrsNtp ntp1;
    ntp1.system_ms_ = 1000;
    uint32_t rtp_time1 = 0xFFFFF000; // Near max uint32_t
    track.test_update_send_report_time(ntp1, rtp_time1);

    // Second SR after wraparound
    SrsNtp ntp2;
    ntp2.system_ms_ = 1020;          // 20ms later
    uint32_t rtp_time2 = 0x000003C0; // Wrapped around, 960 units after wraparound
    track.test_update_send_report_time(ntp2, rtp_time2);

    // Note: Current implementation may not handle wraparound correctly
    // This test documents the current behavior
    // Rate calculation: (0x000003C0 - 0xFFFFF000) will underflow
    // This is a known limitation that may need fixing in the future
}

// Test: Zero time elapsed between SRs (edge case)
VOID TEST(RtcAVSyncTest, ZeroTimeElapsedBetweenSRs)
{
    MockSrsRtcRecvTrackForAVSync track("audio", 12345, 48000, true);

    // First SR
    SrsNtp ntp1;
    ntp1.system_ms_ = 1000;
    uint32_t rtp_time1 = 48000;
    track.test_update_send_report_time(ntp1, rtp_time1);

    double rate_before = track.get_rate();

    // Second SR with same timestamp (0ms elapsed)
    SrsNtp ntp2;
    ntp2.system_ms_ = 1000;     // Same time
    uint32_t rtp_time2 = 48000; // Same RTP time
    track.test_update_send_report_time(ntp2, rtp_time2);

    // Rate should remain unchanged (SDP rate)
    EXPECT_DOUBLE_EQ(rate_before, track.get_rate());
}

// Test: SrsParsedPacket::copy() method
VOID TEST(ParsedPacketTest, CopyParsedPacket)
{
    srs_error_t err;

    // Create a parsed packet with sample data
    SrsParsedPacket packet;
    SrsVideoCodecConfig codec;
    HELPER_EXPECT_SUCCESS(packet.initialize(&codec));

    // Set packet properties
    packet.dts_ = 1000;
    packet.cts_ = 100;

    // Add sample data
    char sample_data1[] = {0x01, 0x02, 0x03};
    char sample_data2[] = {0x04, 0x05, 0x06, 0x07};
    HELPER_EXPECT_SUCCESS(packet.add_sample(sample_data1, sizeof(sample_data1)));
    HELPER_EXPECT_SUCCESS(packet.add_sample(sample_data2, sizeof(sample_data2)));

    // Copy the packet
    SrsParsedPacket *copied = packet.copy();
    ASSERT_TRUE(copied != NULL);

    // Verify all fields are copied correctly
    EXPECT_EQ(packet.dts_, copied->dts_);
    EXPECT_EQ(packet.cts_, copied->cts_);
    EXPECT_EQ(packet.codec_, copied->codec_);
    EXPECT_EQ(packet.nb_samples_, copied->nb_samples_);

    // Verify samples are copied (shared pointers)
    for (int i = 0; i < packet.nb_samples_; i++) {
        EXPECT_EQ(packet.samples_[i].bytes_, copied->samples_[i].bytes_);
        EXPECT_EQ(packet.samples_[i].size_, copied->samples_[i].size_);
    }

    srs_freep(copied);
}

// Test: SrsParsedVideoPacket::copy() method
VOID TEST(ParsedPacketTest, CopyParsedVideoPacket)
{
    srs_error_t err;

    // Create a parsed video packet with sample data
    SrsParsedVideoPacket packet;
    SrsVideoCodecConfig codec;
    HELPER_EXPECT_SUCCESS(packet.initialize(&codec));

    // Set packet properties
    packet.dts_ = 2000;
    packet.cts_ = 200;
    packet.frame_type_ = SrsVideoAvcFrameTypeKeyFrame;
    packet.avc_packet_type_ = SrsVideoAvcFrameTraitNALU;
    packet.has_idr_ = true;
    packet.has_aud_ = false;
    packet.has_sps_pps_ = true;
    packet.first_nalu_type_ = SrsAvcNaluTypeIDR;

    // Add sample data
    uint8_t sample_data[] = {0x65, 0x88, 0x84, 0x00};
    HELPER_EXPECT_SUCCESS(packet.add_sample((char *)sample_data, sizeof(sample_data)));

    // Copy the packet
    SrsParsedVideoPacket *copied = packet.copy();
    ASSERT_TRUE(copied != NULL);

    // Verify base class fields are copied
    EXPECT_EQ(packet.dts_, copied->dts_);
    EXPECT_EQ(packet.cts_, copied->cts_);
    EXPECT_EQ(packet.codec_, copied->codec_);
    EXPECT_EQ(packet.nb_samples_, copied->nb_samples_);

    // Verify video-specific fields are copied
    EXPECT_EQ(packet.frame_type_, copied->frame_type_);
    EXPECT_EQ(packet.avc_packet_type_, copied->avc_packet_type_);
    EXPECT_EQ(packet.has_idr_, copied->has_idr_);
    EXPECT_EQ(packet.has_aud_, copied->has_aud_);
    EXPECT_EQ(packet.has_sps_pps_, copied->has_sps_pps_);
    EXPECT_EQ(packet.first_nalu_type_, copied->first_nalu_type_);

    // Verify samples are copied (shared pointers)
    for (int i = 0; i < packet.nb_samples_; i++) {
        EXPECT_EQ(packet.samples_[i].bytes_, copied->samples_[i].bytes_);
        EXPECT_EQ(packet.samples_[i].size_, copied->samples_[i].size_);
    }

    srs_freep(copied);
}

#ifdef SRS_FFMPEG_FIT
// Helper function to call ffmpeg_log_callback with formatted string
static void call_ffmpeg_log(int level, const char *fmt, ...)
{
    va_list vl;
    va_start(vl, fmt);
    SrsFFmpegLogHelper::ffmpeg_log_callback(NULL, level, fmt, vl);
    va_end(vl);
}

// Test: SrsFFmpegLogHelper::ffmpeg_log_callback() method
VOID TEST(FFmpegLogHelperTest, LogCallback)
{
    // Save original disabled state
    bool original_disabled = SrsFFmpegLogHelper::disabled_;

    // Test 1: Callback should work when not disabled
    SrsFFmpegLogHelper::disabled_ = false;

    // AV_LOG_WARNING level
    call_ffmpeg_log(AV_LOG_WARNING, "Test warning message\n");

    // AV_LOG_INFO level
    call_ffmpeg_log(AV_LOG_INFO, "Test info message\n");

    // AV_LOG_VERBOSE/DEBUG/TRACE levels
    call_ffmpeg_log(AV_LOG_VERBOSE, "Test verbose message\n");
    call_ffmpeg_log(AV_LOG_DEBUG, "Test debug message\n");
    call_ffmpeg_log(AV_LOG_TRACE, "Test trace message\n");

    // Test message without newline (should not strip last character)
    call_ffmpeg_log(AV_LOG_INFO, "Test message without newline");

    // Test message with newline (should strip newline)
    call_ffmpeg_log(AV_LOG_INFO, "Test message with newline\n");

    // Test 2: Callback should return early when disabled
    SrsFFmpegLogHelper::disabled_ = true;

    // These calls should return immediately without processing
    call_ffmpeg_log(AV_LOG_ERROR, "This should not be logged\n");
    call_ffmpeg_log(AV_LOG_WARNING, "This should not be logged\n");
    call_ffmpeg_log(AV_LOG_INFO, "This should not be logged\n");

    // Test 3: Test edge cases
    SrsFFmpegLogHelper::disabled_ = false;

    // Empty message
    call_ffmpeg_log(AV_LOG_INFO, "");

    // Very long message (should be truncated to buffer size)
    std::string long_msg(5000, 'x');
    call_ffmpeg_log(AV_LOG_INFO, "%s", long_msg.c_str());

    // Restore original disabled state
    SrsFFmpegLogHelper::disabled_ = original_disabled;

    // If we reach here without crashing, the test passes
    EXPECT_TRUE(true);
}

// Encode n frames of a 440Hz tone as AAC-LC, 44.1kHz stereo, each frame with
// its ADTS header, as SRS feeds the transcoder from RTMP.
void encode_aac_adts_frames(int n, std::vector<std::string> &frames)
{
    const AVCodec *codec = avcodec_find_encoder_by_name("aac");
    ASSERT_TRUE(codec != NULL);

    AVCodecContext *enc = avcodec_alloc_context3(codec);
    ASSERT_TRUE(enc != NULL);
    enc->sample_rate = 44100;
    enc->channels = 2;
    enc->channel_layout = av_get_default_channel_layout(2);
    enc->sample_fmt = AV_SAMPLE_FMT_FLTP;
    enc->bit_rate = 64000;
    enc->time_base.num = 1;
    enc->time_base.den = 44100;
    ASSERT_GE(avcodec_open2(enc, codec, NULL), 0);

    AVFrame *frame = av_frame_alloc();
    frame->format = enc->sample_fmt;
    frame->nb_samples = enc->frame_size;
    frame->channel_layout = enc->channel_layout;
    ASSERT_GE(av_frame_get_buffer(frame, 0), 0);
    AVPacket *pkt = av_packet_alloc();

    int64_t pts = 0;
    for (int i = 0; (int)frames.size() < n && i < n + 16; i++) {
        ASSERT_GE(av_frame_make_writable(frame), 0);
        for (int ch = 0; ch < 2; ch++) {
            float *samples = (float *)frame->data[ch];
            for (int s = 0; s < frame->nb_samples; s++) {
                samples[s] = 0.3f * sinf(2.0f * 3.14159265f * 440.0f * (float)(pts + s) / 44100.0f);
            }
        }
        frame->pts = pts;
        pts += frame->nb_samples;
        ASSERT_GE(avcodec_send_frame(enc, frame), 0);

        while (avcodec_receive_packet(enc, pkt) >= 0) {
            // ADTS header: AAC-LC (object type 2), 44.1kHz (index 4), 2 channels.
            int len = pkt->size + 7;
            uint8_t adts[7];
            adts[0] = 0xff;
            adts[1] = 0xf1;
            adts[2] = (uint8_t)(((2 - 1) << 6) | (4 << 2) | (2 >> 2));
            adts[3] = (uint8_t)(((2 & 3) << 6) | ((len >> 11) & 0x03));
            adts[4] = (uint8_t)((len >> 3) & 0xff);
            adts[5] = (uint8_t)(((len & 7) << 5) | 0x1f);
            adts[6] = 0xfc;
            frames.push_back(std::string((char *)adts, 7) + std::string((char *)pkt->data, pkt->size));
            av_packet_unref(pkt);
        }
    }

    av_packet_free(&pkt);
    av_frame_free(&frame);
    avcodec_free_context(&enc);
    ASSERT_EQ(n, (int)frames.size());
}

// Transcode AAC frames, 1024 samples at 44.1kHz each, whose first frame has
// the time start in ms, to Opus, and collect the time of every Opus frame.
static void transcode_aac_to_opus(int64_t start, int n, std::vector<int64_t> &opus_dts, int64_t &last_aac_dts)
{
    srs_error_t err;

    std::vector<std::string> frames;
    encode_aac_adts_frames(n, frames);
    ASSERT_EQ(n, (int)frames.size());

    SrsAudioTranscoder transcoder;
    HELPER_ASSERT_SUCCESS(transcoder.initialize(SrsAudioCodecIdAAC, SrsAudioCodecIdOpus, 2, 48000, 48000));

    for (int i = 0; i < n; i++) {
        SrsParsedAudioPacket aac;
        aac.dts_ = start + (int64_t)i * 1024 * 1000 / 44100;
        aac.cts_ = 0;
        last_aac_dts = aac.dts_;
        HELPER_ASSERT_SUCCESS(aac.add_sample((char *)frames[i].data(), (int)frames[i].size()));

        std::vector<SrsParsedAudioPacket *> outs;
        HELPER_ASSERT_SUCCESS(transcoder.transcode(&aac, outs));
        for (size_t j = 0; j < outs.size(); j++) {
            opus_dts.push_back(outs[j]->dts_);
        }
        transcoder.free_frames(outs);
    }
}

// The Opus transcoded from AAC whose first frame is at 1457ms, as ffmpeg's
// TS over SRT starts, keeps the AAC's times from its first frame: no frame
// before the AAC starts, and 20ms between frames, with no jump.
VOID TEST(AudioTranscoderTest, AacToOpusKeepsLateStartTime)
{
    std::vector<int64_t> opus_dts;
    int64_t last_aac_dts = 0;
    transcode_aac_to_opus(1457, 50, opus_dts, last_aac_dts);
    ASSERT_GE(opus_dts.size(), 40u);

    // The Opus encoder's 6.5ms delay moves the frames a little before the AAC.
    EXPECT_GE(opus_dts[0], 1457 - 20);
    EXPECT_LE(opus_dts[0], 1457 + 40);
    for (size_t i = 1; i < opus_dts.size(); i++) {
        EXPECT_GE(opus_dts[i] - opus_dts[i - 1], 19) << "frame " << i << " at " << opus_dts[i] << " after " << opus_dts[i - 1];
        EXPECT_LE(opus_dts[i] - opus_dts[i - 1], 21) << "frame " << i << " at " << opus_dts[i] << " after " << opus_dts[i - 1];
    }
    EXPECT_LE(opus_dts.back(), last_aac_dts + 40);
}

// The Opus transcoded from AAC that starts at 0, as RTMP does, starts at 0.
VOID TEST(AudioTranscoderTest, AacToOpusStartsAtZero)
{
    std::vector<int64_t> opus_dts;
    int64_t last_aac_dts = 0;
    transcode_aac_to_opus(0, 50, opus_dts, last_aac_dts);
    ASSERT_GE(opus_dts.size(), 40u);

    EXPECT_GE(opus_dts[0], -20);
    EXPECT_LE(opus_dts[0], 40);
    for (size_t i = 1; i < opus_dts.size(); i++) {
        EXPECT_GE(opus_dts[i] - opus_dts[i - 1], 19) << "frame " << i;
        EXPECT_LE(opus_dts[i] - opus_dts[i - 1], 21) << "frame " << i;
    }
}
#endif

// Test SrsDvrAsyncCallOnHls::call() method
VOID TEST(DvrAsyncCallOnHlsTest, CallWithMultipleHooks)
{
    srs_error_t err;

    // Create mock config with HTTP hooks enabled
    MockAppConfig mock_config;
    mock_config.http_hooks_enabled_ = true;

    // Create on_hls directive with multiple hook URLs
    mock_config.on_hls_directive_ = new SrsConfDirective();
    mock_config.on_hls_directive_->name_ = "on_hls";
    mock_config.on_hls_directive_->args_.push_back("http://example.com/hook1");
    mock_config.on_hls_directive_->args_.push_back("http://example.com/hook2");

    // Create mock hooks
    MockHttpHooks mock_hooks;

    // Create mock request
    MockRequest mock_req("test_vhost", "live", "stream");

    // Create SrsDvrAsyncCallOnHls instance
    SrsContextId cid;
    SrsDvrAsyncCallOnHls call(cid, &mock_req, "/path/to/file.ts", "http://example.com/file.ts",
                              "m3u8_content", "http://example.com/playlist.m3u8", 1, 10 * SRS_UTIME_SECONDS);

    // Replace global config and hooks with mocks
    call.config_ = &mock_config;
    call.hooks_ = &mock_hooks;

    // Call should succeed and invoke hooks for each URL
    HELPER_EXPECT_SUCCESS(call.call());
}

// Mock HLS muxer for testing SrsHlsController::reap_segment
class MockHlsMuxerForReapSegment : public ISrsHlsMuxer
{
    SRS_DECLARE_PRIVATE:
    int segment_close_count_;
    int segment_open_count_;
    int flush_video_count_;
    int flush_audio_count_;
    srs_error_t segment_close_error_;
    srs_error_t segment_open_error_;
    srs_error_t flush_video_error_;
    srs_error_t flush_audio_error_;

public:
    MockHlsMuxerForReapSegment()
    {
        segment_close_count_ = 0;
        segment_open_count_ = 0;
        flush_video_count_ = 0;
        flush_audio_count_ = 0;
        segment_close_error_ = srs_success;
        segment_open_error_ = srs_success;
        flush_video_error_ = srs_success;
        flush_audio_error_ = srs_success;
    }

    virtual ~MockHlsMuxerForReapSegment()
    {
        srs_freep(segment_close_error_);
        srs_freep(segment_open_error_);
        srs_freep(flush_video_error_);
        srs_freep(flush_audio_error_);
    }

    // ISrsHlsMuxer interface - only implement methods used by reap_segment
    virtual srs_error_t initialize() { return srs_success; }
    virtual void dispose() {}
    virtual int sequence_no() { return 0; }
    virtual std::string ts_url() { return ""; }
    virtual srs_utime_t duration() { return 0; }
    virtual int deviation() { return 0; }
    virtual SrsAudioCodecId latest_acodec() { return SrsAudioCodecIdForbidden; }
    virtual void set_latest_acodec(SrsAudioCodecId v) {}
    virtual SrsVideoCodecId latest_vcodec() { return SrsVideoCodecIdForbidden; }
    virtual void set_latest_vcodec(SrsVideoCodecId v) {}
    virtual bool pure_audio() { return false; }
    virtual bool is_segment_overflow() { return false; }
    virtual bool is_segment_absolutely_overflow() { return false; }
    virtual bool wait_keyframe() { return false; }
    virtual srs_error_t on_publish(ISrsRequest *req) { return srs_success; }
    virtual srs_error_t on_unpublish() { return srs_success; }
    virtual srs_error_t update_config(ISrsRequest *r, std::string entry_prefix,
                                      std::string path, std::string m3u8_file, std::string ts_file,
                                      srs_utime_t fragment, srs_utime_t window, bool ts_floor, double aof_ratio,
                                      bool cleanup, bool wait_keyframe, bool keys, int fragments_per_key,
                                      std::string key_file, std::string key_file_path, std::string key_url)
    {
        return srs_success;
    }
    virtual srs_error_t on_sequence_header() { return srs_success; }
    virtual void update_duration(uint64_t dts) {}
    virtual srs_error_t recover_hls() { return srs_success; }

    // Methods used by reap_segment
    virtual srs_error_t segment_close()
    {
        segment_close_count_++;
        return srs_error_copy(segment_close_error_);
    }

    virtual srs_error_t segment_open()
    {
        segment_open_count_++;
        return srs_error_copy(segment_open_error_);
    }

    virtual srs_error_t flush_video(SrsTsMessageCache *cache)
    {
        flush_video_count_++;
        return srs_error_copy(flush_video_error_);
    }

    virtual srs_error_t flush_audio(SrsTsMessageCache *cache)
    {
        flush_audio_count_++;
        return srs_error_copy(flush_audio_error_);
    }

    // Test helpers
    void set_segment_close_error(srs_error_t err)
    {
        srs_freep(segment_close_error_);
        segment_close_error_ = srs_error_copy(err);
    }

    void set_segment_open_error(srs_error_t err)
    {
        srs_freep(segment_open_error_);
        segment_open_error_ = srs_error_copy(err);
    }

    void set_flush_video_error(srs_error_t err)
    {
        srs_freep(flush_video_error_);
        flush_video_error_ = srs_error_copy(err);
    }

    void set_flush_audio_error(srs_error_t err)
    {
        srs_freep(flush_audio_error_);
        flush_audio_error_ = srs_error_copy(err);
    }

    int get_segment_close_count() const { return segment_close_count_; }
    int get_segment_open_count() const { return segment_open_count_; }
    int get_flush_video_count() const { return flush_video_count_; }
    int get_flush_audio_count() const { return flush_audio_count_; }
};

// Test: SrsHlsController::reap_segment success path
VOID TEST(HlsControllerTest, ReapSegmentSuccess)
{
    srs_error_t err;

    // Create controller
    SrsHlsController controller;

    // Replace muxer with mock
    MockHlsMuxerForReapSegment *mock_muxer = new MockHlsMuxerForReapSegment();
    srs_freep(controller.muxer_);
    controller.muxer_ = mock_muxer;

    // Call reap_segment - should succeed
    HELPER_EXPECT_SUCCESS(controller.reap_segment());

    // Verify the sequence of operations
    EXPECT_EQ(1, mock_muxer->get_segment_close_count());
    EXPECT_EQ(1, mock_muxer->get_segment_open_count());
    EXPECT_EQ(1, mock_muxer->get_flush_video_count());
    EXPECT_EQ(1, mock_muxer->get_flush_audio_count());
}

// Mock HLS segment for testing do_segment_close
class MockHlsSegmentForSegmentClose : public SrsHlsSegment
{
    SRS_DECLARE_PRIVATE:
    srs_error_t rename_error_;
    srs_utime_t mock_duration_;
    bool rename_called_;

public:
    MockHlsSegmentForSegmentClose() : SrsHlsSegment(NULL, SrsAudioCodecIdAAC, SrsVideoCodecIdAVC, NULL)
    {
        rename_error_ = srs_success;
        mock_duration_ = 10 * SRS_UTIME_SECONDS; // Default 10 seconds
        rename_called_ = false;
        sequence_no_ = 1;
        uri_ = "segment-1.ts";
        // tscw_ is already NULL from base class, leave it NULL
    }

    virtual ~MockHlsSegmentForSegmentClose()
    {
        srs_freep(rename_error_);
    }

    virtual srs_error_t rename()
    {
        rename_called_ = true;
        return srs_error_copy(rename_error_);
    }

    virtual srs_utime_t duration()
    {
        return mock_duration_;
    }

    void set_rename_error(srs_error_t err)
    {
        srs_freep(rename_error_);
        rename_error_ = srs_error_copy(err);
    }

    void set_duration(srs_utime_t dur)
    {
        mock_duration_ = dur;
    }

    bool is_rename_called() const
    {
        return rename_called_;
    }
};

// Test: SrsHlsMuxer::do_segment_close2 success path with valid duration
VOID TEST(HlsMuxerTest, DoSegmentCloseSuccess)
{
    srs_error_t err;

    // Create HLS muxer
    SrsHlsMuxer muxer;
    HELPER_EXPECT_SUCCESS(muxer.initialize());

    // Create mock request
    MockRequest req("test_vhost", "live", "stream");
    muxer.req_ = &req;

    // Create mock segment with valid duration (10 seconds)
    MockHlsSegmentForSegmentClose *mock_segment = new MockHlsSegmentForSegmentClose();
    mock_segment->set_duration(10 * SRS_UTIME_SECONDS);
    muxer.current_ = mock_segment;

    // Set max_td_ to 10 seconds (fragment duration)
    muxer.max_td_ = 10 * SRS_UTIME_SECONDS;

    // Call do_segment_close2 - should succeed
    HELPER_EXPECT_SUCCESS(muxer.do_segment_close2());

    // Verify segment was renamed
    EXPECT_TRUE(mock_segment->is_rename_called());

    // Verify segment was added to segments window
    EXPECT_EQ(1, muxer.segments_->size());

    // Verify current_ is set to NULL
    EXPECT_TRUE(muxer.current_ == NULL);

    // Cleanup
    muxer.req_ = NULL;
}

// Test: SrsHlsMuxer::generate_ts_filename with hls_ts_floor enabled
VOID TEST(HlsMuxerTest, GenerateTsFilenameWithFloor)
{
    // Create HLS muxer
    SrsHlsMuxer muxer;

    // Create mock request
    MockRequest req("test_vhost", "live", "stream");
    muxer.req_ = &req;

    // Set up muxer configuration with ts_floor enabled
    muxer.hls_ts_file_ = "[vhost]/[app]/[stream]-[timestamp]-[seq].ts";
    muxer.hls_ts_floor_ = true;
    muxer.hls_fragment_ = 10 * SRS_UTIME_SECONDS;
    muxer.accept_floor_ts_ = 0;
    muxer.previous_floor_ts_ = 0;
    muxer.deviation_ts_ = 0;

    // Create a mock segment with sequence number
    SrsHlsSegment *segment = new SrsHlsSegment(muxer.context_, SrsAudioCodecIdAAC, SrsVideoCodecIdDisabled, new MockSrsFileWriter());
    segment->sequence_no_ = 100;
    muxer.current_ = segment;

    // Call generate_ts_filename
    std::string ts_filename = muxer.generate_ts_filename();

    // Verify the filename contains replaced variables
    EXPECT_TRUE(ts_filename.find("test_vhost") != std::string::npos);
    EXPECT_TRUE(ts_filename.find("live") != std::string::npos);
    EXPECT_TRUE(ts_filename.find("stream") != std::string::npos);
    EXPECT_TRUE(ts_filename.find("100") != std::string::npos); // sequence number

    // Verify accept_floor_ts_ was initialized (should be current_floor_ts - 1 on first call)
    EXPECT_TRUE(muxer.accept_floor_ts_ > 0);

    // Verify previous_floor_ts_ was set
    EXPECT_TRUE(muxer.previous_floor_ts_ > 0);

    // Verify deviation_ts_ was calculated
    EXPECT_TRUE(muxer.deviation_ts_ <= 0); // Should be negative or zero since accept_floor_ts_ starts at current_floor_ts - 1

    // Call again to test the increment logic
    int64_t first_accept_floor_ts = muxer.accept_floor_ts_;
    segment->sequence_no_ = 101;
    std::string ts_filename2 = muxer.generate_ts_filename();

    // Verify accept_floor_ts_ was incremented
    EXPECT_EQ(first_accept_floor_ts + 1, muxer.accept_floor_ts_);

    // Verify sequence number was replaced
    EXPECT_TRUE(ts_filename2.find("101") != std::string::npos);

    // Cleanup
    muxer.req_ = NULL;
    muxer.current_ = NULL;
    srs_freep(segment);
}

// Mock segment for testing do_refresh_m3u8_segment
class MockHlsM4sSegment : public SrsHlsM4sSegment
{
public:
    bool is_sequence_header_;
    srs_utime_t duration_;
    std::string fullpath_;

    MockHlsM4sSegment() : SrsHlsM4sSegment(NULL)
    {
        is_sequence_header_ = false;
        duration_ = 5000 * SRS_UTIME_MILLISECONDS; // 5 seconds
        fullpath_ = "/path/to/segment-[duration].m4s";
        sequence_no_ = 0;
        memset(iv_, 0, 16);
        // Set a test IV value
        for (int i = 0; i < 16; i++) {
            iv_[i] = i;
        }
    }

    virtual ~MockHlsM4sSegment() {}

    virtual bool is_sequence_header() { return is_sequence_header_; }
    virtual srs_utime_t duration() { return duration_; }
    virtual std::string fullpath() { return fullpath_; }
};

// Test: do_refresh_m3u8_segment with encryption enabled
VOID TEST(HlsFmp4MuxerTest, DoRefreshM3u8SegmentWithEncryption)
{
    srs_error_t err;

    // Create muxer and set up encryption
    SrsHlsFmp4Muxer muxer;

    // Set up request
    MockRequest req("test_vhost", "test_app", "test_stream");
    muxer.req_ = &req;

    // Enable encryption
    muxer.hls_keys_ = true;
    muxer.hls_fragments_per_key_ = 5;
    muxer.hls_key_file_ = "key-[seq].key";
    muxer.hls_key_url_ = "https://example.com/keys/";

    // Create mock segment
    MockHlsM4sSegment segment;
    segment.sequence_no_ = 10;                         // 10 % 5 == 0, so key should be written
    segment.is_sequence_header_ = true;                // Should write discontinuity
    segment.duration_ = 5000 * SRS_UTIME_MILLISECONDS; // 5 seconds
    segment.fullpath_ = "/path/to/segment-[duration].m4s";

    // Call do_refresh_m3u8_segment
    std::stringstream ss;
    HELPER_EXPECT_SUCCESS(muxer.do_refresh_m3u8_segment(&segment, ss));

    // Verify output
    std::string output = ss.str();

    // Should contain discontinuity tag
    EXPECT_TRUE(output.find("#EXT-X-DISCONTINUITY") != std::string::npos);

    // Should contain encryption key tag
    EXPECT_TRUE(output.find("#EXT-X-KEY:METHOD=SAMPLE-AES") != std::string::npos);
    EXPECT_TRUE(output.find("https://example.com/keys/") != std::string::npos);
    EXPECT_TRUE(output.find("key-10.key") != std::string::npos);
    EXPECT_TRUE(output.find("IV=0x") != std::string::npos);

    // Should contain EXTINF tag with duration
    EXPECT_TRUE(output.find("#EXTINF:5.000") != std::string::npos);

    // Should contain segment filename
    EXPECT_TRUE(output.find("segment-5000.m4s") != std::string::npos);

    // Cleanup
    muxer.req_ = NULL;
}

// Test: with no hls_key_url, the key URI of fmp4 is the key file's path from the playlist's dir
VOID TEST(HlsFmp4MuxerTest, DoRefreshM3u8SegmentDefaultKeyUrl)
{
    srs_error_t err;

    SrsHlsFmp4Muxer muxer;
    MockRequest req("test_vhost", "test_app", "test_stream");
    muxer.req_ = &req;

    muxer.hls_keys_ = true;
    muxer.hls_fragments_per_key_ = 5;
    muxer.hls_key_file_ = "[app]/[stream]-[seq].key";
    muxer.hls_key_file_path_ = "./objs/nginx/html";
    muxer.hls_key_url_ = "";
    muxer.m3u8_dir_ = "./objs/nginx/html/test_app";

    MockHlsM4sSegment segment;
    segment.sequence_no_ = 10;
    segment.fullpath_ = "./objs/nginx/html/test_app/test_stream-10.m4s";

    // The key is written beside the playlist, so the URI resolves from the playlist to the key.
    std::stringstream ss;
    HELPER_EXPECT_SUCCESS(muxer.do_refresh_m3u8_segment(&segment, ss));
    EXPECT_TRUE(ss.str().find("URI=\"test_stream-10.key\"") != std::string::npos) << ss.str();

    // A key outside the playlist's dir keeps the key file's path.
    muxer.hls_key_file_path_ = "/data/keys";
    std::stringstream ss2;
    HELPER_EXPECT_SUCCESS(muxer.do_refresh_m3u8_segment(&segment, ss2));
    EXPECT_TRUE(ss2.str().find("URI=\"test_app/test_stream-10.key\"") != std::string::npos) << ss2.str();

    muxer.req_ = NULL;
}

// Mock HLS segment for testing SrsHlsMuxer::do_refresh_m3u8_segment
class MockHlsSegmentForRefreshM3u8 : public SrsHlsSegment
{
    SRS_DECLARE_PRIVATE:
    bool is_sequence_header_;
    srs_utime_t duration_;

public:
    MockHlsSegmentForRefreshM3u8() : SrsHlsSegment(NULL, SrsAudioCodecIdAAC, SrsVideoCodecIdAVC, NULL)
    {
        is_sequence_header_ = false;
        duration_ = 5000 * SRS_UTIME_MILLISECONDS; // 5 seconds
        sequence_no_ = 0;
        uri_ = "segment-[duration].ts";
        // Set a test IV value
        for (int i = 0; i < 16; i++) {
            iv_[i] = i;
        }
    }

    virtual ~MockHlsSegmentForRefreshM3u8() {}

    virtual bool is_sequence_header() { return is_sequence_header_; }
    virtual srs_utime_t duration() { return duration_; }

    void set_is_sequence_header(bool v) { is_sequence_header_ = v; }
    void set_duration(srs_utime_t dur) { duration_ = dur; }
};

// Test: SrsHlsMuxer::do_refresh_m3u8_segment with encryption enabled
VOID TEST(HlsMuxerTest, DoRefreshM3u8SegmentWithEncryption)
{
    srs_error_t err;

    // Create muxer
    SrsHlsMuxer muxer;

    // Set up request
    MockRequest req("test_vhost", "test_app", "test_stream");
    muxer.req_ = &req;

    // Enable encryption
    muxer.hls_keys_ = true;
    muxer.hls_fragments_per_key_ = 5;
    muxer.hls_key_file_ = "key-[seq].key";
    muxer.hls_key_url_ = "https://example.com/keys/";

    // Create mock segment
    MockHlsSegmentForRefreshM3u8 segment;
    segment.sequence_no_ = 10;                           // 10 % 5 == 0, so key should be written
    segment.set_is_sequence_header(true);                // Should write discontinuity
    segment.set_duration(5000 * SRS_UTIME_MILLISECONDS); // 5 seconds

    // Call do_refresh_m3u8_segment
    std::stringstream ss;
    HELPER_EXPECT_SUCCESS(muxer.do_refresh_m3u8_segment(&segment, ss));

    // Verify output
    std::string output = ss.str();

    // Should contain discontinuity tag
    EXPECT_TRUE(output.find("#EXT-X-DISCONTINUITY") != std::string::npos);

    // Should contain encryption key tag with AES-128 method
    EXPECT_TRUE(output.find("#EXT-X-KEY:METHOD=AES-128") != std::string::npos);
    EXPECT_TRUE(output.find("https://example.com/keys/") != std::string::npos);
    EXPECT_TRUE(output.find("key-10.key") != std::string::npos);
    EXPECT_TRUE(output.find("IV=0x") != std::string::npos);

    // Should contain EXTINF tag with duration
    EXPECT_TRUE(output.find("#EXTINF:5.000") != std::string::npos);

    // Should contain segment filename with duration replaced
    EXPECT_TRUE(output.find("segment-5000.ts") != std::string::npos);

    // Cleanup
    muxer.req_ = NULL;
}


// Test: with no hls_key_url, the key URI of TS is the key file's path from the playlist's dir
VOID TEST(HlsMuxerTest, DoRefreshM3u8SegmentDefaultKeyUrl)
{
    srs_error_t err;

    SrsHlsMuxer muxer;
    MockRequest req("test_vhost", "test_app", "test_stream");
    muxer.req_ = &req;

    muxer.hls_keys_ = true;
    muxer.hls_fragments_per_key_ = 5;
    muxer.hls_key_file_ = "[app]/[stream]-[seq].key";
    muxer.hls_key_file_path_ = "./objs/nginx/html";
    muxer.hls_key_url_ = "";
    muxer.m3u8_dir_ = "./objs/nginx/html/test_app";

    MockHlsSegmentForRefreshM3u8 segment;
    segment.sequence_no_ = 10;

    // The key is written beside the playlist, so the URI resolves from the playlist to the key.
    std::stringstream ss;
    HELPER_EXPECT_SUCCESS(muxer.do_refresh_m3u8_segment(&segment, ss));
    EXPECT_TRUE(ss.str().find("URI=\"test_stream-10.key\"") != std::string::npos) << ss.str();

    muxer.req_ = NULL;
}
// Mock segment for testing SrsHlsFmp4Muxer::generate_m4s_filename
class MockHlsM4sSegmentForFilename : public SrsHlsM4sSegment
{
public:
    MockHlsM4sSegmentForFilename() : SrsHlsM4sSegment(NULL)
    {
        sequence_no_ = 0;
    }

    virtual ~MockHlsM4sSegmentForFilename() {}
};

// Test: SrsHlsFmp4Muxer::generate_m4s_filename with hls_ts_floor enabled
VOID TEST(HlsFmp4MuxerTest, GenerateM4sFilenameWithFloor)
{
    // Create HLS fmp4 muxer
    SrsHlsFmp4Muxer muxer;

    // Create mock request
    MockRequest req("test_vhost", "live", "stream");
    muxer.req_ = &req;

    // Set up muxer configuration with ts_floor enabled
    muxer.hls_m4s_file_ = "[vhost]/[app]/[stream]-[timestamp]-[seq].m4s";
    muxer.hls_ts_floor_ = true;
    muxer.hls_fragment_ = 10 * SRS_UTIME_SECONDS;
    muxer.accept_floor_ts_ = 0;
    muxer.previous_floor_ts_ = 0;
    muxer.deviation_ts_ = 0;

    // Create a mock segment with sequence number
    MockHlsM4sSegmentForFilename *segment = new MockHlsM4sSegmentForFilename();
    segment->sequence_no_ = 100;
    muxer.current_ = segment;

    // Call generate_m4s_filename
    std::string m4s_filename = muxer.generate_m4s_filename();

    // Verify the filename contains replaced variables
    EXPECT_TRUE(m4s_filename.find("test_vhost") != std::string::npos);
    EXPECT_TRUE(m4s_filename.find("live") != std::string::npos);
    EXPECT_TRUE(m4s_filename.find("stream") != std::string::npos);
    EXPECT_TRUE(m4s_filename.find("100") != std::string::npos); // sequence number

    // Verify accept_floor_ts_ was initialized (should be current_floor_ts - 1 on first call)
    EXPECT_TRUE(muxer.accept_floor_ts_ > 0);

    // Verify previous_floor_ts_ was set
    EXPECT_TRUE(muxer.previous_floor_ts_ > 0);

    // Verify deviation_ts_ was calculated
    EXPECT_TRUE(muxer.deviation_ts_ <= 0); // Should be negative or zero since accept_floor_ts_ starts at current_floor_ts - 1

    // Call again to test the increment logic
    int64_t first_accept_floor_ts = muxer.accept_floor_ts_;
    segment->sequence_no_ = 101;
    std::string m4s_filename2 = muxer.generate_m4s_filename();

    // Verify accept_floor_ts_ was incremented
    EXPECT_EQ(first_accept_floor_ts + 1, muxer.accept_floor_ts_);

    // Verify sequence number was replaced
    EXPECT_TRUE(m4s_filename2.find("101") != std::string::npos);

    // Cleanup
    muxer.req_ = NULL;
    muxer.current_ = NULL;
    srs_freep(segment);
}

// Test: srs_hex_encode_to_string_lowercase converts bytes to lowercase hex string
VOID TEST(KernelUtilityTest, HexEncodeToStringLowercase)
{
    // Test normal case: convert bytes to lowercase hex
    uint8_t src[] = {0xAB, 0xCD, 0xEF, 0x12, 0x34};
    char des[11] = {0}; // 5 bytes * 2 chars + 1 null terminator

    char *result = srs_hex_encode_to_string_lowercase(des, src, 5);

    EXPECT_TRUE(result != NULL);
    EXPECT_STREQ("abcdef1234", des);

    // Test NULL source
    EXPECT_TRUE(NULL == srs_hex_encode_to_string_lowercase(des, NULL, 5));

    // Test zero length
    EXPECT_TRUE(NULL == srs_hex_encode_to_string_lowercase(des, src, 0));

    // Test NULL destination
    EXPECT_TRUE(NULL == srs_hex_encode_to_string_lowercase(NULL, src, 5));
}

// Test: srs_strings_dumps_hex(const std::string &str) dumps string to hex format
VOID TEST(KernelUtilityTest, StringsDumpsHexWithString)
{
    // Test normal case: dump string to hex
    std::string input = "ABC";
    std::string hex_result = srs_strings_dumps_hex(input);

    // Should contain hex values for 'A' (0x41), 'B' (0x42), 'C' (0x43)
    EXPECT_TRUE(hex_result.find("41") != std::string::npos);
    EXPECT_TRUE(hex_result.find("42") != std::string::npos);
    EXPECT_TRUE(hex_result.find("43") != std::string::npos);

    // Test empty string
    std::string empty_input = "";
    std::string empty_result = srs_strings_dumps_hex(empty_input);
    EXPECT_TRUE(empty_result.empty());
}

// Test: srs_hex_decode_string decodes hex string to bytes
VOID TEST(KernelUtilityTest, HexDecodeString)
{
    // Test normal case: decode valid hex string
    if (true) {
        std::string hex_str = "42e01f";
        uint8_t data[3];

        int result = srs_hex_decode_string(data, hex_str.c_str(), (int)hex_str.length());

        EXPECT_EQ(3, result);
        EXPECT_EQ(0x42, data[0]);
        EXPECT_EQ(0xe0, data[1]);
        EXPECT_EQ(0x1f, data[2]);
    }

    // Test uppercase hex string
    if (true) {
        std::string hex_str = "ABCDEF";
        uint8_t data[3];

        int result = srs_hex_decode_string(data, hex_str.c_str(), (int)hex_str.length());

        EXPECT_EQ(3, result);
        EXPECT_EQ(0xAB, data[0]);
        EXPECT_EQ(0xCD, data[1]);
        EXPECT_EQ(0xEF, data[2]);
    }

    // Test mixed case hex string
    if (true) {
        std::string hex_str = "aB12Cd";
        uint8_t data[3];

        int result = srs_hex_decode_string(data, hex_str.c_str(), (int)hex_str.length());

        EXPECT_EQ(3, result);
        EXPECT_EQ(0xAB, data[0]);
        EXPECT_EQ(0x12, data[1]);
        EXPECT_EQ(0xCD, data[2]);
    }

    // Test error case: NULL pointer
    if (true) {
        uint8_t data[3];
        EXPECT_EQ(-1, srs_hex_decode_string(data, NULL, 6));
    }

    // Test error case: odd length (not pairs of hex digits)
    if (true) {
        uint8_t data[3];
        EXPECT_EQ(-1, srs_hex_decode_string(data, "abc", 3));
    }

    // Test error case: invalid hex character
    if (true) {
        uint8_t data[3];
        EXPECT_EQ(-1, srs_hex_decode_string(data, "abcg", 4));
    }
}

// Test: srs_is_boolean checks if string is "true" or "false"
VOID TEST(AppUtilityTest, IsBoolean)
{
    // Test "true" string
    EXPECT_TRUE(srs_is_boolean("true"));

    // Test "false" string
    EXPECT_TRUE(srs_is_boolean("false"));

    // Test non-boolean strings
    EXPECT_FALSE(srs_is_boolean("True"));
    EXPECT_FALSE(srs_is_boolean("False"));
    EXPECT_FALSE(srs_is_boolean("TRUE"));
    EXPECT_FALSE(srs_is_boolean("FALSE"));
    EXPECT_FALSE(srs_is_boolean("yes"));
    EXPECT_FALSE(srs_is_boolean("no"));
    EXPECT_FALSE(srs_is_boolean("1"));
    EXPECT_FALSE(srs_is_boolean("0"));
    EXPECT_FALSE(srs_is_boolean(""));
    EXPECT_FALSE(srs_is_boolean("random"));
}

// Test: Parse libdatachannel SDP from issue 4570 and verify fields
VOID TEST(SdpTest, ParseLibdatachannelSdpFromIssue4570)
{
    srs_error_t err;

    // SDP from issue 4570 - libdatachannel format with video first, then audio
    std::string sdp_str =
        "v=0\r\n"
        "o=- rtc 4158491451 0 IN IP4 127.0.0.1\r\n"
        "s=-\r\n"
        "t=0 0\r\n"
        "a=group:BUNDLE video audio\r\n"
        "a=group:LS video audio\r\n"
        "a=msid-semantic:WMS *\r\n"
        "a=ice-options:ice2,trickle\r\n"
        "a=fingerprint:sha-256 28:37:F7:18:77:FC:46:33:6F:B2:0F:12:83:C2:BF:5C:61:5E:96:EB:4B:B9:97:81:92:7C:82:10:97:B8:8E:60\r\n"
        "m=video 56144 UDP/TLS/RTP/SAVPF 96 97\r\n"
        "c=IN IP4 172.24.64.1\r\n"
        "a=mid:video\r\n"
        "a=sendonly\r\n"
        "a=ssrc:42 cname:video-send\r\n"
        "a=rtcp-mux\r\n"
        "a=rtpmap:96 H264/90000\r\n"
        "a=rtcp-fb:96 nack\r\n"
        "a=rtcp-fb:96 nack pli\r\n"
        "a=rtcp-fb:96 goog-remb\r\n"
        "a=fmtp:96 profile-level-id=42e01f;packetization-mode=1;level-asymmetry-allowed=1\r\n"
        "a=rtpmap:97 RTX/90000\r\n"
        "a=fmtp:97 apt=96\r\n"
        "a=setup:actpass\r\n"
        "a=ice-ufrag:fEw/\r\n"
        "a=ice-pwd:jBua8YGWQKc/Vn6Y9EZ9+0\r\n"
        "a=candidate:1 1 UDP 2122317823 172.24.64.1 56144 typ host\r\n"
        "a=candidate:2 1 UDP 2122315767 10.0.0.94 56144 typ host\r\n"
        "a=candidate:3 1 UDP 1686189695 111.43.134.137 56144 typ srflx raddr 0.0.0.0 rport 0\r\n"
        "a=end-of-candidates\r\n"
        "m=audio 56144 UDP/TLS/RTP/SAVPF 111\r\n"
        "c=IN IP4 172.24.64.1\r\n"
        "a=mid:audio\r\n"
        "a=sendonly\r\n"
        "a=ssrc:43 cname:audio-send\r\n"
        "a=rtcp-mux\r\n"
        "a=rtpmap:111 opus/48000/2\r\n"
        "a=fmtp:111 minptime=10;maxaveragebitrate=98000;stereo=1;sprop-stereo=1;useinbandfec=1\r\n"
        "a=setup:actpass\r\n"
        "a=ice-ufrag:fEw/\r\n"
        "a=ice-pwd:jBua8YGWQKc/Vn6Y9EZ9+0\r\n";

    // Parse the SDP
    SrsSdp sdp;
    HELPER_EXPECT_SUCCESS(sdp.parse(sdp_str));

    // Verify session-level fields
    EXPECT_TRUE(sdp.version_ == "0");
    EXPECT_TRUE(sdp.group_policy_ == "BUNDLE");
    EXPECT_TRUE(sdp.groups_.size() == 2);
    EXPECT_TRUE(sdp.groups_[0] == "video");
    EXPECT_TRUE(sdp.groups_[1] == "audio");

    // Verify we have 2 media descriptions (video and audio)
    EXPECT_TRUE(sdp.media_descs_.size() == 2);

    // Verify first media description is video
    SrsMediaDesc *video_desc = &sdp.media_descs_[0];
    EXPECT_TRUE(video_desc->type_ == "video");
    EXPECT_TRUE(video_desc->mid_ == "video");
    EXPECT_TRUE(video_desc->sendonly_);
    EXPECT_FALSE(video_desc->recvonly_);
    EXPECT_TRUE(video_desc->port_ == 56144);
    EXPECT_TRUE(video_desc->protos_ == "UDP/TLS/RTP/SAVPF");

    // Verify video payload types
    EXPECT_TRUE(video_desc->payload_types_.size() >= 1);

    // Find H264 payload (PT 96)
    SrsMediaPayloadType *h264_payload = NULL;
    for (size_t i = 0; i < video_desc->payload_types_.size(); i++) {
        if (video_desc->payload_types_[i].payload_type_ == 96) {
            h264_payload = &video_desc->payload_types_[i];
            break;
        }
    }
    EXPECT_TRUE(h264_payload != NULL);
    EXPECT_TRUE(h264_payload->encoding_name_ == "H264");
    EXPECT_TRUE(h264_payload->clock_rate_ == 90000);

    // Verify video SSRC
    EXPECT_TRUE(video_desc->ssrc_infos_.size() >= 1);
    bool found_video_ssrc = false;
    for (size_t i = 0; i < video_desc->ssrc_infos_.size(); i++) {
        if (video_desc->ssrc_infos_[i].ssrc_ == 42) {
            found_video_ssrc = true;
            EXPECT_TRUE(video_desc->ssrc_infos_[i].cname_ == "video-send");
            break;
        }
    }
    EXPECT_TRUE(found_video_ssrc);

    // Verify second media description is audio
    SrsMediaDesc *audio_desc = &sdp.media_descs_[1];
    EXPECT_TRUE(audio_desc->type_ == "audio");
    EXPECT_TRUE(audio_desc->mid_ == "audio");
    EXPECT_TRUE(audio_desc->sendonly_);
    EXPECT_FALSE(audio_desc->recvonly_);
    EXPECT_TRUE(audio_desc->port_ == 56144);
    EXPECT_TRUE(audio_desc->protos_ == "UDP/TLS/RTP/SAVPF");

    // Verify audio payload types
    EXPECT_TRUE(audio_desc->payload_types_.size() >= 1);

    // Find Opus payload (PT 111)
    SrsMediaPayloadType *opus_payload = NULL;
    for (size_t i = 0; i < audio_desc->payload_types_.size(); i++) {
        if (audio_desc->payload_types_[i].payload_type_ == 111) {
            opus_payload = &audio_desc->payload_types_[i];
            break;
        }
    }
    EXPECT_TRUE(opus_payload != NULL);
    EXPECT_TRUE(opus_payload->encoding_name_ == "opus");
    EXPECT_TRUE(opus_payload->clock_rate_ == 48000);
    EXPECT_TRUE(opus_payload->encoding_param_ == "2");

    // Verify audio SSRC
    EXPECT_TRUE(audio_desc->ssrc_infos_.size() >= 1);
    bool found_audio_ssrc = false;
    for (size_t i = 0; i < audio_desc->ssrc_infos_.size(); i++) {
        if (audio_desc->ssrc_infos_[i].ssrc_ == 43) {
            found_audio_ssrc = true;
            EXPECT_TRUE(audio_desc->ssrc_infos_[i].cname_ == "audio-send");
            break;
        }
    }
    EXPECT_TRUE(found_audio_ssrc);
}

// Test: srs_api_parse_pagination with various input scenarios
VOID TEST(HttpApiPaginationTest, ParsePagination)
{
    int start, count;

    // Test 1: Default values (no query parameters)
    if (true) {
        MockHttpMessageForApiResponse mock_msg;
        srs_api_parse_pagination(&mock_msg, start, count);
        EXPECT_EQ(0, start);
        EXPECT_EQ(10, count);
    }

    // Test 2: Valid start and count
    if (true) {
        MockHttpMessageForApiResponse mock_msg;
        mock_msg.query_params_["start"] = "5";
        mock_msg.query_params_["count"] = "20";
        srs_api_parse_pagination(&mock_msg, start, count);
        EXPECT_EQ(5, start);
        EXPECT_EQ(20, count);
    }

    // Test 3: Zero count (should use minimum 1)
    if (true) {
        MockHttpMessageForApiResponse mock_msg;
        mock_msg.query_params_["start"] = "0";
        mock_msg.query_params_["count"] = "0";
        srs_api_parse_pagination(&mock_msg, start, count);
        EXPECT_EQ(0, start);
        EXPECT_EQ(1, count);
    }

    // Test 4: Negative start (should use minimum 0)
    if (true) {
        MockHttpMessageForApiResponse mock_msg;
        mock_msg.query_params_["start"] = "-10";
        mock_msg.query_params_["count"] = "5";
        srs_api_parse_pagination(&mock_msg, start, count);
        EXPECT_EQ(0, start);
        EXPECT_EQ(5, count);
    }

    // Test 5: Negative count (should use minimum 1)
    if (true) {
        MockHttpMessageForApiResponse mock_msg;
        mock_msg.query_params_["start"] = "10";
        mock_msg.query_params_["count"] = "-5";
        srs_api_parse_pagination(&mock_msg, start, count);
        EXPECT_EQ(10, start);
        EXPECT_EQ(1, count);
    }

    // Test 6: Empty count string (should use default 10)
    if (true) {
        MockHttpMessageForApiResponse mock_msg;
        mock_msg.query_params_["start"] = "5";
        mock_msg.query_params_["count"] = "";
        srs_api_parse_pagination(&mock_msg, start, count);
        EXPECT_EQ(5, start);
        EXPECT_EQ(10, count);
    }

    // Test 7: Only start parameter
    if (true) {
        MockHttpMessageForApiResponse mock_msg;
        mock_msg.query_params_["start"] = "15";
        srs_api_parse_pagination(&mock_msg, start, count);
        EXPECT_EQ(15, start);
        EXPECT_EQ(10, count);
    }

    // Test 8: Only count parameter
    if (true) {
        MockHttpMessageForApiResponse mock_msg;
        mock_msg.query_params_["count"] = "25";
        srs_api_parse_pagination(&mock_msg, start, count);
        EXPECT_EQ(0, start);
        EXPECT_EQ(25, count);
    }

    // Test 9: Invalid (non-numeric) values
    if (true) {
        MockHttpMessageForApiResponse mock_msg;
        mock_msg.query_params_["start"] = "abc";
        mock_msg.query_params_["count"] = "xyz";
        srs_api_parse_pagination(&mock_msg, start, count);
        // atoi returns 0 for invalid strings
        EXPECT_EQ(0, start);
        EXPECT_EQ(1, count); // minimum enforced since atoi returns 0
    }

    // Test 10: Large values
    if (true) {
        MockHttpMessageForApiResponse mock_msg;
        mock_msg.query_params_["start"] = "1000000";
        mock_msg.query_params_["count"] = "500";
        srs_api_parse_pagination(&mock_msg, start, count);
        EXPECT_EQ(1000000, start);
        EXPECT_EQ(500, count);
    }
}

// Mock SrsHlsM4sSegment to capture dts passed to reap()
// Used for testing issue #4594
class MockSrsHlsM4sSegmentForBug4594 : public SrsHlsM4sSegment
{
public:
    uint64_t captured_reap_dts_;
    bool reap_called_;

    MockSrsHlsM4sSegmentForBug4594() : SrsHlsM4sSegment(NULL), captured_reap_dts_(0), reap_called_(false) {}
    virtual ~MockSrsHlsM4sSegmentForBug4594() {}

    // Override reap to capture the dts parameter
    virtual srs_error_t reap(uint64_t dts) {
        reap_called_ = true;
        captured_reap_dts_ = dts;
        return srs_success;
    }
};

// Mock factory to create MockSrsHlsM4sSegmentForBug4594
class MockAppFactoryForBug4594 : public SrsAppFactory
{
public:
    MockSrsHlsM4sSegmentForBug4594 *mock_segment_;

    MockAppFactoryForBug4594() : mock_segment_(NULL) {}
    virtual ~MockAppFactoryForBug4594() {}

    virtual SrsHlsM4sSegment *create_hls_m4s_segment(ISrsFileWriter *fw) {
        mock_segment_ = new MockSrsHlsM4sSegmentForBug4594();
        return mock_segment_;
    }
};

// Test for issue #4594: SrsHlsMp4Controller::on_unpublish() passes video_dts_=0 to reap()
// for audio-only streams.
//
// When on_unpublish() is called for audio-only streams, the muxer's video_dts_ is 0 because
// write_video() is never called. This causes reap(0) to be called, leading to
// incorrect last sample duration calculation (overflow to huge values like 4294894594).
//
// @see https://github.com/ossrs/srs/issues/4594
// @see https://github.com/ossrs/srs/pull/4602
VOID TEST(HlsFmp4AudioOnlyBugTest, OnUnpublishUsesVideoDtsZeroForAudioOnly)
{
    srs_error_t err;

    // Create SrsHlsMp4Controller - the entry point for HLS fMP4
    SrsHlsMp4Controller controller;

    // Access the internal muxer
    SrsHlsFmp4Muxer *muxer = controller.muxer_;

    // Set up mock request (required by do_segment_close for async hooks)
    MockRequest mock_req("test_vhost", "live", "stream");
    muxer->req_ = &mock_req;

    // The playlist the segment close writes, as a published stream has.
    muxer->m3u8_ = _srs_tmp_file_prefix + "bug4594.m3u8";

    // Create mock segment that captures the dts passed to reap()
    MockSrsHlsM4sSegmentForBug4594 *mock_segment = new MockSrsHlsM4sSegmentForBug4594();
    muxer->current_ = mock_segment;

    // Simulate audio-only stream condition:
    // - controller.audio_dts_ has a value (audio packets were written)
    // - controller.video_dts_ is 0 (no video packets, write_video() never called)
    controller.audio_dts_ = 72702;  // Example audio DTS in milliseconds
    controller.video_dts_ = 0;      // No video for audio-only stream

    // Call on_unpublish() - this triggers the bug path
    HELPER_EXPECT_SUCCESS(controller.on_unpublish());

    // Verify reap was called
    ASSERT_TRUE(mock_segment->reap_called_) << "reap() should have been called by on_unpublish()";

    // THE BUG TEST:
    // This test SHOULD FAIL on the buggy develop branch because
    // captured_reap_dts_ will be 0 instead of a proper audio timestamp (72702)
    EXPECT_GT(mock_segment->captured_reap_dts_, 0u)
        << "Bug #4594: on_unpublish() resulted in reap(dts=0) for audio-only stream! "
        << "Expected non-zero dts (e.g., audio_dts_=" << controller.audio_dts_ << ") "
        << "but reap() received " << mock_segment->captured_reap_dts_;

    // Cleanup
    ::unlink(muxer->m3u8_.c_str());
    muxer->req_ = NULL;
}

// A file writer that counts the writers alive.
class MockLiveFileWriter : public SrsFileWriter
{
public:
    int *live_;

    MockLiveFileWriter(int *live) : live_(live) { (*live_)++; }
    virtual ~MockLiveFileWriter() { (*live_)--; }
};

// An encrypting file writer that counts the writers alive.
class MockLiveEncFileWriter : public SrsEncFileWriter
{
public:
    int *live_;

    MockLiveEncFileWriter(int *live) : live_(live) { (*live_)++; }
    virtual ~MockLiveEncFileWriter() { (*live_)--; }
};

// A factory whose file writers count the writers alive.
class MockAppFactoryLiveWriters : public SrsAppFactory
{
public:
    int live_;

    MockAppFactoryLiveWriters() : live_(0) {}
    virtual ~MockAppFactoryLiveWriters() {}

    virtual ISrsFileWriter *create_file_writer() { return new MockLiveFileWriter(&live_); }
    virtual ISrsFileWriter *create_enc_file_writer() { return new MockLiveEncFileWriter(&live_); }
};

// Each publish updates the config of the fmp4 muxer: a republish keeps one segment writer.
VOID TEST(HlsFmp4MuxerWriterTest, RepublishKeepsOneWriter)
{
    srs_error_t err;

    MockAppFactoryLiveWriters factory;
    MockFmp4SrsRequest req;

    if (true) {
        SrsHlsFmp4Muxer muxer;
        muxer.app_factory_ = &factory;
        HELPER_ASSERT_SUCCESS(muxer.initialize(1, 2));

        HELPER_ASSERT_SUCCESS(muxer.update_config(&req));
        EXPECT_EQ(1, factory.live_);

        HELPER_ASSERT_SUCCESS(muxer.update_config(&req));
        HELPER_ASSERT_SUCCESS(muxer.update_config(&req));
        EXPECT_EQ(1, factory.live_);
    }

    EXPECT_EQ(0, factory.live_);
}

// Each publish updates the config of the TS muxer: a republish frees the last segment writer,
// also when hls_keys changes the kind of writer.
VOID TEST(HlsMuxerWriterTest, RepublishFreesLastWriter)
{
    srs_error_t err;

    MockAppFactoryLiveWriters factory;
    MockFmp4SrsRequest req;

    if (true) {
        SrsHlsMuxer muxer;
        muxer.app_factory_ = &factory;

        HELPER_ASSERT_SUCCESS(muxer.update_config(&req, "", "./objs/nginx/html", "[app]/[stream].m3u8", "[app]/[stream]-[seq].ts",
                                                  10 * SRS_UTIME_SECONDS, 60 * SRS_UTIME_SECONDS, false, 2.1, true, true, false, 5, "", "", ""));
        EXPECT_EQ(1, factory.live_);

        HELPER_ASSERT_SUCCESS(muxer.update_config(&req, "", "./objs/nginx/html", "[app]/[stream].m3u8", "[app]/[stream]-[seq].ts",
                                                  10 * SRS_UTIME_SECONDS, 60 * SRS_UTIME_SECONDS, false, 2.1, true, true, true, 5, "", "", ""));
        EXPECT_EQ(1, factory.live_);
        EXPECT_TRUE(dynamic_cast<SrsEncFileWriter *>(muxer.writer_) != NULL);
    }

    EXPECT_EQ(0, factory.live_);
}

// A file writer that counts the writers alive, and fails each write while asked to, as on a
// full disk. It records, at each close of an open file, the path and whether the file is still
// there.
class MockFailingLiveFileWriter : public MockLiveFileWriter
{
public:
    bool *fail_;
    std::vector<std::pair<std::string, bool> > *closes_;

    MockFailingLiveFileWriter(int *live, bool *fail, std::vector<std::pair<std::string, bool> > *closes)
        : MockLiveFileWriter(live), fail_(fail), closes_(closes) {}
    virtual ~MockFailingLiveFileWriter() { close(); }

    virtual void close()
    {
        if (is_open()) {
            SrsPath path;
            closes_->push_back(std::make_pair(path_, path.exists(path_)));
        }
        MockLiveFileWriter::close();
    }

    virtual srs_error_t write(void *buf, size_t count, ssize_t *pnwrite)
    {
        if (*fail_) {
            return srs_error_new(ERROR_SYSTEM_FILE_WRITE, "mock disk full");
        }
        return MockLiveFileWriter::write(buf, count, pnwrite);
    }
};

// A factory whose file writers count the writers alive, and fail each write while asked to.
class MockAppFactoryFailingWriters : public SrsAppFactory
{
public:
    int live_;
    bool fail_;
    std::vector<std::pair<std::string, bool> > closes_;

    MockAppFactoryFailingWriters() : live_(0), fail_(false) {}
    virtual ~MockAppFactoryFailingWriters() {}

    virtual ISrsFileWriter *create_file_writer() { return new MockFailingLiveFileWriter(&live_, &fail_, &closes_); }
};

// The TS unpublish fails to write the last audio and leaves its segment open, as with
// hls_on_error ignore on a full disk. The republish drops that segment and its temp file before
// it frees the writer the segment holds, and the new segment writes and closes with the new
// writer.
VOID TEST(HlsMuxerWriterTest, RepublishDropsSegmentLeftOpen)
{
    srs_error_t err;

    MockAppFactoryFailingWriters factory;
    MockFmp4SrsRequest req;
    req.stream_ = "hls-left-open";

    SrsPath path;
    string m3u8 = "./objs/nginx/html/live/hls-left-open.m3u8";
    if (path.exists(m3u8)) {
        HELPER_ASSERT_SUCCESS(path.unlink(m3u8));
    }

    if (true) {
        SrsHlsController controller;
        SrsHlsMuxer *muxer = dynamic_cast<SrsHlsMuxer *>(controller.muxer_);
        muxer->app_factory_ = &factory;
        HELPER_ASSERT_SUCCESS(controller.initialize());

        HELPER_ASSERT_SUCCESS(controller.on_publish(&req));
        ASSERT_TRUE(muxer->is_segment_open());
        muxer->set_latest_acodec(SrsAudioCodecIdAAC);
        EXPECT_EQ(1, factory.live_);
        int left_seq = muxer->current_->sequence_no_;
        string left_tmp = muxer->current_->tmppath();
        EXPECT_TRUE(path.exists(left_tmp));

        // An audio frame the unpublish can't write, so it returns before it closes the segment.
        controller.tsmc_->audio_ = new SrsTsMessage();
        controller.tsmc_->audio_->dts_ = 90000;
        controller.tsmc_->audio_->pts_ = 90000;
        controller.tsmc_->audio_->sid_ = SrsTsPESStreamIdAudioCommon;
        const char audio_data[] = {0x01, 0x02, 0x03, 0x04, 0x05};
        controller.tsmc_->audio_->payload_->append(audio_data, sizeof(audio_data));

        factory.fail_ = true;
        err = controller.on_unpublish();
        EXPECT_EQ(ERROR_SYSTEM_FILE_WRITE, srs_error_code(err));
        srs_freep(err);
        factory.fail_ = false;
        EXPECT_TRUE(muxer->is_segment_open());
        EXPECT_EQ(1, factory.live_);

        // The republish drops the segment left open and opens a new one on the new writer. The
        // writer is closed before the temp file is removed, as Windows refuses to remove an open
        // file.
        factory.closes_.clear();
        HELPER_ASSERT_SUCCESS(controller.on_publish(&req));
        EXPECT_EQ(1, factory.live_);
        EXPECT_FALSE(path.exists(left_tmp));
        ASSERT_EQ(1, (int)factory.closes_.size());
        EXPECT_EQ(left_tmp, factory.closes_[0].first);
        EXPECT_TRUE(factory.closes_[0].second);
        ASSERT_TRUE(muxer->is_segment_open());
        EXPECT_EQ(left_seq + 1, muxer->current_->sequence_no_);
        EXPECT_TRUE(muxer->current_->writer_ == muxer->writer_);
        string tmp = muxer->current_->tmppath();
        EXPECT_TRUE(path.exists(tmp));

        // The frame the last unpublish kept in the cache is written to the new segment, which
        // closes: too short to list, so its temp file is removed.
        HELPER_EXPECT_SUCCESS(controller.on_unpublish());
        EXPECT_TRUE(controller.tsmc_->audio_ == NULL);
        EXPECT_FALSE(muxer->is_segment_open());
        EXPECT_FALSE(path.exists(tmp));
        EXPECT_EQ(1, factory.live_);

        controller.dispose();
    }

    EXPECT_EQ(0, factory.live_);
}

// The TS publish fails to write the key of its first segment, which stays open with no file. The
// retry drops that segment before it frees the writer the segment holds, so its new segment
// writes the key and its file with the new writer.
VOID TEST(HlsMuxerWriterTest, KeyWriteFailedRetryOpensNewSegment)
{
    srs_error_t err;

    MockAppFactoryLiveWriters factory;
    MockFmp4SrsRequest req;
    req.stream_ = "hls-key-retry";

    // A dir at the path of the first segment's key, so that key can't be written.
    SrsPath path;
    string key_dir = "./objs/nginx/html/hls-key-retry-keys";
    HELPER_ASSERT_SUCCESS(path.mkdir_all(key_dir + "/live-hls-key-retry-0.key"));
    string key_file = key_dir + "/live-hls-key-retry-1.key";
    if (path.exists(key_file)) {
        HELPER_ASSERT_SUCCESS(path.unlink(key_file));
    }

    if (true) {
        SrsHlsMuxer muxer;
        muxer.app_factory_ = &factory;
        HELPER_ASSERT_SUCCESS(muxer.initialize());
        HELPER_ASSERT_SUCCESS(muxer.on_publish(&req));
        muxer.set_latest_vcodec(SrsVideoCodecIdAVC);

        HELPER_ASSERT_SUCCESS(muxer.update_config(&req, "", "./objs/nginx/html", "[app]/[stream].m3u8", "[app]/[stream]-[seq].ts",
                                                  10 * SRS_UTIME_SECONDS, 60 * SRS_UTIME_SECONDS, false, 2.1, true, true, true, 1,
                                                  "[app]-[stream]-[seq].key", key_dir, ""));
        HELPER_EXPECT_FAILED(muxer.segment_open());
        ASSERT_TRUE(muxer.is_segment_open());
        EXPECT_EQ(0, muxer.current_->sequence_no_);
        EXPECT_FALSE(muxer.writer_->is_open());
        EXPECT_EQ(1, factory.live_);

        // The retry opens a new segment and writes its key.
        HELPER_ASSERT_SUCCESS(muxer.update_config(&req, "", "./objs/nginx/html", "[app]/[stream].m3u8", "[app]/[stream]-[seq].ts",
                                                  10 * SRS_UTIME_SECONDS, 60 * SRS_UTIME_SECONDS, false, 2.1, true, true, true, 1,
                                                  "[app]-[stream]-[seq].key", key_dir, ""));
        EXPECT_EQ(1, factory.live_);
        HELPER_ASSERT_SUCCESS(muxer.segment_open());
        ASSERT_TRUE(muxer.is_segment_open());
        EXPECT_EQ(1, muxer.current_->sequence_no_);
        EXPECT_TRUE(muxer.current_->writer_ == muxer.writer_);
        EXPECT_TRUE(muxer.writer_->is_open());
        EXPECT_TRUE(path.exists(key_file));
        string tmp = muxer.current_->tmppath();
        EXPECT_TRUE(path.exists(tmp));

        // A frame is written with the new writer, and the segment closes.
        SrsTsMessageCache cache;
        cache.video_ = new SrsTsMessage();
        cache.video_->dts_ = 180000;
        cache.video_->pts_ = 180000;
        cache.video_->sid_ = SrsTsPESStreamIdVideoCommon;
        const char video_data[] = {0x00, 0x00, 0x00, 0x01, 0x65, 0x08, 0x04, 0x00};
        cache.video_->payload_->append(video_data, sizeof(video_data));
        HELPER_EXPECT_SUCCESS(muxer.flush_video(&cache));
        EXPECT_TRUE(cache.video_ == NULL);

        HELPER_EXPECT_SUCCESS(muxer.segment_close());
        EXPECT_FALSE(muxer.is_segment_open());
        EXPECT_FALSE(path.exists(tmp));
        EXPECT_EQ(1, factory.live_);

        muxer.dispose();
    }

    EXPECT_EQ(0, factory.live_);

    HELPER_EXPECT_SUCCESS(path.unlink(key_file));
}

// The fmp4 unpublish fails to close its segment, which stays open. The republish keeps the one
// writer, so the segment left open still writes with a live writer and closes.
VOID TEST(HlsFmp4MuxerWriterTest, RepublishKeepsWriterOfSegmentLeftOpen)
{
    srs_error_t err;

    MockAppFactoryLiveWriters factory;
    MockFmp4SrsRequest req;
    req.stream_ = "fmp4-left-open";

    SrsPath path;

    if (true) {
        SrsHlsFmp4Muxer muxer;
        muxer.app_factory_ = &factory;
        HELPER_ASSERT_SUCCESS(muxer.initialize(1, 2));
        HELPER_ASSERT_SUCCESS(muxer.on_publish(&req));
        HELPER_ASSERT_SUCCESS(muxer.update_config(&req));

        HELPER_ASSERT_SUCCESS(muxer.segment_open(0));
        ASSERT_TRUE(muxer.current_ != NULL);
        ISrsFileWriter *writer = muxer.writer_;
        int left_seq = muxer.current_->sequence_no_;
        string tmp = muxer.current_->tmppath();
        EXPECT_TRUE(path.exists(tmp));
        EXPECT_TRUE(writer->is_open());
        EXPECT_EQ(1, factory.live_);

        // The republish keeps the writer and the segment left open, which still writes.
        HELPER_ASSERT_SUCCESS(muxer.on_publish(&req));
        HELPER_ASSERT_SUCCESS(muxer.update_config(&req));
        EXPECT_EQ(1, factory.live_);
        EXPECT_TRUE(muxer.writer_ == writer);
        HELPER_ASSERT_SUCCESS(muxer.segment_open(0));
        ASSERT_TRUE(muxer.current_ != NULL);
        EXPECT_EQ(left_seq, muxer.current_->sequence_no_);
        EXPECT_TRUE(writer->is_open());

        MockSrsFormat fmt;
        MockSrsMediaPacket audio(false, 0);
        HELPER_EXPECT_SUCCESS(muxer.current_->write(&audio, &fmt));
        HELPER_EXPECT_SUCCESS(muxer.segment_close(100));
        EXPECT_TRUE(muxer.current_ == NULL);
        EXPECT_FALSE(writer->is_open());
        EXPECT_FALSE(path.exists(tmp));
        EXPECT_EQ(1, factory.live_);

        muxer.dispose();
    }

    EXPECT_EQ(0, factory.live_);
}

// The TS unpublish fails to write the last audio and leaves its segment open, and the dispose
// removes its temp file. The writer is closed before the temp file is removed, as Windows
// refuses to remove an open file.
VOID TEST(HlsMuxerWriterTest, DisposeClosesSegmentLeftOpen)
{
    srs_error_t err;

    MockAppFactoryFailingWriters factory;
    MockFmp4SrsRequest req;
    req.stream_ = "hls-dispose-left-open";

    SrsPath path;

    if (true) {
        SrsHlsController controller;
        SrsHlsMuxer *muxer = dynamic_cast<SrsHlsMuxer *>(controller.muxer_);
        muxer->app_factory_ = &factory;
        HELPER_ASSERT_SUCCESS(controller.initialize());

        HELPER_ASSERT_SUCCESS(controller.on_publish(&req));
        ASSERT_TRUE(muxer->is_segment_open());
        muxer->set_latest_acodec(SrsAudioCodecIdAAC);
        string left_tmp = muxer->current_->tmppath();
        EXPECT_TRUE(path.exists(left_tmp));

        controller.tsmc_->audio_ = new SrsTsMessage();
        controller.tsmc_->audio_->dts_ = 90000;
        controller.tsmc_->audio_->pts_ = 90000;
        controller.tsmc_->audio_->sid_ = SrsTsPESStreamIdAudioCommon;
        const char audio_data[] = {0x01, 0x02, 0x03, 0x04, 0x05};
        controller.tsmc_->audio_->payload_->append(audio_data, sizeof(audio_data));

        factory.fail_ = true;
        err = controller.on_unpublish();
        EXPECT_EQ(ERROR_SYSTEM_FILE_WRITE, srs_error_code(err));
        srs_freep(err);
        factory.fail_ = false;
        EXPECT_TRUE(muxer->is_segment_open());

        factory.closes_.clear();
        controller.dispose();
        EXPECT_FALSE(muxer->is_segment_open());
        EXPECT_FALSE(path.exists(left_tmp));
        ASSERT_EQ(1, (int)factory.closes_.size());
        EXPECT_EQ(left_tmp, factory.closes_[0].first);
        EXPECT_TRUE(factory.closes_[0].second);
        EXPECT_EQ(1, factory.live_);
    }

    EXPECT_EQ(0, factory.live_);
}

// The fmp4 unpublish fails to close its segment, which stays open, and the dispose removes its
// temp file. The writer is closed before the temp file is removed, as Windows refuses to remove
// an open file.
VOID TEST(HlsFmp4MuxerWriterTest, DisposeClosesSegmentLeftOpen)
{
    srs_error_t err;

    MockAppFactoryFailingWriters factory;
    MockFmp4SrsRequest req;
    req.stream_ = "fmp4-dispose-left-open";

    SrsPath path;

    if (true) {
        SrsHlsFmp4Muxer muxer;
        muxer.app_factory_ = &factory;
        HELPER_ASSERT_SUCCESS(muxer.initialize(1, 2));
        HELPER_ASSERT_SUCCESS(muxer.on_publish(&req));
        HELPER_ASSERT_SUCCESS(muxer.update_config(&req));

        HELPER_ASSERT_SUCCESS(muxer.segment_open(0));
        ASSERT_TRUE(muxer.current_ != NULL);
        string tmp = muxer.current_->tmppath();
        EXPECT_TRUE(path.exists(tmp));

        factory.closes_.clear();
        muxer.dispose();
        EXPECT_TRUE(muxer.current_ == NULL);
        EXPECT_FALSE(path.exists(tmp));
        ASSERT_EQ(1, (int)factory.closes_.size());
        EXPECT_EQ(tmp, factory.closes_[0].first);
        EXPECT_TRUE(factory.closes_[0].second);
        EXPECT_EQ(1, factory.live_);
    }

    EXPECT_EQ(0, factory.live_);
}

// The fmp4 publish fails to write the key of its first segment at the first frame. No segment is
// left open with no file, so the next frame opens a new segment, which writes its key and its
// file, and closes.
VOID TEST(HlsFmp4MuxerWriterTest, KeyWriteFailedNextFrameOpensNewSegment)
{
    srs_error_t err;

    // A dir at the path of the first segment's key, so that key can't be written.
    SrsPath path;
    string key_dir = "./objs/nginx/html/fmp4-key-retry-keys";
    HELPER_ASSERT_SUCCESS(path.mkdir_all(key_dir + "/live-fmp4-key-retry-0.key"));
    string key_file = key_dir + "/live-fmp4-key-retry-1.key";
    if (path.exists(key_file)) {
        HELPER_ASSERT_SUCCESS(path.unlink(key_file));
    }

    SrsSetEnvConfig(*_srs_config, hls_keys, "SRS_VHOST_HLS_HLS_KEYS", "on");
    SrsSetEnvConfig(*_srs_config, hls_fragments_per_key, "SRS_VHOST_HLS_HLS_FRAGMENTS_PER_KEY", "1");
    SrsSetEnvConfig(*_srs_config, hls_key_file, "SRS_VHOST_HLS_HLS_KEY_FILE", "[app]-[stream]-[seq].key");
    SrsSetEnvConfig(*_srs_config, hls_key_file_path, "SRS_VHOST_HLS_HLS_KEY_FILE_PATH", key_dir);

    MockAppFactoryLiveWriters factory;
    MockFmp4SrsRequest req;
    req.stream_ = "fmp4-key-retry";

    if (true) {
        SrsHlsFmp4Muxer muxer;
        muxer.app_factory_ = &factory;
        HELPER_ASSERT_SUCCESS(muxer.initialize(1, 2));
        HELPER_ASSERT_SUCCESS(muxer.on_publish(&req));
        HELPER_ASSERT_SUCCESS(muxer.update_config(&req));
        EXPECT_EQ(1, factory.live_);

        // The first frame can't open its segment, so no segment is open, and the sequence
        // header still waits for the segment that opens.
        HELPER_ASSERT_SUCCESS(muxer.on_sequence_header());
        MockSrsFormat fmt;
        MockSrsMediaPacket first(false, 0);
        HELPER_EXPECT_FAILED(muxer.write_audio(&first, &fmt));
        EXPECT_FALSE(muxer.is_segment_open());
        EXPECT_FALSE(muxer.writer_->is_open());
        EXPECT_TRUE(muxer.sequence_header_pending_);
        EXPECT_EQ(1, factory.live_);

        // The next frame opens a new segment, which writes its key and its file.
        MockSrsMediaPacket next(false, 20);
        HELPER_ASSERT_SUCCESS(muxer.write_audio(&next, &fmt));
        ASSERT_TRUE(muxer.is_segment_open());
        EXPECT_EQ(1, muxer.current_->sequence_no_);
        EXPECT_TRUE(muxer.current_->is_sequence_header());
        EXPECT_FALSE(muxer.sequence_header_pending_);
        EXPECT_TRUE(muxer.writer_->is_open());
        EXPECT_TRUE(path.exists(key_file));
        string tmp = muxer.current_->tmppath();
        string m4s = muxer.current_->fullpath();
        EXPECT_TRUE(path.exists(tmp));

        HELPER_EXPECT_SUCCESS(muxer.segment_close(100));
        EXPECT_FALSE(muxer.is_segment_open());
        EXPECT_FALSE(muxer.writer_->is_open());
        EXPECT_FALSE(path.exists(tmp));
        EXPECT_TRUE(path.exists(m4s));
        EXPECT_EQ(1, factory.live_);

        muxer.dispose();
    }

    EXPECT_EQ(0, factory.live_);

    HELPER_EXPECT_SUCCESS(path.unlink(key_file));
}

// The dispose removes the init file the muxer wrote, with the playlist and the segments.
VOID TEST(HlsFmp4MuxerDisposeTest, RemovesInitFile)
{
    srs_error_t err;

    SrsSetEnvConfig(*_srs_config, init_file, "SRS_VHOST_HLS_HLS_INIT_FILE", "[app]/[stream]-dispose-init.mp4");

    SrsHlsFmp4Muxer muxer;
    HELPER_ASSERT_SUCCESS(muxer.initialize(1, 2));

    MockFmp4SrsRequest req;
    HELPER_ASSERT_SUCCESS(muxer.on_publish(&req));
    HELPER_ASSERT_SUCCESS(muxer.update_config(&req));

    MockSrsFormat fmt;
    HELPER_ASSERT_SUCCESS(muxer.write_init_mp4(&fmt, true, true));

    SrsPath path;
    string init_file = muxer.hls_path_ + "/live/livestream-dispose-init.mp4";
    EXPECT_TRUE(path.exists(init_file));

    muxer.dispose();
    EXPECT_FALSE(path.exists(init_file));

    // A second dispose, with no init file written since, removes nothing and doesn't fail.
    muxer.dispose();
    EXPECT_FALSE(path.exists(init_file));
}

// A file writer that counts the closes of a file no longer at the path it opened, such as one
// renamed while open, which Windows refuses.
class MockFileWriterCloseInPlace : public SrsFileWriter
{
public:
    int *moved_;

    MockFileWriterCloseInPlace(int *moved) : moved_(moved) {}
    virtual ~MockFileWriterCloseInPlace() {}

    virtual void close()
    {
        if (is_open() && !SrsPath().exists(path_)) {
            (*moved_)++;
        }
        SrsFileWriter::close();
    }
};

// A factory whose file writers count the files closed after they were moved.
class MockAppFactoryCloseInPlace : public SrsAppFactory
{
public:
    int moved_;

    MockAppFactoryCloseInPlace() : moved_(0) {}
    virtual ~MockAppFactoryCloseInPlace() {}

    virtual ISrsFileWriter *create_file_writer() { return new MockFileWriterCloseInPlace(&moved_); }
};

// The init file is closed before it is renamed from its temp path, as Windows renames no open file.
VOID TEST(HlsFmp4MuxerInitTest, ClosedBeforeRename)
{
    srs_error_t err;

    SrsSetEnvConfig(*_srs_config, init_file, "SRS_VHOST_HLS_HLS_INIT_FILE", "[app]/[stream]-close-init.mp4");

    MockAppFactoryCloseInPlace factory;
    MockFmp4SrsRequest req;
    MockSrsFormat fmt;
    SrsPath path;

    SrsHlsFmp4Muxer muxer;
    muxer.app_factory_ = &factory;
    HELPER_ASSERT_SUCCESS(muxer.initialize(1, 2));
    HELPER_ASSERT_SUCCESS(muxer.on_publish(&req));
    HELPER_ASSERT_SUCCESS(muxer.update_config(&req));
    HELPER_ASSERT_SUCCESS(muxer.write_init_mp4(&fmt, true, true));

    string init_file = muxer.hls_path_ + "/live/livestream-close-init.mp4";
    EXPECT_TRUE(path.exists(init_file));
    EXPECT_FALSE(path.exists(init_file + ".tmp"));
    EXPECT_EQ(0, factory.moved_);

    muxer.dispose();
}

// A muxer that wrote no init file removes none at the dispose, even if a file is at the init
// file's path, such as one of another muxer.
VOID TEST(HlsFmp4MuxerDisposeTest, NoInitFileWrittenNoneRemoved)
{
    srs_error_t err;

    SrsSetEnvConfig(*_srs_config, init_file, "SRS_VHOST_HLS_HLS_INIT_FILE", "[app]/[stream]-dispose-none-init.mp4");

    MockFmp4SrsRequest req;
    MockSrsFormat fmt;

    SrsHlsFmp4Muxer writer;
    HELPER_ASSERT_SUCCESS(writer.initialize(1, 2));
    HELPER_ASSERT_SUCCESS(writer.on_publish(&req));
    HELPER_ASSERT_SUCCESS(writer.update_config(&req));
    HELPER_ASSERT_SUCCESS(writer.write_init_mp4(&fmt, true, true));

    SrsPath path;
    string init_file = writer.hls_path_ + "/live/livestream-dispose-none-init.mp4";
    EXPECT_TRUE(path.exists(init_file));

    SrsHlsFmp4Muxer muxer;
    HELPER_ASSERT_SUCCESS(muxer.initialize(1, 2));
    HELPER_ASSERT_SUCCESS(muxer.on_publish(&req));
    HELPER_ASSERT_SUCCESS(muxer.update_config(&req));
    muxer.dispose();
    EXPECT_TRUE(path.exists(init_file));

    writer.dispose();
    EXPECT_FALSE(path.exists(init_file));
}

// The init file is written before the first key, so the default KID of its tenc box is zero,
// whatever the memory the muxer was made in held.
VOID TEST(HlsFmp4MuxerKeysTest, InitFileKidIsZero)
{
    srs_error_t err;

    SrsSetEnvConfig(*_srs_config, init_file, "SRS_VHOST_HLS_HLS_INIT_FILE", "[app]/[stream]-kid-init.mp4");

    MockFmp4SrsRequest req;
    MockSrsFormat fmt;
    SrsPath path;
    string init_file;

    // Make the muxer in memory that is not zero, as a heap block reused after another object.
    union {
        char bytes[sizeof(SrsHlsFmp4Muxer)];
        double align;
    } storage;
    memset(storage.bytes, 0xab, sizeof(storage.bytes));
    SrsHlsFmp4Muxer *muxer = new (storage.bytes) SrsHlsFmp4Muxer();

    if (true) {
        HELPER_ASSERT_SUCCESS(muxer->initialize(1, 2));
        HELPER_ASSERT_SUCCESS(muxer->on_publish(&req));
        HELPER_ASSERT_SUCCESS(muxer->update_config(&req));
        muxer->hls_keys_ = true;
        HELPER_ASSERT_SUCCESS(muxer->write_init_mp4(&fmt, true, true));
        init_file = muxer->hls_path_ + "/live/livestream-kid-init.mp4";
    }

    std::ifstream ifs(init_file.c_str(), std::ios::binary);
    std::stringstream ss;
    ss << ifs.rdbuf();
    ifs.close(); // Windows cannot remove a file that is still open.
    std::string data = ss.str();

    muxer->dispose();
    muxer->~SrsHlsFmp4Muxer();
    EXPECT_FALSE(path.exists(init_file));

    // The tenc box: type, version and flags, two reserved or pattern bytes, the protected flag
    // and the IV size, then the 16 bytes of the default KID. One per track.
    int tencs = 0;
    for (size_t pos = data.find("tenc"); pos != std::string::npos; pos = data.find("tenc", pos + 4)) {
        ASSERT_LE(pos + 12 + 16, data.size());
        std::string kid = data.substr(pos + 12, 16);
        EXPECT_EQ(std::string(16, '\0'), kid);
        tencs++;
    }
    EXPECT_EQ(2, tencs);
}
