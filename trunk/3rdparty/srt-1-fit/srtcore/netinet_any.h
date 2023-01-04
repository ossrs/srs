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
written by
   Haivision Systems Inc.
 *****************************************************************************/

#ifndef INC_SRT_NETINET_ANY_H
#define INC_SRT_NETINET_ANY_H

#include <cstring> // memcmp
#include <string>
#include <sstream>
#include "platform_sys.h"

// This structure should replace every use of sockaddr and its currently
// used specializations, sockaddr_in and sockaddr_in6. This is to simplify
// the use of the original BSD API that relies on type-violating type casts.
// You can use the instances of sockaddr_any in every place where sockaddr is
// required.

namespace srt
{

struct sockaddr_any
{
    union
    {
        sockaddr_in sin;
        sockaddr_in6 sin6;
        sockaddr sa;
    };

    // The type is intended to be the same as the length
    // parameter in ::accept, ::bind and ::connect functions.

    // This is the type used by SRT.
    typedef int len_t;

    // This is the type used by system functions
#ifdef _WIN32
    typedef int syslen_t;
#else
    typedef socklen_t syslen_t;
#endif

    // Note: by having `len_t` type here the usage in
    // API functions is here limited to SRT. For system
    // functions you can pass the address here as (socklen_t*)&sa.len,
    // but just do it on your own risk, as there's no guarantee
    // that sizes of `int` and `socklen_t` do not differ. The safest
    // way seems to be using an intermediate proxy to be written
    // back here from the value of `syslen_t`.
    len_t len;

    struct SysLenWrapper
    {
        syslen_t syslen;
        len_t& backwriter;
        syslen_t* operator&() { return &syslen; }

        SysLenWrapper(len_t& source): syslen(source), backwriter(source)
        {
        }

        ~SysLenWrapper()
        {
            backwriter = syslen;
        }
    };

    // Usage:
    //    ::accept(lsn_sock, sa.get(), &sa.syslen());
    SysLenWrapper syslen()
    {
        return SysLenWrapper(len);
    }

    static size_t storage_size()
    {
        typedef union
        {
            sockaddr_in sin;
            sockaddr_in6 sin6;
            sockaddr sa;
        } ucopy;
        return sizeof (ucopy);
    }

    void reset()
    {
        // sin6 is the largest field
        memset((&sin6), 0, sizeof sin6);
        len = 0;
    }

    // Default domain is unspecified, and
    // in this case the size is 0.
    // Note that AF_* (and alias PF_*) types have
    // many various values, of which only
    // AF_INET and AF_INET6 are handled here.
    // Others make the same effect as unspecified.
    explicit sockaddr_any(int domain = AF_UNSPEC)
    {
        // Default domain is "unspecified", 0
        reset();

        // Overriding family as required in the parameters
        // and the size then accordingly.
        sa.sa_family = domain == AF_INET || domain == AF_INET6 ? domain : AF_UNSPEC;
        switch (domain)
        {
        case AF_INET:
            len = len_t(sizeof (sockaddr_in));
            break;

            // Use size of sin6 as the default size
            // len must be properly set so that the
            // family-less sockaddr is passed to bind/accept
        default:
            len = len_t(sizeof (sockaddr_in6));
            break;
        }
    }

    sockaddr_any(const sockaddr_storage& stor)
    {
        // Here the length isn't passed, so just rely on family.
        set((const sockaddr*)&stor);
    }

    sockaddr_any(const sockaddr* source, len_t namelen = 0)
    {
        if (namelen == 0)
            set(source);
        else
            set(source, namelen);
    }

    void set(const sockaddr* source)
    {
        // Less safe version, simply trust the caller that the
        // memory at 'source' is also large enough to contain
        // all data required for particular family.
        if (source->sa_family == AF_INET)
        {
            memcpy((&sin), source, sizeof sin);
            len = sizeof sin;
        }
        else if (source->sa_family == AF_INET6)
        {
            memcpy((&sin6), source, sizeof sin6);
            len = sizeof sin6;
        }
        else
        {
            // Error fallback: no other families than IP are regarded.
            // Note: socket set up this way isn't intended to be used
            // for bind/accept.
            sa.sa_family = AF_UNSPEC;
            len = 0;
        }
    }

    void set(const sockaddr* source, syslen_t namelen)
    {
        // It's not safe to copy it directly, so check.
        if (source->sa_family == AF_INET && namelen >= syslen_t(sizeof sin))
        {
            memcpy((&sin), source, sizeof sin);
            len = sizeof sin;
        }
        else if (source->sa_family == AF_INET6 && namelen >= syslen_t(sizeof sin6))
        {
            // Note: this isn't too safe, may crash for stupid values
            // of source->sa_family or any other data
            // in the source structure, so make sure it's correct first.
            memcpy((&sin6), source, sizeof sin6);
            len = sizeof sin6;
        }
        else
        {
            reset();
        }
    }

    void set(const sockaddr_in& in4)
    {
        memcpy((&sin), &in4, sizeof in4);
        len = sizeof in4;
    }

    void set(const sockaddr_in6& in6)
    {
        memcpy((&sin6), &in6, sizeof in6);
        len = sizeof in6;
    }

    sockaddr_any(const in_addr& i4_adr, uint16_t port)
    {
        // Some cases require separately IPv4 address passed as in_addr,
        // so port is given separately.
        sa.sa_family = AF_INET;
        sin.sin_addr = i4_adr;
        sin.sin_port = htons(port);
        len = sizeof sin;
    }

    sockaddr_any(const in6_addr& i6_adr, uint16_t port)
    {
        sa.sa_family = AF_INET6;
        sin6.sin6_addr = i6_adr;
        sin6.sin6_port = htons(port);
        len = sizeof sin6;
    }

    static len_t size(int family)
    {
        switch (family)
        {
        case AF_INET:
            return len_t(sizeof (sockaddr_in));

        case AF_INET6:
            return len_t(sizeof (sockaddr_in6));

        default:
            return 0; // fallback
        }
    }

    bool empty() const
    {
        bool isempty = true;  // unspec-family address is always empty

        if (sa.sa_family == AF_INET)
        {
            isempty = (sin.sin_port == 0
                    && sin.sin_addr.s_addr == 0);
        }
        else if (sa.sa_family == AF_INET6)
        {
            isempty = (sin6.sin6_port == 0
                    && memcmp(&sin6.sin6_addr, &in6addr_any, sizeof in6addr_any) == 0);
        }
        // otherwise isempty stays with default false
        return isempty;
    }

    len_t size() const
    {
        return size(sa.sa_family);
    }

    int family() const { return sa.sa_family; }
    void family(int val)
    {
        sa.sa_family = val;
        len = size();
    }

    // port is in exactly the same location in both sin and sin6
    // and has the same size. This is actually yet another common
    // field, just not mentioned in the sockaddr structure.
    uint16_t& r_port() { return sin.sin_port; }
    uint16_t r_port() const { return sin.sin_port; }
    int hport() const { return ntohs(sin.sin_port); }

    void hport(int value)
    {
        // Port is fortunately located at the same position
        // in both sockaddr_in and sockaddr_in6 and has the
        // same size.
        sin.sin_port = htons(value);
    }

    sockaddr* get() { return &sa; }
    const sockaddr* get() const { return &sa; }

    // Sometimes you need to get the address
    // the way suitable for e.g. inet_ntop.
    const void* get_addr() const
    {
        if (sa.sa_family == AF_INET)
            return &sin.sin_addr.s_addr;

        if (sa.sa_family == AF_INET6)
            return &sin6.sin6_addr;

        return NULL;
    }

    void* get_addr()
    {
        const sockaddr_any* that = this;
        return (void*)that->get_addr();
    }

    template <int> struct TypeMap;

    template <int af_domain>
    typename TypeMap<af_domain>::type& get();

    struct Equal
    {
        bool operator()(const sockaddr_any& c1, const sockaddr_any& c2)
        {
            if (c1.family() != c2.family())
                return false;

            // Cannot use memcmp due to having in some systems
            // another field like sockaddr_in::sin_len. This exists
            // in some BSD-derived systems, but is not required by POSIX.
            // Therefore sockaddr_any class cannot operate with it,
            // as in this situation it would be safest to state that
            // particular implementations may have additional fields
            // of different purpose beside those required by POSIX.
            //
            // The only reliable way to compare two underlying sockaddr
            // object is then to compare the port value and the address
            // value.
            //
            // Fortunately the port is 16-bit and located at the same
            // offset in both sockaddr_in and sockaddr_in6.

            return c1.sin.sin_port == c2.sin.sin_port
                && c1.equal_address(c2);
        }
    };

    struct EqualAddress
    {
        bool operator()(const sockaddr_any& c1, const sockaddr_any& c2)
        {
            if ( c1.sa.sa_family == AF_INET )
            {
                return c1.sin.sin_addr.s_addr == c2.sin.sin_addr.s_addr;
            }

            if ( c1.sa.sa_family == AF_INET6 )
            {
                return memcmp(&c1.sin6.sin6_addr, &c2.sin6.sin6_addr, sizeof (in6_addr)) == 0;
            }

            return false;
        }

    };

    bool equal_address(const sockaddr_any& rhs) const
    {
        return EqualAddress()(*this, rhs);
    }

    struct Less
    {
        bool operator()(const sockaddr_any& c1, const sockaddr_any& c2)
        {
            return memcmp(&c1, &c2, sizeof(c1)) < 0;
        }
    };

    // Tests if the current address is the "any" wildcard.
    bool isany() const
    {
        if (sa.sa_family == AF_INET)
            return sin.sin_addr.s_addr == INADDR_ANY;

        if (sa.sa_family == AF_INET6)
            return memcmp(&sin6.sin6_addr, &in6addr_any, sizeof in6addr_any) == 0;

        return false;
    }

    // Debug support
    std::string str() const
    {
        if (family() != AF_INET && family() != AF_INET6)
            return "unknown:0";

        std::ostringstream output;
        char hostbuf[1024];
        int flags;

    #if ENABLE_GETNAMEINFO
        flags = NI_NAMEREQD;
    #else
        flags = NI_NUMERICHOST | NI_NUMERICSERV;
    #endif

        if (!getnameinfo(get(), size(), hostbuf, 1024, NULL, 0, flags))
        {
            output << hostbuf;
        }

        output << ":" << hport();
        return output.str();
    }

    bool operator==(const sockaddr_any& other) const
    {
        return Equal()(*this, other);
    }

    bool operator!=(const sockaddr_any& other) const { return !(*this == other); }
};

template<> struct sockaddr_any::TypeMap<AF_INET> { typedef sockaddr_in type; };
template<> struct sockaddr_any::TypeMap<AF_INET6> { typedef sockaddr_in6 type; };

template <>
inline sockaddr_any::TypeMap<AF_INET>::type& sockaddr_any::get<AF_INET>() { return sin; }
template <>
inline sockaddr_any::TypeMap<AF_INET6>::type& sockaddr_any::get<AF_INET6>() { return sin6; }

} // namespace srt

#endif
