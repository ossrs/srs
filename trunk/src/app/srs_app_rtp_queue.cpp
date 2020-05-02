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
    SrsRtpNackInfo& nack_info = queue_[seq];
    (void)nack_info;
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
        srs_verbose("NACK list full, queue size=%u, max_queue_size=%u", queue_.size(), max_queue_size_);
        rtp_queue_->notify_nack_list_full();
    }
}

void SrsRtpNackForReceiver::get_nack_seqs(vector<uint16_t>& seqs)
{
    srs_utime_t now = srs_update_system_time();
    int interval = now - pre_check_time_;
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
            srs_verbose("NACK, drop seq=%u alive time %d bigger than max_alive_time=%d OR nack count %d bigger than %d",
                seq, alive_time, opts_.max_alive_time, nack_info.req_nack_count_, opts_.max_count);
                
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
            srs_verbose("NACK, resend seq=%u, count=%d", seq, nack_info.req_nack_count_);
        }

        ++iter;
    }
}

void SrsRtpNackForReceiver::update_rtt(int rtt)
{
    rtt_ = rtt * SRS_UTIME_MILLISECONDS;
    srs_verbose("NACK, update rtt from %ld to %d", opts_.nack_interval, rtt_);
    // FIXME: limit min and max value.
    opts_.nack_interval = rtt_;
}

SrsRtpRingBuffer::SrsRtpRingBuffer(size_t capacity)
{
    nn_seq_flip_backs = 0;
    high_ = low_ = 0;
    capacity_ = capacity;
    initialized_ = false;

    queue_ = new SrsRtpPacket2*[capacity_];
    memset(queue_, 0, sizeof(SrsRtpPacket2*) * capacity);
}

SrsRtpRingBuffer::~SrsRtpRingBuffer()
{
    srs_freepa(queue_);
}

uint16_t SrsRtpRingBuffer::low()
{
    return low_;
}

uint16_t SrsRtpRingBuffer::high()
{
    return high_;
}

void SrsRtpRingBuffer::advance_to(uint16_t seq)
{
    low_ = seq;
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

void SrsRtpRingBuffer::reset(uint16_t low, uint16_t high)
{
    for (uint16_t s = low; s != high; ++s) {
        queue_[s % capacity_] = NULL;
    }
}

bool SrsRtpRingBuffer::overflow()
{
    return high_ - low_ < capacity_;
}

bool SrsRtpRingBuffer::is_heavy()
{
    return high_ - low_ >= capacity_ / 2;
}

uint16_t SrsRtpRingBuffer::next_start_of_frame()
{
    if (low_ == high_) {
        return low_;
    }

    for (uint16_t s = low_ + 1 ; s != high_; ++s) {
        SrsRtpPacket2*& pkt = queue_[s % capacity_];
        if (pkt && pkt->is_first_packet_of_frame) {
            return s;
        }
    }

    return low_;
}

uint16_t SrsRtpRingBuffer::next_keyframe()
{
    if (low_ == high_) {
        return low_;
    }

    for (uint16_t s = low_ + 1 ; s != high_; ++s) {
        SrsRtpPacket2*& pkt = queue_[s % capacity_];
        if (pkt && pkt->is_key_frame && pkt->is_first_packet_of_frame) {
            return s;
        }
    }

    return low_;
}

uint32_t SrsRtpRingBuffer::get_extended_highest_sequence()
{
    return nn_seq_flip_backs * 65536 + high_;
}

void SrsRtpRingBuffer::update(uint16_t seq, bool startup, uint16_t& nack_low, uint16_t& nack_high)
{
    if (!initialized_) {
        initialized_ = true;
        low_ = high_ = seq;
        return;
    }

    // Normal sequence, seq follows high_.
    if (srs_rtp_seq_distance(high_, seq)) {
        nack_low = high_ + 1;
        nack_high = seq;

        // When distance(seq,high_)>0 and seq<high_, seq must flip back,
        // for example, high_=65535, seq=1, distance(65535,1)>0 and 1<65535.
        if (seq < high_) {
            srs_verbose("warp around, flip_back=%" PRId64, nn_seq_flip_backs);
            ++nn_seq_flip_backs;
        }
        high_ = seq;
        return;
    }

    // Out-of-order sequence, seq before low_.
    if (srs_rtp_seq_distance(seq, low_)) {
        // When startup, we may receive packets in chaos order.
        // Because we don't know the ISN(initiazlie sequence number), the first packet
        // we received maybe no the first packet client sent.
        if (startup) {
            nack_low = seq + 1;
            nack_high = low_;

            srs_info("head seq=%u, cur seq=%u, update head seq because recv less than it.", low_, seq);
            low_ = seq;
        } else {
            srs_verbose("seq=%u, rtx success, too old", seq);
        }
    }
}

SrsRtpPacket2* SrsRtpRingBuffer::at(uint16_t seq)
{
    return queue_[seq % capacity_];
}

SrsRtpQueue::SrsRtpQueue(size_t capacity, bool one_packet_per_frame)
{
    nn_collected_frames = 0;
    queue_ = new SrsRtpRingBuffer(capacity);

    jitter_ = 0;
    last_trans_time_ = -1;
    
    pre_number_of_packet_received_ = 0;
    pre_number_of_packet_lossed_ = 0;

    num_of_packet_received_ = 0;
    number_of_packet_lossed_ = 0;

    one_packet_per_frame_ = one_packet_per_frame;

    request_key_frame_ = false;
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
    SrsRtpNackInfo* nack_info = nack->find(seq);
    if (nack_info) {
        int nack_rtt = nack_info->req_nack_count_ ? ((now - nack_info->pre_req_nack_time_) / SRS_UTIME_MILLISECONDS) : 0;
        (void)nack_rtt;
        srs_verbose("seq=%u, alive time=%d, nack count=%d, rtx success, resend use %dms",
            seq, now - nack_info->generate_time_, nack_info->req_nack_count_, nack_rtt);
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
        srs_verbose("jitter=%.2f", jitter_);
    }

    // OK, we got one new RTP packet, which is not in NACK.
    if (!nack_info) {
        ++num_of_packet_received_;
        uint16_t nack_low = 0, nack_high = 0;
        queue_->update(seq, !nn_collected_frames, nack_low, nack_high);
        if (srs_rtp_seq_distance(nack_low, nack_high)) {
            srs_trace("update nack seq=%u, startup=%d, nack range [%u, %u]", seq, !nn_collected_frames, nack_low, nack_high);
            insert_into_nack_list(nack, nack_low, nack_high);
        }
    }

    // When packets overflow, collect frame and move head to next frame start.
    if (queue_->overflow()) {
        srs_verbose("try collect packet becuase seq out of range");
        collect_packet(nack);

        uint16_t next = queue_->next_start_of_frame();

        // Note that low_ mean not found, clear queue util one packet.
        if (next == queue_->low()) {
            next = queue_->high() - 1;
        }
        srs_trace("seqs out of range, seq range [%u, %u]", queue_->low(), next);

        for (uint16_t s = queue_->low(); s != next; ++s) {
            nack->remove(s);
            queue_->remove(s);
        }

        srs_trace("force update, update head seq from %u to %u when seqs out of range", queue_->low(), next + 1);
        queue_->advance_to(next + 1);
    }

    // Save packet at the position seq.
    queue_->set(seq, pkt);

    // Collect packets to frame when:
    // 1. Marker bit means the last packet of frame received.
    // 2. Queue has lots of packets, the load is heavy.
    // 3. The frame contains only one packet for each frame.
    if (pkt->rtp_header.get_marker() || queue_->is_heavy() || one_packet_per_frame_) {
        collect_packet(nack);
    }

    return err;
}

void SrsRtpQueue::collect_frames(std::vector<std::vector<SrsRtpPacket2*> >& frames)
{
    frames.swap(frames_);
}

bool SrsRtpQueue::should_request_key_frame()
{
    if (request_key_frame_) {
        request_key_frame_ = false;
        return true;
    }

    return request_key_frame_;
}

void SrsRtpQueue::notify_drop_seq(uint16_t seq)
{
    uint16_t next = queue_->next_start_of_frame();

    // Note that low_ mean not found, clear queue util one packet.
    if (next == queue_->low()) {
        next = queue_->high() - 1;
    }

    // When NACK is timeout, move to the next start of frame.
    srs_trace("nack drop seq=%u, drop range [%u, %u]", seq, queue_->low(), next + 1);
    queue_->advance_to(next + 1);
}

void SrsRtpQueue::notify_nack_list_full()
{
    uint16_t next = queue_->next_keyframe();

    // Note that low_ mean not found, clear queue util one packet.
    if (next == queue_->low()) {
        next = queue_->high() - 1;
    }

    // When NACK is overflow, move to the next keyframe.
    srs_trace("nack overflow drop range [%u, %u]", queue_->low(), next + 1);
    queue_->advance_to(next + 1);
}

void SrsRtpQueue::request_keyframe()
{
    request_key_frame_ = true;
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

void SrsRtpQueue::insert_into_nack_list(SrsRtpNackForReceiver* nack, uint16_t seq_start, uint16_t seq_end)
{
    for (uint16_t s = seq_start; s != seq_end; ++s) {
        srs_verbose("loss seq=%u, insert into nack list", s);
        nack->insert(s);
        ++number_of_packet_lossed_;
    }

    nack->check_queue_size();
}

void SrsRtpQueue::collect_packet(SrsRtpNackForReceiver* nack)
{
    while (queue_->low() != queue_->high()) {
        vector<SrsRtpPacket2*> frame;

        uint16_t s = queue_->low();
        for (; s != queue_->high(); ++s) {
            SrsRtpPacket2* pkt = queue_->at(s);

            // In NACK, never collect frame.
            if (nack->find(s) != NULL) {
                srs_verbose("seq=%u, found in nack list when collect frame", s);
                return;
            }

            // Ignore when the first packet not the start.
            if (s == queue_->low() && pkt->nn_original_payload && !pkt->is_first_packet_of_frame) {
                return;
            }

            // OK, collect packet to frame.
            frame.push_back(pkt);

            // Not the last packet, continue to process next one.
            if (!pkt->rtp_header.get_marker() && !one_packet_per_frame_) {
                continue;
            }

            // Done, we got the last packet of frame.
            nn_collected_frames++;
            frames_.push_back(frame);
            break;
        }

        if (queue_->low() != s) {
            // Reset the range of packets to NULL in buffer.
            queue_->reset(queue_->low(), s);

            srs_verbose("head seq=%u, update to %u because collect one full farme", queue_->low(), s + 1);
            queue_->advance_to(s + 1);
        }
    }
}
