
#include <srs_kernel_rtc_rtcp.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>

using namespace std;

SrsRTCPCommon::SrsRTCPCommon()
{
}

SrsRTCPCommon::~SrsRTCPCommon()
{ 
}

srs_error_t SrsRTCPCommon::decode_header(SrsBuffer *buffer)
{
    buffer->read_bytes((char*)(&header_), sizeof(srs_rtcp_header_t));
    header_.length = ntohs(header_.length);
    return srs_success;
}

srs_error_t SrsRTCPCommon::encode_header(SrsBuffer *buffer)
{
    header_.length = htons(header_.length);
    buffer->write_bytes((char*)(&header_), sizeof(srs_rtcp_header_t));
    return srs_success;
}

srs_error_t SrsRTCPCommon::decode(SrsBuffer *buffer)
{
    srs_error_t err = srs_success;
    err = decode_header(buffer);
    if(srs_success != err) {
        return srs_error_wrap(err, "fail to parse rtcp header");
    }
    payload_len_ = (header_.length + 1) * 4 - sizeof(srs_rtcp_header_t);
    buffer->read_bytes((char *)payload_, payload_len_);
    return srs_success;
}

int SrsRTCPCommon::nb_bytes()
{
    return sizeof(srs_rtcp_header_t) + payload_len_;
}

srs_error_t SrsRTCPCommon::encode(SrsBuffer *buffer)
{
    return srs_error_new(ERROR_RTC_RTCP, "not implement");
}

SrsRTCP_App::SrsRTCP_App():ssrc_(0)
{
}

SrsRTCP_App::~SrsRTCP_App()
{
}

const uint32_t SrsRTCP_App::get_ssrc() const
{
    return ssrc_;
}

const uint8_t SrsRTCP_App::get_subtype() const
{
    return header_.rc;
}

const string SrsRTCP_App::get_name() const
{
    return string((char*)name_);
}

const srs_error_t SrsRTCP_App::get_payload(uint8_t*& payload, int& len)
{
    len = payload_len_;
    payload = payload_;
    return srs_success;
}

srs_error_t SrsRTCP_App::set_subtype(uint8_t type)
{
    if(31 < type) {
        return srs_error_new(ERROR_RTC_RTCP, "subtype is out of range. type:%d", type);
    }
    header_.rc = type;
    return srs_success;
}

srs_error_t SrsRTCP_App::set_name(std::string name)
{
    if(name.length() > 4) {
        return srs_error_new(ERROR_RTC_RTCP, "length of name is more than 4. len:%d", name.length());
    }
    memset(name_, 0, sizeof(name_));
    memcpy(name_, name.c_str(), name.length());
    return srs_success;
}

srs_error_t SrsRTCP_App::set_payload(uint8_t* payload, int len)
{
    if(len > (kRtcpPacketSize - 12)) {
        return srs_error_new(ERROR_RTC_RTCP, "length of payload is more than 1488. len:%d", len);
    }
    payload_len_ = len;
    memcpy(payload_, payload, len);
    return srs_success;
}

void SrsRTCP_App::set_ssrc(uint32_t ssrc)
{
    ssrc_ = ssrc;
}

srs_error_t SrsRTCP_App::decode(SrsBuffer *buffer)
{
    srs_error_t err = srs_success;
    err = decode_header(buffer);
    if(srs_success != err) {
        return srs_error_wrap(err, "fail to parse rtcp header");
    }
    ssrc_ = buffer->read_4bytes();
    buffer->read_bytes((char *)name_, sizeof(name_));
    payload_len_ = (header_.length + 1) * 4 - sizeof(srs_rtcp_header_t) - sizeof(name_) - sizeof(ssrc_);
    buffer->read_bytes((char *)payload_, payload_len_);
    return srs_success;
}

int SrsRTCP_App::nb_bytes()
{
    return sizeof(srs_rtcp_header_t) + sizeof(ssrc_) + sizeof(name_) + payload_len_;
}

srs_error_t SrsRTCP_App::encode(SrsBuffer *buffer)
{
    srs_error_t err = srs_success;
    if(! buffer->require(nb_bytes())) {
        return srs_error_new(ERROR_RTC_RTCP, 
            "the size of buffer is not enough. buffer:%d, required:%d", buffer->left(), nb_bytes());
    }
    err = encode_header(buffer);
    if(srs_success != err) {
        return srs_error_wrap(err, "fail to encode rtcp header");
    }
    buffer->write_4bytes(ssrc_);
    buffer->write_bytes((char*)name_, sizeof(name_));
    buffer->write_bytes((char*)payload_, payload_len_);
    
    return srs_success;
}

SrsRTCP_SR::SrsRTCP_SR()
{
    header_.padding = 0;
    header_.type = srs_rtcp_type_sr;
    header_.rc = 0;
    header_.version = kRtcpVersion;
    header_.length = 6;
}

SrsRTCP_SR::~SrsRTCP_SR()
{

}

const uint32_t SrsRTCP_SR::get_sender_ssrc() const
{
    return sender_ssrc_;
}

const uint64_t SrsRTCP_SR::get_ntp() const
{
    return ntp_;
}

const uint32_t SrsRTCP_SR::get_rtp_ts() const
{
    return rtp_ts_;
}

const uint32_t SrsRTCP_SR::get_rtp_send_packets() const
{
    return send_rtp_packets_;
}

const uint32_t SrsRTCP_SR::get_rtp_send_bytes() const
{
    return send_rtp_bytes_;
}

void SrsRTCP_SR::set_sender_ssrc(uint32_t ssrc)
{
    sender_ssrc_ = ssrc;
}

void SrsRTCP_SR::set_ntp(uint64_t ntp)
{
    ntp_ = ntp;
}

void SrsRTCP_SR::set_rtp_ts(uint32_t ts)
{
    rtp_ts_ = ts;
}

void SrsRTCP_SR::set_rtp_send_packets(uint32_t packets)
{
    send_rtp_packets_ = packets;
}

void SrsRTCP_SR::set_rtp_send_bytes(uint32_t bytes)
{
    send_rtp_bytes_ = bytes;
}

srs_error_t SrsRTCP_SR::decode(SrsBuffer *buffer)
{
    srs_error_t err = srs_success;
    err = decode_header(buffer);
    if(srs_success != err) {
        return srs_error_wrap(err, "fail to parse rtcp header");
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

int SrsRTCP_SR::nb_bytes()
{
    return (header_.length + 1) * 4;
}

srs_error_t SrsRTCP_SR::encode(SrsBuffer *buffer)
{
    srs_error_t err = srs_success;
    if(! buffer->require(nb_bytes())) {
        return srs_error_new(ERROR_RTC_RTCP, 
            "the size of buffer is not enough. buffer:%d, required:%d", buffer->left(), nb_bytes());
    }
    err = encode_header(buffer);
    if(srs_success != err) {
        return srs_error_wrap(err, "fail to encode rtcp header");
    }
    buffer->write_4bytes(sender_ssrc_);
    buffer->write_8bytes(ntp_);
    buffer->write_4bytes(rtp_ts_);
    buffer->write_4bytes(send_rtp_packets_);
    buffer->write_4bytes(send_rtp_bytes_);
    return err;
}

SrsRTCP_RR::SrsRTCP_RR(uint32_t sender_ssrc/*=0*/): sender_ssrc_(sender_ssrc)
{
    header_.padding = 0;
    header_.type = srs_rtcp_type_rr;
    header_.rc = 0;
    header_.version = kRtcpVersion;
    header_.length = 7;
}

SrsRTCP_RR::~SrsRTCP_RR()
{
}

const uint32_t SrsRTCP_RR::get_rb_ssrc() const
{
    return rb_.ssrc;
}

const float SrsRTCP_RR::get_lost_rate() const
{
    return rb_.fraction_lost / 256;
}

const uint32_t SrsRTCP_RR::get_lost_packets() const
{
    return rb_.lost_packets;
}

const uint32_t SrsRTCP_RR::get_highest_sn() const
{
    return rb_.highest_sn;
}

const uint32_t SrsRTCP_RR::get_jitter() const
{
    return rb_.jitter;
}

const uint32_t SrsRTCP_RR::get_lsr() const
{
    return rb_.lsr;
}

const uint32_t SrsRTCP_RR::get_dlsr() const
{
    return rb_.dlsr;
}

void SrsRTCP_RR::set_rb_ssrc(uint32_t ssrc)
{
    rb_.ssrc = ssrc;
}

void SrsRTCP_RR::set_lost_rate(float rate)
{
    rb_.fraction_lost = rate * 256;
}

void SrsRTCP_RR::set_lost_packets(uint32_t count)
{
    rb_.lost_packets = count;
}

void SrsRTCP_RR::set_highest_sn(uint32_t sn)
{
    rb_.highest_sn = sn;
}

void SrsRTCP_RR::set_jitter(uint32_t jitter)
{
    rb_.jitter = jitter;
}

void SrsRTCP_RR::set_lsr(uint32_t lsr)
{
    rb_.lsr = lsr;
}

void SrsRTCP_RR::set_dlsr(uint32_t dlsr)
{
    rb_.dlsr = dlsr;
}

void SrsRTCP_RR::set_sender_ntp(uint64_t ntp)
{
    uint32_t lsr = (uint32_t)((ntp >> 16) & 0x00000000FFFFFFFF);
    rb_.lsr = lsr;
}

srs_error_t SrsRTCP_RR::decode(SrsBuffer *buffer)
{
    srs_error_t err = srs_success;
    err = decode_header(buffer);
    if(srs_success != err) {
        return srs_error_wrap(err, "fail to parse rtcp header");
    }
    sender_ssrc_ = buffer->read_4bytes();
    if(header_.rc < 1) {
        return srs_success;
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

int SrsRTCP_RR::nb_bytes()
{
    return (header_.length + 1) * 4;
}

srs_error_t SrsRTCP_RR::encode(SrsBuffer *buffer)
{
    srs_error_t err = srs_success;
    if(! buffer->require(nb_bytes())) {
        return srs_error_new(ERROR_RTC_RTCP, 
            "the size of buffer is not enough. buffer:%d, required:%d", buffer->left(), nb_bytes());
    }

    header_.rc = 1;
    err = encode_header(buffer);
    if(srs_success != err) {
        return srs_error_wrap(err, "fail to encode rtcp header");
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

SrsRTCP_TWCC::SrsRTCP_TWCC(uint32_t sender_ssrc/*=0*/) : sender_ssrc_(sender_ssrc), pkt_len(0)
{
    header_.padding = 0;
    header_.type = srs_rtcp_type_rtpfb;
    header_.rc = 15;
    header_.version = kRtcpVersion;
}
    
SrsRTCP_TWCC::~SrsRTCP_TWCC()
{
}

void SrsRTCP_TWCC::clear()
{
    encoded_chucks_.clear();
    pkt_deltas_.clear();
    recv_packes_.clear();
    recv_sns_.clear();
}

const uint32_t SrsRTCP_TWCC::get_media_ssrc() const
{
    return media_ssrc_;
}
const uint16_t SrsRTCP_TWCC::get_base_sn() const
{
    return base_sn_;
}

const uint32_t SrsRTCP_TWCC::get_reference_time() const
{
    return reference_time_;
}

const uint8_t SrsRTCP_TWCC::get_feedback_count() const
{
    return fb_pkt_count_;
}

const uint16_t SrsRTCP_TWCC::get_packet_status_count() const
{
    return packet_count_;
}
    
const vector<uint16_t> SrsRTCP_TWCC::get_packet_chucks() const
{
    return encoded_chucks_;
}

const vector<uint16_t> SrsRTCP_TWCC::get_recv_deltas() const
{
    return pkt_deltas_;
}

void SrsRTCP_TWCC::set_media_ssrc(uint32_t ssrc)
{
    media_ssrc_ = ssrc;
}
void SrsRTCP_TWCC::set_base_sn(uint16_t sn)
{
    base_sn_ = sn;
}

void SrsRTCP_TWCC::set_packet_status_count(uint16_t count)
{
    packet_count_ = count;
}

void SrsRTCP_TWCC::set_reference_time(uint32_t time)
{
    reference_time_ = time;
}

void SrsRTCP_TWCC::set_feedback_count(uint8_t count)
{
    fb_pkt_count_ = count;
}
    
void SrsRTCP_TWCC::add_packet_chuck(uint16_t chunk)
{
    encoded_chucks_.push_back(chunk);
}

void SrsRTCP_TWCC::add_recv_delta(uint16_t delta)
{
    pkt_deltas_.push_back(delta);
}

srs_error_t SrsRTCP_TWCC::recv_packet(uint16_t sn, srs_utime_t ts)
{
    map<uint16_t, srs_utime_t>::iterator it = recv_packes_.find(sn);
    if(it != recv_packes_.end()) {
        return srs_error_new(ERROR_RTC_RTCP, "twcc: recv duplicated sn:%d", sn);
    }
    recv_packes_[sn] = ts;
    recv_sns_.insert(sn);
    return srs_success;
}

srs_error_t SrsRTCP_TWCC::decode(SrsBuffer *buffer)
{
    srs_error_t err = srs_success;
    
    return err;
}

int SrsRTCP_TWCC::nb_bytes()
{
    return kRtcpPacketSize;
}

srs_utime_t SrsRTCP_TWCC::calculate_delta_us(srs_utime_t ts, srs_utime_t last)
{
    int64_t divisor = kTwccFbReferenceTimeDivisor;
    int64_t delta_us = (ts - last) % divisor;

    if (delta_us > (divisor >> 1))
        delta_us -= divisor;

    delta_us += (delta_us < 0) ? (-kTwccFbDeltaUnit / 2) : (kTwccFbDeltaUnit / 2);
    delta_us /= kTwccFbDeltaUnit;

    return delta_us;
}

bool SrsRTCP_TWCC::can_add_to_chunk(SrsRTCP_TWCC::srs_rtcp_twcc_chunk_t& chunk, int delta_size)
{
	srs_verbose("can_add %d chunk->size %u delta_sizes %d %d %d %d %d %d %d %d %d %d %d %d %d %d"
						" all_same %d has_large_delta %d",
			  delta_size,
			  chunk.size,
			  chunk.delta_sizes[0], chunk.delta_sizes[1], chunk.delta_sizes[2],
			  chunk.delta_sizes[3], chunk.delta_sizes[4], chunk.delta_sizes[5],
			  chunk.delta_sizes[6], chunk.delta_sizes[7], chunk.delta_sizes[8],
			  chunk.delta_sizes[9], chunk.delta_sizes[10], chunk.delta_sizes[11],
			  chunk.delta_sizes[12], chunk.delta_sizes[13],
			  (int)chunk.all_same,
			  (int)chunk.has_large_delta
			  );

    if (chunk.size < kTwccFbTwoBitElements)
        return true;


    if (chunk.size < kTwccFbOneBitElements && !chunk.has_large_delta && delta_size != kTwccFbLargeRecvDeltaBytes)
        return true;


    if (chunk.size < kTwccFbMaxRunLength && chunk.all_same && chunk.delta_sizes[0] == delta_size) {
        srs_verbose("< 8191 && all_same && delta_size[0] %d == %d",
                  chunk.delta_sizes[0], delta_size);
        return true;
    }

    return false;
}

void SrsRTCP_TWCC::add_to_chunk(SrsRTCP_TWCC::srs_rtcp_twcc_chunk_t& chunk, int delta_size)
{
    if (chunk.size < kTwccFbMaxBitElements)
        chunk.delta_sizes[chunk.size] = delta_size;
    chunk.size += 1;
    chunk.all_same = chunk.all_same && delta_size == chunk.delta_sizes[0];
    chunk.has_large_delta = chunk.has_large_delta || delta_size >= kTwccFbLargeRecvDeltaBytes;
}

srs_error_t SrsRTCP_TWCC::encode_chunk_run_length(SrsRTCP_TWCC::srs_rtcp_twcc_chunk_t& chunk)
{
    if (!chunk.all_same || chunk.size > kTwccFbMaxRunLength)
        return srs_error_new(ERROR_RTC_RTCP, "cannot encode by run length. all_same:%d, size:%d", chunk.all_same, chunk.size);

    uint16_t encoded_chunk = (chunk.delta_sizes[0] << 13) | chunk.size;

    encoded_chucks_.push_back(encoded_chunk);
    pkt_len += sizeof(encoded_chunk);

    return 0;
}

srs_error_t SrsRTCP_TWCC::encode_chunk_one_bit(SrsRTCP_TWCC::srs_rtcp_twcc_chunk_t& chunk)
{
    int i = 0;
    if (chunk.has_large_delta)
        return srs_error_new(ERROR_RTC_RTCP, "it's large delta, cannot encode by one bit moe");
    uint16_t encoded_chunk = 0x8000;
    for (i = 0; i < chunk.size; ++i) {
        encoded_chunk |= (chunk.delta_sizes[i] << (kTwccFbOneBitElements - 1 - i));
    }

    encoded_chucks_.push_back(encoded_chunk);
    pkt_len += sizeof(encoded_chunk);

    /* 1 0 symbol_list */
    return srs_success;
}
    
srs_error_t SrsRTCP_TWCC::encode_chunk_two_bit(SrsRTCP_TWCC::srs_rtcp_twcc_chunk_t& chunk, size_t size, bool shift)
{
    unsigned int i = 0;
    uint8_t delta_size = 0;
    
    uint16_t encoded_chunk = 0xc000;
    /* 1 1 symbol_list */
    for (i = 0; i < size; ++i) {
        encoded_chunk |= (chunk.delta_sizes[i] << (2 * (kTwccFbTwoBitElements - 1 - i)));
    }
    encoded_chucks_.push_back(encoded_chunk);
    pkt_len += sizeof(encoded_chunk);

    if (shift) {
        chunk.all_same = true;
        chunk.has_large_delta = false;
        for (i = size; i < chunk.size; ++i) {
            delta_size = chunk.delta_sizes[i];
            chunk.delta_sizes[i - size] = delta_size;
            chunk.all_same = (chunk.all_same && delta_size == chunk.delta_sizes[0]);
            chunk.has_large_delta = chunk.has_large_delta || delta_size == kTwccFbLargeRecvDeltaBytes;
        }
        //	JANUS_LOG(LOG_INFO, "ccc->size %u size %u B\n", ccc->size, size);
        chunk.size -= size;
        //	JANUS_LOG(LOG_INFO, "ccc->size %u shift %d A\n", ccc->size, shift);
    }

    return srs_success;
}

void SrsRTCP_TWCC::reset_chunk(SrsRTCP_TWCC::srs_rtcp_twcc_chunk_t& chunk)
{
    chunk.size = 0;

    chunk.all_same = true;
    chunk.has_large_delta = false;
}

srs_error_t SrsRTCP_TWCC::encode_chunk(SrsRTCP_TWCC::srs_rtcp_twcc_chunk_t& chunk)
{
    srs_error_t err = srs_success;

    if (can_add_to_chunk(chunk, 0) && can_add_to_chunk(chunk, 1) &&
        can_add_to_chunk(chunk, 2))
        return srs_error_new(ERROR_RTC_RTCP, "it should be added to chunk, not encode");

    if (chunk.all_same) {
        if ((err = encode_chunk_run_length(chunk)) != srs_success)
            return srs_error_wrap(err, "fail to encode chunk by run length mode");
        reset_chunk(chunk);
        return err;
    }

    if (chunk.size == kTwccFbOneBitElements) {
        if ((err = encode_chunk_one_bit(chunk)) != srs_success)
            return srs_error_wrap(err, "fail to encode chunk by one bit mode");
        reset_chunk(chunk);
        return err;
    }

    if ((err =encode_chunk_two_bit(chunk, kTwccFbTwoBitElements, true)) != srs_success) 
        return srs_error_wrap(err, "fail to encode chunk by two bit mode");

    return err;
}

srs_error_t SrsRTCP_TWCC::encode_remaining_chunk(SrsRTCP_TWCC::srs_rtcp_twcc_chunk_t& chunk)
{
    if (chunk.all_same) {
        return encode_chunk_run_length(chunk);
    } else if (chunk.size <= kTwccFbTwoBitElements) {
        // FIXME, TRUE or FALSE
        return encode_chunk_two_bit(chunk, chunk.size, false);
    }
    return encode_chunk_one_bit(chunk);
}

srs_error_t SrsRTCP_TWCC::process_pkt_chunk(SrsRTCP_TWCC::srs_rtcp_twcc_chunk_t& chunk, int delta_size)
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
/*
    if (pkt_len + delta_size + kTwccFbChunkBytes > kRtcpPacketSize) {
        JANUS_LOG(LOG_INFO, "chunk_can_not_add, delta_size %u\n", delta_size);
        return -1;
    }
*/
    if ((err = encode_chunk(chunk)) != srs_success) {
        return srs_error_new(ERROR_RTC_RTCP, "chunk can not be encoded, delta_size %u", delta_size);
    }
/*
    ccf->encoded_chunks = g_list_append(ccf->encoded_chunks, ((gpointer) (glong) (chunk)));
    ccf->size_bytes += sizeof(chunk);
    */
    add_to_chunk(chunk, delta_size);
    return err;
}

srs_error_t SrsRTCP_TWCC::encode(SrsBuffer *buffer)
{
    srs_error_t err = srs_success;
    if(! buffer->require(nb_bytes())) {
        return srs_error_new(ERROR_RTC_RTCP, 
            "the size of buffer is not enough. buffer:%d, required:%d", buffer->left(), nb_bytes());
    }
    pkt_len = kTwccFbPktHeaderSize;
    set<uint16_t, less_compare>::iterator it_sn = recv_sns_.begin();
    base_sn_ = *it_sn;
    map<uint16_t, srs_utime_t>::iterator it_ts = recv_packes_.find(base_sn_);
    srs_utime_t ts = it_ts->second;
    reference_time_ = (ts % kTwccFbReferenceTimeDivisor) / kTwccFbTimeMultiplier;
    srs_utime_t last_ts = (srs_utime_t)(reference_time_) * kTwccFbTimeMultiplier;
    uint16_t last_sn = base_sn_;
    packet_count_ = recv_packes_.size();
    do {
        // encode chunk
        SrsRTCP_TWCC::srs_rtcp_twcc_chunk_t chunk;
        for(; it_sn != recv_sns_.end(); ++it_sn) {
            uint16_t current_sn = *it_sn;
            // calculate delta
            it_ts = recv_packes_.find(current_sn);
            srs_utime_t delta_us = calculate_delta_us(it_ts->second, last_ts);
            uint16_t delta = delta_us;
            if(delta != delta_us) {
                return srs_error_new(ERROR_RTC_RTCP, "twcc: delta:%lld, exceeds the 16-bit base receive delta", delta_us);
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
            /* pakcet received, small delta 				1
            * packet received, large or negative delta 	2
            * */
            if ((err = process_pkt_chunk(chunk, recv_delta_size)) != srs_success) {
                return srs_error_new(ERROR_RTC_RTCP, "delta_size %d, failed to append_recv_delta\n", recv_delta_size);
            }

            pkt_deltas_.push_back(delta);
            last_ts += delta * kTwccFbDeltaUnit;
            pkt_len += recv_delta_size;
            last_sn = current_sn;
        }

        if(0 < chunk.size) {
            if((err = encode_remaining_chunk(chunk)) != srs_success) {
                return srs_error_wrap(err, "fail to encode remaining chunk");
            }
        }

        // encode rtcp twcc packet
        if((pkt_len % 4) == 0) {
            header_.length = pkt_len / 4;
        } else {
            header_.length = (pkt_len + 4 - (pkt_len%4)) / 4;
        }
        header_.length -= 1;

        err = encode_header(buffer);
        if(srs_success != err) {
            err = srs_error_wrap(err, "fail to encode rtcp header");
            break;
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

    } while(0);
    
    clear();

    return err;
}

SrsRTCP_Nack::SrsRTCP_Nack(uint32_t sender_ssrc /*= 0*/): sender_ssrc_(sender_ssrc)
{
    header_.padding = 0;
    header_.type = srs_rtcp_type_rtpfb;
    header_.rc = 1;
    header_.version = kRtcpVersion;
}

SrsRTCP_Nack::~SrsRTCP_Nack()
{
}

const uint32_t SrsRTCP_Nack::get_media_ssrc() const
{
    return media_ssrc_;
}

const vector<uint16_t> SrsRTCP_Nack::get_lost_sns() const
{
    vector<uint16_t> sn;
    for(set<uint16_t, less_compare>::iterator it = lost_sns_.begin(); it != lost_sns_.end(); ++it) {
        sn.push_back(*it);
    }
    return sn;
}

void SrsRTCP_Nack::set_media_ssrc(uint32_t ssrc)
{
    media_ssrc_ = ssrc;
}

void SrsRTCP_Nack::add_lost_sn(uint16_t sn)
{
    lost_sns_.insert(sn);
}

srs_error_t SrsRTCP_Nack::decode(SrsBuffer *buffer)
{
    srs_error_t err = srs_success;
    err = decode_header(buffer);
    if(srs_success != err) {
        return srs_error_wrap(err, "fail to parse rtcp header");
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
int SrsRTCP_Nack::nb_bytes()
{
    return kRtcpPacketSize;
}

srs_error_t SrsRTCP_Nack::encode(SrsBuffer *buffer)
{
    srs_error_t err = srs_success;
    if(! buffer->require(nb_bytes())) {
        return srs_error_new(ERROR_RTC_RTCP, 
            "the size of buffer is not enough. buffer:%d, required:%d", buffer->left(), nb_bytes());
    }

    vector<pid_blp_t> chunks;
    do {
        pid_blp_t chunk;
        chunk.in_use = false;
        uint16_t pid = 0;
        for(set<uint16_t, less_compare>::iterator it = lost_sns_.begin(); it != lost_sns_.end(); ++it) {
            uint16_t sn = *it;
            if(!chunk.in_use) {
                chunk.pid = sn;
                chunk.blp = 0;
                chunk.in_use = true;
                pid = sn;
                continue;
            }
            if((sn - pid) < 1) {
                srs_info("Skipping PID to NACK (%d already added)...\n", sn);
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
        err = encode_header(buffer);
        if(srs_success != err) {
            err = srs_error_wrap(err, "fail to encode rtcp header");
            break;
        }
        buffer->write_4bytes(sender_ssrc_);
        buffer->write_4bytes(media_ssrc_);
        for(vector<pid_blp_t>::iterator it_chunk = chunks.begin(); it_chunk != chunks.end(); it_chunk++) {
            buffer->write_2bytes(it_chunk->pid);
            buffer->write_2bytes(it_chunk->blp);
        }
    
    } while(0);

    return err;
}

SrsRTCPCompound::SrsRTCPCompound(): nb_bytes_(0)
{
}

SrsRTCPCompound::~SrsRTCPCompound()
{
   clear();
}

SrsRTCPCommon* SrsRTCPCompound::get_next_rtcp()
{
    if(rtcps_.empty()) {
        return NULL;
    }
    SrsRTCPCommon *rtcp = rtcps_.back();
    nb_bytes_ -= rtcp->nb_bytes();
    rtcps_.pop_back();
    return rtcp;
}

srs_error_t SrsRTCPCompound::add_rtcp(SrsRTCPCommon *rtcp)
{
    int new_len = rtcp->nb_bytes();
    if((new_len + nb_bytes_) > kRtcpPacketSize) {
        return srs_error_new(ERROR_RTC_RTCP, "exceed the rtcp max size. new rtcp: %d, current: %d", new_len, nb_bytes_);
    }
    nb_bytes_ += new_len;
    rtcps_.push_back(rtcp);

    return srs_success;
}

srs_error_t SrsRTCPCompound::decode(SrsBuffer *buffer)
{
    srs_error_t err = srs_success;

    while(0 != buffer->left()) {
        srs_rtcp_header_t* header = (srs_rtcp_header_t *)(buffer->head());
        switch (header->type)
        {
        case srs_rtcp_type_sr:
        {
            SrsRTCP_SR *rtcp = new SrsRTCP_SR;
            err = rtcp->decode(buffer);
            if(srs_success != err) {
                return srs_error_wrap(err, "fail to decode rtcp sr");
            }
            nb_bytes_ += rtcp->nb_bytes();
            rtcps_.push_back(rtcp);
            break;
        }
        case srs_rtcp_type_rr:
        {
            SrsRTCP_RR *rtcp = new SrsRTCP_RR;
            err = rtcp->decode(buffer);
            if(srs_success != err) {
                return srs_error_wrap(err, "fail to decode rtcp rr");
            }
            nb_bytes_ += rtcp->nb_bytes();
            rtcps_.push_back(rtcp);
            break;
        }
        default:
        {
            SrsRTCPCommon *rtcp = new SrsRTCPCommon;
            err = rtcp->decode(buffer);
            if(srs_success != err) {
                return srs_error_wrap(err, "fail to decode rtcp type:%d", header->type);
            }
            nb_bytes_ += rtcp->nb_bytes();
            rtcps_.push_back(rtcp);
            break;
        }
        }
    }

    return err;
}

int SrsRTCPCompound::nb_bytes()
{
    return nb_bytes_;
}

srs_error_t SrsRTCPCompound::encode(SrsBuffer *buffer)
{
    srs_error_t err = srs_success;
    if(false == buffer->require(nb_bytes_)) {
        return srs_error_new(ERROR_RTC_RTCP, 
            "the left size of buffer is not enough. buffer:%d, required:%d", buffer->left(), nb_bytes_);
    }

    vector<SrsRTCPCommon*>::iterator it;
    for(it = rtcps_.begin(); it != rtcps_.end(); ++it) {
        SrsRTCPCommon *rtcp = *it;
        err = rtcp->encode(buffer);
        if(err != srs_success) {
            return srs_error_wrap(err, "fail to encode rtcp compound. type:%d", rtcp->type());
        }
    }

    clear();
    return err;
}

void SrsRTCPCompound::clear()
{
    vector<SrsRTCPCommon*>::iterator it;
    for(it = rtcps_.begin(); it != rtcps_.end(); ++it) {
        SrsRTCPCommon *rtcp = *it;
        delete rtcp;
        rtcp = NULL;
    }
    rtcps_.clear();
    nb_bytes_ = 0;
}
