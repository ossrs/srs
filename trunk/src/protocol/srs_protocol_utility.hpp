//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_PROTOCOL_UTILITY_HPP
#define SRS_PROTOCOL_UTILITY_HPP

#include <srs_core.hpp>

#include <map>
#include <sstream>
#include <string>
#include <vector>

#include <srs_kernel_consts.hpp>
#include <srs_kernel_utility.hpp>

#include <string>
#include <vector>

#include <srs_protocol_st.hpp>

class ISrsHttpMessage;
class ISrsConfig;

class SrsMessageHeader;
class SrsMediaPacket;
class SrsRtmpCommonMessage;
class ISrsProtocolReadWriter;
class ISrsReader;

// The stream and param are inputs and outputs.
extern void srs_net_url_parse_tcurl(std::string tcUrl, std::string &schema, std::string &host, std::string &vhost, std::string &app,
                                    std::string &stream, int &port, std::string &param);

// Some legacy RTMP clients put the query before the stream, as rtmp://ip/app/app2?vhost=xxx/stream,
// where the standard form is rtmp://ip/app/app2/stream?vhost=xxx.
extern std::string srs_net_url_convert_legacy_rtmp_url(const std::string &url);

// OBS may put the stream in the app or in the param, for example:
//      rtmp://ip/live/livestream
//      rtmp://ip/live/livestream?secret=xxx
//      rtmp://ip/live?secret=xxx/livestream
extern void srs_net_url_guess_stream(std::string &app, std::string &param, std::string &stream);

extern std::string srs_net_url_normalize_param(const std::string &param);
extern void srs_net_url_parse_query(std::string q, std::map<std::string, std::string> &query);
extern std::string srs_net_url_encode_tcurl(std::string schema, std::string host, std::string vhost, std::string app, int port);
extern std::string srs_net_url_encode_stream(std::string host, std::string vhost, std::string stream, std::string param, bool with_vhost = true);
extern std::string srs_net_url_encode_sid(std::string vhost, std::string app, std::string stream);
extern void srs_net_url_parse_rtmp_url(std::string url, std::string &tcUrl, std::string &stream);
extern void srs_net_url_parse_http_path(std::string path, std::string &app, std::string &stream);
extern std::string srs_net_url_encode_rtmp_url(std::string server, int port, std::string host, std::string vhost, std::string app, std::string stream, std::string param);

// The message takes data, which is freed on error. The caller frees *ppmsg.
extern srs_error_t srs_rtmp_create_msg(char type, uint32_t timestamp, char *data, int size, int stream_id, SrsRtmpCommonMessage **ppmsg);

struct SrsIPAddress {
    std::string ifname_;
    std::string ip_;
    bool is_ipv4_;
    bool is_internet_;
    bool is_loopback_;
};

class SrsProtocolUtility;
struct ifaddrs;

// It records whether each interface is internet too, for SrsProtocolUtility::is_internet(ifname).
extern void srs_discover_local_ips(SrsProtocolUtility *utility, struct ifaddrs *ifap, std::vector<SrsIPAddress *> &ips);

class ISrsProtocolUtility
{
public:
    ISrsProtocolUtility();
    virtual ~ISrsProtocolUtility();

public:
    virtual std::vector<SrsIPAddress *> &local_ips() = 0;
};

class SrsProtocolUtility : public ISrsProtocolUtility
{
public:
    SrsProtocolUtility();
    virtual ~SrsProtocolUtility();

public:
    virtual srs_error_t write_iovs(ISrsProtocolReadWriter *skt, iovec *iovs, int size, ssize_t *pnwrite = NULL);

public:
    virtual std::vector<SrsIPAddress *> &local_ips();
    virtual std::string public_internet_address(bool ipv4_only = false);
    virtual bool is_internet(std::string ifname);
    virtual bool is_internet(const sockaddr *addr);

public:
    virtual std::string system_hostname(void);
    virtual utsname *system_uname();
};

extern std::string srs_get_original_ip(ISrsHttpMessage *r);

class ISrsRequest;

enum SrtMode {
    SrtModePull = 1,
    SrtModePush = 2,
};

extern bool srs_srt_streamid_info(ISrsConfig *config, const std::string &streamid, SrtMode &mode, std::string &vhost, std::string &url_subpath);
extern bool srs_srt_streamid_to_request(ISrsConfig *config, const std::string &streamid, SrtMode &mode, ISrsRequest *request);

#endif
