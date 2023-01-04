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
****************************************************************************/

/****************************************************************************
written by
   Yunhong Gu, last updated 01/27/2011
modified by
   Haivision Systems Inc.
*****************************************************************************/

#include "platform_sys.h"

#include <iostream>
#include <iomanip> // Logging
#include <srt_compat.h>
#include <csignal>

#include "channel.h"
#include "core.h" // srt_logging:kmlog
#include "packet.h"
#include "logging.h"
#include "netinet_any.h"
#include "utilities.h"

#ifdef _WIN32
typedef int socklen_t;
#endif

using namespace std;
using namespace srt_logging;

namespace srt
{

#ifdef _WIN32
// use INVALID_SOCKET, as provided
#else
static const int INVALID_SOCKET = -1;
#endif

#if ENABLE_SOCK_CLOEXEC
#ifndef _WIN32

#if defined(_AIX) || defined(__APPLE__) || defined(__DragonFly__) || defined(__FreeBSD__) ||                           \
    defined(__FreeBSD_kernel__) || defined(__linux__) || defined(__OpenBSD__) || defined(__NetBSD__)

// Set the CLOEXEC flag using ioctl() function
static int set_cloexec(int fd, int set)
{
    int r;

    do
        r = ioctl(fd, set ? FIOCLEX : FIONCLEX);
    while (r == -1 && errno == EINTR);

    if (r)
        return errno;

    return 0;
}
#else
// Set the CLOEXEC flag using fcntl() function
static int set_cloexec(int fd, int set)
{
    int flags;
    int r;

    do
        r = fcntl(fd, F_GETFD);
    while (r == -1 && errno == EINTR);

    if (r == -1)
        return errno;

    /* Bail out now if already set/clear. */
    if (!!(r & FD_CLOEXEC) == !!set)
        return 0;

    if (set)
        flags = r | FD_CLOEXEC;
    else
        flags = r & ~FD_CLOEXEC;

    do
        r = fcntl(fd, F_SETFD, flags);
    while (r == -1 && errno == EINTR);

    if (r)
        return errno;

    return 0;
}
#endif // if defined(_AIX) ...
#endif // ifndef _WIN32
#endif // if ENABLE_CLOEXEC
} // namespace srt

srt::CChannel::CChannel()
    : m_iSocket(INVALID_SOCKET)
{
}

srt::CChannel::~CChannel() {}

void srt::CChannel::createSocket(int family)
{
#if ENABLE_SOCK_CLOEXEC
    bool cloexec_flag = false;
    // construct an socket
#if defined(SOCK_CLOEXEC)
    m_iSocket = ::socket(family, SOCK_DGRAM | SOCK_CLOEXEC, IPPROTO_UDP);
    if (m_iSocket == INVALID_SOCKET)
    {
        m_iSocket    = ::socket(family, SOCK_DGRAM, IPPROTO_UDP);
        cloexec_flag = true;
    }
#else
    m_iSocket    = ::socket(family, SOCK_DGRAM, IPPROTO_UDP);
    cloexec_flag = true;
#endif
#else  // ENABLE_SOCK_CLOEXEC
    m_iSocket = ::socket(family, SOCK_DGRAM, IPPROTO_UDP);
#endif // ENABLE_SOCK_CLOEXEC

    if (m_iSocket == INVALID_SOCKET)
        throw CUDTException(MJ_SETUP, MN_NONE, NET_ERROR);

#if ENABLE_SOCK_CLOEXEC
#ifdef _WIN32
        // XXX ::SetHandleInformation(hInputWrite, HANDLE_FLAG_INHERIT, 0)
#else
    if (cloexec_flag)
    {
        if (0 != set_cloexec(m_iSocket, 1))
        {
            throw CUDTException(MJ_SETUP, MN_NONE, NET_ERROR);
        }
    }
#endif
#endif // ENABLE_SOCK_CLOEXEC

    if ((m_mcfg.iIpV6Only != -1) && (family == AF_INET6)) // (not an error if it fails)
    {
        const int res SRT_ATR_UNUSED =
            ::setsockopt(m_iSocket, IPPROTO_IPV6, IPV6_V6ONLY, (const char*)&m_mcfg.iIpV6Only, sizeof m_mcfg.iIpV6Only);
#if ENABLE_LOGGING
        if (res == -1)
        {
            int  err = errno;
            char msg[160];
            LOGC(kmlog.Error,
                 log << "::setsockopt: failed to set IPPROTO_IPV6/IPV6_V6ONLY = " << m_mcfg.iIpV6Only << ": "
                     << SysStrError(err, msg, 159));
        }
#endif // ENABLE_LOGGING
    }
}

void srt::CChannel::open(const sockaddr_any& addr)
{
    createSocket(addr.family());
    socklen_t namelen = addr.size();

    if (::bind(m_iSocket, &addr.sa, namelen) == -1)
        throw CUDTException(MJ_SETUP, MN_NORES, NET_ERROR);

    m_BindAddr = addr;
    LOGC(kmlog.Debug, log << "CHANNEL: Bound to local address: " << m_BindAddr.str());

    setUDPSockOpt();
}

void srt::CChannel::open(int family)
{
    createSocket(family);

    // sendto or WSASendTo will also automatically bind the socket
    addrinfo  hints;
    addrinfo* res;

    memset(&hints, 0, sizeof(struct addrinfo));

    hints.ai_flags    = AI_PASSIVE;
    hints.ai_family   = family;
    hints.ai_socktype = SOCK_DGRAM;

    const int eai = ::getaddrinfo(NULL, "0", &hints, &res);
    if (eai != 0)
    {
        // Controversial a little bit because this function occasionally
        // doesn't use errno (here: NET_ERROR for portability), instead
        // it returns 0 if succeeded or an error code. This error code
        // is passed here then. A controversy is around the fact that
        // the receiver of this error has completely no ability to know
        // what this error code's domain is, and it definitely isn't
        // the same as for errno.
        throw CUDTException(MJ_SETUP, MN_NORES, eai);
    }

    // On Windows ai_addrlen has type size_t (unsigned), while bind takes int.
    if (0 != ::bind(m_iSocket, res->ai_addr, (socklen_t)res->ai_addrlen))
    {
        ::freeaddrinfo(res);
        throw CUDTException(MJ_SETUP, MN_NORES, NET_ERROR);
    }
    m_BindAddr = sockaddr_any(res->ai_addr, (sockaddr_any::len_t)res->ai_addrlen);

    ::freeaddrinfo(res);

    HLOGC(kmlog.Debug, log << "CHANNEL: Bound to local address: " << m_BindAddr.str());

    setUDPSockOpt();
}

void srt::CChannel::attach(UDPSOCKET udpsock, const sockaddr_any& udpsocks_addr)
{
    // The getsockname() call is done before calling it and the
    // result is placed into udpsocks_addr.
    m_iSocket  = udpsock;
    m_BindAddr = udpsocks_addr;
    setUDPSockOpt();
}

void srt::CChannel::setUDPSockOpt()
{
#if defined(SUNOS)
    {
        socklen_t optSize;
        // Retrieve starting SND/RCV Buffer sizes.
        int startRCVBUF = 0;
        optSize         = sizeof(startRCVBUF);
        if (0 != ::getsockopt(m_iSocket, SOL_SOCKET, SO_RCVBUF, (void*)&startRCVBUF, &optSize))
        {
            startRCVBUF = -1;
        }
        int startSNDBUF = 0;
        optSize         = sizeof(startSNDBUF);
        if (0 != ::getsockopt(m_iSocket, SOL_SOCKET, SO_SNDBUF, (void*)&startSNDBUF, &optSize))
        {
            startSNDBUF = -1;
        }

        // SunOS will fail setsockopt() if the requested buffer size exceeds system
        //   maximum value.
        // However, do not reduce the buffer size.
        const int maxsize = 64000;
        if (0 !=
            ::setsockopt(
                m_iSocket, SOL_SOCKET, SO_RCVBUF, (const char*)&m_mcfg.iUDPRcvBufSize, sizeof m_mcfg.iUDPRcvBufSize))
        {
            int currentRCVBUF = 0;
            optSize           = sizeof(currentRCVBUF);
            if (0 != ::getsockopt(m_iSocket, SOL_SOCKET, SO_RCVBUF, (void*)&currentRCVBUF, &optSize))
            {
                currentRCVBUF = -1;
            }
            if (maxsize > currentRCVBUF)
            {
                ::setsockopt(m_iSocket, SOL_SOCKET, SO_RCVBUF, (const char*)&maxsize, sizeof maxsize);
            }
        }
        if (0 !=
            ::setsockopt(
                m_iSocket, SOL_SOCKET, SO_SNDBUF, (const char*)&m_mcfg.iUDPSndBufSize, sizeof m_mcfg.iUDPSndBufSize))
        {
            int currentSNDBUF = 0;
            optSize           = sizeof(currentSNDBUF);
            if (0 != ::getsockopt(m_iSocket, SOL_SOCKET, SO_RCVBUF, (void*)&currentSNDBUF, &optSize))
            {
                currentSNDBUF = -1;
            }
            if (maxsize > currentSNDBUF)
            {
                ::setsockopt(m_iSocket, SOL_SOCKET, SO_SNDBUF, (const char*)&maxsize, sizeof maxsize);
            }
        }

        // Retrieve ending SND/RCV Buffer sizes.
        int endRCVBUF = 0;
        optSize       = sizeof(endRCVBUF);
        if (0 != ::getsockopt(m_iSocket, SOL_SOCKET, SO_RCVBUF, (void*)&endRCVBUF, &optSize))
        {
            endRCVBUF = -1;
        }
        int endSNDBUF = 0;
        optSize       = sizeof(endSNDBUF);
        if (0 != ::getsockopt(m_iSocket, SOL_SOCKET, SO_SNDBUF, (void*)&endSNDBUF, &optSize))
        {
            endSNDBUF = -1;
        }
        LOGC(kmlog.Debug,
             log << "SO_RCVBUF:"
                 << " startRCVBUF=" << startRCVBUF << " m_mcfg.iUDPRcvBufSize=" << m_mcfg.iUDPRcvBufSize
                 << " endRCVBUF=" << endRCVBUF);
        LOGC(kmlog.Debug,
             log << "SO_SNDBUF:"
                 << " startSNDBUF=" << startSNDBUF << " m_mcfg.iUDPSndBufSize=" << m_mcfg.iUDPSndBufSize
                 << " endSNDBUF=" << endSNDBUF);
    }
#elif defined(BSD) || TARGET_OS_MAC
    // BSD system will fail setsockopt if the requested buffer size exceeds system maximum value
    int maxsize = 64000;
    if (0 != ::setsockopt(
                 m_iSocket, SOL_SOCKET, SO_RCVBUF, (const char*)&m_mcfg.iUDPRcvBufSize, sizeof m_mcfg.iUDPRcvBufSize))
        ::setsockopt(m_iSocket, SOL_SOCKET, SO_RCVBUF, (const char*)&maxsize, sizeof maxsize);
    if (0 != ::setsockopt(
                 m_iSocket, SOL_SOCKET, SO_SNDBUF, (const char*)&m_mcfg.iUDPSndBufSize, sizeof m_mcfg.iUDPSndBufSize))
        ::setsockopt(m_iSocket, SOL_SOCKET, SO_SNDBUF, (const char*)&maxsize, sizeof maxsize);
#else
    // for other systems, if requested is greated than maximum, the maximum value will be automactally used
    if ((0 !=
         ::setsockopt(
             m_iSocket, SOL_SOCKET, SO_RCVBUF, (const char*)&m_mcfg.iUDPRcvBufSize, sizeof m_mcfg.iUDPRcvBufSize)) ||
        (0 != ::setsockopt(
                  m_iSocket, SOL_SOCKET, SO_SNDBUF, (const char*)&m_mcfg.iUDPSndBufSize, sizeof m_mcfg.iUDPSndBufSize)))
        throw CUDTException(MJ_SETUP, MN_NORES, NET_ERROR);
#endif

    if (m_mcfg.iIpTTL != -1)
    {
        if (m_BindAddr.family() == AF_INET)
        {
            if (0 != ::setsockopt(m_iSocket, IPPROTO_IP, IP_TTL, (const char*)&m_mcfg.iIpTTL, sizeof m_mcfg.iIpTTL))
                throw CUDTException(MJ_SETUP, MN_NORES, NET_ERROR);
        }
        else
        {
            // If IPv6 address is unspecified, set BOTH IP_TTL and IPV6_UNICAST_HOPS.

            // For specified IPv6 address, set IPV6_UNICAST_HOPS ONLY UNLESS it's an IPv4-mapped-IPv6
            if (IN6_IS_ADDR_UNSPECIFIED(&m_BindAddr.sin6.sin6_addr) ||
                !IN6_IS_ADDR_V4MAPPED(&m_BindAddr.sin6.sin6_addr))
            {
                if (0 !=
                    ::setsockopt(
                        m_iSocket, IPPROTO_IPV6, IPV6_UNICAST_HOPS, (const char*)&m_mcfg.iIpTTL, sizeof m_mcfg.iIpTTL))
                {
                    throw CUDTException(MJ_SETUP, MN_NORES, NET_ERROR);
                }
            }
            // For specified IPv6 address, set IP_TTL ONLY WHEN it's an IPv4-mapped-IPv6
            if (IN6_IS_ADDR_UNSPECIFIED(&m_BindAddr.sin6.sin6_addr) || IN6_IS_ADDR_V4MAPPED(&m_BindAddr.sin6.sin6_addr))
            {
                if (0 != ::setsockopt(m_iSocket, IPPROTO_IP, IP_TTL, (const char*)&m_mcfg.iIpTTL, sizeof m_mcfg.iIpTTL))
                {
                    throw CUDTException(MJ_SETUP, MN_NORES, NET_ERROR);
                }
            }
        }
    }

    if (m_mcfg.iIpToS != -1)
    {
        if (m_BindAddr.family() == AF_INET)
        {
            if (0 != ::setsockopt(m_iSocket, IPPROTO_IP, IP_TOS, (const char*)&m_mcfg.iIpToS, sizeof m_mcfg.iIpToS))
                throw CUDTException(MJ_SETUP, MN_NORES, NET_ERROR);
        }
        else
        {
            // If IPv6 address is unspecified, set BOTH IP_TOS and IPV6_TCLASS.

#ifdef IPV6_TCLASS
            // For specified IPv6 address, set IPV6_TCLASS ONLY UNLESS it's an IPv4-mapped-IPv6
            if (IN6_IS_ADDR_UNSPECIFIED(&m_BindAddr.sin6.sin6_addr) ||
                !IN6_IS_ADDR_V4MAPPED(&m_BindAddr.sin6.sin6_addr))
            {
                if (0 != ::setsockopt(
                             m_iSocket, IPPROTO_IPV6, IPV6_TCLASS, (const char*)&m_mcfg.iIpToS, sizeof m_mcfg.iIpToS))
                {
                    throw CUDTException(MJ_SETUP, MN_NORES, NET_ERROR);
                }
            }
#endif

            // For specified IPv6 address, set IP_TOS ONLY WHEN it's an IPv4-mapped-IPv6
            if (IN6_IS_ADDR_UNSPECIFIED(&m_BindAddr.sin6.sin6_addr) || IN6_IS_ADDR_V4MAPPED(&m_BindAddr.sin6.sin6_addr))
            {
                if (0 != ::setsockopt(m_iSocket, IPPROTO_IP, IP_TOS, (const char*)&m_mcfg.iIpToS, sizeof m_mcfg.iIpToS))
                {
                    throw CUDTException(MJ_SETUP, MN_NORES, NET_ERROR);
                }
            }
        }
    }

#ifdef SRT_ENABLE_BINDTODEVICE
    if (!m_mcfg.sBindToDevice.empty())
    {
        if (m_BindAddr.family() != AF_INET)
        {
            LOGC(kmlog.Error, log << "SRTO_BINDTODEVICE can only be set with AF_INET connections");
            throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);
        }

        if (0 != ::setsockopt(
                     m_iSocket, SOL_SOCKET, SO_BINDTODEVICE, m_mcfg.sBindToDevice.c_str(), m_mcfg.sBindToDevice.size()))
        {
#if ENABLE_LOGGING
            char        buf[255];
            const char* err = SysStrError(NET_ERROR, buf, 255);
            LOGC(kmlog.Error, log << "setsockopt(SRTO_BINDTODEVICE): " << err);
#endif // ENABLE_LOGGING
            throw CUDTException(MJ_SETUP, MN_NORES, NET_ERROR);
        }
    }
#endif

#ifdef UNIX
    // Set non-blocking I/O
    // UNIX does not support SO_RCVTIMEO
    int opts = ::fcntl(m_iSocket, F_GETFL);
    if (-1 == ::fcntl(m_iSocket, F_SETFL, opts | O_NONBLOCK))
        throw CUDTException(MJ_SETUP, MN_NORES, NET_ERROR);
#elif defined(_WIN32)
    u_long nonBlocking = 1;
    if (0 != ioctlsocket(m_iSocket, FIONBIO, &nonBlocking))
        throw CUDTException(MJ_SETUP, MN_NORES, NET_ERROR);
#else
    timeval tv;
    tv.tv_sec = 0;
#if defined(BSD) || TARGET_OS_MAC
    // Known BSD bug as the day I wrote this code.
    // A small time out value will cause the socket to block forever.
    tv.tv_usec = 10000;
#else
    tv.tv_usec = 100;
#endif
    // Set receiving time-out value
    if (0 != ::setsockopt(m_iSocket, SOL_SOCKET, SO_RCVTIMEO, (char*)&tv, sizeof(timeval)))
        throw CUDTException(MJ_SETUP, MN_NORES, NET_ERROR);
#endif
}

void srt::CChannel::close() const
{
#ifndef _WIN32
    ::close(m_iSocket);
#else
    ::closesocket(m_iSocket);
#endif
}

int srt::CChannel::getSndBufSize()
{
    socklen_t size = (socklen_t)sizeof m_mcfg.iUDPSndBufSize;
    ::getsockopt(m_iSocket, SOL_SOCKET, SO_SNDBUF, (char*)&m_mcfg.iUDPSndBufSize, &size);
    return m_mcfg.iUDPSndBufSize;
}

int srt::CChannel::getRcvBufSize()
{
    socklen_t size = (socklen_t)sizeof m_mcfg.iUDPRcvBufSize;
    ::getsockopt(m_iSocket, SOL_SOCKET, SO_RCVBUF, (char*)&m_mcfg.iUDPRcvBufSize, &size);
    return m_mcfg.iUDPRcvBufSize;
}

void srt::CChannel::setConfig(const CSrtMuxerConfig& config)
{
    m_mcfg = config;
}

int srt::CChannel::getIpTTL() const
{
    if (m_iSocket == INVALID_SOCKET)
        throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);

    socklen_t size = (socklen_t)sizeof m_mcfg.iIpTTL;
    if (m_BindAddr.family() == AF_INET)
    {
        ::getsockopt(m_iSocket, IPPROTO_IP, IP_TTL, (char*)&m_mcfg.iIpTTL, &size);
    }
    else if (m_BindAddr.family() == AF_INET6)
    {
        ::getsockopt(m_iSocket, IPPROTO_IPV6, IPV6_UNICAST_HOPS, (char*)&m_mcfg.iIpTTL, &size);
    }
    else
    {
        // If family is unspecified, the socket probably doesn't exist.
        LOGC(kmlog.Error, log << "IPE: CChannel::getIpTTL called with unset family");
        throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);
    }
    return m_mcfg.iIpTTL;
}

int srt::CChannel::getIpToS() const
{
    if (m_iSocket == INVALID_SOCKET)
        throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);

    socklen_t size = (socklen_t)sizeof m_mcfg.iIpToS;
    if (m_BindAddr.family() == AF_INET)
    {
        ::getsockopt(m_iSocket, IPPROTO_IP, IP_TOS, (char*)&m_mcfg.iIpToS, &size);
    }
    else if (m_BindAddr.family() == AF_INET6)
    {
#ifdef IPV6_TCLASS
        ::getsockopt(m_iSocket, IPPROTO_IPV6, IPV6_TCLASS, (char*)&m_mcfg.iIpToS, &size);
#endif
    }
    else
    {
        // If family is unspecified, the socket probably doesn't exist.
        LOGC(kmlog.Error, log << "IPE: CChannel::getIpToS called with unset family");
        throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);
    }
    return m_mcfg.iIpToS;
}

#ifdef SRT_ENABLE_BINDTODEVICE
bool srt::CChannel::getBind(char* dst, size_t len)
{
    if (m_iSocket == INVALID_SOCKET)
        return false; // No socket to get data from

    // Try to obtain it directly from the function. If not possible,
    // then return from internal data.
    socklen_t length = len;
    int       res    = ::getsockopt(m_iSocket, SOL_SOCKET, SO_BINDTODEVICE, dst, &length);
    if (res == -1)
        return false; // Happens on Linux v < 3.8

    // For any case
    dst[length] = 0;
    return true;
}
#endif

int srt::CChannel::ioctlQuery(int type SRT_ATR_UNUSED) const
{
#if defined(unix) || defined(__APPLE__)
    int value = 0;
    int res   = ::ioctl(m_iSocket, type, &value);
    if (res != -1)
        return value;
#endif
    return -1;
}

int srt::CChannel::sockoptQuery(int level SRT_ATR_UNUSED, int option SRT_ATR_UNUSED) const
{
#if defined(unix) || defined(__APPLE__)
    int       value = 0;
    socklen_t len   = sizeof(int);
    int       res   = ::getsockopt(m_iSocket, level, option, &value, &len);
    if (res != -1)
        return value;
#endif
    return -1;
}

void srt::CChannel::getSockAddr(sockaddr_any& w_addr) const
{
    // The getsockname function requires only to have enough target
    // space to copy the socket name, it doesn't have to be correlated
    // with the address family. So the maximum space for any name,
    // regardless of the family, does the job.
    socklen_t namelen = (socklen_t)w_addr.storage_size();
    ::getsockname(m_iSocket, (w_addr.get()), (&namelen));
    w_addr.len = namelen;
}

void srt::CChannel::getPeerAddr(sockaddr_any& w_addr) const
{
    socklen_t namelen = (socklen_t)w_addr.storage_size();
    ::getpeername(m_iSocket, (w_addr.get()), (&namelen));
    w_addr.len = namelen;
}

int srt::CChannel::sendto(const sockaddr_any& addr, CPacket& packet) const
{
    HLOGC(kslog.Debug,
          log << "CChannel::sendto: SENDING NOW DST=" << addr.str() << " target=@" << packet.m_iID
              << " size=" << packet.getLength() << " pkt.ts=" << packet.m_iTimeStamp << " " << packet.Info());

#ifdef SRT_TEST_FAKE_LOSS

#define FAKELOSS_STRING_0(x) #x
#define FAKELOSS_STRING(x) FAKELOSS_STRING_0(x)
    const char* fakeloss_text = FAKELOSS_STRING(SRT_TEST_FAKE_LOSS);
#undef FAKELOSS_STRING
#undef FAKELOSS_WRAP

    static int dcounter   = 0;
    static int flwcounter = 0;

    struct FakelossConfig
    {
        pair<int, int> config;
        FakelossConfig(const char* f)
        {
            vector<string> out;
            Split(f, '+', back_inserter(out));

            config.first  = atoi(out[0].c_str());
            config.second = out.size() > 1 ? atoi(out[1].c_str()) : 8;
        }
    };
    static FakelossConfig fakeloss = fakeloss_text;

    if (!packet.isControl())
    {
        ++dcounter;

        if (flwcounter)
        {
            // This is a counter of how many packets in a row shall be lost
            --flwcounter;
            HLOGC(kslog.Debug,
                  log << "CChannel: TEST: FAKE LOSS OF %" << packet.getSeqNo() << " (" << flwcounter
                      << " more to drop)");
            return packet.getLength(); // fake successful sendinf
        }

        if (dcounter > 8)
        {
            // Make a random number in the range between 8 and 24
            const int rnd = srt::sync::genRandomInt(8, 24);

            if (dcounter > rnd)
            {
                dcounter = 1;
                HLOGC(kslog.Debug,
                      log << "CChannel: TEST: FAKE LOSS OF %" << packet.getSeqNo() << " (will drop "
                          << fakeloss.config.first << " more)");
                flwcounter = fakeloss.config.first;
                return packet.getLength(); // fake successful sendinf
            }
        }
    }

#endif

    // convert control information into network order
    packet.toNL();

#ifndef _WIN32
    msghdr mh;
    mh.msg_name       = (sockaddr*)&addr;
    mh.msg_namelen    = addr.size();
    mh.msg_iov        = (iovec*)packet.m_PacketVector;
    mh.msg_iovlen     = 2;
    mh.msg_control    = NULL;
    mh.msg_controllen = 0;
    mh.msg_flags      = 0;

    const int res = ::sendmsg(m_iSocket, &mh, 0);
#else
    DWORD size     = (DWORD)(CPacket::HDR_SIZE + packet.getLength());
    int   addrsize = addr.size();
    int   res = ::WSASendTo(m_iSocket, (LPWSABUF)packet.m_PacketVector, 2, &size, 0, addr.get(), addrsize, NULL, NULL);
    res       = (0 == res) ? size : -1;
#endif

    packet.toHL();

    return res;
}

srt::EReadStatus srt::CChannel::recvfrom(sockaddr_any& w_addr, CPacket& w_packet) const
{
    EReadStatus status    = RST_OK;
    int         msg_flags = 0;
    int         recv_size = -1;

#if defined(UNIX) || defined(_WIN32)
    fd_set  set;
    timeval tv;
    FD_ZERO(&set);
    FD_SET(m_iSocket, &set);
    tv.tv_sec            = 0;
    tv.tv_usec           = 10000;
    const int select_ret = ::select((int)m_iSocket + 1, &set, NULL, &set, &tv);
#else
    const int select_ret = 1; // the socket is expected to be in the blocking mode itself
#endif

    if (select_ret == 0) // timeout
    {
        w_packet.setLength(-1);
        return RST_AGAIN;
    }

#ifndef _WIN32
    if (select_ret > 0)
    {
        msghdr mh;
        mh.msg_name       = (w_addr.get());
        mh.msg_namelen    = w_addr.size();
        mh.msg_iov        = (w_packet.m_PacketVector);
        mh.msg_iovlen     = 2;
        mh.msg_control    = NULL;
        mh.msg_controllen = 0;
        mh.msg_flags      = 0;

        recv_size = ::recvmsg(m_iSocket, (&mh), 0);
        msg_flags = mh.msg_flags;
    }

    // Note that there are exactly four groups of possible errors
    // reported by recvmsg():

    // 1. Temporary error, can't get the data, but you can try again.
    // Codes: EAGAIN/EWOULDBLOCK, EINTR, ECONNREFUSED
    // Return: RST_AGAIN.
    //
    // 2. Problems that should never happen due to unused configurations.
    // Codes: ECONNREFUSED, ENOTCONN
    // Return: RST_ERROR, just formally treat this as IPE.
    //
    // 3. Unexpected runtime errors:
    // Codes: EINVAL, EFAULT, ENOMEM, ENOTSOCK
    // Return: RST_ERROR. Except ENOMEM, this can only be an IPE. ENOMEM
    // should make the program stop as lacking memory will kill the program anyway soon.
    //
    // 4. Expected socket closed in the meantime by another thread.
    // Codes: EBADF
    // Return: RST_ERROR. This will simply make the worker thread exit, which is
    // expected to happen after CChannel::close() is called by another thread.

    // We do not handle <= SOCKET_ERROR as they are handled further by checking the recv_size
    if (select_ret == -1 || recv_size == -1)
    {
        const int err = NET_ERROR;
        if (err == EAGAIN || err == EINTR ||
            err == ECONNREFUSED) // For EAGAIN, this isn't an error, just a useless call.
        {
            status = RST_AGAIN;
        }
        else
        {
            HLOGC(krlog.Debug, log << CONID() << "(sys)recvmsg: " << SysStrError(err) << " [" << err << "]");
            status = RST_ERROR;
        }

        goto Return_error;
    }

#else
    // XXX REFACTORING NEEDED!
    // This procedure uses the WSARecvFrom function that just reads
    // into one buffer. On Windows, the equivalent for recvmsg, WSARecvMsg
    // uses the equivalent of msghdr - WSAMSG, which has different field
    // names and also uses the equivalet of iovec - WSABUF, which has different
    // field names and layout. It is important that this code be translated
    // to the "proper" solution, however this requires that CPacket::m_PacketVector
    // also uses the "platform independent" (or, better, platform-suitable) type
    // which can be appropriate for the appropriate system function, not just iovec
    // (see a specifically provided definition for iovec for windows in packet.h).
    //
    // For the time being, the msg_flags variable is defined in both cases
    // so that it can be checked independently, however it won't have any other
    // value one Windows than 0, unless this procedure below is rewritten
    // to use WSARecvMsg().

    int   recv_ret = SOCKET_ERROR;
    DWORD flag     = 0;

    if (select_ret > 0) // the total number of socket handles that are ready
    {
        DWORD size     = (DWORD)(CPacket::HDR_SIZE + w_packet.getLength());
        int   addrsize = w_addr.size();

        recv_ret = ::WSARecvFrom(m_iSocket,
                                 ((LPWSABUF)w_packet.m_PacketVector),
                                 2,
                                 (&size),
                                 (&flag),
                                 (w_addr.get()),
                                 (&addrsize),
                                 NULL,
                                 NULL);
        if (recv_ret == 0)
            recv_size = size;
    }

    // We do not handle <= SOCKET_ERROR as they are handled further by checking the recv_size
    if (select_ret == SOCKET_ERROR || recv_ret == SOCKET_ERROR) // == SOCKET_ERROR
    {
        recv_size = -1;
        // On Windows this is a little bit more complicated, so simply treat every error
        // as an "again" situation. This should still be probably fixed, but it needs more
        // thorough research. For example, the problem usually reported from here is
        // WSAETIMEDOUT, which isn't mentioned in the documentation of WSARecvFrom at all.
        //
        // These below errors are treated as "fatal", all others are treated as "again".
        static const int  fatals[]   = {WSAEFAULT, WSAEINVAL, WSAENETDOWN, WSANOTINITIALISED, WSA_OPERATION_ABORTED};
        static const int* fatals_end = fatals + Size(fatals);
        const int         err        = NET_ERROR;
        if (std::find(fatals, fatals_end, err) != fatals_end)
        {
            HLOGC(krlog.Debug, log << CONID() << "(sys)WSARecvFrom: " << SysStrError(err) << " [" << err << "]");
            status = RST_ERROR;
        }
        else
        {
            status = RST_AGAIN;
        }

        goto Return_error;
    }

    // Not sure if this problem has ever occurred on Windows, just a sanity check.
    if (flag & MSG_PARTIAL)
        msg_flags = 1;
#endif

    // Sanity check for a case when it didn't fill in even the header
    if (size_t(recv_size) < CPacket::HDR_SIZE)
    {
        status = RST_AGAIN;
        HLOGC(krlog.Debug,
              log << CONID() << "POSSIBLE ATTACK: received too short packet with " << recv_size << " bytes");
        goto Return_error;
    }

    // Fix for an issue with Linux Kernel found during tests at Tencent.
    //
    // There was a bug in older Linux Kernel which caused that when the internal
    // buffer was depleted during reading from the network, not the whole buffer
    // was copied from the packet, EVEN THOUGH THE GIVEN BUFFER WAS OF ENOUGH SIZE.
    // It was still very kind of the buggy procedure, though, that at least
    // they inform the caller about that this has happened by setting MSG_TRUNC
    // flag.
    //
    // Normally this flag should be set only if there was too small buffer given
    // by the caller, so as this code knows that the size is enough, it never
    // predicted this to happen. Just for a case then when you run this on a buggy
    // system that suffers of this problem, the fix for this case is left here.
    //
    // When this happens, then you have at best a fragment of the buffer and it's
    // useless anyway. This is solved by dropping the packet and fake that no
    // packet was received, so the packet will be then retransmitted.
    if (msg_flags != 0)
    {
        HLOGC(krlog.Debug,
              log << CONID() << "NET ERROR: packet size=" << recv_size << " msg_flags=0x" << hex << msg_flags
                  << ", possibly MSG_TRUNC)");
        status = RST_AGAIN;
        goto Return_error;
    }

    w_packet.setLength(recv_size - CPacket::HDR_SIZE);

    // convert back into local host order
    // XXX use NtoHLA().
    // for (int i = 0; i < 4; ++ i)
    //   w_packet.m_nHeader[i] = ntohl(w_packet.m_nHeader[i]);
    {
        uint32_t* p = w_packet.m_nHeader;
        for (size_t i = 0; i < SRT_PH_E_SIZE; ++i)
        {
            *p = ntohl(*p);
            ++p;
        }
    }

    if (w_packet.isControl())
    {
        for (size_t j = 0, n = w_packet.getLength() / sizeof(uint32_t); j < n; ++j)
            *((uint32_t*)w_packet.m_pcData + j) = ntohl(*((uint32_t*)w_packet.m_pcData + j));
    }

    return RST_OK;

Return_error:
    w_packet.setLength(-1);
    return status;
}
