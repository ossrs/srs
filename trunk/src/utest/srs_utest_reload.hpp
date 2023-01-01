//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#ifndef SRS_UTEST_RELOAD_HPP
#define SRS_UTEST_RELOAD_HPP

/*
#include <srs_utest_reload.hpp>
*/
#include <srs_utest.hpp>

#include <srs_utest_config.hpp>
#include <srs_app_reload.hpp>

class MockReloadHandler : public ISrsReloadHandler
{
public:
    bool listen_reloaded;
    bool pithy_print_reloaded;
    bool vhost_added_reloaded;
    bool vhost_removed_reloaded;
    bool vhost_play_reloaded;
    bool vhost_forward_reloaded;
    bool vhost_hls_reloaded;
    bool vhost_dvr_reloaded;
    bool vhost_transcode_reloaded;
    bool ingest_removed_reloaded;
    bool ingest_added_reloaded;
    bool ingest_updated_reloaded;
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
    virtual srs_error_t on_reload_listen();
    virtual srs_error_t on_reload_pithy_print();
    virtual srs_error_t on_reload_vhost_added(std::string vhost);
    virtual srs_error_t on_reload_vhost_removed(std::string vhost);
    virtual srs_error_t on_reload_vhost_play(std::string vhost);
    virtual srs_error_t on_reload_vhost_forward(std::string vhost);
    virtual srs_error_t on_reload_vhost_hls(std::string vhost);
    virtual srs_error_t on_reload_vhost_hds(std::string vhost);
    virtual srs_error_t on_reload_vhost_dvr(std::string vhost);
    virtual srs_error_t on_reload_vhost_transcode(std::string vhost);
    virtual srs_error_t on_reload_ingest_removed(std::string vhost, std::string ingest_id);
    virtual srs_error_t on_reload_ingest_added(std::string vhost, std::string ingest_id);
    virtual srs_error_t on_reload_ingest_updated(std::string vhost, std::string ingest_id);
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

