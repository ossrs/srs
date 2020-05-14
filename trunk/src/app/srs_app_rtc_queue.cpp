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

#include <srs_app_rtc_queue.hpp>

#include <string.h>
#include <unistd.h>
#include <sstream>

using namespace std;

#include <srs_kernel_error.hpp>
#include <srs_kernel_rtc_rtp.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_app_utility.hpp>

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

bool SrsRtpRingBuffer::overflow()
{
    return srs_rtp_seq_distance(begin, end) >= capacity_;
}

uint32_t SrsRtpRingBuffer::get_extended_highest_sequence()
{
    return nn_seq_flip_backs * 65536 + end - 1;
}

bool SrsRtpRingBuffer::update(uint16_t seq, uint16_t& nack_first, uint16_t& nack_last)
{
    if (!initialized_) {
        initialized_ = true;
        begin = seq;
        end = seq + 1;
        return true;
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
        return true;
    }

    // Out-of-order sequence, seq before low_.
    if (srs_rtp_seq_distance(seq, begin) > 0) {
        // When startup, we may receive packets in chaos order.
        // Because we don't know the ISN(initiazlie sequence number), the first packet
        // we received maybe no the first packet client sent.
        // @remark We only log a warning, because it seems ok for publisher.
        return false;
    }

    return true;
}

SrsRtpPacket2* SrsRtpRingBuffer::at(uint16_t seq) {
    return queue_[seq % capacity_];
}

void SrsRtpRingBuffer::notify_nack_list_full()
{
}

void SrsRtpRingBuffer::notify_drop_seq(uint16_t seq)
{
}

SrsRtpNackInfo::SrsRtpNackInfo()
{
    generate_time_ = srs_update_system_time();
    pre_req_nack_time_ = 0;
    req_nack_count_ = 0;
}

SrsRtpNackForReceiver::SrsRtpNackForReceiver(SrsRtpRingBuffer* rtp, size_t queue_size)
{
    max_queue_size_ = queue_size;
    rtp_ = rtp;
    pre_check_time_ = 0;

    srs_info("max_queue_size=%u, nack opt: max_count=%d, max_alive_time=%us, first_nack_interval=%" PRId64 ", nack_interval=%" PRId64,
        max_queue_size_, opts_.max_count, opts_.max_alive_time, opts.first_nack_interval, opts_.nack_interval);
}

SrsRtpNackForReceiver::~SrsRtpNackForReceiver()
{
}

void SrsRtpNackForReceiver::insert(uint16_t first, uint16_t last)
{
    for (uint16_t s = first; s != last; ++s) {
        queue_[s] = SrsRtpNackInfo();
    }
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
        rtp_->notify_nack_list_full();
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
            rtp_->notify_drop_seq(seq);
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

