//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_utest_ai02.hpp>

using namespace std;

#include <srs_app_config.hpp>
#include <srs_app_utility.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_utest_manual_config.hpp>

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
