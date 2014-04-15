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

#ifndef SRS_APP_FLV_HPP
#define SRS_APP_FLV_HPP

/*
#include <srs_app_flv.hpp>
*/
#include <srs_core.hpp>

#ifdef SRS_AUTO_DVR

class SrsSource;
class SrsRequest;
class SrsAmf0Object;
class SrsSharedPtrMessage;

/**
* dvr(digital video recorder) to record RTMP stream to flv file.
* TODO: FIXME: add utest for it.
*/
class SrsDvr
{
private:
    SrsSource* _source;
public:
    SrsDvr(SrsSource* source);
    virtual ~SrsDvr();
public:
    /**
    * publish stream event, continue to write the m3u8,
    * for the muxer object not destroyed.
    */
    virtual int on_publish(SrsRequest* req);
    /**
    * the unpublish event, only close the muxer, donot destroy the 
    * muxer, for when we continue to publish, the m3u8 will continue.
    */
    virtual void on_unpublish();
    /**
    * get some information from metadata, it's optinal.
    */
    virtual int on_meta_data(SrsAmf0Object* metadata);
    /**
    * mux the audio packets to ts.
    */
    virtual int on_audio(SrsSharedPtrMessage* audio);
    /**
    * mux the video packets to ts.
    */
    virtual int on_video(SrsSharedPtrMessage* video);
};

#endif

#endif