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

// Mux RTMP to Adobe HDS streaming.
class SrsHds
{
public:
    SrsHds();
    virtual ~SrsHds();

    srs_error_t on_publish(ISrsRequest *req);
    srs_error_t on_unpublish();

    srs_error_t on_video(SrsMediaPacket *msg);
    srs_error_t on_audio(SrsMediaPacket *msg);

private:
    srs_error_t flush_mainfest();
    srs_error_t flush_bootstrap();
    void adjust_windows();

private:
    std::list<SrsHdsFragment *> fragments;
    SrsHdsFragment *currentSegment;
    int fragment_index;
    SrsMediaPacket *video_sh;
    SrsMediaPacket *audio_sh;

    ISrsRequest *hds_req;
    bool hds_enabled;
};

#endif

#endif
