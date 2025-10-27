//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_app_http_api.hpp>

#include <signal.h>
#include <sstream>
#include <stdlib.h>
#include <unistd.h>
using namespace std;

#include <srs_app_config.hpp>
#include <srs_app_coworkers.hpp>
#include <srs_app_dvr.hpp>
#include <srs_app_http_conn.hpp>
#include <srs_app_rtmp_source.hpp>
#include <srs_app_server.hpp>
#include <srs_app_st.hpp>
#include <srs_app_statistic.hpp>
#include <srs_app_utility.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_consts.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_amf0.hpp>
#include <srs_protocol_json.hpp>
#include <srs_protocol_rtmp_stack.hpp>
#include <srs_protocol_utility.hpp>

#ifdef SRS_VALGRIND
#include <valgrind/memcheck.h>
#include <valgrind/valgrind.h>
#endif

#if defined(__linux__) || defined(SRS_OSX)
#include <sys/utsname.h>
#endif

srs_error_t srs_api_response_jsonp(ISrsHttpResponseWriter *w, string callback, string data)
{
    srs_error_t err = srs_success;

    SrsHttpHeader *h = w->header();

    h->set_content_length(data.length() + callback.length() + 2);
    h->set_content_type("text/javascript");

    if (!callback.empty() && (err = w->write((char *)callback.data(), (int)callback.length())) != srs_success) {
        return srs_error_wrap(err, "write jsonp callback");
    }

    static char *c0 = (char *)"(";
    if ((err = w->write(c0, 1)) != srs_success) {
        return srs_error_wrap(err, "write jsonp left token");
    }
    if ((err = w->write((char *)data.data(), (int)data.length())) != srs_success) {
        return srs_error_wrap(err, "write jsonp data");
    }

    static char *c1 = (char *)")";
    if ((err = w->write(c1, 1)) != srs_success) {
        return srs_error_wrap(err, "write jsonp right token");
    }

    return err;
}

srs_error_t srs_api_response_jsonp_code(ISrsHttpResponseWriter *w, string callback, int code)
{
    SrsUniquePtr<SrsJsonObject> obj(SrsJsonAny::object());

    obj->set("code", SrsJsonAny::integer(code));

    return srs_api_response_jsonp(w, callback, obj->dumps());
}

srs_error_t srs_api_response_jsonp_code(ISrsHttpResponseWriter *w, string callback, srs_error_t err)
{
    SrsUniquePtr<SrsJsonObject> obj(SrsJsonAny::object());

    obj->set("code", SrsJsonAny::integer(srs_error_code(err)));

    return srs_api_response_jsonp(w, callback, obj->dumps());
}

srs_error_t srs_api_response_json(ISrsHttpResponseWriter *w, string data)
{
    srs_error_t err = srs_success;

    SrsHttpHeader *h = w->header();

    h->set_content_length(data.length());
    if (h->content_type().empty()) {
        h->set_content_type("application/json");
    }

    if ((err = w->write((char *)data.data(), (int)data.length())) != srs_success) {
        return srs_error_wrap(err, "write json");
    }

    return err;
}

srs_error_t srs_api_response_json_code(ISrsHttpResponseWriter *w, int code)
{
    SrsUniquePtr<SrsJsonObject> obj(SrsJsonAny::object());

    obj->set("code", SrsJsonAny::integer(code));

    return srs_api_response_json(w, obj->dumps());
}

srs_error_t srs_api_response_json_code(ISrsHttpResponseWriter *w, srs_error_t code)
{
    SrsUniquePtr<SrsJsonObject> obj(SrsJsonAny::object());

    obj->set("code", SrsJsonAny::integer(srs_error_code(code)));

    return srs_api_response_json(w, obj->dumps());
}

srs_error_t srs_api_response(ISrsHttpResponseWriter *w, ISrsHttpMessage *r, std::string json)
{
    // no jsonp, directly response.
    if (!r->is_jsonp()) {
        return srs_api_response_json(w, json);
    }

    // jsonp, get function name from query("callback")
    string callback = r->query_get("callback");
    return srs_api_response_jsonp(w, callback, json);
}

srs_error_t srs_api_response_code(ISrsHttpResponseWriter *w, ISrsHttpMessage *r, int code)
{
    // no jsonp, directly response.
    if (!r->is_jsonp()) {
        return srs_api_response_json_code(w, code);
    }

    // jsonp, get function name from query("callback")
    string callback = r->query_get("callback");
    return srs_api_response_jsonp_code(w, callback, code);
}

// @remark we will free the code.
srs_error_t srs_api_response_code(ISrsHttpResponseWriter *w, ISrsHttpMessage *r, srs_error_t code)
{
    srs_error_t err = srs_success;

    // no jsonp, directly response.
    if (!r->is_jsonp()) {
        err = srs_api_response_json_code(w, code);
    } else {
        // jsonp, get function name from query("callback")
        string callback = r->query_get("callback");
        err = srs_api_response_jsonp_code(w, callback, code);
    }

    if (code != srs_success) {
        srs_warn("error %s", srs_error_desc(code).c_str());
        srs_freep(code);
    }
    return err;
}

SrsGoApiRoot::SrsGoApiRoot()
{
    stat_ = _srs_stat;
}

SrsGoApiRoot::~SrsGoApiRoot()
{
    stat_ = NULL;
}

srs_error_t SrsGoApiRoot::serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r)
{
    SrsUniquePtr<SrsJsonObject> obj(SrsJsonAny::object());

    obj->set("code", SrsJsonAny::integer(ERROR_SUCCESS));
    obj->set("server", SrsJsonAny::str(stat_->server_id().c_str()));
    obj->set("service", SrsJsonAny::str(stat_->service_id().c_str()));
    obj->set("pid", SrsJsonAny::str(stat_->service_pid().c_str()));

    SrsJsonObject *urls = SrsJsonAny::object();
    obj->set("urls", urls);

    urls->set("api", SrsJsonAny::str("the api root"));

    if (true) {
        SrsJsonObject *rtc = SrsJsonAny::object();
        urls->set("rtc", rtc);

        SrsJsonObject *v1 = SrsJsonAny::object();
        rtc->set("v1", v1);

        v1->set("play", SrsJsonAny::str("Play stream"));
        v1->set("publish", SrsJsonAny::str("Publish stream"));
        v1->set("nack", SrsJsonAny::str("Simulate the NACK"));
    }

    return srs_api_response(w, r, obj->dumps());
}

SrsGoApiApi::SrsGoApiApi()
{
    stat_ = _srs_stat;
}

SrsGoApiApi::~SrsGoApiApi()
{
    stat_ = NULL;
}

srs_error_t SrsGoApiApi::serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r)
{
    SrsUniquePtr<SrsJsonObject> obj(SrsJsonAny::object());

    obj->set("code", SrsJsonAny::integer(ERROR_SUCCESS));
    obj->set("server", SrsJsonAny::str(stat_->server_id().c_str()));
    obj->set("service", SrsJsonAny::str(stat_->service_id().c_str()));
    obj->set("pid", SrsJsonAny::str(stat_->service_pid().c_str()));

    SrsJsonObject *urls = SrsJsonAny::object();
    obj->set("urls", urls);

    urls->set("v1", SrsJsonAny::str("the api version 1.0"));

    return srs_api_response(w, r, obj->dumps());
}

SrsGoApiV1::SrsGoApiV1()
{
    stat_ = _srs_stat;
}

SrsGoApiV1::~SrsGoApiV1()
{
    stat_ = NULL;
}

srs_error_t SrsGoApiV1::serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r)
{
    SrsUniquePtr<SrsJsonObject> obj(SrsJsonAny::object());

    obj->set("code", SrsJsonAny::integer(ERROR_SUCCESS));
    obj->set("server", SrsJsonAny::str(stat_->server_id().c_str()));
    obj->set("service", SrsJsonAny::str(stat_->service_id().c_str()));
    obj->set("pid", SrsJsonAny::str(stat_->service_pid().c_str()));

    SrsJsonObject *urls = SrsJsonAny::object();
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
    urls->set("clusters", SrsJsonAny::str("origin cluster server API"));
    urls->set("perf", SrsJsonAny::str("System performance stat"));
    urls->set("tcmalloc", SrsJsonAny::str("tcmalloc api with params ?page=summary|api"));
#ifdef SRS_VALGRIND
    urls->set("valgrind", SrsJsonAny::str("valgrind api with params ?check=full|added|changed|new|quick"));
#endif
#ifdef SRS_SIGNAL_API
    urls->set("signal", SrsJsonAny::str("simulate signal api with params ?signo=SIGHUP|SIGUSR1|SIGUSR2|SIGTERM|SIGQUIT|SIGABRT|SIGINT"));
#endif

    SrsJsonObject *tests = SrsJsonAny::object();
    obj->set("tests", tests);

    tests->set("requests", SrsJsonAny::str("show the request info"));
    tests->set("errors", SrsJsonAny::str("always return an error 100"));
    tests->set("redirects", SrsJsonAny::str("always redirect to /api/v1/test/errors"));
    tests->set("[vhost]", SrsJsonAny::str("http vhost for http://error.srs.com:1985/api/v1/tests/errors"));

    return srs_api_response(w, r, obj->dumps());
}

SrsGoApiVersion::SrsGoApiVersion()
{
    stat_ = _srs_stat;
}

SrsGoApiVersion::~SrsGoApiVersion()
{
    stat_ = NULL;
}

srs_error_t SrsGoApiVersion::serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r)
{
    SrsUniquePtr<SrsJsonObject> obj(SrsJsonAny::object());

    obj->set("code", SrsJsonAny::integer(ERROR_SUCCESS));
    obj->set("server", SrsJsonAny::str(stat_->server_id().c_str()));
    obj->set("service", SrsJsonAny::str(stat_->service_id().c_str()));
    obj->set("pid", SrsJsonAny::str(stat_->service_pid().c_str()));

    SrsJsonObject *data = SrsJsonAny::object();
    obj->set("data", data);

    data->set("major", SrsJsonAny::integer(VERSION_MAJOR));
    data->set("minor", SrsJsonAny::integer(VERSION_MINOR));
    data->set("revision", SrsJsonAny::integer(VERSION_REVISION));
    data->set("version", SrsJsonAny::str(RTMP_SIG_SRS_VERSION));

    return srs_api_response(w, r, obj->dumps());
}

SrsGoApiSummaries::SrsGoApiSummaries()
{
    stat_ = _srs_stat;
}

SrsGoApiSummaries::~SrsGoApiSummaries()
{
    stat_ = NULL;
}

srs_error_t SrsGoApiSummaries::serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r)
{
    SrsUniquePtr<SrsJsonObject> obj(SrsJsonAny::object());

    obj->set("code", SrsJsonAny::integer(ERROR_SUCCESS));
    obj->set("server", SrsJsonAny::str(stat_->server_id().c_str()));
    obj->set("service", SrsJsonAny::str(stat_->service_id().c_str()));
    obj->set("pid", SrsJsonAny::str(stat_->service_pid().c_str()));

    srs_api_dump_summaries(obj.get());

    return srs_api_response(w, r, obj->dumps());
}

SrsGoApiRusages::SrsGoApiRusages()
{
    stat_ = _srs_stat;
}

SrsGoApiRusages::~SrsGoApiRusages()
{
    stat_ = NULL;
}

srs_error_t SrsGoApiRusages::serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r)
{
    SrsUniquePtr<SrsJsonObject> obj(SrsJsonAny::object());

    obj->set("code", SrsJsonAny::integer(ERROR_SUCCESS));
    obj->set("server", SrsJsonAny::str(stat_->server_id().c_str()));
    obj->set("service", SrsJsonAny::str(stat_->service_id().c_str()));
    obj->set("pid", SrsJsonAny::str(stat_->service_pid().c_str()));

    SrsJsonObject *data = SrsJsonAny::object();
    obj->set("data", data);

    SrsRusage *ru = srs_get_system_rusage();

    data->set("ok", SrsJsonAny::boolean(ru->ok_));
    data->set("sample_time", SrsJsonAny::integer(ru->sample_time_));
    data->set("ru_utime", SrsJsonAny::integer(ru->r_.ru_utime.tv_sec));
    data->set("ru_stime", SrsJsonAny::integer(ru->r_.ru_stime.tv_sec));
    data->set("ru_maxrss", SrsJsonAny::integer(ru->r_.ru_maxrss));
    data->set("ru_ixrss", SrsJsonAny::integer(ru->r_.ru_ixrss));
    data->set("ru_idrss", SrsJsonAny::integer(ru->r_.ru_idrss));
    data->set("ru_isrss", SrsJsonAny::integer(ru->r_.ru_isrss));
    data->set("ru_minflt", SrsJsonAny::integer(ru->r_.ru_minflt));
    data->set("ru_majflt", SrsJsonAny::integer(ru->r_.ru_majflt));
    data->set("ru_nswap", SrsJsonAny::integer(ru->r_.ru_nswap));
    data->set("ru_inblock", SrsJsonAny::integer(ru->r_.ru_inblock));
    data->set("ru_oublock", SrsJsonAny::integer(ru->r_.ru_oublock));
    data->set("ru_msgsnd", SrsJsonAny::integer(ru->r_.ru_msgsnd));
    data->set("ru_msgrcv", SrsJsonAny::integer(ru->r_.ru_msgrcv));
    data->set("ru_nsignals", SrsJsonAny::integer(ru->r_.ru_nsignals));
    data->set("ru_nvcsw", SrsJsonAny::integer(ru->r_.ru_nvcsw));
    data->set("ru_nivcsw", SrsJsonAny::integer(ru->r_.ru_nivcsw));

    return srs_api_response(w, r, obj->dumps());
}

SrsGoApiSelfProcStats::SrsGoApiSelfProcStats()
{
    stat_ = _srs_stat;
}

SrsGoApiSelfProcStats::~SrsGoApiSelfProcStats()
{
    stat_ = NULL;
}

srs_error_t SrsGoApiSelfProcStats::serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r)
{
    SrsUniquePtr<SrsJsonObject> obj(SrsJsonAny::object());

    obj->set("code", SrsJsonAny::integer(ERROR_SUCCESS));
    obj->set("server", SrsJsonAny::str(stat_->server_id().c_str()));
    obj->set("service", SrsJsonAny::str(stat_->service_id().c_str()));
    obj->set("pid", SrsJsonAny::str(stat_->service_pid().c_str()));

    SrsJsonObject *data = SrsJsonAny::object();
    obj->set("data", data);

    SrsProcSelfStat *u = srs_get_self_proc_stat();

    string state;
    state += (char)u->state_;

    data->set("ok", SrsJsonAny::boolean(u->ok_));
    data->set("sample_time", SrsJsonAny::integer(u->sample_time_));
    data->set("percent", SrsJsonAny::number(u->percent_));
    data->set("pid", SrsJsonAny::integer(u->pid_));
    data->set("comm", SrsJsonAny::str(u->comm_));
    data->set("state", SrsJsonAny::str(state.c_str()));
    data->set("ppid", SrsJsonAny::integer(u->ppid_));
    data->set("pgrp", SrsJsonAny::integer(u->pgrp_));
    data->set("session", SrsJsonAny::integer(u->session_));
    data->set("tty_nr", SrsJsonAny::integer(u->tty_nr_));
    data->set("tpgid", SrsJsonAny::integer(u->tpgid_));
    data->set("flags", SrsJsonAny::integer(u->flags_));
    data->set("minflt", SrsJsonAny::integer(u->minflt_));
    data->set("cminflt", SrsJsonAny::integer(u->cminflt_));
    data->set("majflt", SrsJsonAny::integer(u->majflt_));
    data->set("cmajflt", SrsJsonAny::integer(u->cmajflt_));
    data->set("utime", SrsJsonAny::integer(u->utime_));
    data->set("stime", SrsJsonAny::integer(u->stime_));
    data->set("cutime", SrsJsonAny::integer(u->cutime_));
    data->set("cstime", SrsJsonAny::integer(u->cstime_));
    data->set("priority", SrsJsonAny::integer(u->priority_));
    data->set("nice", SrsJsonAny::integer(u->nice_));
    data->set("num_threads", SrsJsonAny::integer(u->num_threads_));
    data->set("itrealvalue", SrsJsonAny::integer(u->itrealvalue_));
    data->set("starttime", SrsJsonAny::integer(u->starttime_));
    data->set("vsize", SrsJsonAny::integer(u->vsize_));
    data->set("rss", SrsJsonAny::integer(u->rss_));
    data->set("rsslim", SrsJsonAny::integer(u->rsslim_));
    data->set("startcode", SrsJsonAny::integer(u->startcode_));
    data->set("endcode", SrsJsonAny::integer(u->endcode_));
    data->set("startstack", SrsJsonAny::integer(u->startstack_));
    data->set("kstkesp", SrsJsonAny::integer(u->kstkesp_));
    data->set("kstkeip", SrsJsonAny::integer(u->kstkeip_));
    data->set("signal", SrsJsonAny::integer(u->signal_));
    data->set("blocked", SrsJsonAny::integer(u->blocked_));
    data->set("sigignore", SrsJsonAny::integer(u->sigignore_));
    data->set("sigcatch", SrsJsonAny::integer(u->sigcatch_));
    data->set("wchan", SrsJsonAny::integer(u->wchan_));
    data->set("nswap", SrsJsonAny::integer(u->nswap_));
    data->set("cnswap", SrsJsonAny::integer(u->cnswap_));
    data->set("exit_signal", SrsJsonAny::integer(u->exit_signal_));
    data->set("processor", SrsJsonAny::integer(u->processor_));
    data->set("rt_priority", SrsJsonAny::integer(u->rt_priority_));
    data->set("policy", SrsJsonAny::integer(u->policy_));
    data->set("delayacct_blkio_ticks", SrsJsonAny::integer(u->delayacct_blkio_ticks_));
    data->set("guest_time", SrsJsonAny::integer(u->guest_time_));
    data->set("cguest_time", SrsJsonAny::integer(u->cguest_time_));

    return srs_api_response(w, r, obj->dumps());
}

SrsGoApiSystemProcStats::SrsGoApiSystemProcStats()
{
    stat_ = _srs_stat;
}

SrsGoApiSystemProcStats::~SrsGoApiSystemProcStats()
{
    stat_ = NULL;
}

srs_error_t SrsGoApiSystemProcStats::serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r)
{
    SrsUniquePtr<SrsJsonObject> obj(SrsJsonAny::object());

    obj->set("code", SrsJsonAny::integer(ERROR_SUCCESS));
    obj->set("server", SrsJsonAny::str(stat_->server_id().c_str()));
    obj->set("service", SrsJsonAny::str(stat_->service_id().c_str()));
    obj->set("pid", SrsJsonAny::str(stat_->service_pid().c_str()));

    SrsJsonObject *data = SrsJsonAny::object();
    obj->set("data", data);

    SrsProcSystemStat *s = srs_get_system_proc_stat();

    data->set("ok", SrsJsonAny::boolean(s->ok_));
    data->set("sample_time", SrsJsonAny::integer(s->sample_time_));
    data->set("percent", SrsJsonAny::number(s->percent_));
    data->set("user", SrsJsonAny::integer(s->user_));
    data->set("nice", SrsJsonAny::integer(s->nice_));
    data->set("sys", SrsJsonAny::integer(s->sys_));
    data->set("idle", SrsJsonAny::integer(s->idle_));
    data->set("iowait", SrsJsonAny::integer(s->iowait_));
    data->set("irq", SrsJsonAny::integer(s->irq_));
    data->set("softirq", SrsJsonAny::integer(s->softirq_));
    data->set("steal", SrsJsonAny::integer(s->steal_));
    data->set("guest", SrsJsonAny::integer(s->guest_));

    return srs_api_response(w, r, obj->dumps());
}

SrsGoApiMemInfos::SrsGoApiMemInfos()
{
    stat_ = _srs_stat;
}

SrsGoApiMemInfos::~SrsGoApiMemInfos()
{
    stat_ = NULL;
}

srs_error_t SrsGoApiMemInfos::serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r)
{
    SrsUniquePtr<SrsJsonObject> obj(SrsJsonAny::object());

    obj->set("code", SrsJsonAny::integer(ERROR_SUCCESS));
    obj->set("server", SrsJsonAny::str(stat_->server_id().c_str()));
    obj->set("service", SrsJsonAny::str(stat_->service_id().c_str()));
    obj->set("pid", SrsJsonAny::str(stat_->service_pid().c_str()));

    SrsJsonObject *data = SrsJsonAny::object();
    obj->set("data", data);

    SrsMemInfo *m = srs_get_meminfo();

    data->set("ok", SrsJsonAny::boolean(m->ok_));
    data->set("sample_time", SrsJsonAny::integer(m->sample_time_));
    data->set("percent_ram", SrsJsonAny::number(m->percent_ram_));
    data->set("percent_swap", SrsJsonAny::number(m->percent_swap_));
    data->set("MemActive", SrsJsonAny::integer(m->MemActive_));
    data->set("RealInUse", SrsJsonAny::integer(m->RealInUse_));
    data->set("NotInUse", SrsJsonAny::integer(m->NotInUse_));
    data->set("MemTotal", SrsJsonAny::integer(m->MemTotal_));
    data->set("MemFree", SrsJsonAny::integer(m->MemFree_));
    data->set("Buffers", SrsJsonAny::integer(m->Buffers_));
    data->set("Cached", SrsJsonAny::integer(m->Cached_));
    data->set("SwapTotal", SrsJsonAny::integer(m->SwapTotal_));
    data->set("SwapFree", SrsJsonAny::integer(m->SwapFree_));

    return srs_api_response(w, r, obj->dumps());
}

SrsGoApiAuthors::SrsGoApiAuthors()
{
    stat_ = _srs_stat;
}

SrsGoApiAuthors::~SrsGoApiAuthors()
{
    stat_ = NULL;
}

srs_error_t SrsGoApiAuthors::serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r)
{
    SrsUniquePtr<SrsJsonObject> obj(SrsJsonAny::object());

    obj->set("code", SrsJsonAny::integer(ERROR_SUCCESS));
    obj->set("server", SrsJsonAny::str(stat_->server_id().c_str()));
    obj->set("service", SrsJsonAny::str(stat_->service_id().c_str()));
    obj->set("pid", SrsJsonAny::str(stat_->service_pid().c_str()));

    SrsJsonObject *data = SrsJsonAny::object();
    obj->set("data", data);

    data->set("license", SrsJsonAny::str(RTMP_SIG_SRS_LICENSE));
    data->set("contributors", SrsJsonAny::str(SRS_CONSTRIBUTORS));

    return srs_api_response(w, r, obj->dumps());
}

SrsGoApiFeatures::SrsGoApiFeatures()
{
    stat_ = _srs_stat;
}

SrsGoApiFeatures::~SrsGoApiFeatures()
{
    stat_ = NULL;
}

srs_error_t SrsGoApiFeatures::serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r)
{
    SrsUniquePtr<SrsJsonObject> obj(SrsJsonAny::object());

    obj->set("code", SrsJsonAny::integer(ERROR_SUCCESS));
    obj->set("server", SrsJsonAny::str(stat_->server_id().c_str()));
    obj->set("service", SrsJsonAny::str(stat_->service_id().c_str()));
    obj->set("pid", SrsJsonAny::str(stat_->service_pid().c_str()));

    SrsJsonObject *data = SrsJsonAny::object();
    obj->set("data", data);

    data->set("options", SrsJsonAny::str(SRS_USER_CONFIGURE));
    data->set("options2", SrsJsonAny::str(SRS_CONFIGURE));
    data->set("build", SrsJsonAny::str(SRS_BUILD_DATE));
    data->set("build2", SrsJsonAny::str(SRS_BUILD_TS));

    SrsJsonObject *features = SrsJsonAny::object();
    data->set("features", features);

    features->set("ssl", SrsJsonAny::boolean(true));
    features->set("hls", SrsJsonAny::boolean(true));
#ifdef SRS_HDS
    features->set("hds", SrsJsonAny::boolean(true));
#else
    features->set("hds", SrsJsonAny::boolean(false));
#endif
    features->set("callback", SrsJsonAny::boolean(true));
    features->set("api", SrsJsonAny::boolean(true));
    features->set("httpd", SrsJsonAny::boolean(true));
    features->set("dvr", SrsJsonAny::boolean(true));
    features->set("transcode", SrsJsonAny::boolean(true));
    features->set("ingest", SrsJsonAny::boolean(true));
    features->set("stat", SrsJsonAny::boolean(true));
    features->set("caster", SrsJsonAny::boolean(true));
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
    stat_ = _srs_stat;
}

SrsGoApiRequests::~SrsGoApiRequests()
{
    stat_ = NULL;
}

srs_error_t SrsGoApiRequests::serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r)
{
    SrsUniquePtr<SrsJsonObject> obj(SrsJsonAny::object());

    obj->set("code", SrsJsonAny::integer(ERROR_SUCCESS));
    obj->set("server", SrsJsonAny::str(stat_->server_id().c_str()));
    obj->set("service", SrsJsonAny::str(stat_->service_id().c_str()));
    obj->set("pid", SrsJsonAny::str(stat_->service_pid().c_str()));

    SrsJsonObject *data = SrsJsonAny::object();
    obj->set("data", data);

    data->set("uri", SrsJsonAny::str(r->uri().c_str()));
    data->set("path", SrsJsonAny::str(r->path().c_str()));

    // method
    data->set("METHOD", SrsJsonAny::str(r->method_str().c_str()));

    // request headers
    SrsJsonObject *headers = SrsJsonAny::object();
    data->set("headers", headers);
    r->header()->dumps(headers);

    // server informations
    SrsJsonObject *server = SrsJsonAny::object();
    data->set("headers", server);

    server->set("sigature", SrsJsonAny::str(RTMP_SIG_SRS_KEY));
    server->set("version", SrsJsonAny::str(RTMP_SIG_SRS_VERSION));
    server->set("link", SrsJsonAny::str(RTMP_SIG_SRS_URL));
    server->set("time", SrsJsonAny::integer(srsu2ms(srs_time_now_cached())));

    return srs_api_response(w, r, obj->dumps());
}

SrsGoApiVhosts::SrsGoApiVhosts()
{
    stat_ = _srs_stat;
}

SrsGoApiVhosts::~SrsGoApiVhosts()
{
    stat_ = NULL;
}

srs_error_t SrsGoApiVhosts::serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r)
{
    srs_error_t err = srs_success;

    // path: {pattern}{vhost_id}
    // e.g. /api/v1/vhosts/100     pattern= /api/v1/vhosts/, vhost_id=100
    string vid = r->parse_rest_id(entry_->pattern);
    SrsStatisticVhost *vhost = NULL;

    if (!vid.empty() && (vhost = stat_->find_vhost_by_id(vid)) == NULL) {
        return srs_api_response_code(w, r, ERROR_RTMP_VHOST_NOT_FOUND);
    }

    SrsUniquePtr<SrsJsonObject> obj(SrsJsonAny::object());

    obj->set("code", SrsJsonAny::integer(ERROR_SUCCESS));
    obj->set("server", SrsJsonAny::str(stat_->server_id().c_str()));
    obj->set("service", SrsJsonAny::str(stat_->service_id().c_str()));
    obj->set("pid", SrsJsonAny::str(stat_->service_pid().c_str()));

    if (r->is_http_get()) {
        if (!vhost) {
            SrsJsonArray *data = SrsJsonAny::array();
            obj->set("vhosts", data);

            if ((err = stat_->dumps_vhosts(data)) != srs_success) {
                int code = srs_error_code(err);
                srs_freep(err);
                return srs_api_response_code(w, r, code);
            }
        } else {
            SrsJsonObject *data = SrsJsonAny::object();
            obj->set("vhost", data);
            ;

            if ((err = vhost->dumps(data)) != srs_success) {
                int code = srs_error_code(err);
                srs_freep(err);
                return srs_api_response_code(w, r, code);
            }
        }
    } else {
        return srs_go_http_error(w, SRS_CONSTS_HTTP_MethodNotAllowed);
    }

    return srs_api_response(w, r, obj->dumps());
}

SrsGoApiStreams::SrsGoApiStreams()
{
    stat_ = _srs_stat;
}

SrsGoApiStreams::~SrsGoApiStreams()
{
    stat_ = NULL;
}

srs_error_t SrsGoApiStreams::serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r)
{
    srs_error_t err = srs_success;

    // path: {pattern}{stream_id}
    // e.g. /api/v1/streams/100     pattern= /api/v1/streams/, stream_id=100
    string sid = r->parse_rest_id(entry_->pattern);

    SrsStatisticStream *stream = NULL;
    if (!sid.empty() && (stream = stat_->find_stream(sid)) == NULL) {
        return srs_api_response_code(w, r, ERROR_RTMP_STREAM_NOT_FOUND);
    }

    SrsUniquePtr<SrsJsonObject> obj(SrsJsonAny::object());

    obj->set("code", SrsJsonAny::integer(ERROR_SUCCESS));
    obj->set("server", SrsJsonAny::str(stat_->server_id().c_str()));
    obj->set("service", SrsJsonAny::str(stat_->service_id().c_str()));
    obj->set("pid", SrsJsonAny::str(stat_->service_pid().c_str()));

    if (r->is_http_get()) {
        if (!stream) {
            // Add total count of streams
            int64_t send_bytes = 0, recv_bytes = 0, nstreams = 0, nclients = 0, total_nclients = 0, nerrs = 0;
            if ((err = stat_->dumps_metrics(send_bytes, recv_bytes, nstreams, nclients, total_nclients, nerrs)) != srs_success) {
                int code = srs_error_code(err);
                srs_freep(err);
                return srs_api_response_code(w, r, code);
            }
            obj->set("total", SrsJsonAny::integer(nstreams));

            // Add streams
            SrsJsonArray *data = SrsJsonAny::array();
            obj->set("streams", data);

            std::string rstart = r->query_get("start");
            std::string rcount = r->query_get("count");
            int start = srs_max(0, atoi(rstart.c_str()));
            int count = srs_max(1, atoi(rcount.c_str()));
            if ((err = stat_->dumps_streams(data, start, count)) != srs_success) {
                int code = srs_error_code(err);
                srs_freep(err);
                return srs_api_response_code(w, r, code);
            }
        } else {
            SrsJsonObject *data = SrsJsonAny::object();
            obj->set("stream", data);
            ;

            if ((err = stream->dumps(data)) != srs_success) {
                int code = srs_error_code(err);
                srs_freep(err);
                return srs_api_response_code(w, r, code);
            }
        }
    } else {
        return srs_go_http_error(w, SRS_CONSTS_HTTP_MethodNotAllowed);
    }

    return srs_api_response(w, r, obj->dumps());
}

SrsGoApiClients::SrsGoApiClients()
{
    stat_ = _srs_stat;
}

SrsGoApiClients::~SrsGoApiClients()
{
    stat_ = NULL;
}

srs_error_t SrsGoApiClients::serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r)
{
    srs_error_t err = srs_success;

    // path: {pattern}{client_id}
    // e.g. /api/v1/clients/100     pattern= /api/v1/clients/, client_id=100
    string client_id = r->parse_rest_id(entry_->pattern);

    SrsStatisticClient *client = NULL;
    if (!client_id.empty() && (client = stat_->find_client(client_id)) == NULL) {
        return srs_api_response_code(w, r, ERROR_RTMP_CLIENT_NOT_FOUND);
    }

    SrsUniquePtr<SrsJsonObject> obj(SrsJsonAny::object());

    obj->set("code", SrsJsonAny::integer(ERROR_SUCCESS));
    obj->set("server", SrsJsonAny::str(stat_->server_id().c_str()));
    obj->set("service", SrsJsonAny::str(stat_->service_id().c_str()));
    obj->set("pid", SrsJsonAny::str(stat_->service_pid().c_str()));

    if (r->is_http_get()) {
        if (!client) {
            // Add total count of clients
            int64_t send_bytes = 0, recv_bytes = 0, nstreams = 0, nclients = 0, total_nclients = 0, nerrs = 0;
            if ((err = stat_->dumps_metrics(send_bytes, recv_bytes, nstreams, nclients, total_nclients, nerrs)) != srs_success) {
                int code = srs_error_code(err);
                srs_freep(err);
                return srs_api_response_code(w, r, code);
            }
            obj->set("total", SrsJsonAny::integer(nclients));

            // Add clients
            SrsJsonArray *data = SrsJsonAny::array();
            obj->set("clients", data);

            std::string rstart = r->query_get("start");
            std::string rcount = r->query_get("count");
            int start = srs_max(0, atoi(rstart.c_str()));
            int count = srs_max(1, atoi(rcount.c_str()));
            if ((err = stat_->dumps_clients(data, start, count)) != srs_success) {
                int code = srs_error_code(err);
                srs_freep(err);
                return srs_api_response_code(w, r, code);
            }
        } else {
            SrsJsonObject *data = SrsJsonAny::object();
            obj->set("client", data);

            if ((err = client->dumps(data)) != srs_success) {
                int code = srs_error_code(err);
                srs_freep(err);
                return srs_api_response_code(w, r, code);
            }
        }
    } else if (r->is_http_delete()) {
        if (!client) {
            return srs_api_response_code(w, r, ERROR_RTMP_CLIENT_NOT_FOUND);
        }

        if (client->conn_) {
            client->conn_->expire();
            srs_warn("kickoff client id=%s ok", client_id.c_str());
        } else {
            srs_error("kickoff client id=%s error", client_id.c_str());
            return srs_api_response_code(w, r, SRS_CONSTS_HTTP_BadRequest);
        }
    } else {
        return srs_go_http_error(w, SRS_CONSTS_HTTP_MethodNotAllowed);
    }

    return srs_api_response(w, r, obj->dumps());
}

SrsGoApiRaw::SrsGoApiRaw(ISrsSignalHandler *handler)
{
    handler_ = handler;

    stat_ = _srs_stat;
    config_ = _srs_config;
}

void SrsGoApiRaw::assemble()
{
    raw_api_ = config_->get_raw_api();
    allow_reload_ = config_->get_raw_api_allow_reload();
    allow_query_ = config_->get_raw_api_allow_query();
    allow_update_ = config_->get_raw_api_allow_update();

    config_->subscribe(this);
}

SrsGoApiRaw::~SrsGoApiRaw()
{
    config_->unsubscribe(this);

    stat_ = NULL;
    config_ = NULL;
}

extern srs_error_t _srs_reload_err;
extern SrsReloadState _srs_reload_state;
extern std::string _srs_reload_id;

srs_error_t SrsGoApiRaw::serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r)
{
    srs_error_t err = srs_success;

    std::string rpc = r->query_get("rpc");

    // the object to return for request.
    SrsUniquePtr<SrsJsonObject> obj(SrsJsonAny::object());
    obj->set("code", SrsJsonAny::integer(ERROR_SUCCESS));

    // for rpc=raw, to query the raw api config for http api.
    if (rpc == "raw") {
        // query global scope.
        if ((err = config_->raw_to_json(obj.get())) != srs_success) {
            int code = srs_error_code(err);
            srs_freep(err);
            return srs_api_response_code(w, r, code);
        }

        return srs_api_response(w, r, obj->dumps());
    }

    // whether enabled the HTTP RAW API.
    if (!raw_api_) {
        return srs_api_response_code(w, r, ERROR_SYSTEM_CONFIG_RAW_DISABLED);
    }

    //////////////////////////////////////////////////////////////////////////
    // the rpc is required.
    // the allowed rpc method check.
    if (rpc.empty() || (rpc != "reload" && rpc != "reload-fetch")) {
        return srs_api_response_code(w, r, ERROR_SYSTEM_CONFIG_RAW);
    }

    // for rpc=reload, trigger the server to reload the config.
    if (rpc == "reload") {
        if (!allow_reload_) {
            return srs_api_response_code(w, r, ERROR_SYSTEM_CONFIG_RAW_DISABLED);
        }

        handler_->on_signal(SRS_SIGNAL_RELOAD);
        return srs_api_response_code(w, r, ERROR_SUCCESS);
    } else if (rpc == "reload-fetch") {
        SrsJsonObject *data = SrsJsonAny::object();
        obj->set("data", data);

        data->set("err", SrsJsonAny::integer(srs_error_code(_srs_reload_err)));
        data->set("msg", SrsJsonAny::str(srs_error_summary(_srs_reload_err).c_str()));
        data->set("state", SrsJsonAny::integer(_srs_reload_state));
        data->set("rid", SrsJsonAny::str(_srs_reload_id.c_str()));

        return srs_api_response(w, r, obj->dumps());
    }

    return err;
}

SrsGoApiClusters::SrsGoApiClusters()
{
    stat_ = _srs_stat;
}

SrsGoApiClusters::~SrsGoApiClusters()
{
    stat_ = NULL;
}

srs_error_t SrsGoApiClusters::serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r)
{
    SrsUniquePtr<SrsJsonObject> obj(SrsJsonAny::object());

    obj->set("code", SrsJsonAny::integer(ERROR_SUCCESS));
    SrsJsonObject *data = SrsJsonAny::object();
    obj->set("data", data);

    string ip = r->query_get("ip");
    string vhost = r->query_get("vhost");
    string app = r->query_get("app");
    string stream = r->query_get("stream");
    string coworker = r->query_get("coworker");
    data->set("query", SrsJsonAny::object()
                           ->set("ip", SrsJsonAny::str(ip.c_str()))
                           ->set("vhost", SrsJsonAny::str(vhost.c_str()))
                           ->set("app", SrsJsonAny::str(app.c_str()))
                           ->set("stream", SrsJsonAny::str(stream.c_str())));

    SrsCoWorkers *coworkers = SrsCoWorkers::instance();
    data->set("origin", coworkers->dumps(vhost, coworker, app, stream));

    return srs_api_response(w, r, obj->dumps());
}

SrsGoApiError::SrsGoApiError()
{
    stat_ = _srs_stat;
}

SrsGoApiError::~SrsGoApiError()
{
    stat_ = NULL;
}

srs_error_t SrsGoApiError::serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r)
{
    return srs_api_response_code(w, r, 100);
}

#ifdef SRS_GPERF
#include <gperftools/malloc_extension.h>

SrsGoApiTcmalloc::SrsGoApiTcmalloc()
{
    stat_ = _srs_stat;
}

SrsGoApiTcmalloc::~SrsGoApiTcmalloc()
{
    stat_ = NULL;
}

srs_error_t SrsGoApiTcmalloc::serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r)
{
    srs_error_t err = srs_success;

    string page = r->query_get("page");
    srs_trace("query page=%s", page.c_str());

    if (page == "summary") {
        char buffer[32 * 1024];
        MallocExtension::instance()->GetStats(buffer, sizeof(buffer));

        string data(buffer);
        if ((err = w->write((char *)data.data(), (int)data.length())) != srs_success) {
            return srs_error_wrap(err, "write");
        }

        return err;
    }

    // By default, response the json style response.
    SrsUniquePtr<SrsJsonObject> obj(SrsJsonAny::object());

    obj->set("code", SrsJsonAny::integer(ERROR_SUCCESS));
    SrsJsonObject *data = SrsJsonAny::object();
    obj->set("data", data);

    if (true) {
        SrsJsonObject *p = SrsJsonAny::object();
        data->set("query", p);

        p->set("page", SrsJsonAny::str(page.c_str()));
        p->set("help", SrsJsonAny::str("?page=summary|detail"));
    }

    size_t value = 0;

    // @see https://gperftools.github.io/gperftools/tcmalloc.html
    data->set("release_rate", SrsJsonAny::number(MallocExtension::instance()->GetMemoryReleaseRate()));

    if (true) {
        SrsJsonObject *p = SrsJsonAny::object();
        data->set("generic", p);

        MallocExtension::instance()->GetNumericProperty("generic.current_allocated_bytes", &value);
        p->set("current_allocated_bytes", SrsJsonAny::integer(value));

        MallocExtension::instance()->GetNumericProperty("generic.heap_size", &value);
        p->set("heap_size", SrsJsonAny::integer(value));
    }

    if (true) {
        SrsJsonObject *p = SrsJsonAny::object();
        data->set("tcmalloc", p);

        MallocExtension::instance()->GetNumericProperty("tcmalloc.pageheap_free_bytes", &value);
        p->set("pageheap_free_bytes", SrsJsonAny::integer(value));

        MallocExtension::instance()->GetNumericProperty("tcmalloc.pageheap_unmapped_bytes", &value);
        p->set("pageheap_unmapped_bytes", SrsJsonAny::integer(value));

        MallocExtension::instance()->GetNumericProperty("tcmalloc.slack_bytes", &value);
        p->set("slack_bytes", SrsJsonAny::integer(value));

        MallocExtension::instance()->GetNumericProperty("tcmalloc.max_total_thread_cache_bytes", &value);
        p->set("max_total_thread_cache_bytes", SrsJsonAny::integer(value));

        MallocExtension::instance()->GetNumericProperty("tcmalloc.current_total_thread_cache_bytes", &value);
        p->set("current_total_thread_cache_bytes", SrsJsonAny::integer(value));
    }

    return srs_api_response(w, r, obj->dumps());
}
#endif

#ifdef SRS_VALGRIND
SrsGoApiValgrind::SrsGoApiValgrind()
{
    trd_ = NULL;

    stat_ = _srs_stat;
}

SrsGoApiValgrind::~SrsGoApiValgrind()
{
    srs_freep(trd_);

    stat_ = NULL;
}

srs_error_t SrsGoApiValgrind::serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r)
{
    srs_error_t err = srs_success;

    string check = r->query_get("check");
    srs_trace("query check=%s", check.c_str());

    // Must be full|added|changed|new|quick, set to full for other values.
    if (check != "full" && check != "added" && check != "changed" && check != "new" && check != "quick") {
        srs_warn("force set check=%s to full", check.c_str());
        check = "full";
    }

    // Check if 'new' leak check is supported in current Valgrind version
    if (check == "new") {
#if !defined(VALGRIND_DO_NEW_LEAK_CHECK)
        return srs_error_new(ERROR_NOT_SUPPORTED,
                             "valgrind?check=new requires Valgrind 3.21+, current version is %d.%d",
                             __VALGRIND_MAJOR__, __VALGRIND_MINOR__);
#endif
    }

    if (!trd_) {
        trd_ = new SrsSTCoroutine("valgrind", this, _srs_context->get_id());
        if ((err = trd_->start()) != srs_success) {
            return srs_error_wrap(err, "start");
        }
    }

    // By default, response the json style response.
    SrsUniquePtr<SrsJsonObject> obj(SrsJsonAny::object());

    obj->set("code", SrsJsonAny::integer(ERROR_SUCCESS));

    SrsJsonObject *res = SrsJsonAny::object();
    res->set("check", SrsJsonAny::str(check.c_str()));
    res->set("help", SrsJsonAny::str("?check=full|added|changed|new|quick"));
    res->set("see", SrsJsonAny::str("https://valgrind.org/docs/manual/mc-manual.html"));
    obj->set("data", res);

    // Does a memory check later.
    if (check == "full") {
        res->set("call", SrsJsonAny::str("VALGRIND_DO_LEAK_CHECK"));
    } else if (check == "quick") {
        res->set("call", SrsJsonAny::str("VALGRIND_DO_QUICK_LEAK_CHECK"));
    } else if (check == "added") {
        res->set("call", SrsJsonAny::str("VALGRIND_DO_ADDED_LEAK_CHECK"));
    } else if (check == "changed") {
        res->set("call", SrsJsonAny::str("VALGRIND_DO_CHANGED_LEAK_CHECK"));
    } else if (check == "new") {
        res->set("call", SrsJsonAny::str("VALGRIND_DO_NEW_LEAK_CHECK"));
    }
    task_ = check;

    return srs_api_response(w, r, obj->dumps());
}

srs_error_t SrsGoApiValgrind::cycle()
{
    srs_error_t err = srs_success;

    while (true) {
        if ((err = trd_->pull()) != srs_success) {
            return srs_error_wrap(err, "pull");
        }

        std::string check = task_;
        task_ = "";

        if (!check.empty()) {
            srs_trace("do memory check=%s", check.c_str());
        }

        if (check == "full") {
            VALGRIND_DO_LEAK_CHECK;
        } else if (check == "quick") {
            VALGRIND_DO_QUICK_LEAK_CHECK;
        } else if (check == "added") {
            VALGRIND_DO_ADDED_LEAK_CHECK;
        } else if (check == "changed") {
            VALGRIND_DO_CHANGED_LEAK_CHECK;
        } else if (check == "new") {
            VALGRIND_DO_NEW_LEAK_CHECK;
        }

        srs_usleep(3 * SRS_UTIME_SECONDS);
    }

    return err;
}
#endif

#ifdef SRS_SIGNAL_API
SrsGoApiSignal::SrsGoApiSignal()
{
    stat_ = _srs_stat;
}

SrsGoApiSignal::~SrsGoApiSignal()
{
    stat_ = NULL;
}

srs_error_t SrsGoApiSignal::serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r)
{
    std::string signal = r->query_get("signo");
    srs_trace("query signo=%s", signal.c_str());

    int signo = SIGINT;
    if (signal == "SIGHUP") {
        signo = SRS_SIGNAL_RELOAD;
    } else if (signal == "SIGUSR1") {
        signo = SRS_SIGNAL_REOPEN_LOG;
    } else if (signal == "SIGUSR2") {
        signo = SRS_SIGNAL_UPGRADE;
    } else if (signal == "SIGTERM") {
        signo = SRS_SIGNAL_FAST_QUIT;
    } else if (signal == "SIGQUIT") {
        signo = SRS_SIGNAL_GRACEFULLY_QUIT;
    } else if (signal == "SIGABRT") {
        signo = SRS_SIGNAL_ASSERT_ABORT;
    }

    _srs_server->on_signal(signo);

    // By default, response the json style response.
    SrsUniquePtr<SrsJsonObject> obj(SrsJsonAny::object());

    obj->set("code", SrsJsonAny::integer(ERROR_SUCCESS));

    SrsJsonObject *res = SrsJsonAny::object();
    res->set("signal", SrsJsonAny::str(signal.c_str()));
    res->set("help", SrsJsonAny::str("?signo=SIGHUP|SIGUSR1|SIGUSR2|SIGTERM|SIGQUIT|SIGABRT|SIGINT"));
    res->set("signo", SrsJsonAny::integer(signo));
    obj->set("data", res);

    return srs_api_response(w, r, obj->dumps());
}
#endif

SrsGoApiMetrics::SrsGoApiMetrics()
{
    stat_ = _srs_stat;
    config_ = _srs_config;
}

void SrsGoApiMetrics::assemble()
{
    enabled_ = config_->get_exporter_enabled();
    label_ = config_->get_exporter_label();
    tag_ = config_->get_exporter_tag();
}

SrsGoApiMetrics::~SrsGoApiMetrics()
{
    stat_ = NULL;
    config_ = NULL;
}

srs_error_t SrsGoApiMetrics::serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r)
{
    // whether enabled the HTTP Metrics API.
    if (!enabled_) {
        return srs_api_response_code(w, r, ERROR_EXPORTER_DISABLED);
    }

    /*
     * node_uname gauge
     * build_info gauge
     * cpu gauge
     * memory gauge
     * send_bytes_total counter
     * receive_bytes_total counter
     * streams gauge
     * clients gauge
     * clients_total counter
     * error counter
     */

    std::stringstream ss;

#if defined(__linux__) || defined(SRS_OSX)
    // Get system info
    SrsProtocolUtility utility;
    utsname *system_info = utility.system_uname();
    ss << "# HELP srs_node_uname_info Labeled system information as provided by the uname system call.\n"
       << "# TYPE srs_node_uname_info gauge\n"
       << "srs_node_uname_info{"
       << "sysname=\"" << system_info->sysname << "\","
       << "nodename=\"" << system_info->nodename << "\","
       << "release=\"" << system_info->release << "\","
       << "version=\"" << system_info->version << "\","
       << "machine=\"" << system_info->machine << "\""
       << "} 1\n";
#endif

    // Build info from Config.
    ss << "# HELP srs_build_info A metric with a constant '1' value labeled by build_date, version from which SRS was built.\n"
       << "# TYPE srs_build_info gauge\n"
       << "srs_build_info{"
       << "server=\"" << stat_->server_id() << "\","
       << "service=\"" << stat_->service_id() << "\","
       << "pid=\"" << stat_->service_pid() << "\","
       << "build_date=\"" << SRS_BUILD_DATE << "\","
       << "major=\"" << VERSION_MAJOR << "\","
       << "version=\"" << RTMP_SIG_SRS_VERSION << "\","
       << "code=\"" << RTMP_SIG_SRS_CODE << "\"";
    if (!label_.empty())
        ss << ",label=\"" << label_ << "\"";
    if (!tag_.empty())
        ss << ",tag=\"" << tag_ << "\"";
    ss << "} 1\n";

    // Get ProcSelfStat
    SrsProcSelfStat *u = srs_get_self_proc_stat();

    // The cpu of proc used.
    ss << "# HELP srs_cpu_percent SRS cpu used percent.\n"
       << "# TYPE srs_cpu_percent gauge\n"
       << "srs_cpu_percent "
       << u->percent_ * 100
       << "\n";

    // The memory of proc used.(MBytes)
    int memory = (int)(u->rss_ * 4);
    ss << "# HELP srs_memory SRS memory used.\n"
       << "# TYPE srs_memory gauge\n"
       << "srs_memory "
       << memory
       << "\n";

    // Dump metrics by statistic.
    int64_t send_bytes, recv_bytes, nstreams, nclients, total_nclients, nerrs;
    stat_->dumps_metrics(send_bytes, recv_bytes, nstreams, nclients, total_nclients, nerrs);

    // The total of bytes sent.
    ss << "# HELP srs_send_bytes_total SRS total sent bytes.\n"
       << "# TYPE srs_send_bytes_total counter\n"
       << "srs_send_bytes_total "
       << send_bytes
       << "\n";

    // The total of bytes received.
    ss << "# HELP srs_receive_bytes_total SRS total received bytes.\n"
       << "# TYPE srs_receive_bytes_total counter\n"
       << "srs_receive_bytes_total "
       << recv_bytes
       << "\n";

    // Current number of online streams.
    ss << "# HELP srs_streams The number of SRS concurrent streams.\n"
       << "# TYPE srs_streams gauge\n"
       << "srs_streams "
       << nstreams
       << "\n";

    // Current number of online clients.
    ss << "# HELP srs_clients The number of SRS concurrent clients.\n"
       << "# TYPE srs_clients gauge\n"
       << "srs_clients "
       << nclients
       << "\n";

    // The total of clients connections.
    ss << "# HELP srs_clients_total The total counts of SRS clients.\n"
       << "# TYPE srs_clients_total counter\n"
       << "srs_clients_total "
       << total_nclients
       << "\n";

    // The total of clients errors.
    ss << "# HELP srs_clients_errs_total The total errors of SRS clients.\n"
       << "# TYPE srs_clients_errs_total counter\n"
       << "srs_clients_errs_total "
       << nerrs
       << "\n";

    w->header()->set_content_type("text/plain; charset=utf-8");

    return srs_api_response(w, r, ss.str());
}
