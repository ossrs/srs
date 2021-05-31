//
// Copyright (c) 2013-2021 Winlin
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_UTEST_CONFIG_HPP
#define SRS_UTEST_CONFIG_HPP

/*
#include <srs_utest_config.hpp>
*/
#include <srs_utest.hpp>

#include <string>

#include <srs_app_config.hpp>

#define _MIN_OK_CONF "listen 1935; "

class MockSrsConfigBuffer : public srs_internal::SrsConfigBuffer
{
public:
    MockSrsConfigBuffer(std::string buf);
    virtual ~MockSrsConfigBuffer();
public:
    virtual srs_error_t fullfill(const char* filename);
};

class MockSrsConfig : public SrsConfig
{
public:
    MockSrsConfig();
    virtual ~MockSrsConfig();
public:
    virtual srs_error_t parse(std::string buf);
};

#endif

