/*
The MIT License (MIT)

Copyright (c) 2013-2015 SRS(simple-rtmp-server)

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
#include <srs_kernel_stream.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_core_autofree.hpp>

string srs_codec_video2str(SrsCodecVideo codec)
{
    switch (codec) {
        case SrsCodecVideoAVC: 
            return "H264";
        case SrsCodecVideoOn2VP6:
        case SrsCodecVideoOn2VP6WithAlphaChannel:
            return "VP6";
        case SrsCodecVideoReserved:
        case SrsCodecVideoReserved1:
        case SrsCodecVideoReserved2:
        case SrsCodecVideoDisabled:
        case SrsCodecVideoSorensonH263:
        case SrsCodecVideoScreenVideo:
        case SrsCodecVideoScreenVideoVersion2:
        default:
            return "Other";
    }
}

string srs_codec_audio2str(SrsCodecAudio codec)
{
    switch (codec) {
        case SrsCodecAudioAAC:
            return "AAC";
        case SrsCodecAudioMP3:
            return "MP3";
        case SrsCodecAudioReserved1:
        case SrsCodecAudioLinearPCMPlatformEndian:
        case SrsCodecAudioADPCM:
        case SrsCodecAudioLinearPCMLittleEndian:
        case SrsCodecAudioNellymoser16kHzMono:
        case SrsCodecAudioNellymoser8kHzMono:
        case SrsCodecAudioNellymoser:
        case SrsCodecAudioReservedG711AlawLogarithmicPCM:
        case SrsCodecAudioReservedG711MuLawLogarithmicPCM:
        case SrsCodecAudioReserved:
        case SrsCodecAudioSpeex:
        case SrsCodecAudioReservedMP3_8kHz:
        case SrsCodecAudioReservedDeviceSpecificSound:
        default:
            return "Other";
    }
}

string srs_codec_aac_profile2str(SrsAacProfile aac_profile)
{
    switch (aac_profile) {
        case SrsAacProfileMain: return "Main";
        case SrsAacProfileLC: return "LC";
        case SrsAacProfileSSR: return "SSR";
        default: return "Other";
    }
}

string srs_codec_aac_object2str(SrsAacObjectType aac_object)
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

SrsAacObjectType srs_codec_aac_ts2rtmp(SrsAacProfile profile)
{
    switch (profile) {
        case SrsAacProfileMain: return SrsAacObjectTypeAacMain;
        case SrsAacProfileLC: return SrsAacObjectTypeAacLC;
        case SrsAacProfileSSR: return SrsAacObjectTypeAacSSR;
        default: return SrsAacObjectTypeReserved;
    }
}

SrsAacProfile srs_codec_aac_rtmp2ts(SrsAacObjectType object_type)
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

string srs_codec_avc_profile2str(SrsAvcProfile profile)
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

string srs_codec_avc_level2str(SrsAvcLevel level)
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

/**
* the public data, event HLS disable, others can use it.
*/
// 0 = 5.5 kHz = 5512 Hz
// 1 = 11 kHz = 11025 Hz
// 2 = 22 kHz = 22050 Hz
// 3 = 44 kHz = 44100 Hz
int flv_sample_rates[] = {5512, 11025, 22050, 44100};

// the sample rates in the codec,
// in the sequence header.
int aac_sample_rates[] = 
{
    96000, 88200, 64000, 48000,
    44100, 32000, 24000, 22050,
    16000, 12000, 11025,  8000,
    7350,     0,     0,    0
};

SrsFlvCodec::SrsFlvCodec()
{
}

SrsFlvCodec::~SrsFlvCodec()
{
}

bool SrsFlvCodec::video_is_keyframe(char* data, int size)
{
    // 2bytes required.
    if (size < 1) {
        return false;
    }

    char frame_type = data[0];
    frame_type = (frame_type >> 4) & 0x0F;
    
    return frame_type == SrsCodecVideoAVCFrameKeyFrame;
}

bool SrsFlvCodec::video_is_sequence_header(char* data, int size)
{
    // sequence header only for h264
    if (!video_is_h264(data, size)) {
        return false;
    }
    
    // 2bytes required.
    if (size < 2) {
        return false;
    }

    char frame_type = data[0];
    frame_type = (frame_type >> 4) & 0x0F;

    char avc_packet_type = data[1];
    
    return frame_type == SrsCodecVideoAVCFrameKeyFrame 
        && avc_packet_type == SrsCodecVideoAVCTypeSequenceHeader;
}

bool SrsFlvCodec::audio_is_sequence_header(char* data, int size)
{
    // sequence header only for aac
    if (!audio_is_aac(data, size)) {
        return false;
    }
    
    // 2bytes required.
    if (size < 2) {
        return false;
    }
    
    char aac_packet_type = data[1];
    
    return aac_packet_type == SrsCodecAudioTypeSequenceHeader;
}

bool SrsFlvCodec::video_is_h264(char* data, int size)
{
    // 1bytes required.
    if (size < 1) {
        return false;
    }

    char codec_id = data[0];
    codec_id = codec_id & 0x0F;
    
    return codec_id == SrsCodecVideoAVC;
}

bool SrsFlvCodec::audio_is_aac(char* data, int size)
{
    // 1bytes required.
    if (size < 1) {
        return false;
    }
    
    char sound_format = data[0];
    sound_format = (sound_format >> 4) & 0x0F;
    
    return sound_format == SrsCodecAudioAAC;
}

bool SrsFlvCodec::video_is_acceptable(char* data, int size)
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

string srs_codec_avc_nalu2str(SrsAvcNaluType nalu_type)
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

SrsCodecSampleUnit::SrsCodecSampleUnit()
{
    size = 0;
    bytes = NULL;
}

SrsCodecSampleUnit::~SrsCodecSampleUnit()
{
}

SrsCodecSample::SrsCodecSample()
{
    clear();
}

SrsCodecSample::~SrsCodecSample()
{
}

void SrsCodecSample::clear()
{
    is_video = false;
    nb_sample_units = 0;

    cts = 0;
    frame_type = SrsCodecVideoAVCFrameReserved;
    avc_packet_type = SrsCodecVideoAVCTypeReserved;
    has_idr = false;
    first_nalu_type = SrsAvcNaluTypeReserved;
    
    acodec = SrsCodecAudioReserved1;
    sound_rate = SrsCodecAudioSampleRateReserved;
    sound_size = SrsCodecAudioSampleSizeReserved;
    sound_type = SrsCodecAudioSoundTypeReserved;
    aac_packet_type = SrsCodecAudioTypeReserved;
}

int SrsCodecSample::add_sample_unit(char* bytes, int size)
{
    int ret = ERROR_SUCCESS;
    
    if (nb_sample_units >= SRS_SRS_MAX_CODEC_SAMPLE) {
        ret = ERROR_HLS_DECODE_ERROR;
        srs_error("hls decode samples error, "
            "exceed the max count: %d, ret=%d", SRS_SRS_MAX_CODEC_SAMPLE, ret);
        return ret;
    }
    
    SrsCodecSampleUnit* sample_unit = &sample_units[nb_sample_units++];
    sample_unit->bytes = bytes;
    sample_unit->size = size;
    
    // for video, parse the nalu type, set the IDR flag.
    if (is_video) {
        SrsAvcNaluType nal_unit_type = (SrsAvcNaluType)(bytes[0] & 0x1f);
        
        if (nal_unit_type == SrsAvcNaluTypeIDR) {
            has_idr = true;
        }
    
        if (first_nalu_type == SrsAvcNaluTypeReserved) {
            first_nalu_type = nal_unit_type;
        }
    }
    
    return ret;
}

SrsAvcAacCodec::SrsAvcAacCodec()
{
    width                       = 0;
    height                      = 0;
    duration                    = 0;
    NAL_unit_length             = 0;
    frame_rate                  = 0;

    video_data_rate             = 0;
    video_codec_id              = 0;

    audio_data_rate             = 0;
    audio_codec_id              = 0;

    avc_profile                 = SrsAvcProfileReserved;
    avc_level                   = SrsAvcLevelReserved;
    aac_object                  = SrsAacObjectTypeReserved;
    aac_sample_rate             = SRS_AAC_SAMPLE_RATE_UNSET; // sample rate ignored
    aac_channels                = 0;
    avc_extra_size              = 0;
    avc_extra_data              = NULL;
    aac_extra_size              = 0;
    aac_extra_data              = NULL;

    sequenceParameterSetLength  = 0;
    sequenceParameterSetNALUnit = NULL;
    pictureParameterSetLength   = 0;
    pictureParameterSetNALUnit  = NULL;

    payload_format = SrsAvcPayloadFormatGuess;
    stream = new SrsStream();
}

SrsAvcAacCodec::~SrsAvcAacCodec()
{
    srs_freep(avc_extra_data);
    srs_freep(aac_extra_data);

    srs_freep(stream);
    srs_freep(sequenceParameterSetNALUnit);
    srs_freep(pictureParameterSetNALUnit);
}

int SrsAvcAacCodec::audio_aac_demux(char* data, int size, SrsCodecSample* sample)
{
    int ret = ERROR_SUCCESS;
    
    sample->is_video = false;
    
    if (!data || size <= 0) {
        srs_trace("no audio present, ignore it.");
        return ret;
    }
    
    if ((ret = stream->initialize(data, size)) != ERROR_SUCCESS) {
        return ret;
    }

    // audio decode
    if (!stream->require(1)) {
        ret = ERROR_HLS_DECODE_ERROR;
        srs_error("audio codec decode sound_format failed. ret=%d", ret);
        return ret;
    }
    
    // @see: E.4.2 Audio Tags, video_file_format_spec_v10_1.pdf, page 76
    int8_t sound_format = stream->read_1bytes();
    
    int8_t sound_type = sound_format & 0x01;
    int8_t sound_size = (sound_format >> 1) & 0x01;
    int8_t sound_rate = (sound_format >> 2) & 0x03;
    sound_format = (sound_format >> 4) & 0x0f;
    
    audio_codec_id = sound_format;
    sample->acodec = (SrsCodecAudio)audio_codec_id;

    sample->sound_type = (SrsCodecAudioSoundType)sound_type;
    sample->sound_rate = (SrsCodecAudioSampleRate)sound_rate;
    sample->sound_size = (SrsCodecAudioSampleSize)sound_size;

    // we support h.264+mp3 for hls.
    if (audio_codec_id == SrsCodecAudioMP3) {
        return ERROR_HLS_TRY_MP3;
    }
    
    // only support aac
    if (audio_codec_id != SrsCodecAudioAAC) {
        ret = ERROR_HLS_DECODE_ERROR;
        srs_error("audio codec only support mp3/aac codec. actual=%d, ret=%d", audio_codec_id, ret);
        return ret;
    }

    if (!stream->require(1)) {
        ret = ERROR_HLS_DECODE_ERROR;
        srs_error("audio codec decode aac_packet_type failed. ret=%d", ret);
        return ret;
    }
    
    int8_t aac_packet_type = stream->read_1bytes();
    sample->aac_packet_type = (SrsCodecAudioType)aac_packet_type;
    
    if (aac_packet_type == SrsCodecAudioTypeSequenceHeader) {
        // AudioSpecificConfig
        // 1.6.2.1 AudioSpecificConfig, in aac-mp4a-format-ISO_IEC_14496-3+2001.pdf, page 33.
        aac_extra_size = stream->size() - stream->pos();
        if (aac_extra_size > 0) {
            srs_freep(aac_extra_data);
            aac_extra_data = new char[aac_extra_size];
            memcpy(aac_extra_data, stream->data() + stream->pos(), aac_extra_size);

            // demux the sequence header.
            if ((ret = audio_aac_sequence_header_demux(aac_extra_data, aac_extra_size)) != ERROR_SUCCESS) {
                return ret;
            }
        }
    } else if (aac_packet_type == SrsCodecAudioTypeRawData) {
        // ensure the sequence header demuxed
        if (aac_extra_size <= 0 || !aac_extra_data) {
            ret = ERROR_HLS_DECODE_ERROR;
            srs_error("audio codec decode aac failed, sequence header not found. ret=%d", ret);
            return ret;
        }
        
        // Raw AAC frame data in UI8 []
        // 6.3 Raw Data, aac-iso-13818-7.pdf, page 28
        if ((ret = sample->add_sample_unit(stream->data() + stream->pos(), stream->size() - stream->pos())) != ERROR_SUCCESS) {
            srs_error("audio codec add sample failed. ret=%d", ret);
            return ret;
        }
    } else {
        // ignored.
    }
    
    // reset the sample rate by sequence header
    if (aac_sample_rate != SRS_AAC_SAMPLE_RATE_UNSET) {
        static int aac_sample_rates[] = {
            96000, 88200, 64000, 48000,
            44100, 32000, 24000, 22050,
            16000, 12000, 11025,  8000,
            7350,     0,     0,    0
        };
        switch (aac_sample_rates[aac_sample_rate]) {
            case 11025:
                sample->sound_rate = SrsCodecAudioSampleRate11025;
                break;
            case 22050:
                sample->sound_rate = SrsCodecAudioSampleRate22050;
                break;
            case 44100:
                sample->sound_rate = SrsCodecAudioSampleRate44100;
                break;
            default:
                break;
        };
    }
    
    srs_info("audio decoded, type=%d, codec=%d, asize=%d, rate=%d, format=%d, size=%d", 
        sound_type, audio_codec_id, sound_size, sound_rate, sound_format, size);
    
    return ret;
}

int SrsAvcAacCodec::audio_mp3_demux(char* data, int size, SrsCodecSample* sample)
{
    int ret = ERROR_SUCCESS;

    // we always decode aac then mp3.
    srs_assert(sample->acodec == SrsCodecAudioMP3);
    
    // @see: E.4.2 Audio Tags, video_file_format_spec_v10_1.pdf, page 76
    if (!data || size <= 1) {
        srs_trace("no mp3 audio present, ignore it.");
        return ret;
    }

    // mp3 payload.
    if ((ret = sample->add_sample_unit(data + 1, size - 1)) != ERROR_SUCCESS) {
        srs_error("audio codec add mp3 sample failed. ret=%d", ret);
        return ret;
    }
    
    srs_info("audio decoded, type=%d, codec=%d, asize=%d, rate=%d, format=%d, size=%d", 
        sample->sound_type, audio_codec_id, sample->sound_size, sample->sound_rate, sample->acodec, size);
    
    return ret;
}

int SrsAvcAacCodec::audio_aac_sequence_header_demux(char* data, int size)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = stream->initialize(data, size)) != ERROR_SUCCESS) {
        return ret;
    }
        
    // only need to decode the first 2bytes:
    //      audioObjectType, aac_profile, 5bits.
    //      samplingFrequencyIndex, aac_sample_rate, 4bits.
    //      channelConfiguration, aac_channels, 4bits
    if (!stream->require(2)) {
        ret = ERROR_HLS_DECODE_ERROR;
        srs_error("audio codec decode aac sequence header failed. ret=%d", ret);
        return ret;
    }
    u_int8_t profile_ObjectType = stream->read_1bytes();
    u_int8_t samplingFrequencyIndex = stream->read_1bytes();
        
    aac_channels = (samplingFrequencyIndex >> 3) & 0x0f;
    samplingFrequencyIndex = ((profile_ObjectType << 1) & 0x0e) | ((samplingFrequencyIndex >> 7) & 0x01);
    profile_ObjectType = (profile_ObjectType >> 3) & 0x1f;

    // set the aac sample rate.
    aac_sample_rate = samplingFrequencyIndex;

    // convert the object type in sequence header to aac profile of ADTS.
    aac_object = (SrsAacObjectType)profile_ObjectType;
    if (aac_object == SrsAacObjectTypeReserved) {
        ret = ERROR_HLS_DECODE_ERROR;
        srs_error("audio codec decode aac sequence header failed, "
            "adts object=%d invalid. ret=%d", profile_ObjectType, ret);
        return ret;
    }
        
    // TODO: FIXME: to support aac he/he-v2, see: ngx_rtmp_codec_parse_aac_header
    // @see: https://github.com/winlinvip/nginx-rtmp-module/commit/3a5f9eea78fc8d11e8be922aea9ac349b9dcbfc2
    // 
    // donot force to LC, @see: https://github.com/simple-rtmp-server/srs/issues/81
    // the source will print the sequence header info.
    //if (aac_profile > 3) {
        // Mark all extended profiles as LC
        // to make Android as happy as possible.
        // @see: ngx_rtmp_hls_parse_aac_header
        //aac_profile = 1;
    //}

    return ret;
}

int SrsAvcAacCodec::video_avc_demux(char* data, int size, SrsCodecSample* sample)
{
    int ret = ERROR_SUCCESS;
    
    sample->is_video = true;
    
    if (!data || size <= 0) {
        srs_trace("no video present, ignore it.");
        return ret;
    }
    
    if ((ret = stream->initialize(data, size)) != ERROR_SUCCESS) {
        return ret;
    }

    // video decode
    if (!stream->require(1)) {
        ret = ERROR_HLS_DECODE_ERROR;
        srs_error("video codec decode frame_type failed. ret=%d", ret);
        return ret;
    }
    
    // @see: E.4.3 Video Tags, video_file_format_spec_v10_1.pdf, page 78
    int8_t frame_type = stream->read_1bytes();
    int8_t codec_id = frame_type & 0x0f;
    frame_type = (frame_type >> 4) & 0x0f;
    
    sample->frame_type = (SrsCodecVideoAVCFrame)frame_type;
    
    // ignore info frame without error,
    // @see https://github.com/simple-rtmp-server/srs/issues/288#issuecomment-69863909
    if (sample->frame_type == SrsCodecVideoAVCFrameVideoInfoFrame) {
        srs_warn("video codec igone the info frame, ret=%d", ret);
        return ret;
    }
    
    // only support h.264/avc
    if (codec_id != SrsCodecVideoAVC) {
        ret = ERROR_HLS_DECODE_ERROR;
        srs_error("video codec only support video h.264/avc codec. actual=%d, ret=%d", codec_id, ret);
        return ret;
    }
    video_codec_id = codec_id;
    
    if (!stream->require(4)) {
        ret = ERROR_HLS_DECODE_ERROR;
        srs_error("video codec decode avc_packet_type failed. ret=%d", ret);
        return ret;
    }
    int8_t avc_packet_type = stream->read_1bytes();
    int32_t composition_time = stream->read_3bytes();
    
    // pts = dts + cts.
    sample->cts = composition_time;
    sample->avc_packet_type = (SrsCodecVideoAVCType)avc_packet_type;
    
    if (avc_packet_type == SrsCodecVideoAVCTypeSequenceHeader) {
        if ((ret = avc_demux_sps_pps(stream)) != ERROR_SUCCESS) {
            return ret;
        }
    } else if (avc_packet_type == SrsCodecVideoAVCTypeNALU){
        // ensure the sequence header demuxed
        if (avc_extra_size <= 0 || !avc_extra_data) {
            ret = ERROR_HLS_DECODE_ERROR;
            srs_error("avc decode failed, sequence header not found. ret=%d", ret);
            return ret;
        }

        // guess for the first time.
        if (payload_format == SrsAvcPayloadFormatGuess) {
            // One or more NALUs (Full frames are required)
            // try  "AnnexB" from H.264-AVC-ISO_IEC_14496-10.pdf, page 211.
            if ((ret = avc_demux_annexb_format(stream, sample)) != ERROR_SUCCESS) {
                // stop try when system error.
                if (ret != ERROR_HLS_AVC_TRY_OTHERS) {
                    srs_error("avc demux for annexb failed. ret=%d", ret);
                    return ret;
                }
                
                // try "ISO Base Media File Format" from H.264-AVC-ISO_IEC_14496-15.pdf, page 20
                if ((ret = avc_demux_ibmf_format(stream, sample)) != ERROR_SUCCESS) {
                    return ret;
                } else {
                    payload_format = SrsAvcPayloadFormatIbmf;
                    srs_info("hls guess avc payload is ibmf format.");
                }
            } else {
                payload_format = SrsAvcPayloadFormatAnnexb;
                srs_info("hls guess avc payload is annexb format.");
            }
        } else if (payload_format == SrsAvcPayloadFormatIbmf) {
            // try "ISO Base Media File Format" from H.264-AVC-ISO_IEC_14496-15.pdf, page 20
            if ((ret = avc_demux_ibmf_format(stream, sample)) != ERROR_SUCCESS) {
                return ret;
            }
            srs_info("hls decode avc payload in ibmf format.");
        } else {
            // One or more NALUs (Full frames are required)
            // try  "AnnexB" from H.264-AVC-ISO_IEC_14496-10.pdf, page 211.
            if ((ret = avc_demux_annexb_format(stream, sample)) != ERROR_SUCCESS) {
                // ok, we guess out the payload is annexb, but maybe changed to ibmf.
                if (ret != ERROR_HLS_AVC_TRY_OTHERS) {
                    srs_error("avc demux for annexb failed. ret=%d", ret);
                    return ret;
                }
                
                // try "ISO Base Media File Format" from H.264-AVC-ISO_IEC_14496-15.pdf, page 20
                if ((ret = avc_demux_ibmf_format(stream, sample)) != ERROR_SUCCESS) {
                    return ret;
                } else {
                    payload_format = SrsAvcPayloadFormatIbmf;
                    srs_warn("hls avc payload change from annexb to ibmf format.");
                }
            }
            srs_info("hls decode avc payload in annexb format.");
        }
    } else {
        // ignored.
    }
    
    srs_info("video decoded, type=%d, codec=%d, avc=%d, cts=%d, size=%d", 
        frame_type, video_codec_id, avc_packet_type, composition_time, size);
    
    return ret;
}

int SrsAvcAacCodec::avc_demux_sps_pps(SrsStream* stream)
{
    int ret = ERROR_SUCCESS;
    
    // AVCDecoderConfigurationRecord
    // 5.2.4.1.1 Syntax, H.264-AVC-ISO_IEC_14496-15.pdf, page 16
    avc_extra_size = stream->size() - stream->pos();
    if (avc_extra_size > 0) {
        srs_freep(avc_extra_data);
        avc_extra_data = new char[avc_extra_size];
        memcpy(avc_extra_data, stream->data() + stream->pos(), avc_extra_size);
    }
    
    if (!stream->require(6)) {
        ret = ERROR_HLS_DECODE_ERROR;
        srs_error("avc decode sequenc header failed. ret=%d", ret);
        return ret;
    }
    //int8_t configurationVersion = stream->read_1bytes();
    stream->read_1bytes();
    //int8_t AVCProfileIndication = stream->read_1bytes();
    avc_profile = (SrsAvcProfile)stream->read_1bytes();
    //int8_t profile_compatibility = stream->read_1bytes();
    stream->read_1bytes();
    //int8_t AVCLevelIndication = stream->read_1bytes();
    avc_level = (SrsAvcLevel)stream->read_1bytes();
    
    // parse the NALU size.
    int8_t lengthSizeMinusOne = stream->read_1bytes();
    lengthSizeMinusOne &= 0x03;
    NAL_unit_length = lengthSizeMinusOne;
    
    // 5.3.4.2.1 Syntax, H.264-AVC-ISO_IEC_14496-15.pdf, page 16
    // 5.2.4.1 AVC decoder configuration record
    // 5.2.4.1.2 Semantics
    // The value of this field shall be one of 0, 1, or 3 corresponding to a
    // length encoded with 1, 2, or 4 bytes, respectively.
    if (NAL_unit_length == 2) {
        ret = ERROR_HLS_DECODE_ERROR;
        srs_error("sps lengthSizeMinusOne should never be 2. ret=%d", ret);
        return ret;
    }
    
    // 1 sps, 7.3.2.1 Sequence parameter set RBSP syntax
    // H.264-AVC-ISO_IEC_14496-10.pdf, page 45.
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
    sequenceParameterSetLength = stream->read_2bytes();
    if (!stream->require(sequenceParameterSetLength)) {
        ret = ERROR_HLS_DECODE_ERROR;
        srs_error("avc decode sequenc header sps data failed. ret=%d", ret);
        return ret;
    }
    if (sequenceParameterSetLength > 0) {
        srs_freep(sequenceParameterSetNALUnit);
        sequenceParameterSetNALUnit = new char[sequenceParameterSetLength];
        stream->read_bytes(sequenceParameterSetNALUnit, sequenceParameterSetLength);
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
    pictureParameterSetLength = stream->read_2bytes();
    if (!stream->require(pictureParameterSetLength)) {
        ret = ERROR_HLS_DECODE_ERROR;
        srs_error("avc decode sequenc header pps data failed. ret=%d", ret);
        return ret;
    }
    if (pictureParameterSetLength > 0) {
        srs_freep(pictureParameterSetNALUnit);
        pictureParameterSetNALUnit = new char[pictureParameterSetLength];
        stream->read_bytes(pictureParameterSetNALUnit, pictureParameterSetLength);
    }
    
    return avc_demux_sps();
}

int SrsAvcAacCodec::avc_demux_sps()
{
    int ret = ERROR_SUCCESS;
    
    if (!sequenceParameterSetLength) {
        return ret;
    }
    
    SrsStream stream;
    if ((ret = stream.initialize(sequenceParameterSetNALUnit, sequenceParameterSetLength)) != ERROR_SUCCESS) {
        return ret;
    }
    
    // for NALU, 7.3.1 NAL unit syntax
    // H.264-AVC-ISO_IEC_14496-10-2012.pdf, page 61.
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
    // H.264-AVC-ISO_IEC_14496-10-2012.pdf, page 61.
    // nal_unit_type specifies the type of RBSP data structure contained in the NAL unit as specified in Table 7-1.
    SrsAvcNaluType nal_unit_type = (SrsAvcNaluType)(nutv & 0x1f);
    if (nal_unit_type != 7) {
        ret = ERROR_HLS_DECODE_ERROR;
        srs_error("for sps, nal_unit_type shall be equal to 7. ret=%d", ret);
        return ret;
    }
    
    // decode the rbsp from sps.
    // rbsp[ i ] a raw byte sequence payload is specified as an ordered sequence of bytes.
    int8_t* rbsp = new int8_t[sequenceParameterSetLength];
    SrsAutoFree(int8_t, rbsp);
    
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


int SrsAvcAacCodec::avc_demux_sps_rbsp(char* rbsp, int nb_rbsp)
{
    int ret = ERROR_SUCCESS;
    
    // reparse the rbsp.
    SrsStream stream;
    if ((ret = stream.initialize(rbsp, nb_rbsp)) != ERROR_SUCCESS) {
        return ret;
    }
    
    // for SPS, 7.3.2.1.1 Sequence parameter set data syntax
    // H.264-AVC-ISO_IEC_14496-10-2012.pdf, page 62.
    if (!stream.require(3)) {
        ret = ERROR_HLS_DECODE_ERROR;
        srs_error("sps shall atleast 3bytes. ret=%d", ret);
        return ret;
    }
    u_int8_t profile_idc = stream.read_1bytes();
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
    
    u_int8_t level_idc = stream.read_1bytes();
    if (!level_idc) {
        ret = ERROR_HLS_DECODE_ERROR;
        srs_error("sps the level_idc invalid. ret=%d", ret);
        return ret;
    }
    
    SrsBitStream bs;
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
    
    if (profile_idc == 100 || profile_idc == 110 || profile_idc == 122 || profile_idc == 244
        || profile_idc == 44 || profile_idc == 83 || profile_idc == 86 || profile_idc == 118
        || profile_idc == 128
    ) {
        int32_t chroma_format_idc = -1;
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
            ret = ERROR_HLS_DECODE_ERROR;
            srs_error("sps the seq_scaling_matrix_present_flag invalid. ret=%d", ret);
            return ret;
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
        if (num_ref_frames_in_pic_order_cnt_cycle) {
            ret = ERROR_HLS_DECODE_ERROR;
            srs_error("sps the num_ref_frames_in_pic_order_cnt_cycle invalid. ret=%d", ret);
            return ret;
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
    
    width = (int)(pic_width_in_mbs_minus1 + 1) * 16;
    height = (int)(pic_height_in_map_units_minus1 + 1) * 16;
    
    return ret;
}

int SrsAvcAacCodec::avc_demux_annexb_format(SrsStream* stream, SrsCodecSample* sample)
{
    int ret = ERROR_SUCCESS;
    
    // not annexb, try others
    if (!srs_avc_startswith_annexb(stream, NULL)) {
        return ERROR_HLS_AVC_TRY_OTHERS;
    }
    
    // AnnexB
    // B.1.1 Byte stream NAL unit syntax,
    // H.264-AVC-ISO_IEC_14496-10.pdf, page 211.
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
        if ((ret = sample->add_sample_unit(p, pp - p)) != ERROR_SUCCESS) {
            srs_error("annexb add video sample failed. ret=%d", ret);
            return ret;
        }
    }
    
    return ret;
}

int SrsAvcAacCodec::avc_demux_ibmf_format(SrsStream* stream, SrsCodecSample* sample)
{
    int ret = ERROR_SUCCESS;
    
    int PictureLength = stream->size() - stream->pos();
    
    // 5.3.4.2.1 Syntax, H.264-AVC-ISO_IEC_14496-15.pdf, page 16
    // 5.2.4.1 AVC decoder configuration record
    // 5.2.4.1.2 Semantics
    // The value of this field shall be one of 0, 1, or 3 corresponding to a
    // length encoded with 1, 2, or 4 bytes, respectively.
    srs_assert(NAL_unit_length != 2);
    
    // 5.3.4.2.1 Syntax, H.264-AVC-ISO_IEC_14496-15.pdf, page 20
    for (int i = 0; i < PictureLength;) {
        // unsigned int((NAL_unit_length+1)*8) NALUnitLength;
        if (!stream->require(NAL_unit_length + 1)) {
            ret = ERROR_HLS_DECODE_ERROR;
            srs_error("avc decode NALU size failed. ret=%d", ret);
            return ret;
        }
        int32_t NALUnitLength = 0;
        if (NAL_unit_length == 3) {
            NALUnitLength = stream->read_4bytes();
        } else if (NAL_unit_length == 1) {
            NALUnitLength = stream->read_2bytes();
        } else {
            NALUnitLength = stream->read_1bytes();
        }
        
        // maybe stream is invalid format.
        // see: https://github.com/simple-rtmp-server/srs/issues/183
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
        // 7.3.1 NAL unit syntax, H.264-AVC-ISO_IEC_14496-10.pdf, page 44.
        if ((ret = sample->add_sample_unit(stream->data() + stream->pos(), NALUnitLength)) != ERROR_SUCCESS) {
            srs_error("avc add video sample failed. ret=%d", ret);
            return ret;
        }
        stream->skip(NALUnitLength);
        
        i += NAL_unit_length + 1 + NALUnitLength;
    }
    
    return ret;
}

