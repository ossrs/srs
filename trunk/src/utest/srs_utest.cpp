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

#include <srs_utest.hpp>

#include <srs_kernel_log.hpp>
#include <srs_kernel_error.hpp>
#include <srs_app_server.hpp>
#include <srs_app_config.hpp>
#include <srs_app_log.hpp>

// kernel module.
ISrsLog* _srs_log = new ISrsLog();
ISrsThreadContext* _srs_context = new ISrsThreadContext();
// app module.
SrsConfig* _srs_config = NULL;
SrsServer* _srs_server = NULL;

MockEmptyIO::MockEmptyIO()
{
}

MockEmptyIO::~MockEmptyIO()
{
}

bool MockEmptyIO::is_never_timeout(int64_t /*timeout_us*/)
{
    return true;
}

int MockEmptyIO::read_fully(void* /*buf*/, size_t /*size*/, ssize_t* /*nread*/)
{
    return ERROR_SUCCESS;
}

int MockEmptyIO::write(void* /*buf*/, size_t /*size*/, ssize_t* /*nwrite*/)
{
    return ERROR_SUCCESS;
}

void MockEmptyIO::set_recv_timeout(int64_t /*timeout_us*/)
{
}

int64_t MockEmptyIO::get_recv_timeout()
{
    return -1;
}

int64_t MockEmptyIO::get_recv_bytes()
{
    return -1;
}

void MockEmptyIO::set_send_timeout(int64_t /*timeout_us*/)
{
}

int64_t MockEmptyIO::get_send_timeout()
{
    return 0;
}

int64_t MockEmptyIO::get_send_bytes()
{
    return 0;
}

int MockEmptyIO::writev(const iovec */*iov*/, int /*iov_size*/, ssize_t* /*nwrite*/)
{
    return ERROR_SUCCESS;
}

int MockEmptyIO::read(void* /*buf*/, size_t /*size*/, ssize_t* /*nread*/)
{
    return ERROR_SUCCESS;
}

// basic test and samples.
VOID TEST(SampleTest, FastSampleInt64Test) 
{
    EXPECT_EQ(1, (int)sizeof(int8_t));
    EXPECT_EQ(2, (int)sizeof(int16_t));
    EXPECT_EQ(4, (int)sizeof(int32_t));
    EXPECT_EQ(8, (int)sizeof(int64_t));
}

VOID TEST(SampleTest, FastSampleMacrosTest) 
{
    EXPECT_TRUE(1);
    EXPECT_FALSE(0);
    
    EXPECT_EQ(1, 1); // ==
    EXPECT_NE(1, 2); // !=
    EXPECT_LE(1, 2); // <=
    EXPECT_LT(1, 2); // <
    EXPECT_GE(2, 1); // >=
    EXPECT_GT(2, 1); // >

    EXPECT_STREQ("winlin", "winlin");
    EXPECT_STRNE("winlin", "srs");
    EXPECT_STRCASEEQ("winlin", "Winlin");
    EXPECT_STRCASENE("winlin", "srs");
    
    EXPECT_FLOAT_EQ(1.0, 1.000000000000001);
    EXPECT_DOUBLE_EQ(1.0, 1.0000000000000001);
    EXPECT_NEAR(10, 15, 5);
}
