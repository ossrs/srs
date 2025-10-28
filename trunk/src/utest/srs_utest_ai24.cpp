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
