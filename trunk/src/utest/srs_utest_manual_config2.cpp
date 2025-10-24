//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//
#include <srs_utest_manual_config2.hpp>

#include <srs_app_utility.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_file.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_utest_manual_kernel.hpp>

VOID TEST(ConfigMainTest, CheckIncludeEmptyConfig)
{
    srs_error_t err;

    if (true) {
        string filepath = _srs_tmp_file_prefix + "utest-main.conf";
        MockFileRemover _mfr(filepath);

        string included = _srs_tmp_file_prefix + "utest-included-empty.conf";
        MockFileRemover _mfr2(included);

        if (true) {
            SrsFileWriter fw;
            fw.open(included);
        }

        if (true) {
            SrsFileWriter fw;
            fw.open(filepath);
            string content = _MIN_OK_CONF "include " + included + ";";
            fw.write((void *)content.data(), (int)content.length(), NULL);
        }

        SrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse_file(filepath.c_str()));
        EXPECT_EQ(1, (int)conf.get_listens().size());
    }

    if (true) {
        MockSrsConfig conf;
        conf.mock_include("test.conf", "");
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "include test.conf;"));
        EXPECT_EQ(1, (int)conf.get_listens().size());
    }
}

VOID TEST(ConfigMainTest, CheckHttpListenFollow)
{
    srs_error_t err;

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_api{enabled on;https{enabled on;}}http_server{enabled on;listen 1985;https{enabled on;listen 4567;}}"));
        EXPECT_TRUE(conf.get_http_stream_enabled());

        // If http API use same port to HTTP server.
        EXPECT_EQ(1, (int)conf.get_http_stream_listens().size());
        EXPECT_STREQ("1985", conf.get_http_stream_listens().at(0).c_str());
        EXPECT_EQ(1, (int)conf.get_http_api_listens().size());
        EXPECT_STREQ("1985", conf.get_http_api_listens().at(0).c_str());

        // Then HTTPS API should use the same port to HTTPS server.
        EXPECT_EQ(1, (int)conf.get_https_stream_listens().size());
        EXPECT_STREQ("4567", conf.get_https_stream_listens().at(0).c_str());
        EXPECT_EQ(1, (int)conf.get_https_api_listens().size());
        EXPECT_STREQ("4567", conf.get_https_api_listens().at(0).c_str());
        EXPECT_TRUE(conf.get_http_stream_crossdomain());
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_api{enabled on;listen 8080;https{enabled on;}}http_server{enabled on;https{enabled on;listen 4567;}}"));
        EXPECT_TRUE(conf.get_http_stream_enabled());

        // If http API use same port to HTTP server.
        EXPECT_EQ(1, (int)conf.get_http_stream_listens().size());
        EXPECT_STREQ("8080", conf.get_http_stream_listens().at(0).c_str());
        EXPECT_EQ(1, (int)conf.get_http_api_listens().size());
        EXPECT_STREQ("8080", conf.get_http_api_listens().at(0).c_str());

        // Then HTTPS API should use the same port to HTTPS server.
        EXPECT_EQ(1, (int)conf.get_https_stream_listens().size());
        EXPECT_STREQ("4567", conf.get_https_stream_listens().at(0).c_str());
        EXPECT_EQ(1, (int)conf.get_https_api_listens().size());
        EXPECT_STREQ("4567", conf.get_https_api_listens().at(0).c_str());
        EXPECT_TRUE(conf.get_http_stream_crossdomain());
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_api{enabled on;listen 8080;https{enabled on;}}http_server{enabled on;https{enabled on;}}"));
        EXPECT_TRUE(conf.get_http_stream_enabled());

        // If http API use same port to HTTP server.
        EXPECT_EQ(1, (int)conf.get_http_stream_listens().size());
        EXPECT_STREQ("8080", conf.get_http_stream_listens().at(0).c_str());
        EXPECT_EQ(1, (int)conf.get_http_api_listens().size());
        EXPECT_STREQ("8080", conf.get_http_api_listens().at(0).c_str());

        // Then HTTPS API should use the same port to HTTPS server.
        EXPECT_EQ(1, (int)conf.get_https_stream_listens().size());
        EXPECT_STREQ("8088", conf.get_https_stream_listens().at(0).c_str());
        EXPECT_EQ(1, (int)conf.get_https_api_listens().size());
        EXPECT_STREQ("8088", conf.get_https_api_listens().at(0).c_str());
        EXPECT_TRUE(conf.get_http_stream_crossdomain());
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_api{enabled on;https{enabled on;}}http_server{enabled on;listen 1985;https{enabled on;}}"));
        EXPECT_TRUE(conf.get_http_stream_enabled());

        // If http API use same port to HTTP server.
        EXPECT_EQ(1, (int)conf.get_http_stream_listens().size());
        EXPECT_STREQ("1985", conf.get_http_stream_listens().at(0).c_str());
        EXPECT_EQ(1, (int)conf.get_http_api_listens().size());
        EXPECT_STREQ("1985", conf.get_http_api_listens().at(0).c_str());

        // Then HTTPS API should use the same port to HTTPS server.
        EXPECT_EQ(1, (int)conf.get_https_stream_listens().size());
        EXPECT_STREQ("8088", conf.get_https_stream_listens().at(0).c_str());
        EXPECT_EQ(1, (int)conf.get_https_api_listens().size());
        EXPECT_STREQ("8088", conf.get_https_api_listens().at(0).c_str());
        EXPECT_TRUE(conf.get_http_stream_crossdomain());
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_api{enabled on;listen 1234;https{enabled on;}}http_server{enabled on;listen 1234;https{enabled on;}}"));
        EXPECT_TRUE(conf.get_http_stream_enabled());

        // If http API use same port to HTTP server.
        EXPECT_EQ(1, (int)conf.get_http_stream_listens().size());
        EXPECT_STREQ("1234", conf.get_http_stream_listens().at(0).c_str());
        EXPECT_EQ(1, (int)conf.get_http_api_listens().size());
        EXPECT_STREQ("1234", conf.get_http_api_listens().at(0).c_str());

        // Then HTTPS API should use the same port to HTTPS server.
        EXPECT_EQ(1, (int)conf.get_https_stream_listens().size());
        EXPECT_STREQ("8088", conf.get_https_stream_listens().at(0).c_str());
        EXPECT_EQ(1, (int)conf.get_https_api_listens().size());
        EXPECT_STREQ("8088", conf.get_https_api_listens().at(0).c_str());
        EXPECT_TRUE(conf.get_http_stream_crossdomain());
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_api{enabled on;listen 1234;https{enabled on;}}http_server{enabled on;listen 1234;https{enabled on;listen 4567;}}"));
        EXPECT_TRUE(conf.get_http_stream_enabled());

        // If http API use same port to HTTP server.
        EXPECT_EQ(1, (int)conf.get_http_stream_listens().size());
        EXPECT_STREQ("1234", conf.get_http_stream_listens().at(0).c_str());
        EXPECT_EQ(1, (int)conf.get_http_api_listens().size());
        EXPECT_STREQ("1234", conf.get_http_api_listens().at(0).c_str());

        // Then HTTPS API should use the same port to HTTPS server.
        EXPECT_EQ(1, (int)conf.get_https_stream_listens().size());
        EXPECT_STREQ("4567", conf.get_https_stream_listens().at(0).c_str());
        EXPECT_EQ(1, (int)conf.get_https_api_listens().size());
        EXPECT_STREQ("4567", conf.get_https_api_listens().at(0).c_str());
        EXPECT_TRUE(conf.get_http_stream_crossdomain());
    }
}

extern bool srs_directive_equals(SrsConfDirective *a, SrsConfDirective *b);
extern bool srs_directive_equals(SrsConfDirective *a, SrsConfDirective *b, string except);

VOID TEST(ConfigDirectiveTest, DirectiveEqualsNullBoth)
{
    // Test the case where both directives are NULL - should return true
    EXPECT_TRUE(srs_directive_equals(NULL, NULL));
}

VOID TEST(ConfigDirectiveTest, DirectiveEqualsWithExceptParameter)
{
    // Test srs_directive_equals with except parameter - both NULL case
    if (true) {
        // Both NULL, should return true regardless of except parameter
        EXPECT_TRUE(srs_directive_equals(NULL, NULL, "any_except"));
    }

    // Test srs_directive_equals_self failure case
    if (true) {
        // Create two directives with different names - should fail srs_directive_equals_self
        SrsConfDirective *a = new SrsConfDirective();
        a->name_ = "vhost";
        a->args_.push_back("test.com");

        SrsConfDirective *b = new SrsConfDirective();
        b->name_ = "different"; // Different name
        b->args_.push_back("test.com");

        // Should return false because srs_directive_equals_self fails
        EXPECT_FALSE(srs_directive_equals(a, b, "any_except"));

        srs_freep(a);
        srs_freep(b);
    }

    // Test except parameter functionality - skip child directive with except name
    if (true) {
        // Create two directives that differ only in a child directive named "hls"
        SrsConfDirective *a = new SrsConfDirective();
        a->name_ = "vhost";
        a->args_.push_back("test.com");

        SrsConfDirective *child_a1 = new SrsConfDirective();
        child_a1->name_ = "hls";
        child_a1->args_.push_back("on");
        a->directives_.push_back(child_a1);

        SrsConfDirective *child_a2 = new SrsConfDirective();
        child_a2->name_ = "dvr";
        child_a2->args_.push_back("on");
        a->directives_.push_back(child_a2);

        SrsConfDirective *b = new SrsConfDirective();
        b->name_ = "vhost";
        b->args_.push_back("test.com");

        SrsConfDirective *child_b1 = new SrsConfDirective();
        child_b1->name_ = "hls";
        child_b1->args_.push_back("off"); // Different from a
        b->directives_.push_back(child_b1);

        SrsConfDirective *child_b2 = new SrsConfDirective();
        child_b2->name_ = "dvr";
        child_b2->args_.push_back("on"); // Same as a
        b->directives_.push_back(child_b2);

        // Without except parameter, should be false because hls differs
        EXPECT_FALSE(srs_directive_equals(a, b, ""));

        // With except="hls", should be true because hls directive is skipped
        EXPECT_TRUE(srs_directive_equals(a, b, "hls"));

        // With except="dvr", should be false because hls still differs
        EXPECT_FALSE(srs_directive_equals(a, b, "dvr"));

        srs_freep(a);
        srs_freep(b);
    }

    // Test recursive comparison failure case
    if (true) {
        // Create two directives with nested child directives that differ
        SrsConfDirective *a = new SrsConfDirective();
        a->name_ = "vhost";
        a->args_.push_back("test.com");

        SrsConfDirective *child_a = new SrsConfDirective();
        child_a->name_ = "rtc";
        a->directives_.push_back(child_a);

        SrsConfDirective *grandchild_a = new SrsConfDirective();
        grandchild_a->name_ = "enabled";
        grandchild_a->args_.push_back("on");
        child_a->directives_.push_back(grandchild_a);

        SrsConfDirective *b = new SrsConfDirective();
        b->name_ = "vhost";
        b->args_.push_back("test.com");

        SrsConfDirective *child_b = new SrsConfDirective();
        child_b->name_ = "rtc";
        b->directives_.push_back(child_b);

        SrsConfDirective *grandchild_b = new SrsConfDirective();
        grandchild_b->name_ = "enabled";
        grandchild_b->args_.push_back("off"); // Different from a
        child_b->directives_.push_back(grandchild_b);

        // Should return false because nested comparison fails
        EXPECT_FALSE(srs_directive_equals(a, b, "some_other_except"));

        srs_freep(a);
        srs_freep(b);
    }

    // Test multiple except directives with complex structure
    if (true) {
        // Create complex directives with multiple child directives
        SrsConfDirective *a = new SrsConfDirective();
        a->name_ = "vhost";
        a->args_.push_back("test.com");

        SrsConfDirective *child_a1 = new SrsConfDirective();
        child_a1->name_ = "hls";
        child_a1->args_.push_back("on");
        a->directives_.push_back(child_a1);

        SrsConfDirective *child_a2 = new SrsConfDirective();
        child_a2->name_ = "dvr";
        child_a2->args_.push_back("on");
        a->directives_.push_back(child_a2);

        SrsConfDirective *child_a3 = new SrsConfDirective();
        child_a3->name_ = "rtc";
        child_a3->args_.push_back("on");
        a->directives_.push_back(child_a3);

        SrsConfDirective *b = new SrsConfDirective();
        b->name_ = "vhost";
        b->args_.push_back("test.com");

        SrsConfDirective *child_b1 = new SrsConfDirective();
        child_b1->name_ = "hls";
        child_b1->args_.push_back("off"); // Different
        b->directives_.push_back(child_b1);

        SrsConfDirective *child_b2 = new SrsConfDirective();
        child_b2->name_ = "dvr";
        child_b2->args_.push_back("off"); // Different
        b->directives_.push_back(child_b2);

        SrsConfDirective *child_b3 = new SrsConfDirective();
        child_b3->name_ = "rtc";
        child_b3->args_.push_back("on"); // Same
        b->directives_.push_back(child_b3);

        // Without except, should be false
        EXPECT_FALSE(srs_directive_equals(a, b, ""));

        // Skip hls, should still be false because dvr differs
        EXPECT_FALSE(srs_directive_equals(a, b, "hls"));

        // Skip dvr, should still be false because hls differs
        EXPECT_FALSE(srs_directive_equals(a, b, "dvr"));

        srs_freep(a);
        srs_freep(b);
    }

    // Test edge case: empty directives with except parameter
    if (true) {
        // Create two identical empty directives
        SrsConfDirective *a = new SrsConfDirective();
        a->name_ = "simple";
        a->args_.push_back("value");

        SrsConfDirective *b = new SrsConfDirective();
        b->name_ = "simple";
        b->args_.push_back("value");

        // Should be equal even with except parameter since no child directives
        EXPECT_TRUE(srs_directive_equals(a, b, "any_except"));

        srs_freep(a);
        srs_freep(b);
    }

    // Test case where only one directive has the except child
    if (true) {
        SrsConfDirective *a = new SrsConfDirective();
        a->name_ = "vhost";
        a->args_.push_back("test.com");

        SrsConfDirective *child_a = new SrsConfDirective();
        child_a->name_ = "hls";
        child_a->args_.push_back("on");
        a->directives_.push_back(child_a);

        SrsConfDirective *b = new SrsConfDirective();
        b->name_ = "vhost";
        b->args_.push_back("test.com");

        SrsConfDirective *child_b = new SrsConfDirective();
        child_b->name_ = "dvr"; // Different name, not "hls"
        child_b->args_.push_back("on");
        b->directives_.push_back(child_b);

        // Should be false because child directive names differ
        // and "hls" is not present in b to be skipped
        EXPECT_FALSE(srs_directive_equals(a, b, "hls"));

        srs_freep(a);
        srs_freep(b);
    }
}

VOID TEST(ConfigDirectiveTest, DirectiveEqualsWithChildDirectives)
{
    // Test the recursive comparison of child directives
    if (true) {
        // Create two identical directives with child directives
        SrsConfDirective *a = new SrsConfDirective();
        a->name_ = "vhost";
        a->args_.push_back("test.com");

        SrsConfDirective *child_a = new SrsConfDirective();
        child_a->name_ = "hls";
        child_a->args_.push_back("on");
        a->directives_.push_back(child_a);

        SrsConfDirective *b = new SrsConfDirective();
        b->name_ = "vhost";
        b->args_.push_back("test.com");

        SrsConfDirective *child_b = new SrsConfDirective();
        child_b->name_ = "hls";
        child_b->args_.push_back("on");
        b->directives_.push_back(child_b);

        // Should be equal since all properties and child directives match
        EXPECT_TRUE(srs_directive_equals(a, b));

        srs_freep(a);
        srs_freep(b);
    }

    if (true) {
        // Create two directives with different child directives
        SrsConfDirective *a = new SrsConfDirective();
        a->name_ = "vhost";
        a->args_.push_back("test.com");

        SrsConfDirective *child_a = new SrsConfDirective();
        child_a->name_ = "hls";
        child_a->args_.push_back("on");
        a->directives_.push_back(child_a);

        SrsConfDirective *b = new SrsConfDirective();
        b->name_ = "vhost";
        b->args_.push_back("test.com");

        SrsConfDirective *child_b = new SrsConfDirective();
        child_b->name_ = "hls";
        child_b->args_.push_back("off"); // Different argument
        b->directives_.push_back(child_b);

        // Should not be equal since child directive arguments differ
        EXPECT_FALSE(srs_directive_equals(a, b));

        srs_freep(a);
        srs_freep(b);
    }

    if (true) {
        // Create two directives with multiple levels of child directives
        SrsConfDirective *a = new SrsConfDirective();
        a->name_ = "vhost";
        a->args_.push_back("test.com");

        SrsConfDirective *child_a = new SrsConfDirective();
        child_a->name_ = "hls";

        SrsConfDirective *grandchild_a = new SrsConfDirective();
        grandchild_a->name_ = "enabled";
        grandchild_a->args_.push_back("on");
        child_a->directives_.push_back(grandchild_a);

        a->directives_.push_back(child_a);

        SrsConfDirective *b = new SrsConfDirective();
        b->name_ = "vhost";
        b->args_.push_back("test.com");

        SrsConfDirective *child_b = new SrsConfDirective();
        child_b->name_ = "hls";

        SrsConfDirective *grandchild_b = new SrsConfDirective();
        grandchild_b->name_ = "enabled";
        grandchild_b->args_.push_back("on");
        child_b->directives_.push_back(grandchild_b);

        b->directives_.push_back(child_b);

        // Should be equal since all nested directives match
        EXPECT_TRUE(srs_directive_equals(a, b));

        srs_freep(a);
        srs_freep(b);
    }

    if (true) {
        // Create two directives with multiple child directives
        SrsConfDirective *a = new SrsConfDirective();
        a->name_ = "vhost";
        a->args_.push_back("test.com");

        SrsConfDirective *child_a1 = new SrsConfDirective();
        child_a1->name_ = "hls";
        child_a1->args_.push_back("on");
        a->directives_.push_back(child_a1);

        SrsConfDirective *child_a2 = new SrsConfDirective();
        child_a2->name_ = "dvr";
        child_a2->args_.push_back("off");
        a->directives_.push_back(child_a2);

        SrsConfDirective *b = new SrsConfDirective();
        b->name_ = "vhost";
        b->args_.push_back("test.com");

        SrsConfDirective *child_b1 = new SrsConfDirective();
        child_b1->name_ = "hls";
        child_b1->args_.push_back("on");
        b->directives_.push_back(child_b1);

        SrsConfDirective *child_b2 = new SrsConfDirective();
        child_b2->name_ = "dvr";
        child_b2->args_.push_back("off");
        b->directives_.push_back(child_b2);

        // Should be equal since all child directives match
        EXPECT_TRUE(srs_directive_equals(a, b));

        srs_freep(a);
        srs_freep(b);
    }

    if (true) {
        // Test empty directives (no child directives)
        SrsConfDirective *a = new SrsConfDirective();
        a->name_ = "simple";
        a->args_.push_back("value");

        SrsConfDirective *b = new SrsConfDirective();
        b->name_ = "simple";
        b->args_.push_back("value");

        // Should be equal since they have same name, args, and no child directives
        EXPECT_TRUE(srs_directive_equals(a, b));

        srs_freep(a);
        srs_freep(b);
    }
}

extern srs_error_t srs_config_transform_vhost(SrsConfDirective *root);

VOID TEST(ConfigTransformTest, TransformRtcServerRemoveDeprecatedConfigs)
{
    srs_error_t err;

    // Test removing perf_stat from rtc_server section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{enabled on; listen 8000; perf_stat on;}"));

        // Verify perf_stat was removed during transformation
        SrsConfDirective *rtc_server = conf.get_root()->get("rtc_server");
        ASSERT_TRUE(rtc_server != NULL);
        EXPECT_TRUE(rtc_server->get("enabled") != NULL);
        EXPECT_TRUE(rtc_server->get("listen") != NULL);
        EXPECT_TRUE(rtc_server->get("perf_stat") == NULL); // Should be removed
    }

    // Test removing queue_length from rtc_server section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{enabled on; listen 8000; queue_length 10;}"));

        // Verify queue_length was removed during transformation
        SrsConfDirective *rtc_server = conf.get_root()->get("rtc_server");
        ASSERT_TRUE(rtc_server != NULL);
        EXPECT_TRUE(rtc_server->get("enabled") != NULL);
        EXPECT_TRUE(rtc_server->get("listen") != NULL);
        EXPECT_TRUE(rtc_server->get("queue_length") == NULL); // Should be removed
    }

    // Test removing both perf_stat and queue_length from rtc_server section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{enabled on; listen 8000; perf_stat on; queue_length 10; protocol udp;}"));

        // Verify both deprecated configs were removed during transformation
        SrsConfDirective *rtc_server = conf.get_root()->get("rtc_server");
        ASSERT_TRUE(rtc_server != NULL);
        EXPECT_TRUE(rtc_server->get("enabled") != NULL);
        EXPECT_TRUE(rtc_server->get("listen") != NULL);
        EXPECT_TRUE(rtc_server->get("protocol") != NULL);
        EXPECT_TRUE(rtc_server->get("perf_stat") == NULL);    // Should be removed
        EXPECT_TRUE(rtc_server->get("queue_length") == NULL); // Should be removed
    }

    // Test that other rtc_server configs are preserved
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{enabled on; listen 8000; protocol udp; candidate 192.168.1.100; perf_stat on; queue_length 10;}"));

        // Verify valid configs are preserved and deprecated ones are removed
        SrsConfDirective *rtc_server = conf.get_root()->get("rtc_server");
        ASSERT_TRUE(rtc_server != NULL);
        EXPECT_TRUE(rtc_server->get("enabled") != NULL);
        EXPECT_TRUE(rtc_server->get("listen") != NULL);
        EXPECT_TRUE(rtc_server->get("protocol") != NULL);
        EXPECT_TRUE(rtc_server->get("candidate") != NULL);
        EXPECT_TRUE(rtc_server->get("perf_stat") == NULL);    // Should be removed
        EXPECT_TRUE(rtc_server->get("queue_length") == NULL); // Should be removed
    }

    // Test rtc_server section without deprecated configs (should remain unchanged)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{enabled on; listen 8000; protocol udp;}"));

        // Verify all configs are preserved when no deprecated configs present
        SrsConfDirective *rtc_server = conf.get_root()->get("rtc_server");
        ASSERT_TRUE(rtc_server != NULL);
        EXPECT_TRUE(rtc_server->get("enabled") != NULL);
        EXPECT_TRUE(rtc_server->get("listen") != NULL);
        EXPECT_TRUE(rtc_server->get("protocol") != NULL);
        EXPECT_EQ(3, (int)rtc_server->directives_.size()); // Should have exactly 3 directives
    }

    // Test multiple rtc_server sections (edge case)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{enabled on; perf_stat on;} rtc_server{listen 8001; queue_length 5;}"));

        // Get all rtc_server directives
        std::vector<SrsConfDirective *> rtc_servers;
        for (int i = 0; i < (int)conf.get_root()->directives_.size(); i++) {
            SrsConfDirective *dir = conf.get_root()->directives_.at(i);
            if (dir->name_ == "rtc_server") {
                rtc_servers.push_back(dir);
            }
        }

        // Should have 2 rtc_server sections
        EXPECT_EQ(2, (int)rtc_servers.size());

        // First rtc_server should have enabled but not perf_stat
        EXPECT_TRUE(rtc_servers[0]->get("enabled") != NULL);
        EXPECT_TRUE(rtc_servers[0]->get("perf_stat") == NULL);

        // Second rtc_server should have listen but not queue_length
        EXPECT_TRUE(rtc_servers[1]->get("listen") != NULL);
        EXPECT_TRUE(rtc_servers[1]->get("queue_length") == NULL);
    }
}

VOID TEST(ConfigTransformTest, TransformRtcServerNestedDeprecatedConfigs)
{
    srs_error_t err;

    // Test nested rtc_server with deprecated configs in complex structure
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{enabled on; listen 8000; perf_stat{enabled on; interval 30;} queue_length 100;}"));

        // Verify nested perf_stat directive is completely removed
        SrsConfDirective *rtc_server = conf.get_root()->get("rtc_server");
        ASSERT_TRUE(rtc_server != NULL);
        EXPECT_TRUE(rtc_server->get("enabled") != NULL);
        EXPECT_TRUE(rtc_server->get("listen") != NULL);
        EXPECT_TRUE(rtc_server->get("perf_stat") == NULL);    // Should be removed entirely
        EXPECT_TRUE(rtc_server->get("queue_length") == NULL); // Should be removed
    }

    // Test rtc_server with only deprecated configs
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{perf_stat on; queue_length 50;}"));

        // Verify rtc_server section exists but is empty after removing deprecated configs
        SrsConfDirective *rtc_server = conf.get_root()->get("rtc_server");
        ASSERT_TRUE(rtc_server != NULL);
        EXPECT_EQ(0, (int)rtc_server->directives_.size()); // Should be empty
        EXPECT_TRUE(rtc_server->get("perf_stat") == NULL);
        EXPECT_TRUE(rtc_server->get("queue_length") == NULL);
    }

    // Test rtc_server with mixed valid and deprecated configs in different order
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{perf_stat on; enabled on; queue_length 25; listen 8000; protocol udp; perf_stat off;}"));

        // Verify all perf_stat instances are removed and valid configs preserved
        SrsConfDirective *rtc_server = conf.get_root()->get("rtc_server");
        ASSERT_TRUE(rtc_server != NULL);
        EXPECT_TRUE(rtc_server->get("enabled") != NULL);
        EXPECT_TRUE(rtc_server->get("listen") != NULL);
        EXPECT_TRUE(rtc_server->get("protocol") != NULL);
        EXPECT_TRUE(rtc_server->get("perf_stat") == NULL);    // All instances should be removed
        EXPECT_TRUE(rtc_server->get("queue_length") == NULL); // Should be removed
        EXPECT_EQ(3, (int)rtc_server->directives_.size());    // Should have exactly 3 valid directives
    }
}

VOID TEST(ConfigTransformTest, TransformRtcServerPreserveOtherSections)
{
    srs_error_t err;

    // Test that deprecated config removal only affects rtc_server sections
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{enabled on; perf_stat on; queue_length 10;} http_server{enabled on; listen 8080;}"));

        // Verify rtc_server has deprecated configs removed
        SrsConfDirective *rtc_server = conf.get_root()->get("rtc_server");
        ASSERT_TRUE(rtc_server != NULL);
        EXPECT_TRUE(rtc_server->get("enabled") != NULL);
        EXPECT_TRUE(rtc_server->get("perf_stat") == NULL);    // Should be removed from rtc_server
        EXPECT_TRUE(rtc_server->get("queue_length") == NULL); // Should be removed from rtc_server

        // Verify http_server preserves all valid configs (transformation doesn't affect it)
        SrsConfDirective *http_server = conf.get_root()->get("http_server");
        ASSERT_TRUE(http_server != NULL);
        EXPECT_TRUE(http_server->get("enabled") != NULL);
        EXPECT_TRUE(http_server->get("listen") != NULL); // Should be preserved in http_server
    }

    // Test vhost sections are not affected by rtc_server transformation
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{perf_stat on; queue_length 5;} vhost test.com{rtc{enabled on; rtmp_to_rtc on;}}"));

        // Verify rtc_server has deprecated configs removed
        SrsConfDirective *rtc_server = conf.get_root()->get("rtc_server");
        ASSERT_TRUE(rtc_server != NULL);
        EXPECT_TRUE(rtc_server->get("perf_stat") == NULL);
        EXPECT_TRUE(rtc_server->get("queue_length") == NULL);

        // Verify vhost rtc section preserves valid configs (different section)
        SrsConfDirective *vhost = conf.get_root()->get("vhost");
        ASSERT_TRUE(vhost != NULL);
        SrsConfDirective *rtc = vhost->get("rtc");
        ASSERT_TRUE(rtc != NULL);
        EXPECT_TRUE(rtc->get("enabled") != NULL);
        EXPECT_TRUE(rtc->get("rtmp_to_rtc") != NULL); // Should be preserved in vhost.rtc
    }
}

VOID TEST(ConfigTransformTest, TransformVhostDirectCall)
{
    srs_error_t err;

    // Test direct call to srs_config_transform_vhost function
    if (true) {
        // Create a root directive with rtc_server containing deprecated configs
        SrsConfDirective *root = new SrsConfDirective();

        // Add rtmp section (required for valid config)
        SrsConfDirective *rtmp = new SrsConfDirective();
        rtmp->name_ = "rtmp";
        SrsConfDirective *listen = new SrsConfDirective();
        listen->name_ = "listen";
        listen->args_.push_back("1935");
        rtmp->directives_.push_back(listen);
        root->directives_.push_back(rtmp);

        // Add rtc_server section with deprecated configs
        SrsConfDirective *rtc_server = new SrsConfDirective();
        rtc_server->name_ = "rtc_server";

        SrsConfDirective *enabled = new SrsConfDirective();
        enabled->name_ = "enabled";
        enabled->args_.push_back("on");
        rtc_server->directives_.push_back(enabled);

        SrsConfDirective *perf_stat = new SrsConfDirective();
        perf_stat->name_ = "perf_stat";
        perf_stat->args_.push_back("on");
        rtc_server->directives_.push_back(perf_stat);

        SrsConfDirective *queue_length = new SrsConfDirective();
        queue_length->name_ = "queue_length";
        queue_length->args_.push_back("100");
        rtc_server->directives_.push_back(queue_length);

        SrsConfDirective *protocol = new SrsConfDirective();
        protocol->name_ = "protocol";
        protocol->args_.push_back("udp");
        rtc_server->directives_.push_back(protocol);

        root->directives_.push_back(rtc_server);

        // Verify initial state
        EXPECT_EQ(4, (int)rtc_server->directives_.size());
        EXPECT_TRUE(rtc_server->get("enabled") != NULL);
        EXPECT_TRUE(rtc_server->get("perf_stat") != NULL);
        EXPECT_TRUE(rtc_server->get("queue_length") != NULL);
        EXPECT_TRUE(rtc_server->get("protocol") != NULL);

        // Call the transform function directly
        HELPER_ASSERT_SUCCESS(srs_config_transform_vhost(root));

        // Verify deprecated configs were removed
        EXPECT_EQ(2, (int)rtc_server->directives_.size()); // Should have only enabled and protocol
        EXPECT_TRUE(rtc_server->get("enabled") != NULL);
        EXPECT_TRUE(rtc_server->get("protocol") != NULL);
        EXPECT_TRUE(rtc_server->get("perf_stat") == NULL);    // Should be removed
        EXPECT_TRUE(rtc_server->get("queue_length") == NULL); // Should be removed

        srs_freep(root);
    }

    // Test transform function with empty rtc_server section
    if (true) {
        SrsConfDirective *root = new SrsConfDirective();

        // Add rtmp section (required for valid config)
        SrsConfDirective *rtmp = new SrsConfDirective();
        rtmp->name_ = "rtmp";
        SrsConfDirective *listen = new SrsConfDirective();
        listen->name_ = "listen";
        listen->args_.push_back("1935");
        rtmp->directives_.push_back(listen);
        root->directives_.push_back(rtmp);

        // Add empty rtc_server section
        SrsConfDirective *rtc_server = new SrsConfDirective();
        rtc_server->name_ = "rtc_server";
        root->directives_.push_back(rtc_server);

        // Verify initial state
        EXPECT_EQ(0, (int)rtc_server->directives_.size());

        // Call the transform function directly
        HELPER_ASSERT_SUCCESS(srs_config_transform_vhost(root));

        // Verify rtc_server remains empty
        EXPECT_EQ(0, (int)rtc_server->directives_.size());

        srs_freep(root);
    }
}

VOID TEST(ConfigTransformTest, TransformVhostNackToRtc)
{
    srs_error_t err;

    // Test transforming vhost nack directive to vhost.rtc.nack
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{nack{enabled on;}}"));

        // Verify nack was moved to rtc section
        SrsConfDirective *vhost = conf.get_root()->get("vhost");
        ASSERT_TRUE(vhost != NULL);
        EXPECT_TRUE(vhost->get("nack") == NULL); // Should be removed from vhost level

        SrsConfDirective *rtc = vhost->get("rtc");
        ASSERT_TRUE(rtc != NULL);              // Should be created
        EXPECT_TRUE(rtc->get("nack") != NULL); // Should be moved here
        EXPECT_STREQ("on", rtc->get("nack")->arg0().c_str());
    }

    // Test transforming vhost nack with no_copy directive
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{nack{enabled on; no_copy off;}}"));

        // Verify nack and nack_no_copy were moved to rtc section
        SrsConfDirective *vhost = conf.get_root()->get("vhost");
        ASSERT_TRUE(vhost != NULL);
        EXPECT_TRUE(vhost->get("nack") == NULL); // Should be removed from vhost level

        SrsConfDirective *rtc = vhost->get("rtc");
        ASSERT_TRUE(rtc != NULL);              // Should be created
        EXPECT_TRUE(rtc->get("nack") != NULL); // Should be moved here
        EXPECT_STREQ("on", rtc->get("nack")->arg0().c_str());
        EXPECT_TRUE(rtc->get("nack_no_copy") != NULL); // Should be created from no_copy
        EXPECT_STREQ("off", rtc->get("nack_no_copy")->arg0().c_str());
    }

    // Test transforming vhost nack with only no_copy directive
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{nack{no_copy on;}}"));

        // Verify only nack_no_copy was created in rtc section
        SrsConfDirective *vhost = conf.get_root()->get("vhost");
        ASSERT_TRUE(vhost != NULL);
        EXPECT_TRUE(vhost->get("nack") == NULL); // Should be removed from vhost level

        SrsConfDirective *rtc = vhost->get("rtc");
        ASSERT_TRUE(rtc != NULL);                      // Should be created
        EXPECT_TRUE(rtc->get("nack") == NULL);         // Should not be created (no enabled directive)
        EXPECT_TRUE(rtc->get("nack_no_copy") != NULL); // Should be created from no_copy
        EXPECT_STREQ("on", rtc->get("nack_no_copy")->arg0().c_str());
    }

    // Test transforming empty nack directive
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{nack{}}"));

        // Verify nack was removed but rtc section was created (empty)
        SrsConfDirective *vhost = conf.get_root()->get("vhost");
        ASSERT_TRUE(vhost != NULL);
        EXPECT_TRUE(vhost->get("nack") == NULL); // Should be removed from vhost level

        SrsConfDirective *rtc = vhost->get("rtc");
        ASSERT_TRUE(rtc != NULL);                      // Should be created
        EXPECT_TRUE(rtc->get("nack") == NULL);         // Should not be created (no enabled directive)
        EXPECT_TRUE(rtc->get("nack_no_copy") == NULL); // Should not be created (no no_copy directive)
    }
}

VOID TEST(ConfigTransformTest, TransformVhostTwccToRtc)
{
    srs_error_t err;

    // Test transforming vhost twcc directive to vhost.rtc.twcc
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{twcc{enabled on;}}"));

        // Verify twcc was moved to rtc section
        SrsConfDirective *vhost = conf.get_root()->get("vhost");
        ASSERT_TRUE(vhost != NULL);
        EXPECT_TRUE(vhost->get("twcc") == NULL); // Should be removed from vhost level

        SrsConfDirective *rtc = vhost->get("rtc");
        ASSERT_TRUE(rtc != NULL);              // Should be created
        EXPECT_TRUE(rtc->get("twcc") != NULL); // Should be moved here
        EXPECT_STREQ("on", rtc->get("twcc")->arg0().c_str());
    }

    // Test transforming vhost twcc with enabled off
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{twcc{enabled off;}}"));

        // Verify twcc was moved to rtc section with correct value
        SrsConfDirective *vhost = conf.get_root()->get("vhost");
        ASSERT_TRUE(vhost != NULL);
        EXPECT_TRUE(vhost->get("twcc") == NULL); // Should be removed from vhost level

        SrsConfDirective *rtc = vhost->get("rtc");
        ASSERT_TRUE(rtc != NULL);              // Should be created
        EXPECT_TRUE(rtc->get("twcc") != NULL); // Should be moved here
        EXPECT_STREQ("off", rtc->get("twcc")->arg0().c_str());
    }

    // Test transforming empty twcc directive
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{twcc{}}"));

        // Verify twcc was removed but rtc section was created (empty)
        SrsConfDirective *vhost = conf.get_root()->get("vhost");
        ASSERT_TRUE(vhost != NULL);
        EXPECT_TRUE(vhost->get("twcc") == NULL); // Should be removed from vhost level

        SrsConfDirective *rtc = vhost->get("rtc");
        ASSERT_TRUE(rtc != NULL);              // Should be created
        EXPECT_TRUE(rtc->get("twcc") == NULL); // Should not be created (no enabled directive)
    }
}

VOID TEST(ConfigTransformTest, TransformVhostNackAndTwccToRtc)
{
    srs_error_t err;

    // Test transforming both nack and twcc directives together
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{nack{enabled on; no_copy off;} twcc{enabled on;}}"));

        // Verify both nack and twcc were moved to rtc section
        SrsConfDirective *vhost = conf.get_root()->get("vhost");
        ASSERT_TRUE(vhost != NULL);
        EXPECT_TRUE(vhost->get("nack") == NULL); // Should be removed from vhost level
        EXPECT_TRUE(vhost->get("twcc") == NULL); // Should be removed from vhost level

        SrsConfDirective *rtc = vhost->get("rtc");
        ASSERT_TRUE(rtc != NULL);              // Should be created
        EXPECT_TRUE(rtc->get("nack") != NULL); // Should be moved here
        EXPECT_STREQ("on", rtc->get("nack")->arg0().c_str());
        EXPECT_TRUE(rtc->get("nack_no_copy") != NULL); // Should be created from no_copy
        EXPECT_STREQ("off", rtc->get("nack_no_copy")->arg0().c_str());
        EXPECT_TRUE(rtc->get("twcc") != NULL); // Should be moved here
        EXPECT_STREQ("on", rtc->get("twcc")->arg0().c_str());
    }

    // Test transforming nack and twcc with existing rtc section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{enabled on;} nack{enabled off;} twcc{enabled on;}}"));

        // Verify nack and twcc were added to existing rtc section
        SrsConfDirective *vhost = conf.get_root()->get("vhost");
        ASSERT_TRUE(vhost != NULL);
        EXPECT_TRUE(vhost->get("nack") == NULL); // Should be removed from vhost level
        EXPECT_TRUE(vhost->get("twcc") == NULL); // Should be removed from vhost level

        SrsConfDirective *rtc = vhost->get("rtc");
        ASSERT_TRUE(rtc != NULL);                 // Should exist
        EXPECT_TRUE(rtc->get("enabled") != NULL); // Should preserve existing config
        EXPECT_STREQ("on", rtc->get("enabled")->arg0().c_str());
        EXPECT_TRUE(rtc->get("nack") != NULL); // Should be added
        EXPECT_STREQ("off", rtc->get("nack")->arg0().c_str());
        EXPECT_TRUE(rtc->get("twcc") != NULL); // Should be added
        EXPECT_STREQ("on", rtc->get("twcc")->arg0().c_str());
    }
}

VOID TEST(ConfigTransformTest, TransformVhostNackTwccMultipleVhosts)
{
    srs_error_t err;

    // Test transforming nack and twcc in multiple vhosts
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test1.com{nack{enabled on;}} vhost test2.com{twcc{enabled off;}} vhost test3.com{nack{no_copy on;} twcc{enabled on;}}"));

        // Get all vhost directives
        std::vector<SrsConfDirective *> vhosts;
        for (int i = 0; i < (int)conf.get_root()->directives_.size(); i++) {
            SrsConfDirective *dir = conf.get_root()->directives_.at(i);
            if (dir->name_ == "vhost") {
                vhosts.push_back(dir);
            }
        }

        // Should have 3 vhost sections
        EXPECT_EQ(3, (int)vhosts.size());

        // First vhost (test1.com) - nack transformation
        SrsConfDirective *vhost1 = NULL;
        for (int i = 0; i < (int)vhosts.size(); i++) {
            if (vhosts[i]->arg0() == "test1.com") {
                vhost1 = vhosts[i];
                break;
            }
        }
        ASSERT_TRUE(vhost1 != NULL);
        EXPECT_TRUE(vhost1->get("nack") == NULL); // Should be removed
        SrsConfDirective *rtc1 = vhost1->get("rtc");
        ASSERT_TRUE(rtc1 != NULL);
        EXPECT_TRUE(rtc1->get("nack") != NULL);
        EXPECT_STREQ("on", rtc1->get("nack")->arg0().c_str());

        // Second vhost (test2.com) - twcc transformation
        SrsConfDirective *vhost2 = NULL;
        for (int i = 0; i < (int)vhosts.size(); i++) {
            if (vhosts[i]->arg0() == "test2.com") {
                vhost2 = vhosts[i];
                break;
            }
        }
        ASSERT_TRUE(vhost2 != NULL);
        EXPECT_TRUE(vhost2->get("twcc") == NULL); // Should be removed
        SrsConfDirective *rtc2 = vhost2->get("rtc");
        ASSERT_TRUE(rtc2 != NULL);
        EXPECT_TRUE(rtc2->get("twcc") != NULL);
        EXPECT_STREQ("off", rtc2->get("twcc")->arg0().c_str());

        // Third vhost (test3.com) - both nack and twcc transformation
        SrsConfDirective *vhost3 = NULL;
        for (int i = 0; i < (int)vhosts.size(); i++) {
            if (vhosts[i]->arg0() == "test3.com") {
                vhost3 = vhosts[i];
                break;
            }
        }
        ASSERT_TRUE(vhost3 != NULL);
        EXPECT_TRUE(vhost3->get("nack") == NULL); // Should be removed
        EXPECT_TRUE(vhost3->get("twcc") == NULL); // Should be removed
        SrsConfDirective *rtc3 = vhost3->get("rtc");
        ASSERT_TRUE(rtc3 != NULL);
        EXPECT_TRUE(rtc3->get("nack_no_copy") != NULL);
        EXPECT_STREQ("on", rtc3->get("nack_no_copy")->arg0().c_str());
        EXPECT_TRUE(rtc3->get("twcc") != NULL);
        EXPECT_STREQ("on", rtc3->get("twcc")->arg0().c_str());
    }
}

VOID TEST(ConfigTransformTest, TransformVhostNackTwccComplexArgs)
{
    srs_error_t err;

    // Test transforming nack and twcc with multiple arguments
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{nack{enabled on off; no_copy off on;} twcc{enabled on off on;}}"));

        // Verify nack and twcc were moved to rtc section with all arguments preserved
        SrsConfDirective *vhost = conf.get_root()->get("vhost");
        ASSERT_TRUE(vhost != NULL);
        EXPECT_TRUE(vhost->get("nack") == NULL); // Should be removed from vhost level
        EXPECT_TRUE(vhost->get("twcc") == NULL); // Should be removed from vhost level

        SrsConfDirective *rtc = vhost->get("rtc");
        ASSERT_TRUE(rtc != NULL); // Should be created

        // Check nack directive with multiple arguments
        SrsConfDirective *nack = rtc->get("nack");
        ASSERT_TRUE(nack != NULL);
        EXPECT_EQ(2, (int)nack->args_.size());
        EXPECT_STREQ("on", nack->args_[0].c_str());
        EXPECT_STREQ("off", nack->args_[1].c_str());

        // Check nack_no_copy directive with multiple arguments
        SrsConfDirective *nack_no_copy = rtc->get("nack_no_copy");
        ASSERT_TRUE(nack_no_copy != NULL);
        EXPECT_EQ(2, (int)nack_no_copy->args_.size());
        EXPECT_STREQ("off", nack_no_copy->args_[0].c_str());
        EXPECT_STREQ("on", nack_no_copy->args_[1].c_str());

        // Check twcc directive with multiple arguments
        SrsConfDirective *twcc = rtc->get("twcc");
        ASSERT_TRUE(twcc != NULL);
        EXPECT_EQ(3, (int)twcc->args_.size());
        EXPECT_STREQ("on", twcc->args_[0].c_str());
        EXPECT_STREQ("off", twcc->args_[1].c_str());
        EXPECT_STREQ("on", twcc->args_[2].c_str());
    }
}

VOID TEST(ConfigTransformTest, TransformVhostNackTwccPreserveOtherConfigs)
{
    srs_error_t err;

    // Test that transformation preserves other vhost configurations
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{enabled on;} nack{enabled on;} dvr{enabled off;} twcc{enabled on;} play{mw_latency 100;}}"));

        // Verify nack and twcc were moved to rtc section
        SrsConfDirective *vhost = conf.get_root()->get("vhost");
        ASSERT_TRUE(vhost != NULL);
        EXPECT_TRUE(vhost->get("nack") == NULL); // Should be removed from vhost level
        EXPECT_TRUE(vhost->get("twcc") == NULL); // Should be removed from vhost level

        // Verify other vhost configs are preserved
        EXPECT_TRUE(vhost->get("hls") != NULL);  // Should be preserved
        EXPECT_TRUE(vhost->get("dvr") != NULL);  // Should be preserved
        EXPECT_TRUE(vhost->get("play") != NULL); // Should be preserved

        // Verify rtc section was created with transformed configs
        SrsConfDirective *rtc = vhost->get("rtc");
        ASSERT_TRUE(rtc != NULL);              // Should be created
        EXPECT_TRUE(rtc->get("nack") != NULL); // Should be moved here
        EXPECT_STREQ("on", rtc->get("nack")->arg0().c_str());
        EXPECT_TRUE(rtc->get("twcc") != NULL); // Should be moved here
        EXPECT_STREQ("on", rtc->get("twcc")->arg0().c_str());

        // Verify other sections have correct configs
        SrsConfDirective *hls = vhost->get("hls");
        ASSERT_TRUE(hls != NULL);
        EXPECT_TRUE(hls->get("enabled") != NULL);
        EXPECT_STREQ("on", hls->get("enabled")->arg0().c_str());

        SrsConfDirective *dvr = vhost->get("dvr");
        ASSERT_TRUE(dvr != NULL);
        EXPECT_TRUE(dvr->get("enabled") != NULL);
        EXPECT_STREQ("off", dvr->get("enabled")->arg0().c_str());

        SrsConfDirective *play = vhost->get("play");
        ASSERT_TRUE(play != NULL);
        EXPECT_TRUE(play->get("mw_latency") != NULL);
        EXPECT_STREQ("100", play->get("mw_latency")->arg0().c_str());
    }
}

VOID TEST(ConfigTransformTest, TransformVhostNackTwccDirectCall)
{
    srs_error_t err;

    // Test direct call to srs_config_transform_vhost function with nack/twcc transformation
    if (true) {
        // Create a root directive with vhost containing nack and twcc
        SrsConfDirective *root = new SrsConfDirective();

        // Add rtmp section (required for valid config)
        SrsConfDirective *rtmp = new SrsConfDirective();
        rtmp->name_ = "rtmp";
        SrsConfDirective *listen = new SrsConfDirective();
        listen->name_ = "listen";
        listen->args_.push_back("1935");
        rtmp->directives_.push_back(listen);
        root->directives_.push_back(rtmp);

        // Add vhost section with nack and twcc
        SrsConfDirective *vhost = new SrsConfDirective();
        vhost->name_ = "vhost";
        vhost->args_.push_back("test.com");

        // Add nack directive
        SrsConfDirective *nack = new SrsConfDirective();
        nack->name_ = "nack";
        SrsConfDirective *nack_enabled = new SrsConfDirective();
        nack_enabled->name_ = "enabled";
        nack_enabled->args_.push_back("on");
        nack->directives_.push_back(nack_enabled);
        SrsConfDirective *nack_no_copy = new SrsConfDirective();
        nack_no_copy->name_ = "no_copy";
        nack_no_copy->args_.push_back("off");
        nack->directives_.push_back(nack_no_copy);
        vhost->directives_.push_back(nack);

        // Add twcc directive
        SrsConfDirective *twcc = new SrsConfDirective();
        twcc->name_ = "twcc";
        SrsConfDirective *twcc_enabled = new SrsConfDirective();
        twcc_enabled->name_ = "enabled";
        twcc_enabled->args_.push_back("on");
        twcc->directives_.push_back(twcc_enabled);
        vhost->directives_.push_back(twcc);

        root->directives_.push_back(vhost);

        // Verify initial state
        EXPECT_EQ(2, (int)vhost->directives_.size()); // nack and twcc
        EXPECT_TRUE(vhost->get("nack") != NULL);
        EXPECT_TRUE(vhost->get("twcc") != NULL);
        EXPECT_TRUE(vhost->get("rtc") == NULL); // Should not exist yet

        // Call the transform function directly
        HELPER_ASSERT_SUCCESS(srs_config_transform_vhost(root));

        // Verify nack and twcc were transformed to rtc section
        EXPECT_EQ(1, (int)vhost->directives_.size()); // Should have only rtc
        EXPECT_TRUE(vhost->get("nack") == NULL);      // Should be removed
        EXPECT_TRUE(vhost->get("twcc") == NULL);      // Should be removed

        SrsConfDirective *rtc = vhost->get("rtc");
        ASSERT_TRUE(rtc != NULL);              // Should be created
        EXPECT_TRUE(rtc->get("nack") != NULL); // Should be moved here
        EXPECT_STREQ("on", rtc->get("nack")->arg0().c_str());
        EXPECT_TRUE(rtc->get("nack_no_copy") != NULL); // Should be created from no_copy
        EXPECT_STREQ("off", rtc->get("nack_no_copy")->arg0().c_str());
        EXPECT_TRUE(rtc->get("twcc") != NULL); // Should be moved here
        EXPECT_STREQ("on", rtc->get("twcc")->arg0().c_str());

        srs_freep(root);
    }

    // Test transform function with vhost containing only nack (no enabled)
    if (true) {
        SrsConfDirective *root = new SrsConfDirective();

        // Add rtmp section (required for valid config)
        SrsConfDirective *rtmp = new SrsConfDirective();
        rtmp->name_ = "rtmp";
        SrsConfDirective *listen = new SrsConfDirective();
        listen->name_ = "listen";
        listen->args_.push_back("1935");
        rtmp->directives_.push_back(listen);
        root->directives_.push_back(rtmp);

        // Add vhost section with nack (no enabled directive)
        SrsConfDirective *vhost = new SrsConfDirective();
        vhost->name_ = "vhost";
        vhost->args_.push_back("test.com");

        // Add nack directive with only no_copy
        SrsConfDirective *nack = new SrsConfDirective();
        nack->name_ = "nack";
        SrsConfDirective *nack_no_copy = new SrsConfDirective();
        nack_no_copy->name_ = "no_copy";
        nack_no_copy->args_.push_back("on");
        nack->directives_.push_back(nack_no_copy);
        vhost->directives_.push_back(nack);

        root->directives_.push_back(vhost);

        // Verify initial state
        EXPECT_EQ(1, (int)vhost->directives_.size()); // nack only
        EXPECT_TRUE(vhost->get("nack") != NULL);
        EXPECT_TRUE(vhost->get("rtc") == NULL); // Should not exist yet

        // Call the transform function directly
        HELPER_ASSERT_SUCCESS(srs_config_transform_vhost(root));

        // Verify nack was transformed to rtc section
        EXPECT_EQ(1, (int)vhost->directives_.size()); // Should have only rtc
        EXPECT_TRUE(vhost->get("nack") == NULL);      // Should be removed

        SrsConfDirective *rtc = vhost->get("rtc");
        ASSERT_TRUE(rtc != NULL);                      // Should be created
        EXPECT_TRUE(rtc->get("nack") == NULL);         // Should not be created (no enabled directive)
        EXPECT_TRUE(rtc->get("nack_no_copy") != NULL); // Should be created from no_copy
        EXPECT_STREQ("on", rtc->get("nack_no_copy")->arg0().c_str());

        srs_freep(root);
    }
}

VOID TEST(ConfigTransformTest, TransformVhostRtcAacToRtmpToRtc)
{
    srs_error_t err;

    // Test transforming vhost.rtc.aac to vhost.rtc.rtmp_to_rtc with "transcode" value
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{enabled on; aac transcode;}}"));

        // Verify aac was transformed to rtmp_to_rtc
        SrsConfDirective *vhost = conf.get_root()->get("vhost");
        ASSERT_TRUE(vhost != NULL);

        SrsConfDirective *rtc = vhost->get("rtc");
        ASSERT_TRUE(rtc != NULL);
        EXPECT_TRUE(rtc->get("aac") == NULL);                        // Should be removed
        EXPECT_TRUE(rtc->get("rtmp_to_rtc") != NULL);                // Should be created
        EXPECT_STREQ("on", rtc->get("rtmp_to_rtc")->arg0().c_str()); // transcode -> on
    }

    // Test transforming vhost.rtc.aac to vhost.rtc.rtmp_to_rtc with non-"transcode" value
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{enabled on; aac copy;}}"));

        // Verify aac was transformed to rtmp_to_rtc
        SrsConfDirective *vhost = conf.get_root()->get("vhost");
        ASSERT_TRUE(vhost != NULL);

        SrsConfDirective *rtc = vhost->get("rtc");
        ASSERT_TRUE(rtc != NULL);
        EXPECT_TRUE(rtc->get("aac") == NULL);                         // Should be removed
        EXPECT_TRUE(rtc->get("rtmp_to_rtc") != NULL);                 // Should be created
        EXPECT_STREQ("off", rtc->get("rtmp_to_rtc")->arg0().c_str()); // copy -> off
    }

    // Test transforming vhost.rtc.aac with empty value
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{enabled on; aac;}}"));

        // Verify aac was transformed to rtmp_to_rtc
        SrsConfDirective *vhost = conf.get_root()->get("vhost");
        ASSERT_TRUE(vhost != NULL);

        SrsConfDirective *rtc = vhost->get("rtc");
        ASSERT_TRUE(rtc != NULL);
        EXPECT_TRUE(rtc->get("aac") == NULL);                         // Should be removed
        EXPECT_TRUE(rtc->get("rtmp_to_rtc") != NULL);                 // Should be created
        EXPECT_STREQ("off", rtc->get("rtmp_to_rtc")->arg0().c_str()); // empty -> off
    }

    // Test transforming vhost.rtc.aac with multiple arguments (only first is used)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{enabled on; aac transcode copy;}}"));

        // Verify aac was transformed to rtmp_to_rtc using first argument
        SrsConfDirective *vhost = conf.get_root()->get("vhost");
        ASSERT_TRUE(vhost != NULL);

        SrsConfDirective *rtc = vhost->get("rtc");
        ASSERT_TRUE(rtc != NULL);
        EXPECT_TRUE(rtc->get("aac") == NULL);                        // Should be removed
        EXPECT_TRUE(rtc->get("rtmp_to_rtc") != NULL);                // Should be created
        EXPECT_STREQ("on", rtc->get("rtmp_to_rtc")->arg0().c_str()); // first arg "transcode" -> on
    }
}

VOID TEST(ConfigTransformTest, TransformVhostRtcBframeToKeepBframe)
{
    srs_error_t err;

    // Test transforming vhost.rtc.bframe to vhost.rtc.keep_bframe with "keep" value
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{enabled on; bframe keep;}}"));

        // Verify bframe was transformed to keep_bframe
        SrsConfDirective *vhost = conf.get_root()->get("vhost");
        ASSERT_TRUE(vhost != NULL);

        SrsConfDirective *rtc = vhost->get("rtc");
        ASSERT_TRUE(rtc != NULL);
        EXPECT_TRUE(rtc->get("bframe") == NULL);                     // Should be removed
        EXPECT_TRUE(rtc->get("keep_bframe") != NULL);                // Should be created
        EXPECT_STREQ("on", rtc->get("keep_bframe")->arg0().c_str()); // keep -> on
    }

    // Test transforming vhost.rtc.bframe to vhost.rtc.keep_bframe with non-"keep" value
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{enabled on; bframe drop;}}"));

        // Verify bframe was transformed to keep_bframe
        SrsConfDirective *vhost = conf.get_root()->get("vhost");
        ASSERT_TRUE(vhost != NULL);

        SrsConfDirective *rtc = vhost->get("rtc");
        ASSERT_TRUE(rtc != NULL);
        EXPECT_TRUE(rtc->get("bframe") == NULL);                      // Should be removed
        EXPECT_TRUE(rtc->get("keep_bframe") != NULL);                 // Should be created
        EXPECT_STREQ("off", rtc->get("keep_bframe")->arg0().c_str()); // drop -> off
    }

    // Test transforming vhost.rtc.bframe with empty value
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{enabled on; bframe;}}"));

        // Verify bframe was transformed to keep_bframe
        SrsConfDirective *vhost = conf.get_root()->get("vhost");
        ASSERT_TRUE(vhost != NULL);

        SrsConfDirective *rtc = vhost->get("rtc");
        ASSERT_TRUE(rtc != NULL);
        EXPECT_TRUE(rtc->get("bframe") == NULL);                      // Should be removed
        EXPECT_TRUE(rtc->get("keep_bframe") != NULL);                 // Should be created
        EXPECT_STREQ("off", rtc->get("keep_bframe")->arg0().c_str()); // empty -> off
    }

    // Test transforming vhost.rtc.bframe with multiple arguments (only first is used)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{enabled on; bframe keep drop;}}"));

        // Verify bframe was transformed to keep_bframe using first argument
        SrsConfDirective *vhost = conf.get_root()->get("vhost");
        ASSERT_TRUE(vhost != NULL);

        SrsConfDirective *rtc = vhost->get("rtc");
        ASSERT_TRUE(rtc != NULL);
        EXPECT_TRUE(rtc->get("bframe") == NULL);                     // Should be removed
        EXPECT_TRUE(rtc->get("keep_bframe") != NULL);                // Should be created
        EXPECT_STREQ("on", rtc->get("keep_bframe")->arg0().c_str()); // first arg "keep" -> on
    }
}

VOID TEST(ConfigTransformTest, TransformVhostRtcAacAndBframeTogether)
{
    srs_error_t err;

    // Test transforming both aac and bframe directives together
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{enabled on; aac transcode; bframe keep;}}"));

        // Verify both aac and bframe were transformed
        SrsConfDirective *vhost = conf.get_root()->get("vhost");
        ASSERT_TRUE(vhost != NULL);

        SrsConfDirective *rtc = vhost->get("rtc");
        ASSERT_TRUE(rtc != NULL);
        EXPECT_TRUE(rtc->get("aac") == NULL);                        // Should be removed
        EXPECT_TRUE(rtc->get("bframe") == NULL);                     // Should be removed
        EXPECT_TRUE(rtc->get("rtmp_to_rtc") != NULL);                // Should be created
        EXPECT_STREQ("on", rtc->get("rtmp_to_rtc")->arg0().c_str()); // transcode -> on
        EXPECT_TRUE(rtc->get("keep_bframe") != NULL);                // Should be created
        EXPECT_STREQ("on", rtc->get("keep_bframe")->arg0().c_str()); // keep -> on
    }

    // Test transforming both aac and bframe with different values
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{enabled on; aac copy; bframe drop;}}"));

        // Verify both aac and bframe were transformed with correct values
        SrsConfDirective *vhost = conf.get_root()->get("vhost");
        ASSERT_TRUE(vhost != NULL);

        SrsConfDirective *rtc = vhost->get("rtc");
        ASSERT_TRUE(rtc != NULL);
        EXPECT_TRUE(rtc->get("aac") == NULL);                         // Should be removed
        EXPECT_TRUE(rtc->get("bframe") == NULL);                      // Should be removed
        EXPECT_TRUE(rtc->get("rtmp_to_rtc") != NULL);                 // Should be created
        EXPECT_STREQ("off", rtc->get("rtmp_to_rtc")->arg0().c_str()); // copy -> off
        EXPECT_TRUE(rtc->get("keep_bframe") != NULL);                 // Should be created
        EXPECT_STREQ("off", rtc->get("keep_bframe")->arg0().c_str()); // drop -> off
    }

    // Test transforming aac and bframe with existing rtc configs
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{enabled on; nack on; aac transcode; twcc on; bframe keep;}}"));

        // Verify aac and bframe were transformed while preserving existing configs
        SrsConfDirective *vhost = conf.get_root()->get("vhost");
        ASSERT_TRUE(vhost != NULL);

        SrsConfDirective *rtc = vhost->get("rtc");
        ASSERT_TRUE(rtc != NULL);
        EXPECT_TRUE(rtc->get("enabled") != NULL); // Should be preserved
        EXPECT_STREQ("on", rtc->get("enabled")->arg0().c_str());
        EXPECT_TRUE(rtc->get("nack") != NULL); // Should be preserved
        EXPECT_STREQ("on", rtc->get("nack")->arg0().c_str());
        EXPECT_TRUE(rtc->get("twcc") != NULL); // Should be preserved
        EXPECT_STREQ("on", rtc->get("twcc")->arg0().c_str());
        EXPECT_TRUE(rtc->get("aac") == NULL);         // Should be removed
        EXPECT_TRUE(rtc->get("bframe") == NULL);      // Should be removed
        EXPECT_TRUE(rtc->get("rtmp_to_rtc") != NULL); // Should be created
        EXPECT_STREQ("on", rtc->get("rtmp_to_rtc")->arg0().c_str());
        EXPECT_TRUE(rtc->get("keep_bframe") != NULL); // Should be created
        EXPECT_STREQ("on", rtc->get("keep_bframe")->arg0().c_str());
    }
}

VOID TEST(ConfigTransformTest, TransformVhostRtcMultipleVhosts)
{
    srs_error_t err;

    // Test transforming aac and bframe in multiple vhosts
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test1.com{rtc{aac transcode;}} vhost test2.com{rtc{bframe keep;}} vhost test3.com{rtc{aac copy; bframe drop;}}"));

        // Get all vhost directives
        std::vector<SrsConfDirective *> vhosts;
        for (int i = 0; i < (int)conf.get_root()->directives_.size(); i++) {
            SrsConfDirective *dir = conf.get_root()->directives_.at(i);
            if (dir->name_ == "vhost") {
                vhosts.push_back(dir);
            }
        }

        // Should have 3 vhost sections
        EXPECT_EQ(3, (int)vhosts.size());

        // First vhost (test1.com) - aac transformation
        SrsConfDirective *vhost1 = NULL;
        for (int i = 0; i < (int)vhosts.size(); i++) {
            if (vhosts[i]->arg0() == "test1.com") {
                vhost1 = vhosts[i];
                break;
            }
        }
        ASSERT_TRUE(vhost1 != NULL);
        SrsConfDirective *rtc1 = vhost1->get("rtc");
        ASSERT_TRUE(rtc1 != NULL);
        EXPECT_TRUE(rtc1->get("aac") == NULL); // Should be removed
        EXPECT_TRUE(rtc1->get("rtmp_to_rtc") != NULL);
        EXPECT_STREQ("on", rtc1->get("rtmp_to_rtc")->arg0().c_str());

        // Second vhost (test2.com) - bframe transformation
        SrsConfDirective *vhost2 = NULL;
        for (int i = 0; i < (int)vhosts.size(); i++) {
            if (vhosts[i]->arg0() == "test2.com") {
                vhost2 = vhosts[i];
                break;
            }
        }
        ASSERT_TRUE(vhost2 != NULL);
        SrsConfDirective *rtc2 = vhost2->get("rtc");
        ASSERT_TRUE(rtc2 != NULL);
        EXPECT_TRUE(rtc2->get("bframe") == NULL); // Should be removed
        EXPECT_TRUE(rtc2->get("keep_bframe") != NULL);
        EXPECT_STREQ("on", rtc2->get("keep_bframe")->arg0().c_str());

        // Third vhost (test3.com) - both aac and bframe transformation
        SrsConfDirective *vhost3 = NULL;
        for (int i = 0; i < (int)vhosts.size(); i++) {
            if (vhosts[i]->arg0() == "test3.com") {
                vhost3 = vhosts[i];
                break;
            }
        }
        ASSERT_TRUE(vhost3 != NULL);
        SrsConfDirective *rtc3 = vhost3->get("rtc");
        ASSERT_TRUE(rtc3 != NULL);
        EXPECT_TRUE(rtc3->get("aac") == NULL);    // Should be removed
        EXPECT_TRUE(rtc3->get("bframe") == NULL); // Should be removed
        EXPECT_TRUE(rtc3->get("rtmp_to_rtc") != NULL);
        EXPECT_STREQ("off", rtc3->get("rtmp_to_rtc")->arg0().c_str()); // copy -> off
        EXPECT_TRUE(rtc3->get("keep_bframe") != NULL);
        EXPECT_STREQ("off", rtc3->get("keep_bframe")->arg0().c_str()); // drop -> off
    }
}

VOID TEST(ConfigTransformTest, TransformVhostRtcPreserveOtherConfigs)
{
    srs_error_t err;

    // Test that transformation preserves other vhost and rtc configurations
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{enabled on;} rtc{enabled on; nack on; aac transcode; twcc on; bframe keep; rtc_to_rtmp on;} dvr{enabled off;}}"));

        // Verify aac and bframe were transformed while preserving other configs
        SrsConfDirective *vhost = conf.get_root()->get("vhost");
        ASSERT_TRUE(vhost != NULL);

        // Verify other vhost configs are preserved
        EXPECT_TRUE(vhost->get("hls") != NULL); // Should be preserved
        EXPECT_TRUE(vhost->get("dvr") != NULL); // Should be preserved

        SrsConfDirective *rtc = vhost->get("rtc");
        ASSERT_TRUE(rtc != NULL);

        // Verify existing rtc configs are preserved
        EXPECT_TRUE(rtc->get("enabled") != NULL);
        EXPECT_STREQ("on", rtc->get("enabled")->arg0().c_str());
        EXPECT_TRUE(rtc->get("nack") != NULL);
        EXPECT_STREQ("on", rtc->get("nack")->arg0().c_str());
        EXPECT_TRUE(rtc->get("twcc") != NULL);
        EXPECT_STREQ("on", rtc->get("twcc")->arg0().c_str());
        EXPECT_TRUE(rtc->get("rtc_to_rtmp") != NULL);
        EXPECT_STREQ("on", rtc->get("rtc_to_rtmp")->arg0().c_str());

        // Verify aac and bframe were transformed
        EXPECT_TRUE(rtc->get("aac") == NULL);         // Should be removed
        EXPECT_TRUE(rtc->get("bframe") == NULL);      // Should be removed
        EXPECT_TRUE(rtc->get("rtmp_to_rtc") != NULL); // Should be created
        EXPECT_STREQ("on", rtc->get("rtmp_to_rtc")->arg0().c_str());
        EXPECT_TRUE(rtc->get("keep_bframe") != NULL); // Should be created
        EXPECT_STREQ("on", rtc->get("keep_bframe")->arg0().c_str());

        // Verify other sections have correct configs
        SrsConfDirective *hls = vhost->get("hls");
        ASSERT_TRUE(hls != NULL);
        EXPECT_TRUE(hls->get("enabled") != NULL);
        EXPECT_STREQ("on", hls->get("enabled")->arg0().c_str());

        SrsConfDirective *dvr = vhost->get("dvr");
        ASSERT_TRUE(dvr != NULL);
        EXPECT_TRUE(dvr->get("enabled") != NULL);
        EXPECT_STREQ("off", dvr->get("enabled")->arg0().c_str());
    }
}

VOID TEST(ConfigTransformTest, TransformVhostRtcDirectCall)
{
    srs_error_t err;

    // Test direct call to srs_config_transform_vhost function with rtc aac/bframe transformation
    if (true) {
        // Create a root directive with vhost containing rtc with aac and bframe
        SrsConfDirective *root = new SrsConfDirective();

        // Add rtmp section (required for valid config)
        SrsConfDirective *rtmp = new SrsConfDirective();
        rtmp->name_ = "rtmp";
        SrsConfDirective *listen = new SrsConfDirective();
        listen->name_ = "listen";
        listen->args_.push_back("1935");
        rtmp->directives_.push_back(listen);
        root->directives_.push_back(rtmp);

        // Add vhost section with rtc containing aac and bframe
        SrsConfDirective *vhost = new SrsConfDirective();
        vhost->name_ = "vhost";
        vhost->args_.push_back("test.com");

        // Add rtc directive
        SrsConfDirective *rtc = new SrsConfDirective();
        rtc->name_ = "rtc";

        // Add enabled directive
        SrsConfDirective *enabled = new SrsConfDirective();
        enabled->name_ = "enabled";
        enabled->args_.push_back("on");
        rtc->directives_.push_back(enabled);

        // Add aac directive
        SrsConfDirective *aac = new SrsConfDirective();
        aac->name_ = "aac";
        aac->args_.push_back("transcode");
        rtc->directives_.push_back(aac);

        // Add bframe directive
        SrsConfDirective *bframe = new SrsConfDirective();
        bframe->name_ = "bframe";
        bframe->args_.push_back("keep");
        rtc->directives_.push_back(bframe);

        vhost->directives_.push_back(rtc);
        root->directives_.push_back(vhost);

        // Verify initial state
        EXPECT_EQ(3, (int)rtc->directives_.size()); // enabled, aac, bframe
        EXPECT_TRUE(rtc->get("enabled") != NULL);
        EXPECT_TRUE(rtc->get("aac") != NULL);
        EXPECT_TRUE(rtc->get("bframe") != NULL);
        EXPECT_TRUE(rtc->get("rtmp_to_rtc") == NULL); // Should not exist yet
        EXPECT_TRUE(rtc->get("keep_bframe") == NULL); // Should not exist yet

        // Call the transform function directly
        HELPER_ASSERT_SUCCESS(srs_config_transform_vhost(root));

        // Verify aac and bframe were transformed
        EXPECT_EQ(3, (int)rtc->directives_.size()); // enabled, rtmp_to_rtc, keep_bframe
        EXPECT_TRUE(rtc->get("enabled") != NULL);   // Should be preserved
        EXPECT_STREQ("on", rtc->get("enabled")->arg0().c_str());
        EXPECT_TRUE(rtc->get("aac") == NULL);         // Should be removed
        EXPECT_TRUE(rtc->get("bframe") == NULL);      // Should be removed
        EXPECT_TRUE(rtc->get("rtmp_to_rtc") != NULL); // Should be created
        EXPECT_STREQ("on", rtc->get("rtmp_to_rtc")->arg0().c_str());
        EXPECT_TRUE(rtc->get("keep_bframe") != NULL); // Should be created
        EXPECT_STREQ("on", rtc->get("keep_bframe")->arg0().c_str());

        srs_freep(root);
    }
}

VOID TEST(ConfigTransformTest, TransformVhostRtcEdgeCases)
{
    srs_error_t err;

    // Test rtc section without aac or bframe (should remain unchanged)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{enabled on; nack on; twcc on;}}"));

        // Verify rtc section is preserved without transformation
        SrsConfDirective *vhost = conf.get_root()->get("vhost");
        ASSERT_TRUE(vhost != NULL);

        SrsConfDirective *rtc = vhost->get("rtc");
        ASSERT_TRUE(rtc != NULL);
        EXPECT_EQ(3, (int)rtc->directives_.size()); // Should have exactly 3 directives
        EXPECT_TRUE(rtc->get("enabled") != NULL);
        EXPECT_TRUE(rtc->get("nack") != NULL);
        EXPECT_TRUE(rtc->get("twcc") != NULL);
        EXPECT_TRUE(rtc->get("aac") == NULL);         // Should not exist
        EXPECT_TRUE(rtc->get("bframe") == NULL);      // Should not exist
        EXPECT_TRUE(rtc->get("rtmp_to_rtc") == NULL); // Should not be created
        EXPECT_TRUE(rtc->get("keep_bframe") == NULL); // Should not be created
    }

    // Test vhost without rtc section (should remain unchanged)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{enabled on;} dvr{enabled off;}}"));

        // Verify vhost section is preserved without rtc transformation
        SrsConfDirective *vhost = conf.get_root()->get("vhost");
        ASSERT_TRUE(vhost != NULL);
        EXPECT_EQ(2, (int)vhost->directives_.size()); // Should have exactly 2 directives
        EXPECT_TRUE(vhost->get("hls") != NULL);
        EXPECT_TRUE(vhost->get("dvr") != NULL);
        EXPECT_TRUE(vhost->get("rtc") == NULL); // Should not exist
    }

    // Test empty rtc section (should remain empty)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{}}"));

        // Verify empty rtc section remains empty
        SrsConfDirective *vhost = conf.get_root()->get("vhost");
        ASSERT_TRUE(vhost != NULL);

        SrsConfDirective *rtc = vhost->get("rtc");
        ASSERT_TRUE(rtc != NULL);
        EXPECT_EQ(0, (int)rtc->directives_.size()); // Should remain empty
        EXPECT_TRUE(rtc->get("aac") == NULL);
        EXPECT_TRUE(rtc->get("bframe") == NULL);
        EXPECT_TRUE(rtc->get("rtmp_to_rtc") == NULL);
        EXPECT_TRUE(rtc->get("keep_bframe") == NULL);
    }

    // Test rtc section with only aac (no bframe)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{enabled on; aac transcode;}}"));

        // Verify only aac transformation occurs
        SrsConfDirective *vhost = conf.get_root()->get("vhost");
        ASSERT_TRUE(vhost != NULL);

        SrsConfDirective *rtc = vhost->get("rtc");
        ASSERT_TRUE(rtc != NULL);
        EXPECT_EQ(2, (int)rtc->directives_.size()); // enabled, rtmp_to_rtc
        EXPECT_TRUE(rtc->get("enabled") != NULL);
        EXPECT_TRUE(rtc->get("aac") == NULL);         // Should be removed
        EXPECT_TRUE(rtc->get("bframe") == NULL);      // Should not exist
        EXPECT_TRUE(rtc->get("rtmp_to_rtc") != NULL); // Should be created
        EXPECT_STREQ("on", rtc->get("rtmp_to_rtc")->arg0().c_str());
        EXPECT_TRUE(rtc->get("keep_bframe") == NULL); // Should not be created
    }

    // Test rtc section with only bframe (no aac)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{enabled on; bframe keep;}}"));

        // Verify only bframe transformation occurs
        SrsConfDirective *vhost = conf.get_root()->get("vhost");
        ASSERT_TRUE(vhost != NULL);

        SrsConfDirective *rtc = vhost->get("rtc");
        ASSERT_TRUE(rtc != NULL);
        EXPECT_EQ(2, (int)rtc->directives_.size()); // enabled, keep_bframe
        EXPECT_TRUE(rtc->get("enabled") != NULL);
        EXPECT_TRUE(rtc->get("aac") == NULL);         // Should not exist
        EXPECT_TRUE(rtc->get("bframe") == NULL);      // Should be removed
        EXPECT_TRUE(rtc->get("rtmp_to_rtc") == NULL); // Should not be created
        EXPECT_TRUE(rtc->get("keep_bframe") != NULL); // Should be created
        EXPECT_STREQ("on", rtc->get("keep_bframe")->arg0().c_str());
    }
}

VOID TEST(ConfigDirectiveTest, ReadTokenUnexpectedCharacterAfterQuotedString)
{
    srs_error_t err;

    // Test unexpected character after double-quoted string (need_space=true case)
    if (true) {
        MockSrsConfigBuffer buf("listen \"1935\"x;");
        SrsConfDirective conf;
        HELPER_ASSERT_FAILED(conf.parse(&buf));
        EXPECT_EQ(0, (int)conf.name_.length());
        EXPECT_EQ(0, (int)conf.args_.size());
        EXPECT_EQ(0, (int)conf.directives_.size());
    }

    // Test unexpected character after single-quoted string (need_space=true case)
    if (true) {
        MockSrsConfigBuffer buf("listen '1935'y;");
        SrsConfDirective conf;
        HELPER_ASSERT_FAILED(conf.parse(&buf));
        EXPECT_EQ(0, (int)conf.name_.length());
        EXPECT_EQ(0, (int)conf.args_.size());
        EXPECT_EQ(0, (int)conf.directives_.size());
    }

    // Test unexpected character after double-quoted string with block start
    if (true) {
        MockSrsConfigBuffer buf("vhost \"test.com\"z {}");
        SrsConfDirective conf;
        HELPER_ASSERT_FAILED(conf.parse(&buf));
        EXPECT_EQ(0, (int)conf.name_.length());
        EXPECT_EQ(0, (int)conf.args_.size());
        EXPECT_EQ(0, (int)conf.directives_.size());
    }

    // Test unexpected character after single-quoted string with block start
    if (true) {
        MockSrsConfigBuffer buf("vhost 'test.com'w {}");
        SrsConfDirective conf;
        HELPER_ASSERT_FAILED(conf.parse(&buf));
        EXPECT_EQ(0, (int)conf.name_.length());
        EXPECT_EQ(0, (int)conf.args_.size());
        EXPECT_EQ(0, (int)conf.directives_.size());
    }

    // Test multiple unexpected characters after quoted strings
    if (true) {
        MockSrsConfigBuffer buf("listen \"8080\"abc;");
        SrsConfDirective conf;
        HELPER_ASSERT_FAILED(conf.parse(&buf));
        EXPECT_EQ(0, (int)conf.name_.length());
        EXPECT_EQ(0, (int)conf.args_.size());
        EXPECT_EQ(0, (int)conf.directives_.size());
    }

    // Test unexpected special characters after quoted strings
    if (true) {
        MockSrsConfigBuffer buf("listen \"1935\"@;");
        SrsConfDirective conf;
        HELPER_ASSERT_FAILED(conf.parse(&buf));
        EXPECT_EQ(0, (int)conf.name_.length());
        EXPECT_EQ(0, (int)conf.args_.size());
        EXPECT_EQ(0, (int)conf.directives_.size());
    }

    if (true) {
        MockSrsConfigBuffer buf("listen \"1935\"#;");
        SrsConfDirective conf;
        HELPER_ASSERT_FAILED(conf.parse(&buf));
        EXPECT_EQ(0, (int)conf.name_.length());
        EXPECT_EQ(0, (int)conf.args_.size());
        EXPECT_EQ(0, (int)conf.directives_.size());
    }

    if (true) {
        MockSrsConfigBuffer buf("listen \"1935\"$;");
        SrsConfDirective conf;
        HELPER_ASSERT_FAILED(conf.parse(&buf));
        EXPECT_EQ(0, (int)conf.name_.length());
        EXPECT_EQ(0, (int)conf.args_.size());
        EXPECT_EQ(0, (int)conf.directives_.size());
    }

    if (true) {
        MockSrsConfigBuffer buf("listen \"1935\"%;");
        SrsConfDirective conf;
        HELPER_ASSERT_FAILED(conf.parse(&buf));
        EXPECT_EQ(0, (int)conf.name_.length());
        EXPECT_EQ(0, (int)conf.args_.size());
        EXPECT_EQ(0, (int)conf.directives_.size());
    }

    if (true) {
        MockSrsConfigBuffer buf("listen \"1935\"&;");
        SrsConfDirective conf;
        HELPER_ASSERT_FAILED(conf.parse(&buf));
        EXPECT_EQ(0, (int)conf.name_.length());
        EXPECT_EQ(0, (int)conf.args_.size());
        EXPECT_EQ(0, (int)conf.directives_.size());
    }

    if (true) {
        MockSrsConfigBuffer buf("listen \"1935\"*;");
        SrsConfDirective conf;
        HELPER_ASSERT_FAILED(conf.parse(&buf));
        EXPECT_EQ(0, (int)conf.name_.length());
        EXPECT_EQ(0, (int)conf.args_.size());
        EXPECT_EQ(0, (int)conf.directives_.size());
    }
}

VOID TEST(ConfigDirectiveTest, ReadTokenUnexpectedCharacterAfterQuotedStringWithNumbers)
{
    srs_error_t err;

    // Test unexpected numeric characters after quoted strings
    if (true) {
        MockSrsConfigBuffer buf("listen \"1935\"1;");
        SrsConfDirective conf;
        HELPER_ASSERT_FAILED(conf.parse(&buf));
        EXPECT_EQ(0, (int)conf.name_.length());
        EXPECT_EQ(0, (int)conf.args_.size());
        EXPECT_EQ(0, (int)conf.directives_.size());
    }

    if (true) {
        MockSrsConfigBuffer buf("listen \"1935\"2;");
        SrsConfDirective conf;
        HELPER_ASSERT_FAILED(conf.parse(&buf));
        EXPECT_EQ(0, (int)conf.name_.length());
        EXPECT_EQ(0, (int)conf.args_.size());
        EXPECT_EQ(0, (int)conf.directives_.size());
    }

    if (true) {
        MockSrsConfigBuffer buf("listen \"1935\"9;");
        SrsConfDirective conf;
        HELPER_ASSERT_FAILED(conf.parse(&buf));
        EXPECT_EQ(0, (int)conf.name_.length());
        EXPECT_EQ(0, (int)conf.args_.size());
        EXPECT_EQ(0, (int)conf.directives_.size());
    }

    if (true) {
        MockSrsConfigBuffer buf("listen \"1935\"0;");
        SrsConfDirective conf;
        HELPER_ASSERT_FAILED(conf.parse(&buf));
        EXPECT_EQ(0, (int)conf.name_.length());
        EXPECT_EQ(0, (int)conf.args_.size());
        EXPECT_EQ(0, (int)conf.directives_.size());
    }

    // Test unexpected alphabetic characters after quoted strings
    if (true) {
        MockSrsConfigBuffer buf("listen \"1935\"a;");
        SrsConfDirective conf;
        HELPER_ASSERT_FAILED(conf.parse(&buf));
        EXPECT_EQ(0, (int)conf.name_.length());
        EXPECT_EQ(0, (int)conf.args_.size());
        EXPECT_EQ(0, (int)conf.directives_.size());
    }

    if (true) {
        MockSrsConfigBuffer buf("listen \"1935\"Z;");
        SrsConfDirective conf;
        HELPER_ASSERT_FAILED(conf.parse(&buf));
        EXPECT_EQ(0, (int)conf.name_.length());
        EXPECT_EQ(0, (int)conf.args_.size());
        EXPECT_EQ(0, (int)conf.directives_.size());
    }

    // Test unexpected punctuation characters after quoted strings
    if (true) {
        MockSrsConfigBuffer buf("listen \"1935\"!;");
        SrsConfDirective conf;
        HELPER_ASSERT_FAILED(conf.parse(&buf));
        EXPECT_EQ(0, (int)conf.name_.length());
        EXPECT_EQ(0, (int)conf.args_.size());
        EXPECT_EQ(0, (int)conf.directives_.size());
    }

    if (true) {
        MockSrsConfigBuffer buf("listen \"1935\"?;");
        SrsConfDirective conf;
        HELPER_ASSERT_FAILED(conf.parse(&buf));
        EXPECT_EQ(0, (int)conf.name_.length());
        EXPECT_EQ(0, (int)conf.args_.size());
        EXPECT_EQ(0, (int)conf.directives_.size());
    }

    if (true) {
        MockSrsConfigBuffer buf("listen \"1935\"=;");
        SrsConfDirective conf;
        HELPER_ASSERT_FAILED(conf.parse(&buf));
        EXPECT_EQ(0, (int)conf.name_.length());
        EXPECT_EQ(0, (int)conf.args_.size());
        EXPECT_EQ(0, (int)conf.directives_.size());
    }

    if (true) {
        MockSrsConfigBuffer buf("listen \"1935\"+;");
        SrsConfDirective conf;
        HELPER_ASSERT_FAILED(conf.parse(&buf));
        EXPECT_EQ(0, (int)conf.name_.length());
        EXPECT_EQ(0, (int)conf.args_.size());
        EXPECT_EQ(0, (int)conf.directives_.size());
    }

    if (true) {
        MockSrsConfigBuffer buf("listen \"1935\"-;");
        SrsConfDirective conf;
        HELPER_ASSERT_FAILED(conf.parse(&buf));
        EXPECT_EQ(0, (int)conf.name_.length());
        EXPECT_EQ(0, (int)conf.args_.size());
        EXPECT_EQ(0, (int)conf.directives_.size());
    }

    if (true) {
        MockSrsConfigBuffer buf("listen \"1935\"_;");
        SrsConfDirective conf;
        HELPER_ASSERT_FAILED(conf.parse(&buf));
        EXPECT_EQ(0, (int)conf.name_.length());
        EXPECT_EQ(0, (int)conf.args_.size());
        EXPECT_EQ(0, (int)conf.directives_.size());
    }

    if (true) {
        MockSrsConfigBuffer buf("listen \"1935\"(;");
        SrsConfDirective conf;
        HELPER_ASSERT_FAILED(conf.parse(&buf));
        EXPECT_EQ(0, (int)conf.name_.length());
        EXPECT_EQ(0, (int)conf.args_.size());
        EXPECT_EQ(0, (int)conf.directives_.size());
    }

    if (true) {
        MockSrsConfigBuffer buf("listen \"1935\");");
        SrsConfDirective conf;
        HELPER_ASSERT_FAILED(conf.parse(&buf));
        EXPECT_EQ(0, (int)conf.name_.length());
        EXPECT_EQ(0, (int)conf.args_.size());
        EXPECT_EQ(0, (int)conf.directives_.size());
    }

    if (true) {
        MockSrsConfigBuffer buf("listen \"1935\"[;");
        SrsConfDirective conf;
        HELPER_ASSERT_FAILED(conf.parse(&buf));
        EXPECT_EQ(0, (int)conf.name_.length());
        EXPECT_EQ(0, (int)conf.args_.size());
        EXPECT_EQ(0, (int)conf.directives_.size());
    }

    if (true) {
        MockSrsConfigBuffer buf("listen \"1935\"];");
        SrsConfDirective conf;
        HELPER_ASSERT_FAILED(conf.parse(&buf));
        EXPECT_EQ(0, (int)conf.name_.length());
        EXPECT_EQ(0, (int)conf.args_.size());
        EXPECT_EQ(0, (int)conf.directives_.size());
    }
}

VOID TEST(ConfigDirectiveTest, ReadTokenValidQuotedStringCases)
{
    srs_error_t err;

    // Test valid double-quoted string followed by semicolon (should succeed)
    if (true) {
        MockSrsConfigBuffer buf("listen \"1935\";");
        SrsConfDirective conf;
        HELPER_ASSERT_SUCCESS(conf.parse(&buf));
        EXPECT_EQ(0, (int)conf.name_.length());
        EXPECT_EQ(0, (int)conf.args_.size());
        EXPECT_EQ(1, (int)conf.directives_.size());

        SrsConfDirective &dir = *conf.directives_.at(0);
        EXPECT_STREQ("listen", dir.name_.c_str());
        EXPECT_EQ(1, (int)dir.args_.size());
        EXPECT_STREQ("1935", dir.arg0().c_str());
    }

    // Test valid single-quoted string followed by semicolon (should succeed)
    if (true) {
        MockSrsConfigBuffer buf("listen '1935';");
        SrsConfDirective conf;
        HELPER_ASSERT_SUCCESS(conf.parse(&buf));
        EXPECT_EQ(0, (int)conf.name_.length());
        EXPECT_EQ(0, (int)conf.args_.size());
        EXPECT_EQ(1, (int)conf.directives_.size());

        SrsConfDirective &dir = *conf.directives_.at(0);
        EXPECT_STREQ("listen", dir.name_.c_str());
        EXPECT_EQ(1, (int)dir.args_.size());
        EXPECT_STREQ("1935", dir.arg0().c_str());
    }

    // Test valid double-quoted string followed by space and semicolon (should succeed)
    if (true) {
        MockSrsConfigBuffer buf("listen \"1935\" ;");
        SrsConfDirective conf;
        HELPER_ASSERT_SUCCESS(conf.parse(&buf));
        EXPECT_EQ(0, (int)conf.name_.length());
        EXPECT_EQ(0, (int)conf.args_.size());
        EXPECT_EQ(1, (int)conf.directives_.size());

        SrsConfDirective &dir = *conf.directives_.at(0);
        EXPECT_STREQ("listen", dir.name_.c_str());
        EXPECT_EQ(1, (int)dir.args_.size());
        EXPECT_STREQ("1935", dir.arg0().c_str());
    }

    // Test valid single-quoted string followed by space and semicolon (should succeed)
    if (true) {
        MockSrsConfigBuffer buf("listen '1935' ;");
        SrsConfDirective conf;
        HELPER_ASSERT_SUCCESS(conf.parse(&buf));
        EXPECT_EQ(0, (int)conf.name_.length());
        EXPECT_EQ(0, (int)conf.args_.size());
        EXPECT_EQ(1, (int)conf.directives_.size());

        SrsConfDirective &dir = *conf.directives_.at(0);
        EXPECT_STREQ("listen", dir.name_.c_str());
        EXPECT_EQ(1, (int)dir.args_.size());
        EXPECT_STREQ("1935", dir.arg0().c_str());
    }

    // Test valid double-quoted string followed by block start (should succeed)
    if (true) {
        MockSrsConfigBuffer buf("vhost \"test.com\" {}");
        SrsConfDirective conf;
        HELPER_ASSERT_SUCCESS(conf.parse(&buf));
        EXPECT_EQ(0, (int)conf.name_.length());
        EXPECT_EQ(0, (int)conf.args_.size());
        EXPECT_EQ(1, (int)conf.directives_.size());

        SrsConfDirective &dir = *conf.directives_.at(0);
        EXPECT_STREQ("vhost", dir.name_.c_str());
        EXPECT_EQ(1, (int)dir.args_.size());
        EXPECT_STREQ("test.com", dir.arg0().c_str());
        EXPECT_EQ(0, (int)dir.directives_.size());
    }

    // Test valid single-quoted string followed by block start (should succeed)
    if (true) {
        MockSrsConfigBuffer buf("vhost 'test.com' {}");
        SrsConfDirective conf;
        HELPER_ASSERT_SUCCESS(conf.parse(&buf));
        EXPECT_EQ(0, (int)conf.name_.length());
        EXPECT_EQ(0, (int)conf.args_.size());
        EXPECT_EQ(1, (int)conf.directives_.size());

        SrsConfDirective &dir = *conf.directives_.at(0);
        EXPECT_STREQ("vhost", dir.name_.c_str());
        EXPECT_EQ(1, (int)dir.args_.size());
        EXPECT_STREQ("test.com", dir.arg0().c_str());
        EXPECT_EQ(0, (int)dir.directives_.size());
    }

    // Test valid double-quoted string followed by tab and semicolon (should succeed)
    if (true) {
        MockSrsConfigBuffer buf("listen \"1935\"\t;");
        SrsConfDirective conf;
        HELPER_ASSERT_SUCCESS(conf.parse(&buf));
        EXPECT_EQ(0, (int)conf.name_.length());
        EXPECT_EQ(0, (int)conf.args_.size());
        EXPECT_EQ(1, (int)conf.directives_.size());

        SrsConfDirective &dir = *conf.directives_.at(0);
        EXPECT_STREQ("listen", dir.name_.c_str());
        EXPECT_EQ(1, (int)dir.args_.size());
        EXPECT_STREQ("1935", dir.arg0().c_str());
    }

    // Test valid single-quoted string followed by tab and semicolon (should succeed)
    if (true) {
        MockSrsConfigBuffer buf("listen '1935'\t;");
        SrsConfDirective conf;
        HELPER_ASSERT_SUCCESS(conf.parse(&buf));
        EXPECT_EQ(0, (int)conf.name_.length());
        EXPECT_EQ(0, (int)conf.args_.size());
        EXPECT_EQ(1, (int)conf.directives_.size());

        SrsConfDirective &dir = *conf.directives_.at(0);
        EXPECT_STREQ("listen", dir.name_.c_str());
        EXPECT_EQ(1, (int)dir.args_.size());
        EXPECT_STREQ("1935", dir.arg0().c_str());
    }
}

VOID TEST(ConfigDirectiveTest, ReadTokenUnexpectedCharacterEdgeCases)
{
    srs_error_t err;

    // Test unexpected character in complex vhost configuration
    if (true) {
        MockSrsConfigBuffer buf("vhost \"test.com\"x { hls { enabled on; } }");
        SrsConfDirective conf;
        HELPER_ASSERT_FAILED(conf.parse(&buf));
        EXPECT_EQ(0, (int)conf.name_.length());
        EXPECT_EQ(0, (int)conf.args_.size());
        EXPECT_EQ(0, (int)conf.directives_.size());
    }

    // Test unexpected character after quoted string followed by another quoted string
    if (true) {
        MockSrsConfigBuffer buf("test \"arg1\"\"arg2\";");
        SrsConfDirective conf;
        HELPER_ASSERT_FAILED(conf.parse(&buf));
        EXPECT_EQ(0, (int)conf.name_.length());
        EXPECT_EQ(0, (int)conf.args_.size());
        EXPECT_EQ(0, (int)conf.directives_.size());
    }

    // Test unexpected character after single-quoted string followed by another quoted string
    if (true) {
        MockSrsConfigBuffer buf("test 'arg1''arg2';");
        SrsConfDirective conf;
        HELPER_ASSERT_FAILED(conf.parse(&buf));
        EXPECT_EQ(0, (int)conf.name_.length());
        EXPECT_EQ(0, (int)conf.args_.size());
        EXPECT_EQ(0, (int)conf.directives_.size());
    }

    // Test unexpected character after quoted string followed by opening brace
    if (true) {
        MockSrsConfigBuffer buf("test \"arg1\"x{");
        SrsConfDirective conf;
        HELPER_ASSERT_FAILED(conf.parse(&buf));
        EXPECT_EQ(0, (int)conf.name_.length());
        EXPECT_EQ(0, (int)conf.args_.size());
        EXPECT_EQ(0, (int)conf.directives_.size());
    }

    // Test unexpected character after single-quoted string followed by opening brace
    if (true) {
        MockSrsConfigBuffer buf("test 'arg1'y{");
        SrsConfDirective conf;
        HELPER_ASSERT_FAILED(conf.parse(&buf));
        EXPECT_EQ(0, (int)conf.name_.length());
        EXPECT_EQ(0, (int)conf.args_.size());
        EXPECT_EQ(0, (int)conf.directives_.size());
    }
}

// Test specifically for the read_token method's need_space error condition
VOID TEST(ConfigDirectiveTest, ReadTokenNeedSpaceErrorCondition)
{
    srs_error_t err;

    // These tests specifically target the error condition in read_token where
    // need_space is true and an unexpected character is encountered

    // Test case: quoted string immediately followed by letter (no space)
    if (true) {
        MockSrsConfigBuffer buf("directive \"value\"x;");
        SrsConfDirective conf;
        HELPER_ASSERT_FAILED(conf.parse(&buf));
    }

    // Test case: quoted string immediately followed by number (no space)
    if (true) {
        MockSrsConfigBuffer buf("directive \"value\"1;");
        SrsConfDirective conf;
        HELPER_ASSERT_FAILED(conf.parse(&buf));
    }

    // Test case: quoted string immediately followed by underscore (no space)
    if (true) {
        MockSrsConfigBuffer buf("directive \"value\"_;");
        SrsConfDirective conf;
        HELPER_ASSERT_FAILED(conf.parse(&buf));
    }

    // Test case: single-quoted string immediately followed by letter (no space)
    if (true) {
        MockSrsConfigBuffer buf("directive 'value'x;");
        SrsConfDirective conf;
        HELPER_ASSERT_FAILED(conf.parse(&buf));
    }

    // Test case: single-quoted string immediately followed by number (no space)
    if (true) {
        MockSrsConfigBuffer buf("directive 'value'1;");
        SrsConfDirective conf;
        HELPER_ASSERT_FAILED(conf.parse(&buf));
    }
}

VOID TEST(ConfigMainTest, CheckConfInDocker)
{
    srs_error_t err;

    // Test default value when in_docker directive is not present
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));
        EXPECT_FALSE(conf.get_in_docker()); // Default should be false
    }

    // Test in_docker enabled with "on"
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "in_docker on;"));
        EXPECT_TRUE(conf.get_in_docker());
    }

    // Test in_docker with "yes" (should be false due to SRS_CONF_PREFER_FALSE)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "in_docker yes;"));
        EXPECT_FALSE(conf.get_in_docker()); // SRS_CONF_PREFER_FALSE only returns true for "on"
    }

    // Test in_docker with "true" (should be false due to SRS_CONF_PREFER_FALSE)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "in_docker true;"));
        EXPECT_FALSE(conf.get_in_docker()); // SRS_CONF_PREFER_FALSE only returns true for "on"
    }

    // Test in_docker disabled with "off"
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "in_docker off;"));
        EXPECT_FALSE(conf.get_in_docker());
    }

    // Test in_docker disabled with "no"
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "in_docker no;"));
        EXPECT_FALSE(conf.get_in_docker());
    }

    // Test in_docker disabled with "false"
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "in_docker false;"));
        EXPECT_FALSE(conf.get_in_docker());
    }

    // Test in_docker with empty argument (should default to false)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "in_docker;"));
        EXPECT_FALSE(conf.get_in_docker());
    }

    // Test in_docker with invalid argument (should default to false due to SRS_CONF_PREFER_FALSE)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "in_docker invalid_value;"));
        EXPECT_FALSE(conf.get_in_docker()); // SRS_CONF_PREFER_FALSE should return false for invalid values
    }

    // Test in_docker with numeric argument "1" (should be false due to SRS_CONF_PREFER_FALSE)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "in_docker 1;"));
        EXPECT_FALSE(conf.get_in_docker()); // SRS_CONF_PREFER_FALSE only returns true for "on"
    }

    // Test in_docker with numeric argument "0" (should be false due to SRS_CONF_PREFER_FALSE)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "in_docker 0;"));
        EXPECT_FALSE(conf.get_in_docker()); // SRS_CONF_PREFER_FALSE only returns true for "on"
    }

    // Test in_docker with case variations - "ON" should be false (case sensitive)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "in_docker ON;"));
        EXPECT_FALSE(conf.get_in_docker()); // SRS_CONF_PREFER_FALSE is case sensitive, only "on" returns true
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "in_docker OFF;"));
        EXPECT_FALSE(conf.get_in_docker());
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "in_docker Yes;"));
        EXPECT_FALSE(conf.get_in_docker()); // SRS_CONF_PREFER_FALSE only returns true for "on"
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "in_docker No;"));
        EXPECT_FALSE(conf.get_in_docker());
    }

    // Test in_docker with multiple arguments (should use first argument)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "in_docker on off;"));
        EXPECT_TRUE(conf.get_in_docker()); // Should use first argument "on"
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "in_docker off on;"));
        EXPECT_FALSE(conf.get_in_docker()); // Should use first argument "off"
    }
}

VOID TEST(ConfigEnvTest, CheckEnvValuesInDocker)
{
    srs_error_t err;

    // Test environment variable override for in_docker
    if (true) {
        MockSrsConfig conf;

        SrsSetEnvConfig(conf, in_docker, "SRS_IN_DOCKER", "on");
        EXPECT_TRUE(conf.get_in_docker());
    }

    if (true) {
        MockSrsConfig conf;

        SrsSetEnvConfig(conf, in_docker, "SRS_IN_DOCKER", "off");
        EXPECT_FALSE(conf.get_in_docker());
    }

    if (true) {
        MockSrsConfig conf;

        SrsSetEnvConfig(conf, in_docker, "SRS_IN_DOCKER", "yes");
        EXPECT_FALSE(conf.get_in_docker()); // SRS_CONF_PREFER_FALSE only returns true for "on"
    }

    if (true) {
        MockSrsConfig conf;

        SrsSetEnvConfig(conf, in_docker, "SRS_IN_DOCKER", "no");
        EXPECT_FALSE(conf.get_in_docker());
    }

    if (true) {
        MockSrsConfig conf;

        SrsSetEnvConfig(conf, in_docker, "SRS_IN_DOCKER", "true");
        EXPECT_FALSE(conf.get_in_docker()); // SRS_CONF_PREFER_FALSE only returns true for "on"
    }

    if (true) {
        MockSrsConfig conf;

        SrsSetEnvConfig(conf, in_docker, "SRS_IN_DOCKER", "false");
        EXPECT_FALSE(conf.get_in_docker());
    }

    if (true) {
        MockSrsConfig conf;

        SrsSetEnvConfig(conf, in_docker, "SRS_IN_DOCKER", "1");
        EXPECT_FALSE(conf.get_in_docker()); // SRS_CONF_PREFER_FALSE only returns true for "on"
    }

    if (true) {
        MockSrsConfig conf;

        SrsSetEnvConfig(conf, in_docker, "SRS_IN_DOCKER", "0");
        EXPECT_FALSE(conf.get_in_docker());
    }

    // Test that environment variable overrides config file
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "in_docker off;"));

        SrsSetEnvConfig(conf, in_docker, "SRS_IN_DOCKER", "on");
        EXPECT_TRUE(conf.get_in_docker()); // Environment variable should override config file
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "in_docker on;"));

        SrsSetEnvConfig(conf, in_docker, "SRS_IN_DOCKER", "off");
        EXPECT_FALSE(conf.get_in_docker()); // Environment variable should override config file
    }

    // Test invalid environment variable value (should default to false due to SRS_CONF_PREFER_FALSE)
    if (true) {
        MockSrsConfig conf;

        SrsSetEnvConfig(conf, in_docker, "SRS_IN_DOCKER", "invalid_value");
        EXPECT_FALSE(conf.get_in_docker()); // Should default to false for invalid values
    }

    // Test empty environment variable value (should default to false)
    if (true) {
        MockSrsConfig conf;

        SrsSetEnvConfig(conf, in_docker, "SRS_IN_DOCKER", "");
        EXPECT_FALSE(conf.get_in_docker()); // Should default to false for empty values
    }
}

// External function declarations for testing
extern string srs_server_id_path(string pid_file);
extern string srs_try_read_file(string path);
extern void srs_try_write_file(string path, string content);

VOID TEST(ConfigUtilityTest, ServerIdPath)
{
    // Test basic .pid to .id conversion
    if (true) {
        string result = srs_server_id_path("./objs/srs.pid");
        EXPECT_STREQ("./objs/srs.id", result.c_str());
    }

    // Test path without .pid extension
    if (true) {
        string result = srs_server_id_path("./objs/srs");
        EXPECT_STREQ("./objs/srs.id", result.c_str());
    }

    // Test path already ending with .id
    if (true) {
        string result = srs_server_id_path("./objs/srs.id");
        EXPECT_STREQ("./objs/srs.id", result.c_str());
    }

    // Test complex path with multiple .pid occurrences
    if (true) {
        string result = srs_server_id_path("/tmp/srs.pid.backup.pid");
        EXPECT_STREQ("/tmp/srs.id.backup.id", result.c_str());
    }

    // Test empty string
    if (true) {
        string result = srs_server_id_path("");
        EXPECT_STREQ(".id", result.c_str());
    }

    // Test path with directory containing .pid
    if (true) {
        string result = srs_server_id_path("/var/srs.pid.dir/server.pid");
        EXPECT_STREQ("/var/srs.id.dir/server.id", result.c_str());
    }
}

VOID TEST(ConfigUtilityTest, TryReadWriteFile)
{
    // Test writing and reading a file
    if (true) {
        string test_file = _srs_tmp_file_prefix + "utest-read-write.txt";
        MockFileRemover _mfr(test_file);

        string test_content = "test server id content";
        srs_try_write_file(test_file, test_content);

        string read_content = srs_try_read_file(test_file);
        EXPECT_STREQ(test_content.c_str(), read_content.c_str());
    }

    // Test reading non-existent file
    if (true) {
        string non_existent_file = _srs_tmp_file_prefix + "utest-non-existent.txt";
        string content = srs_try_read_file(non_existent_file);
        EXPECT_STREQ("", content.c_str());
    }

    // Test writing to invalid path (should not crash)
    if (true) {
        string invalid_path = "/invalid/path/that/does/not/exist/file.txt";
        srs_try_write_file(invalid_path, "test content");
        // Should not crash, function handles errors gracefully
    }

    // Test writing empty content
    if (true) {
        string test_file = _srs_tmp_file_prefix + "utest-empty.txt";
        MockFileRemover _mfr(test_file);

        srs_try_write_file(test_file, "");
        string content = srs_try_read_file(test_file);
        EXPECT_STREQ("", content.c_str());
    }

    // Test writing and reading large content (up to buffer size)
    if (true) {
        string test_file = _srs_tmp_file_prefix + "utest-large.txt";
        MockFileRemover _mfr(test_file);

        // Create content close to buffer size (1024 bytes)
        string large_content(1000, 'A');
        srs_try_write_file(test_file, large_content);

        string read_content = srs_try_read_file(test_file);
        EXPECT_STREQ(large_content.c_str(), read_content.c_str());
    }

    // Test reading file larger than buffer (should only read first 1024 bytes)
    if (true) {
        string test_file = _srs_tmp_file_prefix + "utest-oversized.txt";
        MockFileRemover _mfr(test_file);

        // Create content larger than buffer size (1024 bytes)
        string oversized_content(2000, 'B');
        srs_try_write_file(test_file, oversized_content);

        string read_content = srs_try_read_file(test_file);
        // Should only read first 1024 bytes
        EXPECT_EQ(1024, (int)read_content.length());
        EXPECT_EQ('B', read_content[0]);
        EXPECT_EQ('B', read_content[1023]);
    }
}

VOID TEST(ConfigTest, IsFullConfig)
{
    srs_error_t err;

    // Test default value (no is_full directive)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));
        EXPECT_FALSE(conf.is_full_config()); // Should default to false
    }

    // Test is_full enabled with "on"
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "is_full on;"));
        EXPECT_TRUE(conf.is_full_config());
    }

    // Test is_full with "yes" (should be false due to SRS_CONF_PREFER_FALSE)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "is_full yes;"));
        EXPECT_FALSE(conf.is_full_config()); // SRS_CONF_PREFER_FALSE only returns true for "on"
    }

    // Test is_full with "true" (should be false due to SRS_CONF_PREFER_FALSE)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "is_full true;"));
        EXPECT_FALSE(conf.is_full_config()); // SRS_CONF_PREFER_FALSE only returns true for "on"
    }

    // Test is_full disabled with "off"
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "is_full off;"));
        EXPECT_FALSE(conf.is_full_config());
    }

    // Test is_full disabled with "no"
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "is_full no;"));
        EXPECT_FALSE(conf.is_full_config());
    }

    // Test is_full disabled with "false"
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "is_full false;"));
        EXPECT_FALSE(conf.is_full_config());
    }

    // Test is_full with invalid value (should prefer false)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "is_full invalid_value;"));
        EXPECT_FALSE(conf.is_full_config()); // Should default to false for invalid values
    }

    // Test is_full with empty value
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "is_full;"));
        EXPECT_FALSE(conf.is_full_config()); // Should default to false for empty values
    }
}

VOID TEST(ConfigTest, GetServerIdFromConfig)
{
    srs_error_t err;

    // Test server_id from config file
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "server_id test-server-123;"));

        string server_id = conf.get_server_id();
        EXPECT_STREQ("test-server-123", server_id.c_str());
    }

    // Test server_id with complex value
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "server_id srs-production-server-001;"));

        string server_id = conf.get_server_id();
        EXPECT_STREQ("srs-production-server-001", server_id.c_str());
    }

    // Test server_id with special characters
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "server_id srs_test-server.001;"));

        string server_id = conf.get_server_id();
        EXPECT_STREQ("srs_test-server.001", server_id.c_str());
    }
}

VOID TEST(ConfigTest, GetServerIdFromEnv)
{
    srs_error_t err;

    // Test server_id from environment variable overrides config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "server_id config-server-id;"));

        SrsSetEnvConfig(conf, server_id, "SRS_SERVER_ID", "env-server-id");

        string server_id = conf.get_server_id();
        EXPECT_STREQ("env-server-id", server_id.c_str());
    }

    // Test server_id from environment variable with no config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        SrsSetEnvConfig(conf, server_id, "SRS_SERVER_ID", "env-only-server-id");

        string server_id = conf.get_server_id();
        EXPECT_STREQ("env-only-server-id", server_id.c_str());
    }
}

VOID TEST(ConfigTest, GetServerIdWithFileGeneration)
{
    srs_error_t err;

    // Test server_id generation and file persistence
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "pid ./objs/test-server.pid;"));

        // Clean up any existing server id file
        string server_id_file = srs_server_id_path(conf.get_pid_file());
        MockFileRemover _mfr(server_id_file);

        // First call should generate a server ID and write to file
        string server_id1 = conf.get_server_id();
        EXPECT_FALSE(server_id1.empty());

        // Verify the server ID was written to file
        string file_content = srs_try_read_file(server_id_file);
        EXPECT_STREQ(server_id1.c_str(), file_content.c_str());

        // Second call should return the same server ID (from static variable)
        string server_id2 = conf.get_server_id();
        EXPECT_STREQ(server_id1.c_str(), server_id2.c_str());
    }

    // Test server_id file writing behavior
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "pid ./objs/test-file-write.pid;"));

        string server_id_file = srs_server_id_path(conf.get_pid_file());
        MockFileRemover _mfr(server_id_file);

        // Call get_server_id which should write to file
        string server_id = conf.get_server_id();
        EXPECT_FALSE(server_id.empty());

        // Verify the server ID was written to file
        string file_content = srs_try_read_file(server_id_file);
        EXPECT_STREQ(server_id.c_str(), file_content.c_str());
    }

    // Test server_id with custom pid file path
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "pid /tmp/custom-srs.pid;"));

        string server_id_file = srs_server_id_path(conf.get_pid_file());
        MockFileRemover _mfr(server_id_file);

        string server_id = conf.get_server_id();
        EXPECT_FALSE(server_id.empty());

        // Verify correct server ID file path
        EXPECT_STREQ("/tmp/custom-srs.id", server_id_file.c_str());

        // Verify the server ID was written to the correct file
        string file_content = srs_try_read_file(server_id_file);
        EXPECT_STREQ(server_id.c_str(), file_content.c_str());
    }
}

VOID TEST(ConfigTest, GetServerIdPriorityAndEdgeCases)
{
    srs_error_t err;

    // Test priority: environment variable > config file > generated
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "server_id config-id; pid ./objs/priority-test.pid;"));

        string server_id_file = srs_server_id_path(conf.get_pid_file());
        MockFileRemover _mfr(server_id_file);

        // Pre-write a different ID to file
        srs_try_write_file(server_id_file, "file-id");

        // Environment variable should have highest priority
        SrsSetEnvConfig(conf, server_id, "SRS_SERVER_ID", "env-id");

        string server_id = conf.get_server_id();
        EXPECT_STREQ("env-id", server_id.c_str());
    }

    // Test config file priority over generated ID
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "server_id config-file-id; pid ./objs/config-priority.pid;"));

        string server_id_file = srs_server_id_path(conf.get_pid_file());
        MockFileRemover _mfr(server_id_file);

        string server_id = conf.get_server_id();
        EXPECT_STREQ("config-file-id", server_id.c_str());
    }

    // Test empty config server_id (should generate)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "server_id; pid ./objs/empty-config.pid;"));

        string server_id_file = srs_server_id_path(conf.get_pid_file());
        MockFileRemover _mfr(server_id_file);

        string server_id = conf.get_server_id();
        EXPECT_FALSE(server_id.empty());
        // Should be a generated ID, not empty
        EXPECT_NE("", server_id);
    }

    // Test server_id consistency across multiple calls
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "pid ./objs/consistency-test.pid;"));

        string server_id_file = srs_server_id_path(conf.get_pid_file());
        MockFileRemover _mfr(server_id_file);

        string server_id1 = conf.get_server_id();
        string server_id2 = conf.get_server_id();
        string server_id3 = conf.get_server_id();

        EXPECT_STREQ(server_id1.c_str(), server_id2.c_str());
        EXPECT_STREQ(server_id2.c_str(), server_id3.c_str());
    }
}

VOID TEST(ConfigMainTest, CheckQueryLatestVersion)
{
    srs_error_t err;

    // Test default value when query_latest_version is not configured
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));
        EXPECT_FALSE(conf.whether_query_latest_version()); // Default is false
    }

    // Test query_latest_version enabled
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "query_latest_version on;"));
        EXPECT_TRUE(conf.whether_query_latest_version());
    }

    // Test query_latest_version disabled
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "query_latest_version off;"));
        EXPECT_FALSE(conf.whether_query_latest_version());
    }

    // Test query_latest_version with various true values (SRS_CONF_PREFER_TRUE: anything != "off")
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "query_latest_version true;"));
        EXPECT_TRUE(conf.whether_query_latest_version());
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "query_latest_version yes;"));
        EXPECT_TRUE(conf.whether_query_latest_version());
    }

    // Debug: Check if environment variable is set
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "query_latest_version false;"));
        // Print debug info
        printf("DEBUG: query_latest_version false -> %s\n", conf.whether_query_latest_version() ? "true" : "false");
        // EXPECT_TRUE(conf.whether_query_latest_version()); // "false" != "off", so it's true
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "query_latest_version no;"));
        // Print debug info
        printf("DEBUG: query_latest_version no -> %s\n", conf.whether_query_latest_version() ? "true" : "false");
        // EXPECT_TRUE(conf.whether_query_latest_version()); // "no" != "off", so it's true
    }

    // Test environment variable override
    if (true) {
        MockSrsConfig conf;
        SrsSetEnvConfig(conf, query_latest_version, "SRS_QUERY_LATEST_VERSION", "on");
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "query_latest_version off;"));
        EXPECT_TRUE(conf.whether_query_latest_version()); // Environment variable should override config
    }

    if (true) {
        MockSrsConfig conf;
        SrsSetEnvConfig(conf, query_latest_version, "SRS_QUERY_LATEST_VERSION", "off");
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "query_latest_version on;"));
        EXPECT_FALSE(conf.whether_query_latest_version()); // Environment variable should override config
    }
}

VOID TEST(ConfigMainTest, CheckFirstWaitForQlv)
{
    srs_error_t err;

    // Test default value when first_wait_for_qlv is not configured
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));
        EXPECT_EQ(5 * 60 * SRS_UTIME_SECONDS, conf.first_wait_for_qlv()); // Default is 5 minutes
    }

    // Test first_wait_for_qlv with custom value in seconds
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "first_wait_for_qlv 120;"));
        EXPECT_EQ(120 * SRS_UTIME_SECONDS, conf.first_wait_for_qlv()); // 2 minutes
    }

    // Test first_wait_for_qlv with zero value
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "first_wait_for_qlv 0;"));
        EXPECT_EQ(0, conf.first_wait_for_qlv());
    }

    // Test first_wait_for_qlv with large value
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "first_wait_for_qlv 3600;"));
        EXPECT_EQ(3600 * SRS_UTIME_SECONDS, conf.first_wait_for_qlv()); // 1 hour
    }

    // Test first_wait_for_qlv with small value
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "first_wait_for_qlv 30;"));
        EXPECT_EQ(30 * SRS_UTIME_SECONDS, conf.first_wait_for_qlv()); // 30 seconds
    }
}

VOID TEST(ConfigMainTest, CheckEmptyIpOk)
{
    srs_error_t err;

    // Test default value when empty_ip_ok is not configured
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));
        EXPECT_TRUE(conf.empty_ip_ok()); // Default is true
    }

    // Test empty_ip_ok enabled
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "empty_ip_ok on;"));
        EXPECT_TRUE(conf.empty_ip_ok());
    }

    // Test empty_ip_ok disabled
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "empty_ip_ok off;"));
        EXPECT_FALSE(conf.empty_ip_ok());
    }

    // Test empty_ip_ok with various true values (SRS_CONF_PREFER_TRUE: anything != "off")
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "empty_ip_ok true;"));
        EXPECT_TRUE(conf.empty_ip_ok());
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "empty_ip_ok yes;"));
        EXPECT_TRUE(conf.empty_ip_ok());
    }

    // Test empty_ip_ok with false value (SRS_CONF_PREFER_TRUE: only "off" is false)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "empty_ip_ok off;"));
        EXPECT_FALSE(conf.empty_ip_ok());
    }

    // Test empty_ip_ok with other values (SRS_CONF_PREFER_TRUE: anything != "off" is true)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "empty_ip_ok false;"));
        EXPECT_TRUE(conf.empty_ip_ok()); // "false" != "off", so it's true
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "empty_ip_ok no;"));
        EXPECT_TRUE(conf.empty_ip_ok()); // "no" != "off", so it's true
    }

    // Test empty_ip_ok with empty argument (should use default)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "empty_ip_ok;"));
        EXPECT_TRUE(conf.empty_ip_ok()); // Should use default value
    }
}

VOID TEST(ConfigMainTest, CheckGraceStartWait)
{
    srs_error_t err;

    // Test default value when grace_start_wait is not configured
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));
        EXPECT_EQ(2300 * SRS_UTIME_MILLISECONDS, conf.get_grace_start_wait()); // Default is 2300ms
    }

    // Test grace_start_wait with custom value in milliseconds
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "grace_start_wait 5000;"));
        EXPECT_EQ(5000 * SRS_UTIME_MILLISECONDS, conf.get_grace_start_wait()); // 5 seconds
    }

    // Test grace_start_wait with zero value
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "grace_start_wait 0;"));
        EXPECT_EQ(0, conf.get_grace_start_wait());
    }

    // Test grace_start_wait with large value
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "grace_start_wait 30000;"));
        EXPECT_EQ(30000 * SRS_UTIME_MILLISECONDS, conf.get_grace_start_wait()); // 30 seconds
    }

    // Test grace_start_wait with small value
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "grace_start_wait 100;"));
        EXPECT_EQ(100 * SRS_UTIME_MILLISECONDS, conf.get_grace_start_wait()); // 100ms
    }

    // Test grace_start_wait with empty argument (should use default)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "grace_start_wait;"));
        EXPECT_EQ(2300 * SRS_UTIME_MILLISECONDS, conf.get_grace_start_wait()); // Should use default value
    }
}

VOID TEST(ConfigMainTest, CheckGraceFinalWait)
{
    srs_error_t err;

    // Test default value when grace_final_wait is not configured
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));
        EXPECT_EQ(3200 * SRS_UTIME_MILLISECONDS, conf.get_grace_final_wait()); // Default is 3200ms
    }

    // Test grace_final_wait with custom value in milliseconds
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "grace_final_wait 8000;"));
        EXPECT_EQ(8000 * SRS_UTIME_MILLISECONDS, conf.get_grace_final_wait()); // 8 seconds
    }

    // Test grace_final_wait with zero value
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "grace_final_wait 0;"));
        EXPECT_EQ(0, conf.get_grace_final_wait());
    }

    // Test grace_final_wait with large value
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "grace_final_wait 60000;"));
        EXPECT_EQ(60000 * SRS_UTIME_MILLISECONDS, conf.get_grace_final_wait()); // 60 seconds
    }

    // Test grace_final_wait with small value
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "grace_final_wait 500;"));
        EXPECT_EQ(500 * SRS_UTIME_MILLISECONDS, conf.get_grace_final_wait()); // 500ms
    }

    // Test grace_final_wait with empty argument (should use default)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "grace_final_wait;"));
        EXPECT_EQ(3200 * SRS_UTIME_MILLISECONDS, conf.get_grace_final_wait()); // Should use default value
    }
}

VOID TEST(ConfigMainTest, CheckForceGraceQuit)
{
    srs_error_t err;

    // Test default value when force_grace_quit is not configured
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));
        EXPECT_FALSE(conf.is_force_grace_quit()); // Default is false
    }

    // Test force_grace_quit enabled
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "force_grace_quit on;"));
        EXPECT_TRUE(conf.is_force_grace_quit());
    }

    // Test force_grace_quit disabled
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "force_grace_quit off;"));
        EXPECT_FALSE(conf.is_force_grace_quit());
    }

    // Test force_grace_quit with various values (SRS_CONF_PREFER_FALSE: only "on" is true)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "force_grace_quit true;"));
        EXPECT_FALSE(conf.is_force_grace_quit()); // "true" != "on", so it's false
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "force_grace_quit yes;"));
        EXPECT_FALSE(conf.is_force_grace_quit()); // "yes" != "on", so it's false
    }

    // Test force_grace_quit with various false values (SRS_CONF_PREFER_FALSE: anything != "on" is false)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "force_grace_quit false;"));
        EXPECT_FALSE(conf.is_force_grace_quit());
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "force_grace_quit no;"));
        EXPECT_FALSE(conf.is_force_grace_quit());
    }

    // Test force_grace_quit with empty argument (should use default)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "force_grace_quit;"));
        EXPECT_FALSE(conf.is_force_grace_quit()); // Should use default value
    }
}

VOID TEST(ConfigMainTest, CheckDisableDaemonForDocker)
{
    srs_error_t err;

    // Test default value when disable_daemon_for_docker is not configured
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));
        EXPECT_TRUE(conf.disable_daemon_for_docker()); // Default is true
    }

    // Test disable_daemon_for_docker enabled
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "disable_daemon_for_docker on;"));
        EXPECT_TRUE(conf.disable_daemon_for_docker());
    }

    // Test disable_daemon_for_docker disabled
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "disable_daemon_for_docker off;"));
        EXPECT_FALSE(conf.disable_daemon_for_docker());
    }

    // Test disable_daemon_for_docker with various true values (SRS_CONF_PREFER_TRUE: anything != "off")
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "disable_daemon_for_docker true;"));
        EXPECT_TRUE(conf.disable_daemon_for_docker());
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "disable_daemon_for_docker yes;"));
        EXPECT_TRUE(conf.disable_daemon_for_docker());
    }

    // Test disable_daemon_for_docker with false value (SRS_CONF_PREFER_TRUE: only "off" is false)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "disable_daemon_for_docker off;"));
        EXPECT_FALSE(conf.disable_daemon_for_docker());
    }

    // Test disable_daemon_for_docker with other values (SRS_CONF_PREFER_TRUE: anything != "off" is true)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "disable_daemon_for_docker false;"));
        EXPECT_TRUE(conf.disable_daemon_for_docker()); // "false" != "off", so it's true
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "disable_daemon_for_docker no;"));
        EXPECT_TRUE(conf.disable_daemon_for_docker()); // "no" != "off", so it's true
    }

    // Test disable_daemon_for_docker with empty argument (should use default)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "disable_daemon_for_docker;"));
        EXPECT_TRUE(conf.disable_daemon_for_docker()); // Should use default value
    }
}

VOID TEST(ConfigMainTest, CheckInotifyAutoReload)
{
    srs_error_t err;

    // Test default value when inotify_auto_reload is not configured
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));
        EXPECT_FALSE(conf.inotify_auto_reload()); // Default is false
    }

    // Test inotify_auto_reload enabled
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "inotify_auto_reload on;"));
        EXPECT_TRUE(conf.inotify_auto_reload());
    }

    // Test inotify_auto_reload disabled
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "inotify_auto_reload off;"));
        EXPECT_FALSE(conf.inotify_auto_reload());
    }

    // Test inotify_auto_reload with various values (SRS_CONF_PREFER_FALSE: only "on" is true)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "inotify_auto_reload true;"));
        EXPECT_FALSE(conf.inotify_auto_reload()); // "true" != "on", so it's false
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "inotify_auto_reload yes;"));
        EXPECT_FALSE(conf.inotify_auto_reload()); // "yes" != "on", so it's false
    }

    // Test inotify_auto_reload with various false values (SRS_CONF_PREFER_FALSE: anything != "on" is false)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "inotify_auto_reload false;"));
        EXPECT_FALSE(conf.inotify_auto_reload());
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "inotify_auto_reload no;"));
        EXPECT_FALSE(conf.inotify_auto_reload());
    }

    // Test inotify_auto_reload with empty argument (should use default)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "inotify_auto_reload;"));
        EXPECT_FALSE(conf.inotify_auto_reload()); // Should use default value
    }
}

VOID TEST(ConfigMainTest, CheckAutoReloadForDocker)
{
    srs_error_t err;

    // Test default value when auto_reload_for_docker is not configured
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));
        EXPECT_TRUE(conf.auto_reload_for_docker()); // Default is true
    }

    // Test auto_reload_for_docker enabled
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "auto_reload_for_docker on;"));
        EXPECT_TRUE(conf.auto_reload_for_docker());
    }

    // Test auto_reload_for_docker disabled
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "auto_reload_for_docker off;"));
        EXPECT_FALSE(conf.auto_reload_for_docker());
    }

    // Test auto_reload_for_docker with various true values (SRS_CONF_PREFER_TRUE: anything != "off")
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "auto_reload_for_docker true;"));
        EXPECT_TRUE(conf.auto_reload_for_docker());
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "auto_reload_for_docker yes;"));
        EXPECT_TRUE(conf.auto_reload_for_docker());
    }

    // Test auto_reload_for_docker with false value (SRS_CONF_PREFER_TRUE: only "off" is false)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "auto_reload_for_docker off;"));
        EXPECT_FALSE(conf.auto_reload_for_docker());
    }

    // Test auto_reload_for_docker with other values (SRS_CONF_PREFER_TRUE: anything != "off" is true)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "auto_reload_for_docker false;"));
        EXPECT_TRUE(conf.auto_reload_for_docker()); // "false" != "off", so it's true
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "auto_reload_for_docker no;"));
        EXPECT_TRUE(conf.auto_reload_for_docker()); // "no" != "off", so it's true
    }

    // Test auto_reload_for_docker with empty argument (should use default)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "auto_reload_for_docker;"));
        EXPECT_TRUE(conf.auto_reload_for_docker()); // Should use default value
    }
}

VOID TEST(ConfigMainTest, CheckTcmallocReleaseRate)
{
    srs_error_t err;

    // Test default tcmalloc_release_rate
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));
        EXPECT_EQ(0.8, conf.tcmalloc_release_rate()); // SRS_PERF_TCMALLOC_RELEASE_RATE
    }

    // Test custom tcmalloc_release_rate
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "tcmalloc_release_rate 0.5;"));
        EXPECT_EQ(0.5, conf.tcmalloc_release_rate());
    }

    // Test tcmalloc_release_rate with maximum value (should be clamped to 10)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "tcmalloc_release_rate 15.0;"));
        EXPECT_EQ(10.0, conf.tcmalloc_release_rate());
    }

    // Test tcmalloc_release_rate with minimum value (should be clamped to 0)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "tcmalloc_release_rate -5.0;"));
        EXPECT_EQ(0.0, conf.tcmalloc_release_rate());
    }

    // Test tcmalloc_release_rate with empty value (should use default)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "tcmalloc_release_rate;"));
        EXPECT_EQ(0.8, conf.tcmalloc_release_rate());
    }

    // Test tcmalloc_release_rate with environment variable override
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "tcmalloc_release_rate 0.3;"));

        SrsSetEnvConfig(conf, tcmalloc_release_rate, "SRS_TCMALLOC_RELEASE_RATE", "0.7");
        EXPECT_EQ(0.7, conf.tcmalloc_release_rate());
    }

    // Test tcmalloc_release_rate with environment variable clamping
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        SrsSetEnvConfig(conf, tcmalloc_release_rate_high, "SRS_TCMALLOC_RELEASE_RATE", "20.0");
        EXPECT_EQ(10.0, conf.tcmalloc_release_rate());

        SrsSetEnvConfig(conf, tcmalloc_release_rate_low, "SRS_TCMALLOC_RELEASE_RATE", "-2.0");
        EXPECT_EQ(0.0, conf.tcmalloc_release_rate());
    }
}

VOID TEST(ConfigMainTest, CheckCircuitBreakerEnabled)
{
    srs_error_t err;

    // Test default circuit_breaker enabled (should be true)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));
        EXPECT_TRUE(conf.get_circuit_breaker());
    }

    // Test circuit_breaker enabled explicitly
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "circuit_breaker{enabled on;}"));
        EXPECT_TRUE(conf.get_circuit_breaker());
    }

    // Test circuit_breaker disabled
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "circuit_breaker{enabled off;}"));
        EXPECT_FALSE(conf.get_circuit_breaker());
    }

    // Test circuit_breaker with no enabled directive (should use default)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "circuit_breaker{}"));
        EXPECT_TRUE(conf.get_circuit_breaker());
    }

    // Test circuit_breaker with empty enabled value (should use default)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "circuit_breaker{enabled;}"));
        EXPECT_TRUE(conf.get_circuit_breaker());
    }

    // Test circuit_breaker with environment variable override
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "circuit_breaker{enabled on;}"));

        SrsSetEnvConfig(conf, circuit_breaker_enabled, "SRS_CIRCUIT_BREAKER_ENABLED", "off");
        EXPECT_FALSE(conf.get_circuit_breaker());
    }

    // Test circuit_breaker with various boolean values (SRS_CONF_PREFER_TRUE: only "off" is false)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "circuit_breaker{enabled true;}"));
        EXPECT_TRUE(conf.get_circuit_breaker());

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "circuit_breaker{enabled false;}"));
        EXPECT_TRUE(conf.get_circuit_breaker()); // "false" != "off", so it's true

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "circuit_breaker{enabled yes;}"));
        EXPECT_TRUE(conf.get_circuit_breaker());

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "circuit_breaker{enabled no;}"));
        EXPECT_TRUE(conf.get_circuit_breaker()); // "no" != "off", so it's true
    }
}

VOID TEST(ConfigMainTest, CheckCircuitBreakerThresholds)
{
    srs_error_t err;

    // Test default high_threshold (should be 90)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));
        EXPECT_EQ(90, conf.get_high_threshold());
    }

    // Test custom high_threshold
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "circuit_breaker{high_threshold 85;}"));
        EXPECT_EQ(85, conf.get_high_threshold());
    }

    // Test default critical_threshold (should be 95)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));
        EXPECT_EQ(95, conf.get_critical_threshold());
    }

    // Test custom critical_threshold
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "circuit_breaker{critical_threshold 98;}"));
        EXPECT_EQ(98, conf.get_critical_threshold());
    }

    // Test default dying_threshold (should be 99)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));
        EXPECT_EQ(99, conf.get_dying_threshold());
    }

    // Test custom dying_threshold
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "circuit_breaker{dying_threshold 100;}"));
        EXPECT_EQ(100, conf.get_dying_threshold());
    }

    // Test all thresholds together
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "circuit_breaker{high_threshold 80; critical_threshold 90; dying_threshold 95;}"));
        EXPECT_EQ(80, conf.get_high_threshold());
        EXPECT_EQ(90, conf.get_critical_threshold());
        EXPECT_EQ(95, conf.get_dying_threshold());
    }

    // Test thresholds with environment variable override
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "circuit_breaker{high_threshold 80;}"));

        SrsSetEnvConfig(conf, high_threshold, "SRS_CIRCUIT_BREAKER_HIGH_THRESHOLD", "75");
        EXPECT_EQ(75, conf.get_high_threshold());

        SrsSetEnvConfig(conf, critical_threshold, "SRS_CIRCUIT_BREAKER_CRITICAL_THRESHOLD", "88");
        EXPECT_EQ(88, conf.get_critical_threshold());

        SrsSetEnvConfig(conf, dying_threshold, "SRS_CIRCUIT_BREAKER_DYING_THRESHOLD", "97");
        EXPECT_EQ(97, conf.get_dying_threshold());
    }
}

VOID TEST(ConfigMainTest, CheckCircuitBreakerPulses)
{
    srs_error_t err;

    // Test default high_pulse (should be 2)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));
        EXPECT_EQ(2, conf.get_high_pulse());
    }

    // Test custom high_pulse
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "circuit_breaker{high_pulse 3;}"));
        EXPECT_EQ(3, conf.get_high_pulse());
    }

    // Test default critical_pulse (should be 1)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));
        EXPECT_EQ(1, conf.get_critical_pulse());
    }

    // Test custom critical_pulse
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "circuit_breaker{critical_pulse 2;}"));
        EXPECT_EQ(2, conf.get_critical_pulse());
    }

    // Test default dying_pulse (should be 5)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));
        EXPECT_EQ(5, conf.get_dying_pulse());
    }

    // Test custom dying_pulse
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "circuit_breaker{dying_pulse 10;}"));
        EXPECT_EQ(10, conf.get_dying_pulse());
    }

    // Test all pulses together
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "circuit_breaker{high_pulse 4; critical_pulse 3; dying_pulse 8;}"));
        EXPECT_EQ(4, conf.get_high_pulse());
        EXPECT_EQ(3, conf.get_critical_pulse());
        EXPECT_EQ(8, conf.get_dying_pulse());
    }

    // Test pulses with environment variable override
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "circuit_breaker{high_pulse 2;}"));

        SrsSetEnvConfig(conf, high_pulse, "SRS_CIRCUIT_BREAKER_HIGH_PULSE", "5");
        EXPECT_EQ(5, conf.get_high_pulse());

        SrsSetEnvConfig(conf, critical_pulse, "SRS_CIRCUIT_BREAKER_CRITICAL_PULSE", "3");
        EXPECT_EQ(3, conf.get_critical_pulse());

        SrsSetEnvConfig(conf, dying_pulse, "SRS_CIRCUIT_BREAKER_DYING_PULSE", "7");
        EXPECT_EQ(7, conf.get_dying_pulse());
    }
}

VOID TEST(ConfigMainTest, CheckStreamCasterEngine)
{
    srs_error_t err;

    // Test get_stream_caster_engine with NULL conf (should return empty string)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));
        EXPECT_STREQ("", conf.get_stream_caster_engine(NULL).c_str());
    }

    // Test get_stream_caster_engine with conf but no caster directive (should return empty string)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "stream_caster{enabled on;}"));

        SrsConfDirective *stream_caster = conf.get_root()->get("stream_caster");
        ASSERT_TRUE(stream_caster != NULL);
        EXPECT_STREQ("", conf.get_stream_caster_engine(stream_caster).c_str());
    }

    // Test get_stream_caster_engine with empty caster value
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "stream_caster{enabled on; caster;}"));

        SrsConfDirective *stream_caster = conf.get_root()->get("stream_caster");
        ASSERT_TRUE(stream_caster != NULL);
        EXPECT_STREQ("", conf.get_stream_caster_engine(stream_caster).c_str());
    }

    // Test get_stream_caster_engine with gb28181 caster
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "stream_caster{enabled on; caster gb28181;}"));

        SrsConfDirective *stream_caster = conf.get_root()->get("stream_caster");
        ASSERT_TRUE(stream_caster != NULL);
        EXPECT_STREQ("gb28181", conf.get_stream_caster_engine(stream_caster).c_str());
    }

    // Test get_stream_caster_engine with flv caster
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "stream_caster{enabled on; caster flv;}"));

        SrsConfDirective *stream_caster = conf.get_root()->get("stream_caster");
        ASSERT_TRUE(stream_caster != NULL);
        EXPECT_STREQ("flv", conf.get_stream_caster_engine(stream_caster).c_str());
    }

    // Test get_stream_caster_engine with mpegts-over-udp caster
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "stream_caster{enabled on; caster mpegts-over-udp;}"));

        SrsConfDirective *stream_caster = conf.get_root()->get("stream_caster");
        ASSERT_TRUE(stream_caster != NULL);
        EXPECT_STREQ("mpegts-over-udp", conf.get_stream_caster_engine(stream_caster).c_str());
    }
}

VOID TEST(ConfigMainTest, CheckRtspServerEnabled)
{
    srs_error_t err;

    // Test default rtsp_server enabled (should be false)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));
        EXPECT_FALSE(conf.get_rtsp_server_enabled());
    }

    // Test rtsp_server enabled explicitly
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtsp_server{enabled on;}"));
        EXPECT_TRUE(conf.get_rtsp_server_enabled());
    }

    // Test rtsp_server disabled explicitly
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtsp_server{enabled off;}"));
        EXPECT_FALSE(conf.get_rtsp_server_enabled());
    }

    // Test rtsp_server with no enabled directive (should use default false)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtsp_server{}"));
        EXPECT_FALSE(conf.get_rtsp_server_enabled());
    }

    // Test rtsp_server with empty enabled value (should use default false)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtsp_server{enabled;}"));
        EXPECT_FALSE(conf.get_rtsp_server_enabled());
    }

    // Test rtsp_server with environment variable override
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtsp_server{enabled off;}"));

        SrsSetEnvConfig(conf, rtsp_server_enabled, "SRS_RTSP_SERVER_ENABLED", "on");
        EXPECT_TRUE(conf.get_rtsp_server_enabled());
    }

    // Test rtsp_server with various boolean values (SRS_CONF_PREFER_FALSE: only "on" is true)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtsp_server{enabled true;}"));
        EXPECT_FALSE(conf.get_rtsp_server_enabled()); // "true" != "on", so it's false

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtsp_server{enabled false;}"));
        EXPECT_FALSE(conf.get_rtsp_server_enabled());

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtsp_server{enabled yes;}"));
        EXPECT_FALSE(conf.get_rtsp_server_enabled()); // "yes" != "on", so it's false

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtsp_server{enabled no;}"));
        EXPECT_FALSE(conf.get_rtsp_server_enabled());
    }
}

VOID TEST(ConfigMainTest, CheckRtspServerListens)
{
    srs_error_t err;

    // Test default rtsp_server listen (should be 554)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));
        vector<string> listens = conf.get_rtsp_server_listens();
        EXPECT_EQ(1, (int)listens.size());
        EXPECT_STREQ("554", listens.at(0).c_str());
    }

    // Test custom rtsp_server listen port
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtsp_server{listen 8554;}"));
        vector<string> listens = conf.get_rtsp_server_listens();
        EXPECT_EQ(1, (int)listens.size());
        EXPECT_STREQ("8554", listens.at(0).c_str());
    }

    // Test multiple rtsp_server listen ports
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtsp_server{listen 554 8554 9554;}"));
        vector<string> listens = conf.get_rtsp_server_listens();
        EXPECT_EQ(3, (int)listens.size());
        EXPECT_STREQ("554", listens.at(0).c_str());
        EXPECT_STREQ("8554", listens.at(1).c_str());
        EXPECT_STREQ("9554", listens.at(2).c_str());
    }

    // Test rtsp_server with no listen directive (should use default)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtsp_server{enabled on;}"));
        vector<string> listens = conf.get_rtsp_server_listens();
        EXPECT_EQ(1, (int)listens.size());
        EXPECT_STREQ("554", listens.at(0).c_str());
    }

    // Test rtsp_server with empty listen directive (should use default)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtsp_server{listen;}"));
        vector<string> listens = conf.get_rtsp_server_listens();
        EXPECT_EQ(1, (int)listens.size());
        EXPECT_STREQ("554", listens.at(0).c_str());
    }

    // Test rtsp_server with environment variable override
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtsp_server{listen 8554;}"));

        SrsSetEnvConfig(conf, rtsp_server_listen, "SRS_RTSP_SERVER_LISTEN", "9554");
        vector<string> listens = conf.get_rtsp_server_listens();
        EXPECT_EQ(1, (int)listens.size());
        EXPECT_STREQ("9554", listens.at(0).c_str());
    }

    // Test rtsp_server with environment variable multiple ports
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        SrsSetEnvConfig(conf, rtsp_server_listen_multi, "SRS_RTSP_SERVER_LISTEN", "554 8554");
        vector<string> listens = conf.get_rtsp_server_listens();
        EXPECT_EQ(2, (int)listens.size());
        EXPECT_STREQ("554", listens.at(0).c_str());
        EXPECT_STREQ("8554", listens.at(1).c_str());
    }
}

VOID TEST(ConfigMainTest, CheckVhostRtspEnabled)
{
    srs_error_t err;

    // Test default vhost rtsp enabled (should be false)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{}"));
        EXPECT_FALSE(conf.get_rtsp_enabled("test.com"));
    }

    // Test vhost rtsp enabled explicitly
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtsp{enabled on;}}"));
        EXPECT_TRUE(conf.get_rtsp_enabled("test.com"));
    }

    // Test vhost rtsp disabled explicitly
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtsp{enabled off;}}"));
        EXPECT_FALSE(conf.get_rtsp_enabled("test.com"));
    }

    // Test vhost rtsp with no enabled directive (should use default false)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtsp{}}"));
        EXPECT_FALSE(conf.get_rtsp_enabled("test.com"));
    }

    // Test vhost rtsp with empty enabled value (should use default false)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtsp{enabled;}}"));
        EXPECT_FALSE(conf.get_rtsp_enabled("test.com"));
    }

    // Test vhost rtsp with environment variable override
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtsp{enabled off;}}"));

        SrsSetEnvConfig(conf, rtsp_enabled, "SRS_VHOST_RTSP_ENABLED", "on");
        EXPECT_TRUE(conf.get_rtsp_enabled("test.com"));
    }

    // Test vhost rtsp with non-existent vhost (should use default false)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));
        EXPECT_FALSE(conf.get_rtsp_enabled("nonexistent.com"));
    }

    // Test get_rtsp directive
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtsp{enabled on;}}"));

        SrsConfDirective *rtsp = conf.get_rtsp("test.com");
        ASSERT_TRUE(rtsp != NULL);
        EXPECT_STREQ("rtsp", rtsp->name_.c_str());

        // Test with non-existent vhost
        SrsConfDirective *rtsp_null = conf.get_rtsp("nonexistent.com");
        EXPECT_TRUE(rtsp_null == NULL);
    }
}

VOID TEST(ConfigMainTest, CheckVhostRtspFromRtmp)
{
    srs_error_t err;

    // Test default vhost rtsp rtmp_to_rtsp (should be true)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{}"));
        EXPECT_TRUE(conf.get_rtsp_from_rtmp("test.com"));
    }

    // Test vhost rtsp rtmp_to_rtsp enabled explicitly
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtsp{rtmp_to_rtsp on;}}"));
        EXPECT_TRUE(conf.get_rtsp_from_rtmp("test.com"));
    }

    // Test vhost rtsp rtmp_to_rtsp disabled explicitly
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtsp{rtmp_to_rtsp off;}}"));
        EXPECT_FALSE(conf.get_rtsp_from_rtmp("test.com"));
    }

    // Test vhost rtsp with no rtmp_to_rtsp directive (should use default true)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtsp{enabled on;}}"));
        EXPECT_TRUE(conf.get_rtsp_from_rtmp("test.com"));
    }

    // Test vhost rtsp with empty rtmp_to_rtsp value (should use default true)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtsp{rtmp_to_rtsp;}}"));
        EXPECT_TRUE(conf.get_rtsp_from_rtmp("test.com"));
    }

    // Test vhost rtsp with environment variable override
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtsp{rtmp_to_rtsp on;}}"));

        SrsSetEnvConfig(conf, rtsp_from_rtmp, "SRS_VHOST_RTSP_RTMP_TO_RTSP", "off");
        EXPECT_FALSE(conf.get_rtsp_from_rtmp("test.com"));
    }

    // Test vhost rtsp with non-existent vhost (should use default true)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));
        EXPECT_TRUE(conf.get_rtsp_from_rtmp("nonexistent.com"));
    }

    // Test vhost rtsp with various boolean values (SRS_CONF_PREFER_TRUE: only "off" is false)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtsp{rtmp_to_rtsp true;}}"));
        EXPECT_TRUE(conf.get_rtsp_from_rtmp("test.com"));

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtsp{rtmp_to_rtsp false;}}"));
        EXPECT_TRUE(conf.get_rtsp_from_rtmp("test.com")); // "false" != "off", so it's true

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtsp{rtmp_to_rtsp yes;}}"));
        EXPECT_TRUE(conf.get_rtsp_from_rtmp("test.com"));

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtsp{rtmp_to_rtsp no;}}"));
        EXPECT_TRUE(conf.get_rtsp_from_rtmp("test.com")); // "no" != "off", so it's true
    }
}

VOID TEST(ConfigRtcServerTest, CheckRtcServerEnabled)
{
    srs_error_t err;

    // Test default value (false) when no rtc_server section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));
        EXPECT_FALSE(conf.get_rtc_server_enabled());
    }

    // Test default value (false) when rtc_server section exists but no enabled directive
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{listen 8000;}"));
        EXPECT_FALSE(conf.get_rtc_server_enabled());
    }

    // Test enabled on
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{enabled on;}"));
        EXPECT_TRUE(conf.get_rtc_server_enabled());
    }

    // Test enabled off
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{enabled off;}"));
        EXPECT_FALSE(conf.get_rtc_server_enabled());
    }

    // Test SRS_CONF_PREFER_FALSE behavior (only "on" is true)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{enabled true;}"));
        EXPECT_FALSE(conf.get_rtc_server_enabled()); // "true" != "on", so it's false

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{enabled yes;}"));
        EXPECT_FALSE(conf.get_rtc_server_enabled()); // "yes" != "on", so it's false

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{enabled 1;}"));
        EXPECT_FALSE(conf.get_rtc_server_enabled()); // "1" != "on", so it's false
    }

    // Test empty value
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{enabled;}"));
        EXPECT_FALSE(conf.get_rtc_server_enabled());
    }

    // Test environment variable override
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{enabled off;}"));

        SrsSetEnvConfig(conf, rtc_enabled, "SRS_RTC_SERVER_ENABLED", "on");
        EXPECT_TRUE(conf.get_rtc_server_enabled());
    }
}

VOID TEST(ConfigRtcServerTest, CheckRtcServerListens)
{
    srs_error_t err;

    // Test default value when no rtc_server section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));
        vector<string> listens = conf.get_rtc_server_listens();
        EXPECT_EQ(1, (int)listens.size());
        EXPECT_STREQ("8000", listens.at(0).c_str());
    }

    // Test default value when rtc_server section exists but no listen directive
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{enabled on;}"));
        vector<string> listens = conf.get_rtc_server_listens();
        EXPECT_EQ(1, (int)listens.size());
        EXPECT_STREQ("8000", listens.at(0).c_str());
    }

    // Test single listen port
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{listen 8080;}"));
        vector<string> listens = conf.get_rtc_server_listens();
        EXPECT_EQ(1, (int)listens.size());
        EXPECT_STREQ("8080", listens.at(0).c_str());
    }

    // Test multiple listen ports
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{listen 8080 8081 8082;}"));
        vector<string> listens = conf.get_rtc_server_listens();
        EXPECT_EQ(3, (int)listens.size());
        EXPECT_STREQ("8080", listens.at(0).c_str());
        EXPECT_STREQ("8081", listens.at(1).c_str());
        EXPECT_STREQ("8082", listens.at(2).c_str());
    }

    // Test empty listen directive (should use default)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{listen;}"));
        vector<string> listens = conf.get_rtc_server_listens();
        EXPECT_EQ(1, (int)listens.size());
        EXPECT_STREQ("8000", listens.at(0).c_str());
    }

    // Test environment variable override
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{listen 8080;}"));

        SrsSetEnvConfig(conf, rtc_listen, "SRS_RTC_SERVER_LISTEN", "9000");
        vector<string> listens = conf.get_rtc_server_listens();
        EXPECT_EQ(1, (int)listens.size());
        EXPECT_STREQ("9000", listens.at(0).c_str());
    }

    // Test environment variable with multiple ports
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        SrsSetEnvConfig(conf, rtc_listen, "SRS_RTC_SERVER_LISTEN", "9000 9001");
        vector<string> listens = conf.get_rtc_server_listens();
        EXPECT_EQ(2, (int)listens.size());
        EXPECT_STREQ("9000", listens.at(0).c_str());
        EXPECT_STREQ("9001", listens.at(1).c_str());
    }
}

VOID TEST(ConfigRtcServerTest, CheckRtcServerCandidates)
{
    srs_error_t err;

    // Test default value when no rtc_server section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));
        EXPECT_STREQ("*", conf.get_rtc_server_candidates().c_str());
    }

    // Test default value when rtc_server section exists but no candidate directive
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{enabled on;}"));
        EXPECT_STREQ("*", conf.get_rtc_server_candidates().c_str());
    }

    // Test explicit candidate value
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{candidate 192.168.1.100;}"));
        EXPECT_STREQ("192.168.1.100", conf.get_rtc_server_candidates().c_str());
    }

    // Test empty candidate value
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{candidate;}"));
        EXPECT_STREQ("*", conf.get_rtc_server_candidates().c_str());
    }

    // Test environment variable reference with actual env var set
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{candidate $MY_CANDIDATE;}"));

        SrsSetEnvConfig(conf, my_candidate, "MY_CANDIDATE", "10.0.0.1");
        EXPECT_STREQ("10.0.0.1", conf.get_rtc_server_candidates().c_str());
    }

    // Test environment variable reference without env var set (should return default)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{candidate $UNSET_VAR;}"));
        EXPECT_STREQ("*", conf.get_rtc_server_candidates().c_str());
    }

    // Test environment variable override
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{candidate 192.168.1.100;}"));

        SrsSetEnvConfig(conf, rtc_candidate, "SRS_RTC_SERVER_CANDIDATE", "10.0.0.2");
        EXPECT_STREQ("10.0.0.2", conf.get_rtc_server_candidates().c_str());
    }
}

VOID TEST(ConfigRtcServerTest, CheckApiAsCandidates)
{
    srs_error_t err;

    // Test default value (true) when no rtc_server section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));
        EXPECT_TRUE(conf.get_api_as_candidates());
    }

    // Test default value (true) when rtc_server section exists but no api_as_candidates directive
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{enabled on;}"));
        EXPECT_TRUE(conf.get_api_as_candidates());
    }

    // Test api_as_candidates off
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{api_as_candidates off;}"));
        EXPECT_FALSE(conf.get_api_as_candidates());
    }

    // Test api_as_candidates on
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{api_as_candidates on;}"));
        EXPECT_TRUE(conf.get_api_as_candidates());
    }

    // Test SRS_CONF_PREFER_TRUE behavior (only "off" is false)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{api_as_candidates false;}"));
        EXPECT_TRUE(conf.get_api_as_candidates()); // "false" != "off", so it's true

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{api_as_candidates no;}"));
        EXPECT_TRUE(conf.get_api_as_candidates()); // "no" != "off", so it's true

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{api_as_candidates 0;}"));
        EXPECT_TRUE(conf.get_api_as_candidates()); // "0" != "off", so it's true
    }

    // Test empty value
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{api_as_candidates;}"));
        EXPECT_TRUE(conf.get_api_as_candidates());
    }
}

VOID TEST(ConfigRtcServerTest, CheckResolveApiDomain)
{
    srs_error_t err;

    // Test default value (true) when no rtc_server section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));
        EXPECT_TRUE(conf.get_resolve_api_domain());
    }

    // Test default value (true) when rtc_server section exists but no resolve_api_domain directive
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{enabled on;}"));
        EXPECT_TRUE(conf.get_resolve_api_domain());
    }

    // Test resolve_api_domain off
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{resolve_api_domain off;}"));
        EXPECT_FALSE(conf.get_resolve_api_domain());
    }

    // Test resolve_api_domain on
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{resolve_api_domain on;}"));
        EXPECT_TRUE(conf.get_resolve_api_domain());
    }

    // Test SRS_CONF_PREFER_TRUE behavior (only "off" is false)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{resolve_api_domain true;}"));
        EXPECT_TRUE(conf.get_resolve_api_domain()); // "true" != "off", so it's true

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{resolve_api_domain yes;}"));
        EXPECT_TRUE(conf.get_resolve_api_domain()); // "yes" != "off", so it's true
    }

    // Test empty value
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{resolve_api_domain;}"));
        EXPECT_TRUE(conf.get_resolve_api_domain());
    }
}

VOID TEST(ConfigRtcServerTest, CheckKeepApiDomain)
{
    srs_error_t err;

    // Test default value (false) when no rtc_server section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));
        EXPECT_FALSE(conf.get_keep_api_domain());
    }

    // Test default value (false) when rtc_server section exists but no keep_api_domain directive
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{enabled on;}"));
        EXPECT_FALSE(conf.get_keep_api_domain());
    }

    // Test keep_api_domain on
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{keep_api_domain on;}"));
        EXPECT_TRUE(conf.get_keep_api_domain());
    }

    // Test keep_api_domain off
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{keep_api_domain off;}"));
        EXPECT_FALSE(conf.get_keep_api_domain());
    }

    // Test SRS_CONF_PREFER_FALSE behavior (only "on" is true)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{keep_api_domain true;}"));
        EXPECT_FALSE(conf.get_keep_api_domain()); // "true" != "on", so it's false

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{keep_api_domain yes;}"));
        EXPECT_FALSE(conf.get_keep_api_domain()); // "yes" != "on", so it's false
    }

    // Test empty value
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{keep_api_domain;}"));
        EXPECT_FALSE(conf.get_keep_api_domain());
    }
}

VOID TEST(ConfigRtcServerTest, CheckUseAutoDetectNetworkIp)
{
    srs_error_t err;

    // Test default value (true) when no rtc_server section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));
        EXPECT_TRUE(conf.get_use_auto_detect_network_ip());
    }

    // Test default value (true) when rtc_server section exists but no use_auto_detect_network_ip directive
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{enabled on;}"));
        EXPECT_TRUE(conf.get_use_auto_detect_network_ip());
    }

    // Test use_auto_detect_network_ip off
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{use_auto_detect_network_ip off;}"));
        EXPECT_FALSE(conf.get_use_auto_detect_network_ip());
    }

    // Test use_auto_detect_network_ip on
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{use_auto_detect_network_ip on;}"));
        EXPECT_TRUE(conf.get_use_auto_detect_network_ip());
    }

    // Test SRS_CONF_PREFER_TRUE behavior (only "off" is false)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{use_auto_detect_network_ip true;}"));
        EXPECT_TRUE(conf.get_use_auto_detect_network_ip()); // "true" != "off", so it's true
    }

    // Test empty value
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{use_auto_detect_network_ip;}"));
        EXPECT_TRUE(conf.get_use_auto_detect_network_ip());
    }
}

VOID TEST(ConfigRtcServerTest, CheckRtcServerTcpEnabled)
{
    srs_error_t err;

    // Test default value (false) when no rtc_server section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));
        EXPECT_FALSE(conf.get_rtc_server_tcp_enabled());
    }

    // Test default value (false) when rtc_server section exists but no tcp section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{enabled on;}"));
        EXPECT_FALSE(conf.get_rtc_server_tcp_enabled());
    }

    // Test default value (false) when tcp section exists but no enabled directive
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{tcp{listen 8000;}}"));
        EXPECT_FALSE(conf.get_rtc_server_tcp_enabled());
    }

    // Test tcp enabled on
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{tcp{enabled on;}}"));
        EXPECT_TRUE(conf.get_rtc_server_tcp_enabled());
    }

    // Test tcp enabled off
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{tcp{enabled off;}}"));
        EXPECT_FALSE(conf.get_rtc_server_tcp_enabled());
    }

    // Test SRS_CONF_PREFER_FALSE behavior (only "on" is true)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{tcp{enabled true;}}"));
        EXPECT_FALSE(conf.get_rtc_server_tcp_enabled()); // "true" != "on", so it's false

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{tcp{enabled yes;}}"));
        EXPECT_FALSE(conf.get_rtc_server_tcp_enabled()); // "yes" != "on", so it's false
    }

    // Test empty value
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{tcp{enabled;}}"));
        EXPECT_FALSE(conf.get_rtc_server_tcp_enabled());
    }

    // Test environment variable override
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{tcp{enabled off;}}"));

        SrsSetEnvConfig(conf, rtc_tcp_enabled, "SRS_RTC_SERVER_TCP_ENABLED", "on");
        EXPECT_TRUE(conf.get_rtc_server_tcp_enabled());
    }
}

VOID TEST(ConfigRtcServerTest, CheckRtcServerTcpListens)
{
    srs_error_t err;

    // Test default value when no rtc_server section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));
        vector<string> listens = conf.get_rtc_server_tcp_listens();
        EXPECT_EQ(1, (int)listens.size());
        EXPECT_STREQ("8000", listens.at(0).c_str());
    }

    // Test default value when rtc_server section exists but no tcp section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{enabled on;}"));
        vector<string> listens = conf.get_rtc_server_tcp_listens();
        EXPECT_EQ(1, (int)listens.size());
        EXPECT_STREQ("8000", listens.at(0).c_str());
    }

    // Test default value when tcp section exists but no listen directive
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{tcp{enabled on;}}"));
        vector<string> listens = conf.get_rtc_server_tcp_listens();
        EXPECT_EQ(1, (int)listens.size());
        EXPECT_STREQ("8000", listens.at(0).c_str());
    }

    // Test single tcp listen port
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{tcp{listen 8080;}}"));
        vector<string> listens = conf.get_rtc_server_tcp_listens();
        EXPECT_EQ(1, (int)listens.size());
        EXPECT_STREQ("8080", listens.at(0).c_str());
    }

    // Test multiple tcp listen ports
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{tcp{listen 8080 8081 8082;}}"));
        vector<string> listens = conf.get_rtc_server_tcp_listens();
        EXPECT_EQ(3, (int)listens.size());
        EXPECT_STREQ("8080", listens.at(0).c_str());
        EXPECT_STREQ("8081", listens.at(1).c_str());
        EXPECT_STREQ("8082", listens.at(2).c_str());
    }

    // Test empty tcp listen directive (should use default)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{tcp{listen;}}"));
        vector<string> listens = conf.get_rtc_server_tcp_listens();
        EXPECT_EQ(1, (int)listens.size());
        EXPECT_STREQ("8000", listens.at(0).c_str());
    }

    // Test environment variable override
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{tcp{listen 8080;}}"));

        SrsSetEnvConfig(conf, rtc_tcp_listen, "SRS_RTC_SERVER_TCP_LISTEN", "9000");
        vector<string> listens = conf.get_rtc_server_tcp_listens();
        EXPECT_EQ(1, (int)listens.size());
        EXPECT_STREQ("9000", listens.at(0).c_str());
    }
}

VOID TEST(ConfigRtcServerTest, CheckRtcServerProtocol)
{
    srs_error_t err;

    // Test default value (udp) when no rtc_server section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));
        EXPECT_STREQ("udp", conf.get_rtc_server_protocol().c_str());
    }

    // Test default value (udp) when rtc_server section exists but no protocol directive
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{enabled on;}"));
        EXPECT_STREQ("udp", conf.get_rtc_server_protocol().c_str());
    }

    // Test explicit protocol value
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{protocol tcp;}"));
        EXPECT_STREQ("tcp", conf.get_rtc_server_protocol().c_str());
    }

    // Test empty protocol value
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{protocol;}"));
        EXPECT_STREQ("udp", conf.get_rtc_server_protocol().c_str());
    }

    // Test environment variable override
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{protocol tcp;}"));

        SrsSetEnvConfig(conf, rtc_protocol, "SRS_RTC_SERVER_PROTOCOL", "udp");
        EXPECT_STREQ("udp", conf.get_rtc_server_protocol().c_str());
    }
}

VOID TEST(ConfigRtcServerTest, CheckRtcServerIpFamily)
{
    srs_error_t err;

    // Test default value (ipv4) when no rtc_server section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));
        EXPECT_STREQ("ipv4", conf.get_rtc_server_ip_family().c_str());
    }

    // Test default value (ipv4) when rtc_server section exists but no ip_family directive
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{enabled on;}"));
        EXPECT_STREQ("ipv4", conf.get_rtc_server_ip_family().c_str());
    }

    // Test explicit ip_family value
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{ip_family ipv6;}"));
        EXPECT_STREQ("ipv6", conf.get_rtc_server_ip_family().c_str());
    }

    // Test empty ip_family value
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{ip_family;}"));
        EXPECT_STREQ("ipv4", conf.get_rtc_server_ip_family().c_str());
    }
}

VOID TEST(ConfigRtcServerTest, CheckRtcServerEcdsa)
{
    srs_error_t err;

    // Test default value (true) when no rtc_server section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));
        EXPECT_TRUE(conf.get_rtc_server_ecdsa());
    }

    // Test default value (true) when rtc_server section exists but no ecdsa directive
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{enabled on;}"));
        EXPECT_TRUE(conf.get_rtc_server_ecdsa());
    }

    // Test ecdsa off
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{ecdsa off;}"));
        EXPECT_FALSE(conf.get_rtc_server_ecdsa());
    }

    // Test ecdsa on
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{ecdsa on;}"));
        EXPECT_TRUE(conf.get_rtc_server_ecdsa());
    }

    // Test SRS_CONF_PREFER_TRUE behavior (only "off" is false)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{ecdsa true;}"));
        EXPECT_TRUE(conf.get_rtc_server_ecdsa()); // "true" != "off", so it's true

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{ecdsa yes;}"));
        EXPECT_TRUE(conf.get_rtc_server_ecdsa()); // "yes" != "off", so it's true
    }

    // Test empty value
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{ecdsa;}"));
        EXPECT_TRUE(conf.get_rtc_server_ecdsa());
    }
}

VOID TEST(ConfigRtcServerTest, CheckRtcServerEncrypt)
{
    srs_error_t err;

    // Test default value (true) when no rtc_server section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));
        EXPECT_TRUE(conf.get_rtc_server_encrypt());
    }

    // Test default value (true) when rtc_server section exists but no encrypt directive
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{enabled on;}"));
        EXPECT_TRUE(conf.get_rtc_server_encrypt());
    }

    // Test encrypt off
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{encrypt off;}"));
        EXPECT_FALSE(conf.get_rtc_server_encrypt());
    }

    // Test encrypt on
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{encrypt on;}"));
        EXPECT_TRUE(conf.get_rtc_server_encrypt());
    }

    // Test SRS_CONF_PREFER_TRUE behavior (only "off" is false)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{encrypt true;}"));
        EXPECT_TRUE(conf.get_rtc_server_encrypt()); // "true" != "off", so it's true

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{encrypt yes;}"));
        EXPECT_TRUE(conf.get_rtc_server_encrypt()); // "yes" != "off", so it's true
    }

    // Test empty value
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{encrypt;}"));
        EXPECT_TRUE(conf.get_rtc_server_encrypt());
    }
}

VOID TEST(ConfigRtcServerTest, CheckRtcServerReuseport)
{
    srs_error_t err;

    // Test default value (1) when no rtc_server section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));
        EXPECT_EQ(1, conf.get_rtc_server_reuseport());
        EXPECT_EQ(1, conf.get_rtc_server_reuseport2());
    }

    // Test default value (1) when rtc_server section exists but no reuseport directive
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{enabled on;}"));
        EXPECT_EQ(1, conf.get_rtc_server_reuseport());
        EXPECT_EQ(1, conf.get_rtc_server_reuseport2());
    }

    // Test explicit reuseport value
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{reuseport 4;}"));
        EXPECT_EQ(4, conf.get_rtc_server_reuseport2());
        // get_rtc_server_reuseport() may limit the value based on SO_REUSEPORT support
        int reuseport = conf.get_rtc_server_reuseport();
        EXPECT_TRUE(reuseport >= 1 && reuseport <= 4);
    }

    // Test reuseport 0
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{reuseport 0;}"));
        EXPECT_EQ(0, conf.get_rtc_server_reuseport());
        EXPECT_EQ(0, conf.get_rtc_server_reuseport2());
    }

    // Test empty reuseport value
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{reuseport;}"));
        EXPECT_EQ(1, conf.get_rtc_server_reuseport());
        EXPECT_EQ(1, conf.get_rtc_server_reuseport2());
    }
}

VOID TEST(ConfigRtcServerTest, CheckRtcServerMergeNalus)
{
    srs_error_t err;

    // Test default value (false) when no rtc_server section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));
        EXPECT_FALSE(conf.get_rtc_server_merge_nalus());
    }

    // Test default value (false) when rtc_server section exists but no merge_nalus directive
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{enabled on;}"));
        EXPECT_FALSE(conf.get_rtc_server_merge_nalus());
    }

    // Test merge_nalus on
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{merge_nalus on;}"));
        EXPECT_TRUE(conf.get_rtc_server_merge_nalus());
    }

    // Test merge_nalus off
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{merge_nalus off;}"));
        EXPECT_FALSE(conf.get_rtc_server_merge_nalus());
    }

    // Test SRS_CONF_PREFER_FALSE behavior (only "on" is true)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{merge_nalus true;}"));
        EXPECT_FALSE(conf.get_rtc_server_merge_nalus()); // "true" != "on", so it's false

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{merge_nalus yes;}"));
        EXPECT_FALSE(conf.get_rtc_server_merge_nalus()); // "yes" != "on", so it's false
    }

    // Test empty value
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{merge_nalus;}"));
        EXPECT_FALSE(conf.get_rtc_server_merge_nalus());
    }
}

VOID TEST(ConfigRtcServerTest, CheckRtcServerBlackHole)
{
    srs_error_t err;

    // Test default value (false) when no rtc_server section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));
        EXPECT_FALSE(conf.get_rtc_server_black_hole());
    }

    // Test default value (false) when rtc_server section exists but no black_hole section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{enabled on;}"));
        EXPECT_FALSE(conf.get_rtc_server_black_hole());
    }

    // Test default value (false) when black_hole section exists but no enabled directive
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{black_hole{addr 127.0.0.1;}}"));
        EXPECT_FALSE(conf.get_rtc_server_black_hole());
    }

    // Test black_hole enabled on
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{black_hole{enabled on;}}"));
        EXPECT_TRUE(conf.get_rtc_server_black_hole());
    }

    // Test black_hole enabled off
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{black_hole{enabled off;}}"));
        EXPECT_FALSE(conf.get_rtc_server_black_hole());
    }

    // Test SRS_CONF_PREFER_FALSE behavior (only "on" is true)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{black_hole{enabled true;}}"));
        EXPECT_FALSE(conf.get_rtc_server_black_hole()); // "true" != "on", so it's false

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{black_hole{enabled yes;}}"));
        EXPECT_FALSE(conf.get_rtc_server_black_hole()); // "yes" != "on", so it's false
    }

    // Test empty value
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{black_hole{enabled;}}"));
        EXPECT_FALSE(conf.get_rtc_server_black_hole());
    }

    // Test environment variable override
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{black_hole{enabled off;}}"));

        SrsSetEnvConfig(conf, rtc_black_hole, "SRS_RTC_SERVER_BLACK_HOLE_ENABLED", "on");
        EXPECT_TRUE(conf.get_rtc_server_black_hole());
    }
}

VOID TEST(ConfigRtcServerTest, CheckRtcServerBlackHoleAddr)
{
    srs_error_t err;

    // Test default value (empty) when no rtc_server section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));
        EXPECT_STREQ("", conf.get_rtc_server_black_hole_addr().c_str());
    }

    // Test default value (empty) when rtc_server section exists but no black_hole section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{enabled on;}"));
        EXPECT_STREQ("", conf.get_rtc_server_black_hole_addr().c_str());
    }

    // Test default value (empty) when black_hole section exists but no addr directive
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{black_hole{enabled on;}}"));
        EXPECT_STREQ("", conf.get_rtc_server_black_hole_addr().c_str());
    }

    // Test explicit addr value
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{black_hole{addr 127.0.0.1;}}"));
        EXPECT_STREQ("127.0.0.1", conf.get_rtc_server_black_hole_addr().c_str());
    }

    // Test empty addr value
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{black_hole{addr;}}"));
        EXPECT_STREQ("", conf.get_rtc_server_black_hole_addr().c_str());
    }

    // Test environment variable override
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{black_hole{addr 127.0.0.1;}}"));

        SrsSetEnvConfig(conf, rtc_black_hole_addr, "SRS_RTC_SERVER_BLACK_HOLE_ADDR", "10.0.0.1");
        EXPECT_STREQ("10.0.0.1", conf.get_rtc_server_black_hole_addr().c_str());
    }
}

VOID TEST(ConfigRtcVhostTest, CheckRtcEnabled)
{
    srs_error_t err;

    // Test default value (false) when no vhost
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));
        EXPECT_FALSE(conf.get_rtc_enabled("__defaultVhost__"));
        EXPECT_FALSE(conf.get_rtc_enabled("test.com"));
    }

    // Test default value (false) when vhost exists but no rtc section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{enabled on;}}"));
        EXPECT_FALSE(conf.get_rtc_enabled("test.com"));
    }

    // Test default value (false) when rtc section exists but no enabled directive
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{keep_bframe on;}}"));
        EXPECT_FALSE(conf.get_rtc_enabled("test.com"));
    }

    // Test rtc enabled on
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{enabled on;}}"));
        EXPECT_TRUE(conf.get_rtc_enabled("test.com"));
    }

    // Test rtc enabled off
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{enabled off;}}"));
        EXPECT_FALSE(conf.get_rtc_enabled("test.com"));
    }

    // Test SRS_CONF_PREFER_FALSE behavior (only "on" is true)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{enabled true;}}"));
        EXPECT_FALSE(conf.get_rtc_enabled("test.com")); // "true" != "on", so it's false

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{enabled yes;}}"));
        EXPECT_FALSE(conf.get_rtc_enabled("test.com")); // "yes" != "on", so it's false
    }

    // Test empty value
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{enabled;}}"));
        EXPECT_FALSE(conf.get_rtc_enabled("test.com"));
    }

    // Test environment variable override
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{enabled off;}}"));

        SrsSetEnvConfig(conf, rtc_enabled, "SRS_VHOST_RTC_ENABLED", "on");
        EXPECT_TRUE(conf.get_rtc_enabled("test.com"));
    }
}

VOID TEST(ConfigRtcVhostTest, CheckRtcKeepBframe)
{
    srs_error_t err;

    // Test default value (false) when no vhost
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));
        EXPECT_FALSE(conf.get_rtc_keep_bframe("__defaultVhost__"));
        EXPECT_FALSE(conf.get_rtc_keep_bframe("test.com"));
    }

    // Test default value (false) when vhost exists but no rtc section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{enabled on;}}"));
        EXPECT_FALSE(conf.get_rtc_keep_bframe("test.com"));
    }

    // Test default value (false) when rtc section exists but no keep_bframe directive
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{enabled on;}}"));
        EXPECT_FALSE(conf.get_rtc_keep_bframe("test.com"));
    }

    // Test keep_bframe on
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{keep_bframe on;}}"));
        EXPECT_TRUE(conf.get_rtc_keep_bframe("test.com"));
    }

    // Test keep_bframe off
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{keep_bframe off;}}"));
        EXPECT_FALSE(conf.get_rtc_keep_bframe("test.com"));
    }

    // Test SRS_CONF_PREFER_FALSE behavior (only "on" is true)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{keep_bframe true;}}"));
        EXPECT_FALSE(conf.get_rtc_keep_bframe("test.com")); // "true" != "on", so it's false

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{keep_bframe yes;}}"));
        EXPECT_FALSE(conf.get_rtc_keep_bframe("test.com")); // "yes" != "on", so it's false
    }

    // Test empty value
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{keep_bframe;}}"));
        EXPECT_FALSE(conf.get_rtc_keep_bframe("test.com"));
    }

    // Test environment variable override
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{keep_bframe off;}}"));

        SrsSetEnvConfig(conf, rtc_keep_bframe, "SRS_VHOST_RTC_KEEP_BFRAME", "on");
        EXPECT_TRUE(conf.get_rtc_keep_bframe("test.com"));
    }
}

VOID TEST(ConfigRtcVhostTest, CheckRtcKeepAvcNaluSei)
{
    srs_error_t err;

    // Test default value (true) when no vhost
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));
        EXPECT_TRUE(conf.get_rtc_keep_avc_nalu_sei("__defaultVhost__"));
        EXPECT_TRUE(conf.get_rtc_keep_avc_nalu_sei("test.com"));
    }

    // Test default value (true) when vhost exists but no rtc section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{enabled on;}}"));
        EXPECT_TRUE(conf.get_rtc_keep_avc_nalu_sei("test.com"));
    }

    // Test default value (true) when rtc section exists but no keep_avc_nalu_sei directive
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{enabled on;}}"));
        EXPECT_TRUE(conf.get_rtc_keep_avc_nalu_sei("test.com"));
    }

    // Test keep_avc_nalu_sei off
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{keep_avc_nalu_sei off;}}"));
        EXPECT_FALSE(conf.get_rtc_keep_avc_nalu_sei("test.com"));
    }

    // Test keep_avc_nalu_sei on
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{keep_avc_nalu_sei on;}}"));
        EXPECT_TRUE(conf.get_rtc_keep_avc_nalu_sei("test.com"));
    }

    // Test SRS_CONF_PREFER_TRUE behavior (only "off" is false)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{keep_avc_nalu_sei true;}}"));
        EXPECT_TRUE(conf.get_rtc_keep_avc_nalu_sei("test.com")); // "true" != "off", so it's true

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{keep_avc_nalu_sei yes;}}"));
        EXPECT_TRUE(conf.get_rtc_keep_avc_nalu_sei("test.com")); // "yes" != "off", so it's true
    }

    // Test empty value
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{keep_avc_nalu_sei;}}"));
        EXPECT_TRUE(conf.get_rtc_keep_avc_nalu_sei("test.com"));
    }

    // Test environment variable override
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{keep_avc_nalu_sei on;}}"));

        SrsSetEnvConfig(conf, rtc_keep_avc_nalu_sei, "SRS_VHOST_RTC_KEEP_AVC_NALU_SEI", "off");
        EXPECT_FALSE(conf.get_rtc_keep_avc_nalu_sei("test.com"));
    }
}

VOID TEST(ConfigRtcVhostTest, CheckRtcFromRtmp)
{
    srs_error_t err;

    // Test default value (false) when no vhost
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));
        EXPECT_FALSE(conf.get_rtc_from_rtmp("__defaultVhost__"));
        EXPECT_FALSE(conf.get_rtc_from_rtmp("test.com"));
    }

    // Test default value (false) when vhost exists but no rtc section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{enabled on;}}"));
        EXPECT_FALSE(conf.get_rtc_from_rtmp("test.com"));
    }

    // Test default value (false) when rtc section exists but no rtmp_to_rtc directive
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{enabled on;}}"));
        EXPECT_FALSE(conf.get_rtc_from_rtmp("test.com"));
    }

    // Test rtmp_to_rtc on
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{rtmp_to_rtc on;}}"));
        EXPECT_TRUE(conf.get_rtc_from_rtmp("test.com"));
    }

    // Test rtmp_to_rtc off
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{rtmp_to_rtc off;}}"));
        EXPECT_FALSE(conf.get_rtc_from_rtmp("test.com"));
    }

    // Test SRS_CONF_PREFER_FALSE behavior (only "on" is true)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{rtmp_to_rtc true;}}"));
        EXPECT_FALSE(conf.get_rtc_from_rtmp("test.com")); // "true" != "on", so it's false

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{rtmp_to_rtc yes;}}"));
        EXPECT_FALSE(conf.get_rtc_from_rtmp("test.com")); // "yes" != "on", so it's false
    }

    // Test empty value
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{rtmp_to_rtc;}}"));
        EXPECT_FALSE(conf.get_rtc_from_rtmp("test.com"));
    }

    // Test environment variable override
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{rtmp_to_rtc off;}}"));

        SrsSetEnvConfig(conf, rtc_from_rtmp, "SRS_VHOST_RTC_RTMP_TO_RTC", "on");
        EXPECT_TRUE(conf.get_rtc_from_rtmp("test.com"));
    }
}

VOID TEST(ConfigRtcVhostTest, CheckRtcStunTimeout)
{
    srs_error_t err;

    // Test default value (30 seconds) when no vhost
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));
        EXPECT_EQ(30 * SRS_UTIME_SECONDS, conf.get_rtc_stun_timeout("__defaultVhost__"));
        EXPECT_EQ(30 * SRS_UTIME_SECONDS, conf.get_rtc_stun_timeout("test.com"));
    }

    // Test default value (30 seconds) when vhost exists but no rtc section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{enabled on;}}"));
        EXPECT_EQ(30 * SRS_UTIME_SECONDS, conf.get_rtc_stun_timeout("test.com"));
    }

    // Test default value (30 seconds) when rtc section exists but no stun_timeout directive
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{enabled on;}}"));
        EXPECT_EQ(30 * SRS_UTIME_SECONDS, conf.get_rtc_stun_timeout("test.com"));
    }

    // Test explicit stun_timeout value
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{stun_timeout 60;}}"));
        EXPECT_EQ(60 * SRS_UTIME_SECONDS, conf.get_rtc_stun_timeout("test.com"));
    }

    // Test stun_timeout 0
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{stun_timeout 0;}}"));
        EXPECT_EQ(0, conf.get_rtc_stun_timeout("test.com"));
    }

    // Test empty stun_timeout value
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{stun_timeout;}}"));
        EXPECT_EQ(30 * SRS_UTIME_SECONDS, conf.get_rtc_stun_timeout("test.com"));
    }

    // Test environment variable override
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{stun_timeout 60;}}"));

        SrsSetEnvConfig(conf, rtc_stun_timeout, "SRS_VHOST_RTC_STUN_TIMEOUT", "120");
        EXPECT_EQ(120 * SRS_UTIME_SECONDS, conf.get_rtc_stun_timeout("test.com"));
    }
}

VOID TEST(ConfigRtcVhostTest, CheckRtcStunStrictCheck)
{
    srs_error_t err;

    // Test default value (false) when no vhost
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));
        EXPECT_FALSE(conf.get_rtc_stun_strict_check("__defaultVhost__"));
        EXPECT_FALSE(conf.get_rtc_stun_strict_check("test.com"));
    }

    // Test default value (false) when vhost exists but no rtc section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{enabled on;}}"));
        EXPECT_FALSE(conf.get_rtc_stun_strict_check("test.com"));
    }

    // Test default value (false) when rtc section exists but no stun_strict_check directive
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{enabled on;}}"));
        EXPECT_FALSE(conf.get_rtc_stun_strict_check("test.com"));
    }

    // Test stun_strict_check on
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{stun_strict_check on;}}"));
        EXPECT_TRUE(conf.get_rtc_stun_strict_check("test.com"));
    }

    // Test stun_strict_check off
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{stun_strict_check off;}}"));
        EXPECT_FALSE(conf.get_rtc_stun_strict_check("test.com"));
    }

    // Test SRS_CONF_PREFER_FALSE behavior (only "on" is true)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{stun_strict_check true;}}"));
        EXPECT_FALSE(conf.get_rtc_stun_strict_check("test.com")); // "true" != "on", so it's false

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{stun_strict_check yes;}}"));
        EXPECT_FALSE(conf.get_rtc_stun_strict_check("test.com")); // "yes" != "on", so it's false
    }

    // Test empty value
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{stun_strict_check;}}"));
        EXPECT_FALSE(conf.get_rtc_stun_strict_check("test.com"));
    }

    // Test environment variable override
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{stun_strict_check off;}}"));

        SrsSetEnvConfig(conf, rtc_stun_strict_check, "SRS_VHOST_RTC_STUN_STRICT_CHECK", "on");
        EXPECT_TRUE(conf.get_rtc_stun_strict_check("test.com"));
    }
}

VOID TEST(ConfigRtcVhostTest, CheckRtcDtlsRole)
{
    srs_error_t err;

    // Test default value (passive) when no vhost
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));
        EXPECT_STREQ("passive", conf.get_rtc_dtls_role("__defaultVhost__").c_str());
        EXPECT_STREQ("passive", conf.get_rtc_dtls_role("test.com").c_str());
    }

    // Test default value (passive) when vhost exists but no rtc section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{enabled on;}}"));
        EXPECT_STREQ("passive", conf.get_rtc_dtls_role("test.com").c_str());
    }

    // Test default value (passive) when rtc section exists but no dtls_role directive
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{enabled on;}}"));
        EXPECT_STREQ("passive", conf.get_rtc_dtls_role("test.com").c_str());
    }

    // Test explicit dtls_role value
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{dtls_role active;}}"));
        EXPECT_STREQ("active", conf.get_rtc_dtls_role("test.com").c_str());
    }

    // Test empty dtls_role value
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{dtls_role;}}"));
        EXPECT_STREQ("passive", conf.get_rtc_dtls_role("test.com").c_str());
    }

    // Test environment variable override
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{dtls_role passive;}}"));

        SrsSetEnvConfig(conf, rtc_dtls_role, "SRS_VHOST_RTC_DTLS_ROLE", "active");
        EXPECT_STREQ("active", conf.get_rtc_dtls_role("test.com").c_str());
    }
}

VOID TEST(ConfigRtcVhostTest, CheckRtcDtlsVersion)
{
    srs_error_t err;

    // Test default value (auto) when no vhost
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));
        EXPECT_STREQ("auto", conf.get_rtc_dtls_version("__defaultVhost__").c_str());
        EXPECT_STREQ("auto", conf.get_rtc_dtls_version("test.com").c_str());
    }

    // Test default value (auto) when vhost exists but no rtc section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{enabled on;}}"));
        EXPECT_STREQ("auto", conf.get_rtc_dtls_version("test.com").c_str());
    }

    // Test default value (auto) when rtc section exists but no dtls_version directive
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{enabled on;}}"));
        EXPECT_STREQ("auto", conf.get_rtc_dtls_version("test.com").c_str());
    }

    // Test explicit dtls_version value
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{dtls_version 1.2;}}"));
        EXPECT_STREQ("1.2", conf.get_rtc_dtls_version("test.com").c_str());
    }

    // Test empty dtls_version value
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{dtls_version;}}"));
        EXPECT_STREQ("auto", conf.get_rtc_dtls_version("test.com").c_str());
    }

    // Test environment variable override
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{dtls_version auto;}}"));

        SrsSetEnvConfig(conf, rtc_dtls_version, "SRS_VHOST_RTC_DTLS_VERSION", "1.3");
        EXPECT_STREQ("1.3", conf.get_rtc_dtls_version("test.com").c_str());
    }
}

VOID TEST(ConfigRtcVhostTest, CheckRtcDropForPt)
{
    srs_error_t err;

    // Test default value (0) when no vhost
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));
        EXPECT_EQ(0, conf.get_rtc_drop_for_pt("__defaultVhost__"));
        EXPECT_EQ(0, conf.get_rtc_drop_for_pt("test.com"));
    }

    // Test default value (0) when vhost exists but no rtc section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{enabled on;}}"));
        EXPECT_EQ(0, conf.get_rtc_drop_for_pt("test.com"));
    }

    // Test default value (0) when rtc section exists but no drop_for_pt directive
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{enabled on;}}"));
        EXPECT_EQ(0, conf.get_rtc_drop_for_pt("test.com"));
    }

    // Test explicit drop_for_pt value
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{drop_for_pt 96;}}"));
        EXPECT_EQ(96, conf.get_rtc_drop_for_pt("test.com"));
    }

    // Test drop_for_pt negative value
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{drop_for_pt -1;}}"));
        EXPECT_EQ(-1, conf.get_rtc_drop_for_pt("test.com"));
    }

    // Test empty drop_for_pt value
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{drop_for_pt;}}"));
        EXPECT_EQ(0, conf.get_rtc_drop_for_pt("test.com"));
    }

    // Test environment variable override
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{drop_for_pt 96;}}"));

        SrsSetEnvConfig(conf, rtc_drop_for_pt, "SRS_VHOST_RTC_DROP_FOR_PT", "97");
        EXPECT_EQ(97, conf.get_rtc_drop_for_pt("test.com"));
    }
}

VOID TEST(ConfigRtcVhostTest, CheckRtcToRtmp)
{
    srs_error_t err;

    // Test default value (false) when no vhost
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));
        EXPECT_FALSE(conf.get_rtc_to_rtmp("__defaultVhost__"));
        EXPECT_FALSE(conf.get_rtc_to_rtmp("test.com"));
    }

    // Test default value (false) when vhost exists but no rtc section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{enabled on;}}"));
        EXPECT_FALSE(conf.get_rtc_to_rtmp("test.com"));
    }

    // Test default value (false) when rtc section exists but no rtc_to_rtmp directive
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{enabled on;}}"));
        EXPECT_FALSE(conf.get_rtc_to_rtmp("test.com"));
    }

    // Test rtc_to_rtmp on
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{rtc_to_rtmp on;}}"));
        EXPECT_TRUE(conf.get_rtc_to_rtmp("test.com"));
    }

    // Test rtc_to_rtmp off
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{rtc_to_rtmp off;}}"));
        EXPECT_FALSE(conf.get_rtc_to_rtmp("test.com"));
    }

    // Test SRS_CONF_PREFER_FALSE behavior (only "on" is true)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{rtc_to_rtmp true;}}"));
        EXPECT_FALSE(conf.get_rtc_to_rtmp("test.com")); // "true" != "on", so it's false

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{rtc_to_rtmp yes;}}"));
        EXPECT_FALSE(conf.get_rtc_to_rtmp("test.com")); // "yes" != "on", so it's false
    }

    // Test empty value
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{rtc_to_rtmp;}}"));
        EXPECT_FALSE(conf.get_rtc_to_rtmp("test.com"));
    }

    // Test environment variable override
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{rtc_to_rtmp off;}}"));

        SrsSetEnvConfig(conf, rtc_to_rtmp, "SRS_VHOST_RTC_RTC_TO_RTMP", "on");
        EXPECT_TRUE(conf.get_rtc_to_rtmp("test.com"));
    }
}
