/*
The MIT License (MIT)

Copyright (c) 2013-2015 winlin

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

#ifndef SRS_KERNEL_AAC_HPP
#define SRS_KERNEL_AAC_HPP

/*
#include <srs_kernel_aac.hpp>
*/
#include <srs_core.hpp>

#include <string>

class SrsStream;
class SrsFileWriter;
class SrsFileReader;

/**
* encode data to flv file.
*/
class SrsAacEncoder
{
private:
    SrsFileWriter* _fs;
private:
    SrsStream* tag_stream;
public:
    SrsAacEncoder();
    virtual ~SrsAacEncoder();
public:
    /**
    * initialize the underlayer file stream.
    * @remark user can initialize multiple times to encode multiple flv files.
    * @remark, user must free the fs, flv encoder never close/free it.
    */
    virtual int initialize(SrsFileWriter* fs);
public:
    /**
    * write flv header.
    * write following:
    *   1. E.2 The FLV header
    *   2. PreviousTagSize0 UI32 Always 0
    * that is, 9+4=13bytes.
    */
    virtual int write_header();
    virtual int write_header(char flv_header[9]);
    /**
    * write flv metadata. 
    * @param type, the type of data, or other message type.
    * @param data, the amf0 metadata which serialize from:
    *   AMF0 string: onMetaData,
    *   AMF0 object: the metadata object.
    * @remark assert data is not NULL.
    */
    virtual int write_metadata(char type, char* data, int size);
    /**
    * write audio/video packet.
    * @remark assert data is not NULL.
    */
    virtual int write_audio(int64_t timestamp, char* data, int size);
    virtual int write_video(int64_t timestamp, char* data, int size);
public:
    /**
    * get the tag size,
    * including the tag header, body, and 4bytes previous tag size.
    * @remark assert data_size is not negative.
    */
    static int size_tag(int data_size);
private:
    virtual int write_tag(char* header, int header_size, char* tag, int tag_size);
};

#endif

