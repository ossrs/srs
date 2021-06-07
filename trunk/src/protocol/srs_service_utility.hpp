//
// Copyright (c) 2013-2021 Winlin
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_SERVICE_UTILITY_HPP
#define SRS_SERVICE_UTILITY_HPP

#include <srs_core.hpp>

#include <arpa/inet.h>
#include <string>
#include <vector>

#include <srs_service_st.hpp>

class ISrsHttpMessage;

// Whether the url is starts with http:// or https://
extern bool srs_string_is_http(std::string url);
extern bool srs_string_is_rtmp(std::string url);

// Whether string is digit number
//      is_digit("0")  === true
//      is_digit("0000000000")  === true
//      is_digit("1234567890")  === true
//      is_digit("0123456789")  === true
//      is_digit("1234567890a") === false
//      is_digit("a1234567890") === false
//      is_digit("10e3") === false
//      is_digit("!1234567890") === false
//      is_digit("") === false
extern bool srs_is_digit_number(std::string str);

// Get local ip, fill to @param ips
struct SrsIPAddress
{
    // The network interface name, such as eth0, en0, eth1.
    std::string ifname;
    // The IP v4 or v6 address.
    std::string ip;
    // Whether the ip is IPv4 address.
    bool is_ipv4;
    // Whether the ip is internet public IP address.
    bool is_internet;
    // Whether the ip is loopback, such as 127.0.0.1
    bool is_loopback;
};
extern std::vector<SrsIPAddress*>& srs_get_local_ips();

// Get local public ip, empty string if no public internet address found.
extern std::string srs_get_public_internet_address(bool ipv4_only = false);

// Detect whether specified device is internet public address.
extern bool srs_net_device_is_internet(std::string ifname);
extern bool srs_net_device_is_internet(const sockaddr* addr);

// Get the original ip from query and header by proxy.
extern std::string srs_get_original_ip(ISrsHttpMessage* r);

// Get hostname
extern std::string srs_get_system_hostname(void);

#endif

