//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//
#include <srs_utest_config2.hpp>

#include <srs_kernel_error.hpp>
#include <srs_kernel_file.hpp>
#include <srs_protocol_utility.hpp>
#include <srs_utest_kernel.hpp>

static void ensure_local_ip_for_config_test()
{
    vector<SrsIPAddress*>& ips = srs_get_local_ips();
    if (!ips.empty()) return;

    SrsIPAddress* ip = new SrsIPAddress();
    ip->ifname = "lo";
    ip->ip = "127.0.0.1";
    ip->is_ipv4 = true;
    ip->is_internet = false;
    ip->is_loopback = true;
    ips.push_back(ip);
}

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
            fw.write((void*)content.data(), (int)content.length(), NULL);
        }

        SrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse_file(filepath.c_str()));
        EXPECT_EQ(1, (int)conf.get_listens().size());
    }

    if (true) {
        MockSrsConfig conf;
        conf.mock_include("test.conf", "");
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF "include test.conf;"));
        EXPECT_EQ(1, (int)conf.get_listens().size());
    }
}

VOID TEST(ConfigRtmpsTest, DefaultsAndExplicitValues)
{
    srs_error_t err;
    ensure_local_ip_for_config_test();

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF));

        EXPECT_FALSE(conf.get_rtmps_enabled());
        EXPECT_EQ(0, (int)conf.get_rtmps_listen().size());
        EXPECT_STREQ("./conf/server.key", conf.get_rtmps_ssl_key().c_str());
        EXPECT_STREQ("./conf/server.crt", conf.get_rtmps_ssl_cert().c_str());
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF
            "rtmps {"
            "enabled on;"
            "listen 1443 8443;"
            "key ./conf/rtmps.key;"
            "cert ./conf/rtmps.crt;"
            "}"
        ));

        EXPECT_TRUE(conf.get_rtmps_enabled());
        vector<string> listens = conf.get_rtmps_listen();
        ASSERT_EQ(2, (int)listens.size());
        EXPECT_STREQ("1443", listens.at(0).c_str());
        EXPECT_STREQ("8443", listens.at(1).c_str());
        EXPECT_STREQ("./conf/rtmps.key", conf.get_rtmps_ssl_key().c_str());
        EXPECT_STREQ("./conf/rtmps.crt", conf.get_rtmps_ssl_cert().c_str());
    }
}

VOID TEST(ConfigRtmpsTest, EnvironmentOverrides)
{
    srs_error_t err;
    ensure_local_ip_for_config_test();

    MockSrsConfig conf;
    HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF));

    SrsSetEnvConfig(conf, rtmps_enabled, "SRS_RTMPS_ENABLED", "on");
    SrsSetEnvConfig(conf, rtmps_listen, "SRS_RTMPS_LISTEN", "2443 3443");
    SrsSetEnvConfig(conf, rtmps_key, "SRS_RTMPS_KEY", "/tmp/rtmps.key");
    SrsSetEnvConfig(conf, rtmps_cert, "SRS_RTMPS_CERT", "/tmp/rtmps.crt");

    EXPECT_TRUE(conf.get_rtmps_enabled());
    vector<string> listens = conf.get_rtmps_listen();
    ASSERT_EQ(2, (int)listens.size());
    EXPECT_STREQ("2443", listens.at(0).c_str());
    EXPECT_STREQ("3443", listens.at(1).c_str());
    EXPECT_STREQ("/tmp/rtmps.key", conf.get_rtmps_ssl_key().c_str());
    EXPECT_STREQ("/tmp/rtmps.crt", conf.get_rtmps_ssl_cert().c_str());
}

VOID TEST(ConfigRtmpsTest, RejectUnknownDirective)
{
    srs_error_t err;

    MockSrsConfig conf;
    HELPER_ASSERT_FAILED(conf.parse(_MIN_OK_CONF "rtmps { unknown on; }"));
}
