/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2020 John
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

#include <srs_app_rtp_queue.hpp>

#include <string.h>
#include <unistd.h>
#include <sstream>

using namespace std;

#include <srs_kernel_error.hpp>
#include <srs_kernel_rtp.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_app_utility.hpp>

SrsRtpNackInfo::SrsRtpNackInfo()
{
    generate_time_ = srs_update_system_time();
    pre_req_nack_time_ = 0;
    req_nack_count_ = 0;
}

SrsRtpNackForReceiver::SrsRtpNackForReceiver(SrsRtpQueue* rtp_queue, size_t queue_size)
{
    max_queue_size_ = queue_size;
    rtp_queue_ = rtp_queue;
    pre_check_time_ = 0;
    
    srs_info("max_queue_size=%u, nack opt: max_count=%d, max_alive_time=%us, first_nack_interval=%ld, nack_interval=%ld"
        max_queue_size_, opts_.max_count, opts_.max_alive_time, opts.first_nack_interval, opts_.nack_interval);
}

SrsRtpNackForReceiver::~SrsRtpNackForReceiver()
{
}

void SrsRtpNackForReceiver::insert(uint16_t seq)
{
    // FIXME: full, drop packet, and request key frame.
    queue_[seq] = SrsRtpNackInfo();
}

void SrsRtpNackForReceiver::remove(uint16_t seq)
{
    queue_.erase(seq);
}

SrsRtpNackInfo* SrsRtpNackForReceiver::find(uint16_t seq)
{
    std::map<uint16_t, SrsRtpNackInfo>::iterator iter = queue_.find(seq);
    
    if (iter == queue_.end()) {
        return NULL;
    }

    return &(iter->second);
}

void SrsRtpNackForReceiver::check_queue_size()
{
    if (queue_.size() >= max_queue_size_) {
        rtp_queue_->notify_nack_list_full();
    }
}

void SrsRtpNackForReceiver::get_nack_seqs(vector<uint16_t>& seqs)
{
    srs_utime_t now = srs_update_system_time();
    srs_utime_t interval = now - pre_check_time_;
    if (interval < opts_.nack_interval / 2) {
        return;
    }
    pre_check_time_ = now;

    std::map<uint16_t, SrsRtpNackInfo>::iterator iter = queue_.begin();
    while (iter != queue_.end()) {
        const uint16_t& seq = iter->first;
        SrsRtpNackInfo& nack_info = iter->second;

        int alive_time = now - nack_info.generate_time_;
        if (alive_time > opts_.max_alive_time || nack_info.req_nack_count_ > opts_.max_count) {
            rtp_queue_->notify_drop_seq(seq);
            queue_.erase(iter++);
            continue;
        }

        // TODO:Statistics unorder packet.
        if (now - nack_info.generate_time_ < opts_.first_nack_interval) {
            break;
        }

        if (now - nack_info.pre_req_nack_time_ >= opts_.nack_interval && nack_info.req_nack_count_ <= opts_.max_count) {
            ++nack_info.req_nack_count_;
            nack_info.pre_req_nack_time_ = now;
            seqs.push_back(seq);
        }

        ++iter;
    }
}

void SrsRtpNackForReceiver::update_rtt(int rtt)
{
    rtt_ = rtt * SRS_UTIME_MILLISECONDS;
    // FIXME: limit min and max value.
    opts_.nack_interval = rtt_;
}

SrsRtpRingBuffer::SrsRtpRingBuffer(int capacity)
{
    nn_seq_flip_backs = 0;
    begin = end = 0;
    capacity_ = (uint16_t)capacity;
    initialized_ = false;

    queue_ = new SrsRtpPacket2*[capacity_];
    memset(queue_, 0, sizeof(SrsRtpPacket2*) * capacity);
}

SrsRtpRingBuffer::~SrsRtpRingBuffer()
{
    srs_freepa(queue_);
}

bool SrsRtpRingBuffer::empty()
{
    return begin == end;
}

int SrsRtpRingBuffer::size()
{
    int size = srs_rtp_seq_distance(begin, end);
    srs_assert(size >= 0);
    return size;
}

void SrsRtpRingBuffer::advance_to(uint16_t seq)
{
    begin = seq;
}

void SrsRtpRingBuffer::set(uint16_t at, SrsRtpPacket2* pkt)
{
    SrsRtpPacket2* p = queue_[at % capacity_];

    if (p) {
        srs_freep(p);
    }

    queue_[at % capacity_] = pkt;
}

void SrsRtpRingBuffer::remove(uint16_t at)
{
    set(at, NULL);
}

void SrsRtpRingBuffer::reset(uint16_t first, uint16_t last)
{
    for (uint16_t s = first; s != last; ++s) {
        queue_[s % capacity_] = NULL;
    }
}

bool SrsRtpRingBuffer::overflow()
{
    return srs_rtp_seq_distance(begin, end) >= capacity_;
}

uint32_t SrsRtpRingBuffer::get_extended_highest_sequence()
{
    return nn_seq_flip_backs * 65536 + end - 1;
}

void SrsRtpRingBuffer::update(uint16_t seq, uint16_t& nack_first, uint16_t& nack_last)
{
    if (!initialized_) {
        initialized_ = true;
        begin = seq;
        end = seq + 1;
        return;
    }

    // Normal sequence, seq follows high_.
    if (srs_rtp_seq_distance(end, seq) >= 0) {
        nack_first = end;
        nack_last = seq;

        // When distance(seq,high_)>0 and seq<high_, seq must flip back,
        // for example, high_=65535, seq=1, distance(65535,1)>0 and 1<65535.
        // TODO: FIXME: The first flip may be dropped.
        if (seq < end) {
            ++nn_seq_flip_backs;
        }
        end = seq + 1;
        return;
    }

    // Out-of-order sequence, seq before low_.
    if (srs_rtp_seq_distance(seq, begin) > 0) {
        // When startup, we may receive packets in chaos order.
        // Because we don't know the ISN(initiazlie sequence number), the first packet
        // we received maybe no the first packet client sent.
        // @remark We only log a warning, because it seems ok for publisher.
        srs_warn("too old seq %u, range [%u, %u]", seq, begin, end);
    }
}

SrsRtpPacket2* SrsRtpRingBuffer::at(uint16_t seq)
{
    return queue_[seq % capacity_];
}

SrsRtpQueue::SrsRtpQueue(int capacity)
{
    queue_ = new SrsRtpRingBuffer(capacity);

    jitter_ = 0;
    last_trans_time_ = -1;
    
    pre_number_of_packet_received_ = 0;
    pre_number_of_packet_lossed_ = 0;

    num_of_packet_received_ = 0;
    number_of_packet_lossed_ = 0;
}

SrsRtpQueue::~SrsRtpQueue()
{
    srs_freep(queue_);
}

srs_error_t SrsRtpQueue::consume(SrsRtpNackForReceiver* nack, SrsRtpPacket2* pkt)
{
    srs_error_t err = srs_success;

    // TODO: FIXME: Update time for each packet, may hurt performance.
    srs_utime_t now = srs_update_system_time();

    uint16_t seq = pkt->rtp_header.get_sequence();

    SrsRtpNackInfo* nack_info = NULL;

    // If no NACK, disable nack.
    if (nack) {
        nack_info = nack->find(seq);
    }

    if (nack_info) {
        int nack_rtt = nack_info->req_nack_count_ ? ((now - nack_info->pre_req_nack_time_) / SRS_UTIME_MILLISECONDS) : 0;
        (void)nack_rtt;
        nack->remove(seq);
    }

    // Calc jitter time, ignore nack packets.
    // TODO: FIXME: Covert time to srs_utime_t.
    if (last_trans_time_ == -1) {
        last_trans_time_ = now / 1000 - pkt->rtp_header.get_timestamp() / 90;
    } else if (!nack_info) {
        int trans_time = now / 1000 - pkt->rtp_header.get_timestamp() / 90;

        int cur_jitter = trans_time - last_trans_time_;
        if (cur_jitter < 0) {
            cur_jitter = -cur_jitter;
        }

        last_trans_time_ = trans_time;

        jitter_ = (jitter_ * 15.0 / 16.0) + (static_cast<double>(cur_jitter) / 16.0);
    }

    // OK, we got one new RTP packet, which is not in NACK.
    if (!nack_info) {
        ++num_of_packet_received_;
        uint16_t nack_first = 0, nack_last = 0;
        queue_->update(seq, nack_first, nack_last);
        if (nack && srs_rtp_seq_distance(nack_first, nack_last) > 0) {
            srs_trace("update seq=%u, nack range [%u, %u]", seq, nack_first, nack_last);
            insert_into_nack_list(nack, nack_first, nack_last);
        }
    }

    // Save packet at the position seq.
    queue_->set(seq, pkt);

    return err;
}

uint32_t SrsRtpQueue::get_extended_highest_sequence()
{
    return queue_->get_extended_highest_sequence();
}

uint8_t SrsRtpQueue::get_fraction_lost()
{
    int64_t total = (number_of_packet_lossed_ - pre_number_of_packet_lossed_ + num_of_packet_received_ - pre_number_of_packet_received_);
    uint8_t loss = 0;
    if (total > 0) {
        loss = (number_of_packet_lossed_ - pre_number_of_packet_lossed_) * 256 / total;
    }

    pre_number_of_packet_lossed_ = number_of_packet_lossed_;
    pre_number_of_packet_received_ = num_of_packet_received_;

    return loss;
}

uint32_t SrsRtpQueue::get_cumulative_number_of_packets_lost()
{
    return number_of_packet_lossed_;
}

uint32_t SrsRtpQueue::get_interarrival_jitter()
{
    return static_cast<uint32_t>(jitter_);
}

void SrsRtpQueue::insert_into_nack_list(SrsRtpNackForReceiver* nack, uint16_t first, uint16_t last)
{
    if (!nack) {
        return;
    }

    for (uint16_t s = first; s != last; ++s) {
        nack->insert(s);
        ++number_of_packet_lossed_;
    }

    nack->check_queue_size();
}

SrsRtpAudioQueue::SrsRtpAudioQueue(int capacity) : SrsRtpQueue(capacity)
{
}

SrsRtpAudioQueue::~SrsRtpAudioQueue()
{
}

void SrsRtpAudioQueue::notify_drop_seq(uint16_t seq)
{
    uint16_t next = seq + 1;
    if (srs_rtp_seq_distance(queue_->end, seq) > 0) {
        seq = queue_->end;
    }
    srs_trace("nack drop seq=%u, drop range [%u, %u, %u]", seq, queue_->begin, next, queue_->end);

    queue_->advance_to(next);
}

void SrsRtpAudioQueue::notify_nack_list_full()
{
    // TODO: FIXME: Maybe we should not drop all packets.
    queue_->advance_to(queue_->end);
}

srs_error_t SrsRtpAudioQueue::consume(SrsRtpNackForReceiver* nack, SrsRtpPacket2* pkt)
{
    return SrsRtpQueue::consume(nack, pkt);
}

void SrsRtpAudioQueue::collect_frames(SrsRtpNackForReceiver* nack, vector<SrsRtpPacket2*>& frames)
{
    // When done, next point to the next available packet.
    uint16_t next = queue_->begin;

    // If nack disabled, we ignore any empty packet.
    if (!nack) {
        for (; next != queue_->end; ++next) {
            SrsRtpPacket2* pkt = queue_->at(next);
            if (pkt) {
                frames.push_back(pkt);
            }
        }
    } else {
        for (; next != queue_->end; ++next) {
            SrsRtpPacket2* pkt = queue_->at(next);

            // TODO: FIXME: Should not wait for NACK packets.
            // Not found or in NACK, stop collecting frame.
            if (!pkt || nack->find(next) != NULL) {
                srs_trace("wait for nack seq=%u", next);
                break;
            }

            frames.push_back(pkt);
        }
    }

    // Reap packets from begin to next.
    if (next != queue_->begin) {
        // Reset the range of packets to NULL in buffer.
        queue_->reset(queue_->begin, next);

        srs_verbose("RTC collect audio [%u, %u, %u]", queue_->begin, next, queue_->end);
        queue_->advance_to(next);
    }

    // For audio, if overflow, clear all packets.
    // TODO: FIXME: Should notify nack?
    if (queue_->overflow()) {
        queue_->advance_to(queue_->end);
    }
}

SrsRtpVideoQueue::SrsRtpVideoQueue(int capacity) : SrsRtpQueue(capacity)
{
    request_key_frame_ = false;
}

SrsRtpVideoQueue::~SrsRtpVideoQueue()
{
}

void SrsRtpVideoQueue::notify_drop_seq(uint16_t seq)
{
    // If not found start frame, return the end, and we will clear queue.
    uint16_t next = next_start_of_frame(seq);
    srs_trace("nack drop seq=%u, drop range [%u, %u, %u]", seq, queue_->begin, next, queue_->end);

    queue_->advance_to(next);
}

void SrsRtpVideoQueue::notify_nack_list_full()
{
    // If not found start frame, return the end, and we will clear queue.
    uint16_t next = next_keyframe();
    srs_trace("nack overflow, drop range [%u, %u, %u]", queue_->begin, next, queue_->end);

    queue_->advance_to(next);
}

srs_error_t SrsRtpVideoQueue::consume(SrsRtpNackForReceiver* nack, SrsRtpPacket2* pkt)
{
    srs_error_t err = srs_success;

    uint8_t v = (uint8_t)pkt->nalu_type;
    if (v == kFuA) {
        SrsRtpFUAPayload2* payload = dynamic_cast<SrsRtpFUAPayload2*>(pkt->payload);
        if (!payload) {
            srs_freep(pkt);
            return srs_error_new(ERROR_RTC_RTP_MUXER, "FU-A payload");
        }

        pkt->video_is_first_packet = payload->start;
        pkt->video_is_last_packet = payload->end;
        pkt->video_is_idr = (payload->nalu_type == SrsAvcNaluTypeIDR);
    } else {
        pkt->video_is_first_packet = true;
        pkt->video_is_last_packet = true;

        if (v == kStapA) {
            pkt->video_is_idr = true;
        } else {
            pkt->video_is_idr = (pkt->nalu_type == SrsAvcNaluTypeIDR);
        }
    }

    if ((err = SrsRtpQueue::consume(nack, pkt)) != srs_success) {
        return srs_error_wrap(err, "video consume");
    }

    return err;
}

void SrsRtpVideoQueue::collect_frames(SrsRtpNackForReceiver* nack, std::vector<SrsRtpPacket2*>& frames)
{
    while (true) {
        SrsRtpPacket2* pkt = NULL;

        collect_frame(nack, &pkt);

        if (!pkt) {
            break;
        }

        frames.push_back(pkt);
    }

    if (queue_->overflow()) {
        on_overflow(nack);
    }
}

bool SrsRtpVideoQueue::should_request_key_frame()
{
    if (request_key_frame_) {
        request_key_frame_ = false;
        return true;
    }

    return request_key_frame_;
}

void SrsRtpVideoQueue::request_keyframe()
{
    request_key_frame_ = true;
}

void SrsRtpVideoQueue::on_overflow(SrsRtpNackForReceiver* nack)
{
    // If not found start frame, return the end, and we will clear queue.
    uint16_t next = next_start_of_frame(queue_->begin);
    srs_trace("on overflow, remove range [%u, %u, %u]", queue_->begin, next, queue_->end);

    for (uint16_t s = queue_->begin; s != next; ++s) {
        if (nack) {
            nack->remove(s);
        }
        queue_->remove(s);
    }

    queue_->advance_to(next);
}

// TODO: FIXME: Should refer to the FU-A original video frame, to avoid finding for each packet.
void SrsRtpVideoQueue::collect_frame(SrsRtpNackForReceiver* nack, SrsRtpPacket2** ppkt)
{
    bool found = false;
    vector<SrsRtpPacket2*> frame;

    // When done, next point to the next available packet.
    uint16_t next = queue_->begin;

    // If nack disabled, we ignore any empty packet.
    if (!nack) {
        for (; next != queue_->end; ++next) {
            SrsRtpPacket2* pkt = queue_->at(next);
            if (!pkt) {
                continue;
            }

            if (frame.empty() && !pkt->video_is_first_packet) {
                continue;
            }

            frame.push_back(pkt);

            if (pkt->rtp_header.get_marker() || pkt->video_is_last_packet) {
                found = true;
                next++;
                break;
            }
        }
    } else {
        for (; next != queue_->end; ++next) {
            SrsRtpPacket2* pkt = queue_->at(next);

            // TODO: FIXME: Should not wait for NACK packets.
            // Not found or in NACK, stop collecting frame.
            if (!pkt || nack->find(next) != NULL) {
                srs_trace("wait for nack seq=%u", next);
                return;
            }

            // Ignore when the first packet not the start.
            if (frame.empty() && !pkt->video_is_first_packet) {
                return;
            }

            // OK, collect packet to frame.
            frame.push_back(pkt);

            // Done, we got the last packet of frame.
            // @remark Note that the STAP-A is marker false and it's the last packet.
            if (pkt->rtp_header.get_marker() || pkt->video_is_last_packet) {
                found = true;
                next++;
                break;
            }
        }
    }

    if (!found || frame.empty()) {
        return;
    }

    if (next != queue_->begin) {
        // Reset the range of packets to NULL in buffer.
        queue_->reset(queue_->begin, next);

        srs_verbose("RTC collect video [%u, %u, %u]", queue_->begin, next, queue_->end);
        queue_->advance_to(next);
    }

    // Merge packets to one packet.
    covert_frame(frame, ppkt);
    return;
}

void SrsRtpVideoQueue::covert_frame(std::vector<SrsRtpPacket2*>& frame, SrsRtpPacket2** ppkt)
{
    if (frame.size() == 1) {
        *ppkt = frame[0];
        return;
    }

    // If more than one packet in a frame, it must be FU-A.
    SrsRtpPacket2* head = frame.at(0);
    SrsAvcNaluType nalu_type = head->nalu_type;

    // Covert FU-A to one RAW RTP packet.
    int nn_nalus = 0;
    for (size_t i = 0; i < frame.size(); ++i) {
        SrsRtpPacket2* pkt = frame[i];
        SrsRtpFUAPayload2* payload = dynamic_cast<SrsRtpFUAPayload2*>(pkt->payload);
        if (!payload) {
            nn_nalus = 0;
            break;
        }
        nn_nalus += payload->size;
    }

    // Invalid packets, ignore.
    if (nalu_type != (SrsAvcNaluType)kFuA || !nn_nalus) {
        for (int i = 0; i < (int)frame.size(); i++) {
            SrsRtpPacket2* pkt = frame[i];
            srs_freep(pkt);
        }
        return;
    }

    // Merge to one RAW RTP packet.
    // TODO: FIXME: Should covert to multiple NALU RTP packet to avoid copying.
    SrsRtpPacket2* pkt = new SrsRtpPacket2();
    pkt->rtp_header = head->rtp_header;
    pkt->padding = head->padding;

    SrsRtpFUAPayload2* head_payload = dynamic_cast<SrsRtpFUAPayload2*>(head->payload);
    pkt->nalu_type = head_payload->nalu_type;

    SrsRtpRawPayload* payload = pkt->reuse_raw();
    payload->nn_payload = nn_nalus + 1;
    payload->payload = new char[payload->nn_payload];

    SrsBuffer buf(payload->payload, payload->nn_payload);

    buf.write_1bytes(head_payload->nri | head_payload->nalu_type); // NALU header.

    for (size_t i = 0; i < frame.size(); ++i) {
        SrsRtpPacket2* pkt = frame[i];
        SrsRtpFUAPayload2* payload = dynamic_cast<SrsRtpFUAPayload2*>(pkt->payload);
        buf.write_bytes(payload->payload, payload->size);
    }

    *ppkt = pkt;
}

uint16_t SrsRtpVideoQueue::next_start_of_frame(uint16_t seq)
{
    uint16_t s = seq;
    if (srs_rtp_seq_distance(seq, queue_->begin) >= 0) {
        s = queue_->begin + 1;
    }

    for (; s != queue_->end; ++s) {
        SrsRtpPacket2* pkt = queue_->at(s);
        if (pkt && pkt->video_is_first_packet) {
            return s;
        }
    }

    return queue_->end;
}

uint16_t SrsRtpVideoQueue::next_keyframe()
{
    uint16_t s = queue_->begin + 1;

    for (; s != queue_->end; ++s) {
        SrsRtpPacket2* pkt = queue_->at(s);
        if (pkt && pkt->video_is_idr && pkt->video_is_first_packet) {
            return s;
        }
    }

    return queue_->end;
}

