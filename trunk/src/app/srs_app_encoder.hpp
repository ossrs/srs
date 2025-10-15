//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_APP_ENCODER_HPP
#define SRS_APP_ENCODER_HPP

#include <srs_core.hpp>

#include <string>
#include <vector>

#include <srs_app_st.hpp>

class SrsConfDirective;
class ISrsRequest;
class SrsPithyPrint;
class ISrsPithyPrint;
class SrsFFMPEG;
class ISrsFFMPEG;
class ISrsAppConfig;
class ISrsAppFactory;

// The encoder interface.
class ISrsMediaEncoder
{
public:
    ISrsMediaEncoder();
    virtual ~ISrsMediaEncoder();

public:
    virtual srs_error_t on_publish(ISrsRequest *req) = 0;
    virtual void on_unpublish() = 0;
    // Interface ISrsReusableThreadHandler.
public:
    virtual srs_error_t cycle() = 0;
};

// The encoder for a stream, may use multiple
// ffmpegs to transcode the specified stream.
class SrsEncoder : public ISrsCoroutineHandler, public ISrsMediaEncoder
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsAppConfig *config_;
    ISrsAppFactory *app_factory_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    std::string input_stream_name_;
    std::vector<ISrsFFMPEG *> ffmpegs_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsCoroutine *trd_;
    ISrsPithyPrint *pprint_;

public:
    SrsEncoder();
    virtual ~SrsEncoder();

public:
    virtual srs_error_t on_publish(ISrsRequest *req);
    virtual void on_unpublish();
    // Interface ISrsReusableThreadHandler.
public:
    virtual srs_error_t cycle();

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t do_cycle();

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual void clear_engines();
    virtual ISrsFFMPEG *at(int index);
    virtual srs_error_t parse_scope_engines(ISrsRequest *req);
    virtual srs_error_t parse_ffmpeg(ISrsRequest *req, SrsConfDirective *conf);
    virtual srs_error_t initialize_ffmpeg(ISrsFFMPEG *ffmpeg, ISrsRequest *req, SrsConfDirective *engine);
    virtual void show_encode_log_message();
};

#endif
