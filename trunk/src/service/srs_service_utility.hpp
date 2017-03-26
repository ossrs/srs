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

#ifndef SRS_SERVICE_UTILITY_HPP
#define SRS_SERVICE_UTILITY_HPP

#include <srs_core.hpp>

#include <string>
#include <vector>

#include <srs_service_st.hpp>

// client open socket and connect to server.
// @param tm The timeout in ms.
extern int srs_socket_connect(std::string server, int port, int64_t tm, st_netfd_t* pstfd);

// whether the url is starts with http:// or https://
extern bool srs_string_is_http(std::string url);
extern bool srs_string_is_rtmp(std::string url);

// get local ip, fill to @param ips
extern std::vector<std::string>& srs_get_local_ipv4_ips();

// get local public ip, empty string if no public internet address found.
extern std::string srs_get_public_internet_address();

// detect whether specified device is internet public address.
extern bool srs_net_device_is_internet(std::string ifname);
extern bool srs_net_device_is_internet(in_addr_t addr);

#endif

