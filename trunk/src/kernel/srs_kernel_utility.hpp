//
// Copyright (c) 2013-2026 The SRS Authors
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

#define srs_min(a, b) (((a) < (b)) ? (a) : (b))
#define srs_max(a, b) (((a) < (b)) ? (b) : (a))

// Cached to avoid the performance cost of reading the clock each time.
extern srs_utime_t srs_time_now_cached();
extern srs_utime_t srs_time_since_startup();
// A daemon st-thread calls it to update the cache.
extern srs_utime_t srs_time_now_realtime();

// For utest to mock it.
#ifdef SRS_OSX
#define _srs_gettimeofday srs_gettimeofday
#else
typedef int (*srs_gettimeofday_t)(struct timeval *tv, struct timezone *tz);
#endif

// For utest to mock it.
typedef int (*srs_mkdir_t)(const char *path);

extern bool srs_is_little_endian();

extern std::string srs_strconv_format_int(int64_t value);
extern std::string srs_strconv_format_float(double value);
extern std::string srs_strconv_format_bool(bool v);

extern std::string srs_strings_replace(std::string str, std::string old_str, std::string new_str);
extern std::string srs_strings_trim_end(std::string str, std::string trim_chars);
extern std::string srs_strings_trim_start(std::string str, std::string trim_chars);
extern std::string srs_strings_remove(std::string str, std::string remove_chars);
extern std::string srs_erase_first_substr(std::string str, std::string erase_string);
extern std::string srs_erase_last_substr(std::string str, std::string erase_string);
extern bool srs_strings_ends_with(std::string str, std::string flag);
extern bool srs_strings_ends_with(std::string str, std::string flag0, std::string flag1);
extern bool srs_strings_ends_with(std::string str, std::string flag0, std::string flag1, std::string flag2);
extern bool srs_strings_ends_with(std::string str, std::string flag0, std::string flag1, std::string flag2, std::string flag3);
extern bool srs_strings_starts_with(std::string str, std::string flag);
extern bool srs_strings_starts_with(std::string str, std::string flag0, std::string flag1);
extern bool srs_strings_starts_with(std::string str, std::string flag0, std::string flag1, std::string flag2);
extern bool srs_strings_starts_with(std::string str, std::string flag0, std::string flag1, std::string flag2, std::string flag3);
extern bool srs_strings_contains(std::string str, std::string flag);
extern bool srs_strings_contains(std::string str, std::string flag0, std::string flag1);
extern bool srs_strings_contains(std::string str, std::string flag0, std::string flag1, std::string flag2);
extern int srs_strings_count(std::string str, std::string flag);
extern std::string srs_strings_min_match(std::string str, std::vector<std::string> flags);
extern std::vector<std::string> srs_strings_split(std::string s, std::string seperator);
extern std::vector<std::string> srs_strings_split(std::string s, std::vector<std::string> seperators);
extern std::string srs_fmt_sprintf(const char *fmt, ...);

extern std::string srs_strings_dumps_hex(const std::string &str);
extern std::string srs_strings_dumps_hex(const char *str, int length);
extern std::string srs_strings_dumps_hex(const char *str, int length, int limit);
extern std::string srs_strings_dumps_hex(const char *str, int length, int limit, char seperator, int line_limit, char newline);

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

extern bool srs_bytes_equal(void *pa, void *pb, int size);

class SrsPath
{
public:
    SrsPath();
    virtual ~SrsPath();

public:
    virtual bool exists(std::string path);
    virtual bool is_dir(std::string path);
    virtual srs_error_t unlink(std::string path);
    virtual std::string filepath_dir(std::string path);
    virtual std::string filepath_base(std::string path);
    virtual std::string filepath_filename(std::string path);
    virtual std::string filepath_ext(std::string path);

public:
    virtual srs_error_t mkdir_all(std::string dir);
};

extern int srs_hex_decode_string(uint8_t *data, const char *p, int size);

extern char *srs_hex_encode_to_string(char *des, const uint8_t *src, int len);
extern char *srs_hex_encode_to_string_lowercase(char *des, const uint8_t *src, int len);

class ISrsRand
{
public:
    ISrsRand();
    virtual ~ISrsRand();

public:
    virtual void gen_bytes(char *bytes, int size) = 0;
    virtual std::string gen_str(int len) = 0;
    virtual long integer() = 0;
    virtual long integer(long min, long max) = 0;
};

class SrsRand : public ISrsRand
{
public:
    SrsRand();
    virtual ~SrsRand();

public:
    virtual void gen_bytes(char *bytes, int size);
    virtual std::string gen_str(int len);
    virtual long integer();
    virtual long integer(long min, long max);
};

extern bool srs_is_digit_number(std::string str);

extern srs_error_t srs_io_readall(ISrsReader *in, std::string &content);

extern void srs_net_split_hostport(std::string hostport, std::string &host, int &port);

extern void srs_net_split_for_listener(std::string hostport, std::string &ip, int &port);

extern std::string srs_net_address_any();

extern bool srs_net_is_valid_ip(std::string ip);

extern bool srs_net_is_valid_endpoint(std::string endpoint);

extern bool srs_net_is_ipv4(std::string domain);

extern uint32_t srs_net_ipv4_to_integer(std::string ip);

extern bool srs_net_ipv4_within_mask(std::string ip, std::string network, std::string mask);

extern std::string srs_net_get_cidr_mask(std::string network_address);

extern std::string srs_net_get_cidr_ipv4(std::string network_address);

extern bool srs_net_url_is_http(std::string url);
extern bool srs_net_url_is_rtmp(std::string url);

// The stream and param are both input and output.
extern void srs_net_url_parse_tcurl(std::string tcUrl, std::string &schema, std::string &host, std::string &vhost, std::string &app,
                                    std::string &stream, int &port, std::string &param);

// Legacy format: rtmp://ip/app/app2?vhost=xxx/stream
// Standard format: rtmp://ip/app/app2/stream?vhost=xxx
extern std::string srs_net_url_convert_legacy_rtmp_url(const std::string &url);

// To make OBS happy, the stream may be in any of these forms:
//      rtmp://ip/live/livestream
//      rtmp://ip/live/livestream?secret=xxx
//      rtmp://ip/live?secret=xxx/livestream
extern void srs_net_url_guess_stream(std::string &app, std::string &param, std::string &stream);

extern void srs_net_url_parse_query(std::string q, std::map<std::string, std::string> &query);

extern std::string srs_net_url_encode_tcurl(std::string schema, std::string host, std::string vhost, std::string app, int port);

extern std::string srs_net_url_encode_stream(std::string host, std::string vhost, std::string stream, std::string param, bool with_vhost);

extern std::string srs_net_url_encode_sid(std::string vhost, std::string app, std::string stream);

extern void srs_net_url_parse_rtmp_url(std::string url, std::string &tcUrl, std::string &stream);

extern std::string srs_net_url_encode_rtmp_url(std::string server, int port, std::string host, std::string vhost, std::string app, std::string stream, std::string param);

#endif
