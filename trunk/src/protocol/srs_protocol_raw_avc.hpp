//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

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
    virtual srs_error_t mux_sequence_header(std::string sps, std::string pps, std::string& sh);
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

#ifdef SRS_H265
// The raw h.265 stream, in annexb.
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
    virtual srs_error_t annexb_demux(SrsBuffer *stream, char **pframe, int *pnb_frame);
    // whether the frame is sps or pps or vps.
    virtual bool is_sps(char *frame, int nb_frame);
    virtual bool is_pps(char *frame, int nb_frame);
    virtual bool is_vps(char *frame, int nb_frame);

    // Demux the sps or pps or vps to string.
    // @param sps/pps output the sps/pps/vps.
    virtual srs_error_t sps_demux(char *frame, int nb_frame, std::string &sps);
    virtual srs_error_t pps_demux(char *frame, int nb_frame, std::string &pps);
    virtual srs_error_t vps_demux(char *frame, int nb_frame, std::string &vps);

public:
    // The hevc raw data to hevc packet, without flv payload header.
    // Mux the sps/pps/vps to flv sequence header packet.
    // @param sh output the sequence header.
    virtual srs_error_t mux_sequence_header(std::string vps, std::string sps, std::string pps, std::string &sh);
    // The hevc raw data to hevc packet, without flv payload header.
    // Mux the ibp to flv ibp packet.
    // @param ibp output the packet.
    // @param frame_type output the frame type.
    virtual srs_error_t mux_ipb_frame(char *frame, int nb_frame, std::string &ibp);
    // Mux the hevc video packet to flv video packet.
    // @param frame_type, SrsVideoAvcFrameTypeKeyFrame or SrsVideoAvcFrameTypeInterFrame.
    // @param avc_packet_type, SrsVideoAvcFrameTraitSequenceHeader or SrsVideoAvcFrameTraitNALU.
    // @param video the hevc raw data.
    // @param flv output the muxed flv packet.
    // @param nb_flv output the muxed flv size.
    virtual srs_error_t mux_avc2flv(std::string video, int8_t frame_type, int8_t avc_packet_type, uint32_t dts, uint32_t pts, char **flv, int *nb_flv);
};
#endif

// The header of adts sample.
struct SrsRawAacStreamCodec
{
    // Codec level informations.
    int8_t protection_absent;
    SrsAacObjectType aac_object;
    int8_t sampling_frequency_index;
    int8_t channel_configuration;
    int16_t frame_length;

    // Format level, RTMP as such, informations.
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
