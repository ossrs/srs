//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#ifndef SRS_KERNEL_AAC_HPP
#define SRS_KERNEL_AAC_HPP

#include <srs_core.hpp>

#include <string>

#include <srs_kernel_codec.hpp>

class SrsBuffer;
class ISrsStreamWriter;

// Transmux the RTMP packets to AAC stream.
class SrsAacTransmuxer
{
private:
    ISrsStreamWriter* writer;
private:
    SrsAacObjectType aac_object;
    int8_t aac_sample_rate;
    int8_t aac_channels;
    bool got_sequence_header;
public:
    SrsAacTransmuxer();
    virtual ~SrsAacTransmuxer();
public:
    // Initialize the underlayer file stream.
    // @remark User can initialize multiple times to encode multiple aac files.
    // @remark User must free the fs, aac encoder never close/free it.
    virtual srs_error_t initialize(ISrsStreamWriter* fs);
public:
   // Write audio/video packet.
   // @remark The assert data should not be NULL.
    virtual srs_error_t write_audio(int64_t timestamp, char* data, int size);
};

#endif

