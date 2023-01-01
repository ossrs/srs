//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#ifndef SRS_APP_INGEST_HPP
#define SRS_APP_INGEST_HPP

#include <srs_core.hpp>

#include <vector>

#include <srs_app_st.hpp>
#include <srs_app_reload.hpp>

class SrsFFMPEG;
class SrsConfDirective;
class SrsPithyPrint;

// Ingester ffmpeg object.
class SrsIngesterFFMPEG
{
private:
    std::string vhost;
    std::string id;
    SrsFFMPEG* ffmpeg;
    srs_utime_t starttime;
public:
    SrsIngesterFFMPEG();
    virtual ~SrsIngesterFFMPEG();
public:
    virtual srs_error_t initialize(SrsFFMPEG* ff, std::string v, std::string i);
    // The ingest uri, [vhost]/[ingest id]
    virtual std::string uri();
    // The alive in srs_utime_t.
    virtual srs_utime_t alive();
    virtual bool equals(std::string v, std::string i);
    virtual bool equals(std::string v);
public:
    virtual srs_error_t start();
    virtual void stop();
    virtual srs_error_t cycle();
    // @see SrsFFMPEG.fast_stop().
    virtual void fast_stop();
    virtual void fast_kill();
};

// Ingest file/stream/device,
// encode with FFMPEG(optional),
// push to SRS(or any RTMP server) over RTMP.
class SrsIngester : public ISrsCoroutineHandler, public ISrsReloadHandler
{
private:
    std::vector<SrsIngesterFFMPEG*> ingesters;
private:
    SrsCoroutine* trd;
    SrsPithyPrint* pprint;
    // Whether the ingesters are expired, for example, the listen port changed,
    // all ingesters must be restart.
    bool expired;
    // Whether already disposed.
    bool disposed;
public:
    SrsIngester();
    virtual ~SrsIngester();
public:
    virtual void dispose();
public:
    virtual srs_error_t start();
    virtual void stop();
private:
    // Notify FFMPEG to fast stop.
    virtual void fast_stop();
    // When SRS quit, directly kill FFMPEG after fast stop.
    virtual void fast_kill();
// Interface ISrsReusableThreadHandler.
public:
    virtual srs_error_t cycle();
private:
    virtual srs_error_t do_cycle();
private:
    virtual void clear_engines();
    virtual srs_error_t parse();
    virtual srs_error_t parse_ingesters(SrsConfDirective* vhost);
    virtual srs_error_t parse_engines(SrsConfDirective* vhost, SrsConfDirective* ingest);
    virtual srs_error_t initialize_ffmpeg(SrsFFMPEG* ffmpeg, SrsConfDirective* vhost, SrsConfDirective* ingest, SrsConfDirective* engine);
    virtual void show_ingest_log_message();
// Interface ISrsReloadHandler.
public:
    virtual srs_error_t on_reload_vhost_removed(std::string vhost);
    virtual srs_error_t on_reload_vhost_added(std::string vhost);
    virtual srs_error_t on_reload_ingest_removed(std::string vhost, std::string ingest_id);
    virtual srs_error_t on_reload_ingest_added(std::string vhost, std::string ingest_id);
    virtual srs_error_t on_reload_ingest_updated(std::string vhost, std::string ingest_id);
    virtual srs_error_t on_reload_listen();
};

#endif

