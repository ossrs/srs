//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#ifndef SRS_KERNEL_RTC_RTCP_HPP
#define SRS_KERNEL_RTC_RTCP_HPP

#include <srs_core.hpp>

#include <vector>
#include <set>
#include <map>

#include <srs_kernel_buffer.hpp>
#include <srs_kernel_rtc_rtp.hpp>

const int kRtcpPacketSize = 1500;
const uint8_t kRtcpVersion = 0x2;

// 1500 - 20(ip_header) - 8(udp_header)
const int kMaxUDPDataSize = 1472;

// RTCP Packet Types, @see http://www.networksorcery.com/enp/protocol/rtcp.htm
enum SrsRtcpType
{
    SrsRtcpType_fir = 192,
    SrsRtcpType_sr = 200,
    SrsRtcpType_rr = 201,
    SrsRtcpType_sdes = 202,
    SrsRtcpType_bye = 203,
    SrsRtcpType_app = 204,
    SrsRtcpType_rtpfb = 205,
    SrsRtcpType_psfb = 206,
    SrsRtcpType_xr = 207,
};

// @see: https://tools.ietf.org/html/rfc4585#section-6.3
const uint8_t kPLI  = 1;
const uint8_t kSLI  = 2;
const uint8_t kRPSI = 3;
const uint8_t kAFB  = 15;

// RTCP Header, @see http://tools.ietf.org/html/rfc3550#section-6.1
// @remark The header must be 4 bytes, which align with the max field size 2B.
struct SrsRtcpHeader
{
	uint16_t rc:5;
	uint16_t padding:1;
	uint16_t version:2;
	uint16_t type:8;

	uint16_t length:16;

    SrsRtcpHeader() {
        rc = 0;
        padding = 0;
        version = 0;
        type = 0;
        length = 0;
    }
};

class SrsRtcpCommon: public ISrsCodec
{
protected:
    SrsRtcpHeader header_;
    uint32_t ssrc_;
    uint8_t payload_[kRtcpPacketSize];
    int payload_len_;

    char* data_;
    int nb_data_;
protected:
    srs_error_t decode_header(SrsBuffer *buffer);
    srs_error_t encode_header(SrsBuffer *buffer);
public:
    SrsRtcpCommon();
    virtual ~SrsRtcpCommon();
    virtual uint8_t type() const;
    virtual uint8_t get_rc() const;

    uint32_t get_ssrc();
    void set_ssrc(uint32_t ssrc);

    char* data();
    int size();
// interface ISrsCodec
public:
    virtual srs_error_t decode(SrsBuffer *buffer);
    virtual uint64_t nb_bytes();
    virtual srs_error_t encode(SrsBuffer *buffer);
};

class SrsRtcpApp : public SrsRtcpCommon 
{
private:
    uint8_t name_[4];
public:
    SrsRtcpApp();
    virtual ~SrsRtcpApp();

    static bool is_rtcp_app(uint8_t *data, int nb_data);

    virtual uint8_t type() const;
    
    uint8_t get_subtype() const;
    std::string get_name() const;
    srs_error_t get_payload(uint8_t*& payload, int& len);

    srs_error_t set_subtype(uint8_t type);
    srs_error_t set_name(std::string name);
    srs_error_t set_payload(uint8_t* payload, int len);
// interface ISrsCodec
public:
    virtual srs_error_t decode(SrsBuffer *buffer);
    virtual uint64_t nb_bytes();
    virtual srs_error_t encode(SrsBuffer *buffer);
};

struct SrsRtcpRB
{
    uint32_t ssrc;
    uint8_t fraction_lost;
    uint32_t lost_packets;
    uint32_t highest_sn;
    uint32_t jitter;
    uint32_t lsr;
    uint32_t dlsr;

    SrsRtcpRB() {
        ssrc = 0;
        fraction_lost = 0;
        lost_packets = 0;
        highest_sn = 0;
        jitter = 0;
        lsr = 0;
        dlsr = 0;
    }
};

class SrsRtcpSR : public SrsRtcpCommon
{
private:
    uint64_t ntp_;
    uint32_t rtp_ts_;
    uint32_t send_rtp_packets_;
    uint32_t send_rtp_bytes_;

public:
    SrsRtcpSR();
    virtual ~SrsRtcpSR();

    uint8_t get_rc() const;
    // overload SrsRtcpCommon
    virtual uint8_t type() const;
    uint64_t get_ntp() const;
    uint32_t get_rtp_ts() const;
    uint32_t get_rtp_send_packets() const;
    uint32_t get_rtp_send_bytes() const;

    void set_ntp(uint64_t ntp);
    void set_rtp_ts(uint32_t ts);
    void set_rtp_send_packets(uint32_t packets);
    void set_rtp_send_bytes(uint32_t bytes);
// interface ISrsCodec
public:
    virtual srs_error_t decode(SrsBuffer *buffer);
    virtual uint64_t nb_bytes();
    virtual srs_error_t encode(SrsBuffer *buffer);
};

class SrsRtcpRR : public SrsRtcpCommon
{
private:
    SrsRtcpRB rb_;
public:
    SrsRtcpRR(uint32_t sender_ssrc = 0);
    virtual ~SrsRtcpRR();

    // overload SrsRtcpCommon
    virtual uint8_t type() const;

    uint32_t get_rb_ssrc() const;
    float get_lost_rate() const;
    uint32_t get_lost_packets() const;
    uint32_t get_highest_sn() const;
    uint32_t get_jitter() const;
    uint32_t get_lsr() const;
    uint32_t get_dlsr() const;

    void set_rb_ssrc(uint32_t ssrc);
    void set_lost_rate(float rate);
    void set_lost_packets(uint32_t count);
    void set_highest_sn(uint32_t sn);
    void set_jitter(uint32_t jitter);
    void set_lsr(uint32_t lsr);
    void set_dlsr(uint32_t dlsr);
    void set_sender_ntp(uint64_t ntp);
// interface ISrsCodec
public:
    virtual srs_error_t decode(SrsBuffer *buffer);
    virtual uint64_t nb_bytes();
    virtual srs_error_t encode(SrsBuffer *buffer);

};

// @doc: https://tools.ietf.org/html/rfc4585#section-6.1
// As RFC 4585 says, all FB messages MUST use a common packet format,
// inlucde Transport layer FB message and Payload-specific FB message.
class SrsRtcpFbCommon : public SrsRtcpCommon
{
protected:
    uint32_t media_ssrc_;
public:
    SrsRtcpFbCommon();
    virtual ~SrsRtcpFbCommon();

    uint32_t get_media_ssrc() const;
    void set_media_ssrc(uint32_t ssrc);

// interface ISrsCodec
public:
    virtual srs_error_t decode(SrsBuffer *buffer);
    virtual uint64_t nb_bytes();
    virtual srs_error_t encode(SrsBuffer *buffer);   
};


// The Message format of TWCC, @see https://tools.ietf.org/html/draft-holmer-rmcat-transport-wide-cc-extensions-01#section-3.1
//       0                   1                   2                   3
//       0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
//      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//      |V=2|P|  FMT=15 |    PT=205     |           length              |
//      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//      |                     SSRC of packet sender                     |
//      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//      |                      SSRC of media source                     |
//      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//      |      base sequence number     |      packet status count      |
//      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//      |                 reference time                | fb pkt. count |
//      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//      |          packet chunk         |         packet chunk          |
//      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//      .                                                               .
//      .                                                               .
//      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//      |         packet chunk          |  recv delta   |  recv delta   |
//      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//      .                                                               .
//      .                                                               .
//      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//      |           recv delta          |  recv delta   | zero padding  |
//      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
#define kTwccFbPktHeaderSize (4 + 8 + 8)
#define kTwccFbChunkBytes (2)
#define kTwccFbPktFormat (15)
#define kTwccFbPayloadType (205)
#define kTwccFbMaxPktStatusCount (0xffff)
#define kTwccFbDeltaUnit (250)	 // multiple of 250us
#define kTwccFbTimeMultiplier (kTwccFbDeltaUnit * (1 << 8)) // multiplicand multiplier/* 250us -> 64ms  (1 << 8) */
#define kTwccFbReferenceTimeDivisor ((1ll<<24) * kTwccFbTimeMultiplier) // dividend divisor

#define kTwccFbMaxRunLength 		0x1fff
#define kTwccFbOneBitElements 		14
#define kTwccFbTwoBitElements 		7
#define kTwccFbLargeRecvDeltaBytes	2
#define kTwccFbMaxBitElements 		kTwccFbOneBitElements

class SrsRtcpTWCC : public SrsRtcpFbCommon
{
private:
    uint16_t base_sn_;
    int32_t reference_time_;
    uint8_t fb_pkt_count_;
    std::vector<uint16_t> encoded_chucks_;
    std::vector<uint16_t> pkt_deltas_;

    std::map<uint16_t, srs_utime_t> recv_packets_;
    std::set<uint16_t, SrsSeqCompareLess> recv_sns_;

    struct SrsRtcpTWCCChunk {
        uint8_t delta_sizes[kTwccFbMaxBitElements];
        uint16_t size;
        bool all_same;
        bool has_large_delta;
        SrsRtcpTWCCChunk();
    };

    int pkt_len;
    uint16_t next_base_sn_;
private:
    void clear();
    srs_utime_t calculate_delta_us(srs_utime_t ts, srs_utime_t last);
    srs_error_t process_pkt_chunk(SrsRtcpTWCCChunk& chunk, int delta_size);
    bool can_add_to_chunk(SrsRtcpTWCCChunk& chunk, int delta_size);
    void add_to_chunk(SrsRtcpTWCCChunk& chunk, int delta_size);
    srs_error_t encode_chunk(SrsRtcpTWCCChunk& chunk);
    srs_error_t encode_chunk_run_length(SrsRtcpTWCCChunk& chunk);
    srs_error_t encode_chunk_one_bit(SrsRtcpTWCCChunk& chunk);
    srs_error_t encode_chunk_two_bit(SrsRtcpTWCCChunk& chunk, size_t size, bool shift);
    void reset_chunk(SrsRtcpTWCCChunk& chunk);
    srs_error_t encode_remaining_chunk(SrsRtcpTWCCChunk& chunk);
public:
    SrsRtcpTWCC(uint32_t sender_ssrc = 0);
    virtual ~SrsRtcpTWCC();

    uint16_t get_base_sn() const;
    uint32_t get_reference_time() const;
    uint8_t get_feedback_count() const;
    std::vector<uint16_t> get_packet_chucks() const;
    std::vector<uint16_t> get_recv_deltas() const;

    void set_base_sn(uint16_t sn);
    void set_reference_time(uint32_t time);
    void set_feedback_count(uint8_t count);
    void add_packet_chuck(uint16_t chuck);
    void add_recv_delta(uint16_t delta);

    srs_error_t recv_packet(uint16_t sn, srs_utime_t ts);
    bool need_feedback();

// interface ISrsCodec
public:
    virtual srs_error_t decode(SrsBuffer *buffer);
    virtual uint64_t nb_bytes();
    virtual srs_error_t encode(SrsBuffer *buffer);   
private:
    srs_error_t do_encode(SrsBuffer *buffer);
};

class SrsRtcpNack : public SrsRtcpFbCommon
{
private:
    struct SrsPidBlp {
        uint16_t pid;
        uint16_t blp;
        bool in_use;
    };

    std::set<uint16_t, SrsSeqCompareLess> lost_sns_;
public:
    SrsRtcpNack(uint32_t sender_ssrc = 0);
    virtual ~SrsRtcpNack();

    std::vector<uint16_t> get_lost_sns() const;
    bool empty();

    void add_lost_sn(uint16_t sn);
// interface ISrsCodec
public:
    virtual srs_error_t decode(SrsBuffer *buffer);
    virtual uint64_t nb_bytes();
    virtual srs_error_t encode(SrsBuffer *buffer);      
};

class SrsRtcpPli : public SrsRtcpFbCommon
{
public:
    SrsRtcpPli(uint32_t sender_ssrc = 0);
    virtual ~SrsRtcpPli();

// interface ISrsCodec
public:
    virtual srs_error_t decode(SrsBuffer *buffer);
    virtual uint64_t nb_bytes();
    virtual srs_error_t encode(SrsBuffer *buffer);  
};

class SrsRtcpSli : public SrsRtcpFbCommon
{
private:
    uint16_t first_;
    uint16_t number_;
    uint8_t picture_;
public:
    SrsRtcpSli(uint32_t sender_ssrc = 0);
    virtual ~SrsRtcpSli();

 // interface ISrsCodec
public:
    virtual srs_error_t decode(SrsBuffer *buffer);
    virtual uint64_t nb_bytes();
    virtual srs_error_t encode(SrsBuffer *buffer);   
}; 

class SrsRtcpRpsi : public SrsRtcpFbCommon
{
private:
    uint8_t pb_;
    uint8_t payload_type_;
    char* native_rpsi_;
    int nb_native_rpsi_;

public:
    SrsRtcpRpsi(uint32_t sender_ssrc = 0);
    virtual ~SrsRtcpRpsi();

 // interface ISrsCodec
public:
    virtual srs_error_t decode(SrsBuffer *buffer);
    virtual uint64_t nb_bytes();
    virtual srs_error_t encode(SrsBuffer *buffer);   
};

class SrsRtcpXr : public SrsRtcpFbCommon
{
public:
    SrsRtcpXr (uint32_t ssrc = 0);
    virtual ~SrsRtcpXr();

   // interface ISrsCodec
public:
    virtual srs_error_t decode(SrsBuffer *buffer);
    virtual uint64_t nb_bytes();
    virtual srs_error_t encode(SrsBuffer *buffer);   
};

class SrsRtcpCompound : public ISrsCodec
{
private:
    std::vector<SrsRtcpCommon*> rtcps_;
    int nb_bytes_;
    char* data_;
    int nb_data_;
public:
    SrsRtcpCompound();
    virtual ~SrsRtcpCompound();

    // TODO: FIXME: Should rename it to pop(), because it's not a GET method.
    SrsRtcpCommon* get_next_rtcp();
    srs_error_t add_rtcp(SrsRtcpCommon *rtcp);
    void clear();

    char* data();
    int size();

// interface ISrsCodec
public:
    virtual srs_error_t decode(SrsBuffer *buffer);
    virtual uint64_t nb_bytes();
    virtual srs_error_t encode(SrsBuffer *buffer);
};

#endif

