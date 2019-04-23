/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2019 Winlin
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

#ifndef SRS_PROTOCOL_RAW_AVC_HPP
#define SRS_PROTOCOL_RAW_AVC_HPP

#include <srs_core.hpp>

#include <string>

#include <srs_kernel_codec.hpp>

class SrsBuffer;

// The raw h.264 stream, in annexb.
class SrsRawH264Stream
{
public:
    SrsRawH264Stream();
    virtual ~SrsRawH264Stream();
public:
    // Demux the stream in annexb format.
    // @param stream the input stream bytes.
    // @param pframe the output h.264 frame in stream. user should never free it.
    // @param pnb_frame the output h.264 frame size.
    virtual srs_error_t annexb_demux(SrsBuffer* stream, char** pframe, int* pnb_frame);
    // whether the frame is sps or pps.
    virtual bool is_sps(char* frame, int nb_frame);
    virtual bool is_pps(char* frame, int nb_frame);
    // Demux the sps or pps to string.
    // @param sps/pps output the sps/pps.
    virtual srs_error_t sps_demux(char* frame, int nb_frame, std::string& sps);
    virtual srs_error_t pps_demux(char* frame, int nb_frame, std::string& pps);
public:
    // The h264 raw data to h264 packet, without flv payload header.
    // Mux the sps/pps to flv sequence header packet.
    // @param sh output the sequence header.
    virtual srs_error_t mux_sequence_header(std::string sps, std::string pps, uint32_t dts, uint32_t pts, std::string& sh);
    // The h264 raw data to h264 packet, without flv payload header.
    // Mux the ibp to flv ibp packet.
    // @param ibp output the packet.
    // @param frame_type output the frame type.
    virtual srs_error_t mux_ipb_frame(char* frame, int nb_frame, std::string& ibp);
    // Mux the avc video packet to flv video packet.
    // @param frame_type, SrsVideoAvcFrameTypeKeyFrame or SrsVideoAvcFrameTypeInterFrame.
    // @param avc_packet_type, SrsVideoAvcFrameTraitSequenceHeader or SrsVideoAvcFrameTraitNALU.
    // @param video the h.264 raw data.
    // @param flv output the muxed flv packet.
    // @param nb_flv output the muxed flv size.
    virtual srs_error_t mux_avc2flv(std::string video, int8_t frame_type, int8_t avc_packet_type, uint32_t dts, uint32_t pts, char** flv, int* nb_flv);
};

// The header of adts sample.
struct SrsRawAacStreamCodec
{
    int8_t protection_absent;
    SrsAacObjectType aac_object;
    int8_t sampling_frequency_index;
    int8_t channel_configuration;
    int16_t frame_length;
    
    char sound_format;
    char sound_rate;
    char sound_size;
    char sound_type;
    // 0 for sh; 1 for raw data.
    int8_t aac_packet_type;
};

// The raw aac stream, in adts.
class SrsRawAacStream
{
public:
    SrsRawAacStream();
    virtual ~SrsRawAacStream();
public:
    // Demux the stream in adts format.
    // @param stream the input stream bytes.
    // @param pframe the output aac frame in stream. user should never free it.
    // @param pnb_frame the output aac frame size.
    // @param codec the output codec info.
    virtual srs_error_t adts_demux(SrsBuffer* stream, char** pframe, int* pnb_frame, SrsRawAacStreamCodec& codec);
    // Mux aac raw data to aac packet, without flv payload header.
    // Mux the aac specific config to flv sequence header packet.
    // @param sh output the sequence header.
    virtual srs_error_t mux_sequence_header(SrsRawAacStreamCodec* codec, std::string& sh);
    // Mux the aac audio packet to flv audio packet.
    // @param frame the aac raw data.
    // @param nb_frame the count of aac frame.
    // @param codec the codec info of aac.
    // @param flv output the muxed flv packet.
    // @param nb_flv output the muxed flv size.
    virtual srs_error_t mux_aac2flv(char* frame, int nb_frame, SrsRawAacStreamCodec* codec, uint32_t dts, char** flv, int* nb_flv);
};

#endif
