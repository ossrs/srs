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
Copyright (c) 2001 - 2016, The Board of Trustees of the University of Illinois.
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
   Yunhong Gu, last updated 07/25/2010
modified by
   Haivision Systems Inc.
*****************************************************************************/

#define SRT_IMPORT_TIME 1
#include "platform_sys.h"

#include <string>
#include <sstream>
#include <cmath>
#include <iostream>
#include <iomanip>
#include <iterator>
#include <vector>
#include "udt.h"
#include "md5.h"
#include "common.h"
#include "netinet_any.h"
#include "logging.h"
#include "packet.h"
#include "threadname.h"

#include <srt_compat.h> // SysStrError

using namespace std;
using namespace srt::sync;
using namespace srt_logging;

namespace srt_logging
{
extern Logger inlog;
}

namespace srt
{

const char* strerror_get_message(size_t major, size_t minor);
} // namespace srt


srt::CUDTException::CUDTException(CodeMajor major, CodeMinor minor, int err):
m_iMajor(major),
m_iMinor(minor)
{
   if (err == -1)
       m_iErrno = NET_ERROR;
   else
      m_iErrno = err;
}

const char* srt::CUDTException::getErrorMessage() const ATR_NOTHROW
{
    return strerror_get_message(m_iMajor, m_iMinor);
}

std::string srt::CUDTException::getErrorString() const
{
    return getErrorMessage();
}

#define UDT_XCODE(mj, mn) (int(mj)*1000)+int(mn)

int srt::CUDTException::getErrorCode() const
{
    return UDT_XCODE(m_iMajor, m_iMinor);
}

int srt::CUDTException::getErrno() const
{
   return m_iErrno;
}


void srt::CUDTException::clear()
{
   m_iMajor = MJ_SUCCESS;
   m_iMinor = MN_NONE;
   m_iErrno = 0;
}

#undef UDT_XCODE

//
bool srt::CIPAddress::ipcmp(const sockaddr* addr1, const sockaddr* addr2, int ver)
{
   if (AF_INET == ver)
   {
      sockaddr_in* a1 = (sockaddr_in*)addr1;
      sockaddr_in* a2 = (sockaddr_in*)addr2;

      if ((a1->sin_port == a2->sin_port) && (a1->sin_addr.s_addr == a2->sin_addr.s_addr))
         return true;
   }
   else
   {
      sockaddr_in6* a1 = (sockaddr_in6*)addr1;
      sockaddr_in6* a2 = (sockaddr_in6*)addr2;

      if (a1->sin6_port == a2->sin6_port)
      {
         for (int i = 0; i < 16; ++ i)
            if (*((char*)&(a1->sin6_addr) + i) != *((char*)&(a2->sin6_addr) + i))
               return false;

         return true;
      }
   }

   return false;
}

void srt::CIPAddress::ntop(const sockaddr_any& addr, uint32_t ip[4])
{
    if (addr.family() == AF_INET)
    {
        // SRT internal format of IPv4 address.
        // The IPv4 address is in the first field. The rest is 0.
        ip[0] = addr.sin.sin_addr.s_addr;
        ip[1] = ip[2] = ip[3] = 0;
    }
    else
    {
      const sockaddr_in6* a = &addr.sin6;
      ip[3] = (a->sin6_addr.s6_addr[15] << 24) + (a->sin6_addr.s6_addr[14] << 16) + (a->sin6_addr.s6_addr[13] << 8) + a->sin6_addr.s6_addr[12];
      ip[2] = (a->sin6_addr.s6_addr[11] << 24) + (a->sin6_addr.s6_addr[10] << 16) + (a->sin6_addr.s6_addr[9] << 8) + a->sin6_addr.s6_addr[8];
      ip[1] = (a->sin6_addr.s6_addr[7] << 24) + (a->sin6_addr.s6_addr[6] << 16) + (a->sin6_addr.s6_addr[5] << 8) + a->sin6_addr.s6_addr[4];
      ip[0] = (a->sin6_addr.s6_addr[3] << 24) + (a->sin6_addr.s6_addr[2] << 16) + (a->sin6_addr.s6_addr[1] << 8) + a->sin6_addr.s6_addr[0];
    }
}

namespace srt {
bool checkMappedIPv4(const uint16_t* addr)
{
    static const uint16_t ipv4on6_model [8] =
    {
        0, 0, 0, 0, 0, 0xFFFF, 0, 0
    };

    // Compare only first 6 words. Remaining 2 words
    // comprise the IPv4 address, if these first 6 match.
    const uint16_t* mbegin = ipv4on6_model;
    const uint16_t* mend = ipv4on6_model + 6;

    return std::equal(mbegin, mend, addr);
}
}

// XXX This has void return and the first argument is passed by reference.
// Consider simply returning sockaddr_any by value.
void srt::CIPAddress::pton(sockaddr_any& w_addr, const uint32_t ip[4], const sockaddr_any& peer)
{
    //using ::srt_logging::inlog;
    uint32_t* target_ipv4_addr = NULL;

    if (peer.family() == AF_INET)
    {
        sockaddr_in* a = (&w_addr.sin);
        target_ipv4_addr = (uint32_t*) &a->sin_addr.s_addr;
    }
    else // AF_INET6
    {
        // Check if the peer address is a model of IPv4-mapped-on-IPv6.
        // If so, it means that the `ip` array should be interpreted as IPv4.
        const bool is_mapped_ipv4 = checkMappedIPv4((uint16_t*)peer.sin6.sin6_addr.s6_addr);

        sockaddr_in6* a = (&w_addr.sin6);

        // This whole above procedure was only in order to EXCLUDE the
        // possibility of IPv4-mapped-on-IPv6. This below may only happen
        // if BOTH peers are IPv6. Otherwise we have a situation of cross-IP
        // version connection in which case the address in question is always
        // IPv4 in various mapping formats.
        if (!is_mapped_ipv4)
        {
            // Here both agent and peer use IPv6, in which case
            // `ip` contains the full IPv6 address, so just copy
            // it as is.

            // XXX Possibly, a simple
            // memcpy( (a->sin6_addr.s6_addr), ip, 16);
            // would do the same thing, and faster. The address in `ip`,
            // even though coded here as uint32_t, is still big endian.
            for (int i = 0; i < 4; ++ i)
            {
                a->sin6_addr.s6_addr[i * 4 + 0] = ip[i] & 0xFF;
                a->sin6_addr.s6_addr[i * 4 + 1] = (unsigned char)((ip[i] & 0xFF00) >> 8);
                a->sin6_addr.s6_addr[i * 4 + 2] = (unsigned char)((ip[i] & 0xFF0000) >> 16);
                a->sin6_addr.s6_addr[i * 4 + 3] = (unsigned char)((ip[i] & 0xFF000000) >> 24);
            }
            return; // The address is written, nothing left to do.
        }

        // 
        // IPv4 mapped on IPv6

        // Here agent uses IPv6 with IPPROTO_IPV6/IPV6_V6ONLY == 0
        // In this case, the address in `ip` is always an IPv4,
        // although we are not certain as to whether it's using the
        // IPv6 encoding (0::FFFF:IPv4) or SRT encoding (IPv4::0);
        // this must be extra determined.
        //
        // Unfortunately, sockaddr_in6 doesn't give any straightforward
        // method for it, although the official size of a single element
        // of the IPv6 address is 16-bit.

        memset((a->sin6_addr.s6_addr), 0, sizeof a->sin6_addr.s6_addr);

        // The sin6_addr.s6_addr32 is non that portable to use here.
        uint32_t* paddr32 = (uint32_t*)a->sin6_addr.s6_addr;
        uint16_t* paddr16 = (uint16_t*)a->sin6_addr.s6_addr;

        // layout: of IPv4 address 192.168.128.2
        // 16-bit:
        // [0000: 0000: 0000: 0000: 0000: FFFF: 192.168:128.2]
        // 8-bit
        // [00/00/00/00/00/00/00/00/00/00/FF/FF/192/168/128/2]
        // 32-bit
        // [00000000 && 00000000 && 0000FFFF && 192.168.128.2]

        // Spreading every 16-bit word separately to avoid endian dilemmas
        paddr16[2 * 2 + 1] = 0xFFFF;

        target_ipv4_addr = &paddr32[3];
    }

    // Now we have two possible formats of encoding the IPv4 address:
    // 1. If peer is IPv4, it's IPv4::0
    // 2. If peer is IPv6, it's 0::FFFF:IPv4.
    //
    // Has any other possibility happen here, copy an empty address,
    // which will be the only sign of an error.

    const uint16_t* peeraddr16 = (uint16_t*)ip;
    const bool is_mapped_ipv4 = checkMappedIPv4(peeraddr16);

    if (is_mapped_ipv4)
    {
        *target_ipv4_addr = ip[3];
        HLOGC(inlog.Debug, log << "pton: Handshake address: " << w_addr.str() << " provided in IPv6 mapping format");
    }
    // Check SRT IPv4 format.
    else if ((ip[1] | ip[2] | ip[3]) == 0)
    {
        *target_ipv4_addr = ip[0];
        HLOGC(inlog.Debug, log << "pton: Handshake address: " << w_addr.str() << " provided in SRT IPv4 format");
    }
    else
    {
        LOGC(inlog.Error, log << "pton: IPE or net error: can't determine IPv4 carryover format: " << std::hex
                << peeraddr16[0] << ":"
                << peeraddr16[1] << ":"
                << peeraddr16[2] << ":"
                << peeraddr16[3] << ":"
                << peeraddr16[4] << ":"
                << peeraddr16[5] << ":"
                << peeraddr16[6] << ":"
                << peeraddr16[7] << std::dec);
        *target_ipv4_addr = 0;
        if (peer.family() != AF_INET)
        {
            // Additionally overwrite the 0xFFFF that has been
            // just written 50 lines above.
            w_addr.sin6.sin6_addr.s6_addr[10] = 0;
            w_addr.sin6.sin6_addr.s6_addr[11] = 0;
        }
    }
}


namespace srt {
static string ShowIP4(const sockaddr_in* sin)
{
    ostringstream os;
    union
    {
        in_addr sinaddr;
        unsigned char ip[4];
    };
    sinaddr = sin->sin_addr;

    os << int(ip[0]);
    os << ".";
    os << int(ip[1]);
    os << ".";
    os << int(ip[2]);
    os << ".";
    os << int(ip[3]);
    return os.str();
}

static string ShowIP6(const sockaddr_in6* sin)
{
    ostringstream os;
    os.setf(ios::uppercase);

    bool sep = false;
    for (size_t i = 0; i < 16; ++i)
    {
        int v = sin->sin6_addr.s6_addr[i];
        if ( v )
        {
            if ( sep )
                os << ":";

            os << hex << v;
            sep = true;
        }
    }

    return os.str();
}

string CIPAddress::show(const sockaddr* adr)
{
    if ( adr->sa_family == AF_INET )
        return ShowIP4((const sockaddr_in*)adr);
    else if ( adr->sa_family == AF_INET6 )
        return ShowIP6((const sockaddr_in6*)adr);
    else
        return "(unsupported sockaddr type)";
}
} // namespace srt

//
void srt::CMD5::compute(const char* input, unsigned char result[16])
{
   md5_state_t state;

   md5_init(&state);
   md5_append(&state, (const md5_byte_t *)input, (int) strlen(input));
   md5_finish(&state, result);
}

namespace srt {
std::string MessageTypeStr(UDTMessageType mt, uint32_t extt)
{
    using std::string;

    static const char* const udt_types [] = {
        "handshake",
        "keepalive",
        "ack",
        "lossreport",
        "cgwarning", //4
        "shutdown",
        "ackack",
        "dropreq",
        "peererror", //8
    };

    static const char* const srt_types [] = {
        "EXT:none",
        "EXT:hsreq",
        "EXT:hsrsp",
        "EXT:kmreq",
        "EXT:kmrsp",
        "EXT:sid",
        "EXT:congctl",
        "EXT:filter",
        "EXT:group"
    };


    if ( mt == UMSG_EXT )
    {
        if ( extt >= Size(srt_types) )
            return "EXT:unknown";

        return srt_types[extt];
    }

    if ( size_t(mt) > Size(udt_types) )
        return "unknown";

    return udt_types[mt];
}

std::string ConnectStatusStr(EConnectStatus cst)
{
    return
          cst == CONN_CONTINUE ? "INDUCED/CONCLUDING"
        : cst == CONN_RUNNING ? "RUNNING"
        : cst == CONN_ACCEPT ? "ACCEPTED"
        : cst == CONN_RENDEZVOUS ? "RENDEZVOUS (HSv5)"
        : cst == CONN_AGAIN ? "AGAIN"
        : cst == CONN_CONFUSED ? "MISSING HANDSHAKE"
        : "REJECTED";
}

std::string TransmissionEventStr(ETransmissionEvent ev)
{
    static const char* const vals [] =
    {
        "init",
        "ack",
        "ackack",
        "lossreport",
        "checktimer",
        "send",
        "receive",
        "custom",
        "sync"
    };

    size_t vals_size = Size(vals);

    if (size_t(ev) >= vals_size)
        return "UNKNOWN";
    return vals[ev];
}

bool SrtParseConfig(string s, SrtConfig& w_config)
{
    using namespace std;

    vector<string> parts;
    Split(s, ',', back_inserter(parts));

    w_config.type = parts[0];

    for (vector<string>::iterator i = parts.begin()+1; i != parts.end(); ++i)
    {
        vector<string> keyval;
        Split(*i, ':', back_inserter(keyval));
        if (keyval.size() != 2)
            return false;
        if (keyval[1] != "")
            w_config.parameters[keyval[0]] = keyval[1];
    }

    return true;
}
} // namespace srt

namespace srt_logging
{

// Value display utilities
// (also useful for applications)

std::string SockStatusStr(SRT_SOCKSTATUS s)
{
    if (int(s) < int(SRTS_INIT) || int(s) > int(SRTS_NONEXIST))
        return "???";

    static struct AutoMap
    {
        // Values start from 1, so do -1 to avoid empty cell
        std::string names[int(SRTS_NONEXIST)-1+1];

        AutoMap()
        {
#define SINI(statename) names[SRTS_##statename-1] = #statename
            SINI(INIT);
            SINI(OPENED);
            SINI(LISTENING);
            SINI(CONNECTING);
            SINI(CONNECTED);
            SINI(BROKEN);
            SINI(CLOSING);
            SINI(CLOSED);
            SINI(NONEXIST);
#undef SINI
        }
    } names;

    return names.names[int(s)-1];
}

#if ENABLE_BONDING
std::string MemberStatusStr(SRT_MEMBERSTATUS s)
{
    if (int(s) < int(SRT_GST_PENDING) || int(s) > int(SRT_GST_BROKEN))
        return "???";

    static struct AutoMap
    {
        std::string names[int(SRT_GST_BROKEN)+1];

        AutoMap()
        {
#define SINI(statename) names[SRT_GST_##statename] = #statename
            SINI(PENDING);
            SINI(IDLE);
            SINI(RUNNING);
            SINI(BROKEN);
#undef SINI
        }
    } names;

    return names.names[int(s)];
}
#endif

// Logging system implementation

#if ENABLE_LOGGING

srt::logging::LogDispatcher::Proxy::Proxy(LogDispatcher& guy) : that(guy), that_enabled(that.CheckEnabled())
{
    if (that_enabled)
    {
        i_file = "";
        i_line = 0;
        flags = that.src_config->flags;
        // Create logger prefix
        that.CreateLogLinePrefix(os);
    }
}

LogDispatcher::Proxy LogDispatcher::operator()()
{
    return Proxy(*this);
}

void LogDispatcher::CreateLogLinePrefix(std::ostringstream& serr)
{
    using namespace std;
    using namespace srt;

    SRT_STATIC_ASSERT(ThreadName::BUFSIZE >= sizeof("hh:mm:ss.") * 2, // multiply 2 for some margin
                      "ThreadName::BUFSIZE is too small to be used for strftime");
    char tmp_buf[ThreadName::BUFSIZE];
    if ( !isset(SRT_LOGF_DISABLE_TIME) )
    {
        // Not necessary if sending through the queue.
        timeval tv;
        gettimeofday(&tv, NULL);
        struct tm tm = SysLocalTime((time_t) tv.tv_sec);

        if (strftime(tmp_buf, sizeof(tmp_buf), "%X.", &tm))
        {
            serr << tmp_buf << setw(6) << setfill('0') << tv.tv_usec;
        }
    }

    string out_prefix;
    if ( !isset(SRT_LOGF_DISABLE_SEVERITY) )
    {
        out_prefix = prefix;
    }

    // Note: ThreadName::get needs a buffer of size min. ThreadName::BUFSIZE
    if ( !isset(SRT_LOGF_DISABLE_THREADNAME) && ThreadName::get(tmp_buf) )
    {
        serr << "/" << tmp_buf << out_prefix << ": ";
    }
    else
    {
        serr << out_prefix << ": ";
    }
}

std::string LogDispatcher::Proxy::ExtractName(std::string pretty_function)
{
    if ( pretty_function == "" )
        return "";
    size_t pos = pretty_function.find('(');
    if ( pos == std::string::npos )
        return pretty_function; // return unchanged.

    pretty_function = pretty_function.substr(0, pos);

    // There are also template instantiations where the instantiating
    // parameters are encrypted inside. Therefore, search for the first
    // open < and if found, search for symmetric >.

    int depth = 1;
    pos = pretty_function.find('<');
    if ( pos != std::string::npos )
    {
        size_t end = pos+1;
        for(;;)
        {
            ++pos;
            if ( pos == pretty_function.size() )
            {
                --pos;
                break;
            }
            if ( pretty_function[pos] == '<' )
            {
                ++depth;
                continue;
            }

            if ( pretty_function[pos] == '>' )
            {
                --depth;
                if ( depth <= 0 )
                    break;
                continue;
            }
        }

        std::string afterpart = pretty_function.substr(pos+1);
        pretty_function = pretty_function.substr(0, end) + ">" + afterpart;
    }

    // Now see how many :: can be found in the name.
    // If this occurs more than once, take the last two.
    pos = pretty_function.rfind("::");

    if ( pos == std::string::npos || pos < 2 )
        return pretty_function; // return whatever this is. No scope name.

    // Find the next occurrence of :: - if found, copy up to it. If not,
    // return whatever is found.
    pos -= 2;
    pos = pretty_function.rfind("::", pos);
    if ( pos == std::string::npos )
        return pretty_function; // nothing to cut

    return pretty_function.substr(pos+2);
}
#endif

} // (end namespace srt_logging)

