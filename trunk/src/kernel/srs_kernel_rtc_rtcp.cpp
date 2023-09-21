//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#include <srs_kernel_rtc_rtcp.hpp>

#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>

#include <arpa/inet.h>
using namespace std;

SrsRtcpCommon::SrsRtcpCommon(): ssrc_(0), data_(NULL), nb_data_(0)
{
    payload_len_ = 0;
}

SrsRtcpCommon::~SrsRtcpCommon()
{ 
}

uint8_t SrsRtcpCommon::type() const
{
    return header_.type;
}

uint8_t SrsRtcpCommon::get_rc() const
{
    return header_.rc;
}

uint32_t SrsRtcpCommon::get_ssrc()
{
    return ssrc_;
}

void SrsRtcpCommon::set_ssrc(uint32_t ssrc)
{
    ssrc_ = ssrc;
}

char* SrsRtcpCommon::data()
{
    return data_;
}

int SrsRtcpCommon::size()
{
    return nb_data_;
}

srs_error_t SrsRtcpCommon::decode_header(SrsBuffer *buffer)
{
    if (!buffer->require(sizeof(SrsRtcpHeader) + 4)) {
        return srs_error_new(ERROR_RTC_RTCP, "require %d", sizeof(SrsRtcpHeader) + 4);
    }

    buffer->read_bytes((char*)(&header_), sizeof(SrsRtcpHeader));
    header_.length = ntohs(header_.length);

    int payload_len = header_.length * 4;
    if (payload_len > buffer->left()) {
        return srs_error_new(ERROR_RTC_RTCP, 
                "require payload len=%u, buffer left=%u", payload_len, buffer->left());
    }
    ssrc_ = buffer->read_4bytes();

    return srs_success;
}

srs_error_t SrsRtcpCommon::encode_header(SrsBuffer *buffer)
{
    if(! buffer->require(sizeof(SrsRtcpHeader) + 4)) {
        return srs_error_new(ERROR_RTC_RTCP, "require %d", sizeof(SrsRtcpHeader) + 4);
    }
    header_.length = htons(header_.length);
    buffer->write_bytes((char*)(&header_), sizeof(SrsRtcpHeader));
    buffer->write_4bytes(ssrc_);

    return srs_success;
}

srs_error_t SrsRtcpCommon::decode(SrsBuffer *buffer)
{
    srs_error_t err = srs_success;
    data_ = buffer->head();
    nb_data_ = buffer->left();

    if(srs_success != (err = decode_header(buffer))) {
        return srs_error_wrap(err, "decode header");
    }

    payload_len_ = (header_.length + 1) * 4 - sizeof(SrsRtcpHeader) - 4;
    buffer->read_bytes((char *)payload_, payload_len_);

    return err;
}

uint64_t SrsRtcpCommon::nb_bytes()
{
    return sizeof(SrsRtcpHeader) + 4 + payload_len_;
}

srs_error_t SrsRtcpCommon::encode(SrsBuffer *buffer)
{
    return srs_error_new(ERROR_RTC_RTCP, "not implement");
}

SrsRtcpApp::SrsRtcpApp()
{
    ssrc_ = 0;
    header_.padding = 0;
    header_.type = SrsRtcpType_app;
    header_.rc = 0;
    header_.version = kRtcpVersion;
}

SrsRtcpApp::~SrsRtcpApp()
{
}

bool SrsRtcpApp::is_rtcp_app(uint8_t *data, int nb_data)
{
    if (!data || nb_data <12) {
        return false;
    }

    SrsRtcpHeader *header = (SrsRtcpHeader*)data;
    if (header->version == kRtcpVersion
            && header->type == SrsRtcpType_app
            && ntohs(header->length) >= 2) {
        return true;
    }

    return false;
}

uint8_t SrsRtcpApp::type() const
{
    return SrsRtcpType_app;
}

uint8_t SrsRtcpApp::get_subtype() const
{
    return header_.rc;
}

string SrsRtcpApp::get_name() const
{
    return string((char*)name_, strnlen((char*)name_, 4));
}

srs_error_t SrsRtcpApp::get_payload(uint8_t*& payload, int& len)
{
    len = payload_len_;
    payload = payload_;

    return srs_success;
}

srs_error_t SrsRtcpApp::set_subtype(uint8_t type)
{
    if(31 < type) {
        return srs_error_new(ERROR_RTC_RTCP, "invalid type: %d", type);
    }

    header_.rc = type;

    return srs_success;
}

srs_error_t SrsRtcpApp::set_name(std::string name)
{
    if(name.length() > 4) {
        return srs_error_new(ERROR_RTC_RTCP, "invalid name length %zu", name.length());
    }

    memset(name_, 0, sizeof(name_));
    memcpy(name_, name.c_str(), name.length());

    return srs_success;
}

srs_error_t SrsRtcpApp::set_payload(uint8_t* payload, int len)
{
    if(len > (kRtcpPacketSize - 12)) {
        return srs_error_new(ERROR_RTC_RTCP, "invalid payload length %d", len);
    }

    payload_len_ = (len + 3)/ 4 * 4;;
    memcpy(payload_, payload, len);
    if (payload_len_ > len) {
        memset(&payload_[len], 0, payload_len_ - len); //padding
    }
    header_.length = payload_len_/4 + 3 - 1;

    return srs_success;
}

srs_error_t SrsRtcpApp::decode(SrsBuffer *buffer)
{
    /*
    @doc: https://tools.ietf.org/html/rfc3550#section-6.7
    0                   1                   2                   3
    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |V=2|P| subtype |   PT=APP=204  |             length            |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                           SSRC/CSRC                           |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                          name (ASCII)                         |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                   application-dependent data                ...
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    */
    srs_error_t err = srs_success;
    data_ = buffer->head();
    nb_data_ = buffer->left();

    if(srs_success != (err = decode_header(buffer))) {
        return srs_error_wrap(err, "decode header");
    }

    if (header_.type != SrsRtcpType_app || !buffer->require(4)) {
        return srs_error_new(ERROR_RTC_RTCP, "not rtcp app");
    }

    buffer->read_bytes((char *)name_, sizeof(name_));

    // TODO: FIXME: Should check size?
    payload_len_ = (header_.length + 1) * 4 - 8 - sizeof(name_);
    if (payload_len_ > 0) {
        buffer->read_bytes((char *)payload_, payload_len_);
    }

    return srs_success;
}

uint64_t SrsRtcpApp::nb_bytes()
{
    return sizeof(SrsRtcpHeader) + sizeof(ssrc_) + sizeof(name_) + payload_len_;
}

srs_error_t SrsRtcpApp::encode(SrsBuffer *buffer)
{
    /*
    @doc: https://tools.ietf.org/html/rfc3550#section-6.7
    0                   1                   2                   3
    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |V=2|P| subtype |   PT=APP=204  |             length            |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                           SSRC/CSRC                           |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                          name (ASCII)                         |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                   application-dependent data                ...
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    */
    srs_error_t err = srs_success;

    if(!buffer->require(nb_bytes())) {
        return srs_error_new(ERROR_RTC_RTCP, "requires %d bytes", nb_bytes());
    }

    if(srs_success != (err = encode_header(buffer))) {
        return srs_error_wrap(err, "encode header");
    }

    buffer->write_bytes((char*)name_, sizeof(name_));
    buffer->write_bytes((char*)payload_, payload_len_);
    
    return srs_success;
}

SrsRtcpSR::SrsRtcpSR()
{
    header_.padding = 0;
    header_.type = SrsRtcpType_sr;
    header_.rc = 0;
    header_.version = kRtcpVersion;
    header_.length = 6;

    ssrc_ = 0;
    ntp_ = 0;
    rtp_ts_ = 0;
    send_rtp_packets_ = 0;
    send_rtp_bytes_ = 0;
    send_rtp_bytes_ = 0;
}

SrsRtcpSR::~SrsRtcpSR()
{
}

uint8_t SrsRtcpSR::get_rc() const
{
    return header_.rc;
}

uint8_t SrsRtcpSR::type() const
{
    return SrsRtcpType_sr;
}

uint64_t SrsRtcpSR::get_ntp() const
{
    return ntp_;
}

uint32_t SrsRtcpSR::get_rtp_ts() const
{
    return rtp_ts_;
}

uint32_t SrsRtcpSR::get_rtp_send_packets() const
{
    return send_rtp_packets_;
}

uint32_t SrsRtcpSR::get_rtp_send_bytes() const
{
    return send_rtp_bytes_;
}

void SrsRtcpSR::set_ntp(uint64_t ntp)
{
    ntp_ = ntp;
}

void SrsRtcpSR::set_rtp_ts(uint32_t ts)
{
    rtp_ts_ = ts;
}

void SrsRtcpSR::set_rtp_send_packets(uint32_t packets)
{
    send_rtp_packets_ = packets;
}

void SrsRtcpSR::set_rtp_send_bytes(uint32_t bytes)
{
    send_rtp_bytes_ = bytes;
}

srs_error_t SrsRtcpSR::decode(SrsBuffer *buffer)
{
    /* @doc: https://tools.ietf.org/html/rfc3550#section-6.4.1
        0                   1                   2                   3
        0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
header |V=2|P|    RC   |   PT=SR=200   |             length            |
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       |                         SSRC of sender                        |
       +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
sender |              NTP timestamp, most significant word             |
info   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       |             NTP timestamp, least significant word             |
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       |                         RTP timestamp                         |
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       |                     sender's packet count                     |
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       |                      sender's octet count                     |
       +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
report |                 SSRC_1 (SSRC of first source)                 |
block  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  1    | fraction lost |       cumulative number of packets lost       |
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       |           extended highest sequence number received           |
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       |                      interarrival jitter                      |
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       |                         last SR (LSR)                         |
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       |                   delay since last SR (DLSR)                  |
       +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
report |                 SSRC_2 (SSRC of second source)                |
block  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  2    :                               ...                             :
       +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
       |                  profile-specific extensions                  |
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    */
    srs_error_t err = srs_success;
    data_ = buffer->head();
    nb_data_ = buffer->left();

    if(srs_success != (err = decode_header(buffer))) {
        return srs_error_wrap(err, "decode header");
    }

    ntp_ = buffer->read_8bytes();
    rtp_ts_ = buffer->read_4bytes();
    send_rtp_packets_ = buffer->read_4bytes();
    send_rtp_bytes_ = buffer->read_4bytes();

    if(header_.rc > 0) {
        char buf[1500];
        buffer->read_bytes(buf, header_.rc * 24);
    }

    return err;
}

uint64_t SrsRtcpSR::nb_bytes()
{
    return (header_.length + 1) * 4;
}

srs_error_t SrsRtcpSR::encode(SrsBuffer *buffer)
{
    srs_error_t err = srs_success;
 /* @doc: https://tools.ietf.org/html/rfc3550#section-6.4.1
        0                   1                   2                   3
        0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
header |V=2|P|    RC   |   PT=SR=200   |             length            |
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       |                         SSRC of sender                        |
       +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
sender |              NTP timestamp, most significant word             |
info   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       |             NTP timestamp, least significant word             |
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       |                         RTP timestamp                         |
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       |                     sender's packet count                     |
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       |                      sender's octet count                     |
       +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
report |                 SSRC_1 (SSRC of first source)                 |
block  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  1    | fraction lost |       cumulative number of packets lost       |
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       |           extended highest sequence number received           |
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       |                      interarrival jitter                      |
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       |                         last SR (LSR)                         |
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       |                   delay since last SR (DLSR)                  |
       +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
report |                 SSRC_2 (SSRC of second source)                |
block  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  2    :                               ...                             :
       +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
       |                  profile-specific extensions                  |
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    */
    if(!buffer->require(nb_bytes())) {
        return srs_error_new(ERROR_RTC_RTCP, "requires %d bytes", nb_bytes());
    }

    if(srs_success != (err = encode_header(buffer))) {
        return srs_error_wrap(err, "encode header");
    }

    buffer->write_8bytes(ntp_);
    buffer->write_4bytes(rtp_ts_);
    buffer->write_4bytes(send_rtp_packets_);
    buffer->write_4bytes(send_rtp_bytes_);

    return err;
}

SrsRtcpRR::SrsRtcpRR(uint32_t sender_ssrc)
{
    header_.padding = 0;
    header_.type = SrsRtcpType_rr;
    header_.rc = 0;
    header_.version = kRtcpVersion;
    header_.length = 7;
    ssrc_ = sender_ssrc;
    memset((void*)&rb_, 0, sizeof(SrsRtcpRB));
}

SrsRtcpRR::~SrsRtcpRR()
{
}

uint8_t SrsRtcpRR::type() const
{
    return SrsRtcpType_rr;
}

uint32_t SrsRtcpRR::get_rb_ssrc() const
{
    return rb_.ssrc;
}

float SrsRtcpRR::get_lost_rate() const
{
    return rb_.fraction_lost / 256;
}

uint32_t SrsRtcpRR::get_lost_packets() const
{
    return rb_.lost_packets;
}

uint32_t SrsRtcpRR::get_highest_sn() const
{
    return rb_.highest_sn;
}

uint32_t SrsRtcpRR::get_jitter() const
{
    return rb_.jitter;
}

uint32_t SrsRtcpRR::get_lsr() const
{
    return rb_.lsr;
}

uint32_t SrsRtcpRR::get_dlsr() const
{
    return rb_.dlsr;
}

void SrsRtcpRR::set_rb_ssrc(uint32_t ssrc)
{
    rb_.ssrc = ssrc;
}

void SrsRtcpRR::set_lost_rate(float rate)
{
    rb_.fraction_lost = rate * 256;
}

void SrsRtcpRR::set_lost_packets(uint32_t count)
{
    rb_.lost_packets = count;
}

void SrsRtcpRR::set_highest_sn(uint32_t sn)
{
    rb_.highest_sn = sn;
}

void SrsRtcpRR::set_jitter(uint32_t jitter)
{
    rb_.jitter = jitter;
}

void SrsRtcpRR::set_lsr(uint32_t lsr)
{
    rb_.lsr = lsr;
}

void SrsRtcpRR::set_dlsr(uint32_t dlsr)
{
    rb_.dlsr = dlsr;
}

void SrsRtcpRR::set_sender_ntp(uint64_t ntp)
{
    uint32_t lsr = (uint32_t)((ntp >> 16) & 0x00000000FFFFFFFF);
    rb_.lsr = lsr;
}

srs_error_t SrsRtcpRR::decode(SrsBuffer *buffer)
{
    /*
    @doc: https://tools.ietf.org/html/rfc3550#section-6.4.2

        0                   1                   2                   3
        0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
header |V=2|P|    RC   |   PT=RR=201   |             length            |
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       |                     SSRC of packet sender                     |
       +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
report |                 SSRC_1 (SSRC of first source)                 |
block  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  1    | fraction lost |       cumulative number of packets lost       |
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       |           extended highest sequence number received           |
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       |                      interarrival jitter                      |
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       |                         last SR (LSR)                         |
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       |                   delay since last SR (DLSR)                  |
       +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
report |                 SSRC_2 (SSRC of second source)                |
block  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  2    :                               ...                             :
       +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
       |                  profile-specific extensions                  |
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    */
    srs_error_t err = srs_success;
    data_ = buffer->head();
    nb_data_ = buffer->left();

    if(srs_success != (err = decode_header(buffer))) {
        return srs_error_wrap(err, "decode header");
    }

    // @doc https://tools.ietf.org/html/rfc3550#section-6.4.2
    // An empty RR packet (RC = 0) MUST be put at the head of a compound
    // RTCP packet when there is no data transmission or reception to
    // report. e.g. {80 c9 00 01 00 00 00 01}
    if(header_.rc == 0) {
        return srs_error_new(ERROR_RTC_RTCP_EMPTY_RR, "rc=0");
    }

    // TODO: FIXME: Security check for read.
    rb_.ssrc = buffer->read_4bytes();
    rb_.fraction_lost = buffer->read_1bytes();
    rb_.lost_packets = buffer->read_3bytes();
    rb_.highest_sn = buffer->read_4bytes();
    rb_.jitter = buffer->read_4bytes();
    rb_.lsr = buffer->read_4bytes();
    rb_.dlsr = buffer->read_4bytes();

    // TODO: FIXME: Security check for read.
    if(header_.rc > 1) {
        char buf[1500];
        buffer->read_bytes(buf, (header_.rc -1 ) * 24);
    }

    return err;
}

uint64_t SrsRtcpRR::nb_bytes()
{
    return (header_.length + 1) * 4;
}

srs_error_t SrsRtcpRR::encode(SrsBuffer *buffer)
{
    /*
    @doc: https://tools.ietf.org/html/rfc3550#section-6.4.2

        0                   1                   2                   3
        0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
header |V=2|P|    RC   |   PT=RR=201   |             length            |
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       |                     SSRC of packet sender                     |
       +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
report |                 SSRC_1 (SSRC of first source)                 |
block  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  1    | fraction lost |       cumulative number of packets lost       |
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       |           extended highest sequence number received           |
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       |                      interarrival jitter                      |
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       |                         last SR (LSR)                         |
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       |                   delay since last SR (DLSR)                  |
       +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
report |                 SSRC_2 (SSRC of second source)                |
block  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  2    :                               ...                             :
       +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
       |                  profile-specific extensions                  |
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    */
    srs_error_t err = srs_success;

    if(!buffer->require(nb_bytes())) {
        return srs_error_new(ERROR_RTC_RTCP, "requires %d bytes", nb_bytes());
    }

    header_.rc = 1;
    if(srs_success != (err = encode_header(buffer))) {
        return srs_error_wrap(err, "encode header");
    }
    
    buffer->write_4bytes(rb_.ssrc);
    buffer->write_1bytes(rb_.fraction_lost);
    buffer->write_3bytes(rb_.lost_packets);
    buffer->write_4bytes(rb_.highest_sn);
    buffer->write_4bytes(rb_.jitter);
    buffer->write_4bytes(rb_.lsr);
    buffer->write_4bytes(rb_.dlsr);

    return err;
}

SrsRtcpTWCC::SrsRtcpTWCCChunk::SrsRtcpTWCCChunk()
        : size(0), all_same(true), has_large_delta(false)
{
}

SrsRtcpTWCC::SrsRtcpTWCC(uint32_t sender_ssrc) : pkt_len(0)
{
    header_.padding = 0;
    header_.type = SrsRtcpType_rtpfb;
    header_.rc = 15;
    header_.version = kRtcpVersion;
    ssrc_ = sender_ssrc;
    media_ssrc_ = 0;
    base_sn_ = 0;
    reference_time_ = 0;
    fb_pkt_count_ = 0;
    next_base_sn_ = 0;
}

SrsRtcpTWCC::~SrsRtcpTWCC()
{
}

void SrsRtcpTWCC::clear()
{
    encoded_chucks_.clear();
    pkt_deltas_.clear();
    recv_packets_.clear();
    recv_sns_.clear();
    next_base_sn_ = 0;
}

uint16_t SrsRtcpTWCC::get_base_sn() const
{
    return base_sn_;
}

uint32_t SrsRtcpTWCC::get_reference_time() const
{
    return reference_time_;
}

uint8_t SrsRtcpTWCC::get_feedback_count() const
{
    return fb_pkt_count_;
}
    
vector<uint16_t> SrsRtcpTWCC::get_packet_chucks() const
{
    return encoded_chucks_;
}

vector<uint16_t> SrsRtcpTWCC::get_recv_deltas() const
{
    return pkt_deltas_;
}

void SrsRtcpTWCC::set_base_sn(uint16_t sn)
{
    base_sn_ = sn;
}

void SrsRtcpTWCC::set_reference_time(uint32_t time)
{
    reference_time_ = time;
}

void SrsRtcpTWCC::set_feedback_count(uint8_t count)
{
    fb_pkt_count_ = count;
}
    
void SrsRtcpTWCC::add_packet_chuck(uint16_t chunk)
{
    encoded_chucks_.push_back(chunk);
}

void SrsRtcpTWCC::add_recv_delta(uint16_t delta)
{
    pkt_deltas_.push_back(delta);
}

srs_error_t SrsRtcpTWCC::recv_packet(uint16_t sn, srs_utime_t ts)
{
    map<uint16_t, srs_utime_t>::iterator it = recv_packets_.find(sn);
    if(it != recv_packets_.end()) {
        return srs_error_new(ERROR_RTC_RTCP, "TWCC dup seq: %d", sn);
    }

    recv_packets_[sn] = ts;
    recv_sns_.insert(sn);

    return srs_success;
}

bool SrsRtcpTWCC::need_feedback()
{
    return recv_packets_.size() > 0;
}

srs_error_t SrsRtcpTWCC::decode(SrsBuffer *buffer)
{
    /*
    @doc: https://tools.ietf.org/html/draft-holmer-rmcat-transport-wide-cc-extensions-01#section-3.1
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
       .                                                               .
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       |           recv delta          |  recv delta   | zero padding  |
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    */
    srs_error_t err = srs_success;
    data_ = buffer->head();
    nb_data_ = buffer->left();

    if(srs_success != (err = decode_header(buffer))) {
        return srs_error_wrap(err, "decode header");
    }

    payload_len_ = (header_.length + 1) * 4 - sizeof(SrsRtcpHeader) - 4;
    buffer->read_bytes((char *)payload_, payload_len_);

    return err;
}

uint64_t SrsRtcpTWCC::nb_bytes()
{
    return kMaxUDPDataSize;
}

srs_utime_t SrsRtcpTWCC::calculate_delta_us(srs_utime_t ts, srs_utime_t last)
{
    int64_t divisor = kTwccFbReferenceTimeDivisor;
    int64_t delta_us = (ts - last) % divisor;

    if (delta_us > (divisor >> 1))
        delta_us -= divisor;

    delta_us += (delta_us < 0) ? (-kTwccFbDeltaUnit / 2) : (kTwccFbDeltaUnit / 2);
    delta_us /= kTwccFbDeltaUnit;

    return delta_us;
}

bool SrsRtcpTWCC::can_add_to_chunk(SrsRtcpTWCC::SrsRtcpTWCCChunk& chunk, int delta_size)
{
	srs_verbose("can_add %d chunk->size %u delta_sizes %d %d %d %d %d %d %d %d %d %d %d %d %d %d all_same %d has_large_delta %d",
	    delta_size, chunk.size, chunk.delta_sizes[0], chunk.delta_sizes[1], chunk.delta_sizes[2], chunk.delta_sizes[3],
	    chunk.delta_sizes[4], chunk.delta_sizes[5], chunk.delta_sizes[6], chunk.delta_sizes[7], chunk.delta_sizes[8],
	    chunk.delta_sizes[9], chunk.delta_sizes[10], chunk.delta_sizes[11], chunk.delta_sizes[12], chunk.delta_sizes[13],
	    (int)chunk.all_same, (int)chunk.has_large_delta);

    if (chunk.size < kTwccFbTwoBitElements) {
        return true;
    }

    if (chunk.size < kTwccFbOneBitElements && !chunk.has_large_delta && delta_size != kTwccFbLargeRecvDeltaBytes) {
        return true;
    }

    if (chunk.size < kTwccFbMaxRunLength && chunk.all_same && chunk.delta_sizes[0] == delta_size) {
        srs_verbose("< %d && all_same && delta_size[0] %d == %d", kTwccFbMaxRunLength, chunk.delta_sizes[0], delta_size);
        return true;
    }

    return false;
}

void SrsRtcpTWCC::add_to_chunk(SrsRtcpTWCC::SrsRtcpTWCCChunk& chunk, int delta_size)
{
    if (chunk.size < kTwccFbMaxBitElements) {
        chunk.delta_sizes[chunk.size] = delta_size;
    }

    chunk.size += 1;
    chunk.all_same = chunk.all_same && delta_size == chunk.delta_sizes[0];
    chunk.has_large_delta = chunk.has_large_delta || delta_size >= kTwccFbLargeRecvDeltaBytes;
}

srs_error_t SrsRtcpTWCC::encode_chunk_run_length(SrsRtcpTWCC::SrsRtcpTWCCChunk& chunk)
{
    if (!chunk.all_same || chunk.size > kTwccFbMaxRunLength) {
        return srs_error_new(ERROR_RTC_RTCP, "invalid run all_same:%d, size:%d", chunk.all_same, chunk.size);
    }

    uint16_t encoded_chunk = (chunk.delta_sizes[0] << 13) | chunk.size;

    encoded_chucks_.push_back(encoded_chunk);
    pkt_len += sizeof(encoded_chunk);

    return srs_success;
}

srs_error_t SrsRtcpTWCC::encode_chunk_one_bit(SrsRtcpTWCC::SrsRtcpTWCCChunk& chunk)
{
    int i = 0;
    if (chunk.has_large_delta) {
        return srs_error_new(ERROR_RTC_RTCP, "invalid large delta");
    }

    uint16_t encoded_chunk = 0x8000;
    for (i = 0; i < chunk.size; ++i) {
        encoded_chunk |= (chunk.delta_sizes[i] << (kTwccFbOneBitElements - 1 - i));
    }

    encoded_chucks_.push_back(encoded_chunk);
    pkt_len += sizeof(encoded_chunk);

    // 1 0 symbol_list
    return srs_success;
}
    
srs_error_t SrsRtcpTWCC::encode_chunk_two_bit(SrsRtcpTWCC::SrsRtcpTWCCChunk& chunk, size_t size, bool shift)
{
    unsigned int i = 0;
    uint8_t delta_size = 0;
    
    uint16_t encoded_chunk = 0xc000;
    // 1 1 symbol_list
    for (i = 0; i < size; ++i) {
        encoded_chunk |= (chunk.delta_sizes[i] << (2 * (kTwccFbTwoBitElements - 1 - i)));
    }
    encoded_chucks_.push_back(encoded_chunk);
    pkt_len += sizeof(encoded_chunk);

    if (shift) {
        chunk.size -= size;
        chunk.all_same = true;
        chunk.has_large_delta = false;
        for (i = 0; i < chunk.size; ++i) {
            delta_size = chunk.delta_sizes[i + size];
            chunk.delta_sizes[i] = delta_size;
            chunk.all_same = (chunk.all_same && delta_size == chunk.delta_sizes[0]);
            chunk.has_large_delta = chunk.has_large_delta || delta_size == kTwccFbLargeRecvDeltaBytes;
        }
    }

    return srs_success;
}

void SrsRtcpTWCC::reset_chunk(SrsRtcpTWCC::SrsRtcpTWCCChunk& chunk)
{
    chunk.size = 0;

    chunk.all_same = true;
    chunk.has_large_delta = false;
}

srs_error_t SrsRtcpTWCC::encode_chunk(SrsRtcpTWCC::SrsRtcpTWCCChunk& chunk)
{
    srs_error_t err = srs_success;

    if (can_add_to_chunk(chunk, 0) && can_add_to_chunk(chunk, 1) && can_add_to_chunk(chunk, 2)) {
        return srs_error_new(ERROR_RTC_RTCP, "TWCC chunk");
    }

    if (chunk.all_same) {
        if ((err = encode_chunk_run_length(chunk)) != srs_success) {
            return srs_error_wrap(err, "encode run");
        }
        reset_chunk(chunk);
        return err;
    }

    if (chunk.size == kTwccFbOneBitElements) {
        if ((err = encode_chunk_one_bit(chunk)) != srs_success) {
            return srs_error_wrap(err, "encode chunk");
        }
        reset_chunk(chunk);
        return err;
    }

    if ((err = encode_chunk_two_bit(chunk, kTwccFbTwoBitElements, true)) != srs_success) {
        return srs_error_wrap(err, "encode chunk");
    }

    return err;
}

srs_error_t SrsRtcpTWCC::encode_remaining_chunk(SrsRtcpTWCC::SrsRtcpTWCCChunk& chunk)
{
    if (chunk.all_same) {
        return encode_chunk_run_length(chunk);
    } else if (chunk.size <= kTwccFbTwoBitElements) {
        // FIXME, TRUE or FALSE
        return encode_chunk_two_bit(chunk, chunk.size, false);
    }
    return encode_chunk_one_bit(chunk);
}

srs_error_t SrsRtcpTWCC::process_pkt_chunk(SrsRtcpTWCC::SrsRtcpTWCCChunk& chunk, int delta_size)
{
    srs_error_t err = srs_success;

    size_t needed_chunk_size = chunk.size == 0 ? kTwccFbChunkBytes : 0;

    size_t might_occupied = pkt_len + needed_chunk_size + delta_size;
    if (might_occupied > (size_t)kRtcpPacketSize) {
        return srs_error_new(ERROR_RTC_RTCP, "might_occupied %zu", might_occupied);
    }

    if (can_add_to_chunk(chunk, delta_size)) {
        //pkt_len += needed_chunk_size;
        add_to_chunk(chunk, delta_size);
        return err;
    }
    if ((err = encode_chunk(chunk)) != srs_success) {
        return srs_error_wrap(err, "encode chunk, delta_size %u", delta_size);
    }
    add_to_chunk(chunk, delta_size);
    return err;
}

srs_error_t SrsRtcpTWCC::encode(SrsBuffer *buffer)
{
    srs_error_t err = srs_success;

    err = do_encode(buffer);

    if (err != srs_success || next_base_sn_ == 0) {
        clear();
    }

    return err;
}

srs_error_t SrsRtcpTWCC::do_encode(SrsBuffer *buffer)
{
    /*
    @doc: https://tools.ietf.org/html/draft-holmer-rmcat-transport-wide-cc-extensions-01#section-3.1
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
       .                                                               .
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       |           recv delta          |  recv delta   | zero padding  |
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    */
    srs_error_t err = srs_success;

    if(!buffer->require(nb_bytes())) {
        return srs_error_new(ERROR_RTC_RTCP, "requires %d bytes", nb_bytes());
    }

    pkt_len = kTwccFbPktHeaderSize;

    set<uint16_t, SrsSeqCompareLess>::iterator it_sn = recv_sns_.begin();
    if (!next_base_sn_) {
        base_sn_ = *it_sn;
    } else {
        base_sn_ = next_base_sn_;
        it_sn = recv_sns_.find(base_sn_);
    }

    map<uint16_t, srs_utime_t>::iterator it_ts = recv_packets_.find(base_sn_);
    srs_utime_t ts = it_ts->second;

    reference_time_ = (ts % kTwccFbReferenceTimeDivisor) / kTwccFbTimeMultiplier;
    srs_utime_t last_ts = (srs_utime_t)(reference_time_) * kTwccFbTimeMultiplier;

    uint16_t last_sn = base_sn_;
    uint16_t packet_count = 0;

    // encode chunk
    SrsRtcpTWCC::SrsRtcpTWCCChunk chunk;
    for(; it_sn != recv_sns_.end(); ++it_sn) {
        // check whether exceed buffer len
        // max recv_delta_size = 2
        if (pkt_len + 2 >= buffer->left()) {
            break;
        }

        uint16_t current_sn = *it_sn;
        // calculate delta
        it_ts = recv_packets_.find(current_sn);
        if (it_ts == recv_packets_.end()) {
            continue;
        }

        packet_count++;
        srs_utime_t delta_us = calculate_delta_us(it_ts->second, last_ts);
        int16_t delta = delta_us;
        if(delta != delta_us) {
            return srs_error_new(ERROR_RTC_RTCP, "twcc: delta:%" PRId64 ", exceeds the 16bits", delta_us);
        }

        if(current_sn > (last_sn + 1)) {
            // lost packet
            for(uint16_t lost_sn = last_sn + 1; lost_sn < current_sn; ++lost_sn) {
                process_pkt_chunk(chunk, 0);
                packet_count++;
            }
        }

        // FIXME 24-bit base receive delta not supported
        int recv_delta_size = (delta >= 0 && delta <= 0xff) ? 1 : 2;
        if ((err = process_pkt_chunk(chunk, recv_delta_size)) != srs_success) {
            return srs_error_wrap(err, "delta_size %d, failed to append_recv_delta", recv_delta_size);
        }

        pkt_deltas_.push_back(delta);
        last_ts += delta * kTwccFbDeltaUnit;
        pkt_len += recv_delta_size;
        last_sn = current_sn;

        recv_packets_.erase(it_ts);
    }

    next_base_sn_ = 0;
    if (it_sn != recv_sns_.end()) {
        next_base_sn_ = *it_sn;
    }

    if(0 < chunk.size) {
        if((err = encode_remaining_chunk(chunk)) != srs_success) {
            return srs_error_wrap(err, "encode chunk");
        }
    }

    // encode rtcp twcc packet
    if((pkt_len % 4) == 0) {
        header_.length = pkt_len / 4;
    } else {
        header_.length = (pkt_len + 4 - (pkt_len%4)) / 4;
    }
    header_.length -= 1;

    if(srs_success != (err = encode_header(buffer))) {
        return srs_error_wrap(err, "encode header");
    }

    buffer->write_4bytes(media_ssrc_);
    buffer->write_2bytes(base_sn_);
    buffer->write_2bytes(packet_count);
    buffer->write_3bytes(reference_time_);
    buffer->write_1bytes(fb_pkt_count_);

    int required_size = encoded_chucks_.size() * 2;
    if(!buffer->require(required_size)) {
        return srs_error_new(ERROR_RTC_RTCP, "encoded_chucks_[%d] requires %d bytes", (int)encoded_chucks_.size(), required_size);
    }

    for(vector<uint16_t>::iterator it = encoded_chucks_.begin(); it != encoded_chucks_.end(); ++it) {
        buffer->write_2bytes(*it);
    }

    required_size = pkt_deltas_.size() * 2;
    if(!buffer->require(required_size)) {
        return srs_error_new(ERROR_RTC_RTCP, "pkt_deltas_[%d] requires %d bytes", (int)pkt_deltas_.size(), required_size);
    }

    for(vector<uint16_t>::iterator it = pkt_deltas_.begin(); it != pkt_deltas_.end(); ++it) {
        if(0 <= *it && 0xFF >= *it) {
            // small delta
            uint8_t delta = *it;
            buffer->write_1bytes(delta);
        } else {
            // large or negative delta
            buffer->write_2bytes(*it);
        }
    }

    while((pkt_len % 4) != 0) {
        buffer->write_1bytes(0);
        pkt_len++;
    }

    encoded_chucks_.clear();
    pkt_deltas_.clear();

    return err;
}

SrsRtcpNack::SrsRtcpNack(uint32_t sender_ssrc)
{
    header_.padding = 0;
    header_.type = SrsRtcpType_rtpfb;
    header_.rc = 1;
    header_.version = kRtcpVersion;
    ssrc_ = sender_ssrc;
    media_ssrc_ = 0;
}

SrsRtcpNack::~SrsRtcpNack()
{
}

vector<uint16_t> SrsRtcpNack::get_lost_sns() const
{
    vector<uint16_t> sn;
    for(set<uint16_t, SrsSeqCompareLess>::iterator it = lost_sns_.begin(); it != lost_sns_.end(); ++it) {
        sn.push_back(*it);
    }
    return sn;
}

bool SrsRtcpNack::empty()
{
    return lost_sns_.empty();
}

void SrsRtcpNack::add_lost_sn(uint16_t sn)
{
    lost_sns_.insert(sn);
}

srs_error_t SrsRtcpNack::decode(SrsBuffer *buffer)
{
    /*
    @doc: https://tools.ietf.org/html/rfc4585#section-6.1
        0                   1                   2                   3
    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |V=2|P|   FMT   |       PT      |          length               |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                  SSRC of packet sender                        |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                  SSRC of media source                         |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   :            Feedback Control Information (FCI)                 :
   :                                                               :

    Generic NACK
    0                   1                   2                   3
    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |            PID                |             BLP               |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    */
    srs_error_t err = srs_success;
    data_ = buffer->head();
    nb_data_ = buffer->left();

    if(srs_success != (err = decode_header(buffer))) {
        return srs_error_wrap(err, "decode header");
    }

    media_ssrc_ = buffer->read_4bytes();
    char bitmask[20];
    for(int i = 0; i < (header_.length - 2); i++) {
        uint16_t pid = buffer->read_2bytes();
        uint16_t blp = buffer->read_2bytes();
        lost_sns_.insert(pid);
        memset(bitmask, 0, 20);
        for(int j=0; j<16; j++) {
            bitmask[j] = (blp & ( 1 << j )) >> j ? '1' : '0';
            if((blp & ( 1 << j )) >> j)
                lost_sns_.insert(pid+j+1);
        }
        bitmask[16] = '\n';
        srs_info("[%d] %d / %s", i, pid, bitmask);
    }

    return err;
}
uint64_t SrsRtcpNack::nb_bytes()
{
    return kRtcpPacketSize;
}

srs_error_t SrsRtcpNack::encode(SrsBuffer *buffer)
{
    /*
    @doc: https://tools.ietf.org/html/rfc4585#section-6.1
        0                   1                   2                   3
    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |V=2|P|   FMT   |       PT      |          length               |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                  SSRC of packet sender                        |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                  SSRC of media source                         |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   :            Feedback Control Information (FCI)                 :
   :                                                               :

    Generic NACK
    0                   1                   2                   3
    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |            PID                |             BLP               |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    */
    srs_error_t err = srs_success;
    if(!buffer->require(nb_bytes())) {
        return srs_error_new(ERROR_RTC_RTCP, "requires %d bytes", nb_bytes());
    }

    vector<SrsPidBlp> chunks;
    do {
        SrsPidBlp chunk;
        chunk.in_use = false;
        uint16_t pid = 0;
        for(set<uint16_t, SrsSeqCompareLess>::iterator it = lost_sns_.begin(); it != lost_sns_.end(); ++it) {
            uint16_t sn = *it;
            if(!chunk.in_use) {
                chunk.pid = sn;
                chunk.blp = 0;
                chunk.in_use = true;
                pid = sn;
                continue;
            }
            if((sn - pid) < 1) {
                srs_info("skip seq %d", sn);
            } else if( (sn - pid) > 16) {
                // append full chunk
                chunks.push_back(chunk);

                // start new chunk
                chunk.pid = sn;
                chunk.blp = 0;
                chunk.in_use = true;
                pid = sn;
            } else {
                chunk.blp |= 1 << (sn-pid-1);
            }
        }
        if(chunk.in_use) {
            chunks.push_back(chunk);
        }

        header_.length = 2 + chunks.size();
        if(srs_success != (err = encode_header(buffer))) {
            err = srs_error_wrap(err, "encode header");
            break;
        }

        buffer->write_4bytes(media_ssrc_);
        for(vector<SrsPidBlp>::iterator it_chunk = chunks.begin(); it_chunk != chunks.end(); it_chunk++) {
            buffer->write_2bytes(it_chunk->pid);
            buffer->write_2bytes(it_chunk->blp);
        }
    } while(0);

    return err;
}

SrsRtcpFbCommon::SrsRtcpFbCommon()
{
    header_.padding = 0;
    header_.type = SrsRtcpType_psfb;
    header_.rc = 1;
    header_.version = kRtcpVersion;
    //ssrc_ = sender_ssrc;
}

SrsRtcpFbCommon::~SrsRtcpFbCommon()
{

}

uint32_t SrsRtcpFbCommon::get_media_ssrc() const
{
    return media_ssrc_;
}

void SrsRtcpFbCommon::set_media_ssrc(uint32_t ssrc)
{
    media_ssrc_ = ssrc;
}

srs_error_t SrsRtcpFbCommon::decode(SrsBuffer *buffer)
{
    /*
    @doc: https://tools.ietf.org/html/rfc4585#section-6.1
        0                   1                   2                   3
    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |V=2|P|   FMT   |       PT      |          length               |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                  SSRC of packet sender                        |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                  SSRC of media source                         |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   :            Feedback Control Information (FCI)                 :
   :                                                               :
   */

    srs_error_t err = srs_success;
    data_ = buffer->head();
    nb_data_ = buffer->left();

    if(srs_success != (err = decode_header(buffer))) {
        return srs_error_wrap(err, "decode header");
    }

    media_ssrc_ = buffer->read_4bytes();
    int len = (header_.length + 1) * 4 - 12;
    buffer->skip(len);
    return err;
}

uint64_t SrsRtcpFbCommon::nb_bytes()
{
    return kRtcpPacketSize;
}

srs_error_t SrsRtcpFbCommon::encode(SrsBuffer *buffer)
{
    return srs_error_new(ERROR_RTC_RTCP, "not support");
}

SrsRtcpPli::SrsRtcpPli(uint32_t sender_ssrc/*= 0*/)
{
    header_.padding = 0;
    header_.type = SrsRtcpType_psfb;
    header_.rc = kPLI;
    header_.version = kRtcpVersion;
    ssrc_ = sender_ssrc;
}
    
SrsRtcpPli::~SrsRtcpPli()
{
}

srs_error_t SrsRtcpPli::decode(SrsBuffer *buffer)
{
    /*
    @doc: https://tools.ietf.org/html/rfc4585#section-6.1
        0                   1                   2                   3
    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |V=2|P|   FMT   |       PT      |          length               |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                  SSRC of packet sender                        |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                  SSRC of media source                         |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   :            Feedback Control Information (FCI)                 :
   :                                                               :
   */

    srs_error_t err = srs_success;
    data_ = buffer->head();
    nb_data_ = buffer->left();

    if(srs_success != (err = decode_header(buffer))) {
        return srs_error_wrap(err, "decode header");
    }

    media_ssrc_ = buffer->read_4bytes();
    return err;
}

uint64_t SrsRtcpPli::nb_bytes()
{
    return 12;
}

srs_error_t SrsRtcpPli::encode(SrsBuffer *buffer)
{
    /*
    @doc: https://tools.ietf.org/html/rfc4585#section-6.1
        0                   1                   2                   3
    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |V=2|P|   FMT   |       PT      |          length               |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                  SSRC of packet sender                        |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                  SSRC of media source                         |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   :            Feedback Control Information (FCI)                 :
   :                                                               :
    */
    srs_error_t err = srs_success;
    if(!buffer->require(nb_bytes())) {
        return srs_error_new(ERROR_RTC_RTCP, "requires %d bytes", nb_bytes());
    }

    header_.length = 2;
    if(srs_success != (err = encode_header(buffer))) {
        return srs_error_wrap(err, "encode header");
    }

    buffer->write_4bytes(media_ssrc_);
    
    return err;
}

SrsRtcpSli::SrsRtcpSli(uint32_t sender_ssrc/*= 0*/)
{
    first_ = 0;
    number_ = 0;
    picture_ = 0;

    header_.padding = 0;
    header_.type = SrsRtcpType_psfb;
    header_.rc = kSLI;
    header_.version = kRtcpVersion;
    ssrc_ = sender_ssrc;
}

SrsRtcpSli::~SrsRtcpSli()
{
}


srs_error_t SrsRtcpSli::decode(SrsBuffer *buffer)
{
    /*
    @doc: https://tools.ietf.org/html/rfc4585#section-6.1
        0                   1                   2                   3
    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |V=2|P|   FMT   |       PT      |          length               |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                  SSRC of packet sender                        |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                  SSRC of media source                         |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   :            Feedback Control Information (FCI)                 :
   :                                                               :


    @doc: https://tools.ietf.org/html/rfc4585#section-6.3.2
    0                   1                   2                   3
    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |            First        |        Number           | PictureID |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   */

    srs_error_t err = srs_success;
    data_ = buffer->head();
    nb_data_ = buffer->left();

    if(srs_success != (err = decode_header(buffer))) {
        return srs_error_wrap(err, "decode header");
    }

    media_ssrc_ = buffer->read_4bytes();
    int len = (header_.length + 1) * 4 - 12;
    buffer->skip(len);
    return err;
}

uint64_t SrsRtcpSli::nb_bytes()
{
    return kRtcpPacketSize;
}

srs_error_t SrsRtcpSli::encode(SrsBuffer *buffer)
{
    srs_error_t err = srs_success;

    return err;
}

SrsRtcpRpsi::SrsRtcpRpsi(uint32_t sender_ssrc/* = 0*/)
{
    pb_ = 0;
    payload_type_ = 0;
    native_rpsi_ = NULL;
    nb_native_rpsi_ = 0;

    header_.padding = 0;
    header_.type = SrsRtcpType_psfb;
    header_.rc = kRPSI;
    header_.version = kRtcpVersion;
    ssrc_ = sender_ssrc;
}

SrsRtcpRpsi::~SrsRtcpRpsi()
{
}

srs_error_t SrsRtcpRpsi::decode(SrsBuffer *buffer)
{
/*
    @doc: https://tools.ietf.org/html/rfc4585#section-6.1
        0                   1                   2                   3
    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |V=2|P|   FMT   |       PT      |          length               |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                  SSRC of packet sender                        |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                  SSRC of media source                         |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   :            Feedback Control Information (FCI)                 :
   :                                                               :


    @doc: https://tools.ietf.org/html/rfc4585#section-6.3.3
    0                   1                   2                   3
    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |      PB       |0| Payload Type|    Native RPSI bit string     |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |   defined per codec          ...                | Padding (0) |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   */

    srs_error_t err = srs_success;
    data_ = buffer->head();
    nb_data_ = buffer->left();

    if(srs_success != (err = decode_header(buffer))) {
        return srs_error_wrap(err, "decode header");
    }

    media_ssrc_ = buffer->read_4bytes();
    int len = (header_.length + 1) * 4 - 12;
    buffer->skip(len);
    return err;
}

uint64_t SrsRtcpRpsi::nb_bytes()
{
    return kRtcpPacketSize;
}

srs_error_t SrsRtcpRpsi::encode(SrsBuffer *buffer)
{
    srs_error_t err = srs_success;

    return err;
}

SrsRtcpXr::SrsRtcpXr(uint32_t ssrc/*= 0*/)
{
    header_.padding = 0;
    header_.type = SrsRtcpType_xr;
    header_.rc = 0;
    header_.version = kRtcpVersion;
    ssrc_ = ssrc;
}

SrsRtcpXr::~SrsRtcpXr()
{
}

srs_error_t SrsRtcpXr::decode(SrsBuffer *buffer)
{
/*
    @doc: https://tools.ietf.org/html/rfc3611#section-2
    0                   1                   2                   3
    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |V=2|P|reserved |   PT=XR=207   |             length            |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                              SSRC                             |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   :                         report blocks                         :
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   */

    srs_error_t err = srs_success;
    data_ = buffer->head();
    nb_data_ = buffer->left();

    if(srs_success != (err = decode_header(buffer))) {
        return srs_error_wrap(err, "decode header");
    }

    int len = (header_.length + 1) * 4 - 8;
    buffer->skip(len);
    return err;
}

uint64_t SrsRtcpXr::nb_bytes()
{
    return kRtcpPacketSize;
}

srs_error_t SrsRtcpXr::encode(SrsBuffer *buffer)
{
    return srs_error_new(ERROR_RTC_RTCP, "not support");
}

SrsRtcpCompound::SrsRtcpCompound(): nb_bytes_(0), data_(NULL), nb_data_(0)
{
}

SrsRtcpCompound::~SrsRtcpCompound()
{
   clear();
}

SrsRtcpCommon* SrsRtcpCompound::get_next_rtcp()
{
    if(rtcps_.empty()) {
        return NULL;
    }
    SrsRtcpCommon *rtcp = rtcps_.back();
    rtcps_.pop_back();
    return rtcp;
}

srs_error_t SrsRtcpCompound::add_rtcp(SrsRtcpCommon *rtcp)
{
    int new_len = rtcp->nb_bytes();
    if((new_len + nb_bytes_) > kRtcpPacketSize) {
        return srs_error_new(ERROR_RTC_RTCP, "overflow, new rtcp: %d, current: %d", new_len, nb_bytes_);
    }
    nb_bytes_ += new_len;
    rtcps_.push_back(rtcp);

    return srs_success;
}

srs_error_t SrsRtcpCompound::decode(SrsBuffer *buffer)
{
    srs_error_t err = srs_success;
    data_ = buffer->data();
    nb_data_ = buffer->size();

    while (!buffer->empty()) {
        SrsRtcpCommon* rtcp = NULL;
        SrsRtcpHeader* header = (SrsRtcpHeader*)(buffer->head());
        if (header->type == SrsRtcpType_sr) {
            rtcp = new SrsRtcpSR();
        } else if (header->type == SrsRtcpType_rr) {
            rtcp = new SrsRtcpRR();
        } else if (header->type == SrsRtcpType_rtpfb) {
            if(1 == header->rc) {
                //nack
                rtcp = new SrsRtcpNack();
            } else if (15 == header->rc) {
                //twcc
                rtcp = new SrsRtcpTWCC();
            } else {
                // common fb
                rtcp = new SrsRtcpFbCommon();
            }
        } else if(header->type == SrsRtcpType_psfb) {
            if(1 == header->rc) {
                // pli
                rtcp = new SrsRtcpPli();
            } else if(2 == header->rc) {
                //sli
                rtcp = new SrsRtcpSli();
            } else if(3 == header->rc) {
                //rpsi
                rtcp = new SrsRtcpRpsi();
            } else {
                // common psfb
                rtcp = new SrsRtcpFbCommon();
            }
        } else if(header->type == SrsRtcpType_xr) {
            rtcp = new SrsRtcpXr();
        } else {
            rtcp = new SrsRtcpCommon();
        }

        if(srs_success != (err = rtcp->decode(buffer))) {
            srs_freep(rtcp);

            // @doc https://tools.ietf.org/html/rfc3550#section-6.4.2
            // An empty RR packet (RC = 0) MUST be put at the head of a compound
            // RTCP packet when there is no data transmission or reception to
            // report. e.g. {80 c9 00 01 00 00 00 01}
            if (ERROR_RTC_RTCP_EMPTY_RR == srs_error_code(err)) {
                srs_freep(err);
                continue;
            }

            return srs_error_wrap(err, "decode rtcp type=%u rc=%u", header->type, header->rc);
        }

        rtcps_.push_back(rtcp);
    }

    return err;
}

uint64_t SrsRtcpCompound::nb_bytes()
{
    return kRtcpPacketSize;
}

srs_error_t SrsRtcpCompound::encode(SrsBuffer *buffer)
{
    srs_error_t err = srs_success;
    if(!buffer->require(nb_bytes_)) {
        return srs_error_new(ERROR_RTC_RTCP, "requires %d bytes", nb_bytes_);
    }

    vector<SrsRtcpCommon*>::iterator it;
    for(it = rtcps_.begin(); it != rtcps_.end(); ++it) {
        SrsRtcpCommon *rtcp = *it;
        if((err = rtcp->encode(buffer)) != srs_success) {
            return srs_error_wrap(err, "encode compound type:%d", rtcp->type());
        }
    }

    clear();
    return err;
}

void SrsRtcpCompound::clear()
{
    vector<SrsRtcpCommon*>::iterator it;
    for(it = rtcps_.begin(); it != rtcps_.end(); ++it) {
        SrsRtcpCommon *rtcp = *it;
        delete rtcp;
        rtcp = NULL;
    }
    rtcps_.clear();
    nb_bytes_ = 0;
}

char* SrsRtcpCompound::data()
{
    return data_;
}
    
int SrsRtcpCompound::size()
{
    return nb_data_;
}

