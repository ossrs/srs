//
// Copyright (c) 2013-2024 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_app_encoder.hpp>

#include <algorithm>
using namespace std;

#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_app_config.hpp>
#include <srs_protocol_rtmp_stack.hpp>
#include <srs_app_pithy_print.hpp>
#include <srs_app_ffmpeg.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_app_utility.hpp>

// for encoder to detect the dead loop
static std::vector<std::string> _transcoded_url;

SrsEncoder::SrsEncoder()
{
    trd = new SrsDummyCoroutine();
    pprint = SrsPithyPrint::create_encoder();
}

SrsEncoder::~SrsEncoder()
{
    on_unpublish();
    
    srs_freep(trd);
    srs_freep(pprint);
}

srs_error_t SrsEncoder::on_publish(SrsRequest* req)
{
    srs_error_t err = srs_success;
    
    // parse the transcode engines for vhost and app and stream.
    err = parse_scope_engines(req);
    
    // ignore the loop encoder
    // if got a loop, donot transcode the whole stream.
    if (srs_error_code(err) == ERROR_ENCODER_LOOP) {
        clear_engines();
        srs_error_reset(err);
    }
    
    // return for error or no engine.
    if (err != srs_success || ffmpegs.empty()) {
        return err;
    }
    
    // start thread to run all encoding engines.
    srs_freep(trd);
    trd = new SrsSTCoroutine("encoder", this, _srs_context->get_id());
    if ((err = trd->start()) != srs_success) {
        return srs_error_wrap(err, "start encoder");
    }
    
    return err;
}

void SrsEncoder::on_unpublish()
{
    trd->stop();
    clear_engines();
}

// when error, encoder sleep for a while and retry.
#define SRS_RTMP_ENCODER_CIMS (3 * SRS_UTIME_SECONDS)

srs_error_t SrsEncoder::cycle()
{
    srs_error_t err = srs_success;
    
    while (true) {
        // We always check status first.
        // @see https://github.com/ossrs/srs/issues/1634#issuecomment-597571561
        if ((err = trd->pull()) != srs_success) {
            err = srs_error_wrap(err, "encoder");
            break;
        }

        if ((err = do_cycle()) != srs_success) {
            srs_warn("Encoder: Ignore error, %s", srs_error_desc(err).c_str());
            srs_error_reset(err);
        }
    
        srs_usleep(SRS_RTMP_ENCODER_CIMS);
    }
    
    // kill ffmpeg when finished and it alive
    std::vector<SrsFFMPEG*>::iterator it;
    
    for (it = ffmpegs.begin(); it != ffmpegs.end(); ++it) {
        SrsFFMPEG* ffmpeg = *it;
        ffmpeg->stop();
    }
    
    return err;
}

srs_error_t SrsEncoder::do_cycle()
{
    srs_error_t err = srs_success;
    
    std::vector<SrsFFMPEG*>::iterator it;
    for (it = ffmpegs.begin(); it != ffmpegs.end(); ++it) {
        SrsFFMPEG* ffmpeg = *it;
        
        // start all ffmpegs.
        if ((err = ffmpeg->start()) != srs_success) {
            return srs_error_wrap(err, "ffmpeg start");
        }
        
        // check ffmpeg status.
        if ((err = ffmpeg->cycle()) != srs_success) {
            return srs_error_wrap(err, "ffmpeg cycle");
        }
    }
    
    // pithy print
    show_encode_log_message();
    
    return err;
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

srs_error_t SrsEncoder::parse_scope_engines(SrsRequest* req)
{
    srs_error_t err = srs_success;
    
    // parse all transcode engines.
    SrsConfDirective* conf = NULL;
    
    // parse vhost scope engines
    std::string scope = "";
    if ((conf = _srs_config->get_transcode(req->vhost, scope)) != NULL) {
        if ((err = parse_ffmpeg(req, conf)) != srs_success) {
            return srs_error_wrap(err, "parse ffmpeg");
        }
    }
    // parse app scope engines
    scope = req->app;
    if ((conf = _srs_config->get_transcode(req->vhost, scope)) != NULL) {
        if ((err = parse_ffmpeg(req, conf)) != srs_success) {
            return srs_error_wrap(err, "parse ffmpeg");
        }
    }
    // parse stream scope engines
    scope += "/";
    scope += req->stream;
    if ((conf = _srs_config->get_transcode(req->vhost, scope)) != NULL) {
        if ((err = parse_ffmpeg(req, conf)) != srs_success) {
            return srs_error_wrap(err, "parse ffmpeg");
        }
    }
    
    return err;
}

srs_error_t SrsEncoder::parse_ffmpeg(SrsRequest* req, SrsConfDirective* conf)
{
    srs_error_t err = srs_success;
    
    srs_assert(conf);
    
    // enabled
    if (!_srs_config->get_transcode_enabled(conf)) {
        srs_trace("ignore the disabled transcode: %s", conf->arg0().c_str());
        return err;
    }

    //Avoid repeat transcoding when the transcode is the same and source ip is localhost.
    if (req->ip == SRS_CONSTS_LOCALHOST) {
        std::map<string, string> kv_param;
        srs_parse_query_string(req->param, kv_param);
        int transcode_line = -1;
        std::map<string, string>::iterator iter = kv_param.find("transcode_line");
        if (iter != kv_param.end()) {
            transcode_line = ::atoi(iter->second.c_str());
        }

        if (transcode_line == conf->conf_line) {
            srs_trace("ignore the transcode : %s to avoid repeat transcoding", conf->arg0().c_str());
            return err;
        }
    }

    // ffmpeg
    std::string ffmpeg_bin = _srs_config->get_transcode_ffmpeg(conf);
    if (ffmpeg_bin.empty()) {
        srs_trace("ignore the empty ffmpeg transcode: %s", conf->arg0().c_str());
        return err;
    }
    
    // get all engines.
    std::vector<SrsConfDirective*> engines = _srs_config->get_transcode_engines(conf);
    if (engines.empty()) {
        srs_trace("ignore the empty transcode engine: %s", conf->arg0().c_str());
        return err;
    }
    
    // create engine
    for (int i = 0; i < (int)engines.size(); i++) {
        SrsConfDirective* engine = engines[i];
        if (!_srs_config->get_engine_enabled(engine)) {
            srs_trace("ignore the diabled transcode engine: %s %s", conf->arg0().c_str(), engine->arg0().c_str());
            continue;
        }

        SrsFFMPEG* ffmpeg = new SrsFFMPEG(ffmpeg_bin);
        if ((err = initialize_ffmpeg(ffmpeg, req, conf, engine)) != srs_success) {
            srs_freep(ffmpeg);
            return srs_error_wrap(err, "init ffmpeg");
        }
        
        ffmpegs.push_back(ffmpeg);
    }
    
    return err;
}

srs_error_t SrsEncoder::initialize_ffmpeg(SrsFFMPEG* ffmpeg, SrsRequest* req, SrsConfDirective* conf, SrsConfDirective* engine)
{
    srs_error_t err = srs_success;
    
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
    output = srs_string_replace(output, "[param]", req->param);
    output = srs_string_replace(output, "[engine]", engine->arg0());
    output = srs_path_build_timestamp(output);

    std::stringstream params;
    params << "&transcode_line=" << conf->conf_line;
    output.append(params.str());

    std::string log_file = SRS_CONSTS_NULL_FILE; // disabled
    // write ffmpeg info to log file.
    if (_srs_config->get_ff_log_enabled()) {
        log_file = _srs_config->get_ff_log_dir();
        log_file += "/";
        log_file += "ffmpeg-encoder";
        log_file += "-";
        log_file += req->vhost;
        log_file += "-";
        log_file += req->app;
        log_file += "-";
        log_file += req->stream;
        if (!engine->args.empty()) {
            log_file += "-";
            log_file += engine->arg0();
        }
        log_file += ".log";
    }
    
    // important: loop check, donot transcode again.
    std::vector<std::string>::iterator it;
    it = std::find(_transcoded_url.begin(), _transcoded_url.end(), input);
    if (it != _transcoded_url.end()) {
        return srs_error_new(ERROR_ENCODER_LOOP, "detect a loop cycle, input=%s, output=%s", input.c_str(), output.c_str());
    }
    _transcoded_url.push_back(output);
    
    if ((err = ffmpeg->initialize(input, output, log_file)) != srs_success) {
        return srs_error_wrap(err, "init ffmpeg");
    }
    if ((err = ffmpeg->initialize_transcode(engine)) != srs_success) {
        return srs_error_wrap(err, "init transcode");
    }
    
    return err;
}

void SrsEncoder::show_encode_log_message()
{
    pprint->elapse();
    
    // reportable
    if (pprint->can_print()) {
        // TODO: FIXME: show more info.
        srs_trace("-> " SRS_CONSTS_LOG_ENCODER " time=%" PRId64 ", encoders=%d, input=%s",
                  pprint->age(), (int)ffmpegs.size(), input_stream_name.c_str());
    }
}

