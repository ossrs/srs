//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_app_stream_token.hpp>

#include <srs_app_config.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_protocol_rtmp_stack.hpp>
#include <srs_protocol_st.hpp>

// Global instance
SrsStreamPublishTokenManager *_srs_stream_publish_tokens = NULL;

ISrsStreamPublishToken::ISrsStreamPublishToken()
{
}

ISrsStreamPublishToken::~ISrsStreamPublishToken()
{
}

SrsStreamPublishToken::SrsStreamPublishToken(const std::string &stream_url, ISrsStreamPublishTokenManager *manager)
{
    stream_url_ = stream_url;
    acquired_ = false;
    manager_ = manager;
    publisher_cid_ = SrsContextId();
}

SrsStreamPublishToken::~SrsStreamPublishToken()
{
    // Automatically release the token when destroyed
    if (acquired_ && manager_) {
        manager_->release_token(stream_url_);
    }
}

std::string SrsStreamPublishToken::stream_url()
{
    return stream_url_;
}

bool SrsStreamPublishToken::is_acquired()
{
    return acquired_;
}

void SrsStreamPublishToken::set_acquired(bool acquired)
{
    acquired_ = acquired;
}

const SrsContextId &SrsStreamPublishToken::publisher_cid()
{
    return publisher_cid_;
}

void SrsStreamPublishToken::set_publisher_cid(const SrsContextId &cid)
{
    publisher_cid_ = cid;
}

ISrsStreamPublishTokenManager::ISrsStreamPublishTokenManager()
{
}

ISrsStreamPublishTokenManager::~ISrsStreamPublishTokenManager()
{
}

SrsStreamPublishTokenManager::SrsStreamPublishTokenManager()
{
    mutex_ = srs_mutex_new();
}

SrsStreamPublishTokenManager::~SrsStreamPublishTokenManager()
{
    // Clean up all remaining tokens. Each token's destructor automatically calls
    // release_token() which removes it from tokens_ map, so we use while loop
    // to avoid iterator invalidation issues.
    while (!tokens_.empty()) {
        SrsStreamPublishToken *token = tokens_.begin()->second;

        // Token destructor will call release_token() and remove this entry from map
        srs_freep(token);
    }

    srs_mutex_destroy(mutex_);
    srs_trace("stream publish token manager destroyed");
}

srs_error_t SrsStreamPublishTokenManager::acquire_token(ISrsRequest *req, SrsStreamPublishToken *&token)
{
    srs_error_t err = srs_success;

    std::string stream_url = req->get_stream_url();
    SrsContextId current_cid = _srs_context->get_id();

    SrsLocker(&mutex_);

    // Get or create token for this stream
    SrsStreamPublishToken *stream_token = NULL;

    std::map<std::string, SrsStreamPublishToken *>::iterator it = tokens_.find(stream_url);
    if (it != tokens_.end()) {
        stream_token = it->second;
    } else {
        stream_token = new SrsStreamPublishToken(stream_url, this);
        tokens_[stream_url] = stream_token;
    }

    // Check if token is already acquired by another publisher
    if (stream_token->is_acquired()) {
        SrsContextId existing_cid = stream_token->publisher_cid();
        return srs_error_new(ERROR_SYSTEM_STREAM_BUSY,
                             "stream %s is busy, acquired by cid=%s, current cid=%s",
                             stream_url.c_str(), existing_cid.c_str(), current_cid.c_str());
    } else {
        stream_token->set_acquired(true);
        stream_token->set_publisher_cid(current_cid);
    }

    // Return the token from the map (caller will manage its lifetime)
    token = stream_token;

    return err;
}

void SrsStreamPublishTokenManager::release_token(const std::string &stream_url)
{
    SrsLocker(&mutex_);

    // Find and erase the token from the map
    std::map<std::string, SrsStreamPublishToken *>::iterator it = tokens_.find(stream_url);
    srs_assert(it != tokens_.end());

    SrsStreamPublishToken *token = it->second;
    token->set_acquired(false);
    srs_trace("stream publish token released and deleted, url=%s", stream_url.c_str());

    // Erase from map first, then delete the token
    tokens_.erase(it);
}

bool SrsStreamPublishTokenManager::is_acquired(const std::string &stream_url)
{
    SrsLocker(&mutex_);

    std::map<std::string, SrsStreamPublishToken *>::iterator it = tokens_.find(stream_url);
    if (it == tokens_.end()) {
        return false;
    }

    SrsStreamPublishToken *token = it->second;
    return token->is_acquired();
}
