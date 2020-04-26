/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2020 Winlin
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <srs_kernel_mp4.hpp>

#include <srs_kernel_log.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_stream.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_io.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_kernel_buffer.hpp>

#include <string.h>
#include <sstream>
#include <iomanip>
using namespace std;

#define SRS_MP4_EOF_SIZE 0
#define SRS_MP4_USE_LARGE_SIZE 1

#define SRS_MP4_BUF_SIZE 4096

srs_error_t srs_mp4_write_box(ISrsWriter* writer, ISrsCodec* box)
{
    srs_error_t err = srs_success;

    int nb_data = box->nb_bytes();
    std::vector<char> data(nb_data);

    SrsBuffer* buffer = new SrsBuffer(&data[0], nb_data);
    SrsAutoFree(SrsBuffer, buffer);

    if ((err = box->encode(buffer)) != srs_success) {
        return srs_error_wrap(err, "encode box");
    }

    if ((err = writer->write(&data[0], nb_data, NULL)) != srs_success) {
        return srs_error_wrap(err, "write box");
    }

    return err;
}

stringstream& srs_mp4_padding(stringstream& ss, SrsMp4DumpContext dc, int tab)
{
    for (int i = 0; i < (int)dc.level; i++) {
        for (int j = 0; j < tab; j++) {
            ss << " ";
        }
    }
    return ss;
}

stringstream& srs_print_mp4_type(stringstream& ss, uint32_t v)
{
    ss << char(v>>24) << char(v>>16) << char(v>>8) << char(v);
    return ss;
}

stringstream& srs_mp4_print_bytes(stringstream& ss, const char* p, int size, SrsMp4DumpContext dc, int line, int max)
{
    int limit = srs_min((max<0?size:max), size);

    for (int i = 0; i < (int)limit; i += line) {
        int nn_line_elems = srs_min(line, limit-i);
        srs_dumps_array(p+i, nn_line_elems, ss, dc, srs_mp4_pfn_hex, srs_mp4_delimiter_inspace);

        if (i + line < limit) {
            ss << "," << endl;
            srs_mp4_padding(ss, dc);
        }
    }
    return ss;
}

void srs_mp4_delimiter_inline(stringstream& ss, SrsMp4DumpContext dc)
{
    ss << ",";
}

void srs_mp4_delimiter_inspace(stringstream& ss, SrsMp4DumpContext dc)
{
    ss << ", ";
}

void srs_mp4_delimiter_newline(stringstream& ss, SrsMp4DumpContext dc)
{
    ss << endl;
    srs_mp4_padding(ss, dc);
}

int srs_mp4_string_length(string v)
{
    return (int)v.length()+1;
}

void srs_mp4_string_write(SrsBuffer* buf, string v)
{
    if (!v.empty()) {
        buf->write_bytes((char*)v.data(), (int)v.length());
    }
    buf->write_1bytes(0x00);
}

srs_error_t srs_mp4_string_read(SrsBuffer* buf, string& v, int left)
{
    srs_error_t err = srs_success;
    
    if (left == 0) {
        return err;
    }
    
    char* start = buf->data() + buf->pos();
    size_t len = strnlen(start, left);
    
    if ((int)len == left) {
        return srs_error_new(ERROR_MP4_BOX_STRING, "string corrupt, left=%d", left);
    }
    
    v.append(start, len);
    buf->skip((int)len + 1);
    
    return err;
}

SrsMp4DumpContext::SrsMp4DumpContext()
{
    level = 0;
    summary = false;
}

SrsMp4DumpContext::~SrsMp4DumpContext()
{
}

SrsMp4DumpContext SrsMp4DumpContext::indent()
{
    SrsMp4DumpContext ctx = *this;
    ctx.level++;
    return ctx;
}

SrsMp4Box::SrsMp4Box()
{
    smallsize = 0;
    largesize = 0;
    start_pos = 0;
    type = SrsMp4BoxTypeForbidden;
}

SrsMp4Box::~SrsMp4Box()
{
    vector<SrsMp4Box*>::iterator it;
    for (it = boxes.begin(); it != boxes.end(); ++it) {
        SrsMp4Box* box = *it;
        srs_freep(box);
    }
    boxes.clear();
    
}

uint64_t SrsMp4Box::sz()
{
    return smallsize == SRS_MP4_USE_LARGE_SIZE? largesize:smallsize;
}

int SrsMp4Box::sz_header()
{
    return nb_header();
}

int SrsMp4Box::update_size()
{
    uint64_t size = nb_bytes();

    if (size > 0xffffffff) {
        largesize = size;
    } else {
        smallsize = (uint32_t)size;
    }

    return (int)size;
}

int SrsMp4Box::left_space(SrsBuffer* buf)
{
    int left = (int)sz() - (buf->pos() - start_pos);
    return srs_max(0, left);
}

bool SrsMp4Box::is_ftyp()
{
    return type == SrsMp4BoxTypeFTYP;
}

bool SrsMp4Box::is_moov()
{
    return type == SrsMp4BoxTypeMOOV;
}

bool SrsMp4Box::is_mdat()
{
    return type == SrsMp4BoxTypeMDAT;
}

SrsMp4Box* SrsMp4Box::get(SrsMp4BoxType bt)
{
    vector<SrsMp4Box*>::iterator it;
    for (it = boxes.begin(); it != boxes.end(); ++it) {
        SrsMp4Box* box = *it;
        
        if (box->type == bt) {
            return box;
        }
    }
    
    return NULL;
}

int SrsMp4Box::remove(SrsMp4BoxType bt)
{
    int nb_removed = 0;
    
    vector<SrsMp4Box*>::iterator it;
    for (it = boxes.begin(); it != boxes.end();) {
        SrsMp4Box* box = *it;
        
        if (box->type == bt) {
            it = boxes.erase(it);
            srs_freep(box);
        } else {
            ++it;
        }
    }
    
    return nb_removed;
}

void SrsMp4Box::append(SrsMp4Box* box)
{
    boxes.push_back(box);
}

stringstream& SrsMp4Box::dumps(stringstream& ss, SrsMp4DumpContext dc)
{
    srs_mp4_padding(ss, dc);
    srs_print_mp4_type(ss, (uint32_t)type);
    
    ss << ", " << sz();
    if (smallsize == SRS_MP4_USE_LARGE_SIZE) {
        ss << "(large)";
    }
    ss << "B";
    
    dumps_detail(ss, dc);
    
    if (!boxes.empty()) {
        ss << ", " << boxes.size() << " boxes";
    }
    
    // If there contained boxes in header,
    // which means the last box has already output the endl.
    if (!boxes_in_header()) {
        ss << endl;
    }
    
    vector<SrsMp4Box*>::iterator it;
    for (it = boxes.begin(); it != boxes.end(); ++it) {
        SrsMp4Box* box = *it;
        box->dumps(ss, dc.indent());
    }
    
    return ss;
}

srs_error_t SrsMp4Box::discovery(SrsBuffer* buf, SrsMp4Box** ppbox)
{
    *ppbox = NULL;
    
    srs_error_t err = srs_success;
    
    if (!buf->require(8)) {
        return srs_error_new(ERROR_MP4_BOX_REQUIRE_SPACE, "requires 8 only %d bytes", buf->left());
    }
    
    // Discovery the size and type.
    uint64_t largesize = 0;
    uint32_t smallsize = (uint32_t)buf->read_4bytes();
    SrsMp4BoxType type = (SrsMp4BoxType)buf->read_4bytes();
    if (smallsize == SRS_MP4_USE_LARGE_SIZE) {
        if (!buf->require(8)) {
            return srs_error_new(ERROR_MP4_BOX_REQUIRE_SPACE, "requires 16 only %d bytes", buf->left());
        }
        largesize = (uint64_t)buf->read_8bytes();
        buf->skip(-8);
    }
    buf->skip(-8);
    
    // Only support 31bits size.
    if (largesize > 0x7fffffff) {
        return srs_error_new(ERROR_MP4_BOX_OVERFLOW, "overflow 31bits, largesize=%" PRId64, largesize);
    }
    
    SrsMp4Box* box = NULL;
    switch(type) {
        case SrsMp4BoxTypeFTYP: box = new SrsMp4FileTypeBox(); break;
        case SrsMp4BoxTypeMDAT: box = new SrsMp4MediaDataBox(); break;
        case SrsMp4BoxTypeMOOV: box = new SrsMp4MovieBox(); break;
        case SrsMp4BoxTypeMVHD: box = new SrsMp4MovieHeaderBox(); break;
        case SrsMp4BoxTypeTRAK: box = new SrsMp4TrackBox(); break;
        case SrsMp4BoxTypeTKHD: box = new SrsMp4TrackHeaderBox(); break;
        case SrsMp4BoxTypeEDTS: box = new SrsMp4EditBox(); break;
        case SrsMp4BoxTypeELST: box = new SrsMp4EditListBox(); break;
        case SrsMp4BoxTypeMDIA: box = new SrsMp4MediaBox(); break;
        case SrsMp4BoxTypeMDHD: box = new SrsMp4MediaHeaderBox(); break;
        case SrsMp4BoxTypeHDLR: box = new SrsMp4HandlerReferenceBox(); break;
        case SrsMp4BoxTypeMINF: box = new SrsMp4MediaInformationBox(); break;
        case SrsMp4BoxTypeVMHD: box = new SrsMp4VideoMeidaHeaderBox(); break;
        case SrsMp4BoxTypeSMHD: box = new SrsMp4SoundMeidaHeaderBox(); break;
        case SrsMp4BoxTypeDINF: box = new SrsMp4DataInformationBox(); break;
        case SrsMp4BoxTypeURL: box = new SrsMp4DataEntryUrlBox(); break;
        case SrsMp4BoxTypeURN: box = new SrsMp4DataEntryUrnBox(); break;
        case SrsMp4BoxTypeDREF: box = new SrsMp4DataReferenceBox(); break;
        case SrsMp4BoxTypeSTBL: box = new SrsMp4SampleTableBox(); break;
        case SrsMp4BoxTypeSTSD: box = new SrsMp4SampleDescriptionBox(); break;
        case SrsMp4BoxTypeSTTS: box = new SrsMp4DecodingTime2SampleBox(); break;
        case SrsMp4BoxTypeCTTS: box = new SrsMp4CompositionTime2SampleBox(); break;
        case SrsMp4BoxTypeSTSS: box = new SrsMp4SyncSampleBox(); break;
        case SrsMp4BoxTypeSTSC: box = new SrsMp4Sample2ChunkBox(); break;
        case SrsMp4BoxTypeSTCO: box = new SrsMp4ChunkOffsetBox(); break;
        case SrsMp4BoxTypeCO64: box = new SrsMp4ChunkLargeOffsetBox(); break;
        case SrsMp4BoxTypeSTSZ: box = new SrsMp4SampleSizeBox(); break;
        case SrsMp4BoxTypeAVC1: box = new SrsMp4VisualSampleEntry(); break;
        case SrsMp4BoxTypeAVCC: box = new SrsMp4AvccBox(); break;
        case SrsMp4BoxTypeMP4A: box = new SrsMp4AudioSampleEntry(); break;
        case SrsMp4BoxTypeESDS: box = new SrsMp4EsdsBox(); break;
        case SrsMp4BoxTypeUDTA: box = new SrsMp4UserDataBox(); break;
        case SrsMp4BoxTypeMVEX: box = new SrsMp4MovieExtendsBox(); break;
        case SrsMp4BoxTypeTREX: box = new SrsMp4TrackExtendsBox(); break;
        case SrsMp4BoxTypeSTYP: box = new SrsMp4SegmentTypeBox(); break;
        case SrsMp4BoxTypeMOOF: box = new SrsMp4MovieFragmentBox(); break;
        case SrsMp4BoxTypeMFHD: box = new SrsMp4MovieFragmentHeaderBox(); break;
        case SrsMp4BoxTypeTRAF: box = new SrsMp4TrackFragmentBox(); break;
        case SrsMp4BoxTypeTFHD: box = new SrsMp4TrackFragmentHeaderBox(); break;
        case SrsMp4BoxTypeTFDT: box = new SrsMp4TrackFragmentDecodeTimeBox(); break;
        case SrsMp4BoxTypeTRUN: box = new SrsMp4TrackFragmentRunBox(); break;
        case SrsMp4BoxTypeSIDX: box = new SrsMp4SegmentIndexBox(); break;
        // Skip some unknown boxes.
        case SrsMp4BoxTypeFREE: case SrsMp4BoxTypeSKIP: case SrsMp4BoxTypePASP:
        case SrsMp4BoxTypeUUID: default:
            box = new SrsMp4FreeSpaceBox(type); break;
    }
    
    if (box) {
        box->smallsize = smallsize;
        box->largesize = largesize;
        box->type = type;
        *ppbox = box;
    }
    
    return err;
}

int SrsMp4Box::nb_bytes()
{
    int sz = nb_header();
    
    vector<SrsMp4Box*>::iterator it;
    for (it = boxes.begin(); it != boxes.end(); ++it) {
        SrsMp4Box* box = *it;
        sz += box->nb_bytes();
    }
    
    return sz;
}

srs_error_t SrsMp4Box::encode(SrsBuffer* buf)
{
    srs_error_t err = srs_success;

    update_size();
    
    start_pos = buf->pos();
    
    if ((err = encode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "encode box header");
    }
    
    if ((err = encode_boxes(buf)) != srs_success) {
        return srs_error_wrap(err, "encode contained boxes");
    }
    
    return err;
}

srs_error_t SrsMp4Box::decode(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    start_pos = buf->pos();
    
    if ((err = decode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "decode box header");
    }
    
    if ((err = decode_boxes(buf)) != srs_success) {
        return srs_error_wrap(err, "decode contained boxes");
    }
    
    return err;
}

srs_error_t SrsMp4Box::encode_boxes(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    vector<SrsMp4Box*>::iterator it;
    for (it = boxes.begin(); it != boxes.end(); ++it) {
        SrsMp4Box* box = *it;
        if ((err = box->encode(buf)) != srs_success) {
            return srs_error_wrap(err, "encode contained box");
        }
    }
    
    return err;
}

srs_error_t SrsMp4Box::decode_boxes(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    int left = left_space(buf);
    while (left > 0) {
        SrsMp4Box* box = NULL;
        if ((err = discovery(buf, &box)) != srs_success) {
            return srs_error_wrap(err, "discovery contained box");
        }
        
        srs_assert(box);
        if ((err = box->decode(buf)) != srs_success) {
            srs_freep(box);
            return srs_error_wrap(err, "decode contained box");
        }
        
        boxes.push_back(box);
        left -= box->sz();
    }
    
    return err;
}

int SrsMp4Box::nb_header()
{
    int size = 8;
    if (smallsize == SRS_MP4_USE_LARGE_SIZE) {
        size += 8;
    }
    
    if (type == SrsMp4BoxTypeUUID) {
        size += 16;
    }
    
    return size;
}

srs_error_t SrsMp4Box::encode_header(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    // Only support 31bits size.
    if (sz() > 0x7fffffff) {
        return srs_error_new(ERROR_MP4_BOX_OVERFLOW, "box size overflow 31bits, size=%" PRId64, sz());
    }
    
    int size = SrsMp4Box::nb_header();
    if (!buf->require(size)) {
        return srs_error_new(ERROR_MP4_BOX_REQUIRE_SPACE, "requires %d only %d bytes", size, buf->left());
    }
    
    buf->write_4bytes(smallsize);
    if (smallsize == SRS_MP4_USE_LARGE_SIZE) {
        buf->write_8bytes(largesize);
    }
    buf->write_4bytes(type);
    
    if (type == SrsMp4BoxTypeUUID) {
        buf->write_bytes(&usertype[0], 16);
    }
    
    int lrsz = nb_header() - SrsMp4Box::nb_header();
    if (!buf->require(lrsz)) {
        return srs_error_new(ERROR_MP4_BOX_REQUIRE_SPACE, "box requires %d only %d bytes", lrsz, buf->left());
    }
    
    return err;
}

srs_error_t SrsMp4Box::decode_header(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    if (!buf->require(8)) {
        return srs_error_new(ERROR_MP4_BOX_REQUIRE_SPACE, "requires 8 only %d bytes", buf->left());
    }
    smallsize = (uint32_t)buf->read_4bytes();
    type = (SrsMp4BoxType)buf->read_4bytes();
    
    if (smallsize == SRS_MP4_EOF_SIZE) {
        srs_trace("MP4 box EOF.");
        return err;
    }
    
    if (smallsize == SRS_MP4_USE_LARGE_SIZE) {
        if (!buf->require(8)) {
            return srs_error_new(ERROR_MP4_BOX_REQUIRE_SPACE, "box requires 8 only %d bytes", buf->left());
        }
        largesize = (uint64_t)buf->read_8bytes();
    }
    
    // Only support 31bits size.
    if (sz() > 0x7fffffff) {
        return srs_error_new(ERROR_MP4_BOX_OVERFLOW, "box size overflow 31bits, size=%" PRId64, sz());
    }
    
    if (type == SrsMp4BoxTypeUUID) {
        if (!buf->require(16)) {
            return srs_error_new(ERROR_MP4_BOX_REQUIRE_SPACE, "box requires 16 only %d bytes", buf->left());
        }
        usertype.resize(16);
        buf->read_bytes(&usertype[0], 16);
    }
    
    // The left required size, determined by the default version(0).
    int lrsz = nb_header() - SrsMp4Box::nb_header();
    if (!buf->require(lrsz)) {
        return srs_error_new(ERROR_MP4_BOX_REQUIRE_SPACE, "box requires %d only %d bytes", lrsz, buf->left());
    }
    
    return err;
}

bool SrsMp4Box::boxes_in_header()
{
    return false;
}

stringstream& SrsMp4Box::dumps_detail(stringstream& ss, SrsMp4DumpContext dc)
{
    return ss;
}

SrsMp4FullBox::SrsMp4FullBox()
{
    version = 0;
    flags = 0;
}

SrsMp4FullBox::~SrsMp4FullBox()
{
}

int SrsMp4FullBox::nb_header()
{
    return SrsMp4Box::nb_header() + 1 + 3;
}

srs_error_t SrsMp4FullBox::encode_header(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    if ((err = SrsMp4Box::encode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "encode header");
    }
    
    if (!buf->require(4)) {
        return srs_error_new(ERROR_MP4_BOX_REQUIRE_SPACE, "full box requires 4 only %d bytes", buf->left());
    }
    
    buf->write_1bytes(version);
    buf->write_3bytes(flags);
    
    return err;
}

srs_error_t SrsMp4FullBox::decode_header(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    if ((err = SrsMp4Box::decode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "decode header");
    }
    
    if (!buf->require(4)) {
        return srs_error_new(ERROR_MP4_BOX_REQUIRE_SPACE, "full box requires 4 only %d bytes", buf->left());
    }
    
    flags = (uint32_t)buf->read_4bytes();
    
    version = (uint8_t)((flags >> 24) & 0xff);
    flags &= 0x00ffffff;
    
    // The left required size, determined by the version.
    int lrsz = nb_header() - SrsMp4FullBox::nb_header();
    if (!buf->require(lrsz)) {
        return srs_error_new(ERROR_MP4_BOX_REQUIRE_SPACE, "full box requires %d only %d bytes", lrsz, buf->left());
    }
    
    return err;
}

stringstream& SrsMp4FullBox::dumps_detail(stringstream& ss, SrsMp4DumpContext dc)
{
    SrsMp4Box::dumps_detail(ss, dc);
    
    ss << ", FB(4B";
    
    if (version != 0 || flags != 0) {
        ss << ",V" << uint32_t(version)
            << ",0x" << std::setw(2) << std::setfill('0') << std::hex << flags << std::dec;
    }
    
    ss << ")";
    
    return ss;
}

SrsMp4FileTypeBox::SrsMp4FileTypeBox()
{
    type = SrsMp4BoxTypeFTYP;
    major_brand = SrsMp4BoxBrandForbidden;
    minor_version = 0;
}

SrsMp4FileTypeBox::~SrsMp4FileTypeBox()
{
}

void SrsMp4FileTypeBox::set_compatible_brands(SrsMp4BoxBrand b0, SrsMp4BoxBrand b1)
{
    compatible_brands.resize(2);
    compatible_brands[0] = b0;
    compatible_brands[1] = b1;
}

void SrsMp4FileTypeBox::set_compatible_brands(SrsMp4BoxBrand b0, SrsMp4BoxBrand b1, SrsMp4BoxBrand b2, SrsMp4BoxBrand b3)
{
    compatible_brands.resize(4);
    compatible_brands[0] = b0;
    compatible_brands[1] = b1;
    compatible_brands[2] = b2;
    compatible_brands[3] = b3;
}

int SrsMp4FileTypeBox::nb_header()
{
    return (int)(SrsMp4Box::nb_header() + 8 + compatible_brands.size() * 4);
}

srs_error_t SrsMp4FileTypeBox::encode_header(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    if ((err = SrsMp4Box::encode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "encode header");
    }
    
    buf->write_4bytes(major_brand);
    buf->write_4bytes(minor_version);
    
    for (size_t i = 0; i < (size_t)compatible_brands.size(); i++) {
        buf->write_4bytes(compatible_brands[i]);
    }
    
    return err;
}

srs_error_t SrsMp4FileTypeBox::decode_header(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    if ((err = SrsMp4Box::decode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "decode header");
    }
    
    major_brand = (SrsMp4BoxBrand)buf->read_4bytes();
    minor_version = buf->read_4bytes();
    
    // Compatible brands to the end of the box.
    int left = left_space(buf);
    
    if (left > 0) {
        compatible_brands.resize(left / 4);
    }
    
    for (int i = 0; left > 0; i++, left -= 4){
        compatible_brands[i] = (SrsMp4BoxBrand)buf->read_4bytes();
    }
    
    return err;
}

stringstream& SrsMp4FileTypeBox::dumps_detail(stringstream& ss, SrsMp4DumpContext dc)
{
    SrsMp4Box::dumps_detail(ss, dc);
    
    ss << ", brands:";
    srs_print_mp4_type(ss, (uint32_t)major_brand);
    
    ss << "," << minor_version;
    
    if (!compatible_brands.empty()) {
        ss << "(";
        srs_dumps_array(compatible_brands, ss, dc, srs_mp4_pfn_type, srs_mp4_delimiter_inline);
        ss << ")";
    }
    return ss;
}

SrsMp4SegmentTypeBox::SrsMp4SegmentTypeBox()
{
    type = SrsMp4BoxTypeSTYP;
}

SrsMp4SegmentTypeBox::~SrsMp4SegmentTypeBox()
{
}

SrsMp4MovieFragmentBox::SrsMp4MovieFragmentBox()
{
    type = SrsMp4BoxTypeMOOF;
}

SrsMp4MovieFragmentBox::~SrsMp4MovieFragmentBox()
{
}

SrsMp4MovieFragmentHeaderBox* SrsMp4MovieFragmentBox::mfhd()
{
    SrsMp4Box* box = get(SrsMp4BoxTypeMFHD);
    return dynamic_cast<SrsMp4MovieFragmentHeaderBox*>(box);
}

void SrsMp4MovieFragmentBox::set_mfhd(SrsMp4MovieFragmentHeaderBox* v)
{
    remove(SrsMp4BoxTypeMFHD);
    boxes.push_back(v);
}

SrsMp4TrackFragmentBox* SrsMp4MovieFragmentBox::traf()
{
    SrsMp4Box* box = get(SrsMp4BoxTypeTRAF);
    return dynamic_cast<SrsMp4TrackFragmentBox*>(box);
}

void SrsMp4MovieFragmentBox::set_traf(SrsMp4TrackFragmentBox* v)
{
    remove(SrsMp4BoxTypeTRAF);
    boxes.push_back(v);
}

SrsMp4MovieFragmentHeaderBox::SrsMp4MovieFragmentHeaderBox()
{
    type = SrsMp4BoxTypeMFHD;
    
    sequence_number = 0;
}

SrsMp4MovieFragmentHeaderBox::~SrsMp4MovieFragmentHeaderBox()
{
}

int SrsMp4MovieFragmentHeaderBox::nb_header()
{
    return SrsMp4FullBox::nb_header() + 4;
}

srs_error_t SrsMp4MovieFragmentHeaderBox::encode_header(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    if ((err = SrsMp4FullBox::encode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "encode header");
    }
    
    buf->write_4bytes(sequence_number);
    
    return err;
}

srs_error_t SrsMp4MovieFragmentHeaderBox::decode_header(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    if ((err = SrsMp4FullBox::decode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "decode header");
    }
    
    sequence_number = buf->read_4bytes();
    
    return err;
}

stringstream& SrsMp4MovieFragmentHeaderBox::dumps_detail(stringstream& ss, SrsMp4DumpContext dc)
{
    SrsMp4FullBox::dumps_detail(ss, dc);
    
    ss << ", sequence=" << sequence_number;
    return ss;
}

SrsMp4TrackFragmentBox::SrsMp4TrackFragmentBox()
{
    type = SrsMp4BoxTypeTRAF;
}

SrsMp4TrackFragmentBox::~SrsMp4TrackFragmentBox()
{
}

SrsMp4TrackFragmentHeaderBox* SrsMp4TrackFragmentBox::tfhd()
{
    SrsMp4Box* box = get(SrsMp4BoxTypeTFHD);
    return dynamic_cast<SrsMp4TrackFragmentHeaderBox*>(box);
}

void SrsMp4TrackFragmentBox::set_tfhd(SrsMp4TrackFragmentHeaderBox* v)
{
    remove(SrsMp4BoxTypeTFHD);
    boxes.push_back(v);
}

SrsMp4TrackFragmentDecodeTimeBox* SrsMp4TrackFragmentBox::tfdt()
{
    SrsMp4Box* box = get(SrsMp4BoxTypeTFDT);
    return dynamic_cast<SrsMp4TrackFragmentDecodeTimeBox*>(box);
}

void SrsMp4TrackFragmentBox::set_tfdt(SrsMp4TrackFragmentDecodeTimeBox* v)
{
    remove(SrsMp4BoxTypeTFDT);
    boxes.push_back(v);
}

SrsMp4TrackFragmentRunBox* SrsMp4TrackFragmentBox::trun()
{
    SrsMp4Box* box = get(SrsMp4BoxTypeTRUN);
    return dynamic_cast<SrsMp4TrackFragmentRunBox*>(box);
}

void SrsMp4TrackFragmentBox::set_trun(SrsMp4TrackFragmentRunBox* v)
{
    remove(SrsMp4BoxTypeTRUN);
    boxes.push_back(v);
}

SrsMp4TrackFragmentHeaderBox::SrsMp4TrackFragmentHeaderBox()
{
    type = SrsMp4BoxTypeTFHD;
    
    flags = 0;
    base_data_offset = 0;
    track_id = sample_description_index = 0;
    default_sample_duration = default_sample_size = 0;
    default_sample_flags = 0;
}

SrsMp4TrackFragmentHeaderBox::~SrsMp4TrackFragmentHeaderBox()
{
}

int SrsMp4TrackFragmentHeaderBox::nb_header()
{
    int size = SrsMp4FullBox::nb_header() + 4;
    
    if ((flags&SrsMp4TfhdFlagsBaseDataOffset) == SrsMp4TfhdFlagsBaseDataOffset) {
        size += 8;
    }
    if ((flags&SrsMp4TfhdFlagsSampleDescriptionIndex) == SrsMp4TfhdFlagsSampleDescriptionIndex) {
        size += 4;
    }
    if ((flags&SrsMp4TfhdFlagsDefaultSampleDuration) == SrsMp4TfhdFlagsDefaultSampleDuration) {
        size += 4;
    }
    if ((flags&SrsMp4TfhdFlagsDefautlSampleSize) == SrsMp4TfhdFlagsDefautlSampleSize) {
        size += 4;
    }
    if ((flags&SrsMp4TfhdFlagsDefaultSampleFlags) == SrsMp4TfhdFlagsDefaultSampleFlags) {
        size += 4;
    }
    
    return size;
}

srs_error_t SrsMp4TrackFragmentHeaderBox::encode_header(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    if ((err = SrsMp4FullBox::encode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "encode header");
    }
    
    buf->write_4bytes(track_id);
    
    if ((flags&SrsMp4TfhdFlagsBaseDataOffset) == SrsMp4TfhdFlagsBaseDataOffset) {
        buf->write_8bytes(base_data_offset);
    }
    if ((flags&SrsMp4TfhdFlagsSampleDescriptionIndex) == SrsMp4TfhdFlagsSampleDescriptionIndex) {
        buf->write_4bytes(sample_description_index);
    }
    if ((flags&SrsMp4TfhdFlagsDefaultSampleDuration) == SrsMp4TfhdFlagsDefaultSampleDuration) {
        buf->write_4bytes(default_sample_duration);
    }
    if ((flags&SrsMp4TfhdFlagsDefautlSampleSize) == SrsMp4TfhdFlagsDefautlSampleSize) {
        buf->write_4bytes(default_sample_size);
    }
    if ((flags&SrsMp4TfhdFlagsDefaultSampleFlags) == SrsMp4TfhdFlagsDefaultSampleFlags) {
        buf->write_4bytes(default_sample_flags);
    }
    
    return err;
}

srs_error_t SrsMp4TrackFragmentHeaderBox::decode_header(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    if ((err = SrsMp4FullBox::decode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "decode header");
    }
    
    track_id = buf->read_4bytes();
    
    if ((flags&SrsMp4TfhdFlagsBaseDataOffset) == SrsMp4TfhdFlagsBaseDataOffset) {
        base_data_offset = buf->read_8bytes();
    }
    if ((flags&SrsMp4TfhdFlagsSampleDescriptionIndex) == SrsMp4TfhdFlagsSampleDescriptionIndex) {
        sample_description_index = buf->read_4bytes();
    }
    if ((flags&SrsMp4TfhdFlagsDefaultSampleDuration) == SrsMp4TfhdFlagsDefaultSampleDuration) {
        default_sample_duration = buf->read_4bytes();
    }
    if ((flags&SrsMp4TfhdFlagsDefautlSampleSize) == SrsMp4TfhdFlagsDefautlSampleSize) {
        default_sample_size = buf->read_4bytes();
    }
    if ((flags&SrsMp4TfhdFlagsDefaultSampleFlags) == SrsMp4TfhdFlagsDefaultSampleFlags) {
        default_sample_flags = buf->read_4bytes();
    }
    
    return err;
}

stringstream& SrsMp4TrackFragmentHeaderBox::dumps_detail(stringstream& ss, SrsMp4DumpContext dc)
{
    SrsMp4FullBox::dumps_detail(ss, dc);
    
    ss << ", track=" << track_id;
    
    if ((flags&SrsMp4TfhdFlagsBaseDataOffset) == SrsMp4TfhdFlagsBaseDataOffset) {
        ss << ", bdo=" << base_data_offset;
    }
    if ((flags&SrsMp4TfhdFlagsSampleDescriptionIndex) == SrsMp4TfhdFlagsSampleDescriptionIndex) {
        ss << ", sdi=" << sample_description_index;
    }
    if ((flags&SrsMp4TfhdFlagsDefaultSampleDuration) == SrsMp4TfhdFlagsDefaultSampleDuration) {
        ss << ", dsu=" << default_sample_duration;
    }
    if ((flags&SrsMp4TfhdFlagsDefautlSampleSize) == SrsMp4TfhdFlagsDefautlSampleSize) {
        ss << ", dss=" << default_sample_size;
    }
    if ((flags&SrsMp4TfhdFlagsDefaultSampleFlags) == SrsMp4TfhdFlagsDefaultSampleFlags) {
        ss << ", dsf=" << default_sample_flags;
    }
    
    if ((flags&SrsMp4TfhdFlagsDurationIsEmpty) == SrsMp4TfhdFlagsDurationIsEmpty) {
        ss << ", empty-duration";
    }
    if ((flags&SrsMp4TfhdFlagsDefaultBaseIsMoof) == SrsMp4TfhdFlagsDefaultBaseIsMoof) {
        ss << ", moof-base";
    }
    
    return ss;
}

SrsMp4TrackFragmentDecodeTimeBox::SrsMp4TrackFragmentDecodeTimeBox()
{
    type = SrsMp4BoxTypeTFDT;
    base_media_decode_time = 0;
}

SrsMp4TrackFragmentDecodeTimeBox::~SrsMp4TrackFragmentDecodeTimeBox()
{
}

int SrsMp4TrackFragmentDecodeTimeBox::nb_header()
{
    return SrsMp4FullBox::nb_header() + (version? 8:4);
}

srs_error_t SrsMp4TrackFragmentDecodeTimeBox::encode_header(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    if ((err = SrsMp4FullBox::encode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "encode header");
    }
    
    if (version) {
        buf->write_8bytes(base_media_decode_time);
    } else {
        buf->write_4bytes((uint32_t)base_media_decode_time);
    }
    
    return err;
}

srs_error_t SrsMp4TrackFragmentDecodeTimeBox::decode_header(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    if ((err = SrsMp4FullBox::decode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "decode header");
    }
    
    if (version) {
        base_media_decode_time = buf->read_8bytes();
    } else {
        base_media_decode_time = buf->read_4bytes();
    }
    
    return err;
}

stringstream& SrsMp4TrackFragmentDecodeTimeBox::dumps_detail(stringstream& ss, SrsMp4DumpContext dc)
{
    SrsMp4FullBox::dumps_detail(ss, dc);
    
    ss << ", bmdt=" << base_media_decode_time;
    
    return ss;
}

SrsMp4TrunEntry::SrsMp4TrunEntry(SrsMp4FullBox* o)
{
    owner = o;
    sample_duration = sample_size = sample_flags = 0;
    sample_composition_time_offset = 0;
}

SrsMp4TrunEntry::~SrsMp4TrunEntry()
{
}

int SrsMp4TrunEntry::nb_bytes()
{
    int size = 0;
    
    if ((owner->flags&SrsMp4TrunFlagsSampleDuration) == SrsMp4TrunFlagsSampleDuration) {
        size += 4;
    }
    if ((owner->flags&SrsMp4TrunFlagsSampleSize) == SrsMp4TrunFlagsSampleSize) {
        size += 4;
    }
    if ((owner->flags&SrsMp4TrunFlagsSampleFlag) == SrsMp4TrunFlagsSampleFlag) {
        size += 4;
    }
    if ((owner->flags&SrsMp4TrunFlagsSampleCtsOffset) == SrsMp4TrunFlagsSampleCtsOffset) {
        size += 4;
    }
    
    return size;
}

srs_error_t SrsMp4TrunEntry::encode(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    if ((owner->flags&SrsMp4TrunFlagsSampleDuration) == SrsMp4TrunFlagsSampleDuration) {
        buf->write_4bytes(sample_duration);
    }
    if ((owner->flags&SrsMp4TrunFlagsSampleSize) == SrsMp4TrunFlagsSampleSize) {
        buf->write_4bytes(sample_size);
    }
    if ((owner->flags&SrsMp4TrunFlagsSampleFlag) == SrsMp4TrunFlagsSampleFlag) {
        buf->write_4bytes(sample_flags);
    }
    if ((owner->flags&SrsMp4TrunFlagsSampleCtsOffset) == SrsMp4TrunFlagsSampleCtsOffset) {
        if (!owner->version) {
            uint32_t v = (uint32_t)sample_composition_time_offset;
            buf->write_4bytes(v);
        } else {
            int32_t v = (int32_t)sample_composition_time_offset;
            buf->write_4bytes(v);
        }
    }
    
    return err;
}

srs_error_t SrsMp4TrunEntry::decode(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    if ((owner->flags&SrsMp4TrunFlagsSampleDuration) == SrsMp4TrunFlagsSampleDuration) {
        sample_duration = buf->read_4bytes();
    }
    if ((owner->flags&SrsMp4TrunFlagsSampleSize) == SrsMp4TrunFlagsSampleSize) {
        sample_size = buf->read_4bytes();
    }
    if ((owner->flags&SrsMp4TrunFlagsSampleFlag) == SrsMp4TrunFlagsSampleFlag) {
        sample_flags = buf->read_4bytes();
    }
    if ((owner->flags&SrsMp4TrunFlagsSampleCtsOffset) == SrsMp4TrunFlagsSampleCtsOffset) {
        if (!owner->version) {
            uint32_t v = buf->read_4bytes();
            sample_composition_time_offset = v;
        } else {
            int32_t v = buf->read_4bytes();
            sample_composition_time_offset = v;
        }
    }
    
    return err;
}

stringstream& SrsMp4TrunEntry::dumps(stringstream& ss, SrsMp4DumpContext dc)
{
    if ((owner->flags&SrsMp4TrunFlagsSampleDuration) == SrsMp4TrunFlagsSampleDuration) {
        ss << "duration=" << sample_duration;
    }
    if ((owner->flags&SrsMp4TrunFlagsSampleSize) == SrsMp4TrunFlagsSampleSize) {
        ss << ", size=" << sample_size;
    }
    if ((owner->flags&SrsMp4TrunFlagsSampleFlag) == SrsMp4TrunFlagsSampleFlag) {
        ss << ", flags=" << sample_flags;
    }
    if ((owner->flags&SrsMp4TrunFlagsSampleCtsOffset) == SrsMp4TrunFlagsSampleCtsOffset) {
        ss << ", cts=" << sample_composition_time_offset;
    }
    return ss;
}

SrsMp4TrackFragmentRunBox::SrsMp4TrackFragmentRunBox()
{
    type = SrsMp4BoxTypeTRUN;
    first_sample_flags = 0;
    data_offset = 0;
}

SrsMp4TrackFragmentRunBox::~SrsMp4TrackFragmentRunBox()
{
    vector<SrsMp4TrunEntry*>::iterator it;
    for (it = entries.begin(); it != entries.end(); ++it) {
        SrsMp4TrunEntry* entry = *it;
        srs_freep(entry);
    }
}

int SrsMp4TrackFragmentRunBox::nb_header()
{
    int size = SrsMp4FullBox::nb_header() + 4;
    
    if ((flags&SrsMp4TrunFlagsDataOffset) == SrsMp4TrunFlagsDataOffset) {
        size += 4;
    }
    if ((flags&SrsMp4TrunFlagsFirstSample) == SrsMp4TrunFlagsFirstSample) {
        size += 4;
    }
    
    vector<SrsMp4TrunEntry*>::iterator it;
    for (it = entries.begin(); it != entries.end(); ++it) {
        SrsMp4TrunEntry* entry = *it;
        size += entry->nb_bytes();
    }
    
    return size;
}

srs_error_t SrsMp4TrackFragmentRunBox::encode_header(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    if ((err = SrsMp4FullBox::encode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "encode header");
    }

    uint32_t sample_count = (uint32_t)entries.size();
    buf->write_4bytes(sample_count);
    
    if ((flags&SrsMp4TrunFlagsDataOffset) == SrsMp4TrunFlagsDataOffset) {
        buf->write_4bytes(data_offset);
    }
    if ((flags&SrsMp4TrunFlagsFirstSample) == SrsMp4TrunFlagsFirstSample) {
        buf->write_4bytes(first_sample_flags);
    }
    
    vector<SrsMp4TrunEntry*>::iterator it;
    for (it = entries.begin(); it != entries.end(); ++it) {
        SrsMp4TrunEntry* entry = *it;
        if ((err = entry->encode(buf)) != srs_success) {
            return srs_error_wrap(err, "encode entry");
        }
    }
    
    return err;
}

srs_error_t SrsMp4TrackFragmentRunBox::decode_header(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    if ((err = SrsMp4FullBox::decode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "decode header");
    }
    
    uint32_t sample_count = buf->read_4bytes();
    
    if ((flags&SrsMp4TrunFlagsDataOffset) == SrsMp4TrunFlagsDataOffset) {
        data_offset = buf->read_4bytes();
    }
    if ((flags&SrsMp4TrunFlagsFirstSample) == SrsMp4TrunFlagsFirstSample) {
        first_sample_flags = buf->read_4bytes();
    }
    
    for (int i = 0; i < (int)sample_count; i++) {
        SrsMp4TrunEntry* entry = new SrsMp4TrunEntry(this);
        entries.push_back(entry);

        if (!buf->require(entry->nb_bytes())) {
            return srs_error_new(ERROR_MP4_BOX_REQUIRE_SPACE, "trun entry requires %d bytes", entry->nb_bytes());
        }
        
        if ((err = entry->decode(buf)) != srs_success) {
            return srs_error_wrap(err, "decode entry");
        }
    }
    
    return err;
}

stringstream& SrsMp4TrackFragmentRunBox::dumps_detail(stringstream& ss, SrsMp4DumpContext dc)
{
    SrsMp4FullBox::dumps_detail(ss, dc);

    uint32_t sample_count = (uint32_t)entries.size();
    ss << ", samples=" << sample_count;
    
    if ((flags&SrsMp4TrunFlagsDataOffset) == SrsMp4TrunFlagsDataOffset) {
        ss << ", data-offset=" << data_offset;
    }
    if ((flags&SrsMp4TrunFlagsFirstSample) == SrsMp4TrunFlagsFirstSample) {
        ss << ", first-sample=" << first_sample_flags;
    }
    
    if (sample_count > 0) {
        ss << endl;
        srs_mp4_padding(ss, dc.indent());
        srs_dumps_array(entries, ss, dc.indent(), srs_mp4_pfn_box2, srs_mp4_delimiter_newline);
    }
    
    return ss;
}

SrsMp4MediaDataBox::SrsMp4MediaDataBox()
{
    type = SrsMp4BoxTypeMDAT;
    nb_data = 0;
}

SrsMp4MediaDataBox::~SrsMp4MediaDataBox()
{
}

int SrsMp4MediaDataBox::nb_bytes()
{
    return SrsMp4Box::nb_header() + nb_data;
}

srs_error_t SrsMp4MediaDataBox::encode(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    if ((err = SrsMp4Box::encode(buf)) != srs_success) {
        return srs_error_wrap(err, "encode box");
    }
    
    return err;
}

srs_error_t SrsMp4MediaDataBox::decode(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    if ((err = SrsMp4Box::decode(buf)) != srs_success) {
        return srs_error_wrap(err, "decode box");
    }
    
    nb_data = (int)(sz() - nb_header());
    
    return err;
}

srs_error_t SrsMp4MediaDataBox::encode_boxes(SrsBuffer* buf)
{
    return srs_success;
}

srs_error_t SrsMp4MediaDataBox::decode_boxes(SrsBuffer* buf)
{
    return srs_success;
}

stringstream& SrsMp4MediaDataBox::dumps_detail(stringstream& ss, SrsMp4DumpContext dc)
{
    SrsMp4Box::dumps_detail(ss, dc);
    
    ss << ", total " << nb_data << " bytes";
    
    return ss;
}

SrsMp4FreeSpaceBox::SrsMp4FreeSpaceBox(SrsMp4BoxType v)
{
    type = v; // 'free' or 'skip'
}

SrsMp4FreeSpaceBox::~SrsMp4FreeSpaceBox()
{
}

int SrsMp4FreeSpaceBox::nb_header()
{
    return SrsMp4Box::nb_header() + (int)data.size();
}

srs_error_t SrsMp4FreeSpaceBox::encode_header(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    if ((err = SrsMp4Box::encode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "encode header");
    }
    
    if (!data.empty()) {
        buf->write_bytes(&data[0], (int)data.size());
    }
    
    return err;
}

srs_error_t SrsMp4FreeSpaceBox::decode_header(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    if ((err = SrsMp4Box::decode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "decode header");
    }
    
    int left = left_space(buf);
    if (left) {
        data.resize(left);
        buf->read_bytes(&data[0], left);
    }
    
    return err;
}

stringstream& SrsMp4FreeSpaceBox::dumps_detail(stringstream& ss, SrsMp4DumpContext dc)
{
    SrsMp4Box::dumps_detail(ss, dc);
    
    ss << ", free " << data.size() << "B";
    
    if (!data.empty()) {
        ss << endl;
        srs_mp4_padding(ss, dc.indent());
        srs_dumps_array(&data[0], (int)data.size(), ss, dc.indent(), srs_mp4_pfn_hex, srs_mp4_delimiter_inspace);
    }
    return ss;
}

SrsMp4MovieBox::SrsMp4MovieBox()
{
    type = SrsMp4BoxTypeMOOV;
}

SrsMp4MovieBox::~SrsMp4MovieBox()
{
}

SrsMp4MovieHeaderBox* SrsMp4MovieBox::mvhd()
{
    SrsMp4Box* box = get(SrsMp4BoxTypeMVHD);
    return dynamic_cast<SrsMp4MovieHeaderBox*>(box);
}

void SrsMp4MovieBox::set_mvhd(SrsMp4MovieHeaderBox* v)
{
    remove(SrsMp4BoxTypeMVHD);
    boxes.push_back(v);
}

SrsMp4MovieExtendsBox* SrsMp4MovieBox::mvex()
{
    SrsMp4Box* box = get(SrsMp4BoxTypeMVEX);
    return dynamic_cast<SrsMp4MovieExtendsBox*>(box);
}

void SrsMp4MovieBox::set_mvex(SrsMp4MovieExtendsBox* v)
{
    remove(SrsMp4BoxTypeMVEX);
    boxes.push_back(v);
}

SrsMp4TrackBox* SrsMp4MovieBox::video()
{
    for (int i = 0; i < (int)boxes.size(); i++) {
        SrsMp4Box* box = boxes.at(i);
        if (box->type == SrsMp4BoxTypeTRAK) {
            SrsMp4TrackBox* trak = dynamic_cast<SrsMp4TrackBox*>(box);
            if ((trak->track_type() & SrsMp4TrackTypeVideo) == SrsMp4TrackTypeVideo) {
                return trak;
            }
        }
    }
    return NULL;
}

SrsMp4TrackBox* SrsMp4MovieBox::audio()
{
    for (int i = 0; i < (int)boxes.size(); i++) {
        SrsMp4Box* box = boxes.at(i);
        if (box->type == SrsMp4BoxTypeTRAK) {
            SrsMp4TrackBox* trak = dynamic_cast<SrsMp4TrackBox*>(box);
            if ((trak->track_type() & SrsMp4TrackTypeAudio) == SrsMp4TrackTypeAudio) {
                return trak;
            }
        }
    }
    return NULL;
}

void SrsMp4MovieBox::add_trak(SrsMp4TrackBox* v)
{
    boxes.push_back(v);
}

int SrsMp4MovieBox::nb_vide_tracks()
{
    int nb_tracks = 0;
    
    for (int i = 0; i < (int)boxes.size(); i++) {
        SrsMp4Box* box = boxes.at(i);
        if (box->type == SrsMp4BoxTypeTRAK) {
            SrsMp4TrackBox* trak = dynamic_cast<SrsMp4TrackBox*>(box);
            if ((trak->track_type() & SrsMp4TrackTypeVideo) == SrsMp4TrackTypeVideo) {
                nb_tracks++;
            }
        }
    }
    
    return nb_tracks;
}

int SrsMp4MovieBox::nb_soun_tracks()
{
    int nb_tracks = 0;
    
    for (int i = 0; i < (int)boxes.size(); i++) {
        SrsMp4Box* box = boxes.at(i);
        if (box->type == SrsMp4BoxTypeTRAK) {
            SrsMp4TrackBox* trak = dynamic_cast<SrsMp4TrackBox*>(box);
            if ((trak->track_type() & SrsMp4TrackTypeAudio) == SrsMp4TrackTypeAudio) {
                nb_tracks++;
            }
        }
    }
    
    return nb_tracks;
}

int SrsMp4MovieBox::nb_header()
{
    return SrsMp4Box::nb_header();
}

srs_error_t SrsMp4MovieBox::encode_header(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    if ((err = SrsMp4Box::encode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "encode header");
    }
    
    return err;
}

srs_error_t SrsMp4MovieBox::decode_header(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    if ((err = SrsMp4Box::decode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "decode header");
    }
    
    return err;
}

SrsMp4MovieHeaderBox::SrsMp4MovieHeaderBox() : creation_time(0), modification_time(0), timescale(0), duration_in_tbn(0)
{
    type = SrsMp4BoxTypeMVHD;
    
    rate = 0x00010000; // typically 1.0
    volume = 0x0100; // typically, full volume
    reserved0 = 0;
    reserved1 = 0;
    
    int32_t v[] = {0x00010000, 0, 0, 0, 0x00010000, 0, 0, 0, 0x40000000};
    memcpy(matrix, v, 36);
    
    memset(pre_defined, 0, 24);
    
    next_track_ID = 0;
}

SrsMp4MovieHeaderBox::~SrsMp4MovieHeaderBox()
{
}

uint64_t SrsMp4MovieHeaderBox::duration()
{
    if (timescale <= 0) {
        return 0;
    }
    return duration_in_tbn * 1000 / timescale;
}

int SrsMp4MovieHeaderBox::nb_header()
{
    int size = SrsMp4FullBox::nb_header();
    
    if (version == 1) {
        size += 8+8+4+8;
    } else {
        size += 4+4+4+4;
    }
    
    size += 4+2+2+8+36+24+4;
    
    return size;
}

srs_error_t SrsMp4MovieHeaderBox::encode_header(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    if ((err = SrsMp4FullBox::encode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "encode header");
    }
    
    if (version == 1) {
        buf->write_8bytes(creation_time);
        buf->write_8bytes(modification_time);
        buf->write_4bytes(timescale);
        buf->write_8bytes(duration_in_tbn);
    } else {
        buf->write_4bytes((uint32_t)creation_time);
        buf->write_4bytes((uint32_t)modification_time);
        buf->write_4bytes(timescale);
        buf->write_4bytes((uint32_t)duration_in_tbn);
    }
    
    buf->write_4bytes(rate);
    buf->write_2bytes(volume);
    buf->write_2bytes(reserved0);
    buf->write_8bytes(reserved1);
    for (int i = 0; i < 9; i++) {
        buf->write_4bytes(matrix[i]);
    }
    for (int i = 0; i < 6; i++) {
        buf->write_4bytes(pre_defined[i]);
    }
    buf->write_4bytes(next_track_ID);
    
    return err;
}

srs_error_t SrsMp4MovieHeaderBox::decode_header(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    if ((err = SrsMp4FullBox::decode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "decode header");
    }
    
    if (version == 1) {
        creation_time = buf->read_8bytes();
        modification_time = buf->read_8bytes();
        timescale = buf->read_4bytes();
        duration_in_tbn = buf->read_8bytes();
    } else {
        creation_time = buf->read_4bytes();
        modification_time = buf->read_4bytes();
        timescale = buf->read_4bytes();
        duration_in_tbn = buf->read_4bytes();
    }
    
    rate = buf->read_4bytes();
    volume = buf->read_2bytes();
    buf->skip(2);
    buf->skip(8);
    for (int i = 0; i < 9; i++) {
        matrix[i] = buf->read_4bytes();
    }
    buf->skip(24);
    next_track_ID = buf->read_4bytes();
    
    return err;
}

stringstream& SrsMp4MovieHeaderBox::dumps_detail(stringstream& ss, SrsMp4DumpContext dc)
{
    SrsMp4FullBox::dumps_detail(ss, dc);
    
    ss << ", " << std::setprecision(2) << duration() << "ms, TBN=" << timescale << ", nTID=" << next_track_ID;
    return ss;
}

SrsMp4MovieExtendsBox::SrsMp4MovieExtendsBox()
{
    type = SrsMp4BoxTypeMVEX;
}

SrsMp4MovieExtendsBox::~SrsMp4MovieExtendsBox()
{
}

SrsMp4TrackExtendsBox* SrsMp4MovieExtendsBox::trex()
{
    SrsMp4Box* box = get(SrsMp4BoxTypeTREX);
    return dynamic_cast<SrsMp4TrackExtendsBox*>(box);
}

void SrsMp4MovieExtendsBox::set_trex(SrsMp4TrackExtendsBox* v)
{
    remove(SrsMp4BoxTypeTREX);
    boxes.push_back(v);
}

SrsMp4TrackExtendsBox::SrsMp4TrackExtendsBox()
{
    type = SrsMp4BoxTypeTREX;
    track_ID = default_sample_size = default_sample_flags = 0;
    default_sample_size = default_sample_duration = default_sample_description_index = 0;
}

SrsMp4TrackExtendsBox::~SrsMp4TrackExtendsBox()
{
}

int SrsMp4TrackExtendsBox::nb_header()
{
    return SrsMp4FullBox::nb_header() + 4*5;
}

srs_error_t SrsMp4TrackExtendsBox::encode_header(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    if ((err = SrsMp4FullBox::encode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "encode header");
    }
    
    buf->write_4bytes(track_ID);
    buf->write_4bytes(default_sample_description_index);
    buf->write_4bytes(default_sample_duration);
    buf->write_4bytes(default_sample_size);
    buf->write_4bytes(default_sample_flags);
    
    return err;
}

srs_error_t SrsMp4TrackExtendsBox::decode_header(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    if ((err = SrsMp4FullBox::decode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "decode header");
    }
    
    track_ID = buf->read_4bytes();
    default_sample_description_index = buf->read_4bytes();
    default_sample_duration = buf->read_4bytes();
    default_sample_size = buf->read_4bytes();
    default_sample_flags = buf->read_4bytes();
    
    return err;
}

stringstream& SrsMp4TrackExtendsBox::dumps_detail(stringstream& ss, SrsMp4DumpContext dc)
{
    SrsMp4FullBox::dumps_detail(ss, dc);
    
    ss << ", track=#" << track_ID << ", default-sample("
        << "index:" << default_sample_description_index << ", size:" << default_sample_size
        << ", duration:" << default_sample_duration << ", flags:" << default_sample_flags << ")";
    return ss;
}

SrsMp4TrackBox::SrsMp4TrackBox()
{
    type = SrsMp4BoxTypeTRAK;
}

SrsMp4TrackBox::~SrsMp4TrackBox()
{
}

SrsMp4TrackType SrsMp4TrackBox::track_type()
{
    // TODO: Maybe should discovery all mdia boxes.
    SrsMp4MediaBox* box = mdia();
    if (!box) {
        return SrsMp4TrackTypeForbidden;
    }
    return box->track_type();
}

SrsMp4TrackHeaderBox* SrsMp4TrackBox::tkhd()
{
    SrsMp4Box* box = get(SrsMp4BoxTypeTKHD);
    return dynamic_cast<SrsMp4TrackHeaderBox*>(box);
}

void SrsMp4TrackBox::set_tkhd(SrsMp4TrackHeaderBox* v)
{
    remove(SrsMp4BoxTypeTKHD);
    boxes.insert(boxes.begin(), v);
}

SrsMp4ChunkOffsetBox* SrsMp4TrackBox::stco()
{
    SrsMp4SampleTableBox* box = stbl();
    return box? box->stco():NULL;
}

SrsMp4SampleSizeBox* SrsMp4TrackBox::stsz()
{
    SrsMp4SampleTableBox* box = stbl();
    return box? box->stsz():NULL;
}

SrsMp4Sample2ChunkBox* SrsMp4TrackBox::stsc()
{
    SrsMp4SampleTableBox* box = stbl();
    return box? box->stsc():NULL;
}

SrsMp4DecodingTime2SampleBox* SrsMp4TrackBox::stts()
{
    SrsMp4SampleTableBox* box = stbl();
    return box? box->stts():NULL;
}

SrsMp4CompositionTime2SampleBox* SrsMp4TrackBox::ctts()
{
    SrsMp4SampleTableBox* box = stbl();
    return box? box->ctts():NULL;
}

SrsMp4SyncSampleBox* SrsMp4TrackBox::stss()
{
    SrsMp4SampleTableBox* box = stbl();
    return box? box->stss():NULL;
}

SrsMp4MediaHeaderBox* SrsMp4TrackBox::mdhd()
{
    SrsMp4MediaBox* box = mdia();
    return box? box->mdhd():NULL;
}

SrsVideoCodecId SrsMp4TrackBox::vide_codec()
{
    SrsMp4SampleDescriptionBox* box = stsd();
    if (!box) {
        return SrsVideoCodecIdForbidden;
    }
    
    if (box->entry_count() == 0) {
        return SrsVideoCodecIdForbidden;
    }
    
    SrsMp4SampleEntry* entry = box->entrie_at(0);
    switch(entry->type) {
        case SrsMp4BoxTypeAVC1: return SrsVideoCodecIdAVC;
        default: return SrsVideoCodecIdForbidden;
    }
}

SrsAudioCodecId SrsMp4TrackBox::soun_codec()
{
    SrsMp4SampleDescriptionBox* box = stsd();
    if (!box) {
        return SrsAudioCodecIdForbidden;
    }
    
    if (box->entry_count() == 0) {
        return SrsAudioCodecIdForbidden;
    }
    
    SrsMp4SampleEntry* entry = box->entrie_at(0);
    switch(entry->type) {
        case SrsMp4BoxTypeMP4A: return SrsAudioCodecIdAAC;
        default: return SrsAudioCodecIdForbidden;
    }
}

SrsMp4AvccBox* SrsMp4TrackBox::avcc()
{
    SrsMp4VisualSampleEntry* box = avc1();
    return box? box->avcC():NULL;
}

SrsMp4DecoderSpecificInfo* SrsMp4TrackBox::asc()
{
    SrsMp4AudioSampleEntry* box = mp4a();
    return box? box->asc():NULL;
}

SrsMp4MediaBox* SrsMp4TrackBox::mdia()
{
    SrsMp4Box* box = get(SrsMp4BoxTypeMDIA);
    return dynamic_cast<SrsMp4MediaBox*>(box);
}

void SrsMp4TrackBox::set_mdia(SrsMp4MediaBox* v)
{
    remove(SrsMp4BoxTypeMDIA);
    boxes.push_back(v);
}

SrsMp4MediaInformationBox* SrsMp4TrackBox::minf()
{
    SrsMp4MediaBox* box = mdia();
    return box? box->minf():NULL;
}

SrsMp4SampleTableBox* SrsMp4TrackBox::stbl()
{
    SrsMp4MediaInformationBox* box = minf();
    return box? box->stbl():NULL;
}

SrsMp4SampleDescriptionBox* SrsMp4TrackBox::stsd()
{
    SrsMp4SampleTableBox* box = stbl();
    return box? box->stsd():NULL;
}

SrsMp4VisualSampleEntry* SrsMp4TrackBox::avc1()
{
    SrsMp4SampleDescriptionBox* box = stsd();
    return box? box->avc1():NULL;
}

SrsMp4AudioSampleEntry* SrsMp4TrackBox::mp4a()
{
    SrsMp4SampleDescriptionBox* box = stsd();
    return box? box->mp4a():NULL;
}

SrsMp4TrackHeaderBox::SrsMp4TrackHeaderBox() : creation_time(0), modification_time(0), track_ID(0), duration(0)
{
    type = SrsMp4BoxTypeTKHD;
    
    reserved0 = 0;
    reserved1 = 0;
    reserved2 = 0;
    layer = alternate_group = 0;
    volume = 0; // if track_is_audio 0x0100 else 0
    
    int32_t v[] = {0x00010000, 0, 0, 0, 0x00010000, 0, 0, 0, 0x40000000};
    memcpy(matrix, v, 36);
    
    width = height = 0;
    flags = 0x03;
}

SrsMp4TrackHeaderBox::~SrsMp4TrackHeaderBox()
{
}

int SrsMp4TrackHeaderBox::nb_header()
{
    int size = SrsMp4FullBox::nb_header();
    
    if (version == 1) {
        size += 8+8+4+4+8;
    } else {
        size += 4+4+4+4+4;
    }
    
    size += 8+2+2+2+2+36+4+4;
    
    return size;
}

srs_error_t SrsMp4TrackHeaderBox::encode_header(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    if ((err = SrsMp4FullBox::encode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "encode header");
    }
    
    if (version == 1) {
        buf->write_8bytes(creation_time);
        buf->write_8bytes(modification_time);
        buf->write_4bytes(track_ID);
        buf->write_4bytes(reserved0);
        buf->write_8bytes(duration);
    } else {
        buf->write_4bytes((uint32_t)creation_time);
        buf->write_4bytes((uint32_t)modification_time);
        buf->write_4bytes(track_ID);
        buf->write_4bytes(reserved0);
        buf->write_4bytes((uint32_t)duration);
    }
    
    buf->write_8bytes(reserved1);
    buf->write_2bytes(layer);
    buf->write_2bytes(alternate_group);
    buf->write_2bytes(volume);
    buf->write_2bytes(reserved2);
    for (int i = 0; i < 9; i++) {
        buf->write_4bytes(matrix[i]);
    }
    buf->write_4bytes(width);
    buf->write_4bytes(height);
    
    return err;
}

srs_error_t SrsMp4TrackHeaderBox::decode_header(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    if ((err = SrsMp4FullBox::decode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "decode header");
    }
    
    if (version == 1) {
        creation_time = buf->read_8bytes();
        modification_time = buf->read_8bytes();
        track_ID = buf->read_4bytes();
        buf->skip(4);
        duration = buf->read_8bytes();
    } else {
        creation_time = buf->read_4bytes();
        modification_time = buf->read_4bytes();
        track_ID = buf->read_4bytes();
        buf->skip(4);
        duration = buf->read_4bytes();
    }
    
    buf->skip(8);
    layer = buf->read_2bytes();
    alternate_group = buf->read_2bytes();
    volume = buf->read_2bytes();
    buf->skip(2);
    for (int i = 0; i < 9; i++) {
        matrix[i] = buf->read_4bytes();
    }
    width = buf->read_4bytes();
    height = buf->read_4bytes();
    
    return err;
}

stringstream& SrsMp4TrackHeaderBox::dumps_detail(stringstream& ss, SrsMp4DumpContext dc)
{
    SrsMp4FullBox::dumps_detail(ss, dc);
    
    ss << ", track #" << track_ID << ", " << duration << "TBN";
    
    if (volume) {
        ss << ", volume=" << uint32_t(volume>>8) << "." << uint32_t(volume&0xFF);
    }

    ss << ", size=" << uint16_t(width>>16) << "x" << uint16_t(height>>16);
    
    return ss;
}

SrsMp4EditBox::SrsMp4EditBox()
{
    type = SrsMp4BoxTypeEDTS;
}

SrsMp4EditBox::~SrsMp4EditBox()
{
}

SrsMp4ElstEntry::SrsMp4ElstEntry() : segment_duration(0), media_time(0), media_rate_integer(0)
{
    media_rate_fraction = 0;
}

SrsMp4ElstEntry::~SrsMp4ElstEntry()
{
}

stringstream& SrsMp4ElstEntry::dumps(stringstream& ss, SrsMp4DumpContext dc)
{
    return dumps_detail(ss, dc);
}

stringstream& SrsMp4ElstEntry::dumps_detail(stringstream& ss, SrsMp4DumpContext dc)
{
    ss << "Entry, " << segment_duration << "TBN, start=" << media_time << "TBN"
        << ", rate=" << media_rate_integer << "," << media_rate_fraction;
    return ss;
}

SrsMp4EditListBox::SrsMp4EditListBox()
{
    type = SrsMp4BoxTypeELST;
}

SrsMp4EditListBox::~SrsMp4EditListBox()
{
}

int SrsMp4EditListBox::nb_header()
{
    int size = SrsMp4FullBox::nb_header() + 4;
    
    if (version == 1) {
        size += entries.size() * (2+2+8+8);
    } else {
        size += entries.size() * (2+2+4+4);
    }
    
    return size;
}

srs_error_t SrsMp4EditListBox::encode_header(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    if ((err = SrsMp4FullBox::encode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "encode header");
    }
    
    buf->write_4bytes((int)entries.size());
    for (size_t i = 0; i < (size_t)entries.size(); i++) {
        SrsMp4ElstEntry& entry = entries[i];
        
        if (version == 1) {
            buf->write_8bytes(entry.segment_duration);
            buf->write_8bytes(entry.media_time);
        } else {
            buf->write_4bytes((uint32_t)entry.segment_duration);
            buf->write_4bytes((int32_t)entry.media_time);
        }
        
        buf->write_2bytes(entry.media_rate_integer);
        buf->write_2bytes(entry.media_rate_fraction);
    }
    
    return err;
}

srs_error_t SrsMp4EditListBox::decode_header(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    if ((err = SrsMp4FullBox::decode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "decode header");
    }
    
    uint32_t entry_count = buf->read_4bytes();
    if (entry_count > 0) {
        entries.resize(entry_count);
    }
    for (int i = 0; i < (int)entry_count; i++) {
        SrsMp4ElstEntry& entry = entries[i];
        
        if (version == 1) {
            if (!buf->require(16)) {
                return srs_error_new(ERROR_MP4_BOX_REQUIRE_SPACE, "no space");
            }
            entry.segment_duration = buf->read_8bytes();
            entry.media_time = buf->read_8bytes();
        } else {
            if (!buf->require(8)) {
                return srs_error_new(ERROR_MP4_BOX_REQUIRE_SPACE, "no space");
            }
            entry.segment_duration = buf->read_4bytes();
            entry.media_time = buf->read_4bytes();
        }

        if (!buf->require(4)) {
            return srs_error_new(ERROR_MP4_BOX_REQUIRE_SPACE, "no space");
        }
        entry.media_rate_integer = buf->read_2bytes();
        entry.media_rate_fraction = buf->read_2bytes();
    }
    
    return err;
}

stringstream& SrsMp4EditListBox::dumps_detail(stringstream& ss, SrsMp4DumpContext dc)
{
    SrsMp4FullBox::dumps_detail(ss, dc);
    
    ss << ", " << entries.size() << " childs";
    
    if (!entries.empty()) {
        ss << "(+)" << endl;
        srs_mp4_padding(ss, dc.indent());
        srs_dumps_array(entries, ss, dc.indent(), srs_mp4_pfn_detail, srs_mp4_delimiter_newline);
    }
    
    return ss;
}

SrsMp4MediaBox::SrsMp4MediaBox()
{
    type = SrsMp4BoxTypeMDIA;
}

SrsMp4MediaBox::~SrsMp4MediaBox()
{
}

SrsMp4TrackType SrsMp4MediaBox::track_type()
{
    SrsMp4Box* box = get(SrsMp4BoxTypeHDLR);
    if (!box) {
        return SrsMp4TrackTypeForbidden;
    }
    
    SrsMp4HandlerReferenceBox* hdlr = dynamic_cast<SrsMp4HandlerReferenceBox*>(box);
    if (hdlr->handler_type == SrsMp4HandlerTypeSOUN) {
        return SrsMp4TrackTypeAudio;
    } else if (hdlr->handler_type == SrsMp4HandlerTypeVIDE) {
        return SrsMp4TrackTypeVideo;
    } else {
        return SrsMp4TrackTypeForbidden;
    }
}

SrsMp4MediaHeaderBox* SrsMp4MediaBox::mdhd()
{
    SrsMp4Box* box = get(SrsMp4BoxTypeMDHD);
    return dynamic_cast<SrsMp4MediaHeaderBox*>(box);
}

void SrsMp4MediaBox::set_mdhd(SrsMp4MediaHeaderBox* v)
{
    remove(SrsMp4BoxTypeMDHD);
    boxes.insert(boxes.begin(), v);
}

SrsMp4HandlerReferenceBox* SrsMp4MediaBox::hdlr()
{
    SrsMp4Box* box = get(SrsMp4BoxTypeHDLR);
    return dynamic_cast<SrsMp4HandlerReferenceBox*>(box);
}

void SrsMp4MediaBox::set_hdlr(SrsMp4HandlerReferenceBox* v)
{
    remove(SrsMp4BoxTypeHDLR);
    boxes.push_back(v);
}

SrsMp4MediaInformationBox* SrsMp4MediaBox::minf()
{
    SrsMp4Box* box = get(SrsMp4BoxTypeMINF);
    return dynamic_cast<SrsMp4MediaInformationBox*>(box);
}

void SrsMp4MediaBox::set_minf(SrsMp4MediaInformationBox* v)
{
    remove(SrsMp4BoxTypeMINF);
    boxes.push_back(v);
}

SrsMp4MediaHeaderBox::SrsMp4MediaHeaderBox() : creation_time(0), modification_time(0), timescale(0), duration(0)
{
    type = SrsMp4BoxTypeMDHD;
    language = 0;
    pre_defined = 0;
}

SrsMp4MediaHeaderBox::~SrsMp4MediaHeaderBox()
{
}

char SrsMp4MediaHeaderBox::language0()
{
    return (char)(((language >> 10) & 0x1f) + 0x60);
}

void SrsMp4MediaHeaderBox::set_language0(char v)
{
    language |= uint16_t((uint8_t(v) - 0x60) & 0x1f) << 10;
}

char SrsMp4MediaHeaderBox::language1()
{
    return (char)(((language >> 5) & 0x1f) + 0x60);
}

void SrsMp4MediaHeaderBox::set_language1(char v)
{
    language |= uint16_t((uint8_t(v) - 0x60) & 0x1f) << 5;
}

char SrsMp4MediaHeaderBox::language2()
{
    return (char)((language & 0x1f) + 0x60);
}

void SrsMp4MediaHeaderBox::set_language2(char v)
{
    language |= uint16_t((uint8_t(v) - 0x60) & 0x1f);
}

int SrsMp4MediaHeaderBox::nb_header()
{
    int size = SrsMp4FullBox::nb_header();
    
    if (version == 1) {
        size += 8+8+4+8;
    } else {
        size += 4+4+4+4;
    }
    
    size += 2+2;
    
    return size;
}

srs_error_t SrsMp4MediaHeaderBox::encode_header(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    if ((err = SrsMp4FullBox::encode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "encode header");
    }
    
    if (version == 1) {
        buf->write_8bytes(creation_time);
        buf->write_8bytes(modification_time);
        buf->write_4bytes(timescale);
        buf->write_8bytes(duration);
    } else {
        buf->write_4bytes((uint32_t)creation_time);
        buf->write_4bytes((uint32_t)modification_time);
        buf->write_4bytes(timescale);
        buf->write_4bytes((uint32_t)duration);
    }
    
    buf->write_2bytes(language);
    buf->write_2bytes(pre_defined);
    
    return err;
}

srs_error_t SrsMp4MediaHeaderBox::decode_header(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    if ((err = SrsMp4FullBox::decode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "decode header");
    }
    
    if (version == 1) {
        creation_time = buf->read_8bytes();
        modification_time = buf->read_8bytes();
        timescale = buf->read_4bytes();
        duration = buf->read_8bytes();
    } else {
        creation_time = buf->read_4bytes();
        modification_time = buf->read_4bytes();
        timescale = buf->read_4bytes();
        duration = buf->read_4bytes();
    }
    
    language = buf->read_2bytes();
    buf->skip(2);
    
    return err;
}

stringstream& SrsMp4MediaHeaderBox::dumps_detail(stringstream& ss, SrsMp4DumpContext dc)
{
    SrsMp4FullBox::dumps_detail(ss, dc);
    
    ss << ", TBN=" << timescale << ", " << duration << "TBN";
    if (language) {
        ss << ", LANG=" << language0() << language1() << language2();
    }
    return ss;
}

SrsMp4HandlerReferenceBox::SrsMp4HandlerReferenceBox()
{
    type = SrsMp4BoxTypeHDLR;
    
    pre_defined = 0;
    memset(reserved, 0, 12);
    
    handler_type = SrsMp4HandlerTypeForbidden;
}

SrsMp4HandlerReferenceBox::~SrsMp4HandlerReferenceBox()
{
}

bool SrsMp4HandlerReferenceBox::is_video()
{
    return handler_type == SrsMp4HandlerTypeVIDE;
}

bool SrsMp4HandlerReferenceBox::is_audio()
{
    return handler_type == SrsMp4HandlerTypeSOUN;
}

int SrsMp4HandlerReferenceBox::nb_header()
{
    return SrsMp4FullBox::nb_header()+4+4+12+srs_mp4_string_length(name);
}

srs_error_t SrsMp4HandlerReferenceBox::encode_header(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    if ((err = SrsMp4FullBox::encode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "encode header");
    }
    
    buf->write_4bytes(pre_defined);
    buf->write_4bytes(handler_type);
    buf->write_4bytes(reserved[0]);
    buf->write_4bytes(reserved[1]);
    buf->write_4bytes(reserved[2]);
    srs_mp4_string_write(buf, name);
    
    return err;
}

srs_error_t SrsMp4HandlerReferenceBox::decode_header(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    if ((err = SrsMp4FullBox::decode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "decode header");
    }
    
    buf->skip(4);
    handler_type = (SrsMp4HandlerType)buf->read_4bytes();
    buf->skip(12);
    
    if ((err = srs_mp4_string_read(buf, name, left_space(buf))) != srs_success) {
        return srs_error_wrap(err, "hdlr read string");
    }
    
    return err;
}

stringstream& SrsMp4HandlerReferenceBox::dumps_detail(stringstream& ss, SrsMp4DumpContext dc)
{
    SrsMp4FullBox::dumps_detail(ss, dc);
    
    ss << ", ";
    srs_print_mp4_type(ss, (uint32_t)handler_type);
    if (!name.empty()) {
        ss << ", " <<  name;
    }
    
    return ss;
}

SrsMp4MediaInformationBox::SrsMp4MediaInformationBox()
{
    type = SrsMp4BoxTypeMINF;
}

SrsMp4MediaInformationBox::~SrsMp4MediaInformationBox()
{
}

SrsMp4VideoMeidaHeaderBox* SrsMp4MediaInformationBox::vmhd()
{
    SrsMp4Box* box = get(SrsMp4BoxTypeVMHD);
    return dynamic_cast<SrsMp4VideoMeidaHeaderBox*>(box);
}

void SrsMp4MediaInformationBox::set_vmhd(SrsMp4VideoMeidaHeaderBox* v)
{
    remove(SrsMp4BoxTypeVMHD);
    boxes.push_back(v);
}

SrsMp4SoundMeidaHeaderBox* SrsMp4MediaInformationBox::smhd()
{
    SrsMp4Box* box = get(SrsMp4BoxTypeSMHD);
    return dynamic_cast<SrsMp4SoundMeidaHeaderBox*>(box);
}

void SrsMp4MediaInformationBox::set_smhd(SrsMp4SoundMeidaHeaderBox* v)
{
    remove(SrsMp4BoxTypeSMHD);
    boxes.push_back(v);
}

SrsMp4DataInformationBox* SrsMp4MediaInformationBox::dinf()
{
    SrsMp4Box* box = get(SrsMp4BoxTypeDINF);
    return dynamic_cast<SrsMp4DataInformationBox*>(box);
}

void SrsMp4MediaInformationBox::set_dinf(SrsMp4DataInformationBox* v)
{
    remove(SrsMp4BoxTypeDINF);
    boxes.push_back(v);
}

SrsMp4SampleTableBox* SrsMp4MediaInformationBox::stbl()
{
    SrsMp4Box* box = get(SrsMp4BoxTypeSTBL);
    return dynamic_cast<SrsMp4SampleTableBox*>(box);
}

void SrsMp4MediaInformationBox::set_stbl(SrsMp4SampleTableBox* v)
{
    remove(SrsMp4BoxTypeSTBL);
    boxes.push_back(v);
}

SrsMp4VideoMeidaHeaderBox::SrsMp4VideoMeidaHeaderBox()
{
    type = SrsMp4BoxTypeVMHD;
    version = 0;
    flags = 1;
    
    graphicsmode = 0;
    memset(opcolor, 0, 6);
}

SrsMp4VideoMeidaHeaderBox::~SrsMp4VideoMeidaHeaderBox()
{
}

int SrsMp4VideoMeidaHeaderBox::nb_header()
{
    return SrsMp4FullBox::nb_header()+2+6;
}

srs_error_t SrsMp4VideoMeidaHeaderBox::encode_header(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    if ((err = SrsMp4FullBox::encode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "encode header");
    }
    
    buf->write_2bytes(graphicsmode);
    buf->write_2bytes(opcolor[0]);
    buf->write_2bytes(opcolor[1]);
    buf->write_2bytes(opcolor[2]);
    
    return err;
}

srs_error_t SrsMp4VideoMeidaHeaderBox::decode_header(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    if ((err = SrsMp4FullBox::decode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "decode header");
    }
    
    graphicsmode = buf->read_2bytes();
    opcolor[0] = buf->read_2bytes();
    opcolor[1] = buf->read_2bytes();
    opcolor[2] = buf->read_2bytes();
    
    return err;
}

SrsMp4SoundMeidaHeaderBox::SrsMp4SoundMeidaHeaderBox()
{
    type = SrsMp4BoxTypeSMHD;
    
    reserved = balance = 0;
}

SrsMp4SoundMeidaHeaderBox::~SrsMp4SoundMeidaHeaderBox()
{
}

int SrsMp4SoundMeidaHeaderBox::nb_header()
{
    return SrsMp4FullBox::nb_header()+2+2;
}

srs_error_t SrsMp4SoundMeidaHeaderBox::encode_header(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    if ((err = SrsMp4FullBox::encode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "encode header");
    }
    
    buf->write_2bytes(balance);
    buf->write_2bytes(reserved);
    
    return err;
}

srs_error_t SrsMp4SoundMeidaHeaderBox::decode_header(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    if ((err = SrsMp4FullBox::decode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "decode header");
    }
    
    balance = buf->read_2bytes();
    buf->skip(2);
    
    return err;
}

SrsMp4DataInformationBox::SrsMp4DataInformationBox()
{
    type = SrsMp4BoxTypeDINF;
}

SrsMp4DataInformationBox::~SrsMp4DataInformationBox()
{
}

SrsMp4DataReferenceBox* SrsMp4DataInformationBox::dref()
{
    SrsMp4Box* box = get(SrsMp4BoxTypeDREF);
    return dynamic_cast<SrsMp4DataReferenceBox*>(box);
}

void SrsMp4DataInformationBox::set_dref(SrsMp4DataReferenceBox* v)
{
    remove(SrsMp4BoxTypeDREF);
    boxes.push_back(v);
}

SrsMp4DataEntryBox::SrsMp4DataEntryBox()
{
}

SrsMp4DataEntryBox::~SrsMp4DataEntryBox()
{
}

bool SrsMp4DataEntryBox::boxes_in_header()
{
    return true;
}

SrsMp4DataEntryUrlBox::SrsMp4DataEntryUrlBox()
{
    type = SrsMp4BoxTypeURL;
}

SrsMp4DataEntryUrlBox::~SrsMp4DataEntryUrlBox()
{
}

int SrsMp4DataEntryUrlBox::nb_header()
{
    return SrsMp4FullBox::nb_header()+srs_mp4_string_length(location);
}

srs_error_t SrsMp4DataEntryUrlBox::encode_header(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    // a 24-bit integer with flags; one flag is defined (x000001) which means that the media
    // data is in the same file as the Movie Box containing this data reference.
    if (location.empty()) {
        flags = 0x01;
    }
    
    if ((err = SrsMp4FullBox::encode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "encode header");
    }

    srs_mp4_string_write(buf, location);

    return err;
}

srs_error_t SrsMp4DataEntryUrlBox::decode_header(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    if ((err = SrsMp4FullBox::decode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "decode header");
    }
    
    if ((err = srs_mp4_string_read(buf, location, left_space(buf))) != srs_success) {
        return srs_error_wrap(err, "url read location");
    }
    
    return err;
}

stringstream& SrsMp4DataEntryUrlBox::dumps_detail(stringstream& ss, SrsMp4DumpContext dc)
{
    SrsMp4FullBox::dumps_detail(ss, dc);

    ss << ", URL: " << location;
    if (location.empty()) {
        ss << "Same file";
    }
    return ss;
}

SrsMp4DataEntryUrnBox::SrsMp4DataEntryUrnBox()
{
    type = SrsMp4BoxTypeURN;
}

SrsMp4DataEntryUrnBox::~SrsMp4DataEntryUrnBox()
{
}

int SrsMp4DataEntryUrnBox::nb_header()
{
    return SrsMp4FullBox::nb_header()+srs_mp4_string_length(location)+srs_mp4_string_length(name);
}

srs_error_t SrsMp4DataEntryUrnBox::encode_header(SrsBuffer* buf)
{
    srs_error_t err = srs_success;

    // a 24-bit integer with flags; one flag is defined (x000001) which means that the media
    // data is in the same file as the Movie Box containing this data reference.
    if (location.empty()) {
        flags = 0x01;
    }
    
    if ((err = SrsMp4DataEntryBox::encode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "encode entry");
    }
    
    srs_mp4_string_write(buf, location);
    srs_mp4_string_write(buf, name);
    
    return err;
}

srs_error_t SrsMp4DataEntryUrnBox::decode_header(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    if ((err = SrsMp4DataEntryBox::decode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "decode entry");
    }
    
    if ((err = srs_mp4_string_read(buf, location, left_space(buf))) != srs_success) {
        return srs_error_wrap(err, "urn read location");
    }
    
    if ((err = srs_mp4_string_read(buf, name, left_space(buf))) != srs_success) {
        return srs_error_wrap(err, "urn read name");
    }
    
    return err;
}

stringstream& SrsMp4DataEntryUrnBox::dumps_detail(stringstream& ss, SrsMp4DumpContext dc)
{
    SrsMp4FullBox::dumps_detail(ss, dc);

    ss << ", URL: " << location;
    if (location.empty()) {
        ss << "Same file";
    }
    if (!name.empty()) {
        ss << ", " << name;
    }

    return ss;
}

SrsMp4DataReferenceBox::SrsMp4DataReferenceBox()
{
    type = SrsMp4BoxTypeDREF;
}

SrsMp4DataReferenceBox::~SrsMp4DataReferenceBox()
{
    vector<SrsMp4DataEntryBox*>::iterator it;
    for (it = entries.begin(); it != entries.end(); ++it) {
        SrsMp4DataEntryBox* entry = *it;
        srs_freep(entry);
    }
    entries.clear();
}

uint32_t SrsMp4DataReferenceBox::entry_count()
{
    return (uint32_t)entries.size();
}

SrsMp4DataEntryBox* SrsMp4DataReferenceBox::entry_at(int index)
{
    return entries.at(index);
}

void SrsMp4DataReferenceBox::append(SrsMp4Box* v)
{
    SrsMp4DataEntryBox* pv = dynamic_cast<SrsMp4DataEntryBox*>(v);
    if (pv) {
        entries.push_back(pv);
    }
}

int SrsMp4DataReferenceBox::nb_header()
{
    int size = SrsMp4FullBox::nb_header();
    
    size += 4;
    
    vector<SrsMp4DataEntryBox*>::iterator it;
    for (it = entries.begin(); it != entries.end(); ++it) {
        SrsMp4DataEntryBox* entry = *it;
        size += entry->nb_bytes();
    }
    
    return size;
}

srs_error_t SrsMp4DataReferenceBox::encode_header(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    if ((err = SrsMp4FullBox::encode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "encode header");
    }
    
    buf->write_4bytes((int32_t)entries.size());
    
    vector<SrsMp4DataEntryBox*>::iterator it;
    for (it = entries.begin(); it != entries.end(); ++it) {
        SrsMp4DataEntryBox* entry = *it;
        if ((err = entry->encode(buf)) != srs_success) {
            return srs_error_wrap(err, "encode entry");
        }
    }
    
    return err;
}

srs_error_t SrsMp4DataReferenceBox::decode_header(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    if ((err = SrsMp4FullBox::decode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "decode header");
    }
    
    uint32_t nb_entries = buf->read_4bytes();
    for (uint32_t i = 0; i < nb_entries; i++) {
        SrsMp4Box* box = NULL;
        if ((err = SrsMp4Box::discovery(buf, &box)) != srs_success) {
            return srs_error_wrap(err, "discovery box");
        }
        
        if ((err = box->decode(buf)) != srs_success) {
            return srs_error_wrap(err, "decode box");
        }
        
        SrsMp4FullBox* fbox = dynamic_cast<SrsMp4FullBox*>(box);
        if (fbox) {
            fbox->version = version;
            fbox->flags = flags;
        }
        
        if (box->type == SrsMp4BoxTypeURL) {
            entries.push_back(dynamic_cast<SrsMp4DataEntryUrlBox*>(box));
        } else if (box->type == SrsMp4BoxTypeURN) {
            entries.push_back(dynamic_cast<SrsMp4DataEntryUrnBox*>(box));
        } else {
            srs_freep(box);
        }
    }
    
    return err;
}

stringstream& SrsMp4DataReferenceBox::dumps_detail(stringstream& ss, SrsMp4DumpContext dc)
{
    SrsMp4FullBox::dumps_detail(ss, dc);
    
    ss << ", " << entries.size() << " childs";
    if (!entries.empty()) {
        ss << "(+)" << endl;
        srs_dumps_array(entries, ss, dc.indent(), srs_mp4_pfn_box2, srs_mp4_delimiter_newline);
    }
    return ss;
}

SrsMp4SampleTableBox::SrsMp4SampleTableBox()
{
    type = SrsMp4BoxTypeSTBL;
}

SrsMp4SampleTableBox::~SrsMp4SampleTableBox()
{
}

SrsMp4SampleDescriptionBox* SrsMp4SampleTableBox::stsd()
{
    SrsMp4Box* box = get(SrsMp4BoxTypeSTSD);
    return dynamic_cast<SrsMp4SampleDescriptionBox*>(box);
}

void SrsMp4SampleTableBox::set_stsd(SrsMp4SampleDescriptionBox* v)
{
    remove(SrsMp4BoxTypeSTSD);
    boxes.push_back(v);
}

SrsMp4ChunkOffsetBox* SrsMp4SampleTableBox::stco()
{
    SrsMp4Box* box = get(SrsMp4BoxTypeSTCO);
    return dynamic_cast<SrsMp4ChunkOffsetBox*>(box);
}

void SrsMp4SampleTableBox::set_stco(SrsMp4ChunkOffsetBox* v)
{
    remove(SrsMp4BoxTypeSTCO);
    boxes.push_back(v);
}

SrsMp4SampleSizeBox* SrsMp4SampleTableBox::stsz()
{
    SrsMp4Box* box = get(SrsMp4BoxTypeSTSZ);
    return dynamic_cast<SrsMp4SampleSizeBox*>(box);
}

void SrsMp4SampleTableBox::set_stsz(SrsMp4SampleSizeBox* v)
{
    remove(SrsMp4BoxTypeSTSZ);
    boxes.push_back(v);
}

SrsMp4Sample2ChunkBox* SrsMp4SampleTableBox::stsc()
{
    SrsMp4Box* box = get(SrsMp4BoxTypeSTSC);
    return dynamic_cast<SrsMp4Sample2ChunkBox*>(box);
}

void SrsMp4SampleTableBox::set_stsc(SrsMp4Sample2ChunkBox* v)
{
    remove(SrsMp4BoxTypeSTSC);
    boxes.push_back(v);
}

SrsMp4DecodingTime2SampleBox* SrsMp4SampleTableBox::stts()
{
    SrsMp4Box* box = get(SrsMp4BoxTypeSTTS);
    return dynamic_cast<SrsMp4DecodingTime2SampleBox*>(box);
}

void SrsMp4SampleTableBox::set_stts(SrsMp4DecodingTime2SampleBox* v)
{
    remove(SrsMp4BoxTypeSTTS);
    boxes.push_back(v);
}

SrsMp4CompositionTime2SampleBox* SrsMp4SampleTableBox::ctts()
{
    SrsMp4Box* box = get(SrsMp4BoxTypeCTTS);
    return dynamic_cast<SrsMp4CompositionTime2SampleBox*>(box);
}

void SrsMp4SampleTableBox::set_ctts(SrsMp4CompositionTime2SampleBox* v)
{
    remove(SrsMp4BoxTypeCTTS);
    boxes.push_back(v);
}

SrsMp4SyncSampleBox* SrsMp4SampleTableBox::stss()
{
    SrsMp4Box* box = get(SrsMp4BoxTypeSTSS);
    return dynamic_cast<SrsMp4SyncSampleBox*>(box);
}

void SrsMp4SampleTableBox::set_stss(SrsMp4SyncSampleBox* v)
{
    remove(SrsMp4BoxTypeSTSS);
    boxes.push_back(v);
}

int SrsMp4SampleTableBox::nb_header()
{
    return SrsMp4Box::nb_header();
}

srs_error_t SrsMp4SampleTableBox::encode_header(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    if ((err = SrsMp4Box::encode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "encode header");
    }
    
    return err;
}

srs_error_t SrsMp4SampleTableBox::decode_header(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    if ((err = SrsMp4Box::decode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "decode header");
    }
    
    return err;
}

SrsMp4SampleEntry::SrsMp4SampleEntry() : data_reference_index(0)
{
    memset(reserved, 0, 6);
}

SrsMp4SampleEntry::~SrsMp4SampleEntry()
{
}

int SrsMp4SampleEntry::nb_header()
{
    return SrsMp4Box::nb_header()+6+2;
}

srs_error_t SrsMp4SampleEntry::encode_header(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    if ((err = SrsMp4Box::encode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "encode header");
    }
    
    for (int i = 0; i < 6; i++) {
        buf->write_1bytes(reserved[i]);
    }
    buf->write_2bytes(data_reference_index);
    
    return err;
}

srs_error_t SrsMp4SampleEntry::decode_header(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    if ((err = SrsMp4Box::decode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "decode header");
    }
    
    buf->skip(6);
    data_reference_index = buf->read_2bytes();
    
    return err;
}

stringstream& SrsMp4SampleEntry::dumps_detail(stringstream& ss, SrsMp4DumpContext dc)
{
    SrsMp4Box::dumps_detail(ss, dc);
    
    ss << ", refs#" << data_reference_index;
    return ss;
}

SrsMp4VisualSampleEntry::SrsMp4VisualSampleEntry() : width(0), height(0)
{
    type = SrsMp4BoxTypeAVC1;
    
    pre_defined0 = 0;
    reserved0 = 0;
    reserved1 = 0;
    memset(pre_defined1, 0, 12);
    memset(compressorname, 0, 32);
    frame_count = 1;
    horizresolution = 0x00480000; // 72 dpi
    vertresolution = 0x00480000; // 72 dpi
    depth = 0x0018;
    pre_defined2 = -1;
}

SrsMp4VisualSampleEntry::~SrsMp4VisualSampleEntry()
{
}

SrsMp4AvccBox* SrsMp4VisualSampleEntry::avcC()
{
    SrsMp4Box* box = get(SrsMp4BoxTypeAVCC);
    return dynamic_cast<SrsMp4AvccBox*>(box);
}

void SrsMp4VisualSampleEntry::set_avcC(SrsMp4AvccBox* v)
{
    remove(SrsMp4BoxTypeAVCC);
    boxes.push_back(v);
}

int SrsMp4VisualSampleEntry::nb_header()
{
    return SrsMp4SampleEntry::nb_header()+2+2+12+2+2+4+4+4+2+32+2+2;
}

srs_error_t SrsMp4VisualSampleEntry::encode_header(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    if ((err = SrsMp4SampleEntry::encode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "encode entry");
    }
    
    buf->write_2bytes(pre_defined0);
    buf->write_2bytes(reserved0);
    buf->write_4bytes(pre_defined1[0]);
    buf->write_4bytes(pre_defined1[1]);
    buf->write_4bytes(pre_defined1[2]);
    buf->write_2bytes(width);
    buf->write_2bytes(height);
    buf->write_4bytes(horizresolution);
    buf->write_4bytes(vertresolution);
    buf->write_4bytes(reserved1);
    buf->write_2bytes(frame_count);
    buf->write_bytes(compressorname, 32);
    buf->write_2bytes(depth);
    buf->write_2bytes(pre_defined2);
    
    return err;
}

srs_error_t SrsMp4VisualSampleEntry::decode_header(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    if ((err = SrsMp4SampleEntry::decode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "decode entry");
    }
    
    buf->skip(2);
    buf->skip(2);
    buf->skip(12);
    width = buf->read_2bytes();
    height = buf->read_2bytes();
    horizresolution = buf->read_4bytes();
    vertresolution = buf->read_4bytes();
    buf->skip(4);
    frame_count = buf->read_2bytes();
    buf->read_bytes(compressorname, 32);
    depth = buf->read_2bytes();
    buf->skip(2);
    
    return err;
}

stringstream& SrsMp4VisualSampleEntry::dumps_detail(stringstream& ss, SrsMp4DumpContext dc)
{
    SrsMp4SampleEntry::dumps_detail(ss, dc);
    
    ss << ", size=" << width << "x" << height;
    return ss;
}

SrsMp4AvccBox::SrsMp4AvccBox()
{
    type = SrsMp4BoxTypeAVCC;
}

SrsMp4AvccBox::~SrsMp4AvccBox()
{
}

int SrsMp4AvccBox::nb_header()
{
    return SrsMp4Box::nb_header() + (int)avc_config.size();
}

srs_error_t SrsMp4AvccBox::encode_header(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    if ((err = SrsMp4Box::encode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "encode header");
    }
    
    if (!avc_config.empty()) {
        buf->write_bytes(&avc_config[0], (int)avc_config.size());
    }
    
    return err;
}

srs_error_t SrsMp4AvccBox::decode_header(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    if ((err = SrsMp4Box::decode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "decode header");
    }
    
    int nb_config = left_space(buf);
    if (nb_config) {
        avc_config.resize(nb_config);
        buf->read_bytes(&avc_config[0], nb_config);
    }
    
    return err;
}

stringstream& SrsMp4AvccBox::dumps_detail(stringstream& ss, SrsMp4DumpContext dc)
{
    SrsMp4Box::dumps_detail(ss, dc);
    
    ss << ", AVC Config: " << (int)avc_config.size() << "B" << endl;
    srs_mp4_padding(ss, dc.indent());
    srs_mp4_print_bytes(ss, (const char*)&avc_config[0], (int)avc_config.size(), dc.indent());
    return ss;
}

SrsMp4AudioSampleEntry::SrsMp4AudioSampleEntry() : samplerate(0)
{
    type = SrsMp4BoxTypeMP4A;
    
    reserved0 = 0;
    pre_defined0 = 0;
    reserved1 = 0;
    channelcount = 2;
    samplesize = 16;
}

SrsMp4AudioSampleEntry::~SrsMp4AudioSampleEntry()
{
}

SrsMp4EsdsBox* SrsMp4AudioSampleEntry::esds()
{
    SrsMp4Box* box = get(SrsMp4BoxTypeESDS);
    return dynamic_cast<SrsMp4EsdsBox*>(box);
}

void SrsMp4AudioSampleEntry::set_esds(SrsMp4EsdsBox* v)
{
    remove(SrsMp4BoxTypeESDS);
    boxes.push_back(v);
}

SrsMp4DecoderSpecificInfo* SrsMp4AudioSampleEntry::asc()
{
    SrsMp4EsdsBox* box = esds();
    return box? box->asc():NULL;
}

int SrsMp4AudioSampleEntry::nb_header()
{
    return SrsMp4SampleEntry::nb_header()+8+2+2+2+2+4;
}

srs_error_t SrsMp4AudioSampleEntry::encode_header(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    if ((err = SrsMp4SampleEntry::encode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "encode entry");
    }
    
    buf->write_8bytes(reserved0);
    buf->write_2bytes(channelcount);
    buf->write_2bytes(samplesize);
    buf->write_2bytes(pre_defined0);
    buf->write_2bytes(reserved1);
    buf->write_4bytes(samplerate);
    
    return err;
}

srs_error_t SrsMp4AudioSampleEntry::decode_header(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    if ((err = SrsMp4SampleEntry::decode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "decode entry");
    }
    
    buf->skip(8);
    channelcount = buf->read_2bytes();
    samplesize = buf->read_2bytes();
    buf->skip(2);
    buf->skip(2);
    samplerate = buf->read_4bytes();
    
    return err;
}

stringstream& SrsMp4AudioSampleEntry::dumps_detail(stringstream& ss, SrsMp4DumpContext dc)
{
    SrsMp4SampleEntry::dumps_detail(ss, dc);
    
    ss << ", " << channelcount << " channels, " << samplesize << " bits"
        << ", " << (samplerate>>16) << " Hz";
    return ss;
}

SrsMp4BaseDescriptor::SrsMp4BaseDescriptor()
{
    tag = SrsMp4ESTagESforbidden;
    vlen = -1;
    start_pos = 0;
}

SrsMp4BaseDescriptor::~SrsMp4BaseDescriptor()
{
}

int SrsMp4BaseDescriptor::left_space(SrsBuffer* buf)
{
    int left = vlen - (buf->pos() - start_pos);
    return srs_max(0, left);
}

int SrsMp4BaseDescriptor::nb_bytes()
{
    // 1 byte tag.
    int size = 1;
    
    // 1-3 bytes size.
    int32_t length = vlen = nb_payload(); // bit(8) to bit(32)
    if (length > 0x1fffff) {
        size += 4;
    } else if (length > 0x3fff) {
        size += 3;
    } else if (length > 0x7f) {
        size += 2;
    } else {
        size += 1;
    }
    
    // length bytes payload.
    size += length;
    
    return size;
}

srs_error_t SrsMp4BaseDescriptor::encode(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    int size = nb_bytes();
    if (!buf->require(size)) {
        return srs_error_new(ERROR_MP4_BOX_REQUIRE_SPACE, "ES requires %d only %d bytes", size, buf->left());
    }
    
    buf->write_1bytes((uint8_t)tag);
    
    // As an expandable class the size of each class instance in bytes is encoded and accessible
    // through the instance variable sizeOfInstance (see 8.3.3).
    int32_t length = vlen; // bit(8) to bit(32)
    srs_assert(vlen > 0);
    
    if (length > 0x1fffff) {
        buf->write_1bytes(uint8_t(length>>21)|0x80);
    }
    if (length > 0x3fff) {
        buf->write_1bytes(uint8_t(length>>14)|0x80);
    }
    if (length > 0x7f) {
        buf->write_1bytes(uint8_t(length>>7)|0x80);
    }
    buf->write_1bytes(length&0x7f);
    
    if ((err = encode_payload(buf)) != srs_success) {
        return srs_error_wrap(err, "encode payload");
    }
    
    return err;
}

srs_error_t SrsMp4BaseDescriptor::decode(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    int size = nb_bytes();
    if (!buf->require(size)) {
        return srs_error_new(ERROR_MP4_BOX_REQUIRE_SPACE, "requires %d only %d bytes", size, buf->left());
    }
    
    tag = (SrsMp4ESTagEs)buf->read_1bytes();
    
    uint8_t v = 0x80;
    int32_t length = 0x00;
    while ((v&0x80) == 0x80) {
        if (!buf->require(1)) {
            return srs_error_new(ERROR_MP4_BOX_REQUIRE_SPACE, "ES requires 1 only %d bytes", buf->left());
        }
        v = buf->read_1bytes();
        
        length = (length<<7) | (v&0x7f);
    }
    vlen = length;
    
    if (!buf->require(vlen)) {
        return srs_error_new(ERROR_MP4_BOX_REQUIRE_SPACE, "ES requires %d only %d bytes", vlen, buf->left());
    }
    
    start_pos = buf->pos();
    
    if ((err = decode_payload(buf)) != srs_success) {
        return srs_error_wrap(err, "decode payload");
    }
    
    return err;
}

stringstream& SrsMp4BaseDescriptor::dumps_detail(stringstream& ss, SrsMp4DumpContext dc)
{
    ss << ", tag=" << "0x" << std::setw(2) << std::setfill('0') << std::hex << (uint32_t)(uint8_t)tag << std::dec;
    return ss;
}

SrsMp4DecoderSpecificInfo::SrsMp4DecoderSpecificInfo()
{
    tag = SrsMp4ESTagESDecSpecificInfoTag;
}

SrsMp4DecoderSpecificInfo::~SrsMp4DecoderSpecificInfo()
{
}

int32_t SrsMp4DecoderSpecificInfo::nb_payload()
{
    return (int)asc.size();
}

srs_error_t SrsMp4DecoderSpecificInfo::encode_payload(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    if (!asc.empty()) {
        buf->write_bytes(&asc[0], (int)asc.size());
    }
    
    return err;
}

srs_error_t SrsMp4DecoderSpecificInfo::decode_payload(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    int nb_asc = vlen;
    if (nb_asc) {
        asc.resize(nb_asc);
        buf->read_bytes(&asc[0], nb_asc);
    }
    
    return err;
}

stringstream& SrsMp4DecoderSpecificInfo::dumps_detail(stringstream& ss, SrsMp4DumpContext dc)
{
    SrsMp4BaseDescriptor::dumps_detail(ss, dc);
    
    ss << ", ASC " << asc.size() << "B";
    
    ss << endl;
    srs_mp4_padding(ss, dc.indent());
    return srs_mp4_print_bytes(ss, (const char*)&asc[0], (int)asc.size(), dc.indent());
}

SrsMp4DecoderConfigDescriptor::SrsMp4DecoderConfigDescriptor() : upStream(0), bufferSizeDB(0), maxBitrate(0), avgBitrate(0)
{
    tag = SrsMp4ESTagESDecoderConfigDescrTag;
    objectTypeIndication = SrsMp4ObjectTypeForbidden;
    streamType = SrsMp4StreamTypeForbidden;
    decSpecificInfo = NULL;
    reserved = 1;
}

SrsMp4DecoderConfigDescriptor::~SrsMp4DecoderConfigDescriptor()
{
    srs_freep(decSpecificInfo);
}

int32_t SrsMp4DecoderConfigDescriptor::nb_payload()
{
    return 13 + (decSpecificInfo? decSpecificInfo->nb_bytes():0);
}

srs_error_t SrsMp4DecoderConfigDescriptor::encode_payload(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    buf->write_1bytes(objectTypeIndication);
    
    uint8_t v = reserved;
    v |= (upStream&0x01)<<1;
    v |= uint8_t(streamType&0x3f)<<2;
    buf->write_1bytes(v);
    
    buf->write_3bytes(bufferSizeDB);
    buf->write_4bytes(maxBitrate);
    buf->write_4bytes(avgBitrate);
    
    if (decSpecificInfo && (err = decSpecificInfo->encode(buf)) != srs_success) {
        return srs_error_wrap(err, "encode des specific info");
    }
    
    return err;
}

srs_error_t SrsMp4DecoderConfigDescriptor::decode_payload(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    objectTypeIndication = (SrsMp4ObjectType)buf->read_1bytes();
    
    uint8_t v = buf->read_1bytes();
    upStream = (v>>1) & 0x01;
    streamType = (SrsMp4StreamType)((v>>2) & 0x3f);
    reserved = v&0x01;
    
    bufferSizeDB = buf->read_3bytes();
    maxBitrate = buf->read_4bytes();
    avgBitrate = buf->read_4bytes();
    
    int left = left_space(buf);
    if (left > 0) {
        decSpecificInfo = new SrsMp4DecoderSpecificInfo();
        if ((err = decSpecificInfo->decode(buf)) != srs_success) {
            return srs_error_wrap(err, "decode dec specific info");
        }
    }
    
    return err;
}

stringstream& SrsMp4DecoderConfigDescriptor::dumps_detail(stringstream& ss, SrsMp4DumpContext dc)
{
    SrsMp4BaseDescriptor::dumps_detail(ss, dc);
    
    ss << ", type=" << objectTypeIndication << ", stream=" << streamType;
    
    ss << endl;
    srs_mp4_padding(ss, dc.indent());
    
    ss << "decoder specific";
    if (decSpecificInfo) {
        decSpecificInfo->dumps_detail(ss, dc.indent());
    }

    return ss;
}

SrsMp4SLConfigDescriptor::SrsMp4SLConfigDescriptor()
{
    tag = SrsMp4ESTagESSLConfigDescrTag;
    predefined = 2;
}

SrsMp4SLConfigDescriptor::~SrsMp4SLConfigDescriptor()
{
}

int32_t SrsMp4SLConfigDescriptor::nb_payload()
{
    return 1;
}

srs_error_t SrsMp4SLConfigDescriptor::encode_payload(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    buf->write_1bytes(predefined);
    
    return err;
}

srs_error_t SrsMp4SLConfigDescriptor::decode_payload(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    predefined = buf->read_1bytes();
    
    // TODO: FIXME: To support complete SL Config.
    if (predefined != 0x02) {
        return srs_error_new(ERROR_MP4_ESDS_SL_Config, "illegal ESDS SL Config, predefined=%d", predefined);
    }
    
    return err;
}

SrsMp4ES_Descriptor::SrsMp4ES_Descriptor() : ES_ID(0), dependsOn_ES_ID(0), OCR_ES_Id(0)
{
    tag = SrsMp4ESTagESDescrTag;
    streamPriority = streamDependenceFlag = URL_Flag = OCRstreamFlag = 0;
}

SrsMp4ES_Descriptor::~SrsMp4ES_Descriptor()
{
}

int32_t SrsMp4ES_Descriptor::nb_payload()
{
    int size = 2 +1;
    size += streamDependenceFlag? 2:0;
    if (URL_Flag) {
        size += 1 + URLstring.size();
    }
    size += OCRstreamFlag? 2:0;
    size += decConfigDescr.nb_bytes() +slConfigDescr.nb_bytes();
    return size;
}

srs_error_t SrsMp4ES_Descriptor::encode_payload(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    buf->write_2bytes(ES_ID);
    
    uint8_t v = streamPriority & 0x1f;
    v |= (streamDependenceFlag & 0x01) << 7;
    v |= (URL_Flag & 0x01) << 6;
    v |= (OCRstreamFlag & 0x01) << 5;
    buf->write_1bytes(v);
    
    if (streamDependenceFlag) {
        buf->write_2bytes(dependsOn_ES_ID);
    }
    
    if (URL_Flag && !URLstring.empty()) {
        buf->write_1bytes(URLstring.size());
        buf->write_bytes(&URLstring[0], (int)URLstring.size());
    }
    
    if (OCRstreamFlag) {
        buf->write_2bytes(OCR_ES_Id);
    }
    
    if ((err = decConfigDescr.encode(buf)) != srs_success) {
        return srs_error_wrap(err, "encode dec config");
    }
    
    if ((err = slConfigDescr.encode(buf)) != srs_success) {
        return srs_error_wrap(err, "encode sl config");
    }
    
    return err;
}

srs_error_t SrsMp4ES_Descriptor::decode_payload(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    ES_ID = buf->read_2bytes();
    
    uint8_t v = buf->read_1bytes();
    streamPriority = v & 0x1f;
    streamDependenceFlag = (v >> 7) & 0x01;
    URL_Flag = (v >> 6) & 0x01;
    OCRstreamFlag = (v >> 5) & 0x01;
    
    if (streamDependenceFlag) {
        if (!buf->require(2)) {
            return srs_error_new(ERROR_MP4_BOX_REQUIRE_SPACE, "ES requires 2 only %d bytes", buf->left());
        }
        dependsOn_ES_ID = buf->read_2bytes();
    }
    
    if (URL_Flag) {
        if (!buf->require(1)) {
            return srs_error_new(ERROR_MP4_BOX_REQUIRE_SPACE, "URLlength requires 1 only %d bytes", buf->left());
        }
        uint8_t URLlength = buf->read_1bytes();
        
        if (!buf->require(URLlength)) {
            return srs_error_new(ERROR_MP4_BOX_REQUIRE_SPACE, "URL requires %d only %d bytes", URLlength, buf->left());
        }
        URLstring.resize(URLlength);
        buf->read_bytes(&URLstring[0], URLlength);
    }
    
    if (OCRstreamFlag) {
        if (!buf->require(2)) {
            return srs_error_new(ERROR_MP4_BOX_REQUIRE_SPACE, "OCR requires 2 only %d bytes", buf->left());
        }
        OCR_ES_Id = buf->read_2bytes();
    }
    
    if ((err = decConfigDescr.decode(buf)) != srs_success) {
        return srs_error_wrap(err, "decode dec config");
    }
    
    if ((err = slConfigDescr.decode(buf)) != srs_success) {
        return srs_error_wrap(err, "decode sl config");
    }
    
    return err;
}

stringstream& SrsMp4ES_Descriptor::dumps_detail(stringstream& ss, SrsMp4DumpContext dc)
{
    SrsMp4BaseDescriptor::dumps_detail(ss, dc);
    
    ss << ", ID=" << ES_ID;
    
    ss << endl;
    srs_mp4_padding(ss, dc.indent());
    
    ss << "decoder config";
    decConfigDescr.dumps_detail(ss, dc.indent());
    return ss;
}

SrsMp4EsdsBox::SrsMp4EsdsBox()
{
    type = SrsMp4BoxTypeESDS;
    es = new SrsMp4ES_Descriptor();
}

SrsMp4EsdsBox::~SrsMp4EsdsBox()
{
    srs_freep(es);
}

SrsMp4DecoderSpecificInfo* SrsMp4EsdsBox::asc()
{
    return es->decConfigDescr.decSpecificInfo;
}

int SrsMp4EsdsBox::nb_header()
{
    return SrsMp4FullBox::nb_header() + es->nb_bytes();
}

srs_error_t SrsMp4EsdsBox::encode_header(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    if ((err = SrsMp4FullBox::encode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "encode header");
    }
    
    int left = left_space(buf);
    SrsBuffer buffer(buf->data() + buf->pos(), left);
    if ((err = es->encode(&buffer)) != srs_success) {
        return srs_error_wrap(err, "encode es");
    }
    
    buf->skip(buffer.pos());
    
    return err;
}

srs_error_t SrsMp4EsdsBox::decode_header(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    if ((err = SrsMp4FullBox::decode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "decode header");
    }
    
    int left = left_space(buf);
    SrsBuffer buffer(buf->data() + buf->pos(), left);
    if ((err = es->decode(&buffer)) != srs_success) {
        return srs_error_wrap(err, "decode es");
    }
    
    buf->skip(buffer.pos());
    
    return err;
}

stringstream& SrsMp4EsdsBox::dumps_detail(stringstream& ss, SrsMp4DumpContext dc)
{
    SrsMp4FullBox::dumps_detail(ss, dc);
    
    return es->dumps_detail(ss, dc);
}

SrsMp4SampleDescriptionBox::SrsMp4SampleDescriptionBox()
{
    type = SrsMp4BoxTypeSTSD;
}

SrsMp4SampleDescriptionBox::~SrsMp4SampleDescriptionBox()
{
    vector<SrsMp4SampleEntry*>::iterator it;
    for (it = entries.begin(); it != entries.end(); ++it) {
        SrsMp4SampleEntry* entry = *it;
        srs_freep(entry);
    }
    entries.clear();
}

SrsMp4VisualSampleEntry* SrsMp4SampleDescriptionBox::avc1()
{
    vector<SrsMp4SampleEntry*>::iterator it;
    for (it = entries.begin(); it != entries.end(); ++it) {
        SrsMp4SampleEntry* entry = *it;
        if (entry->type == SrsMp4BoxTypeAVC1) {
            return dynamic_cast<SrsMp4VisualSampleEntry*>(entry);
        }
    }
    return NULL;
}

SrsMp4AudioSampleEntry* SrsMp4SampleDescriptionBox::mp4a()
{
    vector<SrsMp4SampleEntry*>::iterator it;
    for (it = entries.begin(); it != entries.end(); ++it) {
        SrsMp4SampleEntry* entry = *it;
        if (entry->type == SrsMp4BoxTypeMP4A) {
            return dynamic_cast<SrsMp4AudioSampleEntry*>(entry);
        }
    }
    return NULL;
}

uint32_t SrsMp4SampleDescriptionBox::entry_count()
{
    return (uint32_t)entries.size();
}

SrsMp4SampleEntry* SrsMp4SampleDescriptionBox::entrie_at(int index)
{
    return entries.at(index);
}

void SrsMp4SampleDescriptionBox::append(SrsMp4Box* v)
{
    SrsMp4SampleEntry* pv = dynamic_cast<SrsMp4SampleEntry*>(v);
    if (pv) {
        entries.push_back(pv);
    }
}

int SrsMp4SampleDescriptionBox::nb_header()
{
    int size = SrsMp4FullBox::nb_header();
    
    size += 4;
    
    vector<SrsMp4SampleEntry*>::iterator it;
    for (it = entries.begin(); it != entries.end(); ++it) {
        SrsMp4SampleEntry* entry = *it;
        size += entry->nb_bytes();
    }
    
    return size;
}

srs_error_t SrsMp4SampleDescriptionBox::encode_header(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    if ((err = SrsMp4FullBox::encode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "encode header");
    }
    
    buf->write_4bytes(entry_count());
    
    vector<SrsMp4SampleEntry*>::iterator it;
    for (it = entries.begin(); it != entries.end(); ++it) {
        SrsMp4SampleEntry* entry = *it;
        if ((err = entry->encode(buf)) != srs_success) {
            return srs_error_wrap(err, "encode entry");
        }
    }
    
    return err;
}

srs_error_t SrsMp4SampleDescriptionBox::decode_header(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    if ((err = SrsMp4FullBox::decode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "decode header");
    }
    
    uint32_t nb_entries = buf->read_4bytes();
    for (uint32_t i = 0; i < nb_entries; i++) {
        SrsMp4Box* box = NULL;
        if ((err = SrsMp4Box::discovery(buf, &box)) != srs_success) {
            return srs_error_wrap(err, "discovery box");
        }
        
        if ((err = box->decode(buf)) != srs_success) {
            return srs_error_wrap(err, "decode box");
        }
        
        SrsMp4SampleEntry* entry = dynamic_cast<SrsMp4SampleEntry*>(box);
        if (entry) {
            entries.push_back(entry);
        } else {
            srs_freep(box);
        }
    }
    
    return err;
}

bool SrsMp4SampleDescriptionBox::boxes_in_header()
{
    return true;
}

stringstream& SrsMp4SampleDescriptionBox::dumps_detail(stringstream& ss, SrsMp4DumpContext dc)
{
    SrsMp4FullBox::dumps_detail(ss, dc);
    
    ss << ", " << entries.size() << " childs";
    if (!entries.empty()) {
        ss << "(+)" << endl;
        srs_dumps_array(entries, ss, dc.indent(), srs_mp4_pfn_box2, srs_mp4_delimiter_newline);
    }
    return ss;
}

SrsMp4SttsEntry::SrsMp4SttsEntry()
{
    sample_count = 0;
    sample_delta = 0;
}

SrsMp4SttsEntry::~SrsMp4SttsEntry()
{
}

stringstream& SrsMp4SttsEntry::dumps_detail(stringstream& ss, SrsMp4DumpContext dc)
{
    ss << "count=" << sample_count << ", delta=" << sample_delta;
    return ss;
}

SrsMp4DecodingTime2SampleBox::SrsMp4DecodingTime2SampleBox()
{
    type = SrsMp4BoxTypeSTTS;
    
    index = count = 0;
}

SrsMp4DecodingTime2SampleBox::~SrsMp4DecodingTime2SampleBox()
{
}

srs_error_t SrsMp4DecodingTime2SampleBox::initialize_counter()
{
    srs_error_t err = srs_success;

    // If only sps/pps and no frames, there is no stts entries.
    if (entries.empty()) {
        return err;
    }
    
    index = 0;
    if (index >= entries.size()) {
        return srs_error_new(ERROR_MP4_ILLEGAL_TIMESTAMP, "illegal ts, empty stts");
    }
    
    count = entries[0].sample_count;
    
    return err;
}

srs_error_t SrsMp4DecodingTime2SampleBox::on_sample(uint32_t sample_index, SrsMp4SttsEntry** ppentry)
{
    srs_error_t err = srs_success;
    
    if (sample_index + 1 > count) {
        index++;
        
        if (index >= entries.size()) {
            return srs_error_new(ERROR_MP4_ILLEGAL_TIMESTAMP, "illegal ts, stts overflow, count=%d", entries.size());
        }
        
        count += entries[index].sample_count;
    }
    
    *ppentry = &entries[index];
    
    return err;
}

int SrsMp4DecodingTime2SampleBox::nb_header()
{
    return SrsMp4FullBox::nb_header() + 4 + 8*(int)entries.size();
}

srs_error_t SrsMp4DecodingTime2SampleBox::encode_header(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    if ((err = SrsMp4FullBox::encode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "encode header");
    }
    
    buf->write_4bytes((int)entries.size());
    for (size_t i = 0; i < (size_t)entries.size(); i++) {
        SrsMp4SttsEntry& entry = entries[i];
        buf->write_4bytes(entry.sample_count);
        buf->write_4bytes(entry.sample_delta);
    }
    
    return err;
}

srs_error_t SrsMp4DecodingTime2SampleBox::decode_header(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    if ((err = SrsMp4FullBox::decode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "decode header");
    }
    
    uint32_t entry_count = buf->read_4bytes();
    if (entry_count) {
        entries.resize(entry_count);
    }
    for (size_t i = 0; i < (size_t)entry_count; i++) {
        if (!buf->require(8)) {
            return srs_error_new(ERROR_MP4_BOX_REQUIRE_SPACE, "no space");
        }

        SrsMp4SttsEntry& entry = entries[i];
        entry.sample_count = buf->read_4bytes();
        entry.sample_delta = buf->read_4bytes();
    }
    
    return err;
}

stringstream& SrsMp4DecodingTime2SampleBox::dumps_detail(stringstream& ss, SrsMp4DumpContext dc)
{
    SrsMp4FullBox::dumps_detail(ss, dc);
    
    ss << ", " << entries.size() << " childs (+)";
    if (!entries.empty()) {
        ss << endl;
        srs_mp4_padding(ss, dc.indent());
        srs_dumps_array(entries, ss, dc.indent(), srs_mp4_pfn_detail, srs_mp4_delimiter_newline);
    }
    return ss;
}

SrsMp4CttsEntry::SrsMp4CttsEntry()
{
    sample_count = 0;
    sample_offset = 0;
}

SrsMp4CttsEntry::~SrsMp4CttsEntry()
{
}

stringstream& SrsMp4CttsEntry::dumps_detail(stringstream& ss, SrsMp4DumpContext dc)
{
    ss << "count=" << sample_count << ", offset=" << sample_offset;
    return ss;
}

SrsMp4CompositionTime2SampleBox::SrsMp4CompositionTime2SampleBox()
{
    type = SrsMp4BoxTypeCTTS;
    
    index = count = 0;
}

SrsMp4CompositionTime2SampleBox::~SrsMp4CompositionTime2SampleBox()
{
}

srs_error_t SrsMp4CompositionTime2SampleBox::initialize_counter()
{
    srs_error_t err = srs_success;

    // If only sps/pps and no frames, there is no stts entries.
    if (entries.empty()) {
        return err;
    }
    
    index = 0;
    if (index >= entries.size()) {
        return srs_error_new(ERROR_MP4_ILLEGAL_TIMESTAMP, "illegal ts, empty ctts");
    }
    
    count = entries[0].sample_count;
    
    return err;
}

srs_error_t SrsMp4CompositionTime2SampleBox::on_sample(uint32_t sample_index, SrsMp4CttsEntry** ppentry)
{
    srs_error_t err = srs_success;
    
    if (sample_index + 1 > count) {
        index++;
        
        if (index >= entries.size()) {
            return srs_error_new(ERROR_MP4_ILLEGAL_TIMESTAMP, "illegal ts, ctts overflow, count=%d", entries.size());
        }
        
        count += entries[index].sample_count;
    }
    
    *ppentry = &entries[index];
    
    return err;
}

int SrsMp4CompositionTime2SampleBox::nb_header()
{
    return SrsMp4FullBox::nb_header() + 4 + 8 * (int)entries.size();
}

srs_error_t SrsMp4CompositionTime2SampleBox::encode_header(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    if ((err = SrsMp4FullBox::encode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "encode header");
    }
    
    buf->write_4bytes((int)entries.size());
    for (size_t i = 0; i < (size_t)entries.size(); i++) {
        SrsMp4CttsEntry& entry = entries[i];
        buf->write_4bytes(entry.sample_count);
        if (version == 0) {
            buf->write_4bytes((uint32_t)entry.sample_offset);
        } else if (version == 1) {
            buf->write_4bytes((int32_t)entry.sample_offset);
        }
    }
    
    return err;
}

srs_error_t SrsMp4CompositionTime2SampleBox::decode_header(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    if ((err = SrsMp4FullBox::decode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "decode header");
    }
    
    uint32_t entry_count = buf->read_4bytes();
    if (entry_count) {
        entries.resize(entry_count);
    }
    for (size_t i = 0; i < (size_t)entry_count; i++) {
        SrsMp4CttsEntry& entry = entries[i];
        entry.sample_count = buf->read_4bytes();
        if (version == 0) {
            entry.sample_offset = (uint32_t)buf->read_4bytes();
        } else if (version == 1) {
            entry.sample_offset = (int32_t)buf->read_4bytes();
        }
    }
    
    return err;
}

stringstream& SrsMp4CompositionTime2SampleBox::dumps_detail(stringstream& ss, SrsMp4DumpContext dc)
{
    SrsMp4FullBox::dumps_detail(ss, dc);
    
    ss << ", " << entries.size() << " childs (+)";
    if (!entries.empty()) {
        ss << endl;
        srs_mp4_padding(ss, dc.indent());
        srs_dumps_array(entries, ss, dc.indent(), srs_mp4_pfn_detail, srs_mp4_delimiter_newline);
    }
    return ss;
}

SrsMp4SyncSampleBox::SrsMp4SyncSampleBox()
{
    type = SrsMp4BoxTypeSTSS;
    
    entry_count = 0;
    sample_numbers = NULL;
}

SrsMp4SyncSampleBox::~SrsMp4SyncSampleBox()
{
    srs_freepa(sample_numbers);
}

bool SrsMp4SyncSampleBox::is_sync(uint32_t sample_index)
{
    for (uint32_t i = 0; i < entry_count; i++) {
        if (sample_index + 1 == sample_numbers[i]) {
            return true;
        }
    }
    return false;
}

int SrsMp4SyncSampleBox::nb_header()
{
    return SrsMp4FullBox::nb_header() +4 +4*entry_count;
}

srs_error_t SrsMp4SyncSampleBox::encode_header(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    if ((err = SrsMp4FullBox::encode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "encode header");
    }
    
    buf->write_4bytes(entry_count);
    for (uint32_t i = 0; i < entry_count; i++) {
        uint32_t sample_number = sample_numbers[i];
        buf->write_4bytes(sample_number);
    }
    
    return err;
}

srs_error_t SrsMp4SyncSampleBox::decode_header(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    if ((err = SrsMp4FullBox::decode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "decode header");
    }
    
    entry_count = buf->read_4bytes();
    if (entry_count > 0) {
        sample_numbers = new uint32_t[entry_count];
    }
    for (uint32_t i = 0; i < entry_count; i++) {
        sample_numbers[i] = buf->read_4bytes();
    }
    
    return err;
}

stringstream& SrsMp4SyncSampleBox::dumps_detail(stringstream& ss, SrsMp4DumpContext dc)
{
    SrsMp4FullBox::dumps_detail(ss, dc);
    
    ss << ", count=" << entry_count;
    if (entry_count > 0) {
        ss << endl;
        srs_mp4_padding(ss, dc.indent());
        srs_dumps_array(sample_numbers, entry_count, ss, dc.indent(), srs_mp4_pfn_elem, srs_mp4_delimiter_inspace);
    }
    return ss;
}

SrsMp4StscEntry::SrsMp4StscEntry()
{
    first_chunk = 0;
    samples_per_chunk = 0;
    sample_description_index = 0;
}

stringstream& SrsMp4StscEntry::dumps_detail(stringstream& ss, SrsMp4DumpContext dc)
{
    ss << "first=" << first_chunk << ", samples=" << samples_per_chunk << ", index=" << sample_description_index;
    return ss;
}

SrsMp4Sample2ChunkBox::SrsMp4Sample2ChunkBox()
{
    type = SrsMp4BoxTypeSTSC;
    
    entry_count = 0;
    entries = NULL;
    index = 0;
}

SrsMp4Sample2ChunkBox::~SrsMp4Sample2ChunkBox()
{
    srs_freepa(entries);
}

void SrsMp4Sample2ChunkBox::initialize_counter()
{
    index = 0;
}

SrsMp4StscEntry* SrsMp4Sample2ChunkBox::on_chunk(uint32_t chunk_index)
{
    // Last chunk?
    if (index >= entry_count - 1) {
        return &entries[index];
    }
    
    // Move next chunk?
    if (chunk_index + 1 >= entries[index + 1].first_chunk) {
        index++;
    }
    return &entries[index];
}

int SrsMp4Sample2ChunkBox::nb_header()
{
    return SrsMp4FullBox::nb_header() +4 + 12*entry_count;
}

srs_error_t SrsMp4Sample2ChunkBox::encode_header(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    if ((err = SrsMp4FullBox::encode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "encode header");
    }
    
    buf->write_4bytes(entry_count);
    for (uint32_t i = 0; i < entry_count; i++) {
        SrsMp4StscEntry& entry = entries[i];
        buf->write_4bytes(entry.first_chunk);
        buf->write_4bytes(entry.samples_per_chunk);
        buf->write_4bytes(entry.sample_description_index);
    }
    
    return err;
}

srs_error_t SrsMp4Sample2ChunkBox::decode_header(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    if ((err = SrsMp4FullBox::decode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "decode header");
    }
    
    entry_count = buf->read_4bytes();
    if (entry_count) {
        entries = new SrsMp4StscEntry[entry_count];
    }
    for (uint32_t i = 0; i < entry_count; i++) {
        SrsMp4StscEntry& entry = entries[i];
        entry.first_chunk = buf->read_4bytes();
        entry.samples_per_chunk = buf->read_4bytes();
        entry.sample_description_index = buf->read_4bytes();
    }
    
    return err;
}

stringstream& SrsMp4Sample2ChunkBox::dumps_detail(stringstream& ss, SrsMp4DumpContext dc)
{
    SrsMp4FullBox::dumps_detail(ss, dc);
    
    ss << ", " << entry_count << " childs (+)";
    if (entry_count > 0) {
        ss << endl;
        srs_mp4_padding(ss, dc.indent());
        srs_dumps_array(entries, entry_count, ss, dc.indent(), srs_mp4_pfn_detail, srs_mp4_delimiter_newline);
    }
    return ss;
}

SrsMp4ChunkOffsetBox::SrsMp4ChunkOffsetBox()
{
    type = SrsMp4BoxTypeSTCO;
    
    entry_count = 0;
    entries = NULL;
}

SrsMp4ChunkOffsetBox::~SrsMp4ChunkOffsetBox()
{
    srs_freepa(entries);
}

int SrsMp4ChunkOffsetBox::nb_header()
{
    return SrsMp4FullBox::nb_header() +4 +4*entry_count;
}

srs_error_t SrsMp4ChunkOffsetBox::encode_header(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    if ((err = SrsMp4FullBox::encode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "encode header");
    }
    
    buf->write_4bytes(entry_count);
    for (uint32_t i = 0; i < entry_count; i++) {
        buf->write_4bytes(entries[i]);
    }
    
    return err;
}

srs_error_t SrsMp4ChunkOffsetBox::decode_header(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    if ((err = SrsMp4FullBox::decode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "decode header");
    }
    
    entry_count = buf->read_4bytes();
    if (entry_count) {
        entries = new uint32_t[entry_count];
    }
    for (uint32_t i = 0; i < entry_count; i++) {
        entries[i] = buf->read_4bytes();
    }
    
    return err;
}

stringstream& SrsMp4ChunkOffsetBox::dumps_detail(stringstream& ss, SrsMp4DumpContext dc)
{
    SrsMp4FullBox::dumps_detail(ss, dc);
    
    ss << ", " << entry_count << " childs (+)";
    if (entry_count > 0) {
        ss << endl;
        srs_mp4_padding(ss, dc.indent());
        srs_dumps_array(entries, entry_count, ss, dc.indent(), srs_mp4_pfn_elem, srs_mp4_delimiter_inspace);
    }
    return ss;
}

SrsMp4ChunkLargeOffsetBox::SrsMp4ChunkLargeOffsetBox()
{
    type = SrsMp4BoxTypeCO64;
    
    entry_count = 0;
    entries = NULL;
}

SrsMp4ChunkLargeOffsetBox::~SrsMp4ChunkLargeOffsetBox()
{
    srs_freepa(entries);
}

int SrsMp4ChunkLargeOffsetBox::nb_header()
{
    return SrsMp4FullBox::nb_header() +4 +8*entry_count;
}

srs_error_t SrsMp4ChunkLargeOffsetBox::encode_header(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    if ((err = SrsMp4FullBox::encode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "encode header");
    }
    
    buf->write_4bytes(entry_count);
    for (uint32_t i = 0; i < entry_count; i++) {
        buf->write_8bytes(entries[i]);
    }
    
    return err;
}

srs_error_t SrsMp4ChunkLargeOffsetBox::decode_header(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    if ((err = SrsMp4FullBox::decode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "decode header");
    }
    
    entry_count = buf->read_4bytes();
    if (entry_count) {
        entries = new uint64_t[entry_count];
    }
    for (uint32_t i = 0; i < entry_count; i++) {
        entries[i] = buf->read_8bytes();
    }
    
    return err;
}

stringstream& SrsMp4ChunkLargeOffsetBox::dumps_detail(stringstream& ss, SrsMp4DumpContext dc)
{
    SrsMp4FullBox::dumps_detail(ss, dc);
    
    ss << ", " << entry_count << " childs (+)";
    if (entry_count > 0) {
        ss << endl;
        srs_mp4_padding(ss, dc.indent());
        srs_dumps_array(entries, entry_count, ss, dc.indent(), srs_mp4_pfn_elem, srs_mp4_delimiter_inspace);
    }
    return ss;
}

SrsMp4SampleSizeBox::SrsMp4SampleSizeBox()
{
    type = SrsMp4BoxTypeSTSZ;
    
    sample_size = sample_count = 0;
    entry_sizes = NULL;
}

SrsMp4SampleSizeBox::~SrsMp4SampleSizeBox()
{
    srs_freepa(entry_sizes);
}

srs_error_t SrsMp4SampleSizeBox::get_sample_size(uint32_t sample_index, uint32_t* psample_size)
{
    srs_error_t err = srs_success;
    
    if (sample_size != 0) {
        *psample_size = sample_size;
        return err;
    }
    
    if (sample_index >= sample_count) {
        return srs_error_new(ERROR_MP4_MOOV_OVERFLOW, "stsz overflow, sample_count=%d", sample_count);
    }
    *psample_size = entry_sizes[sample_index];
    
    return err;
}

int SrsMp4SampleSizeBox::nb_header()
{
    int size = SrsMp4FullBox::nb_header() +4+4;
    if (sample_size == 0) {
        size += 4*sample_count;
    }
    return size;
}

srs_error_t SrsMp4SampleSizeBox::encode_header(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    if ((err = SrsMp4FullBox::encode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "encode header");
    }
    
    buf->write_4bytes(sample_size);
    buf->write_4bytes(sample_count);
    for (uint32_t i = 0; i < sample_count && sample_size == 0; i++) {
        buf->write_4bytes(entry_sizes[i]);
    }
    
    return err;
}

srs_error_t SrsMp4SampleSizeBox::decode_header(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    if ((err = SrsMp4FullBox::decode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "decode header");
    }
    
    sample_size = buf->read_4bytes();
    sample_count = buf->read_4bytes();
    if (!sample_size && sample_count) {
        entry_sizes = new uint32_t[sample_count];
    }
    for (uint32_t i = 0; i < sample_count && sample_size == 0; i++) {
        entry_sizes[i] = buf->read_4bytes();
    }
    
    return err;
}

stringstream& SrsMp4SampleSizeBox::dumps_detail(stringstream& ss, SrsMp4DumpContext dc)
{
    SrsMp4FullBox::dumps_detail(ss, dc);
    
    ss << ", size=" << sample_size << ", " << sample_count << " childs (+)";
    if (!sample_size  && sample_count> 0) {
        ss << endl;
        srs_mp4_padding(ss, dc.indent());
        srs_dumps_array(entry_sizes, sample_count, ss, dc.indent(), srs_mp4_pfn_elem, srs_mp4_delimiter_inspace);
    }
    return ss;
}

SrsMp4UserDataBox::SrsMp4UserDataBox()
{
    type = SrsMp4BoxTypeUDTA;
}

SrsMp4UserDataBox::~SrsMp4UserDataBox()
{
}

int SrsMp4UserDataBox::nb_header()
{
    return SrsMp4Box::nb_header() + (int)data.size();
}

srs_error_t SrsMp4UserDataBox::encode_header(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    if ((err = SrsMp4Box::encode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "encode header");
    }
    
    if (!data.empty()) {
        buf->write_bytes(&data[0], (int)data.size());
    }
    
    return err;
}

srs_error_t SrsMp4UserDataBox::decode_header(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    if ((err = SrsMp4Box::decode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "decode header");
    }
    
    int nb_data = left_space(buf);
    if (nb_data) {
        data.resize(nb_data);
        buf->read_bytes(&data[0], (int)data.size());
    }
    
    return err;
}

stringstream& SrsMp4UserDataBox::dumps_detail(stringstream& ss, SrsMp4DumpContext dc)
{
    SrsMp4Box::dumps_detail(ss, dc);
    
    ss << ", total " << data.size() << "B";
    
    if (!data.empty()) {
        ss << endl;
        srs_mp4_padding(ss, dc.indent());
        srs_dumps_array(&data[0], (int)data.size(), ss, dc.indent(), srs_mp4_pfn_hex, srs_mp4_delimiter_inspace);
    }
    return ss;
}

SrsMp4SegmentIndexBox::SrsMp4SegmentIndexBox()
{
    type = SrsMp4BoxTypeSIDX;
    version = 0;
}

SrsMp4SegmentIndexBox::~SrsMp4SegmentIndexBox()
{
}

int SrsMp4SegmentIndexBox::nb_header()
{
    return SrsMp4Box::nb_header() + 4+4+4 + (!version? 8:16) + 4 + 12*entries.size();
}

srs_error_t SrsMp4SegmentIndexBox::encode_header(SrsBuffer* buf)
{
    srs_error_t err = srs_success;

    if ((err = SrsMp4Box::encode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "encode header");
    }

    buf->write_1bytes(version);
    buf->write_3bytes(flags);
    buf->write_4bytes(reference_id);
    buf->write_4bytes(timescale);
    if (!version) {
        buf->write_4bytes(earliest_presentation_time);
        buf->write_4bytes(first_offset);
    } else {
        buf->write_8bytes(earliest_presentation_time);
        buf->write_8bytes(first_offset);
    }

    buf->write_4bytes((uint32_t)entries.size());
    for (int i = 0; i < (int)entries.size(); i++) {
        SrsMp4SegmentIndexEntry& entry = entries.at(i);

        uint32_t v = uint32_t(entry.reference_type&0x01)<<31;
        v |= entry.referenced_size&0x7fffffff;
        buf->write_4bytes(v);

        buf->write_4bytes(entry.subsegment_duration);

        v = uint32_t(entry.starts_with_SAP&0x01)<<31;
        v |= uint32_t(entry.SAP_type&0x7)<<28;
        v |= entry.SAP_delta_time&0xfffffff;
        buf->write_4bytes(v);
    }

    return err;
}

srs_error_t SrsMp4SegmentIndexBox::decode_header(SrsBuffer* buf)
{
    srs_error_t err = srs_success;

    if ((err = SrsMp4Box::decode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "decode header");
    }

    version = buf->read_1bytes();
    flags = buf->read_3bytes();
    reference_id = buf->read_4bytes();
    timescale = buf->read_4bytes();

    if (!version) {
        if (!buf->require(8)) {
            return srs_error_new(ERROR_MP4_BOX_REQUIRE_SPACE, "no space");
        }
        earliest_presentation_time = buf->read_4bytes();
        first_offset = buf->read_4bytes();
    } else {
        if (!buf->require(16)) {
            return srs_error_new(ERROR_MP4_BOX_REQUIRE_SPACE, "no space");
        }
        earliest_presentation_time = buf->read_8bytes();
        first_offset = buf->read_8bytes();
    }

    uint32_t nn_entries = (uint32_t)(buf->read_4bytes() & 0xffff);
    for (uint32_t i = 0; i < nn_entries; i++) {
        if (!buf->require(12)) {
            return srs_error_new(ERROR_MP4_BOX_REQUIRE_SPACE, "no space");
        }

        SrsMp4SegmentIndexEntry entry;

        uint32_t v = buf->read_4bytes();
        entry.reference_type = uint8_t((v&0x80000000)>>31);
        entry.referenced_size = v&0x7fffffff;

        entry.subsegment_duration = buf->read_4bytes();

        v = buf->read_4bytes();
        entry.starts_with_SAP = uint8_t((v&0x80000000)>>31);
        entry.SAP_type = uint8_t((v&0x70000000)>>28);
        entry.SAP_delta_time = v&0xfffffff;

        entries.push_back(entry);
    }

    return err;
}

stringstream& SrsMp4SegmentIndexBox::dumps_detail(stringstream& ss, SrsMp4DumpContext dc)
{
    SrsMp4Box::dumps_detail(ss, dc);

    ss << ", v" << (int)version << ", flags=" << flags << ", refs#" << reference_id
        << ", TBN=" << timescale << ", ePTS=" << earliest_presentation_time;

    for (int i = 0; i < (int)entries.size(); i++) {
        SrsMp4SegmentIndexEntry& entry = entries.at(i);

        ss << endl;
        srs_mp4_padding(ss, dc.indent());
        ss << "#" << i << ", ref=" << (int)entry.reference_type << "/" << entry.referenced_size
            << ", duration=" << entry.subsegment_duration << ", SAP=" << (int)entry.starts_with_SAP
            << "/" << (int)entry.SAP_type << "/" << entry.SAP_delta_time;
    }

    return ss;
}

SrsMp4Sample::SrsMp4Sample()
{
    type = SrsFrameTypeForbidden;
    offset = 0;
    index = 0;
    dts = pts = 0;
    nb_data = 0;
    data = NULL;
    frame_type = SrsVideoAvcFrameTypeForbidden;
    tbn = 0;
    adjust = 0;
}

SrsMp4Sample::~SrsMp4Sample()
{
    srs_freepa(data);
}

uint32_t SrsMp4Sample::dts_ms()
{
    return (uint32_t)(dts * 1000 / tbn) + adjust;
}

uint32_t SrsMp4Sample::pts_ms()
{
    return (uint32_t)(pts * 1000 / tbn) + adjust;
}

SrsMp4SampleManager::SrsMp4SampleManager()
{
}

SrsMp4SampleManager::~SrsMp4SampleManager()
{
    vector<SrsMp4Sample*>::iterator it;
    for (it = samples.begin(); it != samples.end(); ++it) {
        SrsMp4Sample* sample = *it;
        srs_freep(sample);
    }
    samples.clear();
}

srs_error_t SrsMp4SampleManager::load(SrsMp4MovieBox* moov)
{
    srs_error_t err = srs_success;
    
    map<uint64_t, SrsMp4Sample*> tses;
    
    // Load samples from moov, merge to temp samples.
    if ((err = do_load(tses, moov)) != srs_success) {
        map<uint64_t, SrsMp4Sample*>::iterator it;
        for (it = tses.begin(); it != tses.end(); ++it) {
            SrsMp4Sample* sample = it->second;
            srs_freep(sample);
        }
        
        return srs_error_wrap(err, "load mp4");
    }
    
    // Dumps temp samples.
    // Adjust the sequence diff.
    int32_t maxp = 0;
    int32_t maxn = 0;
    if (true) {
        SrsMp4Sample* pvideo = NULL;
        map<uint64_t, SrsMp4Sample*>::iterator it;
        for (it = tses.begin(); it != tses.end(); ++it) {
            SrsMp4Sample* sample = it->second;
            samples.push_back(sample);
            
            if (sample->type == SrsFrameTypeVideo) {
                pvideo = sample;
            } else if (pvideo) {
                int32_t diff = sample->dts_ms() - pvideo->dts_ms();
                if (diff > 0) {
                    maxp = srs_max(maxp, diff);
                } else {
                    maxn = srs_min(maxn, diff);
                }
                pvideo = NULL;
            }
        }
    }
    
    // Adjust when one of maxp and maxn is zero,
    // that means we can adjust by add maxn or sub maxp,
    // notice that maxn is negative and maxp is positive.
    if (maxp * maxn == 0 && maxp + maxn != 0) {
        map<uint64_t, SrsMp4Sample*>::iterator it;
        for (it = tses.begin(); it != tses.end(); ++it) {
            SrsMp4Sample* sample = it->second;
            if (sample->type == SrsFrameTypeAudio) {
                sample->adjust = 0 - maxp - maxn;
            }
        }
    }
    
    return err;
}

SrsMp4Sample* SrsMp4SampleManager::at(uint32_t index)
{
    if (index < samples.size()) {
        return samples.at(index);
    }
    return NULL;
}

void SrsMp4SampleManager::append(SrsMp4Sample* sample)
{
    samples.push_back(sample);
}

srs_error_t SrsMp4SampleManager::write(SrsMp4MovieBox* moov)
{
    srs_error_t err = srs_success;
    
    SrsMp4TrackBox* vide = moov->video();
    if (vide) {
        bool has_cts = false;
        vector<SrsMp4Sample*>::iterator it;
        for (it = samples.begin(); it != samples.end(); ++it) {
            SrsMp4Sample* sample = *it;
            if (sample->dts != sample->pts && sample->type == SrsFrameTypeVideo) {
                has_cts = true;
                break;
            }
        }
        
        SrsMp4SampleTableBox* stbl = vide->stbl();
        
        SrsMp4DecodingTime2SampleBox* stts = new SrsMp4DecodingTime2SampleBox();
        stbl->set_stts(stts);
        
        SrsMp4SyncSampleBox* stss = new SrsMp4SyncSampleBox();
        stbl->set_stss(stss);
        
        SrsMp4CompositionTime2SampleBox* ctts = NULL;
        if (has_cts) {
            ctts = new SrsMp4CompositionTime2SampleBox();
            stbl->set_ctts(ctts);
        }
        
        SrsMp4Sample2ChunkBox* stsc = new SrsMp4Sample2ChunkBox();
        stbl->set_stsc(stsc);
        
        SrsMp4SampleSizeBox* stsz = new SrsMp4SampleSizeBox();
        stbl->set_stsz(stsz);
        
        SrsMp4ChunkOffsetBox* stco = new SrsMp4ChunkOffsetBox();
        stbl->set_stco(stco);
        
        if ((err = write_track(SrsFrameTypeVideo, stts, stss, ctts, stsc, stsz, stco)) != srs_success) {
            return srs_error_wrap(err, "write vide track");
        }
    }
    
    SrsMp4TrackBox* soun = moov->audio();
    if (soun) {
        SrsMp4SampleTableBox* stbl = soun->stbl();
        
        SrsMp4DecodingTime2SampleBox* stts = new SrsMp4DecodingTime2SampleBox();
        stbl->set_stts(stts);
        
        SrsMp4SyncSampleBox* stss = NULL;
        SrsMp4CompositionTime2SampleBox* ctts = NULL;
        
        SrsMp4Sample2ChunkBox* stsc = new SrsMp4Sample2ChunkBox();
        stbl->set_stsc(stsc);
        
        SrsMp4SampleSizeBox* stsz = new SrsMp4SampleSizeBox();
        stbl->set_stsz(stsz);
        
        SrsMp4ChunkOffsetBox* stco = new SrsMp4ChunkOffsetBox();
        stbl->set_stco(stco);
        
        if ((err = write_track(SrsFrameTypeAudio, stts, stss, ctts, stsc, stsz, stco)) != srs_success) {
            return srs_error_wrap(err, "write soun track");
        }
    }
    
    return err;
}

srs_error_t SrsMp4SampleManager::write(SrsMp4MovieFragmentBox* moof, uint64_t& dts)
{
    srs_error_t err = srs_success;
    
    SrsMp4TrackFragmentBox* traf = moof->traf();
    SrsMp4TrackFragmentRunBox* trun = traf->trun();
    
    trun->flags = SrsMp4TrunFlagsDataOffset | SrsMp4TrunFlagsSampleDuration
        | SrsMp4TrunFlagsSampleSize | SrsMp4TrunFlagsSampleFlag | SrsMp4TrunFlagsSampleCtsOffset;

    SrsMp4Sample* previous = NULL;
    
    vector<SrsMp4Sample*>::iterator it;
    for (it = samples.begin(); it != samples.end(); ++it) {
        SrsMp4Sample* sample = *it;
        SrsMp4TrunEntry* entry = new SrsMp4TrunEntry(trun);
        
        if (!previous) {
            previous = sample;
            entry->sample_flags = 0x02000000;
        } else {
            entry->sample_flags = 0x01000000;
        }
        
        entry->sample_duration = (uint32_t)srs_min(100, sample->dts - dts);
        if (entry->sample_duration == 0) {
            entry->sample_duration = 40;
        }
        dts = sample->dts;
        
        entry->sample_size = sample->nb_data;
        entry->sample_composition_time_offset = (int64_t)(sample->pts - sample->dts);
        if (entry->sample_composition_time_offset < 0) {
            trun->version = 1;
        }
        
        trun->entries.push_back(entry);
    }
    
    return err;
}

srs_error_t SrsMp4SampleManager::write_track(SrsFrameType track,
    SrsMp4DecodingTime2SampleBox* stts, SrsMp4SyncSampleBox* stss, SrsMp4CompositionTime2SampleBox* ctts,
    SrsMp4Sample2ChunkBox* stsc, SrsMp4SampleSizeBox* stsz, SrsMp4ChunkOffsetBox* stco)
{
    srs_error_t err = srs_success;
    
    SrsMp4SttsEntry stts_entry;
    vector<SrsMp4SttsEntry> stts_entries;
    
    SrsMp4CttsEntry ctts_entry;
    vector<SrsMp4CttsEntry> ctts_entries;
    
    vector<uint32_t> stsz_entries;
    vector<uint32_t> stco_entries;
    vector<uint32_t> stss_entries;
    
    SrsMp4Sample* previous = NULL;
    vector<SrsMp4Sample*>::iterator it;
    for (it = samples.begin(); it != samples.end(); ++it) {
        SrsMp4Sample* sample = *it;
        if (sample->type != track) {
            continue;
        }
        
        stsz_entries.push_back(sample->nb_data);
        stco_entries.push_back((uint32_t)sample->offset);
        
        if (sample->frame_type == SrsVideoAvcFrameTypeKeyFrame) {
            stss_entries.push_back(sample->index + 1);
        }
        
        if (stts) {
            if (previous) {
                uint32_t delta = (uint32_t)(sample->dts - previous->dts);
                if (stts_entry.sample_delta == 0 || stts_entry.sample_delta == delta) {
                    stts_entry.sample_delta = delta;
                    stts_entry.sample_count++;
                } else {
                    stts_entries.push_back(stts_entry);
                    stts_entry.sample_count = 1;
                    stts_entry.sample_delta = delta;
                }
            } else {
                // The first sample always in the STTS table.
                stts_entry.sample_count++;
            }
        }
        
        if (ctts) {
            int64_t offset = sample->pts - sample->dts;
            if (offset < 0) {
                ctts->version = 0x01;
            }
            if (ctts_entry.sample_count == 0 || ctts_entry.sample_offset == offset) {
                ctts_entry.sample_count++;
            } else {
                ctts_entries.push_back(ctts_entry);
                ctts_entry.sample_offset = offset;
                ctts_entry.sample_count = 1;
            }
        }
        
        previous = sample;
    }
    
    if (stts && stts_entry.sample_count) {
        stts_entries.push_back(stts_entry);
    }
    
    if (ctts && ctts_entry.sample_count) {
        ctts_entries.push_back(ctts_entry);
    }
    
    if (stts && !stts_entries.empty()) {
        stts->entries = stts_entries;
    }
    
    if (ctts && !ctts_entries.empty()) {
        ctts->entries = ctts_entries;
    }
    
    if (stsc) {
        stsc->entry_count = 1;
        stsc->entries = new SrsMp4StscEntry[1];
        
        SrsMp4StscEntry& v = stsc->entries[0];
        v.first_chunk = v.sample_description_index = v.samples_per_chunk = 1;
    }
    
    if (stsz && !stsz_entries.empty()) {
        stsz->sample_size = 0;
        stsz->sample_count = (uint32_t)stsz_entries.size();
        stsz->entry_sizes = new uint32_t[stsz->sample_count];
        for (int i = 0; i < (int)stsz->sample_count; i++) {
            stsz->entry_sizes[i] = stsz_entries.at(i);
        }
    }
    
    if (stco && !stco_entries.empty()) {
        stco->entry_count = (uint32_t)stco_entries.size();
        stco->entries = new uint32_t[stco->entry_count];
        for (int i = 0; i < (int)stco->entry_count; i++) {
            stco->entries[i] = stco_entries.at(i);
        }
    }
    
    if (stss && !stss_entries.empty()) {
        stss->entry_count = (uint32_t)stss_entries.size();
        stss->sample_numbers = new uint32_t[stss->entry_count];
        for (int i = 0; i < (int)stss->entry_count; i++) {
            stss->sample_numbers[i] = stss_entries.at(i);
        }
    }
    
    return err;
}

srs_error_t SrsMp4SampleManager::do_load(map<uint64_t, SrsMp4Sample*>& tses, SrsMp4MovieBox* moov)
{
    srs_error_t err = srs_success;
    
    SrsMp4TrackBox* vide = moov->video();
    if (vide) {
        SrsMp4MediaHeaderBox* mdhd = vide->mdhd();
        SrsMp4TrackType tt = vide->track_type();
        SrsMp4ChunkOffsetBox* stco = vide->stco();
        SrsMp4SampleSizeBox* stsz = vide->stsz();
        SrsMp4Sample2ChunkBox* stsc = vide->stsc();
        SrsMp4DecodingTime2SampleBox* stts = vide->stts();
        // The composition time to sample table is optional and must only be present if DT and CT differ for any samples.
        SrsMp4CompositionTime2SampleBox* ctts = vide->ctts();
        // If the sync sample box is not present, every sample is a sync sample.
        SrsMp4SyncSampleBox* stss = vide->stss();
        
        if (!mdhd || !stco || !stsz || !stsc || !stts) {
            return srs_error_new(ERROR_MP4_ILLEGAL_TRACK, "illegal track, empty mdhd/stco/stsz/stsc/stts, type=%d", tt);
        }
        
        if ((err = load_trak(tses, SrsFrameTypeVideo, mdhd, stco, stsz, stsc, stts, ctts, stss)) != srs_success) {
            return srs_error_wrap(err, "load vide track");
        }
    }
    
    SrsMp4TrackBox* soun = moov->audio();
    if (soun) {
        SrsMp4MediaHeaderBox* mdhd = soun->mdhd();
        SrsMp4TrackType tt = soun->track_type();
        SrsMp4ChunkOffsetBox* stco = soun->stco();
        SrsMp4SampleSizeBox* stsz = soun->stsz();
        SrsMp4Sample2ChunkBox* stsc = soun->stsc();
        SrsMp4DecodingTime2SampleBox* stts = soun->stts();
        
        if (!mdhd || !stco || !stsz || !stsc || !stts) {
            return srs_error_new(ERROR_MP4_ILLEGAL_TRACK, "illegal track, empty mdhd/stco/stsz/stsc/stts, type=%d", tt);
        }
        
        if ((err = load_trak(tses, SrsFrameTypeAudio, mdhd, stco, stsz, stsc, stts, NULL, NULL)) != srs_success) {
            return srs_error_wrap(err, "load soun track");
        }
    }
    
    return err;
}

srs_error_t SrsMp4SampleManager::load_trak(map<uint64_t, SrsMp4Sample*>& tses, SrsFrameType tt,
    SrsMp4MediaHeaderBox* mdhd, SrsMp4ChunkOffsetBox* stco, SrsMp4SampleSizeBox* stsz, SrsMp4Sample2ChunkBox* stsc,
    SrsMp4DecodingTime2SampleBox* stts, SrsMp4CompositionTime2SampleBox* ctts, SrsMp4SyncSampleBox* stss)
{
    srs_error_t err = srs_success;
    
    // Samples per chunk.
    stsc->initialize_counter();
    
    // DTS box.
    if ((err = stts->initialize_counter()) != srs_success) {
        return srs_error_wrap(err, "stts init counter");
    }
    
    // CTS/PTS box.
    if (ctts && (err = ctts->initialize_counter()) != srs_success) {
        return srs_error_wrap(err, "ctts init counter");
    }
    
    SrsMp4Sample* previous = NULL;
    
    // For each chunk offset.
    for (uint32_t ci = 0; ci < stco->entry_count; ci++) {
        // The sample offset relative in chunk.
        uint32_t sample_relative_offset = 0;
        
        // Find how many samples from stsc.
        SrsMp4StscEntry* stsc_entry = stsc->on_chunk(ci);
        for (uint32_t i = 0; i < stsc_entry->samples_per_chunk; i++) {
            SrsMp4Sample* sample = new SrsMp4Sample();
            sample->type = tt;
            sample->index = (previous? previous->index+1:0);
            sample->tbn = mdhd->timescale;
            sample->offset = stco->entries[ci] + sample_relative_offset;
            
            uint32_t sample_size = 0;
            if ((err = stsz->get_sample_size(sample->index, &sample_size)) != srs_success) {
                srs_freep(sample);
                return srs_error_wrap(err, "stsz get sample size");
            }
            sample_relative_offset += sample_size;
            
            SrsMp4SttsEntry* stts_entry = NULL;
            if ((err = stts->on_sample(sample->index, &stts_entry)) != srs_success) {
                srs_freep(sample);
                return srs_error_wrap(err, "stts on sample");
            }
            if (previous) {
                sample->pts = sample->dts = previous->dts + stts_entry->sample_delta;
            }
            
            SrsMp4CttsEntry* ctts_entry = NULL;
            if (ctts && (err = ctts->on_sample(sample->index, &ctts_entry)) != srs_success) {
                srs_freep(sample);
                return srs_error_wrap(err, "ctts on sample");
            }
            if (ctts_entry) {
                sample->pts = sample->dts + ctts_entry->sample_offset;
            }
            
            if (tt == SrsFrameTypeVideo) {
                if (!stss || stss->is_sync(sample->index)) {
                    sample->frame_type = SrsVideoAvcFrameTypeKeyFrame;
                } else {
                    sample->frame_type = SrsVideoAvcFrameTypeInterFrame;
                }
            }
            
            // Only set the sample size, read data from io when needed.
            sample->nb_data = sample_size;
            sample->data = NULL;
            
            previous = sample;
            tses[sample->offset] = sample;
        }
    }
    
    // Check total samples.
    if (previous && previous->index + 1 != stsz->sample_count) {
        return srs_error_new(ERROR_MP4_ILLEGAL_SAMPLES, "illegal samples count, expect=%d, actual=%d", stsz->sample_count, previous->index + 1);
    }
    
    return err;
}

SrsMp4BoxReader::SrsMp4BoxReader()
{
    rsio = NULL;
    buf = new char[SRS_MP4_BUF_SIZE];
}

SrsMp4BoxReader::~SrsMp4BoxReader()
{
    srs_freepa(buf);
}

srs_error_t SrsMp4BoxReader::initialize(ISrsReadSeeker* rs)
{
    rsio = rs;
    
    return srs_success;
}

srs_error_t SrsMp4BoxReader::read(SrsSimpleStream* stream, SrsMp4Box** ppbox)
{
    srs_error_t err = srs_success;
    
    SrsMp4Box* box = NULL;
    while (true) {
        // For the first time to read the box, maybe it's a basic box which is only 4bytes header.
        // When we disconvery the real box, we know the size of the whole box, then read again and decode it.
        uint64_t required = box? box->sz():4;
        
        // For mdat box, we only requires to decode the header.
        if (box && box->is_mdat()) {
            required = box->sz_header();
        }
        
        // Fill the stream util we can discovery box.
        while (stream->length() < (int)required) {
            ssize_t nread;
            if ((err = rsio->read(buf, SRS_MP4_BUF_SIZE, &nread)) != srs_success) {
                return srs_error_wrap(err, "load failed, nread=%d, required=%d", nread, required);
            }
            
            srs_assert(nread > 0);
            stream->append(buf, (int)nread);
        }
        
        SrsBuffer* buffer = new SrsBuffer(stream->bytes(), stream->length());
        SrsAutoFree(SrsBuffer, buffer);
        
        // Discovery the box with basic header.
        if (!box && (err = SrsMp4Box::discovery(buffer, &box)) != srs_success) {
            if (srs_error_code(err) == ERROR_MP4_BOX_REQUIRE_SPACE) {
                srs_freep(err);
                continue;
            }
            return srs_error_wrap(err, "load box failed");
        }
        
        // When box is discoveried, check whether we can demux the whole box.
        // For mdat, only the header is required.
        required = (box->is_mdat()? box->sz_header():box->sz());
        if (!buffer->require((int)required)) {
            continue;
        }
        
        if (err != srs_success) {
            srs_freep(box);
        } else {
            *ppbox = box;
        }
        
        break;
    }
    
    return err;
}

srs_error_t SrsMp4BoxReader::skip(SrsMp4Box* box, SrsSimpleStream* stream)
{
    srs_error_t err = srs_success;
    
    // For mdat, always skip the content.
    if (box->is_mdat()) {
        int offset = (int)(box->sz() - stream->length());
        if (offset < 0) {
            stream->erase(stream->length() + offset);
        } else {
            stream->erase(stream->length());
        }
        if (offset > 0 && (err = rsio->lseek(offset, SEEK_CUR, NULL)) != srs_success) {
            return srs_error_wrap(err, "io seek");
        }
    } else {
        // Remove the consumed bytes.
        stream->erase((int)box->sz());
    }
    
    return err;
}

SrsMp4Decoder::SrsMp4Decoder()
{
    rsio = NULL;
    brand = SrsMp4BoxBrandForbidden;
    stream = new SrsSimpleStream();
    vcodec = SrsVideoCodecIdForbidden;
    acodec = SrsAudioCodecIdForbidden;
    asc_written = avcc_written = false;
    sample_rate = SrsAudioSampleRateForbidden;
    sound_bits = SrsAudioSampleBitsForbidden;
    channels = SrsAudioChannelsForbidden;
    samples = new SrsMp4SampleManager();
    br = new SrsMp4BoxReader();
    current_index = 0;
    current_offset = 0;
}

SrsMp4Decoder::~SrsMp4Decoder()
{
    srs_freep(br);
    srs_freep(stream);
    srs_freep(samples);
}

srs_error_t SrsMp4Decoder::initialize(ISrsReadSeeker* rs)
{
    srs_error_t err = srs_success;
    
    srs_assert(rs);
    rsio = rs;
    
    if ((err = br->initialize(rs)) != srs_success) {
        return srs_error_wrap(err, "init box reader");
    }
    
    // For mdat before moov, we must reset the offset to the mdat.
    off_t offset = -1;
    
    while (true) {
        SrsMp4Box* box = NULL;
        
        if ((err = load_next_box(&box, 0)) != srs_success) {
            return srs_error_wrap(err, "load box");
        }
        
        if (box->is_ftyp()) {
            SrsMp4FileTypeBox* ftyp = dynamic_cast<SrsMp4FileTypeBox*>(box);
            if ((err = parse_ftyp(ftyp)) != srs_success) {
                return srs_error_wrap(err, "parse ftyp");
            }
        } else if (box->is_mdat()) {
            off_t cur = 0;
            if ((err = rsio->lseek(0, SEEK_CUR, &cur)) != srs_success) {
                return srs_error_wrap(err, "io seek");
            }
            offset = off_t(cur - box->sz());
        } else if (box->is_moov()) {
            SrsMp4MovieBox* moov = dynamic_cast<SrsMp4MovieBox*>(box);
            if ((err = parse_moov(moov)) != srs_success) {
                return srs_error_wrap(err, "parse moov");
            }
            break;
        }
        
        srs_freep(box);
    }
    
    if (brand == SrsMp4BoxBrandForbidden) {
        return srs_error_new(ERROR_MP4_BOX_ILLEGAL_SCHEMA, "missing ftyp");
    }
    
    // Set the offset to the mdat.
    if (offset >= 0) {
        if ((err = rsio->lseek(offset, SEEK_SET, &current_offset)) != srs_success) {
            return srs_error_wrap(err, "seek to mdat");
        }
    }
    
    return err;
}

srs_error_t SrsMp4Decoder::read_sample(SrsMp4HandlerType* pht, uint16_t* pft, uint16_t* pct, uint32_t* pdts, uint32_t* ppts, uint8_t** psample, uint32_t* pnb_sample)
{
    srs_error_t err = srs_success;
    
    if (!avcc_written && !pavcc.empty()) {
        avcc_written = true;
        *pdts = *ppts = 0;
        *pht = SrsMp4HandlerTypeVIDE;
        
        uint32_t nb_sample = *pnb_sample = (uint32_t)pavcc.size();
        uint8_t* sample = *psample = new uint8_t[nb_sample];
        memcpy(sample, &pavcc[0], nb_sample);
        
        *pft = SrsVideoAvcFrameTypeKeyFrame;
        *pct = SrsVideoAvcFrameTraitSequenceHeader;
        
        return err;
    }
    
    if (!asc_written && !pasc.empty()) {
        asc_written = true;
        *pdts = *ppts = 0;
        *pht = SrsMp4HandlerTypeSOUN;
        
        uint32_t nb_sample = *pnb_sample = (uint32_t)pasc.size();
        uint8_t* sample = *psample = new uint8_t[nb_sample];
        memcpy(sample, &pasc[0], nb_sample);
        
        *pft = 0x00;
        *pct = SrsAudioAacFrameTraitSequenceHeader;
        
        return err;
    }
    
    SrsMp4Sample* ps = samples->at(current_index++);
    if (!ps) {
        return srs_error_new(ERROR_SYSTEM_FILE_EOF, "EOF");
    }
    
    if (ps->type == SrsFrameTypeVideo) {
        *pht = SrsMp4HandlerTypeVIDE;
        *pct = SrsVideoAvcFrameTraitNALU;
    } else {
        *pht = SrsMp4HandlerTypeSOUN;
        *pct = SrsAudioAacFrameTraitRawData;
    }
    *pdts = ps->dts_ms();
    *ppts = ps->pts_ms();
    *pft = ps->frame_type;
    
    // Read sample from io, for we never preload the samples(too large).
    if (ps->offset != current_offset) {
        if ((err = rsio->lseek(ps->offset, SEEK_SET, &current_offset)) != srs_success) {
            return srs_error_wrap(err, "seek to sample");
        }
    }
    
    uint32_t nb_sample = ps->nb_data;
    uint8_t* sample = new uint8_t[nb_sample];
    // TODO: FIXME: Use fully read.
    if ((err = rsio->read(sample, nb_sample, NULL)) != srs_success) {
        srs_freepa(sample);
        return srs_error_wrap(err, "read sample");
    }
    
    *psample = sample;
    *pnb_sample = nb_sample;
    current_offset += nb_sample;
    
    return err;
}

srs_error_t SrsMp4Decoder::parse_ftyp(SrsMp4FileTypeBox* ftyp)
{
    srs_error_t err = srs_success;
    
    // File Type Box (ftyp)
    bool legal_brand = false;
    static SrsMp4BoxBrand legal_brands[] = {
        SrsMp4BoxBrandISOM, SrsMp4BoxBrandISO2, SrsMp4BoxBrandAVC1, SrsMp4BoxBrandMP41,
        SrsMp4BoxBrandISO5
    };
    for (int i = 0; i < (int)(sizeof(legal_brands)/sizeof(SrsMp4BoxBrand)); i++) {
        if (ftyp->major_brand == legal_brands[i]) {
            legal_brand = true;
            break;
        }
    }
    if (!legal_brand) {
        return srs_error_new(ERROR_MP4_BOX_ILLEGAL_BRAND, "brand is illegal, brand=%d", ftyp->major_brand);
    }
    
    brand = ftyp->major_brand;
    
    return err;
}

srs_error_t SrsMp4Decoder::parse_moov(SrsMp4MovieBox* moov)
{
    srs_error_t err = srs_success;
    
    SrsMp4MovieHeaderBox* mvhd = moov->mvhd();
    if (!mvhd) {
        return srs_error_new(ERROR_MP4_ILLEGAL_MOOV, "missing mvhd");
    }
    
    SrsMp4TrackBox* vide = moov->video();
    SrsMp4TrackBox* soun = moov->audio();
    if (!vide && !soun) {
        return srs_error_new(ERROR_MP4_ILLEGAL_MOOV, "missing audio and video track");
    }
    
    SrsMp4AudioSampleEntry* mp4a = soun? soun->mp4a():NULL;
    if (mp4a) {
        uint32_t sr = mp4a->samplerate>>16;
        if (sr >= 44100) {
            sample_rate = SrsAudioSampleRate44100;
        } else if (sr >= 22050) {
            sample_rate = SrsAudioSampleRate22050;
        } else if (sr >= 11025) {
            sample_rate = SrsAudioSampleRate11025;
        } else {
            sample_rate = SrsAudioSampleRate5512;
        }
        
        if (mp4a->samplesize == 16) {
            sound_bits = SrsAudioSampleBits16bit;
        } else {
            sound_bits = SrsAudioSampleBits8bit;
        }
        
        if (mp4a->channelcount == 2) {
            channels = SrsAudioChannelsStereo;
        } else {
            channels = SrsAudioChannelsMono;
        }
    }
    
    SrsMp4AvccBox* avcc = vide? vide->avcc():NULL;
    SrsMp4DecoderSpecificInfo* asc = soun? soun->asc():NULL;
    if (vide && !avcc) {
        return srs_error_new(ERROR_MP4_ILLEGAL_MOOV, "missing video sequence header");
    }
    if (soun && !asc) {
        return srs_error_new(ERROR_MP4_ILLEGAL_MOOV, "missing audio sequence header");
    }
    
    vcodec = vide?vide->vide_codec():SrsVideoCodecIdForbidden;
    acodec = soun?soun->soun_codec():SrsAudioCodecIdForbidden;
    
    if (avcc && !avcc->avc_config.empty()) {
        pavcc = avcc->avc_config;
    }
    if (asc && !asc->asc.empty()) {
        pasc = asc->asc;
    }
    
    // Build the samples structure from moov.
    if ((err = samples->load(moov)) != srs_success) {
        return srs_error_wrap(err, "load samples");
    }
    
    stringstream ss;
    ss << "dur=" << mvhd->duration() << "ms";
    // video codec.
    ss << ", vide=" << moov->nb_vide_tracks() << "("
        << srs_video_codec_id2str(vcodec) << "," << pavcc.size() << "BSH"
        << ")";
    // audio codec.
    ss << ", soun=" << moov->nb_soun_tracks() << "("
        << srs_audio_codec_id2str(acodec) << "," << pasc.size() << "BSH"
        << "," << srs_audio_channels2str(channels)
        << "," << srs_audio_sample_bits2str(sound_bits)
        << "," << srs_audio_sample_rate2str(sample_rate)
        << ")";
    
    srs_trace("MP4 moov %s", ss.str().c_str());
    
    return err;
}

srs_error_t SrsMp4Decoder::load_next_box(SrsMp4Box** ppbox, uint32_t required_box_type)
{
    srs_error_t err = srs_success;
    
    while (true) {
        SrsMp4Box* box = NULL;
        if ((err = do_load_next_box(&box, required_box_type)) != srs_success) {
            srs_freep(box);
            return srs_error_wrap(err, "load box");
        }
        
        if (!required_box_type || (uint32_t)box->type == required_box_type) {
            *ppbox = box;
            break;
        }
        srs_freep(box);
    }
    
    return err;
}

srs_error_t SrsMp4Decoder::do_load_next_box(SrsMp4Box** ppbox, uint32_t required_box_type)
{
    srs_error_t err = srs_success;
    
    while (true) {
        SrsMp4Box* box = NULL;
        
        if ((err = br->read(stream, &box)) != srs_success) {
            return srs_error_wrap(err, "read box");
        }
        
        SrsBuffer* buffer = new SrsBuffer(stream->bytes(), stream->length());
        SrsAutoFree(SrsBuffer, buffer);
        
        // Decode the box:
        // 1. Any box, when no box type is required.
        // 2. Matched box, when box type match the required type.
        // 3. Mdat box, always decode the mdat because we only decode the header of it.
        if (!required_box_type || (uint32_t)box->type == required_box_type || box->is_mdat()) {
            err = box->decode(buffer);
        }
        
        // Skip the box from stream, move stream to next box.
        // For mdat box, skip the content in stream or underylayer reader.
        // For other boxes, skip it from stream because we already decoded it or ignore it.
        if (err == srs_success) {
            err = br->skip(box, stream);
        }
        
        if (err != srs_success) {
            srs_freep(box);
            err = srs_error_wrap(err, "decode box");
        } else {
            *ppbox = box;
        }
        
        break;
    }
    
    return err;
}

SrsMp4Encoder::SrsMp4Encoder()
{
    wsio = NULL;
    mdat_bytes = 0;
    mdat_offset = 0;
    nb_audios = nb_videos = 0;
    samples = new SrsMp4SampleManager();
    aduration = vduration = 0;
    width = height = 0;
    
    acodec = SrsAudioCodecIdForbidden;
    sample_rate = SrsAudioSampleRateForbidden;
    sound_bits = SrsAudioSampleBitsForbidden;
    channels = SrsAudioChannelsForbidden;
    vcodec = SrsVideoCodecIdForbidden;
}

SrsMp4Encoder::~SrsMp4Encoder()
{
    srs_freep(samples);
}

srs_error_t SrsMp4Encoder::initialize(ISrsWriteSeeker* ws)
{
    srs_error_t err = srs_success;
    
    wsio = ws;
    
    // Write ftyp box.
    if (true) {
        SrsMp4FileTypeBox* ftyp = new SrsMp4FileTypeBox();
        SrsAutoFree(SrsMp4FileTypeBox, ftyp);
        
        ftyp->major_brand = SrsMp4BoxBrandISOM;
        ftyp->minor_version = 512;
        ftyp->set_compatible_brands(SrsMp4BoxBrandISOM, SrsMp4BoxBrandISO2, SrsMp4BoxBrandAVC1, SrsMp4BoxBrandMP41);
        
        int nb_data = ftyp->nb_bytes();
        std::vector<char> data(nb_data);
        
        SrsBuffer* buffer = new SrsBuffer(&data[0], nb_data);
        SrsAutoFree(SrsBuffer, buffer);
        
        if ((err = ftyp->encode(buffer)) != srs_success) {
            return srs_error_wrap(err, "encode ftyp");
        }
        
        // TODO: FIXME: Ensure write ok.
        if ((err = wsio->write(&data[0], nb_data, NULL)) != srs_success) {
            return srs_error_wrap(err, "write ftyp");
        }
    }

    // 8B reserved free box.
    if (true) {
        SrsMp4FreeSpaceBox* freeb = new SrsMp4FreeSpaceBox(SrsMp4BoxTypeFREE);
        SrsAutoFree(SrsMp4FreeSpaceBox, freeb);

        int nb_data = freeb->nb_bytes();
        std::vector<char> data(nb_data);

        SrsBuffer* buffer = new SrsBuffer(&data[0], nb_data);
        SrsAutoFree(SrsBuffer, buffer);

        if ((err = freeb->encode(buffer)) != srs_success) {
            return srs_error_wrap(err, "encode free box");
        }

        if ((err = wsio->write(&data[0], nb_data, NULL)) != srs_success) {
            return srs_error_wrap(err, "write free box");
        }
    }
    
    // Write mdat box.
    if (true) {
        // Write empty mdat box,
        // its payload will be writen by samples,
        // and we will update its header(size) when flush.
        SrsMp4MediaDataBox* mdat = new SrsMp4MediaDataBox();
        SrsAutoFree(SrsMp4MediaDataBox, mdat);
        
        // Update the mdat box from this offset.
        if ((err = wsio->lseek(0, SEEK_CUR, &mdat_offset)) != srs_success) {
            return srs_error_wrap(err, "seek to mdat");
        }
        
        int nb_data = mdat->sz_header();
        uint8_t* data = new uint8_t[nb_data];
        SrsAutoFreeA(uint8_t, data);
        
        SrsBuffer* buffer = new SrsBuffer((char*)data, nb_data);
        SrsAutoFree(SrsBuffer, buffer);
        
        if ((err = mdat->encode(buffer)) != srs_success) {
            return srs_error_wrap(err, "encode mdat");
        }
        
        // TODO: FIXME: Ensure all bytes are writen.
        if ((err = wsio->write(data, nb_data, NULL)) != srs_success) {
            return srs_error_wrap(err, "write mdat");
        }
        
        mdat_bytes = 0;
    }
    
    return err;
}

srs_error_t SrsMp4Encoder::write_sample(
    SrsFormat* format, SrsMp4HandlerType ht, uint16_t ft, uint16_t ct, uint32_t dts, uint32_t pts,
    uint8_t* sample, uint32_t nb_sample
) {
    srs_error_t err = srs_success;
    
    SrsMp4Sample* ps = new SrsMp4Sample();
    
    // For SPS/PPS or ASC, copy it to moov.
    bool vsh = (ht == SrsMp4HandlerTypeVIDE) && (ct == (uint16_t)SrsVideoAvcFrameTraitSequenceHeader);
    bool ash = (ht == SrsMp4HandlerTypeSOUN) && (ct == (uint16_t)SrsAudioAacFrameTraitSequenceHeader);
    if (vsh || ash) {
        err = copy_sequence_header(format, vsh, sample, nb_sample);
        srs_freep(ps);
        return err;
    }
    
    if (ht == SrsMp4HandlerTypeVIDE) {
        ps->type = SrsFrameTypeVideo;
        ps->frame_type = (SrsVideoAvcFrameType)ft;
        ps->index = nb_videos++;
        vduration = dts;
    } else if (ht == SrsMp4HandlerTypeSOUN) {
        ps->type = SrsFrameTypeAudio;
        ps->index = nb_audios++;
        aduration = dts;
    } else {
        srs_freep(ps);
        return err;
    }
    ps->tbn = 1000;
    ps->dts = dts;
    ps->pts = pts;
    
    if ((err = do_write_sample(ps, sample, nb_sample)) != srs_success) {
        srs_freep(ps);
        return srs_error_wrap(err, "write sample");
    }
    
    // Append to manager to build the moov.
    samples->append(ps);
    
    return err;
}

srs_error_t SrsMp4Encoder::flush()
{
    srs_error_t err = srs_success;
    
    if (!nb_audios && !nb_videos) {
        return srs_error_new(ERROR_MP4_ILLEGAL_MOOV, "Missing audio and video track");
    }
    
    // Write moov.
    if (true) {
        SrsMp4MovieBox* moov = new SrsMp4MovieBox();
        SrsAutoFree(SrsMp4MovieBox, moov);
        
        SrsMp4MovieHeaderBox* mvhd = new SrsMp4MovieHeaderBox();
        moov->set_mvhd(mvhd);
        
        mvhd->timescale = 1000; // Use tbn ms.
        mvhd->duration_in_tbn = srs_max(vduration, aduration);
        mvhd->next_track_ID = 1; // Starts from 1, increase when use it.
        
        if (nb_videos || !pavcc.empty()) {
            SrsMp4TrackBox* trak = new SrsMp4TrackBox();
            moov->add_trak(trak);
            
            SrsMp4TrackHeaderBox* tkhd = new SrsMp4TrackHeaderBox();
            trak->set_tkhd(tkhd);
            
            tkhd->track_ID = mvhd->next_track_ID++;
            tkhd->duration = vduration;
            tkhd->width = (width << 16);
            tkhd->height = (height << 16);
            
            SrsMp4MediaBox* mdia = new SrsMp4MediaBox();
            trak->set_mdia(mdia);
            
            SrsMp4MediaHeaderBox* mdhd = new SrsMp4MediaHeaderBox();
            mdia->set_mdhd(mdhd);
            
            mdhd->timescale = 1000;
            mdhd->duration = vduration;
            mdhd->set_language0('u');
            mdhd->set_language1('n');
            mdhd->set_language2('d');
            
            SrsMp4HandlerReferenceBox* hdlr = new SrsMp4HandlerReferenceBox();
            mdia->set_hdlr(hdlr);
            
            hdlr->handler_type = SrsMp4HandlerTypeVIDE;
            hdlr->name = "VideoHandler";
            
            SrsMp4MediaInformationBox* minf = new SrsMp4MediaInformationBox();
            mdia->set_minf(minf);
            
            SrsMp4VideoMeidaHeaderBox* vmhd = new SrsMp4VideoMeidaHeaderBox();
            minf->set_vmhd(vmhd);
            
            SrsMp4DataInformationBox* dinf = new SrsMp4DataInformationBox();
            minf->set_dinf(dinf);
            
            SrsMp4DataReferenceBox* dref = new SrsMp4DataReferenceBox();
            dinf->set_dref(dref);
            
            SrsMp4DataEntryBox* url = new SrsMp4DataEntryUrlBox();
            dref->append(url);
            
            SrsMp4SampleTableBox* stbl = new SrsMp4SampleTableBox();
            minf->set_stbl(stbl);
            
            SrsMp4SampleDescriptionBox* stsd = new SrsMp4SampleDescriptionBox();
            stbl->set_stsd(stsd);
            
            SrsMp4VisualSampleEntry* avc1 = new SrsMp4VisualSampleEntry();
            stsd->append(avc1);
            
            avc1->width = width;
            avc1->height = height;
            avc1->data_reference_index = 1;
            
            SrsMp4AvccBox* avcC = new SrsMp4AvccBox();
            avc1->set_avcC(avcC);
            
            avcC->avc_config = pavcc;
        }
        
        if (nb_audios || !pasc.empty()) {
            SrsMp4TrackBox* trak = new SrsMp4TrackBox();
            moov->add_trak(trak);
            
            SrsMp4TrackHeaderBox* tkhd = new SrsMp4TrackHeaderBox();
            tkhd->volume = 0x0100;
            trak->set_tkhd(tkhd);
            
            tkhd->track_ID = mvhd->next_track_ID++;
            tkhd->duration = aduration;
            
            SrsMp4MediaBox* mdia = new SrsMp4MediaBox();
            trak->set_mdia(mdia);
            
            SrsMp4MediaHeaderBox* mdhd = new SrsMp4MediaHeaderBox();
            mdia->set_mdhd(mdhd);
            
            mdhd->timescale = 1000;
            mdhd->duration = aduration;
            mdhd->set_language0('u');
            mdhd->set_language1('n');
            mdhd->set_language2('d');
            
            SrsMp4HandlerReferenceBox* hdlr = new SrsMp4HandlerReferenceBox();
            mdia->set_hdlr(hdlr);
            
            hdlr->handler_type = SrsMp4HandlerTypeSOUN;
            hdlr->name = "SoundHandler";
            
            SrsMp4MediaInformationBox* minf = new SrsMp4MediaInformationBox();
            mdia->set_minf(minf);
            
            SrsMp4SoundMeidaHeaderBox* smhd = new SrsMp4SoundMeidaHeaderBox();
            minf->set_smhd(smhd);
            
            SrsMp4DataInformationBox* dinf = new SrsMp4DataInformationBox();
            minf->set_dinf(dinf);
            
            SrsMp4DataReferenceBox* dref = new SrsMp4DataReferenceBox();
            dinf->set_dref(dref);
            
            SrsMp4DataEntryBox* url = new SrsMp4DataEntryUrlBox();
            dref->append(url);
            
            SrsMp4SampleTableBox* stbl = new SrsMp4SampleTableBox();
            minf->set_stbl(stbl);
            
            SrsMp4SampleDescriptionBox* stsd = new SrsMp4SampleDescriptionBox();
            stbl->set_stsd(stsd);
            
            SrsMp4AudioSampleEntry* mp4a = new SrsMp4AudioSampleEntry();
            mp4a->data_reference_index = 1;
            mp4a->samplerate = uint32_t(srs_flv_srates[sample_rate]) << 16;
            if (sound_bits == SrsAudioSampleBits16bit) {
                mp4a->samplesize = 16;
            } else {
                mp4a->samplesize = 8;
            }
            if (channels == SrsAudioChannelsStereo) {
                mp4a->channelcount = 2;
            } else {
                mp4a->channelcount = 1;
            }
            stsd->append(mp4a);
            
            SrsMp4EsdsBox* esds = new SrsMp4EsdsBox();
            mp4a->set_esds(esds);
            
            SrsMp4ES_Descriptor* es = esds->es;
            es->ES_ID = 0x02;
            
            SrsMp4DecoderConfigDescriptor& desc = es->decConfigDescr;
            desc.objectTypeIndication = SrsMp4ObjectTypeAac;
            desc.streamType = SrsMp4StreamTypeAudioStream;
            srs_freep(desc.decSpecificInfo);
            
            SrsMp4DecoderSpecificInfo* asc = new SrsMp4DecoderSpecificInfo();
            desc.decSpecificInfo = asc;
            asc->asc = pasc;;
        }
        
        if ((err = samples->write(moov)) != srs_success) {
            return srs_error_wrap(err, "write samples");
        }
        
        int nb_data = moov->nb_bytes();
        uint8_t* data = new uint8_t[nb_data];
        SrsAutoFreeA(uint8_t, data);
        
        SrsBuffer* buffer = new SrsBuffer((char*)data, nb_data);
        SrsAutoFree(SrsBuffer, buffer);
        
        if ((err = moov->encode(buffer)) != srs_success) {
            return srs_error_wrap(err, "encode moov");
        }
        
        // TODO: FIXME: Ensure all bytes are writen.
        if ((err = wsio->write(data, nb_data, NULL)) != srs_success) {
            return srs_error_wrap(err, "write moov");
        }
    }
    
    // Write mdat box.
    if (true) {
        // Update the mdat box header.
        if ((err = wsio->lseek(mdat_offset, SEEK_SET, NULL)) != srs_success) {
            return srs_error_wrap(err, "seek to mdat");
        }
        
        // Write mdat box with size of data,
        // its payload already writen by samples,
        // and we will update its header(size) when flush.
        SrsMp4MediaDataBox* mdat = new SrsMp4MediaDataBox();
        SrsAutoFree(SrsMp4MediaDataBox, mdat);
        
        int nb_data = mdat->sz_header();
        uint8_t* data = new uint8_t[nb_data];
        SrsAutoFreeA(uint8_t, data);
        
        SrsBuffer* buffer = new SrsBuffer((char*)data, nb_data);
        SrsAutoFree(SrsBuffer, buffer);
        
        // TODO: FIXME: Support 64bits size.
        mdat->nb_data = (int)mdat_bytes;
        if ((err = mdat->encode(buffer)) != srs_success) {
            return srs_error_wrap(err, "encode mdat");
        }
        
        // TODO: FIXME: Ensure all bytes are writen.
        if ((err = wsio->write(data, nb_data, NULL)) != srs_success) {
            return srs_error_wrap(err, "write mdat");
        }
    }
    
    return err;
}

srs_error_t SrsMp4Encoder::copy_sequence_header(SrsFormat* format, bool vsh, uint8_t* sample, uint32_t nb_sample)
{
    srs_error_t err = srs_success;
    
    if (vsh && !pavcc.empty()) {
        if (nb_sample == (uint32_t)pavcc.size() && srs_bytes_equals(sample, &pavcc[0], (int)pavcc.size())) {
            return err;
        }
        
        return srs_error_new(ERROR_MP4_AVCC_CHANGE, "doesn't support avcc change");
    }
    
    if (!vsh && !pasc.empty()) {
        if (nb_sample == (uint32_t)pasc.size() && srs_bytes_equals(sample, &pasc[0], (int)pasc.size())) {
            return err;
        }
        
        return srs_error_new(ERROR_MP4_ASC_CHANGE, "doesn't support asc change");
    }
    
    if (vsh) {
        pavcc = std::vector<char>(sample, sample + nb_sample);
        if (format && format->vcodec) {
            width = format->vcodec->width;
            height = format->vcodec->height;
        }
    }
    
    if (!vsh) {
        pasc = std::vector<char>(sample, sample + nb_sample);
    }
    
    return err;
}

srs_error_t SrsMp4Encoder::do_write_sample(SrsMp4Sample* ps, uint8_t* sample, uint32_t nb_sample)
{
    srs_error_t err = srs_success;
    
    ps->nb_data = nb_sample;
    // Never copy data, for we already writen to writer.
    ps->data = NULL;
    
    // Update the mdat box from this offset.
    if ((err = wsio->lseek(0, SEEK_CUR, &ps->offset)) != srs_success) {
        return srs_error_wrap(err, "seek to offset in mdat");
    }
    
    // TODO: FIXME: Ensure all bytes are writen.
    if ((err = wsio->write(sample, nb_sample, NULL)) != srs_success) {
        return srs_error_wrap(err, "write sample");
    }
    
    mdat_bytes += nb_sample;
    
    return err;
}

SrsMp4M2tsInitEncoder::SrsMp4M2tsInitEncoder()
{
    writer = NULL;
}

SrsMp4M2tsInitEncoder::~SrsMp4M2tsInitEncoder()
{
}

srs_error_t SrsMp4M2tsInitEncoder::initialize(ISrsWriter* w)
{
    writer = w;
    return srs_success;
}

srs_error_t SrsMp4M2tsInitEncoder::write(SrsFormat* format, bool video, int tid)
{
    srs_error_t err = srs_success;
    
    // Write ftyp box.
    if (true) {
        SrsMp4FileTypeBox* ftyp = new SrsMp4FileTypeBox();
        SrsAutoFree(SrsMp4FileTypeBox, ftyp);

        ftyp->major_brand = SrsMp4BoxBrandISO5;
        ftyp->minor_version = 512;
        ftyp->set_compatible_brands(SrsMp4BoxBrandISO6, SrsMp4BoxBrandMP41);

        if ((err = srs_mp4_write_box(writer, ftyp)) != srs_success) {
            return srs_error_wrap(err, "write ftyp");
        }
    }
    
    // Write moov.
    if (true) {
        SrsMp4MovieBox* moov = new SrsMp4MovieBox();
        SrsAutoFree(SrsMp4MovieBox, moov);

        SrsMp4MovieHeaderBox* mvhd = new SrsMp4MovieHeaderBox();
        moov->set_mvhd(mvhd);
        
        mvhd->timescale = 1000; // Use tbn ms.
        mvhd->duration_in_tbn = 0;
        mvhd->next_track_ID = tid;
        
        if (video) {
            SrsMp4TrackBox* trak = new SrsMp4TrackBox();
            moov->add_trak(trak);
            
            SrsMp4TrackHeaderBox* tkhd = new SrsMp4TrackHeaderBox();
            trak->set_tkhd(tkhd);
            
            tkhd->track_ID = mvhd->next_track_ID++;
            tkhd->duration = 0;
            tkhd->width = (format->vcodec->width << 16);
            tkhd->height = (format->vcodec->height << 16);
            
            SrsMp4MediaBox* mdia = new SrsMp4MediaBox();
            trak->set_mdia(mdia);
            
            SrsMp4MediaHeaderBox* mdhd = new SrsMp4MediaHeaderBox();
            mdia->set_mdhd(mdhd);
            
            mdhd->timescale = 1000;
            mdhd->duration = 0;
            mdhd->set_language0('u');
            mdhd->set_language1('n');
            mdhd->set_language2('d');
            
            SrsMp4HandlerReferenceBox* hdlr = new SrsMp4HandlerReferenceBox();
            mdia->set_hdlr(hdlr);
            
            hdlr->handler_type = SrsMp4HandlerTypeVIDE;
            hdlr->name = "VideoHandler";
            
            SrsMp4MediaInformationBox* minf = new SrsMp4MediaInformationBox();
            mdia->set_minf(minf);
            
            SrsMp4VideoMeidaHeaderBox* vmhd = new SrsMp4VideoMeidaHeaderBox();
            minf->set_vmhd(vmhd);
            
            SrsMp4DataInformationBox* dinf = new SrsMp4DataInformationBox();
            minf->set_dinf(dinf);
            
            SrsMp4DataReferenceBox* dref = new SrsMp4DataReferenceBox();
            dinf->set_dref(dref);
            
            SrsMp4DataEntryBox* url = new SrsMp4DataEntryUrlBox();
            dref->append(url);
            
            SrsMp4SampleTableBox* stbl = new SrsMp4SampleTableBox();
            minf->set_stbl(stbl);
            
            SrsMp4SampleDescriptionBox* stsd = new SrsMp4SampleDescriptionBox();
            stbl->set_stsd(stsd);
            
            SrsMp4VisualSampleEntry* avc1 = new SrsMp4VisualSampleEntry();
            stsd->append(avc1);
            
            avc1->width = format->vcodec->width;
            avc1->height = format->vcodec->height;
            avc1->data_reference_index = 1;
            
            SrsMp4AvccBox* avcC = new SrsMp4AvccBox();
            avc1->set_avcC(avcC);
            
            avcC->avc_config = format->vcodec->avc_extra_data;
            
            SrsMp4DecodingTime2SampleBox* stts = new SrsMp4DecodingTime2SampleBox();
            stbl->set_stts(stts);
            
            SrsMp4Sample2ChunkBox* stsc = new SrsMp4Sample2ChunkBox();
            stbl->set_stsc(stsc);
            
            SrsMp4SampleSizeBox* stsz = new SrsMp4SampleSizeBox();
            stbl->set_stsz(stsz);
            
            SrsMp4ChunkOffsetBox* stco = new SrsMp4ChunkOffsetBox();
            stbl->set_stco(stco);
            
            SrsMp4MovieExtendsBox* mvex = new SrsMp4MovieExtendsBox();
            moov->set_mvex(mvex);
            
            SrsMp4TrackExtendsBox* trex = new SrsMp4TrackExtendsBox();
            mvex->set_trex(trex);
            
            trex->track_ID = tid;
            trex->default_sample_description_index = 1;
        } else {
            SrsMp4TrackBox* trak = new SrsMp4TrackBox();
            moov->add_trak(trak);
            
            SrsMp4TrackHeaderBox* tkhd = new SrsMp4TrackHeaderBox();
            tkhd->volume = 0x0100;
            trak->set_tkhd(tkhd);
            
            tkhd->track_ID = mvhd->next_track_ID++;
            tkhd->duration = 0;
            
            SrsMp4MediaBox* mdia = new SrsMp4MediaBox();
            trak->set_mdia(mdia);
            
            SrsMp4MediaHeaderBox* mdhd = new SrsMp4MediaHeaderBox();
            mdia->set_mdhd(mdhd);
            
            mdhd->timescale = 1000;
            mdhd->duration = 0;
            mdhd->set_language0('u');
            mdhd->set_language1('n');
            mdhd->set_language2('d');
            
            SrsMp4HandlerReferenceBox* hdlr = new SrsMp4HandlerReferenceBox();
            mdia->set_hdlr(hdlr);
            
            hdlr->handler_type = SrsMp4HandlerTypeSOUN;
            hdlr->name = "SoundHandler";
            
            SrsMp4MediaInformationBox* minf = new SrsMp4MediaInformationBox();
            mdia->set_minf(minf);
            
            SrsMp4SoundMeidaHeaderBox* smhd = new SrsMp4SoundMeidaHeaderBox();
            minf->set_smhd(smhd);
            
            SrsMp4DataInformationBox* dinf = new SrsMp4DataInformationBox();
            minf->set_dinf(dinf);
            
            SrsMp4DataReferenceBox* dref = new SrsMp4DataReferenceBox();
            dinf->set_dref(dref);
            
            SrsMp4DataEntryBox* url = new SrsMp4DataEntryUrlBox();
            dref->append(url);
            
            SrsMp4SampleTableBox* stbl = new SrsMp4SampleTableBox();
            minf->set_stbl(stbl);
            
            SrsMp4SampleDescriptionBox* stsd = new SrsMp4SampleDescriptionBox();
            stbl->set_stsd(stsd);
            
            SrsMp4AudioSampleEntry* mp4a = new SrsMp4AudioSampleEntry();
            mp4a->data_reference_index = 1;
            mp4a->samplerate = uint32_t(srs_flv_srates[format->acodec->sound_rate]) << 16;
            if (format->acodec->sound_size == SrsAudioSampleBits16bit) {
                mp4a->samplesize = 16;
            } else {
                mp4a->samplesize = 8;
            }
            if (format->acodec->sound_type == SrsAudioChannelsStereo) {
                mp4a->channelcount = 2;
            } else {
                mp4a->channelcount = 1;
            }
            stsd->append(mp4a);
            
            SrsMp4EsdsBox* esds = new SrsMp4EsdsBox();
            mp4a->set_esds(esds);
            
            SrsMp4ES_Descriptor* es = esds->es;
            es->ES_ID = 0x02;
            
            SrsMp4DecoderConfigDescriptor& desc = es->decConfigDescr;
            desc.objectTypeIndication = SrsMp4ObjectTypeAac;
            desc.streamType = SrsMp4StreamTypeAudioStream;
            srs_freep(desc.decSpecificInfo);
            
            SrsMp4DecoderSpecificInfo* asc = new SrsMp4DecoderSpecificInfo();
            desc.decSpecificInfo = asc;
            asc->asc = format->acodec->aac_extra_data;
            
            SrsMp4DecodingTime2SampleBox* stts = new SrsMp4DecodingTime2SampleBox();
            stbl->set_stts(stts);
            
            SrsMp4Sample2ChunkBox* stsc = new SrsMp4Sample2ChunkBox();
            stbl->set_stsc(stsc);
            
            SrsMp4SampleSizeBox* stsz = new SrsMp4SampleSizeBox();
            stbl->set_stsz(stsz);
            
            SrsMp4ChunkOffsetBox* stco = new SrsMp4ChunkOffsetBox();
            stbl->set_stco(stco);
            
            SrsMp4MovieExtendsBox* mvex = new SrsMp4MovieExtendsBox();
            moov->set_mvex(mvex);
            
            SrsMp4TrackExtendsBox* trex = new SrsMp4TrackExtendsBox();
            mvex->set_trex(trex);
            
            trex->track_ID = tid;
            trex->default_sample_description_index = 1;
        }

        if ((err = srs_mp4_write_box(writer, moov)) != srs_success) {
            return srs_error_wrap(err, "write moov");
        }
    }
    
    return err;
}

SrsMp4M2tsSegmentEncoder::SrsMp4M2tsSegmentEncoder()
{
    writer = NULL;
    nb_audios = nb_videos = 0;
    samples = new SrsMp4SampleManager();
    buffer = new SrsBuffer();
    sequence_number = 0;
    decode_basetime = 0;
    styp_bytes = 0;
    mdat_bytes = 0;
}

SrsMp4M2tsSegmentEncoder::~SrsMp4M2tsSegmentEncoder()
{
    srs_freep(samples);
    srs_freep(buffer);
}

srs_error_t SrsMp4M2tsSegmentEncoder::initialize(ISrsWriter* w, uint32_t sequence, srs_utime_t basetime, uint32_t tid)
{
    srs_error_t err = srs_success;
    
    writer = w;
    track_id = tid;
    sequence_number = sequence;
    decode_basetime = basetime;
    
    // Write styp box.
    if (true) {
        SrsMp4SegmentTypeBox* styp = new SrsMp4SegmentTypeBox();
        SrsAutoFree(SrsMp4SegmentTypeBox, styp);
        
        styp->major_brand = SrsMp4BoxBrandMSDH;
        styp->minor_version = 0;
        styp->set_compatible_brands(SrsMp4BoxBrandMSDH, SrsMp4BoxBrandMSIX);

        // Used for sidx to calcalute the referenced size.
        styp_bytes = styp->nb_bytes();

        if ((err = srs_mp4_write_box(writer, styp)) != srs_success) {
            return srs_error_wrap(err, "write styp");
        }
    }

    return err;
}

srs_error_t SrsMp4M2tsSegmentEncoder::write_sample(SrsMp4HandlerType ht,
    uint16_t ft, uint32_t dts, uint32_t pts, uint8_t* sample, uint32_t nb_sample
) {
    srs_error_t err = srs_success;
    
    SrsMp4Sample* ps = new SrsMp4Sample();
    
    if (ht == SrsMp4HandlerTypeVIDE) {
        ps->type = SrsFrameTypeVideo;
        ps->frame_type = (SrsVideoAvcFrameType)ft;
        ps->index = nb_videos++;
    } else if (ht == SrsMp4HandlerTypeSOUN) {
        ps->type = SrsFrameTypeAudio;
        ps->index = nb_audios++;
    } else {
        srs_freep(ps);
        return err;
    }
    
    ps->tbn = 1000;
    ps->dts = dts;
    ps->pts = pts;

    // We should copy the sample data, which is shared ptr from video/audio message.
    // Furthermore, we do free the data when freeing the sample.
    ps->data = new uint8_t[nb_sample];
    memcpy(ps->data, sample, nb_sample);
    ps->nb_data = nb_sample;
    
    // Append to manager to build the moof.
    samples->append(ps);
    
    mdat_bytes += nb_sample;
    
    return err;
}

srs_error_t SrsMp4M2tsSegmentEncoder::flush(uint64_t& dts)
{
    srs_error_t err = srs_success;
    
    if (!nb_audios && !nb_videos) {
        return srs_error_new(ERROR_MP4_ILLEGAL_MOOF, "Missing audio and video track");
    }

    // Although the sidx is not required to start play DASH, but it's required for AV sync.
    SrsMp4SegmentIndexBox* sidx = new SrsMp4SegmentIndexBox();
    SrsAutoFree(SrsMp4SegmentIndexBox, sidx);
    if (true) {
        sidx->version = 1;
        sidx->reference_id = 1;
        sidx->timescale = 1000;
        sidx->earliest_presentation_time = uint64_t(decode_basetime / sidx->timescale);

        uint64_t duration = 0;
        if (samples && !samples->samples.empty()) {
            SrsMp4Sample* first = samples->samples[0];
            SrsMp4Sample* last = samples->samples[samples->samples.size() - 1];
            duration = srs_max(0, last->dts - first->dts);
        }

        SrsMp4SegmentIndexEntry entry;
        memset(&entry, 0, sizeof(entry));
        entry.subsegment_duration = duration;
        entry.starts_with_SAP = 1;
        sidx->entries.push_back(entry);
    }

    // Create a mdat box.
    // its payload will be writen by samples,
    // and we will update its header(size) when flush.
    SrsMp4MediaDataBox* mdat = new SrsMp4MediaDataBox();
    SrsAutoFree(SrsMp4MediaDataBox, mdat);

    // Write moof.
    if (true) {
        SrsMp4MovieFragmentBox* moof = new SrsMp4MovieFragmentBox();
        SrsAutoFree(SrsMp4MovieFragmentBox, moof);
        
        SrsMp4MovieFragmentHeaderBox* mfhd = new SrsMp4MovieFragmentHeaderBox();
        moof->set_mfhd(mfhd);
        
        mfhd->sequence_number = sequence_number;
        
        SrsMp4TrackFragmentBox* traf = new SrsMp4TrackFragmentBox();
        moof->set_traf(traf);
        
        SrsMp4TrackFragmentHeaderBox* tfhd = new SrsMp4TrackFragmentHeaderBox();
        traf->set_tfhd(tfhd);
        
        tfhd->track_id = track_id;
        tfhd->flags = SrsMp4TfhdFlagsDefaultBaseIsMoof;
        
        SrsMp4TrackFragmentDecodeTimeBox* tfdt = new SrsMp4TrackFragmentDecodeTimeBox();
        traf->set_tfdt(tfdt);
        
        tfdt->version = 1;
        tfdt->base_media_decode_time = srsu2ms(decode_basetime);
        
        SrsMp4TrackFragmentRunBox* trun = new SrsMp4TrackFragmentRunBox();
        traf->set_trun(trun);
        
        if ((err = samples->write(moof, dts)) != srs_success) {
            return srs_error_wrap(err, "write samples");
        }
        
        // @remark Remember the data_offset of turn is size(moof)+header(mdat), not including styp or sidx.
        int moof_bytes = moof->nb_bytes();
        trun->data_offset = (int32_t)(moof_bytes + mdat->sz_header());
        mdat->nb_data = (int)mdat_bytes;

        // Update the size of sidx.
        SrsMp4SegmentIndexEntry* entry = &sidx->entries[0];
        entry->referenced_size = moof_bytes + mdat->nb_bytes();
        if ((err = srs_mp4_write_box(writer, sidx)) != srs_success) {
            return srs_error_wrap(err, "write sidx");
        }

        if ((err = srs_mp4_write_box(writer, moof)) != srs_success) {
            return srs_error_wrap(err, "write moof");
        }
    }
    
    // Write mdat.
    if (true) {
        int nb_data = mdat->sz_header();
        uint8_t* data = new uint8_t[nb_data];
        SrsAutoFreeA(uint8_t, data);
        
        SrsBuffer* buffer = new SrsBuffer((char*)data, nb_data);
        SrsAutoFree(SrsBuffer, buffer);
        
        if ((err = mdat->encode(buffer)) != srs_success) {
            return srs_error_wrap(err, "encode mdat");
        }
        
        // TODO: FIXME: Ensure all bytes are writen.
        if ((err = writer->write(data, nb_data, NULL)) != srs_success) {
            return srs_error_wrap(err, "write mdat");
        }
        
        vector<SrsMp4Sample*>::iterator it;
        for (it = samples->samples.begin(); it != samples->samples.end(); ++it) {
            SrsMp4Sample* sample = *it;
            
            // TODO: FIXME: Ensure all bytes are writen.
            if ((err = writer->write(sample->data, sample->nb_data, NULL)) != srs_success) {
                return srs_error_wrap(err, "write sample");
            }
        }
    }

    return err;
}

