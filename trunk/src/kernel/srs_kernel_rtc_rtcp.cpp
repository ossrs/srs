/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2020 LiPeng
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

#include <srs_kernel_rtc_rtcp.hpp>

#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>

#include <arpa/inet.h>
using namespace std;

SrsRtcpCommon::SrsRtcpCommon()
{
}

SrsRtcpCommon::~SrsRtcpCommon()
{ 
}

const uint8_t SrsRtcpCommon::type() const
{
    return header_.type;
}

srs_error_t SrsRtcpCommon::decode_header(SrsBuffer *buffer)
{
    buffer->read_bytes((char*)(&header_), sizeof(SrsRtcpHeader));
    header_.length = ntohs(header_.length);

    return srs_success;
}

srs_error_t SrsRtcpCommon::encode_header(SrsBuffer *buffer)
{
    header_.length = htons(header_.length);
    buffer->write_bytes((char*)(&header_), sizeof(SrsRtcpHeader));

    return srs_success;
}

srs_error_t SrsRtcpCommon::decode(SrsBuffer *buffer)
{
    srs_error_t err = srs_success;

    if(srs_success != (err = decode_header(buffer))) {
        return srs_error_wrap(err, "decode header");
    }

    payload_len_ = (header_.length + 1) * 4 - sizeof(SrsRtcpHeader);
    buffer->read_bytes((char *)payload_, payload_len_);

    return err;
}

int SrsRtcpCommon::nb_bytes()
{
    return sizeof(SrsRtcpHeader) + payload_len_;
}

srs_error_t SrsRtcpCommon::encode(SrsBuffer *buffer)
{
    return srs_error_new(ERROR_RTC_RTCP, "not implement");
}

SrsRtcpApp::SrsRtcpApp():ssrc_(0)
{
}

SrsRtcpApp::~SrsRtcpApp()
{
}

const uint8_t SrsRtcpApp::type() const
{
    return SrsRtcpType_app;
}

const uint32_t SrsRtcpApp::get_ssrc() const
{
    return ssrc_;
}

const uint8_t SrsRtcpApp::get_subtype() const
{
    return header_.rc;
}

const string SrsRtcpApp::get_name() const
{
    return string((char*)name_);
}

const srs_error_t SrsRtcpApp::get_payload(uint8_t*& payload, int& len)
{
    len = payload_len_;
    payload = payload_;

    return srs_success;
}

srs_error_t SrsRtcpApp::set_subtype(uint8_t type)
{
    if(31 < type) {
        return srs_error_new(ERROR_RTC_RTCP, "invalid type: %d", type);
    }

    header_.rc = type;

    return srs_success;
}

srs_error_t SrsRtcpApp::set_name(std::string name)
{
    if(name.length() > 4) {
        return srs_error_new(ERROR_RTC_RTCP, "invalid name length %d", name.length());
    }

    memset(name_, 0, sizeof(name_));
    memcpy(name_, name.c_str(), name.length());

    return srs_success;
}

srs_error_t SrsRtcpApp::set_payload(uint8_t* payload, int len)
{
    if(len > (kRtcpPacketSize - 12)) {
        return srs_error_new(ERROR_RTC_RTCP, "invalid payload length %d", len);
    }

    payload_len_ = len;
    memcpy(payload_, payload, len);

    return srs_success;
}

void SrsRtcpApp::set_ssrc(uint32_t ssrc)
{
    ssrc_ = ssrc;
}

srs_error_t SrsRtcpApp::decode(SrsBuffer *buffer)
{
    srs_error_t err = srs_success;

    if(srs_success != (err = decode_header(buffer))) {
        return srs_error_wrap(err, "decode header");
    }

    ssrc_ = buffer->read_4bytes();
    buffer->read_bytes((char *)name_, sizeof(name_));

    // TODO: FIXME: Should check size?
    payload_len_ = (header_.length + 1) * 4 - sizeof(SrsRtcpHeader) - sizeof(name_) - sizeof(ssrc_);
    buffer->read_bytes((char *)payload_, payload_len_);

    return srs_success;
}

int SrsRtcpApp::nb_bytes()
{
    return sizeof(SrsRtcpHeader) + sizeof(ssrc_) + sizeof(name_) + payload_len_;
}

srs_error_t SrsRtcpApp::encode(SrsBuffer *buffer)
{
    srs_error_t err = srs_success;

    if(!buffer->require(nb_bytes())) {
        return srs_error_new(ERROR_RTC_RTCP, "requires %d bytes", nb_bytes());
    }

    if(srs_success != (err = encode_header(buffer))) {
        return srs_error_wrap(err, "encode header");
    }

    buffer->write_4bytes(ssrc_);
    buffer->write_bytes((char*)name_, sizeof(name_));
    buffer->write_bytes((char*)payload_, payload_len_);
    
    return srs_success;
}

SrsRtcpSR::SrsRtcpSR()
{
    header_.padding = 0;
    header_.type = SrsRtcpType_sr;
    header_.rc = 0;
    header_.version = kRtcpVersion;
    header_.length = 6;
}

SrsRtcpSR::~SrsRtcpSR()
{
}

const uint8_t SrsRtcpSR::get_rc() const
{
    return header_.rc;
}

const uint8_t SrsRtcpSR::type() const
{
    return SrsRtcpType_sr;
}

const uint32_t SrsRtcpSR::get_sender_ssrc() const
{
    return sender_ssrc_;
}

const uint64_t SrsRtcpSR::get_ntp() const
{
    return ntp_;
}

const uint32_t SrsRtcpSR::get_rtp_ts() const
{
    return rtp_ts_;
}

const uint32_t SrsRtcpSR::get_rtp_send_packets() const
{
    return send_rtp_packets_;
}

const uint32_t SrsRtcpSR::get_rtp_send_bytes() const
{
    return send_rtp_bytes_;
}

void SrsRtcpSR::set_sender_ssrc(uint32_t ssrc)
{
    sender_ssrc_ = ssrc;
}

void SrsRtcpSR::set_ntp(uint64_t ntp)
{
    ntp_ = ntp;
}

void SrsRtcpSR::set_rtp_ts(uint32_t ts)
{
    rtp_ts_ = ts;
}

void SrsRtcpSR::set_rtp_send_packets(uint32_t packets)
{
    send_rtp_packets_ = packets;
}

void SrsRtcpSR::set_rtp_send_bytes(uint32_t bytes)
{
    send_rtp_bytes_ = bytes;
}

srs_error_t SrsRtcpSR::decode(SrsBuffer *buffer)
{
    srs_error_t err = srs_success;

    if(srs_success != (err = decode_header(buffer))) {
        return srs_error_wrap(err, "decode header");
    }

    sender_ssrc_ = buffer->read_4bytes();
    ntp_ = buffer->read_8bytes();
    rtp_ts_ = buffer->read_4bytes();
    send_rtp_packets_ = buffer->read_4bytes();
    send_rtp_bytes_ = buffer->read_4bytes();

    if(header_.rc > 0) {
        char buf[1500];
        buffer->read_bytes(buf, header_.rc * 24);
    }

    return err;
}

int SrsRtcpSR::nb_bytes()
{
    return (header_.length + 1) * 4;
}

srs_error_t SrsRtcpSR::encode(SrsBuffer *buffer)
{
    srs_error_t err = srs_success;

    if(!buffer->require(nb_bytes())) {
        return srs_error_new(ERROR_RTC_RTCP, "requires %d bytes", nb_bytes());
    }

    if(srs_success != (err = encode_header(buffer))) {
        return srs_error_wrap(err, "encode header");
    }

    buffer->write_4bytes(sender_ssrc_);
    buffer->write_8bytes(ntp_);
    buffer->write_4bytes(rtp_ts_);
    buffer->write_4bytes(send_rtp_packets_);
    buffer->write_4bytes(send_rtp_bytes_);

    return err;
}

SrsRtcpRR::SrsRtcpRR(uint32_t sender_ssrc): sender_ssrc_(sender_ssrc)
{
    header_.padding = 0;
    header_.type = SrsRtcpType_rr;
    header_.rc = 0;
    header_.version = kRtcpVersion;
    header_.length = 7;
}

SrsRtcpRR::~SrsRtcpRR()
{
}

const uint8_t SrsRtcpRR::type() const
{
    return SrsRtcpType_rr;
}

const uint32_t SrsRtcpRR::get_rb_ssrc() const
{
    return rb_.ssrc;
}

const float SrsRtcpRR::get_lost_rate() const
{
    return rb_.fraction_lost / 256;
}

const uint32_t SrsRtcpRR::get_lost_packets() const
{
    return rb_.lost_packets;
}

const uint32_t SrsRtcpRR::get_highest_sn() const
{
    return rb_.highest_sn;
}

const uint32_t SrsRtcpRR::get_jitter() const
{
    return rb_.jitter;
}

const uint32_t SrsRtcpRR::get_lsr() const
{
    return rb_.lsr;
}

const uint32_t SrsRtcpRR::get_dlsr() const
{
    return rb_.dlsr;
}

void SrsRtcpRR::set_rb_ssrc(uint32_t ssrc)
{
    rb_.ssrc = ssrc;
}

void SrsRtcpRR::set_lost_rate(float rate)
{
    rb_.fraction_lost = rate * 256;
}

void SrsRtcpRR::set_lost_packets(uint32_t count)
{
    rb_.lost_packets = count;
}

void SrsRtcpRR::set_highest_sn(uint32_t sn)
{
    rb_.highest_sn = sn;
}

void SrsRtcpRR::set_jitter(uint32_t jitter)
{
    rb_.jitter = jitter;
}

void SrsRtcpRR::set_lsr(uint32_t lsr)
{
    rb_.lsr = lsr;
}

void SrsRtcpRR::set_dlsr(uint32_t dlsr)
{
    rb_.dlsr = dlsr;
}

void SrsRtcpRR::set_sender_ntp(uint64_t ntp)
{
    uint32_t lsr = (uint32_t)((ntp >> 16) & 0x00000000FFFFFFFF);
    rb_.lsr = lsr;
}

srs_error_t SrsRtcpRR::decode(SrsBuffer *buffer)
{
    srs_error_t err = srs_success;

    if(srs_success != (err = decode_header(buffer))) {
        return srs_error_wrap(err, "decode header");
    }

    sender_ssrc_ = buffer->read_4bytes();
    if(header_.rc < 1) {
        return err;
    }
    rb_.ssrc = buffer->read_4bytes();
    rb_.fraction_lost = buffer->read_1bytes();
    rb_.lost_packets = buffer->read_3bytes();
    rb_.highest_sn = buffer->read_4bytes();
    rb_.jitter = buffer->read_4bytes();
    rb_.lsr = buffer->read_4bytes();
    rb_.dlsr = buffer->read_4bytes();
    
    if(header_.rc > 1) {
        char buf[1500];
        buffer->read_bytes(buf, (header_.rc -1 ) * 24);
    }

    return err;
}

int SrsRtcpRR::nb_bytes()
{
    return (header_.length + 1) * 4;
}

srs_error_t SrsRtcpRR::encode(SrsBuffer *buffer)
{
    srs_error_t err = srs_success;

    if(!buffer->require(nb_bytes())) {
        return srs_error_new(ERROR_RTC_RTCP, "requires %d bytes", nb_bytes());
    }

    header_.rc = 1;
    if(srs_success != (err = encode_header(buffer))) {
        return srs_error_wrap(err, "encode header");
    }
    buffer->write_4bytes(sender_ssrc_);
    
    buffer->write_4bytes(rb_.ssrc);
    buffer->write_1bytes(rb_.fraction_lost);
    buffer->write_3bytes(rb_.lost_packets);
    buffer->write_4bytes(rb_.highest_sn);
    buffer->write_4bytes(rb_.jitter);
    buffer->write_4bytes(rb_.lsr);
    buffer->write_4bytes(rb_.dlsr);

    return err;
}

SrsRtcpTWCC::SrsRtcpTWCC(uint32_t sender_ssrc) : sender_ssrc_(sender_ssrc), pkt_len(0)
{
    header_.padding = 0;
    header_.type = SrsRtcpType_rtpfb;
    header_.rc = 15;
    header_.version = kRtcpVersion;
}
    
SrsRtcpTWCC::~SrsRtcpTWCC()
{
}

void SrsRtcpTWCC::clear()
{
    encoded_chucks_.clear();
    pkt_deltas_.clear();
    recv_packes_.clear();
    recv_sns_.clear();
}

const uint32_t SrsRtcpTWCC::get_media_ssrc() const
{
    return media_ssrc_;
}
const uint16_t SrsRtcpTWCC::get_base_sn() const
{
    return base_sn_;
}

const uint32_t SrsRtcpTWCC::get_reference_time() const
{
    return reference_time_;
}

const uint8_t SrsRtcpTWCC::get_feedback_count() const
{
    return fb_pkt_count_;
}

const uint16_t SrsRtcpTWCC::get_packet_status_count() const
{
    return packet_count_;
}
    
const vector<uint16_t> SrsRtcpTWCC::get_packet_chucks() const
{
    return encoded_chucks_;
}

const vector<uint16_t> SrsRtcpTWCC::get_recv_deltas() const
{
    return pkt_deltas_;
}

void SrsRtcpTWCC::set_media_ssrc(uint32_t ssrc)
{
    media_ssrc_ = ssrc;
}
void SrsRtcpTWCC::set_base_sn(uint16_t sn)
{
    base_sn_ = sn;
}

void SrsRtcpTWCC::set_packet_status_count(uint16_t count)
{
    packet_count_ = count;
}

void SrsRtcpTWCC::set_reference_time(uint32_t time)
{
    reference_time_ = time;
}

void SrsRtcpTWCC::set_feedback_count(uint8_t count)
{
    fb_pkt_count_ = count;
}
    
void SrsRtcpTWCC::add_packet_chuck(uint16_t chunk)
{
    encoded_chucks_.push_back(chunk);
}

void SrsRtcpTWCC::add_recv_delta(uint16_t delta)
{
    pkt_deltas_.push_back(delta);
}

srs_error_t SrsRtcpTWCC::recv_packet(uint16_t sn, srs_utime_t ts)
{
    map<uint16_t, srs_utime_t>::iterator it = recv_packes_.find(sn);
    if(it != recv_packes_.end()) {
        return srs_error_new(ERROR_RTC_RTCP, "TWCC dup seq: %d", sn);
    }

    recv_packes_[sn] = ts;
    recv_sns_.insert(sn);

    return srs_success;
}

srs_error_t SrsRtcpTWCC::decode(SrsBuffer *buffer)
{
    srs_error_t err = srs_success;
    
    return err;
}

int SrsRtcpTWCC::nb_bytes()
{
    return kRtcpPacketSize;
}

srs_utime_t SrsRtcpTWCC::calculate_delta_us(srs_utime_t ts, srs_utime_t last)
{
    int64_t divisor = kTwccFbReferenceTimeDivisor;
    int64_t delta_us = (ts - last) % divisor;

    if (delta_us > (divisor >> 1))
        delta_us -= divisor;

    delta_us += (delta_us < 0) ? (-kTwccFbDeltaUnit / 2) : (kTwccFbDeltaUnit / 2);
    delta_us /= kTwccFbDeltaUnit;

    return delta_us;
}

bool SrsRtcpTWCC::can_add_to_chunk(SrsRtcpTWCC::SrsRtcpTWCCChunk& chunk, int delta_size)
{
	srs_verbose("can_add %d chunk->size %u delta_sizes %d %d %d %d %d %d %d %d %d %d %d %d %d %d all_same %d has_large_delta %d",
	    delta_size, chunk.size, chunk.delta_sizes[0], chunk.delta_sizes[1], chunk.delta_sizes[2], chunk.delta_sizes[3],
	    chunk.delta_sizes[4], chunk.delta_sizes[5], chunk.delta_sizes[6], chunk.delta_sizes[7], chunk.delta_sizes[8],
	    chunk.delta_sizes[9], chunk.delta_sizes[10], chunk.delta_sizes[11], chunk.delta_sizes[12], chunk.delta_sizes[13],
	    (int)chunk.all_same, (int)chunk.has_large_delta);

    if (chunk.size < kTwccFbTwoBitElements) {
        return true;
    }

    if (chunk.size < kTwccFbOneBitElements && !chunk.has_large_delta && delta_size != kTwccFbLargeRecvDeltaBytes) {
        return true;
    }

    if (chunk.size < kTwccFbMaxRunLength && chunk.all_same && chunk.delta_sizes[0] == delta_size) {
        srs_verbose("< %d && all_same && delta_size[0] %d == %d", kTwccFbMaxRunLength, chunk.delta_sizes[0], delta_size);
        return true;
    }

    return false;
}

void SrsRtcpTWCC::add_to_chunk(SrsRtcpTWCC::SrsRtcpTWCCChunk& chunk, int delta_size)
{
    if (chunk.size < kTwccFbMaxBitElements) {
        chunk.delta_sizes[chunk.size] = delta_size;
    }

    chunk.size += 1;
    chunk.all_same = chunk.all_same && delta_size == chunk.delta_sizes[0];
    chunk.has_large_delta = chunk.has_large_delta || delta_size >= kTwccFbLargeRecvDeltaBytes;
}

srs_error_t SrsRtcpTWCC::encode_chunk_run_length(SrsRtcpTWCC::SrsRtcpTWCCChunk& chunk)
{
    if (!chunk.all_same || chunk.size > kTwccFbMaxRunLength) {
        return srs_error_new(ERROR_RTC_RTCP, "invalid run all_same:%d, size:%d", chunk.all_same, chunk.size);
    }

    uint16_t encoded_chunk = (chunk.delta_sizes[0] << 13) | chunk.size;

    encoded_chucks_.push_back(encoded_chunk);
    pkt_len += sizeof(encoded_chunk);

    return srs_success;
}

srs_error_t SrsRtcpTWCC::encode_chunk_one_bit(SrsRtcpTWCC::SrsRtcpTWCCChunk& chunk)
{
    int i = 0;
    if (chunk.has_large_delta) {
        return srs_error_new(ERROR_RTC_RTCP, "invalid large delta");
    }

    uint16_t encoded_chunk = 0x8000;
    for (i = 0; i < chunk.size; ++i) {
        encoded_chunk |= (chunk.delta_sizes[i] << (kTwccFbOneBitElements - 1 - i));
    }

    encoded_chucks_.push_back(encoded_chunk);
    pkt_len += sizeof(encoded_chunk);

    // 1 0 symbol_list
    return srs_success;
}
    
srs_error_t SrsRtcpTWCC::encode_chunk_two_bit(SrsRtcpTWCC::SrsRtcpTWCCChunk& chunk, size_t size, bool shift)
{
    unsigned int i = 0;
    uint8_t delta_size = 0;
    
    uint16_t encoded_chunk = 0xc000;
    // 1 1 symbol_list
    for (i = 0; i < size; ++i) {
        encoded_chunk |= (chunk.delta_sizes[i] << (2 * (kTwccFbTwoBitElements - 1 - i)));
    }
    encoded_chucks_.push_back(encoded_chunk);
    pkt_len += sizeof(encoded_chunk);

    if (shift) {
        chunk.size -= size;
        chunk.all_same = true;
        chunk.has_large_delta = false;
        for (i = 0; i < chunk.size; ++i) {
            delta_size = chunk.delta_sizes[i + size];
            chunk.delta_sizes[i] = delta_size;
            chunk.all_same = (chunk.all_same && delta_size == chunk.delta_sizes[0]);
            chunk.has_large_delta = chunk.has_large_delta || delta_size == kTwccFbLargeRecvDeltaBytes;
        }
    }

    return srs_success;
}

void SrsRtcpTWCC::reset_chunk(SrsRtcpTWCC::SrsRtcpTWCCChunk& chunk)
{
    chunk.size = 0;

    chunk.all_same = true;
    chunk.has_large_delta = false;
}

srs_error_t SrsRtcpTWCC::encode_chunk(SrsRtcpTWCC::SrsRtcpTWCCChunk& chunk)
{
    srs_error_t err = srs_success;

    if (can_add_to_chunk(chunk, 0) && can_add_to_chunk(chunk, 1) && can_add_to_chunk(chunk, 2)) {
        return srs_error_new(ERROR_RTC_RTCP, "TWCC chunk");
    }

    if (chunk.all_same) {
        if ((err = encode_chunk_run_length(chunk)) != srs_success) {
            return srs_error_wrap(err, "encode run");
        }
        reset_chunk(chunk);
        return err;
    }

    if (chunk.size == kTwccFbOneBitElements) {
        if ((err = encode_chunk_one_bit(chunk)) != srs_success) {
            return srs_error_wrap(err, "encode chunk");
        }
        reset_chunk(chunk);
        return err;
    }

    if ((err = encode_chunk_two_bit(chunk, kTwccFbTwoBitElements, true)) != srs_success) {
        return srs_error_wrap(err, "encode chunk");
    }

    return err;
}

srs_error_t SrsRtcpTWCC::encode_remaining_chunk(SrsRtcpTWCC::SrsRtcpTWCCChunk& chunk)
{
    if (chunk.all_same) {
        return encode_chunk_run_length(chunk);
    } else if (chunk.size <= kTwccFbTwoBitElements) {
        // FIXME, TRUE or FALSE
        return encode_chunk_two_bit(chunk, chunk.size, false);
    }
    return encode_chunk_one_bit(chunk);
}

srs_error_t SrsRtcpTWCC::process_pkt_chunk(SrsRtcpTWCC::SrsRtcpTWCCChunk& chunk, int delta_size)
{
    srs_error_t err = srs_success;

    size_t needed_chunk_size = chunk.size == 0 ? kTwccFbChunkBytes : 0;

    size_t might_occupied = pkt_len + needed_chunk_size + delta_size;
    if (might_occupied > kRtcpPacketSize) {
        return srs_error_new(ERROR_RTC_RTCP, "might_occupied %zu", might_occupied);
    }

    if (can_add_to_chunk(chunk, delta_size)) {
        //pkt_len += needed_chunk_size;
        add_to_chunk(chunk, delta_size);
        return err;
    }
    if ((err = encode_chunk(chunk)) != srs_success) {
        return srs_error_new(ERROR_RTC_RTCP, "encode chunk, delta_size %u", delta_size);
    }
    add_to_chunk(chunk, delta_size);
    return err;
}

srs_error_t SrsRtcpTWCC::encode(SrsBuffer *buffer)
{
    srs_error_t err = srs_success;

    err = do_encode(buffer);

    clear();

    return err;
}

srs_error_t SrsRtcpTWCC::do_encode(SrsBuffer *buffer)
{
    srs_error_t err = srs_success;

    if(!buffer->require(nb_bytes())) {
        return srs_error_new(ERROR_RTC_RTCP, "requires %d bytes", nb_bytes());
    }

    pkt_len = kTwccFbPktHeaderSize;
    set<uint16_t, SrsSeqCompareLess>::iterator it_sn = recv_sns_.begin();
    base_sn_ = *it_sn;
    map<uint16_t, srs_utime_t>::iterator it_ts = recv_packes_.find(base_sn_);
    srs_utime_t ts = it_ts->second;
    reference_time_ = (ts % kTwccFbReferenceTimeDivisor) / kTwccFbTimeMultiplier;
    srs_utime_t last_ts = (srs_utime_t)(reference_time_) * kTwccFbTimeMultiplier;
    uint16_t last_sn = base_sn_;
    packet_count_ = recv_packes_.size();

    // encode chunk
    SrsRtcpTWCC::SrsRtcpTWCCChunk chunk;
    for(; it_sn != recv_sns_.end(); ++it_sn) {
        uint16_t current_sn = *it_sn;
        // calculate delta
        it_ts = recv_packes_.find(current_sn);
        srs_utime_t delta_us = calculate_delta_us(it_ts->second, last_ts);
        int16_t delta = delta_us;
        if(delta != delta_us) {
            return srs_error_new(ERROR_RTC_RTCP, "twcc: delta:%" PRId64 ", exceeds the 16bits", delta_us);
        }

        if(current_sn > (last_sn + 1)) {
            // lost packet
            for(uint16_t lost_sn = last_sn + 1; lost_sn < current_sn; ++lost_sn) {
                process_pkt_chunk(chunk, 0);
                packet_count_++;
            }
        }

        // FIXME 24-bit base receive delta not supported
        int recv_delta_size = (delta >= 0 && delta <= 0xff) ? 1 : 2;
        if ((err = process_pkt_chunk(chunk, recv_delta_size)) != srs_success) {
            return srs_error_new(ERROR_RTC_RTCP, "delta_size %d, failed to append_recv_delta", recv_delta_size);
        }

        pkt_deltas_.push_back(delta);
        last_ts += delta * kTwccFbDeltaUnit;
        pkt_len += recv_delta_size;
        last_sn = current_sn;
    }

    if(0 < chunk.size) {
        if((err = encode_remaining_chunk(chunk)) != srs_success) {
            return srs_error_wrap(err, "encode chunk");
        }
    }

    // encode rtcp twcc packet
    if((pkt_len % 4) == 0) {
        header_.length = pkt_len / 4;
    } else {
        header_.length = (pkt_len + 4 - (pkt_len%4)) / 4;
    }
    header_.length -= 1;

    if(srs_success != (err = encode_header(buffer))) {
        return srs_error_wrap(err, "encode header");
    }
    buffer->write_4bytes(sender_ssrc_);
    buffer->write_4bytes(media_ssrc_);
    buffer->write_2bytes(base_sn_);
    buffer->write_2bytes(packet_count_);
    buffer->write_3bytes(reference_time_);
    buffer->write_1bytes(fb_pkt_count_);

    for(vector<uint16_t>::iterator it = encoded_chucks_.begin(); it != encoded_chucks_.end(); ++it) {
        buffer->write_2bytes(*it);
    }
    for(vector<uint16_t>::iterator it = pkt_deltas_.begin(); it != pkt_deltas_.end(); ++it) {
        if(0 <= *it && 0xFF >= *it) {
            // small delta
            uint8_t delta = *it;
            buffer->write_1bytes(delta);
        } else {
            // large or negative delta
            buffer->write_2bytes(*it);
        }
    }
    while((pkt_len % 4) != 0) {
        buffer->write_1bytes(0);
        pkt_len++;
    }

    return err;
}

SrsRtcpNack::SrsRtcpNack(uint32_t sender_ssrc): sender_ssrc_(sender_ssrc)
{
    header_.padding = 0;
    header_.type = SrsRtcpType_rtpfb;
    header_.rc = 1;
    header_.version = kRtcpVersion;
}

SrsRtcpNack::~SrsRtcpNack()
{
}

const uint32_t SrsRtcpNack::get_media_ssrc() const
{
    return media_ssrc_;
}

const vector<uint16_t> SrsRtcpNack::get_lost_sns() const
{
    vector<uint16_t> sn;
    for(set<uint16_t, SrsSeqCompareLess>::iterator it = lost_sns_.begin(); it != lost_sns_.end(); ++it) {
        sn.push_back(*it);
    }
    return sn;
}

void SrsRtcpNack::set_media_ssrc(uint32_t ssrc)
{
    media_ssrc_ = ssrc;
}

void SrsRtcpNack::add_lost_sn(uint16_t sn)
{
    lost_sns_.insert(sn);
}

srs_error_t SrsRtcpNack::decode(SrsBuffer *buffer)
{
    srs_error_t err = srs_success;

    if(srs_success != (err = decode_header(buffer))) {
        return srs_error_wrap(err, "decode header");
    }

    sender_ssrc_ = buffer->read_4bytes();
    media_ssrc_ = buffer->read_4bytes();
    char bitmask[20];
    for(int i = 0; i < (header_.length - 2); i++) {
        uint16_t pid = buffer->read_2bytes();
        uint16_t blp = buffer->read_2bytes();
        lost_sns_.insert(pid);
        memset(bitmask, 0, 20);
        for(int j=0; j<16; j++) {
            bitmask[j] = (blp & ( 1 << j )) >> j ? '1' : '0';
            if((blp & ( 1 << j )) >> j)
                lost_sns_.insert(pid+j+1);
        }
        bitmask[16] = '\n';
        srs_info("[%d] %d / %s", i, pid, bitmask);
    }

    return err;
}
int SrsRtcpNack::nb_bytes()
{
    return kRtcpPacketSize;
}

srs_error_t SrsRtcpNack::encode(SrsBuffer *buffer)
{
    srs_error_t err = srs_success;
    if(!buffer->require(nb_bytes())) {
        return srs_error_new(ERROR_RTC_RTCP, "requires %d bytes", nb_bytes());
    }

    vector<SrsPidBlp> chunks;
    do {
        SrsPidBlp chunk;
        chunk.in_use = false;
        uint16_t pid = 0;
        for(set<uint16_t, SrsSeqCompareLess>::iterator it = lost_sns_.begin(); it != lost_sns_.end(); ++it) {
            uint16_t sn = *it;
            if(!chunk.in_use) {
                chunk.pid = sn;
                chunk.blp = 0;
                chunk.in_use = true;
                pid = sn;
                continue;
            }
            if((sn - pid) < 1) {
                srs_info("skip seq %d", sn);
            } else if( (sn - pid) > 16) {
                // add new chunk
                chunks.push_back(chunk);
                chunk.in_use = false;
            } else {
                chunk.blp |= 1 << (sn-pid-1);
            }
        }
        if(chunk.in_use) {
            chunks.push_back(chunk);
        }

        header_.length = 2 + chunks.size();
        if(srs_success != (err = encode_header(buffer))) {
            err = srs_error_wrap(err, "encode header");
            break;
        }
        buffer->write_4bytes(sender_ssrc_);
        buffer->write_4bytes(media_ssrc_);
        for(vector<SrsPidBlp>::iterator it_chunk = chunks.begin(); it_chunk != chunks.end(); it_chunk++) {
            buffer->write_2bytes(it_chunk->pid);
            buffer->write_2bytes(it_chunk->blp);
        }
    } while(0);

    return err;
}

SrsRtcpCompound::SrsRtcpCompound(): nb_bytes_(0)
{
}

SrsRtcpCompound::~SrsRtcpCompound()
{
   clear();
}

SrsRtcpCommon* SrsRtcpCompound::get_next_rtcp()
{
    if(rtcps_.empty()) {
        return NULL;
    }
    SrsRtcpCommon *rtcp = rtcps_.back();
    nb_bytes_ -= rtcp->nb_bytes();
    rtcps_.pop_back();
    return rtcp;
}

srs_error_t SrsRtcpCompound::add_rtcp(SrsRtcpCommon *rtcp)
{
    int new_len = rtcp->nb_bytes();
    if((new_len + nb_bytes_) > kRtcpPacketSize) {
        return srs_error_new(ERROR_RTC_RTCP, "overflow, new rtcp: %d, current: %d", new_len, nb_bytes_);
    }
    nb_bytes_ += new_len;
    rtcps_.push_back(rtcp);

    return srs_success;
}

srs_error_t SrsRtcpCompound::decode(SrsBuffer *buffer)
{
    srs_error_t err = srs_success;

    while (buffer->empty()) {
        SrsRtcpHeader* header = (SrsRtcpHeader*)(buffer->head());
        if (header->type == SrsRtcpType_sr) {
            SrsRtcpSR *rtcp = new SrsRtcpSR;
            if(srs_success != (err = rtcp->decode(buffer))) {
                return srs_error_wrap(err, "decode sr");
            }
            nb_bytes_ += rtcp->nb_bytes();
            rtcps_.push_back(rtcp);
        } else if (header->type == SrsRtcpType_rr) {
            SrsRtcpRR *rtcp = new SrsRtcpRR;
            if(srs_success != (err = rtcp->decode(buffer))) {
                return srs_error_wrap(err, "decode rr");
            }
            nb_bytes_ += rtcp->nb_bytes();
            rtcps_.push_back(rtcp);
        } else {
            SrsRtcpCommon *rtcp = new SrsRtcpCommon;
            if(srs_success != (err = rtcp->decode(buffer))) {
                return srs_error_wrap(err, "decode type: %#x", header->type);
            }
            nb_bytes_ += rtcp->nb_bytes();
            rtcps_.push_back(rtcp);
        }
    }

    return err;
}

int SrsRtcpCompound::nb_bytes()
{
    return nb_bytes_;
}

srs_error_t SrsRtcpCompound::encode(SrsBuffer *buffer)
{
    srs_error_t err = srs_success;
    if(!buffer->require(nb_bytes_)) {
        return srs_error_new(ERROR_RTC_RTCP, "requires %d bytes", nb_bytes_);
    }

    vector<SrsRtcpCommon*>::iterator it;
    for(it = rtcps_.begin(); it != rtcps_.end(); ++it) {
        SrsRtcpCommon *rtcp = *it;
        if((err = rtcp->encode(buffer)) != srs_success) {
            return srs_error_wrap(err, "encode compound type:%d", rtcp->type());
        }
    }

    clear();
    return err;
}

void SrsRtcpCompound::clear()
{
    vector<SrsRtcpCommon*>::iterator it;
    for(it = rtcps_.begin(); it != rtcps_.end(); ++it) {
        SrsRtcpCommon *rtcp = *it;
        delete rtcp;
        rtcp = NULL;
    }
    rtcps_.clear();
    nb_bytes_ = 0;
}
