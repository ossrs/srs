/*
The MIT License (MIT)

Copyright (c) 2013-2014 winlin

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

#ifndef SRS_APP_DVR_HPP
#define SRS_APP_DVR_HPP

/*
#include <srs_app_dvr.hpp>
*/
#include <srs_core.hpp>

#ifdef SRS_AUTO_DVR

class SrsSource;
class SrsRequest;
class SrsStream;
class SrsOnMetaDataPacket;
class SrsSharedPtrMessage;

/**
* file stream to read/write file.
*/
class SrsFileStream
{
private:
    std::string _file;
    int fd;
public:
    SrsFileStream();
    virtual ~SrsFileStream();
public:
    virtual int open(std::string file);
    virtual int close();
public:
    /**
    * @param pnread, return the read size. NULL to ignore.
    */
    virtual int read(void* buf, size_t count, ssize_t* pnread);
    /**
    * @param pnwrite, return the write size. NULL to ignore.
    */
    virtual int write(void* buf, size_t count, ssize_t* pnwrite);
};

/**
* encode data to flv file.
*/
class SrsFlvEncoder
{
private:
    SrsFileStream* _fs;
private:
    SrsStream* tag_stream;
public:
    SrsFlvEncoder();
    virtual ~SrsFlvEncoder();
public:
    virtual int initialize(SrsFileStream* fs);
public:
    /**
    * write flv header.
    * user can invoke this method multiple times,
    * for example, when get audio/video sequence header.
    * 
    * write following:
    *   1. E.2 The FLV header
    *   2. PreviousTagSize0 UI32 Always 0
    * that is, 9+4=13bytes.
    */
    virtual int write_header();
    virtual int write_metadata(char* data, int size);
    virtual int write_audio(int32_t timestamp, char* data, int size);
    virtual int write_video(int32_t timestamp, char* data, int size);
private:
    virtual int write_tag(char* header, int header_size, char* data, int size);
};

/**
* dvr(digital video recorder) to record RTMP stream to flv file.
* TODO: FIXME: add utest for it.
*/
class SrsDvr
{
private:
    SrsSource* _source;
private:
    bool dvr_enabled;
    SrsFileStream* fs;
    SrsFlvEncoder* enc;
public:
    SrsDvr(SrsSource* source);
    virtual ~SrsDvr();
public:
    /**
    * publish stream event, 
    * when encoder start to publish RTMP stream.
    */
    virtual int on_publish(SrsRequest* req);
    /**
    * the unpublish event.,
    * when encoder stop(unpublish) to publish RTMP stream.
    */
    virtual void on_unpublish();
    /**
    * get some information from metadata, it's optinal.
    */
    virtual int on_meta_data(SrsOnMetaDataPacket* metadata);
    /**
    * mux the audio packets to dvr.
    */
    virtual int on_audio(SrsSharedPtrMessage* audio);
    /**
    * mux the video packets to dvr.
    */
    virtual int on_video(SrsSharedPtrMessage* video);
};

#endif

#endif