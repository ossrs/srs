/*
The MIT License (MIT)

Copyright (c) 2013-2015 SRS(ossrs)

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

#ifndef SRS_KERNEL_UTILITY_HPP
#define SRS_KERNEL_UTILITY_HPP

/*
#include <srs_kernel_utility.hpp>
*/

#include <srs_core.hpp>

#include <string>

class SrsStream;
class SrsBitStream;

// compare
#define srs_min(a, b) (((a) < (b))? (a) : (b))
#define srs_max(a, b) (((a) < (b))? (b) : (a))

// read nalu uev.
extern int srs_avc_nalu_read_uev(SrsBitStream* stream, int32_t& v);
extern int srs_avc_nalu_read_bit(SrsBitStream* stream, int8_t& v);

// get current system time in ms, use cache to avoid performance problem
extern int64_t srs_get_system_time_ms();
extern int64_t srs_get_system_startup_time_ms();
// the deamon st-thread will update it.
extern int64_t srs_update_system_time_ms();

// dns resolve utility, return the resolved ip address.
extern std::string srs_dns_resolve(std::string host);

// whether system is little endian
extern bool srs_is_little_endian();

// replace old_str to new_str of str
extern std::string srs_string_replace(std::string str, std::string old_str, std::string new_str);
// trim char in trim_chars of str
extern std::string srs_string_trim_end(std::string str, std::string trim_chars);
// trim char in trim_chars of str
extern std::string srs_string_trim_start(std::string str, std::string trim_chars);
// remove char in remove_chars of str
extern std::string srs_string_remove(std::string str, std::string remove_chars);
// whether string end with
extern bool srs_string_ends_with(std::string str, std::string flag);
// whether string starts with
extern bool srs_string_starts_with(std::string str, std::string flag);
extern bool srs_string_starts_with(std::string str, std::string flag0, std::string flag1);
// whether string contains with
extern bool srs_string_contains(std::string str, std::string flag);
extern bool srs_string_contains(std::string str, std::string flag0, std::string flag1);
extern bool srs_string_contains(std::string str, std::string flag0, std::string flag1, std::string flag2);

// create dir recursively
extern int srs_create_dir_recursively(std::string dir);

// whether path exists.
extern bool srs_path_exists(std::string path);
// get the dirname of path
extern std::string srs_path_dirname(std::string path);
// get the basename of path
extern std::string srs_path_basename(std::string path);

/**
* whether stream starts with the avc NALU in "AnnexB" 
* from H.264-AVC-ISO_IEC_14496-10.pdf, page 211.
* start code must be "N[00] 00 00 01" where N>=0
* @param pnb_start_code output the size of start code, must >=3. 
*       NULL to ignore.
*/
extern bool srs_avc_startswith_annexb(SrsStream* stream, int* pnb_start_code = NULL);

/**
* whether stream starts with the aac ADTS 
* from aac-mp4a-format-ISO_IEC_14496-3+2001.pdf, page 75, 1.A.2.2 ADTS.
* start code must be '1111 1111 1111'B, that is 0xFFF
*/
extern bool srs_aac_startswith_adts(SrsStream* stream);

/**
* cacl the crc32 of bytes in buf.
*/
extern u_int32_t srs_crc32(const void* buf, int size);

/**
* Decode a base64-encoded string.
*
* @param out      buffer for decoded data
* @param in       null-terminated input string
* @param out_size size in bytes of the out buffer, must be at
*                 least 3/4 of the length of in
* @return         number of bytes written, or a negative value in case of
*                 invalid input
*/
extern int srs_av_base64_decode(u_int8_t* out, const char* in, int out_size);

/**
* Encode data to base64 and null-terminate.
*
* @param out      buffer for encoded data
* @param out_size size in bytes of the out buffer (including the
*                 null terminator), must be at least AV_BASE64_SIZE(in_size)
* @param in       input buffer containing the data to encode
* @param in_size  size in bytes of the in buffer
* @return         out or NULL in case of error
*/
extern char* srs_av_base64_encode(char* out, int out_size, const u_int8_t* in, int in_size);

/**
 * Calculate the output size needed to base64-encode x bytes to a
 * null-terminated string.
 */
#define SRS_AV_BASE64_SIZE(x)  (((x)+2) / 3 * 4 + 1)

/**
* convert hex string to data.
* for example, p=config='139056E5A0'
* output hex to data={0x13, 0x90, 0x56, 0xe5, 0xa0}
*/
extern int ff_hex_to_data(u_int8_t* data, const char* p);

/**
 * generate the c0 chunk header for msg.
 * @param cache, the cache to write header.
 * @param nb_cache, the size of cache.
 * @return the size of header. 0 if cache not enough.
 */
extern int srs_chunk_header_c0(
    int perfer_cid, u_int32_t timestamp, int32_t payload_length,
    int8_t message_type, int32_t stream_id,
    char* cache, int nb_cache
    );

/**
 * generate the c3 chunk header for msg.
 * @param cache, the cache to write header.
 * @param nb_cache, the size of cache.
 * @return the size of header. 0 if cache not enough.
 */
extern int srs_chunk_header_c3(
    int perfer_cid, u_int32_t timestamp,
    char* cache, int nb_cache
    );

#endif

