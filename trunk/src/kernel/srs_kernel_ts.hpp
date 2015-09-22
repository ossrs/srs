/*
The MIT License (MIT)

Copyright (c) 2013-2015 SRS(simple-rtmp-server)

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

#ifndef SRS_KERNEL_TS_HPP
#define SRS_KERNEL_TS_HPP

/*
#include <srs_kernel_ts.hpp>
*/
#include <srs_core.hpp>

#include <string>
#include <map>
#include <vector>

#include <srs_kernel_codec.hpp>

class SrsBuffer;
class SrsTsCache;
class SrsTSMuxer;
class SrsFileWriter;
class SrsFileReader;
class SrsAvcAacCodec;
class SrsCodecSample;
class SrsSimpleBuffer;
class SrsTsAdaptationField;
class SrsTsPayload;
class SrsTsMessage;
class SrsTsPacket;
class SrsTsContext;

// Transport Stream packets are 188 bytes in length.
#define SRS_TS_PACKET_SIZE          188

/**
* the pid of ts packet,
* Table 2-3 - PID table, hls-mpeg-ts-iso13818-1.pdf, page 37
* NOTE - The transport packets with PID values 0x0000, 0x0001, and 0x0010-0x1FFE are allowed to carry a PCR.
*/
enum SrsTsPid
{
    // Program Association Table(see Table 2-25).
    SrsTsPidPAT             = 0x00,
    // Conditional Access Table (see Table 2-27).
    SrsTsPidCAT             = 0x01,
    // Transport Stream Description Table
    SrsTsPidTSDT            = 0x02,
    // Reserved
    SrsTsPidReservedStart   = 0x03,
    SrsTsPidReservedEnd     = 0x0f,
    // May be assigned as network_PID, Program_map_PID, elementary_PID, or for other purposes
    SrsTsPidAppStart        = 0x10,
    SrsTsPidAppEnd          = 0x1ffe,
    // null packets (see Table 2-3)
    SrsTsPidNULL    = 0x01FFF,
};

/**
* the transport_scrambling_control of ts packet,
* Table 2-4 - Scrambling control values, hls-mpeg-ts-iso13818-1.pdf, page 38
*/
enum SrsTsScrambled
{
    // Not scrambled
    SrsTsScrambledDisabled      = 0x00,
    // User-defined
    SrsTsScrambledUserDefined1  = 0x01,
    // User-defined
    SrsTsScrambledUserDefined2  = 0x02,
    // User-defined
    SrsTsScrambledUserDefined3  = 0x03,
};

/**
* the adaption_field_control of ts packet,
* Table 2-5 - Adaptation field control values, hls-mpeg-ts-iso13818-1.pdf, page 38
*/
enum SrsTsAdaptationFieldType
{
    // Reserved for future use by ISO/IEC
    SrsTsAdaptationFieldTypeReserved      = 0x00,
    // No adaptation_field, payload only
    SrsTsAdaptationFieldTypePayloadOnly   = 0x01,
    // Adaptation_field only, no payload
    SrsTsAdaptationFieldTypeAdaptionOnly  = 0x02,
    // Adaptation_field followed by payload
    SrsTsAdaptationFieldTypeBoth          = 0x03,
};

/**
* the actually parsed ts pid,
* @see SrsTsPid, some pid, for example, PMT/Video/Audio is specified by PAT or other tables.
*/
enum SrsTsPidApply
{
    SrsTsPidApplyReserved = 0, // TSPidTypeReserved, nothing parsed, used reserved.
    
    SrsTsPidApplyPAT, // Program associtate table
    SrsTsPidApplyPMT, // Program map table.
    
    SrsTsPidApplyVideo, // for video
    SrsTsPidApplyAudio, // vor audio
};

/**
* Table 2-29 - Stream type assignments
*/
enum SrsTsStream
{
    // ITU-T | ISO/IEC Reserved
    SrsTsStreamReserved         = 0x00,
    // ISO/IEC 11172 Video
    // ITU-T Rec. H.262 | ISO/IEC 13818-2 Video or ISO/IEC 11172-2 constrained parameter video stream
    // ISO/IEC 11172 Audio
    // ISO/IEC 13818-3 Audio
    SrsTsStreamAudioMp3         = 0x04,
    // ITU-T Rec. H.222.0 | ISO/IEC 13818-1 private_sections
    // ITU-T Rec. H.222.0 | ISO/IEC 13818-1 PES packets containing private data
    // ISO/IEC 13522 MHEG
    // ITU-T Rec. H.222.0 | ISO/IEC 13818-1 Annex A DSM-CC
    // ITU-T Rec. H.222.1
    // ISO/IEC 13818-6 type A
    // ISO/IEC 13818-6 type B
    // ISO/IEC 13818-6 type C
    // ISO/IEC 13818-6 type D
    // ITU-T Rec. H.222.0 | ISO/IEC 13818-1 auxiliary
    // ISO/IEC 13818-7 Audio with ADTS transport syntax
    SrsTsStreamAudioAAC        = 0x0f,
    // ISO/IEC 14496-2 Visual
    SrsTsStreamVideoMpeg4      = 0x10,
    // ISO/IEC 14496-3 Audio with the LATM transport syntax as defined in ISO/IEC 14496-3 / AMD 1
    SrsTsStreamAudioMpeg4      = 0x11,
    // ISO/IEC 14496-1 SL-packetized stream or FlexMux stream carried in PES packets
    // ISO/IEC 14496-1 SL-packetized stream or FlexMux stream carried in ISO/IEC14496_sections.
    // ISO/IEC 13818-6 Synchronized Download Protocol
    // ITU-T Rec. H.222.0 | ISO/IEC 13818-1 Reserved
    // 0x15-0x7F
    SrsTsStreamVideoH264       = 0x1b,
    // User Private
    // 0x80-0xFF
    SrsTsStreamAudioAC3        = 0x81,
    SrsTsStreamAudioDTS        = 0x8a,
};
std::string srs_ts_stream2string(SrsTsStream stream);

/**
* the ts channel.
*/
struct SrsTsChannel
{
    int pid;
    SrsTsPidApply apply;
    SrsTsStream stream;
    SrsTsMessage* msg;
    SrsTsContext* context;
    // for encoder.
    u_int8_t continuity_counter;

    SrsTsChannel();
    virtual ~SrsTsChannel();
};

/**
* the stream_id of PES payload of ts packet.
* Table 2-18 - Stream_id assignments, hls-mpeg-ts-iso13818-1.pdf, page 52.
*/
enum SrsTsPESStreamId
{
    // program_stream_map
    SrsTsPESStreamIdProgramStreamMap            = 0xbc, // 0b10111100
    // private_stream_1
    SrsTsPESStreamIdPrivateStream1              = 0xbd, // 0b10111101
    // padding_stream
    SrsTsPESStreamIdPaddingStream               = 0xbe, // 0b10111110
    // private_stream_2
    SrsTsPESStreamIdPrivateStream2              = 0xbf, // 0b10111111

    // 110x xxxx
    // ISO/IEC 13818-3 or ISO/IEC 11172-3 or ISO/IEC 13818-7 or ISO/IEC
    // 14496-3 audio stream number x xxxx
    // ((sid >> 5) & 0x07) == SrsTsPESStreamIdAudio
    // @remark, use SrsTsPESStreamIdAudioCommon as actually audio, SrsTsPESStreamIdAudio to check whether audio.
    SrsTsPESStreamIdAudioChecker                = 0x06, // 0b110
        SrsTsPESStreamIdAudioCommon             = 0xc0,

    // 1110 xxxx
    // ITU-T Rec. H.262 | ISO/IEC 13818-2 or ISO/IEC 11172-2 or ISO/IEC
    // 14496-2 video stream number xxxx
    // ((stream_id >> 4) & 0x0f) == SrsTsPESStreamIdVideo
    // @remark, use SrsTsPESStreamIdVideoCommon as actually video, SrsTsPESStreamIdVideo to check whether video.
    SrsTsPESStreamIdVideoChecker                = 0x0e, // 0b1110
        SrsTsPESStreamIdVideoCommon             = 0xe0,

    // ECM_stream
    SrsTsPESStreamIdEcmStream                   = 0xf0, // 0b11110000
    // EMM_stream
    SrsTsPESStreamIdEmmStream                   = 0xf1, // 0b11110001
    // DSMCC_stream
    SrsTsPESStreamIdDsmccStream                 = 0xf2, // 0b11110010
    // 13522_stream
    SrsTsPESStreamId13522Stream                 = 0xf3, // 0b11110011
    // H_222_1_type_A
    SrsTsPESStreamIdH2221TypeA                  = 0xf4, // 0b11110100
    // H_222_1_type_B
    SrsTsPESStreamIdH2221TypeB                  = 0xf5, // 0b11110101
    // H_222_1_type_C
    SrsTsPESStreamIdH2221TypeC                  = 0xf6, // 0b11110110
    // H_222_1_type_D
    SrsTsPESStreamIdH2221TypeD                  = 0xf7, // 0b11110111
    // H_222_1_type_E
    SrsTsPESStreamIdH2221TypeE                  = 0xf8, // 0b11111000
    // ancillary_stream
    SrsTsPESStreamIdAncillaryStream             = 0xf9, // 0b11111001
    // SL_packetized_stream
    SrsTsPESStreamIdSlPacketizedStream          = 0xfa, // 0b11111010
    // FlexMux_stream
    SrsTsPESStreamIdFlexMuxStream               = 0xfb, // 0b11111011
    // reserved data stream
    // 1111 1100 ... 1111 1110
    // program_stream_directory
    SrsTsPESStreamIdProgramStreamDirectory      = 0xff, // 0b11111111
};

/**
* the media audio/video message parsed from PES packet.
*/
class SrsTsMessage
{
public:
    // decoder only,
    // the ts messgae does not use them, 
    // for user to get the channel and packet.
    SrsTsChannel* channel;
    SrsTsPacket* packet;
public:
    // the audio cache buffer start pts, to flush audio if full.
    // @remark the pts is not the adjust one, it's the orignal pts.
    int64_t start_pts;
    // whether this message with pcr info,
    // generally, the video IDR(I frame, the keyframe of h.264) carray the pcr info.
    bool write_pcr;
    // whether got discontinuity ts, for example, sequence header changed.
    bool is_discontinuity;
public:
    // the timestamp in 90khz
    int64_t dts;
    int64_t pts;
    // the id of pes stream to indicates the payload codec.
    // @remark use is_audio() and is_video() to check it, and stream_number() to finger it out.
    SrsTsPESStreamId sid;
    // the size of payload, 0 indicates the length() of payload.
    u_int16_t PES_packet_length;
    // the chunk id.
    u_int8_t continuity_counter;
    // the payload bytes.
    SrsSimpleBuffer* payload;
public:
    SrsTsMessage(SrsTsChannel* c = NULL, SrsTsPacket* p = NULL);
    virtual ~SrsTsMessage();
// decoder
public:
    /**
    * dumps all bytes in stream to ts message.
    */
    virtual int dump(SrsBuffer* stream, int* pnb_bytes);
    /**
    * whether ts message is completed to reap.
    * @param payload_unit_start_indicator whether new ts message start.
    *       PES_packet_length is 0, the payload_unit_start_indicator=1 to reap ts message.
    *       PES_packet_length > 0, the payload.length() == PES_packet_length to reap ts message.
    * @remark when PES_packet_length>0, the payload_unit_start_indicator should never be 1 when not completed.
    * @remark when fresh, the payload_unit_start_indicator should be 1.
    */
    virtual bool completed(int8_t payload_unit_start_indicator);
    /**
    * whether the message is fresh.
    */
    virtual bool fresh();
public:
    /**
    * whether the sid indicates the elementary stream audio.
    */
    virtual bool is_audio();
    /**
    * whether the sid indicates the elementary stream video.
    */
    virtual bool is_video();
    /**
    * when audio or video, get the stream number which specifies the format of stream.
    * @return the stream number for audio/video; otherwise, -1.
    */
    virtual int stream_number();
public:
    /**
     * detach the ts message,
     * for user maybe need to parse the message by queue.
     * @remark we always use the payload of original message.
     */
    virtual SrsTsMessage* detach();
};

/**
* the ts message handler.
*/
class ISrsTsHandler
{
public:
    ISrsTsHandler();
    virtual ~ISrsTsHandler();
public:
    /**
    * when ts context got message, use handler to process it.
    * @param msg the ts msg, user should never free it.
    * @return an int error code.
    */
    virtual int on_ts_message(SrsTsMessage* msg) = 0;
};

/**
* the context of ts, to decode the ts stream.
*/
class SrsTsContext
{
// codec
private:
    std::map<int, SrsTsChannel*> pids;
    bool pure_audio;
    int8_t sync_byte;
// encoder
private:
    // when any codec changed, write the PAT/PMT.
    SrsCodecVideo vcodec;
    SrsCodecAudio acodec;
public:
    SrsTsContext();
    virtual ~SrsTsContext();
public:
    /**
     * whether the hls stream is pure audio stream.
     */
    virtual bool is_pure_audio();
    /**
     * when PMT table parsed, we know some info about stream.
     */
    virtual void on_pmt_parsed();
    /**
     * reset the context for a new ts segment start.
     */
    virtual void reset();
// codec
public:
    /**
    * get the pid apply, the parsed pid.
    * @return the apply channel; NULL for invalid.
    */
    virtual SrsTsChannel* get(int pid);
    /**
    * set the pid apply, the parsed pid.
    */
    virtual void set(int pid, SrsTsPidApply apply_pid, SrsTsStream stream = SrsTsStreamReserved);
// decode methods
public:
    /**
    * the stream contains only one ts packet.
    * @param handler the ts message handler to process the msg.
    * @remark we will consume all bytes in stream.
    */
    virtual int decode(SrsBuffer* stream, ISrsTsHandler* handler);
// encode methods
public:
    /**
    * write the PES packet, the video/audio stream.
    * @param msg the video/audio msg to write to ts.
    * @param vc the video codec, write the PAT/PMT table when changed.
    * @param ac the audio codec, write the PAT/PMT table when changed.
    */
    virtual int encode(SrsFileWriter* writer, SrsTsMessage* msg, SrsCodecVideo vc, SrsCodecAudio ac);
// drm methods
public:
    /**
     * set sync byte of ts segment.
     * replace the standard ts sync byte to bravo sync byte.
     */
    virtual void set_sync_byte(int8_t sb);
private:
    virtual int encode_pat_pmt(SrsFileWriter* writer, int16_t vpid, SrsTsStream vs, int16_t apid, SrsTsStream as);
    virtual int encode_pes(SrsFileWriter* writer, SrsTsMessage* msg, int16_t pid, SrsTsStream sid, bool pure_audio);
};

/**
* the packet in ts stream,
* 2.4.3.2 Transport Stream packet layer, hls-mpeg-ts-iso13818-1.pdf, page 36
* Transport Stream packets shall be 188 bytes long.
*/
class SrsTsPacket
{
public:
    // 1B
    /**
    * The sync_byte is a fixed 8-bit field whose value is '0100 0111' (0x47). Sync_byte emulation in the choice of
    * values for other regularly occurring fields, such as PID, should be avoided.
    */
    int8_t sync_byte; //8bits

    // 2B
    /**
    * The transport_error_indicator is a 1-bit flag. When set to '1' it indicates that at least
    * 1 uncorrectable bit error exists in the associated Transport Stream packet. This bit may be set to '1' by entities external to
    * the transport layer. When set to '1' this bit shall not be reset to '0' unless the bit value(s) in error have been corrected.
    */
    int8_t transport_error_indicator; //1bit
    /**
    * The payload_unit_start_indicator is a 1-bit flag which has normative meaning for
    * Transport Stream packets that carry PES packets (refer to 2.4.3.6) or PSI data (refer to 2.4.4).
    * 
    * When the payload of the Transport Stream packet contains PES packet data, the payload_unit_start_indicator has the
    * following significance: a '1' indicates that the payload of this Transport Stream packet will commence(start) with the first byte
    * of a PES packet and a '0' indicates no PES packet shall start in this Transport Stream packet. If the
    * payload_unit_start_indicator is set to '1', then one and only one PES packet starts in this Transport Stream packet. This
    * also applies to private streams of stream_type 6 (refer to Table 2-29).
    *
    * When the payload of the Transport Stream packet contains PSI data, the payload_unit_start_indicator has the following
    * significance: if the Transport Stream packet carries the first byte of a PSI section, the payload_unit_start_indicator value
    * shall be '1', indicating that the first byte of the payload of this Transport Stream packet carries the pointer_field. If the
    * Transport Stream packet does not carry the first byte of a PSI section, the payload_unit_start_indicator value shall be '0',
    * indicating that there is no pointer_field in the payload. Refer to 2.4.4.1 and 2.4.4.2. This also applies to private streams of
    * stream_type 5 (refer to Table 2-29).
    * 
    * For null packets the payload_unit_start_indicator shall be set to '0'.
    * 
    * The meaning of this bit for Transport Stream packets carrying only private data is not defined in this Specification.
    */
    int8_t payload_unit_start_indicator; //1bit
    /**
    * The transport_priority is a 1-bit indicator. When set to '1' it indicates that the associated packet is
    * of greater priority than other packets having the same PID which do not have the bit set to '1'. The transport mechanism
    * can use this to prioritize its data within an elementary stream. Depending on the application the transport_priority field
    * may be coded regardless of the PID or within one PID only. This field may be changed by channel specific encoders or
    * decoders.
    */
    int8_t transport_priority; //1bit
    /**
    * The PID is a 13-bit field, indicating the type of the data stored in the packet payload. PID value 0x0000 is
    * reserved for the Program Association Table (see Table 2-25). PID value 0x0001 is reserved for the Conditional Access
    * Table (see Table 2-27). PID values 0x0002 - 0x000F are reserved. PID value 0x1FFF is reserved for null packets (see
    * Table 2-3).
    */
    SrsTsPid pid; //13bits

    // 1B
    /**
    * This 2-bit field indicates the scrambling mode of the Transport Stream packet payload.
    * The Transport Stream packet header, and the adaptation field when present, shall not be scrambled. In the case of a null
    * packet the value of the transport_scrambling_control field shall be set to '00' (see Table 2-4).
    */
    SrsTsScrambled transport_scrambling_control; //2bits
    /**
    * This 2-bit field indicates whether this Transport Stream packet header is followed by an
    * adaptation field and/or payload (see Table 2-5).
    *
    * ITU-T Rec. H.222.0 | ISO/IEC 13818-1 decoders shall discard Transport Stream packets with the
    * adaptation_field_control field set to a value of '00'. In the case of a null packet the value of the adaptation_field_control
    * shall be set to '01'.
    */
    SrsTsAdaptationFieldType adaption_field_control; //2bits
    /**
    * The continuity_counter is a 4-bit field incrementing with each Transport Stream packet with the
    * same PID. The continuity_counter wraps around to 0 after its maximum value. The continuity_counter shall not be
    * incremented when the adaptation_field_control of the packet equals '00'(reseverd) or '10'(adaptation field only).
    * 
    * In Transport Streams, duplicate packets may be sent as two, and only two, consecutive Transport Stream packets of the
    * same PID. The duplicate packets shall have the same continuity_counter value as the original packet and the
    * adaptation_field_control field shall be equal to '01'(payload only) or '11'(both). In duplicate packets each byte of the original packet shall be
    * duplicated, with the exception that in the program clock reference fields, if present, a valid value shall be encoded.
    *
    * The continuity_counter in a particular Transport Stream packet is continuous when it differs by a positive value of one
    * from the continuity_counter value in the previous Transport Stream packet of the same PID, or when either of the nonincrementing
    * conditions (adaptation_field_control set to '00' or '10', or duplicate packets as described above) are met.
    * The continuity counter may be discontinuous when the discontinuity_indicator is set to '1' (refer to 2.4.3.4). In the case of
    * a null packet the value of the continuity_counter is undefined.
    */
    u_int8_t continuity_counter; //4bits
private:
    SrsTsAdaptationField* adaptation_field;
    SrsTsPayload* payload;
public:
    SrsTsContext* context;
public:
    SrsTsPacket(SrsTsContext* c);
    virtual ~SrsTsPacket();
public:
    virtual int decode(SrsBuffer* stream, SrsTsMessage** ppmsg);
public:
    virtual int size();
    virtual int encode(SrsBuffer* stream);
    virtual void padding(int nb_stuffings);
public:
    static SrsTsPacket* create_pat(SrsTsContext* context, 
        int16_t pmt_number, int16_t pmt_pid
    );
    static SrsTsPacket* create_pmt(SrsTsContext* context, 
        int16_t pmt_number, int16_t pmt_pid, int16_t vpid, SrsTsStream vs, 
        int16_t apid, SrsTsStream as
    );
    static SrsTsPacket* create_pes_first(SrsTsContext* context, 
        int16_t pid, SrsTsPESStreamId sid, u_int8_t continuity_counter, bool discontinuity, 
        int64_t pcr, int64_t dts, int64_t pts, int size
    );
    static SrsTsPacket* create_pes_continue(SrsTsContext* context, 
        int16_t pid, SrsTsPESStreamId sid, u_int8_t continuity_counter
    );
};

/**
* the adaption field of ts packet.
* 2.4.3.5 Semantic definition of fields in adaptation field, hls-mpeg-ts-iso13818-1.pdf, page 39
* Table 2-6 - Transport Stream adaptation field, hls-mpeg-ts-iso13818-1.pdf, page 40
*/
class SrsTsAdaptationField
{
public:
    // 1B
    /**
    * The adaptation_field_length is an 8-bit field specifying the number of bytes in the
    * adaptation_field immediately following the adaptation_field_length. The value 0 is for inserting a single stuffing byte in
    * a Transport Stream packet. When the adaptation_field_control value is '11', the value of the adaptation_field_length shall
    * be in the range 0 to 182. When the adaptation_field_control value is '10', the value of the adaptation_field_length shall
    * be 183. For Transport Stream packets carrying PES packets, stuffing is needed when there is insufficient PES packet data
    * to completely fill the Transport Stream packet payload bytes. Stuffing is accomplished by defining an adaptation field
    * longer than the sum of the lengths of the data elements in it, so that the payload bytes remaining after the adaptation field
    * exactly accommodates the available PES packet data. The extra space in the adaptation field is filled with stuffing bytes.
    *
    * This is the only method of stuffing allowed for Transport Stream packets carrying PES packets. For Transport Stream
    * packets carrying PSI, an alternative stuffing method is described in 2.4.4.
    */
    u_int8_t adaption_field_length; //8bits
    // 1B
    /**
    * This is a 1-bit field which when set to '1' indicates that the discontinuity state is true for the
    * current Transport Stream packet. When the discontinuity_indicator is set to '0' or is not present, the discontinuity state is
    * false. The discontinuity indicator is used to indicate two types of discontinuities, system time-base discontinuities and
    * continuity_counter discontinuities.
    * 
    * A system time-base discontinuity is indicated by the use of the discontinuity_indicator in Transport Stream packets of a
    * PID designated as a PCR_PID (refer to 2.4.4.9). When the discontinuity state is true for a Transport Stream packet of a
    * PID designated as a PCR_PID, the next PCR in a Transport Stream packet with that same PID represents a sample of a
    * new system time clock for the associated program. The system time-base discontinuity point is defined to be the instant
    * in time when the first byte of a packet containing a PCR of a new system time-base arrives at the input of the T-STD.
    * The discontinuity_indicator shall be set to '1' in the packet in which the system time-base discontinuity occurs. The
    * discontinuity_indicator bit may also be set to '1' in Transport Stream packets of the same PCR_PID prior to the packet
    * which contains the new system time-base PCR. In this case, once the discontinuity_indicator has been set to '1', it shall
    * continue to be set to '1' in all Transport Stream packets of the same PCR_PID up to and including the Transport Stream
    * packet which contains the first PCR of the new system time-base. After the occurrence of a system time-base
    * discontinuity, no fewer than two PCRs for the new system time-base shall be received before another system time-base
    * discontinuity can occur. Further, except when trick mode status is true, data from no more than two system time-bases
    * shall be present in the set of T-STD buffers for one program at any time.
    *
    * Prior to the occurrence of a system time-base discontinuity, the first byte of a Transport Stream packet which contains a
    * PTS or DTS which refers to the new system time-base shall not arrive at the input of the T-STD. After the occurrence of
    * a system time-base discontinuity, the first byte of a Transport Stream packet which contains a PTS or DTS which refers
    * to the previous system time-base shall not arrive at the input of the T-STD.
    *
    * A continuity_counter discontinuity is indicated by the use of the discontinuity_indicator in any Transport Stream packet.
    * When the discontinuity state is true in any Transport Stream packet of a PID not designated as a PCR_PID, the
    * continuity_counter in that packet may be discontinuous with respect to the previous Transport Stream packet of the same
    * PID. When the discontinuity state is true in a Transport Stream packet of a PID that is designated as a PCR_PID, the
    * continuity_counter may only be discontinuous in the packet in which a system time-base discontinuity occurs. A
    * continuity counter discontinuity point occurs when the discontinuity state is true in a Transport Stream packet and the
    * continuity_counter in the same packet is discontinuous with respect to the previous Transport Stream packet of the same
    * PID. A continuity counter discontinuity point shall occur at most one time from the initiation of the discontinuity state
    * until the conclusion of the discontinuity state. Furthermore, for all PIDs that are not designated as PCR_PIDs, when the
    * discontinuity_indicator is set to '1' in a packet of a specific PID, the discontinuity_indicator may be set to '1' in the next
    * Transport Stream packet of that same PID, but shall not be set to '1' in three consecutive Transport Stream packet of that
    * same PID.
    *
    * For the purpose of this clause, an elementary stream access point is defined as follows:
    *       Video - The first byte of a video sequence header.
    *       Audio - The first byte of an audio frame.
    *
    * After a continuity counter discontinuity in a Transport packet which is designated as containing elementary stream data,
    * the first byte of elementary stream data in a Transport Stream packet of the same PID shall be the first byte of an
    * elementary stream access point or in the case of video, the first byte of an elementary stream access point or a
    * sequence_end_code followed by an access point. Each Transport Stream packet which contains elementary stream data
    * with a PID not designated as a PCR_PID, and in which a continuity counter discontinuity point occurs, and in which a
    * PTS or DTS occurs, shall arrive at the input of the T-STD after the system time-base discontinuity for the associated
    * program occurs. In the case where the discontinuity state is true, if two consecutive Transport Stream packets of the same
    * PID occur which have the same continuity_counter value and have adaptation_field_control values set to '01' or '11', the
    * second packet may be discarded. A Transport Stream shall not be constructed in such a way that discarding such a packet
    * will cause the loss of PES packet payload data or PSI data.
    *
    * After the occurrence of a discontinuity_indicator set to '1' in a Transport Stream packet which contains PSI information,
    * a single discontinuity in the version_number of PSI sections may occur. At the occurrence of such a discontinuity, a
    * version of the TS_program_map_sections of the appropriate program shall be sent with section_length = = 13 and the
    * current_next_indicator = = 1, such that there are no program_descriptors and no elementary streams described. This shall
    * then be followed by a version of the TS_program_map_section for each affected program with the version_number
    * incremented by one and the current_next_indicator = = 1, containing a complete program definition. This indicates a
    * version change in PSI data.
    */
    int8_t discontinuity_indicator; //1bit
    /**
    * The random_access_indicator is a 1-bit field that indicates that the current Transport
    * Stream packet, and possibly subsequent Transport Stream packets with the same PID, contain some information to aid
    * random access at this point. Specifically, when the bit is set to '1', the next PES packet to start in the payload of Transport
    * Stream packets with the current PID shall contain the first byte of a video sequence header if the PES stream type (refer
    * to Table 2-29) is 1 or 2, or shall contain the first byte of an audio frame if the PES stream type is 3 or 4. In addition, in
    * the case of video, a presentation timestamp shall be present in the PES packet containing the first picture following the
    * sequence header. In the case of audio, the presentation timestamp shall be present in the PES packet containing the first
    * byte of the audio frame. In the PCR_PID the random_access_indicator may only be set to '1' in Transport Stream packet
    * containing the PCR fields.
    */
    int8_t random_access_indicator; //1bit
    /**
    * The elementary_stream_priority_indicator is a 1-bit field. It indicates, among
    * packets with the same PID, the priority of the elementary stream data carried within the payload of this Transport Stream
    * packet. A '1' indicates that the payload has a higher priority than the payloads of other Transport Stream packets. In the
    * case of video, this field may be set to '1' only if the payload contains one or more bytes from an intra-coded slice. A
    * value of '0' indicates that the payload has the same priority as all other packets which do not have this bit set to '1'.
    */
    int8_t elementary_stream_priority_indicator; //1bit
    /**
    * The PCR_flag is a 1-bit flag. A value of '1' indicates that the adaptation_field contains a PCR field coded in
    * two parts. A value of '0' indicates that the adaptation field does not contain any PCR field.
    */
    int8_t PCR_flag; //1bit
    /**
    * The OPCR_flag is a 1-bit flag. A value of '1' indicates that the adaptation_field contains an OPCR field
    * coded in two parts. A value of '0' indicates that the adaptation field does not contain any OPCR field.
    */
    int8_t OPCR_flag; //1bit
    /**
    * The splicing_point_flag is a 1-bit flag. When set to '1', it indicates that a splice_countdown field
    * shall be present in the associated adaptation field, specifying the occurrence of a splicing point. A value of '0' indicates
    * that a splice_countdown field is not present in the adaptation field.
    */
    int8_t splicing_point_flag; //1bit
    /**
    * The transport_private_data_flag is a 1-bit flag. A value of '1' indicates that the
    * adaptation field contains one or more private_data bytes. A value of '0' indicates the adaptation field does not contain any
    * private_data bytes.
    */
    int8_t transport_private_data_flag; //1bit
    /**
    * The adaptation_field_extension_flag is a 1-bit field which when set to '1' indicates
    * the presence of an adaptation field extension. A value of '0' indicates that an adaptation field extension is not present in
    * the adaptation field.
    */
    int8_t adaptation_field_extension_flag; //1bit
    
    // if PCR_flag, 6B
    /**
    * The program_clock_reference (PCR) is a
    * 42-bit field coded in two parts. The first part, program_clock_reference_base, is a 33-bit field whose value is given by
    * PCR_base(i), as given in equation 2-2. The second part, program_clock_reference_extension, is a 9-bit field whose value
    * is given by PCR_ext(i), as given in equation 2-3. The PCR indicates the intended time of arrival of the byte containing
    * the last bit of the program_clock_reference_base at the input of the system target decoder.
    */
    int64_t program_clock_reference_base; //33bits
    /**
    * 6bits reserved, must be '1'
    */
    int8_t const1_value0; // 6bits
    int16_t program_clock_reference_extension; //9bits
    
    // if OPCR_flag, 6B
    /**
    * The optional original
    * program reference (OPCR) is a 42-bit field coded in two parts. These two parts, the base and the extension, are coded
    * identically to the two corresponding parts of the PCR field. The presence of the OPCR is indicated by the OPCR_flag.
    * The OPCR field shall be coded only in Transport Stream packets in which the PCR field is present. OPCRs are permitted
    * in both single program and multiple program Transport Streams.
    *
    * OPCR assists in the reconstruction of a single program Transport Stream from another Transport Stream. When
    * reconstructing the original single program Transport Stream, the OPCR may be copied to the PCR field. The resulting
    * PCR value is valid only if the original single program Transport Stream is reconstructed exactly in its entirety. This
    * would include at least any PSI and private data packets which were present in the original Transport Stream and would
    * possibly require other private arrangements. It also means that the OPCR must be an identical copy of its associated PCR
    * in the original single program Transport Stream.
    */
    int64_t original_program_clock_reference_base; //33bits
    /**
    * 6bits reserved, must be '1'
    */
    int8_t const1_value2; // 6bits
    int16_t original_program_clock_reference_extension; //9bits
    
    // if splicing_point_flag, 1B
    /**
    * The splice_countdown is an 8-bit field, representing a value which may be positive or negative. A
    * positive value specifies the remaining number of Transport Stream packets, of the same PID, following the associated
    * Transport Stream packet until a splicing point is reached. Duplicate Transport Stream packets and Transport Stream
    * packets which only contain adaptation fields are excluded. The splicing point is located immediately after the last byte of
    * the Transport Stream packet in which the associated splice_countdown field reaches zero. In the Transport Stream packet
    * where the splice_countdown reaches zero, the last data byte of the Transport Stream packet payload shall be the last byte
    * of a coded audio frame or a coded picture. In the case of video, the corresponding access unit may or may not be
    * terminated by a sequence_end_code. Transport Stream packets with the same PID, which follow, may contain data from
    * a different elementary stream of the same type.
    *
    * The payload of the next Transport Stream packet of the same PID (duplicate packets and packets without payload being
    * excluded) shall commence with the first byte of a PES packet.In the case of audio, the PES packet payload shall
    * commence with an access point. In the case of video, the PES packet payload shall commence with an access point, or
    * with a sequence_end_code, followed by an access point. Thus, the previous coded audio frame or coded picture aligns
    * with the packet boundary, or is padded to make this so. Subsequent to the splicing point, the countdown field may also
    * be present. When the splice_countdown is a negative number whose value is minus n(-n), it indicates that the associated
    * Transport Stream packet is the n-th packet following the splicing point (duplicate packets and packets without payload
    * being excluded).
    * 
    * For the purposes of this subclause, an access point is defined as follows:
    *       Video - The first byte of a video_sequence_header.
    *       Audio - The first byte of an audio frame.
    */
    int8_t splice_countdown; //8bits
    
    // if transport_private_data_flag, 1+p[0] B
    /**
    * The transport_private_data_length is an 8-bit field specifying the number of
    * private_data bytes immediately following the transport private_data_length field. The number of private_data bytes shall
    * not be such that private data extends beyond the adaptation field.
    */
    u_int8_t transport_private_data_length; //8bits
    char* transport_private_data; //[transport_private_data_length]bytes
    
    // if adaptation_field_extension_flag, 2+x B
    /**
    * The adaptation_field_extension_length is an 8-bit field. It indicates the number of
    * bytes of the extended adaptation field data immediately following this field, including reserved bytes if present.
    */
    u_int8_t adaptation_field_extension_length; //8bits
    /**
    * This is a 1-bit field which when set to '1' indicates the presence of the ltw_offset
    * field.
    */
    int8_t ltw_flag; //1bit
    /**
    * This is a 1-bit field which when set to '1' indicates the presence of the piecewise_rate field.
    */
    int8_t piecewise_rate_flag; //1bit
    /**
    * This is a 1-bit flag which when set to '1' indicates that the splice_type and DTS_next_AU fields
    * are present. A value of '0' indicates that neither splice_type nor DTS_next_AU fields are present. This field shall not be
    * set to '1' in Transport Stream packets in which the splicing_point_flag is not set to '1'. Once it is set to '1' in a Transport
    * Stream packet in which the splice_countdown is positive, it shall be set to '1' in all the subsequent Transport Stream
    * packets of the same PID that have the splicing_point_flag set to '1', until the packet in which the splice_countdown
    * reaches zero (including this packet). When this flag is set, if the elementary stream carried in this PID is an audio stream,
    * the splice_type field shall be set to '0000'. If the elementary stream carried in this PID is a video stream, it shall fulfil the
    * constraints indicated by the splice_type value.
    */
    int8_t seamless_splice_flag; //1bit
    /**
    * reserved 5bits, must be '1'
    */
    int8_t const1_value1; //5bits
    // if ltw_flag, 2B
    /**
    * (legal time window_valid_flag) - This is a 1-bit field which when set to '1' indicates that the value of the
    * ltw_offset shall be valid. A value of '0' indicates that the value in the ltw_offset field is undefined.
    */
    int8_t ltw_valid_flag; //1bit
    /**
    * (legal time window offset) - This is a 15-bit field, the value of which is defined only if the ltw_valid flag has
    * a value of '1'. When defined, the legal time window offset is in units of (300/fs) seconds, where fs is the system clock
    * frequency of the program that this PID belongs to, and fulfils:
    *       offset = t1(i) - t(i)
    *       ltw_offset = offset//1
    * where i is the index of the first byte of this Transport Stream packet, offset is the value encoded in this field, t(i) is the
    * arrival time of byte i in the T-STD, and t1(i) is the upper bound in time of a time interval called the Legal Time Window
    * which is associated with this Transport Stream packet.
    */
    int16_t ltw_offset; //15bits
    // if piecewise_rate_flag, 3B
    //2bits reserved
    /**
    * The meaning of this 22-bit field is only defined when both the ltw_flag and the ltw_valid_flag are set
    * to '1'. When defined, it is a positive integer specifying a hypothetical bitrate R which is used to define the end times of
    * the Legal Time Windows of Transport Stream packets of the same PID that follow this packet but do not include the
    * legal_time_window_offset field.
    */
    int32_t piecewise_rate; //22bits
    // if seamless_splice_flag, 5B
    /**
    * This is a 4-bit field. From the first occurrence of this field onwards, it shall have the same value in all the
    * subsequent Transport Stream packets of the same PID in which it is present, until the packet in which the
    * splice_countdown reaches zero (including this packet). If the elementary stream carried in that PID is an audio stream,
    * this field shall have the value '0000'. If the elementary stream carried in that PID is a video stream, this field indicates the
    * conditions that shall be respected by this elementary stream for splicing purposes. These conditions are defined as a
    * function of profile, level and splice_type in Table 2-7 through Table 2-16.
    */
    int8_t splice_type; //4bits
    /**
    * (decoding time stamp next access unit) - This is a 33-bit field, coded in three parts. In the case of
    * continuous and periodic decoding through this splicing point it indicates the decoding time of the first access unit
    * following the splicing point. This decoding time is expressed in the time base which is valid in the Transport Stream
    * packet in which the splice_countdown reaches zero. From the first occurrence of this field onwards, it shall have the
    * same value in all the subsequent Transport Stream packets of the same PID in which it is present, until the packet in
    * which the splice_countdown reaches zero (including this packet).
    */
    int8_t DTS_next_AU0; //3bits
    int8_t marker_bit0; //1bit
    int16_t DTS_next_AU1; //15bits
    int8_t marker_bit1; //1bit
    int16_t DTS_next_AU2; //15bits
    int8_t marker_bit2; //1bit
    // left bytes.
    /**
    * This is a fixed 8-bit value equal to '1111 1111' that can be inserted by the encoder. It is discarded by the
    * decoder.
    */
    int nb_af_ext_reserved;
    
    // left bytes.
    /**
    * This is a fixed 8-bit value equal to '1111 1111' that can be inserted by the encoder. It is discarded by the
    * decoder.
    */
    int nb_af_reserved;
private:
    SrsTsPacket* packet;
public:
    SrsTsAdaptationField(SrsTsPacket* pkt);
    virtual ~SrsTsAdaptationField();
public:
    virtual int decode(SrsBuffer* stream);
public:
    virtual int size();
    virtual int encode(SrsBuffer* stream);
};

/**
* 2.4.4.4 Table_id assignments, hls-mpeg-ts-iso13818-1.pdf, page 62
* The table_id field identifies the contents of a Transport Stream PSI section as shown in Table 2-26.
*/
enum SrsTsPsiId
{
    // program_association_section
    SrsTsPsiIdPas               = 0x00,
    // conditional_access_section (CA_section)
    SrsTsPsiIdCas               = 0x01,
    // TS_program_map_section
    SrsTsPsiIdPms               = 0x02,
    // TS_description_section
    SrsTsPsiIdDs                = 0x03,
    // ISO_IEC_14496_scene_description_section
    SrsTsPsiIdSds               = 0x04,
    // ISO_IEC_14496_object_descriptor_section
    SrsTsPsiIdOds               = 0x05,
    // ITU-T Rec. H.222.0 | ISO/IEC 13818-1 reserved
    SrsTsPsiIdIso138181Start    = 0x06,
    SrsTsPsiIdIso138181End      = 0x37,
    // Defined in ISO/IEC 13818-6
    SrsTsPsiIdIso138186Start    = 0x38,
    SrsTsPsiIdIso138186End      = 0x3F,
    // User private
    SrsTsPsiIdUserStart         = 0x40,
    SrsTsPsiIdUserEnd           = 0xFE,
    // forbidden
    SrsTsPsiIdForbidden         = 0xFF,
};

/**
* the payload of ts packet, can be PES or PSI payload.
*/
class SrsTsPayload
{
protected:
    SrsTsPacket* packet;
public:
    SrsTsPayload(SrsTsPacket* p);
    virtual ~SrsTsPayload();
public:
    virtual int decode(SrsBuffer* stream, SrsTsMessage** ppmsg) = 0;
public:
    virtual int size() = 0;
    virtual int encode(SrsBuffer* stream) = 0;
};

/**
* the PES payload of ts packet.
* 2.4.3.6 PES packet, hls-mpeg-ts-iso13818-1.pdf, page 49
*/
class SrsTsPayloadPES : public SrsTsPayload
{
public:
    // 3B
    /**
    * The packet_start_code_prefix is a 24-bit code. Together with the stream_id that follows it
    * constitutes a packet start code that identifies the beginning of a packet. The packet_start_code_prefix is the bit string
    * '0000 0000 0000 0000 0000 0001' (0x000001).
    */
    int32_t packet_start_code_prefix; //24bits
    // 1B
    /**
    * In Program Streams, the stream_id specifies the type and number of the elementary stream as defined by the
    * stream_id Table 2-18. In Transport Streams, the stream_id may be set to any valid value which correctly describes the
    * elementary stream type as defined in Table 2-18. In Transport Streams, the elementary stream type is specified in the
    * Program Specific Information as specified in 2.4.4.
    */
    // @see SrsTsPESStreamId, value can be SrsTsPESStreamIdAudioCommon or SrsTsPESStreamIdVideoCommon.
    u_int8_t stream_id; //8bits
    // 2B
    /**
    * A 16-bit field specifying the number of bytes in the PES packet following the last byte of the
    * field. A value of 0 indicates that the PES packet length is neither specified nor bounded and is allowed only in
    * PES packets whose payload consists of bytes from a video elementary stream contained in Transport Stream packets.
    */
    u_int16_t PES_packet_length; //16bits

    // 1B
    /**
    * 2bits const '10'
    */
    int8_t const2bits; //2bits
    /**
    * The 2-bit PES_scrambling_control field indicates the scrambling mode of the PES packet
    * payload. When scrambling is performed at the PES level, the PES packet header, including the optional fields when
    * present, shall not be scrambled (see Table 2-19).
    */
    int8_t PES_scrambling_control; //2bits
    /**
    * This is a 1-bit field indicating the priority of the payload in this PES packet. A '1' indicates a higher
    * priority of the payload of the PES packet payload than a PES packet payload with this field set to '0'. A multiplexor can
    * use the PES_priority bit to prioritize its data within an elementary stream. This field shall not be changed by the transport
    * mechanism.
    */
    int8_t PES_priority; //1bit
    /**
    * This is a 1-bit flag. When set to a value of '1' it indicates that the PES packet header is
    * immediately followed by the video start code or audio syncword indicated in the data_stream_alignment_descriptor
    * in 2.6.10 if this descriptor is present. If set to a value of '1' and the descriptor is not present, alignment as indicated in
    * alignment_type '01' in Table 2-47 and Table 2-48 is required. When set to a value of '0' it is not defined whether any such
    * alignment occurs or not.
    */
    int8_t data_alignment_indicator; //1bit
    /**
    * This is a 1-bit field. When set to '1' it indicates that the material of the associated PES packet payload is
    * protected by copyright. When set to '0' it is not defined whether the material is protected by copyright. A copyright
    * descriptor described in 2.6.24 is associated with the elementary stream which contains this PES packet and the copyright
    * flag is set to '1' if the descriptor applies to the material contained in this PES packet
    */
    int8_t copyright; //1bit
    /**
    * This is a 1-bit field. When set to '1' the contents of the associated PES packet payload is an original.
    * When set to '0' it indicates that the contents of the associated PES packet payload is a copy.
    */
    int8_t original_or_copy; //1bit

    // 1B
    /**
    * This is a 2-bit field. When the PTS_DTS_flags field is set to '10', the PTS fields shall be present in
    * the PES packet header. When the PTS_DTS_flags field is set to '11', both the PTS fields and DTS fields shall be present
    * in the PES packet header. When the PTS_DTS_flags field is set to '00' no PTS or DTS fields shall be present in the PES
    * packet header. The value '01' is forbidden.
    */
    int8_t PTS_DTS_flags; //2bits
    /**
    * A 1-bit flag, which when set to '1' indicates that ESCR base and extension fields are present in the PES
    * packet header. When set to '0' it indicates that no ESCR fields are present.
    */
    int8_t ESCR_flag; //1bit
    /**
    * A 1-bit flag, which when set to '1' indicates that the ES_rate field is present in the PES packet header.
    * When set to '0' it indicates that no ES_rate field is present.
    */
    int8_t ES_rate_flag; //1bit
    /**
    * A 1-bit flag, which when set to '1' it indicates the presence of an 8-bit trick mode field. When
    * set to '0' it indicates that this field is not present.
    */
    int8_t DSM_trick_mode_flag; //1bit
    /**
    * A 1-bit flag, which when set to '1' indicates the presence of the additional_copy_info field.
    * When set to '0' it indicates that this field is not present.
    */
    int8_t additional_copy_info_flag; //1bit
    /**
    * A 1-bit flag, which when set to '1' indicates that a CRC field is present in the PES packet. When set to
    * '0' it indicates that this field is not present.
    */
    int8_t PES_CRC_flag; //1bit
    /**
    * A 1-bit flag, which when set to '1' indicates that an extension field exists in this PES packet
    * header. When set to '0' it indicates that this field is not present.
    */
    int8_t PES_extension_flag; //1bit

    // 1B
    /**
    * An 8-bit field specifying the total number of bytes occupied by the optional fields and any
    * stuffing bytes contained in this PES packet header. The presence of optional fields is indicated in the byte that precedes
    * the PES_header_data_length field.
    */
    u_int8_t PES_header_data_length; //8bits

    // 5B
    /**
    * Presentation times shall be related to decoding times as follows: The PTS is a 33-bit
    * number coded in three separate fields. It indicates the time of presentation, tp n (k), in the system target decoder of a
    * presentation unit k of elementary stream n. The value of PTS is specified in units of the period of the system clock
    * frequency divided by 300 (yielding 90 kHz). The presentation time is derived from the PTS according to equation 2-11
    * below. Refer to 2.7.4 for constraints on the frequency of coding presentation timestamps.
    */
    // ===========1B
    // 4bits const
    // 3bits PTS [32..30]
    // 1bit const '1'
    // ===========2B
    // 15bits PTS [29..15]
    // 1bit const '1'
    // ===========2B
    // 15bits PTS [14..0]
    // 1bit const '1'
    int64_t pts; // 33bits

    // 5B
    /**
    * The DTS is a 33-bit number coded in three separate fields. It indicates the decoding time,
    * td n (j), in the system target decoder of an access unit j of elementary stream n. The value of DTS is specified in units of
    * the period of the system clock frequency divided by 300 (yielding 90 kHz).
    */
    // ===========1B
    // 4bits const
    // 3bits DTS [32..30]
    // 1bit const '1'
    // ===========2B
    // 15bits DTS [29..15]
    // 1bit const '1'
    // ===========2B
    // 15bits DTS [14..0]
    // 1bit const '1'
    int64_t dts; // 33bits

    // 6B
    /**
    * The elementary stream clock reference is a 42-bit field coded in two parts. The first
    * part, ESCR_base, is a 33-bit field whose value is given by ESCR_base(i), as given in equation 2-14. The second part,
    * ESCR_ext, is a 9-bit field whose value is given by ESCR_ext(i), as given in equation 2-15. The ESCR field indicates the
    * intended time of arrival of the byte containing the last bit of the ESCR_base at the input of the PES-STD for PES streams
    * (refer to 2.5.2.4).
    */
    // 2bits reserved
    // 3bits ESCR_base[32..30]
    // 1bit const '1'
    // 15bits ESCR_base[29..15]
    // 1bit const '1'
    // 15bits ESCR_base[14..0]
    // 1bit const '1'
    // 9bits ESCR_extension
    // 1bit const '1'
    int64_t ESCR_base; //33bits
    int16_t ESCR_extension; //9bits

    // 3B
    /**
    * The ES_rate field is a 22-bit unsigned integer specifying the rate at which the
    * system target decoder receives bytes of the PES packet in the case of a PES stream. The ES_rate is valid in the PES
    * packet in which it is included and in subsequent PES packets of the same PES stream until a new ES_rate field is
    * encountered. The value of the ES_rate is measured in units of 50 bytes/second. The value 0 is forbidden. The value of the
    * ES_rate is used to define the time of arrival of bytes at the input of a P-STD for PES streams defined in 2.5.2.4. The
    * value encoded in the ES_rate field may vary from PES_packet to PES_packet.
    */
    // 1bit const '1'
    // 22bits ES_rate
    // 1bit const '1'
    int32_t ES_rate; //22bits

    // 1B
    /**
    * A 3-bit field that indicates which trick mode is applied to the associated video stream. In cases of
    * other types of elementary streams, the meanings of this field and those defined by the following five bits are undefined.
    * For the definition of trick_mode status, refer to the trick mode section of 2.4.2.3.
    */
    int8_t trick_mode_control; //3bits
    int8_t trick_mode_value; //5bits

    // 1B
    // 1bit const '1'
    /**
    * This 7-bit field contains private data relating to copyright information.
    */
    int8_t additional_copy_info; //7bits

    // 2B
    /**
    * The previous_PES_packet_CRC is a 16-bit field that contains the CRC value that yields
    * a zero output of the 16 registers in the decoder similar to the one defined in Annex A,
    */
    int16_t previous_PES_packet_CRC; //16bits

    // 1B
    /**
    * A 1-bit flag which when set to '1' indicates that the PES packet header contains private data.
    * When set to a value of '0' it indicates that private data is not present in the PES header.
    */
    int8_t PES_private_data_flag; //1bit
    /**
    * A 1-bit flag which when set to '1' indicates that an ISO/IEC 11172-1 pack header or a
    * Program Stream pack header is stored in this PES packet header. If this field is in a PES packet that is contained in a
    * Program Stream, then this field shall be set to '0'. In a Transport Stream, when set to the value '0' it indicates that no pack
    * header is present in the PES header.
    */
    int8_t pack_header_field_flag; //1bit
    /**
    * A 1-bit flag which when set to '1' indicates that the
    * program_packet_sequence_counter, MPEG1_MPEG2_identifier, and original_stuff_length fields are present in this
    * PES packet. When set to a value of '0' it indicates that these fields are not present in the PES header.
    */
    int8_t program_packet_sequence_counter_flag; //1bit
    /**
    * A 1-bit flag which when set to '1' indicates that the P-STD_buffer_scale and P-STD_buffer_size
    * are present in the PES packet header. When set to a value of '0' it indicates that these fields are not present in the
    * PES header.
    */
    int8_t P_STD_buffer_flag; //1bit
    /**
    * reverved value, must be '1'
    */
    int8_t const1_value0; //3bits
    /**
    * A 1-bit field which when set to '1' indicates the presence of the PES_extension_field_length
    * field and associated fields. When set to a value of '0' this indicates that the PES_extension_field_length field and any
    * associated fields are not present.
    */
    int8_t PES_extension_flag_2; //1bit

    // 16B
    /**
    * This is a 16-byte field which contains private data. This data, combined with the fields before and
    * after, shall not emulate the packet_start_code_prefix (0x000001).
    */
    char* PES_private_data; //128bits

    // (1+x)B
    /**
    * This is an 8-bit field which indicates the length, in bytes, of the pack_header_field().
    */
    u_int8_t pack_field_length; //8bits
    char* pack_field; //[pack_field_length] bytes

    // 2B
    // 1bit const '1'
    /**
    * The program_packet_sequence_counter field is a 7-bit field. It is an optional
    * counter that increments with each successive PES packet from a Program Stream or from an ISO/IEC 11172-1 Stream or
    * the PES packets associated with a single program definition in a Transport Stream, providing functionality similar to a
    * continuity counter (refer to 2.4.3.2). This allows an application to retrieve the original PES packet sequence of a Program
    * Stream or the original packet sequence of the original ISO/IEC 11172-1 stream. The counter will wrap around to 0 after
    * its maximum value. Repetition of PES packets shall not occur. Consequently, no two consecutive PES packets in the
    * program multiplex shall have identical program_packet_sequence_counter values.
    */
    int8_t program_packet_sequence_counter; //7bits
    // 1bit const '1'
    /**
    * A 1-bit flag which when set to '1' indicates that this PES packet carries information from
    * an ISO/IEC 11172-1 stream. When set to '0' it indicates that this PES packet carries information from a Program Stream.
    */
    int8_t MPEG1_MPEG2_identifier; //1bit
    /**
    * This 6-bit field specifies the number of stuffing bytes used in the original ITU-T
    * Rec. H.222.0 | ISO/IEC 13818-1 PES packet header or in the original ISO/IEC 11172-1 packet header.
    */
    int8_t original_stuff_length; //6bits

    // 2B
    // 2bits const '01'
    /**
    * The P-STD_buffer_scale is a 1-bit field, the meaning of which is only defined if this PES packet
    * is contained in a Program Stream. It indicates the scaling factor used to interpret the subsequent P-STD_buffer_size field.
    * If the preceding stream_id indicates an audio stream, P-STD_buffer_scale shall have the value '0'. If the preceding
    * stream_id indicates a video stream, P-STD_buffer_scale shall have the value '1'. For all other stream types, the value
    * may be either '1' or '0'.
    */
    int8_t P_STD_buffer_scale; //1bit
    /**
    * The P-STD_buffer_size is a 13-bit unsigned integer, the meaning of which is only defined if this
    * PES packet is contained in a Program Stream. It defines the size of the input buffer, BS n , in the P-STD. If
    * P-STD_buffer_scale has the value '0', then the P-STD_buffer_size measures the buffer size in units of 128 bytes. If
    * P-STD_buffer_scale has the value '1', then the P-STD_buffer_size measures the buffer size in units of 1024 bytes.
    */
    int16_t P_STD_buffer_size; //13bits

    // (1+x)B
    // 1bit const '1'
    /**
    * This is a 7-bit field which specifies the length, in bytes, of the data following this field in
    * the PES extension field up to and including any reserved bytes.
    */
    u_int8_t PES_extension_field_length; //7bits
    char* PES_extension_field; //[PES_extension_field_length] bytes

    // NB
    /**
    * This is a fixed 8-bit value equal to '1111 1111' that can be inserted by the encoder, for example to meet
    * the requirements of the channel. It is discarded by the decoder. No more than 32 stuffing bytes shall be present in one
    * PES packet header.
    */
    int nb_stuffings;

    // NB
    /**
    * PES_packet_data_bytes shall be contiguous bytes of data from the elementary stream
    * indicated by the packet's stream_id or PID. When the elementary stream data conforms to ITU-T
    * Rec. H.262 | ISO/IEC 13818-2 or ISO/IEC 13818-3, the PES_packet_data_bytes shall be byte aligned to the bytes of this
    * Recommendation | International Standard. The byte-order of the elementary stream shall be preserved. The number of
    * PES_packet_data_bytes, N, is specified by the PES_packet_length field. N shall be equal to the value indicated in the
    * PES_packet_length minus the number of bytes between the last byte of the PES_packet_length field and the first
    * PES_packet_data_byte.
    *
    * In the case of a private_stream_1, private_stream_2, ECM_stream, or EMM_stream, the contents of the
    * PES_packet_data_byte field are user definable and will not be specified by ITU-T | ISO/IEC in the future.
    */
    int nb_bytes;

    // NB
    /**
    * This is a fixed 8-bit value equal to '1111 1111'. It is discarded by the decoder.
    */
    int nb_paddings;
public:
    SrsTsPayloadPES(SrsTsPacket* p);
    virtual ~SrsTsPayloadPES();
public:
    virtual int decode(SrsBuffer* stream, SrsTsMessage** ppmsg);
public:
    virtual int size();
    virtual int encode(SrsBuffer* stream);
private:
    virtual int decode_33bits_dts_pts(SrsBuffer* stream, int64_t* pv);
    virtual int encode_33bits_dts_pts(SrsBuffer* stream, u_int8_t fb, int64_t v);
};

/**
* the PSI payload of ts packet.
* 2.4.4 Program specific information, hls-mpeg-ts-iso13818-1.pdf, page 59
*/
class SrsTsPayloadPSI : public SrsTsPayload
{
public:
    // 1B
    /**
    * This is an 8-bit field whose value shall be the number of bytes, immediately following the pointer_field
    * until the first byte of the first section that is present in the payload of the Transport Stream packet (so a value of 0x00 in
    * the pointer_field indicates that the section starts immediately after the pointer_field). When at least one section begins in
    * a given Transport Stream packet, then the payload_unit_start_indicator (refer to 2.4.3.2) shall be set to 1 and the first
    * byte of the payload of that Transport Stream packet shall contain the pointer. When no section begins in a given
    * Transport Stream packet, then the payload_unit_start_indicator shall be set to 0 and no pointer shall be sent in the
    * payload of that packet.
    */
    int8_t pointer_field;
public:
    // 1B
    /**
    * This is an 8-bit field, which shall be set to 0x00 as shown in Table 2-26.
    */
    SrsTsPsiId table_id; //8bits
    
    // 2B
    /**
    * The section_syntax_indicator is a 1-bit field which shall be set to '1'.
    */
    int8_t section_syntax_indicator; //1bit
    /**
    * const value, must be '0'
    */
    int8_t const0_value; //1bit
    /**
    * reverved value, must be '1'
    */
    int8_t const1_value; //2bits
    /**
    * This is a 12-bit field, the first two bits of which shall be '00'. The remaining 10 bits specify the number
    * of bytes of the section, starting immediately following the section_length field, and including the CRC. The value in this
    * field shall not exceed 1021 (0x3FD).
    */
    u_int16_t section_length; //12bits
public:
    // the specified psi info, for example, PAT fields.
public:
    // 4B
    /**
    * This is a 32-bit field that contains the CRC value that gives a zero output of the registers in the decoder
    * defined in Annex A after processing the entire section.
    * @remark crc32(bytes without pointer field, before crc32 field)
    */
    int32_t CRC_32; //32bits
public:
    SrsTsPayloadPSI(SrsTsPacket* p);
    virtual ~SrsTsPayloadPSI();
public:
    virtual int decode(SrsBuffer* stream, SrsTsMessage** ppmsg);
public:
    virtual int size();
    virtual int encode(SrsBuffer* stream);
protected:
    virtual int psi_size() = 0;
    virtual int psi_encode(SrsBuffer* stream) = 0;
    virtual int psi_decode(SrsBuffer* stream) = 0;
};

/**
* the program of PAT of PSI ts packet.
*/
class SrsTsPayloadPATProgram
{
public:
    // 4B
    /**
    * Program_number is a 16-bit field. It specifies the program to which the program_map_PID is
    * applicable. When set to 0x0000, then the following PID reference shall be the network PID. For all other cases the value
    * of this field is user defined. This field shall not take any single value more than once within one version of the Program
    * Association Table.
    */
    int16_t number; // 16bits
    /**
    * reverved value, must be '1'
    */
    int8_t const1_value; //3bits
    /**
    * program_map_PID/network_PID 13bits
    * network_PID - The network_PID is a 13-bit field, which is used only in conjunction with the value of the
    * program_number set to 0x0000, specifies the PID of the Transport Stream packets which shall contain the Network
    * Information Table. The value of the network_PID field is defined by the user, but shall only take values as specified in
    * Table 2-3. The presence of the network_PID is optional.
    */
    int16_t pid; //13bits
public:
    SrsTsPayloadPATProgram(int16_t n = 0, int16_t p = 0);
    virtual ~SrsTsPayloadPATProgram();
public:
    virtual int decode(SrsBuffer* stream);
public:
    virtual int size();
    virtual int encode(SrsBuffer* stream);
};

/**
* the PAT payload of PSI ts packet.
* 2.4.4.3 Program association Table, hls-mpeg-ts-iso13818-1.pdf, page 61
* The Program Association Table provides the correspondence between a program_number and the PID value of the
* Transport Stream packets which carry the program definition. The program_number is the numeric label associated with
* a program.
*/
class SrsTsPayloadPAT : public SrsTsPayloadPSI
{
public:
    // 2B
    /**
    * This is a 16-bit field which serves as a label to identify this Transport Stream from any other
    * multiplex within a network. Its value is defined by the user.
    */
    u_int16_t transport_stream_id; //16bits
    
    // 1B
    /**
    * reverved value, must be '1'
    */
    int8_t const3_value; //2bits
    /**
    * This 5-bit field is the version number of the whole Program Association Table. The version number
    * shall be incremented by 1 modulo 32 whenever the definition of the Program Association Table changes. When the
    * current_next_indicator is set to '1', then the version_number shall be that of the currently applicable Program Association
    * Table. When the current_next_indicator is set to '0', then the version_number shall be that of the next applicable Program
    * Association Table.
    */
    int8_t version_number; //5bits
    /**
    * A 1-bit indicator, which when set to '1' indicates that the Program Association Table sent is
    * currently applicable. When the bit is set to '0', it indicates that the table sent is not yet applicable and shall be the next
    * table to become valid.
    */
    int8_t current_next_indicator; //1bit
    
    // 1B
    /**
    * This 8-bit field gives the number of this section. The section_number of the first section in the
    * Program Association Table shall be 0x00. It shall be incremented by 1 with each additional section in the Program
    * Association Table.
    */
    u_int8_t section_number; //8bits
    
    // 1B
    /**
    * This 8-bit field specifies the number of the last section (that is, the section with the highest
    * section_number) of the complete Program Association Table.
    */
    u_int8_t last_section_number; //8bits
    
    // multiple 4B program data.
    std::vector<SrsTsPayloadPATProgram*> programs;
public:
    SrsTsPayloadPAT(SrsTsPacket* p);
    virtual ~SrsTsPayloadPAT();
protected:
    virtual int psi_decode(SrsBuffer* stream);
protected:
    virtual int psi_size();
    virtual int psi_encode(SrsBuffer* stream);
};

/**
* the esinfo for PMT program.
*/
class SrsTsPayloadPMTESInfo
{
public:
    // 1B
    /**
    * This is an 8-bit field specifying the type of program element carried within the packets with the PID
    * whose value is specified by the elementary_PID. The values of stream_type are specified in Table 2-29.
    */
    SrsTsStream stream_type; //8bits
    
    // 2B
    /**
    * reverved value, must be '1'
    */
    int8_t const1_value0; //3bits
    /**
    * This is a 13-bit field specifying the PID of the Transport Stream packets which carry the associated
    * program element.
    */
    int16_t elementary_PID; //13bits
    
    // (2+x)B
    /**
    * reverved value, must be '1'
    */
    int8_t const1_value1; //4bits
    /**
    * This is a 12-bit field, the first two bits of which shall be '00'. The remaining 10 bits specify the number
    * of bytes of the descriptors of the associated program element immediately following the ES_info_length field.
    */
    int16_t ES_info_length; //12bits
    char* ES_info; //[ES_info_length] bytes.
public:
    SrsTsPayloadPMTESInfo(SrsTsStream st = SrsTsStreamReserved, int16_t epid = 0);
    virtual ~SrsTsPayloadPMTESInfo();
public:
    virtual int decode(SrsBuffer* stream);
public:
    virtual int size();
    virtual int encode(SrsBuffer* stream);
};

/**
* the PMT payload of PSI ts packet.
* 2.4.4.8 Program Map Table, hls-mpeg-ts-iso13818-1.pdf, page 64
* The Program Map Table provides the mappings between program numbers and the program elements that comprise
* them. A single instance of such a mapping is referred to as a "program definition". The program map table is the
* complete collection of all program definitions for a Transport Stream. This table shall be transmitted in packets, the PID
* values of which are selected by the encoder. More than one PID value may be used, if desired. The table is contained in
* one or more sections with the following syntax. It may be segmented to occupy multiple sections. In each section, the
* section number field shall be set to zero. Sections are identified by the program_number field.
*/
class SrsTsPayloadPMT : public SrsTsPayloadPSI
{
public:
    // 2B
    /**
    * program_number is a 16-bit field. It specifies the program to which the program_map_PID is
    * applicable. One program definition shall be carried within only one TS_program_map_section. This implies that a
    * program definition is never longer than 1016 (0x3F8). See Informative Annex C for ways to deal with the cases when
    * that length is not sufficient. The program_number may be used as a designation for a broadcast channel, for example. By
    * describing the different program elements belonging to a program, data from different sources (e.g. sequential events)
    * can be concatenated together to form a continuous set of streams using a program_number. For examples of applications
    * refer to Annex C.
    */
    u_int16_t program_number; //16bits
    
    // 1B
    /**
    * reverved value, must be '1'
    */
    int8_t const1_value0; //2bits
    /**
    * This 5-bit field is the version number of the TS_program_map_section. The version number shall be
    * incremented by 1 modulo 32 when a change in the information carried within the section occurs. Version number refers
    * to the definition of a single program, and therefore to a single section. When the current_next_indicator is set to '1', then
    * the version_number shall be that of the currently applicable TS_program_map_section. When the current_next_indicator
    * is set to '0', then the version_number shall be that of the next applicable TS_program_map_section.
    */
    int8_t version_number; //5bits
    /**
    * A 1-bit field, which when set to '1' indicates that the TS_program_map_section sent is
    * currently applicable. When the bit is set to '0', it indicates that the TS_program_map_section sent is not yet applicable
    * and shall be the next TS_program_map_section to become valid.
    */
    int8_t current_next_indicator; //1bit
    
    // 1B
    /**
    * The value of this 8-bit field shall be 0x00.
    */
    u_int8_t section_number; //8bits
    
    // 1B
    /**
    * The value of this 8-bit field shall be 0x00.
    */
    u_int8_t last_section_number; //8bits
    
    // 2B
    /**
    * reverved value, must be '1'
    */
    int8_t const1_value1; //3bits
    /**
    * This is a 13-bit field indicating the PID of the Transport Stream packets which shall contain the PCR fields
    * valid for the program specified by program_number. If no PCR is associated with a program definition for private
    * streams, then this field shall take the value of 0x1FFF. Refer to the semantic definition of PCR in 2.4.3.5 and Table 2-3
    * for restrictions on the choice of PCR_PID value.
    */
    int16_t PCR_PID; //13bits
    
    // 2B
    int8_t const1_value2; //4bits
    /**
    * This is a 12-bit field, the first two bits of which shall be '00'. The remaining 10 bits specify the
    * number of bytes of the descriptors immediately following the program_info_length field.
    */
    u_int16_t program_info_length; //12bits
    char* program_info_desc; //[program_info_length]bytes
    
    // array of TSPMTESInfo.
    std::vector<SrsTsPayloadPMTESInfo*> infos;
public:
    SrsTsPayloadPMT(SrsTsPacket* p);
    virtual ~SrsTsPayloadPMT();
protected:
    virtual int psi_decode(SrsBuffer* stream);
protected:
    virtual int psi_size();
    virtual int psi_encode(SrsBuffer* stream);
};

/**
* write data from frame(header info) and buffer(data) to ts file.
* it's a simple object wrapper for utility from nginx-rtmp: SrsMpegtsWriter
*/
class SrsTSMuxer
{
private:
    SrsCodecVideo vcodec;
    SrsCodecAudio acodec;
private:
    SrsTsContext* context;
    SrsFileWriter* writer;
    std::string path;
public:
    SrsTSMuxer(SrsFileWriter* w, SrsTsContext* c, SrsCodecAudio ac, SrsCodecVideo vc);
    virtual ~SrsTSMuxer();
public:
    /**
     * open the writer, donot write the PSI of ts.
     * @param p a string indicates the path of ts file to mux to.
     */
    virtual int open(std::string p);
    /**
    * when open ts, we donot write the header(PSI),
    * for user may need to update the acodec to mp3 or others,
    * so we use delay write PSI, when write audio or video.
    * @remark for audio aac codec, for example, SRS1, it's ok to write PSI when open ts.
    * @see https://github.com/simple-rtmp-server/srs/issues/301
    */
    virtual int update_acodec(SrsCodecAudio ac);
    /**
    * write an audio frame to ts, 
    */
    virtual int write_audio(SrsTsMessage* audio);
    /**
    * write a video frame to ts, 
    */
    virtual int write_video(SrsTsMessage* video);
    /**
    * close the writer.
    */
    virtual void close();
};

/**
* ts stream cache, 
* use to cache ts stream.
* 
* about the flv tbn problem:
*   flv tbn is 1/1000, ts tbn is 1/90000,
*   when timestamp convert to flv tbn, it will loose precise,
*   so we must gather audio frame together, and recalc the timestamp @see SrsTsAacJitter,
*   we use a aac jitter to correct the audio pts.
*/
class SrsTsCache
{
public:
    // current ts message.
    SrsTsMessage* audio;
    SrsTsMessage* video;
public:
    SrsTsCache();
    virtual ~SrsTsCache();
public:
    /**
    * write audio to cache
    */
    virtual int cache_audio(SrsAvcAacCodec* codec, int64_t dts, SrsCodecSample* sample);
    /**
    * write video to muxer.
    */
    virtual int cache_video(SrsAvcAacCodec* codec, int64_t dts, SrsCodecSample* sample);
private:
    virtual int do_cache_mp3(SrsAvcAacCodec* codec, SrsCodecSample* sample);
    virtual int do_cache_aac(SrsAvcAacCodec* codec, SrsCodecSample* sample);
    virtual int do_cache_avc(SrsAvcAacCodec* codec, SrsCodecSample* sample);
};

/**
* encode data to ts file.
*/
class SrsTsEncoder
{
private:
    SrsFileWriter* writer;
private:
    SrsAvcAacCodec* codec;
    SrsCodecSample* sample;
    SrsTsCache* cache;
    SrsTSMuxer* muxer;
    SrsTsContext* context;
public:
    SrsTsEncoder();
    virtual ~SrsTsEncoder();
public:
    /**
     * initialize the underlayer file stream.
     * @param fw the writer to use for ts encoder, user must free it.
     */
    virtual int initialize(SrsFileWriter* fw);
public:
    /**
    * write audio/video packet.
    * @remark assert data is not NULL.
    */
    virtual int write_audio(int64_t timestamp, char* data, int size);
    virtual int write_video(int64_t timestamp, char* data, int size);
private:
    virtual int flush_audio();
    virtual int flush_video();
};

#endif

