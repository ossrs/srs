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

#include <srs_kernel_codec.hpp>

#include <string.h>
#include <stdlib.h>
using namespace std;

#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_kernel_buffer.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_core_autofree.hpp>

string srs_video_codec_id2str(SrsVideoCodecId codec)
{
    switch (codec) {
        case SrsVideoCodecIdAVC:
            return "H264";
        case SrsVideoCodecIdOn2VP6:
        case SrsVideoCodecIdOn2VP6WithAlphaChannel:
            return "VP6";
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

string srs_audio_codec_id2str(SrsAudioCodecId codec)
{
    switch (codec) {
        case SrsAudioCodecIdAAC:
            return "AAC";
        case SrsAudioCodecIdMP3:
            return "MP3";
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

string srs_audio_sample_rate2str(SrsAudioSampleRate v)
{
    switch (v) {
        case SrsAudioSampleRate5512: return "5512";
        case SrsAudioSampleRate11025: return "11025";
        case SrsAudioSampleRate22050: return "22050";
        case SrsAudioSampleRate44100: return "44100";
        default: return "Other";
    }
}

SrsFlvVideo::SrsFlvVideo()
{
}

SrsFlvVideo::~SrsFlvVideo()
{
}

bool SrsFlvVideo::keyframe(char* data, int size)
{
    // 2bytes required.
    if (size < 1) {
        return false;
    }
    
    char frame_type = data[0];
    frame_type = (frame_type >> 4) & 0x0F;
    
    return frame_type == SrsVideoAvcFrameTypeKeyFrame;
}

bool SrsFlvVideo::sh(char* data, int size)
{
    // sequence header only for h264
    if (!h264(data, size)) {
        return false;
    }
    
    // 2bytes required.
    if (size < 2) {
        return false;
    }
    
    char frame_type = data[0];
    frame_type = (frame_type >> 4) & 0x0F;
    
    char avc_packet_type = data[1];
    
    return frame_type == SrsVideoAvcFrameTypeKeyFrame
    && avc_packet_type == SrsVideoAvcFrameTraitSequenceHeader;
}

bool SrsFlvVideo::h264(char* data, int size)
{
    // 1bytes required.
    if (size < 1) {
        return false;
    }
    
    char codec_id = data[0];
    codec_id = codec_id & 0x0F;
    
    return codec_id == SrsVideoCodecIdAVC;
}

bool SrsFlvVideo::acceptable(char* data, int size)
{
    // 1bytes required.
    if (size < 1) {
        return false;
    }
    
    char frame_type = data[0];
    char codec_id = frame_type & 0x0f;
    frame_type = (frame_type >> 4) & 0x0f;
    
    if (frame_type < 1 || frame_type > 5) {
        return false;
    }
    
    if (codec_id < 2 || codec_id > 7) {
        return false;
    }
    
    return true;
}

bool SrsFlvAudio::sh(char* data, int size)
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

bool SrsFlvAudio::aac(char* data, int size)
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
int srs_flv_srates[] = {5512, 11025, 22050, 44100};

// the sample rates in the codec,
// in the sequence header.
int srs_aac_srates[] =
{
    96000, 88200, 64000, 48000,
    44100, 32000, 24000, 22050,
    16000, 12000, 11025,  8000,
    7350,     0,     0,    0
};

string srs_audio_sample_bits2str(SrsAudioSampleBits v)
{
    switch (v) {
        case SrsAudioSampleBits16bit: return "16bits";
        case SrsAudioSampleBits8bit: return "8bits";
        default: return "Other";
    }
}

string srs_audio_channels2str(SrsAudioChannels v)
{
    switch (v) {
        case SrsAudioChannelsStereo: return "Stereo";
        case SrsAudioChannelsMono: return "Mono";
        default: return "Other";
    }
}

string srs_avc_nalu2str(SrsAvcNaluType nalu_type)
{
    switch (nalu_type) {
        case SrsAvcNaluTypeNonIDR: return "NonIDR";
        case SrsAvcNaluTypeDataPartitionA: return "DataPartitionA";
        case SrsAvcNaluTypeDataPartitionB: return "DataPartitionB";
        case SrsAvcNaluTypeDataPartitionC: return "DataPartitionC";
        case SrsAvcNaluTypeIDR: return "IDR";
        case SrsAvcNaluTypeSEI: return "SEI";
        case SrsAvcNaluTypeSPS: return "SPS";
        case SrsAvcNaluTypePPS: return "PPS";
        case SrsAvcNaluTypeAccessUnitDelimiter: return "AccessUnitDelimiter";
        case SrsAvcNaluTypeEOSequence: return "EOSequence";
        case SrsAvcNaluTypeEOStream: return "EOStream";
        case SrsAvcNaluTypeFilterData: return "FilterData";
        case SrsAvcNaluTypeSPSExt: return "SPSExt";
        case SrsAvcNaluTypePrefixNALU: return "PrefixNALU";
        case SrsAvcNaluTypeSubsetSPS: return "SubsetSPS";
        case SrsAvcNaluTypeLayerWithoutPartition: return "LayerWithoutPartition";
        case SrsAvcNaluTypeCodedSliceExt: return "CodedSliceExt";
        case SrsAvcNaluTypeReserved: default: return "Other";
    }
}

string srs_aac_profile2str(SrsAacProfile aac_profile)
{
    switch (aac_profile) {
        case SrsAacProfileMain: return "Main";
        case SrsAacProfileLC: return "LC";
        case SrsAacProfileSSR: return "SSR";
        default: return "Other";
    }
}

string srs_aac_object2str(SrsAacObjectType aac_object)
{
    switch (aac_object) {
        case SrsAacObjectTypeAacMain: return "Main";
        case SrsAacObjectTypeAacHE: return "HE";
        case SrsAacObjectTypeAacHEV2: return "HEv2";
        case SrsAacObjectTypeAacLC: return "LC";
        case SrsAacObjectTypeAacSSR: return "SSR";
        default: return "Other";
    }
}

SrsAacObjectType srs_aac_ts2rtmp(SrsAacProfile profile)
{
    switch (profile) {
        case SrsAacProfileMain: return SrsAacObjectTypeAacMain;
        case SrsAacProfileLC: return SrsAacObjectTypeAacLC;
        case SrsAacProfileSSR: return SrsAacObjectTypeAacSSR;
        default: return SrsAacObjectTypeReserved;
    }
}

SrsAacProfile srs_aac_rtmp2ts(SrsAacObjectType object_type)
{
    switch (object_type) {
        case SrsAacObjectTypeAacMain: return SrsAacProfileMain;
        case SrsAacObjectTypeAacHE:
        case SrsAacObjectTypeAacHEV2:
        case SrsAacObjectTypeAacLC: return SrsAacProfileLC;
        case SrsAacObjectTypeAacSSR: return SrsAacProfileSSR;
        default: return SrsAacProfileReserved;
    }
}

string srs_avc_profile2str(SrsAvcProfile profile)
{
    switch (profile) {
        case SrsAvcProfileBaseline: return "Baseline";
        case SrsAvcProfileConstrainedBaseline: return "Baseline(Constrained)";
        case SrsAvcProfileMain: return "Main";
        case SrsAvcProfileExtended: return "Extended";
        case SrsAvcProfileHigh: return "High";
        case SrsAvcProfileHigh10: return "High(10)";
        case SrsAvcProfileHigh10Intra: return "High(10+Intra)";
        case SrsAvcProfileHigh422: return "High(422)";
        case SrsAvcProfileHigh422Intra: return "High(422+Intra)";
        case SrsAvcProfileHigh444: return "High(444)";
        case SrsAvcProfileHigh444Predictive: return "High(444+Predictive)";
        case SrsAvcProfileHigh444Intra: return "High(444+Intra)";
        default: return "Other";
    }
}

string srs_avc_level2str(SrsAvcLevel level)
{
    switch (level) {
        case SrsAvcLevel_1: return "1";
        case SrsAvcLevel_11: return "1.1";
        case SrsAvcLevel_12: return "1.2";
        case SrsAvcLevel_13: return "1.3";
        case SrsAvcLevel_2: return "2";
        case SrsAvcLevel_21: return "2.1";
        case SrsAvcLevel_22: return "2.2";
        case SrsAvcLevel_3: return "3";
        case SrsAvcLevel_31: return "3.1";
        case SrsAvcLevel_32: return "3.2";
        case SrsAvcLevel_4: return "4";
        case SrsAvcLevel_41: return "4.1";
        case SrsAvcLevel_5: return "5";
        case SrsAvcLevel_51: return "5.1";
        default: return "Other";
    }
}

SrsSample::SrsSample()
{
    size = 0;
    bytes = NULL;
}

SrsSample::~SrsSample()
{
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
    aac_extra_size = 0;
    aac_extra_data = NULL;
}

SrsAudioCodecConfig::~SrsAudioCodecConfig()
{
}

bool SrsAudioCodecConfig::is_aac_codec_ok()
{
    return aac_extra_size > 0 && aac_extra_data;
}

SrsVideoCodecConfig::SrsVideoCodecConfig()
{
    id = SrsVideoCodecIdForbidden;
    video_data_rate = 0;
    frame_rate = duration = 0;
    
    width = 0;
    height = 0;
    
    avc_extra_size = 0;
    avc_extra_data = NULL;
    
    NAL_unit_length = 0;
    avc_profile = SrsAvcProfileReserved;
    avc_level = SrsAvcLevelReserved;
    sequenceParameterSetLength = 0;
    sequenceParameterSetNALUnit = NULL;
    pictureParameterSetLength = 0;
    pictureParameterSetNALUnit = NULL;
    
    payload_format = SrsAvcPayloadFormatGuess;
}

SrsVideoCodecConfig::~SrsVideoCodecConfig()
{
    srs_freepa(avc_extra_data);
    srs_freepa(sequenceParameterSetNALUnit);
    srs_freepa(pictureParameterSetNALUnit);
}

bool SrsVideoCodecConfig::is_avc_codec_ok()
{
    return avc_extra_size > 0 && avc_extra_data;
}

SrsFrame::SrsFrame()
{
    codec = NULL;
    nb_samples = 0;
    dts = 0;
    cts = 0;
}

SrsFrame::~SrsFrame()
{
    srs_freep(codec);
}

int SrsFrame::initialize(SrsCodecConfig* c)
{
    codec = c;
    nb_samples = 0;
    dts = 0;
    cts = 0;
    return ERROR_SUCCESS;
}

int SrsFrame::add_sample(char* bytes, int size)
{
    int ret = ERROR_SUCCESS;
    
    if (nb_samples >= SrsMaxNbSamples) {
        ret = ERROR_HLS_DECODE_ERROR;
        srs_error("Frame samples overflow, max=%d. ret=%d", SrsMaxNbSamples, ret);
        return ret;
    }
    
    SrsSample* sample = &samples[nb_samples++];
    sample->bytes = bytes;
    sample->size = size;
    
    return ret;
}

SrsAudioFrame::SrsAudioFrame()
{
    aac_packet_type = SrsAudioAacFrameTraitForbidden;
}

SrsAudioFrame::~SrsAudioFrame()
{
}

SrsAudioCodecConfig* SrsAudioFrame::acodec()
{
    return (SrsAudioCodecConfig*)codec;
}

SrsVideoFrame::SrsVideoFrame()
{
    frame_type = SrsVideoAvcFrameTypeForbidden;
    avc_packet_type = SrsVideoAvcFrameTraitForbidden;
    has_idr = has_aud = has_sps_pps = false;
    first_nalu_type = SrsAvcNaluTypeForbidden;
}

SrsVideoFrame::~SrsVideoFrame()
{
}

int SrsVideoFrame::add_sample(char* bytes, int size)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = SrsFrame::add_sample(bytes, size)) != ERROR_SUCCESS) {
        return ret;
    }
    
    // for video, parse the nalu type, set the IDR flag.
    SrsAvcNaluType nal_unit_type = (SrsAvcNaluType)(bytes[0] & 0x1f);
    
    if (nal_unit_type == SrsAvcNaluTypeIDR) {
        has_idr = true;
    } else if (nal_unit_type == SrsAvcNaluTypeSPS || nal_unit_type == SrsAvcNaluTypePPS) {
        has_sps_pps = true;
    } else if (nal_unit_type == SrsAvcNaluTypeAccessUnitDelimiter) {
        has_aud = true;
    }
    
    if (first_nalu_type == SrsAvcNaluTypeReserved) {
        first_nalu_type = nal_unit_type;
    }
    
    return ret;
}

SrsVideoCodecConfig* SrsVideoFrame::vcodec()
{
    return (SrsVideoCodecConfig*)codec;
}

SrsFormat::SrsFormat()
{
    acodec = NULL;
    vcodec = NULL;
    audio = NULL;
    video = NULL;
    buffer = new SrsBuffer();
    avc_parse_sps = true;
}

SrsFormat::~SrsFormat()
{
    srs_freep(audio);
    srs_freep(video);
    srs_freep(acodec);
    srs_freep(vcodec);
    srs_freep(buffer);
}

int SrsFormat::initialize()
{
    return ERROR_SUCCESS;
}

int SrsFormat::on_audio(int64_t timestamp, char* data, int size)
{
    int ret = ERROR_SUCCESS;
    
    if (!data || size <= 0) {
        srs_trace("no audio present, ignore it.");
        return ret;
    }
    
    if ((ret = buffer->initialize(data, size)) != ERROR_SUCCESS) {
        return ret;
    }
    
    // audio decode
    if (!buffer->require(1)) {
        ret = ERROR_HLS_DECODE_ERROR;
        srs_error("aac decode sound_format failed. ret=%d", ret);
        return ret;
    }
    
    // @see: E.4.2 Audio Tags, video_file_format_spec_v10_1.pdf, page 76
    uint8_t v = buffer->read_1bytes();
    SrsAudioCodecId codec = (SrsAudioCodecId)((v >> 4) & 0x0f);
    
    if (codec != SrsAudioCodecIdMP3 && codec != SrsAudioCodecIdAAC) {
        return ret;
    }
    
    if (!acodec) {
        acodec = new SrsAudioCodecConfig();
    }
    if (!audio) {
        audio = new SrsAudioFrame();
    }
    
    if ((ret = audio->initialize(acodec)) != ERROR_SUCCESS) {
        return ret;
    }
    
    buffer->skip(-1 * buffer->pos());
    if (codec == SrsAudioCodecIdMP3) {
        return audio_mp3_demux(buffer, timestamp);
    } else if (codec == SrsAudioCodecIdAAC) {
        return audio_aac_demux(buffer, timestamp);
    } else {
        return ret;
    }
}

int SrsFormat::on_video(int64_t timestamp, char* data, int size)
{
    int ret = ERROR_SUCCESS;
    
    if (!data || size <= 0) {
        srs_trace("no video present, ignore it.");
        return ret;
    }
    
    if ((ret = buffer->initialize(data, size)) != ERROR_SUCCESS) {
        return ret;
    }
    
    // video decode
    if (!buffer->require(1)) {
        ret = ERROR_HLS_DECODE_ERROR;
        srs_error("avc decode frame_type failed. ret=%d", ret);
        return ret;
    }
    
    // @see: E.4.3 Video Tags, video_file_format_spec_v10_1.pdf, page 78
    int8_t frame_type = buffer->read_1bytes();
    SrsVideoCodecId codec_id = (SrsVideoCodecId)(frame_type & 0x0f);
    
    // TODO: Support other codecs.
    if (codec_id != SrsVideoCodecIdAVC) {
        return ret;
    }
    
    if (!vcodec) {
        vcodec = new SrsVideoCodecConfig();
    }
    if (!video) {
        video = new SrsVideoFrame();
    }
    
    if ((ret = video->initialize(vcodec)) != ERROR_SUCCESS) {
        return ret;
    }
    
    buffer->skip(-1 * buffer->pos());
    return video_avc_demux(buffer, timestamp);
}

int SrsFormat::on_aac_sequence_header(char* data, int size)
{
    int ret = ERROR_SUCCESS;
    
    if (!acodec) {
        acodec = new SrsAudioCodecConfig();
    }
    if (!audio) {
        audio = new SrsAudioFrame();
    }
    
    if ((ret = audio->initialize(acodec)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return audio_aac_sequence_header_demux(data, size);
}

bool SrsFormat::is_aac_sequence_header()
{
    return acodec && acodec->id == SrsAudioCodecIdAAC
        && audio && audio->aac_packet_type == SrsAudioAacFrameTraitSequenceHeader;
}

bool SrsFormat::is_avc_sequence_header()
{
    return vcodec && vcodec->id == SrsVideoCodecIdAVC
        && video && video->avc_packet_type == SrsVideoAvcFrameTraitSequenceHeader;
}

int SrsFormat::video_avc_demux(SrsBuffer* stream, int64_t timestamp)
{
    int ret = ERROR_SUCCESS;
    
    // @see: E.4.3 Video Tags, video_file_format_spec_v10_1.pdf, page 78
    int8_t frame_type = stream->read_1bytes();
    SrsVideoCodecId codec_id = (SrsVideoCodecId)(frame_type & 0x0f);
    frame_type = (frame_type >> 4) & 0x0f;
    
    video->frame_type = (SrsVideoAvcFrameType)frame_type;
    
    // ignore info frame without error,
    // @see https://github.com/ossrs/srs/issues/288#issuecomment-69863909
    if (video->frame_type == SrsVideoAvcFrameTypeVideoInfoFrame) {
        srs_warn("avc igone the info frame, ret=%d", ret);
        return ret;
    }
    
    // only support h.264/avc
    if (codec_id != SrsVideoCodecIdAVC) {
        ret = ERROR_HLS_DECODE_ERROR;
        srs_error("avc only support video h.264/avc codec. actual=%d, ret=%d", codec_id, ret);
        return ret;
    }
    vcodec->id = codec_id;
    
    if (!stream->require(4)) {
        ret = ERROR_HLS_DECODE_ERROR;
        srs_error("avc decode avc_packet_type failed. ret=%d", ret);
        return ret;
    }
    int8_t avc_packet_type = stream->read_1bytes();
    int32_t composition_time = stream->read_3bytes();
    
    // pts = dts + cts.
    video->dts = timestamp;
    video->cts = composition_time;
    video->avc_packet_type = (SrsVideoAvcFrameTrait)avc_packet_type;
    
    if (avc_packet_type == SrsVideoAvcFrameTraitSequenceHeader) {
        if ((ret = avc_demux_sps_pps(stream)) != ERROR_SUCCESS) {
            return ret;
        }
    } else if (avc_packet_type == SrsVideoAvcFrameTraitNALU){
        if ((ret = video_nalu_demux(stream)) != ERROR_SUCCESS) {
            return ret;
        }
    } else {
        // ignored.
    }
    
    srs_info("avc decoded, type=%d, codec=%d, avc=%d, cts=%d, size=%d",
             frame_type, video_codec_id, avc_packet_type, composition_time, size);
    
    return ret;
}

int SrsFormat::avc_demux_sps_pps(SrsBuffer* stream)
{
    int ret = ERROR_SUCCESS;
    
    // AVCDecoderConfigurationRecord
    // 5.2.4.1.1 Syntax, ISO_IEC_14496-15-AVC-format-2012.pdf, page 16
    vcodec->avc_extra_size = stream->size() - stream->pos();
    if (vcodec->avc_extra_size > 0) {
        srs_freepa(vcodec->avc_extra_data);
        vcodec->avc_extra_data = new char[vcodec->avc_extra_size];
        memcpy(vcodec->avc_extra_data, stream->data() + stream->pos(), vcodec->avc_extra_size);
    }
    
    if (!stream->require(6)) {
        ret = ERROR_HLS_DECODE_ERROR;
        srs_error("avc decode sequenc header failed. ret=%d", ret);
        return ret;
    }
    //int8_t configurationVersion = stream->read_1bytes();
    stream->read_1bytes();
    //int8_t AVCProfileIndication = stream->read_1bytes();
    vcodec->avc_profile = (SrsAvcProfile)stream->read_1bytes();
    //int8_t profile_compatibility = stream->read_1bytes();
    stream->read_1bytes();
    //int8_t AVCLevelIndication = stream->read_1bytes();
    vcodec->avc_level = (SrsAvcLevel)stream->read_1bytes();
    
    // parse the NALU size.
    int8_t lengthSizeMinusOne = stream->read_1bytes();
    lengthSizeMinusOne &= 0x03;
    vcodec->NAL_unit_length = lengthSizeMinusOne;
    
    // 5.3.4.2.1 Syntax, ISO_IEC_14496-15-AVC-format-2012.pdf, page 16
    // 5.2.4.1 AVC decoder configuration record
    // 5.2.4.1.2 Semantics
    // The value of this field shall be one of 0, 1, or 3 corresponding to a
    // length encoded with 1, 2, or 4 bytes, respectively.
    if (vcodec->NAL_unit_length == 2) {
        ret = ERROR_HLS_DECODE_ERROR;
        srs_error("sps lengthSizeMinusOne should never be 2. ret=%d", ret);
        return ret;
    }
    
    // 1 sps, 7.3.2.1 Sequence parameter set RBSP syntax
    // ISO_IEC_14496-10-AVC-2003.pdf, page 45.
    if (!stream->require(1)) {
        ret = ERROR_HLS_DECODE_ERROR;
        srs_error("avc decode sequenc header sps failed. ret=%d", ret);
        return ret;
    }
    int8_t numOfSequenceParameterSets = stream->read_1bytes();
    numOfSequenceParameterSets &= 0x1f;
    if (numOfSequenceParameterSets != 1) {
        ret = ERROR_HLS_DECODE_ERROR;
        srs_error("avc decode sequenc header sps failed. ret=%d", ret);
        return ret;
    }
    if (!stream->require(2)) {
        ret = ERROR_HLS_DECODE_ERROR;
        srs_error("avc decode sequenc header sps size failed. ret=%d", ret);
        return ret;
    }
    vcodec->sequenceParameterSetLength = stream->read_2bytes();
    if (!stream->require(vcodec->sequenceParameterSetLength)) {
        ret = ERROR_HLS_DECODE_ERROR;
        srs_error("avc decode sequenc header sps data failed. ret=%d", ret);
        return ret;
    }
    if (vcodec->sequenceParameterSetLength > 0) {
        srs_freepa(vcodec->sequenceParameterSetNALUnit);
        vcodec->sequenceParameterSetNALUnit = new char[vcodec->sequenceParameterSetLength];
        stream->read_bytes(vcodec->sequenceParameterSetNALUnit, vcodec->sequenceParameterSetLength);
    }
    // 1 pps
    if (!stream->require(1)) {
        ret = ERROR_HLS_DECODE_ERROR;
        srs_error("avc decode sequenc header pps failed. ret=%d", ret);
        return ret;
    }
    int8_t numOfPictureParameterSets = stream->read_1bytes();
    numOfPictureParameterSets &= 0x1f;
    if (numOfPictureParameterSets != 1) {
        ret = ERROR_HLS_DECODE_ERROR;
        srs_error("avc decode sequenc header pps failed. ret=%d", ret);
        return ret;
    }
    if (!stream->require(2)) {
        ret = ERROR_HLS_DECODE_ERROR;
        srs_error("avc decode sequenc header pps size failed. ret=%d", ret);
        return ret;
    }
    vcodec->pictureParameterSetLength = stream->read_2bytes();
    if (!stream->require(vcodec->pictureParameterSetLength)) {
        ret = ERROR_HLS_DECODE_ERROR;
        srs_error("avc decode sequenc header pps data failed. ret=%d", ret);
        return ret;
    }
    if (vcodec->pictureParameterSetLength > 0) {
        srs_freepa(vcodec->pictureParameterSetNALUnit);
        vcodec->pictureParameterSetNALUnit = new char[vcodec->pictureParameterSetLength];
        stream->read_bytes(vcodec->pictureParameterSetNALUnit, vcodec->pictureParameterSetLength);
    }
    
    return avc_demux_sps();
}

int SrsFormat::avc_demux_sps()
{
    int ret = ERROR_SUCCESS;
    
    if (!vcodec->sequenceParameterSetLength) {
        return ret;
    }
    
    SrsBuffer stream;
    if ((ret = stream.initialize(vcodec->sequenceParameterSetNALUnit, vcodec->sequenceParameterSetLength)) != ERROR_SUCCESS) {
        return ret;
    }
    
    // for NALU, 7.3.1 NAL unit syntax
    // ISO_IEC_14496-10-AVC-2012.pdf, page 61.
    if (!stream.require(1)) {
        ret = ERROR_HLS_DECODE_ERROR;
        srs_error("avc decode sps failed. ret=%d", ret);
        return ret;
    }
    int8_t nutv = stream.read_1bytes();
    
    // forbidden_zero_bit shall be equal to 0.
    int8_t forbidden_zero_bit = (nutv >> 7) & 0x01;
    if (forbidden_zero_bit) {
        ret = ERROR_HLS_DECODE_ERROR;
        srs_error("forbidden_zero_bit shall be equal to 0. ret=%d", ret);
        return ret;
    }
    
    // nal_ref_idc not equal to 0 specifies that the content of the NAL unit contains a sequence parameter set or a picture
    // parameter set or a slice of a reference picture or a slice data partition of a reference picture.
    int8_t nal_ref_idc = (nutv >> 5) & 0x03;
    if (!nal_ref_idc) {
        ret = ERROR_HLS_DECODE_ERROR;
        srs_error("for sps, nal_ref_idc shall be not be equal to 0. ret=%d", ret);
        return ret;
    }
    
    // 7.4.1 NAL unit semantics
    // ISO_IEC_14496-10-AVC-2012.pdf, page 61.
    // nal_unit_type specifies the type of RBSP data structure contained in the NAL unit as specified in Table 7-1.
    SrsAvcNaluType nal_unit_type = (SrsAvcNaluType)(nutv & 0x1f);
    if (nal_unit_type != 7) {
        ret = ERROR_HLS_DECODE_ERROR;
        srs_error("for sps, nal_unit_type shall be equal to 7. ret=%d", ret);
        return ret;
    }
    
    // decode the rbsp from sps.
    // rbsp[ i ] a raw byte sequence payload is specified as an ordered sequence of bytes.
    int8_t* rbsp = new int8_t[vcodec->sequenceParameterSetLength];
    SrsAutoFreeA(int8_t, rbsp);
    
    int nb_rbsp = 0;
    while (!stream.empty()) {
        rbsp[nb_rbsp] = stream.read_1bytes();
        
        // XX 00 00 03 XX, the 03 byte should be drop.
        if (nb_rbsp > 2 && rbsp[nb_rbsp - 2] == 0 && rbsp[nb_rbsp - 1] == 0 && rbsp[nb_rbsp] == 3) {
            // read 1byte more.
            if (stream.empty()) {
                break;
            }
            rbsp[nb_rbsp] = stream.read_1bytes();
            nb_rbsp++;
            
            continue;
        }
        
        nb_rbsp++;
    }
    
    return avc_demux_sps_rbsp((char*)rbsp, nb_rbsp);
}


int SrsFormat::avc_demux_sps_rbsp(char* rbsp, int nb_rbsp)
{
    int ret = ERROR_SUCCESS;
    
    // we donot parse the detail of sps.
    // @see https://github.com/ossrs/srs/issues/474
    if (!avc_parse_sps) {
        return ret;
    }
    
    // reparse the rbsp.
    SrsBuffer stream;
    if ((ret = stream.initialize(rbsp, nb_rbsp)) != ERROR_SUCCESS) {
        return ret;
    }
    
    // for SPS, 7.3.2.1.1 Sequence parameter set data syntax
    // ISO_IEC_14496-10-AVC-2012.pdf, page 62.
    if (!stream.require(3)) {
        ret = ERROR_HLS_DECODE_ERROR;
        srs_error("sps shall atleast 3bytes. ret=%d", ret);
        return ret;
    }
    uint8_t profile_idc = stream.read_1bytes();
    if (!profile_idc) {
        ret = ERROR_HLS_DECODE_ERROR;
        srs_error("sps the profile_idc invalid. ret=%d", ret);
        return ret;
    }
    
    int8_t flags = stream.read_1bytes();
    if (flags & 0x03) {
        ret = ERROR_HLS_DECODE_ERROR;
        srs_error("sps the flags invalid. ret=%d", ret);
        return ret;
    }
    
    uint8_t level_idc = stream.read_1bytes();
    if (!level_idc) {
        ret = ERROR_HLS_DECODE_ERROR;
        srs_error("sps the level_idc invalid. ret=%d", ret);
        return ret;
    }
    
    SrsBitBuffer bs;
    if ((ret = bs.initialize(&stream)) != ERROR_SUCCESS) {
        return ret;
    }
    
    int32_t seq_parameter_set_id = -1;
    if ((ret = srs_avc_nalu_read_uev(&bs, seq_parameter_set_id)) != ERROR_SUCCESS) {
        return ret;
    }
    if (seq_parameter_set_id < 0) {
        ret = ERROR_HLS_DECODE_ERROR;
        srs_error("sps the seq_parameter_set_id invalid. ret=%d", ret);
        return ret;
    }
    srs_info("sps parse profile=%d, level=%d, sps_id=%d", profile_idc, level_idc, seq_parameter_set_id);
    
    int32_t chroma_format_idc = -1;
    if (profile_idc == 100 || profile_idc == 110 || profile_idc == 122 || profile_idc == 244
        || profile_idc == 44 || profile_idc == 83 || profile_idc == 86 || profile_idc == 118
        || profile_idc == 128
        ) {
        if ((ret = srs_avc_nalu_read_uev(&bs, chroma_format_idc)) != ERROR_SUCCESS) {
            return ret;
        }
        if (chroma_format_idc == 3) {
            int8_t separate_colour_plane_flag = -1;
            if ((ret = srs_avc_nalu_read_bit(&bs, separate_colour_plane_flag)) != ERROR_SUCCESS) {
                return ret;
            }
        }
        
        int32_t bit_depth_luma_minus8 = -1;
        if ((ret = srs_avc_nalu_read_uev(&bs, bit_depth_luma_minus8)) != ERROR_SUCCESS) {
            return ret;
        }
        
        int32_t bit_depth_chroma_minus8 = -1;
        if ((ret = srs_avc_nalu_read_uev(&bs, bit_depth_chroma_minus8)) != ERROR_SUCCESS) {
            return ret;
        }
        
        int8_t qpprime_y_zero_transform_bypass_flag = -1;
        if ((ret = srs_avc_nalu_read_bit(&bs, qpprime_y_zero_transform_bypass_flag)) != ERROR_SUCCESS) {
            return ret;
        }
        
        int8_t seq_scaling_matrix_present_flag = -1;
        if ((ret = srs_avc_nalu_read_bit(&bs, seq_scaling_matrix_present_flag)) != ERROR_SUCCESS) {
            return ret;
        }
        if (seq_scaling_matrix_present_flag) {
            int nb_scmpfs = ((chroma_format_idc != 3)? 8:12);
            for (int i = 0; i < nb_scmpfs; i++) {
                int8_t seq_scaling_matrix_present_flag_i = -1;
                if ((ret = srs_avc_nalu_read_bit(&bs, seq_scaling_matrix_present_flag_i)) != ERROR_SUCCESS) {
                    return ret;
                }
            }
        }
    }
    
    int32_t log2_max_frame_num_minus4 = -1;
    if ((ret = srs_avc_nalu_read_uev(&bs, log2_max_frame_num_minus4)) != ERROR_SUCCESS) {
        return ret;
    }
    
    int32_t pic_order_cnt_type = -1;
    if ((ret = srs_avc_nalu_read_uev(&bs, pic_order_cnt_type)) != ERROR_SUCCESS) {
        return ret;
    }
    
    if (pic_order_cnt_type == 0) {
        int32_t log2_max_pic_order_cnt_lsb_minus4 = -1;
        if ((ret = srs_avc_nalu_read_uev(&bs, log2_max_pic_order_cnt_lsb_minus4)) != ERROR_SUCCESS) {
            return ret;
        }
    } else if (pic_order_cnt_type == 1) {
        int8_t delta_pic_order_always_zero_flag = -1;
        if ((ret = srs_avc_nalu_read_bit(&bs, delta_pic_order_always_zero_flag)) != ERROR_SUCCESS) {
            return ret;
        }
        
        int32_t offset_for_non_ref_pic = -1;
        if ((ret = srs_avc_nalu_read_uev(&bs, offset_for_non_ref_pic)) != ERROR_SUCCESS) {
            return ret;
        }
        
        int32_t offset_for_top_to_bottom_field = -1;
        if ((ret = srs_avc_nalu_read_uev(&bs, offset_for_top_to_bottom_field)) != ERROR_SUCCESS) {
            return ret;
        }
        
        int32_t num_ref_frames_in_pic_order_cnt_cycle = -1;
        if ((ret = srs_avc_nalu_read_uev(&bs, num_ref_frames_in_pic_order_cnt_cycle)) != ERROR_SUCCESS) {
            return ret;
        }
        if (num_ref_frames_in_pic_order_cnt_cycle < 0) {
            ret = ERROR_HLS_DECODE_ERROR;
            srs_error("sps the num_ref_frames_in_pic_order_cnt_cycle invalid. ret=%d", ret);
            return ret;
        }
        for (int i = 0; i < num_ref_frames_in_pic_order_cnt_cycle; i++) {
            int32_t offset_for_ref_frame_i = -1;
            if ((ret = srs_avc_nalu_read_uev(&bs, offset_for_ref_frame_i)) != ERROR_SUCCESS) {
                return ret;
            }
        }
    }
    
    int32_t max_num_ref_frames = -1;
    if ((ret = srs_avc_nalu_read_uev(&bs, max_num_ref_frames)) != ERROR_SUCCESS) {
        return ret;
    }
    
    int8_t gaps_in_frame_num_value_allowed_flag = -1;
    if ((ret = srs_avc_nalu_read_bit(&bs, gaps_in_frame_num_value_allowed_flag)) != ERROR_SUCCESS) {
        return ret;
    }
    
    int32_t pic_width_in_mbs_minus1 = -1;
    if ((ret = srs_avc_nalu_read_uev(&bs, pic_width_in_mbs_minus1)) != ERROR_SUCCESS) {
        return ret;
    }
    
    int32_t pic_height_in_map_units_minus1 = -1;
    if ((ret = srs_avc_nalu_read_uev(&bs, pic_height_in_map_units_minus1)) != ERROR_SUCCESS) {
        return ret;
    }
    
    vcodec->width = (int)(pic_width_in_mbs_minus1 + 1) * 16;
    vcodec->height = (int)(pic_height_in_map_units_minus1 + 1) * 16;
    
    return ret;
}

int SrsFormat::video_nalu_demux(SrsBuffer* stream)
{
    int ret = ERROR_SUCCESS;
    
    // ensure the sequence header demuxed
    if (!vcodec->is_avc_codec_ok()) {
        srs_warn("avc ignore type=%d for no sequence header. ret=%d", SrsVideoAvcFrameTraitNALU, ret);
        return ret;
    }
    
    // guess for the first time.
    if (vcodec->payload_format == SrsAvcPayloadFormatGuess) {
        // One or more NALUs (Full frames are required)
        // try  "AnnexB" from ISO_IEC_14496-10-AVC-2003.pdf, page 211.
        if ((ret = avc_demux_annexb_format(stream)) != ERROR_SUCCESS) {
            // stop try when system error.
            if (ret != ERROR_HLS_AVC_TRY_OTHERS) {
                srs_error("avc demux for annexb failed. ret=%d", ret);
                return ret;
            }
            
            // try "ISO Base Media File Format" from ISO_IEC_14496-15-AVC-format-2012.pdf, page 20
            if ((ret = avc_demux_ibmf_format(stream)) != ERROR_SUCCESS) {
                return ret;
            } else {
                vcodec->payload_format = SrsAvcPayloadFormatIbmf;
                srs_info("hls guess avc payload is ibmf format.");
            }
        } else {
            vcodec->payload_format = SrsAvcPayloadFormatAnnexb;
            srs_info("hls guess avc payload is annexb format.");
        }
    } else if (vcodec->payload_format == SrsAvcPayloadFormatIbmf) {
        // try "ISO Base Media File Format" from ISO_IEC_14496-15-AVC-format-2012.pdf, page 20
        if ((ret = avc_demux_ibmf_format(stream)) != ERROR_SUCCESS) {
            return ret;
        }
        srs_info("hls decode avc payload in ibmf format.");
    } else {
        // One or more NALUs (Full frames are required)
        // try  "AnnexB" from ISO_IEC_14496-10-AVC-2003.pdf, page 211.
        if ((ret = avc_demux_annexb_format(stream)) != ERROR_SUCCESS) {
            // ok, we guess out the payload is annexb, but maybe changed to ibmf.
            if (ret != ERROR_HLS_AVC_TRY_OTHERS) {
                srs_error("avc demux for annexb failed. ret=%d", ret);
                return ret;
            }
            
            // try "ISO Base Media File Format" from ISO_IEC_14496-15-AVC-format-2012.pdf, page 20
            if ((ret = avc_demux_ibmf_format(stream)) != ERROR_SUCCESS) {
                return ret;
            } else {
                vcodec->payload_format = SrsAvcPayloadFormatIbmf;
                srs_warn("hls avc payload change from annexb to ibmf format.");
            }
        }
        srs_info("hls decode avc payload in annexb format.");
    }
    
    return ret;
}

int SrsFormat::avc_demux_annexb_format(SrsBuffer* stream)
{
    int ret = ERROR_SUCCESS;
    
    // not annexb, try others
    if (!srs_avc_startswith_annexb(stream, NULL)) {
        return ERROR_HLS_AVC_TRY_OTHERS;
    }
    
    // AnnexB
    // B.1.1 Byte stream NAL unit syntax,
    // ISO_IEC_14496-10-AVC-2003.pdf, page 211.
    while (!stream->empty()) {
        // find start code
        int nb_start_code = 0;
        if (!srs_avc_startswith_annexb(stream, &nb_start_code)) {
            return ret;
        }
        
        // skip the start code.
        if (nb_start_code > 0) {
            stream->skip(nb_start_code);
        }
        
        // the NALU start bytes.
        char* p = stream->data() + stream->pos();
        
        // get the last matched NALU
        while (!stream->empty()) {
            if (srs_avc_startswith_annexb(stream, NULL)) {
                break;
            }
            
            stream->skip(1);
        }
        
        char* pp = stream->data() + stream->pos();
        
        // skip the empty.
        if (pp - p <= 0) {
            continue;
        }
        
        // got the NALU.
        if ((ret = video->add_sample(p, (int)(pp - p))) != ERROR_SUCCESS) {
            srs_error("annexb add video sample failed. ret=%d", ret);
            return ret;
        }
    }
    
    return ret;
}

int SrsFormat::avc_demux_ibmf_format(SrsBuffer* stream)
{
    int ret = ERROR_SUCCESS;
    
    int PictureLength = stream->size() - stream->pos();
    
    // 5.3.4.2.1 Syntax, ISO_IEC_14496-15-AVC-format-2012.pdf, page 16
    // 5.2.4.1 AVC decoder configuration record
    // 5.2.4.1.2 Semantics
    // The value of this field shall be one of 0, 1, or 3 corresponding to a
    // length encoded with 1, 2, or 4 bytes, respectively.
    srs_assert(vcodec->NAL_unit_length != 2);
    
    // 5.3.4.2.1 Syntax, ISO_IEC_14496-15-AVC-format-2012.pdf, page 20
    for (int i = 0; i < PictureLength;) {
        // unsigned int((NAL_unit_length+1)*8) NALUnitLength;
        if (!stream->require(vcodec->NAL_unit_length + 1)) {
            ret = ERROR_HLS_DECODE_ERROR;
            srs_error("avc decode NALU size failed. ret=%d", ret);
            return ret;
        }
        int32_t NALUnitLength = 0;
        if (vcodec->NAL_unit_length == 3) {
            NALUnitLength = stream->read_4bytes();
        } else if (vcodec->NAL_unit_length == 1) {
            NALUnitLength = stream->read_2bytes();
        } else {
            NALUnitLength = stream->read_1bytes();
        }
        
        // maybe stream is invalid format.
        // see: https://github.com/ossrs/srs/issues/183
        if (NALUnitLength < 0) {
            ret = ERROR_HLS_DECODE_ERROR;
            srs_error("maybe stream is AnnexB format. ret=%d", ret);
            return ret;
        }
        
        // NALUnit
        if (!stream->require(NALUnitLength)) {
            ret = ERROR_HLS_DECODE_ERROR;
            srs_error("avc decode NALU data failed. ret=%d", ret);
            return ret;
        }
        // 7.3.1 NAL unit syntax, ISO_IEC_14496-10-AVC-2003.pdf, page 44.
        if ((ret = video->add_sample(stream->data() + stream->pos(), NALUnitLength)) != ERROR_SUCCESS) {
            srs_error("avc add video sample failed. ret=%d", ret);
            return ret;
        }
        stream->skip(NALUnitLength);
        
        i += vcodec->NAL_unit_length + 1 + NALUnitLength;
    }
    
    return ret;
}

int SrsFormat::audio_aac_demux(SrsBuffer* stream, int64_t timestamp)
{
    int ret = ERROR_SUCCESS;
    
    audio->cts = 0;
    audio->dts = timestamp;
    
    // @see: E.4.2 Audio Tags, video_file_format_spec_v10_1.pdf, page 76
    int8_t sound_format = stream->read_1bytes();
    
    int8_t sound_type = sound_format & 0x01;
    int8_t sound_size = (sound_format >> 1) & 0x01;
    int8_t sound_rate = (sound_format >> 2) & 0x03;
    sound_format = (sound_format >> 4) & 0x0f;
    
    SrsAudioCodecId codec_id = (SrsAudioCodecId)sound_format;
    acodec->id = codec_id;
    
    acodec->sound_type = (SrsAudioChannels)sound_type;
    acodec->sound_rate = (SrsAudioSampleRate)sound_rate;
    acodec->sound_size = (SrsAudioSampleBits)sound_size;
    
    // we support h.264+mp3 for hls.
    if (codec_id == SrsAudioCodecIdMP3) {
        return ERROR_HLS_TRY_MP3;
    }
    
    // only support aac
    if (codec_id != SrsAudioCodecIdAAC) {
        ret = ERROR_HLS_DECODE_ERROR;
        srs_error("aac only support mp3/aac codec. actual=%d, ret=%d", codec_id, ret);
        return ret;
    }
    
    if (!stream->require(1)) {
        ret = ERROR_HLS_DECODE_ERROR;
        srs_error("aac decode aac_packet_type failed. ret=%d", ret);
        return ret;
    }
    
    SrsAudioAacFrameTrait aac_packet_type = (SrsAudioAacFrameTrait)stream->read_1bytes();
    audio->aac_packet_type = (SrsAudioAacFrameTrait)aac_packet_type;
    
    if (aac_packet_type == SrsAudioAacFrameTraitSequenceHeader) {
        // AudioSpecificConfig
        // 1.6.2.1 AudioSpecificConfig, in ISO_IEC_14496-3-AAC-2001.pdf, page 33.
        acodec->aac_extra_size = stream->size() - stream->pos();
        if (acodec->aac_extra_size > 0) {
            srs_freepa(acodec->aac_extra_data);
            acodec->aac_extra_data = new char[acodec->aac_extra_size];
            memcpy(acodec->aac_extra_data, stream->data() + stream->pos(), acodec->aac_extra_size);
            
            if ((ret = audio_aac_sequence_header_demux(acodec->aac_extra_data, acodec->aac_extra_size)) != ERROR_SUCCESS) {
                return ret;
            }
        }
    } else if (aac_packet_type == SrsAudioAacFrameTraitRawData) {
        // ensure the sequence header demuxed
        if (!acodec->is_aac_codec_ok()) {
            srs_warn("aac ignore type=%d for no sequence header. ret=%d", aac_packet_type, ret);
            return ret;
        }
        
        // Raw AAC frame data in UI8 []
        // 6.3 Raw Data, aac-iso-13818-7.pdf, page 28
        if ((ret = audio->add_sample(stream->data() + stream->pos(), stream->size() - stream->pos())) != ERROR_SUCCESS) {
            srs_error("aac add sample failed. ret=%d", ret);
            return ret;
        }
    } else {
        // ignored.
    }
    
    // reset the sample rate by sequence header
    if (acodec->aac_sample_rate != SrsAacSampleRateUnset) {
        static int srs_aac_srates[] = {
            96000, 88200, 64000, 48000,
            44100, 32000, 24000, 22050,
            16000, 12000, 11025,  8000,
            7350,     0,     0,    0
        };
        switch (srs_aac_srates[acodec->aac_sample_rate]) {
            case 11025:
                acodec->sound_rate = SrsAudioSampleRate11025;
                break;
            case 22050:
                acodec->sound_rate = SrsAudioSampleRate22050;
                break;
            case 44100:
                acodec->sound_rate = SrsAudioSampleRate44100;
                break;
            default:
                break;
        };
    }
    
    srs_info("aac decoded, type=%d, codec=%d, asize=%d, rate=%d, format=%d, size=%d",
             sound_type, codec_id, sound_size, sound_rate, sound_format, size);
    
    return ret;
}

int SrsFormat::audio_mp3_demux(SrsBuffer* stream, int64_t timestamp)
{
    int ret = ERROR_SUCCESS;
    
    audio->cts = 0;
    audio->dts = timestamp;
    
    // @see: E.4.2 Audio Tags, video_file_format_spec_v10_1.pdf, page 76
    int8_t sound_format = stream->read_1bytes();
    
    int8_t sound_type = sound_format & 0x01;
    int8_t sound_size = (sound_format >> 1) & 0x01;
    int8_t sound_rate = (sound_format >> 2) & 0x03;
    sound_format = (sound_format >> 4) & 0x0f;
    
    SrsAudioCodecId codec_id = (SrsAudioCodecId)sound_format;
    acodec->id = codec_id;
    
    acodec->sound_type = (SrsAudioChannels)sound_type;
    acodec->sound_rate = (SrsAudioSampleRate)sound_rate;
    acodec->sound_size = (SrsAudioSampleBits)sound_size;
    
    // we always decode aac then mp3.
    srs_assert(acodec->id == SrsAudioCodecIdMP3);
    
    stream->skip(1);
    if (stream->empty()) {
        return ret;
    }
    
    char* data = stream->data() + stream->pos();
    int size = stream->size() - stream->pos();
    
    // mp3 payload.
    if ((ret = audio->add_sample(data, size)) != ERROR_SUCCESS) {
        srs_error("audio codec add mp3 sample failed. ret=%d", ret);
        return ret;
    }
    
    srs_info("audio decoded, type=%d, codec=%d, asize=%d, rate=%d, format=%d, size=%d",
             acodec->sound_type, acodec->id, acodec->sound_size, acodec->sound_rate, acodec->acodec, size);
    
    return ret;
}

int SrsFormat::audio_aac_sequence_header_demux(char* data, int size)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = buffer->initialize(data, size)) != ERROR_SUCCESS) {
        return ret;
    }
    
    // only need to decode the first 2bytes:
    //      audioObjectType, aac_profile, 5bits.
    //      samplingFrequencyIndex, aac_sample_rate, 4bits.
    //      channelConfiguration, aac_channels, 4bits
    if (!buffer->require(2)) {
        ret = ERROR_HLS_DECODE_ERROR;
        srs_error("audio codec decode aac sequence header failed. ret=%d", ret);
        return ret;
    }
    uint8_t profile_ObjectType = buffer->read_1bytes();
    uint8_t samplingFrequencyIndex = buffer->read_1bytes();
    
    acodec->aac_channels = (samplingFrequencyIndex >> 3) & 0x0f;
    samplingFrequencyIndex = ((profile_ObjectType << 1) & 0x0e) | ((samplingFrequencyIndex >> 7) & 0x01);
    profile_ObjectType = (profile_ObjectType >> 3) & 0x1f;
    
    // set the aac sample rate.
    acodec->aac_sample_rate = samplingFrequencyIndex;
    
    // convert the object type in sequence header to aac profile of ADTS.
    acodec->aac_object = (SrsAacObjectType)profile_ObjectType;
    if (acodec->aac_object == SrsAacObjectTypeReserved) {
        ret = ERROR_HLS_DECODE_ERROR;
        srs_error("audio codec decode aac sequence header failed, "
                  "adts object=%d invalid. ret=%d", profile_ObjectType, ret);
        return ret;
    }
    
    // TODO: FIXME: to support aac he/he-v2, see: ngx_rtmp_codec_parse_aac_header
    // @see: https://github.com/winlinvip/nginx-rtmp-module/commit/3a5f9eea78fc8d11e8be922aea9ac349b9dcbfc2
    //
    // donot force to LC, @see: https://github.com/ossrs/srs/issues/81
    // the source will print the sequence header info.
    //if (aac_profile > 3) {
    // Mark all extended profiles as LC
    // to make Android as happy as possible.
    // @see: ngx_rtmp_hls_parse_aac_header
    //aac_profile = 1;
    //}
    
    return ret;
}

