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

#ifndef SRS_APP_FFMPEG_HPP
#define SRS_APP_FFMPEG_HPP

/*
#include <srs_app_ffmpeg.hpp>
*/
#include <srs_core.hpp>

#ifdef SRS_FFMPEG

#include <string>
#include <vector>

class SrsConfDirective;
class SrsRequest;
class SrsPithyPrint;

/**
* a transcode engine: ffmepg,
* used to transcode a stream to another.
*/
class SrsFFMPEG
{
private:
    bool started;
    pid_t pid;
private:
    std::string log_file;
    int log_fd;
private:
    std::string                 ffmpeg;
    std::vector<std::string>     vfilter;
    std::string                 vcodec;
    int                         vbitrate;
    double                         vfps;
    int                         vwidth;
    int                         vheight;
    int                         vthreads;
    std::string                 vprofile;
    std::string                 vpreset;
    std::vector<std::string>     vparams;
    std::string                 acodec;
    int                         abitrate;
    int                         asample_rate;
    int                         achannels;
    std::vector<std::string>     aparams;
    std::string                 output;
    std::string                 input;
public:
    SrsFFMPEG(std::string ffmpeg_bin);
    virtual ~SrsFFMPEG();
public:
    virtual int initialize(SrsRequest* req, SrsConfDirective* engine);
    virtual int start();
    virtual int cycle();
    virtual void stop();
};

#endif

#endif
