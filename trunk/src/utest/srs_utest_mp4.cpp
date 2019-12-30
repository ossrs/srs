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
#include <srs_utest_mp4.hpp>

#include <sstream>
using namespace std;

#include <srs_kernel_error.hpp>
#include <srs_kernel_mp4.hpp>
#include <srs_core_autofree.hpp>

VOID TEST(KernelMp4Test, PrintPadding)
{
    stringstream ss;
    SrsMp4DumpContext dc;

    srs_mp4_padding(ss, dc);
    EXPECT_STREQ("", ss.str().c_str());

    srs_mp4_padding(ss, dc.indent());
    EXPECT_STREQ("    ", ss.str().c_str());

    srs_mp4_padding(ss, dc.indent());
    EXPECT_STREQ("        ", ss.str().c_str());
}

struct MockBox
{
public:
    MockBox() {
    }
    virtual ~MockBox() {
    }
    virtual void dumps(stringstream&ss, SrsMp4DumpContext /*dc*/) {
        ss << "mock";
    }
    virtual void dumps_detail(stringstream&ss, SrsMp4DumpContext /*dc*/) {
        ss << "mock-detail";
    }
};

VOID TEST(KernelMp4Test, DumpsArray)
{
    if (true) {
        char* p = (char*)"srs";
        vector<char> arr(p, p+3);

        stringstream ss;
        SrsMp4DumpContext dc;
        srs_dumps_array(arr, ss, dc, srs_mp4_pfn_elem, srs_mp4_delimiter_inline);

        EXPECT_STREQ("s,r,s", ss.str().c_str());
    }

    if (true) {
        char arr[] = {'s', 'r', 's'};

        stringstream ss;
        SrsMp4DumpContext dc;
        srs_dumps_array(arr, 3, ss, dc, srs_mp4_pfn_elem, srs_mp4_delimiter_inline);

        EXPECT_STREQ("s,r,s", ss.str().c_str());
    }

    if (true) {
        char arr[] = {'s', 'r', 's'};

        stringstream ss;
        SrsMp4DumpContext dc;
        srs_dumps_array(arr, 3, ss, dc, srs_mp4_pfn_elem, srs_mp4_delimiter_inspace);

        EXPECT_STREQ("s, r, s", ss.str().c_str());
    }

    if (true) {
        char arr[] = {'s', 'r', 's'};

        stringstream ss;
        SrsMp4DumpContext dc;
        srs_dumps_array(arr, 3, ss, dc, srs_mp4_pfn_elem, srs_mp4_delimiter_newline);

        EXPECT_STREQ("s\nr\ns", ss.str().c_str());
    }

    if (true) {
        MockBox arr[1];

        stringstream ss;
        SrsMp4DumpContext dc;
        srs_dumps_array(arr, 1, ss, dc, srs_mp4_pfn_box, srs_mp4_delimiter_inline);

        EXPECT_STREQ("mock", ss.str().c_str());
    }

    if (true) {
        MockBox arr[1];

        stringstream ss;
        SrsMp4DumpContext dc;
        srs_dumps_array(arr, 1, ss, dc, srs_mp4_pfn_detail, srs_mp4_delimiter_inline);

        EXPECT_STREQ("mock-detail", ss.str().c_str());
    }

    if (true) {
        MockBox* arr[1] = {new MockBox()};

        stringstream ss;
        SrsMp4DumpContext dc;
        srs_dumps_array(arr, 1, ss, dc, srs_mp4_pfn_box2, srs_mp4_delimiter_inline);

        EXPECT_STREQ("mock", ss.str().c_str());
        srs_freep(arr[0]);
    }

    if (true) {
        MockBox* arr[1] = {new MockBox()};

        stringstream ss;
        SrsMp4DumpContext dc;
        srs_dumps_array(arr, 1, ss, dc, srs_mp4_pfn_detail2, srs_mp4_delimiter_inline);

        EXPECT_STREQ("mock-detail", ss.str().c_str());
        srs_freep(arr[0]);
    }

    if (true) {
        SrsMp4BoxType arr[] = {SrsMp4BoxTypeUUID};

        stringstream ss;
        SrsMp4DumpContext dc;
        srs_dumps_array(arr, 1, ss, dc, srs_mp4_pfn_type, srs_mp4_delimiter_inline);

        EXPECT_STREQ("uuid", ss.str().c_str());
    }

    if (true) {
        uint8_t arr[] = {0xec};

        stringstream ss;
        SrsMp4DumpContext dc;
        srs_dumps_array(arr, 1, ss, dc, srs_mp4_pfn_hex, srs_mp4_delimiter_inline);

        EXPECT_STREQ("0xec", ss.str().c_str());
    }
}

VOID TEST(KernelMp4Test, PrintBytes)
{
    if (true) {
        uint8_t arr[] = {0xec};

        stringstream ss;
        SrsMp4DumpContext dc;
        srs_mp4_print_bytes(ss, (const char*)arr, 1, dc, 4, 8);

        EXPECT_STREQ("0xec", ss.str().c_str());
    }

    if (true) {
        uint8_t arr[] = {0xc};

        stringstream ss;
        SrsMp4DumpContext dc;
        srs_mp4_print_bytes(ss, (const char*)arr, 1, dc, 4, 8);

        EXPECT_STREQ("0x0c", ss.str().c_str());
    }

    if (true) {
        uint8_t arr[] = {0xec, 0xb1, 0xa3, 0xe1, 0xab};

        stringstream ss;
        SrsMp4DumpContext dc;
        srs_mp4_print_bytes(ss, (const char*)arr, 5, dc, 4, 8);

        EXPECT_STREQ("0xec, 0xb1, 0xa3, 0xe1,\n0xab", ss.str().c_str());
    }
}

