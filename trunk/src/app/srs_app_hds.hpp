//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_APP_HDS_HPP
#define SRS_APP_HDS_HPP

#include <srs_core.hpp>

#ifdef SRS_HDS

#include <list>

class ISrsRequest;
class SrsMediaPacket;
class SrsHdsFragment;
class SrsLiveSource;

// The HDS interface.
class ISrsHds
{
public:
    ISrsHds();
    virtual ~ISrsHds();

public:
    virtual srs_error_t on_publish(ISrsRequest *req) = 0;
    virtual srs_error_t on_unpublish() = 0;
    virtual srs_error_t on_video(SrsMediaPacket *msg) = 0;
    virtual srs_error_t on_audio(SrsMediaPacket *msg) = 0;
};

// Mux RTMP to Adobe HDS streaming.
class SrsHds : public ISrsHds
{
public:
    SrsHds();
    virtual ~SrsHds();

    srs_error_t on_publish(ISrsRequest *req);
    srs_error_t on_unpublish();

    srs_error_t on_video(SrsMediaPacket *msg);
    srs_error_t on_audio(SrsMediaPacket *msg);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srs_error_t flush_mainfest();
    srs_error_t flush_bootstrap();
    void adjust_windows();

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    std::list<SrsHdsFragment *> fragments_;
    SrsHdsFragment *currentSegment_;
    int fragment_index_;
    SrsMediaPacket *video_sh_;
    SrsMediaPacket *audio_sh_;

    ISrsRequest *hds_req_;
    bool hds_enabled_;
};

#endif

#endif
