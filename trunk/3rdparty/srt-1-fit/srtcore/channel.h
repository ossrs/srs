/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2018 Haivision Systems Inc.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 */

/*****************************************************************************
Copyright (c) 2001 - 2011, The Board of Trustees of the University of Illinois.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

* Redistributions of source code must retain the above
  copyright notice, this list of conditions and the
  following disclaimer.

* Redistributions in binary form must reproduce the
  above copyright notice, this list of conditions
  and the following disclaimer in the documentation
  and/or other materials provided with the distribution.

* Neither the name of the University of Illinois
  nor the names of its contributors may be used to
  endorse or promote products derived from this
  software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*****************************************************************************/

/*****************************************************************************
written by
   Yunhong Gu, last updated 01/27/2011
modified by
   Haivision Systems Inc.
*****************************************************************************/

#ifndef INC_SRT_CHANNEL_H
#define INC_SRT_CHANNEL_H

#include "platform_sys.h"
#include "udt.h"
#include "packet.h"
#include "socketconfig.h"
#include "netinet_any.h"

namespace srt
{

class CChannel
{
    void createSocket(int family);

public:
    // XXX There's currently no way to access the socket ID set for
    // whatever the channel is currently working for. Required to find
    // some way to do this, possibly by having a "reverse pointer".
    // Currently just "unimplemented".
    std::string CONID() const { return ""; }

    CChannel();
    ~CChannel();

    /// Open a UDP channel.
    /// @param [in] addr The local address that UDP will use.

    void open(const sockaddr_any& addr);

    void open(int family);

    /// Open a UDP channel based on an existing UDP socket.
    /// @param [in] udpsock UDP socket descriptor.

    void attach(UDPSOCKET udpsock, const sockaddr_any& adr);

    /// Disconnect and close the UDP entity.

    void close() const;

    /// Get the UDP sending buffer size.
    /// @return Current UDP sending buffer size.

    int getSndBufSize();

    /// Get the UDP receiving buffer size.
    /// @return Current UDP receiving buffer size.

    int getRcvBufSize();

    /// Query the socket address that the channel is using.
    /// @param [out] addr pointer to store the returned socket address.

    void getSockAddr(sockaddr_any& addr) const;

    /// Query the peer side socket address that the channel is connect to.
    /// @param [out] addr pointer to store the returned socket address.

    void getPeerAddr(sockaddr_any& addr) const;

    /// Send a packet to the given address.
    /// @param [in] addr pointer to the destination address.
    /// @param [in] packet reference to a CPacket entity.
    /// @return Actual size of data sent.

    int sendto(const sockaddr_any& addr, srt::CPacket& packet) const;

    /// Receive a packet from the channel and record the source address.
    /// @param [in] addr pointer to the source address.
    /// @param [in] packet reference to a CPacket entity.
    /// @return Actual size of data received.

    EReadStatus recvfrom(sockaddr_any& addr, srt::CPacket& packet) const;

    void setConfig(const CSrtMuxerConfig& config);

    /// Get the IP TTL.
    /// @param [in] ttl IP Time To Live.
    /// @return TTL.

    int getIpTTL() const;

    /// Get the IP Type of Service.
    /// @return ToS.

    int getIpToS() const;

#ifdef SRT_ENABLE_BINDTODEVICE
    bool getBind(char* dst, size_t len);
#endif

    int ioctlQuery(int type) const;
    int sockoptQuery(int level, int option) const;

    const sockaddr*     bindAddress() { return m_BindAddr.get(); }
    const sockaddr_any& bindAddressAny() { return m_BindAddr; }

private:
    void setUDPSockOpt();

private:
    UDPSOCKET m_iSocket; // socket descriptor

    // Mutable because when querying original settings
    // this comprises the cache for extracted values,
    // although the object itself isn't considered modified.
    mutable CSrtMuxerConfig m_mcfg; // Note: ReuseAddr is unused and ineffective.
    sockaddr_any            m_BindAddr;
};

} // namespace srt

#endif
