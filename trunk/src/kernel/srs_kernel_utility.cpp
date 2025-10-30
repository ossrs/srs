//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_kernel_utility.hpp>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#include <algorithm>
#include <sstream>
#include <vector>
using namespace std;

#include <srs_core_autofree.hpp>
#include <srs_core_deprecated.hpp>
#include <srs_kernel_buffer.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_flv.hpp>
#include <srs_kernel_io.hpp>
#include <srs_kernel_log.hpp>

#include <climits>
#include <cmath>

// this value must:
// equals to (SRS_SYS_CYCLE_INTERVAL*SRS_SYS_TIME_RESOLUTION_MS_TIMES)*1000
// @see SRS_SYS_TIME_RESOLUTION_MS_TIMES
#define SYS_TIME_RESOLUTION_US 300 * 1000

srs_utime_t _srs_system_time_us_cache = 0;
srs_utime_t _srs_system_time_startup_time = 0;

srs_utime_t srs_time_now_cached()
{
    if (_srs_system_time_us_cache <= 0) {
        srs_time_now_realtime();
    }

    return _srs_system_time_us_cache;
}

srs_utime_t srs_time_since_startup()
{
    if (_srs_system_time_startup_time <= 0) {
        srs_time_now_realtime();
    }

    return _srs_system_time_startup_time;
}

// For utest to mock it.
#ifndef SRS_OSX
srs_gettimeofday_t _srs_gettimeofday = (srs_gettimeofday_t)::gettimeofday;
#endif

srs_utime_t srs_time_now_realtime()
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

bool srs_is_little_endian()
{
    // convert to network(big-endian) order, if not equals,
    // the system is little-endian, so need to convert the int64
    static int little_endian_check = -1;

    if (little_endian_check == -1) {
        union {
            int32_t i;
            int8_t c;
        } little_check_union;

        little_check_union.i = 0x01;
        little_endian_check = little_check_union.c;
    }

    return (little_endian_check == 1);
}

string srs_strconv_format_int(int64_t value)
{
    return srs_fmt_sprintf("%" PRId64, value);
}

string srs_strconv_format_float(double value)
{
    // len(max int64_t) is 20, plus one "+-."
    char tmp[21 + 1];
    snprintf(tmp, sizeof(tmp), "%.2f", value);
    return tmp;
}

string srs_strconv_format_bool(bool v)
{
    return v ? "on" : "off";
}

string srs_strings_replace(string str, string old_str, string new_str)
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

string srs_strings_trim_end(string str, string trim_chars)
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

string srs_strings_trim_start(string str, string trim_chars)
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

string srs_strings_remove(string str, string remove_chars)
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

bool srs_strings_ends_with(string str, string flag)
{
    const size_t pos = str.rfind(flag);
    return (pos != string::npos) && (pos == str.length() - flag.length());
}

bool srs_strings_ends_with(string str, string flag0, string flag1)
{
    return srs_strings_ends_with(str, flag0) || srs_strings_ends_with(str, flag1);
}

bool srs_strings_ends_with(string str, string flag0, string flag1, string flag2)
{
    return srs_strings_ends_with(str, flag0) || srs_strings_ends_with(str, flag1) || srs_strings_ends_with(str, flag2);
}

bool srs_strings_ends_with(string str, string flag0, string flag1, string flag2, string flag3)
{
    return srs_strings_ends_with(str, flag0) || srs_strings_ends_with(str, flag1) || srs_strings_ends_with(str, flag2) || srs_strings_ends_with(str, flag3);
}

bool srs_strings_starts_with(string str, string flag)
{
    return str.find(flag) == 0;
}

bool srs_strings_starts_with(string str, string flag0, string flag1)
{
    return srs_strings_starts_with(str, flag0) || srs_strings_starts_with(str, flag1);
}

bool srs_strings_starts_with(string str, string flag0, string flag1, string flag2)
{
    return srs_strings_starts_with(str, flag0, flag1) || srs_strings_starts_with(str, flag2);
}

bool srs_strings_starts_with(string str, string flag0, string flag1, string flag2, string flag3)
{
    return srs_strings_starts_with(str, flag0, flag1, flag2) || srs_strings_starts_with(str, flag3);
}

bool srs_strings_contains(string str, string flag)
{
    return str.find(flag) != string::npos;
}

bool srs_strings_contains(string str, string flag0, string flag1)
{
    return str.find(flag0) != string::npos || str.find(flag1) != string::npos;
}

bool srs_strings_contains(string str, string flag0, string flag1, string flag2)
{
    return str.find(flag0) != string::npos || str.find(flag1) != string::npos || str.find(flag2) != string::npos;
}

int srs_strings_count(string str, string flag)
{
    int nn = 0;
    for (int i = 0; i < (int)flag.length(); i++) {
        char ch = flag.at(i);
        nn += std::count(str.begin(), str.end(), ch);
    }
    return nn;
}

vector<string> srs_strings_split(string s, string seperator)
{
    vector<string> result;
    if (seperator.empty()) {
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

string srs_strings_min_match(string str, vector<string> seperators)
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

vector<string> srs_strings_split(string str, vector<string> seperators)
{
    vector<string> arr;

    size_t pos = string::npos;
    string s = str;

    while (true) {
        string seperator = srs_strings_min_match(s, seperators);
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

std::string srs_fmt_sprintf(const char *fmt, ...)
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
    SrsPath path;
    if (path.exists(dir)) {
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

    // LCOV_EXCL_START
    // create curren dir.
    mode_t mode = S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IWGRP | S_IXGRP | S_IROTH | S_IXOTH;
    if (::mkdir(dir.c_str(), mode) < 0) {
        if (errno == EEXIST) {
            return ERROR_SYSTEM_DIR_EXISTS;
        }

        ret = ERROR_SYSTEM_CREATE_DIR;
        srs_error("create dir %s failed. ret=%d", dir.c_str(), ret);
        return ret;
    }
    // LCOV_EXCL_STOP

    srs_info("create dir %s success.", dir.c_str());

    return ret;
}

string srs_strings_dumps_hex(const std::string &str)
{
    return srs_strings_dumps_hex(str.c_str(), str.size());
}

string srs_strings_dumps_hex(const char *str, int length)
{
    return srs_strings_dumps_hex(str, length, INT_MAX);
}

string srs_strings_dumps_hex(const char *str, int length, int limit)
{
    return srs_strings_dumps_hex(str, length, limit, ' ', 128, '\n');
}

string srs_strings_dumps_hex(const char *str, int length, int limit, char seperator, int line_limit, char newline)
{
    // 1 byte trailing '\0'.
    const int LIMIT = 1024 * 16 + 1;
    static char buf[LIMIT];

    int len = 0;
    for (int i = 0; i < length && i < limit && len < LIMIT; ++i) {
        int nb = snprintf(buf + len, LIMIT - len, "%02x", (uint8_t)str[i]);
        if (nb <= 0 || nb >= LIMIT - len) {
            break;
        }
        len += nb;

        // Only append seperator and newline when not last byte.
        if (i < length - 1 && i < limit - 1 && len < LIMIT) {
            if (seperator) {
                buf[len++] = seperator;
            }

            if (newline && line_limit && i > 0 && ((i + 1) % line_limit) == 0) {
                buf[len++] = newline;
            }
        }
    }

    // Empty string.
    if (len <= 0) {
        return "";
    }

    // If overflow, cut the trailing newline.
    if (newline && len >= LIMIT - 2 && buf[len - 1] == newline) {
        len--;
    }

    // If overflow, cut the trailing seperator.
    if (seperator && len >= LIMIT - 3 && buf[len - 1] == seperator) {
        len--;
    }

    return string(buf, len);
}

bool srs_bytes_equal(void *pa, void *pb, int size)
{
    uint8_t *a = (uint8_t *)pa;
    uint8_t *b = (uint8_t *)pb;

    if (!a && !b) {
        return true;
    }

    if (!a || !b) {
        return false;
    }

    for (int i = 0; i < size; i++) {
        if (a[i] != b[i]) {
            return false;
        }
    }

    return true;
}

SrsPath::SrsPath()
{
}

SrsPath::~SrsPath()
{
}

bool SrsPath::exists(std::string path)
{
    struct stat st;

    // stat current dir, if exists, return error.
    if (stat(path.c_str(), &st) == 0) {
        return true;
    }

    return false;
}

srs_error_t SrsPath::unlink(std::string path)
{
    // Define the directories that are forbidden to delete.
    std::string forbidden[] = {
        "./",
        "../",
        "/",
        "/bin",
        "/boot",
        "/dev",
        "/etc",
        "/home",
        "/lib",
        "/lib32",
        "/lib64",
        "/libx32",
        "/lost+found",
        "/media",
        "/mnt",
        "/opt",
        "/proc",
        "/root",
        "/run",
        "/sbin",
        "/srv",
        "/sys",
        "/tmp",
        "/usr",
        "/var",
    };
    for (int i = 0; i < (int)(sizeof(forbidden) / sizeof(string)); i++) {
        if (path == forbidden[i]) {
            return srs_error_new(ERROR_SYSTEM_FILE_UNLINK, "unlink forbidden %s", path.c_str());
        }
    }

    if (::unlink(path.c_str()) < 0) {
        return srs_error_new(ERROR_SYSTEM_FILE_UNLINK, "unlink %s", path.c_str());
    }

    return srs_success;
}

std::string SrsPath::filepath_dir(std::string path)
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

string SrsPath::filepath_base(string path)
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

string SrsPath::filepath_filename(string path)
{
    std::string filename = path;
    size_t pos = string::npos;

    if ((pos = filename.rfind(".")) != string::npos) {
        return filename.substr(0, pos);
    }

    return filename;
}

string SrsPath::filepath_ext(string path)
{
    size_t pos = string::npos;

    if ((pos = path.rfind(".")) != string::npos) {
        return path.substr(pos);
    }

    return "";
}

srs_error_t SrsPath::mkdir_all(string dir)
{
    int ret = srs_do_create_dir_recursively(dir);

    if (ret == ERROR_SYSTEM_DIR_EXISTS || ret == ERROR_SUCCESS) {
        return srs_success;
    }

    return srs_error_new(ret, "create dir %s", dir.c_str());
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

char *srs_hex_encode_to_string(char *des, const u_int8_t *src, int len)
{
    if (src == NULL || len == 0 || des == NULL) {
        return NULL;
    }

    const char *hex_table = "0123456789ABCDEF";

    for (int i = 0; i < len; i++) {
        des[i * 2] = hex_table[src[i] >> 4];
        des[i * 2 + 1] = hex_table[src[i] & 0x0F];
    }

    return des;
}

char *srs_hex_encode_to_string_lowercase(char *des, const u_int8_t *src, int len)
{
    if (src == NULL || len == 0 || des == NULL) {
        return NULL;
    }

    const char *hex_table = "0123456789abcdef";

    for (int i = 0; i < len; i++) {
        des[i * 2] = hex_table[src[i] >> 4];
        des[i * 2 + 1] = hex_table[src[i] & 0x0F];
    }

    return des;
}

int srs_hex_decode_string(uint8_t *data, const char *p, int size)
{
    if (size <= 0 || (size % 2) == 1) {
        return -1;
    }

    for (int i = 0; i < (int)size / 2; i++) {
        uint8_t a = srs_from_hex_char(p[i * 2]);
        if (a == (uint8_t)-1) {
            return -1;
        }

        uint8_t b = srs_from_hex_char(p[i * 2 + 1]);
        if (b == (uint8_t)-1) {
            return -1;
        }

        data[i] = (a << 4) | b;
    }

    return size / 2;
}

SrsRand::SrsRand()
{
}

SrsRand::~SrsRand()
{
}

void SrsRand::gen_bytes(char *bytes, int size)
{
    for (int i = 0; i < size; i++) {
        // the common value in [0x0f, 0xf0]
        bytes[i] = 0x0f + (integer() % (256 - 0x0f - 0x0f));
    }
}

std::string SrsRand::gen_str(int len)
{
    static string random_table = "01234567890123456789012345678901234567890123456789abcdefghijklmnopqrstuvwxyz";

    string ret;
    ret.reserve(len);
    for (int i = 0; i < len; ++i) {
        ret.append(1, random_table[integer() % random_table.size()]);
    }

    return ret;
}

long SrsRand::integer()
{
    static bool _random_initialized = false;
    if (!_random_initialized) {
        _random_initialized = true;
        ::srandom((unsigned long)(srs_time_now_realtime() | (::getpid() << 13)));
    }

    return random();
}

long SrsRand::integer(long min, long max)
{
    return min + (integer() % (max - min + 1));
}

bool srs_is_digit_number(string str)
{
    if (str.empty()) {
        return false;
    }

    const char *p = str.c_str();
    const char *p_end = str.data() + str.length();
    for (; p < p_end; p++) {
        if (*p != '0') {
            break;
        }
    }
    if (p == p_end) {
        return true;
    }

    int64_t v = ::atoll(p);
    int64_t powv = (int64_t)pow(10, p_end - p - 1);
    return v / powv >= 1 && v / powv <= 9;
}

srs_error_t srs_io_readall(ISrsReader *in, std::string &content)
{
    srs_error_t err = srs_success;

    const int SRS_HTTP_READ_CACHE_BYTES = 4096;

    // Cache to read, it might cause coroutine switch, so we use local cache here.
    SrsUniquePtr<char[]> buf(new char[SRS_HTTP_READ_CACHE_BYTES]);

    // Whatever, read util EOF.
    while (true) {
        ssize_t nb_read = 0;
        if ((err = in->read(buf.get(), SRS_HTTP_READ_CACHE_BYTES, &nb_read)) != srs_success) {
            int code = srs_error_code(err);
            if (code == ERROR_SYSTEM_FILE_EOF || code == ERROR_HTTP_RESPONSE_EOF || code == ERROR_HTTP_REQUEST_EOF || code == ERROR_HTTP_STREAM_EOF) {
                srs_freep(err);
                return err;
            }
            return srs_error_wrap(err, "read body");
        }

        if (nb_read > 0) {
            content.append(buf.get(), nb_read);
        }
    }

    return err;
}

void srs_net_split_hostport(string hostport, string &host, int &port)
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

void srs_net_split_for_listener(string hostport, string &ip, int &port)
{
    const size_t pos = hostport.rfind(":"); // Look for ":" from the end, to work with IPv6.
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
        ip = srs_net_address_any();
        port = ::atoi(hostport.c_str());
    }
}

string srs_net_address_any()
{
    bool ipv4_active = false;
    bool ipv6_active = false;

    if (true) {
        int fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (fd != -1) {
            ipv4_active = true;
            close(fd);
        }
    }
    if (true) {
        int fd = socket(AF_INET6, SOCK_DGRAM, 0);
        if (fd != -1) {
            ipv6_active = true;
            close(fd);
        }
    }

    if (ipv6_active && !ipv4_active) {
        return SRS_CONSTS_LOOPBACK6;
    }
    return SRS_CONSTS_LOOPBACK;
}

bool srs_net_is_valid_ip(string ip)
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

bool srs_net_is_valid_endpoint(std::string endpoint)
{
    if (endpoint.empty()) {
        return false;
    }

    string host;
    int port = 0;
    srs_net_split_for_listener(endpoint, host, port);

    if (!srs_net_is_valid_ip(host)) {
        return false;
    }

    if (port <= 0) {
        return false;
    }

    return true;
}

bool srs_net_is_ipv4(string domain)
{
    for (int i = 0; i < (int)domain.length(); i++) {
        char ch = domain.at(i);
        if (ch == '.') {
            continue;
        }
        if (ch >= '0' && ch <= '9') {
            continue;
        }

        return false;
    }

    return true;
}

uint32_t srs_net_ipv4_to_integer(string ip)
{
    uint32_t addr = 0;
    if (inet_pton(AF_INET, ip.c_str(), &addr) <= 0) {
        return 0;
    }

    return ntohl(addr);
}

bool srs_net_ipv4_within_mask(string ip, string network, string mask)
{
    uint32_t ip_addr = srs_net_ipv4_to_integer(ip);
    uint32_t mask_addr = srs_net_ipv4_to_integer(mask);
    uint32_t network_addr = srs_net_ipv4_to_integer(network);

    return (ip_addr & mask_addr) == (network_addr & mask_addr);
}

static struct CIDR_VALUE {
    size_t length;
    std::string mask;
} CIDR_VALUES[32] = {
    {1, "128.0.0.0"},
    {2, "192.0.0.0"},
    {3, "224.0.0.0"},
    {4, "240.0.0.0"},
    {5, "248.0.0.0"},
    {6, "252.0.0.0"},
    {7, "254.0.0.0"},
    {8, "255.0.0.0"},
    {9, "255.128.0.0"},
    {10, "255.192.0.0"},
    {11, "255.224.0.0"},
    {12, "255.240.0.0"},
    {13, "255.248.0.0"},
    {14, "255.252.0.0"},
    {15, "255.254.0.0"},
    {16, "255.255.0.0"},
    {17, "255.255.128.0"},
    {18, "255.255.192.0"},
    {19, "255.255.224.0"},
    {20, "255.255.240.0"},
    {21, "255.255.248.0"},
    {22, "255.255.252.0"},
    {23, "255.255.254.0"},
    {24, "255.255.255.0"},
    {25, "255.255.255.128"},
    {26, "255.255.255.192"},
    {27, "255.255.255.224"},
    {28, "255.255.255.240"},
    {29, "255.255.255.248"},
    {30, "255.255.255.252"},
    {31, "255.255.255.254"},
    {32, "255.255.255.255"},
};

string srs_net_get_cidr_mask(string network_address)
{
    string delimiter = "/";

    size_t delimiter_position = network_address.find(delimiter);
    if (delimiter_position == string::npos) {
        // Even if it does not have "/N", it can be a valid IP, by default "/32".
        if (srs_net_is_ipv4(network_address)) {
            return CIDR_VALUES[32 - 1].mask;
        }
        return "";
    }

    // Change here to include IPv6 support.
    string is_ipv4_address = network_address.substr(0, delimiter_position);
    if (!srs_net_is_ipv4(is_ipv4_address)) {
        return "";
    }

    size_t cidr_length_position = delimiter_position + delimiter.length();
    if (cidr_length_position >= network_address.length()) {
        return "";
    }

    string cidr_length = network_address.substr(cidr_length_position, network_address.length());
    if (cidr_length.length() <= 0) {
        return "";
    }

    size_t cidr_length_num = 31;
    try {
        cidr_length_num = atoi(cidr_length.c_str());
        if (cidr_length_num <= 0) {
            return "";
        }
    } catch (...) {
        return "";
    }

    return CIDR_VALUES[cidr_length_num - 1].mask;
}

string srs_net_get_cidr_ipv4(string network_address)
{
    string delimiter = "/";

    size_t delimiter_position = network_address.find(delimiter);
    if (delimiter_position == string::npos) {
        // Even if it does not have "/N", it can be a valid IP, by default "/32".
        if (srs_net_is_ipv4(network_address)) {
            return network_address;
        }
        return "";
    }

    // Change here to include IPv6 support.
    string ipv4_address = network_address.substr(0, delimiter_position);
    if (!srs_net_is_ipv4(ipv4_address)) {
        return "";
    }

    size_t cidr_length_position = delimiter_position + delimiter.length();
    if (cidr_length_position >= network_address.length()) {
        return "";
    }

    string cidr_length = network_address.substr(cidr_length_position, network_address.length());
    if (cidr_length.length() <= 0) {
        return "";
    }

    try {
        size_t cidr_length_num = atoi(cidr_length.c_str());
        if (cidr_length_num <= 0) {
            return "";
        }
    } catch (...) {
        return "";
    }

    return ipv4_address;
}

bool srs_net_url_is_http(string url)
{
    return srs_strings_starts_with(url, "http://", "https://");
}

bool srs_net_url_is_rtmp(string url)
{
    return srs_strings_starts_with(url, "rtmp://");
}
