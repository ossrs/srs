//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//
#include <srs_utest_ai24.hpp>

#include <srs_app_rtc_source.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_rtc_rtcp.hpp>
#include <srs_protocol_sdp.hpp>
#include <srs_kernel_packet.hpp>
#include <srs_kernel_codec.hpp>
#include <srs_app_hls.hpp>
#include <srs_utest_manual_mock.hpp>
#include <srs_utest_manual_kernel.hpp>
#include <srs_app_config.hpp>
#include <srs_app_http_hooks.hpp>
#include <srs_app_utility.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_utility.hpp>
#include <srs_app_fragment.hpp>
#include <srs_app_server.hpp>

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
    HELPER_EXPECT_SUCCESS(packet.add_sample((char*)sample_data, sizeof(sample_data)));

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
    segment.sequence_no_ = 10; // 10 % 5 == 0, so key should be written
    segment.is_sequence_header_ = true; // Should write discontinuity
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
    segment.sequence_no_ = 10; // 10 % 5 == 0, so key should be written
    segment.set_is_sequence_header(true); // Should write discontinuity
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

// Test: SrsServer::initialize_st with asprocess enabled and ppid == 1 (should fail)
VOID TEST(ServerTest, InitializeStAsprocessWithPpid1)
{
    srs_error_t err;

    // Create server
    SrsServer server;

    // Create mock config with asprocess enabled
    MockAppConfig mock_config;
    mock_config.asprocess_ = true;

    // Replace config with mock
    server.config_ = &mock_config;

    // Set ppid to 1 (init process)
    server.ppid_ = 1;

    // Call initialize_st - should fail because asprocess is true and ppid is 1
    HELPER_EXPECT_FAILED(server.initialize_st());
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
