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

#ifndef SRS_APP_RTP_QUEUE_HPP
#define SRS_APP_RTP_QUEUE_HPP

#include <srs_core.hpp>

#include <string>
#include <vector>
#include <map>

class SrsRtpPacket2;
class SrsRtpQueue;

struct SrsNackOption
{
    SrsNackOption()
    {
        // Default nack option.
        max_count = 10;
        max_alive_time = 2 * SRS_UTIME_SECONDS;
        first_nack_interval = 10 * SRS_UTIME_MILLISECONDS;
        nack_interval = 400 * SRS_UTIME_MILLISECONDS;
    }
    int max_count;
    srs_utime_t max_alive_time;
    int64_t first_nack_interval;
    int64_t nack_interval;
};

struct SrsRtpNackInfo
{
    SrsRtpNackInfo();

    // Use to control the time of first nack req and the life of seq.
    srs_utime_t generate_time_;
    // Use to control nack interval.
    srs_utime_t pre_req_nack_time_;
    // Use to control nack times.
    int req_nack_count_;
};

// The "distance" between two uint16 number, for example:
//      distance(low=3, high=5) === (int16_t)(uint16_t)((uint16_t)3-(uint16_t)5) === -2
//      distance(low=3, high=65534) === (int16_t)(uint16_t)((uint16_t)3-(uint16_t)65534) === 5
//      distance(low=65532, high=65534) === (int16_t)(uint16_t)((uint16_t)65532-(uint16_t)65534) === -2
// For RTP sequence, it's only uint16 and may flip back, so 3 maybe 3+0xffff.
inline int16_t srs_rtp_seq_distance(const uint16_t& low, const uint16_t& high)
{
    return (int16_t)(high - low);
}

class SrsRtpNackForReceiver
{
private:
    struct SeqComp {
        bool operator()(const uint16_t& low, const uint16_t& high) const {
            return srs_rtp_seq_distance(low, high) > 0;
        }
    };
private:
    // Nack queue, seq order, oldest to newest.
    std::map<uint16_t, SrsRtpNackInfo, SeqComp> queue_;
    // Max nack count.
    size_t max_queue_size_;
    SrsRtpQueue* rtp_queue_;
    SrsNackOption opts_;
private:
    srs_utime_t pre_check_time_;
private:
    int rtt_;
public:
    SrsRtpNackForReceiver(SrsRtpQueue* rtp_queue, size_t queue_size);
    virtual ~SrsRtpNackForReceiver();
public:
    void insert(uint16_t seq);
    void remove(uint16_t seq);
    SrsRtpNackInfo* find(uint16_t seq);
    void check_queue_size();
public:
    void get_nack_seqs(std::vector<uint16_t>& seqs);
public:
    void update_rtt(int rtt);
};

// For UDP, the packets sequence may present as bellow:
//      [seq1(done)|seq2|seq3 ... seq10|seq11(lost)|seq12|seq13]
//                   \___(head_sequence_)   \               \___(highest_sequence_)
//                                           \___(no received, in nack list)
//      * seq1: The packet is done, we already got the entire frame and processed it.
//      * seq2,seq3,...,seq10,seq12,seq13: We are processing theses packets, for example, some FU-A or NALUs,
//               but not an entire video frame right now.
//      * seq10: This packet is lost or not received, we put it in the nack list.
// We store the received packets in ring buffer.
template<typename T>
class SrsRtpRingBuffer
{
private:
    // Capacity of the ring-buffer.
    uint16_t capacity_;
    // Ring bufer.
    T* queue_;
    // Increase one when uint16 flip back, for get_extended_highest_sequence.
    uint64_t nn_seq_flip_backs;
    // Whether initialized, because we use uint16 so we can't use -1.
    bool initialized_;
public:
    // The begin iterator for ring buffer.
    // For example, when got 1 elems, the begin is 0.
    uint16_t begin;
    // The end iterator for ring buffer.
    // For example, when got 1 elems, the end is 1.
    uint16_t end;
public:
    SrsRtpRingBuffer(int capacity) {
        nn_seq_flip_backs = 0;
        begin = end = 0;
        capacity_ = (uint16_t)capacity;
        initialized_ = false;

        queue_ = new T[capacity_];
        memset(queue_, 0, sizeof(T) * capacity);
    }
    virtual ~SrsRtpRingBuffer() {
        srs_freepa(queue_);
    }
public:
    // Whether the ring buffer is empty.
    bool empty() {
        return begin == end;
    }
    // Get the count of elems in ring buffer.
    int size() {
        int size = srs_rtp_seq_distance(begin, end);
        srs_assert(size >= 0);
        return size;
    }
    // Move the low position of buffer to seq.
    void advance_to(uint16_t seq) {
        begin = seq;
    }
    // Free the packet at position.
    void set(uint16_t at, T pkt) {
        T p = queue_[at % capacity_];

        if (p) {
            srs_freep(p);
        }

        queue_[at % capacity_] = pkt;
    }
    void remove(uint16_t at) {
        set(at, NULL);
    }
    // Directly reset range [first, last) to NULL.
    void reset(uint16_t first, uint16_t last) {
        for (uint16_t s = first; s != last; ++s) {
            queue_[s % capacity_] = NULL;
        }
    }
    // Whether queue overflow or heavy(too many packets and need clear).
    bool overflow() {
        return srs_rtp_seq_distance(begin, end) >= capacity_;
    }
    // The highest sequence number, calculate the flip back base.
    uint32_t get_extended_highest_sequence() {
        return nn_seq_flip_backs * 65536 + end - 1;
    }
    // Update the sequence, got the nack range by [first, last).
    // @return If false, the seq is too old.
    bool update(uint16_t seq, uint16_t& nack_first, uint16_t& nack_last) {
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
    // Get the packet by seq.
    T at(uint16_t seq) {
        return queue_[seq % capacity_];
    }
};

class SrsRtpQueue
{
private:
    double jitter_;
    // TODO: FIXME: Covert time to srs_utime_t.
    int64_t last_trans_time_;
    uint64_t pre_number_of_packet_received_;
    uint64_t pre_number_of_packet_lossed_;
    uint64_t num_of_packet_received_;
    uint64_t number_of_packet_lossed_;
protected:
    SrsRtpRingBuffer<SrsRtpPacket2*>* queue_;
public:
    SrsRtpQueue(int capacity);
    virtual ~SrsRtpQueue();
public:
    virtual srs_error_t consume(SrsRtpNackForReceiver* nack, SrsRtpPacket2* pkt);
    virtual void notify_drop_seq(uint16_t seq) = 0;
    virtual void notify_nack_list_full() = 0;
public:
    uint32_t get_extended_highest_sequence();
    uint8_t get_fraction_lost();
    uint32_t get_cumulative_number_of_packets_lost();
    uint32_t get_interarrival_jitter();
private:
    void insert_into_nack_list(SrsRtpNackForReceiver* nack, uint16_t first, uint16_t last);
};

class SrsRtpAudioQueue : public SrsRtpQueue
{
public:
    SrsRtpAudioQueue(int capacity);
    virtual ~SrsRtpAudioQueue();
public:
    virtual void notify_drop_seq(uint16_t seq);
    virtual void notify_nack_list_full();
    virtual srs_error_t consume(SrsRtpNackForReceiver* nack, SrsRtpPacket2* pkt);
    virtual void collect_frames(SrsRtpNackForReceiver* nack, std::vector<SrsRtpPacket2*>& frames);
};

class SrsRtpVideoQueue : public SrsRtpQueue
{
private:
    bool request_key_frame_;
public:
    SrsRtpVideoQueue(int capacity);
    virtual ~SrsRtpVideoQueue();
public:
    virtual void notify_drop_seq(uint16_t seq);
    virtual void notify_nack_list_full();
    virtual srs_error_t consume(SrsRtpNackForReceiver* nack, SrsRtpPacket2* pkt);
    virtual void collect_frames(SrsRtpNackForReceiver* nack, std::vector<SrsRtpPacket2*>& frame);
    bool should_request_key_frame();
    void request_keyframe();
private:
    virtual void on_overflow(SrsRtpNackForReceiver* nack);
    virtual void collect_frame(SrsRtpNackForReceiver* nack, SrsRtpPacket2** ppkt);
    virtual void covert_frame(std::vector<SrsRtpPacket2*>& frame, SrsRtpPacket2** ppkt);
    uint16_t next_start_of_frame(uint16_t seq);
    uint16_t next_keyframe();
};

#endif
