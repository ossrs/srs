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
inline bool srs_rtp_seq_distance(const uint16_t& low, const uint16_t& high)
{
    return ((int16_t)(high - low)) > 0;
}

class SrsRtpNackForReceiver
{
private:
    struct SeqComp {
        bool operator()(const uint16_t& low, const uint16_t& high) const {
            return srs_rtp_seq_distance(low, high);
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
class SrsRtpRingBuffer
{
private:
    // Capacity of the ring-buffer.
    uint16_t capacity_;
    // Ring bufer.
    SrsRtpPacket2** queue_;
    // Increase one when uint16 flip back, for get_extended_highest_sequence.
    uint64_t nn_seq_flip_backs;
    // Whether initialized, because we use uint16 so we can't use -1.
    bool initialized_;
private:
    // Current position we are working at.
    uint16_t low_;
    uint16_t high_;
public:
    SrsRtpRingBuffer(int capacity);
    virtual ~SrsRtpRingBuffer();
public:
    // Move the position of buffer.
    uint16_t low();
    uint16_t high();
    void advance_to(uint16_t seq);
    // Free the packet at position.
    void set(uint16_t at, SrsRtpPacket2* pkt);
    void remove(uint16_t at);
    // Directly reset range [low, high] to NULL.
    void reset(uint16_t low, uint16_t high);
    // Whether queue overflow or heavy(too many packets and need clear).
    bool overflow();
    bool is_heavy();
    // Get the next start packet of frame.
    // @remark If not found, return the low_, which should never be the "next" one,
    // because it MAY or NOT current start packet of frame but never be the next.
    uint16_t next_start_of_frame();
    // Get the next seq of keyframe.
    // @remark Return low_ if not found.
    uint16_t next_keyframe();
    // The highest sequence number, calculate the flip back base.
    uint32_t get_extended_highest_sequence();
    // Update the sequence, got the nack range by [low, high].
    void update(uint16_t seq, bool startup, uint16_t& nack_low, uint16_t& nack_high);
    // Get the packet by seq.
    SrsRtpPacket2* at(uint16_t seq);
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
    SrsRtpRingBuffer* queue_;
    uint64_t nn_collected_frames;
    std::vector<std::vector<SrsRtpPacket2*> > frames_;
    const char* tag_;
private:
    bool request_key_frame_;
public:
    SrsRtpQueue(const char* tag, int capacity);
    virtual ~SrsRtpQueue();
public:
    virtual srs_error_t consume(SrsRtpNackForReceiver* nack, SrsRtpPacket2* pkt);
    // TODO: FIXME: Should merge FU-A to RAW, then we can return RAW payloads.
    void collect_frames(std::vector<std::vector<SrsRtpPacket2*> >& frames);
    bool should_request_key_frame();
    void notify_drop_seq(uint16_t seq);
    void notify_nack_list_full();
    void request_keyframe();
public:
    uint32_t get_extended_highest_sequence();
    uint8_t get_fraction_lost();
    uint32_t get_cumulative_number_of_packets_lost();
    uint32_t get_interarrival_jitter();
private:
    void insert_into_nack_list(SrsRtpNackForReceiver* nack, uint16_t seq_start, uint16_t seq_end);
protected:
    virtual void collect_packet(SrsRtpNackForReceiver* nack) = 0;
};

class SrsRtpAudioQueue : public SrsRtpQueue
{
public:
    SrsRtpAudioQueue(int capacity);
    virtual ~SrsRtpAudioQueue();
public:
    virtual srs_error_t consume(SrsRtpNackForReceiver* nack, SrsRtpPacket2* pkt);
protected:
    virtual void collect_packet(SrsRtpNackForReceiver* nack);
};

class SrsRtpVideoQueue : public SrsRtpQueue
{
public:
    SrsRtpVideoQueue(int capacity);
    virtual ~SrsRtpVideoQueue();
public:
    virtual srs_error_t consume(SrsRtpNackForReceiver* nack, SrsRtpPacket2* pkt);
protected:
    virtual void collect_packet(SrsRtpNackForReceiver* nack);
private:
    virtual void do_collect_packet(SrsRtpNackForReceiver* nack, std::vector<SrsRtpPacket2*>& frame);
};

#endif
