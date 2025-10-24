//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_utest_ai04.hpp>

#include <srs_app_rtc_source.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_rtc_rtp.hpp>
#include <srs_protocol_st.hpp>
#include <srs_utest_manual_kernel.hpp>

// Helper function to create a mock RTP packet for testing
SrsRtpPacket *mock_create_audio_rtp_packet(uint16_t sequence, uint32_t timestamp, const char *payload_data = NULL, int payload_size = 10)
{
    SrsRtpPacket *pkt = new SrsRtpPacket();

    // Set RTP header
    pkt->header_.set_padding(false);
    pkt->header_.set_marker(false);
    pkt->header_.set_payload_type(111); // Audio payload type
    pkt->header_.set_sequence(sequence);
    pkt->header_.set_timestamp(timestamp);
    pkt->header_.set_ssrc(0x12345678);

    // For audio cache testing, we don't need actual payload data or avsync time
    // The cache is only concerned with sequence numbers and system time for jitter buffer logic
    // We're not testing the downstream transcoding, just the cache reordering behavior

    // Set frame type for audio
    pkt->frame_type_ = SrsFrameTypeAudio;

    return pkt;
}

// Helper function to free a vector of RTP packets
void free_audio_packets(std::vector<SrsRtpPacket *> &packets)
{
    for (size_t i = 0; i < packets.size(); ++i) {
        srs_freep(packets[i]);
    }
    packets.clear();
}

VOID TEST(RTC3AudioCacheTest, BasicPacketProcessing)
{
    srs_error_t err;

    // Test basic packet processing in order
    if (true) {
        SrsRtcFrameBuilderAudioPacketCache cache;
        std::vector<SrsRtpPacket *> ready_packets;

        // Process first packet
        SrsUniquePtr<SrsRtpPacket> pkt1(mock_create_audio_rtp_packet(100, 1000));
        HELPER_EXPECT_SUCCESS(cache.process_packet(pkt1.get(), ready_packets));

        // First packet should be processed immediately
        EXPECT_EQ(1, (int)ready_packets.size());
        EXPECT_EQ(100, ready_packets[0]->header_.get_sequence());

        free_audio_packets(ready_packets);

        // Process second packet in sequence
        SrsUniquePtr<SrsRtpPacket> pkt2(mock_create_audio_rtp_packet(101, 1020));
        HELPER_EXPECT_SUCCESS(cache.process_packet(pkt2.get(), ready_packets));

        // Second packet should be processed immediately
        EXPECT_EQ(1, (int)ready_packets.size());
        EXPECT_EQ(101, ready_packets[0]->header_.get_sequence());

        free_audio_packets(ready_packets);
    }
}

VOID TEST(RTC3AudioCacheTest, OutOfOrderPackets)
{
    srs_error_t err;

    // Test out-of-order packet handling
    if (true) {
        SrsRtcFrameBuilderAudioPacketCache cache;
        std::vector<SrsRtpPacket *> ready_packets;

        // Process first packet 100 to initialize
        SrsUniquePtr<SrsRtpPacket> pkt1(mock_create_audio_rtp_packet(100, 1000));
        HELPER_EXPECT_SUCCESS(cache.process_packet(pkt1.get(), ready_packets));
        EXPECT_EQ(1, (int)ready_packets.size());
        free_audio_packets(ready_packets);

        // Process packet 103 (out of order - missing 101, 102)
        SrsUniquePtr<SrsRtpPacket> pkt3(mock_create_audio_rtp_packet(103, 1060));
        HELPER_EXPECT_SUCCESS(cache.process_packet(pkt3.get(), ready_packets));

        // Should not process yet, waiting for missing packets
        EXPECT_EQ(0, (int)ready_packets.size());

        // Process packet 101 (fills gap)
        SrsUniquePtr<SrsRtpPacket> pkt2(mock_create_audio_rtp_packet(101, 1020));
        HELPER_EXPECT_SUCCESS(cache.process_packet(pkt2.get(), ready_packets));

        // Should process packet 101 now
        EXPECT_EQ(1, (int)ready_packets.size());
        EXPECT_EQ(101, ready_packets[0]->header_.get_sequence());
        free_audio_packets(ready_packets);

        // Process packet 102 (completes sequence)
        SrsUniquePtr<SrsRtpPacket> pkt4(mock_create_audio_rtp_packet(102, 1040));
        HELPER_EXPECT_SUCCESS(cache.process_packet(pkt4.get(), ready_packets));

        // Should process both 102 and 103
        EXPECT_EQ(2, (int)ready_packets.size());
        EXPECT_EQ(102, ready_packets[0]->header_.get_sequence());
        EXPECT_EQ(103, ready_packets[1]->header_.get_sequence());

        free_audio_packets(ready_packets);
    }
}

VOID TEST(RTC3AudioCacheTest, LatePacketHandling)
{
    srs_error_t err;

    // Test late packet detection and discard
    if (true) {
        SrsRtcFrameBuilderAudioPacketCache cache;
        std::vector<SrsRtpPacket *> ready_packets;

        // Process packets 100, 101, 102 in order
        for (uint16_t seq = 100; seq <= 102; seq++) {
            SrsUniquePtr<SrsRtpPacket> pkt(mock_create_audio_rtp_packet(seq, 1000 + (seq - 100) * 20));
            HELPER_EXPECT_SUCCESS(cache.process_packet(pkt.get(), ready_packets));
            EXPECT_EQ(1, (int)ready_packets.size());
            EXPECT_EQ(seq, ready_packets[0]->header_.get_sequence());
            free_audio_packets(ready_packets);
        }

        // Try to process packet 99 (late packet - already processed)
        SrsUniquePtr<SrsRtpPacket> late_pkt(mock_create_audio_rtp_packet(99, 980));
        HELPER_EXPECT_SUCCESS(cache.process_packet(late_pkt.get(), ready_packets));

        // Late packet should be discarded, no packets ready
        EXPECT_EQ(0, (int)ready_packets.size());
    }
}

VOID TEST(RTC3AudioCacheTest, SequenceNumberWrapAround)
{
    srs_error_t err;

    // Test sequence number wrap-around (16-bit overflow)
    if (true) {
        SrsRtcFrameBuilderAudioPacketCache cache;
        std::vector<SrsRtpPacket *> ready_packets;

        // Process packets near the 16-bit boundary
        uint16_t seq_near_max = 65534;
        SrsUniquePtr<SrsRtpPacket> pkt1(mock_create_audio_rtp_packet(seq_near_max, 1000));
        HELPER_EXPECT_SUCCESS(cache.process_packet(pkt1.get(), ready_packets));
        EXPECT_EQ(1, (int)ready_packets.size());
        EXPECT_EQ(seq_near_max, ready_packets[0]->header_.get_sequence());
        free_audio_packets(ready_packets);

        // Process packet 65535
        SrsUniquePtr<SrsRtpPacket> pkt2(mock_create_audio_rtp_packet(65535, 1020));
        HELPER_EXPECT_SUCCESS(cache.process_packet(pkt2.get(), ready_packets));
        EXPECT_EQ(1, (int)ready_packets.size());
        EXPECT_EQ(65535, ready_packets[0]->header_.get_sequence());
        free_audio_packets(ready_packets);

        // Process packet 0 (after wrap-around)
        SrsUniquePtr<SrsRtpPacket> pkt3(mock_create_audio_rtp_packet(0, 1040));
        HELPER_EXPECT_SUCCESS(cache.process_packet(pkt3.get(), ready_packets));
        EXPECT_EQ(1, (int)ready_packets.size());
        EXPECT_EQ(0, ready_packets[0]->header_.get_sequence());
        free_audio_packets(ready_packets);

        // Process packet 1
        SrsUniquePtr<SrsRtpPacket> pkt4(mock_create_audio_rtp_packet(1, 1060));
        HELPER_EXPECT_SUCCESS(cache.process_packet(pkt4.get(), ready_packets));
        EXPECT_EQ(1, (int)ready_packets.size());
        EXPECT_EQ(1, ready_packets[0]->header_.get_sequence());
        free_audio_packets(ready_packets);
    }
}

VOID TEST(RTC3AudioCacheTest, PacketLossWithTimeout)
{
    srs_error_t err;

    // Test packet loss handling with timeout
    if (true) {
        SrsRtcFrameBuilderAudioPacketCache cache;
        std::vector<SrsRtpPacket *> ready_packets;

        // Set a very short timeout for testing (1ms)
        cache.set_timeout(1 * SRS_UTIME_MILLISECONDS);

        // Process first packet to initialize
        SrsUniquePtr<SrsRtpPacket> pkt1(mock_create_audio_rtp_packet(100, 1000));
        HELPER_EXPECT_SUCCESS(cache.process_packet(pkt1.get(), ready_packets));
        EXPECT_EQ(1, (int)ready_packets.size());
        free_audio_packets(ready_packets);

        // Process packet 103 (missing 101, 102)
        SrsUniquePtr<SrsRtpPacket> pkt3(mock_create_audio_rtp_packet(103, 1060));
        HELPER_EXPECT_SUCCESS(cache.process_packet(pkt3.get(), ready_packets));

        // Should not process yet, waiting for missing packets
        EXPECT_EQ(0, (int)ready_packets.size());

        bool found_103 = false;
        for (int i = 0; i < 10 && !found_103; i++) {
            // Sleep for N ms to exceed the 1ms timeout
            srs_usleep(3 * SRS_UTIME_MILLISECONDS);

            // Process another packet to trigger timeout check
            SrsUniquePtr<SrsRtpPacket> pkt4(mock_create_audio_rtp_packet(104, 1080));
            HELPER_EXPECT_SUCCESS(cache.process_packet(pkt4.get(), ready_packets));

            // Should process packet 103 despite missing 101, 102 due to timeout
            for (size_t i = 0; i < ready_packets.size(); i++) {
                if (ready_packets[i]->header_.get_sequence() == 103) {
                    found_103 = true;
                    break;
                }
            }
        }
        EXPECT_TRUE(found_103);
        free_audio_packets(ready_packets);
    }
}

VOID TEST(RTC3AudioCacheTest, BufferOverflowProtection)
{
    srs_error_t err;

    // Test buffer overflow protection
    if (true) {
        SrsRtcFrameBuilderAudioPacketCache cache;
        std::vector<SrsRtpPacket *> ready_packets;

        // Process first packet to initialize
        SrsUniquePtr<SrsRtpPacket> pkt1(mock_create_audio_rtp_packet(100, 1000));
        HELPER_EXPECT_SUCCESS(cache.process_packet(pkt1.get(), ready_packets));
        EXPECT_EQ(1, (int)ready_packets.size());
        free_audio_packets(ready_packets);

        // Fill buffer with out-of-order packets to trigger overflow protection
        // Add packets with large gaps to fill the buffer
        for (uint16_t i = 0; i < AUDIO_JITTER_BUFFER_SIZE + 10; i++) {
            uint16_t seq = 200 + i * 2; // Create gaps to avoid immediate processing
            SrsUniquePtr<SrsRtpPacket> pkt(mock_create_audio_rtp_packet(seq, 2000 + i * 20));
            HELPER_EXPECT_SUCCESS(cache.process_packet(pkt.get(), ready_packets));
        }

        // Buffer overflow protection should have kicked in and processed some packets
        EXPECT_GT((int)ready_packets.size(), 0);
        free_audio_packets(ready_packets);
    }
}

VOID TEST(RTC3AudioCacheTest, ClearAllFunctionality)
{
    srs_error_t err;

    // Test clear_all functionality
    if (true) {
        SrsRtcFrameBuilderAudioPacketCache cache;
        std::vector<SrsRtpPacket *> ready_packets;

        // Process first packet to initialize
        SrsUniquePtr<SrsRtpPacket> pkt1(mock_create_audio_rtp_packet(100, 1000));
        HELPER_EXPECT_SUCCESS(cache.process_packet(pkt1.get(), ready_packets));
        free_audio_packets(ready_packets);

        // Add some out-of-order packets to buffer
        SrsUniquePtr<SrsRtpPacket> pkt3(mock_create_audio_rtp_packet(103, 1060));
        HELPER_EXPECT_SUCCESS(cache.process_packet(pkt3.get(), ready_packets));

        SrsUniquePtr<SrsRtpPacket> pkt5(mock_create_audio_rtp_packet(105, 1100));
        HELPER_EXPECT_SUCCESS(cache.process_packet(pkt5.get(), ready_packets));

        // Should have some packets waiting in buffer
        EXPECT_EQ(0, (int)ready_packets.size());

        // Clear all cached packets
        cache.clear_all();

        EXPECT_EQ(0, (int)cache.audio_buffer_.size());
    }
}

VOID TEST(RTC3AudioCacheTest, DuplicatePacketHandling)
{
    srs_error_t err;

    // Test duplicate packet handling
    if (true) {
        SrsRtcFrameBuilderAudioPacketCache cache;
        std::vector<SrsRtpPacket *> ready_packets;

        // Process first packet
        SrsUniquePtr<SrsRtpPacket> pkt1(mock_create_audio_rtp_packet(100, 1000));
        HELPER_EXPECT_SUCCESS(cache.process_packet(pkt1.get(), ready_packets));
        EXPECT_EQ(1, (int)ready_packets.size());
        free_audio_packets(ready_packets);

        // Process packet 102 (out of order)
        SrsUniquePtr<SrsRtpPacket> pkt3(mock_create_audio_rtp_packet(102, 1040));
        HELPER_EXPECT_SUCCESS(cache.process_packet(pkt3.get(), ready_packets));
        EXPECT_EQ(0, (int)ready_packets.size()); // Waiting for 101

        // Process duplicate packet 102
        SrsUniquePtr<SrsRtpPacket> pkt3_dup(mock_create_audio_rtp_packet(102, 1040));
        HELPER_EXPECT_SUCCESS(cache.process_packet(pkt3_dup.get(), ready_packets));
        EXPECT_EQ(0, (int)ready_packets.size()); // Still waiting for 101

        // Process packet 101 to complete sequence
        SrsUniquePtr<SrsRtpPacket> pkt2(mock_create_audio_rtp_packet(101, 1020));
        HELPER_EXPECT_SUCCESS(cache.process_packet(pkt2.get(), ready_packets));

        // Should process 101 and one instance of 102 (duplicate should be handled)
        EXPECT_GE((int)ready_packets.size(), 2);
        EXPECT_EQ(101, ready_packets[0]->header_.get_sequence());
        EXPECT_EQ(102, ready_packets[1]->header_.get_sequence());
        free_audio_packets(ready_packets);
    }
}
