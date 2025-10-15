//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_app_ng_exec.hpp>

#include <stdlib.h>
using namespace std;

#include <srs_app_config.hpp>
#include <srs_app_process.hpp>
#include <srs_app_utility.hpp>
#include <srs_kernel_consts.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_kernel_pithy_print.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_rtmp_stack.hpp>
#include <srs_protocol_utility.hpp>

ISrsNgExec::ISrsNgExec()
{
}

ISrsNgExec::~ISrsNgExec()
{
}

SrsNgExec::SrsNgExec()
{
    trd_ = new SrsDummyCoroutine();
    pprint_ = SrsPithyPrint::create_exec();

    config_ = _srs_config;
}

SrsNgExec::~SrsNgExec()
{
    on_unpublish();

    srs_freep(trd_);
    srs_freep(pprint_);

    config_ = NULL;
}

srs_error_t SrsNgExec::on_publish(ISrsRequest *req)
{
    srs_error_t err = srs_success;

    // when publish, parse the exec_publish.
    if ((err = parse_exec_publish(req)) != srs_success) {
        return srs_error_wrap(err, "exec publish");
    }

    // start thread to run all processes.
    srs_freep(trd_);
    trd_ = new SrsSTCoroutine("encoder", this, _srs_context->get_id());
    if ((err = trd_->start()) != srs_success) {
        return srs_error_wrap(err, "start thread");
    }

    return err;
}

void SrsNgExec::on_unpublish()
{
    trd_->stop();
    clear_exec_publish();
}

// when error, ng-exec sleep for a while and retry.
#define SRS_RTMP_EXEC_CIMS (3 * SRS_UTIME_SECONDS)
srs_error_t SrsNgExec::cycle()
{
    srs_error_t err = srs_success;

    while (true) {
        // We always check status first.
        // @see https://github.com/ossrs/srs/issues/1634#issuecomment-597571561
        if ((err = trd_->pull()) != srs_success) {
            err = srs_error_wrap(err, "ng exec cycle");
            break;
        }

        if ((err = do_cycle()) != srs_success) {
            srs_warn("EXEC: Ignore error, %s", srs_error_desc(err).c_str());
            srs_freep(err);
        }

        srs_usleep(SRS_RTMP_EXEC_CIMS);
    }

    std::vector<SrsProcess *>::iterator it;
    for (it = exec_publishs_.begin(); it != exec_publishs_.end(); ++it) {
        SrsProcess *ep = *it;
        ep->stop();
    }

    return err;
}

srs_error_t SrsNgExec::do_cycle()
{
    srs_error_t err = srs_success;

    // ignore when no exec.
    if (exec_publishs_.empty()) {
        return err;
    }

    std::vector<SrsProcess *>::iterator it;
    for (it = exec_publishs_.begin(); it != exec_publishs_.end(); ++it) {
        SrsProcess *process = *it;

        // start all processes.
        if ((err = process->start()) != srs_success) {
            return srs_error_wrap(err, "process start");
        }

        // check process status.
        if ((err = process->cycle()) != srs_success) {
            return srs_error_wrap(err, "process cycle");
        }
    }

    // pithy print
    show_exec_log_message();

    return err;
}

srs_error_t SrsNgExec::parse_exec_publish(ISrsRequest *req)
{
    srs_error_t err = srs_success;

    if (!config_->get_exec_enabled(req->vhost_)) {
        srs_trace("ignore disabled exec for vhost=%s", req->vhost_.c_str());
        return err;
    }

    // stream name: vhost/app/stream for print
    input_stream_name_ = req->vhost_;
    input_stream_name_ += "/";
    input_stream_name_ += req->app_;
    input_stream_name_ += "/";
    input_stream_name_ += req->stream_;

    std::vector<SrsConfDirective *> eps = config_->get_exec_publishs(req->vhost_);
    for (int i = 0; i < (int)eps.size(); i++) {
        SrsConfDirective *ep = eps.at(i);
        SrsProcess *process = new SrsProcess();

        std::string binary = ep->arg0();
        std::vector<std::string> argv;
        for (int i = 0; i < (int)ep->args_.size(); i++) {
            std::string epa = ep->args_.at(i);

            if (srs_strings_contains(epa, ">")) {
                vector<string> epas = srs_strings_split(epa, ">");
                for (int j = 0; j < (int)epas.size(); j++) {
                    argv.push_back(parse(req, epas.at(j)));
                    if (j == 0) {
                        argv.push_back(">");
                    }
                }
                continue;
            }

            argv.push_back(parse(req, epa));
        }

        if ((err = process->initialize(binary, argv)) != srs_success) {
            srs_freep(process);
            return srs_error_wrap(err, "initialize process failed, binary=%s, vhost=%s", binary.c_str(), req->vhost_.c_str());
        }

        exec_publishs_.push_back(process);
    }

    return err;
}

void SrsNgExec::clear_exec_publish()
{
    std::vector<SrsProcess *>::iterator it;
    for (it = exec_publishs_.begin(); it != exec_publishs_.end(); ++it) {
        SrsProcess *ep = *it;
        srs_freep(ep);
    }
    exec_publishs_.clear();
}

void SrsNgExec::show_exec_log_message()
{
    pprint_->elapse();

    // reportable
    if (pprint_->can_print()) {
        // TODO: FIXME: show more info.
        srs_trace("-> " SRS_CONSTS_LOG_EXEC " time=%" PRId64 ", publish=%d, input=%s",
                  pprint_->age(), (int)exec_publishs_.size(), input_stream_name_.c_str());
    }
}

string SrsNgExec::parse(ISrsRequest *req, string tmpl)
{
    string output = tmpl;

    output = srs_strings_replace(output, "[vhost]", req->vhost_);
    output = srs_strings_replace(output, "[port]", srs_strconv_format_int(req->port_));
    output = srs_strings_replace(output, "[app]", req->app_);
    output = srs_strings_replace(output, "[stream]", req->stream_);

    output = srs_strings_replace(output, "[tcUrl]", req->tcUrl_);
    output = srs_strings_replace(output, "[swfUrl]", req->swfUrl_);
    output = srs_strings_replace(output, "[pageUrl]", req->pageUrl_);

    output = srs_path_build_timestamp(output);

    if (output.find("[url]") != string::npos) {
        string url = srs_net_url_encode_rtmp_url(req->host_, req->port_, req->host_, req->vhost_, req->app_, req->stream_, req->param_);
        output = srs_strings_replace(output, "[url]", url);
    }

    return output;
}
