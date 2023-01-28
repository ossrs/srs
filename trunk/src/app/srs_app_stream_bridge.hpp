//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#ifndef SRS_APP_STREAM_BRIDGE_HPP
#define SRS_APP_STREAM_BRIDGE_HPP

#include <srs_core.hpp>

class SrsRequest;
class SrsSharedPtrMessage;

// A stream bridge is used to convert stream via different protocols, such as bridge for RTMP and RTC. Generally, we use
// frame as message for bridge. A frame is a audio or video frame, such as an I/B/P frame, a general frame for decoder.
// So you must assemble RTP or TS packets to a video frame if WebRTC or SRT.
class ISrsStreamBridge
{
public:
    ISrsStreamBridge();
    virtual ~ISrsStreamBridge();
public:
    virtual srs_error_t initialize(SrsRequest* r) = 0;
    virtual srs_error_t on_publish() = 0;
    virtual srs_error_t on_frame(SrsSharedPtrMessage* frame) = 0;
    virtual void on_unpublish() = 0;
};

#endif

