//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_APP_STREAM_TOKEN_HPP
#define SRS_APP_STREAM_TOKEN_HPP

#include <srs_core.hpp>

#include <map>
#include <string>

#include <srs_core_autofree.hpp>
#include <srs_kernel_error.hpp>
#include <srs_protocol_st.hpp>

class ISrsRequest;
class SrsStreamPublishTokenManager;
class ISrsStreamPublishTokenManager;

class ISrsStreamPublishToken
{
public:
    ISrsStreamPublishToken();
    virtual ~ISrsStreamPublishToken();

public:
    virtual bool is_acquired() = 0;
    virtual void set_acquired(bool acquired) = 0;
};

// Only one publisher, over any protocol (RTMP, RTC, SRT, etc.), can hold the token of a stream URL.
class SrsStreamPublishToken : public ISrsStreamPublishToken
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    std::string stream_url_;
    bool acquired_;
    ISrsStreamPublishTokenManager *manager_;
    SrsContextId publisher_cid_;

public:
    SrsStreamPublishToken(const std::string &stream_url, ISrsStreamPublishTokenManager *manager);
    virtual ~SrsStreamPublishToken();

public:
    std::string stream_url();

    bool is_acquired();

    void set_acquired(bool acquired);

    const SrsContextId &publisher_cid();

    void set_publisher_cid(const SrsContextId &cid);
};

class ISrsStreamPublishTokenManager
{
public:
    ISrsStreamPublishTokenManager();
    virtual ~ISrsStreamPublishTokenManager();

public:
    virtual srs_error_t acquire_token(ISrsRequest *req, SrsStreamPublishToken *&token) = 0;
    virtual void release_token(const std::string &stream_url) = 0;
    virtual bool is_acquired(const std::string &stream_url) = 0;
};

class SrsStreamPublishTokenManager : public ISrsStreamPublishTokenManager
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    std::map<std::string, SrsStreamPublishToken *>
        tokens_;
    srs_mutex_t mutex_;

public:
    SrsStreamPublishTokenManager();
    virtual ~SrsStreamPublishTokenManager();

public:
    srs_error_t acquire_token(ISrsRequest *req, SrsStreamPublishToken *&token);

    // Called by the token destructor.
    void release_token(const std::string &stream_url);

    bool is_acquired(const std::string &stream_url);
};

extern SrsStreamPublishTokenManager *_srs_stream_publish_tokens;

#endif
