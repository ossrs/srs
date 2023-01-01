//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#ifndef SRS_PROTOCOL_FORMAT_HPP
#define SRS_PROTOCOL_FORMAT_HPP

#include <srs_core.hpp>

#include <srs_kernel_codec.hpp>

class SrsOnMetaDataPacket;
class SrsSharedPtrMessage;

/**
 * Create special structure from RTMP stream, for example, the metadata.
 */
class SrsRtmpFormat : public SrsFormat
{
public:
    SrsRtmpFormat();
    virtual ~SrsRtmpFormat();
public:
    // Initialize the format from metadata, optional.
    virtual srs_error_t on_metadata(SrsOnMetaDataPacket* meta);
    // When got a parsed audio packet.
    virtual srs_error_t on_audio(SrsSharedPtrMessage* shared_audio);
    virtual srs_error_t on_audio(int64_t timestamp, char* data, int size);
    // When got a parsed video packet.
    virtual srs_error_t on_video(SrsSharedPtrMessage* shared_video);
    virtual srs_error_t on_video(int64_t timestamp, char* data, int size);
};

#endif

