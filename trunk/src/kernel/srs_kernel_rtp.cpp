/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2020 Winlin
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

#include <srs_kernel_rtp.hpp>

#include <fcntl.h>
#include <sstream>
using namespace std;

#include <srs_kernel_log.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_buffer.hpp>
#include <srs_kernel_utility.hpp>

SrsRtpSharedPacket::SrsRtpSharedPacketPayload::SrsRtpSharedPacketPayload()
{
    payload = NULL;
    size = 0;
    shared_count = 0;
}

SrsRtpSharedPacket::SrsRtpSharedPacketPayload::~SrsRtpSharedPacketPayload()
{
    srs_freepa(payload);
}

SrsRtpSharedPacket::SrsRtpSharedPacket()
{
    payload_ptr = NULL;

    payload = NULL;
    size = 0;

    timestamp = -1;
    sequence = 0;
    ssrc = 0;
    payload_type = 0;
}

SrsRtpSharedPacket::~SrsRtpSharedPacket()
{
    if (payload_ptr) {
        if (payload_ptr->shared_count == 0) {
            srs_freep(payload_ptr);
        } else {
            --payload_ptr->shared_count;
        }
    }
}

srs_error_t SrsRtpSharedPacket::create(int64_t t, uint16_t seq, uint32_t sc, uint16_t pt, char* p, int s)
{
    srs_error_t err = srs_success;

    if (size < 0) {
        return srs_error_new(ERROR_RTP_PACKET_CREATE, "create packet size=%d", size);
    }   

    srs_assert(!payload_ptr);

    timestamp = t;
    sequence = seq;
    ssrc = sc;
    payload_type = pt;

    payload_ptr = new SrsRtpSharedPacketPayload();
    payload_ptr->payload = p;
    payload_ptr->size = s;

    payload = payload_ptr->payload;
    size = payload_ptr->size;

    return err;
}

SrsRtpSharedPacket* SrsRtpSharedPacket::copy()
{
    SrsRtpSharedPacket* copy = new SrsRtpSharedPacket();

    copy->payload_ptr = payload_ptr;
    payload_ptr->shared_count++;

    copy->payload = payload;
    copy->size = size;

    copy->timestamp = timestamp;
    copy->sequence = sequence;
    copy->ssrc = ssrc;
    copy->payload_type = payload_type;

    return copy;
}
