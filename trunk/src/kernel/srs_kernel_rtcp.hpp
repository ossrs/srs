
#ifndef SRS_KERNEL_RTCP_HPP
#define SRS_KERNEL_RTCP_HPP

#include <srs_kernel_buffer.hpp>
#include <srs_kernel_rtp.hpp>
#include <vector>
#include <set>

const int kRtcpPacketSize        = 1500;
const uint8_t kRtcpVersion = 0x2;

/*! \brief RTCP Packet Types (http://www.networksorcery.com/enp/protocol/rtcp.htm) */
typedef enum {
    RTCP_FIR = 192,
    RTCP_SR = 200,
    RTCP_RR = 201,
    RTCP_SDES = 202,
    RTCP_BYE = 203,
    RTCP_APP = 204,
    RTCP_RTPFB = 205,
    RTCP_PSFB = 206,
    RTCP_XR = 207,
} srs_rtcp_type_t;


/*! \brief RTCP Header (http://tools.ietf.org/html/rfc3550#section-6.1) */
typedef struct srs_rtcp_header_s
{
	uint16_t rc:5;
	uint16_t padding:1;
	uint16_t version:2;
	uint16_t type:8;

	uint16_t length:16;
} srs_rtcp_header_t;

class SrsRTCPCommon: public ISrsCodec
{
protected:
    srs_rtcp_header_t header_;
    uint8_t payload_[kRtcpPacketSize];
    int payload_len_;

protected:
    srs_error_t decode_header(SrsBuffer *buffer);
    srs_error_t encode_header(SrsBuffer *buffer);
public:
    SrsRTCPCommon();
    virtual ~SrsRTCPCommon();
    virtual const uint8_t type() const { return header_.type; }

public:
    // ISrsCodec
    virtual srs_error_t decode(SrsBuffer *buffer);
    virtual int nb_bytes();
    virtual srs_error_t encode(SrsBuffer *buffer);
};

typedef struct srs_rtcp_report_block_s {
    uint32_t ssrc;
    uint8_t fraction_lost;
    uint32_t lost_packets;
    uint32_t highest_sn;
    uint32_t jitter;
    uint32_t lsr;
    uint32_t dlsr;
}srs_rtcp_rb_t;

class SrsRTCP_SR : public SrsRTCPCommon
{
private:
    uint32_t sender_ssrc_;
    uint64_t ntp_;
    uint32_t rtp_ts_;
    uint32_t send_rtp_packets_;
    uint32_t send_rtp_bytes_;
public:
    SrsRTCP_SR();
    virtual ~SrsRTCP_SR();

    const uint8_t get_rc() const { return header_.rc; }
    // overload SrsRTCPCommon
    virtual const uint8_t type() const { return RTCP_SR; }
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

class SrsRTCP_RR : public SrsRTCPCommon
{
private:
    uint32_t sender_ssrc_;
    srs_rtcp_rb_t rb_;
public:
    SrsRTCP_RR(uint32_t sender_ssrc = 0);
    virtual ~SrsRTCP_RR();

    // overload SrsRTCPCommon
    virtual const uint8_t type() const { return RTCP_RR; }

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

class SrsRTCP_TWCC : public SrsRTCPCommon
{
private:
    uint32_t sender_ssrc_;
    uint32_t media_ssrc_;
    uint16_t base_sn_;
    uint32_t reference_time_;
    uint8_t fb_pkt_count_;
    std::vector<uint16_t> packet_chucks_;
    std::vector<uint8_t> recv_deltas_;
public:
    SrsRTCP_TWCC(uint32_t sender_ssrc = 0);
    virtual ~SrsRTCP_TWCC();

    const uint32_t get_media_ssrc() const;
    const uint16_t get_base_sn() const;
    const uint32_t get_reference_time() const;
    const uint8_t get_feedback_count() const;
    const uint16_t get_packet_status_count() const;
    const std::vector<uint16_t> get_packet_chucks() const;
    const std::vector<uint8_t> get_recv_deltas() const;

    void set_media_ssrc(uint32_t ssrc);
    void set_base_sn(uint16_t sn);
    void set_reference_time(uint32_t time);
    void set_feedback_count(uint8_t count);
    void add_packet_chuck(uint16_t chuck);
    void add_recv_delta(uint8_t delta);

public:
    // ISrsCodec
    virtual srs_error_t decode(SrsBuffer *buffer);
    virtual int nb_bytes();
    virtual srs_error_t encode(SrsBuffer *buffer);   

};

struct less_compare {
    bool operator()(const uint16_t &lhs, const uint16_t &rhs) const {
        return SnCompare(rhs, lhs);
    }
};
class SrsRTCP_Nack : public SrsRTCPCommon
{
private:
    typedef struct pid_blp_s {  
        uint16_t pid;
        uint16_t blp;
    }pid_blp_t;

    uint32_t sender_ssrc_;
    uint32_t media_ssrc_;
    std::set<uint16_t, less_compare> lost_sns_;
public:
    SrsRTCP_Nack(uint32_t sender_ssrc = 0);
    virtual ~SrsRTCP_Nack();

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


class SrsRTCPCompound : public ISrsCodec
{
private:
    std::vector<SrsRTCPCommon* > rtcps_;
    int nb_bytes_;
public:
    SrsRTCPCompound();
    virtual ~SrsRTCPCompound();

    SrsRTCPCommon* get_next_rtcp();
    srs_error_t add_rtcp(SrsRTCPCommon *rtcp);
    void clear();

public:
    // ISrsCodec
    virtual srs_error_t decode(SrsBuffer *buffer);
    virtual int nb_bytes();
    virtual srs_error_t encode(SrsBuffer *buffer);
};

#endif
