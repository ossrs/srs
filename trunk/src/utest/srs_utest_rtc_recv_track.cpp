//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//
#include "srs_utest_rtc_recv_track.hpp"

using namespace std;

class SrsRtcRecvTrack;

class SrsRtcRecvTrackForTest : public SrsRtcAudioRecvTrack {
public:
    void receiver_insert(uint16_t first, uint16_t last) {
        nack_receiver_->insert(first, last);
    }

    SrsRtcRecvTrackForTest(SrsRtcConnection *session,
                            SrsRtcTrackDescription *track_desc)
        : SrsRtcAudioRecvTrack(session, track_desc) {}
};

VOID TEST(RtcRecvTrackTest, OnNackTest) {
    SrsRtcTrackDescription track_desc;
    SrsRtcRecvTrackForTest recv_track(nullptr, &track_desc);

    // Create a test RTP packet
    SrsRtpPacket pkt;
    pkt.header.set_sequence(100);

    SrsRtpPacket *ppkt = &pkt;

    // Test case 1: NACK info does not exist
    srs_error_t err = recv_track.on_nack(&ppkt);
    EXPECT_TRUE(srs_error_code(err) == ERROR_SUCCESS);

    // Test case 2: NACK info exists
    recv_track.receiver_insert(100, 100);
    err = recv_track.on_nack(&ppkt);
    EXPECT_TRUE(srs_error_code(err) == ERROR_SUCCESS);
}