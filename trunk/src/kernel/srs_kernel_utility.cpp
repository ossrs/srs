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

int srs_avc_nalu_read_uev(SrsBitStream* stream, int32_t& v)
{
    int ret = ERROR_SUCCESS;
    
    if (stream->empty()) {
        return ERROR_AVC_NALU_UEV;
    }
    
    // ue(v) in 9.1 Parsing process for Exp-Golomb codes
    // H.264-AVC-ISO_IEC_14496-10-2012.pdf, page 227.
    // Syntax elements coded as ue(v), me(v), or se(v) are Exp-Golomb-coded.
    //      leadingZeroBits = -1;
    //      for( b = 0; !b; leadingZeroBits++ )
    //          b = read_bits( 1 )
    // The variable codeNum is then assigned as follows:
    //      codeNum = (2<<leadingZeroBits) – 1 + read_bits( leadingZeroBits )
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
        v += b << (leadingZeroBits - 1);
    }
    
    return ret;
}

int srs_avc_nalu_read_bit(SrsBitStream* stream, int8_t& v)
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

bool srs_string_starts_with(string str, string flag)
{
    return str.find(flag) == 0;
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
static const u_int32_t crc_table[256] = {
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

// @see http://www.stmc.edu.hk/~vincent/ffmpeg_0.4.9-pre1/libavformat/mpegtsenc.c
unsigned int mpegts_crc32(const u_int8_t *data, int len)
{
    register int i;
    unsigned int crc = 0xffffffff;
    
    for (i=0; i<len; i++)
        crc = (crc << 8) ^ crc_table[((crc >> 24) ^ *data++) & 0xff];
    
    return crc;
}

u_int32_t srs_crc32(const void* buf, int size)
{
    return mpegts_crc32((const u_int8_t*)buf, size);
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
    (((uint32_t)((const u_int8_t*)(x))[0] << 24) |    \
               (((const u_int8_t*)(x))[1] << 16) |    \
               (((const u_int8_t*)(x))[2] <<  8) |    \
                ((const u_int8_t*)(x))[3])
#endif

#ifndef AV_WL32
#   define AV_WL32(p, darg) do {                \
        unsigned d = (darg);                    \
        ((u_int8_t*)(p))[0] = (d);               \
        ((u_int8_t*)(p))[1] = (d)>>8;            \
        ((u_int8_t*)(p))[2] = (d)>>16;           \
        ((u_int8_t*)(p))[3] = (d)>>24;           \
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
static const u_int32_t av_bswap32(u_int32_t x)
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
static const u_int8_t map2[256] =
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

int srs_av_base64_decode(u_int8_t* out, const char* in_str, int out_size)
{
    u_int8_t *dst = out;
    u_int8_t *end = out + out_size;
    // no sign extension
    const u_int8_t *in = (const u_int8_t*)in_str;
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

char* srs_av_base64_encode(char* out, int out_size, const u_int8_t* in, int in_size)
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

int ff_hex_to_data(u_int8_t* data, const char* p)
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

