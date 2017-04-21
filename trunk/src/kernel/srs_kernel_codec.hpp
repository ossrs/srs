/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2017 OSSRS(winlin)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef SRS_KERNEL_CODEC_HPP
#define SRS_KERNEL_CODEC_HPP

#include <srs_core.hpp>

#include <string>
#include <vector>

class SrsBuffer;

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
    SrsVideoAvcFrameTraitReserved = 3,
    SrsVideoAvcFrameTraitForbidden = 3,
    
    SrsVideoAvcFrameTraitSequenceHeader = 0,
    SrsVideoAvcFrameTraitNALU = 1,
    SrsVideoAvcFrameTraitSequenceHeaderEOF = 2,
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
    SrsAudioCodecIdReservedMP3_8kHz = 14,
    SrsAudioCodecIdReservedDeviceSpecificSound = 15,
};
std::string srs_audio_codec_id2str(SrsAudioCodecId codec);

/**
 * The audio AAC frame trait(characteristic).
 * @doc video_file_format_spec_v10_1.pdf, page 77, E.4.2 Audio Tags
 * AACPacketType IF SoundFormat == 10 UI8
 * The following values are defined:
 *      0 = AAC sequence header
 *      1 = AAC raw
 */
enum SrsAudioAacFrameTrait
{
    // set to the max value to reserved, for array map.
    SrsAudioAacFrameTraitReserved = 2,
    SrsAudioAacFrameTraitForbidden = 2,
    
    SrsAudioAacFrameTraitSequenceHeader = 0,
    SrsAudioAacFrameTraitRawData = 1,
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
    SrsAudioSampleRateReserved = 4,
    SrsAudioSampleRateForbidden = 4,
    
    SrsAudioSampleRate5512 = 0,
    SrsAudioSampleRate11025 = 1,
    SrsAudioSampleRate22050 = 2,
    SrsAudioSampleRate44100 = 3,
};
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
    static bool sh(char* data, int size);
    /**
     * check codec h264.
     */
    static bool h264(char* data, int size);
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
    // The ptr of unit, user must manage it.
    char* bytes;
public:
    SrsSample();
    virtual ~SrsSample();
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
    // lengthSizeMinusOne, ISO_IEC_14496-15-AVC-format-2012.pdf, page 16
    int8_t NAL_unit_length;
    std::vector<char> sequenceParameterSetNALUnit;
    std::vector<char> pictureParameterSetNALUnit;
public:
    // the avc payload format.
    SrsAvcPayloadFormat payload_format;
public:
    SrsVideoCodecConfig();
    virtual ~SrsVideoCodecConfig();
public:
    virtual bool is_avc_codec_ok();
};

/**
 * A frame, consists of a codec and a group of samples.
 */
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
    virtual int initialize(SrsCodecConfig* c);
    // Add a sample to frame.
    virtual int add_sample(char* bytes, int size);
};

/**
 * A audio frame, besides a frame, contains the audio frame info, such as frame type.
 */
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

/**
 * A video frame, besides a frame, contains the video frame info, such as frame type.
 */
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
    // Add the sample without ANNEXB or IBMF header, or RAW AAC or MP3 data.
    virtual int add_sample(char* bytes, int size);
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
    SrsBuffer* buffer;
public:
    // for sequence header, whether parse the h.264 sps.
    // TODO: FIXME: Refine it.
    bool            avc_parse_sps;
public:
    SrsFormat();
    virtual ~SrsFormat();
public:
    // Initialize the format.
    virtual int initialize();
    // When got a parsed audio packet.
    // @param data The data in FLV format.
    virtual int on_audio(int64_t timestamp, char* data, int size);
    // When got a parsed video packet.
    // @param data The data in FLV format.
    virtual int on_video(int64_t timestamp, char* data, int size);
    // When got a audio aac sequence header.
    virtual int on_aac_sequence_header(char* data, int size);
public:
    virtual bool is_aac_sequence_header();
    virtual bool is_avc_sequence_header();
private:
    // Demux the video packet in H.264 codec.
    // The packet is muxed in FLV format, defined in flv specification.
    //          Demux the sps/pps from sequence header.
    //          Demux the samples from NALUs.
    virtual int video_avc_demux(SrsBuffer* stream, int64_t timestamp);
private:
    // Parse the H.264 SPS/PPS.
    virtual int avc_demux_sps_pps(SrsBuffer* stream);
    virtual int avc_demux_sps();
    virtual int avc_demux_sps_rbsp(char* rbsp, int nb_rbsp);
private:
    // Parse the H.264 NALUs.
    virtual int video_nalu_demux(SrsBuffer* stream);
    // Demux the avc NALU in "AnnexB" from ISO_IEC_14496-10-AVC-2003.pdf, page 211.
    virtual int avc_demux_annexb_format(SrsBuffer* stream);
    // Demux the avc NALU in "ISO Base Media File Format" from ISO_IEC_14496-15-AVC-format-2012.pdf, page 20
    virtual int avc_demux_ibmf_format(SrsBuffer* stream);
private:
    // Demux the audio packet in AAC codec.
    //          Demux the asc from sequence header.
    //          Demux the sampels from RAW data.
    virtual int audio_aac_demux(SrsBuffer* stream, int64_t timestamp);
    virtual int audio_mp3_demux(SrsBuffer* stream, int64_t timestamp);
public:
    // Directly demux the sequence header, without RTMP packet header.
    virtual int audio_aac_sequence_header_demux(char* data, int size);
};

#endif

