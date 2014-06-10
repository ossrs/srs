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
#include <srs_utest_buffer.hpp>

#include <srs_kernel_error.hpp>
#include <srs_kernel_utility.hpp>

MockBufferReader::MockBufferReader(const char* data)
{
    str = data;
}

MockBufferReader::~MockBufferReader()
{
}

int MockBufferReader::read(void* buf, size_t size, ssize_t* nread)
{
    int len = srs_min(str.length(), size);

    memcpy(buf, str.data(), len);
    
    if (nread) {
        *nread = len;
    }

    return ERROR_SUCCESS;
}

VOID TEST(BufferTest, DefaultObject)
{
    SrsBuffer b;
    
    EXPECT_EQ(0, b.length());
    EXPECT_EQ(NULL, b.bytes());
}

VOID TEST(BufferTest, AppendBytes)
{
    SrsBuffer b;
    
    char winlin[] = "winlin";
    b.append(winlin, strlen(winlin));
    EXPECT_EQ((int)strlen(winlin), b.length());
    ASSERT_TRUE(NULL != b.bytes());
    EXPECT_EQ('w', b.bytes()[0]);
    EXPECT_EQ('n', b.bytes()[5]);

    b.append(winlin, strlen(winlin));
    EXPECT_EQ(2 * (int)strlen(winlin), b.length());
    ASSERT_TRUE(NULL != b.bytes());
    EXPECT_EQ('w', b.bytes()[0]);
    EXPECT_EQ('n', b.bytes()[5]);
    EXPECT_EQ('w', b.bytes()[6]);
    EXPECT_EQ('n', b.bytes()[11]);
}

VOID TEST(BufferTest, EraseBytes)
{
    SrsBuffer b;
    
    b.erase(0);
    b.erase(-1);
    EXPECT_EQ(0, b.length());
    
    char winlin[] = "winlin";
    b.append(winlin, strlen(winlin));
    b.erase(b.length());
    EXPECT_EQ(0, b.length());
    
    b.erase(0);
    b.erase(-1);
    EXPECT_EQ(0, b.length());
    
    b.append(winlin, strlen(winlin));
    b.erase(1);
    EXPECT_EQ(5, b.length());
    EXPECT_EQ('i', b.bytes()[0]);
    EXPECT_EQ('n', b.bytes()[4]);
    
    b.erase(2);
    EXPECT_EQ(3, b.length());
    EXPECT_EQ('l', b.bytes()[0]);
    EXPECT_EQ('n', b.bytes()[2]);
    
    b.erase(0);
    b.erase(-1);
    EXPECT_EQ(3, b.length());
    
    b.erase(3);
    EXPECT_EQ(0, b.length());
}

VOID TEST(BufferTest, Grow)
{
    SrsBuffer b;
    MockBufferReader r("winlin");
    
    b.grow(&r, 1);
    EXPECT_EQ(6, b.length());
    EXPECT_EQ('w', b.bytes()[0]);

    b.grow(&r, 3);
    EXPECT_EQ(6, b.length());
    EXPECT_EQ('n', b.bytes()[2]);
    
    b.grow(&r, 100);
    EXPECT_EQ(102, b.length());
    EXPECT_EQ('l', b.bytes()[99]);
}
