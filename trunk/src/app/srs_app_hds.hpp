//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#ifndef SRS_APP_HDS_HPP
#define SRS_APP_HDS_HPP

#include <srs_core.hpp>

#ifdef SRS_HDS

#include <list>

class SrsRequest;
class SrsSharedPtrMessage;
class SrsHdsFragment;
class SrsLiveSource;

// Mux RTMP to Adobe HDS streaming.
class SrsHds
{
public:
    SrsHds();
    virtual ~SrsHds();
    
    srs_error_t on_publish(SrsRequest* req);
    srs_error_t on_unpublish();
    
    srs_error_t on_video(SrsSharedPtrMessage* msg);
    srs_error_t on_audio(SrsSharedPtrMessage* msg);
    
private:
    srs_error_t flush_mainfest();
    srs_error_t flush_bootstrap();
    void adjust_windows();
    
private:
    std::list<SrsHdsFragment *> fragments;
    SrsHdsFragment *currentSegment;
    int fragment_index;
    SrsSharedPtrMessage *video_sh;
    SrsSharedPtrMessage *audio_sh;
    
    SrsRequest *hds_req;
    bool hds_enabled;
};

#endif

#endif
