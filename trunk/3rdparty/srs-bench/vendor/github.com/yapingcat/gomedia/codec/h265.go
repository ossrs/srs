package codec

import (
    "bytes"
)

// nal_unit_header() {
//     forbidden_zero_bit      f(1)
//     nal_unit_type           u(6)
//     nuh_layer_id            u(6)
//     nuh_temporal_id_plus1   u(3)
// }

type H265NaluHdr struct {
    Forbidden_zero_bit    uint8
    Nal_unit_type         uint8
    Nuh_layer_id          uint8
    Nuh_temporal_id_plus1 uint8
}

func (hdr *H265NaluHdr) Decode(bs *BitStream) {
    hdr.Forbidden_zero_bit = bs.GetBit()
    hdr.Nal_unit_type = bs.Uint8(6)
    hdr.Nuh_layer_id = bs.Uint8(6)
    hdr.Nuh_temporal_id_plus1 = bs.Uint8(3)
}

type VPS struct {
    Vps_video_parameter_set_id               uint8
    Vps_base_layer_internal_flag             uint8
    Vps_base_layer_available_flag            uint8
    Vps_max_layers_minus1                    uint8
    Vps_max_sub_layers_minus1                uint8
    Vps_temporal_id_nesting_flag             uint8
    Vps_reserved_0xffff_16bits               uint16
    Ptl                                      ProfileTierLevel
    Vps_sub_layer_ordering_info_present_flag uint8
    Vps_max_dec_pic_buffering_minus1         [8]uint64
    Vps_max_num_reorder_pics                 [8]uint64
    Vps_max_latency_increase_plus1           [8]uint64
    Vps_max_layer_id                         uint8
    Vps_num_layer_sets_minus1                uint64
    Layer_id_included_flag                   [][]uint8
    Vps_timing_info_present_flag             uint8
    TimeInfo                                 VPSTimeInfo
    // Vps_extension_flag                       uint8
}

type VPSTimeInfo struct {
    Vps_num_units_in_tick               uint32
    Vps_time_scale                      uint32
    Vps_poc_proportional_to_timing_flag uint8
    Vps_num_ticks_poc_diff_one_minus1   uint64
    Vps_num_hrd_parameters              uint64
    Hrd_layer_set_idx                   []uint64
    Cprms_present_flag                  []uint8
}

type ProfileTierLevel struct {
    General_profile_space              uint8
    General_tier_flag                  uint8
    General_profile_idc                uint8
    General_profile_compatibility_flag uint32
    General_constraint_indicator_flag  uint64
    General_level_idc                  uint8
    Sub_layer_profile_present_flag     [8]uint8
    Sub_layer_level_present_flag       [8]uint8
}

//nalu without startcode
func (vps *VPS) Decode(nalu []byte) {
    sodb := CovertRbspToSodb(nalu)
    bs := NewBitStream(sodb)
    hdr := H265NaluHdr{}
    hdr.Decode(bs)
    vps.Vps_video_parameter_set_id = bs.Uint8(4)
    vps.Vps_base_layer_internal_flag = bs.Uint8(1)
    vps.Vps_base_layer_available_flag = bs.Uint8(1)
    vps.Vps_max_layers_minus1 = bs.Uint8(6)
    vps.Vps_max_sub_layers_minus1 = bs.Uint8(3)
    vps.Vps_temporal_id_nesting_flag = bs.Uint8(1)
    vps.Vps_reserved_0xffff_16bits = bs.Uint16(16)
    vps.Ptl = Profile_tier_level(1, vps.Vps_max_sub_layers_minus1, bs)
    vps.Vps_sub_layer_ordering_info_present_flag = bs.Uint8(1)
    var i int
    if vps.Vps_sub_layer_ordering_info_present_flag > 0 {
        i = 0
    } else {
        i = int(vps.Vps_max_sub_layers_minus1)
    }
    for ; i <= int(vps.Vps_max_sub_layers_minus1); i++ {
        vps.Vps_max_dec_pic_buffering_minus1[i] = bs.ReadUE()
        vps.Vps_max_num_reorder_pics[i] = bs.ReadUE()
        vps.Vps_max_latency_increase_plus1[i] = bs.ReadUE()
    }
    vps.Vps_max_layer_id = bs.Uint8(6)
    vps.Vps_num_layer_sets_minus1 = bs.ReadUE()
    vps.Layer_id_included_flag = make([][]uint8, vps.Vps_num_layer_sets_minus1)
    for i := 1; i <= int(vps.Vps_num_layer_sets_minus1); i++ {
        vps.Layer_id_included_flag[i] = make([]uint8, vps.Vps_max_layer_id)
        for j := 0; j <= int(vps.Vps_max_layer_id); j++ {
            vps.Layer_id_included_flag[i][j] = bs.Uint8(1)
        }
    }
    vps.Vps_timing_info_present_flag = bs.Uint8(1)
    if vps.Vps_timing_info_present_flag == 1 {
        vps.TimeInfo = ParserVPSTimeinfo(bs)
    }
}

//ffmpeg hevc.c
//static void hvcc_parse_ptl(GetBitContext *gb,HEVCDecoderConfigurationRecord *hvcc,unsigned int max_sub_layers_minus1)
func Profile_tier_level(profilePresentFlag uint8, maxNumSubLayersMinus1 uint8, bs *BitStream) ProfileTierLevel {
    var ptl ProfileTierLevel
    ptl.General_profile_space = bs.Uint8(2)
    ptl.General_tier_flag = bs.Uint8(1)
    ptl.General_profile_idc = bs.Uint8(5)
    ptl.General_profile_compatibility_flag = bs.Uint32(32)
    ptl.General_constraint_indicator_flag = bs.GetBits(48)
    ptl.General_level_idc = bs.Uint8(8)
    for i := 0; i < int(maxNumSubLayersMinus1); i++ {
        ptl.Sub_layer_profile_present_flag[i] = bs.GetBit()
        ptl.Sub_layer_level_present_flag[i] = bs.GetBit()
    }
    if maxNumSubLayersMinus1 > 0 {
        for i := maxNumSubLayersMinus1; i < 8; i++ {
            bs.SkipBits(2)
        }
    }

    for i := 0; i < int(maxNumSubLayersMinus1); i++ {
        if ptl.Sub_layer_profile_present_flag[i] == 1 {
            /*
             * sub_layer_profile_space[i]                     u(2)
             * sub_layer_tier_flag[i]                         u(1)
             * sub_layer_profile_idc[i]                       u(5)
             * sub_layer_profile_compatibility_flag[i][0..31] u(32)
             * sub_layer_progressive_source_flag[i]           u(1)
             * sub_layer_interlaced_source_flag[i]            u(1)
             * sub_layer_non_packed_constraint_flag[i]        u(1)
             * sub_layer_frame_only_constraint_flag[i]        u(1)
             * sub_layer_reserved_zero_44bits[i]              u(44)
             */
            bs.SkipBits(88)
        }
        if ptl.Sub_layer_level_present_flag[i] == 1 {
            bs.SkipBits(8)
        }
    }
    return ptl
}

func ParserVPSTimeinfo(bs *BitStream) VPSTimeInfo {
    var ti VPSTimeInfo
    ti.Vps_num_units_in_tick = bs.Uint32(32)
    ti.Vps_time_scale = bs.Uint32(32)
    ti.Vps_poc_proportional_to_timing_flag = bs.Uint8(1)
    if ti.Vps_poc_proportional_to_timing_flag == 1 {
        ti.Vps_num_ticks_poc_diff_one_minus1 = bs.ReadUE()
    }
    ti.Vps_num_hrd_parameters = bs.ReadUE()
    // for i := 0; i < int(ti.Vps_num_hrd_parameters); i++ {
    //     ti.Hrd_layer_set_idx[i] = bs.ReadUE()
    //     if i > 0 {
    //         ti.Cprms_present_flag[i] = bs.Uint8(1)
    //     }
    //     //Hrd_parameters(ti.Cprms_present_flag[i])
    // }
    return ti
}

type H265RawSPS struct {
    Sps_video_parameter_set_id               uint8
    Sps_max_sub_layers_minus1                uint8
    Sps_temporal_id_nesting_flag             uint8
    Ptl                                      ProfileTierLevel
    Sps_seq_parameter_set_id                 uint64
    Chroma_format_idc                        uint64
    Pic_width_in_luma_samples                uint64
    Pic_height_in_luma_samples               uint64
    Conformance_window_flag                  uint8
    Conf_win_left_offset                     uint64
    Conf_win_right_offset                    uint64
    Conf_win_top_offset                      uint64
    Conf_win_bottom_offset                   uint64
    Bit_depth_luma_minus8                    uint64
    Bit_depth_chroma_minus8                  uint64
    Log2_max_pic_order_cnt_lsb_minus4        uint64
    Sps_sub_layer_ordering_info_present_flag uint8
    Vui_parameters_present_flag              uint8
    Vui                                      VUI_Parameters
}

//nalu without startcode
func (sps *H265RawSPS) Decode(nalu []byte) {
    sodb := CovertRbspToSodb(nalu)
    bs := NewBitStream(sodb)
    hdr := H265NaluHdr{}
    hdr.Decode(bs)
    sps.Sps_video_parameter_set_id = bs.Uint8(4)
    sps.Sps_max_sub_layers_minus1 = bs.Uint8(3)
    sps.Sps_temporal_id_nesting_flag = bs.Uint8(1)
    sps.Ptl = Profile_tier_level(1, sps.Sps_max_sub_layers_minus1, bs)
    sps.Sps_seq_parameter_set_id = bs.ReadUE()
    sps.Chroma_format_idc = bs.ReadUE()
    if sps.Chroma_format_idc == 3 {
        bs.SkipBits(1)
    }
    sps.Pic_width_in_luma_samples = bs.ReadUE()
    sps.Pic_height_in_luma_samples = bs.ReadUE()
    sps.Conformance_window_flag = bs.Uint8(1)
    if sps.Conformance_window_flag == 1 {
        sps.Conf_win_left_offset = bs.ReadUE()
        sps.Conf_win_right_offset = bs.ReadUE()
        sps.Conf_win_top_offset = bs.ReadUE()
        sps.Conf_win_bottom_offset = bs.ReadUE()
    }
    sps.Bit_depth_luma_minus8 = bs.ReadUE()
    sps.Bit_depth_chroma_minus8 = bs.ReadUE()
    sps.Log2_max_pic_order_cnt_lsb_minus4 = bs.ReadUE()
    sps.Sps_sub_layer_ordering_info_present_flag = bs.Uint8(1)
    i := 0
    if sps.Sps_sub_layer_ordering_info_present_flag == 0 {
        i = int(sps.Sps_max_sub_layers_minus1)
    }
    for ; i <= int(sps.Sps_max_sub_layers_minus1); i++ {
        bs.ReadUE()
        bs.ReadUE()
        bs.ReadUE()
    }

    bs.ReadUE() // log2_min_luma_coding_block_size_minus3
    bs.ReadUE() // log2_diff_max_min_luma_coding_block_size
    bs.ReadUE() // log2_min_transform_block_size_minus2
    bs.ReadUE() // log2_diff_max_min_transform_block_size
    bs.ReadUE() // max_transform_hierarchy_depth_inter
    bs.ReadUE() // max_transform_hierarchy_depth_intra
    scaling_list_enabled_flag := bs.GetBit()
    if scaling_list_enabled_flag > 0 {
        sps_scaling_list_data_present_flag := bs.GetBit()
        if sps_scaling_list_data_present_flag > 0 {
            scaling_list_data(bs)
        }
    }

    bs.SkipBits(1)
    bs.SkipBits(1)
    if bs.GetBit() == 1 {
        bs.GetBits(4)
        bs.GetBits(4)
        bs.ReadUE()
        bs.ReadUE()
        bs.GetBit()
    }
    num_short_term_ref_pic_sets := bs.ReadUE()
    if num_short_term_ref_pic_sets > 64 {
        panic("beyond HEVC_MAX_SHORT_TERM_REF_PIC_SETS")
    }
    var num_delta_pocs [64]uint32
    for i := 0; i < int(num_short_term_ref_pic_sets); i++ {
        parse_rps(i, num_short_term_ref_pic_sets, num_delta_pocs, bs)
    }
    if bs.GetBit() == 1 {
        num_long_term_ref_pics_sps := bs.ReadUE()
        for i := 0; i < int(num_long_term_ref_pics_sps); i++ {
            length := Min(int(sps.Log2_max_pic_order_cnt_lsb_minus4+4), 16)
            bs.SkipBits(length)
            bs.SkipBits(1)
        }
    }
    bs.SkipBits(1)
    bs.SkipBits(1)
    sps.Vui_parameters_present_flag = bs.GetBit()
    if sps.Vui_parameters_present_flag == 1 {
        sps.Vui.Decode(bs, sps.Sps_max_sub_layers_minus1)
    }
}

type VUI_Parameters struct {
    Aspect_ratio_info_present_flag          uint8
    Overscan_info_present_flag              uint8
    Chroma_loc_info_present_flag            uint8
    Neutral_chroma_indication_flag          uint8
    Field_seq_flag                          uint8
    Frame_field_info_present_flag           uint8
    Default_display_window_flag             uint8
    Vui_timing_info_present_flag            uint8
    Vui_num_units_in_tick                   uint32
    Vui_time_scale                          uint32
    Vui_poc_proportional_to_timing_flag     uint8
    Vui_hrd_parameters_present_flag         uint8
    Bitstream_restriction_flag              uint8
    Tiles_fixed_structure_flag              uint8
    Motion_vectors_over_pic_boundaries_flag uint8
    Restricted_ref_pic_lists_flag           uint8
    Min_spatial_segmentation_idc            uint64
    Max_bytes_per_pic_denom                 uint64
    Max_bits_per_min_cu_denom               uint64
    Log2_max_mv_length_horizontal           uint64
    Log2_max_mv_length_vertical             uint64
}

func (vui *VUI_Parameters) Decode(bs *BitStream, max_sub_layers_minus1 uint8) {
    vui.Aspect_ratio_info_present_flag = bs.Uint8(1)
    if vui.Aspect_ratio_info_present_flag == 1 {
        if bs.Uint8(8) == 255 {
            bs.SkipBits(32)
        }
    }
    vui.Overscan_info_present_flag = bs.Uint8(1)
    if vui.Overscan_info_present_flag == 1 {
        bs.SkipBits(1)
    }
    if bs.GetBit() == 1 {
        bs.SkipBits(4)
        if bs.GetBit() == 1 {
            bs.SkipBits(24)
        }
    }
    vui.Chroma_loc_info_present_flag = bs.GetBit()
    if vui.Chroma_loc_info_present_flag == 1 {
        bs.ReadUE()
        bs.ReadUE()
    }
    vui.Neutral_chroma_indication_flag = bs.GetBit()
    vui.Field_seq_flag = bs.GetBit()
    vui.Frame_field_info_present_flag = bs.GetBit()
    vui.Default_display_window_flag = bs.GetBit()
    if vui.Default_display_window_flag == 1 {
        bs.ReadUE()
        bs.ReadUE()
        bs.ReadUE()
        bs.ReadUE()
    }
    vui.Vui_timing_info_present_flag = bs.GetBit()
    if vui.Vui_timing_info_present_flag == 1 {
        vui.Vui_num_units_in_tick = bs.Uint32(32)
        vui.Vui_time_scale = bs.Uint32(32)
        vui.Vui_poc_proportional_to_timing_flag = bs.GetBit()
        if vui.Vui_poc_proportional_to_timing_flag == 1 {
            bs.ReadUE()
        }
        vui.Vui_hrd_parameters_present_flag = bs.GetBit()
        if vui.Vui_hrd_parameters_present_flag == 1 {
            skip_hrd_parameters(1, uint32(max_sub_layers_minus1), bs)
        }
    }
    vui.Bitstream_restriction_flag = bs.GetBit()
    if vui.Bitstream_restriction_flag == 1 {
        vui.Tiles_fixed_structure_flag = bs.GetBit()
        vui.Motion_vectors_over_pic_boundaries_flag = bs.GetBit()
        vui.Restricted_ref_pic_lists_flag = bs.GetBit()
        vui.Min_spatial_segmentation_idc = bs.ReadUE()
        vui.Max_bytes_per_pic_denom = bs.ReadUE()
        vui.Max_bits_per_min_cu_denom = bs.ReadUE()
        vui.Log2_max_mv_length_horizontal = bs.ReadUE()
        vui.Log2_max_mv_length_vertical = bs.ReadUE()
    }
}

func skip_hrd_parameters(cprms_present_flag uint8, max_sub_layers_minus1 uint32, bs *BitStream) {
    nal_hrd_parameters_present_flag := uint8(0)
    vcl_hrd_parameters_present_flag := uint8(0)
    sub_pic_hrd_params_present_flag := uint8(0)
    if cprms_present_flag == 1 {
        nal_hrd_parameters_present_flag = bs.GetBit()
        vcl_hrd_parameters_present_flag = bs.GetBit()

        if nal_hrd_parameters_present_flag == 1 || vcl_hrd_parameters_present_flag == 1 {
            sub_pic_hrd_params_present_flag = bs.GetBit()

            if sub_pic_hrd_params_present_flag == 1 {
                /*
                 * tick_divisor_minus2                          u(8)
                 * du_cpb_removal_delay_increment_length_minus1 u(5)
                 * sub_pic_cpb_params_in_pic_timing_sei_flag    u(1)
                 * dpb_output_delay_du_length_minus1            u(5)
                 */
                bs.SkipBits(19)
            }

            bs.SkipBits(8)

            if sub_pic_hrd_params_present_flag == 1 {
                // cpb_size_du_scale
                bs.SkipBits(4)
            }

            /*
             * initial_cpb_removal_delay_length_minus1 u(5)
             * au_cpb_removal_delay_length_minus1      u(5)
             * dpb_output_delay_length_minus1          u(5)
             */
            bs.SkipBits(15)
        }
    }
    for i := 0; i <= int(max_sub_layers_minus1); i++ {
        fixed_pic_rate_general_flag := bs.GetBit()
        fixed_pic_rate_within_cvs_flag := uint8(0)
        low_delay_hrd_flag := uint8(0)
        cpb_cnt_minus1 := uint32(0)
        if fixed_pic_rate_general_flag == 0 {
            fixed_pic_rate_within_cvs_flag = bs.GetBit()
        }
        if fixed_pic_rate_within_cvs_flag == 1 {
            bs.ReadUE()
        } else {
            low_delay_hrd_flag = bs.GetBit()
        }
        if low_delay_hrd_flag == 0 {
            cpb_cnt_minus1 = uint32(bs.ReadUE())
            if cpb_cnt_minus1 > 31 {
                panic("cpb_cnt_minus1 > 31")
            }
        }
        skip_sub_layer_hrd_parameters := func() {
            for i := 0; i < int(cpb_cnt_minus1); i++ {
                bs.ReadUE()
                bs.ReadUE()
                if sub_pic_hrd_params_present_flag == 1 {
                    bs.ReadUE()
                    bs.ReadUE()
                }
                bs.SkipBits(1)
            }
        }
        if nal_hrd_parameters_present_flag == 1 {
            skip_sub_layer_hrd_parameters()
        }
        if vcl_hrd_parameters_present_flag == 1 {
            skip_sub_layer_hrd_parameters()
        }
    }
}

func scaling_list_data(bs *BitStream) {
    for i := 0; i < 4; i++ {
        maxj := 6
        if i == 3 {
            maxj = 2
        }
        for j := 0; j < maxj; j++ {
            if bs.GetBit() == 0 {
                bs.ReadUE()
            } else {
                num_coeffs := Min(64, 1<<(4+(i<<1)))
                if i > 1 {
                    bs.ReadSE()
                }
                for k := 0; k < num_coeffs; k++ {
                    bs.ReadSE()
                }
            }
        }
    }
}

func parse_rps(rps_idx int, nums_rps uint64, num_delta_pocs [64]uint32, bs *BitStream) {
    if rps_idx > 0 && bs.GetBit() > 0 {
        if rps_idx > int(nums_rps) {
            panic("rps_idx > int(nums_rps)")
        }
        bs.SkipBits(1)
        bs.ReadUE()
        num_delta_pocs[rps_idx] = 0
        for i := uint32(0); i <= num_delta_pocs[rps_idx-1]; i++ {
            var use_delta_flag uint8
            var used_by_curr_pic_flag uint8 = bs.GetBit()
            if used_by_curr_pic_flag == 0 {
                use_delta_flag = bs.GetBit()
            }
            if use_delta_flag > 0 || used_by_curr_pic_flag > 0 {
                num_delta_pocs[rps_idx]++
            }
        }
    } else {
        num_negative_pics := bs.ReadUE()
        num_positive_pics := bs.ReadUE()
        if (num_negative_pics+num_positive_pics)*2 > uint64(bs.RemainBits()) {
            panic("(num_negative_pics + num_positive_pics) * 2> uint64(bs.RemainBits())")
        }
        for i := 0; i < int(num_negative_pics); i++ {
            bs.ReadUE()
            bs.SkipBits(1)
        }
        for i := 0; i < int(num_positive_pics); i++ {
            bs.ReadUE()
            bs.SkipBits(1)
        }
    }
}

type H265RawPPS struct {
    Pps_pic_parameter_set_id                 uint64
    Pps_seq_parameter_set_id                 uint64
    Dependent_slice_segments_enabled_flag    uint8
    Output_flag_present_flag                 uint8
    Num_extra_slice_header_bits              uint8
    Sign_data_hiding_enabled_flag            uint8
    Cabac_init_present_flag                  uint8
    Num_ref_idx_l0_default_active_minus1     uint64
    Num_ref_idx_l1_default_active_minus1     uint64
    Init_qp_minus26                          int64
    Constrained_intra_pred_flag              uint8
    Transform_skip_enabled_flag              uint8
    Cu_qp_delta_enabled_flag                 uint8
    Diff_cu_qp_delta_depth                   uint64
    Pps_cb_qp_offset                         int64
    Pps_cr_qp_offset                         int64
    Pps_slice_chroma_qp_offsets_present_flag uint8
    Weighted_pred_flag                       uint8
    Weighted_bipred_flag                     uint8
    Transquant_bypass_enabled_flag           uint8
    Tiles_enabled_flag                       uint8
    Entropy_coding_sync_enabled_flag         uint8
}

//nalu without startcode
func (pps *H265RawPPS) Decode(nalu []byte) {
    sodb := CovertRbspToSodb(nalu)
    bs := NewBitStream(sodb)
    hdr := H265NaluHdr{}
    hdr.Decode(bs)
    pps.Pps_pic_parameter_set_id = bs.ReadUE()
    pps.Pps_seq_parameter_set_id = bs.ReadUE()
    pps.Dependent_slice_segments_enabled_flag = bs.GetBit()
    pps.Output_flag_present_flag = bs.GetBit()
    pps.Num_extra_slice_header_bits = bs.Uint8(3)
    pps.Sign_data_hiding_enabled_flag = bs.GetBit()
    pps.Cabac_init_present_flag = bs.GetBit()
    pps.Num_ref_idx_l0_default_active_minus1 = bs.ReadUE()
    pps.Num_ref_idx_l1_default_active_minus1 = bs.ReadUE()
    pps.Init_qp_minus26 = bs.ReadSE()
    pps.Constrained_intra_pred_flag = bs.GetBit()
    pps.Transform_skip_enabled_flag = bs.GetBit()
    pps.Cu_qp_delta_enabled_flag = bs.GetBit()
    if pps.Cu_qp_delta_enabled_flag == 1 {
        pps.Diff_cu_qp_delta_depth = bs.ReadUE()
    }
    pps.Pps_cb_qp_offset = bs.ReadSE()
    pps.Pps_cr_qp_offset = bs.ReadSE()
    pps.Pps_slice_chroma_qp_offsets_present_flag = bs.GetBit()
    pps.Weighted_pred_flag = bs.GetBit()
    pps.Weighted_bipred_flag = bs.GetBit()
    pps.Transquant_bypass_enabled_flag = bs.GetBit()
    pps.Tiles_enabled_flag = bs.GetBit()
    pps.Entropy_coding_sync_enabled_flag = bs.GetBit()
}

func GetH265Resolution(sps []byte) (width uint32, height uint32) {
    start, sc := FindStartCode(sps, 0)
    h265sps := H265RawSPS{}
    h265sps.Decode(sps[start+int(sc):])
    width = uint32(h265sps.Pic_width_in_luma_samples)
    height = uint32(h265sps.Pic_height_in_luma_samples)
    return
}

func GetVPSIdWithStartCode(vps []byte) uint8 {
    start, sc := FindStartCode(vps, 0)
    return GetVPSId(vps[start+int(sc):])
}

func GetVPSId(vps []byte) uint8 {
    var rawvps VPS
    rawvps.Decode(vps)
    return rawvps.Vps_video_parameter_set_id
}

func GetH265SPSIdWithStartCode(sps []byte) uint64 {
    start, sc := FindStartCode(sps, 0)
    return GetH265SPSId(sps[start+int(sc):])
}

func GetH265SPSId(sps []byte) uint64 {
    var rawsps H265RawSPS
    rawsps.Decode(sps)
    return rawsps.Sps_seq_parameter_set_id
}

func GetH65PPSIdWithStartCode(pps []byte) uint64 {
    start, sc := FindStartCode(pps, 0)
    return GetH265SPSId(pps[start+int(sc):])
}

func GetH265PPSId(pps []byte) uint64 {
    var rawpps H265RawPPS
    rawpps.Decode(pps)
    return rawpps.Pps_pic_parameter_set_id
}

/*
ISO/IEC 14496-15:2017(E) 8.3.3.1.2 Syntax (p71)

aligned(8) class HEVCDecoderConfigurationRecord {
    unsigned int(8) configurationVersion = 1;
    unsigned int(2) general_profile_space;
    unsigned int(1) general_tier_flag;
    unsigned int(5) general_profile_idc;
    unsigned int(32) general_profile_compatibility_flags;
    unsigned int(48) general_constraint_indicator_flags;
    unsigned int(8) general_level_idc;
    bit(4) reserved = '1111'b;
    unsigned int(12) min_spatial_segmentation_idc;
    bit(6) reserved = '111111'b;
    unsigned int(2) parallelismType;
    bit(6) reserved = '111111'b;
    unsigned int(2) chromaFormat;
    bit(5) reserved = '11111'b;
    unsigned int(3) bitDepthLumaMinus8;
    bit(5) reserved = '11111'b;
    unsigned int(3) bitDepthChromaMinus8;
    bit(16) avgFrameRate;
    bit(2) constantFrameRate;
    bit(3) numTemporalLayers;
    bit(1) temporalIdNested;
    unsigned int(2) lengthSizeMinusOne;
    unsigned int(8) numOfArrays;
    for (j=0; j < numOfArrays; j++) {
        bit(1) array_completeness;
        unsigned int(1) reserved = 0;
        unsigned int(6) NAL_unit_type;
        unsigned int(16) numNalus;
        for (i=0; i< numNalus; i++) {
            unsigned int(16) nalUnitLength;
            bit(8*nalUnitLength) nalUnit;
        }
    }
}
*/

type NalUnit struct {
    NalUnitLength uint16
    Nalu          []byte
}

type HVCCNALUnitArray struct {
    Array_completeness uint8
    NAL_unit_type      uint8
    NumNalus           uint16
    NalUnits           []*NalUnit
}

type HEVCRecordConfiguration struct {
    ConfigurationVersion                uint8
    General_profile_space               uint8
    General_tier_flag                   uint8
    General_profile_idc                 uint8
    General_profile_compatibility_flags uint32
    General_constraint_indicator_flags  uint64
    General_level_idc                   uint8
    Min_spatial_segmentation_idc        uint16
    ParallelismType                     uint8
    ChromaFormat                        uint8
    BitDepthLumaMinus8                  uint8
    BitDepthChromaMinus8                uint8
    AvgFrameRate                        uint16
    ConstantFrameRate                   uint8
    NumTemporalLayers                   uint8
    TemporalIdNested                    uint8
    LengthSizeMinusOne                  uint8
    NumOfArrays                         uint8
    Arrays                              []*HVCCNALUnitArray
}

func NewHEVCRecordConfiguration() *HEVCRecordConfiguration {
    return &HEVCRecordConfiguration{
        ConfigurationVersion:                1,
        General_profile_compatibility_flags: 0xffffffff,
        General_constraint_indicator_flags:  0xffffffffffffffff,
        Min_spatial_segmentation_idc:        4097,
        LengthSizeMinusOne:                  3,
    }
}

func (hvcc *HEVCRecordConfiguration) Encode() []byte {
    bsw := NewBitStreamWriter(512)
    bsw.PutByte(hvcc.ConfigurationVersion)
    bsw.PutUint8(hvcc.General_profile_space, 2)
    bsw.PutUint8(hvcc.General_tier_flag, 1)
    bsw.PutUint8(hvcc.General_profile_idc, 5)
    bsw.PutUint32(hvcc.General_profile_compatibility_flags, 32)
    bsw.PutUint64(hvcc.General_constraint_indicator_flags, 48)
    bsw.PutByte(hvcc.General_level_idc)
    bsw.PutUint8(0x0F, 4)
    bsw.PutUint16(hvcc.Min_spatial_segmentation_idc, 12)
    bsw.PutUint8(0x3F, 6)
    //ffmpeg hvcc_write(AVIOContext *pb, HEVCDecoderConfigurationRecord *hvcc)
    /*
     * parallelismType indicates the type of parallelism that is used to meet
     * the restrictions imposed by min_spatial_segmentation_idc when the value
     * of min_spatial_segmentation_idc is greater than 0.
     */
    if hvcc.Min_spatial_segmentation_idc == 0 {
        hvcc.ParallelismType = 0
    }
    bsw.PutUint8(hvcc.ParallelismType, 2)
    bsw.PutUint8(0x3F, 6)
    bsw.PutUint8(hvcc.ChromaFormat, 2)
    bsw.PutUint8(0x1F, 5)
    bsw.PutUint8(hvcc.BitDepthLumaMinus8, 3)
    bsw.PutUint8(0x1F, 5)
    bsw.PutUint8(hvcc.BitDepthChromaMinus8, 3)
    bsw.PutUint16(hvcc.AvgFrameRate, 16)
    bsw.PutUint8(hvcc.ConstantFrameRate, 2)
    bsw.PutUint8(hvcc.NumTemporalLayers, 3)
    bsw.PutUint8(hvcc.TemporalIdNested, 1)
    bsw.PutUint8(hvcc.LengthSizeMinusOne, 2)
    bsw.PutByte(uint8(len(hvcc.Arrays)))
    for _, arrays := range hvcc.Arrays {
        bsw.PutUint8(arrays.Array_completeness, 1)
        bsw.PutUint8(0, 1)
        bsw.PutUint8(arrays.NAL_unit_type, 6)
        bsw.PutUint16(arrays.NumNalus, 16)
        for _, nalu := range arrays.NalUnits {
            bsw.PutUint16(nalu.NalUnitLength, 16)
            bsw.PutBytes(nalu.Nalu)
        }
    }
    return bsw.Bits()
}

func (hvcc *HEVCRecordConfiguration) Decode(hevc []byte) {
    bs := NewBitStream(hevc)
    hvcc.ConfigurationVersion = bs.Uint8(8)
    hvcc.General_profile_space = bs.Uint8(2)
    hvcc.General_tier_flag = bs.Uint8(1)
    hvcc.General_profile_idc = bs.Uint8(5)
    hvcc.General_profile_compatibility_flags = bs.Uint32(32)
    hvcc.General_constraint_indicator_flags = bs.GetBits(48)
    hvcc.General_level_idc = bs.Uint8(8)
    bs.SkipBits(4)
    hvcc.Min_spatial_segmentation_idc = bs.Uint16(12)
    bs.SkipBits(6)
    hvcc.ParallelismType = bs.Uint8(2)
    bs.SkipBits(6)
    hvcc.ChromaFormat = bs.Uint8(2)
    bs.SkipBits(5)
    hvcc.BitDepthLumaMinus8 = bs.Uint8(3)
    bs.SkipBits(5)
    hvcc.BitDepthChromaMinus8 = bs.Uint8(3)
    hvcc.AvgFrameRate = bs.Uint16(16)
    hvcc.ConstantFrameRate = bs.Uint8(2)
    hvcc.NumTemporalLayers = bs.Uint8(3)
    hvcc.TemporalIdNested = bs.Uint8(1)
    hvcc.LengthSizeMinusOne = bs.Uint8(2)
    hvcc.NumOfArrays = bs.Uint8(8)
    hvcc.Arrays = make([]*HVCCNALUnitArray, hvcc.NumOfArrays)
    for i := 0; i < int(hvcc.NumOfArrays); i++ {
        hvcc.Arrays[i] = new(HVCCNALUnitArray)
        hvcc.Arrays[i].Array_completeness = bs.GetBit()
        bs.SkipBits(1)
        hvcc.Arrays[i].NAL_unit_type = bs.Uint8(6)
        hvcc.Arrays[i].NumNalus = bs.Uint16(16)
        hvcc.Arrays[i].NalUnits = make([]*NalUnit, hvcc.Arrays[i].NumNalus)
        for j := 0; j < int(hvcc.Arrays[i].NumNalus); j++ {
            hvcc.Arrays[i].NalUnits[j] = new(NalUnit)
            hvcc.Arrays[i].NalUnits[j].NalUnitLength = bs.Uint16(16)
            hvcc.Arrays[i].NalUnits[j].Nalu = bs.GetBytes(int(hvcc.Arrays[i].NalUnits[j].NalUnitLength))
        }
    }
}

func (hvcc *HEVCRecordConfiguration) UpdateSPS(sps []byte) {
    start, sc := FindStartCode(sps, 0)
    sps = sps[start+int(sc):]
    var rawsps H265RawSPS
    rawsps.Decode(sps)
    spsid := rawsps.Sps_seq_parameter_set_id
    var needUpdate bool = false
    i := 0
    for ; i < len(hvcc.Arrays); i++ {
        arrays := hvcc.Arrays[i]
        found := false
        if arrays.NAL_unit_type == uint8(H265_NAL_SPS) {
            j := 0
            for ; j < len(arrays.NalUnits); j++ {
                if spsid != GetH265SPSId(arrays.NalUnits[j].Nalu) {
                    found = true
                    continue
                }
                //find the same sps nalu
                if arrays.NalUnits[j].NalUnitLength == uint16(len(sps)) && bytes.Equal(arrays.NalUnits[j].Nalu, sps) {
                    return
                }
                tmpsps := make([]byte, len(sps))
                copy(tmpsps, sps)
                arrays.NalUnits[j].Nalu = tmpsps
                arrays.NalUnits[j].NalUnitLength = uint16(len(tmpsps))
                needUpdate = true
                break
            }
            if j == len(arrays.NalUnits) {
                nalu := &NalUnit{
                    Nalu:          make([]byte, len(sps)),
                    NalUnitLength: uint16(len(sps)),
                }
                copy(nalu.Nalu, sps)
                arrays.NalUnits = append(arrays.NalUnits, nalu)
                needUpdate = true
            }
        }
        if found {
            break
        }
    }
    if i == len(hvcc.Arrays) {
        nua := &HVCCNALUnitArray{
            Array_completeness: 1,
            NAL_unit_type:      33,
            NumNalus:           1,
            NalUnits:           make([]*NalUnit, 1),
        }
        nu := &NalUnit{
            NalUnitLength: uint16(len(sps)),
            Nalu:          make([]byte, len(sps)),
        }
        copy(nu.Nalu, sps)
        nua.NalUnits[0] = nu
        hvcc.Arrays = append(hvcc.Arrays, nua)
        needUpdate = true
    }
    if needUpdate {
        hvcc.NumTemporalLayers = uint8(Max(int(hvcc.NumTemporalLayers), int(rawsps.Sps_max_sub_layers_minus1+1)))
        hvcc.TemporalIdNested = rawsps.Sps_temporal_id_nesting_flag
        hvcc.ChromaFormat = uint8(rawsps.Chroma_format_idc)
        hvcc.BitDepthChromaMinus8 = uint8(rawsps.Bit_depth_chroma_minus8)
        hvcc.BitDepthLumaMinus8 = uint8(rawsps.Bit_depth_luma_minus8)
        hvcc.updatePtl(rawsps.Ptl)
        hvcc.updateVui(rawsps.Vui)
    }
}

func (hvcc *HEVCRecordConfiguration) UpdatePPS(pps []byte) {
    start, sc := FindStartCode(pps, 0)
    pps = pps[start+int(sc):]
    var rawpps H265RawPPS
    rawpps.Decode(pps)
    ppsid := rawpps.Pps_pic_parameter_set_id
    var needUpdate bool = false
    i := 0
    for ; i < len(hvcc.Arrays); i++ {
        arrays := hvcc.Arrays[i]
        found := false
        if arrays.NAL_unit_type == uint8(H265_NAL_PPS) {
            j := 0
            for ; j < len(arrays.NalUnits); j++ {
                if ppsid != GetH265PPSId(arrays.NalUnits[j].Nalu) {
                    found = true
                    continue
                }
                //find the same sps nalu
                if arrays.NalUnits[j].NalUnitLength == uint16(len(pps)) && bytes.Equal(arrays.NalUnits[j].Nalu, pps) {
                    return
                }
                tmppps := make([]byte, len(pps))
                copy(tmppps, pps)
                arrays.NalUnits[j].Nalu = tmppps
                arrays.NalUnits[j].NalUnitLength = uint16(len(tmppps))
                needUpdate = true
                break
            }
            if j == len(arrays.NalUnits) {
                nalu := &NalUnit{
                    Nalu:          make([]byte, len(pps)),
                    NalUnitLength: uint16(len(pps)),
                }
                copy(nalu.Nalu, pps)
                arrays.NalUnits = append(arrays.NalUnits, nalu)
                needUpdate = true
            }
        }
        if found {
            break
        }
    }
    if i == len(hvcc.Arrays) {
        nua := &HVCCNALUnitArray{
            Array_completeness: 1,
            NAL_unit_type:      34,
            NumNalus:           1,
            NalUnits:           make([]*NalUnit, 1),
        }
        nu := &NalUnit{
            NalUnitLength: uint16(len(pps)),
            Nalu:          make([]byte, len(pps)),
        }
        copy(nu.Nalu, pps)
        nua.NalUnits[0] = nu
        hvcc.Arrays = append(hvcc.Arrays, nua)
        needUpdate = true
    }
    if needUpdate {
        if rawpps.Entropy_coding_sync_enabled_flag == 1 && rawpps.Tiles_enabled_flag == 1 {
            hvcc.ParallelismType = 0
        } else if rawpps.Entropy_coding_sync_enabled_flag == 1 {
            hvcc.ParallelismType = 3
        } else if rawpps.Tiles_enabled_flag == 1 {
            hvcc.ParallelismType = 2
        } else {
            hvcc.ParallelismType = 1
        }
    }
}

func (hvcc *HEVCRecordConfiguration) UpdateVPS(vps []byte) {
    start, sc := FindStartCode(vps, 0)
    vps = vps[start+int(sc):]
    var rawvps VPS
    rawvps.Decode(vps)
    vpsid := rawvps.Vps_video_parameter_set_id
    var needUpdate bool = false
    i := 0
    for ; i < len(hvcc.Arrays); i++ {
        arrays := hvcc.Arrays[i]
        found := false
        if arrays.NAL_unit_type == uint8(H265_NAL_VPS) {
            found = true
            j := 0
            for ; j < len(arrays.NalUnits); j++ {
                if vpsid != GetVPSId(arrays.NalUnits[j].Nalu) {
                    found = true
                    continue
                }
                //find the same sps nalu
                if arrays.NalUnits[j].NalUnitLength == uint16(len(vps)) && bytes.Equal(arrays.NalUnits[j].Nalu, vps) {
                    return
                }
                tmpvps := make([]byte, len(vps))
                copy(tmpvps, vps)
                arrays.NalUnits[j].Nalu = tmpvps
                arrays.NalUnits[j].NalUnitLength = uint16(len(tmpvps))
                needUpdate = true
                break
            }
            if j == len(arrays.NalUnits) {
                nalu := &NalUnit{
                    Nalu:          make([]byte, len(vps)),
                    NalUnitLength: uint16(len(vps)),
                }
                copy(nalu.Nalu, vps)
                arrays.NalUnits = append(arrays.NalUnits, nalu)
                needUpdate = true
            }
        }
        if found {
            break
        }
    }
    if i == len(hvcc.Arrays) {
        nua := &HVCCNALUnitArray{
            Array_completeness: 1,
            NAL_unit_type:      32,
            NumNalus:           1,
            NalUnits:           make([]*NalUnit, 1),
        }
        nu := &NalUnit{
            NalUnitLength: uint16(len(vps)),
            Nalu:          make([]byte, len(vps)),
        }
        copy(nu.Nalu, vps)
        nua.NalUnits[0] = nu
        hvcc.Arrays = append(hvcc.Arrays, nua)
        needUpdate = true
    }
    if needUpdate {
        hvcc.NumTemporalLayers = uint8(Max(int(hvcc.NumTemporalLayers), int(rawvps.Vps_max_layers_minus1+1)))
        hvcc.updatePtl(rawvps.Ptl)
    }
}

func (hvcc *HEVCRecordConfiguration) ToNalus() (nalus []byte) {
    startcode := []byte{0x00, 0x00, 0x00, 0x01}
    for _, arrays := range hvcc.Arrays {
        for _, unit := range arrays.NalUnits {
            nalus = append(nalus, startcode...)
            nalus = append(nalus, unit.Nalu[:unit.NalUnitLength]...)
        }
    }
    return
}

func (hvcc *HEVCRecordConfiguration) updatePtl(ptl ProfileTierLevel) {
    hvcc.General_profile_space = ptl.General_profile_space
    if hvcc.General_tier_flag < ptl.General_tier_flag {
        hvcc.General_level_idc = ptl.General_level_idc
    } else {
        hvcc.General_level_idc = uint8(Max(int(hvcc.General_level_idc), int(ptl.General_level_idc)))
    }
    hvcc.General_tier_flag = uint8(Max(int(hvcc.General_tier_flag), int(ptl.General_tier_flag)))
    hvcc.General_profile_idc = uint8(Max(int(hvcc.General_profile_idc), int(ptl.General_profile_idc)))
    hvcc.General_profile_compatibility_flags &= ptl.General_profile_compatibility_flag
    hvcc.General_constraint_indicator_flags &= ptl.General_constraint_indicator_flag
}

func (hvcc *HEVCRecordConfiguration) updateVui(vui VUI_Parameters) {
    hvcc.Min_spatial_segmentation_idc = uint16(Min(int(hvcc.Min_spatial_segmentation_idc), int(vui.Min_spatial_segmentation_idc)))
}
