//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//
#include <srs_utest_ai39.hpp>

#include <srs_kernel_buffer.hpp>
#include <srs_kernel_codec.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_mp4.hpp>
#include <srs_kernel_packet.hpp>
#include <srs_kernel_ts.hpp>

#include <string.h>

#include <sstream>
#include <string>
#include <vector>

using namespace std;

void UtestBitWriter::u(uint32_t v, int n)
{
    for (int i = n - 1; i >= 0; i--) {
        bits_.push_back((v >> i) & 0x01);
    }
}

void UtestBitWriter::ue(uint32_t v)
{
    uint32_t code = v + 1;
    int nb_bits = 0;
    for (uint32_t t = code; t; t >>= 1) {
        nb_bits++;
    }
    u(0, nb_bits - 1);
    u(code, nb_bits);
}

void UtestBitWriter::se(int32_t v)
{
    ue((v > 0) ? (2 * v - 1) : (-2 * v));
}

int UtestBitWriter::size() const
{
    return (int)bits_.size();
}

std::vector<char> UtestBitWriter::bytes(int n) const
{
    std::vector<char> data((n + 7) / 8, 0);
    for (int i = 0; i < n; i++) {
        data[i / 8] |= (char)(bits_[i] << (7 - i % 8));
    }
    return data;
}

std::vector<char> UtestBitWriter::bytes() const
{
    return bytes(size());
}

typedef srs_error_t (*UtestRbspParser)(SrsFormat *format, char *data, int size);

// Parse every bit prefix of a parameter set, zero padded to a byte, and append the description of each
// error to errors. The variants of a test place the field boundaries at every bit offset of a byte, so
// some prefix ends right before each field.
static void utest_parse_prefixes(const UtestBitWriter &b, UtestRbspParser parser, std::string &errors)
{
    for (int n = 0; n < b.size(); n++) {
        std::vector<char> data = b.bytes(n);
        char empty = 0;

        SrsFormat format;
        srs_error_t err = format.initialize();
        EXPECT_TRUE(err == srs_success);
        srs_freep(err);

        err = parser(&format, data.empty() ? &empty : &data[0], (int)data.size());
        if (err != srs_success) {
            errors += srs_error_desc(err);
            errors += "\n";
            srs_freep(err);
        }
    }
}

static void utest_expect_errors(const std::string &errors, const char **names, int nb_names)
{
    for (int i = 0; i < nb_names; i++) {
        EXPECT_NE(std::string::npos, errors.find(names[i])) << "no error for " << names[i];
    }
}

static srs_error_t utest_parse_hevc_vps(SrsFormat *format, char *data, int size)
{
    return format->hevc_demux_vps_rbsp(data, size);
}

static srs_error_t utest_parse_hevc_sps(SrsFormat *format, char *data, int size)
{
    return format->hevc_demux_sps_rbsp(data, size);
}

static srs_error_t utest_parse_hevc_pps(SrsFormat *format, char *data, int size)
{
    return format->hevc_demux_pps_rbsp(data, size);
}

static srs_error_t utest_parse_avc_sps(SrsFormat *format, char *data, int size)
{
    return format->avc_demux_sps_rbsp(data, size);
}

// profile_tier_level(1, 0) of a Main profile stream: general_profile_idc 1 compatible with Main and Main 10,
// progressive frames, level 3.1. Main 10 compatibility selects the 7 + 1 + 35 bits layout of the 43 bits.
static void utest_hevc_write_ptl_main(UtestBitWriter &b)
{
    b.u(0, 2);          // general_profile_space
    b.u(0, 1);          // general_tier_flag
    b.u(1, 5);          // general_profile_idc
    b.u(0x60000000, 32); // general_profile_compatibility_flag[1] and [2]
    b.u(0x9, 4);        // progressive_source, interlaced_source, non_packed_constraint, frame_only_constraint
    b.u(0, 11);
    b.u(0, 32);  // the 43 constraint and reserved bits
    b.u(0, 1);   // general_inbld_flag
    b.u(93, 8);  // general_level_idc
}

// The fields of video_parameter_set_rbsp() up to vps_num_layer_sets_minus1, where the parser stops.
struct UtestHevcVps {
    int max_sub_layers_minus1_;
    bool ordering_info_present_;
    uint32_t max_dec_pic_buffering_minus1_;

    UtestHevcVps()
    {
        max_sub_layers_minus1_ = 0;
        ordering_info_present_ = false;
        max_dec_pic_buffering_minus1_ = 4;
    }
};

static void utest_hevc_write_vps_rbsp(UtestBitWriter &b, const UtestHevcVps &v)
{
    b.u(3, 4);      // vps_video_parameter_set_id
    b.u(1, 1);      // vps_base_layer_internal_flag
    b.u(1, 1);      // vps_base_layer_available_flag
    b.u(0, 6);      // vps_max_layers_minus1
    b.u(v.max_sub_layers_minus1_, 3);
    b.u(1, 1);      // vps_temporal_id_nesting_flag
    b.u(0xffff, 16); // vps_reserved_0xffff_16bits
    utest_hevc_write_ptl_main(b);
    if (v.max_sub_layers_minus1_ > 0) {
        b.u(0, 2 * v.max_sub_layers_minus1_); // sub_layer_profile_present_flag, sub_layer_level_present_flag
        b.u(0, 2 * (8 - v.max_sub_layers_minus1_)); // reserved_zero_2bits
    }

    b.u(v.ordering_info_present_ ? 1 : 0, 1);
    for (int i = (v.ordering_info_present_ ? 0 : v.max_sub_layers_minus1_); i <= v.max_sub_layers_minus1_; i++) {
        b.ue(v.max_dec_pic_buffering_minus1_ + i);
        b.ue(2); // vps_max_num_reorder_pics
        b.ue(0); // vps_max_latency_increase_plus1
    }
    b.u(5, 6); // vps_max_layer_id
    b.ue(1);   // vps_num_layer_sets_minus1
}

// The fields of seq_parameter_set_rbsp() up to log2_max_pic_order_cnt_lsb_minus4, where the parser stops.
struct UtestHevcSps {
    uint32_t sps_id_;
    uint32_t chroma_format_idc_;
    int separate_colour_plane_flag_;
    uint32_t width_;
    uint32_t height_;
    bool conformance_window_;
    uint32_t conf_left_;
    uint32_t conf_right_;
    uint32_t conf_top_;
    uint32_t conf_bottom_;
    uint32_t bit_depth_luma_minus8_;

    UtestHevcSps()
    {
        sps_id_ = 0;
        chroma_format_idc_ = 1;
        separate_colour_plane_flag_ = 0;
        width_ = 1920;
        height_ = 1088;
        conformance_window_ = true;
        conf_left_ = 0;
        conf_right_ = 0;
        conf_top_ = 0;
        conf_bottom_ = 4;
        bit_depth_luma_minus8_ = 0;
    }
};

static void utest_hevc_write_sps_rbsp(UtestBitWriter &b, const UtestHevcSps &s)
{
    b.u(0, 4); // sps_video_parameter_set_id
    b.u(0, 3); // sps_max_sub_layers_minus1
    b.u(1, 1); // sps_temporal_id_nesting_flag
    utest_hevc_write_ptl_main(b);
    b.ue(s.sps_id_);
    b.ue(s.chroma_format_idc_);
    if (s.chroma_format_idc_ == 3) {
        b.u(s.separate_colour_plane_flag_, 1);
    }
    b.ue(s.width_);
    b.ue(s.height_);
    b.u(s.conformance_window_ ? 1 : 0, 1);
    if (s.conformance_window_) {
        b.ue(s.conf_left_);
        b.ue(s.conf_right_);
        b.ue(s.conf_top_);
        b.ue(s.conf_bottom_);
    }
    b.ue(s.bit_depth_luma_minus8_);
    b.ue(0); // bit_depth_chroma_minus8
    b.ue(4); // log2_max_pic_order_cnt_lsb_minus4
}

// The fields of pic_parameter_set_rbsp() up to the pps extension flags, where the parser stops.
struct UtestHevcPps {
    uint32_t pps_id_;
    bool cu_qp_delta_enabled_;
    bool tiles_;
    bool uniform_spacing_;
    bool deblocking_control_;
    bool deblocking_disabled_;
    bool scaling_list_;
    bool extension_;

    UtestHevcPps()
    {
        pps_id_ = 5;
        cu_qp_delta_enabled_ = true;
        tiles_ = true;
        uniform_spacing_ = false;
        deblocking_control_ = true;
        deblocking_disabled_ = false;
        scaling_list_ = false;
        extension_ = true;
    }
};

// scaling_list_data() of 7.3.4: the first 4x4 list and the second 16x16 list are coded, the others
// predict from a reference list.
static void utest_hevc_write_scaling_list_data(UtestBitWriter &b)
{
    for (int size_id = 0; size_id < 4; size_id++) {
        for (int matrix_id = 0; matrix_id < 6; matrix_id += (size_id == 3) ? 3 : 1) {
            if (size_id == 0 && matrix_id == 0) {
                b.u(1, 1); // scaling_list_pred_mode_flag
                for (int i = 0; i < 16; i++) {
                    b.se(1); // scaling_list_delta_coef
                }
            } else if (size_id == 2 && matrix_id == 1) {
                b.u(1, 1);
                b.se(5); // scaling_list_dc_coef_minus8
                for (int i = 0; i < 64; i++) {
                    b.se(0);
                }
            } else {
                b.u(0, 1);
                b.ue((size_id == 1 && matrix_id == 2) ? 2 : 0); // scaling_list_pred_matrix_id_delta
            }
        }
    }
}

// The PPS fields before pps_scaling_list_data_present_flag.
static void utest_hevc_write_pps_head(UtestBitWriter &b, const UtestHevcPps &p)
{
    b.ue(p.pps_id_);
    b.ue(2);   // pps_seq_parameter_set_id
    b.u(1, 1); // dependent_slice_segments_enabled_flag
    b.u(0, 1); // output_flag_present_flag
    b.u(5, 3); // num_extra_slice_header_bits
    b.u(1, 1); // sign_data_hiding_enabled_flag
    b.u(0, 1); // cabac_init_present_flag
    b.ue(3);   // num_ref_idx_l0_default_active_minus1
    b.ue(1);   // num_ref_idx_l1_default_active_minus1
    b.se(-4);  // init_qp_minus26
    b.u(1, 1); // constrained_intra_pred_flag
    b.u(0, 1); // transform_skip_enabled_flag
    b.u(p.cu_qp_delta_enabled_ ? 1 : 0, 1);
    if (p.cu_qp_delta_enabled_) {
        b.ue(2); // diff_cu_qp_delta_depth
    }
    b.se(3);   // pps_cb_qp_offset
    b.se(-2);  // pps_cr_qp_offset
    b.u(1, 1); // pps_slice_chroma_qp_offsets_present_flag
    b.u(0, 1); // weighted_pred_flag
    b.u(1, 1); // weighted_bipred_flag
    b.u(0, 1); // transquant_bypass_enabled_flag
    b.u(p.tiles_ ? 1 : 0, 1);
    b.u(1, 1); // entropy_coding_sync_enabled_flag
    if (p.tiles_) {
        b.ue(2); // num_tile_columns_minus1
        b.ue(1); // num_tile_rows_minus1
        b.u(p.uniform_spacing_ ? 1 : 0, 1);
        if (!p.uniform_spacing_) {
            b.ue(10); // column_width_minus1[0]
            b.ue(20); // column_width_minus1[1]
            b.ue(30); // row_height_minus1[0]
        }
        b.u(1, 1); // loop_filter_across_tiles_enabled_flag
    }
    b.u(1, 1); // pps_loop_filter_across_slices_enabled_flag
    b.u(p.deblocking_control_ ? 1 : 0, 1);
    if (p.deblocking_control_) {
        b.u(1, 1); // deblocking_filter_override_enabled_flag
        b.u(p.deblocking_disabled_ ? 1 : 0, 1);
        if (!p.deblocking_disabled_) {
            b.se(-3); // pps_beta_offset_div2
            b.se(2);  // pps_tc_offset_div2
        }
    }
}

static void utest_hevc_write_pps_rbsp(UtestBitWriter &b, const UtestHevcPps &p)
{
    utest_hevc_write_pps_head(b, p);
    b.u(p.scaling_list_ ? 1 : 0, 1);
    if (p.scaling_list_) {
        utest_hevc_write_scaling_list_data(b);
    }
    b.u(1, 1); // lists_modification_present_flag
    b.ue(3);   // log2_parallel_merge_level_minus2
    b.u(0, 1); // slice_segment_header_extension_present_flag
    b.u(p.extension_ ? 1 : 0, 1);
    if (p.extension_) {
        b.u(1, 1);   // pps_range_extension_flag
        b.u(0, 1);   // pps_multilayer_extension_flag
        b.u(1, 1);   // pps_3d_extension_flag
        b.u(0, 1);   // pps_scc_extension_flag
        b.u(0xa, 4); // pps_extension_4bits
    }
}

// The fields of an H.264 seq_parameter_set_data() up to vui_parameters_present_flag, where the parser stops.
struct UtestAvcSpsFields {
    uint8_t profile_idc_;
    uint32_t sps_id_;
    uint32_t chroma_format_idc_;
    bool scaling_matrix_;
    uint32_t poc_type_;
    uint32_t num_ref_frames_in_poc_cycle_;
    uint32_t width_mbs_minus1_;
    uint32_t height_map_units_minus1_;
    bool frame_mbs_only_;
    bool cropping_;
    uint32_t crop_left_;
    uint32_t crop_right_;
    uint32_t crop_top_;
    uint32_t crop_bottom_;

    UtestAvcSpsFields()
    {
        profile_idc_ = 100;
        sps_id_ = 0;
        chroma_format_idc_ = 1;
        scaling_matrix_ = true;
        poc_type_ = 1;
        num_ref_frames_in_poc_cycle_ = 2;
        width_mbs_minus1_ = 79;
        height_map_units_minus1_ = 22;
        frame_mbs_only_ = false;
        cropping_ = true;
        crop_left_ = 0;
        crop_right_ = 0;
        crop_top_ = 0;
        crop_bottom_ = 8;
    }
};

static void utest_avc_write_sps_rbsp(UtestBitWriter &b, const UtestAvcSpsFields &s)
{
    b.u(s.profile_idc_, 8);
    b.u(0, 8);  // constraint_set flags and reserved_zero_2bits
    b.u(40, 8); // level_idc
    b.ue(s.sps_id_);
    if (s.profile_idc_ == 100) {
        b.ue(s.chroma_format_idc_);
        if (s.chroma_format_idc_ == 3) {
            b.u(0, 1); // separate_colour_plane_flag
        }
        b.ue(0);   // bit_depth_luma_minus8
        b.ue(0);   // bit_depth_chroma_minus8
        b.u(0, 1); // qpprime_y_zero_transform_bypass_flag
        b.u(s.scaling_matrix_ ? 1 : 0, 1);
        if (s.scaling_matrix_) {
            int nb_lists = (s.chroma_format_idc_ != 3) ? 8 : 12;
            for (int i = 0; i < nb_lists; i++) {
                // Only the first list is present, a delta_scale of -8 makes nextScale 0 so it ends at once.
                b.u(i == 0 ? 1 : 0, 1);
                if (i == 0) {
                    b.se(-8);
                }
            }
        }
    }
    b.ue(0); // log2_max_frame_num_minus4
    b.ue(s.poc_type_);
    if (s.poc_type_ == 0) {
        b.ue(2); // log2_max_pic_order_cnt_lsb_minus4
    } else if (s.poc_type_ == 1) {
        b.u(1, 1); // delta_pic_order_always_zero_flag
        b.ue(1);   // offset_for_non_ref_pic
        b.ue(2);   // offset_for_top_to_bottom_field
        b.ue(s.num_ref_frames_in_poc_cycle_);
        for (uint32_t i = 0; i < s.num_ref_frames_in_poc_cycle_; i++) {
            b.ue(i + 1); // offset_for_ref_frame[i]
        }
    }
    b.ue(1);   // max_num_ref_frames
    b.u(0, 1); // gaps_in_frame_num_value_allowed_flag
    b.ue(s.width_mbs_minus1_);
    b.ue(s.height_map_units_minus1_);
    b.u(s.frame_mbs_only_ ? 1 : 0, 1);
    if (!s.frame_mbs_only_) {
        b.u(1, 1); // mb_adaptive_frame_field_flag
    }
    b.u(1, 1); // direct_8x8_inference_flag
    b.u(s.cropping_ ? 1 : 0, 1);
    if (s.cropping_) {
        b.ue(s.crop_left_);
        b.ue(s.crop_right_);
        b.ue(s.crop_top_);
        b.ue(s.crop_bottom_);
    }
    b.u(0, 1); // vui_parameters_present_flag
}

VOID TEST(KernelHevcPtlTest, RangeExtensionProfileReadsConstraintFlags)
{
    srs_error_t err;

    // general_profile_idc 4 is the format range extensions profile, which codes nine constraint flags
    // then reserved_zero_34bits.
    UtestBitWriter b;
    b.u(1, 2);           // general_profile_space
    b.u(1, 1);           // general_tier_flag
    b.u(4, 5);           // general_profile_idc
    b.u(0x08000000, 32); // general_profile_compatibility_flag[4]
    b.u(0xa, 4);         // progressive 1, interlaced 0, non_packed 1, frame_only 0
    b.u(0x155, 9);       // max_12bit 1, max_10bit 0, max_8bit 1, 422 0, 420 1, mono 0, intra 1, one_pic 0, lower_rate 1
    b.u(2, 2);
    b.u(0x12345678, 32); // reserved_zero_34bits
    b.u(1, 1);           // general_inbld_flag
    b.u(120, 8);         // general_level_idc

    std::vector<char> data = b.bytes();
    SrsBuffer stream(&data[0], (int)data.size());
    SrsBitBuffer bs(&stream);
    SrsHevcProfileTierLevel ptl;
    SrsFormat format;
    HELPER_EXPECT_SUCCESS(format.hevc_demux_rbsp_ptl(&bs, &ptl, 1, 0));

    EXPECT_EQ(1, ptl.general_profile_space_);
    EXPECT_EQ(1, ptl.general_tier_flag_);
    EXPECT_EQ(4, ptl.general_profile_idc_);
    EXPECT_EQ(1, ptl.general_profile_compatibility_flag_[4]);
    EXPECT_EQ(0, ptl.general_profile_compatibility_flag_[5]);
    EXPECT_EQ(1, ptl.general_progressive_source_flag_);
    EXPECT_EQ(0, ptl.general_interlaced_source_flag_);
    EXPECT_EQ(1, ptl.general_non_packed_constraint_flag_);
    EXPECT_EQ(0, ptl.general_frame_only_constraint_flag_);
    EXPECT_EQ(1, ptl.general_max_12bit_constraint_flag_);
    EXPECT_EQ(0, ptl.general_max_10bit_constraint_flag_);
    EXPECT_EQ(1, ptl.general_max_8bit_constraint_flag_);
    EXPECT_EQ(0, ptl.general_max_422chroma_constraint_flag_);
    EXPECT_EQ(1, ptl.general_max_420chroma_constraint_flag_);
    EXPECT_EQ(0, ptl.general_max_monochrome_constraint_flag_);
    EXPECT_EQ(1, ptl.general_intra_constraint_flag_);
    EXPECT_EQ(0, ptl.general_one_picture_only_constraint_flag_);
    EXPECT_EQ(1, ptl.general_lower_bit_rate_constraint_flag_);
    EXPECT_EQ(((uint64_t)2 << 32) | 0x12345678, ptl.general_reserved_zero_34bits_);
    EXPECT_EQ(1, ptl.general_inbld_flag_);
    EXPECT_EQ(120, ptl.general_level_idc_);
    EXPECT_EQ(0, stream.left());
}

VOID TEST(KernelHevcPtlTest, ScreenContentProfileReadsFourteenBitFlag)
{
    srs_error_t err;

    // general_profile_idc 9 is the screen content coding profile, which adds max_14bit_constraint_flag
    // before reserved_zero_33bits.
    UtestBitWriter b;
    b.u(0, 2);
    b.u(0, 1);
    b.u(9, 5);
    b.u(0, 32);
    b.u(0, 4);
    b.u(0x1ff, 9);       // all nine constraint flags
    b.u(1, 1);           // general_max_14bit_constraint_flag
    b.u(1, 1);
    b.u(0xcafebabe, 32); // reserved_zero_33bits
    b.u(0, 1);           // general_inbld_flag
    b.u(153, 8);         // general_level_idc

    std::vector<char> data = b.bytes();
    SrsBuffer stream(&data[0], (int)data.size());
    SrsBitBuffer bs(&stream);
    SrsHevcProfileTierLevel ptl;
    SrsFormat format;
    HELPER_EXPECT_SUCCESS(format.hevc_demux_rbsp_ptl(&bs, &ptl, 1, 0));

    EXPECT_EQ(9, ptl.general_profile_idc_);
    EXPECT_EQ(1, ptl.general_max_12bit_constraint_flag_);
    EXPECT_EQ(1, ptl.general_lower_bit_rate_constraint_flag_);
    EXPECT_EQ(1, ptl.general_max_14bit_constraint_flag_);
    EXPECT_EQ(((uint64_t)1 << 32) | 0xcafebabe, ptl.general_reserved_zero_33bits_);
    EXPECT_EQ(0, ptl.general_inbld_flag_);
    EXPECT_EQ(153, ptl.general_level_idc_);
}

VOID TEST(KernelHevcPtlTest, SubLayersReadProfileAndLevel)
{
    srs_error_t err;

    // profile_tier_level(0, 4): no general profile, four sub-layers. Sub-layer 0 is a range extensions
    // profile with a level, sub-layer 1 profile 6 without a level, sub-layer 2 Main with a level, and
    // sub-layer 3 carries nothing.
    UtestBitWriter b;
    b.u(90, 8); // general_level_idc
    b.u(3, 2);  // sub-layer 0: profile and level present
    b.u(2, 2);  // sub-layer 1: profile present
    b.u(3, 2);  // sub-layer 2: profile and level present
    b.u(0, 2);  // sub-layer 3: nothing
    b.u(0, 8);  // reserved_zero_2bits for i from 4 to 7

    // Sub-layer 0, profile 4: nine constraint flags then reserved_zero_34bits, and inbld_flag.
    b.u(2, 2);   // sub_layer_profile_space
    b.u(1, 1);   // sub_layer_tier_flag
    b.u(4, 5);   // sub_layer_profile_idc
    b.u(0, 32);  // sub_layer_profile_compatibility_flag
    b.u(0x5, 4); // progressive 0, interlaced 1, non_packed 0, frame_only 1
    b.u(0x0aa, 9);
    b.u(1, 2);
    b.u(0x11223344, 32);
    b.u(1, 1);   // sub_layer_inbld_flag
    b.u(60, 8);  // sub_layer_level_idc

    // Sub-layer 1, profile 6: the same 43 bits layout, but reserved_zero_bit instead of inbld_flag.
    b.u(0, 2);
    b.u(0, 1);
    b.u(6, 5);
    b.u(0, 32);
    b.u(0, 4);
    b.u(0, 9);
    b.u(3, 2);
    b.u(0x55667788, 32);
    b.u(1, 1); // sub_layer_reserved_zero_bit

    // Sub-layer 2, Main profile: reserved_zero_43bits and inbld_flag.
    b.u(0, 2);
    b.u(0, 1);
    b.u(1, 5);
    b.u(0, 32);
    b.u(0, 4);
    b.u(0x7ff, 11);
    b.u(0x99aabbcc, 32);
    b.u(0, 1);
    b.u(93, 8);

    std::vector<char> data = b.bytes();
    SrsBuffer stream(&data[0], (int)data.size());
    SrsBitBuffer bs(&stream);
    SrsHevcProfileTierLevel ptl;
    SrsFormat format;
    HELPER_EXPECT_SUCCESS(format.hevc_demux_rbsp_ptl(&bs, &ptl, 0, 4));

    EXPECT_EQ(90, ptl.general_level_idc_);
    ASSERT_EQ(4u, ptl.sub_layer_level_idc_.size());

    EXPECT_EQ(2, ptl.sub_layer_profile_space_[0]);
    EXPECT_EQ(1, ptl.sub_layer_tier_flag_[0]);
    EXPECT_EQ(4, ptl.sub_layer_profile_idc_[0]);
    EXPECT_EQ(0, ptl.sub_layer_progressive_source_flag_[0]);
    EXPECT_EQ(1, ptl.sub_layer_interlaced_source_flag_[0]);
    EXPECT_EQ(0, ptl.sub_layer_non_packed_constraint_flag_[0]);
    EXPECT_EQ(1, ptl.sub_layer_frame_only_constraint_flag_[0]);
    EXPECT_EQ(0, ptl.sub_layer_max_12bit_constraint_flag_[0]);
    EXPECT_EQ(1, ptl.sub_layer_max_10bit_constraint_flag_[0]);
    EXPECT_EQ(0, ptl.sub_layer_max_8bit_constraint_flag_[0]);
    EXPECT_EQ(1, ptl.sub_layer_max_422chroma_constraint_flag_[0]);
    EXPECT_EQ(0, ptl.sub_layer_max_420chroma_constraint_flag_[0]);
    EXPECT_EQ(1, ptl.sub_layer_max_monochrome_constraint_flag_[0]);
    EXPECT_EQ(0, ptl.sub_layer_intra_constraint_flag_[0]);
    EXPECT_EQ(1, ptl.sub_layer_one_picture_only_constraint_flag_[0]);
    EXPECT_EQ(0, ptl.sub_layer_lower_bit_rate_constraint_flag_[0]);
    EXPECT_EQ(((uint64_t)1 << 32) | 0x11223344, ptl.sub_layer_reserved_zero_34bits_[0]);
    EXPECT_EQ(1, ptl.sub_layer_inbld_flag_[0]);
    EXPECT_EQ(60, ptl.sub_layer_level_idc_[0]);

    EXPECT_EQ(6, ptl.sub_layer_profile_idc_[1]);
    EXPECT_EQ(((uint64_t)3 << 32) | 0x55667788, ptl.sub_layer_reserved_zero_34bits_[1]);
    EXPECT_EQ(1, ptl.sub_layer_reserved_zero_bit_[1]);
    EXPECT_EQ(0, ptl.sub_layer_level_idc_[1]);

    EXPECT_EQ(1, ptl.sub_layer_profile_idc_[2]);
    EXPECT_EQ(((uint64_t)0x7ff << 32) | 0x99aabbcc, ptl.sub_layer_reserved_zero_43bits_[2]);
    EXPECT_EQ(0, ptl.sub_layer_inbld_flag_[2]);
    EXPECT_EQ(93, ptl.sub_layer_level_idc_[2]);

    EXPECT_EQ(0, ptl.sub_layer_profile_present_flag_[3]);
    EXPECT_EQ(0, ptl.sub_layer_level_idc_[3]);
    EXPECT_EQ(0, stream.left());
}

// Each case is profile_tier_level() cut on a byte boundary right before the named field.
VOID TEST(KernelHevcPtlTest, TruncatedFieldsFail)
{
    srs_error_t err;

    if (true) {
        // No profile, and no byte for general_level_idc.
        char data[1] = {0};
        SrsBuffer stream(data, 0);
        SrsBitBuffer bs(&stream);
        SrsHevcProfileTierLevel ptl;
        SrsFormat format;
        err = format.hevc_demux_rbsp_ptl(&bs, &ptl, 0, 0);
        EXPECT_NE(std::string::npos, srs_error_desc(err).find("ptl level requires 8"));
        srs_freep(err);
    }

    if (true) {
        // general_level_idc, then nothing for the flags of sub-layer 0.
        char data[1] = {93};
        SrsBuffer stream(data, sizeof(data));
        SrsBitBuffer bs(&stream);
        SrsHevcProfileTierLevel ptl;
        SrsFormat format;
        err = format.hevc_demux_rbsp_ptl(&bs, &ptl, 0, 1);
        EXPECT_NE(std::string::npos, srs_error_desc(err).find("ptl present_flag requires 2"));
        srs_freep(err);
    }

    if (true) {
        // general_level_idc, the flags of sub-layer 0 and three reserved_zero_2bits of the seven.
        char data[2] = {93, 0};
        SrsBuffer stream(data, sizeof(data));
        SrsBitBuffer bs(&stream);
        SrsHevcProfileTierLevel ptl;
        SrsFormat format;
        err = format.hevc_demux_rbsp_ptl(&bs, &ptl, 0, 1);
        EXPECT_NE(std::string::npos, srs_error_desc(err).find("ptl reserved_zero requires 2"));
        srs_freep(err);
    }

    if (true) {
        // Sub-layer 0 has a profile, but the data ends after the reserved bits.
        char data[3] = {93, (char)0x80, 0};
        SrsBuffer stream(data, sizeof(data));
        SrsBitBuffer bs(&stream);
        SrsHevcProfileTierLevel ptl;
        SrsFormat format;
        err = format.hevc_demux_rbsp_ptl(&bs, &ptl, 0, 1);
        EXPECT_NE(std::string::npos, srs_error_desc(err).find("ptl sub_layer_profile requires 88"));
        srs_freep(err);
    }

    if (true) {
        // Sub-layer 0 has a level, but the data ends after the reserved bits.
        char data[3] = {93, 0x40, 0};
        SrsBuffer stream(data, sizeof(data));
        SrsBitBuffer bs(&stream);
        SrsHevcProfileTierLevel ptl;
        SrsFormat format;
        err = format.hevc_demux_rbsp_ptl(&bs, &ptl, 0, 1);
        EXPECT_NE(std::string::npos, srs_error_desc(err).find("ptl sub_layer_level requires 8"));
        srs_freep(err);
    }
}

VOID TEST(KernelHevcVpsTest, ParsesSubLayerOrderingInfo)
{
    srs_error_t err;

    UtestHevcVps v;
    v.max_sub_layers_minus1_ = 2;
    v.ordering_info_present_ = true;
    UtestBitWriter b;
    utest_hevc_write_vps_rbsp(b, v);
    std::vector<char> data = b.bytes();

    SrsFormat format;
    HELPER_EXPECT_SUCCESS(format.initialize());
    HELPER_EXPECT_SUCCESS(format.hevc_demux_vps_rbsp(&data[0], (int)data.size()));

    SrsHevcDecoderConfigurationRecord *dec = &format.vcodec_->hevc_dec_conf_record_;
    SrsHevcRbspVps *vps = &dec->vps_table_[3];
    EXPECT_EQ(3, vps->vps_video_parameter_set_id_);
    EXPECT_EQ(1, vps->vps_base_layer_internal_flag_);
    EXPECT_EQ(1, vps->vps_base_layer_available_flag_);
    EXPECT_EQ(2, vps->vps_max_sub_layers_minus1_);
    EXPECT_EQ(1, vps->vps_temporal_id_nesting_flag_);
    EXPECT_EQ(0xffff, vps->vps_reserved_0xffff_16bits_);
    EXPECT_EQ(1, vps->vps_sub_layer_ordering_info_present_flag_);
    EXPECT_EQ(4u, vps->vps_max_dec_pic_buffering_minus1_[0]);
    EXPECT_EQ(5u, vps->vps_max_dec_pic_buffering_minus1_[1]);
    EXPECT_EQ(6u, vps->vps_max_dec_pic_buffering_minus1_[2]);
    EXPECT_EQ(2u, vps->vps_max_num_reorder_pics_[2]);
    EXPECT_EQ(0u, vps->vps_max_latency_increase_plus1_[2]);
    EXPECT_EQ(5, vps->vps_max_layer_id_);
    EXPECT_EQ(1u, vps->vps_num_layer_sets_minus1_);
    EXPECT_EQ(1, dec->general_profile_idc_);
    EXPECT_EQ(93, dec->general_level_idc_);
}

VOID TEST(KernelHevcVpsTest, TruncatedFieldsFail)
{
    std::string errors;
    for (int max_sub = 0; max_sub < 2; max_sub++) {
        for (int ordering = 0; ordering < 2; ordering++) {
            uint32_t dec_values[] = {0, 1, 3, 7};
            for (int i = 0; i < 4; i++) {
                UtestHevcVps v;
                v.max_sub_layers_minus1_ = max_sub;
                v.ordering_info_present_ = (ordering == 1);
                v.max_dec_pic_buffering_minus1_ = dec_values[i];
                UtestBitWriter b;
                utest_hevc_write_vps_rbsp(b, v);
                utest_parse_prefixes(b, utest_parse_hevc_vps, errors);
            }
        }
    }

    const char *names[] = {"vps requires 4", "sublayer flag requires 1", "max_dec_pic_buffering_minus1",
                           "max_num_reorder_pics", "max_latency_increase_plus1", "vps maxlayer requires",
                           "num_layer_sets_minus1"};
    utest_expect_errors(errors, names, sizeof(names) / sizeof(names[0]));
}

VOID TEST(KernelHevcVpsTest, NaluHeaderChecks)
{
    srs_error_t err;

    if (true) {
        // forbidden_zero_bit is 1.
        char data[] = {(char)0x80, 0x01, 0x0c, 0x01};
        SrsBuffer stream(data, sizeof(data));
        SrsFormat format;
        HELPER_EXPECT_SUCCESS(format.initialize());
        err = format.hevc_demux_vps(&stream);
        EXPECT_NE(std::string::npos, srs_error_desc(err).find("forbidden_zero_bit"));
        srs_freep(err);
    }

    if (true) {
        // nal_unit_type 33 is an SPS.
        char data[] = {0x42, 0x01, 0x0c, 0x01};
        SrsBuffer stream(data, sizeof(data));
        SrsFormat format;
        HELPER_EXPECT_SUCCESS(format.initialize());
        err = format.hevc_demux_vps(&stream);
        EXPECT_NE(std::string::npos, srs_error_desc(err).find("hevc vps nal_unit_type=33"));
        srs_freep(err);
    }
}

VOID TEST(KernelHevcSpsTest, ConformanceWindowCropsTo1080p)
{
    srs_error_t err;

    // 4:2:0 crops two luma rows per conf_win_bottom_offset, so 1088 - 2 * 4 is 1080.
    UtestHevcSps s;
    s.sps_id_ = 3;
    s.bit_depth_luma_minus8_ = 2;
    UtestBitWriter b;
    utest_hevc_write_sps_rbsp(b, s);
    std::vector<char> data = b.bytes();

    SrsFormat format;
    HELPER_EXPECT_SUCCESS(format.initialize());
    HELPER_EXPECT_SUCCESS(format.hevc_demux_sps_rbsp(&data[0], (int)data.size()));

    EXPECT_EQ(1920, format.vcodec_->width_);
    EXPECT_EQ(1080, format.vcodec_->height_);
    EXPECT_EQ(SrsHevcProfileMain, format.vcodec_->hevc_profile_);
    EXPECT_EQ(93, (int)format.vcodec_->hevc_level_);

    SrsHevcDecoderConfigurationRecord *dec = &format.vcodec_->hevc_dec_conf_record_;
    SrsHevcRbspSps *sps = &dec->sps_table_[3];
    EXPECT_EQ(3u, sps->sps_seq_parameter_set_id_);
    EXPECT_EQ(1, sps->sps_temporal_id_nesting_flag_);
    EXPECT_EQ(1u, sps->chroma_format_idc_);
    EXPECT_EQ(1920u, sps->pic_width_in_luma_samples_);
    EXPECT_EQ(1088u, sps->pic_height_in_luma_samples_);
    EXPECT_EQ(1, sps->conformance_window_flag_);
    EXPECT_EQ(4u, sps->conf_win_bottom_offset_);
    EXPECT_EQ(2u, sps->bit_depth_luma_minus8_);
    EXPECT_EQ(4u, sps->log2_max_pic_order_cnt_lsb_minus4_);
    EXPECT_EQ(10, dec->bit_depth_luma_minus8_);
    EXPECT_EQ(8, dec->bit_depth_chroma_minus8_);
}

VOID TEST(KernelHevcSpsTest, SeparateColourPlanesCropOneSamplePerOffset)
{
    srs_error_t err;

    // 4:4:4 with separate colour planes has SubWidthC and SubHeightC of 1.
    UtestHevcSps s;
    s.chroma_format_idc_ = 3;
    s.separate_colour_plane_flag_ = 1;
    s.width_ = 1280;
    s.height_ = 720;
    s.conf_left_ = 2;
    s.conf_right_ = 6;
    s.conf_top_ = 1;
    s.conf_bottom_ = 7;
    UtestBitWriter b;
    utest_hevc_write_sps_rbsp(b, s);
    std::vector<char> data = b.bytes();

    SrsFormat format;
    HELPER_EXPECT_SUCCESS(format.initialize());
    HELPER_EXPECT_SUCCESS(format.hevc_demux_sps_rbsp(&data[0], (int)data.size()));

    EXPECT_EQ(1272, format.vcodec_->width_);
    EXPECT_EQ(712, format.vcodec_->height_);
    SrsHevcRbspSps *sps = &format.vcodec_->hevc_dec_conf_record_.sps_table_[0];
    EXPECT_EQ(3u, sps->chroma_format_idc_);
    EXPECT_EQ(1, sps->separate_colour_plane_flag_);
}

VOID TEST(KernelHevcSpsTest, InvalidSpsFails)
{
    srs_error_t err;

    if (true) {
        // sps_seq_parameter_set_id must be less than 16.
        UtestHevcSps s;
        s.sps_id_ = 16;
        UtestBitWriter b;
        utest_hevc_write_sps_rbsp(b, s);
        std::vector<char> data = b.bytes();

        SrsFormat format;
        HELPER_EXPECT_SUCCESS(format.initialize());
        err = format.hevc_demux_sps_rbsp(&data[0], (int)data.size());
        EXPECT_NE(std::string::npos, srs_error_desc(err).find("sps id out of range: 16"));
        srs_freep(err);
    }

    if (true) {
        // The parser is off, so a broken SPS is ignored.
        char data[] = {0x01};
        SrsFormat format;
        HELPER_EXPECT_SUCCESS(format.initialize());
        format.avc_parse_sps_ = false;
        HELPER_EXPECT_SUCCESS(format.hevc_demux_sps_rbsp(data, sizeof(data)));
        EXPECT_EQ(0, format.vcodec_->width_);
    }
}

VOID TEST(KernelHevcSpsTest, TruncatedFieldsFail)
{
    std::string errors;
    uint32_t ids[] = {0, 1, 3, 7};
    for (int i = 0; i < 4; i++) {
        for (int chroma = 1; chroma <= 3; chroma += 2) {
            UtestHevcSps s;
            s.sps_id_ = ids[i];
            s.chroma_format_idc_ = chroma;
            s.conf_left_ = 1;
            s.conf_right_ = 2;
            s.conf_top_ = 3;
            s.conf_bottom_ = 4;
            UtestBitWriter b;
            utest_hevc_write_sps_rbsp(b, s);
            utest_parse_prefixes(b, utest_parse_hevc_sps, errors);
        }
    }

    const char *names[] = {"sps requires 2", "sps rbsp ptl", "sps_seq_parameter_set_id", "chroma_format_idc",
                           "separate_colour_plane_flag requires 1", "pic_width_in_luma_samples",
                           "pic_height_in_luma_samples", "conformance_window_flag requires 1", "conf_win_left_offset",
                           "conf_win_right_offset", "conf_win_top_offset", "conf_win_bottom_offset",
                           "bit_depth_luma_minus8", "bit_depth_chroma_minus8", "log2_max_pic_order_cnt_lsb_minus4"};
    utest_expect_errors(errors, names, sizeof(names) / sizeof(names[0]));
}

VOID TEST(KernelHevcSpsTest, NaluHeaderChecks)
{
    srs_error_t err;

    if (true) {
        char data[1] = {0};
        SrsBuffer stream(data, 0);
        SrsFormat format;
        HELPER_EXPECT_SUCCESS(format.initialize());
        err = format.hevc_demux_sps(&stream);
        EXPECT_NE(std::string::npos, srs_error_desc(err).find("decode hevc sps requires 1"));
        srs_freep(err);
    }

    if (true) {
        char data[] = {(char)0x80, 0x01, 0x01, 0x01};
        SrsBuffer stream(data, sizeof(data));
        SrsFormat format;
        HELPER_EXPECT_SUCCESS(format.initialize());
        err = format.hevc_demux_sps(&stream);
        EXPECT_NE(std::string::npos, srs_error_desc(err).find("forbidden_zero_bit"));
        srs_freep(err);
    }

    if (true) {
        // nal_unit_type 32 is a VPS.
        char data[] = {0x40, 0x01, 0x01, 0x01};
        SrsBuffer stream(data, sizeof(data));
        SrsFormat format;
        HELPER_EXPECT_SUCCESS(format.initialize());
        err = format.hevc_demux_sps(&stream);
        EXPECT_NE(std::string::npos, srs_error_desc(err).find("hevc sps nal_unit_type=32"));
        srs_freep(err);
    }
}

VOID TEST(KernelHevcPpsTest, ParsesTilesDeblockingAndExtensions)
{
    srs_error_t err;

    UtestHevcPps p;
    UtestBitWriter b;
    utest_hevc_write_pps_rbsp(b, p);
    std::vector<char> data = b.bytes();

    SrsFormat format;
    HELPER_EXPECT_SUCCESS(format.initialize());
    HELPER_EXPECT_SUCCESS(format.hevc_demux_pps_rbsp(&data[0], (int)data.size()));

    SrsHevcRbspPps *pps = &format.vcodec_->hevc_dec_conf_record_.pps_table_[5];
    EXPECT_EQ(5u, pps->pps_pic_parameter_set_id_);
    EXPECT_EQ(2u, pps->pps_seq_parameter_set_id_);
    EXPECT_EQ(1, pps->dependent_slice_segments_enabled_flag_);
    EXPECT_EQ(0, pps->output_flag_present_flag_);
    EXPECT_EQ(5, pps->num_extra_slice_header_bits_);
    EXPECT_EQ(1, pps->sign_data_hiding_enabled_flag_);
    EXPECT_EQ(3u, pps->num_ref_idx_l0_default_active_minus1_);
    EXPECT_EQ(1u, pps->num_ref_idx_l1_default_active_minus1_);
    EXPECT_EQ(-4, pps->init_qp_minus26_);
    EXPECT_EQ(1, pps->constrained_intra_pred_flag_);
    EXPECT_EQ(1, pps->cu_qp_delta_enabled_flag_);
    EXPECT_EQ(2u, pps->diff_cu_qp_delta_depth_);
    EXPECT_EQ(3, pps->pps_cb_qp_offset_);
    EXPECT_EQ(-2, pps->pps_cr_qp_offset_);
    EXPECT_EQ(1, pps->pps_slice_chroma_qp_offsets_present_flag_);
    EXPECT_EQ(1, pps->weighted_bipred_flag_);
    EXPECT_EQ(1, pps->tiles_enabled_flag_);
    EXPECT_EQ(1, pps->entropy_coding_sync_enabled_flag_);
    EXPECT_EQ(2u, pps->num_tile_columns_minus1_);
    EXPECT_EQ(1u, pps->num_tile_rows_minus1_);
    EXPECT_EQ(0, pps->uniform_spacing_flag_);
    ASSERT_EQ(2u, pps->column_width_minus1_.size());
    EXPECT_EQ(10u, pps->column_width_minus1_[0]);
    EXPECT_EQ(20u, pps->column_width_minus1_[1]);
    ASSERT_EQ(1u, pps->row_height_minus1_.size());
    EXPECT_EQ(30u, pps->row_height_minus1_[0]);
    EXPECT_EQ(1, pps->loop_filter_across_tiles_enabled_flag_);
    EXPECT_EQ(1, pps->pps_loop_filter_across_slices_enabled_flag_);
    EXPECT_EQ(1, pps->deblocking_filter_control_present_flag_);
    EXPECT_EQ(1, pps->deblocking_filter_override_enabled_flag_);
    EXPECT_EQ(0, pps->pps_deblocking_filter_disabled_flag_);
    EXPECT_EQ(-3, pps->pps_beta_offset_div2_);
    EXPECT_EQ(2, pps->pps_tc_offset_div2_);
    EXPECT_EQ(0, pps->pps_scaling_list_data_present_flag_);
    EXPECT_EQ(1, pps->lists_modification_present_flag_);
    EXPECT_EQ(3u, pps->log2_parallel_merge_level_minus2_);
    EXPECT_EQ(1, pps->pps_extension_present_flag_);
    EXPECT_EQ(1, pps->pps_range_extension_flag_);
    EXPECT_EQ(0, pps->pps_multilayer_extension_flag_);
    EXPECT_EQ(1, pps->pps_3d_extension_flag_);
    EXPECT_EQ(0, pps->pps_scc_extension_flag_);
    EXPECT_EQ(0xa, pps->pps_extension_4bits_);
}

VOID TEST(KernelHevcPpsTest, ParsesScalingListData)
{
    srs_error_t err;

    UtestHevcPps p;
    p.tiles_ = false;
    p.deblocking_disabled_ = true;
    p.scaling_list_ = true;
    UtestBitWriter b;
    utest_hevc_write_pps_rbsp(b, p);
    std::vector<char> data = b.bytes();

    SrsFormat format;
    HELPER_EXPECT_SUCCESS(format.initialize());
    HELPER_EXPECT_SUCCESS(format.hevc_demux_pps_rbsp(&data[0], (int)data.size()));

    SrsHevcRbspPps *pps = &format.vcodec_->hevc_dec_conf_record_.pps_table_[5];
    EXPECT_EQ(1, pps->pps_deblocking_filter_disabled_flag_);
    EXPECT_EQ(1, pps->pps_scaling_list_data_present_flag_);

    // The 4x4 list starts from nextCoef 8 and adds 1 per coefficient.
    SrsHevcScalingListData *sld = &pps->scaling_list_data_;
    EXPECT_EQ(1, sld->scaling_list_pred_mode_flag_[0][0]);
    EXPECT_EQ(9u, sld->ScalingList_[0][0][0]);
    EXPECT_EQ(24u, sld->ScalingList_[0][0][15]);

    // The 16x16 list starts from scaling_list_dc_coef_minus8 + 8 and every delta is 0.
    EXPECT_EQ(1, sld->scaling_list_pred_mode_flag_[2][1]);
    EXPECT_EQ(5, sld->scaling_list_dc_coef_minus8_[0][1]);
    EXPECT_EQ(13u, sld->ScalingList_[2][1][0]);
    EXPECT_EQ(13u, sld->ScalingList_[2][1][63]);

    EXPECT_EQ(0, sld->scaling_list_pred_mode_flag_[1][2]);
    EXPECT_EQ(2u, sld->scaling_list_pred_matrix_id_delta_[1][2]);
    EXPECT_EQ(0, sld->scaling_list_pred_mode_flag_[3][3]);

    // The fields after the lists are read from the right position.
    EXPECT_EQ(1, pps->lists_modification_present_flag_);
    EXPECT_EQ(3u, pps->log2_parallel_merge_level_minus2_);
    EXPECT_EQ(0xa, pps->pps_extension_4bits_);
}

VOID TEST(KernelHevcPpsTest, InvalidPpsIdFails)
{
    srs_error_t err;

    // pps_pic_parameter_set_id must be less than 64.
    UtestHevcPps p;
    p.pps_id_ = 64;
    UtestBitWriter b;
    utest_hevc_write_pps_rbsp(b, p);
    std::vector<char> data = b.bytes();

    SrsFormat format;
    HELPER_EXPECT_SUCCESS(format.initialize());
    err = format.hevc_demux_pps_rbsp(&data[0], (int)data.size());
    EXPECT_NE(std::string::npos, srs_error_desc(err).find("pps id out of range: 64"));
    srs_freep(err);
}

VOID TEST(KernelHevcPpsTest, TruncatedFieldsFail)
{
    std::string errors;
    uint32_t ids[] = {0, 1, 3, 7, 15, 31};
    for (int i = 0; i < 6; i++) {
        for (int cu_qp = 0; cu_qp < 2; cu_qp++) {
            UtestHevcPps p;
            p.pps_id_ = ids[i];
            p.cu_qp_delta_enabled_ = (cu_qp == 1);
            UtestBitWriter b;
            utest_hevc_write_pps_rbsp(b, p);
            utest_parse_prefixes(b, utest_parse_hevc_pps, errors);
        }
    }

    const char *names[] = {"pps_pic_parameter_set_id", "pps_seq_parameter_set_id", "pps slice requires 7",
                           "num_ref_idx_l0_default_active_minus1", "num_ref_idx_l1_default_active_minus1",
                           "init_qp_minus26", "pps requires 3", "diff_cu_qp_delta_depth", "pps_cb_qp_offset",
                           "pps_cr_qp_offset", "pps slice_chroma_qp requires 6", "num_tile_columns_minus1",
                           "num_tile_rows_minus1", "uniform_spacing_flag requires 1", "column_width_minus1",
                           "row_height_minus1", "loop_filter_across_tiles_enabled_flag requires 1",
                           "pps loop deblocking filter requires 2", "pps loop deblocking filter flag requires 2",
                           "pps_beta_offset_div2", "pps_tc_offset_div2", "pps scaling_list_data requires 1",
                           "lists_modification_present_flag requires 1", "log2_parallel_merge_level_minus2",
                           "extension_present_flag requires 2", "pps_range_extension_flag requires 8"};
    utest_expect_errors(errors, names, sizeof(names) / sizeof(names[0]));
}

// scaling_list_data() reads scaling_list_pred_mode_flag without checking for bits, so these cases cut the
// data only where the next field is an Exp-Golomb code, which does check. A cut that leaves exactly one
// zero bit would decode that code as 0 and then read the flag from an empty buffer, so the PPS id is picked
// to avoid it.
static void utest_expect_scaling_list_cut_fails(int nb_coded_matrices, bool coded_mode, const char *name)
{
    srs_error_t err;

    uint32_t ids[] = {0, 1, 3};
    bool tested = false;
    for (int i = 0; i < 3 && !tested; i++) {
        UtestHevcPps p;
        p.pps_id_ = ids[i];
        p.tiles_ = false;
        p.deblocking_disabled_ = true;
        UtestBitWriter b;
        utest_hevc_write_pps_head(b, p);
        b.u(1, 1); // pps_scaling_list_data_present_flag

        // Matrices that predict from matrix 0, then the mode flag of the matrix that is cut.
        for (int m = 0; m < nb_coded_matrices; m++) {
            b.u(0, 1);
            b.ue(0);
        }
        b.u(coded_mode ? 1 : 0, 1);
        if (b.size() % 8 == 1) {
            continue;
        }

        std::vector<char> data = b.bytes();
        SrsFormat format;
        HELPER_EXPECT_SUCCESS(format.initialize());
        err = format.hevc_demux_pps_rbsp(&data[0], (int)data.size());
        EXPECT_NE(std::string::npos, srs_error_desc(err).find(name)) << srs_error_desc(err);
        srs_freep(err);
        tested = true;
    }
    EXPECT_TRUE(tested);
}

VOID TEST(KernelHevcPpsTest, TruncatedScalingListFails)
{
    // The first 4x4 matrix predicts, so scaling_list_pred_matrix_id_delta is next.
    utest_expect_scaling_list_cut_fails(0, false, "scaling_list_pred_matrix_id_delta");
    // The first 4x4 matrix is coded, so the first scaling_list_delta_coef is next.
    utest_expect_scaling_list_cut_fails(0, true, "scaling_list_delta_coef");
    // The six 4x4 and six 8x8 matrices predict, and the first 16x16 matrix is coded, so its
    // scaling_list_dc_coef_minus8 is next.
    utest_expect_scaling_list_cut_fails(12, true, "scaling_list_dc_coef_minus8");
}

VOID TEST(KernelHevcPpsTest, NaluHeaderChecks)
{
    srs_error_t err;

    if (true) {
        char data[1] = {0};
        SrsBuffer stream(data, 0);
        SrsFormat format;
        HELPER_EXPECT_SUCCESS(format.initialize());
        err = format.hevc_demux_pps(&stream);
        EXPECT_NE(std::string::npos, srs_error_desc(err).find("decode hevc pps requires 1"));
        srs_freep(err);
    }

    if (true) {
        char data[] = {(char)0x80, 0x01, (char)0xc1};
        SrsBuffer stream(data, sizeof(data));
        SrsFormat format;
        HELPER_EXPECT_SUCCESS(format.initialize());
        err = format.hevc_demux_pps(&stream);
        EXPECT_NE(std::string::npos, srs_error_desc(err).find("forbidden_zero_bit"));
        srs_freep(err);
    }

    if (true) {
        // nal_unit_type 33 is an SPS.
        char data[] = {0x42, 0x01, (char)0xc1};
        SrsBuffer stream(data, sizeof(data));
        SrsFormat format;
        HELPER_EXPECT_SUCCESS(format.initialize());
        err = format.hevc_demux_pps(&stream);
        EXPECT_NE(std::string::npos, srs_error_desc(err).find("hevc pps nal_unit_type=33"));
        srs_freep(err);
    }
}

// The 23 bytes of HEVCDecoderConfigurationRecord before the arrays, with lengthSizeMinusOne 3.
static std::vector<char> utest_hevc_hvcc_header(uint8_t version, uint8_t nb_arrays)
{
    const uint8_t header[] = {version, 0x01, 0x60, 0x00, 0x00, 0x00, 0x90, 0x00, 0x00, 0x00, 0x00, 0x00,
                              0x5d, 0xf0, 0x00, 0xfc, 0xfd, 0xf8, 0xf8, 0x00, 0x00, 0x0f, nb_arrays};
    return std::vector<char>((const char *)header, (const char *)header + sizeof(header));
}

VOID TEST(KernelHevcHvccTest, InvalidRecordsFail)
{
    srs_error_t err;

    if (true) {
        std::vector<char> data = utest_hevc_hvcc_header(2, 0);
        SrsBuffer stream(&data[0], (int)data.size());
        SrsFormat format;
        HELPER_EXPECT_SUCCESS(format.initialize());
        err = format.hevc_demux_hvcc(&stream);
        EXPECT_NE(std::string::npos, srs_error_desc(err).find("invalid version=2"));
        srs_freep(err);
    }

    if (true) {
        // One array, but no array header.
        std::vector<char> data = utest_hevc_hvcc_header(1, 1);
        SrsBuffer stream(&data[0], (int)data.size());
        SrsFormat format;
        HELPER_EXPECT_SUCCESS(format.initialize());
        err = format.hevc_demux_hvcc(&stream);
        EXPECT_NE(std::string::npos, srs_error_desc(err).find("requires 3 only 0 bytes"));
        srs_freep(err);
    }

    if (true) {
        // A VPS array of one NALU, but no nalUnitLength.
        std::vector<char> data = utest_hevc_hvcc_header(1, 1);
        const char array[] = {0x20, 0x00, 0x01};
        data.insert(data.end(), array, array + sizeof(array));
        SrsBuffer stream(&data[0], (int)data.size());
        SrsFormat format;
        HELPER_EXPECT_SUCCESS(format.initialize());
        err = format.hevc_demux_hvcc(&stream);
        EXPECT_NE(std::string::npos, srs_error_desc(err).find("num_nalus requires 2"));
        srs_freep(err);
    }

    if (true) {
        // nalUnitLength is 10, but only 2 bytes follow.
        std::vector<char> data = utest_hevc_hvcc_header(1, 1);
        const char array[] = {0x20, 0x00, 0x01, 0x00, 0x0a, 0x40, 0x01};
        data.insert(data.end(), array, array + sizeof(array));
        SrsBuffer stream(&data[0], (int)data.size());
        SrsFormat format;
        HELPER_EXPECT_SUCCESS(format.initialize());
        err = format.hevc_demux_hvcc(&stream);
        EXPECT_NE(std::string::npos, srs_error_desc(err).find("requires 10 only 2 bytes"));
        srs_freep(err);
    }

    if (true) {
        // The VPS NALU has forbidden_zero_bit set.
        std::vector<char> data = utest_hevc_hvcc_header(1, 1);
        const char array[] = {0x20, 0x00, 0x01, 0x00, 0x02, (char)0x80, 0x01};
        data.insert(data.end(), array, array + sizeof(array));
        SrsBuffer stream(&data[0], (int)data.size());
        SrsFormat format;
        HELPER_EXPECT_SUCCESS(format.initialize());
        err = format.hevc_demux_hvcc(&stream);
        EXPECT_NE(std::string::npos, srs_error_desc(err).find("hevc demux vps/sps/pps failed"));
        srs_freep(err);
    }
}

VOID TEST(KernelHevcHvccTest, OtherNaluTypesAreKeptButNotParsed)
{
    srs_error_t err;

    // A prefix SEI array (nal_unit_type 39) is stored, and the parser skips it.
    std::vector<char> data = utest_hevc_hvcc_header(1, 1);
    const char array[] = {(char)0xa7, 0x00, 0x01, 0x00, 0x03, 0x4e, 0x01, 0x05};
    data.insert(data.end(), array, array + sizeof(array));
    SrsBuffer stream(&data[0], (int)data.size());
    SrsFormat format;
    HELPER_EXPECT_SUCCESS(format.initialize());
    HELPER_EXPECT_SUCCESS(format.hevc_demux_hvcc(&stream));

    SrsHevcDecoderConfigurationRecord *dec = &format.vcodec_->hevc_dec_conf_record_;
    ASSERT_EQ(1u, dec->nalu_vec_.size());
    EXPECT_EQ(1, dec->nalu_vec_[0].array_completeness_);
    EXPECT_EQ(39, dec->nalu_vec_[0].nal_unit_type_);
    ASSERT_EQ(1u, dec->nalu_vec_[0].nal_data_vec_.size());
    EXPECT_EQ(3, dec->nalu_vec_[0].nal_data_vec_[0].nal_unit_length_);
    EXPECT_EQ(0, format.vcodec_->width_);
}

VOID TEST(KernelAvcSpsTest, InterlacedSpsWithPocCycleIs720p)
{
    srs_error_t err;

    // 80x23 macroblock pairs of an interlaced picture make 1280x736, cropped by 2 * 8 rows to 720.
    UtestAvcSpsFields s;
    UtestBitWriter b;
    utest_avc_write_sps_rbsp(b, s);
    std::vector<char> data = b.bytes();

    SrsFormat format;
    HELPER_EXPECT_SUCCESS(format.initialize());
    HELPER_EXPECT_SUCCESS(format.avc_demux_sps_rbsp(&data[0], (int)data.size()));
    EXPECT_EQ(1280, format.vcodec_->width_);
    EXPECT_EQ(720, format.vcodec_->height_);
}

VOID TEST(KernelAvcSpsTest, TruncatedFieldsFail)
{
    std::string errors;
    uint32_t ids[] = {0, 1, 3, 7};
    for (int i = 0; i < 4; i++) {
        for (int chroma = 1; chroma <= 3; chroma += 2) {
            for (uint32_t poc = 0; poc < 2; poc++) {
                UtestAvcSpsFields s;
                s.sps_id_ = ids[i];
                s.chroma_format_idc_ = chroma;
                s.poc_type_ = poc;
                s.crop_left_ = 1;
                s.crop_right_ = 2;
                s.crop_top_ = 3;
                s.crop_bottom_ = 4;
                UtestBitWriter b;
                utest_avc_write_sps_rbsp(b, s);
                utest_parse_prefixes(b, utest_parse_avc_sps, errors);
            }
        }
    }

    const char *names[] = {"sps shall atleast 3bytes", "read seq_parameter_set_id", "read chroma_format_idc",
                           "read separate_colour_plane_flag", "read bit_depth_luma_minus8",
                           "read bit_depth_chroma_minus8", "read qpprime_y_zero_transform_bypass_flag",
                           "read seq_scaling_matrix_present_flag", "read seq_scaling_list_present_flag_i",
                           "read log2_max_frame_num_minus4", "read pic_order_cnt_type",
                           "read log2_max_pic_order_cnt_lsb_minus4", "read delta_pic_order_always_zero_flag",
                           "read offset_for_non_ref_pic", "read offset_for_top_to_bottom_field",
                           "read num_ref_frames_in_pic_order_cnt_cycle", "read offset_for_ref_frame_i",
                           "read max_num_ref_frames", "read gaps_in_frame_num_value_allowed_flag",
                           "read pic_width_in_mbs_minus1", "read pic_height_in_map_units_minus1",
                           "read frame_mbs_only_flag", "read mb_adaptive_frame_field_flag",
                           "read direct_8x8_inference_flag", "read frame_cropping_flag", "read frame_crop_left_offset",
                           "read frame_crop_right_offset", "read frame_crop_top_offset",
                           "read frame_crop_bottom_offset", "read vui_parameters_present_flag"};
    utest_expect_errors(errors, names, sizeof(names) / sizeof(names[0]));
}

VOID TEST(KernelAvcSpsTest, InvalidHeaderFieldsFail)
{
    srs_error_t err;

    char no_profile[] = {0x00, 0x00, 0x28, (char)0x80};
    char bad_flags[] = {0x64, 0x01, 0x28, (char)0x80};
    char no_level[] = {0x64, 0x00, 0x00, (char)0x80};
    char *cases[] = {no_profile, bad_flags, no_level};
    const char *names[] = {"profile_idc invalid", "flags invalid", "level_idc invalid"};
    for (int i = 0; i < 3; i++) {
        SrsFormat format;
        HELPER_EXPECT_SUCCESS(format.initialize());
        err = format.avc_demux_sps_rbsp(cases[i], 4);
        EXPECT_NE(std::string::npos, srs_error_desc(err).find(names[i]));
        srs_freep(err);
    }
}

VOID TEST(KernelAvcSpsTest, NaluHeaderChecks)
{
    srs_error_t err;

    // forbidden_zero_bit set, nal_ref_idc 0, nal_unit_type 8 (PPS).
    char forbidden[] = {(char)0xe7, 0x64, 0x00, 0x28};
    char no_ref[] = {0x07, 0x64, 0x00, 0x28};
    char pps[] = {0x68, 0x64, 0x00, 0x28};
    char *cases[] = {forbidden, no_ref, pps};
    const char *names[] = {"forbidden_zero_bit", "nal_ref_idc", "nal_unit_type shall be equal to 7"};
    for (int i = 0; i < 3; i++) {
        SrsFormat format;
        HELPER_EXPECT_SUCCESS(format.initialize());
        format.vcodec_->sequenceParameterSetNALUnit_.assign(cases[i], cases[i] + 4);
        err = format.avc_demux_sps();
        EXPECT_NE(std::string::npos, srs_error_desc(err).find(names[i]));
        srs_freep(err);
    }

    if (true) {
        // The parser is off, so only the NALU header is checked.
        char sps[] = {0x67, 0x00, 0x00, 0x00};
        SrsFormat format;
        HELPER_EXPECT_SUCCESS(format.initialize());
        format.avc_parse_sps_ = false;
        format.vcodec_->sequenceParameterSetNALUnit_.assign(sps, sps + 4);
        HELPER_EXPECT_SUCCESS(format.avc_demux_sps());
        EXPECT_EQ(0, format.vcodec_->width_);
    }
}

VOID TEST(KernelAvcSpsTest, TruncatedDecoderConfigurationFails)
{
    srs_error_t err;

    if (true) {
        // One SPS of 10 bytes, but only 2 follow.
        char data[] = {0x01, 0x64, 0x00, 0x28, (char)0xff, (char)0xe1, 0x00, 0x0a, 0x67, 0x64};
        SrsBuffer stream(data, sizeof(data));
        SrsFormat format;
        HELPER_EXPECT_SUCCESS(format.initialize());
        err = format.avc_demux_sps_pps(&stream);
        EXPECT_NE(std::string::npos, srs_error_desc(err).find("decode SPS data"));
        srs_freep(err);
    }

    if (true) {
        // One SPS of 2 bytes, then one PPS of 10 bytes, but only 2 follow.
        char data[] = {0x01, 0x64, 0x00, 0x28, (char)0xff, (char)0xe1, 0x00, 0x02, 0x67, 0x64,
                       0x01, 0x00, 0x0a, 0x68, (char)0xee};
        SrsBuffer stream(data, sizeof(data));
        SrsFormat format;
        HELPER_EXPECT_SUCCESS(format.initialize());
        err = format.avc_demux_sps_pps(&stream);
        EXPECT_NE(std::string::npos, srs_error_desc(err).find("decode PPS data"));
        srs_freep(err);
    }
}

static void utest_append_48bits(std::vector<char> &data, uint64_t v)
{
    for (int i = 5; i >= 0; i--) {
        data.push_back((char)((v >> (8 * i)) & 0xff));
    }
}

// PTS or DTS in the 33 bits layout of the PES header: 4 bits prefix, then bits 32..30, 29..15 and 14..0,
// each followed by a marker bit.
static void utest_append_pts(std::vector<char> &data, uint8_t prefix, int64_t v)
{
    data.push_back((char)((prefix << 4) | (((v >> 30) & 0x07) << 1) | 1));
    int32_t mid = (int32_t)((((v >> 15) & 0x7fff) << 1) | 1);
    data.push_back((char)(mid >> 8));
    data.push_back((char)mid);
    int32_t low = (int32_t)(((v & 0x7fff) << 1) | 1);
    data.push_back((char)(low >> 8));
    data.push_back((char)low);
}

VOID TEST(KernelTsAdaptationFieldTest, DecodeAllOptionalFields)
{
    srs_error_t err;

    std::vector<char> data;
    data.push_back(32);         // adaptation_field_length
    data.push_back((char)0xdf); // discontinuity, random_access, PCR, OPCR, splicing_point, private_data, extension
    // PCR: base 33 bits, 6 reserved bits, extension 9 bits.
    utest_append_48bits(data, ((uint64_t)0x123456789LL << 15) | (0x3f << 9) | 0x0ab);
    utest_append_48bits(data, ((uint64_t)0x0abcdef01LL << 15) | (0x3f << 9) | 0x155);
    data.push_back((char)0xf6); // splice_countdown -10
    data.push_back(3);          // transport_private_data_length
    data.push_back('a');
    data.push_back('b');
    data.push_back('c');
    // adaptation_field_extension_length counts the bytes after it: the flags, ltw, piecewise_rate and the
    // seamless_splice fields.
    data.push_back(11);
    data.push_back((char)0xff); // ltw, piecewise_rate, seamless_splice and 5 reserved bits
    data.push_back((char)0x92); // ltw_valid_flag 1, ltw_offset 0x1234
    data.push_back(0x34);
    data.push_back((char)0xea); // 2 reserved bits, piecewise_rate 0x2abcde
    data.push_back((char)0xbc);
    data.push_back((char)0xde);
    data.push_back(0x3b);       // splice_type 3, DTS_next_AU[32..30] 5, marker
    data.push_back(0x24);       // DTS_next_AU[29..15] 0x1234, marker
    data.push_back(0x69);
    data.push_back((char)0xff); // DTS_next_AU[14..0] 0x7fff, marker
    data.push_back((char)0xff);
    data.push_back((char)0xff); // two stuffing bytes
    data.push_back((char)0xff);
    data.push_back(0x47); // the next byte after the adaptation field

    SrsTsPacket pkt(NULL);
    pkt.adaption_field_control_ = SrsTsAdaptationFieldTypeBoth;
    SrsTsAdaptationField af(&pkt);
    SrsBuffer stream(&data[0], (int)data.size());
    HELPER_EXPECT_SUCCESS(af.decode(&stream));

    EXPECT_EQ(33, stream.pos());
    EXPECT_EQ(1, af.discontinuity_indicator_);
    EXPECT_EQ(1, af.random_access_indicator_);
    EXPECT_EQ(0, af.elementary_stream_priority_indicator_);
    EXPECT_EQ(0x123456789LL, af.program_clock_reference_base_);
    EXPECT_EQ(0x0ab, af.program_clock_reference_extension_);
    EXPECT_EQ(1, af.OPCR_flag_);
    EXPECT_EQ(0x0abcdef01LL, af.original_program_clock_reference_base_);
    EXPECT_EQ(0x155, af.original_program_clock_reference_extension_);
    EXPECT_EQ(0x3f, af.const1_value2_);
    EXPECT_EQ(-10, af.splice_countdown_);
    ASSERT_EQ(3u, af.transport_private_data_.size());
    EXPECT_EQ("abc", std::string(&af.transport_private_data_[0], 3));
    EXPECT_EQ(11, af.adaptation_field_extension_length_);
    EXPECT_EQ(1, af.ltw_flag_);
    EXPECT_EQ(1, af.piecewise_rate_flag_);
    EXPECT_EQ(1, af.seamless_splice_flag_);
    EXPECT_EQ(0x1f, af.const1_value1_);
    EXPECT_EQ(1, af.ltw_valid_flag_);
    EXPECT_EQ(0x1234, af.ltw_offset_);
    EXPECT_EQ(0x2abcde, af.piecewise_rate_);
    EXPECT_EQ(3, af.splice_type_);
    EXPECT_EQ(5, af.DTS_next_AU0_);
    EXPECT_EQ(1, af.marker_bit0_);
    EXPECT_EQ(0x1234, af.DTS_next_AU1_);
    EXPECT_EQ(1, af.marker_bit1_);
    EXPECT_EQ(0x7fff, af.DTS_next_AU2_);
    EXPECT_EQ(1, af.marker_bit2_);
}

VOID TEST(KernelTsAdaptationFieldTest, EncodeThenDecodePcrSpliceAndPrivateData)
{
    srs_error_t err;

    SrsTsPacket pkt(NULL);
    pkt.adaption_field_control_ = SrsTsAdaptationFieldTypeBoth;
    SrsTsAdaptationField af(&pkt);
    af.discontinuity_indicator_ = 1;
    af.random_access_indicator_ = 1;
    af.PCR_flag_ = 1;
    af.program_clock_reference_base_ = 0x1fedcba98LL;
    af.program_clock_reference_extension_ = 0x101;
    af.splicing_point_flag_ = 1;
    af.splice_countdown_ = 5;
    af.transport_private_data_flag_ = 1;
    af.transport_private_data_.push_back('x');
    af.transport_private_data_.push_back('y');

    int size = af.size();
    EXPECT_EQ(12, size);
    EXPECT_EQ(11, af.adaption_field_length_);

    std::vector<char> data(size, 0);
    SrsBuffer stream(&data[0], size);
    HELPER_EXPECT_SUCCESS(af.encode(&stream));
    EXPECT_EQ(size, stream.pos());
    EXPECT_EQ(11, data[0]);
    EXPECT_EQ((char)0xd6, data[1]);

    SrsTsAdaptationField decoded(&pkt);
    SrsBuffer input(&data[0], size);
    HELPER_EXPECT_SUCCESS(decoded.decode(&input));
    EXPECT_EQ(size, input.pos());
    EXPECT_EQ(1, decoded.discontinuity_indicator_);
    EXPECT_EQ(1, decoded.random_access_indicator_);
    EXPECT_EQ(1, decoded.PCR_flag_);
    EXPECT_EQ(0x1fedcba98LL, decoded.program_clock_reference_base_);
    EXPECT_EQ(0x101, decoded.program_clock_reference_extension_);
    EXPECT_EQ(5, decoded.splice_countdown_);
    ASSERT_EQ(2u, decoded.transport_private_data_.size());
    EXPECT_EQ('x', decoded.transport_private_data_[0]);
    EXPECT_EQ('y', decoded.transport_private_data_[1]);
}

VOID TEST(KernelTsAdaptationFieldTest, EncodeSkipsOpcrAndExtensionFields)
{
    srs_error_t err;

    // The muxer writes the extension flags, but leaves the bytes of OPCR, ltw, piecewise_rate,
    // seamless_splice and the reserved bytes as they are.
    SrsTsPacket pkt(NULL);
    pkt.adaption_field_control_ = SrsTsAdaptationFieldTypePayloadOnly;
    SrsTsAdaptationField af(&pkt);
    af.adaption_field_length_ = 22;
    af.OPCR_flag_ = 1;
    af.adaptation_field_extension_flag_ = 1;
    af.adaptation_field_extension_length_ = 11;
    af.ltw_flag_ = 1;
    af.piecewise_rate_flag_ = 1;
    af.seamless_splice_flag_ = 1;
    af.nb_af_ext_reserved_ = 1;
    af.nb_af_reserved_ = 1;

    std::vector<char> data(32, 0x55);
    SrsBuffer stream(&data[0], (int)data.size());
    HELPER_EXPECT_SUCCESS(af.encode(&stream));

    // 1B length, 1B flags, 6B OPCR, 1B extension length, 1B extension flags, 2B ltw, 3B piecewise_rate,
    // 5B seamless_splice, 1B extension reserved, 1B reserved.
    EXPECT_EQ(22, stream.pos());
    EXPECT_EQ(22, data[0]);
    EXPECT_EQ(0x09, data[1]);
    EXPECT_EQ(0x55, data[2]);
    EXPECT_EQ(11, data[8]);
    EXPECT_EQ((char)0xff, data[9]);
    EXPECT_EQ(0x55, data[10]);
}

static void utest_expect_af_decode_fails(const char *bytes, int size, const char *name)
{
    SrsTsPacket pkt(NULL);
    pkt.adaption_field_control_ = SrsTsAdaptationFieldTypePayloadOnly;
    SrsTsAdaptationField af(&pkt);
    std::vector<char> data(bytes, bytes + size);
    SrsBuffer stream(&data[0], size);
    srs_error_t err = af.decode(&stream);
    EXPECT_NE(std::string::npos, srs_error_desc(err).find(name)) << name;
    srs_freep(err);
}

// Each case ends right before the named field.
VOID TEST(KernelTsAdaptationFieldTest, DecodeTruncatedFieldsFail)
{
    const char opcr[] = {100, 0x08};
    utest_expect_af_decode_fails(opcr, sizeof(opcr), "demux af OPCR_flag");
    const char splicing[] = {100, 0x04};
    utest_expect_af_decode_fails(splicing, sizeof(splicing), "demux af splicing_point_flag");
    const char private_flag[] = {100, 0x02};
    utest_expect_af_decode_fails(private_flag, sizeof(private_flag), "demux af transport_private_data_flag");
    const char private_data[] = {100, 0x02, 5, 'a', 'b'};
    utest_expect_af_decode_fails(private_data, sizeof(private_data), "demux af transport_private_data");
    const char extension[] = {100, 0x01, 1};
    utest_expect_af_decode_fails(extension, sizeof(extension), "demux af adaptation_field_extension_flag");
    const char ltw[] = {100, 0x01, 3, (char)0x80, 0x01};
    utest_expect_af_decode_fails(ltw, sizeof(ltw), "demux af ltw_flag");
    const char piecewise[] = {100, 0x01, 4, 0x40, 0x01, 0x02};
    utest_expect_af_decode_fails(piecewise, sizeof(piecewise), "demux af piecewise_rate_flag");
    const char seamless[] = {100, 0x01, 6, 0x20, 0x01, 0x02, 0x03, 0x04};
    utest_expect_af_decode_fails(seamless, sizeof(seamless), "demux af seamless_splice_flag");
}

// Encode a field whose flags are set into a buffer of size bytes.
static void utest_expect_af_encode_fails(SrsTsAdaptationField *af, int size, const char *name)
{
    af->adaption_field_length_ = 100;
    std::vector<char> data(size + 1, 0);
    SrsBuffer stream(&data[0], size);
    srs_error_t err = af->encode(&stream);
    EXPECT_NE(std::string::npos, srs_error_desc(err).find(name)) << name;
    srs_freep(err);
}

VOID TEST(KernelTsAdaptationFieldTest, EncodeTruncatedFieldsFail)
{
    SrsTsPacket pkt(NULL);
    pkt.adaption_field_control_ = SrsTsAdaptationFieldTypePayloadOnly;

    if (true) {
        SrsTsAdaptationField af(&pkt);
        af.PCR_flag_ = 1;
        utest_expect_af_encode_fails(&af, 2, "mux af PCR_flag");
    }
    if (true) {
        SrsTsAdaptationField af(&pkt);
        af.OPCR_flag_ = 1;
        utest_expect_af_encode_fails(&af, 2, "mux af OPCR_flag");
    }
    if (true) {
        SrsTsAdaptationField af(&pkt);
        af.splicing_point_flag_ = 1;
        utest_expect_af_encode_fails(&af, 2, "mux af splicing_point_flag");
    }
    if (true) {
        SrsTsAdaptationField af(&pkt);
        af.transport_private_data_flag_ = 1;
        af.transport_private_data_.push_back('a');
        utest_expect_af_encode_fails(&af, 2, "mux af transport_private_data_flag");
        utest_expect_af_encode_fails(&af, 3, "mux af transport_private_data");
    }
    if (true) {
        SrsTsAdaptationField af(&pkt);
        af.adaptation_field_extension_flag_ = 1;
        utest_expect_af_encode_fails(&af, 2, "mux af adaptation_field_extension_flag");
    }
    if (true) {
        SrsTsAdaptationField af(&pkt);
        af.adaptation_field_extension_flag_ = 1;
        af.ltw_flag_ = 1;
        utest_expect_af_encode_fails(&af, 4, "mux af ltw_flag");
    }
    if (true) {
        SrsTsAdaptationField af(&pkt);
        af.adaptation_field_extension_flag_ = 1;
        af.piecewise_rate_flag_ = 1;
        utest_expect_af_encode_fails(&af, 4, "mux af piecewise_rate_flag");
    }
    if (true) {
        SrsTsAdaptationField af(&pkt);
        af.adaptation_field_extension_flag_ = 1;
        af.seamless_splice_flag_ = 1;
        utest_expect_af_encode_fails(&af, 4, "mux af seamless_splice_flag");
    }
}

VOID TEST(KernelTsPesTest, DecodeAllOptionalFields)
{
    srs_error_t err;

    std::vector<char> data;
    data.push_back(0x00);
    data.push_back(0x00);
    data.push_back(0x01);
    data.push_back((char)0xe0); // video stream
    data.push_back(0x00);       // PES_packet_length 55: 3 bytes of flags, 48 bytes of header, 4 bytes of payload
    data.push_back(55);
    data.push_back((char)0x84); // '10', data_alignment_indicator
    data.push_back((char)0xbf); // PTS, ESCR, ES_rate, DSM_trick_mode, additional_copy_info, PES_CRC, PES_extension
    data.push_back(48);         // PES_header_data_length
    utest_append_pts(data, 0x02, 90000);
    for (int i = 0; i < 6; i++) {
        data.push_back((char)0xff); // ESCR
    }
    data.push_back((char)0x82); // marker, ES_rate 0x12345, marker
    data.push_back(0x46);
    data.push_back((char)0x8b);
    data.push_back(0x55);       // trick_mode_control 2, 5 bits 0x15
    data.push_back((char)0xaa); // marker, additional_copy_info 0x2a
    data.push_back((char)0xbe); // previous_PES_packet_CRC
    data.push_back((char)0xef);
    data.push_back((char)0xff); // private_data, pack_header_field, sequence_counter, P-STD, reserved, extension_2
    for (int i = 0; i < 16; i++) {
        data.push_back((char)i); // PES_private_data
    }
    data.push_back(2); // pack_field_length
    data.push_back((char)0xab);
    data.push_back((char)0xcd);
    data.push_back((char)0xd5); // marker, program_packet_sequence_counter 0x55
    data.push_back((char)0xea); // marker, MPEG1_MPEG2_identifier 1, original_stuff_length 0x2a
    data.push_back(0x7a);       // '01', P-STD_buffer_scale 1, P-STD_buffer_size 0x1abc
    data.push_back((char)0xbc);
    data.push_back((char)0x83); // marker, PES_extension_field_length 3
    data.push_back(1);
    data.push_back(2);
    data.push_back(3);
    data.push_back((char)0xff); // two stuffing bytes
    data.push_back((char)0xff);
    data.push_back('p');
    data.push_back('a');
    data.push_back('y');
    data.push_back('d');

    SrsMpegPES pes;
    SrsBuffer stream(&data[0], (int)data.size());
    HELPER_EXPECT_SUCCESS(pes.decode(&stream));

    EXPECT_EQ(57, stream.pos());
    EXPECT_EQ(0xe0, pes.stream_id_);
    EXPECT_EQ(1, pes.data_alignment_indicator_);
    EXPECT_EQ(90000, pes.pts_);
    EXPECT_EQ(90000, pes.dts_);
    EXPECT_EQ(1, pes.ESCR_flag_);
    EXPECT_EQ(0, pes.ESCR_base_);
    EXPECT_EQ(0x12345, pes.ES_rate_);
    EXPECT_EQ(2, pes.trick_mode_control_);
    EXPECT_EQ(0x15, pes.trick_mode_value_);
    EXPECT_EQ(0x2a, pes.additional_copy_info_);
    EXPECT_EQ(0xbeef, (uint16_t)pes.previous_PES_packet_CRC_);
    EXPECT_EQ(1, pes.PES_private_data_flag_);
    EXPECT_EQ(1, pes.pack_header_field_flag_);
    EXPECT_EQ(1, pes.program_packet_sequence_counter_flag_);
    EXPECT_EQ(1, pes.P_STD_buffer_flag_);
    EXPECT_EQ(7, pes.const1_value0_);
    EXPECT_EQ(1, pes.PES_extension_flag_2_);
    ASSERT_EQ(16u, pes.PES_private_data_.size());
    EXPECT_EQ(15, pes.PES_private_data_[15]);
    ASSERT_EQ(2u, pes.pack_field_.size());
    EXPECT_EQ((char)0xcd, pes.pack_field_[1]);
    EXPECT_EQ(0x55, pes.program_packet_sequence_counter_);
    EXPECT_EQ(1, pes.MPEG1_MPEG2_identifier_);
    EXPECT_EQ(0x2a, pes.original_stuff_length_);
    EXPECT_EQ(1, pes.P_STD_buffer_scale_);
    EXPECT_EQ(0x1abc, pes.P_STD_buffer_size_);
    ASSERT_EQ(3u, pes.PES_extension_field_.size());
    EXPECT_EQ(3, pes.PES_extension_field_[2]);
    EXPECT_EQ(2, pes.nb_stuffings_);
    EXPECT_TRUE(pes.has_payload_);
    EXPECT_EQ(4, pes.nb_payload_);
}

static void utest_expect_pes_decode_fails(const char *bytes, int size, const char *name)
{
    SrsMpegPES pes;
    std::vector<char> data(bytes, bytes + size);
    SrsBuffer stream(&data[0], size);
    srs_error_t err = pes.decode(&stream);
    EXPECT_NE(std::string::npos, srs_error_desc(err).find(name)) << name;
    srs_freep(err);
}

VOID TEST(KernelTsPesTest, DecodeTruncatedFieldsFail)
{
    // PES_extension with PES_private_data, but only 3 bytes of the 16.
    const char ext_payload[] = {0, 0, 1, (char)0xe0, 0, 0, (char)0x80, 0x01, 20, (char)0x80, 1, 2, 3};
    utest_expect_pes_decode_fails(ext_payload, sizeof(ext_payload), "demux PSE ext payload");

    // PES_private_data, then a pack_header_field of 10 bytes, but only 2 follow.
    std::vector<char> ext_pack;
    const char head[] = {0, 0, 1, (char)0xe0, 0, 0, (char)0x80, 0x01, 40, (char)0xc0};
    ext_pack.insert(ext_pack.end(), head, head + sizeof(head));
    ext_pack.insert(ext_pack.end(), 16, 0);
    ext_pack.push_back(10);
    ext_pack.push_back(1);
    ext_pack.push_back(2);
    utest_expect_pes_decode_fails(&ext_pack[0], (int)ext_pack.size(), "demux PSE ext pack");

    // PES_extension_field of 5 bytes, but only 2 follow.
    const char ext_field[] = {0, 0, 1, (char)0xe0, 0, 0, (char)0x80, 0x01, 10, 0x01, 5, 1, 2};
    utest_expect_pes_decode_fails(ext_field, sizeof(ext_field), "demux PSE ext field");

    // PES_header_data_length is 4, but no stuffing byte follows.
    const char stuffings[] = {0, 0, 1, (char)0xe0, 0, 0, (char)0x80, 0x00, 4};
    utest_expect_pes_decode_fails(stuffings, sizeof(stuffings), "demux PSE stuffings");

    // A program stream map must have a PES_packet_length.
    const char empty_psm[] = {0, 0, 1, (char)0xbc, 0, 0};
    utest_expect_pes_decode_fails(empty_psm, sizeof(empty_psm), "Invalid PES_packet_length=0 for PS");
}

VOID TEST(KernelTsPesTest, PaddingStreamIsDropped)
{
    srs_error_t err;

    const char data[] = {0, 0, 1, (char)0xbe, 0, 4, (char)0xff, (char)0xff, (char)0xff, (char)0xff};
    std::vector<char> buf(data, data + sizeof(data));
    SrsMpegPES pes;
    SrsBuffer stream(&buf[0], (int)buf.size());
    HELPER_EXPECT_SUCCESS(pes.decode(&stream));
    EXPECT_EQ(4, pes.nb_paddings_);
    EXPECT_EQ((int)buf.size(), stream.pos());
    EXPECT_FALSE(pes.has_payload_);
}

VOID TEST(KernelTsPesTest, EncodeAllOptionalFields)
{
    srs_error_t err;

    SrsMpegPES pes;
    pes.packet_start_code_prefix_ = 0x01;
    pes.stream_id_ = 0xe0;
    pes.PES_packet_length_ = 100;
    pes.PTS_DTS_flags_ = 0x02;
    pes.pts_ = 180000;
    pes.ESCR_flag_ = 1;
    pes.ES_rate_flag_ = 1;
    pes.DSM_trick_mode_flag_ = 1;
    pes.additional_copy_info_flag_ = 1;
    pes.PES_CRC_flag_ = 1;
    pes.PES_extension_flag_ = 1;
    pes.PES_private_data_flag_ = 1;
    pes.PES_private_data_.resize(16, 0);
    pes.pack_header_field_flag_ = 1;
    pes.pack_field_.resize(2, 0);
    pes.program_packet_sequence_counter_flag_ = 1;
    pes.P_STD_buffer_flag_ = 1;
    pes.PES_extension_flag_2_ = 1;
    pes.PES_extension_field_.resize(3, 0);

    // 9 bytes of fixed header, 19 bytes of optional fields and 27 bytes of PES extension.
    int size = pes.size();
    EXPECT_EQ(55, size);
    EXPECT_EQ(46, pes.PES_header_data_length_);

    std::vector<char> data(size, 0);
    SrsBuffer stream(&data[0], size);
    HELPER_EXPECT_SUCCESS(pes.encode(&stream));
    EXPECT_EQ(size, stream.pos());

    EXPECT_EQ(0x01, data[2]);
    EXPECT_EQ((char)0xe0, data[3]);
    // PES_packet_length counts the payload, the 3 bytes of flags and the header data.
    EXPECT_EQ(0, data[4]);
    EXPECT_EQ(149, (uint8_t)data[5]);
    EXPECT_EQ((char)0x80, data[6]);
    EXPECT_EQ((char)0xbf, data[7]);
    EXPECT_EQ(46, data[8]);

    // The PTS is written, the other optional fields are skipped up to the PES extension flags.
    SrsMpegPES decoded;
    SrsBuffer pts(&data[9], 5);
    int64_t v = 0;
    HELPER_EXPECT_SUCCESS(decoded.decode_33bits_dts_pts(&pts, &v));
    EXPECT_EQ(180000, v);
    EXPECT_EQ(0xf1, (uint8_t)data[27] & 0xf1);
}

VOID TEST(KernelTsPesTest, EncodeThenDecodePtsAndDts)
{
    srs_error_t err;

    SrsMpegPES pes;
    pes.packet_start_code_prefix_ = 0x01;
    pes.stream_id_ = 0xc0;
    pes.PES_packet_length_ = 10;
    pes.PTS_DTS_flags_ = 0x03;
    pes.pts_ = 0x1ffffffffLL;
    pes.dts_ = 0x1fffeffffLL;

    int size = pes.size();
    std::vector<char> data(size, 0);
    SrsBuffer stream(&data[0], size);
    HELPER_EXPECT_SUCCESS(pes.encode(&stream));

    SrsMpegPES decoded;
    SrsBuffer input(&data[0], size);
    HELPER_EXPECT_SUCCESS(decoded.decode(&input));
    EXPECT_EQ(0xc0, decoded.stream_id_);
    EXPECT_EQ(0x03, decoded.PTS_DTS_flags_);
    EXPECT_EQ(0x1ffffffffLL, decoded.pts_);
    EXPECT_EQ(0x1fffeffffLL, decoded.dts_);
    EXPECT_EQ(10, decoded.nb_payload_);
}

VOID TEST(KernelTsPesTest, EncodeTruncatedExtensionFails)
{
    srs_error_t err;

    SrsMpegPES pes;
    pes.packet_start_code_prefix_ = 0x01;
    pes.stream_id_ = 0xe0;
    pes.PES_extension_flag_ = 1;
    pes.PES_private_data_flag_ = 1;
    pes.PES_private_data_.resize(16, 0);

    // The fixed header and the extension flags fit, the 16 bytes of private data do not.
    std::vector<char> data(12, 0);
    SrsBuffer stream(&data[0], (int)data.size());
    err = pes.encode(&stream);
    EXPECT_NE(std::string::npos, srs_error_desc(err).find("mux PSE ext payload"));
    srs_freep(err);
}

VOID TEST(KernelMp4DumpTest, LargeBoxWithChildren)
{
    SrsMp4MovieBox moov;
    moov.smallsize_ = 1; // The size field is 1 when a 64 bits largesize follows.
    moov.largesize_ = 0x100000000LL;
    moov.append(new SrsMp4MovieHeaderBox());

    std::stringstream ss;
    SrsMp4DumpContext dc;
    moov.dumps(ss, dc);
    std::string s = ss.str();
    EXPECT_NE(std::string::npos, s.find("moov, 4294967296(large)B"));
    EXPECT_NE(std::string::npos, s.find(", 1 boxes"));
    EXPECT_NE(std::string::npos, s.find("    mvhd"));
}

static int utest_count(const std::string &s, const std::string &sub)
{
    int n = 0;
    for (size_t pos = s.find(sub); pos != std::string::npos; pos = s.find(sub, pos + 1)) {
        n++;
    }
    return n;
}

static std::string utest_dumps(SrsMp4Box *box, bool summary)
{
    std::stringstream ss;
    SrsMp4DumpContext dc;
    dc.summary_ = summary;
    box->dumps(ss, dc);
    return ss.str();
}

// A summary dump lists only the first 8 entries of a table.
VOID TEST(KernelMp4DumpTest, SummaryLimitsVectorTables)
{
    if (true) {
        SrsMp4DecodingTime2SampleBox stts;
        SrsMp4CompositionTime2SampleBox ctts;
        SrsMp4EditListBox elst;
        for (int i = 0; i < 10; i++) {
            SrsMp4SttsEntry e0;
            e0.sample_count_ = 1;
            stts.entries_.push_back(e0);
            SrsMp4CttsEntry e1;
            e1.sample_count_ = 1;
            ctts.entries_.push_back(e1);
            SrsMp4ElstEntry e2;
            e2.segment_duration_ = 100;
            elst.entries_.push_back(e2);
        }
        EXPECT_EQ(8, utest_count(utest_dumps(&stts, true), "count=1, delta="));
        EXPECT_EQ(10, utest_count(utest_dumps(&stts, false), "count=1, delta="));
        EXPECT_EQ(8, utest_count(utest_dumps(&ctts, true), "count=1, offset="));
        EXPECT_EQ(8, utest_count(utest_dumps(&elst, true), "Entry, 100TBN"));
    }

    if (true) {
        SrsMp4TrackFragmentRunBox trun;
        SrsMp4DataReferenceBox dref;
        SrsMp4SampleDescriptionBox stsd;
        trun.flags_ = SrsMp4TrunFlagsSampleDuration;
        for (int i = 0; i < 10; i++) {
            trun.entries_.push_back(new SrsMp4TrunEntry(&trun));
            dref.append(new SrsMp4DataEntryUrlBox());
            stsd.append(new SrsMp4VisualSampleEntry(SrsMp4BoxTypeAVC1));
        }
        EXPECT_EQ(8, utest_count(utest_dumps(&dref, true), "URL: "));
        EXPECT_EQ(8, utest_count(utest_dumps(&stsd, true), "avc1"));
        EXPECT_EQ(8, utest_count(utest_dumps(&trun, true), "duration=0"));
    }
}

VOID TEST(KernelMp4DumpTest, ArrayTables)
{
    SrsMp4SyncSampleBox stss;
    stss.entry_count_ = 10;
    stss.sample_numbers_ = new uint32_t[10];
    SrsMp4ChunkOffsetBox stco;
    stco.entry_count_ = 3;
    stco.entries_ = new uint32_t[3];
    SrsMp4ChunkLargeOffsetBox co64;
    co64.entry_count_ = 2;
    co64.entries_ = new uint64_t[2];
    SrsMp4SampleSizeBox stsz;
    stsz.sample_count_ = 3;
    stsz.entry_sizes_ = new uint32_t[3];
    SrsMp4Sample2ChunkBox stsc;
    stsc.entry_count_ = 2;
    stsc.entries_ = new SrsMp4StscEntry[2];
    for (int i = 0; i < 10; i++) {
        stss.sample_numbers_[i] = 1000 + i;
    }
    for (int i = 0; i < 3; i++) {
        stco.entries_[i] = 2000 + i;
        stsz.entry_sizes_[i] = 3000 + i;
    }
    co64.entries_[0] = 0x100000000LL;
    co64.entries_[1] = 0x100000001LL;
    stsc.entries_[0].first_chunk_ = 1;
    stsc.entries_[0].samples_per_chunk_ = 5;
    stsc.entries_[0].sample_description_index_ = 1;
    stsc.entries_[1].first_chunk_ = 3;
    stsc.entries_[1].samples_per_chunk_ = 2;
    stsc.entries_[1].sample_description_index_ = 1;

    std::string s = utest_dumps(&stss, true);
    EXPECT_NE(std::string::npos, s.find("1000, 1001"));
    EXPECT_NE(std::string::npos, s.find("1007"));
    EXPECT_EQ(std::string::npos, s.find("1008"));
    EXPECT_NE(std::string::npos, utest_dumps(&stss, false).find("1008, 1009"));

    EXPECT_NE(std::string::npos, utest_dumps(&stco, false).find("2000, 2001, 2002"));
    EXPECT_NE(std::string::npos, utest_dumps(&co64, false).find("4294967296, 4294967297"));
    EXPECT_NE(std::string::npos, utest_dumps(&stsz, false).find("3000, 3001, 3002"));

    s = utest_dumps(&stsc, false);
    EXPECT_NE(std::string::npos, s.find("first=1, samples=5, index=1"));
    EXPECT_NE(std::string::npos, s.find("first=3, samples=2, index=1"));
}

VOID TEST(KernelMp4DumpTest, EntriesAndDescriptors)
{
    if (true) {
        SrsMp4ElstEntry entry;
        entry.segment_duration_ = 3000;
        entry.media_time_ = 1024;
        std::stringstream ss;
        SrsMp4DumpContext dc;
        entry.dumps(ss, dc);
        EXPECT_NE(std::string::npos, ss.str().find("Entry, 3000TBN, start=1024TBN"));
    }

    if (true) {
        SrsMp4EsdsBox esds;
        std::string s = utest_dumps(&esds, false);
        EXPECT_NE(std::string::npos, s.find("esds"));
        EXPECT_NE(std::string::npos, s.find(", ID="));
    }

    if (true) {
        SrsMp4SegmentIndexBox sidx;
        sidx.reference_id_ = 1;
        sidx.timescale_ = 1000;
        for (int i = 0; i < 2; i++) {
            SrsMp4SegmentIndexEntry entry;
            entry.reference_type_ = 0;
            entry.referenced_size_ = 5000 + i;
            entry.subsegment_duration_ = 2000;
            entry.starts_with_SAP_ = 1;
            entry.SAP_type_ = 1;
            entry.SAP_delta_time_ = 0;
            sidx.entries_.push_back(entry);
        }
        std::string s = utest_dumps(&sidx, false);
        EXPECT_NE(std::string::npos, s.find("#0, ref=0/5000, duration=2000, SAP=1/1/0"));
        EXPECT_NE(std::string::npos, s.find("#1, ref=0/5001"));
    }
}
