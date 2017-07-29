/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2017 OSSRS(winlin)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <srs_app_http_api.hpp>

#include <sstream>
#include <stdlib.h>
#include <signal.h>
using namespace std;

#include <srs_kernel_log.hpp>
#include <srs_kernel_error.hpp>
#include <srs_app_st.hpp>
#include <srs_core_autofree.hpp>
#include <srs_protocol_json.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_app_utility.hpp>
#include <srs_app_statistic.hpp>
#include <srs_rtmp_stack.hpp>
#include <srs_app_dvr.hpp>
#include <srs_app_config.hpp>
#include <srs_app_source.hpp>
#include <srs_app_http_conn.hpp>
#include <srs_kernel_consts.hpp>
#include <srs_app_server.hpp>
#include <srs_protocol_amf0.hpp>
#include <srs_protocol_utility.hpp>

int srs_api_response_jsonp(ISrsHttpResponseWriter* w, string callback, string data)
{
    int ret = ERROR_SUCCESS;
    
    SrsHttpHeader* h = w->header();
    
    h->set_content_length(data.length() + callback.length() + 2);
    h->set_content_type("text/javascript");
    
    if (!callback.empty() && (ret = w->write((char*)callback.data(), (int)callback.length())) != ERROR_SUCCESS) {
        return ret;
    }
    
    static char* c0 = (char*)"(";
    if ((ret = w->write(c0, 1)) != ERROR_SUCCESS) {
        return ret;
    }
    if ((ret = w->write((char*)data.data(), (int)data.length())) != ERROR_SUCCESS) {
        return ret;
    }
    
    static char* c1 = (char*)")";
    if ((ret = w->write(c1, 1)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int srs_api_response_jsonp_code(ISrsHttpResponseWriter* w, string callback, int code)
{
    SrsJsonObject* obj = SrsJsonAny::object();
    SrsAutoFree(SrsJsonObject, obj);
    
    obj->set("code", SrsJsonAny::integer(code));
    
    return srs_api_response_jsonp(w, callback, obj->dumps());
}

int srs_api_response_json(ISrsHttpResponseWriter* w, string data)
{
    SrsHttpHeader* h = w->header();
    
    h->set_content_length(data.length());
    h->set_content_type("application/json");
    
    return w->write((char*)data.data(), (int)data.length());
}

int srs_api_response_json_code(ISrsHttpResponseWriter* w, int code)
{
    SrsJsonObject* obj = SrsJsonAny::object();
    SrsAutoFree(SrsJsonObject, obj);
    
    obj->set("code", SrsJsonAny::integer(code));
    
    return srs_api_response_json(w, obj->dumps());
}

int srs_api_response(ISrsHttpResponseWriter* w, ISrsHttpMessage* r, std::string json)
{
    // no jsonp, directly response.
    if (!r->is_jsonp()) {
        return srs_api_response_json(w, json);
    }
    
    // jsonp, get function name from query("callback")
    string callback = r->query_get("callback");
    return srs_api_response_jsonp(w, callback, json);
}

int srs_api_response_code(ISrsHttpResponseWriter* w, ISrsHttpMessage* r, int code)
{
    // no jsonp, directly response.
    if (!r->is_jsonp()) {
        return srs_api_response_json_code(w, code);
    }
    
    // jsonp, get function name from query("callback")
    string callback = r->query_get("callback");
    return srs_api_response_jsonp_code(w, callback, code);
}

SrsGoApiRoot::SrsGoApiRoot()
{
}

SrsGoApiRoot::~SrsGoApiRoot()
{
}

int SrsGoApiRoot::serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r)
{
    SrsStatistic* stat = SrsStatistic::instance();
    
    SrsJsonObject* obj = SrsJsonAny::object();
    SrsAutoFree(SrsJsonObject, obj);
    
    obj->set("code", SrsJsonAny::integer(ERROR_SUCCESS));
    obj->set("server", SrsJsonAny::integer(stat->server_id()));
    
    SrsJsonObject* urls = SrsJsonAny::object();
    obj->set("urls", urls);
    
    urls->set("api", SrsJsonAny::str("the api root"));
    
    return srs_api_response(w, r, obj->dumps());
}

SrsGoApiApi::SrsGoApiApi()
{
}

SrsGoApiApi::~SrsGoApiApi()
{
}

int SrsGoApiApi::serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r)
{
    SrsStatistic* stat = SrsStatistic::instance();
    
    SrsJsonObject* obj = SrsJsonAny::object();
    SrsAutoFree(SrsJsonObject, obj);
    
    obj->set("code", SrsJsonAny::integer(ERROR_SUCCESS));
    obj->set("server", SrsJsonAny::integer(stat->server_id()));
    
    SrsJsonObject* urls = SrsJsonAny::object();
    obj->set("urls", urls);
    
    urls->set("v1", SrsJsonAny::str("the api version 1.0"));
    
    return srs_api_response(w, r, obj->dumps());
}

SrsGoApiV1::SrsGoApiV1()
{
}

SrsGoApiV1::~SrsGoApiV1()
{
}

int SrsGoApiV1::serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r)
{
    SrsStatistic* stat = SrsStatistic::instance();
    
    SrsJsonObject* obj = SrsJsonAny::object();
    SrsAutoFree(SrsJsonObject, obj);
    
    obj->set("code", SrsJsonAny::integer(ERROR_SUCCESS));
    obj->set("server", SrsJsonAny::integer(stat->server_id()));
    
    SrsJsonObject* urls = SrsJsonAny::object();
    obj->set("urls", urls);
    
    urls->set("versions", SrsJsonAny::str("the version of SRS"));
    urls->set("summaries", SrsJsonAny::str("the summary(pid, argv, pwd, cpu, mem) of SRS"));
    urls->set("rusages", SrsJsonAny::str("the rusage of SRS"));
    urls->set("self_proc_stats", SrsJsonAny::str("the self process stats"));
    urls->set("system_proc_stats", SrsJsonAny::str("the system process stats"));
    urls->set("meminfos", SrsJsonAny::str("the meminfo of system"));
    urls->set("authors", SrsJsonAny::str("the license, copyright, authors and contributors"));
    urls->set("features", SrsJsonAny::str("the supported features of SRS"));
    urls->set("requests", SrsJsonAny::str("the request itself, for http debug"));
    urls->set("vhosts", SrsJsonAny::str("manage all vhosts or specified vhost"));
    urls->set("streams", SrsJsonAny::str("manage all streams or specified stream"));
    urls->set("clients", SrsJsonAny::str("manage all clients or specified client, default query top 10 clients"));
    urls->set("raw", SrsJsonAny::str("raw api for srs, support CUID srs for instance the config"));
    
    SrsJsonObject* tests = SrsJsonAny::object();
    obj->set("tests", tests);
    
    tests->set("requests", SrsJsonAny::str("show the request info"));
    tests->set("errors", SrsJsonAny::str("always return an error 100"));
    tests->set("redirects", SrsJsonAny::str("always redirect to /api/v1/test/errors"));
    tests->set("[vhost]", SrsJsonAny::str("http vhost for http://error.srs.com:1985/api/v1/tests/errors"));
    
    return srs_api_response(w, r, obj->dumps());
}

SrsGoApiVersion::SrsGoApiVersion()
{
}

SrsGoApiVersion::~SrsGoApiVersion()
{
}

int SrsGoApiVersion::serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r)
{
    SrsStatistic* stat = SrsStatistic::instance();
    
    SrsJsonObject* obj = SrsJsonAny::object();
    SrsAutoFree(SrsJsonObject, obj);
    
    obj->set("code", SrsJsonAny::integer(ERROR_SUCCESS));
    obj->set("server", SrsJsonAny::integer(stat->server_id()));
    
    SrsJsonObject* data = SrsJsonAny::object();
    obj->set("data", data);
    
    data->set("major", SrsJsonAny::integer(VERSION_MAJOR));
    data->set("minor", SrsJsonAny::integer(VERSION_MINOR));
    data->set("revision", SrsJsonAny::integer(VERSION_REVISION));
    data->set("version", SrsJsonAny::str(RTMP_SIG_SRS_VERSION));
    
    return srs_api_response(w, r, obj->dumps());
}

SrsGoApiSummaries::SrsGoApiSummaries()
{
}

SrsGoApiSummaries::~SrsGoApiSummaries()
{
}

int SrsGoApiSummaries::serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r)
{
    SrsStatistic* stat = SrsStatistic::instance();
    
    SrsJsonObject* obj = SrsJsonAny::object();
    SrsAutoFree(SrsJsonObject, obj);
    
    obj->set("code", SrsJsonAny::integer(ERROR_SUCCESS));
    obj->set("server", SrsJsonAny::integer(stat->server_id()));
    
    srs_api_dump_summaries(obj);
    
    return srs_api_response(w, r, obj->dumps());
}

SrsGoApiRusages::SrsGoApiRusages()
{
}

SrsGoApiRusages::~SrsGoApiRusages()
{
}

int SrsGoApiRusages::serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r)
{
    SrsStatistic* stat = SrsStatistic::instance();
    
    SrsJsonObject* obj = SrsJsonAny::object();
    SrsAutoFree(SrsJsonObject, obj);
    
    obj->set("code", SrsJsonAny::integer(ERROR_SUCCESS));
    obj->set("server", SrsJsonAny::integer(stat->server_id()));
    
    SrsJsonObject* data = SrsJsonAny::object();
    obj->set("data", data);
    
    SrsRusage* ru = srs_get_system_rusage();
    
    data->set("ok", SrsJsonAny::boolean(ru->ok));
    data->set("sample_time", SrsJsonAny::integer(ru->sample_time));
    data->set("ru_utime", SrsJsonAny::integer(ru->r.ru_utime.tv_sec));
    data->set("ru_stime", SrsJsonAny::integer(ru->r.ru_stime.tv_sec));
    data->set("ru_maxrss", SrsJsonAny::integer(ru->r.ru_maxrss));
    data->set("ru_ixrss", SrsJsonAny::integer(ru->r.ru_ixrss));
    data->set("ru_idrss", SrsJsonAny::integer(ru->r.ru_idrss));
    data->set("ru_isrss", SrsJsonAny::integer(ru->r.ru_isrss));
    data->set("ru_minflt", SrsJsonAny::integer(ru->r.ru_minflt));
    data->set("ru_majflt", SrsJsonAny::integer(ru->r.ru_majflt));
    data->set("ru_nswap", SrsJsonAny::integer(ru->r.ru_nswap));
    data->set("ru_inblock", SrsJsonAny::integer(ru->r.ru_inblock));
    data->set("ru_oublock", SrsJsonAny::integer(ru->r.ru_oublock));
    data->set("ru_msgsnd", SrsJsonAny::integer(ru->r.ru_msgsnd));
    data->set("ru_msgrcv", SrsJsonAny::integer(ru->r.ru_msgrcv));
    data->set("ru_nsignals", SrsJsonAny::integer(ru->r.ru_nsignals));
    data->set("ru_nvcsw", SrsJsonAny::integer(ru->r.ru_nvcsw));
    data->set("ru_nivcsw", SrsJsonAny::integer(ru->r.ru_nivcsw));
    
    return srs_api_response(w, r, obj->dumps());
}

SrsGoApiSelfProcStats::SrsGoApiSelfProcStats()
{
}

SrsGoApiSelfProcStats::~SrsGoApiSelfProcStats()
{
}

int SrsGoApiSelfProcStats::serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r)
{
    SrsStatistic* stat = SrsStatistic::instance();
    
    SrsJsonObject* obj = SrsJsonAny::object();
    SrsAutoFree(SrsJsonObject, obj);
    
    obj->set("code", SrsJsonAny::integer(ERROR_SUCCESS));
    obj->set("server", SrsJsonAny::integer(stat->server_id()));
    
    SrsJsonObject* data = SrsJsonAny::object();
    obj->set("data", data);
    
    SrsProcSelfStat* u = srs_get_self_proc_stat();
    
    string state;
    state += (char)u->state;
    
    data->set("ok", SrsJsonAny::boolean(u->ok));
    data->set("sample_time", SrsJsonAny::integer(u->sample_time));
    data->set("percent", SrsJsonAny::number(u->percent));
    data->set("pid", SrsJsonAny::integer(u->pid));
    data->set("comm", SrsJsonAny::str(u->comm));
    data->set("state", SrsJsonAny::str(state.c_str()));
    data->set("ppid", SrsJsonAny::integer(u->ppid));
    data->set("pgrp", SrsJsonAny::integer(u->pgrp));
    data->set("session", SrsJsonAny::integer(u->session));
    data->set("tty_nr", SrsJsonAny::integer(u->tty_nr));
    data->set("tpgid", SrsJsonAny::integer(u->tpgid));
    data->set("flags", SrsJsonAny::integer(u->flags));
    data->set("minflt", SrsJsonAny::integer(u->minflt));
    data->set("cminflt", SrsJsonAny::integer(u->cminflt));
    data->set("majflt", SrsJsonAny::integer(u->majflt));
    data->set("cmajflt", SrsJsonAny::integer(u->cmajflt));
    data->set("utime", SrsJsonAny::integer(u->utime));
    data->set("stime", SrsJsonAny::integer(u->stime));
    data->set("cutime", SrsJsonAny::integer(u->cutime));
    data->set("cstime", SrsJsonAny::integer(u->cstime));
    data->set("priority", SrsJsonAny::integer(u->priority));
    data->set("nice", SrsJsonAny::integer(u->nice));
    data->set("num_threads", SrsJsonAny::integer(u->num_threads));
    data->set("itrealvalue", SrsJsonAny::integer(u->itrealvalue));
    data->set("starttime", SrsJsonAny::integer(u->starttime));
    data->set("vsize", SrsJsonAny::integer(u->vsize));
    data->set("rss", SrsJsonAny::integer(u->rss));
    data->set("rsslim", SrsJsonAny::integer(u->rsslim));
    data->set("startcode", SrsJsonAny::integer(u->startcode));
    data->set("endcode", SrsJsonAny::integer(u->endcode));
    data->set("startstack", SrsJsonAny::integer(u->startstack));
    data->set("kstkesp", SrsJsonAny::integer(u->kstkesp));
    data->set("kstkeip", SrsJsonAny::integer(u->kstkeip));
    data->set("signal", SrsJsonAny::integer(u->signal));
    data->set("blocked", SrsJsonAny::integer(u->blocked));
    data->set("sigignore", SrsJsonAny::integer(u->sigignore));
    data->set("sigcatch", SrsJsonAny::integer(u->sigcatch));
    data->set("wchan", SrsJsonAny::integer(u->wchan));
    data->set("nswap", SrsJsonAny::integer(u->nswap));
    data->set("cnswap", SrsJsonAny::integer(u->cnswap));
    data->set("exit_signal", SrsJsonAny::integer(u->exit_signal));
    data->set("processor", SrsJsonAny::integer(u->processor));
    data->set("rt_priority", SrsJsonAny::integer(u->rt_priority));
    data->set("policy", SrsJsonAny::integer(u->policy));
    data->set("delayacct_blkio_ticks", SrsJsonAny::integer(u->delayacct_blkio_ticks));
    data->set("guest_time", SrsJsonAny::integer(u->guest_time));
    data->set("cguest_time", SrsJsonAny::integer(u->cguest_time));
    
    return srs_api_response(w, r, obj->dumps());
}

SrsGoApiSystemProcStats::SrsGoApiSystemProcStats()
{
}

SrsGoApiSystemProcStats::~SrsGoApiSystemProcStats()
{
}

int SrsGoApiSystemProcStats::serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r)
{
    SrsStatistic* stat = SrsStatistic::instance();
    
    SrsJsonObject* obj = SrsJsonAny::object();
    SrsAutoFree(SrsJsonObject, obj);
    
    obj->set("code", SrsJsonAny::integer(ERROR_SUCCESS));
    obj->set("server", SrsJsonAny::integer(stat->server_id()));
    
    SrsJsonObject* data = SrsJsonAny::object();
    obj->set("data", data);
    
    SrsProcSystemStat* s = srs_get_system_proc_stat();
    
    data->set("ok", SrsJsonAny::boolean(s->ok));
    data->set("sample_time", SrsJsonAny::integer(s->sample_time));
    data->set("percent", SrsJsonAny::number(s->percent));
    data->set("user", SrsJsonAny::integer(s->user));
    data->set("nice", SrsJsonAny::integer(s->nice));
    data->set("sys", SrsJsonAny::integer(s->sys));
    data->set("idle", SrsJsonAny::integer(s->idle));
    data->set("iowait", SrsJsonAny::integer(s->iowait));
    data->set("irq", SrsJsonAny::integer(s->irq));
    data->set("softirq", SrsJsonAny::integer(s->softirq));
    data->set("steal", SrsJsonAny::integer(s->steal));
    data->set("guest", SrsJsonAny::integer(s->guest));
    
    return srs_api_response(w, r, obj->dumps());
}

SrsGoApiMemInfos::SrsGoApiMemInfos()
{
}

SrsGoApiMemInfos::~SrsGoApiMemInfos()
{
}

int SrsGoApiMemInfos::serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r)
{
    SrsStatistic* stat = SrsStatistic::instance();
    
    SrsJsonObject* obj = SrsJsonAny::object();
    SrsAutoFree(SrsJsonObject, obj);
    
    obj->set("code", SrsJsonAny::integer(ERROR_SUCCESS));
    obj->set("server", SrsJsonAny::integer(stat->server_id()));
    
    SrsJsonObject* data = SrsJsonAny::object();
    obj->set("data", data);
    
    SrsMemInfo* m = srs_get_meminfo();
    
    data->set("ok", SrsJsonAny::boolean(m->ok));
    data->set("sample_time", SrsJsonAny::integer(m->sample_time));
    data->set("percent_ram", SrsJsonAny::number(m->percent_ram));
    data->set("percent_swap", SrsJsonAny::number(m->percent_swap));
    data->set("MemActive", SrsJsonAny::integer(m->MemActive));
    data->set("RealInUse", SrsJsonAny::integer(m->RealInUse));
    data->set("NotInUse", SrsJsonAny::integer(m->NotInUse));
    data->set("MemTotal", SrsJsonAny::integer(m->MemTotal));
    data->set("MemFree", SrsJsonAny::integer(m->MemFree));
    data->set("Buffers", SrsJsonAny::integer(m->Buffers));
    data->set("Cached", SrsJsonAny::integer(m->Cached));
    data->set("SwapTotal", SrsJsonAny::integer(m->SwapTotal));
    data->set("SwapFree", SrsJsonAny::integer(m->SwapFree));
    
    return srs_api_response(w, r, obj->dumps());
}

SrsGoApiAuthors::SrsGoApiAuthors()
{
}

SrsGoApiAuthors::~SrsGoApiAuthors()
{
}

int SrsGoApiAuthors::serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r)
{
    SrsStatistic* stat = SrsStatistic::instance();
    
    SrsJsonObject* obj = SrsJsonAny::object();
    SrsAutoFree(SrsJsonObject, obj);
    
    obj->set("code", SrsJsonAny::integer(ERROR_SUCCESS));
    obj->set("server", SrsJsonAny::integer(stat->server_id()));
    
    SrsJsonObject* data = SrsJsonAny::object();
    obj->set("data", data);
    
    data->set("primary", SrsJsonAny::str(RTMP_SIG_SRS_PRIMARY));
    data->set("license", SrsJsonAny::str(RTMP_SIG_SRS_LICENSE));
    data->set("copyright", SrsJsonAny::str(RTMP_SIG_SRS_COPYRIGHT));
    data->set("authors", SrsJsonAny::str(RTMP_SIG_SRS_AUTHROS));
    data->set("contributors", SrsJsonAny::str(SRS_AUTO_CONSTRIBUTORS));
    
    return srs_api_response(w, r, obj->dumps());
}

SrsGoApiFeatures::SrsGoApiFeatures()
{
}

SrsGoApiFeatures::~SrsGoApiFeatures()
{
}

int SrsGoApiFeatures::serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r)
{
    SrsStatistic* stat = SrsStatistic::instance();
    
    SrsJsonObject* obj = SrsJsonAny::object();
    SrsAutoFree(SrsJsonObject, obj);
    
    obj->set("code", SrsJsonAny::integer(ERROR_SUCCESS));
    obj->set("server", SrsJsonAny::integer(stat->server_id()));
    
    SrsJsonObject* data = SrsJsonAny::object();
    obj->set("data", data);
    
    data->set("options", SrsJsonAny::str(SRS_AUTO_USER_CONFIGURE));
    data->set("options2", SrsJsonAny::str(SRS_AUTO_CONFIGURE));
    data->set("build", SrsJsonAny::str(SRS_AUTO_BUILD_DATE));
    data->set("build2", SrsJsonAny::str(SRS_AUTO_BUILD_TS));
    
    SrsJsonObject* features = SrsJsonAny::object();
    data->set("features", features);
    
#ifdef SRS_AUTO_SSL
    features->set("ssl", SrsJsonAny::boolean(true));
#else
    features->set("ssl", SrsJsonAny::boolean(false));
#endif
    features->set("hls", SrsJsonAny::boolean(true));
#ifdef SRS_AUTO_HDS
    features->set("hds", SrsJsonAny::boolean(true));
#else
    features->set("hds", SrsJsonAny::boolean(false));
#endif
    features->set("callback", SrsJsonAny::boolean(true));
    features->set("api", SrsJsonAny::boolean(true));
    features->set("httpd", SrsJsonAny::boolean(true));
    features->set("dvr", SrsJsonAny::boolean(true));
#ifdef SRS_AUTO_TRANSCODE
    features->set("transcode", SrsJsonAny::boolean(true));
#else
    features->set("transcode", SrsJsonAny::boolean(false));
#endif
#ifdef SRS_AUTO_INGEST
    features->set("ingest", SrsJsonAny::boolean(true));
#else
    features->set("ingest", SrsJsonAny::boolean(false));
#endif
#ifdef SRS_AUTO_STAT
    features->set("stat", SrsJsonAny::boolean(true));
#else
    features->set("stat", SrsJsonAny::boolean(false));
#endif
#ifdef SRS_AUTO_NGINX
    features->set("nginx", SrsJsonAny::boolean(true));
#else
    features->set("nginx", SrsJsonAny::boolean(false));
#endif
#ifdef SRS_AUTO_FFMPEG_TOOL
    features->set("ffmpeg", SrsJsonAny::boolean(true));
#else
    features->set("ffmpeg", SrsJsonAny::boolean(false));
#endif
#ifdef SRS_AUTO_STREAM_CASTER
    features->set("caster", SrsJsonAny::boolean(true));
#else
    features->set("caster", SrsJsonAny::boolean(false));
#endif
#ifdef SRS_PERF_COMPLEX_SEND
    features->set("complex_send", SrsJsonAny::boolean(true));
#else
    features->set("complex_send", SrsJsonAny::boolean(false));
#endif
#ifdef SRS_PERF_TCP_NODELAY
    features->set("tcp_nodelay", SrsJsonAny::boolean(true));
#else
    features->set("tcp_nodelay", SrsJsonAny::boolean(false));
#endif
#ifdef SRS_PERF_SO_SNDBUF_SIZE
    features->set("so_sendbuf", SrsJsonAny::boolean(true));
#else
    features->set("so_sendbuf", SrsJsonAny::boolean(false));
#endif
#ifdef SRS_PERF_MERGED_READ
    features->set("mr", SrsJsonAny::boolean(true));
#else
    features->set("mr", SrsJsonAny::boolean(false));
#endif
    
    return srs_api_response(w, r, obj->dumps());
}

SrsGoApiRequests::SrsGoApiRequests()
{
}

SrsGoApiRequests::~SrsGoApiRequests()
{
}

int SrsGoApiRequests::serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r)
{
    SrsStatistic* stat = SrsStatistic::instance();
    
    SrsJsonObject* obj = SrsJsonAny::object();
    SrsAutoFree(SrsJsonObject, obj);
    
    obj->set("code", SrsJsonAny::integer(ERROR_SUCCESS));
    obj->set("server", SrsJsonAny::integer(stat->server_id()));
    
    SrsJsonObject* data = SrsJsonAny::object();
    obj->set("data", data);
    
    data->set("uri", SrsJsonAny::str(r->uri().c_str()));
    data->set("path", SrsJsonAny::str(r->path().c_str()));
    
    // method
    data->set("METHOD", SrsJsonAny::str(r->method_str().c_str()));
    
    // request headers
    SrsJsonObject* headers = SrsJsonAny::object();
    data->set("headers", headers);
    
    for (int i = 0; i < r->request_header_count(); i++) {
        std::string key = r->request_header_key_at(i);
        std::string value = r->request_header_value_at(i);
        headers->set(key, SrsJsonAny::str(value.c_str()));
    }
    
    // server informations
    SrsJsonObject* server = SrsJsonAny::object();
    data->set("headers", server);
    
    server->set("sigature", SrsJsonAny::str(RTMP_SIG_SRS_KEY));
    server->set("version", SrsJsonAny::str(RTMP_SIG_SRS_VERSION));
    server->set("link", SrsJsonAny::str(RTMP_SIG_SRS_URL));
    server->set("time", SrsJsonAny::integer(srs_get_system_time_ms()));
    
    return srs_api_response(w, r, obj->dumps());
}

SrsGoApiVhosts::SrsGoApiVhosts()
{
}

SrsGoApiVhosts::~SrsGoApiVhosts()
{
}

int SrsGoApiVhosts::serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r)
{
    int ret = ERROR_SUCCESS;
    
    SrsStatistic* stat = SrsStatistic::instance();
    
    // path: {pattern}{vhost_id}
    // e.g. /api/v1/vhosts/100     pattern= /api/v1/vhosts/, vhost_id=100
    int vid = r->parse_rest_id(entry->pattern);
    SrsStatisticVhost* vhost = NULL;
    
    if (vid > 0 && (vhost = stat->find_vhost(vid)) == NULL) {
        ret = ERROR_RTMP_VHOST_NOT_FOUND;
        srs_error("vhost id=%d not found. ret=%d", vid, ret);
        return srs_api_response_code(w, r, ret);
    }
    
    SrsJsonObject* obj = SrsJsonAny::object();
    SrsAutoFree(SrsJsonObject, obj);
    
    obj->set("code", SrsJsonAny::integer(ERROR_SUCCESS));
    obj->set("server", SrsJsonAny::integer(stat->server_id()));
    
    if (r->is_http_get()) {
        if (!vhost) {
            SrsJsonArray* data = SrsJsonAny::array();
            obj->set("vhosts", data);
            
            if ((ret = stat->dumps_vhosts(data)) != ERROR_SUCCESS) {
                return srs_api_response_code(w, r, ret);
            }
        } else {
            SrsJsonObject* data = SrsJsonAny::object();
            obj->set("vhost", data);;
            
            if ((ret = vhost->dumps(data)) != ERROR_SUCCESS) {
                return srs_api_response_code(w, r, ret);
            }
        }
    } else {
        return srs_go_http_error(w, SRS_CONSTS_HTTP_MethodNotAllowed);
    }
    
    return srs_api_response(w, r, obj->dumps());
}

SrsGoApiStreams::SrsGoApiStreams()
{
}

SrsGoApiStreams::~SrsGoApiStreams()
{
}

int SrsGoApiStreams::serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r)
{
    int ret = ERROR_SUCCESS;
    
    SrsStatistic* stat = SrsStatistic::instance();
    
    // path: {pattern}{stream_id}
    // e.g. /api/v1/streams/100     pattern= /api/v1/streams/, stream_id=100
    int sid = r->parse_rest_id(entry->pattern);
    
    SrsStatisticStream* stream = NULL;
    if (sid >= 0 && (stream = stat->find_stream(sid)) == NULL) {
        ret = ERROR_RTMP_STREAM_NOT_FOUND;
        srs_error("stream id=%d not found. ret=%d", sid, ret);
        return srs_api_response_code(w, r, ret);
    }
    
    SrsJsonObject* obj = SrsJsonAny::object();
    SrsAutoFree(SrsJsonObject, obj);
    
    obj->set("code", SrsJsonAny::integer(ERROR_SUCCESS));
    obj->set("server", SrsJsonAny::integer(stat->server_id()));
    
    if (r->is_http_get()) {
        if (!stream) {
            SrsJsonArray* data = SrsJsonAny::array();
            obj->set("streams", data);
            
            if ((ret = stat->dumps_streams(data)) != ERROR_SUCCESS) {
                return srs_api_response_code(w, r, ret);
            }
        } else {
            SrsJsonObject* data = SrsJsonAny::object();
            obj->set("stream", data);;
            
            if ((ret = stream->dumps(data)) != ERROR_SUCCESS) {
                return srs_api_response_code(w, r, ret);
            }
        }
    } else {
        return srs_go_http_error(w, SRS_CONSTS_HTTP_MethodNotAllowed);
    }
    
    return srs_api_response(w, r, obj->dumps());
}

SrsGoApiClients::SrsGoApiClients()
{
}

SrsGoApiClients::~SrsGoApiClients()
{
}

int SrsGoApiClients::serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r)
{
    int ret = ERROR_SUCCESS;
    
    SrsStatistic* stat = SrsStatistic::instance();
    
    // path: {pattern}{client_id}
    // e.g. /api/v1/clients/100     pattern= /api/v1/clients/, client_id=100
    int cid = r->parse_rest_id(entry->pattern);
    
    SrsStatisticClient* client = NULL;
    if (cid >= 0 && (client = stat->find_client(cid)) == NULL) {
        ret = ERROR_RTMP_CLIENT_NOT_FOUND;
        srs_error("client id=%d not found. ret=%d", cid, ret);
        return srs_api_response_code(w, r, ret);
    }
    
    SrsJsonObject* obj = SrsJsonAny::object();
    SrsAutoFree(SrsJsonObject, obj);
    
    obj->set("code", SrsJsonAny::integer(ERROR_SUCCESS));
    obj->set("server", SrsJsonAny::integer(stat->server_id()));
    
    if (r->is_http_get()) {
        if (!client) {
            SrsJsonArray* data = SrsJsonAny::array();
            obj->set("clients", data);
            
            std::string rstart = r->query_get("start");
            std::string rcount = r->query_get("count");
            int start = srs_max(0, atoi(rstart.c_str()));
            int count = srs_max(10, atoi(rcount.c_str()));
            if ((ret = stat->dumps_clients(data, start, count)) != ERROR_SUCCESS) {
                return srs_api_response_code(w, r, ret);
            }
        } else {
            SrsJsonObject* data = SrsJsonAny::object();
            obj->set("client", data);;
            
            if ((ret = client->dumps(data)) != ERROR_SUCCESS) {
                return srs_api_response_code(w, r, ret);
            }
        }
    } else if (r->is_http_delete()) {
        if (!client) {
            ret = ERROR_RTMP_CLIENT_NOT_FOUND;
            srs_error("client id=%d not found. ret=%d", cid, ret);
            return srs_api_response_code(w, r, ret);
        }
        
        client->conn->expire();
        srs_warn("kickoff client id=%d ok", cid);
    } else {
        return srs_go_http_error(w, SRS_CONSTS_HTTP_MethodNotAllowed);
    }
    
    return srs_api_response(w, r, obj->dumps());
}

SrsGoApiRaw::SrsGoApiRaw(SrsServer* svr)
{
    server = svr;
    
    raw_api = _srs_config->get_raw_api();
    allow_reload = _srs_config->get_raw_api_allow_reload();
    allow_query = _srs_config->get_raw_api_allow_query();
    allow_update = _srs_config->get_raw_api_allow_update();
    
    _srs_config->subscribe(this);
}

SrsGoApiRaw::~SrsGoApiRaw()
{
    _srs_config->unsubscribe(this);
}

int SrsGoApiRaw::serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r)
{
    int ret = ERROR_SUCCESS;
    
    std::string rpc = r->query_get("rpc");
    
    // the object to return for request.
    SrsJsonObject* obj = SrsJsonAny::object();
    SrsAutoFree(SrsJsonObject, obj);
    obj->set("code", SrsJsonAny::integer(ERROR_SUCCESS));
    
    // for rpc=raw, to query the raw api config for http api.
    if (rpc == "raw") {
        // query global scope.
        if ((ret = _srs_config->raw_to_json(obj)) != ERROR_SUCCESS) {
            srs_error("raw api rpc raw failed. ret=%d", ret);
            return srs_api_response_code(w, r, ret);
        }
        
        return srs_api_response(w, r, obj->dumps());
    }
    
    // whether enabled the HTTP RAW API.
    if (!raw_api) {
        ret = ERROR_SYSTEM_CONFIG_RAW_DISABLED;
        srs_warn("raw api disabled. ret=%d", ret);
        return srs_api_response_code(w, r, ret);
    }
    
    //////////////////////////////////////////////////////////////////////////
    // the rpc is required.
    // the allowd rpc method check.
    if (rpc.empty() || (rpc != "reload" && rpc != "query" && rpc != "raw" && rpc != "update")) {
        ret = ERROR_SYSTEM_CONFIG_RAW;
        srs_error("raw api invalid rpc=%s. ret=%d", rpc.c_str(), ret);
        return srs_api_response_code(w, r, ret);
    }
    
    // for rpc=reload, trigger the server to reload the config.
    if (rpc == "reload") {
        if (!allow_reload) {
            ret = ERROR_SYSTEM_CONFIG_RAW_DISABLED;
            srs_error("raw api reload disabled rpc=%s. ret=%d", rpc.c_str(), ret);
            return srs_api_response_code(w, r, ret);
        }
        
        srs_trace("raw api trigger reload. ret=%d", ret);
        server->on_signal(SRS_SIGNAL_RELOAD);
        return srs_api_response_code(w, r, ret);
    }
    
    // for rpc=query, to get the configs of server.
    //      @param scope the scope to query for config, it can be:
    //              global, the configs belongs to the root, donot includes any sub directives.
    //              minimal, the minimal summary of server, for preview stream to got the port serving.
    //              vhost, the configs for specified vhost by @param vhost.
    //      @param vhost the vhost name for @param scope is vhost to query config.
    //              for the default vhost, must be __defaultVhost__
    if (rpc == "query") {
        if (!allow_query) {
            ret = ERROR_SYSTEM_CONFIG_RAW_DISABLED;
            srs_error("raw api allow_query disabled rpc=%s. ret=%d", rpc.c_str(), ret);
            return srs_api_response_code(w, r, ret);
        }
        
        std::string scope = r->query_get("scope");
        std::string vhost = r->query_get("vhost");
        if (scope.empty() || (scope != "global" && scope != "vhost" && scope != "minimal")) {
            ret = ERROR_SYSTEM_CONFIG_RAW_NOT_ALLOWED;
            srs_error("raw api query invalid scope=%s. ret=%d", scope.c_str(), ret);
            return srs_api_response_code(w, r, ret);
        }
        
        if (scope == "vhost") {
            // query vhost scope.
            if (vhost.empty()) {
                ret = ERROR_SYSTEM_CONFIG_RAW_PARAMS;
                srs_error("raw api query vhost invalid vhost=%s. ret=%d", vhost.c_str(), ret);
                return srs_api_response_code(w, r, ret);
            }
            
            SrsConfDirective* root = _srs_config->get_root();
            SrsConfDirective* conf = root->get("vhost", vhost);
            if (!conf) {
                ret = ERROR_SYSTEM_CONFIG_RAW_PARAMS;
                srs_error("raw api query vhost invalid vhost=%s. ret=%d", vhost.c_str(), ret);
                return srs_api_response_code(w, r, ret);
            }
            
            SrsJsonObject* data = SrsJsonAny::object();
            obj->set("vhost", data);
            if ((ret = _srs_config->vhost_to_json(conf, data)) != ERROR_SUCCESS) {
                srs_error("raw api query vhost failed. ret=%d", ret);
                return srs_api_response_code(w, r, ret);
            }
        } else if (scope == "minimal") {
            SrsJsonObject* data = SrsJsonAny::object();
            obj->set("minimal", data);
            
            // query minimal scope.
            if ((ret = _srs_config->minimal_to_json(data)) != ERROR_SUCCESS) {
                srs_error("raw api query global failed. ret=%d", ret);
                return srs_api_response_code(w, r, ret);
            }
        } else {
            SrsJsonObject* data = SrsJsonAny::object();
            obj->set("global", data);
            
            // query global scope.
            if ((ret = _srs_config->global_to_json(data)) != ERROR_SUCCESS) {
                srs_error("raw api query global failed. ret=%d", ret);
                return srs_api_response_code(w, r, ret);
            }
        }
        
        return srs_api_response(w, r, obj->dumps());
    }
    
    // for rpc=update, to update the configs of server.
    //      @scope the scope to update for config.
    //      @value the updated value for scope.
    //      @param the extra param for scope.
    //      @data the extra data for scope.
    // possible updates:
    //      @scope          @value              value-description
    //      listen          1935,1936           the port list.
    //      pid             ./objs/srs.pid      the pid file of srs.
    //      chunk_size      60000               the global RTMP chunk_size.
    //      ff_log_dir      ./objs              the dir for ffmpeg log.
    //      srs_log_tank    file                the tank to log, file or console.
    //      srs_log_level   trace               the level of log, verbose, info, trace, warn, error.
    //      srs_log_file    ./objs/srs.log      the log file when tank is file.
    //      max_connections 1000                the max connections of srs.
    //      utc_time        false               whether enable utc time.
    //      pithy_print_ms  10000               the pithy print interval in ms.
    // vhost specified updates:
    //      @scope          @value              @param              @data               description
    //      vhost           ossrs.net           create              -                   create vhost ossrs.net
    //      vhost           ossrs.net           update              new.ossrs.net       the new name to update vhost
    // dvr specified updates:
    //      @scope          @value              @param              @data               description
    //      dvr             ossrs.net           enable              live/livestream     enable the dvr of stream
    //      dvr             ossrs.net           disable             live/livestream     disable the dvr of stream
    if (rpc == "update") {
        if (!allow_update) {
            ret = ERROR_SYSTEM_CONFIG_RAW_DISABLED;
            srs_error("raw api allow_update disabled rpc=%s. ret=%d", rpc.c_str(), ret);
            return srs_api_response_code(w, r, ret);
        }
        
        std::string scope = r->query_get("scope");
        std::string value = r->query_get("value");
        if (scope.empty()) {
            ret = ERROR_SYSTEM_CONFIG_RAW_NOT_ALLOWED;
            srs_error("raw api query invalid empty scope. ret=%d", ret);
            return srs_api_response_code(w, r, ret);
        }
        if (scope != "listen" && scope != "pid" && scope != "chunk_size"
            && scope != "ff_log_dir" && scope != "srs_log_tank" && scope != "srs_log_level"
            && scope != "srs_log_file" && scope != "max_connections" && scope != "utc_time"
            && scope != "pithy_print_ms" && scope != "vhost" && scope != "dvr"
            ) {
            ret = ERROR_SYSTEM_CONFIG_RAW_NOT_ALLOWED;
            srs_error("raw api query invalid scope=%s. ret=%d", scope.c_str(), ret);
            return srs_api_response_code(w, r, ret);
        }
        
        bool applied = false;
        string extra = "";
        if (scope == "listen") {
            vector<string> eps = srs_string_split(value, ",");
            
            bool invalid = eps.empty();
            for (int i = 0; i < (int)eps.size(); i++) {
                string ep = eps.at(i);
                int port = ::atoi(ep.c_str());
                if (port <= 2 || port >= 65535) {
                    invalid = true;
                    break;
                }
            }
            if (invalid) {
                ret = ERROR_SYSTEM_CONFIG_RAW_PARAMS;
                srs_error("raw api update check listen=%s failed. ret=%d", value.c_str(), ret);
                return srs_api_response_code(w, r, ret);
            }
            
            if ((ret = _srs_config->raw_set_listen(eps, applied)) != ERROR_SUCCESS) {
                srs_error("raw api update listen=%s failed. ret=%d", value.c_str(), ret);
                return srs_api_response_code(w, r, ret);
            }
        } else if (scope == "pid") {
            if (value.empty() || !srs_string_starts_with(value, "./", "/tmp/", "/var/") || !srs_string_ends_with(value, ".pid")) {
                ret = ERROR_SYSTEM_CONFIG_RAW_PARAMS;
                srs_error("raw api update check pid=%s failed. ret=%d", value.c_str(), ret);
                return srs_api_response_code(w, r, ret);
            }
            
            if ((ret = _srs_config->raw_set_pid(value, applied)) != ERROR_SUCCESS) {
                srs_error("raw api update pid=%s failed. ret=%d", value.c_str(), ret);
                return srs_api_response_code(w, r, ret);
            }
        } else if (scope == "chunk_size") {
            int csv = ::atoi(value.c_str());
            if (csv < 128 || csv > 65535 || !srs_is_digit_number(value)) {
                ret = ERROR_SYSTEM_CONFIG_RAW_PARAMS;
                srs_error("raw api update check chunk_size=%s/%d failed. ret=%d", value.c_str(), csv, ret);
                return srs_api_response_code(w, r, ret);
            }
            
            if ((ret = _srs_config->raw_set_chunk_size(value, applied)) != ERROR_SUCCESS) {
                srs_error("raw api update chunk_size=%s/%d failed. ret=%d", value.c_str(), csv, ret);
                return srs_api_response_code(w, r, ret);
            }
        } else if (scope == "ff_log_dir") {
            if (value.empty() || (value != "/dev/null" && !srs_string_starts_with(value, "./", "/tmp/", "/var/"))) {
                ret = ERROR_SYSTEM_CONFIG_RAW_PARAMS;
                srs_error("raw api update check ff_log_dir=%s failed. ret=%d", value.c_str(), ret);
                return srs_api_response_code(w, r, ret);
            }
            
            if ((ret = _srs_config->raw_set_ff_log_dir(value, applied)) != ERROR_SUCCESS) {
                srs_error("raw api update ff_log_dir=%s failed. ret=%d", value.c_str(), ret);
                return srs_api_response_code(w, r, ret);
            }
        } else if (scope == "srs_log_tank") {
            if (value.empty() || (value != "file" && value != "console")) {
                ret = ERROR_SYSTEM_CONFIG_RAW_PARAMS;
                srs_error("raw api update check srs_log_tank=%s failed. ret=%d", value.c_str(), ret);
                return srs_api_response_code(w, r, ret);
            }
            
            if ((ret = _srs_config->raw_set_srs_log_tank(value, applied)) != ERROR_SUCCESS) {
                srs_error("raw api update srs_log_tank=%s failed. ret=%d", value.c_str(), ret);
                return srs_api_response_code(w, r, ret);
            }
        } else if (scope == "srs_log_level") {
            if (value != "verbose" && value != "info" && value != "trace" && value != "warn" && value != "error") {
                ret = ERROR_SYSTEM_CONFIG_RAW_PARAMS;
                srs_error("raw api update check srs_log_level=%s failed. ret=%d", value.c_str(), ret);
                return srs_api_response_code(w, r, ret);
            }
            
            if ((ret = _srs_config->raw_set_srs_log_level(value, applied)) != ERROR_SUCCESS) {
                srs_error("raw api update srs_log_level=%s failed. ret=%d", value.c_str(), ret);
                return srs_api_response_code(w, r, ret);
            }
        } else if (scope == "srs_log_file") {
            if (value.empty() || !srs_string_starts_with(value, "./", "/tmp/", "/var/") || !srs_string_ends_with(value, ".log")) {
                ret = ERROR_SYSTEM_CONFIG_RAW_PARAMS;
                srs_error("raw api update check srs_log_file=%s failed. ret=%d", value.c_str(), ret);
                return srs_api_response_code(w, r, ret);
            }
            
            if ((ret = _srs_config->raw_set_srs_log_file(value, applied)) != ERROR_SUCCESS) {
                srs_error("raw api update srs_log_file=%s failed. ret=%d", value.c_str(), ret);
                return srs_api_response_code(w, r, ret);
            }
        } else if (scope == "max_connections") {
            int mcv = ::atoi(value.c_str());
            if (mcv < 10 || mcv > 65535 || !srs_is_digit_number(value)) {
                ret = ERROR_SYSTEM_CONFIG_RAW_PARAMS;
                srs_error("raw api update check max_connections=%s/%d failed. ret=%d", value.c_str(), mcv, ret);
                return srs_api_response_code(w, r, ret);
            }
            
            if ((ret = _srs_config->raw_set_max_connections(value, applied)) != ERROR_SUCCESS) {
                srs_error("raw api update max_connections=%s/%d failed. ret=%d", value.c_str(), mcv, ret);
                return srs_api_response_code(w, r, ret);
            }
        } else if (scope == "utc_time") {
            if (!srs_is_boolean(value)) {
                ret = ERROR_SYSTEM_CONFIG_RAW_PARAMS;
                srs_error("raw api update check utc_time=%s failed. ret=%d", value.c_str(), ret);
                return srs_api_response_code(w, r, ret);
            }
            
            if ((ret = _srs_config->raw_set_utc_time(srs_config_bool2switch(value), applied)) != ERROR_SUCCESS) {
                srs_error("raw api update utc_time=%s failed. ret=%d", value.c_str(), ret);
                return srs_api_response_code(w, r, ret);
            }
        } else if (scope == "pithy_print_ms") {
            int ppmv = ::atoi(value.c_str());
            if (ppmv < 100 || ppmv > 300000 || !srs_is_digit_number(value)) {
                ret = ERROR_SYSTEM_CONFIG_RAW_PARAMS;
                srs_error("raw api update check pithy_print_ms=%s/%d failed. ret=%d", value.c_str(), ppmv, ret);
                return srs_api_response_code(w, r, ret);
            }
            
            if ((ret = _srs_config->raw_set_pithy_print_ms(value, applied)) != ERROR_SUCCESS) {
                srs_error("raw api update pithy_print_ms=%s/%d failed. ret=%d", value.c_str(), ppmv, ret);
                return srs_api_response_code(w, r, ret);
            }
        } else if (scope == "vhost") {
            std::string param = r->query_get("param");
            std::string data = r->query_get("data");
            if (param != "create" && param != "update" && param != "delete" && param != "disable" && param != "enable") {
                ret = ERROR_SYSTEM_CONFIG_RAW_NOT_ALLOWED;
                srs_error("raw api query invalid scope=%s, param=%s. ret=%d", scope.c_str(), param.c_str(), ret);
                return srs_api_response_code(w, r, ret);
            }
            extra += " " + param;
            
            if (param == "create") {
                // when create, the vhost must not exists.
                if (param.empty() || _srs_config->get_vhost(value, false)) {
                    ret = ERROR_SYSTEM_CONFIG_RAW_PARAMS;
                    srs_error("raw api update check vhost=%s, param=%s failed. ret=%d", value.c_str(), param.c_str(), ret);
                    return srs_api_response_code(w, r, ret);
                }
                
                if ((ret = _srs_config->raw_create_vhost(value, applied)) != ERROR_SUCCESS) {
                    srs_error("raw api update vhost=%s, param=%s failed. ret=%d", value.c_str(), param.c_str(), ret);
                    return srs_api_response_code(w, r, ret);
                }
            } else if (param == "update") {
                extra += " to " + data;
                
                // when update, the vhost must exists and disabled.
                SrsConfDirective* vhost = _srs_config->get_vhost(value, false);
                if (data.empty() || data == value || param.empty() || !vhost || _srs_config->get_vhost_enabled(vhost)) {
                    ret = ERROR_SYSTEM_CONFIG_RAW_PARAMS;
                    srs_error("raw api update check vhost=%s, param=%s, data=%s failed. ret=%d", value.c_str(), param.c_str(), data.c_str(), ret);
                    return srs_api_response_code(w, r, ret);
                }
                
                if ((ret = _srs_config->raw_update_vhost(value, data, applied)) != ERROR_SUCCESS) {
                    srs_error("raw api update vhost=%s, param=%s, data=%s failed. ret=%d", value.c_str(), param.c_str(), data.c_str(), ret);
                    return srs_api_response_code(w, r, ret);
                }
            } else if (param == "delete") {
                // when delete, the vhost must exists and disabled.
                SrsConfDirective* vhost = _srs_config->get_vhost(value, false);
                if (param.empty() || !vhost || _srs_config->get_vhost_enabled(vhost)) {
                    ret = ERROR_SYSTEM_CONFIG_RAW_PARAMS;
                    srs_error("raw api update check vhost=%s, param=%s failed. ret=%d", value.c_str(), param.c_str(), ret);
                    return srs_api_response_code(w, r, ret);
                }
                
                if ((ret = _srs_config->raw_delete_vhost(value, applied)) != ERROR_SUCCESS) {
                    srs_error("raw api update vhost=%s, param=%s failed. ret=%d", value.c_str(), param.c_str(), ret);
                    return srs_api_response_code(w, r, ret);
                }
            } else if (param == "disable") {
                // when disable, the vhost must exists and enabled.
                SrsConfDirective* vhost = _srs_config->get_vhost(value, false);
                if (param.empty() || !vhost || !_srs_config->get_vhost_enabled(vhost)) {
                    ret = ERROR_SYSTEM_CONFIG_RAW_PARAMS;
                    srs_error("raw api update check vhost=%s, param=%s failed. ret=%d", value.c_str(), param.c_str(), ret);
                    return srs_api_response_code(w, r, ret);
                }
                
                if ((ret = _srs_config->raw_disable_vhost(value, applied)) != ERROR_SUCCESS) {
                    srs_error("raw api update vhost=%s, param=%s failed. ret=%d", value.c_str(), param.c_str(), ret);
                    return srs_api_response_code(w, r, ret);
                }
            } else if (param == "enable") {
                // when enable, the vhost must exists and disabled.
                SrsConfDirective* vhost = _srs_config->get_vhost(value, false);
                if (param.empty() || !vhost || _srs_config->get_vhost_enabled(vhost)) {
                    ret = ERROR_SYSTEM_CONFIG_RAW_PARAMS;
                    srs_error("raw api update check vhost=%s, param=%s failed. ret=%d", value.c_str(), param.c_str(), ret);
                    return srs_api_response_code(w, r, ret);
                }
                
                if ((ret = _srs_config->raw_enable_vhost(value, applied)) != ERROR_SUCCESS) {
                    srs_error("raw api update vhost=%s, param=%s failed. ret=%d", value.c_str(), param.c_str(), ret);
                    return srs_api_response_code(w, r, ret);
                }
            } else {
                // TODO: support other param.
            }
        } else if (scope == "dvr") {
            std::string action = r->query_get("param");
            std::string stream = r->query_get("data");
            extra += "/" + stream + " to " + action;
            
            if (action != "enable" && action != "disable") {
                ret = ERROR_SYSTEM_CONFIG_RAW_NOT_ALLOWED;
                srs_error("raw api query invalid scope=%s, param=%s. ret=%d", scope.c_str(), action.c_str(), ret);
                return srs_api_response_code(w, r, ret);
            }
            
            if (!_srs_config->get_dvr_enabled(value)) {
                ret = ERROR_SYSTEM_CONFIG_RAW_NOT_ALLOWED;
                srs_error("raw api query invalid scope=%s, value=%s, param=%s. ret=%d", scope.c_str(), value.c_str(), action.c_str(), ret);
                return srs_api_response_code(w, r, ret);
            }
            
            if (action == "enable") {
                if ((ret = _srs_config->raw_enable_dvr(value, stream, applied)) != ERROR_SUCCESS) {
                    srs_error("raw api update dvr=%s/%s, param=%s failed. ret=%d", value.c_str(), stream.c_str(), action.c_str(), ret);
                    return srs_api_response_code(w, r, ret);
                }
            } else {
                if ((ret = _srs_config->raw_disable_dvr(value, stream, applied)) != ERROR_SUCCESS) {
                    srs_error("raw api update dvr=%s/%s, param=%s failed. ret=%d", value.c_str(), stream.c_str(), action.c_str(), ret);
                    return srs_api_response_code(w, r, ret);
                }
            }
        } else {
            // TODO: support other scope.
        }
        
        // whether the config applied.
        if (applied) {
            server->on_signal(SRS_SIGNAL_PERSISTENCE_CONFIG);
            srs_trace("raw api update %s=%s%s ok.", scope.c_str(), value.c_str(), extra.c_str());
        } else {
            srs_warn("raw api update not applied %s=%s%s.", scope.c_str(), value.c_str(), extra.c_str());
        }
        
        return srs_api_response(w, r, obj->dumps());
    }
    
    return ret;
}

int SrsGoApiRaw::on_reload_http_api_raw_api()
{
    raw_api = _srs_config->get_raw_api();
    allow_reload = _srs_config->get_raw_api_allow_reload();
    allow_query = _srs_config->get_raw_api_allow_query();
    allow_update = _srs_config->get_raw_api_allow_update();
    
    return ERROR_SUCCESS;
}

SrsGoApiError::SrsGoApiError()
{
}

SrsGoApiError::~SrsGoApiError()
{
}

int SrsGoApiError::serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r)
{
    return srs_api_response_code(w, r, 100);
}

SrsHttpApi::SrsHttpApi(IConnectionManager* cm, srs_netfd_t fd, SrsHttpServeMux* m, string cip)
: SrsConnection(cm, fd, cip)
{
    mux = m;
    cors = new SrsHttpCorsMux();
    parser = new SrsHttpParser();
    
    _srs_config->subscribe(this);
}

SrsHttpApi::~SrsHttpApi()
{
    srs_freep(parser);
    srs_freep(cors);
    
    _srs_config->unsubscribe(this);
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
    if ((ret = parser->initialize(HTTP_REQUEST, true)) != ERROR_SUCCESS) {
        srs_error("api initialize http parser failed. ret=%d", ret);
        return ret;
    }
    
    // set the recv timeout, for some clients never disconnect the connection.
    // @see https://github.com/ossrs/srs/issues/398
    skt->set_recv_timeout(SRS_HTTP_RECV_TMMS);
    
    // initialize the cors, which will proxy to mux.
    bool crossdomain_enabled = _srs_config->get_http_api_crossdomain();
    if ((ret = cors->initialize(mux, crossdomain_enabled)) != ERROR_SUCCESS) {
        return ret;
    }
    
    // process http messages.
    while (true) {
        srs_error_t err = srs_success;
        if ((err = trd->pull()) != srs_success) {
            // TODO: FIXME: Use error
            ret = srs_error_code(err);
            srs_freep(err);
            return ret;
        }
        
        ISrsHttpMessage* req = NULL;
        
        // get a http message
        if ((ret = parser->parse_message(skt, this, &req)) != ERROR_SUCCESS) {
            return ret;
        }
        
        // if SUCCESS, always NOT-NULL.
        srs_assert(req);
        
        // always free it in this scope.
        SrsAutoFree(ISrsHttpMessage, req);
        
        // ok, handle http request.
        SrsHttpResponseWriter writer(skt);
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
        // @see https://github.com/ossrs/srs/issues/399
        if (!req->is_keep_alive()) {
            break;
        }
    }
    
    return ret;
}

int SrsHttpApi::process_request(ISrsHttpResponseWriter* w, ISrsHttpMessage* r)
{
    int ret = ERROR_SUCCESS;
    
    SrsHttpMessage* hm = dynamic_cast<SrsHttpMessage*>(r);
    srs_assert(hm);
    
    srs_trace("HTTP API %s %s, content-length=%" PRId64 ", chunked=%d/%d",
              r->method_str().c_str(), r->url().c_str(), r->content_length(),
              hm->is_chunked(), hm->is_infinite_chunked());
    
    // use cors server mux to serve http request, which will proxy to mux.
    if ((ret = cors->serve_http(w, r)) != ERROR_SUCCESS) {
        if (!srs_is_client_gracefully_close(ret)) {
            srs_error("serve http msg failed. ret=%d", ret);
        }
        return ret;
    }
    
    return ret;
}

int SrsHttpApi::on_reload_http_api_crossdomain()
{
    int ret = ERROR_SUCCESS;
    
    bool crossdomain_enabled = _srs_config->get_http_api_crossdomain();
    if ((ret = cors->initialize(mux, crossdomain_enabled)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ERROR_SUCCESS;
}

