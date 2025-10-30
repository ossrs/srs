//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_protocol_utility.hpp>

#include <unistd.h>

#include <algorithm>
#include <arpa/inet.h>
#include <sstream>
#include <stdlib.h>
using namespace std;

#include <srs_kernel_buffer.hpp>
#include <srs_kernel_codec.hpp>
#include <srs_kernel_consts.hpp>
#include <srs_kernel_log.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_io.hpp>
#include <srs_protocol_rtmp_stack.hpp>

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <limits.h>
#include <map>
#include <math.h>
#include <net/if.h>
#include <netdb.h>
#include <sstream>
#include <stdlib.h>
#include <unistd.h>
using namespace std;

#include <srs_core_autofree.hpp>
#include <srs_kernel_consts.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_factory.hpp>
#include <srs_kernel_log.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_http_stack.hpp>
#include <srs_protocol_st.hpp>

void srs_net_url_parse_tcurl(string tcUrl, string &schema, string &host, string &vhost, string &app, string &stream, int &port, string &param)
{
    // Build the full URL with stream and param if provided
    string fullUrl = tcUrl;
    fullUrl += stream.empty() ? "/" : (stream.at(0) == '/' ? stream : "/" + stream);
    fullUrl += param.empty() ? "" : (param.at(0) == '?' ? param : "?" + param);

    // For compatibility, transform legacy ...vhost... format
    //      rtmp://ip/app...vhost...VHOST/stream
    // to query parameter format:
    //      rtmp://ip/app?vhost=VHOST/stream
    fullUrl = srs_strings_replace(fullUrl, "...vhost...", "?vhost=");

    // Convert legacy RTMP URL format to standard format
    // Legacy: rtmp://ip/app/app2?vhost=xxx/stream
    // Standard: rtmp://ip/app/app2/stream?vhost=xxx
    fullUrl = srs_net_url_convert_legacy_rtmp_url(fullUrl);

    // Remove the _definst_ of FMLE URL.
    if (fullUrl.find("/_definst_") != string::npos) {
        fullUrl = srs_strings_replace(fullUrl, "/_definst_", "");
    }

    // Parse the standard URL using SrsHttpUri.
    SrsHttpUri uri;
    srs_error_t err = srs_success;
    if ((err = uri.initialize(fullUrl)) != srs_success) {
        srs_warn("Ignore parse url=%s err %s", fullUrl.c_str(), srs_error_desc(err).c_str());
        srs_freep(err);
        return;
    }

    // Extract basic URL components
    schema = uri.get_schema();
    host = uri.get_host();
    port = uri.get_port();
    SrsPath path;
    stream = path.filepath_base(uri.get_path());
    param = uri.get_query().empty() ? "" : "?" + uri.get_query();
    param += uri.get_fragment().empty() ? "" : "#" + uri.get_fragment();

    // Parse app without the prefix slash.
    app = path.filepath_dir(uri.get_path());
    if (!app.empty() && app.at(0) == '/')
        app = app.substr(1);
    if (app.empty())
        app = SRS_CONSTS_RTMP_DEFAULT_APP;

    // Discover vhost from query parameters, or use host if not specified.
    string vhost_in_query = uri.get_query_by_key("vhost");
    if (vhost_in_query.empty())
        vhost_in_query = uri.get_query_by_key("domain");
    if (!vhost_in_query.empty() && vhost_in_query != SRS_CONSTS_RTMP_DEFAULT_VHOST)
        vhost = vhost_in_query;
    if (vhost.empty())
        vhost = host;

    // Only one param, the default vhost, clear it.
    if (param.find("&") == string::npos && vhost_in_query == SRS_CONSTS_RTMP_DEFAULT_VHOST) {
        param = "";
    }
}

string srs_net_url_convert_legacy_rtmp_url(const string &url)
{
    // Check if this is a legacy RTMP URL format: rtmp://ip/app/app2?vhost=xxx/stream
    // We need to convert it to standard format: rtmp://ip/app/app2/stream?vhost=xxx

    // Find the query part starting with ?
    size_t query_pos = url.find('?');

    // Find the last slash in the URL
    size_t last_slash_pos = url.rfind('/');
    if (last_slash_pos == string::npos) {
        // No slash in URL, return as is
        return url;
    }

    // Check for normal legacy case: query exists and slash is after query
    if (query_pos != string::npos && last_slash_pos > query_pos) {
        // Normal legacy case: rtmp://ip/app/app2?vhost=xxx/stream
        string base_url = url.substr(0, query_pos);                            // rtmp://ip/app/app2
        string query_part = url.substr(query_pos, last_slash_pos - query_pos); // ?vhost=xxx
        string stream_part = url.substr(last_slash_pos);                       // /stream

        // Reconstruct as standard format: base_url + stream_part + query_part
        return base_url + stream_part + query_part;
    }

    // No conversion needed, return as is
    return url;
}

void srs_net_url_guess_stream(string &app, string &param, string &stream)
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

void srs_net_url_parse_query(string q, map<string, string> &query)
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

    vector<string> kvs = srs_strings_split(q, flags);
    for (int i = 0; i < (int)kvs.size(); i += 2) {
        string k = kvs.at(i);
        string v = (i < (int)kvs.size() - 1) ? kvs.at(i + 1) : "";

        query[k] = v;
    }
}

string srs_net_url_encode_tcurl(string schema, string host, string vhost, string app, int port)
{
    string tcUrl = schema + "://";

    if (vhost == SRS_CONSTS_RTMP_DEFAULT_VHOST) {
        tcUrl += host.empty() ? SRS_CONSTS_RTMP_DEFAULT_VHOST : host;
    } else {
        tcUrl += vhost;
    }

    if (port && port != SRS_CONSTS_RTMP_DEFAULT_PORT) {
        tcUrl += ":" + srs_strconv_format_int(port);
    }

    tcUrl += "/" + app;

    return tcUrl;
}

string srs_net_url_encode_stream(string host, string vhost, string stream, string param, bool with_vhost)
{
    string url = stream;
    string query = param;

    // If no vhost in param, try to append one.
    string guessVhost;
    if (query.find("vhost=") == string::npos) {
        if (vhost != SRS_CONSTS_RTMP_DEFAULT_VHOST) {
            guessVhost = vhost;
        } else if (!srs_net_is_ipv4(host)) {
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
    query = srs_strings_trim_start(query, "&?");

    // Prefix query with ?.
    if (!query.empty() && !srs_strings_starts_with(query, "?")) {
        url += "?";
    }

    // Append query to url.
    if (!query.empty()) {
        url += query;
    }

    return url;
}

string srs_net_url_encode_sid(string vhost, string app, string stream)
{
    std::string url = "";

    if (SRS_CONSTS_RTMP_DEFAULT_VHOST != vhost) {
        url += vhost;
    }
    url += "/" + app;

    // Strip only known streaming extensions, not arbitrary dots in stream names.
    // This fixes issue 4011 where stream names like "WavMain.exe_rooms_290" were
    // incorrectly truncated to "WavMain" because filepath_filename() stripped
    // everything after any dot.
    std::string stream_name = stream;
    size_t pos = stream_name.rfind(".");
    if (pos != string::npos) {
        std::string ext = stream_name.substr(pos);
        // Only strip known streaming extensions
        if (ext == ".flv" || ext == ".m3u8" || ext == ".mp4" || ext == ".ts") {
            stream_name = stream_name.substr(0, pos);
        }
    }
    url += "/" + stream_name;

    return url;
}

void srs_net_url_parse_rtmp_url(string url, string &tcUrl, string &stream)
{
    size_t pos;

    if ((pos = url.rfind("/")) != string::npos) {
        stream = url.substr(pos + 1);
        tcUrl = url.substr(0, pos);
    } else {
        tcUrl = url;
    }
}

string srs_net_url_encode_rtmp_url(string server, int port, string host, string vhost, string app, string stream, string param)
{
    string tcUrl = "rtmp://" + server + ":" + srs_strconv_format_int(port) + "/" + app;
    string streamWithQuery = srs_net_url_encode_stream(host, vhost, stream, param);
    string url = tcUrl + "/" + streamWithQuery;
    return url;
}

srs_error_t srs_do_rtmp_create_msg(char type, uint32_t timestamp, char *data, int size, int stream_id, SrsRtmpCommonMessage **ppmsg)
{
    srs_error_t err = srs_success;

    *ppmsg = NULL;
    SrsRtmpCommonMessage *msg = NULL;

    if (type == SrsFrameTypeAudio) {
        SrsMessageHeader header;
        header.initialize_audio(size, timestamp, stream_id);

        msg = new SrsRtmpCommonMessage();
        if ((err = msg->create(&header, data, size)) != srs_success) {
            srs_freep(msg);
            return srs_error_wrap(err, "create message");
        }
    } else if (type == SrsFrameTypeVideo) {
        SrsMessageHeader header;
        header.initialize_video(size, timestamp, stream_id);

        msg = new SrsRtmpCommonMessage();
        if ((err = msg->create(&header, data, size)) != srs_success) {
            srs_freep(msg);
            return srs_error_wrap(err, "create message");
        }
    } else if (type == SrsFrameTypeScript) {
        SrsMessageHeader header;
        header.initialize_amf0_script(size, stream_id);

        msg = new SrsRtmpCommonMessage();
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

srs_error_t srs_rtmp_create_msg(char type, uint32_t timestamp, char *data, int size, int stream_id, SrsRtmpCommonMessage **ppmsg)
{
    srs_error_t err = srs_success;

    // only when failed, we must free the data.
    if ((err = srs_do_rtmp_create_msg(type, timestamp, data, size, stream_id, ppmsg)) != srs_success) {
        srs_freepa(data);
        return srs_error_wrap(err, "create message");
    }

    return err;
}

ISrsProtocolUtility::ISrsProtocolUtility()
{
}

ISrsProtocolUtility::~ISrsProtocolUtility()
{
}

SrsProtocolUtility::SrsProtocolUtility()
{
}

SrsProtocolUtility::~SrsProtocolUtility()
{
}

srs_error_t SrsProtocolUtility::write_iovs(ISrsProtocolReadWriter *skt, iovec *iovs, int size, ssize_t *pnwrite)
{
    srs_error_t err = srs_success;

    // the limits of writev iovs.
    // for linux, generally it's 1024.
    static int limits = (int)sysconf(_SC_IOV_MAX);

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

// we detect all network device as internet or intranet device, by its ip address.
//      key is device name, for instance, eth0
//      value is whether internet, for instance, true.
static std::map<std::string, bool> _srs_device_ifs;

bool SrsProtocolUtility::is_internet(string ifname)
{
    srs_info("check ifname=%s", ifname.c_str());

    if (_srs_device_ifs.find(ifname) == _srs_device_ifs.end()) {
        return false;
    }
    return _srs_device_ifs[ifname];
}

bool SrsProtocolUtility::is_internet(const sockaddr *addr)
{
    if (addr->sa_family == AF_INET) {
        const in_addr inaddr = ((sockaddr_in *)addr)->sin_addr;
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
    } else if (addr->sa_family == AF_INET6) {
        const sockaddr_in6 *a6 = (const sockaddr_in6 *)addr;

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

        // LCOV_EXCL_START
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
        // LCOV_EXCL_STOP
    }

    return true;
}

void discover_network_iface(SrsProtocolUtility *utility, ifaddrs *cur, vector<SrsIPAddress *> &ips, stringstream &ss0, stringstream &ss1, bool ipv6, bool loopback)
{
    char saddr[64];
    char *h = (char *)saddr;
    socklen_t nbh = (socklen_t)sizeof(saddr);
    const int r0 = getnameinfo(cur->ifa_addr, sizeof(sockaddr_storage), h, nbh, NULL, 0, NI_NUMERICHOST);
    if (r0) {
        srs_warn("convert local ip failed: %s", gai_strerror(r0));
        return;
    }

    std::string ip(saddr, strlen(saddr));
    ss0 << ", iface[" << (int)ips.size() << "] " << cur->ifa_name << " " << (ipv6 ? "ipv6" : "ipv4")
        << " 0x" << std::hex << cur->ifa_flags << std::dec << " " << ip;

    SrsIPAddress *ip_address = new SrsIPAddress();
    ip_address->ip_ = ip;
    ip_address->is_ipv4_ = !ipv6;
    ip_address->is_loopback_ = loopback;
    ip_address->ifname_ = cur->ifa_name;
    ip_address->is_internet_ = utility->is_internet(cur->ifa_addr);
    ips.push_back(ip_address);

    // set the device internet status.
    if (!ip_address->is_internet_) {
        ss1 << ", intranet ";
        _srs_device_ifs[cur->ifa_name] = false;
    } else {
        ss1 << ", internet ";
        _srs_device_ifs[cur->ifa_name] = true;
    }
    ss1 << cur->ifa_name << " " << ip;
}

vector<SrsIPAddress *> _srs_system_ips;

void retrieve_local_ips(SrsProtocolUtility *utility)
{
    vector<SrsIPAddress *> &ips = _srs_system_ips;

    // Get the addresses.
    ifaddrs *ifap;
    if (getifaddrs(&ifap) == -1) {
        srs_warn("retrieve local ips, getifaddrs failed.");
        return;
    }

    stringstream ss0;
    ss0 << "ips";

    stringstream ss1;
    ss1 << "devices";

    // Discover IPv4 first.
    for (ifaddrs *p = ifap; p; p = p->ifa_next) {
        ifaddrs *cur = p;

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
            discover_network_iface(utility, cur, ips, ss0, ss1, false, loopback);
        }
    }

    // Then, discover IPv6 addresses.
    for (ifaddrs *p = ifap; p; p = p->ifa_next) {
        ifaddrs *cur = p;

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
            discover_network_iface(utility, cur, ips, ss0, ss1, true, loopback);
        }
    }

    // If empty, disover IPv4 loopback.
    if (ips.empty()) {
        for (ifaddrs *p = ifap; p; p = p->ifa_next) {
            ifaddrs *cur = p;

            // Ignore if no address for this interface.
            // @see https://github.com/ossrs/srs/issues/1087#issuecomment-408847115
            if (!cur->ifa_addr) {
                continue;
            }

            // LCOV_EXCL_START
            // retrieve IP address, ignore the tun0 network device, whose addr is NULL.
            // @see: https://github.com/ossrs/srs/issues/141
            bool ipv4 = (cur->ifa_addr->sa_family == AF_INET);
            bool ready = (cur->ifa_flags & IFF_UP) && (cur->ifa_flags & IFF_RUNNING);
            bool ignored = (!cur->ifa_addr) || (cur->ifa_flags & IFF_POINTOPOINT) || (cur->ifa_flags & IFF_PROMISC);
            bool loopback = (cur->ifa_flags & IFF_LOOPBACK);
            if (ipv4 && ready && !ignored) {
                discover_network_iface(utility, cur, ips, ss0, ss1, false, loopback);
            }   
            // LCOV_EXCL_STOP
        }
    }

    srs_trace("%s", ss0.str().c_str());
    srs_trace("%s", ss1.str().c_str());

    freeifaddrs(ifap);
}

vector<SrsIPAddress *> &SrsProtocolUtility::local_ips()
{
    if (_srs_system_ips.empty()) {
        retrieve_local_ips(this);
    }

    return _srs_system_ips;
}

std::string _public_internet_address;

string SrsProtocolUtility::public_internet_address(bool ipv4_only)
{
    if (!_public_internet_address.empty()) {
        return _public_internet_address;
    }

    std::vector<SrsIPAddress *> &ips = local_ips();

    // find the best match public address.
    for (int i = 0; i < (int)ips.size(); i++) {
        SrsIPAddress *ip = ips[i];
        if (!ip->is_internet_) {
            continue;
        }
        if (ipv4_only && !ip->is_ipv4_) {
            continue;
        }

        srs_warn("use public address as ip: %s, ifname=%s", ip->ip_.c_str(), ip->ifname_.c_str());
        _public_internet_address = ip->ip_;
        return ip->ip_;
    }

    // no public address, use private address.
    for (int i = 0; i < (int)ips.size(); i++) {
        SrsIPAddress *ip = ips[i];
        if (ip->is_loopback_) {
            continue;
        }
        if (ipv4_only && !ip->is_ipv4_) {
            continue;
        }

        srs_warn("use private address as ip: %s, ifname=%s", ip->ip_.c_str(), ip->ifname_.c_str());
        _public_internet_address = ip->ip_;
        return ip->ip_;
    }

    // LCOV_EXCL_START
    // Finally, use first whatever kind of address.
    if (!ips.empty() && _public_internet_address.empty()) {
        SrsIPAddress *ip = ips[0];

        srs_warn("use first address as ip: %s, ifname=%s", ip->ip_.c_str(), ip->ifname_.c_str());
        _public_internet_address = ip->ip_;
        return ip->ip_;
    }
    // LCOV_EXCL_STOP

    return "";
}

string srs_get_original_ip(ISrsHttpMessage *r)
{
    SrsHttpHeader *h = r->header();

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

// LCOV_EXCL_START
string SrsProtocolUtility::system_hostname()
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
// LCOV_EXCL_STOP

#if defined(__linux__) || defined(SRS_OSX)
utsname *SrsProtocolUtility::system_uname()
{
    static utsname *system_info = NULL;

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

// See streamid of https://github.com/ossrs/srs/issues/2893
// TODO: FIMXE: We should parse SRT streamid to URL object, rather than a HTTP url subpath.
bool srs_srt_streamid_info(const std::string &streamid, SrtMode &mode, std::string &vhost, std::string &url_subpath)
{
    mode = SrtModePull;

    size_t pos = streamid.find("#!::");
    if (pos != 0) {
        pos = streamid.find("/");
        if (pos == streamid.npos) {
            SrsUniquePtr<ISrsConfig> config(_srs_kernel_factory->create_config());
            url_subpath = config->get_default_app_name() + "/" + streamid;
            return true;
        }
        url_subpath = streamid;
        return true;
    }

    // SRT url supports multiple QueryStrings, which are passed to RTMP to realize authentication and other capabilities
    //@see https://github.com/ossrs/srs/issues/2893
    std::string params;
    std::string real_streamid;
    real_streamid = streamid.substr(4);

    // Compatible with previous auth querystring, like this one:
    //      srt://127.0.0.1:10080?streamid=#!::h=live/livestream?secret=xxx,m=publish
    real_streamid = srs_strings_replace(real_streamid, "?", ",");

    std::map<std::string, std::string> query;
    srs_net_url_parse_query(real_streamid, query);
    for (std::map<std::string, std::string>::iterator it = query.begin(); it != query.end(); ++it) {
        if (it->first == "h") {
            std::string host = it->second;

            size_t r0 = host.find("/");
            size_t r1 = host.rfind("/");
            if (r0 != std::string::npos && r0 != std::string::npos) {
                // Compatible with previous style, see https://github.com/ossrs/srs/issues/2893#compatible
                //      srt://127.0.0.1:10080?streamid=#!::h=live/livestream,m=publish
                //      srt://127.0.0.1:10080?streamid=#!::h=live/livestream,m=request
                //      srt://127.0.0.1:10080?streamid=#!::h=srs.srt.com.cn/live/livestream,m=publish
                if (r0 != r1) {
                    // We got vhost in host.
                    url_subpath = host.substr(r0 + 1);
                    host = host.substr(0, r0);

                    params.append("vhost=");
                    params.append(host);
                    params.append("&");
                    vhost = host;
                } else {
                    // Only stream in host.
                    url_subpath = host;
                }
            } else {
                // New URL style, see https://github.com/ossrs/srs/issues/2893#solution
                //      srt://host.com:10080?streamid=#!::h=host.com,r=app/stream,key1=value1,key2=value2
                //      srt://1.2.3.4:10080?streamid=#!::h=host.com,r=app/stream,key1=value1,key2=value2
                //      srt://1.2.3.4:10080?streamid=#!::r=app/stream,key1=value1,key2=value2
                params.append("vhost=");
                params.append(host);
                params.append("&");
                vhost = host;
            }
        } else if (it->first == "r") {
            url_subpath = it->second;
        } else if (it->first == "m") {
            std::string mode_str = it->second; // support m=publish or m=request
            std::transform(it->second.begin(), it->second.end(), mode_str.begin(), ::tolower);
            if (mode_str == "publish") {
                mode = SrtModePush;
            } else if (mode_str == "request") {
                mode = SrtModePull;
            } else {
                srs_warn("unknown mode_str:%s", mode_str.c_str());
                return false;
            }
        } else {
            params.append(it->first);
            params.append("=");
            params.append(it->second);
            params.append("&");
        }
    }

    if (url_subpath.empty()) {
        return false;
    }

    if (!params.empty()) {
        url_subpath.append("?");
        url_subpath.append(params);
        url_subpath = url_subpath.substr(0, url_subpath.length() - 1); // remove last '&'
    }

    return true;
}

bool srs_srt_streamid_to_request(const std::string &streamid, SrtMode &mode, ISrsRequest *request)
{
    string url_subpath = "";
    bool ret = srs_srt_streamid_info(streamid, mode, request->vhost_, url_subpath);
    if (!ret) {
        return ret;
    }

    size_t pos = url_subpath.find("/");
    string stream_with_params = "";
    if (pos == string::npos) {
        SrsUniquePtr<ISrsConfig> config(_srs_kernel_factory->create_config());
        request->app_ = config->get_default_app_name();
        stream_with_params = url_subpath;
    } else {
        request->app_ = url_subpath.substr(0, pos);
        stream_with_params = url_subpath.substr(pos + 1);
    }

    pos = stream_with_params.find("?");
    if (pos == string::npos) {
        request->stream_ = stream_with_params;
    } else {
        request->stream_ = stream_with_params.substr(0, pos);
        request->param_ = stream_with_params.substr(pos + 1);
    }

    SrsProtocolUtility utility;
    request->host_ = utility.public_internet_address();
    if (request->vhost_.empty())
        request->vhost_ = request->host_;
    request->tcUrl_ = srs_net_url_encode_tcurl("srt", request->host_, request->vhost_, request->app_, request->port_);

    return ret;
}
