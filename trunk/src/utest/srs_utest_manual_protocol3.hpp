//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_UTEST_PROTOCOL3_HPP
#define SRS_UTEST_PROTOCOL3_HPP

/*
#include <srs_utest_manual_protocol3.hpp>
*/
#include <srs_utest_manual_protocol.hpp>

#include <srs_protocol_conn.hpp>

// Mock classes for testing protocol connections
class MockConnection : public ISrsConnection
{
public:
    std::string ip_;

public:
    MockConnection(std::string ip = "127.0.0.1");
    virtual ~MockConnection();

public:
    virtual std::string remote_ip();
    virtual std::string desc();
    virtual const SrsContextId &get_id();
};

#endif
