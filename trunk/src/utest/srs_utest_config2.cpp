//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//
#include <srs_utest_config2.hpp>

#include <srs_kernel_error.hpp>
#include <srs_kernel_file.hpp>
#include <srs_utest_kernel.hpp>

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

