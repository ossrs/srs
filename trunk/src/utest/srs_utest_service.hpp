//
// Copyright (c) 2013-2021 Winlin
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_UTEST_SERVICE_HPP
#define SRS_UTEST_SERVICE_HPP

/*
#include <srs_utest_service.hpp>
*/
#include <srs_utest.hpp>

#include <srs_app_st.hpp>
#include <srs_service_conn.hpp>

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

