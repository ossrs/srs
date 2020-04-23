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

class SrsRtpSharedPacket;
class SrsRtpQueue;

struct SrsNackOption
{
    SrsNackOption()
    {
        // Default nack option.
        max_count = 5;
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

inline bool seq_cmp(const uint16_t& l, const uint16_t& r)
{
    return ((int16_t)(r - l)) > 0;
}

struct SeqComp
{   
    bool operator()(const uint16_t& l, const uint16_t& r) const
    {   
        return seq_cmp(l, r);
    }   
};

class SrsRtpNackList
{
private:
    // Nack queue, seq order, oldest to newest.
    std::map<uint16_t, SrsRtpNackInfo, SeqComp> queue_;
    SrsRtpQueue* rtp_queue_;
    SrsNackOption opts_;
private:
    srs_utime_t pre_check_time_;
private:
    int rtt_;
public:
    SrsRtpNackList(SrsRtpQueue* rtp_queue);
    virtual ~SrsRtpNackList();
public:
    void insert(uint16_t seq);
    void remove(uint16_t seq);
    SrsRtpNackInfo* find(uint16_t seq);
public:
    void dump();
public:
    void get_nack_seqs(std::vector<uint16_t>& seqs);
public:
    void update_rtt(int rtt);
};

class SrsRtpQueue
{
private:
    /*
     *[seq1|seq2|seq3|seq4|seq5 ... seq10|seq11(loss)|seq12(loss)|seq13]
     *             \___(head_sequence_)      \                      \___(highest_sequence_)
     *                                        \___(no received, in nack list)
     */
    // Capacity of the ring-buffer.
    size_t   capacity_;
    // Thei highest sequence we have receive.
    uint16_t highest_sequence_;
    // The sequence waitting to read.
    uint16_t head_sequence_;
    bool initialized_;
    bool start_collected_;
    // Ring bufer.
    SrsRtpSharedPacket** queue_;
private:
    uint64_t cycle_;
    double jitter_;
    int64_t last_trans_time_;
    uint64_t pre_number_of_packet_received_;
    uint64_t pre_number_of_packet_lossed_;
    uint64_t num_of_packet_received_;
    uint64_t number_of_packet_lossed_;
private:
    bool one_packet_per_frame_;
public:
    SrsRtpNackList nack_;
private:
    std::vector<std::vector<SrsRtpSharedPacket*> > frames_;
public:
    SrsRtpQueue(size_t capacity = 1024, bool one_packet_per_frame = false);
    virtual ~SrsRtpQueue();
public:
    srs_error_t insert(SrsRtpSharedPacket* rtp_pkt);
    srs_error_t remove(uint16_t seq);
public:
    void get_and_clean_collected_frames(std::vector<std::vector<SrsRtpSharedPacket*> >& frames);
    void notify_drop_seq(uint16_t seq);
public:
    uint32_t get_extended_highest_sequence();
    uint8_t get_fraction_lost();
    uint32_t get_cumulative_number_of_packets_lost();
    uint32_t get_interarrival_jitter();
public:
    void update_rtt(int rtt);
private:
    void insert_into_nack_list(uint16_t seq_start, uint16_t seq_end);
private:
    void collect_packet();
};

#endif
