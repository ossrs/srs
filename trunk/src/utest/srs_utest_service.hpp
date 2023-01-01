//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#ifndef SRS_UTEST_SERVICE_HPP
#define SRS_UTEST_SERVICE_HPP

/*
#include <srs_utest_service.hpp>
*/
#include <srs_utest.hpp>

#include <srs_app_st.hpp>
#include <srs_protocol_conn.hpp>

class MockSrsConnection : public ISrsConnection
{
public:
    // Whether switch the coroutine context when free the object, for special case test.
    bool do_switch;
public:
    MockSrsConnection();
    virtual ~MockSrsConnection();
// Interface ISrsConnection.
public:
    virtual const SrsContextId& get_id();
    virtual std::string desc();
    virtual std::string remote_ip();
};

#endif

