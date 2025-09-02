//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_protocol_format.hpp>

#include <srs_core_autofree.hpp>
#include <srs_kernel_buffer.hpp>
#include <srs_kernel_codec.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_rtmp_stack.hpp>

SrsRtmpFormat::SrsRtmpFormat()
{
}

SrsRtmpFormat::~SrsRtmpFormat()
{
}

srs_error_t SrsRtmpFormat::on_metadata(SrsOnMetaDataPacket *meta)
{
    // TODO: FIXME: Try to initialize format from metadata.
    return srs_success;
}

srs_error_t SrsRtmpFormat::on_audio(SrsMediaPacket *shared_audio)
{
    SrsMediaPacket *msg = shared_audio;
    char *data = msg->payload();
    int size = msg->size();

    return SrsFormat::on_audio(msg->timestamp, data, size);
}

srs_error_t SrsRtmpFormat::on_audio(int64_t timestamp, char *data, int size)
{
    return SrsFormat::on_audio(timestamp, data, size);
}

srs_error_t SrsRtmpFormat::on_video(SrsMediaPacket *shared_video)
{
    SrsMediaPacket *msg = shared_video;
    char *data = msg->payload();
    int size = msg->size();

    return SrsFormat::on_video(msg->timestamp, data, size);
}

srs_error_t SrsRtmpFormat::on_video(int64_t timestamp, char *data, int size)
{
    return SrsFormat::on_video(timestamp, data, size);
}
