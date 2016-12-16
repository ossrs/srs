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

#include <srs_app_encoder.hpp>

#include <algorithm>
using namespace std;

#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_app_config.hpp>
#include <srs_rtmp_stack.hpp>
#include <srs_app_pithy_print.hpp>
#include <srs_app_ffmpeg.hpp>
#include <srs_kernel_utility.hpp>

#ifdef SRS_AUTO_TRANSCODE

// when error, encoder sleep for a while and retry.
#define SRS_RTMP_ENCODER_SLEEP_US (int64_t)(3*1000*1000LL)

// for encoder to detect the dead loop
static std::vector<std::string> _transcoded_url;

SrsEncoder::SrsEncoder()
{
    pthread = new SrsReusableThread("encoder", this, SRS_RTMP_ENCODER_SLEEP_US);
    pprint = SrsPithyPrint::create_encoder();
}

SrsEncoder::~SrsEncoder()
{
    on_unpublish();
    
    srs_freep(pthread);
    srs_freep(pprint);
}

int SrsEncoder::on_publish(SrsRequest* req)
{
    int ret = ERROR_SUCCESS;

    // parse the transcode engines for vhost and app and stream.
    ret = parse_scope_engines(req);
    
    // ignore the loop encoder
    // if got a loop, donot transcode the whole stream.
    if (ret == ERROR_ENCODER_LOOP) {
        clear_engines();
        ret = ERROR_SUCCESS;
    }
    
    // return for error or no engine.
    if (ret != ERROR_SUCCESS || ffmpegs.empty()) {
        return ret;
    }
    
    // start thread to run all encoding engines.
    if ((ret = pthread->start()) != ERROR_SUCCESS) {
        srs_error("st_thread_create failed. ret=%d", ret);
        return ret;
    }
    srs_trace("encoder thread cid=%d, current_cid=%d", pthread->cid(), _srs_context->get_id());
    
    return ret;
}

void SrsEncoder::on_unpublish()
{
    pthread->stop();
    clear_engines();
}

int SrsEncoder::cycle()
{
    int ret = ERROR_SUCCESS;
    
    std::vector<SrsFFMPEG*>::iterator it;
    for (it = ffmpegs.begin(); it != ffmpegs.end(); ++it) {
        SrsFFMPEG* ffmpeg = *it;
        
        // start all ffmpegs.
        if ((ret = ffmpeg->start()) != ERROR_SUCCESS) {
            srs_error("transcode ffmpeg start failed. ret=%d", ret);
            return ret;
        }

        // check ffmpeg status.
        if ((ret = ffmpeg->cycle()) != ERROR_SUCCESS) {
            srs_error("transcode ffmpeg cycle failed. ret=%d", ret);
            return ret;
        }
    }

    // pithy print
    show_encode_log_message();
    
    return ret;
}

void SrsEncoder::on_thread_stop()
{
    // kill ffmpeg when finished and it alive
    std::vector<SrsFFMPEG*>::iterator it;

    for (it = ffmpegs.begin(); it != ffmpegs.end(); ++it) {
        SrsFFMPEG* ffmpeg = *it;
        ffmpeg->stop();
    }
}

void SrsEncoder::clear_engines()
{
    std::vector<SrsFFMPEG*>::iterator it;
    
    for (it = ffmpegs.begin(); it != ffmpegs.end(); ++it) {
        SrsFFMPEG* ffmpeg = *it;
    
        std::string output = ffmpeg->output();
        
        std::vector<std::string>::iterator tu_it;
        tu_it = std::find(_transcoded_url.begin(), _transcoded_url.end(), output);
        if (tu_it != _transcoded_url.end()) {
            _transcoded_url.erase(tu_it);
        }
        
        srs_freep(ffmpeg);
    }

    ffmpegs.clear();
}

SrsFFMPEG* SrsEncoder::at(int index)
{
    return ffmpegs[index];
}

int SrsEncoder::parse_scope_engines(SrsRequest* req)
{
    int ret = ERROR_SUCCESS;
    
    // parse all transcode engines.
    SrsConfDirective* conf = NULL;
    
    // parse vhost scope engines
    std::string scope = "";
    if ((conf = _srs_config->get_transcode(req->vhost, scope)) != NULL) {
        if ((ret = parse_ffmpeg(req, conf)) != ERROR_SUCCESS) {
            if (ret != ERROR_ENCODER_LOOP) {
                srs_error("parse vhost scope=%s transcode engines failed. "
                    "ret=%d", scope.c_str(), ret);
            }
            return ret;
        }
    }
    // parse app scope engines
    scope = req->app;
    if ((conf = _srs_config->get_transcode(req->vhost, scope)) != NULL) {
        if ((ret = parse_ffmpeg(req, conf)) != ERROR_SUCCESS) {
            if (ret != ERROR_ENCODER_LOOP) {
                srs_error("parse app scope=%s transcode engines failed. "
                    "ret=%d", scope.c_str(), ret);
            }
            return ret;
        }
    }
    // parse stream scope engines
    scope += "/";
    scope += req->stream;
    if ((conf = _srs_config->get_transcode(req->vhost, scope)) != NULL) {
        if ((ret = parse_ffmpeg(req, conf)) != ERROR_SUCCESS) {
            if (ret != ERROR_ENCODER_LOOP) {
                srs_error("parse stream scope=%s transcode engines failed. "
                    "ret=%d", scope.c_str(), ret);
            }
            return ret;
        }
    }
    
    return ret;
}

int SrsEncoder::parse_ffmpeg(SrsRequest* req, SrsConfDirective* conf)
{
    int ret = ERROR_SUCCESS;
    
    srs_assert(conf);
    
    // enabled
    if (!_srs_config->get_transcode_enabled(conf)) {
        srs_trace("ignore the disabled transcode: %s", conf->arg0().c_str());
        return ret;
    }
    
    // ffmpeg
    std::string ffmpeg_bin = _srs_config->get_transcode_ffmpeg(conf);
    if (ffmpeg_bin.empty()) {
        srs_trace("ignore the empty ffmpeg transcode: %s", 
            conf->arg0().c_str());
        return ret;
    }
    
    // get all engines.
    std::vector<SrsConfDirective*> engines = _srs_config->get_transcode_engines(conf);
    if (engines.empty()) {
        srs_trace("ignore the empty transcode engine: %s", 
            conf->arg0().c_str());
        return ret;
    }
    
    // create engine
    for (int i = 0; i < (int)engines.size(); i++) {
        SrsConfDirective* engine = engines[i];
        if (!_srs_config->get_engine_enabled(engine)) {
            srs_trace("ignore the diabled transcode engine: %s %s", 
                conf->arg0().c_str(), engine->arg0().c_str());
            continue;
        }
        
        SrsFFMPEG* ffmpeg = new SrsFFMPEG(ffmpeg_bin);
        if ((ret = initialize_ffmpeg(ffmpeg, req, engine)) != ERROR_SUCCESS) {
            srs_freep(ffmpeg);
            if (ret != ERROR_ENCODER_LOOP) {
                srs_error("invalid transcode engine: %s %s", conf->arg0().c_str(), engine->arg0().c_str());
            }
            return ret;
        }

        ffmpegs.push_back(ffmpeg);
    }
    
    return ret;
}

int SrsEncoder::initialize_ffmpeg(SrsFFMPEG* ffmpeg, SrsRequest* req, SrsConfDirective* engine)
{
    int ret = ERROR_SUCCESS;

    std::string input;
    // input stream, from local.
    // ie. rtmp://localhost:1935/live/livestream
    input = "rtmp://";
    input += SRS_CONSTS_LOCALHOST;
    input += ":";
    input += srs_int2str(req->port);
    input += "/";
    input += req->app;
    input += "?vhost=";
    input += req->vhost;
    input += "/";
    input += req->stream;

    // stream name: vhost/app/stream for print
    input_stream_name = req->vhost;
    input_stream_name += "/";
    input_stream_name += req->app;
    input_stream_name += "/";
    input_stream_name += req->stream;
    
    std::string output = _srs_config->get_engine_output(engine);
    // output stream, to other/self server
    // ie. rtmp://localhost:1935/live/livestream_sd
    output = srs_string_replace(output, "[vhost]", req->vhost);
    output = srs_string_replace(output, "[port]", srs_int2str(req->port));
    output = srs_string_replace(output, "[app]", req->app);
    output = srs_string_replace(output, "[stream]", req->stream);
    output = srs_string_replace(output, "[engine]", engine->arg0());
    
    std::string log_file = SRS_CONSTS_NULL_FILE; // disabled
    // write ffmpeg info to log file.
    if (_srs_config->get_ffmpeg_log_enabled()) {
        log_file = _srs_config->get_ffmpeg_log_dir();
        log_file += "/";
        log_file += "ffmpeg-encoder";
        log_file += "-";
        log_file += req->vhost;
        log_file += "-";
        log_file += req->app;
        log_file += "-";
        log_file += req->stream;
        log_file += ".log";
    }

    // important: loop check, donot transcode again.
    std::vector<std::string>::iterator it;
    it = std::find(_transcoded_url.begin(), _transcoded_url.end(), input);
    if (it != _transcoded_url.end()) {
        ret = ERROR_ENCODER_LOOP;
        srs_trace("detect a loop cycle, input=%s, output=%s, ignore it. ret=%d",
            input.c_str(), output.c_str(), ret);
        return ret;
    }
    _transcoded_url.push_back(output);
    
    if ((ret = ffmpeg->initialize(input, output, log_file)) != ERROR_SUCCESS) {
        return ret;
    }
    if ((ret = ffmpeg->initialize_transcode(engine)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

void SrsEncoder::show_encode_log_message()
{
    pprint->elapse();

    // reportable
    if (pprint->can_print()) {
        // TODO: FIXME: show more info.
        srs_trace("-> "SRS_CONSTS_LOG_ENCODER" time=%"PRId64", encoders=%d, input=%s", 
            pprint->age(), (int)ffmpegs.size(), input_stream_name.c_str());
    }
}

#endif


