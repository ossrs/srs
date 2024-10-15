//
// Copyright (c) 2013-2024 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_protocol_raw_avc.hpp>

#include <string.h>
using namespace std;

#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_kernel_buffer.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_codec.hpp>

SrsRawH264Stream::SrsRawH264Stream()
{
}

SrsRawH264Stream::~SrsRawH264Stream()
{
}

srs_error_t SrsRawH264Stream::annexb_demux(SrsBuffer* stream, char** pframe, int* pnb_frame)
{
    srs_error_t err = srs_success;
    
    *pframe = NULL;
    *pnb_frame = 0;
    
    while (!stream->empty()) {
        // each frame must prefixed by annexb format.
        // about annexb, @see ISO_IEC_14496-10-AVC-2003.pdf, page 211.
        int pnb_start_code = 0;
        if (!srs_avc_startswith_annexb(stream, &pnb_start_code)) {
            return srs_error_new(ERROR_H264_API_NO_PREFIXED, "annexb start code");
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
    
    return err;
}

bool SrsRawH264Stream::is_sps(char* frame, int nb_frame)
{
    srs_assert(nb_frame > 0);
    
    // 5bits, 7.3.1 NAL unit syntax,
    // ISO_IEC_14496-10-AVC-2003.pdf, page 44.
    //  7: SPS, 8: PPS, 5: I Frame, 1: P Frame
    uint8_t nal_unit_type = (char)frame[0] & 0x1f;
    
    return nal_unit_type == 7;
}

bool SrsRawH264Stream::is_pps(char* frame, int nb_frame)
{
    srs_assert(nb_frame > 0);
    
    // 5bits, 7.3.1 NAL unit syntax,
    // ISO_IEC_14496-10-AVC-2003.pdf, page 44.
    //  7: SPS, 8: PPS, 5: I Frame, 1: P Frame
    uint8_t nal_unit_type = (char)frame[0] & 0x1f;
    
    return nal_unit_type == 8;
}

srs_error_t SrsRawH264Stream::sps_demux(char* frame, int nb_frame, string& sps)
{
    srs_error_t err = srs_success;
    
    // atleast 1bytes for SPS to decode the type, profile, constrain and level.
    if (nb_frame < 4) {
        return err;
    }

    sps = string(frame, nb_frame);

    return err;
}

srs_error_t SrsRawH264Stream::pps_demux(char* frame, int nb_frame, string& pps)
{
    srs_error_t err = srs_success;

    if (nb_frame <= 0) {
        return srs_error_new(ERROR_STREAM_CASTER_AVC_PPS, "no pps");
    }

    pps = string(frame, nb_frame);

    return err;
}

srs_error_t SrsRawH264Stream::mux_sequence_header(string sps, string pps, string& sh)
{
    srs_error_t err = srs_success;
    
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
    int nb_packet = 5 + (3 + (int)sps.length()) + (3 + (int)pps.length());
    SrsUniquePtr<char[]> packet(new char[nb_packet]);

    // use stream to generate the h264 packet.
    SrsBuffer stream(packet.get(), nb_packet);
    
    // decode the SPS:
    // @see: 7.3.2.1.1, ISO_IEC_14496-10-AVC-2012.pdf, page 62
    if (true) {
        srs_assert((int)sps.length() >= 4);
        char* frame = (char*)sps.data();
        
        // @see: Annex A Profiles and levels, ISO_IEC_14496-10-AVC-2003.pdf, page 205
        //      Baseline profile profile_idc is 66(0x42).
        //      Main profile profile_idc is 77(0x4d).
        //      Extended profile profile_idc is 88(0x58).
        uint8_t profile_idc = frame[1];
        //uint8_t constraint_set = frame[2];
        uint8_t level_idc = frame[3];
        
        // generate the sps/pps header
        // 5.3.4.2.1 Syntax, ISO_IEC_14496-15-AVC-format-2012.pdf, page 16
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
        stream.write_1bytes(uint8_t(0xfc | 0x03));
    }
    
    // sps
    if (true) {
        // 5.3.4.2.1 Syntax, ISO_IEC_14496-15-AVC-format-2012.pdf, page 16
        // numOfSequenceParameterSets, always 1
        stream.write_1bytes(uint8_t(0xe0 | 0x01));
        // sequenceParameterSetLength
        stream.write_2bytes((int16_t)sps.length());
        // sequenceParameterSetNALUnit
        stream.write_string(sps);
    }
    
    // pps
    if (true) {
        // 5.3.4.2.1 Syntax, ISO_IEC_14496-15-AVC-format-2012.pdf, page 16
        // numOfPictureParameterSets, always 1
        stream.write_1bytes(0x01);
        // pictureParameterSetLength
        stream.write_2bytes((int16_t)pps.length());
        // pictureParameterSetNALUnit
        stream.write_string(pps);
    }
    
    // TODO: FIXME: for more profile.
    // 5.3.4.2.1 Syntax, ISO_IEC_14496-15-AVC-format-2012.pdf, page 16
    // profile_idc == 100 || profile_idc == 110 || profile_idc == 122 || profile_idc == 144

    sh = string(packet.get(), nb_packet);
    
    return err;
}

srs_error_t SrsRawH264Stream::mux_ipb_frame(char* frame, int nb_frame, string& ibp)
{
    srs_error_t err = srs_success;
    
    // 4bytes size of nalu:
    //      NALUnitLength
    // Nbytes of nalu.
    //      NALUnit
    int nb_packet = 4 + nb_frame;
    SrsUniquePtr<char[]> packet(new char[nb_packet]);

    // use stream to generate the h264 packet.
    SrsBuffer stream(packet.get(), nb_packet);
    
    // 5.3.4.2.1 Syntax, ISO_IEC_14496-15-AVC-format-2012.pdf, page 16
    // lengthSizeMinusOne, or NAL_unit_length, always use 4bytes size
    uint32_t NAL_unit_length = nb_frame;
    
    // mux the avc NALU in "ISO Base Media File Format"
    // from ISO_IEC_14496-15-AVC-format-2012.pdf, page 20
    // NALUnitLength
    stream.write_4bytes(NAL_unit_length);
    // NALUnit
    stream.write_bytes(frame, nb_frame);

    ibp = string(packet.get(), nb_packet);
    
    return err;
}

srs_error_t SrsRawH264Stream::mux_avc2flv(string video, int8_t frame_type, int8_t avc_packet_type, uint32_t dts, uint32_t pts, char** flv, int* nb_flv)
{
    srs_error_t err = srs_success;
    
    // for h264 in RTMP video payload, there is 5bytes header:
    //      1bytes, FrameType | CodecID
    //      1bytes, AVCPacketType
    //      3bytes, CompositionTime, the cts.
    // @see: E.4.3 Video Tags, video_file_format_spec_v10_1.pdf, page 78
    int size = (int)video.length() + 5;
    char* data = new char[size];
    char* p = data;
    
    // @see: E.4.3 Video Tags, video_file_format_spec_v10_1.pdf, page 78
    // Frame Type, Type of video frame.
    // CodecID, Codec Identifier.
    // set the rtmp header
    *p++ = (frame_type << 4) | SrsVideoCodecIdAVC;
    
    // AVCPacketType
    *p++ = avc_packet_type;
    
    // CompositionTime
    // pts = dts + cts, or
    // cts = pts - dts.
    // where cts is the header in rtmp video packet payload header.
    uint32_t cts = pts - dts;
    char* pp = (char*)&cts;
    *p++ = pp[2];
    *p++ = pp[1];
    *p++ = pp[0];
    
    // h.264 raw data.
    memcpy(p, video.data(), video.length());
    
    *flv = data;
    *nb_flv = size;
    
    return err;
}

#ifdef SRS_H265

SrsRawHEVCStream::SrsRawHEVCStream()
{
}

SrsRawHEVCStream::~SrsRawHEVCStream()
{
}

srs_error_t SrsRawHEVCStream::annexb_demux(SrsBuffer *stream, char **pframe, int *pnb_frame)
{
    srs_error_t err = srs_success;

    *pframe = NULL;
    *pnb_frame = 0;

    while (!stream->empty()) {
        // each frame must prefixed by annexb format.
        // @see B.2 Byte stream NAL unit syntax and semantics
        // @doc ITU-T-H.265-2021.pdf, page 292.
        int pnb_start_code = 0;
        if (!srs_avc_startswith_annexb(stream, &pnb_start_code)) {
            return srs_error_new(ERROR_HEVC_API_NO_PREFIXED, "hevc annexb start code");
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

    return err;
}

// whether the frame is vps or sps or pps.
bool SrsRawHEVCStream::is_vps(char *frame, int nb_frame)
{
    srs_assert(nb_frame > 0);

    // 7bits, 7.4.2.2 NAL unit header semantics,
    // @see Table 7-1 – NAL unit type codes and NAL unit type classes
    // @doc ITU-T-H.265-2021.pdf, page 86.

    return SrsHevcNaluTypeParse(frame[0]) == SrsHevcNaluType_VPS;
}

bool SrsRawHEVCStream::is_sps(char *frame, int nb_frame)
{
    srs_assert(nb_frame > 0);

    // 7bits, 7.4.2.2 NAL unit header semantics,
    // @see Table 7-1 – NAL unit type codes and NAL unit type classes
    // @doc ITU-T-H.265-2021.pdf, page 86.

    return SrsHevcNaluTypeParse(frame[0]) == SrsHevcNaluType_SPS;
}

bool SrsRawHEVCStream::is_pps(char *frame, int nb_frame)
{
    srs_assert(nb_frame > 0);

    // 7bits, 7.4.2.2 NAL unit header semantics,
    // @see Table 7-1 – NAL unit type codes and NAL unit type classes
    // @doc ITU-T-H.265-2021.pdf, page 86.

    return SrsHevcNaluTypeParse(frame[0]) == SrsHevcNaluType_PPS;
}

srs_error_t SrsRawHEVCStream::vps_demux(char *frame, int nb_frame, std::string &vps)
{
    srs_error_t err = srs_success;

    if (nb_frame <= 0) {
        return srs_error_new(ERROR_STREAM_CASTER_HEVC_VPS, "no hevc vps");
    }

    vps = string(frame, nb_frame);

    return err;
}

srs_error_t SrsRawHEVCStream::sps_demux(char *frame, int nb_frame, std::string &sps)
{
    srs_error_t err = srs_success;

    // atleast 1bytes for SPS to decode the type, profile, constrain and level.
    if (nb_frame < 4) {
        return err;
    }

    sps = string(frame, nb_frame);

    return err;
}

srs_error_t SrsRawHEVCStream::pps_demux(char *frame, int nb_frame, std::string &pps)
{
    srs_error_t err = srs_success;

    if (nb_frame <= 0) {
        return srs_error_new(ERROR_STREAM_CASTER_HEVC_PPS, "no hevc pps");
    }

    pps = string(frame, nb_frame);

    return err;
}

srs_error_t SrsRawHEVCStream::mux_sequence_header(std::string vps, std::string sps, std::vector<std::string>& pps, std::string &hvcC)
{
    srs_error_t err = srs_success;

    // hevc header information:
    // 23bytes header:
    //      configurationVersion, general_profile_space, general_tier_flag, general_profile_idc
    //      general_profile_compatibility_flags, general_constraint_indicator_flags,
    //      general_level_idc, min_spatial_segmentation_idc, parallelismType,
    //      chromaFormat, bitDepthLumaMinus8, bitDepthChromaMinus8,
    //      avgFrameRate, constantFrameRate, numTemporalLayers, temporalIdNested,
    //      lengthSizeMinusOne, numOfArrays
    // 5bytes size of vps/sps/pps:
    //      array_completeness, nal_unit_type, numNalus, nalUnitLength,
    // Nbytes of vps/sps/pps.
    //      sequenceParameterSetNALUnit

    // use simple mode: nalu size + nalu data
    int pps_size = 0;
    for (std::vector<std::string>::iterator it = pps.begin(); it != pps.end(); it++) {
        pps_size += 2 + it->length();
    }

    int nb_packet = 23 + 5 + (int)vps.length() + 5 + (int)sps.length() + 5 + pps_size - 2;
    SrsUniquePtr<char[]> packet(new char[nb_packet]);

    // use stream to generate the hevc packet.
    SrsBuffer stream(packet.get(), nb_packet);

    SrsFormat format;
    if ((err = format.initialize()) != srs_success) {
        return srs_error_wrap(err, "format failed");
    }
    // hevc_dec_conf_record
    SrsHevcDecoderConfigurationRecord *hevc_info = &format.vcodec->hevc_dec_conf_record_;

    if (true) {
        // H265 VPS (video_parameter_set_rbsp()) NAL Unit.
        // @see Section 7.3.2.1 ("Video parameter set RBSP syntax") of the H.265
        // @doc ITU-T-H.265-2021.pdf, page 54.
        SrsBuffer vps_stream((char*)vps.data(), vps.length());
        if ((err = format.hevc_demux_vps(&vps_stream)) != srs_success) {
            return srs_error_wrap(err, "vps demux failed, len=%d", vps.length());
        }

        // H265 SPS Nal Unit (seq_parameter_set_rbsp()) parser.
        // @see Section 7.3.2.2 ("Sequence parameter set RBSP syntax") of the H.265
        // @doc ITU-T-H.265-2021.pdf, page 55.
        SrsBuffer sps_stream((char*)sps.data(), sps.length());
        if ((err = format.hevc_demux_sps(&sps_stream)) != srs_success) {
            return srs_error_wrap(err, "sps demux failed, len=%d",sps.length());
        }
    }

    // configurationVersion
    stream.write_1bytes(0x01);

    uint8_t temp8bits = 0;
    // general_profile_space(2bits), general_tier_flag(1bit), general_profile_idc(5bits)
    temp8bits |= ((hevc_info->general_profile_space & 0x03) << 6);
    temp8bits |= ((hevc_info->general_tier_flag & 0x01) << 5);
    temp8bits |= (hevc_info->general_profile_idc & 0x1f);
    stream.write_1bytes(temp8bits);

    stream.write_4bytes(hevc_info->general_profile_compatibility_flags);
    stream.write_2bytes((hevc_info->general_constraint_indicator_flags >> 32) & 0xffff);
    stream.write_4bytes(hevc_info->general_constraint_indicator_flags & 0xffffffff);
    stream.write_1bytes(hevc_info->general_level_idc);
    stream.write_2bytes(0xf000 | (hevc_info->min_spatial_segmentation_idc & 0x0fff));
    stream.write_1bytes(0xfc | (hevc_info->parallelism_type & 0x03));
    stream.write_1bytes(0xfc | (hevc_info->chroma_format & 0x03));
    stream.write_1bytes(0xf8 | (hevc_info->bit_depth_luma_minus8 & 0x07));
    stream.write_1bytes(0xf8 | (hevc_info->bit_depth_chroma_minus8 & 0x07));
    stream.write_2bytes(hevc_info->avg_frame_rate);

    hevc_info->length_size_minus_one = 3;
    temp8bits = 0;

    //8bits: constant_frame_rate(2bits), num_temporal_layers(3bits),
    //       temporal_id_nested(1bit), length_size_minus_one(2bits)
    temp8bits |= (hevc_info->constant_frame_rate << 6) | 0xc0;
    temp8bits |= (hevc_info->num_temporal_layers << 3) | 0x38;
    temp8bits |= (hevc_info->temporal_id_nested << 2) | 0x04;
    temp8bits |= (hevc_info->length_size_minus_one & 0x03);
    stream.write_1bytes(temp8bits);

    // numOfArrays, default 3
    stream.write_1bytes(0x03);

    // vps
    if (true) {
        // nal_type
        stream.write_1bytes(SrsHevcNaluType_VPS & 0x3f);
        // numOfVideoParameterSets, always 1
        stream.write_2bytes(0x01);
        // videoParameterSetLength
        stream.write_2bytes((int16_t)vps.length());
        // videoParameterSetNALUnit
        stream.write_string(vps);
    }

    // sps
    if (true) {
        // nal_type
        stream.write_1bytes(SrsHevcNaluType_SPS & 0x3f);
        // numOfSequenceParameterSets, always 1
        stream.write_2bytes(0x01);
        // sequenceParameterSetLength
        stream.write_2bytes((int16_t)sps.length());
        // sequenceParameterSetNALUnit
        stream.write_string(sps);
    }

    // pps
    if (true) {
        // nal_type
        stream.write_1bytes(SrsHevcNaluType_PPS & 0x3f);
        // numOfPictureParameterSets
        stream.write_2bytes(pps.size());

        for (std::vector<std::string>::iterator it = pps.begin(); it != pps.end(); it++) {
            //pictureParameterSetLength
            stream.write_2bytes((int16_t)it->length());
            //pictureParameterSetNALUnit
            stream.write_string(*it);
        }
    }

    hvcC = string(packet.get(), nb_packet);

    return err;
}

srs_error_t SrsRawHEVCStream::mux_ipb_frame(char *frame, int nb_frame, std::string &ibp)
{
    srs_error_t err = srs_success;

    // 4bytes size of nalu:
    //      NALUnitLength
    // Nbytes of nalu.
    //      NALUnit
    int nb_packet = 4 + nb_frame;
    SrsUniquePtr<char[]> packet(new char[nb_packet]);

    // use stream to generate the h265 packet.
    SrsBuffer stream(packet.get(), nb_packet);

    // 5.3.4.2.1 Syntax, ISO_IEC_14496-15-AVC-format-2012.pdf, page 16
    // lengthSizeMinusOne, or NAL_unit_length, always use 4bytes size
    uint32_t NAL_unit_length = nb_frame;

    // mux the avc NALU in "ISO Base Media File Format"
    // from ISO_IEC_14496-15-AVC-format-2012.pdf, page 20
    // NALUnitLength
    stream.write_4bytes(NAL_unit_length);
    // NALUnit
    stream.write_bytes(frame, nb_frame);

    ibp = string(packet.get(), nb_packet);

    return err;
}

srs_error_t SrsRawHEVCStream::mux_avc2flv(std::string video, int8_t frame_type, int8_t avc_packet_type, uint32_t dts, uint32_t pts, char **flv, int *nb_flv)
{
    srs_error_t err = srs_success;

    // for h265 in RTMP video payload, there is 5bytes header:
    //      1bytes, FrameType | CodecID
    //      1bytes, AVCPacketType
    //      3bytes, CompositionTime, the cts.
    // @see: E.4.3 Video Tags, video_file_format_spec_v10_1.pdf, page 78
    int size = (int)video.length() + 5;
    char *data = new char[size];
    char *p = data;

    // @see: E.4.3 Video Tags, video_file_format_spec_v10_1.pdf, page 78
    // Frame Type, Type of video frame.
    // CodecID, Codec Identifier.
    // set the rtmp header
    *p++ = (frame_type << 4) | SrsVideoCodecIdHEVC;

    // AVCPacketType
    *p++ = avc_packet_type;

    // CompositionTime
    // pts = dts + cts, or
    // cts = pts - dts.
    // where cts is the header in rtmp video packet payload header.
    uint32_t cts = pts - dts;
    char *pp = (char *)&cts;
    *p++ = pp[2];
    *p++ = pp[1];
    *p++ = pp[0];

    // hevc raw data.
    memcpy(p, video.data(), video.length());

    *flv = data;
    *nb_flv = size;

    return err;
}
#endif

SrsRawAacStream::SrsRawAacStream()
{
}

SrsRawAacStream::~SrsRawAacStream()
{
}

srs_error_t SrsRawAacStream::adts_demux(SrsBuffer* stream, char** pframe, int* pnb_frame, SrsRawAacStreamCodec& codec)
{
    srs_error_t err = srs_success;
    
    while (!stream->empty()) {
        int adts_header_start = stream->pos();
        
        // decode the ADTS.
        // @see ISO_IEC_13818-7-AAC-2004.pdf, page 26
        //      6.2 Audio Data Transport Stream, ADTS
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
            return srs_error_new(ERROR_AAC_ADTS_HEADER, "requires 7 only %d bytes", stream->left());
        }
        
        // for aac, the frame must be ADTS format.
        if (!srs_aac_startswith_adts(stream)) {
            return srs_error_new(ERROR_AAC_REQUIRED_ADTS, "not adts");
        }
        
        // syncword 12 bslbf
        stream->read_1bytes();
        // 4bits left.
        // adts_fixed_header(), 1.A.2.2.1 Fixed Header of ADTS
        // ID 1 bslbf
        // layer 2 uimsbf
        // protection_absent 1 bslbf
        int8_t pav = (stream->read_1bytes() & 0x0f);
        int8_t id = (pav >> 3) & 0x01;
        /*int8_t layer = (pav >> 1) & 0x03;*/
        int8_t protection_absent = pav & 0x01;
        
        /**
         * ID: MPEG identifier, set to '1' if the audio data in the ADTS stream are MPEG-2 AAC (See ISO/IEC 13818-7)
         * and set to '0' if the audio data are MPEG-4. See also ISO/IEC 11172-3, subclause 2.4.2.3.
         */
        if (id != 0x01) {
            // well, some system always use 0, but actually is aac format.
            // for example, houjian vod ts always set the aac id to 0, actually 1.
            // we just ignore it, and alwyas use 1(aac) to demux.
            id = 0x01;
        }
        
        int16_t sfiv = stream->read_2bytes();
        // profile 2 uimsbf
        // sampling_frequency_index 4 uimsbf
        // private_bit 1 bslbf
        // channel_configuration 3 uimsbf
        // original/copy 1 bslbf
        // home 1 bslbf
        int8_t profile = (sfiv >> 14) & 0x03;
        int8_t sampling_frequency_index = (sfiv >> 10) & 0x0f;
        /*int8_t private_bit = (sfiv >> 9) & 0x01;*/
        int8_t channel_configuration = (sfiv >> 6) & 0x07;
        /*int8_t original = (sfiv >> 5) & 0x01;*/
        /*int8_t home = (sfiv >> 4) & 0x01;*/
        //int8_t Emphasis; @remark, Emphasis is removed
        // 4bits left.
        // adts_variable_header(), 1.A.2.2.2 Variable Header of ADTS
        // copyright_identification_bit 1 bslbf
        // copyright_identification_start 1 bslbf
        /*int8_t fh_copyright_identification_bit = (fh1 >> 3) & 0x01;*/
        /*int8_t fh_copyright_identification_start = (fh1 >> 2) & 0x01;*/
        // frame_length 13 bslbf: Length of the frame including headers and error_check in bytes.
        // use the left 2bits as the 13 and 12 bit,
        // the frame_length is 13bits, so we move 13-2=11.
        int16_t frame_length = (sfiv << 11) & 0x1800;
        
        int32_t abfv = stream->read_3bytes();
        // frame_length 13 bslbf: consume the first 13-2=11bits
        // the fh2 is 24bits, so we move right 24-11=13.
        frame_length |= (abfv >> 13) & 0x07ff;
        // adts_buffer_fullness 11 bslbf
        /*int16_t fh_adts_buffer_fullness = (abfv >> 2) & 0x7ff;*/
        // number_of_raw_data_blocks_in_frame 2 uimsbf
        /*int16_t number_of_raw_data_blocks_in_frame = abfv & 0x03;*/
        // adts_error_check(), 1.A.2.2.3 Error detection
        if (!protection_absent) {
            if (!stream->require(2)) {
                return srs_error_new(ERROR_AAC_ADTS_HEADER, "requires 2 only %d bytes", stream->left());
            }
            // crc_check 16 Rpchof
            /*int16_t crc_check = */stream->read_2bytes();
        }
        
        // TODO: check the sampling_frequency_index
        // TODO: check the channel_configuration
        
        // raw_data_blocks
        int adts_header_size = stream->pos() - adts_header_start;
        int raw_data_size = frame_length - adts_header_size;
        if (!stream->require(raw_data_size)) {
            return srs_error_new(ERROR_AAC_ADTS_HEADER, "requires %d only %d bytes", raw_data_size, stream->left());
        }
        
        // the codec info.
        codec.protection_absent = protection_absent;
        codec.aac_object = srs_aac_ts2rtmp((SrsAacProfile)profile);
        codec.sampling_frequency_index = sampling_frequency_index;
        codec.channel_configuration = channel_configuration;
        codec.frame_length = frame_length;
        
        // The aac sampleing rate defined in srs_aac_srates.
        // TODO: FIXME: maybe need to resample audio.
        codec.sound_format = 10; // AAC
        if (sampling_frequency_index <= 0x0c && sampling_frequency_index > 0x0a) {
            codec.sound_rate = SrsAudioSampleRate5512;
        } else if (sampling_frequency_index <= 0x0a && sampling_frequency_index > 0x07) {
            codec.sound_rate = SrsAudioSampleRate11025;
        } else if (sampling_frequency_index <= 0x07 && sampling_frequency_index > 0x04) {
            codec.sound_rate = SrsAudioSampleRate22050;
        } else if (sampling_frequency_index <= 0x04) {
            codec.sound_rate = SrsAudioSampleRate44100;
        } else {
            codec.sound_rate = SrsAudioSampleRate44100;
            srs_warn("adts invalid sample rate for flv, rate=%#x", sampling_frequency_index);
        }
        codec.sound_type = srs_max(0, srs_min(1, channel_configuration - 1));
        // TODO: FIXME: finger it out the sound size by adts.
        codec.sound_size = 1; // 0(8bits) or 1(16bits).
        
        // frame data.
        *pframe = stream->data() + stream->pos();
        *pnb_frame = raw_data_size;
        stream->skip(raw_data_size);
        
        break;
    }
    
    return err;
}

srs_error_t SrsRawAacStream::mux_sequence_header(SrsRawAacStreamCodec* codec, string& sh)
{
    srs_error_t err = srs_success;

    // only support aac profile 1-4.
    if (codec->aac_object == SrsAacObjectTypeReserved) {
        return srs_error_new(ERROR_AAC_DATA_INVALID, "invalid aac object");
    }
    
    SrsAacObjectType audioObjectType = codec->aac_object;
    char channelConfiguration = codec->channel_configuration;

    // Here we are generating AAC sequence header, the ASC structure,
    // because we have already parsed the sampling rate from AAC codec,
    // which is more precise than the sound_rate defined by RTMP.
    //
    // For example, AAC sampling_frequency_index is 3(48000HZ) or 4(44100HZ),
    // the sound_rate is always 3(44100HZ), if we covert sound_rate to
    // sampling_frequency_index, we may make mistake.
    uint8_t samplingFrequencyIndex = (uint8_t)codec->sampling_frequency_index;
    if (samplingFrequencyIndex >= SrsAacSampleRateUnset) {
        switch (codec->sound_rate) {
            case SrsAudioSampleRate5512:
                samplingFrequencyIndex = 0x0c; break;
            case SrsAudioSampleRate11025:
                samplingFrequencyIndex = 0x0a; break;
            case SrsAudioSampleRate22050:
                samplingFrequencyIndex = 0x07; break;
            case SrsAudioSampleRate44100:
                samplingFrequencyIndex = 0x04; break;
            default:
                break;
        }
    }
    if (samplingFrequencyIndex >= SrsAacSampleRateUnset) {
        return srs_error_new(ERROR_AAC_DATA_INVALID, "invalid sample index %d", samplingFrequencyIndex);
    }

    char chs[2];
    // @see ISO_IEC_14496-3-AAC-2001.pdf
    // AudioSpecificConfig (), page 33
    // 1.6.2.1 AudioSpecificConfig
    // audioObjectType; 5 bslbf
    chs[0] = (audioObjectType << 3) & 0xf8;
    // 3bits left.
    
    // samplingFrequencyIndex; 4 bslbf
    chs[0] |= (samplingFrequencyIndex >> 1) & 0x07;
    chs[1] = (samplingFrequencyIndex << 7) & 0x80;
    // 7bits left.
    
    // channelConfiguration; 4 bslbf
    chs[1] |= (channelConfiguration << 3) & 0x78;
    // 3bits left.
    
    // GASpecificConfig(), page 451
    // 4.4.1 Decoder configuration (GASpecificConfig)
    // frameLengthFlag; 1 bslbf
    // dependsOnCoreCoder; 1 bslbf
    // extensionFlag; 1 bslbf
    sh = string((char*)chs, sizeof(chs));
    
    return err;
}

srs_error_t SrsRawAacStream::mux_aac2flv(char* frame, int nb_frame, SrsRawAacStreamCodec* codec, uint32_t dts, char** flv, int* nb_flv)
{
    srs_error_t err = srs_success;
    
    char sound_format = codec->sound_format;
    char sound_type = codec->sound_type;
    char sound_size = codec->sound_size;
    char sound_rate = codec->sound_rate;
    char aac_packet_type = codec->aac_packet_type;
    
    // for audio frame, there is 1 or 2 bytes header:
    //      1bytes, SoundFormat|SoundRate|SoundSize|SoundType
    //      1bytes, AACPacketType for SoundFormat == 10, 0 is sequence header.
    int size = nb_frame + 1;
    if (sound_format == SrsAudioCodecIdAAC) {
        size += 1;
    }
    char* data = new char[size];
    char* p = data;
    
    uint8_t audio_header = sound_type & 0x01;
    audio_header |= (sound_size << 1) & 0x02;
    audio_header |= (sound_rate << 2) & 0x0c;
    audio_header |= (sound_format << 4) & 0xf0;
    
    *p++ = audio_header;
    
    if (sound_format == SrsAudioCodecIdAAC) {
        *p++ = aac_packet_type;
    }
    
    memcpy(p, frame, nb_frame);
    
    *flv = data;
    *nb_flv = size;
    
    return err;
}

