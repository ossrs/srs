//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//
#include <srs_utest_core.hpp>

using namespace std;

#include <srs_core_autofree.hpp>

VOID TEST(CoreAutoFreeTest, Free)
{
    char* data = new char[32];
    srs_freepa(data);
    EXPECT_TRUE(data == NULL);

    if (true) {
        data = new char[32];
        SrsAutoFreeA(char, data);
    }
    EXPECT_TRUE(data == NULL);
}

VOID TEST(CoreMacroseTest, Check)
{
#ifndef SRS_BUILD_TS
    EXPECT_TRUE(false);
#endif
#ifndef SRS_BUILD_DATE
    EXPECT_TRUE(false);
#endif
#ifndef SRS_UNAME
    EXPECT_TRUE(false);
#endif
#ifndef SRS_USER_CONFIGURE
    EXPECT_TRUE(false);
#endif
#ifndef SRS_CONFIGURE
    EXPECT_TRUE(false);
#endif
#ifndef SRS_PREFIX
    EXPECT_TRUE(false);
#endif
#ifndef SRS_CONSTRIBUTORS
    EXPECT_TRUE(false);
#endif
}

VOID TEST(CoreLogger, CheckVsnprintf)
{
    if (true) {
        char buf[1024];
        HELPER_ARRAY_INIT(buf, sizeof(buf), 0xf);

        // Return the number of characters printed.
        EXPECT_EQ(6, snprintf(buf, sizeof(buf), "%s", "Hello!"));
        EXPECT_EQ('H', buf[0]);
        EXPECT_EQ('!', buf[5]);
        EXPECT_EQ(0x0, buf[6]);
        EXPECT_EQ(0xf, buf[7]);
    }

    if (true) {
        char buf[1024];
        HELPER_ARRAY_INIT(buf, sizeof(buf), 0xf);

        // Return the number of characters that would have been printed if the size were unlimited.
        EXPECT_EQ(6, snprintf(buf, 3, "%s", "Hello!"));
        EXPECT_EQ('H', buf[0]);
        EXPECT_EQ('e', buf[1]);
        EXPECT_EQ(0, buf[2]);
        EXPECT_EQ(0xf, buf[3]);
    }

    if (true) {
        char buf[5];
        EXPECT_EQ(4, snprintf(buf, sizeof(buf), "Hell"));
        EXPECT_STREQ("Hell", buf);

        EXPECT_EQ(5, snprintf(buf, sizeof(buf), "Hello"));
        EXPECT_STREQ("Hell", buf);

        EXPECT_EQ(10, snprintf(buf, sizeof(buf), "HelloWorld"));
        EXPECT_STREQ("Hell", buf);
    }
}

