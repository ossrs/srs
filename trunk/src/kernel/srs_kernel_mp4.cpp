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

#include <string.h>

#include <srs_kernel_error.hpp>

SrsMp4Box::SrsMp4Box()
{
    size = 0;
    type = 0;
}

SrsMp4Box::~SrsMp4Box()
{
}

SrsMp4FullBox::SrsMp4FullBox()
{
    version = 0;
    flags = 0;
}

SrsMp4FullBox::~SrsMp4FullBox()
{
}

SrsMp4FileTypeBox::SrsMp4FileTypeBox()
{
    type = 0x66747970; // 'ftyp'
    nb_compatible_brands = 0;
    compatible_brands = NULL;
    major_brand = minor_version = 0;
}

SrsMp4FileTypeBox::~SrsMp4FileTypeBox()
{
    srs_freepa(compatible_brands);
}

SrsMp4MediaDataBox::SrsMp4MediaDataBox()
{
    type = 0x6d646174; // 'mdat'
    data = NULL;
    nb_data = 0;
}

SrsMp4MediaDataBox::~SrsMp4MediaDataBox()
{
    srs_freepa(data);
}

SrsMp4FreeSpaceBox::SrsMp4FreeSpaceBox()
{
    type = 0x66726565; // ‘free’ or ‘skip’
}

SrsMp4FreeSpaceBox::~SrsMp4FreeSpaceBox()
{
}

SrsMp4MovieBox::SrsMp4MovieBox()
{
    type = 0x6d6f6f76; // 'moov'
}

SrsMp4MovieBox::~SrsMp4MovieBox()
{
}

SrsMp4MovieHeaderBox::SrsMp4MovieHeaderBox()
{
    type = 0x6d766864; // 'mvhd'
    
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

SrsMp4TrackBox::SrsMp4TrackBox()
{
    type = 0x7472616b; // 'trak'
}

SrsMp4TrackBox::~SrsMp4TrackBox()
{
}

SrsMp4TrackHeaderBox::SrsMp4TrackHeaderBox()
{
    type = 0x746b6864; // 'tkhd'
    
    reserved0 = 0;
    reserved1 = 0;
    reserved2 = 0;
    layer = alternate_group = 0;
    volume = 0x0100; // if track_is_audio 0x0100 else 0
    
    int32_t v[] = {0x00010000, 0, 0, 0, 0x00010000, 0, 0, 0, 0x40000000};
    memcpy(matrix, v, 36);
}

SrsMp4TrackHeaderBox::~SrsMp4TrackHeaderBox()
{
}

SrsMp4EditBox::SrsMp4EditBox()
{
    type = 0x65647473; // 'edts'
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
    type = 0x656c7374; // 'elst'
    
    entry_count = 0;
    entries = NULL;
}

SrsMp4EditListBox::~SrsMp4EditListBox()
{
    srs_freepa(entries);
}

SrsMp4MediaBox::SrsMp4MediaBox()
{
    type = 0x6d646961; // 'mdia'
}

SrsMp4MediaBox::~SrsMp4MediaBox()
{
}

SrsMp4MediaHeaderBox::SrsMp4MediaHeaderBox()
{
    type = 0x6d646864; // 'mdhd'
    
    pad = 0;
    pre_defined = 0;
}

SrsMp4MediaHeaderBox::~SrsMp4MediaHeaderBox()
{
}

SrsMp4HandlerReferenceBox::SrsMp4HandlerReferenceBox()
{
    type = 0x68646c72; // 'hdlr'
    
    pre_defined = 0;
    memset(reserved, 0, 12);
}

SrsMp4HandlerReferenceBox::~SrsMp4HandlerReferenceBox()
{
}

SrsMp4MediaInformationBox::SrsMp4MediaInformationBox()
{
    type = 0x6d696e66; // 'minf'
}

SrsMp4MediaInformationBox::~SrsMp4MediaInformationBox()
{
}

SrsMp4VideoMeidaHeaderBox::SrsMp4VideoMeidaHeaderBox()
{
    type = 0x766d6864; // 'vmhd'
    version = 0;
    flags = 1;
    
    graphicsmode = 0;
    memset(opcolor, 0, 6);
}

SrsMp4VideoMeidaHeaderBox::~SrsMp4VideoMeidaHeaderBox()
{
}

SrsMp4SoundMeidaHeaderBox::SrsMp4SoundMeidaHeaderBox()
{
    type = 0x736d6864; // 'smhd'
    
    reserved = balance = 0;
}

SrsMp4SoundMeidaHeaderBox::~SrsMp4SoundMeidaHeaderBox()
{
}

SrsMp4DataInformationBox::SrsMp4DataInformationBox()
{
    type = 0x64696e66; // 'dinf'
}

SrsMp4DataInformationBox::~SrsMp4DataInformationBox()
{
}

SrsMp4DataEntryBox::SrsMp4DataEntryBox()
{
}

SrsMp4DataEntryUrlBox::SrsMp4DataEntryUrlBox()
{
    type = 0x75726c20; // 'url '
}

SrsMp4DataEntryUrnBox::SrsMp4DataEntryUrnBox()
{
    type = 0x75726e20; // 'urn '
}

SrsMp4DataReferenceBox::SrsMp4DataReferenceBox()
{
    type = 0x64726566; // 'dref'
    
    entry_count = 0;
    entries = NULL;
}

SrsMp4DataReferenceBox::~SrsMp4DataReferenceBox()
{
}

SrsMp4SampleTableBox::SrsMp4SampleTableBox()
{
    type = 0x7374626c; // 'stbl'
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

SrsMp4AudioSampleEntry::SrsMp4AudioSampleEntry()
{
    memset(reserved0, 0, 8);
    pre_defined0 = 0;
    reserved1 = 0;
    channelcount = 2;
    samplesize = 16;
}

SrsMp4AudioSampleEntry::~SrsMp4AudioSampleEntry()
{
}

SrsMp4SampleDescriptionBox::SrsMp4SampleDescriptionBox()
{
    type = 0x73747364; // 'stsd'
    
    entry_count = 0;
    entries = NULL;
}

SrsMp4SampleDescriptionBox::~SrsMp4SampleDescriptionBox()
{
    srs_freepa(entries);
}

SrsMp4SttsEntry::SrsMp4SttsEntry()
{
    sample_count = 0;
    sample_delta = 0;
}

SrsMp4DecodingTime2SampleBox::SrsMp4DecodingTime2SampleBox()
{
    type = 0x73747473; // 'stts'
    
    entry_count = 0;
    entries = NULL;
}

SrsMp4DecodingTime2SampleBox::~SrsMp4DecodingTime2SampleBox()
{
    srs_freepa(entries);
}

SrsMp4CttsEntry::SrsMp4CttsEntry()
{
    sample_count = 0;
    sample_offset = 0;
}

SrsMp4CompositionTime2SampleBox::SrsMp4CompositionTime2SampleBox()
{
    type = 0x63747473; // 'ctts'
    
    entry_count = 0;
    entries = NULL;
}

SrsMp4CompositionTime2SampleBox::~SrsMp4CompositionTime2SampleBox()
{
    srs_freepa(entries);
}

SrsMp4SyncSampleBox::SrsMp4SyncSampleBox()
{
    type = 0x73747373; // 'stss'
    
    entry_count = 0;
    sample_numbers = NULL;
}

SrsMp4SyncSampleBox::~SrsMp4SyncSampleBox()
{
    srs_freepa(sample_numbers);
}

SrsMp4StscEntry::SrsMp4StscEntry()
{
    first_chunk = 0;
    samples_per_chunk = 0;
    sample_description_index = 0;
}

SrsMp4Sample2ChunkBox::SrsMp4Sample2ChunkBox()
{
    type = 0x73747363; // 'stsc'
    
    entry_count = 0;
    entries = NULL;
}

SrsMp4Sample2ChunkBox::~SrsMp4Sample2ChunkBox()
{
    srs_freepa(entries);
}

SrsMp4ChunkOffsetBox::SrsMp4ChunkOffsetBox()
{
    type = 0x7374636f; // 'stco'
    
    entry_count = 0;
    entries = NULL;
}

SrsMp4ChunkOffsetBox::~SrsMp4ChunkOffsetBox()
{
    srs_freepa(entries);
}

SrsMp4SampleSizeBox::SrsMp4SampleSizeBox()
{
    type = 0x7374737a; // 'stsz'
    
    sample_size = sample_count = 0;
    entry_sizes = NULL;
}

SrsMp4SampleSizeBox::~SrsMp4SampleSizeBox()
{
    srs_freepa(entry_sizes);
}

SrsMp4Decoder::SrsMp4Decoder()
{
}

SrsMp4Decoder::~SrsMp4Decoder()
{
}

int SrsMp4Decoder::initialize(ISrsReader* r)
{
    srs_assert(r);
    reader = r;
    
    return ERROR_SUCCESS;
}

