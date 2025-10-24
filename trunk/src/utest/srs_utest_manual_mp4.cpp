//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//
#include <srs_utest_manual_mp4.hpp>

#include <sstream>
using namespace std;

#include <srs_app_hls.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_mp4.hpp>
#include <srs_utest_manual_kernel.hpp>

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

struct MockBox {
public:
    MockBox()
    {
    }
    virtual ~MockBox()
    {
    }
    virtual void dumps(stringstream &ss, SrsMp4DumpContext /*dc*/)
    {
        ss << "mock";
    }
    virtual void dumps_detail(stringstream &ss, SrsMp4DumpContext /*dc*/)
    {
        ss << "mock-detail";
    }
};

VOID TEST(KernelMp4Test, DumpsArray)
{
    if (true) {
        char *p = (char *)"srs";
        vector<char> arr(p, p + 3);

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
        MockBox *arr[1] = {new MockBox()};

        stringstream ss;
        SrsMp4DumpContext dc;
        srs_dumps_array(arr, 1, ss, dc, srs_mp4_pfn_box2, srs_mp4_delimiter_inline);

        EXPECT_STREQ("mock", ss.str().c_str());
        srs_freep(arr[0]);
    }

    if (true) {
        MockBox *arr[1] = {new MockBox()};

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
        srs_mp4_print_bytes(ss, (const char *)arr, 1, dc, 4, 8);

        EXPECT_STREQ("0xec", ss.str().c_str());
    }

    if (true) {
        uint8_t arr[] = {0xc};

        stringstream ss;
        SrsMp4DumpContext dc;
        srs_mp4_print_bytes(ss, (const char *)arr, 1, dc, 4, 8);

        EXPECT_STREQ("0x0c", ss.str().c_str());
    }

    if (true) {
        uint8_t arr[] = {0xec, 0xb1, 0xa3, 0xe1, 0xab};

        stringstream ss;
        SrsMp4DumpContext dc;
        srs_mp4_print_bytes(ss, (const char *)arr, 5, dc, 4, 8);

        EXPECT_STREQ("0xec, 0xb1, 0xa3, 0xe1,\n0xab", ss.str().c_str());
    }
}

VOID TEST(KernelMp4Test, ChildBoxes)
{
    SrsMp4Box *box = new SrsMp4Box();
    EXPECT_TRUE(box->get(SrsMp4BoxTypeFTYP) == NULL);

    SrsMp4Box *ftyp = new SrsMp4FileTypeBox();
    box->append(ftyp);
    EXPECT_TRUE(box->get(SrsMp4BoxTypeFTYP) == ftyp);

    box->remove(SrsMp4BoxTypeFTYP);
    EXPECT_TRUE(box->get(SrsMp4BoxTypeFTYP) == NULL);

    srs_freep(box);
}

VOID TEST(KernelMp4Test, DiscoveryBox)
{
    srs_error_t err;
    SrsMp4Box *pbox;

    if (true) {
        SrsBuffer b(NULL, 0);
        HELPER_ASSERT_FAILED(SrsMp4Box::discovery(&b, &pbox));
    }

    if (true) {
        uint8_t data[] = {0, 0, 0, 1, 0, 0, 0, 0};
        SrsBuffer b((char *)data, sizeof(data));
        HELPER_ASSERT_FAILED(SrsMp4Box::discovery(&b, &pbox));
    }

    if (true) {
        uint8_t data[] = {0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0};
        SrsBuffer b((char *)data, sizeof(data));
        HELPER_ASSERT_FAILED(SrsMp4Box::discovery(&b, &pbox));
    }

    if (true) {
        uint8_t data[8];
        SrsBuffer b((char *)data, sizeof(data));
        b.write_4bytes(8);
        b.write_4bytes(SrsMp4BoxTypeEDTS);
        b.skip(-8);
        HELPER_ASSERT_SUCCESS(SrsMp4Box::discovery(&b, &pbox));
        ASSERT_EQ(SrsMp4BoxTypeEDTS, pbox->type_);
        srs_freep(pbox);
    }

    if (true) {
        uint8_t data[8];
        SrsBuffer b((char *)data, sizeof(data));
        b.write_4bytes(8);
        b.write_4bytes(SrsMp4BoxTypeELST);
        b.skip(-8);
        HELPER_ASSERT_SUCCESS(SrsMp4Box::discovery(&b, &pbox));
        ASSERT_EQ(SrsMp4BoxTypeELST, pbox->type_);
        srs_freep(pbox);
    }

    if (true) {
        uint8_t data[8];
        SrsBuffer b((char *)data, sizeof(data));
        b.write_4bytes(8);
        b.write_4bytes(SrsMp4BoxTypeURN);
        b.skip(-8);
        HELPER_ASSERT_SUCCESS(SrsMp4Box::discovery(&b, &pbox));
        ASSERT_EQ(SrsMp4BoxTypeURN, pbox->type_);
        srs_freep(pbox);
    }

    if (true) {
        uint8_t data[8];
        SrsBuffer b((char *)data, sizeof(data));
        b.write_4bytes(8);
        b.write_4bytes(SrsMp4BoxTypeCTTS);
        b.skip(-8);
        HELPER_ASSERT_SUCCESS(SrsMp4Box::discovery(&b, &pbox));
        ASSERT_EQ(SrsMp4BoxTypeCTTS, pbox->type_);
        srs_freep(pbox);
    }

    if (true) {
        uint8_t data[8];
        SrsBuffer b((char *)data, sizeof(data));
        b.write_4bytes(8);
        b.write_4bytes(SrsMp4BoxTypeCO64);
        b.skip(-8);
        HELPER_ASSERT_SUCCESS(SrsMp4Box::discovery(&b, &pbox));
        ASSERT_EQ(SrsMp4BoxTypeCO64, pbox->type_);
        srs_freep(pbox);
    }

    if (true) {
        uint8_t data[8];
        SrsBuffer b((char *)data, sizeof(data));
        b.write_4bytes(8);
        b.write_4bytes(SrsMp4BoxTypeUDTA);
        b.skip(-8);
        HELPER_ASSERT_SUCCESS(SrsMp4Box::discovery(&b, &pbox));
        ASSERT_EQ(SrsMp4BoxTypeUDTA, pbox->type_);
        srs_freep(pbox);
    }

    if (true) {
        uint8_t data[8];
        SrsBuffer b((char *)data, sizeof(data));
        b.write_4bytes(8);
        b.write_4bytes(SrsMp4BoxTypeMVEX);
        b.skip(-8);
        HELPER_ASSERT_SUCCESS(SrsMp4Box::discovery(&b, &pbox));
        ASSERT_EQ(SrsMp4BoxTypeMVEX, pbox->type_);
        srs_freep(pbox);
    }

    if (true) {
        uint8_t data[8];
        SrsBuffer b((char *)data, sizeof(data));
        b.write_4bytes(8);
        b.write_4bytes(SrsMp4BoxTypeTREX);
        b.skip(-8);
        HELPER_ASSERT_SUCCESS(SrsMp4Box::discovery(&b, &pbox));
        ASSERT_EQ(SrsMp4BoxTypeTREX, pbox->type_);
        srs_freep(pbox);
    }

    if (true) {
        uint8_t data[8];
        SrsBuffer b((char *)data, sizeof(data));
        b.write_4bytes(8);
        b.write_4bytes(SrsMp4BoxTypeSTYP);
        b.skip(-8);
        HELPER_ASSERT_SUCCESS(SrsMp4Box::discovery(&b, &pbox));
        ASSERT_EQ(SrsMp4BoxTypeSTYP, pbox->type_);
        srs_freep(pbox);
    }

    if (true) {
        uint8_t data[8];
        SrsBuffer b((char *)data, sizeof(data));
        b.write_4bytes(8);
        b.write_4bytes(SrsMp4BoxTypeMOOF);
        b.skip(-8);
        HELPER_ASSERT_SUCCESS(SrsMp4Box::discovery(&b, &pbox));
        ASSERT_EQ(SrsMp4BoxTypeMOOF, pbox->type_);
        srs_freep(pbox);
    }

    if (true) {
        uint8_t data[8];
        SrsBuffer b((char *)data, sizeof(data));
        b.write_4bytes(8);
        b.write_4bytes(SrsMp4BoxTypeMFHD);
        b.skip(-8);
        HELPER_ASSERT_SUCCESS(SrsMp4Box::discovery(&b, &pbox));
        ASSERT_EQ(SrsMp4BoxTypeMFHD, pbox->type_);
        srs_freep(pbox);
    }

    if (true) {
        uint8_t data[8];
        SrsBuffer b((char *)data, sizeof(data));
        b.write_4bytes(8);
        b.write_4bytes(SrsMp4BoxTypeTRAF);
        b.skip(-8);
        HELPER_ASSERT_SUCCESS(SrsMp4Box::discovery(&b, &pbox));
        ASSERT_EQ(SrsMp4BoxTypeTRAF, pbox->type_);
        srs_freep(pbox);
    }

    if (true) {
        uint8_t data[8];
        SrsBuffer b((char *)data, sizeof(data));
        b.write_4bytes(8);
        b.write_4bytes(SrsMp4BoxTypeTFHD);
        b.skip(-8);
        HELPER_ASSERT_SUCCESS(SrsMp4Box::discovery(&b, &pbox));
        ASSERT_EQ(SrsMp4BoxTypeTFHD, pbox->type_);
        srs_freep(pbox);
    }

    if (true) {
        uint8_t data[8];
        SrsBuffer b((char *)data, sizeof(data));
        b.write_4bytes(8);
        b.write_4bytes(SrsMp4BoxTypeTFDT);
        b.skip(-8);
        HELPER_ASSERT_SUCCESS(SrsMp4Box::discovery(&b, &pbox));
        ASSERT_EQ(SrsMp4BoxTypeTFDT, pbox->type_);
        srs_freep(pbox);
    }

    if (true) {
        uint8_t data[8];
        SrsBuffer b((char *)data, sizeof(data));
        b.write_4bytes(8);
        b.write_4bytes(SrsMp4BoxTypeTRUN);
        b.skip(-8);
        HELPER_ASSERT_SUCCESS(SrsMp4Box::discovery(&b, &pbox));
        ASSERT_EQ(SrsMp4BoxTypeTRUN, pbox->type_);
        srs_freep(pbox);
    }

    if (true) {
        uint8_t data[8];
        SrsBuffer b((char *)data, sizeof(data));
        b.write_4bytes(8);
        b.write_4bytes(SrsMp4BoxTypeSIDX);
        b.skip(-8);
        HELPER_ASSERT_SUCCESS(SrsMp4Box::discovery(&b, &pbox));
        ASSERT_EQ(SrsMp4BoxTypeSIDX, pbox->type_);
        srs_freep(pbox);
    }
}

VOID TEST(KernelMp4Test, UUIDBoxDecode)
{
    srs_error_t err;
    SrsMp4Box *pbox;

    if (true) {
        uint8_t data[24];
        SrsBuffer b((char *)data, sizeof(data));
        b.write_4bytes(8);
        b.write_4bytes(SrsMp4BoxTypeUUID);
        b.skip(-8);
        SrsMp4Box box;
        HELPER_EXPECT_SUCCESS(box.decode(&b));
    }

    if (true) {
        uint8_t data[8];
        SrsBuffer b((char *)data, sizeof(data));
        b.write_4bytes(8);
        b.write_4bytes(SrsMp4BoxTypeUUID);
        b.skip(-8);
        SrsMp4Box box;
        HELPER_ASSERT_FAILED(box.decode(&b));
    }

    if (true) {
        uint8_t data[16];
        SrsBuffer b((char *)data, sizeof(data));
        b.write_4bytes(1);
        b.write_8bytes(0x80000000LL);
        b.write_4bytes(SrsMp4BoxTypeUUID);
        b.skip(-16);
        SrsMp4Box box;
        HELPER_ASSERT_FAILED(box.decode(&b));
    }

    if (true) {
        uint8_t data[8];
        SrsBuffer b((char *)data, sizeof(data));
        b.write_4bytes(1);
        b.write_4bytes(SrsMp4BoxTypeUUID);
        b.skip(-8);
        SrsMp4Box box;
        HELPER_ASSERT_FAILED(box.decode(&b));
    }

    if (true) {
        SrsBuffer b(NULL, 0);
        SrsMp4Box box;
        HELPER_ASSERT_FAILED(box.decode(&b));
    }

    if (true) {
        uint8_t data[8];
        SrsBuffer b((char *)data, sizeof(data));
        b.write_4bytes(0);
        b.write_4bytes(SrsMp4BoxTypeUUID);
        b.skip(-8);
        HELPER_ASSERT_SUCCESS(SrsMp4Box::discovery(&b, &pbox));
        ASSERT_EQ(SrsMp4BoxTypeUUID, pbox->type_);
        HELPER_EXPECT_SUCCESS(pbox->decode(&b));
        srs_freep(pbox);
    }

    if (true) {
        uint8_t data[8];
        SrsBuffer b((char *)data, sizeof(data));
        b.write_4bytes(8);
        b.write_4bytes(SrsMp4BoxTypeUUID);
        b.skip(-8);
        HELPER_ASSERT_SUCCESS(SrsMp4Box::discovery(&b, &pbox));
        ASSERT_EQ(SrsMp4BoxTypeUUID, pbox->type_);
        srs_freep(pbox);
    }

    if (true) {
        uint8_t data[8];
        SrsBuffer b((char *)data, sizeof(data));
        b.write_4bytes(0);
        b.write_4bytes(SrsMp4BoxTypeUUID);
        b.skip(-8);
        HELPER_ASSERT_SUCCESS(SrsMp4Box::discovery(&b, &pbox));
        ASSERT_EQ(SrsMp4BoxTypeUUID, pbox->type_);
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
        box.type_ = SrsMp4BoxTypeFREE;
        box.usertype_.resize(8);
        ASSERT_EQ(8, (int)box.nb_bytes());
        HELPER_ASSERT_SUCCESS(box.encode(&b));
    }

    if (true) {
        char data[24];
        SrsBuffer b(data, 24);

        SrsMp4Box box;
        box.type_ = SrsMp4BoxTypeUUID;
        box.usertype_.resize(16);
        ASSERT_EQ(24, (int)box.nb_bytes());
        HELPER_ASSERT_SUCCESS(box.encode(&b));
    }
}

VOID TEST(KernelMp4Test, FullBoxDump)
{
    srs_error_t err;

    if (true) {
        uint8_t data[12];
        SrsBuffer b((char *)data, sizeof(data));
        b.write_4bytes(12);
        b.write_4bytes(SrsMp4BoxTypeMFHD);
        b.write_1bytes(1);
        b.write_3bytes(2);
        b.skip(-12);

        SrsMp4FullBox box;
        HELPER_ASSERT_SUCCESS(box.decode(&b));
        EXPECT_EQ(1, box.version_);
        EXPECT_EQ(2, (int)box.flags_);
    }

    if (true) {
        SrsMp4FileTypeBox box;
        box.major_brand_ = SrsMp4BoxBrandISO2;
        box.compatible_brands_.push_back(SrsMp4BoxBrandISOM);
        EXPECT_EQ(20, (int)box.update_size());

        stringstream ss;
        SrsMp4DumpContext dc;
        box.dumps(ss, dc);

        string v = ss.str();
        EXPECT_STREQ("ftyp, 20B, brands:iso2,0(isom)\n", v.c_str());
    }

    if (true) {
        SrsMp4FullBox box;
        box.type_ = SrsMp4BoxTypeFTYP;
        box.version_ = 1;
        box.flags_ = 0x02;
        EXPECT_EQ(12, (int)box.update_size());

        stringstream ss;
        SrsMp4DumpContext dc;
        box.dumps(ss, dc);

        string v = ss.str();
        EXPECT_STREQ("ftyp, 12B, FB(4B,V1,0x02)\n", v.c_str());
    }

    if (true) {
        SrsMp4FullBox box;
        box.type_ = SrsMp4BoxTypeFTYP;
        box.version_ = 1;
        EXPECT_EQ(12, (int)box.update_size());

        stringstream ss;
        SrsMp4DumpContext dc;
        box.dumps(ss, dc);

        string v = ss.str();
        EXPECT_STREQ("ftyp, 12B, FB(4B,V1,0x00)\n", v.c_str());
    }

    if (true) {
        SrsMp4FullBox box;
        box.type_ = SrsMp4BoxTypeFTYP;
        EXPECT_EQ(12, (int)box.update_size());

        stringstream ss;
        SrsMp4DumpContext dc;
        box.dumps(ss, dc);

        string v = ss.str();
        EXPECT_STREQ("ftyp, 12B, FB(4B)\n", v.c_str());
    }
}

VOID TEST(KernelMp4Test, MOOFBox)
{
    if (true) {
        SrsMp4MovieFragmentBox box;

        SrsMp4MovieFragmentHeaderBox *mfhd = new SrsMp4MovieFragmentHeaderBox();
        box.set_mfhd(mfhd);
        EXPECT_EQ(box.mfhd(), mfhd);

        SrsMp4TrackFragmentBox *traf = new SrsMp4TrackFragmentBox();
        box.add_traf(traf);
        EXPECT_TRUE(traf == box.get(SrsMp4BoxTypeTRAF));
    }
}

VOID TEST(KernelMp4Test, MFHDBox)
{
    srs_error_t err;

    if (true) {
        uint8_t data[12 + 4];
        SrsBuffer b((char *)data, sizeof(data));
        b.write_4bytes(16);
        b.write_4bytes(SrsMp4BoxTypeMFHD);
        b.write_1bytes(0);
        b.write_3bytes(0);
        b.write_4bytes(3);
        b.skip(-16);

        SrsMp4MovieFragmentHeaderBox box;
        HELPER_ASSERT_SUCCESS(box.decode(&b));
        EXPECT_EQ(3, (int)box.sequence_number_);
    }

    if (true) {
        SrsMp4MovieFragmentHeaderBox box;
        box.sequence_number_ = 3;
        EXPECT_EQ(16, (int)box.update_size());

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
        char buf[12 + 4];
        SrsBuffer b(buf, sizeof(buf));

        if (true) {
            SrsMp4TrackFragmentHeaderBox box;
            box.track_id_ = 100;
            EXPECT_EQ((int)sizeof(buf), (int)box.nb_bytes());
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
            EXPECT_EQ(100, (int)box.track_id_);
        }
    }

    if (true) {
        char buf[12 + 28];
        SrsBuffer b(buf, sizeof(buf));

        if (true) {
            SrsMp4TrackFragmentHeaderBox box;
            box.track_id_ = 100;
            box.flags_ = SrsMp4TfhdFlagsBaseDataOffset | SrsMp4TfhdFlagsSampleDescriptionIndex | SrsMp4TfhdFlagsDefaultSampleDuration | SrsMp4TfhdFlagsDefautlSampleSize | SrsMp4TfhdFlagsDefaultSampleFlags | SrsMp4TfhdFlagsDurationIsEmpty | SrsMp4TfhdFlagsDefaultBaseIsMoof;
            box.base_data_offset_ = 10;
            box.sample_description_index_ = 11;
            box.default_sample_duration_ = 12;
            box.default_sample_size_ = 13;
            box.default_sample_flags_ = 14;
            EXPECT_EQ((int)sizeof(buf), (int)box.nb_bytes());
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
            EXPECT_EQ((int)box.track_id_, 100);
            EXPECT_EQ((int)box.base_data_offset_, 10);
            EXPECT_EQ((int)box.sample_description_index_, 11);
            EXPECT_EQ((int)box.default_sample_duration_, 12);
            EXPECT_EQ((int)box.default_sample_size_, 13);
            EXPECT_EQ((int)box.default_sample_flags_, 14);
        }
    }
}

VOID TEST(KernelMp4Test, TFDTBox)
{
    srs_error_t err;

    if (true) {
        char buf[12 + 4];
        SrsBuffer b(buf, sizeof(buf));

        if (true) {
            SrsMp4TrackFragmentDecodeTimeBox box;
            box.base_media_decode_time_ = 100;
            EXPECT_EQ((int)sizeof(buf), (int)box.nb_bytes());
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
            EXPECT_EQ(100, (int)box.base_media_decode_time_);
        }
    }

    if (true) {
        char buf[12 + 8];
        SrsBuffer b(buf, sizeof(buf));

        if (true) {
            SrsMp4TrackFragmentDecodeTimeBox box;
            box.version_ = 1;
            box.base_media_decode_time_ = 100;
            EXPECT_EQ((int)sizeof(buf), (int)box.nb_bytes());
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
            EXPECT_EQ(100, (int)box.base_media_decode_time_);
        }
    }
}

VOID TEST(KernelMp4Test, TRUNBox)
{
    srs_error_t err;

    if (true) {
        char buf[12 + 4];
        SrsBuffer b(buf, sizeof(buf));

        if (true) {
            SrsMp4TrackFragmentRunBox box;
            EXPECT_EQ((int)sizeof(buf), (int)box.nb_bytes());
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
        char buf[12 + 8];
        SrsBuffer b(buf, sizeof(buf));

        if (true) {
            SrsMp4TrackFragmentRunBox box;
            box.flags_ = SrsMp4TrunFlagsSampleDuration;

            SrsMp4TrunEntry *entry = new SrsMp4TrunEntry(&box);
            entry->sample_duration_ = 1000;
            box.entries_.push_back(entry);

            EXPECT_EQ((int)sizeof(buf), (int)box.nb_bytes());
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
            ASSERT_EQ(1, (int)box.entries_.size());

            SrsMp4TrunEntry *entry = box.entries_.at(0);
            EXPECT_EQ(1000, (int)entry->sample_duration_);
        }
    }
}

VOID TEST(KernelMp4Test, FreeBox)
{
    srs_error_t err;

    if (true) {
        char buf[8 + 4];
        SrsBuffer b(buf, sizeof(buf));

        if (true) {
            SrsMp4FreeSpaceBox box(SrsMp4BoxTypeFREE);
            box.data_.resize(4);
            EXPECT_EQ((int)sizeof(buf), (int)box.nb_bytes());
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
            EXPECT_EQ(4, (int)box.data_.size());
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
            EXPECT_EQ((int)sizeof(buf), (int)box.nb_bytes());
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

        SrsMp4MovieHeaderBox *mvhd = new SrsMp4MovieHeaderBox();
        box.set_mvhd(mvhd);
        EXPECT_TRUE(mvhd == box.mvhd());

        SrsMp4MovieExtendsBox *mvex = new SrsMp4MovieExtendsBox();
        box.set_mvex(mvex);
        EXPECT_TRUE(mvex == box.mvex());

        SrsMp4TrackBox *video = new SrsMp4TrackBox();
        if (true) {
            SrsMp4MediaBox *media = new SrsMp4MediaBox();
            SrsMp4HandlerReferenceBox *hdr = new SrsMp4HandlerReferenceBox();
            hdr->handler_type_ = SrsMp4HandlerTypeVIDE;
            media->set_hdlr(hdr);
            video->set_mdia(media);
        }
        box.add_trak(video);
        EXPECT_TRUE(video == box.video());
        EXPECT_EQ(1, box.nb_vide_tracks());

        SrsMp4TrackBox *audio = new SrsMp4TrackBox();
        if (true) {
            SrsMp4MediaBox *media = new SrsMp4MediaBox();
            SrsMp4HandlerReferenceBox *hdr = new SrsMp4HandlerReferenceBox();
            hdr->handler_type_ = SrsMp4HandlerTypeSOUN;
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
        char buf[12 + 20];
        SrsBuffer b(buf, sizeof(buf));

        if (true) {
            SrsMp4TrackExtendsBox box;
            box.track_ID_ = 1;
            box.default_sample_description_index_ = 2;
            box.default_sample_size_ = 3;
            box.default_sample_duration_ = 4;
            box.default_sample_flags_ = 5;
            EXPECT_EQ((int)sizeof(buf), (int)box.nb_bytes());
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
            EXPECT_EQ((int)box.track_ID_, 1);
            EXPECT_EQ((int)box.default_sample_description_index_, 2);
            EXPECT_EQ((int)box.default_sample_size_, 3);
            EXPECT_EQ((int)box.default_sample_duration_, 4);
            EXPECT_EQ((int)box.default_sample_flags_, 5);
        }
    }

    SrsMp4MovieExtendsBox box;

    SrsMp4TrackExtendsBox *trex = new SrsMp4TrackExtendsBox();
    box.add_trex(trex);
    EXPECT_TRUE(trex == box.get(SrsMp4BoxTypeTREX));
}

VOID TEST(KernelMp4Test, TKHDBox)
{
    srs_error_t err;

    if (true) {
        char buf[12 + 20 + 60];
        SrsBuffer b(buf, sizeof(buf));

        if (true) {
            SrsMp4TrackHeaderBox box;
            box.track_ID_ = 1;
            EXPECT_EQ((int)sizeof(buf), (int)box.nb_bytes());
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
            EXPECT_EQ((int)box.track_ID_, 1);
        }
    }

    if (true) {
        char buf[12 + 32 + 60];
        SrsBuffer b(buf, sizeof(buf));

        if (true) {
            SrsMp4TrackHeaderBox box;
            box.version_ = 1;
            box.track_ID_ = 1;
            EXPECT_EQ((int)sizeof(buf), (int)box.nb_bytes());
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
            EXPECT_EQ((int)box.track_ID_, 1);
        }
    }
}

VOID TEST(KernelMp4Test, ELSTBox)
{
    srs_error_t err;

    if (true) {
        char buf[12 + 4];
        SrsBuffer b(buf, sizeof(buf));

        if (true) {
            SrsMp4EditListBox box;
            EXPECT_EQ((int)sizeof(buf), (int)box.nb_bytes());
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
        char buf[12 + 4 + 12];
        SrsBuffer b(buf, sizeof(buf));

        if (true) {
            SrsMp4EditListBox box;
            if (true) {
                SrsMp4ElstEntry entry;
                box.entries_.push_back(entry);
            }
            EXPECT_EQ((int)sizeof(buf), (int)box.nb_bytes());
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
        SrsMp4HandlerReferenceBox *hdlr = new SrsMp4HandlerReferenceBox();
        box.set_hdlr(hdlr);
        EXPECT_TRUE(hdlr == box.hdlr());
    }
}

VOID TEST(KernelMp4Test, MDHDBox)
{
    srs_error_t err;

    if (true) {
        char buf[12 + 20];
        SrsBuffer b(buf, sizeof(buf));

        if (true) {
            SrsMp4MediaHeaderBox box;
            EXPECT_EQ((int)sizeof(buf), (int)box.nb_bytes());
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
        char buf[12 + 20];
        SrsBuffer b(buf, sizeof(buf));

        if (true) {
            SrsMp4MediaHeaderBox box;
            box.set_language0('C');
            box.set_language1('N');
            box.set_language2('E');
            EXPECT_EQ((int)sizeof(buf), (int)box.nb_bytes());
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
        box.handler_type_ = SrsMp4HandlerTypeVIDE;
        EXPECT_TRUE(box.is_video());
    }

    if (true) {
        SrsMp4HandlerReferenceBox box;
        box.handler_type_ = SrsMp4HandlerTypeSOUN;
        EXPECT_TRUE(box.is_audio());
    }
}

VOID TEST(KernelMp4Test, HDLRBox)
{
    srs_error_t err;

    if (true) {
        char buf[12 + 21];
        SrsBuffer b(buf, sizeof(buf));

        if (true) {
            SrsMp4HandlerReferenceBox box;
            box.handler_type_ = SrsMp4HandlerTypeSOUN;
            EXPECT_EQ((int)sizeof(buf), (int)box.nb_bytes());
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
            EXPECT_EQ(SrsMp4HandlerTypeSOUN, box.handler_type_);
        }
    }

    if (true) {
        char buf[12 + 21];
        SrsBuffer b(buf, sizeof(buf));

        if (true) {
            SrsMp4HandlerReferenceBox box;
            box.handler_type_ = SrsMp4HandlerTypeVIDE;
            EXPECT_EQ((int)sizeof(buf), (int)box.nb_bytes());
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
            EXPECT_EQ(SrsMp4HandlerTypeVIDE, box.handler_type_);
        }
    }

    if (true) {
        char buf[12 + 24];
        SrsBuffer b(buf, sizeof(buf));

        if (true) {
            SrsMp4HandlerReferenceBox box;
            box.handler_type_ = SrsMp4HandlerTypeVIDE;
            box.name_ = "srs";
            EXPECT_EQ((int)sizeof(buf), (int)box.nb_bytes());
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
            EXPECT_EQ(SrsMp4HandlerTypeVIDE, box.handler_type_);
        }
    }

    if (true) {
        SrsMp4MediaInformationBox box;
        SrsMp4VideoMeidaHeaderBox *vmhd = new SrsMp4VideoMeidaHeaderBox();
        box.set_vmhd(vmhd);
        EXPECT_TRUE(vmhd == box.vmhd());
    }

    if (true) {
        SrsMp4MediaInformationBox box;
        SrsMp4SoundMeidaHeaderBox *smhd = new SrsMp4SoundMeidaHeaderBox();
        box.set_smhd(smhd);
        EXPECT_TRUE(smhd == box.smhd());
    }

    if (true) {
        SrsMp4MediaInformationBox box;
        SrsMp4DataInformationBox *dinf = new SrsMp4DataInformationBox();
        box.set_dinf(dinf);
        EXPECT_TRUE(dinf == box.dinf());
    }

    if (true) {
        SrsMp4DataInformationBox box;
        SrsMp4DataReferenceBox *dref = new SrsMp4DataReferenceBox();
        box.set_dref(dref);
        EXPECT_TRUE(dref == box.dref());
    }
}

VOID TEST(KernelMp4Test, URLBox)
{
    srs_error_t err;

    if (true) {
        char buf[12 + 1];
        SrsBuffer b(buf, sizeof(buf));

        if (true) {
            SrsMp4DataEntryUrlBox box;
            EXPECT_EQ((int)sizeof(buf), (int)box.nb_bytes());
            HELPER_EXPECT_SUCCESS(box.encode(&b));

            stringstream ss;
            SrsMp4DumpContext dc;
            box.dumps(ss, dc);

            string v = ss.str();
            EXPECT_STREQ("url , 13B, FB(4B,V0,0x01), URL: Same file", v.c_str());
        }

        if (true) {
            b.skip(-1 * b.pos());
            SrsMp4DataEntryUrlBox box;
            HELPER_EXPECT_SUCCESS(box.decode(&b));
        }
    }

    if (true) {
        char buf[12 + 2];
        SrsBuffer b(buf, sizeof(buf));

        if (true) {
            SrsMp4DataEntryUrnBox box;
            EXPECT_EQ((int)sizeof(buf), (int)box.nb_bytes());
            HELPER_EXPECT_SUCCESS(box.encode(&b));

            stringstream ss;
            SrsMp4DumpContext dc;
            box.dumps(ss, dc);

            string v = ss.str();
            EXPECT_STREQ("urn , 14B, FB(4B,V0,0x01), URL: Same file", v.c_str());
        }

        if (true) {
            b.skip(-1 * b.pos());
            SrsMp4DataEntryUrnBox box;
            HELPER_EXPECT_SUCCESS(box.decode(&b));
        }
    }

    if (true) {
        SrsMp4DataReferenceBox box;
        SrsMp4DataEntryUrnBox *urn = new SrsMp4DataEntryUrnBox();
        box.append(urn);
        EXPECT_TRUE(urn == box.entry_at(0));
    }

    if (true) {
        char buf[12 + 4 + 12 + 2];
        SrsBuffer b(buf, sizeof(buf));

        if (true) {
            SrsMp4DataReferenceBox box;
            SrsMp4DataEntryUrnBox *urn = new SrsMp4DataEntryUrnBox();
            box.append(urn);
            EXPECT_EQ((int)sizeof(buf), (int)box.nb_bytes());
            HELPER_EXPECT_SUCCESS(box.encode(&b));

            stringstream ss;
            SrsMp4DumpContext dc;
            box.dumps(ss, dc);

            string v = ss.str();
            EXPECT_STREQ("dref, 30B, FB(4B), 1 childs(+)\n    urn , 14B, FB(4B,V0,0x01), URL: Same file\n", v.c_str());
        }

        if (true) {
            b.skip(-1 * b.pos());
            SrsMp4DataReferenceBox box;
            HELPER_EXPECT_SUCCESS(box.decode(&b));
        }
    }

    if (true) {
        SrsMp4SampleTableBox box;
        SrsMp4CompositionTime2SampleBox *ctts = new SrsMp4CompositionTime2SampleBox();
        box.set_ctts(ctts);
        EXPECT_TRUE(ctts == box.ctts());
    }
}

VOID TEST(KernelMp4Test, SampleDescBox)
{
    srs_error_t err;

    if (true) {
        char buf[8 + 8 + 70];
        SrsBuffer b(buf, sizeof(buf));

        if (true) {
            SrsMp4VisualSampleEntry box = SrsMp4VisualSampleEntry(SrsMp4BoxTypeAVC1);
            box.data_reference_index_ = 1;
            EXPECT_EQ((int)sizeof(buf), (int)box.nb_bytes());
            HELPER_EXPECT_SUCCESS(box.encode(&b));

            stringstream ss;
            SrsMp4DumpContext dc;
            box.dumps(ss, dc);

            string v = ss.str();
            EXPECT_STREQ("avc1, 86B, refs#1, size=0x0\n", v.c_str());
        }

        if (true) {
            b.skip(-1 * b.pos());
            SrsMp4VisualSampleEntry box = SrsMp4VisualSampleEntry(SrsMp4BoxTypeAVC1);
            HELPER_EXPECT_SUCCESS(box.decode(&b));
        }
    }

    if (true) {
        char buf[8];
        SrsBuffer b(buf, sizeof(buf));

        if (true) {
            SrsMp4AvccBox box;
            EXPECT_EQ((int)sizeof(buf), (int)box.nb_bytes());
            HELPER_EXPECT_SUCCESS(box.encode(&b));

            stringstream ss;
            SrsMp4DumpContext dc;
            box.dumps(ss, dc);

            string v = ss.str();
            EXPECT_STREQ("avcC, 8B, AVC Config: 0B\n    \n", v.c_str());
        }

        if (true) {
            b.skip(-1 * b.pos());
            SrsMp4AvccBox box;
            HELPER_EXPECT_SUCCESS(box.decode(&b));
        }
    }

    if (true) {
        char buf[8 + 8 + 70];
        SrsBuffer b(buf, sizeof(buf));

        if (true) {
            SrsMp4VisualSampleEntry box = SrsMp4VisualSampleEntry(SrsMp4BoxTypeHEV1);
            box.data_reference_index_ = 1;
            EXPECT_EQ((int)sizeof(buf), (int)box.nb_bytes());
            HELPER_EXPECT_SUCCESS(box.encode(&b));

            stringstream ss;
            SrsMp4DumpContext dc;
            box.dumps(ss, dc);

            string v = ss.str();
            EXPECT_STREQ("hev1, 86B, refs#1, size=0x0\n", v.c_str());
        }

        if (true) {
            b.skip(-1 * b.pos());
            SrsMp4VisualSampleEntry box = SrsMp4VisualSampleEntry(SrsMp4BoxTypeHEV1);
            HELPER_EXPECT_SUCCESS(box.decode(&b));
        }
    }

    if (true) {
        char buf[8];
        SrsBuffer b(buf, sizeof(buf));

        if (true) {
            SrsMp4HvcCBox box;
            EXPECT_EQ((int)sizeof(buf), (int)box.nb_bytes());
            HELPER_EXPECT_SUCCESS(box.encode(&b));

            stringstream ss;
            SrsMp4DumpContext dc;
            box.dumps(ss, dc);

            string v = ss.str();
            EXPECT_STREQ("hvcC, 8B, HEVC Config: 0B\n    \n", v.c_str());
        }

        if (true) {
            b.skip(-1 * b.pos());
            SrsMp4HvcCBox box;
            HELPER_EXPECT_SUCCESS(box.decode(&b));
        }
    }

    if (true) {
        char buf[8 + 8 + 20];
        SrsBuffer b(buf, sizeof(buf));

        if (true) {
            SrsMp4AudioSampleEntry box;
            box.data_reference_index_ = 1;
            EXPECT_EQ((int)sizeof(buf), (int)box.nb_bytes());
            HELPER_EXPECT_SUCCESS(box.encode(&b));

            stringstream ss;
            SrsMp4DumpContext dc;
            box.dumps(ss, dc);

            string v = ss.str();
            EXPECT_STREQ("mp4a, 36B, refs#1, 2 channels, 16 bits, 0 Hz\n", v.c_str());
        }

        if (true) {
            b.skip(-1 * b.pos());
            SrsMp4AudioSampleEntry box;
            HELPER_EXPECT_SUCCESS(box.decode(&b));
        }
    }
}

VOID TEST(KernelMp4Test, SpecificInfoBox)
{
    srs_error_t err;

    if (true) {
        char buf[2 + 2];
        SrsBuffer b(buf, sizeof(buf));

        if (true) {
            SrsMp4DecoderSpecificInfo box;
            box.asc_.resize(2);
            EXPECT_EQ((int)sizeof(buf), (int)box.nb_bytes());
            HELPER_EXPECT_SUCCESS(box.encode(&b));

            stringstream ss;
            SrsMp4DumpContext dc;
            box.dumps_detail(ss, dc);

            string v = ss.str();
            EXPECT_STREQ(", tag=0x05, ASC 2B\n    0x00, 0x00", v.c_str());
        }

        if (true) {
            b.skip(-1 * b.pos());
            SrsMp4DecoderSpecificInfo box;
            HELPER_EXPECT_SUCCESS(box.decode(&b));
        }
    }

    if (true) {
        char buf[2 + 13];
        SrsBuffer b(buf, sizeof(buf));

        if (true) {
            SrsMp4DecoderConfigDescriptor box;
            EXPECT_EQ((int)sizeof(buf), (int)box.nb_bytes());
            HELPER_EXPECT_SUCCESS(box.encode(&b));

            stringstream ss;
            SrsMp4DumpContext dc;
            box.dumps_detail(ss, dc);

            string v = ss.str();
            EXPECT_STREQ(", tag=0x04, type=0, stream=0\n    decoder specific", v.c_str());
        }

        if (true) {
            b.skip(-1 * b.pos());
            SrsMp4DecoderConfigDescriptor box;
            HELPER_EXPECT_SUCCESS(box.decode(&b));
        }
    }

    if (true) {
        char buf[2 + 21];
        SrsBuffer b(buf, sizeof(buf));

        if (true) {
            SrsMp4ES_Descriptor box;
            EXPECT_EQ((int)sizeof(buf), (int)box.nb_bytes());
            HELPER_EXPECT_SUCCESS(box.encode(&b));

            stringstream ss;
            SrsMp4DumpContext dc;
            box.dumps_detail(ss, dc);

            string v = ss.str();
            EXPECT_STREQ(", tag=0x03, ID=0\n    decoder config, tag=0x04, type=0, stream=0\n        decoder specific", v.c_str());
        }

        if (true) {
            b.skip(-1 * b.pos());
            SrsMp4DecoderConfigDescriptor box;
            HELPER_EXPECT_SUCCESS(box.decode(&b));
        }
    }
}

VOID TEST(KernelMp4Test, STSDBox)
{
    srs_error_t err;

    if (true) {
        char buf[32];
        SrsBuffer b(buf, sizeof(buf));

        if (true) {
            SrsMp4SampleDescriptionBox box;
            box.entries_.push_back(new SrsMp4SampleEntry());
            EXPECT_EQ((int)sizeof(buf), (int)box.nb_bytes());
            HELPER_EXPECT_SUCCESS(box.encode(&b));

            stringstream ss;
            SrsMp4DumpContext dc;
            box.dumps_detail(ss, dc);

            string v = ss.str();
            EXPECT_STREQ(", FB(4B), 1 childs(+)\n    ", v.c_str());
        }

        if (true) {
            b.skip(-1 * b.pos());
            SrsMp4SampleDescriptionBox box;
            HELPER_EXPECT_SUCCESS(box.decode(&b));
        }
    }

    if (true) {
        char buf[24];
        SrsBuffer b(buf, sizeof(buf));

        if (true) {
            SrsMp4DecodingTime2SampleBox box;
            box.entries_.push_back(SrsMp4SttsEntry());
            EXPECT_EQ((int)sizeof(buf), (int)box.nb_bytes());
            HELPER_EXPECT_SUCCESS(box.encode(&b));

            stringstream ss;
            SrsMp4DumpContext dc;
            box.dumps_detail(ss, dc);

            string v = ss.str();
            EXPECT_STREQ(", FB(4B), 1 childs (+)\n    count=0, delta=0", v.c_str());
        }

        if (true) {
            b.skip(-1 * b.pos());
            SrsMp4DecodingTime2SampleBox box;
            HELPER_EXPECT_SUCCESS(box.decode(&b));
        }
    }

    if (true) {
        char buf[24];
        SrsBuffer b(buf, sizeof(buf));

        if (true) {
            SrsMp4CompositionTime2SampleBox box;
            box.entries_.push_back(SrsMp4CttsEntry());
            EXPECT_EQ((int)sizeof(buf), (int)box.nb_bytes());
            HELPER_EXPECT_SUCCESS(box.encode(&b));

            stringstream ss;
            SrsMp4DumpContext dc;
            box.dumps_detail(ss, dc);

            string v = ss.str();
            EXPECT_STREQ(", FB(4B), 1 childs (+)\n    count=0, offset=0", v.c_str());
        }

        if (true) {
            b.skip(-1 * b.pos());
            SrsMp4CompositionTime2SampleBox box;
            HELPER_EXPECT_SUCCESS(box.decode(&b));
        }
    }

    if (true) {
        char buf[16];
        SrsBuffer b(buf, sizeof(buf));

        if (true) {
            SrsMp4SyncSampleBox box;
            EXPECT_EQ((int)sizeof(buf), (int)box.nb_bytes());
            HELPER_EXPECT_SUCCESS(box.encode(&b));

            stringstream ss;
            SrsMp4DumpContext dc;
            box.dumps_detail(ss, dc);

            string v = ss.str();
            EXPECT_STREQ(", FB(4B), count=0", v.c_str());
        }

        if (true) {
            b.skip(-1 * b.pos());
            SrsMp4SyncSampleBox box;
            HELPER_EXPECT_SUCCESS(box.decode(&b));
        }
    }

    if (true) {
        char buf[16];
        SrsBuffer b(buf, sizeof(buf));

        if (true) {
            SrsMp4Sample2ChunkBox box;
            EXPECT_EQ((int)sizeof(buf), (int)box.nb_bytes());
            HELPER_EXPECT_SUCCESS(box.encode(&b));

            stringstream ss;
            SrsMp4DumpContext dc;
            box.dumps_detail(ss, dc);

            string v = ss.str();
            EXPECT_STREQ(", FB(4B), 0 childs (+)", v.c_str());
        }

        if (true) {
            b.skip(-1 * b.pos());
            SrsMp4Sample2ChunkBox box;
            HELPER_EXPECT_SUCCESS(box.decode(&b));
        }
    }

    if (true) {
        char buf[16];
        SrsBuffer b(buf, sizeof(buf));

        if (true) {
            SrsMp4ChunkOffsetBox box;
            EXPECT_EQ((int)sizeof(buf), (int)box.nb_bytes());
            HELPER_EXPECT_SUCCESS(box.encode(&b));

            stringstream ss;
            SrsMp4DumpContext dc;
            box.dumps_detail(ss, dc);

            string v = ss.str();
            EXPECT_STREQ(", FB(4B), 0 childs (+)", v.c_str());
        }

        if (true) {
            b.skip(-1 * b.pos());
            SrsMp4ChunkOffsetBox box;
            HELPER_EXPECT_SUCCESS(box.decode(&b));
        }
    }

    if (true) {
        char buf[16];
        SrsBuffer b(buf, sizeof(buf));

        if (true) {
            SrsMp4ChunkLargeOffsetBox box;
            EXPECT_EQ((int)sizeof(buf), (int)box.nb_bytes());
            HELPER_EXPECT_SUCCESS(box.encode(&b));

            stringstream ss;
            SrsMp4DumpContext dc;
            box.dumps_detail(ss, dc);

            string v = ss.str();
            EXPECT_STREQ(", FB(4B), 0 childs (+)", v.c_str());
        }

        if (true) {
            b.skip(-1 * b.pos());
            SrsMp4ChunkLargeOffsetBox box;
            HELPER_EXPECT_SUCCESS(box.decode(&b));
        }
    }

    if (true) {
        char buf[20];
        SrsBuffer b(buf, sizeof(buf));

        if (true) {
            SrsMp4SampleSizeBox box;
            EXPECT_EQ((int)sizeof(buf), (int)box.nb_bytes());
            HELPER_EXPECT_SUCCESS(box.encode(&b));

            stringstream ss;
            SrsMp4DumpContext dc;
            box.dumps_detail(ss, dc);

            string v = ss.str();
            EXPECT_STREQ(", FB(4B), size=0, 0 childs (+)", v.c_str());
        }

        if (true) {
            b.skip(-1 * b.pos());
            SrsMp4SampleSizeBox box;
            HELPER_EXPECT_SUCCESS(box.decode(&b));
        }
    }

    if (true) {
        char buf[10];
        SrsBuffer b(buf, sizeof(buf));

        if (true) {
            SrsMp4UserDataBox box;
            box.data_.resize(2);
            EXPECT_EQ((int)sizeof(buf), (int)box.nb_bytes());
            HELPER_EXPECT_SUCCESS(box.encode(&b));

            stringstream ss;
            SrsMp4DumpContext dc;
            box.dumps_detail(ss, dc);

            string v = ss.str();
            EXPECT_STREQ(", total 2B\n    0x00, 0x00", v.c_str());
        }

        if (true) {
            b.skip(-1 * b.pos());
            SrsMp4UserDataBox box;
            HELPER_EXPECT_SUCCESS(box.decode(&b));
        }
    }

    if (true) {
        char buf[32];
        SrsBuffer b(buf, sizeof(buf));

        if (true) {
            SrsMp4SegmentIndexBox box;
            EXPECT_EQ((int)sizeof(buf), (int)box.nb_bytes());
            HELPER_EXPECT_SUCCESS(box.encode(&b));

            stringstream ss;
            SrsMp4DumpContext dc;
            box.dumps_detail(ss, dc);

            string v = ss.str();
            EXPECT_TRUE(v.length() > 0);
        }

        if (true) {
            b.skip(-1 * b.pos());
            SrsMp4SegmentIndexBox box;
            HELPER_EXPECT_SUCCESS(box.decode(&b));
        }
    }

    if (true) {
        char buf[108];
        SrsBuffer b(buf, sizeof(buf));

        if (true) {
            SrsMp4MovieHeaderBox box;
            EXPECT_EQ((int)sizeof(buf), (int)box.nb_bytes());
            HELPER_EXPECT_SUCCESS(box.encode(&b));

            stringstream ss;
            SrsMp4DumpContext dc;
            box.dumps_detail(ss, dc);

            string v = ss.str();
            EXPECT_STREQ(", FB(4B), 0ms, TBN=0, nTID=0", v.c_str());
        }

        if (true) {
            b.skip(-1 * b.pos());
            SrsMp4MovieHeaderBox box;
            HELPER_EXPECT_SUCCESS(box.decode(&b));
        }
    }

    if (true) {
        SrsMp4TrackBox box;
        SrsMp4TrackHeaderBox *tkhd = new SrsMp4TrackHeaderBox();
        box.set_tkhd(tkhd);
        EXPECT_TRUE(tkhd == box.tkhd());
    }

    if (true) {
        char buf[16];
        SrsBuffer b(buf, sizeof(buf));

        if (true) {
            SrsMp4CompositionTime2SampleBox box;
            EXPECT_EQ((int)sizeof(buf), (int)box.nb_bytes());
            HELPER_EXPECT_SUCCESS(box.encode(&b));

            stringstream ss;
            SrsMp4DumpContext dc;
            box.dumps_detail(ss, dc);

            string v = ss.str();
            EXPECT_STREQ(", FB(4B), 0 childs (+)", v.c_str());
        }

        if (true) {
            b.skip(-1 * b.pos());
            SrsMp4CompositionTime2SampleBox box;
            HELPER_EXPECT_SUCCESS(box.decode(&b));
        }
    }
}

VOID TEST(KernelMp4Test, SAIZBox)
{
    srs_error_t err;
    // flags & 1 == 0; default_sample_info_size == 1
    if (true) {
        SrsMp4SampleAuxiliaryInfoSizeBox saiz;

        uint8_t data[12 + 5];
        SrsBuffer b((char *)data, sizeof(data));
        b.write_4bytes(16);
        b.write_4bytes(SrsMp4BoxTypeSAIZ);
        b.write_1bytes(0);
        b.write_3bytes(0);
        b.write_1bytes(1);
        b.write_4bytes(0);
        b.skip(-17);

        HELPER_ASSERT_SUCCESS(saiz.decode(&b));
        EXPECT_EQ(17, (int)saiz.nb_header());
        EXPECT_EQ(0, (int)saiz.version_);
        EXPECT_EQ(0, (int)saiz.flags_);
        EXPECT_EQ(1, (int)saiz.default_sample_info_size_);
        EXPECT_EQ(0, (int)saiz.sample_count_);
        EXPECT_EQ(0, saiz.sample_info_sizes_.size());
    }

    // flags & 1 == 1; default_sample_info_size == 1
    if (true) {
        SrsMp4SampleAuxiliaryInfoSizeBox saiz;

        uint8_t data[12 + 13];
        SrsBuffer b((char *)data, sizeof(data));
        b.write_4bytes(16);
        b.write_4bytes(SrsMp4BoxTypeSAIZ);
        b.write_1bytes(0);
        b.write_3bytes(1);
        b.write_4bytes(1);
        b.write_4bytes(2);
        b.write_1bytes(1);
        b.write_4bytes(0);
        b.skip(-25);

        HELPER_ASSERT_SUCCESS(saiz.decode(&b));
        EXPECT_EQ(25, (int)saiz.nb_header());
        EXPECT_EQ(0, (int)saiz.version_);
        EXPECT_EQ(1, (int)saiz.flags_);
        EXPECT_EQ(1, (int)saiz.aux_info_type_);
        EXPECT_EQ(2, (int)saiz.aux_info_type_parameter_);
        EXPECT_EQ(1, (int)saiz.default_sample_info_size_);
        EXPECT_EQ(0, (int)saiz.sample_count_);
        EXPECT_EQ(0, saiz.sample_info_sizes_.size());
    }

    // flags & 1 == 1; default_sample_info_size == 0; sample_count = 3;
    if (true) {
        SrsMp4SampleAuxiliaryInfoSizeBox saiz;

        uint8_t data[12 + 16];
        SrsBuffer b((char *)data, sizeof(data));
        b.write_4bytes(16);
        b.write_4bytes(SrsMp4BoxTypeSAIZ);
        b.write_1bytes(0);
        b.write_3bytes(1);
        b.write_4bytes(1);
        b.write_4bytes(2);
        b.write_1bytes(0);
        b.write_4bytes(3);
        b.write_1bytes(4);
        b.write_1bytes(5);
        b.write_1bytes(6);
        b.skip(-28);

        HELPER_ASSERT_SUCCESS(saiz.decode(&b));
        EXPECT_EQ(28, (int)saiz.nb_header());
        EXPECT_EQ(0, (int)saiz.version_);
        EXPECT_EQ(1, (int)saiz.flags_);
        EXPECT_EQ(1, (int)saiz.aux_info_type_);
        EXPECT_EQ(2, (int)saiz.aux_info_type_parameter_);
        EXPECT_EQ(0, (int)saiz.default_sample_info_size_);
        EXPECT_EQ(3, (int)saiz.sample_count_);
        EXPECT_EQ(3, saiz.sample_info_sizes_.size());
        EXPECT_EQ(4, saiz.sample_info_sizes_[0]);
        EXPECT_EQ(5, saiz.sample_info_sizes_[1]);
        EXPECT_EQ(6, saiz.sample_info_sizes_[2]);
    }

    if (true) {
        SrsMp4SampleAuxiliaryInfoSizeBox saiz;
        saiz.flags_ = 0;
        saiz.default_sample_info_size_ = 1;
        saiz.sample_count_ = 0;

        EXPECT_EQ(17, saiz.nb_header());

        stringstream ss;
        SrsMp4DumpContext dc;
        saiz.dumps_detail(ss, dc);
        string v = ss.str();
        EXPECT_STREQ("default_sample_info_size=1, sample_count=0", v.c_str());
    }

    if (true) {
        SrsMp4SampleAuxiliaryInfoSizeBox saiz;
        saiz.flags_ = 1;
        saiz.default_sample_info_size_ = 1;
        saiz.sample_count_ = 0;

        EXPECT_EQ(25, saiz.nb_header());
        stringstream ss;
        SrsMp4DumpContext dc;
        saiz.dumps_detail(ss, dc);
        string v = ss.str();
        EXPECT_STREQ("default_sample_info_size=1, sample_count=0", v.c_str());
    }

    if (true) {
        SrsMp4SampleAuxiliaryInfoSizeBox saiz;
        saiz.flags_ = 1;
        saiz.default_sample_info_size_ = 0;
        saiz.sample_count_ = 1;
        saiz.sample_info_sizes_.push_back(4);

        EXPECT_EQ(26, saiz.nb_header());
        stringstream ss;
        SrsMp4DumpContext dc;
        saiz.dumps_detail(ss, dc);
        string v = ss.str();
        EXPECT_STREQ("default_sample_info_size=0, sample_count=1", v.c_str());
    }
}

VOID TEST(KernelMp4Test, SAIOBox)
{
    srs_error_t err;

    if (true) {
        SrsMp4SampleAuxiliaryInfoOffsetBox saio;

        uint8_t data[12 + 8];
        SrsBuffer b((char *)data, sizeof(data));
        b.write_4bytes(16);
        b.write_4bytes(SrsMp4BoxTypeSAIO);
        b.write_1bytes(0);
        b.write_3bytes(0);
        b.write_4bytes(1);
        b.write_4bytes(2);
        b.skip(-20);

        HELPER_ASSERT_SUCCESS(saio.decode(&b));
        EXPECT_EQ(20, (int)saio.nb_header());
        EXPECT_EQ(0, (int)saio.version_);
        EXPECT_EQ(0, (int)saio.flags_);
        EXPECT_EQ(1, (int)saio.offsets_.size());
        EXPECT_EQ(2, (int)saio.offsets_[0]);
    }

    if (true) {
        SrsMp4SampleAuxiliaryInfoOffsetBox saio;

        uint8_t data[12 + 16];
        SrsBuffer b((char *)data, sizeof(data));
        b.write_4bytes(16);
        b.write_4bytes(SrsMp4BoxTypeSAIO);
        b.write_1bytes(0);
        b.write_3bytes(1);
        b.write_4bytes(1);
        b.write_4bytes(2);
        b.write_4bytes(1);
        b.write_4bytes(2);
        b.skip(-28);

        HELPER_ASSERT_SUCCESS(saio.decode(&b));
        EXPECT_EQ(28, (int)saio.nb_header());
        EXPECT_EQ(0, (int)saio.version_);
        EXPECT_EQ(1, (int)saio.flags_);
        EXPECT_EQ(1, (int)saio.aux_info_type_);
        EXPECT_EQ(2, (int)saio.aux_info_type_parameter_);
        EXPECT_EQ(1, (int)saio.offsets_.size());
        EXPECT_EQ(2, (int)saio.offsets_[0]);
    }

    if (true) {
        SrsMp4SampleAuxiliaryInfoOffsetBox saio;

        uint8_t data[12 + 20];
        SrsBuffer b((char *)data, sizeof(data));
        b.write_4bytes(16);
        b.write_4bytes(SrsMp4BoxTypeSAIO);
        b.write_1bytes(1);
        b.write_3bytes(1);
        b.write_4bytes(1);
        b.write_4bytes(2);
        b.write_4bytes(1);
        b.write_8bytes(2);
        b.skip(-32);

        HELPER_ASSERT_SUCCESS(saio.decode(&b));
        EXPECT_EQ(32, (int)saio.nb_header());
        EXPECT_EQ(1, (int)saio.version_);
        EXPECT_EQ(1, (int)saio.flags_);
        EXPECT_EQ(1, (int)saio.aux_info_type_);
        EXPECT_EQ(2, (int)saio.aux_info_type_parameter_);
        EXPECT_EQ(1, (int)saio.offsets_.size());
        EXPECT_EQ(2, (int)saio.offsets_[0]);
    }

    if (true) {
        SrsMp4SampleAuxiliaryInfoOffsetBox saio;
        saio.version_ = 0;
        saio.flags_ = 0;
        saio.offsets_.push_back(2);
        EXPECT_EQ(20, (int)saio.nb_header());

        stringstream ss;
        SrsMp4DumpContext dc;
        saio.dumps_detail(ss, dc);
        string v = ss.str();
        EXPECT_STREQ("entry_count=1", v.c_str());
    }

    if (true) {
        SrsMp4SampleAuxiliaryInfoOffsetBox saio;
        saio.version_ = 0;
        saio.flags_ = 1;
        saio.offsets_.push_back(2);
        EXPECT_EQ(28, (int)saio.nb_header());

        stringstream ss;
        SrsMp4DumpContext dc;
        saio.dumps_detail(ss, dc);
        string v = ss.str();
        EXPECT_STREQ("entry_count=1", v.c_str());
    }

    if (true) {
        SrsMp4SampleAuxiliaryInfoOffsetBox saio;
        saio.version_ = 1;
        saio.flags_ = 1;
        saio.offsets_.push_back(2);
        EXPECT_EQ(32, (int)saio.nb_header());

        stringstream ss;
        SrsMp4DumpContext dc;
        saio.dumps_detail(ss, dc);
        string v = ss.str();
        EXPECT_STREQ("entry_count=1", v.c_str());
    }
}

VOID TEST(KernelMp4Test, SENCBox)
{
    srs_error_t err;

    if (true) {
        SrsMp4SampleEncryptionBox senc(8);

        uint8_t data[12 + 4];
        SrsBuffer b((char *)data, sizeof(data));
        b.write_4bytes(16);
        b.write_4bytes(SrsMp4BoxTypeSENC);
        b.write_1bytes(0);
        b.write_3bytes(0);
        b.write_4bytes(0);
        b.skip(-16);

        HELPER_ASSERT_SUCCESS(senc.decode(&b));
        EXPECT_EQ(16, (int)senc.nb_header());
        EXPECT_EQ(0, (int)senc.version_);
        EXPECT_EQ(0, (int)senc.flags_);
        EXPECT_EQ(0, (int)senc.entries_.size());
    }

    if (true) {
        SrsMp4SampleEncryptionBox senc(8);

        uint8_t data[12 + 12];
        SrsBuffer b((char *)data, sizeof(data));
        b.write_4bytes(16);
        b.write_4bytes(SrsMp4BoxTypeSENC);
        b.write_1bytes(0);
        b.write_3bytes(0);
        b.write_4bytes(1);
        b.write_8bytes(1);
        b.skip(-24);

        HELPER_ASSERT_SUCCESS(senc.decode(&b));
        EXPECT_EQ(24, (int)senc.nb_header());
        EXPECT_EQ(0, (int)senc.version_);
        EXPECT_EQ(0, (int)senc.flags_);
        EXPECT_EQ(1, (int)senc.entries_.size());
    }
}

VOID TEST(KernelMp4Test, FRMABox)
{
    srs_error_t err;

    if (true) {
        SrsMp4OriginalFormatBox frma(1);
        uint8_t data[8 + 4];
        SrsBuffer b((char *)data, sizeof(data));
        b.write_4bytes(0);
        b.write_4bytes(SrsMp4BoxTypeFRMA);
        b.write_4bytes(1);
        b.skip(-12);

        HELPER_ASSERT_SUCCESS(frma.decode(&b));
        EXPECT_EQ(12, (int)frma.nb_header());
    }

    if (true) {
        SrsMp4OriginalFormatBox frma(1);
        EXPECT_EQ(12, (int)frma.nb_header());

        stringstream ss;
        SrsMp4DumpContext dc;
        frma.dumps_detail(ss, dc);
        string v = ss.str();
        EXPECT_STREQ("original format=1\n", v.c_str());
    }
}

VOID TEST(KernelMp4Test, SCHMBox)
{
    srs_error_t err;

    if (true) {
        SrsMp4SchemeTypeBox schm;
        uint8_t data[12 + 8];
        SrsBuffer b((char *)data, sizeof(data));
        b.write_4bytes(16);
        b.write_4bytes(SrsMp4BoxTypeSCHM);
        b.write_1bytes(0);
        b.write_3bytes(0);
        b.write_4bytes(1);
        b.write_4bytes(2);
        b.skip(-20);

        HELPER_ASSERT_SUCCESS(schm.decode(&b));
        EXPECT_EQ(20, (int)schm.nb_header());
        EXPECT_EQ(0, (int)schm.version_);
        EXPECT_EQ(0, (int)schm.flags_);
        EXPECT_EQ(1, (int)schm.scheme_type_);
        EXPECT_EQ(2, (int)schm.scheme_version_);
    }

    if (true) {
        SrsMp4SchemeTypeBox schm;
        uint8_t data[12 + 8 + 4];
        SrsBuffer b((char *)data, sizeof(data));
        b.write_4bytes(16);
        b.write_4bytes(SrsMp4BoxTypeSCHM);
        b.write_1bytes(0);
        b.write_3bytes(1);
        b.write_4bytes(1);
        b.write_4bytes(2);
        b.write_1bytes(65);
        b.write_1bytes(65);
        b.write_1bytes(65);
        b.write_1bytes(0);
        b.skip(-24);

        HELPER_ASSERT_SUCCESS(schm.decode(&b));
        EXPECT_EQ(24, (int)schm.nb_header());
        EXPECT_EQ(0, (int)schm.version_);
        EXPECT_EQ(1, (int)schm.flags_);
        EXPECT_EQ(1, (int)schm.scheme_type_);
        EXPECT_EQ(2, (int)schm.scheme_version_);

        stringstream ss;
        SrsMp4DumpContext dc;
        schm.dumps_detail(ss, dc);
        string v = ss.str();
        EXPECT_STREQ("scheme_type=1, scheme_version=2\nscheme_uri=AAA\n", v.c_str());
    }
}

VOID TEST(KernelMp4Test, SrsInitMp4Segment)
{
    srs_error_t err;
    // write single video segment
    if (true) {
        MockSrsFileWriter fw;
        SrsInitMp4Segment segment(&fw);

        segment.set_path("/tmp/init.mp4");
        SrsFormat fmt;
        HELPER_ASSERT_SUCCESS(fmt.initialize());

        uint8_t raw[] = {
            0x17,
            0x00, 0x00, 0x00, 0x00, 0x01, 0x64, 0x00, 0x20, 0xff, 0xe1, 0x00, 0x19, 0x67, 0x64, 0x00, 0x20,
            0xac, 0xd9, 0x40, 0xc0, 0x29, 0xb0, 0x11, 0x00, 0x00, 0x03, 0x00, 0x01, 0x00, 0x00, 0x03, 0x00,
            0x32, 0x0f, 0x18, 0x31, 0x96, 0x01, 0x00, 0x05, 0x68, 0xeb, 0xec, 0xb2, 0x2c};
        HELPER_ASSERT_SUCCESS(fmt.on_video(0, (char *)raw, sizeof(raw)));

        HELPER_ASSERT_SUCCESS(segment.write_video_only(&fmt, 1));
        EXPECT_TRUE(fw.filesize() > 0);
    }

    // write single audio segment
    if (true) {
        MockSrsFileWriter fw;
        SrsInitMp4Segment segment(&fw);

        segment.set_path("/tmp/init.mp4");
        SrsFormat fmt;
        HELPER_ASSERT_SUCCESS(fmt.initialize());

        uint8_t raw[] = {
            0xaf, 0x00, 0x12, 0x10};

        HELPER_ASSERT_SUCCESS(fmt.on_audio(0, (char *)raw, sizeof(raw)));

        HELPER_ASSERT_SUCCESS(segment.write_audio_only(&fmt, 1));
        EXPECT_TRUE(fw.filesize() > 0);
    }

    // write both audio and video segment
    if (true) {
        MockSrsFileWriter fw;
        SrsInitMp4Segment segment(&fw);

        segment.set_path("/tmp/init.mp4");
        SrsFormat fmt;
        HELPER_ASSERT_SUCCESS(fmt.initialize());

        uint8_t video_raw[] = {
            0x17,
            0x00, 0x00, 0x00, 0x00, 0x01, 0x64, 0x00, 0x20, 0xff, 0xe1, 0x00, 0x19, 0x67, 0x64, 0x00, 0x20,
            0xac, 0xd9, 0x40, 0xc0, 0x29, 0xb0, 0x11, 0x00, 0x00, 0x03, 0x00, 0x01, 0x00, 0x00, 0x03, 0x00,
            0x32, 0x0f, 0x18, 0x31, 0x96, 0x01, 0x00, 0x05, 0x68, 0xeb, 0xec, 0xb2, 0x2c};

        HELPER_ASSERT_SUCCESS(fmt.on_video(0, (char *)video_raw, sizeof(video_raw)));

        uint8_t audio_raw[] = {
            0xaf, 0x00, 0x12, 0x10};

        HELPER_ASSERT_SUCCESS(fmt.on_audio(0, (char *)audio_raw, sizeof(audio_raw)));

        HELPER_ASSERT_SUCCESS(segment.write(&fmt, 1, 2));
        EXPECT_TRUE(fw.filesize() > 0);
    }
}

VOID TEST(KernelMp4Test, SrsFmp4SegmentEncoder)
{
    srs_error_t err;

    if (true) {
        MockSrsFileWriter fw;
        SrsFmp4SegmentEncoder encoder;
        encoder.initialize(&fw, 0, 0, 1, 2);

        SrsFormat video_fmt;
        HELPER_ASSERT_SUCCESS(video_fmt.initialize());

        SrsFormat audio_fmt;
        HELPER_ASSERT_SUCCESS(audio_fmt.initialize());

        uint8_t video_raw[] = {
            0x17,
            0x00, 0x00, 0x00, 0x00, 0x01, 0x64, 0x00, 0x20, 0xff, 0xe1, 0x00, 0x19, 0x67, 0x64, 0x00, 0x20,
            0xac, 0xd9, 0x40, 0xc0, 0x29, 0xb0, 0x11, 0x00, 0x00, 0x03, 0x00, 0x01, 0x00, 0x00, 0x03, 0x00,
            0x32, 0x0f, 0x18, 0x31, 0x96, 0x01, 0x00, 0x05, 0x68, 0xeb, 0xec, 0xb2, 0x2c};

        HELPER_ASSERT_SUCCESS(video_fmt.on_video(0, (char *)video_raw, sizeof(video_raw)));

        uint8_t audio_raw[] = {
            0xaf, 0x00, 0x12, 0x10};

        HELPER_ASSERT_SUCCESS(audio_fmt.on_audio(0, (char *)audio_raw, sizeof(audio_raw)));

        SrsVideoAvcFrameType video_frame_type = video_fmt.video_->frame_type_;
        uint32_t cts = (uint32_t)video_fmt.video_->cts_;

        uint32_t dts = 0;
        uint32_t pts = dts + cts;

        uint8_t *video_sample = (uint8_t *)video_fmt.raw_;
        uint32_t nb_video_sample = (uint32_t)video_fmt.nb_raw_;
        encoder.write_sample(SrsMp4HandlerTypeVIDE, video_frame_type, dts, pts, video_sample, nb_video_sample);
        uint8_t *audio_sample = (uint8_t *)audio_fmt.raw_;
        uint32_t nb_audio_sample = (uint32_t)audio_fmt.nb_raw_;
        encoder.write_sample(SrsMp4HandlerTypeSOUN, 0, 0, 0, audio_sample, nb_audio_sample);
        encoder.flush(dts);
        EXPECT_TRUE(fw.filesize() > 0);
    }
}

VOID TEST(KernelMp4Test, SrsMp4M2tsInitEncoder)
{
    srs_error_t err;

    if (true) {
        MockSrsFileWriter fw;
        HELPER_ASSERT_SUCCESS(fw.open("test.mp4"));

        SrsMp4M2tsInitEncoder enc;
        HELPER_ASSERT_SUCCESS(enc.initialize(&fw));

        SrsFormat fmt;
        EXPECT_TRUE(srs_success == fmt.initialize());

        uint8_t raw[] = {
            0x17,
            0x00, 0x00, 0x00, 0x00, 0x01, 0x64, 0x00, 0x20, 0xff, 0xe1, 0x00, 0x19, 0x67, 0x64, 0x00, 0x20,
            0xac, 0xd9, 0x40, 0xc0, 0x29, 0xb0, 0x11, 0x00, 0x00, 0x03, 0x00, 0x01, 0x00, 0x00, 0x03, 0x00,
            0x32, 0x0f, 0x18, 0x31, 0x96, 0x01, 0x00, 0x05, 0x68, 0xeb, 0xec, 0xb2, 0x2c};
        EXPECT_TRUE(srs_success == fmt.on_video(0, (char *)raw, sizeof(raw)));

        HELPER_ASSERT_SUCCESS(enc.write(&fmt, true, 1));
        EXPECT_TRUE(fw.filesize() > 0);
    }

    if (true) {
        MockSrsFileWriter fw;
        HELPER_ASSERT_SUCCESS(fw.open("test.mp4"));

        SrsMp4M2tsInitEncoder enc;
        HELPER_ASSERT_SUCCESS(enc.initialize(&fw));

        SrsFormat fmt;
        EXPECT_TRUE(srs_success == fmt.initialize());

        uint8_t raw[] = {
            0xaf, 0x00, 0x12, 0x10};
        EXPECT_TRUE(srs_success == fmt.on_audio(0, (char *)raw, sizeof(raw)));

        HELPER_ASSERT_SUCCESS(enc.write(&fmt, false, 1));
        EXPECT_TRUE(fw.filesize() > 0);
    }

    if (true) {
        MockSrsFileWriter fw;
        HELPER_ASSERT_SUCCESS(fw.open("test.mp4"));

        SrsMp4M2tsInitEncoder enc;
        HELPER_ASSERT_SUCCESS(enc.initialize(&fw));

        SrsFormat fmt;
        EXPECT_TRUE(srs_success == fmt.initialize());

        uint8_t video_raw[] = {
            0x17,
            0x00, 0x00, 0x00, 0x00, 0x01, 0x64, 0x00, 0x20, 0xff, 0xe1, 0x00, 0x19, 0x67, 0x64, 0x00, 0x20,
            0xac, 0xd9, 0x40, 0xc0, 0x29, 0xb0, 0x11, 0x00, 0x00, 0x03, 0x00, 0x01, 0x00, 0x00, 0x03, 0x00,
            0x32, 0x0f, 0x18, 0x31, 0x96, 0x01, 0x00, 0x05, 0x68, 0xeb, 0xec, 0xb2, 0x2c};
        EXPECT_TRUE(srs_success == fmt.on_video(0, (char *)video_raw, sizeof(video_raw)));

        uint8_t audio_raw[] = {
            0xaf, 0x00, 0x12, 0x10};
        EXPECT_TRUE(srs_success == fmt.on_audio(0, (char *)audio_raw, sizeof(audio_raw)));

        HELPER_ASSERT_SUCCESS(enc.write(&fmt, 1, 2));
        EXPECT_TRUE(fw.filesize() > 0);
    }
}

VOID TEST(KernelMp4Test, SrsMp4DvrJitter)
{
    // Test basic initialization
    if (true) {
        SrsMp4DvrJitter jitter;

        // Should not be initialized yet
        EXPECT_FALSE(jitter.is_initialized());

        // Delta should be 0 for uninitialized jitter
        EXPECT_EQ(0, jitter.get_first_sample_delta(SrsFrameTypeVideo));
        EXPECT_EQ(0, jitter.get_first_sample_delta(SrsFrameTypeAudio));
    }

    // Test audio first scenario
    if (true) {
        SrsMp4DvrJitter jitter;

        // Create audio sample that arrives first
        SrsMp4Sample audio_sample;
        audio_sample.type_ = SrsFrameTypeAudio;
        audio_sample.dts_ = 1000; // Audio starts at 1000us

        // Create video sample that arrives later
        SrsMp4Sample video_sample;
        video_sample.type_ = SrsFrameTypeVideo;
        video_sample.dts_ = 2000; // Video starts at 2000us

        // Process samples
        jitter.on_sample(&audio_sample);
        jitter.on_sample(&video_sample);

        // Should be initialized now
        EXPECT_TRUE(jitter.is_initialized());

        // Video should have delta = video_start - audio_start = 2000 - 1000 = 1000
        EXPECT_EQ(1000, jitter.get_first_sample_delta(SrsFrameTypeVideo));

        // Audio should have delta = 0 (since audio started first)
        EXPECT_EQ(0, jitter.get_first_sample_delta(SrsFrameTypeAudio));
    }

    // Test video first scenario
    if (true) {
        SrsMp4DvrJitter jitter;

        // Create video sample that arrives first
        SrsMp4Sample video_sample;
        video_sample.type_ = SrsFrameTypeVideo;
        video_sample.dts_ = 500; // Video starts at 500us

        // Create audio sample that arrives later
        SrsMp4Sample audio_sample;
        audio_sample.type_ = SrsFrameTypeAudio;
        audio_sample.dts_ = 1500; // Audio starts at 1500us

        // Process samples
        jitter.on_sample(&video_sample);
        jitter.on_sample(&audio_sample);

        // Should be initialized now
        EXPECT_TRUE(jitter.is_initialized());

        // Audio should have delta = audio_start - video_start = 1500 - 500 = 1000
        EXPECT_EQ(1000, jitter.get_first_sample_delta(SrsFrameTypeAudio));

        // Video should have delta = 0 (since video started first)
        EXPECT_EQ(0, jitter.get_first_sample_delta(SrsFrameTypeVideo));
    }

    // Test same start time scenario
    if (true) {
        SrsMp4DvrJitter jitter;

        // Create samples with same start time
        SrsMp4Sample audio_sample;
        audio_sample.type_ = SrsFrameTypeAudio;
        audio_sample.dts_ = 1000;

        SrsMp4Sample video_sample;
        video_sample.type_ = SrsFrameTypeVideo;
        video_sample.dts_ = 1000;

        // Process samples
        jitter.on_sample(&audio_sample);
        jitter.on_sample(&video_sample);

        // Should be initialized now
        EXPECT_TRUE(jitter.is_initialized());

        // Both should have delta = 0 (same start time)
        EXPECT_EQ(0, jitter.get_first_sample_delta(SrsFrameTypeVideo));
        EXPECT_EQ(0, jitter.get_first_sample_delta(SrsFrameTypeAudio));
    }

    // Test reset functionality
    if (true) {
        SrsMp4DvrJitter jitter;

        // Initialize with samples
        SrsMp4Sample audio_sample;
        audio_sample.type_ = SrsFrameTypeAudio;
        audio_sample.dts_ = 1000;

        jitter.on_sample(&audio_sample);

        // Reset and verify
        jitter.reset();
        EXPECT_FALSE(jitter.is_initialized());
        EXPECT_EQ(0, jitter.get_first_sample_delta(SrsFrameTypeVideo));
        EXPECT_EQ(0, jitter.get_first_sample_delta(SrsFrameTypeAudio));
    }

    // Test multiple samples of same type (should only record first)
    if (true) {
        SrsMp4DvrJitter jitter;

        // Create multiple audio samples
        SrsMp4Sample audio1;
        audio1.type_ = SrsFrameTypeAudio;
        audio1.dts_ = 1000;

        SrsMp4Sample audio2;
        audio2.type_ = SrsFrameTypeAudio;
        audio2.dts_ = 2000; // This should be ignored

        SrsMp4Sample video1;
        video1.type_ = SrsFrameTypeVideo;
        video1.dts_ = 1500;

        // Process samples
        jitter.on_sample(&audio1);
        jitter.on_sample(&audio2); // Should be ignored
        jitter.on_sample(&video1);

        // Should use first audio sample (1000) not second (2000)
        EXPECT_EQ(500, jitter.get_first_sample_delta(SrsFrameTypeVideo)); // 1500 - 1000 = 500
        EXPECT_EQ(0, jitter.get_first_sample_delta(SrsFrameTypeAudio));
    }
}
