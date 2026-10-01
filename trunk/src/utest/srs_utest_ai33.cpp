//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//
#include <srs_utest_ai33.hpp>

#include <srs_kernel_error.hpp>

using namespace std;

// Append the n bits of v, most significant first, one 0/1 entry per bit.
static void utest_avc_write_bits(std::vector<uint8_t> &bits, uint32_t v, int n)
{
    for (int i = n - 1; i >= 0; i--) {
        bits.push_back((v >> i) & 0x01);
    }
}

// ue(v), 9.1 Parsing process for Exp-Golomb codes: leadingZeroBits zeros, then codeNum + 1 in
// leadingZeroBits + 1 bits.
static void utest_avc_write_ue(std::vector<uint8_t> &bits, uint32_t code_num)
{
    uint32_t v = code_num + 1;
    int nb_bits = 0;
    for (uint32_t t = v; t; t >>= 1) {
        nb_bits++;
    }
    utest_avc_write_bits(bits, 0, nb_bits - 1);
    utest_avc_write_bits(bits, v, nb_bits);
}

// se(v), Table 9-3: codeNum k maps to (-1)^(k+1) * Ceil(k / 2), so 1 is 1, -1 is 2, 2 is 3, -2 is 4.
static void utest_avc_write_se(std::vector<uint8_t> &bits, int32_t v)
{
    utest_avc_write_ue(bits, (v > 0) ? (2 * v - 1) : (-2 * v));
}

// The fields of a hand-built High profile SPS, the others are fixed by utest_avc_build_sps_rbsp.
struct UtestAvcSps {
    uint8_t profile_idc_;
    int chroma_format_idc_;
    bool seq_scaling_matrix_present_flag_;
    // The coded delta_scale values of scaling list i, empty when seq_scaling_list_present_flag[i] is 0.
    std::vector<std::vector<int> > scaling_lists_;
    int pic_width_in_mbs_minus1_;
    int pic_height_in_map_units_minus1_;
    int frame_crop_bottom_offset_;

    UtestAvcSps()
    {
        profile_idc_ = 100;
        chroma_format_idc_ = 1;
        seq_scaling_matrix_present_flag_ = false;
        pic_width_in_mbs_minus1_ = 0;
        pic_height_in_map_units_minus1_ = 0;
        frame_crop_bottom_offset_ = 0;
    }
};

// Build the SPS RBSP, from profile_idc to rbsp_trailing_bits(), following 7.3.2.1.1 and 7.3.2.1.1.1 of
// ISO_IEC_14496-10-AVC-2012.pdf, page 62.
static std::vector<char> utest_avc_build_sps_rbsp(const UtestAvcSps &sps)
{
    std::vector<uint8_t> bits;
    utest_avc_write_bits(bits, sps.profile_idc_, 8);
    utest_avc_write_bits(bits, 0, 8);  // constraint_set0_flag to constraint_set5_flag, reserved_zero_2bits
    utest_avc_write_bits(bits, 31, 8); // level_idc
    utest_avc_write_ue(bits, 0);       // seq_parameter_set_id

    // Only the High profiles carry the chroma format and the scaling matrix, other profiles such as Main
    // go straight to log2_max_frame_num_minus4.
    uint8_t p = sps.profile_idc_;
    bool high = (p == 100 || p == 110 || p == 122 || p == 244 || p == 44 || p == 83 || p == 86 || p == 118 || p == 128);
    if (high) {
        utest_avc_write_ue(bits, sps.chroma_format_idc_);
        if (sps.chroma_format_idc_ == 3) {
            utest_avc_write_bits(bits, 0, 1); // separate_colour_plane_flag
        }
        utest_avc_write_ue(bits, 0);      // bit_depth_luma_minus8
        utest_avc_write_ue(bits, 0);      // bit_depth_chroma_minus8
        utest_avc_write_bits(bits, 0, 1); // qpprime_y_zero_transform_bypass_flag
        utest_avc_write_bits(bits, sps.seq_scaling_matrix_present_flag_ ? 1 : 0, 1);
    }
    if (high && sps.seq_scaling_matrix_present_flag_) {
        int nb_lists = (sps.chroma_format_idc_ != 3) ? 8 : 12;
        for (int i = 0; i < nb_lists; i++) {
            std::vector<int> deltas;
            if (i < (int)sps.scaling_lists_.size()) {
                deltas = sps.scaling_lists_[i];
            }

            // seq_scaling_list_present_flag[i], then the delta_scale values of scaling_list().
            utest_avc_write_bits(bits, deltas.empty() ? 0 : 1, 1);
            for (int j = 0; j < (int)deltas.size(); j++) {
                utest_avc_write_se(bits, deltas[j]);
            }
        }
    }

    utest_avc_write_ue(bits, 0); // log2_max_frame_num_minus4
    utest_avc_write_ue(bits, 0); // pic_order_cnt_type
    utest_avc_write_ue(bits, 2); // log2_max_pic_order_cnt_lsb_minus4
    utest_avc_write_ue(bits, 1); // max_num_ref_frames
    utest_avc_write_bits(bits, 0, 1); // gaps_in_frame_num_value_allowed_flag
    utest_avc_write_ue(bits, sps.pic_width_in_mbs_minus1_);
    utest_avc_write_ue(bits, sps.pic_height_in_map_units_minus1_);
    utest_avc_write_bits(bits, 1, 1); // frame_mbs_only_flag
    utest_avc_write_bits(bits, 1, 1); // direct_8x8_inference_flag
    utest_avc_write_bits(bits, sps.frame_crop_bottom_offset_ ? 1 : 0, 1); // frame_cropping_flag
    if (sps.frame_crop_bottom_offset_) {
        utest_avc_write_ue(bits, 0); // frame_crop_left_offset
        utest_avc_write_ue(bits, 0); // frame_crop_right_offset
        utest_avc_write_ue(bits, 0); // frame_crop_top_offset
        utest_avc_write_ue(bits, sps.frame_crop_bottom_offset_);
    }
    utest_avc_write_bits(bits, 0, 1); // vui_parameters_present_flag

    // rbsp_trailing_bits(): rbsp_stop_one_bit, then rbsp_alignment_zero_bit to the byte boundary.
    bits.push_back(1);
    while (bits.size() % 8) {
        bits.push_back(0);
    }

    std::vector<char> rbsp(bits.size() / 8, 0);
    for (int i = 0; i < (int)bits.size(); i++) {
        rbsp[i / 8] |= (char)(bits[i] << (7 - i % 8));
    }
    return rbsp;
}

// The delta_scale values of a scaling list where nextScale never becomes 0, so all size values are coded.
static std::vector<int> utest_avc_full_scaling_list(int size)
{
    std::vector<int> deltas;
    for (int j = 0; j < size; j++) {
        deltas.push_back((j % 2) ? -1 : 2);
    }
    return deltas;
}

// The SPS NALU of a real 1280x720 High profile camera with all eight scaling lists present, from #4756.
static const uint8_t utest_avc_camera_sps[] = {
    0x67, 0x64, 0x00, 0x1f, 0xad, 0x84, 0x05, 0x45, 0x62, 0xb8, 0xac, 0x54,
    0x74, 0x20, 0x2a, 0x2b, 0x15, 0xc5, 0x62, 0xa3, 0xa1, 0x01, 0x51, 0x58,
    0xae, 0x2b, 0x15, 0x1d, 0x08, 0x0a, 0x8a, 0xc5, 0x71, 0x58, 0xa8, 0xe8,
    0x40, 0x54, 0x56, 0x2b, 0x8a, 0xc5, 0x47, 0x42, 0x02, 0xa2, 0xb1, 0x5c,
    0x56, 0x2a, 0x3a, 0x10, 0x24, 0x99, 0x39, 0x3c, 0x9f, 0x27, 0xe4, 0xfe,
    0x4f, 0xc9, 0xf2, 0x79, 0xb9, 0xb3, 0x4d, 0x08, 0x12, 0x4c, 0x9c, 0x9e,
    0x4f, 0x93, 0xf2, 0x7f, 0x27, 0xe4, 0xf9, 0x3c, 0xdc, 0xd9, 0xa6, 0x4d,
    0x00, 0xa0, 0x0b, 0x74, 0xdc, 0x04, 0x04, 0x04, 0x08};

// Reproduce #4756: an SPS with seq_scaling_matrix_present_flag=1 carries a scaling_list() after each
// seq_scaling_list_present_flag[i] that is set, see 7.3.2.1.1 of ISO_IEC_14496-10-AVC-2012.pdf. The
// picture size comes after the lists, so the parser must read through them. This is the SPS of a real
// 1280x720 High profile camera with all eight lists present, from the issue; FFmpeg's trace_headers
// reads pic_width_in_mbs_minus1=79 and pic_height_in_map_units_minus1=44 from it.
VOID TEST(ReproduceIssue4756, CameraSpsWithEightScalingListsIs1280x720)
{
    srs_error_t err;

    const uint8_t *sps = utest_avc_camera_sps;
    SrsFormat format;
    HELPER_EXPECT_SUCCESS(format.initialize());
    format.vcodec_->sequenceParameterSetNALUnit_.assign((char *)sps, (char *)sps + sizeof(utest_avc_camera_sps));

    HELPER_EXPECT_SUCCESS(format.avc_demux_sps());
    EXPECT_EQ(1280, format.vcodec_->width_);
    EXPECT_EQ(720, format.vcodec_->height_);
}

// Reproduce #4756 with all eight lists fully coded: 16 delta_scale values for each 4x4 list (i < 6) and 64
// for each 8x8 list. The picture is 1920x1080, 120x68 macroblocks cropped by 8 rows at the bottom.
VOID TEST(ReproduceIssue4756, FullyCodedScalingListsAreSkipped)
{
    srs_error_t err;

    UtestAvcSps sps;
    sps.seq_scaling_matrix_present_flag_ = true;
    for (int i = 0; i < 8; i++) {
        sps.scaling_lists_.push_back(utest_avc_full_scaling_list((i < 6) ? 16 : 64));
    }
    sps.pic_width_in_mbs_minus1_ = 119;
    sps.pic_height_in_map_units_minus1_ = 67;
    sps.frame_crop_bottom_offset_ = 4;
    std::vector<char> rbsp = utest_avc_build_sps_rbsp(sps);

    SrsFormat format;
    HELPER_EXPECT_SUCCESS(format.initialize());
    HELPER_EXPECT_SUCCESS(format.avc_demux_sps_rbsp(&rbsp[0], (int)rbsp.size()));
    EXPECT_EQ(1920, format.vcodec_->width_);
    EXPECT_EQ(1080, format.vcodec_->height_);
}

// Reproduce #4756 with useDefaultScalingMatrixFlag: a first delta_scale of -8 makes nextScale 0 at j=0, so
// each list is a single coded value and the default list of Table 7-2 applies.
VOID TEST(ReproduceIssue4756, DefaultScalingMatrixListsAreSkipped)
{
    srs_error_t err;

    UtestAvcSps sps;
    sps.seq_scaling_matrix_present_flag_ = true;
    for (int i = 0; i < 8; i++) {
        sps.scaling_lists_.push_back(std::vector<int>(1, -8));
    }
    sps.pic_width_in_mbs_minus1_ = 79;
    sps.pic_height_in_map_units_minus1_ = 44;
    std::vector<char> rbsp = utest_avc_build_sps_rbsp(sps);

    SrsFormat format;
    HELPER_EXPECT_SUCCESS(format.initialize());
    HELPER_EXPECT_SUCCESS(format.avc_demux_sps_rbsp(&rbsp[0], (int)rbsp.size()));
    EXPECT_EQ(1280, format.vcodec_->width_);
    EXPECT_EQ(720, format.vcodec_->height_);
}

// Reproduce #4756 with lists that end early: once nextScale becomes 0 the remaining values repeat lastScale
// and are not coded. List 0 codes 8 to 12 to 14 to 0 in three values, list 6 codes 8 to 16 to 0 in two, and
// the other lists are not present. The picture is 640x480.
VOID TEST(ReproduceIssue4756, EarlyTerminatedScalingListsAreSkipped)
{
    srs_error_t err;

    UtestAvcSps sps;
    sps.seq_scaling_matrix_present_flag_ = true;
    sps.scaling_lists_.resize(8);
    sps.scaling_lists_[0].push_back(4);
    sps.scaling_lists_[0].push_back(2);
    sps.scaling_lists_[0].push_back(-14);
    sps.scaling_lists_[6].push_back(8);
    sps.scaling_lists_[6].push_back(-16);
    sps.pic_width_in_mbs_minus1_ = 39;
    sps.pic_height_in_map_units_minus1_ = 29;
    std::vector<char> rbsp = utest_avc_build_sps_rbsp(sps);

    SrsFormat format;
    HELPER_EXPECT_SUCCESS(format.initialize());
    HELPER_EXPECT_SUCCESS(format.avc_demux_sps_rbsp(&rbsp[0], (int)rbsp.size()));
    EXPECT_EQ(640, format.vcodec_->width_);
    EXPECT_EQ(480, format.vcodec_->height_);
}

// Reproduce #4756 for 4:4:4: chroma_format_idc=3 has twelve lists, and lists 6 to 11 are all 8x8. Lists 1,
// 7, 10 and 11 are present. The picture is 352x288, with no cropping.
VOID TEST(ReproduceIssue4756, TwelveScalingListsOf444AreSkipped)
{
    srs_error_t err;

    UtestAvcSps sps;
    sps.profile_idc_ = 244;
    sps.chroma_format_idc_ = 3;
    sps.seq_scaling_matrix_present_flag_ = true;
    sps.scaling_lists_.resize(12);
    sps.scaling_lists_[1] = utest_avc_full_scaling_list(16);
    sps.scaling_lists_[7] = utest_avc_full_scaling_list(64);
    sps.scaling_lists_[10] = utest_avc_full_scaling_list(64);
    sps.scaling_lists_[11] = utest_avc_full_scaling_list(64);
    sps.pic_width_in_mbs_minus1_ = 21;
    sps.pic_height_in_map_units_minus1_ = 17;
    std::vector<char> rbsp = utest_avc_build_sps_rbsp(sps);

    SrsFormat format;
    HELPER_EXPECT_SUCCESS(format.initialize());
    HELPER_EXPECT_SUCCESS(format.avc_demux_sps_rbsp(&rbsp[0], (int)rbsp.size()));
    EXPECT_EQ(352, format.vcodec_->width_);
    EXPECT_EQ(288, format.vcodec_->height_);
}

// The flags without any list: seq_scaling_matrix_present_flag=1 but every seq_scaling_list_present_flag[i]
// is 0, so only the eight flag bits follow. This already parses correctly and passes before the fix; it
// guards the case where no scaling_list() is coded.
VOID TEST(ReproduceIssue4756, ScalingMatrixWithoutListsIsUnchanged)
{
    srs_error_t err;

    UtestAvcSps sps;
    sps.seq_scaling_matrix_present_flag_ = true;
    sps.pic_width_in_mbs_minus1_ = 79;
    sps.pic_height_in_map_units_minus1_ = 44;
    std::vector<char> rbsp = utest_avc_build_sps_rbsp(sps);

    SrsFormat format;
    HELPER_EXPECT_SUCCESS(format.initialize());
    HELPER_EXPECT_SUCCESS(format.avc_demux_sps_rbsp(&rbsp[0], (int)rbsp.size()));
    EXPECT_EQ(1280, format.vcodec_->width_);
    EXPECT_EQ(720, format.vcodec_->height_);
}

// No scaling matrix, seq_scaling_matrix_present_flag=0, the common High profile SPS. This already parses
// correctly and passes before the fix; it also checks the SPS builder of these tests.
VOID TEST(ReproduceIssue4756, NoScalingMatrixIsUnchanged)
{
    srs_error_t err;

    UtestAvcSps sps;
    sps.pic_width_in_mbs_minus1_ = 119;
    sps.pic_height_in_map_units_minus1_ = 67;
    sps.frame_crop_bottom_offset_ = 4;
    std::vector<char> rbsp = utest_avc_build_sps_rbsp(sps);

    SrsFormat format;
    HELPER_EXPECT_SUCCESS(format.initialize());
    HELPER_EXPECT_SUCCESS(format.avc_demux_sps_rbsp(&rbsp[0], (int)rbsp.size()));
    EXPECT_EQ(1920, format.vcodec_->width_);
    EXPECT_EQ(1080, format.vcodec_->height_);
}

// Reproduce #4756 with lists of every kind in one SPS: list 0, 5 and 7 select the default matrix with a single
// delta_scale of -8, lists 1 and 4 are not present, list 2 and 6 are fully coded, and list 3 ends early after
// three values. The picture is 1280x720.
VOID TEST(ReproduceIssue4756, MixedScalingListsAreSkipped)
{
    srs_error_t err;

    UtestAvcSps sps;
    sps.seq_scaling_matrix_present_flag_ = true;
    sps.scaling_lists_.resize(8);
    sps.scaling_lists_[0].push_back(-8);
    sps.scaling_lists_[2] = utest_avc_full_scaling_list(16);
    sps.scaling_lists_[3].push_back(4);
    sps.scaling_lists_[3].push_back(2);
    sps.scaling_lists_[3].push_back(-14);
    sps.scaling_lists_[5].push_back(-8);
    sps.scaling_lists_[6] = utest_avc_full_scaling_list(64);
    sps.scaling_lists_[7].push_back(-8);
    sps.pic_width_in_mbs_minus1_ = 79;
    sps.pic_height_in_map_units_minus1_ = 44;
    std::vector<char> rbsp = utest_avc_build_sps_rbsp(sps);

    SrsFormat format;
    HELPER_EXPECT_SUCCESS(format.initialize());
    HELPER_EXPECT_SUCCESS(format.avc_demux_sps_rbsp(&rbsp[0], (int)rbsp.size()));
    EXPECT_EQ(1280, format.vcodec_->width_);
    EXPECT_EQ(720, format.vcodec_->height_);
}

// Reproduce #4756 for 4:2:2: High 4:2:2 profile (profile_idc=122) with chroma_format_idc=2 still has eight
// lists, only 4:4:4 has twelve. All eight are fully coded. The picture is 640x480, with no cropping.
VOID TEST(ReproduceIssue4756, EightScalingListsOf422AreSkipped)
{
    srs_error_t err;

    UtestAvcSps sps;
    sps.profile_idc_ = 122;
    sps.chroma_format_idc_ = 2;
    sps.seq_scaling_matrix_present_flag_ = true;
    for (int i = 0; i < 8; i++) {
        sps.scaling_lists_.push_back(utest_avc_full_scaling_list((i < 6) ? 16 : 64));
    }
    sps.pic_width_in_mbs_minus1_ = 39;
    sps.pic_height_in_map_units_minus1_ = 29;
    std::vector<char> rbsp = utest_avc_build_sps_rbsp(sps);

    SrsFormat format;
    HELPER_EXPECT_SUCCESS(format.initialize());
    HELPER_EXPECT_SUCCESS(format.avc_demux_sps_rbsp(&rbsp[0], (int)rbsp.size()));
    EXPECT_EQ(640, format.vcodec_->width_);
    EXPECT_EQ(480, format.vcodec_->height_);
}

// An SPS that ends in the middle of a scaling list must fail, instead of reading the picture size from
// whatever bits are left. The fields before the lists take 32 bits and list 0 ends at bit 97, so the first 12
// bytes (96 bits) of an SPS with eight fully coded lists end inside list 0.
VOID TEST(ReproduceIssue4756, TruncatedScalingListFails)
{
    srs_error_t err;

    UtestAvcSps sps;
    sps.seq_scaling_matrix_present_flag_ = true;
    for (int i = 0; i < 8; i++) {
        sps.scaling_lists_.push_back(utest_avc_full_scaling_list((i < 6) ? 16 : 64));
    }
    sps.pic_width_in_mbs_minus1_ = 79;
    sps.pic_height_in_map_units_minus1_ = 44;
    std::vector<char> rbsp = utest_avc_build_sps_rbsp(sps);

    SrsFormat format;
    HELPER_EXPECT_SUCCESS(format.initialize());
    err = format.avc_demux_sps_rbsp(&rbsp[0], 12);
    EXPECT_TRUE(err != srs_success);
    EXPECT_TRUE(srs_error_desc(err).find("scaling_list") != string::npos) << srs_error_desc(err);
    srs_freep(err);
}

// Main profile (profile_idc=77) has no chroma_format_idc and no scaling matrix fields, so the parser goes
// from seq_parameter_set_id straight to log2_max_frame_num_minus4. The picture is 640x360, coded as 640x368
// and cropped by 8 rows at the bottom. This already parses correctly and passes before the fix.
VOID TEST(ReproduceIssue4756, MainProfileWithoutHighFieldsIsUnchanged)
{
    srs_error_t err;

    UtestAvcSps sps;
    sps.profile_idc_ = 77;
    sps.pic_width_in_mbs_minus1_ = 39;
    sps.pic_height_in_map_units_minus1_ = 22;
    sps.frame_crop_bottom_offset_ = 4;
    std::vector<char> rbsp = utest_avc_build_sps_rbsp(sps);

    SrsFormat format;
    HELPER_EXPECT_SUCCESS(format.initialize());
    HELPER_EXPECT_SUCCESS(format.avc_demux_sps_rbsp(&rbsp[0], (int)rbsp.size()));
    EXPECT_EQ(640, format.vcodec_->width_);
    EXPECT_EQ(360, format.vcodec_->height_);
}

// Reproduce #4756 the way a publisher sends it: the camera SPS padded with trailing zero bytes, in an RTMP AVC
// sequence header. The zero bytes after rbsp_trailing_bits() are never read, so the picture is still 1280x720.
VOID TEST(ReproduceIssue4756, CameraSpsWithTrailingZerosInSequenceHeader)
{
    srs_error_t err;

    std::vector<char> sps(utest_avc_camera_sps, utest_avc_camera_sps + sizeof(utest_avc_camera_sps));
    sps.insert(sps.end(), 8, 0);
    const char pps[] = {0x68, (char)0xee, 0x38, (char)0xb0};

    // FLV video tag: keyframe and AVC (0x17), AVCPacketType=0 (sequence header), CompositionTime=0.
    std::vector<char> tag;
    tag.push_back(0x17);
    tag.insert(tag.end(), 4, 0);
    // AVCDecoderConfigurationRecord, ISO_IEC_14496-15-AVC-format-2012.pdf, 5.2.4.1: configurationVersion,
    // AVCProfileIndication, profile_compatibility and AVCLevelIndication from the SPS, lengthSizeMinusOne=3,
    // one SPS and one PPS, each with a 16-bit length.
    tag.push_back(0x01);
    tag.insert(tag.end(), sps.begin() + 1, sps.begin() + 4);
    tag.push_back((char)0xff);
    tag.push_back((char)0xe1);
    tag.push_back((char)(sps.size() >> 8));
    tag.push_back((char)(sps.size() & 0xff));
    tag.insert(tag.end(), sps.begin(), sps.end());
    tag.push_back(0x01);
    tag.push_back(0x00);
    tag.push_back((char)sizeof(pps));
    tag.insert(tag.end(), pps, pps + sizeof(pps));

    SrsFormat format;
    HELPER_EXPECT_SUCCESS(format.initialize());
    HELPER_EXPECT_SUCCESS(format.on_video(0, &tag[0], (int)tag.size()));
    ASSERT_TRUE(format.vcodec() != NULL);
    EXPECT_EQ(1280, format.vcodec()->width_);
    EXPECT_EQ(720, format.vcodec()->height_);
}
