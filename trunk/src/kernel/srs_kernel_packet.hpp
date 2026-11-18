//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_KERNEL_PACKET_HPP
#define SRS_KERNEL_PACKET_HPP

#include <srs_core.hpp>

#include <srs_kernel_codec.hpp>

// A NALU for H.264 and H.265, or the whole raw frame for AAC. The SPS/PPS and the AAC ASC of a
// sequence header are not samples.
class SrsNaluSample
{
public:
    int size_;
    char *bytes_;

public:
    SrsNaluSample();
    SrsNaluSample(char *b, int s);
    ~SrsNaluSample();

public:
    SrsNaluSample *copy();
};

// A media frame with its timing, not parsed by codec.
class SrsMediaPacket
{
public:
    // The timebase depends on the context.
    int64_t timestamp_;
    SrsFrameType message_type_;

public:
    // Optional, used by some protocols such as RTMP.
    int32_t stream_id_;
    // Optional, used only by RTMP. When unset, RTMP picks a chunk stream id by the message type.
    int32_t prefer_cid_;

public:
    SrsSharedPtr<SrsMemoryBlock> payload_;

public:
    SrsMediaPacket();
    virtual ~SrsMediaPacket();

public:
    char *payload() { return payload_.get() ? payload_->payload() : NULL; }
    int size() { return payload_.get() ? payload_->size() : 0; }

public:
    virtual void wrap(char *payload, int size);
    virtual bool check(int stream_id);

public:
    virtual bool is_av();
    virtual bool is_audio();
    virtual bool is_video();

public:
    virtual SrsMediaPacket *copy();
};

class SrsParsedPacket
{
public:
    // In milliseconds.
    int64_t dts_;
    // PTS = DTS + CTS.
    int32_t cts_;

public:
    SrsCodecConfig *codec_;
    int nb_samples_;
    SrsNaluSample samples_[SrsMaxNbSamples];

public:
    SrsParsedPacket();
    virtual ~SrsParsedPacket();

public:
    virtual srs_error_t initialize(SrsCodecConfig *c);
    virtual srs_error_t add_sample(char *bytes, int size);

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual SrsParsedPacket *copy();
    virtual void do_copy(SrsParsedPacket *p);
};

class SrsParsedAudioPacket : public SrsParsedPacket
{
public:
    SrsAudioAacFrameTrait aac_packet_type_;

public:
    SrsParsedAudioPacket();
    virtual ~SrsParsedAudioPacket();

public:
    virtual SrsAudioCodecConfig *acodec();

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual SrsParsedAudioPacket *copy();
};

class SrsParsedVideoPacket : public SrsParsedPacket
{
public:
    SrsVideoAvcFrameType frame_type_;
    SrsVideoAvcFrameTrait avc_packet_type_;
    bool has_idr_;
    bool has_aud_;
    bool has_sps_pps_;
    SrsAvcNaluType first_nalu_type_;

public:
    SrsParsedVideoPacket();
    virtual ~SrsParsedVideoPacket();

public:
    virtual srs_error_t initialize(SrsCodecConfig *c);
    // The sample has no AnnexB start code or IBMF length prefix.
    virtual srs_error_t add_sample(char *bytes, int size);

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual SrsParsedVideoPacket *copy();

public:
    virtual SrsVideoCodecConfig *vcodec();

public:
    static srs_error_t parse_avc_nalu_type(const SrsNaluSample *sample, SrsAvcNaluType &avc_nalu_type);
    static srs_error_t parse_avc_bframe(const SrsNaluSample *sample, bool &is_b_frame);
    static srs_error_t parse_hevc_nalu_type(const SrsNaluSample *sample, SrsHevcNaluType &hevc_nalu_type);
    static srs_error_t parse_hevc_bframe(const SrsNaluSample *sample, SrsFormat *format, bool &is_b_frame);
};

// A stream may have only audio, such as a radio app.
class ISrsFormat
{
public:
    ISrsFormat();
    virtual ~ISrsFormat();

public:
    virtual srs_error_t initialize() = 0;
    // The data is an FLV audio tag body.
    virtual srs_error_t on_audio(int64_t timestamp, char *data, int size) = 0;
    // The data is an FLV video tag body.
    virtual srs_error_t on_video(int64_t timestamp, char *data, int size) = 0;
    virtual srs_error_t on_aac_sequence_header(char *data, int size) = 0;

public:
    virtual bool is_aac_sequence_header() = 0;
    virtual bool is_mp3_sequence_header() = 0;
    // TODO: is avc|hevc|av1 sequence header
    virtual bool is_avc_sequence_header() = 0;

public:
    virtual SrsParsedAudioPacket *audio() = 0;
    virtual SrsAudioCodecConfig *acodec() = 0;
    virtual SrsParsedVideoPacket *video() = 0;
    virtual SrsVideoCodecConfig *vcodec() = 0;
};

class SrsFormat : public ISrsFormat
{
public:
    SrsParsedAudioPacket *audio_;
    SrsAudioCodecConfig *acodec_;
    SrsParsedVideoPacket *video_;
    SrsVideoCodecConfig *vcodec_;

public:
    char *raw_;
    int nb_raw_;

public:
    // TODO: FIXME: Refine it.
    bool avc_parse_sps_;
    bool try_annexb_first_;

public:
    SrsFormat();
    virtual ~SrsFormat();

public:
    virtual srs_error_t initialize();
    virtual srs_error_t on_audio(int64_t timestamp, char *data, int size);
    virtual srs_error_t on_video(int64_t timestamp, char *data, int size);
    virtual srs_error_t on_aac_sequence_header(char *data, int size);

public:
    virtual bool is_aac_sequence_header();
    virtual bool is_mp3_sequence_header();
    // TODO: is avc|hevc|av1 sequence header
    virtual bool is_avc_sequence_header();

public:
    virtual SrsParsedAudioPacket *audio();
    virtual SrsAudioCodecConfig *acodec();
    virtual SrsParsedVideoPacket *video();
    virtual SrsVideoCodecConfig *vcodec();

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // The packet is an FLV video tag body, in H.264 or H.265.
    virtual srs_error_t video_avc_demux(SrsBuffer *stream, int64_t timestamp);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t hevc_demux_hvcc(SrsBuffer *stream);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t hevc_demux_vps_sps_pps(SrsHevcHvccNalu *nal);
    virtual srs_error_t hevc_demux_vps_rbsp(char *rbsp, int nb_rbsp);
    virtual srs_error_t hevc_demux_sps_rbsp(char *rbsp, int nb_rbsp);
    virtual srs_error_t hevc_demux_pps_rbsp(char *rbsp, int nb_rbsp);
    virtual srs_error_t hevc_demux_rbsp_ptl(SrsBitBuffer *bs, SrsHevcProfileTierLevel *ptl, int profile_present_flag, int max_sub_layers_minus1);

public:
    virtual srs_error_t hevc_demux_vps(SrsBuffer *stream);
    virtual srs_error_t hevc_demux_sps(SrsBuffer *stream);
    virtual srs_error_t hevc_demux_pps(SrsBuffer *stream);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t avc_demux_sps_pps(SrsBuffer *stream);
    virtual srs_error_t avc_demux_sps_rbsp(char *rbsp, int nb_rbsp);

public:
    // Parses the SPS in vcodec_->sequenceParameterSetNALUnit_.
    virtual srs_error_t avc_demux_sps();

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t video_nalu_demux(SrsBuffer *stream);
    // The "AnnexB" format of ISO_IEC_14496-10-AVC-2003.pdf, page 211.
    virtual srs_error_t avc_demux_annexb_format(SrsBuffer *stream);
    virtual srs_error_t do_avc_demux_annexb_format(SrsBuffer *stream);
    // The "ISO Base Media File Format" of ISO_IEC_14496-15-AVC-format-2012.pdf, page 20.
    virtual srs_error_t avc_demux_ibmf_format(SrsBuffer *stream);
    virtual srs_error_t do_avc_demux_ibmf_format(SrsBuffer *stream);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t audio_aac_demux(SrsBuffer *stream, int64_t timestamp);
    virtual srs_error_t audio_mp3_demux(SrsBuffer *stream, int64_t timestamp, bool fresh);

public:
    // The data is the AudioSpecificConfig, with no FLV audio tag header.
    virtual srs_error_t audio_aac_sequence_header_demux(char *data, int size);
};

#endif
