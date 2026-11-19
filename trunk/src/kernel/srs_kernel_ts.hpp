//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_KERNEL_TS_HPP
#define SRS_KERNEL_TS_HPP

#include <srs_core.hpp>

#include <map>
#include <string>
#include <vector>

#include <srs_kernel_codec.hpp>
#include <srs_kernel_file.hpp>
#include <srs_kernel_packet.hpp>

typedef struct evp_cipher_ctx_st EVP_CIPHER_CTX;

class SrsBuffer;
class SrsTsMessageCache;
class ISrsTsMessageCache;
class SrsTsContextWriter;
class ISrsTsContextWriter;
class ISrsStreamWriter;
class SrsFileReader;
class SrsFormat;
class ISrsFormat;
class SrsSimpleStream;
class SrsTsAdaptationField;
class SrsTsPayload;
class SrsTsMessage;
class SrsTsPacket;
class ISrsTsContext;
class SrsTsContext;
class SrsPsPacket;

// Transport Stream packets are 188 bytes in length.
#define SRS_TS_PACKET_SIZE 188

// In 90kHz units, 720ms.
#define SRS_CONSTS_HLS_PURE_AUDIO_AGGREGATE 720 * 90

// Table 2-3 - PID table, hls-mpeg-ts-iso13818-1.pdf, page 37
// Only PIDs 0x0000, 0x0001 and 0x0010-0x1FFE may carry a PCR.
enum SrsTsPid {
    SrsTsPidPAT = 0x00,
    SrsTsPidCAT = 0x01,
    SrsTsPidTSDT = 0x02,
    SrsTsPidReservedStart = 0x03,
    SrsTsPidReservedEnd = 0x0f,
    // May be assigned as network_PID, Program_map_PID, elementary_PID, or for other purposes.
    SrsTsPidAppStart = 0x10,
    SrsTsPidAppEnd = 0x1ffe,
    SrsTsPidNULL = 0x01FFF,
};

// Table 2-4 - Scrambling control values, hls-mpeg-ts-iso13818-1.pdf, page 38
enum SrsTsScrambled {
    SrsTsScrambledDisabled = 0x00,
    SrsTsScrambledUserDefined1 = 0x01,
    SrsTsScrambledUserDefined2 = 0x02,
    SrsTsScrambledUserDefined3 = 0x03,
};

// Table 2-5 - Adaptation field control values, hls-mpeg-ts-iso13818-1.pdf, page 38
enum SrsTsAdaptationFieldType {
    SrsTsAdaptationFieldTypeReserved = 0x00,
    SrsTsAdaptationFieldTypePayloadOnly = 0x01,
    SrsTsAdaptationFieldTypeAdaptionOnly = 0x02,
    SrsTsAdaptationFieldTypeBoth = 0x03,
};

// What a PID carries. The PAT has a fixed PID; the PAT gives the PMT PIDs, and the PMT gives the
// video and audio PIDs.
enum SrsTsPidApply {
    SrsTsPidApplyReserved = 0,

    SrsTsPidApplyPAT,
    SrsTsPidApplyPMT,

    SrsTsPidApplyVideo,
    SrsTsPidApplyAudio,
};

// Table 2-29 - Stream type assignments, hls-mpeg-ts-iso13818-1.pdf, page 66
enum SrsTsStream {
    SrsTsStreamReserved = 0x00,
    SrsTsStreamForbidden = 0xff,
    // ISO/IEC 11172 Audio
    SrsTsStreamAudioMp3 = 0x03,
    // ISO/IEC 13818-7 Audio with ADTS transport syntax
    SrsTsStreamAudioAAC = 0x0f,
    // ISO/IEC 14496-2 Visual
    SrsTsStreamVideoMpeg4 = 0x10,
    // ISO/IEC 14496-3 Audio with the LATM transport syntax as defined in ISO/IEC 14496-3 / AMD 1
    SrsTsStreamAudioMpeg4 = 0x11,
    SrsTsStreamVideoH264 = 0x1b,
    SrsTsStreamVideoHEVC = 0x24,
    // 0x80-0xFF are User Private.
    SrsTsStreamAudioAC3 = 0x81,
    SrsTsStreamAudioDTS = 0x8a,
};
std::string srs_ts_stream2string(SrsTsStream stream);

struct SrsTsChannel {
    int pid_;
    SrsTsPidApply apply_;
    SrsTsStream stream_;
    SrsTsMessage *msg_;
    ISrsTsContext *context_;
    // For the encoder.
    uint8_t continuity_counter_;

    SrsTsChannel();
    virtual ~SrsTsChannel();
};

// Table 2-18 - Stream_id assignments, hls-mpeg-ts-iso13818-1.pdf, page 52.
enum SrsTsPESStreamId {
    SrsTsPESStreamIdProgramStreamMap = 0xbc,
    SrsTsPESStreamIdPrivateStream1 = 0xbd,
    SrsTsPESStreamIdPaddingStream = 0xbe,
    SrsTsPESStreamIdPrivateStream2 = 0xbf,

    // 110x xxxx: ISO/IEC 13818-3 or ISO/IEC 11172-3 or ISO/IEC 13818-7 or ISO/IEC 14496-3 audio
    // stream number x xxxx. The checker is the top 3 bits.
    SrsTsPESStreamIdAudioChecker = 0x06,
    SrsTsPESStreamIdAudioCommon = 0xc0,

    // 1110 xxxx: ITU-T Rec. H.262 | ISO/IEC 13818-2 or ISO/IEC 11172-2 or ISO/IEC 14496-2 video
    // stream number xxxx. The checker is the top 4 bits.
    SrsTsPESStreamIdVideoChecker = 0x0e,
    SrsTsPESStreamIdVideoCommon = 0xe0,

    SrsTsPESStreamIdEcmStream = 0xf0,
    SrsTsPESStreamIdEmmStream = 0xf1,
    SrsTsPESStreamIdDsmccStream = 0xf2,
    SrsTsPESStreamId13522Stream = 0xf3,
    SrsTsPESStreamIdH2221TypeA = 0xf4,
    SrsTsPESStreamIdH2221TypeB = 0xf5,
    SrsTsPESStreamIdH2221TypeC = 0xf6,
    SrsTsPESStreamIdH2221TypeD = 0xf7,
    SrsTsPESStreamIdH2221TypeE = 0xf8,
    SrsTsPESStreamIdAncillaryStream = 0xf9,
    SrsTsPESStreamIdSlPacketizedStream = 0xfa,
    SrsTsPESStreamIdFlexMuxStream = 0xfb,
    // 0xfc to 0xfe are reserved data streams.
    SrsTsPESStreamIdProgramStreamDirectory = 0xff,
};

// The audio or video message of a PES packet.
class SrsTsMessage
{
public:
    // Set by the decoder for the handler; the message does not use them.
    SrsTsChannel *channel_;
    SrsTsPacket *packet_;
    // Set by the PS decoder, to get the RTP packet source.
    void *ps_helper_;

public:
    int64_t start_pts_;
    bool write_pcr_;
    bool is_discontinuity_;

public:
    uint8_t continuity_counter_;

public:
    // In 90kHz.
    int64_t dts_;
    int64_t pts_;
    SrsTsPESStreamId sid_;
    // 0 means the length is not given, so the message ends at the next payload_unit_start_indicator.
    uint16_t PES_packet_length_;
    SrsSimpleStream *payload_;

public:
    SrsTsMessage(SrsTsChannel *c = NULL, SrsTsPacket *p = NULL);
    virtual ~SrsTsMessage();
    // For decoder
public:
    virtual srs_error_t dump(SrsBuffer *stream, int *pnb_bytes);
    // When PES_packet_length is set, a new unit should never start before the message is complete.
    virtual bool completed(int8_t payload_unit_start_indicator);
    virtual bool fresh();

public:
    virtual bool is_audio();
    virtual bool is_video();
    virtual int stream_number();

public:
    // The copy takes the payload, such as for the caller to queue the message.
    virtual SrsTsMessage *detach();
};

class ISrsTsHandler
{
public:
    ISrsTsHandler();
    virtual ~ISrsTsHandler();

public:
    // The handler should never free msg.
    virtual srs_error_t on_ts_message(SrsTsMessage *msg) = 0;
};

class ISrsTsContext
{
public:
    ISrsTsContext();
    virtual ~ISrsTsContext();

public:
    virtual bool is_pure_audio() = 0;
    virtual void on_pmt_parsed() = 0;
    // For a new ts segment.
    virtual void reset() = 0;

public:
    virtual SrsTsChannel *get(int pid) = 0;
    virtual void set(int pid, SrsTsPidApply apply_pid, SrsTsStream stream = SrsTsStreamReserved) = 0;

public:
    // A video message holds AnnexB NALUs, for SrsRawH264Stream::annexb_demux, and an AAC audio
    // message holds ADTS frames, for SrsRawAacStream::adts_demux.
    virtual srs_error_t decode(SrsBuffer *stream, ISrsTsHandler *handler) = 0;

public:
    // A video message is a frame of one or more NALUs, and an audio message is an audio packet,
    // ADTS for AAC, as SrsTsMessageCache caches them.
    virtual srs_error_t encode(ISrsStreamWriter *writer, SrsTsMessage *msg, SrsVideoCodecId vc, SrsAudioCodecId ac) = 0;
};

class SrsTsContext : public ISrsTsContext
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // Set when PAT and PMT are written; PES packets fail before that.
    // @see https://github.com/ossrs/srs/issues/834
    bool ready_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    std::map<int, SrsTsChannel *> pids_;
    bool pure_audio_;
    int8_t sync_byte_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    SrsVideoCodecId vcodec_;
    SrsAudioCodecId acodec_;
    // ISO/IEC 13818-1 2.6.4 requires the version_number to change, modulo 32, whenever the
    // PMT changes. Strict demuxers, such as libVLC, otherwise miss streams announced later,
    // such as AAC audio after a video-only PMT.
    uint8_t pmt_version_;

public:
    SrsTsContext();
    virtual ~SrsTsContext();

public:
    // TODO: FIXME: merge with muxer codec detect.
    virtual bool is_pure_audio();
    virtual void on_pmt_parsed();
    virtual void reset();

public:
    virtual SrsTsChannel *get(int pid);
    virtual void set(int pid, SrsTsPidApply apply_pid, SrsTsStream stream = SrsTsStreamReserved);

public:
    virtual srs_error_t decode(SrsBuffer *stream, ISrsTsHandler *handler);

public:
    virtual srs_error_t encode(ISrsStreamWriter *writer, SrsTsMessage *msg, SrsVideoCodecId vc, SrsAudioCodecId ac);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t encode_pat_pmt(ISrsStreamWriter *writer, int16_t vpid, SrsTsStream vs, int16_t apid, SrsTsStream as);
    virtual srs_error_t encode_pes(ISrsStreamWriter *writer, SrsTsMessage *msg, int16_t pid, SrsTsStream sid, bool pure_audio);
};

// 2.4.3.2 Transport Stream packet layer, hls-mpeg-ts-iso13818-1.pdf, page 36
class SrsTsPacket
{
public:
    // 1B
    // Always 0x47.
    int8_t sync_byte_; // 8bits

    // 2B
    // At least one uncorrectable bit error in the packet. Entities outside the transport layer may set it.
    int8_t transport_error_indicator_; // 1bit
    // For PES data, 1 means exactly one PES packet starts in this packet, and 0 means none starts. For
    // PSI data, 1 means the payload starts with the pointer_field of a new section. 0 for null packets.
    int8_t payload_unit_start_indicator_; // 1bit
    // Higher priority than the packets of the same PID without the bit set.
    int8_t transport_priority_; // 1bit
    SrsTsPid pid_;              // 13bits

    // 1B
    // The header and the adaptation field are never scrambled.
    SrsTsScrambled transport_scrambling_control_; // 2bits
    // Decoders discard packets with '00'. Null packets use '01'.
    SrsTsAdaptationFieldType adaption_field_control_; // 2bits
    // Increments with each packet of the same PID and wraps to 0, but not for adaptation_field_control '00'
    // or '10'. A duplicate packet keeps the counter of the original. It may jump when the
    // discontinuity_indicator is set.
    uint8_t continuity_counter_; // 4bits
/* clang-format off */
SRS_DECLARE_PRIVATE:   /* clang-format on */
    SrsTsAdaptationField *adaptation_field_;
    SrsTsPayload *payload_;

public:
    ISrsTsContext *context_;

public:
    SrsTsPacket(ISrsTsContext *c);
    virtual ~SrsTsPacket();

public:
    virtual srs_error_t decode(SrsBuffer *stream, SrsTsMessage **ppmsg);

public:
    virtual int size();
    virtual srs_error_t encode(SrsBuffer *stream);
    virtual void padding(int nb_stuffings);

public:
    static SrsTsPacket *create_pat(ISrsTsContext *context, int16_t pmt_number, int16_t pmt_pid);
    static SrsTsPacket *create_pmt(ISrsTsContext *context, int16_t pmt_number, int16_t pmt_pid,
                                   int16_t vpid, SrsTsStream vs, int16_t apid, SrsTsStream as, uint8_t version_number = 0);
    static SrsTsPacket *create_pes_first(ISrsTsContext *context, int16_t pid, SrsTsPESStreamId sid,
                                         uint8_t continuity_counter, bool discontinuity, int64_t pcr, int64_t dts, int64_t pts, int size);
    static SrsTsPacket *create_pes_continue(ISrsTsContext *context,
                                            int16_t pid, SrsTsPESStreamId sid, uint8_t continuity_counter);
};

// 2.4.3.5 Semantic definition of fields in adaptation field, hls-mpeg-ts-iso13818-1.pdf, page 39
// Table 2-6 - Transport Stream adaptation field, hls-mpeg-ts-iso13818-1.pdf, page 40
class SrsTsAdaptationField
{
public:
    // 1B
    // The bytes after this field. 0 inserts a single stuffing byte. It is 0 to 182 for
    // adaptation_field_control '11', and 183 for '10'. A packet of PES data is stuffed only by a longer
    // adaptation field filled with stuffing bytes; PSI has another way, in 2.4.4.
    uint8_t adaption_field_length_; // 8bits
    // 1B
    // A system time-base discontinuity on a PCR_PID, where the next PCR starts a new system time clock,
    // or a continuity_counter discontinuity. After a continuity_counter discontinuity, the elementary
    // stream data restarts at an access point: a video sequence header or an audio frame.
    int8_t discontinuity_indicator_; // 1bit
    // The next PES packet of this PID starts with a video sequence header or an audio frame, to aid random
    // access, and has a PTS. On a PCR_PID, it is set only in packets with a PCR.
    int8_t random_access_indicator_; // 1bit
    // A higher priority than the payloads of other packets of the same PID. For video, set only when the
    // payload holds bytes of an intra-coded slice.
    int8_t elementary_stream_priority_indicator_; // 1bit
    int8_t PCR_flag_;                             // 1bit
    int8_t OPCR_flag_;                            // 1bit
    int8_t splicing_point_flag_;                  // 1bit
    int8_t transport_private_data_flag_;          // 1bit
    int8_t adaptation_field_extension_flag_;      // 1bit

    // If PCR_flag, 6B
    // The 42-bit PCR is the intended arrival time, at the system target decoder, of the byte holding the
    // last bit of the base. See equations 2-2 and 2-3.
    int64_t program_clock_reference_base_; // 33bits
    // 6bits reserved, must be '1'
    int8_t const1_value0_;                      // 6bits
    int16_t program_clock_reference_extension_; // 9bits

    // If OPCR_flag, 6B
    // Coded like the PCR, and only in packets with a PCR. It helps rebuild a single program Transport Stream
    // from another one, where the OPCR may be copied to the PCR.
    int64_t original_program_clock_reference_base_; // 33bits
    // 6bits reserved, must be '1'
    int8_t const1_value2_;                               // 6bits
    int16_t original_program_clock_reference_extension_; // 9bits

    // If splicing_point_flag, 1B
    // When positive, the packets of this PID left until the splicing point, which is right after the packet
    // where it reaches 0; that packet ends with the last byte of an audio frame or a picture. When -n, this
    // packet is the n-th packet after the splicing point. Duplicate packets and packets with only an
    // adaptation field are not counted.
    int8_t splice_countdown_; // 8bits

    // If transport_private_data_flag, 1+p[0] B
    std::vector<char> transport_private_data_; //[transport_private_data_length]bytes

    // If adaptation_field_extension_flag, 2+x B
    // The bytes after this field, including reserved bytes.
    uint8_t adaptation_field_extension_length_; // 8bits
    int8_t ltw_flag_;                           // 1bit
    int8_t piecewise_rate_flag_;                // 1bit
    // Set only with splicing_point_flag, and then kept until the splice_countdown reaches 0.
    int8_t seamless_splice_flag_; // 1bit
    // reserved 5bits, must be '1'
    int8_t const1_value1_; // 5bits
    // if ltw_flag, 2B
    int8_t ltw_valid_flag_; // 1bit
    // Defined only when ltw_valid_flag is set. In units of 300/fs seconds, where fs is the system clock
    // frequency of the program: the upper bound of the Legal Time Window of this packet minus the arrival
    // time of its first byte.
    int16_t ltw_offset_; // 15bits
    // if piecewise_rate_flag, 3B
    // 2bits reserved
    // Defined only when ltw_flag and ltw_valid_flag are set. A hypothetical bitrate for the Legal Time
    // Windows of the next packets of this PID that have no ltw_offset.
    int32_t piecewise_rate_; // 22bits
    // if seamless_splice_flag, 5B
    // The same value until the splice_countdown reaches 0. '0000' for audio; for video, Table 2-7 to
    // Table 2-16 give the splicing constraints.
    int8_t splice_type_; // 4bits
    // The decoding time of the first access unit after the splicing point, in the time base of the packet
    // where the splice_countdown reaches 0. The same value until then.
    int8_t DTS_next_AU0_;  // 3bits
    int8_t marker_bit0_;   // 1bit
    int16_t DTS_next_AU1_; // 15bits
    int8_t marker_bit1_;   // 1bit
    int16_t DTS_next_AU2_; // 15bits
    int8_t marker_bit2_;   // 1bit
    // The bytes left, each 0xFF, which an encoder may insert and a decoder discards.
    int nb_af_ext_reserved_;

    // The bytes left, each 0xFF, which an encoder may insert and a decoder discards.
    int nb_af_reserved_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    SrsTsPacket *packet_;

public:
    SrsTsAdaptationField(SrsTsPacket *pkt);
    virtual ~SrsTsAdaptationField();

public:
    virtual srs_error_t decode(SrsBuffer *stream);

public:
    virtual int size();
    virtual srs_error_t encode(SrsBuffer *stream);
};

// 2.4.4.4 Table_id assignments, hls-mpeg-ts-iso13818-1.pdf, page 62
// Table 2-26, the table_id of a PSI section.
enum SrsTsPsiId {
    // program_association_section
    SrsTsPsiIdPas = 0x00,
    // conditional_access_section (CA_section)
    SrsTsPsiIdCas = 0x01,
    // TS_program_map_section
    SrsTsPsiIdPms = 0x02,
    // TS_description_section
    SrsTsPsiIdDs = 0x03,
    // ISO_IEC_14496_scene_description_section
    SrsTsPsiIdSds = 0x04,
    // ISO_IEC_14496_object_descriptor_section
    SrsTsPsiIdOds = 0x05,
    SrsTsPsiIdIso138181Start = 0x06,
    SrsTsPsiIdIso138181End = 0x37,
    SrsTsPsiIdIso138186Start = 0x38,
    SrsTsPsiIdIso138186End = 0x3F,
    SrsTsPsiIdUserStart = 0x40,
    SrsTsPsiIdUserEnd = 0xFE,
    SrsTsPsiIdForbidden = 0xFF,
};

// A PES or PSI payload.
class SrsTsPayload
{
// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    SrsTsPacket *packet_;

public:
    SrsTsPayload(SrsTsPacket *p);
    virtual ~SrsTsPayload();

public:
    virtual srs_error_t decode(SrsBuffer *stream, SrsTsMessage **ppmsg) = 0;

public:
    virtual int size() = 0;
    virtual srs_error_t encode(SrsBuffer *stream) = 0;
};

// The PES packet of both TS and PS.
class SrsMpegPES
{
public:
    // 3B
    // Always 0x000001; with the stream_id it is the packet start code.
    int32_t packet_start_code_prefix_; // 24bits
    // 1B
    // In PS it gives the type and number of the elementary stream; in TS the PSI gives the type, and
    // this may be any valid value of Table 2-18.
    // @see SrsTsPESStreamId
    uint8_t stream_id_; // 8bits
    // 2B
    // The bytes after this field. 0 means neither specified nor bounded, allowed only for video in TS.
    uint16_t PES_packet_length_; // 16bits

    // 1B
    // 2bits const '10'
    int8_t const2bits_; // 2bits
    // The PES header, with its optional fields, is never scrambled.
    int8_t PES_scrambling_control_; // 2bits
    // A higher priority than payloads without it. The transport mechanism must not change it.
    int8_t PES_priority_; // 1bit
    // The header is followed right away by the video start code or audio syncword of the
    // data_stream_alignment_descriptor in 2.6.10, or, without the descriptor, by alignment_type '01' of
    // Table 2-47 and Table 2-48. 0 says nothing about alignment.
    int8_t data_alignment_indicator_; // 1bit
    // 0 does not say whether the material is protected by copyright.
    int8_t copyright_;        // 1bit
    int8_t original_or_copy_; // 1bit

    // 1B
    // '10' has a PTS, '11' a PTS and a DTS, '00' neither, and '01' is forbidden.
    int8_t PTS_DTS_flags_;             // 2bits
    int8_t ESCR_flag_;                 // 1bit
    int8_t ES_rate_flag_;              // 1bit
    int8_t DSM_trick_mode_flag_;       // 1bit
    int8_t additional_copy_info_flag_; // 1bit
    int8_t PES_CRC_flag_;              // 1bit
    int8_t PES_extension_flag_;        // 1bit

    // 1B
    // The bytes of the optional fields and the stuffing bytes in this header.
    uint8_t PES_header_data_length_; // 8bits

    // 5B
    // The presentation time in the system target decoder, in 90kHz, the system clock divided by 300. Refer
    // to 2.7.4 for how often it must be coded.
    // --------------1B
    // 4bits const
    // 3bits PTS [32..30]
    // 1bit const '1'
    // --------------2B
    // 15bits PTS [29..15]
    // 1bit const '1'
    // --------------2B
    // 15bits PTS [14..0]
    // 1bit const '1'
    int64_t pts_; // 33bits

    // 5B
    // The decoding time in the system target decoder, in 90kHz.
    // --------------1B
    // 4bits const
    // 3bits DTS [32..30]
    // 1bit const '1'
    // --------------2B
    // 15bits DTS [29..15]
    // 1bit const '1'
    // --------------2B
    // 15bits DTS [14..0]
    // 1bit const '1'
    int64_t dts_; // 33bits

    // 6B
    // The intended arrival time, at the PES-STD, of the byte holding the last bit of ESCR_base. See
    // equations 2-14 and 2-15, and 2.5.2.4.
    // 2bits reserved
    // 3bits ESCR_base[32..30]
    // 1bit const '1'
    // 15bits ESCR_base[29..15]
    // 1bit const '1'
    // 15bits ESCR_base[14..0]
    // 1bit const '1'
    // 9bits ESCR_extension
    // 1bit const '1'
    int64_t ESCR_base_;      // 33bits
    int16_t ESCR_extension_; // 9bits

    // 3B
    // The rate the system target decoder receives the bytes, in units of 50 bytes/second; 0 is forbidden.
    // It holds for the next PES packets of the stream until a new ES_rate.
    // 1bit const '1'
    // 22bits ES_rate
    // 1bit const '1'
    int32_t ES_rate_; // 22bits

    // 1B
    // The trick mode of a video stream, see 2.4.2.3; undefined for other streams.
    int8_t trick_mode_control_; // 3bits
    int8_t trick_mode_value_;   // 5bits

    // 1B
    // 1bit const '1'
    // Private data on copyright.
    int8_t additional_copy_info_; // 7bits

    // 2B
    // The CRC of the previous PES packet, as in Annex A.
    int16_t previous_PES_packet_CRC_; // 16bits

    // 1B
    int8_t PES_private_data_flag_; // 1bit
    // An ISO/IEC 11172-1 or PS pack header in this PES header; always 0 in a PS.
    int8_t pack_header_field_flag_;               // 1bit
    int8_t program_packet_sequence_counter_flag_; // 1bit
    int8_t P_STD_buffer_flag_;                    // 1bit
    // reserved value, must be '1'
    int8_t const1_value0_;        // 3bits
    int8_t PES_extension_flag_2_; // 1bit

    // 16B
    // With the fields around it, it must not emulate the packet_start_code_prefix (0x000001).
    std::vector<char> PES_private_data_; // 128bits

    // (1+x)B
    std::vector<char> pack_field_; //[pack_field_length] bytes

    // 2B
    // 1bit const '1'
    // Increments with each PES packet of a PS, an ISO/IEC 11172-1 stream, or a TS program, like the
    // continuity counter, and wraps to 0. It lets an application rebuild the original packet order.
    int8_t program_packet_sequence_counter_; // 7bits
    // 1bit const '1'
    // 1 for an ISO/IEC 11172-1 stream, 0 for a PS.
    int8_t MPEG1_MPEG2_identifier_; // 1bit
    // The stuffing bytes of the original PES or ISO/IEC 11172-1 packet header.
    int8_t original_stuff_length_; // 6bits

    // 2B
    // 2bits const '01'
    // Defined only in a PS. 0 for audio, 1 for video, either for others.
    int8_t P_STD_buffer_scale_; // 1bit
    // Defined only in a PS. The P-STD input buffer size, in 128 bytes when the scale is 0, or 1024 bytes
    // when it is 1.
    int16_t P_STD_buffer_size_; // 13bits

    // (1+x)B
    // 1bit const '1'
    std::vector<char> PES_extension_field_; //[PES_extension_field_length] bytes

    // NB
    // Stuffing bytes of 0xFF, which a decoder discards; at most 32 in a PES header.
    int nb_stuffings_;

    // NB
    // The contents of private_stream_1, private_stream_2, ECM_stream and EMM_stream are user defined.
    int nb_bytes_;

    // NB
    // Padding bytes of 0xFF, which a decoder discards.
    int nb_paddings_;

public:
    bool has_payload_;
    int nb_payload_;

public:
    SrsMpegPES();
    virtual ~SrsMpegPES();

public:
    virtual srs_error_t decode(SrsBuffer *stream);

public:
    virtual int size();
    virtual srs_error_t encode(SrsBuffer *stream);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t decode_33bits_dts_pts(SrsBuffer *stream, int64_t *pv);
    virtual srs_error_t encode_33bits_dts_pts(SrsBuffer *stream, uint8_t fb, int64_t v);
};

// 2.4.3.6 PES packet, hls-mpeg-ts-iso13818-1.pdf, page 49
class SrsTsPayloadPES : public SrsTsPayload
{
public:
    SrsMpegPES pes_;

public:
    SrsTsPayloadPES(SrsTsPacket *p);
    virtual ~SrsTsPayloadPES();

public:
    virtual srs_error_t decode(SrsBuffer *stream, SrsTsMessage **ppmsg);

public:
    virtual int size();
    virtual srs_error_t encode(SrsBuffer *stream);
};

// 2.4.4 Program specific information, hls-mpeg-ts-iso13818-1.pdf, page 59
class SrsTsPayloadPSI : public SrsTsPayload
{
public:
    // 1B
    // The bytes after this field before the first section, so 0 means the section starts right after it.
    // Present only when payload_unit_start_indicator is 1, which means a section starts in this packet.
    int8_t pointer_field;

public:
    // 1B
    // See Table 2-26.
    SrsTsPsiId table_id; // 8bits

    // 2B
    // Always '1'.
    int8_t section_syntax_indicator; // 1bit
    // const value, must be '0'
    int8_t const0_value; // 1bit
    // reserved value, must be '1'
    int8_t const1_value; // 2bits
    // The first 2 bits are '00'. The bytes after this field, with the CRC, at most 1021 (0x3FD).
    uint16_t section_length; // 12bits
public:
public:
    // 4B
    // The CRC over the section from table_id, without the pointer_field; Annex A.
    int32_t CRC_32; // 32bits
public:
    SrsTsPayloadPSI(SrsTsPacket *p);
    virtual ~SrsTsPayloadPSI();

public:
    virtual srs_error_t decode(SrsBuffer *stream, SrsTsMessage **ppmsg);

public:
    virtual int size();
    virtual srs_error_t encode(SrsBuffer *stream);

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual int psi_size() = 0;
    virtual srs_error_t psi_encode(SrsBuffer *stream) = 0;
    virtual srs_error_t psi_decode(SrsBuffer *stream) = 0;
};

class SrsTsPayloadPATProgram
{
public:
    // 4B
    // 0x0000 means the PID is the network_PID; other values are user defined, each at most once in a PAT
    // version.
    int16_t number; // 16bits
    // reserved value, must be '1'
    int8_t const1_value; // 3bits
    // The program_map_PID, or the network_PID of the Network Information Table.
    int16_t pid; // 13bits
public:
    SrsTsPayloadPATProgram(int16_t n = 0, int16_t p = 0);
    virtual ~SrsTsPayloadPATProgram();

public:
    virtual srs_error_t decode(SrsBuffer *stream);

public:
    virtual int size();
    virtual srs_error_t encode(SrsBuffer *stream);
};

// 2.4.4.3 Program association Table, hls-mpeg-ts-iso13818-1.pdf, page 61
// It maps each program_number to the PID of the packets with its program definition.
class SrsTsPayloadPAT : public SrsTsPayloadPSI
{
public:
    // 2B
    // A user defined label of this Transport Stream among the other multiplexes of a network.
    uint16_t transport_stream_id; // 16bits

    // 1B
    // reserved value, must be '1'
    int8_t const3_value; // 2bits
    // Increments by 1, modulo 32, whenever the PAT changes. It is the version of the current table when
    // current_next_indicator is 1, else of the next one.
    int8_t version_number; // 5bits
    // 0 means the table is not applicable yet, and is the next one to become valid.
    int8_t current_next_indicator; // 1bit

    // 1B
    // The first section is 0x00, and each next section adds 1.
    uint8_t section_number; // 8bits

    // 1B
    uint8_t last_section_number; // 8bits

    // 4B each.
    std::vector<SrsTsPayloadPATProgram *> programs;

public:
    SrsTsPayloadPAT(SrsTsPacket *p);
    virtual ~SrsTsPayloadPAT();

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual srs_error_t psi_decode(SrsBuffer *stream);

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual int psi_size();
    virtual srs_error_t psi_encode(SrsBuffer *stream);
};

class SrsTsPayloadPMTESInfo
{
public:
    // 1B
    SrsTsStream stream_type; // 8bits

    // 2B
    // reserved value, must be '1'
    int8_t const1_value0;   // 3bits
    int16_t elementary_PID; // 13bits

    // (2+x)B
    // reserved value, must be '1'
    int8_t const1_value1;      // 4bits
    std::vector<char> ES_info; //[ES_info_length] bytes.
public:
    SrsTsPayloadPMTESInfo(SrsTsStream st = SrsTsStreamReserved, int16_t epid = 0);
    virtual ~SrsTsPayloadPMTESInfo();

public:
    virtual srs_error_t decode(SrsBuffer *stream);

public:
    virtual int size();
    virtual srs_error_t encode(SrsBuffer *stream);
};

// 2.4.4.8 Program Map Table, hls-mpeg-ts-iso13818-1.pdf, page 64
// Each section holds one program definition, the program elements of a program_number, so its
// section_number is always 0. The encoder picks the PIDs that carry it.
class SrsTsPayloadPMT : public SrsTsPayloadPSI
{
public:
    // 2B
    // One program definition fits in one section, so it is at most 1016 (0x3F8) bytes; Annex C has ways
    // around that.
    uint16_t program_number; // 16bits

    // 1B
    // reserved value, must be '1'
    int8_t const1_value0; // 2bits
    // Increments by 1, modulo 32, whenever the section changes. It is the version of the current section
    // when current_next_indicator is 1, else of the next one.
    int8_t version_number; // 5bits
    // 0 means the section is not applicable yet, and is the next one to become valid.
    int8_t current_next_indicator; // 1bit

    // 1B
    // Always 0x00.
    uint8_t section_number; // 8bits

    // 1B
    // Always 0x00.
    uint8_t last_section_number; // 8bits

    // 2B
    // reserved value, must be '1'
    int8_t const1_value1; // 3bits
    // The PID of the packets with the PCR of this program, or 0x1FFF when a private stream program has no
    // PCR. See 2.4.3.5 and Table 2-3 for the values allowed.
    int16_t PCR_PID; // 13bits

    // 2B
    int8_t const1_value2; // 4bits
    // 12-bit program_info_length, the first 2 bits '00'.
    std::vector<char> program_info_desc; //[program_info_length]bytes

    std::vector<SrsTsPayloadPMTESInfo *> infos;

public:
    SrsTsPayloadPMT(SrsTsPacket *p);
    virtual ~SrsTsPayloadPMT();

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual srs_error_t psi_decode(SrsBuffer *stream);

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual int psi_size();
    virtual srs_error_t psi_encode(SrsBuffer *stream);
};

class ISrsTsContextWriter
{
public:
    ISrsTsContextWriter();
    virtual ~ISrsTsContextWriter();

public:
    virtual srs_error_t write_audio(SrsTsMessage *audio) = 0;
    virtual srs_error_t write_video(SrsTsMessage *video) = 0;

public:
    virtual SrsVideoCodecId vcodec() = 0;
    virtual void set_vcodec(SrsVideoCodecId v) = 0;

public:
    virtual SrsAudioCodecId acodec() = 0;
    virtual void set_acodec(SrsAudioCodecId v) = 0;
};

class SrsTsContextWriter : public ISrsTsContextWriter
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    SrsVideoCodecId vcodec_;
    SrsAudioCodecId acodec_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsTsContext *context_;
    ISrsStreamWriter *writer_;
    std::string path_;

public:
    SrsTsContextWriter(ISrsStreamWriter *w, ISrsTsContext *c, SrsAudioCodecId ac, SrsVideoCodecId vc);
    virtual ~SrsTsContextWriter();

public:
    virtual srs_error_t write_audio(SrsTsMessage *audio);
    virtual srs_error_t write_video(SrsTsMessage *video);

public:
    virtual SrsVideoCodecId vcodec();
    virtual void set_vcodec(SrsVideoCodecId v);

public:
    SrsAudioCodecId acodec();
    void set_acodec(SrsAudioCodecId v);
};

// For HLS encryption.
class SrsEncFileWriter : public SrsFileWriter
{
public:
    SrsEncFileWriter();
    virtual ~SrsEncFileWriter();

public:
    virtual srs_error_t write(void *data, size_t count, ssize_t *pnwrite);
    virtual void close();

public:
    srs_error_t config_cipher(unsigned char *key, unsigned char *iv);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    EVP_CIPHER_CTX *cipher_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    char *buf;
    int nb_buf;
};

// Groups frames into TS messages, such as many AAC frames into one message.
class ISrsTsMessageCache
{
public:
    ISrsTsMessageCache();
    virtual ~ISrsTsMessageCache();

public:
    virtual srs_error_t cache_audio(SrsParsedAudioPacket *frame, int64_t dts) = 0;
    virtual srs_error_t cache_video(SrsParsedVideoPacket *frame, int64_t dts) = 0;

public:
    virtual SrsTsMessage *audio() = 0;
    virtual void set_audio(SrsTsMessage *msg) = 0;
    virtual SrsTsMessage *video() = 0;
    virtual void set_video(SrsTsMessage *msg) = 0;
};

class SrsTsMessageCache : public ISrsTsMessageCache
{
public:
    SrsTsMessage *audio_;
    SrsTsMessage *video_;

public:
    SrsTsMessageCache();
    virtual ~SrsTsMessageCache();

public:
    virtual srs_error_t cache_audio(SrsParsedAudioPacket *frame, int64_t dts);
    virtual srs_error_t cache_video(SrsParsedVideoPacket *frame, int64_t dts);

public:
    virtual SrsTsMessage *audio();
    virtual void set_audio(SrsTsMessage *msg);
    virtual SrsTsMessage *video();
    virtual void set_video(SrsTsMessage *msg);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t do_cache_mp3(SrsParsedAudioPacket *frame);
    virtual srs_error_t do_cache_aac(SrsParsedAudioPacket *frame);
    virtual srs_error_t do_cache_avc(SrsParsedVideoPacket *frame);
    virtual srs_error_t do_cache_hevc(SrsParsedVideoPacket *frame);
};

class ISrsTsTransmuxer
{
public:
    ISrsTsTransmuxer();
    virtual ~ISrsTsTransmuxer();

public:
    virtual srs_error_t initialize(ISrsStreamWriter *fw) = 0;
    virtual srs_error_t write_audio(int64_t timestamp, char *data, int size) = 0;
    virtual srs_error_t write_video(int64_t timestamp, char *data, int size) = 0;

public:
    virtual void set_has_audio(bool v) = 0;
    virtual void set_has_video(bool v) = 0;
    virtual void set_guess_has_av(bool v) = 0;
};

// Transmux the RTMP stream to HTTP-TS stream.
class SrsTsTransmuxer : public ISrsTsTransmuxer
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsStreamWriter *writer_;
    bool has_audio_;
    bool has_video_;
    bool guess_has_av_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsFormat *format_;
    ISrsTsMessageCache *tsmc_;
    ISrsTsContextWriter *tscw_;
    ISrsTsContext *context_;

public:
    SrsTsTransmuxer();
    virtual ~SrsTsTransmuxer();

public:
    void set_has_audio(bool v);
    void set_has_video(bool v);
    void set_guess_has_av(bool v);

public:
    virtual srs_error_t initialize(ISrsStreamWriter *fw);

public:
    virtual srs_error_t write_audio(int64_t timestamp, char *data, int size);
    virtual srs_error_t write_video(int64_t timestamp, char *data, int size);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t flush_audio();
    virtual srs_error_t flush_video();
};

extern uint32_t srs_crc32_mpegts(const void *buf, int size);

#endif
