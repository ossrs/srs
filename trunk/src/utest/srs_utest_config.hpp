//
// Copyright (c) 2013-2023 The SRS Authors
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

class ISrsSetEnvConfig
{
private:
    std::string key;
public:
    ISrsSetEnvConfig(const std::string& k, const std::string& v, bool overwrite) {
        key = k;
        srs_setenv(k, v, overwrite);
    }
    virtual ~ISrsSetEnvConfig() {
        srs_unsetenv(key);
    }
private:
    // Adds, changes environment variables, which may starts with $.
    int srs_setenv(const std::string& key, const std::string& value, bool overwrite);
    // Deletes environment variables, which may starts with $.
    int srs_unsetenv(const std::string& key);
};

#define SrsSetEnvConfig(instance, key, value) \
    ISrsSetEnvConfig _SRS_free_##instance(key, value, true)

#endif

