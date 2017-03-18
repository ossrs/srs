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

#include <string>
#include <vector>

#include <srs_app_fragment.hpp>

class SrsRequest;
class SrsOriginHub;
class SrsSharedPtrMessage;
class SrsFormat;

/**
 * The FMP4(Fragmented MP4) for DASH streaming.
 */
class SrsFragmentedMp4 : public SrsFragment
{
public:
    SrsFragmentedMp4();
    virtual ~SrsFragmentedMp4();
};

/**
 * The writer to write MPD for DASH.
 */
class SrsMpdWriter
{
private:
    SrsRequest* req;
    int64_t last_update_mpd;
private:
    // The duration of fragment in ms.
    int fragment;
    // The period to update the mpd in ms.
    int update_period;
    // The timeshift buffer depth.
    int timeshit;
    // The base or home dir for dash to write files.
    std::string home;
    // The MPD path template, from which to build the file path.
    std::string mpd_file;
public:
    SrsMpdWriter();
    virtual ~SrsMpdWriter();
public:
    virtual int initialize(SrsRequest* r);
    // Write MPD according to parsed format of stream.
    virtual int write(SrsFormat* format);
};

/**
 * The controller for DASH, control the MPD and FMP4 generating system.
 */
class SrsDashController
{
private:
    SrsRequest* req;
    SrsMpdWriter* mpd;
    std::vector<SrsFragmentedMp4*> fragments;
private:
    std::string home;
    int video_tack_id;
    int audio_track_id;
public:
    SrsDashController();
    virtual ~SrsDashController();
public:
    virtual int initialize(SrsRequest* r);
    virtual int on_audio(SrsSharedPtrMessage* shared_audio, SrsFormat* format);
    virtual int on_video(SrsSharedPtrMessage* shared_video, SrsFormat* format);
private:
    virtual int refresh_mpd(SrsFormat* format);
    virtual int refresh_init_mp4(SrsSharedPtrMessage* msg, SrsFormat* format);
};

/**
 * The MPEG-DASH encoder, transmux RTMP to DASH.
 */
class SrsDash
{
private:
    bool enabled;
private:
    SrsRequest* req;
    SrsOriginHub* hub;
    SrsDashController* controller;
public:
    SrsDash();
    virtual ~SrsDash();
public:
    // Initalize the encoder.
    virtual int initialize(SrsOriginHub* h, SrsRequest* r);
    // When stream start publishing.
    virtual int on_publish();
    // When got an shared audio message.
    virtual int on_audio(SrsSharedPtrMessage* shared_audio, SrsFormat* format);
    // When got an shared video message.
    virtual int on_video(SrsSharedPtrMessage* shared_video, SrsFormat* format);
    // When stream stop publishing.
    virtual void on_unpublish();
};

#endif

