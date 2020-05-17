/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2020 LiPeng
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

#ifndef SRS_KERNEL_RTC_RTCP_HPP
#define SRS_KERNEL_RTC_RTCP_HPP

#include <srs_core.hpp>

#include <srs_kernel_buffer.hpp>
#include <srs_kernel_rtc_rtp.hpp>
#include <vector>
#include <set>
#include <map>

const int kRtcpPacketSize = 1500;
const uint8_t kRtcpVersion = 0x2;

// RTCP Packet Types, @see http://www.networksorcery.com/enp/protocol/rtcp.htm
enum SrsRtcpType {
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

// RTCP Header, @see http://tools.ietf.org/html/rfc3550#section-6.1
struct SrsRtcpHeader
{
	uint16_t rc:5;
	uint16_t padding:1;
	uint16_t version:2;
	uint16_t type:8;

	uint16_t length:16;
};

struct SrsSeqCompareLess {
    bool operator()(const uint16_t &lhs, const uint16_t &rhs) const {
        return srs_seq_is_newer(rhs, lhs);
    }
};

class SrsRtcpCommon: public ISrsCodec
{
protected:
    SrsRtcpHeader header_;
    uint8_t payload_[kRtcpPacketSize];
    int payload_len_;

protected:
    srs_error_t decode_header(SrsBuffer *buffer);
    srs_error_t encode_header(SrsBuffer *buffer);
public:
    SrsRtcpCommon();
    virtual ~SrsRtcpCommon();
    virtual const uint8_t type() const { return header_.type; }

public:
    // ISrsCodec
    virtual srs_error_t decode(SrsBuffer *buffer);
    virtual int nb_bytes();
    virtual srs_error_t encode(SrsBuffer *buffer);
};

class SrsRtcpApp : public SrsRtcpCommon 
{
    SrsRtcpHeader header_;
    uint32_t ssrc_;
    uint8_t name_[4];
    uint8_t payload_[kRtcpPacketSize];
    int payload_len_;
public:
    SrsRtcpApp();
    virtual ~SrsRtcpApp();

    virtual const uint8_t type() const { return SrsRtcpType_app; }
    
    const uint32_t get_ssrc() const;
    const uint8_t get_subtype() const;
    const std::string get_name() const;
    const srs_error_t get_payload(uint8_t*& payload, int& len);

    void set_ssrc(uint32_t ssrc);
    srs_error_t set_subtype(uint8_t type);
    srs_error_t set_name(std::string name);
    srs_error_t set_payload(uint8_t* payload, int len);
public:
    // ISrsCodec
    virtual srs_error_t decode(SrsBuffer *buffer);
    virtual int nb_bytes();
    virtual srs_error_t encode(SrsBuffer *buffer);
};

struct srs_rtcp_rb_t {
    uint32_t ssrc;
    uint8_t fraction_lost;
    uint32_t lost_packets;
    uint32_t highest_sn;
    uint32_t jitter;
    uint32_t lsr;
    uint32_t dlsr;
};

class SrsRtcpSR : public SrsRtcpCommon
{
private:
    uint32_t sender_ssrc_;
    uint64_t ntp_;
    uint32_t rtp_ts_;
    uint32_t send_rtp_packets_;
    uint32_t send_rtp_bytes_;
public:
    SrsRtcpSR();
    virtual ~SrsRtcpSR();

    const uint8_t get_rc() const { return header_.rc; }
    // overload SrsRtcpCommon
    virtual const uint8_t type() const { return SrsRtcpType_sr; }
    const uint32_t get_sender_ssrc() const;
    const uint64_t get_ntp() const;
    const uint32_t get_rtp_ts() const;
    const uint32_t get_rtp_send_packets() const;
    const uint32_t get_rtp_send_bytes() const;

    void set_sender_ssrc(uint32_t ssrc);
    void set_ntp(uint64_t ntp);
    void set_rtp_ts(uint32_t ts);
    void set_rtp_send_packets(uint32_t packets);
    void set_rtp_send_bytes(uint32_t bytes);

public:
    // ISrsCodec
    virtual srs_error_t decode(SrsBuffer *buffer);
    virtual int nb_bytes();
    virtual srs_error_t encode(SrsBuffer *buffer);
};

class SrsRtcpRR : public SrsRtcpCommon
{
private:
    uint32_t sender_ssrc_;
    srs_rtcp_rb_t rb_;
public:
    SrsRtcpRR(uint32_t sender_ssrc = 0);
    virtual ~SrsRtcpRR();

    // overload SrsRtcpCommon
    virtual const uint8_t type() const { return SrsRtcpType_rr; }

    const uint32_t get_rb_ssrc() const;
    const float get_lost_rate() const;
    const uint32_t get_lost_packets() const;
    const uint32_t get_highest_sn() const;
    const uint32_t get_jitter() const;
    const uint32_t get_lsr() const;
    const uint32_t get_dlsr() const;

    void set_rb_ssrc(uint32_t ssrc);
    void set_lost_rate(float rate);
    void set_lost_packets(uint32_t count);
    void set_highest_sn(uint32_t sn);
    void set_jitter(uint32_t jitter);
    void set_lsr(uint32_t lsr);
    void set_dlsr(uint32_t dlsr);
    void set_sender_ntp(uint64_t ntp);

public:
    // ISrsCodec
    virtual srs_error_t decode(SrsBuffer *buffer);
    virtual int nb_bytes();
    virtual srs_error_t encode(SrsBuffer *buffer);

};

/*
         0                   1                   2                   3
        0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       |V=2|P|  FMT=15 |    PT=205     |           length              |
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       |                     SSRC of packet sender                     |
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       |                      SSRC of media source                     |
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       |      base sequence number     |      packet status count      |
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       |                 reference time                | fb pkt. count |
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       |          packet chunk         |         packet chunk          |
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       .                                                               .
       .                                                               .
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       |         packet chunk          |  recv delta   |  recv delta   |
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       .                                                               .
*/
#define kTwccFbPktHeaderSize  				 (4 + 8 + 8)
#define kTwccFbChunkBytes						(2)
#define kTwccFbPktFormat 						  (15)
#define kTwccFbPayloadType					   (205)
#define kTwccFbMaxPktStatusCount	  (0xffff)
//#define kTwccFbMaxPktLength				    (4 + (1 << 16))
#define kTwccFbDeltaUnit 					       (250)	 // multiple of 250us
#define kTwccFbTimeMultiplier   		     (kTwccFbDeltaUnit * (1 << 8)) // multiplicand multiplier/* 250us -> 64ms  (1 << 8) */
#define kTwccFbReferenceTimeDivisor 	((1ll<<24) * kTwccFbTimeMultiplier) // dividend divisor

#define kTwccFbMaxRunLength 		0x1fff
#define kTwccFbOneBitElements 		14
#define kTwccFbTwoBitElements 		7
#define kTwccFbLargeRecvDeltaBytes	2
#define kTwccFbMaxBitElements 		kTwccFbOneBitElements

class SrsRtcpTWCC : public SrsRtcpCommon
{
private:
    uint32_t sender_ssrc_;
    uint32_t media_ssrc_;
    uint16_t base_sn_;
    uint16_t packet_count_;
    int32_t reference_time_;
    uint8_t fb_pkt_count_;
    std::vector<uint16_t> encoded_chucks_;
    std::vector<uint16_t> pkt_deltas_;

    std::map<uint16_t, srs_utime_t> recv_packes_;
    std::set<uint16_t, SrsSeqCompareLess> recv_sns_;

    typedef struct srs_rtcp_twcc_chunk {
        uint8_t delta_sizes[kTwccFbMaxBitElements];
        uint16_t size;
        bool all_same;
        bool has_large_delta;
    }srs_rtcp_twcc_chunk_t;

    int pkt_len;

private:
    void clear();
    srs_utime_t calculate_delta_us(srs_utime_t ts, srs_utime_t last);
    srs_error_t process_pkt_chunk(srs_rtcp_twcc_chunk_t& chunk, int delta_size);
    bool can_add_to_chunk(srs_rtcp_twcc_chunk_t& chunk, int delta_size);
    void add_to_chunk(srs_rtcp_twcc_chunk_t& chunk, int delta_size);
    srs_error_t encode_chunk(srs_rtcp_twcc_chunk_t& chunk);
    srs_error_t encode_chunk_run_length(srs_rtcp_twcc_chunk_t& chunk);
    srs_error_t encode_chunk_one_bit(srs_rtcp_twcc_chunk_t& chunk);
    srs_error_t encode_chunk_two_bit(srs_rtcp_twcc_chunk_t& chunk, size_t size, bool shift);
    void reset_chunk(srs_rtcp_twcc_chunk_t& chunk);
    srs_error_t encode_remaining_chunk(srs_rtcp_twcc_chunk_t& chunk);

public:
    SrsRtcpTWCC(uint32_t sender_ssrc = 0);
    virtual ~SrsRtcpTWCC();

    const uint32_t get_media_ssrc() const;
    const uint16_t get_base_sn() const;
    const uint16_t get_packet_status_count() const;
    const uint32_t get_reference_time() const;
    const uint8_t get_feedback_count() const;
    const std::vector<uint16_t> get_packet_chucks() const;
    const std::vector<uint16_t> get_recv_deltas() const;

    void set_media_ssrc(uint32_t ssrc);
    void set_base_sn(uint16_t sn);
    void set_packet_status_count(uint16_t count);
    void set_reference_time(uint32_t time);
    void set_feedback_count(uint8_t count);
    void add_packet_chuck(uint16_t chuck);
    void add_recv_delta(uint16_t delta);

    srs_error_t recv_packet(uint16_t sn, srs_utime_t ts);

public:
    // ISrsCodec
    virtual srs_error_t decode(SrsBuffer *buffer);
    virtual int nb_bytes();
    virtual srs_error_t encode(SrsBuffer *buffer);   

};

class SrsRtcpNack : public SrsRtcpCommon
{
private:
    typedef struct pid_blp_s {  
        uint16_t pid;
        uint16_t blp;
        bool in_use;
    }pid_blp_t;

    uint32_t sender_ssrc_;
    uint32_t media_ssrc_;
    std::set<uint16_t, SrsSeqCompareLess> lost_sns_;
public:
    SrsRtcpNack(uint32_t sender_ssrc = 0);
    virtual ~SrsRtcpNack();

    const uint32_t get_media_ssrc() const;
    const std::vector<uint16_t> get_lost_sns() const;

    void set_media_ssrc(uint32_t ssrc);
    void add_lost_sn(uint16_t sn);

public:
    // ISrsCodec
    virtual srs_error_t decode(SrsBuffer *buffer);
    virtual int nb_bytes();
    virtual srs_error_t encode(SrsBuffer *buffer);      
};


class SrsRtcpCompound : public ISrsCodec
{
private:
    std::vector<SrsRtcpCommon*> rtcps_;
    int nb_bytes_;
public:
    SrsRtcpCompound();
    virtual ~SrsRtcpCompound();

    SrsRtcpCommon* get_next_rtcp();
    srs_error_t add_rtcp(SrsRtcpCommon *rtcp);
    void clear();

public:
    // ISrsCodec
    virtual srs_error_t decode(SrsBuffer *buffer);
    virtual int nb_bytes();
    virtual srs_error_t encode(SrsBuffer *buffer);
};

#endif
