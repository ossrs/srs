//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_PROTOCOL_UTILITY_HPP
#define SRS_PROTOCOL_UTILITY_HPP

#include <srs_core.hpp>

#include <sys/uio.h>

#include <map>
#include <sstream>
#include <string>
#include <vector>

#include <srs_kernel_consts.hpp>
#include <srs_kernel_utility.hpp>

#include <arpa/inet.h>
#include <string>
#include <vector>

#include <srs_protocol_st.hpp>

#if defined(__linux__) || defined(SRS_OSX)
#include <sys/utsname.h>
#endif

class ISrsHttpMessage;

class SrsMessageHeader;
class SrsMediaPacket;
class SrsRtmpCommonMessage;
class ISrsProtocolReadWriter;
class ISrsReader;

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
extern std::string srs_net_url_encode_stream(std::string host, std::string vhost, std::string stream, std::string param, bool with_vhost = true);

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

/**
 * create shared ptr message from bytes.
 * @param data the packet bytes. user should never free it.
 * @param ppmsg output the shared ptr message. user should free it.
 */
extern srs_error_t srs_rtmp_create_msg(char type, uint32_t timestamp, char *data, int size, int stream_id, SrsRtmpCommonMessage **ppmsg);

// write large numbers of iovs.
extern srs_error_t srs_write_large_iovs(ISrsProtocolReadWriter *skt, iovec *iovs, int size, ssize_t *pnwrite = NULL);

// Get local ip, fill to @param ips
struct SrsIPAddress {
    // The network interface name, such as eth0, en0, eth1.
    std::string ifname;
    // The IP v4 or v6 address.
    std::string ip;
    // Whether the ip is IPv4 address.
    bool is_ipv4;
    // Whether the ip is internet public IP address.
    bool is_internet;
    // Whether the ip is loopback, such as 127.0.0.1
    bool is_loopback;
};
extern std::vector<SrsIPAddress *> &srs_get_local_ips();

// Get local public ip, empty string if no public internet address found.
extern std::string srs_get_public_internet_address(bool ipv4_only = false);

// Detect whether specified device is internet public address.
extern bool srs_net_device_is_internet(std::string ifname);
extern bool srs_net_device_is_internet(const sockaddr *addr);

// Get the original ip from query and header by proxy.
extern std::string srs_get_original_ip(ISrsHttpMessage *r);

// Get hostname
extern std::string srs_get_system_hostname(void);

#if defined(__linux__) || defined(SRS_OSX)
// Get system uname info.
extern utsname *srs_get_system_uname_info();
#endif

#endif
