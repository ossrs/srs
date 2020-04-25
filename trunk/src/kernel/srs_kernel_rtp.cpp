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
    padding_length   = 0;
    extension        = false;
    cc               = 0;
    marker           = false;
    payload_type     = 0;
    sequence         = 0;
    timestamp        = 0;
    ssrc             = 0;
    extension_length = 0;
}

void SrsRtpHeader::reset()
{
    // We only reset the optional fields, the required field such as ssrc
    // will always be set by user.
    padding          = false;
    extension        = false;
    cc               = 0;
    marker           = false;
    extension_length = 0;
}

SrsRtpHeader::~SrsRtpHeader()
{
}

srs_error_t SrsRtpHeader::decode(SrsBuffer* stream)
{
    srs_error_t err = srs_success;

    if (stream->size() < kRtpHeaderFixedSize) {
        return srs_error_new(ERROR_RTC_RTP_MUXER, "rtp payload incorrect");
    }

    /*   
      0                   1                   2                   3
      0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     |V=2|P|X|  CC   |M|     PT      |       sequence number         |
     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     |                           timestamp                           |
     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     |           synchronization source (SSRC) identifier            |
     +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
     |            contributing source (CSRC) identifiers             |
     |                             ....                              |
     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    */

    uint8_t first = stream->read_1bytes();
    padding = (first & 0x20);
    extension = (first & 0x10);
    cc = (first & 0x0F);

    uint8_t second = stream->read_1bytes();
    marker = (second & 0x80);
    payload_type = (second & 0x7F);

    sequence = stream->read_2bytes();
    timestamp = stream->read_4bytes();
    ssrc = stream->read_4bytes();

    if ((size_t)stream->size() < header_size()) {
        return srs_error_new(ERROR_RTC_RTP_MUXER, "rtp payload incorrect");
    }

    for (uint8_t i = 0; i < cc; ++i) {
        csrc[i] = stream->read_4bytes();
    }    

    if (extension) {
        // TODO:
        uint16_t profile_id = stream->read_2bytes();
        extension_length = stream->read_2bytes();
        // @see: https://tools.ietf.org/html/rfc3550#section-5.3.1
        stream->skip(extension_length * 4);

        srs_verbose("extension, profile_id=%u, length=%u", profile_id, extension_length);

        // @see: https://tools.ietf.org/html/rfc5285#section-4.2
        if (profile_id == 0xBEDE) {
        }    
    }

    if (padding) {
        padding_length = *(reinterpret_cast<uint8_t*>(stream->data() + stream->size() - 1));
        if (padding_length > (stream->size() - stream->pos())) {
            return srs_error_new(ERROR_RTC_RTP_MUXER, "rtp payload incorrect");
        }

        srs_verbose("offset=%d, padding_length=%u", stream->size(), padding_length);
    }

    return err;
}

srs_error_t SrsRtpHeader::encode(SrsBuffer* stream)
{
    srs_error_t err = srs_success;

    // Encode the RTP fix header, 12bytes.
    // @see https://tools.ietf.org/html/rfc1889#section-5.1
    char* op = stream->head();
    char* p = op;

    // The version, padding, extension and cc, total 1 byte.
    uint8_t v = 0x80 | cc;
    if (padding) {
        v |= 0x20;
    }
    if (extension) {
        v |= 0x10;
    }
    *p++ = v;

    // The marker and payload type, total 1 byte.
    v = payload_type;
    if (marker) {
        v |= kRtpMarker;
    }
    *p++ = v;

    // The sequence number, 2 bytes.
    char* pp = (char*)&sequence;
    *p++ = pp[1];
    *p++ = pp[0];

    // The timestamp, 4 bytes.
    pp = (char*)&timestamp;
    *p++ = pp[3];
    *p++ = pp[2];
    *p++ = pp[1];
    *p++ = pp[0];

    // The SSRC, 4 bytes.
    pp = (char*)&ssrc;
    *p++ = pp[3];
    *p++ = pp[2];
    *p++ = pp[1];
    *p++ = pp[0];

    // The CSRC list: 0 to 15 items, each is 4 bytes.
    for (size_t i = 0; i < cc; ++i) {
        pp = (char*)&csrc[i];
        *p++ = pp[3];
        *p++ = pp[2];
        *p++ = pp[1];
        *p++ = pp[0];
    }

    // TODO: Write exteinsion field.
    if (extension) {
    }

    // Consume the data.
    stream->skip(p - op);

    return err;
}

size_t SrsRtpHeader::header_size()
{
    return kRtpHeaderFixedSize + cc * 4 + (extension ? (extension_length + 1) * 4 : 0);
}

SrsRtpPacket2::SrsRtpPacket2()
{
    payload = NULL;
    padding = 0;

    cache_raw = new SrsRtpRawPayload();
    cache_fua = new SrsRtpFUAPayload2();
    cache_payload = 0;
}

SrsRtpPacket2::~SrsRtpPacket2()
{
    // We may use the cache as payload.
    if (payload == cache_raw || payload == cache_fua) {
        payload = NULL;
    }

    srs_freep(payload);
    srs_freep(cache_raw);
    srs_freep(cache_fua);
}

void SrsRtpPacket2::set_padding(int size)
{
    rtp_header.set_padding(size > 0);
    rtp_header.set_padding_length(size);
    if (cache_payload) {
        cache_payload += size - padding;
    }
    padding = size;
}

void SrsRtpPacket2::add_padding(int size)
{
    rtp_header.set_padding(padding + size > 0);
    rtp_header.set_padding_length(rtp_header.get_padding_length() + size);
    if (cache_payload) {
        cache_payload += size;
    }
    padding += size;
}

void SrsRtpPacket2::reset()
{
    rtp_header.reset();
    padding = 0;
    cache_payload = 0;

    // We may use the cache as payload.
    if (payload == cache_raw || payload == cache_fua) {
        payload = NULL;
    }
    srs_freep(payload);
}

SrsRtpRawPayload* SrsRtpPacket2::reuse_raw()
{
    payload = cache_raw;
    return cache_raw;
}

SrsRtpFUAPayload2* SrsRtpPacket2::reuse_fua()
{
    payload = cache_fua;
    return cache_fua;
}

int SrsRtpPacket2::nb_bytes()
{
    if (!cache_payload) {
        cache_payload = rtp_header.header_size() + (payload? payload->nb_bytes():0) + padding;
    }
    return cache_payload;
}

srs_error_t SrsRtpPacket2::encode(SrsBuffer* buf)
{
    srs_error_t err = srs_success;

    if ((err = rtp_header.encode(buf)) != srs_success) {
        return srs_error_wrap(err, "rtp header");
    }

    if (payload && (err = payload->encode(buf)) != srs_success) {
        return srs_error_wrap(err, "encode payload");
    }

    if (padding > 0) {
        if (!buf->require(padding)) {
            return srs_error_new(ERROR_RTC_RTP_MUXER, "requires %d bytes", padding);
        }
        memset(buf->data() + buf->pos(), padding, padding);
        buf->skip(padding);
    }

    return err;
}

SrsRtpRawPayload::SrsRtpRawPayload()
{
    payload = NULL;
    nn_payload = 0;
}

SrsRtpRawPayload::~SrsRtpRawPayload()
{
}

int SrsRtpRawPayload::nb_bytes()
{
    return nn_payload;
}

srs_error_t SrsRtpRawPayload::encode(SrsBuffer* buf)
{
    if (nn_payload <= 0) {
        return srs_success;
    }

    if (!buf->require(nn_payload)) {
        return srs_error_new(ERROR_RTC_RTP_MUXER, "requires %d bytes", nn_payload);
    }

    buf->write_bytes(payload, nn_payload);

    return srs_success;
}

SrsRtpRawNALUs::SrsRtpRawNALUs()
{
    cursor = 0;
    nn_bytes = 0;
}

SrsRtpRawNALUs::~SrsRtpRawNALUs()
{
    if (true) {
        int nn_nalus = (int)nalus.size();
        for (int i = 0; i < nn_nalus; i++) {
            SrsSample* p = nalus[i];
            srs_freep(p);
        }
    }
}

void SrsRtpRawNALUs::push_back(SrsSample* sample)
{
    if (sample->size <= 0) {
        return;
    }

    if (!nalus.empty()) {
        SrsSample* p = new SrsSample();
        p->bytes = (char*)"\0\0\1";
        p->size = 3;
        nn_bytes += 3;
        nalus.push_back(p);
    }

    nn_bytes += sample->size;
    nalus.push_back(sample);
}

uint8_t SrsRtpRawNALUs::skip_first_byte()
{
    srs_assert (cursor >= 0 && nn_bytes > 0 && cursor < nn_bytes);
    cursor++;
    return uint8_t(nalus[0]->bytes[0]);
}

srs_error_t SrsRtpRawNALUs::read_samples(vector<SrsSample*>& samples, int packet_size)
{
    if (cursor + packet_size < 0 || cursor + packet_size > nn_bytes) {
        return srs_error_new(ERROR_RTC_RTP_MUXER, "cursor=%d, max=%d, size=%d", cursor, nn_bytes, packet_size);
    }

    int pos = cursor;
    cursor += packet_size;
    int left = packet_size;

    int nn_nalus = (int)nalus.size();
    for (int i = 0; left > 0 && i < nn_nalus; i++) {
        SrsSample* p = nalus[i];

        // Ignore previous consumed samples.
        if (pos && pos - p->size >= 0) {
            pos -= p->size;
            continue;
        }

        // Now, we are working at the sample.
        int nn = srs_min(left, p->size - pos);
        srs_assert(nn > 0);

        SrsSample* sample = new SrsSample();
        samples.push_back(sample);

        sample->bytes = p->bytes + pos;
        sample->size = nn;

        left -= nn;
        pos = 0;
    }

    return srs_success;
}

int SrsRtpRawNALUs::nb_bytes()
{
    int size = 0;

    int nn_nalus = (int)nalus.size();
    for (int i = 0; i < nn_nalus; i++) {
        SrsSample* p = nalus[i];
        size += p->size;
    }

    return size;
}

srs_error_t SrsRtpRawNALUs::encode(SrsBuffer* buf)
{
    int nn_nalus = (int)nalus.size();
    for (int i = 0; i < nn_nalus; i++) {
        SrsSample* p = nalus[i];

        if (!buf->require(p->size)) {
            return srs_error_new(ERROR_RTC_RTP_MUXER, "requires %d bytes", p->size);
        }

        buf->write_bytes(p->bytes, p->size);
    }

    return srs_success;
}

SrsRtpSTAPPayload::SrsRtpSTAPPayload()
{
    nri = (SrsAvcNaluType)0;
}

SrsRtpSTAPPayload::~SrsRtpSTAPPayload()
{
    int nn_nalus = (int)nalus.size();
    for (int i = 0; i < nn_nalus; i++) {
        SrsSample* p = nalus[i];
        srs_freep(p);
    }
}

int SrsRtpSTAPPayload::nb_bytes()
{
    int size = 1;

    int nn_nalus = (int)nalus.size();
    for (int i = 0; i < nn_nalus; i++) {
        SrsSample* p = nalus[i];
        size += 2 + p->size;
    }

    return size;
}

srs_error_t SrsRtpSTAPPayload::encode(SrsBuffer* buf)
{
    if (!buf->require(1)) {
        return srs_error_new(ERROR_RTC_RTP_MUXER, "requires %d bytes", 1);
    }

    // STAP header, RTP payload format for aggregation packets
    // @see https://tools.ietf.org/html/rfc6184#section-5.7
    uint8_t v = kStapA;
    v |= (nri & (~kNalTypeMask));
    buf->write_1bytes(v);

    // NALUs.
    int nn_nalus = (int)nalus.size();
    for (int i = 0; i < nn_nalus; i++) {
        SrsSample* p = nalus[i];

        if (!buf->require(2 + p->size)) {
            return srs_error_new(ERROR_RTC_RTP_MUXER, "requires %d bytes", 2 + p->size);
        }

        buf->write_2bytes(p->size);
        buf->write_bytes(p->bytes, p->size);
    }

    return srs_success;
}

SrsRtpFUAPayload::SrsRtpFUAPayload()
{
    start = end = false;
    nri = nalu_type = (SrsAvcNaluType)0;
}

SrsRtpFUAPayload::~SrsRtpFUAPayload()
{
    int nn_nalus = (int)nalus.size();
    for (int i = 0; i < nn_nalus; i++) {
        SrsSample* p = nalus[i];
        srs_freep(p);
    }
}

int SrsRtpFUAPayload::nb_bytes()
{
    int size = 2;

    int nn_nalus = (int)nalus.size();
    for (int i = 0; i < nn_nalus; i++) {
        SrsSample* p = nalus[i];
        size += p->size;
    }

    return size;
}

srs_error_t SrsRtpFUAPayload::encode(SrsBuffer* buf)
{
    if (!buf->require(2)) {
        return srs_error_new(ERROR_RTC_RTP_MUXER, "requires %d bytes", 1);
    }

    // FU indicator, @see https://tools.ietf.org/html/rfc6184#section-5.8
    uint8_t fu_indicate = kFuA;
    fu_indicate |= (nri & (~kNalTypeMask));
    buf->write_1bytes(fu_indicate);

    // FU header, @see https://tools.ietf.org/html/rfc6184#section-5.8
    uint8_t fu_header = nalu_type;
    if (start) {
        fu_header |= kStart;
    }
    if (end) {
        fu_header |= kEnd;
    }
    buf->write_1bytes(fu_header);

    // FU payload, @see https://tools.ietf.org/html/rfc6184#section-5.8
    int nn_nalus = (int)nalus.size();
    for (int i = 0; i < nn_nalus; i++) {
        SrsSample* p = nalus[i];

        if (!buf->require(p->size)) {
            return srs_error_new(ERROR_RTC_RTP_MUXER, "requires %d bytes", p->size);
        }

        buf->write_bytes(p->bytes, p->size);
    }

    return srs_success;
}

SrsRtpFUAPayload2::SrsRtpFUAPayload2()
{
    start = end = false;
    nri = nalu_type = (SrsAvcNaluType)0;

    payload = NULL;
    size = 0;
}

SrsRtpFUAPayload2::~SrsRtpFUAPayload2()
{
}

int SrsRtpFUAPayload2::nb_bytes()
{
    return 2 + size;
}

srs_error_t SrsRtpFUAPayload2::encode(SrsBuffer* buf)
{
    if (!buf->require(2 + size)) {
        return srs_error_new(ERROR_RTC_RTP_MUXER, "requires %d bytes", 1);
    }

    // Fast encoding.
    char* p = buf->head();

    // FU indicator, @see https://tools.ietf.org/html/rfc6184#section-5.8
    uint8_t fu_indicate = kFuA;
    fu_indicate |= (nri & (~kNalTypeMask));
    *p++ = fu_indicate;

    // FU header, @see https://tools.ietf.org/html/rfc6184#section-5.8
    uint8_t fu_header = nalu_type;
    if (start) {
        fu_header |= kStart;
    }
    if (end) {
        fu_header |= kEnd;
    }
    *p++ = fu_header;

    // FU payload, @see https://tools.ietf.org/html/rfc6184#section-5.8
    memcpy(p, payload, size);

    // Consume bytes.
    buf->skip(2 + size);

    return srs_success;
}

SrsRtpPayloadHeader::SrsRtpPayloadHeader()
{
    is_first_packet_of_frame = false;
    is_last_packet_of_frame = false;
    is_key_frame = false;
}

SrsRtpPayloadHeader::~SrsRtpPayloadHeader()
{
}

SrsRtpPayloadHeader::SrsRtpPayloadHeader(const SrsRtpPayloadHeader& rhs)
{
    operator=(rhs);
}

SrsRtpPayloadHeader& SrsRtpPayloadHeader::operator=(const SrsRtpPayloadHeader& rhs)
{
    is_first_packet_of_frame = rhs.is_first_packet_of_frame;
    is_last_packet_of_frame = rhs.is_last_packet_of_frame;

    return *this;
}

SrsRtpH264Header::SrsRtpH264Header() : SrsRtpPayloadHeader()
{
}

SrsRtpH264Header::~SrsRtpH264Header()
{
}

SrsRtpH264Header::SrsRtpH264Header(const SrsRtpH264Header& rhs)
{
    operator=(rhs);
}

SrsRtpH264Header& SrsRtpH264Header::operator=(const SrsRtpH264Header& rhs)
{
    SrsRtpPayloadHeader::operator=(rhs);
    nalu_type = rhs.nalu_type;
    nalu_header = rhs.nalu_header;
    nalu_offset = rhs.nalu_offset;

    return *this;
}

SrsRtpOpusHeader::SrsRtpOpusHeader() : SrsRtpPayloadHeader()
{
}

SrsRtpOpusHeader::~SrsRtpOpusHeader()
{
}

SrsRtpOpusHeader::SrsRtpOpusHeader(const SrsRtpOpusHeader& rhs)
{
    operator=(rhs);
}

SrsRtpOpusHeader& SrsRtpOpusHeader::operator=(const SrsRtpOpusHeader& rhs)
{
    SrsRtpPayloadHeader::operator=(rhs);
    return *this;
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

    rtp_payload_header = NULL;
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

    srs_freep(rtp_payload_header);
}

srs_error_t SrsRtpSharedPacket::create(SrsRtpHeader* rtp_h, SrsRtpPayloadHeader* rtp_ph, char* p, int s)
{
    srs_error_t err = srs_success;

    if (s < 0) {
        return srs_error_new(ERROR_RTP_PACKET_CREATE, "create packet size=%d", s);
    }   

    srs_assert(!payload_ptr);

    this->rtp_header = *rtp_h;
    this->rtp_payload_header = rtp_ph;

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

srs_error_t SrsRtpSharedPacket::decode(char* buf, int nb_buf)
{
    srs_error_t err = srs_success;

    SrsBuffer stream(buf, nb_buf);
    if ((err = rtp_header.decode(&stream)) != srs_success) {
        return srs_error_wrap(err, "rtp header decode failed");
    }

    payload_ptr = new SrsRtpSharedPacketPayload();
    payload_ptr->payload = buf;
    payload_ptr->size = nb_buf;

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
    if (dynamic_cast<SrsRtpH264Header*>(rtp_payload_header)) {
        copy->rtp_payload_header = new SrsRtpH264Header(*(dynamic_cast<SrsRtpH264Header*>(rtp_payload_header)));
    } else if (dynamic_cast<SrsRtpOpusHeader*>(rtp_payload_header)) {
        copy->rtp_payload_header = new SrsRtpOpusHeader(*(dynamic_cast<SrsRtpOpusHeader*>(rtp_payload_header)));
    }

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
