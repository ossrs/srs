//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_APP_FORWARD_HPP
#define SRS_APP_FORWARD_HPP

#include <srs_core.hpp>

#include <string>

#include <srs_app_st.hpp>

class ISrsProtocolReadWriter;
class SrsMediaPacket;
class SrsOnMetaDataPacket;
class SrsMessageQueue;
class SrsRtmpJitter;
class SrsRtmpClient;
class ISrsRequest;
class SrsLiveSource;
class SrsOriginHub;
class ISrsOriginHub;
class SrsKbps;
class SrsSimpleRtmpClient;
class ISrsBasicRtmpClient;
class ISrsAppFactory;
class ISrsAppConfig;
class ISrsStatistic;

class ISrsForwarder
{
public:
    ISrsForwarder();
    virtual ~ISrsForwarder();

public:
    virtual srs_error_t initialize(ISrsRequest *r, std::string ep) = 0;
    virtual void set_queue_size(srs_utime_t queue_size) = 0;
    virtual srs_error_t on_publish() = 0;
    virtual void on_unpublish() = 0;
    virtual srs_error_t on_meta_data(SrsMediaPacket *shared_metadata) = 0;
    virtual srs_error_t on_audio(SrsMediaPacket *shared_audio) = 0;
    virtual srs_error_t on_video(SrsMediaPacket *shared_video) = 0;
};

class SrsForwarder : public ISrsCoroutineHandler, public ISrsForwarder
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsAppFactory *app_factory_;
    ISrsAppConfig *config_;
    ISrsStatistic *stat_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // Identifies this forwarder in the HTTP API.
    std::string id_;
    std::string ep_forward_;
    ISrsRequest *req_;
    // The publishing stream this forwarder belongs to. It differs from req_ for a forward.backend
    // url, which has its own app and stream.
    ISrsRequest *stream_req_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    SrsContextId source_cid_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsCoroutine *trd_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsOriginHub *hub_;
    ISrsBasicRtmpClient *sdk_;
    SrsRtmpJitter *jitter_;
    SrsMessageQueue *queue_;
    SrsMediaPacket *metadata_;
    SrsMediaPacket *sh_audio_;
    SrsMediaPacket *sh_video_;
    // So the destination never starts from the middle of a GOP.
    bool wait_keyframe_;

public:
    SrsForwarder(ISrsOriginHub *h, ISrsRequest *req);
    virtual ~SrsForwarder();

public:
    virtual srs_error_t initialize(ISrsRequest *r, std::string ep);
    virtual void set_queue_size(srs_utime_t queue_size);

public:
    virtual srs_error_t on_publish();
    virtual void on_unpublish();
    virtual srs_error_t on_meta_data(SrsMediaPacket *shared_metadata);
    virtual srs_error_t on_audio(SrsMediaPacket *shared_audio);
    virtual srs_error_t on_video(SrsMediaPacket *shared_video);

public:
    virtual srs_error_t cycle();

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t do_cycle();
    virtual std::string destination_url();

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t forward();
    virtual srs_error_t enqueue(SrsMediaPacket *msg);
};

#endif
