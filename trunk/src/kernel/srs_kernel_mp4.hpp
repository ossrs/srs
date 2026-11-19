//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_KERNEL_MP4_HPP
#define SRS_KERNEL_MP4_HPP

#include <srs_core.hpp>

#include <srs_kernel_buffer.hpp>
#include <srs_kernel_codec.hpp>
#include <srs_kernel_utility.hpp>

#include <map>
#include <sstream>
#include <string>
#include <vector>

typedef struct evp_cipher_ctx_st EVP_CIPHER_CTX;

class ISrsWriter;
class ISrsWriteSeeker;
class ISrsReadSeeker;
class SrsMp4TrackBox;
class SrsMp4MediaBox;
class SrsSimpleStream;
class SrsMp4MovieHeaderBox;
class SrsMp4TrackHeaderBox;
class SrsMp4SampleTableBox;
class SrsMp4MediaInformationBox;
class SrsMp4SampleDescriptionBox;
class SrsMp4AvccBox;
class SrsMp4DecoderSpecificInfo;
class SrsMp4VisualSampleEntry;
class SrsMp4HvcCBox;
class SrsMp4AudioSampleEntry;
class SrsMp4EsdsBox;
class SrsMp4ChunkOffsetBox;
class SrsMp4ChunkLargeOffsetBox;
class SrsMp4SampleSizeBox;
class SrsMp4Sample2ChunkBox;
class SrsMp4DecodingTime2SampleBox;
class SrsMp4CompositionTime2SampleBox;
class SrsMp4SyncSampleBox;
class SrsMp4MediaHeaderBox;
class SrsMp4HandlerReferenceBox;
class SrsMp4VideoMeidaHeaderBox;
class SrsMp4DataInformationBox;
class SrsMp4DataReferenceBox;
class SrsMp4SoundMeidaHeaderBox;
class SrsMp4MovieExtendsBox;
class SrsMp4TrackExtendsBox;
class SrsMp4MovieFragmentHeaderBox;
class SrsMp4TrackFragmentBox;
class SrsMp4TrackFragmentHeaderBox;
class SrsMp4TrackFragmentDecodeTimeBox;
class SrsMp4TrackFragmentRunBox;
class SrsMp4EditBox;
class SrsMp4EditListBox;

// 4.2 Object Structure
// ISO_IEC_14496-12-base-format-2012.pdf, page 16
enum SrsMp4BoxType {
    SrsMp4BoxTypeForbidden = 0x00,

    SrsMp4BoxTypeUUID = 0x75756964, // 'uuid'
    SrsMp4BoxTypeFTYP = 0x66747970, // 'ftyp'
    SrsMp4BoxTypeMDAT = 0x6d646174, // 'mdat'
    SrsMp4BoxTypeFREE = 0x66726565, // 'free'
    SrsMp4BoxTypeSKIP = 0x736b6970, // 'skip'
    SrsMp4BoxTypeMOOV = 0x6d6f6f76, // 'moov'
    SrsMp4BoxTypeMVHD = 0x6d766864, // 'mvhd'
    SrsMp4BoxTypeTRAK = 0x7472616b, // 'trak'
    SrsMp4BoxTypeTKHD = 0x746b6864, // 'tkhd'
    SrsMp4BoxTypeEDTS = 0x65647473, // 'edts'
    SrsMp4BoxTypeELST = 0x656c7374, // 'elst'
    SrsMp4BoxTypeMDIA = 0x6d646961, // 'mdia'
    SrsMp4BoxTypeMDHD = 0x6d646864, // 'mdhd'
    SrsMp4BoxTypeHDLR = 0x68646c72, // 'hdlr'
    SrsMp4BoxTypeMINF = 0x6d696e66, // 'minf'
    SrsMp4BoxTypeVMHD = 0x766d6864, // 'vmhd'
    SrsMp4BoxTypeSMHD = 0x736d6864, // 'smhd'
    SrsMp4BoxTypeDINF = 0x64696e66, // 'dinf'
    SrsMp4BoxTypeURL = 0x75726c20,  // 'url '
    SrsMp4BoxTypeURN = 0x75726e20,  // 'urn '
    SrsMp4BoxTypeDREF = 0x64726566, // 'dref'
    SrsMp4BoxTypeSTBL = 0x7374626c, // 'stbl'
    SrsMp4BoxTypeSTSD = 0x73747364, // 'stsd'
    SrsMp4BoxTypeSTTS = 0x73747473, // 'stts'
    SrsMp4BoxTypeCTTS = 0x63747473, // 'ctts'
    SrsMp4BoxTypeSTSS = 0x73747373, // 'stss'
    SrsMp4BoxTypeSTSC = 0x73747363, // 'stsc'
    SrsMp4BoxTypeSTCO = 0x7374636f, // 'stco'
    SrsMp4BoxTypeCO64 = 0x636f3634, // 'co64'
    SrsMp4BoxTypeSTSZ = 0x7374737a, // 'stsz'
    SrsMp4BoxTypeSTZ2 = 0x73747a32, // 'stz2'
    SrsMp4BoxTypeAVC1 = 0x61766331, // 'avc1'
    SrsMp4BoxTypeAVCC = 0x61766343, // 'avcC'
    SrsMp4BoxTypeMP4A = 0x6d703461, // 'mp4a'
    SrsMp4BoxTypeESDS = 0x65736473, // 'esds'
    SrsMp4BoxTypeUDTA = 0x75647461, // 'udta'
    SrsMp4BoxTypeMVEX = 0x6d766578, // 'mvex'
    SrsMp4BoxTypeTREX = 0x74726578, // 'trex'
    SrsMp4BoxTypePASP = 0x70617370, // 'pasp'
    SrsMp4BoxTypeSTYP = 0x73747970, // 'styp'
    SrsMp4BoxTypeMOOF = 0x6d6f6f66, // 'moof'
    SrsMp4BoxTypeMFHD = 0x6d666864, // 'mfhd'
    SrsMp4BoxTypeTRAF = 0x74726166, // 'traf'
    SrsMp4BoxTypeTFHD = 0x74666864, // 'tfhd'
    SrsMp4BoxTypeTFDT = 0x74666474, // 'tfdt'
    SrsMp4BoxTypeTRUN = 0x7472756e, // 'trun'
    SrsMp4BoxTypeSIDX = 0x73696478, // 'sidx'
    SrsMp4BoxTypeHEV1 = 0x68657631, // 'hev1'
    SrsMp4BoxTypeHVCC = 0x68766343, // 'hvcC'
    SrsMp4BoxTypeSENC = 0x73656e63, // 'senc'
    SrsMp4BoxTypeSAIZ = 0x7361697a, // 'saiz'
    SrsMp4BoxTypeSAIO = 0x7361696f, // 'saio'
    SrsMp4BoxTypeENCV = 0x656e6376, // 'encv'
    SrsMp4BoxTypeENCA = 0x656e6361, // 'enca'
    SrsMp4BoxTypeSINF = 0x73696e66, // 'sinf'
    SrsMp4BoxTypeSCHI = 0x73636869, // 'schi'
    SrsMp4BoxTypeTENC = 0x74656e63, // 'tenc'
    SrsMp4BoxTypeFRMA = 0x66726d61, // 'frma'
    SrsMp4BoxTypeSCHM = 0x7363686d, // 'schm'
};

// @see ISO-IEC-23001-7.pdf, 4.2
enum SrsMp4CENSchemeType {
    SrsMp4CENSchemeCENC = 0x63656e63, // 'cenc'
    SrsMp4CENSchemeCBC1 = 0x63626331, // 'cbc1'
    SrsMp4CENSchemeCENS = 0x63656e73, // 'cens'
    SrsMp4CENSchemeCBCS = 0x63626373, // 'cbcs'
    SrsMp4CENSchemeSVE1 = 0x73766531, // 'sve1'
};

// 8.4.3.3 Semantics
// ISO_IEC_14496-12-base-format-2012.pdf, page 37
enum SrsMp4HandlerType {
    SrsMp4HandlerTypeForbidden = 0x00,

    SrsMp4HandlerTypeVIDE = 0x76696465, // 'vide'
    SrsMp4HandlerTypeSOUN = 0x736f756e, // 'soun'
};

// File format brands
// ISO_IEC_14496-12-base-format-2012.pdf, page 166
enum SrsMp4BoxBrand {
    SrsMp4BoxBrandForbidden = 0x00,
    SrsMp4BoxBrandISOM = 0x69736f6d, // 'isom'
    SrsMp4BoxBrandISO2 = 0x69736f32, // 'iso2'
    SrsMp4BoxBrandAVC1 = 0x61766331, // 'avc1'
    SrsMp4BoxBrandMP41 = 0x6d703431, // 'mp41'
    SrsMp4BoxBrandISO5 = 0x69736f35, // 'iso5'
    SrsMp4BoxBrandISO6 = 0x69736f36, // 'iso6'
    SrsMp4BoxBrandMP42 = 0x6d703432, // 'mp42'
    SrsMp4BoxBrandDASH = 0x64617368, // 'dash'
    SrsMp4BoxBrandMSDH = 0x6d736468, // 'msdh'
    SrsMp4BoxBrandMSIX = 0x6d736978, // 'msix'
    SrsMp4BoxBrandHEV1 = 0x68657631, // 'hev1'
};

class SrsMp4DumpContext
{
public:
    int level_;
    bool summary_;

    SrsMp4DumpContext();
    virtual ~SrsMp4DumpContext();

    SrsMp4DumpContext indent();
};

// 4.2 Object Structure
// ISO_IEC_14496-12-base-format-2012.pdf, page 16
class SrsMp4Box : public ISrsCodec
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // The size includes the header and all contained boxes. A size of 1 means the actual size is in
    // largesize; a size of 0 means the box extends to the end of the file, normally only for mdat.
    uint32_t smallsize_;
    uint64_t largesize_;

public:
    // User extensions set the type to 'uuid' and put their extended type in usertype.
    SrsMp4BoxType type_;
    std::vector<char> usertype_;

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    std::vector<SrsMp4Box *> boxes_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    int start_pos_;

public:
    SrsMp4Box();
    virtual ~SrsMp4Box();

public:
    virtual uint64_t sz();
    virtual int sz_header();
    virtual uint64_t update_size();
    virtual int left_space(SrsBuffer *buf);
    virtual bool is_ftyp();
    virtual bool is_moov();
    virtual bool is_mdat();
    virtual SrsMp4Box *get(SrsMp4BoxType bt);
    virtual int remove(SrsMp4BoxType bt);
    virtual void append(SrsMp4Box *box);
    virtual std::stringstream &dumps(std::stringstream &ss, SrsMp4DumpContext dc);
    // The caller frees the box in ppbox.
    static srs_error_t discovery(SrsBuffer *buf, SrsMp4Box **ppbox);

public:
    virtual uint64_t nb_bytes();
    virtual srs_error_t encode(SrsBuffer *buf);
    virtual srs_error_t decode(SrsBuffer *buf);

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual srs_error_t encode_boxes(SrsBuffer *buf);
    virtual srs_error_t decode_boxes(SrsBuffer *buf);
// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual int
    nb_header();
    // No buffer check is needed: the caller already checked the buffer holds the whole box.
    virtual srs_error_t encode_header(SrsBuffer *buf);
    // A buffer check is needed only when the box size depends on more than the version.
    virtual srs_error_t decode_header(SrsBuffer *buf);
    virtual bool boxes_in_header();

public:
    virtual std::stringstream &dumps_detail(std::stringstream &ss, SrsMp4DumpContext dc);
};

// 4.2 Object Structure
// ISO_IEC_14496-12-base-format-2012.pdf, page 17
class SrsMp4FullBox : public SrsMp4Box
{
public:
    uint8_t version_;
    uint32_t flags_;

public:
    SrsMp4FullBox();
    virtual ~SrsMp4FullBox();

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual int nb_header();
    virtual srs_error_t encode_header(SrsBuffer *buf);
    virtual srs_error_t decode_header(SrsBuffer *buf);

public:
    virtual std::stringstream &dumps_detail(std::stringstream &ss, SrsMp4DumpContext dc);
};

// 4.3 File Type Box (ftyp)
// ISO_IEC_14496-12-base-format-2012.pdf, page 17
class SrsMp4FileTypeBox : public SrsMp4Box
{
public:
    SrsMp4BoxBrand major_brand_;
    uint32_t minor_version_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    std::vector<SrsMp4BoxBrand>
        compatible_brands_;

public:
    SrsMp4FileTypeBox();
    virtual ~SrsMp4FileTypeBox();

public:
    virtual void set_compatible_brands(SrsMp4BoxBrand b0, SrsMp4BoxBrand b1);
    virtual void set_compatible_brands(SrsMp4BoxBrand b0, SrsMp4BoxBrand b1, SrsMp4BoxBrand b2);
    virtual void set_compatible_brands(SrsMp4BoxBrand b0, SrsMp4BoxBrand b1, SrsMp4BoxBrand b2, SrsMp4BoxBrand b3);

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual int nb_header();
    virtual srs_error_t encode_header(SrsBuffer *buf);
    virtual srs_error_t decode_header(SrsBuffer *buf);

public:
    virtual std::stringstream &dumps_detail(std::stringstream &ss, SrsMp4DumpContext dc);
};

// 8.16.2 Segment Type Box (styp)
// ISO_IEC_14496-12-base-format-2012.pdf, page 105
// In a segment file, the styp box must be the first box if present.
class SrsMp4SegmentTypeBox : public SrsMp4FileTypeBox
{
public:
    SrsMp4SegmentTypeBox();
    virtual ~SrsMp4SegmentTypeBox();
};

// 8.8.4 Movie Fragment Box (moof)
// ISO_IEC_14496-12-base-format-2012.pdf, page 66
class SrsMp4MovieFragmentBox : public SrsMp4Box
{
public:
    SrsMp4MovieFragmentBox();
    virtual ~SrsMp4MovieFragmentBox();

public:
    virtual SrsMp4MovieFragmentHeaderBox *mfhd();
    virtual void set_mfhd(SrsMp4MovieFragmentHeaderBox *v);
    virtual void add_traf(SrsMp4TrackFragmentBox *v);
};

// 8.8.5 Movie Fragment Header Box (mfhd)
// ISO_IEC_14496-12-base-format-2012.pdf, page 67
class SrsMp4MovieFragmentHeaderBox : public SrsMp4FullBox
{
public:
    // Usually starts at 1, and must increase with each fragment in the file, so readers can check the order.
    uint32_t sequence_number_;

public:
    SrsMp4MovieFragmentHeaderBox();
    virtual ~SrsMp4MovieFragmentHeaderBox();

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual int nb_header();
    virtual srs_error_t encode_header(SrsBuffer *buf);
    virtual srs_error_t decode_header(SrsBuffer *buf);

public:
    virtual std::stringstream &dumps_detail(std::stringstream &ss, SrsMp4DumpContext dc);
};

// 8.8.6 Track Fragment Box (traf)
// ISO_IEC_14496-12-base-format-2012.pdf, page 67
class SrsMp4TrackFragmentBox : public SrsMp4Box
{
public:
    SrsMp4TrackFragmentBox();
    virtual ~SrsMp4TrackFragmentBox();

public:
    virtual SrsMp4TrackFragmentHeaderBox *tfhd();
    virtual void set_tfhd(SrsMp4TrackFragmentHeaderBox *v);
    virtual SrsMp4TrackFragmentDecodeTimeBox *tfdt();
    virtual void set_tfdt(SrsMp4TrackFragmentDecodeTimeBox *tfdt);
    virtual SrsMp4TrackFragmentRunBox *trun();
    virtual void set_trun(SrsMp4TrackFragmentRunBox *v);
};

// The tf_flags of tfhd.
// ISO_IEC_14496-12-base-format-2012.pdf, page 68
enum SrsMp4TfhdFlags {
    // Without base-data-offset, the first track fragment's data starts at the first byte of the moof, and
    // each later one starts at the end of the data of the one before, which must be in the same file.
    SrsMp4TfhdFlagsBaseDataOffset = 0x000001,
    // Overrides the default in the trex box for this fragment.
    SrsMp4TfhdFlagsSampleDescriptionIndex = 0x000002,
    SrsMp4TfhdFlagsDefaultSampleDuration = 0x000008,
    SrsMp4TfhdFlagsDefautlSampleSize = 0x000010,
    SrsMp4TfhdFlagsDefaultSampleFlags = 0x000020,
    // No samples for the default duration. A file must not have both edit lists in the moov and
    // empty-duration fragments.
    SrsMp4TfhdFlagsDurationIsEmpty = 0x010000,
    // Without base-data-offset, the data offset base is the first byte of the moof. Required by the 'iso5'
    // brand, and not allowed in brands earlier than iso5.
    SrsMp4TfhdFlagsDefaultBaseIsMoof = 0x020000,
};

// 8.8.7 Track Fragment Header Box (tfhd)
// ISO_IEC_14496-12-base-format-2012.pdf, page 68
class SrsMp4TrackFragmentHeaderBox : public SrsMp4FullBox
{
public:
    uint32_t track_id_;

public:
    uint64_t base_data_offset_;
    uint32_t sample_description_index_;
    uint32_t default_sample_duration_;
    uint32_t default_sample_size_;
    uint32_t default_sample_flags_;

public:
    SrsMp4TrackFragmentHeaderBox();
    virtual ~SrsMp4TrackFragmentHeaderBox();

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual int nb_header();
    virtual srs_error_t encode_header(SrsBuffer *buf);
    virtual srs_error_t decode_header(SrsBuffer *buf);

public:
    virtual std::stringstream &dumps_detail(std::stringstream &ss, SrsMp4DumpContext dc);
};

// 8.8.12 Track fragment decode time (tfdt)
// ISO_IEC_14496-12-base-format-2012.pdf, page 72
class SrsMp4TrackFragmentDecodeTimeBox : public SrsMp4FullBox
{
public:
    // The decode time of the first sample in the fragment, in ms.
    uint64_t base_media_decode_time_;

public:
    SrsMp4TrackFragmentDecodeTimeBox();
    virtual ~SrsMp4TrackFragmentDecodeTimeBox();

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual int nb_header();
    virtual srs_error_t encode_header(SrsBuffer *buf);
    virtual srs_error_t decode_header(SrsBuffer *buf);

public:
    virtual std::stringstream &dumps_detail(std::stringstream &ss, SrsMp4DumpContext dc);
};

// The tr_flags for trun
// ISO_IEC_14496-12-base-format-2012.pdf, page 69
enum SrsMp4TrunFlags {
    SrsMp4TrunFlagsDataOffset = 0x000001,
    // Overrides the default flags for the first sample only, such as a key frame followed by difference
    // frames. If set, the per-sample flags must not be present.
    SrsMp4TrunFlagsFirstSample = 0x000004,
    // Each sample has its own duration, size or flags; otherwise the default is used.
    SrsMp4TrunFlagsSampleDuration = 0x000100,
    SrsMp4TrunFlagsSampleSize = 0x000200,
    SrsMp4TrunFlagsSampleFlag = 0x000400,
    // Each sample has a composition time offset, as for I/P/B video in MPEG.
    SrsMp4TrunFlagsSampleCtsOffset = 0x000800,
};

// ISO_IEC_14496-12-base-format-2012.pdf, page 69
class SrsMp4TrunEntry : public ISrsCodec
{
public:
    SrsMp4FullBox *owner_;

    uint32_t sample_duration_;
    uint32_t sample_size_;
    uint32_t sample_flags_;
    // Unsigned 32 bits in version 0, signed 32 bits otherwise.
    int64_t sample_composition_time_offset_;

    SrsMp4TrunEntry(SrsMp4FullBox *o);
    virtual ~SrsMp4TrunEntry();

    virtual uint64_t nb_bytes();
    virtual srs_error_t encode(SrsBuffer *buf);
    virtual srs_error_t decode(SrsBuffer *buf);

    virtual std::stringstream &dumps(std::stringstream &ss, SrsMp4DumpContext dc);
};

// 8.8.8 Track Fragment Run Box (trun)
// ISO_IEC_14496-12-base-format-2012.pdf, page 69
class SrsMp4TrackFragmentRunBox : public SrsMp4FullBox
{
public:
    // The sample_count field is the size of entries.
    // uint32_t sample_count;
public:
    // Added to the data offset base set up by the tfhd.
    int32_t data_offset_;
    uint32_t first_sample_flags_;

public:
    std::vector<SrsMp4TrunEntry *> entries_;

public:
    SrsMp4TrackFragmentRunBox();
    virtual ~SrsMp4TrackFragmentRunBox();

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual int nb_header();
    virtual srs_error_t encode_header(SrsBuffer *buf);
    virtual srs_error_t decode_header(SrsBuffer *buf);

public:
    virtual std::stringstream &dumps_detail(std::stringstream &ss, SrsMp4DumpContext dc);
};

// 8.1.1 Media Data Box (mdat)
// ISO_IEC_14496-12-base-format-2012.pdf, page 29
// Only the header is coded; the caller reads, writes or skips the nb_data bytes of media data itself.
class SrsMp4MediaDataBox : public SrsMp4Box
{
public:
    uint64_t nb_data_;

public:
    SrsMp4MediaDataBox();
    virtual ~SrsMp4MediaDataBox();

public:
    // Includes the media data, for the smallsize or largesize field.
    virtual uint64_t nb_bytes();
    // The buffer holds only the sz_header() bytes.
    virtual srs_error_t encode(SrsBuffer *buf);
    virtual srs_error_t decode(SrsBuffer *buf);

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual srs_error_t encode_boxes(SrsBuffer *buf);
    virtual srs_error_t decode_boxes(SrsBuffer *buf);

public:
    virtual std::stringstream &dumps_detail(std::stringstream &ss, SrsMp4DumpContext dc);
};

// 8.1.2 Free Space Box (free or skip)
// ISO_IEC_14496-12-base-format-2012.pdf, page 29
class SrsMp4FreeSpaceBox : public SrsMp4Box
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    std::vector<char> data_;

public:
    SrsMp4FreeSpaceBox(SrsMp4BoxType v);
    virtual ~SrsMp4FreeSpaceBox();

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual int nb_header();
    virtual srs_error_t encode_header(SrsBuffer *buf);
    virtual srs_error_t decode_header(SrsBuffer *buf);

public:
    virtual std::stringstream &dumps_detail(std::stringstream &ss, SrsMp4DumpContext dc);
};

// 8.2.1 Movie Box (moov)
// ISO_IEC_14496-12-base-format-2012.pdf, page 30
class SrsMp4MovieBox : public SrsMp4Box
{
public:
    SrsMp4MovieBox();
    virtual ~SrsMp4MovieBox();

public:
    virtual SrsMp4MovieHeaderBox *mvhd();
    virtual void set_mvhd(SrsMp4MovieHeaderBox *v);
    virtual SrsMp4MovieExtendsBox *mvex();
    virtual void set_mvex(SrsMp4MovieExtendsBox *v);
    virtual SrsMp4TrackBox *video();
    virtual SrsMp4TrackBox *audio();
    virtual void add_trak(SrsMp4TrackBox *v);
    virtual int nb_vide_tracks();
    virtual int nb_soun_tracks();

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual int nb_header();
    virtual srs_error_t encode_header(SrsBuffer *buf);
    virtual srs_error_t decode_header(SrsBuffer *buf);
};

// 8.2.2 Movie Header Box (mvhd)
// ISO_IEC_14496-12-base-format-2012.pdf, page 31
class SrsMp4MovieHeaderBox : public SrsMp4FullBox
{
public:
    // In seconds since midnight, Jan. 1, 1904, in UTC.
    uint64_t creation_time_;
    uint64_t modification_time_;

public:
    // The number of time units in one second.
    uint32_t timescale_;
    // The duration of the longest track, in timescale units; all 1s if unknown.
    uint64_t duration_in_tbn_;

public:
    // A fixed point 16.16 number; 1.0 (0x00010000) is normal forward playback.
    uint32_t rate_;
    // A fixed point 8.8 number; 1.0 (0x0100) is full volume.
    uint16_t volume_;
    uint16_t reserved0_;
    uint64_t reserved1_;
    // The video transformation matrix; (u,v,w) are restricted to (0,0,1), hex values (0,0,0x40000000).
    int32_t matrix_[9];
    uint32_t pre_defined_[6];
    // Must be larger than the largest track ID in use; 0 is not a valid track ID.
    uint32_t next_track_ID_;

public:
    SrsMp4MovieHeaderBox();
    virtual ~SrsMp4MovieHeaderBox();

public:
    virtual uint64_t duration();

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual int nb_header();
    virtual srs_error_t encode_header(SrsBuffer *buf);
    virtual srs_error_t decode_header(SrsBuffer *buf);

public:
    virtual std::stringstream &dumps_detail(std::stringstream &ss, SrsMp4DumpContext dc);
};

enum SrsMp4TrackType {
    SrsMp4TrackTypeForbidden = 0x00,
    SrsMp4TrackTypeAudio = 0x01,
    SrsMp4TrackTypeVideo = 0x02,
};

// 8.8.1 Movie Extends Box (mvex)
// ISO_IEC_14496-12-base-format-2012.pdf, page 64
// Tells readers that the file may have moof boxes, which hold more samples of the tracks.
class SrsMp4MovieExtendsBox : public SrsMp4Box
{
public:
    SrsMp4MovieExtendsBox();
    virtual ~SrsMp4MovieExtendsBox();

public:
    virtual void add_trex(SrsMp4TrackExtendsBox *v);
};

// 8.8.3 Track Extends Box(trex)
// ISO_IEC_14496-12-base-format-2012.pdf, page 65
class SrsMp4TrackExtendsBox : public SrsMp4FullBox
{
public:
    uint32_t track_ID_;
    // Defaults used in the track fragments.
    uint32_t default_sample_description_index_;
    uint32_t default_sample_duration_;
    uint32_t default_sample_size_;
    uint32_t default_sample_flags_;

public:
    SrsMp4TrackExtendsBox();
    virtual ~SrsMp4TrackExtendsBox();

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual int nb_header();
    virtual srs_error_t encode_header(SrsBuffer *buf);
    virtual srs_error_t decode_header(SrsBuffer *buf);

public:
    virtual std::stringstream &dumps_detail(std::stringstream &ss, SrsMp4DumpContext dc);
};

// 8.3.1 Track Box (trak)
// ISO_IEC_14496-12-base-format-2012.pdf, page 32
class SrsMp4TrackBox : public SrsMp4Box
{
public:
    SrsMp4TrackBox();
    virtual ~SrsMp4TrackBox();

public:
    virtual SrsMp4TrackType track_type();
    virtual SrsMp4TrackHeaderBox *tkhd();
    virtual void set_tkhd(SrsMp4TrackHeaderBox *v);
    virtual void set_edts(SrsMp4EditBox *v);

public:
    virtual SrsMp4ChunkOffsetBox *stco();
    virtual SrsMp4SampleSizeBox *stsz();
    virtual SrsMp4Sample2ChunkBox *stsc();
    virtual SrsMp4DecodingTime2SampleBox *stts();
    virtual SrsMp4CompositionTime2SampleBox *ctts();
    virtual SrsMp4SyncSampleBox *stss();
    virtual SrsMp4MediaHeaderBox *mdhd();

public:
    virtual SrsVideoCodecId vide_codec();
    virtual SrsAudioCodecId soun_codec();
    virtual SrsMp4AvccBox *avcc();
    virtual SrsMp4DecoderSpecificInfo *asc();

public:
    virtual SrsMp4MediaBox *mdia();
    virtual void set_mdia(SrsMp4MediaBox *v);
    virtual SrsMp4MediaInformationBox *minf();
    virtual SrsMp4SampleTableBox *stbl();
    virtual SrsMp4SampleDescriptionBox *stsd();

public:
    virtual SrsMp4VisualSampleEntry *avc1();
    virtual SrsMp4AudioSampleEntry *mp4a();
};

// 8.3.2 Track Header Box (tkhd)
// ISO_IEC_14496-12-base-format-2012.pdf, page 32
class SrsMp4TrackHeaderBox : public SrsMp4FullBox
{
public:
    // In seconds since midnight, Jan. 1, 1904, in UTC.
    uint64_t creation_time_;
    uint64_t modification_time_;
    // Never reused, and cannot be 0.
    uint32_t track_ID_;
    uint32_t reserved0_;
    // In the mvhd timescale: the sum of the edit durations, or of the sample durations without an edit
    // list; all 1s if unknown.
    uint64_t duration_;

public:
    uint64_t reserved1_;
    // Tracks with lower numbers are closer to the viewer; 0 is the normal value.
    int16_t layer_;
    // 0 means no relation to other tracks. Tracks with alternate data for one another share a nonzero
    // group, and only one track of a group is played at a time.
    int16_t alternate_group_;
    // A fixed 8.8 value; full volume is 1.0 (0x0100), the normal value.
    int16_t volume_;
    uint16_t reserved2_;
    // The video transformation matrix; (u,v,w) are restricted to (0,0,1), hex (0,0,0x40000000).
    int32_t matrix_[9];
    // The visual presentation size as fixed-point 16.16 values, which may differ from the pixel size in the
    // sample description; the images are scaled to this size.
    int32_t width_;
    int32_t height_;

public:
    SrsMp4TrackHeaderBox();
    virtual ~SrsMp4TrackHeaderBox();

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual int nb_header();
    virtual srs_error_t encode_header(SrsBuffer *buf);
    virtual srs_error_t decode_header(SrsBuffer *buf);

public:
    virtual std::stringstream &dumps_detail(std::stringstream &ss, SrsMp4DumpContext dc);
};

// 8.6.5 Edit Box (edts)
// ISO_IEC_14496-12-base-format-2012.pdf, page 54
// Maps the presentation timeline to the media timeline as stored in the file.
class SrsMp4EditBox : public SrsMp4Box
{
public:
    SrsMp4EditBox();
    virtual ~SrsMp4EditBox();

public:
    virtual void set_elst(SrsMp4EditListBox *v);
};

// 8.6.6 Edit List Box
// ISO_IEC_14496-12-base-format-2012.pdf, page 55
class SrsMp4ElstEntry
{
public:
    // In the mvhd timescale.
    uint64_t segment_duration_;
    // In the media timescale, in composition time. -1 is an empty edit, which the last edit of a track
    // must never be.
    int64_t media_time_;

public:
    // 0 means a dwell, which shows the media at media_time for segment_duration; otherwise it must be 1.
    int16_t media_rate_integer_;
    int16_t media_rate_fraction_;

public:
    SrsMp4ElstEntry();
    virtual ~SrsMp4ElstEntry();

public:
    virtual std::stringstream &dumps(std::stringstream &ss, SrsMp4DumpContext dc);
    virtual std::stringstream &dumps_detail(std::stringstream &ss, SrsMp4DumpContext dc);
};

// 8.6.6 Edit List Box (elst)
// ISO_IEC_14496-12-base-format-2012.pdf, page 54
class SrsMp4EditListBox : public SrsMp4FullBox
{
public:
    std::vector<SrsMp4ElstEntry> entries_;

public:
    SrsMp4EditListBox();
    virtual ~SrsMp4EditListBox();

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual int nb_header();
    virtual srs_error_t encode_header(SrsBuffer *buf);
    virtual srs_error_t decode_header(SrsBuffer *buf);

public:
    virtual std::stringstream &dumps_detail(std::stringstream &ss, SrsMp4DumpContext dc);
};

// 8.4.1 Media Box (mdia)
// ISO_IEC_14496-12-base-format-2012.pdf, page 36
class SrsMp4MediaBox : public SrsMp4Box
{
public:
    SrsMp4MediaBox();
    virtual ~SrsMp4MediaBox();

public:
    virtual SrsMp4TrackType track_type();
    virtual SrsMp4MediaHeaderBox *mdhd();
    virtual void set_mdhd(SrsMp4MediaHeaderBox *v);
    virtual SrsMp4HandlerReferenceBox *hdlr();
    virtual void set_hdlr(SrsMp4HandlerReferenceBox *v);
    virtual SrsMp4MediaInformationBox *minf();
    virtual void set_minf(SrsMp4MediaInformationBox *v);
};

// 8.4.2 Media Header Box (mdhd)
// ISO_IEC_14496-12-base-format-2012.pdf, page 36
class SrsMp4MediaHeaderBox : public SrsMp4FullBox
{
public:
    // In seconds since midnight, Jan. 1, 1904, in UTC.
    uint64_t creation_time_;
    uint64_t modification_time_;
    // The number of time units in one second of this media.
    uint32_t timescale_;
    // The duration of this media, in its timescale; all 1s if unknown.
    uint64_t duration_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // The ISO 639-2/T three-letter code; each 5-bit character is its ASCII value minus 0x60.
    uint16_t language_;
    uint16_t pre_defined_;

public:
    SrsMp4MediaHeaderBox();
    virtual ~SrsMp4MediaHeaderBox();

public:
    // The ASCII letters of the language code, such as 'u', 'n', 'd'.
    virtual char language0();
    virtual void set_language0(char v);
    virtual char language1();
    virtual void set_language1(char v);
    virtual char language2();
    virtual void set_language2(char v);

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual int nb_header();
    virtual srs_error_t encode_header(SrsBuffer *buf);
    virtual srs_error_t decode_header(SrsBuffer *buf);

public:
    virtual std::stringstream &dumps_detail(std::stringstream &ss, SrsMp4DumpContext dc);
};

// 8.4.3 Handler Reference Box (hdlr)
// ISO_IEC_14496-12-base-format-2012.pdf, page 37
class SrsMp4HandlerReferenceBox : public SrsMp4FullBox
{
public:
    uint32_t pre_defined_;
    SrsMp4HandlerType handler_type_;
    uint32_t reserved_[3];
    // A null-terminated UTF-8 name of the track type, for debugging and inspection.
    std::string name_;

public:
    SrsMp4HandlerReferenceBox();
    virtual ~SrsMp4HandlerReferenceBox();

public:
    virtual bool is_video();
    virtual bool is_audio();

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual int nb_header();
    virtual srs_error_t encode_header(SrsBuffer *buf);
    virtual srs_error_t decode_header(SrsBuffer *buf);

public:
    virtual std::stringstream &dumps_detail(std::stringstream &ss, SrsMp4DumpContext dc);
};

// 8.4.4 Media Information Box (minf)
// ISO_IEC_14496-12-base-format-2012.pdf, page 38
class SrsMp4MediaInformationBox : public SrsMp4Box
{
public:
    SrsMp4MediaInformationBox();
    virtual ~SrsMp4MediaInformationBox();

public:
    virtual SrsMp4VideoMeidaHeaderBox *vmhd();
    virtual void set_vmhd(SrsMp4VideoMeidaHeaderBox *v);
    virtual SrsMp4SoundMeidaHeaderBox *smhd();
    virtual void set_smhd(SrsMp4SoundMeidaHeaderBox *v);
    virtual SrsMp4DataInformationBox *dinf();
    virtual void set_dinf(SrsMp4DataInformationBox *v);
    virtual SrsMp4SampleTableBox *stbl();
    virtual void set_stbl(SrsMp4SampleTableBox *v);
};

// 8.4.5.2 Video Media Header Box (vmhd)
// ISO_IEC_14496-12-base-format-2012.pdf, page 38
// The flags field has the value 1.
class SrsMp4VideoMeidaHeaderBox : public SrsMp4FullBox
{
public:
    // 0 is copy, which copies over the existing image.
    uint16_t graphicsmode_;
    // Red, green and blue, for use by graphics modes.
    uint16_t opcolor_[3];

public:
    SrsMp4VideoMeidaHeaderBox();
    virtual ~SrsMp4VideoMeidaHeaderBox();

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual int nb_header();
    virtual srs_error_t encode_header(SrsBuffer *buf);
    virtual srs_error_t decode_header(SrsBuffer *buf);
};

// 8.4.5.3 Sound Media Header Box (smhd)
// ISO_IEC_14496-12-base-format-2012.pdf, page 39
class SrsMp4SoundMeidaHeaderBox : public SrsMp4FullBox
{
public:
    // A fixed-point 8.8 number that places mono audio in stereo space; 0 is the center, the normal value,
    // -1.0 full left and 1.0 full right.
    int16_t balance_;
    uint16_t reserved_;

public:
    SrsMp4SoundMeidaHeaderBox();
    virtual ~SrsMp4SoundMeidaHeaderBox();

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual int nb_header();
    virtual srs_error_t encode_header(SrsBuffer *buf);
    virtual srs_error_t decode_header(SrsBuffer *buf);
};

// 8.7.1 Data Information Box (dinf)
// ISO_IEC_14496-12-base-format-2012.pdf, page 56
class SrsMp4DataInformationBox : public SrsMp4Box
{
public:
    SrsMp4DataInformationBox();
    virtual ~SrsMp4DataInformationBox();

public:
    virtual SrsMp4DataReferenceBox *dref();
    virtual void set_dref(SrsMp4DataReferenceBox *v);
};

// 8.7.2 Data Reference Box
// ISO_IEC_14496-12-base-format-2012.pdf, page 56
// The flag 0x000001 means the media data is in the same file as the moov box.
class SrsMp4DataEntryBox : public SrsMp4FullBox
{
public:
    std::string location_;

public:
    SrsMp4DataEntryBox();
    virtual ~SrsMp4DataEntryBox();

public:
    virtual bool boxes_in_header();
};

// 8.7.2 Data Reference Box (url )
// ISO_IEC_14496-12-base-format-2012.pdf, page 56
class SrsMp4DataEntryUrlBox : public SrsMp4DataEntryBox
{
public:
    SrsMp4DataEntryUrlBox();
    virtual ~SrsMp4DataEntryUrlBox();

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual int nb_header();
    virtual srs_error_t encode_header(SrsBuffer *buf);
    virtual srs_error_t decode_header(SrsBuffer *buf);

public:
    virtual std::stringstream &dumps_detail(std::stringstream &ss, SrsMp4DumpContext dc);
};

// 8.7.2 Data Reference Box (urn )
// ISO_IEC_14496-12-base-format-2012.pdf, page 56
class SrsMp4DataEntryUrnBox : public SrsMp4DataEntryBox
{
public:
    std::string name_;

public:
    SrsMp4DataEntryUrnBox();
    virtual ~SrsMp4DataEntryUrnBox();

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual int nb_header();
    virtual srs_error_t encode_header(SrsBuffer *buf);
    virtual srs_error_t decode_header(SrsBuffer *buf);

public:
    virtual std::stringstream &dumps_detail(std::stringstream &ss, SrsMp4DumpContext dc);
};

// 8.7.2 Data Reference Box (dref)
// ISO_IEC_14496-12-base-format-2012.pdf, page 56
// The data_reference_index of a sample entry selects an entry of this table.
class SrsMp4DataReferenceBox : public SrsMp4FullBox
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    std::vector<SrsMp4DataEntryBox *> entries_;

public:
    SrsMp4DataReferenceBox();
    virtual ~SrsMp4DataReferenceBox();

public:
    virtual uint32_t entry_count();
    virtual SrsMp4DataEntryBox *entry_at(int index);
    // The box must be a SrsMp4DataEntryBox.
    virtual void append(SrsMp4Box *box);

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual int nb_header();
    virtual srs_error_t encode_header(SrsBuffer *buf);
    virtual srs_error_t decode_header(SrsBuffer *buf);

public:
    virtual std::stringstream &dumps_detail(std::stringstream &ss, SrsMp4DumpContext dc);
};

// 8.5.1 Sample Table Box (stbl)
// ISO_IEC_14496-12-base-format-2012.pdf, page 40
class SrsMp4SampleTableBox : public SrsMp4Box
{
public:
    SrsMp4SampleTableBox();
    virtual ~SrsMp4SampleTableBox();

public:
    virtual SrsMp4SampleDescriptionBox *stsd();
    virtual void set_stsd(SrsMp4SampleDescriptionBox *v);
    virtual SrsMp4ChunkOffsetBox *stco();
    virtual void set_stco(SrsMp4ChunkOffsetBox *v);
    virtual SrsMp4ChunkLargeOffsetBox *co64();
    virtual void set_co64(SrsMp4ChunkLargeOffsetBox *v);
    virtual SrsMp4SampleSizeBox *stsz();
    virtual void set_stsz(SrsMp4SampleSizeBox *v);
    virtual SrsMp4Sample2ChunkBox *stsc();
    virtual void set_stsc(SrsMp4Sample2ChunkBox *v);
    virtual SrsMp4DecodingTime2SampleBox *stts();
    virtual void set_stts(SrsMp4DecodingTime2SampleBox *v);
    virtual SrsMp4CompositionTime2SampleBox *ctts();
    virtual void set_ctts(SrsMp4CompositionTime2SampleBox *v);
    virtual SrsMp4SyncSampleBox *stss();
    virtual void set_stss(SrsMp4SyncSampleBox *v);

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual int nb_header();
    virtual srs_error_t encode_header(SrsBuffer *buf);
    virtual srs_error_t decode_header(SrsBuffer *buf);
};

// 8.5.2 Sample Description Box
// ISO_IEC_14496-12-base-format-2012.pdf, page 43
class SrsMp4SampleEntry : public SrsMp4Box
{
public:
    uint8_t reserved[6];
    // The index of the dref entry for the data of these samples, from 1 to the entry count.
    uint16_t data_reference_index_;

public:
    SrsMp4SampleEntry();
    virtual ~SrsMp4SampleEntry();

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual int nb_header();
    virtual srs_error_t encode_header(SrsBuffer *buf);
    virtual srs_error_t decode_header(SrsBuffer *buf);

public:
    virtual std::stringstream &dumps_detail(std::stringstream &ss, SrsMp4DumpContext dc);
};

// 8.5.2 Sample Description Box (avc1)
// ISO_IEC_14496-12-base-format-2012.pdf, page 44
class SrsMp4VisualSampleEntry : public SrsMp4SampleEntry
{
public:
    uint16_t pre_defined0_;
    uint16_t reserved0_;
    uint32_t pre_defined1_[3];
    // The maximum visual size, in pixels.
    uint16_t width_;
    uint16_t height_;
    uint32_t horizresolution_;
    uint32_t vertresolution_;
    uint32_t reserved1_;
    // Frames per sample, 1 by default.
    uint16_t frame_count_;
    // The first byte is the length of the name, then the name, then padding to 32 bytes. May be all 0.
    char compressorname_[32];
    // 0x0018 means color with no alpha.
    uint16_t depth_;
    int16_t pre_defined2_;

public:
    SrsMp4VisualSampleEntry(SrsMp4BoxType boxType);
    virtual ~SrsMp4VisualSampleEntry();

public:
    virtual SrsMp4AvccBox *avcC();
    virtual void set_avcC(SrsMp4AvccBox *v);

public:
    virtual SrsMp4HvcCBox *hvcC();
    virtual void set_hvcC(SrsMp4HvcCBox *v);

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual int nb_header();
    virtual srs_error_t encode_header(SrsBuffer *buf);
    virtual srs_error_t decode_header(SrsBuffer *buf);

public:
    virtual std::stringstream &dumps_detail(std::stringstream &ss, SrsMp4DumpContext dc);
};

// 5.3.4 AVC Video Stream Definition (avcC)
// ISO_IEC_14496-15-AVC-format-2012.pdf, page 19
class SrsMp4AvccBox : public SrsMp4Box
{
public:
    std::vector<char> avc_config_;

public:
    SrsMp4AvccBox();
    virtual ~SrsMp4AvccBox();

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual int nb_header();
    virtual srs_error_t encode_header(SrsBuffer *buf);
    virtual srs_error_t decode_header(SrsBuffer *buf);

public:
    virtual std::stringstream &dumps_detail(std::stringstream &ss, SrsMp4DumpContext dc);
};

// 8.4.1 HEVC Video Stream Definition (hvcC)
// ISO-14496-15-AVC-file-format-2017.pdf, page 73
class SrsMp4HvcCBox : public SrsMp4Box
{
public:
    std::vector<char> hevc_config_;

public:
    SrsMp4HvcCBox();
    virtual ~SrsMp4HvcCBox();

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual int nb_header();
    virtual srs_error_t encode_header(SrsBuffer *buf);
    virtual srs_error_t decode_header(SrsBuffer *buf);

public:
    virtual std::stringstream &dumps_detail(std::stringstream &ss, SrsMp4DumpContext dc);
};

// 8.5.2 Sample Description Box (mp4a)
// ISO_IEC_14496-12-base-format-2012.pdf, page 45
class SrsMp4AudioSampleEntry : public SrsMp4SampleEntry
{
public:
    uint64_t reserved0_;
    uint16_t channelcount_;
    uint16_t samplesize_;
    uint16_t pre_defined0_;
    uint16_t reserved1_;
    uint32_t samplerate_;

public:
    SrsMp4AudioSampleEntry();
    virtual ~SrsMp4AudioSampleEntry();

public:
    virtual SrsMp4EsdsBox *esds();
    virtual void set_esds(SrsMp4EsdsBox *v);
    virtual SrsMp4DecoderSpecificInfo *asc();

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual int nb_header();
    virtual srs_error_t encode_header(SrsBuffer *buf);
    virtual srs_error_t decode_header(SrsBuffer *buf);

public:
    virtual std::stringstream &dumps_detail(std::stringstream &ss, SrsMp4DumpContext dc);
};

// Table 1 — List of Class Tags for Descriptors
// ISO_IEC_14496-1-System-2010.pdf, page 31
enum SrsMp4ESTagEs {
    SrsMp4ESTagESforbidden = 0x00,
    SrsMp4ESTagESObjectDescrTag = 0x01,
    SrsMp4ESTagESInitialObjectDescrTag = 0x02,
    SrsMp4ESTagESDescrTag = 0x03,
    SrsMp4ESTagESDecoderConfigDescrTag = 0x04,
    SrsMp4ESTagESDecSpecificInfoTag = 0x05,
    SrsMp4ESTagESSLConfigDescrTag = 0x06,
    SrsMp4ESTagESExtSLConfigDescrTag = 0x064,
};

// 7.2.2.2 BaseDescriptor
// ISO_IEC_14496-1-System-2010.pdf, page 32
class SrsMp4BaseDescriptor : public ISrsCodec
{
public:
    SrsMp4ESTagEs tag; // bit(8)
    // The sizeOfInstance of the expandable class, see 8.3.3.
    int32_t vlen;    // bit(28)
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    int start_pos;

public:
    SrsMp4BaseDescriptor();
    virtual ~SrsMp4BaseDescriptor();

public:
    virtual int left_space(SrsBuffer *buf);

public:
    virtual uint64_t nb_bytes();
    virtual srs_error_t encode(SrsBuffer *buf);
    virtual srs_error_t decode(SrsBuffer *buf);

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual int32_t nb_payload() = 0;
    virtual srs_error_t encode_payload(SrsBuffer *buf) = 0;
    virtual srs_error_t decode_payload(SrsBuffer *buf) = 0;

public:
    virtual std::stringstream &dumps_detail(std::stringstream &ss, SrsMp4DumpContext dc);
};

// Table 5 — objectTypeIndication Values
// ISO_IEC_14496-1-System-2010.pdf, page 49
enum SrsMp4ObjectType {
    SrsMp4ObjectTypeForbidden = 0x00,
    // Audio ISO/IEC 14496-3.
    SrsMp4ObjectTypeAac = 0x40,
    // Audio ISO/IEC 13818-3, MPEG-2 audio.
    SrsMp4ObjectTypeMp3 = 0x69,
    // Audio ISO/IEC 11172-3, MPEG-1 audio.
    SrsMp4ObjectTypeMp1a = 0x6B,
};

extern SrsMp4ObjectType srs_mp4_audio_object_type(SrsAudioCodecId codec, SrsAudioSampleRate sample_rate);

// Table 6 — streamType Values
// ISO_IEC_14496-1-System-2010.pdf, page 51
enum SrsMp4StreamType {
    SrsMp4StreamTypeForbidden = 0x00,
    SrsMp4StreamTypeAudioStream = 0x05,
};

// 7.2.6.7 DecoderSpecificInfo
// ISO_IEC_14496-1-System-2010.pdf, page 51
class SrsMp4DecoderSpecificInfo : public SrsMp4BaseDescriptor
{
public:
    // AAC Audio Specific Config.
    // 1.6.2.1 AudioSpecificConfig, in ISO_IEC_14496-3-AAC-2001.pdf, page 33.
    std::vector<char> asc_;

public:
    SrsMp4DecoderSpecificInfo();
    virtual ~SrsMp4DecoderSpecificInfo();

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual int32_t nb_payload();
    virtual srs_error_t encode_payload(SrsBuffer *buf);
    virtual srs_error_t decode_payload(SrsBuffer *buf);

public:
    virtual std::stringstream &dumps_detail(std::stringstream &ss, SrsMp4DumpContext dc);
};

// 7.2.6.6 DecoderConfigDescriptor
// ISO_IEC_14496-1-System-2010.pdf, page 48
class SrsMp4DecoderConfigDescriptor : public SrsMp4BaseDescriptor
{
public:
    SrsMp4ObjectType objectTypeIndication; // bit(8)
    SrsMp4StreamType streamType;           // bit(6)
    uint8_t upStream;                      // bit(1)
    uint8_t reserved;                      // bit(1)
    uint32_t bufferSizeDB_;                // bit(24)
    uint32_t maxBitrate_;
    uint32_t avgBitrate_;
    SrsMp4DecoderSpecificInfo *decSpecificInfo_; // optional.
public:
    SrsMp4DecoderConfigDescriptor();
    virtual ~SrsMp4DecoderConfigDescriptor();

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual int32_t nb_payload();
    virtual srs_error_t encode_payload(SrsBuffer *buf);
    virtual srs_error_t decode_payload(SrsBuffer *buf);

public:
    virtual std::stringstream &dumps_detail(std::stringstream &ss, SrsMp4DumpContext dc);
};

// 7.3.2.3 SL Packet Header Configuration
// ISO_IEC_14496-1-System-2010.pdf, page 92
class SrsMp4SLConfigDescriptor : public SrsMp4BaseDescriptor
{
public:
    uint8_t predefined_;

public:
    SrsMp4SLConfigDescriptor();
    virtual ~SrsMp4SLConfigDescriptor();

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual int32_t nb_payload();
    virtual srs_error_t encode_payload(SrsBuffer *buf);
    virtual srs_error_t decode_payload(SrsBuffer *buf);
};

// 7.2.6.5 ES_Descriptor
// ISO_IEC_14496-1-System-2010.pdf, page 47
class SrsMp4ES_Descriptor : public SrsMp4BaseDescriptor
{
public:
    uint16_t ES_ID_;
    uint8_t streamDependenceFlag_; // bit(1)
    uint8_t URL_Flag_;             // bit(1)
    uint8_t OCRstreamFlag_;        // bit(1)
    uint8_t streamPriority_;       // bit(5)
    // if (streamDependenceFlag)
    uint16_t dependsOn_ES_ID_;
    // if (URL_Flag)
    std::vector<char> URLstring_;
    // if (OCRstreamFlag)
    uint16_t OCR_ES_Id_;
    SrsMp4DecoderConfigDescriptor decConfigDescr_;
    SrsMp4SLConfigDescriptor slConfigDescr_;

public:
    SrsMp4ES_Descriptor();
    virtual ~SrsMp4ES_Descriptor();

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual int32_t nb_payload();
    virtual srs_error_t encode_payload(SrsBuffer *buf);
    virtual srs_error_t decode_payload(SrsBuffer *buf);

public:
    virtual std::stringstream &dumps_detail(std::stringstream &ss, SrsMp4DumpContext dc);
};

// 5.6 Sample Description Boxes
// Elementary Stream Descriptors (esds)
// ISO_IEC_14496-14-MP4-2003.pdf, page 15
// @see http://www.mp4ra.org/codecs.html
class SrsMp4EsdsBox : public SrsMp4FullBox
{
public:
    SrsMp4ES_Descriptor *es;

public:
    SrsMp4EsdsBox();
    virtual ~SrsMp4EsdsBox();

public:
    virtual SrsMp4DecoderSpecificInfo *asc();

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual int nb_header();
    virtual srs_error_t encode_header(SrsBuffer *buf);
    virtual srs_error_t decode_header(SrsBuffer *buf);

public:
    virtual std::stringstream &dumps_detail(std::stringstream &ss, SrsMp4DumpContext dc);
};

// 8.5.2 Sample Description Box (stsd), for Audio/Video.
// ISO_IEC_14496-12-base-format-2012.pdf, page 40
class SrsMp4SampleDescriptionBox : public SrsMp4FullBox
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    std::vector<SrsMp4SampleEntry *> entries_;

public:
    SrsMp4SampleDescriptionBox();
    virtual ~SrsMp4SampleDescriptionBox();

public:
    virtual SrsMp4VisualSampleEntry *avc1();
    virtual SrsMp4AudioSampleEntry *mp4a();

public:
    virtual uint32_t entry_count();
    virtual SrsMp4SampleEntry *entrie_at(int index);
    // The box must be a SrsMp4SampleEntry.
    virtual void append(SrsMp4Box *box);

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual int nb_header();
    virtual srs_error_t encode_header(SrsBuffer *buf);
    virtual srs_error_t decode_header(SrsBuffer *buf);
    virtual bool boxes_in_header();

public:
    virtual std::stringstream &dumps_detail(std::stringstream &ss, SrsMp4DumpContext dc);
};

// 8.6.1.2 Decoding Time to Sample Box (stts), for Audio/Video.
// ISO_IEC_14496-12-base-format-2012.pdf, page 48
class SrsMp4SttsEntry
{
public:
    // The number of consecutive samples with this delta.
    uint32_t sample_count_;
    // In the media timescale.
    uint32_t sample_delta_;
    SrsMp4SttsEntry();
    virtual ~SrsMp4SttsEntry();

public:
    virtual std::stringstream &dumps_detail(std::stringstream &ss, SrsMp4DumpContext dc);
};

// 8.6.1.2 Decoding Time to Sample Box (stts), for Audio/Video.
// ISO_IEC_14496-12-base-format-2012.pdf, page 48
class SrsMp4DecodingTime2SampleBox : public SrsMp4FullBox
{
public:
    std::vector<SrsMp4SttsEntry> entries_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    uint32_t index_;
    uint32_t count_;

public:
    SrsMp4DecodingTime2SampleBox();
    virtual ~SrsMp4DecodingTime2SampleBox();

public:
    virtual srs_error_t initialize_counter();
    virtual srs_error_t on_sample(uint32_t sample_index, SrsMp4SttsEntry **ppentry);

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual int nb_header();
    virtual srs_error_t encode_header(SrsBuffer *buf);
    virtual srs_error_t decode_header(SrsBuffer *buf);

public:
    virtual std::stringstream &dumps_detail(std::stringstream &ss, SrsMp4DumpContext dc);
};

// 8.6.1.3 Composition Time to Sample Box (ctts), for Video.
// ISO_IEC_14496-12-base-format-2012.pdf, page 49
class SrsMp4CttsEntry
{
public:
    // The number of consecutive samples with this offset.
    uint32_t sample_count_;
    // CT(n) = DT(n) + CTTS(n); unsigned 32 bits in version 0, where DT must be less than CT, and signed
    // 32 bits in version 1.
    int64_t sample_offset_;
    SrsMp4CttsEntry();
    virtual ~SrsMp4CttsEntry();

public:
    virtual std::stringstream &dumps_detail(std::stringstream &ss, SrsMp4DumpContext dc);
};

// 8.6.1.3 Composition Time to Sample Box (ctts), for Video.
// ISO_IEC_14496-12-base-format-2012.pdf, page 49
class SrsMp4CompositionTime2SampleBox : public SrsMp4FullBox
{
public:
    std::vector<SrsMp4CttsEntry> entries_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    uint32_t index_;
    uint32_t count_;

public:
    SrsMp4CompositionTime2SampleBox();
    virtual ~SrsMp4CompositionTime2SampleBox();

public:
    virtual srs_error_t initialize_counter();
    virtual srs_error_t on_sample(uint32_t sample_index, SrsMp4CttsEntry **ppentry);

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual int nb_header();
    virtual srs_error_t encode_header(SrsBuffer *buf);
    virtual srs_error_t decode_header(SrsBuffer *buf);

public:
    virtual std::stringstream &dumps_detail(std::stringstream &ss, SrsMp4DumpContext dc);
};

// 8.6.2 Sync Sample Box (stss), for Video.
// ISO_IEC_14496-12-base-format-2012.pdf, page 51
class SrsMp4SyncSampleBox : public SrsMp4FullBox
{
public:
    uint32_t entry_count_;
    // In strictly increasing order, starting from 1.
    uint32_t *sample_numbers_;

public:
    SrsMp4SyncSampleBox();
    virtual ~SrsMp4SyncSampleBox();

public:
    virtual bool is_sync(uint32_t sample_index);

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual int nb_header();
    virtual srs_error_t encode_header(SrsBuffer *buf);
    virtual srs_error_t decode_header(SrsBuffer *buf);

public:
    virtual std::stringstream &dumps_detail(std::stringstream &ss, SrsMp4DumpContext dc);
};

// 8.7.4 Sample To Chunk Box (stsc), for Audio/Video.
// ISO_IEC_14496-12-base-format-2012.pdf, page 58
class SrsMp4StscEntry
{
public:
    // The first chunk of a run of chunks with the same samples_per_chunk and sample_description_index.
    // The first chunk of a track is 1.
    uint32_t first_chunk_;
    uint32_t samples_per_chunk_;
    // From 1 to the number of entries in the stsd.
    uint32_t sample_description_index_;
    SrsMp4StscEntry();

public:
    virtual std::stringstream &dumps_detail(std::stringstream &ss, SrsMp4DumpContext dc);
};

// 8.7.4 Sample To Chunk Box (stsc), for Audio/Video.
// ISO_IEC_14496-12-base-format-2012.pdf, page 58
class SrsMp4Sample2ChunkBox : public SrsMp4FullBox
{
public:
    uint32_t entry_count_;
    SrsMp4StscEntry *entries_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    uint32_t index_;

public:
    SrsMp4Sample2ChunkBox();
    virtual ~SrsMp4Sample2ChunkBox();

public:
    virtual void initialize_counter();
    virtual SrsMp4StscEntry *on_chunk(uint32_t chunk_index);

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual int nb_header();
    virtual srs_error_t encode_header(SrsBuffer *buf);
    virtual srs_error_t decode_header(SrsBuffer *buf);

public:
    virtual std::stringstream &dumps_detail(std::stringstream &ss, SrsMp4DumpContext dc);
};

// 8.7.5 Chunk Offset Box (stco), for Audio/Video.
// ISO_IEC_14496-12-base-format-2012.pdf, page 59
// A sample table has at most one of stco and co64.
class SrsMp4ChunkOffsetBox : public SrsMp4FullBox
{
public:
    uint32_t entry_count_;
    // The offset of each chunk in the file.
    uint32_t *entries_;

public:
    SrsMp4ChunkOffsetBox();
    virtual ~SrsMp4ChunkOffsetBox();

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual int nb_header();
    virtual srs_error_t encode_header(SrsBuffer *buf);
    virtual srs_error_t decode_header(SrsBuffer *buf);

public:
    virtual std::stringstream &dumps_detail(std::stringstream &ss, SrsMp4DumpContext dc);
};

// 8.7.5 Chunk Large Offset Box (co64), for Audio/Video.
// ISO_IEC_14496-12-base-format-2012.pdf, page 59
// A sample table has at most one of stco and co64.
class SrsMp4ChunkLargeOffsetBox : public SrsMp4FullBox
{
public:
    uint32_t entry_count_;
    // The offset of each chunk in the file.
    uint64_t *entries_;

public:
    SrsMp4ChunkLargeOffsetBox();
    virtual ~SrsMp4ChunkLargeOffsetBox();

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual int nb_header();
    virtual srs_error_t encode_header(SrsBuffer *buf);
    virtual srs_error_t decode_header(SrsBuffer *buf);

public:
    virtual std::stringstream &dumps_detail(std::stringstream &ss, SrsMp4DumpContext dc);
};

// 8.7.3.2 Sample Size Box (stsz), for Audio/Video.
// ISO_IEC_14496-12-base-format-2012.pdf, page 58
class SrsMp4SampleSizeBox : public SrsMp4FullBox
{
public:
    // Nonzero when all samples have this size, and no table follows; 0 when the table gives each size.
    uint32_t sample_size_;
    // The number of samples in the track, and the table size when sample_size is 0.
    uint32_t sample_count_;
    uint32_t *entry_sizes_;

public:
    SrsMp4SampleSizeBox();
    virtual ~SrsMp4SampleSizeBox();

public:
    virtual srs_error_t get_sample_size(uint32_t sample_index, uint32_t *psample_size);

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual int nb_header();
    virtual srs_error_t encode_header(SrsBuffer *buf);
    virtual srs_error_t decode_header(SrsBuffer *buf);

public:
    virtual std::stringstream &dumps_detail(std::stringstream &ss, SrsMp4DumpContext dc);
};

// 8.10.1 User Data Box (udta)
// ISO_IEC_14496-12-base-format-2012.pdf, page 78
class SrsMp4UserDataBox : public SrsMp4Box
{
public:
    std::vector<char> data_;

public:
    SrsMp4UserDataBox();
    virtual ~SrsMp4UserDataBox();

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual int nb_header();
    virtual srs_error_t encode_header(SrsBuffer *buf);
    virtual srs_error_t decode_header(SrsBuffer *buf);

public:
    virtual std::stringstream &dumps_detail(std::stringstream &ss, SrsMp4DumpContext dc);
};

// @doc https://patches.videolan.org/patch/103/
struct SrsMp4SegmentIndexEntry {
    uint8_t reference_type_;       // 1bit
    uint32_t referenced_size_;     // 31bits
    uint32_t subsegment_duration_; // 32bits
    uint8_t starts_with_SAP_;      // 1bit
    uint8_t SAP_type_;             // 3bits
    uint32_t SAP_delta_time_;      // 28bits
};

// For MPEG-DASH.
// @doc https://gpac.wp.imt.fr/2012/02/01/dash-support/
// @doc https://patches.videolan.org/patch/103/
// @doc https://github.com/necccc/iso-bmff-parser-stream/blob/master/lib/box/sidx.js
class SrsMp4SegmentIndexBox : public SrsMp4Box
{
public:
    uint8_t version_;
    uint32_t flags_;
    uint32_t reference_id_;
    uint32_t timescale_;
    uint64_t earliest_presentation_time_;
    uint64_t first_offset_;
    std::vector<SrsMp4SegmentIndexEntry> entries_;

public:
    SrsMp4SegmentIndexBox();
    virtual ~SrsMp4SegmentIndexBox();

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual int nb_header();
    virtual srs_error_t encode_header(SrsBuffer *buf);
    virtual srs_error_t decode_header(SrsBuffer *buf);

public:
    virtual std::stringstream &dumps_detail(std::stringstream &ss, SrsMp4DumpContext dc);
};

// Sample auxiliary information sizes box (saiz)
// @see ISO_IEC_14496-12-base-format-2012.pdf, 8.7.8, page 62
// @see https://github.com/gpac/mp4box.js/blob/master/src/parsing/saiz.js
// Syntax
// aligned(8) class SampleAuxiliaryInformationSizesBox extends FullBox('saiz', version=0, flags)
// {
//   if (flags & 1) {
//     unsigned int(32) aux_info_type;
//     unsigned int(32) aux_info_type_parameter;
//   }
//   unsigned int(8) default_sample_info_size;
//   unsigned int(32) sample_count;
//   if (default_sample_info_size == 0) {
//     unsigned int(8) sample_info_size[sample_count];
//   }
// }
class SrsMp4SampleAuxiliaryInfoSizeBox : public SrsMp4FullBox
{
public:
    uint32_t aux_info_type_;
    uint32_t aux_info_type_parameter_;

    uint8_t default_sample_info_size_;
    uint32_t sample_count_;
    std::vector<uint8_t> sample_info_sizes_;

public:
    SrsMp4SampleAuxiliaryInfoSizeBox();
    virtual ~SrsMp4SampleAuxiliaryInfoSizeBox();

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual int nb_header();
    virtual srs_error_t encode_header(SrsBuffer *buf);
    virtual srs_error_t decode_header(SrsBuffer *buf);

public:
    virtual std::stringstream &dumps_detail(std::stringstream &ss, SrsMp4DumpContext dc);
};

// Sample auxiliary information offsets box (saio)
// @see ISO_IEC_14496-12-base-format-2012.pdf, 8.7.9, page 63
// @see https://github.com/gpac/mp4box.js/blob/master/src/parsing/saio.js
// Syntax
// aligned(8) class SampleAuxiliaryInformationOffsetsBox extends FullBox('saio', version, flags)
// {
//   if (flags & 1) {
//     unsigned int(32) aux_info_type;
//     unsigned int(32) aux_info_type_parameter;
//   }
//   unsigned int(32) entry_count;
//   if (version == 0) {
//     unsigned int(32) offset[entry_count];
//   } else {
//     unsigned int(64) offset[entry_count];
//   }
// }
class SrsMp4SampleAuxiliaryInfoOffsetBox : public SrsMp4FullBox
{
public:
    uint32_t aux_info_type_;
    uint32_t aux_info_type_parameter_;
    // uint32_t entry_count;
    std::vector<uint64_t> offsets_;

public:
    SrsMp4SampleAuxiliaryInfoOffsetBox();
    virtual ~SrsMp4SampleAuxiliaryInfoOffsetBox();

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual int nb_header();
    virtual srs_error_t encode_header(SrsBuffer *buf);
    virtual srs_error_t decode_header(SrsBuffer *buf);

public:
    virtual std::stringstream &dumps_detail(std::stringstream &ss, SrsMp4DumpContext dc);
};

enum SrsMp4CencSampleEncryptionFlags {
    SrsMp4CencSampleEncryptionTrackDefault = 0x01,
    SrsMp4CencSampleEncryptionUseSubSample = 0x02,
};

struct SrsMp4SubSampleEncryptionInfo : public ISrsCodec {
    uint16_t bytes_of_clear_data_;
    uint32_t bytes_of_protected_data_;

    SrsMp4SubSampleEncryptionInfo();
    virtual ~SrsMp4SubSampleEncryptionInfo();

    virtual uint64_t nb_bytes();
    virtual srs_error_t encode(SrsBuffer *buf);
    virtual srs_error_t decode(SrsBuffer *buf);

    virtual std::stringstream &dumps(std::stringstream &ss, SrsMp4DumpContext dc);
};

class SrsMp4SampleEncryptionEntry : public ISrsCodec
{
public:
    // Only when flags & 0x02.
    std::vector<SrsMp4SubSampleEncryptionInfo> subsample_infos_;

public:
    SrsMp4SampleEncryptionEntry(SrsMp4FullBox *senc, uint8_t per_sample_iv_size);
    virtual ~SrsMp4SampleEncryptionEntry();

    virtual srs_error_t set_iv(uint8_t *iv, uint8_t iv_size);
    virtual uint64_t nb_bytes();
    virtual srs_error_t encode(SrsBuffer *buf);
    virtual srs_error_t decode(SrsBuffer *buf);

    virtual std::stringstream &dumps(std::stringstream &ss, SrsMp4DumpContext dc);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    SrsMp4FullBox *senc_;
    uint8_t per_sample_iv_size_;
    uint8_t *iv_;
};

// Sample encryption box (senc)
// @see ISO-IEC-23001-7.pdf 7.2.1
// @see https://cdn.standards.iteh.ai/samples/84637/c960c91d60ae4da7a2f9380bd7e08642/ISO-IEC-FDIS-23001-7.pdf
// CENC SAI is the sample auxiliary information that holds the IV and subsample information of a sample.
// @see ISO-IEC-23001-7.pdf 7.2.2
// Syntax
// aligned(8) class SampleEncryptionBox extend FullBox(`senc`, version=0, flags)
// {
//   unsigned int(32) sample_count;
//   {
//     unsigned int(Per_Sample_IV_Size*8) InitializationVector;
//     if (flags & 0x000002)
//     {
//       unsigned int(16) subsample_count;
//       {
//         unsigned int(16) BytesOfClearData;
//         unsigned int(32) BytesOfProtectedData;
//       } [ subsample_count ]
//     }
//   } [ sample_count ]
// }
class SrsMp4SampleEncryptionBox : public SrsMp4FullBox
{
public:
    std::vector<SrsMp4SampleEncryptionEntry *> entries_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    uint8_t per_sample_iv_size_;

public:
    // @see ISO-IEC-23001-7.pdf 9.1
    // Per_Sample_IV_Size is 0, 8 or 16.
    SrsMp4SampleEncryptionBox(uint8_t per_sample_iv_size);
    virtual ~SrsMp4SampleEncryptionBox();

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual int nb_header();
    virtual srs_error_t encode_header(SrsBuffer *buf);
    virtual srs_error_t decode_header(SrsBuffer *buf);

public:
    virtual std::stringstream &dumps_detail(std::stringstream &ss, SrsMp4DumpContext dc);
};

// Original Format Box (frma)
// @see ISO_IEC_14496-12-base-format-2012.pdf, 8.12.2, page 81
// aligned(8) class OriginalFormatBox(codingname) extends Box ('frma') {
//   unsigned int(32) data_format = codingname;
// }
class SrsMp4OriginalFormatBox : public SrsMp4Box
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    uint32_t data_format_;

public:
    SrsMp4OriginalFormatBox(uint32_t original_format);
    virtual ~SrsMp4OriginalFormatBox();

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual int nb_header();
    virtual srs_error_t encode_header(SrsBuffer *buf);
    virtual srs_error_t decode_header(SrsBuffer *buf);

public:
    virtual std::stringstream &dumps_detail(std::stringstream &ss, SrsMp4DumpContext dc);
};

// Scheme Type Box (schm)
// @see ISO_IEC_14496-12-base-format-2012.pdf, 8.12.5, page 81
// aligned(8) class SchemeTypeBox extends FullBox('schm', 0, flags) {
//   unsigned int(32) scheme_type;    // 4CC identifying the scheme
//   unsigned int(32) scheme_version; // scheme version
//   if (flags & 0x000001) {
//     unsigned int(8) scheme_uri[];  // browser uri
//   }
// }
// @see ISO-IEC-23001-7.pdf 4.1
// The scheme_version field shall be 0x00010000, major version 1 and minor version 0.
#define SCHM_SCHEME_URI_MAX_SIZE 128
class SrsMp4SchemeTypeBox : public SrsMp4FullBox
{
public:
    uint32_t scheme_type_;
    uint32_t scheme_version_;
    char scheme_uri_[SCHM_SCHEME_URI_MAX_SIZE];
    uint32_t scheme_uri_size_;

public:
    SrsMp4SchemeTypeBox();
    virtual ~SrsMp4SchemeTypeBox();

public:
    virtual void set_scheme_uri(char *uri, uint32_t uri_size);

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual int nb_header();
    virtual srs_error_t encode_header(SrsBuffer *buf);
    virtual srs_error_t decode_header(SrsBuffer *buf);

public:
    virtual std::stringstream &dumps_detail(std::stringstream &ss, SrsMp4DumpContext dc);
};

// Scheme Information Box (schi)
// @see ISO_IEC_14496-12-base-format-2012.pdf, 8.12.6, page 82
// aligned(8) class SchemeInformationBox extends Box('schi') {
//   Box scheme_specific_data[];
// }
class SrsMp4SchemeInfoBox : public SrsMp4Box
{
public:
    SrsMp4SchemeInfoBox();
    virtual ~SrsMp4SchemeInfoBox();
};

// Protection Scheme Information Box (sinf)
// @see ISO_IEC_14496-12-base-format-2012.pdf, 8.12.1, page 80
// aligned(8) class ProtectionSchemeInfoBox(fmt) extends Box('sinf') {
//   OriginalFormatBox(fmt) original_format; // frma
//   SchemeTypeBox          scheme_type_box; // optional
//   SchemeInformationBox   info;            // optional
// }
class SrsMp4ProtectionSchemeInfoBox : public SrsMp4Box
{
public:
    SrsMp4ProtectionSchemeInfoBox();
    virtual ~SrsMp4ProtectionSchemeInfoBox();

public:
    virtual SrsMp4OriginalFormatBox *frma();
    virtual void set_frma(SrsMp4OriginalFormatBox *v);
    virtual SrsMp4SchemeTypeBox *schm();
    virtual void set_schm(SrsMp4SchemeTypeBox *v);
    virtual SrsMp4SchemeInfoBox *schi();
    virtual void set_schi(SrsMp4SchemeInfoBox *v);
};

// Track Encryption box (tenc)
// @see ISO-IEC-23001-7.pdf 8.2
// aligned(8) class TrackEncryptionBox extends FullBox('tenc', version, flags=0) {
//   unsigned int(8) reserved = 0;
//   if (version == 0) {
//     unsigned int(8) reserved = 0;
//   } else { // version is 1 or greater
//     unsigned int(4) default_crypt_byte_block;
//     unsigned int(4) default_skip_byte_block;
//   }
//   unsigned int(8) default_isProtected;
//   unsigned int(8) default_Per_Sample_IV_Size;
//   unsigned int(8)[16] default_KID;
//   if (default_isProtected == 1 && default_Per_Sample_IV_Size == 0) {
//     unsigned int(8) default_constant_IV_size;
//     unsigned int(8)[default_constant_IV_size] default_constant_IV;
//   }
// }
// @see https://developer.apple.com/documentation/http-live-streaming/about-the-common-media-application-format-with-http-live-streaming-hls
// HLS supports fMP4 segments that are clear or encrypted with 'cbcs', signaled by EXT-X-KEY with
// METHOD=SAMPLE-AES.
// @see ISO-IEC-23001-7.pdf 10.4.1 Definition, for 'cbcs':
//   - The schm scheme_type is 'cbcs', and the tenc version is 1.
//   - NAL structured video uses subsample encryption (9.5) with a pattern (9.6), so crypt_byte_block and
//     skip_byte_block are not 0.
//   - IVs are constant: default_Per_Sample_IV_Size and Per_Sample_IV_Size are 0.
//   - Other tracks use whole-block full-sample encryption (9.7), so skip_byte_block is 0.
//   - crypt_byte_block + skip_byte_block should be 10.
//   - The slice header is clear. The protected range starts at the first full byte of slice data (for HEVC,
//     after the byte_alignment() of the slice_segment_header()) and runs to the end of the NAL unit.
// @see ISO-IEC-23001-7.pdf 10.4.2, informative
//   A 1:9 pattern (10% encrypted) is recommended: the first 16-byte block of each 160 bytes is encrypted.
//   A partial 16-byte block at the end of the protected range is left clear.
// @see ISO-IEC-23001-7.pdf 9.7 Whole-block full sample encryption
//   The sample is encrypted from offset 0 up to the last 16-byte boundary, leaving the trailing 0-15 bytes
//   clear. The IV is reset at every sample.
class SrsMp4TrackEncryptionBox : public SrsMp4FullBox
{
public:
    uint8_t reserved_;
    uint8_t reserved_2_;
    uint8_t default_crypt_byte_block_;
    uint8_t default_skip_byte_block_;
    uint8_t default_is_protected_;
    uint8_t default_per_sample_IV_size_;
    uint8_t default_KID_[16];
    uint8_t default_constant_IV_size_;
    uint8_t default_constant_IV_[16];

public:
    SrsMp4TrackEncryptionBox();
    virtual ~SrsMp4TrackEncryptionBox();

public:
    virtual void set_default_constant_IV(uint8_t *iv, uint8_t iv_size);

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual int nb_header();
    virtual srs_error_t encode_header(SrsBuffer *buf);
    virtual srs_error_t decode_header(SrsBuffer *buf);

public:
    virtual std::stringstream &dumps_detail(std::stringstream &ss, SrsMp4DumpContext dc);
};

// TODO: add SchemeTypeBox(schm), set scheme_type=cbcs

// Usually one video or audio frame.
class SrsMp4Sample
{
public:
    SrsFrameType type_;
    srs_off_t offset_;
    // Starts from 0 within the track.
    uint32_t index_;
    // In tbn.
    uint64_t dts_;
    // In tbn; for video.
    uint64_t pts_;
    uint32_t tbn_;
    // For video.
    SrsVideoAvcFrameType frame_type_;
    // In ms, such as to make the A/V timestamps increase monotonically.
    int32_t adjust_;
    uint32_t nb_data_;
    uint8_t *data_;

public:
    SrsMp4Sample();
    virtual ~SrsMp4Sample();

public:
    virtual uint32_t dts_ms();
    virtual uint32_t pts_ms();
};

// Keeps A/V in sync in DVR MP4 files, by the offset between the first audio and video timestamps.
class SrsMp4DvrJitter
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    uint64_t video_start_dts_;
    uint64_t audio_start_dts_;
    bool has_first_video_;
    bool has_first_audio_;

public:
    SrsMp4DvrJitter();
    virtual ~SrsMp4DvrJitter();

public:
    virtual void on_sample(SrsMp4Sample *sample);
    // The stts delta of the first sample of the track.
    virtual uint32_t get_first_sample_delta(SrsFrameType track);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual void reset();
    virtual bool is_initialized();
};

class SrsMp4SampleManager
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    SrsMp4DvrJitter *jitter_;

public:
    std::vector<SrsMp4Sample *> samples_;

public:
    SrsMp4SampleManager();
    virtual ~SrsMp4SampleManager();

public:
    // The moov must have at least one track.
    virtual srs_error_t load(SrsMp4MovieBox *moov);
    virtual SrsMp4Sample *at(uint32_t index);
    virtual void append(SrsMp4Sample *sample);
    virtual srs_error_t write(SrsMp4MovieBox *moov);
    // The dts is the dts of the last segment.
    virtual srs_error_t write(SrsMp4TrackFragmentBox *traf, uint64_t dts);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t write_track(SrsFrameType track,
                                    SrsMp4DecodingTime2SampleBox *stts, SrsMp4SyncSampleBox *stss, SrsMp4CompositionTime2SampleBox *ctts,
                                    SrsMp4Sample2ChunkBox *stsc, SrsMp4SampleSizeBox *stsz, SrsMp4FullBox *co);
    virtual srs_error_t do_load(std::map<uint64_t, SrsMp4Sample *> &tses, SrsMp4MovieBox *moov);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // The tses map is keyed by the sample offset in the file.
    // TODO: Support co64 for stco.
    virtual srs_error_t load_trak(std::map<uint64_t, SrsMp4Sample *> &tses, SrsFrameType tt,
              SrsMp4MediaHeaderBox *mdhd, SrsMp4ChunkOffsetBox *stco, SrsMp4SampleSizeBox *stsz, SrsMp4Sample2ChunkBox *stsc,
              SrsMp4DecodingTime2SampleBox *stts, SrsMp4CompositionTime2SampleBox *ctts, SrsMp4SyncSampleBox *stss);
};

// Reads boxes without decoding them.
class SrsMp4BoxReader
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsReadSeeker *rsio_;
    char *buf_;

public:
    SrsMp4BoxReader();
    virtual ~SrsMp4BoxReader();

public:
    virtual srs_error_t initialize(ISrsReadSeeker *rs);

public:
    // Fills the stream with the bytes of the box to decode; for mdat, only its header.
    virtual srs_error_t read(SrsSimpleStream *stream, SrsMp4Box **ppbox);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srs_error_t do_read(SrsSimpleStream *stream, SrsMp4Box *&box);

public:
    virtual srs_error_t skip(SrsMp4Box *box, SrsSimpleStream *stream);
};

class SrsMp4Decoder
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    SrsMp4BoxBrand brand_;
    SrsMp4SampleManager *samples_;
    uint32_t current_index_;
    srs_off_t current_offset_;

public:
    // Forbidden when there is no video track.
    // TODO: FIXME: Use SrsFormat instead.
    SrsVideoCodecId vcodec_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // The avcC, with the H.264 SPS and PPS.
    std::vector<char>
        pavcc_;
    bool avcc_written_;

public:
    // Forbidden when there is no audio track.
    SrsAudioCodecId acodec_;
    SrsAudioSampleRate sample_rate_;
    SrsAudioSampleBits sound_bits_;
    SrsAudioChannels channels_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // The AAC asc from the esds box.
    std::vector<char>
        pasc_;
    bool asc_written_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // A seeker, to seek to the moov of a general MP4.
    ISrsReadSeeker *rsio_;
    SrsMp4BoxReader *br_;
    // TODO: FIXME: refine for performance issue.
    SrsSimpleStream *stream_;

public:
    SrsMp4Decoder();
    virtual ~SrsMp4Decoder();

public:
    virtual srs_error_t initialize(ISrsReadSeeker *rs);
    // The first two samples are the audio and video sequence headers.
    // The pft is a SrsVideoAvcFrameType for video, ignored for audio. The pct is a SrsVideoAvcFrameTrait for
    // video, or a SrsAudioAacFrameTrait for audio. The pdts and ppts are in ms. The caller frees psample.
    virtual srs_error_t read_sample(SrsMp4HandlerType *pht, uint16_t *pft, uint16_t *pct,
                                    uint32_t *pdts, uint32_t *ppts, uint8_t **psample, uint32_t *pnb_sample);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t parse_ftyp(SrsMp4FileTypeBox *ftyp);
    virtual srs_error_t parse_moov(SrsMp4MovieBox *moov);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // A required_box_type of 0 means any box.
    virtual srs_error_t load_next_box(SrsMp4Box **ppbox, uint32_t required_box_type);
    // Never loads the mdat content, which is too large.
    virtual srs_error_t do_load_next_box(SrsMp4Box **ppbox, uint32_t required_box_type);
};

class ISrsMp4Encoder
{
public:
    ISrsMp4Encoder();
    virtual ~ISrsMp4Encoder();

public:
    SrsVideoCodecId vcodec_;

public:
    virtual srs_error_t initialize(ISrsWriteSeeker *ws) = 0;
    virtual srs_error_t write_sample(SrsFormat *format, SrsMp4HandlerType ht, uint16_t ft, uint16_t ct,
                                     uint32_t dts, uint32_t pts, uint8_t *sample, uint32_t nb_sample) = 0;
    virtual srs_error_t flush() = 0;
    virtual void set_audio_codec(SrsAudioCodecId vcodec, SrsAudioSampleRate sample_rate, SrsAudioSampleBits sound_bits, SrsAudioChannels channels) = 0;
};

class SrsMp4Encoder : public ISrsMp4Encoder
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsWriteSeeker *wsio_;
    // The mdat header at this offset gets the final size at flush.
    srs_off_t mdat_offset_;
    uint64_t mdat_bytes_;
    SrsMp4SampleManager *samples_;

public:
    // Forbidden when there is no audio track.
    SrsAudioCodecId acodec_;
    SrsAudioSampleRate sample_rate_;
    SrsAudioSampleBits sound_bits_;
    SrsAudioChannels channels_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // The AAC asc for the esds box.
    std::vector<char>
        pasc_;
    uint32_t nb_audios_;
    uint64_t aduration_;

public:
    // Forbidden when there is no video track.
    SrsVideoCodecId vcodec_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // The avcC, with the H.264 SPS and PPS.
    std::vector<char>
        pavcc_;
    // The hvcC, with the H.265 VPS, SPS and PPS.
    std::vector<char> phvcc_;
    uint32_t nb_videos_;
    uint64_t vduration_;
    uint32_t width_;
    uint32_t height_;

public:
    SrsMp4Encoder();
    virtual ~SrsMp4Encoder();

public:
    virtual srs_error_t initialize(ISrsWriteSeeker *ws);
    // The ft is a SrsVideoAvcFrameType for video. The ct is a SrsVideoAvcFrameTrait for video, or a
    // SrsAudioAacFrameTrait for audio. The dts and pts are in ms.
    virtual srs_error_t write_sample(SrsFormat *format, SrsMp4HandlerType ht, uint16_t ft, uint16_t ct,
                                     uint32_t dts, uint32_t pts, uint8_t *sample, uint32_t nb_sample);
    // Writes the moov.
    virtual srs_error_t flush();

    virtual void set_audio_codec(SrsAudioCodecId vcodec, SrsAudioSampleRate sample_rate, SrsAudioSampleBits sound_bits, SrsAudioChannels channels);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t copy_sequence_header(SrsFormat *format, bool vsh, uint8_t *sample, uint32_t nb_sample);
    virtual srs_error_t do_write_sample(SrsMp4Sample *ps, uint8_t *sample, uint32_t nb_sample);
    virtual SrsMp4ObjectType get_audio_object_type();
};

class ISrsMp4M2tsInitEncoder
{
public:
    ISrsMp4M2tsInitEncoder();
    virtual ~ISrsMp4M2tsInitEncoder();

public:
    virtual srs_error_t initialize(ISrsWriter *w) = 0;
    // Writes the sequence header.
    virtual srs_error_t write(SrsFormat *format, bool video, int tid) = 0;
};

// Writes the fMP4 init.mp4 with the sequence header.
// TODO: What the M2ts short for?
class SrsMp4M2tsInitEncoder : public ISrsMp4M2tsInitEncoder
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsWriter *writer_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    uint8_t crypt_byte_block_;
    uint8_t skip_byte_block_;
    unsigned char kid_[16];
    unsigned char iv_[16];
    uint8_t iv_size_;
    bool is_protected_;

public:
    SrsMp4M2tsInitEncoder();
    virtual ~SrsMp4M2tsInitEncoder();

public:
    virtual srs_error_t initialize(ISrsWriter *w);
    // TODO: review kid(map to a key) and iv, which are shared between audio/video tracks.
    virtual void config_encryption(uint8_t crypt_byte_block, uint8_t skip_byte_block, unsigned char *kid, unsigned char *iv, uint8_t iv_size);
    // Writes the sequence header.
    // TODO: merge this method to its sibling.
    virtual srs_error_t write(SrsFormat *format, bool video, int tid);

    /**
     *  The mp4 box format for init.mp4.
     *
     *  |ftyp|
     *  |moov|
     *  |    |mvhd|
     *  |    |trak|
     *  |    |trak|
     *  |    |....|
     *  |    |mvex|
     *  |    |    |trex|
     *  |    |    |trex|
     *  |    |    |....|
     */
    virtual srs_error_t write(SrsFormat *format, int v_tid, int a_tid);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    /**
     * box->type = 'encv' or 'enca'
     * |encv|
     * |    |sinf|
     * |    |    |frma|
     * |    |    |schm|
     * |    |    |schi|
     * |    |    |    |tenc|
     */
    virtual srs_error_t config_sample_description_encryption(SrsMp4SampleEntry *box);
};

class ISrsMp4M2tsSegmentEncoder
{
public:
    ISrsMp4M2tsSegmentEncoder();
    virtual ~ISrsMp4M2tsSegmentEncoder();

public:
    virtual srs_error_t initialize(ISrsWriter *w, uint32_t sequence, srs_utime_t basetime, uint32_t tid) = 0;
    // Caches a sample of raw AAC or AVC data, since the sequence header is in the init.mp4. The ft is a
    // SrsVideoAvcFrameType for video. The dts and pts are in ms.
    virtual srs_error_t write_sample(SrsMp4HandlerType ht, uint16_t ft,
                                     uint32_t dts, uint32_t pts, uint8_t *sample, uint32_t nb_sample) = 0;
    // Writes the moof and mdat.
    virtual srs_error_t flush(uint64_t &dts) = 0;
};

// Caches the samples until flush, since the trun box goes before the mdat.
// TODO: fmp4 support package more than one tracks.
class SrsMp4M2tsSegmentEncoder : public ISrsMp4M2tsSegmentEncoder
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsWriter *writer_;
    uint32_t sequence_number_;
    srs_utime_t decode_basetime_;
    uint32_t track_id_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    uint32_t nb_audios_;
    uint32_t nb_videos_;
    uint32_t styp_bytes_;
    uint64_t mdat_bytes_;
    SrsMp4SampleManager *samples_;

public:
    SrsMp4M2tsSegmentEncoder();
    virtual ~SrsMp4M2tsSegmentEncoder();

public:
    virtual srs_error_t initialize(ISrsWriter *w, uint32_t sequence, srs_utime_t basetime, uint32_t tid);
    virtual srs_error_t write_sample(SrsMp4HandlerType ht, uint16_t ft,
                                     uint32_t dts, uint32_t pts, uint8_t *sample, uint32_t nb_sample);
    virtual srs_error_t flush(uint64_t &dts);
};

// Caches the samples until flush, since the trun box goes before the mdat.
// TODO: fmp4 support package more than one tracks.
class SrsFmp4SegmentEncoder
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsWriter *writer_;
    uint32_t sequence_number_;
    // TODO: audio, video may have different basetime.
    srs_utime_t decode_basetime_;
    uint32_t audio_track_id_;
    uint32_t video_track_id_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    uint32_t nb_audios_;
    uint32_t nb_videos_;
    uint32_t styp_bytes_;
    uint64_t mdat_audio_bytes_;
    uint64_t mdat_video_bytes_;
    SrsMp4SampleManager *audio_samples_;
    SrsMp4SampleManager *video_samples_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    EVP_CIPHER_CTX *cipher_;
    unsigned char iv_[16];
    bool do_sample_encryption_;
    // To find the video slices to encrypt.
    SrsVideoCodecId video_codec_;
    int nalu_length_size_;

public:
    SrsFmp4SegmentEncoder();
    virtual ~SrsFmp4SegmentEncoder();

public:
    virtual srs_error_t initialize(ISrsWriter *w, uint32_t sequence, srs_utime_t basetime, uint32_t v_tid, uint32_t a_tid);
    virtual srs_error_t config_cipher(unsigned char *key, unsigned char *iv);
    // How cbcs finds the video slices to encrypt, used only with a cipher. A nalu_length_size other than
    // 1 to 4 means 4.
    virtual void config_video_cipher(SrsVideoCodecId codec, int nalu_length_size);
    // Caches a sample of raw AAC or AVC data, since the sequence header is in the init.mp4. The ft is a
    // SrsVideoAvcFrameType for video. The dts and pts are in ms.
    virtual srs_error_t write_sample(SrsMp4HandlerType ht, uint16_t ft,
                                     uint32_t dts, uint32_t pts, uint8_t *sample, uint32_t nb_sample);
    // Writes the moof and mdat.
    virtual srs_error_t flush(uint64_t dts);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // Encrypt a sample in place by cbcs, and add its entry to senc.
    virtual void encrypt_video(SrsMp4Sample *sample, SrsMp4SampleEncryptionBox *senc);
    virtual void encrypt_audio(SrsMp4Sample *sample, SrsMp4SampleEncryptionBox *senc);
};

#include <iomanip>

#define SrsMp4SummaryCount 8

extern std::stringstream &srs_mp4_padding(std::stringstream &ss, SrsMp4DumpContext dc, int tab = 4);

extern void srs_mp4_delimiter_inline(std::stringstream &ss, SrsMp4DumpContext dc);
extern void srs_mp4_delimiter_inspace(std::stringstream &ss, SrsMp4DumpContext dc);
extern void srs_mp4_delimiter_newline(std::stringstream &ss, SrsMp4DumpContext dc);

extern std::stringstream &srs_print_mp4_type(std::stringstream &ss, uint32_t v);
extern std::stringstream &srs_mp4_print_bytes(std::stringstream &ss, const char *p, int size, SrsMp4DumpContext dc, int line = SrsMp4SummaryCount, int max = -1);

// TODO: FIXME: Extract to common utility.
template <typename T>
std::stringstream &srs_dumps_array(std::vector<T> &arr, std::stringstream &ss, SrsMp4DumpContext dc,
                                   void (*pfn)(T &, std::stringstream &, SrsMp4DumpContext),
                                   void (*delimiter)(std::stringstream &, SrsMp4DumpContext))
{
    int limit = arr.size();
    if (dc.summary_) {
        limit = srs_min(SrsMp4SummaryCount, limit);
    }

    for (size_t i = 0; i < (size_t)limit; i++) {
        T &elem = arr[i];

        pfn(elem, ss, dc);

        if ((int)i < limit - 1) {
            delimiter(ss, dc);
        }
    }
    return ss;
}

// TODO: FIXME: Extract to common utility.
template <typename T>
std::stringstream &srs_dumps_array(T *arr, int size, std::stringstream &ss, SrsMp4DumpContext dc,
                                   void (*pfn)(T &, std::stringstream &, SrsMp4DumpContext),
                                   void (*delimiter)(std::stringstream &, SrsMp4DumpContext))
{
    int limit = size;
    if (dc.summary_) {
        limit = srs_min(SrsMp4SummaryCount, limit);
    }

    for (size_t i = 0; i < (size_t)limit; i++) {
        T &elem = arr[i];

        pfn(elem, ss, dc);

        if ((int)i < limit - 1) {
            delimiter(ss, dc);
        }
    }
    return ss;
}

template <typename T>
void srs_mp4_pfn_box(T &elem, std::stringstream &ss, SrsMp4DumpContext dc)
{
    elem.dumps(ss, dc);
}

template <typename T>
void srs_mp4_pfn_detail(T &elem, std::stringstream &ss, SrsMp4DumpContext dc)
{
    elem.dumps_detail(ss, dc);
}

template <typename T>
void srs_mp4_pfn_box2(T *&elem, std::stringstream &ss, SrsMp4DumpContext dc)
{
    elem->dumps(ss, dc);
}

template <typename T>
void srs_mp4_pfn_detail2(T *&elem, std::stringstream &ss, SrsMp4DumpContext dc)
{
    elem->dumps_detail(ss, dc);
}

template <typename T>
void srs_mp4_pfn_type(T &elem, std::stringstream &ss, SrsMp4DumpContext /*dc*/)
{
    srs_print_mp4_type(ss, (uint32_t)elem);
}

template <typename T>
void srs_mp4_pfn_hex(T &elem, std::stringstream &ss, SrsMp4DumpContext /*dc*/)
{
    ss << "0x" << std::setw(2) << std::setfill('0') << std::hex << (uint32_t)(uint8_t)elem << std::dec;
}

template <typename T>
void srs_mp4_pfn_elem(T &elem, std::stringstream &ss, SrsMp4DumpContext /*dc*/)
{
    ss << elem;
}

#endif
