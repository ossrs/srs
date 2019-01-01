/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2019 Winlin
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef SRS_APP_HDS_HPP
#define SRS_APP_HDS_HPP

#include <srs_core.hpp>

#ifdef SRS_AUTO_HDS

#include <list>

class SrsRequest;
class SrsSharedPtrMessage;
class SrsHdsFragment;
class SrsSource;

class SrsHds
{
public:
    SrsHds();
    virtual ~SrsHds();
    
    srs_error_t on_publish(SrsRequest* req);
    srs_error_t on_unpublish();
    
    srs_error_t on_video(SrsSharedPtrMessage* msg);
    srs_error_t on_audio(SrsSharedPtrMessage* msg);
    
private:
    srs_error_t flush_mainfest();
    srs_error_t flush_bootstrap();
    void adjust_windows();
    
private:
    std::list<SrsHdsFragment *> fragments;
    SrsHdsFragment *currentSegment;
    int fragment_index;
    SrsSharedPtrMessage *video_sh;
    SrsSharedPtrMessage *audio_sh;
    
    SrsRequest *hds_req;
    bool hds_enabled;
};

#endif

#endif
