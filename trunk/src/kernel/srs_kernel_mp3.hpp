//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_KERNEL_MP3_HPP
#define SRS_KERNEL_MP3_HPP

#include <srs_core.hpp>

#include <string>

class SrsBuffer;
class ISrsFileWriter;

// The interface for MP3 transmuxer.
class ISrsMp3Transmuxer
{
public:
    ISrsMp3Transmuxer();
    virtual ~ISrsMp3Transmuxer();

public:
    virtual srs_error_t initialize(ISrsFileWriter *fw) = 0;
    virtual srs_error_t write_header() = 0;
    virtual srs_error_t write_audio(int64_t timestamp, char *data, int size) = 0;
};

/**
 * Transmux RTMP packet to MP3 stream.
 */
class SrsMp3Transmuxer : public ISrsMp3Transmuxer
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsFileWriter *writer_;

public:
    SrsMp3Transmuxer();
    virtual ~SrsMp3Transmuxer();

public:
    /**
     * initialize the underlayer file stream.
     * @remark user can initialize multiple times to encode multiple mp3 files.
     * @remark, user must free the @param fw, mp3 encoder never close/free it.
     */
    virtual srs_error_t initialize(ISrsFileWriter *fw);

public:
    /**
     * write mp3 id3 v2.3 header.
     * @see mp3.id3v2.3.0.pdf, http://id3.org/id3v2.3.0
     */
    virtual srs_error_t write_header();
    /**
     * write audio/video packet.
     * @remark assert data is not NULL.
     */
    virtual srs_error_t write_audio(int64_t timestamp, char *data, int size);
};

#endif
