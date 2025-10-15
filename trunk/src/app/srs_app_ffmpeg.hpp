//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_APP_FFMPEG_HPP
#define SRS_APP_FFMPEG_HPP

#include <srs_core.hpp>

#ifdef SRS_FFMPEG_STUB

#include <string>
#include <vector>

class SrsConfDirective;
class SrsPithyPrint;
class ISrsPithyPrint;
class SrsProcess;
class ISrsProcess;
class ISrsAppConfig;

// The ffmpeg interface.
class ISrsFFMPEG
{
public:
    ISrsFFMPEG();
    virtual ~ISrsFFMPEG();

public:
    virtual void append_iparam(std::string iparam) = 0;
    virtual void set_oformat(std::string format) = 0;
    virtual std::string output() = 0;

public:
    virtual srs_error_t initialize(std::string in, std::string out, std::string log) = 0;
    virtual srs_error_t initialize_transcode(SrsConfDirective *engine) = 0;
    virtual srs_error_t initialize_copy() = 0;

public:
    virtual srs_error_t start() = 0;
    virtual srs_error_t cycle() = 0;
    virtual void stop() = 0;

public:
    virtual void fast_stop() = 0;
    virtual void fast_kill() = 0;
};

// A transcode engine: ffmepg, used to transcode a stream to another.
class SrsFFMPEG : public ISrsFFMPEG
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsAppConfig *config_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsProcess *process_;
    std::vector<std::string> params_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    std::string log_file_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    std::string ffmpeg_;
    std::vector<std::string> iparams_;
    std::vector<std::string> perfile_;
    std::string iformat_;
    std::string input_;
    std::vector<std::string> vfilter_;
    std::string vcodec_;
    int vbitrate_;
    double vfps_;
    int vwidth_;
    int vheight_;
    int vthreads_;
    std::string vprofile_;
    std::string vpreset_;
    std::vector<std::string> vparams_;
    std::string acodec_;
    int abitrate_;
    int asample_rate_;
    int achannels_;
    std::vector<std::string> aparams_;
    std::string oformat_;
    std::string output_;

public:
    SrsFFMPEG(std::string ffmpeg_bin);
    virtual ~SrsFFMPEG();

public:
    virtual void append_iparam(std::string iparam);
    virtual void set_oformat(std::string format);
    virtual std::string output();

public:
    virtual srs_error_t initialize(std::string in, std::string out, std::string log);
    virtual srs_error_t initialize_transcode(SrsConfDirective *engine);
    virtual srs_error_t initialize_copy();

public:
    virtual srs_error_t start();
    virtual srs_error_t cycle();
    virtual void stop();

public:
    virtual void fast_stop();
    virtual void fast_kill();
};

#endif

#endif
