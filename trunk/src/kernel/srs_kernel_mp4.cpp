/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2017 OSSRS(winlin)
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

stringstream& srs_padding(stringstream& ss, SrsMp4DumpContext dc, int tab = 4)
{
    for (int i = 0; i < dc.level; i++) {
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

#define SrsSummaryCount 8

template<typename T>
stringstream& srs_dumps_array(std::vector<T>&arr, stringstream& ss, SrsMp4DumpContext dc,
    void (*pfn)(T&, stringstream&, SrsMp4DumpContext),
    void (*delimiter)(stringstream&,SrsMp4DumpContext))
{
    int limit = arr.size();
    if (dc.summary) {
        limit = srs_min(SrsSummaryCount, limit);
    }
    
    for (size_t i = 0; i < limit; i++) {
        T& elem = arr[i];
        
        pfn(elem, ss, dc);
        
        if (i < limit - 1) {
            delimiter(ss, dc);
        }
    }
    return ss;
}

template<typename T>
stringstream& srs_dumps_array(T* arr, int size, stringstream& ss, SrsMp4DumpContext dc,
    void (*pfn)(T&, stringstream&, SrsMp4DumpContext),
    void (*delimiter)(stringstream&, SrsMp4DumpContext))
{
    int limit = size;
    if (dc.summary) {
        limit = srs_min(SrsSummaryCount, limit);
    }
    
    for (size_t i = 0; i < limit; i++) {
        T& elem = arr[i];
        
        pfn(elem, ss, dc);
        
        if (i < limit - 1) {
            delimiter(ss, dc);
        }
    }
    return ss;
}

void srs_delimiter_inline(stringstream& ss, SrsMp4DumpContext dc)
{
    ss << ",";
}

void srs_delimiter_inlinespace(stringstream& ss, SrsMp4DumpContext dc)
{
    ss << ", ";
}

void srs_delimiter_newline(stringstream& ss, SrsMp4DumpContext dc)
{
    ss << endl;
    srs_padding(ss, dc);
}

template<typename T>
void srs_pfn_box(T& elem, stringstream& ss, SrsMp4DumpContext dc)
{
    elem.dumps(ss, dc);
}

template<typename T>
void srs_pfn_detail(T& elem, stringstream& ss, SrsMp4DumpContext dc)
{
    elem.dumps_detail(ss, dc);
}

template<typename T>
void srs_pfn_pbox(T*& elem, stringstream& ss, SrsMp4DumpContext dc)
{
    elem->dumps(ss, dc);
}

template<typename T>
void srs_pfn_pdetail(T*& elem, stringstream& ss, SrsMp4DumpContext dc)
{
    elem->dumps_detail(ss, dc);
}

template<typename T>
void srs_pfn_types(T& elem, stringstream& ss, SrsMp4DumpContext dc)
{
    srs_print_mp4_type(ss, (uint32_t)elem);
}

template<typename T>
void srs_pfn_hex(T& elem, stringstream& ss, SrsMp4DumpContext dc)
{
    ss << "0x" << std::setw(2) << std::setfill('0') << std::hex << (uint32_t)(uint8_t)elem << std::dec;
}

template<typename T>
void srs_pfn_elems(T& elem, stringstream& ss, SrsMp4DumpContext dc)
{
    ss << elem;
}

stringstream& srs_print_bytes(stringstream& ss, const char* p, int size, SrsMp4DumpContext dc, int line = SrsSummaryCount, int max = -1)
{
    if (max == -1) {
        max = size;
    }
    
    for (int i = 0; i < max; i++) {
        ss << "0x" << std::setw(2) << std::setfill('0') << std::hex << (uint32_t)(uint8_t)p[i] << std::dec;
         if (i < max -1) {
             ss << ", ";
             if (((i+1)%line) == 0) {
                 ss << endl;
                 srs_padding(ss, dc);
             }
        }
    }
    return ss;
}

int srs_mp4_string_length(const string& v)
{
    return (int)v.length()+1;
}

void srs_mp4_string_write(SrsBuffer* buf, const string& v)
{
    // Nothing for empty string.
    if (v.empty()) {
        return;
    }
    
    buf->write_bytes((char*)v.data(), (int)v.length());
    buf->write_1bytes(0x00);
}

int srs_mp4_string_read(SrsBuffer* buf, string& v, int left)
{
    int ret = ERROR_SUCCESS;
    
    if (left == 0) {
        return ret;
    }
    
    char* start = buf->data() + buf->pos();
    size_t len = strnlen(start, left);
    
    if ((int)len == left) {
        ret = ERROR_MP4_BOX_STRING;
        srs_error("MP4 string corrupt, left=%d. ret=%d", left, ret);
        return ret;
    }
    
    v.append(start, len);
    buf->skip((int)len + 1);
    
    return ret;
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

int SrsMp4Box::left_space(SrsBuffer* buf)
{
    return (int)sz() - (buf->pos() - start_pos);
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
        } else {
            ++it;
        }
    }
    
    return nb_removed;
}

stringstream& SrsMp4Box::dumps(stringstream& ss, SrsMp4DumpContext dc)
{
    srs_padding(ss, dc);
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

int SrsMp4Box::discovery(SrsBuffer* buf, SrsMp4Box** ppbox)
{
    *ppbox = NULL;
    
    int ret = ERROR_SUCCESS;
    
    if (!buf->require(8)) {
        ret = ERROR_MP4_BOX_REQUIRE_SPACE;
        srs_error("MP4 discovery require 8 bytes space. ret=%d", ret);
        return ret;
    }
    
    // Discovery the size and type.
    uint64_t largesize = 0;
    uint32_t smallsize = (uint32_t)buf->read_4bytes();
    SrsMp4BoxType type = (SrsMp4BoxType)buf->read_4bytes();
    if (smallsize == SRS_MP4_USE_LARGE_SIZE) {
        if (!buf->require(8)) {
            ret = ERROR_MP4_BOX_REQUIRE_SPACE;
            srs_error("MP4 discovery require 16 bytes space. ret=%d", ret);
            return ret;
        }
        largesize = (uint64_t)buf->read_8bytes();
        buf->skip(-8);
    }
    buf->skip(-8);
    
    // Only support 31bits size.
    if (largesize > 0x7fffffff) {
        ret = ERROR_MP4_BOX_OVERFLOW;
        srs_error("MP4 discovery overflow 31bits, size=%" PRId64 ". ret=%d", largesize, ret);
        return ret;
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
        // Skip some unknown boxes.
        case SrsMp4BoxTypeFREE: case SrsMp4BoxTypeSKIP: case SrsMp4BoxTypePASP:
            box = new SrsMp4FreeSpaceBox(type); break;
        default:
            ret = ERROR_MP4_BOX_ILLEGAL_TYPE;
            srs_error("MP4 illegal box type=%d. ret=%d", type, ret);
            break;
    }
    
    if (box) {
        box->smallsize = smallsize;
        box->largesize = largesize;
        box->type = type;
        *ppbox = box;
    }
    
    return ret;
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

int SrsMp4Box::encode(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    uint64_t size = nb_bytes();
    if (size > 0xffffffff) {
        largesize = size;
    } else {
        smallsize = (uint32_t)size;
    }
    
    start_pos = buf->pos();
    
    if ((ret = encode_header(buf)) != ERROR_SUCCESS) {
        srs_error("MP4 encode box header failed. ret=%d", ret);
        return ret;
    }
    
    if ((ret = encode_boxes(buf)) != ERROR_SUCCESS) {
        srs_error("MP4 encode contained boxes failed. ret=%d", ret);
        return ret;
    }
    
    return ret;
}

int SrsMp4Box::decode(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    start_pos = buf->pos();
    
    if ((ret = decode_header(buf)) != ERROR_SUCCESS) {
        srs_error("MP4 decode box header failed. ret=%d", ret);
        return ret;
    }
    
    if ((ret = decode_boxes(buf)) != ERROR_SUCCESS) {
        srs_error("MP4 decode contained boxes failed. ret=%d", ret);
        return ret;
    }
    
    return ret;
}

int SrsMp4Box::encode_boxes(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    vector<SrsMp4Box*>::iterator it;
    for (it = boxes.begin(); it != boxes.end(); ++it) {
        SrsMp4Box* box = *it;
        if ((ret = box->encode(buf)) != ERROR_SUCCESS) {
            srs_error("MP4 encode contained box failed. ret=%d", ret);
            return ret;
        }
    }
    
    return ret;
}

int SrsMp4Box::decode_boxes(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    int left = left_space(buf);
    while (left > 0) {
        SrsMp4Box* box = NULL;
        if ((ret = discovery(buf, &box)) != ERROR_SUCCESS) {
            srs_error("MP4 discovery contained box failed. ret=%d", ret);
            return ret;
        }
        
        srs_assert(box);
        if ((ret = box->decode(buf)) != ERROR_SUCCESS) {
            srs_freep(box);
            srs_error("MP4 decode contained box failed. ret=%d", ret);
            return ret;
        }
        
        boxes.push_back(box);
        left -= box->sz();
    }
    
    return ret;
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

int SrsMp4Box::encode_header(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    // Only support 31bits size.
    if (sz() > 0x7fffffff) {
        ret = ERROR_MP4_BOX_OVERFLOW;
        srs_error("MP4 box size overflow 31bits, size=%" PRId64 ". ret=%d", sz(), ret);
        return ret;
    }
    
    int size = SrsMp4Box::nb_header();
    if (!buf->require(size)) {
        ret = ERROR_MP4_BOX_REQUIRE_SPACE;
        srs_error("MP4 box require %d bytes space. ret=%d", size, ret);
        return ret;
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
        ret = ERROR_MP4_BOX_REQUIRE_SPACE;
        srs_error("MP4 box require %d bytes space. ret=%d", lrsz, ret);
        return ret;
    }
    
    return ret;
}

int SrsMp4Box::decode_header(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    if (!buf->require(8)) {
        ret = ERROR_MP4_BOX_REQUIRE_SPACE;
        srs_error("MP4 box require 8 bytes space. ret=%d", ret);
        return ret;
    }
    smallsize = (uint32_t)buf->read_4bytes();
    type = (SrsMp4BoxType)buf->read_4bytes();
    
    if (smallsize == SRS_MP4_EOF_SIZE) {
        srs_trace("MP4 box EOF.");
        return ret;
    }
    
    if (smallsize == SRS_MP4_USE_LARGE_SIZE) {
        if (!buf->require(8)) {
            ret = ERROR_MP4_BOX_REQUIRE_SPACE;
            srs_error("MP4 box require 8 bytes space. ret=%d", ret);
            return ret;
        }
        largesize = (uint64_t)buf->read_8bytes();
    }
    
    // Only support 31bits size.
    if (sz() > 0x7fffffff) {
        ret = ERROR_MP4_BOX_OVERFLOW;
        srs_error("MP4 box size overflow 31bits, size=%" PRId64 ". ret=%d", sz(), ret);
        return ret;
    }
    
    if (type == SrsMp4BoxTypeUUID) {
        if (!buf->require(16)) {
            ret = ERROR_MP4_BOX_REQUIRE_SPACE;
            srs_error("MP4 box requires 16 bytes space. ret=%d", ret);
            return ret;
        }
        usertype.resize(16);
        buf->read_bytes(&usertype[0], 16);
    }
    
    // The left required size, determined by the default version(0).
    int lrsz = nb_header() - SrsMp4Box::nb_header();
    if (!buf->require(lrsz)) {
        ret = ERROR_MP4_BOX_REQUIRE_SPACE;
        srs_error("MP4 box requires %d bytes space. ret=%d", lrsz, ret);
        return ret;
    }
    
    return ret;
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

int SrsMp4FullBox::encode_header(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = SrsMp4Box::encode_header(buf)) != ERROR_SUCCESS) {
        return ret;
    }
    
    if (!buf->require(4)) {
        ret = ERROR_MP4_BOX_REQUIRE_SPACE;
        srs_error("MP4 full box requires 4 bytes space. ret=%d", ret);
        return ret;
    }
    
    buf->write_1bytes(version);
    buf->write_3bytes(flags);
    
    return ret;
}

int SrsMp4FullBox::decode_header(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = SrsMp4Box::decode_header(buf)) != ERROR_SUCCESS) {
        return ret;
    }
    
    if (!buf->require(4)) {
        ret = ERROR_MP4_BOX_REQUIRE_SPACE;
        srs_error("MP4 full box requires 4 bytes space. ret=%d", ret);
        return ret;
    }
    
    flags = (uint32_t)buf->read_4bytes();
    
    version = (uint8_t)((flags >> 24) & 0xff);
    flags &= 0x00ffffff;
    
    // The left required size, determined by the version.
    int lrsz = nb_header() - SrsMp4FullBox::nb_header();
    if (!buf->require(lrsz)) {
        ret = ERROR_MP4_BOX_REQUIRE_SPACE;
        srs_error("MP4 full box requires %d bytes space. ret=%d", lrsz, ret);
        return ret;
    }
    
    return ret;
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

int SrsMp4FileTypeBox::encode_header(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = SrsMp4Box::encode_header(buf)) != ERROR_SUCCESS) {
        return ret;
    }
    
    buf->write_4bytes(major_brand);
    buf->write_4bytes(minor_version);
    
    for (size_t i = 0; i < compatible_brands.size(); i++) {
        buf->write_4bytes(compatible_brands[i]);
    }
    
    return ret;
}

int SrsMp4FileTypeBox::decode_header(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = SrsMp4Box::decode_header(buf)) != ERROR_SUCCESS) {
        return ret;
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
    
    return ret;
}

stringstream& SrsMp4FileTypeBox::dumps_detail(stringstream& ss, SrsMp4DumpContext dc)
{
    SrsMp4Box::dumps_detail(ss, dc);
    
    ss << ", brands:";
    srs_print_mp4_type(ss, (uint32_t)major_brand);
    
    ss << "," << minor_version;
    
    if (!compatible_brands.empty()) {
        ss << "(";
        srs_dumps_array(compatible_brands, ss, dc, srs_pfn_types, srs_delimiter_inline);
        ss << ")";
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

int SrsMp4MediaDataBox::encode(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = SrsMp4Box::encode(buf)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int SrsMp4MediaDataBox::decode(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = SrsMp4Box::decode(buf)) != ERROR_SUCCESS) {
        return ret;
    }
    
    nb_data = (int)(sz() - nb_header());
    
    // Because the 
    
    return ret;
}

int SrsMp4MediaDataBox::encode_boxes(SrsBuffer* buf)
{
    return ERROR_SUCCESS;
}

int SrsMp4MediaDataBox::decode_boxes(SrsBuffer* buf)
{
    return ERROR_SUCCESS;
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

int SrsMp4FreeSpaceBox::encode_header(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = SrsMp4Box::encode_header(buf)) != ERROR_SUCCESS) {
        return ret;
    }
    
    if (!data.empty()) {
        buf->write_bytes(&data[0], (int)data.size());
    }
    
    return ret;
}

int SrsMp4FreeSpaceBox::decode_header(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = SrsMp4Box::decode_header(buf)) != ERROR_SUCCESS) {
        return ret;
    }
    
    int left = left_space(buf);
    if (left) {
        data.resize(left);
        buf->read_bytes(&data[0], left);
    }
    
    return ret;
}

stringstream& SrsMp4FreeSpaceBox::dumps_detail(stringstream& ss, SrsMp4DumpContext dc)
{
    SrsMp4Box::dumps_detail(ss, dc);
    
    ss << ", free " << data.size() << "B";
    
    if (!data.empty()) {
        ss << endl;
        srs_padding(ss, dc.indent());
        srs_dumps_array(&data[0], (int)data.size(), ss, dc.indent(), srs_pfn_hex, srs_delimiter_inlinespace);
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

int SrsMp4MovieBox::encode_header(SrsBuffer* buf)
{
    return SrsMp4Box::encode_header(buf);
}

int SrsMp4MovieBox::decode_header(SrsBuffer* buf)
{
    return SrsMp4Box::decode_header(buf);
}

SrsMp4MovieHeaderBox::SrsMp4MovieHeaderBox()
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

int SrsMp4MovieHeaderBox::encode_header(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = SrsMp4FullBox::encode_header(buf)) != ERROR_SUCCESS) {
        return ret;
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
    
    return ret;
}

int SrsMp4MovieHeaderBox::decode_header(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = SrsMp4FullBox::decode_header(buf)) != ERROR_SUCCESS) {
        return ret;
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
    
    return ret;
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

int SrsMp4TrackExtendsBox::encode_header(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = SrsMp4FullBox::encode_header(buf)) != ERROR_SUCCESS) {
        return ret;
    }
    
    buf->write_4bytes(track_ID);
    buf->write_4bytes(default_sample_description_index);
    buf->write_4bytes(default_sample_duration);
    buf->write_4bytes(default_sample_size);
    buf->write_4bytes(default_sample_flags);
    
    return ret;
}

int SrsMp4TrackExtendsBox::decode_header(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = SrsMp4FullBox::decode_header(buf)) != ERROR_SUCCESS) {
        return ret;
    }
    
    track_ID = buf->read_4bytes();
    default_sample_description_index = buf->read_4bytes();
    default_sample_duration = buf->read_4bytes();
    default_sample_size = buf->read_4bytes();
    default_sample_flags = buf->read_4bytes();
    
    return ret;
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

SrsMp4TrackHeaderBox::SrsMp4TrackHeaderBox()
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

int SrsMp4TrackHeaderBox::encode_header(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = SrsMp4FullBox::encode_header(buf)) != ERROR_SUCCESS) {
        return ret;
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
    
    return ret;
}

int SrsMp4TrackHeaderBox::decode_header(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = SrsMp4FullBox::decode_header(buf)) != ERROR_SUCCESS) {
        return ret;
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
    
    return ret;
}

stringstream& SrsMp4TrackHeaderBox::dumps_detail(stringstream& ss, SrsMp4DumpContext dc)
{
    SrsMp4FullBox::dumps_detail(ss, dc);
    
    ss << ", track #" << track_ID << ", " << duration << "TBN";
    
    if (volume) {
        ss << ", volume=" << uint32_t(volume>>8) << "." << uint32_t(volume&0xFF);
    }
    
    if (width || height) {
        ss << ", size=" << uint16_t(width>>16) << "x" << uint16_t(height>>16);
    }
    
    return ss;
}

SrsMp4EditBox::SrsMp4EditBox()
{
    type = SrsMp4BoxTypeEDTS;
}

SrsMp4EditBox::~SrsMp4EditBox()
{
}

SrsMp4ElstEntry::SrsMp4ElstEntry()
{
    media_rate_fraction = 0;
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

int SrsMp4EditListBox::encode_header(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = SrsMp4FullBox::encode_header(buf)) != ERROR_SUCCESS) {
        return ret;
    }
    
    buf->write_4bytes((int)entries.size());
    for (size_t i = 0; i < entries.size(); i++) {
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
    
    return ret;
}

int SrsMp4EditListBox::decode_header(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = SrsMp4FullBox::decode_header(buf)) != ERROR_SUCCESS) {
        return ret;
    }
    
    uint32_t entry_count = buf->read_4bytes();
    if (entry_count > 0) {
        entries.resize(entry_count);
    }
    for (int i = 0; i < (int)entry_count; i++) {
        SrsMp4ElstEntry& entry = entries[i];
        
        if (version == 1) {
            entry.segment_duration = buf->read_8bytes();
            entry.media_time = buf->read_8bytes();
        } else {
            entry.segment_duration = buf->read_4bytes();
            entry.media_time = buf->read_4bytes();
        }
        
        entry.media_rate_integer = buf->read_2bytes();
        entry.media_rate_fraction = buf->read_2bytes();
    }
    
    return ret;
}

stringstream& SrsMp4EditListBox::dumps_detail(stringstream& ss, SrsMp4DumpContext dc)
{
    SrsMp4FullBox::dumps_detail(ss, dc);
    
    ss << ", " << entries.size() << " childs";
    
    if (!entries.empty()) {
        ss << "(+)" << endl;
        srs_padding(ss, dc.indent());
        srs_dumps_array(entries, ss, dc.indent(), srs_pfn_detail, srs_delimiter_newline);
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

SrsMp4MediaHeaderBox::SrsMp4MediaHeaderBox()
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

int SrsMp4MediaHeaderBox::encode_header(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = SrsMp4FullBox::encode_header(buf)) != ERROR_SUCCESS) {
        return ret;
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
    
    return ret;
}

int SrsMp4MediaHeaderBox::decode_header(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = SrsMp4FullBox::decode_header(buf)) != ERROR_SUCCESS) {
        return ret;
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
    
    return ret;
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

int SrsMp4HandlerReferenceBox::encode_header(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = SrsMp4FullBox::encode_header(buf)) != ERROR_SUCCESS) {
        return ret;
    }
    
    buf->write_4bytes(pre_defined);
    buf->write_4bytes(handler_type);
    buf->write_4bytes(reserved[0]);
    buf->write_4bytes(reserved[1]);
    buf->write_4bytes(reserved[2]);
    srs_mp4_string_write(buf, name);
    
    return ret;
}

int SrsMp4HandlerReferenceBox::decode_header(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = SrsMp4FullBox::decode_header(buf)) != ERROR_SUCCESS) {
        return ret;
    }
    
    buf->skip(4);
    handler_type = (SrsMp4HandlerType)buf->read_4bytes();
    buf->skip(12);
    
    if ((ret = srs_mp4_string_read(buf, name, left_space(buf))) != ERROR_SUCCESS) {
        srs_error("MP4 hdlr read string failed. ret=%d", ret);
        return ret;
    }
    
    return ret;
}

stringstream& SrsMp4HandlerReferenceBox::dumps_detail(stringstream& ss, SrsMp4DumpContext dc)
{
    SrsMp4FullBox::dumps_detail(ss, dc);
    
    ss << ", ";
    srs_print_mp4_type(ss, (uint32_t)handler_type);
    ss << ", " <<  name;
    
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

int SrsMp4VideoMeidaHeaderBox::encode_header(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = SrsMp4FullBox::encode_header(buf)) != ERROR_SUCCESS) {
        return ret;
    }
    
    buf->write_2bytes(graphicsmode);
    buf->write_2bytes(opcolor[0]);
    buf->write_2bytes(opcolor[1]);
    buf->write_2bytes(opcolor[2]);
    
    return ret;
}

int SrsMp4VideoMeidaHeaderBox::decode_header(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = SrsMp4FullBox::decode_header(buf)) != ERROR_SUCCESS) {
        return ret;
    }
    
    graphicsmode = buf->read_2bytes();
    opcolor[0] = buf->read_2bytes();
    opcolor[1] = buf->read_2bytes();
    opcolor[2] = buf->read_2bytes();
    
    return ret;
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

int SrsMp4SoundMeidaHeaderBox::encode_header(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = SrsMp4FullBox::encode_header(buf)) != ERROR_SUCCESS) {
        return ret;
    }
    
    buf->write_2bytes(balance);
    buf->write_2bytes(reserved);
    
    return ret;
}

int SrsMp4SoundMeidaHeaderBox::decode_header(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = SrsMp4FullBox::decode_header(buf)) != ERROR_SUCCESS) {
        return ret;
    }
    
    balance = buf->read_2bytes();
    buf->skip(2);
    
    return ret;
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

SrsMp4DataEntryUrlBox::SrsMp4DataEntryUrlBox()
{
    type = SrsMp4BoxTypeURL;
}

SrsMp4DataEntryUrlBox::~SrsMp4DataEntryUrlBox()
{
}

int SrsMp4DataEntryUrlBox::nb_header()
{
    // a 24-bit integer with flags; one flag is defined (x000001) which means that the media
    // data is in the same file as the Movie Box containing this data reference.
    if (location.empty()) {
        return SrsMp4FullBox::nb_header();
    }
    return SrsMp4FullBox::nb_header()+srs_mp4_string_length(location);
}

int SrsMp4DataEntryUrlBox::encode_header(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    // a 24-bit integer with flags; one flag is defined (x000001) which means that the media
    // data is in the same file as the Movie Box containing this data reference.
    if (location.empty()) {
        flags = 0x01;
    }
    
    if ((ret = SrsMp4FullBox::encode_header(buf)) != ERROR_SUCCESS) {
        return ret;
    }
    
    if (!location.empty()) {
        srs_mp4_string_write(buf, location);
    }
    
    return ret;
}

int SrsMp4DataEntryUrlBox::decode_header(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = SrsMp4FullBox::decode_header(buf)) != ERROR_SUCCESS) {
        return ret;
    }
    
    // a 24-bit integer with flags; one flag is defined (x000001) which means that the media
    // data is in the same file as the Movie Box containing this data reference.
    if (flags == 0x01) {
        return ret;
    }
    
    if ((ret = srs_mp4_string_read(buf, location, left_space(buf))) != ERROR_SUCCESS) {
        srs_error("MP4 url read location failed. ret=%d", ret);
        return ret;
    }
    
    return ret;
}

stringstream& SrsMp4DataEntryUrlBox::dumps_detail(stringstream& ss, SrsMp4DumpContext dc)
{
    ss << "URL: " << location;
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

int SrsMp4DataEntryUrnBox::encode_header(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = SrsMp4DataEntryBox::encode_header(buf)) != ERROR_SUCCESS) {
        return ret;
    }
    
    srs_mp4_string_write(buf, location);
    srs_mp4_string_write(buf, name);
    
    return ret;
}

int SrsMp4DataEntryUrnBox::decode_header(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = SrsMp4DataEntryBox::decode_header(buf)) != ERROR_SUCCESS) {
        return ret;
    }
    
    if ((ret = srs_mp4_string_read(buf, location, left_space(buf))) != ERROR_SUCCESS) {
        srs_error("MP4 urn read location failed. ret=%d", ret);
        return ret;
    }
    
    if ((ret = srs_mp4_string_read(buf, name, left_space(buf))) != ERROR_SUCCESS) {
        srs_error("MP4 urn read name failed. ret=%d", ret);
        return ret;
    }
    
    return ret;
}

stringstream& SrsMp4DataEntryUrnBox::dumps_detail(stringstream& ss, SrsMp4DumpContext dc)
{
    ss << "URN: " << name << ", " << location;
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

SrsMp4DataReferenceBox* SrsMp4DataReferenceBox::append(SrsMp4DataEntryBox* v)
{
    entries.push_back(v);
    return this;
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

int SrsMp4DataReferenceBox::encode_header(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = SrsMp4FullBox::encode_header(buf)) != ERROR_SUCCESS) {
        return ret;
    }
    
    buf->write_4bytes((int32_t)entries.size());
    
    vector<SrsMp4DataEntryBox*>::iterator it;
    for (it = entries.begin(); it != entries.end(); ++it) {
        SrsMp4DataEntryBox* entry = *it;
        if ((ret = entry->encode(buf)) != ERROR_SUCCESS) {
            return ret;
        }
    }
    
    return ret;
}

int SrsMp4DataReferenceBox::decode_header(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = SrsMp4FullBox::decode_header(buf)) != ERROR_SUCCESS) {
        return ret;
    }
    
    uint32_t nb_entries = buf->read_4bytes();
    for (uint32_t i = 0; i < nb_entries; i++) {
        SrsMp4Box* box = NULL;
        if ((ret = SrsMp4Box::discovery(buf, &box)) != ERROR_SUCCESS) {
            return ret;
        }
        
        if ((ret = box->decode(buf)) != ERROR_SUCCESS) {
            return ret;
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
    
    return ret;
}

stringstream& SrsMp4DataReferenceBox::dumps_detail(stringstream& ss, SrsMp4DumpContext dc)
{
    SrsMp4FullBox::dumps_detail(ss, dc);
    
    ss << ", " << entries.size() << " childs";
    if (!entries.empty()) {
        ss << "(+)" << endl;
        srs_padding(ss, dc.indent());
        srs_dumps_array(entries, ss, dc.indent(), srs_pfn_pdetail, srs_delimiter_newline);
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

int SrsMp4SampleTableBox::encode_header(SrsBuffer* buf)
{
    return SrsMp4Box::encode_header(buf);
}

int SrsMp4SampleTableBox::decode_header(SrsBuffer* buf)
{
    return SrsMp4Box::decode_header(buf);
}

SrsMp4SampleEntry::SrsMp4SampleEntry()
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

int SrsMp4SampleEntry::encode_header(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = SrsMp4Box::encode_header(buf)) != ERROR_SUCCESS) {
        return ret;
    }
    
    for (int i = 0; i < 6; i++) {
        buf->write_1bytes(reserved[i]);
    }
    buf->write_2bytes(data_reference_index);
    
    return ret;
}

int SrsMp4SampleEntry::decode_header(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = SrsMp4Box::decode_header(buf)) != ERROR_SUCCESS) {
        return ret;
    }
    
    buf->skip(6);
    data_reference_index = buf->read_2bytes();
    
    return ret;
}

stringstream& SrsMp4SampleEntry::dumps_detail(stringstream& ss, SrsMp4DumpContext dc)
{
    SrsMp4Box::dumps_detail(ss, dc);
    
    ss << ", refs#" << data_reference_index;
    return ss;
}

SrsMp4VisualSampleEntry::SrsMp4VisualSampleEntry()
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

int SrsMp4VisualSampleEntry::encode_header(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = SrsMp4SampleEntry::encode_header(buf)) != ERROR_SUCCESS) {
        return ret;
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
    
    return ret;
}

int SrsMp4VisualSampleEntry::decode_header(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = SrsMp4SampleEntry::decode_header(buf)) != ERROR_SUCCESS) {
        return ret;
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
    
    return ret;
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

int SrsMp4AvccBox::encode_header(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = SrsMp4Box::encode_header(buf)) != ERROR_SUCCESS) {
        return ret;
    }
    
    if (!avc_config.empty()) {
        buf->write_bytes(&avc_config[0], (int)avc_config.size());
    }
    
    return ret;
}

int SrsMp4AvccBox::decode_header(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = SrsMp4Box::decode_header(buf)) != ERROR_SUCCESS) {
        return ret;
    }
    
    int nb_config = left_space(buf);
    if (nb_config) {
        avc_config.resize(nb_config);
        buf->read_bytes(&avc_config[0], nb_config);
    }
    
    return ret;
}

stringstream& SrsMp4AvccBox::dumps_detail(stringstream& ss, SrsMp4DumpContext dc)
{
    SrsMp4Box::dumps_detail(ss, dc);
    
    ss << ", AVC Config: " << (int)avc_config.size() << "B" << endl;
    srs_padding(ss, dc.indent());
    srs_print_bytes(ss, (const char*)&avc_config[0], (int)avc_config.size(), dc.indent());
    return ss;
}

SrsMp4AudioSampleEntry::SrsMp4AudioSampleEntry()
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

int SrsMp4AudioSampleEntry::encode_header(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = SrsMp4SampleEntry::encode_header(buf)) != ERROR_SUCCESS) {
        return ret;
    }
    
    buf->write_8bytes(reserved0);
    buf->write_2bytes(channelcount);
    buf->write_2bytes(samplesize);
    buf->write_2bytes(pre_defined0);
    buf->write_2bytes(reserved1);
    buf->write_4bytes(samplerate);
    
    return ret;
}

int SrsMp4AudioSampleEntry::decode_header(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = SrsMp4SampleEntry::decode_header(buf)) != ERROR_SUCCESS) {
        return ret;
    }
    
    buf->skip(8);
    channelcount = buf->read_2bytes();
    samplesize = buf->read_2bytes();
    buf->skip(2);
    buf->skip(2);
    samplerate = buf->read_4bytes();
    
    return ret;
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
    return vlen - (buf->pos() - start_pos);
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

int SrsMp4BaseDescriptor::encode(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    int size = nb_bytes();
    if (!buf->require(size)) {
        ret = ERROR_MP4_BOX_REQUIRE_SPACE;
        srs_error("MP4 ES requires %d bytes space. ret=%d", size, ret);
        return ret;
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
    
    if ((ret = encode_payload(buf)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int SrsMp4BaseDescriptor::decode(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    int size = nb_bytes();
    if (!buf->require(size)) {
        ret = ERROR_MP4_BOX_REQUIRE_SPACE;
        srs_error("MP4 ES requires %d bytes space. ret=%d", size, ret);
        return ret;
    }
    
    tag = (SrsMp4ESTagEs)buf->read_1bytes();
    
    uint8_t v = 0x80;
    int32_t length = 0x00;
    while ((v&0x80) == 0x80) {
        if (!buf->require(1)) {
            ret = ERROR_MP4_BOX_REQUIRE_SPACE;
            srs_error("MP4 ES requires 1 byte space. ret=%d", ret);
            return ret;
        }
        v = buf->read_1bytes();
        
        length = (length<<7) | (v&0x7f);
    }
    vlen = length;
    
    if (!buf->require(vlen)) {
        ret = ERROR_MP4_BOX_REQUIRE_SPACE;
        srs_error("MP4 ES requires %d bytes space. ret=%d", vlen, ret);
        return ret;
    }
    
    start_pos = buf->pos();
    
    if ((ret = decode_payload(buf)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
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

int SrsMp4DecoderSpecificInfo::encode_payload(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    if (!asc.empty()) {
        buf->write_bytes(&asc[0], (int)asc.size());
    }
    
    return ret;
}

int SrsMp4DecoderSpecificInfo::decode_payload(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    int nb_asc = vlen;
    if (nb_asc) {
        asc.resize(nb_asc);
        buf->read_bytes(&asc[0], nb_asc);
    }
    
    return ret;
}

stringstream& SrsMp4DecoderSpecificInfo::dumps_detail(stringstream& ss, SrsMp4DumpContext dc)
{
    SrsMp4BaseDescriptor::dumps_detail(ss, dc);
    
    ss << ", ASC " << asc.size() << "B";
    
    ss << endl;
    srs_padding(ss, dc.indent());
    return srs_print_bytes(ss, (const char*)&asc[0], (int)asc.size(), dc.indent());
}

SrsMp4DecoderConfigDescriptor::SrsMp4DecoderConfigDescriptor()
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

int SrsMp4DecoderConfigDescriptor::encode_payload(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    buf->write_1bytes(objectTypeIndication);
    
    uint8_t v = reserved;
    v |= (upStream&0x01)<<1;
    v |= uint8_t(streamType&0x3f)<<2;
    buf->write_1bytes(v);
    
    buf->write_3bytes(bufferSizeDB);
    buf->write_4bytes(maxBitrate);
    buf->write_4bytes(avgBitrate);
    
    if (decSpecificInfo && (ret = decSpecificInfo->encode(buf)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int SrsMp4DecoderConfigDescriptor::decode_payload(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
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
        if ((ret = decSpecificInfo->decode(buf)) != ERROR_SUCCESS) {
            return ret;
        }
    }
    
    return ret;
}

stringstream& SrsMp4DecoderConfigDescriptor::dumps_detail(stringstream& ss, SrsMp4DumpContext dc)
{
    SrsMp4BaseDescriptor::dumps_detail(ss, dc);
    
    ss << ", type=" << objectTypeIndication << ", stream=" << streamType;
    
    ss << endl;
    srs_padding(ss, dc.indent());
    
    ss << "decoder specific";
    return decSpecificInfo->dumps_detail(ss, dc.indent());
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

int SrsMp4SLConfigDescriptor::encode_payload(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    buf->write_1bytes(predefined);
    
    return ret;
}

int SrsMp4SLConfigDescriptor::decode_payload(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    predefined = buf->read_1bytes();
    
    // TODO: FIXME: To support complete SL Config.
    if (predefined != 0x02) {
        ret = ERROR_MP4_ESDS_SL_Config;
        srs_error("MP4 illegal ESDS SL Config, predefined=%d. ret=%d", predefined, ret);
        return ret;
    }
    
    return ret;
}

SrsMp4ES_Descriptor::SrsMp4ES_Descriptor()
{
    tag = SrsMp4ESTagESDescrTag;
    streamDependenceFlag = URL_Flag = OCRstreamFlag = 0;
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

int SrsMp4ES_Descriptor::encode_payload(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
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
        buf->write_bytes(&URLstring[0], URLstring.size());
    }
    
    if (OCRstreamFlag) {
        buf->write_2bytes(OCR_ES_Id);
    }
    
    if ((ret = decConfigDescr.encode(buf)) != ERROR_SUCCESS) {
        return ret;
    }
    
    if ((ret = slConfigDescr.encode(buf)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int SrsMp4ES_Descriptor::decode_payload(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    ES_ID = buf->read_2bytes();
    
    uint8_t v = buf->read_1bytes();
    streamPriority = v & 0x1f;
    streamDependenceFlag = (v >> 7) & 0x01;
    URL_Flag = (v >> 6) & 0x01;
    OCRstreamFlag = (v >> 5) & 0x01;
    
    if (streamDependenceFlag) {
        if (!buf->require(2)) {
            ret = ERROR_MP4_BOX_REQUIRE_SPACE;
            srs_error("MP4 ES requires 2 bytes space. ret=%d", ret);
            return ret;
        }
        dependsOn_ES_ID = buf->read_2bytes();
    }
    
    if (URL_Flag) {
        if (!buf->require(1)) {
            ret = ERROR_MP4_BOX_REQUIRE_SPACE;
            srs_error("MP4 ES requires 1 byte space. ret=%d", ret);
            return ret;
        }
        uint8_t URLlength = buf->read_1bytes();
        
        if (!buf->require(URLlength)) {
            ret = ERROR_MP4_BOX_REQUIRE_SPACE;
            srs_error("MP4 ES requires %d bytes space. ret=%d", URLlength, ret);
            return ret;
        }
        URLstring.resize(URLlength);
        buf->read_bytes(&URLstring[0], URLlength);
    }
    
    if (OCRstreamFlag) {
        if (!buf->require(2)) {
            ret = ERROR_MP4_BOX_REQUIRE_SPACE;
            srs_error("MP4 ES requires 2 bytes space. ret=%d", ret);
            return ret;
        }
        OCR_ES_Id = buf->read_2bytes();
    }
    
    if ((ret = decConfigDescr.decode(buf)) != ERROR_SUCCESS) {
        return ret;
    }
    
    if ((ret = slConfigDescr.decode(buf)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

stringstream& SrsMp4ES_Descriptor::dumps_detail(stringstream& ss, SrsMp4DumpContext dc)
{
    SrsMp4BaseDescriptor::dumps_detail(ss, dc);
    
    ss << ", ID=" << ES_ID;
    
    ss << endl;
    srs_padding(ss, dc.indent());
    
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

int SrsMp4EsdsBox::encode_header(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = SrsMp4FullBox::encode_header(buf)) != ERROR_SUCCESS) {
        return ret;
    }
    
    int left = left_space(buf);
    SrsBuffer buffer(buf->data() + buf->pos(), left);
    if ((ret = es->encode(&buffer)) != ERROR_SUCCESS) {
        return ret;
    }
    
    buf->skip(buffer.pos());
    
    return ret;
}

int SrsMp4EsdsBox::decode_header(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = SrsMp4FullBox::decode_header(buf)) != ERROR_SUCCESS) {
        return ret;
    }
    
    int left = left_space(buf);
    SrsBuffer buffer(buf->data() + buf->pos(), left);
    if ((ret = es->decode(&buffer)) != ERROR_SUCCESS) {
        return ret;
    }
    
    buf->skip(buffer.pos());
    
    return ret;
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

SrsMp4SampleDescriptionBox* SrsMp4SampleDescriptionBox::append(SrsMp4SampleEntry* v)
{
    entries.push_back(v);
    return this;
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

int SrsMp4SampleDescriptionBox::encode_header(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = SrsMp4FullBox::encode_header(buf)) != ERROR_SUCCESS) {
        return ret;
    }
    
    buf->write_4bytes(entry_count());
    
    vector<SrsMp4SampleEntry*>::iterator it;
    for (it = entries.begin(); it != entries.end(); ++it) {
        SrsMp4SampleEntry* entry = *it;
        if ((ret = entry->encode(buf)) != ERROR_SUCCESS) {
            return ret;
        }
    }
    
    return ret;
}

int SrsMp4SampleDescriptionBox::decode_header(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = SrsMp4FullBox::decode_header(buf)) != ERROR_SUCCESS) {
        return ret;
    }
    
    uint32_t nb_entries = buf->read_4bytes();
    for (uint32_t i = 0; i < nb_entries; i++) {
        SrsMp4Box* box = NULL;
        if ((ret = SrsMp4Box::discovery(buf, &box)) != ERROR_SUCCESS) {
            return ret;
        }
        
        if ((ret = box->decode(buf)) != ERROR_SUCCESS) {
            return ret;
        }
        
        SrsMp4SampleEntry* entry = dynamic_cast<SrsMp4SampleEntry*>(box);
        if (entry) {
            entries.push_back(entry);
        } else {
            srs_freep(box);
        }
    }
    
    return ret;
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
        srs_dumps_array(entries, ss, dc.indent(), srs_pfn_pbox, srs_delimiter_newline);
    }
    return ss;
}

SrsMp4SttsEntry::SrsMp4SttsEntry()
{
    sample_count = 0;
    sample_delta = 0;
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

int SrsMp4DecodingTime2SampleBox::initialize_counter()
{
    int ret = ERROR_SUCCESS;
    
    index = 0;
    if (index >= entries.size()) {
        ret = ERROR_MP4_ILLEGAL_TIMESTAMP;
        srs_error("MP4 illegal ts, empty stts. ret=%d", ret);
        return ret;
    }
    
    count = entries[0].sample_count;
    
    return ret;
}

int SrsMp4DecodingTime2SampleBox::on_sample(uint32_t sample_index, SrsMp4SttsEntry** ppentry)
{
    int ret = ERROR_SUCCESS;
    
    if (sample_index + 1 > count) {
        index++;
        
        if (index >= entries.size()) {
            ret = ERROR_MP4_ILLEGAL_TIMESTAMP;
            srs_error("MP4 illegal ts, stts overflow, count=%d. ret=%d", entries.size(), ret);
            return ret;
        }
        
        count += entries[index].sample_count;
    }
    
    *ppentry = &entries[index];
    
    return ret;
}

int SrsMp4DecodingTime2SampleBox::nb_header()
{
    return SrsMp4FullBox::nb_header() + 4 + 8 * (int)entries.size();
}

int SrsMp4DecodingTime2SampleBox::encode_header(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = SrsMp4FullBox::encode_header(buf)) != ERROR_SUCCESS) {
        return ret;
    }
    
    buf->write_4bytes((int)entries.size());
    for (size_t i = 0; i < entries.size(); i++) {
        SrsMp4SttsEntry& entry = entries[i];
        buf->write_4bytes(entry.sample_count);
        buf->write_4bytes(entry.sample_delta);
    }
    
    return ret;
}

int SrsMp4DecodingTime2SampleBox::decode_header(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = SrsMp4FullBox::decode_header(buf)) != ERROR_SUCCESS) {
        return ret;
    }
    
    uint32_t entry_count = buf->read_4bytes();
    if (entry_count) {
        entries.resize(entry_count);
    }
    for (size_t i = 0; i < entry_count; i++) {
        SrsMp4SttsEntry& entry = entries[i];
        entry.sample_count = buf->read_4bytes();
        entry.sample_delta = buf->read_4bytes();
    }
    
    return ret;
}

stringstream& SrsMp4DecodingTime2SampleBox::dumps_detail(stringstream& ss, SrsMp4DumpContext dc)
{
    SrsMp4FullBox::dumps_detail(ss, dc);
    
    ss << ", " << entries.size() << " childs (+)";
    if (!entries.empty()) {
        ss << endl;
        srs_padding(ss, dc.indent());
        srs_dumps_array(entries, ss, dc.indent(), srs_pfn_detail, srs_delimiter_newline);
    }
    return ss;
}

SrsMp4CttsEntry::SrsMp4CttsEntry()
{
    sample_count = 0;
    sample_offset = 0;
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

int SrsMp4CompositionTime2SampleBox::initialize_counter()
{
    int ret = ERROR_SUCCESS;
    
    index = 0;
    if (index >= entries.size()) {
        ret = ERROR_MP4_ILLEGAL_TIMESTAMP;
        srs_error("MP4 illegal ts, empty ctts. ret=%d", ret);
        return ret;
    }
    
    count = entries[0].sample_count;
    
    return ret;
}

int SrsMp4CompositionTime2SampleBox::on_sample(uint32_t sample_index, SrsMp4CttsEntry** ppentry)
{
    int ret = ERROR_SUCCESS;
    
    if (sample_index + 1 > count) {
        index++;
        
        if (index >= entries.size()) {
            ret = ERROR_MP4_ILLEGAL_TIMESTAMP;
            srs_error("MP4 illegal ts, ctts overflow, count=%d. ret=%d", entries.size(), ret);
            return ret;
        }
        
        count += entries[index].sample_count;
    }
    
    *ppentry = &entries[index];
    
    return ret;
}

int SrsMp4CompositionTime2SampleBox::nb_header()
{
    return SrsMp4FullBox::nb_header() + 4 + 8 * (int)entries.size();
}

int SrsMp4CompositionTime2SampleBox::encode_header(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = SrsMp4FullBox::encode_header(buf)) != ERROR_SUCCESS) {
        return ret;
    }
    
    buf->write_4bytes((int)entries.size());
    for (size_t i = 0; i < entries.size(); i++) {
        SrsMp4CttsEntry& entry = entries[i];
        buf->write_4bytes(entry.sample_count);
        if (version == 0) {
            buf->write_4bytes((uint32_t)entry.sample_offset);
        } else if (version == 1) {
            buf->write_4bytes((int32_t)entry.sample_offset);
        }
    }
    
    return ret;
}

int SrsMp4CompositionTime2SampleBox::decode_header(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = SrsMp4FullBox::decode_header(buf)) != ERROR_SUCCESS) {
        return ret;
    }
    
    uint32_t entry_count = buf->read_4bytes();
    if (entry_count) {
        entries.resize(entry_count);
    }
    for (size_t i = 0; i < entry_count; i++) {
        SrsMp4CttsEntry& entry = entries[i];
        entry.sample_count = buf->read_4bytes();
        if (version == 0) {
            entry.sample_offset = (uint32_t)buf->read_4bytes();
        } else if (version == 1) {
            entry.sample_offset = (int32_t)buf->read_4bytes();
        }
    }
    
    return ret;
}

stringstream& SrsMp4CompositionTime2SampleBox::dumps_detail(stringstream& ss, SrsMp4DumpContext dc)
{
    SrsMp4FullBox::dumps_detail(ss, dc);
    
    ss << ", " << entries.size() << " childs (+)";
    if (!entries.empty()) {
        ss << endl;
        srs_padding(ss, dc.indent());
        srs_dumps_array(entries, ss, dc.indent(), srs_pfn_detail, srs_delimiter_newline);
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

int SrsMp4SyncSampleBox::encode_header(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = SrsMp4FullBox::encode_header(buf)) != ERROR_SUCCESS) {
        return ret;
    }
    
    buf->write_4bytes(entry_count);
    for (uint32_t i = 0; i < entry_count; i++) {
        uint32_t sample_number = sample_numbers[i];
        buf->write_4bytes(sample_number);
    }
    
    return ret;
}

int SrsMp4SyncSampleBox::decode_header(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = SrsMp4FullBox::decode_header(buf)) != ERROR_SUCCESS) {
        return ret;
    }
    
    entry_count = buf->read_4bytes();
    if (entry_count > 0) {
        sample_numbers = new uint32_t[entry_count];
    }
    for (uint32_t i = 0; i < entry_count; i++) {
        sample_numbers[i] = buf->read_4bytes();
    }
    
    return ret;
}

stringstream& SrsMp4SyncSampleBox::dumps_detail(stringstream& ss, SrsMp4DumpContext dc)
{
    SrsMp4FullBox::dumps_detail(ss, dc);
    
    ss << ", count=" << entry_count;
    if (entry_count > 0) {
        ss << endl;
        srs_padding(ss, dc.indent());
        srs_dumps_array(sample_numbers, entry_count, ss, dc.indent(), srs_pfn_elems, srs_delimiter_inlinespace);
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

int SrsMp4Sample2ChunkBox::encode_header(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = SrsMp4FullBox::encode_header(buf)) != ERROR_SUCCESS) {
        return ret;
    }
    
    buf->write_4bytes(entry_count);
    for (uint32_t i = 0; i < entry_count; i++) {
        SrsMp4StscEntry& entry = entries[i];
        buf->write_4bytes(entry.first_chunk);
        buf->write_4bytes(entry.samples_per_chunk);
        buf->write_4bytes(entry.sample_description_index);
    }
    
    return ret;
}

int SrsMp4Sample2ChunkBox::decode_header(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = SrsMp4FullBox::decode_header(buf)) != ERROR_SUCCESS) {
        return ret;
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
    
    return ret;
}

stringstream& SrsMp4Sample2ChunkBox::dumps_detail(stringstream& ss, SrsMp4DumpContext dc)
{
    SrsMp4FullBox::dumps_detail(ss, dc);
    
    ss << ", " << entry_count << " childs (+)";
    if (entry_count > 0) {
        ss << endl;
        srs_padding(ss, dc.indent());
        srs_dumps_array(entries, entry_count, ss, dc.indent(), srs_pfn_detail, srs_delimiter_newline);
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

int SrsMp4ChunkOffsetBox::encode_header(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = SrsMp4FullBox::encode_header(buf)) != ERROR_SUCCESS) {
        return ret;
    }
    
    buf->write_4bytes(entry_count);
    for (uint32_t i = 0; i < entry_count; i++) {
        buf->write_4bytes(entries[i]);
    }
    
    return ret;
}

int SrsMp4ChunkOffsetBox::decode_header(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = SrsMp4FullBox::decode_header(buf)) != ERROR_SUCCESS) {
        return ret;
    }
    
    entry_count = buf->read_4bytes();
    if (entry_count) {
        entries = new uint32_t[entry_count];
    }
    for (uint32_t i = 0; i < entry_count; i++) {
        entries[i] = buf->read_4bytes();
    }
    
    return ret;
}

stringstream& SrsMp4ChunkOffsetBox::dumps_detail(stringstream& ss, SrsMp4DumpContext dc)
{
    SrsMp4FullBox::dumps_detail(ss, dc);
    
    ss << ", " << entry_count << " childs (+)";
    if (entry_count > 0) {
        ss << endl;
        srs_padding(ss, dc.indent());
        srs_dumps_array(entries, entry_count, ss, dc.indent(), srs_pfn_elems, srs_delimiter_inlinespace);
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

int SrsMp4ChunkLargeOffsetBox::encode_header(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = SrsMp4FullBox::encode_header(buf)) != ERROR_SUCCESS) {
        return ret;
    }
    
    buf->write_4bytes(entry_count);
    for (uint32_t i = 0; i < entry_count; i++) {
        buf->write_8bytes(entries[i]);
    }
    
    return ret;
}

int SrsMp4ChunkLargeOffsetBox::decode_header(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = SrsMp4FullBox::decode_header(buf)) != ERROR_SUCCESS) {
        return ret;
    }
    
    entry_count = buf->read_4bytes();
    if (entry_count) {
        entries = new uint64_t[entry_count];
    }
    for (uint32_t i = 0; i < entry_count; i++) {
        entries[i] = buf->read_8bytes();
    }
    
    return ret;
}

stringstream& SrsMp4ChunkLargeOffsetBox::dumps_detail(stringstream& ss, SrsMp4DumpContext dc)
{
    SrsMp4FullBox::dumps_detail(ss, dc);
    
    ss << ", " << entry_count << " childs (+)";
    if (entry_count > 0) {
        ss << endl;
        srs_padding(ss, dc.indent());
        srs_dumps_array(entries, entry_count, ss, dc.indent(), srs_pfn_elems, srs_delimiter_inlinespace);
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

int SrsMp4SampleSizeBox::get_sample_size(uint32_t sample_index, uint32_t* psample_size)
{
    int ret = ERROR_SUCCESS;
    
    if (sample_size != 0) {
        *psample_size = sample_size;
        return ret;
    }
    
    if (sample_index >= sample_count) {
        ret = ERROR_MP4_MOOV_OVERFLOW;
        srs_error("MP4 stsz overflow, sample_count=%d. ret=%d", sample_count, ret);
    }
    *psample_size = entry_sizes[sample_index];
    
    return ret;
}

int SrsMp4SampleSizeBox::nb_header()
{
    int size = SrsMp4FullBox::nb_header() +4+4;
    if (sample_size == 0) {
        size += 4*sample_count;
    }
    return size;
}

int SrsMp4SampleSizeBox::encode_header(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = SrsMp4FullBox::encode_header(buf)) != ERROR_SUCCESS) {
        return ret;
    }
    
    buf->write_4bytes(sample_size);
    buf->write_4bytes(sample_count);
    for (uint32_t i = 0; i < sample_count && sample_size == 0; i++) {
        buf->write_4bytes(entry_sizes[i]);
    }
    
    return ret;
}

int SrsMp4SampleSizeBox::decode_header(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = SrsMp4FullBox::decode_header(buf)) != ERROR_SUCCESS) {
        return ret;
    }
    
    sample_size = buf->read_4bytes();
    sample_count = buf->read_4bytes();
    if (!sample_size && sample_count) {
        entry_sizes = new uint32_t[sample_count];
    }
    for (uint32_t i = 0; i < sample_count && sample_size == 0; i++) {
        entry_sizes[i] = buf->read_4bytes();
    }
    
    return ret;
}

stringstream& SrsMp4SampleSizeBox::dumps_detail(stringstream& ss, SrsMp4DumpContext dc)
{
    SrsMp4FullBox::dumps_detail(ss, dc);
    
    ss << ", size=" << sample_size << ", " << sample_count << " childs (+)";
    if (!sample_size  && sample_count> 0) {
        ss << endl;
        srs_padding(ss, dc.indent());
        srs_dumps_array(entry_sizes, sample_count, ss, dc.indent(), srs_pfn_elems, srs_delimiter_inlinespace);
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

int SrsMp4UserDataBox::encode_header(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = SrsMp4Box::encode_header(buf)) != ERROR_SUCCESS) {
        return ret;
    }
    
    if (!data.empty()) {
        buf->write_bytes(&data[0], (int)data.size());
    }
    
    return ret;
}

int SrsMp4UserDataBox::decode_header(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = SrsMp4Box::decode_header(buf)) != ERROR_SUCCESS) {
        return ret;
    }
    
    int nb_data = left_space(buf);
    if (nb_data) {
        data.resize(nb_data);
        buf->read_bytes(&data[0], (int)data.size());
    }
    
    return ret;
}

stringstream& SrsMp4UserDataBox::dumps_detail(stringstream& ss, SrsMp4DumpContext dc)
{
    SrsMp4Box::dumps_detail(ss, dc);
    
    ss << ", total " << data.size() << "B";
    
    if (!data.empty()) {
        ss << endl;
        srs_padding(ss, dc.indent());
        srs_dumps_array(&data[0], (int)data.size(), ss, dc.indent(), srs_pfn_hex, srs_delimiter_inlinespace);
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

int SrsMp4SampleManager::load(SrsMp4MovieBox* moov)
{
    int ret = ERROR_SUCCESS;
    
    map<uint64_t, SrsMp4Sample*> tses;
    
    // Load samples from moov, merge to temp samples.
    if ((ret = do_load(tses, moov)) != ERROR_SUCCESS) {
        map<uint64_t, SrsMp4Sample*>::iterator it;
        for (it = tses.begin(); it != tses.end(); ++it) {
            SrsMp4Sample* sample = it->second;
            srs_freep(sample);
        }
        
        return ret;
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
    
    return ret;
}

SrsMp4Sample* SrsMp4SampleManager::at(uint32_t index)
{
    if (index >= samples.size() - 1) {
        return NULL;
    }
    return samples.at(index);
}

void SrsMp4SampleManager::append(SrsMp4Sample* sample)
{
    samples.push_back(sample);
}

int SrsMp4SampleManager::write(SrsMp4MovieBox* moov)
{
    int ret = ERROR_SUCCESS;
    
    SrsMp4TrackBox* vide = moov->video();
    if (vide) {
        bool has_cts = false;
        vector<SrsMp4Sample*>::iterator it;
        for (it = samples.begin(); it != samples.end(); ++it) {
            SrsMp4Sample* sample = *it;
            if (sample->dts != sample->pts) {
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
        
        if ((ret = write_track(SrsFrameTypeVideo, stts, stss, ctts, stsc, stsz, stco)) != ERROR_SUCCESS) {
            return ret;
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
        
        if ((ret = write_track(SrsFrameTypeAudio, stts, stss, ctts, stsc, stsz, stco)) != ERROR_SUCCESS) {
            return ret;
        }
    }
    
    return ret;
}

int SrsMp4SampleManager::write_track(SrsFrameType track,
    SrsMp4DecodingTime2SampleBox* stts, SrsMp4SyncSampleBox* stss, SrsMp4CompositionTime2SampleBox* ctts,
    SrsMp4Sample2ChunkBox* stsc, SrsMp4SampleSizeBox* stsz, SrsMp4ChunkOffsetBox* stco)
{
    int ret = ERROR_SUCCESS;
    
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
    
    return ret;
}

int SrsMp4SampleManager::do_load(map<uint64_t, SrsMp4Sample*>& tses, SrsMp4MovieBox* moov)
{
    int ret = ERROR_SUCCESS;
    
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
            ret = ERROR_MP4_ILLEGAL_TRACK;
            srs_error("MP4 illegal track, empty mdhd/stco/stsz/stsc/stts, type=%d. ret=%d", tt, ret);
            return ret;
        }
        
        if ((ret = load_trak(tses, SrsFrameTypeVideo, mdhd, stco, stsz, stsc, stts, ctts, stss)) != ERROR_SUCCESS) {
            return ret;
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
            ret = ERROR_MP4_ILLEGAL_TRACK;
            srs_error("MP4 illegal track, empty mdhd/stco/stsz/stsc/stts, type=%d. ret=%d", tt, ret);
            return ret;
        }
        
        if ((ret = load_trak(tses, SrsFrameTypeAudio, mdhd, stco, stsz, stsc, stts, NULL, NULL)) != ERROR_SUCCESS) {
            return ret;
        }
    }
    
    return ret;
}

int SrsMp4SampleManager::load_trak(map<uint64_t, SrsMp4Sample*>& tses, SrsFrameType tt,
    SrsMp4MediaHeaderBox* mdhd, SrsMp4ChunkOffsetBox* stco, SrsMp4SampleSizeBox* stsz, SrsMp4Sample2ChunkBox* stsc,
    SrsMp4DecodingTime2SampleBox* stts, SrsMp4CompositionTime2SampleBox* ctts, SrsMp4SyncSampleBox* stss)
{
    int ret = ERROR_SUCCESS;
    
    // Samples per chunk.
    stsc->initialize_counter();
    
    // DTS box.
    if ((ret = stts->initialize_counter()) != ERROR_SUCCESS) {
        return ret;
    }
    
    // CTS/PTS box.
    if (ctts && (ret = ctts->initialize_counter()) != ERROR_SUCCESS) {
        return ret;
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
            if ((ret = stsz->get_sample_size(sample->index, &sample_size)) != ERROR_SUCCESS) {
                return ret;
            }
            sample_relative_offset += sample_size;
            
            SrsMp4SttsEntry* stts_entry = NULL;
            if ((ret = stts->on_sample(sample->index, &stts_entry)) != ERROR_SUCCESS) {
                return ret;
            }
            if (previous) {
                sample->pts = sample->dts = previous->dts + stts_entry->sample_delta;
            }
            
            SrsMp4CttsEntry* ctts_entry = NULL;
            if (ctts && (ret = ctts->on_sample(sample->index, &ctts_entry)) != ERROR_SUCCESS) {
                return ret;
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
        ret = ERROR_MP4_ILLEGAL_SAMPLES;
        srs_error("MP4 illegal samples count, expect=%d, actual=%d. ret=%d", stsz->sample_count, previous->index + 1, ret);
        return ret;
    }
    
    return ret;
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

int SrsMp4BoxReader::initialize(ISrsReadSeeker* rs)
{
    rsio = rs;
    
    return ERROR_SUCCESS;
}

int SrsMp4BoxReader::read(SrsSimpleStream* stream, SrsMp4Box** ppbox)
{
    int ret = ERROR_SUCCESS;
    
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
            if ((ret = rsio->read(buf, SRS_MP4_BUF_SIZE, &nread)) != ERROR_SUCCESS) {
                if (ret != ERROR_SYSTEM_FILE_EOF) {
                    srs_error("MP4 load failed, nread=%d, required=%d. ret=%d", nread, required, ret);
                }
                return ret;
            }
            
            srs_assert(nread > 0);
            stream->append(buf, (int)nread);
        }
        
        SrsBuffer* buffer = new SrsBuffer(stream->bytes(), stream->length());
        SrsAutoFree(SrsBuffer, buffer);
        
        // Discovery the box with basic header.
        if (!box && (ret = SrsMp4Box::discovery(buffer, &box)) != ERROR_SUCCESS) {
            if (ret == ERROR_MP4_BOX_REQUIRE_SPACE) {
                continue;
            }
            srs_error("MP4 load box failed. ret=%d", ret);
            return ret;
        }
        
        // When box is discoveried, check whether we can demux the whole box.
        // For mdat, only the header is required.
        required = (box->is_mdat()? box->sz_header():box->sz());
        if (!buffer->require((int)required)) {
            continue;
        }
        
        if (ret != ERROR_SUCCESS) {
            srs_freep(box);
        } else {
            *ppbox = box;
        }
        
        break;
    }
    
    return ret;
}

int SrsMp4BoxReader::skip(SrsMp4Box* box, SrsSimpleStream* stream)
{
    int ret = ERROR_SUCCESS;
    
    // For mdat, always skip the content.
    if (box->is_mdat()) {
        int offset = (int)(box->sz() - stream->length());
        if (offset < 0) {
            stream->erase(stream->length() + offset);
        } else {
            stream->erase(stream->length());
        }
        if (offset > 0 && (ret = rsio->lseek(offset, SEEK_CUR, NULL)) != ERROR_SUCCESS) {
            return ret;
        }
    } else {
        // Remove the consumed bytes.
        stream->erase((int)box->sz());
    }
    
    return ret;
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

int SrsMp4Decoder::initialize(ISrsReadSeeker* rs)
{
    int ret = ERROR_SUCCESS;
    
    srs_assert(rs);
    rsio = rs;
    
    if ((ret = br->initialize(rs)) != ERROR_SUCCESS) {
        return ret;
    }
    
    // For mdat before moov, we must reset the offset to the mdat.
    off_t offset = -1;
    
    while (true) {
        SrsMp4Box* box = NULL;
        
        if ((ret = load_next_box(&box, 0)) != ERROR_SUCCESS) {
            return ret;
        }
        
        if (box->is_ftyp()) {
            SrsMp4FileTypeBox* ftyp = dynamic_cast<SrsMp4FileTypeBox*>(box);
            if ((ret = parse_ftyp(ftyp)) != ERROR_SUCCESS) {
                return ret;
            }
        } else if (box->is_mdat()) {
            off_t cur = 0;
            if ((ret = rsio->lseek(0, SEEK_CUR, &cur)) != ERROR_SUCCESS) {
                return ret;
            }
            offset = off_t(cur - box->sz());
        } else if (box->is_moov()) {
            SrsMp4MovieBox* moov = dynamic_cast<SrsMp4MovieBox*>(box);
            if ((ret = parse_moov(moov)) != ERROR_SUCCESS) {
                return ret;
            }
            break;
        }
        
        srs_freep(box);
    }
    
    if (brand == SrsMp4BoxBrandForbidden) {
        ret = ERROR_MP4_BOX_ILLEGAL_SCHEMA;
        srs_error("MP4 missing ftyp. ret=%d", ret);
        return ret;
    }
    
    // Set the offset to the mdat.
    if (offset >= 0) {
        return rsio->lseek(offset, SEEK_SET, &current_offset);
    }
    
    return ret;
}

int SrsMp4Decoder::read_sample(SrsMp4HandlerType* pht, uint16_t* pft, uint16_t* pct, uint32_t* pdts, uint32_t* ppts, uint8_t** psample, uint32_t* pnb_sample)
{
    int ret = ERROR_SUCCESS;
    
    if (!avcc_written && !pavcc.empty()) {
        avcc_written = true;
        *pdts = *ppts = 0;
        *pht = SrsMp4HandlerTypeVIDE;
        
        uint32_t nb_sample = *pnb_sample = (uint32_t)pavcc.size();
        uint8_t* sample = *psample = new uint8_t[nb_sample];
        memcpy(sample, &pavcc[0], nb_sample);
        
        *pft = SrsVideoAvcFrameTypeKeyFrame;
        *pct = SrsVideoAvcFrameTraitSequenceHeader;
        
        return ret;
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
        
        return ret;
    }
    
    SrsMp4Sample* ps = samples->at(current_index++);
    if (!ps) {
        return ERROR_SYSTEM_FILE_EOF;
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
        if ((ret = rsio->lseek(ps->offset, SEEK_SET, &current_offset)) != ERROR_SUCCESS) {
            return ret;
        }
    }
    
    uint32_t nb_sample = ps->nb_data;
    uint8_t* sample = new uint8_t[nb_sample];
    // TODO: FIXME: Use fully read.
    if ((ret = rsio->read(sample, nb_sample, NULL)) != ERROR_SUCCESS) {
        srs_freepa(sample);
        return ret;
    }
    
    *psample = sample;
    *pnb_sample = nb_sample;
    current_offset += nb_sample;
    
    return ret;
}

int SrsMp4Decoder::parse_ftyp(SrsMp4FileTypeBox* ftyp)
{
    int ret = ERROR_SUCCESS;
    
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
        ret = ERROR_MP4_BOX_ILLEGAL_BRAND;
        srs_error("MP4 brand is illegal, brand=%d. ret=%d", ftyp->major_brand, ret);
        return ret;
    }
    
    brand = ftyp->major_brand;
    
    return ret;
}

int SrsMp4Decoder::parse_moov(SrsMp4MovieBox* moov)
{
    int ret = ERROR_SUCCESS;
    
    SrsMp4MovieHeaderBox* mvhd = moov->mvhd();
    if (!mvhd) {
        ret = ERROR_MP4_ILLEGAL_MOOV;
        srs_error("MP4 missing mvhd. ret=%d", ret);
        return ret;
    }
    
    SrsMp4TrackBox* vide = moov->video();
    SrsMp4TrackBox* soun = moov->audio();
    if (!vide && !soun) {
        ret = ERROR_MP4_ILLEGAL_MOOV;
        srs_error("MP4 missing audio and video track. ret=%d", ret);
        return ret;
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
        ret = ERROR_MP4_ILLEGAL_MOOV;
        srs_error("MP4 missing video sequence header. ret=%d", ret);
        return ret;
    }
    if (soun && !asc) {
        ret = ERROR_MP4_ILLEGAL_MOOV;
        srs_error("MP4 missing audio sequence header. ret=%d", ret);
        return ret;
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
    if ((ret = samples->load(moov)) != ERROR_SUCCESS) {
        srs_error("MP4 load samples failed. ret=%d", ret);
        return ret;
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
    
    return ret;
}

int SrsMp4Decoder::load_next_box(SrsMp4Box** ppbox, uint32_t required_box_type)
{
    int ret = ERROR_SUCCESS;
    
    while (true) {
        SrsMp4Box* box = NULL;
        if ((ret = do_load_next_box(&box, required_box_type)) != ERROR_SUCCESS) {
            srs_freep(box);
            return ret;
        }
        
        if (!required_box_type || box->type == required_box_type) {
            *ppbox = box;
            break;
        }
        srs_freep(box);
    }
    
    return ret;
}

int SrsMp4Decoder::do_load_next_box(SrsMp4Box** ppbox, uint32_t required_box_type)
{
    int ret = ERROR_SUCCESS;
    
    while (true) {
        SrsMp4Box* box = NULL;
        
        if ((ret = br->read(stream, &box)) != ERROR_SUCCESS) {
            return ret;
        }
        
        SrsBuffer* buffer = new SrsBuffer(stream->bytes(), stream->length());
        SrsAutoFree(SrsBuffer, buffer);
        
        // Decode the box:
        // 1. Any box, when no box type is required.
        // 2. Matched box, when box type match the required type.
        // 3. Mdat box, always decode the mdat because we only decode the header of it.
        if (!required_box_type || box->type == required_box_type || box->is_mdat()) {
            ret = box->decode(buffer);
        }
        
        // Skip the box from stream, move stream to next box.
        // For mdat box, skip the content in stream or underylayer reader.
        // For other boxes, skip it from stream because we already decoded it or ignore it.
        if (ret == ERROR_SUCCESS) {
            ret = br->skip(box, stream);
        }
        
        if (ret != ERROR_SUCCESS) {
            srs_freep(box);
        } else {
            *ppbox = box;
        }
        
        break;
    }
    
    return ret;
}

SrsMp4Encoder::SrsMp4Encoder()
{
    wsio = NULL;
    mdat_bytes = 0;
    mdat_offset = 0;
    buffer = new SrsBuffer();
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
    srs_freep(buffer);
}

int SrsMp4Encoder::initialize(ISrsWriteSeeker* ws)
{
    int ret = ERROR_SUCCESS;
    
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
        if ((ret = buffer->initialize(&data[0], nb_data)) != ERROR_SUCCESS) {
            return ret;
        }
        if ((ret = ftyp->encode(buffer)) != ERROR_SUCCESS) {
            return ret;
        }
        
        // TODO: FIXME: Ensure write ok.
        if ((ret = wsio->write(&data[0], nb_data, NULL)) != ERROR_SUCCESS) {
            return ret;
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
        if ((ret = wsio->lseek(0, SEEK_CUR, &mdat_offset)) != ERROR_SUCCESS) {
            return ret;
        }
        
        int nb_data = mdat->sz_header();
        uint8_t* data = new uint8_t[nb_data];
        SrsAutoFreeA(uint8_t, data);
        if ((ret = buffer->initialize((char*)data, nb_data)) != ERROR_SUCCESS) {
            return ret;
        }
        if ((ret = mdat->encode(buffer)) != ERROR_SUCCESS) {
            return ret;
        }
        
        // TODO: FIXME: Ensure all bytes are writen.
        if ((ret = wsio->write(data, nb_data, NULL)) != ERROR_SUCCESS) {
            return ret;
        }
        
        mdat_bytes = 0;
    }
    
    return ret;
}

int SrsMp4Encoder::write_sample(SrsMp4HandlerType ht, uint16_t ft, uint16_t ct, uint32_t dts, uint32_t pts, uint8_t* sample, uint32_t nb_sample)
{
    int ret = ERROR_SUCCESS;
    
    SrsMp4Sample* ps = new SrsMp4Sample();
    
    // For SPS/PPS or ASC, copy it to moov.
    bool vsh = (ht == SrsMp4HandlerTypeVIDE) && (ct == SrsVideoAvcFrameTraitSequenceHeader);
    bool ash = (ht == SrsMp4HandlerTypeSOUN) && (ct == SrsAudioAacFrameTraitSequenceHeader);
    if (vsh || ash) {
        ret = copy_sequence_header(vsh, sample, nb_sample);
        srs_freep(ps);
        return ret;
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
        return ret;
    }
    ps->tbn = 1000;
    ps->dts = dts;
    ps->pts = pts;
    
    if ((ret = do_write_sample(ps, sample, nb_sample)) != ERROR_SUCCESS) {
        srs_freep(ps);
        return ret;
    }
    
    // Append to manager to build the moov.
    samples->append(ps);
    
    return ret;
}

int SrsMp4Encoder::flush()
{
    int ret = ERROR_SUCCESS;
    
    if (!nb_audios && !nb_videos) {
        ret = ERROR_MP4_ILLEGAL_MOOV;
        srs_error("MP4 missing audio and video track. ret=%d", ret);
        return ret;
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
        
        if (nb_videos) {
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
            
            SrsMp4AvccBox* avcC = new SrsMp4AvccBox();
            avc1->set_avcC(avcC);
            
            avcC->avc_config = pavcc;
        }
        
        if (nb_audios) {
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
        
        if ((ret = samples->write(moov)) != ERROR_SUCCESS) {
            return ret;
        }
        
        int nb_data = moov->nb_bytes();
        uint8_t* data = new uint8_t[nb_data];
        SrsAutoFreeA(uint8_t, data);
        if ((ret = buffer->initialize((char*)data, nb_data)) != ERROR_SUCCESS) {
            return ret;
        }
        
        if ((ret = moov->encode(buffer)) != ERROR_SUCCESS) {
            return ret;
        }
        
        // TODO: FIXME: Ensure all bytes are writen.
        if ((ret = wsio->write(data, nb_data, NULL)) != ERROR_SUCCESS) {
            return ret;
        }
    }
    
    // Write mdat box.
    if (true) {
        // Update the mdat box header.
        if ((ret = wsio->lseek(mdat_offset, SEEK_SET, NULL)) != ERROR_SUCCESS) {
            return ret;
        }
        
        // Write mdat box with size of data,
        // its payload already writen by samples,
        // and we will update its header(size) when flush.
        SrsMp4MediaDataBox* mdat = new SrsMp4MediaDataBox();
        SrsAutoFree(SrsMp4MediaDataBox, mdat);
        
        int nb_data = mdat->sz_header();
        uint8_t* data = new uint8_t[nb_data];
        SrsAutoFreeA(uint8_t, data);
        if ((ret = buffer->initialize((char*)data, nb_data)) != ERROR_SUCCESS) {
            return ret;
        }
        
        // TODO: FIXME: Support 64bits size.
        mdat->nb_data = (int)mdat_bytes;
        if ((ret = mdat->encode(buffer)) != ERROR_SUCCESS) {
            return ret;
        }
        
        // TODO: FIXME: Ensure all bytes are writen.
        if ((ret = wsio->write(data, nb_data, NULL)) != ERROR_SUCCESS) {
            return ret;
        }
    }
    
    return ret;
}

int SrsMp4Encoder::copy_sequence_header(bool vsh, uint8_t* sample, uint32_t nb_sample)
{
    int ret = ERROR_SUCCESS;
    
    if (vsh && !pavcc.empty()) {
        if (nb_sample == pavcc.size() && srs_bytes_equals(sample, &pavcc[0], (int)pavcc.size())) {
            return ret;
        }
        
        ret = ERROR_MP4_AVCC_CHANGE;
        srs_error("MP4 doesn't support avcc change. ret=%d", ret);
        return ret;
    }
    
    if (!vsh && !pasc.empty()) {
        if (nb_sample == pasc.size() && srs_bytes_equals(sample, &pasc[0], (int)pasc.size())) {
            return ret;
        }
        
        ret = ERROR_MP4_ASC_CHANGE;
        srs_error("MP4 doesn't support asc change. ret=%d", ret);
        return ret;
    }
    
    if (vsh) {
        pavcc = std::vector<char>(sample, sample + nb_sample);
        
        // TODO: FIXME: Parse the width and height.
    }
    
    if (!vsh) {
        pasc = std::vector<char>(sample, sample + nb_sample);
    }
    
    return ret;
}

int SrsMp4Encoder::do_write_sample(SrsMp4Sample* ps, uint8_t* sample, uint32_t nb_sample)
{
    int ret = ERROR_SUCCESS;
    
    ps->nb_data = nb_sample;
    // Never copy data, for we already writen to writer.
    ps->data = NULL;
    
    // Update the mdat box from this offset.
    if ((ret = wsio->lseek(0, SEEK_CUR, &ps->offset)) != ERROR_SUCCESS) {
        return ret;
    }
    
    // TODO: FIXME: Ensure all bytes are writen.
    if ((ret = wsio->write(sample, nb_sample, NULL)) != ERROR_SUCCESS) {
        return ret;
    }
    
    mdat_bytes += nb_sample;
    
    return ret;
}

