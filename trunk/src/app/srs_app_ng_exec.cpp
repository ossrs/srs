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

#include <srs_app_ng_exec.hpp>

#include <stdlib.h>
using namespace std;

#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_app_config.hpp>
#include <srs_rtmp_stack.hpp>
#include <srs_app_pithy_print.hpp>
#include <srs_app_process.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_kernel_consts.hpp>
#include <srs_protocol_utility.hpp>

// when error, ng-exec sleep for a while and retry.
#define SRS_RTMP_EXEC_CIMS (3000)

SrsNgExec::SrsNgExec()
{
    pthread = new SrsReusableThread("encoder", this, SRS_RTMP_EXEC_CIMS);
    pprint = SrsPithyPrint::create_exec();
}

SrsNgExec::~SrsNgExec()
{
    on_unpublish();
    
    srs_freep(pthread);
    srs_freep(pprint);
}

int SrsNgExec::on_publish(SrsRequest* req)
{
    int ret = ERROR_SUCCESS;
    
    // when publish, parse the exec_publish.
    if ((ret = parse_exec_publish(req)) != ERROR_SUCCESS) {
        return ret;
    }
    
    // start thread to run all processes.
    if ((ret = pthread->start()) != ERROR_SUCCESS) {
        srs_error("st_thread_create failed. ret=%d", ret);
        return ret;
    }
    srs_trace("exec thread cid=%d, current_cid=%d", pthread->cid(), _srs_context->get_id());
    
    return ret;
}

void SrsNgExec::on_unpublish()
{
    pthread->stop();
    clear_exec_publish();
}

int SrsNgExec::cycle()
{
    int ret = ERROR_SUCCESS;
    
    // ignore when no exec.
    if (exec_publishs.empty()) {
        return ret;
    }
    
    std::vector<SrsProcess*>::iterator it;
    for (it = exec_publishs.begin(); it != exec_publishs.end(); ++it) {
        SrsProcess* process = *it;
        
        // start all processes.
        if ((ret = process->start()) != ERROR_SUCCESS) {
            srs_error("exec publish start failed. ret=%d", ret);
            return ret;
        }
        
        // check process status.
        if ((ret = process->cycle()) != ERROR_SUCCESS) {
            srs_error("exec publish cycle failed. ret=%d", ret);
            return ret;
        }
    }
    
    // pithy print
    show_exec_log_message();
    
    return ret;
}

void SrsNgExec::on_thread_stop()
{
    std::vector<SrsProcess*>::iterator it;
    for (it = exec_publishs.begin(); it != exec_publishs.end(); ++it) {
        SrsProcess* ep = *it;
        ep->stop();
    }
}

int SrsNgExec::parse_exec_publish(SrsRequest* req)
{
    int ret = ERROR_SUCCESS;
    
    if (!_srs_config->get_exec_enabled(req->vhost)) {
        srs_trace("ignore disabled exec for vhost=%s", req->vhost.c_str());
        return ret;
    }
    
    // stream name: vhost/app/stream for print
    input_stream_name = req->vhost;
    input_stream_name += "/";
    input_stream_name += req->app;
    input_stream_name += "/";
    input_stream_name += req->stream;
    
    std::vector<SrsConfDirective*> eps = _srs_config->get_exec_publishs(req->vhost);
    for (int i = 0; i < (int)eps.size(); i++) {
        SrsConfDirective* ep = eps.at(i);
        SrsProcess* process = new SrsProcess();
        
        std::string binary = ep->arg0();
        std::vector<std::string> argv;
        for (int i = 0; i < (int)ep->args.size(); i++) {
            std::string epa = ep->args.at(i);
            
            if (srs_string_contains(epa, ">")) {
                vector<string> epas = srs_string_split(epa, ">");
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
        
        if ((ret = process->initialize(binary, argv)) != ERROR_SUCCESS) {
            srs_freep(process);
            srs_error("initialize process failed, binary=%s, vhost=%s, ret=%d", binary.c_str(), req->vhost.c_str(), ret);
            return ret;
        }
        
        exec_publishs.push_back(process);
    }
    
    return ret;
}

void SrsNgExec::clear_exec_publish()
{
    std::vector<SrsProcess*>::iterator it;
    for (it = exec_publishs.begin(); it != exec_publishs.end(); ++it) {
        SrsProcess* ep = *it;
        srs_freep(ep);
    }
    exec_publishs.clear();
}

void SrsNgExec::show_exec_log_message()
{
    pprint->elapse();
    
    // reportable
    if (pprint->can_print()) {
        // TODO: FIXME: show more info.
        srs_trace("-> "SRS_CONSTS_LOG_EXEC" time=%"PRId64", publish=%d, input=%s",
                  pprint->age(), (int)exec_publishs.size(), input_stream_name.c_str());
    }
}

string SrsNgExec::parse(SrsRequest* req, string tmpl)
{
    string output = tmpl;
    
    output = srs_string_replace(output, "[vhost]", req->vhost);
    output = srs_string_replace(output, "[port]", srs_int2str(req->port));
    output = srs_string_replace(output, "[app]", req->app);
    output = srs_string_replace(output, "[stream]", req->stream);
    
    output = srs_string_replace(output, "[tcUrl]", req->tcUrl);
    output = srs_string_replace(output, "[swfUrl]", req->swfUrl);
    output = srs_string_replace(output, "[pageUrl]", req->pageUrl);
    
    if (output.find("[url]") != string::npos) {
        string url = srs_generate_rtmp_url(req->host, req->port, req->vhost, req->app, req->stream);
        output = srs_string_replace(output, "[url]", url);
    }
    
    return output;
}

