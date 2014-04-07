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

#include <srs_app_ingest.hpp>

#ifdef SRS_INGEST

#include <srs_kernel_error.hpp>
#include <srs_app_config.hpp>
#include <srs_kernel_log.hpp>
#include <srs_app_ffmpeg.hpp>
#include <srs_app_pithy_print.hpp>

// when error, ingester sleep for a while and retry.
#define SRS_INGESTER_SLEEP_US (int64_t)(3*1000*1000LL)

SrsIngester::SrsIngester()
{
    // TODO: FIXME: support reload.
    pthread = new SrsThread(this, SRS_INGESTER_SLEEP_US);
    pithy_print = new SrsPithyPrint(SRS_STAGE_INGESTER);
}

SrsIngester::~SrsIngester()
{
    srs_freep(pthread);
    clear_engines();
}

int SrsIngester::start()
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = parse()) != ERROR_SUCCESS) {
        clear_engines();
        ret = ERROR_SUCCESS;
        return ret;
    }
    
    // return for error or no engine.
    if (ffmpegs.empty()) {
        return ret;
    }
    
    // start thread to run all encoding engines.
    if ((ret = pthread->start()) != ERROR_SUCCESS) {
        srs_error("st_thread_create failed. ret=%d", ret);
        return ret;
    }
    
    return ret;
}

int SrsIngester::parse_ingesters(SrsConfDirective* vhost)
{
    int ret = ERROR_SUCCESS;
    
    std::vector<SrsConfDirective*> ingesters;
    _srs_config->get_ingesters(vhost->arg0(), ingesters);
    
    // create engine
    for (int i = 0; i < (int)ingesters.size(); i++) {
        SrsConfDirective* ingest = ingesters[i];
        if (!_srs_config->get_ingest_enabled(ingest)) {
            continue;
        }
        
        if ((ret = parse_engines(vhost, ingest)) != ERROR_SUCCESS) {
            return ret;
        }
    }
    
    return ret;
}

int SrsIngester::parse_engines(SrsConfDirective* vhost, SrsConfDirective* ingest)
{
    int ret = ERROR_SUCCESS;
        
    std::string ffmpeg_bin = _srs_config->get_ingest_ffmpeg(ingest);
    if (ffmpeg_bin.empty()) {
        ret = ERROR_ENCODER_PARSE;
        srs_trace("empty ffmpeg ret=%d", ret);
        return ret;
    }
    
    // get all engines.
    std::vector<SrsConfDirective*> engines;
    _srs_config->get_transcode_engines(ingest, engines);
    if (engines.empty()) {
        SrsFFMPEG* ffmpeg = new SrsFFMPEG(ffmpeg_bin);
        if ((ret = initialize_ffmpeg(ffmpeg, vhost, ingest, NULL)) != ERROR_SUCCESS) {
            srs_freep(ffmpeg);
            if (ret != ERROR_ENCODER_LOOP) {
                srs_error("invalid ingest engine. ret=%d", ret);
            }
            return ret;
        }

        ffmpegs.push_back(ffmpeg);
        return ret;
    }

    // create engine
    for (int i = 0; i < (int)engines.size(); i++) {
        SrsConfDirective* engine = engines[i];
        SrsFFMPEG* ffmpeg = new SrsFFMPEG(ffmpeg_bin);
        if ((ret = initialize_ffmpeg(ffmpeg, vhost, ingest, engine)) != ERROR_SUCCESS) {
            srs_freep(ffmpeg);
            if (ret != ERROR_ENCODER_LOOP) {
                srs_error("invalid ingest engine: %s %s", ingest->arg0().c_str(), engine->arg0().c_str());
            }
            return ret;
        }

        ffmpegs.push_back(ffmpeg);
    }
    
    return ret;
}

void SrsIngester::stop()
{
    pthread->stop();
    clear_engines();
}

int SrsIngester::cycle()
{
    int ret = ERROR_SUCCESS;
    
    std::vector<SrsFFMPEG*>::iterator it;
    for (it = ffmpegs.begin(); it != ffmpegs.end(); ++it) {
        SrsFFMPEG* ffmpeg = *it;
        
        // start all ffmpegs.
        if ((ret = ffmpeg->start()) != ERROR_SUCCESS) {
            srs_error("ingest ffmpeg start failed. ret=%d", ret);
            return ret;
        }

        // check ffmpeg status.
        if ((ret = ffmpeg->cycle()) != ERROR_SUCCESS) {
            srs_error("ingest ffmpeg cycle failed. ret=%d", ret);
            return ret;
        }
    }

    // pithy print
    ingester();
    pithy_print->elapse(SRS_INGESTER_SLEEP_US / 1000);
    
    return ret;
}

void SrsIngester::on_thread_stop()
{
}

void SrsIngester::clear_engines()
{
    std::vector<SrsFFMPEG*>::iterator it;
    
    for (it = ffmpegs.begin(); it != ffmpegs.end(); ++it) {
        SrsFFMPEG* ffmpeg = *it;
        srs_freep(ffmpeg);
    }

    ffmpegs.clear();
}

int SrsIngester::parse()
{
    int ret = ERROR_SUCCESS;
    
    // parse ingesters
    std::vector<SrsConfDirective*> vhosts;
    _srs_config->get_vhosts(vhosts);
    
    for (int i = 0; i < (int)vhosts.size(); i++) {
        SrsConfDirective* vhost = vhosts[i];
        if ((ret = parse_ingesters(vhost)) != ERROR_SUCCESS) {
            return ret;
        }
    }
    
    return ret;
}

int SrsIngester::initialize_ffmpeg(SrsFFMPEG* ffmpeg, SrsConfDirective* vhost, SrsConfDirective* ingest, SrsConfDirective* engine)
{
    int ret = ERROR_SUCCESS;
    
    SrsConfDirective* listen = _srs_config->get_listen();
    srs_assert(listen->args.size() > 0);
    std::string port = listen->arg0();
    
    std::string output = _srs_config->get_engine_output(engine);
    // output stream, to other/self server
    // ie. rtmp://127.0.0.1:1935/live/livestream_sd
    output = srs_string_replace(output, "[vhost]", vhost->arg0());
    output = srs_string_replace(output, "[port]", port);
    if (output.empty()) {
        ret = ERROR_ENCODER_NO_OUTPUT;
        srs_trace("empty ingest output url. ret=%d", ret);
        return ret;
    }
    
    // find the app and stream in rtmp url
    std::string url = output;
    std::string app, stream;
    size_t pos = std::string::npos;
    if ((pos = url.rfind("/")) != std::string::npos) {
        stream = url.substr(pos + 1);
        url = url.substr(0, pos);
    }
    if ((pos = url.rfind("/")) != std::string::npos) {
        app = url.substr(pos + 1);
        url = url.substr(0, pos);
    }
    if ((pos = app.rfind("?")) != std::string::npos) {
        app = app.substr(0, pos);
    }
    
    std::string log_file;
    // write ffmpeg info to log file.
    log_file = _srs_config->get_ffmpeg_log_dir();
    log_file += "/";
    log_file += "ingest";
    log_file += "-";
    log_file += vhost->arg0();
    log_file += "-";
    log_file += app;
    log_file += "-";
    log_file += stream;
    log_file += ".log";

    // stream name: vhost/app/stream for print
    input_stream_name = vhost->arg0();
    input_stream_name += "/";
    input_stream_name += app;
    input_stream_name += "/";
    input_stream_name += stream;
    
    // input
    std::string input_type = _srs_config->get_ingest_input_type(ingest);
    if (input_type.empty()) {
        ret = ERROR_ENCODER_NO_INPUT;
        srs_trace("empty ingest intput type. ret=%d", ret);
        return ret;
    }

    if (input_type == SRS_INGEST_TYPE_FILE) {
        std::string input_url = _srs_config->get_ingest_input_url(ingest);
        if (input_url.empty()) {
            ret = ERROR_ENCODER_NO_INPUT;
            srs_trace("empty ingest intput url. ret=%d", ret);
            return ret;
        }
        
        // for file, set re.
        ffmpeg->set_iparams("-re");
    
        if ((ret = ffmpeg->initialize(input_url, output, log_file)) != ERROR_SUCCESS) {
            return ret;
        }
    } else {
        ret = ERROR_ENCODER_INPUT_TYPE;
        srs_error("invalid ingest type=%s, ret=%d", input_type.c_str(), ret);
    }
    
    if (!engine || !_srs_config->get_engine_enabled(engine)) {
        if ((ret = ffmpeg->initialize_copy()) != ERROR_SUCCESS) {
            return ret;
        }
    } else {
        if ((ret = ffmpeg->initialize_transcode(engine)) != ERROR_SUCCESS) {
            return ret;
        }
    }
    
    return ret;
}

void SrsIngester::ingester()
{
    // reportable
    if (pithy_print->can_print()) {
        // TODO: FIXME: show more info.
        srs_trace("-> time=%"PRId64", ingesters=%d, input=%s", 
            pithy_print->get_age(), (int)ffmpegs.size(), input_stream_name.c_str());
    }
}

#endif
