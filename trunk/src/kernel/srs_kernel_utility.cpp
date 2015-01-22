/*
The MIT License (MIT)

Copyright (c) 2013-2015 winlin

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

// for srs-librtmp, @see https://github.com/winlinvip/simple-rtmp-server/issues/213
#ifndef _WIN32
#include <unistd.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/time.h>
#endif

#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>

using namespace std;

#include <srs_kernel_log.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_stream.hpp>

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
int64_t srs_update_system_time_ms()
{
    timeval now;
    
    if (gettimeofday(&now, NULL) < 0) {
        srs_warn("gettimeofday failed, ignore");
        return -1;
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
        return _srs_system_time_us_cache;
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
    
    return _srs_system_time_us_cache;
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

int __srs_create_dir_recursively(string dir)
{
    int ret = ERROR_SUCCESS;
    
    struct stat st;
    
    // stat current dir, if exists, return error.
    if (stat(dir.c_str(), &st) == 0) {
        return ERROR_SYSTEM_DIR_EXISTS;
    }
    
    // create parent first.
    size_t pos;
    if ((pos = dir.rfind("/")) != std::string::npos) {
        std::string parent = dir.substr(0, pos);
        ret = __srs_create_dir_recursively(parent);
        // return for error.
        if (ret != ERROR_SUCCESS && ret != ERROR_SYSTEM_DIR_EXISTS) {
            return ret;
        }
        // parent exists, set to ok.
        ret = ERROR_SUCCESS;
    }
    
    // create curren dir.
    mode_t mode = S_IRUSR|S_IWUSR|S_IXUSR|S_IRGRP|S_IWGRP|S_IXGRP|S_IROTH|S_IXOTH;
    if (::mkdir(dir.c_str(), mode) < 0) {
        if (errno == EEXIST) {
            return ERROR_SYSTEM_DIR_EXISTS;
        }
        
        ret = ERROR_SYSTEM_CREATE_DIR;
        srs_error("create dir %s failed. ret=%d", dir.c_str(), ret);
        return ret;
    }
    srs_info("create dir %s success.", dir.c_str());
    
    return ret;
}

int srs_create_dir_recursively(string dir)
{
    int ret = ERROR_SUCCESS;
    
    ret = __srs_create_dir_recursively(dir);
    
    if (ret == ERROR_SYSTEM_DIR_EXISTS) {
        return ERROR_SUCCESS;
    }
    
    return ret;
}

bool srs_avc_startswith_annexb(SrsStream* stream, int* pnb_start_code)
{
    char* bytes = stream->data() + stream->pos();
    char* p = bytes;
    
    for (;;) {
        if (!stream->require(p - bytes + 3)) {
            return false;
        }
        
        // not match
        if (p[0] != (char)0x00 || p[1] != (char)0x00) {
            return false;
        }
        
        // match N[00] 00 00 01, where N>=0
        if (p[2] == (char)0x01) {
            if (pnb_start_code) {
                *pnb_start_code = (int)(p - bytes) + 3;
            }
            return true;
        }
        
        p++;
    }
    
    return false;
}

bool srs_aac_startswith_adts(SrsStream* stream)
{
    char* bytes = stream->data() + stream->pos();
    char* p = bytes;
    
    if (!stream->require(p - bytes + 2)) {
        return false;
    }
    
    // matched 12bits 0xFFF,
    // @remark, we must cast the 0xff to char to compare.
    if (p[0] != (char)0xff || (char)(p[1] & 0xf0) != (char)0xf0) {
        return false;
    }
    
    return true;
}

