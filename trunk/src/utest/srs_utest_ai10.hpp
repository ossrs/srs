//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_UTEST_AI10_HPP
#define SRS_UTEST_AI10_HPP

/*
#include <srs_utest_ai10.hpp>
*/
#include <srs_utest.hpp>

#include <srs_app_rtc_codec.hpp>
#include <srs_app_rtc_conn.hpp>
#include <srs_app_rtc_source.hpp>
#include <srs_kernel_rtc_rtp.hpp>
#include <srs_protocol_amf0.hpp>
#include <srs_protocol_format.hpp>
#include <srs_protocol_rtmp_stack.hpp>
#include <srs_utest_ai09.hpp>

// Forward declarations
class SrsMediaPacket;
class SrsRtpPacket;

// Helper functions for creating test objects
SrsRtpPacket *create_test_rtp_packet(uint16_t seq, uint32_t ts, uint32_t ssrc, bool marker = false);
SrsRtcTrackDescription *create_test_track_description(std::string type, uint32_t ssrc);
SrsCodecPayload *create_test_codec_payload(uint8_t pt, std::string name, int sample);

#endif
