/*
The MIT License (MIT)

Copyright (c) 2013-2014 winlin

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
    srs_freep(data);
    EXPECT_TRUE(data == NULL);

    if (true) {
        data = new char[32];
        SrsAutoFree(char, data);
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
#ifndef SRS_AUTO_EMBEDED_TOOL_CHAIN
    EXPECT_TRUE(false);
#endif
#ifndef SRS_AUTO_PREFIX
    EXPECT_TRUE(false);
#endif
#ifndef SRS_AUTO_CONSTRIBUTORS
    EXPECT_TRUE(false);
#endif
}
