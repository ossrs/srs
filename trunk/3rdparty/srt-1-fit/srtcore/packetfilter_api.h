/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2019 Haivision Systems Inc.
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 * 
 */

#ifndef INC_SRT_PACKETFILTER_API_H
#define INC_SRT_PACKETFILTER_API_H

#include "platform_sys.h"

#include <cstring>
#include <string>
#include <map>
#include <vector>
#include <utility>

namespace srt {

class CPacket;

enum SrtPktHeaderFields
{
    SRT_PH_SEQNO = 0,     //< sequence number
    SRT_PH_MSGNO = 1,     //< message number
    SRT_PH_TIMESTAMP = 2, //< time stamp
    SRT_PH_ID = 3,        //< socket ID

    // Must be the last value - this is size of all, not a field id
    SRT_PH_E_SIZE
};


enum SRT_ARQLevel
{
    SRT_ARQ_NEVER,  //< Never send LOSSREPORT
    SRT_ARQ_ONREQ,  //< Only record the loss, but report only those that are returned in receive()
    SRT_ARQ_ALWAYS, //< always send LOSSREPORT immediately after detecting a loss
};

struct SrtConfig
{
    std::string type;
    typedef std::map<std::string, std::string> par_t;
    par_t parameters;
};

struct SrtFilterConfig: SrtConfig
{
    size_t extra_size; // needed for filter option check against payload size
};

struct SrtFilterInitializer
{
    SRTSOCKET socket_id;
    int32_t snd_isn;
    int32_t rcv_isn;
    size_t payload_size;
    size_t rcvbuf_size;
};

struct SrtPacket
{
    uint32_t hdr[SRT_PH_E_SIZE];
    char buffer[SRT_LIVE_MAX_PLSIZE];
    size_t length;

    SrtPacket(size_t size): length(size)
    {
        memset(hdr, 0, sizeof(hdr));
    }

    uint32_t header(SrtPktHeaderFields field) { return hdr[field]; }
    char* data() { return buffer; }
    const char* data() const { return buffer; }
    size_t size() const { return length; }
};


bool ParseFilterConfig(const std::string& s, SrtFilterConfig& w_config);


class SrtPacketFilterBase
{
    SrtFilterInitializer initParams;

protected:

    SRTSOCKET socketID() const { return initParams.socket_id; }
    int32_t sndISN() const { return initParams.snd_isn; }
    int32_t rcvISN() const { return initParams.rcv_isn; }
    size_t payloadSize() const { return initParams.payload_size; }
    size_t rcvBufferSize() const { return initParams.rcvbuf_size; }

    friend class PacketFilter;

    // Beside the size of the rows, special values:
    // 0: if you have 0 specified for rows, there are only columns
    // -1: Only during the handshake, use the value specified by peer.
    // -N: The N value still specifies the size, but in particular
    //     dimension there is no filter control packet formed nor expected.

public:

    typedef std::vector< std::pair<int32_t, int32_t> > loss_seqs_t;

protected:

    SrtPacketFilterBase(const SrtFilterInitializer& i): initParams(i)
    {
    }

    // Sender side

    /// This function creates and stores the filter control packet with
    /// a prediction to be immediately sent. This is called in the function
    /// that normally is prepared for extracting a data packet from the sender
    /// buffer and send it over the channel. The returned value informs the
    /// caller whether the control packet was available and therefore provided.
    /// @param [OUT] packet Target place where the packet should be stored
    /// @param [IN] seq Sequence number of the packet last requested for sending
    /// @return true if the control packet has been provided
    virtual bool packControlPacket(SrtPacket& packet, int32_t seq) = 0;

    /// This is called at the moment when the sender queue decided to pick up
    /// a new packet from the scheduled packets. This should be then used to
    /// continue filling the group, possibly followed by final calculating the
    /// control packet ready to send. The packet received by this function is
    /// potentially allowed to be modified.
    /// @param [INOUT] packet The packet about to send
    virtual void feedSource(CPacket& packet) = 0;


    // Receiver side

    // This function is called at the moment when a new data packet has
    // arrived (no matter if subsequent or recovered). The 'state' value
    // defines the configured level of loss state required to send the
    // loss report.
    virtual bool receive(const CPacket& pkt, loss_seqs_t& loss_seqs) = 0;

    // Backward configuration.
    // This should have some stable value after the configuration is parsed,
    // and it should be a stable value set ONCE, after the filter module is ready.
    virtual SRT_ARQLevel arqLevel() = 0;

    virtual ~SrtPacketFilterBase()
    {
    }
};

} // namespace srt

#endif
