//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#include <srs_app_tencentcloud.hpp>
#ifdef SRS_APM

#include <srs_kernel_error.hpp>
#include <srs_kernel_buffer.hpp>
#include <srs_app_config.hpp>
#include <srs_protocol_http_client.hpp>
#include <srs_protocol_http_stack.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_utility.hpp>
#include <srs_core_autofree.hpp>
#include <srs_protocol_json.hpp>
#include <srs_app_statistic.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_app_utility.hpp>
#include <srs_protocol_protobuf.hpp>
#include <srs_protocol_amf0.hpp>

#include <string>
#include <map>
#include <algorithm>
#include <stdio.h>
#include <string.h>
#include <openssl/sha.h>
#include <openssl/hmac.h>
#include <unistd.h>
using namespace std;

// See https://cloud.tencent.com/document/product/614/12445
namespace tencentcloud_api_sign {
    std::string sha1(const void *data, size_t len) {
        unsigned char digest[SHA_DIGEST_LENGTH];
        SHA_CTX ctx;
        SHA1_Init(&ctx);
        SHA1_Update(&ctx, data, len);
        SHA1_Final(digest, &ctx);
        char c_sha1[SHA_DIGEST_LENGTH*2+1];
        for (unsigned i = 0; i < SHA_DIGEST_LENGTH; ++i) {
            snprintf(&c_sha1[i*2], 3, "%02x", (unsigned int)digest[i]);
        }
        return c_sha1;
    }

    std::string hmac_sha1(const char *key, const void *data, size_t len) {
        unsigned char digest[EVP_MAX_MD_SIZE];
        unsigned digest_len;
        char c_hmacsha1[EVP_MAX_MD_SIZE*2+1];
#if !defined(OPENSSL_VERSION_NUMBER) || OPENSSL_VERSION_NUMBER < 0x10100000L
        HMAC_CTX ctx;
        HMAC_CTX_init(&ctx);
        HMAC_Init_ex(&ctx, key, strlen(key), EVP_sha1(), NULL);
        HMAC_Update(&ctx, (unsigned char*)data, len);
        HMAC_Final(&ctx, digest, &digest_len);
        HMAC_CTX_cleanup(&ctx);
#else
        HMAC_CTX *ctx = HMAC_CTX_new();
        HMAC_CTX_reset(ctx);
        HMAC_Init_ex(ctx, key, strlen(key), EVP_sha1(), NULL);
        HMAC_Update(ctx, (unsigned char *)data, len);
        HMAC_Final(ctx, digest, &digest_len);
        HMAC_CTX_free(ctx);
#endif
        for (unsigned i = 0; i != digest_len; ++i) {
            snprintf(&c_hmacsha1[i*2], 3, "%02x", (unsigned int)digest[i]);
        }
        return c_hmacsha1;
    }

    std::string urlencode(const char *s) {
        static unsigned char hexchars[] = "0123456789ABCDEF";
        size_t length = strlen(s), pos = 0;
        unsigned char c_url[length*3+1];
        const unsigned char *p = (const unsigned char *)s;
        for (; *p; ++p) {
            if (isalnum((unsigned char)*p) || (*p == '-') ||
                (*p == '_') || (*p == '.') || (*p == '~')) {
                c_url[pos++] = *p;
            } else {
                c_url[pos++] = '%';
                c_url[pos++] = hexchars[(*p)>>4];
                c_url[pos++] = hexchars[(*p)&15U];
            }
        }
        c_url[pos] = 0;
        return (char*)c_url;
    }

    std::string signature(const std::string &secret_id,
                          const std::string &secret_key,
                          std::string method,
                          const std::string &path,
                          const std::map<std::string, std::string> &params,
                          const std::map<std::string, std::string> &headers,
                          long expire) {

        const size_t SIGNLEN = 1024;
        std::string http_request_info, uri_parm_list,
                header_list, str_to_sign, sign_key;
        transform(method.begin(), method.end(), method.begin(), ::tolower);
        http_request_info.reserve(SIGNLEN);
        http_request_info.append(method).append("\n").append(path).append("\n");
        uri_parm_list.reserve(SIGNLEN);
        std::map<std::string, std::string>::const_iterator iter;
        for (iter = params.begin();
             iter != params.end(); ) {
            uri_parm_list.append(iter->first);
            http_request_info.append(iter->first).append("=")
                    .append(urlencode(iter->second.c_str()));
            if (++iter != params.end()) {
                uri_parm_list.append(";");
                http_request_info.append("&");
            }
        }
        http_request_info.append("\n");
        header_list.reserve(SIGNLEN);
        for (iter = headers.begin();
             iter != headers.end(); ++iter) {
            sign_key = iter->first;
            transform(sign_key.begin(), sign_key.end(), sign_key.begin(), ::tolower);
            if (sign_key == "content-type" || sign_key == "content-md5"
                || sign_key == "host" || sign_key[0] == 'x') {
                header_list.append(sign_key);
                http_request_info.append(sign_key).append("=")
                        .append(urlencode(iter->second.c_str()));
                header_list.append(";");
                http_request_info.append("&");
            }
        }
        if (!header_list.empty()) {
            header_list[header_list.size() - 1] = 0;
            http_request_info[http_request_info.size() - 1] = '\n';
        }
        //printf("%s\nEOF\n", http_request_info.c_str());
        char signed_time[SIGNLEN] = {0};
        int signed_time_len = snprintf(signed_time, SIGNLEN,
                                       "%lu;%lu", time(0) - 60, time(0) + expire);
        //snprintf(signed_time, SIGNLEN, "1510109254;1510109314");
        std::string signkey = hmac_sha1(secret_key.c_str(),
                                        signed_time, signed_time_len);
        str_to_sign.reserve(SIGNLEN);
        str_to_sign.append("sha1").append("\n")
                .append(signed_time).append("\n")
                .append(sha1(http_request_info.c_str(), http_request_info.size()))
                .append("\n");
        //printf("%s\nEOF\n", str_to_sign.c_str());
        std::stringstream c_signature;
        c_signature << "q-sign-algorithm=sha1&q-ak=" << secret_id.c_str()
                    << "&q-sign-time=" << signed_time
                    << "&q-key-time=" << signed_time
                    << "&q-header-list=" << header_list.c_str()
                    << "&q-url-param-list=" << uri_parm_list.c_str()
                    << "&q-signature=" << hmac_sha1(signkey.c_str(), str_to_sign.c_str(), str_to_sign.size()).c_str();
        return c_signature.str();
    }
}

// See https://cloud.tencent.com/document/api/614/16873
class SrsClsLogContent : public ISrsEncoder
{
private:
    // required string key = 1;
    std::string key_;
    // required string value = 2;
    std::string value_;
public:
    SrsClsLogContent();
    virtual ~SrsClsLogContent();
public:
    SrsClsLogContent* set_key(std::string v);
    SrsClsLogContent* set_value(std::string v);
public:
    virtual uint64_t nb_bytes();
    srs_error_t encode(SrsBuffer* b);
};

// See https://cloud.tencent.com/document/api/614/16873
class SrsClsLog : public ISrsEncoder
{
private:
    // required int64 time = 1;
    int64_t time_;
    // repeated Content contents= 2;
    std::vector<SrsClsLogContent*> contents_;
public:
    SrsClsLog();
    virtual ~SrsClsLog();
public:
    SrsClsLogContent* add_content();
    SrsClsLog* set_time(int64_t v);
public:
    virtual uint64_t nb_bytes();
    srs_error_t encode(SrsBuffer* b);
};

// See https://cloud.tencent.com/document/api/614/16873
class SrsClsLogGroup : public ISrsEncoder
{
private:
    // repeated Log logs= 1;
    std::vector<SrsClsLog*> logs_;
    // optional string source = 4;
    std::string source_;
public:
    SrsClsLogGroup();
    virtual ~SrsClsLogGroup();
public:
    SrsClsLogGroup* set_source(std::string v);
    SrsClsLog* add_log();
public:
    virtual uint64_t nb_bytes();
    srs_error_t encode(SrsBuffer* b);
};

// See https://cloud.tencent.com/document/api/614/16873
class SrsClsLogGroupList
{
private:
    // repeated LogGroup logGroupList = 1;
    std::vector<SrsClsLogGroup*> groups_;
public:
    SrsClsLogGroupList();
    virtual ~SrsClsLogGroupList();
public:
    bool empty();
    SrsClsLogGroup* add_log_group();
public:
    virtual uint64_t nb_bytes();
    srs_error_t encode(SrsBuffer* b);
};

SrsClsLogContent::SrsClsLogContent()
{
}

SrsClsLogContent::~SrsClsLogContent()
{
}

SrsClsLogContent* SrsClsLogContent::set_key(std::string v)
{
    key_ = v;
    return this;
}

SrsClsLogContent* SrsClsLogContent::set_value(std::string v)
{
    value_ = v;
    return this;
}

uint64_t SrsClsLogContent::nb_bytes()
{
    uint64_t  nn = SrsProtobufKey::sizeof_key() + SrsProtobufString::sizeof_string(key_);
    nn += SrsProtobufKey::sizeof_key() + SrsProtobufString::sizeof_string(value_);
    return nn;
}

srs_error_t SrsClsLogContent::encode(SrsBuffer* b)
{
    srs_error_t err = srs_success;

    // Encode the key.
    if ((err = SrsProtobufKey::encode(b, 1, SrsProtobufFieldString)) != srs_success) {
        return srs_error_wrap(err, "key");
    }

    if ((err = SrsProtobufString::encode(b, key_)) != srs_success) {
        return srs_error_wrap(err, "encode key=%s", key_.c_str());
    }

    // Encode the value.
    if ((err = SrsProtobufKey::encode(b, 2, SrsProtobufFieldString)) != srs_success) {
        return srs_error_wrap(err, "key");
    }

    if ((err = SrsProtobufString::encode(b, value_)) != srs_success) {
        return srs_error_wrap(err, "encode value=%s", value_.c_str());
    }

    return err;
}

SrsClsLog::SrsClsLog()
{
}

SrsClsLog::~SrsClsLog()
{
    for (std::vector<SrsClsLogContent*>::iterator it = contents_.begin(); it != contents_.end(); ++it) {
        SrsClsLogContent* content = *it;
        srs_freep(content);
    }
}

SrsClsLogContent* SrsClsLog::add_content()
{
    SrsClsLogContent* content = new SrsClsLogContent();
    contents_.push_back(content);
    return content;
}

SrsClsLog* SrsClsLog::set_time(int64_t v)
{
    time_ = v;
    return this;
}

uint64_t SrsClsLog::nb_bytes()
{
    uint64_t nn = SrsProtobufKey::sizeof_key() + SrsProtobufVarints::sizeof_varint(time_);

    for (std::vector<SrsClsLogContent*>::iterator it = contents_.begin(); it != contents_.end(); ++it) {
        SrsClsLogContent* content = *it;
        nn += SrsProtobufKey::sizeof_key() + SrsProtobufObject::sizeof_object(content);
    }

    return nn;
}

srs_error_t SrsClsLog::encode(SrsBuffer* b)
{
    srs_error_t  err = srs_success;

    // Encode the time.
    if ((err = SrsProtobufKey::encode(b, 1, SrsProtobufFieldVarint)) != srs_success) {
        return srs_error_wrap(err, "key");
    }

    if ((err = SrsProtobufVarints::encode(b, time_)) != srs_success) {
        return srs_error_wrap(err, "encode time");
    }

    // Encode each content.
    for (std::vector<SrsClsLogContent*>::iterator it = contents_.begin(); it != contents_.end(); ++it) {
        SrsClsLogContent* content = *it;

        if ((err = SrsProtobufKey::encode(b, 2, SrsProtobufFieldObject)) != srs_success) {
            return srs_error_wrap(err, "key");
        }

        if ((err = SrsProtobufObject::encode(b, content)) != srs_success) {
            return srs_error_wrap(err, "encode content");
        }
    }

    return err;
}

SrsClsLogGroup::SrsClsLogGroup()
{
}

SrsClsLogGroup::~SrsClsLogGroup()
{
    for (std::vector<SrsClsLog*>::iterator it = logs_.begin(); it != logs_.end(); ++it) {
        SrsClsLog* log = *it;
        srs_freep(log);
    }
}

SrsClsLogGroup* SrsClsLogGroup::set_source(std::string v)
{
    source_ = v;
    return this;
}

SrsClsLog* SrsClsLogGroup::add_log()
{
    SrsClsLog* log = new SrsClsLog();
    logs_.push_back(log);
    return log;
}

uint64_t SrsClsLogGroup::nb_bytes()
{
    uint64_t nn = 0;
    for (std::vector<SrsClsLog*>::iterator it = logs_.begin(); it != logs_.end(); ++it) {
        SrsClsLog* log = *it;
        nn += SrsProtobufKey::sizeof_key() + SrsProtobufObject::sizeof_object(log);
    }

    nn += SrsProtobufKey::sizeof_key() + SrsProtobufString::sizeof_string(source_);
    return nn;
}

srs_error_t SrsClsLogGroup::encode(SrsBuffer* b)
{
    srs_error_t err = srs_success;

    // Encode each log.
    for (std::vector<SrsClsLog*>::iterator it = logs_.begin(); it != logs_.end(); ++it) {
        SrsClsLog* log = *it;

        if ((err = SrsProtobufKey::encode(b, 1, SrsProtobufFieldObject)) != srs_success) {
            return srs_error_wrap(err, "key");
        }

        if ((err = SrsProtobufObject::encode(b, log)) != srs_success) {
            return srs_error_wrap(err, "encode log");
        }
    }

    // Encode the optional source.
    if ((err = SrsProtobufKey::encode(b, 4, SrsProtobufFieldString)) != srs_success) {
        return srs_error_wrap(err, "key");
    }

    if ((err = SrsProtobufString::encode(b, source_)) != srs_success) {
        return srs_error_wrap(err, "encode source=%s", source_.c_str());
    }

    return err;
}

SrsClsLogGroupList::SrsClsLogGroupList()
{
}

SrsClsLogGroupList::~SrsClsLogGroupList()
{
    for (std::vector<SrsClsLogGroup*>::iterator it = groups_.begin(); it != groups_.end(); ++it) {
        SrsClsLogGroup* group = *it;
        srs_freep(group);
    }
}

bool SrsClsLogGroupList::empty()
{
    return groups_.empty();
}

SrsClsLogGroup* SrsClsLogGroupList::add_log_group()
{
    SrsClsLogGroup* group = new SrsClsLogGroup();
    groups_.push_back(group);
    return group;
}

uint64_t SrsClsLogGroupList::nb_bytes()
{
    uint64_t nn = 0;
    for (std::vector<SrsClsLogGroup*>::iterator it = groups_.begin(); it != groups_.end(); ++it) {
        SrsClsLogGroup* group = *it;
        nn += SrsProtobufKey::sizeof_key() + SrsProtobufObject::sizeof_object(group);
    }
    return nn;
}

srs_error_t SrsClsLogGroupList::encode(SrsBuffer* b)
{
    srs_error_t err = srs_success;

    // Encode each group.
    for (std::vector<SrsClsLogGroup*>::iterator it = groups_.begin(); it != groups_.end(); ++it) {
        SrsClsLogGroup* group = *it;

        if ((err = SrsProtobufKey::encode(b, 1, SrsProtobufFieldObject)) != srs_success) {
            return srs_error_wrap(err, "key");
        }

        if ((err = SrsProtobufObject::encode(b, group)) != srs_success) {
            return srs_error_wrap(err, "encode group");
        }
    }

    return err;
}

SrsClsSugar::SrsClsSugar()
{
    log_groups_ = new SrsClsLogGroupList();
    log_group_ = log_groups_->add_log_group();
    log_ = log_group_->add_log();

    log_group_->set_source(srs_get_public_internet_address(true));
    log_->set_time(srs_get_system_time() / SRS_UTIME_MILLISECONDS);
    kv("agent", RTMP_SIG_SRS_SERVER);

    string label = _srs_cls->label();
    if (!label.empty()) {
        kv("label", label);
    }

    string tag = _srs_cls->tag();
    if (!tag.empty()) {
        kv("tag", tag);
    }

    string server_id = SrsStatistic::instance()->server_id();
    if (!server_id.empty()) {
        kv("id", server_id);
    }
}

SrsClsSugar::~SrsClsSugar()
{
    srs_freep(log_groups_);
}

uint64_t SrsClsSugar::nb_bytes()
{
    return log_groups_->nb_bytes();
}

srs_error_t SrsClsSugar::encode(SrsBuffer* b)
{
    return log_groups_->encode(b);
}

bool SrsClsSugar::empty()
{
    return log_groups_->empty();
}

SrsClsSugar* SrsClsSugar::kv(std::string k, std::string v)
{
    log_->add_content()->set_key(k)->set_value(v);
    return this;
}

SrsClsSugars::SrsClsSugars()
{
}

SrsClsSugars::~SrsClsSugars()
{
    for (vector<SrsClsSugar*>::iterator it = sugars.begin(); it != sugars.end(); ++it) {
        SrsClsSugar* sugar = *it;
        srs_freep(sugar);
    }
}

uint64_t SrsClsSugars::nb_bytes()
{
    uint64_t size = 0;
    for (vector<SrsClsSugar*>::iterator it = sugars.begin(); it != sugars.end(); ++it) {
        SrsClsSugar* sugar = *it;
        size += sugar->nb_bytes();
    }
    return size;
}

srs_error_t SrsClsSugars::encode(SrsBuffer* b)
{
    srs_error_t err = srs_success;

    for (vector<SrsClsSugar*>::iterator it = sugars.begin(); it != sugars.end(); ++it) {
        SrsClsSugar* sugar = *it;
        if ((err = sugar->encode(b)) != srs_success) {
            return srs_error_wrap(err, "encode %d sugars", (int)sugars.size());
        }
    }

    return err;
}

SrsClsSugar* SrsClsSugars::create()
{
    SrsClsSugar* sugar = new SrsClsSugar();
    sugars.push_back(sugar);
    return sugar;
}

SrsClsSugars* SrsClsSugars::slice(int max_size)
{
    SrsClsSugars* v = new SrsClsSugars();

    uint64_t v_size = 0;
    for (vector<SrsClsSugar*>::iterator it = sugars.begin(); it != sugars.end();) {
        SrsClsSugar* sugar = *it;

        // Always consume it.
        it = sugars.erase(it);

        // If empty, ignore it.
        if (sugar->empty()) {
            srs_freep(sugar);
            continue;
        }

        // Not empty, append it, to make sure at least one elem.
        v->sugars.push_back(sugar);

        // Util exceed the max size.
        v_size += sugar->nb_bytes();
        if ((int)v_size > max_size) {
            break;
        }
    }

    return v;
}

bool SrsClsSugars::empty()
{
    return sugars.empty();
}

int SrsClsSugars::size()
{
    return (int)sugars.size();
}

SrsClsClient* _srs_cls = NULL;

SrsClsClient::SrsClsClient()
{
    enabled_ = false;
    stat_heartbeat_ = false;
    stat_streams_ = false;
    debug_logging_ = false;
    heartbeat_ratio_ = 0;
    streams_ratio_ = 0;
    nn_logs_ = 0;
    sugars_ = new SrsClsSugars();
}

SrsClsClient::~SrsClsClient()
{
    srs_freep(sugars_);
}

bool SrsClsClient::enabled()
{
    return enabled_;
}

string SrsClsClient::label()
{
    return label_;
}

string SrsClsClient::tag()
{
    return tag_;
}

uint64_t SrsClsClient::nn_logs()
{
    return nn_logs_;
}

srs_error_t SrsClsClient::initialize()
{
    srs_error_t err = srs_success;

    enabled_ = _srs_config->get_tencentcloud_cls_enabled();
    if (!enabled_) {
        srs_trace("TencentCloud CLS is disabled");
        return err;
    }

    label_ = _srs_config->get_tencentcloud_cls_label();
    tag_ = _srs_config->get_tencentcloud_cls_tag();
    stat_heartbeat_ = _srs_config->get_tencentcloud_cls_stat_heartbeat();
    stat_streams_ = _srs_config->get_tencentcloud_cls_stat_streams();
    debug_logging_ = _srs_config->get_tencentcloud_cls_debug_logging();
    heartbeat_ratio_ = srs_max(1, _srs_config->get_tencentcloud_cls_heartbeat_ratio());
    streams_ratio_ = srs_max(1, _srs_config->get_tencentcloud_cls_streams_ratio());

    secret_id_ = _srs_config->get_tencentcloud_cls_secret_id();
    if (secret_id_.empty()) {
        return srs_error_new(ERROR_CLS_INVALID_CONFIG, "CLS no config for secret_id");
    }

    string secret_key = _srs_config->get_tencentcloud_cls_secret_key();
    if (secret_key.empty()) {
        return srs_error_new(ERROR_CLS_INVALID_CONFIG, "CLS no config for secret_key");
    }

    endpoint_ = _srs_config->get_tencentcloud_cls_endpoint();
    if (endpoint_.empty()) {
        return srs_error_new(ERROR_CLS_INVALID_CONFIG, "CLS no config for endpoint");
    }

    topic_ = _srs_config->get_tencentcloud_cls_topic_id();
    if (topic_.empty()) {
        return srs_error_new(ERROR_CLS_INVALID_CONFIG, "CLS no config for topic_id");
    }

    srs_trace("Initialize TencentCloud CLS label=%s, tag=%s, secret_id=%dB, secret_key=%dB, endpoint=%s, topic=%s, heartbeat=%d/%d, streams=%d/%d debug_logging=%d",
        label_.c_str(), tag_.c_str(), secret_id_.length(), secret_key.length(), endpoint_.c_str(), topic_.c_str(), stat_heartbeat_, heartbeat_ratio_, stat_streams_, streams_ratio_, debug_logging_);

    return err;
}

srs_error_t SrsClsClient::report()
{
    srs_error_t err = srs_success;

    if ((err = dump_summaries(sugars_)) != srs_success) {
        return srs_error_wrap(err, "dump summary");
    }

    if ((err = dump_streams(sugars_)) != srs_success) {
        return srs_error_wrap(err, "dump streams");
    }

    if (sugars_->empty()) {
        return err;
    }

    SrsClsSugars* sugars = sugars_;
    SrsAutoFree(SrsClsSugars, sugars);
    sugars_ = new SrsClsSugars();

    if ((err = send_logs(sugars)) != srs_success) {
        return srs_error_wrap(err, "cls");
    }

    return err;
}

srs_error_t SrsClsClient::do_send_logs(ISrsEncoder* sugar, int count, int total)
{
    srs_error_t err = srs_success;

    uint64_t size = sugar->nb_bytes();
    // Max size is 5MB, error is 403:LogSizeExceed, see https://cloud.tencent.com/document/api/614/12402
    if (size >= 5 * 1024 * 1024) {
        return srs_error_new(ERROR_CLS_EXCEED_SIZE, "exceed 5MB actual %d", size);
    }

    char* buf = new char[size];
    SrsAutoFreeA(char, buf);

    memset(buf, 0, size);
    SrsBuffer b(buf, size);
    if ((err = sugar->encode(&b)) != srs_success) {
        return srs_error_wrap(err, "encode log");
    }

    string body(buf, size);

    // Write a CLS log to service specified by url.
    string url = "http://" + endpoint_ + ":80/structuredlog?topic_id=" + topic_;

    SrsHttpUri uri;
    if ((err = uri.initialize(url)) != srs_success) {
        return srs_error_wrap(err, "http: post failed. url=%s", url.c_str());
    }

    SrsHttpClient http;
    if ((err = http.initialize(uri.get_schema(), uri.get_host(), uri.get_port())) != srs_success) {
        return srs_error_wrap(err, "http: init client");
    }

    // Sign the request, see https://cloud.tencent.com/document/product/614/56475
    if (true) {
        map<string, string> params;
        params["topic_id"] = topic_;

        map<string, string> headers;
        headers["Host"] = uri.get_host();
        headers["Content-Type"] = "application/x-protobuf";
        http.set_header("Content-Type", "application/x-protobuf");

        string method = "POST";
        string secret_key = _srs_config->get_tencentcloud_cls_secret_key();
        std::string signature = tencentcloud_api_sign::signature(
            secret_id_, secret_key, method, uri.get_path(), params, headers, 300 // Expire in seconds
        );
        headers["Authorization"] = signature;
        http.set_header("Authorization", signature);
    }

    string path = uri.get_path();
    if (!uri.get_query().empty()) {
        path += "?" + uri.get_query();
    }

    // Start request and parse response.
    ISrsHttpMessage* msg = NULL;
    if ((err = http.post(path, body, &msg)) != srs_success) {
        return srs_error_wrap(err, "http: client post");
    }
    SrsAutoFree(ISrsHttpMessage, msg);

    string res;
    uint16_t code = msg->status_code();
    if ((err = msg->body_read_all(res)) != srs_success) {
        return srs_error_wrap(err, "http: body read");
    }

    // ensure the http status is ok.
    if (code != SRS_CONSTS_HTTP_OK && code != SRS_CONSTS_HTTP_Created) {
        return srs_error_new(ERROR_HTTP_STATUS_INVALID, "http: status %d, body is %s", code, res.c_str());
    }

    string request_id = msg->header()->get("X-Cls-Requestid");
    if (request_id.empty() && !debug_logging_) {
        srs_warn("no CLS requestId for log %dB", body.length());
    }

    if (debug_logging_) {
        string server_id = SrsStatistic::instance()->server_id();
        srs_trace("CLS write logs=%d/%d, size=%dB, server_id=%s, request_id=%s", count, total, body.length(), server_id.c_str(), request_id.c_str());
    }

    return err;
}

// For each upload, never exceed 2MB, to avoid burst of CPU or network usage.
#define SRS_CLS_BATCH_MAX_LOG_SIZE 2 * 1024 * 1024

srs_error_t SrsClsClient::send_logs(SrsClsSugars* sugars)
{
    srs_error_t err = srs_success;

    // Record the total logs sent out.
    int total = sugars->size();
    nn_logs_ += total;

    // Never do infinite loop, limit to a max loop and drop logs if exceed.
    for (int i = 0; i < 128 && !sugars->empty(); ++i) {
        SrsClsSugars* v = sugars->slice(SRS_CLS_BATCH_MAX_LOG_SIZE);
        SrsAutoFree(SrsClsSugars, v);

        if ((err = do_send_logs((ISrsEncoder*)v, v->size(), total)) != srs_success) {
            return srs_error_wrap(err, "send %d/%d/%d logs", v->size(), i, total);
        }
    }

    return err;
}

srs_error_t SrsClsClient::dump_summaries(SrsClsSugars* sugars)
{
    srs_error_t err = srs_success;

    // Ignore if disabled.
    if (!enabled_ || !stat_heartbeat_) {
        return err;
    }

    // Whether it's time to report heartbeat.
    static int nn_heartbeat = -1;
    bool interval_ok = nn_heartbeat == -1 || ++nn_heartbeat >= heartbeat_ratio_;
    if (interval_ok) {
        nn_heartbeat = 0;
    }
    if (!interval_ok) {
        return err;
    }

    SrsClsSugar* sugar = sugars->create();
    sugar->kv("hint", "summary");
    sugar->kv("version", RTMP_SIG_SRS_VERSION);
    sugar->kv("pid", srs_fmt("%d", getpid()));

    // Server ID to identify logs from a set of servers' logs.
    SrsStatistic::instance()->dumps_cls_summaries(sugar);

    SrsProcSelfStat* u = srs_get_self_proc_stat();
    if (u->ok) {
        // The cpu usage of SRS, 1 means 1/1000
        if (u->percent > 0) {
            sugar->kv("cpu", srs_fmt("%d", (int)(u->percent * 1000)));
        }
    }

    SrsPlatformInfo* p = srs_get_platform_info();
    if (p->ok) {
        // The uptime of SRS, in seconds.
        if (p->srs_startup_time > 0) {
            sugar->kv("uptime", srs_fmt("%d", (int)((srs_get_system_time() - p->srs_startup_time) / SRS_UTIME_SECONDS)));
        }
        // The load of system, load every 1 minute, 1 means 1/1000.
        if (p->load_one_minutes > 0) {
            sugar->kv("load", srs_fmt("%d", (int)(p->load_one_minutes * 1000)));
        }
    }

    SrsRusage* r = srs_get_system_rusage();
    SrsMemInfo* m = srs_get_meminfo();
    if (r->ok && m->ok) {
        float self_mem_percent = 0;
        if (m->MemTotal > 0) {
            self_mem_percent = (float)(r->r.ru_maxrss / (double)m->MemTotal);
        }

        // The memory of SRS, 1 means 1/1000
        if (self_mem_percent > 0) {
            sugar->kv("mem", srs_fmt("%d", (int)(self_mem_percent * 1000)));
        }
    }

    SrsProcSystemStat* s = srs_get_system_proc_stat();
    if (s->ok) {
        // The cpu usage of system, 1 means 1/1000
        if (s->percent > 0) {
            sugar->kv("cpu2", srs_fmt("%d", (int)(s->percent * 1000)));
        }
    }

    SrsNetworkRtmpServer* nrs = srs_get_network_rtmp_server();
    if (nrs->ok) {
        // The number of connections of SRS.
        if (nrs->nb_conn_srs > 0) {
            sugar->kv("conn", srs_fmt("%d", nrs->nb_conn_srs));
        }
        // The number of connections of system.
        if (nrs->nb_conn_sys > 0) {
            sugar->kv("conn2", srs_fmt("%d", nrs->nb_conn_sys));
        }
        // The received kbps in 30s of SRS.
        if (nrs->rkbps_30s > 0) {
            sugar->kv("recv", srs_fmt("%d", nrs->rkbps_30s));
        }
        // The sending out kbps in 30s of SRS.
        if (nrs->skbps_30s > 0) {
            sugar->kv("send", srs_fmt("%d", nrs->skbps_30s));
        }
    }

    return err;
}

srs_error_t SrsClsClient::dump_streams(SrsClsSugars* sugars)
{
    srs_error_t err = srs_success;

    // Ignore if disabled.
    if (!enabled_ || !stat_streams_) {
        return err;
    }

    // Whether it's time to report streams.
    static int nn_streams = -1;
    bool interval_ok = nn_streams == -1 || ++nn_streams >= streams_ratio_;
    if (interval_ok) {
        nn_streams = 0;
    }
    if (!interval_ok) {
        return err;
    }

    // Dumps all streams as sugars.
    SrsStatistic::instance()->dumps_cls_streams(sugars);

    return err;
}

SrsOtelExportTraceServiceRequest::SrsOtelExportTraceServiceRequest()
{
}

SrsOtelExportTraceServiceRequest::~SrsOtelExportTraceServiceRequest()
{
    for (vector<SrsOtelResourceSpans*>::iterator it = spans_.begin(); it != spans_.end(); ++it) {
        SrsOtelResourceSpans* span = *it;
        srs_freep(span);
    }
}

SrsOtelResourceSpans* SrsOtelExportTraceServiceRequest::append()
{
    SrsOtelResourceSpans* v = new SrsOtelResourceSpans();
    spans_.push_back(v);
    return v;
}

uint64_t SrsOtelExportTraceServiceRequest::nb_bytes()
{
    uint64_t nn = 0;
    for (vector<SrsOtelResourceSpans*>::iterator it = spans_.begin(); it != spans_.end(); ++it) {
        SrsOtelResourceSpans* span = *it;
        nn += SrsProtobufKey::sizeof_key() + SrsProtobufObject::sizeof_object(span);
    }
    return nn;
}

srs_error_t SrsOtelExportTraceServiceRequest::encode(SrsBuffer* b)
{
    srs_error_t err = srs_success;

    // Encode each span.
    for (vector<SrsOtelResourceSpans*>::iterator it = spans_.begin(); it != spans_.end(); ++it) {
        SrsOtelResourceSpans* span = *it;

        if ((err = SrsProtobufKey::encode(b, 1, SrsProtobufFieldObject)) != srs_success) {
            return srs_error_wrap(err, "key");
        }

        if ((err = SrsProtobufObject::encode(b, span)) != srs_success) {
            return srs_error_wrap(err, "encode span");
        }
    }

    return err;
}

SrsOtelResourceSpans::SrsOtelResourceSpans()
{
    resource_ = new SrsOtelResource();
}

SrsOtelResourceSpans::~SrsOtelResourceSpans()
{
    srs_freep(resource_);
    
    for (std::vector<SrsOtelScopeSpans*>::iterator it = spans_.begin(); it != spans_.end(); ++it) {
        SrsOtelScopeSpans* span = *it;
        srs_freep(span);
    }
}

SrsOtelResource* SrsOtelResourceSpans::resource()
{
    return resource_;
}

SrsOtelScopeSpans* SrsOtelResourceSpans::append()
{
    SrsOtelScopeSpans* v = new SrsOtelScopeSpans();
    spans_.push_back(v);
    return v;
}

uint64_t SrsOtelResourceSpans::nb_bytes()
{
    uint64_t nn = SrsProtobufKey::sizeof_key() + SrsProtobufObject::sizeof_object(resource_);
    for (std::vector<SrsOtelScopeSpans*>::iterator it = spans_.begin(); it != spans_.end(); ++it) {
        SrsOtelScopeSpans* span = *it;
        nn += SrsProtobufKey::sizeof_key() + SrsProtobufObject::sizeof_object(span);
    }
    return nn;
}

srs_error_t SrsOtelResourceSpans::encode(SrsBuffer* b)
{
    srs_error_t err = srs_success;

    // Encode the resource.
    if ((err = SrsProtobufKey::encode(b, 1, SrsProtobufFieldObject)) != srs_success) {
        return srs_error_wrap(err, "key");
    }

    if ((err = SrsProtobufObject::encode(b, resource_)) != srs_success) {
        return srs_error_wrap(err, "encode resource");
    }

    // Encode scope spans.

    // Encode each group.
    for (std::vector<SrsOtelScopeSpans*>::iterator it = spans_.begin(); it != spans_.end(); ++it) {
        SrsOtelScopeSpans* span = *it;

        if ((err = SrsProtobufKey::encode(b, 2, SrsProtobufFieldObject)) != srs_success) {
            return srs_error_wrap(err, "key");
        }

        if ((err = SrsProtobufObject::encode(b, span)) != srs_success) {
            return srs_error_wrap(err, "encode span");
        }
    }

    return err;
}

SrsOtelResource::SrsOtelResource()
{
}

SrsOtelResource::~SrsOtelResource()
{
    for (vector<SrsOtelAttribute*>::iterator it = attributes_.begin(); it != attributes_.end(); ++it) {
        SrsOtelAttribute* attribute = *it;
        srs_freep(attribute);
    }
}

SrsOtelResource* SrsOtelResource::add_addr(SrsOtelAttribute* v)
{
    attributes_.push_back(v);
    return this;
}

uint64_t SrsOtelResource::nb_bytes()
{
    uint64_t nn = 0;
    for (vector<SrsOtelAttribute*>::iterator it = attributes_.begin(); it != attributes_.end(); ++it) {
        SrsOtelAttribute* attribute = *it;
        nn += SrsProtobufKey::sizeof_key() + SrsProtobufObject::sizeof_object(attribute);
    }
    return nn;
}

srs_error_t SrsOtelResource::encode(SrsBuffer* b)
{
    srs_error_t err = srs_success;

    // Encode attributes.
    for (vector<SrsOtelAttribute*>::iterator it = attributes_.begin(); it != attributes_.end(); ++it) {
        SrsOtelAttribute* attribute = *it;

        if ((err = SrsProtobufKey::encode(b, 1, SrsProtobufFieldObject)) != srs_success) {
            return srs_error_wrap(err, "key");
        }

        if ((err = SrsProtobufObject::encode(b, attribute)) != srs_success) {
            return srs_error_wrap(err, "encode attribute");
        }
    }

    return err;
}

SrsOtelAttribute::SrsOtelAttribute()
{
    value_ = new SrsOtelAnyValue();
}

SrsOtelAttribute::~SrsOtelAttribute()
{
    srs_freep(value_);
}

const std::string& SrsOtelAttribute::key()
{
    return key_;
}

SrsOtelAttribute* SrsOtelAttribute::kv(std::string k, std::string v)
{
    SrsOtelAttribute* attr = new SrsOtelAttribute();
    attr->key_ = k;
    attr->value_->set_string(v);
    return attr;
}

SrsOtelAttribute* SrsOtelAttribute::kvi(std::string k, int64_t v)
{
    SrsOtelAttribute* attr = new SrsOtelAttribute();
    attr->key_ = k;
    attr->value_->set_int(v);
    return attr;
}

uint64_t SrsOtelAttribute::nb_bytes()
{
    uint64_t  nn = SrsProtobufKey::sizeof_key() + SrsProtobufString::sizeof_string(key_);
    nn += SrsProtobufKey::sizeof_key() + SrsProtobufObject::sizeof_object(value_);
    return nn;
}

srs_error_t SrsOtelAttribute::encode(SrsBuffer* b)
{
    srs_error_t err = srs_success;

    // Encode the key.
    if ((err = SrsProtobufKey::encode(b, 1, SrsProtobufFieldString)) != srs_success) {
        return srs_error_wrap(err, "key");
    }

    if ((err = SrsProtobufString::encode(b, key_)) != srs_success) {
        return srs_error_wrap(err, "encode key=%s", key_.c_str());
    }

    // Encode the value.
    if ((err = SrsProtobufKey::encode(b, 2, SrsProtobufFieldObject)) != srs_success) {
        return srs_error_wrap(err, "key");
    }

    if ((err = SrsProtobufObject::encode(b, value_)) != srs_success) {
        return srs_error_wrap(err, "encode value");
    }

    return err;
}

SrsOtelAnyValue::SrsOtelAnyValue()
{
    used_field_id_ = 1;
    int_value_ = 0;
}

SrsOtelAnyValue::~SrsOtelAnyValue()
{
}

SrsOtelAnyValue* SrsOtelAnyValue::set_string(const std::string& v)
{
    string_value_ = v;
    used_field_id_ = 1;
    return this;
}

SrsOtelAnyValue* SrsOtelAnyValue::set_int(int64_t v)
{
    int_value_ = v;
    used_field_id_ = 3;
    return this;
}

uint64_t SrsOtelAnyValue::nb_bytes()
{
    uint64_t  nn = 0;
    if (used_field_id_ == 1) nn = SrsProtobufKey::sizeof_key() + SrsProtobufString::sizeof_string(string_value_);
    if (used_field_id_ == 3) nn = SrsProtobufKey::sizeof_key() + SrsProtobufVarints::sizeof_varint(int_value_);
    return nn;
}

srs_error_t SrsOtelAnyValue::encode(SrsBuffer* b)
{
    srs_error_t err = srs_success;

    if (used_field_id_ == 1) {
        // Encode the string value.
        if ((err = SrsProtobufKey::encode(b, 1, SrsProtobufFieldString)) != srs_success) {
            return srs_error_wrap(err, "key");
        }

        if ((err = SrsProtobufString::encode(b, string_value_)) != srs_success) {
            return srs_error_wrap(err, "encode value=%s", string_value_.c_str());
        }
    } else if (used_field_id_ == 3) {
        // Encode the int value.
        if ((err = SrsProtobufKey::encode(b, 3, SrsProtobufFieldVarint)) != srs_success) {
            return srs_error_wrap(err, "key");
        }

        if ((err = SrsProtobufVarints::encode(b, int_value_)) != srs_success) {
            return srs_error_wrap(err, "encode value=%" PRId64, int_value_);
        }
    }

    return err;
}

SrsOtelScopeSpans::SrsOtelScopeSpans()
{
    scope_ = new SrsOtelScope();
}

SrsOtelScopeSpans::~SrsOtelScopeSpans()
{
    srs_freep(scope_);
    for (std::vector<SrsOtelSpan*>::iterator it = spans_.begin(); it != spans_.end(); ++it) {
        SrsOtelSpan* span = *it;
        srs_freep(span);
    }
}

SrsOtelScope* SrsOtelScopeSpans::scope()
{
    return scope_;
}

SrsOtelScopeSpans* SrsOtelScopeSpans::swap(std::vector<SrsOtelSpan*>& spans)
{
    spans_.swap(spans);
    return this;
}

int SrsOtelScopeSpans::size()
{
    return (int)spans_.size();
}

uint64_t SrsOtelScopeSpans::nb_bytes()
{
    uint64_t nn = SrsProtobufKey::sizeof_key() + SrsProtobufObject::sizeof_object(scope_);
    for (std::vector<SrsOtelSpan*>::iterator it = spans_.begin(); it != spans_.end(); ++it) {
        SrsOtelSpan* span = *it;
        nn += SrsProtobufKey::sizeof_key() + SrsProtobufObject::sizeof_object(span);
    }
    return nn;
}

srs_error_t SrsOtelScopeSpans::encode(SrsBuffer* b)
{
    srs_error_t err = srs_success;

    // Encode the scope.
    if ((err = SrsProtobufKey::encode(b, 1, SrsProtobufFieldObject)) != srs_success) {
        return srs_error_wrap(err, "key");
    }

    if ((err = SrsProtobufObject::encode(b, scope_)) != srs_success) {
        return srs_error_wrap(err, "encode scope");
    }

    // Encode each span.
    for (std::vector<SrsOtelSpan*>::iterator it = spans_.begin(); it != spans_.end(); ++it) {
        SrsOtelSpan* span = *it;

        if ((err = SrsProtobufKey::encode(b, 2, SrsProtobufFieldObject)) != srs_success) {
            return srs_error_wrap(err, "key");
        }

        if ((err = SrsProtobufObject::encode(b, span)) != srs_success) {
            return srs_error_wrap(err, "encode span");
        }
    }
    return err;
}

SrsOtelScope::SrsOtelScope()
{
}

SrsOtelScope::~SrsOtelScope()
{
}

uint64_t SrsOtelScope::nb_bytes()
{
    uint64_t nn = 0;
    nn = SrsProtobufKey::sizeof_key() + SrsProtobufString::sizeof_string(name_);
    return nn;
}

srs_error_t SrsOtelScope::encode(SrsBuffer* b)
{
    srs_error_t err = srs_success;

    // Encode the name.
    if ((err = SrsProtobufKey::encode(b, 1, SrsProtobufFieldString)) != srs_success) {
        return srs_error_wrap(err, "key");
    }

    if ((err = SrsProtobufString::encode(b, name_)) != srs_success) {
        return srs_error_wrap(err, "encode name=%s", name_.c_str());
    }

    return err;
}

SrsOtelSpan::SrsOtelSpan()
{
    start_time_unix_nano_ = 0;
    end_time_unix_nano_ = 0;
    status_ = new SrsOtelStatus();
}

SrsOtelSpan::~SrsOtelSpan()
{
    for (vector<SrsOtelAttribute*>::iterator it = attributes_.begin(); it != attributes_.end(); ++it) {
        SrsOtelAttribute* attribute = *it;
        srs_freep(attribute);
    }

    for (vector<SrsOtelEvent*>::iterator it = events_.begin(); it != events_.end(); ++it) {
        SrsOtelEvent* event = *it;
        srs_freep(event);
    }

    for (vector<SrsOtelLink*>::iterator it = links_.begin(); it != links_.end(); ++it) {
        SrsOtelLink* link = *it;
        srs_freep(link);
    }

    srs_freep(status_);
}

SrsOtelAttribute* SrsOtelSpan::attr(const std::string k)
{
    for (vector<SrsOtelAttribute*>::iterator it = attributes_.begin(); it != attributes_.end(); ++it) {
        SrsOtelAttribute* attribute = *it;
        if (attribute->key() == k) {
            return attribute;
        }
    }
    return NULL;
}

uint64_t SrsOtelSpan::nb_bytes()
{
    uint64_t nn = 0;

    nn += SrsProtobufKey::sizeof_key() + SrsProtobufString::sizeof_string(trace_id_);
    nn += SrsProtobufKey::sizeof_key() + SrsProtobufString::sizeof_string(span_id_);
    if (!parent_span_id_.empty()) {
        nn += SrsProtobufKey::sizeof_key() + SrsProtobufString::sizeof_string(parent_span_id_);
    }
    nn += SrsProtobufKey::sizeof_key() + SrsProtobufString::sizeof_string(name_);
    nn += SrsProtobufKey::sizeof_key() + SrsProtobufVarints::sizeof_varint(kind_);
    nn += SrsProtobufKey::sizeof_key() + SrsProtobufFixed64::sizeof_int(start_time_unix_nano_);
    nn += SrsProtobufKey::sizeof_key() + SrsProtobufFixed64::sizeof_int(end_time_unix_nano_);

    for (vector<SrsOtelAttribute*>::iterator it = attributes_.begin(); it != attributes_.end(); ++it) {
        SrsOtelAttribute* attribute = *it;
        nn += SrsProtobufKey::sizeof_key() + SrsProtobufObject::sizeof_object(attribute);
    }

    for (vector<SrsOtelEvent*>::iterator it = events_.begin(); it != events_.end(); ++it) {
        SrsOtelEvent* event = *it;
        nn += SrsProtobufKey::sizeof_key() + SrsProtobufObject::sizeof_object(event);
    }

    for (vector<SrsOtelLink*>::iterator it = links_.begin(); it != links_.end(); ++it) {
        SrsOtelLink* link = *it;
        nn += SrsProtobufKey::sizeof_key() + SrsProtobufObject::sizeof_object(link);
    }

    nn += SrsProtobufKey::sizeof_key() + SrsProtobufObject::sizeof_object(status_);

    return nn;
}

srs_error_t SrsOtelSpan::encode(SrsBuffer* b)
{
    srs_error_t err = srs_success;

    // Encode the trace id.
    if ((err = SrsProtobufKey::encode(b, 1, SrsProtobufFieldString)) != srs_success) {
        return srs_error_wrap(err, "key");
    }

    if ((err = SrsProtobufString::encode(b, trace_id_)) != srs_success) {
        return srs_error_wrap(err, "encode trace_id=%s", trace_id_.c_str());
    }

    // Encode the span id.
    if ((err = SrsProtobufKey::encode(b, 2, SrsProtobufFieldString)) != srs_success) {
        return srs_error_wrap(err, "key");
    }

    if ((err = SrsProtobufString::encode(b, span_id_)) != srs_success) {
        return srs_error_wrap(err, "encode span_id=%s", span_id_.c_str());
    }

    // Encode the parent span id.
    if (!parent_span_id_.empty()) {
        if ((err = SrsProtobufKey::encode(b, 4, SrsProtobufFieldString)) != srs_success) {
            return srs_error_wrap(err, "key");
        }

        if ((err = SrsProtobufString::encode(b, parent_span_id_)) != srs_success) {
            return srs_error_wrap(err, "encode parent_span_id=%s", parent_span_id_.c_str());
        }
    }

    // Encode the name.
    if ((err = SrsProtobufKey::encode(b, 5, SrsProtobufFieldString)) != srs_success) {
        return srs_error_wrap(err, "key");
    }

    if ((err = SrsProtobufString::encode(b, name_)) != srs_success) {
        return srs_error_wrap(err, "encode name=%s", name_.c_str());
    }

    // Encode the kind.
    if ((err = SrsProtobufKey::encode(b, 6, SrsProtobufFieldEnum)) != srs_success) {
        return srs_error_wrap(err, "key");
    }

    if ((err = SrsProtobufVarints::encode(b, kind_)) != srs_success) {
        return srs_error_wrap(err, "encode kind=%d", (int)kind_);
    }

    // Encode the start time.
    if ((err = SrsProtobufKey::encode(b, 7, SrsProtobufField64bit)) != srs_success) {
        return srs_error_wrap(err, "key");
    }

    if ((err = SrsProtobufFixed64::encode(b, start_time_unix_nano_)) != srs_success) {
        return srs_error_wrap(err, "encode start_time=%" PRId64, start_time_unix_nano_);
    }

    // Encode the end time.
    if ((err = SrsProtobufKey::encode(b, 8, SrsProtobufField64bit)) != srs_success) {
        return srs_error_wrap(err, "key");
    }

    if ((err = SrsProtobufFixed64::encode(b, end_time_unix_nano_)) != srs_success) {
        return srs_error_wrap(err, "encode end_time=%" PRId64, end_time_unix_nano_);
    }

    // Encode attribute if not empty.
    for (vector<SrsOtelAttribute*>::iterator it = attributes_.begin(); it != attributes_.end(); ++it) {
        SrsOtelAttribute* attribute = *it;

        if ((err = SrsProtobufKey::encode(b, 9, SrsProtobufFieldObject)) != srs_success) {
            return srs_error_wrap(err, "key");
        }

        if ((err = SrsProtobufObject::encode(b, attribute)) != srs_success) {
            return srs_error_wrap(err, "encode attribute");
        }
    }

    // Encode the events if not empty.
    for (vector<SrsOtelEvent*>::iterator it = events_.begin(); it != events_.end(); ++it) {
        SrsOtelEvent* event = *it;

        if ((err = SrsProtobufKey::encode(b, 11, SrsProtobufFieldObject)) != srs_success) {
            return srs_error_wrap(err, "key");
        }

        if ((err = SrsProtobufObject::encode(b, event)) != srs_success) {
            return srs_error_wrap(err, "encode event");
        }
    }

    // Encode the links if not empty.
    for (vector<SrsOtelLink*>::iterator it = links_.begin(); it != links_.end(); ++it) {
        SrsOtelLink* link = *it;

        if ((err = SrsProtobufKey::encode(b, 13, SrsProtobufFieldObject)) != srs_success) {
            return srs_error_wrap(err, "key");
        }

        if ((err = SrsProtobufObject::encode(b, link)) != srs_success) {
            return srs_error_wrap(err, "encode link");
        }
    }

    // Encode the status.
    if ((err = SrsProtobufKey::encode(b, 15, SrsProtobufFieldObject)) != srs_success) {
        return srs_error_wrap(err, "key");
    }

    if ((err = SrsProtobufObject::encode(b, status_)) != srs_success) {
        return srs_error_wrap(err, "encode status");
    }

    return err;
}

SrsOtelEvent::SrsOtelEvent()
{
    time_ = srs_update_system_time();
}

SrsOtelEvent::~SrsOtelEvent()
{
    for (vector<SrsOtelAttribute*>::iterator it = attributes_.begin(); it != attributes_.end(); ++it) {
        SrsOtelAttribute* attribute = *it;
        srs_freep(attribute);
    }
}

SrsOtelEvent* SrsOtelEvent::create(std::string v)
{
    SrsOtelEvent* e = new SrsOtelEvent();
    e->name_ = v;
    return e;
}

SrsOtelEvent* SrsOtelEvent::add_attr(SrsOtelAttribute* v)
{
    attributes_.push_back(v);
    return this;
}

uint64_t SrsOtelEvent::nb_bytes()
{
    uint64_t nn = 0;
    nn += SrsProtobufKey::sizeof_key() + SrsProtobufFixed64::sizeof_int(time_);
    nn += SrsProtobufKey::sizeof_key() + SrsProtobufString::sizeof_string(name_);
    for (vector<SrsOtelAttribute*>::iterator it = attributes_.begin(); it != attributes_.end(); ++it) {
        SrsOtelAttribute* attribute = *it;
        nn += SrsProtobufKey::sizeof_key() + SrsProtobufObject::sizeof_object(attribute);
    }
    return nn;
}

srs_error_t SrsOtelEvent::encode(SrsBuffer* b)
{
    srs_error_t err = srs_success;

    // Encode the time.
    if ((err = SrsProtobufKey::encode(b, 1, SrsProtobufField64bit)) != srs_success) {
        return srs_error_wrap(err, "key");
    }

    if ((err = SrsProtobufFixed64::encode(b, time_)) != srs_success) {
        return srs_error_wrap(err, "encode time=%" PRId64, time_);
    }

    // Encode the name.
    if ((err = SrsProtobufKey::encode(b, 2, SrsProtobufFieldString)) != srs_success) {
        return srs_error_wrap(err, "key");
    }

    if ((err = SrsProtobufString::encode(b, name_)) != srs_success) {
        return srs_error_wrap(err, "encode key=%s", name_.c_str());
    }

    // Encode attributes.
    for (vector<SrsOtelAttribute*>::iterator it = attributes_.begin(); it != attributes_.end(); ++it) {
        SrsOtelAttribute* attribute = *it;

        if ((err = SrsProtobufKey::encode(b, 3, SrsProtobufFieldObject)) != srs_success) {
            return srs_error_wrap(err, "key");
        }

        if ((err = SrsProtobufObject::encode(b, attribute)) != srs_success) {
            return srs_error_wrap(err, "encode attribute");
        }
    }

    return err;
}

SrsOtelLink::SrsOtelLink()
{
}

SrsOtelLink::~SrsOtelLink()
{
}

SrsOtelLink* SrsOtelLink::create()
{
    return new SrsOtelLink();
}

SrsOtelLink* SrsOtelLink::set_id(const std::string& trace_id, const std::string& span_id)
{
    trace_id_ = trace_id;
    span_id_ = span_id;
    return this;
}

uint64_t SrsOtelLink::nb_bytes()
{
    uint64_t nn = 0;
    nn += SrsProtobufKey::sizeof_key() + SrsProtobufString::sizeof_string(trace_id_);
    nn += SrsProtobufKey::sizeof_key() + SrsProtobufString::sizeof_string(span_id_);
    return nn;
}

srs_error_t SrsOtelLink::encode(SrsBuffer* b)
{
    srs_error_t err = srs_success;

    // Encode the trace id.
    if ((err = SrsProtobufKey::encode(b, 1, SrsProtobufFieldString)) != srs_success) {
        return srs_error_wrap(err, "key");
    }

    if ((err = SrsProtobufString::encode(b, trace_id_)) != srs_success) {
        return srs_error_wrap(err, "encode trace_id=%s", trace_id_.c_str());
    }

    // Encode the span id.
    if ((err = SrsProtobufKey::encode(b, 2, SrsProtobufFieldString)) != srs_success) {
        return srs_error_wrap(err, "key");
    }

    if ((err = SrsProtobufString::encode(b, span_id_)) != srs_success) {
        return srs_error_wrap(err, "encode span_id=%s", span_id_.c_str());
    }

    return err;
}

SrsOtelStatus::SrsOtelStatus()
{
    code_ = SrsApmStatusUnset;
}

SrsOtelStatus::~SrsOtelStatus()
{
}

uint64_t SrsOtelStatus::nb_bytes()
{
    uint64_t nn = 0;
    if (!message_.empty()) {
        nn += SrsProtobufKey::sizeof_key() + SrsProtobufString::sizeof_string(message_);
    }
    if (code_ != SrsApmStatusUnset) {
        nn += SrsProtobufKey::sizeof_key() + SrsProtobufVarints::sizeof_varint(code_);
    }
    return nn;
}

srs_error_t SrsOtelStatus::encode(SrsBuffer* b)
{
    srs_error_t err = srs_success;

    // Encode the message.
    if (!message_.empty()) {
        if ((err = SrsProtobufKey::encode(b, 2, SrsProtobufFieldString)) != srs_success) {
            return srs_error_wrap(err, "key");
        }

        if ((err = SrsProtobufString::encode(b, message_)) != srs_success) {
            return srs_error_wrap(err, "encode message=%s", message_.c_str());
        }
    }

    // Encode the status.
    if (code_ != SrsApmStatusUnset) {
        if ((err = SrsProtobufKey::encode(b, 3, SrsProtobufFieldEnum)) != srs_success) {
            return srs_error_wrap(err, "key");
        }

        if ((err = SrsProtobufVarints::encode(b, code_)) != srs_success) {
            return srs_error_wrap(err, "encode kind=%d", (int) code_);
        }
    }

    return err;
}

SrsApmClient* _srs_apm = NULL;

SrsApmContext::SrsApmContext(const std::string& name)
{
    name_ = name;
    kind_ = SrsApmKindUnspecified;

    set_trace_id(srs_random_str(16));
    set_span_id(srs_random_str(8));
    // We must not use time cache, or span render might fail.
    start_time_ = srs_update_system_time();
    ended_ = false;
    status_ = SrsApmStatusUnset;
    err_ = srs_success;

    span_ = NULL;
    parent_ = NULL;
}

SrsApmContext::~SrsApmContext()
{
    srs_freep(err_);

    // Span is not created by context, so should never free it here.
    if (span_) {
        span_->ctx_ = NULL;
        span_ = NULL;
    }

    // Free all child context.
    for (vector<SrsApmContext*>::iterator it = childs_.begin(); it != childs_.end(); ++it) {
        SrsApmContext* ctx = *it;
        srs_freep(ctx);
    }

    for (vector<SrsOtelAttribute*>::iterator it = attributes_.begin(); it != attributes_.end(); ++it) {
        SrsOtelAttribute* attribute = *it;
        srs_freep(attribute);
    }

    for (vector<SrsOtelLink*>::iterator it = links_.begin(); it != links_.end(); ++it) {
        SrsOtelLink* link = *it;
        srs_freep(link);
    }
}

void SrsApmContext::set_trace_id(std::string v)
{
    trace_id_ = v;
    str_trace_id_ = srs_string_dumps_hex(trace_id_.data(), trace_id_.length(), INT_MAX, 0, INT_MAX, 0);
}

void SrsApmContext::set_span_id(std::string v)
{
    span_id_ = v;
    str_span_id_ = srs_string_dumps_hex(span_id_.data(), span_id_.length(), INT_MAX, 0, INT_MAX, 0);
}

const char* SrsApmContext::format_trace_id()
{
    return str_trace_id_.c_str();
}

const char* SrsApmContext::format_span_id()
{
    return str_span_id_.c_str();
}

SrsApmContext* SrsApmContext::root()
{
    // Root is node that has no parent.
    if (!parent_) return this;

    // Use cached root or parent root, literally they should be the same.
    return parent_->root();
}

void SrsApmContext::set_parent(SrsApmContext* parent)
{
    if (ended_) return;

    parent_ = parent;
    if (parent) {
        set_trace_id(parent->trace_id_);
        parent_span_id_ = parent->span_id_;
        parent->childs_.push_back(this);
    }
}

void SrsApmContext::set_status(SrsApmStatus status, const std::string& description)
{
    if (ended_) return;

    status_ = status;
    if (status == SrsApmStatusError) {
        description_ = description;
    }
}

bool SrsApmContext::all_ended()
{
    if (!ended_) return false;

    for (vector<SrsApmContext*>::iterator it = childs_.begin(); it != childs_.end(); ++it) {
        SrsApmContext* ctx = *it;
        if (!ctx->all_ended()) return false;
    }

    return true;
}

int SrsApmContext::count_spans()
{
    int nn = span_ ? 1 : 0;

    for (vector<SrsApmContext*>::iterator it = childs_.begin(); it != childs_.end(); ++it) {
        SrsApmContext* ctx = *it;
        nn += ctx->count_spans();
    }

    return nn;
}

void SrsApmContext::update_trace_id(std::string v)
{
    if (ended_) return;

    set_trace_id(v);

    for (vector<SrsApmContext*>::iterator it = childs_.begin(); it != childs_.end(); ++it) {
        SrsApmContext* ctx = *it;
        ctx->set_trace_id(v);
    }
}

void SrsApmContext::link(SrsApmContext* to)
{
    if (ended_) return;

    links_.push_back(SrsOtelLink::create()->set_id(to->trace_id_, to->span_id_));
    attributes_.push_back(SrsOtelAttribute::kv("link_trace", to->str_trace_id_));
    attributes_.push_back(SrsOtelAttribute::kv("link_span", to->str_span_id_));

    to->links_.push_back(SrsOtelLink::create()->set_id(trace_id_, span_id_));
    to->attributes_.push_back(SrsOtelAttribute::kv("link_trace", str_trace_id_));
    to->attributes_.push_back(SrsOtelAttribute::kv("link_span", str_span_id_));

    srs_trace("APM: Link span %s(trace=%s, span=%s) with %s(trace=%s, span=%s)",
        name_.c_str(), str_trace_id_.c_str(), str_span_id_.c_str(), to->name_.c_str(),
        to->str_trace_id_.c_str(), to->str_span_id_.c_str()
    );
}

void SrsApmContext::end()
{
    if (ended_) return;
    ended_ = true;

    SrsOtelSpan* otel = new SrsOtelSpan();

    otel->name_ = name_;
    otel->trace_id_ = trace_id_;
    otel->span_id_ = span_id_;
    otel->parent_span_id_ = parent_span_id_;
    otel->kind_ = kind_;
    otel->start_time_unix_nano_ = start_time_ * 1000;
    // We must not use time cache, or span render might fail.
    otel->end_time_unix_nano_ = srs_update_system_time() * 1000;

    otel->status_->code_ = status_;
    otel->status_->message_ = description_;

    otel->attributes_.swap(attributes_);
    otel->links_.swap(links_);

    // Insert server ip if not exists.
    if (!otel->attr("ip")) {
        otel->attributes_.push_back(SrsOtelAttribute::kv("ip", srs_get_public_internet_address()));
    }
    if (!otel->attr("component")) {
        otel->attributes_.push_back(SrsOtelAttribute::kv("component", "srs"));
    }

    // See https://github.com/open-telemetry/opentelemetry-specification/blob/main/specification/trace/semantic_conventions/exceptions.md
    if (err_ != srs_success) {
        // Set the events for detail about the error.
        otel->events_.push_back(SrsOtelEvent::create("exception")
            ->add_attr(SrsOtelAttribute::kv("exception.type", srs_fmt("code_%d_%s", srs_error_code(err_), srs_error_code_str(err_).c_str())))
            ->add_attr(SrsOtelAttribute::kv("exception.message", srs_error_summary(err_)))
            ->add_attr(SrsOtelAttribute::kv("exception.stacktrace", srs_error_desc(err_)))
        );

        // We also use HTTP status code for APM to class the error. Note that it also works for non standard HTTP status
        // code, for example, SRS error codes.
        otel->attributes_.push_back(SrsOtelAttribute::kv("http.status_code", srs_fmt("%d", srs_error_code(err_))));
    }

    _srs_apm->snapshot(otel);
    srs_info("APM: Snapshot name=%s, trace=%s, span=%s", name_.c_str(), trace_id_.c_str(), span_id_.c_str());
}

SrsApmSpan::SrsApmSpan(const std::string& name)
{
    child_ = false;

    // Create the context for this span.
    ctx_ = new SrsApmContext(name);
    ctx_->span_ = this;

    // Default to internal span.
    ctx_->kind_ = SrsApmKindInternal;
}

SrsApmSpan::~SrsApmSpan()
{
    end();

    // Context might be freed by other span.
    if (!ctx_) return;

    // Span is not available.
    ctx_->span_ = NULL;

    // Dispose the context tree when all spans are ended.
    SrsApmContext* root = ctx_->root();

    // Only free the tree when free the last span, because we might create new span even all spans are ended only if the
    // root span has not been freed, for example, when RTMP client cycle done, we create a final span.
    if (root->count_spans() == 0 && root->all_ended()) {
        srs_freep(root);
    }
}

const char* SrsApmSpan::format_trace_id()
{
    return ctx_ ? ctx_->format_trace_id() : "";
}

const char* SrsApmSpan::format_span_id()
{
    return ctx_ ? ctx_->format_span_id() : "";
}

ISrsApmSpan* SrsApmSpan::set_name(const std::string& name)
{
    if (ctx_) ctx_->name_ = name;
    return this;
}

ISrsApmSpan* SrsApmSpan::set_kind(SrsApmKind kind)
{
    if (ctx_) ctx_->kind_ = kind;
    return this;
}

ISrsApmSpan* SrsApmSpan::as_child(ISrsApmSpan* parent)
{
    // Should not be child of multiple parent spans.
    if (child_) return this;

    // For child, always load parent from context.
    SrsApmSpan* span = dynamic_cast<SrsApmSpan*>(parent);
    if (span) {
        ctx_->set_parent(span->ctx_);
        child_ = true;
    }

    return this;
}

ISrsApmSpan* SrsApmSpan::set_status(SrsApmStatus status, const std::string& description)
{
    if (ctx_) ctx_->set_status(status, description);
    return this;
}

ISrsApmSpan* SrsApmSpan::record_error(srs_error_t err)
{
    if (ctx_) ctx_->err_ = srs_error_copy(err);
    return this;
}

ISrsApmSpan* SrsApmSpan::attr(const std::string& k, const std::string& v)
{
    if (ctx_) ctx_->attributes_.push_back(SrsOtelAttribute::kv(k, v));
    return this;
}

ISrsApmSpan* SrsApmSpan::link(ISrsApmSpan* span)
{
    SrsApmSpan* to = dynamic_cast<SrsApmSpan*>(span);
    if (ctx_ && span) ctx_->link(to->ctx_);
    return this;
}

ISrsApmSpan* SrsApmSpan::end()
{
    if (ctx_) ctx_->end();
    return this;
}

ISrsApmSpan* SrsApmSpan::extract(SrsAmf0Object* h)
{
    if (!ctx_) return this;

    SrsAmf0Any* prop = h->ensure_property_string("Traceparent");
    if (!prop) return this;

    std::string trace_parent = prop->to_str();
    vector<string> vs = srs_string_split(trace_parent, "-");
    if (vs.size() != 4) return this;

    // Update trace id for span and all its child spans.
    ctx_->update_trace_id(vs[1]);

    // Update the parent span id of span, note that the parent is NULL.
    ctx_->parent_span_id_ = vs[2];

    return this;
}

ISrsApmSpan* SrsApmSpan::inject(SrsAmf0Object* h)
{
    if (!ctx_) return this;

    string v = text_propagator();
    if (!v.empty()) h->set("Traceparent", SrsAmf0Any::str(v.c_str()));

    return this;
}

std::string SrsApmSpan::text_propagator()
{
    // FlagsSampled is a bitmask with the sampled bit set. A SpanContext
    // with the sampling bit set means the span is sampled.
    const uint8_t FlagsSampled = 0x01;
    const uint8_t supportedVersion = 0;
    static char buf[256];

    // For text based propagation, for example, HTTP header "Traceparent: 00-bb8dedf16c53ab4b6ceb1f4ca6d985bb-29247096662468ab-01"
    // About the "%.2x", please see https://www.quora.com/What-does-2x-do-in-C-code for detail.
    int nn = snprintf(buf, sizeof(buf), "%.2x-%s-%s-%.2x", supportedVersion, ctx_->trace_id_.c_str(), ctx_->span_id_.c_str(), FlagsSampled);
    if (nn > 0 && nn < (int)sizeof(buf)) {
        return string(buf, nn);
    }

    return "";
}

static int _srs_apm_key = -1;
void _srs_apm_destructor(void* arg)
{
    // We don't free the span, because it's not created by us.
    // Note that it's safe because keys will be reset when coroutine is terminated.
    //SrsApmSpan* span = (SrsApmSpan*)arg;
}

ISrsApmSpan* SrsApmSpan::store()
{
    ISrsApmSpan* span = _srs_apm->load();
    if (span == this) return this;

    int r0 = srs_thread_setspecific(_srs_apm_key, this);
    srs_assert(r0 == 0);
    return this;
}

ISrsApmSpan* SrsApmSpan::load()
{
    if (_srs_apm_key < 0) {
        int r0 = srs_key_create(&_srs_apm_key, _srs_apm_destructor);
        srs_assert(r0 == 0);
    }

    void* span = srs_thread_getspecific(_srs_apm_key);
    return (ISrsApmSpan*)span;
}

SrsApmClient::SrsApmClient()
{
    enabled_ = false;
    nn_spans_ = 0;
}

SrsApmClient::~SrsApmClient()
{
    for (vector<SrsOtelSpan*>::iterator it = spans_.begin(); it != spans_.end(); ++it) {
        SrsOtelSpan* span = *it;
        srs_freep(span);
    }
}

srs_error_t SrsApmClient::initialize()
{
    srs_error_t err = srs_success;

    enabled_ = _srs_config->get_tencentcloud_apm_enabled();
    if (!enabled_) {
        srs_trace("TencentCloud APM is disabled");
        return err;
    }

    team_ = _srs_config->get_tencentcloud_apm_team();
    token_ = _srs_config->get_tencentcloud_apm_token();
    endpoint_ = _srs_config->get_tencentcloud_apm_endpoint();
    service_name_ = _srs_config->get_tencentcloud_apm_service_name();
    debug_logging_ = _srs_config->get_tencentcloud_apm_debug_logging();
    srs_trace("Initialize TencentCloud APM, team=%s, token=%dB, endpoint=%s, service_name=%s, debug_logging=%d", team_.c_str(), token_.length(), endpoint_.c_str(), service_name_.c_str(), debug_logging_);

    // Check authentication, the team or token.
    if (team_.empty()) {
        return srs_error_new(ERROR_APM_AUTH, "No authentication team for APM");
    }
    if (token_.empty()) {
        return srs_error_new(ERROR_APM_AUTH, "No authentication token for APM");
    }

    // Please note that 4317 is for GRPC/HTTP2, while SRS only support HTTP and the port shoule be 55681.
    if (srs_string_contains(endpoint_, ":4317")) {
        return srs_error_new(ERROR_APM_ENDPOINT, "Port 4317 is for GRPC over HTTP2 for APM");
    }

    return err;
}

srs_error_t SrsApmClient::report()
{
    srs_error_t err = do_report();
    if (err != srs_success) {
        return srs_error_wrap(err, "team=%s, token=%dB", team_.c_str(), token_.length());
    }

    return err;
}

srs_error_t SrsApmClient::do_report()
{
    srs_error_t err = srs_success;

    if (spans_.empty()) return err;

    // Update statistaic for APM.
    nn_spans_ += spans_.size();

    SrsOtelExportTraceServiceRequest* sugar = new SrsOtelExportTraceServiceRequest();
    SrsAutoFree(SrsOtelExportTraceServiceRequest, sugar);

    SrsOtelResourceSpans* rs = sugar->append();
    // See https://github.com/open-telemetry/opentelemetry-specification/tree/main/specification/resource/semantic_conventions
    rs->resource()->add_addr(SrsOtelAttribute::kv("service.name", service_name_));
    // For Tencent Cloud APM authentication, see https://console.cloud.tencent.com/apm/monitor/access
    rs->resource()->add_addr(SrsOtelAttribute::kv("token", token_));
    // For Tencent Cloud APM debugging, see https://console.cloud.tencent.com/apm/monitor/team
    rs->resource()->add_addr(SrsOtelAttribute::kv("tapm.team", team_));

    SrsOtelScopeSpans* spans = rs->append();
    spans->scope()->name_ = "srs";
    spans->swap(spans_);

    // Send out over HTTP1.
    uint64_t size = sugar->nb_bytes();
    if (size >= 5 * 1024 * 1024) {
        return srs_error_new(ERROR_APM_EXCEED_SIZE, "exceed 5MB actual %d", size);
    }

    char* buf = new char[size];
    SrsAutoFreeA(char, buf);

    memset(buf, 0, size);
    SrsBuffer b(buf, size);
    if ((err = sugar->encode(&b)) != srs_success) {
        return srs_error_wrap(err, "encode log");
    }

    string body(buf, size);

    // Write a CLS log to service specified by url.
    string url = "http://" + endpoint_ + "/v1/traces";

    SrsHttpUri uri;
    if ((err = uri.initialize(url)) != srs_success) {
        return srs_error_wrap(err, "http: post failed. url=%s", url.c_str());
    }

    SrsHttpClient http;
    if ((err = http.initialize(uri.get_schema(), uri.get_host(), uri.get_port())) != srs_success) {
        return srs_error_wrap(err, "http: init client");
    }

    // Setup the request.
    if (true) {
        http.set_header("Content-Type", "application/x-protobuf");
    }

    string path = uri.get_path();
    if (!uri.get_query().empty()) {
        path += "?" + uri.get_query();
    }

    // Start request and parse response.
    ISrsHttpMessage* msg = NULL;
    if ((err = http.post(path, body, &msg)) != srs_success) {
        return srs_error_wrap(err, "http: client post");
    }
    SrsAutoFree(ISrsHttpMessage, msg);

    string res;
    uint16_t code = msg->status_code();
    if ((err = msg->body_read_all(res)) != srs_success) {
        return srs_error_wrap(err, "http: body read");
    }

    // ensure the http status is ok.
    if (code != SRS_CONSTS_HTTP_OK && code != SRS_CONSTS_HTTP_Created) {
        return srs_error_new(ERROR_HTTP_STATUS_INVALID, "http: status %d, body is %s", code, res.c_str());
    }

    if (debug_logging_) {
        string server_id = SrsStatistic::instance()->server_id();
        srs_trace("APM write team=%s, token=%dB, logs=%d, size=%dB, server_id=%s", team_.c_str(), token_.length(), spans->size(), body.length(), server_id.c_str());
    }

    return err;
}

bool SrsApmClient::enabled()
{
    return enabled_;
}

uint64_t SrsApmClient::nn_spans()
{
    return nn_spans_;
}

ISrsApmSpan* SrsApmClient::span(const std::string& name)
{
    if (!enabled_) return new ISrsApmSpan();
    return new SrsApmSpan(name);
}

ISrsApmSpan* SrsApmClient::dummy()
{
    return new ISrsApmSpan();
}

ISrsApmSpan* SrsApmClient::extract(ISrsApmSpan* v, SrsAmf0Object* h)
{
    SrsApmSpan* span = dynamic_cast<SrsApmSpan*>(v);
    if (span) span->extract(h);
    return v;
}

ISrsApmSpan* SrsApmClient::inject(ISrsApmSpan* v, SrsAmf0Object* h)
{
    SrsApmSpan* span = dynamic_cast<SrsApmSpan*>(v);
    if (span) span->inject(h);
    return v;
}

void SrsApmClient::store(ISrsApmSpan* v)
{
    SrsApmSpan* span = dynamic_cast<SrsApmSpan*>(v);
    if (span) span->store();
}

ISrsApmSpan* SrsApmClient::load()
{
    static ISrsApmSpan* dummy = new ISrsApmSpan();
    ISrsApmSpan* span = SrsApmSpan::load();
    return span ? span : dummy;
}

void SrsApmClient::snapshot(SrsOtelSpan* span)
{
    spans_.push_back(span);
}
#endif

