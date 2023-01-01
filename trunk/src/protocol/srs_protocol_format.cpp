//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#include <srs_protocol_format.hpp>

#include <srs_kernel_error.hpp>
#include <srs_kernel_codec.hpp>
#include <srs_protocol_rtmp_stack.hpp>
#include <srs_kernel_buffer.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_utility.hpp>

SrsRtmpFormat::SrsRtmpFormat()
{
}

SrsRtmpFormat::~SrsRtmpFormat()
{
}

srs_error_t SrsRtmpFormat::on_metadata(SrsOnMetaDataPacket* meta)
{
    // TODO: FIXME: Try to initialize format from metadata.
    return srs_success;
}

srs_error_t SrsRtmpFormat::on_audio(SrsSharedPtrMessage* shared_audio)
{
    SrsSharedPtrMessage* msg = shared_audio;
    char* data = msg->payload;
    int size = msg->size;
    
    return SrsFormat::on_audio(msg->timestamp, data, size);
}

srs_error_t SrsRtmpFormat::on_audio(int64_t timestamp, char* data, int size)
{
    return SrsFormat::on_audio(timestamp, data, size);
}

srs_error_t SrsRtmpFormat::on_video(SrsSharedPtrMessage* shared_video)
{
    SrsSharedPtrMessage* msg = shared_video;
    char* data = msg->payload;
    int size = msg->size;
    
    return SrsFormat::on_video(msg->timestamp, data, size);
}

srs_error_t SrsRtmpFormat::on_video(int64_t timestamp, char* data, int size)
{
    return SrsFormat::on_video(timestamp, data, size);
}

