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
    /**
    * initialize the underlayer file stream,
    * user can initialize multiple times to encode multiple flv files.
    */
    virtual int initialize(SrsFileStream* fs);
public:
    /**
    * write flv header.
    * write following:
    *   1. E.2 The FLV header
    *   2. PreviousTagSize0 UI32 Always 0
    * that is, 9+4=13bytes.
    */
    virtual int write_header();
    /**
    * write flv metadata. 
    * serialize from:
    *   AMF0 string: onMetaData,
    *   AMF0 object: the metadata object.
    */
    virtual int write_metadata(char* data, int size);
    /**
    * write audio/video packet.
    */
    virtual int write_audio(int32_t timestamp, char* data, int size);
    virtual int write_video(int32_t timestamp, char* data, int size);
private:
    virtual int write_tag(char* header, int header_size, char* tag, int tag_size);
};

/**
* the plan for dvr.
* use to control the following dvr params:
* 1. filename: the filename for record file.
* 2. reap flv: when to reap the flv and start new piece.
*/
class SrsDvrPlan
{
protected:
    /**
    * the underlayer dvr stream.
    * if close, the flv is reap and closed.
    * if open, new flv file is crote.
    */
    SrsFileStream* fs;
    SrsFlvEncoder* enc;
    bool dvr_enabled;
    SrsSource* _source;
public:
    SrsDvrPlan();
    virtual ~SrsDvrPlan();
public:
    virtual int initialize(SrsSource* source, SrsRequest* req);
    virtual int on_publish(SrsRequest* req) = 0;
    virtual void on_unpublish() = 0;
    virtual int on_meta_data(SrsOnMetaDataPacket* metadata);
    virtual int on_audio(SrsSharedPtrMessage* audio);
    virtual int on_video(SrsSharedPtrMessage* video);
protected:
    virtual int flv_open(std::string stream, std::string path);
    virtual int flv_close();
public:
    static SrsDvrPlan* create_plan(std::string vhost);
};

/**
* session plan: reap flv when session complete(unpublish)
*/
class SrsDvrSessionPlan : public SrsDvrPlan
{
public:
    SrsDvrSessionPlan();
    virtual ~SrsDvrSessionPlan();
public:
    virtual int on_publish(SrsRequest* req);
    virtual void on_unpublish();
};

/**
* segment plan: reap flv when duration exceed.
*/
class SrsDvrSegmentPlan : public SrsDvrPlan
{
private:
    int64_t duration;
    int64_t starttime;
    // in config, in ms
    int segment_duration;
public:
    SrsDvrSegmentPlan();
    virtual ~SrsDvrSegmentPlan();
public:
    virtual int initialize(SrsSource* source, SrsRequest* req);
    virtual int on_publish(SrsRequest* req);
    virtual void on_unpublish();
    virtual int on_audio(SrsSharedPtrMessage* audio);
    virtual int on_video(SrsSharedPtrMessage* video);
private:
    virtual int update_duration(SrsSharedPtrMessage* msg);
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
    SrsDvrPlan* plan;
public:
    SrsDvr(SrsSource* source);
    virtual ~SrsDvr();
public:
    /**
    * initialize dvr, create dvr plan.
    * when system initialize(encoder publish at first time, or reload),
    * initialize the dvr will reinitialize the plan, the whole dvr framework.
    */
    virtual int initialize(SrsRequest* req);
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