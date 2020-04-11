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

const int kRtpHeaderFixedSize = 12;
const uint8_t kRtpMarker = 0x80;

class SrsBuffer;

class SrsRtpHeader
{
private:
    bool padding;
    bool extension;
    uint8_t cc;
    bool marker;
    uint8_t payload_type;
    uint16_t sequence;
    int64_t timestamp;
    uint32_t ssrc;
    uint32_t csrc[15];
    uint16_t extension_length;
    // TODO:extension field.
public:
    SrsRtpHeader();
    virtual ~SrsRtpHeader();
    SrsRtpHeader(const SrsRtpHeader& rhs);
    SrsRtpHeader& operator=(const SrsRtpHeader& rhs);
public:
    srs_error_t decode(SrsBuffer* stream);
    srs_error_t encode(SrsBuffer* stream);
public:
    size_t header_size();
public:
    void set_marker(bool marker);
    bool get_marker() const { return marker; }
    void set_payload_type(uint8_t payload_type);
    uint8_t get_payload_type() const { return payload_type; }
    void set_sequence(uint16_t sequence);
    uint16_t get_sequence() const  { return sequence; }
    void set_timestamp(int64_t timestamp);
    int64_t get_timestamp() const { return timestamp; }
    void set_ssrc(uint32_t ssrc);
    uint32_t get_ssrc() const { return ssrc; }
};

class SrsRtpPacket2
{
public:
    SrsRtpHeader rtp_header;
    // @remark We only refer to the memory, user must free it.
    char* payload;
    int nn_payload;
public:
    SrsRtpPacket2();
    virtual ~SrsRtpPacket2();
public:
    virtual srs_error_t encode(SrsBuffer* stream);
};

class SrsRtpSharedPacket
{
private:
    class SrsRtpSharedPacketPayload 
    {   
    public:
        // Rtp packet buffer, include rtp header and payload.
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
    SrsRtpHeader rtp_header;
    char* payload;
    int size;
public:
    SrsRtpSharedPacket();
    virtual ~SrsRtpSharedPacket();
public:
    srs_error_t create(int64_t timestamp, uint16_t sequence, uint32_t ssrc, uint16_t payload_type, char* payload, int size);
    SrsRtpSharedPacket* copy();
// Interface to modify rtp header 
public:
    srs_error_t modify_rtp_header_marker(bool marker);
    srs_error_t modify_rtp_header_ssrc(uint32_t ssrc);
    srs_error_t modify_rtp_header_payload_type(uint8_t payload_type);
};

#endif
