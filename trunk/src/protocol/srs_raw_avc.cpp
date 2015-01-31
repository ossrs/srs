/*
The MIT License (MIT)

Copyright (c) 2013-2015 winlin

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

#include <srs_raw_avc.hpp>

#include <string.h>
using namespace std;

#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_kernel_stream.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_codec.hpp>

SrsRawH264Stream::SrsRawH264Stream()
{
}

SrsRawH264Stream::~SrsRawH264Stream()
{
}

int SrsRawH264Stream::annexb_demux(SrsStream* stream, char** pframe, int* pnb_frame)
{
    int ret = ERROR_SUCCESS;

    *pframe = NULL;
    *pnb_frame = 0;

    while (!stream->empty()) {
        // each frame must prefixed by annexb format.
        // about annexb, @see H.264-AVC-ISO_IEC_14496-10.pdf, page 211.
        int pnb_start_code = 0;
        if (!srs_avc_startswith_annexb(stream, &pnb_start_code)) {
            return ERROR_H264_API_NO_PREFIXED;
        }
        int start = stream->pos() + pnb_start_code;
        
        // find the last frame prefixed by annexb format.
        stream->skip(pnb_start_code);
        while (!stream->empty()) {
            if (srs_avc_startswith_annexb(stream, NULL)) {
                break;
            }
            stream->skip(1);
        }
        
        // demux the frame.
        *pnb_frame = stream->pos() - start;
        *pframe = stream->data() + start;
        break;
    }

    return ret;
}

bool SrsRawH264Stream::is_sps(char* frame, int nb_frame)
{
    srs_assert(nb_frame > 0);
    
    // 5bits, 7.3.1 NAL unit syntax, 
    // H.264-AVC-ISO_IEC_14496-10.pdf, page 44.
    //  7: SPS, 8: PPS, 5: I Frame, 1: P Frame
    u_int8_t nal_unit_type = (char)frame[0] & 0x1f;

    return nal_unit_type == 7;
}

bool SrsRawH264Stream::is_pps(char* frame, int nb_frame)
{
    srs_assert(nb_frame > 0);
    
    // 5bits, 7.3.1 NAL unit syntax, 
    // H.264-AVC-ISO_IEC_14496-10.pdf, page 44.
    //  7: SPS, 8: PPS, 5: I Frame, 1: P Frame
    u_int8_t nal_unit_type = (char)frame[0] & 0x1f;

    return nal_unit_type == 8;
}

int SrsRawH264Stream::sps_demux(char* frame, int nb_frame, string& sps)
{
    int ret = ERROR_SUCCESS;

    // atleast 1bytes for SPS to decode the type, profile, constrain and level.
    if (nb_frame < 4) {
        return ret;
    }
        
    sps = "";
    if (nb_frame > 0) {
        sps.append(frame, nb_frame);
    }
    
    // should never be empty.
    if (sps.empty()) {
        return ERROR_STREAM_CASTER_AVC_SPS;
    }
        
    return ret;
}

int SrsRawH264Stream::pps_demux(char* frame, int nb_frame, string& pps)
{
    int ret = ERROR_SUCCESS;

    pps = "";
    if (nb_frame > 0) {
        pps.append(frame, nb_frame);
    }

    // should never be empty.
    if (pps.empty()) {
        return ERROR_STREAM_CASTER_AVC_PPS;
    }

    return ret;
}

int SrsRawH264Stream::mux_sequence_header(string sps, string pps, u_int32_t dts, u_int32_t pts, string& sh)
{
    int ret = ERROR_SUCCESS;

    // 5bytes sps/pps header:
    //      configurationVersion, AVCProfileIndication, profile_compatibility,
    //      AVCLevelIndication, lengthSizeMinusOne
    // 3bytes size of sps:
    //      numOfSequenceParameterSets, sequenceParameterSetLength(2B)
    // Nbytes of sps.
    //      sequenceParameterSetNALUnit
    // 3bytes size of pps:
    //      numOfPictureParameterSets, pictureParameterSetLength
    // Nbytes of pps:
    //      pictureParameterSetNALUnit
    int nb_packet = 5 
        + 3 + (int)sps.length() 
        + 3 + (int)pps.length();
    char* packet = new char[nb_packet];
    SrsAutoFree(char, packet);

    // use stream to generate the h264 packet.
    SrsStream stream;
    if ((ret = stream.initialize(packet, nb_packet)) != ERROR_SUCCESS) {
        return ret;
    }
    
    // decode the SPS: 
    // @see: 7.3.2.1.1, H.264-AVC-ISO_IEC_14496-10-2012.pdf, page 62
    if (true) {
        srs_assert((int)sps.length() >= 4);
        char* frame = (char*)sps.data();
    
        // @see: Annex A Profiles and levels, H.264-AVC-ISO_IEC_14496-10.pdf, page 205
        //      Baseline profile profile_idc is 66(0x42).
        //      Main profile profile_idc is 77(0x4d).
        //      Extended profile profile_idc is 88(0x58).
        u_int8_t profile_idc = frame[1];
        //u_int8_t constraint_set = frame[2];
        u_int8_t level_idc = frame[3];
        
        // generate the sps/pps header
        // 5.3.4.2.1 Syntax, H.264-AVC-ISO_IEC_14496-15.pdf, page 16
        // configurationVersion
        stream.write_1bytes(0x01);
        // AVCProfileIndication
        stream.write_1bytes(profile_idc);
        // profile_compatibility
        stream.write_1bytes(0x00);
        // AVCLevelIndication
        stream.write_1bytes(level_idc);
        // lengthSizeMinusOne, or NAL_unit_length, always use 4bytes size,
        // so we always set it to 0x03.
        stream.write_1bytes(0x03);
    }
    
    // sps
    if (true) {
        // 5.3.4.2.1 Syntax, H.264-AVC-ISO_IEC_14496-15.pdf, page 16
        // numOfSequenceParameterSets, always 1
        stream.write_1bytes(0x01);
        // sequenceParameterSetLength
        stream.write_2bytes(sps.length());
        // sequenceParameterSetNALUnit
        stream.write_string(sps);
    }
    
    // pps
    if (true) {
        // 5.3.4.2.1 Syntax, H.264-AVC-ISO_IEC_14496-15.pdf, page 16
        // numOfPictureParameterSets, always 1
        stream.write_1bytes(0x01);
        // pictureParameterSetLength
        stream.write_2bytes(pps.length());
        // pictureParameterSetNALUnit
        stream.write_string(pps);
    }

    // TODO: FIXME: for more profile.
    // 5.3.4.2.1 Syntax, H.264-AVC-ISO_IEC_14496-15.pdf, page 16
    // profile_idc == 100 || profile_idc == 110 || profile_idc == 122 || profile_idc == 144

    sh = "";
    sh.append(packet, nb_packet);

    return ret;
}

int SrsRawH264Stream::mux_ipb_frame(char* frame, int nb_frame, u_int32_t dts, u_int32_t pts, string& ibp, int8_t& frame_type)
{
    int ret = ERROR_SUCCESS;
    
    // 5bits, 7.3.1 NAL unit syntax, 
    // H.264-AVC-ISO_IEC_14496-10.pdf, page 44.
    //  7: SPS, 8: PPS, 5: I Frame, 1: P Frame
    u_int8_t nal_unit_type = (char)frame[0] & 0x1f;
    
    // 4bytes size of nalu:
    //      NALUnitLength
    // Nbytes of nalu.
    //      NALUnit
    int nb_packet = 4 + nb_frame;
    char* packet = new char[nb_packet];
    SrsAutoFree(char, packet);
    
    // use stream to generate the h264 packet.
    SrsStream stream;
    if ((ret = stream.initialize(packet, nb_packet)) != ERROR_SUCCESS) {
        return ret;
    }

    // 5.3.4.2.1 Syntax, H.264-AVC-ISO_IEC_14496-15.pdf, page 16
    // lengthSizeMinusOne, or NAL_unit_length, always use 4bytes size
    u_int32_t NAL_unit_length = nb_frame;
    
    // mux the avc NALU in "ISO Base Media File Format" 
    // from H.264-AVC-ISO_IEC_14496-15.pdf, page 20
    // NALUnitLength
    stream.write_4bytes(NAL_unit_length);
    // NALUnit
    stream.write_bytes(frame, nb_frame);

    // send out h264 packet.
    frame_type = SrsCodecVideoAVCFrameInterFrame;
    if (nal_unit_type != 1) {
        frame_type = SrsCodecVideoAVCFrameKeyFrame;
    }

    ibp = "";
    ibp.append(packet, nb_packet);

    return ret;
}

int SrsRawH264Stream::mux_avc2flv(string video, int8_t frame_type, int8_t avc_packet_type, u_int32_t dts, u_int32_t pts, char** flv, int* nb_flv)
{
    int ret = ERROR_SUCCESS;
    
    // for h264 in RTMP video payload, there is 5bytes header:
    //      1bytes, FrameType | CodecID
    //      1bytes, AVCPacketType
    //      3bytes, CompositionTime, the cts.
    // @see: E.4.3 Video Tags, video_file_format_spec_v10_1.pdf, page 78
    int size = video.length() + 5;
    char* data = new char[size];
    char* p = data;
    
    // @see: E.4.3 Video Tags, video_file_format_spec_v10_1.pdf, page 78
    // Frame Type, Type of video frame.
    // CodecID, Codec Identifier.
    // set the rtmp header
    *p++ = (frame_type << 4) | SrsCodecVideoAVC;
    
    // AVCPacketType
    *p++ = avc_packet_type;

    // CompositionTime
    // pts = dts + cts, or 
    // cts = pts - dts.
    // where cts is the header in rtmp video packet payload header.
    u_int32_t cts = pts - dts;
    char* pp = (char*)&cts;
    *p++ = pp[2];
    *p++ = pp[1];
    *p++ = pp[0];
    
    // h.264 raw data.
    memcpy(p, video.data(), video.length());

    *flv = data;
    *nb_flv = size;

    return ret;
}

SrsRawAacStream::SrsRawAacStream()
{
}

SrsRawAacStream::~SrsRawAacStream()
{
}

int SrsRawAacStream::adts_demux(SrsStream* stream, char** pframe, int* pnb_frame, SrsRawAacStreamCodec& codec)
{
    int ret = ERROR_SUCCESS;
    
    while (!stream->empty()) {
        int adts_header_start = stream->pos();
        
        // decode the ADTS.
        // @see aac-mp4a-format-ISO_IEC_14496-3+2001.pdf, page 75,
        //      1.A.2.2 Audio_Data_Transport_Stream frame, ADTS
        // @see https://github.com/winlinvip/simple-rtmp-server/issues/212#issuecomment-64145885
        // byte_alignment()
        
        // adts_fixed_header:
        //      12bits syncword,
        //      16bits left.
        // adts_variable_header:
        //      28bits
        //      12+16+28=56bits
        // adts_error_check:
        //      16bits if protection_absent
        //      56+16=72bits
        // if protection_absent:
        //      require(7bytes)=56bits
        // else
        //      require(9bytes)=72bits
        if (!stream->require(7)) {
            return ERROR_AAC_ADTS_HEADER;
        }
        
        // for aac, the frame must be ADTS format.
        if (!srs_aac_startswith_adts(stream)) {
            return ERROR_AAC_REQUIRED_ADTS;
        }
        
        // Syncword 12 bslbf
        stream->read_1bytes();
        // 4bits left.
        // adts_fixed_header(), 1.A.2.2.1 Fixed Header of ADTS
        // ID 1 bslbf
        // Layer 2 uimsbf
        // protection_absent 1 bslbf
        int8_t fh0 = (stream->read_1bytes() & 0x0f);
        /*int8_t fh_id = (fh0 >> 3) & 0x01;*/
        /*int8_t fh_layer = (fh0 >> 1) & 0x03;*/
        int8_t fh_protection_absent = fh0 & 0x01;
        
        int16_t fh1 = stream->read_2bytes();
        // Profile_ObjectType 2 uimsbf
        // sampling_frequency_index 4 uimsbf
        // private_bit 1 bslbf
        // channel_configuration 3 uimsbf
        // original/copy 1 bslbf
        // home 1 bslbf
        int8_t fh_Profile_ObjectType = (fh1 >> 14) & 0x03;
        int8_t fh_sampling_frequency_index = (fh1 >> 10) & 0x0f;
        /*int8_t fh_private_bit = (fh1 >> 9) & 0x01;*/
        int8_t fh_channel_configuration = (fh1 >> 6) & 0x07;
        /*int8_t fh_original = (fh1 >> 5) & 0x01;*/
        /*int8_t fh_home = (fh1 >> 4) & 0x01;*/
        // @remark, Emphasis is removed, 
        //      @see https://github.com/winlinvip/simple-rtmp-server/issues/212#issuecomment-64154736
        //int8_t fh_Emphasis = (fh1 >> 2) & 0x03;
        // 4bits left.
        // adts_variable_header(), 1.A.2.2.2 Variable Header of ADTS
        // copyright_identification_bit 1 bslbf
        // copyright_identification_start 1 bslbf
        /*int8_t fh_copyright_identification_bit = (fh1 >> 3) & 0x01;*/
        /*int8_t fh_copyright_identification_start = (fh1 >> 2) & 0x01;*/
        // aac_frame_length 13 bslbf: Length of the frame including headers and error_check in bytes.
        // use the left 2bits as the 13 and 12 bit,
        // the aac_frame_length is 13bits, so we move 13-2=11.
        int16_t fh_aac_frame_length = (fh1 << 11) & 0x0800;
        
        int32_t fh2 = stream->read_3bytes();
        // aac_frame_length 13 bslbf: consume the first 13-2=11bits
        // the fh2 is 24bits, so we move right 24-11=13.
        fh_aac_frame_length |= (fh2 >> 13) & 0x07ff;
        // adts_buffer_fullness 11 bslbf
        /*int16_t fh_adts_buffer_fullness = (fh2 >> 2) & 0x7ff;*/
        // no_raw_data_blocks_in_frame 2 uimsbf
        /*int16_t fh_no_raw_data_blocks_in_frame = fh2 & 0x03;*/
        // adts_error_check(), 1.A.2.2.3 Error detection
        if (!fh_protection_absent) {
            if (!stream->require(2)) {
                return ERROR_AAC_ADTS_HEADER;
            }
            // crc_check 16 Rpchof
            /*int16_t crc_check = */stream->read_2bytes();
        }
        
        // TODO: check the fh_sampling_frequency_index
        // TODO: check the fh_channel_configuration
        
        // raw_data_blocks
        int adts_header_size = stream->pos() - adts_header_start;
        int raw_data_size = fh_aac_frame_length - adts_header_size;
        if (!stream->require(raw_data_size)) {
            return ERROR_AAC_ADTS_HEADER;
        }
        
        // the profile = object_id + 1
        // @see aac-mp4a-format-ISO_IEC_14496-3+2001.pdf, page 78,
        //      Table 1. A.9 ¨C MPEG-2 Audio profiles and MPEG-4 Audio object types
        char aac_profile = fh_Profile_ObjectType + 1;
        
        // the codec info.
        codec.protection_absent = fh_protection_absent;
        codec.Profile_ObjectType = fh_Profile_ObjectType;
        codec.sampling_frequency_index = fh_sampling_frequency_index;
        codec.channel_configuration = fh_channel_configuration;
        codec.aac_frame_length = fh_aac_frame_length;

        codec.aac_profile = aac_profile;
        codec.aac_samplerate = fh_sampling_frequency_index;
        codec.aac_channel = fh_channel_configuration;
        
        // @see srs_audio_write_raw_frame().
        codec.sound_format = 10; // AAC
        if (fh_sampling_frequency_index <= 0x0c && fh_sampling_frequency_index > 0x0a) {
            codec.sound_rate = SrsCodecAudioSampleRate5512;
        } else if (fh_sampling_frequency_index <= 0x0a && fh_sampling_frequency_index > 0x07) {
            codec.sound_rate = SrsCodecAudioSampleRate11025;
        } else if (fh_sampling_frequency_index <= 0x07 && fh_sampling_frequency_index > 0x04) {
            codec.sound_rate = SrsCodecAudioSampleRate22050;
        } else if (fh_sampling_frequency_index <= 0x04) {
            codec.sound_rate = SrsCodecAudioSampleRate44100;
        } else {
            codec.sound_rate = SrsCodecAudioSampleRate44100;
            srs_warn("adts invalid sample rate for flv, rate=%#x", fh_sampling_frequency_index);
        }
        codec.sound_size = srs_max(0, srs_min(1, fh_channel_configuration - 1));
        // TODO: FIXME: finger it out the sound size by adts.
        codec.sound_size = 1; // 0(8bits) or 1(16bits).

        // frame data.
        *pframe = stream->data() + stream->pos();
        *pnb_frame = raw_data_size;
        stream->skip(raw_data_size);

        break;
    }

    return ret;
}

int SrsRawAacStream::mux_sequence_header(SrsRawAacStreamCodec* codec, string& sh)
{
    int ret = ERROR_SUCCESS;
    
    char aac_channel = codec->aac_channel;
    char aac_profile = codec->aac_profile;
    char aac_samplerate = codec->aac_samplerate;

    // override the aac samplerate by user specified.
    // @see https://github.com/winlinvip/simple-rtmp-server/issues/212#issuecomment-64146899
    switch (codec->sound_rate) {
        case SrsCodecAudioSampleRate11025: 
            aac_samplerate = 0x0a; break;
        case SrsCodecAudioSampleRate22050: 
            aac_samplerate = 0x07; break;
        case SrsCodecAudioSampleRate44100: 
            aac_samplerate = 0x04; break;
        default:
            break;
    }

    sh = "";

    char ch = 0;
    // @see aac-mp4a-format-ISO_IEC_14496-3+2001.pdf
    // AudioSpecificConfig (), page 33
    // 1.6.2.1 AudioSpecificConfig
    // audioObjectType; 5 bslbf
    ch = (aac_profile << 3) & 0xf8;
    // 3bits left.
        
    // samplingFrequencyIndex; 4 bslbf
    ch |= (aac_samplerate >> 1) & 0x07;
    sh += ch;
    ch = (aac_samplerate << 7) & 0x80;
    if (aac_samplerate == 0x0f) {
        return ERROR_AAC_DATA_INVALID;
    }
    // 7bits left.
        
    // channelConfiguration; 4 bslbf
    ch |= (aac_channel << 3) & 0x78;
    // 3bits left.
        
    // only support aac profile 1-4.
    if (aac_profile < 1 || aac_profile > 4) {
        return ERROR_AAC_DATA_INVALID;
    }
    // GASpecificConfig(), page 451
    // 4.4.1 Decoder configuration (GASpecificConfig)
    // frameLengthFlag; 1 bslbf
    // dependsOnCoreCoder; 1 bslbf
    // extensionFlag; 1 bslbf
    sh += ch;

    return ret;
}

int SrsRawAacStream::mux_aac2flv(char* frame, int nb_frame, SrsRawAacStreamCodec* codec, u_int32_t dts, char** flv, int* nb_flv)
{
    int ret = ERROR_SUCCESS;

    char sound_format = codec->sound_format;
    char sound_type = codec->sound_type;
    char sound_size = codec->sound_size;
    char sound_rate = codec->sound_rate;
    char aac_packet_type = codec->aac_packet_type;

    // for audio frame, there is 1 or 2 bytes header:
    //      1bytes, SoundFormat|SoundRate|SoundSize|SoundType
    //      1bytes, AACPacketType for SoundFormat == 10, 0 is sequence header.
    int size = nb_frame + 1;
    if (sound_format == SrsCodecAudioAAC) {
        size += 1;
    }
    char* data = new char[size];
    char* p = data;
    
    u_int8_t audio_header = sound_type & 0x01;
    audio_header |= (sound_size << 1) & 0x02;
    audio_header |= (sound_rate << 2) & 0x0c;
    audio_header |= (sound_format << 4) & 0xf0;
    
    *p++ = audio_header;
    
    if (sound_format == SrsCodecAudioAAC) {
        *p++ = aac_packet_type;
    }
    
    memcpy(p, frame, nb_frame);

    *flv = data;
    *nb_flv = size;

    return ret;
}

