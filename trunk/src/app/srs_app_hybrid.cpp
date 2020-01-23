/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2020 Winlin
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

#include <srs_app_hybrid.hpp>

#include <srs_app_server.hpp>
#include <srs_app_config.hpp>

SrsHybridServer::SrsHybridServer()
{
    srs = new SrsServer();
}

SrsHybridServer::~SrsHybridServer()
{
    srs_freep(srs);
}

srs_error_t SrsHybridServer::initialize()
{
    srs_error_t err = srs_success;

    // Initialize the whole system, set hooks to handle server level events.
    if ((err = srs->initialize(NULL)) != srs_success) {
        return srs_error_wrap(err, "server initialize");
    }

    if ((err = srs->initialize_st()) != srs_success) {
        return srs_error_wrap(err, "initialize st");
    }

#ifdef SRS_AUTO_SRT
    if(_srs_config->get_srt_enabled()) {
        srs_trace("srt server is enabled...");
        unsigned short srt_port = _srs_config->get_srt_listen_port();
        srs_trace("srt server listen port:%d", srt_port);
        err = srt2rtmp::get_instance()->init();
        if (err != srs_success) {
            srs_error_wrap(err, "srt start srt2rtmp error");
            return err;
        }

        srt_ptr = std::make_shared<srt_server>(srt_port);
        if (!srt_ptr) {
            srs_error_wrap(err, "srt listen %d", srt_port);
        }
    } else {
        srs_trace("srt server is disabled...");
    }
#endif

    return err;
}

srs_error_t SrsHybridServer::run()
{
    srs_error_t err = srs_success;

    if ((err = srs->initialize_signal()) != srs_success) {
        return srs_error_wrap(err, "initialize signal");
    }

    if ((err = srs->acquire_pid_file()) != srs_success) {
        return srs_error_wrap(err, "acquire pid file");
    }

    if ((err = srs->listen()) != srs_success) {
        return srs_error_wrap(err, "listen");
    }

    if ((err = srs->register_signal()) != srs_success) {
        return srs_error_wrap(err, "register signal");
    }

    if ((err = srs->http_handle()) != srs_success) {
        return srs_error_wrap(err, "http handle");
    }

    if ((err = srs->ingest()) != srs_success) {
        return srs_error_wrap(err, "ingest");
    }

#ifdef SRS_AUTO_SRT
    if(_srs_config->get_srt_enabled()) {
        srt_ptr->start();
    }
#endif

    if ((err = srs->cycle()) != srs_success) {
        return srs_error_wrap(err, "main cycle");
    }

    return err;
}

