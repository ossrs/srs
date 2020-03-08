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

#ifndef SRS_APP_RTP_HPP
#define SRS_APP_RTP_HPP

#include <srs_core.hpp>

#include <string>
#include <vector>
#include <map>

class SrsFormat;
class SrsSharedPtrMessage;
class SrsRequest;
class SrsOriginHub;

class SrsRtpRawFrame
{
public:
    int64_t timestamp;
    char* payload;
    int size;
public:
    SrsRtpRawFrame(char* buf, int len);
    virtual ~SrsRtpRawFrame();
public:
    srs_error_t avcc_to_annexb();
    srs_error_t frame_to_packet();
};

class SrsRtpMuxer
{
private:
    std::map<uint32_t, std::string> packet_queue;
public:
    SrsRtpMuxer();
    virtual ~SrsRtpMuxer();
public:
    srs_error_t video_frame_to_packet(SrsSharedPtrMessage* shared_video, SrsFormat* format);
    srs_error_t audio_frame_to_packet(SrsSharedPtrMessage* shared_video, SrsFormat* format);
};

class SrsRtp
{
private:
    SrsRequest* req;
    bool enabled;
    bool disposable;
    srs_utime_t last_update_time;
    SrsRtpMuxer* rtp_muxer;
    SrsOriginHub* hub;
public:
    SrsRtp();
    virtual ~SrsRtp();
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
