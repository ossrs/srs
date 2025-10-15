//
// Copyright (c) 2013-2025 The SRS Authors
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

// The stream publish token represents exclusive access to publish a stream.
// Only one publisher can hold a token for a given stream URL at any time.
// This prevents race conditions across all protocols (RTMP, RTC, SRT, etc.).
class SrsStreamPublishToken
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // The stream URL this token is for
    std::string stream_url_;
    // Whether this token is currently acquired
    bool acquired_;
    // The token manager that created this token
    SrsStreamPublishTokenManager *manager_;
    // The context ID of the publisher that acquired this token
    SrsContextId publisher_cid_;

public:
    SrsStreamPublishToken(const std::string &stream_url, SrsStreamPublishTokenManager *manager);
    virtual ~SrsStreamPublishToken();

public:
    // Get the stream URL this token is for
    std::string stream_url();

    // Check if this token is currently acquired
    bool is_acquired();

    void set_acquired(bool acquired);

    // Get the publisher context ID that acquired this token
    const SrsContextId &publisher_cid();

    void set_publisher_cid(const SrsContextId &cid);
};

// The interface for stream publish token manager
class ISrsStreamPublishTokenManager
{
public:
    ISrsStreamPublishTokenManager();
    virtual ~ISrsStreamPublishTokenManager();

public:
    virtual srs_error_t acquire_token(ISrsRequest *req, SrsStreamPublishToken *&token) = 0;
    virtual void release_token(const std::string &stream_url) = 0;
};

// The global stream publish token manager ensures only one publisher
// can acquire a token for a given stream URL at any time.
// This prevents race conditions across all protocols.
class SrsStreamPublishTokenManager : public ISrsStreamPublishTokenManager
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // Map of stream URL to token
    std::map<std::string, SrsStreamPublishToken *>
        tokens_;
    // Mutex to protect the tokens map
    srs_mutex_t mutex_;

public:
    SrsStreamPublishTokenManager();
    virtual ~SrsStreamPublishTokenManager();

public:
    // Acquire a publish token for the given stream URL.
    // Returns success if token was acquired, error if stream is already being published.
    // @param req The request containing stream information
    // @param token Output parameter for the acquired token (will be set to NULL on error)
    srs_error_t acquire_token(ISrsRequest *req, SrsStreamPublishToken *&token);

    // Release a publish token for the given stream URL.
    // This is called automatically when the token is destroyed.
    // @param stream_url The stream URL to release the token for
    void release_token(const std::string &stream_url);
};

// Global instance accessor
extern SrsStreamPublishTokenManager *_srs_stream_publish_tokens;

#endif
