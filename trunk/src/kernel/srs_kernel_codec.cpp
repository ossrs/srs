//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_kernel_codec.hpp>

#include <algorithm>
#include <stdlib.h>
#include <string.h>
using namespace std;

#include <srs_core_autofree.hpp>
#include <srs_kernel_buffer.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_kbps.hpp>
#include <srs_kernel_log.hpp>
#include <srs_kernel_rtc_rtp.hpp>
#include <srs_kernel_utility.hpp>

srs_error_t srs_avc_nalu_read_uev(SrsBitBuffer *stream, int32_t &v)
{
    srs_error_t err = srs_success;

    if (stream->empty()) {
        return srs_error_new(ERROR_AVC_NALU_UEV, "empty stream");
    }

    // ue(v) in 9.1 Parsing process for Exp-Golomb codes
    // ISO_IEC_14496-10-AVC-2012.pdf, page 227.
    // Syntax elements coded as ue(v), me(v), or se(v) are Exp-Golomb-coded.
    //      leadingZeroBits = -1;
    //      for( b = 0; !b; leadingZeroBits++ )
    //          b = read_bits( 1 )
    // The variable codeNum is then assigned as follows:
    //      codeNum = (2<<leadingZeroBits) - 1 + read_bits( leadingZeroBits )
    int leadingZeroBits = -1;
    for (int8_t b = 0; !b && !stream->empty(); leadingZeroBits++) {
        b = stream->read_bit();
    }

    if (leadingZeroBits >= 31) {
        return srs_error_new(ERROR_AVC_NALU_UEV, "%dbits overflow 31bits", leadingZeroBits);
    }

    v = (1 << leadingZeroBits) - 1;
    for (int i = 0; i < (int)leadingZeroBits; i++) {
        if (stream->empty()) {
            return srs_error_new(ERROR_AVC_NALU_UEV, "no bytes for leadingZeroBits=%d", leadingZeroBits);
        }

        int32_t b = stream->read_bit();
        v += b << (leadingZeroBits - 1 - i);
    }

    return err;
}

srs_error_t srs_avc_nalu_read_bit(SrsBitBuffer *stream, int8_t &v)
{
    srs_error_t err = srs_success;

    if (stream->empty()) {
        return srs_error_new(ERROR_AVC_NALU_UEV, "empty stream");
    }

    v = stream->read_bit();

    return err;
}

string srs_video_codec_id2str(SrsVideoCodecId codec)
{
    switch (codec) {
    case SrsVideoCodecIdAVC:
        return "H264";
    case SrsVideoCodecIdOn2VP6:
    case SrsVideoCodecIdOn2VP6WithAlphaChannel:
        return "VP6";
    case SrsVideoCodecIdHEVC:
        return "HEVC";
    case SrsVideoCodecIdAV1:
        return "AV1";
    case SrsVideoCodecIdReserved:
    case SrsVideoCodecIdReserved1:
    case SrsVideoCodecIdReserved2:
    case SrsVideoCodecIdDisabled:
    case SrsVideoCodecIdSorensonH263:
    case SrsVideoCodecIdScreenVideo:
    case SrsVideoCodecIdScreenVideoVersion2:
    default:
        return "Other";
    }
}

SrsVideoCodecId srs_video_codec_str2id(const std::string &codec)
{
    std::string upper_codec = codec;
    std::transform(upper_codec.begin(), upper_codec.end(), upper_codec.begin(), ::toupper);

    if (upper_codec == "H264" || upper_codec == "AVC") {
        return SrsVideoCodecIdAVC;
    } else if (upper_codec == "H265" || upper_codec == "HEVC") {
        return SrsVideoCodecIdHEVC;
    } else if (upper_codec == "AV1") {
        return SrsVideoCodecIdAV1;
    } else if (upper_codec == "VP6") {
        return SrsVideoCodecIdOn2VP6;
    } else if (upper_codec == "VP6A") {
        return SrsVideoCodecIdOn2VP6WithAlphaChannel;
    }

    return SrsVideoCodecIdReserved;
}

string srs_audio_codec_id2str(SrsAudioCodecId codec)
{
    switch (codec) {
    case SrsAudioCodecIdAAC:
        return "AAC";
    case SrsAudioCodecIdMP3:
        return "MP3";
    case SrsAudioCodecIdOpus:
        return "Opus";
    case SrsAudioCodecIdReserved1:
    case SrsAudioCodecIdLinearPCMPlatformEndian:
    case SrsAudioCodecIdADPCM:
    case SrsAudioCodecIdLinearPCMLittleEndian:
    case SrsAudioCodecIdNellymoser16kHzMono:
    case SrsAudioCodecIdNellymoser8kHzMono:
    case SrsAudioCodecIdNellymoser:
    case SrsAudioCodecIdReservedG711AlawLogarithmicPCM:
    case SrsAudioCodecIdReservedG711MuLawLogarithmicPCM:
    case SrsAudioCodecIdReserved:
    case SrsAudioCodecIdSpeex:
    case SrsAudioCodecIdReservedMP3_8kHz:
    case SrsAudioCodecIdReservedDeviceSpecificSound:
    default:
        return "Other";
    }
}

SrsAudioCodecId srs_audio_codec_str2id(const std::string &codec)
{
    // to uppercase
    std::string upper_codec = codec;
    std::transform(upper_codec.begin(), upper_codec.end(), upper_codec.begin(), ::toupper);

    if (upper_codec == "AAC") {
        return SrsAudioCodecIdAAC;
    } else if (upper_codec == "MP3") {
        return SrsAudioCodecIdMP3;
    } else if (upper_codec == "OPUS") {
        return SrsAudioCodecIdOpus;
    } else if (upper_codec == "SPEEX") {
        return SrsAudioCodecIdSpeex;
    }

    return SrsAudioCodecIdReserved1;
}

SrsAudioSampleRate srs_audio_sample_rate_from_number(uint32_t v)
{
    if (v == 5512)
        return SrsAudioSampleRate5512;
    if (v == 11025)
        return SrsAudioSampleRate11025;
    if (v == 22050)
        return SrsAudioSampleRate22050;
    if (v == 44100)
        return SrsAudioSampleRate44100;

    if (v == 12000)
        return SrsAudioSampleRate12000;
    if (v == 24000)
        return SrsAudioSampleRate24000;
    if (v == 48000)
        return SrsAudioSampleRate48000;

    if (v == 8000)
        return SrsAudioSampleRateNB8kHz;
    if (v == 12000)
        return SrsAudioSampleRateMB12kHz;
    if (v == 16000)
        return SrsAudioSampleRateWB16kHz;
    if (v == 24000)
        return SrsAudioSampleRateSWB24kHz;
    if (v == 48000)
        return SrsAudioSampleRateFB48kHz;

    return SrsAudioSampleRateForbidden;
}

SrsAudioSampleRate srs_audio_sample_rate_guess_number(uint32_t v)
{
    if (v >= 48000)
        return SrsAudioSampleRate48000;
    if (v >= 44100)
        return SrsAudioSampleRate44100;
    if (v >= 24000)
        return SrsAudioSampleRate24000;
    if (v >= 24000)
        return SrsAudioSampleRate24000;
    if (v >= 22050)
        return SrsAudioSampleRate22050;
    if (v >= 16000)
        return SrsAudioSampleRateWB16kHz;
    if (v >= 12000)
        return SrsAudioSampleRate12000;
    if (v >= 8000)
        return SrsAudioSampleRateNB8kHz;
    if (v >= 5512)
        return SrsAudioSampleRate5512;

    return SrsAudioSampleRateForbidden;
}

uint32_t srs_audio_sample_rate2number(SrsAudioSampleRate v)
{
    if (v == SrsAudioSampleRate5512)
        return 5512;
    if (v == SrsAudioSampleRate11025)
        return 11025;
    if (v == SrsAudioSampleRate22050)
        return 22050;
    if (v == SrsAudioSampleRate44100)
        return 44100;

    if (v == SrsAudioSampleRate12000)
        return 12000;
    if (v == SrsAudioSampleRate24000)
        return 24000;
    if (v == SrsAudioSampleRate48000)
        return 48000;

    if (v == SrsAudioSampleRateNB8kHz)
        return 8000;
    if (v == SrsAudioSampleRateMB12kHz)
        return 12000;
    if (v == SrsAudioSampleRateWB16kHz)
        return 16000;
    if (v == SrsAudioSampleRateSWB24kHz)
        return 24000;
    if (v == SrsAudioSampleRateFB48kHz)
        return 48000;

    return 0;
}

string srs_audio_sample_rate2str(SrsAudioSampleRate v)
{
    switch (v) {
    case SrsAudioSampleRate5512:
        return "5512";
    case SrsAudioSampleRate11025:
        return "11025";
    case SrsAudioSampleRate22050:
        return "22050";
    case SrsAudioSampleRate44100:
        return "44100";
    case SrsAudioSampleRateNB8kHz:
        return "NB8kHz";
    case SrsAudioSampleRateMB12kHz:
        return "MB12kHz";
    case SrsAudioSampleRateWB16kHz:
        return "WB16kHz";
    case SrsAudioSampleRateSWB24kHz:
        return "SWB24kHz";
    case SrsAudioSampleRateFB48kHz:
        return "FB48kHz";
    default:
        return "Other";
    }
}

SrsFlvVideo::SrsFlvVideo()
{
}

SrsFlvVideo::~SrsFlvVideo()
{
}

bool SrsFlvVideo::keyframe(char *data, int size)
{
    // 2bytes required.
    if (size < 1) {
        return false;
    }

    // See rtmp_specification_1.0.pdf
    // See https://github.com/veovera/enhanced-rtmp
    uint8_t frame_type = data[0] & 0x7f;
    frame_type = (frame_type >> 4) & 0x0F;

    return frame_type == SrsVideoAvcFrameTypeKeyFrame;
}

bool SrsFlvVideo::sh(char *data, int size)
{
    // Check sequence header only for H.264 or H.265
    bool codec_ok = h264(data, size);
    codec_ok = codec_ok ? true : hevc(data, size);
    if (!codec_ok)
        return false;

    // 2bytes required.
    if (size < 2) {
        return false;
    }

    uint8_t frame_type = data[0];
    bool is_ext_header = frame_type & 0x80;
    SrsVideoAvcFrameTrait avc_packet_type = SrsVideoAvcFrameTraitForbidden;
    if (!is_ext_header) {
        // See rtmp_specification_1.0.pdf
        frame_type = (frame_type >> 4) & 0x0F;
        avc_packet_type = (SrsVideoAvcFrameTrait)data[1];
    } else {
        // See https://github.com/veovera/enhanced-rtmp
        avc_packet_type = (SrsVideoAvcFrameTrait)(frame_type & 0x0f);
        frame_type = (frame_type >> 4) & 0x07;
    }

    // Note that SrsVideoHEVCFrameTraitPacketTypeSequenceStart is equal to SrsVideoAvcFrameTraitSequenceHeader
    return frame_type == SrsVideoAvcFrameTypeKeyFrame && avc_packet_type == SrsVideoAvcFrameTraitSequenceHeader;
}

bool SrsFlvVideo::h264(char *data, int size)
{
    // 1bytes required.
    if (size < 1) {
        return false;
    }

    char codec_id = data[0];
    codec_id = codec_id & 0x0F;

    return codec_id == SrsVideoCodecIdAVC;
}

bool SrsFlvVideo::hevc(char *data, int size)
{
    // 1bytes required.
    if (size < 1) {
        return false;
    }

    uint8_t frame_type = data[0];
    bool is_ext_header = frame_type & 0x80;
    SrsVideoCodecId codec_id = SrsVideoCodecIdForbidden;
    if (!is_ext_header) {
        // See rtmp_specification_1.0.pdf
        codec_id = (SrsVideoCodecId)(frame_type & 0x0F);
    } else {
        // See https://github.com/veovera/enhanced-rtmp
        if (size < 5) {
            return false;
        }

        // Video FourCC
        if (data[1] != 'h' || data[2] != 'v' || data[3] != 'c' || data[4] != '1') {
            return false;
        }
        codec_id = SrsVideoCodecIdHEVC;
    }

    return codec_id == SrsVideoCodecIdHEVC;
}

bool SrsFlvVideo::acceptable(char *data, int size)
{
    // 1bytes required.
    if (size < 1) {
        return false;
    }

    uint8_t frame_type = data[0];
    bool is_ext_header = frame_type & 0x80;
    SrsVideoCodecId codec_id = SrsVideoCodecIdForbidden;
    if (!is_ext_header) {
        // See rtmp_specification_1.0.pdf
        codec_id = (SrsVideoCodecId)(frame_type & 0x0f);
        frame_type = (frame_type >> 4) & 0x0f;

        if (frame_type < 1 || frame_type > 5) {
            return false;
        }
    } else {
        // See https://github.com/veovera/enhanced-rtmp
        uint8_t packet_type = frame_type & 0x0f;
        frame_type = (frame_type >> 4) & 0x07;

        if (packet_type > SrsVideoHEVCFrameTraitPacketTypeMPEG2TSSequenceStart || frame_type > SrsVideoAvcFrameTypeVideoInfoFrame) {
            return false;
        }

        if (size < 5) {
            return false;
        }

        if (data[1] != 'h' || data[2] != 'v' || data[3] != 'c' || data[4] != '1') {
            return false;
        }
        codec_id = SrsVideoCodecIdHEVC;
    }

    if (codec_id != SrsVideoCodecIdAVC && codec_id != SrsVideoCodecIdAV1 && codec_id != SrsVideoCodecIdHEVC) {
        return false;
    }

    return true;
}

SrsFlvAudio::SrsFlvAudio()
{
}

SrsFlvAudio::~SrsFlvAudio()
{
}

bool SrsFlvAudio::sh(char *data, int size)
{
    // sequence header only for aac
    if (!aac(data, size)) {
        return false;
    }

    // 2bytes required.
    if (size < 2) {
        return false;
    }

    char aac_packet_type = data[1];

    return aac_packet_type == SrsAudioAacFrameTraitSequenceHeader;
}

bool SrsFlvAudio::aac(char *data, int size)
{
    // 1bytes required.
    if (size < 1) {
        return false;
    }

    char sound_format = data[0];
    sound_format = (sound_format >> 4) & 0x0F;

    return sound_format == SrsAudioCodecIdAAC;
}

/**
 * the public data, event HLS disable, others can use it.
 */
// 0 = 5.5 kHz = 5512 Hz
// 1 = 11 kHz = 11025 Hz
// 2 = 22 kHz = 22050 Hz
// 3 = 44 kHz = 44100 Hz
int srs_flv_srates[] = {5512, 11025, 22050, 44100, 0};

// the sample rates in the codec,
// in the sequence header.
int srs_aac_srates[] =
    {
        96000, 88200, 64000, 48000,
        44100, 32000, 24000, 22050,
        16000, 12000, 11025, 8000,
        7350, 0, 0, 0};

string srs_audio_sample_bits2str(SrsAudioSampleBits v)
{
    switch (v) {
    case SrsAudioSampleBits16bit:
        return "16bits";
    case SrsAudioSampleBits8bit:
        return "8bits";
    default:
        return "Other";
    }
}

string srs_audio_channels2str(SrsAudioChannels v)
{
    switch (v) {
    case SrsAudioChannelsStereo:
        return "Stereo";
    case SrsAudioChannelsMono:
        return "Mono";
    default:
        return "Other";
    }
}

string srs_avc_nalu2str(SrsAvcNaluType nalu_type)
{
    switch (nalu_type) {
    case SrsAvcNaluTypeNonIDR:
        return "NonIDR";
    case SrsAvcNaluTypeDataPartitionA:
        return "DataPartitionA";
    case SrsAvcNaluTypeDataPartitionB:
        return "DataPartitionB";
    case SrsAvcNaluTypeDataPartitionC:
        return "DataPartitionC";
    case SrsAvcNaluTypeIDR:
        return "IDR";
    case SrsAvcNaluTypeSEI:
        return "SEI";
    case SrsAvcNaluTypeSPS:
        return "SPS";
    case SrsAvcNaluTypePPS:
        return "PPS";
    case SrsAvcNaluTypeAccessUnitDelimiter:
        return "AccessUnitDelimiter";
    case SrsAvcNaluTypeEOSequence:
        return "EOSequence";
    case SrsAvcNaluTypeEOStream:
        return "EOStream";
    case SrsAvcNaluTypeFilterData:
        return "FilterData";
    case SrsAvcNaluTypeSPSExt:
        return "SPSExt";
    case SrsAvcNaluTypePrefixNALU:
        return "PrefixNALU";
    case SrsAvcNaluTypeSubsetSPS:
        return "SubsetSPS";
    case SrsAvcNaluTypeLayerWithoutPartition:
        return "LayerWithoutPartition";
    case SrsAvcNaluTypeCodedSliceExt:
        return "CodedSliceExt";
    case SrsAvcNaluTypeReserved:
    default:
        return "Other";
    }
}

string srs_aac_profile2str(SrsAacProfile aac_profile)
{
    switch (aac_profile) {
    case SrsAacProfileMain:
        return "Main";
    case SrsAacProfileLC:
        return "LC";
    case SrsAacProfileSSR:
        return "SSR";
    default:
        return "Other";
    }
}

string srs_aac_object2str(SrsAacObjectType aac_object)
{
    switch (aac_object) {
    case SrsAacObjectTypeAacMain:
        return "Main";
    case SrsAacObjectTypeAacHE:
        return "HE";
    case SrsAacObjectTypeAacHEV2:
        return "HEv2";
    case SrsAacObjectTypeAacLC:
        return "LC";
    case SrsAacObjectTypeAacSSR:
        return "SSR";
    default:
        return "Other";
    }
}

SrsAacObjectType srs_aac_ts2rtmp(SrsAacProfile profile)
{
    switch (profile) {
    case SrsAacProfileMain:
        return SrsAacObjectTypeAacMain;
    case SrsAacProfileLC:
        return SrsAacObjectTypeAacLC;
    case SrsAacProfileSSR:
        return SrsAacObjectTypeAacSSR;
    default:
        return SrsAacObjectTypeReserved;
    }
}

SrsAacProfile srs_aac_rtmp2ts(SrsAacObjectType object_type)
{
    switch (object_type) {
    case SrsAacObjectTypeAacMain:
        return SrsAacProfileMain;
    case SrsAacObjectTypeAacHE:
    case SrsAacObjectTypeAacHEV2:
    case SrsAacObjectTypeAacLC:
        return SrsAacProfileLC;
    case SrsAacObjectTypeAacSSR:
        return SrsAacProfileSSR;
    default:
        return SrsAacProfileReserved;
    }
}

string srs_avc_profile2str(SrsAvcProfile profile)
{
    switch (profile) {
    case SrsAvcProfileBaseline:
        return "Baseline";
    case SrsAvcProfileConstrainedBaseline:
        return "Baseline(Constrained)";
    case SrsAvcProfileMain:
        return "Main";
    case SrsAvcProfileExtended:
        return "Extended";
    case SrsAvcProfileHigh:
        return "High";
    case SrsAvcProfileHigh10:
        return "High(10)";
    case SrsAvcProfileHigh10Intra:
        return "High(10+Intra)";
    case SrsAvcProfileHigh422:
        return "High(422)";
    case SrsAvcProfileHigh422Intra:
        return "High(422+Intra)";
    case SrsAvcProfileHigh444:
        return "High(444)";
    case SrsAvcProfileHigh444Predictive:
        return "High(444+Predictive)";
    case SrsAvcProfileHigh444Intra:
        return "High(444+Intra)";
    default:
        return "Other";
    }
}

string srs_avc_level2str(SrsAvcLevel level)
{
    switch (level) {
    case SrsAvcLevel_1:
        return "1";
    case SrsAvcLevel_11:
        return "1.1";
    case SrsAvcLevel_12:
        return "1.2";
    case SrsAvcLevel_13:
        return "1.3";
    case SrsAvcLevel_2:
        return "2";
    case SrsAvcLevel_21:
        return "2.1";
    case SrsAvcLevel_22:
        return "2.2";
    case SrsAvcLevel_3:
        return "3";
    case SrsAvcLevel_31:
        return "3.1";
    case SrsAvcLevel_32:
        return "3.2";
    case SrsAvcLevel_4:
        return "4";
    case SrsAvcLevel_41:
        return "4.1";
    case SrsAvcLevel_5:
        return "5";
    case SrsAvcLevel_51:
        return "5.1";
    default:
        return "Other";
    }
}

string srs_hevc_profile2str(SrsHevcProfile profile)
{
    switch (profile) {
    case SrsHevcProfileMain:
        return "Main";
    case SrsHevcProfileMain10:
        return "Main10";
    case SrsHevcProfileMainStillPicture:
        return "Main Still Picture";
    case SrsHevcProfileRext:
        return "Rext";
    default:
        return "Other";
    }
}

string srs_hevc_level2str(SrsHevcLevel level)
{
    switch (level) {
    case SrsHevcLevel_1:
        return "1";
    case SrsHevcLevel_2:
        return "2";
    case SrsHevcLevel_21:
        return "2.1";
    case SrsHevcLevel_3:
        return "3";
    case SrsHevcLevel_31:
        return "3.1";
    case SrsHevcLevel_4:
        return "4";
    case SrsHevcLevel_41:
        return "4.1";
    case SrsHevcLevel_5:
        return "5";
    case SrsHevcLevel_51:
        return "5.1";
    case SrsHevcLevel_52:
        return "5.2";
    case SrsHevcLevel_6:
        return "6";
    case SrsHevcLevel_61:
        return "6.1";
    case SrsHevcLevel_62:
        return "6.2";
    default:
        return "Other";
    }
}

SrsCodecConfig::SrsCodecConfig()
{
}

SrsCodecConfig::~SrsCodecConfig()
{
}

SrsAudioCodecConfig::SrsAudioCodecConfig()
{
    id = SrsAudioCodecIdForbidden;
    sound_rate = SrsAudioSampleRateForbidden;
    sound_size = SrsAudioSampleBitsForbidden;
    sound_type = SrsAudioChannelsForbidden;

    audio_data_rate = 0;

    aac_object = SrsAacObjectTypeForbidden;
    aac_sample_rate = SrsAacSampleRateUnset; // sample rate ignored
    aac_channels = 0;
}

SrsAudioCodecConfig::~SrsAudioCodecConfig()
{
}

bool SrsAudioCodecConfig::is_aac_codec_ok()
{
    return !aac_extra_data.empty();
}

SrsVideoCodecConfig::SrsVideoCodecConfig()
{
    id = SrsVideoCodecIdForbidden;
    video_data_rate = 0;
    frame_rate = duration = 0;

    width = 0;
    height = 0;

    NAL_unit_length = 0;
    avc_profile = SrsAvcProfileReserved;
    avc_level = SrsAvcLevelReserved;

    payload_format = SrsAvcPayloadFormatGuess;
}

SrsVideoCodecConfig::~SrsVideoCodecConfig()
{
}

bool SrsVideoCodecConfig::is_avc_codec_ok()
{
    return !avc_extra_data.empty();
}

bool srs_avc_startswith_annexb(SrsBuffer *stream, int *pnb_start_code)
{
    if (!stream) {
        return false;
    }

    char *bytes = stream->data() + stream->pos();
    char *p = bytes;

    for (;;) {
        if (!stream->require((int)(p - bytes + 3))) {
            return false;
        }

        // not match
        if (p[0] != (char)0x00 || p[1] != (char)0x00) {
            return false;
        }

        // match N[00] 00 00 01, where N>=0
        if (p[2] == (char)0x01) {
            if (pnb_start_code) {
                *pnb_start_code = (int)(p - bytes) + 3;
            }
            return true;
        }

        p++;
    }

    return false;
}
