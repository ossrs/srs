//
// Copyright (c) 2013-2025 The SRS Authors
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

// The UDP black hole, for developer to use wireshark to catch plaintext packets.
// For example, server receive UDP packets at udp://8000, and forward the plaintext packet to black hole,
// we can use wireshark to capture the plaintext.
class SrsRtcBlackhole
{
public:
    bool blackhole_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    sockaddr_in *blackhole_addr_;
    srs_netfd_t blackhole_stfd_;

public:
    SrsRtcBlackhole();
    virtual ~SrsRtcBlackhole();

public:
    srs_error_t initialize();
    void sendto(void *data, int len);
};

extern SrsRtcBlackhole *_srs_blackhole;

// The user config for RTC publish or play.
class SrsRtcUserConfig
{
public:
    // Original variables from API.
    std::string remote_sdp_str_;
    SrsSdp remote_sdp_;
    std::string eip_;
    std::string codec_;
    std::string api_;

    // Session data.
    std::string local_sdp_str_;
    std::string session_id_;
    std::string token_;

    // Generated data.
    ISrsRequest *req_;
    bool publish_;
    bool dtls_;
    bool srtp_;

    // The order of audio and video, or whether audio is before video. Please make sure the order is match for offer and
    // answer, or client might fail at setRemoveDescription(answer). See https://github.com/ossrs/srs/issues/3179
    bool audio_before_video_;

public:
    SrsRtcUserConfig();
    virtual ~SrsRtcUserConfig();
};

// Discover the candidates for RTC server.
extern std::set<std::string> discover_candidates(SrsProtocolUtility *utility, ISrsAppConfig *config, SrsRtcUserConfig *ruc);

// The dns resolve utility, return the resolved ip address.
extern std::string srs_dns_resolve(std::string host, int &family);

// RTC session manager to handle WebRTC session lifecycle and management.
class SrsRtcSessionManager : public ISrsExecRtcAsyncTask
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
    // WebRTC async call worker for non-blocking operations.
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

    // interface ISrsExecRtcAsyncTask
public:
    virtual srs_error_t exec_rtc_async_work(ISrsAsyncCallTask *t);

public:
    virtual srs_error_t on_udp_packet(ISrsUdpMuxSocket *skt);
};

// Helper function to discover candidate IPs from API server hostname
// Used by WebRTC ICE candidate discovery
extern srs_error_t api_server_as_candidates(ISrsAppConfig *config, std::string api, std::set<std::string> &candidate_ips);

#endif
