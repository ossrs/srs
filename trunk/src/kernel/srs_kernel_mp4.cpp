/*
The MIT License (MIT)

Copyright (c) 2013-2017 SRS(ossrs)

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

#include <srs_kernel_mp4.hpp>

#include <srs_kernel_log.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_stream.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_io.hpp>
#include <srs_kernel_buffer.hpp>

#include <string.h>
using namespace std;

#define SRS_MP4_BOX_UUID 0x75756964 // 'uuid'
#define SRS_MP4_BOX_FTYP 0x66747970 // 'ftyp'
#define SRS_MP4_BOX_MDAT 0x6d646174 // 'mdat'
#define SRS_MP4_BOX_FREE 0x66726565 // 'free'
#define SRS_MP4_BOX_SKIP 0x736b6970 // 'skip'
#define SRS_MP4_BOX_MOOV 0x6d6f6f76 // 'moov'
#define SRS_MP4_BOX_MVHD 0x6d766864 // 'mvhd'
#define SRS_MP4_BOX_TRAK 0x7472616b // 'trak'
#define SRS_MP4_BOX_TKHD 0x746b6864 // 'tkhd'
#define SRS_MP4_BOX_EDTS 0x65647473 // 'edts'
#define SRS_MP4_BOX_ELST 0x656c7374 // 'elst'
#define SRS_MP4_BOX_MDIA 0x6d646961 // 'mdia'
#define SRS_MP4_BOX_MDHD 0x6d646864 // 'mdhd'
#define SRS_MP4_BOX_HDLR 0x68646c72 // 'hdlr'
#define SRS_MP4_BOX_MINF 0x6d696e66 // 'minf'
#define SRS_MP4_BOX_VMHD 0x766d6864 // 'vmhd'
#define SRS_MP4_BOX_SMHD 0x736d6864 // 'smhd'
#define SRS_MP4_BOX_DINF 0x64696e66 // 'dinf'
#define SRS_MP4_BOX_URL  0x75726c20 // 'url '
#define SRS_MP4_BOX_URN  0x75726e20 // 'urn '
#define SRS_MP4_BOX_DREF 0x64726566 // 'dref'
#define SRS_MP4_BOX_STBL 0x7374626c // 'stbl'
#define SRS_MP4_BOX_STSD 0x73747364 // 'stsd'
#define SRS_MP4_BOX_STTS 0x73747473 // 'stts'
#define SRS_MP4_BOX_CTTS 0x63747473 // 'ctts'
#define SRS_MP4_BOX_STSS 0x73747373 // 'stss'
#define SRS_MP4_BOX_STSC 0x73747363 // 'stsc'
#define SRS_MP4_BOX_STCO 0x7374636f // 'stco'
#define SRS_MP4_BOX_CO64 0x636f3634 // 'co64'
#define SRS_MP4_BOX_STSZ 0x7374737a // 'stsz'
#define SRS_MP4_BOX_STZ2 0x73747a32 // 'stz2'
#define SRS_MP4_BOX_AVC1 0x61766331 // 'avc1'
#define SRS_MP4_BOX_AVCC 0x61766343 // 'avcC'
#define SRS_MP4_BOX_MP4A 0x6d703461 // 'mp4a'
#define SRS_MP4_BOX_ESDS 0x65736473 // 'esds'

#define SRS_MP4_BRAND_ISOM 0x69736f6d // 'isom'
#define SRS_MP4_BRAND_ISO2 0x69736f32 // 'iso2'
#define SRS_MP4_BRAND_AVC1 0x61766331 // 'avc1'
#define SRS_MP4_BRAND_MP41 0x6d703431 // 'mp41'

#define SRS_MP4_HANDLER_VIDE 0x76696465 // 'vide'
#define SRS_MP4_HANDLER_SOUN 0x736f756e // 'soun'

#define SRS_MP4_EOF_SIZE 0
#define SRS_MP4_USE_LARGE_SIZE 1

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
    
    if (len == left) {
        ret = ERROR_MP4_BOX_STRING;
        srs_error("MP4 string corrupt, left=%d. ret=%d", left, ret);
        return ret;
    }
    
    v.append(start, len);
    buf->skip((int)len + 1);
    
    return ret;
}

SrsMp4Box::SrsMp4Box()
{
    smallsize = 0;
    largesize = 0;
    usertype = NULL;
    start_pos = 0;
    type = 0;
}

SrsMp4Box::~SrsMp4Box()
{
    vector<SrsMp4Box*>::iterator it;
    for (it = boxes.begin(); it != boxes.end(); ++it) {
        SrsMp4Box* box = *it;
        srs_freep(box);
    }
    boxes.clear();
    
    srs_freepa(usertype);
}

uint64_t SrsMp4Box::sz()
{
    return smallsize == SRS_MP4_USE_LARGE_SIZE? largesize:smallsize;
}

int SrsMp4Box::left_space(SrsBuffer* buf)
{
    return (int)sz() - (buf->pos() - start_pos);
}

bool SrsMp4Box::is_ftyp()
{
    return type == SRS_MP4_BOX_FTYP;
}

bool SrsMp4Box::is_moov()
{
    return type == SRS_MP4_BOX_MOOV;
}

bool SrsMp4Box::is_mdat()
{
    return type == SRS_MP4_BOX_MDAT;
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
    uint32_t type = (uint32_t)buf->read_4bytes();
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
        srs_error("MP4 discovery overflow 31bits, size=%"PRId64". ret=%d", largesize, ret);
        return ret;
    }
    
    SrsMp4Box* box = NULL;
    switch(type) {
        case SRS_MP4_BOX_FTYP: box = new SrsMp4FileTypeBox(); break;
        case SRS_MP4_BOX_MDAT: box = new SrsMp4MediaDataBox(); break;
        case SRS_MP4_BOX_FREE: case SRS_MP4_BOX_SKIP: box = new SrsMp4FreeSpaceBox(); break;
        case SRS_MP4_BOX_MOOV: box = new SrsMp4MovieBox(); break;
        case SRS_MP4_BOX_MVHD: box = new SrsMp4MovieHeaderBox(); break;
        case SRS_MP4_BOX_TRAK: box = new SrsMp4TrackBox(); break;
        case SRS_MP4_BOX_TKHD: box = new SrsMp4TrackHeaderBox(); break;
        case SRS_MP4_BOX_EDTS: box = new SrsMp4EditBox(); break;
        case SRS_MP4_BOX_ELST: box = new SrsMp4EditListBox(); break;
        case SRS_MP4_BOX_MDIA: box = new SrsMp4MediaBox(); break;
        case SRS_MP4_BOX_MDHD: box = new SrsMp4MediaHeaderBox(); break;
        case SRS_MP4_BOX_HDLR: box = new SrsMp4HandlerReferenceBox(); break;
        case SRS_MP4_BOX_MINF: box = new SrsMp4MediaInformationBox(); break;
        case SRS_MP4_BOX_VMHD: box = new SrsMp4VideoMeidaHeaderBox(); break;
        case SRS_MP4_BOX_SMHD: box = new SrsMp4SoundMeidaHeaderBox(); break;
        case SRS_MP4_BOX_DINF: box = new SrsMp4DataInformationBox(); break;
        case SRS_MP4_BOX_URL: box = new SrsMp4DataEntryUrlBox(); break;
        case SRS_MP4_BOX_URN: box = new SrsMp4DataEntryUrnBox(); break;
        case SRS_MP4_BOX_DREF: box = new SrsMp4DataReferenceBox(); break;
        case SRS_MP4_BOX_STBL: box = new SrsMp4SampleTableBox(); break;
        case SRS_MP4_BOX_STSD: box = new SrsMp4SampleDescriptionBox(); break;
        case SRS_MP4_BOX_STTS: box = new SrsMp4DecodingTime2SampleBox(); break;
        case SRS_MP4_BOX_CTTS: box = new SrsMp4CompositionTime2SampleBox(); break;
        case SRS_MP4_BOX_STSS: box = new SrsMp4SyncSampleBox(); break;
        case SRS_MP4_BOX_STSC: box = new SrsMp4Sample2ChunkBox(); break;
        case SRS_MP4_BOX_STCO: box = new SrsMp4ChunkOffsetBox(); break;
        case SRS_MP4_BOX_CO64: box = new SrsMp4ChunkLargeOffsetBox(); break;
        case SRS_MP4_BOX_STSZ: box = new SrsMp4SampleSizeBox(); break;
        case SRS_MP4_BOX_AVC1: box = new SrsMp4VisualSampleEntry(); break;
        case SRS_MP4_BOX_AVCC: box = new SrsMp4AvccBox(); break;
        case SRS_MP4_BOX_MP4A: box = new SrsMp4AudioSampleEntry(); break;
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
    
    if (type == SRS_MP4_BOX_UUID) {
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
        srs_error("MP4 box size overflow 31bits, size=%"PRId64". ret=%d", sz(), ret);
        return ret;
    }
    
    int size = nb_header();
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
    
    if (type == SRS_MP4_BOX_UUID) {
        buf->write_bytes((char*)usertype, 16);
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
    type = (uint32_t)buf->read_4bytes();
    
    if (smallsize == SRS_MP4_EOF_SIZE) {
        srs_warn("MP4 box EOF.");
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
        srs_error("MP4 box size overflow 31bits, size=%"PRId64". ret=%d", sz(), ret);
        return ret;
    }
    
    if (type == SRS_MP4_BOX_UUID) {
        if (!buf->require(16)) {
            ret = ERROR_MP4_BOX_REQUIRE_SPACE;
            srs_error("MP4 box requires 16 bytes space. ret=%d", ret);
            return ret;
        }
        usertype = new uint8_t[16];
        buf->read_bytes((char*)usertype, 16);
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

SrsMp4FileTypeBox::SrsMp4FileTypeBox()
{
    type = SRS_MP4_BOX_FTYP;
    nb_compatible_brands = 0;
    compatible_brands = NULL;
    major_brand = minor_version = 0;
}

SrsMp4FileTypeBox::~SrsMp4FileTypeBox()
{
    srs_freepa(compatible_brands);
}

int SrsMp4FileTypeBox::nb_header()
{
    return SrsMp4Box::nb_header() + 8 + nb_compatible_brands * 4;
}

int SrsMp4FileTypeBox::encode_header(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = SrsMp4Box::encode_header(buf)) != ERROR_SUCCESS) {
        return ret;
    }
    
    buf->write_4bytes(major_brand);
    buf->write_4bytes(minor_version);
    
    for (int i = 0; i < nb_compatible_brands; i++) {
        uint32_t& cb = compatible_brands[i];
        buf->write_4bytes(cb);
    }
    
    return ret;
}

int SrsMp4FileTypeBox::decode_header(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = SrsMp4Box::decode_header(buf)) != ERROR_SUCCESS) {
        return ret;
    }
    
    major_brand = buf->read_4bytes();
    minor_version = buf->read_4bytes();
    
    // Compatible brands to the end of the box.
    int left = left_space(buf);
    
    if (left > 0) {
        nb_compatible_brands = left / 4;
        compatible_brands = new uint32_t[nb_compatible_brands];
    }
    
    for (int i = 0; left > 0; i++, left -= 4){
        uint32_t cb = buf->read_4bytes();
        compatible_brands[i] = cb;
    }
    
    return ret;
}

SrsMp4MediaDataBox::SrsMp4MediaDataBox()
{
    type = SRS_MP4_BOX_MDAT;
    data = NULL;
    nb_data = 0;
}

SrsMp4MediaDataBox::~SrsMp4MediaDataBox()
{
    srs_freepa(data);
}

int SrsMp4MediaDataBox::nb_header()
{
    return SrsMp4Box::nb_header() + nb_data;
}

int SrsMp4MediaDataBox::encode_header(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = SrsMp4Box::encode_header(buf)) != ERROR_SUCCESS) {
        return ret;
    }
    
    if (nb_data) {
        buf->write_bytes((char*)data, nb_data);
    }
    
    return ret;
}

int SrsMp4MediaDataBox::decode_header(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = SrsMp4Box::decode_header(buf)) != ERROR_SUCCESS) {
        return ret;
    }
    
    int left = left_space(buf);
    if (left) {
        data = new uint8_t[left];
        buf->read_bytes((char*)data, left);
    }
    
    return ret;
}

SrsMp4FreeSpaceBox::SrsMp4FreeSpaceBox()
{
    type = SRS_MP4_BOX_FREE; // 'free' or 'skip'
    data = NULL;
    nb_data = 0;
}

SrsMp4FreeSpaceBox::~SrsMp4FreeSpaceBox()
{
    srs_freepa(data);
}

int SrsMp4FreeSpaceBox::nb_header()
{
    return SrsMp4Box::nb_header() + nb_data;
}

int SrsMp4FreeSpaceBox::encode_header(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = SrsMp4Box::encode_header(buf)) != ERROR_SUCCESS) {
        return ret;
    }
    
    if (nb_data) {
        buf->write_bytes((char*)data, nb_data);
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
        data = new uint8_t[left];
        buf->read_bytes((char*)data, left);
    }
    
    return ret;
}

SrsMp4MovieBox::SrsMp4MovieBox()
{
    type = SRS_MP4_BOX_MOOV;
}

SrsMp4MovieBox::~SrsMp4MovieBox()
{
}

SrsMp4MovieHeaderBox::SrsMp4MovieHeaderBox()
{
    type = SRS_MP4_BOX_MVHD;
    
    rate = 0x00010000; // typically 1.0
    volume = 0x0100; // typically, full volume
    reserved0 = 0;
    reserved1 = 0;
    
    int32_t v[] = {0x00010000, 0, 0, 0, 0x00010000, 0, 0, 0, 0x40000000};
    memcpy(matrix, v, 36);
    
    memset(pre_defined, 0, 24);
}

SrsMp4MovieHeaderBox::~SrsMp4MovieHeaderBox()
{
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
        buf->write_8bytes(duration);
    } else {
        buf->write_4bytes((uint32_t)creation_time);
        buf->write_4bytes((uint32_t)modification_time);
        buf->write_4bytes(timescale);
        buf->write_4bytes((uint32_t)duration);
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
        duration = buf->read_8bytes();
    } else {
        creation_time = buf->read_4bytes();
        modification_time = buf->read_4bytes();
        timescale = buf->read_4bytes();
        duration = buf->read_4bytes();
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

SrsMp4TrackBox::SrsMp4TrackBox()
{
    type = SRS_MP4_BOX_TRAK;
}

SrsMp4TrackBox::~SrsMp4TrackBox()
{
}

SrsMp4TrackHeaderBox::SrsMp4TrackHeaderBox()
{
    type = SRS_MP4_BOX_TKHD;
    
    reserved0 = 0;
    reserved1 = 0;
    reserved2 = 0;
    layer = alternate_group = 0;
    volume = 0; // if track_is_audio 0x0100 else 0
    
    int32_t v[] = {0x00010000, 0, 0, 0, 0x00010000, 0, 0, 0, 0x40000000};
    memcpy(matrix, v, 36);
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

SrsMp4EditBox::SrsMp4EditBox()
{
    type = SRS_MP4_BOX_EDTS;
}

SrsMp4EditBox::~SrsMp4EditBox()
{
}

SrsMp4ElstEntry::SrsMp4ElstEntry()
{
    media_rate_fraction = 0;
}

SrsMp4EditListBox::SrsMp4EditListBox()
{
    type = SRS_MP4_BOX_ELST;
    
    entry_count = 0;
    entries = NULL;
}

SrsMp4EditListBox::~SrsMp4EditListBox()
{
    srs_freepa(entries);
}

int SrsMp4EditListBox::nb_header()
{
    int size = SrsMp4FullBox::nb_header() + 4;
    
    if (version == 1) {
        size += entry_count * (2+2+8+8);
    } else {
        size += entry_count * (2+2+4+4);
    }
    
    return size;
}

int SrsMp4EditListBox::encode_header(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = SrsMp4FullBox::encode_header(buf)) != ERROR_SUCCESS) {
        return ret;
    }
    
    buf->write_4bytes(entry_count);
    for (uint32_t i = 0; i < entry_count; i++) {
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
    
    entry_count = buf->read_4bytes();
    if (entry_count > 0) {
        entries = new SrsMp4ElstEntry[entry_count];
    }
    for (int i = 0; i < entry_count; i++) {
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

SrsMp4MediaBox::SrsMp4MediaBox()
{
    type = SRS_MP4_BOX_MDIA;
}

SrsMp4MediaBox::~SrsMp4MediaBox()
{
}

SrsMp4MediaHeaderBox::SrsMp4MediaHeaderBox()
{
    type = SRS_MP4_BOX_MDHD;
    language = 0;
    pre_defined = 0;
}

SrsMp4MediaHeaderBox::~SrsMp4MediaHeaderBox()
{
}

uint8_t SrsMp4MediaHeaderBox::language0()
{
    return (language >> 10) & 0x1f;
}

void SrsMp4MediaHeaderBox::set_language0(uint8_t v)
{
    language |= uint16_t(v & 0x1f) << 10;
}

uint8_t SrsMp4MediaHeaderBox::language1()
{
    return (language >> 5) & 0x1f;
}

void SrsMp4MediaHeaderBox::set_language1(uint8_t v)
{
    language |= uint16_t(v & 0x1f) << 5;
}

uint8_t SrsMp4MediaHeaderBox::language2()
{
    return language & 0x1f;
}

void SrsMp4MediaHeaderBox::set_language2(uint8_t v)
{
    language |= uint16_t(v & 0x1f);
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

SrsMp4HandlerReferenceBox::SrsMp4HandlerReferenceBox()
{
    type = SRS_MP4_BOX_HDLR;
    
    pre_defined = 0;
    memset(reserved, 0, 12);
}

SrsMp4HandlerReferenceBox::~SrsMp4HandlerReferenceBox()
{
}

bool SrsMp4HandlerReferenceBox::is_video()
{
    return handler_type == SRS_MP4_HANDLER_VIDE;
}

bool SrsMp4HandlerReferenceBox::is_audio()
{
    return handler_type == SRS_MP4_HANDLER_SOUN;
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
    handler_type = buf->read_4bytes();
    buf->skip(12);
    
    if ((ret = srs_mp4_string_read(buf, name, left_space(buf))) != ERROR_SUCCESS) {
        srs_error("MP4 hdlr read string failed. ret=%d", ret);
        return ret;
    }
    
    return ret;
}

SrsMp4MediaInformationBox::SrsMp4MediaInformationBox()
{
    type = SRS_MP4_BOX_MINF;
}

SrsMp4MediaInformationBox::~SrsMp4MediaInformationBox()
{
}

SrsMp4VideoMeidaHeaderBox::SrsMp4VideoMeidaHeaderBox()
{
    type = SRS_MP4_BOX_VMHD;
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
    type = SRS_MP4_BOX_SMHD;
    
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
    type = SRS_MP4_BOX_DINF;
}

SrsMp4DataInformationBox::~SrsMp4DataInformationBox()
{
}

SrsMp4DataEntryBox::SrsMp4DataEntryBox()
{
}

SrsMp4DataEntryBox::~SrsMp4DataEntryBox()
{
}

SrsMp4DataEntryUrlBox::SrsMp4DataEntryUrlBox()
{
    type = SRS_MP4_BOX_URL;
}

SrsMp4DataEntryUrlBox::~SrsMp4DataEntryUrlBox()
{
}

int SrsMp4DataEntryUrlBox::nb_header()
{
    // a 24-bit integer with flags; one flag is defined (x000001) which means that the media
    // data is in the same file as the Movie Box containing this data reference.
    if (flags == 1) {
        return SrsMp4FullBox::nb_header();
    }
    return SrsMp4FullBox::nb_header()+srs_mp4_string_length(location);
}

int SrsMp4DataEntryUrlBox::encode_header(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = SrsMp4FullBox::encode_header(buf)) != ERROR_SUCCESS) {
        return ret;
    }
    
    // a 24-bit integer with flags; one flag is defined (x000001) which means that the media
    // data is in the same file as the Movie Box containing this data reference.
    if (location.empty()) {
        flags = 0x01;
        return ret;
    }
    
    srs_mp4_string_write(buf, location);
    
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

SrsMp4DataEntryUrnBox::SrsMp4DataEntryUrnBox()
{
    type = SRS_MP4_BOX_URN;
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

SrsMp4DataReferenceBox::SrsMp4DataReferenceBox()
{
    type = SRS_MP4_BOX_DREF;
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
        
        if (box->type == SRS_MP4_BOX_URL) {
            entries.push_back(dynamic_cast<SrsMp4DataEntryUrlBox*>(box));
        } else if (box->type == SRS_MP4_BOX_URN) {
            entries.push_back(dynamic_cast<SrsMp4DataEntryUrnBox*>(box));
        } else {
            srs_freep(box);
        }
    }
    
    return ret;
}

SrsMp4SampleTableBox::SrsMp4SampleTableBox()
{
    type = SRS_MP4_BOX_STBL;
}

SrsMp4SampleTableBox::~SrsMp4SampleTableBox()
{
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

SrsMp4VisualSampleEntry::SrsMp4VisualSampleEntry()
{
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

SrsMp4AvccBox::SrsMp4AvccBox()
{
    type = SRS_MP4_BOX_AVCC;
    nb_config = 0;
    avc_config = NULL;
}

SrsMp4AvccBox::~SrsMp4AvccBox()
{
    srs_freepa(avc_config);
}

int SrsMp4AvccBox::nb_header()
{
    return SrsMp4Box::nb_header()+nb_config;
}

int SrsMp4AvccBox::encode_header(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = SrsMp4Box::encode_header(buf)) != ERROR_SUCCESS) {
        return ret;
    }
    
    if (nb_config) {
        buf->write_bytes((char*)avc_config, nb_config);
    }
    
    return ret;
}

int SrsMp4AvccBox::decode_header(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = SrsMp4Box::decode_header(buf)) != ERROR_SUCCESS) {
        return ret;
    }
    
    nb_config = left_space(buf);
    if (nb_config) {
        avc_config = new uint8_t[nb_config];
        buf->read_bytes((char*)avc_config, nb_config);
    }
    
    return ret;
}

SrsMp4AudioSampleEntry::SrsMp4AudioSampleEntry()
{
    reserved0 = 0;
    pre_defined0 = 0;
    reserved1 = 0;
    channelcount = 2;
    samplesize = 16;
}

SrsMp4AudioSampleEntry::~SrsMp4AudioSampleEntry()
{
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

SrsMp4SampleDescriptionBox::SrsMp4SampleDescriptionBox()
{
    type = SRS_MP4_BOX_STSD;
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

uint32_t SrsMp4SampleDescriptionBox::entry_count()
{
    return (uint32_t)entries.size();
}

SrsMp4SampleEntry* SrsMp4SampleDescriptionBox::entrie_at(int index)
{
    return entries.at(index);
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

SrsMp4SttsEntry::SrsMp4SttsEntry()
{
    sample_count = 0;
    sample_delta = 0;
}

SrsMp4DecodingTime2SampleBox::SrsMp4DecodingTime2SampleBox()
{
    type = SRS_MP4_BOX_STTS;
    
    entry_count = 0;
    entries = NULL;
}

SrsMp4DecodingTime2SampleBox::~SrsMp4DecodingTime2SampleBox()
{
    srs_freepa(entries);
}

int SrsMp4DecodingTime2SampleBox::nb_header()
{
    return SrsMp4FullBox::nb_header() + 4 + 8*entry_count;
}

int SrsMp4DecodingTime2SampleBox::encode_header(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = SrsMp4FullBox::encode_header(buf)) != ERROR_SUCCESS) {
        return ret;
    }
    
    buf->write_4bytes(entry_count);
    for (uint32_t i = 0; i < entry_count; i++) {
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
    
    entry_count = buf->read_4bytes();
    if (entry_count) {
        entries = new SrsMp4SttsEntry[entry_count];
    }
    for (uint32_t i = 0; i < entry_count; i++) {
        SrsMp4SttsEntry& entry = entries[i];
        entry.sample_count = buf->read_4bytes();
        entry.sample_delta = buf->read_4bytes();
    }
    
    return ret;
}

SrsMp4CttsEntry::SrsMp4CttsEntry()
{
    sample_count = 0;
    sample_offset = 0;
}

SrsMp4CompositionTime2SampleBox::SrsMp4CompositionTime2SampleBox()
{
    type = SRS_MP4_BOX_CTTS;
    
    entry_count = 0;
    entries = NULL;
}

SrsMp4CompositionTime2SampleBox::~SrsMp4CompositionTime2SampleBox()
{
    srs_freepa(entries);
}

int SrsMp4CompositionTime2SampleBox::nb_header()
{
    return SrsMp4FullBox::nb_header() + 4 + 8*entry_count;
}

int SrsMp4CompositionTime2SampleBox::encode_header(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = SrsMp4FullBox::encode_header(buf)) != ERROR_SUCCESS) {
        return ret;
    }
    
    buf->write_4bytes(entry_count);
    for (uint32_t i = 0; i < entry_count; i++) {
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
    
    entry_count = buf->read_4bytes();
    if (entry_count) {
        entries = new SrsMp4CttsEntry[entry_count];
    }
    for (uint32_t i = 0; i < entry_count; i++) {
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

SrsMp4SyncSampleBox::SrsMp4SyncSampleBox()
{
    type = SRS_MP4_BOX_STSS;
    
    entry_count = 0;
    sample_numbers = NULL;
}

SrsMp4SyncSampleBox::~SrsMp4SyncSampleBox()
{
    srs_freepa(sample_numbers);
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

SrsMp4StscEntry::SrsMp4StscEntry()
{
    first_chunk = 0;
    samples_per_chunk = 0;
    sample_description_index = 0;
}

SrsMp4Sample2ChunkBox::SrsMp4Sample2ChunkBox()
{
    type = SRS_MP4_BOX_STSC;
    
    entry_count = 0;
    entries = NULL;
}

SrsMp4Sample2ChunkBox::~SrsMp4Sample2ChunkBox()
{
    srs_freepa(entries);
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

SrsMp4ChunkOffsetBox::SrsMp4ChunkOffsetBox()
{
    type = SRS_MP4_BOX_STCO;
    
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

SrsMp4ChunkLargeOffsetBox::SrsMp4ChunkLargeOffsetBox()
{
    type = SRS_MP4_BOX_CO64;
    
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

SrsMp4SampleSizeBox::SrsMp4SampleSizeBox()
{
    type = SRS_MP4_BOX_STSZ;
    
    sample_size = sample_count = 0;
    entry_sizes = NULL;
}

SrsMp4SampleSizeBox::~SrsMp4SampleSizeBox()
{
    srs_freepa(entry_sizes);
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
    if (sample_size == 0) {
        entry_sizes = new uint32_t[sample_count];
    }
    for (uint32_t i = 0; i < sample_count && sample_size == 0; i++) {
        entry_sizes[i] = buf->read_4bytes();
    }
    
    return ret;
}

#define SRS_MP4_BUF_SIZE 4096

SrsMp4Decoder::SrsMp4Decoder()
{
    reader = NULL;
    buf = new char[SRS_MP4_BUF_SIZE];
    stream = new SrsSimpleStream();
}

SrsMp4Decoder::~SrsMp4Decoder()
{
    srs_freepa(buf);
    srs_freep(stream);
}

int SrsMp4Decoder::initialize(ISrsReader* r)
{
    int ret = ERROR_SUCCESS;
    
    srs_assert(r);
    reader = r;
    
    // File Type Box (ftyp)
    if (true) {
        SrsMp4Box* box = NULL;
        SrsAutoFree(SrsMp4Box, box);
        
        if ((ret = load_next_box(&box, SRS_MP4_BOX_FTYP)) != ERROR_SUCCESS) {
            return ret;
        }
        SrsMp4FileTypeBox* ftyp = dynamic_cast<SrsMp4FileTypeBox*>(box);
        
        bool legal_brand = false;
        static uint32_t legal_brands[] = {
            SRS_MP4_BRAND_ISOM, SRS_MP4_BRAND_ISO2, SRS_MP4_BRAND_AVC1, SRS_MP4_BRAND_MP41
        };
        for (int i = 0; i < sizeof(legal_brands)/sizeof(uint32_t); i++) {
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
    }
    
    // Media Data Box (mdat) or Movie Box (moov)
    SrsMp4Box* box = NULL;
    SrsAutoFree(SrsMp4Box, box);
    while (true) {
        if ((ret = load_next_box(&box, 0)) != ERROR_SUCCESS) {
            return ret;
        }
        
        if (!box->is_mdat() && !box->is_moov()) {
            srs_freep(box);
            continue;
        }
        break;
    }
    
    // Only support non-seek mp4, that is, mdat should never before moov.
    // @see https://github.com/ossrs/srs/issues/738#issuecomment-276343669
    if (box->is_mdat()) {
        ret = ERROR_MP4_NOT_NON_SEEKABLE;
        srs_error("MP4 is not non-seekable. ret=%d", ret);
        return ret;
    }
    
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
    
    SrsMp4Box* box = NULL;
    while (true) {
        uint64_t required = box? box->sz():4;
        while (stream->length() < required) {
            ssize_t nread;
            if ((ret = reader->read(buf, SRS_MP4_BUF_SIZE, &nread)) != ERROR_SUCCESS) {
                srs_error("MP4 load failed, nread=%d, required=%d. ret=%d", nread, required, ret);
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
        
        // Decode util we can demux the whole box.
        if (!buffer->require((int)box->sz())) {
            continue;
        }
        
        if (!required_box_type || box->type == required_box_type) {
            ret = box->decode(buffer);
        }
        
        // Remove the consumed bytes.
        stream->erase((int)box->sz());
        
        if (ret != ERROR_SUCCESS) {
            srs_freep(box);
        } else {
            *ppbox = box;
        }
        
        break;
    }
    
    return ret;
}

