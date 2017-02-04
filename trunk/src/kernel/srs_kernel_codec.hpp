/*
The MIT License (MIT)

Copyright (c) 2013-2017 SRS(ossrs)

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#ifndef SRS_KERNEL_CODEC_HPP
#define SRS_KERNEL_CODEC_HPP

/*
#include <srs_kernel_codec.hpp>
*/

#include <srs_core.hpp>

#include <string>

class SrsBuffer;

// AACPacketType IF SoundFormat == 10 UI8
// The following values are defined:
//     0 = AAC sequence header
//     1 = AAC raw
enum SrsCodecAudioType
{
    // set to the max value to reserved, for array map.
    SrsCodecAudioTypeReserved = 2,
    SrsCodecAudioTypeForbidden = 2,
    
    SrsCodecAudioTypeSequenceHeader = 0,
    SrsCodecAudioTypeRawData = 1,
};

// E.4.3.1 VIDEODATA
// Frame Type UB [4]
// Type of video frame. The following values are defined:
//     1 = key frame (for AVC, a seekable frame)
//     2 = inter frame (for AVC, a non-seekable frame)
//     3 = disposable inter frame (H.263 only)
//     4 = generated key frame (reserved for server use only)
//     5 = video info/command frame
enum SrsCodecVideoAVCFrame
{
    // set to the zero to reserved, for array map.
    SrsCodecVideoAVCFrameReserved = 0,
    SrsCodecVideoAVCFrameForbidden = 0,
    SrsCodecVideoAVCFrameReserved1 = 6,
    
    SrsCodecVideoAVCFrameKeyFrame = 1,
    SrsCodecVideoAVCFrameInterFrame = 2,
    SrsCodecVideoAVCFrameDisposableInterFrame = 3,
    SrsCodecVideoAVCFrameGeneratedKeyFrame = 4,
    SrsCodecVideoAVCFrameVideoInfoFrame = 5,
};

// AVCPacketType IF CodecID == 7 UI8
// The following values are defined:
//     0 = AVC sequence header
//     1 = AVC NALU
//     2 = AVC end of sequence (lower level NALU sequence ender is
//         not required or supported)
enum SrsCodecVideoAVCType
{
    // set to the max value to reserved, for array map.
    SrsCodecVideoAVCTypeReserved = 3,
    SrsCodecVideoAVCTypeForbidden = 3,
    
    SrsCodecVideoAVCTypeSequenceHeader = 0,
    SrsCodecVideoAVCTypeNALU = 1,
    SrsCodecVideoAVCTypeSequenceHeaderEOF = 2,
};

// E.4.3.1 VIDEODATA
// CodecID UB [4]
// Codec Identifier. The following values are defined:
//     2 = Sorenson H.263
//     3 = Screen video
//     4 = On2 VP6
//     5 = On2 VP6 with alpha channel
//     6 = Screen video version 2
//     7 = AVC
enum SrsCodecVideo
{
    // set to the zero to reserved, for array map.
    SrsCodecVideoReserved = 0,
    SrsCodecVideoForbidden = 0,
    SrsCodecVideoReserved1 = 1,
    SrsCodecVideoReserved2 = 9,
    
    // for user to disable video, for example, use pure audio hls.
    SrsCodecVideoDisabled = 8,
    
    SrsCodecVideoSorensonH263 = 2,
    SrsCodecVideoScreenVideo = 3,
    SrsCodecVideoOn2VP6 = 4,
    SrsCodecVideoOn2VP6WithAlphaChannel = 5,
    SrsCodecVideoScreenVideoVersion2 = 6,
    SrsCodecVideoAVC = 7,
};
std::string srs_codec_video2str(SrsCodecVideo codec);

// SoundFormat UB [4] 
// Format of SoundData. The following values are defined:
//     0 = Linear PCM, platform endian
//     1 = ADPCM
//     2 = MP3
//     3 = Linear PCM, little endian
//     4 = Nellymoser 16 kHz mono
//     5 = Nellymoser 8 kHz mono
//     6 = Nellymoser
//     7 = G.711 A-law logarithmic PCM
//     8 = G.711 mu-law logarithmic PCM
//     9 = reserved
//     10 = AAC
//     11 = Speex
//     14 = MP3 8 kHz
//     15 = Device-specific sound
// Formats 7, 8, 14, and 15 are reserved.
// AAC is supported in Flash Player 9,0,115,0 and higher.
// Speex is supported in Flash Player 10 and higher.
enum SrsCodecAudio
{
    // set to the max value to reserved, for array map.
    SrsCodecAudioReserved1 = 16,
    SrsCodecAudioForbidden = 16,
    
    // for user to disable audio, for example, use pure video hls.
    SrsCodecAudioDisabled = 17,
    
    SrsCodecAudioLinearPCMPlatformEndian = 0,
    SrsCodecAudioADPCM = 1,
    SrsCodecAudioMP3 = 2,
    SrsCodecAudioLinearPCMLittleEndian = 3,
    SrsCodecAudioNellymoser16kHzMono = 4,
    SrsCodecAudioNellymoser8kHzMono = 5,
    SrsCodecAudioNellymoser = 6,
    SrsCodecAudioReservedG711AlawLogarithmicPCM = 7,
    SrsCodecAudioReservedG711MuLawLogarithmicPCM = 8,
    SrsCodecAudioReserved = 9,
    SrsCodecAudioAAC = 10,
    SrsCodecAudioSpeex = 11,
    SrsCodecAudioReservedMP3_8kHz = 14,
    SrsCodecAudioReservedDeviceSpecificSound = 15,
};
std::string srs_codec_audio2str(SrsCodecAudio codec);

/**
* the FLV/RTMP supported audio sample rate.
* Sampling rate. The following values are defined:
* 0 = 5.5 kHz = 5512 Hz
* 1 = 11 kHz = 11025 Hz
* 2 = 22 kHz = 22050 Hz
* 3 = 44 kHz = 44100 Hz
*/
enum SrsCodecAudioSampleRate
{
    // set to the max value to reserved, for array map.
    SrsCodecAudioSampleRateReserved = 4,
    SrsCodecAudioSampleRateForbidden = 4,
    
    SrsCodecAudioSampleRate5512 = 0,
    SrsCodecAudioSampleRate11025 = 1,
    SrsCodecAudioSampleRate22050 = 2,
    SrsCodecAudioSampleRate44100 = 3,
};
std::string srs_codec_audio_samplerate2str(SrsCodecAudioSampleRate v);

/**
* E.4.1 FLV Tag, page 75
*/
enum SrsCodecFlvTag
{
    // set to the zero to reserved, for array map.
    SrsCodecFlvTagReserved = 0,
    SrsCodecFlvTagForbidden = 0,

    // 8 = audio
    SrsCodecFlvTagAudio = 8,
    // 9 = video
    SrsCodecFlvTagVideo = 9,
    // 18 = script data
    SrsCodecFlvTagScript = 18,
};

/**
* Annex E. The FLV File Format
* @see SrsAvcAacCodec for the media stream codec.
*/
class SrsFlvCodec
{
public:
    SrsFlvCodec();
    virtual ~SrsFlvCodec();
// the following function used to finger out the flv/rtmp packet detail.
public:
    /**
    * only check the frame_type, not check the codec type.
    */
    static bool video_is_keyframe(char* data, int size);
    /**
    * check codec h264, keyframe, sequence header
    */
    static bool video_is_sequence_header(char* data, int size);
    /**
    * check codec aac, sequence header
    */
    static bool audio_is_sequence_header(char* data, int size);
    /**
    * check codec h264.
    */
    static bool video_is_h264(char* data, int size);
    /**
    * check codec aac.
    */
    static bool audio_is_aac(char* data, int size);
    /**
     * check the video RTMP/flv header info,
     * @return true if video RTMP/flv header is ok.
     * @remark all type of audio is possible, no need to check audio.
     */
    static bool video_is_acceptable(char* data, int size);
};

/**
* the public data, event HLS disable, others can use it.
*/
/**
* the flv sample rate map
*/
extern int flv_sample_rates[];

/**
* the aac sample rate map
*/
extern int aac_sample_rates[];

// The impossible aac sample rate index.
#define SRS_AAC_SAMPLE_RATE_UNSET 15

// The max number of NALUs in a video, or aac frame in audio packet.
#define SRS_MAX_CODEC_SAMPLE 256

/**
* the FLV/RTMP supported audio sample size.
* Size of each audio sample. This parameter only pertains to
* uncompressed formats. Compressed formats always decode
* to 16 bits internally.
* 0 = 8-bit samples
* 1 = 16-bit samples
*/
enum SrsCodecAudioSampleSize
{
    // set to the max value to reserved, for array map.
    SrsCodecAudioSampleSizeReserved = 2,
    SrsCodecAudioSampleSizeForbidden = 2,
    
    SrsCodecAudioSampleSize8bit = 0,
    SrsCodecAudioSampleSize16bit = 1,
};
std::string srs_codec_audio_samplesize2str(SrsCodecAudioSampleSize v);

/**
* the FLV/RTMP supported audio sound type/channel.
* Mono or stereo sound
* 0 = Mono sound
* 1 = Stereo sound
*/
enum SrsCodecAudioSoundType
{
    // set to the max value to reserved, for array map.
    SrsCodecAudioSoundTypeReserved = 2,
    SrsCodecAudioSoundTypeForbidden = 2,
    
    SrsCodecAudioSoundTypeMono = 0,
    SrsCodecAudioSoundTypeStereo = 1,
};
std::string srs_codec_audio_channels2str(SrsCodecAudioSoundType v);

/**
 * Table 7-1 - NAL unit type codes, syntax element categories, and NAL unit type classes
 * ISO_IEC_14496-10-AVC-2012.pdf, page 83.
 */
enum SrsAvcNaluType
{
    // Unspecified
    SrsAvcNaluTypeReserved = 0,
    
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
std::string srs_codec_avc_nalu2str(SrsAvcNaluType nalu_type);

/**
* the codec sample unit.
* for h.264 video packet, a NALU is a sample unit.
* for aac raw audio packet, a NALU is the entire aac raw data.
* for sequence header, it's not a sample unit.
*/
class SrsCodecSampleUnit
{
public:
    /**
    * the sample bytes is directly ptr to packet bytes,
    * user should never use it when packet destroyed.
    */
    int size;
    char* bytes;
public:
    SrsCodecSampleUnit();
    virtual ~SrsCodecSampleUnit();
};

/**
* the samples in the flv audio/video packet.
* the sample used to analysis a video/audio packet,
* split the h.264 NALUs to buffers, or aac raw data to a buffer,
* and decode the video/audio specified infos.
* 
* the sample unit:
*       a video packet codec in h.264 contains many NALUs, each is a sample unit.
*       a audio packet codec in aac is a sample unit.
* @remark, the video/audio sequence header is not sample unit,
*       all sequence header stores as extra data, 
*       @see SrsAvcAacCodec.avc_extra_data and SrsAvcAacCodec.aac_extra_data
* @remark, user must clear all samples before decode a new video/audio packet.
*/
class SrsCodecSample
{
public:
    /**
    * each audio/video raw data packet will dumps to one or multiple buffers,
    * the buffers will write to hls and clear to reset.
    * generally, aac audio packet corresponding to one buffer,
    * where avc/h264 video packet may contains multiple buffer.
    */
    int nb_sample_units;
    SrsCodecSampleUnit sample_units[SRS_MAX_CODEC_SAMPLE];
public:
    /**
    * whether the sample is video sample which demux from video packet.
    */
    bool is_video;
    /**
    * CompositionTime, video_file_format_spec_v10_1.pdf, page 78.
    * cts = pts - dts, where dts = flvheader->timestamp.
    */
    int32_t cts;
public:
    // video specified
    SrsCodecVideoAVCFrame frame_type;
    SrsCodecVideoAVCType avc_packet_type;
    // whether sample_units contains IDR frame.
    bool has_idr;
    // Whether exists AUD NALU.
    bool has_aud;
    // Whether exists SPS/PPS NALU.
    bool has_sps_pps;
    // The first nalu type.
    SrsAvcNaluType first_nalu_type;
public:
    // audio specified
    SrsCodecAudio acodec;
    // audio aac specified.
    SrsCodecAudioSampleRate sound_rate;
    SrsCodecAudioSampleSize sound_size;
    SrsCodecAudioSoundType sound_type;
    SrsCodecAudioType aac_packet_type;
public:
    SrsCodecSample();
    virtual ~SrsCodecSample();
public:
    /**
    * clear all samples.
    * the sample units never copy the bytes, it directly use the ptr,
    * so when video/audio packet is destroyed, the sample must be clear.
    * in a word, user must clear sample before demux it.
    * @remark demux sample use SrsAvcAacCodec.audio_aac_demux or video_avc_demux.
    */
    void clear();
    /**
    * add the a sample unit, it's a h.264 NALU or aac raw data.
    * the sample unit directly use the ptr of packet bytes,
    * so user must never use sample unit when packet is destroyed.
    * in a word, user must clear sample before demux it.
    */
    int add_sample_unit(char* bytes, int size);
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
    
    // @see 7.1 Profiles, aac-iso-13818-7.pdf, page 40
    SrsAacProfileMain = 0,
    SrsAacProfileLC = 1,
    SrsAacProfileSSR = 2,
};
std::string srs_codec_aac_profile2str(SrsAacProfile aac_profile);

/**
* the aac object type, for RTMP sequence header
* for AudioSpecificConfig, @see ISO_IEC_14496-3-AAC-2001.pdf, page 33
* for audioObjectType, @see ISO_IEC_14496-3-AAC-2001.pdf, page 23
*/
enum SrsAacObjectType
{
    SrsAacObjectTypeReserved = 0,
    
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
std::string srs_codec_aac_object2str(SrsAacObjectType aac_object);
// ts/hls/adts audio header profile to RTMP sequence header object type.
SrsAacObjectType srs_codec_aac_ts2rtmp(SrsAacProfile profile);
// RTMP sequence header object type to ts/hls/adts audio header profile.
SrsAacProfile srs_codec_aac_rtmp2ts(SrsAacObjectType object_type);

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
std::string srs_codec_avc_profile2str(SrsAvcProfile profile);

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
std::string srs_codec_avc_level2str(SrsAvcLevel level);

#if !defined(SRS_EXPORT_LIBRTMP)

/**
* the h264/avc and aac codec, for media stream.
*
* to demux the FLV/RTMP video/audio packet to sample,
* add each NALUs of h.264 as a sample unit to sample,
* while the entire aac raw data as a sample unit.
*
* for sequence header,
* demux it and save it in the avc_extra_data and aac_extra_data,
* 
* for the codec info, such as audio sample rate,
* decode from FLV/RTMP header, then use codec info in sequence 
* header to override it.
*/
class SrsAvcAacCodec
{
private:
    SrsBuffer* stream;
public:
    /**
    * metadata specified
    */
    int             duration;
    int             width;
    int             height;
    int             frame_rate;
    // @see: SrsCodecVideo
    int             video_codec_id;
    int             video_data_rate; // in bps
    // @see: SrsCod ecAudioType
    int             audio_codec_id;
    int             audio_data_rate; // in bps
public:
    /**
    * video specified
    */
    // profile_idc, ISO_IEC_14496-10-AVC-2003.pdf, page 45.
    SrsAvcProfile   avc_profile; 
    // level_idc, ISO_IEC_14496-10-AVC-2003.pdf, page 45.
    SrsAvcLevel     avc_level; 
    // lengthSizeMinusOne, ISO_IEC_14496-15-AVC-format-2012.pdf, page 16
    int8_t          NAL_unit_length;
    uint16_t       sequenceParameterSetLength;
    char*           sequenceParameterSetNALUnit;
    uint16_t       pictureParameterSetLength;
    char*           pictureParameterSetNALUnit;
private:
    // the avc payload format.
    SrsAvcPayloadFormat payload_format;
public:
    /**
    * audio specified
    * audioObjectType, in 1.6.2.1 AudioSpecificConfig, page 33,
    * 1.5.1.1 Audio object type definition, page 23,
    *           in ISO_IEC_14496-3-AAC-2001.pdf.
    */
    SrsAacObjectType    aac_object;
    /**
    * samplingFrequencyIndex
    */
    uint8_t        aac_sample_rate;
    /**
    * channelConfiguration
    */
    uint8_t        aac_channels;
public:
    /**
    * the avc extra data, the AVC sequence header,
    * without the flv codec header,
    * @see: ffmpeg, AVCodecContext::extradata
    */
    int             avc_extra_size;
    char*           avc_extra_data;
    /**
    * the aac extra data, the AAC sequence header,
    * without the flv codec header,
    * @see: ffmpeg, AVCodecContext::extradata
    */
    int             aac_extra_size;
    char*           aac_extra_data;
public:
    // for sequence header, whether parse the h.264 sps.
    bool            avc_parse_sps;
public:
    SrsAvcAacCodec();
    virtual ~SrsAvcAacCodec();
public:
    // whether avc or aac codec sequence header or extra data is decoded ok.
    virtual bool is_avc_codec_ok();
    virtual bool is_aac_codec_ok();
// the following function used for hls to build the sample and codec.
public:
    /**
    * demux the audio packet in aac codec.
    * the packet mux in FLV/RTMP format defined in flv specification.
    * demux the audio speicified data(sound_format, sound_size, ...) to sample.
    * demux the aac specified data(aac_profile, ...) to codec from sequence header.
    * demux the aac raw to sample units.
    */
    virtual int audio_aac_demux(char* data, int size, SrsCodecSample* sample);
    virtual int audio_mp3_demux(char* data, int size, SrsCodecSample* sample);
    /**
    * demux the video packet in h.264 codec.
    * the packet mux in FLV/RTMP format defined in flv specification.
    * demux the video specified data(frame_type, codec_id, ...) to sample.
    * demux the h.264 sepcified data(avc_profile, ...) to codec from sequence header.
    * demux the h.264 NALUs to sampe units.
    */
    virtual int video_avc_demux(char* data, int size, SrsCodecSample* sample);
private:
    virtual int video_nalu_demux(SrsBuffer* stream, SrsCodecSample* sample);
public:
    /**
    * directly demux the sequence header, without RTMP packet header.
    */
    virtual int audio_aac_sequence_header_demux(char* data, int size);
private:
    /**
    * when avc packet type is SrsCodecVideoAVCTypeSequenceHeader,
    * decode the sps and pps.
    */
    virtual int avc_demux_sps_pps(SrsBuffer* stream);
    /**
     * decode the sps rbsp stream.
     */
    virtual int avc_demux_sps();
    virtual int avc_demux_sps_rbsp(char* rbsp, int nb_rbsp);
    /**
    * demux the avc NALU in "AnnexB" 
    * from ISO_IEC_14496-10-AVC-2003.pdf, page 211.
    */
    virtual int avc_demux_annexb_format(SrsBuffer* stream, SrsCodecSample* sample);
    /**
    * demux the avc NALU in "ISO Base Media File Format" 
    * from ISO_IEC_14496-15-AVC-format-2012.pdf, page 20
    */
    virtual int avc_demux_ibmf_format(SrsBuffer* stream, SrsCodecSample* sample);
};

#endif

#endif

