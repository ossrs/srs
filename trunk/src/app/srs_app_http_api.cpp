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
#include <srs_rtmp_amf0.hpp>

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
    SrsAmf0Object* obj = SrsAmf0Any::object();
    SrsAutoFree(SrsAmf0Object, obj);
    
    obj->set("code", SrsAmf0Any::number(code));
    
    return srs_api_response_jsonp(w, callback, obj->to_json());
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
    SrsAmf0Object* obj = SrsAmf0Any::object();
    SrsAutoFree(SrsAmf0Object, obj);
    
    obj->set("code", SrsAmf0Any::number(code));
    
    return srs_api_response_json(w, obj->to_json());
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
    
    SrsAmf0Object* obj = SrsAmf0Any::object();
    SrsAutoFree(SrsAmf0Object, obj);
    
    obj->set("code", SrsAmf0Any::number(ERROR_SUCCESS));
    obj->set("server", SrsAmf0Any::number(stat->server_id()));
    
    SrsAmf0Object* urls = SrsAmf0Any::object();
    obj->set("urls", urls);
    
    urls->set("api", SrsAmf0Any::str("the api root"));
        
    return srs_api_response(w, r, obj->to_json());
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
    
    SrsAmf0Object* obj = SrsAmf0Any::object();
    SrsAutoFree(SrsAmf0Object, obj);
    
    obj->set("code", SrsAmf0Any::number(ERROR_SUCCESS));
    obj->set("server", SrsAmf0Any::number(stat->server_id()));
    
    SrsAmf0Object* urls = SrsAmf0Any::object();
    obj->set("urls", urls);
    
    urls->set("v1", SrsAmf0Any::str("the api version 1.0"));
    
    return srs_api_response(w, r, obj->to_json());
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
    
    SrsAmf0Object* obj = SrsAmf0Any::object();
    SrsAutoFree(SrsAmf0Object, obj);
    
    obj->set("code", SrsAmf0Any::number(ERROR_SUCCESS));
    obj->set("server", SrsAmf0Any::number(stat->server_id()));
    
    SrsAmf0Object* urls = SrsAmf0Any::object();
    obj->set("urls", urls);
    
    urls->set("versions", SrsAmf0Any::str("the version of SRS"));
    urls->set("summaries", SrsAmf0Any::str("the summary(pid, argv, pwd, cpu, mem) of SRS"));
    urls->set("rusages", SrsAmf0Any::str("the rusage of SRS"));
    urls->set("self_proc_stats", SrsAmf0Any::str("the self process stats"));
    urls->set("system_proc_stats", SrsAmf0Any::str("the system process stats"));
    urls->set("meminfos", SrsAmf0Any::str("the meminfo of system"));
    urls->set("authors", SrsAmf0Any::str("the license, copyright, authors and contributors"));
    urls->set("features", SrsAmf0Any::str("the supported features of SRS"));
    urls->set("requests", SrsAmf0Any::str("the request itself, for http debug"));
    urls->set("vhosts", SrsAmf0Any::str("manage all vhosts or specified vhost"));
    urls->set("streams", SrsAmf0Any::str("manage all streams or specified stream"));
    urls->set("clients", SrsAmf0Any::str("manage all clients or specified client, default query top 10 clients"));
    urls->set("raw", SrsAmf0Any::str("raw api for srs, support CUID srs for instance the config"));
    
    SrsAmf0Object* tests = SrsAmf0Any::object();
    obj->set("tests", tests);
    
    tests->set("requests", SrsAmf0Any::str("show the request info"));
    tests->set("errors", SrsAmf0Any::str("always return an error 100"));
    tests->set("redirects", SrsAmf0Any::str("always redirect to /api/v1/test/errors"));
    tests->set("[vhost]", SrsAmf0Any::str("http vhost for http://error.srs.com:1985/api/v1/tests/errors"));
    
    return srs_api_response(w, r, obj->to_json());
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
    
    SrsAmf0Object* obj = SrsAmf0Any::object();
    SrsAutoFree(SrsAmf0Object, obj);
    
    obj->set("code", SrsAmf0Any::number(ERROR_SUCCESS));
    obj->set("server", SrsAmf0Any::number(stat->server_id()));
    
    SrsAmf0Object* data = SrsAmf0Any::object();
    obj->set("data", data);
    
    data->set("major", SrsAmf0Any::number(VERSION_MAJOR));
    data->set("minor", SrsAmf0Any::number(VERSION_MINOR));
    data->set("revision", SrsAmf0Any::number(VERSION_REVISION));
    data->set("version", SrsAmf0Any::str(RTMP_SIG_SRS_VERSION));
    
    return srs_api_response(w, r, obj->to_json());
}

SrsGoApiSummaries::SrsGoApiSummaries()
{
}

SrsGoApiSummaries::~SrsGoApiSummaries()
{
}

int SrsGoApiSummaries::serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r)
{
    SrsAmf0Object* obj = SrsAmf0Any::object();
    SrsAutoFree(SrsAmf0Object, obj);
    
    srs_api_dump_summaries(obj);
    
    return srs_api_response(w, r, obj->to_json());
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
    
    SrsAmf0Object* obj = SrsAmf0Any::object();
    SrsAutoFree(SrsAmf0Object, obj);
    
    obj->set("code", SrsAmf0Any::number(ERROR_SUCCESS));
    obj->set("server", SrsAmf0Any::number(stat->server_id()));
    
    SrsAmf0Object* data = SrsAmf0Any::object();
    obj->set("data", data);
    
    SrsRusage* ru = srs_get_system_rusage();
    
    data->set("ok", SrsAmf0Any::boolean(ru->ok));
    data->set("sample_time", SrsAmf0Any::number(ru->sample_time));
    data->set("ru_utime", SrsAmf0Any::number(ru->r.ru_utime.tv_sec));
    data->set("ru_stime", SrsAmf0Any::number(ru->r.ru_stime.tv_sec));
    data->set("ru_maxrss", SrsAmf0Any::number(ru->r.ru_maxrss));
    data->set("ru_ixrss", SrsAmf0Any::number(ru->r.ru_ixrss));
    data->set("ru_idrss", SrsAmf0Any::number(ru->r.ru_idrss));
    data->set("ru_isrss", SrsAmf0Any::number(ru->r.ru_isrss));
    data->set("ru_minflt", SrsAmf0Any::number(ru->r.ru_minflt));
    data->set("ru_majflt", SrsAmf0Any::number(ru->r.ru_majflt));
    data->set("ru_nswap", SrsAmf0Any::number(ru->r.ru_nswap));
    data->set("ru_inblock", SrsAmf0Any::number(ru->r.ru_inblock));
    data->set("ru_oublock", SrsAmf0Any::number(ru->r.ru_oublock));
    data->set("ru_msgsnd", SrsAmf0Any::number(ru->r.ru_msgsnd));
    data->set("ru_msgrcv", SrsAmf0Any::number(ru->r.ru_msgrcv));
    data->set("ru_nsignals", SrsAmf0Any::number(ru->r.ru_nsignals));
    data->set("ru_nvcsw", SrsAmf0Any::number(ru->r.ru_nvcsw));
    data->set("ru_nivcsw", SrsAmf0Any::number(ru->r.ru_nivcsw));
    
    return srs_api_response(w, r, obj->to_json());
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
    
    SrsAmf0Object* obj = SrsAmf0Any::object();
    SrsAutoFree(SrsAmf0Object, obj);
    
    obj->set("code", SrsAmf0Any::number(ERROR_SUCCESS));
    obj->set("server", SrsAmf0Any::number(stat->server_id()));
    
    SrsAmf0Object* data = SrsAmf0Any::object();
    obj->set("data", data);
    
    SrsProcSelfStat* u = srs_get_self_proc_stat();
    
    string state;
    state += (char)u->state;
    
    data->set("ok", SrsAmf0Any::boolean(u->ok));
    data->set("sample_time", SrsAmf0Any::number(u->sample_time));
    data->set("percent", SrsAmf0Any::number(u->percent));
    data->set("pid", SrsAmf0Any::number(u->pid));
    data->set("comm", SrsAmf0Any::str(u->comm));
    data->set("state", SrsAmf0Any::str(state.c_str()));
    data->set("ppid", SrsAmf0Any::number(u->ppid));
    data->set("pgrp", SrsAmf0Any::number(u->pgrp));
    data->set("session", SrsAmf0Any::number(u->session));
    data->set("tty_nr", SrsAmf0Any::number(u->tty_nr));
    data->set("tpgid", SrsAmf0Any::number(u->tpgid));
    data->set("flags", SrsAmf0Any::number(u->flags));
    data->set("minflt", SrsAmf0Any::number(u->minflt));
    data->set("cminflt", SrsAmf0Any::number(u->cminflt));
    data->set("majflt", SrsAmf0Any::number(u->majflt));
    data->set("cmajflt", SrsAmf0Any::number(u->cmajflt));
    data->set("utime", SrsAmf0Any::number(u->utime));
    data->set("stime", SrsAmf0Any::number(u->stime));
    data->set("cutime", SrsAmf0Any::number(u->cutime));
    data->set("cstime", SrsAmf0Any::number(u->cstime));
    data->set("priority", SrsAmf0Any::number(u->priority));
    data->set("nice", SrsAmf0Any::number(u->nice));
    data->set("num_threads", SrsAmf0Any::number(u->num_threads));
    data->set("itrealvalue", SrsAmf0Any::number(u->itrealvalue));
    data->set("starttime", SrsAmf0Any::number(u->starttime));
    data->set("vsize", SrsAmf0Any::number(u->vsize));
    data->set("rss", SrsAmf0Any::number(u->rss));
    data->set("rsslim", SrsAmf0Any::number(u->rsslim));
    data->set("startcode", SrsAmf0Any::number(u->startcode));
    data->set("endcode", SrsAmf0Any::number(u->endcode));
    data->set("startstack", SrsAmf0Any::number(u->startstack));
    data->set("kstkesp", SrsAmf0Any::number(u->kstkesp));
    data->set("kstkeip", SrsAmf0Any::number(u->kstkeip));
    data->set("signal", SrsAmf0Any::number(u->signal));
    data->set("blocked", SrsAmf0Any::number(u->blocked));
    data->set("sigignore", SrsAmf0Any::number(u->sigignore));
    data->set("sigcatch", SrsAmf0Any::number(u->sigcatch));
    data->set("wchan", SrsAmf0Any::number(u->wchan));
    data->set("nswap", SrsAmf0Any::number(u->nswap));
    data->set("cnswap", SrsAmf0Any::number(u->cnswap));
    data->set("exit_signal", SrsAmf0Any::number(u->exit_signal));
    data->set("processor", SrsAmf0Any::number(u->processor));
    data->set("rt_priority", SrsAmf0Any::number(u->rt_priority));
    data->set("policy", SrsAmf0Any::number(u->policy));
    data->set("delayacct_blkio_ticks", SrsAmf0Any::number(u->delayacct_blkio_ticks));
    data->set("guest_time", SrsAmf0Any::number(u->guest_time));
    data->set("cguest_time", SrsAmf0Any::number(u->cguest_time));
    
    return srs_api_response(w, r, obj->to_json());
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
    
    SrsAmf0Object* obj = SrsAmf0Any::object();
    SrsAutoFree(SrsAmf0Object, obj);
    
    obj->set("code", SrsAmf0Any::number(ERROR_SUCCESS));
    obj->set("server", SrsAmf0Any::number(stat->server_id()));
    
    SrsAmf0Object* data = SrsAmf0Any::object();
    obj->set("data", data);
    
    SrsProcSystemStat* s = srs_get_system_proc_stat();
    
    data->set("ok", SrsAmf0Any::boolean(s->ok));
    data->set("sample_time", SrsAmf0Any::number(s->sample_time));
    data->set("percent", SrsAmf0Any::number(s->percent));
    data->set("user", SrsAmf0Any::number(s->user));
    data->set("nice", SrsAmf0Any::number(s->nice));
    data->set("sys", SrsAmf0Any::number(s->sys));
    data->set("idle", SrsAmf0Any::number(s->idle));
    data->set("iowait", SrsAmf0Any::number(s->iowait));
    data->set("irq", SrsAmf0Any::number(s->irq));
    data->set("softirq", SrsAmf0Any::number(s->softirq));
    data->set("steal", SrsAmf0Any::number(s->steal));
    data->set("guest", SrsAmf0Any::number(s->guest));
    
    return srs_api_response(w, r, obj->to_json());
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
    
    SrsAmf0Object* obj = SrsAmf0Any::object();
    SrsAutoFree(SrsAmf0Object, obj);
    
    obj->set("code", SrsAmf0Any::number(ERROR_SUCCESS));
    obj->set("server", SrsAmf0Any::number(stat->server_id()));
    
    SrsAmf0Object* data = SrsAmf0Any::object();
    obj->set("data", data);
    
    SrsMemInfo* m = srs_get_meminfo();
    
    data->set("ok", SrsAmf0Any::boolean(m->ok));
    data->set("sample_time", SrsAmf0Any::number(m->sample_time));
    data->set("percent_ram", SrsAmf0Any::number(m->percent_ram));
    data->set("percent_swap", SrsAmf0Any::number(m->percent_swap));
    data->set("MemActive", SrsAmf0Any::number(m->MemActive));
    data->set("RealInUse", SrsAmf0Any::number(m->RealInUse));
    data->set("NotInUse", SrsAmf0Any::number(m->NotInUse));
    data->set("MemTotal", SrsAmf0Any::number(m->MemTotal));
    data->set("MemFree", SrsAmf0Any::number(m->MemFree));
    data->set("Buffers", SrsAmf0Any::number(m->Buffers));
    data->set("Cached", SrsAmf0Any::number(m->Cached));
    data->set("SwapTotal", SrsAmf0Any::number(m->SwapTotal));
    data->set("SwapFree", SrsAmf0Any::number(m->SwapFree));
    
    return srs_api_response(w, r, obj->to_json());
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
    
    SrsAmf0Object* obj = SrsAmf0Any::object();
    SrsAutoFree(SrsAmf0Object, obj);
    
    obj->set("code", SrsAmf0Any::number(ERROR_SUCCESS));
    obj->set("server", SrsAmf0Any::number(stat->server_id()));
    
    SrsAmf0Object* data = SrsAmf0Any::object();
    obj->set("data", data);
    
    data->set("primary", SrsAmf0Any::str(RTMP_SIG_SRS_PRIMARY));
    data->set("license", SrsAmf0Any::str(RTMP_SIG_SRS_LICENSE));
    data->set("copyright", SrsAmf0Any::str(RTMP_SIG_SRS_COPYRIGHT));
    data->set("authors", SrsAmf0Any::str(RTMP_SIG_SRS_AUTHROS));
    data->set("contributors_link", SrsAmf0Any::str(RTMP_SIG_SRS_CONTRIBUTORS_URL));
    data->set("contributors", SrsAmf0Any::str(SRS_AUTO_CONSTRIBUTORS));
    
    return srs_api_response(w, r, obj->to_json());
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
    
    SrsAmf0Object* obj = SrsAmf0Any::object();
    SrsAutoFree(SrsAmf0Object, obj);
    
    obj->set("code", SrsAmf0Any::number(ERROR_SUCCESS));
    obj->set("server", SrsAmf0Any::number(stat->server_id()));
    
    SrsAmf0Object* data = SrsAmf0Any::object();
    obj->set("data", data);
    
    data->set("options", SrsAmf0Any::str(SRS_AUTO_USER_CONFIGURE));
    data->set("options2", SrsAmf0Any::str(SRS_AUTO_CONFIGURE));
    data->set("build", SrsAmf0Any::str(SRS_AUTO_BUILD_DATE));
    data->set("build2", SrsAmf0Any::str(SRS_AUTO_BUILD_TS));
    
    SrsAmf0Object* features = SrsAmf0Any::object();
    data->set("features", features);
    
#ifdef SRS_AUTO_SSL
    features->set("ssl", SrsAmf0Any::boolean(true));
#else
    features->set("ssl", SrsAmf0Any::boolean(false));
#endif
#ifdef SRS_AUTO_HLS
    features->set("hls", SrsAmf0Any::boolean(true));
#else
    features->set("hls", SrsAmf0Any::boolean(false));
#endif
#ifdef SRS_AUTO_HDS
    features->set("hds", SrsAmf0Any::boolean(true));
#else
    features->set("hds", SrsAmf0Any::boolean(false));
#endif
#ifdef SRS_AUTO_HTTP_CALLBACK
    features->set("callback", SrsAmf0Any::boolean(true));
#else
    features->set("callback", SrsAmf0Any::boolean(false));
#endif
#ifdef SRS_AUTO_HTTP_API
    features->set("api", SrsAmf0Any::boolean(true));
#else
    features->set("api", SrsAmf0Any::boolean(false));
#endif
#ifdef SRS_AUTO_HTTP_SERVER
    features->set("httpd", SrsAmf0Any::boolean(true));
#else
    features->set("httpd", SrsAmf0Any::boolean(false));
#endif
#ifdef SRS_AUTO_DVR
    features->set("dvr", SrsAmf0Any::boolean(true));
#else
    features->set("dvr", SrsAmf0Any::boolean(false));
#endif
#ifdef SRS_AUTO_TRANSCODE
    features->set("transcode", SrsAmf0Any::boolean(true));
#else
    features->set("transcode", SrsAmf0Any::boolean(false));
#endif
#ifdef SRS_AUTO_INGEST
    features->set("ingest", SrsAmf0Any::boolean(true));
#else
    features->set("ingest", SrsAmf0Any::boolean(false));
#endif
#ifdef SRS_AUTO_STAT
    features->set("stat", SrsAmf0Any::boolean(true));
#else
    features->set("stat", SrsAmf0Any::boolean(false));
#endif
#ifdef SRS_AUTO_NGINX
    features->set("nginx", SrsAmf0Any::boolean(true));
#else
    features->set("nginx", SrsAmf0Any::boolean(false));
#endif
#ifdef SRS_AUTO_FFMPEG_TOOL
    features->set("ffmpeg", SrsAmf0Any::boolean(true));
#else
    features->set("ffmpeg", SrsAmf0Any::boolean(false));
#endif
#ifdef SRS_AUTO_STREAM_CASTER
    features->set("caster", SrsAmf0Any::boolean(true));
#else
    features->set("caster", SrsAmf0Any::boolean(false));
#endif
#ifdef SRS_PERF_COMPLEX_SEND
    features->set("complex_send", SrsAmf0Any::boolean(true));
#else
    features->set("complex_send", SrsAmf0Any::boolean(false));
#endif
#ifdef SRS_PERF_TCP_NODELAY
    features->set("tcp_nodelay", SrsAmf0Any::boolean(true));
#else
    features->set("tcp_nodelay", SrsAmf0Any::boolean(false));
#endif
#ifdef SRS_PERF_SO_SNDBUF_SIZE
    features->set("so_sendbuf", SrsAmf0Any::boolean(true));
#else
    features->set("so_sendbuf", SrsAmf0Any::boolean(false));
#endif
#ifdef SRS_PERF_MERGED_READ
    features->set("mr", SrsAmf0Any::boolean(true));
#else
    features->set("mr", SrsAmf0Any::boolean(false));
#endif
    
    return srs_api_response(w, r, obj->to_json());
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
    
    SrsAmf0Object* obj = SrsAmf0Any::object();
    SrsAutoFree(SrsAmf0Object, obj);
    
    obj->set("code", SrsAmf0Any::number(ERROR_SUCCESS));
    obj->set("server", SrsAmf0Any::number(stat->server_id()));
    
    SrsAmf0Object* data = SrsAmf0Any::object();
    obj->set("data", data);
    
    data->set("uri", SrsAmf0Any::str(r->uri().c_str()));
    data->set("path", SrsAmf0Any::str(r->path().c_str()));
    
    // method
    data->set("METHOD", SrsAmf0Any::str(r->method_str().c_str()));
    
    // request headers
    SrsAmf0Object* headers = SrsAmf0Any::object();
    data->set("headers", headers);
    
    for (int i = 0; i < r->request_header_count(); i++) {
        std::string key = r->request_header_key_at(i);
        std::string value = r->request_header_value_at(i);
        headers->set(key, SrsAmf0Any::str(value.c_str()));
    }
    
    // server informations
    SrsAmf0Object* server = SrsAmf0Any::object();
    data->set("headers", server);
    
    server->set("sigature", SrsAmf0Any::str(RTMP_SIG_SRS_KEY));
    server->set("name", SrsAmf0Any::str(RTMP_SIG_SRS_NAME));
    server->set("version", SrsAmf0Any::str(RTMP_SIG_SRS_VERSION));
    server->set("link", SrsAmf0Any::str(RTMP_SIG_SRS_URL));
    server->set("time", SrsAmf0Any::number(srs_get_system_time_ms()));
    
    return srs_api_response(w, r, obj->to_json());
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
    
    SrsAmf0Object* obj = SrsAmf0Any::object();
    SrsAutoFree(SrsAmf0Object, obj);
    
    obj->set("code", SrsAmf0Any::number(ERROR_SUCCESS));
    obj->set("server", SrsAmf0Any::number(stat->server_id()));
    
    if (r->is_http_get()) {
        if (!vhost) {
            SrsAmf0StrictArray* data = SrsAmf0Any::strict_array();
            obj->set("vhosts", data);
            
            if ((ret = stat->dumps_vhosts(data)) != ERROR_SUCCESS) {
                return srs_api_response_code(w, r, ret);
            }
        } else {
            SrsAmf0Object* data = SrsAmf0Any::object();
            obj->set("vhost", data);;
            
            if ((ret = vhost->dumps(data)) != ERROR_SUCCESS) {
                return srs_api_response_code(w, r, ret);
            }
        }
    } else {
        return srs_go_http_error(w, SRS_CONSTS_HTTP_MethodNotAllowed);
    }
    
    return srs_api_response(w, r, obj->to_json());
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
    
    SrsAmf0Object* obj = SrsAmf0Any::object();
    SrsAutoFree(SrsAmf0Object, obj);
    
    obj->set("code", SrsAmf0Any::number(ERROR_SUCCESS));
    obj->set("server", SrsAmf0Any::number(stat->server_id()));
    
    if (r->is_http_get()) {
        if (!stream) {
            SrsAmf0StrictArray* data = SrsAmf0Any::strict_array();
            obj->set("streams", data);
            
            if ((ret = stat->dumps_streams(data)) != ERROR_SUCCESS) {
                return srs_api_response_code(w, r, ret);
            }
        } else {
            SrsAmf0Object* data = SrsAmf0Any::object();
            obj->set("stream", data);;
            
            if ((ret = stream->dumps(data)) != ERROR_SUCCESS) {
                return srs_api_response_code(w, r, ret);
            }
        }
    } else {
        return srs_go_http_error(w, SRS_CONSTS_HTTP_MethodNotAllowed);
    }
    
    return srs_api_response(w, r, obj->to_json());
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
    
    SrsAmf0Object* obj = SrsAmf0Any::object();
    SrsAutoFree(SrsAmf0Object, obj);
    
    obj->set("code", SrsAmf0Any::number(ERROR_SUCCESS));
    obj->set("server", SrsAmf0Any::number(stat->server_id()));
    
    if (r->is_http_get()) {
        if (!client) {
            SrsAmf0StrictArray* data = SrsAmf0Any::strict_array();
            obj->set("clients", data);
            
            if ((ret = stat->dumps_clients(data, 0, 10)) != ERROR_SUCCESS) {
                return srs_api_response_code(w, r, ret);
            }
        } else {
            SrsAmf0Object* data = SrsAmf0Any::object();
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
        srs_warn("kickoff client id=%d", cid);
    } else {
        return srs_go_http_error(w, SRS_CONSTS_HTTP_MethodNotAllowed);
    }
    
    return srs_api_response(w, r, obj->to_json());
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
    SrsAmf0Object* obj = SrsAmf0Any::object();
    SrsAutoFree(SrsAmf0Object, obj);
    obj->set("code", SrsAmf0Any::number(ERROR_SUCCESS));
    
    // for rpc=raw, to query the raw api config for http api.
    if (rpc == "raw") {
        // query global scope.
        if ((ret = _srs_config->raw_to_json(obj)) != ERROR_SUCCESS) {
            srs_error("raw api rpc raw failed. ret=%d", ret);
            return srs_api_response_code(w, r, ret);
        }
        
        return srs_api_response(w, r, obj->to_json());
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
        if (scope.empty() || (scope != "global" && scope != "vhost")) {
            ret = ERROR_SYSTEM_CONFIG_RAW_PARAMS;
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
            
            SrsAmf0Object* data = SrsAmf0Any::object();
            obj->set("vhost", data);
            if ((ret = _srs_config->vhost_to_json(conf, data)) != ERROR_SUCCESS) {
                srs_error("raw api query vhost failed. ret=%d", ret);
                return srs_api_response_code(w, r, ret);
            }
        } else {
            SrsAmf0Object* data = SrsAmf0Any::object();
            obj->set("global", data);
            
            // query global scope.
            if ((ret = _srs_config->global_to_json(data)) != ERROR_SUCCESS) {
                srs_error("raw api query global failed. ret=%d", ret);
                return srs_api_response_code(w, r, ret);
            }
        }
        
        return srs_api_response(w, r, obj->to_json());
    }
    
    // for rpc=update, to update the configs of server.
    //      @param scope the scope to update for config.
    //      @param value the updated value for scope.
    // possible updates:
    //      @param scope            @param value                value-description
    //      global.listen           1935,1936                   the port list.
    if (rpc == "update") {
        if (!allow_update) {
            ret = ERROR_SYSTEM_CONFIG_RAW_DISABLED;
            srs_error("raw api allow_update disabled rpc=%s. ret=%d", rpc.c_str(), ret);
            return srs_api_response_code(w, r, ret);
        }
        
        std::string scope = r->query_get("scope");
        std::string value = r->query_get("value");
        if (scope.empty() || (scope != "global.listen")) {
            ret = ERROR_SYSTEM_CONFIG_RAW_PARAMS;
            srs_error("raw api query invalid scope=%s. ret=%d", scope.c_str(), ret);
            return srs_api_response_code(w, r, ret);
        }
        
        if (scope == "global.listen") {
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
                srs_error("raw api update global.listen=%s failed. ret=%d", value.c_str(), ret);
                return srs_api_response_code(w, r, ret);
            }
            
            if ((ret = _srs_config->raw_set_listen(eps)) != ERROR_SUCCESS) {
                srs_error("raw api update global.listen=%s failed. ret=%d", value.c_str(), ret);
                return srs_api_response_code(w, r, ret);
            }
        }
        
        server->on_signal(SRS_SIGNAL_RELOAD);
        server->on_signal(SRS_SIGNAL_PERSISTENCE_CONFIG);
        srs_trace("raw api update %s=%s ok.", scope.c_str(), value.c_str());
        
        return srs_api_response(w, r, obj->to_json());
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


SrsHttpApi::SrsHttpApi(IConnectionManager* cm, st_netfd_t fd, SrsHttpServeMux* m)
    : SrsConnection(cm, fd)
{
    mux = m;
    parser = new SrsHttpParser();
    crossdomain_required = false;
    
    _srs_config->subscribe(this);
}

SrsHttpApi::~SrsHttpApi()
{
    srs_freep(parser);
    
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
    if ((ret = parser->initialize(HTTP_REQUEST)) != ERROR_SUCCESS) {
        srs_error("api initialize http parser failed. ret=%d", ret);
        return ret;
    }
    
    // underlayer socket
    SrsStSocket skt(stfd);
    
    // set the recv timeout, for some clients never disconnect the connection.
    // @see https://github.com/simple-rtmp-server/srs/issues/398
    skt.set_recv_timeout(SRS_HTTP_RECV_TIMEOUT_US);
    
    // initialize the crossdomain
    crossdomain_enabled = _srs_config->get_http_api_crossdomain();
    
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

int SrsHttpApi::on_reload_http_api_crossdomain()
{
    crossdomain_enabled = _srs_config->get_http_api_crossdomain();
    
    return ERROR_SUCCESS;
}

#endif

