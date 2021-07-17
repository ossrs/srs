//
// Copyright (c) 2013-2021 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_APP_RTC_SDP_HPP
#define SRS_APP_RTC_SDP_HPP

#include <srs_core.hpp>
#include <srs_kernel_utility.hpp>

#include <stdint.h>

#include <string>
#include <vector>
#include <map>
const std::string kTWCCExt = "http://www.ietf.org/id/draft-holmer-rmcat-transport-wide-cc-extensions-01";

// TDOO: FIXME: Rename it, and add utest.
extern std::vector<std::string> split_str(const std::string& str, const std::string& delim);

class SrsSessionConfig
{
public:
    std::string dtls_role;
    std::string dtls_version;
};

class SrsSessionInfo
{
public:
    SrsSessionInfo();
    virtual ~SrsSessionInfo();

    srs_error_t parse_attribute(const std::string& attribute, const std::string& value);
    srs_error_t encode(std::ostringstream& os);

    bool operator==(const SrsSessionInfo& rhs);
    // user-defined copy assignment (copy-and-swap idiom)
    SrsSessionInfo& operator=(SrsSessionInfo other);
public:
    std::string ice_ufrag_;
    std::string ice_pwd_;
    std::string ice_options_;
    std::string fingerprint_algo_;
    std::string fingerprint_;
    std::string setup_;
};

class SrsSSRCInfo
{
public:
    SrsSSRCInfo();
    SrsSSRCInfo(uint32_t ssrc, std::string cname, std::string stream_id, std::string track_id);
    virtual ~SrsSSRCInfo();
public:
    srs_error_t encode(std::ostringstream& os);
public:
    uint32_t ssrc_;
    std::string cname_;
    // See https://webrtchacks.com/sdp-anatomy/
    // a=ssrc:2231627014 msid:lgsCFqt9kN2fVKw5wg3NKqGdATQoltEwOdMS daed9400-d0dd-4db3-b949-422499e96e2d
    // a=ssrc:2231627014 msid:{msid_} {msid_tracker_}
    std::string msid_;
    std::string msid_tracker_;
    std::string mslabel_;
    std::string label_;
};

class SrsSSRCGroup
{
public:
    SrsSSRCGroup();
    SrsSSRCGroup(const std::string& usage, const std::vector<uint32_t>& ssrcs);
    virtual ~SrsSSRCGroup();
public:
    srs_error_t encode(std::ostringstream& os);
public:
    // e.g FIX, FEC, SIM.
    std::string semantic_;
    // SSRCs of this type. 
    std::vector<uint32_t> ssrcs_;
public:
    bool is_sim() const { // SrsSSRCGroup::is_sim()
        return semantic_ == "SIM";
    }
};

struct H264SpecificParam
{
    std::string profile_level_id;
    std::string packetization_mode;
    std::string level_asymmerty_allow;
};

extern srs_error_t srs_parse_h264_fmtp(const std::string& fmtp, H264SpecificParam& h264_param);

class SrsMediaPayloadType
{
public:
    SrsMediaPayloadType(int payload_type);
    virtual ~SrsMediaPayloadType();

    srs_error_t encode(std::ostringstream& os);
public:
    int payload_type_;

    std::string encoding_name_;
    int clock_rate_;
    std::string encoding_param_;

    std::vector<std::string> rtcp_fb_;
    std::string format_specific_param_;
};

struct SrsCandidate
{
    std::string ip_;
    int port_;
    std::string type_;
};

class SrsMediaDesc
{
public:
    SrsMediaDesc(const std::string& type);
    virtual ~SrsMediaDesc();
public:
    srs_error_t parse_line(const std::string& line);
    srs_error_t encode(std::ostringstream& os);
    SrsMediaPayloadType* find_media_with_payload_type(int payload_type);
    std::vector<SrsMediaPayloadType> find_media_with_encoding_name(const std::string& encoding_name) const;
    const std::map<int, std::string>& get_extmaps() const { return extmaps_; }
    srs_error_t update_msid(std::string id);

    bool is_audio() const { return type_ == "audio"; }
    bool is_video() const { return type_ == "video"; }
private:
    srs_error_t parse_attribute(const std::string& content);
    srs_error_t parse_attr_rtpmap(const std::string& value);
    srs_error_t parse_attr_rtcp(const std::string& value);
    srs_error_t parse_attr_rtcp_fb(const std::string& value);
    srs_error_t parse_attr_fmtp(const std::string& value);
    srs_error_t parse_attr_mid(const std::string& value);
    srs_error_t parse_attr_msid(const std::string& value);
    srs_error_t parse_attr_ssrc(const std::string& value);
    srs_error_t parse_attr_ssrc_group(const std::string& value);
    srs_error_t parse_attr_extmap(const std::string& value);
private:
    SrsSSRCInfo& fetch_or_create_ssrc_info(uint32_t ssrc);

public:
    SrsSessionInfo session_info_;
    std::string type_;
    int port_;

    bool rtcp_mux_;
    bool rtcp_rsize_;

    bool sendonly_;
    bool recvonly_;
    bool sendrecv_;
    bool inactive_;

    std::string mid_;
    std::string msid_;
    std::string msid_tracker_;
    std::string protos_;
    std::vector<SrsMediaPayloadType> payload_types_;

    std::vector<SrsCandidate> candidates_;
    std::vector<SrsSSRCGroup> ssrc_groups_;
    std::vector<SrsSSRCInfo>  ssrc_infos_;
    std::map<int, std::string> extmaps_;

public:
    // Whether SSRS is original stream.
    // @see https://github.com/ossrs/srs/pull/2420#discussion_r655792920
    bool is_original_ssrc(const SrsSSRCInfo* info) const;
};

class SrsSdp
{
public:
    SrsSdp();
    virtual ~SrsSdp();
public:
    srs_error_t parse(const std::string& sdp_str);
    srs_error_t encode(std::ostringstream& os);
public:
    std::vector<SrsMediaDesc*> find_media_descs(const std::string& type);
public:
    void set_ice_ufrag(const std::string& ufrag);
    void set_ice_pwd(const std::string& pwd);
    void set_dtls_role(const std::string& dtls_role);
    void set_fingerprint_algo(const std::string& algo);
    void set_fingerprint(const std::string& fingerprint);
    void add_candidate(const std::string& ip, const int& port, const std::string& type);

    std::string get_ice_ufrag() const;
    std::string get_ice_pwd() const;
    std::string get_dtls_role() const;
private:
    srs_error_t parse_line(const std::string& line);
private:
    srs_error_t parse_origin(const std::string& content);
    srs_error_t parse_version(const std::string& content);
    srs_error_t parse_session_name(const std::string& content);
    srs_error_t parse_timing(const std::string& content);
    srs_error_t parse_attribute(const std::string& content);
    srs_error_t parse_media_description(const std::string& content);
    srs_error_t parse_attr_group(const std::string& content);
private:
    bool in_media_session_;
public:
    // version
    std::string version_;

    // origin
    std::string username_;
    std::string session_id_;
    std::string session_version_;
    std::string nettype_;
    std::string addrtype_;
    std::string unicast_address_;

    // session_name
    std::string session_name_;

    // timing
    int64_t start_time_;
    int64_t end_time_;

    SrsSessionInfo session_info_;
    SrsSessionConfig session_config_;
    SrsSessionConfig session_negotiate_;

    std::vector<std::string> groups_;
    std::string group_policy_;

    std::string msid_semantic_;
    std::vector<std::string> msids_;

    // m-line, media sessions
    std::vector<SrsMediaDesc> media_descs_;

     bool is_unified() const;
    // TODO: FIXME: will be fixed when use single pc.
    srs_error_t update_msid(std::string id);
};

#endif
