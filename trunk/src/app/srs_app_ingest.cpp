/*
The MIT License (MIT)

Copyright (c) 2013-2016 SRS(ossrs)

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

#include <stdlib.h>
using namespace std;

#include <srs_kernel_error.hpp>
#include <srs_app_config.hpp>
#include <srs_kernel_log.hpp>
#include <srs_app_ffmpeg.hpp>
#include <srs_app_pithy_print.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_app_utility.hpp>
#include <srs_protocol_utility.hpp>

// when error, ingester sleep for a while and retry.
// ingest never sleep a long time, for we must start the stream ASAP.
#define SRS_AUTO_INGESTER_SLEEP_US (int64_t)(3*1000*1000LL)

SrsIngesterFFMPEG::SrsIngesterFFMPEG()
{
    ffmpeg = NULL;
}

SrsIngesterFFMPEG::~SrsIngesterFFMPEG()
{
    srs_freep(ffmpeg);
}

int SrsIngesterFFMPEG::initialize(SrsFFMPEG* ff, string v, string i)
{
    int ret = ERROR_SUCCESS;
    
    ffmpeg = ff;
    vhost = v;
    id = i;
    starttime = srs_get_system_time_ms();
    
    return ret;
}

string SrsIngesterFFMPEG::uri()
{
    return vhost + "/" + id;
}

int SrsIngesterFFMPEG::alive()
{
    return (int)(srs_get_system_time_ms() - starttime);
}

bool SrsIngesterFFMPEG::equals(string v)
{
    return vhost == v;
}

bool SrsIngesterFFMPEG::equals(string v, string i)
{
    return vhost == v && id == i;
}

int SrsIngesterFFMPEG::start()
{
    return ffmpeg->start();
}

void SrsIngesterFFMPEG::stop()
{
    ffmpeg->stop();
}

int SrsIngesterFFMPEG::cycle()
{
    return ffmpeg->cycle();
}

void SrsIngesterFFMPEG::fast_stop()
{
    ffmpeg->fast_stop();
}

SrsIngester::SrsIngester()
{
    _srs_config->subscribe(this);
    
    expired = false;
    
    pthread = new SrsReusableThread("ingest", this, SRS_AUTO_INGESTER_SLEEP_US);
    pprint = SrsPithyPrint::create_ingester();
}

SrsIngester::~SrsIngester()
{
    _srs_config->unsubscribe(this);
    
    srs_freep(pthread);
    clear_engines();
}

void SrsIngester::dispose()
{
    // first, use fast stop to notice all FFMPEG to quit gracefully.
    fast_stop();
    
    // then, use stop to wait FFMPEG quit one by one and send SIGKILL if needed.
    stop();
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

void SrsIngester::stop()
{
    pthread->stop();
    clear_engines();
}

void SrsIngester::fast_stop()
{
    std::vector<SrsIngesterFFMPEG*>::iterator it;
    for (it = ingesters.begin(); it != ingesters.end(); ++it) {
        SrsIngesterFFMPEG* ingester = *it;
        ingester->fast_stop();
    }
    
    if (!ingesters.empty()) {
        srs_trace("fast stop all ingesters ok.");
    }
}

int SrsIngester::cycle()
{
    int ret = ERROR_SUCCESS;
    
    // when expired, restart all ingesters.
    if (expired) {
        expired = false;
        
        // stop current ingesters.
        fast_stop();
        clear_engines();
        
        // re-prase the ingesters.
        if ((ret = parse()) != ERROR_SUCCESS) {
            return ret;
        }
    }
    
    // cycle exists ingesters.
    std::vector<SrsIngesterFFMPEG*>::iterator it;
    for (it = ingesters.begin(); it != ingesters.end(); ++it) {
        SrsIngesterFFMPEG* ingester = *it;
        
        // start all ffmpegs.
        if ((ret = ingester->start()) != ERROR_SUCCESS) {
            srs_error("ingest ffmpeg start failed. ret=%d", ret);
            return ret;
        }

        // check ffmpeg status.
        if ((ret = ingester->cycle()) != ERROR_SUCCESS) {
            srs_error("ingest ffmpeg cycle failed. ret=%d", ret);
            return ret;
        }
    }

    // pithy print
    show_ingest_log_message();
    
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

int SrsIngester::parse_ingesters(SrsConfDirective* vhost)
{
    int ret = ERROR_SUCCESS;
    
    // when vhost disabled, ignore any ingesters.
    if (!_srs_config->get_vhost_enabled(vhost)) {
        return ret;
    }
    
    std::vector<SrsConfDirective*> ingesters = _srs_config->get_ingesters(vhost->arg0());
    
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
    std::vector<SrsConfDirective*> engines = _srs_config->get_transcode_engines(ingest);
    
    // create ingesters without engines.
    if (engines.empty()) {
        SrsFFMPEG* ffmpeg = new SrsFFMPEG(ffmpeg_bin);
        if ((ret = initialize_ffmpeg(ffmpeg, vhost, ingest, NULL)) != ERROR_SUCCESS) {
            srs_freep(ffmpeg);
            if (ret != ERROR_ENCODER_LOOP) {
                srs_error("invalid ingest engine. ret=%d", ret);
            }
            return ret;
        }
        
        SrsIngesterFFMPEG* ingester = new SrsIngesterFFMPEG();
        if ((ret = ingester->initialize(ffmpeg, vhost->arg0(), ingest->arg0())) != ERROR_SUCCESS) {
            srs_freep(ingester);
            return ret;
        }
        
        ingesters.push_back(ingester);
        return ret;
    }
    
    // create ingesters with engine
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
        
        SrsIngesterFFMPEG* ingester = new SrsIngesterFFMPEG();
        if ((ret = ingester->initialize(ffmpeg, vhost->arg0(), ingest->arg0())) != ERROR_SUCCESS) {
            srs_freep(ingester);
            return ret;
        }
        
        ingesters.push_back(ingester);
    }
    
    return ret;
}

int SrsIngester::initialize_ffmpeg(SrsFFMPEG* ffmpeg, SrsConfDirective* vhost, SrsConfDirective* ingest, SrsConfDirective* engine)
{
    int ret = ERROR_SUCCESS;
    
    int port;
    if (true) {
        std::vector<std::string> ip_ports = _srs_config->get_listens();
        srs_assert(ip_ports.size() > 0);
        
        std::string ip;
        std::string ep = ip_ports[0];
        srs_parse_endpoint(ep, ip, port);
    }
    
    std::string output = _srs_config->get_engine_output(engine);
    // output stream, to other/self server
    // ie. rtmp://localhost:1935/live/livestream_sd
    output = srs_string_replace(output, "[vhost]", vhost->arg0());
    output = srs_string_replace(output, "[port]", srs_int2str(port));
    if (output.empty()) {
        ret = ERROR_ENCODER_NO_OUTPUT;
        srs_trace("empty output url, ingest=%s. ret=%d", ingest->arg0().c_str(), ret);
        return ret;
    }
    
    // find the app and stream in rtmp url
    std::string app, stream;
    if (true) {
        int port = SRS_CONSTS_RTMP_DEFAULT_PORT;
        std::string tcUrl, schema, host, vhost2, param;
        srs_parse_rtmp_url(output, tcUrl, stream);
        srs_discovery_tc_url(tcUrl, schema, host, vhost2, app, port, param);
    }
    
    std::string log_file = SRS_CONSTS_NULL_FILE; // disabled
    // write ffmpeg info to log file.
    if (_srs_config->get_ffmpeg_log_enabled()) {
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
    }
    
    // input
    std::string input_type = _srs_config->get_ingest_input_type(ingest);
    if (input_type.empty()) {
        ret = ERROR_ENCODER_NO_INPUT;
        srs_trace("empty intput type, ingest=%s. ret=%d", ingest->arg0().c_str(), ret);
        return ret;
    }

    if (srs_config_ingest_is_file(input_type)) {
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
    } else if (srs_config_ingest_is_stream(input_type)) {
        std::string input_url = _srs_config->get_ingest_input_url(ingest);
        if (input_url.empty()) {
            ret = ERROR_ENCODER_NO_INPUT;
            srs_trace("empty intput url, ingest=%s. ret=%d", ingest->arg0().c_str(), ret);
            return ret;
        }
        
        // for stream, no re.
        std::string input_transport = _srs_config->get_ingest_input_transport(ingest);
        if (srs_config_ingest_is_tcp(input_transport)) {
            ffmpeg->set_iparams("-rtsp_transport");
        } else {
            ffmpeg->set_iparams("");
        }

    
        if ((ret = ffmpeg->initialize(input_url, output, log_file)) != ERROR_SUCCESS) {
            return ret;
        }
    } else {
        ret = ERROR_ENCODER_INPUT_TYPE;
        srs_error("invalid ingest=%s type=%s, ret=%d", 
            ingest->arg0().c_str(), input_type.c_str(), ret);
    }
    
    // set output format to flv for RTMP
    ffmpeg->set_oformat("flv");
    
    std::string vcodec = _srs_config->get_engine_vcodec(engine);
    std::string acodec = _srs_config->get_engine_acodec(engine);
    // whatever the engine config, use copy as default.
    bool engine_disabled = !engine || !_srs_config->get_engine_enabled(engine);
    if (engine_disabled || vcodec.empty() || acodec.empty()) {
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

void SrsIngester::show_ingest_log_message()
{
    pprint->elapse();

    if ((int)ingesters.size() <= 0) {
        return;
    }
    
    // random choose one ingester to report.
    int index = rand() % (int)ingesters.size();
    SrsIngesterFFMPEG* ingester = ingesters.at(index);
    
    // reportable
    if (pprint->can_print()) {
        srs_trace("-> "SRS_CONSTS_LOG_INGESTER" time=%"PRId64", ingesters=%d, #%d(alive=%ds, %s)",
            pprint->age(), (int)ingesters.size(), index, ingester->alive() / 1000, ingester->uri().c_str());
    }
}

int SrsIngester::on_reload_vhost_removed(string vhost)
{
    int ret = ERROR_SUCCESS;
    
    std::vector<SrsIngesterFFMPEG*>::iterator it;
    
    for (it = ingesters.begin(); it != ingesters.end();) {
        SrsIngesterFFMPEG* ingester = *it;
        
        if (!ingester->equals(vhost)) {
            ++it;
            continue;
        }
        
        // stop the ffmpeg and free it.
        ingester->stop();
        
        srs_trace("reload stop ingester, vhost=%s, id=%s", vhost.c_str(), ingester->uri().c_str());
            
        srs_freep(ingester);
        
        // remove the item from ingesters.
        it = ingesters.erase(it);
    }

    return ret;
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

int SrsIngester::on_reload_ingest_removed(string vhost, string ingest_id)
{
    int ret = ERROR_SUCCESS;
    
    std::vector<SrsIngesterFFMPEG*>::iterator it;
    
    for (it = ingesters.begin(); it != ingesters.end();) {
        SrsIngesterFFMPEG* ingester = *it;
        
        if (!ingester->equals(vhost, ingest_id)) {
            ++it;
            continue;
        }
        
        // stop the ffmpeg and free it.
        ingester->stop();
        
        srs_trace("reload stop ingester, vhost=%s, id=%s", vhost.c_str(), ingester->uri().c_str());
            
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

int SrsIngester::on_reload_listen()
{
    expired = true;
    return ERROR_SUCCESS;
}

#endif

