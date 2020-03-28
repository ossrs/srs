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

#ifndef SRS_APP_RTC_HPP
#define SRS_APP_RTC_HPP

#include <srs_core.hpp>

#include <string>
#include <vector>
#include <map>

class SrsFormat;
class SrsSample;
class SrsSharedPtrMessage;
class SrsRtpSharedPacket;
class SrsRequest;
class SrsOriginHub;
class SrsAudioRecode;
class SrsBuffer;

const int max_payload_size = 1200;
const int kRtpPacketSize = 1500;

const uint8_t kOpusPayloadType = 111;
const uint8_t kH264PayloadType = 102;

const uint8_t kNalTypeMask = 0x1F;

const uint8_t kStapA = 24;
const uint8_t kFuA   = 28;

const uint8_t kStart = 0x80;
const uint8_t kEnd   = 0x40;

const int kChannel      = 2;
const int kSamplerate   = 48000;
const int kArrayLength  = 8;
const int kArrayBuffer  = 4096;

// FIXME: ssrc can relate to source
const uint32_t kAudioSSRC = 3233846890;
const uint32_t kVideoSSRC = 3233846889;

// TODO: Define interface class like ISrsRtpMuxer to support SrsRtpOpusMuxer and so on.
class SrsRtpMuxer
{
private:
    uint16_t sequence;
    std::string sps;
    std::string pps;
public:
    bool discard_bframe;
public:
    SrsRtpMuxer();
    virtual ~SrsRtpMuxer();
public:
    srs_error_t frame_to_packet(SrsSharedPtrMessage* shared_video, SrsFormat* format);
private:
    srs_error_t packet_fu_a(SrsSharedPtrMessage* shared_frame, SrsFormat* format, SrsSample* sample, std::vector<SrsRtpSharedPacket*>& rtp_packet_vec);
    srs_error_t packet_single_nalu(SrsSharedPtrMessage* shared_frame, SrsFormat* format, SrsSample* sample, std::vector<SrsRtpSharedPacket*>& rtp_packet_vec);
    srs_error_t packet_stap_a(const std::string &sps, const std::string& pps, SrsSharedPtrMessage* shared_frame, std::vector<SrsRtpSharedPacket*>& rtp_packet_vec);
};

// TODO: FIXME: It's not a muxer, but a transcoder.
class SrsRtpOpusMuxer
{
private:
    // TODO: FIXME: How to handle timestamp overflow?
    uint32_t timestamp;
    uint16_t sequence;
    SrsAudioRecode* transcode;
public:
    SrsRtpOpusMuxer();
    virtual ~SrsRtpOpusMuxer();
    virtual srs_error_t initialize();
public:
    srs_error_t frame_to_packet(SrsSharedPtrMessage* shared_audio, SrsFormat* format, SrsBuffer* stream);
private:
    srs_error_t packet_opus(SrsSharedPtrMessage* shared_frame, SrsSample* sample, std::vector<SrsRtpSharedPacket*>& rtp_packet_vec);
};

class SrsRtc
{
private:
    SrsRequest* req;
    bool enabled;
    bool disposable;
    bool discard_aac;
    srs_utime_t last_update_time;
    SrsRtpMuxer* rtp_h264_muxer;
    SrsRtpOpusMuxer* rtp_opus_muxer;
    SrsOriginHub* hub;
public:
    SrsRtc();
    virtual ~SrsRtc();
public:
    virtual void dispose();
    virtual srs_error_t cycle();
public:
    virtual srs_error_t initialize(SrsOriginHub* h, SrsRequest* r);
    virtual srs_error_t on_publish();
    virtual void on_unpublish();
    virtual srs_error_t on_audio(SrsSharedPtrMessage* shared_audio, SrsFormat* format);
    virtual srs_error_t on_video(SrsSharedPtrMessage* shared_video, SrsFormat* format);
};

#endif
