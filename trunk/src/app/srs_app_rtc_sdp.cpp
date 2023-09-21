//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#include <srs_app_rtc_sdp.hpp>

#include <stdlib.h>

#include <iostream>
#include <sstream>
#include <vector>
#include <algorithm>
using namespace std;

#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>

// TODO: FIXME: Maybe we should use json.encode to escape it?
const std::string kCRLF = "\r\n";

#define FETCH(is,word) \
if (!(is >> word)) {\
    return srs_error_new(ERROR_RTC_SDP_DECODE, "fetch failed");\
}\

#define FETCH_WITH_DELIM(is,word,delim) \
if (!getline(is,word,delim)) {\
    return srs_error_new(ERROR_RTC_SDP_DECODE, "fetch with delim failed");\
}\

std::vector<std::string> split_str(const std::string& str, const std::string& delim)
{
    std::vector<std::string> ret;
    size_t pre_pos = 0;
    std::string tmp;
    size_t pos = 0;
    do {
        pos = str.find(delim, pre_pos);
        tmp = str.substr(pre_pos, pos - pre_pos);
        ret.push_back(tmp);
        pre_pos = pos + delim.size();
    } while (pos != std::string::npos);

    return ret;
}

static void skip_first_spaces(std::string& str)
{
    while (! str.empty() && str[0] == ' ') {
        str.erase(0, 1);
    }
}

srs_error_t srs_parse_h264_fmtp(const std::string& fmtp, H264SpecificParam& h264_param)
{
    srs_error_t err = srs_success;

    std::vector<std::string> vec = srs_string_split(fmtp, ";");
    for (size_t i = 0; i < vec.size(); ++i) {
        std::vector<std::string> kv = srs_string_split(vec[i], "=");
        if (kv.size() != 2) continue;

        if (kv[0] == "profile-level-id") {
            h264_param.profile_level_id = kv[1];
        } else if (kv[0] == "packetization-mode") {
            // 6.3.  Non-Interleaved Mode
            // This mode is in use when the value of the OPTIONAL packetization-mode
            // media type parameter is equal to 1.  This mode SHOULD be supported.
            // It is primarily intended for low-delay applications.  Only single NAL
            // unit packets, STAP-As, and FU-As MAY be used in this mode.  STAP-Bs,
            // MTAPs, and FU-Bs MUST NOT be used.  The transmission order of NAL
            // units MUST comply with the NAL unit decoding order.
            // @see https://tools.ietf.org/html/rfc6184#section-6.3
            h264_param.packetization_mode = kv[1];
        } else if (kv[0] == "level-asymmetry-allowed") {
            h264_param.level_asymmerty_allow = kv[1];
        }
    }

    if (h264_param.profile_level_id.empty()) {
        return srs_error_new(ERROR_RTC_SDP_DECODE, "no h264 param: profile-level-id");
    }
    if (h264_param.packetization_mode.empty()) {
        return srs_error_new(ERROR_RTC_SDP_DECODE, "no h264 param: packetization-mode");
    }
    if (h264_param.level_asymmerty_allow.empty()) {
        return srs_error_new(ERROR_RTC_SDP_DECODE, "no h264 param: level-asymmetry-allowed");
    }

    return err;
}

SrsSessionInfo::SrsSessionInfo()
{
}

SrsSessionInfo::~SrsSessionInfo()
{
}

srs_error_t SrsSessionInfo::parse_attribute(const std::string& attribute, const std::string& value)
{
    srs_error_t err = srs_success;

    if (attribute == "ice-ufrag") {
        ice_ufrag_ = value;
    } else if (attribute == "ice-pwd") {
        ice_pwd_ = value;
    } else if (attribute == "ice-options") {
        ice_options_ = value;
    } else if (attribute == "fingerprint") {
        std::istringstream is(value);
        FETCH(is, fingerprint_algo_);
        FETCH(is, fingerprint_);
    } else if (attribute == "setup") {
        // @see: https://tools.ietf.org/html/rfc4145#section-4
        setup_ = value;
    } else {
        srs_trace("ignore attribute=%s, value=%s", attribute.c_str(), value.c_str());
    }

    return err;
}

srs_error_t SrsSessionInfo::encode(std::ostringstream& os)
{
    srs_error_t err = srs_success;

    if (!ice_ufrag_.empty()) {
        os << "a=ice-ufrag:" << ice_ufrag_ << kCRLF;
    }
    
    if (!ice_pwd_.empty()) {
        os << "a=ice-pwd:" << ice_pwd_ << kCRLF;
    }

    // For ICE-lite, we never set the trickle.
    if (!ice_options_.empty()) {
        os << "a=ice-options:" << ice_options_ << kCRLF;
    }
    
    if (!fingerprint_algo_.empty() && ! fingerprint_.empty()) {
        os << "a=fingerprint:" << fingerprint_algo_ << " " << fingerprint_ << kCRLF;
    }
    
    if (!setup_.empty()) {
        os << "a=setup:" << setup_ << kCRLF;
    }

    return err;
}

bool SrsSessionInfo::operator==(const SrsSessionInfo& rhs)
{
    return ice_ufrag_        == rhs.ice_ufrag_ &&
           ice_pwd_          == rhs.ice_pwd_ &&
           ice_options_      == rhs.ice_options_ &&
           fingerprint_algo_ == rhs.fingerprint_algo_ &&
           fingerprint_      == rhs.fingerprint_ &&
           setup_            == rhs.setup_;
}

SrsSessionInfo &SrsSessionInfo::operator=(SrsSessionInfo other) {
    std::swap(ice_ufrag_, other.ice_ufrag_);
    std::swap(ice_pwd_, other.ice_pwd_);
    std::swap(ice_options_, other.ice_options_);
    std::swap(fingerprint_algo_, other.fingerprint_algo_);
    std::swap(fingerprint_, other.fingerprint_);
    std::swap(setup_, other.setup_);
    return *this;
}

SrsSSRCInfo::SrsSSRCInfo()
{
    ssrc_ = 0;
}

SrsSSRCInfo::SrsSSRCInfo(uint32_t ssrc, std::string cname, std::string stream_id, std::string track_id)
{
    ssrc_ = ssrc;
    cname_ = cname;
    msid_ = stream_id;
    msid_tracker_ = track_id;
    mslabel_ = msid_;
    label_ = msid_tracker_;
}

SrsSSRCInfo::~SrsSSRCInfo()
{
}

srs_error_t SrsSSRCInfo::encode(std::ostringstream& os)
{
    srs_error_t err = srs_success;

    if (ssrc_ == 0) {
        return srs_error_new(ERROR_RTC_SDP_DECODE, "invalid ssrc");
    }

    // See AnnexF at page 101 of https://openstd.samr.gov.cn/bzgk/gb/newGbInfo?hcno=469659DC56B9B8187671FF08748CEC89
    // Encode the bellow format:
    //      a=ssrc:0100008888 cname:0100008888
    //      a=ssrc:0100008888 label:gb28181
    // As GB28181 format:
    //      y=0100008888
    if (label_ == "gb28181") {
        os << "y=" << (cname_.empty() ? srs_fmt("%u", ssrc_) : cname_) << kCRLF;
        return err;
    }

    os << "a=ssrc:" << ssrc_ << " cname:" << cname_ << kCRLF;
    if (!msid_.empty()) {
        os << "a=ssrc:" << ssrc_ << " msid:" << msid_;
        if (!msid_tracker_.empty()) {
            os << " " << msid_tracker_;
        }
        os << kCRLF;
    }
    if (!mslabel_.empty()) {
        os << "a=ssrc:" << ssrc_ << " mslabel:" << mslabel_ << kCRLF;
    }
    if (!label_.empty()) {
        os << "a=ssrc:" << ssrc_ << " label:" << label_ << kCRLF;
    }

    return err;
}

SrsSSRCGroup::SrsSSRCGroup()
{
}

SrsSSRCGroup::~SrsSSRCGroup()
{
}

SrsSSRCGroup::SrsSSRCGroup(const std::string& semantic, const std::vector<uint32_t>& ssrcs)
{
    semantic_ = semantic;
    ssrcs_ = ssrcs;
}

srs_error_t SrsSSRCGroup::encode(std::ostringstream& os)
{
    srs_error_t err = srs_success;

    if (semantic_.empty()) {
        return srs_error_new(ERROR_RTC_SDP_DECODE, "invalid semantics");
    }

    if (ssrcs_.size() == 0) {
        return srs_error_new(ERROR_RTC_SDP_DECODE, "invalid ssrcs");
    }

    os << "a=ssrc-group:" << semantic_;
    for (int i = 0; i < (int)ssrcs_.size(); i++) {
        os << " " << ssrcs_[i];
    }

    return err;
}

SrsMediaPayloadType::SrsMediaPayloadType(int payload_type)
{
    payload_type_ = payload_type;
    clock_rate_ = 0;
}

SrsMediaPayloadType::~SrsMediaPayloadType()
{
}

srs_error_t SrsMediaPayloadType::encode(std::ostringstream& os)
{
    srs_error_t err = srs_success;

    os << "a=rtpmap:" << payload_type_ << " " << encoding_name_ << "/" << clock_rate_;
    if (!encoding_param_.empty()) {
        os << "/" << encoding_param_;
    }
    os << kCRLF;

    for (std::vector<std::string>::iterator iter = rtcp_fb_.begin(); iter != rtcp_fb_.end(); ++iter) {
        os << "a=rtcp-fb:" << payload_type_ << " " << *iter << kCRLF;
    }

    if (!format_specific_param_.empty()) {
        os << "a=fmtp:" << payload_type_ << " " << format_specific_param_
           // TODO: FIXME: Remove the test code bellow.
           // << ";x-google-max-bitrate=6000;x-google-min-bitrate=5100;x-google-start-bitrate=5000"
           << kCRLF;
    }

    return err;
}

SrsMediaDesc::SrsMediaDesc(const std::string& type)
{
    type_ = type;

    port_ = 0;
    rtcp_mux_ = false;
    rtcp_rsize_ = false;

    sendrecv_ = false;
    recvonly_ = false;
    sendonly_ = false;
    inactive_ = false;

    connection_ = "c=IN IP4 0.0.0.0";
}

SrsMediaDesc::~SrsMediaDesc()
{
}

SrsMediaPayloadType* SrsMediaDesc::find_media_with_payload_type(int payload_type)
{
    for (size_t i = 0; i < payload_types_.size(); ++i) {
        if (payload_types_[i].payload_type_ == payload_type) {
            return &payload_types_[i];
        }
    }

    return NULL;
}

vector<SrsMediaPayloadType> SrsMediaDesc::find_media_with_encoding_name(const std::string& encoding_name) const
{
    std::vector<SrsMediaPayloadType> payloads;

    std::string lower_name(encoding_name), upper_name(encoding_name);
    transform(encoding_name.begin(), encoding_name.end(), lower_name.begin(), ::tolower);
    transform(encoding_name.begin(), encoding_name.end(), upper_name.begin(), ::toupper);

    for (size_t i = 0; i < payload_types_.size(); ++i) {
        if (payload_types_[i].encoding_name_ == std::string(lower_name.c_str()) ||
            payload_types_[i].encoding_name_ == std::string(upper_name.c_str())) {
            payloads.push_back(payload_types_[i]);
        }
    }

    return payloads;
}

srs_error_t SrsMediaDesc::update_msid(string id)
{
    srs_error_t err = srs_success;

    for(vector<SrsSSRCInfo>::iterator it = ssrc_infos_.begin(); it != ssrc_infos_.end(); ++it) {
        SrsSSRCInfo& info = *it;

        info.msid_ = id;
        info.mslabel_ = id;
    }

    return err;
}

srs_error_t SrsMediaDesc::parse_line(const std::string& line)
{
    srs_error_t err = srs_success;
    std::string content = line.substr(2);

    switch (line[0]) {
        case 'a': {
            return parse_attribute(content);
        }
        case 'c': {
            // TODO: process c-line
            break;
        }
        default: {
            srs_trace("ignore media line=%s", line.c_str());
            break;
        }
    }

    return err;
}

srs_error_t SrsMediaDesc::encode(std::ostringstream& os)
{
    srs_error_t err = srs_success;

    os << "m=" << type_ << " " << port_ << " " << protos_;
    for (std::vector<SrsMediaPayloadType>::iterator iter = payload_types_.begin(); iter != payload_types_.end(); ++iter) {
        os << " " << iter->payload_type_;
    }

    os << kCRLF;

    // TODO:nettype and address type
    if (!connection_.empty()) os << connection_ << kCRLF;

    if ((err = session_info_.encode(os)) != srs_success) {
        return srs_error_wrap(err, "encode session info failed");
    }

    if (!mid_.empty()) os << "a=mid:" << mid_ << kCRLF;
    if (!msid_.empty()) {
        os << "a=msid:" << msid_;
        
        if (!msid_tracker_.empty()) {
            os << " " << msid_tracker_;
        }

        os << kCRLF;
    }

    for(map<int, string>::iterator it = extmaps_.begin(); it != extmaps_.end(); ++it) {
        os << "a=extmap:"<< it->first<< " "<< it->second<< kCRLF;
    }
    if (sendonly_) {
        os << "a=sendonly" << kCRLF;
    }
    if (recvonly_) {
        os << "a=recvonly" << kCRLF;
    }
    if (sendrecv_) {
        os << "a=sendrecv" << kCRLF;
    }
    if (inactive_) {
        os << "a=inactive" << kCRLF;
    }

    if (rtcp_mux_) {
        os << "a=rtcp-mux" << kCRLF;
    }

    if (rtcp_rsize_) {
        os << "a=rtcp-rsize" << kCRLF;
    }

    for (std::vector<SrsMediaPayloadType>::iterator iter = payload_types_.begin(); iter != payload_types_.end(); ++iter) {
        if ((err = iter->encode(os)) != srs_success) {
            return srs_error_wrap(err, "encode media payload failed");
        }
    }

    for (std::vector<SrsSSRCInfo>::iterator iter = ssrc_infos_.begin(); iter != ssrc_infos_.end(); ++iter) {
        SrsSSRCInfo& ssrc_info = *iter;

        if ((err = ssrc_info.encode(os)) != srs_success) {
            return srs_error_wrap(err, "encode ssrc failed");
        }
    }

    int foundation = 0;
    int component_id = 1; /* RTP */
    for (std::vector<SrsCandidate>::iterator iter = candidates_.begin(); iter != candidates_.end(); ++iter) {
        // @see: https://tools.ietf.org/html/draft-ietf-ice-rfc5245bis-00#section-4.2
        uint32_t priority = (1<<24)*(126) + (1<<8)*(65535) + (1)*(256 - component_id);

        // See ICE TCP at https://www.rfc-editor.org/rfc/rfc6544
        if (iter->protocol_ == "tcp") {
            os << "a=candidate:" << foundation++ << " "
               << component_id << " tcp " << priority << " "
               << iter->ip_ << " " << iter->port_
               << " typ " << iter->type_
               << " tcptype passive"
               << kCRLF;
            continue;
        }

        // @see: https://tools.ietf.org/id/draft-ietf-mmusic-ice-sip-sdp-14.html#rfc.section.5.1
        os << "a=candidate:" << foundation++ << " "
           << component_id << " udp " << priority << " "
           << iter->ip_ << " " << iter->port_
           << " typ " << iter->type_
           << " generation 0" << kCRLF;

        srs_verbose("local SDP candidate line=%s", os.str().c_str());
    }

    return err;
}

srs_error_t SrsMediaDesc::parse_attribute(const std::string& content)
{
    srs_error_t err = srs_success;
    std::string attribute = "";
    std::string value = "";
    size_t pos = content.find_first_of(":");

    if (pos != std::string::npos) {
        attribute = content.substr(0, pos);
        value = content.substr(pos + 1);
    } else {
        attribute = content;
    }

    if (attribute == "extmap") {
        return parse_attr_extmap(value);
    } else if (attribute == "rtpmap") {
        return parse_attr_rtpmap(value);
    } else if (attribute == "rtcp") {
        return parse_attr_rtcp(value);
    } else if (attribute == "rtcp-fb") {
        return parse_attr_rtcp_fb(value);
    } else if (attribute == "fmtp") {
        return parse_attr_fmtp(value);
    } else if (attribute == "mid") {
        return parse_attr_mid(value);
    } else if (attribute == "msid") {
        return parse_attr_msid(value);
    } else if (attribute == "ssrc") {
        return parse_attr_ssrc(value);
    } else if (attribute == "ssrc-group") {
        return parse_attr_ssrc_group(value);
    } else if (attribute == "rtcp-mux") {
        rtcp_mux_ = true;
    } else if (attribute == "rtcp-rsize") {
        rtcp_rsize_ = true;
    } else if (attribute == "recvonly") {
        recvonly_ = true;
    } else if (attribute == "sendonly") {
        sendonly_ = true;
    } else if (attribute == "sendrecv") {
        sendrecv_ = true;
    } else if (attribute == "inactive") {
        inactive_ = true;
    } else {
        return session_info_.parse_attribute(attribute, value);
    }

    return err;
}
srs_error_t SrsMediaDesc::parse_attr_extmap(const std::string& value)
{
    srs_error_t err = srs_success;
    std::istringstream is(value);
    int id = 0;
    FETCH(is, id);
    if(extmaps_.end() != extmaps_.find(id)) {
        return srs_error_new(ERROR_RTC_SDP_DECODE, "duplicate ext id: %d", id);
    }
    string ext;
    FETCH(is, ext);
    extmaps_[id] = ext;
    return err;
}

srs_error_t SrsMediaDesc::parse_attr_rtpmap(const std::string& value)
{
    srs_error_t err = srs_success;
    // @see: https://tools.ietf.org/html/rfc4566#page-25
    // a=rtpmap:<payload type> <encoding name>/<clock rate> [/<encoding parameters>]

    std::istringstream is(value);

    int payload_type = 0;
    FETCH(is, payload_type);

    SrsMediaPayloadType* payload = find_media_with_payload_type(payload_type);
    if (payload == NULL) {
        return srs_error_new(ERROR_RTC_SDP_DECODE, "can not find payload %d when pase rtpmap", payload_type);
    }

    std::string word;
    FETCH(is, word);

    std::vector<std::string> vec = split_str(word, "/");
    if (vec.size() < 2) {
        return srs_error_new(ERROR_RTC_SDP_DECODE, "invalid rtpmap line=%s", value.c_str());
    }

    payload->encoding_name_ = vec[0];
    payload->clock_rate_ = atoi(vec[1].c_str());

    if (vec.size() == 3) {
        payload->encoding_param_ = vec[2];
    }

    return err;
}

srs_error_t SrsMediaDesc::parse_attr_rtcp(const std::string& value)
{
    srs_error_t err = srs_success;

    // TODO:parse rtcp attribute

    return err;
}

srs_error_t SrsMediaDesc::parse_attr_rtcp_fb(const std::string& value)
{
    srs_error_t err = srs_success;
    // @see: https://tools.ietf.org/html/rfc5104#section-7.1

    std::istringstream is(value);

    int payload_type = 0;
    FETCH(is, payload_type);

    SrsMediaPayloadType* payload = find_media_with_payload_type(payload_type);
    if (payload == NULL) {
        return srs_error_new(ERROR_RTC_SDP_DECODE, "can not find payload %d when pase rtcp-fb", payload_type);
    }

    std::string rtcp_fb = is.str().substr(is.tellg());
    skip_first_spaces(rtcp_fb);

    payload->rtcp_fb_.push_back(rtcp_fb);

    return err;
}

srs_error_t SrsMediaDesc::parse_attr_fmtp(const std::string& value)
{
    srs_error_t err = srs_success;
    // @see: https://tools.ietf.org/html/rfc4566#page-30
    // a=fmtp:<format> <format specific parameters>

    std::istringstream is(value);

    int payload_type = 0;
    FETCH(is, payload_type);

    SrsMediaPayloadType* payload = find_media_with_payload_type(payload_type);
    if (payload == NULL) {
        return srs_error_new(ERROR_RTC_SDP_DECODE, "can not find payload %d when pase fmtp", payload_type);
    }

    std::string word;
    FETCH(is, word);

    payload->format_specific_param_ = word;

    return err;
}

srs_error_t SrsMediaDesc::parse_attr_mid(const std::string& value)
{
    // @see: https://tools.ietf.org/html/rfc3388#section-3
    srs_error_t err = srs_success;
    std::istringstream is(value);
    // mid_ means m-line id
    FETCH(is, mid_);
    srs_verbose("mid=%s", mid_.c_str());
    return err;
}

srs_error_t SrsMediaDesc::parse_attr_msid(const std::string& value)
{
    // @see: https://tools.ietf.org/id/draft-ietf-mmusic-msid-08.html#rfc.section.2
    // TODO: msid and msid_tracker
    srs_error_t err = srs_success;
    std::istringstream is(value);
    // msid_ means media stream id
    FETCH(is, msid_);
    is >> msid_tracker_;
    return err;
}

srs_error_t SrsMediaDesc::parse_attr_ssrc(const std::string& value)
{
    srs_error_t err = srs_success;
    // @see: https://tools.ietf.org/html/rfc5576#section-4.1

    std::istringstream is(value);

    uint32_t ssrc = 0;
    FETCH(is, ssrc);

    std::string ssrc_attr = "";
    FETCH_WITH_DELIM(is, ssrc_attr, ':');
    skip_first_spaces(ssrc_attr);

    std::string ssrc_value = is.str().substr(is.tellg());
    skip_first_spaces(ssrc_value);

    SrsSSRCInfo& ssrc_info = fetch_or_create_ssrc_info(ssrc);

    if (ssrc_attr == "cname") {
        // @see: https://tools.ietf.org/html/rfc5576#section-6.1
        ssrc_info.cname_ = ssrc_value;
        ssrc_info.ssrc_ = ssrc;
    } else if (ssrc_attr == "msid") {
        // @see: https://tools.ietf.org/html/draft-alvestrand-mmusic-msid-00#section-2
        std::vector<std::string> vec = split_str(ssrc_value, " ");
        if (vec.empty()) {
            return srs_error_new(ERROR_RTC_SDP_DECODE, "invalid ssrc line=%s", value.c_str());
        }

        ssrc_info.msid_ = vec[0];
        if (vec.size() > 1) {
            ssrc_info.msid_tracker_ = vec[1];
        }
    } else if (ssrc_attr == "mslabel") {
        ssrc_info.mslabel_ = ssrc_value;
    } else if (ssrc_attr == "label") {
        ssrc_info.label_ = ssrc_value;
    }

    return err;
}

srs_error_t SrsMediaDesc::parse_attr_ssrc_group(const std::string& value)
{
    srs_error_t err = srs_success;
    // @see: https://tools.ietf.org/html/rfc5576#section-4.2
    // a=ssrc-group:<semantics> <ssrc-id> ...

    std::istringstream is(value);

    std::string semantics;
    FETCH(is, semantics);

    std::string ssrc_ids = is.str().substr(is.tellg());
    skip_first_spaces(ssrc_ids);

    std::vector<std::string> vec = split_str(ssrc_ids, " ");
    if (vec.size() == 0) {
        return srs_error_new(ERROR_RTC_SDP_DECODE, "invalid ssrc-group line=%s", value.c_str());
    }

    std::vector<uint32_t> ssrcs;
    for (size_t i = 0; i < vec.size(); ++i) {
        std::istringstream in_stream(vec[i]);
        uint32_t ssrc = 0;
        in_stream >> ssrc;
        ssrcs.push_back(ssrc);
    }
    ssrc_groups_.push_back(SrsSSRCGroup(semantics, ssrcs));

    return err;
}

SrsSSRCInfo& SrsMediaDesc::fetch_or_create_ssrc_info(uint32_t ssrc)
{
    for (size_t i = 0; i < ssrc_infos_.size(); ++i) {
        if (ssrc_infos_[i].ssrc_ == ssrc) {
            return ssrc_infos_[i];
        }
    }

    SrsSSRCInfo ssrc_info;
    ssrc_info.ssrc_ = ssrc;
    ssrc_infos_.push_back(ssrc_info);

    return ssrc_infos_.back();
}

SrsSdp::SrsSdp()
{
    in_media_session_ = false;
    
    start_time_ = 0;
    end_time_ = 0;

    ice_lite_ = "a=ice-lite";
}

SrsSdp::~SrsSdp()
{
}

srs_error_t SrsSdp::parse(const std::string& sdp_str)
{
    srs_error_t err = srs_success;

    // All webrtc SrsSdp annotated example
    // @see: https://tools.ietf.org/html/draft-ietf-rtcweb-SrsSdp-11
    // Sdp example
    // session info
    // v=
    // o=
    // s=
    // t=
    // media description
    // m=
    // a=
    // ...
    // media description
    // m=
    // a=
    // ...
    std::istringstream is(sdp_str);
    std::string line;
    while (getline(is, line)) {
        srs_verbose("%s", line.c_str());
        if (line.size() < 2 || line[1] != '=') {
            return srs_error_new(ERROR_RTC_SDP_DECODE, "invalid sdp line=%s", line.c_str());
        }
        if (!line.empty() && line[line.size()-1] == '\r') {
            line.erase(line.size()-1, 1);
        }

        // Strip the space of line, for pion WebRTC client.
        line = srs_string_trim_end(line, " ");

        if ((err = parse_line(line)) != srs_success) {
            return srs_error_wrap(err, "parse sdp line failed");
        }
    }

    // The msid/tracker/mslabel is optional for SSRC, so we copy it when it's empty.
    for (std::vector<SrsMediaDesc>::iterator iter = media_descs_.begin(); iter != media_descs_.end(); ++iter) {
        SrsMediaDesc& media_desc = *iter;

        for (size_t i = 0; i < media_desc.ssrc_infos_.size(); ++i) {
            SrsSSRCInfo& ssrc_info = media_desc.ssrc_infos_.at(i);

            if (ssrc_info.msid_.empty()) {
                ssrc_info.msid_  = media_desc.msid_;
            }

            if (ssrc_info.msid_tracker_.empty()) {
                ssrc_info.msid_tracker_ = media_desc.msid_tracker_;
            }

            if (ssrc_info.mslabel_.empty()) {
                ssrc_info.mslabel_ = media_desc.msid_;
            }

            if (ssrc_info.label_.empty()) {
                ssrc_info.label_ = media_desc.msid_tracker_;
            }
        }
    }

    return err;
}

srs_error_t SrsSdp::encode(std::ostringstream& os)
{
    srs_error_t err = srs_success;

    os << "v=" << version_ << kCRLF;
    os << "o=" << username_ << " " << session_id_ << " " << session_version_ << " " << nettype_ << " " << addrtype_ << " " << unicast_address_ << kCRLF;
    os << "s=" << session_name_ << kCRLF;
    // Session level connection data, see https://www.ietf.org/rfc/rfc4566.html#section-5.7
    if (!connection_.empty()) os << connection_ << kCRLF;
    // Timing, see https://www.ietf.org/rfc/rfc4566.html#section-5.9
    os << "t=" << start_time_ << " " << end_time_ << kCRLF;
    // ice-lite is a minimal version of the ICE specification, intended for servers running on a public IP address.
    if (!ice_lite_.empty()) os << ice_lite_ << kCRLF;

    if (!groups_.empty()) {
        os << "a=group:" << group_policy_;
        for (std::vector<std::string>::iterator iter = groups_.begin(); iter != groups_.end(); ++iter) {
            os << " " << *iter;
        }
        os << kCRLF;
    }

    if (!msid_semantic_.empty() || !msids_.empty()) {
        os << "a=msid-semantic: " << msid_semantic_;
        for (std::vector<std::string>::iterator iter = msids_.begin(); iter != msids_.end(); ++iter) {
            os << " " << *iter;
        }
        os << kCRLF;
    }

    if ((err = session_info_.encode(os)) != srs_success) {
        return srs_error_wrap(err, "encode session info failed");
    }

    for (std::vector<SrsMediaDesc>::iterator iter = media_descs_.begin(); iter != media_descs_.end(); ++iter) {
        if ((err = (*iter).encode(os)) != srs_success) {
            return srs_error_wrap(err, "encode media description failed");
        }
    }

    return err;
}

std::vector<SrsMediaDesc*> SrsSdp::find_media_descs(const std::string& type)
{
    std::vector<SrsMediaDesc*> descs;
    for (std::vector<SrsMediaDesc>::iterator iter = media_descs_.begin(); iter != media_descs_.end(); ++iter) {
        SrsMediaDesc* desc = &(*iter);

        if (desc->type_ == type) {
            descs.push_back(desc);
        }
    }

    return descs;
}

void SrsSdp::set_ice_ufrag(const std::string& ufrag)
{
    for (std::vector<SrsMediaDesc>::iterator iter = media_descs_.begin(); iter != media_descs_.end(); ++iter) {
        SrsMediaDesc* desc = &(*iter);
        desc->session_info_.ice_ufrag_ = ufrag;
    }
}

void SrsSdp::set_ice_pwd(const std::string& pwd)
{
    for (std::vector<SrsMediaDesc>::iterator iter = media_descs_.begin(); iter != media_descs_.end(); ++iter) {
        SrsMediaDesc* desc = &(*iter);
        desc->session_info_.ice_pwd_ = pwd;
    }
}

void SrsSdp::set_dtls_role(const std::string& dtls_role)
{
    for (std::vector<SrsMediaDesc>::iterator iter = media_descs_.begin(); iter != media_descs_.end(); ++iter) {
        SrsMediaDesc* desc = &(*iter);
        desc->session_info_.setup_ = dtls_role;
    }
}

void SrsSdp::set_fingerprint_algo(const std::string& algo)
{
    for (std::vector<SrsMediaDesc>::iterator iter = media_descs_.begin(); iter != media_descs_.end(); ++iter) {
        SrsMediaDesc* desc = &(*iter);
        desc->session_info_.fingerprint_algo_ = algo;
    }
}

void SrsSdp::set_fingerprint(const std::string& fingerprint)
{
    for (std::vector<SrsMediaDesc>::iterator iter = media_descs_.begin(); iter != media_descs_.end(); ++iter) {
        SrsMediaDesc* desc = &(*iter);
        desc->session_info_.fingerprint_ = fingerprint;
    }
}

void SrsSdp::add_candidate(const std::string& protocol, const std::string& ip, const int& port, const std::string& type)
{
    // @see: https://tools.ietf.org/id/draft-ietf-mmusic-ice-sip-sdp-14.html#rfc.section.5.1
    SrsCandidate candidate;
    candidate.protocol_ = protocol;
    candidate.ip_ = ip;
    candidate.port_ = port;
    candidate.type_ = type;

    for (std::vector<SrsMediaDesc>::iterator iter = media_descs_.begin(); iter != media_descs_.end(); ++iter) {
        SrsMediaDesc* desc = &(*iter);
        desc->candidates_.push_back(candidate);
    }
}

std::string SrsSdp::get_ice_ufrag() const
{
    // For OBS WHIP, use the global ice-ufrag.
    if (!session_info_.ice_ufrag_.empty()) {
        return session_info_.ice_ufrag_;
    }

    // Because we use BUNDLE, so we can choose the first element.
    for (std::vector<SrsMediaDesc>::const_iterator iter = media_descs_.begin(); iter != media_descs_.end(); ++iter) {
        const SrsMediaDesc* desc = &(*iter);
        return desc->session_info_.ice_ufrag_;
    }

    return "";
}

std::string SrsSdp::get_ice_pwd() const
{
    // For OBS WHIP, use the global ice pwd.
    if (!session_info_.ice_pwd_.empty()) {
        return session_info_.ice_pwd_;
    }

    // Because we use BUNDLE, so we can choose the first element.
    for (std::vector<SrsMediaDesc>::const_iterator iter = media_descs_.begin(); iter != media_descs_.end(); ++iter) {
        const SrsMediaDesc* desc = &(*iter);
        return desc->session_info_.ice_pwd_;
    }

    return "";
}

std::string SrsSdp::get_dtls_role() const
{
    // Because we use BUNDLE, so we can choose the first element.
    for (std::vector<SrsMediaDesc>::const_iterator iter = media_descs_.begin(); iter != media_descs_.end(); ++iter) {
        const SrsMediaDesc* desc = &(*iter);
        return desc->session_info_.setup_;
    }

    return "";
}

srs_error_t SrsSdp::parse_line(const std::string& line)
{
    srs_error_t err = srs_success;

    std::string content = line.substr(2);

    switch (line[0]) {
        case 'o': {
            return parse_origin(content);
        }
        case 'v': {
            return parse_version(content);
        }
        case 's': {
            return parse_session_name(content);
        }
        case 't': {
            return parse_timing(content);
        }
        case 'a': {
            if (in_media_session_) {
                return media_descs_.back().parse_line(line);
            }
            return parse_attribute(content);
        }
        case 'y': {
            return parse_gb28181_ssrc(content);
        }
        case 'm': {
            return parse_media_description(content);
        }
        case 'c': {
            // TODO: process c-line
            break;
        }
        default: {
            srs_trace("ignore sdp line=%s", line.c_str());
            break;
        }
    }

    return err;
}

srs_error_t SrsSdp::parse_origin(const std::string& content)
{
    srs_error_t err = srs_success;

    // @see: https://tools.ietf.org/html/rfc4566#section-5.2
    // o=<username> <sess-id> <sess-version> <nettype> <addrtype> <unicast-address>
    // eg. o=- 9164462281920464688 2 IN IP4 127.0.0.1
    std::istringstream is(content);

    FETCH(is, username_);
    FETCH(is, session_id_);
    FETCH(is, session_version_);
    FETCH(is, nettype_);
    FETCH(is, addrtype_);
    FETCH(is, unicast_address_);

    return err;
}

srs_error_t SrsSdp::parse_version(const std::string& content)
{
    srs_error_t err = srs_success;
    // @see: https://tools.ietf.org/html/rfc4566#section-5.1

    std::istringstream is(content);

    FETCH(is, version_);

    return err;
}

srs_error_t SrsSdp::parse_session_name(const std::string& content)
{
    srs_error_t err = srs_success;
    // @see: https://tools.ietf.org/html/rfc4566#section-5.3
    // s=<session name>

    session_name_ = content;

    return err;
}

srs_error_t SrsSdp::parse_timing(const std::string& content)
{
    srs_error_t err = srs_success;
    // @see: https://tools.ietf.org/html/rfc4566#section-5.9
    // t=<start-time> <stop-time>
    
    std::istringstream is(content);

    FETCH(is, start_time_);
    FETCH(is, end_time_);

    return err;
}

srs_error_t SrsSdp::parse_attribute(const std::string& content)
{
    srs_error_t err = srs_success;
    // @see: https://tools.ietf.org/html/rfc4566#section-5.13
    // a=<attribute>
    // a=<attribute>:<value>

    std::string attribute = "";
    std::string value = "";
    size_t pos = content.find_first_of(":");

    if (pos != std::string::npos) {
        attribute = content.substr(0, pos);
        value = content.substr(pos + 1);
    }

    if (attribute == "group") {
        return parse_attr_group(value);
    } else if (attribute == "msid-semantic") {
        std::istringstream is(value);
        FETCH(is, msid_semantic_);

        std::string msid;
        while (is >> msid) {
            msids_.push_back(msid);
        }
    } else {
        return session_info_.parse_attribute(attribute, value);
    }

    return err;
}

srs_error_t SrsSdp::parse_gb28181_ssrc(const std::string& content)
{
    srs_error_t err = srs_success;

    // See AnnexF at page 101 of https://openstd.samr.gov.cn/bzgk/gb/newGbInfo?hcno=469659DC56B9B8187671FF08748CEC89
    // Convert SSRC of GB28181 from:
    //      y=0100008888
    // to standard format:
    //      a=ssrc:0100008888 cname:0100008888
    //      a=ssrc:0100008888 label:gb28181
    string cname = srs_fmt("a=ssrc:%s cname:%s", content.c_str(), content.c_str());
    if ((err = media_descs_.back().parse_line(cname)) != srs_success) {
        return srs_error_wrap(err, "parse gb %s cname", content.c_str());
    }

    string label = srs_fmt("a=ssrc:%s label:gb28181", content.c_str());
    if ((err = media_descs_.back().parse_line(label)) != srs_success) {
        return srs_error_wrap(err, "parse gb %s label", content.c_str());
    }

    return err;
}

srs_error_t SrsSdp::parse_attr_group(const std::string& value)
{
    srs_error_t err = srs_success;
    // @see: https://tools.ietf.org/html/rfc5888#section-5

    // Overlook the OBS WHIP group LS, as it is utilized for synchronizing the playback of
    // the relevant media streams, see https://datatracker.ietf.org/doc/html/rfc5888#section-7
    if (srs_string_starts_with(value, "LS")) {
        return err;
    }

    std::istringstream is(value);

    FETCH(is, group_policy_);

    std::string word;
    while (is >> word) {
        groups_.push_back(word);
    }

    return err;
}

srs_error_t SrsSdp::parse_media_description(const std::string& content)
{
    srs_error_t err = srs_success;

    // @see: https://tools.ietf.org/html/rfc4566#section-5.14
    // m=<media> <port> <proto> <fmt> ...
    // m=<media> <port>/<number of ports> <proto> <fmt> ...
    std::istringstream is(content);

    std::string media;
    FETCH(is, media);

    int port;
    FETCH(is, port);

    std::string proto;
    FETCH(is, proto);

    media_descs_.push_back(SrsMediaDesc(media));
    media_descs_.back().protos_ = proto;
    media_descs_.back().port_ = port;

    int fmt;
    while (is >> fmt) {
        media_descs_.back().payload_types_.push_back(SrsMediaPayloadType(fmt));
    }

    if (!in_media_session_) {
        in_media_session_ = true;
    }

    return err;
}

bool SrsSdp::is_unified() const
{
    // TODO: FIXME: Maybe we should consider other situations.
    return media_descs_.size() > 2;
}

srs_error_t SrsSdp::update_msid(string id)
{
    srs_error_t err = srs_success;

    msids_.clear();
    msids_.push_back(id);

    for (vector<SrsMediaDesc>::iterator it = media_descs_.begin(); it != media_descs_.end(); ++it) {
        SrsMediaDesc& desc = *it;

        if ((err = desc.update_msid(id)) != srs_success) {
            return srs_error_wrap(err, "desc %s update msid %s", desc.mid_.c_str(), id.c_str());
        }
    }

    return err;
}

