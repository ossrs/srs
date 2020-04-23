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

SrsRtpNackList::SrsRtpNackList(SrsRtpQueue* rtp_queue)
{
    rtp_queue_ = rtp_queue;
    pre_check_time_ = 0;
    
    srs_info("nack opt: max_count=%d, max_alive_time=%us, first_nack_interval=%ld, nack_interval=%ld"
        opts_.max_count, opts_.max_alive_time, opts.first_nack_interval, opts_.nack_interval);
}

SrsRtpNackList::~SrsRtpNackList()
{
}

void SrsRtpNackList::insert(uint16_t seq)
{
    // FIXME: full, drop packet, and request key frame.
    SrsRtpNackInfo& nack_info = queue_[seq];
}

void SrsRtpNackList::remove(uint16_t seq)
{
    queue_.erase(seq);
}

SrsRtpNackInfo* SrsRtpNackList::find(uint16_t seq)
{
    std::map<uint16_t, SrsRtpNackInfo>::iterator iter = queue_.find(seq);
    
    if (iter == queue_.end()) {
        return NULL;
    }

    return &(iter->second);
}

void SrsRtpNackList::dump()
{
    return;
    srs_verbose("@debug, queue size=%u", queue_.size());
    for (std::map<uint16_t, SrsRtpNackInfo>::iterator iter = queue_.begin(); iter != queue_.end(); ++iter) {
        srs_verbose("@debug, nack seq=%u", iter->first);
    }
}

void SrsRtpNackList::get_nack_seqs(vector<uint16_t>& seqs)
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

void SrsRtpNackList::update_rtt(int rtt)
{
    rtt_ = rtt * SRS_UTIME_MILLISECONDS;
    srs_verbose("NACK, update rtt from %ld to %d", opts_.nack_interval, rtt_);
    // FIXME: limit min and max value.
    opts_.nack_interval = rtt_;
}

SrsRtpQueue::SrsRtpQueue(size_t capacity, bool one_packet_per_frame)
    : nack_(this)
{
    capacity_ = capacity;
    head_sequence_ = 0;
    highest_sequence_ = 0;
    initialized_ = false;
    start_collected_ = false;
    queue_ = new SrsRtpSharedPacket*[capacity_];
    memset(queue_, 0, sizeof(SrsRtpSharedPacket*) * capacity);

    cycle_ = 0;
    jitter_ = 0;
    last_trans_time_ = 0;
    
    pre_number_of_packet_received_ = 0;
    pre_number_of_packet_lossed_ = 0;

    num_of_packet_received_ = 0;
    number_of_packet_lossed_ = 0;

    one_packet_per_frame_ = one_packet_per_frame;
}

SrsRtpQueue::~SrsRtpQueue()
{
    srs_freepa(queue_);
}

srs_error_t SrsRtpQueue::insert(SrsRtpSharedPacket* rtp_pkt)
{
    srs_error_t err = srs_success;

    uint16_t seq = rtp_pkt->rtp_header.get_sequence();

    srs_utime_t now = srs_update_system_time();

    // First packet recv, init head_sequence and highest_sequence.
    if (! initialized_) {
        initialized_ = true;
        head_sequence_ = seq;
        highest_sequence_ = seq;

        ++num_of_packet_received_;

        last_trans_time_ = now/1000 - rtp_pkt->rtp_header.get_timestamp()/90;
    } else {
        SrsRtpNackInfo* nack_info = NULL;
        if ((nack_info = nack_.find(seq)) != NULL) {
            int nack_rtt = nack_info->req_nack_count_ ? ((now - nack_info->pre_req_nack_time_) / SRS_UTIME_MILLISECONDS) : 0;
            srs_verbose("seq=%u, alive time=%d, nack count=%d, rtx success, resend use %dms", 
                seq, now - nack_info->generate_time_, nack_info->req_nack_count_, nack_rtt);
            nack_.remove(seq);
        } else {
            // Calc jitter.
            {
                int trans_time = now/1000 - rtp_pkt->rtp_header.get_timestamp()/90;

                int cur_jitter = trans_time - last_trans_time_;
                if (cur_jitter < 0) {
                    cur_jitter = -cur_jitter;
                }

                last_trans_time_ = trans_time;

                jitter_ = (jitter_ * 15.0 / 16.0) + (static_cast<double>(cur_jitter) / 16.0);
                srs_verbose("jitter=%.2f", jitter_);
            }

            ++num_of_packet_received_;
            // seq > highest_sequence_
            if (seq_cmp(highest_sequence_, seq)) {
                insert_into_nack_list(highest_sequence_ + 1, seq);

                if (seq < highest_sequence_) {
                    srs_verbose("warp around, cycle=%lu", cycle_);
                    ++cycle_;
                }
                highest_sequence_ = seq;
            } else {
                // Because we don't know the ISN(initiazlie sequence number), the first packet
                // we received maybe no the first paacet client sented.
                if (! start_collected_) {
                    if (seq_cmp(seq, head_sequence_)) {
                        srs_info("head seq=%u, cur seq=%u, update head seq because recv less than it.", head_sequence_, seq);
                        head_sequence_ = seq;
                    }
                    insert_into_nack_list(seq + 1, highest_sequence_);
                } else {
                    srs_verbose("seq=%u, rtx success, too old", seq);
                }
            }
        }
    }

    int delay = highest_sequence_ - head_sequence_ + 1;
    srs_verbose("seqs range=[%u-%u], delay=%d", head_sequence_, highest_sequence_, delay);

    // Check seqs out of range.
    if (head_sequence_ + capacity_ < highest_sequence_) {
        srs_verbose("try collect packet becuase seq out of range");
        collect_packet();
    }
    while (head_sequence_ + capacity_ < highest_sequence_) {
        srs_trace("seqs out of range, head seq=%u, hightest seq=%u", head_sequence_, highest_sequence_);
        remove(head_sequence_);
        uint16_t s = head_sequence_ + 1;
        for ( ; s != highest_sequence_; ++s) {
            SrsRtpSharedPacket*& pkt = queue_[s % capacity_];
            // Choose the new head sequence. Must be the first packet of frame.
            if (pkt && pkt->rtp_payload_header->is_first_packet_of_frame) {
                srs_trace("find except, update head seq from %u to %u when seqs out of range", head_sequence_, s);
                head_sequence_ = s;
                break;
            }

            // Drop the seq.
            nack_.remove(s);
            srs_verbose("seqs out of range, drop seq=%u", s);
            if (pkt && pkt->rtp_header.get_sequence() == s) {
                delete pkt;
                pkt = NULL;
            }
        }
        srs_trace("force update, update head seq from %u to %u when seqs out of range", head_sequence_, s);
        head_sequence_ = s;
    }

    SrsRtpSharedPacket* old_pkt = queue_[seq % capacity_];
    if (old_pkt) {
        delete old_pkt;
    }

    queue_[seq % capacity_] = rtp_pkt->copy();

    // Marker bit means the last packet of frame received.
    if (rtp_pkt->rtp_header.get_marker() || (highest_sequence_ - head_sequence_ >= capacity_ / 2) || one_packet_per_frame_) {
        collect_packet();
    }

    return err;
}

srs_error_t SrsRtpQueue::remove(uint16_t seq)
{
    srs_error_t err = srs_success;

    SrsRtpSharedPacket*& pkt = queue_[seq % capacity_];
    if (pkt && pkt->rtp_header.get_sequence() == seq) {
        delete pkt;
        pkt = NULL;
    }

    return err;
}

void SrsRtpQueue::get_and_clean_collected_frames(std::vector<std::vector<SrsRtpSharedPacket*> >& frames)
{
    frames.swap(frames_);
}

void SrsRtpQueue::notify_drop_seq(uint16_t seq)
{
    uint16_t s = seq + 1;
    for ( ; s != highest_sequence_; ++s) {
        SrsRtpSharedPacket* pkt = queue_[s % capacity_];
        if (pkt && pkt->rtp_payload_header->is_first_packet_of_frame) {
            break;
        }
    }

    srs_verbose("drop seq=%u, highest seq=%u, update head seq %u to %u", seq, highest_sequence_, head_sequence_, s);
    head_sequence_ = s;
}

uint32_t SrsRtpQueue::get_extended_highest_sequence()
{
    return cycle_ * 65536 + highest_sequence_;
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

void SrsRtpQueue::update_rtt(int rtt)
{
    nack_.update_rtt(rtt);
}

void SrsRtpQueue::insert_into_nack_list(uint16_t seq_start, uint16_t seq_end)
{
    for (uint16_t s = seq_start; s != seq_end; ++s) {
        srs_verbose("loss seq=%u, insert into nack list", s);
        nack_.insert(s);
        ++number_of_packet_lossed_;
    }

    // FIXME: Record key frame sequence.
    // FIXME: When nack list too long, clear and send PLI.
}

void SrsRtpQueue::collect_packet()
{
    vector<SrsRtpSharedPacket*> frame;
    for (uint16_t s = head_sequence_; s != highest_sequence_; ++s) {
        SrsRtpSharedPacket* pkt = queue_[s % capacity_];

        nack_.dump();

        if (nack_.find(s) != NULL) {
            srs_verbose("seq=%u, found in nack list when collect frame", s);
            break;
        }

        // We must collect frame from first packet to last packet.
        if (s == head_sequence_ && pkt->rtp_payload_size() != 0 && ! pkt->rtp_payload_header->is_first_packet_of_frame) {
            break;
        }

        frame.push_back(pkt->copy());
        if (pkt->rtp_header.get_marker() || one_packet_per_frame_) {
            if (! start_collected_) {
                start_collected_ = true;
            }
            frames_.push_back(frame);
            frame.clear();

            srs_verbose("head seq=%u, update to %u because collect one full farme", head_sequence_, s + 1);
            head_sequence_ = s + 1;
        }
    }

    // remove the tmp buffer
    for (size_t i = 0; i < frame.size(); ++i) {
        srs_freep(frame[i]);
    }
}
