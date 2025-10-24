//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_UTEST_PROTO_STACK_HPP
#define SRS_UTEST_PROTO_STACK_HPP

/*
#include <srs_utest_rtmp.hpp>
*/
#include <srs_utest.hpp>

#include <srs_protocol_rtmp_stack.hpp>
#include <srs_utest_manual_protocol.hpp>

// Mock classes for RTMP testing
class MockPacket : public SrsRtmpCommand
{
public:
    int size;

public:
    MockPacket();
    virtual ~MockPacket();

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual int get_size();
};

class MockPacket2 : public MockPacket
{
public:
    char *payload;

public:
    MockPacket2();
    virtual ~MockPacket2();
    virtual srs_error_t encode(int &size, char *&payload);
};

class MockMRHandler : public IMergeReadHandler
{
public:
    ssize_t nn;

public:
    MockMRHandler();
    virtual void on_read(ssize_t nread);
};

#endif
