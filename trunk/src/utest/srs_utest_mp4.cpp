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
    srs_error_t err;

    if (true) {
        uint8_t data[12];
        SrsBuffer b((char*)data, sizeof(data));
        b.write_4bytes(12); b.write_4bytes(SrsMp4BoxTypeMFHD); b.write_1bytes(1); b.write_3bytes(2); b.skip(-12);

        SrsMp4FullBox box;
        HELPER_ASSERT_SUCCESS(box.decode(&b));
        EXPECT_EQ(1, box.version);
        EXPECT_EQ(2, box.flags);
    }

    if (true) {
        SrsMp4FileTypeBox box;
        box.major_brand = SrsMp4BoxBrandISO2;
        box.compatible_brands.push_back(SrsMp4BoxBrandISOM);
        EXPECT_EQ(20, box.update_size());

        stringstream ss;
        SrsMp4DumpContext dc;
        box.dumps(ss, dc);

        string v = ss.str();
        EXPECT_STREQ("ftyp, 20B, brands:iso2,0(isom)\n", v.c_str());
    }

    if (true) {
        SrsMp4FullBox box;
        box.type = SrsMp4BoxTypeFTYP;
        box.version = 1;
        box.flags = 0x02;
        EXPECT_EQ(12, box.update_size());

        stringstream ss;
        SrsMp4DumpContext dc;
        box.dumps(ss, dc);

        string v = ss.str();
        EXPECT_STREQ("ftyp, 12B, FB(4B,V1,0x02)\n", v.c_str());
    }

    if (true) {
        SrsMp4FullBox box;
        box.type = SrsMp4BoxTypeFTYP;
        box.version = 1;
        EXPECT_EQ(12, box.update_size());

        stringstream ss;
        SrsMp4DumpContext dc;
        box.dumps(ss, dc);

        string v = ss.str();
        EXPECT_STREQ("ftyp, 12B, FB(4B,V1,0x00)\n", v.c_str());
    }

    if (true) {
        SrsMp4FullBox box;
        box.type = SrsMp4BoxTypeFTYP;
        EXPECT_EQ(12, box.update_size());

        stringstream ss;
        SrsMp4DumpContext dc;
        box.dumps(ss, dc);

        string v = ss.str();
        EXPECT_STREQ("ftyp, 12B, FB(4B)\n", v.c_str());
    }
}

VOID TEST(KernelMp4Test, MFHDBox)
{
    srs_error_t err;

    if (true) {
        uint8_t data[12+4];
        SrsBuffer b((char*)data, sizeof(data));
        b.write_4bytes(16); b.write_4bytes(SrsMp4BoxTypeMFHD); b.write_1bytes(0); b.write_3bytes(0);
        b.write_4bytes(3); b.skip(-16);

        SrsMp4MovieFragmentHeaderBox box;
        HELPER_ASSERT_SUCCESS(box.decode(&b));
        EXPECT_EQ(3, box.sequence_number);
    }

    if (true) {
        SrsMp4MovieFragmentHeaderBox box;
        box.sequence_number = 3;
        EXPECT_EQ(16, box.update_size());

        stringstream ss;
        SrsMp4DumpContext dc;
        box.dumps(ss, dc);

        string v = ss.str();
        EXPECT_STREQ("mfhd, 16B, FB(4B), sequence=3\n", v.c_str());
    }

    SrsMp4TrackFragmentBox box;
    EXPECT_TRUE(NULL == box.tfhd());
    EXPECT_TRUE(NULL == box.tfdt());
}

VOID TEST(KernelMp4Test, TFHDBox)
{
    srs_error_t err;

    if (true) {
        char buf[12+4];
        SrsBuffer b(buf, sizeof(buf));

        if (true) {
            SrsMp4TrackFragmentHeaderBox box;
            box.track_id = 100;
            EXPECT_EQ(sizeof(buf), box.nb_bytes());
            HELPER_EXPECT_SUCCESS(box.encode(&b));

            stringstream ss;
            SrsMp4DumpContext dc;
            box.dumps(ss, dc);

            string v = ss.str();
            EXPECT_STREQ("tfhd, 16B, FB(4B), track=100\n", v.c_str());
        }

        if (true) {
            b.skip(-1 * b.pos());
            SrsMp4TrackFragmentHeaderBox box;
            HELPER_EXPECT_SUCCESS(box.decode(&b));
            EXPECT_EQ(100, box.track_id);
        }
    }

    if (true) {
        char buf[12+28];
        SrsBuffer b(buf, sizeof(buf));

        if (true) {
            SrsMp4TrackFragmentHeaderBox box;
            box.track_id = 100;
            box.flags = SrsMp4TfhdFlagsBaseDataOffset | SrsMp4TfhdFlagsSampleDescriptionIndex
                | SrsMp4TfhdFlagsDefaultSampleDuration | SrsMp4TfhdFlagsDefautlSampleSize
                | SrsMp4TfhdFlagsDefaultSampleFlags | SrsMp4TfhdFlagsDurationIsEmpty
                | SrsMp4TfhdFlagsDefaultBaseIsMoof;
            box.base_data_offset = 10;
            box.sample_description_index = 11;
            box.default_sample_duration = 12;
            box.default_sample_size = 13;
            box.default_sample_flags = 14;
            EXPECT_EQ(sizeof(buf), box.nb_bytes());
            HELPER_EXPECT_SUCCESS(box.encode(&b));

            stringstream ss;
            SrsMp4DumpContext dc;
            box.dumps(ss, dc);

            string v = ss.str();
            EXPECT_STREQ("tfhd, 40B, FB(4B,V0,0x3003b), track=100, bdo=10, sdi=11, dsu=12, dss=13, dsf=14, empty-duration, moof-base\n", v.c_str());
        }

        if (true) {
            b.skip(-1 * b.pos());
            SrsMp4TrackFragmentHeaderBox box;
            HELPER_EXPECT_SUCCESS(box.decode(&b));
            EXPECT_EQ(100, box.track_id);
            EXPECT_EQ(box.base_data_offset, 10);
            EXPECT_EQ(box.sample_description_index, 11);
            EXPECT_EQ(box.default_sample_duration, 12);
            EXPECT_EQ(box.default_sample_size, 13);
            EXPECT_EQ(box.default_sample_flags, 14);
        }
    }
}

VOID TEST(KernelMp4Test, TFDTBox)
{
    srs_error_t err;

    if (true) {
        char buf[12+4];
        SrsBuffer b(buf, sizeof(buf));

        if (true) {
            SrsMp4TrackFragmentDecodeTimeBox box;
            box.base_media_decode_time = 100;
            EXPECT_EQ(sizeof(buf), box.nb_bytes());
            HELPER_EXPECT_SUCCESS(box.encode(&b));

            stringstream ss;
            SrsMp4DumpContext dc;
            box.dumps(ss, dc);

            string v = ss.str();
            EXPECT_STREQ("tfdt, 16B, FB(4B), bmdt=100\n", v.c_str());
        }

        if (true) {
            b.skip(-1 * b.pos());
            SrsMp4TrackFragmentDecodeTimeBox box;
            HELPER_EXPECT_SUCCESS(box.decode(&b));
            EXPECT_EQ(100, box.base_media_decode_time);
        }
    }

    if (true) {
        char buf[12+8];
        SrsBuffer b(buf, sizeof(buf));

        if (true) {
            SrsMp4TrackFragmentDecodeTimeBox box;
            box.version = 1;
            box.base_media_decode_time = 100;
            EXPECT_EQ(sizeof(buf), box.nb_bytes());
            HELPER_EXPECT_SUCCESS(box.encode(&b));

            stringstream ss;
            SrsMp4DumpContext dc;
            box.dumps(ss, dc);

            string v = ss.str();
            EXPECT_STREQ("tfdt, 20B, FB(4B,V1,0x00), bmdt=100\n", v.c_str());
        }

        if (true) {
            b.skip(-1 * b.pos());
            SrsMp4TrackFragmentDecodeTimeBox box;
            HELPER_EXPECT_SUCCESS(box.decode(&b));
            EXPECT_EQ(100, box.base_media_decode_time);
        }
    }
}

VOID TEST(KernelMp4Test, TRUNBox)
{
    srs_error_t err;

    if (true) {
        char buf[12+4];
        SrsBuffer b(buf, sizeof(buf));

        if (true) {
            SrsMp4TrackFragmentRunBox box;
            EXPECT_EQ(sizeof(buf), box.nb_bytes());
            HELPER_EXPECT_SUCCESS(box.encode(&b));

            stringstream ss;
            SrsMp4DumpContext dc;
            box.dumps(ss, dc);

            string v = ss.str();
            EXPECT_STREQ("trun, 16B, FB(4B), samples=0\n", v.c_str());
        }

        if (true) {
            b.skip(-1 * b.pos());
            SrsMp4TrackFragmentDecodeTimeBox box;
            HELPER_EXPECT_SUCCESS(box.decode(&b));
        }
    }

    if (true) {
        char buf[12+8];
        SrsBuffer b(buf, sizeof(buf));

        if (true) {
            SrsMp4TrackFragmentRunBox box;
            box.flags = SrsMp4TrunFlagsSampleDuration;

            SrsMp4TrunEntry* entry = new SrsMp4TrunEntry(&box);
            entry->sample_duration = 1000;
            box.entries.push_back(entry);

            EXPECT_EQ(sizeof(buf), box.nb_bytes());
            HELPER_EXPECT_SUCCESS(box.encode(&b));

            stringstream ss;
            SrsMp4DumpContext dc;
            box.dumps(ss, dc);

            string v = ss.str();
            EXPECT_STREQ("trun, 20B, FB(4B,V0,0x100), samples=1\n    duration=1000\n", v.c_str());
        }

        if (true) {
            b.skip(-1 * b.pos());
            SrsMp4TrackFragmentRunBox box;
            HELPER_EXPECT_SUCCESS(box.decode(&b));
            ASSERT_EQ(1, box.entries.size());

            SrsMp4TrunEntry* entry = box.entries.at(0);
            EXPECT_EQ(1000, entry->sample_duration);
        }
    }
}

VOID TEST(KernelMp4Test, FreeBox)
{
    srs_error_t err;

    if (true) {
        char buf[8+4];
        SrsBuffer b(buf, sizeof(buf));

        if (true) {
            SrsMp4FreeSpaceBox box(SrsMp4BoxTypeFREE);
            box.data.resize(4);
            EXPECT_EQ(sizeof(buf), box.nb_bytes());
            HELPER_EXPECT_SUCCESS(box.encode(&b));

            stringstream ss;
            SrsMp4DumpContext dc;
            box.dumps(ss, dc);

            string v = ss.str();
            EXPECT_STREQ("free, 12B, free 4B\n    0x00, 0x00, 0x00, 0x00\n", v.c_str());
        }

        if (true) {
            b.skip(-1 * b.pos());
            SrsMp4FreeSpaceBox box(SrsMp4BoxTypeSKIP);
            HELPER_EXPECT_SUCCESS(box.decode(&b));
            EXPECT_EQ(4, box.data.size());
        }
    }
}

VOID TEST(KernelMp4Test, MOOVBox)
{
    srs_error_t err;

    if (true) {
        char buf[8];
        SrsBuffer b(buf, sizeof(buf));

        if (true) {
            SrsMp4MovieBox box;
            EXPECT_EQ(sizeof(buf), box.nb_bytes());
            HELPER_EXPECT_SUCCESS(box.encode(&b));

            stringstream ss;
            SrsMp4DumpContext dc;
            box.dumps(ss, dc);

            string v = ss.str();
            EXPECT_STREQ("moov, 8B\n", v.c_str());
        }

        if (true) {
            b.skip(-1 * b.pos());
            SrsMp4MovieBox box;
            HELPER_EXPECT_SUCCESS(box.decode(&b));
        }
    }

    if (true) {
        SrsMp4MovieBox box;
        EXPECT_TRUE(NULL == box.mvhd());
        EXPECT_TRUE(NULL == box.mvex());
        EXPECT_TRUE(NULL == box.video());
        EXPECT_TRUE(NULL == box.audio());
        EXPECT_EQ(0, box.nb_vide_tracks());
        EXPECT_EQ(0, box.nb_soun_tracks());

        SrsMp4MovieHeaderBox* mvhd = new SrsMp4MovieHeaderBox();
        box.set_mvhd(mvhd);
        EXPECT_TRUE(mvhd == box.mvhd());

        SrsMp4MovieExtendsBox* mvex = new SrsMp4MovieExtendsBox();
        box.set_mvex(mvex);
        EXPECT_TRUE(mvex == box.mvex());

        SrsMp4TrackBox* video = new SrsMp4TrackBox();
        if (true) {
            SrsMp4MediaBox* media = new SrsMp4MediaBox();
            SrsMp4HandlerReferenceBox* hdr = new SrsMp4HandlerReferenceBox();
            hdr->handler_type = SrsMp4HandlerTypeVIDE;
            media->set_hdlr(hdr);
            video->set_mdia(media);
        }
        box.add_trak(video);
        EXPECT_TRUE(video == box.video());
        EXPECT_EQ(1, box.nb_vide_tracks());

        SrsMp4TrackBox* audio = new SrsMp4TrackBox();
        if (true) {
            SrsMp4MediaBox* media = new SrsMp4MediaBox();
            SrsMp4HandlerReferenceBox* hdr = new SrsMp4HandlerReferenceBox();
            hdr->handler_type = SrsMp4HandlerTypeSOUN;
            media->set_hdlr(hdr);
            audio->set_mdia(media);
        }
        box.add_trak(audio);
        EXPECT_TRUE(audio == box.audio());
        EXPECT_EQ(1, box.nb_soun_tracks());
    }
}

VOID TEST(KernelMp4Test, TREXBox)
{
    srs_error_t err;

    if (true) {
        char buf[12+20];
        SrsBuffer b(buf, sizeof(buf));

        if (true) {
            SrsMp4TrackExtendsBox box;
            box.track_ID = 1; box.default_sample_description_index = 2; box.default_sample_size = 3;
            box.default_sample_duration = 4; box.default_sample_flags = 5;
            EXPECT_EQ(sizeof(buf), box.nb_bytes());
            HELPER_EXPECT_SUCCESS(box.encode(&b));

            stringstream ss;
            SrsMp4DumpContext dc;
            box.dumps(ss, dc);

            string v = ss.str();
            EXPECT_STREQ("trex, 32B, FB(4B), track=#1, default-sample(index:2, size:3, duration:4, flags:5)\n", v.c_str());
        }

        if (true) {
            b.skip(-1 * b.pos());
            SrsMp4TrackExtendsBox box;
            HELPER_EXPECT_SUCCESS(box.decode(&b));
            EXPECT_EQ(box.track_ID, 1);
            EXPECT_EQ(box.default_sample_description_index, 2);
            EXPECT_EQ(box.default_sample_size, 3);
            EXPECT_EQ(box.default_sample_duration, 4);
            EXPECT_EQ(box.default_sample_flags, 5);
        }
    }

    SrsMp4MovieExtendsBox box;
    EXPECT_TRUE(NULL == box.trex());

    SrsMp4TrackExtendsBox* trex = new SrsMp4TrackExtendsBox();
    box.set_trex(trex);
    EXPECT_TRUE(trex == box.trex());
}

VOID TEST(KernelMp4Test, TKHDBox)
{
    srs_error_t err;

    if (true) {
        char buf[12+20+60];
        SrsBuffer b(buf, sizeof(buf));

        if (true) {
            SrsMp4TrackHeaderBox box;
            box.track_ID = 1;
            EXPECT_EQ(sizeof(buf), box.nb_bytes());
            HELPER_EXPECT_SUCCESS(box.encode(&b));

            stringstream ss;
            SrsMp4DumpContext dc;
            box.dumps(ss, dc);

            string v = ss.str();
            EXPECT_STREQ("tkhd, 92B, FB(4B,V0,0x03), track #1, 0TBN, size=0x0\n", v.c_str());
        }

        if (true) {
            b.skip(-1 * b.pos());
            SrsMp4TrackHeaderBox box;
            HELPER_EXPECT_SUCCESS(box.decode(&b));
            EXPECT_EQ(box.track_ID, 1);
        }
    }

    if (true) {
        char buf[12+32+60];
        SrsBuffer b(buf, sizeof(buf));

        if (true) {
            SrsMp4TrackHeaderBox box;
            box.version = 1;
            box.track_ID = 1;
            EXPECT_EQ(sizeof(buf), box.nb_bytes());
            HELPER_EXPECT_SUCCESS(box.encode(&b));

            stringstream ss;
            SrsMp4DumpContext dc;
            box.dumps(ss, dc);

            string v = ss.str();
            EXPECT_STREQ("tkhd, 104B, FB(4B,V1,0x03), track #1, 0TBN, size=0x0\n", v.c_str());
        }

        if (true) {
            b.skip(-1 * b.pos());
            SrsMp4TrackHeaderBox box;
            HELPER_EXPECT_SUCCESS(box.decode(&b));
            EXPECT_EQ(box.track_ID, 1);
        }
    }
}

VOID TEST(KernelMp4Test, ELSTBox)
{
    srs_error_t err;

    if (true) {
        char buf[12+4];
        SrsBuffer b(buf, sizeof(buf));

        if (true) {
            SrsMp4EditListBox box;
            EXPECT_EQ(sizeof(buf), box.nb_bytes());
            HELPER_EXPECT_SUCCESS(box.encode(&b));

            stringstream ss;
            SrsMp4DumpContext dc;
            box.dumps(ss, dc);

            string v = ss.str();
            EXPECT_STREQ("elst, 16B, FB(4B), 0 childs\n", v.c_str());
        }

        if (true) {
            b.skip(-1 * b.pos());
            SrsMp4EditListBox box;
            HELPER_EXPECT_SUCCESS(box.decode(&b));
        }
    }

    if (true) {
        char buf[12+4+12];
        SrsBuffer b(buf, sizeof(buf));

        if (true) {
            SrsMp4EditListBox box;
            if (true) {
                SrsMp4ElstEntry entry;
                box.entries.push_back(entry);
            }
            EXPECT_EQ(sizeof(buf), box.nb_bytes());
            HELPER_EXPECT_SUCCESS(box.encode(&b));

            stringstream ss;
            SrsMp4DumpContext dc;
            box.dumps(ss, dc);

            string v = ss.str();
            EXPECT_STREQ("elst, 28B, FB(4B), 1 childs(+)\n    Entry, 0TBN, start=0TBN, rate=0,0\n", v.c_str());
        }

        if (true) {
            b.skip(-1 * b.pos());
            SrsMp4EditListBox box;
            HELPER_EXPECT_SUCCESS(box.decode(&b));
        }
    }

    if (true) {
        SrsMp4MediaBox box;
        SrsMp4HandlerReferenceBox* hdlr = new SrsMp4HandlerReferenceBox();
        box.set_hdlr(hdlr);
        EXPECT_TRUE(hdlr == box.hdlr());
    }
}

VOID TEST(KernelMp4Test, MDHDBox)
{
    srs_error_t err;

    if (true) {
        char buf[12+20];
        SrsBuffer b(buf, sizeof(buf));

        if (true) {
            SrsMp4MediaHeaderBox box;
            EXPECT_EQ(sizeof(buf), box.nb_bytes());
            HELPER_EXPECT_SUCCESS(box.encode(&b));

            stringstream ss;
            SrsMp4DumpContext dc;
            box.dumps(ss, dc);

            string v = ss.str();
            EXPECT_STREQ("mdhd, 32B, FB(4B), TBN=0, 0TBN\n", v.c_str());
        }

        if (true) {
            b.skip(-1 * b.pos());
            SrsMp4MediaHeaderBox box;
            HELPER_EXPECT_SUCCESS(box.decode(&b));
        }
    }

    if (true) {
        char buf[12+20];
        SrsBuffer b(buf, sizeof(buf));

        if (true) {
            SrsMp4MediaHeaderBox box;
            box.set_language0('C');
            box.set_language1('N');
            box.set_language2('E');
            EXPECT_EQ(sizeof(buf), box.nb_bytes());
            HELPER_EXPECT_SUCCESS(box.encode(&b));

            stringstream ss;
            SrsMp4DumpContext dc;
            box.dumps(ss, dc);

            string v = ss.str();
            EXPECT_STREQ("mdhd, 32B, FB(4B), TBN=0, 0TBN, LANG=cne\n", v.c_str());
        }

        if (true) {
            b.skip(-1 * b.pos());
            SrsMp4MediaHeaderBox box;
            HELPER_EXPECT_SUCCESS(box.decode(&b));
        }
    }

    if (true) {
        SrsMp4MediaHeaderBox box;

        box.set_language0('C');
        EXPECT_EQ('c', box.language0());

        box.set_language1('N');
        EXPECT_EQ('n', box.language1());

        box.set_language2('E');
        EXPECT_EQ('e', box.language2());
    }

    if (true) {
        SrsMp4HandlerReferenceBox box;
        box.handler_type = SrsMp4HandlerTypeVIDE;
        EXPECT_TRUE(box.is_video());
    }

    if (true) {
        SrsMp4HandlerReferenceBox box;
        box.handler_type = SrsMp4HandlerTypeSOUN;
        EXPECT_TRUE(box.is_audio());
    }
}

VOID TEST(KernelMp4Test, HDLRBox)
{
    srs_error_t err;

    if (true) {
        char buf[12+21];
        SrsBuffer b(buf, sizeof(buf));

        if (true) {
            SrsMp4HandlerReferenceBox box;
            box.handler_type = SrsMp4HandlerTypeSOUN;
            EXPECT_EQ(sizeof(buf), box.nb_bytes());
            HELPER_EXPECT_SUCCESS(box.encode(&b));

            stringstream ss;
            SrsMp4DumpContext dc;
            box.dumps(ss, dc);

            string v = ss.str();
            EXPECT_STREQ("hdlr, 33B, FB(4B), soun\n", v.c_str());
        }

        if (true) {
            b.skip(-1 * b.pos());
            SrsMp4HandlerReferenceBox box;
            HELPER_EXPECT_SUCCESS(box.decode(&b));
            EXPECT_EQ(SrsMp4HandlerTypeSOUN, box.handler_type);
        }
    }

    if (true) {
        char buf[12+21];
        SrsBuffer b(buf, sizeof(buf));

        if (true) {
            SrsMp4HandlerReferenceBox box;
            box.handler_type = SrsMp4HandlerTypeVIDE;
            EXPECT_EQ(sizeof(buf), box.nb_bytes());
            HELPER_EXPECT_SUCCESS(box.encode(&b));

            stringstream ss;
            SrsMp4DumpContext dc;
            box.dumps(ss, dc);

            string v = ss.str();
            EXPECT_STREQ("hdlr, 33B, FB(4B), vide\n", v.c_str());
        }

        if (true) {
            b.skip(-1 * b.pos());
            SrsMp4HandlerReferenceBox box;
            HELPER_EXPECT_SUCCESS(box.decode(&b));
            EXPECT_EQ(SrsMp4HandlerTypeVIDE, box.handler_type);
        }
    }

    if (true) {
        char buf[12+24];
        SrsBuffer b(buf, sizeof(buf));

        if (true) {
            SrsMp4HandlerReferenceBox box;
            box.handler_type = SrsMp4HandlerTypeVIDE;
            box.name = "srs";
            EXPECT_EQ(sizeof(buf), box.nb_bytes());
            HELPER_EXPECT_SUCCESS(box.encode(&b));

            stringstream ss;
            SrsMp4DumpContext dc;
            box.dumps(ss, dc);

            string v = ss.str();
            EXPECT_STREQ("hdlr, 36B, FB(4B), vide, srs\n", v.c_str());
        }

        if (true) {
            b.skip(-1 * b.pos());
            SrsMp4HandlerReferenceBox box;
            HELPER_EXPECT_SUCCESS(box.decode(&b));
            EXPECT_EQ(SrsMp4HandlerTypeVIDE, box.handler_type);
        }
    }

    if (true) {
        SrsMp4MediaInformationBox box;
        SrsMp4VideoMeidaHeaderBox* vmhd = new SrsMp4VideoMeidaHeaderBox();
        box.set_vmhd(vmhd);
        EXPECT_TRUE(vmhd == box.vmhd());
    }

    if (true) {
        SrsMp4MediaInformationBox box;
        SrsMp4SoundMeidaHeaderBox* smhd = new SrsMp4SoundMeidaHeaderBox();
        box.set_smhd(smhd);
        EXPECT_TRUE(smhd == box.smhd());
    }

    if (true) {
        SrsMp4MediaInformationBox box;
        SrsMp4DataInformationBox* dinf = new SrsMp4DataInformationBox();
        box.set_dinf(dinf);
        EXPECT_TRUE(dinf == box.dinf());
    }

    if (true) {
        SrsMp4DataInformationBox box;
        SrsMp4DataReferenceBox* dref = new SrsMp4DataReferenceBox();
        box.set_dref(dref);
        EXPECT_TRUE(dref == box.dref());
    }
}

VOID TEST(KernelMp4Test, URLBox)
{
    srs_error_t err;

    if (true) {
        char buf[12+1];
        SrsBuffer b(buf, sizeof(buf));

        if (true) {
            SrsMp4DataEntryUrlBox box;
            EXPECT_EQ(sizeof(buf), box.nb_bytes());
            HELPER_EXPECT_SUCCESS(box.encode(&b));

            stringstream ss;
            SrsMp4DumpContext dc;
            box.dumps(ss, dc);

            string v = ss.str();
            EXPECT_STREQ("url , 13B, FB(4B,V0,0x01), URL: Same file\n", v.c_str());
        }

        if (true) {
            b.skip(-1 * b.pos());
            SrsMp4DataEntryUrlBox box;
            HELPER_EXPECT_SUCCESS(box.decode(&b));
        }
    }
}

