/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2017 OSSRS(winlin)
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

#include <srs_service_utility.hpp>

#include <unistd.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <map>
#include <sstream>
using namespace std;

#include <srs_service_st.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_consts.hpp>
#include <srs_kernel_log.hpp>
#include <srs_kernel_utility.hpp>

int srs_socket_connect(string server, int port, int64_t tm, st_netfd_t* pstfd)
{
    int ret = ERROR_SUCCESS;
    
    st_utime_t timeout = ST_UTIME_NO_TIMEOUT;
    if (tm != SRS_CONSTS_NO_TMMS) {
        timeout = (st_utime_t)(tm * 1000);
    }
    
    *pstfd = NULL;
    st_netfd_t stfd = NULL;
    sockaddr_in addr;
    
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if(sock == -1){
        ret = ERROR_SOCKET_CREATE;
        srs_error("create socket error. ret=%d", ret);
        return ret;
    }
    
    srs_assert(!stfd);
    stfd = st_netfd_open_socket(sock);
    if(stfd == NULL){
        ret = ERROR_ST_OPEN_SOCKET;
        srs_error("st_netfd_open_socket failed. ret=%d", ret);
        return ret;
    }
    
    // connect to server.
    std::string ip = srs_dns_resolve(server);
    if (ip.empty()) {
        ret = ERROR_SYSTEM_IP_INVALID;
        srs_error("dns resolve server error, ip empty. ret=%d", ret);
        goto failed;
    }
    
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(ip.c_str());
    
    if (st_connect(stfd, (const struct sockaddr*)&addr, sizeof(sockaddr_in), timeout) == -1){
        ret = ERROR_ST_CONNECT;
        srs_error("connect to server error. ip=%s, port=%d, ret=%d", ip.c_str(), port, ret);
        goto failed;
    }
    srs_info("connect ok. server=%s, ip=%s, port=%d", server.c_str(), ip.c_str(), port);
    
    *pstfd = stfd;
    return ret;
    
failed:
    if (stfd) {
        srs_close_stfd(stfd);
    }
    return ret;
}

bool srs_string_is_http(string url)
{
    return srs_string_starts_with(url, "http://", "https://");
}

bool srs_string_is_rtmp(string url)
{
    return srs_string_starts_with(url, "rtmp://");
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

bool srs_net_device_is_internet(in_addr_t addr)
{
    uint32_t addr_h = ntohl(addr);
    
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
    
    return true;
}

vector<string> _srs_system_ipv4_ips;

void retrieve_local_ipv4_ips()
{
    vector<string>& ips = _srs_system_ipv4_ips;
    
    ips.clear();
    
    ifaddrs* ifap;
    if (getifaddrs(&ifap) == -1) {
        srs_warn("retrieve local ips, ini ifaddrs failed.");
        return;
    }
    
    stringstream ss0;
    ss0 << "ips";
    
    stringstream ss1;
    ss1 << "devices";
    
    ifaddrs* p = ifap;
    while (p != NULL) {
        ifaddrs* cur = p;
        sockaddr* addr = cur->ifa_addr;
        p = p->ifa_next;
        
        // retrieve ipv4 addr
        // ignore the tun0 network device,
        // which addr is NULL.
        // @see: https://github.com/ossrs/srs/issues/141
        if (addr && addr->sa_family == AF_INET) {
            in_addr* inaddr = &((sockaddr_in*)addr)->sin_addr;
            
            char buf[16];
            memset(buf, 0, sizeof(buf));
            
            if ((inet_ntop(addr->sa_family, inaddr, buf, sizeof(buf))) == NULL) {
                srs_warn("convert local ip failed");
                break;
            }
            
            std::string ip = buf;
            if (ip != SRS_CONSTS_LOCALHOST) {
                ss0 << ", local[" << (int)ips.size() << "] ipv4 " << ip;
                ips.push_back(ip);
            }
            
            // set the device internet status.
            if (!srs_net_device_is_internet(inaddr->s_addr)) {
                ss1 << ", intranet ";
                _srs_device_ifs[cur->ifa_name] = false;
            } else {
                ss1 << ", internet ";
                _srs_device_ifs[cur->ifa_name] = true;
            }
            ss1 << cur->ifa_name << " " << ip;
        }
    }
    srs_trace(ss0.str().c_str());
    srs_trace(ss1.str().c_str());
    
    freeifaddrs(ifap);
}

vector<string>& srs_get_local_ipv4_ips()
{
    if (_srs_system_ipv4_ips.empty()) {
        retrieve_local_ipv4_ips();
    }
    
    return _srs_system_ipv4_ips;
}

std::string _public_internet_address;

string srs_get_public_internet_address()
{
    if (!_public_internet_address.empty()) {
        return _public_internet_address;
    }
    
    std::vector<std::string>& ips = srs_get_local_ipv4_ips();
    
    // find the best match public address.
    for (int i = 0; i < (int)ips.size(); i++) {
        std::string ip = ips[i];
        in_addr_t addr = inet_addr(ip.c_str());
        uint32_t addr_h = ntohl(addr);
        // lo, 127.0.0.0-127.0.0.1
        if (addr_h >= 0x7f000000 && addr_h <= 0x7f000001) {
            srs_trace("ignore private address: %s", ip.c_str());
            continue;
        }
        // Class A 10.0.0.0-10.255.255.255
        if (addr_h >= 0x0a000000 && addr_h <= 0x0affffff) {
            srs_trace("ignore private address: %s", ip.c_str());
            continue;
        }
        // Class B 172.16.0.0-172.31.255.255
        if (addr_h >= 0xac100000 && addr_h <= 0xac1fffff) {
            srs_trace("ignore private address: %s", ip.c_str());
            continue;
        }
        // Class C 192.168.0.0-192.168.255.255
        if (addr_h >= 0xc0a80000 && addr_h <= 0xc0a8ffff) {
            srs_trace("ignore private address: %s", ip.c_str());
            continue;
        }
        srs_warn("use public address as ip: %s", ip.c_str());
        
        _public_internet_address = ip;
        return ip;
    }
    
    // no public address, use private address.
    for (int i = 0; i < (int)ips.size(); i++) {
        std::string ip = ips[i];
        in_addr_t addr = inet_addr(ip.c_str());
        uint32_t addr_h = ntohl(addr);
        // lo, 127.0.0.0-127.0.0.1
        if (addr_h >= 0x7f000000 && addr_h <= 0x7f000001) {
            srs_trace("ignore private address: %s", ip.c_str());
            continue;
        }
        srs_warn("use private address as ip: %s", ip.c_str());
        
        _public_internet_address = ip;
        return ip;
    }
    
    return "";
}

