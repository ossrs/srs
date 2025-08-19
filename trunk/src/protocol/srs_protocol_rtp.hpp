//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_PROTOCOL_RTP_HPP
#define SRS_PROTOCOL_RTP_HPP

#include <srs_core.hpp>

#include <vector>

#include <srs_kernel_codec.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_flv.hpp>
#include <srs_kernel_rtc_rtp.hpp>

class SrsSharedPtrMessage;
class SrsSample;
class SrsRtpPacket;
class SrsFormat;

// RTP video builder for packaging video NALUs into RTP packets
class SrsRtpVideoBuilder
{
private:
    uint16_t video_sequence_;
    uint32_t video_ssrc_;
    uint8_t video_payload_type_;
    SrsFormat *format_;

public:
    SrsRtpVideoBuilder();
    virtual ~SrsRtpVideoBuilder();

public:
    srs_error_t initialize(SrsFormat *format, uint32_t ssrc, uint8_t payload_type);
    srs_error_t package_stap_a(SrsSharedPtrMessage *msg, SrsRtpPacket *pkt);
    srs_error_t package_nalus(SrsSharedPtrMessage *msg, const std::vector<SrsSample *> &samples, std::vector<SrsRtpPacket *> &pkts);
    srs_error_t package_single_nalu(SrsSharedPtrMessage *msg, SrsSample *sample, std::vector<SrsRtpPacket *> &pkts);
    srs_error_t package_fu_a(SrsSharedPtrMessage *msg, SrsSample *sample, int fu_payload_size, std::vector<SrsRtpPacket *> &pkts);
};

#endif
