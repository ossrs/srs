/*
The MIT License (MIT)

Copyright (c) 2013-2015 winlin

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

#include <srs_kernel_codec.hpp>

class SrsStream;
class SrsTsCache;
class SrsTSMuxer;
class SrsFileWriter;
class SrsFileReader;
class SrsAvcAacCodec;
class SrsCodecSample;
class SrsSimpleBuffer;
class SrsTsAdaptationField;
class SrsTsPayload;

// Transport Stream packets are 188 bytes in length.
#define SRS_TS_PACKET_SIZE          188

// @see: ngx_rtmp_SrsMpegtsFrame_t
class SrsMpegtsFrame
{
public:
    int64_t         pts;
    int64_t         dts;
    int             pid;
    int             sid;
    int             cc;
    bool            key;
    
    SrsMpegtsFrame();
};

/**
* the pid of ts packet,
* Table 2-3 每 PID table, hls-mpeg-ts-iso13818-1.pdf, page 37
*/
enum SrsTsPid
{
    // Program Association Table(see Table 2-25).
    SrsTsPidPAT    = 0x00,
    // Conditional Access Table (see Table 2-27).
    SrsTsPidCAT    = 0x01,
    // Transport Stream Description Table
    SrsTsPidTSDT    = 0x02,
    // null packets (see Table 2-3)
    SrsTsPidNULL    = 0x01FFF,
};

/**
* the transport_scrambling_control of ts packet,
* Table 2-4 每 Scrambling control values, hls-mpeg-ts-iso13818-1.pdf, page 38
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
* Table 2-5 每 Adaptation field control values, hls-mpeg-ts-iso13818-1.pdf, page 38
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
* the context of ts, to decode the ts stream.
*/
class SrsTsContext
{
public:
    SrsTsContext();
    virtual ~SrsTsContext();
public:
    /**
    * the stream contains only one ts packet.
    * @remark we will consume all bytes in stream.
    */
    virtual int decode(SrsStream* stream);
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
    * following significance: a '1' indicates that the payload of this Transport Stream packet will commence with the first byte
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
    * Table (see Table 2-27). PID values 0x0002 每 0x000F are reserved. PID value 0x1FFF is reserved for null packets (see
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
    *incremented when the adaptation_field_control of the packet equals '00' or '10'.
    * 
    * In Transport Streams, duplicate packets may be sent as two, and only two, consecutive Transport Stream packets of the
    * same PID. The duplicate packets shall have the same continuity_counter value as the original packet and the
    * adaptation_field_control field shall be equal to '01' or '11'. In duplicate packets each byte of the original packet shall be
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
    SrsTsPacket();
    virtual ~SrsTsPacket();
public:
    virtual int decode(SrsStream* stream);
};

/**
* the adaption field of ts packet.
* 2.4.3.5 Semantic definition of fields in adaptation field, hls-mpeg-ts-iso13818-1.pdf, page 39
* Table 2-6 每 Transport Stream adaptation field, hls-mpeg-ts-iso13818-1.pdf, page 40
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
    *       Video 每 The first byte of a video sequence header.
    *       Audio 每 The first byte of an audio frame.
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
    //6bits reserved.
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
    //6bits reserved.
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
    *       Video 每 The first byte of a video_sequence_header.
    *       Audio 每 The first byte of an audio frame.
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
    //5bits reserved
    // if ltw_flag, 2B
    /**
    * (legal time window_valid_flag) 每 This is a 1-bit field which when set to '1' indicates that the value of the
    * ltw_offset shall be valid. A value of '0' indicates that the value in the ltw_offset field is undefined.
    */
    int8_t ltw_valid_flag; //1bit
    /**
    * (legal time window offset) 每 This is a 15-bit field, the value of which is defined only if the ltw_valid flag has
    * a value of '1'. When defined, the legal time window offset is in units of (300/fs) seconds, where fs is the system clock
    * frequency of the program that this PID belongs to, and fulfils:
    *       offset = t1(i) 每 t(i)
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
    * (decoding time stamp next access unit) 每 This is a 33-bit field, coded in three parts. In the case of
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
    virtual int decode(SrsStream* stream);
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
    // reserved 3bits
    /**
    * program_map_PID/network_PID 13bits
    * network_PID 每 The network_PID is a 13-bit field, which is used only in conjunction with the value of the
    * program_number set to 0x0000, specifies the PID of the Transport Stream packets which shall contain the Network
    * Information Table. The value of the network_PID field is defined by the user, but shall only take values as specified in
    * Table 2-3. The presence of the network_PID is optional.
    */
    int16_t pid;
public:
    SrsTsPayloadPATProgram();
    virtual ~SrsTsPayloadPATProgram();
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
    virtual int decode(SrsStream* stream) = 0;
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
    SrsTsPayloadPSI(SrsTsPacket* p);
    virtual ~SrsTsPayloadPSI();
public:
    virtual int decode(SrsStream* stream);
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
    // 2bits reserved. 
    /**
    * This is a 12-bit field, the first two bits of which shall be '00'. The remaining 10 bits specify the number
    * of bytes of the section, starting immediately following the section_length field, and including the CRC. The value in this
    * field shall not exceed 1021 (0x3FD).
    */
    u_int16_t section_length; //12bits
    
    // 2B
    /**
    * This is a 16-bit field which serves as a label to identify this Transport Stream from any other
    * multiplex within a network. Its value is defined by the user.
    */
    u_int16_t transport_stream_id; //16bits
    
    // 1B
    // 2bits reerverd.
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
    int nb_programs;
    SrsTsPayloadPATProgram* programs;
    
    // 4B
    int32_t CRC_32; //32bits
public:
    SrsTsPayloadPAT(SrsTsPacket* p);
    virtual ~SrsTsPayloadPAT();
public:
    virtual int decode(SrsStream* stream);
};

/**
* write data from frame(header info) and buffer(data) to ts file.
* it's a simple object wrapper for utility from nginx-rtmp: SrsMpegtsWriter
*/
class SrsTSMuxer
{
private:
    SrsCodecAudio previous;
    SrsCodecAudio current;
private:
    SrsFileWriter* writer;
    std::string path;
public:
    SrsTSMuxer(SrsFileWriter* w);
    virtual ~SrsTSMuxer();
public:
    /**
    * open the writer, donot write the PSI of ts.
    */
    virtual int open(std::string _path);
    /**
    * when open ts, we donot write the header(PSI),
    * for user may need to update the acodec to mp3 or others,
    * so we use delay write PSI, when write audio or video.
    * @remark for audio aac codec, for example, SRS1, it's ok to write PSI when open ts.
    * @see https://github.com/winlinvip/simple-rtmp-server/issues/301
    */
    virtual int update_acodec(SrsCodecAudio ac);
    /**
    * write an audio frame to ts, 
    * @remark write PSI first when not write yet.
    */
    virtual int write_audio(SrsMpegtsFrame* af, SrsSimpleBuffer* ab);
    /**
    * write a video frame to ts, 
    * @remark write PSI first when not write yet.
    */
    virtual int write_video(SrsMpegtsFrame* vf, SrsSimpleBuffer* vb);
    /**
    * close the writer.
    */
    virtual void close();
};

/**
* jitter correct for audio,
* the sample rate 44100/32000 will lost precise,
* when mp4/ts(tbn=90000) covert to flv/rtmp(1000),
* so the Hls on ipad or iphone will corrupt,
* @see nginx-rtmp: est_pts
*/
class SrsTsAacJitter
{
private:
    int64_t base_pts;
    int64_t nb_samples;
    int sync_ms;
public:
    SrsTsAacJitter();
    virtual ~SrsTsAacJitter();
    /**
    * when buffer start, calc the "correct" pts for ts,
    * @param flv_pts, the flv pts calc from flv header timestamp,
    * @param sample_rate, the sample rate in format(flv/RTMP packet header).
    * @param aac_sample_rate, the sample rate in codec(sequence header).
    * @return the calc correct pts.
    */
    virtual int64_t on_buffer_start(int64_t flv_pts, int sample_rate, int aac_sample_rate);
    /**
    * when buffer continue, muxer donot write to file,
    * the audio buffer continue grow and donot need a pts,
    * for the ts audio PES packet only has one pts at the first time.
    */
    virtual void on_buffer_continue();
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
    // current frame and buffer
    SrsMpegtsFrame* af;
    SrsSimpleBuffer* ab;
    SrsMpegtsFrame* vf;
    SrsSimpleBuffer* vb;
public:
    // the audio cache buffer start pts, to flush audio if full.
    // @remark the pts is not the adjust one, it's the orignal pts.
    int64_t audio_buffer_start_pts;
protected:
    // time jitter for aac
    SrsTsAacJitter* aac_jitter;
public:
    SrsTsCache();
    virtual ~SrsTsCache();
public:
    /**
    * write audio to cache
    */
    virtual int cache_audio(SrsAvcAacCodec* codec, int64_t pts, SrsCodecSample* sample);
    /**
    * write video to muxer.
    */
    virtual int cache_video(SrsAvcAacCodec* codec, int64_t dts, SrsCodecSample* sample);
private:
    virtual int do_cache_audio(SrsAvcAacCodec* codec, SrsCodecSample* sample);
    virtual int do_cache_video(SrsAvcAacCodec* codec, SrsCodecSample* sample);
};

/**
* encode data to ts file.
*/
class SrsTsEncoder
{
private:
    SrsFileWriter* _fs;
private:
    SrsAvcAacCodec* codec;
    SrsCodecSample* sample;
    SrsTsCache* cache;
    SrsTSMuxer* muxer;
public:
    SrsTsEncoder();
    virtual ~SrsTsEncoder();
public:
    /**
    * initialize the underlayer file stream.
    */
    virtual int initialize(SrsFileWriter* fs);
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

