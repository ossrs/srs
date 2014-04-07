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

#include <srs_app_encoder.hpp>

#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_app_config.hpp>
#include <srs_protocol_rtmp.hpp>
#include <srs_app_pithy_print.hpp>
#include <srs_protocol_rtmp_stack.hpp>
#include <srs_app_ffmpeg.hpp>

#ifdef SRS_TRANSCODE

// when error, encoder sleep for a while and retry.
#define SRS_ENCODER_SLEEP_US (int64_t)(3*1000*1000LL)

SrsEncoder::SrsEncoder()
{
    pthread = new SrsThread(this, SRS_ENCODER_SLEEP_US);
    pithy_print = new SrsPithyPrint(SRS_STAGE_ENCODER);
}

SrsEncoder::~SrsEncoder()
{
    on_unpublish();
    
    srs_freep(pthread);
}

int SrsEncoder::on_publish(SrsRequest* req)
{
    int ret = ERROR_SUCCESS;

    ret = parse_scope_engines(req);
    
    // ignore the loop encoder
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
            srs_error("ffmpeg start failed. ret=%d", ret);
            return ret;
        }

        // check ffmpeg status.
        if ((ret = ffmpeg->cycle()) != ERROR_SUCCESS) {
            srs_error("ffmpeg cycle failed. ret=%d", ret);
            return ret;
        }
    }

    // pithy print
    encoder();
    pithy_print->elapse(SRS_ENCODER_SLEEP_US / 1000);
    
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
        if ((ret = parse_transcode(req, conf)) != ERROR_SUCCESS) {
            srs_error("parse vhost scope=%s transcode engines failed. "
                "ret=%d", scope.c_str(), ret);
            return ret;
        }
    }
    // parse app scope engines
    scope = req->app;
    if ((conf = _srs_config->get_transcode(req->vhost, scope)) != NULL) {
        if ((ret = parse_transcode(req, conf)) != ERROR_SUCCESS) {
            srs_error("parse app scope=%s transcode engines failed. "
                "ret=%d", scope.c_str(), ret);
            return ret;
        }
    }
    // parse stream scope engines
    scope += "/";
    scope += req->stream;
    if ((conf = _srs_config->get_transcode(req->vhost, scope)) != NULL) {
        if ((ret = parse_transcode(req, conf)) != ERROR_SUCCESS) {
            srs_error("parse stream scope=%s transcode engines failed. "
                "ret=%d", scope.c_str(), ret);
            return ret;
        }
    }
    
    return ret;
}

int SrsEncoder::parse_transcode(SrsRequest* req, SrsConfDirective* conf)
{
    int ret = ERROR_SUCCESS;
    
    srs_assert(conf);
    
    // enabled
    if (!_srs_config->get_transcode_enabled(conf)) {
        srs_trace("ignore the disabled transcode: %s", 
            conf->arg0().c_str());
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
    std::vector<SrsConfDirective*> engines;
    _srs_config->get_transcode_engines(conf, engines);
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
        
        if ((ret = ffmpeg->initialize(req, engine)) != ERROR_SUCCESS) {
            srs_freep(ffmpeg);
            
            // if got a loop, donot transcode the whole stream.
            if (ret == ERROR_ENCODER_LOOP) {
                break;
            }
            
            srs_error("invalid transcode engine: %s %s", 
                conf->arg0().c_str(), engine->arg0().c_str());
            return ret;
        }

        ffmpegs.push_back(ffmpeg);
    }
    
    return ret;
}

void SrsEncoder::encoder()
{
    // reportable
    if (pithy_print->can_print()) {
        // TODO: FIXME: show more info.
        srs_trace("-> time=%"PRId64", encoders=%d", pithy_print->get_age(), (int)ffmpegs.size());
    }
}

#endif

