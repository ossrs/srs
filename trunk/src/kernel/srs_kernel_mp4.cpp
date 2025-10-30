//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_kernel_mp4.hpp>

#include <srs_core_autofree.hpp>
#include <srs_core_deprecated.hpp>
#include <srs_kernel_buffer.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_io.hpp>
#include <srs_kernel_log.hpp>
#include <srs_kernel_packet.hpp>
#include <srs_kernel_stream.hpp>
#include <srs_kernel_utility.hpp>

#include <iomanip>
#include <openssl/aes.h>
#include <sstream>
#include <string.h>
using namespace std;

// For CentOS 6 or C++98, @see https://github.com/ossrs/srs/issues/2815
#ifndef UINT32_MAX
#define UINT32_MAX (4294967295U)
#endif

#define SRS_MP4_EOF_SIZE 0
#define SRS_MP4_USE_LARGE_SIZE 1

#define SRS_MP4_BUF_SIZE 4096

srs_error_t srs_mp4_write_box(ISrsWriter *writer, ISrsCodec *box)
{
    srs_error_t err = srs_success;

    int nb_data = box->nb_bytes();
    std::vector<char> data(nb_data);

    SrsUniquePtr<SrsBuffer> buffer(new SrsBuffer(&data[0], nb_data));
    if ((err = box->encode(buffer.get())) != srs_success) {
        return srs_error_wrap(err, "encode box");
    }

    if ((err = writer->write(&data[0], nb_data, NULL)) != srs_success) {
        return srs_error_wrap(err, "write box");
    }

    return err;
}

stringstream &srs_mp4_padding(stringstream &ss, SrsMp4DumpContext dc, int tab)
{
    for (int i = 0; i < (int)dc.level_; i++) {
        for (int j = 0; j < tab; j++) {
            ss << " ";
        }
    }
    return ss;
}

stringstream &srs_print_mp4_type(stringstream &ss, uint32_t v)
{
    ss << char(v >> 24) << char(v >> 16) << char(v >> 8) << char(v);
    return ss;
}

stringstream &srs_mp4_print_bytes(stringstream &ss, const char *p, int size, SrsMp4DumpContext dc, int line, int max)
{
    int limit = srs_min((max < 0 ? size : max), size);

    for (int i = 0; i < (int)limit; i += line) {
        int nn_line_elems = srs_min(line, limit - i);
        srs_dumps_array(p + i, nn_line_elems, ss, dc, srs_mp4_pfn_hex, srs_mp4_delimiter_inspace);

        if (i + line < limit) {
            ss << "," << endl;
            srs_mp4_padding(ss, dc);
        }
    }
    return ss;
}

void srs_mp4_delimiter_inline(stringstream &ss, SrsMp4DumpContext dc)
{
    ss << ",";
}

void srs_mp4_delimiter_inspace(stringstream &ss, SrsMp4DumpContext dc)
{
    ss << ", ";
}

void srs_mp4_delimiter_newline(stringstream &ss, SrsMp4DumpContext dc)
{
    ss << endl;
    srs_mp4_padding(ss, dc);
}

int srs_mp4_string_length(string v)
{
    return (int)v.length() + 1;
}

void srs_mp4_string_write(SrsBuffer *buf, string v)
{
    if (!v.empty()) {
        buf->write_bytes((char *)v.data(), (int)v.length());
    }
    buf->write_1bytes(0x00);
}

srs_error_t srs_mp4_string_read(SrsBuffer *buf, string &v, int left)
{
    srs_error_t err = srs_success;

    if (left == 0) {
        return err;
    }

    char *start = buf->data() + buf->pos();
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
    level_ = 0;
    summary_ = false;
}

SrsMp4DumpContext::~SrsMp4DumpContext()
{
}

SrsMp4DumpContext SrsMp4DumpContext::indent()
{
    SrsMp4DumpContext ctx = *this;
    ctx.level_++;
    return ctx;
}

SrsMp4Box::SrsMp4Box()
{
    smallsize_ = 0;
    largesize_ = 0;
    start_pos_ = 0;
    type_ = SrsMp4BoxTypeForbidden;
}

SrsMp4Box::~SrsMp4Box()
{
    vector<SrsMp4Box *>::iterator it;
    for (it = boxes_.begin(); it != boxes_.end(); ++it) {
        SrsMp4Box *box = *it;
        srs_freep(box);
    }
    boxes_.clear();
}

uint64_t SrsMp4Box::sz()
{
    return smallsize_ == SRS_MP4_USE_LARGE_SIZE ? largesize_ : smallsize_;
}

int SrsMp4Box::sz_header()
{
    return nb_header();
}

uint64_t SrsMp4Box::update_size()
{
    uint64_t size = nb_bytes();

    if (size > 0xffffffff) {
        largesize_ = size;
        smallsize_ = SRS_MP4_USE_LARGE_SIZE;
    } else {
        smallsize_ = (uint32_t)size;
    }

    return size;
}

int SrsMp4Box::left_space(SrsBuffer *buf)
{
    int left = (int)sz() - (buf->pos() - start_pos_);
    return srs_max(0, left);
}

bool SrsMp4Box::is_ftyp()
{
    return type_ == SrsMp4BoxTypeFTYP;
}

bool SrsMp4Box::is_moov()
{
    return type_ == SrsMp4BoxTypeMOOV;
}

bool SrsMp4Box::is_mdat()
{
    return type_ == SrsMp4BoxTypeMDAT;
}

SrsMp4Box *SrsMp4Box::get(SrsMp4BoxType bt)
{
    vector<SrsMp4Box *>::iterator it;
    for (it = boxes_.begin(); it != boxes_.end(); ++it) {
        SrsMp4Box *box = *it;

        if (box->type_ == bt) {
            return box;
        }
    }

    return NULL;
}

int SrsMp4Box::remove(SrsMp4BoxType bt)
{
    int nb_removed = 0;

    vector<SrsMp4Box *>::iterator it;
    for (it = boxes_.begin(); it != boxes_.end();) {
        SrsMp4Box *box = *it;

        if (box->type_ == bt) {
            it = boxes_.erase(it);
            srs_freep(box);
        } else {
            ++it;
        }
    }

    return nb_removed;
}

void SrsMp4Box::append(SrsMp4Box *box)
{
    boxes_.push_back(box);
}

// LCOV_EXCL_START
stringstream &SrsMp4Box::dumps(stringstream &ss, SrsMp4DumpContext dc)
{
    srs_mp4_padding(ss, dc);
    srs_print_mp4_type(ss, (uint32_t)type_);

    ss << ", " << sz();
    if (smallsize_ == SRS_MP4_USE_LARGE_SIZE) {
        ss << "(large)";
    }
    ss << "B";

    dumps_detail(ss, dc);

    if (!boxes_.empty()) {
        ss << ", " << boxes_.size() << " boxes";
    }

    // If there contained boxes in header,
    // which means the last box has already output the endl.
    if (!boxes_in_header()) {
        ss << endl;
    }

    vector<SrsMp4Box *>::iterator it;
    for (it = boxes_.begin(); it != boxes_.end(); ++it) {
        SrsMp4Box *box = *it;
        box->dumps(ss, dc.indent());
    }

    return ss;
}
// LCOV_EXCL_STOP

srs_error_t SrsMp4Box::discovery(SrsBuffer *buf, SrsMp4Box **ppbox)
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
    }

    // Reset the buffer, because we only peek it.
    buf->skip(-8);
    if (smallsize == SRS_MP4_USE_LARGE_SIZE) {
        buf->skip(-8);
    }

    SrsMp4Box *box = NULL;
    switch (type) {
    case SrsMp4BoxTypeFTYP:
        box = new SrsMp4FileTypeBox();
        break;
    case SrsMp4BoxTypeMDAT:
        box = new SrsMp4MediaDataBox();
        break;
    case SrsMp4BoxTypeMOOV:
        box = new SrsMp4MovieBox();
        break;
    case SrsMp4BoxTypeMVHD:
        box = new SrsMp4MovieHeaderBox();
        break;
    case SrsMp4BoxTypeTRAK:
        box = new SrsMp4TrackBox();
        break;
    case SrsMp4BoxTypeTKHD:
        box = new SrsMp4TrackHeaderBox();
        break;
    case SrsMp4BoxTypeEDTS:
        box = new SrsMp4EditBox();
        break;
    case SrsMp4BoxTypeELST:
        box = new SrsMp4EditListBox();
        break;
    case SrsMp4BoxTypeMDIA:
        box = new SrsMp4MediaBox();
        break;
    case SrsMp4BoxTypeMDHD:
        box = new SrsMp4MediaHeaderBox();
        break;
    case SrsMp4BoxTypeHDLR:
        box = new SrsMp4HandlerReferenceBox();
        break;
    case SrsMp4BoxTypeMINF:
        box = new SrsMp4MediaInformationBox();
        break;
    case SrsMp4BoxTypeVMHD:
        box = new SrsMp4VideoMeidaHeaderBox();
        break;
    case SrsMp4BoxTypeSMHD:
        box = new SrsMp4SoundMeidaHeaderBox();
        break;
    case SrsMp4BoxTypeDINF:
        box = new SrsMp4DataInformationBox();
        break;
    case SrsMp4BoxTypeURL:
        box = new SrsMp4DataEntryUrlBox();
        break;
    case SrsMp4BoxTypeURN:
        box = new SrsMp4DataEntryUrnBox();
        break;
    case SrsMp4BoxTypeDREF:
        box = new SrsMp4DataReferenceBox();
        break;
    case SrsMp4BoxTypeSTBL:
        box = new SrsMp4SampleTableBox();
        break;
    case SrsMp4BoxTypeSTSD:
        box = new SrsMp4SampleDescriptionBox();
        break;
    case SrsMp4BoxTypeSTTS:
        box = new SrsMp4DecodingTime2SampleBox();
        break;
    case SrsMp4BoxTypeCTTS:
        box = new SrsMp4CompositionTime2SampleBox();
        break;
    case SrsMp4BoxTypeSTSS:
        box = new SrsMp4SyncSampleBox();
        break;
    case SrsMp4BoxTypeSTSC:
        box = new SrsMp4Sample2ChunkBox();
        break;
    case SrsMp4BoxTypeSTCO:
        box = new SrsMp4ChunkOffsetBox();
        break;
    case SrsMp4BoxTypeCO64:
        box = new SrsMp4ChunkLargeOffsetBox();
        break;
    case SrsMp4BoxTypeSTSZ:
        box = new SrsMp4SampleSizeBox();
        break;
    case SrsMp4BoxTypeAVC1:
        box = new SrsMp4VisualSampleEntry(SrsMp4BoxTypeAVC1);
        break;
    case SrsMp4BoxTypeHEV1:
        box = new SrsMp4VisualSampleEntry(SrsMp4BoxTypeHEV1);
        break;
    case SrsMp4BoxTypeAVCC:
        box = new SrsMp4AvccBox();
        break;
    case SrsMp4BoxTypeHVCC:
        box = new SrsMp4HvcCBox();
        break;
    case SrsMp4BoxTypeMP4A:
        box = new SrsMp4AudioSampleEntry();
        break;
    case SrsMp4BoxTypeESDS:
        box = new SrsMp4EsdsBox();
        break;
    case SrsMp4BoxTypeUDTA:
        box = new SrsMp4UserDataBox();
        break;
    case SrsMp4BoxTypeMVEX:
        box = new SrsMp4MovieExtendsBox();
        break;
    case SrsMp4BoxTypeTREX:
        box = new SrsMp4TrackExtendsBox();
        break;
    case SrsMp4BoxTypeSTYP:
        box = new SrsMp4SegmentTypeBox();
        break;
    case SrsMp4BoxTypeMOOF:
        box = new SrsMp4MovieFragmentBox();
        break;
    case SrsMp4BoxTypeMFHD:
        box = new SrsMp4MovieFragmentHeaderBox();
        break;
    case SrsMp4BoxTypeTRAF:
        box = new SrsMp4TrackFragmentBox();
        break;
    case SrsMp4BoxTypeTFHD:
        box = new SrsMp4TrackFragmentHeaderBox();
        break;
    case SrsMp4BoxTypeTFDT:
        box = new SrsMp4TrackFragmentDecodeTimeBox();
        break;
    case SrsMp4BoxTypeTRUN:
        box = new SrsMp4TrackFragmentRunBox();
        break;
    case SrsMp4BoxTypeSIDX:
        box = new SrsMp4SegmentIndexBox();
        break;
    // Skip some unknown boxes.
    case SrsMp4BoxTypeFREE:
    case SrsMp4BoxTypeSKIP:
    case SrsMp4BoxTypePASP:
    case SrsMp4BoxTypeUUID:
    default:
        box = new SrsMp4FreeSpaceBox(type);
        break;
    }

    if (box) {
        box->smallsize_ = smallsize;
        box->largesize_ = largesize;
        box->type_ = type;
        *ppbox = box;
    }

    return err;
}

uint64_t SrsMp4Box::nb_bytes()
{
    uint64_t sz = nb_header();

    vector<SrsMp4Box *>::iterator it;
    for (it = boxes_.begin(); it != boxes_.end(); ++it) {
        SrsMp4Box *box = *it;
        sz += box->nb_bytes();
    }

    return sz;
}

srs_error_t SrsMp4Box::encode(SrsBuffer *buf)
{
    srs_error_t err = srs_success;

    update_size();

    start_pos_ = buf->pos();

    if ((err = encode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "encode box header");
    }

    if ((err = encode_boxes(buf)) != srs_success) {
        return srs_error_wrap(err, "encode contained boxes");
    }

    return err;
}

srs_error_t SrsMp4Box::decode(SrsBuffer *buf)
{
    srs_error_t err = srs_success;

    start_pos_ = buf->pos();

    if ((err = decode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "decode box header");
    }

    if ((err = decode_boxes(buf)) != srs_success) {
        return srs_error_wrap(err, "decode contained boxes");
    }

    return err;
}

srs_error_t SrsMp4Box::encode_boxes(SrsBuffer *buf)
{
    srs_error_t err = srs_success;

    vector<SrsMp4Box *>::iterator it;
    for (it = boxes_.begin(); it != boxes_.end(); ++it) {
        SrsMp4Box *box = *it;
        if ((err = box->encode(buf)) != srs_success) {
            return srs_error_wrap(err, "encode contained box");
        }
    }

    return err;
}

srs_error_t SrsMp4Box::decode_boxes(SrsBuffer *buf)
{
    srs_error_t err = srs_success;

    int left = left_space(buf);
    while (left > 0) {
        SrsMp4Box *box = NULL;
        if ((err = discovery(buf, &box)) != srs_success) {
            return srs_error_wrap(err, "discovery contained box");
        }

        srs_assert(box);
        if ((err = box->decode(buf)) != srs_success) {
            srs_freep(box);
            return srs_error_wrap(err, "decode contained box");
        }

        boxes_.push_back(box);
        left -= box->sz();
    }

    return err;
}

int SrsMp4Box::nb_header()
{
    int size = 8;
    if (smallsize_ == SRS_MP4_USE_LARGE_SIZE) {
        size += 8;
    }

    if (type_ == SrsMp4BoxTypeUUID) {
        size += 16;
    }

    return size;
}

srs_error_t SrsMp4Box::encode_header(SrsBuffer *buf)
{
    srs_error_t err = srs_success;

    int size = SrsMp4Box::nb_header();
    if (!buf->require(size)) {
        return srs_error_new(ERROR_MP4_BOX_REQUIRE_SPACE, "requires %d only %d bytes", size, buf->left());
    }

    buf->write_4bytes(smallsize_);
    buf->write_4bytes(type_);
    if (smallsize_ == SRS_MP4_USE_LARGE_SIZE) {
        buf->write_8bytes(largesize_);
    }

    if (type_ == SrsMp4BoxTypeUUID) {
        buf->write_bytes(&usertype_[0], 16);
    }

    int lrsz = nb_header() - SrsMp4Box::nb_header();
    if (!buf->require(lrsz)) {
        return srs_error_new(ERROR_MP4_BOX_REQUIRE_SPACE, "box requires %d only %d bytes", lrsz, buf->left());
    }

    return err;
}

srs_error_t SrsMp4Box::decode_header(SrsBuffer *buf)
{
    srs_error_t err = srs_success;

    if (!buf->require(8)) {
        return srs_error_new(ERROR_MP4_BOX_REQUIRE_SPACE, "requires 8 only %d bytes", buf->left());
    }
    smallsize_ = (uint32_t)buf->read_4bytes();
    type_ = (SrsMp4BoxType)buf->read_4bytes();

    if (smallsize_ == SRS_MP4_EOF_SIZE) {
        srs_trace("MP4 box EOF.");
        return err;
    }

    if (smallsize_ == SRS_MP4_USE_LARGE_SIZE) {
        if (!buf->require(8)) {
            return srs_error_new(ERROR_MP4_BOX_REQUIRE_SPACE, "box requires 8 only %d bytes", buf->left());
        }
        largesize_ = (uint64_t)buf->read_8bytes();
    }

    // Only support 31bits size.
    if (sz() > 0x7fffffff) {
        return srs_error_new(ERROR_MP4_BOX_OVERFLOW, "box size overflow 31bits, size=%" PRId64, sz());
    }

    if (type_ == SrsMp4BoxTypeUUID) {
        if (!buf->require(16)) {
            return srs_error_new(ERROR_MP4_BOX_REQUIRE_SPACE, "box requires 16 only %d bytes", buf->left());
        }
        usertype_.resize(16);
        buf->read_bytes(&usertype_[0], 16);
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

stringstream &SrsMp4Box::dumps_detail(stringstream &ss, SrsMp4DumpContext dc)
{
    return ss;
}

SrsMp4FullBox::SrsMp4FullBox()
{
    version_ = 0;
    flags_ = 0;
}

SrsMp4FullBox::~SrsMp4FullBox()
{
}

int SrsMp4FullBox::nb_header()
{
    return SrsMp4Box::nb_header() + 1 + 3;
}

srs_error_t SrsMp4FullBox::encode_header(SrsBuffer *buf)
{
    srs_error_t err = srs_success;

    if ((err = SrsMp4Box::encode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "encode header");
    }

    if (!buf->require(4)) {
        return srs_error_new(ERROR_MP4_BOX_REQUIRE_SPACE, "full box requires 4 only %d bytes", buf->left());
    }

    buf->write_1bytes(version_);
    buf->write_3bytes(flags_);

    return err;
}

srs_error_t SrsMp4FullBox::decode_header(SrsBuffer *buf)
{
    srs_error_t err = srs_success;

    if ((err = SrsMp4Box::decode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "decode header");
    }

    if (!buf->require(4)) {
        return srs_error_new(ERROR_MP4_BOX_REQUIRE_SPACE, "full box requires 4 only %d bytes", buf->left());
    }

    flags_ = (uint32_t)buf->read_4bytes();

    version_ = (uint8_t)((flags_ >> 24) & 0xff);
    flags_ &= 0x00ffffff;

    // The left required size, determined by the version.
    int lrsz = nb_header() - SrsMp4FullBox::nb_header();
    if (!buf->require(lrsz)) {
        return srs_error_new(ERROR_MP4_BOX_REQUIRE_SPACE, "full box requires %d only %d bytes", lrsz, buf->left());
    }

    return err;
}

stringstream &SrsMp4FullBox::dumps_detail(stringstream &ss, SrsMp4DumpContext dc)
{
    SrsMp4Box::dumps_detail(ss, dc);

    ss << ", FB(4B";

    if (version_ != 0 || flags_ != 0) {
        ss << ",V" << uint32_t(version_)
           << ",0x" << std::setw(2) << std::setfill('0') << std::hex << flags_ << std::dec;
    }

    ss << ")";

    return ss;
}

SrsMp4FileTypeBox::SrsMp4FileTypeBox()
{
    type_ = SrsMp4BoxTypeFTYP;
    major_brand_ = SrsMp4BoxBrandForbidden;
    minor_version_ = 0;
}

SrsMp4FileTypeBox::~SrsMp4FileTypeBox()
{
}

void SrsMp4FileTypeBox::set_compatible_brands(SrsMp4BoxBrand b0, SrsMp4BoxBrand b1)
{
    compatible_brands_.resize(2);
    compatible_brands_[0] = b0;
    compatible_brands_[1] = b1;
}

void SrsMp4FileTypeBox::set_compatible_brands(SrsMp4BoxBrand b0, SrsMp4BoxBrand b1, SrsMp4BoxBrand b2)
{
    compatible_brands_.resize(3);
    compatible_brands_[0] = b0;
    compatible_brands_[1] = b1;
    compatible_brands_[2] = b2;
}

void SrsMp4FileTypeBox::set_compatible_brands(SrsMp4BoxBrand b0, SrsMp4BoxBrand b1, SrsMp4BoxBrand b2, SrsMp4BoxBrand b3)
{
    compatible_brands_.resize(4);
    compatible_brands_[0] = b0;
    compatible_brands_[1] = b1;
    compatible_brands_[2] = b2;
    compatible_brands_[3] = b3;
}

int SrsMp4FileTypeBox::nb_header()
{
    return (int)(SrsMp4Box::nb_header() + 8 + compatible_brands_.size() * 4);
}

srs_error_t SrsMp4FileTypeBox::encode_header(SrsBuffer *buf)
{
    srs_error_t err = srs_success;

    if ((err = SrsMp4Box::encode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "encode header");
    }

    buf->write_4bytes(major_brand_);
    buf->write_4bytes(minor_version_);

    for (size_t i = 0; i < (size_t)compatible_brands_.size(); i++) {
        buf->write_4bytes(compatible_brands_[i]);
    }

    return err;
}

srs_error_t SrsMp4FileTypeBox::decode_header(SrsBuffer *buf)
{
    srs_error_t err = srs_success;

    if ((err = SrsMp4Box::decode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "decode header");
    }

    major_brand_ = (SrsMp4BoxBrand)buf->read_4bytes();
    minor_version_ = buf->read_4bytes();

    // Compatible brands to the end of the box.
    int left = left_space(buf);

    if (left > 0) {
        compatible_brands_.resize(left / 4);
    }

    for (int i = 0; left > 0; i++, left -= 4) {
        compatible_brands_[i] = (SrsMp4BoxBrand)buf->read_4bytes();
    }

    return err;
}

stringstream &SrsMp4FileTypeBox::dumps_detail(stringstream &ss, SrsMp4DumpContext dc)
{
    SrsMp4Box::dumps_detail(ss, dc);

    ss << ", brands:";
    srs_print_mp4_type(ss, (uint32_t)major_brand_);

    ss << "," << minor_version_;

    if (!compatible_brands_.empty()) {
        ss << "(";
        srs_dumps_array(compatible_brands_, ss, dc, srs_mp4_pfn_type, srs_mp4_delimiter_inline);
        ss << ")";
    }
    return ss;
}

SrsMp4SegmentTypeBox::SrsMp4SegmentTypeBox()
{
    type_ = SrsMp4BoxTypeSTYP;
}

SrsMp4SegmentTypeBox::~SrsMp4SegmentTypeBox()
{
}

SrsMp4MovieFragmentBox::SrsMp4MovieFragmentBox()
{
    type_ = SrsMp4BoxTypeMOOF;
}

SrsMp4MovieFragmentBox::~SrsMp4MovieFragmentBox()
{
}

SrsMp4MovieFragmentHeaderBox *SrsMp4MovieFragmentBox::mfhd()
{
    SrsMp4Box *box = get(SrsMp4BoxTypeMFHD);
    return dynamic_cast<SrsMp4MovieFragmentHeaderBox *>(box);
}

void SrsMp4MovieFragmentBox::set_mfhd(SrsMp4MovieFragmentHeaderBox *v)
{
    remove(SrsMp4BoxTypeMFHD);
    boxes_.push_back(v);
}

void SrsMp4MovieFragmentBox::add_traf(SrsMp4TrackFragmentBox *v)
{
    boxes_.push_back(v);
}

SrsMp4MovieFragmentHeaderBox::SrsMp4MovieFragmentHeaderBox()
{
    type_ = SrsMp4BoxTypeMFHD;

    sequence_number_ = 0;
}

SrsMp4MovieFragmentHeaderBox::~SrsMp4MovieFragmentHeaderBox()
{
}

int SrsMp4MovieFragmentHeaderBox::nb_header()
{
    return SrsMp4FullBox::nb_header() + 4;
}

srs_error_t SrsMp4MovieFragmentHeaderBox::encode_header(SrsBuffer *buf)
{
    srs_error_t err = srs_success;

    if ((err = SrsMp4FullBox::encode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "encode header");
    }

    buf->write_4bytes(sequence_number_);

    return err;
}

srs_error_t SrsMp4MovieFragmentHeaderBox::decode_header(SrsBuffer *buf)
{
    srs_error_t err = srs_success;

    if ((err = SrsMp4FullBox::decode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "decode header");
    }

    sequence_number_ = buf->read_4bytes();

    return err;
}

stringstream &SrsMp4MovieFragmentHeaderBox::dumps_detail(stringstream &ss, SrsMp4DumpContext dc)
{
    SrsMp4FullBox::dumps_detail(ss, dc);

    ss << ", sequence=" << sequence_number_;
    return ss;
}

SrsMp4TrackFragmentBox::SrsMp4TrackFragmentBox()
{
    type_ = SrsMp4BoxTypeTRAF;
}

SrsMp4TrackFragmentBox::~SrsMp4TrackFragmentBox()
{
}

SrsMp4TrackFragmentHeaderBox *SrsMp4TrackFragmentBox::tfhd()
{
    SrsMp4Box *box = get(SrsMp4BoxTypeTFHD);
    return dynamic_cast<SrsMp4TrackFragmentHeaderBox *>(box);
}

void SrsMp4TrackFragmentBox::set_tfhd(SrsMp4TrackFragmentHeaderBox *v)
{
    remove(SrsMp4BoxTypeTFHD);
    boxes_.push_back(v);
}

SrsMp4TrackFragmentDecodeTimeBox *SrsMp4TrackFragmentBox::tfdt()
{
    SrsMp4Box *box = get(SrsMp4BoxTypeTFDT);
    return dynamic_cast<SrsMp4TrackFragmentDecodeTimeBox *>(box);
}

void SrsMp4TrackFragmentBox::set_tfdt(SrsMp4TrackFragmentDecodeTimeBox *v)
{
    remove(SrsMp4BoxTypeTFDT);
    boxes_.push_back(v);
}

SrsMp4TrackFragmentRunBox *SrsMp4TrackFragmentBox::trun()
{
    SrsMp4Box *box = get(SrsMp4BoxTypeTRUN);
    return dynamic_cast<SrsMp4TrackFragmentRunBox *>(box);
}

void SrsMp4TrackFragmentBox::set_trun(SrsMp4TrackFragmentRunBox *v)
{
    remove(SrsMp4BoxTypeTRUN);
    boxes_.push_back(v);
}

SrsMp4TrackFragmentHeaderBox::SrsMp4TrackFragmentHeaderBox()
{
    type_ = SrsMp4BoxTypeTFHD;

    flags_ = 0;
    base_data_offset_ = 0;
    track_id_ = sample_description_index_ = 0;
    default_sample_duration_ = default_sample_size_ = 0;
    default_sample_flags_ = 0;
}

SrsMp4TrackFragmentHeaderBox::~SrsMp4TrackFragmentHeaderBox()
{
}

int SrsMp4TrackFragmentHeaderBox::nb_header()
{
    int size = SrsMp4FullBox::nb_header() + 4;

    if ((flags_ & SrsMp4TfhdFlagsBaseDataOffset) == SrsMp4TfhdFlagsBaseDataOffset) {
        size += 8;
    }
    if ((flags_ & SrsMp4TfhdFlagsSampleDescriptionIndex) == SrsMp4TfhdFlagsSampleDescriptionIndex) {
        size += 4;
    }
    if ((flags_ & SrsMp4TfhdFlagsDefaultSampleDuration) == SrsMp4TfhdFlagsDefaultSampleDuration) {
        size += 4;
    }
    if ((flags_ & SrsMp4TfhdFlagsDefautlSampleSize) == SrsMp4TfhdFlagsDefautlSampleSize) {
        size += 4;
    }
    if ((flags_ & SrsMp4TfhdFlagsDefaultSampleFlags) == SrsMp4TfhdFlagsDefaultSampleFlags) {
        size += 4;
    }

    return size;
}

srs_error_t SrsMp4TrackFragmentHeaderBox::encode_header(SrsBuffer *buf)
{
    srs_error_t err = srs_success;

    if ((err = SrsMp4FullBox::encode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "encode header");
    }

    buf->write_4bytes(track_id_);

    if ((flags_ & SrsMp4TfhdFlagsBaseDataOffset) == SrsMp4TfhdFlagsBaseDataOffset) {
        buf->write_8bytes(base_data_offset_);
    }
    if ((flags_ & SrsMp4TfhdFlagsSampleDescriptionIndex) == SrsMp4TfhdFlagsSampleDescriptionIndex) {
        buf->write_4bytes(sample_description_index_);
    }
    if ((flags_ & SrsMp4TfhdFlagsDefaultSampleDuration) == SrsMp4TfhdFlagsDefaultSampleDuration) {
        buf->write_4bytes(default_sample_duration_);
    }
    if ((flags_ & SrsMp4TfhdFlagsDefautlSampleSize) == SrsMp4TfhdFlagsDefautlSampleSize) {
        buf->write_4bytes(default_sample_size_);
    }
    if ((flags_ & SrsMp4TfhdFlagsDefaultSampleFlags) == SrsMp4TfhdFlagsDefaultSampleFlags) {
        buf->write_4bytes(default_sample_flags_);
    }

    return err;
}

srs_error_t SrsMp4TrackFragmentHeaderBox::decode_header(SrsBuffer *buf)
{
    srs_error_t err = srs_success;

    if ((err = SrsMp4FullBox::decode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "decode header");
    }

    track_id_ = buf->read_4bytes();

    if ((flags_ & SrsMp4TfhdFlagsBaseDataOffset) == SrsMp4TfhdFlagsBaseDataOffset) {
        base_data_offset_ = buf->read_8bytes();
    }
    if ((flags_ & SrsMp4TfhdFlagsSampleDescriptionIndex) == SrsMp4TfhdFlagsSampleDescriptionIndex) {
        sample_description_index_ = buf->read_4bytes();
    }
    if ((flags_ & SrsMp4TfhdFlagsDefaultSampleDuration) == SrsMp4TfhdFlagsDefaultSampleDuration) {
        default_sample_duration_ = buf->read_4bytes();
    }
    if ((flags_ & SrsMp4TfhdFlagsDefautlSampleSize) == SrsMp4TfhdFlagsDefautlSampleSize) {
        default_sample_size_ = buf->read_4bytes();
    }
    if ((flags_ & SrsMp4TfhdFlagsDefaultSampleFlags) == SrsMp4TfhdFlagsDefaultSampleFlags) {
        default_sample_flags_ = buf->read_4bytes();
    }

    return err;
}

stringstream &SrsMp4TrackFragmentHeaderBox::dumps_detail(stringstream &ss, SrsMp4DumpContext dc)
{
    SrsMp4FullBox::dumps_detail(ss, dc);

    ss << ", track=" << track_id_;

    if ((flags_ & SrsMp4TfhdFlagsBaseDataOffset) == SrsMp4TfhdFlagsBaseDataOffset) {
        ss << ", bdo=" << base_data_offset_;
    }
    if ((flags_ & SrsMp4TfhdFlagsSampleDescriptionIndex) == SrsMp4TfhdFlagsSampleDescriptionIndex) {
        ss << ", sdi=" << sample_description_index_;
    }
    if ((flags_ & SrsMp4TfhdFlagsDefaultSampleDuration) == SrsMp4TfhdFlagsDefaultSampleDuration) {
        ss << ", dsu=" << default_sample_duration_;
    }
    if ((flags_ & SrsMp4TfhdFlagsDefautlSampleSize) == SrsMp4TfhdFlagsDefautlSampleSize) {
        ss << ", dss=" << default_sample_size_;
    }
    if ((flags_ & SrsMp4TfhdFlagsDefaultSampleFlags) == SrsMp4TfhdFlagsDefaultSampleFlags) {
        ss << ", dsf=" << default_sample_flags_;
    }

    if ((flags_ & SrsMp4TfhdFlagsDurationIsEmpty) == SrsMp4TfhdFlagsDurationIsEmpty) {
        ss << ", empty-duration";
    }
    if ((flags_ & SrsMp4TfhdFlagsDefaultBaseIsMoof) == SrsMp4TfhdFlagsDefaultBaseIsMoof) {
        ss << ", moof-base";
    }

    return ss;
}

SrsMp4TrackFragmentDecodeTimeBox::SrsMp4TrackFragmentDecodeTimeBox()
{
    type_ = SrsMp4BoxTypeTFDT;
    base_media_decode_time_ = 0;
}

SrsMp4TrackFragmentDecodeTimeBox::~SrsMp4TrackFragmentDecodeTimeBox()
{
}

int SrsMp4TrackFragmentDecodeTimeBox::nb_header()
{
    return SrsMp4FullBox::nb_header() + (version_ ? 8 : 4);
}

srs_error_t SrsMp4TrackFragmentDecodeTimeBox::encode_header(SrsBuffer *buf)
{
    srs_error_t err = srs_success;

    if ((err = SrsMp4FullBox::encode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "encode header");
    }

    if (version_) {
        buf->write_8bytes(base_media_decode_time_);
    } else {
        buf->write_4bytes((uint32_t)base_media_decode_time_);
    }

    return err;
}

srs_error_t SrsMp4TrackFragmentDecodeTimeBox::decode_header(SrsBuffer *buf)
{
    srs_error_t err = srs_success;

    if ((err = SrsMp4FullBox::decode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "decode header");
    }

    if (version_) {
        base_media_decode_time_ = buf->read_8bytes();
    } else {
        base_media_decode_time_ = buf->read_4bytes();
    }

    return err;
}

stringstream &SrsMp4TrackFragmentDecodeTimeBox::dumps_detail(stringstream &ss, SrsMp4DumpContext dc)
{
    SrsMp4FullBox::dumps_detail(ss, dc);

    ss << ", bmdt=" << base_media_decode_time_;

    return ss;
}

SrsMp4TrunEntry::SrsMp4TrunEntry(SrsMp4FullBox *o)
{
    owner_ = o;
    sample_duration_ = sample_size_ = sample_flags_ = 0;
    sample_composition_time_offset_ = 0;
}

SrsMp4TrunEntry::~SrsMp4TrunEntry()
{
}

uint64_t SrsMp4TrunEntry::nb_bytes()
{
    int size = 0;

    if ((owner_->flags_ & SrsMp4TrunFlagsSampleDuration) == SrsMp4TrunFlagsSampleDuration) {
        size += 4;
    }
    if ((owner_->flags_ & SrsMp4TrunFlagsSampleSize) == SrsMp4TrunFlagsSampleSize) {
        size += 4;
    }
    if ((owner_->flags_ & SrsMp4TrunFlagsSampleFlag) == SrsMp4TrunFlagsSampleFlag) {
        size += 4;
    }
    if ((owner_->flags_ & SrsMp4TrunFlagsSampleCtsOffset) == SrsMp4TrunFlagsSampleCtsOffset) {
        size += 4;
    }

    return size;
}

srs_error_t SrsMp4TrunEntry::encode(SrsBuffer *buf)
{
    srs_error_t err = srs_success;

    if ((owner_->flags_ & SrsMp4TrunFlagsSampleDuration) == SrsMp4TrunFlagsSampleDuration) {
        buf->write_4bytes(sample_duration_);
    }
    if ((owner_->flags_ & SrsMp4TrunFlagsSampleSize) == SrsMp4TrunFlagsSampleSize) {
        buf->write_4bytes(sample_size_);
    }
    if ((owner_->flags_ & SrsMp4TrunFlagsSampleFlag) == SrsMp4TrunFlagsSampleFlag) {
        buf->write_4bytes(sample_flags_);
    }
    if ((owner_->flags_ & SrsMp4TrunFlagsSampleCtsOffset) == SrsMp4TrunFlagsSampleCtsOffset) {
        if (!owner_->version_) {
            uint32_t v = (uint32_t)sample_composition_time_offset_;
            buf->write_4bytes(v);
        } else {
            int32_t v = (int32_t)sample_composition_time_offset_;
            buf->write_4bytes(v);
        }
    }

    return err;
}

srs_error_t SrsMp4TrunEntry::decode(SrsBuffer *buf)
{
    srs_error_t err = srs_success;

    if ((owner_->flags_ & SrsMp4TrunFlagsSampleDuration) == SrsMp4TrunFlagsSampleDuration) {
        sample_duration_ = buf->read_4bytes();
    }
    if ((owner_->flags_ & SrsMp4TrunFlagsSampleSize) == SrsMp4TrunFlagsSampleSize) {
        sample_size_ = buf->read_4bytes();
    }
    if ((owner_->flags_ & SrsMp4TrunFlagsSampleFlag) == SrsMp4TrunFlagsSampleFlag) {
        sample_flags_ = buf->read_4bytes();
    }
    if ((owner_->flags_ & SrsMp4TrunFlagsSampleCtsOffset) == SrsMp4TrunFlagsSampleCtsOffset) {
        if (!owner_->version_) {
            uint32_t v = buf->read_4bytes();
            sample_composition_time_offset_ = v;
        } else {
            int32_t v = buf->read_4bytes();
            sample_composition_time_offset_ = v;
        }
    }

    return err;
}

stringstream &SrsMp4TrunEntry::dumps(stringstream &ss, SrsMp4DumpContext dc)
{
    if ((owner_->flags_ & SrsMp4TrunFlagsSampleDuration) == SrsMp4TrunFlagsSampleDuration) {
        ss << "duration=" << sample_duration_;
    }
    if ((owner_->flags_ & SrsMp4TrunFlagsSampleSize) == SrsMp4TrunFlagsSampleSize) {
        ss << ", size=" << sample_size_;
    }
    if ((owner_->flags_ & SrsMp4TrunFlagsSampleFlag) == SrsMp4TrunFlagsSampleFlag) {
        ss << ", flags=" << sample_flags_;
    }
    if ((owner_->flags_ & SrsMp4TrunFlagsSampleCtsOffset) == SrsMp4TrunFlagsSampleCtsOffset) {
        ss << ", cts=" << sample_composition_time_offset_;
    }
    return ss;
}

SrsMp4TrackFragmentRunBox::SrsMp4TrackFragmentRunBox()
{
    type_ = SrsMp4BoxTypeTRUN;
    first_sample_flags_ = 0;
    data_offset_ = 0;
}

SrsMp4TrackFragmentRunBox::~SrsMp4TrackFragmentRunBox()
{
    vector<SrsMp4TrunEntry *>::iterator it;
    for (it = entries_.begin(); it != entries_.end(); ++it) {
        SrsMp4TrunEntry *entry = *it;
        srs_freep(entry);
    }
}

int SrsMp4TrackFragmentRunBox::nb_header()
{
    int size = SrsMp4FullBox::nb_header() + 4;

    if ((flags_ & SrsMp4TrunFlagsDataOffset) == SrsMp4TrunFlagsDataOffset) {
        size += 4;
    }
    if ((flags_ & SrsMp4TrunFlagsFirstSample) == SrsMp4TrunFlagsFirstSample) {
        size += 4;
    }

    vector<SrsMp4TrunEntry *>::iterator it;
    for (it = entries_.begin(); it != entries_.end(); ++it) {
        SrsMp4TrunEntry *entry = *it;
        size += entry->nb_bytes();
    }

    return size;
}

srs_error_t SrsMp4TrackFragmentRunBox::encode_header(SrsBuffer *buf)
{
    srs_error_t err = srs_success;

    if ((err = SrsMp4FullBox::encode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "encode header");
    }

    uint32_t sample_count = (uint32_t)entries_.size();
    buf->write_4bytes(sample_count);

    if ((flags_ & SrsMp4TrunFlagsDataOffset) == SrsMp4TrunFlagsDataOffset) {
        buf->write_4bytes(data_offset_);
    }
    if ((flags_ & SrsMp4TrunFlagsFirstSample) == SrsMp4TrunFlagsFirstSample) {
        buf->write_4bytes(first_sample_flags_);
    }

    vector<SrsMp4TrunEntry *>::iterator it;
    for (it = entries_.begin(); it != entries_.end(); ++it) {
        SrsMp4TrunEntry *entry = *it;
        if ((err = entry->encode(buf)) != srs_success) {
            return srs_error_wrap(err, "encode entry");
        }
    }

    return err;
}

srs_error_t SrsMp4TrackFragmentRunBox::decode_header(SrsBuffer *buf)
{
    srs_error_t err = srs_success;

    if ((err = SrsMp4FullBox::decode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "decode header");
    }

    uint32_t sample_count = buf->read_4bytes();

    if ((flags_ & SrsMp4TrunFlagsDataOffset) == SrsMp4TrunFlagsDataOffset) {
        data_offset_ = buf->read_4bytes();
    }
    if ((flags_ & SrsMp4TrunFlagsFirstSample) == SrsMp4TrunFlagsFirstSample) {
        first_sample_flags_ = buf->read_4bytes();
    }

    for (int i = 0; i < (int)sample_count; i++) {
        SrsMp4TrunEntry *entry = new SrsMp4TrunEntry(this);
        entries_.push_back(entry);

        if (!buf->require(entry->nb_bytes())) {
            return srs_error_new(ERROR_MP4_BOX_REQUIRE_SPACE, "trun entry requires %d bytes", entry->nb_bytes());
        }

        if ((err = entry->decode(buf)) != srs_success) {
            return srs_error_wrap(err, "decode entry");
        }
    }

    return err;
}

stringstream &SrsMp4TrackFragmentRunBox::dumps_detail(stringstream &ss, SrsMp4DumpContext dc)
{
    SrsMp4FullBox::dumps_detail(ss, dc);

    uint32_t sample_count = (uint32_t)entries_.size();
    ss << ", samples=" << sample_count;

    if ((flags_ & SrsMp4TrunFlagsDataOffset) == SrsMp4TrunFlagsDataOffset) {
        ss << ", data-offset=" << data_offset_;
    }
    if ((flags_ & SrsMp4TrunFlagsFirstSample) == SrsMp4TrunFlagsFirstSample) {
        ss << ", first-sample=" << first_sample_flags_;
    }

    if (sample_count > 0) {
        ss << endl;
        srs_mp4_padding(ss, dc.indent());
        srs_dumps_array(entries_, ss, dc.indent(), srs_mp4_pfn_box2, srs_mp4_delimiter_newline);
    }

    return ss;
}

SrsMp4MediaDataBox::SrsMp4MediaDataBox()
{
    type_ = SrsMp4BoxTypeMDAT;
    nb_data_ = 0;
}

SrsMp4MediaDataBox::~SrsMp4MediaDataBox()
{
}

uint64_t SrsMp4MediaDataBox::nb_bytes()
{
    return SrsMp4Box::nb_header() + nb_data_;
}

srs_error_t SrsMp4MediaDataBox::encode(SrsBuffer *buf)
{
    srs_error_t err = srs_success;

    if ((err = SrsMp4Box::encode(buf)) != srs_success) {
        return srs_error_wrap(err, "encode box");
    }

    return err;
}

srs_error_t SrsMp4MediaDataBox::decode(SrsBuffer *buf)
{
    srs_error_t err = srs_success;

    if ((err = SrsMp4Box::decode(buf)) != srs_success) {
        return srs_error_wrap(err, "decode box");
    }

    nb_data_ = sz() - (uint64_t)nb_header();

    return err;
}

srs_error_t SrsMp4MediaDataBox::encode_boxes(SrsBuffer *buf)
{
    return srs_success;
}

srs_error_t SrsMp4MediaDataBox::decode_boxes(SrsBuffer *buf)
{
    return srs_success;
}

stringstream &SrsMp4MediaDataBox::dumps_detail(stringstream &ss, SrsMp4DumpContext dc)
{
    SrsMp4Box::dumps_detail(ss, dc);

    ss << ", total " << nb_data_ << " bytes";

    return ss;
}

SrsMp4FreeSpaceBox::SrsMp4FreeSpaceBox(SrsMp4BoxType v)
{
    type_ = v; // 'free' or 'skip'
}

SrsMp4FreeSpaceBox::~SrsMp4FreeSpaceBox()
{
}

int SrsMp4FreeSpaceBox::nb_header()
{
    return SrsMp4Box::nb_header() + (int)data_.size();
}

srs_error_t SrsMp4FreeSpaceBox::encode_header(SrsBuffer *buf)
{
    srs_error_t err = srs_success;

    if ((err = SrsMp4Box::encode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "encode header");
    }

    if (!data_.empty()) {
        buf->write_bytes(&data_[0], (int)data_.size());
    }

    return err;
}

srs_error_t SrsMp4FreeSpaceBox::decode_header(SrsBuffer *buf)
{
    srs_error_t err = srs_success;

    if ((err = SrsMp4Box::decode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "decode header");
    }

    int left = left_space(buf);
    if (left) {
        data_.resize(left);
        buf->read_bytes(&data_[0], left);
    }

    return err;
}

stringstream &SrsMp4FreeSpaceBox::dumps_detail(stringstream &ss, SrsMp4DumpContext dc)
{
    SrsMp4Box::dumps_detail(ss, dc);

    ss << ", free " << data_.size() << "B";

    if (!data_.empty()) {
        ss << endl;
        srs_mp4_padding(ss, dc.indent());
        srs_dumps_array(&data_[0], (int)data_.size(), ss, dc.indent(), srs_mp4_pfn_hex, srs_mp4_delimiter_inspace);
    }
    return ss;
}

SrsMp4MovieBox::SrsMp4MovieBox()
{
    type_ = SrsMp4BoxTypeMOOV;
}

SrsMp4MovieBox::~SrsMp4MovieBox()
{
}

SrsMp4MovieHeaderBox *SrsMp4MovieBox::mvhd()
{
    SrsMp4Box *box = get(SrsMp4BoxTypeMVHD);
    return dynamic_cast<SrsMp4MovieHeaderBox *>(box);
}

void SrsMp4MovieBox::set_mvhd(SrsMp4MovieHeaderBox *v)
{
    remove(SrsMp4BoxTypeMVHD);
    boxes_.push_back(v);
}

SrsMp4MovieExtendsBox *SrsMp4MovieBox::mvex()
{
    SrsMp4Box *box = get(SrsMp4BoxTypeMVEX);
    return dynamic_cast<SrsMp4MovieExtendsBox *>(box);
}

void SrsMp4MovieBox::set_mvex(SrsMp4MovieExtendsBox *v)
{
    remove(SrsMp4BoxTypeMVEX);
    boxes_.push_back(v);
}

SrsMp4TrackBox *SrsMp4MovieBox::video()
{
    for (int i = 0; i < (int)boxes_.size(); i++) {
        SrsMp4Box *box = boxes_.at(i);
        if (box->type_ == SrsMp4BoxTypeTRAK) {
            SrsMp4TrackBox *trak = dynamic_cast<SrsMp4TrackBox *>(box);
            if ((trak->track_type() & SrsMp4TrackTypeVideo) == SrsMp4TrackTypeVideo) {
                return trak;
            }
        }
    }
    return NULL;
}

SrsMp4TrackBox *SrsMp4MovieBox::audio()
{
    for (int i = 0; i < (int)boxes_.size(); i++) {
        SrsMp4Box *box = boxes_.at(i);
        if (box->type_ == SrsMp4BoxTypeTRAK) {
            SrsMp4TrackBox *trak = dynamic_cast<SrsMp4TrackBox *>(box);
            if ((trak->track_type() & SrsMp4TrackTypeAudio) == SrsMp4TrackTypeAudio) {
                return trak;
            }
        }
    }
    return NULL;
}

void SrsMp4MovieBox::add_trak(SrsMp4TrackBox *v)
{
    boxes_.push_back(v);
}

int SrsMp4MovieBox::nb_vide_tracks()
{
    int nb_tracks = 0;

    for (int i = 0; i < (int)boxes_.size(); i++) {
        SrsMp4Box *box = boxes_.at(i);
        if (box->type_ == SrsMp4BoxTypeTRAK) {
            SrsMp4TrackBox *trak = dynamic_cast<SrsMp4TrackBox *>(box);
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

    for (int i = 0; i < (int)boxes_.size(); i++) {
        SrsMp4Box *box = boxes_.at(i);
        if (box->type_ == SrsMp4BoxTypeTRAK) {
            SrsMp4TrackBox *trak = dynamic_cast<SrsMp4TrackBox *>(box);
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

srs_error_t SrsMp4MovieBox::encode_header(SrsBuffer *buf)
{
    srs_error_t err = srs_success;

    if ((err = SrsMp4Box::encode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "encode header");
    }

    return err;
}

srs_error_t SrsMp4MovieBox::decode_header(SrsBuffer *buf)
{
    srs_error_t err = srs_success;

    if ((err = SrsMp4Box::decode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "decode header");
    }

    return err;
}

SrsMp4MovieHeaderBox::SrsMp4MovieHeaderBox() : creation_time_(0), modification_time_(0), timescale_(0), duration_in_tbn_(0)
{
    type_ = SrsMp4BoxTypeMVHD;

    rate_ = 0x00010000; // typically 1.0
    volume_ = 0x0100;   // typically, full volume
    reserved0_ = 0;
    reserved1_ = 0;

    int32_t v[] = {0x00010000, 0, 0, 0, 0x00010000, 0, 0, 0, 0x40000000};
    memcpy(matrix_, v, 36);

    memset(pre_defined_, 0, 24);

    next_track_ID_ = 0;
}

SrsMp4MovieHeaderBox::~SrsMp4MovieHeaderBox()
{
}

uint64_t SrsMp4MovieHeaderBox::duration()
{
    if (timescale_ <= 0) {
        return 0;
    }
    return duration_in_tbn_ * 1000 / timescale_;
}

int SrsMp4MovieHeaderBox::nb_header()
{
    int size = SrsMp4FullBox::nb_header();

    if (version_ == 1) {
        size += 8 + 8 + 4 + 8;
    } else {
        size += 4 + 4 + 4 + 4;
    }

    size += 4 + 2 + 2 + 8 + 36 + 24 + 4;

    return size;
}

srs_error_t SrsMp4MovieHeaderBox::encode_header(SrsBuffer *buf)
{
    srs_error_t err = srs_success;

    if ((err = SrsMp4FullBox::encode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "encode header");
    }

    if (version_ == 1) {
        buf->write_8bytes(creation_time_);
        buf->write_8bytes(modification_time_);
        buf->write_4bytes(timescale_);
        buf->write_8bytes(duration_in_tbn_);
    } else {
        buf->write_4bytes((uint32_t)creation_time_);
        buf->write_4bytes((uint32_t)modification_time_);
        buf->write_4bytes(timescale_);
        buf->write_4bytes((uint32_t)duration_in_tbn_);
    }

    buf->write_4bytes(rate_);
    buf->write_2bytes(volume_);
    buf->write_2bytes(reserved0_);
    buf->write_8bytes(reserved1_);
    for (int i = 0; i < 9; i++) {
        buf->write_4bytes(matrix_[i]);
    }
    for (int i = 0; i < 6; i++) {
        buf->write_4bytes(pre_defined_[i]);
    }
    buf->write_4bytes(next_track_ID_);

    return err;
}

srs_error_t SrsMp4MovieHeaderBox::decode_header(SrsBuffer *buf)
{
    srs_error_t err = srs_success;

    if ((err = SrsMp4FullBox::decode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "decode header");
    }

    if (version_ == 1) {
        creation_time_ = buf->read_8bytes();
        modification_time_ = buf->read_8bytes();
        timescale_ = buf->read_4bytes();
        duration_in_tbn_ = buf->read_8bytes();
    } else {
        creation_time_ = buf->read_4bytes();
        modification_time_ = buf->read_4bytes();
        timescale_ = buf->read_4bytes();
        duration_in_tbn_ = buf->read_4bytes();
    }

    rate_ = buf->read_4bytes();
    volume_ = buf->read_2bytes();
    buf->skip(2);
    buf->skip(8);
    for (int i = 0; i < 9; i++) {
        matrix_[i] = buf->read_4bytes();
    }
    buf->skip(24);
    next_track_ID_ = buf->read_4bytes();

    return err;
}

stringstream &SrsMp4MovieHeaderBox::dumps_detail(stringstream &ss, SrsMp4DumpContext dc)
{
    SrsMp4FullBox::dumps_detail(ss, dc);

    ss << ", " << std::setprecision(2) << duration() << "ms, TBN=" << timescale_ << ", nTID=" << next_track_ID_;
    return ss;
}

SrsMp4MovieExtendsBox::SrsMp4MovieExtendsBox()
{
    type_ = SrsMp4BoxTypeMVEX;
}

SrsMp4MovieExtendsBox::~SrsMp4MovieExtendsBox()
{
}

void SrsMp4MovieExtendsBox::add_trex(SrsMp4TrackExtendsBox *v)
{
    boxes_.push_back(v);
}

SrsMp4TrackExtendsBox::SrsMp4TrackExtendsBox()
{
    type_ = SrsMp4BoxTypeTREX;
    track_ID_ = default_sample_size_ = default_sample_flags_ = 0;
    default_sample_size_ = default_sample_duration_ = default_sample_description_index_ = 0;
}

SrsMp4TrackExtendsBox::~SrsMp4TrackExtendsBox()
{
}

int SrsMp4TrackExtendsBox::nb_header()
{
    return SrsMp4FullBox::nb_header() + 4 * 5;
}

srs_error_t SrsMp4TrackExtendsBox::encode_header(SrsBuffer *buf)
{
    srs_error_t err = srs_success;

    if ((err = SrsMp4FullBox::encode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "encode header");
    }

    buf->write_4bytes(track_ID_);
    buf->write_4bytes(default_sample_description_index_);
    buf->write_4bytes(default_sample_duration_);
    buf->write_4bytes(default_sample_size_);
    buf->write_4bytes(default_sample_flags_);

    return err;
}

srs_error_t SrsMp4TrackExtendsBox::decode_header(SrsBuffer *buf)
{
    srs_error_t err = srs_success;

    if ((err = SrsMp4FullBox::decode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "decode header");
    }

    track_ID_ = buf->read_4bytes();
    default_sample_description_index_ = buf->read_4bytes();
    default_sample_duration_ = buf->read_4bytes();
    default_sample_size_ = buf->read_4bytes();
    default_sample_flags_ = buf->read_4bytes();

    return err;
}

stringstream &SrsMp4TrackExtendsBox::dumps_detail(stringstream &ss, SrsMp4DumpContext dc)
{
    SrsMp4FullBox::dumps_detail(ss, dc);

    ss << ", track=#" << track_ID_ << ", default-sample("
       << "index:" << default_sample_description_index_ << ", size:" << default_sample_size_
       << ", duration:" << default_sample_duration_ << ", flags:" << default_sample_flags_ << ")";
    return ss;
}

SrsMp4TrackBox::SrsMp4TrackBox()
{
    type_ = SrsMp4BoxTypeTRAK;
}

SrsMp4TrackBox::~SrsMp4TrackBox()
{
}

SrsMp4TrackType SrsMp4TrackBox::track_type()
{
    // TODO: Maybe should discovery all mdia boxes.
    SrsMp4MediaBox *box = mdia();
    if (!box) {
        return SrsMp4TrackTypeForbidden;
    }
    return box->track_type();
}

SrsMp4TrackHeaderBox *SrsMp4TrackBox::tkhd()
{
    SrsMp4Box *box = get(SrsMp4BoxTypeTKHD);
    return dynamic_cast<SrsMp4TrackHeaderBox *>(box);
}

void SrsMp4TrackBox::set_tkhd(SrsMp4TrackHeaderBox *v)
{
    remove(SrsMp4BoxTypeTKHD);
    boxes_.insert(boxes_.begin(), v);
}

void SrsMp4TrackBox::set_edts(SrsMp4EditBox *v)
{
    remove(SrsMp4BoxTypeEDTS);
    boxes_.insert(boxes_.begin(), v);
}

SrsMp4ChunkOffsetBox *SrsMp4TrackBox::stco()
{
    SrsMp4SampleTableBox *box = stbl();
    return box ? box->stco() : NULL;
}

SrsMp4SampleSizeBox *SrsMp4TrackBox::stsz()
{
    SrsMp4SampleTableBox *box = stbl();
    return box ? box->stsz() : NULL;
}

SrsMp4Sample2ChunkBox *SrsMp4TrackBox::stsc()
{
    SrsMp4SampleTableBox *box = stbl();
    return box ? box->stsc() : NULL;
}

SrsMp4DecodingTime2SampleBox *SrsMp4TrackBox::stts()
{
    SrsMp4SampleTableBox *box = stbl();
    return box ? box->stts() : NULL;
}

SrsMp4CompositionTime2SampleBox *SrsMp4TrackBox::ctts()
{
    SrsMp4SampleTableBox *box = stbl();
    return box ? box->ctts() : NULL;
}

SrsMp4SyncSampleBox *SrsMp4TrackBox::stss()
{
    SrsMp4SampleTableBox *box = stbl();
    return box ? box->stss() : NULL;
}

SrsMp4MediaHeaderBox *SrsMp4TrackBox::mdhd()
{
    SrsMp4MediaBox *box = mdia();
    return box ? box->mdhd() : NULL;
}

SrsVideoCodecId SrsMp4TrackBox::vide_codec()
{
    SrsMp4SampleDescriptionBox *box = stsd();
    if (!box) {
        return SrsVideoCodecIdForbidden;
    }

    if (box->entry_count() == 0) {
        return SrsVideoCodecIdForbidden;
    }

    SrsMp4SampleEntry *entry = box->entrie_at(0);
    switch (entry->type_) {
    case SrsMp4BoxTypeAVC1:
        return SrsVideoCodecIdAVC;
    default:
        return SrsVideoCodecIdForbidden;
    }
}

SrsAudioCodecId SrsMp4TrackBox::soun_codec()
{
    SrsMp4SampleDescriptionBox *box = stsd();
    if (!box) {
        return SrsAudioCodecIdForbidden;
    }

    if (box->entry_count() == 0) {
        return SrsAudioCodecIdForbidden;
    }

    SrsMp4EsdsBox *esds_box = mp4a()->esds();
    switch (esds_box->es->decConfigDescr_.objectTypeIndication) {
    case SrsMp4ObjectTypeAac:
        return SrsAudioCodecIdAAC;
    case SrsMp4ObjectTypeMp3:
    case SrsMp4ObjectTypeMp1a:
        return SrsAudioCodecIdMP3;
    default:
        return SrsAudioCodecIdForbidden;
    }
}

SrsMp4AvccBox *SrsMp4TrackBox::avcc()
{
    SrsMp4VisualSampleEntry *box = avc1();
    return box ? box->avcC() : NULL;
}

SrsMp4DecoderSpecificInfo *SrsMp4TrackBox::asc()
{
    SrsMp4AudioSampleEntry *box = mp4a();
    return box ? box->asc() : NULL;
}

SrsMp4MediaBox *SrsMp4TrackBox::mdia()
{
    SrsMp4Box *box = get(SrsMp4BoxTypeMDIA);
    return dynamic_cast<SrsMp4MediaBox *>(box);
}

void SrsMp4TrackBox::set_mdia(SrsMp4MediaBox *v)
{
    remove(SrsMp4BoxTypeMDIA);
    boxes_.push_back(v);
}

SrsMp4MediaInformationBox *SrsMp4TrackBox::minf()
{
    SrsMp4MediaBox *box = mdia();
    return box ? box->minf() : NULL;
}

SrsMp4SampleTableBox *SrsMp4TrackBox::stbl()
{
    SrsMp4MediaInformationBox *box = minf();
    return box ? box->stbl() : NULL;
}

SrsMp4SampleDescriptionBox *SrsMp4TrackBox::stsd()
{
    SrsMp4SampleTableBox *box = stbl();
    return box ? box->stsd() : NULL;
}

SrsMp4VisualSampleEntry *SrsMp4TrackBox::avc1()
{
    SrsMp4SampleDescriptionBox *box = stsd();
    return box ? box->avc1() : NULL;
}

SrsMp4AudioSampleEntry *SrsMp4TrackBox::mp4a()
{
    SrsMp4SampleDescriptionBox *box = stsd();
    return box ? box->mp4a() : NULL;
}

SrsMp4TrackHeaderBox::SrsMp4TrackHeaderBox() : creation_time_(0), modification_time_(0), track_ID_(0), duration_(0)
{
    type_ = SrsMp4BoxTypeTKHD;

    reserved0_ = 0;
    reserved1_ = 0;
    reserved2_ = 0;
    layer_ = alternate_group_ = 0;
    volume_ = 0; // if track_is_audio 0x0100 else 0

    int32_t v[] = {0x00010000, 0, 0, 0, 0x00010000, 0, 0, 0, 0x40000000};
    memcpy(matrix_, v, 36);

    width_ = height_ = 0;
    flags_ = 0x03;
}

SrsMp4TrackHeaderBox::~SrsMp4TrackHeaderBox()
{
}

int SrsMp4TrackHeaderBox::nb_header()
{
    int size = SrsMp4FullBox::nb_header();

    if (version_ == 1) {
        size += 8 + 8 + 4 + 4 + 8;
    } else {
        size += 4 + 4 + 4 + 4 + 4;
    }

    size += 8 + 2 + 2 + 2 + 2 + 36 + 4 + 4;

    return size;
}

srs_error_t SrsMp4TrackHeaderBox::encode_header(SrsBuffer *buf)
{
    srs_error_t err = srs_success;

    if ((err = SrsMp4FullBox::encode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "encode header");
    }

    if (version_ == 1) {
        buf->write_8bytes(creation_time_);
        buf->write_8bytes(modification_time_);
        buf->write_4bytes(track_ID_);
        buf->write_4bytes(reserved0_);
        buf->write_8bytes(duration_);
    } else {
        buf->write_4bytes((uint32_t)creation_time_);
        buf->write_4bytes((uint32_t)modification_time_);
        buf->write_4bytes(track_ID_);
        buf->write_4bytes(reserved0_);
        buf->write_4bytes((uint32_t)duration_);
    }

    buf->write_8bytes(reserved1_);
    buf->write_2bytes(layer_);
    buf->write_2bytes(alternate_group_);
    buf->write_2bytes(volume_);
    buf->write_2bytes(reserved2_);
    for (int i = 0; i < 9; i++) {
        buf->write_4bytes(matrix_[i]);
    }
    buf->write_4bytes(width_);
    buf->write_4bytes(height_);

    return err;
}

srs_error_t SrsMp4TrackHeaderBox::decode_header(SrsBuffer *buf)
{
    srs_error_t err = srs_success;

    if ((err = SrsMp4FullBox::decode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "decode header");
    }

    if (version_ == 1) {
        creation_time_ = buf->read_8bytes();
        modification_time_ = buf->read_8bytes();
        track_ID_ = buf->read_4bytes();
        buf->skip(4);
        duration_ = buf->read_8bytes();
    } else {
        creation_time_ = buf->read_4bytes();
        modification_time_ = buf->read_4bytes();
        track_ID_ = buf->read_4bytes();
        buf->skip(4);
        duration_ = buf->read_4bytes();
    }

    buf->skip(8);
    layer_ = buf->read_2bytes();
    alternate_group_ = buf->read_2bytes();
    volume_ = buf->read_2bytes();
    buf->skip(2);
    for (int i = 0; i < 9; i++) {
        matrix_[i] = buf->read_4bytes();
    }
    width_ = buf->read_4bytes();
    height_ = buf->read_4bytes();

    return err;
}

stringstream &SrsMp4TrackHeaderBox::dumps_detail(stringstream &ss, SrsMp4DumpContext dc)
{
    SrsMp4FullBox::dumps_detail(ss, dc);

    ss << ", track #" << track_ID_ << ", " << duration_ << "TBN";

    if (volume_) {
        ss << ", volume=" << uint32_t(volume_ >> 8) << "." << uint32_t(volume_ & 0xFF);
    }

    ss << ", size=" << uint16_t(width_ >> 16) << "x" << uint16_t(height_ >> 16);

    return ss;
}

SrsMp4EditBox::SrsMp4EditBox()
{
    type_ = SrsMp4BoxTypeEDTS;
}

SrsMp4EditBox::~SrsMp4EditBox()
{
}

void SrsMp4EditBox::set_elst(SrsMp4EditListBox *v)
{
    remove(SrsMp4BoxTypeELST);
    boxes_.insert(boxes_.begin(), v);
}

SrsMp4ElstEntry::SrsMp4ElstEntry() : segment_duration_(0), media_time_(0), media_rate_integer_(0)
{
    media_rate_fraction_ = 0;
}

SrsMp4ElstEntry::~SrsMp4ElstEntry()
{
}

// LCOV_EXCL_START
stringstream &SrsMp4ElstEntry::dumps(stringstream &ss, SrsMp4DumpContext dc)
{
    return dumps_detail(ss, dc);
}
// LCOV_EXCL_STOP

stringstream &SrsMp4ElstEntry::dumps_detail(stringstream &ss, SrsMp4DumpContext dc)
{
    ss << "Entry, " << segment_duration_ << "TBN, start=" << media_time_ << "TBN"
       << ", rate=" << media_rate_integer_ << "," << media_rate_fraction_;
    return ss;
}

SrsMp4EditListBox::SrsMp4EditListBox()
{
    type_ = SrsMp4BoxTypeELST;
}

SrsMp4EditListBox::~SrsMp4EditListBox()
{
}

int SrsMp4EditListBox::nb_header()
{
    int size = SrsMp4FullBox::nb_header() + 4;

    if (version_ == 1) {
        size += entries_.size() * (2 + 2 + 8 + 8);
    } else {
        size += entries_.size() * (2 + 2 + 4 + 4);
    }

    return size;
}

srs_error_t SrsMp4EditListBox::encode_header(SrsBuffer *buf)
{
    srs_error_t err = srs_success;

    if ((err = SrsMp4FullBox::encode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "encode header");
    }

    buf->write_4bytes((int)entries_.size());
    for (size_t i = 0; i < (size_t)entries_.size(); i++) {
        SrsMp4ElstEntry &entry = entries_[i];

        if (version_ == 1) {
            buf->write_8bytes(entry.segment_duration_);
            buf->write_8bytes(entry.media_time_);
        } else {
            buf->write_4bytes((uint32_t)entry.segment_duration_);
            buf->write_4bytes((int32_t)entry.media_time_);
        }

        buf->write_2bytes(entry.media_rate_integer_);
        buf->write_2bytes(entry.media_rate_fraction_);
    }

    return err;
}

srs_error_t SrsMp4EditListBox::decode_header(SrsBuffer *buf)
{
    srs_error_t err = srs_success;

    if ((err = SrsMp4FullBox::decode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "decode header");
    }

    uint32_t entry_count = buf->read_4bytes();
    if (entry_count > 0) {
        entries_.resize(entry_count);
    }
    for (int i = 0; i < (int)entry_count; i++) {
        SrsMp4ElstEntry &entry = entries_[i];

        if (version_ == 1) {
            if (!buf->require(16)) {
                return srs_error_new(ERROR_MP4_BOX_REQUIRE_SPACE, "no space");
            }
            entry.segment_duration_ = buf->read_8bytes();
            entry.media_time_ = buf->read_8bytes();
        } else {
            if (!buf->require(8)) {
                return srs_error_new(ERROR_MP4_BOX_REQUIRE_SPACE, "no space");
            }
            entry.segment_duration_ = buf->read_4bytes();
            entry.media_time_ = buf->read_4bytes();
        }

        if (!buf->require(4)) {
            return srs_error_new(ERROR_MP4_BOX_REQUIRE_SPACE, "no space");
        }
        entry.media_rate_integer_ = buf->read_2bytes();
        entry.media_rate_fraction_ = buf->read_2bytes();
    }

    return err;
}

stringstream &SrsMp4EditListBox::dumps_detail(stringstream &ss, SrsMp4DumpContext dc)
{
    SrsMp4FullBox::dumps_detail(ss, dc);

    ss << ", " << entries_.size() << " childs";

    if (!entries_.empty()) {
        ss << "(+)" << endl;
        srs_mp4_padding(ss, dc.indent());
        srs_dumps_array(entries_, ss, dc.indent(), srs_mp4_pfn_detail, srs_mp4_delimiter_newline);
    }

    return ss;
}

SrsMp4MediaBox::SrsMp4MediaBox()
{
    type_ = SrsMp4BoxTypeMDIA;
}

SrsMp4MediaBox::~SrsMp4MediaBox()
{
}

SrsMp4TrackType SrsMp4MediaBox::track_type()
{
    SrsMp4Box *box = get(SrsMp4BoxTypeHDLR);
    if (!box) {
        return SrsMp4TrackTypeForbidden;
    }

    SrsMp4HandlerReferenceBox *hdlr = dynamic_cast<SrsMp4HandlerReferenceBox *>(box);
    if (hdlr->handler_type_ == SrsMp4HandlerTypeSOUN) {
        return SrsMp4TrackTypeAudio;
    } else if (hdlr->handler_type_ == SrsMp4HandlerTypeVIDE) {
        return SrsMp4TrackTypeVideo;
    } else {
        return SrsMp4TrackTypeForbidden;
    }
}

SrsMp4MediaHeaderBox *SrsMp4MediaBox::mdhd()
{
    SrsMp4Box *box = get(SrsMp4BoxTypeMDHD);
    return dynamic_cast<SrsMp4MediaHeaderBox *>(box);
}

void SrsMp4MediaBox::set_mdhd(SrsMp4MediaHeaderBox *v)
{
    remove(SrsMp4BoxTypeMDHD);
    boxes_.insert(boxes_.begin(), v);
}

SrsMp4HandlerReferenceBox *SrsMp4MediaBox::hdlr()
{
    SrsMp4Box *box = get(SrsMp4BoxTypeHDLR);
    return dynamic_cast<SrsMp4HandlerReferenceBox *>(box);
}

void SrsMp4MediaBox::set_hdlr(SrsMp4HandlerReferenceBox *v)
{
    remove(SrsMp4BoxTypeHDLR);
    boxes_.push_back(v);
}

SrsMp4MediaInformationBox *SrsMp4MediaBox::minf()
{
    SrsMp4Box *box = get(SrsMp4BoxTypeMINF);
    return dynamic_cast<SrsMp4MediaInformationBox *>(box);
}

void SrsMp4MediaBox::set_minf(SrsMp4MediaInformationBox *v)
{
    remove(SrsMp4BoxTypeMINF);
    boxes_.push_back(v);
}

SrsMp4MediaHeaderBox::SrsMp4MediaHeaderBox() : creation_time_(0), modification_time_(0), timescale_(0), duration_(0)
{
    type_ = SrsMp4BoxTypeMDHD;
    language_ = 0;
    pre_defined_ = 0;
}

SrsMp4MediaHeaderBox::~SrsMp4MediaHeaderBox()
{
}

char SrsMp4MediaHeaderBox::language0()
{
    return (char)(((language_ >> 10) & 0x1f) + 0x60);
}

void SrsMp4MediaHeaderBox::set_language0(char v)
{
    language_ |= uint16_t((uint8_t(v) - 0x60) & 0x1f) << 10;
}

char SrsMp4MediaHeaderBox::language1()
{
    return (char)(((language_ >> 5) & 0x1f) + 0x60);
}

void SrsMp4MediaHeaderBox::set_language1(char v)
{
    language_ |= uint16_t((uint8_t(v) - 0x60) & 0x1f) << 5;
}

char SrsMp4MediaHeaderBox::language2()
{
    return (char)((language_ & 0x1f) + 0x60);
}

void SrsMp4MediaHeaderBox::set_language2(char v)
{
    language_ |= uint16_t((uint8_t(v) - 0x60) & 0x1f);
}

int SrsMp4MediaHeaderBox::nb_header()
{
    int size = SrsMp4FullBox::nb_header();

    if (version_ == 1) {
        size += 8 + 8 + 4 + 8;
    } else {
        size += 4 + 4 + 4 + 4;
    }

    size += 2 + 2;

    return size;
}

srs_error_t SrsMp4MediaHeaderBox::encode_header(SrsBuffer *buf)
{
    srs_error_t err = srs_success;

    if ((err = SrsMp4FullBox::encode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "encode header");
    }

    if (version_ == 1) {
        buf->write_8bytes(creation_time_);
        buf->write_8bytes(modification_time_);
        buf->write_4bytes(timescale_);
        buf->write_8bytes(duration_);
    } else {
        buf->write_4bytes((uint32_t)creation_time_);
        buf->write_4bytes((uint32_t)modification_time_);
        buf->write_4bytes(timescale_);
        buf->write_4bytes((uint32_t)duration_);
    }

    buf->write_2bytes(language_);
    buf->write_2bytes(pre_defined_);

    return err;
}

srs_error_t SrsMp4MediaHeaderBox::decode_header(SrsBuffer *buf)
{
    srs_error_t err = srs_success;

    if ((err = SrsMp4FullBox::decode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "decode header");
    }

    if (version_ == 1) {
        creation_time_ = buf->read_8bytes();
        modification_time_ = buf->read_8bytes();
        timescale_ = buf->read_4bytes();
        duration_ = buf->read_8bytes();
    } else {
        creation_time_ = buf->read_4bytes();
        modification_time_ = buf->read_4bytes();
        timescale_ = buf->read_4bytes();
        duration_ = buf->read_4bytes();
    }

    language_ = buf->read_2bytes();
    buf->skip(2);

    return err;
}

stringstream &SrsMp4MediaHeaderBox::dumps_detail(stringstream &ss, SrsMp4DumpContext dc)
{
    SrsMp4FullBox::dumps_detail(ss, dc);

    ss << ", TBN=" << timescale_ << ", " << duration_ << "TBN";
    if (language_) {
        ss << ", LANG=" << language0() << language1() << language2();
    }
    return ss;
}

SrsMp4HandlerReferenceBox::SrsMp4HandlerReferenceBox()
{
    type_ = SrsMp4BoxTypeHDLR;

    pre_defined_ = 0;
    memset(reserved_, 0, 12);

    handler_type_ = SrsMp4HandlerTypeForbidden;
}

SrsMp4HandlerReferenceBox::~SrsMp4HandlerReferenceBox()
{
}

bool SrsMp4HandlerReferenceBox::is_video()
{
    return handler_type_ == SrsMp4HandlerTypeVIDE;
}

bool SrsMp4HandlerReferenceBox::is_audio()
{
    return handler_type_ == SrsMp4HandlerTypeSOUN;
}

int SrsMp4HandlerReferenceBox::nb_header()
{
    return SrsMp4FullBox::nb_header() + 4 + 4 + 12 + srs_mp4_string_length(name_);
}

srs_error_t SrsMp4HandlerReferenceBox::encode_header(SrsBuffer *buf)
{
    srs_error_t err = srs_success;

    if ((err = SrsMp4FullBox::encode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "encode header");
    }

    buf->write_4bytes(pre_defined_);
    buf->write_4bytes(handler_type_);
    buf->write_4bytes(reserved_[0]);
    buf->write_4bytes(reserved_[1]);
    buf->write_4bytes(reserved_[2]);
    srs_mp4_string_write(buf, name_);

    return err;
}

srs_error_t SrsMp4HandlerReferenceBox::decode_header(SrsBuffer *buf)
{
    srs_error_t err = srs_success;

    if ((err = SrsMp4FullBox::decode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "decode header");
    }

    buf->skip(4);
    handler_type_ = (SrsMp4HandlerType)buf->read_4bytes();
    buf->skip(12);

    if ((err = srs_mp4_string_read(buf, name_, left_space(buf))) != srs_success) {
        return srs_error_wrap(err, "hdlr read string");
    }

    return err;
}

stringstream &SrsMp4HandlerReferenceBox::dumps_detail(stringstream &ss, SrsMp4DumpContext dc)
{
    SrsMp4FullBox::dumps_detail(ss, dc);

    ss << ", ";
    srs_print_mp4_type(ss, (uint32_t)handler_type_);
    if (!name_.empty()) {
        ss << ", " << name_;
    }

    return ss;
}

SrsMp4MediaInformationBox::SrsMp4MediaInformationBox()
{
    type_ = SrsMp4BoxTypeMINF;
}

SrsMp4MediaInformationBox::~SrsMp4MediaInformationBox()
{
}

SrsMp4VideoMeidaHeaderBox *SrsMp4MediaInformationBox::vmhd()
{
    SrsMp4Box *box = get(SrsMp4BoxTypeVMHD);
    return dynamic_cast<SrsMp4VideoMeidaHeaderBox *>(box);
}

void SrsMp4MediaInformationBox::set_vmhd(SrsMp4VideoMeidaHeaderBox *v)
{
    remove(SrsMp4BoxTypeVMHD);
    boxes_.push_back(v);
}

SrsMp4SoundMeidaHeaderBox *SrsMp4MediaInformationBox::smhd()
{
    SrsMp4Box *box = get(SrsMp4BoxTypeSMHD);
    return dynamic_cast<SrsMp4SoundMeidaHeaderBox *>(box);
}

void SrsMp4MediaInformationBox::set_smhd(SrsMp4SoundMeidaHeaderBox *v)
{
    remove(SrsMp4BoxTypeSMHD);
    boxes_.push_back(v);
}

SrsMp4DataInformationBox *SrsMp4MediaInformationBox::dinf()
{
    SrsMp4Box *box = get(SrsMp4BoxTypeDINF);
    return dynamic_cast<SrsMp4DataInformationBox *>(box);
}

void SrsMp4MediaInformationBox::set_dinf(SrsMp4DataInformationBox *v)
{
    remove(SrsMp4BoxTypeDINF);
    boxes_.push_back(v);
}

SrsMp4SampleTableBox *SrsMp4MediaInformationBox::stbl()
{
    SrsMp4Box *box = get(SrsMp4BoxTypeSTBL);
    return dynamic_cast<SrsMp4SampleTableBox *>(box);
}

void SrsMp4MediaInformationBox::set_stbl(SrsMp4SampleTableBox *v)
{
    remove(SrsMp4BoxTypeSTBL);
    boxes_.push_back(v);
}

SrsMp4VideoMeidaHeaderBox::SrsMp4VideoMeidaHeaderBox()
{
    type_ = SrsMp4BoxTypeVMHD;
    version_ = 0;
    flags_ = 1;

    graphicsmode_ = 0;
    memset(opcolor_, 0, 6);
}

SrsMp4VideoMeidaHeaderBox::~SrsMp4VideoMeidaHeaderBox()
{
}

int SrsMp4VideoMeidaHeaderBox::nb_header()
{
    return SrsMp4FullBox::nb_header() + 2 + 6;
}

srs_error_t SrsMp4VideoMeidaHeaderBox::encode_header(SrsBuffer *buf)
{
    srs_error_t err = srs_success;

    if ((err = SrsMp4FullBox::encode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "encode header");
    }

    buf->write_2bytes(graphicsmode_);
    buf->write_2bytes(opcolor_[0]);
    buf->write_2bytes(opcolor_[1]);
    buf->write_2bytes(opcolor_[2]);

    return err;
}

srs_error_t SrsMp4VideoMeidaHeaderBox::decode_header(SrsBuffer *buf)
{
    srs_error_t err = srs_success;

    if ((err = SrsMp4FullBox::decode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "decode header");
    }

    graphicsmode_ = buf->read_2bytes();
    opcolor_[0] = buf->read_2bytes();
    opcolor_[1] = buf->read_2bytes();
    opcolor_[2] = buf->read_2bytes();

    return err;
}

SrsMp4SoundMeidaHeaderBox::SrsMp4SoundMeidaHeaderBox()
{
    type_ = SrsMp4BoxTypeSMHD;

    reserved_ = balance_ = 0;
}

SrsMp4SoundMeidaHeaderBox::~SrsMp4SoundMeidaHeaderBox()
{
}

int SrsMp4SoundMeidaHeaderBox::nb_header()
{
    return SrsMp4FullBox::nb_header() + 2 + 2;
}

srs_error_t SrsMp4SoundMeidaHeaderBox::encode_header(SrsBuffer *buf)
{
    srs_error_t err = srs_success;

    if ((err = SrsMp4FullBox::encode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "encode header");
    }

    buf->write_2bytes(balance_);
    buf->write_2bytes(reserved_);

    return err;
}

srs_error_t SrsMp4SoundMeidaHeaderBox::decode_header(SrsBuffer *buf)
{
    srs_error_t err = srs_success;

    if ((err = SrsMp4FullBox::decode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "decode header");
    }

    balance_ = buf->read_2bytes();
    buf->skip(2);

    return err;
}

SrsMp4DataInformationBox::SrsMp4DataInformationBox()
{
    type_ = SrsMp4BoxTypeDINF;
}

SrsMp4DataInformationBox::~SrsMp4DataInformationBox()
{
}

SrsMp4DataReferenceBox *SrsMp4DataInformationBox::dref()
{
    SrsMp4Box *box = get(SrsMp4BoxTypeDREF);
    return dynamic_cast<SrsMp4DataReferenceBox *>(box);
}

void SrsMp4DataInformationBox::set_dref(SrsMp4DataReferenceBox *v)
{
    remove(SrsMp4BoxTypeDREF);
    boxes_.push_back(v);
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
    type_ = SrsMp4BoxTypeURL;
}

SrsMp4DataEntryUrlBox::~SrsMp4DataEntryUrlBox()
{
}

int SrsMp4DataEntryUrlBox::nb_header()
{
    return SrsMp4FullBox::nb_header() + srs_mp4_string_length(location_);
}

srs_error_t SrsMp4DataEntryUrlBox::encode_header(SrsBuffer *buf)
{
    srs_error_t err = srs_success;

    // a 24-bit integer with flags; one flag is defined (x000001) which means that the media
    // data is in the same file as the Movie Box containing this data reference.
    if (location_.empty()) {
        flags_ = 0x01;
    }

    if ((err = SrsMp4FullBox::encode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "encode header");
    }

    srs_mp4_string_write(buf, location_);

    return err;
}

srs_error_t SrsMp4DataEntryUrlBox::decode_header(SrsBuffer *buf)
{
    srs_error_t err = srs_success;

    if ((err = SrsMp4FullBox::decode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "decode header");
    }

    if ((err = srs_mp4_string_read(buf, location_, left_space(buf))) != srs_success) {
        return srs_error_wrap(err, "url read location");
    }

    return err;
}

stringstream &SrsMp4DataEntryUrlBox::dumps_detail(stringstream &ss, SrsMp4DumpContext dc)
{
    SrsMp4FullBox::dumps_detail(ss, dc);

    ss << ", URL: " << location_;
    if (location_.empty()) {
        ss << "Same file";
    }
    return ss;
}

SrsMp4DataEntryUrnBox::SrsMp4DataEntryUrnBox()
{
    type_ = SrsMp4BoxTypeURN;
}

SrsMp4DataEntryUrnBox::~SrsMp4DataEntryUrnBox()
{
}

int SrsMp4DataEntryUrnBox::nb_header()
{
    return SrsMp4FullBox::nb_header() + srs_mp4_string_length(location_) + srs_mp4_string_length(name_);
}

srs_error_t SrsMp4DataEntryUrnBox::encode_header(SrsBuffer *buf)
{
    srs_error_t err = srs_success;

    // a 24-bit integer with flags; one flag is defined (x000001) which means that the media
    // data is in the same file as the Movie Box containing this data reference.
    if (location_.empty()) {
        flags_ = 0x01;
    }

    if ((err = SrsMp4DataEntryBox::encode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "encode entry");
    }

    srs_mp4_string_write(buf, location_);
    srs_mp4_string_write(buf, name_);

    return err;
}

srs_error_t SrsMp4DataEntryUrnBox::decode_header(SrsBuffer *buf)
{
    srs_error_t err = srs_success;

    if ((err = SrsMp4DataEntryBox::decode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "decode entry");
    }

    if ((err = srs_mp4_string_read(buf, location_, left_space(buf))) != srs_success) {
        return srs_error_wrap(err, "urn read location");
    }

    if ((err = srs_mp4_string_read(buf, name_, left_space(buf))) != srs_success) {
        return srs_error_wrap(err, "urn read name");
    }

    return err;
}

stringstream &SrsMp4DataEntryUrnBox::dumps_detail(stringstream &ss, SrsMp4DumpContext dc)
{
    SrsMp4FullBox::dumps_detail(ss, dc);

    ss << ", URL: " << location_;
    if (location_.empty()) {
        ss << "Same file";
    }
    if (!name_.empty()) {
        ss << ", " << name_;
    }

    return ss;
}

SrsMp4DataReferenceBox::SrsMp4DataReferenceBox()
{
    type_ = SrsMp4BoxTypeDREF;
}

SrsMp4DataReferenceBox::~SrsMp4DataReferenceBox()
{
    vector<SrsMp4DataEntryBox *>::iterator it;
    for (it = entries_.begin(); it != entries_.end(); ++it) {
        SrsMp4DataEntryBox *entry = *it;
        srs_freep(entry);
    }
    entries_.clear();
}

uint32_t SrsMp4DataReferenceBox::entry_count()
{
    return (uint32_t)entries_.size();
}

SrsMp4DataEntryBox *SrsMp4DataReferenceBox::entry_at(int index)
{
    return entries_.at(index);
}

// Note that box must be SrsMp4DataEntryBox*
void SrsMp4DataReferenceBox::append(SrsMp4Box *box)
{
    entries_.push_back((SrsMp4DataEntryBox *)box);
}

int SrsMp4DataReferenceBox::nb_header()
{
    int size = SrsMp4FullBox::nb_header();

    size += 4;

    vector<SrsMp4DataEntryBox *>::iterator it;
    for (it = entries_.begin(); it != entries_.end(); ++it) {
        SrsMp4DataEntryBox *entry = *it;
        size += entry->nb_bytes();
    }

    return size;
}

srs_error_t SrsMp4DataReferenceBox::encode_header(SrsBuffer *buf)
{
    srs_error_t err = srs_success;

    if ((err = SrsMp4FullBox::encode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "encode header");
    }

    buf->write_4bytes((int32_t)entries_.size());

    vector<SrsMp4DataEntryBox *>::iterator it;
    for (it = entries_.begin(); it != entries_.end(); ++it) {
        SrsMp4DataEntryBox *entry = *it;
        if ((err = entry->encode(buf)) != srs_success) {
            return srs_error_wrap(err, "encode entry");
        }
    }

    return err;
}

srs_error_t SrsMp4DataReferenceBox::decode_header(SrsBuffer *buf)
{
    srs_error_t err = srs_success;

    if ((err = SrsMp4FullBox::decode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "decode header");
    }

    uint32_t nb_entries = buf->read_4bytes();
    for (uint32_t i = 0; i < nb_entries; i++) {
        SrsMp4Box *box = NULL;
        if ((err = SrsMp4Box::discovery(buf, &box)) != srs_success) {
            return srs_error_wrap(err, "discovery box");
        }

        if ((err = box->decode(buf)) != srs_success) {
            return srs_error_wrap(err, "decode box");
        }

        SrsMp4FullBox *fbox = dynamic_cast<SrsMp4FullBox *>(box);
        if (fbox) {
            fbox->version_ = version_;
            fbox->flags_ = flags_;
        }

        if (box->type_ == SrsMp4BoxTypeURL) {
            entries_.push_back(dynamic_cast<SrsMp4DataEntryUrlBox *>(box));
        } else if (box->type_ == SrsMp4BoxTypeURN) {
            entries_.push_back(dynamic_cast<SrsMp4DataEntryUrnBox *>(box));
        } else {
            srs_freep(box);
        }
    }

    return err;
}

stringstream &SrsMp4DataReferenceBox::dumps_detail(stringstream &ss, SrsMp4DumpContext dc)
{
    SrsMp4FullBox::dumps_detail(ss, dc);

    ss << ", " << entries_.size() << " childs";
    if (!entries_.empty()) {
        ss << "(+)" << endl;
        srs_dumps_array(entries_, ss, dc.indent(), srs_mp4_pfn_box2, srs_mp4_delimiter_newline);
    }
    return ss;
}

SrsMp4SampleTableBox::SrsMp4SampleTableBox()
{
    type_ = SrsMp4BoxTypeSTBL;
}

SrsMp4SampleTableBox::~SrsMp4SampleTableBox()
{
}

SrsMp4SampleDescriptionBox *SrsMp4SampleTableBox::stsd()
{
    SrsMp4Box *box = get(SrsMp4BoxTypeSTSD);
    return dynamic_cast<SrsMp4SampleDescriptionBox *>(box);
}

void SrsMp4SampleTableBox::set_stsd(SrsMp4SampleDescriptionBox *v)
{
    remove(SrsMp4BoxTypeSTSD);
    boxes_.push_back(v);
}

SrsMp4ChunkOffsetBox *SrsMp4SampleTableBox::stco()
{
    SrsMp4Box *box = get(SrsMp4BoxTypeSTCO);
    return dynamic_cast<SrsMp4ChunkOffsetBox *>(box);
}

void SrsMp4SampleTableBox::set_stco(SrsMp4ChunkOffsetBox *v)
{
    remove(SrsMp4BoxTypeSTCO);
    boxes_.push_back(v);
}

SrsMp4ChunkLargeOffsetBox *SrsMp4SampleTableBox::co64()
{
    SrsMp4Box *box = get(SrsMp4BoxTypeCO64);
    return dynamic_cast<SrsMp4ChunkLargeOffsetBox *>(box);
}

void SrsMp4SampleTableBox::set_co64(SrsMp4ChunkLargeOffsetBox *v)
{
    remove(SrsMp4BoxTypeCO64);
    boxes_.push_back(v);
}

SrsMp4SampleSizeBox *SrsMp4SampleTableBox::stsz()
{
    SrsMp4Box *box = get(SrsMp4BoxTypeSTSZ);
    return dynamic_cast<SrsMp4SampleSizeBox *>(box);
}

void SrsMp4SampleTableBox::set_stsz(SrsMp4SampleSizeBox *v)
{
    remove(SrsMp4BoxTypeSTSZ);
    boxes_.push_back(v);
}

SrsMp4Sample2ChunkBox *SrsMp4SampleTableBox::stsc()
{
    SrsMp4Box *box = get(SrsMp4BoxTypeSTSC);
    return dynamic_cast<SrsMp4Sample2ChunkBox *>(box);
}

void SrsMp4SampleTableBox::set_stsc(SrsMp4Sample2ChunkBox *v)
{
    remove(SrsMp4BoxTypeSTSC);
    boxes_.push_back(v);
}

SrsMp4DecodingTime2SampleBox *SrsMp4SampleTableBox::stts()
{
    SrsMp4Box *box = get(SrsMp4BoxTypeSTTS);
    return dynamic_cast<SrsMp4DecodingTime2SampleBox *>(box);
}

void SrsMp4SampleTableBox::set_stts(SrsMp4DecodingTime2SampleBox *v)
{
    remove(SrsMp4BoxTypeSTTS);
    boxes_.push_back(v);
}

SrsMp4CompositionTime2SampleBox *SrsMp4SampleTableBox::ctts()
{
    SrsMp4Box *box = get(SrsMp4BoxTypeCTTS);
    return dynamic_cast<SrsMp4CompositionTime2SampleBox *>(box);
}

void SrsMp4SampleTableBox::set_ctts(SrsMp4CompositionTime2SampleBox *v)
{
    remove(SrsMp4BoxTypeCTTS);
    boxes_.push_back(v);
}

SrsMp4SyncSampleBox *SrsMp4SampleTableBox::stss()
{
    SrsMp4Box *box = get(SrsMp4BoxTypeSTSS);
    return dynamic_cast<SrsMp4SyncSampleBox *>(box);
}

void SrsMp4SampleTableBox::set_stss(SrsMp4SyncSampleBox *v)
{
    remove(SrsMp4BoxTypeSTSS);
    boxes_.push_back(v);
}

int SrsMp4SampleTableBox::nb_header()
{
    return SrsMp4Box::nb_header();
}

srs_error_t SrsMp4SampleTableBox::encode_header(SrsBuffer *buf)
{
    srs_error_t err = srs_success;

    if ((err = SrsMp4Box::encode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "encode header");
    }

    return err;
}

srs_error_t SrsMp4SampleTableBox::decode_header(SrsBuffer *buf)
{
    srs_error_t err = srs_success;

    if ((err = SrsMp4Box::decode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "decode header");
    }

    return err;
}

SrsMp4SampleEntry::SrsMp4SampleEntry() : data_reference_index_(0)
{
    memset(reserved, 0, 6);
}

SrsMp4SampleEntry::~SrsMp4SampleEntry()
{
}

int SrsMp4SampleEntry::nb_header()
{
    return SrsMp4Box::nb_header() + 6 + 2;
}

srs_error_t SrsMp4SampleEntry::encode_header(SrsBuffer *buf)
{
    srs_error_t err = srs_success;

    if ((err = SrsMp4Box::encode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "encode header");
    }

    for (int i = 0; i < 6; i++) {
        buf->write_1bytes(reserved[i]);
    }
    buf->write_2bytes(data_reference_index_);

    return err;
}

srs_error_t SrsMp4SampleEntry::decode_header(SrsBuffer *buf)
{
    srs_error_t err = srs_success;

    if ((err = SrsMp4Box::decode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "decode header");
    }

    buf->skip(6);
    data_reference_index_ = buf->read_2bytes();

    return err;
}

stringstream &SrsMp4SampleEntry::dumps_detail(stringstream &ss, SrsMp4DumpContext dc)
{
    SrsMp4Box::dumps_detail(ss, dc);

    ss << ", refs#" << data_reference_index_;
    return ss;
}

SrsMp4VisualSampleEntry::SrsMp4VisualSampleEntry(SrsMp4BoxType boxType) : width_(0), height_(0)
{
    type_ = boxType;

    pre_defined0_ = 0;
    reserved0_ = 0;
    reserved1_ = 0;
    memset(pre_defined1_, 0, 12);
    memset(compressorname_, 0, 32);
    frame_count_ = 1;
    horizresolution_ = 0x00480000; // 72 dpi
    vertresolution_ = 0x00480000;  // 72 dpi
    depth_ = 0x0018;
    pre_defined2_ = -1;
}

SrsMp4VisualSampleEntry::~SrsMp4VisualSampleEntry()
{
}

SrsMp4AvccBox *SrsMp4VisualSampleEntry::avcC()
{
    SrsMp4Box *box = get(SrsMp4BoxTypeAVCC);
    return dynamic_cast<SrsMp4AvccBox *>(box);
}

void SrsMp4VisualSampleEntry::set_avcC(SrsMp4AvccBox *v)
{
    remove(SrsMp4BoxTypeAVCC);
    boxes_.push_back(v);
}

SrsMp4HvcCBox *SrsMp4VisualSampleEntry::hvcC()
{
    SrsMp4Box *box = get(SrsMp4BoxTypeHVCC);
    return dynamic_cast<SrsMp4HvcCBox *>(box);
}

void SrsMp4VisualSampleEntry::set_hvcC(SrsMp4HvcCBox *v)
{
    remove(SrsMp4BoxTypeHVCC);
    boxes_.push_back(v);
}

int SrsMp4VisualSampleEntry::nb_header()
{
    return SrsMp4SampleEntry::nb_header() + 2 + 2 + 12 + 2 + 2 + 4 + 4 + 4 + 2 + 32 + 2 + 2;
}

srs_error_t SrsMp4VisualSampleEntry::encode_header(SrsBuffer *buf)
{
    srs_error_t err = srs_success;

    if ((err = SrsMp4SampleEntry::encode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "encode entry");
    }

    buf->write_2bytes(pre_defined0_);
    buf->write_2bytes(reserved0_);
    buf->write_4bytes(pre_defined1_[0]);
    buf->write_4bytes(pre_defined1_[1]);
    buf->write_4bytes(pre_defined1_[2]);
    buf->write_2bytes(width_);
    buf->write_2bytes(height_);
    buf->write_4bytes(horizresolution_);
    buf->write_4bytes(vertresolution_);
    buf->write_4bytes(reserved1_);
    buf->write_2bytes(frame_count_);
    buf->write_bytes(compressorname_, 32);
    buf->write_2bytes(depth_);
    buf->write_2bytes(pre_defined2_);

    return err;
}

srs_error_t SrsMp4VisualSampleEntry::decode_header(SrsBuffer *buf)
{
    srs_error_t err = srs_success;

    if ((err = SrsMp4SampleEntry::decode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "decode entry");
    }

    buf->skip(2);
    buf->skip(2);
    buf->skip(12);
    width_ = buf->read_2bytes();
    height_ = buf->read_2bytes();
    horizresolution_ = buf->read_4bytes();
    vertresolution_ = buf->read_4bytes();
    buf->skip(4);
    frame_count_ = buf->read_2bytes();
    buf->read_bytes(compressorname_, 32);
    depth_ = buf->read_2bytes();
    buf->skip(2);

    return err;
}

stringstream &SrsMp4VisualSampleEntry::dumps_detail(stringstream &ss, SrsMp4DumpContext dc)
{
    SrsMp4SampleEntry::dumps_detail(ss, dc);

    ss << ", size=" << width_ << "x" << height_;
    return ss;
}

SrsMp4AvccBox::SrsMp4AvccBox()
{
    type_ = SrsMp4BoxTypeAVCC;
}

SrsMp4AvccBox::~SrsMp4AvccBox()
{
}

int SrsMp4AvccBox::nb_header()
{
    return SrsMp4Box::nb_header() + (int)avc_config_.size();
}

srs_error_t SrsMp4AvccBox::encode_header(SrsBuffer *buf)
{
    srs_error_t err = srs_success;

    if ((err = SrsMp4Box::encode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "encode header");
    }

    if (!avc_config_.empty()) {
        buf->write_bytes(&avc_config_[0], (int)avc_config_.size());
    }

    return err;
}

srs_error_t SrsMp4AvccBox::decode_header(SrsBuffer *buf)
{
    srs_error_t err = srs_success;

    if ((err = SrsMp4Box::decode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "decode header");
    }

    int nb_config = left_space(buf);
    if (nb_config) {
        avc_config_.resize(nb_config);
        buf->read_bytes(&avc_config_[0], nb_config);
    }

    return err;
}

stringstream &SrsMp4AvccBox::dumps_detail(stringstream &ss, SrsMp4DumpContext dc)
{
    SrsMp4Box::dumps_detail(ss, dc);

    ss << ", AVC Config: " << (int)avc_config_.size() << "B" << endl;
    srs_mp4_padding(ss, dc.indent());
    srs_mp4_print_bytes(ss, (const char *)&avc_config_[0], (int)avc_config_.size(), dc.indent());
    return ss;
}

SrsMp4HvcCBox::SrsMp4HvcCBox()
{
    type_ = SrsMp4BoxTypeHVCC;
}

SrsMp4HvcCBox::~SrsMp4HvcCBox()
{
}

int SrsMp4HvcCBox::nb_header()
{
    return SrsMp4Box::nb_header() + (int)hevc_config_.size();
}

srs_error_t SrsMp4HvcCBox::encode_header(SrsBuffer *buf)
{
    srs_error_t err = srs_success;

    if ((err = SrsMp4Box::encode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "encode header");
    }

    if (!hevc_config_.empty()) {
        buf->write_bytes(&hevc_config_[0], (int)hevc_config_.size());
    }

    return err;
}

srs_error_t SrsMp4HvcCBox::decode_header(SrsBuffer *buf)
{
    srs_error_t err = srs_success;

    if ((err = SrsMp4Box::decode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "decode header");
    }

    int nb_config = left_space(buf);
    if (nb_config) {
        hevc_config_.resize(nb_config);
        buf->read_bytes(&hevc_config_[0], nb_config);
    }

    return err;
}

stringstream &SrsMp4HvcCBox::dumps_detail(stringstream &ss, SrsMp4DumpContext dc)
{
    SrsMp4Box::dumps_detail(ss, dc);

    ss << ", HEVC Config: " << (int)hevc_config_.size() << "B" << endl;
    srs_mp4_padding(ss, dc.indent());
    srs_mp4_print_bytes(ss, (const char *)&hevc_config_[0], (int)hevc_config_.size(), dc.indent());
    return ss;
}

SrsMp4AudioSampleEntry::SrsMp4AudioSampleEntry() : samplerate_(0)
{
    type_ = SrsMp4BoxTypeMP4A;

    reserved0_ = 0;
    pre_defined0_ = 0;
    reserved1_ = 0;
    channelcount_ = 2;
    samplesize_ = 16;
}

SrsMp4AudioSampleEntry::~SrsMp4AudioSampleEntry()
{
}

SrsMp4EsdsBox *SrsMp4AudioSampleEntry::esds()
{
    SrsMp4Box *box = get(SrsMp4BoxTypeESDS);
    return dynamic_cast<SrsMp4EsdsBox *>(box);
}

void SrsMp4AudioSampleEntry::set_esds(SrsMp4EsdsBox *v)
{
    remove(SrsMp4BoxTypeESDS);
    boxes_.push_back(v);
}

SrsMp4DecoderSpecificInfo *SrsMp4AudioSampleEntry::asc()
{
    SrsMp4EsdsBox *box = esds();
    return box ? box->asc() : NULL;
}

int SrsMp4AudioSampleEntry::nb_header()
{
    return SrsMp4SampleEntry::nb_header() + 8 + 2 + 2 + 2 + 2 + 4;
}

srs_error_t SrsMp4AudioSampleEntry::encode_header(SrsBuffer *buf)
{
    srs_error_t err = srs_success;

    if ((err = SrsMp4SampleEntry::encode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "encode entry");
    }

    buf->write_8bytes(reserved0_);
    buf->write_2bytes(channelcount_);
    buf->write_2bytes(samplesize_);
    buf->write_2bytes(pre_defined0_);
    buf->write_2bytes(reserved1_);
    buf->write_4bytes(samplerate_);

    return err;
}

srs_error_t SrsMp4AudioSampleEntry::decode_header(SrsBuffer *buf)
{
    srs_error_t err = srs_success;

    if ((err = SrsMp4SampleEntry::decode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "decode entry");
    }

    buf->skip(8);
    channelcount_ = buf->read_2bytes();
    samplesize_ = buf->read_2bytes();
    buf->skip(2);
    buf->skip(2);
    samplerate_ = buf->read_4bytes();

    return err;
}

stringstream &SrsMp4AudioSampleEntry::dumps_detail(stringstream &ss, SrsMp4DumpContext dc)
{
    SrsMp4SampleEntry::dumps_detail(ss, dc);

    ss << ", " << channelcount_ << " channels, " << samplesize_ << " bits"
       << ", " << (samplerate_ >> 16) << " Hz";
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

int SrsMp4BaseDescriptor::left_space(SrsBuffer *buf)
{
    int left = vlen - (buf->pos() - start_pos);
    return srs_max(0, left);
}

uint64_t SrsMp4BaseDescriptor::nb_bytes()
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

srs_error_t SrsMp4BaseDescriptor::encode(SrsBuffer *buf)
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
        buf->write_1bytes(uint8_t(length >> 21) | 0x80);
    }
    if (length > 0x3fff) {
        buf->write_1bytes(uint8_t(length >> 14) | 0x80);
    }
    if (length > 0x7f) {
        buf->write_1bytes(uint8_t(length >> 7) | 0x80);
    }
    buf->write_1bytes(length & 0x7f);

    if ((err = encode_payload(buf)) != srs_success) {
        return srs_error_wrap(err, "encode payload");
    }

    return err;
}

srs_error_t SrsMp4BaseDescriptor::decode(SrsBuffer *buf)
{
    srs_error_t err = srs_success;

    int size = nb_bytes();
    if (!buf->require(size)) {
        return srs_error_new(ERROR_MP4_BOX_REQUIRE_SPACE, "requires %d only %d bytes", size, buf->left());
    }

    tag = (SrsMp4ESTagEs)buf->read_1bytes();

    uint8_t v = 0x80;
    int32_t length = 0x00;
    while ((v & 0x80) == 0x80) {
        if (!buf->require(1)) {
            return srs_error_new(ERROR_MP4_BOX_REQUIRE_SPACE, "ES requires 1 only %d bytes", buf->left());
        }
        v = buf->read_1bytes();

        length = (length << 7) | (v & 0x7f);
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

stringstream &SrsMp4BaseDescriptor::dumps_detail(stringstream &ss, SrsMp4DumpContext dc)
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
    return (int)asc_.size();
}

srs_error_t SrsMp4DecoderSpecificInfo::encode_payload(SrsBuffer *buf)
{
    srs_error_t err = srs_success;

    if (!asc_.empty()) {
        buf->write_bytes(&asc_[0], (int)asc_.size());
    }

    return err;
}

srs_error_t SrsMp4DecoderSpecificInfo::decode_payload(SrsBuffer *buf)
{
    srs_error_t err = srs_success;

    int nb_asc = vlen;
    if (nb_asc) {
        asc_.resize(nb_asc);
        buf->read_bytes(&asc_[0], nb_asc);
    }

    return err;
}

stringstream &SrsMp4DecoderSpecificInfo::dumps_detail(stringstream &ss, SrsMp4DumpContext dc)
{
    SrsMp4BaseDescriptor::dumps_detail(ss, dc);

    ss << ", ASC " << asc_.size() << "B";

    ss << endl;
    srs_mp4_padding(ss, dc.indent());
    return srs_mp4_print_bytes(ss, (const char *)&asc_[0], (int)asc_.size(), dc.indent());
}

SrsMp4DecoderConfigDescriptor::SrsMp4DecoderConfigDescriptor() : upStream(0), bufferSizeDB_(0), maxBitrate_(0), avgBitrate_(0)
{
    tag = SrsMp4ESTagESDecoderConfigDescrTag;
    objectTypeIndication = SrsMp4ObjectTypeForbidden;
    streamType = SrsMp4StreamTypeForbidden;
    decSpecificInfo_ = NULL;
    reserved = 1;
}

SrsMp4DecoderConfigDescriptor::~SrsMp4DecoderConfigDescriptor()
{
    srs_freep(decSpecificInfo_);
}

int32_t SrsMp4DecoderConfigDescriptor::nb_payload()
{
    return 13 + (decSpecificInfo_ ? decSpecificInfo_->nb_bytes() : 0);
}

srs_error_t SrsMp4DecoderConfigDescriptor::encode_payload(SrsBuffer *buf)
{
    srs_error_t err = srs_success;

    buf->write_1bytes(objectTypeIndication);

    uint8_t v = reserved;
    v |= (upStream & 0x01) << 1;
    v |= uint8_t(streamType & 0x3f) << 2;
    buf->write_1bytes(v);

    buf->write_3bytes(bufferSizeDB_);
    buf->write_4bytes(maxBitrate_);
    buf->write_4bytes(avgBitrate_);

    if (decSpecificInfo_ && (err = decSpecificInfo_->encode(buf)) != srs_success) {
        return srs_error_wrap(err, "encode des specific info");
    }

    return err;
}

srs_error_t SrsMp4DecoderConfigDescriptor::decode_payload(SrsBuffer *buf)
{
    srs_error_t err = srs_success;

    objectTypeIndication = (SrsMp4ObjectType)buf->read_1bytes();

    uint8_t v = buf->read_1bytes();
    upStream = (v >> 1) & 0x01;
    streamType = (SrsMp4StreamType)((v >> 2) & 0x3f);
    reserved = v & 0x01;

    bufferSizeDB_ = buf->read_3bytes();
    maxBitrate_ = buf->read_4bytes();
    avgBitrate_ = buf->read_4bytes();

    int left = left_space(buf);
    if (left > 0) {
        decSpecificInfo_ = new SrsMp4DecoderSpecificInfo();
        if ((err = decSpecificInfo_->decode(buf)) != srs_success) {
            return srs_error_wrap(err, "decode dec specific info");
        }
    }

    return err;
}

stringstream &SrsMp4DecoderConfigDescriptor::dumps_detail(stringstream &ss, SrsMp4DumpContext dc)
{
    SrsMp4BaseDescriptor::dumps_detail(ss, dc);

    ss << ", type=" << objectTypeIndication << ", stream=" << streamType;

    ss << endl;
    srs_mp4_padding(ss, dc.indent());

    ss << "decoder specific";
    if (decSpecificInfo_) {
        decSpecificInfo_->dumps_detail(ss, dc.indent());
    }

    return ss;
}

SrsMp4SLConfigDescriptor::SrsMp4SLConfigDescriptor()
{
    tag = SrsMp4ESTagESSLConfigDescrTag;
    predefined_ = 2;
}

SrsMp4SLConfigDescriptor::~SrsMp4SLConfigDescriptor()
{
}

int32_t SrsMp4SLConfigDescriptor::nb_payload()
{
    return 1;
}

srs_error_t SrsMp4SLConfigDescriptor::encode_payload(SrsBuffer *buf)
{
    srs_error_t err = srs_success;

    buf->write_1bytes(predefined_);

    return err;
}

srs_error_t SrsMp4SLConfigDescriptor::decode_payload(SrsBuffer *buf)
{
    srs_error_t err = srs_success;

    predefined_ = buf->read_1bytes();

    // TODO: FIXME: To support complete SL Config.
    if (predefined_ != 0x02) {
        return srs_error_new(ERROR_MP4_ESDS_SL_Config, "illegal ESDS SL Config, predefined=%d", predefined_);
    }

    return err;
}

SrsMp4ES_Descriptor::SrsMp4ES_Descriptor() : ES_ID_(0), dependsOn_ES_ID_(0), OCR_ES_Id_(0)
{
    tag = SrsMp4ESTagESDescrTag;
    streamPriority_ = streamDependenceFlag_ = URL_Flag_ = OCRstreamFlag_ = 0;
}

SrsMp4ES_Descriptor::~SrsMp4ES_Descriptor()
{
}

int32_t SrsMp4ES_Descriptor::nb_payload()
{
    int size = 2 + 1;
    size += streamDependenceFlag_ ? 2 : 0;
    if (URL_Flag_) {
        size += 1 + URLstring_.size();
    }
    size += OCRstreamFlag_ ? 2 : 0;
    size += decConfigDescr_.nb_bytes() + slConfigDescr_.nb_bytes();
    return size;
}

srs_error_t SrsMp4ES_Descriptor::encode_payload(SrsBuffer *buf)
{
    srs_error_t err = srs_success;

    buf->write_2bytes(ES_ID_);

    uint8_t v = streamPriority_ & 0x1f;
    v |= (streamDependenceFlag_ & 0x01) << 7;
    v |= (URL_Flag_ & 0x01) << 6;
    v |= (OCRstreamFlag_ & 0x01) << 5;
    buf->write_1bytes(v);

    if (streamDependenceFlag_) {
        buf->write_2bytes(dependsOn_ES_ID_);
    }

    if (URL_Flag_ && !URLstring_.empty()) {
        buf->write_1bytes(URLstring_.size());
        buf->write_bytes(&URLstring_[0], (int)URLstring_.size());
    }

    if (OCRstreamFlag_) {
        buf->write_2bytes(OCR_ES_Id_);
    }

    if ((err = decConfigDescr_.encode(buf)) != srs_success) {
        return srs_error_wrap(err, "encode dec config");
    }

    if ((err = slConfigDescr_.encode(buf)) != srs_success) {
        return srs_error_wrap(err, "encode sl config");
    }

    return err;
}

srs_error_t SrsMp4ES_Descriptor::decode_payload(SrsBuffer *buf)
{
    srs_error_t err = srs_success;

    ES_ID_ = buf->read_2bytes();

    uint8_t v = buf->read_1bytes();
    streamPriority_ = v & 0x1f;
    streamDependenceFlag_ = (v >> 7) & 0x01;
    URL_Flag_ = (v >> 6) & 0x01;
    OCRstreamFlag_ = (v >> 5) & 0x01;

    if (streamDependenceFlag_) {
        if (!buf->require(2)) {
            return srs_error_new(ERROR_MP4_BOX_REQUIRE_SPACE, "ES requires 2 only %d bytes", buf->left());
        }
        dependsOn_ES_ID_ = buf->read_2bytes();
    }

    if (URL_Flag_) {
        if (!buf->require(1)) {
            return srs_error_new(ERROR_MP4_BOX_REQUIRE_SPACE, "URLlength requires 1 only %d bytes", buf->left());
        }
        uint8_t URLlength = buf->read_1bytes();

        if (!buf->require(URLlength)) {
            return srs_error_new(ERROR_MP4_BOX_REQUIRE_SPACE, "URL requires %d only %d bytes", URLlength, buf->left());
        }
        URLstring_.resize(URLlength);
        buf->read_bytes(&URLstring_[0], URLlength);
    }

    if (OCRstreamFlag_) {
        if (!buf->require(2)) {
            return srs_error_new(ERROR_MP4_BOX_REQUIRE_SPACE, "OCR requires 2 only %d bytes", buf->left());
        }
        OCR_ES_Id_ = buf->read_2bytes();
    }

    if ((err = decConfigDescr_.decode(buf)) != srs_success) {
        return srs_error_wrap(err, "decode dec config");
    }

    if ((err = slConfigDescr_.decode(buf)) != srs_success) {
        return srs_error_wrap(err, "decode sl config");
    }

    return err;
}

stringstream &SrsMp4ES_Descriptor::dumps_detail(stringstream &ss, SrsMp4DumpContext dc)
{
    SrsMp4BaseDescriptor::dumps_detail(ss, dc);

    ss << ", ID=" << ES_ID_;

    ss << endl;
    srs_mp4_padding(ss, dc.indent());

    ss << "decoder config";
    decConfigDescr_.dumps_detail(ss, dc.indent());
    return ss;
}

SrsMp4EsdsBox::SrsMp4EsdsBox()
{
    type_ = SrsMp4BoxTypeESDS;
    es = new SrsMp4ES_Descriptor();
}

SrsMp4EsdsBox::~SrsMp4EsdsBox()
{
    srs_freep(es);
}

SrsMp4DecoderSpecificInfo *SrsMp4EsdsBox::asc()
{
    return es->decConfigDescr_.decSpecificInfo_;
}

int SrsMp4EsdsBox::nb_header()
{
    return SrsMp4FullBox::nb_header() + es->nb_bytes();
}

srs_error_t SrsMp4EsdsBox::encode_header(SrsBuffer *buf)
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

srs_error_t SrsMp4EsdsBox::decode_header(SrsBuffer *buf)
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

// LCOV_EXCL_START
stringstream &SrsMp4EsdsBox::dumps_detail(stringstream &ss, SrsMp4DumpContext dc)
{
    SrsMp4FullBox::dumps_detail(ss, dc);

    return es->dumps_detail(ss, dc);
}
// LCOV_EXCL_STOP

SrsMp4SampleDescriptionBox::SrsMp4SampleDescriptionBox()
{
    type_ = SrsMp4BoxTypeSTSD;
}

SrsMp4SampleDescriptionBox::~SrsMp4SampleDescriptionBox()
{
    vector<SrsMp4SampleEntry *>::iterator it;
    for (it = entries_.begin(); it != entries_.end(); ++it) {
        SrsMp4SampleEntry *entry = *it;
        srs_freep(entry);
    }
    entries_.clear();
}

SrsMp4VisualSampleEntry *SrsMp4SampleDescriptionBox::avc1()
{
    vector<SrsMp4SampleEntry *>::iterator it;
    for (it = entries_.begin(); it != entries_.end(); ++it) {
        SrsMp4SampleEntry *entry = *it;
        if (entry->type_ == SrsMp4BoxTypeAVC1) {
            return dynamic_cast<SrsMp4VisualSampleEntry *>(entry);
        }
    }
    return NULL;
}

SrsMp4AudioSampleEntry *SrsMp4SampleDescriptionBox::mp4a()
{
    vector<SrsMp4SampleEntry *>::iterator it;
    for (it = entries_.begin(); it != entries_.end(); ++it) {
        SrsMp4SampleEntry *entry = *it;
        if (entry->type_ == SrsMp4BoxTypeMP4A) {
            return dynamic_cast<SrsMp4AudioSampleEntry *>(entry);
        }
    }
    return NULL;
}

uint32_t SrsMp4SampleDescriptionBox::entry_count()
{
    return (uint32_t)entries_.size();
}

SrsMp4SampleEntry *SrsMp4SampleDescriptionBox::entrie_at(int index)
{
    return entries_.at(index);
}

// Note that box must be SrsMp4SampleEntry*
void SrsMp4SampleDescriptionBox::append(SrsMp4Box *box)
{
    entries_.push_back((SrsMp4SampleEntry *)box);
}

int SrsMp4SampleDescriptionBox::nb_header()
{
    int size = SrsMp4FullBox::nb_header();

    size += 4;

    vector<SrsMp4SampleEntry *>::iterator it;
    for (it = entries_.begin(); it != entries_.end(); ++it) {
        SrsMp4SampleEntry *entry = *it;
        size += entry->nb_bytes();
    }

    return size;
}

srs_error_t SrsMp4SampleDescriptionBox::encode_header(SrsBuffer *buf)
{
    srs_error_t err = srs_success;

    if ((err = SrsMp4FullBox::encode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "encode header");
    }

    buf->write_4bytes(entry_count());

    vector<SrsMp4SampleEntry *>::iterator it;
    for (it = entries_.begin(); it != entries_.end(); ++it) {
        SrsMp4SampleEntry *entry = *it;
        if ((err = entry->encode(buf)) != srs_success) {
            return srs_error_wrap(err, "encode entry");
        }
    }

    return err;
}

srs_error_t SrsMp4SampleDescriptionBox::decode_header(SrsBuffer *buf)
{
    srs_error_t err = srs_success;

    if ((err = SrsMp4FullBox::decode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "decode header");
    }

    uint32_t nb_entries = buf->read_4bytes();
    for (uint32_t i = 0; i < nb_entries; i++) {
        SrsMp4Box *box = NULL;
        if ((err = SrsMp4Box::discovery(buf, &box)) != srs_success) {
            return srs_error_wrap(err, "discovery box");
        }

        if ((err = box->decode(buf)) != srs_success) {
            return srs_error_wrap(err, "decode box");
        }

        SrsMp4SampleEntry *entry = dynamic_cast<SrsMp4SampleEntry *>(box);
        if (entry) {
            entries_.push_back(entry);
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

stringstream &SrsMp4SampleDescriptionBox::dumps_detail(stringstream &ss, SrsMp4DumpContext dc)
{
    SrsMp4FullBox::dumps_detail(ss, dc);

    ss << ", " << entries_.size() << " childs";
    if (!entries_.empty()) {
        ss << "(+)" << endl;
        srs_dumps_array(entries_, ss, dc.indent(), srs_mp4_pfn_box2, srs_mp4_delimiter_newline);
    }
    return ss;
}

SrsMp4SttsEntry::SrsMp4SttsEntry()
{
    sample_count_ = 0;
    sample_delta_ = 0;
}

SrsMp4SttsEntry::~SrsMp4SttsEntry()
{
}

stringstream &SrsMp4SttsEntry::dumps_detail(stringstream &ss, SrsMp4DumpContext dc)
{
    ss << "count=" << sample_count_ << ", delta=" << sample_delta_;
    return ss;
}

SrsMp4DecodingTime2SampleBox::SrsMp4DecodingTime2SampleBox()
{
    type_ = SrsMp4BoxTypeSTTS;

    index_ = count_ = 0;
}

SrsMp4DecodingTime2SampleBox::~SrsMp4DecodingTime2SampleBox()
{
}

srs_error_t SrsMp4DecodingTime2SampleBox::initialize_counter()
{
    srs_error_t err = srs_success;

    // If only sps/pps and no frames, there is no stts entries.
    if (entries_.empty()) {
        return err;
    }

    index_ = 0;
    if (index_ >= entries_.size()) {
        return srs_error_new(ERROR_MP4_ILLEGAL_TIMESTAMP, "illegal ts, empty stts");
    }

    count_ = entries_[0].sample_count_;

    return err;
}

srs_error_t SrsMp4DecodingTime2SampleBox::on_sample(uint32_t sample_index, SrsMp4SttsEntry **ppentry)
{
    srs_error_t err = srs_success;

    if (sample_index + 1 > count_) {
        index_++;

        if (index_ >= entries_.size()) {
            return srs_error_new(ERROR_MP4_ILLEGAL_TIMESTAMP, "illegal ts, stts overflow, count=%zd", entries_.size());
        }

        count_ += entries_[index_].sample_count_;
    }

    *ppentry = &entries_[index_];

    return err;
}

int SrsMp4DecodingTime2SampleBox::nb_header()
{
    return SrsMp4FullBox::nb_header() + 4 + 8 * (int)entries_.size();
}

srs_error_t SrsMp4DecodingTime2SampleBox::encode_header(SrsBuffer *buf)
{
    srs_error_t err = srs_success;

    if ((err = SrsMp4FullBox::encode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "encode header");
    }

    buf->write_4bytes((int)entries_.size());
    for (size_t i = 0; i < (size_t)entries_.size(); i++) {
        SrsMp4SttsEntry &entry = entries_[i];
        buf->write_4bytes(entry.sample_count_);
        buf->write_4bytes(entry.sample_delta_);
    }

    return err;
}

srs_error_t SrsMp4DecodingTime2SampleBox::decode_header(SrsBuffer *buf)
{
    srs_error_t err = srs_success;

    if ((err = SrsMp4FullBox::decode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "decode header");
    }

    uint32_t entry_count = buf->read_4bytes();
    if (entry_count) {
        entries_.resize(entry_count);
    }
    for (size_t i = 0; i < (size_t)entry_count; i++) {
        if (!buf->require(8)) {
            return srs_error_new(ERROR_MP4_BOX_REQUIRE_SPACE, "no space");
        }

        SrsMp4SttsEntry &entry = entries_[i];
        entry.sample_count_ = buf->read_4bytes();
        entry.sample_delta_ = buf->read_4bytes();
    }

    return err;
}

stringstream &SrsMp4DecodingTime2SampleBox::dumps_detail(stringstream &ss, SrsMp4DumpContext dc)
{
    SrsMp4FullBox::dumps_detail(ss, dc);

    ss << ", " << entries_.size() << " childs (+)";
    if (!entries_.empty()) {
        ss << endl;
        srs_mp4_padding(ss, dc.indent());
        srs_dumps_array(entries_, ss, dc.indent(), srs_mp4_pfn_detail, srs_mp4_delimiter_newline);
    }
    return ss;
}

SrsMp4CttsEntry::SrsMp4CttsEntry()
{
    sample_count_ = 0;
    sample_offset_ = 0;
}

SrsMp4CttsEntry::~SrsMp4CttsEntry()
{
}

stringstream &SrsMp4CttsEntry::dumps_detail(stringstream &ss, SrsMp4DumpContext dc)
{
    ss << "count=" << sample_count_ << ", offset=" << sample_offset_;
    return ss;
}

SrsMp4CompositionTime2SampleBox::SrsMp4CompositionTime2SampleBox()
{
    type_ = SrsMp4BoxTypeCTTS;

    index_ = count_ = 0;
}

SrsMp4CompositionTime2SampleBox::~SrsMp4CompositionTime2SampleBox()
{
}

srs_error_t SrsMp4CompositionTime2SampleBox::initialize_counter()
{
    srs_error_t err = srs_success;

    // If only sps/pps and no frames, there is no stts entries.
    if (entries_.empty()) {
        return err;
    }

    index_ = 0;
    if (index_ >= entries_.size()) {
        return srs_error_new(ERROR_MP4_ILLEGAL_TIMESTAMP, "illegal ts, empty ctts");
    }

    count_ = entries_[0].sample_count_;

    return err;
}

srs_error_t SrsMp4CompositionTime2SampleBox::on_sample(uint32_t sample_index, SrsMp4CttsEntry **ppentry)
{
    srs_error_t err = srs_success;

    if (sample_index + 1 > count_) {
        index_++;

        if (index_ >= entries_.size()) {
            return srs_error_new(ERROR_MP4_ILLEGAL_TIMESTAMP, "illegal ts, ctts overflow, count=%d", (int)entries_.size());
        }

        count_ += entries_[index_].sample_count_;
    }

    *ppentry = &entries_[index_];

    return err;
}

int SrsMp4CompositionTime2SampleBox::nb_header()
{
    return SrsMp4FullBox::nb_header() + 4 + 8 * (int)entries_.size();
}

srs_error_t SrsMp4CompositionTime2SampleBox::encode_header(SrsBuffer *buf)
{
    srs_error_t err = srs_success;

    if ((err = SrsMp4FullBox::encode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "encode header");
    }

    buf->write_4bytes((int)entries_.size());
    for (size_t i = 0; i < (size_t)entries_.size(); i++) {
        SrsMp4CttsEntry &entry = entries_[i];
        buf->write_4bytes(entry.sample_count_);
        if (version_ == 0) {
            buf->write_4bytes((uint32_t)entry.sample_offset_);
        } else if (version_ == 1) {
            buf->write_4bytes((int32_t)entry.sample_offset_);
        }
    }

    return err;
}

srs_error_t SrsMp4CompositionTime2SampleBox::decode_header(SrsBuffer *buf)
{
    srs_error_t err = srs_success;

    if ((err = SrsMp4FullBox::decode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "decode header");
    }

    uint32_t entry_count = buf->read_4bytes();
    if (entry_count) {
        entries_.resize(entry_count);
    }
    for (size_t i = 0; i < (size_t)entry_count; i++) {
        SrsMp4CttsEntry &entry = entries_[i];
        entry.sample_count_ = buf->read_4bytes();
        if (version_ == 0) {
            entry.sample_offset_ = (uint32_t)buf->read_4bytes();
        } else if (version_ == 1) {
            entry.sample_offset_ = (int32_t)buf->read_4bytes();
        }
    }

    return err;
}

stringstream &SrsMp4CompositionTime2SampleBox::dumps_detail(stringstream &ss, SrsMp4DumpContext dc)
{
    SrsMp4FullBox::dumps_detail(ss, dc);

    ss << ", " << entries_.size() << " childs (+)";
    if (!entries_.empty()) {
        ss << endl;
        srs_mp4_padding(ss, dc.indent());
        srs_dumps_array(entries_, ss, dc.indent(), srs_mp4_pfn_detail, srs_mp4_delimiter_newline);
    }
    return ss;
}

SrsMp4SyncSampleBox::SrsMp4SyncSampleBox()
{
    type_ = SrsMp4BoxTypeSTSS;

    entry_count_ = 0;
    sample_numbers_ = NULL;
}

SrsMp4SyncSampleBox::~SrsMp4SyncSampleBox()
{
    srs_freepa(sample_numbers_);
}

bool SrsMp4SyncSampleBox::is_sync(uint32_t sample_index)
{
    for (uint32_t i = 0; i < entry_count_; i++) {
        if (sample_index + 1 == sample_numbers_[i]) {
            return true;
        }
    }
    return false;
}

int SrsMp4SyncSampleBox::nb_header()
{
    return SrsMp4FullBox::nb_header() + 4 + 4 * entry_count_;
}

srs_error_t SrsMp4SyncSampleBox::encode_header(SrsBuffer *buf)
{
    srs_error_t err = srs_success;

    if ((err = SrsMp4FullBox::encode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "encode header");
    }

    buf->write_4bytes(entry_count_);
    for (uint32_t i = 0; i < entry_count_; i++) {
        uint32_t sample_number = sample_numbers_[i];
        buf->write_4bytes(sample_number);
    }

    return err;
}

srs_error_t SrsMp4SyncSampleBox::decode_header(SrsBuffer *buf)
{
    srs_error_t err = srs_success;

    if ((err = SrsMp4FullBox::decode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "decode header");
    }

    entry_count_ = buf->read_4bytes();
    if (entry_count_ > 0) {
        sample_numbers_ = new uint32_t[entry_count_];
    }
    for (uint32_t i = 0; i < entry_count_; i++) {
        sample_numbers_[i] = buf->read_4bytes();
    }

    return err;
}

stringstream &SrsMp4SyncSampleBox::dumps_detail(stringstream &ss, SrsMp4DumpContext dc)
{
    SrsMp4FullBox::dumps_detail(ss, dc);

    ss << ", count=" << entry_count_;
    if (entry_count_ > 0) {
        ss << endl;
        srs_mp4_padding(ss, dc.indent());
        srs_dumps_array(sample_numbers_, entry_count_, ss, dc.indent(), srs_mp4_pfn_elem, srs_mp4_delimiter_inspace);
    }
    return ss;
}

SrsMp4StscEntry::SrsMp4StscEntry()
{
    first_chunk_ = 0;
    samples_per_chunk_ = 0;
    sample_description_index_ = 0;
}

// LCOV_EXCL_START
stringstream &SrsMp4StscEntry::dumps_detail(stringstream &ss, SrsMp4DumpContext dc)
{
    ss << "first=" << first_chunk_ << ", samples=" << samples_per_chunk_ << ", index=" << sample_description_index_;
    return ss;
}
// LCOV_EXCL_STOP

SrsMp4Sample2ChunkBox::SrsMp4Sample2ChunkBox()
{
    type_ = SrsMp4BoxTypeSTSC;

    entry_count_ = 0;
    entries_ = NULL;
    index_ = 0;
}

SrsMp4Sample2ChunkBox::~SrsMp4Sample2ChunkBox()
{
    srs_freepa(entries_);
}

void SrsMp4Sample2ChunkBox::initialize_counter()
{
    index_ = 0;
}

SrsMp4StscEntry *SrsMp4Sample2ChunkBox::on_chunk(uint32_t chunk_index)
{
    // Last chunk?
    if (index_ >= entry_count_ - 1) {
        return &entries_[index_];
    }

    // Move next chunk?
    if (chunk_index + 1 >= entries_[index_ + 1].first_chunk_) {
        index_++;
    }
    return &entries_[index_];
}

int SrsMp4Sample2ChunkBox::nb_header()
{
    return SrsMp4FullBox::nb_header() + 4 + 12 * entry_count_;
}

srs_error_t SrsMp4Sample2ChunkBox::encode_header(SrsBuffer *buf)
{
    srs_error_t err = srs_success;

    if ((err = SrsMp4FullBox::encode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "encode header");
    }

    buf->write_4bytes(entry_count_);
    for (uint32_t i = 0; i < entry_count_; i++) {
        SrsMp4StscEntry &entry = entries_[i];
        buf->write_4bytes(entry.first_chunk_);
        buf->write_4bytes(entry.samples_per_chunk_);
        buf->write_4bytes(entry.sample_description_index_);
    }

    return err;
}

srs_error_t SrsMp4Sample2ChunkBox::decode_header(SrsBuffer *buf)
{
    srs_error_t err = srs_success;

    if ((err = SrsMp4FullBox::decode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "decode header");
    }

    entry_count_ = buf->read_4bytes();
    if (entry_count_) {
        entries_ = new SrsMp4StscEntry[entry_count_];
    }
    for (uint32_t i = 0; i < entry_count_; i++) {
        SrsMp4StscEntry &entry = entries_[i];
        entry.first_chunk_ = buf->read_4bytes();
        entry.samples_per_chunk_ = buf->read_4bytes();
        entry.sample_description_index_ = buf->read_4bytes();
    }

    return err;
}

stringstream &SrsMp4Sample2ChunkBox::dumps_detail(stringstream &ss, SrsMp4DumpContext dc)
{
    SrsMp4FullBox::dumps_detail(ss, dc);

    ss << ", " << entry_count_ << " childs (+)";
    if (entry_count_ > 0) {
        ss << endl;
        srs_mp4_padding(ss, dc.indent());
        srs_dumps_array(entries_, entry_count_, ss, dc.indent(), srs_mp4_pfn_detail, srs_mp4_delimiter_newline);
    }
    return ss;
}

SrsMp4ChunkOffsetBox::SrsMp4ChunkOffsetBox()
{
    type_ = SrsMp4BoxTypeSTCO;

    entry_count_ = 0;
    entries_ = NULL;
}

SrsMp4ChunkOffsetBox::~SrsMp4ChunkOffsetBox()
{
    srs_freepa(entries_);
}

int SrsMp4ChunkOffsetBox::nb_header()
{
    return SrsMp4FullBox::nb_header() + 4 + 4 * entry_count_;
}

srs_error_t SrsMp4ChunkOffsetBox::encode_header(SrsBuffer *buf)
{
    srs_error_t err = srs_success;

    if ((err = SrsMp4FullBox::encode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "encode header");
    }

    buf->write_4bytes(entry_count_);
    for (uint32_t i = 0; i < entry_count_; i++) {
        buf->write_4bytes(entries_[i]);
    }

    return err;
}

srs_error_t SrsMp4ChunkOffsetBox::decode_header(SrsBuffer *buf)
{
    srs_error_t err = srs_success;

    if ((err = SrsMp4FullBox::decode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "decode header");
    }

    entry_count_ = buf->read_4bytes();
    if (entry_count_) {
        entries_ = new uint32_t[entry_count_];
    }
    for (uint32_t i = 0; i < entry_count_; i++) {
        entries_[i] = buf->read_4bytes();
    }

    return err;
}

// LCOV_EXCL_START
stringstream &SrsMp4ChunkOffsetBox::dumps_detail(stringstream &ss, SrsMp4DumpContext dc)
{
    SrsMp4FullBox::dumps_detail(ss, dc);

    ss << ", " << entry_count_ << " childs (+)";
    if (entry_count_ > 0) {
        ss << endl;
        srs_mp4_padding(ss, dc.indent());
        srs_dumps_array(entries_, entry_count_, ss, dc.indent(), srs_mp4_pfn_elem, srs_mp4_delimiter_inspace);
    }
    return ss;
}
// LCOV_EXCL_STOP

SrsMp4ChunkLargeOffsetBox::SrsMp4ChunkLargeOffsetBox()
{
    type_ = SrsMp4BoxTypeCO64;

    entry_count_ = 0;
    entries_ = NULL;
}

SrsMp4ChunkLargeOffsetBox::~SrsMp4ChunkLargeOffsetBox()
{
    srs_freepa(entries_);
}

int SrsMp4ChunkLargeOffsetBox::nb_header()
{
    return SrsMp4FullBox::nb_header() + 4 + 8 * entry_count_;
}

srs_error_t SrsMp4ChunkLargeOffsetBox::encode_header(SrsBuffer *buf)
{
    srs_error_t err = srs_success;

    if ((err = SrsMp4FullBox::encode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "encode header");
    }

    buf->write_4bytes(entry_count_);
    for (uint32_t i = 0; i < entry_count_; i++) {
        buf->write_8bytes(entries_[i]);
    }

    return err;
}

srs_error_t SrsMp4ChunkLargeOffsetBox::decode_header(SrsBuffer *buf)
{
    srs_error_t err = srs_success;

    if ((err = SrsMp4FullBox::decode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "decode header");
    }

    entry_count_ = buf->read_4bytes();
    if (entry_count_) {
        entries_ = new uint64_t[entry_count_];
    }
    for (uint32_t i = 0; i < entry_count_; i++) {
        entries_[i] = buf->read_8bytes();
    }

    return err;
}

// LCOV_EXCL_START
stringstream &SrsMp4ChunkLargeOffsetBox::dumps_detail(stringstream &ss, SrsMp4DumpContext dc)
{
    SrsMp4FullBox::dumps_detail(ss, dc);

    ss << ", " << entry_count_ << " childs (+)";
    if (entry_count_ > 0) {
        ss << endl;
        srs_mp4_padding(ss, dc.indent());
        srs_dumps_array(entries_, entry_count_, ss, dc.indent(), srs_mp4_pfn_elem, srs_mp4_delimiter_inspace);
    }
    return ss;
}
// LCOV_EXCL_STOP

SrsMp4SampleSizeBox::SrsMp4SampleSizeBox()
{
    type_ = SrsMp4BoxTypeSTSZ;

    sample_size_ = sample_count_ = 0;
    entry_sizes_ = NULL;
}

SrsMp4SampleSizeBox::~SrsMp4SampleSizeBox()
{
    srs_freepa(entry_sizes_);
}

srs_error_t SrsMp4SampleSizeBox::get_sample_size(uint32_t sample_index, uint32_t *psample_size)
{
    srs_error_t err = srs_success;

    if (sample_size_ != 0) {
        *psample_size = sample_size_;
        return err;
    }

    if (sample_index >= sample_count_) {
        return srs_error_new(ERROR_MP4_MOOV_OVERFLOW, "stsz overflow, sample_count=%d", sample_count_);
    }
    *psample_size = entry_sizes_[sample_index];

    return err;
}

int SrsMp4SampleSizeBox::nb_header()
{
    int size = SrsMp4FullBox::nb_header() + 4 + 4;
    if (sample_size_ == 0) {
        size += 4 * sample_count_;
    }
    return size;
}

srs_error_t SrsMp4SampleSizeBox::encode_header(SrsBuffer *buf)
{
    srs_error_t err = srs_success;

    if ((err = SrsMp4FullBox::encode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "encode header");
    }

    buf->write_4bytes(sample_size_);
    buf->write_4bytes(sample_count_);
    for (uint32_t i = 0; i < sample_count_ && sample_size_ == 0; i++) {
        buf->write_4bytes(entry_sizes_[i]);
    }

    return err;
}

srs_error_t SrsMp4SampleSizeBox::decode_header(SrsBuffer *buf)
{
    srs_error_t err = srs_success;

    if ((err = SrsMp4FullBox::decode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "decode header");
    }

    sample_size_ = buf->read_4bytes();
    sample_count_ = buf->read_4bytes();
    if (!sample_size_ && sample_count_) {
        entry_sizes_ = new uint32_t[sample_count_];
    }
    for (uint32_t i = 0; i < sample_count_ && sample_size_ == 0; i++) {
        entry_sizes_[i] = buf->read_4bytes();
    }

    return err;
}

// LCOV_EXCL_START
stringstream &SrsMp4SampleSizeBox::dumps_detail(stringstream &ss, SrsMp4DumpContext dc)
{
    SrsMp4FullBox::dumps_detail(ss, dc);

    ss << ", size=" << sample_size_ << ", " << sample_count_ << " childs (+)";
    if (!sample_size_ && sample_count_ > 0) {
        ss << endl;
        srs_mp4_padding(ss, dc.indent());
        srs_dumps_array(entry_sizes_, sample_count_, ss, dc.indent(), srs_mp4_pfn_elem, srs_mp4_delimiter_inspace);
    }
    return ss;
}
// LCOV_EXCL_STOP

SrsMp4UserDataBox::SrsMp4UserDataBox()
{
    type_ = SrsMp4BoxTypeUDTA;
}

SrsMp4UserDataBox::~SrsMp4UserDataBox()
{
}

int SrsMp4UserDataBox::nb_header()
{
    return SrsMp4Box::nb_header() + (int)data_.size();
}

srs_error_t SrsMp4UserDataBox::encode_header(SrsBuffer *buf)
{
    srs_error_t err = srs_success;

    if ((err = SrsMp4Box::encode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "encode header");
    }

    if (!data_.empty()) {
        buf->write_bytes(&data_[0], (int)data_.size());
    }

    return err;
}

srs_error_t SrsMp4UserDataBox::decode_header(SrsBuffer *buf)
{
    srs_error_t err = srs_success;

    if ((err = SrsMp4Box::decode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "decode header");
    }

    int nb_data = left_space(buf);
    if (nb_data) {
        data_.resize(nb_data);
        buf->read_bytes(&data_[0], (int)data_.size());
    }

    return err;
}

stringstream &SrsMp4UserDataBox::dumps_detail(stringstream &ss, SrsMp4DumpContext dc)
{
    SrsMp4Box::dumps_detail(ss, dc);

    ss << ", total " << data_.size() << "B";

    if (!data_.empty()) {
        ss << endl;
        srs_mp4_padding(ss, dc.indent());
        srs_dumps_array(&data_[0], (int)data_.size(), ss, dc.indent(), srs_mp4_pfn_hex, srs_mp4_delimiter_inspace);
    }
    return ss;
}

SrsMp4SegmentIndexBox::SrsMp4SegmentIndexBox()
{
    type_ = SrsMp4BoxTypeSIDX;
    version_ = 0;
    flags_ = reference_id_ = timescale_ = 0;
    earliest_presentation_time_ = first_offset_ = 0;
}

SrsMp4SegmentIndexBox::~SrsMp4SegmentIndexBox()
{
}

int SrsMp4SegmentIndexBox::nb_header()
{
    return SrsMp4Box::nb_header() + 4 + 4 + 4 + (!version_ ? 8 : 16) + 4 + 12 * entries_.size();
}

srs_error_t SrsMp4SegmentIndexBox::encode_header(SrsBuffer *buf)
{
    srs_error_t err = srs_success;

    if ((err = SrsMp4Box::encode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "encode header");
    }

    buf->write_1bytes(version_);
    buf->write_3bytes(flags_);
    buf->write_4bytes(reference_id_);
    buf->write_4bytes(timescale_);
    if (!version_) {
        buf->write_4bytes(earliest_presentation_time_);
        buf->write_4bytes(first_offset_);
    } else {
        buf->write_8bytes(earliest_presentation_time_);
        buf->write_8bytes(first_offset_);
    }

    buf->write_4bytes((uint32_t)entries_.size());
    for (int i = 0; i < (int)entries_.size(); i++) {
        SrsMp4SegmentIndexEntry &entry = entries_.at(i);

        uint32_t v = uint32_t(entry.reference_type_ & 0x01) << 31;
        v |= entry.referenced_size_ & 0x7fffffff;
        buf->write_4bytes(v);

        buf->write_4bytes(entry.subsegment_duration_);

        v = uint32_t(entry.starts_with_SAP_ & 0x01) << 31;
        v |= uint32_t(entry.SAP_type_ & 0x7) << 28;
        v |= entry.SAP_delta_time_ & 0xfffffff;
        buf->write_4bytes(v);
    }

    return err;
}

srs_error_t SrsMp4SegmentIndexBox::decode_header(SrsBuffer *buf)
{
    srs_error_t err = srs_success;

    if ((err = SrsMp4Box::decode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "decode header");
    }

    version_ = buf->read_1bytes();
    flags_ = buf->read_3bytes();
    reference_id_ = buf->read_4bytes();
    timescale_ = buf->read_4bytes();

    if (!version_) {
        if (!buf->require(8)) {
            return srs_error_new(ERROR_MP4_BOX_REQUIRE_SPACE, "no space");
        }
        earliest_presentation_time_ = buf->read_4bytes();
        first_offset_ = buf->read_4bytes();
    } else {
        if (!buf->require(16)) {
            return srs_error_new(ERROR_MP4_BOX_REQUIRE_SPACE, "no space");
        }
        earliest_presentation_time_ = buf->read_8bytes();
        first_offset_ = buf->read_8bytes();
    }

    uint32_t nn_entries = (uint32_t)(buf->read_4bytes() & 0xffff);
    for (uint32_t i = 0; i < nn_entries; i++) {
        if (!buf->require(12)) {
            return srs_error_new(ERROR_MP4_BOX_REQUIRE_SPACE, "no space");
        }

        SrsMp4SegmentIndexEntry entry;

        uint32_t v = buf->read_4bytes();
        entry.reference_type_ = uint8_t((v & 0x80000000) >> 31);
        entry.referenced_size_ = v & 0x7fffffff;

        entry.subsegment_duration_ = buf->read_4bytes();

        v = buf->read_4bytes();
        entry.starts_with_SAP_ = uint8_t((v & 0x80000000) >> 31);
        entry.SAP_type_ = uint8_t((v & 0x70000000) >> 28);
        entry.SAP_delta_time_ = v & 0xfffffff;

        entries_.push_back(entry);
    }

    return err;
}

// LCOV_EXCL_START
stringstream &SrsMp4SegmentIndexBox::dumps_detail(stringstream &ss, SrsMp4DumpContext dc)
{
    SrsMp4Box::dumps_detail(ss, dc);

    ss << ", v" << (int)version_ << ", flags=" << flags_ << ", refs#" << reference_id_
       << ", TBN=" << timescale_ << ", ePTS=" << earliest_presentation_time_;

    for (int i = 0; i < (int)entries_.size(); i++) {
        SrsMp4SegmentIndexEntry &entry = entries_.at(i);

        ss << endl;
        srs_mp4_padding(ss, dc.indent());
        ss << "#" << i << ", ref=" << (int)entry.reference_type_ << "/" << entry.referenced_size_
           << ", duration=" << entry.subsegment_duration_ << ", SAP=" << (int)entry.starts_with_SAP_
           << "/" << (int)entry.SAP_type_ << "/" << entry.SAP_delta_time_;
    }

    return ss;
}
// LCOV_EXCL_STOP

SrsMp4SampleAuxiliaryInfoSizeBox::SrsMp4SampleAuxiliaryInfoSizeBox()
{
    type_ = SrsMp4BoxTypeSAIZ;
}

SrsMp4SampleAuxiliaryInfoSizeBox::~SrsMp4SampleAuxiliaryInfoSizeBox()
{
}

int SrsMp4SampleAuxiliaryInfoSizeBox::nb_header()
{
    int size = SrsMp4FullBox::nb_header();

    if (flags_ & 0x01) {
        size += 8; // add sizeof(aux_info_type) + sizeof(aux_info_type_parameter);
    }

    size += 1; // sizeof(default_sample_info_size);
    size += 4; // sizeof(sample_count);

    if (default_sample_info_size_ == 0) {
        size += sample_info_sizes_.size();
    }

    return size;
}

srs_error_t SrsMp4SampleAuxiliaryInfoSizeBox::encode_header(SrsBuffer *buf)
{
    srs_error_t err = srs_success;

    if ((err = SrsMp4FullBox::encode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "encode header");
    }

    if (flags_ & 0x01) {
        buf->write_4bytes(aux_info_type_);
        buf->write_4bytes(aux_info_type_parameter_);
    }

    buf->write_1bytes(default_sample_info_size_);

    if (default_sample_info_size_ == 0) {
        buf->write_4bytes(sample_info_sizes_.size());
        vector<uint8_t>::iterator it;
        for (it = sample_info_sizes_.begin(); it != sample_info_sizes_.end(); ++it) {
            buf->write_1bytes(*it);
        }
    } else {
        buf->write_4bytes(sample_count_);
    }

    return err;
}

srs_error_t SrsMp4SampleAuxiliaryInfoSizeBox::decode_header(SrsBuffer *buf)
{
    srs_error_t err = srs_success;

    if ((err = SrsMp4FullBox::decode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "decode header");
    }

    if (flags_ & 0x01) {
        aux_info_type_ = buf->read_4bytes();
        aux_info_type_parameter_ = buf->read_4bytes();
    }

    default_sample_info_size_ = buf->read_1bytes();
    sample_count_ = buf->read_4bytes();

    if (default_sample_info_size_ == 0) {
        for (int i = 0; i < (int)sample_count_; i++) {
            sample_info_sizes_.push_back(buf->read_1bytes());
        }
    }

    return err;
}

std::stringstream &SrsMp4SampleAuxiliaryInfoSizeBox::dumps_detail(std::stringstream &ss, SrsMp4DumpContext dc)
{
    ss << "default_sample_info_size=" << (int)default_sample_info_size_ << ", sample_count=" << sample_count_;
    return ss;
}

SrsMp4SampleAuxiliaryInfoOffsetBox::SrsMp4SampleAuxiliaryInfoOffsetBox()
{
    type_ = SrsMp4BoxTypeSAIO;
}

SrsMp4SampleAuxiliaryInfoOffsetBox::~SrsMp4SampleAuxiliaryInfoOffsetBox()
{
}

int SrsMp4SampleAuxiliaryInfoOffsetBox::nb_header()
{
    int size = SrsMp4FullBox::nb_header();

    if (flags_ & 0x01) {
        size += 8; // sizeof(aux_info_type) + sizeof(aux_info_type_parameter);
    }

    size += 4; // sizeof(entry_count);
    if (version_ == 0) {
        size += offsets_.size() * 4;
    } else {
        size += offsets_.size() * 8;
    }

    return size;
}

srs_error_t SrsMp4SampleAuxiliaryInfoOffsetBox::encode_header(SrsBuffer *buf)
{
    srs_error_t err = srs_success;

    if ((err = SrsMp4FullBox::encode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "encode header");
    }

    if (flags_ & 0x01) {
        buf->write_4bytes(aux_info_type_);
        buf->write_4bytes(aux_info_type_parameter_);
    }

    buf->write_4bytes(offsets_.size());
    vector<uint64_t>::iterator it;
    for (it = offsets_.begin(); it != offsets_.end(); ++it) {
        if (version_ == 0) {
            buf->write_4bytes(*it);
        } else {
            buf->write_8bytes(*it);
        }
    }

    return err;
}

srs_error_t SrsMp4SampleAuxiliaryInfoOffsetBox::decode_header(SrsBuffer *buf)
{
    srs_error_t err = srs_success;

    if ((err = SrsMp4FullBox::decode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "decode header");
    }

    if (flags_ & 0x01) {
        aux_info_type_ = buf->read_4bytes();
        aux_info_type_parameter_ = buf->read_4bytes();
    }

    uint32_t entry_count = buf->read_4bytes();
    for (int i = 0; i < (int)entry_count; i++) {
        if (version_ == 0) {
            offsets_.push_back(buf->read_4bytes());
        } else {
            offsets_.push_back(buf->read_8bytes());
        }
    }

    return err;
}

std::stringstream &SrsMp4SampleAuxiliaryInfoOffsetBox::dumps_detail(std::stringstream &ss, SrsMp4DumpContext dc)
{
    ss << "entry_count=" << offsets_.size();
    return ss;
}

SrsMp4SubSampleEncryptionInfo::SrsMp4SubSampleEncryptionInfo()
{
    bytes_of_clear_data_ = 0;
    bytes_of_protected_data_ = 0;
}

SrsMp4SubSampleEncryptionInfo::~SrsMp4SubSampleEncryptionInfo()
{
}

uint64_t SrsMp4SubSampleEncryptionInfo::nb_bytes()
{
    // sizeof(bytes_of_clear_data) + sizeof(bytes_of_protected_data);
    return 6;
}

srs_error_t SrsMp4SubSampleEncryptionInfo::encode(SrsBuffer *buf)
{
    buf->write_2bytes(bytes_of_clear_data_);
    buf->write_4bytes(bytes_of_protected_data_);

    return srs_success;
}

srs_error_t SrsMp4SubSampleEncryptionInfo::decode(SrsBuffer *buf)
{
    bytes_of_clear_data_ = buf->read_2bytes();
    bytes_of_protected_data_ = buf->read_4bytes();

    return srs_success;
}

std::stringstream &SrsMp4SubSampleEncryptionInfo::dumps(std::stringstream &ss, SrsMp4DumpContext dc)
{
    ss << "bytes_of_clear_data=" << bytes_of_clear_data_ << ", bytes_of_protected_data=" << bytes_of_protected_data_;
    return ss;
}

SrsMp4SampleEncryptionEntry::SrsMp4SampleEncryptionEntry(SrsMp4FullBox *senc, uint8_t per_sample_iv_size)
{
    senc_ = senc;
    srs_assert(per_sample_iv_size == 0 || per_sample_iv_size == 8 || per_sample_iv_size == 16);
    per_sample_iv_size_ = per_sample_iv_size;
    iv_ = (uint8_t *)malloc(per_sample_iv_size);
}

SrsMp4SampleEncryptionEntry::~SrsMp4SampleEncryptionEntry()
{
    free(iv_);
    iv_ = NULL;
}

srs_error_t SrsMp4SampleEncryptionEntry::set_iv(uint8_t *iv, uint8_t iv_size)
{
    srs_assert(iv_size == per_sample_iv_size_);
    memcpy(iv_, iv, iv_size);

    return srs_success;
}

uint64_t SrsMp4SampleEncryptionEntry::nb_bytes()
{
    uint64_t size = per_sample_iv_size_;
    if (senc_->flags_ & SrsMp4CencSampleEncryptionUseSubSample) {
        size += 2; // size of subsample_count
        size += subsample_infos_.size() * 6;
    }

    return size;
}

srs_error_t SrsMp4SampleEncryptionEntry::encode(SrsBuffer *buf)
{
    if (per_sample_iv_size_ != 0) {
        buf->write_bytes((char *)iv_, per_sample_iv_size_);
    }

    if (senc_->flags_ & SrsMp4CencSampleEncryptionUseSubSample) {
        buf->write_2bytes(subsample_infos_.size());

        vector<SrsMp4SubSampleEncryptionInfo>::iterator it;
        for (it = subsample_infos_.begin(); it != subsample_infos_.end(); ++it) {
            (*it).encode(buf);
        }
    }

    return srs_success;
}

srs_error_t SrsMp4SampleEncryptionEntry::decode(SrsBuffer *buf)
{
    if (per_sample_iv_size_ > 0) {
        buf->read_bytes((char *)iv_, per_sample_iv_size_);
    }

    if (senc_->flags_ & SrsMp4CencSampleEncryptionUseSubSample) {
        uint16_t subsample_count = buf->read_2bytes();
        for (uint16_t i = 0; i < subsample_count; i++) {
            SrsMp4SubSampleEncryptionInfo info;
            info.decode(buf);
            subsample_infos_.push_back(info);
        }
    }
    return srs_success;
}

std::stringstream &SrsMp4SampleEncryptionEntry::dumps(std::stringstream &ss, SrsMp4DumpContext dc)
{
    // TODO: dump what?
    ss << "iv=" << iv_ << endl;

    vector<SrsMp4SubSampleEncryptionInfo>::iterator it;
    for (it = subsample_infos_.begin(); it != subsample_infos_.end(); ++it) {
        (*it).dumps(ss, dc);
        ss << endl;
    }

    return ss;
}

SrsMp4SampleEncryptionBox::SrsMp4SampleEncryptionBox(uint8_t per_sample_iv_size)
{
    version_ = 0;
    flags_ = SrsMp4CencSampleEncryptionUseSubSample;
    type_ = SrsMp4BoxTypeSENC;
    srs_assert(per_sample_iv_size == 0 || per_sample_iv_size == 8 || per_sample_iv_size == 16);
    per_sample_iv_size_ = per_sample_iv_size;
}

SrsMp4SampleEncryptionBox::~SrsMp4SampleEncryptionBox()
{
    vector<SrsMp4SampleEncryptionEntry *>::iterator it;
    for (it = entries_.begin(); it != entries_.end(); it++) {
        SrsMp4SampleEncryptionEntry *entry = *it;
        srs_freep(entry);
    }
    entries_.clear();
}

int SrsMp4SampleEncryptionBox::nb_header()
{
    int size = SrsMp4FullBox::nb_header() + 4;

    vector<SrsMp4SampleEncryptionEntry *>::iterator it;
    for (it = entries_.begin(); it < entries_.end(); it++) {
        size += (*it)->nb_bytes();
    }

    return size;
}

srs_error_t SrsMp4SampleEncryptionBox::encode_header(SrsBuffer *buf)
{
    srs_error_t err = srs_success;

    if ((err = SrsMp4FullBox::encode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "encode header");
    }

    buf->write_4bytes(entries_.size());
    vector<SrsMp4SampleEncryptionEntry *>::iterator it;
    for (it = entries_.begin(); it != entries_.end(); it++) {
        (*it)->encode(buf);
    }

    return err;
}

srs_error_t SrsMp4SampleEncryptionBox::decode_header(SrsBuffer *buf)
{
    srs_error_t err = srs_success;

    if ((err = SrsMp4FullBox::decode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "decode header");
    }

    vector<SrsMp4SampleEncryptionEntry *>::iterator it;
    for (it = entries_.begin(); it != entries_.end(); it++) {
        SrsMp4SampleEncryptionEntry *entry = *it;
        srs_freep(entry);
    }
    entries_.clear();

    int32_t size = buf->read_4bytes();
    for (int i = 0; i < size; i++) {
        SrsMp4SampleEncryptionEntry *entry = new SrsMp4SampleEncryptionEntry(this, per_sample_iv_size_);
        entry->decode(buf);
        entries_.push_back(entry);
    }

    return err;
}

std::stringstream &SrsMp4SampleEncryptionBox::dumps_detail(std::stringstream &ss, SrsMp4DumpContext dc)
{
    ss << "sample_count=" << entries_.size() << endl;
    return ss;
}

SrsMp4ProtectionSchemeInfoBox::SrsMp4ProtectionSchemeInfoBox()
{
    type_ = SrsMp4BoxTypeSINF;
}

SrsMp4ProtectionSchemeInfoBox::~SrsMp4ProtectionSchemeInfoBox()
{
}

SrsMp4OriginalFormatBox *SrsMp4ProtectionSchemeInfoBox::frma()
{
    SrsMp4Box *box = get(SrsMp4BoxTypeFRMA);
    return dynamic_cast<SrsMp4OriginalFormatBox *>(box);
}

void SrsMp4ProtectionSchemeInfoBox::set_frma(SrsMp4OriginalFormatBox *v)
{
    remove(SrsMp4BoxTypeFRMA);
    boxes_.push_back(v);
}

SrsMp4SchemeTypeBox *SrsMp4ProtectionSchemeInfoBox::schm()
{
    SrsMp4Box *box = get(SrsMp4BoxTypeSCHM);
    return dynamic_cast<SrsMp4SchemeTypeBox *>(box);
}

void SrsMp4ProtectionSchemeInfoBox::set_schm(SrsMp4SchemeTypeBox *v)
{
    remove(SrsMp4BoxTypeSCHM);
    boxes_.push_back(v);
}

SrsMp4SchemeInfoBox *SrsMp4ProtectionSchemeInfoBox::schi()
{
    SrsMp4Box *box = get(SrsMp4BoxTypeSCHI);
    return dynamic_cast<SrsMp4SchemeInfoBox *>(box);
}

void SrsMp4ProtectionSchemeInfoBox::set_schi(SrsMp4SchemeInfoBox *v)
{
    remove(SrsMp4BoxTypeSCHI);
    boxes_.push_back(v);
}

SrsMp4OriginalFormatBox::SrsMp4OriginalFormatBox(uint32_t original_format)
{
    type_ = SrsMp4BoxTypeFRMA;
    data_format_ = original_format;
}

SrsMp4OriginalFormatBox::~SrsMp4OriginalFormatBox()
{
}

int SrsMp4OriginalFormatBox::nb_header()
{
    return SrsMp4Box::nb_header() + 4;
}

srs_error_t SrsMp4OriginalFormatBox::encode_header(SrsBuffer *buf)
{
    srs_error_t err = srs_success;

    if ((err = SrsMp4Box::encode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "encode header");
    }

    buf->write_4bytes(data_format_);

    return err;
}

srs_error_t SrsMp4OriginalFormatBox::decode_header(SrsBuffer *buf)
{
    srs_error_t err = srs_success;

    if ((err = SrsMp4Box::decode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "decode header");
    }

    data_format_ = buf->read_4bytes();

    return err;
}

std::stringstream &SrsMp4OriginalFormatBox::dumps_detail(std::stringstream &ss, SrsMp4DumpContext dc)
{
    ss << "original format=" << data_format_ << endl;
    return ss;
}

SrsMp4SchemeTypeBox::SrsMp4SchemeTypeBox()
{
    type_ = SrsMp4BoxTypeSCHM;
    scheme_uri_size_ = 0;
}

SrsMp4SchemeTypeBox::~SrsMp4SchemeTypeBox()
{
}

void SrsMp4SchemeTypeBox::set_scheme_uri(char *uri, uint32_t uri_size)
{
    srs_assert(uri_size < SCHM_SCHEME_URI_MAX_SIZE);
    memcpy(scheme_uri_, uri, uri_size);
    scheme_uri_size_ = uri_size;
    scheme_uri_[uri_size] = '\0';
}

int SrsMp4SchemeTypeBox::nb_header()
{
    int size = SrsMp4FullBox::nb_header() + 4 + 4; // sizeof(scheme_type) + sizeof(scheme_version)

    if (flags_ & 0x01) {
        size += scheme_uri_size_;
    }

    return size;
}

srs_error_t SrsMp4SchemeTypeBox::encode_header(SrsBuffer *buf)
{
    srs_error_t err = srs_success;

    if ((err = SrsMp4FullBox::encode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "encode header");
    }

    buf->write_4bytes(scheme_type_);
    buf->write_4bytes(scheme_version_);

    if (flags_ & 0x01) {
        buf->write_bytes(scheme_uri_, scheme_uri_size_);
        buf->write_1bytes(0);
    }

    return err;
}

srs_error_t SrsMp4SchemeTypeBox::decode_header(SrsBuffer *buf)
{
    srs_error_t err = srs_success;

    if ((err = SrsMp4FullBox::decode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "decode header");
    }
    scheme_type_ = buf->read_4bytes();
    scheme_version_ = buf->read_4bytes();

    if (flags_ & 0x01) {
        memset(scheme_uri_, 0, SCHM_SCHEME_URI_MAX_SIZE);
        int s = 0;
        while (s < SCHM_SCHEME_URI_MAX_SIZE - 1) {
            char c = buf->read_1bytes();
            scheme_uri_[s] = c;
            s++;
            if (c == '\0') {
                break;
            }
        }
        scheme_uri_size_ = s;
    }

    return err;
}

std::stringstream &SrsMp4SchemeTypeBox::dumps_detail(std::stringstream &ss, SrsMp4DumpContext dc)
{
    ss << "scheme_type=" << scheme_type_ << ", scheme_version=" << scheme_version_ << endl;
    if (flags_ & 0x01) {
        ss << "scheme_uri=" << scheme_uri_ << endl;
    }

    return ss;
}

SrsMp4SchemeInfoBox::SrsMp4SchemeInfoBox()
{
    type_ = SrsMp4BoxTypeSCHI;
}

SrsMp4SchemeInfoBox::~SrsMp4SchemeInfoBox()
{
}

SrsMp4TrackEncryptionBox::SrsMp4TrackEncryptionBox()
{
    type_ = SrsMp4BoxTypeTENC;
}

SrsMp4TrackEncryptionBox::~SrsMp4TrackEncryptionBox()
{
}

void SrsMp4TrackEncryptionBox::set_default_constant_IV(uint8_t *iv, uint8_t iv_size)
{
    srs_assert(iv_size == 8 || iv_size == 16);
    memcpy(default_constant_IV_, iv, iv_size);
    default_constant_IV_size_ = iv_size;
}

int SrsMp4TrackEncryptionBox::nb_header()
{
    int size = SrsMp4FullBox::nb_header();
    size += 1;  // sizeof(reserved)
    size += 1;  // sizeof(reserved_2) or sizeof(default_crypt_byte_block) + sizeof(default_skip_byte_block);
    size += 1;  // sizeof(default_isProtected);
    size += 1;  // sizeof(default_Per_Sample_IV_Size;
    size += 16; // sizeof(default_KID);
    if (default_is_protected_ == 1 && default_per_sample_IV_size_ == 0) {
        size += 1 + default_constant_IV_size_; // sizeof(default_constant_IV_size) + sizeof(default_constant_IV);
    }

    return size;
}

srs_error_t SrsMp4TrackEncryptionBox::encode_header(SrsBuffer *buf)
{
    srs_error_t err = srs_success;

    if ((err = SrsMp4FullBox::encode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "encode header");
    }

    buf->write_1bytes(reserved_);
    if (version_ == 0) {
        buf->write_1bytes(reserved_2_);
    } else {
        buf->write_1bytes((default_crypt_byte_block_ << 4) | (default_skip_byte_block_ & 0x0F));
    }

    buf->write_1bytes(default_is_protected_);
    buf->write_1bytes(default_per_sample_IV_size_);
    buf->write_bytes((char *)default_KID_, 16);
    if (default_is_protected_ == 1 && default_per_sample_IV_size_ == 0) {
        buf->write_1bytes(default_constant_IV_size_);
        buf->write_bytes((char *)default_constant_IV_, default_constant_IV_size_);
    }

    return err;
}

srs_error_t SrsMp4TrackEncryptionBox::decode_header(SrsBuffer *buf)
{
    srs_error_t err = srs_success;

    if ((err = SrsMp4FullBox::decode_header(buf)) != srs_success) {
        return srs_error_wrap(err, "encode header");
    }
    reserved_ = buf->read_1bytes();
    if (version_ == 0) {
        reserved_2_ = buf->read_1bytes();
    } else {
        uint8_t v = buf->read_1bytes();
        default_crypt_byte_block_ = v >> 4;
        default_skip_byte_block_ = v & 0x0f;
    }

    default_is_protected_ = buf->read_1bytes();
    default_per_sample_IV_size_ = buf->read_1bytes();
    buf->read_bytes((char *)default_KID_, 16);

    if (default_is_protected_ == 1 && default_per_sample_IV_size_ == 0) {
        default_constant_IV_size_ = buf->read_1bytes();
        srs_assert(default_constant_IV_size_ == 8 || default_constant_IV_size_ == 16);
        buf->read_bytes((char *)default_constant_IV_, default_constant_IV_size_);
    }

    return err;
}

std::stringstream &SrsMp4TrackEncryptionBox::dumps_detail(std::stringstream &ss, SrsMp4DumpContext dc)
{
    if (version_ != 0) {
        ss << "default_crypt_byte_block=" << default_crypt_byte_block_ << ", default_skip_byte_block=" << default_skip_byte_block_ << endl;
    }
    ss << "default_isProtected=" << default_is_protected_ << ", default_per_sample_IV_size=" << default_per_sample_IV_size_ << endl;

    return ss;
}

SrsMp4Sample::SrsMp4Sample()
{
    type_ = SrsFrameTypeForbidden;
    offset_ = 0;
    index_ = 0;
    dts_ = pts_ = 0;
    nb_data_ = 0;
    data_ = NULL;
    frame_type_ = SrsVideoAvcFrameTypeForbidden;
    tbn_ = 0;
    adjust_ = 0;
}

SrsMp4Sample::~SrsMp4Sample()
{
    srs_freepa(data_);
}

uint32_t SrsMp4Sample::dts_ms()
{
    return (uint32_t)(dts_ * 1000 / tbn_) + adjust_;
}

uint32_t SrsMp4Sample::pts_ms()
{
    return (uint32_t)(pts_ * 1000 / tbn_) + adjust_;
}

SrsMp4DvrJitter::SrsMp4DvrJitter()
{
    reset();
}

SrsMp4DvrJitter::~SrsMp4DvrJitter()
{
}

void SrsMp4DvrJitter::on_sample(SrsMp4Sample *sample)
{
    if (!has_first_audio_ && sample->type_ == SrsFrameTypeAudio) {
        has_first_audio_ = true;
        audio_start_dts_ = sample->dts_;
    }

    if (!has_first_video_ && sample->type_ == SrsFrameTypeVideo) {
        has_first_video_ = true;
        video_start_dts_ = sample->dts_;
    }
}

uint32_t SrsMp4DvrJitter::get_first_sample_delta(SrsFrameType track)
{
    if (track == SrsFrameTypeVideo) {
        return video_start_dts_ > audio_start_dts_ ? video_start_dts_ - audio_start_dts_ : 0;
    } else if (track == SrsFrameTypeAudio) {
        return audio_start_dts_ > video_start_dts_ ? audio_start_dts_ - video_start_dts_ : 0;
    }
    return 0;
}

void SrsMp4DvrJitter::reset()
{
    video_start_dts_ = 0;
    audio_start_dts_ = 0;
    has_first_video_ = false;
    has_first_audio_ = false;
}

bool SrsMp4DvrJitter::is_initialized()
{
    return has_first_video_ && has_first_audio_;
}

SrsMp4SampleManager::SrsMp4SampleManager()
{
    jitter_ = new SrsMp4DvrJitter();
}

SrsMp4SampleManager::~SrsMp4SampleManager()
{
    srs_freep(jitter_);

    vector<SrsMp4Sample *>::iterator it;
    for (it = samples_.begin(); it != samples_.end(); ++it) {
        SrsMp4Sample *sample = *it;
        srs_freep(sample);
    }
    samples_.clear();
}

srs_error_t SrsMp4SampleManager::load(SrsMp4MovieBox *moov)
{
    srs_error_t err = srs_success;

    map<uint64_t, SrsMp4Sample *> tses;

    // Load samples from moov, merge to temp samples.
    if ((err = do_load(tses, moov)) != srs_success) {
        map<uint64_t, SrsMp4Sample *>::iterator it;
        for (it = tses.begin(); it != tses.end(); ++it) {
            SrsMp4Sample *sample = it->second;
            srs_freep(sample);
        }

        return srs_error_wrap(err, "load mp4");
    }

    // Dumps temp samples.
    // Adjust the sequence diff.
    int32_t maxp = 0;
    int32_t maxn = 0;
    if (true) {
        SrsMp4Sample *pvideo = NULL;
        map<uint64_t, SrsMp4Sample *>::iterator it;
        for (it = tses.begin(); it != tses.end(); ++it) {
            SrsMp4Sample *sample = it->second;
            samples_.push_back(sample);

            if (sample->type_ == SrsFrameTypeVideo) {
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
        map<uint64_t, SrsMp4Sample *>::iterator it;
        for (it = tses.begin(); it != tses.end(); ++it) {
            SrsMp4Sample *sample = it->second;
            if (sample->type_ == SrsFrameTypeAudio) {
                sample->adjust_ = 0 - maxp - maxn;
            }
        }
    }

    return err;
}

SrsMp4Sample *SrsMp4SampleManager::at(uint32_t index)
{
    if (index < samples_.size()) {
        return samples_.at(index);
    }
    return NULL;
}

void SrsMp4SampleManager::append(SrsMp4Sample *sample)
{
    jitter_->on_sample(sample);
    samples_.push_back(sample);
}

srs_error_t SrsMp4SampleManager::write(SrsMp4MovieBox *moov)
{
    srs_error_t err = srs_success;

    SrsMp4TrackBox *vide = moov->video();
    if (vide) {
        bool has_cts = false;
        vector<SrsMp4Sample *>::iterator it;
        for (it = samples_.begin(); it != samples_.end(); ++it) {
            SrsMp4Sample *sample = *it;
            if (sample->dts_ != sample->pts_ && sample->type_ == SrsFrameTypeVideo) {
                has_cts = true;
                break;
            }
        }

        SrsMp4SampleTableBox *stbl = vide->stbl();

        SrsMp4DecodingTime2SampleBox *stts = new SrsMp4DecodingTime2SampleBox();
        stbl->set_stts(stts);

        SrsMp4SyncSampleBox *stss = new SrsMp4SyncSampleBox();
        stbl->set_stss(stss);

        SrsMp4CompositionTime2SampleBox *ctts = NULL;
        if (has_cts) {
            ctts = new SrsMp4CompositionTime2SampleBox();
            stbl->set_ctts(ctts);
        }

        SrsMp4Sample2ChunkBox *stsc = new SrsMp4Sample2ChunkBox();
        stbl->set_stsc(stsc);

        SrsMp4SampleSizeBox *stsz = new SrsMp4SampleSizeBox();
        stbl->set_stsz(stsz);

        SrsMp4FullBox *co = NULL;
        // When sample offset less than UINT32_MAX, we use stco(support 32bit offset) box to save storage space.
        if (samples_.empty() || (*samples_.rbegin())->offset_ < UINT32_MAX) {
            // stco support 32bit offset.
            co = new SrsMp4ChunkOffsetBox();
            stbl->set_stco(static_cast<SrsMp4ChunkOffsetBox *>(co));
        } else {
            // When sample offset bigger than UINT32_MAX, we use co64(support 64bit offset) box to avoid overflow.
            co = new SrsMp4ChunkLargeOffsetBox();
            stbl->set_co64(static_cast<SrsMp4ChunkLargeOffsetBox *>(co));
        }

        if ((err = write_track(SrsFrameTypeVideo, stts, stss, ctts, stsc, stsz, co)) != srs_success) {
            return srs_error_wrap(err, "write vide track");
        }
    }

    SrsMp4TrackBox *soun = moov->audio();
    if (soun) {
        SrsMp4SampleTableBox *stbl = soun->stbl();

        SrsMp4DecodingTime2SampleBox *stts = new SrsMp4DecodingTime2SampleBox();
        stbl->set_stts(stts);

        SrsMp4SyncSampleBox *stss = NULL;
        SrsMp4CompositionTime2SampleBox *ctts = NULL;

        SrsMp4Sample2ChunkBox *stsc = new SrsMp4Sample2ChunkBox();
        stbl->set_stsc(stsc);

        SrsMp4SampleSizeBox *stsz = new SrsMp4SampleSizeBox();
        stbl->set_stsz(stsz);

        SrsMp4FullBox *co = NULL;
        if (samples_.empty() || (*samples_.rbegin())->offset_ < UINT32_MAX) {
            co = new SrsMp4ChunkOffsetBox();
            stbl->set_stco(static_cast<SrsMp4ChunkOffsetBox *>(co));
        } else {
            co = new SrsMp4ChunkLargeOffsetBox();
            stbl->set_co64(static_cast<SrsMp4ChunkLargeOffsetBox *>(co));
        }

        if ((err = write_track(SrsFrameTypeAudio, stts, stss, ctts, stsc, stsz, co)) != srs_success) {
            return srs_error_wrap(err, "write soun track");
        }
    }

    return err;
}

srs_error_t SrsMp4SampleManager::write(SrsMp4TrackFragmentBox *traf, uint64_t dts)
{
    srs_error_t err = srs_success;

    SrsMp4TrackFragmentRunBox *trun = traf->trun();
    trun->flags_ = SrsMp4TrunFlagsDataOffset | SrsMp4TrunFlagsSampleDuration | SrsMp4TrunFlagsSampleSize | SrsMp4TrunFlagsSampleFlag | SrsMp4TrunFlagsSampleCtsOffset;

    SrsMp4Sample *previous = NULL;

    vector<SrsMp4Sample *>::iterator it;
    for (it = samples_.begin(); it != samples_.end(); ++it) {
        SrsMp4Sample *sample = *it;
        SrsMp4TrunEntry *entry = new SrsMp4TrunEntry(trun);

        if (!previous) {
            previous = sample;
            entry->sample_flags_ = 0x02000000;
        } else {
            entry->sample_flags_ = 0x01000000;
        }

        vector<SrsMp4Sample *>::iterator iter = (it + 1);
        if (iter == samples_.end()) {
            entry->sample_duration_ = dts - sample->dts_;
        } else {
            entry->sample_duration_ = (*iter)->dts_ - sample->dts_;
        }

        entry->sample_size_ = sample->nb_data_;
        entry->sample_composition_time_offset_ = (int64_t)(sample->pts_ - sample->dts_);
        if (entry->sample_composition_time_offset_ < 0) {
            trun->version_ = 1;
        }

        trun->entries_.push_back(entry);
    }

    return err;
}

srs_error_t SrsMp4SampleManager::write_track(SrsFrameType track,
                                             SrsMp4DecodingTime2SampleBox *stts, SrsMp4SyncSampleBox *stss, SrsMp4CompositionTime2SampleBox *ctts,
                                             SrsMp4Sample2ChunkBox *stsc, SrsMp4SampleSizeBox *stsz, SrsMp4FullBox *co)
{
    srs_error_t err = srs_success;

    SrsMp4SttsEntry stts_entry;
    vector<SrsMp4SttsEntry> stts_entries;

    SrsMp4CttsEntry ctts_entry;
    vector<SrsMp4CttsEntry> ctts_entries;

    vector<uint32_t> stsz_entries;
    vector<uint64_t> co_entries;
    vector<uint32_t> stss_entries;

    SrsMp4Sample *previous = NULL;
    vector<SrsMp4Sample *>::iterator it;
    for (it = samples_.begin(); it != samples_.end(); ++it) {
        SrsMp4Sample *sample = *it;
        if (sample->type_ != track) {
            continue;
        }

        stsz_entries.push_back(sample->nb_data_);
        co_entries.push_back((uint64_t)sample->offset_);

        if (sample->frame_type_ == SrsVideoAvcFrameTypeKeyFrame) {
            stss_entries.push_back(sample->index_ + 1);
        }

        if (stts) {
            if (previous) {
                uint32_t delta = (uint32_t)(sample->dts_ - previous->dts_);
                if (stts_entry.sample_delta_ == 0 || stts_entry.sample_delta_ == delta) {
                    stts_entry.sample_delta_ = delta;
                    stts_entry.sample_count_++;
                } else {
                    stts_entries.push_back(stts_entry);
                    stts_entry.sample_count_ = 1;
                    stts_entry.sample_delta_ = delta;
                }
            } else {
                // The first sample always in the STTS table.
                stts_entry.sample_count_++;
                stts_entry.sample_delta_ = jitter_->get_first_sample_delta(track);
            }
        }

        if (ctts) {
            int64_t offset = sample->pts_ - sample->dts_;
            if (offset < 0) {
                ctts->version_ = 0x01;
            }
            if (ctts_entry.sample_count_ == 0 || ctts_entry.sample_offset_ == offset) {
                ctts_entry.sample_count_++;
            } else {
                ctts_entries.push_back(ctts_entry);
                ctts_entry.sample_offset_ = offset;
                ctts_entry.sample_count_ = 1;
            }
        }

        previous = sample;
    }

    if (stts && stts_entry.sample_count_) {
        stts_entries.push_back(stts_entry);
    }

    if (ctts && ctts_entry.sample_count_) {
        ctts_entries.push_back(ctts_entry);
    }

    if (stts && !stts_entries.empty()) {
        stts->entries_ = stts_entries;
    }

    if (ctts && !ctts_entries.empty()) {
        ctts->entries_ = ctts_entries;
    }

    if (stsc) {
        stsc->entry_count_ = 1;
        stsc->entries_ = new SrsMp4StscEntry[1];

        SrsMp4StscEntry &v = stsc->entries_[0];
        v.first_chunk_ = v.sample_description_index_ = v.samples_per_chunk_ = 1;
    }

    if (stsz && !stsz_entries.empty()) {
        stsz->sample_size_ = 0;
        stsz->sample_count_ = (uint32_t)stsz_entries.size();
        stsz->entry_sizes_ = new uint32_t[stsz->sample_count_];
        for (int i = 0; i < (int)stsz->sample_count_; i++) {
            stsz->entry_sizes_[i] = stsz_entries.at(i);
        }
    }

    if (!co_entries.empty()) {
        SrsMp4ChunkOffsetBox *stco = dynamic_cast<SrsMp4ChunkOffsetBox *>(co);
        SrsMp4ChunkLargeOffsetBox *co64 = dynamic_cast<SrsMp4ChunkLargeOffsetBox *>(co);

        if (stco) {
            stco->entry_count_ = (uint32_t)co_entries.size();
            stco->entries_ = new uint32_t[stco->entry_count_];
            for (int i = 0; i < (int)stco->entry_count_; i++) {
                stco->entries_[i] = co_entries.at(i);
            }
        } else if (co64) {
            co64->entry_count_ = (uint32_t)co_entries.size();
            co64->entries_ = new uint64_t[co64->entry_count_];
            for (int i = 0; i < (int)co64->entry_count_; i++) {
                co64->entries_[i] = co_entries.at(i);
            }
        }
    }

    if (stss && !stss_entries.empty()) {
        stss->entry_count_ = (uint32_t)stss_entries.size();
        stss->sample_numbers_ = new uint32_t[stss->entry_count_];
        for (int i = 0; i < (int)stss->entry_count_; i++) {
            stss->sample_numbers_[i] = stss_entries.at(i);
        }
    }

    return err;
}

srs_error_t SrsMp4SampleManager::do_load(map<uint64_t, SrsMp4Sample *> &tses, SrsMp4MovieBox *moov)
{
    srs_error_t err = srs_success;

    SrsMp4TrackBox *vide = moov->video();
    if (vide) {
        SrsMp4MediaHeaderBox *mdhd = vide->mdhd();
        SrsMp4TrackType tt = vide->track_type();
        SrsMp4ChunkOffsetBox *stco = vide->stco();
        SrsMp4SampleSizeBox *stsz = vide->stsz();
        SrsMp4Sample2ChunkBox *stsc = vide->stsc();
        SrsMp4DecodingTime2SampleBox *stts = vide->stts();
        // The composition time to sample table is optional and must only be present if DT and CT differ for any samples.
        SrsMp4CompositionTime2SampleBox *ctts = vide->ctts();
        // If the sync sample box is not present, every sample is a sync sample.
        SrsMp4SyncSampleBox *stss = vide->stss();

        if (!mdhd || !stco || !stsz || !stsc || !stts) {
            return srs_error_new(ERROR_MP4_ILLEGAL_TRACK, "illegal track, empty mdhd/stco/stsz/stsc/stts, type=%d", tt);
        }

        if ((err = load_trak(tses, SrsFrameTypeVideo, mdhd, stco, stsz, stsc, stts, ctts, stss)) != srs_success) {
            return srs_error_wrap(err, "load vide track");
        }
    }

    SrsMp4TrackBox *soun = moov->audio();
    if (soun) {
        SrsMp4MediaHeaderBox *mdhd = soun->mdhd();
        SrsMp4TrackType tt = soun->track_type();
        SrsMp4ChunkOffsetBox *stco = soun->stco();
        SrsMp4SampleSizeBox *stsz = soun->stsz();
        SrsMp4Sample2ChunkBox *stsc = soun->stsc();
        SrsMp4DecodingTime2SampleBox *stts = soun->stts();

        if (!mdhd || !stco || !stsz || !stsc || !stts) {
            return srs_error_new(ERROR_MP4_ILLEGAL_TRACK, "illegal track, empty mdhd/stco/stsz/stsc/stts, type=%d", tt);
        }

        if ((err = load_trak(tses, SrsFrameTypeAudio, mdhd, stco, stsz, stsc, stts, NULL, NULL)) != srs_success) {
            return srs_error_wrap(err, "load soun track");
        }
    }

    return err;
}

srs_error_t SrsMp4SampleManager::load_trak(map<uint64_t, SrsMp4Sample *> &tses, SrsFrameType tt,
                                           SrsMp4MediaHeaderBox *mdhd, SrsMp4ChunkOffsetBox *stco, SrsMp4SampleSizeBox *stsz, SrsMp4Sample2ChunkBox *stsc,
                                           SrsMp4DecodingTime2SampleBox *stts, SrsMp4CompositionTime2SampleBox *ctts, SrsMp4SyncSampleBox *stss)
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

    SrsMp4Sample *previous = NULL;

    // For each chunk offset.
    for (uint32_t ci = 0; ci < stco->entry_count_; ci++) {
        // The sample offset relative in chunk.
        uint32_t sample_relative_offset = 0;

        // Find how many samples from stsc.
        SrsMp4StscEntry *stsc_entry = stsc->on_chunk(ci);
        for (uint32_t i = 0; i < stsc_entry->samples_per_chunk_; i++) {
            SrsMp4Sample *sample = new SrsMp4Sample();
            sample->type_ = tt;
            sample->index_ = (previous ? previous->index_ + 1 : 0);
            sample->tbn_ = mdhd->timescale_;
            sample->offset_ = stco->entries_[ci] + sample_relative_offset;

            uint32_t sample_size = 0;
            if ((err = stsz->get_sample_size(sample->index_, &sample_size)) != srs_success) {
                srs_freep(sample);
                return srs_error_wrap(err, "stsz get sample size");
            }
            sample_relative_offset += sample_size;

            SrsMp4SttsEntry *stts_entry = NULL;
            if ((err = stts->on_sample(sample->index_, &stts_entry)) != srs_success) {
                srs_freep(sample);
                return srs_error_wrap(err, "stts on sample");
            }
            if (previous) {
                sample->pts_ = sample->dts_ = previous->dts_ + stts_entry->sample_delta_;
            }

            SrsMp4CttsEntry *ctts_entry = NULL;
            if (ctts && (err = ctts->on_sample(sample->index_, &ctts_entry)) != srs_success) {
                srs_freep(sample);
                return srs_error_wrap(err, "ctts on sample");
            }
            if (ctts_entry) {
                sample->pts_ = sample->dts_ + ctts_entry->sample_offset_;
            }

            if (tt == SrsFrameTypeVideo) {
                if (!stss || stss->is_sync(sample->index_)) {
                    sample->frame_type_ = SrsVideoAvcFrameTypeKeyFrame;
                } else {
                    sample->frame_type_ = SrsVideoAvcFrameTypeInterFrame;
                }
            }

            // Only set the sample size, read data from io when needed.
            sample->nb_data_ = sample_size;
            sample->data_ = NULL;

            previous = sample;
            tses[sample->offset_] = sample;
        }
    }

    // Check total samples.
    if (previous && previous->index_ + 1 != stsz->sample_count_) {
        return srs_error_new(ERROR_MP4_ILLEGAL_SAMPLES, "illegal samples count, expect=%d, actual=%d", stsz->sample_count_, previous->index_ + 1);
    }

    return err;
}

SrsMp4BoxReader::SrsMp4BoxReader()
{
    rsio_ = NULL;
    buf_ = new char[SRS_MP4_BUF_SIZE];
}

SrsMp4BoxReader::~SrsMp4BoxReader()
{
    srs_freepa(buf_);
}

srs_error_t SrsMp4BoxReader::initialize(ISrsReadSeeker *rs)
{
    rsio_ = rs;

    return srs_success;
}

srs_error_t SrsMp4BoxReader::read(SrsSimpleStream *stream, SrsMp4Box **ppbox)
{
    srs_error_t err = srs_success;

    SrsMp4Box *box = NULL;
    if ((err = do_read(stream, box)) == srs_success) {
        *ppbox = box;
        return err;
    }

    // When error, free the created box.
    srs_freep(box);

    return err;
}

srs_error_t SrsMp4BoxReader::do_read(SrsSimpleStream *stream, SrsMp4Box *&box)
{
    srs_error_t err = srs_success;

    while (true) {
        // For the first time to read the box, maybe it's a basic box which is only 4bytes header.
        // When we disconvery the real box, we know the size of the whole box, then read again and decode it.
        uint64_t required = box ? box->sz() : 4;

        // For mdat box, we only requires to decode the header.
        if (box && box->is_mdat()) {
            required = box->sz_header();
        }

        // Fill the stream util we can discovery box.
        while (stream->length() < (int)required) {
            ssize_t nread;
            if ((err = rsio_->read(buf_, SRS_MP4_BUF_SIZE, &nread)) != srs_success) {
                return srs_error_wrap(err, "load failed, nread=%d, required=%d", (int)nread, (int)required);
            }

            srs_assert(nread > 0);
            stream->append(buf_, (int)nread);
        }

        SrsUniquePtr<SrsBuffer> buffer(new SrsBuffer(stream->bytes(), stream->length()));

        // Discovery the box with basic header.
        if (!box && (err = SrsMp4Box::discovery(buffer.get(), &box)) != srs_success) {
            if (srs_error_code(err) == ERROR_MP4_BOX_REQUIRE_SPACE) {
                srs_freep(err);
                continue;
            }
            return srs_error_wrap(err, "load box failed");
        }

        // When box is discoveried, check whether we can demux the whole box.
        // For mdat, only the header is required.
        required = (box->is_mdat() ? box->sz_header() : box->sz());
        if (!buffer->require((int)required)) {
            continue;
        }

        break;
    }

    return err;
}

srs_error_t SrsMp4BoxReader::skip(SrsMp4Box *box, SrsSimpleStream *stream)
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
        if (offset > 0 && (err = rsio_->lseek(offset, SEEK_CUR, NULL)) != srs_success) {
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
    rsio_ = NULL;
    brand_ = SrsMp4BoxBrandForbidden;
    stream_ = new SrsSimpleStream();
    vcodec_ = SrsVideoCodecIdForbidden;
    acodec_ = SrsAudioCodecIdForbidden;
    asc_written_ = avcc_written_ = false;
    sample_rate_ = SrsAudioSampleRateForbidden;
    sound_bits_ = SrsAudioSampleBitsForbidden;
    channels_ = SrsAudioChannelsForbidden;
    samples_ = new SrsMp4SampleManager();
    br_ = new SrsMp4BoxReader();
    current_index_ = 0;
    current_offset_ = 0;
}

SrsMp4Decoder::~SrsMp4Decoder()
{
    srs_freep(br_);
    srs_freep(stream_);
    srs_freep(samples_);
}

srs_error_t SrsMp4Decoder::initialize(ISrsReadSeeker *rs)
{
    srs_error_t err = srs_success;

    srs_assert(rs);
    rsio_ = rs;

    if ((err = br_->initialize(rs)) != srs_success) {
        return srs_error_wrap(err, "init box reader");
    }

    // For mdat before moov, we must reset the offset to the mdat.
    off_t offset = -1;

    while (true) {
        SrsMp4Box *box_raw = NULL;
        if ((err = load_next_box(&box_raw, 0)) != srs_success) {
            return srs_error_wrap(err, "load box");
        }
        SrsUniquePtr<SrsMp4Box> box(box_raw);

        if (box->is_ftyp()) {
            SrsMp4FileTypeBox *ftyp = dynamic_cast<SrsMp4FileTypeBox *>(box.get());
            if ((err = parse_ftyp(ftyp)) != srs_success) {
                return srs_error_wrap(err, "parse ftyp");
            }
        } else if (box->is_mdat()) {
            off_t cur = 0;
            if ((err = rsio_->lseek(0, SEEK_CUR, &cur)) != srs_success) {
                return srs_error_wrap(err, "io seek");
            }
            offset = off_t(cur - box->sz());
        } else if (box->is_moov()) {
            SrsMp4MovieBox *moov = dynamic_cast<SrsMp4MovieBox *>(box.get());
            if ((err = parse_moov(moov)) != srs_success) {
                return srs_error_wrap(err, "parse moov");
            }
            break;
        }
    }

    if (brand_ == SrsMp4BoxBrandForbidden) {
        return srs_error_new(ERROR_MP4_BOX_ILLEGAL_SCHEMA, "missing ftyp");
    }

    // Set the offset to the mdat.
    if (offset >= 0) {
        if ((err = rsio_->lseek(offset, SEEK_SET, &current_offset_)) != srs_success) {
            return srs_error_wrap(err, "seek to mdat");
        }
    }

    return err;
}

srs_error_t SrsMp4Decoder::read_sample(SrsMp4HandlerType *pht, uint16_t *pft, uint16_t *pct, uint32_t *pdts, uint32_t *ppts, uint8_t **psample, uint32_t *pnb_sample)
{
    srs_error_t err = srs_success;

    if (!avcc_written_ && !pavcc_.empty()) {
        avcc_written_ = true;
        *pdts = *ppts = 0;
        *pht = SrsMp4HandlerTypeVIDE;

        uint32_t nb_sample = *pnb_sample = (uint32_t)pavcc_.size();
        uint8_t *sample = *psample = new uint8_t[nb_sample];
        memcpy(sample, &pavcc_[0], nb_sample);

        *pft = SrsVideoAvcFrameTypeKeyFrame;
        *pct = SrsVideoAvcFrameTraitSequenceHeader;

        return err;
    }

    if (!asc_written_ && !pasc_.empty()) {
        asc_written_ = true;
        *pdts = *ppts = 0;
        *pht = SrsMp4HandlerTypeSOUN;

        uint32_t nb_sample = *pnb_sample = (uint32_t)pasc_.size();
        uint8_t *sample = *psample = new uint8_t[nb_sample];
        memcpy(sample, &pasc_[0], nb_sample);

        *pft = 0x00;
        *pct = SrsAudioAacFrameTraitSequenceHeader;

        return err;
    }

    SrsMp4Sample *ps = samples_->at(current_index_++);
    if (!ps) {
        return srs_error_new(ERROR_SYSTEM_FILE_EOF, "EOF");
    }

    if (ps->type_ == SrsFrameTypeVideo) {
        *pht = SrsMp4HandlerTypeVIDE;
        *pct = SrsVideoAvcFrameTraitNALU;
    } else {
        *pht = SrsMp4HandlerTypeSOUN;
        *pct = SrsAudioAacFrameTraitRawData;
    }
    *pdts = ps->dts_ms();
    *ppts = ps->pts_ms();
    *pft = ps->frame_type_;

    // Read sample from io, for we never preload the samples(too large).
    if (ps->offset_ != current_offset_) {
        if ((err = rsio_->lseek(ps->offset_, SEEK_SET, &current_offset_)) != srs_success) {
            return srs_error_wrap(err, "seek to sample");
        }
    }

    uint32_t nb_sample = ps->nb_data_;
    uint8_t *sample = new uint8_t[nb_sample];
    // TODO: FIXME: Use fully read.
    if ((err = rsio_->read(sample, nb_sample, NULL)) != srs_success) {
        srs_freepa(sample);
        return srs_error_wrap(err, "read sample");
    }

    *psample = sample;
    *pnb_sample = nb_sample;
    current_offset_ += nb_sample;

    return err;
}

srs_error_t SrsMp4Decoder::parse_ftyp(SrsMp4FileTypeBox *ftyp)
{
    srs_error_t err = srs_success;

    // File Type Box (ftyp)
    bool legal_brand = false;
    static SrsMp4BoxBrand legal_brands[] = {
        SrsMp4BoxBrandISOM, SrsMp4BoxBrandISO2, SrsMp4BoxBrandAVC1, SrsMp4BoxBrandMP41,
        SrsMp4BoxBrandISO5};
    for (int i = 0; i < (int)(sizeof(legal_brands) / sizeof(SrsMp4BoxBrand)); i++) {
        if (ftyp->major_brand_ == legal_brands[i]) {
            legal_brand = true;
            break;
        }
    }
    if (!legal_brand) {
        return srs_error_new(ERROR_MP4_BOX_ILLEGAL_BRAND, "brand is illegal, brand=%d", ftyp->major_brand_);
    }

    brand_ = ftyp->major_brand_;

    return err;
}

srs_error_t SrsMp4Decoder::parse_moov(SrsMp4MovieBox *moov)
{
    srs_error_t err = srs_success;

    SrsMp4MovieHeaderBox *mvhd = moov->mvhd();
    if (!mvhd) {
        return srs_error_new(ERROR_MP4_ILLEGAL_MOOV, "missing mvhd");
    }

    SrsMp4TrackBox *vide = moov->video();
    SrsMp4TrackBox *soun = moov->audio();
    if (!vide && !soun) {
        return srs_error_new(ERROR_MP4_ILLEGAL_MOOV, "missing audio and video track");
    }

    SrsMp4AudioSampleEntry *mp4a = soun ? soun->mp4a() : NULL;
    if (mp4a) {
        uint32_t sr = mp4a->samplerate_ >> 16;
        if ((sample_rate_ = srs_audio_sample_rate_from_number(sr)) == SrsAudioSampleRateForbidden) {
            sample_rate_ = srs_audio_sample_rate_guess_number(sr);
        }

        if (mp4a->samplesize_ == 16) {
            sound_bits_ = SrsAudioSampleBits16bit;
        } else {
            sound_bits_ = SrsAudioSampleBits8bit;
        }

        if (mp4a->channelcount_ == 2) {
            channels_ = SrsAudioChannelsStereo;
        } else {
            channels_ = SrsAudioChannelsMono;
        }
    }

    SrsMp4AvccBox *avcc = vide ? vide->avcc() : NULL;
    SrsMp4DecoderSpecificInfo *asc = soun ? soun->asc() : NULL;
    if (vide && !avcc) {
        return srs_error_new(ERROR_MP4_ILLEGAL_MOOV, "missing video sequence header");
    }
    if (soun && !asc && soun->soun_codec() == SrsAudioCodecIdAAC) {
        return srs_error_new(ERROR_MP4_ILLEGAL_MOOV, "missing audio sequence header");
    }

    vcodec_ = vide ? vide->vide_codec() : SrsVideoCodecIdForbidden;
    acodec_ = soun ? soun->soun_codec() : SrsAudioCodecIdForbidden;

    if (avcc && !avcc->avc_config_.empty()) {
        pavcc_ = avcc->avc_config_;
    }
    if (asc && !asc->asc_.empty()) {
        pasc_ = asc->asc_;
    }

    // Build the samples structure from moov.
    if ((err = samples_->load(moov)) != srs_success) {
        return srs_error_wrap(err, "load samples");
    }

    stringstream ss;
    ss << "dur=" << mvhd->duration() << "ms";
    // video codec.
    ss << ", vide=" << moov->nb_vide_tracks() << "("
       << srs_video_codec_id2str(vcodec_) << "," << pavcc_.size() << "BSH"
       << ")";
    // audio codec.
    ss << ", soun=" << moov->nb_soun_tracks() << "("
       << srs_audio_codec_id2str(acodec_) << "," << pasc_.size() << "BSH"
       << "," << srs_audio_channels2str(channels_)
       << "," << srs_audio_sample_bits2str(sound_bits_)
       << "," << srs_audio_sample_rate2str(sample_rate_)
       << ")";

    srs_trace("MP4 moov %s", ss.str().c_str());

    return err;
}

srs_error_t SrsMp4Decoder::load_next_box(SrsMp4Box **ppbox, uint32_t required_box_type)
{
    srs_error_t err = srs_success;

    while (true) {
        SrsMp4Box *box = NULL;
        if ((err = do_load_next_box(&box, required_box_type)) != srs_success) {
            return srs_error_wrap(err, "load box");
        }

        if (!required_box_type || (uint32_t)box->type_ == required_box_type) {
            *ppbox = box;
            return err;
        }

        // Free the box is not matched the required type.
        srs_freep(box);
    }

    return err;
}

srs_error_t SrsMp4Decoder::do_load_next_box(SrsMp4Box **ppbox, uint32_t required_box_type)
{
    srs_error_t err = srs_success;

    while (true) {
        SrsMp4Box *box = NULL;

        if ((err = br_->read(stream_, &box)) != srs_success) {
            return srs_error_wrap(err, "read box");
        }

        SrsUniquePtr<SrsBuffer> buffer(new SrsBuffer(stream_->bytes(), stream_->length()));

        // Decode the box:
        // 1. Any box, when no box type is required.
        // 2. Matched box, when box type match the required type.
        // 3. Mdat box, always decode the mdat because we only decode the header of it.
        if (!required_box_type || (uint32_t)box->type_ == required_box_type || box->is_mdat()) {
            err = box->decode(buffer.get());
        }

        // Skip the box from stream, move stream to next box.
        // For mdat box, skip the content in stream or underylayer reader.
        // For other boxes, skip it from stream because we already decoded it or ignore it.
        if (err == srs_success) {
            err = br_->skip(box, stream_);
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

ISrsMp4Encoder::ISrsMp4Encoder()
{
}

ISrsMp4Encoder::~ISrsMp4Encoder()
{
}

SrsMp4Encoder::SrsMp4Encoder()
{
    wsio_ = NULL;
    mdat_bytes_ = 0;
    mdat_offset_ = 0;
    nb_audios_ = nb_videos_ = 0;
    samples_ = new SrsMp4SampleManager();
    aduration_ = vduration_ = 0;
    width_ = height_ = 0;

    acodec_ = SrsAudioCodecIdForbidden;
    sample_rate_ = SrsAudioSampleRateForbidden;
    sound_bits_ = SrsAudioSampleBitsForbidden;
    channels_ = SrsAudioChannelsForbidden;
    vcodec_ = SrsVideoCodecIdForbidden;
}

SrsMp4Encoder::~SrsMp4Encoder()
{
    srs_freep(samples_);
}

srs_error_t SrsMp4Encoder::initialize(ISrsWriteSeeker *ws)
{
    srs_error_t err = srs_success;

    wsio_ = ws;

    // Write ftyp box.
    if (true) {
        SrsUniquePtr<SrsMp4FileTypeBox> ftyp(new SrsMp4FileTypeBox());

        ftyp->major_brand_ = SrsMp4BoxBrandISOM;
        ftyp->minor_version_ = 512;
        ftyp->set_compatible_brands(SrsMp4BoxBrandISOM, SrsMp4BoxBrandISO2, SrsMp4BoxBrandMP41);

        int nb_data = ftyp->nb_bytes();
        std::vector<char> data(nb_data);

        SrsUniquePtr<SrsBuffer> buffer(new SrsBuffer(&data[0], nb_data));
        if ((err = ftyp->encode(buffer.get())) != srs_success) {
            return srs_error_wrap(err, "encode ftyp");
        }

        // TODO: FIXME: Ensure write ok.
        if ((err = wsio_->write(&data[0], nb_data, NULL)) != srs_success) {
            return srs_error_wrap(err, "write ftyp");
        }
    }

    // 8B reserved free box.
    if (true) {
        SrsUniquePtr<SrsMp4FreeSpaceBox> freeb(new SrsMp4FreeSpaceBox(SrsMp4BoxTypeFREE));

        int nb_data = freeb->nb_bytes();
        std::vector<char> data(nb_data);

        SrsUniquePtr<SrsBuffer> buffer(new SrsBuffer(&data[0], nb_data));
        if ((err = freeb->encode(buffer.get())) != srs_success) {
            return srs_error_wrap(err, "encode free box");
        }

        if ((err = wsio_->write(&data[0], nb_data, NULL)) != srs_success) {
            return srs_error_wrap(err, "write free box");
        }
    }

    // Write mdat box.
    if (true) {
        // Write empty mdat box,
        // its payload will be writen by samples,
        // and we will update its header(size) when flush.
        SrsUniquePtr<SrsMp4MediaDataBox> mdat(new SrsMp4MediaDataBox());

        // Update the mdat box from this offset.
        if ((err = wsio_->lseek(0, SEEK_CUR, &mdat_offset_)) != srs_success) {
            return srs_error_wrap(err, "seek to mdat");
        }

        int nb_data = mdat->sz_header();
        SrsUniquePtr<uint8_t[]> data(new uint8_t[nb_data]);

        SrsUniquePtr<SrsBuffer> buffer(new SrsBuffer((char *)data.get(), nb_data));
        if ((err = mdat->encode(buffer.get())) != srs_success) {
            return srs_error_wrap(err, "encode mdat");
        }

        // TODO: FIXME: Ensure all bytes are writen.
        if ((err = wsio_->write(data.get(), nb_data, NULL)) != srs_success) {
            return srs_error_wrap(err, "write mdat");
        }

        mdat_bytes_ = 0;
    }

    return err;
}

srs_error_t SrsMp4Encoder::write_sample(
    SrsFormat *format, SrsMp4HandlerType ht, uint16_t ft, uint16_t ct, uint32_t dts, uint32_t pts,
    uint8_t *sample, uint32_t nb_sample)
{
    srs_error_t err = srs_success;

    SrsMp4Sample *ps = new SrsMp4Sample();

    // For SPS/PPS or ASC, copy it to moov.
    bool vsh = (ht == SrsMp4HandlerTypeVIDE) && (ct == (uint16_t)SrsVideoAvcFrameTraitSequenceHeader);
    bool ash = (ht == SrsMp4HandlerTypeSOUN) && (ct == (uint16_t)SrsAudioAacFrameTraitSequenceHeader);
    if (vsh || ash) {
        err = copy_sequence_header(format, vsh, sample, nb_sample);
        srs_freep(ps);
        return err;
    }

    if (ht == SrsMp4HandlerTypeVIDE) {
        ps->type_ = SrsFrameTypeVideo;
        ps->frame_type_ = (SrsVideoAvcFrameType)ft;
        ps->index_ = nb_videos_++;
        vduration_ = dts;
    } else if (ht == SrsMp4HandlerTypeSOUN) {
        ps->type_ = SrsFrameTypeAudio;
        ps->index_ = nb_audios_++;
        aduration_ = dts;
    } else {
        srs_freep(ps);
        return err;
    }
    ps->tbn_ = 1000;
    ps->dts_ = dts;
    ps->pts_ = pts;

    if ((err = do_write_sample(ps, sample, nb_sample)) != srs_success) {
        srs_freep(ps);
        return srs_error_wrap(err, "write sample");
    }

    // Append to manager to build the moov.
    samples_->append(ps);

    return err;
}

srs_error_t SrsMp4Encoder::flush()
{
    srs_error_t err = srs_success;

    if (!nb_audios_ && !nb_videos_) {
        return srs_error_new(ERROR_MP4_ILLEGAL_MOOV, "Missing audio and video track, nb_audios=%d, nb_videos=%d", nb_audios_, nb_videos_);
    }

    // Write moov.
    if (true) {
        SrsUniquePtr<SrsMp4MovieBox> moov(new SrsMp4MovieBox());

        SrsMp4MovieHeaderBox *mvhd = new SrsMp4MovieHeaderBox();
        moov->set_mvhd(mvhd);

        mvhd->timescale_ = 1000; // Use tbn ms.
        mvhd->duration_in_tbn_ = srs_max(vduration_, aduration_);
        mvhd->next_track_ID_ = 1; // Starts from 1, increase when use it.

        if (nb_videos_ || !pavcc_.empty() || !phvcc_.empty()) {
            SrsMp4TrackBox *trak = new SrsMp4TrackBox();
            moov->add_trak(trak);

            SrsMp4EditBox *edts = new SrsMp4EditBox();
            trak->set_edts(edts);

            SrsMp4EditListBox *elst = new SrsMp4EditListBox();
            edts->set_elst(elst);
            elst->version_ = 0;

            SrsMp4ElstEntry entry;
            entry.segment_duration_ = mvhd->duration_in_tbn_;
            entry.media_rate_integer_ = 1;
            elst->entries_.push_back(entry);

            SrsMp4TrackHeaderBox *tkhd = new SrsMp4TrackHeaderBox();
            trak->set_tkhd(tkhd);

            tkhd->track_ID_ = mvhd->next_track_ID_++;
            tkhd->duration_ = vduration_;
            tkhd->width_ = (width_ << 16);
            tkhd->height_ = (height_ << 16);

            SrsMp4MediaBox *mdia = new SrsMp4MediaBox();
            trak->set_mdia(mdia);

            SrsMp4MediaHeaderBox *mdhd = new SrsMp4MediaHeaderBox();
            mdia->set_mdhd(mdhd);

            mdhd->timescale_ = 1000;
            mdhd->duration_ = vduration_;
            mdhd->set_language0('u');
            mdhd->set_language1('n');
            mdhd->set_language2('d');

            SrsMp4HandlerReferenceBox *hdlr = new SrsMp4HandlerReferenceBox();
            mdia->set_hdlr(hdlr);

            hdlr->handler_type_ = SrsMp4HandlerTypeVIDE;
            hdlr->name_ = "VideoHandler";

            SrsMp4MediaInformationBox *minf = new SrsMp4MediaInformationBox();
            mdia->set_minf(minf);

            SrsMp4VideoMeidaHeaderBox *vmhd = new SrsMp4VideoMeidaHeaderBox();
            minf->set_vmhd(vmhd);

            SrsMp4DataInformationBox *dinf = new SrsMp4DataInformationBox();
            minf->set_dinf(dinf);

            SrsMp4DataReferenceBox *dref = new SrsMp4DataReferenceBox();
            dinf->set_dref(dref);

            SrsMp4DataEntryBox *url = new SrsMp4DataEntryUrlBox();
            dref->append(url);

            SrsMp4SampleTableBox *stbl = new SrsMp4SampleTableBox();
            minf->set_stbl(stbl);

            SrsMp4SampleDescriptionBox *stsd = new SrsMp4SampleDescriptionBox();
            stbl->set_stsd(stsd);

            if (vcodec_ == SrsVideoCodecIdAVC) {
                SrsMp4VisualSampleEntry *avc1 = new SrsMp4VisualSampleEntry(SrsMp4BoxTypeAVC1);
                stsd->append(avc1);

                avc1->width_ = width_;
                avc1->height_ = height_;
                avc1->data_reference_index_ = 1;

                SrsMp4AvccBox *avcC = new SrsMp4AvccBox();
                avc1->set_avcC(avcC);

                avcC->avc_config_ = pavcc_;
            } else {
                SrsMp4VisualSampleEntry *hev1 = new SrsMp4VisualSampleEntry(SrsMp4BoxTypeHEV1);
                stsd->append(hev1);

                hev1->width_ = width_;
                hev1->height_ = height_;
                hev1->data_reference_index_ = 1;

                SrsMp4HvcCBox *hvcC = new SrsMp4HvcCBox();
                hev1->set_hvcC(hvcC);

                hvcC->hevc_config_ = phvcc_;
            }
        }

        if (nb_audios_ || !pasc_.empty()) {
            SrsMp4TrackBox *trak = new SrsMp4TrackBox();
            moov->add_trak(trak);

            SrsMp4TrackHeaderBox *tkhd = new SrsMp4TrackHeaderBox();
            tkhd->volume_ = 0x0100;
            trak->set_tkhd(tkhd);

            tkhd->track_ID_ = mvhd->next_track_ID_++;
            tkhd->duration_ = aduration_;

            SrsMp4MediaBox *mdia = new SrsMp4MediaBox();
            trak->set_mdia(mdia);

            SrsMp4MediaHeaderBox *mdhd = new SrsMp4MediaHeaderBox();
            mdia->set_mdhd(mdhd);

            mdhd->timescale_ = 1000;
            mdhd->duration_ = aduration_;
            mdhd->set_language0('u');
            mdhd->set_language1('n');
            mdhd->set_language2('d');

            SrsMp4HandlerReferenceBox *hdlr = new SrsMp4HandlerReferenceBox();
            mdia->set_hdlr(hdlr);

            hdlr->handler_type_ = SrsMp4HandlerTypeSOUN;
            hdlr->name_ = "SoundHandler";

            SrsMp4MediaInformationBox *minf = new SrsMp4MediaInformationBox();
            mdia->set_minf(minf);

            SrsMp4SoundMeidaHeaderBox *smhd = new SrsMp4SoundMeidaHeaderBox();
            minf->set_smhd(smhd);

            SrsMp4DataInformationBox *dinf = new SrsMp4DataInformationBox();
            minf->set_dinf(dinf);

            SrsMp4DataReferenceBox *dref = new SrsMp4DataReferenceBox();
            dinf->set_dref(dref);

            SrsMp4DataEntryBox *url = new SrsMp4DataEntryUrlBox();
            dref->append(url);

            SrsMp4SampleTableBox *stbl = new SrsMp4SampleTableBox();
            minf->set_stbl(stbl);

            SrsMp4SampleDescriptionBox *stsd = new SrsMp4SampleDescriptionBox();
            stbl->set_stsd(stsd);

            SrsMp4AudioSampleEntry *mp4a = new SrsMp4AudioSampleEntry();
            mp4a->data_reference_index_ = 1;
            mp4a->samplerate_ = srs_audio_sample_rate2number(sample_rate_);
            if (sound_bits_ == SrsAudioSampleBits16bit) {
                mp4a->samplesize_ = 16;
            } else {
                mp4a->samplesize_ = 8;
            }
            if (channels_ == SrsAudioChannelsStereo) {
                mp4a->channelcount_ = 2;
            } else {
                mp4a->channelcount_ = 1;
            }
            stsd->append(mp4a);

            SrsMp4EsdsBox *esds = new SrsMp4EsdsBox();
            mp4a->set_esds(esds);

            SrsMp4ES_Descriptor *es = esds->es;
            es->ES_ID_ = 0x02;

            SrsMp4DecoderConfigDescriptor &desc = es->decConfigDescr_;
            desc.objectTypeIndication = get_audio_object_type();
            desc.streamType = SrsMp4StreamTypeAudioStream;
            srs_freep(desc.decSpecificInfo_);

            if (SrsMp4ObjectTypeAac == desc.objectTypeIndication) {
                SrsMp4DecoderSpecificInfo *asc = new SrsMp4DecoderSpecificInfo();
                desc.decSpecificInfo_ = asc;
                asc->asc_ = pasc_;
            }
        }

        if ((err = samples_->write(moov.get())) != srs_success) {
            return srs_error_wrap(err, "write samples");
        }

        int nb_data = moov->nb_bytes();
        SrsUniquePtr<uint8_t[]> data(new uint8_t[nb_data]);

        SrsUniquePtr<SrsBuffer> buffer(new SrsBuffer((char *)data.get(), nb_data));
        if ((err = moov->encode(buffer.get())) != srs_success) {
            return srs_error_wrap(err, "encode moov");
        }

        // TODO: FIXME: Ensure all bytes are writen.
        if ((err = wsio_->write(data.get(), nb_data, NULL)) != srs_success) {
            return srs_error_wrap(err, "write moov");
        }
    }

    // Write mdat box.
    if (true) {
        // Write mdat box with size of data,
        // its payload already writen by samples,
        // and we will update its header(size) when flush.
        SrsUniquePtr<SrsMp4MediaDataBox> mdat(new SrsMp4MediaDataBox());

        // Update the size of mdat first, for over 2GB file.
        mdat->nb_data_ = mdat_bytes_;
        mdat->update_size();

        int nb_data = mdat->sz_header();
        SrsUniquePtr<uint8_t[]> data(new uint8_t[nb_data]);

        SrsUniquePtr<SrsBuffer> buffer(new SrsBuffer((char *)data.get(), nb_data));
        if ((err = mdat->encode(buffer.get())) != srs_success) {
            return srs_error_wrap(err, "encode mdat");
        }

        // We might adjust the offset of mdat, for large size, 2GB+ as such.
        if (nb_data > 8) {
            // For large size, the header of mdat MUST be 16.
            if (nb_data != 16) {
                return srs_error_new(ERROR_MP4_ILLEGAL_MDAT, "Invalid mdat header size %d", nb_data);
            }
            // Use large size, to the start of reserved free box.
            mdat_offset_ -= 8;
        }

        // Seek to the start of mdat.
        if ((err = wsio_->lseek(mdat_offset_, SEEK_SET, NULL)) != srs_success) {
            return srs_error_wrap(err, "seek to mdat");
        }

        // TODO: FIXME: Ensure all bytes are writen.
        if ((err = wsio_->write(data.get(), nb_data, NULL)) != srs_success) {
            return srs_error_wrap(err, "write mdat");
        }
    }

    return err;
}

void SrsMp4Encoder::set_audio_codec(SrsAudioCodecId vcodec, SrsAudioSampleRate sample_rate, SrsAudioSampleBits sound_bits, SrsAudioChannels channels)
{
    acodec_ = vcodec;
    sample_rate_ = sample_rate;
    sound_bits_ = sound_bits;
    channels_ = channels;
}

srs_error_t SrsMp4Encoder::copy_sequence_header(SrsFormat *format, bool vsh, uint8_t *sample, uint32_t nb_sample)
{
    srs_error_t err = srs_success;

    if (vsh) {
        // AVC
        if (format->vcodec_->id_ == SrsVideoCodecIdAVC && !pavcc_.empty()) {
            if (nb_sample == (uint32_t)pavcc_.size() && srs_bytes_equal(sample, &pavcc_[0], (int)pavcc_.size())) {
                return err;
            }

            return srs_error_new(ERROR_MP4_AVCC_CHANGE, "doesn't support avcc change");
        }
        // HEVC
        if (format->vcodec_->id_ == SrsVideoCodecIdHEVC && !phvcc_.empty()) {
            if (nb_sample == (uint32_t)phvcc_.size() && srs_bytes_equal(sample, &phvcc_[0], (int)phvcc_.size())) {
                return err;
            }

            return srs_error_new(ERROR_MP4_HVCC_CHANGE, "doesn't support hvcC change");
        }
    }

    if (!vsh && !pasc_.empty()) {
        if (nb_sample == (uint32_t)pasc_.size() && srs_bytes_equal(sample, &pasc_[0], (int)pasc_.size())) {
            return err;
        }

        return srs_error_new(ERROR_MP4_ASC_CHANGE, "doesn't support asc change");
    }

    if (vsh) {
        if (format->vcodec_->id_ == SrsVideoCodecIdHEVC) {
            phvcc_ = std::vector<char>(sample, sample + nb_sample);
        } else {
            pavcc_ = std::vector<char>(sample, sample + nb_sample);
        }
        if (format && format->vcodec_) {
            vcodec_ = format->vcodec_->id_;
            width_ = format->vcodec_->width_;
            height_ = format->vcodec_->height_;
        }
    }

    if (!vsh) {
        pasc_ = std::vector<char>(sample, sample + nb_sample);
    }

    return err;
}

srs_error_t SrsMp4Encoder::do_write_sample(SrsMp4Sample *ps, uint8_t *sample, uint32_t nb_sample)
{
    srs_error_t err = srs_success;

    ps->nb_data_ = nb_sample;
    // Never copy data, for we already writen to writer.
    ps->data_ = NULL;

    // Update the mdat box from this offset.
    if ((err = wsio_->lseek(0, SEEK_CUR, &ps->offset_)) != srs_success) {
        return srs_error_wrap(err, "seek to offset in mdat");
    }

    // TODO: FIXME: Ensure all bytes are writen.
    if ((err = wsio_->write(sample, nb_sample, NULL)) != srs_success) {
        return srs_error_wrap(err, "write sample");
    }

    mdat_bytes_ += nb_sample;

    return err;
}

SrsMp4ObjectType SrsMp4Encoder::get_audio_object_type()
{
    switch (acodec_) {
    case SrsAudioCodecIdAAC:
        return SrsMp4ObjectTypeAac;
    case SrsAudioCodecIdMP3:
        return (srs_audio_sample_rate2number(sample_rate_) > 24000) ? SrsMp4ObjectTypeMp1a : SrsMp4ObjectTypeMp3; // 11172 - 3
    default:
        return SrsMp4ObjectTypeForbidden;
    }
}

ISrsMp4M2tsInitEncoder::ISrsMp4M2tsInitEncoder()
{
}

ISrsMp4M2tsInitEncoder::~ISrsMp4M2tsInitEncoder()
{
}

SrsMp4M2tsInitEncoder::SrsMp4M2tsInitEncoder()
{
    writer_ = NULL;
    crypt_byte_block_ = 0;
    skip_byte_block_ = 0;
    iv_size_ = 0;
    is_protected_ = false;
}

SrsMp4M2tsInitEncoder::~SrsMp4M2tsInitEncoder()
{
}

srs_error_t SrsMp4M2tsInitEncoder::initialize(ISrsWriter *w)
{
    writer_ = w;
    return srs_success;
}

void SrsMp4M2tsInitEncoder::config_encryption(uint8_t crypt_byte_block, uint8_t skip_byte_block, unsigned char *kid, unsigned char *iv, uint8_t iv_size)
{
    srs_assert(crypt_byte_block + skip_byte_block == 10);
    srs_assert(iv_size == 8 || iv_size == 16);
    crypt_byte_block_ = crypt_byte_block;
    skip_byte_block_ = skip_byte_block;
    memcpy(kid_, kid, 16);
    memcpy(iv_, iv, iv_size);
    iv_size_ = iv_size;
    is_protected_ = true;
}

srs_error_t SrsMp4M2tsInitEncoder::write(SrsFormat *format, bool video, int tid)
{
    srs_error_t err = srs_success;

    // Write ftyp box.
    if (true) {
        SrsUniquePtr<SrsMp4FileTypeBox> ftyp(new SrsMp4FileTypeBox());

        ftyp->major_brand_ = SrsMp4BoxBrandISO5;
        ftyp->minor_version_ = 512;
        ftyp->set_compatible_brands(SrsMp4BoxBrandISO6, SrsMp4BoxBrandMP41);

        if ((err = srs_mp4_write_box(writer_, ftyp.get())) != srs_success) {
            return srs_error_wrap(err, "write ftyp");
        }
    }

    // Write moov.
    if (true) {
        SrsUniquePtr<SrsMp4MovieBox> moov(new SrsMp4MovieBox());

        SrsMp4MovieHeaderBox *mvhd = new SrsMp4MovieHeaderBox();
        moov->set_mvhd(mvhd);

        mvhd->timescale_ = 1000; // Use tbn ms.
        mvhd->duration_in_tbn_ = 0;
        mvhd->next_track_ID_ = tid;

        if (video) {
            SrsMp4TrackBox *trak = new SrsMp4TrackBox();
            moov->add_trak(trak);

            SrsMp4TrackHeaderBox *tkhd = new SrsMp4TrackHeaderBox();
            trak->set_tkhd(tkhd);

            tkhd->track_ID_ = mvhd->next_track_ID_++;
            tkhd->duration_ = 0;
            tkhd->width_ = (format->vcodec_->width_ << 16);
            tkhd->height_ = (format->vcodec_->height_ << 16);

            SrsMp4MediaBox *mdia = new SrsMp4MediaBox();
            trak->set_mdia(mdia);

            SrsMp4MediaHeaderBox *mdhd = new SrsMp4MediaHeaderBox();
            mdia->set_mdhd(mdhd);

            mdhd->timescale_ = 1000;
            mdhd->duration_ = 0;
            mdhd->set_language0('u');
            mdhd->set_language1('n');
            mdhd->set_language2('d');

            SrsMp4HandlerReferenceBox *hdlr = new SrsMp4HandlerReferenceBox();
            mdia->set_hdlr(hdlr);

            hdlr->handler_type_ = SrsMp4HandlerTypeVIDE;
            hdlr->name_ = "VideoHandler";

            SrsMp4MediaInformationBox *minf = new SrsMp4MediaInformationBox();
            mdia->set_minf(minf);

            SrsMp4VideoMeidaHeaderBox *vmhd = new SrsMp4VideoMeidaHeaderBox();
            minf->set_vmhd(vmhd);

            SrsMp4DataInformationBox *dinf = new SrsMp4DataInformationBox();
            minf->set_dinf(dinf);

            SrsMp4DataReferenceBox *dref = new SrsMp4DataReferenceBox();
            dinf->set_dref(dref);

            SrsMp4DataEntryBox *url = new SrsMp4DataEntryUrlBox();
            dref->append(url);

            SrsMp4SampleTableBox *stbl = new SrsMp4SampleTableBox();
            minf->set_stbl(stbl);

            SrsMp4SampleDescriptionBox *stsd = new SrsMp4SampleDescriptionBox();
            stbl->set_stsd(stsd);

            if (format->vcodec_->id_ == SrsVideoCodecIdAVC) {
                SrsMp4VisualSampleEntry *avc1 = new SrsMp4VisualSampleEntry(SrsMp4BoxTypeAVC1);
                stsd->append(avc1);

                avc1->width_ = format->vcodec_->width_;
                avc1->height_ = format->vcodec_->height_;
                avc1->data_reference_index_ = 1;

                SrsMp4AvccBox *avcC = new SrsMp4AvccBox();
                avc1->set_avcC(avcC);

                avcC->avc_config_ = format->vcodec_->avc_extra_data_;

                if (is_protected_ && ((err = config_sample_description_encryption(avc1)) != srs_success)) {
                    return srs_error_wrap(err, "encrypt avc1 box");
                }
            } else {
                SrsMp4VisualSampleEntry *hev1 = new SrsMp4VisualSampleEntry(SrsMp4BoxTypeHEV1);
                stsd->append(hev1);

                hev1->width_ = format->vcodec_->width_;
                hev1->height_ = format->vcodec_->height_;
                hev1->data_reference_index_ = 1;

                SrsMp4HvcCBox *hvcC = new SrsMp4HvcCBox();
                hev1->set_hvcC(hvcC);

                hvcC->hevc_config_ = format->vcodec_->avc_extra_data_;

                if (is_protected_ && ((err = config_sample_description_encryption(hev1)) != srs_success)) {
                    return srs_error_wrap(err, "encrypt hev1 box");
                }
            }

            SrsMp4DecodingTime2SampleBox *stts = new SrsMp4DecodingTime2SampleBox();
            stbl->set_stts(stts);

            SrsMp4Sample2ChunkBox *stsc = new SrsMp4Sample2ChunkBox();
            stbl->set_stsc(stsc);

            SrsMp4SampleSizeBox *stsz = new SrsMp4SampleSizeBox();
            stbl->set_stsz(stsz);

            // TODO: FIXME: need to check using stco or co64?
            SrsMp4ChunkOffsetBox *stco = new SrsMp4ChunkOffsetBox();
            stbl->set_stco(stco);

            SrsMp4MovieExtendsBox *mvex = new SrsMp4MovieExtendsBox();
            moov->set_mvex(mvex);

            SrsMp4TrackExtendsBox *trex = new SrsMp4TrackExtendsBox();
            mvex->add_trex(trex);

            trex->track_ID_ = tid;
            trex->default_sample_description_index_ = 1;
        } else {
            SrsMp4TrackBox *trak = new SrsMp4TrackBox();
            moov->add_trak(trak);

            SrsMp4TrackHeaderBox *tkhd = new SrsMp4TrackHeaderBox();
            tkhd->volume_ = 0x0100;
            trak->set_tkhd(tkhd);

            tkhd->track_ID_ = mvhd->next_track_ID_++;
            tkhd->duration_ = 0;

            SrsMp4MediaBox *mdia = new SrsMp4MediaBox();
            trak->set_mdia(mdia);

            SrsMp4MediaHeaderBox *mdhd = new SrsMp4MediaHeaderBox();
            mdia->set_mdhd(mdhd);

            mdhd->timescale_ = 1000;
            mdhd->duration_ = 0;
            mdhd->set_language0('u');
            mdhd->set_language1('n');
            mdhd->set_language2('d');

            SrsMp4HandlerReferenceBox *hdlr = new SrsMp4HandlerReferenceBox();
            mdia->set_hdlr(hdlr);

            hdlr->handler_type_ = SrsMp4HandlerTypeSOUN;
            hdlr->name_ = "SoundHandler";

            SrsMp4MediaInformationBox *minf = new SrsMp4MediaInformationBox();
            mdia->set_minf(minf);

            SrsMp4SoundMeidaHeaderBox *smhd = new SrsMp4SoundMeidaHeaderBox();
            minf->set_smhd(smhd);

            SrsMp4DataInformationBox *dinf = new SrsMp4DataInformationBox();
            minf->set_dinf(dinf);

            SrsMp4DataReferenceBox *dref = new SrsMp4DataReferenceBox();
            dinf->set_dref(dref);

            SrsMp4DataEntryBox *url = new SrsMp4DataEntryUrlBox();
            dref->append(url);

            SrsMp4SampleTableBox *stbl = new SrsMp4SampleTableBox();
            minf->set_stbl(stbl);

            SrsMp4SampleDescriptionBox *stsd = new SrsMp4SampleDescriptionBox();
            stbl->set_stsd(stsd);

            SrsMp4AudioSampleEntry *mp4a = new SrsMp4AudioSampleEntry();
            mp4a->data_reference_index_ = 1;
            mp4a->samplerate_ = uint32_t(srs_flv_srates[format->acodec_->sound_rate_]) << 16;
            if (format->acodec_->sound_size_ == SrsAudioSampleBits16bit) {
                mp4a->samplesize_ = 16;
            } else {
                mp4a->samplesize_ = 8;
            }
            if (format->acodec_->sound_type_ == SrsAudioChannelsStereo) {
                mp4a->channelcount_ = 2;
            } else {
                mp4a->channelcount_ = 1;
            }
            stsd->append(mp4a);

            SrsMp4EsdsBox *esds = new SrsMp4EsdsBox();
            mp4a->set_esds(esds);

            if (is_protected_ && ((err = config_sample_description_encryption(mp4a)) != srs_success)) {
                return srs_error_wrap(err, "encrypt mp4a box");
            }

            SrsMp4ES_Descriptor *es = esds->es;
            es->ES_ID_ = 0x02;

            SrsMp4DecoderConfigDescriptor &desc = es->decConfigDescr_;
            desc.objectTypeIndication = SrsMp4ObjectTypeAac;
            desc.streamType = SrsMp4StreamTypeAudioStream;
            srs_freep(desc.decSpecificInfo_);

            SrsMp4DecoderSpecificInfo *asc = new SrsMp4DecoderSpecificInfo();
            desc.decSpecificInfo_ = asc;
            asc->asc_ = format->acodec_->aac_extra_data_;

            SrsMp4DecodingTime2SampleBox *stts = new SrsMp4DecodingTime2SampleBox();
            stbl->set_stts(stts);

            SrsMp4Sample2ChunkBox *stsc = new SrsMp4Sample2ChunkBox();
            stbl->set_stsc(stsc);

            SrsMp4SampleSizeBox *stsz = new SrsMp4SampleSizeBox();
            stbl->set_stsz(stsz);

            // TODO: FIXME: need to check using stco or co64?
            SrsMp4ChunkOffsetBox *stco = new SrsMp4ChunkOffsetBox();
            stbl->set_stco(stco);

            SrsMp4MovieExtendsBox *mvex = new SrsMp4MovieExtendsBox();
            moov->set_mvex(mvex);

            SrsMp4TrackExtendsBox *trex = new SrsMp4TrackExtendsBox();
            mvex->add_trex(trex);

            trex->track_ID_ = tid;
            trex->default_sample_description_index_ = 1;
        }

        if ((err = srs_mp4_write_box(writer_, moov.get())) != srs_success) {
            return srs_error_wrap(err, "write moov");
        }
    }

    return err;
}

srs_error_t SrsMp4M2tsInitEncoder::write(SrsFormat *format, int v_tid, int a_tid)
{
    srs_error_t err = srs_success;

    // Write ftyp box.
    if (true) {
        SrsUniquePtr<SrsMp4FileTypeBox> ftyp(new SrsMp4FileTypeBox());

        ftyp->major_brand_ = SrsMp4BoxBrandMP42; // SrsMp4BoxBrandISO5;
        ftyp->minor_version_ = 512;
        ftyp->set_compatible_brands(SrsMp4BoxBrandISO6, SrsMp4BoxBrandMP41);

        if ((err = srs_mp4_write_box(writer_, ftyp.get())) != srs_success) {
            return srs_error_wrap(err, "write ftyp");
        }
    }

    // Write moov.
    if (true) {
        SrsUniquePtr<SrsMp4MovieBox> moov(new SrsMp4MovieBox());

        SrsMp4MovieHeaderBox *mvhd = new SrsMp4MovieHeaderBox();
        moov->set_mvhd(mvhd);

        mvhd->timescale_ = 1000; // Use tbn ms.
        mvhd->duration_in_tbn_ = 0;
        mvhd->next_track_ID_ = 4294967295; // 2^32 - 1

        // write video track
        if (format->vcodec_) {
            SrsMp4TrackBox *trak = new SrsMp4TrackBox();
            moov->add_trak(trak);

            SrsMp4TrackHeaderBox *tkhd = new SrsMp4TrackHeaderBox();
            trak->set_tkhd(tkhd);

            tkhd->track_ID_ = v_tid;
            tkhd->duration_ = 0;
            tkhd->width_ = (format->vcodec_->width_ << 16);
            tkhd->height_ = (format->vcodec_->height_ << 16);

            SrsMp4MediaBox *mdia = new SrsMp4MediaBox();
            trak->set_mdia(mdia);

            SrsMp4MediaHeaderBox *mdhd = new SrsMp4MediaHeaderBox();
            mdia->set_mdhd(mdhd);

            mdhd->timescale_ = 1000;
            mdhd->duration_ = 0;
            mdhd->set_language0('u');
            mdhd->set_language1('n');
            mdhd->set_language2('d');

            SrsMp4HandlerReferenceBox *hdlr = new SrsMp4HandlerReferenceBox();
            mdia->set_hdlr(hdlr);

            hdlr->handler_type_ = SrsMp4HandlerTypeVIDE;
            hdlr->name_ = "VideoHandler";

            SrsMp4MediaInformationBox *minf = new SrsMp4MediaInformationBox();
            mdia->set_minf(minf);

            SrsMp4VideoMeidaHeaderBox *vmhd = new SrsMp4VideoMeidaHeaderBox();
            minf->set_vmhd(vmhd);

            SrsMp4DataInformationBox *dinf = new SrsMp4DataInformationBox();
            minf->set_dinf(dinf);

            SrsMp4DataReferenceBox *dref = new SrsMp4DataReferenceBox();
            dinf->set_dref(dref);

            SrsMp4DataEntryBox *url = new SrsMp4DataEntryUrlBox();
            dref->append(url);

            SrsMp4SampleTableBox *stbl = new SrsMp4SampleTableBox();
            minf->set_stbl(stbl);

            SrsMp4SampleDescriptionBox *stsd = new SrsMp4SampleDescriptionBox();
            stbl->set_stsd(stsd);

            if (format->vcodec_->id_ == SrsVideoCodecIdAVC) {
                SrsMp4VisualSampleEntry *avc1 = new SrsMp4VisualSampleEntry(SrsMp4BoxTypeAVC1);
                stsd->append(avc1);

                avc1->width_ = format->vcodec_->width_;
                avc1->height_ = format->vcodec_->height_;
                avc1->data_reference_index_ = 1;

                SrsMp4AvccBox *avcC = new SrsMp4AvccBox();
                avc1->set_avcC(avcC);

                avcC->avc_config_ = format->vcodec_->avc_extra_data_;

                if (is_protected_ && ((err = config_sample_description_encryption(avc1)) != srs_success)) {
                    return srs_error_wrap(err, "encrypt avc1 box");
                }
            } else {
                SrsMp4VisualSampleEntry *hev1 = new SrsMp4VisualSampleEntry(SrsMp4BoxTypeHEV1);
                stsd->append(hev1);

                hev1->width_ = format->vcodec_->width_;
                hev1->height_ = format->vcodec_->height_;
                hev1->data_reference_index_ = 1;

                SrsMp4HvcCBox *hvcC = new SrsMp4HvcCBox();
                hev1->set_hvcC(hvcC);

                hvcC->hevc_config_ = format->vcodec_->avc_extra_data_;

                if (is_protected_ && ((err = config_sample_description_encryption(hev1)) != srs_success)) {
                    return srs_error_wrap(err, "encrypt hev1 box");
                }
            }

            SrsMp4DecodingTime2SampleBox *stts = new SrsMp4DecodingTime2SampleBox();
            stbl->set_stts(stts);

            SrsMp4Sample2ChunkBox *stsc = new SrsMp4Sample2ChunkBox();
            stbl->set_stsc(stsc);

            SrsMp4SampleSizeBox *stsz = new SrsMp4SampleSizeBox();
            stbl->set_stsz(stsz);

            // TODO: FIXME: need to check using stco or co64?
            SrsMp4ChunkOffsetBox *stco = new SrsMp4ChunkOffsetBox();
            stbl->set_stco(stco);
        }

        // write audio track
        if (format->acodec_) {
            SrsMp4TrackBox *trak = new SrsMp4TrackBox();
            moov->add_trak(trak);

            SrsMp4TrackHeaderBox *tkhd = new SrsMp4TrackHeaderBox();
            tkhd->volume_ = 0x0100;
            trak->set_tkhd(tkhd);

            tkhd->track_ID_ = a_tid;
            tkhd->duration_ = 0;

            SrsMp4MediaBox *mdia = new SrsMp4MediaBox();
            trak->set_mdia(mdia);

            SrsMp4MediaHeaderBox *mdhd = new SrsMp4MediaHeaderBox();
            mdia->set_mdhd(mdhd);

            mdhd->timescale_ = 1000;
            mdhd->duration_ = 0;
            mdhd->set_language0('u');
            mdhd->set_language1('n');
            mdhd->set_language2('d');

            SrsMp4HandlerReferenceBox *hdlr = new SrsMp4HandlerReferenceBox();
            mdia->set_hdlr(hdlr);

            hdlr->handler_type_ = SrsMp4HandlerTypeSOUN;
            hdlr->name_ = "SoundHandler";

            SrsMp4MediaInformationBox *minf = new SrsMp4MediaInformationBox();
            mdia->set_minf(minf);

            SrsMp4SoundMeidaHeaderBox *smhd = new SrsMp4SoundMeidaHeaderBox();
            minf->set_smhd(smhd);

            SrsMp4DataInformationBox *dinf = new SrsMp4DataInformationBox();
            minf->set_dinf(dinf);

            SrsMp4DataReferenceBox *dref = new SrsMp4DataReferenceBox();
            dinf->set_dref(dref);

            SrsMp4DataEntryBox *url = new SrsMp4DataEntryUrlBox();
            dref->append(url);

            SrsMp4SampleTableBox *stbl = new SrsMp4SampleTableBox();
            minf->set_stbl(stbl);

            SrsMp4SampleDescriptionBox *stsd = new SrsMp4SampleDescriptionBox();
            stbl->set_stsd(stsd);

            SrsMp4AudioSampleEntry *mp4a = new SrsMp4AudioSampleEntry();
            mp4a->data_reference_index_ = 1;
            mp4a->samplerate_ = uint32_t(srs_flv_srates[format->acodec_->sound_rate_]) << 16;
            if (format->acodec_->sound_size_ == SrsAudioSampleBits16bit) {
                mp4a->samplesize_ = 16;
            } else {
                mp4a->samplesize_ = 8;
            }
            if (format->acodec_->sound_type_ == SrsAudioChannelsStereo) {
                mp4a->channelcount_ = 2;
            } else {
                mp4a->channelcount_ = 1;
            }
            stsd->append(mp4a);

            SrsMp4EsdsBox *esds = new SrsMp4EsdsBox();
            mp4a->set_esds(esds);
            if (is_protected_ && ((err = config_sample_description_encryption(mp4a)) != srs_success)) {
                return srs_error_wrap(err, "encrypt mp4a box.");
            }

            SrsMp4ES_Descriptor *es = esds->es;
            es->ES_ID_ = 0x02;

            SrsMp4DecoderConfigDescriptor &desc = es->decConfigDescr_;
            desc.objectTypeIndication = SrsMp4ObjectTypeAac;
            desc.streamType = SrsMp4StreamTypeAudioStream;
            srs_freep(desc.decSpecificInfo_);

            SrsMp4DecoderSpecificInfo *asc = new SrsMp4DecoderSpecificInfo();
            desc.decSpecificInfo_ = asc;
            asc->asc_ = format->acodec_->aac_extra_data_;

            SrsMp4DecodingTime2SampleBox *stts = new SrsMp4DecodingTime2SampleBox();
            stbl->set_stts(stts);

            SrsMp4Sample2ChunkBox *stsc = new SrsMp4Sample2ChunkBox();
            stbl->set_stsc(stsc);

            SrsMp4SampleSizeBox *stsz = new SrsMp4SampleSizeBox();
            stbl->set_stsz(stsz);

            // TODO: FIXME: need to check using stco or co64?
            SrsMp4ChunkOffsetBox *stco = new SrsMp4ChunkOffsetBox();
            stbl->set_stco(stco);
        }

        if (true) {
            SrsMp4MovieExtendsBox *mvex = new SrsMp4MovieExtendsBox();
            moov->set_mvex(mvex);

            // video trex
            if (format->vcodec_) {
                SrsMp4TrackExtendsBox *v_trex = new SrsMp4TrackExtendsBox();
                mvex->add_trex(v_trex);

                v_trex->track_ID_ = v_tid;
                v_trex->default_sample_description_index_ = 1;
            }

            // audio trex
            if (format->acodec_) {
                SrsMp4TrackExtendsBox *a_trex = new SrsMp4TrackExtendsBox();
                mvex->add_trex(a_trex);

                a_trex->track_ID_ = a_tid;
                a_trex->default_sample_description_index_ = 1;
            }
        }

        if ((err = srs_mp4_write_box(writer_, moov.get())) != srs_success) {
            return srs_error_wrap(err, "write moov");
        }
    }

    return err;
}

/**
 * box->type = 'encv' or 'enca'
 * |encv|
 * |    |sinf|
 * |    |    |frma|
 * |    |    |schm|
 * |    |    |schi|
 * |    |    |    |tenc|
 */
srs_error_t SrsMp4M2tsInitEncoder::config_sample_description_encryption(SrsMp4SampleEntry *box)
{
    srs_error_t err = srs_success;

    bool is_video_sample = false;
    SrsMp4BoxType original_type = box->type_;

    if (original_type == SrsMp4BoxTypeAVC1 || original_type == SrsMp4BoxTypeHEV1) {
        box->type_ = SrsMp4BoxTypeENCV;
        is_video_sample = true;
    } else if (original_type == SrsMp4BoxTypeMP4A) {
        box->type_ = SrsMp4BoxTypeENCA;
    } else {
        return srs_error_new(ERROR_MP4_BOX_ILLEGAL_TYPE, "unknown sample type 0x%x to encrypt", original_type);
    }

    SrsMp4ProtectionSchemeInfoBox *sinf = new SrsMp4ProtectionSchemeInfoBox();
    box->append(sinf);

    SrsMp4OriginalFormatBox *frma = new SrsMp4OriginalFormatBox(original_type);
    sinf->set_frma(frma);

    SrsMp4SchemeTypeBox *schm = new SrsMp4SchemeTypeBox();
    schm->scheme_type_ = SrsMp4CENSchemeCBCS;
    schm->scheme_version_ = 0x00010000;
    sinf->set_schm(schm);

    SrsMp4SchemeInfoBox *schi = new SrsMp4SchemeInfoBox();
    SrsMp4TrackEncryptionBox *tenc = new SrsMp4TrackEncryptionBox();
    tenc->version_ = 1;
    tenc->default_crypt_byte_block_ = is_video_sample ? crypt_byte_block_ : 0;
    tenc->default_skip_byte_block_ = is_video_sample ? skip_byte_block_ : 0;
    tenc->default_is_protected_ = 1;
    tenc->default_per_sample_IV_size_ = 0;
    tenc->default_constant_IV_size_ = iv_size_;
    memcpy(tenc->default_constant_IV_, iv_, iv_size_);
    memcpy(tenc->default_KID_, kid_, 16);

    schi->append(tenc);
    sinf->set_schi(schi);

    return err;
}

ISrsMp4M2tsSegmentEncoder::ISrsMp4M2tsSegmentEncoder()
{
}

ISrsMp4M2tsSegmentEncoder::~ISrsMp4M2tsSegmentEncoder()
{
}

SrsMp4M2tsSegmentEncoder::SrsMp4M2tsSegmentEncoder()
{
    writer_ = NULL;
    nb_audios_ = nb_videos_ = 0;
    samples_ = new SrsMp4SampleManager();
    sequence_number_ = 0;
    decode_basetime_ = 0;
    styp_bytes_ = 0;
    mdat_bytes_ = 0;
    track_id_ = 0;
}

SrsMp4M2tsSegmentEncoder::~SrsMp4M2tsSegmentEncoder()
{
    srs_freep(samples_);
}

srs_error_t SrsMp4M2tsSegmentEncoder::initialize(ISrsWriter *w, uint32_t sequence, srs_utime_t basetime, uint32_t tid)
{
    srs_error_t err = srs_success;

    writer_ = w;
    track_id_ = tid;
    sequence_number_ = sequence;
    decode_basetime_ = basetime;

    // Write styp box.
    if (true) {
        SrsUniquePtr<SrsMp4SegmentTypeBox> styp(new SrsMp4SegmentTypeBox());

        styp->major_brand_ = SrsMp4BoxBrandMSDH;
        styp->minor_version_ = 0;
        styp->set_compatible_brands(SrsMp4BoxBrandMSDH, SrsMp4BoxBrandMSIX);

        // Used for sidx to calcalute the referenced size.
        styp_bytes_ = styp->nb_bytes();

        if ((err = srs_mp4_write_box(writer_, styp.get())) != srs_success) {
            return srs_error_wrap(err, "write styp");
        }
    }

    return err;
}

srs_error_t SrsMp4M2tsSegmentEncoder::write_sample(SrsMp4HandlerType ht,
                                                   uint16_t ft, uint32_t dts, uint32_t pts, uint8_t *sample, uint32_t nb_sample)
{
    srs_error_t err = srs_success;

    SrsMp4Sample *ps = new SrsMp4Sample();

    if (ht == SrsMp4HandlerTypeVIDE) {
        ps->type_ = SrsFrameTypeVideo;
        ps->frame_type_ = (SrsVideoAvcFrameType)ft;
        ps->index_ = nb_videos_++;
    } else if (ht == SrsMp4HandlerTypeSOUN) {
        ps->type_ = SrsFrameTypeAudio;
        ps->index_ = nb_audios_++;
    } else {
        srs_freep(ps);
        return err;
    }

    ps->tbn_ = 1000;
    ps->dts_ = dts;
    ps->pts_ = pts;

    // We should copy the sample data, which is shared ptr from video/audio message.
    // Furthermore, we do free the data when freeing the sample.
    ps->data_ = new uint8_t[nb_sample];
    memcpy(ps->data_, sample, nb_sample);
    ps->nb_data_ = nb_sample;

    // Append to manager to build the moof.
    samples_->append(ps);

    mdat_bytes_ += nb_sample;

    return err;
}

srs_error_t SrsMp4M2tsSegmentEncoder::flush(uint64_t &dts)
{
    srs_error_t err = srs_success;

    if (!nb_audios_ && !nb_videos_) {
        return srs_error_new(ERROR_MP4_ILLEGAL_MOOF, "Missing audio and video track");
    }

    // Although the sidx is not required to start play DASH, but it's required for AV sync.
    SrsUniquePtr<SrsMp4SegmentIndexBox> sidx(new SrsMp4SegmentIndexBox());
    if (true) {
        sidx->version_ = 1;
        sidx->reference_id_ = 1;
        sidx->timescale_ = 1000;
        sidx->earliest_presentation_time_ = uint64_t(decode_basetime_ / sidx->timescale_);

        uint64_t duration = 0;
        if (samples_ && !samples_->samples_.empty()) {
            SrsMp4Sample *first = samples_->samples_[0];
            duration = srs_max(0, dts - first->dts_);
        }

        SrsMp4SegmentIndexEntry entry;
        memset(&entry, 0, sizeof(entry));
        entry.subsegment_duration_ = duration;
        entry.starts_with_SAP_ = 1;
        sidx->entries_.push_back(entry);
    }

    // Create a mdat box.
    // its payload will be writen by samples,
    // and we will update its header(size) when flush.
    SrsUniquePtr<SrsMp4MediaDataBox> mdat(new SrsMp4MediaDataBox());

    // Write moof.
    if (true) {
        SrsUniquePtr<SrsMp4MovieFragmentBox> moof(new SrsMp4MovieFragmentBox());

        SrsMp4MovieFragmentHeaderBox *mfhd = new SrsMp4MovieFragmentHeaderBox();
        moof->set_mfhd(mfhd);

        mfhd->sequence_number_ = sequence_number_;

        SrsMp4TrackFragmentBox *traf = new SrsMp4TrackFragmentBox();
        moof->add_traf(traf);

        SrsMp4TrackFragmentHeaderBox *tfhd = new SrsMp4TrackFragmentHeaderBox();
        traf->set_tfhd(tfhd);

        tfhd->track_id_ = track_id_;
        tfhd->flags_ = SrsMp4TfhdFlagsDefaultBaseIsMoof;

        SrsMp4TrackFragmentDecodeTimeBox *tfdt = new SrsMp4TrackFragmentDecodeTimeBox();
        traf->set_tfdt(tfdt);

        tfdt->version_ = 1;
        tfdt->base_media_decode_time_ = srsu2ms(decode_basetime_);

        SrsMp4TrackFragmentRunBox *trun = new SrsMp4TrackFragmentRunBox();
        traf->set_trun(trun);

        if ((err = samples_->write(traf, dts)) != srs_success) {
            return srs_error_wrap(err, "write samples");
        }

        // @remark Remember the data_offset of turn is size(moof)+header(mdat), not including styp or sidx.
        int moof_bytes = moof->nb_bytes();
        trun->data_offset_ = (int32_t)(moof_bytes + mdat->sz_header());
        mdat->nb_data_ = mdat_bytes_;

        // Update the size of sidx.
        SrsMp4SegmentIndexEntry *entry = &sidx->entries_[0];
        entry->referenced_size_ = moof_bytes + mdat->nb_bytes();
        if ((err = srs_mp4_write_box(writer_, sidx.get())) != srs_success) {
            return srs_error_wrap(err, "write sidx");
        }

        if ((err = srs_mp4_write_box(writer_, moof.get())) != srs_success) {
            return srs_error_wrap(err, "write moof");
        }
    }

    // Write mdat.
    if (true) {
        int nb_data = mdat->sz_header();
        SrsUniquePtr<uint8_t[]> data(new uint8_t[nb_data]);

        SrsUniquePtr<SrsBuffer> buffer(new SrsBuffer((char *)data.get(), nb_data));
        if ((err = mdat->encode(buffer.get())) != srs_success) {
            return srs_error_wrap(err, "encode mdat");
        }

        // TODO: FIXME: Ensure all bytes are writen.
        if ((err = writer_->write(data.get(), nb_data, NULL)) != srs_success) {
            return srs_error_wrap(err, "write mdat");
        }

        vector<SrsMp4Sample *>::iterator it;
        for (it = samples_->samples_.begin(); it != samples_->samples_.end(); ++it) {
            SrsMp4Sample *sample = *it;

            // TODO: FIXME: Ensure all bytes are writen.
            if ((err = writer_->write(sample->data_, sample->nb_data_, NULL)) != srs_success) {
                return srs_error_wrap(err, "write sample");
            }
        }
    }

    return err;
}

SrsFmp4SegmentEncoder::SrsFmp4SegmentEncoder()
{
    writer_ = NULL;
    sequence_number_ = 0;
    decode_basetime_ = 0;
    audio_track_id_ = 0;
    video_track_id_ = 0;
    nb_audios_ = 0;
    nb_videos_ = 0;
    styp_bytes_ = 0;
    mdat_audio_bytes_ = 0;
    mdat_video_bytes_ = 0;
    audio_samples_ = new SrsMp4SampleManager();
    video_samples_ = new SrsMp4SampleManager();

    memset(iv_, 0, 16);
    key_ = (unsigned char *)new AES_KEY();
    do_sample_encryption_ = false;
}

SrsFmp4SegmentEncoder::~SrsFmp4SegmentEncoder()
{
    srs_freep(audio_samples_);
    srs_freep(video_samples_);

    AES_KEY *k = (AES_KEY *)key_;
    srs_freep(k);
}

srs_error_t SrsFmp4SegmentEncoder::initialize(ISrsWriter *w, uint32_t sequence, srs_utime_t basetime, uint32_t v_tid, uint32_t a_tid)
{
    srs_error_t err = srs_success;

    writer_ = w;
    sequence_number_ = sequence;
    decode_basetime_ = basetime;
    video_track_id_ = v_tid;
    audio_track_id_ = a_tid;

    return err;
}

srs_error_t SrsFmp4SegmentEncoder::config_cipher(unsigned char *key, unsigned char *iv)
{
    srs_error_t err = srs_success;

    memcpy(this->iv_, iv, 16);

    AES_KEY *k = (AES_KEY *)this->key_;
    if (AES_set_encrypt_key(key, 16 * 8, k)) {
        return srs_error_new(ERROR_SYSTEM_FILE_WRITE, "set aes key failed");
    }
    do_sample_encryption_ = true;

    return err;
}

srs_error_t SrsFmp4SegmentEncoder::write_sample(SrsMp4HandlerType ht, uint16_t ft,
                                                uint32_t dts, uint32_t pts, uint8_t *sample, uint32_t nb_sample)
{
    srs_error_t err = srs_success;

    SrsMp4Sample *ps = new SrsMp4Sample();

    if (ht == SrsMp4HandlerTypeVIDE) {
        ps->type_ = SrsFrameTypeVideo;
        ps->frame_type_ = (SrsVideoAvcFrameType)ft;
        ps->index_ = nb_videos_++;
        video_samples_->append(ps);
        mdat_video_bytes_ += nb_sample;
    } else if (ht == SrsMp4HandlerTypeSOUN) {
        ps->type_ = SrsFrameTypeAudio;
        ps->index_ = nb_audios_++;
        audio_samples_->append(ps);
        mdat_audio_bytes_ += nb_sample;
    } else {
        srs_freep(ps);
        return err;
    }

    ps->tbn_ = 1000;
    ps->dts_ = dts;
    ps->pts_ = pts;

    // We should copy the sample data, which is shared ptr from video/audio message.
    // Furthermore, we do free the data when freeing the sample.
    ps->data_ = new uint8_t[nb_sample];
    memcpy(ps->data_, sample, nb_sample);
    ps->nb_data_ = nb_sample;

    return err;
}

srs_error_t SrsFmp4SegmentEncoder::flush(uint64_t dts)
{
    srs_error_t err = srs_success;
    SrsMp4TrackFragmentRunBox *video_trun = NULL;
    SrsMp4TrackFragmentRunBox *audio_trun = NULL;

    if (nb_videos_ == 0 && nb_audios_ == 0) {
        return srs_error_new(ERROR_MP4_ILLEGAL_MDAT, "empty samples");
    }
    // Create a mdat box.
    // its payload will be writen by samples,
    // and we will update its header(size) when flush.
    SrsUniquePtr<SrsMp4MediaDataBox> mdat(new SrsMp4MediaDataBox());

    SrsUniquePtr<SrsMp4MovieFragmentBox> moof(new SrsMp4MovieFragmentBox());

    SrsMp4MovieFragmentHeaderBox *mfhd = new SrsMp4MovieFragmentHeaderBox();
    moof->set_mfhd(mfhd);
    mfhd->sequence_number_ = sequence_number_;

    // write video traf
    if (mdat_video_bytes_ > 0) {
        // video traf
        SrsMp4TrackFragmentBox *traf = new SrsMp4TrackFragmentBox();
        moof->add_traf(traf);

        SrsMp4TrackFragmentHeaderBox *tfhd = new SrsMp4TrackFragmentHeaderBox();
        traf->set_tfhd(tfhd);

        tfhd->track_id_ = video_track_id_;
        tfhd->flags_ = SrsMp4TfhdFlagsDefaultBaseIsMoof;

        SrsMp4TrackFragmentDecodeTimeBox *tfdt = new SrsMp4TrackFragmentDecodeTimeBox();
        traf->set_tfdt(tfdt);

        tfdt->version_ = 1;
        tfdt->base_media_decode_time_ = srsu2ms(decode_basetime_);

        SrsMp4TrackFragmentRunBox *trun = new SrsMp4TrackFragmentRunBox();
        traf->set_trun(trun);
        video_trun = trun;

        if ((err = video_samples_->write(traf, dts)) != srs_success) {
            return srs_error_wrap(err, "write samples");
        }

        // TODO: write senc, and optional saiz & saio
        if (do_sample_encryption_) {
            SrsMp4SampleEncryptionBox *senc = new SrsMp4SampleEncryptionBox(0);
            // video_samples_;
            vector<SrsMp4Sample *>::iterator it;
            // write video sample data
            for (it = video_samples_->samples_.begin(); it != video_samples_->samples_.end(); ++it) {
                // SrsMp4Sample* sample = *it;
                // TODO: parse hevc|avc, nalu slice header, and calculate
                // sample->data;
                // sample->nb_data;
            }

            traf->append(senc);
        }
    }

    // write audio traf
    if (mdat_audio_bytes_ > 0) {
        // audio traf
        SrsMp4TrackFragmentBox *traf = new SrsMp4TrackFragmentBox();
        moof->add_traf(traf);

        SrsMp4TrackFragmentHeaderBox *tfhd = new SrsMp4TrackFragmentHeaderBox();
        traf->set_tfhd(tfhd);

        tfhd->track_id_ = audio_track_id_;
        tfhd->flags_ = SrsMp4TfhdFlagsDefaultBaseIsMoof;

        SrsMp4TrackFragmentDecodeTimeBox *tfdt = new SrsMp4TrackFragmentDecodeTimeBox();
        traf->set_tfdt(tfdt);

        tfdt->version_ = 1;
        tfdt->base_media_decode_time_ = srsu2ms(decode_basetime_);

        SrsMp4TrackFragmentRunBox *trun = new SrsMp4TrackFragmentRunBox();
        traf->set_trun(trun);
        audio_trun = trun;

        if ((err = audio_samples_->write(traf, dts)) != srs_success) {
            return srs_error_wrap(err, "write samples");
        }

        // TODO: write senc, and optional saiz & saio
        if (do_sample_encryption_) {
            SrsMp4SampleEncryptionBox *senc = new SrsMp4SampleEncryptionBox(0);
            // this->iv_;
            traf->append(senc);
        }
    }

    // @remark Remember the data_offset of turn is size(moof)+header(mdat)
    int moof_bytes = moof->nb_bytes();
    // rewrite video data_offset
    if (video_trun != NULL) {
        video_trun->data_offset_ = (int32_t)(moof_bytes + mdat->sz_header() + 0);
    }

    if (audio_trun != NULL) {
        audio_trun->data_offset_ = (int32_t)(moof_bytes + mdat->sz_header() + mdat_video_bytes_);
    }

    // srs_trace("seq: %d, moof_bytes=%d, mdat->sz_header=%d", sequence_number_, moof->nb_bytes(), mdat->sz_header());
    // srs_trace("mdat_video_bytes_ = %d, mdat_audio_bytes_ = %d", mdat_video_bytes_, mdat_audio_bytes_);

    if ((err = srs_mp4_write_box(writer_, moof.get())) != srs_success) {
        return srs_error_wrap(err, "write moof");
    }

    mdat->nb_data_ = mdat_video_bytes_ + mdat_audio_bytes_;
    // Write mdat.
    if (true) {
        int nb_data = mdat->sz_header();
        SrsUniquePtr<uint8_t[]> data(new uint8_t[nb_data]);

        SrsUniquePtr<SrsBuffer> buffer(new SrsBuffer((char *)data.get(), nb_data));
        if ((err = mdat->encode(buffer.get())) != srs_success) {
            return srs_error_wrap(err, "encode mdat");
        }

        // TODO: FIXME: Ensure all bytes are writen.
        if ((err = writer_->write(data.get(), nb_data, NULL)) != srs_success) {
            return srs_error_wrap(err, "write mdat");
        }

        vector<SrsMp4Sample *>::iterator it;
        // write video sample data
        for (it = video_samples_->samples_.begin(); it != video_samples_->samples_.end(); ++it) {
            SrsMp4Sample *sample = *it;

            // TODO: FIXME: Ensure all bytes are writen.
            // TODO: do cbcs encryption here. sample are nalu_length + nalu data.
            if ((err = writer_->write(sample->data_, sample->nb_data_, NULL)) != srs_success) {
                return srs_error_wrap(err, "write sample");
            }
        }

        // write audio sample data
        for (it = audio_samples_->samples_.begin(); it != audio_samples_->samples_.end(); ++it) {
            SrsMp4Sample *sample = *it;

            // TODO: FIXME: Ensure all bytes are writen.
            // TODO: do cbcs encryption here
            if ((err = writer_->write(sample->data_, sample->nb_data_, NULL)) != srs_success) {
                return srs_error_wrap(err, "write sample");
            }
        }
    }

    return err;
}
