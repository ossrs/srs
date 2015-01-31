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

