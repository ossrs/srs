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

VOID TEST(KernelMp4Test, ChildBoxes)
{
    SrsMp4Box* box = new SrsMp4Box();
    EXPECT_TRUE(box->get(SrsMp4BoxTypeFTYP) == NULL);

    SrsMp4Box* ftyp = new SrsMp4FileTypeBox();
    box->append(ftyp);
    EXPECT_TRUE(box->get(SrsMp4BoxTypeFTYP) == ftyp);

    box->remove(SrsMp4BoxTypeFTYP);
    EXPECT_TRUE(box->get(SrsMp4BoxTypeFTYP) == NULL);

    srs_freep(box);
}

VOID TEST(KernelMp4Test, DiscoveryBox)
{
    srs_error_t err;
    SrsMp4Box* pbox;

    if (true) {
        SrsBuffer b;
        HELPER_ASSERT_FAILED(SrsMp4Box::discovery(&b, &pbox));
    }

    if (true) {
        uint8_t data[] = {0,0,0,1, 0,0,0,0};
        SrsBuffer b((char*)data, sizeof(data));
        HELPER_ASSERT_FAILED(SrsMp4Box::discovery(&b, &pbox));
    }

    if (true) {
        uint8_t data[] = {0,0,0,1, 0,0,0,1,0,0,0,0};
        SrsBuffer b((char*)data, sizeof(data));
        HELPER_ASSERT_FAILED(SrsMp4Box::discovery(&b, &pbox));
    }

    if (true) {
        uint8_t data[8];
        SrsBuffer b((char*)data, sizeof(data));
        b.write_4bytes(8); b.write_4bytes(SrsMp4BoxTypeEDTS); b.skip(-8);
        HELPER_ASSERT_SUCCESS(SrsMp4Box::discovery(&b, &pbox));
        ASSERT_EQ(SrsMp4BoxTypeEDTS, pbox->type);
        srs_freep(pbox);
    }

    if (true) {
        uint8_t data[8];
        SrsBuffer b((char*)data, sizeof(data));
        b.write_4bytes(8); b.write_4bytes(SrsMp4BoxTypeELST); b.skip(-8);
        HELPER_ASSERT_SUCCESS(SrsMp4Box::discovery(&b, &pbox));
        ASSERT_EQ(SrsMp4BoxTypeELST, pbox->type);
        srs_freep(pbox);
    }

    if (true) {
        uint8_t data[8];
        SrsBuffer b((char*)data, sizeof(data));
        b.write_4bytes(8); b.write_4bytes(SrsMp4BoxTypeURN); b.skip(-8);
        HELPER_ASSERT_SUCCESS(SrsMp4Box::discovery(&b, &pbox));
        ASSERT_EQ(SrsMp4BoxTypeURN, pbox->type);
        srs_freep(pbox);
    }

    if (true) {
        uint8_t data[8];
        SrsBuffer b((char*)data, sizeof(data));
        b.write_4bytes(8); b.write_4bytes(SrsMp4BoxTypeCTTS); b.skip(-8);
        HELPER_ASSERT_SUCCESS(SrsMp4Box::discovery(&b, &pbox));
        ASSERT_EQ(SrsMp4BoxTypeCTTS, pbox->type);
        srs_freep(pbox);
    }

    if (true) {
        uint8_t data[8];
        SrsBuffer b((char*)data, sizeof(data));
        b.write_4bytes(8); b.write_4bytes(SrsMp4BoxTypeCO64); b.skip(-8);
        HELPER_ASSERT_SUCCESS(SrsMp4Box::discovery(&b, &pbox));
        ASSERT_EQ(SrsMp4BoxTypeCO64, pbox->type);
        srs_freep(pbox);
    }

    if (true) {
        uint8_t data[8];
        SrsBuffer b((char*)data, sizeof(data));
        b.write_4bytes(8); b.write_4bytes(SrsMp4BoxTypeUDTA); b.skip(-8);
        HELPER_ASSERT_SUCCESS(SrsMp4Box::discovery(&b, &pbox));
        ASSERT_EQ(SrsMp4BoxTypeUDTA, pbox->type);
        srs_freep(pbox);
    }

    if (true) {
        uint8_t data[8];
        SrsBuffer b((char*)data, sizeof(data));
        b.write_4bytes(8); b.write_4bytes(SrsMp4BoxTypeMVEX); b.skip(-8);
        HELPER_ASSERT_SUCCESS(SrsMp4Box::discovery(&b, &pbox));
        ASSERT_EQ(SrsMp4BoxTypeMVEX, pbox->type);
        srs_freep(pbox);
    }

    if (true) {
        uint8_t data[8];
        SrsBuffer b((char*)data, sizeof(data));
        b.write_4bytes(8); b.write_4bytes(SrsMp4BoxTypeTREX); b.skip(-8);
        HELPER_ASSERT_SUCCESS(SrsMp4Box::discovery(&b, &pbox));
        ASSERT_EQ(SrsMp4BoxTypeTREX, pbox->type);
        srs_freep(pbox);
    }

    if (true) {
        uint8_t data[8];
        SrsBuffer b((char*)data, sizeof(data));
        b.write_4bytes(8); b.write_4bytes(SrsMp4BoxTypeSTYP); b.skip(-8);
        HELPER_ASSERT_SUCCESS(SrsMp4Box::discovery(&b, &pbox));
        ASSERT_EQ(SrsMp4BoxTypeSTYP, pbox->type);
        srs_freep(pbox);
    }

    if (true) {
        uint8_t data[8];
        SrsBuffer b((char*)data, sizeof(data));
        b.write_4bytes(8); b.write_4bytes(SrsMp4BoxTypeMOOF); b.skip(-8);
        HELPER_ASSERT_SUCCESS(SrsMp4Box::discovery(&b, &pbox));
        ASSERT_EQ(SrsMp4BoxTypeMOOF, pbox->type);
        srs_freep(pbox);
    }

    if (true) {
        uint8_t data[8];
        SrsBuffer b((char*)data, sizeof(data));
        b.write_4bytes(8); b.write_4bytes(SrsMp4BoxTypeMFHD); b.skip(-8);
        HELPER_ASSERT_SUCCESS(SrsMp4Box::discovery(&b, &pbox));
        ASSERT_EQ(SrsMp4BoxTypeMFHD, pbox->type);
        srs_freep(pbox);
    }

    if (true) {
        uint8_t data[8];
        SrsBuffer b((char*)data, sizeof(data));
        b.write_4bytes(8); b.write_4bytes(SrsMp4BoxTypeTRAF); b.skip(-8);
        HELPER_ASSERT_SUCCESS(SrsMp4Box::discovery(&b, &pbox));
        ASSERT_EQ(SrsMp4BoxTypeTRAF, pbox->type);
        srs_freep(pbox);
    }

    if (true) {
        uint8_t data[8];
        SrsBuffer b((char*)data, sizeof(data));
        b.write_4bytes(8); b.write_4bytes(SrsMp4BoxTypeTFHD); b.skip(-8);
        HELPER_ASSERT_SUCCESS(SrsMp4Box::discovery(&b, &pbox));
        ASSERT_EQ(SrsMp4BoxTypeTFHD, pbox->type);
        srs_freep(pbox);
    }

    if (true) {
        uint8_t data[8];
        SrsBuffer b((char*)data, sizeof(data));
        b.write_4bytes(8); b.write_4bytes(SrsMp4BoxTypeTFDT); b.skip(-8);
        HELPER_ASSERT_SUCCESS(SrsMp4Box::discovery(&b, &pbox));
        ASSERT_EQ(SrsMp4BoxTypeTFDT, pbox->type);
        srs_freep(pbox);
    }

    if (true) {
        uint8_t data[8];
        SrsBuffer b((char*)data, sizeof(data));
        b.write_4bytes(8); b.write_4bytes(SrsMp4BoxTypeTRUN); b.skip(-8);
        HELPER_ASSERT_SUCCESS(SrsMp4Box::discovery(&b, &pbox));
        ASSERT_EQ(SrsMp4BoxTypeTRUN, pbox->type);
        srs_freep(pbox);
    }

    if (true) {
        uint8_t data[8];
        SrsBuffer b((char*)data, sizeof(data));
        b.write_4bytes(8); b.write_4bytes(SrsMp4BoxTypeSIDX); b.skip(-8);
        HELPER_ASSERT_SUCCESS(SrsMp4Box::discovery(&b, &pbox));
        ASSERT_EQ(SrsMp4BoxTypeSIDX, pbox->type);
        srs_freep(pbox);
    }
}

VOID TEST(KernelMp4Test, UUIDBoxDecode)
{
    srs_error_t err;
    SrsMp4Box* pbox;

    if (true) {
        uint8_t data[24];
        SrsBuffer b((char*)data, sizeof(data));
        b.write_4bytes(8); b.write_4bytes(SrsMp4BoxTypeUUID); b.skip(-24);
        SrsMp4Box box;
        HELPER_ASSERT_FAILED(box.decode(&b));
    }

    if (true) {
        uint8_t data[8];
        SrsBuffer b((char*)data, sizeof(data));
        b.write_4bytes(8); b.write_4bytes(SrsMp4BoxTypeUUID); b.skip(-8);
        SrsMp4Box box;
        HELPER_ASSERT_FAILED(box.decode(&b));
    }

    if (true) {
        uint8_t data[16];
        SrsBuffer b((char*)data, sizeof(data));
        b.write_4bytes(1); b.write_8bytes(0x80000000LL); b.write_4bytes(SrsMp4BoxTypeUUID); b.skip(-16);
        SrsMp4Box box;
        HELPER_ASSERT_FAILED(box.decode(&b));
    }

    if (true) {
        uint8_t data[8];
        SrsBuffer b((char*)data, sizeof(data));
        b.write_4bytes(1); b.write_4bytes(SrsMp4BoxTypeUUID); b.skip(-8);
        SrsMp4Box box;
        HELPER_ASSERT_FAILED(box.decode(&b));
    }

    if (true) {
        SrsBuffer b;
        SrsMp4Box box;
        HELPER_ASSERT_FAILED(box.decode(&b));
    }

    if (true) {
        uint8_t data[8];
        SrsBuffer b((char*)data, sizeof(data));
        b.write_4bytes(0); b.write_4bytes(SrsMp4BoxTypeUUID); b.skip(-8);
        HELPER_ASSERT_SUCCESS(SrsMp4Box::discovery(&b, &pbox));
        ASSERT_EQ(SrsMp4BoxTypeUUID, pbox->type);
        HELPER_EXPECT_SUCCESS(pbox->decode(&b));
        srs_freep(pbox);
    }

    if (true) {
        uint8_t data[8];
        SrsBuffer b((char*)data, sizeof(data));
        b.write_4bytes(8); b.write_4bytes(SrsMp4BoxTypeUUID); b.skip(-8);
        HELPER_ASSERT_SUCCESS(SrsMp4Box::discovery(&b, &pbox));
        ASSERT_EQ(SrsMp4BoxTypeUUID, pbox->type);
        srs_freep(pbox);
    }

    if (true) {
        uint8_t data[8];
        SrsBuffer b((char*)data, sizeof(data));
        b.write_4bytes(0); b.write_4bytes(SrsMp4BoxTypeUUID); b.skip(-8);
        HELPER_ASSERT_SUCCESS(SrsMp4Box::discovery(&b, &pbox));
        ASSERT_EQ(SrsMp4BoxTypeUUID, pbox->type);
        srs_freep(pbox);
    }
}

VOID TEST(KernelMp4Test, UUIDBoxEncode)
{
    srs_error_t err;

    if (true) {
        char data[8];
        SrsBuffer b(data, 8);

        SrsMp4Box box;
        box.type = SrsMp4BoxTypeFREE;
        box.usertype.resize(8);
        ASSERT_EQ(8, box.nb_bytes());
        HELPER_ASSERT_SUCCESS(box.encode(&b));
    }

    if (true) {
        char data[24];
        SrsBuffer b(data, 24);

        SrsMp4Box box;
        box.type = SrsMp4BoxTypeUUID;
        box.usertype.resize(16);
        ASSERT_EQ(24, box.nb_bytes());
        HELPER_ASSERT_SUCCESS(box.encode(&b));
    }
}

VOID TEST(KernelMp4Test, FullBoxDump)
{
    if (true) {
        stringstream ss;
        SrsMp4DumpContext dc;
        SrsMp4FileTypeBox box;
        box.major_brand = SrsMp4BoxBrandISO2;
        box.compatible_brands.push_back(SrsMp4BoxBrandISOM);
        box.dumps(ss, dc);

        string v = ss.str();
        EXPECT_STREQ("ftyp, 0B, brands:iso2,0(isom)\n", v.c_str());
    }

    if (true) {
        stringstream ss;
        SrsMp4DumpContext dc;
        SrsMp4FullBox box;
        box.type = SrsMp4BoxTypeFTYP;
        box.version = 1;
        box.flags = 0x02;
        box.dumps(ss, dc);

        string v = ss.str();
        EXPECT_STREQ("ftyp, 0B, FB(4B,V1,0x02)\n", v.c_str());
    }

    if (true) {
        stringstream ss;
        SrsMp4DumpContext dc;
        SrsMp4FullBox box;
        box.type = SrsMp4BoxTypeFTYP;
        box.version = 1;
        box.dumps(ss, dc);

        string v = ss.str();
        EXPECT_STREQ("ftyp, 0B, FB(4B,V1,0x00)\n", v.c_str());
    }

    if (true) {
        stringstream ss;
        SrsMp4DumpContext dc;
        SrsMp4FullBox box;
        box.type = SrsMp4BoxTypeFTYP;
        box.dumps(ss, dc);

        string v = ss.str();
        EXPECT_STREQ("ftyp, 0B, FB(4B)\n", v.c_str());
    }
}

