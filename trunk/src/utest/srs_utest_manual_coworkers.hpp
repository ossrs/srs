//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_UTEST_COWORKERS_HPP
#define SRS_UTEST_COWORKERS_HPP

#include <srs_utest.hpp>

#include <srs_protocol_rtmp_stack.hpp>

// Mock request class for testing
class MockSrsRequest : public ISrsRequest
{
public:
    MockSrsRequest(std::string vhost, std::string app, std::string stream, std::string host = "127.0.0.1", int port = 1935);
    virtual ~MockSrsRequest();
    virtual ISrsRequest *copy();
    virtual std::string get_stream_url();
    virtual void update_auth(ISrsRequest *req);
    virtual void strip();
    virtual ISrsRequest *as_http();
};

#endif
