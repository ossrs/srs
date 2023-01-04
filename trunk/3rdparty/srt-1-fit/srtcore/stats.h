/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2021 Haivision Systems Inc.
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 * 
 */

#ifndef INC_SRT_STATS_H
#define INC_SRT_STATS_H

#include "platform_sys.h"
#include "packet.h"

namespace srt
{
namespace stats
{

class Packets
{
public:
    Packets() : m_count(0) {}

    Packets(uint32_t num) : m_count(num) {}

    void reset()
    {
        m_count = 0;
    }

    Packets& operator+= (const Packets& other)
    {
        m_count   += other.m_count;
        return *this;
    }

    uint32_t count() const 
    {
        return m_count;
    }

private:
    uint32_t m_count;
};

class BytesPackets
{
public:
    BytesPackets()
        : m_bytes(0)
        , m_packets(0)
    {}

    BytesPackets(uint64_t bytes, uint32_t n = 1)
        : m_bytes(bytes)
        , m_packets(n)
    {}

    BytesPackets& operator+= (const BytesPackets& other)
    {
        m_bytes   += other.m_bytes;
        m_packets += other.m_packets;
        return *this;
    }

public:
    void reset()
    {
        m_packets = 0;
        m_bytes = 0;
    }

    void count(uint64_t bytes, size_t n = 1)
    {
        m_packets += (uint32_t) n;
        m_bytes += bytes;
    }

    uint64_t bytes() const 
    {
        return m_bytes;
    }

    uint32_t count() const 
    {
        return m_packets;
    }

    uint64_t bytesWithHdr() const
    {
        return m_bytes + m_packets * CPacket::SRT_DATA_HDR_SIZE;
    }

private:
    uint64_t m_bytes;
    uint32_t m_packets;
};

template <class METRIC_TYPE>
struct Metric
{
    METRIC_TYPE trace;
    METRIC_TYPE total;

    void count(METRIC_TYPE val)
    {
        trace += val;
        total += val;
    }

    void reset()
    {
        trace.reset();
        total.reset();
    }

    void resetTrace()
    {
        trace.reset();
    }
};

/// Sender-side statistics.
struct Sender
{
    Metric<BytesPackets> sent;
    Metric<BytesPackets> sentUnique;
    Metric<BytesPackets> sentRetrans; // The number of data packets retransmitted by the sender.
    Metric<Packets> lost; // The number of packets reported lost (including repeated reports) to the sender in NAKs.
    Metric<BytesPackets> dropped; // The number of data packets dropped by the sender.

    Metric<Packets> sentFilterExtra; // The number of packets generate by the packet filter and sent by the sender.
    
    Metric<Packets> recvdAck; // The number of ACK packets received by the sender.
    Metric<Packets> recvdNak; // The number of ACK packets received by the sender.

    void reset()
    {
        sent.reset();
        sentUnique.reset();
        sentRetrans.reset();
        lost.reset();
        dropped.reset();
        recvdAck.reset();
        recvdNak.reset();
        sentFilterExtra.reset();
    }

    void resetTrace()
    {
        sent.resetTrace();
        sentUnique.resetTrace();
        sentRetrans.resetTrace();
        lost.resetTrace();
        dropped.resetTrace();
        recvdAck.resetTrace();
        recvdNak.resetTrace();
        sentFilterExtra.resetTrace();
    }
};

/// Receiver-side statistics.
struct Receiver
{
    Metric<BytesPackets> recvd;
    Metric<BytesPackets> recvdUnique;
    Metric<BytesPackets> recvdRetrans; // The number of retransmitted data packets received by the receiver.
    Metric<BytesPackets> lost; // The number of packets detected by the receiver as lost.
    Metric<BytesPackets> dropped; // The number of packets dropped by the receiver (as too-late to be delivered).
    Metric<BytesPackets> recvdBelated; // The number of belated packets received (dropped as too late but eventually received).
    Metric<BytesPackets> undecrypted; // The number of packets received by the receiver that failed to be decrypted.

    Metric<Packets> recvdFilterExtra; // The number of filter packets (e.g. FEC) received by the receiver.
    Metric<Packets> suppliedByFilter; // The number of lost packets got from the packet filter at the receiver side (e.g. loss recovered by FEC).
    Metric<Packets> lossFilter; // The number of lost DATA packets not recovered by the packet filter at the receiver side.

    Metric<Packets> sentAck; // The number of ACK packets sent by the receiver.
    Metric<Packets> sentNak; // The number of NACK packets sent by the receiver.

    void reset()
    {
        recvd.reset();
        recvdUnique.reset();
        recvdRetrans.reset();
        lost.reset();
        dropped.reset();
        recvdBelated.reset();
        undecrypted.reset();
        recvdFilterExtra.reset();
        suppliedByFilter.reset();
        lossFilter.reset();
        sentAck.reset();
        sentNak.reset();
    }

    void resetTrace()
    {
        recvd.resetTrace();
        recvdUnique.resetTrace();
        recvdRetrans.resetTrace();
        lost.resetTrace();
        dropped.resetTrace();
        recvdBelated.resetTrace();
        undecrypted.resetTrace();
        recvdFilterExtra.resetTrace();
        suppliedByFilter.resetTrace();
        lossFilter.resetTrace();
        sentAck.resetTrace();
        sentNak.resetTrace();
    }
};

} // namespace stats
} // namespace srt

#endif // INC_SRT_STATS_H


