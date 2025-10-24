//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_utest_manual_reload.hpp>

using namespace std;

#include <srs_kernel_error.hpp>

MockReloadHandler::MockReloadHandler()
{
    reset();
}

MockReloadHandler::~MockReloadHandler()
{
}

void MockReloadHandler::reset()
{
    vhost_chunk_size_reloaded = false;
}

int MockReloadHandler::count_total()
{
    return 1;
}

int MockReloadHandler::count_true()
{
    int count_true = 0;

    if (vhost_chunk_size_reloaded)
        count_true++;

    return count_true;
}

int MockReloadHandler::count_false()
{
    int count_false = 0;

    if (!vhost_chunk_size_reloaded)
        count_false++;

    return count_false;
}

bool MockReloadHandler::all_false()
{
    return count_true() == 0;
}

bool MockReloadHandler::all_true()
{
    return count_true() == count_total();
}

srs_error_t MockReloadHandler::on_reload_vhost_chunk_size(string /*vhost*/)
{
    vhost_chunk_size_reloaded = true;
    return srs_success;
}

MockSrsReloadConfig::MockSrsReloadConfig()
{
}

MockSrsReloadConfig::~MockSrsReloadConfig()
{
}

srs_error_t MockSrsReloadConfig::do_reload(string buf)
{
    srs_error_t err = srs_success;

    MockSrsReloadConfig conf;
    if ((err = conf.mock_parse(buf)) != srs_success) {
        return srs_error_wrap(err, "parse");
    }

    if ((err = MockSrsConfig::reload_conf(&conf)) != srs_success) {
        return srs_error_wrap(err, "reload conf");
    }

    return err;
}

VOID TEST(ConfigReloadTest, ReloadVhostChunkSize)
{
    srs_error_t err = srs_success;

    MockReloadHandler handler;
    MockSrsReloadConfig conf;

    conf.subscribe(&handler);
    HELPER_EXPECT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost ossrs.net { chunk_size 60000; }"));
    HELPER_EXPECT_SUCCESS(conf.do_reload(_MIN_OK_CONF "vhost ossrs.net { chunk_size 60000; }"));
    EXPECT_TRUE(handler.all_false());
    handler.reset();

    HELPER_EXPECT_SUCCESS(conf.do_reload(_MIN_OK_CONF "vhost ossrs.net { chunk_size 65536; }"));
    EXPECT_TRUE(handler.vhost_chunk_size_reloaded);
    EXPECT_EQ(1, handler.count_true());
    handler.reset();

    HELPER_EXPECT_SUCCESS(conf.do_reload(_MIN_OK_CONF "vhost ossrs.net { chunk_size 60000; }"));
    EXPECT_EQ(1, handler.count_true());
    handler.reset();
}

VOID TEST(ConfigReloadTest, ReloadAddNewVhost)
{
    srs_error_t err = srs_success;

    MockReloadHandler handler;
    MockSrsReloadConfig conf;

    conf.subscribe(&handler);
    // Start with one vhost
    HELPER_EXPECT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost ossrs.net { chunk_size 60000; }"));

    // Add a new vhost - should not crash
    HELPER_EXPECT_SUCCESS(conf.do_reload(_MIN_OK_CONF "vhost ossrs.net { chunk_size 60000; } vhost new_vhost { chunk_size 65536; }"));
    // Handler should not be triggered for new vhost
    EXPECT_TRUE(handler.all_false());
    handler.reset();
}

VOID TEST(ConfigReloadTest, ReloadRemoveVhost)
{
    srs_error_t err = srs_success;

    MockReloadHandler handler;
    MockSrsReloadConfig conf;

    conf.subscribe(&handler);
    // Start with two vhosts
    HELPER_EXPECT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost ossrs.net { chunk_size 60000; } vhost old_vhost { chunk_size 65536; }"));

    // Remove old_vhost - should not crash
    HELPER_EXPECT_SUCCESS(conf.do_reload(_MIN_OK_CONF "vhost ossrs.net { chunk_size 60000; }"));
    // Handler should not be triggered for removed vhost
    EXPECT_TRUE(handler.all_false());
    handler.reset();
}
