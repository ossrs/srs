/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2020 Winlin
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

#include <srs_app_rtc_api.hpp>

#include <srs_app_rtc_conn.hpp>
#include <srs_app_rtc_server.hpp>
#include <srs_protocol_json.hpp>
#include <srs_core_autofree.hpp>
#include <srs_app_http_api.hpp>
#include <srs_protocol_utility.hpp>
#include <srs_app_config.hpp>
#include <srs_app_statistic.hpp>

#include <unistd.h>
#include <deque>
using namespace std;

uint32_t SrsGoApiRtcPlay::ssrc_num = 0;

SrsGoApiRtcPlay::SrsGoApiRtcPlay(SrsRtcServer* server)
{
    server_ = server;
}

SrsGoApiRtcPlay::~SrsGoApiRtcPlay()
{
}


// Request:
//      POST /rtc/v1/play/
//      {
//          "sdp":"offer...", "streamurl":"webrtc://r.ossrs.net/live/livestream",
//          "api":'http...", "clientip":"..."
//      }
// Response:
//      {"sdp":"answer...", "sid":"..."}
// @see https://github.com/rtcdn/rtcdn-draft
srs_error_t SrsGoApiRtcPlay::serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r)
{
    srs_error_t err = srs_success;

    SrsJsonObject* res = SrsJsonAny::object();
    SrsAutoFree(SrsJsonObject, res);

    if ((err = do_serve_http(w, r, res)) != srs_success) {
        srs_warn("RTC error %s", srs_error_desc(err).c_str()); srs_freep(err);
        return srs_api_response_code(w, r, SRS_CONSTS_HTTP_BadRequest);
    }

    return srs_api_response(w, r, res->dumps());
}

srs_error_t SrsGoApiRtcPlay::do_serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r, SrsJsonObject* res)
{
    srs_error_t err = srs_success;

    // For each RTC session, we use short-term HTTP connection.
    SrsHttpHeader* hdr = w->header();
    hdr->set("Connection", "Close");

    // Parse req, the request json object, from body.
    SrsJsonObject* req = NULL;
    SrsAutoFree(SrsJsonObject, req);
    if (true) {
        string req_json;
        if ((err = r->body_read_all(req_json)) != srs_success) {
            return srs_error_wrap(err, "read body");
        }

        SrsJsonAny* json = SrsJsonAny::loads(req_json);
        if (!json || !json->is_object()) {
            return srs_error_new(ERROR_RTC_API_BODY, "invalid body %s", req_json.c_str());
        }

        req = json->to_object();
    }

    // Fetch params from req object.
    SrsJsonAny* prop = NULL;
    if ((prop = req->ensure_property_string("sdp")) == NULL) {
        return srs_error_wrap(err, "not sdp");
    }
    string remote_sdp_str = prop->to_str();

    if ((prop = req->ensure_property_string("streamurl")) == NULL) {
        return srs_error_wrap(err, "not streamurl");
    }
    string streamurl = prop->to_str();

    string clientip;
    if ((prop = req->ensure_property_string("clientip")) != NULL) {
        clientip = prop->to_str();
    }

    string api;
    if ((prop = req->ensure_property_string("api")) != NULL) {
        api = prop->to_str();
    }

    // TODO: FIXME: Parse vhost.
    // Parse app and stream from streamurl.
    string app;
    string stream_name;
    if (true) {
        string tcUrl;
        srs_parse_rtmp_url(streamurl, tcUrl, stream_name);

        int port;
        string schema, host, vhost, param;
        srs_discovery_tc_url(tcUrl, schema, host, vhost, app, stream_name, port, param);
    }

    // For client to specifies the EIP of server.
    string eip = r->query_get("eip");
    // For client to specifies whether encrypt by SRTP.
    string encrypt = r->query_get("encrypt");

    srs_trace("RTC play %s, api=%s, clientip=%s, app=%s, stream=%s, offer=%dB, eip=%s, encrypt=%s",
        streamurl.c_str(), api.c_str(), clientip.c_str(), app.c_str(), stream_name.c_str(), remote_sdp_str.length(),
        eip.c_str(), encrypt.c_str());

    // TODO: FIXME: It seems remote_sdp doesn't represents the full SDP information.
    SrsSdp remote_sdp;
    if ((err = remote_sdp.parse(remote_sdp_str)) != srs_success) {
        return srs_error_wrap(err, "parse sdp failed: %s", remote_sdp_str.c_str());
    }

    if ((err = check_remote_sdp(remote_sdp)) != srs_success) {
        return srs_error_wrap(err, "remote sdp check failed");
    }

    SrsRequest request;
    request.app = app;
    request.stream = stream_name;

    // TODO: FIXME: Parse vhost.
    // discovery vhost, resolve the vhost from config
    SrsConfDirective* parsed_vhost = _srs_config->get_vhost("");
    if (parsed_vhost) {
        request.vhost = parsed_vhost->arg0();
    }

    SrsSdp local_sdp;
    if ((err = exchange_sdp(&request, remote_sdp, local_sdp)) != srs_success) {
        return srs_error_wrap(err, "remote sdp have error or unsupport attributes");
    }

    // Whether enabled.
    bool server_enabled = _srs_config->get_rtc_server_enabled();
    bool rtc_enabled = _srs_config->get_rtc_enabled(request.vhost);
    if (server_enabled && !rtc_enabled) {
        srs_warn("RTC disabled in vhost %s", request.vhost.c_str());
    }
    if (!server_enabled || !rtc_enabled) {
        return srs_error_new(ERROR_RTC_DISABLED, "Disabled server=%d, rtc=%d, vhost=%s",
            server_enabled, rtc_enabled, request.vhost.c_str());
    }

    // TODO: FIXME: When server enabled, but vhost disabled, should report error.
    SrsRtcSession* session = NULL;
    if ((err = server_->create_session(&request, remote_sdp, local_sdp, eip, false, &session)) != srs_success) {
        return srs_error_wrap(err, "create session");
    }
    if (encrypt.empty()) {
        session->set_encrypt(_srs_config->get_rtc_server_encrypt());
    } else {
        session->set_encrypt(encrypt != "false");
    }

    ostringstream os;
    if ((err = local_sdp.encode(os)) != srs_success) {
        return srs_error_wrap(err, "encode sdp");
    }

    string local_sdp_str = os.str();

    srs_verbose("local_sdp=%s", local_sdp_str.c_str());

    res->set("code", SrsJsonAny::integer(ERROR_SUCCESS));
    res->set("server", SrsJsonAny::integer(SrsStatistic::instance()->server_id()));

    // TODO: add candidates in response json?

    res->set("sdp", SrsJsonAny::str(local_sdp_str.c_str()));
    res->set("sessionid", SrsJsonAny::str(session->username().c_str()));

    srs_trace("RTC username=%s, offer=%dB, answer=%dB", session->username().c_str(),
        remote_sdp_str.length(), local_sdp_str.length());

    return err;
}

srs_error_t SrsGoApiRtcPlay::check_remote_sdp(const SrsSdp& remote_sdp)
{
    srs_error_t err = srs_success;

    if (remote_sdp.group_policy_ != "BUNDLE") {
        return srs_error_new(ERROR_RTC_SDP_EXCHANGE, "now only support BUNDLE, group policy=%s", remote_sdp.group_policy_.c_str());
    }

    if (remote_sdp.media_descs_.empty()) {
        return srs_error_new(ERROR_RTC_SDP_EXCHANGE, "no media descriptions");
    }

    for (std::vector<SrsMediaDesc>::const_iterator iter = remote_sdp.media_descs_.begin(); iter != remote_sdp.media_descs_.end(); ++iter) {
        if (iter->type_ != "audio" && iter->type_ != "video") {
            return srs_error_new(ERROR_RTC_SDP_EXCHANGE, "unsupport media type=%s", iter->type_.c_str());
        }

        if (! iter->rtcp_mux_) {
            return srs_error_new(ERROR_RTC_SDP_EXCHANGE, "now only suppor rtcp-mux");
        }

        for (std::vector<SrsMediaPayloadType>::const_iterator iter_media = iter->payload_types_.begin(); iter_media != iter->payload_types_.end(); ++iter_media) {
            if (iter->sendonly_) {
                return srs_error_new(ERROR_RTC_SDP_EXCHANGE, "play API only support sendrecv/recvonly");
            }
        }
    }

    return err;
}

srs_error_t SrsGoApiRtcPlay::exchange_sdp(SrsRequest* req, const SrsSdp& remote_sdp, SrsSdp& local_sdp)
{
    srs_error_t err = srs_success;
    local_sdp.version_ = "0";

    local_sdp.username_        = RTMP_SIG_SRS_SERVER;
    local_sdp.session_id_      = srs_int2str((int64_t)this);
    local_sdp.session_version_ = "2";
    local_sdp.nettype_         = "IN";
    local_sdp.addrtype_        = "IP4";
    local_sdp.unicast_address_ = "0.0.0.0";

    local_sdp.session_name_ = "SRSPlaySession";

    local_sdp.msid_semantic_ = "WMS";
    local_sdp.msids_.push_back(req->app + "/" + req->stream);

    local_sdp.group_policy_ = "BUNDLE";

    bool nack_enabled = _srs_config->get_rtc_nack_enabled(req->vhost);

    for (size_t i = 0; i < remote_sdp.media_descs_.size(); ++i) {
        const SrsMediaDesc& remote_media_desc = remote_sdp.media_descs_[i];

        if (remote_media_desc.is_audio()) {
            local_sdp.media_descs_.push_back(SrsMediaDesc("audio"));
        } else if (remote_media_desc.is_video()) {
            local_sdp.media_descs_.push_back(SrsMediaDesc("video"));
        }

        SrsMediaDesc& local_media_desc = local_sdp.media_descs_.back();

        if (remote_media_desc.is_audio()) {
            // TODO: check opus format specific param
            std::vector<SrsMediaPayloadType> payloads = remote_media_desc.find_media_with_encoding_name("opus");
            for (std::vector<SrsMediaPayloadType>::iterator iter = payloads.begin(); iter != payloads.end(); ++iter) {
                local_media_desc.payload_types_.push_back(*iter);
                SrsMediaPayloadType& payload_type = local_media_desc.payload_types_.back();

                // TODO: FIXME: Only support some transport algorithms.
                vector<string> rtcp_fb;
                payload_type.rtcp_fb_.swap(rtcp_fb);
                for (int j = 0; j < (int)rtcp_fb.size(); j++) {
                    if (nack_enabled) {
                        if (rtcp_fb.at(j) == "nack" || rtcp_fb.at(j) == "nack pli") {
                            payload_type.rtcp_fb_.push_back(rtcp_fb.at(j));
                        }
                    }
                }

                // Only choose one match opus codec.
                break;
            }

            if (local_media_desc.payload_types_.empty()) {
                return srs_error_new(ERROR_RTC_SDP_EXCHANGE, "no found valid opus payload type");
            }
        } else if (remote_media_desc.is_video()) {
            std::deque<SrsMediaPayloadType> backup_payloads;
            std::vector<SrsMediaPayloadType> payloads = remote_media_desc.find_media_with_encoding_name("H264");
            for (std::vector<SrsMediaPayloadType>::iterator iter = payloads.begin(); iter != payloads.end(); ++iter) {
                if (iter->format_specific_param_.empty()) {
                    backup_payloads.push_front(*iter);
                    continue;
                }
                H264SpecificParam h264_param;
                if ((err = srs_parse_h264_fmtp(iter->format_specific_param_, h264_param)) != srs_success) {
                    srs_error_reset(err); continue;
                }

                // Try to pick the "best match" H.264 payload type.
                if (h264_param.packetization_mode == "1" && h264_param.level_asymmerty_allow == "1") {
                    local_media_desc.payload_types_.push_back(*iter);
                    SrsMediaPayloadType& payload_type = local_media_desc.payload_types_.back();

                    // TODO: FIXME: Only support some transport algorithms.
                    vector<string> rtcp_fb;
                    payload_type.rtcp_fb_.swap(rtcp_fb);
                    for (int j = 0; j < (int)rtcp_fb.size(); j++) {
                        if (nack_enabled) {
                            if (rtcp_fb.at(j) == "nack" || rtcp_fb.at(j) == "nack pli") {
                                payload_type.rtcp_fb_.push_back(rtcp_fb.at(j));
                            }
                        }
                    }

                    // Only choose first match H.264 payload type.
                    break;
                }

                backup_payloads.push_back(*iter);
            }

            // Try my best to pick at least one media payload type.
            if (local_media_desc.payload_types_.empty() && ! backup_payloads.empty()) {
                srs_warn("choose backup H.264 payload type=%d", backup_payloads.front().payload_type_);
                local_media_desc.payload_types_.push_back(backup_payloads.front());
            }

            if (local_media_desc.payload_types_.empty()) {
                return srs_error_new(ERROR_RTC_SDP_EXCHANGE, "no found valid H.264 payload type");
            }
        }

        local_media_desc.mid_ = remote_media_desc.mid_;
        local_sdp.groups_.push_back(local_media_desc.mid_);

        local_media_desc.port_ = 9;
        local_media_desc.protos_ = "UDP/TLS/RTP/SAVPF";

        if (remote_media_desc.session_info_.setup_ == "active") {
            local_media_desc.session_info_.setup_ = "passive";
        } else if (remote_media_desc.session_info_.setup_ == "passive") {
            local_media_desc.session_info_.setup_ = "active";
        } else if (remote_media_desc.session_info_.setup_ == "actpass") {
            local_media_desc.session_info_.setup_ = "passive";
        } else {
            // @see: https://tools.ietf.org/html/rfc4145#section-4.1
            // The default value of the setup attribute in an offer/answer exchange
            // is 'active' in the offer and 'passive' in the answer.
            local_media_desc.session_info_.setup_ = "passive";
        }

        if (remote_media_desc.sendonly_) {
            local_media_desc.recvonly_ = true;
        } else if (remote_media_desc.recvonly_) {
            local_media_desc.sendonly_ = true;
        } else if (remote_media_desc.sendrecv_) {
            local_media_desc.sendrecv_ = true;
        }

        local_media_desc.rtcp_mux_ = true;
        local_media_desc.rtcp_rsize_ = true;

        // TODO: FIXME: Avoid SSRC collision.
        if (!ssrc_num) {
            ssrc_num = ::getpid() * 10000 + ::getpid() * 100 + ::getpid();
        }

        if (local_media_desc.sendonly_ || local_media_desc.sendrecv_) {
            SrsSSRCInfo ssrc_info;
            ssrc_info.ssrc_ = ++ssrc_num;
            // TODO:use formated cname
            ssrc_info.cname_ = "test_sdp_cname";
            local_media_desc.ssrc_infos_.push_back(ssrc_info);
        }
    }

    return err;
}

uint32_t SrsGoApiRtcPublish::ssrc_num = 0;

SrsGoApiRtcPublish::SrsGoApiRtcPublish(SrsRtcServer* server)
{
    server_ = server;
}

SrsGoApiRtcPublish::~SrsGoApiRtcPublish()
{
}


// Request:
//      POST /rtc/v1/publish/
//      {
//          "sdp":"offer...", "streamurl":"webrtc://r.ossrs.net/live/livestream",
//          "api":'http...", "clientip":"..."
//      }
// Response:
//      {"sdp":"answer...", "sid":"..."}
// @see https://github.com/rtcdn/rtcdn-draft
srs_error_t SrsGoApiRtcPublish::serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r)
{
    srs_error_t err = srs_success;

    SrsJsonObject* res = SrsJsonAny::object();
    SrsAutoFree(SrsJsonObject, res);

    if ((err = do_serve_http(w, r, res)) != srs_success) {
        srs_warn("RTC error %s", srs_error_desc(err).c_str()); srs_freep(err);
        return srs_api_response_code(w, r, SRS_CONSTS_HTTP_BadRequest);
    }

    return srs_api_response(w, r, res->dumps());
}

srs_error_t SrsGoApiRtcPublish::do_serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r, SrsJsonObject* res)
{
    srs_error_t err = srs_success;

    // For each RTC session, we use short-term HTTP connection.
    SrsHttpHeader* hdr = w->header();
    hdr->set("Connection", "Close");

    // Parse req, the request json object, from body.
    SrsJsonObject* req = NULL;
    if (true) {
        string req_json;
        if ((err = r->body_read_all(req_json)) != srs_success) {
            return srs_error_wrap(err, "read body");
        }

        SrsJsonAny* json = SrsJsonAny::loads(req_json);
        if (!json || !json->is_object()) {
            return srs_error_new(ERROR_RTC_API_BODY, "invalid body %s", req_json.c_str());
        }

        req = json->to_object();
    }

    // Fetch params from req object.
    SrsJsonAny* prop = NULL;
    if ((prop = req->ensure_property_string("sdp")) == NULL) {
        return srs_error_wrap(err, "not sdp");
    }
    string remote_sdp_str = prop->to_str();

    if ((prop = req->ensure_property_string("streamurl")) == NULL) {
        return srs_error_wrap(err, "not streamurl");
    }
    string streamurl = prop->to_str();

    string clientip;
    if ((prop = req->ensure_property_string("clientip")) != NULL) {
        clientip = prop->to_str();
    }

    string api;
    if ((prop = req->ensure_property_string("api")) != NULL) {
        api = prop->to_str();
    }

    // Parse app and stream from streamurl.
    string app;
    string stream_name;
    if (true) {
        string tcUrl;
        srs_parse_rtmp_url(streamurl, tcUrl, stream_name);

        int port;
        string schema, host, vhost, param;
        srs_discovery_tc_url(tcUrl, schema, host, vhost, app, stream_name, port, param);
    }

    // For client to specifies the EIP of server.
    string eip = r->query_get("eip");

    srs_trace("RTC publish %s, api=%s, clientip=%s, app=%s, stream=%s, offer=%dB, eip=%s",
        streamurl.c_str(), api.c_str(), clientip.c_str(), app.c_str(), stream_name.c_str(), remote_sdp_str.length(), eip.c_str());

    // TODO: FIXME: It seems remote_sdp doesn't represents the full SDP information.
    SrsSdp remote_sdp;
    if ((err = remote_sdp.parse(remote_sdp_str)) != srs_success) {
        return srs_error_wrap(err, "parse sdp failed: %s", remote_sdp_str.c_str());
    }

    if ((err = check_remote_sdp(remote_sdp)) != srs_success) {
        return srs_error_wrap(err, "remote sdp check failed");
    }

    SrsRequest request;
    request.app = app;
    request.stream = stream_name;

    // TODO: FIXME: Parse vhost.
    // discovery vhost, resolve the vhost from config
    SrsConfDirective* parsed_vhost = _srs_config->get_vhost("");
    if (parsed_vhost) {
        request.vhost = parsed_vhost->arg0();
    }

    SrsSdp local_sdp;
    if ((err = exchange_sdp(&request, remote_sdp, local_sdp)) != srs_success) {
        return srs_error_wrap(err, "remote sdp have error or unsupport attributes");
    }

    // Whether enabled.
    bool server_enabled = _srs_config->get_rtc_server_enabled();
    bool rtc_enabled = _srs_config->get_rtc_enabled(request.vhost);
    if (server_enabled && !rtc_enabled) {
        srs_warn("RTC disabled in vhost %s", request.vhost.c_str());
    }
    if (!server_enabled || !rtc_enabled) {
        return srs_error_new(ERROR_RTC_DISABLED, "Disabled server=%d, rtc=%d, vhost=%s",
            server_enabled, rtc_enabled, request.vhost.c_str());
    }

    // TODO: FIXME: When server enabled, but vhost disabled, should report error.
    SrsRtcSession* session = NULL;
    if ((err = server_->create_session(&request, remote_sdp, local_sdp, eip, true, &session)) != srs_success) {
        return srs_error_wrap(err, "create session");
    }

    ostringstream os;
    if ((err = local_sdp.encode(os)) != srs_success) {
        return srs_error_wrap(err, "encode sdp");
    }

    string local_sdp_str = os.str();

    srs_verbose("local_sdp=%s", local_sdp_str.c_str());

    res->set("code", SrsJsonAny::integer(ERROR_SUCCESS));
    res->set("server", SrsJsonAny::integer(SrsStatistic::instance()->server_id()));

    // TODO: add candidates in response json?

    res->set("sdp", SrsJsonAny::str(local_sdp_str.c_str()));
    res->set("sessionid", SrsJsonAny::str(session->username().c_str()));

    srs_trace("RTC username=%s, offer=%dB, answer=%dB", session->username().c_str(),
        remote_sdp_str.length(), local_sdp_str.length());

    return err;
}

srs_error_t SrsGoApiRtcPublish::check_remote_sdp(const SrsSdp& remote_sdp)
{
    srs_error_t err = srs_success;

    if (remote_sdp.group_policy_ != "BUNDLE") {
        return srs_error_new(ERROR_RTC_SDP_EXCHANGE, "now only support BUNDLE, group policy=%s", remote_sdp.group_policy_.c_str());
    }

    if (remote_sdp.media_descs_.empty()) {
        return srs_error_new(ERROR_RTC_SDP_EXCHANGE, "no media descriptions");
    }

    for (std::vector<SrsMediaDesc>::const_iterator iter = remote_sdp.media_descs_.begin(); iter != remote_sdp.media_descs_.end(); ++iter) {
        if (iter->type_ != "audio" && iter->type_ != "video") {
            return srs_error_new(ERROR_RTC_SDP_EXCHANGE, "unsupport media type=%s", iter->type_.c_str());
        }

        if (! iter->rtcp_mux_) {
            return srs_error_new(ERROR_RTC_SDP_EXCHANGE, "now only suppor rtcp-mux");
        }

        for (std::vector<SrsMediaPayloadType>::const_iterator iter_media = iter->payload_types_.begin(); iter_media != iter->payload_types_.end(); ++iter_media) {
            if (iter->recvonly_) {
                return srs_error_new(ERROR_RTC_SDP_EXCHANGE, "publish API only support sendrecv/sendonly");
            }
        }
    }

    return err;
}

srs_error_t SrsGoApiRtcPublish::exchange_sdp(SrsRequest* req, const SrsSdp& remote_sdp, SrsSdp& local_sdp)
{
    srs_error_t err = srs_success;
    local_sdp.version_ = "0";

    local_sdp.username_        = RTMP_SIG_SRS_SERVER;
    local_sdp.session_id_      = srs_int2str((int64_t)this);
    local_sdp.session_version_ = "2";
    local_sdp.nettype_         = "IN";
    local_sdp.addrtype_        = "IP4";
    local_sdp.unicast_address_ = "0.0.0.0";

    local_sdp.session_name_ = "SRSPublishSession";

    local_sdp.msid_semantic_ = "WMS";
    local_sdp.msids_.push_back(req->app + "/" + req->stream);

    local_sdp.group_policy_ = "BUNDLE";

    bool nack_enabled = _srs_config->get_rtc_nack_enabled(req->vhost);
    bool twcc_enabled = _srs_config->get_rtc_twcc_enabled(req->vhost);

    for (size_t i = 0; i < remote_sdp.media_descs_.size(); ++i) {
        const SrsMediaDesc& remote_media_desc = remote_sdp.media_descs_[i];

        if (remote_media_desc.is_audio()) {
            local_sdp.media_descs_.push_back(SrsMediaDesc("audio"));
        } else if (remote_media_desc.is_video()) {
            local_sdp.media_descs_.push_back(SrsMediaDesc("video"));
        }

        SrsMediaDesc& local_media_desc = local_sdp.media_descs_.back();

        if (remote_media_desc.is_audio()) {
            // TODO: check opus format specific param
            std::vector<SrsMediaPayloadType> payloads = remote_media_desc.find_media_with_encoding_name("opus");
            for (std::vector<SrsMediaPayloadType>::iterator iter = payloads.begin(); iter != payloads.end(); ++iter) {
                local_media_desc.payload_types_.push_back(*iter);
                SrsMediaPayloadType& payload_type = local_media_desc.payload_types_.back();

                // TODO: FIXME: Only support some transport algorithms.
                vector<string> rtcp_fb;
                payload_type.rtcp_fb_.swap(rtcp_fb);
                for (int j = 0; j < (int)rtcp_fb.size(); j++) {
                    if (nack_enabled) {
                        if (rtcp_fb.at(j) == "nack" || rtcp_fb.at(j) == "nack pli") {
                            payload_type.rtcp_fb_.push_back(rtcp_fb.at(j));
                        }
                    }
                    if (twcc_enabled) {
                        if (rtcp_fb.at(j) == "transport-cc") {
                            payload_type.rtcp_fb_.push_back(rtcp_fb.at(j));
                        }
                    }
                }

                // Only choose one match opus codec.
                break;
            }
            map<int, string> extmaps = remote_media_desc.get_extmaps();
            for(map<int, string>::iterator it_ext = extmaps.begin(); it_ext != extmaps.end(); ++it_ext) {
                if (it_ext->second == kTWCCExt) {
                    local_media_desc.extmaps_[it_ext->first] = kTWCCExt;
                    break;
                }
            }

            if (local_media_desc.payload_types_.empty()) {
                return srs_error_new(ERROR_RTC_SDP_EXCHANGE, "no valid found opus payload type");
            }

        } else if (remote_media_desc.is_video()) {
            std::deque<SrsMediaPayloadType> backup_payloads;
            std::vector<SrsMediaPayloadType> payloads = remote_media_desc.find_media_with_encoding_name("H264");
            for (std::vector<SrsMediaPayloadType>::iterator iter = payloads.begin(); iter != payloads.end(); ++iter) {
                if (iter->format_specific_param_.empty()) {
                    backup_payloads.push_front(*iter);
                    continue;
                }
                H264SpecificParam h264_param;
                if ((err = srs_parse_h264_fmtp(iter->format_specific_param_, h264_param)) != srs_success) {
                    srs_error_reset(err); continue;
                }

                // Try to pick the "best match" H.264 payload type.
                if (h264_param.packetization_mode == "1" && h264_param.level_asymmerty_allow == "1") {
                    local_media_desc.payload_types_.push_back(*iter);
                    SrsMediaPayloadType& payload_type = local_media_desc.payload_types_.back();

                    // TODO: FIXME: Only support some transport algorithms.
                    vector<string> rtcp_fb;
                    payload_type.rtcp_fb_.swap(rtcp_fb);
                    for (int j = 0; j < (int)rtcp_fb.size(); j++) {
                        if (nack_enabled) {
                            if (rtcp_fb.at(j) == "nack" || rtcp_fb.at(j) == "nack pli") {
                                payload_type.rtcp_fb_.push_back(rtcp_fb.at(j));
                            }
                        }
                        if (twcc_enabled) {
                            if (rtcp_fb.at(j) == "transport-cc") {
                                payload_type.rtcp_fb_.push_back(rtcp_fb.at(j));
                            }
                        }
                    }

                    // Only choose first match H.264 payload type.
                    break;
                }

                backup_payloads.push_back(*iter);
            }
            map<int, string> extmaps = remote_media_desc.get_extmaps();
            for(map<int, string>::iterator it_ext = extmaps.begin(); it_ext != extmaps.end(); ++it_ext) {
                if (it_ext->second == kTWCCExt) {
                    local_media_desc.extmaps_[it_ext->first] = kTWCCExt;
                    break;
                }
            }

            // Try my best to pick at least one media payload type.
            if (local_media_desc.payload_types_.empty() && ! backup_payloads.empty()) {
                srs_warn("choose backup H.264 payload type=%d", backup_payloads.front().payload_type_);
                local_media_desc.payload_types_.push_back(backup_payloads.front());
            }

            if (local_media_desc.payload_types_.empty()) {
                return srs_error_new(ERROR_RTC_SDP_EXCHANGE, "no found valid H.264 payload type");
            }

            // TODO: FIXME: Support RRTR?
            //local_media_desc.payload_types_.back().rtcp_fb_.push_back("rrtr");
        }

        local_media_desc.mid_ = remote_media_desc.mid_;
        local_sdp.groups_.push_back(local_media_desc.mid_);

        local_media_desc.port_ = 9;
        local_media_desc.protos_ = "UDP/TLS/RTP/SAVPF";

        if (remote_media_desc.session_info_.setup_ == "active") {
            local_media_desc.session_info_.setup_ = "passive";
        } else if (remote_media_desc.session_info_.setup_ == "passive") {
            local_media_desc.session_info_.setup_ = "active";
        } else if (remote_media_desc.session_info_.setup_ == "actpass") {
            local_media_desc.session_info_.setup_ = "passive";
        } else {
            // @see: https://tools.ietf.org/html/rfc4145#section-4.1
            // The default value of the setup attribute in an offer/answer exchange
            // is 'active' in the offer and 'passive' in the answer.
            local_media_desc.session_info_.setup_ = "passive";
        }

        local_media_desc.rtcp_mux_ = true;

        // For publisher, we are always sendonly.
        local_media_desc.sendonly_ = false;
        local_media_desc.recvonly_ = true;
        local_media_desc.sendrecv_ = false;
    }

    return err;
}

SrsGoApiRtcNACK::SrsGoApiRtcNACK(SrsRtcServer* server)
{
    server_ = server;
}

SrsGoApiRtcNACK::~SrsGoApiRtcNACK()
{
}

srs_error_t SrsGoApiRtcNACK::serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r)
{
    srs_error_t err = srs_success;

    SrsJsonObject* res = SrsJsonAny::object();
    SrsAutoFree(SrsJsonObject, res);

    res->set("code", SrsJsonAny::integer(ERROR_SUCCESS));

    if ((err = do_serve_http(w, r, res)) != srs_success) {
        srs_warn("RTC NACK err %s", srs_error_desc(err).c_str());
        res->set("code", SrsJsonAny::integer(srs_error_code(err)));
        srs_freep(err);
    }

    return srs_api_response(w, r, res->dumps());
}

srs_error_t SrsGoApiRtcNACK::do_serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r, SrsJsonObject* res)
{
    string username = r->query_get("username");
    string dropv = r->query_get("drop");

    SrsJsonObject* query = SrsJsonAny::object();
    res->set("query", query);

    query->set("username", SrsJsonAny::str(username.c_str()));
    query->set("drop", SrsJsonAny::str(dropv.c_str()));
    query->set("help", SrsJsonAny::str("?username=string&drop=int"));

    int drop = ::atoi(dropv.c_str());
    if (drop <= 0) {
        return srs_error_new(ERROR_RTC_INVALID_PARAMS, "invalid drop=%s/%d", dropv.c_str(), drop);
    }

    SrsRtcSession* session = server_->find_session_by_username(username);
    if (!session) {
        return srs_error_new(ERROR_RTC_NO_SESSION, "no session username=%s", username.c_str());
    }

    session->simulate_nack_drop(drop);

    srs_trace("RTC NACK session peer_id=%s, username=%s, drop=%s/%d", session->peer_id().c_str(),
        username.c_str(), dropv.c_str(), drop);

    return srs_success;
}

