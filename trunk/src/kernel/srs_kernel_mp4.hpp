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
 * 4.3 File Type Box
 * ISO_IEC_14496-12-base-format-2012.pdf, page 17
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
 * 8.2.1 Movie Box
 * ISO_IEC_14496-12-base-format-2012.pdf, page 31
 */
class SrsMp4MovieBox : public SrsMp4Box
{
public:
    SrsMp4MovieBox();
    virtual ~SrsMp4MovieBox();
};

/**
 * 8.2.2 Movie Header Box
 * ISO_IEC_14496-12-base-format-2012.pdf, page 31
 */
class SrsMp4MovieHeaderBox : public SrsMp4Box
{
public:
    SrsMp4MovieHeaderBox();
    virtual ~SrsMp4MovieHeaderBox();
};

#endif

