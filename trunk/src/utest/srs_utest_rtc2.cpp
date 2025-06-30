//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//
#include <srs_utest_rtc2.hpp>

#include <srs_kernel_error.hpp>
#include <srs_core_autofree.hpp>
#include <srs_app_rtc_source.hpp>
#include <srs_kernel_codec.hpp>

#include <srs_utest_service.hpp>

#include <vector>
#include <chrono>
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
        unknown_video.name_ = "H266";  // Future codec not yet supported
        EXPECT_EQ(SrsVideoCodecIdReserved, unknown_video.codec(true));
        
        // Completely unknown video codec
        SrsCodecPayload invalid_video;
        invalid_video.name_ = "UNKNOWN_VIDEO";
        EXPECT_EQ(SrsVideoCodecIdReserved, invalid_video.codec(true));
    }
    
    if (true) {
        // Unknown audio codec
        SrsCodecPayload unknown_audio;
        unknown_audio.name_ = "FLAC";  // Not supported in this context
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
        EXPECT_EQ(SrsVideoCodecIdAVC, codec2);  // Still returns cached H264 value

        // Manual cache reset allows new parsing
        payload.codec_ = -1;
        int8_t codec3 = payload.codec(true);
        EXPECT_EQ(SrsVideoCodecIdHEVC, codec3);  // Now returns HEVC
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
        long_name.name_ = std::string(1000, 'A');  // 1000 character string
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
        auto start = std::chrono::high_resolution_clock::now();
        int8_t codec1 = payload.codec(true);
        auto end1 = std::chrono::high_resolution_clock::now();

        // Subsequent calls should be faster (cached)
        auto start2 = std::chrono::high_resolution_clock::now();
        int8_t codec2 = payload.codec(true);
        auto end2 = std::chrono::high_resolution_clock::now();

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
        SrsCodecPayload* copied = original.copy();

        // The copied payload should have the same name but uncached codec
        EXPECT_EQ(original.name_, copied->name_);
        EXPECT_EQ(original.pt_, copied->pt_);
        EXPECT_EQ(original.sample_, copied->sample_);
        EXPECT_EQ(-1, copied->codec_);  // Should not copy cached value

        // But calling codec() should return the same result
        EXPECT_EQ(SrsVideoCodecIdAVC, copied->codec(true));

        srs_freep(copied);
    }
}
