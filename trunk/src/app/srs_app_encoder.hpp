//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#ifndef SRS_APP_ENCODER_HPP
#define SRS_APP_ENCODER_HPP

#include <srs_core.hpp>

#include <string>
#include <vector>

#include <srs_app_st.hpp>

class SrsConfDirective;
class SrsRequest;
class SrsPithyPrint;
class SrsFFMPEG;

// The encoder for a stream, may use multiple
// ffmpegs to transcode the specified stream.
class SrsEncoder : public ISrsCoroutineHandler
{
private:
    std::string input_stream_name;
    std::vector<SrsFFMPEG*> ffmpegs;
private:
    SrsCoroutine* trd;
    SrsPithyPrint* pprint;
public:
    SrsEncoder();
    virtual ~SrsEncoder();
public:
    virtual srs_error_t on_publish(SrsRequest* req);
    virtual void on_unpublish();
// Interface ISrsReusableThreadHandler.
public:
    virtual srs_error_t cycle();
private:
    virtual srs_error_t do_cycle();
private:
    virtual void clear_engines();
    virtual SrsFFMPEG* at(int index);
    virtual srs_error_t parse_scope_engines(SrsRequest* req);
    virtual srs_error_t parse_ffmpeg(SrsRequest* req, SrsConfDirective* conf);
    virtual srs_error_t initialize_ffmpeg(SrsFFMPEG* ffmpeg, SrsRequest* req, SrsConfDirective* engine);
    virtual void show_encode_log_message();
};

#endif

