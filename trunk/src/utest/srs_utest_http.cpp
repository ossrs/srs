/*
The MIT License (MIT)

Copyright (c) 2013-2019 Winlin

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
#include <srs_utest_http.hpp>

#include <srs_http_stack.hpp>

VOID TEST(ProtocolHTTPTest, StatusCode2Text)
{
    EXPECT_STREQ(SRS_CONSTS_HTTP_OK_str, srs_generate_http_status_text(SRS_CONSTS_HTTP_OK).c_str());
    EXPECT_STREQ("Status Unknown", srs_generate_http_status_text(999).c_str());

    EXPECT_FALSE(srs_go_http_body_allowd(SRS_CONSTS_HTTP_Continue));
    EXPECT_FALSE(srs_go_http_body_allowd(SRS_CONSTS_HTTP_OK-1));
    EXPECT_FALSE(srs_go_http_body_allowd(SRS_CONSTS_HTTP_NoContent));
    EXPECT_FALSE(srs_go_http_body_allowd(SRS_CONSTS_HTTP_NotModified));
    EXPECT_TRUE(srs_go_http_body_allowd(SRS_CONSTS_HTTP_OK));
}

VOID TEST(ProtocolHTTPTest, ResponseDetect)
{
    EXPECT_STREQ("application/octet-stream", srs_go_http_detect(NULL, 0).c_str());
}
