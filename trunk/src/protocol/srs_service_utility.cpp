//
// Copyright (c) 2013-2021 Winlin
//
// SPDX-License-Identifier: MIT
//

#include <srs_service_utility.hpp>

#include <unistd.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <math.h>
#include <stdlib.h>
#include <map>
#include <sstream>
using namespace std;

#include <srs_service_st.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_consts.hpp>
#include <srs_kernel_log.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_http_stack.hpp>

bool srs_string_is_http(string url)
{
    return srs_string_starts_with(url, "http://", "https://");
}

bool srs_string_is_rtmp(string url)
{
    return srs_string_starts_with(url, "rtmp://");
}

bool srs_is_digit_number(string str)
{
    if (str.empty()) {
        return false;
    }

    const char* p = str.c_str();
    const char* p_end = str.data() + str.length();
    for (; p < p_end; p++) {
        if (*p != '0') {
            break;
        }
    }
    if (p == p_end) {
        return true;
    }

    int64_t v = ::atoll(p);
    int64_t powv = (int64_t)pow(10, p_end - p - 1);
    return  v / powv >= 1 && v / powv <= 9;
}

// we detect all network device as internet or intranet device, by its ip address.
//      key is device name, for instance, eth0
//      value is whether internet, for instance, true.
static std::map<std::string, bool> _srs_device_ifs;

bool srs_net_device_is_internet(string ifname)
{
    srs_info("check ifname=%s", ifname.c_str());
    
    if (_srs_device_ifs.find(ifname) == _srs_device_ifs.end()) {
        return false;
    }
    return _srs_device_ifs[ifname];
}

bool srs_net_device_is_internet(const sockaddr* addr)
{
    if(addr->sa_family == AF_INET) {
        const in_addr inaddr = ((sockaddr_in*)addr)->sin_addr;
        const uint32_t addr_h = ntohl(inaddr.s_addr);

        // lo, 127.0.0.0-127.0.0.1
        if (addr_h >= 0x7f000000 && addr_h <= 0x7f000001) {
            return false;
        }

        // Class A 10.0.0.0-10.255.255.255
        if (addr_h >= 0x0a000000 && addr_h <= 0x0affffff) {
            return false;
        }

        // Class B 172.16.0.0-172.31.255.255
        if (addr_h >= 0xac100000 && addr_h <= 0xac1fffff) {
            return false;
        }

        // Class C 192.168.0.0-192.168.255.255
        if (addr_h >= 0xc0a80000 && addr_h <= 0xc0a8ffff) {
            return false;
        }
    } else if(addr->sa_family == AF_INET6) {
        const sockaddr_in6* a6 = (const sockaddr_in6*)addr;

        // IPv6 loopback is ::1
        if (IN6_IS_ADDR_LOOPBACK(&a6->sin6_addr)) {
            return false;
        }

        // IPv6 unspecified is ::
        if (IN6_IS_ADDR_UNSPECIFIED(&a6->sin6_addr)) {
            return false;
        }

        // From IPv4, you might know APIPA (Automatic Private IP Addressing) or AutoNet.
        // Whenever automatic IP configuration through DHCP fails.
        // The prefix of a site-local address is FE80::/10.
        if (IN6_IS_ADDR_LINKLOCAL(&a6->sin6_addr)) {
            return false;
        }

        // Site-local addresses are equivalent to private IP addresses in IPv4.
        // The prefix of a site-local address is FEC0::/10.
        // https://4sysops.com/archives/ipv6-tutorial-part-6-site-local-addresses-and-link-local-addresses/
        if (IN6_IS_ADDR_SITELOCAL(&a6->sin6_addr)) {
           return false;
        }

        // Others.
        if (IN6_IS_ADDR_MULTICAST(&a6->sin6_addr)) {
            return false;
        }
        if (IN6_IS_ADDR_MC_NODELOCAL(&a6->sin6_addr)) {
            return false;
        }
        if (IN6_IS_ADDR_MC_LINKLOCAL(&a6->sin6_addr)) {
            return false;
        }
        if (IN6_IS_ADDR_MC_SITELOCAL(&a6->sin6_addr)) {
            return false;
        }
        if (IN6_IS_ADDR_MC_ORGLOCAL(&a6->sin6_addr)) {
            return false;
        }
        if (IN6_IS_ADDR_MC_GLOBAL(&a6->sin6_addr)) {
            return false;
        }
    }
    
    return true;
}

vector<SrsIPAddress*> _srs_system_ips;

void discover_network_iface(ifaddrs* cur, vector<SrsIPAddress*>& ips, stringstream& ss0, stringstream& ss1, bool ipv6, bool loopback)
{
    char saddr[64];
    char* h = (char*)saddr;
    socklen_t nbh = (socklen_t)sizeof(saddr);
    const int r0 = getnameinfo(cur->ifa_addr, sizeof(sockaddr_storage), h, nbh, NULL, 0, NI_NUMERICHOST);
    if(r0) {
        srs_warn("convert local ip failed: %s", gai_strerror(r0));
        return;
    }
    
    std::string ip(saddr, strlen(saddr));
    ss0 << ", iface[" << (int)ips.size() << "] " << cur->ifa_name << " " << (ipv6? "ipv6":"ipv4")
    << " 0x" << std::hex << cur->ifa_flags  << std::dec << " " << ip;

    SrsIPAddress* ip_address = new SrsIPAddress();
    ip_address->ip = ip;
    ip_address->is_ipv4 = !ipv6;
    ip_address->is_loopback = loopback;
    ip_address->ifname = cur->ifa_name;
    ip_address->is_internet = srs_net_device_is_internet(cur->ifa_addr);
    ips.push_back(ip_address);
    
    // set the device internet status.
    if (!ip_address->is_internet) {
        ss1 << ", intranet ";
        _srs_device_ifs[cur->ifa_name] = false;
    } else {
        ss1 << ", internet ";
        _srs_device_ifs[cur->ifa_name] = true;
    }
    ss1 << cur->ifa_name << " " << ip;
}

void retrieve_local_ips()
{
    vector<SrsIPAddress*>& ips = _srs_system_ips;

    // Release previous IPs.
    for (int i = 0; i < (int)ips.size(); i++) {
        SrsIPAddress* ip = ips[i];
        srs_freep(ip);
    }
    ips.clear();

    // Get the addresses.
    ifaddrs* ifap;
    if (getifaddrs(&ifap) == -1) {
        srs_warn("retrieve local ips, getifaddrs failed.");
        return;
    }
    
    stringstream ss0;
    ss0 << "ips";
    
    stringstream ss1;
    ss1 << "devices";
    
    // Discover IPv4 first.
    for (ifaddrs* p = ifap; p ; p = p->ifa_next) {
        ifaddrs* cur = p;
        
        // Ignore if no address for this interface.
        // @see https://github.com/ossrs/srs/issues/1087#issuecomment-408847115
        if (!cur->ifa_addr) {
            continue;
        }
        
        // retrieve IP address, ignore the tun0 network device, whose addr is NULL.
        // @see: https://github.com/ossrs/srs/issues/141
        bool ipv4 = (cur->ifa_addr->sa_family == AF_INET);
        bool ready = (cur->ifa_flags & IFF_UP) && (cur->ifa_flags & IFF_RUNNING);
        // Ignore IFF_PROMISC(Interface is in promiscuous mode), which may be set by Wireshark.
        bool ignored = (!cur->ifa_addr) || (cur->ifa_flags & IFF_LOOPBACK) || (cur->ifa_flags & IFF_POINTOPOINT);
        bool loopback = (cur->ifa_flags & IFF_LOOPBACK);
        if (ipv4 && ready && !ignored) {
            discover_network_iface(cur, ips, ss0, ss1, false, loopback);
        }
    }
    
    // Then, discover IPv6 addresses.
    for (ifaddrs* p = ifap; p ; p = p->ifa_next) {
        ifaddrs* cur = p;
        
        // Ignore if no address for this interface.
        // @see https://github.com/ossrs/srs/issues/1087#issuecomment-408847115
        if (!cur->ifa_addr) {
            continue;
        }
        
        // retrieve IP address, ignore the tun0 network device, whose addr is NULL.
        // @see: https://github.com/ossrs/srs/issues/141
        bool ipv6 = (cur->ifa_addr->sa_family == AF_INET6);
        bool ready = (cur->ifa_flags & IFF_UP) && (cur->ifa_flags & IFF_RUNNING);
        bool ignored = (!cur->ifa_addr) || (cur->ifa_flags & IFF_POINTOPOINT) || (cur->ifa_flags & IFF_PROMISC) || (cur->ifa_flags & IFF_LOOPBACK);
        bool loopback = (cur->ifa_flags & IFF_LOOPBACK);
        if (ipv6 && ready && !ignored) {
            discover_network_iface(cur, ips, ss0, ss1, true, loopback);
        }
    }
    
    // If empty, disover IPv4 loopback.
    if (ips.empty()) {
        for (ifaddrs* p = ifap; p ; p = p->ifa_next) {
            ifaddrs* cur = p;
            
            // Ignore if no address for this interface.
            // @see https://github.com/ossrs/srs/issues/1087#issuecomment-408847115
            if (!cur->ifa_addr) {
                continue;
            }
            
            // retrieve IP address, ignore the tun0 network device, whose addr is NULL.
            // @see: https://github.com/ossrs/srs/issues/141
            bool ipv4 = (cur->ifa_addr->sa_family == AF_INET);
            bool ready = (cur->ifa_flags & IFF_UP) && (cur->ifa_flags & IFF_RUNNING);
            bool ignored = (!cur->ifa_addr) || (cur->ifa_flags & IFF_POINTOPOINT) || (cur->ifa_flags & IFF_PROMISC);
            bool loopback = (cur->ifa_flags & IFF_LOOPBACK);
            if (ipv4 && ready && !ignored) {
                discover_network_iface(cur, ips, ss0, ss1, false, loopback);
            }
        }
    }
    
    srs_trace("%s", ss0.str().c_str());
    srs_trace("%s", ss1.str().c_str());
    
    freeifaddrs(ifap);
}

vector<SrsIPAddress*>& srs_get_local_ips()
{
    if (_srs_system_ips.empty()) {
        retrieve_local_ips();
    }
    
    return _srs_system_ips;
}

std::string _public_internet_address;

string srs_get_public_internet_address(bool ipv4_only)
{
    if (!_public_internet_address.empty()) {
        return _public_internet_address;
    }
    
    std::vector<SrsIPAddress*>& ips = srs_get_local_ips();
    
    // find the best match public address.
    for (int i = 0; i < (int)ips.size(); i++) {
        SrsIPAddress* ip = ips[i];
        if (!ip->is_internet) {
            continue;
        }
        if (ipv4_only && !ip->is_ipv4) {
            continue;
        }

        srs_warn("use public address as ip: %s, ifname=%s", ip->ip.c_str(), ip->ifname.c_str());
        _public_internet_address = ip->ip;
        return ip->ip;
    }
    
    // no public address, use private address.
    for (int i = 0; i < (int)ips.size(); i++) {
        SrsIPAddress* ip = ips[i];
        if (ip->is_loopback) {
            continue;
        }
        if (ipv4_only && !ip->is_ipv4) {
            continue;
        }

        srs_warn("use private address as ip: %s, ifname=%s", ip->ip.c_str(), ip->ifname.c_str());
        _public_internet_address = ip->ip;
        return ip->ip;
    }
    
    // Finally, use first whatever kind of address.
    if (!ips.empty() && _public_internet_address.empty()) {
        SrsIPAddress* ip = ips[0];

        srs_warn("use first address as ip: %s, ifname=%s", ip->ip.c_str(), ip->ifname.c_str());
        _public_internet_address = ip->ip;
        return ip->ip;
    }
    
    return "";
}

string srs_get_original_ip(ISrsHttpMessage* r)
{
    SrsHttpHeader* h = r->header();

    string x_forwarded_for = h->get("X-Forwarded-For");
    if (!x_forwarded_for.empty()) {
        size_t pos = string::npos;
        if ((pos = x_forwarded_for.find(",")) == string::npos) {
            return x_forwarded_for;
        }
        return x_forwarded_for.substr(0, pos);
    }

    string x_real_ip = h->get("X-Real-IP");
    if (!x_real_ip.empty()) {
        size_t pos = string::npos;
        if ((pos = x_real_ip.find(":")) == string::npos) {
            return x_real_ip;
        }
        return x_real_ip.substr(0, pos);
    }

    return "";
}


std::string _srs_system_hostname;

string srs_get_system_hostname()
{
    if (!_srs_system_hostname.empty()) {
        return _srs_system_hostname;
    }

    char buf[256];
    if (-1 == gethostname(buf, sizeof(buf))) {
        srs_warn("gethostbyname fail");
        return "";
    }

    _srs_system_hostname = std::string(buf);
    return _srs_system_hostname;
}
