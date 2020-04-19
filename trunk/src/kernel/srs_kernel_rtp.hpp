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

#include <srs_kernel_buffer.hpp>
#include <srs_kernel_codec.hpp>

#include <string>

const int kRtpHeaderFixedSize = 12;
const uint8_t kRtpMarker = 0x80;

// H.264 nalu header type mask.
const uint8_t kNalTypeMask      = 0x1F;

class SrsBuffer;
class SrsRtpRawPayload;
class SrsRtpFUAPayload2;

class SrsRtpHeader
{
private:
    bool padding;
    bool extension;
    uint8_t cc;
    bool marker;
    uint8_t payload_type;
    uint16_t sequence;
    int32_t timestamp;
    uint32_t ssrc;
    uint32_t csrc[15];
    uint16_t extension_length;
    // TODO:extension field.
public:
    SrsRtpHeader();
    virtual ~SrsRtpHeader();
    void reset();
public:
    srs_error_t decode(SrsBuffer* stream);
    srs_error_t encode(SrsBuffer* stream);
public:
    size_t header_size();
public:
    inline void set_marker(bool v) { marker = v; }
    bool get_marker() const { return marker; }
    inline void set_payload_type(uint8_t v) { payload_type = v; }
    uint8_t get_payload_type() const { return payload_type; }
    inline void set_sequence(uint16_t v) { sequence = v; }
    uint16_t get_sequence() const  { return sequence; }
    inline void set_timestamp(int64_t v) { timestamp = (uint32_t)v; }
    int64_t get_timestamp() const { return timestamp; }
    inline void set_ssrc(uint32_t v) { ssrc = v; }
    uint32_t get_ssrc() const { return ssrc; }
    inline void set_padding(bool v) { padding = v; }
};

class SrsRtpPacket2
{
public:
    SrsRtpHeader rtp_header;
    ISrsEncoder* payload;
    int padding;
private:
    SrsRtpRawPayload* cache_raw;
    SrsRtpFUAPayload2* cache_fua;
    int cache_payload;
public:
    SrsRtpPacket2();
    virtual ~SrsRtpPacket2();
public:
    // Set the padding of RTP packet.
    void set_padding(int size);
    // Increase the padding of RTP packet.
    void add_padding(int size);
    // Reset RTP packet.
    void reset();
    // Reuse the cached raw message as payload.
    SrsRtpRawPayload* reuse_raw();
    // Reuse the cached fua message as payload.
    SrsRtpFUAPayload2* reuse_fua();
// interface ISrsEncoder
public:
    virtual int nb_bytes();
    virtual srs_error_t encode(SrsBuffer* buf);
};

// Single payload data.
class SrsRtpRawPayload : public ISrsEncoder
{
public:
    // The RAW payload, directly point to the shared memory.
    // @remark We only refer to the memory, user must free its bytes.
    char* payload;
    int nn_payload;
public:
    SrsRtpRawPayload();
    virtual ~SrsRtpRawPayload();
// interface ISrsEncoder
public:
    virtual int nb_bytes();
    virtual srs_error_t encode(SrsBuffer* buf);
};

// Multiple NALUs, automatically insert 001 between NALUs.
class SrsRtpRawNALUs : public ISrsEncoder
{
private:
    // We will manage the samples, but the sample itself point to the shared memory.
    std::vector<SrsSample*> nalus;
    int nn_bytes;
    int cursor;
public:
    SrsRtpRawNALUs();
    virtual ~SrsRtpRawNALUs();
public:
    void push_back(SrsSample* sample);
public:
    uint8_t skip_first_byte();
    // We will manage the returned samples, if user want to manage it, please copy it.
    srs_error_t read_samples(std::vector<SrsSample*>& samples, int packet_size);
// interface ISrsEncoder
public:
    virtual int nb_bytes();
    virtual srs_error_t encode(SrsBuffer* buf);
};

// STAP-A, for multiple NALUs.
class SrsRtpSTAPPayload : public ISrsEncoder
{
public:
    // The NRI in NALU type.
    SrsAvcNaluType nri;
    // The NALU samples, we will manage the samples.
    // @remark We only refer to the memory, user must free its bytes.
    std::vector<SrsSample*> nalus;
public:
    SrsRtpSTAPPayload();
    virtual ~SrsRtpSTAPPayload();
// interface ISrsEncoder
public:
    virtual int nb_bytes();
    virtual srs_error_t encode(SrsBuffer* buf);
};

// FU-A, for one NALU with multiple fragments.
// With more than one payload.
class SrsRtpFUAPayload : public ISrsEncoder
{
public:
    // The NRI in NALU type.
    SrsAvcNaluType nri;
    // The FUA header.
    bool start;
    bool end;
    SrsAvcNaluType nalu_type;
    // The NALU samples, we manage the samples.
    // @remark We only refer to the memory, user must free its bytes.
    std::vector<SrsSample*> nalus;
public:
    SrsRtpFUAPayload();
    virtual ~SrsRtpFUAPayload();
// interface ISrsEncoder
public:
    virtual int nb_bytes();
    virtual srs_error_t encode(SrsBuffer* buf);
};

// FU-A, for one NALU with multiple fragments.
// With only one payload.
class SrsRtpFUAPayload2 : public ISrsEncoder
{
public:
    // The NRI in NALU type.
    SrsAvcNaluType nri;
    // The FUA header.
    bool start;
    bool end;
    SrsAvcNaluType nalu_type;
    // The payload and size,
    char* payload;
    int size;
public:
    SrsRtpFUAPayload2();
    virtual ~SrsRtpFUAPayload2();
// interface ISrsEncoder
public:
    virtual int nb_bytes();
    virtual srs_error_t encode(SrsBuffer* buf);
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
