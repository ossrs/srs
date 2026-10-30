//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//
#ifndef SRS_UTEST_AI_RTC_AVSYNC_HPP
#define SRS_UTEST_AI_RTC_AVSYNC_HPP

#include <srs_utest.hpp>

#include <string>
#include <vector>

#ifdef SRS_FFMPEG_FIT
// Encode n frames of a 440Hz tone as AAC-LC, 44.1kHz stereo, each frame with
// its ADTS header, as SRS feeds the transcoder from RTMP.
void encode_aac_adts_frames(int n, std::vector<std::string> &frames);
#endif

#endif
