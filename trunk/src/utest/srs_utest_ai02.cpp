//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_utest_ai02.hpp>

using namespace std;

#include <srs_app_config.hpp>
#include <srs_app_security.hpp>
#include <srs_app_utility.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_file.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_rtmp_stack.hpp>
#include <srs_utest_ai06.hpp>
#include <srs_utest_manual_config.hpp>
#include <srs_utest_manual_kernel.hpp>

VOID TEST(ConfigHttpsStreamTest, CheckHttpsStreamListensDefault)
{
    srs_error_t err;

    // Test default value when no http_server section exists
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        vector<string> listens = conf.get_https_stream_listens();
        EXPECT_EQ(1, (int)listens.size());
        EXPECT_STREQ("8088", listens.at(0).c_str());
    }

    // Test default value when http_server exists but no https section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_server{enabled on;}"));

        vector<string> listens = conf.get_https_stream_listens();
        EXPECT_EQ(1, (int)listens.size());
        EXPECT_STREQ("8088", listens.at(0).c_str());
    }

    // Test default value when https section exists but no listen directive
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_server{enabled on; https{enabled on;}}"));

        vector<string> listens = conf.get_https_stream_listens();
        EXPECT_EQ(1, (int)listens.size());
        EXPECT_STREQ("8088", listens.at(0).c_str());
    }
}

VOID TEST(ConfigHttpsStreamTest, CheckHttpsStreamListensEmptyArgs)
{
    srs_error_t err;

    // Test default value when listen directive exists but has no arguments
    // This tests the specific case: if (listens.empty()) { listens.push_back(DEFAULT); }
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_server{enabled on; https{enabled on; listen;}}"));

        vector<string> listens = conf.get_https_stream_listens();
        EXPECT_EQ(1, (int)listens.size());
        EXPECT_STREQ("8088", listens.at(0).c_str());
    }
}

VOID TEST(ConfigHttpsStreamTest, CheckHttpsStreamListensWithArgs)
{
    srs_error_t err;

    // Test single listen port
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_server{enabled on; https{enabled on; listen 9443;}}"));

        vector<string> listens = conf.get_https_stream_listens();
        EXPECT_EQ(1, (int)listens.size());
        EXPECT_STREQ("9443", listens.at(0).c_str());
    }

    // Test multiple listen ports
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_server{enabled on; https{enabled on; listen 9443 9444 9445;}}"));

        vector<string> listens = conf.get_https_stream_listens();
        EXPECT_EQ(3, (int)listens.size());
        EXPECT_STREQ("9443", listens.at(0).c_str());
        EXPECT_STREQ("9444", listens.at(1).c_str());
        EXPECT_STREQ("9445", listens.at(2).c_str());
    }

    // Test IPv6 address
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_server{enabled on; https{enabled on; listen [::1]:9443;}}"));

        vector<string> listens = conf.get_https_stream_listens();
        EXPECT_EQ(1, (int)listens.size());
        EXPECT_STREQ("[::1]:9443", listens.at(0).c_str());
    }

    // Test IPv4 address with port
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_server{enabled on; https{enabled on; listen 127.0.0.1:9443;}}"));

        vector<string> listens = conf.get_https_stream_listens();
        EXPECT_EQ(1, (int)listens.size());
        EXPECT_STREQ("127.0.0.1:9443", listens.at(0).c_str());
    }
}

VOID TEST(ConfigHttpsStreamTest, CheckHttpsStreamListensEnvOverride)
{
    srs_error_t err;

    // Test environment variable override with single port
    if (true) {
        MockSrsConfig conf;

        SrsSetEnvConfig(conf, https_listen, "SRS_HTTP_SERVER_HTTPS_LISTEN", "7443");

        vector<string> listens = conf.get_https_stream_listens();
        EXPECT_EQ(1, (int)listens.size());
        EXPECT_STREQ("7443", listens.at(0).c_str());
    }

    // Test environment variable override with multiple ports
    if (true) {
        MockSrsConfig conf;

        SrsSetEnvConfig(conf, https_listen, "SRS_HTTP_SERVER_HTTPS_LISTEN", "7443 7444 7445");

        vector<string> listens = conf.get_https_stream_listens();
        EXPECT_EQ(3, (int)listens.size());
        EXPECT_STREQ("7443", listens.at(0).c_str());
        EXPECT_STREQ("7444", listens.at(1).c_str());
        EXPECT_STREQ("7445", listens.at(2).c_str());
    }

    // Test environment variable override takes precedence over config file
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_server{enabled on; https{enabled on; listen 9443;}}"));

        SrsSetEnvConfig(conf, https_listen, "SRS_HTTP_SERVER_HTTPS_LISTEN", "7443");

        vector<string> listens = conf.get_https_stream_listens();
        EXPECT_EQ(1, (int)listens.size());
        EXPECT_STREQ("7443", listens.at(0).c_str());
    }

    // Test empty environment variable falls back to config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_server{enabled on; https{enabled on; listen 9443;}}"));

        SrsSetEnvConfig(conf, https_listen, "SRS_HTTP_SERVER_HTTPS_LISTEN", "");

        vector<string> listens = conf.get_https_stream_listens();
        EXPECT_EQ(1, (int)listens.size());
        EXPECT_STREQ("9443", listens.at(0).c_str());
    }
}

VOID TEST(ConfigHttpsStreamTest, CheckHttpsStreamListensEdgeCases)
{
    srs_error_t err;

    // Test with whitespace in environment variable
    if (true) {
        MockSrsConfig conf;

        SrsSetEnvConfig(conf, https_listen, "SRS_HTTP_SERVER_HTTPS_LISTEN", "7443 7444");

        vector<string> listens = conf.get_https_stream_listens();
        EXPECT_EQ(2, (int)listens.size());
        EXPECT_STREQ("7443", listens.at(0).c_str());
        EXPECT_STREQ("7444", listens.at(1).c_str());
    }

    // Test environment variable with empty string (should fall back to default)
    if (true) {
        MockSrsConfig conf;

        SrsSetEnvConfig(conf, https_listen, "SRS_HTTP_SERVER_HTTPS_LISTEN", "");

        vector<string> listens = conf.get_https_stream_listens();
        EXPECT_EQ(1, (int)listens.size());
        EXPECT_STREQ("8088", listens.at(0).c_str());
    }

    // Test complex configuration with nested sections
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF
                                              "http_server{"
                                              "enabled on;"
                                              "listen 8080;"
                                              "dir ./objs/nginx/html;"
                                              "https{"
                                              "enabled on;"
                                              "listen 8443 9443;"
                                              "key ./conf/server.key;"
                                              "cert ./conf/server.crt;"
                                              "}"
                                              "}"));

        vector<string> listens = conf.get_https_stream_listens();
        EXPECT_EQ(2, (int)listens.size());
        EXPECT_STREQ("8443", listens.at(0).c_str());
        EXPECT_STREQ("9443", listens.at(1).c_str());
    }
}

VOID TEST(ConfigHttpsStreamTest, CheckHttpsStreamListensDefaultBehavior)
{
    srs_error_t err;

    // Test the specific case mentioned in the request: empty listens vector should use DEFAULT
    // This simulates the internal behavior when conf->args_.size() is 0
    if (true) {
        MockSrsConfig conf;
        // Create a configuration where listen directive exists but has no arguments
        // This should trigger the "if (listens.empty()) { listens.push_back(DEFAULT); }" code path
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_server{enabled on; https{enabled on; listen;}}"));

        vector<string> listens = conf.get_https_stream_listens();
        EXPECT_EQ(1, (int)listens.size());
        EXPECT_STREQ("8088", listens.at(0).c_str());
    }

    // Test default behavior when environment variable is not set and no config exists
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        vector<string> listens = conf.get_https_stream_listens();
        EXPECT_EQ(1, (int)listens.size());
        EXPECT_STREQ("8088", listens.at(0).c_str());
    }

    // Test that default is used when https section is missing
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_server{enabled on; listen 8080;}"));

        vector<string> listens = conf.get_https_stream_listens();
        EXPECT_EQ(1, (int)listens.size());
        EXPECT_STREQ("8088", listens.at(0).c_str());
    }

    // Test that default is used when listen directive is missing from https section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_server{enabled on; https{enabled on; key ./server.key;}}"));

        vector<string> listens = conf.get_https_stream_listens();
        EXPECT_EQ(1, (int)listens.size());
        EXPECT_STREQ("8088", listens.at(0).c_str());
    }
}

VOID TEST(ConfigHttpsStreamTest, CheckHttpsStreamListensConsistency)
{
    srs_error_t err;

    // Test consistency with other similar methods (like get_http_stream_listens)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF
                                              "http_server{"
                                              "enabled on;"
                                              "listen 8080;"
                                              "https{"
                                              "enabled on;"
                                              "listen 8443;"
                                              "}"
                                              "}"));

        vector<string> http_listens = conf.get_http_stream_listens();
        vector<string> https_listens = conf.get_https_stream_listens();

        EXPECT_EQ(1, (int)http_listens.size());
        EXPECT_STREQ("8080", http_listens.at(0).c_str());

        EXPECT_EQ(1, (int)https_listens.size());
        EXPECT_STREQ("8443", https_listens.at(0).c_str());
    }

    // Test that HTTPS and HTTP can have different ports
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF
                                              "http_server{"
                                              "enabled on;"
                                              "listen 8080 8081;"
                                              "https{"
                                              "enabled on;"
                                              "listen 8443 8444 8445;"
                                              "}"
                                              "}"));

        vector<string> http_listens = conf.get_http_stream_listens();
        vector<string> https_listens = conf.get_https_stream_listens();

        EXPECT_EQ(2, (int)http_listens.size());
        EXPECT_EQ(3, (int)https_listens.size());

        EXPECT_STREQ("8080", http_listens.at(0).c_str());
        EXPECT_STREQ("8081", http_listens.at(1).c_str());

        EXPECT_STREQ("8443", https_listens.at(0).c_str());
        EXPECT_STREQ("8444", https_listens.at(1).c_str());
        EXPECT_STREQ("8445", https_listens.at(2).c_str());
    }
}

VOID TEST(ConfigHttpsStreamTest, CheckHttpsStreamSslKey)
{
    srs_error_t err;

    // Test default value when no http_server section exists
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        EXPECT_STREQ("./conf/server.key", conf.get_https_stream_ssl_key().c_str());
    }

    // Test default value when https section doesn't exist
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_server{enabled on;}"));

        EXPECT_STREQ("./conf/server.key", conf.get_https_stream_ssl_key().c_str());
    }

    // Test default value when key directive doesn't exist
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_server{enabled on; https{enabled on;}}"));

        EXPECT_STREQ("./conf/server.key", conf.get_https_stream_ssl_key().c_str());
    }

    // Test custom key path
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_server{enabled on; https{enabled on; key /path/to/custom.key;}}"));

        EXPECT_STREQ("/path/to/custom.key", conf.get_https_stream_ssl_key().c_str());
    }

    // Test relative key path
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_server{enabled on; https{enabled on; key ./ssl/server.key;}}"));

        EXPECT_STREQ("./ssl/server.key", conf.get_https_stream_ssl_key().c_str());
    }
}

VOID TEST(ConfigHttpsStreamTest, CheckHttpsStreamSslCert)
{
    srs_error_t err;

    // Test default value when no http_server section exists
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        EXPECT_STREQ("./conf/server.crt", conf.get_https_stream_ssl_cert().c_str());
    }

    // Test default value when https section doesn't exist
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_server{enabled on;}"));

        EXPECT_STREQ("./conf/server.crt", conf.get_https_stream_ssl_cert().c_str());
    }

    // Test default value when cert directive doesn't exist
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_server{enabled on; https{enabled on;}}"));

        EXPECT_STREQ("./conf/server.crt", conf.get_https_stream_ssl_cert().c_str());
    }

    // Test custom cert path
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_server{enabled on; https{enabled on; cert /path/to/custom.crt;}}"));

        EXPECT_STREQ("/path/to/custom.crt", conf.get_https_stream_ssl_cert().c_str());
    }

    // Test relative cert path
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_server{enabled on; https{enabled on; cert ./ssl/server.crt;}}"));

        EXPECT_STREQ("./ssl/server.crt", conf.get_https_stream_ssl_cert().c_str());
    }

    // Test both key and cert together
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF
                                              "http_server{"
                                              "enabled on;"
                                              "https{"
                                              "enabled on;"
                                              "key /custom/path/server.key;"
                                              "cert /custom/path/server.crt;"
                                              "}"
                                              "}"));

        EXPECT_STREQ("/custom/path/server.key", conf.get_https_stream_ssl_key().c_str());
        EXPECT_STREQ("/custom/path/server.crt", conf.get_https_stream_ssl_cert().c_str());
    }
}

VOID TEST(ConfigVhostHttpTest, CheckVhostHttpEnabled)
{
    srs_error_t err;

    // Test default value when no vhost exists
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        EXPECT_FALSE(conf.get_vhost_http_enabled("__defaultVhost__"));
    }

    // Test default value when vhost exists but no http_static section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{}"));

        EXPECT_FALSE(conf.get_vhost_http_enabled("test.com"));
    }

    // Test default value when http_static section exists but no enabled directive
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{http_static{}}"));

        EXPECT_FALSE(conf.get_vhost_http_enabled("test.com"));
    }

    // Test default value when enabled directive exists but is empty
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{http_static{enabled;}}"));

        EXPECT_FALSE(conf.get_vhost_http_enabled("test.com"));
    }

    // Test enabled = on
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{http_static{enabled on;}}"));

        EXPECT_TRUE(conf.get_vhost_http_enabled("test.com"));
    }

    // Test enabled = off
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{http_static{enabled off;}}"));

        EXPECT_FALSE(conf.get_vhost_http_enabled("test.com"));
    }

    // Test enabled = true (SRS_CONF_PREFER_FALSE: only "on" is true)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{http_static{enabled true;}}"));

        EXPECT_FALSE(conf.get_vhost_http_enabled("test.com")); // "true" != "on", so it's false
    }

    // Test enabled = false (SRS_CONF_PREFER_FALSE: only "on" is true)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{http_static{enabled false;}}"));

        EXPECT_FALSE(conf.get_vhost_http_enabled("test.com")); // "false" != "on", so it's false
    }
}

VOID TEST(ConfigVhostHttpTest, CheckVhostHttpMount)
{
    srs_error_t err;

    // Test default value when no vhost exists
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        EXPECT_STREQ("[vhost]/", conf.get_vhost_http_mount("__defaultVhost__").c_str());
    }

    // Test default value when vhost exists but no http_static section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{}"));

        EXPECT_STREQ("[vhost]/", conf.get_vhost_http_mount("test.com").c_str());
    }

    // Test default value when http_static section exists but no mount directive
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{http_static{}}"));

        EXPECT_STREQ("[vhost]/", conf.get_vhost_http_mount("test.com").c_str());
    }

    // Test default value when mount directive exists but is empty
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{http_static{mount;}}"));

        EXPECT_STREQ("[vhost]/", conf.get_vhost_http_mount("test.com").c_str());
    }

    // Test custom mount path
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{http_static{mount /custom/path;}}"));

        EXPECT_STREQ("/custom/path", conf.get_vhost_http_mount("test.com").c_str());
    }

    // Test root mount path
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{http_static{mount /;}}"));

        EXPECT_STREQ("/", conf.get_vhost_http_mount("test.com").c_str());
    }
}

VOID TEST(ConfigVhostHttpTest, CheckVhostHttpDir)
{
    srs_error_t err;

    // Test default value when no vhost exists
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        EXPECT_STREQ("./objs/nginx/html", conf.get_vhost_http_dir("__defaultVhost__").c_str());
    }

    // Test default value when vhost exists but no http_static section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{}"));

        EXPECT_STREQ("./objs/nginx/html", conf.get_vhost_http_dir("test.com").c_str());
    }

    // Test default value when http_static section exists but no dir directive
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{http_static{}}"));

        EXPECT_STREQ("./objs/nginx/html", conf.get_vhost_http_dir("test.com").c_str());
    }

    // Test default value when dir directive exists but is empty
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{http_static{dir;}}"));

        EXPECT_STREQ("./objs/nginx/html", conf.get_vhost_http_dir("test.com").c_str());
    }

    // Test custom directory path
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{http_static{dir /var/www/html;}}"));

        EXPECT_STREQ("/var/www/html", conf.get_vhost_http_dir("test.com").c_str());
    }

    // Test relative directory path
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{http_static{dir ./html;}}"));

        EXPECT_STREQ("./html", conf.get_vhost_http_dir("test.com").c_str());
    }
}

VOID TEST(ConfigVhostHttpRemuxTest, CheckVhostHttpRemuxEnabled)
{
    srs_error_t err;

    // Test default value when no vhost exists
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        EXPECT_FALSE(conf.get_vhost_http_remux_enabled("__defaultVhost__"));
    }

    // Test default value when vhost exists but no http_remux section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{}"));

        EXPECT_FALSE(conf.get_vhost_http_remux_enabled("test.com"));
    }

    // Test default value when http_remux section exists but no enabled directive
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{http_remux{}}"));

        EXPECT_FALSE(conf.get_vhost_http_remux_enabled("test.com"));
    }

    // Test default value when enabled directive exists but is empty
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{http_remux{enabled;}}"));

        EXPECT_FALSE(conf.get_vhost_http_remux_enabled("test.com"));
    }

    // Test enabled = on
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{http_remux{enabled on;}}"));

        EXPECT_TRUE(conf.get_vhost_http_remux_enabled("test.com"));
    }

    // Test enabled = off
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{http_remux{enabled off;}}"));

        EXPECT_FALSE(conf.get_vhost_http_remux_enabled("test.com"));
    }

    // Test enabled = true (SRS_CONF_PREFER_FALSE: only "on" is true)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{http_remux{enabled true;}}"));

        EXPECT_FALSE(conf.get_vhost_http_remux_enabled("test.com")); // "true" != "on", so it's false
    }

    // Test enabled = false (SRS_CONF_PREFER_FALSE: only "on" is true)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{http_remux{enabled false;}}"));

        EXPECT_FALSE(conf.get_vhost_http_remux_enabled("test.com")); // "false" != "on", so it's false
    }
}

VOID TEST(ConfigVhostHttpRemuxTest, CheckVhostHttpRemuxFastCache)
{
    srs_error_t err;

    // Test default value when no vhost exists
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        EXPECT_EQ(0, conf.get_vhost_http_remux_fast_cache("__defaultVhost__"));
    }

    // Test default value when vhost exists but no http_remux section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{}"));

        EXPECT_EQ(0, conf.get_vhost_http_remux_fast_cache("test.com"));
    }

    // Test default value when http_remux section exists but no fast_cache directive
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{http_remux{}}"));

        EXPECT_EQ(0, conf.get_vhost_http_remux_fast_cache("test.com"));
    }

    // Test default value when fast_cache directive exists but is empty
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{http_remux{fast_cache;}}"));

        EXPECT_EQ(0, conf.get_vhost_http_remux_fast_cache("test.com"));
    }

    // Test fast_cache = 30 (seconds)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{http_remux{fast_cache 30;}}"));

        EXPECT_EQ(30 * SRS_UTIME_SECONDS, conf.get_vhost_http_remux_fast_cache("test.com"));
    }

    // Test fast_cache = 0.5 (half second)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{http_remux{fast_cache 0.5;}}"));

        EXPECT_EQ(srs_utime_t(0.5 * SRS_UTIME_SECONDS), conf.get_vhost_http_remux_fast_cache("test.com"));
    }

    // Test fast_cache = 120 (2 minutes)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{http_remux{fast_cache 120;}}"));

        EXPECT_EQ(120 * SRS_UTIME_SECONDS, conf.get_vhost_http_remux_fast_cache("test.com"));
    }
}

VOID TEST(ConfigVhostHttpRemuxTest, CheckVhostHttpRemuxDropIfNotMatch)
{
    srs_error_t err;

    // Test default value when no vhost exists
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        EXPECT_TRUE(conf.get_vhost_http_remux_drop_if_not_match("__defaultVhost__"));
    }

    // Test default value when vhost exists but no http_remux section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{}"));

        EXPECT_TRUE(conf.get_vhost_http_remux_drop_if_not_match("test.com"));
    }

    // Test default value when http_remux section exists but no drop_if_not_match directive
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{http_remux{}}"));

        EXPECT_TRUE(conf.get_vhost_http_remux_drop_if_not_match("test.com"));
    }

    // Test default value when drop_if_not_match directive exists but is empty
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{http_remux{drop_if_not_match;}}"));

        EXPECT_TRUE(conf.get_vhost_http_remux_drop_if_not_match("test.com"));
    }

    // Test drop_if_not_match = on
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{http_remux{drop_if_not_match on;}}"));

        EXPECT_TRUE(conf.get_vhost_http_remux_drop_if_not_match("test.com"));
    }

    // Test drop_if_not_match = off (SRS_CONF_PREFER_TRUE: only "off" is false)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{http_remux{drop_if_not_match off;}}"));

        EXPECT_FALSE(conf.get_vhost_http_remux_drop_if_not_match("test.com")); // "off" is false
    }

    // Test drop_if_not_match = true (SRS_CONF_PREFER_TRUE: only "off" is false)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{http_remux{drop_if_not_match true;}}"));

        EXPECT_TRUE(conf.get_vhost_http_remux_drop_if_not_match("test.com")); // "true" != "off", so it's true
    }

    // Test drop_if_not_match = false (SRS_CONF_PREFER_TRUE: only "off" is false)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{http_remux{drop_if_not_match false;}}"));

        EXPECT_TRUE(conf.get_vhost_http_remux_drop_if_not_match("test.com")); // "false" != "off", so it's true
    }
}

VOID TEST(ConfigVhostHttpRemuxTest, CheckVhostHttpRemuxHasAudio)
{
    srs_error_t err;

    // Test default value when no vhost exists
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        EXPECT_TRUE(conf.get_vhost_http_remux_has_audio("__defaultVhost__"));
    }

    // Test default value when vhost exists but no http_remux section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{}"));

        EXPECT_TRUE(conf.get_vhost_http_remux_has_audio("test.com"));
    }

    // Test default value when http_remux section exists but no has_audio directive
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{http_remux{}}"));

        EXPECT_TRUE(conf.get_vhost_http_remux_has_audio("test.com"));
    }

    // Test default value when has_audio directive exists but is empty
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{http_remux{has_audio;}}"));

        EXPECT_TRUE(conf.get_vhost_http_remux_has_audio("test.com"));
    }

    // Test has_audio = on
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{http_remux{has_audio on;}}"));

        EXPECT_TRUE(conf.get_vhost_http_remux_has_audio("test.com"));
    }

    // Test has_audio = off (SRS_CONF_PREFER_TRUE: only "off" is false)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{http_remux{has_audio off;}}"));

        EXPECT_FALSE(conf.get_vhost_http_remux_has_audio("test.com")); // "off" is false
    }

    // Test has_audio = true (SRS_CONF_PREFER_TRUE: only "off" is false)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{http_remux{has_audio true;}}"));

        EXPECT_TRUE(conf.get_vhost_http_remux_has_audio("test.com")); // "true" != "off", so it's true
    }

    // Test has_audio = false (SRS_CONF_PREFER_TRUE: only "off" is false)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{http_remux{has_audio false;}}"));

        EXPECT_TRUE(conf.get_vhost_http_remux_has_audio("test.com")); // "false" != "off", so it's true
    }
}

VOID TEST(ConfigVhostHttpRemuxTest, CheckVhostHttpRemuxHasVideo)
{
    srs_error_t err;

    // Test default value when no vhost exists
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        EXPECT_TRUE(conf.get_vhost_http_remux_has_video("__defaultVhost__"));
    }

    // Test default value when vhost exists but no http_remux section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{}"));

        EXPECT_TRUE(conf.get_vhost_http_remux_has_video("test.com"));
    }

    // Test default value when http_remux section exists but no has_video directive
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{http_remux{}}"));

        EXPECT_TRUE(conf.get_vhost_http_remux_has_video("test.com"));
    }

    // Test default value when has_video directive exists but is empty
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{http_remux{has_video;}}"));

        EXPECT_TRUE(conf.get_vhost_http_remux_has_video("test.com"));
    }

    // Test has_video = on
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{http_remux{has_video on;}}"));

        EXPECT_TRUE(conf.get_vhost_http_remux_has_video("test.com"));
    }

    // Test has_video = off (SRS_CONF_PREFER_TRUE: only "off" is false)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{http_remux{has_video off;}}"));

        EXPECT_FALSE(conf.get_vhost_http_remux_has_video("test.com")); // "off" is false
    }

    // Test has_video = true (SRS_CONF_PREFER_TRUE: only "off" is false)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{http_remux{has_video true;}}"));

        EXPECT_TRUE(conf.get_vhost_http_remux_has_video("test.com")); // "true" != "off", so it's true
    }

    // Test has_video = false (SRS_CONF_PREFER_TRUE: only "off" is false)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{http_remux{has_video false;}}"));

        EXPECT_TRUE(conf.get_vhost_http_remux_has_video("test.com")); // "false" != "off", so it's true
    }
}

VOID TEST(ConfigVhostHttpRemuxTest, CheckVhostHttpRemuxGuessHasAv)
{
    srs_error_t err;

    // Test default value when no vhost exists
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        EXPECT_TRUE(conf.get_vhost_http_remux_guess_has_av("__defaultVhost__"));
    }

    // Test default value when vhost exists but no http_remux section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{}"));

        EXPECT_TRUE(conf.get_vhost_http_remux_guess_has_av("test.com"));
    }

    // Test default value when http_remux section exists but no guess_has_av directive
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{http_remux{}}"));

        EXPECT_TRUE(conf.get_vhost_http_remux_guess_has_av("test.com"));
    }

    // Test default value when guess_has_av directive exists but is empty
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{http_remux{guess_has_av;}}"));

        EXPECT_TRUE(conf.get_vhost_http_remux_guess_has_av("test.com"));
    }

    // Test guess_has_av = on
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{http_remux{guess_has_av on;}}"));

        EXPECT_TRUE(conf.get_vhost_http_remux_guess_has_av("test.com"));
    }

    // Test guess_has_av = off (SRS_CONF_PREFER_TRUE: only "off" is false)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{http_remux{guess_has_av off;}}"));

        EXPECT_FALSE(conf.get_vhost_http_remux_guess_has_av("test.com")); // "off" is false
    }

    // Test guess_has_av = true (SRS_CONF_PREFER_TRUE: only "off" is false)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{http_remux{guess_has_av true;}}"));

        EXPECT_TRUE(conf.get_vhost_http_remux_guess_has_av("test.com")); // "true" != "off", so it's true
    }

    // Test guess_has_av = false (SRS_CONF_PREFER_TRUE: only "off" is false)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{http_remux{guess_has_av false;}}"));

        EXPECT_TRUE(conf.get_vhost_http_remux_guess_has_av("test.com")); // "false" != "off", so it's true
    }
}

VOID TEST(ConfigVhostHttpRemuxTest, CheckVhostHttpRemuxMount)
{
    srs_error_t err;

    // Test default value when no vhost exists
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        EXPECT_STREQ("[vhost]/[app]/[stream].flv", conf.get_vhost_http_remux_mount("__defaultVhost__").c_str());
    }

    // Test default value when vhost exists but no http_remux section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{}"));

        EXPECT_STREQ("[vhost]/[app]/[stream].flv", conf.get_vhost_http_remux_mount("test.com").c_str());
    }

    // Test default value when http_remux section exists but no mount directive
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{http_remux{}}"));

        EXPECT_STREQ("[vhost]/[app]/[stream].flv", conf.get_vhost_http_remux_mount("test.com").c_str());
    }

    // Test default value when mount directive exists but is empty
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{http_remux{mount;}}"));

        EXPECT_STREQ("[vhost]/[app]/[stream].flv", conf.get_vhost_http_remux_mount("test.com").c_str());
    }

    // Test custom mount path
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{http_remux{mount /live/;}}"));

        EXPECT_STREQ("/live/", conf.get_vhost_http_remux_mount("test.com").c_str());
    }

    // Test root mount path
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{http_remux{mount /;}}"));

        EXPECT_STREQ("/", conf.get_vhost_http_remux_mount("test.com").c_str());
    }

    // Test mount path without trailing slash
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{http_remux{mount /stream;}}"));

        EXPECT_STREQ("/stream", conf.get_vhost_http_remux_mount("test.com").c_str());
    }
}

VOID TEST(ConfigHeartbeatTest, CheckHeartbeatEnabled)
{
    srs_error_t err;

    // Test default value when no heartbeat section exists
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        EXPECT_FALSE(conf.get_heartbeat_enabled());
    }

    // Test default value when heartbeat section exists but no enabled directive
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "heartbeat{}"));

        EXPECT_FALSE(conf.get_heartbeat_enabled());
    }

    // Test default value when enabled directive exists but is empty
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "heartbeat{enabled;}"));

        EXPECT_FALSE(conf.get_heartbeat_enabled());
    }

    // Test enabled = on
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "heartbeat{enabled on;}"));

        EXPECT_TRUE(conf.get_heartbeat_enabled());
    }

    // Test enabled = off
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "heartbeat{enabled off;}"));

        EXPECT_FALSE(conf.get_heartbeat_enabled());
    }

    // Test enabled = true (SRS_CONF_PREFER_FALSE: only "on" is true)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "heartbeat{enabled true;}"));

        EXPECT_FALSE(conf.get_heartbeat_enabled()); // "true" != "on", so it's false
    }

    // Test enabled = false (SRS_CONF_PREFER_FALSE: only "on" is true)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "heartbeat{enabled false;}"));

        EXPECT_FALSE(conf.get_heartbeat_enabled()); // "false" != "on", so it's false
    }
}

VOID TEST(ConfigHeartbeatTest, CheckHeartbeatUrl)
{
    srs_error_t err;

    // Test default value when no heartbeat section exists
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        EXPECT_STREQ("http://127.0.0.1:8085/api/v1/servers", conf.get_heartbeat_url().c_str());
    }

    // Test default value when heartbeat section exists but no url directive
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "heartbeat{}"));

        EXPECT_STREQ("http://127.0.0.1:8085/api/v1/servers", conf.get_heartbeat_url().c_str());
    }

    // Test default value when url directive exists but is empty
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "heartbeat{url;}"));

        EXPECT_STREQ("http://127.0.0.1:8085/api/v1/servers", conf.get_heartbeat_url().c_str());
    }

    // Test custom URL
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "heartbeat{url http://127.0.0.1:8085/api/v1/heartbeat;}"));

        EXPECT_STREQ("http://127.0.0.1:8085/api/v1/heartbeat", conf.get_heartbeat_url().c_str());
    }

    // Test HTTPS URL
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "heartbeat{url https://example.com/heartbeat;}"));

        EXPECT_STREQ("https://example.com/heartbeat", conf.get_heartbeat_url().c_str());
    }

    // Test URL with query parameters
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "heartbeat{url http://example.com/api?token=abc123;}"));

        EXPECT_STREQ("http://example.com/api?token=abc123", conf.get_heartbeat_url().c_str());
    }
}

VOID TEST(ConfigHeartbeatTest, CheckHeartbeatDeviceId)
{
    srs_error_t err;

    // Test default value when no heartbeat section exists
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        EXPECT_STREQ("", conf.get_heartbeat_device_id().c_str());
    }

    // Test default value when heartbeat section exists but no device_id directive
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "heartbeat{}"));

        EXPECT_STREQ("", conf.get_heartbeat_device_id().c_str());
    }

    // Test default value when device_id directive exists but is empty
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "heartbeat{device_id;}"));

        EXPECT_STREQ("", conf.get_heartbeat_device_id().c_str());
    }

    // Test custom device ID
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "heartbeat{device_id srs-server-001;}"));

        EXPECT_STREQ("srs-server-001", conf.get_heartbeat_device_id().c_str());
    }

    // Test numeric device ID
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "heartbeat{device_id 12345;}"));

        EXPECT_STREQ("12345", conf.get_heartbeat_device_id().c_str());
    }

    // Test UUID-like device ID
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "heartbeat{device_id 550e8400-e29b-41d4-a716-446655440000;}"));

        EXPECT_STREQ("550e8400-e29b-41d4-a716-446655440000", conf.get_heartbeat_device_id().c_str());
    }
}

VOID TEST(ConfigHeartbeatTest, CheckHeartbeatSummaries)
{
    srs_error_t err;

    // Test default value when no heartbeat section exists
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        EXPECT_FALSE(conf.get_heartbeat_summaries());
    }

    // Test default value when heartbeat section exists but no summaries directive
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "heartbeat{}"));

        EXPECT_FALSE(conf.get_heartbeat_summaries());
    }

    // Test default value when summaries directive exists but is empty
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "heartbeat{summaries;}"));

        EXPECT_FALSE(conf.get_heartbeat_summaries());
    }

    // Test summaries = on
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "heartbeat{summaries on;}"));

        EXPECT_TRUE(conf.get_heartbeat_summaries());
    }

    // Test summaries = off
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "heartbeat{summaries off;}"));

        EXPECT_FALSE(conf.get_heartbeat_summaries());
    }

    // Test summaries = true (SRS_CONF_PREFER_FALSE: only "on" is true)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "heartbeat{summaries true;}"));

        EXPECT_FALSE(conf.get_heartbeat_summaries()); // "true" != "on", so it's false
    }

    // Test summaries = false (SRS_CONF_PREFER_FALSE: only "on" is true)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "heartbeat{summaries false;}"));

        EXPECT_FALSE(conf.get_heartbeat_summaries()); // "false" != "on", so it's false
    }
}

VOID TEST(ConfigHeartbeatTest, CheckHeartbeatPorts)
{
    srs_error_t err;

    // Test default value when no heartbeat section exists
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        EXPECT_FALSE(conf.get_heartbeat_ports());
    }

    // Test default value when heartbeat section exists but no ports directive
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "heartbeat{}"));

        EXPECT_FALSE(conf.get_heartbeat_ports());
    }

    // Test default value when ports directive exists but is empty
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "heartbeat{ports;}"));

        EXPECT_FALSE(conf.get_heartbeat_ports());
    }

    // Test ports = on
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "heartbeat{ports on;}"));

        EXPECT_TRUE(conf.get_heartbeat_ports());
    }

    // Test ports = off
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "heartbeat{ports off;}"));

        EXPECT_FALSE(conf.get_heartbeat_ports());
    }

    // Test ports = true (SRS_CONF_PREFER_FALSE: only "on" is true)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "heartbeat{ports true;}"));

        EXPECT_FALSE(conf.get_heartbeat_ports()); // "true" != "on", so it's false
    }

    // Test ports = false (SRS_CONF_PREFER_FALSE: only "on" is true)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "heartbeat{ports false;}"));

        EXPECT_FALSE(conf.get_heartbeat_ports()); // "false" != "on", so it's false
    }

    // Test environment variable override
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "heartbeat{ports off;}"));

        SrsSetEnvConfig(conf, heartbeat_ports, "SRS_HEARTBEAT_PORTS", "on");
        EXPECT_TRUE(conf.get_heartbeat_ports());
    }

    // Test environment variable override with config file precedence when env is empty
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "heartbeat{ports on;}"));

        SrsSetEnvConfig(conf, heartbeat_ports, "SRS_HEARTBEAT_PORTS", "");
        EXPECT_TRUE(conf.get_heartbeat_ports());
    }
}

VOID TEST(ConfigHeartbeatTest, CheckHeartbeatCompleteConfiguration)
{
    srs_error_t err;

    // Test complete heartbeat configuration
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF
                                              "heartbeat{"
                                              "enabled on;"
                                              "url http://127.0.0.1:8085/api/v1/heartbeat;"
                                              "device_id srs-server-001;"
                                              "summaries on;"
                                              "ports on;"
                                              "}"));

        EXPECT_TRUE(conf.get_heartbeat_enabled());
        EXPECT_STREQ("http://127.0.0.1:8085/api/v1/heartbeat", conf.get_heartbeat_url().c_str());
        EXPECT_STREQ("srs-server-001", conf.get_heartbeat_device_id().c_str());
        EXPECT_TRUE(conf.get_heartbeat_summaries());
        EXPECT_TRUE(conf.get_heartbeat_ports());
    }

    // Test heartbeat configuration with mixed values (SRS_CONF_PREFER_FALSE behavior)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF
                                              "heartbeat{"
                                              "enabled true;"
                                              "url https://example.com/heartbeat?token=abc123;"
                                              "device_id 550e8400-e29b-41d4-a716-446655440000;"
                                              "summaries false;"
                                              "ports true;"
                                              "}"));

        EXPECT_FALSE(conf.get_heartbeat_enabled()); // "true" != "on", so it's false
        EXPECT_STREQ("https://example.com/heartbeat?token=abc123", conf.get_heartbeat_url().c_str());
        EXPECT_STREQ("550e8400-e29b-41d4-a716-446655440000", conf.get_heartbeat_device_id().c_str());
        EXPECT_FALSE(conf.get_heartbeat_summaries()); // "false" != "on", so it's false
        EXPECT_FALSE(conf.get_heartbeat_ports());     // "true" != "on", so it's false
    }

    // Test heartbeat configuration with all defaults (disabled)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF
                                              "heartbeat{"
                                              "enabled off;"
                                              "summaries off;"
                                              "ports off;"
                                              "}"));

        EXPECT_FALSE(conf.get_heartbeat_enabled());
        EXPECT_STREQ("http://127.0.0.1:8085/api/v1/servers", conf.get_heartbeat_url().c_str()); // Default URL
        EXPECT_STREQ("", conf.get_heartbeat_device_id().c_str());
        EXPECT_FALSE(conf.get_heartbeat_summaries());
        EXPECT_FALSE(conf.get_heartbeat_ports());
    }
}

VOID TEST(ConfigStatsTest, CheckStatsEnabled)
{
    srs_error_t err;

    // Test default value when no stats section exists
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        EXPECT_TRUE(conf.get_stats_enabled()); // Default is true
    }

    // Test default value when stats section exists but no enabled directive
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "stats{}"));

        EXPECT_TRUE(conf.get_stats_enabled()); // Default is true
    }

    // Test default value when enabled directive exists but is empty
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "stats{enabled;}"));

        EXPECT_TRUE(conf.get_stats_enabled()); // Default is true
    }

    // Test enabled = on
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "stats{enabled on;}"));

        EXPECT_TRUE(conf.get_stats_enabled());
    }

    // Test enabled = off (SRS_CONF_PREFER_TRUE: only "off" is false)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "stats{enabled off;}"));

        EXPECT_FALSE(conf.get_stats_enabled()); // "off" is false
    }

    // Test enabled = true (SRS_CONF_PREFER_TRUE: only "off" is false)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "stats{enabled true;}"));

        EXPECT_TRUE(conf.get_stats_enabled()); // "true" != "off", so it's true
    }

    // Test enabled = false (SRS_CONF_PREFER_TRUE: only "off" is false)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "stats{enabled false;}"));

        EXPECT_TRUE(conf.get_stats_enabled()); // "false" != "off", so it's true
    }

    // Test enabled = 0 (SRS_CONF_PREFER_TRUE: only "off" is false)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "stats{enabled 0;}"));

        EXPECT_TRUE(conf.get_stats_enabled()); // "0" != "off", so it's true
    }

    // Test enabled = 1 (SRS_CONF_PREFER_TRUE: only "off" is false)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "stats{enabled 1;}"));

        EXPECT_TRUE(conf.get_stats_enabled()); // "1" != "off", so it's true
    }
}

VOID TEST(ConfigRtmpsTest, CheckRtmpsEnabled)
{
    srs_error_t err;

    // Test default value when no rtmps section exists
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        EXPECT_FALSE(conf.get_rtmps_enabled()); // Default is false
    }

    // Test default value when rtmps section exists but no enabled directive
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtmps{}"));

        EXPECT_FALSE(conf.get_rtmps_enabled()); // Default is false
    }

    // Test default value when enabled directive exists but is empty
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtmps{enabled;}"));

        EXPECT_FALSE(conf.get_rtmps_enabled()); // Default is false
    }

    // Test enabled = on
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtmps{enabled on;}"));

        EXPECT_TRUE(conf.get_rtmps_enabled());
    }

    // Test enabled = off
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtmps{enabled off;}"));

        EXPECT_FALSE(conf.get_rtmps_enabled());
    }

    // Test enabled = true (SRS_CONF_PREFER_FALSE: only "on" is true)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtmps{enabled true;}"));

        EXPECT_FALSE(conf.get_rtmps_enabled()); // "true" != "on", so it's false
    }

    // Test enabled = false (SRS_CONF_PREFER_FALSE: only "on" is true)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtmps{enabled false;}"));

        EXPECT_FALSE(conf.get_rtmps_enabled()); // "false" != "on", so it's false
    }

    // Test enabled = 1 (SRS_CONF_PREFER_FALSE: only "on" is true)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtmps{enabled 1;}"));

        EXPECT_FALSE(conf.get_rtmps_enabled()); // "1" != "on", so it's false
    }

    // Test enabled = 0 (SRS_CONF_PREFER_FALSE: only "on" is true)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtmps{enabled 0;}"));

        EXPECT_FALSE(conf.get_rtmps_enabled()); // "0" != "on", so it's false
    }

    // Test environment variable override
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtmps{enabled off;}"));

        SrsSetEnvConfig(conf, rtmps_enabled, "SRS_RTMPS_ENABLED", "on");
        EXPECT_TRUE(conf.get_rtmps_enabled());
    }

    // Test environment variable override with config file precedence when env is empty
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtmps{enabled on;}"));

        SrsSetEnvConfig(conf, rtmps_enabled, "SRS_RTMPS_ENABLED", "");
        EXPECT_TRUE(conf.get_rtmps_enabled());
    }

    // Test environment variable override with false value
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtmps{enabled on;}"));

        SrsSetEnvConfig(conf, rtmps_enabled, "SRS_RTMPS_ENABLED", "off");
        EXPECT_FALSE(conf.get_rtmps_enabled());
    }
}

VOID TEST(ConfigRtmpsTest, CheckRtmpsListen)
{
    srs_error_t err;

    // Test default value when no rtmps section exists
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        vector<string> listens = conf.get_rtmps_listen();
        EXPECT_EQ(0, (int)listens.size()); // Empty by default
    }

    // Test default value when rtmps section exists but no listen directive
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtmps{}"));

        vector<string> listens = conf.get_rtmps_listen();
        EXPECT_EQ(0, (int)listens.size()); // Empty by default
    }

    // Test default value when listen directive exists but has no arguments
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtmps{listen;}"));

        vector<string> listens = conf.get_rtmps_listen();
        EXPECT_EQ(0, (int)listens.size()); // Empty when no args
    }

    // Test single listen port
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtmps{listen 1936;}"));

        vector<string> listens = conf.get_rtmps_listen();
        EXPECT_EQ(1, (int)listens.size());
        EXPECT_STREQ("1936", listens.at(0).c_str());
    }

    // Test multiple listen ports
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtmps{listen 1936 1937 1938;}"));

        vector<string> listens = conf.get_rtmps_listen();
        EXPECT_EQ(3, (int)listens.size());
        EXPECT_STREQ("1936", listens.at(0).c_str());
        EXPECT_STREQ("1937", listens.at(1).c_str());
        EXPECT_STREQ("1938", listens.at(2).c_str());
    }

    // Test IPv6 address
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtmps{listen [::1]:1936;}"));

        vector<string> listens = conf.get_rtmps_listen();
        EXPECT_EQ(1, (int)listens.size());
        EXPECT_STREQ("[::1]:1936", listens.at(0).c_str());
    }

    // Test IPv4 address with port
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtmps{listen 127.0.0.1:1936;}"));

        vector<string> listens = conf.get_rtmps_listen();
        EXPECT_EQ(1, (int)listens.size());
        EXPECT_STREQ("127.0.0.1:1936", listens.at(0).c_str());
    }
}

VOID TEST(ConfigRtmpsTest, CheckRtmpsListenEnvOverride)
{
    srs_error_t err;

    // Test environment variable override with single port
    if (true) {
        MockSrsConfig conf;

        SrsSetEnvConfig(conf, rtmps_listen, "SRS_RTMPS_LISTEN", "1936");

        vector<string> listens = conf.get_rtmps_listen();
        EXPECT_EQ(1, (int)listens.size());
        EXPECT_STREQ("1936", listens.at(0).c_str());
    }

    // Test environment variable override with multiple ports
    if (true) {
        MockSrsConfig conf;

        SrsSetEnvConfig(conf, rtmps_listen, "SRS_RTMPS_LISTEN", "1936 1937 1938");

        vector<string> listens = conf.get_rtmps_listen();
        EXPECT_EQ(3, (int)listens.size());
        EXPECT_STREQ("1936", listens.at(0).c_str());
        EXPECT_STREQ("1937", listens.at(1).c_str());
        EXPECT_STREQ("1938", listens.at(2).c_str());
    }

    // Test environment variable override takes precedence over config file
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtmps{listen 1936;}"));

        SrsSetEnvConfig(conf, rtmps_listen, "SRS_RTMPS_LISTEN", "1937");

        vector<string> listens = conf.get_rtmps_listen();
        EXPECT_EQ(1, (int)listens.size());
        EXPECT_STREQ("1937", listens.at(0).c_str());
    }

    // Test empty environment variable falls back to config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtmps{listen 1936;}"));

        SrsSetEnvConfig(conf, rtmps_listen, "SRS_RTMPS_LISTEN", "");

        vector<string> listens = conf.get_rtmps_listen();
        EXPECT_EQ(1, (int)listens.size());
        EXPECT_STREQ("1936", listens.at(0).c_str());
    }
}

VOID TEST(ConfigRtmpsTest, CheckRtmpsSslKey)
{
    srs_error_t err;

    // Test default value when no rtmps section exists
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        EXPECT_STREQ("./conf/server.key", conf.get_rtmps_ssl_key().c_str());
    }

    // Test default value when rtmps section exists but no key directive
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtmps{}"));

        EXPECT_STREQ("./conf/server.key", conf.get_rtmps_ssl_key().c_str());
    }

    // Test behavior when key directive exists but is empty (returns empty string, not default)
    // Note: This is the actual behavior of the current implementation
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtmps{key;}"));

        EXPECT_STREQ("", conf.get_rtmps_ssl_key().c_str()); // Returns empty string, not default
    }

    // Test custom key path
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtmps{key /path/to/custom.key;}"));

        EXPECT_STREQ("/path/to/custom.key", conf.get_rtmps_ssl_key().c_str());
    }

    // Test relative key path
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtmps{key ./ssl/server.key;}"));

        EXPECT_STREQ("./ssl/server.key", conf.get_rtmps_ssl_key().c_str());
    }

    // Test environment variable override
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtmps{key ./conf/server.key;}"));

        SrsSetEnvConfig(conf, rtmps_key, "SRS_RTMPS_KEY", "/custom/path/server.key");
        EXPECT_STREQ("/custom/path/server.key", conf.get_rtmps_ssl_key().c_str());
    }

    // Test environment variable override with empty value falls back to config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtmps{key ./ssl/custom.key;}"));

        SrsSetEnvConfig(conf, rtmps_key, "SRS_RTMPS_KEY", "");
        EXPECT_STREQ("./ssl/custom.key", conf.get_rtmps_ssl_key().c_str());
    }
}

VOID TEST(ConfigRtmpsTest, CheckRtmpsSslCert)
{
    srs_error_t err;

    // Test default value when no rtmps section exists
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        EXPECT_STREQ("./conf/server.crt", conf.get_rtmps_ssl_cert().c_str());
    }

    // Test default value when rtmps section exists but no cert directive
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtmps{}"));

        EXPECT_STREQ("./conf/server.crt", conf.get_rtmps_ssl_cert().c_str());
    }

    // Test behavior when cert directive exists but is empty (returns empty string, not default)
    // Note: This is the actual behavior of the current implementation
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtmps{cert;}"));

        EXPECT_STREQ("", conf.get_rtmps_ssl_cert().c_str()); // Returns empty string, not default
    }

    // Test custom cert path
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtmps{cert /path/to/custom.crt;}"));

        EXPECT_STREQ("/path/to/custom.crt", conf.get_rtmps_ssl_cert().c_str());
    }

    // Test relative cert path
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtmps{cert ./ssl/server.crt;}"));

        EXPECT_STREQ("./ssl/server.crt", conf.get_rtmps_ssl_cert().c_str());
    }

    // Test environment variable override
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtmps{cert ./conf/server.crt;}"));

        SrsSetEnvConfig(conf, rtmps_cert, "SRS_RTMPS_CERT", "/custom/path/server.crt");
        EXPECT_STREQ("/custom/path/server.crt", conf.get_rtmps_ssl_cert().c_str());
    }

    // Test environment variable override with empty value falls back to config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtmps{cert ./ssl/custom.crt;}"));

        SrsSetEnvConfig(conf, rtmps_cert, "SRS_RTMPS_CERT", "");
        EXPECT_STREQ("./ssl/custom.crt", conf.get_rtmps_ssl_cert().c_str());
    }
}

VOID TEST(ConfigRtmpsTest, CheckRtmpsCompleteConfiguration)
{
    srs_error_t err;

    // Test complete RTMPS configuration
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF
                                              "rtmps{"
                                              "enabled on;"
                                              "listen 1936 1937;"
                                              "key /path/to/server.key;"
                                              "cert /path/to/server.crt;"
                                              "}"));

        EXPECT_TRUE(conf.get_rtmps_enabled());

        vector<string> listens = conf.get_rtmps_listen();
        EXPECT_EQ(2, (int)listens.size());
        EXPECT_STREQ("1936", listens.at(0).c_str());
        EXPECT_STREQ("1937", listens.at(1).c_str());

        EXPECT_STREQ("/path/to/server.key", conf.get_rtmps_ssl_key().c_str());
        EXPECT_STREQ("/path/to/server.crt", conf.get_rtmps_ssl_cert().c_str());
    }

    // Test RTMPS configuration with environment variable overrides
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF
                                              "rtmps{"
                                              "enabled off;"
                                              "listen 1936;"
                                              "key ./conf/server.key;"
                                              "cert ./conf/server.crt;"
                                              "}"));

        SrsSetEnvConfig(conf, rtmps_enabled, "SRS_RTMPS_ENABLED", "on");
        SrsSetEnvConfig(conf, rtmps_listen, "SRS_RTMPS_LISTEN", "1937 1938");
        SrsSetEnvConfig(conf, rtmps_key, "SRS_RTMPS_KEY", "/env/server.key");
        SrsSetEnvConfig(conf, rtmps_cert, "SRS_RTMPS_CERT", "/env/server.crt");

        EXPECT_TRUE(conf.get_rtmps_enabled());

        vector<string> listens = conf.get_rtmps_listen();
        EXPECT_EQ(2, (int)listens.size());
        EXPECT_STREQ("1937", listens.at(0).c_str());
        EXPECT_STREQ("1938", listens.at(1).c_str());

        EXPECT_STREQ("/env/server.key", conf.get_rtmps_ssl_key().c_str());
        EXPECT_STREQ("/env/server.crt", conf.get_rtmps_ssl_cert().c_str());
    }

    // Test RTMPS configuration with defaults (disabled)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF
                                              "rtmps{"
                                              "enabled off;"
                                              "}"));

        EXPECT_FALSE(conf.get_rtmps_enabled());

        vector<string> listens = conf.get_rtmps_listen();
        EXPECT_EQ(0, (int)listens.size()); // Empty by default

        EXPECT_STREQ("./conf/server.key", conf.get_rtmps_ssl_key().c_str());  // Default key
        EXPECT_STREQ("./conf/server.crt", conf.get_rtmps_ssl_cert().c_str()); // Default cert
    }

    // Test RTMPS configuration with mixed IPv4/IPv6 addresses
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF
                                              "rtmps{"
                                              "enabled on;"
                                              "listen 127.0.0.1:1936 [::1]:1937 1938;"
                                              "key ./ssl/mixed.key;"
                                              "cert ./ssl/mixed.crt;"
                                              "}"));

        EXPECT_TRUE(conf.get_rtmps_enabled());

        vector<string> listens = conf.get_rtmps_listen();
        EXPECT_EQ(3, (int)listens.size());
        EXPECT_STREQ("127.0.0.1:1936", listens.at(0).c_str());
        EXPECT_STREQ("[::1]:1937", listens.at(1).c_str());
        EXPECT_STREQ("1938", listens.at(2).c_str());

        EXPECT_STREQ("./ssl/mixed.key", conf.get_rtmps_ssl_key().c_str());
        EXPECT_STREQ("./ssl/mixed.crt", conf.get_rtmps_ssl_cert().c_str());
    }
}

VOID TEST(ConfigRtmpsTest, CheckRtmpsEdgeCases)
{
    srs_error_t err;

    // Test RTMPS with only key specified (cert uses default)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtmps{key /custom/server.key;}"));

        EXPECT_STREQ("/custom/server.key", conf.get_rtmps_ssl_key().c_str());
        EXPECT_STREQ("./conf/server.crt", conf.get_rtmps_ssl_cert().c_str()); // Default cert
    }

    // Test RTMPS with only cert specified (key uses default)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtmps{cert /custom/server.crt;}"));

        EXPECT_STREQ("./conf/server.key", conf.get_rtmps_ssl_key().c_str()); // Default key
        EXPECT_STREQ("/custom/server.crt", conf.get_rtmps_ssl_cert().c_str());
    }

    // Test RTMPS with only listen specified (other values use defaults)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtmps{listen 1936;}"));

        EXPECT_FALSE(conf.get_rtmps_enabled()); // Default is false

        vector<string> listens = conf.get_rtmps_listen();
        EXPECT_EQ(1, (int)listens.size());
        EXPECT_STREQ("1936", listens.at(0).c_str());

        EXPECT_STREQ("./conf/server.key", conf.get_rtmps_ssl_key().c_str());  // Default key
        EXPECT_STREQ("./conf/server.crt", conf.get_rtmps_ssl_cert().c_str()); // Default cert
    }

    // Test RTMPS with empty section (all defaults)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtmps{}"));

        EXPECT_FALSE(conf.get_rtmps_enabled()); // Default is false

        vector<string> listens = conf.get_rtmps_listen();
        EXPECT_EQ(0, (int)listens.size()); // Empty by default

        EXPECT_STREQ("./conf/server.key", conf.get_rtmps_ssl_key().c_str());  // Default key
        EXPECT_STREQ("./conf/server.crt", conf.get_rtmps_ssl_cert().c_str()); // Default cert
    }

    // Test environment variables with no config section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        SrsSetEnvConfig(conf, rtmps_enabled, "SRS_RTMPS_ENABLED", "on");
        SrsSetEnvConfig(conf, rtmps_listen, "SRS_RTMPS_LISTEN", "1936");
        SrsSetEnvConfig(conf, rtmps_key, "SRS_RTMPS_KEY", "/env/server.key");
        SrsSetEnvConfig(conf, rtmps_cert, "SRS_RTMPS_CERT", "/env/server.crt");

        EXPECT_TRUE(conf.get_rtmps_enabled());

        vector<string> listens = conf.get_rtmps_listen();
        EXPECT_EQ(1, (int)listens.size());
        EXPECT_STREQ("1936", listens.at(0).c_str());

        EXPECT_STREQ("/env/server.key", conf.get_rtmps_ssl_key().c_str());
        EXPECT_STREQ("/env/server.crt", conf.get_rtmps_ssl_cert().c_str());
    }
}

VOID TEST(ConfigEnvTest, CheckEnvValuesVhostCluster)
{
    srs_error_t err;

    // SRS_VHOST_CLUSTER_MODE and SRS_VHOST_CLUSTER_ORIGIN make the vhost an edge without a config file.
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost __defaultVhost__{}"));

        SrsSetEnvConfig(conf, mode, "SRS_VHOST_CLUSTER_MODE", "remote");
        SrsSetEnvConfig(conf, origin, "SRS_VHOST_CLUSTER_ORIGIN", "127.0.0.1:19360 127.0.0.1:19361");
        EXPECT_TRUE(conf.get_vhost_is_edge("__defaultVhost__"));

        SrsConfDirective *origins = conf.get_vhost_edge_origin("__defaultVhost__");
        ASSERT_TRUE(origins != NULL);
        EXPECT_EQ(2, (int)origins->args_.size());
        EXPECT_STREQ("127.0.0.1:19360", origins->arg0().c_str());
        EXPECT_STREQ("127.0.0.1:19361", origins->arg1().c_str());
    }

    // The env overwrites the cluster of the config file.
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost __defaultVhost__{cluster{mode remote; origin 127.0.0.1:1935;}}"));

        SrsSetEnvConfig(conf, mode, "SRS_VHOST_CLUSTER_MODE", "local");
        SrsSetEnvConfig(conf, origin, "SRS_VHOST_CLUSTER_ORIGIN", "127.0.0.1:19360");
        EXPECT_FALSE(conf.get_vhost_is_edge("__defaultVhost__"));

        SrsConfDirective *origins = conf.get_vhost_edge_origin("__defaultVhost__");
        ASSERT_TRUE(origins != NULL);
        EXPECT_EQ(1, (int)origins->args_.size());
        EXPECT_STREQ("127.0.0.1:19360", origins->arg0().c_str());
    }

    // Without env, the cluster of the config file is used.
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost __defaultVhost__{cluster{mode remote; origin 127.0.0.1:1935;}}"));

        EXPECT_TRUE(conf.get_vhost_is_edge("__defaultVhost__"));
        SrsConfDirective *origins = conf.get_vhost_edge_origin("__defaultVhost__");
        ASSERT_TRUE(origins != NULL);
        EXPECT_STREQ("127.0.0.1:1935", origins->arg0().c_str());
    }
}

// SRS_VHOST_CLUSTER_TOKEN_TRAVERSE and SRS_VHOST_CLUSTER_PROTOCOL set the token traverse and the protocol of
// an edge to pull from its origin, for every vhost, overwriting the config file.
VOID TEST(ConfigEnvTest, CheckEnvValuesVhostClusterEdge)
{
    srs_error_t err;

    // The defaults: no token traverse, and RTMP to the origin.
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));
        EXPECT_FALSE(conf.get_vhost_edge_token_traverse("__defaultVhost__"));
        EXPECT_STREQ("rtmp", conf.get_vhost_edge_protocol("__defaultVhost__").c_str());
    }

    // The env without a vhost in the config file.
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        SrsSetEnvConfig(conf, token_traverse, "SRS_VHOST_CLUSTER_TOKEN_TRAVERSE", "on");
        SrsSetEnvConfig(conf, protocol, "SRS_VHOST_CLUSTER_PROTOCOL", "flv");
        EXPECT_TRUE(conf.get_vhost_edge_token_traverse("__defaultVhost__"));
        EXPECT_TRUE(conf.get_vhost_edge_token_traverse("no.such.vhost"));
        EXPECT_STREQ("flv", conf.get_vhost_edge_protocol("__defaultVhost__").c_str());
        EXPECT_STREQ("flv", conf.get_vhost_edge_protocol("no.such.vhost").c_str());
    }

    // The env overwrites the cluster of the config file.
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost ossrs.net{cluster{mode remote; origin 127.0.0.1:1935; token_traverse on; protocol flv;}}"));
        EXPECT_TRUE(conf.get_vhost_edge_token_traverse("ossrs.net"));
        EXPECT_STREQ("flv", conf.get_vhost_edge_protocol("ossrs.net").c_str());

        SrsSetEnvConfig(conf, token_traverse, "SRS_VHOST_CLUSTER_TOKEN_TRAVERSE", "off");
        SrsSetEnvConfig(conf, protocol, "SRS_VHOST_CLUSTER_PROTOCOL", "flvs");
        EXPECT_FALSE(conf.get_vhost_edge_token_traverse("ossrs.net"));
        EXPECT_STREQ("flvs", conf.get_vhost_edge_protocol("ossrs.net").c_str());
    }

    // An empty env overwrites nothing.
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost ossrs.net{cluster{token_traverse on; protocol flv;}}"));

        SrsSetEnvConfig(conf, token_traverse, "SRS_VHOST_CLUSTER_TOKEN_TRAVERSE", "");
        SrsSetEnvConfig(conf, protocol, "SRS_VHOST_CLUSTER_PROTOCOL", "");
        EXPECT_TRUE(conf.get_vhost_edge_token_traverse("ossrs.net"));
        EXPECT_STREQ("flv", conf.get_vhost_edge_protocol("ossrs.net").c_str());
    }
}

// SRS_VHOST_CLUSTER_ORIGIN_CLUSTER and SRS_VHOST_CLUSTER_COWORKERS turn the origin cluster on and set the HTTP APIs
// of its coworkers, for every vhost, overwriting the config file.
VOID TEST(ConfigEnvTest, CheckEnvValuesVhostOriginCluster)
{
    srs_error_t err;

    // The defaults: no origin cluster, and no coworkers.
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));
        EXPECT_FALSE(conf.get_vhost_origin_cluster("__defaultVhost__"));
        EXPECT_TRUE(conf.get_vhost_coworkers("__defaultVhost__").empty());
    }

    // The env without a vhost in the config file, the coworkers space separated.
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        SrsSetEnvConfig(conf, origin_cluster, "SRS_VHOST_CLUSTER_ORIGIN_CLUSTER", "on");
        SrsSetEnvConfig(conf, coworkers, "SRS_VHOST_CLUSTER_COWORKERS", " 127.0.0.1:9091  127.0.0.1:9092 ");
        EXPECT_TRUE(conf.get_vhost_origin_cluster("__defaultVhost__"));
        EXPECT_TRUE(conf.get_vhost_origin_cluster("no.such.vhost"));
        EXPECT_TRUE(conf.get_vhost_origin_cluster((SrsConfDirective *)NULL));

        vector<string> coworkers = conf.get_vhost_coworkers("no.such.vhost");
        ASSERT_EQ(2, (int)coworkers.size());
        EXPECT_STREQ("127.0.0.1:9091", coworkers.at(0).c_str());
        EXPECT_STREQ("127.0.0.1:9092", coworkers.at(1).c_str());
    }

    // The env overwrites the cluster of the config file.
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost ossrs.net{cluster{origin_cluster on; coworkers 127.0.0.1:9091 127.0.0.1:9092;}}"));
        EXPECT_TRUE(conf.get_vhost_origin_cluster("ossrs.net"));
        EXPECT_EQ(2, (int)conf.get_vhost_coworkers("ossrs.net").size());

        SrsSetEnvConfig(conf, origin_cluster, "SRS_VHOST_CLUSTER_ORIGIN_CLUSTER", "off");
        SrsSetEnvConfig(conf, coworkers, "SRS_VHOST_CLUSTER_COWORKERS", "127.0.0.1:9093");
        EXPECT_FALSE(conf.get_vhost_origin_cluster("ossrs.net"));
        EXPECT_FALSE(conf.get_vhost_origin_cluster(conf.get_vhost("ossrs.net")));

        vector<string> coworkers = conf.get_vhost_coworkers("ossrs.net");
        ASSERT_EQ(1, (int)coworkers.size());
        EXPECT_STREQ("127.0.0.1:9093", coworkers.at(0).c_str());
    }

    // An empty env overwrites nothing.
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost ossrs.net{cluster{origin_cluster on; coworkers 127.0.0.1:9091;}}"));

        SrsSetEnvConfig(conf, origin_cluster, "SRS_VHOST_CLUSTER_ORIGIN_CLUSTER", "");
        SrsSetEnvConfig(conf, coworkers, "SRS_VHOST_CLUSTER_COWORKERS", "");
        EXPECT_TRUE(conf.get_vhost_origin_cluster("ossrs.net"));

        vector<string> coworkers = conf.get_vhost_coworkers("ossrs.net");
        ASSERT_EQ(1, (int)coworkers.size());
        EXPECT_STREQ("127.0.0.1:9091", coworkers.at(0).c_str());
    }
}

// SRS_VHOST_DVR_DVR_APPLY sets the streams DVR records, for every vhost, overwriting the config file.
VOID TEST(ConfigEnvTest, CheckEnvValuesVhostDvrApply)
{
    srs_error_t err;

    SrsRequest applied;
    applied.app_ = "live";
    applied.stream_ = "s2";
    SrsRequest other;
    other.app_ = "live";
    other.stream_ = "s3";

    // The default: no filter, so every stream is recorded.
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));
        EXPECT_TRUE(conf.get_dvr_apply("__defaultVhost__") == NULL);
    }

    // The env without a dvr section in the config file, space separated.
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        SrsSetEnvConfig(conf, dvr_apply, "SRS_VHOST_DVR_DVR_APPLY", " live/s1  live/s2 ");
        SrsConfDirective *apply = conf.get_dvr_apply("no.such.vhost");
        ASSERT_TRUE(apply != NULL);
        ASSERT_EQ(2, (int)apply->args_.size());
        EXPECT_STREQ("live/s1", apply->args_.at(0).c_str());
        EXPECT_STREQ("live/s2", apply->args_.at(1).c_str());
        EXPECT_TRUE(srs_config_apply_filter(apply, &applied));
        EXPECT_FALSE(srs_config_apply_filter(apply, &other));
    }

    // The env overwrites the dvr_apply of the config file, with all too.
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost ossrs.net{dvr{dvr_apply live/s3;}}"));
        EXPECT_TRUE(srs_config_apply_filter(conf.get_dvr_apply("ossrs.net"), &other));

        SrsSetEnvConfig(conf, dvr_apply, "SRS_VHOST_DVR_DVR_APPLY", "live/s2");
        EXPECT_TRUE(srs_config_apply_filter(conf.get_dvr_apply("ossrs.net"), &applied));
        EXPECT_FALSE(srs_config_apply_filter(conf.get_dvr_apply("ossrs.net"), &other));
    }
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost ossrs.net{dvr{dvr_apply live/s3;}}"));

        SrsSetEnvConfig(conf, dvr_apply, "SRS_VHOST_DVR_DVR_APPLY", "all");
        SrsConfDirective *apply = conf.get_dvr_apply("ossrs.net");
        ASSERT_TRUE(apply != NULL);
        EXPECT_STREQ("all", apply->arg0().c_str());
        EXPECT_TRUE(srs_config_apply_filter(apply, &applied));
    }

    // An empty env, or one of spaces only, overwrites nothing.
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost ossrs.net{dvr{dvr_apply live/s3;}}"));

        SrsSetEnvConfig(conf, dvr_apply, "SRS_VHOST_DVR_DVR_APPLY", "");
        EXPECT_TRUE(srs_config_apply_filter(conf.get_dvr_apply("ossrs.net"), &other));
        EXPECT_FALSE(srs_config_apply_filter(conf.get_dvr_apply("ossrs.net"), &applied));
    }
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost ossrs.net{dvr{dvr_apply live/s3;}}"));

        SrsSetEnvConfig(conf, dvr_apply, "SRS_VHOST_DVR_DVR_APPLY", "   ");
        EXPECT_TRUE(srs_config_apply_filter(conf.get_dvr_apply("ossrs.net"), &other));
        EXPECT_FALSE(srs_config_apply_filter(conf.get_dvr_apply("ossrs.net"), &applied));
    }
}

VOID TEST(ConfigEnvTest, CheckEnvValuesStreamCaster)
{
    srs_error_t err;

    // SRS_STREAM_CASTER_ENABLED creates a stream caster when the config has none.
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        SrsSetEnvConfig(conf, enabled, "SRS_STREAM_CASTER_ENABLED", "on");
        SrsSetEnvConfig(conf, caster, "SRS_STREAM_CASTER_CASTER", "gb28181");
        SrsSetEnvConfig(conf, output, "SRS_STREAM_CASTER_OUTPUT", "rtmp://127.0.0.1:1935/live/[stream]");
        SrsSetEnvConfig(conf, listen, "SRS_STREAM_CASTER_LISTEN", "9000");
        SrsSetEnvConfig(conf, timeout, "SRS_STREAM_CASTER_MEDIA_CONNECT_TIMEOUT", "3");

        vector<SrsConfDirective *> casters = conf.get_stream_casters();
        ASSERT_EQ(1, (int)casters.size());
        SrsConfDirective *c = casters.at(0);
        EXPECT_TRUE(conf.get_stream_caster_enabled(c));
        EXPECT_STREQ("gb28181", conf.get_stream_caster_engine(c).c_str());
        EXPECT_STREQ("rtmp://127.0.0.1:1935/live/[stream]", conf.get_stream_caster_output(c).c_str());
        EXPECT_EQ(9000, conf.get_stream_caster_listen(c));
        EXPECT_EQ(3 * SRS_UTIME_SECONDS, conf.get_stream_caster_media_connect_timeout(c));

        // The same caster is returned every time, not a new one.
        EXPECT_EQ(c, conf.get_stream_casters().at(0));
    }

    // The env overwrites the stream caster of the config file.
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "stream_caster{enabled off; caster mpegts_over_udp; output rtmp://127.0.0.1/live/livestream; listen 8935;}"));

        SrsSetEnvConfig(conf, enabled, "SRS_STREAM_CASTER_ENABLED", "on");
        SrsSetEnvConfig(conf, listen, "SRS_STREAM_CASTER_LISTEN", "9000");

        vector<SrsConfDirective *> casters = conf.get_stream_casters();
        ASSERT_EQ(1, (int)casters.size());
        SrsConfDirective *c = casters.at(0);
        EXPECT_TRUE(conf.get_stream_caster_enabled(c));
        EXPECT_STREQ("mpegts_over_udp", conf.get_stream_caster_engine(c).c_str());
        EXPECT_EQ(9000, conf.get_stream_caster_listen(c));
    }

    // Without env, there is no stream caster, and the config file is used.
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));
        EXPECT_EQ(0, (int)conf.get_stream_casters().size());
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "stream_caster{enabled on; caster mpegts_over_udp; listen 8935;}"));

        vector<SrsConfDirective *> casters = conf.get_stream_casters();
        ASSERT_EQ(1, (int)casters.size());
        EXPECT_TRUE(conf.get_stream_caster_enabled(casters.at(0)));
        EXPECT_EQ(8935, conf.get_stream_caster_listen(casters.at(0)));
    }
}

extern bool _srs_in_docker;

// parse_options() reads in_docker from the config it parsed, not from the global config.
VOID TEST(ConfigParseOptionsTest, InDockerFromParsedConfig)
{
    srs_error_t err;

    string filepath = _srs_tmp_file_prefix + "utest-in-docker.conf";
    MockFileRemover _mfr(filepath);

    if (true) {
        SrsFileWriter fw;
        HELPER_ASSERT_SUCCESS(fw.open(filepath));
        string content = _MIN_OK_CONF "in_docker on;";
        HELPER_ASSERT_SUCCESS(fw.write((void *)content.data(), (int)content.length(), NULL));
    }

    // The global config leaves in_docker off, so a read of the global is caught.
    ASSERT_FALSE(_srs_config->get_in_docker());

    SrsConfig conf;
    // Log to the console through the env, so get_log_tank_file() returns before it changes
    // its process-wide default for docker.
    SrsSetEnvConfig(conf, log_tank, "SRS_LOG_TANK", "console");

    bool in_docker = _srs_in_docker;
    char *argv[] = {(char *)"srs", (char *)"-c", (char *)filepath.c_str()};
    err = conf.parse_options(3, argv);
    bool parsed_in_docker = _srs_in_docker;
    _srs_in_docker = in_docker;

    HELPER_EXPECT_SUCCESS(err);
    EXPECT_TRUE(conf.get_in_docker());
    EXPECT_TRUE(parsed_in_docker);
}

// With env only, the parsed config has the default vhost, so the config test, which checks
// the parsed config, checks the vhost settings set by env.
VOID TEST(ConfigParseOptionsTest, EnvOnlyCheckSeesVhostSettings)
{
    srs_error_t err;

    if (true) {
        SrsConfig conf;
        conf.env_only_ = true;
        HELPER_EXPECT_SUCCESS(conf.parse_config());
        EXPECT_TRUE(conf.get_vhost("__defaultVhost__", false) != NULL);
        HELPER_EXPECT_SUCCESS(conf.check_config());
    }

    if (true) {
        SrsConfig conf;
        conf.env_only_ = true;
        SrsSetEnvConfig(conf, forward, "SRS_VHOST_FORWARD_ENABLED", "on");
        SrsSetEnvConfig(conf, kickoff, "SRS_VHOST_PUBLISH_KICKOFF_FOR_IDLE", "1000");
        HELPER_EXPECT_SUCCESS(conf.parse_config());
        err = conf.check_config();
        EXPECT_TRUE(err != srs_success);
        if (err != srs_success) {
            string desc = srs_error_desc(err);
            EXPECT_TRUE(desc.find("vhost.forward conflicts with vhost.publish.kickoff_for_idle") != string::npos) << desc;
        }
        srs_freep(err);
    }

    // Each setting alone is accepted.
    if (true) {
        SrsConfig conf;
        conf.env_only_ = true;
        SrsSetEnvConfig(conf, forward, "SRS_VHOST_FORWARD_ENABLED", "on");
        HELPER_EXPECT_SUCCESS(conf.parse_config());
        HELPER_EXPECT_SUCCESS(conf.check_config());
    }
    if (true) {
        SrsConfig conf;
        conf.env_only_ = true;
        SrsSetEnvConfig(conf, kickoff, "SRS_VHOST_PUBLISH_KICKOFF_FOR_IDLE", "1000");
        HELPER_EXPECT_SUCCESS(conf.parse_config());
        HELPER_EXPECT_SUCCESS(conf.check_config());
    }
}

// With a config file, parse_config() parses the file, adds no default vhost, and the check
// refuses the same conflict of the file.
VOID TEST(ConfigParseOptionsTest, FileCheckSeesVhostSettings)
{
    srs_error_t err;

    string filepath = _srs_tmp_file_prefix + "utest-parse-config.conf";
    MockFileRemover _mfr(filepath);

    if (true) {
        SrsFileWriter fw;
        HELPER_ASSERT_SUCCESS(fw.open(filepath));
        string content = _MIN_OK_CONF "vhost v.ossrs.net { forward { enabled on; } publish { kickoff_for_idle 1000; } }";
        HELPER_ASSERT_SUCCESS(fw.write((void *)content.data(), (int)content.length(), NULL));
    }

    SrsConfig conf;
    conf.config_file_ = filepath;
    HELPER_EXPECT_SUCCESS(conf.parse_config());
    EXPECT_TRUE(conf.get_vhost("__defaultVhost__", false) == NULL);
    EXPECT_TRUE(conf.get_vhost("v.ossrs.net", false) != NULL);
    err = conf.check_config();
    EXPECT_TRUE(err != srs_success);
    srs_freep(err);
}

// The config owns the generator of its default server id.
VOID TEST(ConfigServerIdTest, ConstructorCreatesRand)
{
    SrsConfig conf;
    EXPECT_TRUE(conf.rand_ != NULL);
    EXPECT_TRUE(dynamic_cast<SrsRand *>(conf.rand_) != NULL);
}

// Without a server id in the env, the config or a server id file, the default is drawn once
// through the config's generator and kept.
VOID TEST(ConfigServerIdTest, DefaultDrawsThroughInjectedRand)
{
    // Without a pid file, there is no server id file to read the default from.
    SrsConfig conf;
    conf.env_only_ = true;
    ASSERT_STREQ("", conf.get_pid_file().c_str());

    MockRandForHandshake rand;
    rand.gen_str_values_.push_back("abc1234");
    srs_freep(conf.rand_);
    conf.rand_ = &rand;

    string id1 = conf.get_server_id();
    string id2 = conf.get_server_id();
    conf.rand_ = NULL;

    EXPECT_STREQ("vid-abc1234", id1.c_str());
    EXPECT_STREQ("vid-abc1234", id2.c_str());
    ASSERT_EQ(1, (int)rand.gen_str_lens_.size());
    EXPECT_EQ(7, rand.gen_str_lens_.at(0));
}

// Each config keeps its own default server id, so one drawn by an earlier config is not reused.
VOID TEST(ConfigServerIdTest, DefaultIsKeptPerConfig)
{
    MockRandForHandshake rand1;
    rand1.gen_str_values_.push_back("first01");
    MockRandForHandshake rand2;
    rand2.gen_str_values_.push_back("second2");

    string id1, id2;
    if (true) {
        SrsConfig conf;
        conf.env_only_ = true;
        srs_freep(conf.rand_);
        conf.rand_ = &rand1;
        id1 = conf.get_server_id();
        conf.rand_ = NULL;
    }
    if (true) {
        SrsConfig conf;
        conf.env_only_ = true;
        srs_freep(conf.rand_);
        conf.rand_ = &rand2;
        id2 = conf.get_server_id();
        conf.rand_ = NULL;
    }

    EXPECT_STREQ("vid-first01", id1.c_str());
    EXPECT_STREQ("vid-second2", id2.c_str());
    EXPECT_EQ(1, (int)rand1.gen_str_lens_.size());
    EXPECT_EQ(1, (int)rand2.gen_str_lens_.size());
}

// Count the rules of a security directive with the name and args.
static int count_security_rules(SrsConfDirective *rules, string name, string action, string value)
{
    int n = 0;
    for (int i = 0; rules && i < (int)rules->directives_.size(); i++) {
        SrsConfDirective *rule = rules->at(i);
        if (rule->name_ == name && rule->arg0() == action && rule->arg1() == value && rule->args_.size() == 2) {
            n++;
        }
    }
    return n;
}

// SRS_VHOST_SECURITY_ENABLED turns the security on or off for every vhost, overwriting the config file.
VOID TEST(ConfigSecurityTest, EnabledFromEnv)
{
    srs_error_t err;

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));
        EXPECT_FALSE(conf.get_security_enabled("__defaultVhost__"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));
        SrsSetEnvConfig(conf, security_enabled, "SRS_VHOST_SECURITY_ENABLED", "on");
        EXPECT_TRUE(conf.get_security_enabled("__defaultVhost__"));
        EXPECT_TRUE(conf.get_security_enabled("no.such.vhost"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost ossrs.net{security{enabled on;}}"));
        EXPECT_TRUE(conf.get_security_enabled("ossrs.net"));
        SrsSetEnvConfig(conf, security_enabled, "SRS_VHOST_SECURITY_ENABLED", "off");
        EXPECT_FALSE(conf.get_security_enabled("ossrs.net"));

        SrsConfDirective *vhost = conf.get_vhost("ossrs.net");
        ASSERT_TRUE(vhost != NULL);
        EXPECT_FALSE(conf.get_security_enabled(vhost));
    }
}

// SRS_VHOST_SECURITY_ALLOW_PUBLISH, SRS_VHOST_SECURITY_ALLOW_PLAY, SRS_VHOST_SECURITY_DENY_PUBLISH and
// SRS_VHOST_SECURITY_DENY_PLAY, each a space separated list of all, an ip or a cidr, make one rule per item,
// like the allow and deny directives of the config file.
VOID TEST(ConfigSecurityTest, RulesFromEnv)
{
    srs_error_t err;

    MockSrsConfig conf;
    HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));
    EXPECT_TRUE(conf.get_security_rules("__defaultVhost__") == NULL);

    SrsSetEnvConfig(conf, allow_publish, "SRS_VHOST_SECURITY_ALLOW_PUBLISH", "127.0.0.1  10.0.0.0/8");
    SrsSetEnvConfig(conf, allow_play, "SRS_VHOST_SECURITY_ALLOW_PLAY", "all");
    SrsSetEnvConfig(conf, deny_publish, "SRS_VHOST_SECURITY_DENY_PUBLISH", "10.1.2.3");
    SrsSetEnvConfig(conf, deny_play, "SRS_VHOST_SECURITY_DENY_PLAY", "192.168.0.0/16 172.16.0.1");

    SrsConfDirective *rules = conf.get_security_rules("__defaultVhost__");
    ASSERT_TRUE(rules != NULL);
    EXPECT_EQ(6, (int)rules->directives_.size());
    EXPECT_EQ(1, count_security_rules(rules, "allow", "publish", "127.0.0.1"));
    EXPECT_EQ(1, count_security_rules(rules, "allow", "publish", "10.0.0.0/8"));
    EXPECT_EQ(1, count_security_rules(rules, "allow", "play", "all"));
    EXPECT_EQ(1, count_security_rules(rules, "deny", "publish", "10.1.2.3"));
    EXPECT_EQ(1, count_security_rules(rules, "deny", "play", "192.168.0.0/16"));
    EXPECT_EQ(1, count_security_rules(rules, "deny", "play", "172.16.0.1"));

    // The same rules for every vhost, and for each call.
    EXPECT_TRUE(conf.get_security_rules("no.such.vhost") == rules);
    EXPECT_TRUE(conf.get_security_rules("__defaultVhost__") == rules);
}

// The env rules replace the rules of the config file, even when only one of them is set; an empty value
// overwrites nothing.
VOID TEST(ConfigSecurityTest, EnvRulesReplaceFile)
{
    srs_error_t err;

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost ossrs.net{security{enabled on; allow publish all; allow play all;}}"));
        SrsSetEnvConfig(conf, deny_play, "SRS_VHOST_SECURITY_DENY_PLAY", "all");
        SrsConfDirective *rules = conf.get_security_rules("ossrs.net");
        ASSERT_TRUE(rules != NULL);
        EXPECT_EQ(1, (int)rules->directives_.size());
        EXPECT_EQ(1, count_security_rules(rules, "deny", "play", "all"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost ossrs.net{security{enabled on; allow publish all;}}"));
        SrsSetEnvConfig(conf, allow_play, "SRS_VHOST_SECURITY_ALLOW_PLAY", "");
        SrsSetEnvConfig(conf, deny_play, "SRS_VHOST_SECURITY_DENY_PLAY", "   ");
        SrsConfDirective *rules = conf.get_security_rules("ossrs.net");
        ASSERT_TRUE(rules != NULL);
        EXPECT_EQ(1, count_security_rules(rules, "allow", "publish", "all"));
        EXPECT_EQ(0, count_security_rules(rules, "deny", "play", ""));
    }
}

// The security check applies the env rules: deny wins over allow, an ip or cidr matches the client, and with
// the security on but no rule at all, every client is denied.
VOID TEST(ConfigSecurityTest, CheckWithEnvRules)
{
    srs_error_t err;

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));
        SrsSetEnvConfig(conf, security_enabled, "SRS_VHOST_SECURITY_ENABLED", "on");
        SrsSetEnvConfig(conf, allow_publish, "SRS_VHOST_SECURITY_ALLOW_PUBLISH", "all");
        SrsSetEnvConfig(conf, deny_publish, "SRS_VHOST_SECURITY_DENY_PUBLISH", "127.0.0.1");
        SrsSetEnvConfig(conf, allow_play, "SRS_VHOST_SECURITY_ALLOW_PLAY", "10.0.0.0/8");

        SrsSecurity sec;
        sec.config_ = &conf;
        SrsRequest req;
        req.vhost_ = "__defaultVhost__";
        HELPER_EXPECT_FAILED(sec.check(SrsRtmpConnFMLEPublish, "127.0.0.1", &req));
        HELPER_EXPECT_SUCCESS(sec.check(SrsRtcConnPublish, "192.168.1.2", &req));
        HELPER_EXPECT_SUCCESS(sec.check(SrsRtmpConnPlay, "10.2.3.4", &req));
        HELPER_EXPECT_FAILED(sec.check(SrsFlvPlay, "127.0.0.1", &req));
        sec.config_ = NULL;
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));
        SrsSetEnvConfig(conf, security_enabled, "SRS_VHOST_SECURITY_ENABLED", "on");

        SrsSecurity sec;
        sec.config_ = &conf;
        SrsRequest req;
        req.vhost_ = "__defaultVhost__";
        HELPER_EXPECT_FAILED(sec.check(SrsSrtConnPublish, "127.0.0.1", &req));
        HELPER_EXPECT_FAILED(sec.check(SrsSrtConnPlay, "127.0.0.1", &req));
        sec.config_ = NULL;
    }

    if (true) {
        // Rules without the security on change nothing.
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));
        SrsSetEnvConfig(conf, deny_publish, "SRS_VHOST_SECURITY_DENY_PUBLISH", "all");

        SrsSecurity sec;
        sec.config_ = &conf;
        SrsRequest req;
        req.vhost_ = "__defaultVhost__";
        HELPER_EXPECT_SUCCESS(sec.check(SrsRtmpConnFMLEPublish, "127.0.0.1", &req));
        sec.config_ = NULL;
    }
}

// An RTSP, HTTP-TS, MP3 or AAC player is a player for the security: the play rules allow and deny it,
// and the publish rules don't.
VOID TEST(ConfigSecurityTest, PlayRulesApplyToEveryPlayer)
{
    srs_error_t err;

    SrsRtmpConnType types[] = {SrsRtspConnPlay, SrsTsPlay, SrsMp3Play, SrsAacPlay};
    for (int i = 0; i < 4; i++) {
        SrsRtmpConnType type = types[i];

        if (true) {
            MockSrsConfig conf;
            HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));
            SrsSetEnvConfig(conf, security_enabled, "SRS_VHOST_SECURITY_ENABLED", "on");
            SrsSetEnvConfig(conf, allow_play, "SRS_VHOST_SECURITY_ALLOW_PLAY", "10.0.0.0/8");
            SrsSetEnvConfig(conf, deny_play, "SRS_VHOST_SECURITY_DENY_PLAY", "10.2.3.4");

            SrsSecurity sec;
            sec.config_ = &conf;
            SrsRequest req;
            req.vhost_ = "__defaultVhost__";
            HELPER_EXPECT_SUCCESS(sec.check(type, "10.2.3.5", &req));
            HELPER_EXPECT_FAILED(sec.check(type, "10.2.3.4", &req));
            HELPER_EXPECT_FAILED(sec.check(type, "192.168.1.2", &req));
            sec.config_ = NULL;
        }

        if (true) {
            MockSrsConfig conf;
            HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));
            SrsSetEnvConfig(conf, security_enabled, "SRS_VHOST_SECURITY_ENABLED", "on");
            SrsSetEnvConfig(conf, allow_play, "SRS_VHOST_SECURITY_ALLOW_PLAY", "all");
            SrsSetEnvConfig(conf, deny_publish, "SRS_VHOST_SECURITY_DENY_PUBLISH", "all");

            SrsSecurity sec;
            sec.config_ = &conf;
            SrsRequest req;
            req.vhost_ = "__defaultVhost__";
            HELPER_EXPECT_SUCCESS(sec.check(type, "127.0.0.1", &req));
            sec.config_ = NULL;
        }
    }
}

// Each player and publisher type names its protocol in the API, and only publishers publish.
VOID TEST(ClientTypeTest, EachTypeNamesItsProtocol)
{
    EXPECT_STREQ("rtmp-play", srs_client_type_string(SrsRtmpConnPlay).c_str());
    EXPECT_STREQ("hls-play", srs_client_type_string(SrsHlsPlay).c_str());
    EXPECT_STREQ("flv-play", srs_client_type_string(SrsFlvPlay).c_str());
    EXPECT_STREQ("ts-play", srs_client_type_string(SrsTsPlay).c_str());
    EXPECT_STREQ("mp3-play", srs_client_type_string(SrsMp3Play).c_str());
    EXPECT_STREQ("aac-play", srs_client_type_string(SrsAacPlay).c_str());
    EXPECT_STREQ("rtc-play", srs_client_type_string(SrsRtcConnPlay).c_str());
    EXPECT_STREQ("srt-play", srs_client_type_string(SrsSrtConnPlay).c_str());
    EXPECT_STREQ("rtsp-play", srs_client_type_string(SrsRtspConnPlay).c_str());
    EXPECT_STREQ("fmle-publish", srs_client_type_string(SrsRtmpConnFMLEPublish).c_str());
    EXPECT_STREQ("rtc-publish", srs_client_type_string(SrsRtcConnPublish).c_str());
    EXPECT_STREQ("srt-publish", srs_client_type_string(SrsSrtConnPublish).c_str());

    EXPECT_FALSE(srs_client_type_is_publish(SrsTsPlay));
    EXPECT_FALSE(srs_client_type_is_publish(SrsMp3Play));
    EXPECT_FALSE(srs_client_type_is_publish(SrsAacPlay));
    EXPECT_FALSE(srs_client_type_is_publish(SrsRtspConnPlay));
    EXPECT_TRUE(srs_client_type_is_publish(SrsRtcConnPublish));
}
