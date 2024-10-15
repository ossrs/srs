//
// Copyright (c) 2013-2024 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_app_ingest.hpp>

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

SrsIngesterFFMPEG::SrsIngesterFFMPEG()
{
    ffmpeg = NULL;
    starttime = 0;
}

SrsIngesterFFMPEG::~SrsIngesterFFMPEG()
{
    srs_freep(ffmpeg);
}

srs_error_t SrsIngesterFFMPEG::initialize(SrsFFMPEG* ff, string v, string i)
{
    srs_error_t err = srs_success;
    
    ffmpeg = ff;
    vhost = v;
    id = i;
    starttime = srs_get_system_time();
    
    return err;
}

string SrsIngesterFFMPEG::uri()
{
    return vhost + "/" + id;
}

srs_utime_t SrsIngesterFFMPEG::alive()
{
    return srs_get_system_time() - starttime;
}

bool SrsIngesterFFMPEG::equals(string v)
{
    return vhost == v;
}

bool SrsIngesterFFMPEG::equals(string v, string i)
{
    return vhost == v && id == i;
}

srs_error_t SrsIngesterFFMPEG::start()
{
    return ffmpeg->start();
}

void SrsIngesterFFMPEG::stop()
{
    ffmpeg->stop();
}

srs_error_t SrsIngesterFFMPEG::cycle()
{
    return ffmpeg->cycle();
}

void SrsIngesterFFMPEG::fast_stop()
{
    ffmpeg->fast_stop();
}

void SrsIngesterFFMPEG::fast_kill()
{
    ffmpeg->fast_kill();
}

SrsIngester::SrsIngester()
{
    _srs_config->subscribe(this);
    
    expired = false;
    disposed = false;
    
    trd = new SrsDummyCoroutine();
    pprint_ = SrsPithyPrint::create_ingester();
}

SrsIngester::~SrsIngester()
{
    _srs_config->unsubscribe(this);
    
    srs_freep(trd);
    clear_engines();
    srs_freep(pprint_);
}

void SrsIngester::dispose()
{
    if (disposed) {
        return;
    }
    disposed = true;

    // first, use fast stop to notice all FFMPEG to quit gracefully.
    fast_stop();

    srs_usleep(100 * SRS_UTIME_MILLISECONDS);
    
    // then, use fast kill to ensure FFMPEG quit.
    fast_kill();
}

srs_error_t SrsIngester::start()
{
    srs_error_t err = srs_success;
    
    if ((err = parse()) != srs_success) {
        clear_engines();
        return srs_error_wrap(err, "parse");
    }
    
    // even no ingesters, we must also start it,
    // for the reload may add more ingesters.
    
    // start thread to run all encoding engines.
    srs_freep(trd);
    trd = new SrsSTCoroutine("ingest", this, _srs_context->get_id());
    
    if ((err = trd->start()) != srs_success) {
        return srs_error_wrap(err, "start coroutine");
    }
    
    return err;
}

void SrsIngester::stop()
{
    trd->stop();
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

void SrsIngester::fast_kill()
{
    std::vector<SrsIngesterFFMPEG*>::iterator it;
    for (it = ingesters.begin(); it != ingesters.end(); ++it) {
        SrsIngesterFFMPEG* ingester = *it;
        ingester->fast_kill();
    }

    if (!ingesters.empty()) {
        srs_trace("fast kill all ingesters ok.");
    }
}

// when error, ingester sleep for a while and retry.
// ingest never sleep a long time, for we must start the stream ASAP.
#define SRS_INGESTER_CIMS (3 * SRS_UTIME_SECONDS)

srs_error_t SrsIngester::cycle()
{
    srs_error_t err = srs_success;
    
    while (!disposed) {
        // We always check status first.
        // @see https://github.com/ossrs/srs/issues/1634#issuecomment-597571561
        if ((err = trd->pull()) != srs_success) {
            return srs_error_wrap(err, "ingester");
        }

        if ((err = do_cycle()) != srs_success) {
            srs_warn("Ingester: Ignore error, %s", srs_error_desc(err).c_str());
            srs_freep(err);
        }

        srs_usleep(SRS_INGESTER_CIMS);
    }
    
    return err;
}

srs_error_t SrsIngester::do_cycle()
{
    srs_error_t err = srs_success;
    
    // when expired, restart all ingesters.
    if (expired) {
        expired = false;
        
        // stop current ingesters.
        fast_stop();
        clear_engines();
        
        // re-prase the ingesters.
        if ((err = parse()) != srs_success) {
            return srs_error_wrap(err, "parse");
        }
    }
    
    // cycle exists ingesters.
    std::vector<SrsIngesterFFMPEG*>::iterator it;
    for (it = ingesters.begin(); it != ingesters.end(); ++it) {
        SrsIngesterFFMPEG* ingester = *it;
        
        // start all ffmpegs.
        if ((err = ingester->start()) != srs_success) {
            return srs_error_wrap(err, "ingester start");
        }
        
        // check ffmpeg status.
        if ((err = ingester->cycle()) != srs_success) {
            return srs_error_wrap(err, "ingester cycle");
        }
    }
    
    // pithy print
    show_ingest_log_message();
    
    return err;
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

srs_error_t SrsIngester::parse()
{
    srs_error_t err = srs_success;
    
    // parse ingesters
    std::vector<SrsConfDirective*> vhosts;
    _srs_config->get_vhosts(vhosts);
    
    for (int i = 0; i < (int)vhosts.size(); i++) {
        SrsConfDirective* vhost = vhosts[i];
        if ((err = parse_ingesters(vhost)) != srs_success) {
            return srs_error_wrap(err, "parse ingesters");
        }
    }
    
    return err;
}

srs_error_t SrsIngester::parse_ingesters(SrsConfDirective* vhost)
{
    srs_error_t err = srs_success;
    
    // when vhost disabled, ignore any ingesters.
    if (!_srs_config->get_vhost_enabled(vhost)) {
        return err;
    }
    
    std::vector<SrsConfDirective*> ingesters = _srs_config->get_ingesters(vhost->arg0());
    
    // create engine
    for (int i = 0; i < (int)ingesters.size(); i++) {
        SrsConfDirective* ingest = ingesters[i];
        if ((err = parse_engines(vhost, ingest)) != srs_success) {
            return srs_error_wrap(err, "parse engines");
        }
    }
    
    return err;
}

srs_error_t SrsIngester::parse_engines(SrsConfDirective* vhost, SrsConfDirective* ingest)
{
    srs_error_t err = srs_success;
    
    if (!_srs_config->get_ingest_enabled(ingest)) {
        return err;
    }
    
    std::string ffmpeg_bin = _srs_config->get_ingest_ffmpeg(ingest);
    if (ffmpeg_bin.empty()) {
        return srs_error_new(ERROR_ENCODER_PARSE, "parse ffmpeg");
    }
    
    // get all engines.
    std::vector<SrsConfDirective*> engines = _srs_config->get_transcode_engines(ingest);
    
    // create ingesters without engines.
    if (engines.empty()) {
        SrsFFMPEG* ffmpeg = new SrsFFMPEG(ffmpeg_bin);
        if ((err = initialize_ffmpeg(ffmpeg, vhost, ingest, NULL)) != srs_success) {
            srs_freep(ffmpeg);
            return srs_error_wrap(err, "init ffmpeg");
        }
        
        SrsIngesterFFMPEG* ingester = new SrsIngesterFFMPEG();
        if ((err = ingester->initialize(ffmpeg, vhost->arg0(), ingest->arg0())) != srs_success) {
            srs_freep(ingester);
            return srs_error_wrap(err, "init ingester");
        }
        
        ingesters.push_back(ingester);
        return err;
    }
    
    // create ingesters with engine
    for (int i = 0; i < (int)engines.size(); i++) {
        SrsConfDirective* engine = engines[i];
        SrsFFMPEG* ffmpeg = new SrsFFMPEG(ffmpeg_bin);
        if ((err = initialize_ffmpeg(ffmpeg, vhost, ingest, engine)) != srs_success) {
            srs_freep(ffmpeg);
            return srs_error_wrap(err, "init ffmpeg");
        }
        
        SrsIngesterFFMPEG* ingester = new SrsIngesterFFMPEG();
        if ((err = ingester->initialize(ffmpeg, vhost->arg0(), ingest->arg0())) != srs_success) {
            srs_freep(ingester);
            return srs_error_wrap(err, "init ingester");
        }
        
        ingesters.push_back(ingester);
    }
    
    return err;
}

srs_error_t SrsIngester::initialize_ffmpeg(SrsFFMPEG* ffmpeg, SrsConfDirective* vhost, SrsConfDirective* ingest, SrsConfDirective* engine)
{
    srs_error_t err = srs_success;
    
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
    output = srs_path_build_timestamp(output);
    // Remove the only param with default vhost.
    output = srs_string_replace(output, "vhost=" SRS_CONSTS_RTMP_DEFAULT_VHOST, "");
    output = srs_string_replace(output, "?&", "?");
    output = srs_string_replace(output, "?/", "/"); // For params over app.
    output = srs_string_trim_end(output, "?");
    if (output.empty()) {
        return srs_error_new(ERROR_ENCODER_NO_OUTPUT, "empty output url, ingest=%s", ingest->arg0().c_str());
    }
    
    // find the app and stream in rtmp url
    std::string app, stream;
    if (true) {
        int port = SRS_CONSTS_RTMP_DEFAULT_PORT;
        std::string tcUrl, schema, host, vhost2, param;
        srs_parse_rtmp_url(output, tcUrl, stream);
        srs_discovery_tc_url(tcUrl, schema, host, vhost2, app, stream, port, param);
    }
    
    std::string log_file = SRS_CONSTS_NULL_FILE; // disabled
    // write ffmpeg info to log file.
    if (_srs_config->get_ff_log_enabled()) {
        log_file = _srs_config->get_ff_log_dir();
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

    std::string log_level = _srs_config->get_ff_log_level();
    if (!log_level.empty()) {
        ffmpeg->append_iparam("-loglevel");
        ffmpeg->append_iparam(log_level);
    }

    // input
    std::string input_type = _srs_config->get_ingest_input_type(ingest);
    if (input_type.empty()) {
        return srs_error_new(ERROR_ENCODER_NO_INPUT, "empty intput type, ingest=%s", ingest->arg0().c_str());
    }
    
    if (srs_config_ingest_is_file(input_type)) {
        std::string input_url = _srs_config->get_ingest_input_url(ingest);
        if (input_url.empty()) {
            return srs_error_new(ERROR_ENCODER_NO_INPUT, "empty intput url, ingest=%s", ingest->arg0().c_str());
        }
        
        // for file, set re.
        ffmpeg->append_iparam("-re");
        
        if ((err = ffmpeg->initialize(input_url, output, log_file)) != srs_success) {
            return srs_error_wrap(err, "init ffmpeg");
        }
    } else if (srs_config_ingest_is_stream(input_type)) {
        std::string input_url = _srs_config->get_ingest_input_url(ingest);
        if (input_url.empty()) {
            return srs_error_new(ERROR_ENCODER_NO_INPUT, "empty intput url, ingest=%s", ingest->arg0().c_str());
        }
        
        // For stream, also use -re, to ingest HLS better.
        ffmpeg->append_iparam("-re");
        
        if ((err = ffmpeg->initialize(input_url, output, log_file)) != srs_success) {
            return srs_error_wrap(err, "init ffmpeg");
        }
    } else {
        return srs_error_new(ERROR_ENCODER_INPUT_TYPE, "invalid ingest=%s type=%s", ingest->arg0().c_str(), input_type.c_str());
    }
    
    // set output format to flv for RTMP
    ffmpeg->set_oformat("flv");
    
    std::string vcodec = _srs_config->get_engine_vcodec(engine);
    std::string acodec = _srs_config->get_engine_acodec(engine);
    // whatever the engine config, use copy as default.
    bool engine_disabled = !engine || !_srs_config->get_engine_enabled(engine);
    if (engine_disabled || vcodec.empty() || acodec.empty()) {
        if ((err = ffmpeg->initialize_copy()) != srs_success) {
            return srs_error_wrap(err, "init ffmpeg");
        }
    } else {
        if ((err = ffmpeg->initialize_transcode(engine)) != srs_success) {
            return srs_error_wrap(err, "init ffmpeg");
        }
    }
    
    srs_trace("parse success, ingest=%s, vhost=%s", ingest->arg0().c_str(), vhost->arg0().c_str());
    
    return err;
}

void SrsIngester::show_ingest_log_message()
{
    pprint_->elapse();
    
    if ((int)ingesters.size() <= 0) {
        return;
    }
    
    // random choose one ingester to report.
    int index = srs_random() % (int)ingesters.size();
    SrsIngesterFFMPEG* ingester = ingesters.at(index);
    
    // reportable
    if (pprint_->can_print()) {
        srs_trace("-> " SRS_CONSTS_LOG_INGESTER " time=%dms, ingesters=%d, #%d(alive=%dms, %s)",
                  srsu2msi(pprint_->age()), (int)ingesters.size(), index, srsu2msi(ingester->alive()), ingester->uri().c_str());
    }
}

srs_error_t SrsIngester::on_reload_vhost_removed(string vhost)
{
    srs_error_t err = srs_success;
    
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
    
    return err;
}

srs_error_t SrsIngester::on_reload_vhost_added(string vhost)
{
    srs_error_t err = srs_success;
    
    SrsConfDirective* _vhost = _srs_config->get_vhost(vhost);
    if ((err = parse_ingesters(_vhost)) != srs_success) {
        return srs_error_wrap(err, "parse ingesters");
    }
    
    srs_trace("reload add vhost ingesters, vhost=%s", vhost.c_str());
    
    return err;
}

srs_error_t SrsIngester::on_reload_ingest_removed(string vhost, string ingest_id)
{
    srs_error_t err = srs_success;
    
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
    
    return err;
}

srs_error_t SrsIngester::on_reload_ingest_added(string vhost, string ingest_id)
{
    srs_error_t err = srs_success;
    
    SrsConfDirective* _vhost = _srs_config->get_vhost(vhost);
    SrsConfDirective* _ingester = _srs_config->get_ingest_by_id(vhost, ingest_id);
    
    if ((err = parse_engines(_vhost, _ingester)) != srs_success) {
        return srs_error_wrap(err, "parse engines");
    }
    
    srs_trace("reload add ingester, vhost=%s, id=%s", vhost.c_str(), ingest_id.c_str());
    
    return err;
}

srs_error_t SrsIngester::on_reload_ingest_updated(string vhost, string ingest_id)
{
    srs_error_t err = srs_success;
    
    if ((err = on_reload_ingest_removed(vhost, ingest_id)) != srs_success) {
        return srs_error_wrap(err, "reload ingest removed");
    }
    
    if ((err = on_reload_ingest_added(vhost, ingest_id)) != srs_success) {
        return srs_error_wrap(err, "reload ingest added");
    }
    
    srs_trace("reload updated ingester, vhost=%s, id=%s", vhost.c_str(), ingest_id.c_str());
    
    return err;
}

srs_error_t SrsIngester::on_reload_listen()
{
    expired = true;
    return srs_success;
}

