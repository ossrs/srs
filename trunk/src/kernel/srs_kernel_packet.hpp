//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_KERNEL_PACKET_HPP
#define SRS_KERNEL_PACKET_HPP

#include <srs_core.hpp>

#include <srs_kernel_codec.hpp>

/**
 * A sample is the NAL unit of a parsed packet.
 * It's a NALU for H.264, H.265.
 * It's the whole AAC raw data for AAC.
 * @remark Neither SPS/PPS or ASC is sample unit, it's codec sequence header.
 */
class SrsNaluSample
{
public:
    // The size of unit.
    int size_;
    // The ptr of unit, user must free it.
    char *bytes_;

public:
    SrsNaluSample();
    SrsNaluSample(char *b, int s);
    ~SrsNaluSample();

public:
    // Copy sample, share the bytes pointer.
    SrsNaluSample *copy();
};

// A media packet containing raw, undecoded media data.
// This represents a single media frame or packet with timing information
// and payload data, but without codec-specific parsing or decoding.
class SrsMediaPacket
{
public:
    // Timestamp of the media packet. The timebase is defined by context.
    int64_t timestamp_;
    // Type of the media packet (audio, video, or script).
    SrsFrameType message_type_;

public:
    // Stream identifier for the packet. It's optional, so only used for some
    // protocols, for example, RTMP.
    int32_t stream_id_;

public:
    // Raw payload data of the media packet.
    SrsSharedPtr<SrsMemoryBlock> payload_;

public:
    SrsMediaPacket();
    virtual ~SrsMediaPacket();

public:
    // Backward compatibility accessors
    char *payload() { return payload_.get() ? payload_->payload() : NULL; }
    int size() { return payload_.get() ? payload_->size() : 0; }

public:
    // Create shared ptr message from RAW payload.
    // @remark Note that the header is set to zero.
    virtual void wrap(char *payload, int size);
    // check prefer cid and stream id.
    // @return whether stream id already set.
    virtual bool check(int stream_id);

public:
    virtual bool is_av();
    virtual bool is_audio();
    virtual bool is_video();

public:
    // copy current shared ptr message, use ref-count.
    // @remark, assert object is created.
    virtual SrsMediaPacket *copy();
};

// A parsed packet, consists of a codec and a group of samples.
class SrsParsedPacket
{
public:
    // The DTS/PTS in milliseconds, which is TBN=1000.
    int64_t dts_;
    // PTS = DTS + CTS.
    int32_t cts_;

public:
    // The codec info of frame.
    SrsCodecConfig *codec_;
    // The actual parsed number of samples.
    int nb_samples_;
    // The sampels cache.
    SrsNaluSample samples_[SrsMaxNbSamples];

public:
    SrsParsedPacket();
    virtual ~SrsParsedPacket();

public:
    // Initialize the frame, to parse sampels.
    virtual srs_error_t initialize(SrsCodecConfig *c);
    // Add a sample to frame.
    virtual srs_error_t add_sample(char *bytes, int size);

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    // Copy the packet.
    virtual SrsParsedPacket *copy();
    virtual void do_copy(SrsParsedPacket *p);
};

// A parsed audio packet, besides a frame, contains the audio frame info, such as frame type.
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

// A parsed video packet, besides a frame, contains the video frame info, such as frame type.
class SrsParsedVideoPacket : public SrsParsedPacket
{
public:
    // video specified
    SrsVideoAvcFrameType frame_type_;
    SrsVideoAvcFrameTrait avc_packet_type_;
    // whether sample_units contains IDR frame.
    bool has_idr_;
    // Whether exists AUD NALU.
    bool has_aud_;
    // Whether exists SPS/PPS NALU.
    bool has_sps_pps_;
    // The first nalu type.
    SrsAvcNaluType first_nalu_type_;

public:
    SrsParsedVideoPacket();
    virtual ~SrsParsedVideoPacket();

public:
    // Initialize the frame, to parse sampels.
    virtual srs_error_t initialize(SrsCodecConfig *c);
    // Add the sample without ANNEXB or IBMF header, or RAW AAC or MP3 data.
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

/**
 * Interface for codec format.
 * A codec format, including one or many stream, each stream identified by a frame.
 * For example, a typical RTMP stream format, consits of a video and audio frame.
 * Maybe some RTMP stream only has a audio stream, for instance, redio application.
 */
class ISrsFormat
{
public:
    ISrsFormat();
    virtual ~ISrsFormat();

public:
    // Initialize the format.
    virtual srs_error_t initialize() = 0;
    // When got a parsed audio packet.
    // @param data The data in FLV format.
    virtual srs_error_t on_audio(int64_t timestamp, char *data, int size) = 0;
    // When got a parsed video packet.
    // @param data The data in FLV format.
    virtual srs_error_t on_video(int64_t timestamp, char *data, int size) = 0;
    // When got a audio aac sequence header.
    virtual srs_error_t on_aac_sequence_header(char *data, int size) = 0;

public:
    virtual bool is_aac_sequence_header() = 0;
    virtual bool is_mp3_sequence_header() = 0;
    // TODO: is avc|hevc|av1 sequence header
    virtual bool is_avc_sequence_header() = 0;

public:
    // Getters for codec and packet information
    virtual SrsParsedAudioPacket *audio() = 0;
    virtual SrsAudioCodecConfig *acodec() = 0;
    virtual SrsParsedVideoPacket *video() = 0;
    virtual SrsVideoCodecConfig *vcodec() = 0;
};

/**
 * A codec format, including one or many stream, each stream identified by a frame.
 * For example, a typical RTMP stream format, consits of a video and audio frame.
 * Maybe some RTMP stream only has a audio stream, for instance, redio application.
 */
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
    // for sequence header, whether parse the h.264 sps.
    // TODO: FIXME: Refine it.
    bool avc_parse_sps_;
    // Whether try to parse in ANNEXB, then by IBMF.
    bool try_annexb_first_;

public:
    SrsFormat();
    virtual ~SrsFormat();

public:
    // Initialize the format.
    virtual srs_error_t initialize();
    // When got a parsed audio packet.
    // @param data The data in FLV format.
    virtual srs_error_t on_audio(int64_t timestamp, char *data, int size);
    // When got a parsed video packet.
    // @param data The data in FLV format.
    virtual srs_error_t on_video(int64_t timestamp, char *data, int size);
    // When got a audio aac sequence header.
    virtual srs_error_t on_aac_sequence_header(char *data, int size);

public:
    virtual bool is_aac_sequence_header();
    virtual bool is_mp3_sequence_header();
    // TODO: is avc|hevc|av1 sequence header
    virtual bool is_avc_sequence_header();

public:
    // Getters for codec and packet information
    virtual SrsParsedAudioPacket *audio();
    virtual SrsAudioCodecConfig *acodec();
    virtual SrsParsedVideoPacket *video();
    virtual SrsVideoCodecConfig *vcodec();

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // Demux the video packet in H.264 codec.
    // The packet is muxed in FLV format, defined in flv specification.
    //          Demux the sps/pps from sequence header.
    //          Demux the samples from NALUs.
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
    // Parse the H.264 SPS/PPS.
    virtual srs_error_t avc_demux_sps_pps(SrsBuffer *stream);
    virtual srs_error_t avc_demux_sps();
    virtual srs_error_t avc_demux_sps_rbsp(char *rbsp, int nb_rbsp);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // Parse the H.264 or H.265 NALUs.
    virtual srs_error_t video_nalu_demux(SrsBuffer *stream);
    // Demux the avc NALU in "AnnexB" from ISO_IEC_14496-10-AVC-2003.pdf, page 211.
    virtual srs_error_t avc_demux_annexb_format(SrsBuffer *stream);
    virtual srs_error_t do_avc_demux_annexb_format(SrsBuffer *stream);
    // Demux the avc NALU in "ISO Base Media File Format" from ISO_IEC_14496-15-AVC-format-2012.pdf, page 20
    virtual srs_error_t avc_demux_ibmf_format(SrsBuffer *stream);
    virtual srs_error_t do_avc_demux_ibmf_format(SrsBuffer *stream);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // Demux the audio packet in AAC codec.
    //          Demux the asc from sequence header.
    //          Demux the sampels from RAW data.
    virtual srs_error_t audio_aac_demux(SrsBuffer *stream, int64_t timestamp);
    virtual srs_error_t audio_mp3_demux(SrsBuffer *stream, int64_t timestamp, bool fresh);

public:
    // Directly demux the sequence header, without RTMP packet header.
    virtual srs_error_t audio_aac_sequence_header_demux(char *data, int size);
};

#endif
