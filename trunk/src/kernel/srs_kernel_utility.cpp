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

#include <srs_kernel_utility.hpp>

#include <unistd.h>
#include <string.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/time.h>

using namespace std;

#include <srs_kernel_log.hpp>

// this value must:
// equals to (SRS_SYS_CYCLE_INTERVAL*SRS_SYS_TIME_RESOLUTION_MS_TIMES)*1000
// @see SRS_SYS_TIME_RESOLUTION_MS_TIMES
#define SYS_TIME_RESOLUTION_US 300*1000

static int64_t _srs_system_time_us_cache = 0;
static int64_t _srs_system_time_startup_time = 0;

int64_t srs_get_system_time_ms()
{
    if (_srs_system_time_us_cache <= 0) {
        srs_update_system_time_ms();
    }
    
    return _srs_system_time_us_cache / 1000;
}
int64_t srs_get_system_startup_time_ms()
{
    if (_srs_system_time_startup_time <= 0) {
        srs_update_system_time_ms();
    }
    
    return _srs_system_time_startup_time / 1000;
}
void srs_update_system_time_ms()
{
    timeval now;
    
    if (gettimeofday(&now, NULL) < 0) {
        srs_warn("gettimeofday failed, ignore");
        return;
    }

    // @see: https://github.com/winlinvip/simple-rtmp-server/issues/35
    // we must convert the tv_sec/tv_usec to int64_t.
    int64_t now_us = ((int64_t)now.tv_sec) * 1000 * 1000 + (int64_t)now.tv_usec;
    
    // for some ARM os, the starttime maybe invalid,
    // for example, on the cubieboard2, the srs_startup_time is 1262304014640,
    // while now is 1403842979210 in ms, diff is 141538964570 ms, 1638 days
    // it's impossible, and maybe the problem of startup time is invalid.
    // use date +%s to get system time is 1403844851.
    // so we use relative time.
    if (_srs_system_time_us_cache <= 0) {
        _srs_system_time_us_cache = now_us;
        _srs_system_time_startup_time = now_us;
        return;
    }
    
    // use relative time.
    int64_t diff = now_us - _srs_system_time_us_cache;
    diff = srs_max(0, diff);
    if (diff < 0 || diff > 1000 * SYS_TIME_RESOLUTION_US) {
        srs_warn("system time jump, history=%"PRId64"us, now=%"PRId64"us, diff=%"PRId64"us", 
            _srs_system_time_us_cache, now_us, diff);
        // @see: https://github.com/winlinvip/simple-rtmp-server/issues/109
        _srs_system_time_startup_time += diff;
    }
    
    _srs_system_time_us_cache = now_us;
    srs_info("system time updated, startup=%"PRId64"us, now=%"PRId64"us", 
        _srs_system_time_startup_time, _srs_system_time_us_cache);
}

string srs_dns_resolve(string host)
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

string srs_string_replace(string str, string old_str, string new_str)
{
    std::string ret = str;
    
    if (old_str == new_str) {
        return ret;
    }
    
    size_t pos = 0;
    while ((pos = ret.find(old_str, pos)) != std::string::npos) {
        ret = ret.replace(pos, old_str.length(), new_str);
    }
    
    return ret;
}

string srs_string_trim_end(string str, string trim_chars)
{
    std::string ret = str;
    
    for (int i = 0; i < (int)trim_chars.length(); i++) {
        char ch = trim_chars.at(i);
        
        while (!ret.empty() && ret.at(ret.length() - 1) == ch) {
            ret.erase(ret.end() - 1);
            
            // ok, matched, should reset the search
            i = 0;
        }
    }
    
    return ret;
}

string srs_string_trim_start(string str, string trim_chars)
{
    std::string ret = str;
    
    for (int i = 0; i < (int)trim_chars.length(); i++) {
        char ch = trim_chars.at(i);
        
        while (!ret.empty() && ret.at(0) == ch) {
            ret.erase(ret.begin());
            
            // ok, matched, should reset the search
            i = 0;
        }
    }
    
    return ret;
}

string srs_string_remove(string str, string remove_chars)
{
    std::string ret = str;
    
    for (int i = 0; i < (int)remove_chars.length(); i++) {
        char ch = remove_chars.at(i);
        
        for (std::string::iterator it = ret.begin(); it != ret.end();) {
            if (ch == *it) {
                it = ret.erase(it);
                
                // ok, matched, should reset the search
                i = 0;
            } else {
                ++it;
            }
        }
    }
    
    return ret;
}

bool srs_string_ends_with(string str, string flag)
{
    return str.rfind(flag) == str.length() - flag.length();
}
