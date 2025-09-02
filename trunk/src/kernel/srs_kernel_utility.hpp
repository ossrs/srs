//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_KERNEL_UTILITY_HPP
#define SRS_KERNEL_UTILITY_HPP

#include <srs_core.hpp>

#include <map>
#include <sstream>
#include <string>
#include <vector>

class SrsBuffer;
class SrsBitBuffer;
class ISrsReader;

// Basic compare function.
#define srs_min(a, b) (((a) < (b)) ? (a) : (b))
#define srs_max(a, b) (((a) < (b)) ? (b) : (a))

// Get current system time in srs_utime_t, use cache to avoid performance problem
extern srs_utime_t srs_time_now_cached();
extern srs_utime_t srs_time_since_startup();
// A daemon st-thread updates it.
extern srs_utime_t srs_time_now_realtime();

// For utest to mock it.
#include <sys/time.h>
#ifdef SRS_OSX
#define _srs_gettimeofday gettimeofday
#else
typedef int (*srs_gettimeofday_t)(struct timeval *tv, struct timezone *tz);
#endif

// Whether system is little endian
extern bool srs_is_little_endian();

// Parse the int64 value to string.
extern std::string srs_strconv_format_int(int64_t value);
// Parse the float value to string, precise is 2.
extern std::string srs_strconv_format_float(double value);
// Convert bool to switch value, true to "on", false to "off".
extern std::string srs_strconv_format_bool(bool v);

// Replace old_str to new_str of str
extern std::string srs_strings_replace(std::string str, std::string old_str, std::string new_str);
// Trim char in trim_chars of str
extern std::string srs_strings_trim_end(std::string str, std::string trim_chars);
// Trim char in trim_chars of str
extern std::string srs_strings_trim_start(std::string str, std::string trim_chars);
// Remove char in remove_chars of str
extern std::string srs_strings_remove(std::string str, std::string remove_chars);
// Remove first substring from str
extern std::string srs_erase_first_substr(std::string str, std::string erase_string);
// Remove last substring from str
extern std::string srs_erase_last_substr(std::string str, std::string erase_string);
// Whether string end with
extern bool srs_strings_ends_with(std::string str, std::string flag);
extern bool srs_strings_ends_with(std::string str, std::string flag0, std::string flag1);
extern bool srs_strings_ends_with(std::string str, std::string flag0, std::string flag1, std::string flag2);
extern bool srs_strings_ends_with(std::string str, std::string flag0, std::string flag1, std::string flag2, std::string flag3);
// Whether string starts with
extern bool srs_strings_starts_with(std::string str, std::string flag);
extern bool srs_strings_starts_with(std::string str, std::string flag0, std::string flag1);
extern bool srs_strings_starts_with(std::string str, std::string flag0, std::string flag1, std::string flag2);
extern bool srs_strings_starts_with(std::string str, std::string flag0, std::string flag1, std::string flag2, std::string flag3);
// Whether string contains with
extern bool srs_strings_contains(std::string str, std::string flag);
extern bool srs_strings_contains(std::string str, std::string flag0, std::string flag1);
extern bool srs_strings_contains(std::string str, std::string flag0, std::string flag1, std::string flag2);
// Count each char of flag in string
extern int srs_strings_count(std::string str, std::string flag);
// Find the min match in str for flags.
extern std::string srs_strings_min_match(std::string str, std::vector<std::string> flags);
// Split the string by seperator to array.
extern std::vector<std::string> srs_strings_split(std::string s, std::string seperator);
extern std::vector<std::string> srs_strings_split(std::string s, std::vector<std::string> seperators);
// Format to a string.
extern std::string srs_fmt_sprintf(const char *fmt, ...);

// Dump string(str in length) to hex, it will process min(limit, length) chars.
// Append seperator between each elem, and newline when exceed line_limit, '\0' to ignore.
extern std::string srs_strings_dumps_hex(const std::string &str);
extern std::string srs_strings_dumps_hex(const char *str, int length);
extern std::string srs_strings_dumps_hex(const char *str, int length, int limit);
extern std::string srs_strings_dumps_hex(const char *str, int length, int limit, char seperator, int line_limit, char newline);

// join string in vector with indicated separator
template <typename T>
std::string srs_strings_join(std::vector<T> &vs, std::string separator)
{
    std::stringstream ss;

    for (int i = 0; i < (int)vs.size(); i++) {
        ss << vs.at(i);
        if (i != (int)vs.size() - 1) {
            ss << separator;
        }
    }

    return ss.str();
}

// Compare two vector with string.
template <typename T>
bool srs_strings_equal(std::vector<T> &a, std::vector<T> &b)
{
    if (a.size() != b.size()) {
        return false;
    }

    for (int i = 0; i < (int)a.size(); i++) {
        if (a.at(i) != b.at(i)) {
            return false;
        }
    }

    return true;
}

// Compare the memory in bytes.
// @return true if completely equal; otherwise, false.
extern bool srs_bytes_equal(void *pa, void *pb, int size);

// Create dir recursively
extern srs_error_t srs_os_mkdir_all(std::string dir);

// Whether path exists.
extern bool srs_path_exists(std::string path);
// Get the dirname of path, for instance, dirname("/live/livestream")="/live"
extern std::string srs_path_filepath_dir(std::string path);
// Get the basename of path, for instance, basename("/live/livestream")="livestream"
extern std::string srs_path_filepath_base(std::string path);
// Get the filename of path, for instance, filename("livestream.flv")="livestream"
extern std::string srs_path_filepath_filename(std::string path);
// Get the file extension of path, for instance, filext("live.flv")=".flv"
extern std::string srs_path_filepath_ext(std::string path);

// Covert hex string p to uint8 data, for example:
//      srs_hex_decode_string(data, string("139056E5A0"))
//      which outputs the data in hex {0x13, 0x90, 0x56, 0xe5, 0xa0}.
extern int srs_hex_decode_string(uint8_t *data, const char *p, int size);

// Convert data string to hex, for example:
//      srs_hex_encode_to_string(des, {0xf3, 0x3f}, 2)
//      which outputs the des is string("F33F").
extern char *srs_hex_encode_to_string(char *des, const uint8_t *src, int len);
// Output in lowercase, such as string("f33f").
extern char *srs_hex_encode_to_string_lowercase(char *des, const uint8_t *src, int len);

// Generate ramdom data for handshake.
extern void srs_rand_gen_bytes(char *bytes, int size);

// Generate random string [0-9a-z] in size of len bytes.
extern std::string srs_rand_gen_str(int len);

// Generate random value, use srandom(now_us) to init seed if not initialized.
extern long srs_rand_integer();

// Whether string is digit number
//      is_digit("0")  is true
//      is_digit("0000000000")  is true
//      is_digit("1234567890")  is true
//      is_digit("0123456789")  is true
//      is_digit("1234567890a") is false
//      is_digit("a1234567890") is false
//      is_digit("10e3") is false
//      is_digit("!1234567890") is false
//      is_digit("") is false
extern bool srs_is_digit_number(std::string str);

// Read all content util EOF.
extern srs_error_t srs_io_readall(ISrsReader *in, std::string &content);

// Split the host:port to host and port.
// @remark the hostport format in <host[:port]>, where port is optional.
extern void srs_net_split_hostport(std::string hostport, std::string &host, int &port);

// Parse the endpoint to ip and port.
// @remark The hostport format in <[ip:]port>, where ip is default to "0.0.0.0".
extern void srs_net_split_for_listener(std::string hostport, std::string &ip, int &port);

// The "ANY" address to listen, it's "0.0.0.0" for ipv4, and "::" for ipv6.
// @remark We prefer ipv4, only use ipv6 if ipv4 is disabled.
extern std::string srs_net_address_any();

// Check whether the ip is valid.
extern bool srs_net_is_valid_ip(std::string ip);

// Check whether the endpoint is valid.
extern bool srs_net_is_valid_endpoint(std::string endpoint);

// Whether domain is an IPv4 address.
extern bool srs_net_is_ipv4(std::string domain);

// Convert an IPv4 from string to uint32_t.
extern uint32_t srs_net_ipv4_to_integer(std::string ip);

// Whether the IPv4 is in an IP mask.
extern bool srs_net_ipv4_within_mask(std::string ip, std::string network, std::string mask);

// Get the CIDR (Classless Inter-Domain Routing) mask for a network address.
extern std::string srs_net_get_cidr_mask(std::string network_address);

// Get the CIDR (Classless Inter-Domain Routing) IPv4 for a network address.
extern std::string srs_net_get_cidr_ipv4(std::string network_address);

// Whether the url is starts with http:// or https://
extern bool srs_net_url_is_http(std::string url);
extern bool srs_net_url_is_rtmp(std::string url);

/**
 * parse the tcUrl, output the schema, host, vhost, app and port.
 * @param tcUrl, the input tcUrl, for example,
 *       rtmp://192.168.1.10:19350/live?vhost=vhost.ossrs.net
 * @param schema, for example, rtmp
 * @param host, for example, 192.168.1.10
 * @param vhost, for example, vhost.ossrs.net.
 *       vhost default to host, when user not set vhost in query of app.
 * @param app, for example, live
 * @param port, for example, 19350
 *       default to 1935 if not specified.
 * param param, for example, vhost=vhost.ossrs.net
 * @remark The param stream is input and output param, that is:
 *       input: tcUrl+stream
 *       output: schema, host, vhost, app, stream, port, param
 */
extern void srs_net_url_parse_tcurl(std::string tcUrl, std::string &schema, std::string &host, std::string &vhost, std::string &app,
                                    std::string &stream, int &port, std::string &param);

// Convert legacy RTMP URL format to standard format.
// Legacy format: rtmp://ip/app/app2?vhost=xxx/stream
// Standard format: rtmp://ip/app/app2/stream?vhost=xxx
extern std::string srs_net_url_convert_legacy_rtmp_url(const std::string &url);

// Guessing stream by app and param, to make OBS happy. For example:
//      rtmp://ip/live/livestream
//      rtmp://ip/live/livestream?secret=xxx
//      rtmp://ip/live?secret=xxx/livestream
extern void srs_net_url_guess_stream(std::string &app, std::string &param, std::string &stream);

// parse query string to map(k,v).
// must format as key=value&...&keyN=valueN
extern void srs_net_url_parse_query(std::string q, std::map<std::string, std::string> &query);

/**
 * generate the tcUrl without param.
 * @remark Use host as tcUrl.vhost if vhost is default vhost.
 */
extern std::string srs_net_url_encode_tcurl(std::string schema, std::string host, std::string vhost, std::string app, int port);

/**
 * Generate the stream with param.
 * @remark Append vhost in query string if not default vhost.
 */
extern std::string srs_net_url_encode_stream(std::string host, std::string vhost, std::string stream, std::string param, bool with_vhost);

// get the stream identify, vhost/app/stream.
extern std::string srs_net_url_encode_sid(std::string vhost, std::string app, std::string stream);

// parse the rtmp url to tcUrl/stream,
// for example, rtmp://v.ossrs.net/live/livestream to
//      tcUrl: rtmp://v.ossrs.net/live
//      stream: livestream
extern void srs_net_url_parse_rtmp_url(std::string url, std::string &tcUrl, std::string &stream);

// Genereate the rtmp url, for instance, rtmp://server:port/app/stream?param
// @remark We always put vhost in param, in the query of url.
extern std::string srs_net_url_encode_rtmp_url(std::string server, int port, std::string host, std::string vhost, std::string app, std::string stream, std::string param);

#endif
