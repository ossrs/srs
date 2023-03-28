//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#include <srs_protocol_utility.hpp>

#ifndef _WIN32
#include <unistd.h>
#endif

#include <arpa/inet.h>
#include <stdlib.h>
#include <sstream>
using namespace std;

#include <srs_kernel_log.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_kernel_buffer.hpp>
#include <srs_protocol_rtmp_stack.hpp>
#include <srs_kernel_codec.hpp>
#include <srs_kernel_consts.hpp>
#include <srs_protocol_rtmp_stack.hpp>
#include <srs_protocol_io.hpp>

#include <unistd.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <math.h>
#include <stdlib.h>
#include <map>
#include <sstream>
using namespace std;

#include <srs_protocol_st.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_consts.hpp>
#include <srs_kernel_log.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_http_stack.hpp>
#include <srs_core_autofree.hpp>

void srs_discovery_tc_url(string tcUrl, string& schema, string& host, string& vhost, string& app, string& stream, int& port, string& param)
{
    // For compatibility, transform
    //      rtmp://ip/app...vhost...VHOST/stream
    // to typical format:
    //      rtmp://ip/app?vhost=VHOST/stream
    string fullUrl = srs_string_replace(tcUrl, "...vhost...", "?vhost=");

    // Standard URL is:
    //      rtmp://ip/app/app2/stream?k=v
    // Where after last slash is stream.
    fullUrl += stream.empty() ? "/" : (stream.at(0) == '/' ? stream : "/" + stream);
    fullUrl += param.empty() ? "" : (param.at(0) == '?' ? param : "?" + param);

    // First, we covert the FMLE URL to standard URL:
    //      rtmp://ip/app/app2?k=v/stream , or:
    //      rtmp://ip/app/app2#k=v/stream
    size_t pos_query = fullUrl.find_first_of("?#");
    size_t pos_rslash = fullUrl.rfind("/");
    if (pos_rslash != string::npos && pos_query != string::npos && pos_query < pos_rslash) {
        fullUrl = fullUrl.substr(0, pos_query) // rtmp://ip/app/app2
                  + fullUrl.substr(pos_rslash) // /stream
                  + fullUrl.substr(pos_query, pos_rslash - pos_query); // ?k=v
    }

    // Remove the _definst_ of FMLE URL.
    if (fullUrl.find("/_definst_") != string::npos) {
        fullUrl = srs_string_replace(fullUrl, "/_definst_", "");
    }

    // Parse the standard URL.
    SrsHttpUri uri;
    srs_error_t err = srs_success;
    if ((err = uri.initialize(fullUrl)) != srs_success) {
        srs_warn("Ignore parse url=%s err %s", fullUrl.c_str(), srs_error_desc(err).c_str());
        srs_freep(err);
        return;
    }

    schema = uri.get_schema();
    host = uri.get_host();
    port = uri.get_port();
    stream = srs_path_basename(uri.get_path());
    param = uri.get_query().empty() ? "" : "?" + uri.get_query();
    param += uri.get_fragment().empty() ? "" : "#" + uri.get_fragment();

    // Parse app without the prefix slash.
    app = srs_path_dirname(uri.get_path());
    if (!app.empty() && app.at(0) == '/') app = app.substr(1);
    if (app.empty()) app = SRS_CONSTS_RTMP_DEFAULT_APP;

    // Try to parse vhost from query, or use host if not specified.
    string vhost_in_query = uri.get_query_by_key("vhost");
    if (vhost_in_query.empty()) vhost_in_query = uri.get_query_by_key("domain");
    if (!vhost_in_query.empty() && vhost_in_query != SRS_CONSTS_RTMP_DEFAULT_VHOST) vhost = vhost_in_query;
    if (vhost.empty()) vhost = host;

    // Only one param, the default vhost, clear it.
    if (param.find("&") == string::npos && vhost_in_query == SRS_CONSTS_RTMP_DEFAULT_VHOST) {
        param = "";
    }
}

void srs_guess_stream_by_app(string& app, string& param, string& stream)
{
    size_t pos = std::string::npos;

    // Extract stream from app, if contains slash.
    if ((pos = app.find("/")) != std::string::npos) {
        stream = app.substr(pos + 1);
        app = app.substr(0, pos);

        if ((pos = stream.find("?")) != std::string::npos) {
            param = stream.substr(pos);
            stream = stream.substr(0, pos);
        }
        return;
    }

    // Extract stream from param, if contains slash.
    if ((pos = param.find("/")) != std::string::npos) {
        stream = param.substr(pos + 1);
        param = param.substr(0, pos);
    }
}

void srs_parse_query_string(string q, map<string,string>& query)
{
    // query string flags.
    static vector<string> flags;
    if (flags.empty()) {
        flags.push_back("=");
        flags.push_back(",");
        flags.push_back("&&");
        flags.push_back("&");
        flags.push_back(";");
    }
    
    vector<string> kvs = srs_string_split(q, flags);
    for (int i = 0; i < (int)kvs.size(); i+=2) {
        string k = kvs.at(i);
        string v = (i < (int)kvs.size() - 1)? kvs.at(i+1):"";
        
        query[k] = v;
    }
}

void srs_random_generate(char* bytes, int size)
{
    for (int i = 0; i < size; i++) {
        // the common value in [0x0f, 0xf0]
        bytes[i] = 0x0f + (srs_random() % (256 - 0x0f - 0x0f));
    }
}

std::string srs_random_str(int len)
{
    static string random_table = "01234567890123456789012345678901234567890123456789abcdefghijklmnopqrstuvwxyz";

    string ret;
    ret.reserve(len);
    for (int i = 0; i < len; ++i) {
        ret.append(1, random_table[srs_random() % random_table.size()]);
    }

    return ret;
}

long srs_random()
{
    static bool _random_initialized = false;
    if (!_random_initialized) {
        _random_initialized = true;
        ::srandom((unsigned long)(srs_update_system_time() | (::getpid()<<13)));
    }

    return random();
}

string srs_generate_tc_url(string schema, string host, string vhost, string app, int port)
{
    string tcUrl = schema + "://";
    
    if (vhost == SRS_CONSTS_RTMP_DEFAULT_VHOST) {
        tcUrl += host.empty() ? SRS_CONSTS_RTMP_DEFAULT_VHOST : host;
    } else {
        tcUrl += vhost;
    }
    
    if (port && port != SRS_CONSTS_RTMP_DEFAULT_PORT) {
        tcUrl += ":" + srs_int2str(port);
    }
    
    tcUrl += "/" + app;
    
    return tcUrl;
}

string srs_generate_stream_with_query(string host, string vhost, string stream, string param, bool with_vhost)
{
    string url = stream;
    string query = param;

    // If no vhost in param, try to append one.
    string guessVhost;
    if (query.find("vhost=") == string::npos) {
        if (vhost != SRS_CONSTS_RTMP_DEFAULT_VHOST) {
            guessVhost = vhost;
        } else if (!srs_is_ipv4(host)) {
            guessVhost = host;
        }
    }

    // Well, if vhost exists, always append in query string.
    if (!guessVhost.empty() && query.find("vhost=") == string::npos) {
        query += "&vhost=" + guessVhost;
    }

    // If not pass in query, remove it.
    if (!with_vhost) {
        size_t pos = query.find("&vhost=");
        if (pos == string::npos) {
            pos = query.find("vhost=");
        }

        size_t end = query.find("&", pos + 1);
        if (end == string::npos) {
            end = query.length();
        }

        if (pos != string::npos && end != string::npos && end > pos) {
            query = query.substr(0, pos) + query.substr(end);
        }
    }
    
    // Remove the start & and ? when param is empty.
    query = srs_string_trim_start(query, "&?");

    // Prefix query with ?.
    if (!query.empty() && !srs_string_starts_with(query, "?")) {
        url += "?";
    }
    
    // Append query to url.
    if (!query.empty()) {
        url += query;
    }
    
    return url;
}

template<typename T>
srs_error_t srs_do_rtmp_create_msg(char type, uint32_t timestamp, char* data, int size, int stream_id, T** ppmsg)
{
    srs_error_t err = srs_success;
    
    *ppmsg = NULL;
    T* msg = NULL;
    
    if (type == SrsFrameTypeAudio) {
        SrsMessageHeader header;
        header.initialize_audio(size, timestamp, stream_id);
        
        msg = new T();
        if ((err = msg->create(&header, data, size)) != srs_success) {
            srs_freep(msg);
            return srs_error_wrap(err, "create message");
        }
    } else if (type == SrsFrameTypeVideo) {
        SrsMessageHeader header;
        header.initialize_video(size, timestamp, stream_id);
        
        msg = new T();
        if ((err = msg->create(&header, data, size)) != srs_success) {
            srs_freep(msg);
            return srs_error_wrap(err, "create message");
        }
    } else if (type == SrsFrameTypeScript) {
        SrsMessageHeader header;
        header.initialize_amf0_script(size, stream_id);
        
        msg = new T();
        if ((err = msg->create(&header, data, size)) != srs_success) {
            srs_freep(msg);
            return srs_error_wrap(err, "create message");
        }
    } else {
        return srs_error_new(ERROR_STREAM_CASTER_FLV_TAG, "unknown tag=%#x", (uint8_t)type);
    }
    
    *ppmsg = msg;
    
    return err;
}

srs_error_t srs_rtmp_create_msg(char type, uint32_t timestamp, char* data, int size, int stream_id, SrsSharedPtrMessage** ppmsg)
{
    srs_error_t err = srs_success;
    
    // only when failed, we must free the data.
    if ((err = srs_do_rtmp_create_msg(type, timestamp, data, size, stream_id, ppmsg)) != srs_success) {
        srs_freepa(data);
        return srs_error_wrap(err, "create message");
    }
    
    return err;
}

srs_error_t srs_rtmp_create_msg(char type, uint32_t timestamp, char* data, int size, int stream_id, SrsCommonMessage** ppmsg)
{
    srs_error_t err = srs_success;
    
    // only when failed, we must free the data.
    if ((err = srs_do_rtmp_create_msg(type, timestamp, data, size, stream_id, ppmsg)) != srs_success) {
        srs_freepa(data);
        return srs_error_wrap(err, "create message");
    }
    
    return err;
}

string srs_generate_stream_url(string vhost, string app, string stream)
{
    std::string url = "";
    
    if (SRS_CONSTS_RTMP_DEFAULT_VHOST != vhost){
        url += vhost;
    }
    url += "/" + app;
    // Note that we ignore any extension.
    url += "/" + srs_path_filename(stream);
    
    return url;
}

void srs_parse_rtmp_url(string url, string& tcUrl, string& stream)
{
    size_t pos;
    
    if ((pos = url.rfind("/")) != string::npos) {
        stream = url.substr(pos + 1);
        tcUrl = url.substr(0, pos);
    } else {
        tcUrl = url;
    }
}

string srs_generate_rtmp_url(string server, int port, string host, string vhost, string app, string stream, string param)
{
    string tcUrl = "rtmp://" + server + ":" + srs_int2str(port) + "/"  + app;
    string streamWithQuery = srs_generate_stream_with_query(host, vhost, stream, param);
    string url = tcUrl + "/" + streamWithQuery;
    return url;
}

srs_error_t srs_write_large_iovs(ISrsProtocolReadWriter* skt, iovec* iovs, int size, ssize_t* pnwrite)
{
    srs_error_t err = srs_success;
    
    // the limits of writev iovs.
#ifndef _WIN32
    // for linux, generally it's 1024.
    static int limits = (int)sysconf(_SC_IOV_MAX);
#else
    static int limits = 1024;
#endif
    
    // send in a time.
    if (size <= limits) {
        if ((err = skt->writev(iovs, size, pnwrite)) != srs_success) {
            return srs_error_wrap(err, "writev");
        }
        return err;
    }
   
    // send in multiple times.
    int cur_iov = 0;
    ssize_t nwrite = 0;
    while (cur_iov < size) {
        int cur_count = srs_min(limits, size - cur_iov);
        if ((err = skt->writev(iovs + cur_iov, cur_count, &nwrite)) != srs_success) {
            return srs_error_wrap(err, "writev");
        }
        cur_iov += cur_count;
        if (pnwrite) {
            *pnwrite += nwrite;
        }
    }
    
    return err;
}

bool srs_is_ipv4(string domain)
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

uint32_t srs_ipv4_to_num(string ip) {
    uint32_t addr = 0;
    if (inet_pton(AF_INET, ip.c_str(), &addr) <= 0) {
        return 0;
    }

    return ntohl(addr);
}

bool srs_ipv4_within_mask(string ip, string network, string mask) {
    uint32_t ip_addr = srs_ipv4_to_num(ip);
    uint32_t mask_addr = srs_ipv4_to_num(mask);
    uint32_t network_addr = srs_ipv4_to_num(network);

    return (ip_addr & mask_addr) == (network_addr & mask_addr);
}

static struct CIDR_VALUE {
    size_t length;
    std::string mask;
} CIDR_VALUES[32] = {
    { 1,  "128.0.0.0" },
    { 2,  "192.0.0.0" },
    { 3,  "224.0.0.0" },
    { 4,  "240.0.0.0" },
    { 5,  "248.0.0.0" },
    { 6,  "252.0.0.0" },
    { 7,  "254.0.0.0" },
    { 8,  "255.0.0.0" },
    { 9,  "255.128.0.0" },
    { 10, "255.192.0.0" },
    { 11, "255.224.0.0" },
    { 12, "255.240.0.0" },
    { 13, "255.248.0.0" },
    { 14, "255.252.0.0" },
    { 15, "255.254.0.0" },
    { 16, "255.255.0.0" },
    { 17, "255.255.128.0" },
    { 18, "255.255.192.0" },
    { 19, "255.255.224.0" },
    { 20, "255.255.240.0" },
    { 21, "255.255.248.0" },
    { 22, "255.255.252.0" },
    { 23, "255.255.254.0" },
    { 24, "255.255.255.0" },
    { 25, "255.255.255.128" },
    { 26, "255.255.255.192" },
    { 27, "255.255.255.224" },
    { 28, "255.255.255.240" },
    { 29, "255.255.255.248" },
    { 30, "255.255.255.252" },
    { 31, "255.255.255.254" },
    { 32, "255.255.255.255" },
};

string srs_get_cidr_mask(string network_address) {
    string delimiter = "/";

    size_t delimiter_position = network_address.find(delimiter);
    if (delimiter_position == string::npos) {
        // Even if it does not have "/N", it can be a valid IP, by default "/32".
        if (srs_is_ipv4(network_address)) {
            return CIDR_VALUES[32-1].mask;
        }
        return "";
    }

    // Change here to include IPv6 support.
    string is_ipv4_address = network_address.substr(0, delimiter_position);
    if (!srs_is_ipv4(is_ipv4_address)) {
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

    return CIDR_VALUES[cidr_length_num-1].mask;
}

string srs_get_cidr_ipv4(string network_address) {
    string delimiter = "/";

    size_t delimiter_position = network_address.find(delimiter);
    if (delimiter_position == string::npos) {
        // Even if it does not have "/N", it can be a valid IP, by default "/32".
        if (srs_is_ipv4(network_address)) {
            return network_address;
        }
        return "";
    }

    // Change here to include IPv6 support.
    string ipv4_address = network_address.substr(0, delimiter_position);
    if (!srs_is_ipv4(ipv4_address)) {
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

bool srs_string_is_http(string url)
{
    return srs_string_starts_with(url, "http://", "https://");
}

bool srs_string_is_rtmp(string url)
{
    return srs_string_starts_with(url, "rtmp://");
}

bool srs_is_digit_number(string str)
{
    if (str.empty()) {
        return false;
    }

    const char* p = str.c_str();
    const char* p_end = str.data() + str.length();
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
    return  v / powv >= 1 && v / powv <= 9;
}

// we detect all network device as internet or intranet device, by its ip address.
//      key is device name, for instance, eth0
//      value is whether internet, for instance, true.
static std::map<std::string, bool> _srs_device_ifs;

bool srs_net_device_is_internet(string ifname)
{
    srs_info("check ifname=%s", ifname.c_str());

    if (_srs_device_ifs.find(ifname) == _srs_device_ifs.end()) {
        return false;
    }
    return _srs_device_ifs[ifname];
}

bool srs_net_device_is_internet(const sockaddr* addr)
{
    if(addr->sa_family == AF_INET) {
        const in_addr inaddr = ((sockaddr_in*)addr)->sin_addr;
        const uint32_t addr_h = ntohl(inaddr.s_addr);

        // lo, 127.0.0.0-127.0.0.1
        if (addr_h >= 0x7f000000 && addr_h <= 0x7f000001) {
            return false;
        }

        // Class A 10.0.0.0-10.255.255.255
        if (addr_h >= 0x0a000000 && addr_h <= 0x0affffff) {
            return false;
        }

        // Class B 172.16.0.0-172.31.255.255
        if (addr_h >= 0xac100000 && addr_h <= 0xac1fffff) {
            return false;
        }

        // Class C 192.168.0.0-192.168.255.255
        if (addr_h >= 0xc0a80000 && addr_h <= 0xc0a8ffff) {
            return false;
        }
    } else if(addr->sa_family == AF_INET6) {
        const sockaddr_in6* a6 = (const sockaddr_in6*)addr;

        // IPv6 loopback is ::1
        if (IN6_IS_ADDR_LOOPBACK(&a6->sin6_addr)) {
            return false;
        }

        // IPv6 unspecified is ::
        if (IN6_IS_ADDR_UNSPECIFIED(&a6->sin6_addr)) {
            return false;
        }

        // From IPv4, you might know APIPA (Automatic Private IP Addressing) or AutoNet.
        // Whenever automatic IP configuration through DHCP fails.
        // The prefix of a site-local address is FE80::/10.
        if (IN6_IS_ADDR_LINKLOCAL(&a6->sin6_addr)) {
            return false;
        }

        // Site-local addresses are equivalent to private IP addresses in IPv4.
        // The prefix of a site-local address is FEC0::/10.
        // https://4sysops.com/archives/ipv6-tutorial-part-6-site-local-addresses-and-link-local-addresses/
        if (IN6_IS_ADDR_SITELOCAL(&a6->sin6_addr)) {
            return false;
        }

        // Others.
        if (IN6_IS_ADDR_MULTICAST(&a6->sin6_addr)) {
            return false;
        }
        if (IN6_IS_ADDR_MC_NODELOCAL(&a6->sin6_addr)) {
            return false;
        }
        if (IN6_IS_ADDR_MC_LINKLOCAL(&a6->sin6_addr)) {
            return false;
        }
        if (IN6_IS_ADDR_MC_SITELOCAL(&a6->sin6_addr)) {
            return false;
        }
        if (IN6_IS_ADDR_MC_ORGLOCAL(&a6->sin6_addr)) {
            return false;
        }
        if (IN6_IS_ADDR_MC_GLOBAL(&a6->sin6_addr)) {
            return false;
        }
    }

    return true;
}

vector<SrsIPAddress*> _srs_system_ips;
void srs_free_global_system_ips()
{
    vector<SrsIPAddress*>& ips = _srs_system_ips;

    // Release previous IPs.
    for (int i = 0; i < (int)ips.size(); i++) {
        SrsIPAddress* ip = ips[i];
        srs_freep(ip);
    }
    ips.clear();
}

void discover_network_iface(ifaddrs* cur, vector<SrsIPAddress*>& ips, stringstream& ss0, stringstream& ss1, bool ipv6, bool loopback)
{
    char saddr[64];
    char* h = (char*)saddr;
    socklen_t nbh = (socklen_t)sizeof(saddr);
    const int r0 = getnameinfo(cur->ifa_addr, sizeof(sockaddr_storage), h, nbh, NULL, 0, NI_NUMERICHOST);
    if(r0) {
        srs_warn("convert local ip failed: %s", gai_strerror(r0));
        return;
    }

    std::string ip(saddr, strlen(saddr));
    ss0 << ", iface[" << (int)ips.size() << "] " << cur->ifa_name << " " << (ipv6? "ipv6":"ipv4")
        << " 0x" << std::hex << cur->ifa_flags  << std::dec << " " << ip;

    SrsIPAddress* ip_address = new SrsIPAddress();
    ip_address->ip = ip;
    ip_address->is_ipv4 = !ipv6;
    ip_address->is_loopback = loopback;
    ip_address->ifname = cur->ifa_name;
    ip_address->is_internet = srs_net_device_is_internet(cur->ifa_addr);
    ips.push_back(ip_address);

    // set the device internet status.
    if (!ip_address->is_internet) {
        ss1 << ", intranet ";
        _srs_device_ifs[cur->ifa_name] = false;
    } else {
        ss1 << ", internet ";
        _srs_device_ifs[cur->ifa_name] = true;
    }
    ss1 << cur->ifa_name << " " << ip;
}

void retrieve_local_ips()
{
    // Release previous IPs.
    srs_free_global_system_ips();

    vector<SrsIPAddress*>& ips = _srs_system_ips;

    // Get the addresses.
    ifaddrs* ifap;
    if (getifaddrs(&ifap) == -1) {
        srs_warn("retrieve local ips, getifaddrs failed.");
        return;
    }

    stringstream ss0;
    ss0 << "ips";

    stringstream ss1;
    ss1 << "devices";

    // Discover IPv4 first.
    for (ifaddrs* p = ifap; p ; p = p->ifa_next) {
        ifaddrs* cur = p;

        // Ignore if no address for this interface.
        // @see https://github.com/ossrs/srs/issues/1087#issuecomment-408847115
        if (!cur->ifa_addr) {
            continue;
        }

        // retrieve IP address, ignore the tun0 network device, whose addr is NULL.
        // @see: https://github.com/ossrs/srs/issues/141
        bool ipv4 = (cur->ifa_addr->sa_family == AF_INET);
        bool ready = (cur->ifa_flags & IFF_UP) && (cur->ifa_flags & IFF_RUNNING);
        // Ignore IFF_PROMISC(Interface is in promiscuous mode), which may be set by Wireshark.
        bool ignored = (!cur->ifa_addr) || (cur->ifa_flags & IFF_LOOPBACK) || (cur->ifa_flags & IFF_POINTOPOINT);
        bool loopback = (cur->ifa_flags & IFF_LOOPBACK);
        if (ipv4 && ready && !ignored) {
            discover_network_iface(cur, ips, ss0, ss1, false, loopback);
        }
    }

    // Then, discover IPv6 addresses.
    for (ifaddrs* p = ifap; p ; p = p->ifa_next) {
        ifaddrs* cur = p;

        // Ignore if no address for this interface.
        // @see https://github.com/ossrs/srs/issues/1087#issuecomment-408847115
        if (!cur->ifa_addr) {
            continue;
        }

        // retrieve IP address, ignore the tun0 network device, whose addr is NULL.
        // @see: https://github.com/ossrs/srs/issues/141
        bool ipv6 = (cur->ifa_addr->sa_family == AF_INET6);
        bool ready = (cur->ifa_flags & IFF_UP) && (cur->ifa_flags & IFF_RUNNING);
        bool ignored = (!cur->ifa_addr) || (cur->ifa_flags & IFF_POINTOPOINT) || (cur->ifa_flags & IFF_PROMISC) || (cur->ifa_flags & IFF_LOOPBACK);
        bool loopback = (cur->ifa_flags & IFF_LOOPBACK);
        if (ipv6 && ready && !ignored) {
            discover_network_iface(cur, ips, ss0, ss1, true, loopback);
        }
    }

    // If empty, disover IPv4 loopback.
    if (ips.empty()) {
        for (ifaddrs* p = ifap; p ; p = p->ifa_next) {
            ifaddrs* cur = p;

            // Ignore if no address for this interface.
            // @see https://github.com/ossrs/srs/issues/1087#issuecomment-408847115
            if (!cur->ifa_addr) {
                continue;
            }

            // retrieve IP address, ignore the tun0 network device, whose addr is NULL.
            // @see: https://github.com/ossrs/srs/issues/141
            bool ipv4 = (cur->ifa_addr->sa_family == AF_INET);
            bool ready = (cur->ifa_flags & IFF_UP) && (cur->ifa_flags & IFF_RUNNING);
            bool ignored = (!cur->ifa_addr) || (cur->ifa_flags & IFF_POINTOPOINT) || (cur->ifa_flags & IFF_PROMISC);
            bool loopback = (cur->ifa_flags & IFF_LOOPBACK);
            if (ipv4 && ready && !ignored) {
                discover_network_iface(cur, ips, ss0, ss1, false, loopback);
            }
        }
    }

    srs_trace("%s", ss0.str().c_str());
    srs_trace("%s", ss1.str().c_str());

    freeifaddrs(ifap);
}

vector<SrsIPAddress*>& srs_get_local_ips()
{
    if (_srs_system_ips.empty()) {
        retrieve_local_ips();
    }

    return _srs_system_ips;
}

std::string _public_internet_address;

string srs_get_public_internet_address(bool ipv4_only)
{
    if (!_public_internet_address.empty()) {
        return _public_internet_address;
    }

    std::vector<SrsIPAddress*>& ips = srs_get_local_ips();

    // find the best match public address.
    for (int i = 0; i < (int)ips.size(); i++) {
        SrsIPAddress* ip = ips[i];
        if (!ip->is_internet) {
            continue;
        }
        if (ipv4_only && !ip->is_ipv4) {
            continue;
        }

        srs_warn("use public address as ip: %s, ifname=%s", ip->ip.c_str(), ip->ifname.c_str());
        _public_internet_address = ip->ip;
        return ip->ip;
    }

    // no public address, use private address.
    for (int i = 0; i < (int)ips.size(); i++) {
        SrsIPAddress* ip = ips[i];
        if (ip->is_loopback) {
            continue;
        }
        if (ipv4_only && !ip->is_ipv4) {
            continue;
        }

        srs_warn("use private address as ip: %s, ifname=%s", ip->ip.c_str(), ip->ifname.c_str());
        _public_internet_address = ip->ip;
        return ip->ip;
    }

    // Finally, use first whatever kind of address.
    if (!ips.empty() && _public_internet_address.empty()) {
        SrsIPAddress* ip = ips[0];

        srs_warn("use first address as ip: %s, ifname=%s", ip->ip.c_str(), ip->ifname.c_str());
        _public_internet_address = ip->ip;
        return ip->ip;
    }

    return "";
}

string srs_get_original_ip(ISrsHttpMessage* r)
{
    SrsHttpHeader* h = r->header();

    string x_forwarded_for = h->get("X-Forwarded-For");
    if (!x_forwarded_for.empty()) {
        size_t pos = string::npos;
        if ((pos = x_forwarded_for.find(",")) == string::npos) {
            return x_forwarded_for;
        }
        return x_forwarded_for.substr(0, pos);
    }

    string x_real_ip = h->get("X-Real-IP");
    if (!x_real_ip.empty()) {
        size_t pos = string::npos;
        if ((pos = x_real_ip.find(":")) == string::npos) {
            return x_real_ip;
        }
        return x_real_ip.substr(0, pos);
    }

    return "";
}

std::string _srs_system_hostname;

string srs_get_system_hostname()
{
    if (!_srs_system_hostname.empty()) {
        return _srs_system_hostname;
    }

    char buf[256];
    if (-1 == gethostname(buf, sizeof(buf))) {
        srs_warn("gethostbyname fail");
        return "";
    }

    _srs_system_hostname = std::string(buf);
    return _srs_system_hostname;
}

srs_error_t srs_ioutil_read_all(ISrsReader* in, std::string& content)
{
    srs_error_t err = srs_success;

    // Cache to read, it might cause coroutine switch, so we use local cache here.
    char* buf = new char[SRS_HTTP_READ_CACHE_BYTES];
    SrsAutoFreeA(char, buf);

    // Whatever, read util EOF.
    while (true) {
        ssize_t nb_read = 0;
        if ((err = in->read(buf, SRS_HTTP_READ_CACHE_BYTES, &nb_read)) != srs_success) {
            int code = srs_error_code(err);
            if (code == ERROR_SYSTEM_FILE_EOF || code == ERROR_HTTP_RESPONSE_EOF || code == ERROR_HTTP_REQUEST_EOF
                || code == ERROR_HTTP_STREAM_EOF
            ) {
                srs_freep(err);
                return err;
            }
            return srs_error_wrap(err, "read body");
        }

        if (nb_read > 0) {
            content.append(buf, nb_read);
        }
    }

    return err;
}

#if defined(__linux__) || defined(SRS_OSX)
utsname* srs_get_system_uname_info()
{
    static utsname* system_info = NULL;

    if (system_info != NULL) {
        return system_info;
    }

    system_info = new utsname();
    memset(system_info, 0, sizeof(utsname));
    if (uname(system_info) < 0) {
        srs_warn("uname failed");
    }

    return system_info;
}
#endif
