//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#ifndef SRS_KERNEL_CODEC_HPP
#define SRS_KERNEL_CODEC_HPP

#include <srs_core.hpp>

#include <string>
#include <vector>

class SrsBuffer;
class SrsBitBuffer;

/**
 * The video codec id.
 * @doc video_file_format_spec_v10_1.pdf, page78, E.4.3.1 VIDEODATA
 * CodecID UB [4]
 * Codec Identifier. The following values are defined for FLV:
 *      2 = Sorenson H.263
 *      3 = Screen video
 *      4 = On2 VP6
 *      5 = On2 VP6 with alpha channel
 *      6 = Screen video version 2
 *      7 = AVC
 *     12 = HEVC
 */
enum SrsVideoCodecId
{
    // set to the zero to reserved, for array map.
    SrsVideoCodecIdReserved = 0,
    SrsVideoCodecIdForbidden = 0,
    SrsVideoCodecIdReserved1 = 1,
    SrsVideoCodecIdReserved2 = 9,
    
    // for user to disable video, for example, use pure audio hls.
    SrsVideoCodecIdDisabled = 8,
    
    SrsVideoCodecIdSorensonH263 = 2,
    SrsVideoCodecIdScreenVideo = 3,
    SrsVideoCodecIdOn2VP6 = 4,
    SrsVideoCodecIdOn2VP6WithAlphaChannel = 5,
    SrsVideoCodecIdScreenVideoVersion2 = 6,
    SrsVideoCodecIdAVC = 7,
    // See page 79 at @doc https://github.com/CDN-Union/H265/blob/master/Document/video_file_format_spec_v10_1_ksyun_20170615.doc
    SrsVideoCodecIdHEVC = 12,
    // https://mp.weixin.qq.com/s/H3qI7zsON5sdf4oDJ9qlkg
    SrsVideoCodecIdAV1 = 13,
};
std::string srs_video_codec_id2str(SrsVideoCodecId codec);

/**
 * The video AVC frame trait(characteristic).
 * @doc video_file_format_spec_v10_1.pdf, page79, E.4.3.2 AVCVIDEOPACKET
 * AVCPacketType IF CodecID == 7 UI8
 * The following values are defined:
 *      0 = AVC sequence header
 *      1 = AVC NALU
 *      2 = AVC end of sequence (lower level NALU sequence ender is not required or supported)
 */
enum SrsVideoAvcFrameTrait
{
    // set to the max value to reserved, for array map.
    SrsVideoAvcFrameTraitReserved = 6,
    SrsVideoAvcFrameTraitForbidden = 6,
    
    SrsVideoAvcFrameTraitSequenceHeader = 0,
    SrsVideoAvcFrameTraitNALU = 1,
    SrsVideoAvcFrameTraitSequenceHeaderEOF = 2,

    SrsVideoHEVCFrameTraitPacketTypeSequenceStart = 0,
    SrsVideoHEVCFrameTraitPacketTypeCodedFrames = 1,
    SrsVideoHEVCFrameTraitPacketTypeSequenceEnd = 2,
    // CompositionTime Offset is implied to equal zero. This is
    // an optimization to save putting SI24 composition time value of zero on
    // the wire. See pseudo code below in the VideoTagBody section
    SrsVideoHEVCFrameTraitPacketTypeCodedFramesX = 3,
    // VideoTagBody does not contain video data. VideoTagBody
    // instead contains an AMF encoded metadata. See Metadata Frame
    // section for an illustration of its usage. As an example, the metadata
    // can be HDR information. This is a good way to signal HDR
    // information. This also opens up future ways to express additional
    // metadata that is meant for the next video sequence.
    //
    // note: presence of PacketTypeMetadata means that FrameType
    // flags at the top of this table should be ignored
    SrsVideoHEVCFrameTraitPacketTypeMetadata = 4,
    // Carriage of bitstream in MPEG-2 TS format
    SrsVideoHEVCFrameTraitPacketTypeMPEG2TSSequenceStart = 5,
};

/**
 * The video AVC frame type, such as I/P/B.
 * @doc video_file_format_spec_v10_1.pdf, page78, E.4.3.1 VIDEODATA
 * Frame Type UB [4]
 * Type of video frame. The following values are defined:
 *      1 = key frame (for AVC, a seekable frame)
 *      2 = inter frame (for AVC, a non-seekable frame)
 *      3 = disposable inter frame (H.263 only)
 *      4 = generated key frame (reserved for server use only)
 *      5 = video info/command frame
 */
enum SrsVideoAvcFrameType
{
    // set to the zero to reserved, for array map.
    SrsVideoAvcFrameTypeReserved = 0,
    SrsVideoAvcFrameTypeForbidden = 0,
    SrsVideoAvcFrameTypeReserved1 = 6,
    
    SrsVideoAvcFrameTypeKeyFrame = 1,
    SrsVideoAvcFrameTypeInterFrame = 2,
    SrsVideoAvcFrameTypeDisposableInterFrame = 3,
    SrsVideoAvcFrameTypeGeneratedKeyFrame = 4,
    SrsVideoAvcFrameTypeVideoInfoFrame = 5,
};

/**
 * The audio codec id.
 * @doc video_file_format_spec_v10_1.pdf, page 76, E.4.2 Audio Tags
 * SoundFormat UB [4]
 * Format of SoundData. The following values are defined:
 *     0 = Linear PCM, platform endian
 *     1 = ADPCM
 *     2 = MP3
 *     3 = Linear PCM, little endian
 *     4 = Nellymoser 16 kHz mono
 *     5 = Nellymoser 8 kHz mono
 *     6 = Nellymoser
 *     7 = G.711 A-law logarithmic PCM
 *     8 = G.711 mu-law logarithmic PCM
 *     9 = reserved
 *     10 = AAC
 *     11 = Speex
 *     14 = MP3 8 kHz
 *     15 = Device-specific sound
 * Formats 7, 8, 14, and 15 are reserved.
 * AAC is supported in Flash Player 9,0,115,0 and higher.
 * Speex is supported in Flash Player 10 and higher.
 */
enum SrsAudioCodecId
{
    // set to the max value to reserved, for array map.
    SrsAudioCodecIdReserved1 = 16,
    SrsAudioCodecIdForbidden = 16,
    
    // for user to disable audio, for example, use pure video hls.
    SrsAudioCodecIdDisabled = 17,
    
    SrsAudioCodecIdLinearPCMPlatformEndian = 0,
    SrsAudioCodecIdADPCM = 1,
    SrsAudioCodecIdMP3 = 2,
    SrsAudioCodecIdLinearPCMLittleEndian = 3,
    SrsAudioCodecIdNellymoser16kHzMono = 4,
    SrsAudioCodecIdNellymoser8kHzMono = 5,
    SrsAudioCodecIdNellymoser = 6,
    SrsAudioCodecIdReservedG711AlawLogarithmicPCM = 7,
    SrsAudioCodecIdReservedG711MuLawLogarithmicPCM = 8,
    SrsAudioCodecIdReserved = 9,
    SrsAudioCodecIdAAC = 10,
    SrsAudioCodecIdSpeex = 11,
    // For FLV, it's undefined, we define it as Opus for WebRTC.
    SrsAudioCodecIdOpus = 13,
    SrsAudioCodecIdReservedMP3_8kHz = 14,
    SrsAudioCodecIdReservedDeviceSpecificSound = 15,
};
std::string srs_audio_codec_id2str(SrsAudioCodecId codec);

/**
 * The audio AAC frame trait(characteristic).
 * @doc video_file_format_spec_v10_1.pdf, page 77, E.4.2 Audio Tags
 * AACPacketType IF SoundFormat == 10 or 13 UI8
 * The following values are defined:
 *      0 = AAC sequence header
 *      1 = AAC raw
 */
enum SrsAudioAacFrameTrait
{
    // set to the max value to reserved, for array map.
    SrsAudioAacFrameTraitReserved = 0xff,
    SrsAudioAacFrameTraitForbidden = 0xff,

    // For AAC, we detect the sequence header by content.
    SrsAudioAacFrameTraitSequenceHeader = 0,
    SrsAudioAacFrameTraitRawData = 1,
    
    // For Opus, the frame trait, may has more than one traits.
    SrsAudioOpusFrameTraitRaw = 2,
    SrsAudioOpusFrameTraitSamplingRate = 4,
    SrsAudioOpusFrameTraitAudioLevel = 8,

    // 16/32 reserved for g711a/g711u 

    // For MP3 we assume the first packet is sequence header, while it actually is not the same thing, because we do
    // this to simplify the workflow, to make sure we can detect the audio codec from the sequence headers.
    SrsAudioMp3FrameTraitSequenceHeader = 63,
    SrsAudioMp3FrameTraitRawData = 64,
};

/**
 * The audio sample rate.
 * @see srs_flv_srates and srs_aac_srates.
 * @doc video_file_format_spec_v10_1.pdf, page 76, E.4.2 Audio Tags
 *      0 = 5.5 kHz = 5512 Hz
 *      1 = 11 kHz = 11025 Hz
 *      2 = 22 kHz = 22050 Hz
 *      3 = 44 kHz = 44100 Hz
 * However, we can extends this table.
 * @remark Use srs_flv_srates to convert it.
 */
enum SrsAudioSampleRate
{
    // set to the max value to reserved, for array map.
    SrsAudioSampleRateReserved = 0xff,
    SrsAudioSampleRateForbidden = 0xff,
    
    // For FLV, only support 5, 11, 22, 44KHz sampling rate.
    SrsAudioSampleRate5512 = 0,
    SrsAudioSampleRate11025 = 1,
    SrsAudioSampleRate22050 = 2,
    SrsAudioSampleRate44100 = 3,

    // For MP4, extra sampling rate to FLV.
    SrsAudioSampleRate12000 = 12,
    SrsAudioSampleRate24000 = 24,
    SrsAudioSampleRate48000 = 48,
    
    // For Opus, support 8, 12, 16, 24, 48KHz
    // We will write a UINT8 sampling rate after FLV audio tag header.
    // @doc https://tools.ietf.org/html/rfc6716#section-2
    SrsAudioSampleRateNB8kHz   = 8,  // NB (narrowband)
    SrsAudioSampleRateMB12kHz  = 12, // MB (medium-band)
    SrsAudioSampleRateWB16kHz  = 16, // WB (wideband)
    SrsAudioSampleRateSWB24kHz = 24, // SWB (super-wideband)
    SrsAudioSampleRateFB48kHz  = 48, // FB (fullband)
};
SrsAudioSampleRate srs_audio_sample_rate_from_number(uint32_t v);
SrsAudioSampleRate srs_audio_sample_rate_guess_number(uint32_t v);
uint32_t srs_audio_sample_rate2number(SrsAudioSampleRate v);
std::string srs_audio_sample_rate2str(SrsAudioSampleRate v);

/**
 * The frame type, for example, audio, video or data.
 * @doc video_file_format_spec_v10_1.pdf, page 75, E.4.1 FLV Tag
 */
enum SrsFrameType
{
    // set to the zero to reserved, for array map.
    SrsFrameTypeReserved = 0,
    SrsFrameTypeForbidden = 0,
    
    // 8 = audio
    SrsFrameTypeAudio = 8,
    // 9 = video
    SrsFrameTypeVideo = 9,
    // 18 = script data
    SrsFrameTypeScript = 18,
};

/**
 * Fast tough the codec of FLV video.
 * @doc video_file_format_spec_v10_1.pdf, page 78, E.4.3 Video Tags
 */
class SrsFlvVideo
{
public:
    SrsFlvVideo();
    virtual ~SrsFlvVideo();
    // the following function used to finger out the flv/rtmp packet detail.
public:
    /**
     * only check the frame_type, not check the codec type.
     */
    static bool keyframe(char* data, int size);
    /**
     * check codec h264, keyframe, sequence header
     */
    // TODO: FIXME: Remove it, use SrsFormat instead.
    static bool sh(char* data, int size);
    /**
     * check codec h264.
     */
    static bool h264(char* data, int size);
#ifdef SRS_H265
    // Check whether codec is HEVC(H.265).
    static bool hevc(char* data, int size);
#endif
    /**
     * check the video RTMP/flv header info,
     * @return true if video RTMP/flv header is ok.
     * @remark all type of audio is possible, no need to check audio.
     */
    static bool acceptable(char* data, int size);
};

/**
 * Fast tough the codec of FLV video.
 * @doc video_file_format_spec_v10_1.pdf, page 76, E.4.2 Audio Tags
 */
class SrsFlvAudio
{
public:
    SrsFlvAudio();
    virtual ~SrsFlvAudio();
    // the following function used to finger out the flv/rtmp packet detail.
public:
    /**
     * check codec aac, sequence header
     */
    static bool sh(char* data, int size);
    /**
     * check codec aac.
     */
    static bool aac(char* data, int size);
};

/**
 * the public data, event HLS disable, others can use it.
 */
/**
 * the flv sample rate map
 */
extern int srs_flv_srates[];

/**
 * the aac sample rate map
 */
extern int srs_aac_srates[];

// The number of aac samplerates, size for srs_aac_srates.
#define SrsAAcSampleRateNumbers 16

// The impossible aac sample rate index.
#define SrsAacSampleRateUnset 15

// The max number of NALUs in a video, or aac frame in audio packet.
#define SrsMaxNbSamples 256

/**
 * The audio sample size in bits.
 * @doc video_file_format_spec_v10_1.pdf, page 76, E.4.2 Audio Tags
 * Size of each audio sample. This parameter only pertains to
 * uncompressed formats. Compressed formats always decode
 * to 16 bits internally.
 *      0 = 8-bit samples
 *      1 = 16-bit samples
 */
enum SrsAudioSampleBits
{
    // set to the max value to reserved, for array map.
    SrsAudioSampleBitsReserved = 2,
    SrsAudioSampleBitsForbidden = 2,
    
    SrsAudioSampleBits8bit = 0,
    SrsAudioSampleBits16bit = 1,
};
std::string srs_audio_sample_bits2str(SrsAudioSampleBits v);

/**
 * The audio channels.
 * @doc video_file_format_spec_v10_1.pdf, page 77, E.4.2 Audio Tags
 * Mono or stereo sound
 *      0 = Mono sound
 *      1 = Stereo sound
 */
enum SrsAudioChannels
{
    // set to the max value to reserved, for array map.
    SrsAudioChannelsReserved = 2,
    SrsAudioChannelsForbidden = 2,
    
    SrsAudioChannelsMono = 0,
    SrsAudioChannelsStereo = 1,
};
std::string srs_audio_channels2str(SrsAudioChannels v);

/**
 * Table 7-1 - NAL unit type codes, syntax element categories, and NAL unit type classes
 * ISO_IEC_14496-10-AVC-2012.pdf, page 83.
 */
enum SrsAvcNaluType
{
    // Unspecified
    SrsAvcNaluTypeReserved = 0,
    SrsAvcNaluTypeForbidden = 0,
    
    // Coded slice of a non-IDR picture slice_layer_without_partitioning_rbsp( )
    SrsAvcNaluTypeNonIDR = 1,
    // Coded slice data partition A slice_data_partition_a_layer_rbsp( )
    SrsAvcNaluTypeDataPartitionA = 2,
    // Coded slice data partition B slice_data_partition_b_layer_rbsp( )
    SrsAvcNaluTypeDataPartitionB = 3,
    // Coded slice data partition C slice_data_partition_c_layer_rbsp( )
    SrsAvcNaluTypeDataPartitionC = 4,
    // Coded slice of an IDR picture slice_layer_without_partitioning_rbsp( )
    SrsAvcNaluTypeIDR = 5,
    // Supplemental enhancement information (SEI) sei_rbsp( )
    SrsAvcNaluTypeSEI = 6,
    // Sequence parameter set seq_parameter_set_rbsp( )
    SrsAvcNaluTypeSPS = 7,
    // Picture parameter set pic_parameter_set_rbsp( )
    SrsAvcNaluTypePPS = 8,
    // Access unit delimiter access_unit_delimiter_rbsp( )
    SrsAvcNaluTypeAccessUnitDelimiter = 9,
    // End of sequence end_of_seq_rbsp( )
    SrsAvcNaluTypeEOSequence = 10,
    // End of stream end_of_stream_rbsp( )
    SrsAvcNaluTypeEOStream = 11,
    // Filler data filler_data_rbsp( )
    SrsAvcNaluTypeFilterData = 12,
    // Sequence parameter set extension seq_parameter_set_extension_rbsp( )
    SrsAvcNaluTypeSPSExt = 13,
    // Prefix NAL unit prefix_nal_unit_rbsp( )
    SrsAvcNaluTypePrefixNALU = 14,
    // Subset sequence parameter set subset_seq_parameter_set_rbsp( )
    SrsAvcNaluTypeSubsetSPS = 15,
    // Coded slice of an auxiliary coded picture without partitioning slice_layer_without_partitioning_rbsp( )
    SrsAvcNaluTypeLayerWithoutPartition = 19,
    // Coded slice extension slice_layer_extension_rbsp( )
    SrsAvcNaluTypeCodedSliceExt = 20,
};
std::string srs_avc_nalu2str(SrsAvcNaluType nalu_type);

#ifdef SRS_H265
/**
 * The enum NALU type for HEVC
 * @see Table 7-1 – NAL unit type codes and NAL unit type classes
 * @doc ITU-T-H.265-2021.pdf, page 86.
 */
enum SrsHevcNaluType {
    SrsHevcNaluType_CODED_SLICE_TRAIL_N =       0,
    SrsHevcNaluType_CODED_SLICE_TRAIL_R, //1
    SrsHevcNaluType_CODED_SLICE_TSA_N,   //2
    SrsHevcNaluType_CODED_SLICE_TLA,     //3
    SrsHevcNaluType_CODED_SLICE_STSA_N,  //4
    SrsHevcNaluType_CODED_SLICE_STSA_R,  //5
    SrsHevcNaluType_CODED_SLICE_RADL_N,  //6
    SrsHevcNaluType_CODED_SLICE_DLP,     //7
    SrsHevcNaluType_CODED_SLICE_RASL_N,  //8
    SrsHevcNaluType_CODED_SLICE_TFD,     //9
    SrsHevcNaluType_RESERVED_10,
    SrsHevcNaluType_RESERVED_11,
    SrsHevcNaluType_RESERVED_12,
    SrsHevcNaluType_RESERVED_13,
    SrsHevcNaluType_RESERVED_14,
    SrsHevcNaluType_RESERVED_15,
    SrsHevcNaluType_CODED_SLICE_BLA,      //16
    SrsHevcNaluType_CODED_SLICE_BLANT,    //17
    SrsHevcNaluType_CODED_SLICE_BLA_N_LP, //18
    SrsHevcNaluType_CODED_SLICE_IDR,      //19
    SrsHevcNaluType_CODED_SLICE_IDR_N_LP, //20
    SrsHevcNaluType_CODED_SLICE_CRA,      //21
    SrsHevcNaluType_RESERVED_22,
    SrsHevcNaluType_RESERVED_23,
    SrsHevcNaluType_RESERVED_24,
    SrsHevcNaluType_RESERVED_25,
    SrsHevcNaluType_RESERVED_26,
    SrsHevcNaluType_RESERVED_27,
    SrsHevcNaluType_RESERVED_28,
    SrsHevcNaluType_RESERVED_29,
    SrsHevcNaluType_RESERVED_30,
    SrsHevcNaluType_RESERVED_31,
    SrsHevcNaluType_VPS,                   // 32
    SrsHevcNaluType_SPS,                   // 33
    SrsHevcNaluType_PPS,                   // 34
    SrsHevcNaluType_ACCESS_UNIT_DELIMITER, // 35
    SrsHevcNaluType_EOS,                   // 36
    SrsHevcNaluType_EOB,                   // 37
    SrsHevcNaluType_FILLER_DATA,           // 38
    SrsHevcNaluType_SEI ,                  // 39 Prefix SEI
    SrsHevcNaluType_SEI_SUFFIX,            // 40 Suffix SEI
    SrsHevcNaluType_RESERVED_41,
    SrsHevcNaluType_RESERVED_42,
    SrsHevcNaluType_RESERVED_43,
    SrsHevcNaluType_RESERVED_44,
    SrsHevcNaluType_RESERVED_45,
    SrsHevcNaluType_RESERVED_46,
    SrsHevcNaluType_RESERVED_47,
    SrsHevcNaluType_UNSPECIFIED_48,
    SrsHevcNaluType_UNSPECIFIED_49,
    SrsHevcNaluType_UNSPECIFIED_50,
    SrsHevcNaluType_UNSPECIFIED_51,
    SrsHevcNaluType_UNSPECIFIED_52,
    SrsHevcNaluType_UNSPECIFIED_53,
    SrsHevcNaluType_UNSPECIFIED_54,
    SrsHevcNaluType_UNSPECIFIED_55,
    SrsHevcNaluType_UNSPECIFIED_56,
    SrsHevcNaluType_UNSPECIFIED_57,
    SrsHevcNaluType_UNSPECIFIED_58,
    SrsHevcNaluType_UNSPECIFIED_59,
    SrsHevcNaluType_UNSPECIFIED_60,
    SrsHevcNaluType_UNSPECIFIED_61,
    SrsHevcNaluType_UNSPECIFIED_62,
    SrsHevcNaluType_UNSPECIFIED_63,
    SrsHevcNaluType_INVALID,
};
#define SrsHevcNaluTypeParse(code) (SrsHevcNaluType)((code & 0x7E) >> 1)

struct SrsHevcNalData {
    uint16_t nal_unit_length;
    std::vector<uint8_t> nal_unit_data;
};

struct SrsHevcHvccNalu {
    uint8_t array_completeness;
    uint8_t nal_unit_type;
    uint16_t num_nalus;
    std::vector<SrsHevcNalData> nal_data_vec;
};

/**
 * HEVC Common Max define.
 * @doc ITU-T-H.265-2021.pdf
 */
// @see F.7.3.2.1: vps_video_parameter_set_id is u(4).
// @doc ITU-T-H.265-2021.pdf, page 464.
const int SrsHevcMax_VPS_COUNT = 16;
// @see 7.4.3.2.1: sps_seq_parameter_set_id is in [0, 15].
// @doc ITU-T-H.265-2021.pdf, page 95.
const int SrsHevcMax_SPS_COUNT = 16;
// @see 7.4.3.3.1: pps_pic_parameter_set_id is in [0, 63].
// @doc ITU-T-H.265-2021.pdf, page 102.
const int SrsHevcMax_PPS_COUNT = 64;

/**
 * Profile, tier and level
 * @see 7.3.3 Profile, tier and level syntax
 * @doc ITU-T-H.265-2021.pdf, page 62.
 */
struct SrsHevcProfileTierLevel
{
public:
    uint8_t general_profile_space;
    uint8_t general_tier_flag;
    uint8_t general_profile_idc;
    uint8_t general_profile_compatibility_flag[32];
    uint8_t general_progressive_source_flag;
    uint8_t general_interlaced_source_flag;
    uint8_t general_non_packed_constraint_flag;
    uint8_t general_frame_only_constraint_flag;
    uint8_t general_max_12bit_constraint_flag;
    uint8_t general_max_10bit_constraint_flag;
    uint8_t general_max_8bit_constraint_flag;
    uint8_t general_max_422chroma_constraint_flag;
    uint8_t general_max_420chroma_constraint_flag;
    uint8_t general_max_monochrome_constraint_flag;
    uint8_t general_intra_constraint_flag;
    uint8_t general_one_picture_only_constraint_flag;
    uint8_t general_lower_bit_rate_constraint_flag;
    uint32_t general_max_14bit_constraint_flag;
    uint8_t general_reserved_zero_7bits;
    uint64_t general_reserved_zero_33bits;
    uint64_t general_reserved_zero_34bits;
    uint64_t general_reserved_zero_35bits;
    uint64_t general_reserved_zero_43bits;
    uint8_t general_inbld_flag;
    uint8_t general_reserved_zero_bit;
    uint8_t general_level_idc;
    std::vector<uint8_t> sub_layer_profile_present_flag;
    std::vector<uint8_t> sub_layer_level_present_flag;
    uint8_t reserved_zero_2bits[8];
    std::vector<uint8_t> sub_layer_profile_space;
    std::vector<uint8_t> sub_layer_tier_flag;
    std::vector<uint8_t> sub_layer_profile_idc;
    std::vector<std::vector<uint8_t> > sub_layer_profile_compatibility_flag;
    std::vector<uint8_t> sub_layer_progressive_source_flag;
    std::vector<uint8_t> sub_layer_interlaced_source_flag;
    std::vector<uint8_t> sub_layer_non_packed_constraint_flag;
    std::vector<uint8_t> sub_layer_frame_only_constraint_flag;
    std::vector<uint8_t> sub_layer_max_12bit_constraint_flag;
    std::vector<uint8_t> sub_layer_max_10bit_constraint_flag;
    std::vector<uint8_t> sub_layer_max_8bit_constraint_flag;
    std::vector<uint8_t> sub_layer_max_422chroma_constraint_flag;
    std::vector<uint8_t> sub_layer_max_420chroma_constraint_flag;
    std::vector<uint8_t> sub_layer_max_monochrome_constraint_flag;
    std::vector<uint8_t> sub_layer_intra_constraint_flag;
    std::vector<uint8_t> sub_layer_one_picture_only_constraint_flag;
    std::vector<uint8_t> sub_layer_lower_bit_rate_constraint_flag;
    std::vector<uint8_t> sub_layer_reserved_zero_7bits;
    std::vector<uint64_t> sub_layer_reserved_zero_33bits;
    std::vector<uint64_t> sub_layer_reserved_zero_34bits;
    std::vector<uint64_t> sub_layer_reserved_zero_35bits;
    std::vector<uint64_t> sub_layer_reserved_zero_43bits;
    std::vector<uint8_t> sub_layer_inbld_flag;
    std::vector<uint8_t> sub_layer_reserved_zero_bit;
    std::vector<uint8_t> sub_layer_level_idc;

public:
    SrsHevcProfileTierLevel();
    virtual ~SrsHevcProfileTierLevel();
};

/**
 * Sub-layer HRD parameters
 * @see E.2.3 Sub-layer HRD parameters syntax
 * @doc ITU-T-H.265-2021.pdf, page 440.
 */
struct SrsHevcSubLayerHrdParameters
{
    std::vector<int> bit_rate_value_minus1;
    std::vector<int> cpb_size_value_minus1;
    std::vector<int> cpb_size_du_value_minus1;
    std::vector<int> bit_rate_du_value_minus1;
    std::vector<uint8_t> cbr_flag;
};

/**
 * HRD parameters
 * @see E.2.2 HRD parameters syntax
 * @doc ITU-T-H.265-2021.pdf, page 439.
 */
struct SrsHevcHrdParameters
{
    uint8_t nal_hrd_parameters_present_flag;
    uint8_t vcl_hrd_parameters_present_flag;
    uint8_t sub_pic_hrd_params_present_flag;
    uint8_t tick_divisor_minus2;
    uint8_t du_cpb_removal_delay_increment_length_minus1;
    uint8_t sub_pic_cpb_params_in_pic_timing_sei_flag;
    uint8_t dpb_output_delay_du_length_minus1;
    uint8_t bit_rate_scale;
    uint8_t cpb_size_scale;
    uint8_t cpb_size_du_scale;
    uint8_t initial_cpb_removal_delay_length_minus1;
    uint8_t au_cpb_removal_delay_length_minus1;
    uint8_t dpb_output_delay_length_minus1;
    std::vector<uint8_t> fixed_pic_rate_general_flag;
    std::vector<uint8_t> fixed_pic_rate_within_cvs_flag;
    std::vector<int> elemental_duration_in_tc_minus1;
    std::vector<uint8_t> low_delay_hrd_flag;
    std::vector<int> cpb_cnt_minus1;
    SrsHevcSubLayerHrdParameters sub_layer_hrd_parameters; // nal
    SrsHevcSubLayerHrdParameters sub_layer_hrd_parameters_v; // vlc
};

/**
 * Scaling list data
 * @see 7.3.4 Scaling list data syntax
 * @doc ITU-T-H.265-2021.pdf, page 65.
 */
struct SrsHevcScalingListData
{
    uint32_t scaling_list_pred_mode_flag[4][6];
    uint32_t scaling_list_pred_matrix_id_delta[4][6];
    int32_t scaling_list_dc_coef_minus8[4][6];
    uint32_t ScalingList[4][6][64];
    int32_t coefNum;
};

/**
 * Sequence parameter set range extension
 * @see 7.3.2.2.2 Sequence parameter set range extension syntax
 * @doc ITU-T-H.265-2021.pdf, page 57.
 */
struct SrsHevcSpsRangeExtension
{
    uint8_t transform_skip_rotation_enabled_flag;
    uint8_t transform_skip_context_enabled_flag;
    uint8_t implicit_rdpcm_enabled_flag;
    uint8_t explicit_rdpcm_enabled_flag;
    uint8_t extended_precision_processing_flag;
    uint8_t intra_smoothing_disabled_flag;
    uint8_t high_precision_offsets_enabled_flag;
    uint8_t persistent_rice_adaptation_enabled_flag;
    uint8_t cabac_bypass_alignment_enabled_flag;
};

/**
 * Picture parameter set RBSP syntax
 * @see 7.3.2.3.1 General picture parameter set RBSP syntax
 * @doc ITU-T-H.265-2021.pdf, page 57.
 */
struct SrsHevcPpsRangeExtension
{
    uint32_t log2_max_transform_skip_block_size_minus2;
    uint8_t cross_component_prediction_enabled_flag;
    uint8_t chroma_qp_offset_list_enabled_flag;
    uint32_t diff_cu_chroma_qp_offset_depth;
    uint32_t chroma_qp_offset_list_len_minus1;
    std::vector<int> cb_qp_offset_list;
    std::vector<int> cr_qp_offset_list;
    uint32_t log2_sao_offset_scale_luma;
    uint32_t log2_sao_offset_scale_chroma;
};

/**
 * Short-term reference picture set
 * @see 7.3.7 Short-term reference picture set syntax
 * @doc ITU-T-H.265-2021.pdf, page 70.
 */
struct SrsHevcStRefPicSet
{
    uint8_t inter_ref_pic_set_prediction_flag;
    int delta_idx_minus1;
    uint8_t delta_rps_sign;
    int abs_delta_rps_minus1;
    std::vector<uint8_t> used_by_curr_pic_flag;
    std::vector<uint8_t> use_delta_flag;
    int num_negative_pics;
    int num_positive_pics;

    std::vector<int> delta_poc_s0_minus1;
    std::vector<uint8_t> used_by_curr_pic_s0_flag;
    std::vector<int> delta_poc_s1_minus1;
    std::vector<uint8_t> used_by_curr_pic_s1_flag;
};

/**
 * VUI parameters
 * @see E.2.1 VUI parameters syntax
 * @doc ITU-T-H.265-2021.pdf, page 437.
 */
struct SrsHevcVuiParameters
{
    uint8_t aspect_ratio_info_present_flag;
    uint8_t aspect_ratio_idc;
    int sar_width;
    int sar_height;
    uint8_t overscan_info_present_flag;
    uint8_t overscan_appropriate_flag;
    uint8_t video_signal_type_present_flag;
    uint8_t video_format;
    uint8_t video_full_range_flag;
    uint8_t colour_description_present_flag;
    uint8_t colour_primaries;
    uint8_t transfer_characteristics;
    uint8_t matrix_coeffs;
    uint8_t chroma_loc_info_present_flag;
    int chroma_sample_loc_type_top_field;
    int chroma_sample_loc_type_bottom_field;
    uint8_t neutral_chroma_indication_flag;
    uint8_t field_seq_flag;
    uint8_t frame_field_info_present_flag;
    uint8_t default_display_window_flag;
    int def_disp_win_left_offset;
    int def_disp_win_right_offset;
    int def_disp_win_top_offset;
    int def_disp_win_bottom_offset;
    uint8_t vui_timing_info_present_flag;
    uint32_t vui_num_units_in_tick;
    uint32_t vui_time_scale;
    uint8_t vui_poc_proportional_to_timing_flag;
    int vui_num_ticks_poc_diff_one_minus1;
    uint8_t vui_hrd_parameters_present_flag;
    SrsHevcHrdParameters hrd_parameters;
    uint8_t bitstream_restriction_flag;
    uint8_t tiles_fixed_structure_flag;
    uint8_t motion_vectors_over_pic_boundaries_flag;
    uint8_t restricted_ref_pic_lists_flag;
    int min_spatial_segmentation_idc;
    int max_bytes_per_pic_denom;
    int max_bits_per_min_cu_denom;
    int log2_max_mv_length_horizontal;
    int log2_max_mv_length_vertical;
};

/**
 * Video Parameter Set
 * @see 7.3.2.1 Video parameter set RBSP syntax
 * @doc ITU-T-H.265-2021.pdf, page 54.
 */
struct SrsHevcRbspVps
{
    uint8_t vps_video_parameter_set_id;    // u(4)
    uint8_t vps_base_layer_internal_flag;  // u(1)
    uint8_t vps_base_layer_available_flag; // u(1)
    uint8_t vps_max_layers_minus1;         // u(6)
    uint8_t vps_max_sub_layers_minus1;     // u(3)
    uint8_t vps_temporal_id_nesting_flag;  // u(1)
    int vps_reserved_0xffff_16bits;        // u(16)
    SrsHevcProfileTierLevel ptl;
    uint8_t vps_sub_layer_ordering_info_present_flag;
    // Sublayers
    uint32_t vps_max_dec_pic_buffering_minus1[8]; // max u(3)
    uint32_t vps_max_num_reorder_pics[8];
    uint32_t vps_max_latency_increase_plus1[8];
    uint8_t vps_max_layer_id;
    uint32_t vps_num_layer_sets_minus1;
    std::vector<std::vector<uint8_t>> layer_id_included_flag;
    uint8_t vps_timing_info_present_flag;
    uint32_t vps_num_units_in_tick;
    uint32_t vps_time_scale;
    uint8_t vps_poc_proportional_to_timing_flag;
    uint32_t vps_num_ticks_poc_diff_one_minus1;
    uint32_t vps_num_hrd_parameters;
    std::vector<uint32_t> hrd_layer_set_idx;
    std::vector<uint8_t> cprms_present_flag;
    SrsHevcHrdParameters hrd_parameters;
    uint8_t vps_extension_flag;
    uint8_t vps_extension_data_flag;
};

/**
 * Sequence Parameter Set
 * @see 7.3.2.2 Sequence parameter set RBSP syntax
 * @doc ITU-T-H.265-2021.pdf, page 55.
 */
struct SrsHevcRbspSps
{
    uint8_t sps_video_parameter_set_id;
    uint8_t sps_max_sub_layers_minus1;
    uint8_t sps_temporal_id_nesting_flag;
    SrsHevcProfileTierLevel ptl;
    uint32_t sps_seq_parameter_set_id;
    uint32_t chroma_format_idc;
    uint8_t separate_colour_plane_flag;
    uint32_t pic_width_in_luma_samples;
    uint32_t pic_height_in_luma_samples;
    uint32_t conformance_window_flag;
    uint32_t conf_win_left_offset;
    uint32_t conf_win_right_offset;
    uint32_t conf_win_top_offset;
    uint32_t conf_win_bottom_offset;
    uint32_t bit_depth_luma_minus8;
    uint32_t bit_depth_chroma_minus8;
    uint32_t log2_max_pic_order_cnt_lsb_minus4;
    uint8_t sps_sub_layer_ordering_info_present_flag;
    uint32_t sps_max_dec_pic_buffering_minus1[8]; // max u(3)
    uint32_t sps_max_num_reorder_pics[8];
    uint32_t sps_max_latency_increase_plus1[8];
    uint32_t log2_min_luma_coding_block_size_minus3;
    uint32_t log2_diff_max_min_luma_coding_block_size;
    uint32_t log2_min_luma_transform_block_size_minus2;
    uint32_t log2_diff_max_min_luma_transform_block_size;
    uint32_t max_transform_hierarchy_depth_inter;
    uint32_t max_transform_hierarchy_depth_intra;
    uint8_t scaling_list_enabled_flag;
    uint8_t sps_infer_scaling_list_flag;
    uint32_t sps_scaling_list_ref_layer_id;
    uint32_t sps_scaling_list_data_present_flag;
    SrsHevcScalingListData scaling_list_data;
    uint8_t amp_enabled_flag;
    uint8_t sample_adaptive_offset_enabled_flag;
    uint8_t pcm_enabled_flag;
    uint8_t pcm_sample_bit_depth_luma_minus1;
    uint8_t pcm_sample_bit_depth_chroma_minus1;
    uint32_t log2_min_pcm_luma_coding_block_size_minus3;
    uint32_t log2_diff_max_min_pcm_luma_coding_block_size;
    uint8_t pcm_loop_filter_disabled_flag;
    uint32_t num_short_term_ref_pic_sets;
    std::vector<SrsHevcStRefPicSet> st_ref_pic_set;
    uint8_t long_term_ref_pics_present_flag;
    uint32_t num_long_term_ref_pics_sps;
    uint32_t lt_ref_pic_poc_lsb_sps_bytes;
    std::vector<uint32_t> lt_ref_pic_poc_lsb_sps;
    std::vector<uint8_t> used_by_curr_pic_lt_sps_flag;
    uint8_t sps_temporal_mvp_enabled_flag;
    uint8_t strong_intra_smoothing_enabled_flag;
    uint8_t vui_parameters_present_flag;
    SrsHevcVuiParameters vui;
    uint8_t sps_extension_present_flag;
    uint8_t sps_range_extension_flag;
    uint8_t sps_multilayer_extension_flag;
    uint8_t sps_3d_extension_flag;
    uint8_t sps_extension_5bits;
    SrsHevcSpsRangeExtension sps_range_extension;
    uint8_t inter_view_mv_vert_constraint_flag; // sps_multilayer_extension_t sps_multilayer_extension;
    // sps_3d_extension_t sps_3d_extension;
    // int sps_extension_data_flag; // no need
    // rbsp_trailing_bits()...
};

/**
 * Picture Parameter Set
 * @see 7.3.2.3 Picture parameter set RBSP syntax
 * @doc ITU-T-H.265-2021.pdf, page 57.
 */
struct SrsHevcRbspPps
{
    uint8_t pps_pic_parameter_set_id;
    uint8_t pps_seq_parameter_set_id;
    uint8_t dependent_slice_segments_enabled_flag;
    uint8_t output_flag_present_flag;
    uint8_t num_extra_slice_header_bits;
    uint8_t sign_data_hiding_enabled_flag;
    uint8_t cabac_init_present_flag;
    uint32_t num_ref_idx_l0_default_active_minus1;
    uint32_t num_ref_idx_l1_default_active_minus1;
    int32_t init_qp_minus26;
    uint8_t constrained_intra_pred_flag;
    uint8_t transform_skip_enabled_flag;
    uint8_t cu_qp_delta_enabled_flag;
    uint32_t diff_cu_qp_delta_depth;
    int32_t pps_cb_qp_offset;
    int32_t pps_cr_qp_offset;
    uint8_t pps_slice_chroma_qp_offsets_present_flag;
    uint8_t weighted_pred_flag;
    uint32_t weighted_bipred_flag;
    uint8_t transquant_bypass_enabled_flag;
    uint8_t tiles_enabled_flag;
    uint8_t entropy_coding_sync_enabled_flag;
    uint32_t num_tile_columns_minus1;
    uint32_t num_tile_rows_minus1;
    uint32_t uniform_spacing_flag;
    std::vector<uint32_t> column_width_minus1;
    std::vector<uint32_t> row_height_minus1;
    uint8_t loop_filter_across_tiles_enabled_flag;
    uint8_t pps_loop_filter_across_slices_enabled_flag;
    uint8_t deblocking_filter_control_present_flag;
    uint8_t deblocking_filter_override_enabled_flag;
    uint8_t pps_deblocking_filter_disabled_flag;
    int32_t pps_beta_offset_div2;
    int32_t pps_tc_offset_div2;
    uint8_t pps_scaling_list_data_present_flag;
    SrsHevcScalingListData scaling_list_data;
    uint8_t lists_modification_present_flag;
    uint32_t log2_parallel_merge_level_minus2;
    uint8_t slice_segment_header_extension_present_flag;
    uint8_t pps_extension_present_flag;
    uint8_t pps_range_extension_flag;
    uint8_t pps_multilayer_extension_flag;
    uint8_t pps_3d_extension_flag;
    uint8_t pps_scc_extension_flag;
    uint8_t pps_extension_4bits;
    SrsHevcPpsRangeExtension pps_range_extension;
    // pps_multilayer_extension_t pps_multilayer_extension;
    // pps_3d_extension_t pps_3d_extension;
    uint8_t pps_extension_data_flag;
    // rbsp_trailing_bits( ) ...
};

struct SrsHevcDecoderConfigurationRecord
{
    uint8_t configuration_version;
    uint8_t general_profile_space;
    uint8_t general_tier_flag;
    uint8_t general_profile_idc;
    uint32_t general_profile_compatibility_flags;
    uint64_t general_constraint_indicator_flags;
    uint8_t general_level_idc;
    uint16_t min_spatial_segmentation_idc;
    uint8_t parallelism_type;
    uint8_t chroma_format;
    uint8_t bit_depth_luma_minus8;
    uint8_t bit_depth_chroma_minus8;
    uint16_t avg_frame_rate;
    uint8_t constant_frame_rate;
    uint8_t num_temporal_layers;
    uint8_t temporal_id_nested;
    uint8_t length_size_minus_one;
    std::vector<SrsHevcHvccNalu> nalu_vec;

    SrsHevcRbspVps vps_table[SrsHevcMax_VPS_COUNT];
    SrsHevcRbspSps sps_table[SrsHevcMax_SPS_COUNT];
    SrsHevcRbspPps pps_table[SrsHevcMax_PPS_COUNT];
};

#endif

/**
 * Table 7-6 – Name association to slice_type
 * ISO_IEC_14496-10-AVC-2012.pdf, page 105.
 */
enum SrsAvcSliceType
{
    SrsAvcSliceTypeP   = 0,
    SrsAvcSliceTypeB   = 1,
    SrsAvcSliceTypeI   = 2,
    SrsAvcSliceTypeSP  = 3,
    SrsAvcSliceTypeSI  = 4,
    SrsAvcSliceTypeP1  = 5,
    SrsAvcSliceTypeB1  = 6,
    SrsAvcSliceTypeI1  = 7,
    SrsAvcSliceTypeSP1 = 8,
    SrsAvcSliceTypeSI1 = 9,
};

/**
 * the avc payload format, must be ibmf or annexb format.
 * we guess by annexb first, then ibmf for the first time,
 * and we always use the guessed format for the next time.
 */
enum SrsAvcPayloadFormat
{
    SrsAvcPayloadFormatGuess = 0,
    SrsAvcPayloadFormatAnnexb,
    SrsAvcPayloadFormatIbmf,
};

/**
 * the aac profile, for ADTS(HLS/TS)
 * @see https://github.com/ossrs/srs/issues/310
 */
enum SrsAacProfile
{
    SrsAacProfileReserved = 3,
    
    // @see 7.1 Profiles, ISO_IEC_13818-7-AAC-2004.pdf, page 40
    SrsAacProfileMain = 0,
    SrsAacProfileLC = 1,
    SrsAacProfileSSR = 2,
};
std::string srs_aac_profile2str(SrsAacProfile aac_profile);

/**
 * the aac object type, for RTMP sequence header
 * for AudioSpecificConfig, @see ISO_IEC_14496-3-AAC-2001.pdf, page 33
 * for audioObjectType, @see ISO_IEC_14496-3-AAC-2001.pdf, page 23
 */
enum SrsAacObjectType
{
    SrsAacObjectTypeReserved = 0,
    SrsAacObjectTypeForbidden = 0,
    
    // Table 1.1 - Audio Object Type definition
    // @see @see ISO_IEC_14496-3-AAC-2001.pdf, page 23
    SrsAacObjectTypeAacMain = 1,
    SrsAacObjectTypeAacLC = 2,
    SrsAacObjectTypeAacSSR = 3,
    
    // AAC HE = LC+SBR
    SrsAacObjectTypeAacHE = 5,
    // AAC HEv2 = LC+SBR+PS
    SrsAacObjectTypeAacHEV2 = 29,
};
std::string srs_aac_object2str(SrsAacObjectType aac_object);
// ts/hls/adts audio header profile to RTMP sequence header object type.
SrsAacObjectType srs_aac_ts2rtmp(SrsAacProfile profile);
// RTMP sequence header object type to ts/hls/adts audio header profile.
SrsAacProfile srs_aac_rtmp2ts(SrsAacObjectType object_type);

/**
 * the profile for avc/h.264.
 * @see Annex A Profiles and levels, ISO_IEC_14496-10-AVC-2003.pdf, page 205.
 */
enum SrsAvcProfile
{
    SrsAvcProfileReserved = 0,
    
    // @see ffmpeg, libavcodec/avcodec.h:2713
    SrsAvcProfileBaseline = 66,
    // FF_PROFILE_H264_CONSTRAINED  (1<<9)  // 8+1; constraint_set1_flag
    // FF_PROFILE_H264_CONSTRAINED_BASELINE (66|FF_PROFILE_H264_CONSTRAINED)
    SrsAvcProfileConstrainedBaseline = 578,
    SrsAvcProfileMain = 77,
    SrsAvcProfileExtended = 88,
    SrsAvcProfileHigh = 100,
    SrsAvcProfileHigh10 = 110,
    SrsAvcProfileHigh10Intra = 2158,
    SrsAvcProfileHigh422 = 122,
    SrsAvcProfileHigh422Intra = 2170,
    SrsAvcProfileHigh444 = 144,
    SrsAvcProfileHigh444Predictive = 244,
    SrsAvcProfileHigh444Intra = 2192,
};
std::string srs_avc_profile2str(SrsAvcProfile profile);

/**
 * the level for avc/h.264.
 * @see Annex A Profiles and levels, ISO_IEC_14496-10-AVC-2003.pdf, page 207.
 */
enum SrsAvcLevel
{
    SrsAvcLevelReserved = 0,
    
    SrsAvcLevel_1 = 10,
    SrsAvcLevel_11 = 11,
    SrsAvcLevel_12 = 12,
    SrsAvcLevel_13 = 13,
    SrsAvcLevel_2 = 20,
    SrsAvcLevel_21 = 21,
    SrsAvcLevel_22 = 22,
    SrsAvcLevel_3 = 30,
    SrsAvcLevel_31 = 31,
    SrsAvcLevel_32 = 32,
    SrsAvcLevel_4 = 40,
    SrsAvcLevel_41 = 41,
    SrsAvcLevel_5 = 50,
    SrsAvcLevel_51 = 51,
};
std::string srs_avc_level2str(SrsAvcLevel level);

#ifdef SRS_H265

/**
 * the profile for hevc/h.265, Annex A Profiles, tiers and levels
 * @see A.3 Profiles
 * @doc ITU-T-H.265-2021.pdf, page 268.
 */
enum SrsHevcProfile
{
    SrsHevcProfileReserved = 0,

    // @see ffmpeg, libavcodec/avcodec.h:2986
    SrsHevcProfileMain = 1,
    SrsHevcProfileMain10 = 2,
    SrsHevcProfileMainStillPicture = 3,
    SrsHevcProfileRext = 4,
};
std::string srs_hevc_profile2str(SrsHevcProfile profile);

/**
 * the level for hevc/h.265, Annex A Profiles, tiers and levels
 * @see A.4 Tiers and levels
 * @doc ITU-T-H.265-2021.pdf, page 283.
 */
enum SrsHevcLevel
{
    SrsHevcLevelReserved = 0,

    SrsHevcLevel_1  = 30,
    SrsHevcLevel_2  = 60,
    SrsHevcLevel_21 = 63,
    SrsHevcLevel_3  = 90,
    SrsHevcLevel_31 = 93,
    SrsHevcLevel_4  = 120,
    SrsHevcLevel_41 = 123,
    SrsHevcLevel_5  = 150,
    SrsHevcLevel_51 = 153,
    SrsHevcLevel_52 = 156,
    SrsHevcLevel_6  = 180,
    SrsHevcLevel_61 = 183,
    SrsHevcLevel_62 = 186,
};
std::string srs_hevc_level2str(SrsHevcLevel level);

#endif

/**
 * A sample is the unit of frame.
 * It's a NALU for H.264.
 * It's the whole AAC raw data for AAC.
 * @remark Neither SPS/PPS or ASC is sample unit, it's codec sequence header.
 */
class SrsSample
{
public:
    // The size of unit.
    int size;
    // The ptr of unit, user must free it.
    char* bytes;
    // Whether is B frame.
    bool bframe;
public:
    SrsSample();
    SrsSample(char* b, int s);
    ~SrsSample();
public:
    // If we need to know whether sample is bframe, we have to parse the NALU payload.
    srs_error_t parse_bframe();
    // Copy sample, share the bytes pointer.
    SrsSample* copy();
};

/**
 * The codec is the information of encoder,
 * corresponding to the sequence header of FLV,
 * parsed to detail info.
 */
class SrsCodecConfig
{
public:
    SrsCodecConfig();
    virtual ~SrsCodecConfig();
};

/**
 * The audio codec info.
 */
class SrsAudioCodecConfig : public SrsCodecConfig
{
    // In FLV specification.
public:
    // The audio codec id; for FLV, it's SoundFormat.
    SrsAudioCodecId id;
    // The audio sample rate; for FLV, it's SoundRate.
    SrsAudioSampleRate sound_rate;
    // The audio sample size, such as 16 bits; for FLV, it's SoundSize.
    SrsAudioSampleBits sound_size;
    // The audio number of channels; for FLV, it's SoundType.
    // TODO: FIXME: Rename to sound_channels.
    SrsAudioChannels sound_type;
    int audio_data_rate; // in bps
    // In AAC specification.
public:
    /**
     * audio specified
     * audioObjectType, in 1.6.2.1 AudioSpecificConfig, page 33,
     * 1.5.1.1 Audio object type definition, page 23,
     *           in ISO_IEC_14496-3-AAC-2001.pdf.
     */
    SrsAacObjectType aac_object;
    /**
     * samplingFrequencyIndex
     */
    uint8_t aac_sample_rate;
    /**
     * channelConfiguration
     */
    uint8_t aac_channels;
    // Sequence header payload.
public:
    /**
     * the aac extra data, the AAC sequence header,
     * without the flv codec header,
     * @see: ffmpeg, AVCodecContext::extradata
     */
    std::vector<char> aac_extra_data;
public:
    SrsAudioCodecConfig();
    virtual ~SrsAudioCodecConfig();
public:
    virtual bool is_aac_codec_ok();
};

/**
 * The video codec info.
 */
class SrsVideoCodecConfig : public SrsCodecConfig
{
public:
    SrsVideoCodecId id;
    int video_data_rate; // in bps
    double frame_rate;
    double duration;
    int width;
    int height;
public:
    /**
     * the avc extra data, the AVC sequence header,
     * without the flv codec header,
     * @see: ffmpeg, AVCodecContext::extradata
     */
    std::vector<char> avc_extra_data;
public:
    /**
     * video specified
     */
    // profile_idc, ISO_IEC_14496-10-AVC-2003.pdf, page 45.
    SrsAvcProfile avc_profile;
    // level_idc, ISO_IEC_14496-10-AVC-2003.pdf, page 45.
    SrsAvcLevel avc_level;
#ifdef SRS_H265
    // The profile_idc, ITU-T-H.265-2021.pdf, page 62.
    SrsHevcProfile hevc_profile;
    // The level_idc, ITU-T-H.265-2021.pdf, page 63.
    SrsHevcLevel hevc_level;
#endif
    // lengthSizeMinusOne, ISO_IEC_14496-15-AVC-format-2012.pdf, page 16
    int8_t NAL_unit_length;
    // Note that we may resize the vector, so the under-layer bytes may change.
    std::vector<char> sequenceParameterSetNALUnit;
    std::vector<char> pictureParameterSetNALUnit;
public:
    // the avc payload format.
    SrsAvcPayloadFormat payload_format;
#ifdef SRS_H265
public:
    SrsHevcDecoderConfigurationRecord hevc_dec_conf_record_;
#endif
public:
    SrsVideoCodecConfig();
    virtual ~SrsVideoCodecConfig();
public:
    virtual bool is_avc_codec_ok();
};

// A frame, consists of a codec and a group of samples.
// TODO: FIXME: Rename to packet to follow names of FFmpeg, which means before decoding or after decoding.
class SrsFrame
{
public:
    // The DTS/PTS in milliseconds, which is TBN=1000.
    int64_t dts;
    // PTS = DTS + CTS.
    int32_t cts;
public:
    // The codec info of frame.
    SrsCodecConfig* codec;
    // The actual parsed number of samples.
    int nb_samples;
    // The sampels cache.
    SrsSample samples[SrsMaxNbSamples];
public:
    SrsFrame();
    virtual ~SrsFrame();
public:
    // Initialize the frame, to parse sampels.
    virtual srs_error_t initialize(SrsCodecConfig* c);
    // Add a sample to frame.
    virtual srs_error_t add_sample(char* bytes, int size);
};

// A audio frame, besides a frame, contains the audio frame info, such as frame type.
// TODO: FIXME: Rename to packet to follow names of FFmpeg, which means before decoding or after decoding.
class SrsAudioFrame : public SrsFrame
{
public:
    SrsAudioAacFrameTrait aac_packet_type;
public:
    SrsAudioFrame();
    virtual ~SrsAudioFrame();
public:
    virtual SrsAudioCodecConfig* acodec();
};

// A video frame, besides a frame, contains the video frame info, such as frame type.
// TODO: FIXME: Rename to packet to follow names of FFmpeg, which means before decoding or after decoding.
class SrsVideoFrame : public SrsFrame
{
public:
    // video specified
    SrsVideoAvcFrameType frame_type;
    SrsVideoAvcFrameTrait avc_packet_type;
    // whether sample_units contains IDR frame.
    bool has_idr;
    // Whether exists AUD NALU.
    bool has_aud;
    // Whether exists SPS/PPS NALU.
    bool has_sps_pps;
    // The first nalu type.
    SrsAvcNaluType first_nalu_type;
public:
    SrsVideoFrame();
    virtual ~SrsVideoFrame();
public:
    // Initialize the frame, to parse sampels.
    virtual srs_error_t initialize(SrsCodecConfig* c);
    // Add the sample without ANNEXB or IBMF header, or RAW AAC or MP3 data.
    virtual srs_error_t add_sample(char* bytes, int size);
public:
    virtual SrsVideoCodecConfig* vcodec();
};

/**
 * A codec format, including one or many stream, each stream identified by a frame.
 * For example, a typical RTMP stream format, consits of a video and audio frame.
 * Maybe some RTMP stream only has a audio stream, for instance, redio application.
 */
class SrsFormat
{
public:
    SrsAudioFrame* audio;
    SrsAudioCodecConfig* acodec;
    SrsVideoFrame* video;
    SrsVideoCodecConfig* vcodec;
public:
    char* raw;
    int nb_raw;
public:
    // for sequence header, whether parse the h.264 sps.
    // TODO: FIXME: Refine it.
    bool avc_parse_sps;
    // Whether try to parse in ANNEXB, then by IBMF.
    bool try_annexb_first;
public:
    SrsFormat();
    virtual ~SrsFormat();
public:
    // Initialize the format.
    virtual srs_error_t initialize();
    // When got a parsed audio packet.
    // @param data The data in FLV format.
    virtual srs_error_t on_audio(int64_t timestamp, char* data, int size);
    // When got a parsed video packet.
    // @param data The data in FLV format.
    virtual srs_error_t on_video(int64_t timestamp, char* data, int size);
    // When got a audio aac sequence header.
    virtual srs_error_t on_aac_sequence_header(char* data, int size);
public:
    virtual bool is_aac_sequence_header();
    virtual bool is_mp3_sequence_header();
    virtual bool is_avc_sequence_header();
private:
    // Demux the video packet in H.264 codec.
    // The packet is muxed in FLV format, defined in flv specification.
    //          Demux the sps/pps from sequence header.
    //          Demux the samples from NALUs.
    virtual srs_error_t video_avc_demux(SrsBuffer* stream, int64_t timestamp);
#ifdef SRS_H265
private:
    virtual srs_error_t hevc_demux_hvcc(SrsBuffer* stream);
private:
    virtual srs_error_t hevc_demux_vps_sps_pps(SrsHevcHvccNalu *nal);
    virtual srs_error_t hevc_demux_vps_rbsp(char *rbsp, int nb_rbsp);
    virtual srs_error_t hevc_demux_sps_rbsp(char *rbsp, int nb_rbsp);
    virtual srs_error_t hevc_demux_pps_rbsp(char *rbsp, int nb_rbsp);
    virtual srs_error_t hevc_demux_rbsp_ptl(SrsBitBuffer* bs, SrsHevcProfileTierLevel* ptl, int profile_present_flag, int max_sub_layers_minus1);
public:
    virtual srs_error_t hevc_demux_vps(SrsBuffer *stream);
    virtual srs_error_t hevc_demux_sps(SrsBuffer *stream);
    virtual srs_error_t hevc_demux_pps(SrsBuffer *stream);
#endif
private:
    // Parse the H.264 SPS/PPS.
    virtual srs_error_t avc_demux_sps_pps(SrsBuffer* stream);
    virtual srs_error_t avc_demux_sps();
    virtual srs_error_t avc_demux_sps_rbsp(char* rbsp, int nb_rbsp);
private:
    // Parse the H.264 or H.265 NALUs.
    virtual srs_error_t video_nalu_demux(SrsBuffer* stream);
    // Demux the avc NALU in "AnnexB" from ISO_IEC_14496-10-AVC-2003.pdf, page 211.
    virtual srs_error_t avc_demux_annexb_format(SrsBuffer* stream);
    virtual srs_error_t do_avc_demux_annexb_format(SrsBuffer* stream);
    // Demux the avc NALU in "ISO Base Media File Format" from ISO_IEC_14496-15-AVC-format-2012.pdf, page 20
    virtual srs_error_t avc_demux_ibmf_format(SrsBuffer* stream);
    virtual srs_error_t do_avc_demux_ibmf_format(SrsBuffer* stream);
private:
    // Demux the audio packet in AAC codec.
    //          Demux the asc from sequence header.
    //          Demux the sampels from RAW data.
    virtual srs_error_t audio_aac_demux(SrsBuffer* stream, int64_t timestamp);
    virtual srs_error_t audio_mp3_demux(SrsBuffer* stream, int64_t timestamp, bool fresh);
public:
    // Directly demux the sequence header, without RTMP packet header.
    virtual srs_error_t audio_aac_sequence_header_demux(char* data, int size);
};

#endif

