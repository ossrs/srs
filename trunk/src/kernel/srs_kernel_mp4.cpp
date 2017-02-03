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
#include <sstream>
using namespace std;

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
        srs_error("MP4 discovery overflow 31bits, size=%"PRId64". ret=%d", largesize, ret);
        return ret;
    }
    
    SrsMp4Box* box = NULL;
    switch(type) {
        case SrsMp4BoxTypeFTYP: box = new SrsMp4FileTypeBox(); break;
        case SrsMp4BoxTypeMDAT: box = new SrsMp4MediaDataBox(); break;
        case SrsMp4BoxTypeFREE: case SrsMp4BoxTypeSKIP: box = new SrsMp4FreeSpaceBox(); break;
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
    
    if (type == SrsMp4BoxTypeUUID) {
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
    type = (SrsMp4BoxType)buf->read_4bytes();
    
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
    
    if (type == SrsMp4BoxTypeUUID) {
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
    type = SrsMp4BoxTypeFTYP;
    nb_compatible_brands = 0;
    compatible_brands = NULL;
    major_brand = SrsMp4BoxBrandForbidden;
    minor_version = 0;
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
        nb_compatible_brands = left / 4;
        compatible_brands = new SrsMp4BoxBrand[nb_compatible_brands];
    }
    
    for (int i = 0; left > 0; i++, left -= 4){
        compatible_brands[i] = (SrsMp4BoxBrand)buf->read_4bytes();
    }
    
    return ret;
}

SrsMp4MediaDataBox::SrsMp4MediaDataBox()
{
    type = SrsMp4BoxTypeMDAT;
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
    type = SrsMp4BoxTypeFREE; // 'free' or 'skip'
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

SrsMp4TrackBox* SrsMp4MovieBox::video()
{
    for (int i = 0; i < boxes.size(); i++) {
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
    for (int i = 0; i < boxes.size(); i++) {
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

int SrsMp4MovieBox::nb_vide_tracks()
{
    int nb_tracks = 0;
    
    for (int i = 0; i < boxes.size(); i++) {
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
    
    for (int i = 0; i < boxes.size(); i++) {
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

SrsMp4TrackBox::SrsMp4TrackBox()
{
    type = SrsMp4BoxTypeTRAK;
}

SrsMp4TrackBox::~SrsMp4TrackBox()
{
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
}

SrsMp4TrackHeaderBox::~SrsMp4TrackHeaderBox()
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

SrsCodecVideo SrsMp4TrackBox::vide_codec()
{
    SrsMp4SampleDescriptionBox* box = stsd();
    if (!box) {
        return SrsCodecVideoForbidden;
    }
    
    if (box->entry_count() == 0) {
        return SrsCodecVideoForbidden;
    }
    
    SrsMp4SampleEntry* entry = box->entrie_at(0);
    switch(entry->type) {
        case SrsMp4BoxTypeAVC1: return SrsCodecVideoAVC;
        default: return SrsCodecVideoForbidden;
    }
}

SrsCodecAudio SrsMp4TrackBox::soun_codec()
{
    SrsMp4SampleDescriptionBox* box = stsd();
    if (!box) {
        return SrsCodecAudioForbidden;
    }
    
    if (box->entry_count() == 0) {
        return SrsCodecAudioForbidden;
    }
    
    SrsMp4SampleEntry* entry = box->entrie_at(0);
    switch(entry->type) {
        case SrsMp4BoxTypeMP4A: return SrsCodecAudioAAC;
        default: return SrsCodecAudioForbidden;
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
    type = SrsMp4BoxTypeEDTS;
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
    type = SrsMp4BoxTypeELST;
    
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

SrsMp4MediaInformationBox* SrsMp4MediaBox::minf()
{
    SrsMp4Box* box = get(SrsMp4BoxTypeMINF);
    return dynamic_cast<SrsMp4MediaInformationBox*>(box);
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

SrsMp4MediaInformationBox::SrsMp4MediaInformationBox()
{
    type = SrsMp4BoxTypeMINF;
}

SrsMp4MediaInformationBox::~SrsMp4MediaInformationBox()
{
}

SrsMp4SampleTableBox* SrsMp4MediaInformationBox::stbl()
{
    SrsMp4Box* box = get(SrsMp4BoxTypeSTBL);
    return dynamic_cast<SrsMp4SampleTableBox*>(box);
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

SrsMp4AvccBox* SrsMp4VisualSampleEntry::avcC()
{
    SrsMp4Box* box = get(SrsMp4BoxTypeAVCC);
    return dynamic_cast<SrsMp4AvccBox*>(box);
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
    type = SrsMp4BoxTypeAVCC;
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

SrsMp4EsdsBox* SrsMp4AudioSampleEntry::esds()
{
    SrsMp4Box* box = get(SrsMp4BoxTypeESDS);
    return dynamic_cast<SrsMp4EsdsBox*>(box);
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

SrsMp4DecoderSpecificInfo::SrsMp4DecoderSpecificInfo()
{
    tag = SrsMp4ESTagESDecSpecificInfoTag;
    nb_asc = 0;
    asc = NULL;
}

SrsMp4DecoderSpecificInfo::~SrsMp4DecoderSpecificInfo()
{
    srs_freepa(asc);
}

int32_t SrsMp4DecoderSpecificInfo::nb_payload()
{
    return nb_asc;
}

int SrsMp4DecoderSpecificInfo::encode_payload(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    if (nb_asc) {
        buf->write_bytes((char*)asc, nb_asc);
    }
    
    return ret;
}

int SrsMp4DecoderSpecificInfo::decode_payload(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    nb_asc = vlen;
    if (nb_asc) {
        asc = new uint8_t[nb_asc];
        buf->read_bytes((char*)asc, nb_asc);
    }
    
    return ret;
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
    return 12 + (decSpecificInfo? decSpecificInfo->nb_bytes():0);
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
    URLlength = 0;
    URLstring = NULL;
}

SrsMp4ES_Descriptor::~SrsMp4ES_Descriptor()
{
    srs_freepa(URLstring);
}

int32_t SrsMp4ES_Descriptor::nb_payload()
{
    int size = 2 +1;
    size += streamDependenceFlag? 2:0;
    if (URL_Flag) {
        size += 1 + URLlength;
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
    
    if (URL_Flag && URLlength) {
        buf->write_1bytes(URLlength);
        buf->write_bytes((char*)URLstring, URLlength);
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
        URLlength = buf->read_1bytes();
        
        if (!buf->require(URLlength)) {
            ret = ERROR_MP4_BOX_REQUIRE_SPACE;
            srs_error("MP4 ES requires %d bytes space. ret=%d", URLlength, ret);
            return ret;
        }
        URLstring = new uint8_t[URLlength];
        buf->read_bytes((char*)URLstring, URLlength);
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
    type = SrsMp4BoxTypeSTTS;
    
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
    type = SrsMp4BoxTypeCTTS;
    
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
    type = SrsMp4BoxTypeSTSS;
    
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
    type = SrsMp4BoxTypeSTSC;
    
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

SrsMp4UserDataBox::SrsMp4UserDataBox()
{
    type = SrsMp4BoxTypeUDTA;
    nb_data = 0;
    data = NULL;
}

SrsMp4UserDataBox::~SrsMp4UserDataBox()
{
    srs_freepa(data);
}

int SrsMp4UserDataBox::nb_header()
{
    return SrsMp4Box::nb_header()+nb_data;
}

int SrsMp4UserDataBox::encode_header(SrsBuffer* buf)
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

int SrsMp4UserDataBox::decode_header(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = SrsMp4Box::decode_header(buf)) != ERROR_SUCCESS) {
        return ret;
    }
    
    nb_data = left_space(buf);
    if (nb_data) {
        data = new uint8_t[nb_data];
        buf->read_bytes((char*)data, nb_data);
    }
    
    return ret;
}

#define SRS_MP4_BUF_SIZE 4096

SrsMp4Decoder::SrsMp4Decoder()
{
    rsio = NULL;
    brand = SrsMp4BoxBrandForbidden;
    buf = new char[SRS_MP4_BUF_SIZE];
    stream = new SrsSimpleStream();
}

SrsMp4Decoder::~SrsMp4Decoder()
{
    srs_freepa(buf);
    srs_freep(stream);
}

int SrsMp4Decoder::initialize(ISrsReadSeeker* rs)
{
    int ret = ERROR_SUCCESS;
    
    srs_assert(rs);
    rsio = rs;
    
    // For mdat before moov, we must reset the io.
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
    
    // Reset the io to the start to reparse the general MP4.
    if (offset >= 0) {
        return rsio->lseek(offset, SEEK_SET, NULL);
    }
    
    return ret;
}

int SrsMp4Decoder::parse_ftyp(SrsMp4FileTypeBox* ftyp)
{
    int ret = ERROR_SUCCESS;
    
    // File Type Box (ftyp)
    bool legal_brand = false;
    static SrsMp4BoxBrand legal_brands[] = {
        SrsMp4BoxBrandISOM, SrsMp4BoxBrandISO2, SrsMp4BoxBrandAVC1, SrsMp4BoxBrandMP41
    };
    for (int i = 0; i < sizeof(legal_brands)/sizeof(SrsMp4BoxBrand); i++) {
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
    
    stringstream ss;
    ss << "dur=" << mvhd->duration() << "ms";
    // video codec.
    ss << ", vide=" << moov->nb_vide_tracks() << "("
        << srs_codec_video2str(vide?vide->vide_codec():SrsCodecVideoForbidden)
        << "," << (avcc? avcc->nb_config:0) << "BSH" << ")";
    // audio codec.
    ss << ", soun=" << moov->nb_soun_tracks() << "("
        << srs_codec_audio2str(soun?soun->soun_codec():SrsCodecAudioForbidden)
        << "," << (asc? asc->nb_asc:0) << "BSH" << ")";
    
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
    
    SrsMp4Box* box = NULL;
    while (true) {
        uint64_t required = box? box->sz():4;
        while (stream->length() < required) {
            ssize_t nread;
            if ((ret = rsio->read(buf, SRS_MP4_BUF_SIZE, &nread)) != ERROR_SUCCESS) {
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
        
        // For mdat, skip the content.
        if (box->is_mdat()) {
            // Never load the mdat box content, instead we skip it, for it's too large.
            // The demuxer use seeker to read sample one by one.
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
            }
        } else {
            // Util we can demux the whole box.
            if (!buffer->require((int)box->sz())) {
                continue;
            }
            
            // Decode the matched box or any box is matched.
            if (!required_box_type || box->type == required_box_type) {
                ret = box->decode(buffer);
            }
        
            // Remove the consumed bytes.
            stream->erase((int)box->sz());
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

