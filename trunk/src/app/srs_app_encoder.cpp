//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_app_encoder.hpp>

#include <algorithm>
using namespace std;

#include <srs_app_config.hpp>
#include <srs_app_factory.hpp>
#include <srs_app_ffmpeg.hpp>
#include <srs_app_utility.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_kernel_pithy_print.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_rtmp_stack.hpp>

// for encoder to detect the dead loop
static std::vector<std::string> _transcoded_url;

ISrsMediaEncoder::ISrsMediaEncoder()
{
}

ISrsMediaEncoder::~ISrsMediaEncoder()
{
}

SrsEncoder::SrsEncoder()
{
    trd_ = new SrsDummyCoroutine();
    pprint_ = SrsPithyPrint::create_encoder();

    config_ = _srs_config;
    app_factory_ = _srs_app_factory;
}

SrsEncoder::~SrsEncoder()
{
    on_unpublish();

    srs_freep(trd_);
    srs_freep(pprint_);

    config_ = NULL;
    app_factory_ = NULL;
}

srs_error_t SrsEncoder::on_publish(ISrsRequest *req)
{
    srs_error_t err = srs_success;

    // parse the transcode engines for vhost and app and stream.
    err = parse_scope_engines(req);

    // ignore the loop encoder
    // if got a loop, donot transcode the whole stream.
    if (srs_error_code(err) == ERROR_ENCODER_LOOP) {
        clear_engines();
        srs_freep(err);
    }

    // return for error or no engine.
    if (err != srs_success || ffmpegs_.empty()) {
        return err;
    }

    // start thread to run all encoding engines.
    srs_freep(trd_);
    trd_ = app_factory_->create_coroutine("encoder", this, _srs_context->get_id());
    if ((err = trd_->start()) != srs_success) {
        return srs_error_wrap(err, "start encoder");
    }

    return err;
}

void SrsEncoder::on_unpublish()
{
    trd_->stop();
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
        if ((err = trd_->pull()) != srs_success) {
            err = srs_error_wrap(err, "encoder");
            break;
        }

        if ((err = do_cycle()) != srs_success) {
            srs_warn("Encoder: Ignore error, %s", srs_error_desc(err).c_str());
            srs_freep(err);
        }

        srs_usleep(SRS_RTMP_ENCODER_CIMS);
    }

    // kill ffmpeg when finished and it alive
    std::vector<ISrsFFMPEG *>::iterator it;

    for (it = ffmpegs_.begin(); it != ffmpegs_.end(); ++it) {
        ISrsFFMPEG *ffmpeg = *it;
        ffmpeg->stop();
    }

    return err;
}

srs_error_t SrsEncoder::do_cycle()
{
    srs_error_t err = srs_success;

    std::vector<ISrsFFMPEG *>::iterator it;
    for (it = ffmpegs_.begin(); it != ffmpegs_.end(); ++it) {
        ISrsFFMPEG *ffmpeg = *it;

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
    std::vector<ISrsFFMPEG *>::iterator it;

    for (it = ffmpegs_.begin(); it != ffmpegs_.end(); ++it) {
        ISrsFFMPEG *ffmpeg = *it;

        std::string output = ffmpeg->output();

        std::vector<std::string>::iterator tu_it;
        tu_it = std::find(_transcoded_url.begin(), _transcoded_url.end(), output);
        if (tu_it != _transcoded_url.end()) {
            _transcoded_url.erase(tu_it);
        }

        srs_freep(ffmpeg);
    }

    ffmpegs_.clear();
}

ISrsFFMPEG *SrsEncoder::at(int index)
{
    return ffmpegs_[index];
}

srs_error_t SrsEncoder::parse_scope_engines(ISrsRequest *req)
{
    srs_error_t err = srs_success;

    // parse all transcode engines.
    SrsConfDirective *conf = NULL;

    // parse vhost scope engines
    std::string scope = "";
    if ((conf = config_->get_transcode(req->vhost_, scope)) != NULL) {
        if ((err = parse_ffmpeg(req, conf)) != srs_success) {
            return srs_error_wrap(err, "parse ffmpeg");
        }
    }
    // parse app scope engines
    scope = req->app_;
    if ((conf = config_->get_transcode(req->vhost_, scope)) != NULL) {
        if ((err = parse_ffmpeg(req, conf)) != srs_success) {
            return srs_error_wrap(err, "parse ffmpeg");
        }
    }
    // parse stream scope engines
    scope += "/";
    scope += req->stream_;
    if ((conf = config_->get_transcode(req->vhost_, scope)) != NULL) {
        if ((err = parse_ffmpeg(req, conf)) != srs_success) {
            return srs_error_wrap(err, "parse ffmpeg");
        }
    }

    return err;
}

srs_error_t SrsEncoder::parse_ffmpeg(ISrsRequest *req, SrsConfDirective *conf)
{
    srs_error_t err = srs_success;

    srs_assert(conf);

    // enabled
    if (!config_->get_transcode_enabled(conf)) {
        srs_trace("ignore the disabled transcode: %s", conf->arg0().c_str());
        return err;
    }

    // ffmpeg
    std::string ffmpeg_bin = config_->get_transcode_ffmpeg(conf);
    if (ffmpeg_bin.empty()) {
        srs_trace("ignore the empty ffmpeg transcode: %s", conf->arg0().c_str());
        return err;
    }

    // get all engines.
    std::vector<SrsConfDirective *> engines = config_->get_transcode_engines(conf);
    if (engines.empty()) {
        srs_trace("ignore the empty transcode engine: %s", conf->arg0().c_str());
        return err;
    }

    // create engine
    for (int i = 0; i < (int)engines.size(); i++) {
        SrsConfDirective *engine = engines[i];
        if (!config_->get_engine_enabled(engine)) {
            srs_trace("ignore the diabled transcode engine: %s %s", conf->arg0().c_str(), engine->arg0().c_str());
            continue;
        }

        ISrsFFMPEG *ffmpeg = app_factory_->create_ffmpeg(ffmpeg_bin);
        if ((err = initialize_ffmpeg(ffmpeg, req, engine)) != srs_success) {
            srs_freep(ffmpeg);
            return srs_error_wrap(err, "init ffmpeg");
        }

        ffmpegs_.push_back(ffmpeg);
    }

    return err;
}

srs_error_t SrsEncoder::initialize_ffmpeg(ISrsFFMPEG *ffmpeg, ISrsRequest *req, SrsConfDirective *engine)
{
    srs_error_t err = srs_success;

    std::string input;
    // input stream, from local.
    // ie. rtmp://localhost:1935/live/livestream
    input = "rtmp://";
    input += SRS_CONSTS_LOCALHOST;
    input += ":";
    input += srs_strconv_format_int(req->port_);
    input += "/";
    input += req->app_;
    input += "/";
    input += req->stream_;
    input += "?vhost=";
    input += req->vhost_;

    // stream name: vhost/app/stream for print
    input_stream_name_ = req->vhost_;
    input_stream_name_ += "/";
    input_stream_name_ += req->app_;
    input_stream_name_ += "/";
    input_stream_name_ += req->stream_;

    std::string output = config_->get_engine_output(engine);
    // output stream, to other/self server
    // ie. rtmp://localhost:1935/live/livestream_sd
    output = srs_strings_replace(output, "[vhost]", req->vhost_);
    output = srs_strings_replace(output, "[port]", srs_strconv_format_int(req->port_));
    output = srs_strings_replace(output, "[app]", req->app_);
    output = srs_strings_replace(output, "[stream]", req->stream_);
    output = srs_strings_replace(output, "[param]", req->param_);
    output = srs_strings_replace(output, "[engine]", engine->arg0());
    output = srs_path_build_timestamp(output);

    std::string log_file = SRS_CONSTS_NULL_FILE; // disabled
    // write ffmpeg info to log file.
    if (config_->get_ff_log_enabled()) {
        log_file = config_->get_ff_log_dir();
        log_file += "/";
        log_file += "ffmpeg-encoder";
        log_file += "-";
        log_file += req->vhost_;
        log_file += "-";
        log_file += req->app_;
        log_file += "-";
        log_file += req->stream_;
        if (!engine->args_.empty()) {
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
    pprint_->elapse();

    // reportable
    if (pprint_->can_print()) {
        // TODO: FIXME: show more info.
        srs_trace("-> " SRS_CONSTS_LOG_ENCODER " time=%" PRId64 ", encoders=%d, input=%s",
                  pprint_->age(), (int)ffmpegs_.size(), input_stream_name_.c_str());
    }
}
