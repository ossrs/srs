//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_APP_INGEST_HPP
#define SRS_APP_INGEST_HPP

#include <srs_core.hpp>

#include <vector>

#include <srs_app_reload.hpp>
#include <srs_app_st.hpp>

class SrsFFMPEG;
class ISrsFFMPEG;
class SrsConfDirective;
class SrsPithyPrint;
class ISrsPithyPrint;
class ISrsAppFactory;
class ISrsAppConfig;
class ISrsRand;

class ISrsIngesterFFMPEG
{
public:
    ISrsIngesterFFMPEG();
    virtual ~ISrsIngesterFFMPEG();

public:
    virtual srs_error_t initialize(ISrsFFMPEG *ff, std::string v, std::string i) = 0;
    virtual std::string uri() = 0;
    virtual srs_utime_t alive() = 0;
    virtual bool equals(std::string v, std::string i) = 0;
    virtual bool equals(std::string v) = 0;

public:
    virtual srs_error_t start() = 0;
    virtual void stop() = 0;
    virtual srs_error_t cycle() = 0;
    virtual void fast_stop() = 0;
    virtual void fast_kill() = 0;
};

class SrsIngesterFFMPEG : public ISrsIngesterFFMPEG
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    std::string vhost_;
    std::string id_;
    ISrsFFMPEG *ffmpeg_;
    srs_utime_t starttime_;

public:
    SrsIngesterFFMPEG();
    virtual ~SrsIngesterFFMPEG();

public:
    virtual srs_error_t initialize(ISrsFFMPEG *ff, std::string v, std::string i);
    virtual std::string uri();
    virtual srs_utime_t alive();
    virtual bool equals(std::string v, std::string i);
    virtual bool equals(std::string v);

public:
    virtual srs_error_t start();
    virtual void stop();
    virtual srs_error_t cycle();
    virtual void fast_stop();
    virtual void fast_kill();
};

class ISrsIngester
{
public:
    ISrsIngester();
    virtual ~ISrsIngester();

public:
    virtual void assemble() = 0;
    virtual void dispose() = 0;
    virtual srs_error_t start() = 0;
    virtual void stop() = 0;
};

// Pulls a file or stream with FFmpeg, transcoding it when an engine is enabled, and pushes it
// over RTMP to SRS or any other RTMP server.
class SrsIngester : public ISrsIngester, public ISrsCoroutineHandler
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsAppFactory *app_factory_;
    ISrsAppConfig *config_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    std::vector<ISrsIngesterFFMPEG *> ingesters_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsCoroutine *trd_;
    ISrsPithyPrint *pprint_;
    ISrsRand *rand_;
    bool expired_;
    bool disposed_;

public:
    SrsIngester();
    void assemble(); // Creates the members, to avoid calling functions in the constructor.
    virtual ~SrsIngester();

public:
    virtual void dispose();

public:
    virtual srs_error_t start();
    virtual void stop();

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual void fast_stop();
    virtual void fast_kill();

public:
    virtual srs_error_t cycle();

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t do_cycle();

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual void clear_engines();
    virtual srs_error_t parse();
    virtual srs_error_t parse_ingesters(SrsConfDirective *vhost);
    virtual srs_error_t parse_engines(SrsConfDirective *vhost, SrsConfDirective *ingest);
    virtual srs_error_t initialize_ffmpeg(ISrsFFMPEG *ffmpeg, SrsConfDirective *vhost, SrsConfDirective *ingest, SrsConfDirective *engine);
    virtual void show_ingest_log_message();
};

#endif
