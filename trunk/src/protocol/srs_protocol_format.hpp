//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_PROTOCOL_FORMAT_HPP
#define SRS_PROTOCOL_FORMAT_HPP

#include <srs_core.hpp>

#include <srs_kernel_codec.hpp>
#include <srs_kernel_packet.hpp>

class SrsOnMetaDataPacket;
class SrsMediaPacket;

class SrsRtmpFormat : public SrsFormat
{
public:
    SrsRtmpFormat();
    virtual ~SrsRtmpFormat();

public:
    virtual srs_error_t on_metadata(SrsOnMetaDataPacket *meta);
    virtual srs_error_t on_audio(SrsMediaPacket *shared_audio);
    virtual srs_error_t on_audio(int64_t timestamp, char *data, int size);
    virtual srs_error_t on_video(SrsMediaPacket *shared_video);
    virtual srs_error_t on_video(int64_t timestamp, char *data, int size);
};

#endif
