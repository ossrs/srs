//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//
#include "srs_utest_manual_rtc_recv_track.hpp"

#include <srs_app_rtc_conn.hpp>
#include <srs_app_rtc_dtls.hpp>
#include <srs_app_rtc_source.hpp>
#include <srs_kernel_error.hpp>

using namespace std;

class SrsRtcRecvTrack;

class MockSrsRtcRecvTrack : public SrsRtcRecvTrack
{
private:
    static SrsRtcTrackDescription *create_default_track_desc()
    {
        SrsRtcTrackDescription *desc = new SrsRtcTrackDescription();
        // Initialize with minimal required values
        desc->type_ = "audio";
        desc->id_ = "test_track";
        desc->ssrc_ = 12345;
        desc->is_active_ = true;
        return desc;
    }

public:
    void receiver_insert(uint16_t first, uint16_t last)
    {
        nack_receiver_->insert(first, last);
    }

    void receiver_remove(uint16_t seq)
    {
        nack_receiver_->remove(seq);
    }

    SrsRtpNackInfo *receiver_find(uint16_t seq)
    {
        return nack_receiver_->find(seq);
    }

    void set_nack_no_copy_for_test(bool v)
    {
        set_nack_no_copy(v);
    }

    SrsRtpPacket *get_packet_from_queue(uint16_t seq)
    {
        return rtp_queue_->at(seq);
    }

    bool is_queue_empty()
    {
        return rtp_queue_->empty();
    }

    int get_queue_size()
    {
        return rtp_queue_->size();
    }

    // Implement pure virtual methods from SrsRtcRecvTrack
    virtual srs_error_t on_rtp(SrsSharedPtr<SrsRtcSource> &source, SrsRtpPacket *pkt)
    {
        // Mock implementation - just return success
        return srs_success;
    }

    virtual srs_error_t check_send_nacks()
    {
        // Mock implementation - just return success
        return srs_success;
    }

    MockSrsRtcRecvTrack()
        : SrsRtcRecvTrack(nullptr, create_default_track_desc(), true, false) {} // true for is_audio, false for init_rate_from_sdp
};

VOID TEST(RtcRecvTrackTest, OnNackBasicTest)
{
    srs_error_t err;
    MockSrsRtcRecvTrack recv_track;

    // Create a test RTP packet
    SrsRtpPacket pkt;
    pkt.header_.set_sequence(100);

    SrsRtpPacket *ppkt = &pkt;

    // Test case 1: NACK info does not exist (new packet)
    HELPER_EXPECT_SUCCESS(recv_track.on_nack(&ppkt));

    // Test case 2: NACK info exists (recovered packet)
    recv_track.receiver_insert(101, 102);
    SrsRtpPacket pkt2;
    pkt2.header_.set_sequence(101);
    ppkt = &pkt2;
    HELPER_EXPECT_SUCCESS(recv_track.on_nack(&ppkt));
}

VOID TEST(RtcRecvTrackTest, OnNackRecoveredPacketTest)
{
    srs_error_t err;
    MockSrsRtcRecvTrack recv_track;

    // Insert a sequence into NACK receiver (simulating lost packet)
    recv_track.receiver_insert(200, 201);

    // Verify the packet is in NACK receiver
    EXPECT_TRUE(recv_track.receiver_find(200) != NULL);

    // Create recovered packet
    SrsRtpPacket pkt;
    pkt.header_.set_sequence(200);
    SrsRtpPacket *ppkt = &pkt;

    // Process the recovered packet
    HELPER_EXPECT_SUCCESS(recv_track.on_nack(&ppkt));

    // Verify the packet was removed from NACK receiver
    EXPECT_TRUE(recv_track.receiver_find(200) == NULL);

    // Verify the packet was added to RTP queue
    SrsRtpPacket *queued_pkt = recv_track.get_packet_from_queue(200);
    EXPECT_TRUE(queued_pkt != NULL);
    EXPECT_TRUE(queued_pkt->header_.get_sequence() == 200);
}

VOID TEST(RtcRecvTrackTest, OnNackSequentialPacketsTest)
{
    srs_error_t err;
    MockSrsRtcRecvTrack recv_track;

    // Process sequential packets
    for (uint16_t seq = 300; seq < 305; seq++) {
        SrsRtpPacket pkt;
        pkt.header_.set_sequence(seq);
        SrsRtpPacket *ppkt = &pkt;

        HELPER_EXPECT_SUCCESS(recv_track.on_nack(&ppkt));

        // Verify packet is in queue
        SrsRtpPacket *queued_pkt = recv_track.get_packet_from_queue(seq);
        EXPECT_TRUE(queued_pkt != NULL);
        EXPECT_TRUE(queued_pkt->header_.get_sequence() == seq);
    }
}

VOID TEST(RtcRecvTrackTest, OnNackOutOfOrderPacketsTest)
{
    srs_error_t err;
    MockSrsRtcRecvTrack recv_track;

    // Process packets out of order: 400, 402, 401
    uint16_t sequences[] = {400, 402, 401};

    for (int i = 0; i < 3; i++) {
        SrsRtpPacket pkt;
        pkt.header_.set_sequence(sequences[i]);
        SrsRtpPacket *ppkt = &pkt;

        HELPER_EXPECT_SUCCESS(recv_track.on_nack(&ppkt));

        // Verify packet is in queue
        SrsRtpPacket *queued_pkt = recv_track.get_packet_from_queue(sequences[i]);
        EXPECT_TRUE(queued_pkt != NULL);
        EXPECT_TRUE(queued_pkt->header_.get_sequence() == sequences[i]);
    }
}

VOID TEST(RtcRecvTrackTest, OnNackNoCopyModeTest)
{
    srs_error_t err;
    MockSrsRtcRecvTrack recv_track;

    // Enable no-copy mode
    recv_track.set_nack_no_copy_for_test(true);

    // Create a test RTP packet
    SrsRtpPacket *pkt = new SrsRtpPacket();
    pkt->header_.set_sequence(500);
    SrsRtpPacket *ppkt = pkt;

    // Process packet in no-copy mode
    HELPER_EXPECT_SUCCESS(recv_track.on_nack(&ppkt));

    // In no-copy mode, ppkt should be set to NULL
    EXPECT_TRUE(ppkt == NULL);

    // Verify packet is in queue (original packet, not a copy)
    SrsRtpPacket *queued_pkt = recv_track.get_packet_from_queue(500);
    EXPECT_TRUE(queued_pkt != NULL);
    EXPECT_TRUE(queued_pkt->header_.get_sequence() == 500);
    EXPECT_TRUE(queued_pkt == pkt); // Should be the same object
}

VOID TEST(RtcRecvTrackTest, OnNackCopyModeTest)
{
    srs_error_t err;
    MockSrsRtcRecvTrack recv_track;

    // Ensure copy mode is enabled (default)
    recv_track.set_nack_no_copy_for_test(false);

    // Create a test RTP packet
    SrsRtpPacket pkt;
    pkt.header_.set_sequence(600);
    SrsRtpPacket *ppkt = &pkt;

    // Process packet in copy mode
    HELPER_EXPECT_SUCCESS(recv_track.on_nack(&ppkt));

    // In copy mode, ppkt should remain unchanged
    EXPECT_TRUE(ppkt == &pkt);

    // Verify packet is in queue (should be a copy)
    SrsRtpPacket *queued_pkt = recv_track.get_packet_from_queue(600);
    EXPECT_TRUE(queued_pkt != NULL);
    EXPECT_TRUE(queued_pkt->header_.get_sequence() == 600);
    EXPECT_TRUE(queued_pkt != &pkt); // Should be a different object (copy)
}

VOID TEST(RtcRecvTrackTest, OnNackSequenceWrapAroundTest)
{
    srs_error_t err;
    MockSrsRtcRecvTrack recv_track;

    // Test sequence number wrap-around (65535 -> 0)
    uint16_t sequences[] = {65534, 65535, 0, 1, 2};

    for (int i = 0; i < 5; i++) {
        SrsRtpPacket pkt;
        pkt.header_.set_sequence(sequences[i]);
        SrsRtpPacket *ppkt = &pkt;

        HELPER_EXPECT_SUCCESS(recv_track.on_nack(&ppkt));

        // Verify packet is in queue
        SrsRtpPacket *queued_pkt = recv_track.get_packet_from_queue(sequences[i]);
        EXPECT_TRUE(queued_pkt != NULL);
        EXPECT_TRUE(queued_pkt->header_.get_sequence() == sequences[i]);
    }
}

VOID TEST(RtcRecvTrackTest, OnNackMixedRecoveredAndNewPacketsTest)
{
    srs_error_t err;
    MockSrsRtcRecvTrack recv_track;

    // Insert some sequences into NACK receiver (simulating lost packets)
    recv_track.receiver_insert(700, 702); // 700, 701 are "lost"

    // Verify packets are in NACK receiver
    EXPECT_TRUE(recv_track.receiver_find(700) != NULL);
    EXPECT_TRUE(recv_track.receiver_find(701) != NULL);

    // Process mixed sequence: new packet (699), recovered packet (700), new packet (702), recovered packet (701)
    uint16_t sequences[] = {699, 700, 702, 701};
    bool should_be_in_nack[] = {false, true, false, true};

    for (int i = 0; i < 4; i++) {
        SrsRtpPacket pkt;
        pkt.header_.set_sequence(sequences[i]);
        SrsRtpPacket *ppkt = &pkt;

        // Check if packet is in NACK receiver before processing
        bool was_in_nack = (recv_track.receiver_find(sequences[i]) != NULL);
        EXPECT_TRUE(was_in_nack == should_be_in_nack[i]);

        HELPER_EXPECT_SUCCESS(recv_track.on_nack(&ppkt));

        // Verify packet was removed from NACK receiver if it was there
        if (was_in_nack) {
            EXPECT_TRUE(recv_track.receiver_find(sequences[i]) == NULL);
        }

        // Verify packet is in queue
        SrsRtpPacket *queued_pkt = recv_track.get_packet_from_queue(sequences[i]);
        EXPECT_TRUE(queued_pkt != NULL);
        EXPECT_TRUE(queued_pkt->header_.get_sequence() == sequences[i]);
    }
}

VOID TEST(RtcRecvTrackTest, OnNackDuplicatePacketTest)
{
    srs_error_t err;
    MockSrsRtcRecvTrack recv_track;

    // Process the same packet twice
    uint16_t seq = 800;

    // First time processing
    SrsRtpPacket pkt1;
    pkt1.header_.set_sequence(seq);
    SrsRtpPacket *ppkt1 = &pkt1;
    HELPER_EXPECT_SUCCESS(recv_track.on_nack(&ppkt1));

    // Verify packet is in queue
    SrsRtpPacket *queued_pkt1 = recv_track.get_packet_from_queue(seq);
    EXPECT_TRUE(queued_pkt1 != NULL);
    EXPECT_TRUE(queued_pkt1->header_.get_sequence() == seq);

    // Second time processing (duplicate)
    SrsRtpPacket pkt2;
    pkt2.header_.set_sequence(seq);
    SrsRtpPacket *ppkt2 = &pkt2;
    HELPER_EXPECT_SUCCESS(recv_track.on_nack(&ppkt2));

    // Verify the packet in queue is replaced (should be the new one)
    SrsRtpPacket *queued_pkt2 = recv_track.get_packet_from_queue(seq);
    EXPECT_TRUE(queued_pkt2 != NULL);
    EXPECT_TRUE(queued_pkt2->header_.get_sequence() == seq);
    // In copy mode, it should be a different object
    EXPECT_TRUE(queued_pkt2 != queued_pkt1);
}

VOID TEST(RtcRecvTrackTest, OnNackLargeGapTest)
{
    srs_error_t err;
    MockSrsRtcRecvTrack recv_track;

    // Process packets with large gap to test NACK range handling
    uint16_t seq1 = 900;
    uint16_t seq2 = 950; // Gap of 50 packets

    // Process first packet
    SrsRtpPacket pkt1;
    pkt1.header_.set_sequence(seq1);
    SrsRtpPacket *ppkt1 = &pkt1;
    HELPER_EXPECT_SUCCESS(recv_track.on_nack(&ppkt1));

    // Process second packet with large gap
    SrsRtpPacket pkt2;
    pkt2.header_.set_sequence(seq2);
    SrsRtpPacket *ppkt2 = &pkt2;
    HELPER_EXPECT_SUCCESS(recv_track.on_nack(&ppkt2));

    // Verify both packets are in queue
    SrsRtpPacket *queued_pkt1 = recv_track.get_packet_from_queue(seq1);
    EXPECT_TRUE(queued_pkt1 != NULL);
    EXPECT_TRUE(queued_pkt1->header_.get_sequence() == seq1);

    SrsRtpPacket *queued_pkt2 = recv_track.get_packet_from_queue(seq2);
    EXPECT_TRUE(queued_pkt2 != NULL);
    EXPECT_TRUE(queued_pkt2->header_.get_sequence() == seq2);
}

VOID TEST(RtcRecvTrackTest, OnNackRecoveredPacketRemovedFromNackTest)
{
    srs_error_t err;
    MockSrsRtcRecvTrack recv_track;

    // Insert multiple sequences into NACK receiver
    recv_track.receiver_insert(1000, 1005); // 1000, 1001, 1002, 1003, 1004

    // Verify all packets are in NACK receiver
    for (uint16_t seq = 1000; seq < 1005; seq++) {
        EXPECT_TRUE(recv_track.receiver_find(seq) != NULL);
    }

    // Recover some packets (1001, 1003)
    uint16_t recovered_seqs[] = {1001, 1003};
    for (int i = 0; i < 2; i++) {
        SrsRtpPacket pkt;
        pkt.header_.set_sequence(recovered_seqs[i]);
        SrsRtpPacket *ppkt = &pkt;

        HELPER_EXPECT_SUCCESS(recv_track.on_nack(&ppkt));

        // Verify recovered packet is removed from NACK receiver
        EXPECT_TRUE(recv_track.receiver_find(recovered_seqs[i]) == NULL);

        // Verify packet is in queue
        SrsRtpPacket *queued_pkt = recv_track.get_packet_from_queue(recovered_seqs[i]);
        EXPECT_TRUE(queued_pkt != NULL);
        EXPECT_TRUE(queued_pkt->header_.get_sequence() == recovered_seqs[i]);
    }

    // Verify non-recovered packets are still in NACK receiver
    uint16_t non_recovered_seqs[] = {1000, 1002, 1004};
    for (int i = 0; i < 3; i++) {
        EXPECT_TRUE(recv_track.receiver_find(non_recovered_seqs[i]) != NULL);
    }
}

VOID TEST(RtcRecvTrackTest, OnNackBugFixVerificationTest)
{
    srs_error_t err;
    MockSrsRtcRecvTrack recv_track;

    // This test specifically verifies the bug fix from PR #4467
    // The bug was that recovered packets were not being added to the RTP queue

    // Step 1: Simulate lost packet by adding to NACK receiver
    uint16_t lost_seq = 1100;
    recv_track.receiver_insert(lost_seq, lost_seq + 1);

    // Verify packet is in NACK receiver (lost)
    EXPECT_TRUE(recv_track.receiver_find(lost_seq) != NULL);

    // Step 2: Simulate packet recovery (retransmission arrives)
    SrsRtpPacket recovered_pkt;
    recovered_pkt.header_.set_sequence(lost_seq);
    SrsRtpPacket *ppkt = &recovered_pkt;

    // Step 3: Process the recovered packet
    HELPER_EXPECT_SUCCESS(recv_track.on_nack(&ppkt));

    // Step 4: Verify the fix - packet should be removed from NACK receiver
    EXPECT_TRUE(recv_track.receiver_find(lost_seq) == NULL);

    // Step 5: Verify the fix - packet should be added to RTP queue (this was the bug)
    SrsRtpPacket *queued_pkt = recv_track.get_packet_from_queue(lost_seq);
    EXPECT_TRUE(queued_pkt != NULL);
    EXPECT_TRUE(queued_pkt->header_.get_sequence() == lost_seq);

    // This test ensures that recovered packets are properly processed and not discarded
}

VOID TEST(RtcRecvTrackTest, OnNackControlFlowTest)
{
    srs_error_t err;
    MockSrsRtcRecvTrack recv_track;

    // Test the control flow differences between recovered and new packets

    // Scenario 1: New packet (not in NACK receiver)
    uint16_t new_seq = 1200;
    SrsRtpPacket new_pkt;
    new_pkt.header_.set_sequence(new_seq);
    SrsRtpPacket *new_ppkt = &new_pkt;

    // Should not be in NACK receiver initially
    EXPECT_TRUE(recv_track.receiver_find(new_seq) == NULL);

    HELPER_EXPECT_SUCCESS(recv_track.on_nack(&new_ppkt));

    // New packet should be added to queue
    SrsRtpPacket *new_queued = recv_track.get_packet_from_queue(new_seq);
    EXPECT_TRUE(new_queued != NULL);
    EXPECT_TRUE(new_queued->header_.get_sequence() == new_seq);

    // Scenario 2: Recovered packet (in NACK receiver)
    uint16_t recovered_seq = 1201;
    recv_track.receiver_insert(recovered_seq, recovered_seq + 1);

    // Should be in NACK receiver initially
    EXPECT_TRUE(recv_track.receiver_find(recovered_seq) != NULL);

    SrsRtpPacket recovered_pkt;
    recovered_pkt.header_.set_sequence(recovered_seq);
    SrsRtpPacket *recovered_ppkt = &recovered_pkt;

    HELPER_EXPECT_SUCCESS(recv_track.on_nack(&recovered_ppkt));

    // Recovered packet should be removed from NACK receiver
    EXPECT_TRUE(recv_track.receiver_find(recovered_seq) == NULL);

    // Recovered packet should also be added to queue (the fix)
    SrsRtpPacket *recovered_queued = recv_track.get_packet_from_queue(recovered_seq);
    EXPECT_TRUE(recovered_queued != NULL);
    EXPECT_TRUE(recovered_queued->header_.get_sequence() == recovered_seq);
}

VOID TEST(RtcRecvTrackTest, OnNackStressTest)
{
    srs_error_t err;
    MockSrsRtcRecvTrack recv_track;

    // Stress test with many packets, mixed recovered and new
    const int num_packets = 100;
    const uint16_t base_seq = 2000;

    // Insert every other packet into NACK receiver (simulate 50% loss)
    for (int i = 0; i < num_packets; i += 2) {
        uint16_t seq = base_seq + i;
        recv_track.receiver_insert(seq, seq + 1);
    }

    // Process all packets
    for (int i = 0; i < num_packets; i++) {
        uint16_t seq = base_seq + i;
        bool should_be_recovered = (i % 2 == 0);

        // Verify initial state
        bool is_in_nack = (recv_track.receiver_find(seq) != NULL);
        EXPECT_TRUE(is_in_nack == should_be_recovered);

        SrsRtpPacket pkt;
        pkt.header_.set_sequence(seq);
        SrsRtpPacket *ppkt = &pkt;

        HELPER_EXPECT_SUCCESS(recv_track.on_nack(&ppkt));

        // Verify packet was removed from NACK receiver if it was there
        EXPECT_TRUE(recv_track.receiver_find(seq) == NULL);

        // Verify packet is in queue (the key fix)
        SrsRtpPacket *queued_pkt = recv_track.get_packet_from_queue(seq);
        EXPECT_TRUE(queued_pkt != NULL);
        EXPECT_TRUE(queued_pkt->header_.get_sequence() == seq);
    }
}
