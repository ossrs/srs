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

#ifndef SRS_KERNEL_MP4_HPP
#define SRS_KERNEL_MP4_HPP

/*
#include <srs_kernel_mp4.hpp>
*/
#include <srs_core.hpp>

#include <string>

/**
 * 4.2 Object Structure
 * ISO_IEC_14496-12-base-format-2012.pdf, page 16
 */
class SrsMp4Box
{
public:
    // if size is 1 then the actual size is in the field largesize;
    // if size is 0, then this box is the last one in the file, and its contents
    // extend to the end of the file (normally only used for a Media Data Box)
    uint32_t size;
    uint32_t type;
public:
    SrsMp4Box();
    virtual ~SrsMp4Box();
};

/**
 * 4.2 Object Structure
 * ISO_IEC_14496-12-base-format-2012.pdf, page 16
 */
class SrsMp4FullBox : public SrsMp4Box
{
public:
    // an integer that specifies the version of this format of the box.
    uint8_t version;
    // a map of flags
    uint32_t flags;
public:
    SrsMp4FullBox();
    virtual ~SrsMp4FullBox();
};

/**
 * 4.3 File Type Box (ftyp)
 * ISO_IEC_14496-12-base-format-2012.pdf, page 17
 * Files written to this version of this specification must contain a file-type box. For compatibility with an earlier 
 * version of this specification, files may be conformant to this specification and not contain a file-type box. Files 
 * with no file-type box should be read as if they contained an FTYP box with Major_brand='mp41', minor_version=0, and 
 * the single compatible brand 'mp41'.
 */
class SrsMp4FileTypeBox : public SrsMp4Box
{
public:
    // a brand identifier
    uint32_t major_brand;
    // an informative integer for the minor version of the major brand
    uint32_t minor_version;
private:
    // a list, to the end of the box, of brands
    int nb_compatible_brands;
    uint32_t* compatible_brands;
public:
    SrsMp4FileTypeBox();
    virtual ~SrsMp4FileTypeBox();
};

/**
 * 8.1.1 Media Data Box (mdat)
 * ISO_IEC_14496-12-base-format-2012.pdf, page 29
 * This box contains the media data. In video tracks, this box would contain video frames. 
 * A presentation may contain zero or more Media Data Boxes. The actual media data follows the type field; 
 * its structure is described by the metadata (see particularly the sample table, subclause 8.5, and the 
 * item location box, subclause 8.11.3).
 */
class SrsMp4MediaDataBox : public SrsMp4Box
{
private:
    int nb_data;
    uint8_t* data;
public:
    SrsMp4MediaDataBox();
    virtual ~SrsMp4MediaDataBox();
};

/**
 * 8.1.2 Free Space Box (free or skip)
 * ISO_IEC_14496-12-base-format-2012.pdf, page 29
 */
class SrsMp4FreeSpaceBox : public SrsMp4Box
{
public:
    SrsMp4FreeSpaceBox();
    virtual ~SrsMp4FreeSpaceBox();
};

/**
 * 8.2.1 Movie Box (moov)
 * ISO_IEC_14496-12-base-format-2012.pdf, page 30
 * The metadata for a presentation is stored in the single Movie Box which occurs at the top-level of a file.
 * Normally this box is close to the beginning or end of the file, though this is not required.
 */
class SrsMp4MovieBox : public SrsMp4Box
{
public:
    SrsMp4MovieBox();
    virtual ~SrsMp4MovieBox();
};

/**
 * 8.2.2 Movie Header Box (mvhd)
 * ISO_IEC_14496-12-base-format-2012.pdf, page 31
 */
class SrsMp4MovieHeaderBox : public SrsMp4FullBox
{
public:
    // an integer that declares the creation time of the presentation (in seconds since
    // midnight, Jan. 1, 1904, in UTC time)
    uint64_t creation_time;
    // an integer that declares the most recent time the presentation was modified (in
    // seconds since midnight, Jan. 1, 1904, in UTC time)
    uint64_t modification_time;
    // an integer that specifies the time-scale for the entire presentation; this is the number of
    // time units that pass in one second. For example, a time coordinate system that measures time in
    // sixtieths of a second has a time scale of 60.
    uint32_t timescale;
    // an integer that declares length of the presentation (in the indicated timescale). This property
    // is derived from the presentation’s tracks: the value of this field corresponds to the duration of the
    // longest track in the presentation. If the duration cannot be determined then duration is set to all 1s.
    uint64_t duration;
public:
    // a fixed point 16.16 number that indicates the preferred rate to play the presentation; 1.0
    // (0x00010000) is normal forward playback
    uint32_t rate;
    // a fixed point 8.8 number that indicates the preferred playback volume. 1.0 (0x0100) is full volume.
    uint16_t volume;
    uint16_t reserved0;
    uint64_t reserved1;
    // a transformation matrix for the video; (u,v,w) are restricted here to (0,0,1), hex values (0,0,0x40000000).
    int32_t matrix[9];
    uint32_t pre_defined[6];
    // a non-zero integer that indicates a value to use for the track ID of the next track to be
    // added to this presentation. Zero is not a valid track ID value. The value of next_track_ID shall be
    // larger than the largest track-ID in use. If this value is equal to all 1s (32-bit maxint), and a new media
    // track is to be added, then a search must be made in the file for an unused track identifier.
    uint32_t next_track_ID;
public:
    SrsMp4MovieHeaderBox();
    virtual ~SrsMp4MovieHeaderBox();
};

/**
 * 8.3.1 Track Box (trak)
 * ISO_IEC_14496-12-base-format-2012.pdf, page 32
 * This is a container box for a single track of a presentation. A presentation consists of one or more tracks. 
 * Each track is independent of the other tracks in the presentation and carries its own temporal and spatial 
 * information. Each track will contain its associated Media Box.
 */
class SrsMp4TrackBox : public SrsMp4Box
{
public:
    SrsMp4TrackBox();
    virtual ~SrsMp4TrackBox();
};

/**
 * 8.3.2 Track Header Box (tkhd)
 * ISO_IEC_14496-12-base-format-2012.pdf, page 32
 */
class SrsMp4TrackHeaderBox : public SrsMp4FullBox
{
public:
    // an integer that declares the creation time of the presentation (in seconds since
    // midnight, Jan. 1, 1904, in UTC time)
    uint64_t creation_time;
    // an integer that declares the most recent time the presentation was modified (in
    // seconds since midnight, Jan. 1, 1904, in UTC time)
    uint64_t modification_time;
    // an integer that specifies the time-scale for the entire presentation; this is the number of
    // time units that pass in one second. For example, a time coordinate system that measures time in
    // sixtieths of a second has a time scale of 60.
    uint32_t timescale;
    // an integer that uniquely identifies this track over the entire life-time of this presentation.
    // Track IDs are never re-used and cannot be zero.
    uint32_t track_ID;
    uint32_t reserved0;
    // an integer that indicates the duration of this track (in the timescale indicated in the Movie
    // Header Box). The value of this field is equal to the sum of the durations of all of the track’s edits. If
    // there is no edit list, then the duration is the sum of the sample durations, converted into the timescale
    // in the Movie Header Box. If the duration of this track cannot be determined then duration is set to all
    // 1s.
    uint64_t duration;
public:
    uint64_t reserved1;
    // specifies the front-to-back ordering of video tracks; tracks with lower numbers are closer to the
    // viewer. 0 is the normal value, and -1 would be in front of track 0, and so on.
    int16_t layer;
    // an integer that specifies a group or collection of tracks. If this field is 0 there is no
    // information on possible relations to other tracks. If this field is not 0, it should be the same for tracks
    // that contain alternate data for one another and different for tracks belonging to different such groups.
    // Only one track within an alternate group should be played or streamed at any one time, and must be
    // distinguishable from other tracks in the group via attributes such as bitrate, codec, language, packet
    // size etc. A group may have only one member.
    int16_t alternate_group;
    // a fixed 8.8 value specifying the track's relative audio volume. Full volume is 1.0 (0x0100) and
    // is the normal value. Its value is irrelevant for a purely visual track. Tracks may be composed by
    // combining them according to their volume, and then using the overall Movie Header Box volume
    // setting; or more complex audio composition (e.g. MPEG-4 BIFS) may be used.
    int16_t volume;
    uint16_t reserved2;
    // a transformation matrix for the video; (u,v,w) are restricted here to (0,0,1), hex (0,0,0x40000000).
    int32_t matrix[9];
    // the track's visual presentation size as fixed-point 16.16 values. These need
    // not be the same as the pixel dimensions of the images, which is documented in the sample
    // description(s); all images in the sequence are scaled to this size, before any overall transformation of
    // the track represented by the matrix. The pixel dimensions of the images are the default values.
    int32_t width;
    int32_t height;
public:
    SrsMp4TrackHeaderBox();
    virtual ~SrsMp4TrackHeaderBox();
};

/**
 * 8.6.5 Edit Box (edts)
 * ISO_IEC_14496-12-base-format-2012.pdf, page 54
 * An Edit Box maps the presentation time-line to the media time-line as it is stored in the file. 
 * The Edit Box is a container for the edit lists.
 */
class SrsMp4EditBox : public SrsMp4Box
{
public:
    SrsMp4EditBox();
    virtual ~SrsMp4EditBox();
};

/**
 * 8.6.6 Edit List Box
 * ISO_IEC_14496-12-base-format-2012.pdf, page 55
 */
struct SrsMp4ElstEntry
{
public:
    // an integer that specifies the duration of this edit segment in units of the timescale
    // in the Movie Header Box
    uint64_t segment_duration;
    // an integer containing the starting time within the media of this edit segment (in media time
    // scale units, in composition time). If this field is set to –1, it is an empty edit. The last edit in a track
    // shall never be an empty edit. Any difference between the duration in the Movie Header Box, and the
    // track’s duration is expressed as an implicit empty edit at the end.
    int64_t media_time;
public:
    // specifies the relative rate at which to play the media corresponding to this edit segment. If this value is 0,
    // then the edit is specifying a ‘dwell’: the media at media-time is presented for the segment-duration. Otherwise
    // this field shall contain the value 1.
    int16_t media_rate_integer;
    int16_t media_rate_fraction;
public:
    SrsMp4ElstEntry();
};

/**
 * 8.6.6 Edit List Box (elst)
 * ISO_IEC_14496-12-base-format-2012.pdf, page 54
 * This box contains an explicit timeline map. Each entry defines part of the track time-line: by mapping part of 
 * the media time-line, or by indicating ‘empty’ time, or by defining a ‘dwell’, where a single time-point in the 
 * media is held for a period.
 */
class SrsMp4EditListBox : public SrsMp4FullBox
{
public:
    // an integer that gives the number of entries in the following table
    uint32_t entry_count;
    SrsMp4ElstEntry* entries;
public:
    SrsMp4EditListBox();
    virtual ~SrsMp4EditListBox();
};

/**
 * 8.4.1 Media Box (mdia)
 * ISO_IEC_14496-12-base-format-2012.pdf, page 36
 * The media declaration container contains all the objects that declare information about the media data within a
 * track.
 */
class SrsMp4MediaBox : public SrsMp4Box
{
public:
    SrsMp4MediaBox();
    virtual ~SrsMp4MediaBox();
};

/**
 * 8.4.2 Media Header Box (mdhd)
 * ISO_IEC_14496-12-base-format-2012.pdf, page 36
 * The media declaration container contains all the objects that declare information about the media data within a
 * track.
 */
class SrsMp4MediaHeaderBox : public SrsMp4FullBox
{
public:
    // an integer that declares the creation time of the presentation (in seconds since
    // midnight, Jan. 1, 1904, in UTC time)
    uint64_t creation_time;
    // an integer that declares the most recent time the presentation was modified (in
    // seconds since midnight, Jan. 1, 1904, in UTC time)
    uint64_t modification_time;
    // an integer that specifies the time-scale for the entire presentation; this is the number of
    // time units that pass in one second. For example, a time coordinate system that measures time in
    // sixtieths of a second has a time scale of 60.
    uint32_t timescale;
    // an integer that declares length of the presentation (in the indicated timescale). This property
    // is derived from the presentation’s tracks: the value of this field corresponds to the duration of the
    // longest track in the presentation. If the duration cannot be determined then duration is set to all 1s.
    uint64_t duration;
public:
    uint8_t pad:1;
    // the language code for this media. See ISO 639-2/T for the set of three character
    // codes. Each character is packed as the difference between its ASCII value and 0x60. Since the code
    // is confined to being three lower-case letters, these values are strictly positive.
    uint16_t language:15;
    uint16_t pre_defined;
public:
    SrsMp4MediaHeaderBox();
    virtual ~SrsMp4MediaHeaderBox();
};

/**
 * 8.4.3 Handler Reference Box (hdlr)
 * ISO_IEC_14496-12-base-format-2012.pdf, page 37
 * This box within a Media Box declares the process by which the media-data in the track is presented, and thus,
 * the nature of the media in a track. For example, a video track would be handled by a video handler.
 */
class SrsMp4HandlerReferenceBox : public SrsMp4FullBox
{
public:
    uint32_t pre_defined;
    // an integer containing one of the following values, or a value from a derived specification:
    //      ‘vide’, Video track
    //      ‘soun’, Audio track
    uint32_t handler_type;
    uint32_t reserved[3];
    // a null-terminated string in UTF-8 characters which gives a human-readable name for the track
    // type (for debugging and inspection purposes).
    std::string name;
public:
    SrsMp4HandlerReferenceBox();
    virtual ~SrsMp4HandlerReferenceBox();
};

/**
 * 8.4.4 Media Information Box (minf)
 * ISO_IEC_14496-12-base-format-2012.pdf, page 38
 * This box contains all the objects that declare characteristic information of the media in the track.
 */
class SrsMp4MediaInformationBox : public SrsMp4Box
{
public:
    SrsMp4MediaInformationBox();
    virtual ~SrsMp4MediaInformationBox();
};

/**
 * 8.4.5.2 Video Media Header Box (vmhd)
 * ISO_IEC_14496-12-base-format-2012.pdf, page 38
 * The video media header contains general presentation information, independent of the coding, for video 
 * media. Note that the flags field has the value 1.
 */
class SrsMp4VideoMeidaHeaderBox : public SrsMp4FullBox
{
public:
    // a composition mode for this video track, from the following enumerated set,
    // which may be extended by derived specifications:
    //      copy = 0 copy over the existing image
    uint16_t graphicsmode;
    // a set of 3 colour values (red, green, blue) available for use by graphics modes
    uint16_t opcolor[3];
public:
    SrsMp4VideoMeidaHeaderBox();
    virtual ~SrsMp4VideoMeidaHeaderBox();
};

/**
 * 8.4.5.3 Sound Media Header Box (smhd)
 * ISO_IEC_14496-12-base-format-2012.pdf, page 39
 * The sound media header contains general presentation information, independent of the coding, for audio 
 * media. This header is used for all tracks containing audio.
 */
class SrsMp4SoundMeidaHeaderBox : public SrsMp4FullBox
{
public:
    // a fixed-point 8.8 number that places mono audio tracks in a stereo space; 0 is centre (the
    // normal value); full left is -1.0 and full right is 1.0.
    int16_t balance;
    uint16_t reserved;
public:
    SrsMp4SoundMeidaHeaderBox();
    virtual ~SrsMp4SoundMeidaHeaderBox();
};

/**
 * 8.7.1 Data Information Box (dinf)
 * ISO_IEC_14496-12-base-format-2012.pdf, page 56
 * The data information box contains objects that declare the location of the media information in a track.
 */
class SrsMp4DataInformationBox : public SrsMp4Box
{
public:
    SrsMp4DataInformationBox();
    virtual ~SrsMp4DataInformationBox();
};

/**
 * 8.7.2 Data Reference Box
 * ISO_IEC_14496-12-base-format-2012.pdf, page 56
 */
class SrsMp4DataEntryBox : public SrsMp4FullBox
{
public:
    std::string location;
public:
    SrsMp4DataEntryBox();
};

/**
 * 8.7.2 Data Reference Box (url )
 * ISO_IEC_14496-12-base-format-2012.pdf, page 56
 */
class SrsMp4DataEntryUrlBox : public SrsMp4DataEntryBox
{
public:
    SrsMp4DataEntryUrlBox();
};

/**
 * 8.7.2 Data Reference Box (urn )
 * ISO_IEC_14496-12-base-format-2012.pdf, page 56
 */
class SrsMp4DataEntryUrnBox : public SrsMp4DataEntryBox
{
public:
    std::string name;
public:
    SrsMp4DataEntryUrnBox();
};

/**
 * 8.7.2 Data Reference Box (dref)
 * ISO_IEC_14496-12-base-format-2012.pdf, page 56
 * The data reference object contains a table of data references (normally URLs) that declare the location(s) of 
 * the media data used within the presentation. The data reference index in the sample description ties entries 
 * in this table to the samples in the track. A track may be split over several sources in this way.
 */
class SrsMp4DataReferenceBox : public SrsMp4FullBox
{
public:
    // an integer that counts the actual entries
    uint32_t entry_count;
    SrsMp4DataEntryBox* entries;
public:
    SrsMp4DataReferenceBox();
    virtual ~SrsMp4DataReferenceBox();
};

/**
 * 8.5.1 Sample Table Box (stbl)
 * ISO_IEC_14496-12-base-format-2012.pdf, page 40
 * The sample table contains all the time and data indexing of the media samples in a track. Using the tables 
 * here, it is possible to locate samples in time, determine their type (e.g. I-frame or not), and determine their 
 * size, container, and offset into that container.
 */
class SrsMp4SampleTableBox : public SrsMp4Box
{
public:
    SrsMp4SampleTableBox();
    virtual ~SrsMp4SampleTableBox();
};

/**
 * 8.5.2 Sample Description Box
 * ISO_IEC_14496-12-base-format-2012.pdf, page 43
 */
class SrsMp4SampleEntry : public SrsMp4Box
{
public:
    uint8_t reserved[6];
    // an integer that contains the index of the data reference to use to retrieve
    // data associated with samples that use this sample description. Data references are stored in Data
    // Reference Boxes. The index ranges from 1 to the number of data references.
    uint16_t data_reference_index;
public:
    SrsMp4SampleEntry();
    virtual ~SrsMp4SampleEntry();
};

/**
 * 8.5.2 Sample Description Box (avc1)
 * ISO_IEC_14496-12-base-format-2012.pdf, page 44
 */
class SrsMp4VisualSampleEntry : public SrsMp4SampleEntry
{
public:
    uint16_t pre_defined0;
    uint16_t reserved0;
    uint32_t pre_defined1[3];
    // the maximum visual width and height of the stream described by this sample
    // description, in pixels
    uint16_t width;
    uint16_t height;
    uint32_t horizresolution;
    uint32_t vertresolution;
    uint32_t reserved1;
    // how many frames of compressed video are stored in each sample. The default is
    // 1, for one frame per sample; it may be more than 1 for multiple frames per sample
    uint16_t frame_count;
    // a name, for informative purposes. It is formatted in a fixed 32-byte field, with the first
    // byte set to the number of bytes to be displayed, followed by that number of bytes of displayable data,
    // and then padding to complete 32 bytes total (including the size byte). The field may be set to 0.
    char compressorname[32];
    // one of the following values
    //      0x0018 – images are in colour with no alpha
    uint16_t depth;
    int16_t pre_defined2;
public:
    SrsMp4VisualSampleEntry();
    virtual ~SrsMp4VisualSampleEntry();
};

/**
 * 8.5.2 Sample Description Box (mp4a)
 * ISO_IEC_14496-12-base-format-2012.pdf, page 45
 */
class SrsMp4AudioSampleEntry : public SrsMp4SampleEntry
{
public:
    uint32_t reserved0[2];
    uint16_t channelcount;
    uint16_t samplesize;
    uint16_t pre_defined0;
    uint16_t reserved1;
    uint32_t samplerate;
public:
    SrsMp4AudioSampleEntry();
    virtual ~SrsMp4AudioSampleEntry();
};

/**
 * 8.5.2 Sample Description Box (stsd)
 * ISO_IEC_14496-12-base-format-2012.pdf, page 40
 * The sample description table gives detailed information about the coding type used, and any initialization 
 * information needed for that coding.
 */
class SrsMp4SampleDescriptionBox : public SrsMp4FullBox
{
public:
    // an integer that gives the number of entries in the following table
    uint32_t entry_count;
    SrsMp4SampleEntry* entries;
public:
    SrsMp4SampleDescriptionBox();
    virtual ~SrsMp4SampleDescriptionBox();
};

/**
 * 8.6.1.2 Decoding Time to Sample Box (stts)
 * ISO_IEC_14496-12-base-format-2012.pdf, page 48
 */
struct SrsMp4SttsEntry
{
    // an integer that counts the number of consecutive samples that have the given
    // duration.
    uint32_t sample_count;
    // an integer that gives the delta of these samples in the time-scale of the media.
    uint32_t sample_delta;
    // Constructor
    SrsMp4SttsEntry();
};

/**
 * 8.6.1.2 Decoding Time to Sample Box (stts)
 * ISO_IEC_14496-12-base-format-2012.pdf, page 48
 * This box contains a compact version of a table that allows indexing from decoding time to sample number. 
 * Other tables give sample sizes and pointers, from the sample number. Each entry in the table gives the 
 * number of consecutive samples with the same time delta, and the delta of those samples. By adding the 
 * deltas a complete time-to-sample map may be built.
 */
class SrsMp4DecodingTime2SampleBox : public SrsMp4FullBox
{
public:
    // an integer that gives the number of entries in the following table.
    uint32_t entry_count;
    SrsMp4SttsEntry* entries;
public:
    SrsMp4DecodingTime2SampleBox();
    virtual ~SrsMp4DecodingTime2SampleBox();
};


/**
 * 8.6.1.3 Composition Time to Sample Box (ctts)
 * ISO_IEC_14496-12-base-format-2012.pdf, page 49
 */
struct SrsMp4CttsEntry
{
    // an integer that counts the number of consecutive samples that have the given offset.
    uint32_t sample_count;
    // uint32_t for version=0
    // int32_t for version=1
    // an integer that gives the offset between CT and DT, such that CT(n) = DT(n) +
    // CTTS(n).
    int64_t sample_offset;
    // Constructor
    SrsMp4CttsEntry();
};
 
 /**
 * 8.6.1.3 Composition Time to Sample Box (ctts)
 * ISO_IEC_14496-12-base-format-2012.pdf, page 49
 * This box provides the offset between decoding time and composition time. In version 0 of this box the 
 * decoding time must be less than the composition time, and the offsets are expressed as unsigned numbers
 * such that CT(n) = DT(n) + CTTS(n) where CTTS(n) is the (uncompressed) table entry for sample n. In version
 * 1 of this box, the composition timeline and the decoding timeline are still derived from each other, but the
 * offsets are signed. It is recommended that for the computed composition timestamps, there is exactly one with
 * the value 0 (zero).
 */
class SrsMp4CompositionTime2SampleBox : public SrsMp4FullBox
{
public:
    // an integer that gives the number of entries in the following table.
    uint32_t entry_count;
    SrsMp4CttsEntry* entries;
public:
    SrsMp4CompositionTime2SampleBox();
    virtual ~SrsMp4CompositionTime2SampleBox();
};

#endif

