//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_PROTOCOL_RAW_AVC_HPP
#define SRS_PROTOCOL_RAW_AVC_HPP

#include <srs_core.hpp>

#include <string>

#include <srs_kernel_codec.hpp>

class SrsBuffer;

class ISrsRawH264Stream
{
public:
    ISrsRawH264Stream();
    virtual ~ISrsRawH264Stream();

public:
    virtual srs_error_t annexb_demux(SrsBuffer *stream, char **pframe, int *pnb_frame) = 0;
    virtual bool is_sps(char *frame, int nb_frame) = 0;
    virtual bool is_pps(char *frame, int nb_frame) = 0;
    virtual srs_error_t sps_demux(char *frame, int nb_frame, std::string &sps) = 0;
    virtual srs_error_t pps_demux(char *frame, int nb_frame, std::string &pps) = 0;
    virtual srs_error_t mux_sequence_header(std::string sps, std::string pps, std::string &sh) = 0;
    virtual srs_error_t mux_ipb_frame(char *frame, int nb_frame, std::string &ibp) = 0;
    virtual srs_error_t mux_avc2flv(std::string video, int8_t frame_type, int8_t avc_packet_type, uint32_t dts, uint32_t pts, char **flv, int *nb_flv) = 0;
};

// The raw H.264 stream, in AnnexB.
class SrsRawH264Stream : public ISrsRawH264Stream
{
public:
    SrsRawH264Stream();
    virtual ~SrsRawH264Stream();

public:
    // The frame points into the stream; never free it.
    virtual srs_error_t annexb_demux(SrsBuffer *stream, char **pframe, int *pnb_frame);
    virtual bool is_sps(char *frame, int nb_frame);
    virtual bool is_pps(char *frame, int nb_frame);
    virtual srs_error_t sps_demux(char *frame, int nb_frame, std::string &sps);
    virtual srs_error_t pps_demux(char *frame, int nb_frame, std::string &pps);

public:
    // The output has no FLV payload header.
    virtual srs_error_t mux_sequence_header(std::string sps, std::string pps, std::string &sh);
    // The output has no FLV payload header.
    virtual srs_error_t mux_ipb_frame(char *frame, int nb_frame, std::string &ibp);
    // The frame_type is SrsVideoAvcFrameTypeKeyFrame or SrsVideoAvcFrameTypeInterFrame.
    // The avc_packet_type is SrsVideoAvcFrameTraitSequenceHeader or SrsVideoAvcFrameTraitNALU.
    virtual srs_error_t mux_avc2flv(std::string video, int8_t frame_type, int8_t avc_packet_type, uint32_t dts, uint32_t pts, char **flv, int *nb_flv);
};

class ISrsRawHEVCStream
{
public:
    ISrsRawHEVCStream();
    virtual ~ISrsRawHEVCStream();

public:
    virtual srs_error_t annexb_demux(SrsBuffer *stream, char **pframe, int *pnb_frame) = 0;
    virtual bool is_sps(char *frame, int nb_frame) = 0;
    virtual bool is_pps(char *frame, int nb_frame) = 0;
    virtual bool is_vps(char *frame, int nb_frame) = 0;
    virtual srs_error_t sps_demux(char *frame, int nb_frame, std::string &sps) = 0;
    virtual srs_error_t pps_demux(char *frame, int nb_frame, std::string &pps) = 0;
    virtual srs_error_t vps_demux(char *frame, int nb_frame, std::string &vps) = 0;
    virtual srs_error_t mux_sequence_header(std::string vps, std::string sps, std::vector<std::string> &pps, std::string &sh) = 0;
    virtual srs_error_t mux_ipb_frame(char *frame, int nb_frame, std::string &ibp) = 0;
    virtual srs_error_t mux_hevc2flv(std::string video, int8_t frame_type, int8_t avc_packet_type, uint32_t dts, uint32_t pts, char **flv, int *nb_flv) = 0;
    virtual srs_error_t mux_hevc2flv_enhanced(std::string video, int8_t frame_type, int8_t packet_type, uint32_t dts, uint32_t pts, char **flv, int *nb_flv) = 0;
};

// The raw H.265 stream, in AnnexB.
class SrsRawHEVCStream : public ISrsRawHEVCStream
{
public:
    SrsRawHEVCStream();
    virtual ~SrsRawHEVCStream();

public:
    // The frame points into the stream; never free it.
    virtual srs_error_t annexb_demux(SrsBuffer *stream, char **pframe, int *pnb_frame);
    virtual bool is_sps(char *frame, int nb_frame);
    virtual bool is_pps(char *frame, int nb_frame);
    virtual bool is_vps(char *frame, int nb_frame);

    virtual srs_error_t sps_demux(char *frame, int nb_frame, std::string &sps);
    virtual srs_error_t pps_demux(char *frame, int nb_frame, std::string &pps);
    virtual srs_error_t vps_demux(char *frame, int nb_frame, std::string &vps);

public:
    // The output has no FLV payload header.
    virtual srs_error_t mux_sequence_header(std::string vps, std::string sps, std::vector<std::string> &pps, std::string &sh);
    // The output has no FLV payload header.
    virtual srs_error_t mux_ipb_frame(char *frame, int nb_frame, std::string &ibp);
    // FLV codec ID 12 for HEVC is a non-standard extension used by CDN vendors in China.
    // Enhanced RTMP, in mux_hevc2flv_enhanced, is the more universal standard.
    // The frame_type is SrsVideoAvcFrameTypeKeyFrame or SrsVideoAvcFrameTypeInterFrame.
    // The avc_packet_type is SrsVideoAvcFrameTraitSequenceHeader or SrsVideoAvcFrameTraitNALU.
    virtual srs_error_t mux_hevc2flv(std::string video, int8_t frame_type, int8_t avc_packet_type, uint32_t dts, uint32_t pts, char **flv, int *nb_flv);
    // Enhanced RTMP identifies HEVC by the fourcc 'hvc1' instead of a codec ID.
    // @see https://veovera.org/docs/enhanced/enhanced-rtmp-v1.pdf
    // The packet_type is SrsVideoHEVCFrameTraitPacketTypeSequenceStart, SrsVideoHEVCFrameTraitPacketTypeCodedFrames
    //      with the composition time pts - dts, or SrsVideoHEVCFrameTraitPacketTypeCodedFramesX without it.
    // The frame_type is SrsVideoAvcFrameTypeKeyFrame or SrsVideoAvcFrameTypeInterFrame.
    virtual srs_error_t mux_hevc2flv_enhanced(std::string video, int8_t frame_type, int8_t packet_type, uint32_t dts, uint32_t pts, char **flv, int *nb_flv);
};

struct SrsRawAacStreamCodec {
    int8_t protection_absent_;
    SrsAacObjectType aac_object_;
    int8_t sampling_frequency_index_;
    int8_t channel_configuration_;
    int16_t frame_length_;

    // The RTMP audio fields.
    char sound_format_;
    char sound_rate_;
    char sound_size_;
    char sound_type_;
    // 0 for the sequence header, 1 for raw data.
    int8_t aac_packet_type_;
};

class ISrsRawAacStream
{
public:
    ISrsRawAacStream();
    virtual ~ISrsRawAacStream();

public:
    virtual srs_error_t adts_demux(SrsBuffer *stream, char **pframe, int *pnb_frame, SrsRawAacStreamCodec &codec) = 0;
    virtual srs_error_t mux_sequence_header(SrsRawAacStreamCodec *codec, std::string &sh) = 0;
    virtual srs_error_t mux_aac2flv(char *frame, int nb_frame, SrsRawAacStreamCodec *codec, uint32_t dts, char **flv, int *nb_flv) = 0;
};

// The raw AAC stream, in ADTS.
class SrsRawAacStream : public ISrsRawAacStream
{
public:
    SrsRawAacStream();
    virtual ~SrsRawAacStream();

public:
    // The frame points into the stream; never free it.
    virtual srs_error_t adts_demux(SrsBuffer *stream, char **pframe, int *pnb_frame, SrsRawAacStreamCodec &codec);
    // The output has no FLV payload header.
    virtual srs_error_t mux_sequence_header(SrsRawAacStreamCodec *codec, std::string &sh);
    virtual srs_error_t mux_aac2flv(char *frame, int nb_frame, SrsRawAacStreamCodec *codec, uint32_t dts, char **flv, int *nb_flv);
};

// The ADTS syncword is '1111 1111 1111'B, that is 0xFFF.
// @see ISO_IEC_14496-3-AAC-2001.pdf, page 75, 1.A.2.2 ADTS.
extern bool srs_aac_startswith_adts(SrsBuffer *stream);

#endif
