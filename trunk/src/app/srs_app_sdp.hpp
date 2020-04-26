/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2020 John
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef SRS_APP_SDP_HPP
#define SRS_APP_SDP_HPP

#include <srs_core.hpp>
#include <srs_kernel_utility.hpp>

#include <stdint.h>

#include <string>
#include <vector>

class SrsSessionInfo
{
public:
    SrsSessionInfo();
    virtual ~SrsSessionInfo();

    srs_error_t parse_attribute(const std::string& attribute, const std::string& value);
    srs_error_t encode(std::ostringstream& os);

    bool operator=(const SrsSessionInfo& rhs);
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
    virtual ~SrsSSRCInfo();
public:
    srs_error_t encode(std::ostringstream& os);
public:
    uint32_t ssrc_;
    std::string cname_;
    std::string msid_;
    std::string msid_tracker_;
    std::string mslabel_;
    std::string label_;
};

class SrsSSRCGroup
{
};

struct H264SpecificParam
{
    std::string profile_level_id;
    std::string packetization_mode;
    std::string level_asymmerty_allow;
};

extern srs_error_t parse_h264_fmtp(const std::string& fmtp, H264SpecificParam& h264_param);

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
public:
    const SrsMediaDesc* find_media_desc(const std::string& type) const;
public:
    void set_ice_ufrag(const std::string& ufrag);
    void set_ice_pwd(const std::string& pwd);
    void set_fingerprint_algo(const std::string& algo);
    void set_fingerprint(const std::string& fingerprint);
    void add_candidate(const std::string& ip, const int& port, const std::string& type);

    std::string get_ice_ufrag() const;
    std::string get_ice_pwd() const;
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

    std::vector<std::string> groups_;
    std::string group_policy_;

    std::string msid_semantic_;
    std::vector<std::string> msids_;

    // m-line, media sessions
    std::vector<SrsMediaDesc> media_descs_;
};

#endif
