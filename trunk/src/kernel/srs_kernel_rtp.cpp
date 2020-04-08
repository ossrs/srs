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

#include <srs_kernel_rtp.hpp>

#include <fcntl.h>
#include <sstream>
using namespace std;

#include <srs_kernel_log.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_buffer.hpp>
#include <srs_kernel_utility.hpp>


SrsRtpHeader::SrsRtpHeader()
{
    padding          = false;
    extension        = false;
    cc               = 0;
    marker           = false;
    payload_type     = 0;
    sequence         = 0;
    timestamp        = 0;
    ssrc             = 0;
    extension_length = 0;
}

SrsRtpHeader::SrsRtpHeader(const SrsRtpHeader& rhs)
{
    operator=(rhs);
}

SrsRtpHeader& SrsRtpHeader::operator=(const SrsRtpHeader& rhs)
{
    padding          = rhs.padding;
    extension        = rhs.extension;
    cc               = rhs.cc;
    marker           = rhs.marker;
    payload_type     = rhs.payload_type;
    sequence         = rhs.sequence;
    timestamp        = rhs.timestamp;
    ssrc             = rhs.ssrc;
    for (size_t i = 0; i < cc; ++i) {
        csrc[i] = rhs.csrc[i];
    }
    extension_length = rhs.extension_length;

    return *this;
}

SrsRtpHeader::~SrsRtpHeader()
{
}

srs_error_t SrsRtpHeader::decode(SrsBuffer* stream)
{
    srs_error_t err = srs_success;

    // TODO:

    return err;
}

srs_error_t SrsRtpHeader::encode(SrsBuffer* stream)
{
    srs_error_t err = srs_success;

    uint8_t first = 0x80 | cc;
    if (padding) {
        first |= 0x40;
    }
    if (extension) {
        first |= 0x10;
    }
    stream->write_1bytes(first);
    uint8_t second = payload_type;
    if (marker) {
        payload_type |= kRtpMarker;
    }
    stream->write_1bytes(second);
    stream->write_2bytes(sequence);
    stream->write_4bytes(timestamp);
    stream->write_4bytes(ssrc);
    for (size_t i = 0; i < cc; ++i) {
        stream->write_4bytes(csrc[i]);
    }

    // TODO: Write exteinsion field.
    if (extension) {
    }

    return err;
}

size_t SrsRtpHeader::header_size()
{
    return kRtpHeaderFixedSize + cc * 4 + (extension ? (extension_length + 1) * 4 : 0);
}

void SrsRtpHeader::set_marker(bool marker)
{
    this->marker = marker;
}

void SrsRtpHeader::set_payload_type(uint8_t payload_type)
{
    this->payload_type = payload_type;
}

void SrsRtpHeader::set_sequence(uint16_t sequence)
{
    this->sequence = sequence;
}

void SrsRtpHeader::set_timestamp(int64_t timestamp)
{
    this->timestamp = timestamp;
}

void SrsRtpHeader::set_ssrc(uint32_t ssrc)
{
    this->ssrc = ssrc;
}

SrsRtpSharedPacket::SrsRtpSharedPacketPayload::SrsRtpSharedPacketPayload()
{
    payload = NULL;
    size = 0;
    shared_count = 0;
}

SrsRtpSharedPacket::SrsRtpSharedPacketPayload::~SrsRtpSharedPacketPayload()
{
    srs_freepa(payload);
}

SrsRtpSharedPacket::SrsRtpSharedPacket()
{
    payload_ptr = NULL;

    payload = NULL;
    size = 0;
}

SrsRtpSharedPacket::~SrsRtpSharedPacket()
{
    if (payload_ptr) {
        if (payload_ptr->shared_count == 0) {
            srs_freep(payload_ptr);
        } else {
            --payload_ptr->shared_count;
        }
    }
}

srs_error_t SrsRtpSharedPacket::create(int64_t timestamp, uint16_t sequence, uint32_t ssrc, uint16_t payload_type, char* p, int s)
{
    srs_error_t err = srs_success;

    if (s < 0) {
        return srs_error_new(ERROR_RTP_PACKET_CREATE, "create packet size=%d", s);
    }   

    srs_assert(!payload_ptr);

    rtp_header.set_timestamp(timestamp);
    rtp_header.set_sequence(sequence);
    rtp_header.set_ssrc(ssrc);
    rtp_header.set_payload_type(payload_type);

    // TODO: rtp header padding.
    size_t buffer_size = rtp_header.header_size() + s;
    
    char* buffer = new char[buffer_size];
    SrsBuffer stream(buffer, buffer_size);
    if ((err = rtp_header.encode(&stream)) != srs_success) {
        srs_freepa(buffer);
        return srs_error_wrap(err, "rtp header encode");
    }

    stream.write_bytes(p, s);
    payload_ptr = new SrsRtpSharedPacketPayload();
    payload_ptr->payload = buffer;
    payload_ptr->size = buffer_size;

    this->payload = payload_ptr->payload;
    this->size = payload_ptr->size;

    return err;
}

SrsRtpSharedPacket* SrsRtpSharedPacket::copy()
{
    SrsRtpSharedPacket* copy = new SrsRtpSharedPacket();

    copy->payload_ptr = payload_ptr;
    payload_ptr->shared_count++;

    copy->rtp_header = rtp_header;

    copy->payload = payload;
    copy->size = size;

    return copy;
}

srs_error_t SrsRtpSharedPacket::modify_rtp_header_marker(bool marker)
{
    srs_error_t err = srs_success;
    if (payload_ptr == NULL || payload_ptr->payload == NULL || payload_ptr->size < kRtpHeaderFixedSize) {
        return srs_error_new(ERROR_RTC_RTP_MUXER, "rtp payload incorrect");
    }

    rtp_header.set_marker(marker);
    if (marker) {
        payload_ptr->payload[1] |= kRtpMarker;
    } else {
        payload_ptr->payload[1] &= (~kRtpMarker);
    }

    return err;
}

srs_error_t SrsRtpSharedPacket::modify_rtp_header_ssrc(uint32_t ssrc)
{
    srs_error_t err = srs_success;

    if (payload_ptr == NULL || payload_ptr->payload == NULL || payload_ptr->size < kRtpHeaderFixedSize) {
        return srs_error_new(ERROR_RTC_RTP_MUXER, "rtp payload incorrect");
    }

    rtp_header.set_ssrc(ssrc);

    SrsBuffer stream(payload_ptr->payload + 8, 4);
    stream.write_4bytes(ssrc);

    return err;
}

srs_error_t SrsRtpSharedPacket::modify_rtp_header_payload_type(uint8_t payload_type)
{
    srs_error_t err = srs_success;

    if (payload_ptr == NULL || payload_ptr->payload == NULL || payload_ptr->size < kRtpHeaderFixedSize) {
        return srs_error_new(ERROR_RTC_RTP_MUXER, "rtp payload incorrect");
    }

    rtp_header.set_payload_type(payload_type);
    payload_ptr->payload[1] = (payload_ptr->payload[1] & 0x80) | payload_type;

    return err;
}
