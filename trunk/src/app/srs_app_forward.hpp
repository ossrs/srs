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

// The forward interface.
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

// Forward the stream to other servers.
class SrsForwarder : public ISrsCoroutineHandler, public ISrsForwarder
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsAppFactory *app_factory_;
    ISrsAppConfig *config_;
    ISrsStatistic *stat_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // The forward id, which identifies this forwarder in the statistic and the HTTP API.
    std::string id_;
    // The ep to forward, server[:port].
    std::string ep_forward_;
    ISrsRequest *req_;
    // The request of the publishing stream this forwarder belongs to, for the statistic. It differs
    // from req_ for a forward.backend url, which has its own app and stream.
    ISrsRequest *stream_req_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // The source or stream context id to bind to.
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
    // Cache the metadata and sequence headers, sent first after each publish to the destination.
    SrsMediaPacket *metadata_;
    SrsMediaPacket *sh_audio_;
    SrsMediaPacket *sh_video_;
    // Whether to drop video until the next keyframe, after a publish or a queue overflow, so the
    // destination never starts from the middle of a GOP. Audio and sequence headers pass.
    bool wait_keyframe_;

public:
    // The req is the publishing stream this forwarder belongs to.
    SrsForwarder(ISrsOriginHub *h, ISrsRequest *req);
    virtual ~SrsForwarder();

public:
    virtual srs_error_t initialize(ISrsRequest *r, std::string ep);
    virtual void set_queue_size(srs_utime_t queue_size);

public:
    virtual srs_error_t on_publish();
    virtual void on_unpublish();
    // Forward the audio packet.
    // @param shared_metadata, directly ptr, copy it if need to save it.
    virtual srs_error_t on_meta_data(SrsMediaPacket *shared_metadata);
    // Forward the audio packet.
    // @param shared_audio, directly ptr, copy it if need to save it.
    virtual srs_error_t on_audio(SrsMediaPacket *shared_audio);
    // Forward the video packet.
    // @param shared_video, directly ptr, copy it if need to save it.
    virtual srs_error_t on_video(SrsMediaPacket *shared_video);
    // Interface ISrsReusableThread2Handler.
public:
    virtual srs_error_t cycle();

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t do_cycle();
    // The full destination url to publish to, with the stream key and query.
    virtual std::string destination_url();

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t forward();
    // Enqueue a packet, and wait for the next keyframe if the queue overflows and shrinks.
    virtual srs_error_t enqueue(SrsMediaPacket *msg);
};

#endif
