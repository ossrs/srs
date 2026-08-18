//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_utest_ai01.hpp>

using namespace std;

#include <srs_app_config.hpp>
#include <srs_kernel_consts.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_utility.hpp>

VOID TEST(ConfigRtcTest, CheckRtcPliForRtmpDefault)
{
    srs_error_t err;

    // Test default value when no configuration is provided
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        // Default should be 6 seconds
        EXPECT_EQ(6 * SRS_UTIME_SECONDS, conf.get_rtc_pli_for_rtmp("__defaultVhost__"));
        EXPECT_EQ(6 * SRS_UTIME_SECONDS, conf.get_rtc_pli_for_rtmp("test.com"));
    }

    // Test default value when vhost exists but no rtc section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{enabled on;}}"));

        // Should return default value when no rtc section
        EXPECT_EQ(6 * SRS_UTIME_SECONDS, conf.get_rtc_pli_for_rtmp("test.com"));
    }

    // Test default value when rtc section exists but no pli_for_rtmp
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{enabled on;}}"));

        // Should return default value when no pli_for_rtmp config
        EXPECT_EQ(6 * SRS_UTIME_SECONDS, conf.get_rtc_pli_for_rtmp("test.com"));
    }
}

VOID TEST(ConfigRtcTest, CheckRtcPliForRtmpConfigFile)
{
    srs_error_t err;

    // Test valid configuration values
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{pli_for_rtmp 3;}}"));

        EXPECT_EQ(3 * SRS_UTIME_SECONDS, conf.get_rtc_pli_for_rtmp("test.com"));
    }

    // Test decimal configuration values
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{pli_for_rtmp 2.5;}}"));

        EXPECT_EQ((srs_utime_t)(2.5 * SRS_UTIME_SECONDS), conf.get_rtc_pli_for_rtmp("test.com"));
    }

    // Test minimum valid value (0.5 seconds)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{pli_for_rtmp 0.5;}}"));

        EXPECT_EQ(500 * SRS_UTIME_MILLISECONDS, conf.get_rtc_pli_for_rtmp("test.com"));
    }

    // Test maximum valid value (30 seconds)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{pli_for_rtmp 30;}}"));

        EXPECT_EQ(30 * SRS_UTIME_SECONDS, conf.get_rtc_pli_for_rtmp("test.com"));
    }
}

VOID TEST(ConfigRtcTest, CheckRtcPliForRtmpRangeValidation)
{
    srs_error_t err;

    // Test value below minimum (should reset to default)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{pli_for_rtmp 0.1;}}"));

        // Should reset to default (6 seconds) when value is too small
        EXPECT_EQ(6 * SRS_UTIME_SECONDS, conf.get_rtc_pli_for_rtmp("test.com"));
    }

    // Test value above maximum (should reset to default)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{pli_for_rtmp 60;}}"));

        // Should reset to default (6 seconds) when value is too large
        EXPECT_EQ(6 * SRS_UTIME_SECONDS, conf.get_rtc_pli_for_rtmp("test.com"));
    }

    // Test zero value (should reset to default)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{pli_for_rtmp 0;}}"));

        // Should reset to default (6 seconds) when value is zero
        EXPECT_EQ(6 * SRS_UTIME_SECONDS, conf.get_rtc_pli_for_rtmp("test.com"));
    }

    // Test negative value (should reset to default)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{pli_for_rtmp -1;}}"));

        // Should reset to default (6 seconds) when value is negative
        EXPECT_EQ(6 * SRS_UTIME_SECONDS, conf.get_rtc_pli_for_rtmp("test.com"));
    }
}

VOID TEST(ConfigRtcTest, CheckRtcPliForRtmpEnvironmentVariable)
{
    srs_error_t err;

    // Test environment variable override with valid value
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{pli_for_rtmp 10;}}"));

        SrsSetEnvConfig(conf, pli_for_rtmp, "SRS_VHOST_RTC_PLI_FOR_RTMP", "5");

        // Environment variable should override config file
        EXPECT_EQ(5 * SRS_UTIME_SECONDS, conf.get_rtc_pli_for_rtmp("test.com"));
    }

    // Test environment variable with decimal value
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        SrsSetEnvConfig(conf, pli_for_rtmp, "SRS_VHOST_RTC_PLI_FOR_RTMP", "1.5");

        EXPECT_EQ((srs_utime_t)(1.5 * SRS_UTIME_SECONDS), conf.get_rtc_pli_for_rtmp("__defaultVhost__"));
    }

    // Test environment variable with minimum valid value
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        SrsSetEnvConfig(conf, pli_for_rtmp, "SRS_VHOST_RTC_PLI_FOR_RTMP", "0.5");

        EXPECT_EQ(500 * SRS_UTIME_MILLISECONDS, conf.get_rtc_pli_for_rtmp("__defaultVhost__"));
    }

    // Test environment variable with maximum valid value
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        SrsSetEnvConfig(conf, pli_for_rtmp, "SRS_VHOST_RTC_PLI_FOR_RTMP", "30");

        EXPECT_EQ(30 * SRS_UTIME_SECONDS, conf.get_rtc_pli_for_rtmp("__defaultVhost__"));
    }

    // Test environment variable with value below minimum (should reset to default)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        SrsSetEnvConfig(conf, pli_for_rtmp, "SRS_VHOST_RTC_PLI_FOR_RTMP", "0.1");

        // Should reset to default (6 seconds) when env value is too small
        EXPECT_EQ(6 * SRS_UTIME_SECONDS, conf.get_rtc_pli_for_rtmp("__defaultVhost__"));
    }

    // Test environment variable with value above maximum (should reset to default)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        SrsSetEnvConfig(conf, pli_for_rtmp, "SRS_VHOST_RTC_PLI_FOR_RTMP", "60");

        // Should reset to default (6 seconds) when env value is too large
        EXPECT_EQ(6 * SRS_UTIME_SECONDS, conf.get_rtc_pli_for_rtmp("__defaultVhost__"));
    }
}

VOID TEST(ConfigRtcTest, CheckRtcPliForRtmpEdgeCases)
{
    srs_error_t err;

    // Test empty configuration value (should use default)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{pli_for_rtmp;}}"));

        // Should return default value when config value is empty
        EXPECT_EQ(6 * SRS_UTIME_SECONDS, conf.get_rtc_pli_for_rtmp("test.com"));
    }

    // Test empty environment variable (should use config file value)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{pli_for_rtmp 8;}}"));

        SrsSetEnvConfig(conf, pli_for_rtmp, "SRS_VHOST_RTC_PLI_FOR_RTMP", "");

        // Should use config file value when env is empty
        EXPECT_EQ(8 * SRS_UTIME_SECONDS, conf.get_rtc_pli_for_rtmp("test.com"));
    }

    // Test non-numeric environment variable (should parse as 0 and reset to default)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        SrsSetEnvConfig(conf, pli_for_rtmp, "SRS_VHOST_RTC_PLI_FOR_RTMP", "invalid");

        // Should reset to default when env value is non-numeric
        EXPECT_EQ(6 * SRS_UTIME_SECONDS, conf.get_rtc_pli_for_rtmp("__defaultVhost__"));
    }

    // Test non-numeric configuration value (should parse as 0 and reset to default)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{pli_for_rtmp invalid;}}"));

        // Should reset to default when config value is non-numeric
        EXPECT_EQ(6 * SRS_UTIME_SECONDS, conf.get_rtc_pli_for_rtmp("test.com"));
    }

    // Test multiple vhosts with different configurations
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF
                                              "vhost vhost1.com{rtc{pli_for_rtmp 2;}} "
                                              "vhost vhost2.com{rtc{pli_for_rtmp 8;}} "
                                              "vhost vhost3.com{rtc{enabled on;}}"));

        EXPECT_EQ(2 * SRS_UTIME_SECONDS, conf.get_rtc_pli_for_rtmp("vhost1.com"));
        EXPECT_EQ(8 * SRS_UTIME_SECONDS, conf.get_rtc_pli_for_rtmp("vhost2.com"));
        EXPECT_EQ(6 * SRS_UTIME_SECONDS, conf.get_rtc_pli_for_rtmp("vhost3.com")); // default
    }

    // Test environment variable affects all vhosts
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF
                                              "vhost vhost1.com{rtc{pli_for_rtmp 2;}} "
                                              "vhost vhost2.com{rtc{pli_for_rtmp 8;}}"));

        SrsSetEnvConfig(conf, pli_for_rtmp, "SRS_VHOST_RTC_PLI_FOR_RTMP", "15");

        // Environment variable should override all vhost configs
        EXPECT_EQ(15 * SRS_UTIME_SECONDS, conf.get_rtc_pli_for_rtmp("vhost1.com"));
        EXPECT_EQ(15 * SRS_UTIME_SECONDS, conf.get_rtc_pli_for_rtmp("vhost2.com"));
        EXPECT_EQ(15 * SRS_UTIME_SECONDS, conf.get_rtc_pli_for_rtmp("__defaultVhost__"));
    }
}

VOID TEST(ConfigRtcTest, CheckRtcPliForRtmpBoundaryConditions)
{
    srs_error_t err;

    // Test exact minimum boundary (500ms)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{pli_for_rtmp 0.5;}}"));

        EXPECT_EQ(500 * SRS_UTIME_MILLISECONDS, conf.get_rtc_pli_for_rtmp("test.com"));
    }

    // Test just below minimum boundary (499ms - should reset to default)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{pli_for_rtmp 0.499;}}"));

        // Should reset to default (6 seconds) when value is just below minimum
        EXPECT_EQ(6 * SRS_UTIME_SECONDS, conf.get_rtc_pli_for_rtmp("test.com"));
    }

    // Test just above minimum boundary (501ms)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{pli_for_rtmp 0.501;}}"));

        EXPECT_EQ((srs_utime_t)(0.501 * SRS_UTIME_SECONDS), conf.get_rtc_pli_for_rtmp("test.com"));
    }

    // Test exact maximum boundary (30 seconds)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{pli_for_rtmp 30;}}"));

        EXPECT_EQ(30 * SRS_UTIME_SECONDS, conf.get_rtc_pli_for_rtmp("test.com"));
    }

    // Test just above maximum boundary (30.1 seconds - should reset to default)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{pli_for_rtmp 30.1;}}"));

        // Should reset to default (6 seconds) when value is just above maximum
        EXPECT_EQ(6 * SRS_UTIME_SECONDS, conf.get_rtc_pli_for_rtmp("test.com"));
    }

    // Test just below maximum boundary (29.9 seconds)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{pli_for_rtmp 29.9;}}"));

        EXPECT_EQ((srs_utime_t)(29.9 * SRS_UTIME_SECONDS), conf.get_rtc_pli_for_rtmp("test.com"));
    }

    // Test high precision decimal values
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{pli_for_rtmp 1.234567;}}"));

        EXPECT_EQ((srs_utime_t)(1.234567 * SRS_UTIME_SECONDS), conf.get_rtc_pli_for_rtmp("test.com"));
    }

    // Test very small valid value
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{pli_for_rtmp 0.500001;}}"));

        EXPECT_EQ((srs_utime_t)(0.500001 * SRS_UTIME_SECONDS), conf.get_rtc_pli_for_rtmp("test.com"));
    }
}

VOID TEST(ConfigRtcTest, CheckRtcPliForRtmpEnvironmentEdgeCases)
{
    srs_error_t err;

    // Test environment variable with boundary values
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        // Test exact minimum via environment
        SrsSetEnvConfig(conf, pli_for_rtmp1, "SRS_VHOST_RTC_PLI_FOR_RTMP", "0.5");
        EXPECT_EQ(500 * SRS_UTIME_MILLISECONDS, conf.get_rtc_pli_for_rtmp("__defaultVhost__"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        // Test exact maximum via environment
        SrsSetEnvConfig(conf, pli_for_rtmp2, "SRS_VHOST_RTC_PLI_FOR_RTMP", "30.0");
        EXPECT_EQ(30 * SRS_UTIME_SECONDS, conf.get_rtc_pli_for_rtmp("__defaultVhost__"));
    }

    // Test environment variable with zero value (should reset to default)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        SrsSetEnvConfig(conf, pli_for_rtmp3, "SRS_VHOST_RTC_PLI_FOR_RTMP", "0");
        EXPECT_EQ(6 * SRS_UTIME_SECONDS, conf.get_rtc_pli_for_rtmp("__defaultVhost__"));
    }

    // Test environment variable with negative value (should reset to default)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        SrsSetEnvConfig(conf, pli_for_rtmp4, "SRS_VHOST_RTC_PLI_FOR_RTMP", "-5");
        EXPECT_EQ(6 * SRS_UTIME_SECONDS, conf.get_rtc_pli_for_rtmp("__defaultVhost__"));
    }

    // Test environment variable overrides config even when config is valid
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{pli_for_rtmp 5;}}"));

        SrsSetEnvConfig(conf, pli_for_rtmp5, "SRS_VHOST_RTC_PLI_FOR_RTMP", "10");

        // Environment should override valid config value
        EXPECT_EQ(10 * SRS_UTIME_SECONDS, conf.get_rtc_pli_for_rtmp("test.com"));
    }

    // Test environment variable overrides config even when env value gets reset to default
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{pli_for_rtmp 5;}}"));

        SrsSetEnvConfig(conf, pli_for_rtmp6, "SRS_VHOST_RTC_PLI_FOR_RTMP", "100");

        // Environment should override config, but invalid env value resets to default
        EXPECT_EQ(6 * SRS_UTIME_SECONDS, conf.get_rtc_pli_for_rtmp("test.com"));
    }
}

VOID TEST(ConfigRtcTest, CheckRtcNackEnabled)
{
    srs_error_t err;

    // Test default value when no configuration is provided
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        // Default should be true
        EXPECT_TRUE(conf.get_rtc_nack_enabled("__defaultVhost__"));
        EXPECT_TRUE(conf.get_rtc_nack_enabled("test.com"));
    }

    // Test default value when vhost exists but no rtc section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{enabled on;}}"));

        // Should return default value when no rtc section
        EXPECT_TRUE(conf.get_rtc_nack_enabled("test.com"));
    }

    // Test default value when rtc section exists but no nack config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{enabled on;}}"));

        // Should return default value when no nack config
        EXPECT_TRUE(conf.get_rtc_nack_enabled("test.com"));
    }

    // Test explicit nack enabled
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{nack on;}}"));

        EXPECT_TRUE(conf.get_rtc_nack_enabled("test.com"));
    }

    // Test explicit nack disabled
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{nack off;}}"));

        EXPECT_FALSE(conf.get_rtc_nack_enabled("test.com"));
    }

    // Test various boolean values (SRS_CONF_PREFER_TRUE: only "off" is false)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{nack true;}}"));
        EXPECT_TRUE(conf.get_rtc_nack_enabled("test.com")); // "true" != "off", so it's true

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{nack false;}}"));
        EXPECT_TRUE(conf.get_rtc_nack_enabled("test.com")); // "false" != "off", so it's true

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{nack yes;}}"));
        EXPECT_TRUE(conf.get_rtc_nack_enabled("test.com")); // "yes" != "off", so it's true

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{nack no;}}"));
        EXPECT_TRUE(conf.get_rtc_nack_enabled("test.com")); // "no" != "off", so it's true
    }
}

VOID TEST(ConfigRtcTest, CheckRtcNackNoCopy)
{
    srs_error_t err;

    // Test default value when no configuration is provided
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        // Default should be true
        EXPECT_TRUE(conf.get_rtc_nack_no_copy("__defaultVhost__"));
        EXPECT_TRUE(conf.get_rtc_nack_no_copy("test.com"));
    }

    // Test default value when vhost exists but no rtc section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{enabled on;}}"));

        // Should return default value when no rtc section
        EXPECT_TRUE(conf.get_rtc_nack_no_copy("test.com"));
    }

    // Test default value when rtc section exists but no nack_no_copy config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{enabled on;}}"));

        // Should return default value when no nack_no_copy config
        EXPECT_TRUE(conf.get_rtc_nack_no_copy("test.com"));
    }

    // Test default value when nack_no_copy has empty argument
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{nack_no_copy;}}"));

        // Should return default value when empty argument
        EXPECT_TRUE(conf.get_rtc_nack_no_copy("test.com"));
    }

    // Test explicit nack_no_copy enabled
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{nack_no_copy on;}}"));

        EXPECT_TRUE(conf.get_rtc_nack_no_copy("test.com"));
    }

    // Test explicit nack_no_copy disabled
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{nack_no_copy off;}}"));

        EXPECT_FALSE(conf.get_rtc_nack_no_copy("test.com"));
    }

    // Test various boolean values (SRS_CONF_PREFER_TRUE: only "off" is false)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{nack_no_copy true;}}"));
        EXPECT_TRUE(conf.get_rtc_nack_no_copy("test.com")); // "true" != "off", so it's true

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{nack_no_copy false;}}"));
        EXPECT_TRUE(conf.get_rtc_nack_no_copy("test.com")); // "false" != "off", so it's true

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{nack_no_copy yes;}}"));
        EXPECT_TRUE(conf.get_rtc_nack_no_copy("test.com")); // "yes" != "off", so it's true

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{nack_no_copy no;}}"));
        EXPECT_TRUE(conf.get_rtc_nack_no_copy("test.com")); // "no" != "off", so it's true
    }
}

VOID TEST(ConfigRtcTest, CheckRtcTwccEnabled)
{
    srs_error_t err;

    // Test default value when no configuration is provided
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        // Default should be true
        EXPECT_TRUE(conf.get_rtc_twcc_enabled("__defaultVhost__"));
        EXPECT_TRUE(conf.get_rtc_twcc_enabled("test.com"));
    }

    // Test default value when vhost exists but no rtc section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{enabled on;}}"));

        // Should return default value when no rtc section
        EXPECT_TRUE(conf.get_rtc_twcc_enabled("test.com"));
    }

    // Test default value when rtc section exists but no twcc config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{enabled on;}}"));

        // Should return default value when no twcc config
        EXPECT_TRUE(conf.get_rtc_twcc_enabled("test.com"));
    }

    // Test explicit twcc enabled
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{twcc on;}}"));

        EXPECT_TRUE(conf.get_rtc_twcc_enabled("test.com"));
    }

    // Test explicit twcc disabled
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{twcc off;}}"));

        EXPECT_FALSE(conf.get_rtc_twcc_enabled("test.com"));
    }

    // Test various boolean values (SRS_CONF_PREFER_TRUE: only "off" is false)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{twcc true;}}"));
        EXPECT_TRUE(conf.get_rtc_twcc_enabled("test.com")); // "true" != "off", so it's true

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{twcc false;}}"));
        EXPECT_TRUE(conf.get_rtc_twcc_enabled("test.com")); // "false" != "off", so it's true

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{twcc yes;}}"));
        EXPECT_TRUE(conf.get_rtc_twcc_enabled("test.com")); // "yes" != "off", so it's true

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{twcc no;}}"));
        EXPECT_TRUE(conf.get_rtc_twcc_enabled("test.com")); // "no" != "off", so it's true
    }
}

VOID TEST(ConfigRtcTest, CheckRtcOpusBitrate)
{
    srs_error_t err;

    // Test default value when no configuration is provided
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        // Default should be 48000
        EXPECT_EQ(48000, conf.get_rtc_opus_bitrate("__defaultVhost__"));
        EXPECT_EQ(48000, conf.get_rtc_opus_bitrate("test.com"));
    }

    // Test default value when vhost exists but no rtc section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{enabled on;}}"));

        // Should return default value when no rtc section
        EXPECT_EQ(48000, conf.get_rtc_opus_bitrate("test.com"));
    }

    // Test default value when rtc section exists but no opus_bitrate config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{enabled on;}}"));

        // Should return default value when no opus_bitrate config
        EXPECT_EQ(48000, conf.get_rtc_opus_bitrate("test.com"));
    }

    // Test default value when opus_bitrate has empty argument
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{opus_bitrate;}}"));

        // Should return default value when empty argument
        EXPECT_EQ(48000, conf.get_rtc_opus_bitrate("test.com"));
    }

    // Test valid opus bitrate values
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{opus_bitrate 64000;}}"));
        EXPECT_EQ(64000, conf.get_rtc_opus_bitrate("test.com"));

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{opus_bitrate 8000;}}"));
        EXPECT_EQ(8000, conf.get_rtc_opus_bitrate("test.com"));

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{opus_bitrate 320000;}}"));
        EXPECT_EQ(320000, conf.get_rtc_opus_bitrate("test.com"));
    }

    // Test invalid opus bitrate values (should reset to default)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{opus_bitrate 7999;}}"));
        EXPECT_EQ(48000, conf.get_rtc_opus_bitrate("test.com")); // Below minimum, reset to default

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{opus_bitrate 320001;}}"));
        EXPECT_EQ(48000, conf.get_rtc_opus_bitrate("test.com")); // Above maximum, reset to default

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{opus_bitrate 0;}}"));
        EXPECT_EQ(48000, conf.get_rtc_opus_bitrate("test.com")); // Zero, reset to default

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{opus_bitrate -1000;}}"));
        EXPECT_EQ(48000, conf.get_rtc_opus_bitrate("test.com")); // Negative, reset to default
    }
}

VOID TEST(ConfigRtcTest, CheckRtcOpusBitrateEnvironment)
{
    srs_error_t err;

    // Test environment variable overrides config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{opus_bitrate 64000;}}"));

        SrsSetEnvConfig(conf, opus_bitrate1, "SRS_VHOST_RTC_OPUS_BITRATE", "96000");

        // Environment should override config value
        EXPECT_EQ(96000, conf.get_rtc_opus_bitrate("test.com"));
    }

    // Test environment variable with valid boundary values
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        SrsSetEnvConfig(conf, opus_bitrate2, "SRS_VHOST_RTC_OPUS_BITRATE", "8000");
        EXPECT_EQ(8000, conf.get_rtc_opus_bitrate("test.com"));

        SrsSetEnvConfig(conf, opus_bitrate3, "SRS_VHOST_RTC_OPUS_BITRATE", "320000");
        EXPECT_EQ(320000, conf.get_rtc_opus_bitrate("test.com"));
    }

    // Test environment variable with invalid values (should reset to default)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        SrsSetEnvConfig(conf, opus_bitrate4, "SRS_VHOST_RTC_OPUS_BITRATE", "7999");
        EXPECT_EQ(48000, conf.get_rtc_opus_bitrate("test.com")); // Below minimum, reset to default

        SrsSetEnvConfig(conf, opus_bitrate5, "SRS_VHOST_RTC_OPUS_BITRATE", "320001");
        EXPECT_EQ(48000, conf.get_rtc_opus_bitrate("test.com")); // Above maximum, reset to default

        SrsSetEnvConfig(conf, opus_bitrate6, "SRS_VHOST_RTC_OPUS_BITRATE", "0");
        EXPECT_EQ(48000, conf.get_rtc_opus_bitrate("test.com")); // Zero, reset to default
    }

    // Test environment variable overrides config even when config is valid
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{opus_bitrate 64000;}}"));

        SrsSetEnvConfig(conf, opus_bitrate7, "SRS_VHOST_RTC_OPUS_BITRATE", "128000");

        // Environment should override valid config value
        EXPECT_EQ(128000, conf.get_rtc_opus_bitrate("test.com"));
    }

    // Test environment variable overrides config even when env value gets reset to default
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{opus_bitrate 64000;}}"));

        SrsSetEnvConfig(conf, opus_bitrate8, "SRS_VHOST_RTC_OPUS_BITRATE", "500000");

        // Environment should override config, but invalid env value resets to default
        EXPECT_EQ(48000, conf.get_rtc_opus_bitrate("test.com"));
    }
}

VOID TEST(ConfigRtcTest, CheckRtcAacBitrate)
{
    srs_error_t err;

    // Test default value when no configuration is provided
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        // Default should be 48000
        EXPECT_EQ(48000, conf.get_rtc_aac_bitrate("__defaultVhost__"));
        EXPECT_EQ(48000, conf.get_rtc_aac_bitrate("test.com"));
    }

    // Test default value when vhost exists but no rtc section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{enabled on;}}"));

        // Should return default value when no rtc section
        EXPECT_EQ(48000, conf.get_rtc_aac_bitrate("test.com"));
    }

    // Test default value when rtc section exists but no aac_bitrate config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{enabled on;}}"));

        // Should return default value when no aac_bitrate config
        EXPECT_EQ(48000, conf.get_rtc_aac_bitrate("test.com"));
    }

    // Test default value when aac_bitrate has empty argument
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{aac_bitrate;}}"));

        // Should return default value when empty argument
        EXPECT_EQ(48000, conf.get_rtc_aac_bitrate("test.com"));
    }

    // Test valid aac bitrate values
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{aac_bitrate 64000;}}"));
        EXPECT_EQ(64000, conf.get_rtc_aac_bitrate("test.com"));

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{aac_bitrate 8000;}}"));
        EXPECT_EQ(8000, conf.get_rtc_aac_bitrate("test.com"));

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{aac_bitrate 320000;}}"));
        EXPECT_EQ(320000, conf.get_rtc_aac_bitrate("test.com"));
    }

    // Test invalid aac bitrate values (should reset to default)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{aac_bitrate 7999;}}"));
        EXPECT_EQ(48000, conf.get_rtc_aac_bitrate("test.com")); // Below minimum, reset to default

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{aac_bitrate 320001;}}"));
        EXPECT_EQ(48000, conf.get_rtc_aac_bitrate("test.com")); // Above maximum, reset to default

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{aac_bitrate 0;}}"));
        EXPECT_EQ(48000, conf.get_rtc_aac_bitrate("test.com")); // Zero, reset to default

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{aac_bitrate -1000;}}"));
        EXPECT_EQ(48000, conf.get_rtc_aac_bitrate("test.com")); // Negative, reset to default
    }
}

VOID TEST(ConfigRtcTest, CheckRtcAacBitrateEnvironment)
{
    srs_error_t err;

    // Test environment variable overrides config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{aac_bitrate 64000;}}"));

        SrsSetEnvConfig(conf, aac_bitrate1, "SRS_VHOST_RTC_AAC_BITRATE", "96000");

        // Environment should override config value
        EXPECT_EQ(96000, conf.get_rtc_aac_bitrate("test.com"));
    }

    // Test environment variable with valid boundary values
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        SrsSetEnvConfig(conf, aac_bitrate2, "SRS_VHOST_RTC_AAC_BITRATE", "8000");
        EXPECT_EQ(8000, conf.get_rtc_aac_bitrate("test.com"));

        SrsSetEnvConfig(conf, aac_bitrate3, "SRS_VHOST_RTC_AAC_BITRATE", "320000");
        EXPECT_EQ(320000, conf.get_rtc_aac_bitrate("test.com"));
    }

    // Test environment variable with invalid values (should reset to default)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        SrsSetEnvConfig(conf, aac_bitrate4, "SRS_VHOST_RTC_AAC_BITRATE", "7999");
        EXPECT_EQ(48000, conf.get_rtc_aac_bitrate("test.com")); // Below minimum, reset to default

        SrsSetEnvConfig(conf, aac_bitrate5, "SRS_VHOST_RTC_AAC_BITRATE", "320001");
        EXPECT_EQ(48000, conf.get_rtc_aac_bitrate("test.com")); // Above maximum, reset to default

        SrsSetEnvConfig(conf, aac_bitrate6, "SRS_VHOST_RTC_AAC_BITRATE", "0");
        EXPECT_EQ(48000, conf.get_rtc_aac_bitrate("test.com")); // Zero, reset to default
    }

    // Test environment variable overrides config even when config is valid
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{aac_bitrate 64000;}}"));

        SrsSetEnvConfig(conf, aac_bitrate7, "SRS_VHOST_RTC_AAC_BITRATE", "128000");

        // Environment should override valid config value
        EXPECT_EQ(128000, conf.get_rtc_aac_bitrate("test.com"));
    }

    // Test environment variable overrides config even when env value gets reset to default
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{aac_bitrate 64000;}}"));

        SrsSetEnvConfig(conf, aac_bitrate8, "SRS_VHOST_RTC_AAC_BITRATE", "500000");

        // Environment should override config, but invalid env value resets to default
        EXPECT_EQ(48000, conf.get_rtc_aac_bitrate("test.com"));
    }
}

VOID TEST(ConfigVhostTest, CheckGopCacheMaxFrames)
{
    srs_error_t err;

    // Test default value when no configuration is provided
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        // Default should be 2500
        EXPECT_EQ(2500, conf.get_gop_cache_max_frames("__defaultVhost__"));
        EXPECT_EQ(2500, conf.get_gop_cache_max_frames("test.com"));
    }

    // Test default value when vhost exists but no play section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{enabled on;}}"));

        // Should return default value when no play section
        EXPECT_EQ(2500, conf.get_gop_cache_max_frames("test.com"));
    }

    // Test default value when play section exists but no gop_cache_max_frames config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{play{mw_latency 350;}}"));

        // Should return default value when no gop_cache_max_frames config
        EXPECT_EQ(2500, conf.get_gop_cache_max_frames("test.com"));
    }

    // Test default value when gop_cache_max_frames has empty argument
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{play{gop_cache_max_frames;}}"));

        // Should return default value when empty argument
        EXPECT_EQ(2500, conf.get_gop_cache_max_frames("test.com"));
    }

    // Test valid gop_cache_max_frames values
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{play{gop_cache_max_frames 1000;}}"));
        EXPECT_EQ(1000, conf.get_gop_cache_max_frames("test.com"));

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{play{gop_cache_max_frames 5000;}}"));
        EXPECT_EQ(5000, conf.get_gop_cache_max_frames("test.com"));

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{play{gop_cache_max_frames 0;}}"));
        EXPECT_EQ(0, conf.get_gop_cache_max_frames("test.com"));
    }

    // Test negative values (should be parsed as is)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{play{gop_cache_max_frames -100;}}"));
        EXPECT_EQ(-100, conf.get_gop_cache_max_frames("test.com"));
    }
}

VOID TEST(ConfigVhostTest, CheckDebugSrsUpnode)
{
    srs_error_t err;

    // Test default value when no configuration is provided
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        // Default should be true
        EXPECT_TRUE(conf.get_debug_srs_upnode("__defaultVhost__"));
        EXPECT_TRUE(conf.get_debug_srs_upnode("test.com"));
    }

    // Test default value when vhost exists but no cluster section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{enabled on;}}"));

        // Should return default value when no cluster section
        EXPECT_TRUE(conf.get_debug_srs_upnode("test.com"));
    }

    // Test default value when cluster exists but no debug_srs_upnode config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{cluster{mode local;}}"));

        // Should return default value when no debug_srs_upnode config
        EXPECT_TRUE(conf.get_debug_srs_upnode("test.com"));
    }

    // Test default value when debug_srs_upnode has empty argument
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{cluster{debug_srs_upnode;}}"));

        // Should return default value when empty argument
        EXPECT_TRUE(conf.get_debug_srs_upnode("test.com"));
    }

    // Test explicit debug_srs_upnode enabled
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{cluster{debug_srs_upnode on;}}"));

        EXPECT_TRUE(conf.get_debug_srs_upnode("test.com"));
    }

    // Test explicit debug_srs_upnode disabled
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{cluster{debug_srs_upnode off;}}"));

        EXPECT_FALSE(conf.get_debug_srs_upnode("test.com"));
    }

    // Test various boolean values (SRS_CONF_PREFER_TRUE: only "off" is false)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{cluster{debug_srs_upnode true;}}"));
        EXPECT_TRUE(conf.get_debug_srs_upnode("test.com")); // "true" != "off", so it's true

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{cluster{debug_srs_upnode false;}}"));
        EXPECT_TRUE(conf.get_debug_srs_upnode("test.com")); // "false" != "off", so it's true

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{cluster{debug_srs_upnode yes;}}"));
        EXPECT_TRUE(conf.get_debug_srs_upnode("test.com")); // "yes" != "off", so it's true

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{cluster{debug_srs_upnode no;}}"));
        EXPECT_TRUE(conf.get_debug_srs_upnode("test.com")); // "no" != "off", so it's true
    }
}

VOID TEST(ConfigVhostTest, CheckAtc)
{
    srs_error_t err;

    // Test default value when no configuration is provided
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        // Default should be false (SRS_CONF_PREFER_FALSE)
        EXPECT_FALSE(conf.get_atc("__defaultVhost__"));
        EXPECT_FALSE(conf.get_atc("test.com"));
    }

    // Test default value when vhost exists but no play section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{enabled on;}}"));

        // Should return default value when no play section
        EXPECT_FALSE(conf.get_atc("test.com"));
    }

    // Test default value when play exists but no atc config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{play{mw_latency 350;}}"));

        // Should return default value when no atc config
        EXPECT_FALSE(conf.get_atc("test.com"));
    }

    // Test default value when atc has empty argument
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{play{atc;}}"));

        // Should return default value when empty argument
        EXPECT_FALSE(conf.get_atc("test.com"));
    }

    // Test explicit atc enabled
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{play{atc on;}}"));

        EXPECT_TRUE(conf.get_atc("test.com"));
    }

    // Test explicit atc disabled
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{play{atc off;}}"));

        EXPECT_FALSE(conf.get_atc("test.com"));
    }

    // Test various boolean values (SRS_CONF_PREFER_FALSE: only "on" is true)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{play{atc true;}}"));
        EXPECT_FALSE(conf.get_atc("test.com")); // "true" != "on", so it's false

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{play{atc false;}}"));
        EXPECT_FALSE(conf.get_atc("test.com")); // "false" != "on", so it's false

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{play{atc yes;}}"));
        EXPECT_FALSE(conf.get_atc("test.com")); // "yes" != "on", so it's false

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{play{atc no;}}"));
        EXPECT_FALSE(conf.get_atc("test.com")); // "no" != "on", so it's false
    }
}

VOID TEST(ConfigVhostTest, CheckAtcAuto)
{
    srs_error_t err;

    // Test default value when no configuration is provided
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        // Default should be false (SRS_CONF_PREFER_FALSE)
        EXPECT_FALSE(conf.get_atc_auto("__defaultVhost__"));
        EXPECT_FALSE(conf.get_atc_auto("test.com"));
    }

    // Test default value when vhost exists but no play section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{enabled on;}}"));

        // Should return default value when no play section
        EXPECT_FALSE(conf.get_atc_auto("test.com"));
    }

    // Test default value when play exists but no atc_auto config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{play{mw_latency 350;}}"));

        // Should return default value when no atc_auto config
        EXPECT_FALSE(conf.get_atc_auto("test.com"));
    }

    // Test default value when atc_auto has empty argument
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{play{atc_auto;}}"));

        // Should return default value when empty argument
        EXPECT_FALSE(conf.get_atc_auto("test.com"));
    }

    // Test explicit atc_auto enabled
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{play{atc_auto on;}}"));

        EXPECT_TRUE(conf.get_atc_auto("test.com"));
    }

    // Test explicit atc_auto disabled
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{play{atc_auto off;}}"));

        EXPECT_FALSE(conf.get_atc_auto("test.com"));
    }

    // Test various boolean values (SRS_CONF_PREFER_FALSE: only "on" is true)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{play{atc_auto true;}}"));
        EXPECT_FALSE(conf.get_atc_auto("test.com")); // "true" != "on", so it's false

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{play{atc_auto false;}}"));
        EXPECT_FALSE(conf.get_atc_auto("test.com")); // "false" != "on", so it's false

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{play{atc_auto yes;}}"));
        EXPECT_FALSE(conf.get_atc_auto("test.com")); // "yes" != "on", so it's false

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{play{atc_auto no;}}"));
        EXPECT_FALSE(conf.get_atc_auto("test.com")); // "no" != "on", so it's false
    }
}

VOID TEST(ConfigVhostTest, CheckTimeJitter)
{
    srs_error_t err;

    // Test default value when no configuration is provided
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        // Default should be 1 (SrsRtmpJitterAlgorithmFULL) - default is "full"
        EXPECT_EQ(1, conf.get_time_jitter("__defaultVhost__"));
        EXPECT_EQ(1, conf.get_time_jitter("test.com"));
    }

    // Test default value when vhost exists but no play section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{enabled on;}}"));

        // Should return default value when no play section
        EXPECT_EQ(1, conf.get_time_jitter("test.com"));
    }

    // Test default value when play exists but no time_jitter config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{play{mw_latency 350;}}"));

        // Should return default value when no time_jitter config
        EXPECT_EQ(1, conf.get_time_jitter("test.com"));
    }

    // Test default value when time_jitter has empty argument
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{play{time_jitter;}}"));

        // Should return default value when empty argument
        EXPECT_EQ(1, conf.get_time_jitter("test.com"));
    }

    // Test various time_jitter string values
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{play{time_jitter off;}}"));
        EXPECT_EQ(3, conf.get_time_jitter("test.com")); // SrsRtmpJitterAlgorithmOFF = 3

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{play{time_jitter full;}}"));
        EXPECT_EQ(1, conf.get_time_jitter("test.com")); // SrsRtmpJitterAlgorithmFULL = 1

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{play{time_jitter zero;}}"));
        EXPECT_EQ(2, conf.get_time_jitter("test.com")); // SrsRtmpJitterAlgorithmZERO = 2
    }

    // Test invalid time_jitter values (should return OFF=3)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{play{time_jitter invalid;}}"));
        EXPECT_EQ(3, conf.get_time_jitter("test.com")); // Should return OFF=3 for invalid value

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{play{time_jitter simple;}}"));
        EXPECT_EQ(3, conf.get_time_jitter("test.com")); // "simple" is not supported, returns OFF=3
    }
}

VOID TEST(ConfigVhostTest, CheckMixCorrect)
{
    srs_error_t err;

    // Test default value when no configuration is provided
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        // Default should be false (SRS_CONF_PREFER_FALSE)
        EXPECT_FALSE(conf.get_mix_correct("__defaultVhost__"));
        EXPECT_FALSE(conf.get_mix_correct("test.com"));
    }

    // Test default value when vhost exists but no play section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{enabled on;}}"));

        // Should return default value when no play section
        EXPECT_FALSE(conf.get_mix_correct("test.com"));
    }

    // Test default value when play exists but no mix_correct config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{play{mw_latency 350;}}"));

        // Should return default value when no mix_correct config
        EXPECT_FALSE(conf.get_mix_correct("test.com"));
    }

    // Test default value when mix_correct has empty argument
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{play{mix_correct;}}"));

        // Should return default value when empty argument
        EXPECT_FALSE(conf.get_mix_correct("test.com"));
    }

    // Test explicit mix_correct enabled
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{play{mix_correct on;}}"));

        EXPECT_TRUE(conf.get_mix_correct("test.com"));
    }

    // Test explicit mix_correct disabled
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{play{mix_correct off;}}"));

        EXPECT_FALSE(conf.get_mix_correct("test.com"));
    }

    // Test various boolean values (SRS_CONF_PREFER_FALSE: only "on" is true)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{play{mix_correct true;}}"));
        EXPECT_FALSE(conf.get_mix_correct("test.com")); // "true" != "on", so it's false

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{play{mix_correct false;}}"));
        EXPECT_FALSE(conf.get_mix_correct("test.com")); // "false" != "on", so it's false

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{play{mix_correct yes;}}"));
        EXPECT_FALSE(conf.get_mix_correct("test.com")); // "yes" != "on", so it's false

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{play{mix_correct no;}}"));
        EXPECT_FALSE(conf.get_mix_correct("test.com")); // "no" != "on", so it's false
    }
}

VOID TEST(ConfigHlsTest, CheckHlsM3u8File)
{
    srs_error_t err;

    // Test default value when no configuration is provided
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        // Default should be "[app]/[stream].m3u8"
        EXPECT_STREQ("[app]/[stream].m3u8", conf.get_hls_m3u8_file("__defaultVhost__").c_str());
        EXPECT_STREQ("[app]/[stream].m3u8", conf.get_hls_m3u8_file("test.com").c_str());
    }

    // Test default value when vhost exists but no hls section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{play{mw_latency 350;}}"));

        // Should return default value when no hls section
        EXPECT_STREQ("[app]/[stream].m3u8", conf.get_hls_m3u8_file("test.com").c_str());
    }

    // Test default value when hls section exists but no hls_m3u8_file config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{enabled on;}}"));

        // Should return default value when no hls_m3u8_file config
        EXPECT_STREQ("[app]/[stream].m3u8", conf.get_hls_m3u8_file("test.com").c_str());
    }

    // Test default value when hls_m3u8_file has empty argument
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_m3u8_file;}}"));

        // Should return default value when empty argument
        EXPECT_STREQ("[app]/[stream].m3u8", conf.get_hls_m3u8_file("test.com").c_str());
    }

    // Test custom hls_m3u8_file values
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_m3u8_file custom/[stream].m3u8;}}"));
        EXPECT_STREQ("custom/[stream].m3u8", conf.get_hls_m3u8_file("test.com").c_str());

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_m3u8_file [app]/live.m3u8;}}"));
        EXPECT_STREQ("[app]/live.m3u8", conf.get_hls_m3u8_file("test.com").c_str());

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_m3u8_file /path/to/[stream]-[seq].m3u8;}}"));
        EXPECT_STREQ("/path/to/[stream]-[seq].m3u8", conf.get_hls_m3u8_file("test.com").c_str());
    }
}

VOID TEST(ConfigHlsTest, CheckHlsM3u8FileEnvironment)
{
    srs_error_t err;

    // Test environment variable overrides config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_m3u8_file custom.m3u8;}}"));

        SrsSetEnvConfig(conf, hls_m3u8_file, "SRS_VHOST_HLS_HLS_M3U8_FILE", "env/[stream].m3u8");

        // Environment should override config value
        EXPECT_STREQ("env/[stream].m3u8", conf.get_hls_m3u8_file("test.com").c_str());
    }

    // Test environment variable with default vhost
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        SrsSetEnvConfig(conf, hls_m3u8_file2, "SRS_VHOST_HLS_HLS_M3U8_FILE", "global/[app]/[stream].m3u8");

        EXPECT_STREQ("global/[app]/[stream].m3u8", conf.get_hls_m3u8_file("__defaultVhost__").c_str());
    }

    // Test empty environment variable (should use config file value)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_m3u8_file config.m3u8;}}"));

        SrsSetEnvConfig(conf, hls_m3u8_file3, "SRS_VHOST_HLS_HLS_M3U8_FILE", "");

        // Should use config file value when env is empty
        EXPECT_STREQ("config.m3u8", conf.get_hls_m3u8_file("test.com").c_str());
    }
}

VOID TEST(ConfigHlsTest, CheckHlsTsFile)
{
    srs_error_t err;

    // Test default value when no configuration is provided
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        // Default should be "[app]/[stream]-[seq].ts"
        EXPECT_STREQ("[app]/[stream]-[seq].ts", conf.get_hls_ts_file("__defaultVhost__").c_str());
        EXPECT_STREQ("[app]/[stream]-[seq].ts", conf.get_hls_ts_file("test.com").c_str());
    }

    // Test default value when vhost exists but no hls section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{play{mw_latency 350;}}"));

        // Should return default value when no hls section
        EXPECT_STREQ("[app]/[stream]-[seq].ts", conf.get_hls_ts_file("test.com").c_str());
    }

    // Test default value when hls section exists but no hls_ts_file config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{enabled on;}}"));

        // Should return default value when no hls_ts_file config
        EXPECT_STREQ("[app]/[stream]-[seq].ts", conf.get_hls_ts_file("test.com").c_str());
    }

    // Test default value when hls_ts_file has empty argument
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_ts_file;}}"));

        // Should return default value when empty argument
        EXPECT_STREQ("[app]/[stream]-[seq].ts", conf.get_hls_ts_file("test.com").c_str());
    }

    // Test custom hls_ts_file values
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_ts_file custom/[stream]-[seq].ts;}}"));
        EXPECT_STREQ("custom/[stream]-[seq].ts", conf.get_hls_ts_file("test.com").c_str());

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_ts_file [app]/segment-[seq].ts;}}"));
        EXPECT_STREQ("[app]/segment-[seq].ts", conf.get_hls_ts_file("test.com").c_str());

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_ts_file /path/to/[stream]/[seq].ts;}}"));
        EXPECT_STREQ("/path/to/[stream]/[seq].ts", conf.get_hls_ts_file("test.com").c_str());
    }

    // Test environment variable overrides config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_ts_file custom.ts;}}"));

        SrsSetEnvConfig(conf, hls_ts_file, "SRS_VHOST_HLS_HLS_TS_FILE", "env/[stream]-[seq].ts");

        // Environment should override config value
        EXPECT_STREQ("env/[stream]-[seq].ts", conf.get_hls_ts_file("test.com").c_str());
    }
}

VOID TEST(ConfigHlsTest, CheckHlsFmp4File)
{
    srs_error_t err;

    // Test default value when no configuration is provided
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        // Default should be "[app]/[stream]-[seq].m4s"
        EXPECT_STREQ("[app]/[stream]-[seq].m4s", conf.get_hls_fmp4_file("__defaultVhost__").c_str());
        EXPECT_STREQ("[app]/[stream]-[seq].m4s", conf.get_hls_fmp4_file("test.com").c_str());
    }

    // Test default value when vhost exists but no hls section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{play{mw_latency 350;}}"));

        // Should return default value when no hls section
        EXPECT_STREQ("[app]/[stream]-[seq].m4s", conf.get_hls_fmp4_file("test.com").c_str());
    }

    // Test default value when hls section exists but no hls_fmp4_file config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{enabled on;}}"));

        // Should return default value when no hls_fmp4_file config
        EXPECT_STREQ("[app]/[stream]-[seq].m4s", conf.get_hls_fmp4_file("test.com").c_str());
    }

    // Test default value when hls_fmp4_file has empty argument
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_fmp4_file;}}"));

        // Should return default value when empty argument
        EXPECT_STREQ("[app]/[stream]-[seq].m4s", conf.get_hls_fmp4_file("test.com").c_str());
    }

    // Test custom hls_fmp4_file values
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_fmp4_file custom/[stream]-[seq].m4s;}}"));
        EXPECT_STREQ("custom/[stream]-[seq].m4s", conf.get_hls_fmp4_file("test.com").c_str());

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_fmp4_file [app]/segment-[seq].m4s;}}"));
        EXPECT_STREQ("[app]/segment-[seq].m4s", conf.get_hls_fmp4_file("test.com").c_str());

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_fmp4_file /path/to/[stream]/[seq].m4s;}}"));
        EXPECT_STREQ("/path/to/[stream]/[seq].m4s", conf.get_hls_fmp4_file("test.com").c_str());
    }

    // Test environment variable overrides config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_fmp4_file custom.m4s;}}"));

        SrsSetEnvConfig(conf, hls_fmp4_file, "SRS_VHOST_HLS_HLS_FMP4_FILE", "env/[stream]-[seq].m4s");

        // Environment should override config value
        EXPECT_STREQ("env/[stream]-[seq].m4s", conf.get_hls_fmp4_file("test.com").c_str());
    }
}

VOID TEST(ConfigHlsTest, CheckHlsInitFile)
{
    srs_error_t err;

    // Test default value when no configuration is provided
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        // Default should be "[app]/[stream]/init.mp4"
        EXPECT_STREQ("[app]/[stream]/init.mp4", conf.get_hls_init_file("__defaultVhost__").c_str());
        EXPECT_STREQ("[app]/[stream]/init.mp4", conf.get_hls_init_file("test.com").c_str());
    }

    // Test default value when vhost exists but no hls section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{play{mw_latency 350;}}"));

        // Should return default value when no hls section
        EXPECT_STREQ("[app]/[stream]/init.mp4", conf.get_hls_init_file("test.com").c_str());
    }

    // Test default value when hls section exists but no hls_init_file config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{enabled on;}}"));

        // Should return default value when no hls_init_file config
        EXPECT_STREQ("[app]/[stream]/init.mp4", conf.get_hls_init_file("test.com").c_str());
    }

    // Test default value when hls_init_file has empty argument
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_init_file;}}"));

        // Should return default value when empty argument
        EXPECT_STREQ("[app]/[stream]/init.mp4", conf.get_hls_init_file("test.com").c_str());
    }

    // Test custom hls_init_file values
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_init_file custom/[stream]/init.mp4;}}"));
        EXPECT_STREQ("custom/[stream]/init.mp4", conf.get_hls_init_file("test.com").c_str());

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_init_file [app]/[stream]-init.mp4;}}"));
        EXPECT_STREQ("[app]/[stream]-init.mp4", conf.get_hls_init_file("test.com").c_str());

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_init_file /path/to/[stream]/initialization.mp4;}}"));
        EXPECT_STREQ("/path/to/[stream]/initialization.mp4", conf.get_hls_init_file("test.com").c_str());
    }

    // Test environment variable overrides config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_init_file custom-init.mp4;}}"));

        SrsSetEnvConfig(conf, hls_init_file, "SRS_VHOST_HLS_HLS_INIT_FILE", "env/[stream]/init.mp4");

        // Environment should override config value
        EXPECT_STREQ("env/[stream]/init.mp4", conf.get_hls_init_file("test.com").c_str());
    }
}

VOID TEST(ConfigHlsTest, CheckHlsTsFloor)
{
    srs_error_t err;

    // Test default value when no configuration is provided
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        // Default should be false (SRS_CONF_PREFER_FALSE)
        EXPECT_FALSE(conf.get_hls_ts_floor("__defaultVhost__"));
        EXPECT_FALSE(conf.get_hls_ts_floor("test.com"));
    }

    // Test default value when vhost exists but no hls section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{play{mw_latency 350;}}"));

        // Should return default value when no hls section
        EXPECT_FALSE(conf.get_hls_ts_floor("test.com"));
    }

    // Test default value when hls section exists but no hls_ts_floor config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{enabled on;}}"));

        // Should return default value when no hls_ts_floor config
        EXPECT_FALSE(conf.get_hls_ts_floor("test.com"));
    }

    // Test default value when hls_ts_floor has empty argument
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_ts_floor;}}"));

        // Should return default value when empty argument
        EXPECT_FALSE(conf.get_hls_ts_floor("test.com"));
    }

    // Test explicit hls_ts_floor enabled
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_ts_floor on;}}"));

        EXPECT_TRUE(conf.get_hls_ts_floor("test.com"));
    }

    // Test explicit hls_ts_floor disabled
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_ts_floor off;}}"));

        EXPECT_FALSE(conf.get_hls_ts_floor("test.com"));
    }

    // Test various boolean values (SRS_CONF_PREFER_FALSE: only "on" is true)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_ts_floor true;}}"));
        EXPECT_FALSE(conf.get_hls_ts_floor("test.com")); // "true" != "on", so it's false

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_ts_floor false;}}"));
        EXPECT_FALSE(conf.get_hls_ts_floor("test.com")); // "false" != "on", so it's false

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_ts_floor yes;}}"));
        EXPECT_FALSE(conf.get_hls_ts_floor("test.com")); // "yes" != "on", so it's false

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_ts_floor no;}}"));
        EXPECT_FALSE(conf.get_hls_ts_floor("test.com")); // "no" != "on", so it's false
    }

    // Test environment variable overrides config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_ts_floor off;}}"));

        SrsSetEnvConfig(conf, hls_ts_floor, "SRS_VHOST_HLS_HLS_TS_FLOOR", "on");

        // Environment should override config value
        EXPECT_TRUE(conf.get_hls_ts_floor("test.com"));
    }
}

VOID TEST(ConfigHlsTest, CheckHlsFragment)
{
    srs_error_t err;

    // Test default value when no configuration is provided
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        // Default should be 10 seconds
        EXPECT_EQ(10 * SRS_UTIME_SECONDS, conf.get_hls_fragment("__defaultVhost__"));
        EXPECT_EQ(10 * SRS_UTIME_SECONDS, conf.get_hls_fragment("test.com"));
    }

    // Test default value when vhost exists but no hls section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{play{mw_latency 350;}}"));

        // Should return default value when no hls section
        EXPECT_EQ(10 * SRS_UTIME_SECONDS, conf.get_hls_fragment("test.com"));
    }

    // Test default value when hls section exists but no hls_fragment config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{enabled on;}}"));

        // Should return default value when no hls_fragment config
        EXPECT_EQ(10 * SRS_UTIME_SECONDS, conf.get_hls_fragment("test.com"));
    }

    // Test default value when hls_fragment has empty argument
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_fragment;}}"));

        // Should return default value when empty argument
        EXPECT_EQ(10 * SRS_UTIME_SECONDS, conf.get_hls_fragment("test.com"));
    }

    // Test valid hls_fragment values
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_fragment 5;}}"));
        EXPECT_EQ(5 * SRS_UTIME_SECONDS, conf.get_hls_fragment("test.com"));

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_fragment 15;}}"));
        EXPECT_EQ(15 * SRS_UTIME_SECONDS, conf.get_hls_fragment("test.com"));

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_fragment 2.5;}}"));
        EXPECT_EQ((srs_utime_t)(2.5 * SRS_UTIME_SECONDS), conf.get_hls_fragment("test.com"));

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_fragment 0.5;}}"));
        EXPECT_EQ(500 * SRS_UTIME_MILLISECONDS, conf.get_hls_fragment("test.com"));
    }

    // Test environment variable overrides config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_fragment 5;}}"));

        SrsSetEnvConfig(conf, hls_fragment, "SRS_VHOST_HLS_HLS_FRAGMENT", "8");

        // Environment should override config value
        EXPECT_EQ(8 * SRS_UTIME_SECONDS, conf.get_hls_fragment("test.com"));
    }

    // Test environment variable with decimal value
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        SrsSetEnvConfig(conf, hls_fragment2, "SRS_VHOST_HLS_HLS_FRAGMENT", "3.5");

        EXPECT_EQ((srs_utime_t)(3.5 * SRS_UTIME_SECONDS), conf.get_hls_fragment("__defaultVhost__"));
    }
}

VOID TEST(ConfigHlsTest, CheckHlsTdRatio)
{
    srs_error_t err;

    // Test default value when no configuration is provided
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        // Default should be 1.0
        EXPECT_EQ(1.0, conf.get_hls_td_ratio("__defaultVhost__"));
        EXPECT_EQ(1.0, conf.get_hls_td_ratio("test.com"));
    }

    // Test default value when vhost exists but no hls section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{play{mw_latency 350;}}"));

        // Should return default value when no hls section
        EXPECT_EQ(1.0, conf.get_hls_td_ratio("test.com"));
    }

    // Test default value when hls section exists but no hls_td_ratio config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{enabled on;}}"));

        // Should return default value when no hls_td_ratio config
        EXPECT_EQ(1.0, conf.get_hls_td_ratio("test.com"));
    }

    // Test default value when hls_td_ratio has empty argument
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_td_ratio;}}"));

        // Should return default value when empty argument
        EXPECT_EQ(1.0, conf.get_hls_td_ratio("test.com"));
    }

    // Test valid hls_td_ratio values
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_td_ratio 1.5;}}"));
        EXPECT_EQ(1.5, conf.get_hls_td_ratio("test.com"));

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_td_ratio 2.0;}}"));
        EXPECT_EQ(2.0, conf.get_hls_td_ratio("test.com"));

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_td_ratio 0.8;}}"));
        EXPECT_EQ(0.8, conf.get_hls_td_ratio("test.com"));

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_td_ratio 3;}}"));
        EXPECT_EQ(3.0, conf.get_hls_td_ratio("test.com"));
    }

    // Test environment variable overrides config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_td_ratio 1.5;}}"));

        SrsSetEnvConfig(conf, hls_td_ratio, "SRS_VHOST_HLS_HLS_TD_RATIO", "2.5");

        // Environment should override config value
        EXPECT_EQ(2.5, conf.get_hls_td_ratio("test.com"));
    }
}

VOID TEST(ConfigHlsTest, CheckHlsAofRatio)
{
    srs_error_t err;

    // Test default value when no configuration is provided
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        // Default should be 2.1
        EXPECT_EQ(2.1, conf.get_hls_aof_ratio("__defaultVhost__"));
        EXPECT_EQ(2.1, conf.get_hls_aof_ratio("test.com"));
    }

    // Test default value when vhost exists but no hls section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{play{mw_latency 350;}}"));

        // Should return default value when no hls section
        EXPECT_EQ(2.1, conf.get_hls_aof_ratio("test.com"));
    }

    // Test default value when hls section exists but no hls_aof_ratio config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{enabled on;}}"));

        // Should return default value when no hls_aof_ratio config
        EXPECT_EQ(2.1, conf.get_hls_aof_ratio("test.com"));
    }

    // Test default value when hls_aof_ratio has empty argument
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_aof_ratio;}}"));

        // Should return default value when empty argument
        EXPECT_EQ(2.1, conf.get_hls_aof_ratio("test.com"));
    }

    // Test valid hls_aof_ratio values
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_aof_ratio 1.5;}}"));
        EXPECT_EQ(1.5, conf.get_hls_aof_ratio("test.com"));

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_aof_ratio 3.0;}}"));
        EXPECT_EQ(3.0, conf.get_hls_aof_ratio("test.com"));

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_aof_ratio 0.5;}}"));
        EXPECT_EQ(0.5, conf.get_hls_aof_ratio("test.com"));

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_aof_ratio 5;}}"));
        EXPECT_EQ(5.0, conf.get_hls_aof_ratio("test.com"));
    }

    // Test environment variable overrides config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_aof_ratio 1.5;}}"));

        SrsSetEnvConfig(conf, hls_aof_ratio, "SRS_VHOST_HLS_HLS_AOF_RATIO", "3.5");

        // Environment should override config value
        EXPECT_EQ(3.5, conf.get_hls_aof_ratio("test.com"));
    }
}

VOID TEST(ConfigHlsTest, CheckHlsWindow)
{
    srs_error_t err;

    // Test default value when no configuration is provided
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        // Default should be 60 seconds
        EXPECT_EQ(60 * SRS_UTIME_SECONDS, conf.get_hls_window("__defaultVhost__"));
        EXPECT_EQ(60 * SRS_UTIME_SECONDS, conf.get_hls_window("test.com"));
    }

    // Test default value when vhost exists but no hls section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{play{mw_latency 350;}}"));

        // Should return default value when no hls section
        EXPECT_EQ(60 * SRS_UTIME_SECONDS, conf.get_hls_window("test.com"));
    }

    // Test default value when hls section exists but no hls_window config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{enabled on;}}"));

        // Should return default value when no hls_window config
        EXPECT_EQ(60 * SRS_UTIME_SECONDS, conf.get_hls_window("test.com"));
    }

    // Test default value when hls_window has empty argument
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_window;}}"));

        // Should return default value when empty argument
        EXPECT_EQ(60 * SRS_UTIME_SECONDS, conf.get_hls_window("test.com"));
    }

    // Test valid hls_window values
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_window 30;}}"));
        EXPECT_EQ(30 * SRS_UTIME_SECONDS, conf.get_hls_window("test.com"));

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_window 120;}}"));
        EXPECT_EQ(120 * SRS_UTIME_SECONDS, conf.get_hls_window("test.com"));

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_window 10.5;}}"));
        EXPECT_EQ((srs_utime_t)(10.5 * SRS_UTIME_SECONDS), conf.get_hls_window("test.com"));

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_window 0.5;}}"));
        EXPECT_EQ(500 * SRS_UTIME_MILLISECONDS, conf.get_hls_window("test.com"));
    }

    // Test environment variable overrides config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_window 30;}}"));

        SrsSetEnvConfig(conf, hls_window, "SRS_VHOST_HLS_HLS_WINDOW", "90");

        // Environment should override config value
        EXPECT_EQ(90 * SRS_UTIME_SECONDS, conf.get_hls_window("test.com"));
    }

    // Test environment variable with decimal value
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        SrsSetEnvConfig(conf, hls_window2, "SRS_VHOST_HLS_HLS_WINDOW", "45.5");

        EXPECT_EQ((srs_utime_t)(45.5 * SRS_UTIME_SECONDS), conf.get_hls_window("__defaultVhost__"));
    }
}

VOID TEST(ConfigHlsTest, CheckHlsOnError)
{
    srs_error_t err;

    // Test default value when no configuration is provided
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        // Default should be "continue"
        EXPECT_STREQ("continue", conf.get_hls_on_error("__defaultVhost__").c_str());
        EXPECT_STREQ("continue", conf.get_hls_on_error("test.com").c_str());
    }

    // Test default value when vhost exists but no hls section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{play{mw_latency 350;}}"));

        // Should return default value when no hls section
        EXPECT_STREQ("continue", conf.get_hls_on_error("test.com").c_str());
    }

    // Test default value when hls section exists but no hls_on_error config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{enabled on;}}"));

        // Should return default value when no hls_on_error config
        EXPECT_STREQ("continue", conf.get_hls_on_error("test.com").c_str());
    }

    // Test default value when hls_on_error has empty argument
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_on_error;}}"));

        // Should return default value when empty argument
        EXPECT_STREQ("continue", conf.get_hls_on_error("test.com").c_str());
    }

    // Test custom hls_on_error values
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_on_error disconnect;}}"));
        EXPECT_STREQ("disconnect", conf.get_hls_on_error("test.com").c_str());

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_on_error ignore;}}"));
        EXPECT_STREQ("ignore", conf.get_hls_on_error("test.com").c_str());

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_on_error custom_action;}}"));
        EXPECT_STREQ("custom_action", conf.get_hls_on_error("test.com").c_str());
    }

    // Test environment variable overrides config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_on_error disconnect;}}"));

        SrsSetEnvConfig(conf, hls_on_error, "SRS_VHOST_HLS_HLS_ON_ERROR", "ignore");

        // Environment should override config value
        EXPECT_STREQ("ignore", conf.get_hls_on_error("test.com").c_str());
    }
}

VOID TEST(ConfigHlsTest, CheckVhostHlsNbNotify)
{
    srs_error_t err;

    // Test default value when no configuration is provided
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        // Default should be 64
        EXPECT_EQ(64, conf.get_vhost_hls_nb_notify("__defaultVhost__"));
        EXPECT_EQ(64, conf.get_vhost_hls_nb_notify("test.com"));
    }

    // Test default value when vhost exists but no hls section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{play{mw_latency 350;}}"));

        // Should return default value when no hls section
        EXPECT_EQ(64, conf.get_vhost_hls_nb_notify("test.com"));
    }

    // Test default value when hls section exists but no hls_nb_notify config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{enabled on;}}"));

        // Should return default value when no hls_nb_notify config
        EXPECT_EQ(64, conf.get_vhost_hls_nb_notify("test.com"));
    }

    // Test default value when hls_nb_notify has empty argument
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_nb_notify;}}"));

        // Should return default value when empty argument
        EXPECT_EQ(64, conf.get_vhost_hls_nb_notify("test.com"));
    }

    // Test valid hls_nb_notify values
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_nb_notify 32;}}"));
        EXPECT_EQ(32, conf.get_vhost_hls_nb_notify("test.com"));

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_nb_notify 128;}}"));
        EXPECT_EQ(128, conf.get_vhost_hls_nb_notify("test.com"));

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_nb_notify 0;}}"));
        EXPECT_EQ(0, conf.get_vhost_hls_nb_notify("test.com"));

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_nb_notify 1;}}"));
        EXPECT_EQ(1, conf.get_vhost_hls_nb_notify("test.com"));
    }

    // Test environment variable overrides config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_nb_notify 32;}}"));

        SrsSetEnvConfig(conf, hls_nb_notify, "SRS_VHOST_HLS_HLS_NB_NOTIFY", "256");

        // Environment should override config value
        EXPECT_EQ(256, conf.get_vhost_hls_nb_notify("test.com"));
    }
}

VOID TEST(ConfigHlsTest, CheckVhostHlsDtsDirectly)
{
    srs_error_t err;

    // Test default value when no configuration is provided
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        // Default should be true (SRS_CONF_PREFER_TRUE)
        EXPECT_TRUE(conf.get_vhost_hls_dts_directly("__defaultVhost__"));
        EXPECT_TRUE(conf.get_vhost_hls_dts_directly("test.com"));
    }

    // Test default value when vhost exists but no hls section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{play{mw_latency 350;}}"));

        // Should return default value when no hls section
        EXPECT_TRUE(conf.get_vhost_hls_dts_directly("test.com"));
    }

    // Test default value when hls section exists but no hls_dts_directly config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{enabled on;}}"));

        // Should return default value when no hls_dts_directly config
        EXPECT_TRUE(conf.get_vhost_hls_dts_directly("test.com"));
    }

    // Test default value when hls_dts_directly has empty argument
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_dts_directly;}}"));

        // Should return default value when empty argument
        EXPECT_TRUE(conf.get_vhost_hls_dts_directly("test.com"));
    }

    // Test explicit hls_dts_directly enabled
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_dts_directly on;}}"));

        EXPECT_TRUE(conf.get_vhost_hls_dts_directly("test.com"));
    }

    // Test explicit hls_dts_directly disabled
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_dts_directly off;}}"));

        EXPECT_FALSE(conf.get_vhost_hls_dts_directly("test.com"));
    }

    // Test various boolean values (SRS_CONF_PREFER_TRUE: only "off" is false)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_dts_directly true;}}"));
        EXPECT_TRUE(conf.get_vhost_hls_dts_directly("test.com")); // "true" != "off", so it's true

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_dts_directly false;}}"));
        EXPECT_TRUE(conf.get_vhost_hls_dts_directly("test.com")); // "false" != "off", so it's true

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_dts_directly yes;}}"));
        EXPECT_TRUE(conf.get_vhost_hls_dts_directly("test.com")); // "yes" != "off", so it's true

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_dts_directly no;}}"));
        EXPECT_TRUE(conf.get_vhost_hls_dts_directly("test.com")); // "no" != "off", so it's true
    }

    // Test environment variable overrides config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_dts_directly on;}}"));

        SrsSetEnvConfig(conf, hls_dts_directly, "SRS_VHOST_HLS_HLS_DTS_DIRECTLY", "off");

        // Environment should override config value
        EXPECT_FALSE(conf.get_vhost_hls_dts_directly("test.com"));
    }
}

VOID TEST(ConfigHlsTest, CheckHlsCtxEnabled)
{
    srs_error_t err;

    // Test default value when no configuration is provided
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        // Default should be true (SRS_CONF_PREFER_TRUE)
        EXPECT_TRUE(conf.get_hls_ctx_enabled("__defaultVhost__"));
        EXPECT_TRUE(conf.get_hls_ctx_enabled("test.com"));
    }

    // Test default value when vhost exists but no hls section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{play{mw_latency 350;}}"));

        // Should return default value when no hls section
        EXPECT_TRUE(conf.get_hls_ctx_enabled("test.com"));
    }

    // Test default value when hls section exists but no hls_ctx config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{enabled on;}}"));

        // Should return default value when no hls_ctx config
        EXPECT_TRUE(conf.get_hls_ctx_enabled("test.com"));
    }

    // Test default value when hls_ctx has empty argument
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_ctx;}}"));

        // Should return default value when empty argument
        EXPECT_TRUE(conf.get_hls_ctx_enabled("test.com"));
    }

    // Test explicit hls_ctx enabled
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_ctx on;}}"));

        EXPECT_TRUE(conf.get_hls_ctx_enabled("test.com"));
    }

    // Test explicit hls_ctx disabled
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_ctx off;}}"));

        EXPECT_FALSE(conf.get_hls_ctx_enabled("test.com"));
    }

    // Test various boolean values (SRS_CONF_PREFER_TRUE: only "off" is false)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_ctx true;}}"));
        EXPECT_TRUE(conf.get_hls_ctx_enabled("test.com")); // "true" != "off", so it's true

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_ctx false;}}"));
        EXPECT_TRUE(conf.get_hls_ctx_enabled("test.com")); // "false" != "off", so it's true

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_ctx yes;}}"));
        EXPECT_TRUE(conf.get_hls_ctx_enabled("test.com")); // "yes" != "off", so it's true

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_ctx no;}}"));
        EXPECT_TRUE(conf.get_hls_ctx_enabled("test.com")); // "no" != "off", so it's true
    }

    // Test environment variable overrides config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_ctx on;}}"));

        SrsSetEnvConfig(conf, hls_ctx, "SRS_VHOST_HLS_HLS_CTX", "off");

        // Environment should override config value
        EXPECT_FALSE(conf.get_hls_ctx_enabled("test.com"));
    }
}

VOID TEST(ConfigHlsTest, CheckHlsTsCtxEnabled)
{
    srs_error_t err;

    // Test default value when no configuration is provided
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        // Default should be true (SRS_CONF_PREFER_TRUE)
        EXPECT_TRUE(conf.get_hls_ts_ctx_enabled("__defaultVhost__"));
        EXPECT_TRUE(conf.get_hls_ts_ctx_enabled("test.com"));
    }

    // Test default value when vhost exists but no hls section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{play{mw_latency 350;}}"));

        // Should return default value when no hls section
        EXPECT_TRUE(conf.get_hls_ts_ctx_enabled("test.com"));
    }

    // Test default value when hls section exists but no hls_ts_ctx config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{enabled on;}}"));

        // Should return default value when no hls_ts_ctx config
        EXPECT_TRUE(conf.get_hls_ts_ctx_enabled("test.com"));
    }

    // Test default value when hls_ts_ctx has empty argument
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_ts_ctx;}}"));

        // Should return default value when empty argument
        EXPECT_TRUE(conf.get_hls_ts_ctx_enabled("test.com"));
    }

    // Test explicit hls_ts_ctx enabled
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_ts_ctx on;}}"));

        EXPECT_TRUE(conf.get_hls_ts_ctx_enabled("test.com"));
    }

    // Test explicit hls_ts_ctx disabled
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_ts_ctx off;}}"));

        EXPECT_FALSE(conf.get_hls_ts_ctx_enabled("test.com"));
    }

    // Test various boolean values (SRS_CONF_PREFER_TRUE: only "off" is false)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_ts_ctx true;}}"));
        EXPECT_TRUE(conf.get_hls_ts_ctx_enabled("test.com")); // "true" != "off", so it's true

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_ts_ctx false;}}"));
        EXPECT_TRUE(conf.get_hls_ts_ctx_enabled("test.com")); // "false" != "off", so it's true

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_ts_ctx yes;}}"));
        EXPECT_TRUE(conf.get_hls_ts_ctx_enabled("test.com")); // "yes" != "off", so it's true

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_ts_ctx no;}}"));
        EXPECT_TRUE(conf.get_hls_ts_ctx_enabled("test.com")); // "no" != "off", so it's true
    }

    // Test environment variable overrides config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_ts_ctx on;}}"));

        SrsSetEnvConfig(conf, hls_ts_ctx, "SRS_VHOST_HLS_HLS_TS_CTX", "off");

        // Environment should override config value
        EXPECT_FALSE(conf.get_hls_ts_ctx_enabled("test.com"));
    }
}

VOID TEST(ConfigHlsTest, CheckHlsCleanup)
{
    srs_error_t err;

    // Test default value when no configuration is provided
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        // Default should be true (SRS_CONF_PREFER_TRUE)
        EXPECT_TRUE(conf.get_hls_cleanup("__defaultVhost__"));
        EXPECT_TRUE(conf.get_hls_cleanup("test.com"));
    }

    // Test default value when vhost exists but no hls section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{play{mw_latency 350;}}"));

        // Should return default value when no hls section
        EXPECT_TRUE(conf.get_hls_cleanup("test.com"));
    }

    // Test default value when hls section exists but no hls_cleanup config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{enabled on;}}"));

        // Should return default value when no hls_cleanup config
        EXPECT_TRUE(conf.get_hls_cleanup("test.com"));
    }

    // Test default value when hls_cleanup has empty argument
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_cleanup;}}"));

        // Should return default value when empty argument
        EXPECT_TRUE(conf.get_hls_cleanup("test.com"));
    }

    // Test explicit hls_cleanup enabled
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_cleanup on;}}"));

        EXPECT_TRUE(conf.get_hls_cleanup("test.com"));
    }

    // Test explicit hls_cleanup disabled
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_cleanup off;}}"));

        EXPECT_FALSE(conf.get_hls_cleanup("test.com"));
    }

    // Test various boolean values (SRS_CONF_PREFER_TRUE: only "off" is false)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_cleanup true;}}"));
        EXPECT_TRUE(conf.get_hls_cleanup("test.com")); // "true" != "off", so it's true

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_cleanup false;}}"));
        EXPECT_TRUE(conf.get_hls_cleanup("test.com")); // "false" != "off", so it's true

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_cleanup yes;}}"));
        EXPECT_TRUE(conf.get_hls_cleanup("test.com")); // "yes" != "off", so it's true

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_cleanup no;}}"));
        EXPECT_TRUE(conf.get_hls_cleanup("test.com")); // "no" != "off", so it's true
    }

    // Test environment variable overrides config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_cleanup on;}}"));

        SrsSetEnvConfig(conf, hls_cleanup, "SRS_VHOST_HLS_HLS_CLEANUP", "off");

        // Environment should override config value
        EXPECT_FALSE(conf.get_hls_cleanup("test.com"));
    }
}

VOID TEST(ConfigHlsTest, CheckHlsDispose)
{
    srs_error_t err;

    // Test default value when no configuration is provided
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        // Default should be 120 seconds
        EXPECT_EQ(120 * SRS_UTIME_SECONDS, conf.get_hls_dispose("__defaultVhost__"));
        EXPECT_EQ(120 * SRS_UTIME_SECONDS, conf.get_hls_dispose("test.com"));
    }

    // Test default value when vhost exists but no hls section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{play{mw_latency 350;}}"));

        // Should return default value when no hls section
        EXPECT_EQ(120 * SRS_UTIME_SECONDS, conf.get_hls_dispose("test.com"));
    }

    // Test default value when hls section exists but no hls_dispose config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{enabled on;}}"));

        // Should return default value when no hls_dispose config
        EXPECT_EQ(120 * SRS_UTIME_SECONDS, conf.get_hls_dispose("test.com"));
    }

    // Test default value when hls_dispose has empty argument
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_dispose;}}"));

        // Should return default value when empty argument
        EXPECT_EQ(120 * SRS_UTIME_SECONDS, conf.get_hls_dispose("test.com"));
    }

    // Test valid hls_dispose values
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_dispose 60;}}"));
        EXPECT_EQ(60 * SRS_UTIME_SECONDS, conf.get_hls_dispose("test.com"));

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_dispose 300;}}"));
        EXPECT_EQ(300 * SRS_UTIME_SECONDS, conf.get_hls_dispose("test.com"));

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_dispose 0;}}"));
        EXPECT_EQ(0, conf.get_hls_dispose("test.com"));

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_dispose 1;}}"));
        EXPECT_EQ(1 * SRS_UTIME_SECONDS, conf.get_hls_dispose("test.com"));
    }

    // Test environment variable overrides config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_dispose 60;}}"));

        SrsSetEnvConfig(conf, hls_dispose, "SRS_VHOST_HLS_HLS_DISPOSE", "180");

        // Environment should override config value
        EXPECT_EQ(180 * SRS_UTIME_SECONDS, conf.get_hls_dispose("test.com"));
    }
}

VOID TEST(ConfigHlsTest, CheckHlsWaitKeyframe)
{
    srs_error_t err;

    // Test default value when no configuration is provided
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        // Default should be true (SRS_CONF_PREFER_TRUE)
        EXPECT_TRUE(conf.get_hls_wait_keyframe("__defaultVhost__"));
        EXPECT_TRUE(conf.get_hls_wait_keyframe("test.com"));
    }

    // Test default value when vhost exists but no hls section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{play{mw_latency 350;}}"));

        // Should return default value when no hls section
        EXPECT_TRUE(conf.get_hls_wait_keyframe("test.com"));
    }

    // Test default value when hls section exists but no hls_wait_keyframe config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{enabled on;}}"));

        // Should return default value when no hls_wait_keyframe config
        EXPECT_TRUE(conf.get_hls_wait_keyframe("test.com"));
    }

    // Test default value when hls_wait_keyframe has empty argument
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_wait_keyframe;}}"));

        // Should return default value when empty argument
        EXPECT_TRUE(conf.get_hls_wait_keyframe("test.com"));
    }

    // Test explicit hls_wait_keyframe enabled
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_wait_keyframe on;}}"));

        EXPECT_TRUE(conf.get_hls_wait_keyframe("test.com"));
    }

    // Test explicit hls_wait_keyframe disabled
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_wait_keyframe off;}}"));

        EXPECT_FALSE(conf.get_hls_wait_keyframe("test.com"));
    }

    // Test various boolean values (SRS_CONF_PREFER_TRUE: only "off" is false)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_wait_keyframe true;}}"));
        EXPECT_TRUE(conf.get_hls_wait_keyframe("test.com")); // "true" != "off", so it's true

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_wait_keyframe false;}}"));
        EXPECT_TRUE(conf.get_hls_wait_keyframe("test.com")); // "false" != "off", so it's true

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_wait_keyframe yes;}}"));
        EXPECT_TRUE(conf.get_hls_wait_keyframe("test.com")); // "yes" != "off", so it's true

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_wait_keyframe no;}}"));
        EXPECT_TRUE(conf.get_hls_wait_keyframe("test.com")); // "no" != "off", so it's true
    }

    // Test environment variable overrides config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_wait_keyframe on;}}"));

        SrsSetEnvConfig(conf, hls_wait_keyframe, "SRS_VHOST_HLS_HLS_WAIT_KEYFRAME", "off");

        // Environment should override config value
        EXPECT_FALSE(conf.get_hls_wait_keyframe("test.com"));
    }
}

VOID TEST(ConfigHlsTest, CheckHlsKeys)
{
    srs_error_t err;

    // Test default value when no configuration is provided
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        // Default should be false (SRS_CONF_PREFER_TRUE with DEFAULT = false)
        EXPECT_FALSE(conf.get_hls_keys("__defaultVhost__"));
        EXPECT_FALSE(conf.get_hls_keys("test.com"));
    }

    // Test default value when vhost exists but no hls section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{play{mw_latency 350;}}"));

        // Should return default value when no hls section
        EXPECT_FALSE(conf.get_hls_keys("test.com"));
    }

    // Test default value when hls section exists but no hls_keys config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{enabled on;}}"));

        // Should return default value when no hls_keys config
        EXPECT_FALSE(conf.get_hls_keys("test.com"));
    }

    // Test default value when hls_keys has empty argument
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_keys;}}"));

        // Should return default value when empty argument
        EXPECT_FALSE(conf.get_hls_keys("test.com"));
    }

    // Test explicit hls_keys enabled
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_keys on;}}"));

        EXPECT_TRUE(conf.get_hls_keys("test.com"));
    }

    // Test explicit hls_keys disabled
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_keys off;}}"));

        EXPECT_FALSE(conf.get_hls_keys("test.com"));
    }

    // Test various boolean values (SRS_CONF_PREFER_TRUE: only "off" is false)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_keys true;}}"));
        EXPECT_TRUE(conf.get_hls_keys("test.com")); // "true" != "off", so it's true

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_keys false;}}"));
        EXPECT_TRUE(conf.get_hls_keys("test.com")); // "false" != "off", so it's true

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_keys yes;}}"));
        EXPECT_TRUE(conf.get_hls_keys("test.com")); // "yes" != "off", so it's true

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_keys no;}}"));
        EXPECT_TRUE(conf.get_hls_keys("test.com")); // "no" != "off", so it's true
    }

    // Test environment variable overrides config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_keys off;}}"));

        SrsSetEnvConfig(conf, hls_keys, "SRS_VHOST_HLS_HLS_KEYS", "on");

        // Environment should override config value
        EXPECT_TRUE(conf.get_hls_keys("test.com"));
    }
}

VOID TEST(ConfigHlsTest, CheckHlsFragmentsPerKey)
{
    srs_error_t err;

    // Test default value when no configuration is provided
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        // Default should be 5
        EXPECT_EQ(5, conf.get_hls_fragments_per_key("__defaultVhost__"));
        EXPECT_EQ(5, conf.get_hls_fragments_per_key("test.com"));
    }

    // Test default value when vhost exists but no hls section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{play{mw_latency 350;}}"));

        // Should return default value when no hls section
        EXPECT_EQ(5, conf.get_hls_fragments_per_key("test.com"));
    }

    // Test default value when hls section exists but no hls_fragments_per_key config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{enabled on;}}"));

        // Should return default value when no hls_fragments_per_key config
        EXPECT_EQ(5, conf.get_hls_fragments_per_key("test.com"));
    }

    // Test default value when hls_fragments_per_key has empty argument
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_fragments_per_key;}}"));

        // Should return default value when empty argument
        EXPECT_EQ(5, conf.get_hls_fragments_per_key("test.com"));
    }

    // Test valid hls_fragments_per_key values
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_fragments_per_key 3;}}"));
        EXPECT_EQ(3, conf.get_hls_fragments_per_key("test.com"));

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_fragments_per_key 10;}}"));
        EXPECT_EQ(10, conf.get_hls_fragments_per_key("test.com"));

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_fragments_per_key 1;}}"));
        EXPECT_EQ(1, conf.get_hls_fragments_per_key("test.com"));

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_fragments_per_key 0;}}"));
        EXPECT_EQ(0, conf.get_hls_fragments_per_key("test.com"));
    }

    // Test environment variable overrides config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_fragments_per_key 3;}}"));

        SrsSetEnvConfig(conf, hls_fragments_per_key, "SRS_VHOST_HLS_HLS_FRAGMENTS_PER_KEY", "8");

        // Environment should override config value
        EXPECT_EQ(8, conf.get_hls_fragments_per_key("test.com"));
    }
}

VOID TEST(ConfigHlsTest, CheckHlsKeyFile)
{
    srs_error_t err;

    // Test default value when no configuration is provided
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        // Default should be "[app]/[stream]-[seq].key"
        EXPECT_STREQ("[app]/[stream]-[seq].key", conf.get_hls_key_file("__defaultVhost__").c_str());
        EXPECT_STREQ("[app]/[stream]-[seq].key", conf.get_hls_key_file("test.com").c_str());
    }

    // Test default value when vhost exists but no hls section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{play{mw_latency 350;}}"));

        // Should return default value when no hls section
        EXPECT_STREQ("[app]/[stream]-[seq].key", conf.get_hls_key_file("test.com").c_str());
    }

    // Test default value when hls section exists but no hls_key_file config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{enabled on;}}"));

        // Should return default value when no hls_key_file config
        EXPECT_STREQ("[app]/[stream]-[seq].key", conf.get_hls_key_file("test.com").c_str());
    }

    // Test default value when hls_key_file has empty argument
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_key_file;}}"));

        // Should return default value when empty argument
        EXPECT_STREQ("[app]/[stream]-[seq].key", conf.get_hls_key_file("test.com").c_str());
    }

    // Test custom hls_key_file values
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_key_file custom/[stream]-[seq].key;}}"));
        EXPECT_STREQ("custom/[stream]-[seq].key", conf.get_hls_key_file("test.com").c_str());

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_key_file [app]/key-[seq].key;}}"));
        EXPECT_STREQ("[app]/key-[seq].key", conf.get_hls_key_file("test.com").c_str());

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_key_file /path/to/[stream]/[seq].key;}}"));
        EXPECT_STREQ("/path/to/[stream]/[seq].key", conf.get_hls_key_file("test.com").c_str());
    }

    // Test environment variable overrides config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_key_file custom.key;}}"));

        SrsSetEnvConfig(conf, hls_key_file, "SRS_VHOST_HLS_HLS_KEY_FILE", "env/[stream]-[seq].key");

        // Environment should override config value
        EXPECT_STREQ("env/[stream]-[seq].key", conf.get_hls_key_file("test.com").c_str());
    }
}

VOID TEST(ConfigHlsTest, CheckHlsKeyFilePath)
{
    srs_error_t err;

    // Test default value when no configuration is provided
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        // Default should be the same as get_hls_path() which is "./objs/nginx/html"
        EXPECT_STREQ("./objs/nginx/html", conf.get_hls_key_file_path("__defaultVhost__").c_str());
        EXPECT_STREQ("./objs/nginx/html", conf.get_hls_key_file_path("test.com").c_str());
    }

    // Test default value when vhost exists but no hls section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{play{mw_latency 350;}}"));

        // Should return default value when no hls section
        EXPECT_STREQ("./objs/nginx/html", conf.get_hls_key_file_path("test.com").c_str());
    }

    // Test default value when hls section exists but no hls_key_file_path config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{enabled on;}}"));

        // Should return default value when no hls_key_file_path config
        EXPECT_STREQ("./objs/nginx/html", conf.get_hls_key_file_path("test.com").c_str());
    }

    // Test default value when hls_key_file_path has empty argument
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_key_file_path;}}"));

        // Should return default value when empty argument
        EXPECT_STREQ("./objs/nginx/html", conf.get_hls_key_file_path("test.com").c_str());
    }

    // Test custom hls_key_file_path values
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_key_file_path /custom/path;}}"));
        EXPECT_STREQ("/custom/path", conf.get_hls_key_file_path("test.com").c_str());

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_key_file_path ./keys;}}"));
        EXPECT_STREQ("./keys", conf.get_hls_key_file_path("test.com").c_str());

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_key_file_path /var/www/keys;}}"));
        EXPECT_STREQ("/var/www/keys", conf.get_hls_key_file_path("test.com").c_str());
    }

    // Test environment variable overrides config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_key_file_path /custom/path;}}"));

        SrsSetEnvConfig(conf, hls_key_file_path, "SRS_VHOST_HLS_HLS_KEY_FILE_PATH", "/env/path");

        // Environment should override config value
        EXPECT_STREQ("/env/path", conf.get_hls_key_file_path("test.com").c_str());
    }
}

VOID TEST(ConfigHlsTest, CheckHlsKeyUrl)
{
    srs_error_t err;

    // Test default value when no configuration is provided
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        // Default should be the same as get_hls_path() which is "./objs/nginx/html"
        EXPECT_STREQ("./objs/nginx/html", conf.get_hls_key_url("__defaultVhost__").c_str());
        EXPECT_STREQ("./objs/nginx/html", conf.get_hls_key_url("test.com").c_str());
    }

    // Test default value when vhost exists but no hls section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{play{mw_latency 350;}}"));

        // Should return default value when no hls section
        EXPECT_STREQ("./objs/nginx/html", conf.get_hls_key_url("test.com").c_str());
    }

    // Test default value when hls section exists but no hls_key_url config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{enabled on;}}"));

        // Should return default value when no hls_key_url config
        EXPECT_STREQ("./objs/nginx/html", conf.get_hls_key_url("test.com").c_str());
    }

    // Test default value when hls_key_url has empty argument
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_key_url;}}"));

        // Should return default value when empty argument
        EXPECT_STREQ("./objs/nginx/html", conf.get_hls_key_url("test.com").c_str());
    }

    // Test custom hls_key_url values
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_key_url http://example.com/keys;}}"));
        EXPECT_STREQ("http://example.com/keys", conf.get_hls_key_url("test.com").c_str());

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_key_url https://cdn.example.com/hls/keys;}}"));
        EXPECT_STREQ("https://cdn.example.com/hls/keys", conf.get_hls_key_url("test.com").c_str());

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_key_url /relative/path/keys;}}"));
        EXPECT_STREQ("/relative/path/keys", conf.get_hls_key_url("test.com").c_str());
    }

    // Test environment variable overrides config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_key_url http://example.com/keys;}}"));

        SrsSetEnvConfig(conf, hls_key_url, "SRS_VHOST_HLS_HLS_KEY_URL", "https://env.example.com/keys");

        // Environment should override config value
        EXPECT_STREQ("https://env.example.com/keys", conf.get_hls_key_url("test.com").c_str());
    }
}

VOID TEST(ConfigHlsTest, CheckHlsRecover)
{
    srs_error_t err;

    // Test default value when no configuration is provided
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        // Default should be true (SRS_CONF_PREFER_TRUE)
        EXPECT_TRUE(conf.get_hls_recover("__defaultVhost__"));
        EXPECT_TRUE(conf.get_hls_recover("test.com"));
    }

    // Test default value when vhost exists but no hls section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{play{mw_latency 350;}}"));

        // Should return default value when no hls section
        EXPECT_TRUE(conf.get_hls_recover("test.com"));
    }

    // Test default value when hls section exists but no hls_recover config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{enabled on;}}"));

        // Should return default value when no hls_recover config
        EXPECT_TRUE(conf.get_hls_recover("test.com"));
    }

    // Test default value when hls_recover has empty argument
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_recover;}}"));

        // Should return default value when empty argument
        EXPECT_TRUE(conf.get_hls_recover("test.com"));
    }

    // Test explicit hls_recover enabled
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_recover on;}}"));

        EXPECT_TRUE(conf.get_hls_recover("test.com"));
    }

    // Test explicit hls_recover disabled
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_recover off;}}"));

        EXPECT_FALSE(conf.get_hls_recover("test.com"));
    }

    // Test various boolean values (SRS_CONF_PREFER_TRUE: only "off" is false)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_recover true;}}"));
        EXPECT_TRUE(conf.get_hls_recover("test.com")); // "true" != "off", so it's true

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_recover false;}}"));
        EXPECT_TRUE(conf.get_hls_recover("test.com")); // "false" != "off", so it's true

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_recover yes;}}"));
        EXPECT_TRUE(conf.get_hls_recover("test.com")); // "yes" != "off", so it's true

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_recover no;}}"));
        EXPECT_TRUE(conf.get_hls_recover("test.com")); // "no" != "off", so it's true
    }

    // Test environment variable overrides config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_recover on;}}"));

        SrsSetEnvConfig(conf, hls_recover, "SRS_VHOST_HLS_HLS_RECOVER", "off");

        // Environment should override config value
        EXPECT_FALSE(conf.get_hls_recover("test.com"));
    }
}

VOID TEST(ConfigVhostTest, CheckQueueLength)
{
    srs_error_t err;

    // Test default value when no configuration is provided
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        // Default should be 30 seconds
        EXPECT_EQ(30 * SRS_UTIME_SECONDS, conf.get_queue_length("__defaultVhost__"));
        EXPECT_EQ(30 * SRS_UTIME_SECONDS, conf.get_queue_length("test.com"));
    }

    // Test default value when vhost exists but no play section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{enabled on;}}"));

        // Should return default value when no play section
        EXPECT_EQ(30 * SRS_UTIME_SECONDS, conf.get_queue_length("test.com"));
    }

    // Test default value when play exists but no queue_length config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{play{mw_latency 350;}}"));

        // Should return default value when no queue_length config
        EXPECT_EQ(30 * SRS_UTIME_SECONDS, conf.get_queue_length("test.com"));
    }

    // Test default value when queue_length has empty argument
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{play{queue_length;}}"));

        // Should return default value when empty argument
        EXPECT_EQ(30 * SRS_UTIME_SECONDS, conf.get_queue_length("test.com"));
    }

    // Test valid queue_length values
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{play{queue_length 10;}}"));
        EXPECT_EQ(10 * SRS_UTIME_SECONDS, conf.get_queue_length("test.com"));

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{play{queue_length 60;}}"));
        EXPECT_EQ(60 * SRS_UTIME_SECONDS, conf.get_queue_length("test.com"));

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{play{queue_length 0;}}"));
        EXPECT_EQ(0 * SRS_UTIME_SECONDS, conf.get_queue_length("test.com"));
    }

    // Test negative values (should be parsed as is)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{play{queue_length -5;}}"));
        EXPECT_EQ(-5 * SRS_UTIME_SECONDS, conf.get_queue_length("test.com"));
    }
}

VOID TEST(ConfigVhostTest, CheckReferEnabled)
{
    srs_error_t err;

    // Test default value when no configuration is provided
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        // Default should be false (SRS_CONF_PREFER_FALSE)
        EXPECT_FALSE(conf.get_refer_enabled("__defaultVhost__"));
        EXPECT_FALSE(conf.get_refer_enabled("test.com"));
    }

    // Test default value when vhost exists but no refer config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{enabled on;}}"));

        // Should return default value when no refer config
        EXPECT_FALSE(conf.get_refer_enabled("test.com"));
    }

    // Test default value when refer section exists but no enabled config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{refer{all play.ossrs.net;}}"));

        // Should return default value when no enabled config
        EXPECT_FALSE(conf.get_refer_enabled("test.com"));
    }

    // Test default value when refer enabled has empty argument
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{refer{enabled;}}"));

        // Should return default value when empty argument
        EXPECT_FALSE(conf.get_refer_enabled("test.com"));
    }

    // Test explicit refer enabled
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{refer{enabled on;}}"));

        EXPECT_TRUE(conf.get_refer_enabled("test.com"));
    }

    // Test explicit refer disabled
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{refer{enabled off;}}"));

        EXPECT_FALSE(conf.get_refer_enabled("test.com"));
    }

    // Test various boolean values (SRS_CONF_PREFER_FALSE: only "on" is true)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{refer{enabled true;}}"));
        EXPECT_FALSE(conf.get_refer_enabled("test.com")); // "true" != "on", so it's false

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{refer{enabled false;}}"));
        EXPECT_FALSE(conf.get_refer_enabled("test.com")); // "false" != "on", so it's false

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{refer{enabled yes;}}"));
        EXPECT_FALSE(conf.get_refer_enabled("test.com")); // "yes" != "on", so it's false

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{refer{enabled no;}}"));
        EXPECT_FALSE(conf.get_refer_enabled("test.com")); // "no" != "on", so it's false
    }
}

VOID TEST(ConfigVhostTest, CheckChunkSize)
{
    srs_error_t err;

    // Test default value when no vhost specified (empty vhost)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse("rtmp{listen 1935; chunk_size 4096;}"));

        // Should return global chunk size for empty vhost
        EXPECT_EQ(4096, conf.get_chunk_size(""));
    }

    // Test vhost that doesn't exist (should use global)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse("rtmp{listen 1935; chunk_size 8192;}"));

        // Should return global chunk size when vhost doesn't exist
        EXPECT_EQ(8192, conf.get_chunk_size("nonexistent.com"));
    }

    // Test vhost exists but no chunk_size config (should use global)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse("rtmp{listen 1935; chunk_size 16384;} vhost test.com{hls{enabled on;}}"));

        // Should return global chunk size when vhost has no chunk_size
        EXPECT_EQ(16384, conf.get_chunk_size("test.com"));
    }

    // Test vhost with specific chunk_size
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse("rtmp{listen 1935; chunk_size 4096;} vhost test.com{chunk_size 8192;}"));

        // Should return vhost-specific chunk size
        EXPECT_EQ(8192, conf.get_chunk_size("test.com"));
    }

    // Test multiple vhosts with different chunk sizes
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse("rtmp{listen 1935; chunk_size 4096;} vhost test1.com{chunk_size 8192;} vhost test2.com{chunk_size 16384;}"));

        EXPECT_EQ(8192, conf.get_chunk_size("test1.com"));
        EXPECT_EQ(16384, conf.get_chunk_size("test2.com"));
        EXPECT_EQ(4096, conf.get_chunk_size("other.com")); // Should use global for non-configured vhost
    }
}

VOID TEST(ConfigVhostTest, CheckMwMsgs)
{
    srs_error_t err;

    // Test default values for different combinations of is_realtime and is_rtc
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        // Default should be SRS_PERF_MW_MIN_MSGS for normal case
        EXPECT_EQ(SRS_PERF_MW_MIN_MSGS, conf.get_mw_msgs("__defaultVhost__", false, false));
        EXPECT_EQ(SRS_PERF_MW_MIN_MSGS, conf.get_mw_msgs("test.com", false, false));

        // Default should be SRS_PERF_MW_MIN_MSGS_REALTIME for realtime case
        EXPECT_EQ(SRS_PERF_MW_MIN_MSGS_REALTIME, conf.get_mw_msgs("__defaultVhost__", true, false));
        EXPECT_EQ(SRS_PERF_MW_MIN_MSGS_REALTIME, conf.get_mw_msgs("test.com", true, false));

        // Default should be SRS_PERF_MW_MIN_MSGS_FOR_RTC for RTC case (but realtime overrides RTC)
        EXPECT_EQ(SRS_PERF_MW_MIN_MSGS_FOR_RTC, conf.get_mw_msgs("__defaultVhost__", false, true));
        EXPECT_EQ(SRS_PERF_MW_MIN_MSGS_FOR_RTC, conf.get_mw_msgs("test.com", false, true));
        EXPECT_EQ(SRS_PERF_MW_MIN_MSGS_REALTIME, conf.get_mw_msgs("__defaultVhost__", true, true)); // realtime overrides RTC
        EXPECT_EQ(SRS_PERF_MW_MIN_MSGS_REALTIME, conf.get_mw_msgs("test.com", true, true));         // realtime overrides RTC
    }

    // Test default value when vhost exists but no play section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{enabled on;}}"));

        // Should return default values when no play section
        EXPECT_EQ(SRS_PERF_MW_MIN_MSGS, conf.get_mw_msgs("test.com", false, false));
        EXPECT_EQ(SRS_PERF_MW_MIN_MSGS_REALTIME, conf.get_mw_msgs("test.com", true, false));
        EXPECT_EQ(SRS_PERF_MW_MIN_MSGS_FOR_RTC, conf.get_mw_msgs("test.com", false, true));
        EXPECT_EQ(SRS_PERF_MW_MIN_MSGS_REALTIME, conf.get_mw_msgs("test.com", true, true)); // realtime overrides RTC
    }

    // Test default value when play section exists but no mw_msgs config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{play{mw_latency 350;}}"));

        // Should return default values when no mw_msgs config
        EXPECT_EQ(SRS_PERF_MW_MIN_MSGS, conf.get_mw_msgs("test.com", false, false));
        EXPECT_EQ(SRS_PERF_MW_MIN_MSGS_REALTIME, conf.get_mw_msgs("test.com", true, false));
        EXPECT_EQ(SRS_PERF_MW_MIN_MSGS_FOR_RTC, conf.get_mw_msgs("test.com", false, true));
        EXPECT_EQ(SRS_PERF_MW_MIN_MSGS_REALTIME, conf.get_mw_msgs("test.com", true, true)); // realtime overrides RTC
    }

    // Test default value when mw_msgs has empty argument
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{play{mw_msgs;}}"));

        // Should return default values when empty argument
        EXPECT_EQ(SRS_PERF_MW_MIN_MSGS, conf.get_mw_msgs("test.com", false, false));
        EXPECT_EQ(SRS_PERF_MW_MIN_MSGS_REALTIME, conf.get_mw_msgs("test.com", true, false));
        EXPECT_EQ(SRS_PERF_MW_MIN_MSGS_FOR_RTC, conf.get_mw_msgs("test.com", false, true));
        EXPECT_EQ(SRS_PERF_MW_MIN_MSGS_REALTIME, conf.get_mw_msgs("test.com", true, true)); // realtime overrides RTC
    }

    // Test valid mw_msgs values
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{play{mw_msgs 64;}}"));

        // Should return configured value for all combinations
        EXPECT_EQ(64, conf.get_mw_msgs("test.com", false, false));
        EXPECT_EQ(64, conf.get_mw_msgs("test.com", true, false));
        EXPECT_EQ(64, conf.get_mw_msgs("test.com", false, true));
        EXPECT_EQ(64, conf.get_mw_msgs("test.com", true, true));
    }

    // Test mw_msgs value exceeding maximum (should be clamped to SRS_PERF_MW_MSGS=128)
    if (true) {
        MockSrsConfig conf;
        int large_value = 256; // Larger than SRS_PERF_MW_MSGS (128)
        string config_str = _MIN_OK_CONF "vhost test.com{play{mw_msgs " + srs_strconv_format_int(large_value) + ";}}";
        HELPER_ASSERT_SUCCESS(conf.mock_parse(config_str));

        // Should be clamped to maximum value (128)
        EXPECT_EQ(128, conf.get_mw_msgs("test.com", false, false));
        EXPECT_EQ(128, conf.get_mw_msgs("test.com", true, false));
        EXPECT_EQ(128, conf.get_mw_msgs("test.com", false, true));
        EXPECT_EQ(128, conf.get_mw_msgs("test.com", true, true));
    }

    // Test various valid mw_msgs values
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{play{mw_msgs 128;}}"));
        EXPECT_EQ(128, conf.get_mw_msgs("test.com", false, false));

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{play{mw_msgs 64;}}"));
        EXPECT_EQ(64, conf.get_mw_msgs("test.com", false, false));

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{play{mw_msgs 1;}}"));
        EXPECT_EQ(1, conf.get_mw_msgs("test.com", false, false));

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{play{mw_msgs 0;}}"));
        EXPECT_EQ(0, conf.get_mw_msgs("test.com", false, false));
    }
}

VOID TEST(ConfigPublishTest, CheckPublishKickoffForIdle)
{
    srs_error_t err;

    // Test default value when no configuration is provided
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        // Default should be 0 (no kickoff)
        EXPECT_EQ(0, conf.get_publish_kickoff_for_idle("__defaultVhost__"));
        EXPECT_EQ(0, conf.get_publish_kickoff_for_idle("test.com"));
    }

    // Test default value when vhost exists but no publish section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{enabled on;}}"));

        // Should return default value when no publish section
        EXPECT_EQ(0, conf.get_publish_kickoff_for_idle("test.com"));
    }

    // Test default value when publish exists but no kickoff_for_idle config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{publish{mr_latency 350;}}"));

        // Should return default value when no kickoff_for_idle config
        EXPECT_EQ(0, conf.get_publish_kickoff_for_idle("test.com"));
    }

    // Test default value when kickoff_for_idle has empty argument
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{publish{kickoff_for_idle;}}"));

        // Should return default value when empty argument
        EXPECT_EQ(0, conf.get_publish_kickoff_for_idle("test.com"));
    }

    // Test valid kickoff_for_idle values
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{publish{kickoff_for_idle 30;}}"));
        EXPECT_EQ(30 * SRS_UTIME_SECONDS, conf.get_publish_kickoff_for_idle("test.com"));

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{publish{kickoff_for_idle 60.5;}}"));
        EXPECT_EQ((srs_utime_t)(60.5 * SRS_UTIME_SECONDS), conf.get_publish_kickoff_for_idle("test.com"));

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{publish{kickoff_for_idle 0;}}"));
        EXPECT_EQ(0, conf.get_publish_kickoff_for_idle("test.com"));
    }

    // Test negative values (should be parsed as is)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{publish{kickoff_for_idle -10;}}"));
        EXPECT_EQ((srs_utime_t)(-10 * SRS_UTIME_SECONDS), conf.get_publish_kickoff_for_idle("test.com"));
    }

    // Test multiple vhosts with different configurations
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF
                                              "vhost vhost1.com{publish{kickoff_for_idle 30;}} "
                                              "vhost vhost2.com{publish{kickoff_for_idle 60;}} "
                                              "vhost vhost3.com{publish{mr_latency 350;}}"));

        EXPECT_EQ(30 * SRS_UTIME_SECONDS, conf.get_publish_kickoff_for_idle("vhost1.com"));
        EXPECT_EQ(60 * SRS_UTIME_SECONDS, conf.get_publish_kickoff_for_idle("vhost2.com"));
        EXPECT_EQ(0, conf.get_publish_kickoff_for_idle("vhost3.com")); // default
    }
}

VOID TEST(ConfigClusterTest, CheckVhostEdgeProtocol)
{
    srs_error_t err;

    // Test default value when no configuration is provided
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        // Default should be "rtmp"
        EXPECT_STREQ("rtmp", conf.get_vhost_edge_protocol("__defaultVhost__").c_str());
        EXPECT_STREQ("rtmp", conf.get_vhost_edge_protocol("test.com").c_str());
    }

    // Test default value when vhost exists but no cluster section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{enabled on;}}"));

        // Should return default value when no cluster section
        EXPECT_STREQ("rtmp", conf.get_vhost_edge_protocol("test.com").c_str());
    }

    // Test default value when cluster exists but no protocol config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{cluster{mode edge;}}"));

        // Should return default value when no protocol config
        EXPECT_STREQ("rtmp", conf.get_vhost_edge_protocol("test.com").c_str());
    }

    // Test valid protocol values
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{cluster{protocol rtmp;}}"));
        EXPECT_STREQ("rtmp", conf.get_vhost_edge_protocol("test.com").c_str());

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{cluster{protocol srt;}}"));
        EXPECT_STREQ("srt", conf.get_vhost_edge_protocol("test.com").c_str());

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{cluster{protocol webrtc;}}"));
        EXPECT_STREQ("webrtc", conf.get_vhost_edge_protocol("test.com").c_str());
    }

    // Test custom protocol values
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{cluster{protocol custom_protocol;}}"));
        EXPECT_STREQ("custom_protocol", conf.get_vhost_edge_protocol("test.com").c_str());
    }

    // Test multiple vhosts with different protocols
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF
                                              "vhost vhost1.com{cluster{protocol rtmp;}} "
                                              "vhost vhost2.com{cluster{protocol srt;}} "
                                              "vhost vhost3.com{cluster{mode edge;}}"));

        EXPECT_STREQ("rtmp", conf.get_vhost_edge_protocol("vhost1.com").c_str());
        EXPECT_STREQ("srt", conf.get_vhost_edge_protocol("vhost2.com").c_str());
        EXPECT_STREQ("rtmp", conf.get_vhost_edge_protocol("vhost3.com").c_str()); // default
    }
}

VOID TEST(ConfigClusterTest, CheckVhostEdgeFollowClient)
{
    srs_error_t err;

    // Test default value when no configuration is provided
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        // Default should be false
        EXPECT_FALSE(conf.get_vhost_edge_follow_client("__defaultVhost__"));
        EXPECT_FALSE(conf.get_vhost_edge_follow_client("test.com"));
    }

    // Test default value when vhost exists but no cluster section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{enabled on;}}"));

        // Should return default value when no cluster section
        EXPECT_FALSE(conf.get_vhost_edge_follow_client("test.com"));
    }

    // Test default value when cluster exists but no follow_client config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{cluster{mode edge;}}"));

        // Should return default value when no follow_client config
        EXPECT_FALSE(conf.get_vhost_edge_follow_client("test.com"));
    }

    // Test explicit follow_client enabled
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{cluster{follow_client on;}}"));

        EXPECT_TRUE(conf.get_vhost_edge_follow_client("test.com"));
    }

    // Test explicit follow_client disabled
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{cluster{follow_client off;}}"));

        EXPECT_FALSE(conf.get_vhost_edge_follow_client("test.com"));
    }

    // Test various boolean values (SRS_CONF_PREFER_FALSE: only "on" is true)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{cluster{follow_client true;}}"));
        EXPECT_FALSE(conf.get_vhost_edge_follow_client("test.com")); // "true" != "on", so it's false

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{cluster{follow_client false;}}"));
        EXPECT_FALSE(conf.get_vhost_edge_follow_client("test.com")); // "false" != "on", so it's false

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{cluster{follow_client yes;}}"));
        EXPECT_FALSE(conf.get_vhost_edge_follow_client("test.com")); // "yes" != "on", so it's false

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{cluster{follow_client no;}}"));
        EXPECT_FALSE(conf.get_vhost_edge_follow_client("test.com")); // "no" != "on", so it's false
    }
}

VOID TEST(ConfigFFmpegTest, CheckFFLogDir)
{
    srs_error_t err;

    // Test default value when no configuration is provided
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        // Default should be "./objs"
        EXPECT_STREQ("./objs", conf.get_ff_log_dir().c_str());
    }

    // Test valid ff_log_dir values
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "ff_log_dir /tmp/logs;"));
        EXPECT_STREQ("/tmp/logs", conf.get_ff_log_dir().c_str());

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "ff_log_dir ./custom_logs;"));
        EXPECT_STREQ("./custom_logs", conf.get_ff_log_dir().c_str());

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "ff_log_dir /var/log/srs;"));
        EXPECT_STREQ("/var/log/srs", conf.get_ff_log_dir().c_str());
    }

    // Test empty ff_log_dir value (should return default)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "ff_log_dir;"));

        // Should return default value when empty argument
        EXPECT_STREQ("./objs", conf.get_ff_log_dir().c_str());
    }

    // Test ff_log_dir with spaces and special characters
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "ff_log_dir \"/path with spaces\";"));
        EXPECT_STREQ("/path with spaces", conf.get_ff_log_dir().c_str());
    }
}

VOID TEST(ConfigFFmpegTest, CheckFFLogLevel)
{
    srs_error_t err;

    // Test default value when no configuration is provided
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        // Default should be "info"
        EXPECT_STREQ("info", conf.get_ff_log_level().c_str());
    }

    // Test valid ff_log_level values
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "ff_log_level debug;"));
        EXPECT_STREQ("debug", conf.get_ff_log_level().c_str());

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "ff_log_level info;"));
        EXPECT_STREQ("info", conf.get_ff_log_level().c_str());

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "ff_log_level warning;"));
        EXPECT_STREQ("warning", conf.get_ff_log_level().c_str());

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "ff_log_level error;"));
        EXPECT_STREQ("error", conf.get_ff_log_level().c_str());

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "ff_log_level quiet;"));
        EXPECT_STREQ("quiet", conf.get_ff_log_level().c_str());
    }

    // Test empty ff_log_level value (should return default)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "ff_log_level;"));

        // Should return default value when empty argument
        EXPECT_STREQ("info", conf.get_ff_log_level().c_str());
    }

    // Test custom ff_log_level values
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "ff_log_level custom_level;"));
        EXPECT_STREQ("custom_level", conf.get_ff_log_level().c_str());
    }
}

VOID TEST(ConfigDashTest, CheckDashEnabled)
{
    srs_error_t err;

    // Test default value when no configuration is provided
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        // Default should be false
        EXPECT_FALSE(conf.get_dash_enabled("__defaultVhost__"));
        EXPECT_FALSE(conf.get_dash_enabled("test.com"));
    }

    // Test default value when vhost exists but no dash section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{enabled on;}}"));

        // Should return default value when no dash section
        EXPECT_FALSE(conf.get_dash_enabled("test.com"));
    }

    // Test default value when dash exists but no enabled config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dash{dash_fragment 10;}}"));

        // Should return default value when no enabled config
        EXPECT_FALSE(conf.get_dash_enabled("test.com"));
    }

    // Test default value when enabled has empty argument
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dash{enabled;}}"));

        // Should return default value when empty argument
        EXPECT_FALSE(conf.get_dash_enabled("test.com"));
    }

    // Test explicit dash enabled
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dash{enabled on;}}"));

        EXPECT_TRUE(conf.get_dash_enabled("test.com"));
    }

    // Test explicit dash disabled
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dash{enabled off;}}"));

        EXPECT_FALSE(conf.get_dash_enabled("test.com"));
    }

    // Test various boolean values (SRS_CONF_PREFER_FALSE: only "on" is true)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dash{enabled true;}}"));
        EXPECT_FALSE(conf.get_dash_enabled("test.com")); // "true" != "on", so it's false

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dash{enabled false;}}"));
        EXPECT_FALSE(conf.get_dash_enabled("test.com")); // "false" != "on", so it's false

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dash{enabled yes;}}"));
        EXPECT_FALSE(conf.get_dash_enabled("test.com")); // "yes" != "on", so it's false

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dash{enabled no;}}"));
        EXPECT_FALSE(conf.get_dash_enabled("test.com")); // "no" != "on", so it's false
    }

    // Test multiple vhosts with different configurations
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF
                                              "vhost vhost1.com{dash{enabled on;}} "
                                              "vhost vhost2.com{dash{enabled off;}} "
                                              "vhost vhost3.com{dash{dash_fragment 10;}}"));

        EXPECT_TRUE(conf.get_dash_enabled("vhost1.com"));
        EXPECT_FALSE(conf.get_dash_enabled("vhost2.com"));
        EXPECT_FALSE(conf.get_dash_enabled("vhost3.com")); // default
    }
}

VOID TEST(ConfigDashTest, CheckDashFragment)
{
    srs_error_t err;

    // Test default value when no configuration is provided
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        // Default should be 10 seconds
        EXPECT_EQ(10 * SRS_UTIME_SECONDS, conf.get_dash_fragment("__defaultVhost__"));
        EXPECT_EQ(10 * SRS_UTIME_SECONDS, conf.get_dash_fragment("test.com"));
    }

    // Test default value when vhost exists but no dash section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{enabled on;}}"));

        // Should return default value when no dash section
        EXPECT_EQ(10 * SRS_UTIME_SECONDS, conf.get_dash_fragment("test.com"));
    }

    // Test default value when dash exists but no dash_fragment config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dash{enabled on;}}"));

        // Should return default value when no dash_fragment config
        EXPECT_EQ(10 * SRS_UTIME_SECONDS, conf.get_dash_fragment("test.com"));
    }

    // Test default value when dash_fragment has empty argument
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dash{dash_fragment;}}"));

        // Should return default value when empty argument
        EXPECT_EQ(10 * SRS_UTIME_SECONDS, conf.get_dash_fragment("test.com"));
    }

    // Test valid dash_fragment values
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dash{dash_fragment 10;}}"));
        EXPECT_EQ(10 * SRS_UTIME_SECONDS, conf.get_dash_fragment("test.com"));

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dash{dash_fragment 5.5;}}"));
        EXPECT_EQ((srs_utime_t)(5.5 * SRS_UTIME_SECONDS), conf.get_dash_fragment("test.com"));

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dash{dash_fragment 60;}}"));
        EXPECT_EQ(60 * SRS_UTIME_SECONDS, conf.get_dash_fragment("test.com"));
    }

    // Test zero and negative values
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dash{dash_fragment 0;}}"));
        EXPECT_EQ(0, conf.get_dash_fragment("test.com"));

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dash{dash_fragment -5;}}"));
        EXPECT_EQ((srs_utime_t)(-5 * SRS_UTIME_SECONDS), conf.get_dash_fragment("test.com"));
    }
}

VOID TEST(ConfigDashTest, CheckDashUpdatePeriod)
{
    srs_error_t err;

    // Test default value when no configuration is provided
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        // Default should be 5 seconds
        EXPECT_EQ(5 * SRS_UTIME_SECONDS, conf.get_dash_update_period("__defaultVhost__"));
        EXPECT_EQ(5 * SRS_UTIME_SECONDS, conf.get_dash_update_period("test.com"));
    }

    // Test default value when vhost exists but no dash section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{enabled on;}}"));

        // Should return default value when no dash section
        EXPECT_EQ(5 * SRS_UTIME_SECONDS, conf.get_dash_update_period("test.com"));
    }

    // Test default value when dash exists but no dash_update_period config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dash{enabled on;}}"));

        // Should return default value when no dash_update_period config
        EXPECT_EQ(5 * SRS_UTIME_SECONDS, conf.get_dash_update_period("test.com"));
    }

    // Test default value when dash_update_period has empty argument
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dash{dash_update_period;}}"));

        // Should return default value when empty argument
        EXPECT_EQ(5 * SRS_UTIME_SECONDS, conf.get_dash_update_period("test.com"));
    }

    // Test valid dash_update_period values
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dash{dash_update_period 60;}}"));
        EXPECT_EQ(60 * SRS_UTIME_SECONDS, conf.get_dash_update_period("test.com"));

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dash{dash_update_period 30.5;}}"));
        EXPECT_EQ((srs_utime_t)(30.5 * SRS_UTIME_SECONDS), conf.get_dash_update_period("test.com"));

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dash{dash_update_period 300;}}"));
        EXPECT_EQ(300 * SRS_UTIME_SECONDS, conf.get_dash_update_period("test.com"));
    }

    // Test zero and negative values
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dash{dash_update_period 0;}}"));
        EXPECT_EQ(0, conf.get_dash_update_period("test.com"));

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dash{dash_update_period -10;}}"));
        EXPECT_EQ((srs_utime_t)(-10 * SRS_UTIME_SECONDS), conf.get_dash_update_period("test.com"));
    }
}

VOID TEST(ConfigDashTest, CheckDashTimeshift)
{
    srs_error_t err;

    // Test default value when no configuration is provided
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        // Default should be 300 seconds
        EXPECT_EQ(300 * SRS_UTIME_SECONDS, conf.get_dash_timeshift("__defaultVhost__"));
        EXPECT_EQ(300 * SRS_UTIME_SECONDS, conf.get_dash_timeshift("test.com"));
    }

    // Test default value when vhost exists but no dash section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{enabled on;}}"));

        // Should return default value when no dash section
        EXPECT_EQ(300 * SRS_UTIME_SECONDS, conf.get_dash_timeshift("test.com"));
    }

    // Test default value when dash exists but no dash_timeshift config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dash{enabled on;}}"));

        // Should return default value when no dash_timeshift config
        EXPECT_EQ(300 * SRS_UTIME_SECONDS, conf.get_dash_timeshift("test.com"));
    }

    // Test default value when dash_timeshift has empty argument
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dash{dash_timeshift;}}"));

        // Should return default value when empty argument
        EXPECT_EQ(300 * SRS_UTIME_SECONDS, conf.get_dash_timeshift("test.com"));
    }

    // Test valid dash_timeshift values
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dash{dash_timeshift 120;}}"));
        EXPECT_EQ(120 * SRS_UTIME_SECONDS, conf.get_dash_timeshift("test.com"));

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dash{dash_timeshift 60.5;}}"));
        EXPECT_EQ((srs_utime_t)(60.5 * SRS_UTIME_SECONDS), conf.get_dash_timeshift("test.com"));

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dash{dash_timeshift 600;}}"));
        EXPECT_EQ(600 * SRS_UTIME_SECONDS, conf.get_dash_timeshift("test.com"));
    }

    // Test zero and negative values
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dash{dash_timeshift 0;}}"));
        EXPECT_EQ(0, conf.get_dash_timeshift("test.com"));

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dash{dash_timeshift -30;}}"));
        EXPECT_EQ((srs_utime_t)(-30 * SRS_UTIME_SECONDS), conf.get_dash_timeshift("test.com"));
    }
}

VOID TEST(ConfigDashTest, CheckDashPath)
{
    srs_error_t err;

    // Test default value when no configuration is provided
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        // Default should be "./objs/nginx/html"
        EXPECT_STREQ("./objs/nginx/html", conf.get_dash_path("__defaultVhost__").c_str());
        EXPECT_STREQ("./objs/nginx/html", conf.get_dash_path("test.com").c_str());
    }

    // Test default value when vhost exists but no dash section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{enabled on;}}"));

        // Should return default value when no dash section
        EXPECT_STREQ("./objs/nginx/html", conf.get_dash_path("test.com").c_str());
    }

    // Test default value when dash exists but no dash_path config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dash{enabled on;}}"));

        // Should return default value when no dash_path config
        EXPECT_STREQ("./objs/nginx/html", conf.get_dash_path("test.com").c_str());
    }

    // Test default value when dash_path has empty argument
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dash{dash_path;}}"));

        // Should return default value when empty argument
        EXPECT_STREQ("./objs/nginx/html", conf.get_dash_path("test.com").c_str());
    }

    // Test valid dash_path values
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dash{dash_path /tmp/dash;}}"));
        EXPECT_STREQ("/tmp/dash", conf.get_dash_path("test.com").c_str());

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dash{dash_path ./custom_dash;}}"));
        EXPECT_STREQ("./custom_dash", conf.get_dash_path("test.com").c_str());

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dash{dash_path /var/www/dash;}}"));
        EXPECT_STREQ("/var/www/dash", conf.get_dash_path("test.com").c_str());
    }

    // Test dash_path with spaces and special characters
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dash{dash_path \"/path with spaces\";}}"));
        EXPECT_STREQ("/path with spaces", conf.get_dash_path("test.com").c_str());
    }

    // Test multiple vhosts with different dash paths
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF
                                              "vhost vhost1.com{dash{dash_path /tmp/dash1;}} "
                                              "vhost vhost2.com{dash{dash_path /tmp/dash2;}} "
                                              "vhost vhost3.com{dash{enabled on;}}"));

        EXPECT_STREQ("/tmp/dash1", conf.get_dash_path("vhost1.com").c_str());
        EXPECT_STREQ("/tmp/dash2", conf.get_dash_path("vhost2.com").c_str());
        EXPECT_STREQ("./objs/nginx/html", conf.get_dash_path("vhost3.com").c_str()); // default
    }
}

VOID TEST(ConfigDashTest, CheckDashMpdFile)
{
    srs_error_t err;

    // Test default value when no configuration is provided
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        // Default should be "[app]/[stream].mpd"
        EXPECT_STREQ("[app]/[stream].mpd", conf.get_dash_mpd_file("__defaultVhost__").c_str());
        EXPECT_STREQ("[app]/[stream].mpd", conf.get_dash_mpd_file("test.com").c_str());
    }

    // Test default value when vhost exists but no dash section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{enabled on;}}"));

        // Should return default value when no dash section
        EXPECT_STREQ("[app]/[stream].mpd", conf.get_dash_mpd_file("test.com").c_str());
    }

    // Test default value when dash exists but no dash_mpd_file config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dash{enabled on;}}"));

        // Should return default value when no dash_mpd_file config
        EXPECT_STREQ("[app]/[stream].mpd", conf.get_dash_mpd_file("test.com").c_str());
    }

    // Test default value when dash_mpd_file has empty argument
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dash{dash_mpd_file;}}"));

        // Should return default value when empty argument
        EXPECT_STREQ("[app]/[stream].mpd", conf.get_dash_mpd_file("test.com").c_str());
    }

    // Test valid dash_mpd_file values
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dash{dash_mpd_file [app]/[stream].mpd;}}"));
        EXPECT_STREQ("[app]/[stream].mpd", conf.get_dash_mpd_file("test.com").c_str());

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dash{dash_mpd_file custom.mpd;}}"));
        EXPECT_STREQ("custom.mpd", conf.get_dash_mpd_file("test.com").c_str());

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dash{dash_mpd_file [vhost]/[app]/[stream].mpd;}}"));
        EXPECT_STREQ("[vhost]/[app]/[stream].mpd", conf.get_dash_mpd_file("test.com").c_str());
    }

    // Test dash_mpd_file with complex patterns
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dash{dash_mpd_file [timestamp]/[app]/[stream].mpd;}}"));
        EXPECT_STREQ("[timestamp]/[app]/[stream].mpd", conf.get_dash_mpd_file("test.com").c_str());
    }

    // Test multiple vhosts with different mpd file patterns
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF
                                              "vhost vhost1.com{dash{dash_mpd_file [app]/[stream].mpd;}} "
                                              "vhost vhost2.com{dash{dash_mpd_file custom.mpd;}} "
                                              "vhost vhost3.com{dash{enabled on;}}"));

        EXPECT_STREQ("[app]/[stream].mpd", conf.get_dash_mpd_file("vhost1.com").c_str());
        EXPECT_STREQ("custom.mpd", conf.get_dash_mpd_file("vhost2.com").c_str());
        EXPECT_STREQ("[app]/[stream].mpd", conf.get_dash_mpd_file("vhost3.com").c_str()); // default
    }
}

VOID TEST(ConfigDashTest, CheckDashWindowSize)
{
    srs_error_t err;

    // Test default value when no configuration is provided
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        // Default should be 5
        EXPECT_EQ(5, conf.get_dash_window_size("__defaultVhost__"));
        EXPECT_EQ(5, conf.get_dash_window_size("test.com"));
    }

    // Test default value when vhost exists but no dash section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{enabled on;}}"));

        // Should return default value when no dash section
        EXPECT_EQ(5, conf.get_dash_window_size("test.com"));
    }

    // Test default value when dash exists but no dash_window_size config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dash{enabled on;}}"));

        // Should return default value when no dash_window_size config
        EXPECT_EQ(5, conf.get_dash_window_size("test.com"));
    }

    // Test default value when dash_window_size has empty argument
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dash{dash_window_size;}}"));

        // Should return default value when empty argument
        EXPECT_EQ(5, conf.get_dash_window_size("test.com"));
    }

    // Test valid dash_window_size values
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dash{dash_window_size 10;}}"));
        EXPECT_EQ(10, conf.get_dash_window_size("test.com"));

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dash{dash_window_size 1;}}"));
        EXPECT_EQ(1, conf.get_dash_window_size("test.com"));

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dash{dash_window_size 100;}}"));
        EXPECT_EQ(100, conf.get_dash_window_size("test.com"));
    }

    // Test zero and negative values
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dash{dash_window_size 0;}}"));
        EXPECT_EQ(0, conf.get_dash_window_size("test.com"));

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dash{dash_window_size -5;}}"));
        EXPECT_EQ(-5, conf.get_dash_window_size("test.com"));
    }
}

VOID TEST(ConfigDashTest, CheckDashWindowSizeEnvironment)
{
    srs_error_t err;

    // Test environment variable overrides config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dash{dash_window_size 10;}}"));

        SrsSetEnvConfig(conf, dash_window_size1, "SRS_VHOST_DASH_DASH_WINDOW_SIZE", "20");

        // Environment should override config value
        // Note: Due to SRS_OVERWRITE_BY_ENV_FLOAT_SECONDS bug, env values are converted to microseconds
        EXPECT_EQ(20 * SRS_UTIME_SECONDS, conf.get_dash_window_size("test.com"));
    }

    // Test environment variable with valid values
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        SrsSetEnvConfig(conf, dash_window_size2, "SRS_VHOST_DASH_DASH_WINDOW_SIZE", "15");
        EXPECT_EQ(15 * SRS_UTIME_SECONDS, conf.get_dash_window_size("test.com"));

        SrsSetEnvConfig(conf, dash_window_size3, "SRS_VHOST_DASH_DASH_WINDOW_SIZE", "1");
        EXPECT_EQ(1 * SRS_UTIME_SECONDS, conf.get_dash_window_size("test.com"));
    }

    // Test environment variable with zero and negative values
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        SrsSetEnvConfig(conf, dash_window_size4, "SRS_VHOST_DASH_DASH_WINDOW_SIZE", "0");
        EXPECT_EQ(0, conf.get_dash_window_size("test.com"));

        SrsSetEnvConfig(conf, dash_window_size5, "SRS_VHOST_DASH_DASH_WINDOW_SIZE", "-10");
        EXPECT_EQ(-10 * SRS_UTIME_SECONDS, conf.get_dash_window_size("test.com"));
    }
}

VOID TEST(ConfigDashTest, CheckDashCleanup)
{
    srs_error_t err;

    // Test default value when no configuration is provided
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        // Default should be true
        EXPECT_TRUE(conf.get_dash_cleanup("__defaultVhost__"));
        EXPECT_TRUE(conf.get_dash_cleanup("test.com"));
    }

    // Test default value when vhost exists but no dash section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{enabled on;}}"));

        // Should return default value when no dash section
        EXPECT_TRUE(conf.get_dash_cleanup("test.com"));
    }

    // Test default value when dash exists but no dash_cleanup config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dash{enabled on;}}"));

        // Should return default value when no dash_cleanup config
        EXPECT_TRUE(conf.get_dash_cleanup("test.com"));
    }

    // Test default value when dash_cleanup has empty argument
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dash{dash_cleanup;}}"));

        // Should return default value when empty argument
        EXPECT_TRUE(conf.get_dash_cleanup("test.com"));
    }

    // Test explicit dash_cleanup enabled
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dash{dash_cleanup on;}}"));

        EXPECT_TRUE(conf.get_dash_cleanup("test.com"));
    }

    // Test explicit dash_cleanup disabled
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dash{dash_cleanup off;}}"));

        EXPECT_FALSE(conf.get_dash_cleanup("test.com"));
    }

    // Test various boolean values (SRS_CONF_PREFER_TRUE: only "off" is false)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dash{dash_cleanup true;}}"));
        EXPECT_TRUE(conf.get_dash_cleanup("test.com")); // "true" != "off", so it's true

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dash{dash_cleanup false;}}"));
        EXPECT_TRUE(conf.get_dash_cleanup("test.com")); // "false" != "off", so it's true

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dash{dash_cleanup yes;}}"));
        EXPECT_TRUE(conf.get_dash_cleanup("test.com")); // "yes" != "off", so it's true

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dash{dash_cleanup no;}}"));
        EXPECT_TRUE(conf.get_dash_cleanup("test.com")); // "no" != "off", so it's true
    }
}

VOID TEST(ConfigDashTest, CheckDashCleanupEnvironment)
{
    srs_error_t err;

    // Test environment variable overrides config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dash{dash_cleanup on;}}"));

        SrsSetEnvConfig(conf, dash_cleanup1, "SRS_VHOST_DASH_DASH_CLEANUP", "off");

        // Environment should override config value
        EXPECT_FALSE(conf.get_dash_cleanup("test.com"));
    }

    // Test environment variable with various boolean values
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        SrsSetEnvConfig(conf, dash_cleanup2, "SRS_VHOST_DASH_DASH_CLEANUP", "on");
        EXPECT_TRUE(conf.get_dash_cleanup("test.com"));

        SrsSetEnvConfig(conf, dash_cleanup3, "SRS_VHOST_DASH_DASH_CLEANUP", "true");
        EXPECT_TRUE(conf.get_dash_cleanup("test.com")); // "true" != "off", so it's true

        SrsSetEnvConfig(conf, dash_cleanup4, "SRS_VHOST_DASH_DASH_CLEANUP", "false");
        EXPECT_TRUE(conf.get_dash_cleanup("test.com")); // "false" != "off", so it's true

        SrsSetEnvConfig(conf, dash_cleanup5, "SRS_VHOST_DASH_DASH_CLEANUP", "off");
        EXPECT_FALSE(conf.get_dash_cleanup("test.com")); // Only "off" is false
    }
}

VOID TEST(ConfigDashTest, CheckDashDispose)
{
    srs_error_t err;

    // Test default value when no configuration is provided
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        // Default should be 120 seconds
        EXPECT_EQ(120 * SRS_UTIME_SECONDS, conf.get_dash_dispose("__defaultVhost__"));
        EXPECT_EQ(120 * SRS_UTIME_SECONDS, conf.get_dash_dispose("test.com"));
    }

    // Test default value when vhost exists but no dash section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{enabled on;}}"));

        // Should return default value when no dash section
        EXPECT_EQ(120 * SRS_UTIME_SECONDS, conf.get_dash_dispose("test.com"));
    }

    // Test default value when dash exists but no dash_dispose config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dash{enabled on;}}"));

        // Should return default value when no dash_dispose config
        EXPECT_EQ(120 * SRS_UTIME_SECONDS, conf.get_dash_dispose("test.com"));
    }

    // Test default value when dash_dispose has empty argument
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dash{dash_dispose;}}"));

        // Should return default value when empty argument
        EXPECT_EQ(120 * SRS_UTIME_SECONDS, conf.get_dash_dispose("test.com"));
    }

    // Test valid dash_dispose values
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dash{dash_dispose 120;}}"));
        EXPECT_EQ(120 * SRS_UTIME_SECONDS, conf.get_dash_dispose("test.com"));

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dash{dash_dispose 300;}}"));
        EXPECT_EQ(300 * SRS_UTIME_SECONDS, conf.get_dash_dispose("test.com"));

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dash{dash_dispose 0;}}"));
        EXPECT_EQ(0, conf.get_dash_dispose("test.com"));
    }

    // Test negative values
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dash{dash_dispose -60;}}"));
        EXPECT_EQ(-60 * SRS_UTIME_SECONDS, conf.get_dash_dispose("test.com"));
    }
}

VOID TEST(ConfigHlsTest, CheckHlsEnabled)
{
    srs_error_t err;

    // Test default value when no configuration is provided
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        // Default should be false
        EXPECT_FALSE(conf.get_hls_enabled("__defaultVhost__"));
        EXPECT_FALSE(conf.get_hls_enabled("test.com"));
    }

    // Test default value when vhost exists but no hls section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dash{enabled on;}}"));

        // Should return default value when no hls section
        EXPECT_FALSE(conf.get_hls_enabled("test.com"));
    }

    // Test default value when hls exists but no enabled config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_fragment 10;}}"));

        // Should return default value when no enabled config
        EXPECT_FALSE(conf.get_hls_enabled("test.com"));
    }

    // Test default value when enabled has empty argument
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{enabled;}}"));

        // Should return default value when empty argument
        EXPECT_FALSE(conf.get_hls_enabled("test.com"));
    }

    // Test explicit hls enabled
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{enabled on;}}"));

        EXPECT_TRUE(conf.get_hls_enabled("test.com"));
    }

    // Test explicit hls disabled
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{enabled off;}}"));

        EXPECT_FALSE(conf.get_hls_enabled("test.com"));
    }

    // Test various boolean values (SRS_CONF_PREFER_FALSE: only "on" is true)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{enabled true;}}"));
        EXPECT_FALSE(conf.get_hls_enabled("test.com")); // "true" != "on", so it's false

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{enabled false;}}"));
        EXPECT_FALSE(conf.get_hls_enabled("test.com")); // "false" != "on", so it's false

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{enabled yes;}}"));
        EXPECT_FALSE(conf.get_hls_enabled("test.com")); // "yes" != "on", so it's false

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{enabled no;}}"));
        EXPECT_FALSE(conf.get_hls_enabled("test.com")); // "no" != "on", so it's false
    }

    // Test multiple vhosts with different configurations
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF
                                              "vhost vhost1.com{hls{enabled on;}} "
                                              "vhost vhost2.com{hls{enabled off;}} "
                                              "vhost vhost3.com{hls{hls_fragment 10;}}"));

        EXPECT_TRUE(conf.get_hls_enabled("vhost1.com"));
        EXPECT_FALSE(conf.get_hls_enabled("vhost2.com"));
        EXPECT_FALSE(conf.get_hls_enabled("vhost3.com")); // default
    }
}

VOID TEST(ConfigHlsTest, CheckHlsUseFmp4)
{
    srs_error_t err;

    // Test default value when no configuration is provided
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        // Default should be false
        EXPECT_FALSE(conf.get_hls_use_fmp4("__defaultVhost__"));
        EXPECT_FALSE(conf.get_hls_use_fmp4("test.com"));
    }

    // Test default value when vhost exists but no hls section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dash{enabled on;}}"));

        // Should return default value when no hls section
        EXPECT_FALSE(conf.get_hls_use_fmp4("test.com"));
    }

    // Test default value when hls exists but no hls_use_fmp4 config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{enabled on;}}"));

        // Should return default value when no hls_use_fmp4 config
        EXPECT_FALSE(conf.get_hls_use_fmp4("test.com"));
    }

    // Test default value when hls_use_fmp4 has empty argument
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_use_fmp4;}}"));

        // Should return default value when empty argument
        EXPECT_FALSE(conf.get_hls_use_fmp4("test.com"));
    }

    // Test explicit hls_use_fmp4 enabled
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_use_fmp4 on;}}"));

        EXPECT_TRUE(conf.get_hls_use_fmp4("test.com"));
    }

    // Test explicit hls_use_fmp4 disabled
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_use_fmp4 off;}}"));

        EXPECT_FALSE(conf.get_hls_use_fmp4("test.com"));
    }

    // Test various boolean values (SRS_CONF_PREFER_FALSE: only "on" is true)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_use_fmp4 true;}}"));
        EXPECT_FALSE(conf.get_hls_use_fmp4("test.com")); // "true" != "on", so it's false

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_use_fmp4 false;}}"));
        EXPECT_FALSE(conf.get_hls_use_fmp4("test.com")); // "false" != "on", so it's false

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_use_fmp4 yes;}}"));
        EXPECT_FALSE(conf.get_hls_use_fmp4("test.com")); // "yes" != "on", so it's false

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_use_fmp4 no;}}"));
        EXPECT_FALSE(conf.get_hls_use_fmp4("test.com")); // "no" != "on", so it's false
    }

    // Test multiple vhosts with different configurations
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF
                                              "vhost vhost1.com{hls{hls_use_fmp4 on;}} "
                                              "vhost vhost2.com{hls{hls_use_fmp4 off;}} "
                                              "vhost vhost3.com{hls{enabled on;}}"));

        EXPECT_TRUE(conf.get_hls_use_fmp4("vhost1.com"));
        EXPECT_FALSE(conf.get_hls_use_fmp4("vhost2.com"));
        EXPECT_FALSE(conf.get_hls_use_fmp4("vhost3.com")); // default
    }
}

VOID TEST(ConfigHlsTest, CheckHlsEntryPrefix)
{
    srs_error_t err;

    // Test default value when no configuration is provided
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        // Default should be empty string
        EXPECT_STREQ("", conf.get_hls_entry_prefix("__defaultVhost__").c_str());
        EXPECT_STREQ("", conf.get_hls_entry_prefix("test.com").c_str());
    }

    // Test default value when vhost exists but no hls section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dash{enabled on;}}"));

        // Should return default value when no hls section
        EXPECT_STREQ("", conf.get_hls_entry_prefix("test.com").c_str());
    }

    // Test default value when hls exists but no hls_entry_prefix config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{enabled on;}}"));

        // Should return default value when no hls_entry_prefix config
        EXPECT_STREQ("", conf.get_hls_entry_prefix("test.com").c_str());
    }

    // Test valid hls_entry_prefix values
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_entry_prefix http://cdn.example.com/;}}"));
        EXPECT_STREQ("http://cdn.example.com/", conf.get_hls_entry_prefix("test.com").c_str());

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_entry_prefix https://example.com/hls/;}}"));
        EXPECT_STREQ("https://example.com/hls/", conf.get_hls_entry_prefix("test.com").c_str());

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_entry_prefix /static/hls/;}}"));
        EXPECT_STREQ("/static/hls/", conf.get_hls_entry_prefix("test.com").c_str());
    }

    // Test empty hls_entry_prefix value
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{hls_entry_prefix \"\";}}"));
        EXPECT_STREQ("", conf.get_hls_entry_prefix("test.com").c_str());
    }

    // Test multiple vhosts with different entry prefixes
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF
                                              "vhost vhost1.com{hls{hls_entry_prefix http://cdn1.com/;}} "
                                              "vhost vhost2.com{hls{hls_entry_prefix http://cdn2.com/;}} "
                                              "vhost vhost3.com{hls{enabled on;}}"));

        EXPECT_STREQ("http://cdn1.com/", conf.get_hls_entry_prefix("vhost1.com").c_str());
        EXPECT_STREQ("http://cdn2.com/", conf.get_hls_entry_prefix("vhost2.com").c_str());
        EXPECT_STREQ("", conf.get_hls_entry_prefix("vhost3.com").c_str()); // default
    }
}

// HDS Configuration Tests
VOID TEST(ConfigHdsTest, CheckHdsEnabled)
{
    srs_error_t err;

    // Test default value when no configuration is provided
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        // Default should be false (SRS_CONF_PREFER_FALSE)
        EXPECT_FALSE(conf.get_hds_enabled("__defaultVhost__"));
        EXPECT_FALSE(conf.get_hds_enabled("test.com"));
    }

    // Test default value when vhost exists but no hds section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{enabled on;}}"));

        // Should return default value when no hds section
        EXPECT_FALSE(conf.get_hds_enabled("test.com"));
    }

    // Test default value when hds exists but no enabled config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hds{hds_path ./objs/nginx/html;}}"));

        // Should return default value when no enabled config
        EXPECT_FALSE(conf.get_hds_enabled("test.com"));
    }

    // Test default value when enabled has empty argument
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hds{enabled;}}"));

        // Should return default value when empty argument
        EXPECT_FALSE(conf.get_hds_enabled("test.com"));
    }

    // Test explicit hds enabled
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hds{enabled on;}}"));

        EXPECT_TRUE(conf.get_hds_enabled("test.com"));
    }

    // Test explicit hds disabled
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hds{enabled off;}}"));

        EXPECT_FALSE(conf.get_hds_enabled("test.com"));
    }

    // Test various boolean values (SRS_CONF_PREFER_FALSE: only "on" is true)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hds{enabled true;}}"));
        EXPECT_FALSE(conf.get_hds_enabled("test.com")); // "true" != "on", so it's false

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hds{enabled false;}}"));
        EXPECT_FALSE(conf.get_hds_enabled("test.com")); // "false" != "on", so it's false

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hds{enabled yes;}}"));
        EXPECT_FALSE(conf.get_hds_enabled("test.com")); // "yes" != "on", so it's false

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hds{enabled no;}}"));
        EXPECT_FALSE(conf.get_hds_enabled("test.com")); // "no" != "on", so it's false
    }
}

VOID TEST(ConfigHdsTest, CheckHdsPath)
{
    srs_error_t err;

    // Test default value when no configuration is provided
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        // Default should be "./objs/nginx/html"
        EXPECT_STREQ("./objs/nginx/html", conf.get_hds_path("__defaultVhost__").c_str());
        EXPECT_STREQ("./objs/nginx/html", conf.get_hds_path("test.com").c_str());
    }

    // Test default value when vhost exists but no hds section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{enabled on;}}"));

        // Should return default value when no hds section
        EXPECT_STREQ("./objs/nginx/html", conf.get_hds_path("test.com").c_str());
    }

    // Test default value when hds exists but no hds_path config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hds{enabled on;}}"));

        // Should return default value when no hds_path config
        EXPECT_STREQ("./objs/nginx/html", conf.get_hds_path("test.com").c_str());
    }

    // Test default value when hds_path has empty argument
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hds{hds_path;}}"));

        // Should return default value when empty argument
        EXPECT_STREQ("./objs/nginx/html", conf.get_hds_path("test.com").c_str());
    }

    // Test valid hds_path values
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hds{hds_path /var/www/hds;}}"));
        EXPECT_STREQ("/var/www/hds", conf.get_hds_path("test.com").c_str());

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hds{hds_path ./custom/hds/path;}}"));
        EXPECT_STREQ("./custom/hds/path", conf.get_hds_path("test.com").c_str());

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hds{hds_path /tmp/hds;}}"));
        EXPECT_STREQ("/tmp/hds", conf.get_hds_path("test.com").c_str());
    }

    // Test multiple vhosts with different hds paths
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF
                                              "vhost vhost1.com{hds{hds_path /var/www/hds1;}} "
                                              "vhost vhost2.com{hds{hds_path /var/www/hds2;}} "
                                              "vhost vhost3.com{hds{enabled on;}}"));

        EXPECT_STREQ("/var/www/hds1", conf.get_hds_path("vhost1.com").c_str());
        EXPECT_STREQ("/var/www/hds2", conf.get_hds_path("vhost2.com").c_str());
        EXPECT_STREQ("./objs/nginx/html", conf.get_hds_path("vhost3.com").c_str()); // default
    }
}

VOID TEST(ConfigHdsTest, CheckHdsFragment)
{
    srs_error_t err;

    // Test default value when no configuration is provided
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        // Default should be 10 seconds
        EXPECT_EQ(10 * SRS_UTIME_SECONDS, conf.get_hds_fragment("__defaultVhost__"));
        EXPECT_EQ(10 * SRS_UTIME_SECONDS, conf.get_hds_fragment("test.com"));
    }

    // Test default value when vhost exists but no hds section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{enabled on;}}"));

        // Should return default value when no hds section
        EXPECT_EQ(10 * SRS_UTIME_SECONDS, conf.get_hds_fragment("test.com"));
    }

    // Test default value when hds exists but no hds_fragment config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hds{enabled on;}}"));

        // Should return default value when no hds_fragment config
        EXPECT_EQ(10 * SRS_UTIME_SECONDS, conf.get_hds_fragment("test.com"));
    }

    // Test default value when hds_fragment has empty argument
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hds{hds_fragment;}}"));

        // Should return default value when empty argument
        EXPECT_EQ(10 * SRS_UTIME_SECONDS, conf.get_hds_fragment("test.com"));
    }

    // Test valid hds_fragment values
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hds{hds_fragment 5;}}"));
        EXPECT_EQ(5 * SRS_UTIME_SECONDS, conf.get_hds_fragment("test.com"));

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hds{hds_fragment 2.5;}}"));
        EXPECT_EQ((srs_utime_t)(2.5 * SRS_UTIME_SECONDS), conf.get_hds_fragment("test.com"));

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hds{hds_fragment 30;}}"));
        EXPECT_EQ(30 * SRS_UTIME_SECONDS, conf.get_hds_fragment("test.com"));
    }

    // Test zero and negative values
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hds{hds_fragment 0;}}"));
        EXPECT_EQ(0, conf.get_hds_fragment("test.com"));

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hds{hds_fragment -5;}}"));
        EXPECT_EQ((srs_utime_t)(-5 * SRS_UTIME_SECONDS), conf.get_hds_fragment("test.com"));
    }

    // Test multiple vhosts with different fragment values
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF
                                              "vhost vhost1.com{hds{hds_fragment 5;}} "
                                              "vhost vhost2.com{hds{hds_fragment 15;}} "
                                              "vhost vhost3.com{hds{enabled on;}}"));

        EXPECT_EQ(5 * SRS_UTIME_SECONDS, conf.get_hds_fragment("vhost1.com"));
        EXPECT_EQ(15 * SRS_UTIME_SECONDS, conf.get_hds_fragment("vhost2.com"));
        EXPECT_EQ(10 * SRS_UTIME_SECONDS, conf.get_hds_fragment("vhost3.com")); // default
    }
}

VOID TEST(ConfigHdsTest, CheckHdsWindow)
{
    srs_error_t err;

    // Test default value when no configuration is provided
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        // Default should be 60 seconds
        EXPECT_EQ(60 * SRS_UTIME_SECONDS, conf.get_hds_window("__defaultVhost__"));
        EXPECT_EQ(60 * SRS_UTIME_SECONDS, conf.get_hds_window("test.com"));
    }

    // Test default value when vhost exists but no hds section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{enabled on;}}"));

        // Should return default value when no hds section
        EXPECT_EQ(60 * SRS_UTIME_SECONDS, conf.get_hds_window("test.com"));
    }

    // Test default value when hds exists but no hds_window config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hds{enabled on;}}"));

        // Should return default value when no hds_window config
        EXPECT_EQ(60 * SRS_UTIME_SECONDS, conf.get_hds_window("test.com"));
    }

    // Test default value when hds_window has empty argument
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hds{hds_window;}}"));

        // Should return default value when empty argument
        EXPECT_EQ(60 * SRS_UTIME_SECONDS, conf.get_hds_window("test.com"));
    }

    // Test valid hds_window values
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hds{hds_window 30;}}"));
        EXPECT_EQ(30 * SRS_UTIME_SECONDS, conf.get_hds_window("test.com"));

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hds{hds_window 120.5;}}"));
        EXPECT_EQ((srs_utime_t)(120.5 * SRS_UTIME_SECONDS), conf.get_hds_window("test.com"));

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hds{hds_window 300;}}"));
        EXPECT_EQ(300 * SRS_UTIME_SECONDS, conf.get_hds_window("test.com"));
    }

    // Test zero and negative values
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hds{hds_window 0;}}"));
        EXPECT_EQ(0, conf.get_hds_window("test.com"));

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hds{hds_window -10;}}"));
        EXPECT_EQ((srs_utime_t)(-10 * SRS_UTIME_SECONDS), conf.get_hds_window("test.com"));
    }

    // Test multiple vhosts with different window values
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF
                                              "vhost vhost1.com{hds{hds_window 30;}} "
                                              "vhost vhost2.com{hds{hds_window 120;}} "
                                              "vhost vhost3.com{hds{enabled on;}}"));

        EXPECT_EQ(30 * SRS_UTIME_SECONDS, conf.get_hds_window("vhost1.com"));
        EXPECT_EQ(120 * SRS_UTIME_SECONDS, conf.get_hds_window("vhost2.com"));
        EXPECT_EQ(60 * SRS_UTIME_SECONDS, conf.get_hds_window("vhost3.com")); // default
    }
}

// DVR Configuration Tests
VOID TEST(ConfigDvrTest, CheckDvrEnabled)
{
    srs_error_t err;

    // Test default value when no configuration is provided
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        // Default should be false (SRS_CONF_PREFER_FALSE)
        EXPECT_FALSE(conf.get_dvr_enabled("__defaultVhost__"));
        EXPECT_FALSE(conf.get_dvr_enabled("test.com"));
    }

    // Test default value when vhost exists but no dvr section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{enabled on;}}"));

        // Should return default value when no dvr section
        EXPECT_FALSE(conf.get_dvr_enabled("test.com"));
    }

    // Test default value when dvr exists but no enabled config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dvr{dvr_path ./objs/nginx/html;}}"));

        // Should return default value when no enabled config
        EXPECT_FALSE(conf.get_dvr_enabled("test.com"));
    }

    // Test default value when enabled has empty argument
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dvr{enabled;}}"));

        // Should return default value when empty argument
        EXPECT_FALSE(conf.get_dvr_enabled("test.com"));
    }

    // Test explicit dvr enabled
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dvr{enabled on;}}"));

        EXPECT_TRUE(conf.get_dvr_enabled("test.com"));
    }

    // Test explicit dvr disabled
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dvr{enabled off;}}"));

        EXPECT_FALSE(conf.get_dvr_enabled("test.com"));
    }

    // Test various boolean values (SRS_CONF_PREFER_FALSE: only "on" is true)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dvr{enabled true;}}"));
        EXPECT_FALSE(conf.get_dvr_enabled("test.com")); // "true" != "on", so it's false

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dvr{enabled false;}}"));
        EXPECT_FALSE(conf.get_dvr_enabled("test.com")); // "false" != "on", so it's false

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dvr{enabled yes;}}"));
        EXPECT_FALSE(conf.get_dvr_enabled("test.com")); // "yes" != "on", so it's false

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dvr{enabled no;}}"));
        EXPECT_FALSE(conf.get_dvr_enabled("test.com")); // "no" != "on", so it's false
    }
}

VOID TEST(ConfigDvrTest, CheckDvrApply)
{
    srs_error_t err;

    // Test default value when no configuration is provided
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        // Should return NULL when no dvr section
        EXPECT_TRUE(conf.get_dvr_apply("__defaultVhost__") == NULL);
        EXPECT_TRUE(conf.get_dvr_apply("test.com") == NULL);
    }

    // Test default value when vhost exists but no dvr section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{enabled on;}}"));

        // Should return NULL when no dvr section
        EXPECT_TRUE(conf.get_dvr_apply("test.com") == NULL);
    }

    // Test default value when dvr exists but no dvr_apply config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dvr{enabled on;}}"));

        // Should return NULL when no dvr_apply config
        EXPECT_TRUE(conf.get_dvr_apply("test.com") == NULL);
    }

    // Test default value when dvr_apply has empty argument
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dvr{dvr_apply;}}"));

        // Should return NULL when empty argument
        EXPECT_TRUE(conf.get_dvr_apply("test.com") == NULL);
    }

    // Test valid dvr_apply values
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dvr{dvr_apply all;}}"));

        SrsConfDirective *apply = conf.get_dvr_apply("test.com");
        EXPECT_TRUE(apply != NULL);
        if (apply) {
            EXPECT_STREQ("all", apply->arg0().c_str());
        }

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dvr{dvr_apply play;}}"));
        apply = conf.get_dvr_apply("test.com");
        EXPECT_TRUE(apply != NULL);
        if (apply) {
            EXPECT_STREQ("play", apply->arg0().c_str());
        }

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dvr{dvr_apply publish;}}"));
        apply = conf.get_dvr_apply("test.com");
        EXPECT_TRUE(apply != NULL);
        if (apply) {
            EXPECT_STREQ("publish", apply->arg0().c_str());
        }
    }

    // Test multiple vhosts with different apply values
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF
                                              "vhost vhost1.com{dvr{dvr_apply all;}} "
                                              "vhost vhost2.com{dvr{dvr_apply play;}} "
                                              "vhost vhost3.com{dvr{enabled on;}}"));

        SrsConfDirective *apply1 = conf.get_dvr_apply("vhost1.com");
        EXPECT_TRUE(apply1 != NULL);
        if (apply1) {
            EXPECT_STREQ("all", apply1->arg0().c_str());
        }

        SrsConfDirective *apply2 = conf.get_dvr_apply("vhost2.com");
        EXPECT_TRUE(apply2 != NULL);
        if (apply2) {
            EXPECT_STREQ("play", apply2->arg0().c_str());
        }

        SrsConfDirective *apply3 = conf.get_dvr_apply("vhost3.com");
        EXPECT_TRUE(apply3 == NULL); // default
    }
}

VOID TEST(ConfigDvrTest, CheckDvrPath)
{
    srs_error_t err;

    // Test default value when no configuration is provided
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        // Default should be "./objs/nginx/html/[app]/[stream].[timestamp].flv"
        EXPECT_STREQ("./objs/nginx/html/[app]/[stream].[timestamp].flv", conf.get_dvr_path("__defaultVhost__").c_str());
        EXPECT_STREQ("./objs/nginx/html/[app]/[stream].[timestamp].flv", conf.get_dvr_path("test.com").c_str());
    }

    // Test default value when vhost exists but no dvr section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{enabled on;}}"));

        // Should return default value when no dvr section
        EXPECT_STREQ("./objs/nginx/html/[app]/[stream].[timestamp].flv", conf.get_dvr_path("test.com").c_str());
    }

    // Test default value when dvr exists but no dvr_path config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dvr{enabled on;}}"));

        // Should return default value when no dvr_path config
        EXPECT_STREQ("./objs/nginx/html/[app]/[stream].[timestamp].flv", conf.get_dvr_path("test.com").c_str());
    }

    // Test default value when dvr_path has empty argument
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dvr{dvr_path;}}"));

        // Should return default value when empty argument
        EXPECT_STREQ("./objs/nginx/html/[app]/[stream].[timestamp].flv", conf.get_dvr_path("test.com").c_str());
    }

    // Test valid dvr_path values
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dvr{dvr_path /var/recordings;}}"));
        EXPECT_STREQ("/var/recordings", conf.get_dvr_path("test.com").c_str());

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dvr{dvr_path ./custom/dvr/path;}}"));
        EXPECT_STREQ("./custom/dvr/path", conf.get_dvr_path("test.com").c_str());

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dvr{dvr_path /tmp/dvr;}}"));
        EXPECT_STREQ("/tmp/dvr", conf.get_dvr_path("test.com").c_str());
    }

    // Test multiple vhosts with different dvr paths
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF
                                              "vhost vhost1.com{dvr{dvr_path /var/recordings1;}} "
                                              "vhost vhost2.com{dvr{dvr_path /var/recordings2;}} "
                                              "vhost vhost3.com{dvr{enabled on;}}"));

        EXPECT_STREQ("/var/recordings1", conf.get_dvr_path("vhost1.com").c_str());
        EXPECT_STREQ("/var/recordings2", conf.get_dvr_path("vhost2.com").c_str());
        EXPECT_STREQ("./objs/nginx/html/[app]/[stream].[timestamp].flv", conf.get_dvr_path("vhost3.com").c_str()); // default
    }
}

VOID TEST(ConfigDvrTest, CheckDvrPlan)
{
    srs_error_t err;

    // Test default value when no configuration is provided
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        // Default should be "session"
        EXPECT_STREQ("session", conf.get_dvr_plan("__defaultVhost__").c_str());
        EXPECT_STREQ("session", conf.get_dvr_plan("test.com").c_str());
    }

    // Test default value when vhost exists but no dvr section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{enabled on;}}"));

        // Should return default value when no dvr section
        EXPECT_STREQ("session", conf.get_dvr_plan("test.com").c_str());
    }

    // Test default value when dvr exists but no dvr_plan config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dvr{enabled on;}}"));

        // Should return default value when no dvr_plan config
        EXPECT_STREQ("session", conf.get_dvr_plan("test.com").c_str());
    }

    // Test default value when dvr_plan has empty argument
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dvr{dvr_plan;}}"));

        // Should return default value when empty argument
        EXPECT_STREQ("session", conf.get_dvr_plan("test.com").c_str());
    }

    // Test valid dvr_plan values
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dvr{dvr_plan session;}}"));
        EXPECT_STREQ("session", conf.get_dvr_plan("test.com").c_str());

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dvr{dvr_plan segment;}}"));
        EXPECT_STREQ("segment", conf.get_dvr_plan("test.com").c_str());

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dvr{dvr_plan append;}}"));
        EXPECT_STREQ("append", conf.get_dvr_plan("test.com").c_str());
    }

    // Test multiple vhosts with different dvr plans
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF
                                              "vhost vhost1.com{dvr{dvr_plan session;}} "
                                              "vhost vhost2.com{dvr{dvr_plan segment;}} "
                                              "vhost vhost3.com{dvr{enabled on;}}"));

        EXPECT_STREQ("session", conf.get_dvr_plan("vhost1.com").c_str());
        EXPECT_STREQ("segment", conf.get_dvr_plan("vhost2.com").c_str());
        EXPECT_STREQ("session", conf.get_dvr_plan("vhost3.com").c_str()); // default
    }
}

VOID TEST(ConfigDvrTest, CheckDvrDuration)
{
    srs_error_t err;

    // Test default value when no configuration is provided
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        // Default should be 30 seconds
        EXPECT_EQ(30 * SRS_UTIME_SECONDS, conf.get_dvr_duration("__defaultVhost__"));
        EXPECT_EQ(30 * SRS_UTIME_SECONDS, conf.get_dvr_duration("test.com"));
    }

    // Test default value when vhost exists but no dvr section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{enabled on;}}"));

        // Should return default value when no dvr section
        EXPECT_EQ(30 * SRS_UTIME_SECONDS, conf.get_dvr_duration("test.com"));
    }

    // Test default value when dvr exists but no dvr_duration config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dvr{enabled on;}}"));

        // Should return default value when no dvr_duration config
        EXPECT_EQ(30 * SRS_UTIME_SECONDS, conf.get_dvr_duration("test.com"));
    }

    // Test default value when dvr_duration has empty argument
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dvr{dvr_duration;}}"));

        // Should return default value when empty argument
        EXPECT_EQ(30 * SRS_UTIME_SECONDS, conf.get_dvr_duration("test.com"));
    }

    // Test valid dvr_duration values
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dvr{dvr_duration 60;}}"));
        EXPECT_EQ(60 * SRS_UTIME_SECONDS, conf.get_dvr_duration("test.com"));

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dvr{dvr_duration 120;}}"));
        EXPECT_EQ(120 * SRS_UTIME_SECONDS, conf.get_dvr_duration("test.com"));

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dvr{dvr_duration 3600;}}"));
        EXPECT_EQ(3600 * SRS_UTIME_SECONDS, conf.get_dvr_duration("test.com"));
    }

    // Test zero and negative values
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dvr{dvr_duration 0;}}"));
        EXPECT_EQ(0, conf.get_dvr_duration("test.com"));

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dvr{dvr_duration -60;}}"));
        EXPECT_EQ((srs_utime_t)(-60 * SRS_UTIME_SECONDS), conf.get_dvr_duration("test.com"));
    }

    // Test multiple vhosts with different duration values
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF
                                              "vhost vhost1.com{dvr{dvr_duration 60;}} "
                                              "vhost vhost2.com{dvr{dvr_duration 300;}} "
                                              "vhost vhost3.com{dvr{enabled on;}}"));

        EXPECT_EQ(60 * SRS_UTIME_SECONDS, conf.get_dvr_duration("vhost1.com"));
        EXPECT_EQ(300 * SRS_UTIME_SECONDS, conf.get_dvr_duration("vhost2.com"));
        EXPECT_EQ(30 * SRS_UTIME_SECONDS, conf.get_dvr_duration("vhost3.com")); // default
    }
}

VOID TEST(ConfigDvrTest, CheckDvrWaitKeyframe)
{
    srs_error_t err;

    // Test default value when no configuration is provided
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        // Default should be true (SRS_CONF_PREFER_TRUE)
        EXPECT_TRUE(conf.get_dvr_wait_keyframe("__defaultVhost__"));
        EXPECT_TRUE(conf.get_dvr_wait_keyframe("test.com"));
    }

    // Test default value when vhost exists but no dvr section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{enabled on;}}"));

        // Should return default value when no dvr section
        EXPECT_TRUE(conf.get_dvr_wait_keyframe("test.com"));
    }

    // Test default value when dvr exists but no dvr_wait_keyframe config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dvr{enabled on;}}"));

        // Should return default value when no dvr_wait_keyframe config
        EXPECT_TRUE(conf.get_dvr_wait_keyframe("test.com"));
    }

    // Test default value when dvr_wait_keyframe has empty argument
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dvr{dvr_wait_keyframe;}}"));

        // Should return default value when empty argument
        EXPECT_TRUE(conf.get_dvr_wait_keyframe("test.com"));
    }

    // Test explicit dvr_wait_keyframe enabled
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dvr{dvr_wait_keyframe on;}}"));

        EXPECT_TRUE(conf.get_dvr_wait_keyframe("test.com"));
    }

    // Test explicit dvr_wait_keyframe disabled
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dvr{dvr_wait_keyframe off;}}"));

        EXPECT_FALSE(conf.get_dvr_wait_keyframe("test.com"));
    }

    // Test various boolean values (SRS_CONF_PREFER_TRUE: only "off" is false)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dvr{dvr_wait_keyframe true;}}"));
        EXPECT_TRUE(conf.get_dvr_wait_keyframe("test.com")); // "true" != "off", so it's true

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dvr{dvr_wait_keyframe false;}}"));
        EXPECT_TRUE(conf.get_dvr_wait_keyframe("test.com")); // "false" != "off", so it's true

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dvr{dvr_wait_keyframe yes;}}"));
        EXPECT_TRUE(conf.get_dvr_wait_keyframe("test.com")); // "yes" != "off", so it's true

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dvr{dvr_wait_keyframe no;}}"));
        EXPECT_TRUE(conf.get_dvr_wait_keyframe("test.com")); // "no" != "off", so it's true
    }
}

VOID TEST(ConfigDvrTest, CheckDvrTimeJitter)
{
    srs_error_t err;

    // Test default value when no configuration is provided
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        // Default should be 1 (SrsRtmpJitterAlgorithmFULL) - default is "full"
        EXPECT_EQ(1, conf.get_dvr_time_jitter("__defaultVhost__"));
        EXPECT_EQ(1, conf.get_dvr_time_jitter("test.com"));
    }

    // Test default value when vhost exists but no dvr section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{enabled on;}}"));

        // Should return default value when no dvr section
        EXPECT_EQ(1, conf.get_dvr_time_jitter("test.com"));
    }

    // Test default value when dvr exists but no time_jitter config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dvr{enabled on;}}"));

        // Should return default value when no time_jitter config
        EXPECT_EQ(1, conf.get_dvr_time_jitter("test.com"));
    }

    // Test default value when time_jitter has empty argument
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dvr{time_jitter;}}"));

        // Should return default value when empty argument
        EXPECT_EQ(1, conf.get_dvr_time_jitter("test.com"));
    }

    // Test various time_jitter string values
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dvr{time_jitter off;}}"));
        EXPECT_EQ(3, conf.get_dvr_time_jitter("test.com")); // SrsRtmpJitterAlgorithmOFF = 3

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dvr{time_jitter full;}}"));
        EXPECT_EQ(1, conf.get_dvr_time_jitter("test.com")); // SrsRtmpJitterAlgorithmFULL = 1

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dvr{time_jitter zero;}}"));
        EXPECT_EQ(2, conf.get_dvr_time_jitter("test.com")); // SrsRtmpJitterAlgorithmZERO = 2
    }

    // Test invalid time_jitter values (should return OFF=3)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dvr{time_jitter invalid;}}"));
        EXPECT_EQ(3, conf.get_dvr_time_jitter("test.com")); // Should return OFF=3 for invalid value

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{dvr{time_jitter simple;}}"));
        EXPECT_EQ(3, conf.get_dvr_time_jitter("test.com")); // "simple" is not supported, returns OFF=3
    }

    // Test multiple vhosts with different time jitter values
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF
                                              "vhost vhost1.com{dvr{time_jitter off;}} "
                                              "vhost vhost2.com{dvr{time_jitter zero;}} "
                                              "vhost vhost3.com{dvr{enabled on;}}"));

        EXPECT_EQ(3, conf.get_dvr_time_jitter("vhost1.com")); // OFF
        EXPECT_EQ(2, conf.get_dvr_time_jitter("vhost2.com")); // ZERO
        EXPECT_EQ(1, conf.get_dvr_time_jitter("vhost3.com")); // default FULL
    }
}

// HTTP API Configuration Tests
VOID TEST(ConfigHttpApiTest, CheckHttpApiEnabled)
{
    srs_error_t err;

    // Test default value when no configuration is provided
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        // Default should be false (SRS_CONF_PREFER_FALSE)
        EXPECT_FALSE(conf.get_http_api_enabled());
    }

    // Test default value when http_api exists but no enabled config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_api{listen 1985;}"));

        // Should return default value when no enabled config
        EXPECT_FALSE(conf.get_http_api_enabled());
    }

    // Test default value when enabled has empty argument
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_api{enabled;}"));

        // Should return default value when empty argument
        EXPECT_FALSE(conf.get_http_api_enabled());
    }

    // Test explicit http_api enabled
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_api{enabled on;}"));

        EXPECT_TRUE(conf.get_http_api_enabled());
    }

    // Test explicit http_api disabled
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_api{enabled off;}"));

        EXPECT_FALSE(conf.get_http_api_enabled());
    }

    // Test various boolean values (SRS_CONF_PREFER_FALSE: only "on" is true)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_api{enabled true;}"));
        EXPECT_FALSE(conf.get_http_api_enabled()); // "true" != "on", so it's false

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_api{enabled false;}"));
        EXPECT_FALSE(conf.get_http_api_enabled()); // "false" != "on", so it's false

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_api{enabled yes;}"));
        EXPECT_FALSE(conf.get_http_api_enabled()); // "yes" != "on", so it's false

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_api{enabled no;}"));
        EXPECT_FALSE(conf.get_http_api_enabled()); // "no" != "on", so it's false
    }
}

VOID TEST(ConfigHttpApiTest, CheckHttpApiListens)
{
    srs_error_t err;

    // Test default value when no configuration is provided
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        // Default should be ["1985"]
        vector<string> listens = conf.get_http_api_listens();
        EXPECT_EQ(1, (int)listens.size());
        EXPECT_STREQ("1985", listens.at(0).c_str());
    }

    // Test default value when http_api exists but no listen config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_api{enabled on;}"));

        // Should return default value when no listen config
        vector<string> listens = conf.get_http_api_listens();
        EXPECT_EQ(1, (int)listens.size());
        EXPECT_STREQ("1985", listens.at(0).c_str());
    }

    // Test single listen port
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_api{listen 8080;}"));

        vector<string> listens = conf.get_http_api_listens();
        EXPECT_EQ(1, (int)listens.size());
        EXPECT_STREQ("8080", listens.at(0).c_str());
    }

    // Test multiple listen ports
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_api{listen 1985 8080 9090;}"));

        vector<string> listens = conf.get_http_api_listens();
        EXPECT_EQ(3, (int)listens.size());
        EXPECT_STREQ("1985", listens.at(0).c_str());
        EXPECT_STREQ("8080", listens.at(1).c_str());
        EXPECT_STREQ("9090", listens.at(2).c_str());
    }

    // Test listen with IP address
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_api{listen 127.0.0.1:1985;}"));

        vector<string> listens = conf.get_http_api_listens();
        EXPECT_EQ(1, (int)listens.size());
        EXPECT_STREQ("127.0.0.1:1985", listens.at(0).c_str());
    }

    // Test mixed listen formats
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_api{listen 1985 127.0.0.1:8080 0.0.0.0:9090;}"));

        vector<string> listens = conf.get_http_api_listens();
        EXPECT_EQ(3, (int)listens.size());
        EXPECT_STREQ("1985", listens.at(0).c_str());
        EXPECT_STREQ("127.0.0.1:8080", listens.at(1).c_str());
        EXPECT_STREQ("0.0.0.0:9090", listens.at(2).c_str());
    }
}

VOID TEST(ConfigHttpApiTest, CheckHttpApiCrossdomain)
{
    srs_error_t err;

    // Test default value when no configuration is provided
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        // Default should be true (SRS_CONF_PREFER_TRUE)
        EXPECT_TRUE(conf.get_http_api_crossdomain());
    }

    // Test default value when http_api exists but no crossdomain config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_api{enabled on;}"));

        // Should return default value when no crossdomain config
        EXPECT_TRUE(conf.get_http_api_crossdomain());
    }

    // Test default value when crossdomain has empty argument
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_api{crossdomain;}"));

        // Should return default value when empty argument
        EXPECT_TRUE(conf.get_http_api_crossdomain());
    }

    // Test explicit crossdomain enabled
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_api{crossdomain on;}"));

        EXPECT_TRUE(conf.get_http_api_crossdomain());
    }

    // Test explicit crossdomain disabled
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_api{crossdomain off;}"));

        EXPECT_FALSE(conf.get_http_api_crossdomain());
    }

    // Test various boolean values (SRS_CONF_PREFER_TRUE: only "off" is false)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_api{crossdomain true;}"));
        EXPECT_TRUE(conf.get_http_api_crossdomain()); // "true" != "off", so it's true

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_api{crossdomain false;}"));
        EXPECT_TRUE(conf.get_http_api_crossdomain()); // "false" != "off", so it's true

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_api{crossdomain yes;}"));
        EXPECT_TRUE(conf.get_http_api_crossdomain()); // "yes" != "off", so it's true

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_api{crossdomain no;}"));
        EXPECT_TRUE(conf.get_http_api_crossdomain()); // "no" != "off", so it's true
    }
}

VOID TEST(ConfigHttpApiTest, CheckRawApi)
{
    srs_error_t err;

    // Test default value when no configuration is provided
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        // Default should be false (SRS_CONF_PREFER_FALSE)
        EXPECT_FALSE(conf.get_raw_api());
    }

    // Test default value when http_api exists but no raw_api section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_api{enabled on;}"));

        // Should return default value when no raw_api section
        EXPECT_FALSE(conf.get_raw_api());
    }

    // Test default value when raw_api exists but no enabled config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_api{raw_api{allow_reload on;}}"));

        // Should return default value when no enabled config
        EXPECT_FALSE(conf.get_raw_api());
    }

    // Test default value when enabled has empty argument
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_api{raw_api{enabled;}}"));

        // Should return default value when empty argument
        EXPECT_FALSE(conf.get_raw_api());
    }

    // Test explicit raw_api enabled
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_api{raw_api{enabled on;}}"));

        EXPECT_TRUE(conf.get_raw_api());
    }

    // Test explicit raw_api disabled
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_api{raw_api{enabled off;}}"));

        EXPECT_FALSE(conf.get_raw_api());
    }

    // Test various boolean values (SRS_CONF_PREFER_FALSE: only "on" is true)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_api{raw_api{enabled true;}}"));
        EXPECT_FALSE(conf.get_raw_api()); // "true" != "on", so it's false

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_api{raw_api{enabled false;}}"));
        EXPECT_FALSE(conf.get_raw_api()); // "false" != "on", so it's false

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_api{raw_api{enabled yes;}}"));
        EXPECT_FALSE(conf.get_raw_api()); // "yes" != "on", so it's false

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_api{raw_api{enabled no;}}"));
        EXPECT_FALSE(conf.get_raw_api()); // "no" != "on", so it's false
    }
}

VOID TEST(ConfigHttpApiTest, CheckRawApiAllowReload)
{
    srs_error_t err;

    // Test default value when no configuration is provided
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        // Default should be false (SRS_CONF_PREFER_FALSE)
        EXPECT_FALSE(conf.get_raw_api_allow_reload());
    }

    // Test default value when http_api exists but no raw_api section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_api{enabled on;}"));

        // Should return default value when no raw_api section
        EXPECT_FALSE(conf.get_raw_api_allow_reload());
    }

    // Test default value when raw_api exists but no allow_reload config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_api{raw_api{enabled on;}}"));

        // Should return default value when no allow_reload config
        EXPECT_FALSE(conf.get_raw_api_allow_reload());
    }

    // Test default value when allow_reload has empty argument
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_api{raw_api{allow_reload;}}"));

        // Should return default value when empty argument
        EXPECT_FALSE(conf.get_raw_api_allow_reload());
    }

    // Test explicit allow_reload enabled
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_api{raw_api{allow_reload on;}}"));

        EXPECT_TRUE(conf.get_raw_api_allow_reload());
    }

    // Test explicit allow_reload disabled
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_api{raw_api{allow_reload off;}}"));

        EXPECT_FALSE(conf.get_raw_api_allow_reload());
    }

    // Test various boolean values (SRS_CONF_PREFER_FALSE: only "on" is true)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_api{raw_api{allow_reload true;}}"));
        EXPECT_FALSE(conf.get_raw_api_allow_reload()); // "true" != "on", so it's false

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_api{raw_api{allow_reload false;}}"));
        EXPECT_FALSE(conf.get_raw_api_allow_reload()); // "false" != "on", so it's false

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_api{raw_api{allow_reload yes;}}"));
        EXPECT_FALSE(conf.get_raw_api_allow_reload()); // "yes" != "on", so it's false

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_api{raw_api{allow_reload no;}}"));
        EXPECT_FALSE(conf.get_raw_api_allow_reload()); // "no" != "on", so it's false
    }
}

VOID TEST(ConfigHttpApiTest, CheckHttpApiAuthEnabled)
{
    srs_error_t err;

    // Test default value when no configuration is provided
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        // Default should be false (SRS_CONF_PREFER_FALSE)
        EXPECT_FALSE(conf.get_http_api_auth_enabled());
    }

    // Test default value when http_api exists but no auth section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_api{enabled on;}"));

        // Should return default value when no auth section
        EXPECT_FALSE(conf.get_http_api_auth_enabled());
    }

    // Test default value when auth exists but no enabled config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_api{auth{username admin;}}"));

        // Should return default value when no enabled config
        EXPECT_FALSE(conf.get_http_api_auth_enabled());
    }

    // Test default value when enabled has empty argument
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_api{auth{enabled;}}"));

        // Should return default value when empty argument
        EXPECT_FALSE(conf.get_http_api_auth_enabled());
    }

    // Test explicit auth enabled
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_api{auth{enabled on;type basic;}}"));

        EXPECT_TRUE(conf.get_http_api_auth_enabled());
    }

    // Test explicit auth disabled
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_api{auth{enabled off;}}"));

        EXPECT_FALSE(conf.get_http_api_auth_enabled());
    }

    // Test various boolean values (SRS_CONF_PREFER_FALSE: only "on" is true)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_api{auth{enabled true;}}"));
        EXPECT_FALSE(conf.get_http_api_auth_enabled()); // "true" != "on", so it's false

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_api{auth{enabled false;}}"));
        EXPECT_FALSE(conf.get_http_api_auth_enabled()); // "false" != "on", so it's false

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_api{auth{enabled yes;}}"));
        EXPECT_FALSE(conf.get_http_api_auth_enabled()); // "yes" != "on", so it's false

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_api{auth{enabled no;}}"));
        EXPECT_FALSE(conf.get_http_api_auth_enabled()); // "no" != "on", so it's false
    }
}

VOID TEST(ConfigHttpApiTest, CheckHttpApiAuthUsername)
{
    srs_error_t err;

    // Test default value when no configuration is provided
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        // Default should be empty string
        EXPECT_STREQ("", conf.get_http_api_auth_username().c_str());
    }

    // Test default value when http_api exists but no auth section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_api{enabled on;}"));

        // Should return default value when no auth section
        EXPECT_STREQ("", conf.get_http_api_auth_username().c_str());
    }

    // Test default value when auth exists but no username config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_api{auth{enabled on;type basic;}}"));

        // Should return default value when no username config
        EXPECT_STREQ("", conf.get_http_api_auth_username().c_str());
    }

    // Test valid username values
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_api{auth{username myuser;}}"));
        EXPECT_STREQ("myuser", conf.get_http_api_auth_username().c_str());

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_api{auth{username root;}}"));
        EXPECT_STREQ("root", conf.get_http_api_auth_username().c_str());

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_api{auth{username user123;}}"));
        EXPECT_STREQ("user123", conf.get_http_api_auth_username().c_str());
    }

    // Test empty username (should return empty string, not default)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_api{auth{username \"\";}}"));
        EXPECT_STREQ("", conf.get_http_api_auth_username().c_str());
    }
}

VOID TEST(ConfigHttpApiTest, CheckHttpApiAuthPassword)
{
    srs_error_t err;

    // Test default value when no configuration is provided
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        // Default should be empty string
        EXPECT_STREQ("", conf.get_http_api_auth_password().c_str());
    }

    // Test default value when http_api exists but no auth section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_api{enabled on;}"));

        // Should return default value when no auth section
        EXPECT_STREQ("", conf.get_http_api_auth_password().c_str());
    }

    // Test default value when auth exists but no password config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_api{auth{enabled on;type basic;}}"));

        // Should return default value when no password config
        EXPECT_STREQ("", conf.get_http_api_auth_password().c_str());
    }

    // Test valid password values
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_api{auth{password mypass;}}"));
        EXPECT_STREQ("mypass", conf.get_http_api_auth_password().c_str());

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_api{auth{password secret123;}}"));
        EXPECT_STREQ("secret123", conf.get_http_api_auth_password().c_str());

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_api{auth{password \"complex password\";}}"));
        EXPECT_STREQ("complex password", conf.get_http_api_auth_password().c_str());
    }

    // Test empty password (should return empty string, not default)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_api{auth{password \"\";}}"));
        EXPECT_STREQ("", conf.get_http_api_auth_password().c_str());
    }

    // Test complete auth configuration
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_api{enabled on; auth{enabled on; type basic; username testuser; password testpass;}}"));

        EXPECT_TRUE(conf.get_http_api_enabled());
        EXPECT_TRUE(conf.get_http_api_auth_enabled());
        EXPECT_STREQ("testuser", conf.get_http_api_auth_username().c_str());
        EXPECT_STREQ("testpass", conf.get_http_api_auth_password().c_str());
    }
}

VOID TEST(ConfigHttpsApiTest, CheckHttpsApiListensDefault)
{
    srs_error_t err;

    // Test default value when no configuration is provided
    // Note: HTTPS API defaults to HTTPS server port when HTTP API and HTTP server use same port,
    // otherwise defaults to 1990. Since HTTP API defaults to 1985 and HTTP server to 8080,
    // they're different, so HTTPS API should default to 1990.
    // HTTPS API requires HTTP API to be enabled.
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_api{enabled on;}"));

        vector<string> listens = conf.get_https_api_listens();
        EXPECT_EQ(1, (int)listens.size());
        EXPECT_STREQ("1990", listens.at(0).c_str()); // Default port
    }

    // Test default value when http_api section exists but no https config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_api{enabled on;}"));

        vector<string> listens = conf.get_https_api_listens();
        EXPECT_EQ(1, (int)listens.size());
        EXPECT_STREQ("1990", listens.at(0).c_str()); // Default port
    }

    // Test default value when https section exists but no listen config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_api{enabled on;https{enabled on;}}"));

        vector<string> listens = conf.get_https_api_listens();
        EXPECT_EQ(1, (int)listens.size());
        EXPECT_STREQ("1990", listens.at(0).c_str()); // Default port
    }

    // Test empty listen arguments (should use default)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_api{enabled on;https{listen;}}"));

        vector<string> listens = conf.get_https_api_listens();
        EXPECT_EQ(1, (int)listens.size());
        EXPECT_STREQ("1990", listens.at(0).c_str()); // Default port
    }
}

VOID TEST(ConfigHttpsApiTest, CheckHttpsApiListensCustom)
{
    srs_error_t err;

    // Test single custom listen port
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_api{enabled on;https{listen 8443;}}"));

        vector<string> listens = conf.get_https_api_listens();
        EXPECT_EQ(1, (int)listens.size());
        EXPECT_STREQ("8443", listens.at(0).c_str());
    }

    // Test multiple listen ports
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_api{enabled on;https{listen 8443 9443 10443;}}"));

        vector<string> listens = conf.get_https_api_listens();
        EXPECT_EQ(3, (int)listens.size());
        EXPECT_STREQ("8443", listens.at(0).c_str());
        EXPECT_STREQ("9443", listens.at(1).c_str());
        EXPECT_STREQ("10443", listens.at(2).c_str());
    }

    // Test listen with IP address
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_api{enabled on;https{listen 127.0.0.1:8443;}}"));

        vector<string> listens = conf.get_https_api_listens();
        EXPECT_EQ(1, (int)listens.size());
        EXPECT_STREQ("127.0.0.1:8443", listens.at(0).c_str());
    }
}

VOID TEST(ConfigHttpsApiTest, CheckHttpsApiSslKey)
{
    srs_error_t err;

    // Test default value when no configuration is provided
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_api{enabled on;}"));

        string key = conf.get_https_api_ssl_key();
        EXPECT_STREQ("./conf/server.key", key.c_str()); // Default key file
    }

    // Test default value when http_api section exists but no https config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_api{enabled on;}"));

        string key = conf.get_https_api_ssl_key();
        EXPECT_STREQ("./conf/server.key", key.c_str()); // Default key file
    }

    // Test default value when https section exists but no key config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_api{enabled on;https{enabled on;}}"));

        string key = conf.get_https_api_ssl_key();
        EXPECT_STREQ("./conf/server.key", key.c_str()); // Default key file
    }

    // Test custom key file
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_api{enabled on;https{key /path/to/custom.key;}}"));

        string key = conf.get_https_api_ssl_key();
        EXPECT_STREQ("/path/to/custom.key", key.c_str());
    }

    // Test relative key file path
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_api{enabled on;https{key ./ssl/api.key;}}"));

        string key = conf.get_https_api_ssl_key();
        EXPECT_STREQ("./ssl/api.key", key.c_str());
    }
}

VOID TEST(ConfigHttpsApiTest, CheckHttpsApiSslCert)
{
    srs_error_t err;

    // Test default value when no configuration is provided
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_api{enabled on;}"));

        string cert = conf.get_https_api_ssl_cert();
        EXPECT_STREQ("./conf/server.crt", cert.c_str()); // Default cert file
    }

    // Test default value when http_api section exists but no https config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_api{enabled on;}"));

        string cert = conf.get_https_api_ssl_cert();
        EXPECT_STREQ("./conf/server.crt", cert.c_str()); // Default cert file
    }

    // Test default value when https section exists but no cert config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_api{enabled on;https{enabled on;}}"));

        string cert = conf.get_https_api_ssl_cert();
        EXPECT_STREQ("./conf/server.crt", cert.c_str()); // Default cert file
    }

    // Test custom cert file
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_api{enabled on;https{cert /path/to/custom.crt;}}"));

        string cert = conf.get_https_api_ssl_cert();
        EXPECT_STREQ("/path/to/custom.crt", cert.c_str());
    }

    // Test relative cert file path
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_api{enabled on;https{cert ./ssl/api.crt;}}"));

        string cert = conf.get_https_api_ssl_cert();
        EXPECT_STREQ("./ssl/api.crt", cert.c_str());
    }
}

VOID TEST(ConfigSrtServerTest, CheckSrtEnabled)
{
    srs_error_t err;

    // Test default value when no configuration is provided
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        // Default should be false (SRS_CONF_PREFER_FALSE)
        EXPECT_FALSE(conf.get_srt_enabled());
    }

    // Test default value when srt_server section exists but no enabled config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "srt_server{listen 10080;}"));

        // Should return default value when no enabled config
        EXPECT_FALSE(conf.get_srt_enabled());
    }

    // Test explicit srt enabled
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "srt_server{enabled on;}"));

        EXPECT_TRUE(conf.get_srt_enabled());
    }

    // Test explicit srt disabled
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "srt_server{enabled off;}"));

        EXPECT_FALSE(conf.get_srt_enabled());
    }

    // Test various boolean values (SRS_CONF_PREFER_FALSE: only "on" is true)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "srt_server{enabled true;}"));
        EXPECT_FALSE(conf.get_srt_enabled()); // "true" != "on", so it's false

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "srt_server{enabled yes;}"));
        EXPECT_FALSE(conf.get_srt_enabled()); // "yes" != "on", so it's false
    }
}

VOID TEST(ConfigSrtServerTest, CheckSrtListens)
{
    srs_error_t err;

    // Test default value when no configuration is provided
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        vector<string> listens = conf.get_srt_listens();
        EXPECT_EQ(1, (int)listens.size());
        EXPECT_STREQ("10080", listens.at(0).c_str()); // Default port
    }

    // Test default value when srt_server section exists but no listen config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "srt_server{enabled on;}"));

        vector<string> listens = conf.get_srt_listens();
        EXPECT_EQ(1, (int)listens.size());
        EXPECT_STREQ("10080", listens.at(0).c_str()); // Default port
    }

    // Test empty listen arguments (should use default)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "srt_server{listen;}"));

        vector<string> listens = conf.get_srt_listens();
        EXPECT_EQ(1, (int)listens.size());
        EXPECT_STREQ("10080", listens.at(0).c_str()); // Default port
    }

    // Test single custom listen port
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "srt_server{listen 9000;}"));

        vector<string> listens = conf.get_srt_listens();
        EXPECT_EQ(1, (int)listens.size());
        EXPECT_STREQ("9000", listens.at(0).c_str());
    }

    // Test multiple listen ports
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "srt_server{listen 9000 9001 9002;}"));

        vector<string> listens = conf.get_srt_listens();
        EXPECT_EQ(3, (int)listens.size());
        EXPECT_STREQ("9000", listens.at(0).c_str());
        EXPECT_STREQ("9001", listens.at(1).c_str());
        EXPECT_STREQ("9002", listens.at(2).c_str());
    }
}

VOID TEST(ConfigSrtServerTest, CheckSrtoMaxbw)
{
    srs_error_t err;

    // Test default value when no configuration is provided
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        // Default should be -1
        EXPECT_EQ(-1, conf.get_srto_maxbw());
    }

    // Test default value when srt_server section exists but no maxbw config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "srt_server{enabled on;}"));

        // Should return default value when no maxbw config
        EXPECT_EQ(-1, conf.get_srto_maxbw());
    }

    // Test custom maxbw values
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "srt_server{maxbw 1000000;}"));
        EXPECT_EQ(1000000, conf.get_srto_maxbw());

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "srt_server{maxbw 0;}"));
        EXPECT_EQ(0, conf.get_srto_maxbw());

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "srt_server{maxbw -1;}"));
        EXPECT_EQ(-1, conf.get_srto_maxbw());
    }

    // Test empty maxbw argument (should use default)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "srt_server{maxbw;}"));

        EXPECT_EQ(-1, conf.get_srto_maxbw()); // Default value
    }
}

VOID TEST(ConfigSrtServerTest, CheckSrtoMss)
{
    srs_error_t err;

    // Test default value when no configuration is provided
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        // Default should be 1500
        EXPECT_EQ(1500, conf.get_srto_mss());
    }

    // Test default value when srt_server section exists but no mss config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "srt_server{enabled on;}"));

        // Should return default value when no mss config
        EXPECT_EQ(1500, conf.get_srto_mss());
    }

    // Test custom mss values
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "srt_server{mss 1400;}"));
        EXPECT_EQ(1400, conf.get_srto_mss());

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "srt_server{mss 1200;}"));
        EXPECT_EQ(1200, conf.get_srto_mss());

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "srt_server{mss 576;}"));
        EXPECT_EQ(576, conf.get_srto_mss());
    }

    // Test empty mss argument (should use default)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "srt_server{mss;}"));

        EXPECT_EQ(1500, conf.get_srto_mss()); // Default value
    }
}

VOID TEST(ConfigSrtServerTest, CheckSrtoTsbpdmode)
{
    srs_error_t err;

    // Test default value when no configuration is provided
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        // Default should be true
        EXPECT_TRUE(conf.get_srto_tsbpdmode());
    }

    // Test default value when srt_server section exists but no tsbpdmode config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "srt_server{enabled on;}"));

        // Should return default value when no tsbpdmode config
        EXPECT_TRUE(conf.get_srto_tsbpdmode());
    }

    // Test explicit tsbpdmode enabled
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "srt_server{tsbpdmode on;}"));

        EXPECT_TRUE(conf.get_srto_tsbpdmode());
    }

    // Test explicit tsbpdmode disabled
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "srt_server{tsbpdmode off;}"));

        EXPECT_FALSE(conf.get_srto_tsbpdmode());
    }

    // Test various boolean values (SRS_CONF_PREFER_TRUE: only "off" is false)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "srt_server{tsbpdmode true;}"));
        EXPECT_TRUE(conf.get_srto_tsbpdmode()); // "true" != "off", so it's true

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "srt_server{tsbpdmode yes;}"));
        EXPECT_TRUE(conf.get_srto_tsbpdmode()); // "yes" != "off", so it's true

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "srt_server{tsbpdmode false;}"));
        EXPECT_TRUE(conf.get_srto_tsbpdmode()); // "false" != "off", so it's true
    }

    // Test empty tsbpdmode argument (should use default)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "srt_server{tsbpdmode;}"));

        EXPECT_TRUE(conf.get_srto_tsbpdmode()); // Default value
    }
}

VOID TEST(ConfigSrtServerTest, CheckSrtoLatency)
{
    srs_error_t err;

    // Test default value when no configuration is provided
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        // Default should be 120
        EXPECT_EQ(120, conf.get_srto_latency());
    }

    // Test default value when srt_server section exists but no latency config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "srt_server{enabled on;}"));

        // Should return default value when no latency config
        EXPECT_EQ(120, conf.get_srto_latency());
    }

    // Test custom latency values
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "srt_server{latency 200;}"));
        EXPECT_EQ(200, conf.get_srto_latency());

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "srt_server{latency 50;}"));
        EXPECT_EQ(50, conf.get_srto_latency());

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "srt_server{latency 0;}"));
        EXPECT_EQ(0, conf.get_srto_latency());
    }

    // Test empty latency argument (should use default)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "srt_server{latency;}"));

        EXPECT_EQ(120, conf.get_srto_latency()); // Default value
    }
}

VOID TEST(ConfigSrtServerTest, CheckSrtoRecvLatency)
{
    srs_error_t err;

    // Test default value when no configuration is provided
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        // Default should be 120
        EXPECT_EQ(120, conf.get_srto_recv_latency());
    }

    // Test default value when srt_server section exists but no recvlatency config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "srt_server{enabled on;}"));

        // Should return default value when no recvlatency config
        EXPECT_EQ(120, conf.get_srto_recv_latency());
    }

    // Test custom recvlatency values
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "srt_server{recvlatency 300;}"));
        EXPECT_EQ(300, conf.get_srto_recv_latency());

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "srt_server{recvlatency 100;}"));
        EXPECT_EQ(100, conf.get_srto_recv_latency());

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "srt_server{recvlatency 0;}"));
        EXPECT_EQ(0, conf.get_srto_recv_latency());
    }

    // Test empty recvlatency argument (should use default)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "srt_server{recvlatency;}"));

        EXPECT_EQ(120, conf.get_srto_recv_latency()); // Default value
    }
}

VOID TEST(ConfigSrtServerTest, CheckSrtoPeerLatency)
{
    srs_error_t err;

    // Test default value when no configuration is provided
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        // Default should be 0
        EXPECT_EQ(0, conf.get_srto_peer_latency());
    }

    // Test default value when srt_server section exists but no peerlatency config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "srt_server{enabled on;}"));

        // Should return default value when no peerlatency config
        EXPECT_EQ(0, conf.get_srto_peer_latency());
    }

    // Test custom peerlatency values
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "srt_server{peerlatency 150;}"));
        EXPECT_EQ(150, conf.get_srto_peer_latency());

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "srt_server{peerlatency 200;}"));
        EXPECT_EQ(200, conf.get_srto_peer_latency());

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "srt_server{peerlatency 0;}"));
        EXPECT_EQ(0, conf.get_srto_peer_latency());
    }

    // Test empty peerlatency argument (should use default)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "srt_server{peerlatency;}"));

        EXPECT_EQ(0, conf.get_srto_peer_latency()); // Default value
    }
}

VOID TEST(ConfigSrtServerTest, CheckSrtSeiFilter)
{
    srs_error_t err;

    // Test default value when no configuration is provided
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        // Default should be true
        EXPECT_TRUE(conf.get_srt_sei_filter());
    }

    // Test default value when srt_server section exists but no sei_filter config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "srt_server{enabled on;}"));

        // Should return default value when no sei_filter config
        EXPECT_TRUE(conf.get_srt_sei_filter());
    }

    // Test explicit sei_filter enabled
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "srt_server{sei_filter on;}"));

        EXPECT_TRUE(conf.get_srt_sei_filter());
    }

    // Test explicit sei_filter disabled
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "srt_server{sei_filter off;}"));

        EXPECT_FALSE(conf.get_srt_sei_filter());
    }

    // Test various boolean values (SRS_CONF_PREFER_TRUE: only "off" is false)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "srt_server{sei_filter true;}"));
        EXPECT_TRUE(conf.get_srt_sei_filter()); // "true" != "off", so it's true

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "srt_server{sei_filter yes;}"));
        EXPECT_TRUE(conf.get_srt_sei_filter()); // "yes" != "off", so it's true

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "srt_server{sei_filter false;}"));
        EXPECT_TRUE(conf.get_srt_sei_filter()); // "false" != "off", so it's true
    }

    // Test empty sei_filter argument (should use default)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "srt_server{sei_filter;}"));

        EXPECT_TRUE(conf.get_srt_sei_filter()); // Default value
    }
}

VOID TEST(ConfigSrtServerTest, CheckSrtSeiFilterEnvironment)
{
    srs_error_t err;

    // Test environment variable override
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "srt_server{sei_filter on;}"));

        SrsSetEnvConfig(conf, sei_filter, "SRS_SRT_SERVER_SEI_FILTER", "off");

        // Environment variable should override config file
        EXPECT_FALSE(conf.get_srt_sei_filter());
    }

    // Test environment variable with "on" value
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "srt_server{sei_filter off;}"));

        SrsSetEnvConfig(conf, sei_filter, "SRS_SRT_SERVER_SEI_FILTER", "on");

        // Environment variable should override config file
        EXPECT_TRUE(conf.get_srt_sei_filter());
    }

    // Test environment variable with default when no config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        SrsSetEnvConfig(conf, sei_filter, "SRS_SRT_SERVER_SEI_FILTER", "off");

        // Environment variable should override default
        EXPECT_FALSE(conf.get_srt_sei_filter());
    }
}

VOID TEST(ConfigSrtServerTest, CheckSrtoTlpktdrop)
{
    srs_error_t err;

    // Test default value when no configuration is provided
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        // Default should be true
        EXPECT_TRUE(conf.get_srto_tlpktdrop());
    }

    // Test default value when srt_server section exists but no tlpkdrop config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "srt_server{enabled on;}"));

        // Should return default value when no tlpkdrop config
        EXPECT_TRUE(conf.get_srto_tlpktdrop());
    }

    // Test explicit tlpkdrop enabled
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "srt_server{tlpkdrop on;}"));

        EXPECT_TRUE(conf.get_srto_tlpktdrop());
    }

    // Test explicit tlpkdrop disabled
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "srt_server{tlpkdrop off;}"));

        EXPECT_FALSE(conf.get_srto_tlpktdrop());
    }

    // Test various boolean values (SRS_CONF_PREFER_TRUE: only "off" is false)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "srt_server{tlpkdrop true;}"));
        EXPECT_TRUE(conf.get_srto_tlpktdrop()); // "true" != "off", so it's true

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "srt_server{tlpkdrop yes;}"));
        EXPECT_TRUE(conf.get_srto_tlpktdrop()); // "yes" != "off", so it's true

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "srt_server{tlpkdrop false;}"));
        EXPECT_TRUE(conf.get_srto_tlpktdrop()); // "false" != "off", so it's true
    }

    // Test empty tlpkdrop argument (should use default)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "srt_server{tlpkdrop;}"));

        EXPECT_TRUE(conf.get_srto_tlpktdrop()); // Default value
    }
}

VOID TEST(ConfigSrtServerTest, CheckSrtoConntimeout)
{
    srs_error_t err;

    // Test default value when no configuration is provided
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        // Default should be 3 seconds
        EXPECT_EQ(3 * SRS_UTIME_SECONDS, conf.get_srto_conntimeout());
    }

    // Test default value when srt_server section exists but no connect_timeout config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "srt_server{enabled on;}"));

        // Should return default value when no connect_timeout config
        EXPECT_EQ(3 * SRS_UTIME_SECONDS, conf.get_srto_conntimeout());
    }

    // Test custom connect_timeout values (in milliseconds)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "srt_server{connect_timeout 5000;}"));
        EXPECT_EQ(5000 * SRS_UTIME_MILLISECONDS, conf.get_srto_conntimeout());

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "srt_server{connect_timeout 1000;}"));
        EXPECT_EQ(1000 * SRS_UTIME_MILLISECONDS, conf.get_srto_conntimeout());

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "srt_server{connect_timeout 10000;}"));
        EXPECT_EQ(10000 * SRS_UTIME_MILLISECONDS, conf.get_srto_conntimeout());
    }

    // Test empty connect_timeout argument (should use default)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "srt_server{connect_timeout;}"));

        EXPECT_EQ(3 * SRS_UTIME_SECONDS, conf.get_srto_conntimeout()); // Default value
    }
}

VOID TEST(ConfigSrtServerTest, CheckSrtoPeeridletimeout)
{
    srs_error_t err;

    // Test default value when no configuration is provided
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        // Default should be 10 seconds
        EXPECT_EQ(10 * SRS_UTIME_SECONDS, conf.get_srto_peeridletimeout());
    }

    // Test default value when srt_server section exists but no peer_idle_timeout config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "srt_server{enabled on;}"));

        // Should return default value when no peer_idle_timeout config
        EXPECT_EQ(10 * SRS_UTIME_SECONDS, conf.get_srto_peeridletimeout());
    }

    // Test custom peer_idle_timeout values (in milliseconds)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "srt_server{peer_idle_timeout 15000;}"));
        EXPECT_EQ(15000 * SRS_UTIME_MILLISECONDS, conf.get_srto_peeridletimeout());

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "srt_server{peer_idle_timeout 5000;}"));
        EXPECT_EQ(5000 * SRS_UTIME_MILLISECONDS, conf.get_srto_peeridletimeout());

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "srt_server{peer_idle_timeout 30000;}"));
        EXPECT_EQ(30000 * SRS_UTIME_MILLISECONDS, conf.get_srto_peeridletimeout());
    }

    // Test empty peer_idle_timeout argument (should use default)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "srt_server{peer_idle_timeout;}"));

        EXPECT_EQ(10 * SRS_UTIME_SECONDS, conf.get_srto_peeridletimeout()); // Default value
    }
}

VOID TEST(ConfigSrtServerTest, CheckSrtoSendbuf)
{
    srs_error_t err;

    // Test default value when no configuration is provided
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        // Default should be 8192 * (1500 - 28) = 8192 * 1472 = 12058624
        int expected_default = 8192 * (1500 - 28);
        EXPECT_EQ(expected_default, conf.get_srto_sendbuf());
    }

    // Test default value when srt_server section exists but no sendbuf config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "srt_server{enabled on;}"));

        // Should return default value when no sendbuf config
        int expected_default = 8192 * (1500 - 28);
        EXPECT_EQ(expected_default, conf.get_srto_sendbuf());
    }

    // Test custom sendbuf values
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "srt_server{sendbuf 1048576;}"));
        EXPECT_EQ(1048576, conf.get_srto_sendbuf());

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "srt_server{sendbuf 2097152;}"));
        EXPECT_EQ(2097152, conf.get_srto_sendbuf());

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "srt_server{sendbuf 65536;}"));
        EXPECT_EQ(65536, conf.get_srto_sendbuf());
    }

    // Test empty sendbuf argument (should use default)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "srt_server{sendbuf;}"));

        int expected_default = 8192 * (1500 - 28);
        EXPECT_EQ(expected_default, conf.get_srto_sendbuf()); // Default value
    }
}

VOID TEST(ConfigSrtServerTest, CheckSrtoRecvbuf)
{
    srs_error_t err;

    // Test default value when no configuration is provided
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        // Default should be 8192 * (1500 - 28) = 8192 * 1472 = 12058624
        int expected_default = 8192 * (1500 - 28);
        EXPECT_EQ(expected_default, conf.get_srto_recvbuf());
    }

    // Test default value when srt_server section exists but no recvbuf config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "srt_server{enabled on;}"));

        // Should return default value when no recvbuf config
        int expected_default = 8192 * (1500 - 28);
        EXPECT_EQ(expected_default, conf.get_srto_recvbuf());
    }

    // Test custom recvbuf values
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "srt_server{recvbuf 1048576;}"));
        EXPECT_EQ(1048576, conf.get_srto_recvbuf());

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "srt_server{recvbuf 2097152;}"));
        EXPECT_EQ(2097152, conf.get_srto_recvbuf());

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "srt_server{recvbuf 65536;}"));
        EXPECT_EQ(65536, conf.get_srto_recvbuf());
    }

    // Test empty recvbuf argument (should use default)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "srt_server{recvbuf;}"));

        int expected_default = 8192 * (1500 - 28);
        EXPECT_EQ(expected_default, conf.get_srto_recvbuf()); // Default value
    }
}

VOID TEST(ConfigSrtServerTest, CheckSrtoPayloadsize)
{
    srs_error_t err;

    // Test default value when no configuration is provided
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        // Default should be 1316
        EXPECT_EQ(1316, conf.get_srto_payloadsize());
    }

    // Test default value when srt_server section exists but no payloadsize config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "srt_server{enabled on;}"));

        // Should return default value when no payloadsize config
        EXPECT_EQ(1316, conf.get_srto_payloadsize());
    }

    // Test custom payloadsize values
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "srt_server{payloadsize 1200;}"));
        EXPECT_EQ(1200, conf.get_srto_payloadsize());

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "srt_server{payloadsize 1400;}"));
        EXPECT_EQ(1400, conf.get_srto_payloadsize());

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "srt_server{payloadsize 1000;}"));
        EXPECT_EQ(1000, conf.get_srto_payloadsize());
    }

    // Test empty payloadsize argument (should use default)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "srt_server{payloadsize;}"));

        EXPECT_EQ(1316, conf.get_srto_payloadsize()); // Default value
    }
}

VOID TEST(ConfigSrtServerTest, CheckSrtoPayloadsizeEnvironment)
{
    srs_error_t err;

    // Test environment variable override
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "srt_server{payloadsize 1200;}"));

        SrsSetEnvConfig(conf, payloadsize, "SRS_SRT_SERVER_PAYLOADSIZE", "1400");

        // Environment variable should override config file
        EXPECT_EQ(1400, conf.get_srto_payloadsize());
    }

    // Test environment variable with default when no config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        SrsSetEnvConfig(conf, payloadsize, "SRS_SRT_SERVER_PAYLOADSIZE", "1500");

        // Environment variable should override default
        EXPECT_EQ(1500, conf.get_srto_payloadsize());
    }

    // Test environment variable with zero value
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        SrsSetEnvConfig(conf, payloadsize, "SRS_SRT_SERVER_PAYLOADSIZE", "0");

        // Environment variable should override default
        EXPECT_EQ(0, conf.get_srto_payloadsize());
    }
}

VOID TEST(ConfigSrtServerTest, CheckSrtoPassphrase)
{
    srs_error_t err;

    // Test default value when no configuration is provided
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        // Default should be empty string
        string passphrase = conf.get_srto_passphrase();
        EXPECT_STREQ("", passphrase.c_str());
    }

    // Test default value when srt_server section exists but no passphrase config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "srt_server{enabled on;}"));

        // Should return default value when no passphrase config
        string passphrase = conf.get_srto_passphrase();
        EXPECT_STREQ("", passphrase.c_str());
    }

    // Test custom passphrase values
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "srt_server{passphrase mysecretkey;}"));
        string passphrase = conf.get_srto_passphrase();
        EXPECT_STREQ("mysecretkey", passphrase.c_str());

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "srt_server{passphrase \"complex password with spaces\";}"));
        passphrase = conf.get_srto_passphrase();
        EXPECT_STREQ("complex password with spaces", passphrase.c_str());

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "srt_server{passphrase 123456789;}"));
        passphrase = conf.get_srto_passphrase();
        EXPECT_STREQ("123456789", passphrase.c_str());
    }

    // Test empty passphrase argument (should use default)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "srt_server{passphrase;}"));

        string passphrase = conf.get_srto_passphrase();
        EXPECT_STREQ("", passphrase.c_str()); // Default value
    }
}

VOID TEST(ConfigSrtServerTest, CheckSrtoPbkeylen)
{
    srs_error_t err;

    // Test default value when no configuration is provided
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        // Default should be 0
        EXPECT_EQ(0, conf.get_srto_pbkeylen());
    }

    // Test default value when srt_server section exists but no pbkeylen config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "srt_server{enabled on;}"));

        // Should return default value when no pbkeylen config
        EXPECT_EQ(0, conf.get_srto_pbkeylen());
    }

    // Test custom pbkeylen values
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "srt_server{pbkeylen 16;}"));
        EXPECT_EQ(16, conf.get_srto_pbkeylen());

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "srt_server{pbkeylen 24;}"));
        EXPECT_EQ(24, conf.get_srto_pbkeylen());

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "srt_server{pbkeylen 32;}"));
        EXPECT_EQ(32, conf.get_srto_pbkeylen());
    }

    // Test empty pbkeylen argument (should use default)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "srt_server{pbkeylen;}"));

        EXPECT_EQ(0, conf.get_srto_pbkeylen()); // Default value
    }
}

VOID TEST(ConfigSrtServerTest, CheckDefaultAppName)
{
    srs_error_t err;

    // Test default value when no configuration is provided
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        // Default should be "live"
        string app_name = conf.get_default_app_name();
        EXPECT_STREQ("live", app_name.c_str());
    }

    // Test default value when srt_server section exists but no default_app config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "srt_server{enabled on;}"));

        // Should return default value when no default_app config
        string app_name = conf.get_default_app_name();
        EXPECT_STREQ("live", app_name.c_str());
    }

    // Test custom default_app values
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "srt_server{default_app myapp;}"));
        string app_name = conf.get_default_app_name();
        EXPECT_STREQ("myapp", app_name.c_str());

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "srt_server{default_app streaming;}"));
        app_name = conf.get_default_app_name();
        EXPECT_STREQ("streaming", app_name.c_str());

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "srt_server{default_app broadcast;}"));
        app_name = conf.get_default_app_name();
        EXPECT_STREQ("broadcast", app_name.c_str());
    }

    // Test empty default_app argument (should use default)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "srt_server{default_app;}"));

        string app_name = conf.get_default_app_name();
        EXPECT_STREQ("live", app_name.c_str()); // Default value
    }
}

VOID TEST(ConfigSrtVhostTest, CheckSrtEnabled)
{
    srs_error_t err;

    // Test default value when no configuration is provided
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        // Default should be false (SRS_CONF_PREFER_FALSE)
        EXPECT_FALSE(conf.get_srt_enabled("__defaultVhost__"));
        EXPECT_FALSE(conf.get_srt_enabled("test.com"));
    }

    // Test default value when vhost exists but no srt section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{enabled on;}}"));

        // Should return default value when no srt section
        EXPECT_FALSE(conf.get_srt_enabled("test.com"));
    }

    // Test default value when srt section exists but no enabled config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{srt{srt_to_rtmp on;}}"));

        // Should return default value when no enabled config
        EXPECT_FALSE(conf.get_srt_enabled("test.com"));
    }

    // Test explicit srt enabled
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{srt{enabled on;}}"));

        EXPECT_TRUE(conf.get_srt_enabled("test.com"));
    }

    // Test explicit srt disabled
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{srt{enabled off;}}"));

        EXPECT_FALSE(conf.get_srt_enabled("test.com"));
    }

    // Test various boolean values (SRS_CONF_PREFER_FALSE: only "on" is true)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{srt{enabled true;}}"));
        EXPECT_FALSE(conf.get_srt_enabled("test.com")); // "true" != "on", so it's false

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{srt{enabled yes;}}"));
        EXPECT_FALSE(conf.get_srt_enabled("test.com")); // "yes" != "on", so it's false
    }

    // Test empty enabled argument (should use default)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{srt{enabled;}}"));

        EXPECT_FALSE(conf.get_srt_enabled("test.com")); // Default value
    }
}

VOID TEST(ConfigSrtVhostTest, CheckSrtToRtmp)
{
    srs_error_t err;

    // Test default value when no configuration is provided
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        // Default should be false (off)
        EXPECT_FALSE(conf.get_srt_to_rtmp("__defaultVhost__"));
        EXPECT_FALSE(conf.get_srt_to_rtmp("test.com"));
    }

    // Test default value when vhost exists but no srt section
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{hls{enabled on;}}"));

        // Should return default value when no srt section
        EXPECT_FALSE(conf.get_srt_to_rtmp("test.com"));
    }

    // Test default value when srt section exists but no srt_to_rtmp config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{srt{enabled on;}}"));

        // Should return default value when no srt_to_rtmp config
        EXPECT_FALSE(conf.get_srt_to_rtmp("test.com"));
    }

    // Test explicit srt_to_rtmp enabled
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{srt{srt_to_rtmp on;}}"));

        EXPECT_TRUE(conf.get_srt_to_rtmp("test.com"));
    }

    // Test explicit srt_to_rtmp disabled
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{srt{srt_to_rtmp off;}}"));

        EXPECT_FALSE(conf.get_srt_to_rtmp("test.com"));
    }

    // Test various boolean values (SRS_CONF_PREFER_FALSE: only "on" is true)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{srt{srt_to_rtmp true;}}"));
        EXPECT_FALSE(conf.get_srt_to_rtmp("test.com")); // "true" != "on", so it's false

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{srt{srt_to_rtmp yes;}}"));
        EXPECT_FALSE(conf.get_srt_to_rtmp("test.com")); // "yes" != "on", so it's false
    }

    // Test empty srt_to_rtmp argument (should use default)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{srt{srt_to_rtmp;}}"));

        EXPECT_FALSE(conf.get_srt_to_rtmp("test.com")); // Default value is false (off)
    }
}

VOID TEST(ConfigSrtVhostTest, CheckSrtToRtmpEnvironment)
{
    srs_error_t err;

    // Test environment variable override
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{srt{srt_to_rtmp on;}}"));

        SrsSetEnvConfig(conf, srt_to_rtmp, "SRS_VHOST_SRT_TO_RTMP", "off");

        // Environment variable should override config file
        EXPECT_FALSE(conf.get_srt_to_rtmp("test.com"));
    }

    // Test environment variable with "on" value
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{srt{srt_to_rtmp off;}}"));

        SrsSetEnvConfig(conf, srt_to_rtmp, "SRS_VHOST_SRT_TO_RTMP", "on");

        // Environment variable should override config file
        EXPECT_TRUE(conf.get_srt_to_rtmp("test.com"));
    }

    // Test environment variable with default when no config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        SrsSetEnvConfig(conf, srt_to_rtmp, "SRS_VHOST_SRT_TO_RTMP", "off");

        // Environment variable should override default
        EXPECT_FALSE(conf.get_srt_to_rtmp("__defaultVhost__"));
    }

    // Test environment variable affects all vhosts
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF
                                              "vhost vhost1.com{srt{srt_to_rtmp on;}} "
                                              "vhost vhost2.com{srt{srt_to_rtmp off;}}"));

        SrsSetEnvConfig(conf, srt_to_rtmp, "SRS_VHOST_SRT_TO_RTMP", "off");

        // Environment variable should override all vhost configs
        EXPECT_FALSE(conf.get_srt_to_rtmp("vhost1.com"));
        EXPECT_FALSE(conf.get_srt_to_rtmp("vhost2.com"));
        EXPECT_FALSE(conf.get_srt_to_rtmp("__defaultVhost__"));
    }
}

VOID TEST(ConfigHttpStreamTest, CheckHttpStreamEnabled)
{
    srs_error_t err;

    // Test default value when no configuration is provided
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        // Default should be false (SRS_CONF_PREFER_FALSE)
        EXPECT_FALSE(conf.get_http_stream_enabled());
    }

    // Test default value when http_server section exists but no enabled config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_server{listen 8080;}"));

        // Should return default value when no enabled config
        EXPECT_FALSE(conf.get_http_stream_enabled());
    }

    // Test explicit http_server enabled
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_server{enabled on;}"));

        EXPECT_TRUE(conf.get_http_stream_enabled());
    }

    // Test explicit http_server disabled
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_server{enabled off;}"));

        EXPECT_FALSE(conf.get_http_stream_enabled());
    }

    // Test various boolean values (SRS_CONF_PREFER_FALSE: only "on" is true)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_server{enabled true;}"));
        EXPECT_FALSE(conf.get_http_stream_enabled()); // "true" != "on", so it's false

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_server{enabled yes;}"));
        EXPECT_FALSE(conf.get_http_stream_enabled()); // "yes" != "on", so it's false
    }

    // Test empty enabled argument (should use default)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_server{enabled;}"));

        EXPECT_FALSE(conf.get_http_stream_enabled()); // Default value
    }
}

VOID TEST(ConfigHttpStreamTest, CheckHttpStreamListens)
{
    srs_error_t err;

    // Test default value when no configuration is provided
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        vector<string> listens = conf.get_http_stream_listens();
        EXPECT_EQ(1, (int)listens.size());
        EXPECT_STREQ("8080", listens.at(0).c_str()); // Default port
    }

    // Test default value when http_server section exists but no listen config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_server{enabled on;}"));

        vector<string> listens = conf.get_http_stream_listens();
        EXPECT_EQ(1, (int)listens.size());
        EXPECT_STREQ("8080", listens.at(0).c_str()); // Default port
    }

    // Test empty listen arguments (should use default)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_server{listen;}"));

        vector<string> listens = conf.get_http_stream_listens();
        EXPECT_EQ(1, (int)listens.size());
        EXPECT_STREQ("8080", listens.at(0).c_str()); // Default port
    }

    // Test single custom listen port
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_server{listen 9080;}"));

        vector<string> listens = conf.get_http_stream_listens();
        EXPECT_EQ(1, (int)listens.size());
        EXPECT_STREQ("9080", listens.at(0).c_str());
    }

    // Test multiple listen ports
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_server{listen 8080 8081 8082;}"));

        vector<string> listens = conf.get_http_stream_listens();
        EXPECT_EQ(3, (int)listens.size());
        EXPECT_STREQ("8080", listens.at(0).c_str());
        EXPECT_STREQ("8081", listens.at(1).c_str());
        EXPECT_STREQ("8082", listens.at(2).c_str());
    }

    // Test listen with IP address
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_server{listen 127.0.0.1:8080;}"));

        vector<string> listens = conf.get_http_stream_listens();
        EXPECT_EQ(1, (int)listens.size());
        EXPECT_STREQ("127.0.0.1:8080", listens.at(0).c_str());
    }
}

VOID TEST(ConfigHttpStreamTest, CheckHttpStreamDir)
{
    srs_error_t err;

    // Test default value when no configuration is provided
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));

        string dir = conf.get_http_stream_dir();
        EXPECT_STREQ("./objs/nginx/html", dir.c_str()); // Default directory
    }

    // Test default value when http_server section exists but no dir config
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_server{enabled on;}"));

        string dir = conf.get_http_stream_dir();
        EXPECT_STREQ("./objs/nginx/html", dir.c_str()); // Default directory
    }

    // Test custom dir values
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_server{dir /var/www/html;}"));
        string dir = conf.get_http_stream_dir();
        EXPECT_STREQ("/var/www/html", dir.c_str());

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_server{dir ./www;}"));
        dir = conf.get_http_stream_dir();
        EXPECT_STREQ("./www", dir.c_str());

        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_server{dir /home/user/public_html;}"));
        dir = conf.get_http_stream_dir();
        EXPECT_STREQ("/home/user/public_html", dir.c_str());
    }

    // Test empty dir argument (should use default)
    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "http_server{dir;}"));

        string dir = conf.get_http_stream_dir();
        EXPECT_STREQ("./objs/nginx/html", dir.c_str()); // Default directory
    }
}
