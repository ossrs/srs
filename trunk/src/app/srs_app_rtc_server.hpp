//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_APP_RTC_SERVER_HPP
#define SRS_APP_RTC_SERVER_HPP

#include <srs_core.hpp>

#include <srs_app_async_call.hpp>
#include <srs_app_listener.hpp>
#include <srs_app_reload.hpp>
#include <srs_app_rtc_conn.hpp>
#include <srs_app_st.hpp>
#include <srs_kernel_hourglass.hpp>
#include <srs_protocol_sdp.hpp>

#include <set>
#include <string>

class SrsRtcServer;
class SrsHourGlass;
class SrsRtcConnection;
class ISrsRtcConnection;
class ISrsRequest;
class SrsSdp;
class SrsRtcSource;
class SrsResourceManager;
class SrsAsyncCallWorker;
class ISrsUdpMuxSocket;
class ISrsResourceManager;
class ISrsStreamPublishTokenManager;
class ISrsRtcSourceManager;
class ISrsDtlsCertificate;
class ISrsAppConfig;
class ISrsAppFactory;
class ISrsProtocolUtility;

class ISrsRtcBlackhole
{
public:
    ISrsRtcBlackhole();
    virtual ~ISrsRtcBlackhole();

public:
    virtual srs_error_t initialize() = 0;
    virtual void sendto(void *data, int len) = 0;
};

// Lets a developer capture RTC packets as plaintext with Wireshark: the server copies the plaintext packets to this
// UDP address.
class SrsRtcBlackhole : public ISrsRtcBlackhole
{
public:
    bool blackhole_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsAppConfig *config_;

    sockaddr_in *blackhole_addr_;
    srs_netfd_t blackhole_stfd_;

public:
    SrsRtcBlackhole();
    virtual ~SrsRtcBlackhole();

public:
    virtual srs_error_t initialize();
    virtual void sendto(void *data, int len);
};

extern SrsRtcBlackhole *_srs_blackhole;

class SrsRtcUserConfig
{
public:
    std::string remote_sdp_str_;
    SrsSdp remote_sdp_;
    std::string eip_;
    std::string vcodec_;
    std::string acodec_;
    std::string api_;

    std::string local_sdp_str_;
    std::string session_id_;
    std::string token_;

    ISrsRequest *req_;
    bool publish_;
    bool dtls_;
    bool srtp_;

    // The answer must list audio and video in the same order as the offer, or the client may fail at
    // setRemoteDescription(answer). See https://github.com/ossrs/srs/issues/3179
    bool audio_before_video_;

public:
    SrsRtcUserConfig();
    virtual ~SrsRtcUserConfig();
};

extern std::set<std::string> discover_candidates(ISrsProtocolUtility *utility, ISrsAppConfig *config, SrsRtcUserConfig *ruc);

extern std::string srs_dns_resolve(std::string host, int &family);

class ISrsRtcSessionManager
{
public:
    ISrsRtcSessionManager();
    virtual ~ISrsRtcSessionManager();

public:
    virtual srs_error_t initialize() = 0;
    virtual ISrsRtcConnection *find_rtc_session_by_username(const std::string &ufrag) = 0;
    virtual srs_error_t create_rtc_session(SrsRtcUserConfig *ruc, SrsSdp &local_sdp, ISrsRtcConnection **psession) = 0;
    virtual void srs_update_rtc_sessions() = 0;
    virtual srs_error_t on_udp_packet(ISrsUdpMuxSocket *skt) = 0;
};

class SrsRtcSessionManager : public ISrsRtcSessionManager, public ISrsExecRtcAsyncTask
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsResourceManager *conn_manager_;
    ISrsStreamPublishTokenManager *stream_publish_tokens_;
    ISrsRtcSourceManager *rtc_sources_;
    ISrsDtlsCertificate *dtls_certificate_;
    ISrsAppConfig *config_;
    ISrsAppFactory *app_factory_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    SrsAsyncCallWorker *rtc_async_;

public:
    SrsRtcSessionManager();
    virtual ~SrsRtcSessionManager();

public:
    virtual srs_error_t initialize();

public:
    virtual ISrsRtcConnection *find_rtc_session_by_username(const std::string &ufrag);
    virtual srs_error_t create_rtc_session(SrsRtcUserConfig *ruc, SrsSdp &local_sdp, ISrsRtcConnection **psession);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t do_create_rtc_session(SrsRtcUserConfig *ruc, SrsSdp &local_sdp, ISrsRtcConnection *session);

public:
    virtual void srs_update_rtc_sessions();

public:
    virtual srs_error_t exec_rtc_async_work(ISrsAsyncCallTask *t);

public:
    virtual srs_error_t on_udp_packet(ISrsUdpMuxSocket *skt);
};

extern srs_error_t api_server_as_candidates(ISrsAppConfig *config, std::string api, std::set<std::string> &candidate_ips);

#endif
