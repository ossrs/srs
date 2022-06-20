//
// Copyright (c) 2013-2022 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
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
private:
    std::map<std::string, std::string> included_files;
public:
    virtual srs_error_t parse(std::string buf);
    virtual srs_error_t mock_include(const std::string file_name, const std::string content);
protected:
    virtual srs_error_t build_buffer(std::string src, srs_internal::SrsConfigBuffer** pbuffer);
};

#endif

