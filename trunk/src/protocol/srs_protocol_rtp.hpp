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

class SrsMediaPacket;
class SrsNaluSample;
class SrsRtpPacket;
class SrsFormat;

// RTP video builder for packaging video NALUs into RTP packets
class SrsRtpVideoBuilder
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    uint16_t video_sequence_;
    uint32_t video_ssrc_;
    uint8_t video_payload_type_;
    SrsFormat *format_;

public:
    SrsRtpVideoBuilder();
    virtual ~SrsRtpVideoBuilder();

public:
    srs_error_t initialize(SrsFormat *format, uint32_t ssrc, uint8_t payload_type);
    srs_error_t package_stap_a(SrsMediaPacket *msg, SrsRtpPacket *pkt);
    srs_error_t package_nalus(SrsMediaPacket *msg, const std::vector<SrsNaluSample *> &samples, std::vector<SrsRtpPacket *> &pkts);
    srs_error_t package_single_nalu(SrsMediaPacket *msg, SrsNaluSample *sample, std::vector<SrsRtpPacket *> &pkts);
    srs_error_t package_fu_a(SrsMediaPacket *msg, SrsNaluSample *sample, int fu_payload_size, std::vector<SrsRtpPacket *> &pkts);
};

#endif
