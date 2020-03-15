/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2020 Winlin
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

#ifndef SRS_GB28181_STACK_HPP
#define SRS_GB28181_STACK_HPP

#include <srs_core.hpp>

#include <string>
#include <sstream>

#include <srs_kernel_consts.hpp>

class SrsBuffer;
class SrsSimpleStream;
class SrsSimpleBufferX;
class SrsAudioFrame;
class ISrsProtocolReadWriter;


#define ERROR_RTP_PS_CORRUPT				12060
#define ERROR_RTP_PS_HK_PRIVATE_PROTO		12061
#define ERROR_RTP_PS_FIRST_TSB_LOSS			12062

#pragma pack (1)
struct Packet_Start_Code {
	u_int32_t start_code; //4bytes,need htonl exchange
	//u_int8_t start_code[3];
	//u_int8_t stream_id[1];
};

struct PS_Packet_Header {
	Packet_Start_Code psc; //4bytes
	u_int8_t holder[9];
	u_int8_t pack_stuffing_length; //low 3bit,high 5 bits are reserved;
};

struct PS_Sys_Header {
	Packet_Start_Code psc;
	u_int16_t header_length;
};

struct PS_Map {
	Packet_Start_Code psc;
	u_int16_t psm_length;  //2 bytes need htons exchange
};

struct PS_PES {
	Packet_Start_Code psc; //4bytes
	u_int16_t pes_packet_length;  //2 bytes need htonl exchange
	u_int8_t holder[2];
	u_int8_t pes_header_data_length;
};
#pragma pack ()

// TODO: (besson) may rewrite this class derived from Srs2SRtpPacket in future
// The 28181 stream rtp packet. Titled everything with "28181" is not a good idea 
// 5. RTP Data Transfer Protocol, @see rfc3550-2003-rtp.pdf, page 12
class Srs2SRtpPacket
{
public:
    // The version (V): 2 bits
    // This field identifies the version of RTP. The version defined by this specification is two (2).
    // (The value 1 is used by the first draft version of RTP and the value 0 is used by the protocol
    // initially implemented in the \vat" audio tool.)
    int8_t version; //2bits
    // The padding (P): 1 bit
    // If the padding bit is set, the packet contains one or more additional padding octets at the
    // end which are not part of the payload. The last octet of the padding contains a count of
    // how many padding octets should be ignored, including itself. Padding may be needed by
    // some encryption algorithms with fixed block sizes or for carrying several RTP packets in a
    // lower-layer protocol data unit.
    int8_t padding; //1bit
    // The extension (X): 1 bit
    // If the extension bit is set, the fixed header must be followed by exactly one header extension,
    // with a format defined in Section 5.3.1.
    int8_t extension; //1bit
    // The CSRC count (CC): 4 bits
    // The CSRC count contains the number of CSRC identifiers that follow the fixed header.
    int8_t csrc_count; //4bits
    // The marker (M): 1 bit
    // The interpretation of the marker is defined by a profile. It is intended to allow significant
    // events such as frame boundaries to be marked in the packet stream. A profile may define
    // additional marker bits or specify that there is no marker bit by changing the number of bits
    // in the payload type field (see Section 5.3).
    int8_t marker; //1bit
    // The payload type (PT): 7 bits
    // This field identifies the format of the RTP payload and determines its interpretation by the
    // application. A profile may specify a default static mapping of payload type codes to payload
    // formats. Additional payload type codes may be defined dynamically through non-RTP means
    // (see Section 3). A set of default mappings for audio and video is specified in the companion
    // RFC 3551 [1]. An RTP source may change the payload type during a session, but this field
    // should not be used for multiplexing separate media streams (see Section 5.2).
    // A receiver must ignore packets with payload types that it does not understand.
    int8_t payload_type; //7bits
    // The sequence number: 16 bits
    // The sequence number increments by one for each RTP data packet sent, and may be used
    // by the receiver to detect packet loss and to restore packet sequence. The initial value of the
    // sequence number should be random (unpredictable) to make known-plaintext attacks on
    // encryption more dicult, even if the source itself does not encrypt according to the method
    // in Section 9.1, because the packets may flow through a translator that does. Techniques for
    // choosing unpredictable numbers are discussed in [17].
    uint16_t sequence_number; //16bits
    // The timestamp: 32 bits
    // The timestamp reflects the sampling instant of the first octet in the RTP data packet. The
    // sampling instant must be derived from a clock that increments monotonically and linearly
    // in time to allow synchronization and jitter calculations (see Section 6.4.1). The resolution
    // of the clock must be sucient for the desired synchronization accuracy and for measuring
    // packet arrival jitter (one tick per video frame is typically not sucient). The clock frequency
    // is dependent on the format of data carried as payload and is specified statically in the profile
    // or payload format specification that defines the format, or may be specified dynamically for
    // payload formats defined through non-RTP means. If RTP packets are generated periodically,
    // The nominal sampling instant as determined from the sampling clock is to be used, not a
    // reading of the system clock. As an example, for fixed-rate audio the timestamp clock would
    // likely increment by one for each sampling period. If an audio application reads blocks covering
    // 160 sampling periods from the input device, the timestamp would be increased by 160 for
    // each such block, regardless of whether the block is transmitted in a packet or dropped as
    // silent.
    // 
    // The initial value of the timestamp should be random, as for the sequence number. Several
    // consecutive RTP packets will have equal timestamps if they are (logically) generated at once,
    // e.g., belong to the same video frame. Consecutive RTP packets may contain timestamps that
    // are not monotonic if the data is not transmitted in the order it was sampled, as in the case
    // of MPEG interpolated video frames. (The sequence numbers of the packets as transmitted
    // will still be monotonic.)
    // 
    // RTP timestamps from different media streams may advance at different rates and usually
    // have independent, random offsets. Therefore, although these timestamps are sucient to
    // reconstruct the timing of a single stream, directly comparing RTP timestamps from different
    // media is not effective for synchronization. Instead, for each medium the RTP timestamp
    // is related to the sampling instant by pairing it with a timestamp from a reference clock
    // (wallclock) that represents the time when the data corresponding to the RTP timestamp was
    // sampled. The reference clock is shared by all media to be synchronized. The timestamp
    // pairs are not transmitted in every data packet, but at a lower rate in RTCP SR packets as
    // described in Section 6.4.
    // 
    // The sampling instant is chosen as the point of reference for the RTP timestamp because it is
    // known to the transmitting endpoint and has a common definition for all media, independent
    // of encoding delays or other processing. The purpose is to allow synchronized presentation of
    // all media sampled at the same time.
    // 
    // Applications transmitting stored data rather than data sampled in real time typically use a
    // virtual presentation timeline derived from wallclock time to determine when the next frame
    // or other unit of each medium in the stored data should be presented. In this case, the RTP
    // timestamp would reflect the presentation time for each unit. That is, the RTP timestamp for
    // each unit would be related to the wallclock time at which the unit becomes current on the
    // virtual presentation timeline. Actual presentation occurs some time later as determined by
    // The receiver.
    // 
    // An example describing live audio narration of prerecorded video illustrates the significance
    // of choosing the sampling instant as the reference point. In this scenario, the video would
    // be presented locally for the narrator to view and would be simultaneously transmitted using
    // RTP. The sampling instant" of a video frame transmitted in RTP would be established by
    // referencing its timestamp to the wallclock time when that video frame was presented to the
    // narrator. The sampling instant for the audio RTP packets containing the narrator's speech
    // would be established by referencing the same wallclock time when the audio was sampled.
    // The audio and video may even be transmitted by different hosts if the reference clocks on
    // The two hosts are synchronized by some means such as NTP. A receiver can then synchronize
    // presentation of the audio and video packets by relating their RTP timestamps using the
    // timestamp pairs in RTCP SR packets.
    uint32_t timestamp; //32bits
    // The SSRC: 32 bits
    // The SSRC field identifies the synchronization source. This identifier should be chosen
    // randomly, with the intent that no two synchronization sources within the same RTP session
    // will have the same SSRC identifier. An example algorithm for generating a random identifier
    // is presented in Appendix A.6. Although the probability of multiple sources choosing the same
    // identifier is low, all RTP implementations must be prepared to detect and resolve collisions.
    // Section 8 describes the probability of collision along with a mechanism for resolving collisions
    // and detecting RTP-level forwarding loops based on the uniqueness of the SSRC identifier. If
    // a source changes its source transport address, it must also choose a new SSRC identifier to
    // avoid being interpreted as a looped source (see Section 8.2).
    uint32_t ssrc; //32bits
    
    // The payload.
    SrsSimpleBufferX* payload;

    // Beikesong: target tgt h264 stream or other types
	SrsSimpleBufferX* tgtstream;

    // Whether transport in chunked payload.
    bool chunked;
    // Whether message is completed.
    // normal message always completed.
    // while chunked completed when the last chunk arriaved.
    bool completed;

    // whether some private data in stream
	bool private_proto;
    
    // The audio samples, one rtp packets may contains multiple audio samples.
    SrsAudioFrame* audio;

public:
    Srs2SRtpPacket();
    virtual ~Srs2SRtpPacket();
public:
    // copy the header from src.
    virtual void copy(Srs2SRtpPacket* src);
    // 
    virtual void copy_v2(Srs2SRtpPacket* src);
    // reap the src to this packet, reap the payload.
    virtual void reap(Srs2SRtpPacket* src);
    // 
    virtual void reap_v2(Srs2SRtpPacket* src);
    // a dispatcher on different rtp packet dintecoder 
    virtual int decode(SrsBuffer* stream);
    // a diapatcher on different stream-core decoder
	virtual int decode_stream();

private:
    // decode rtp packet from stream
    virtual int decode_96ps_rtp(SrsBuffer* stream, int8_t marker);
	// decode ps packet form stream, only aim on high stabilable
	virtual int decode_96ps_core();
};

#endif