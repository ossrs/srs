//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_KERNEL_RTC_QUEUE_HPP
#define SRS_KERNEL_RTC_QUEUE_HPP

#include <srs_core.hpp>

#include <map>
#include <string>
#include <vector>

#include <srs_kernel_rtc_rtcp.hpp>
#include <srs_kernel_rtc_rtp.hpp>

class SrsRtpPacket;
class SrsRtpQueue;
class SrsRtpRingBuffer;

// Over UDP, RTP packets may be lost or come out of order.
class SrsRtpRingBuffer
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    uint16_t capacity_;
    SrsRtpPacket **queue_;
    uint64_t nn_seq_flip_backs_;
    // A flag, since a uint16 sequence has no free value such as -1.
    bool initialized_;

public:
    uint16_t begin_;
    uint16_t end_;

public:
    SrsRtpRingBuffer(int capacity);
    virtual ~SrsRtpRingBuffer();

public:
    bool empty();
    int size();
    void advance_to(uint16_t seq);
    void set(uint16_t at, SrsRtpPacket *pkt);
    void remove(uint16_t at);
    uint32_t get_extended_highest_sequence();
    // Outputs the lost range [nack_first, nack_last).
    bool update(uint16_t seq, uint16_t &nack_first, uint16_t &nack_last);
    SrsRtpPacket *at(uint16_t seq);

public:
    // TODO: FIXME: Refine it?
    void notify_nack_list_full();
    void notify_drop_seq(uint16_t seq);

public:
    void clear_histroy(uint16_t seq);
    void clear_all_histroy();
};

struct SrsNackOption {
    int max_count_;
    srs_utime_t max_alive_time_;
    srs_utime_t first_nack_interval_;
    srs_utime_t nack_interval_;

    srs_utime_t max_nack_interval_;
    srs_utime_t min_nack_interval_;
    srs_utime_t nack_check_interval_;

    SrsNackOption();
};

struct SrsRtpNackInfo {
    srs_utime_t generate_time_;
    srs_utime_t pre_req_nack_time_;
    int req_nack_count_;

    SrsRtpNackInfo();
};

class SrsRtpNackForReceiver
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    std::map<uint16_t, SrsRtpNackInfo, SrsSeqCompareLess>
        queue_;
    size_t max_queue_size_;
    SrsRtpRingBuffer *rtp_;
    SrsNackOption opts_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srs_utime_t pre_check_time_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    int rtt_;

public:
    SrsRtpNackForReceiver(SrsRtpRingBuffer *rtp, size_t queue_size);
    virtual ~SrsRtpNackForReceiver();

public:
    void insert(uint16_t first, uint16_t last);
    void remove(uint16_t seq);
    SrsRtpNackInfo *find(uint16_t seq);
    void check_queue_size();

public:
    void get_nack_seqs(SrsRtcpNack &seqs, uint32_t &timeout_nacks);

public:
    void update_rtt(int rtt);
};

#endif
