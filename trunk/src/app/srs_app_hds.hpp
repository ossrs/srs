/*
The MIT License (MIT)

Copyright (c) 2013-2015 wenjiegit
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

#ifndef SRS_APP_HDS_HPP
#define SRS_APP_HDS_HPP

#include <srs_core.hpp>

#include <list>

class SrsRequest;
class SrsSharedPtrMessage;
class SrsHdsFragment;
class SrsSource;

class SrsHds
{
public:
    SrsHds(SrsSource* s);
    ~SrsHds();

    int on_publish(SrsRequest* req);
    int on_unpublish();

    int on_video(SrsSharedPtrMessage* msg);
    int on_audio(SrsSharedPtrMessage* msg);

private:
    int flush_mainfest();
    int flush_bootstrap();
    void adjust_windows();

private:
    std::list<SrsHdsFragment *> fragments;
    SrsHdsFragment *currentSegment;
    SrsSource *source;
    int fragment_index;
    SrsSharedPtrMessage *video_sh;
    SrsSharedPtrMessage *audio_sh;

    SrsRequest *hds_req;
};

#endif // SRS_APP_HDS_HPP
