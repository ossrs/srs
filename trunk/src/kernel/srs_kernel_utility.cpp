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

#include <srs_kernel_utility.hpp>

// for srs-librtmp, @see https://github.com/ossrs/srs/issues/213
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

#include <vector>
using namespace std;

#include <srs_kernel_log.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_buffer.hpp>
#include <srs_kernel_flv.hpp>

// this value must:
// equals to (SRS_SYS_CYCLE_INTERVAL*SRS_SYS_TIME_RESOLUTION_MS_TIMES)*1000
// @see SRS_SYS_TIME_RESOLUTION_MS_TIMES
#define SYS_TIME_RESOLUTION_US 300*1000

int srs_avc_nalu_read_uev(SrsBitBuffer* stream, int32_t& v)
{
    int ret = ERROR_SUCCESS;
    
    if (stream->empty()) {
        return ERROR_AVC_NALU_UEV;
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
        return ERROR_AVC_NALU_UEV;
    }
    
    v = (1 << leadingZeroBits) - 1;
    for (int i = 0; i < leadingZeroBits; i++) {
        int32_t b = stream->read_bit();
        v += b << (leadingZeroBits - 1 - i);
    }
    
    return ret;
}

int srs_avc_nalu_read_bit(SrsBitBuffer* stream, int8_t& v)
{
    int ret = ERROR_SUCCESS;
    
    if (stream->empty()) {
        return ERROR_AVC_NALU_UEV;
    }
    
    v = stream->read_bit();
    
    return ret;
}

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
    
    // @see: https://github.com/ossrs/srs/issues/35
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
        return _srs_system_time_us_cache / 1000;
    }
    
    // use relative time.
    int64_t diff = now_us - _srs_system_time_us_cache;
    diff = srs_max(0, diff);
    if (diff < 0 || diff > 1000 * SYS_TIME_RESOLUTION_US) {
        srs_warn("clock jump, history=%" PRId64 "us, now=%" PRId64 "us, diff=%" PRId64 "us", _srs_system_time_us_cache, now_us, diff);
        // @see: https://github.com/ossrs/srs/issues/109
        _srs_system_time_startup_time += diff;
    }
    
    _srs_system_time_us_cache = now_us;
    srs_info("clock updated, startup=%" PRId64 "us, now=%" PRId64 "us", _srs_system_time_startup_time, _srs_system_time_us_cache);
    
    return _srs_system_time_us_cache / 1000;
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
    
    // covert the first entry to ip.
    if (answer->h_length > 0) {
        inet_ntop(AF_INET, answer->h_addr_list[0], ipv4, sizeof(ipv4));
    }
    
    return ipv4;
}

void srs_parse_hostport(const string& hostport, string& host, int& port)
{
    size_t pos = hostport.find(":");
    if (pos != std::string::npos) {
        string p = hostport.substr(pos + 1);
        host = hostport.substr(0, pos);
        port = ::atoi(p.c_str());
    } else {
        host = hostport;
    }
}

void srs_parse_endpoint(string hostport, string& ip, int& port)
{
    ip = "0.0.0.0";
    
    size_t pos = string::npos;
    if ((pos = hostport.find(":")) != string::npos) {
        ip = hostport.substr(0, pos);
        string sport = hostport.substr(pos + 1);
        port = ::atoi(sport.c_str());
    } else {
        port = ::atoi(hostport.c_str());
    }
}

string srs_int2str(int64_t value)
{
    // len(max int64_t) is 20, plus one "+-."
    char tmp[22];
    snprintf(tmp, 22, "%" PRId64, value);
    return tmp;
}

string srs_float2str(double value)
{
    // len(max int64_t) is 20, plus one "+-."
    char tmp[22];
    snprintf(tmp, 22, "%.2f", value);
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
    ssize_t pos = str.rfind(flag);
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

vector<string> srs_string_split(string str, string flag)
{
    vector<string> arr;
    
    size_t pos;
    string s = str;
    
    while ((pos = s.find(flag)) != string::npos) {
        if (pos != 0) {
            arr.push_back(s.substr(0, pos));
        }
        s = s.substr(pos + flag.length());
    }
    
    if (!s.empty()) {
        arr.push_back(s);
    }
    
    return arr;
}

string srs_string_min_match(string str, vector<string> flags)
{
    string match;
    
    size_t min_pos = string::npos;
    for (vector<string>::iterator it = flags.begin(); it != flags.end(); ++it) {
        string flag = *it;
        
        size_t pos = str.find(flag);
        if (pos == string::npos) {
            continue;
        }
        
        if (min_pos == string::npos || pos < min_pos) {
            min_pos = pos;
            match = flag;
        }
    }
    
    return match;
}

vector<string> srs_string_split(string str, vector<string> flags)
{
    vector<string> arr;
    
    size_t pos = string::npos;
    string s = str;
    
    while (true) {
        string flag = srs_string_min_match(s, flags);
        if (flag.empty()) {
            break;
        }
        
        if ((pos = s.find(flag)) == string::npos) {
            break;
        }
        
        if (pos != 0) {
            arr.push_back(s.substr(0, pos));
        }
        s = s.substr(pos + flag.length());
    }
    
    if (!s.empty()) {
        arr.push_back(s);
    }
    
    return arr;
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
    // for srs-librtmp, @see https://github.com/ossrs/srs/issues/213
#ifndef _WIN32
    mode_t mode = S_IRUSR|S_IWUSR|S_IXUSR|S_IRGRP|S_IWGRP|S_IXGRP|S_IROTH|S_IXOTH;
    if (::mkdir(dir.c_str(), mode) < 0) {
#else
        if (::mkdir(dir.c_str()) < 0) {
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
    
    int srs_create_dir_recursively(string dir)
    {
        int ret = ERROR_SUCCESS;
        
        ret = srs_do_create_dir_recursively(dir);
        
        if (ret == ERROR_SYSTEM_DIR_EXISTS) {
            return ERROR_SUCCESS;
        }
        
        return ret;
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
        size_t pos = string::npos;
        
        if ((pos = dirname.rfind("/")) != string::npos) {
            if (pos == 0) {
                return "/";
            }
            dirname = dirname.substr(0, pos);
        }
        
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
    
    // @see http://www.stmc.edu.hk/~vincent/ffmpeg_0.4.9-pre1/libavformat/mpegtsenc.c
    unsigned int __mpegts_crc32(const uint8_t *data, int len)
    {
        /*
         * MPEG2 transport stream (aka DVB) mux
         * Copyright (c) 2003 Fabrice Bellard.
         *
         * This library is free software; you can redistribute it and/or
         * modify it under the terms of the GNU Lesser General Public
         * License as published by the Free Software Foundation; either
         * version 2 of the License, or (at your option) any later version.
         *
         * This library is distributed in the hope that it will be useful,
         * but WITHOUT ANY WARRANTY; without even the implied warranty of
         * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
         * Lesser General Public License for more details.
         *
         * You should have received a copy of the GNU Lesser General Public
         * License along with this library; if not, write to the Free Software
         * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
         */
        static const uint32_t table[256] = {
            0x00000000, 0x04c11db7, 0x09823b6e, 0x0d4326d9, 0x130476dc, 0x17c56b6b,
            0x1a864db2, 0x1e475005, 0x2608edb8, 0x22c9f00f, 0x2f8ad6d6, 0x2b4bcb61,
            0x350c9b64, 0x31cd86d3, 0x3c8ea00a, 0x384fbdbd, 0x4c11db70, 0x48d0c6c7,
            0x4593e01e, 0x4152fda9, 0x5f15adac, 0x5bd4b01b, 0x569796c2, 0x52568b75,
            0x6a1936c8, 0x6ed82b7f, 0x639b0da6, 0x675a1011, 0x791d4014, 0x7ddc5da3,
            0x709f7b7a, 0x745e66cd, 0x9823b6e0, 0x9ce2ab57, 0x91a18d8e, 0x95609039,
            0x8b27c03c, 0x8fe6dd8b, 0x82a5fb52, 0x8664e6e5, 0xbe2b5b58, 0xbaea46ef,
            0xb7a96036, 0xb3687d81, 0xad2f2d84, 0xa9ee3033, 0xa4ad16ea, 0xa06c0b5d,
            0xd4326d90, 0xd0f37027, 0xddb056fe, 0xd9714b49, 0xc7361b4c, 0xc3f706fb,
            0xceb42022, 0xca753d95, 0xf23a8028, 0xf6fb9d9f, 0xfbb8bb46, 0xff79a6f1,
            0xe13ef6f4, 0xe5ffeb43, 0xe8bccd9a, 0xec7dd02d, 0x34867077, 0x30476dc0,
            0x3d044b19, 0x39c556ae, 0x278206ab, 0x23431b1c, 0x2e003dc5, 0x2ac12072,
            0x128e9dcf, 0x164f8078, 0x1b0ca6a1, 0x1fcdbb16, 0x018aeb13, 0x054bf6a4,
            0x0808d07d, 0x0cc9cdca, 0x7897ab07, 0x7c56b6b0, 0x71159069, 0x75d48dde,
            0x6b93dddb, 0x6f52c06c, 0x6211e6b5, 0x66d0fb02, 0x5e9f46bf, 0x5a5e5b08,
            0x571d7dd1, 0x53dc6066, 0x4d9b3063, 0x495a2dd4, 0x44190b0d, 0x40d816ba,
            0xaca5c697, 0xa864db20, 0xa527fdf9, 0xa1e6e04e, 0xbfa1b04b, 0xbb60adfc,
            0xb6238b25, 0xb2e29692, 0x8aad2b2f, 0x8e6c3698, 0x832f1041, 0x87ee0df6,
            0x99a95df3, 0x9d684044, 0x902b669d, 0x94ea7b2a, 0xe0b41de7, 0xe4750050,
            0xe9362689, 0xedf73b3e, 0xf3b06b3b, 0xf771768c, 0xfa325055, 0xfef34de2,
            0xc6bcf05f, 0xc27dede8, 0xcf3ecb31, 0xcbffd686, 0xd5b88683, 0xd1799b34,
            0xdc3abded, 0xd8fba05a, 0x690ce0ee, 0x6dcdfd59, 0x608edb80, 0x644fc637,
            0x7a089632, 0x7ec98b85, 0x738aad5c, 0x774bb0eb, 0x4f040d56, 0x4bc510e1,
            0x46863638, 0x42472b8f, 0x5c007b8a, 0x58c1663d, 0x558240e4, 0x51435d53,
            0x251d3b9e, 0x21dc2629, 0x2c9f00f0, 0x285e1d47, 0x36194d42, 0x32d850f5,
            0x3f9b762c, 0x3b5a6b9b, 0x0315d626, 0x07d4cb91, 0x0a97ed48, 0x0e56f0ff,
            0x1011a0fa, 0x14d0bd4d, 0x19939b94, 0x1d528623, 0xf12f560e, 0xf5ee4bb9,
            0xf8ad6d60, 0xfc6c70d7, 0xe22b20d2, 0xe6ea3d65, 0xeba91bbc, 0xef68060b,
            0xd727bbb6, 0xd3e6a601, 0xdea580d8, 0xda649d6f, 0xc423cd6a, 0xc0e2d0dd,
            0xcda1f604, 0xc960ebb3, 0xbd3e8d7e, 0xb9ff90c9, 0xb4bcb610, 0xb07daba7,
            0xae3afba2, 0xaafbe615, 0xa7b8c0cc, 0xa379dd7b, 0x9b3660c6, 0x9ff77d71,
            0x92b45ba8, 0x9675461f, 0x8832161a, 0x8cf30bad, 0x81b02d74, 0x857130c3,
            0x5d8a9099, 0x594b8d2e, 0x5408abf7, 0x50c9b640, 0x4e8ee645, 0x4a4ffbf2,
            0x470cdd2b, 0x43cdc09c, 0x7b827d21, 0x7f436096, 0x7200464f, 0x76c15bf8,
            0x68860bfd, 0x6c47164a, 0x61043093, 0x65c52d24, 0x119b4be9, 0x155a565e,
            0x18197087, 0x1cd86d30, 0x029f3d35, 0x065e2082, 0x0b1d065b, 0x0fdc1bec,
            0x3793a651, 0x3352bbe6, 0x3e119d3f, 0x3ad08088, 0x2497d08d, 0x2056cd3a,
            0x2d15ebe3, 0x29d4f654, 0xc5a92679, 0xc1683bce, 0xcc2b1d17, 0xc8ea00a0,
            0xd6ad50a5, 0xd26c4d12, 0xdf2f6bcb, 0xdbee767c, 0xe3a1cbc1, 0xe760d676,
            0xea23f0af, 0xeee2ed18, 0xf0a5bd1d, 0xf464a0aa, 0xf9278673, 0xfde69bc4,
            0x89b8fd09, 0x8d79e0be, 0x803ac667, 0x84fbdbd0, 0x9abc8bd5, 0x9e7d9662,
            0x933eb0bb, 0x97ffad0c, 0xafb010b1, 0xab710d06, 0xa6322bdf, 0xa2f33668,
            0xbcb4666d, 0xb8757bda, 0xb5365d03, 0xb1f740b4
        };
        
        uint32_t crc = 0xffffffff;
        
        for (int i=0; i<len; i++) {
            crc = (crc << 8) ^ table[((crc >> 24) ^ *data++) & 0xff];
        }
        
        return crc;
    }
    
    // @see https://github.com/ETrun/crc32/blob/master/crc32.c
    uint32_t __crc32_ieee(uint32_t init, const uint8_t* buf, size_t nb_buf)
    {
        /*----------------------------------------------------------------------------*\
         *  CRC-32 version 2.0.0 by Craig Bruce, 2006-04-29.
         *
         *  This program generates the CRC-32 values for the files named in the
         *  command-line arguments.  These are the same CRC-32 values used by GZIP,
         *  PKZIP, and ZMODEM.  The Crc32_ComputeBuf() can also be detached and
         *  used independently.
         *
         *  THIS PROGRAM IS PUBLIC-DOMAIN SOFTWARE.
         *
         *  Based on the byte-oriented implementation "File Verification Using CRC"
         *  by Mark R. Nelson in Dr. Dobb's Journal, May 1992, pp. 64-67.
         *
         *  v1.0.0: original release.
         *  v1.0.1: fixed printf formats.
         *  v1.0.2: fixed something else.
         *  v1.0.3: replaced CRC constant table by generator function.
         *  v1.0.4: reformatted code, made ANSI C.  1994-12-05.
         *  v2.0.0: rewrote to use memory buffer & static table, 2006-04-29.
         *  v2.1.0: modified by Nico, 2013-04-20
         \*----------------------------------------------------------------------------*/
        static const uint32_t table[256] = {
            0x00000000,0x77073096,0xEE0E612C,0x990951BA,0x076DC419,0x706AF48F,0xE963A535,
            0x9E6495A3,0x0EDB8832,0x79DCB8A4,0xE0D5E91E,0x97D2D988,0x09B64C2B,0x7EB17CBD,
            0xE7B82D07,0x90BF1D91,0x1DB71064,0x6AB020F2,0xF3B97148,0x84BE41DE,0x1ADAD47D,
            0x6DDDE4EB,0xF4D4B551,0x83D385C7,0x136C9856,0x646BA8C0,0xFD62F97A,0x8A65C9EC,
            0x14015C4F,0x63066CD9,0xFA0F3D63,0x8D080DF5,0x3B6E20C8,0x4C69105E,0xD56041E4,
            0xA2677172,0x3C03E4D1,0x4B04D447,0xD20D85FD,0xA50AB56B,0x35B5A8FA,0x42B2986C,
            0xDBBBC9D6,0xACBCF940,0x32D86CE3,0x45DF5C75,0xDCD60DCF,0xABD13D59,0x26D930AC,
            0x51DE003A,0xC8D75180,0xBFD06116,0x21B4F4B5,0x56B3C423,0xCFBA9599,0xB8BDA50F,
            0x2802B89E,0x5F058808,0xC60CD9B2,0xB10BE924,0x2F6F7C87,0x58684C11,0xC1611DAB,
            0xB6662D3D,0x76DC4190,0x01DB7106,0x98D220BC,0xEFD5102A,0x71B18589,0x06B6B51F,
            0x9FBFE4A5,0xE8B8D433,0x7807C9A2,0x0F00F934,0x9609A88E,0xE10E9818,0x7F6A0DBB,
            0x086D3D2D,0x91646C97,0xE6635C01,0x6B6B51F4,0x1C6C6162,0x856530D8,0xF262004E,
            0x6C0695ED,0x1B01A57B,0x8208F4C1,0xF50FC457,0x65B0D9C6,0x12B7E950,0x8BBEB8EA,
            0xFCB9887C,0x62DD1DDF,0x15DA2D49,0x8CD37CF3,0xFBD44C65,0x4DB26158,0x3AB551CE,
            0xA3BC0074,0xD4BB30E2,0x4ADFA541,0x3DD895D7,0xA4D1C46D,0xD3D6F4FB,0x4369E96A,
            0x346ED9FC,0xAD678846,0xDA60B8D0,0x44042D73,0x33031DE5,0xAA0A4C5F,0xDD0D7CC9,
            0x5005713C,0x270241AA,0xBE0B1010,0xC90C2086,0x5768B525,0x206F85B3,0xB966D409,
            0xCE61E49F,0x5EDEF90E,0x29D9C998,0xB0D09822,0xC7D7A8B4,0x59B33D17,0x2EB40D81,
            0xB7BD5C3B,0xC0BA6CAD,0xEDB88320,0x9ABFB3B6,0x03B6E20C,0x74B1D29A,0xEAD54739,
            0x9DD277AF,0x04DB2615,0x73DC1683,0xE3630B12,0x94643B84,0x0D6D6A3E,0x7A6A5AA8,
            0xE40ECF0B,0x9309FF9D,0x0A00AE27,0x7D079EB1,0xF00F9344,0x8708A3D2,0x1E01F268,
            0x6906C2FE,0xF762575D,0x806567CB,0x196C3671,0x6E6B06E7,0xFED41B76,0x89D32BE0,
            0x10DA7A5A,0x67DD4ACC,0xF9B9DF6F,0x8EBEEFF9,0x17B7BE43,0x60B08ED5,0xD6D6A3E8,
            0xA1D1937E,0x38D8C2C4,0x4FDFF252,0xD1BB67F1,0xA6BC5767,0x3FB506DD,0x48B2364B,
            0xD80D2BDA,0xAF0A1B4C,0x36034AF6,0x41047A60,0xDF60EFC3,0xA867DF55,0x316E8EEF,
            0x4669BE79,0xCB61B38C,0xBC66831A,0x256FD2A0,0x5268E236,0xCC0C7795,0xBB0B4703,
            0x220216B9,0x5505262F,0xC5BA3BBE,0xB2BD0B28,0x2BB45A92,0x5CB36A04,0xC2D7FFA7,
            0xB5D0CF31,0x2CD99E8B,0x5BDEAE1D,0x9B64C2B0,0xEC63F226,0x756AA39C,0x026D930A,
            0x9C0906A9,0xEB0E363F,0x72076785,0x05005713,0x95BF4A82,0xE2B87A14,0x7BB12BAE,
            0x0CB61B38,0x92D28E9B,0xE5D5BE0D,0x7CDCEFB7,0x0BDBDF21,0x86D3D2D4,0xF1D4E242,
            0x68DDB3F8,0x1FDA836E,0x81BE16CD,0xF6B9265B,0x6FB077E1,0x18B74777,0x88085AE6,
            0xFF0F6A70,0x66063BCA,0x11010B5C,0x8F659EFF,0xF862AE69,0x616BFFD3,0x166CCF45,
            0xA00AE278,0xD70DD2EE,0x4E048354,0x3903B3C2,0xA7672661,0xD06016F7,0x4969474D,
            0x3E6E77DB,0xAED16A4A,0xD9D65ADC,0x40DF0B66,0x37D83BF0,0xA9BCAE53,0xDEBB9EC5,
            0x47B2CF7F,0x30B5FFE9,0xBDBDF21C,0xCABAC28A,0x53B39330,0x24B4A3A6,0xBAD03605,
            0xCDD70693,0x54DE5729,0x23D967BF,0xB3667A2E,0xC4614AB8,0x5D681B02,0x2A6F2B94,
            0xB40BBE37,0xC30C8EA1,0x5A05DF1B,0x2D02EF8D
        };
        
        uint32_t crc = init ^ 0xFFFFFFFF;
        
        for (size_t i = 0; i < nb_buf; i++) {
            crc = table[(crc ^ buf[i]) & 0xff] ^ (crc >> 8);
        }
        
        return crc^0xFFFFFFFF;
    }
    
    uint32_t srs_crc32_mpegts(const void* buf, int size)
    {
        return __mpegts_crc32((const uint8_t*)buf, size);
    }
    
    uint32_t srs_crc32_ieee(const void* buf, int size, uint32_t previous)
    {
        return __crc32_ieee(previous, (const uint8_t*)buf, size);
    }
    
    /*
     * Copyright (c) 2006 Ryan Martell. (rdm4@martellventures.com)
     *
     * This file is part of FFmpeg.
     *
     * FFmpeg is free software; you can redistribute it and/or
     * modify it under the terms of the GNU Lesser General Public
     * License as published by the Free Software Foundation; either
     * version 2.1 of the License, or (at your option) any later version.
     *
     * FFmpeg is distributed in the hope that it will be useful,
     * but WITHOUT ANY WARRANTY; without even the implied warranty of
     * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
     * Lesser General Public License for more details.
     *
     * You should have received a copy of the GNU Lesser General Public
     * License along with FFmpeg; if not, write to the Free Software
     * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
     */
    
#ifndef UINT_MAX
#define UINT_MAX 0xffffffff
#endif
    
#ifndef AV_RB32
#   define AV_RB32(x)                                \
    (((uint32_t)((const uint8_t*)(x))[0] << 24) |    \
    (((const uint8_t*)(x))[1] << 16) |    \
    (((const uint8_t*)(x))[2] <<  8) |    \
    ((const uint8_t*)(x))[3])
#endif
    
#ifndef AV_WL32
#   define AV_WL32(p, darg) do {                \
    unsigned d = (darg);                    \
    ((uint8_t*)(p))[0] = (d);               \
    ((uint8_t*)(p))[1] = (d)>>8;            \
    ((uint8_t*)(p))[2] = (d)>>16;           \
    ((uint8_t*)(p))[3] = (d)>>24;           \
} while(0)
#endif
    
#   define AV_WN(s, p, v) AV_WL##s(p, v)
    
#   if    defined(AV_WN32) && !defined(AV_WL32)
#       define AV_WL32(p, v) AV_WN32(p, v)
#   elif !defined(AV_WN32) &&  defined(AV_WL32)
#       define AV_WN32(p, v) AV_WL32(p, v)
#   endif
    
#ifndef AV_WN32
    #   define AV_WN32(p, v) AV_WN(32, p, v)
#endif
    
#define AV_BSWAP16C(x) (((x) << 8 & 0xff00)  | ((x) >> 8 & 0x00ff))
#define AV_BSWAP32C(x) (AV_BSWAP16C(x) << 16 | AV_BSWAP16C((x) >> 16))
    
#ifndef av_bswap32
    static const uint32_t av_bswap32(uint32_t x)
    {
        return AV_BSWAP32C(x);
    }
#endif
    
#define av_be2ne32(x) av_bswap32(x)
    
    /**
     * @file
     * @brief Base64 encode/decode
     * @author Ryan Martell <rdm4@martellventures.com> (with lots of Michael)
     */
    
    /* ---------------- private code */
    static const uint8_t map2[256] =
    {
        0xfe, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff,
        
        0x3e, 0xff, 0xff, 0xff, 0x3f, 0x34, 0x35, 0x36,
        0x37, 0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0xff,
        0xff, 0xff, 0xfe, 0xff, 0xff, 0xff, 0x00, 0x01,
        0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09,
        0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11,
        0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x1a, 0x1b,
        0x1c, 0x1d, 0x1e, 0x1f, 0x20, 0x21, 0x22, 0x23,
        0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x2b,
        0x2c, 0x2d, 0x2e, 0x2f, 0x30, 0x31, 0x32, 0x33,
        
        0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    };
    
#define BASE64_DEC_STEP(i) do { \
    bits = map2[in[i]]; \
    if (bits & 0x80) \
        goto out ## i; \
    v = i ? (v << 6) + bits : bits; \
} while(0)
    
    int srs_av_base64_decode(uint8_t* out, const char* in_str, int out_size)
    {
        uint8_t *dst = out;
        uint8_t *end = out + out_size;
        // no sign extension
        const uint8_t *in = (const uint8_t*)in_str;
        unsigned bits = 0xff;
        unsigned v;
        
        while (end - dst > 3) {
            BASE64_DEC_STEP(0);
            BASE64_DEC_STEP(1);
            BASE64_DEC_STEP(2);
            BASE64_DEC_STEP(3);
            // Using AV_WB32 directly confuses compiler
            v = av_be2ne32(v << 8);
            AV_WN32(dst, v);
            dst += 3;
            in += 4;
        }
        if (end - dst) {
            BASE64_DEC_STEP(0);
            BASE64_DEC_STEP(1);
            BASE64_DEC_STEP(2);
            BASE64_DEC_STEP(3);
            *dst++ = v >> 16;
            if (end - dst)
                *dst++ = v >> 8;
            if (end - dst)
                *dst++ = v;
            in += 4;
        }
        while (1) {
            BASE64_DEC_STEP(0);
            in++;
            BASE64_DEC_STEP(0);
            in++;
            BASE64_DEC_STEP(0);
            in++;
            BASE64_DEC_STEP(0);
            in++;
        }
        
    out3:
        *dst++ = v >> 10;
        v <<= 2;
    out2:
        *dst++ = v >> 4;
    out1:
    out0:
        return bits & 1 ? -1 : dst - out;
    }
    
    /*****************************************************************************
     * b64_encode: Stolen from VLC's http.c.
     * Simplified by Michael.
     * Fixed edge cases and made it work from data (vs. strings) by Ryan.
     *****************************************************************************/
    
    char* srs_av_base64_encode(char* out, int out_size, const uint8_t* in, int in_size)
    {
        static const char b64[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        char *ret, *dst;
        unsigned i_bits = 0;
        int i_shift = 0;
        int bytes_remaining = in_size;
        
        if (in_size >= (int)(UINT_MAX / 4) ||
            out_size < SRS_AV_BASE64_SIZE(in_size))
            return NULL;
        ret = dst = out;
        while (bytes_remaining > 3) {
            i_bits = AV_RB32(in);
            in += 3; bytes_remaining -= 3;
            *dst++ = b64[ i_bits>>26        ];
            *dst++ = b64[(i_bits>>20) & 0x3F];
            *dst++ = b64[(i_bits>>14) & 0x3F];
            *dst++ = b64[(i_bits>>8 ) & 0x3F];
        }
        i_bits = 0;
        while (bytes_remaining) {
            i_bits = (i_bits << 8) + *in++;
            bytes_remaining--;
            i_shift += 8;
        }
        while (i_shift > 0) {
            *dst++ = b64[(i_bits << 6 >> i_shift) & 0x3f];
            i_shift -= 6;
        }
        while ((dst - ret) & 3)
            *dst++ = '=';
        *dst = '\0';
        
        return ret;
    }
    
#define SPACE_CHARS " \t\r\n"
    
    int av_toupper(int c)
    {
        if (c >= 'a' && c <= 'z') {
            c ^= 0x20;
        }
        return c;
    }
    
    int ff_hex_to_data(uint8_t* data, const char* p)
    {
        int c, len, v;
        
        len = 0;
        v = 1;
        for (;;) {
            p += strspn(p, SPACE_CHARS);
            if (*p == '\0')
                break;
            c = av_toupper((unsigned char) *p++);
            if (c >= '0' && c <= '9')
                c = c - '0';
            else if (c >= 'A' && c <= 'F')
                c = c - 'A' + 10;
            else
                break;
            v = (v << 4) | c;
            if (v & 0x100) {
                if (data)
                    data[len] = v;
                len++;
                v = 1;
            }
        }
        return len;
    }
    
    int srs_chunk_header_c0(int perfer_cid, uint32_t timestamp, int32_t payload_length, int8_t message_type, int32_t stream_id, char* cache, int nb_cache)
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
        *p++ = 0x00 | (perfer_cid & 0x3F);
        
        // chunk message header, 11 bytes
        // timestamp, 3bytes, big-endian
        if (timestamp < RTMP_EXTENDED_TIMESTAMP) {
            pp = (char*)&timestamp;
            *p++ = pp[2];
            *p++ = pp[1];
            *p++ = pp[0];
        } else {
            *p++ = 0xFF;
            *p++ = 0xFF;
            *p++ = 0xFF;
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
    
    int srs_chunk_header_c3(int perfer_cid, uint32_t timestamp, char* cache, int nb_cache)
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
        // @remark, if perfer_cid > 0x3F, that is, use 2B/3B chunk header,
        // SRS will rollback to 1B chunk header.
        *p++ = 0xC0 | (perfer_cid & 0x3F);
        
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
    
