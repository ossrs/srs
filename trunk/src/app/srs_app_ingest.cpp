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

#ifdef SRS_AUTO_INGEST

using namespace std;

#include <srs_kernel_error.hpp>
#include <srs_app_config.hpp>
#include <srs_kernel_log.hpp>
#include <srs_app_ffmpeg.hpp>
#include <srs_app_pithy_print.hpp>

// when error, ingester sleep for a while and retry.
// ingest never sleep a long time, for we must start the stream ASAP.
#define SRS_AUTO_INGESTER_SLEEP_US (int64_t)(6*100*1000LL)

SrsIngesterFFMPEG::SrsIngesterFFMPEG(SrsFFMPEG* _ffmpeg, string _vhost, string _id)
{
    ffmpeg = _ffmpeg;
    vhost = _vhost;
    id = _id;
}

SrsIngesterFFMPEG::~SrsIngesterFFMPEG()
{
    srs_freep(ffmpeg);
}

SrsIngester::SrsIngester()
{
    _srs_config->subscribe(this);
    
    pthread = new SrsThread(this, SRS_AUTO_INGESTER_SLEEP_US, true);
    pithy_print = new SrsPithyPrint(SRS_STAGE_INGESTER);
}

SrsIngester::~SrsIngester()
{
    _srs_config->unsubscribe(this);
    
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
    
    // even no ingesters, we must also start it,
    // for the reload may add more ingesters.
    
    // start thread to run all encoding engines.
    if ((ret = pthread->start()) != ERROR_SUCCESS) {
        srs_error("st_thread_create failed. ret=%d", ret);
        return ret;
    }
    srs_trace("ingest thread cid=%d, current_cid=%d", pthread->cid(), _srs_context->get_id());
    
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
        if ((ret = parse_engines(vhost, ingest)) != ERROR_SUCCESS) {
            return ret;
        }
    }
    
    return ret;
}

int SrsIngester::parse_engines(SrsConfDirective* vhost, SrsConfDirective* ingest)
{
    int ret = ERROR_SUCCESS;

    if (!_srs_config->get_ingest_enabled(ingest)) {
        return ret;
    }
    
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

        SrsIngesterFFMPEG* ingester = new SrsIngesterFFMPEG(ffmpeg, vhost->arg0(), ingest->arg0());
        ingesters.push_back(ingester);
        return ret;
    }

    // create engine
    for (int i = 0; i < (int)engines.size(); i++) {
        SrsConfDirective* engine = engines[i];
        SrsFFMPEG* ffmpeg = new SrsFFMPEG(ffmpeg_bin);
        if ((ret = initialize_ffmpeg(ffmpeg, vhost, ingest, engine)) != ERROR_SUCCESS) {
            srs_freep(ffmpeg);
            if (ret != ERROR_ENCODER_LOOP) {
                srs_error("invalid ingest engine: %s %s, ret=%d", 
                    ingest->arg0().c_str(), engine->arg0().c_str(), ret);
            }
            return ret;
        }

        SrsIngesterFFMPEG* ingester = new SrsIngesterFFMPEG(ffmpeg, vhost->arg0(), ingest->arg0());
        ingesters.push_back(ingester);
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
    
    std::vector<SrsIngesterFFMPEG*>::iterator it;
    for (it = ingesters.begin(); it != ingesters.end(); ++it) {
        SrsIngesterFFMPEG* ingester = *it;
        
        // start all ffmpegs.
        if ((ret = ingester->ffmpeg->start()) != ERROR_SUCCESS) {
            srs_error("ingest ffmpeg start failed. ret=%d", ret);
            return ret;
        }

        // check ffmpeg status.
        if ((ret = ingester->ffmpeg->cycle()) != ERROR_SUCCESS) {
            srs_error("ingest ffmpeg cycle failed. ret=%d", ret);
            return ret;
        }
    }

    // pithy print
    ingester();
    pithy_print->elapse();
    
    return ret;
}

void SrsIngester::on_thread_stop()
{
}

void SrsIngester::clear_engines()
{
    std::vector<SrsIngesterFFMPEG*>::iterator it;
    
    for (it = ingesters.begin(); it != ingesters.end(); ++it) {
        SrsIngesterFFMPEG* ingester = *it;
        srs_freep(ingester);
    }

    ingesters.clear();
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
        srs_trace("empty output url, ingest=%s. ret=%d", ingest->arg0().c_str(), ret);
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
    log_file += "ffmpeg-ingest";
    log_file += "-";
    log_file += vhost->arg0();
    log_file += "-";
    log_file += app;
    log_file += "-";
    log_file += stream;
    log_file += ".log";
    
    // input
    std::string input_type = _srs_config->get_ingest_input_type(ingest);
    if (input_type.empty()) {
        ret = ERROR_ENCODER_NO_INPUT;
        srs_trace("empty intput type, ingest=%s. ret=%d", ingest->arg0().c_str(), ret);
        return ret;
    }

    if (input_type == SRS_AUTO_INGEST_TYPE_FILE) {
        std::string input_url = _srs_config->get_ingest_input_url(ingest);
        if (input_url.empty()) {
            ret = ERROR_ENCODER_NO_INPUT;
            srs_trace("empty intput url, ingest=%s. ret=%d", ingest->arg0().c_str(), ret);
            return ret;
        }
        
        // for file, set re.
        ffmpeg->set_iparams("-re");
    
        if ((ret = ffmpeg->initialize(input_url, output, log_file)) != ERROR_SUCCESS) {
            return ret;
        }
    } else if (input_type == SRS_AUTO_INGEST_TYPE_STREAM) {
        std::string input_url = _srs_config->get_ingest_input_url(ingest);
        if (input_url.empty()) {
            ret = ERROR_ENCODER_NO_INPUT;
            srs_trace("empty intput url, ingest=%s. ret=%d", ingest->arg0().c_str(), ret);
            return ret;
        }
        
        // for stream, no re.
        ffmpeg->set_iparams("");
    
        if ((ret = ffmpeg->initialize(input_url, output, log_file)) != ERROR_SUCCESS) {
            return ret;
        }
    } else {
        ret = ERROR_ENCODER_INPUT_TYPE;
        srs_error("invalid ingest=%s type=%s, ret=%d", 
            ingest->arg0().c_str(), input_type.c_str(), ret);
    }
    
    std::string vcodec = _srs_config->get_engine_vcodec(engine);
    std::string acodec = _srs_config->get_engine_acodec(engine);
    // whatever the engine config, use copy as default.
    if (!engine || vcodec.empty() || acodec.empty()
        || !_srs_config->get_engine_enabled(engine)
    ) {
        if ((ret = ffmpeg->initialize_copy()) != ERROR_SUCCESS) {
            return ret;
        }
    } else {
        if ((ret = ffmpeg->initialize_transcode(engine)) != ERROR_SUCCESS) {
            return ret;
        }
    }
    
    srs_trace("parse success, ingest=%s, vhost=%s", 
        ingest->arg0().c_str(), vhost->arg0().c_str());
    
    return ret;
}

void SrsIngester::ingester()
{
    if ((int)ingesters.size() <= 0) {
        return;
    }
    
    // reportable
    if (pithy_print->can_print()) {
        // TODO: FIXME: show more info.
        srs_trace("-> "SRS_LOG_ID_INGESTER
            " time=%"PRId64", ingesters=%d", pithy_print->age(), (int)ingesters.size());
    }
}

int SrsIngester::on_reload_vhost_added(string vhost)
{
    int ret = ERROR_SUCCESS;
    
    SrsConfDirective* _vhost = _srs_config->get_vhost(vhost);
    if ((ret = parse_ingesters(_vhost)) != ERROR_SUCCESS) {
        return ret;
    }
    
    srs_trace("reload add vhost ingesters, vhost=%s", vhost.c_str());

    return ret;
}

int SrsIngester::on_reload_vhost_removed(string vhost)
{
    int ret = ERROR_SUCCESS;
    
    std::vector<SrsIngesterFFMPEG*>::iterator it;
    
    for (it = ingesters.begin(); it != ingesters.end();) {
        SrsIngesterFFMPEG* ingester = *it;
        
        if (ingester->vhost != vhost) {
            ++it;
            continue;
        }
        
        // stop the ffmpeg and free it.
        ingester->ffmpeg->stop();
        
        srs_trace("reload stop ingester, "
            "vhost=%s, id=%s", vhost.c_str(), ingester->id.c_str());
            
        srs_freep(ingester);
        
        // remove the item from ingesters.
        it = ingesters.erase(it);
    }

    return ret;
}

int SrsIngester::on_reload_ingest_removed(string vhost, string ingest_id)
{
    int ret = ERROR_SUCCESS;
    
    std::vector<SrsIngesterFFMPEG*>::iterator it;
    
    for (it = ingesters.begin(); it != ingesters.end();) {
        SrsIngesterFFMPEG* ingester = *it;
        
        if (ingester->vhost != vhost || ingester->id != ingest_id) {
            ++it;
            continue;
        }
        
        // stop the ffmpeg and free it.
        ingester->ffmpeg->stop();
        
        srs_trace("reload stop ingester, "
            "vhost=%s, id=%s", vhost.c_str(), ingester->id.c_str());
            
        srs_freep(ingester);
        
        // remove the item from ingesters.
        it = ingesters.erase(it);
    }
    
    return ret;
}

int SrsIngester::on_reload_ingest_added(string vhost, string ingest_id)
{
    int ret = ERROR_SUCCESS;
    
    SrsConfDirective* _vhost = _srs_config->get_vhost(vhost);
    SrsConfDirective* _ingester = _srs_config->get_ingest_by_id(vhost, ingest_id);
    
    if ((ret = parse_engines(_vhost, _ingester)) != ERROR_SUCCESS) {
        return ret;
    }
    
    srs_trace("reload add ingester, "
        "vhost=%s, id=%s", vhost.c_str(), ingest_id.c_str());
    
    return ret;
}

int SrsIngester::on_reload_ingest_updated(string vhost, string ingest_id)
{
    int ret = ERROR_SUCCESS;

    if ((ret = on_reload_ingest_removed(vhost, ingest_id)) != ERROR_SUCCESS) {
        return ret;
    }

    if ((ret = on_reload_ingest_added(vhost, ingest_id)) != ERROR_SUCCESS) {
        return ret;
    }
    
    srs_trace("reload updated ingester, "
        "vhost=%s, id=%s", vhost.c_str(), ingest_id.c_str());
    
    return ret;
}

#endif
