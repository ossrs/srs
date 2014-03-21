/*
The MIT License (MIT)

Copyright (c) 2013-2014 winlin

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include <srs_core.hpp>

#include <string.h>
#include <netdb.h>
#include <arpa/inet.h>

std::string srs_replace(std::string str, std::string old_str, std::string new_str)
{
    std::string ret = str;
    
    if (old_str == new_str) {
        return ret;
    }
    
    size_t pos = 0;
    while ((pos = ret.find(old_str, pos)) != std::string::npos) {
        ret = ret.replace(pos, old_str.length(), new_str);
        pos += new_str.length();
    }
    
    return ret;
}

std::string srs_dns_resolve(std::string host)
{
    if (inet_addr(host.c_str()) != INADDR_NONE) {
        return host;
    }
    
    hostent* answer = gethostbyname(host.c_str());
    if (answer == NULL) {
        return "";
    }
    
    char ipv4[16];
    memset(ipv4, 0, sizeof(ipv4));
    for (int i = 0; i < answer->h_length; i++) {
        inet_ntop(AF_INET, answer->h_addr_list[i], ipv4, sizeof(ipv4));
        break;
    }
    
    return ipv4;
}

bool srs_is_little_endian()
{
    // convert to network(big-endian) order, if not equals, 
    // the system is little-endian, so need to convert the int64
    static int little_endian_check = -1;
    
    if(little_endian_check == -1) {
        union {
            int32_t i;
            int8_t c;
        } little_check_union;
        
        little_check_union.i = 0x01;
        little_endian_check = little_check_union.c;
    }
    
    return (little_endian_check == 1);
}

