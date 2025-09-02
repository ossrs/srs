//
// Copyright (c) 2013-2025 The SRS Authors
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
class SrsKbps;
class SrsSimpleRtmpClient;

// Forward the stream to other servers.
class SrsForwarder : public ISrsCoroutineHandler
{
private:
    // The ep to forward, server[:port].
    std::string ep_forward;
    ISrsRequest *req;

private:
    // The source or stream context id to bind to.
    SrsContextId source_cid_;

private:
    SrsCoroutine *trd;

private:
    SrsOriginHub *hub;
    SrsSimpleRtmpClient *sdk;
    SrsRtmpJitter *jitter;
    SrsMessageQueue *queue;
    // Cache the sequence header for retry when slave is failed.
    SrsMediaPacket *sh_audio;
    SrsMediaPacket *sh_video;

public:
    SrsForwarder(SrsOriginHub *h);
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

private:
    virtual srs_error_t do_cycle();

private:
    virtual srs_error_t forward();
};

#endif
