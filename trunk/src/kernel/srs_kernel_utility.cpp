//
// Copyright (c) 2013-2024 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_kernel_utility.hpp>

#ifndef _WIN32
#include <unistd.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/time.h>
#endif

#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdarg.h>

#include <vector>
#include <algorithm>
using namespace std;

#include <srs_core_autofree.hpp>
#include <srs_kernel_log.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_buffer.hpp>
#include <srs_kernel_flv.hpp>
#include <srs_core_deprecated.hpp>

// this value must:
// equals to (SRS_SYS_CYCLE_INTERVAL*SRS_SYS_TIME_RESOLUTION_MS_TIMES)*1000
// @see SRS_SYS_TIME_RESOLUTION_MS_TIMES
#define SYS_TIME_RESOLUTION_US 300*1000

srs_error_t srs_avc_nalu_read_uev(SrsBitBuffer* stream, int32_t& v)
{
    srs_error_t err = srs_success;
    
    if (stream->empty()) {
        return srs_error_new(ERROR_AVC_NALU_UEV, "empty stream");
    }
    
    // ue(v) in 9.1 Parsing process for Exp-Golomb codes
    // ISO_IEC_14496-10-AVC-2012.pdf, page 227.
    // Syntax elements coded as ue(v), me(v), or se(v) are Exp-Golomb-coded.
    //      leadingZeroBits = -1;
    //      for( b = 0; !b; leadingZeroBits++ )
    //          b = read_bits( 1 )
    // The variable codeNum is then assigned as follows:
    //      codeNum = (2<<leadingZeroBits) - 1 + read_bits( leadingZeroBits )
    int leadingZeroBits = -1;
    for (int8_t b = 0; !b && !stream->empty(); leadingZeroBits++) {
        b = stream->read_bit();
    }
    
    if (leadingZeroBits >= 31) {
        return srs_error_new(ERROR_AVC_NALU_UEV, "%dbits overflow 31bits", leadingZeroBits);
    }
    
    v = (1 << leadingZeroBits) - 1;
    for (int i = 0; i < (int)leadingZeroBits; i++) {
        if (stream->empty()) {
            return srs_error_new(ERROR_AVC_NALU_UEV, "no bytes for leadingZeroBits=%d", leadingZeroBits);
        }
        
        int32_t b = stream->read_bit();
        v += b << (leadingZeroBits - 1 - i);
    }
    
    return err;
}

srs_error_t srs_avc_nalu_read_bit(SrsBitBuffer* stream, int8_t& v)
{
    srs_error_t err = srs_success;
    
    if (stream->empty()) {
        return srs_error_new(ERROR_AVC_NALU_UEV, "empty stream");
    }
    
    v = stream->read_bit();
    
    return err;
}

srs_utime_t _srs_system_time_us_cache = 0;
srs_utime_t _srs_system_time_startup_time = 0;

srs_utime_t srs_get_system_time()
{
    if (_srs_system_time_us_cache <= 0) {
        srs_update_system_time();
    }
    
    return _srs_system_time_us_cache;
}

srs_utime_t srs_get_system_startup_time()
{
    if (_srs_system_time_startup_time <= 0) {
        srs_update_system_time();
    }

    return _srs_system_time_startup_time;
}

// For utest to mock it.
#ifndef SRS_OSX
srs_gettimeofday_t _srs_gettimeofday = (srs_gettimeofday_t)::gettimeofday;
#endif

srs_utime_t srs_update_system_time()
{
    timeval now;
    
    if (_srs_gettimeofday(&now, NULL) < 0) {
        srs_warn("gettimeofday failed, ignore");
        return -1;
    }
    
    // we must convert the tv_sec/tv_usec to int64_t.
    int64_t now_us = ((int64_t)now.tv_sec) * 1000 * 1000 + (int64_t)now.tv_usec;
    
    // for some ARM os, the starttime maybe invalid,
    // for example, on the cubieboard2, the srs_startup_time is 1262304014640,
    // while now is 1403842979210 in ms, diff is 141538964570 ms, 1638 days
    // it's impossible, and maybe the problem of startup time is invalid.
    // use date +%s to get system time is 1403844851.
    // so we use relative time.
    if (_srs_system_time_us_cache <= 0) {
        _srs_system_time_startup_time = _srs_system_time_us_cache = now_us;
        return _srs_system_time_us_cache;
    }
    
    // use relative time.
    int64_t diff = now_us - _srs_system_time_us_cache;
    diff = srs_max(0, diff);
    if (diff < 0 || diff > 1000 * SYS_TIME_RESOLUTION_US) {
        srs_warn("clock jump, history=%" PRId64 "us, now=%" PRId64 "us, diff=%" PRId64 "us", _srs_system_time_us_cache, now_us, diff);
        _srs_system_time_startup_time += diff;
    }
    
    _srs_system_time_us_cache = now_us;
    srs_info("clock updated, startup=%" PRId64 "us, now=%" PRId64 "us", _srs_system_time_startup_time, _srs_system_time_us_cache);
    
    return _srs_system_time_us_cache;
}

// TODO: FIXME: Replace by ST dns resolve.
string srs_dns_resolve(string host, int& family)
{
    addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = family;
    
    addrinfo* r = NULL;
    SrsAutoFreeH(addrinfo, r, freeaddrinfo);
    if(getaddrinfo(host.c_str(), NULL, &hints, &r)) {
        return "";
    }
    
    char shost[64];
    memset(shost, 0, sizeof(shost));
    if (getnameinfo(r->ai_addr, r->ai_addrlen, shost, sizeof(shost), NULL, 0, NI_NUMERICHOST)) {
        return "";
    }

   family = r->ai_family;
   return string(shost);
}

void srs_parse_hostport(string hostport, string& host, int& port)
{
    // No host or port.
    if (hostport.empty()) {
        return;
    }

    size_t pos = string::npos;

    // Host only for ipv4.
    if ((pos = hostport.rfind(":")) == string::npos) {
        host = hostport;
        return;
    }

    // For ipv4(only one colon), host:port.
    if (hostport.find(":") == pos) {
        host = hostport.substr(0, pos);
        string p = hostport.substr(pos + 1);
        if (!p.empty() && p != "0") {
            port = ::atoi(p.c_str());
        }
        return;
    }

    // Host only for ipv6.
    if (hostport.at(0) != '[' || (pos = hostport.rfind("]:")) == string::npos) {
        host = hostport;
        return;
    }

    // For ipv6, [host]:port.
    host = hostport.substr(1, pos - 1);
    string p = hostport.substr(pos + 2);
    if (!p.empty() && p != "0") {
        port = ::atoi(p.c_str());
    }
}

string srs_any_address_for_listener()
{
    bool ipv4_active = false;
    bool ipv6_active = false;

    if (true) {
        int fd = socket(AF_INET, SOCK_DGRAM, 0);
        if(fd != -1) {
            ipv4_active = true;
            close(fd);
        }
    }
    if (true) {
        int fd = socket(AF_INET6, SOCK_DGRAM, 0);
        if(fd != -1) {
            ipv6_active = true;
            close(fd);
        }
    }

    if (ipv6_active && !ipv4_active) {
        return SRS_CONSTS_LOOPBACK6;
    }
    return SRS_CONSTS_LOOPBACK;
}

void srs_parse_endpoint(string hostport, string& ip, int& port)
{
    const size_t pos = hostport.rfind(":");   // Look for ":" from the end, to work with IPv6.
    if (pos != std::string::npos) {
        if ((pos >= 1) && (hostport[0] == '[') && (hostport[pos - 1] == ']')) {
            // Handle IPv6 in RFC 2732 format, e.g. [3ffe:dead:beef::1]:1935
            ip = hostport.substr(1, pos - 2);
        } else {
            // Handle IP address
            ip = hostport.substr(0, pos);
        }
        
        const string sport = hostport.substr(pos + 1);
        port = ::atoi(sport.c_str());
    } else {
        ip = srs_any_address_for_listener();
        port = ::atoi(hostport.c_str());
    }
}

bool srs_check_ip_addr_valid(string ip)
{
    unsigned char buf[sizeof(struct in6_addr)];

    // check ipv4
    int ret = inet_pton(AF_INET, ip.data(), buf);
    if (ret > 0) {
        return true;
    }
        
    ret = inet_pton(AF_INET6, ip.data(), buf);
    if (ret > 0) {
        return true;
    }
        
    return false;
}

string srs_int2str(int64_t value)
{
    return srs_fmt("%" PRId64, value);
}

string srs_float2str(double value)
{
    // len(max int64_t) is 20, plus one "+-."
    char tmp[21 + 1];
    snprintf(tmp, sizeof(tmp), "%.2f", value);
    return tmp;
}

string srs_bool2switch(bool v) {
    return v? "on" : "off";
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
        pos += new_str.length();
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
            i = -1;
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
            i = -1;
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
                i = -1;
            } else {
                ++it;
            }
        }
    }
    
    return ret;
}

string srs_erase_first_substr(string str, string erase_string)
{
	std::string ret = str;

	size_t pos = ret.find(erase_string);

	if (pos != std::string::npos) {
		ret.erase(pos, erase_string.length());
	}
    
	return ret;
}

string srs_erase_last_substr(string str, string erase_string)
{
	std::string ret = str;

	size_t pos = ret.rfind(erase_string);

	if (pos != std::string::npos) {
		ret.erase(pos, erase_string.length());
	}
    
	return ret;
}

bool srs_string_ends_with(string str, string flag)
{
    const size_t pos = str.rfind(flag);
    return (pos != string::npos) && (pos == str.length() - flag.length());
}

bool srs_string_ends_with(string str, string flag0, string flag1)
{
    return srs_string_ends_with(str, flag0) || srs_string_ends_with(str, flag1);
}

bool srs_string_ends_with(string str, string flag0, string flag1, string flag2)
{
    return srs_string_ends_with(str, flag0) || srs_string_ends_with(str, flag1) || srs_string_ends_with(str, flag2);
}

bool srs_string_ends_with(string str, string flag0, string flag1, string flag2, string flag3)
{
    return srs_string_ends_with(str, flag0) || srs_string_ends_with(str, flag1) || srs_string_ends_with(str, flag2) || srs_string_ends_with(str, flag3);
}

bool srs_string_starts_with(string str, string flag)
{
    return str.find(flag) == 0;
}

bool srs_string_starts_with(string str, string flag0, string flag1)
{
    return srs_string_starts_with(str, flag0) || srs_string_starts_with(str, flag1);
}

bool srs_string_starts_with(string str, string flag0, string flag1, string flag2)
{
    return srs_string_starts_with(str, flag0, flag1) || srs_string_starts_with(str, flag2);
}

bool srs_string_starts_with(string str, string flag0, string flag1, string flag2, string flag3)
{
    return srs_string_starts_with(str, flag0, flag1, flag2) || srs_string_starts_with(str, flag3);
}

bool srs_string_contains(string str, string flag)
{
    return str.find(flag) != string::npos;
}

bool srs_string_contains(string str, string flag0, string flag1)
{
    return str.find(flag0) != string::npos || str.find(flag1) != string::npos;
}

bool srs_string_contains(string str, string flag0, string flag1, string flag2)
{
    return str.find(flag0) != string::npos || str.find(flag1) != string::npos || str.find(flag2) != string::npos;
}

int srs_string_count(string str, string flag)
{
    int nn = 0;
    for (int i = 0; i < (int)flag.length(); i++) {
        char ch = flag.at(i);
        nn += std::count(str.begin(), str.end(), ch);
    }
    return nn;
}


vector<string> srs_string_split(string s, string seperator)
{
    vector<string> result;
    if(seperator.empty()){
        result.push_back(s);
        return result;
    }
    
    size_t posBegin = 0;
    size_t posSeperator = s.find(seperator);
    while (posSeperator != string::npos) {
        result.push_back(s.substr(posBegin, posSeperator - posBegin));
        posBegin = posSeperator + seperator.length(); // next byte of seperator
        posSeperator = s.find(seperator, posBegin);
    }
    // push the last element
    result.push_back(s.substr(posBegin));
    return result;
}

string srs_string_min_match(string str, vector<string> seperators)
{
    string match;
    
    if (seperators.empty()) {
        return str;
    }
    
    size_t min_pos = string::npos;
    for (vector<string>::iterator it = seperators.begin(); it != seperators.end(); ++it) {
        string seperator = *it;
        
        size_t pos = str.find(seperator);
        if (pos == string::npos) {
            continue;
        }
        
        if (min_pos == string::npos || pos < min_pos) {
            min_pos = pos;
            match = seperator;
        }
    }
    
    return match;
}

vector<string> srs_string_split(string str, vector<string> seperators)
{
    vector<string> arr;
    
    size_t pos = string::npos;
    string s = str;
    
    while (true) {
        string seperator = srs_string_min_match(s, seperators);
        if (seperator.empty()) {
            break;
        }
        
        if ((pos = s.find(seperator)) == string::npos) {
            break;
        }

        arr.push_back(s.substr(0, pos));
        s = s.substr(pos + seperator.length());
    }
    
    if (!s.empty()) {
        arr.push_back(s);
    }
    
    return arr;
}

std::string srs_fmt(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);

    static char buf[8192];
    int r0 = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    string v;
    if (r0 > 0 && r0 < (int)sizeof(buf)) {
        v.append(buf, r0);
    }

    return v;
}

int srs_do_create_dir_recursively(string dir)
{
    int ret = ERROR_SUCCESS;
    
    // stat current dir, if exists, return error.
    if (srs_path_exists(dir)) {
        return ERROR_SYSTEM_DIR_EXISTS;
    }
    
    // create parent first.
    size_t pos;
    if ((pos = dir.rfind("/")) != std::string::npos) {
        std::string parent = dir.substr(0, pos);
        ret = srs_do_create_dir_recursively(parent);
        // return for error.
        if (ret != ERROR_SUCCESS && ret != ERROR_SYSTEM_DIR_EXISTS) {
            return ret;
        }
        // parent exists, set to ok.
        ret = ERROR_SUCCESS;
    }
    
    // create curren dir.
#ifdef _WIN32
    if (::_mkdir(dir.c_str()) < 0) {
#else
    mode_t mode = S_IRUSR|S_IWUSR|S_IXUSR|S_IRGRP|S_IWGRP|S_IXGRP|S_IROTH|S_IXOTH;
    if (::mkdir(dir.c_str(), mode) < 0) {
#endif
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
    
bool srs_bytes_equals(void* pa, void* pb, int size)
{
    uint8_t* a = (uint8_t*)pa;
    uint8_t* b = (uint8_t*)pb;
    
    if (!a && !b) {
        return true;
    }
    
    if (!a || !b) {
        return false;
    }
    
    for(int i = 0; i < size; i++){
        if(a[i] != b[i]){
            return false;
        }
    }
    
    return true;
}

srs_error_t srs_create_dir_recursively(string dir)
{
    int ret = srs_do_create_dir_recursively(dir);
    
    if (ret == ERROR_SYSTEM_DIR_EXISTS || ret == ERROR_SUCCESS) {
        return srs_success;
    }
    
    return srs_error_new(ret, "create dir %s", dir.c_str());
}

bool srs_path_exists(std::string path)
{
    struct stat st;
    
    // stat current dir, if exists, return error.
    if (stat(path.c_str(), &st) == 0) {
        return true;
    }
    
    return false;
}

string srs_path_dirname(string path)
{
    std::string dirname = path;

    // No slash, it must be current dir.
    size_t pos = string::npos;
    if ((pos = dirname.rfind("/")) == string::npos) {
        return "./";
    }

    // Path under root.
    if (pos == 0) {
        return "/";
    }

    // Fetch the directory.
    dirname = dirname.substr(0, pos);
    return dirname;
}

string srs_path_basename(string path)
{
    std::string dirname = path;
    size_t pos = string::npos;
    
    if ((pos = dirname.rfind("/")) != string::npos) {
        // the basename("/") is "/"
        if (dirname.length() == 1) {
            return dirname;
        }
        dirname = dirname.substr(pos + 1);
    }
    
    return dirname;
}

string srs_path_filename(string path)
{
    std::string filename = path;
    size_t pos = string::npos;
    
    if ((pos = filename.rfind(".")) != string::npos) {
        return filename.substr(0, pos);
    }
    
    return filename;
}

string srs_path_filext(string path)
{
    size_t pos = string::npos;
    
    if ((pos = path.rfind(".")) != string::npos) {
        return path.substr(pos);
    }
    
    return "";
}

bool srs_avc_startswith_annexb(SrsBuffer* stream, int* pnb_start_code)
{
    if (!stream) {
        return false;
    }
    
    char* bytes = stream->data() + stream->pos();
    char* p = bytes;
    
    for (;;) {
        if (!stream->require((int)(p - bytes + 3))) {
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

bool srs_aac_startswith_adts(SrsBuffer* stream)
{
    if (!stream) {
        return false;
    }
    
    char* bytes = stream->data() + stream->pos();
    char* p = bytes;
    
    if (!stream->require((int)(p - bytes) + 2)) {
        return false;
    }
    
    // matched 12bits 0xFFF,
    // @remark, we must cast the 0xff to char to compare.
    if (p[0] != (char)0xff || (char)(p[1] & 0xf0) != (char)0xf0) {
        return false;
    }
    
    return true;
}
    
// @see pycrc reflect at https://github.com/winlinvip/pycrc/blob/master/pycrc/algorithms.py#L107
uint64_t __crc32_reflect(uint64_t data, int width)
{
    uint64_t res = data & 0x01;
    
    for (int i = 0; i < (int)width - 1; i++) {
        data >>= 1;
        res = (res << 1) | (data & 0x01);
    }
    
    return res;
}
    
// @see pycrc gen_table at https://github.com/winlinvip/pycrc/blob/master/pycrc/algorithms.py#L178
void __crc32_make_table(uint32_t t[256], uint32_t poly, bool reflect_in)
{
    int width = 32; // 32bits checksum.
    uint64_t msb_mask = (uint32_t)(0x01 << (width - 1));
    uint64_t mask = (uint32_t)(((msb_mask - 1) << 1) | 1);
    
    int tbl_idx_width = 8; // table index size.
    int tbl_width = 0x01 << tbl_idx_width; // table size: 256
    
    for (int i = 0; i < (int)tbl_width; i++) {
        uint64_t reg = uint64_t(i);
        
        if (reflect_in) {
            reg = __crc32_reflect(reg, tbl_idx_width);
        }
        
        reg = reg << (width - tbl_idx_width);
        for (int j = 0; j < tbl_idx_width; j++) {
            if ((reg&msb_mask) != 0) {
                reg = (reg << 1) ^ poly;
            } else {
                reg = reg << 1;
            }
        }
        
        if (reflect_in) {
            reg = __crc32_reflect(reg, width);
        }
        
        t[i] = (uint32_t)(reg & mask);
    }
}
 
// @see pycrc table_driven at https://github.com/winlinvip/pycrc/blob/master/pycrc/algorithms.py#L207
uint32_t __crc32_table_driven(uint32_t* t, const void* buf, int size, uint32_t previous, bool reflect_in, uint32_t xor_in, bool reflect_out, uint32_t xor_out)
{
    int width = 32; // 32bits checksum.
    uint64_t msb_mask = (uint32_t)(0x01 << (width - 1));
    uint64_t mask = (uint32_t)(((msb_mask - 1) << 1) | 1);
    
    int tbl_idx_width = 8; // table index size.
    
    uint8_t* p = (uint8_t*)buf;
    uint64_t reg = 0;
    
    if (!reflect_in) {
        reg = xor_in;
        
        for (int i = 0; i < (int)size; i++) {
            uint8_t tblidx = (uint8_t)((reg >> (width - tbl_idx_width)) ^ p[i]);
            reg = t[tblidx] ^ (reg << tbl_idx_width);
        }
    } else {
        reg = previous ^ __crc32_reflect(xor_in, width);
        
        for (int i = 0; i < (int)size; i++) {
            uint8_t tblidx = (uint8_t)(reg ^ p[i]);
            reg = t[tblidx] ^ (reg >> tbl_idx_width);
        }
        
        reg = __crc32_reflect(reg, width);
    }
    
    if (reflect_out) {
        reg = __crc32_reflect(reg, width);
    }
    
    reg ^= xor_out;
    return (uint32_t)(reg & mask);
}
    
// @see pycrc https://github.com/winlinvip/pycrc/blob/master/pycrc/algorithms.py#L207
// IEEETable is the table for the IEEE polynomial.
static uint32_t __crc32_IEEE_table[256];
static bool __crc32_IEEE_table_initialized = false;

// @see pycrc https://github.com/winlinvip/pycrc/blob/master/pycrc/models.py#L220
//      crc32('123456789') = 0xcbf43926
// where it's defined as model:
//      'name':         'crc-32',
//      'width':         32,
//      'poly':          0x4c11db7,
//      'reflect_in':    True,
//      'xor_in':        0xffffffff,
//      'reflect_out':   True,
//      'xor_out':       0xffffffff,
//      'check':         0xcbf43926,
uint32_t srs_crc32_ieee(const void* buf, int size, uint32_t previous)
{
    // @see golang IEEE of hash/crc32/crc32.go
    // IEEE is by far and away the most common CRC-32 polynomial.
    // Used by ethernet (IEEE 802.3), v.42, fddi, gzip, zip, png, ...
    // @remark The poly of CRC32 IEEE is 0x04C11DB7, its reverse is 0xEDB88320,
    //      please read https://en.wikipedia.org/wiki/Cyclic_redundancy_check
    uint32_t poly = 0x04C11DB7;
    
    bool reflect_in = true;
    uint32_t xor_in = 0xffffffff;
    bool reflect_out = true;
    uint32_t xor_out = 0xffffffff;
    
    if (!__crc32_IEEE_table_initialized) {
        __crc32_make_table(__crc32_IEEE_table, poly, reflect_in);
        __crc32_IEEE_table_initialized = true;
    }
    
    return __crc32_table_driven(__crc32_IEEE_table, buf, size, previous, reflect_in, xor_in, reflect_out, xor_out);
}
    
// @see pycrc https://github.com/winlinvip/pycrc/blob/master/pycrc/algorithms.py#L238
// IEEETable is the table for the MPEG polynomial.
static uint32_t __crc32_MPEG_table[256];
static bool __crc32_MPEG_table_initialized = false;

// @see pycrc https://github.com/winlinvip/pycrc/blob/master/pycrc/models.py#L238
//      crc32('123456789') = 0x0376e6e7
// where it's defined as model:
//      'name':         'crc-32',
//      'width':         32,
//      'poly':          0x4c11db7,
//      'reflect_in':    False,
//      'xor_in':        0xffffffff,
//      'reflect_out':   False,
//      'xor_out':       0x0,
//      'check':         0x0376e6e7,
uint32_t srs_crc32_mpegts(const void* buf, int size)
{
    // @see golang IEEE of hash/crc32/crc32.go
    // IEEE is by far and away the most common CRC-32 polynomial.
    // Used by ethernet (IEEE 802.3), v.42, fddi, gzip, zip, png, ...
    // @remark The poly of CRC32 IEEE is 0x04C11DB7, its reverse is 0xEDB88320,
    //      please read https://en.wikipedia.org/wiki/Cyclic_redundancy_check
    uint32_t poly = 0x04C11DB7;
    
    bool reflect_in = false;
    uint32_t xor_in = 0xffffffff;
    bool reflect_out = false;
    uint32_t xor_out = 0x0;
    
    if (!__crc32_MPEG_table_initialized) {
        __crc32_make_table(__crc32_MPEG_table, poly, reflect_in);
        __crc32_MPEG_table_initialized = true;
    }
    
    return __crc32_table_driven(__crc32_MPEG_table, buf, size, 0x00, reflect_in, xor_in, reflect_out, xor_out);
}

// We use the standard encoding:
//      var StdEncoding = NewEncoding(encodeStd)
// StdEncoding is the standard base64 encoding, as defined in RFC 4648.
namespace {
    char padding = '=';
    string encoder = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
}
// @see golang encoding/base64/base64.go
srs_error_t srs_av_base64_decode(string cipher, string& plaintext)
{
    srs_error_t err = srs_success;
    
    uint8_t decodeMap[256];
    memset(decodeMap, 0xff, sizeof(decodeMap));
    
    for (int i = 0; i < (int)encoder.length(); i++) {
        decodeMap[(uint8_t)encoder.at(i)] = uint8_t(i);
    }
    
    // decode is like Decode but returns an additional 'end' value, which
    // indicates if end-of-message padding or a partial quantum was encountered
    // and thus any additional data is an error.
    int si = 0;
    
    // skip over newlines
    for (; si < (int)cipher.length() && (cipher.at(si) == '\n' || cipher.at(si) == '\r'); si++) {
    }
    
    for (bool end = false; si < (int)cipher.length() && !end;) {
        // Decode quantum using the base64 alphabet
        uint8_t dbuf[4];
        memset(dbuf, 0x00, sizeof(dbuf));
        
        int dinc = 3;
        int dlen = 4;
        srs_assert(dinc > 0);
        
        for (int j = 0; j < (int)sizeof(dbuf); j++) {
            if (si == (int)cipher.length()) {
                if (padding != -1 || j < 2) {
                    return srs_error_new(ERROR_BASE64_DECODE, "corrupt input at %d", si);
                }
                
                dinc = j - 1;
                dlen = j;
                end = true;
                break;
            }
            
            char in = cipher.at(si);
            
            si++;
            // skip over newlines
            for (; si < (int)cipher.length() && (cipher.at(si) == '\n' || cipher.at(si) == '\r'); si++) {
            }
            
            if (in == padding) {
                // We've reached the end and there's padding
                switch (j) {
                    case 0:
                    case 1:
                        // incorrect padding
                        return srs_error_new(ERROR_BASE64_DECODE, "corrupt input at %d", si);
                    case 2:
                        // "==" is expected, the first "=" is already consumed.
                        if (si == (int)cipher.length()) {
                            return srs_error_new(ERROR_BASE64_DECODE, "corrupt input at %d", si);
                        }
                        if (cipher.at(si) != padding) {
                            // incorrect padding
                            return srs_error_new(ERROR_BASE64_DECODE, "corrupt input at %d", si);
                        }
                        
                        si++;
                        // skip over newlines
                        for (; si < (int)cipher.length() && (cipher.at(si) == '\n' || cipher.at(si) == '\r'); si++) {
                        }
                }
                
                if (si < (int)cipher.length()) {
                    // trailing garbage
                    err = srs_error_new(ERROR_BASE64_DECODE, "corrupt input at %d", si);
                }
                dinc = 3;
                dlen = j;
                end = true;
                break;
            }
            
            dbuf[j] = decodeMap[(uint8_t)in];
            if (dbuf[j] == 0xff) {
                return srs_error_new(ERROR_BASE64_DECODE, "corrupt input at %d", si);
            }
        }
        
        // Convert 4x 6bit source bytes into 3 bytes
        uint32_t val = uint32_t(dbuf[0])<<18 | uint32_t(dbuf[1])<<12 | uint32_t(dbuf[2])<<6 | uint32_t(dbuf[3]);
        if (dlen >= 2) {
            plaintext.append(1, char(val >> 16));
        }
        if (dlen >= 3) {
            plaintext.append(1, char(val >> 8));
        }
        if (dlen >= 4) {
            plaintext.append(1, char(val));
        }
    }
    
    return err;
}

// @see golang encoding/base64/base64.go
srs_error_t srs_av_base64_encode(std::string plaintext, std::string& cipher)
{
    srs_error_t err = srs_success;
    uint8_t decodeMap[256];
    memset(decodeMap, 0xff, sizeof(decodeMap));
    
    for (int i = 0; i < (int)encoder.length(); i++) {
        decodeMap[(uint8_t)encoder.at(i)] = uint8_t(i);
    }
    cipher.clear();

    uint32_t val = 0;
    int si = 0;
    int n = (plaintext.length() / 3) * 3;
    uint8_t* p =  (uint8_t*)plaintext.c_str();
    while(si < n) {
        // Convert 3x 8bit source bytes into 4 bytes
        val = (uint32_t(p[si + 0]) << 16) | (uint32_t(p[si + 1])<< 8) | uint32_t(p[si + 2]);

        cipher += encoder[val>>18&0x3f];
        cipher += encoder[val>>12&0x3f];
        cipher += encoder[val>>6&0x3f];
        cipher += encoder[val&0x3f];

        si += 3;
    }

    int remain = plaintext.length() - si;
    if(0 == remain) {
        return err;
    }

    val = uint32_t(p[si + 0]) << 16;
    if( 2 == remain) {
        val |= uint32_t(p[si + 1]) << 8;
    }

    cipher += encoder[val>>18&0x3f];
    cipher += encoder[val>>12&0x3f];

    switch (remain) {
    case 2:
        cipher += encoder[val>>6&0x3f];
        cipher += padding;
        break;
    case 1:
        cipher += padding;
        cipher += padding;
        break;
    }


    return err;
}

#define SPACE_CHARS " \t\r\n"

int av_toupper(int c)
{
    if (c >= 'a' && c <= 'z') {
        c ^= 0x20;
    }
    return c;
}
    
// fromHexChar converts a hex character into its value and a success flag.
uint8_t srs_from_hex_char(uint8_t c)
{
    if ('0' <= c && c <= '9') {
        return c - '0';
    }
    if ('a' <= c && c <= 'f') {
        return c - 'a' + 10;
    }
    if ('A' <= c && c <= 'F') {
        return c - 'A' + 10;
    }

    return -1;
}

char* srs_data_to_hex(char* des, const u_int8_t* src, int len)
{
    if(src == NULL || len == 0 || des == NULL){
        return NULL;
    }

    const char *hex_table = "0123456789ABCDEF";
    
    for (int i=0; i<len; i++) {
        des[i * 2]     = hex_table[src[i] >> 4];
        des[i * 2 + 1] = hex_table[src[i] & 0x0F];
    }  

    return des;
}

char* srs_data_to_hex_lowercase(char* des, const u_int8_t* src, int len)
{
    if(src == NULL || len == 0 || des == NULL){
        return NULL;
    }

    const char *hex_table = "0123456789abcdef";

    for (int i=0; i<len; i++) {
        des[i * 2]     = hex_table[src[i] >> 4];
        des[i * 2 + 1] = hex_table[src[i] & 0x0F];
    }

    return des;
}

int srs_hex_to_data(uint8_t* data, const char* p, int size)
{
    if (size <= 0 || (size%2) == 1) {
        return -1;
    }
    
    for (int i = 0; i < (int)size / 2; i++) {
        uint8_t a = srs_from_hex_char(p[i*2]);
        if (a == (uint8_t)-1) {
            return -1;
        }
        
        uint8_t b = srs_from_hex_char(p[i*2 + 1]);
        if (b == (uint8_t)-1) {
            return -1;
        }
        
        data[i] = (a << 4) | b;
    }
    
    return size / 2;
}

int srs_chunk_header_c0(int prefer_cid, uint32_t timestamp, int32_t payload_length, int8_t message_type, int32_t stream_id, char* cache, int nb_cache)
{
    // to directly set the field.
    char* pp = NULL;
    
    // generate the header.
    char* p = cache;
    
    // no header.
    if (nb_cache < SRS_CONSTS_RTMP_MAX_FMT0_HEADER_SIZE) {
        return 0;
    }
    
    // write new chunk stream header, fmt is 0
    *p++ = 0x00 | (prefer_cid & 0x3F);
    
    // chunk message header, 11 bytes
    // timestamp, 3bytes, big-endian
    if (timestamp < RTMP_EXTENDED_TIMESTAMP) {
        pp = (char*)&timestamp;
        *p++ = pp[2];
        *p++ = pp[1];
        *p++ = pp[0];
    } else {
        *p++ = (char)0xFF;
        *p++ = (char)0xFF;
        *p++ = (char)0xFF;
    }
    
    // message_length, 3bytes, big-endian
    pp = (char*)&payload_length;
    *p++ = pp[2];
    *p++ = pp[1];
    *p++ = pp[0];
    
    // message_type, 1bytes
    *p++ = message_type;
    
    // stream_id, 4bytes, little-endian
    pp = (char*)&stream_id;
    *p++ = pp[0];
    *p++ = pp[1];
    *p++ = pp[2];
    *p++ = pp[3];
    
    // for c0
    // chunk extended timestamp header, 0 or 4 bytes, big-endian
    //
    // for c3:
    // chunk extended timestamp header, 0 or 4 bytes, big-endian
    // 6.1.3. Extended Timestamp
    // This field is transmitted only when the normal time stamp in the
    // chunk message header is set to 0x00ffffff. If normal time stamp is
    // set to any value less than 0x00ffffff, this field MUST NOT be
    // present. This field MUST NOT be present if the timestamp field is not
    // present. Type 3 chunks MUST NOT have this field.
    // adobe changed for Type3 chunk:
    //        FMLE always sendout the extended-timestamp,
    //        must send the extended-timestamp to FMS,
    //        must send the extended-timestamp to flash-player.
    // @see: ngx_rtmp_prepare_message
    // @see: http://blog.csdn.net/win_lin/article/details/13363699
    // TODO: FIXME: extract to outer.
    if (timestamp >= RTMP_EXTENDED_TIMESTAMP) {
        pp = (char*)&timestamp;
        *p++ = pp[3];
        *p++ = pp[2];
        *p++ = pp[1];
        *p++ = pp[0];
    }
    
    // always has header
    return (int)(p - cache);
}

int srs_chunk_header_c3(int prefer_cid, uint32_t timestamp, char* cache, int nb_cache)
{
    // to directly set the field.
    char* pp = NULL;
    
    // generate the header.
    char* p = cache;
    
    // no header.
    if (nb_cache < SRS_CONSTS_RTMP_MAX_FMT3_HEADER_SIZE) {
        return 0;
    }
    
    // write no message header chunk stream, fmt is 3
    // @remark, if prefer_cid > 0x3F, that is, use 2B/3B chunk header,
    // SRS will rollback to 1B chunk header.
    *p++ = 0xC0 | (prefer_cid & 0x3F);
    
    // for c0
    // chunk extended timestamp header, 0 or 4 bytes, big-endian
    //
    // for c3:
    // chunk extended timestamp header, 0 or 4 bytes, big-endian
    // 6.1.3. Extended Timestamp
    // This field is transmitted only when the normal time stamp in the
    // chunk message header is set to 0x00ffffff. If normal time stamp is
    // set to any value less than 0x00ffffff, this field MUST NOT be
    // present. This field MUST NOT be present if the timestamp field is not
    // present. Type 3 chunks MUST NOT have this field.
    // adobe changed for Type3 chunk:
    //        FMLE always sendout the extended-timestamp,
    //        must send the extended-timestamp to FMS,
    //        must send the extended-timestamp to flash-player.
    // @see: ngx_rtmp_prepare_message
    // @see: http://blog.csdn.net/win_lin/article/details/13363699
    // TODO: FIXME: extract to outer.
    if (timestamp >= RTMP_EXTENDED_TIMESTAMP) {
        pp = (char*)&timestamp;
        *p++ = pp[3];
        *p++ = pp[2];
        *p++ = pp[1];
        *p++ = pp[0];
    }
    
    // always has header
    return (int)(p - cache);
}

