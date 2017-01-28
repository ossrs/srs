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
}

SrsMp4MovieHeaderBox::~SrsMp4MovieHeaderBox()
{
}

