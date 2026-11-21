//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_KERNEL_PS_HPP
#define SRS_KERNEL_PS_HPP

#include <srs_core.hpp>

#include <srs_kernel_ts.hpp>

class SrsPsPacket;
class SrsPsContext;

struct SrsPsDecodeHelper {
public:
    // Set by the caller from the current RTP packet.
    uint16_t rtp_seq_;
    uint32_t rtp_ts_;
    uint8_t rtp_pt_;

public:
    // For debugging logs, not used in decoding.
    uint32_t pack_id_;
    uint16_t pack_first_seq_;
    // The last sequence of the previous message in the current pack.
    uint16_t pack_pre_msg_last_seq_;
    uint16_t pack_nn_msgs_;

public:
    SrsPsContext *ctx_;
    SrsPsPacket *ps_;

public:
    SrsPsDecodeHelper();
};

class ISrsPsMessageHandler : public ISrsTsHandler
{
public:
    ISrsPsMessageHandler();
    virtual ~ISrsPsMessageHandler();

public:
    // In recover mode, the handler should drop all messages of the pack. The nn_recover is the retry count, 1 for the
    // first time and 2 for the second time.
    virtual void on_recover_mode(int nn_recover) = 0;
};

class ISrsPsContext
{
public:
    ISrsPsContext();
    virtual ~ISrsPsContext();

public:
    virtual SrsPsDecodeHelper *helper() = 0;
    virtual void set_detect_ps_integrity(bool v) = 0;
    virtual srs_error_t decode(SrsBuffer *stream, ISrsPsMessageHandler *handler) = 0;
    virtual SrsTsMessage *last() = 0;
    virtual SrsTsMessage *reap() = 0;
};

class SrsPsContext : public ISrsPsContext
{
public:
    SrsPsDecodeHelper helper_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    SrsTsMessage *last_;
    SrsPsPacket *current_;
    bool detect_ps_integrity_;

public:
    // From the latest PSM.
    SrsTsStream video_stream_type_;
    SrsTsStream audio_stream_type_;

public:
    SrsPsContext();
    virtual ~SrsPsContext();

public:
    void set_detect_ps_integrity(bool v);
    SrsTsMessage *last();
    SrsTsMessage *reap();
    virtual SrsPsDecodeHelper *helper();

public:
    // A video message holds AnnexB NALUs, see SrsRawH264Stream::annexb_demux. An audio message holds ADTS frames, see
    // SrsRawAacStream::adts_demux.
    virtual srs_error_t decode(SrsBuffer *stream, ISrsPsMessageHandler *handler);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srs_error_t do_decode(SrsBuffer *stream, ISrsPsMessageHandler *handler);
};

// 2.5.3.3 Pack layer of Program Stream, hls-mpeg-ts-iso13818-1.pdf, page 73
class SrsPsPacket
{
public:
    SrsPsContext *context_;

public:
    // A pack counter in the high 16 bits, and the RTP seq of the first packet of the pack in the low 16 bits.
    uint32_t id_;
    bool has_pack_header_;
    bool has_system_header_;
    // Table 2-33 – Program Stream pack header, hls-mpeg-ts-iso13818-1.pdf, page 73
public:
    // 4B
    uint32_t pack_start_code_; // 32bits

    // 6B
    //      2bits const '01'
    //      3bits system_clock_reference_base [32..30]
    //      1bit marker_bit '1'
    //      15bits system_clock_reference_base [29..15]
    //      1bit marker_bit '1'
    //      15bits system_clock_reference_base [14..0]
    //      1bit marker_bit '1'
    //      9bits system_clock_reference_extension
    //      1bit marker_bit '1'
    // The SCR is the intended time of arrival, at the decoder input, of the byte that holds the last bit of
    // system_clock_reference_base.
    uint64_t system_clock_reference_base_;      // 33bits
    uint16_t system_clock_reference_extension_; // 9bits

    // 3B
    //      22bits program_mux_rate
    //      1bit marker_bit '1'
    //      1bit marker_bit '1'
    // In units of 50 bytes/second. The value 0 is forbidden, and it may vary from pack to pack.
    uint32_t program_mux_rate_; // 22bits

    // 1B
    //      5bits reserved
    //      3bits pack_stuffing_length
    uint8_t pack_stuffing_length_; // 3bits
    // Table 2-34 – Program Stream system header, hls-mpeg-ts-iso13818-1.pdf, page 74
public:
    // 4B
    uint32_t system_header_start_code_;

    // 2B
    // Later versions of the spec may add fields to the system header.
    uint16_t header_length_;

    // 3B
    //      1bit marker_bit '1'
    //      22bits rate_bound
    //      1bit marker_bit '1'
    // At least the largest program_mux_rate of any pack in the stream.
    uint32_t rate_bound_;

    // 1B
    //      6bits audio_bound
    //      1bit fixed_flag
    //      1bit CSPS_flag
    // At least the largest number of audio streams decoded at the same time, from 0 to 32.
    uint8_t audio_bound_;
    // When 1, the stream meets the constraints of 2.7.9.
    uint8_t CSPS_flag_;

    // 1B
    //      1bit system_audio_lock_flag
    //      1bit system_video_lock_flag
    //      1bit marker_bit '1'
    //      5bits video_bound
    // When 1, the ratio of the system clock frequency to the audio sampling rate is constant.
    uint8_t system_audio_lock_flag_;
    // When 1, the ratio of the system clock frequency to the video frame rate is constant.
    uint8_t system_video_lock_flag_;
    // At least the largest number of video streams decoded at the same time, from 0 to 16.
    uint8_t video_bound_;

    // 1B
    //      1bit packet_rate_restriction_flag
    //      7bits reserved_bits
    // When CSPS_flag is 1, it selects the packet rate constraint of 2.7.9; otherwise it is undefined.
    uint8_t packet_rate_restriction_flag_;

    // 3B
    //      8bits stream_id
    //      2bits fixed '11'
    //      1bit buffer_bound_scale
    //      13bits buffer_size_bound
    // An entry follows while the next bit is 1.
    // The stream_id 0xB8 means all audio streams, 0xB9 means all video streams, and any other value is at least 0xBC,
    // a stream as in Table 2-18.
    uint8_t audio_stream_id_;
    uint8_t audio_buffer_bound_scale_;
    uint16_t audio_buffer_size_bound_;

    uint8_t video_stream_id_;
    uint8_t video_buffer_bound_scale_;
    uint16_t video_buffer_size_bound_;

public:
    SrsPsPacket(SrsPsContext *context);
    virtual ~SrsPsPacket();

public:
    virtual srs_error_t decode(SrsBuffer *stream);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t decode_pack(SrsBuffer *stream);
    virtual srs_error_t decode_system(SrsBuffer *stream);
};

// 2.5.4 Program Stream map, hls-mpeg-ts-iso13818-1.pdf, page 77
class SrsPsPsmPacket
{
public:
    // 2B
    //      1bit current_next_indicator
    //      2bits reserved
    //      5bits program_stream_map_version
    //      7bits reserved
    //      1bit marker_bit '1'
    // When 1, the PSM applies now; when 0, it is the next PSM to apply.
    uint8_t current_next_indicator_;
    // Incremented by 1 modulo 32 whenever the PSM changes.
    uint8_t program_stream_map_version_;

    // 2B + [program_stream_info_length_]B
    uint16_t program_stream_info_length_;

    // 2B
    uint16_t elementary_stream_map_length_;

    // 4B + [elementary_stream_info_length_]B
    //      8bits stream_type
    //      8bits elementary_stream_id
    //      16bits elementary_stream_info_length
    // The stream_type is from Table 2-29, and 0x05 is prohibited. The elementary_stream_id is the stream_id in the
    // headers of the PES packets that carry the stream. The descriptors are defined in 2.6.
    uint8_t video_stream_type_;
    uint8_t video_elementary_stream_id_;
    uint16_t video_elementary_stream_info_length_;

    uint8_t audio_stream_type_;
    uint8_t audio_elementary_stream_id_;
    uint16_t audio_elementary_stream_info_length_;

    // 4B
    // The CRC of the decoder in Annex A over the whole PSM.
    uint32_t CRC_32_;

public:
    SrsPsPsmPacket();
    virtual ~SrsPsPsmPacket();

public:
    virtual srs_error_t decode(SrsBuffer *stream);
};

#endif
