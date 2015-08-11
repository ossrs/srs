/*
The MIT License (MIT)

Copyright (c) 2013-2015 SRS(simple-rtmp-server)

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
#include <srs_app_st.hpp>
#include <srs_core_autofree.hpp>
#include <srs_app_json.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_app_utility.hpp>
#include <srs_app_statistic.hpp>
#include <srs_rtmp_stack.hpp>
#include <srs_app_dvr.hpp>
#include <srs_app_config.hpp>
#include <srs_app_http_conn.hpp>

SrsGoApiRoot::SrsGoApiRoot()
{
}

SrsGoApiRoot::~SrsGoApiRoot()
{
}

int SrsGoApiRoot::serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r)
{
    std::stringstream ss;
    
    ss << SRS_JOBJECT_START
        << SRS_JFIELD_ERROR(ERROR_SUCCESS) << SRS_JFIELD_CONT
        << SRS_JFIELD_ORG("urls", SRS_JOBJECT_START)
            << SRS_JFIELD_STR("api", "the api root")
        << SRS_JOBJECT_END
        << SRS_JOBJECT_END;
        
    return srs_http_response_json(w, ss.str());
}

SrsGoApiApi::SrsGoApiApi()
{
}

SrsGoApiApi::~SrsGoApiApi()
{
}

int SrsGoApiApi::serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r)
{
    std::stringstream ss;
    
    ss << SRS_JOBJECT_START
        << SRS_JFIELD_ERROR(ERROR_SUCCESS) << SRS_JFIELD_CONT
        << SRS_JFIELD_ORG("urls", SRS_JOBJECT_START)
            << SRS_JFIELD_STR("v1", "the api version 1.0")
        << SRS_JOBJECT_END
        << SRS_JOBJECT_END;
        
    return srs_http_response_json(w, ss.str());
}

SrsGoApiV1::SrsGoApiV1()
{
}

SrsGoApiV1::~SrsGoApiV1()
{
}

int SrsGoApiV1::serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r)
{
    std::stringstream ss;
    
    ss << SRS_JOBJECT_START
        << SRS_JFIELD_ERROR(ERROR_SUCCESS) << SRS_JFIELD_CONT
        << SRS_JFIELD_ORG("urls", SRS_JOBJECT_START)
            << SRS_JFIELD_STR("versions", "the version of SRS") << SRS_JFIELD_CONT
            << SRS_JFIELD_STR("summaries", "the summary(pid, argv, pwd, cpu, mem) of SRS") << SRS_JFIELD_CONT
            << SRS_JFIELD_STR("rusages", "the rusage of SRS") << SRS_JFIELD_CONT
            << SRS_JFIELD_STR("self_proc_stats", "the self process stats") << SRS_JFIELD_CONT
            << SRS_JFIELD_STR("system_proc_stats", "the system process stats") << SRS_JFIELD_CONT
            << SRS_JFIELD_STR("meminfos", "the meminfo of system") << SRS_JFIELD_CONT
            << SRS_JFIELD_STR("authors", "the primary authors and contributors") << SRS_JFIELD_CONT
            << SRS_JFIELD_STR("requests", "the request itself, for http debug") << SRS_JFIELD_CONT
            << SRS_JFIELD_STR("vhosts", "dumps vhost to json") << SRS_JFIELD_CONT
            << SRS_JFIELD_STR("streams", "dumps streams to json") << SRS_JFIELD_CONT
            << SRS_JFIELD_ORG("test", SRS_JOBJECT_START)
                << SRS_JFIELD_STR("requests", "show the request info") << SRS_JFIELD_CONT
                << SRS_JFIELD_STR("errors", "always return an error 100") << SRS_JFIELD_CONT
                << SRS_JFIELD_STR("redirects", "always redirect to /api/v1/test/errors") << SRS_JFIELD_CONT
                << SRS_JFIELD_STR(".vhost.", "http vhost for error.srs.com/api/v1/test/errors")
            << SRS_JOBJECT_END
        << SRS_JOBJECT_END
        << SRS_JOBJECT_END;
    
    return srs_http_response_json(w, ss.str());
}

SrsGoApiVersion::SrsGoApiVersion()
{
}

SrsGoApiVersion::~SrsGoApiVersion()
{
}

int SrsGoApiVersion::serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r)
{
    std::stringstream ss;
    
    ss << SRS_JOBJECT_START
        << SRS_JFIELD_ERROR(ERROR_SUCCESS) << SRS_JFIELD_CONT
        << SRS_JFIELD_ORG("data", SRS_JOBJECT_START)
            << SRS_JFIELD_ORG("major", VERSION_MAJOR) << SRS_JFIELD_CONT
            << SRS_JFIELD_ORG("minor", VERSION_MINOR) << SRS_JFIELD_CONT
            << SRS_JFIELD_ORG("revision", VERSION_REVISION) << SRS_JFIELD_CONT
            << SRS_JFIELD_STR("version", RTMP_SIG_SRS_VERSION)
        << SRS_JOBJECT_END
        << SRS_JOBJECT_END;
    
    return srs_http_response_json(w, ss.str());
}

SrsGoApiSummaries::SrsGoApiSummaries()
{
}

SrsGoApiSummaries::~SrsGoApiSummaries()
{
}

int SrsGoApiSummaries::serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r)
{
    std::stringstream ss;
    srs_api_dump_summaries(ss);
    return srs_http_response_json(w, ss.str());
}

SrsGoApiRusages::SrsGoApiRusages()
{
}

SrsGoApiRusages::~SrsGoApiRusages()
{
}

int SrsGoApiRusages::serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* req)
{
    std::stringstream ss;
    
    SrsRusage* r = srs_get_system_rusage();
    
    ss << SRS_JOBJECT_START
        << SRS_JFIELD_ERROR(ERROR_SUCCESS) << SRS_JFIELD_CONT
        << SRS_JFIELD_ORG("data", SRS_JOBJECT_START)
            << SRS_JFIELD_ORG("ok", (r->ok? "true":"false")) << SRS_JFIELD_CONT
            << SRS_JFIELD_ORG("sample_time", r->sample_time) << SRS_JFIELD_CONT
            << SRS_JFIELD_ORG("ru_utime", r->r.ru_utime.tv_sec) << SRS_JFIELD_CONT
            << SRS_JFIELD_ORG("ru_stime", r->r.ru_stime.tv_sec) << SRS_JFIELD_CONT
            << SRS_JFIELD_ORG("ru_maxrss", r->r.ru_maxrss) << SRS_JFIELD_CONT
            << SRS_JFIELD_ORG("ru_ixrss", r->r.ru_ixrss) << SRS_JFIELD_CONT
            << SRS_JFIELD_ORG("ru_idrss", r->r.ru_idrss) << SRS_JFIELD_CONT
            << SRS_JFIELD_ORG("ru_isrss", r->r.ru_isrss) << SRS_JFIELD_CONT
            << SRS_JFIELD_ORG("ru_minflt", r->r.ru_minflt) << SRS_JFIELD_CONT
            << SRS_JFIELD_ORG("ru_majflt", r->r.ru_majflt) << SRS_JFIELD_CONT
            << SRS_JFIELD_ORG("ru_nswap", r->r.ru_nswap) << SRS_JFIELD_CONT
            << SRS_JFIELD_ORG("ru_inblock", r->r.ru_inblock) << SRS_JFIELD_CONT
            << SRS_JFIELD_ORG("ru_oublock", r->r.ru_oublock) << SRS_JFIELD_CONT
            << SRS_JFIELD_ORG("ru_msgsnd", r->r.ru_msgsnd) << SRS_JFIELD_CONT
            << SRS_JFIELD_ORG("ru_msgrcv", r->r.ru_msgrcv) << SRS_JFIELD_CONT
            << SRS_JFIELD_ORG("ru_nsignals", r->r.ru_nsignals) << SRS_JFIELD_CONT
            << SRS_JFIELD_ORG("ru_nvcsw", r->r.ru_nvcsw) << SRS_JFIELD_CONT
            << SRS_JFIELD_ORG("ru_nivcsw", r->r.ru_nivcsw)
        << SRS_JOBJECT_END
        << SRS_JOBJECT_END;
    
    return srs_http_response_json(w, ss.str());
}

SrsGoApiSelfProcStats::SrsGoApiSelfProcStats()
{
}

SrsGoApiSelfProcStats::~SrsGoApiSelfProcStats()
{
}

int SrsGoApiSelfProcStats::serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r)
{
    std::stringstream ss;
    
    SrsProcSelfStat* u = srs_get_self_proc_stat();
    
    ss << SRS_JOBJECT_START
        << SRS_JFIELD_ERROR(ERROR_SUCCESS) << SRS_JFIELD_CONT
        << SRS_JFIELD_ORG("data", SRS_JOBJECT_START)
            << SRS_JFIELD_ORG("ok", (u->ok? "true":"false")) << SRS_JFIELD_CONT
            << SRS_JFIELD_ORG("sample_time", u->sample_time) << SRS_JFIELD_CONT
            << SRS_JFIELD_ORG("percent", u->percent) << SRS_JFIELD_CONT
            << SRS_JFIELD_ORG("pid", u->pid) << SRS_JFIELD_CONT
            << SRS_JFIELD_STR("comm", u->comm) << SRS_JFIELD_CONT
            << SRS_JFIELD_STR("state", u->state) << SRS_JFIELD_CONT
            << SRS_JFIELD_ORG("ppid", u->ppid) << SRS_JFIELD_CONT
            << SRS_JFIELD_ORG("pgrp", u->pgrp) << SRS_JFIELD_CONT
            << SRS_JFIELD_ORG("session", u->session) << SRS_JFIELD_CONT
            << SRS_JFIELD_ORG("tty_nr", u->tty_nr) << SRS_JFIELD_CONT
            << SRS_JFIELD_ORG("tpgid", u->tpgid) << SRS_JFIELD_CONT
            << SRS_JFIELD_ORG("flags", u->flags) << SRS_JFIELD_CONT
            << SRS_JFIELD_ORG("minflt", u->minflt) << SRS_JFIELD_CONT
            << SRS_JFIELD_ORG("cminflt", u->cminflt) << SRS_JFIELD_CONT
            << SRS_JFIELD_ORG("majflt", u->majflt) << SRS_JFIELD_CONT
            << SRS_JFIELD_ORG("cmajflt", u->cmajflt) << SRS_JFIELD_CONT
            << SRS_JFIELD_ORG("utime", u->utime) << SRS_JFIELD_CONT
            << SRS_JFIELD_ORG("stime", u->stime) << SRS_JFIELD_CONT
            << SRS_JFIELD_ORG("cutime", u->cutime) << SRS_JFIELD_CONT
            << SRS_JFIELD_ORG("cstime", u->cstime) << SRS_JFIELD_CONT
            << SRS_JFIELD_ORG("priority", u->priority) << SRS_JFIELD_CONT
            << SRS_JFIELD_ORG("nice", u->nice) << SRS_JFIELD_CONT
            << SRS_JFIELD_ORG("num_threads", u->num_threads) << SRS_JFIELD_CONT
            << SRS_JFIELD_ORG("itrealvalue", u->itrealvalue) << SRS_JFIELD_CONT
            << SRS_JFIELD_ORG("starttime", u->starttime) << SRS_JFIELD_CONT
            << SRS_JFIELD_ORG("vsize", u->vsize) << SRS_JFIELD_CONT
            << SRS_JFIELD_ORG("rss", u->rss) << SRS_JFIELD_CONT
            << SRS_JFIELD_ORG("rsslim", u->rsslim) << SRS_JFIELD_CONT
            << SRS_JFIELD_ORG("startcode", u->startcode) << SRS_JFIELD_CONT
            << SRS_JFIELD_ORG("endcode", u->endcode) << SRS_JFIELD_CONT
            << SRS_JFIELD_ORG("startstack", u->startstack) << SRS_JFIELD_CONT
            << SRS_JFIELD_ORG("kstkesp", u->kstkesp) << SRS_JFIELD_CONT
            << SRS_JFIELD_ORG("kstkeip", u->kstkeip) << SRS_JFIELD_CONT
            << SRS_JFIELD_ORG("signal", u->signal) << SRS_JFIELD_CONT
            << SRS_JFIELD_ORG("blocked", u->blocked) << SRS_JFIELD_CONT
            << SRS_JFIELD_ORG("sigignore", u->sigignore) << SRS_JFIELD_CONT
            << SRS_JFIELD_ORG("sigcatch", u->sigcatch) << SRS_JFIELD_CONT
            << SRS_JFIELD_ORG("wchan", u->wchan) << SRS_JFIELD_CONT
            << SRS_JFIELD_ORG("nswap", u->nswap) << SRS_JFIELD_CONT
            << SRS_JFIELD_ORG("cnswap", u->cnswap) << SRS_JFIELD_CONT
            << SRS_JFIELD_ORG("exit_signal", u->exit_signal) << SRS_JFIELD_CONT
            << SRS_JFIELD_ORG("processor", u->processor) << SRS_JFIELD_CONT
            << SRS_JFIELD_ORG("rt_priority", u->rt_priority) << SRS_JFIELD_CONT
            << SRS_JFIELD_ORG("policy", u->policy) << SRS_JFIELD_CONT
            << SRS_JFIELD_ORG("delayacct_blkio_ticks", u->delayacct_blkio_ticks) << SRS_JFIELD_CONT
            << SRS_JFIELD_ORG("guest_time", u->guest_time) << SRS_JFIELD_CONT
            << SRS_JFIELD_ORG("cguest_time", u->cguest_time)
        << SRS_JOBJECT_END
        << SRS_JOBJECT_END;
    
    return srs_http_response_json(w, ss.str());
}

SrsGoApiSystemProcStats::SrsGoApiSystemProcStats()
{
}

SrsGoApiSystemProcStats::~SrsGoApiSystemProcStats()
{
}

int SrsGoApiSystemProcStats::serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r)
{
    std::stringstream ss;
    
    SrsProcSystemStat* s = srs_get_system_proc_stat();
    
    ss << SRS_JOBJECT_START
        << SRS_JFIELD_ERROR(ERROR_SUCCESS) << SRS_JFIELD_CONT
        << SRS_JFIELD_ORG("data", SRS_JOBJECT_START)
            << SRS_JFIELD_ORG("ok", (s->ok? "true":"false")) << SRS_JFIELD_CONT
            << SRS_JFIELD_ORG("sample_time", s->sample_time) << SRS_JFIELD_CONT
            << SRS_JFIELD_ORG("percent", s->percent) << SRS_JFIELD_CONT
            << SRS_JFIELD_ORG("user", s->user) << SRS_JFIELD_CONT
            << SRS_JFIELD_ORG("nice", s->nice) << SRS_JFIELD_CONT
            << SRS_JFIELD_ORG("sys", s->sys) << SRS_JFIELD_CONT
            << SRS_JFIELD_ORG("idle", s->idle) << SRS_JFIELD_CONT
            << SRS_JFIELD_ORG("iowait", s->iowait) << SRS_JFIELD_CONT
            << SRS_JFIELD_ORG("irq", s->irq) << SRS_JFIELD_CONT
            << SRS_JFIELD_ORG("softirq", s->softirq) << SRS_JFIELD_CONT
            << SRS_JFIELD_ORG("steal", s->steal) << SRS_JFIELD_CONT
            << SRS_JFIELD_ORG("guest", s->guest)
        << SRS_JOBJECT_END
        << SRS_JOBJECT_END;
    
    return srs_http_response_json(w, ss.str());
}

SrsGoApiMemInfos::SrsGoApiMemInfos()
{
}

SrsGoApiMemInfos::~SrsGoApiMemInfos()
{
}

int SrsGoApiMemInfos::serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r)
{
    std::stringstream ss;
    
    SrsMemInfo* m = srs_get_meminfo();
    
    ss << SRS_JOBJECT_START
        << SRS_JFIELD_ERROR(ERROR_SUCCESS) << SRS_JFIELD_CONT
        << SRS_JFIELD_ORG("data", SRS_JOBJECT_START)
            << SRS_JFIELD_ORG("ok", (m->ok? "true":"false")) << SRS_JFIELD_CONT
            << SRS_JFIELD_ORG("sample_time", m->sample_time) << SRS_JFIELD_CONT
            << SRS_JFIELD_ORG("percent_ram", m->percent_ram) << SRS_JFIELD_CONT
            << SRS_JFIELD_ORG("percent_swap", m->percent_swap) << SRS_JFIELD_CONT
            << SRS_JFIELD_ORG("MemActive", m->MemActive) << SRS_JFIELD_CONT
            << SRS_JFIELD_ORG("RealInUse", m->RealInUse) << SRS_JFIELD_CONT
            << SRS_JFIELD_ORG("NotInUse", m->NotInUse) << SRS_JFIELD_CONT
            << SRS_JFIELD_ORG("MemTotal", m->MemTotal) << SRS_JFIELD_CONT
            << SRS_JFIELD_ORG("MemFree", m->MemFree) << SRS_JFIELD_CONT
            << SRS_JFIELD_ORG("Buffers", m->Buffers) << SRS_JFIELD_CONT
            << SRS_JFIELD_ORG("Cached", m->Cached) << SRS_JFIELD_CONT
            << SRS_JFIELD_ORG("SwapTotal", m->SwapTotal) << SRS_JFIELD_CONT
            << SRS_JFIELD_ORG("SwapFree", m->SwapFree)
        << SRS_JOBJECT_END
        << SRS_JOBJECT_END;
    
    return srs_http_response_json(w, ss.str());
}

SrsGoApiAuthors::SrsGoApiAuthors()
{
}

SrsGoApiAuthors::~SrsGoApiAuthors()
{
}

int SrsGoApiAuthors::serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r)
{
    std::stringstream ss;
    
    ss << SRS_JOBJECT_START
        << SRS_JFIELD_ERROR(ERROR_SUCCESS) << SRS_JFIELD_CONT
        << SRS_JFIELD_ORG("data", SRS_JOBJECT_START)
            << SRS_JFIELD_STR("primary", RTMP_SIG_SRS_PRIMARY) << SRS_JFIELD_CONT
            << SRS_JFIELD_STR("authors", RTMP_SIG_SRS_AUTHROS) << SRS_JFIELD_CONT
            << SRS_JFIELD_STR("contributors_link", RTMP_SIG_SRS_CONTRIBUTORS_URL) << SRS_JFIELD_CONT
            << SRS_JFIELD_STR("contributors", SRS_AUTO_CONSTRIBUTORS)
        << SRS_JOBJECT_END
        << SRS_JOBJECT_END;
    
    return srs_http_response_json(w, ss.str());
}

SrsGoApiRequests::SrsGoApiRequests()
{
}

SrsGoApiRequests::~SrsGoApiRequests()
{
}

int SrsGoApiRequests::serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r)
{
    ISrsHttpMessage* req = r;
    
    std::stringstream ss;
    
    ss << SRS_JOBJECT_START
        << SRS_JFIELD_ERROR(ERROR_SUCCESS) << SRS_JFIELD_CONT
        << SRS_JFIELD_ORG("data", SRS_JOBJECT_START)
            << SRS_JFIELD_STR("uri", req->uri()) << SRS_JFIELD_CONT
            << SRS_JFIELD_STR("path", req->path()) << SRS_JFIELD_CONT;
    
    // method
    if (req->is_http_get()) {
        ss  << SRS_JFIELD_STR("METHOD", "GET");
    } else if (req->is_http_post()) {
        ss  << SRS_JFIELD_STR("METHOD", "POST");
    } else if (req->is_http_put()) {
        ss  << SRS_JFIELD_STR("METHOD", "PUT");
    } else if (req->is_http_delete()) {
        ss  << SRS_JFIELD_STR("METHOD", "DELETE");
    } else {
        ss  << SRS_JFIELD_ORG("METHOD", req->method());
    }
    ss << SRS_JFIELD_CONT;
    
    // request headers
    ss      << SRS_JFIELD_NAME("headers") << SRS_JOBJECT_START;
    for (int i = 0; i < req->request_header_count(); i++) {
        std::string key = req->request_header_key_at(i);
        std::string value = req->request_header_value_at(i);
        if ( i < req->request_header_count() - 1) {
            ss      << SRS_JFIELD_STR(key, value) << SRS_JFIELD_CONT;
        } else {
            ss      << SRS_JFIELD_STR(key, value);
        }
    }
    ss      << SRS_JOBJECT_END << SRS_JFIELD_CONT;
    
    // server informations
    ss      << SRS_JFIELD_NAME("server") << SRS_JOBJECT_START
                << SRS_JFIELD_STR("sigature", RTMP_SIG_SRS_KEY) << SRS_JFIELD_CONT
                << SRS_JFIELD_STR("name", RTMP_SIG_SRS_NAME) << SRS_JFIELD_CONT
                << SRS_JFIELD_STR("version", RTMP_SIG_SRS_VERSION) << SRS_JFIELD_CONT
                << SRS_JFIELD_STR("link", RTMP_SIG_SRS_URL) << SRS_JFIELD_CONT
                << SRS_JFIELD_ORG("time", srs_get_system_time_ms())
            << SRS_JOBJECT_END
        << SRS_JOBJECT_END
        << SRS_JOBJECT_END;
    
    return srs_http_response_json(w, ss.str());
}

SrsGoApiVhosts::SrsGoApiVhosts()
{
}

SrsGoApiVhosts::~SrsGoApiVhosts()
{
}

int SrsGoApiVhosts::serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r)
{
    std::stringstream data;
    SrsStatistic* stat = SrsStatistic::instance();
    int ret = stat->dumps_vhosts(data);
    
    std::stringstream ss;
    
    ss << SRS_JOBJECT_START
            << SRS_JFIELD_ERROR(ret) << SRS_JFIELD_CONT
            << SRS_JFIELD_ORG("server", stat->server_id()) << SRS_JFIELD_CONT
            << SRS_JFIELD_ORG("vhosts", data.str())
        << SRS_JOBJECT_END;
    
    return srs_http_response_json(w, ss.str());
}

SrsGoApiStreams::SrsGoApiStreams()
{
}

SrsGoApiStreams::~SrsGoApiStreams()
{
}

int SrsGoApiStreams::serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r)
{
    std::stringstream data;
    SrsStatistic* stat = SrsStatistic::instance();
    int ret = stat->dumps_streams(data);
    
    std::stringstream ss;
    
    ss << SRS_JOBJECT_START
            << SRS_JFIELD_ERROR(ret) << SRS_JFIELD_CONT
            << SRS_JFIELD_ORG("server", stat->server_id()) << SRS_JFIELD_CONT
            << SRS_JFIELD_ORG("streams", data.str())
        << SRS_JOBJECT_END;
    
    return srs_http_response_json(w, ss.str());
}

SrsGoApiError::SrsGoApiError()
{
}

SrsGoApiError::~SrsGoApiError()
{
}

int SrsGoApiError::serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r)
{
    std::stringstream ss;
    
    ss << SRS_JOBJECT_START
            << SRS_JFIELD_ERROR(100) << SRS_JFIELD_CONT
            << SRS_JFIELD_STR("msg", "SRS demo error.") << SRS_JFIELD_CONT
            << SRS_JFIELD_STR("path", r->path())
        << SRS_JOBJECT_END;
    
    return srs_http_response_json(w, ss.str());
}

SrsHttpApi::SrsHttpApi(IConnectionManager* cm, st_netfd_t fd, SrsHttpServeMux* m)
    : SrsConnection(cm, fd)
{
    mux = m;
    parser = new SrsHttpParser();
    crossdomain_required = false;
}

SrsHttpApi::~SrsHttpApi()
{
    srs_freep(parser);
}

void SrsHttpApi::resample()
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

void SrsHttpApi::cleanup()
{
    // TODO: FIXME: implements it
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
    
    // set the recv timeout, for some clients never disconnect the connection.
    // @see https://github.com/simple-rtmp-server/srs/issues/398
    skt.set_recv_timeout(SRS_HTTP_RECV_TIMEOUT_US);
    
    // process http messages.
    while(!disposed) {
        ISrsHttpMessage* req = NULL;
        
        // get a http message
        if ((ret = parser->parse_message(&skt, this, &req)) != ERROR_SUCCESS) {
            return ret;
        }

        // if SUCCESS, always NOT-NULL.
        srs_assert(req);
        
        // always free it in this scope.
        SrsAutoFree(ISrsHttpMessage, req);
        
        // ok, handle http request.
        SrsHttpResponseWriter writer(&skt);
        if ((ret = process_request(&writer, req)) != ERROR_SUCCESS) {
            return ret;
        }

        // read all rest bytes in request body.
        char buf[SRS_HTTP_READ_CACHE_BYTES];
        ISrsHttpResponseReader* br = req->body_reader();
        while (!br->eof()) {
            if ((ret = br->read(buf, SRS_HTTP_READ_CACHE_BYTES, NULL)) != ERROR_SUCCESS) {
                return ret;
            }
        }

        // donot keep alive, disconnect it.
        // @see https://github.com/simple-rtmp-server/srs/issues/399
        if (!req->is_keep_alive()) {
            break;
        }
    }
        
    return ret;
}

int SrsHttpApi::process_request(ISrsHttpResponseWriter* w, ISrsHttpMessage* r) 
{
    int ret = ERROR_SUCCESS;
    
    srs_trace("HTTP %s %s, content-length=%"PRId64"", 
        r->method_str().c_str(), r->url().c_str(), r->content_length());
    
    // method is OPTIONS and enable crossdomain, required crossdomain header.
    if (r->is_http_options() && _srs_config->get_http_api_crossdomain()) {
        crossdomain_required = true;
    }

    // whenever crossdomain required, set crossdomain header.
    if (crossdomain_required) {
        w->header()->set("Access-Control-Allow-Origin", "*");
        w->header()->set("Access-Control-Allow-Methods", "GET, POST, HEAD, PUT, DELETE");
        w->header()->set("Access-Control-Allow-Headers", "Cache-Control,X-Proxy-Authorization,X-Requested-With,Content-Type");
    }

    // handle the http options.
    if (r->is_http_options()) {
        w->header()->set_content_length(0);
        if (_srs_config->get_http_api_crossdomain()) {
            w->write_header(SRS_CONSTS_HTTP_OK);
        } else {
            w->write_header(SRS_CONSTS_HTTP_MethodNotAllowed);
        }
        return w->final_request();
    }
    
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

