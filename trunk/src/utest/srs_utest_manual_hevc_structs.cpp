#include <srs_utest.hpp>

#include <srs_kernel_codec.hpp>

VOID TEST(HevcStructsTest, SrsHevcProfileTierLevel_FieldNaming)
{
    // Test that all fields follow SRS naming convention (ending with underscore)
    SrsHevcProfileTierLevel ptl;

    // Test basic field access - these should compile without errors
    ptl.general_profile_space_ = 1;
    ptl.general_tier_flag_ = 0;
    ptl.general_profile_idc_ = 2;
    ptl.general_profile_compatibility_flag_[0] = 1;
    ptl.general_progressive_source_flag_ = 1;
    ptl.general_interlaced_source_flag_ = 0;
    ptl.general_non_packed_constraint_flag_ = 1;
    ptl.general_frame_only_constraint_flag_ = 0;
    ptl.general_max_12bit_constraint_flag_ = 1;
    ptl.general_max_10bit_constraint_flag_ = 0;
    ptl.general_max_8bit_constraint_flag_ = 1;
    ptl.general_max_422chroma_constraint_flag_ = 0;
    ptl.general_max_420chroma_constraint_flag_ = 1;
    ptl.general_max_monochrome_constraint_flag_ = 0;
    ptl.general_intra_constraint_flag_ = 1;
    ptl.general_one_picture_only_constraint_flag_ = 0;
    ptl.general_lower_bit_rate_constraint_flag_ = 1;
    ptl.general_max_14bit_constraint_flag_ = 0;
    ptl.general_reserved_zero_7bits_ = 0;
    ptl.general_reserved_zero_33bits_ = 0;
    ptl.general_reserved_zero_34bits_ = 0;
    ptl.general_reserved_zero_35bits_ = 0;
    ptl.general_reserved_zero_43bits_ = 0;
    ptl.general_inbld_flag_ = 1;
    ptl.general_reserved_zero_bit_ = 0;
    ptl.general_level_idc_ = 93;
    ptl.reserved_zero_2bits_[0] = 0;

    // Test vector fields
    ptl.sub_layer_profile_present_flag_.resize(1);
    ptl.sub_layer_level_present_flag_.resize(1);
    ptl.sub_layer_profile_space_.resize(1);
    ptl.sub_layer_tier_flag_.resize(1);
    ptl.sub_layer_profile_idc_.resize(1);
    ptl.sub_layer_profile_compatibility_flag_.resize(1);
    ptl.sub_layer_progressive_source_flag_.resize(1);
    ptl.sub_layer_interlaced_source_flag_.resize(1);
    ptl.sub_layer_non_packed_constraint_flag_.resize(1);
    ptl.sub_layer_frame_only_constraint_flag_.resize(1);
    ptl.sub_layer_max_12bit_constraint_flag_.resize(1);
    ptl.sub_layer_max_10bit_constraint_flag_.resize(1);
    ptl.sub_layer_max_8bit_constraint_flag_.resize(1);
    ptl.sub_layer_max_422chroma_constraint_flag_.resize(1);
    ptl.sub_layer_max_420chroma_constraint_flag_.resize(1);
    ptl.sub_layer_max_monochrome_constraint_flag_.resize(1);
    ptl.sub_layer_intra_constraint_flag_.resize(1);
    ptl.sub_layer_one_picture_only_constraint_flag_.resize(1);
    ptl.sub_layer_lower_bit_rate_constraint_flag_.resize(1);
    ptl.sub_layer_reserved_zero_7bits_.resize(1);
    ptl.sub_layer_reserved_zero_33bits_.resize(1);
    ptl.sub_layer_reserved_zero_34bits_.resize(1);
    ptl.sub_layer_reserved_zero_35bits_.resize(1);
    ptl.sub_layer_reserved_zero_43bits_.resize(1);
    ptl.sub_layer_inbld_flag_.resize(1);
    ptl.sub_layer_reserved_zero_bit_.resize(1);
    ptl.sub_layer_level_idc_.resize(1);

    // Verify values were set correctly
    EXPECT_EQ(1, ptl.general_profile_space_);
    EXPECT_EQ(0, ptl.general_tier_flag_);
    EXPECT_EQ(2, ptl.general_profile_idc_);
    EXPECT_EQ(1, ptl.general_profile_compatibility_flag_[0]);
    EXPECT_EQ(93, ptl.general_level_idc_);
    EXPECT_EQ(1, ptl.sub_layer_profile_present_flag_.size());
}

VOID TEST(HevcStructsTest, SrsHevcSubLayerHrdParameters_FieldNaming)
{
    // Test that SrsHevcSubLayerHrdParameters already has correct naming
    SrsHevcSubLayerHrdParameters params;

    // Test field access - these should compile without errors
    params.bit_rate_value_minus1_.resize(1);
    params.cpb_size_value_minus1_.resize(1);
    params.cpb_size_du_value_minus1_.resize(1);
    params.bit_rate_du_value_minus1_.resize(1);
    params.cbr_flag_.resize(1);

    params.bit_rate_value_minus1_[0] = 1000;
    params.cpb_size_value_minus1_[0] = 2000;
    params.cpb_size_du_value_minus1_[0] = 3000;
    params.bit_rate_du_value_minus1_[0] = 4000;
    params.cbr_flag_[0] = 1;

    // Verify values were set correctly
    EXPECT_EQ(1000, params.bit_rate_value_minus1_[0]);
    EXPECT_EQ(2000, params.cpb_size_value_minus1_[0]);
    EXPECT_EQ(3000, params.cpb_size_du_value_minus1_[0]);
    EXPECT_EQ(4000, params.bit_rate_du_value_minus1_[0]);
    EXPECT_EQ(1, params.cbr_flag_[0]);
}
