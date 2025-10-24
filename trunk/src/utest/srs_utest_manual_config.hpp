//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_UTEST_CONFIG_HPP
#define SRS_UTEST_CONFIG_HPP

/*
#include <srs_utest_manual_config.hpp>
*/
#include <srs_utest.hpp>

#include <string>

#include <srs_app_config.hpp>

#define _MIN_OK_CONF "rtmp{listen 1935;} "

class MockSrsConfigBuffer : public srs_internal::SrsConfigBuffer
{
public:
    MockSrsConfigBuffer(std::string buf);
    virtual ~MockSrsConfigBuffer();

public:
    virtual srs_error_t fullfill(const char *filename);
};

class MockSrsConfig : public SrsConfig
{
public:
    MockSrsConfig();
    virtual ~MockSrsConfig();

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    std::map<std::string, std::string> included_files;

public:
    virtual srs_error_t mock_parse(std::string buf);
    virtual srs_error_t mock_include(const std::string file_name, const std::string content);

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual srs_error_t build_buffer(std::string src, srs_internal::SrsConfigBuffer **pbuffer);
};

class ISrsSetEnvConfig
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    std::string key;
    SrsConfig *conf;

public:
    ISrsSetEnvConfig(SrsConfig *c, const std::string &k, const std::string &v, bool overwrite)
    {
        conf = c;
        key = k;
        srs_setenv(k, v, overwrite);
    }
    virtual ~ISrsSetEnvConfig()
    {
        srs_unsetenv(key);
        srs_freep(conf->env_cache_);
        conf->env_cache_ = new SrsConfDirective();
    }

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // Adds, changes environment variables, which may starts with $.
    int
    srs_setenv(const std::string &key, const std::string &value, bool overwrite);
    // Deletes environment variables, which may starts with $.
    int srs_unsetenv(const std::string &key);
};

#define SrsSetEnvConfig(conf, instance, key, value) \
    ISrsSetEnvConfig _SRS_free_##instance(&conf, key, value, true)

#endif
