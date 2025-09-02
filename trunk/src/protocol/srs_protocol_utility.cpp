//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_protocol_utility.hpp>

#include <unistd.h>

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
    stream = srs_path_filepath_base(uri.get_path());
    param = uri.get_query().empty() ? "" : "?" + uri.get_query();
    param += uri.get_fragment().empty() ? "" : "#" + uri.get_fragment();

    // Parse app without the prefix slash.
    app = srs_path_filepath_dir(uri.get_path());
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
    // Note that we ignore any extension.
    url += "/" + srs_path_filepath_filename(stream);

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

srs_error_t srs_write_large_iovs(ISrsProtocolReadWriter *skt, iovec *iovs, int size, ssize_t *pnwrite)
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

bool srs_net_device_is_internet(string ifname)
{
    srs_info("check ifname=%s", ifname.c_str());

    if (_srs_device_ifs.find(ifname) == _srs_device_ifs.end()) {
        return false;
    }
    return _srs_device_ifs[ifname];
}

bool srs_net_device_is_internet(const sockaddr *addr)
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

vector<SrsIPAddress *> _srs_system_ips;

void discover_network_iface(ifaddrs *cur, vector<SrsIPAddress *> &ips, stringstream &ss0, stringstream &ss1, bool ipv6, bool loopback)
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
            discover_network_iface(cur, ips, ss0, ss1, false, loopback);
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
            discover_network_iface(cur, ips, ss0, ss1, true, loopback);
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

vector<SrsIPAddress *> &srs_get_local_ips()
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

    std::vector<SrsIPAddress *> &ips = srs_get_local_ips();

    // find the best match public address.
    for (int i = 0; i < (int)ips.size(); i++) {
        SrsIPAddress *ip = ips[i];
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
        SrsIPAddress *ip = ips[i];
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
        SrsIPAddress *ip = ips[0];

        srs_warn("use first address as ip: %s, ifname=%s", ip->ip.c_str(), ip->ifname.c_str());
        _public_internet_address = ip->ip;
        return ip->ip;
    }

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

#if defined(__linux__) || defined(SRS_OSX)
utsname *srs_get_system_uname_info()
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
