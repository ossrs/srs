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

srs_error_t SrsRtpHeader::decode(SrsBuffer* buf)
{
    srs_error_t err = srs_success;

    if (!buf->require(kRtpHeaderFixedSize)) {
        return srs_error_new(ERROR_RTC_RTP_MUXER, "requires %d+ bytes", kRtpHeaderFixedSize);
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

    uint8_t first = buf->read_1bytes();
    padding = (first & 0x20);
    extension = (first & 0x10);
    cc = (first & 0x0F);

    uint8_t second = buf->read_1bytes();
    marker = (second & 0x80);
    payload_type = (second & 0x7F);

    sequence = buf->read_2bytes();
    timestamp = buf->read_4bytes();
    ssrc = buf->read_4bytes();

    int ext_bytes = nb_bytes() - kRtpHeaderFixedSize;
    if (!buf->require(ext_bytes)) {
        return srs_error_new(ERROR_RTC_RTP_MUXER, "requires %d+ bytes", ext_bytes);
    }

    for (uint8_t i = 0; i < cc; ++i) {
        csrc[i] = buf->read_4bytes();
    }    

    if (extension) {
        uint16_t profile_id = buf->read_2bytes();
        extension_length = buf->read_2bytes();

        // TODO: FIXME: Read extensions.
        // @see: https://tools.ietf.org/html/rfc3550#section-5.3.1
        buf->skip(extension_length * 4);

        // @see: https://tools.ietf.org/html/rfc5285#section-4.2
        if (profile_id == 0xBEDE) {
            // TODO: FIXME: Implements it.
        }    
    }

    if (padding && !buf->empty()) {
        padding_length = *(reinterpret_cast<uint8_t*>(buf->data() + buf->size() - 1));
        if (!buf->require(padding_length)) {
            return srs_error_new(ERROR_RTC_RTP_MUXER, "padding requires %d bytes", padding_length);
        }
    }

    return err;
}

srs_error_t SrsRtpHeader::encode(SrsBuffer* buf)
{
    srs_error_t err = srs_success;

    // Encode the RTP fix header, 12bytes.
    // @see https://tools.ietf.org/html/rfc1889#section-5.1
    char* op = buf->head();
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
    buf->skip(p - op);

    return err;
}

int SrsRtpHeader::nb_bytes()
{
    return kRtpHeaderFixedSize + cc * 4 + (extension ? (extension_length + 1) * 4 : 0);
}

void SrsRtpHeader::set_marker(bool v)
{
    marker = v;
}

bool SrsRtpHeader::get_marker() const
{
    return marker;
}

void SrsRtpHeader::set_payload_type(uint8_t v)
{
    payload_type = v;
}

uint8_t SrsRtpHeader::get_payload_type() const
{
    return payload_type;
}

void SrsRtpHeader::set_sequence(uint16_t v)
{
    sequence = v;
}

uint16_t SrsRtpHeader::get_sequence() const
{
    return sequence;
}

void SrsRtpHeader::set_timestamp(uint32_t v)
{
    timestamp = v;
}

uint32_t SrsRtpHeader::get_timestamp() const
{
    return timestamp;
}

void SrsRtpHeader::set_ssrc(uint32_t v)
{
    ssrc = v;
}

uint32_t SrsRtpHeader::get_ssrc() const
{
    return ssrc;
}

void SrsRtpHeader::set_padding(bool v)
{
    padding = v;
}

void SrsRtpHeader::set_padding_length(uint8_t v)
{
    padding_length = v;
}

uint8_t SrsRtpHeader::get_padding_length() const
{
    return padding_length;
}

ISrsRtpPacketDecodeHandler::ISrsRtpPacketDecodeHandler()
{
}

ISrsRtpPacketDecodeHandler::~ISrsRtpPacketDecodeHandler()
{
}

SrsRtpPacket2::SrsRtpPacket2()
{
    padding = 0;
    payload = NULL;
    decode_handler = NULL;

    nalu_type = SrsAvcNaluTypeReserved;
    original_bytes = NULL;

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

    srs_freepa(original_bytes);
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

void SrsRtpPacket2::set_decode_handler(ISrsRtpPacketDecodeHandler* h)
{
    decode_handler = h;
}

int SrsRtpPacket2::nb_bytes()
{
    if (!cache_payload) {
        cache_payload = rtp_header.nb_bytes() + (payload? payload->nb_bytes():0) + padding;
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
        return srs_error_wrap(err, "rtp payload");
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

srs_error_t SrsRtpPacket2::decode(SrsBuffer* buf)
{
    srs_error_t err = srs_success;

    if ((err = rtp_header.decode(buf)) != srs_success) {
        return srs_error_wrap(err, "rtp header");
    }

    // We must skip the padding bytes before parsing payload.
    padding = rtp_header.get_padding_length();
    if (!buf->require(padding)) {
        return srs_error_wrap(err, "requires padding %d bytes", padding);
    }
    buf->set_size(buf->size() - padding);

    // Try to parse the NALU type for video decoder.
    if (!buf->empty()) {
        nalu_type = SrsAvcNaluType((uint8_t)(buf->head()[0] & kNalTypeMask));
    }

    // If user set the decode handler, call it to set the payload.
    if (decode_handler) {
        decode_handler->on_before_decode_payload(this, buf, &payload);
    }

    // By default, we always use the RAW payload.
    if (!payload) {
        payload = reuse_raw();
    }

    if ((err = payload->decode(buf)) != srs_success) {
        return srs_error_wrap(err, "rtp payload");
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

srs_error_t SrsRtpRawPayload::decode(SrsBuffer* buf)
{
    if (buf->empty()) {
        return srs_success;
    }

    payload = buf->head();
    nn_payload = buf->left();

    return srs_success;
}

SrsRtpRawNALUs::SrsRtpRawNALUs()
{
    cursor = 0;
    nn_bytes = 0;
}

SrsRtpRawNALUs::~SrsRtpRawNALUs()
{
    int nn_nalus = (int)nalus.size();
    for (int i = 0; i < nn_nalus; i++) {
        SrsSample* p = nalus[i];
        srs_freep(p);
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

srs_error_t SrsRtpRawNALUs::decode(SrsBuffer* buf)
{
    if (buf->empty()) {
        return srs_success;
    }

    SrsSample* sample = new SrsSample();
    sample->bytes = buf->head();
    sample->size = buf->left();
    buf->skip(sample->size);

    nalus.push_back(sample);

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

SrsSample* SrsRtpSTAPPayload::get_sps()
{
    int nn_nalus = (int)nalus.size();
    for (int i = 0; i < nn_nalus; i++) {
        SrsSample* p = nalus[i];
        if (!p || !p->size) {
            continue;
        }

        SrsAvcNaluType nalu_type = (SrsAvcNaluType)(p->bytes[0] & kNalTypeMask);
        if (nalu_type == SrsAvcNaluTypeSPS) {
            return p;
        }
    }

    return NULL;
}

SrsSample* SrsRtpSTAPPayload::get_pps()
{
    int nn_nalus = (int)nalus.size();
    for (int i = 0; i < nn_nalus; i++) {
        SrsSample* p = nalus[i];
        if (!p || !p->size) {
            continue;
        }

        SrsAvcNaluType nalu_type = (SrsAvcNaluType)(p->bytes[0] & kNalTypeMask);
        if (nalu_type == SrsAvcNaluTypePPS) {
            return p;
        }
    }

    return NULL;
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

srs_error_t SrsRtpSTAPPayload::decode(SrsBuffer* buf)
{
    if (!buf->require(1)) {
        return srs_error_new(ERROR_RTC_RTP_MUXER, "requires %d bytes", 1);
    }

    // STAP header, RTP payload format for aggregation packets
    // @see https://tools.ietf.org/html/rfc6184#section-5.7
    uint8_t v = buf->read_1bytes();
    nri = SrsAvcNaluType(v & (~kNalTypeMask));

    // NALUs.
    while (!buf->empty()) {
        if (!buf->require(2)) {
            return srs_error_new(ERROR_RTC_RTP_MUXER, "requires %d bytes", 2);
        }

        int size = buf->read_2bytes();
        if (!buf->require(size)) {
            return srs_error_new(ERROR_RTC_RTP_MUXER, "requires %d bytes", size);
        }

        SrsSample* sample = new SrsSample();
        sample->bytes = buf->head();
        sample->size = size;
        buf->skip(size);

        nalus.push_back(sample);
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

srs_error_t SrsRtpFUAPayload::decode(SrsBuffer* buf)
{
    if (!buf->require(2)) {
        return srs_error_new(ERROR_RTC_RTP_MUXER, "requires %d bytes", 2);
    }

    // FU indicator, @see https://tools.ietf.org/html/rfc6184#section-5.8
    uint8_t v = buf->read_1bytes();
    nri = SrsAvcNaluType(v & (~kNalTypeMask));

    // FU header, @see https://tools.ietf.org/html/rfc6184#section-5.8
    v = buf->read_1bytes();
    start = v & kStart;
    end = v & kEnd;
    nalu_type = SrsAvcNaluType(v & kNalTypeMask);

    if (!buf->require(1)) {
        return srs_error_new(ERROR_RTC_RTP_MUXER, "requires %d bytes", 1);
    }

    SrsSample* sample = new SrsSample();
    sample->bytes = buf->head();
    sample->size = buf->left();
    buf->skip(sample->size);

    nalus.push_back(sample);

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

srs_error_t SrsRtpFUAPayload2::decode(SrsBuffer* buf)
{
    if (!buf->require(2)) {
        return srs_error_new(ERROR_RTC_RTP_MUXER, "requires %d bytes", 2);
    }

    // FU indicator, @see https://tools.ietf.org/html/rfc6184#section-5.8
    uint8_t v = buf->read_1bytes();
    nri = SrsAvcNaluType(v & (~kNalTypeMask));

    // FU header, @see https://tools.ietf.org/html/rfc6184#section-5.8
    v = buf->read_1bytes();
    start = v & kStart;
    end = v & kEnd;
    nalu_type = SrsAvcNaluType(v & kNalTypeMask);

    if (!buf->require(1)) {
        return srs_error_new(ERROR_RTC_RTP_MUXER, "requires %d bytes", 1);
    }

    payload = buf->head();
    size = buf->left();
    buf->skip(size);

    return srs_success;
}
