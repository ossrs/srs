//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//
#include <srs_utest_ai03.hpp>

#include <srs_app_rtc_source.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_codec.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_rtc_rtp.hpp>

#include <srs_utest_manual_service.hpp>

#include <chrono>
#include <vector>
using namespace std;

VOID TEST(KernelRTC2Test, SrsCodecPayloadVideoCodecCaching)
{
    // Test video codec caching mechanism
    if (true) {
        SrsCodecPayload payload;
        payload.name_ = "H264";

        // First call should parse and cache the codec
        int8_t codec1 = payload.codec(true);
        EXPECT_EQ(SrsVideoCodecIdAVC, codec1);

        // Second call should return cached value
        int8_t codec2 = payload.codec(true);
        EXPECT_EQ(SrsVideoCodecIdAVC, codec2);
        EXPECT_EQ(codec1, codec2);
    }
}

VOID TEST(KernelRTC2Test, SrsCodecPayloadAudioCodecCaching)
{
    // Test audio codec caching mechanism
    if (true) {
        SrsCodecPayload payload;
        payload.name_ = "AAC";

        // First call should parse and cache the codec
        int8_t codec1 = payload.codec(false);
        EXPECT_EQ(SrsAudioCodecIdAAC, codec1);

        // Second call should return cached value
        int8_t codec2 = payload.codec(false);
        EXPECT_EQ(SrsAudioCodecIdAAC, codec2);
        EXPECT_EQ(codec1, codec2);
    }
}

VOID TEST(KernelRTC2Test, SrsCodecPayloadVideoCodecTypes)
{
    // Test various video codec types
    if (true) {
        // H.264/AVC codec
        SrsCodecPayload h264_payload;
        h264_payload.name_ = "H264";
        EXPECT_EQ(SrsVideoCodecIdAVC, h264_payload.codec(true));

        // Alternative H.264 name
        SrsCodecPayload avc_payload;
        avc_payload.name_ = "AVC";
        EXPECT_EQ(SrsVideoCodecIdAVC, avc_payload.codec(true));
    }

    if (true) {
        // H.265/HEVC codec
        SrsCodecPayload h265_payload;
        h265_payload.name_ = "H265";
        EXPECT_EQ(SrsVideoCodecIdHEVC, h265_payload.codec(true));

        // Alternative H.265 name
        SrsCodecPayload hevc_payload;
        hevc_payload.name_ = "HEVC";
        EXPECT_EQ(SrsVideoCodecIdHEVC, hevc_payload.codec(true));
    }

    if (true) {
        // AV1 codec
        SrsCodecPayload av1_payload;
        av1_payload.name_ = "AV1";
        EXPECT_EQ(SrsVideoCodecIdAV1, av1_payload.codec(true));
    }

    if (true) {
        // VP6 codec
        SrsCodecPayload vp6_payload;
        vp6_payload.name_ = "VP6";
        EXPECT_EQ(SrsVideoCodecIdOn2VP6, vp6_payload.codec(true));

        // VP6 with alpha channel
        SrsCodecPayload vp6a_payload;
        vp6a_payload.name_ = "VP6A";
        EXPECT_EQ(SrsVideoCodecIdOn2VP6WithAlphaChannel, vp6a_payload.codec(true));
    }
}

VOID TEST(KernelRTC2Test, SrsCodecPayloadAudioCodecTypes)
{
    // Test various audio codec types
    if (true) {
        // AAC codec
        SrsCodecPayload aac_payload;
        aac_payload.name_ = "AAC";
        EXPECT_EQ(SrsAudioCodecIdAAC, aac_payload.codec(false));
    }

    if (true) {
        // MP3 codec
        SrsCodecPayload mp3_payload;
        mp3_payload.name_ = "MP3";
        EXPECT_EQ(SrsAudioCodecIdMP3, mp3_payload.codec(false));
    }

    if (true) {
        // Opus codec
        SrsCodecPayload opus_payload;
        opus_payload.name_ = "OPUS";
        EXPECT_EQ(SrsAudioCodecIdOpus, opus_payload.codec(false));
    }

    if (true) {
        // Speex codec
        SrsCodecPayload speex_payload;
        speex_payload.name_ = "SPEEX";
        EXPECT_EQ(SrsAudioCodecIdSpeex, speex_payload.codec(false));
    }
}

VOID TEST(KernelRTC2Test, SrsCodecPayloadCaseInsensitive)
{
    // Test case insensitive codec name parsing
    if (true) {
        // Video codecs - lowercase
        SrsCodecPayload h264_lower;
        h264_lower.name_ = "h264";
        EXPECT_EQ(SrsVideoCodecIdAVC, h264_lower.codec(true));

        SrsCodecPayload hevc_lower;
        hevc_lower.name_ = "hevc";
        EXPECT_EQ(SrsVideoCodecIdHEVC, hevc_lower.codec(true));

        // Video codecs - mixed case
        SrsCodecPayload h264_mixed;
        h264_mixed.name_ = "H264";
        EXPECT_EQ(SrsVideoCodecIdAVC, h264_mixed.codec(true));

        SrsCodecPayload hevc_mixed;
        hevc_mixed.name_ = "Hevc";
        EXPECT_EQ(SrsVideoCodecIdHEVC, hevc_mixed.codec(true));
    }

    if (true) {
        // Audio codecs - lowercase
        SrsCodecPayload aac_lower;
        aac_lower.name_ = "aac";
        EXPECT_EQ(SrsAudioCodecIdAAC, aac_lower.codec(false));

        SrsCodecPayload opus_lower;
        opus_lower.name_ = "opus";
        EXPECT_EQ(SrsAudioCodecIdOpus, opus_lower.codec(false));

        // Audio codecs - mixed case
        SrsCodecPayload mp3_mixed;
        mp3_mixed.name_ = "Mp3";
        EXPECT_EQ(SrsAudioCodecIdMP3, mp3_mixed.codec(false));
    }
}

VOID TEST(KernelRTC2Test, SrsCodecPayloadUnknownCodecs)
{
    // Test unknown/unsupported codec handling
    if (true) {
        // Unknown video codec
        SrsCodecPayload unknown_video;
        unknown_video.name_ = "H266"; // Future codec not yet supported
        EXPECT_EQ(SrsVideoCodecIdReserved, unknown_video.codec(true));

        // Completely unknown video codec
        SrsCodecPayload invalid_video;
        invalid_video.name_ = "UNKNOWN_VIDEO";
        EXPECT_EQ(SrsVideoCodecIdReserved, invalid_video.codec(true));
    }

    if (true) {
        // Unknown audio codec
        SrsCodecPayload unknown_audio;
        unknown_audio.name_ = "FLAC"; // Not supported in this context
        EXPECT_EQ(SrsAudioCodecIdReserved1, unknown_audio.codec(false));

        // Completely unknown audio codec
        SrsCodecPayload invalid_audio;
        invalid_audio.name_ = "UNKNOWN_AUDIO";
        EXPECT_EQ(SrsAudioCodecIdReserved1, invalid_audio.codec(false));
    }
}

VOID TEST(KernelRTC2Test, SrsCodecPayloadEmptyName)
{
    // Test empty codec name handling
    if (true) {
        SrsCodecPayload empty_video;
        empty_video.name_ = "";
        EXPECT_EQ(SrsVideoCodecIdReserved, empty_video.codec(true));

        SrsCodecPayload empty_audio;
        empty_audio.name_ = "";
        EXPECT_EQ(SrsAudioCodecIdReserved1, empty_audio.codec(false));
    }
}

VOID TEST(KernelRTC2Test, SrsCodecPayloadContextSensitive)
{
    // Test that the same codec name can be interpreted differently based on context
    if (true) {
        // This test demonstrates that the video/audio context parameter matters
        SrsCodecPayload payload;
        payload.name_ = "H264";

        // When called with video=true, should return video codec ID
        int8_t video_codec = payload.codec(true);
        EXPECT_EQ(SrsVideoCodecIdAVC, video_codec);

        // Reset the cached value to test audio context
        payload.codec_ = -1;

        // When called with video=false, H264 is not a valid audio codec
        int8_t audio_codec = payload.codec(false);
        EXPECT_EQ(SrsAudioCodecIdReserved1, audio_codec);
    }
}

VOID TEST(KernelRTC2Test, SrsCodecPayloadConstructorInitialization)
{
    // Test that codec_ is properly initialized to -1
    if (true) {
        SrsCodecPayload payload1;
        // codec_ should be initialized to -1 (not cached)
        EXPECT_EQ(-1, payload1.codec_);

        SrsCodecPayload payload2(96, "H264", 90000);
        // codec_ should be initialized to -1 (not cached)
        EXPECT_EQ(-1, payload2.codec_);
    }
}

VOID TEST(KernelRTC2Test, SrsCodecPayloadMultipleCallsConsistency)
{
    // Test that multiple calls with the same context return consistent results
    if (true) {
        SrsCodecPayload payload;
        payload.name_ = "HEVC";

        // Multiple calls should return the same result
        for (int i = 0; i < 10; i++) {
            EXPECT_EQ(SrsVideoCodecIdHEVC, payload.codec(true));
        }

        // Reset and test audio context
        payload.codec_ = -1;
        payload.name_ = "OPUS";

        for (int i = 0; i < 10; i++) {
            EXPECT_EQ(SrsAudioCodecIdOpus, payload.codec(false));
        }
    }
}

VOID TEST(KernelRTC2Test, SrsCodecPayloadCacheInvalidation)
{
    // Test that changing name_ doesn't automatically invalidate cache
    // (This demonstrates the current behavior - cache is not automatically invalidated)
    if (true) {
        SrsCodecPayload payload;
        payload.name_ = "H264";

        // First call caches the result
        int8_t codec1 = payload.codec(true);
        EXPECT_EQ(SrsVideoCodecIdAVC, codec1);

        // Change the name but cache should still return old value
        payload.name_ = "HEVC";
        int8_t codec2 = payload.codec(true);
        EXPECT_EQ(SrsVideoCodecIdAVC, codec2); // Still returns cached H264 value

        // Manual cache reset allows new parsing
        payload.codec_ = -1;
        int8_t codec3 = payload.codec(true);
        EXPECT_EQ(SrsVideoCodecIdHEVC, codec3); // Now returns HEVC
    }
}

VOID TEST(KernelRTC2Test, SrsCodecPayloadSpecialCharacters)
{
    // Test codec names with special characters or whitespace
    if (true) {
        // Test with leading/trailing spaces (should still work due to uppercase conversion)
        SrsCodecPayload payload_spaces;
        payload_spaces.name_ = " H264 ";
        // Note: The current implementation doesn't trim spaces, so this will be unknown
        EXPECT_EQ(SrsVideoCodecIdReserved, payload_spaces.codec(true));

        // Test with numbers and special characters
        SrsCodecPayload payload_special;
        payload_special.name_ = "H.264";
        EXPECT_EQ(SrsVideoCodecIdReserved, payload_special.codec(true));
    }
}

VOID TEST(KernelRTC2Test, SrsCodecPayloadBoundaryValues)
{
    // Test boundary values and edge cases
    if (true) {
        // Test very long codec name
        SrsCodecPayload long_name;
        long_name.name_ = std::string(1000, 'A'); // 1000 character string
        EXPECT_EQ(SrsVideoCodecIdReserved, long_name.codec(true));
        // Already cached by video, so should return same result for audio.
        EXPECT_EQ(SrsVideoCodecIdReserved, long_name.codec(false));
    }

    if (true) {
        // Test single character names
        SrsCodecPayload single_char;
        single_char.name_ = "A";
        EXPECT_EQ(SrsVideoCodecIdReserved, single_char.codec(true));
        // Already cached by video, so should return same result for audio.
        EXPECT_EQ(SrsVideoCodecIdReserved, single_char.codec(false));
    }
}

VOID TEST(KernelRTC2Test, SrsCodecPayloadPerformanceCache)
{
    // Test that caching provides performance benefit (conceptual test)
    if (true) {
        SrsCodecPayload payload;
        payload.name_ = "H264";

        // First call does parsing and caching
        std::chrono::high_resolution_clock::now();
        int8_t codec1 = payload.codec(true);
        std::chrono::high_resolution_clock::now();

        // Subsequent calls should be faster (cached)
        std::chrono::high_resolution_clock::now();
        int8_t codec2 = payload.codec(true);
        std::chrono::high_resolution_clock::now();

        // Both should return the same result
        EXPECT_EQ(codec1, codec2);
        EXPECT_EQ(SrsVideoCodecIdAVC, codec1);

        // Note: In practice, the performance difference might be negligible
        // for such simple string comparisons, but the caching mechanism is there
    }
}

VOID TEST(KernelRTC2Test, SrsVideoPayloadInheritance)
{
    // Test that SrsVideoPayload inherits codec functionality correctly
    if (true) {
        SrsVideoPayload video_payload;
        video_payload.name_ = "H265";

        // Should work the same as base class
        EXPECT_EQ(SrsVideoCodecIdHEVC, video_payload.codec(true));

        // Test caching works in derived class
        EXPECT_EQ(SrsVideoCodecIdHEVC, video_payload.codec(true));
    }
}

VOID TEST(KernelRTC2Test, SrsAudioPayloadInheritance)
{
    // Test that SrsAudioPayload inherits codec functionality correctly
    if (true) {
        SrsAudioPayload audio_payload;
        audio_payload.name_ = "AAC";

        // Should work the same as base class
        EXPECT_EQ(SrsAudioCodecIdAAC, audio_payload.codec(false));

        // Test caching works in derived class
        EXPECT_EQ(SrsAudioCodecIdAAC, audio_payload.codec(false));
    }
}

VOID TEST(KernelRTC2Test, SrsCodecPayloadCopyBehavior)
{
    // Test that copy() method doesn't copy the cached codec_ value
    if (true) {
        SrsCodecPayload original;
        original.name_ = "H264";
        original.pt_ = 96;
        original.sample_ = 90000;

        // Cache the codec value
        int8_t codec_original = original.codec(true);
        EXPECT_EQ(SrsVideoCodecIdAVC, codec_original);

        // Copy the payload
        SrsCodecPayload *copied = original.copy();

        // The copied payload should have the same name but uncached codec
        EXPECT_EQ(original.name_, copied->name_);
        EXPECT_EQ(original.pt_, copied->pt_);
        EXPECT_EQ(original.sample_, copied->sample_);
        EXPECT_EQ(-1, copied->codec_); // Should not copy cached value

        // But calling codec() should return the same result
        EXPECT_EQ(SrsVideoCodecIdAVC, copied->codec(true));

        srs_freep(copied);
    }
}

// Helper function to create a test RTP packet
SrsRtpPacket *mock_create_test_rtp_packet(uint16_t sequence_number, uint32_t timestamp, bool marker = false)
{
    SrsRtpPacket *pkt = new SrsRtpPacket();
    pkt->header_.set_sequence(sequence_number);
    pkt->header_.set_timestamp(timestamp);
    pkt->header_.set_marker(marker);
    pkt->header_.set_ssrc(12345);
    return pkt;
}

// Helper function to create a test FU-A payload
SrsRtpFUAPayload2 *mock_create_test_fua_payload(bool start, bool end, const char *payload_data, int size)
{
    SrsRtpFUAPayload2 *fua = new SrsRtpFUAPayload2();
    fua->start_ = start;
    fua->end_ = end;
    fua->nalu_type_ = SrsAvcNaluTypeNonIDR; // Use a common NALU type
    fua->nri_ = SrsAvcNaluTypeNonIDR;

    // Create a buffer for the payload
    char *buf = new char[size];
    memcpy(buf, payload_data, size);
    fua->payload_ = buf;
    fua->size_ = size;

    return fua;
}

VOID TEST(KernelRTC2Test, SrsRtcFrameBuilderVideoPacketCacheBasicOperations)
{
    // Test basic store and get operations
    if (true) {
        SrsRtcFrameBuilderVideoPacketCache cache;

        // Test storing and retrieving a packet
        SrsRtpPacket *pkt1 = mock_create_test_rtp_packet(100, 1000);
        cache.store_packet(pkt1);

        SrsRtpPacket *retrieved = cache.get_packet(100);
        EXPECT_TRUE(retrieved != NULL);
        EXPECT_EQ(100, retrieved->header_.get_sequence());
        EXPECT_EQ(1000, retrieved->header_.get_timestamp());

        // Test getting non-existent packet
        SrsRtpPacket *missing = cache.get_packet(200);
        EXPECT_TRUE(missing == NULL);
    }
}

VOID TEST(KernelRTC2Test, SrsRtcFrameBuilderVideoPacketCacheNullPacket)
{
    // Test handling of null packets
    if (true) {
        SrsRtcFrameBuilderVideoPacketCache cache;

        // Storing null packet should be ignored
        cache.store_packet(NULL);

        // Cache should remain empty
        SrsRtpPacket *retrieved = cache.get_packet(100);
        EXPECT_TRUE(retrieved == NULL);
    }
}

VOID TEST(KernelRTC2Test, SrsRtcFrameBuilderVideoPacketCacheOverwrite)
{
    // Test overwriting packets in the same slot
    if (true) {
        SrsRtcFrameBuilderVideoPacketCache cache;

        // Store first packet
        SrsRtpPacket *pkt1 = mock_create_test_rtp_packet(100, 1000);
        cache.store_packet(pkt1);

        // Store second packet with same sequence (should overwrite)
        SrsRtpPacket *pkt2 = mock_create_test_rtp_packet(100, 2000);
        cache.store_packet(pkt2);

        // Should get the second packet
        SrsRtpPacket *retrieved = cache.get_packet(100);
        EXPECT_TRUE(retrieved != NULL);
        EXPECT_EQ(100, retrieved->header_.get_sequence());
        EXPECT_EQ(2000, retrieved->header_.get_timestamp());
    }
}

VOID TEST(KernelRTC2Test, SrsRtcFrameBuilderVideoPacketCacheModuloIndexing)
{
    int cache_size = SrsRtcFrameBuilderVideoPacketCache::cache_size_;

    // Test that cache uses modulo indexing (N slots)
    if (true) {
        SrsRtcFrameBuilderVideoPacketCache cache;

        // Store packets that would map to the same cache slot (N apart)
        SrsRtpPacket *pkt1 = mock_create_test_rtp_packet(100, 1000);
        SrsRtpPacket *pkt2 = mock_create_test_rtp_packet(100 + cache_size, 2000);

        cache.store_packet(pkt1);
        cache.store_packet(pkt2);

        // Should get the second packet (overwrote first)
        SrsRtpPacket *retrieved1 = cache.get_packet(100);
        EXPECT_TRUE(retrieved1 == NULL); // First packet was overwritten

        SrsRtpPacket *retrieved2 = cache.get_packet(100 + cache_size);
        EXPECT_TRUE(retrieved2 != NULL);
        EXPECT_EQ(100 + cache_size, retrieved2->header_.get_sequence());
        EXPECT_EQ(2000, retrieved2->header_.get_timestamp());
    }
}

VOID TEST(KernelRTC2Test, SrsRtcFrameBuilderVideoPacketCacheTakePacket)
{
    // Test take_packet functionality
    if (true) {
        SrsRtcFrameBuilderVideoPacketCache cache;

        // Store a packet
        SrsRtpPacket *pkt = mock_create_test_rtp_packet(100, 1000);
        cache.store_packet(pkt);

        // Take the packet (should remove from cache)
        SrsRtpPacket *taken = cache.take_packet(100);
        EXPECT_TRUE(taken != NULL);
        EXPECT_EQ(100, taken->header_.get_sequence());

        // Clean up the taken packet
        srs_freep(taken);

        // Packet should no longer be in cache
        SrsRtpPacket *retrieved = cache.get_packet(100);
        EXPECT_TRUE(retrieved == NULL);
    }
}

VOID TEST(KernelRTC2Test, SrsRtcFrameBuilderVideoPacketCacheTakeNonExistent)
{
    // Test taking non-existent packet
    if (true) {
        SrsRtcFrameBuilderVideoPacketCache cache;

        // Try to take packet that doesn't exist
        SrsRtpPacket *taken = cache.take_packet(100);
        EXPECT_TRUE(taken == NULL);
    }
}

VOID TEST(KernelRTC2Test, SrsRtcFrameBuilderVideoPacketCacheClearAll)
{
    // Test clear_all functionality
    if (true) {
        SrsRtcFrameBuilderVideoPacketCache cache;

        // Store multiple packets
        for (int i = 0; i < 10; i++) {
            SrsRtpPacket *pkt = mock_create_test_rtp_packet(100 + i, 1000 + i);
            cache.store_packet(pkt);
        }

        // Verify packets are stored
        SrsRtpPacket *retrieved = cache.get_packet(105);
        EXPECT_TRUE(retrieved != NULL);

        // Clear all packets
        cache.clear_all();

        // Verify all packets are gone
        for (int i = 0; i < 10; i++) {
            SrsRtpPacket *pkt = cache.get_packet(100 + i);
            EXPECT_TRUE(pkt == NULL);
        }
    }
}

VOID TEST(KernelRTC2Test, SrsRtcFrameBuilderVideoPacketCacheFindNextLostSnComplete)
{
    // Test find_next_lost_sn when frame is complete
    if (true) {
        SrsRtcFrameBuilderVideoPacketCache cache;

        // Store a complete sequence of packets with same timestamp
        uint32_t timestamp = 1000;
        for (uint16_t i = 100; i <= 105; i++) {
            SrsRtpPacket *pkt = mock_create_test_rtp_packet(i, timestamp, i == 105); // Last packet has marker
            cache.store_packet(pkt);
        }

        uint16_t end_sn = 0;
        int32_t result = cache.find_next_lost_sn(100, 100, end_sn);

        // Should return -1 (complete frame) and set end_sn to marker packet
        EXPECT_EQ(-1, result);
        EXPECT_EQ(105, end_sn);
    }
}

VOID TEST(KernelRTC2Test, SrsRtcFrameBuilderVideoPacketCacheFindNextLostSnMissing)
{
    // Test find_next_lost_sn when packet is missing
    if (true) {
        SrsRtcFrameBuilderVideoPacketCache cache;

        // Store packets with a gap (missing 103)
        uint32_t timestamp = 1000;
        cache.store_packet(mock_create_test_rtp_packet(100, timestamp));
        cache.store_packet(mock_create_test_rtp_packet(101, timestamp));
        cache.store_packet(mock_create_test_rtp_packet(102, timestamp));
        // Skip 103
        cache.store_packet(mock_create_test_rtp_packet(104, timestamp));
        cache.store_packet(mock_create_test_rtp_packet(105, timestamp, true)); // marker

        uint16_t end_sn = 0;
        int32_t result = cache.find_next_lost_sn(100, 100, end_sn);

        // Should return the missing sequence number
        EXPECT_EQ(103, result);
    }
}

VOID TEST(KernelRTC2Test, SrsRtcFrameBuilderVideoPacketCacheFindNextLostSnDifferentTimestamp)
{
    // Test find_next_lost_sn when timestamp changes (frame boundary)
    // NOTE: This tests the current implementation behavior, which uses timestamp changes
    // to detect frame boundaries. According to RFC 6184, the marker bit should be the
    // primary mechanism, but SRS also uses timestamp changes as a fallback.
    if (true) {
        SrsRtcFrameBuilderVideoPacketCache cache;

        // Store packets with different timestamps but no marker bits
        cache.store_packet(mock_create_test_rtp_packet(100, 1000, false)); // marker is false
        cache.store_packet(mock_create_test_rtp_packet(101, 1000, false)); // marker is false
        cache.store_packet(mock_create_test_rtp_packet(102, 2000, false)); // Different timestamp, marker false

        uint16_t end_sn = 0;
        int32_t result = cache.find_next_lost_sn(100, 100, end_sn);

        // Current implementation: returns -1 (complete frame) and sets end_sn to last packet of same timestamp
        // This is SRS-specific behavior that uses timestamp changes as frame boundary detection
        EXPECT_EQ(-1, result);
        EXPECT_EQ(101, end_sn);
    }
}

VOID TEST(KernelRTC2Test, SrsRtcFrameBuilderVideoPacketCacheFindNextLostSnRfcCompliant)
{
    // Test RFC 6184 compliant frame boundary detection using marker bit
    if (true) {
        SrsRtcFrameBuilderVideoPacketCache cache;

        // Store packets: 100 and 101 have same timestamp, 101 has marker bit, 102 has different timestamp
        cache.store_packet(mock_create_test_rtp_packet(100, 1000, false)); // marker is false
        cache.store_packet(mock_create_test_rtp_packet(101, 1000, true));  // marker is true (end of access unit)
        cache.store_packet(mock_create_test_rtp_packet(102, 2000, false)); // different timestamp

        uint16_t end_sn = 0;
        int32_t result = cache.find_next_lost_sn(100, 100, end_sn);

        // RFC 6184 compliant: Should return -1 (complete frame) and set end_sn to marker packet (101)
        // The marker bit should take precedence over timestamp changes
        EXPECT_EQ(-1, result);
        EXPECT_EQ(101, end_sn);
    }
}

VOID TEST(KernelRTC2Test, SrsRtcFrameBuilderVideoPacketCacheFindNextLostSnMarkerVsTimestamp)
{
    // Test the priority between marker bit and timestamp change
    // This demonstrates the current SRS implementation behavior vs RFC 6184 compliance
    if (true) {
        SrsRtcFrameBuilderVideoPacketCache cache;

        // Critical scenario: 100 and 101 have same timestamp, 101 has marker bit, 102 has different timestamp
        // This tests whether SRS respects the marker bit or prioritizes timestamp changes
        cache.store_packet(mock_create_test_rtp_packet(100, 1000, false)); // marker is false
        cache.store_packet(mock_create_test_rtp_packet(101, 1000, true));  // marker is true (should end frame here)
        cache.store_packet(mock_create_test_rtp_packet(102, 2000, false)); // different timestamp

        uint16_t end_sn = 0;
        int32_t result = cache.find_next_lost_sn(100, 100, end_sn);

        // Current SRS implementation behavior:
        // The algorithm checks marker bit BEFORE timestamp change in the loop
        // So it should detect the marker bit on packet 101 and end the frame there
        EXPECT_EQ(-1, result);
        EXPECT_EQ(101, end_sn); // Should end at marker packet, not at timestamp change

        // This actually demonstrates that SRS IS RFC-compliant in this case!
        // The marker bit is detected first and takes precedence
    }
}

VOID TEST(KernelRTC2Test, SrsRtcFrameBuilderVideoPacketCacheTimestampPriorityIssue)
{
    // Test the specific issue: timestamp check happens BEFORE marker check
    // This demonstrates a potential RFC compliance issue
    if (true) {
        SrsRtcFrameBuilderVideoPacketCache cache;

        // Scenario where timestamp change occurs before we reach the marker bit
        cache.store_packet(mock_create_test_rtp_packet(100, 1000, false)); // timestamp=1000, marker=false
        cache.store_packet(mock_create_test_rtp_packet(101, 2000, false)); // timestamp=2000, marker=false (timestamp changed!)
        cache.store_packet(mock_create_test_rtp_packet(102, 2000, true));  // timestamp=2000, marker=true (actual frame end)

        uint16_t end_sn = 0;
        int32_t result = cache.find_next_lost_sn(100, 100, end_sn);

        // Current SRS implementation: timestamp check happens FIRST
        // When it reaches packet 101, it detects timestamp change and ends frame at packet 100
        // It never gets to check the marker bit on packet 102
        EXPECT_EQ(-1, result);
        EXPECT_EQ(100, end_sn); // Ends at last packet with original timestamp (100)

        // This is NOT RFC-compliant! According to RFC 6184, it should continue until
        // the marker bit (packet 102) and set end_sn = 102
        // The comment in line 1591 "check time first, avoid two small frame mixed case decode fail"
        // shows this is intentional for robustness, but it's not RFC-compliant
    }
}

VOID TEST(KernelRTC2Test, SrsRtcFrameBuilderVideoPacketCacheFindNextLostSnNoMarkerNoTimestampChange)
{
    // Test behavior when neither marker bit nor timestamp change occurs
    // This tests the cache overflow detection
    if (true) {
        SrsRtcFrameBuilderVideoPacketCache cache;

        // Store many packets with same timestamp and no marker bit
        // This should eventually trigger cache overflow detection
        uint32_t timestamp = 1000;
        for (int i = 0; i < 10; i++) {
            cache.store_packet(mock_create_test_rtp_packet(100 + i, timestamp, false));
        }

        uint16_t end_sn = 0;
        int32_t result = cache.find_next_lost_sn(100, 100, end_sn);

        // Should continue searching until cache limit or missing packet
        // In this case, all packets are present, so it should continue until cache overflow
        // The exact behavior depends on cache size (512), but for this small test it should work
        EXPECT_EQ(110, result); // Should find missing packet after the stored range
    }
}

VOID TEST(KernelRTC2Test, SrsRtcFrameBuilderVideoPacketCacheCheckFrameCompleteSimple)
{
    // Test check_frame_complete with non-fragmented packets
    if (true) {
        SrsRtcFrameBuilderVideoPacketCache cache;

        // Store simple packets (no FU-A payload)
        for (uint16_t i = 100; i <= 103; i++) {
            SrsRtpPacket *pkt = mock_create_test_rtp_packet(i, 1000);
            cache.store_packet(pkt);
        }

        // When there are no FU-A payloads, fu_s_c == fu_e_c (both are 0)
        bool complete = cache.check_frame_complete(100, 103);
        EXPECT_TRUE(complete); // Expected: no FU-A fragments means complete (0 == 0)
    }
}

VOID TEST(KernelRTC2Test, SrsRtcFrameBuilderVideoPacketCacheCheckFrameCompleteFragmented)
{
    // Test check_frame_complete with fragmented packets (FU-A)
    if (true) {
        SrsRtcFrameBuilderVideoPacketCache cache;

        // Create fragmented packets with matching start/end counts
        char payload_data[] = "test_payload";

        // First fragment (start=true, end=false)
        SrsRtpPacket *pkt1 = mock_create_test_rtp_packet(100, 1000);
        SrsRtpFUAPayload2 *fua1 = mock_create_test_fua_payload(true, false, payload_data, sizeof(payload_data));
        pkt1->set_payload(fua1, SrsRtpPacketPayloadTypeFUA2);
        cache.store_packet(pkt1);

        // Middle fragment (start=false, end=false)
        SrsRtpPacket *pkt2 = mock_create_test_rtp_packet(101, 1000);
        SrsRtpFUAPayload2 *fua2 = mock_create_test_fua_payload(false, false, payload_data, sizeof(payload_data));
        pkt2->set_payload(fua2, SrsRtpPacketPayloadTypeFUA2);
        cache.store_packet(pkt2);

        // Last fragment (start=false, end=true)
        SrsRtpPacket *pkt3 = mock_create_test_rtp_packet(102, 1000);
        SrsRtpFUAPayload2 *fua3 = mock_create_test_fua_payload(false, true, payload_data, sizeof(payload_data));
        pkt3->set_payload(fua3, SrsRtpPacketPayloadTypeFUA2);
        cache.store_packet(pkt3);

        // Should return true (1 start fragment == 1 end fragment = complete fragmented frame)
        bool complete = cache.check_frame_complete(100, 102);
        EXPECT_TRUE(complete);
    }
}

VOID TEST(KernelRTC2Test, SrsRtcFrameBuilderVideoPacketCacheCheckFrameIncompleteFragmented)
{
    // Test check_frame_complete with incomplete fragmented packets
    if (true) {
        SrsRtcFrameBuilderVideoPacketCache cache;

        char payload_data[] = "test_payload";

        // Two start fragments but only one end fragment (incomplete)
        SrsRtpPacket *pkt1 = mock_create_test_rtp_packet(100, 1000);
        SrsRtpFUAPayload2 *fua1 = mock_create_test_fua_payload(true, false, payload_data, sizeof(payload_data));
        pkt1->set_payload(fua1, SrsRtpPacketPayloadTypeFUA2);
        cache.store_packet(pkt1);

        SrsRtpPacket *pkt2 = mock_create_test_rtp_packet(101, 1000);
        SrsRtpFUAPayload2 *fua2 = mock_create_test_fua_payload(true, false, payload_data, sizeof(payload_data));
        pkt2->set_payload(fua2, SrsRtpPacketPayloadTypeFUA2);
        cache.store_packet(pkt2);

        SrsRtpPacket *pkt3 = mock_create_test_rtp_packet(102, 1000);
        SrsRtpFUAPayload2 *fua3 = mock_create_test_fua_payload(false, true, payload_data, sizeof(payload_data));
        pkt3->set_payload(fua3, SrsRtpPacketPayloadTypeFUA2);
        cache.store_packet(pkt3);

        // Should return false (2 starts, 1 end - mismatch)
        bool complete = cache.check_frame_complete(100, 102);
        EXPECT_FALSE(complete);
    }
}

VOID TEST(KernelRTC2Test, SrsRtcFrameBuilderVideoPacketCacheCheckFrameCompleteOneStartOneEnd)
{
    // Test check_frame_complete with exactly 1 start and 1 end fragment (correct case)
    if (true) {
        SrsRtcFrameBuilderVideoPacketCache cache;

        char payload_data[] = "test_payload";

        // Single fragmented NALU: start fragment
        SrsRtpPacket *pkt1 = mock_create_test_rtp_packet(100, 1000);
        SrsRtpFUAPayload2 *fua1 = mock_create_test_fua_payload(true, false, payload_data, sizeof(payload_data));
        pkt1->set_payload(fua1, SrsRtpPacketPayloadTypeFUA2);
        cache.store_packet(pkt1);

        // Middle fragments (no start, no end)
        SrsRtpPacket *pkt2 = mock_create_test_rtp_packet(101, 1000);
        SrsRtpFUAPayload2 *fua2 = mock_create_test_fua_payload(false, false, payload_data, sizeof(payload_data));
        pkt2->set_payload(fua2, SrsRtpPacketPayloadTypeFUA2);
        cache.store_packet(pkt2);

        SrsRtpPacket *pkt3 = mock_create_test_rtp_packet(102, 1000);
        SrsRtpFUAPayload2 *fua3 = mock_create_test_fua_payload(false, false, payload_data, sizeof(payload_data));
        pkt3->set_payload(fua3, SrsRtpPacketPayloadTypeFUA2);
        cache.store_packet(pkt3);

        // End fragment
        SrsRtpPacket *pkt4 = mock_create_test_rtp_packet(103, 1000);
        SrsRtpFUAPayload2 *fua4 = mock_create_test_fua_payload(false, true, payload_data, sizeof(payload_data));
        pkt4->set_payload(fua4, SrsRtpPacketPayloadTypeFUA2);
        cache.store_packet(pkt4);

        // Should return true (1 start == 1 end = complete fragmented frame)
        bool complete = cache.check_frame_complete(100, 103);
        EXPECT_TRUE(complete);
    }
}

VOID TEST(KernelRTC2Test, SrsRtcFrameBuilderVideoPacketCacheCheckFrameCompleteMultipleNalus)
{
    // Test check_frame_complete with multiple complete fragmented NALUs (2 start == 2 end = complete)
    if (true) {
        SrsRtcFrameBuilderVideoPacketCache cache;

        char payload_data[] = "test_payload";

        // First NALU: start fragment
        SrsRtpPacket *pkt1 = mock_create_test_rtp_packet(100, 1000);
        SrsRtpFUAPayload2 *fua1 = mock_create_test_fua_payload(true, false, payload_data, sizeof(payload_data));
        pkt1->set_payload(fua1, SrsRtpPacketPayloadTypeFUA2);
        cache.store_packet(pkt1);

        // First NALU: end fragment
        SrsRtpPacket *pkt2 = mock_create_test_rtp_packet(101, 1000);
        SrsRtpFUAPayload2 *fua2 = mock_create_test_fua_payload(false, true, payload_data, sizeof(payload_data));
        pkt2->set_payload(fua2, SrsRtpPacketPayloadTypeFUA2);
        cache.store_packet(pkt2);

        // Second NALU: start fragment
        SrsRtpPacket *pkt3 = mock_create_test_rtp_packet(102, 1000);
        SrsRtpFUAPayload2 *fua3 = mock_create_test_fua_payload(true, false, payload_data, sizeof(payload_data));
        pkt3->set_payload(fua3, SrsRtpPacketPayloadTypeFUA2);
        cache.store_packet(pkt3);

        // Second NALU: end fragment
        SrsRtpPacket *pkt4 = mock_create_test_rtp_packet(103, 1000);
        SrsRtpFUAPayload2 *fua4 = mock_create_test_fua_payload(false, true, payload_data, sizeof(payload_data));
        pkt4->set_payload(fua4, SrsRtpPacketPayloadTypeFUA2);
        cache.store_packet(pkt4);

        // Should return true (2 starts == 2 ends = complete fragmented frame)
        bool complete = cache.check_frame_complete(100, 103);
        EXPECT_TRUE(complete);
    }
}

VOID TEST(KernelRTC2Test, SrsRtcFrameBuilderVideoPacketCacheCheckFrameCompleteNullPackets)
{
    // Test check_frame_complete with null packets in range
    if (true) {
        SrsRtcFrameBuilderVideoPacketCache cache;

        // Store only some packets in the range
        SrsRtpPacket *pkt1 = mock_create_test_rtp_packet(100, 1000);
        cache.store_packet(pkt1);
        // Skip 101 (will be null)
        SrsRtpPacket *pkt3 = mock_create_test_rtp_packet(102, 1000);
        cache.store_packet(pkt3);

        // Should handle null packets gracefully and return true (no fragmentation, 0 == 0)
        bool complete = cache.check_frame_complete(100, 102);
        EXPECT_TRUE(complete);
    }
}

VOID TEST(KernelRTC2Test, SrsRtcFrameBuilderVideoPacketCacheSequenceWrapAround)
{
    // Test sequence number wrap-around (16-bit overflow)
    if (true) {
        SrsRtcFrameBuilderVideoPacketCache cache;

        // Test near the 16-bit boundary
        uint16_t seq_near_max = 65534;
        SrsRtpPacket *pkt1 = mock_create_test_rtp_packet(seq_near_max, 1000);
        cache.store_packet(pkt1);

        uint16_t seq_wrapped = 1; // After wrap-around
        SrsRtpPacket *pkt2 = mock_create_test_rtp_packet(seq_wrapped, 1000);
        cache.store_packet(pkt2);

        // Should be able to retrieve both packets
        SrsRtpPacket *retrieved1 = cache.get_packet(seq_near_max);
        EXPECT_TRUE(retrieved1 != NULL);
        EXPECT_EQ(seq_near_max, retrieved1->header_.get_sequence());

        SrsRtpPacket *retrieved2 = cache.get_packet(seq_wrapped);
        EXPECT_TRUE(retrieved2 != NULL);
        EXPECT_EQ(seq_wrapped, retrieved2->header_.get_sequence());
    }
}

VOID TEST(KernelRTC2Test, SrsRtcFrameBuilderVideoPacketCacheMemoryManagement)
{
    // Test that cache properly manages memory
    if (true) {
        SrsRtcFrameBuilderVideoPacketCache cache;

        // Store a packet
        SrsRtpPacket *pkt = mock_create_test_rtp_packet(100, 1000);
        cache.store_packet(pkt);

        // Overwrite with another packet (should free the first one)
        SrsRtpPacket *pkt2 = mock_create_test_rtp_packet(100, 2000);
        cache.store_packet(pkt2);

        // Verify the second packet is stored
        SrsRtpPacket *retrieved = cache.get_packet(100);
        EXPECT_TRUE(retrieved != NULL);
        EXPECT_EQ(2000, retrieved->header_.get_timestamp());

        // Clear all should free remaining packets
        cache.clear_all();

        // Cache should be empty
        SrsRtpPacket *after_clear = cache.get_packet(100);
        EXPECT_TRUE(after_clear == NULL);
    }
}

VOID TEST(KernelRTC2Test, SrsRtcFrameBuilderVideoFrameDetectorBasicConstruction)
{
    // Test basic construction and destruction
    if (true) {
        SrsRtcFrameBuilderVideoPacketCache cache;
        SrsRtcFrameBuilderVideoFrameDetector detector(&cache);

        // Verify constructor initializes private members correctly
        EXPECT_EQ(&cache, detector.video_cache_);
        EXPECT_EQ(0, detector.header_sn_);
        EXPECT_EQ(0, detector.lost_sn_);
        EXPECT_EQ(-1, detector.rtp_key_frame_ts_);
    }
}

VOID TEST(KernelRTC2Test, SrsRtcFrameBuilderVideoFrameDetectorOnKeyframeStart)
{
    // Test on_keyframe_start functionality
    if (true) {
        SrsRtcFrameBuilderVideoPacketCache cache;
        SrsRtcFrameBuilderVideoFrameDetector detector(&cache);

        // Verify initial state
        EXPECT_EQ(-1, detector.rtp_key_frame_ts_);
        EXPECT_EQ(0, detector.header_sn_);
        EXPECT_EQ(0, detector.lost_sn_);

        // Create a keyframe packet
        SrsUniquePtr<SrsRtpPacket> keyframe_pkt(mock_create_test_rtp_packet(100, 1000));

        // Call on_keyframe_start - should initialize internal state
        detector.on_keyframe_start(keyframe_pkt.get());

        // Verify state after first keyframe
        EXPECT_EQ(1000, detector.rtp_key_frame_ts_);
        EXPECT_EQ(100, detector.header_sn_);
        EXPECT_EQ(101, detector.lost_sn_); // header_sn_ + 1

        // Test that subsequent calls with same timestamp don't reset
        SrsUniquePtr<SrsRtpPacket> same_keyframe_pkt(mock_create_test_rtp_packet(101, 1000));
        detector.on_keyframe_start(same_keyframe_pkt.get());

        // State should remain unchanged for same timestamp
        EXPECT_EQ(1000, detector.rtp_key_frame_ts_);
        EXPECT_EQ(100, detector.header_sn_);
        EXPECT_EQ(101, detector.lost_sn_);

        // Test that calls with different timestamp do reset
        SrsUniquePtr<SrsRtpPacket> new_keyframe_pkt(mock_create_test_rtp_packet(200, 2000));
        detector.on_keyframe_start(new_keyframe_pkt.get());

        // State should be reset for new timestamp
        EXPECT_EQ(2000, detector.rtp_key_frame_ts_);
        EXPECT_EQ(200, detector.header_sn_);
        EXPECT_EQ(201, detector.lost_sn_); // header_sn_ + 1

        // All keyframe packets will be automatically cleaned up by SrsUniquePtr destructors
    }
}

VOID TEST(KernelRTC2Test, SrsRtcFrameBuilderVideoFrameDetectorDetectFrameBasic)
{
    srs_error_t err;

    // Test basic detect_frame functionality
    if (true) {
        SrsRtcFrameBuilderVideoPacketCache cache;
        SrsRtcFrameBuilderVideoFrameDetector detector(&cache);

        // Initialize with keyframe
        SrsUniquePtr<SrsRtpPacket> keyframe_pkt(mock_create_test_rtp_packet(100, 1000));
        detector.on_keyframe_start(keyframe_pkt.get());

        // Verify initial state after keyframe
        EXPECT_EQ(100, detector.header_sn_);
        EXPECT_EQ(101, detector.lost_sn_);

        // Store some packets in cache to form a complete frame
        cache.store_packet(mock_create_test_rtp_packet(100, 1000, false));
        cache.store_packet(mock_create_test_rtp_packet(101, 1000, false));
        cache.store_packet(mock_create_test_rtp_packet(102, 1000, true)); // marker bit

        uint16_t frame_start = 0, frame_end = 0;
        bool frame_ready = false;

        // Test detecting frame when receiving the last packet
        HELPER_EXPECT_SUCCESS(detector.detect_frame(102, frame_start, frame_end, frame_ready));

        // If frame is ready, verify the frame boundaries
        if (frame_ready) {
            EXPECT_EQ(100, frame_start);
            EXPECT_EQ(102, frame_end);
        }

        // keyframe_pkt will be automatically cleaned up by SrsUniquePtr destructor
    }
}

VOID TEST(KernelRTC2Test, SrsRtcFrameBuilderVideoFrameDetectorDetectFrameWithGaps)
{
    srs_error_t err;

    // Test detect_frame with missing packets
    if (true) {
        SrsRtcFrameBuilderVideoPacketCache cache;
        SrsRtcFrameBuilderVideoFrameDetector detector(&cache);

        // Initialize with keyframe
        SrsUniquePtr<SrsRtpPacket> keyframe_pkt(mock_create_test_rtp_packet(100, 1000));
        detector.on_keyframe_start(keyframe_pkt.get());

        // Verify initial state
        EXPECT_EQ(100, detector.header_sn_);
        EXPECT_EQ(101, detector.lost_sn_);

        // Store packets with a gap (missing 101)
        cache.store_packet(mock_create_test_rtp_packet(100, 1000, false));
        // Skip 101 - this creates a gap
        cache.store_packet(mock_create_test_rtp_packet(102, 1000, false));
        cache.store_packet(mock_create_test_rtp_packet(103, 1000, true)); // marker bit

        uint16_t frame_start = 0, frame_end = 0;
        bool frame_ready = false;

        // Test detecting frame - should not be ready due to missing packet
        HELPER_EXPECT_SUCCESS(detector.detect_frame(103, frame_start, frame_end, frame_ready));
        EXPECT_FALSE(frame_ready); // Should not be ready due to gap

        // Verify lost_sn_ is set to the missing packet
        EXPECT_EQ(101, detector.lost_sn_);

        // Test is_lost_sn functionality
        bool is_lost = detector.is_lost_sn(101);
        EXPECT_TRUE(is_lost);                   // 101 should be the lost sequence number
        EXPECT_FALSE(detector.is_lost_sn(100)); // 100 should not be lost
        EXPECT_FALSE(detector.is_lost_sn(102)); // 102 should not be lost

        // keyframe_pkt will be automatically cleaned up by SrsUniquePtr destructor
    }
}

VOID TEST(KernelRTC2Test, SrsRtcFrameBuilderVideoFrameDetectorDetectFrameRecovery)
{
    srs_error_t err;

    // Test frame detection recovery after missing packet arrives
    if (true) {
        SrsRtcFrameBuilderVideoPacketCache cache;
        SrsRtcFrameBuilderVideoFrameDetector detector(&cache);

        // Initialize with keyframe
        SrsUniquePtr<SrsRtpPacket> keyframe_pkt(mock_create_test_rtp_packet(100, 1000));
        detector.on_keyframe_start(keyframe_pkt.get());

        // Store packets with a gap (missing 101)
        cache.store_packet(mock_create_test_rtp_packet(100, 1000, false));
        cache.store_packet(mock_create_test_rtp_packet(102, 1000, false));
        cache.store_packet(mock_create_test_rtp_packet(103, 1000, true)); // marker bit

        uint16_t frame_start = 0, frame_end = 0;
        bool frame_ready = false;

        // First detection should succeed but frame not ready due to missing packet
        HELPER_EXPECT_SUCCESS(detector.detect_frame(103, frame_start, frame_end, frame_ready));
        EXPECT_FALSE(frame_ready);

        // Now add the missing packet
        cache.store_packet(mock_create_test_rtp_packet(101, 1000, false));

        // Detection should now succeed when we receive the missing packet
        HELPER_EXPECT_SUCCESS(detector.detect_frame(101, frame_start, frame_end, frame_ready));

        // If frame is ready, verify the frame boundaries
        if (frame_ready) {
            EXPECT_EQ(100, frame_start);
            EXPECT_EQ(103, frame_end);
        }

        // keyframe_pkt will be automatically cleaned up by SrsUniquePtr destructor
    }
}

VOID TEST(KernelRTC2Test, SrsRtcFrameBuilderVideoFrameDetectorDetectNextFrame)
{
    srs_error_t err;

    // Test detect_next_frame functionality
    if (true) {
        SrsRtcFrameBuilderVideoPacketCache cache;
        SrsRtcFrameBuilderVideoFrameDetector detector(&cache);

        // Verify initial state
        EXPECT_EQ(0, detector.header_sn_);
        EXPECT_EQ(0, detector.lost_sn_);

        // Store packets for next frame
        cache.store_packet(mock_create_test_rtp_packet(200, 2000, false));
        cache.store_packet(mock_create_test_rtp_packet(201, 2000, false));
        cache.store_packet(mock_create_test_rtp_packet(202, 2000, true)); // marker bit

        uint16_t frame_start = 0, frame_end = 0;
        bool frame_ready = false;

        // Test detecting next frame starting from sequence 200
        HELPER_EXPECT_SUCCESS(detector.detect_next_frame(200, frame_start, frame_end, frame_ready));

        // Verify header_sn_ is updated by detect_next_frame
        EXPECT_EQ(200, detector.header_sn_);

        if (frame_ready) {
            EXPECT_EQ(200, frame_start);
            EXPECT_EQ(202, frame_end);
        }
    }
}

VOID TEST(KernelRTC2Test, SrsRtcFrameBuilderVideoFrameDetectorDetectNextFrameWithGaps)
{
    srs_error_t err;

    // Test detect_next_frame with missing packets
    if (true) {
        SrsRtcFrameBuilderVideoPacketCache cache;
        SrsRtcFrameBuilderVideoFrameDetector detector(&cache);

        // Store packets with a gap (missing 201)
        cache.store_packet(mock_create_test_rtp_packet(200, 2000, false));
        // Skip 201 - this creates a gap
        cache.store_packet(mock_create_test_rtp_packet(202, 2000, false));
        cache.store_packet(mock_create_test_rtp_packet(203, 2000, true)); // marker bit

        uint16_t frame_start = 0, frame_end = 0;
        bool frame_ready = false;

        // Test detecting next frame - should not be ready due to missing packet
        HELPER_EXPECT_SUCCESS(detector.detect_next_frame(200, frame_start, frame_end, frame_ready));
        EXPECT_FALSE(frame_ready); // Should not be ready due to gap
    }
}

VOID TEST(KernelRTC2Test, SrsRtcFrameBuilderVideoFrameDetectorOnKeyframeDetached)
{
    // Test on_keyframe_detached functionality
    if (true) {
        SrsRtcFrameBuilderVideoPacketCache cache;
        SrsRtcFrameBuilderVideoFrameDetector detector(&cache);

        // Initialize with keyframe
        SrsUniquePtr<SrsRtpPacket> keyframe_pkt(mock_create_test_rtp_packet(100, 1000));
        detector.on_keyframe_start(keyframe_pkt.get());

        // Verify keyframe is set
        EXPECT_EQ(1000, detector.rtp_key_frame_ts_);
        EXPECT_EQ(100, detector.header_sn_);

        // Detach keyframe
        detector.on_keyframe_detached();

        // Verify keyframe timestamp is reset to -1
        EXPECT_EQ(-1, detector.rtp_key_frame_ts_);
        // header_sn_ and lost_sn_ should remain unchanged
        EXPECT_EQ(100, detector.header_sn_);

        // After detaching, should be able to start new keyframe
        SrsUniquePtr<SrsRtpPacket> new_keyframe_pkt(mock_create_test_rtp_packet(200, 2000));
        detector.on_keyframe_start(new_keyframe_pkt.get());

        // Verify new keyframe is set
        EXPECT_EQ(2000, detector.rtp_key_frame_ts_);
        EXPECT_EQ(200, detector.header_sn_);
        EXPECT_EQ(201, detector.lost_sn_);

        // Both keyframe packets will be automatically cleaned up by SrsUniquePtr destructors
    }
}

VOID TEST(KernelRTC2Test, SrsRtcFrameBuilderVideoFrameDetectorSequenceWrapAround)
{
    srs_error_t err;

    // Test frame detection with sequence number wrap-around
    if (true) {
        SrsRtcFrameBuilderVideoPacketCache cache;
        SrsRtcFrameBuilderVideoFrameDetector detector(&cache);

        // Initialize with keyframe near sequence wrap-around
        uint16_t seq_near_max = 65534;
        SrsUniquePtr<SrsRtpPacket> keyframe_pkt(mock_create_test_rtp_packet(seq_near_max, 1000));
        detector.on_keyframe_start(keyframe_pkt.get());

        // Store packets across wrap-around boundary
        cache.store_packet(mock_create_test_rtp_packet(seq_near_max, 1000, false));
        cache.store_packet(mock_create_test_rtp_packet(65535, 1000, false));
        cache.store_packet(mock_create_test_rtp_packet(0, 1000, false)); // wrapped
        cache.store_packet(mock_create_test_rtp_packet(1, 1000, true));  // marker bit

        uint16_t frame_start = 0, frame_end = 0;
        bool frame_ready = false;

        // Test detecting frame across wrap-around
        HELPER_EXPECT_SUCCESS(detector.detect_frame(1, frame_start, frame_end, frame_ready));

        // If frame is ready, verify the frame boundaries
        if (frame_ready) {
            EXPECT_EQ(65534, frame_start);
            EXPECT_EQ(1, frame_end);
        }

        // keyframe_pkt will be automatically cleaned up by SrsUniquePtr destructor
    }
}

VOID TEST(KernelRTC2Test, SrsRtcFrameBuilderVideoFrameDetectorIsLostSnBasic)
{
    srs_error_t err;

    // Test is_lost_sn functionality
    if (true) {
        SrsRtcFrameBuilderVideoPacketCache cache;
        SrsRtcFrameBuilderVideoFrameDetector detector(&cache);

        // Initialize with keyframe
        SrsUniquePtr<SrsRtpPacket> keyframe_pkt(mock_create_test_rtp_packet(100, 1000));
        detector.on_keyframe_start(keyframe_pkt.get());

        // Verify initial lost_sn_
        EXPECT_EQ(101, detector.lost_sn_);

        // Store packets with a gap
        cache.store_packet(mock_create_test_rtp_packet(100, 1000, false));
        // Skip 101 - creates gap
        cache.store_packet(mock_create_test_rtp_packet(102, 1000, true));

        uint16_t frame_start = 0, frame_end = 0;
        bool frame_ready = false;

        // Trigger detection to set lost_sn_
        HELPER_EXPECT_SUCCESS(detector.detect_frame(102, frame_start, frame_end, frame_ready));

        // Verify lost_sn_ is set to the missing packet
        EXPECT_EQ(101, detector.lost_sn_);

        // Test is_lost_sn
        EXPECT_TRUE(detector.is_lost_sn(101));  // Should be lost
        EXPECT_FALSE(detector.is_lost_sn(100)); // Should not be lost
        EXPECT_FALSE(detector.is_lost_sn(102)); // Should not be lost
        EXPECT_FALSE(detector.is_lost_sn(103)); // Should not be lost

        // keyframe_pkt will be automatically cleaned up by SrsUniquePtr destructor
    }
}

VOID TEST(KernelRTC2Test, SrsRtcFrameBuilderVideoFrameDetectorPrivateMemberStateTracking)
{
    srs_error_t err;

    // Test detailed private member state changes during frame detection
    if (true) {
        SrsRtcFrameBuilderVideoPacketCache cache;
        SrsRtcFrameBuilderVideoFrameDetector detector(&cache);

        // Verify initial state - constructor should set video_cache_
        EXPECT_EQ(&cache, detector.video_cache_);
        EXPECT_EQ(0, detector.header_sn_);
        EXPECT_EQ(0, detector.lost_sn_);
        EXPECT_EQ(-1, detector.rtp_key_frame_ts_);

        // Test keyframe initialization
        SrsUniquePtr<SrsRtpPacket> keyframe_pkt(mock_create_test_rtp_packet(500, 5000));
        detector.on_keyframe_start(keyframe_pkt.get());

        EXPECT_EQ(5000, detector.rtp_key_frame_ts_);
        EXPECT_EQ(500, detector.header_sn_);
        EXPECT_EQ(501, detector.lost_sn_); // header_sn_ + 1

        // Test previous packet handling (sequence < header_sn_)
        cache.store_packet(mock_create_test_rtp_packet(499, 5000, false));
        cache.store_packet(mock_create_test_rtp_packet(500, 5000, false));
        cache.store_packet(mock_create_test_rtp_packet(501, 5000, true));

        uint16_t frame_start = 0, frame_end = 0;
        bool frame_ready = false;

        // Detect with previous packet (499 < 500)
        HELPER_EXPECT_SUCCESS(detector.detect_frame(499, frame_start, frame_end, frame_ready));

        // header_sn_ should be updated to the earlier packet
        EXPECT_EQ(499, detector.header_sn_);

        // keyframe_pkt will be automatically cleaned up by SrsUniquePtr destructor
    }
}

VOID TEST(KernelRTC2Test, SrsRtcFrameBuilderVideoFrameDetectorCacheOverflow)
{
    srs_error_t err;

    // Test behavior when cache overflows
    if (true) {
        SrsRtcFrameBuilderVideoPacketCache cache;
        SrsRtcFrameBuilderVideoFrameDetector detector(&cache);

        // Store more than cache_size_ packets with same timestamp and no marker bit
        // This will trigger cache overflow detection in find_next_lost_sn
        uint32_t timestamp = 1000;
        uint16_t start_sn = 100;

        // Store N packets (more than cache_size_) to guarantee overflow
        for (int i = 0; i < SrsRtcFrameBuilderVideoPacketCache::cache_size_; i++) {
            cache.store_packet(mock_create_test_rtp_packet(start_sn + i, timestamp, false));
        }

        uint16_t frame_start = 0, frame_end = 0;
        bool frame_ready = false;

        // Test detect_next_frame with cache overflow scenario
        // This should trigger find_next_lost_sn to return -2 (cache overflow)
        HELPER_EXPECT_FAILED(detector.detect_next_frame(start_sn, frame_start, frame_end, frame_ready));
        EXPECT_FALSE(frame_ready);
    }
}

VOID TEST(KernelRTC2Test, SrsRtcFrameBuilderVideoFrameDetectorPreviousPacketHandling)
{
    srs_error_t err;

    // Test handling of previous packets in the same frame
    if (true) {
        SrsRtcFrameBuilderVideoPacketCache cache;
        SrsRtcFrameBuilderVideoFrameDetector detector(&cache);

        // Initialize with keyframe
        SrsUniquePtr<SrsRtpPacket> keyframe_pkt(mock_create_test_rtp_packet(100, 1000));
        detector.on_keyframe_start(keyframe_pkt.get());

        // Store packets in order
        cache.store_packet(mock_create_test_rtp_packet(100, 1000, false));
        cache.store_packet(mock_create_test_rtp_packet(101, 1000, false));
        cache.store_packet(mock_create_test_rtp_packet(102, 1000, true));

        uint16_t frame_start = 0, frame_end = 0;
        bool frame_ready = false;

        // First detect with later packet
        HELPER_EXPECT_SUCCESS(detector.detect_frame(102, frame_start, frame_end, frame_ready));

        // Then detect with earlier packet (should handle previous packet case)
        HELPER_EXPECT_SUCCESS(detector.detect_frame(101, frame_start, frame_end, frame_ready));

        // keyframe_pkt will be automatically cleaned up by SrsUniquePtr destructor
    }
}

VOID TEST(KernelRTC2Test, SrsRtcFrameBuilderVideoFrameDetectorFragmentedFrames)
{
    srs_error_t err;

    // Test frame detection with fragmented packets (FU-A)
    if (true) {
        SrsRtcFrameBuilderVideoPacketCache cache;
        SrsRtcFrameBuilderVideoFrameDetector detector(&cache);

        // Initialize with keyframe
        SrsUniquePtr<SrsRtpPacket> keyframe_pkt(mock_create_test_rtp_packet(100, 1000));
        detector.on_keyframe_start(keyframe_pkt.get());

        // Create fragmented frame with FU-A payloads
        char payload_data[] = "test_fragmented_payload";

        // Start fragment
        SrsRtpPacket *pkt1 = mock_create_test_rtp_packet(100, 1000);
        SrsRtpFUAPayload2 *fua1 = mock_create_test_fua_payload(true, false, payload_data, sizeof(payload_data));
        pkt1->set_payload(fua1, SrsRtpPacketPayloadTypeFUA2);
        cache.store_packet(pkt1);

        // Middle fragment
        SrsRtpPacket *pkt2 = mock_create_test_rtp_packet(101, 1000);
        SrsRtpFUAPayload2 *fua2 = mock_create_test_fua_payload(false, false, payload_data, sizeof(payload_data));
        pkt2->set_payload(fua2, SrsRtpPacketPayloadTypeFUA2);
        cache.store_packet(pkt2);

        // End fragment with marker bit
        SrsRtpPacket *pkt3 = mock_create_test_rtp_packet(102, 1000, true);
        SrsRtpFUAPayload2 *fua3 = mock_create_test_fua_payload(false, true, payload_data, sizeof(payload_data));
        pkt3->set_payload(fua3, SrsRtpPacketPayloadTypeFUA2);
        cache.store_packet(pkt3);

        uint16_t frame_start = 0, frame_end = 0;
        bool frame_ready = false;

        // Test detecting fragmented frame
        HELPER_EXPECT_SUCCESS(detector.detect_frame(102, frame_start, frame_end, frame_ready));

        // keyframe_pkt will be automatically cleaned up by SrsUniquePtr destructor
    }
}

VOID TEST(KernelRTC2Test, SrsRtcFrameBuilderVideoFrameDetectorNullPacketHandling)
{
    srs_error_t err;

    // Test handling of null packets and edge cases
    if (true) {
        SrsRtcFrameBuilderVideoPacketCache cache;
        SrsRtcFrameBuilderVideoFrameDetector detector(&cache);

        // Test on_keyframe_start with null packet (should not crash)
        // Note: In real implementation, null packets should be handled gracefully

        // Test detect_frame without initialization
        uint16_t frame_start = 0, frame_end = 0;
        bool frame_ready = false;

        HELPER_EXPECT_SUCCESS(detector.detect_frame(100, frame_start, frame_end, frame_ready));
        EXPECT_FALSE(frame_ready); // Should not be ready without proper initialization

        // Test detect_next_frame without packets
        HELPER_EXPECT_SUCCESS(detector.detect_next_frame(100, frame_start, frame_end, frame_ready));
        EXPECT_FALSE(frame_ready); // Should not be ready without packets
    }
}

VOID TEST(KernelRTC2Test, SrsRtcFrameBuilderSequenceWrapAroundFix)
{
    // Test for the sequence number wraparound assertion fix
    //
    // ISSUE BACKGROUND:
    // The check_frame_complete() used srs_rtp_seq_distance(start, end) + 1
    // and asserted that the result >= 1. However, when sequence numbers wrap around (e.g., end < start),
    // srs_rtp_seq_distance can return negative values, causing the assertion to fail and crash the server.
    //
    // THE CRASH:
    // For example, if start=5 and end=3, then srs_rtp_seq_distance(5, 3) = (int16_t)(3 - 5) = -2
    // So cnt = -2 + 1 = -1, which fails the assertion cnt >= 1
    //
    // THE FIX:
    // Added validation to check if cnt <= 0 and handle it gracefully:
    // - check_frame_complete() returns false for invalid ranges

    SrsRtcFrameBuilderVideoPacketCache frame_builder;

    // Test check_frame_complete with wraparound sequence numbers
    // This should not crash and should return false for invalid ranges
    EXPECT_FALSE(frame_builder.check_frame_complete(5, 3));    // end < start
    EXPECT_FALSE(frame_builder.check_frame_complete(100, 99)); // end < start

    // Valid cases should still work
    EXPECT_TRUE(frame_builder.check_frame_complete(3, 3)); // same sequence
    EXPECT_TRUE(frame_builder.check_frame_complete(3, 5)); // normal case
}
