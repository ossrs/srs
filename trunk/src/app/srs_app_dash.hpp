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

#ifndef SRS_APP_DASH_HPP
#define SRS_APP_DASH_HPP

/*
#include <srs_app_dash.hpp>
*/
#include <srs_core.hpp>

class SrsRequest;
class SrsOriginHub;
class SrsSharedPtrMessage;

/**
 * The MPEG-DASH encoder, transmux RTMP to DASH.
 */
class SrsMpegDash
{
private:
    bool enabled;
private:
    SrsRequest* req;
    SrsOriginHub* hub;
public:
    SrsMpegDash();
    virtual ~SrsMpegDash();
public:
    // Initalize the encoder.
    virtual int initialize(SrsOriginHub* h, SrsRequest* r);
    // When stream start publishing.
    virtual int on_publish();
    // When got an shared audio message.
    virtual int on_audio(SrsSharedPtrMessage* shared_audio);
    // When got an shared video message.
    virtual int on_video(SrsSharedPtrMessage* shared_video, bool is_sequence_header);
    // When stream stop publishing.
    virtual void on_unpublish();
};

#endif
