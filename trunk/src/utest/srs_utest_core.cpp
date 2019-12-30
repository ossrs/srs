/*
The MIT License (MIT)

Copyright (c) 2013-2020 Winlin

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/
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
#ifndef SRS_AUTO_BUILD_TS
    EXPECT_TRUE(false);
#endif
#ifndef SRS_AUTO_BUILD_DATE
    EXPECT_TRUE(false);
#endif
#ifndef SRS_AUTO_UNAME
    EXPECT_TRUE(false);
#endif
#ifndef SRS_AUTO_USER_CONFIGURE
    EXPECT_TRUE(false);
#endif
#ifndef SRS_AUTO_CONFIGURE
    EXPECT_TRUE(false);
#endif
#ifndef SRS_AUTO_PREFIX
    EXPECT_TRUE(false);
#endif
#ifndef SRS_AUTO_CONSTRIBUTORS
    EXPECT_TRUE(false);
#endif
}

VOID TEST(CoreLogger, CheckVsnprintf)
{
    if (true) {
        char buf[1024];
        HELPER_ARRAY_INIT(buf, sizeof(buf), 0xf);

        // Return the number of characters printed.
        EXPECT_EQ(6, sprintf(buf, "%s", "Hello!"));
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
}

