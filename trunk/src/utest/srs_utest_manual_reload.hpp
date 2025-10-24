//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_UTEST_RELOAD_HPP
#define SRS_UTEST_RELOAD_HPP

/*
#include <srs_utest_reload.hpp>
*/
#include <srs_utest.hpp>

#include <srs_app_reload.hpp>
#include <srs_utest_manual_config.hpp>

class MockReloadHandler : public ISrsReloadHandler
{
public:
    bool vhost_chunk_size_reloaded;

public:
    MockReloadHandler();
    virtual ~MockReloadHandler();

public:
    virtual void reset();
    virtual bool all_false();
    virtual bool all_true();
    virtual int count_total();
    virtual int count_true();
    virtual int count_false();

public:
    virtual srs_error_t on_reload_vhost_chunk_size(std::string vhost);
};

class MockSrsReloadConfig : public MockSrsConfig
{
public:
    MockSrsReloadConfig();
    virtual ~MockSrsReloadConfig();

public:
    virtual srs_error_t do_reload(std::string buf);
};

#endif
