/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2020 Winlin
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

#ifndef SRS_PROTOCOL_RAW_HEVC_HPP
#define SRS_PROTOCOL_RAW_HEVC_HPP

#include <srs_core.hpp>
#include <srs_kernel_codec.hpp>
#include <string>

class SrsBuffer;

// The raw h.264 stream, in annexb.
class SrsRawHEVCStream
{
public:
    SrsRawHEVCStream();
    virtual ~SrsRawHEVCStream();
public:
    // Demux the stream in annexb format.
    // @param stream the input stream bytes.
    // @param pframe the output hevc frame in stream. user should never free it.
    // @param pnb_frame the output hevc frame size.
    virtual srs_error_t annexb_demux(SrsBuffer* stream, char** pframe, int* pnb_frame);
    // whether the frame is sps or pps or vps.
    virtual bool is_sps(char* frame, int nb_frame);
    virtual bool is_pps(char* frame, int nb_frame);
    virtual bool is_vps(char* frame, int nb_frame);

    // Demux the sps or pps or vps to string.
    // @param sps/pps output the sps/pps/vps.
    virtual srs_error_t sps_demux(char* frame, int nb_frame, std::string& sps);
    virtual srs_error_t pps_demux(char* frame, int nb_frame, std::string& pps);
    virtual srs_error_t vps_demux(char* frame, int nb_frame, std::string& vps);
public:
    // The hevc raw data to hevc packet, without flv payload header.
    // Mux the sps/pps/vps to flv sequence header packet.
    // @param sh output the sequence header.
    virtual srs_error_t mux_sequence_header(std::string sps, std::string pps, std::string vps,
                                            uint32_t dts, uint32_t pts, std::string& sh);
    // The hevc raw data to hevc packet, without flv payload header.
    // Mux the ibp to flv ibp packet.
    // @param ibp output the packet.
    // @param frame_type output the frame type.
    virtual srs_error_t mux_ipb_frame(char* frame, int nb_frame, std::string& ibp);
    // Mux the hevc video packet to flv video packet.
    // @param frame_type, SrsVideoAvcFrameTypeKeyFrame or SrsVideoAvcFrameTypeInterFrame.
    // @param avc_packet_type, SrsVideoAvcFrameTraitSequenceHeader or SrsVideoAvcFrameTraitNALU.
    // @param video the hevc raw data.
    // @param flv output the muxed flv packet.
    // @param nb_flv output the muxed flv size.
    virtual srs_error_t mux_avc2flv(std::string video, int8_t frame_type, int8_t avc_packet_type, uint32_t dts, uint32_t pts, char** flv, int* nb_flv);
};

#endif//end SRS_PROTOCOL_RAW_HEVC_HPP