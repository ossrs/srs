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

#ifndef SRS_KERNEL_RTP_HPP
#define SRS_KERNEL_RTP_HPP

#include <srs_core.hpp>

#include <string>

const uint8_t kMarker = 0x80;

class SrsRtpSharedPacket
{
private:
     class SrsRtpSharedPacketPayload 
     {   
     public:
         char* payload;
         int size;
         int shared_count;
     public:
         SrsRtpSharedPacketPayload();
         virtual ~SrsRtpSharedPacketPayload();
     };  
private:
     SrsRtpSharedPacketPayload* payload_ptr;
public:
     char* payload;
     int size;
public:
     int64_t timestamp;
     uint16_t sequence;
     uint32_t ssrc;
     uint16_t payload_type;
public:
    SrsRtpSharedPacket();
    virtual ~SrsRtpSharedPacket();
public:
    srs_error_t create(int64_t t, uint16_t seq, uint32_t sc, uint16_t pt, char* p, int s);
    SrsRtpSharedPacket* copy();
// interface to modify rtp header
public:
    srs_error_t set_marker(bool marker);
};

#endif
