//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#ifndef SRS_APP_RTC_QUEUE_HPP
#define SRS_APP_RTC_QUEUE_HPP

#include <srs_core.hpp>

#include <string>
#include <vector>
#include <map>

#include <srs_kernel_rtc_rtp.hpp>
#include <srs_kernel_rtc_rtcp.hpp>

class SrsRtpPacket;
class SrsRtpQueue;
class SrsRtpRingBuffer;

// For UDP, the packets sequence may present as bellow:
//      [seq1(done)|seq2|seq3 ... seq10|seq11(lost)|seq12|seq13]
//                   \___(head_sequence_)   \               \___(highest_sequence_)
//                                           \___(no received, in nack list)
//      * seq1: The packet is done, we have already got and processed it.
//      * seq2,seq3,...,seq10,seq12,seq13: Theses packets are in queue and wait to be processed.
//      * seq11: This packet is lost or not received, we will put it in the nack list.
// We store the received packets in ring buffer.
class SrsRtpRingBuffer
{
private:
    // Capacity of the ring-buffer.
    uint16_t capacity_;
    // Ring bufer.
    SrsRtpPacket** queue_;
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
    SrsRtpRingBuffer(int capacity);
    virtual ~SrsRtpRingBuffer();
public:
    // Whether the ring buffer is empty.
    bool empty();
    // Get the count of elems in ring buffer.
    int size();
    // Move the low position of buffer to seq.
    void advance_to(uint16_t seq);
    // Free the packet at position.
    void set(uint16_t at, SrsRtpPacket* pkt);
    void remove(uint16_t at);
    // The highest sequence number, calculate the flip back base.
    uint32_t get_extended_highest_sequence();
    // Update the sequence, got the nack range by [first, last).
    // @return If false, the seq is too old.
    bool update(uint16_t seq, uint16_t& nack_first, uint16_t& nack_last);
    // Get the packet by seq.
    SrsRtpPacket* at(uint16_t seq);
public:
    // TODO: FIXME: Refine it?
    void notify_nack_list_full();
    void notify_drop_seq(uint16_t seq);
public:
    void clear_histroy(uint16_t seq);
    void clear_all_histroy();
};

struct SrsNackOption
{
    int max_count;
    srs_utime_t max_alive_time;
    srs_utime_t first_nack_interval;
    srs_utime_t nack_interval;

    srs_utime_t max_nack_interval;
    srs_utime_t min_nack_interval;
    srs_utime_t nack_check_interval;

    SrsNackOption();
};

struct SrsRtpNackInfo
{
    // Use to control the time of first nack req and the life of seq.
    srs_utime_t generate_time_;
    // Use to control nack interval.
    srs_utime_t pre_req_nack_time_;
    // Use to control nack times.
    int req_nack_count_;

    SrsRtpNackInfo();
};

class SrsRtpNackForReceiver
{
private:
    // Nack queue, seq order, oldest to newest.
    std::map<uint16_t, SrsRtpNackInfo, SrsSeqCompareLess> queue_;
    // Max nack count.
    size_t max_queue_size_;
    SrsRtpRingBuffer* rtp_;
    SrsNackOption opts_;
private:
    srs_utime_t pre_check_time_;
private:
    int rtt_;
public:
    SrsRtpNackForReceiver(SrsRtpRingBuffer* rtp, size_t queue_size);
    virtual ~SrsRtpNackForReceiver();
public:
    void insert(uint16_t first, uint16_t last);
    void remove(uint16_t seq);
    SrsRtpNackInfo* find(uint16_t seq);
    void check_queue_size();
public:
    void get_nack_seqs(SrsRtcpNack& seqs, uint32_t& timeout_nacks);
public:
    void update_rtt(int rtt);
};

#endif
