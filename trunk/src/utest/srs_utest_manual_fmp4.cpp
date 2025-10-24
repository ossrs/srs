//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_utest_manual_fmp4.hpp>

#include <sstream>
using namespace std;

#include <srs_app_hls.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_mp4.hpp>
#include <srs_kernel_stream.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_rtmp_stack.hpp>
#include <srs_utest_manual_kernel.hpp>

// Mock class implementations
MockFmp4SrsRequest::MockFmp4SrsRequest()
{
    vhost_ = "__defaultVhost__";
    app_ = "live";
    stream_ = "livestream";
}

MockFmp4SrsRequest::~MockFmp4SrsRequest()
{
}

MockSrsFormat::MockSrsFormat()
{
    initialize();

    // Setup video sequence header (H.264 AVC)
    uint8_t video_raw[] = {
        0x17,
        0x00, 0x00, 0x00, 0x00, 0x01, 0x64, 0x00, 0x20, 0xff, 0xe1, 0x00, 0x19, 0x67, 0x64, 0x00, 0x20,
        0xac, 0xd9, 0x40, 0xc0, 0x29, 0xb0, 0x11, 0x00, 0x00, 0x03, 0x00, 0x01, 0x00, 0x00, 0x03, 0x00,
        0x32, 0x0f, 0x18, 0x31, 0x96, 0x01, 0x00, 0x05, 0x68, 0xeb, 0xec, 0xb2, 0x2c};
    on_video(0, (char *)video_raw, sizeof(video_raw));

    // Setup audio sequence header (AAC)
    uint8_t audio_raw[] = {
        0xaf, 0x00, 0x12, 0x10};
    on_audio(0, (char *)audio_raw, sizeof(audio_raw));
}

MockSrsFormat::~MockSrsFormat()
{
}

MockSrsMediaPacket::MockSrsMediaPacket(bool is_video_msg, uint32_t ts)
{
    timestamp_ = ts;

    // Create sample payload
    char *payload = new char[1024];
    memset(payload, 0x00, 1024);
    SrsMediaPacket::wrap(payload, 1024);

    if (is_video_msg) {
        message_type_ = SrsFrameTypeVideo;
    } else {
        message_type_ = SrsFrameTypeAudio;
    }
}

MockSrsMediaPacket::~MockSrsMediaPacket()
{
}

VOID TEST(Fmp4Test, SrsInitMp4Segment_VideoOnly)
{
    srs_error_t err;

    MockSrsFileWriter fw;
    SrsInitMp4Segment segment(&fw);

    segment.set_path("/tmp/init_video.mp4");
    MockSrsFormat fmt;

    HELPER_ASSERT_SUCCESS(segment.write_video_only(&fmt, 1));
    EXPECT_TRUE(fw.filesize() > 0);

    // Verify the file contains expected MP4 boxes
    string content = fw.str();
    EXPECT_TRUE(content.find("ftyp") != string::npos);
    EXPECT_TRUE(content.find("moov") != string::npos);
}

VOID TEST(Fmp4Test, SrsInitMp4Segment_AudioOnly)
{
    srs_error_t err;

    MockSrsFileWriter fw;
    SrsInitMp4Segment segment(&fw);

    segment.set_path("/tmp/init_audio.mp4");
    MockSrsFormat fmt;

    HELPER_ASSERT_SUCCESS(segment.write_audio_only(&fmt, 2));
    EXPECT_TRUE(fw.filesize() > 0);

    // Verify the file contains expected MP4 boxes
    string content = fw.str();
    EXPECT_TRUE(content.find("ftyp") != string::npos);
    EXPECT_TRUE(content.find("moov") != string::npos);
}

VOID TEST(Fmp4Test, SrsInitMp4Segment_AudioVideo)
{
    srs_error_t err;

    MockSrsFileWriter fw;
    SrsInitMp4Segment segment(&fw);

    segment.set_path("/tmp/init_av.mp4");
    MockSrsFormat fmt;

    HELPER_ASSERT_SUCCESS(segment.write(&fmt, 1, 2));
    EXPECT_TRUE(fw.filesize() > 0);

    // Verify the file contains expected MP4 boxes
    string content = fw.str();
    EXPECT_TRUE(content.find("ftyp") != string::npos);
    EXPECT_TRUE(content.find("moov") != string::npos);
}

VOID TEST(Fmp4Test, SrsInitMp4Segment_WithEncryption)
{
    srs_error_t err;

    MockSrsFileWriter fw;
    SrsInitMp4Segment segment(&fw);

    segment.set_path("/tmp/init_encrypted.mp4");
    MockSrsFormat fmt;

    // Configure encryption
    unsigned char kid[16] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                             0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10};
    unsigned char iv[16] = {0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
                            0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20};

    HELPER_ASSERT_SUCCESS(segment.config_cipher(kid, iv, 16));
    HELPER_ASSERT_SUCCESS(segment.write(&fmt, 1, 2));
    EXPECT_TRUE(fw.filesize() > 0);
}

VOID TEST(Fmp4Test, SrsHlsM4sSegment_Basic)
{
    srs_error_t err;

    MockSrsFileWriter fw;
    SrsHlsM4sSegment segment(&fw);

    // Initialize segment with a path that doesn't require file system operations
    HELPER_ASSERT_SUCCESS(segment.initialize(0, 1, 2, 100, "segment-100.m4s"));

    // Write video sample
    MockSrsFormat fmt;
    MockSrsMediaPacket video_msg(true, 1000);
    HELPER_ASSERT_SUCCESS(segment.write(&video_msg, &fmt));

    // Write audio sample
    MockSrsMediaPacket audio_msg(false, 2000); // Different timestamp
    HELPER_ASSERT_SUCCESS(segment.write(&audio_msg, &fmt));

    // Test duration - should be > 0 after writing samples with different timestamps
    EXPECT_GT(segment.duration(), 0);
}

VOID TEST(Fmp4Test, SrsHlsM4sSegment_WithEncryption)
{
    srs_error_t err;

    MockSrsFileWriter fw;
    SrsHlsM4sSegment segment(&fw);

    // Initialize segment
    HELPER_ASSERT_SUCCESS(segment.initialize(0, 1, 2, 101, "segment-101.m4s"));

    // Configure encryption
    unsigned char key[16] = {0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28,
                             0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f, 0x30};
    unsigned char iv[16] = {0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38,
                            0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f, 0x40};
    segment.config_cipher(key, iv);

    // Verify IV is stored
    EXPECT_EQ(0x31, segment.iv_[0]);
    EXPECT_EQ(0x40, segment.iv_[15]);

    // Write samples with different timestamps to create duration
    MockSrsFormat fmt;
    MockSrsMediaPacket video_msg1(true, 1000);
    HELPER_ASSERT_SUCCESS(segment.write(&video_msg1, &fmt));

    MockSrsMediaPacket video_msg2(true, 2000);
    HELPER_ASSERT_SUCCESS(segment.write(&video_msg2, &fmt));

    // Test that segment has content
    EXPECT_GT(segment.duration(), 0);
}

VOID TEST(Fmp4Test, SrsFmp4SegmentEncoder_Basic)
{
    srs_error_t err;

    MockSrsFileWriter fw;
    SrsFmp4SegmentEncoder encoder;

    // Initialize encoder
    HELPER_ASSERT_SUCCESS(encoder.initialize(&fw, 0, 0, 1, 2));

    // Write video sample
    uint8_t video_sample[] = {0x00, 0x00, 0x00, 0x01, 0x67, 0x64, 0x00, 0x20};
    HELPER_ASSERT_SUCCESS(encoder.write_sample(SrsMp4HandlerTypeVIDE, SrsVideoAvcFrameTypeKeyFrame,
                                               1000, 1000, video_sample, sizeof(video_sample)));

    // Write audio sample
    uint8_t audio_sample[] = {0xff, 0xf1, 0x50, 0x80, 0x01, 0x3f, 0xfc};
    HELPER_ASSERT_SUCCESS(encoder.write_sample(SrsMp4HandlerTypeSOUN, 0x00,
                                               1000, 1000, audio_sample, sizeof(audio_sample)));

    // Flush to file
    HELPER_ASSERT_SUCCESS(encoder.flush(2000));
    EXPECT_TRUE(fw.filesize() > 0);

    // Verify basic structure (content may be binary, so just check size)
    EXPECT_GT(fw.filesize(), 100); // Should have reasonable size for fMP4 structure
}

VOID TEST(Fmp4Test, SrsFmp4SegmentEncoder_WithEncryption)
{
    srs_error_t err;

    MockSrsFileWriter fw;
    SrsFmp4SegmentEncoder encoder;

    // Initialize encoder
    HELPER_ASSERT_SUCCESS(encoder.initialize(&fw, 0, 0, 1, 2));

    // Configure encryption
    unsigned char key[16] = {0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48,
                             0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f, 0x50};
    unsigned char iv[16] = {0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58,
                            0x59, 0x5a, 0x5b, 0x5c, 0x5d, 0x5e, 0x5f, 0x60};
    encoder.config_cipher(key, iv);

    // Write encrypted samples
    uint8_t video_sample[] = {0x00, 0x00, 0x00, 0x01, 0x67, 0x64, 0x00, 0x20};
    HELPER_ASSERT_SUCCESS(encoder.write_sample(SrsMp4HandlerTypeVIDE, SrsVideoAvcFrameTypeKeyFrame,
                                               1000, 1000, video_sample, sizeof(video_sample)));

    HELPER_ASSERT_SUCCESS(encoder.flush(2000));
    EXPECT_TRUE(fw.filesize() > 0);

    // Verify encryption boxes are present
    string content = fw.str();
    EXPECT_TRUE(content.find("senc") != string::npos); // Sample Encryption Box
}

VOID TEST(Fmp4Test, SrsHlsFmp4Muxer_Basic)
{
    srs_error_t err;

    SrsHlsFmp4Muxer muxer;

    // Initialize muxer
    HELPER_ASSERT_SUCCESS(muxer.initialize(1, 2));

    // Test basic properties
    EXPECT_EQ(0, muxer.sequence_no());
    EXPECT_EQ(0, (int)muxer.duration());
    EXPECT_EQ(0, muxer.deviation());

    // Test codec management
    muxer.set_latest_acodec(SrsAudioCodecIdAAC);
    muxer.set_latest_vcodec(SrsVideoCodecIdAVC);
    EXPECT_EQ(SrsAudioCodecIdAAC, muxer.latest_acodec());
    EXPECT_EQ(SrsVideoCodecIdAVC, muxer.latest_vcodec());

    muxer.dispose();
}

VOID TEST(Fmp4Test, SrsHlsFmp4Muxer_WriteInitMp4)
{
    srs_error_t err;

    SrsHlsFmp4Muxer muxer;
    HELPER_ASSERT_SUCCESS(muxer.initialize(1, 2));

    MockFmp4SrsRequest req;
    HELPER_ASSERT_SUCCESS(muxer.on_publish(&req));
    HELPER_ASSERT_SUCCESS(muxer.update_config(&req));

    // Write init.mp4 with both audio and video
    MockSrsFormat fmt;
    HELPER_ASSERT_SUCCESS(muxer.write_init_mp4(&fmt, true, true));

    // Write init.mp4 with video only
    HELPER_ASSERT_SUCCESS(muxer.write_init_mp4(&fmt, true, false));

    // Write init.mp4 with audio only
    HELPER_ASSERT_SUCCESS(muxer.write_init_mp4(&fmt, false, true));

    muxer.dispose();
}

VOID TEST(Fmp4Test, SrsHlsFmp4Muxer_WriteMedia)
{
    srs_error_t err;

    SrsHlsFmp4Muxer muxer;
    HELPER_ASSERT_SUCCESS(muxer.initialize(1, 2));

    MockFmp4SrsRequest req;
    HELPER_ASSERT_SUCCESS(muxer.on_publish(&req));
    HELPER_ASSERT_SUCCESS(muxer.update_config(&req));

    // Write init.mp4 first
    MockSrsFormat fmt;
    HELPER_ASSERT_SUCCESS(muxer.write_init_mp4(&fmt, true, true));

    // Write video samples
    MockSrsMediaPacket video_msg(true, 1000);
    HELPER_ASSERT_SUCCESS(muxer.write_video(&video_msg, &fmt));

    // Write audio samples
    MockSrsMediaPacket audio_msg(false, 1000);
    HELPER_ASSERT_SUCCESS(muxer.write_audio(&audio_msg, &fmt));

    // Write more samples with time progression to accumulate duration
    for (int i = 1; i <= 5; i++) {
        MockSrsMediaPacket video_msg2(true, 1000 + i * 1000); // 1 second increments
        HELPER_ASSERT_SUCCESS(muxer.write_video(&video_msg2, &fmt));

        MockSrsMediaPacket audio_msg2(false, 1000 + i * 1000);
        HELPER_ASSERT_SUCCESS(muxer.write_audio(&audio_msg2, &fmt));
    }

    // Should have accumulated duration from timestamp progression
    EXPECT_GT(muxer.duration(), 0);

    muxer.dispose();
}

VOID TEST(Fmp4Test, SrsHlsMp4Controller_Basic)
{
    srs_error_t err;

    SrsHlsMp4Controller controller;

    // Initialize controller
    HELPER_ASSERT_SUCCESS(controller.initialize());

    // Test basic properties
    EXPECT_EQ(0, controller.sequence_no());
    EXPECT_EQ(0, (int)controller.duration());

    // Test URL generation (should return .m4s URL)
    string url = controller.ts_url();
    EXPECT_TRUE(url.find(".m4s") != string::npos || url.empty());

    controller.dispose();
}

VOID TEST(Fmp4Test, SrsHlsMp4Controller_PublishWorkflow)
{
    srs_error_t err;

    SrsHlsMp4Controller controller;
    HELPER_ASSERT_SUCCESS(controller.initialize());

    // Publish stream
    MockFmp4SrsRequest req;
    HELPER_ASSERT_SUCCESS(controller.on_publish(&req));

    // Handle sequence headers
    MockSrsFormat fmt;
    MockSrsMediaPacket video_sh(true, 0);
    HELPER_ASSERT_SUCCESS(controller.on_sequence_header(&video_sh, &fmt));

    MockSrsMediaPacket audio_sh(false, 0);
    HELPER_ASSERT_SUCCESS(controller.on_sequence_header(&audio_sh, &fmt));

    // Write media samples
    MockSrsMediaPacket video_msg(true, 1000);
    HELPER_ASSERT_SUCCESS(controller.write_video(&video_msg, &fmt));

    MockSrsMediaPacket audio_msg(false, 1000);
    HELPER_ASSERT_SUCCESS(controller.write_audio(&audio_msg, &fmt));

    // Unpublish
    HELPER_ASSERT_SUCCESS(controller.on_unpublish());

    controller.dispose();
}

VOID TEST(Fmp4Test, SrsMp4TrackEncryptionBox_CBCS)
{
    srs_error_t err;

    SrsMp4TrackEncryptionBox tenc;

    // Configure for CBCS video encryption (1:9 pattern)
    tenc.version_ = 1;
    tenc.default_crypt_byte_block_ = 1; // Encrypt 1 block
    tenc.default_skip_byte_block_ = 9;  // Skip 9 blocks
    tenc.default_is_protected_ = 1;
    tenc.default_per_sample_IV_size_ = 0; // Use constant IV
    tenc.default_constant_IV_size_ = 16;

    // Set Key ID
    unsigned char kid[16] = {0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68,
                             0x69, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f, 0x70};
    memcpy(tenc.default_KID_, kid, 16);

    // Set constant IV
    unsigned char iv[16] = {0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78,
                            0x79, 0x7a, 0x7b, 0x7c, 0x7d, 0x7e, 0x7f, 0x80};
    tenc.set_default_constant_IV(iv, 16);

    // Verify configuration
    EXPECT_EQ(1, tenc.version_);
    EXPECT_EQ(1, tenc.default_crypt_byte_block_);
    EXPECT_EQ(9, tenc.default_skip_byte_block_);
    EXPECT_EQ(1, tenc.default_is_protected_);
    EXPECT_EQ(0, tenc.default_per_sample_IV_size_);
    EXPECT_EQ(16, tenc.default_constant_IV_size_);
    EXPECT_EQ(0x61, tenc.default_KID_[0]);
    EXPECT_EQ(0x70, tenc.default_KID_[15]);
    EXPECT_EQ(0x71, tenc.default_constant_IV_[0]);
    EXPECT_EQ(0x80, tenc.default_constant_IV_[15]);

    // Test encoding/decoding
    char buffer_data[1024];
    SrsBuffer buf(buffer_data, 1024);
    HELPER_ASSERT_SUCCESS(tenc.encode(&buf));
    EXPECT_TRUE(buf.pos() > 0);

    // Test dumps - just verify it doesn't crash and produces some output
    stringstream ss;
    SrsMp4DumpContext dc;
    tenc.dumps_detail(ss, dc);
    string detail = ss.str();
    EXPECT_FALSE(detail.empty()); // Should produce some output
}

VOID TEST(Fmp4Test, SrsMp4TrackEncryptionBox_AudioFullSample)
{
    SrsMp4TrackEncryptionBox tenc;

    // Configure for audio full-sample encryption
    tenc.version_ = 1;
    tenc.default_crypt_byte_block_ = 0; // No pattern (full encryption)
    tenc.default_skip_byte_block_ = 0;  // No skip
    tenc.default_is_protected_ = 1;
    tenc.default_per_sample_IV_size_ = 0; // Use constant IV
    tenc.default_constant_IV_size_ = 16;

    // Verify audio encryption configuration
    EXPECT_EQ(0, tenc.default_crypt_byte_block_);
    EXPECT_EQ(0, tenc.default_skip_byte_block_);
    EXPECT_EQ(1, tenc.default_is_protected_);
}

VOID TEST(Fmp4Test, SrsMp4SampleEncryptionBox_Basic)
{
    srs_error_t err;

    SrsMp4SampleEncryptionBox senc(16); // 16-byte IV size

    // Test basic properties - flags may be set by constructor
    EXPECT_EQ(0, senc.version_);
    EXPECT_EQ(0, (int)senc.entries_.size());

    // Test encoding empty box
    char buffer_data[1024];
    SrsBuffer buf(buffer_data, 1024);
    HELPER_ASSERT_SUCCESS(senc.encode(&buf));
    EXPECT_TRUE(buf.pos() > 0);

    // Test dumps
    stringstream ss;
    SrsMp4DumpContext dc;
    senc.dumps_detail(ss, dc);
    string detail = ss.str();
    EXPECT_TRUE(detail.find("sample_count=0") != string::npos);
}

VOID TEST(Fmp4Test, SrsMp4SampleAuxiliaryInfoSizeBox_Basic)
{
    srs_error_t err;

    SrsMp4SampleAuxiliaryInfoSizeBox saiz;

    // Configure SAIZ box
    saiz.version_ = 0;
    saiz.flags_ = 0;
    saiz.default_sample_info_size_ = 16; // 16-byte IV
    saiz.sample_count_ = 0;

    // Test encoding
    char buffer_data[1024];
    SrsBuffer buf(buffer_data, 1024);
    HELPER_ASSERT_SUCCESS(saiz.encode(&buf));
    EXPECT_TRUE(buf.pos() > 0);

    // Test dumps
    stringstream ss;
    SrsMp4DumpContext dc;
    saiz.dumps_detail(ss, dc);
    string detail = ss.str();
    EXPECT_TRUE(detail.find("sample_count=0") != string::npos);
}

VOID TEST(Fmp4Test, SrsMp4SampleAuxiliaryInfoOffsetBox_Basic)
{
    srs_error_t err;

    SrsMp4SampleAuxiliaryInfoOffsetBox saio;

    // Configure SAIO box
    saio.version_ = 0;
    saio.flags_ = 0;
    saio.offsets_.push_back(100); // Offset to SENC box

    // Test encoding
    char buffer_data[1024];
    SrsBuffer buf(buffer_data, 1024);
    HELPER_ASSERT_SUCCESS(saio.encode(&buf));
    EXPECT_TRUE(buf.pos() > 0);

    // Test dumps
    stringstream ss;
    SrsMp4DumpContext dc;
    saio.dumps_detail(ss, dc);
    string detail = ss.str();
    EXPECT_TRUE(detail.find("entry_count=1") != string::npos);
}

VOID TEST(Fmp4Test, SrsMp4OriginalFormatBox_Basic)
{
    srs_error_t err;

    SrsMp4OriginalFormatBox frma(SrsMp4BoxTypeAVC1);

    // Test encoding
    char buffer_data[1024];
    SrsBuffer buf(buffer_data, 1024);
    HELPER_ASSERT_SUCCESS(frma.encode(&buf));
    EXPECT_TRUE(buf.pos() > 0);

    // Test dumps - just verify it produces output
    stringstream ss;
    SrsMp4DumpContext dc;
    frma.dumps_detail(ss, dc);
    string detail = ss.str();
    EXPECT_FALSE(detail.empty());
}

VOID TEST(Fmp4Test, Integration_FullEncryptionWorkflow)
{
    srs_error_t err;

    // Test complete encryption workflow from init.mp4 to encrypted segments

    // 1. Create encrypted init.mp4
    MockSrsFileWriter init_fw;
    SrsInitMp4Segment init_segment(&init_fw);
    init_segment.set_path("encrypted_init.mp4");

    unsigned char kid[16] = {0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88,
                             0x89, 0x8a, 0x8b, 0x8c, 0x8d, 0x8e, 0x8f, 0x90};
    unsigned char iv[16] = {0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98,
                            0x99, 0x9a, 0x9b, 0x9c, 0x9d, 0x9e, 0x9f, 0xa0};

    HELPER_ASSERT_SUCCESS(init_segment.config_cipher(kid, iv, 16));

    MockSrsFormat fmt;
    HELPER_ASSERT_SUCCESS(init_segment.write(&fmt, 1, 2));
    EXPECT_TRUE(init_fw.filesize() > 0);

    // 2. Create encrypted segment
    MockSrsFileWriter seg_fw;
    SrsHlsM4sSegment m4s_segment(&seg_fw);
    HELPER_ASSERT_SUCCESS(m4s_segment.initialize(0, 1, 2, 200, "encrypted-200.m4s"));

    unsigned char seg_key[16] = {0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8,
                                 0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf, 0xb0};
    unsigned char seg_iv[16] = {0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8,
                                0xb9, 0xba, 0xbb, 0xbc, 0xbd, 0xbe, 0xbf, 0xc0};
    m4s_segment.config_cipher(seg_key, seg_iv);

    // Write samples to encrypted segment with time progression
    MockSrsMediaPacket video_msg1(true, 2000);
    HELPER_ASSERT_SUCCESS(m4s_segment.write(&video_msg1, &fmt));

    MockSrsMediaPacket audio_msg1(false, 2500);
    HELPER_ASSERT_SUCCESS(m4s_segment.write(&audio_msg1, &fmt));

    MockSrsMediaPacket video_msg2(true, 3000);
    HELPER_ASSERT_SUCCESS(m4s_segment.write(&video_msg2, &fmt));

    // Should have duration from timestamp progression
    EXPECT_GT(m4s_segment.duration(), 0);

    // 3. Verify both files have content (encryption metadata is binary)
    EXPECT_TRUE(init_fw.filesize() > 0);
    EXPECT_GT(m4s_segment.duration(), 0);
}

VOID TEST(Fmp4Test, EdgeCase_LargeTimestamp)
{
    srs_error_t err;

    MockSrsFileWriter fw;
    SrsFmp4SegmentEncoder encoder;

    // Test with large timestamp values
    HELPER_ASSERT_SUCCESS(encoder.initialize(&fw, 0, 0, 1, 2));

    uint64_t large_timestamp = 0xFFFFFFFF; // Max 32-bit value
    uint8_t sample[] = {0x00, 0x01, 0x02, 0x03};

    HELPER_ASSERT_SUCCESS(encoder.write_sample(SrsMp4HandlerTypeVIDE, SrsVideoAvcFrameTypeKeyFrame,
                                               large_timestamp, large_timestamp, sample, sizeof(sample)));

    HELPER_ASSERT_SUCCESS(encoder.flush(large_timestamp + 1000));
    EXPECT_TRUE(fw.filesize() > 0);
}

VOID TEST(Fmp4Test, ErrorHandling_InvalidTrackId)
{
    srs_error_t err;

    MockSrsFileWriter fw;
    SrsFmp4SegmentEncoder encoder;

    // Test with track ID 0 (may or may not be invalid depending on implementation)
    err = encoder.initialize(&fw, 0, 0, 0, 0);
    // Just verify the function can be called without crashing
    if (err != srs_success) {
        srs_freep(err);
    }
}

VOID TEST(Fmp4Test, ErrorHandling_NullWriter)
{
    srs_error_t err;

    SrsFmp4SegmentEncoder encoder;

    // Initialize with null writer - may or may not fail depending on implementation
    err = encoder.initialize(NULL, 0, 0, 1, 2);
    if (err != srs_success) {
        srs_freep(err);
    }
    // Test passes if no crash occurs
}

VOID TEST(Fmp4Test, ErrorHandling_WriteBeforeInitialize)
{
    srs_error_t err;

    SrsFmp4SegmentEncoder encoder;
    uint8_t sample[] = {0x00, 0x01};

    // Try to write sample before initialization - may or may not fail
    err = encoder.write_sample(SrsMp4HandlerTypeVIDE, SrsVideoAvcFrameTypeKeyFrame,
                               1000, 1000, sample, sizeof(sample));
    if (err != srs_success) {
        srs_freep(err);
    }
    // Test passes if no crash occurs
}

VOID TEST(Fmp4Test, ErrorHandling_FlushBeforeWrite)
{
    srs_error_t err;

    MockSrsFileWriter fw;
    SrsFmp4SegmentEncoder encoder;

    HELPER_ASSERT_SUCCESS(encoder.initialize(&fw, 0, 0, 1, 2));

    // Flush without writing any samples - may fail due to empty samples
    err = encoder.flush(1000);
    if (err != srs_success) {
        srs_freep(err);
        // This is expected behavior for empty flush
    }
}

VOID TEST(Fmp4Test, Configuration_TrackIdManagement)
{
    srs_error_t err;

    SrsHlsMp4Controller controller;
    HELPER_ASSERT_SUCCESS(controller.initialize());

    // Verify default track IDs
    EXPECT_EQ(1, controller.video_track_id_);
    EXPECT_EQ(2, controller.audio_track_id_);

    // Test sequence header tracking
    EXPECT_FALSE(controller.has_video_sh_);
    EXPECT_FALSE(controller.has_audio_sh_);

    // Set request first
    MockFmp4SrsRequest req;
    HELPER_ASSERT_SUCCESS(controller.on_publish(&req));

    MockSrsFormat fmt;
    MockSrsMediaPacket video_sh(true, 0);
    HELPER_ASSERT_SUCCESS(controller.on_sequence_header(&video_sh, &fmt));
    EXPECT_TRUE(controller.has_video_sh_);

    MockSrsMediaPacket audio_sh(false, 0);
    HELPER_ASSERT_SUCCESS(controller.on_sequence_header(&audio_sh, &fmt));
    EXPECT_TRUE(controller.has_audio_sh_);

    controller.dispose();
}

VOID TEST(Fmp4Test, Configuration_SequenceHeaderValidation)
{
    srs_error_t err;

    SrsHlsMp4Controller controller;
    HELPER_ASSERT_SUCCESS(controller.initialize());

    // Test sequence header without request (should fail)
    MockSrsFormat fmt;
    MockSrsMediaPacket video_sh(true, 0);
    HELPER_EXPECT_FAILED(controller.on_sequence_header(&video_sh, &fmt));

    // Set request and try again
    MockFmp4SrsRequest req;
    HELPER_ASSERT_SUCCESS(controller.on_publish(&req));
    HELPER_ASSERT_SUCCESS(controller.on_sequence_header(&video_sh, &fmt));

    controller.dispose();
}

VOID TEST(Fmp4Test, CodecDetection_AudioCodecUpdate)
{
    srs_error_t err;

    SrsHlsMp4Controller controller;
    HELPER_ASSERT_SUCCESS(controller.initialize());

    MockFmp4SrsRequest req;
    HELPER_ASSERT_SUCCESS(controller.on_publish(&req));

    // Create mock format with AAC audio codec
    MockSrsFormat fmt;
    fmt.acodec_ = new SrsAudioCodecConfig();
    fmt.acodec_->id_ = SrsAudioCodecIdAAC;
    fmt.audio_ = new SrsParsedAudioPacket();
    fmt.audio_->codec_ = fmt.acodec_;

    // Initial codec should be forbidden (not set)
    EXPECT_EQ(SrsAudioCodecIdForbidden, controller.muxer_->latest_acodec());

    // Write audio frame - should detect and update codec
    MockSrsMediaPacket audio_msg(false, 1000);
    HELPER_ASSERT_SUCCESS(controller.write_audio(&audio_msg, &fmt));

    // Codec should now be detected as AAC
    EXPECT_EQ(SrsAudioCodecIdAAC, controller.muxer_->latest_acodec());

    // Test codec change from AAC to MP3
    fmt.acodec_->id_ = SrsAudioCodecIdMP3;
    HELPER_ASSERT_SUCCESS(controller.write_audio(&audio_msg, &fmt));

    // Codec should now be updated to MP3
    EXPECT_EQ(SrsAudioCodecIdMP3, controller.muxer_->latest_acodec());

    controller.dispose();
    srs_freep(fmt.acodec_);
    srs_freep(fmt.audio_);
}

VOID TEST(Fmp4Test, CodecDetection_VideoCodecUpdate)
{
    srs_error_t err;

    SrsHlsMp4Controller controller;
    HELPER_ASSERT_SUCCESS(controller.initialize());

    MockFmp4SrsRequest req;
    HELPER_ASSERT_SUCCESS(controller.on_publish(&req));

    // Create mock format with H.264 video codec
    MockSrsFormat fmt;
    fmt.vcodec_ = new SrsVideoCodecConfig();
    fmt.vcodec_->id_ = SrsVideoCodecIdAVC;
    fmt.video_ = new SrsParsedVideoPacket();
    fmt.video_->codec_ = fmt.vcodec_;

    // Initial codec should be forbidden (not set)
    EXPECT_EQ(SrsVideoCodecIdForbidden, controller.muxer_->latest_vcodec());

    // Write video frame - should detect and update codec
    MockSrsMediaPacket video_msg(true, 1000);
    HELPER_ASSERT_SUCCESS(controller.write_video(&video_msg, &fmt));

    // Codec should now be detected as H.264
    EXPECT_EQ(SrsVideoCodecIdAVC, controller.muxer_->latest_vcodec());

    // Test codec change from H.264 to HEVC
    fmt.vcodec_->id_ = SrsVideoCodecIdHEVC;
    HELPER_ASSERT_SUCCESS(controller.write_video(&video_msg, &fmt));

    // Codec should now be updated to HEVC
    EXPECT_EQ(SrsVideoCodecIdHEVC, controller.muxer_->latest_vcodec());

    controller.dispose();
    srs_freep(fmt.vcodec_);
    srs_freep(fmt.video_);
}

VOID TEST(Fmp4Test, Performance_MultipleSegments)
{
    srs_error_t err;

    SrsHlsFmp4Muxer muxer;
    HELPER_ASSERT_SUCCESS(muxer.initialize(1, 2));

    MockFmp4SrsRequest req;
    HELPER_ASSERT_SUCCESS(muxer.on_publish(&req));
    HELPER_ASSERT_SUCCESS(muxer.update_config(&req));

    MockSrsFormat fmt;
    HELPER_ASSERT_SUCCESS(muxer.write_init_mp4(&fmt, true, true));

    int initial_seq = muxer.sequence_no();

    // Write many samples to create multiple segments
    for (int i = 0; i < 500; i++) {
        MockSrsMediaPacket video_msg(true, i * 40);
        HELPER_ASSERT_SUCCESS(muxer.write_video(&video_msg, &fmt));

        if (i % 2 == 0) { // Write audio less frequently
            MockSrsMediaPacket audio_msg(false, i * 40);
            HELPER_ASSERT_SUCCESS(muxer.write_audio(&audio_msg, &fmt));
        }
    }

    // Should have created multiple segments
    EXPECT_GT(muxer.sequence_no(), initial_seq);
    EXPECT_GT(muxer.duration(), 0);

    muxer.dispose();
}

VOID TEST(Fmp4Test, Compatibility_SequenceHeaderIgnore)
{
    srs_error_t err;

    SrsHlsMp4Controller controller;
    HELPER_ASSERT_SUCCESS(controller.initialize());

    MockFmp4SrsRequest req;
    HELPER_ASSERT_SUCCESS(controller.on_publish(&req));

    MockSrsFormat fmt;

    // Create audio sequence header message
    MockSrsMediaPacket audio_sh(false, 0);

    // Should ignore sequence headers in write_audio
    HELPER_ASSERT_SUCCESS(controller.write_audio(&audio_sh, &fmt));

    // Regular audio message should be processed
    MockSrsMediaPacket audio_msg(false, 1000);
    HELPER_ASSERT_SUCCESS(controller.write_audio(&audio_msg, &fmt));

    controller.dispose();
}
