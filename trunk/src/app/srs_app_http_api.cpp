/*
The MIT License (MIT)

Copyright (c) 2013-2015 winlin

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

#include <srs_app_http_api.hpp>

#ifdef SRS_AUTO_HTTP_API

#include <sstream>
using namespace std;

#include <srs_kernel_log.hpp>
#include <srs_kernel_error.hpp>
#include <srs_app_st_socket.hpp>
#include <srs_core_autofree.hpp>
#include <srs_app_json.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_app_utility.hpp>
#include <srs_app_statistic.hpp>
#include <srs_rtmp_sdk.hpp>

SrsGoApiRoot::SrsGoApiRoot()
{
}

SrsGoApiRoot::~SrsGoApiRoot()
{
}

int SrsGoApiRoot::serve_http(ISrsGoHttpResponseWriter* w, SrsHttpMessage* r)
{
    std::stringstream ss;
    
    ss << __SRS_JOBJECT_START
        << __SRS_JFIELD_ERROR(ERROR_SUCCESS) << __SRS_JFIELD_CONT
        << __SRS_JFIELD_ORG("urls", __SRS_JOBJECT_START)
            << __SRS_JFIELD_STR("api", "the api root")
        << __SRS_JOBJECT_END
        << __SRS_JOBJECT_END;
        
    return srs_go_http_response_json(w, ss.str());
}

SrsGoApiApi::SrsGoApiApi()
{
}

SrsGoApiApi::~SrsGoApiApi()
{
}

int SrsGoApiApi::serve_http(ISrsGoHttpResponseWriter* w, SrsHttpMessage* r)
{
    std::stringstream ss;
    
    ss << __SRS_JOBJECT_START
        << __SRS_JFIELD_ERROR(ERROR_SUCCESS) << __SRS_JFIELD_CONT
        << __SRS_JFIELD_ORG("urls", __SRS_JOBJECT_START)
            << __SRS_JFIELD_STR("v1", "the api version 1.0")
        << __SRS_JOBJECT_END
        << __SRS_JOBJECT_END;
        
    return srs_go_http_response_json(w, ss.str());
}

SrsGoApiV1::SrsGoApiV1()
{
}

SrsGoApiV1::~SrsGoApiV1()
{
}

int SrsGoApiV1::serve_http(ISrsGoHttpResponseWriter* w, SrsHttpMessage* r)
{
    std::stringstream ss;
    
    ss << __SRS_JOBJECT_START
        << __SRS_JFIELD_ERROR(ERROR_SUCCESS) << __SRS_JFIELD_CONT
        << __SRS_JFIELD_ORG("urls", __SRS_JOBJECT_START)
            << __SRS_JFIELD_STR("versions", "the version of SRS") << __SRS_JFIELD_CONT
            << __SRS_JFIELD_STR("summaries", "the summary(pid, argv, pwd, cpu, mem) of SRS") << __SRS_JFIELD_CONT
            << __SRS_JFIELD_STR("rusages", "the rusage of SRS") << __SRS_JFIELD_CONT
            << __SRS_JFIELD_STR("self_proc_stats", "the self process stats") << __SRS_JFIELD_CONT
            << __SRS_JFIELD_STR("system_proc_stats", "the system process stats") << __SRS_JFIELD_CONT
            << __SRS_JFIELD_STR("meminfos", "the meminfo of system") << __SRS_JFIELD_CONT
            << __SRS_JFIELD_STR("authors", "the primary authors and contributors") << __SRS_JFIELD_CONT
            << __SRS_JFIELD_STR("requests", "the request itself, for http debug") << __SRS_JFIELD_CONT
            << __SRS_JFIELD_STR("vhosts", "dumps vhost to json") << __SRS_JFIELD_CONT
            << __SRS_JFIELD_STR("streams", "dumps streams to json")
        << __SRS_JOBJECT_END
        << __SRS_JOBJECT_END;
    
    return srs_go_http_response_json(w, ss.str());
}

SrsGoApiVersion::SrsGoApiVersion()
{
}

SrsGoApiVersion::~SrsGoApiVersion()
{
}

int SrsGoApiVersion::serve_http(ISrsGoHttpResponseWriter* w, SrsHttpMessage* r)
{
    std::stringstream ss;
    
    ss << __SRS_JOBJECT_START
        << __SRS_JFIELD_ERROR(ERROR_SUCCESS) << __SRS_JFIELD_CONT
        << __SRS_JFIELD_ORG("data", __SRS_JOBJECT_START)
            << __SRS_JFIELD_ORG("major", VERSION_MAJOR) << __SRS_JFIELD_CONT
            << __SRS_JFIELD_ORG("minor", VERSION_MINOR) << __SRS_JFIELD_CONT
            << __SRS_JFIELD_ORG("revision", VERSION_REVISION) << __SRS_JFIELD_CONT
            << __SRS_JFIELD_STR("version", RTMP_SIG_SRS_VERSION)
        << __SRS_JOBJECT_END
        << __SRS_JOBJECT_END;
    
    return srs_go_http_response_json(w, ss.str());
}

SrsGoApiSummaries::SrsGoApiSummaries()
{
}

SrsGoApiSummaries::~SrsGoApiSummaries()
{
}

int SrsGoApiSummaries::serve_http(ISrsGoHttpResponseWriter* w, SrsHttpMessage* r)
{
    std::stringstream ss;
    srs_api_dump_summaries(ss);
    return srs_go_http_response_json(w, ss.str());
}

SrsGoApiRusages::SrsGoApiRusages()
{
}

SrsGoApiRusages::~SrsGoApiRusages()
{
}

int SrsGoApiRusages::serve_http(ISrsGoHttpResponseWriter* w, SrsHttpMessage* req)
{
    std::stringstream ss;
    
    SrsRusage* r = srs_get_system_rusage();
    
    ss << __SRS_JOBJECT_START
        << __SRS_JFIELD_ERROR(ERROR_SUCCESS) << __SRS_JFIELD_CONT
        << __SRS_JFIELD_ORG("data", __SRS_JOBJECT_START)
            << __SRS_JFIELD_ORG("ok", (r->ok? "true":"false")) << __SRS_JFIELD_CONT
            << __SRS_JFIELD_ORG("sample_time", r->sample_time) << __SRS_JFIELD_CONT
            << __SRS_JFIELD_ORG("ru_utime", r->r.ru_utime.tv_sec) << __SRS_JFIELD_CONT
            << __SRS_JFIELD_ORG("ru_stime", r->r.ru_stime.tv_sec) << __SRS_JFIELD_CONT
            << __SRS_JFIELD_ORG("ru_maxrss", r->r.ru_maxrss) << __SRS_JFIELD_CONT
            << __SRS_JFIELD_ORG("ru_ixrss", r->r.ru_ixrss) << __SRS_JFIELD_CONT
            << __SRS_JFIELD_ORG("ru_idrss", r->r.ru_idrss) << __SRS_JFIELD_CONT
            << __SRS_JFIELD_ORG("ru_isrss", r->r.ru_isrss) << __SRS_JFIELD_CONT
            << __SRS_JFIELD_ORG("ru_minflt", r->r.ru_minflt) << __SRS_JFIELD_CONT
            << __SRS_JFIELD_ORG("ru_majflt", r->r.ru_majflt) << __SRS_JFIELD_CONT
            << __SRS_JFIELD_ORG("ru_nswap", r->r.ru_nswap) << __SRS_JFIELD_CONT
            << __SRS_JFIELD_ORG("ru_inblock", r->r.ru_inblock) << __SRS_JFIELD_CONT
            << __SRS_JFIELD_ORG("ru_oublock", r->r.ru_oublock) << __SRS_JFIELD_CONT
            << __SRS_JFIELD_ORG("ru_msgsnd", r->r.ru_msgsnd) << __SRS_JFIELD_CONT
            << __SRS_JFIELD_ORG("ru_msgrcv", r->r.ru_msgrcv) << __SRS_JFIELD_CONT
            << __SRS_JFIELD_ORG("ru_nsignals", r->r.ru_nsignals) << __SRS_JFIELD_CONT
            << __SRS_JFIELD_ORG("ru_nvcsw", r->r.ru_nvcsw) << __SRS_JFIELD_CONT
            << __SRS_JFIELD_ORG("ru_nivcsw", r->r.ru_nivcsw)
        << __SRS_JOBJECT_END
        << __SRS_JOBJECT_END;
    
    return srs_go_http_response_json(w, ss.str());
}

SrsGoApiSelfProcStats::SrsGoApiSelfProcStats()
{
}

SrsGoApiSelfProcStats::~SrsGoApiSelfProcStats()
{
}

int SrsGoApiSelfProcStats::serve_http(ISrsGoHttpResponseWriter* w, SrsHttpMessage* r)
{
    std::stringstream ss;
    
    SrsProcSelfStat* u = srs_get_self_proc_stat();
    
    ss << __SRS_JOBJECT_START
        << __SRS_JFIELD_ERROR(ERROR_SUCCESS) << __SRS_JFIELD_CONT
        << __SRS_JFIELD_ORG("data", __SRS_JOBJECT_START)
            << __SRS_JFIELD_ORG("ok", (u->ok? "true":"false")) << __SRS_JFIELD_CONT
            << __SRS_JFIELD_ORG("sample_time", u->sample_time) << __SRS_JFIELD_CONT
            << __SRS_JFIELD_ORG("percent", u->percent) << __SRS_JFIELD_CONT
            << __SRS_JFIELD_ORG("pid", u->pid) << __SRS_JFIELD_CONT
            << __SRS_JFIELD_STR("comm", u->comm) << __SRS_JFIELD_CONT
            << __SRS_JFIELD_STR("state", u->state) << __SRS_JFIELD_CONT
            << __SRS_JFIELD_ORG("ppid", u->ppid) << __SRS_JFIELD_CONT
            << __SRS_JFIELD_ORG("pgrp", u->pgrp) << __SRS_JFIELD_CONT
            << __SRS_JFIELD_ORG("session", u->session) << __SRS_JFIELD_CONT
            << __SRS_JFIELD_ORG("tty_nr", u->tty_nr) << __SRS_JFIELD_CONT
            << __SRS_JFIELD_ORG("tpgid", u->tpgid) << __SRS_JFIELD_CONT
            << __SRS_JFIELD_ORG("flags", u->flags) << __SRS_JFIELD_CONT
            << __SRS_JFIELD_ORG("minflt", u->minflt) << __SRS_JFIELD_CONT
            << __SRS_JFIELD_ORG("cminflt", u->cminflt) << __SRS_JFIELD_CONT
            << __SRS_JFIELD_ORG("majflt", u->majflt) << __SRS_JFIELD_CONT
            << __SRS_JFIELD_ORG("cmajflt", u->cmajflt) << __SRS_JFIELD_CONT
            << __SRS_JFIELD_ORG("utime", u->utime) << __SRS_JFIELD_CONT
            << __SRS_JFIELD_ORG("stime", u->stime) << __SRS_JFIELD_CONT
            << __SRS_JFIELD_ORG("cutime", u->cutime) << __SRS_JFIELD_CONT
            << __SRS_JFIELD_ORG("cstime", u->cstime) << __SRS_JFIELD_CONT
            << __SRS_JFIELD_ORG("priority", u->priority) << __SRS_JFIELD_CONT
            << __SRS_JFIELD_ORG("nice", u->nice) << __SRS_JFIELD_CONT
            << __SRS_JFIELD_ORG("num_threads", u->num_threads) << __SRS_JFIELD_CONT
            << __SRS_JFIELD_ORG("itrealvalue", u->itrealvalue) << __SRS_JFIELD_CONT
            << __SRS_JFIELD_ORG("starttime", u->starttime) << __SRS_JFIELD_CONT
            << __SRS_JFIELD_ORG("vsize", u->vsize) << __SRS_JFIELD_CONT
            << __SRS_JFIELD_ORG("rss", u->rss) << __SRS_JFIELD_CONT
            << __SRS_JFIELD_ORG("rsslim", u->rsslim) << __SRS_JFIELD_CONT
            << __SRS_JFIELD_ORG("startcode", u->startcode) << __SRS_JFIELD_CONT
            << __SRS_JFIELD_ORG("endcode", u->endcode) << __SRS_JFIELD_CONT
            << __SRS_JFIELD_ORG("startstack", u->startstack) << __SRS_JFIELD_CONT
            << __SRS_JFIELD_ORG("kstkesp", u->kstkesp) << __SRS_JFIELD_CONT
            << __SRS_JFIELD_ORG("kstkeip", u->kstkeip) << __SRS_JFIELD_CONT
            << __SRS_JFIELD_ORG("signal", u->signal) << __SRS_JFIELD_CONT
            << __SRS_JFIELD_ORG("blocked", u->blocked) << __SRS_JFIELD_CONT
            << __SRS_JFIELD_ORG("sigignore", u->sigignore) << __SRS_JFIELD_CONT
            << __SRS_JFIELD_ORG("sigcatch", u->sigcatch) << __SRS_JFIELD_CONT
            << __SRS_JFIELD_ORG("wchan", u->wchan) << __SRS_JFIELD_CONT
            << __SRS_JFIELD_ORG("nswap", u->nswap) << __SRS_JFIELD_CONT
            << __SRS_JFIELD_ORG("cnswap", u->cnswap) << __SRS_JFIELD_CONT
            << __SRS_JFIELD_ORG("exit_signal", u->exit_signal) << __SRS_JFIELD_CONT
            << __SRS_JFIELD_ORG("processor", u->processor) << __SRS_JFIELD_CONT
            << __SRS_JFIELD_ORG("rt_priority", u->rt_priority) << __SRS_JFIELD_CONT
            << __SRS_JFIELD_ORG("policy", u->policy) << __SRS_JFIELD_CONT
            << __SRS_JFIELD_ORG("delayacct_blkio_ticks", u->delayacct_blkio_ticks) << __SRS_JFIELD_CONT
            << __SRS_JFIELD_ORG("guest_time", u->guest_time) << __SRS_JFIELD_CONT
            << __SRS_JFIELD_ORG("cguest_time", u->cguest_time)
        << __SRS_JOBJECT_END
        << __SRS_JOBJECT_END;
    
    return srs_go_http_response_json(w, ss.str());
}

SrsGoApiSystemProcStats::SrsGoApiSystemProcStats()
{
}

SrsGoApiSystemProcStats::~SrsGoApiSystemProcStats()
{
}

int SrsGoApiSystemProcStats::serve_http(ISrsGoHttpResponseWriter* w, SrsHttpMessage* r)
{
    std::stringstream ss;
    
    SrsProcSystemStat* s = srs_get_system_proc_stat();
    
    ss << __SRS_JOBJECT_START
        << __SRS_JFIELD_ERROR(ERROR_SUCCESS) << __SRS_JFIELD_CONT
        << __SRS_JFIELD_ORG("data", __SRS_JOBJECT_START)
            << __SRS_JFIELD_ORG("ok", (s->ok? "true":"false")) << __SRS_JFIELD_CONT
            << __SRS_JFIELD_ORG("sample_time", s->sample_time) << __SRS_JFIELD_CONT
            << __SRS_JFIELD_ORG("percent", s->percent) << __SRS_JFIELD_CONT
            << __SRS_JFIELD_ORG("user", s->user) << __SRS_JFIELD_CONT
            << __SRS_JFIELD_ORG("nice", s->nice) << __SRS_JFIELD_CONT
            << __SRS_JFIELD_ORG("sys", s->sys) << __SRS_JFIELD_CONT
            << __SRS_JFIELD_ORG("idle", s->idle) << __SRS_JFIELD_CONT
            << __SRS_JFIELD_ORG("iowait", s->iowait) << __SRS_JFIELD_CONT
            << __SRS_JFIELD_ORG("irq", s->irq) << __SRS_JFIELD_CONT
            << __SRS_JFIELD_ORG("softirq", s->softirq) << __SRS_JFIELD_CONT
            << __SRS_JFIELD_ORG("steal", s->steal) << __SRS_JFIELD_CONT
            << __SRS_JFIELD_ORG("guest", s->guest)
        << __SRS_JOBJECT_END
        << __SRS_JOBJECT_END;
    
    return srs_go_http_response_json(w, ss.str());
}

SrsGoApiMemInfos::SrsGoApiMemInfos()
{
}

SrsGoApiMemInfos::~SrsGoApiMemInfos()
{
}

int SrsGoApiMemInfos::serve_http(ISrsGoHttpResponseWriter* w, SrsHttpMessage* r)
{
    std::stringstream ss;
    
    SrsMemInfo* m = srs_get_meminfo();
    
    ss << __SRS_JOBJECT_START
        << __SRS_JFIELD_ERROR(ERROR_SUCCESS) << __SRS_JFIELD_CONT
        << __SRS_JFIELD_ORG("data", __SRS_JOBJECT_START)
            << __SRS_JFIELD_ORG("ok", (m->ok? "true":"false")) << __SRS_JFIELD_CONT
            << __SRS_JFIELD_ORG("sample_time", m->sample_time) << __SRS_JFIELD_CONT
            << __SRS_JFIELD_ORG("percent_ram", m->percent_ram) << __SRS_JFIELD_CONT
            << __SRS_JFIELD_ORG("percent_swap", m->percent_swap) << __SRS_JFIELD_CONT
            << __SRS_JFIELD_ORG("MemActive", m->MemActive) << __SRS_JFIELD_CONT
            << __SRS_JFIELD_ORG("RealInUse", m->RealInUse) << __SRS_JFIELD_CONT
            << __SRS_JFIELD_ORG("NotInUse", m->NotInUse) << __SRS_JFIELD_CONT
            << __SRS_JFIELD_ORG("MemTotal", m->MemTotal) << __SRS_JFIELD_CONT
            << __SRS_JFIELD_ORG("MemFree", m->MemFree) << __SRS_JFIELD_CONT
            << __SRS_JFIELD_ORG("Buffers", m->Buffers) << __SRS_JFIELD_CONT
            << __SRS_JFIELD_ORG("Cached", m->Cached) << __SRS_JFIELD_CONT
            << __SRS_JFIELD_ORG("SwapTotal", m->SwapTotal) << __SRS_JFIELD_CONT
            << __SRS_JFIELD_ORG("SwapFree", m->SwapFree)
        << __SRS_JOBJECT_END
        << __SRS_JOBJECT_END;
    
    return srs_go_http_response_json(w, ss.str());
}

SrsGoApiAuthors::SrsGoApiAuthors()
{
}

SrsGoApiAuthors::~SrsGoApiAuthors()
{
}

int SrsGoApiAuthors::serve_http(ISrsGoHttpResponseWriter* w, SrsHttpMessage* r)
{
    std::stringstream ss;
    
    ss << __SRS_JOBJECT_START
        << __SRS_JFIELD_ERROR(ERROR_SUCCESS) << __SRS_JFIELD_CONT
        << __SRS_JFIELD_ORG("data", __SRS_JOBJECT_START)
            << __SRS_JFIELD_STR("primary", RTMP_SIG_SRS_PRIMARY) << __SRS_JFIELD_CONT
            << __SRS_JFIELD_STR("authors", RTMP_SIG_SRS_AUTHROS) << __SRS_JFIELD_CONT
            << __SRS_JFIELD_STR("contributors_link", RTMP_SIG_SRS_CONTRIBUTORS_URL) << __SRS_JFIELD_CONT
            << __SRS_JFIELD_STR("contributors", SRS_AUTO_CONSTRIBUTORS)
        << __SRS_JOBJECT_END
        << __SRS_JOBJECT_END;
    
    return srs_go_http_response_json(w, ss.str());
}

SrsGoApiRequests::SrsGoApiRequests()
{
}

SrsGoApiRequests::~SrsGoApiRequests()
{
}

int SrsGoApiRequests::serve_http(ISrsGoHttpResponseWriter* w, SrsHttpMessage* r)
{
    SrsHttpMessage* req = r;
    
    std::stringstream ss;
    
    ss << __SRS_JOBJECT_START
        << __SRS_JFIELD_ERROR(ERROR_SUCCESS) << __SRS_JFIELD_CONT
        << __SRS_JFIELD_ORG("data", __SRS_JOBJECT_START)
            << __SRS_JFIELD_STR("uri", req->uri()) << __SRS_JFIELD_CONT
            << __SRS_JFIELD_STR("path", req->path()) << __SRS_JFIELD_CONT;
    
    // method
    if (req->is_http_get()) {
        ss  << __SRS_JFIELD_STR("METHOD", "GET");
    } else if (req->is_http_post()) {
        ss  << __SRS_JFIELD_STR("METHOD", "POST");
    } else if (req->is_http_put()) {
        ss  << __SRS_JFIELD_STR("METHOD", "PUT");
    } else if (req->is_http_delete()) {
        ss  << __SRS_JFIELD_STR("METHOD", "DELETE");
    } else {
        ss  << __SRS_JFIELD_ORG("METHOD", req->method());
    }
    ss << __SRS_JFIELD_CONT;
    
    // request headers
    ss      << __SRS_JFIELD_NAME("headers") << __SRS_JOBJECT_START;
    for (int i = 0; i < req->request_header_count(); i++) {
        std::string key = req->request_header_key_at(i);
        std::string value = req->request_header_value_at(i);
        if ( i < req->request_header_count() - 1) {
            ss      << __SRS_JFIELD_STR(key, value) << __SRS_JFIELD_CONT;
        } else {
            ss      << __SRS_JFIELD_STR(key, value);
        }
    }
    ss      << __SRS_JOBJECT_END << __SRS_JFIELD_CONT;
    
    // server informations
    ss      << __SRS_JFIELD_NAME("server") << __SRS_JOBJECT_START
                << __SRS_JFIELD_STR("sigature", RTMP_SIG_SRS_KEY) << __SRS_JFIELD_CONT
                << __SRS_JFIELD_STR("name", RTMP_SIG_SRS_NAME) << __SRS_JFIELD_CONT
                << __SRS_JFIELD_STR("version", RTMP_SIG_SRS_VERSION) << __SRS_JFIELD_CONT
                << __SRS_JFIELD_STR("link", RTMP_SIG_SRS_URL) << __SRS_JFIELD_CONT
                << __SRS_JFIELD_ORG("time", srs_get_system_time_ms())
            << __SRS_JOBJECT_END
        << __SRS_JOBJECT_END
        << __SRS_JOBJECT_END;
    
    return srs_go_http_response_json(w, ss.str());
}

SrsGoApiVhosts::SrsGoApiVhosts()
{
}

SrsGoApiVhosts::~SrsGoApiVhosts()
{
}

int SrsGoApiVhosts::serve_http(ISrsGoHttpResponseWriter* w, SrsHttpMessage* r)
{
    std::stringstream data;
    SrsStatistic* stat = SrsStatistic::instance();
    int ret = stat->dumps_vhosts(data);
    
    std::stringstream ss;
    
    ss << __SRS_JOBJECT_START
        << __SRS_JFIELD_ERROR(ret) << __SRS_JFIELD_CONT
        << __SRS_JFIELD_ORG("server", stat->server_id()) << __SRS_JFIELD_CONT
        << __SRS_JFIELD_ORG("vhosts", data.str())
        << __SRS_JOBJECT_END;
    
    return srs_go_http_response_json(w, ss.str());
}

SrsGoApiStreams::SrsGoApiStreams()
{
}

SrsGoApiStreams::~SrsGoApiStreams()
{
}

int SrsGoApiStreams::serve_http(ISrsGoHttpResponseWriter* w, SrsHttpMessage* r)
{
    std::stringstream data;
    SrsStatistic* stat = SrsStatistic::instance();
    int ret = stat->dumps_streams(data);
    
    std::stringstream ss;
    
    ss << __SRS_JOBJECT_START
        << __SRS_JFIELD_ERROR(ret) << __SRS_JFIELD_CONT
        << __SRS_JFIELD_ORG("server", stat->server_id()) << __SRS_JFIELD_CONT
        << __SRS_JFIELD_ORG("streams", data.str())
        << __SRS_JOBJECT_END;
    
    return srs_go_http_response_json(w, ss.str());
}

SrsHttpApi::SrsHttpApi(SrsServer* svr, st_netfd_t fd, SrsGoHttpServeMux* m) 
    : SrsConnection(svr, fd)
{
    mux = m;
    parser = new SrsHttpParser();
}

SrsHttpApi::~SrsHttpApi()
{
    srs_freep(parser);
}

void SrsHttpApi::kbps_resample()
{
    // TODO: FIXME: implements it
}

int64_t SrsHttpApi::get_send_bytes_delta()
{
    // TODO: FIXME: implements it
    return 0;
}

int64_t SrsHttpApi::get_recv_bytes_delta()
{
    // TODO: FIXME: implements it
    return 0;
}

int SrsHttpApi::do_cycle()
{
    int ret = ERROR_SUCCESS;
    
    srs_trace("api get peer ip success. ip=%s", ip.c_str());
    
    // initialize parser
    if ((ret = parser->initialize(HTTP_REQUEST)) != ERROR_SUCCESS) {
        srs_error("api initialize http parser failed. ret=%d", ret);
        return ret;
    }
    
    // underlayer socket
    SrsStSocket skt(stfd);
    
    // process http messages.
    for (;;) {
        SrsHttpMessage* req = NULL;
        
        // get a http message
        if ((ret = parser->parse_message(&skt, &req)) != ERROR_SUCCESS) {
            return ret;
        }

        // if SUCCESS, always NOT-NULL and completed message.
        srs_assert(req);
        srs_assert(req->is_complete());
        
        // always free it in this scope.
        SrsAutoFree(SrsHttpMessage, req);
        
        // ok, handle http request.
        SrsGoHttpResponseWriter writer(&skt);
        if ((ret = process_request(&writer, req)) != ERROR_SUCCESS) {
            return ret;
        }
    }
        
    return ret;
}

int SrsHttpApi::process_request(ISrsGoHttpResponseWriter* w, SrsHttpMessage* r) 
{
    int ret = ERROR_SUCCESS;
    
    srs_trace("HTTP %s %s, content-length=%"PRId64"", 
        r->method_str().c_str(), r->url().c_str(), r->content_length());
    
    // use default server mux to serve http request.
    if ((ret = mux->serve_http(w, r)) != ERROR_SUCCESS) {
        if (!srs_is_client_gracefully_close(ret)) {
            srs_error("serve http msg failed. ret=%d", ret);
        }
        return ret;
    }
    
    return ret;
}

#endif

