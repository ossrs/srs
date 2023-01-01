//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#ifndef SRS_KERNEL_PS_HPP
#define SRS_KERNEL_PS_HPP

#include <srs_core.hpp>

#include <srs_kernel_ts.hpp>

class SrsPsPacket;
class SrsPsContext;

// The helper for PS decoding.
struct SrsPsDecodeHelper
{
public:
    // For debugging to get the RTP packet source. Not used in context.
    uint16_t rtp_seq_;
    // For debugging to get the RTP packet timestamp. Not used in context.
    uint32_t rtp_ts_;
    // For debugging to get the RTP packet payload type. Not used in context.
    uint8_t rtp_pt_;
public:
    // For debugging, current pack id. Not used in context.
    uint32_t pack_id_;
    // For debugging, the first sequence of current pack. Not used in context.
    uint16_t pack_first_seq_;
    // For debugging, the last sequence of previous message in current pack. Not used in context.
    uint16_t pack_pre_msg_last_seq_;
    // For debugging, the number of messages in pack. Not used in context.
    uint16_t pack_nn_msgs_;
public:
    // The PS context for decoding.
    SrsPsContext* ctx_;
    // The PS packet for decoding.
    SrsPsPacket* ps_;
public:
    SrsPsDecodeHelper();
};

// The PS message handler.
class ISrsPsMessageHandler : public ISrsTsHandler
{
public:
    ISrsPsMessageHandler();
    virtual ~ISrsPsMessageHandler();
public:
    // When enter recover mode, user should drop all messages in pack. The nn_recover indicates the number of retry
    // during recovery, that is 0 for the first time, and 1 for the second time.
    virtual void on_recover_mode(int nn_recover) = 0;
};

// The PS context, to process PS PES stream.
class SrsPsContext
{
public:
    SrsPsDecodeHelper helper_;
private:
    // The last decoding PS(TS) message.
    SrsTsMessage* last_;
    // The current parsing PS packet context.
    SrsPsPacket* current_;
    // Whether detect PS packet header integrity.
    bool detect_ps_integrity_;
public:
    // The stream type parsed from latest PSM packet.
    SrsTsStream video_stream_type_;
    SrsTsStream audio_stream_type_;
public:
    SrsPsContext();
    virtual ~SrsPsContext();
public:
    // Set whether detecting PS header integrity.
    void set_detect_ps_integrity(bool v);
    // Get the last PS(TS) message. Create one if not exists.
    SrsTsMessage* last();
    // Reap the last message and create a fresh one.
    SrsTsMessage* reap();
public:
    // Feed with ts packets, decode as ts message, callback handler if got one ts message.
    //      A ts video message can be decoded to NALUs by SrsRawH264Stream::annexb_demux.
    //      A ts audio message can be decoded to RAW frame by SrsRawAacStream::adts_demux.
    // @param handler The ts message handler to process the msg.
    // @remark We will consume all bytes in stream.
    virtual srs_error_t decode(SrsBuffer* stream, ISrsPsMessageHandler* handler);
private:
    srs_error_t do_decode(SrsBuffer* stream, ISrsPsMessageHandler* handler);
};

// The packet in ps stream.
// 2.5.3.3 Pack layer of Program Stream, hls-mpeg-ts-iso13818-1.pdf, page 73
class SrsPsPacket
{
public:
    SrsPsContext* context_;
public:
    // The global ID of pack header. The low 16 bits is reserved for seq. Automatically generate the high bits in
    // constructor.
    uint32_t id_;
    // Whether the PS pack stream has pack and system header.
    bool has_pack_header_;
    bool has_system_header_;
// Table 2-33 – Program Stream pack header, hls-mpeg-ts-iso13818-1.pdf, page 73
public:
    // 4B
    // The pack_start_code is the bit string '0000 0000 0000 0000 0000 0001 1011 1010' (0x000001BA). It identifies the
    // beginning of a pack.
    uint32_t pack_start_code_; // 32bits

    // 6B
    //      2bits const '01'
    //      3bits system_clock_reference_base [32..30]
    //      1bit marker_bit '1'
    //      15bits system_clock_reference_base [29..15]
    //      1bit marker_bit '1'
    //      22bits system_clock_reference_base [14..0]
    //      1bit marker_bit '1'
    //      9bits system_clock_reference_extension
    //      1bit marker_bit '1'
    // The system clock reference (SCR) is a 42-bit field coded in two parts. The first part,
    // system_clock_reference_base, is a 33-bit field whose value is given by SCR_base(i) as given in equation 2-19.
    // The second part, system_clock_reference_extension, is a 9-bit field whose value is given by SCR_ext(i), as given
    // in equation 2-20. The SCR indicates the intended time of arrival of the byte containing the last bit of the
    // system_clock_reference_base at the input of the program target decoder.
    uint64_t system_clock_reference_base_; // 32bits
    uint16_t system_clock_reference_extension_; // 9bits

    // 3B
    //      22bits program_mux_rate
    //      1bit marker_bit '1'
    //      1bit marker_bit '1'
    // This is a 22-bit integer specifying the rate at which the P-STD receives the Program Stream during the pack in
    // which it is included. The value of program_mux_rate is measured in units of 50 bytes/second. The value 0 is
    // forbidden. The value represented in program_mux_rate is used to define the time of arrival of bytes at the input
    // to the P-STD in 2.5.2. The value encoded in the program_mux_rate field may vary from pack to pack in an ITU-T
    // Rec. H.222.0 | ISO/IEC 13818-1 program multiplexed stream.
    uint32_t program_mux_rate_; // 22bits

    // 1B
    //      5bits reserved
    //      3bits pack_stuffing_length
    // A 3-bit integer specifying the number of stuffing bytes which follow this field.
    uint8_t pack_stuffing_length_; // 3bits
// Table 2-34 – Program Stream system header, hls-mpeg-ts-iso13818-1.pdf, page 74
public:
    // 4B
    // The system_header_start_code is the bit string '0000 0000 0000 0000 0000 0001 1011 1011' (0x000001BB). It
    // identifies the beginning of a system header.
    uint32_t system_header_start_code_;

    // 2B
    // This 16-bit field indicates the length in bytes of the system header following the header_length field. Future
    // extensions of this Specification may extend the system header.
    uint16_t header_length_;

    // 3B
    //      1bit marker_bit '1'
    //      22bits rate_bound
    //      1bit marker_bit '1'
    // A 22-bit field. The rate_bound is an integer value greater than or equal to the maximum value of the
    // program_mux_rate field coded in any pack of the Program Stream. It may be used by a decoder to assess whether it
    // is capable of decoding the entire stream.
    uint32_t rate_bound_;

    // 1B
    //      6bits audio_bound
    //      1bit fixed_flag
    //      1bit CSPS_flag
    // A 6-bit field. The audio_bound is an integer in the inclusive range from 0 to 32 and is set to a value greater
    // than or equal to the maximum number of ISO/IEC 13818-3 and ISO/IEC 11172-3 audio streams in the Program Stream
    // for which the decoding processes are simultaneously active. For the purpose of this subclause, the decoding
    // process of an ISO/IEC 13818-3 or ISO/IEC 11172-3 audio stream is active if the STD buffer is not empty or if a
    // Presentation Unit is being presented in the P-STD model.
    uint8_t audio_bound_;
    // The CSPS_flag is a 1-bit field. If its value is set to '1' the Program Stream meets the constraints defined in
    // 2.7.9.
    uint8_t CSPS_flag_;

    // 1B
    //      1bit system_audio_lock_flag
    //      1bit system_video_lock_flag
    //      1bit marker_bit '1'
    //      5bits video_bound
    // The system_audio_lock_flag is a 1-bit field indicating that there is a specified, constant rational relationship
    // between the audio sampling rate and the system_clock_frequency in the system target decoder. The
    // system_clock_frequency is defined in 2.5.2.1 and the audio sampling rate is specified in ISO/IEC 13818-3. The
    // system_audio_lock_flag may only be set to '1' if, for all presentation units in all audio elementary streams in
    // the Program Stream, the ratio of system_clock_frequency to the actual audio sampling rate, SCASR, is constant and
    // equal to the value indicated in the following table at the nominal sampling rate indicated in the audio stream.
    uint8_t system_audio_lock_flag_;
    // The system_video_lock_flag is a 1-bit field indicating that there is a specified, constant rational relationship
    // between the video frame rate and the system clock frequency in the system target decoder. Subclause 2.5.2.1
    // defines system_clock_frequency and the video frame rate is specified in ITU-T Rec. H.262 | ISO/IEC 13818-2. The
    // system_video_lock_flag may only be set to '1' if, for all presentation units in all video elementary streams in
    // the ITU-T Rec. H.222.0 | ISO/IEC 13818-1 program, the ratio of system_clock_frequency to the actual video frame
    // rate, SCFR, is constant and equal to the value indicated in the following table at the nominal frame rate
    // indicated in the video stream.
    uint8_t system_video_lock_flag_;
    // The video_bound is a 5-bit integer in the inclusive range from 0 to 16 and is set to a value greater than or
    // equal to the maximum number of ITU-T Rec. H.262 | ISO/IEC 13818-2 and ISO/IEC 11172-2 streams in the Program
    // Stream of which the decoding processes are simultaneously active. For the purpose of this subclause, the decoding
    // process of an ITU-T Rec. H.262 | ISO/IEC 13818-2 and ISO/IEC 11172-2 video stream is active if the P-STD buffer
    // is not empty, or if a Presentation Unit is being presented in the P-STD model, or if the reorder buffer is not
    // empty.
    uint8_t video_bound_;

    // 1B
    //      1bit packet_rate_restriction_flag
    //      5bits reserved_bits
    // The packet_rate_restriction_flag is a 1-bit flag. If the CSPS flag is set to '1', the
    // packet_rate_restriction_flag indicates which constraint is applicable to the packet rate, as specified in 2.7.9.
    // If the CSPS flag is set to value of '0', then the meaning of the packet_rate_restriction_flag is undefined.
    uint8_t packet_rate_restriction_flag_;

    // 3B
    //      8bits stream_id
    //      2bits fixed '11'
    //      1bit buffer_bound_scale
    //      13bits buffer_size_bound
    // Has some audio or video stream, by the next bit is 1.
    // Note that we ignore other streams except audio and video.
    //
    // The stream_id is an 8-bit field that indicates the coding and elementary stream number of the stream to which the
    // following P-STD_buffer_bound_scale and P-STD_buffer_size_bound fields refer.
    // If stream_id equals '1011 1000' the P-STD_buffer_bound_scale and P-STD_buffer_size_bound fields following the
    // stream_id refer to all audio streams in the Program Stream.
    // If stream_id equals '1011 1001' the P-STD_buffer_bound_scale and P-STD_buffer_size_bound fields following the
    // stream_id refer to all video streams in the Program Stream.
    // If the stream_id takes on any other value it shall be a byte value greater than or equal to '1011 1100' and shall
    // be interpreted as referring to the stream coding and elementary stream number according to Table 2-18.
    //
    uint8_t audio_stream_id_;
    uint8_t audio_buffer_bound_scale_;
    uint16_t audio_buffer_size_bound_;
    //
    uint8_t video_stream_id_;
    uint8_t video_buffer_bound_scale_;
    uint16_t video_buffer_size_bound_;
public:
    SrsPsPacket(SrsPsContext* context);
    virtual ~SrsPsPacket();
public:
    virtual srs_error_t decode(SrsBuffer* stream);
private:
    virtual srs_error_t decode_pack(SrsBuffer* stream);
    virtual srs_error_t decode_system(SrsBuffer* stream);
};

// The Program Stream Map (PSM) provides a description of the elementary streams in the Program Stream and their
// relationship to one another.
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
    // This is a 1-bit field, when set to '1' indicates that the Program Stream Map sent is currently applicable. When
    // the bit is set to '0', it indicates that the Program Stream Map sent is not yet applicable and shall be the next
    // table to become valid.
    uint8_t current_next_indicator_;
    // This 5-bit field is the version number of the whole Program Stream Map. The version number shall be incremented
    // by 1 modulo 32 whenever the definition of the Program Stream Map changes. When the current_next_indicator is set
    // to '1', then the program_stream_map_version shall be that of the currently applicable Program Stream Map. When
    // the current_next_indicator is set to '0', then the program_stream_map_version shall be that of the next
    // applicable Program Stream Map.
    uint8_t program_stream_map_version_;

    // 2B + [program_stream_info_length_]B
    // The program_stream_info_length is a 16-bit field indicating the total length of the descriptors immediately
    // following this field.
    uint16_t program_stream_info_length_;

    // 2B
    // This is a 16-bit field specifying the total length, in bytes, of all elementary stream information in this
    // program stream map. It includes the stream_type, elementary_stream_id, and elementary_stream_info_length fields.
    uint16_t elementary_stream_map_length_;

    // 4B + [elementary_stream_info_length_]B
    //      8bits stream_type
    //      8bits elementary_stream_id
    //      16bits elementary_stream_info_length
    // This 8-bit field specifies the type of the stream according to Table 2-29. The stream_type field shall only
    // identify elementary streams contained in PES packets. A value of 0x05 is prohibited.
    // The elementary_stream_id is an 8-bit field indicating the value of the stream_id field in the PES packet headers
    // of PES packets in which this elementary stream is stored.
    // The elementary_stream_info_length is a 16-bit field indicating the length in bytes of the descriptors immediately
    // following this field.
    // Definition for the descriptor() fields may be found in 2.6.
    //
    uint8_t video_stream_type_;
    uint8_t video_elementary_stream_id_;
    uint16_t video_elementary_stream_info_length_;
    //
    uint8_t audio_stream_type_;
    uint8_t audio_elementary_stream_id_;
    uint16_t audio_elementary_stream_info_length_;

    // 4B
    // This is a 32-bit field that contains the CRC value that gives a zero output of the registers in the decoder
    // defined in Annex A after processing the entire program stream map.
    uint32_t CRC_32_;
public:
    SrsPsPsmPacket();
    virtual ~SrsPsPsmPacket();
public:
    virtual srs_error_t decode(SrsBuffer* stream);
};

#endif

