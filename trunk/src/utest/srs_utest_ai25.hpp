//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//
#ifndef SRS_UTEST_AI_PLAY_HOOK_REJECTION_HPP
#define SRS_UTEST_AI_PLAY_HOOK_REJECTION_HPP

#include <srs_utest.hpp>

#include <srs_kernel_file.hpp>

#include <string>

// Mock ISrsFileReaderFactory for testing SrsHlsStream, which reads the m3u8 of an existing session.
class MockFileReaderFactoryForHlsStream : public ISrsFileReaderFactory
{
public:
    std::string content_;
    int create_count_;

public:
    MockFileReaderFactoryForHlsStream(std::string content);
    virtual ~MockFileReaderFactoryForHlsStream();

public:
    virtual SrsFileReader *create_file_reader();
};

#endif
