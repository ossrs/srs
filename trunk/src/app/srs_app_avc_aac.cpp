/*
The MIT License (MIT)

Copyright (c) 2013-2014 winlin

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

#include <srs_app_avc_aac.hpp>

#include <stdlib.h>

#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_kernel_codec.hpp>
#include <srs_kernel_stream.hpp>

SrsCodecBuffer::SrsCodecBuffer()
{
    size = 0;
    bytes = NULL;
}

void SrsCodecBuffer::append(void* data, int len)
{
    srs_assert(data);
    srs_assert(len > 0);

    bytes = (char*)realloc(bytes, size + len);
    memcpy(bytes + size, data, len);
    size += len;
}

void SrsCodecBuffer::free()
{
    size = 0;
    srs_freep(bytes);
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
    nb_buffers = 0;

    cts = 0;
    frame_type = SrsCodecVideoAVCFrameReserved;
    avc_packet_type = SrsCodecVideoAVCTypeReserved;
    
    sound_rate = SrsCodecAudioSampleRateReserved;
    sound_size = SrsCodecAudioSampleSizeReserved;
    sound_type = SrsCodecAudioSoundTypeReserved;
    aac_packet_type = SrsCodecAudioTypeReserved;
}

int SrsCodecSample::add_sample(char* bytes, int size)
{
    int ret = ERROR_SUCCESS;
    
    if (nb_buffers >= SRS_MAX_CODEC_SAMPLE) {
        ret = ERROR_HLS_DECODE_ERROR;
        srs_error("hls decode samples error, "
            "exceed the max count: %d, ret=%d", SRS_MAX_CODEC_SAMPLE, ret);
        return ret;
    }
    
    SrsCodecBuffer* buf = &buffers[nb_buffers++];
    buf->bytes = bytes;
    buf->size = size;
    
    return ret;
}

SrsAvcAacCodec::SrsAvcAacCodec()
{
    width                     = 0;
    height                     = 0;
    duration                 = 0;
    NAL_unit_length            = 0;
    frame_rate                 = 0;
    video_data_rate         = 0;
    video_codec_id             = 0;
    audio_data_rate         = 0;
    audio_codec_id             = 0;
    avc_profile             = 0;
    avc_level                 = 0;
    aac_profile                = 0;
    aac_sample_rate            = 0;
    aac_channels            = 0;
    avc_extra_size             = 0;
    avc_extra_data             = NULL;
    aac_extra_size             = 0;
    aac_extra_data             = NULL;
    sequenceParameterSetLength = 0;
    sequenceParameterSetNALUnit = NULL;
    pictureParameterSetLength = 0;
    pictureParameterSetNALUnit = NULL;

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

int SrsAvcAacCodec::audio_aac_demux(int8_t* data, int size, SrsCodecSample* sample)
{
    int ret = ERROR_SUCCESS;
    
    sample->is_video = false;
    
    if (!data || size <= 0) {
        srs_trace("no audio present, hls ignore it.");
        return ret;
    }
    
    if ((ret = stream->initialize((char*)data, size)) != ERROR_SUCCESS) {
        return ret;
    }

    // audio decode
    if (!stream->require(1)) {
        ret = ERROR_HLS_DECODE_ERROR;
        srs_error("hls decode audio sound_format failed. ret=%d", ret);
        return ret;
    }
    
    int8_t sound_format = stream->read_1bytes();
    
    int8_t sound_type = sound_format & 0x01;
    int8_t sound_size = (sound_format >> 1) & 0x01;
    int8_t sound_rate = (sound_format >> 2) & 0x03;
    sound_format = (sound_format >> 4) & 0x0f;
    
    audio_codec_id = sound_format;
    sample->sound_type = (SrsCodecAudioSoundType)sound_type;
    sample->sound_rate = (SrsCodecAudioSampleRate)sound_rate;
    sample->sound_size = (SrsCodecAudioSampleSize)sound_size;
    
    // reset the sample rate by sequence header
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
    };
    
    // only support aac
    if (audio_codec_id != SrsCodecAudioAAC) {
        ret = ERROR_HLS_DECODE_ERROR;
        srs_error("hls only support audio aac codec. actual=%d, ret=%d", audio_codec_id, ret);
        return ret;
    }

    if (!stream->require(1)) {
        ret = ERROR_HLS_DECODE_ERROR;
        srs_error("hls decode audio aac_packet_type failed. ret=%d", ret);
        return ret;
    }
    
    int8_t aac_packet_type = stream->read_1bytes();
    sample->aac_packet_type = (SrsCodecAudioType)aac_packet_type;
    
    if (aac_packet_type == SrsCodecAudioTypeSequenceHeader) {
        // AudioSpecificConfig
        // 1.6.2.1 AudioSpecificConfig, in aac-mp4a-format-ISO_IEC_14496-3+2001.pdf, page 33.
        aac_extra_size = stream->left();
        if (aac_extra_size > 0) {
            srs_freep(aac_extra_data);
            aac_extra_data = new char[aac_extra_size];
            memcpy(aac_extra_data, stream->current(), aac_extra_size);
        }
        
        // only need to decode the first 2bytes:
        // audioObjectType, aac_profile, 5bits.
        // samplingFrequencyIndex, aac_sample_rate, 4bits.
        // channelConfiguration, aac_channels, 4bits
        if (!stream->require(2)) {
            ret = ERROR_HLS_DECODE_ERROR;
            srs_error("hls decode audio aac sequence header failed. ret=%d", ret);
            return ret;
        }
        aac_profile = stream->read_1bytes();
        aac_sample_rate = stream->read_1bytes();
        
        aac_channels = (aac_sample_rate >> 3) & 0x0f;
        aac_sample_rate = ((aac_profile << 1) & 0x0e) | ((aac_sample_rate >> 7) & 0x01);
        aac_profile = (aac_profile >> 3) & 0x1f;
        
        if (aac_profile == 0 || aac_profile == 0x1f) {
            ret = ERROR_HLS_DECODE_ERROR;
            srs_error("hls decode audio aac sequence header failed, "
                "adts object=%d invalid. ret=%d", aac_profile, ret);
            return ret;
        }
        
        // aac_profile = audioObjectType - 1
        aac_profile--;
        
        if (aac_profile > 3) {
            // Mark all extended profiles as LC
            // to make Android as happy as possible.
            // @see: ngx_rtmp_hls_parse_aac_header
            aac_profile = 1;
        }
    } else if (aac_packet_type == SrsCodecAudioTypeRawData) {
        // ensure the sequence header demuxed
        if (aac_extra_size <= 0 || !aac_extra_data) {
            ret = ERROR_HLS_DECODE_ERROR;
            srs_error("hls decode audio aac failed, sequence header not found. ret=%d", ret);
            return ret;
        }
        
        // Raw AAC frame data in UI8 []
        // 6.3 Raw Data, aac-iso-13818-7.pdf, page 28
        if ((ret = sample->add_sample(stream->current(), stream->left())) != ERROR_SUCCESS) {
            srs_error("hls add audio sample failed. ret=%d", ret);
            return ret;
        }
    } else {
        // ignored.
    }
    
    srs_info("audio decoded, type=%d, codec=%d, asize=%d, rate=%d, format=%d, size=%d", 
        sound_type, audio_codec_id, sound_size, sound_rate, sound_format, size);
    
    return ret;
}

int SrsAvcAacCodec::video_avc_demux(int8_t* data, int size, SrsCodecSample* sample)
{
    int ret = ERROR_SUCCESS;
    
    sample->is_video = true;
    
    if (!data || size <= 0) {
        srs_trace("no video present, hls ignore it.");
        return ret;
    }
    
    if ((ret = stream->initialize((char*)data, size)) != ERROR_SUCCESS) {
        return ret;
    }

    // video decode
    if (!stream->require(1)) {
        ret = ERROR_HLS_DECODE_ERROR;
        srs_error("hls decode video frame_type failed. ret=%d", ret);
        return ret;
    }
    
    int8_t frame_type = stream->read_1bytes();
    int8_t codec_id = frame_type & 0x0f;
    frame_type = (frame_type >> 4) & 0x0f;
    
    sample->frame_type = (SrsCodecVideoAVCFrame)frame_type;
    
    // only support h.264/avc
    if (codec_id != SrsCodecVideoAVC) {
        ret = ERROR_HLS_DECODE_ERROR;
        srs_error("hls only support video h.264/avc codec. actual=%d, ret=%d", codec_id, ret);
        return ret;
    }
    video_codec_id = codec_id;
    
    if (!stream->require(4)) {
        ret = ERROR_HLS_DECODE_ERROR;
        srs_error("hls decode video avc_packet_type failed. ret=%d", ret);
        return ret;
    }
    int8_t avc_packet_type = stream->read_1bytes();
    int32_t composition_time = stream->read_3bytes();
    
    // pts = dts + cts.
    sample->cts = composition_time;
    sample->avc_packet_type = (SrsCodecVideoAVCType)avc_packet_type;
    
    if (avc_packet_type == SrsCodecVideoAVCTypeSequenceHeader) {
        // AVCDecoderConfigurationRecord
        // 5.2.4.1.1 Syntax, H.264-AVC-ISO_IEC_14496-15.pdf, page 16
        avc_extra_size = stream->left();
        if (avc_extra_size > 0) {
            srs_freep(avc_extra_data);
            avc_extra_data = new char[avc_extra_size];
            memcpy(avc_extra_data, stream->current(), avc_extra_size);
        }
        
        if (!stream->require(6)) {
            ret = ERROR_HLS_DECODE_ERROR;
            srs_error("hls decode video avc sequenc header failed. ret=%d", ret);
            return ret;
        }
        //int8_t configurationVersion = stream->read_1bytes();
        //int8_t AVCProfileIndication = stream->read_1bytes();
        //int8_t profile_compatibility = stream->read_1bytes();
        //int8_t AVCLevelIndication = stream->read_1bytes();
        stream->skip(4);
        // parse the NALU size.
        int8_t lengthSizeMinusOne = stream->read_1bytes();
        lengthSizeMinusOne &= 0x03;
        NAL_unit_length = lengthSizeMinusOne;
        
        // 1 sps
        if (!stream->require(1)) {
            ret = ERROR_HLS_DECODE_ERROR;
            srs_error("hls decode video avc sequenc header sps failed. ret=%d", ret);
            return ret;
        }
        int8_t numOfSequenceParameterSets = stream->read_1bytes();
        numOfSequenceParameterSets &= 0x1f;
        if (numOfSequenceParameterSets != 1) {
            ret = ERROR_HLS_DECODE_ERROR;
            srs_error("hls decode video avc sequenc header sps failed. ret=%d", ret);
            return ret;
        }
        if (!stream->require(2)) {
            ret = ERROR_HLS_DECODE_ERROR;
            srs_error("hls decode video avc sequenc header sps size failed. ret=%d", ret);
            return ret;
        }
        sequenceParameterSetLength = stream->read_2bytes();
        if (!stream->require(sequenceParameterSetLength)) {
            ret = ERROR_HLS_DECODE_ERROR;
            srs_error("hls decode video avc sequenc header sps data failed. ret=%d", ret);
            return ret;
        }
        if (sequenceParameterSetLength > 0) {
            srs_freep(sequenceParameterSetNALUnit);
            sequenceParameterSetNALUnit = new char[sequenceParameterSetLength];
            memcpy(sequenceParameterSetNALUnit, stream->current(), sequenceParameterSetLength);
            stream->skip(sequenceParameterSetLength);
        }
        // 1 pps
        if (!stream->require(1)) {
            ret = ERROR_HLS_DECODE_ERROR;
            srs_error("hls decode video avc sequenc header pps failed. ret=%d", ret);
            return ret;
        }
        int8_t numOfPictureParameterSets = stream->read_1bytes();
        numOfPictureParameterSets &= 0x1f;
        if (numOfPictureParameterSets != 1) {
            ret = ERROR_HLS_DECODE_ERROR;
            srs_error("hls decode video avc sequenc header pps failed. ret=%d", ret);
            return ret;
        }
        if (!stream->require(2)) {
            ret = ERROR_HLS_DECODE_ERROR;
            srs_error("hls decode video avc sequenc header pps size failed. ret=%d", ret);
            return ret;
        }
        pictureParameterSetLength = stream->read_2bytes();
        if (!stream->require(pictureParameterSetLength)) {
            ret = ERROR_HLS_DECODE_ERROR;
            srs_error("hls decode video avc sequenc header pps data failed. ret=%d", ret);
            return ret;
        }
        if (pictureParameterSetLength > 0) {
            srs_freep(pictureParameterSetNALUnit);
            pictureParameterSetNALUnit = new char[pictureParameterSetLength];
            memcpy(pictureParameterSetNALUnit, stream->current(), pictureParameterSetLength);
            stream->skip(pictureParameterSetLength);
        }
    } else if (avc_packet_type == SrsCodecVideoAVCTypeNALU){
        // ensure the sequence header demuxed
        if (avc_extra_size <= 0 || !avc_extra_data) {
            ret = ERROR_HLS_DECODE_ERROR;
            srs_error("hls decode video avc failed, sequence header not found. ret=%d", ret);
            return ret;
        }
        
        // One or more NALUs (Full frames are required)
        // 5.3.4.2.1 Syntax, H.264-AVC-ISO_IEC_14496-15.pdf, page 20
        int PictureLength = stream->left();
        for (int i = 0; i < PictureLength;) {
            if (!stream->require(NAL_unit_length + 1)) {
                ret = ERROR_HLS_DECODE_ERROR;
                srs_error("hls decode video avc NALU size failed. ret=%d", ret);
                return ret;
            }
            int32_t NALUnitLength = 0;
            if (NAL_unit_length == 3) {
                NALUnitLength = stream->read_4bytes();
            } else if (NALUnitLength == 2) {
                NALUnitLength = stream->read_3bytes();
            } else if (NALUnitLength == 1) {
                NALUnitLength = stream->read_2bytes();
            } else {
                NALUnitLength = stream->read_1bytes();
            }
            // NALUnit
            if (!stream->require(NALUnitLength)) {
                ret = ERROR_HLS_DECODE_ERROR;
                srs_error("hls decode video avc NALU data failed. ret=%d", ret);
                return ret;
            }
            // 7.3.1 NAL unit syntax, H.264-AVC-ISO_IEC_14496-10.pdf, page 44.
            if ((ret = sample->add_sample(stream->current(), NALUnitLength)) != ERROR_SUCCESS) {
                srs_error("hls add video sample failed. ret=%d", ret);
                return ret;
            }
            stream->skip(NALUnitLength);
            
            i += NAL_unit_length + 1 + NALUnitLength;
        }
    } else {
        // ignored.
    }
    
    srs_info("video decoded, type=%d, codec=%d, avc=%d, time=%d, size=%d", 
        frame_type, video_codec_id, avc_packet_type, composition_time, size);
    
    return ret;
}
