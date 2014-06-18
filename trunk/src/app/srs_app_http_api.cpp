/*
The MIT License (MIT)

Copyright (c) 2013-2014 winlin

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
#include <srs_app_http.hpp>
#include <srs_app_socket.hpp>
#include <srs_core_autofree.hpp>
#include <srs_app_json.hpp>
#include <srs_app_config.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_app_utility.hpp>

SrsApiRoot::SrsApiRoot()
{
    handlers.push_back(new SrsApiApi());
}

SrsApiRoot::~SrsApiRoot()
{
}

bool SrsApiRoot::is_handler_valid(SrsHttpMessage* req, int& status_code, std::string& reason_phrase) 
{
    if (!SrsHttpHandler::is_handler_valid(req, status_code, reason_phrase)) {
        return false;
    }
    
    if (req->match()->matched_url.length() != 1) {
        status_code = HTTP_NotFound;
        reason_phrase = HTTP_NotFound_str;
        return false;
    }
    
    return true;
}

bool SrsApiRoot::can_handle(const char* path, int length, const char** pchild)
{
    // reset the child path to path,
    // for child to reparse the path.
    *pchild = path;
    
    // only compare the first char.
    return srs_path_equals("/", path, 1);
}

int SrsApiRoot::do_process_request(SrsSocket* skt, SrsHttpMessage* req)
{
    std::stringstream ss;
    
    ss << JOBJECT_START
        << JFIELD_ERROR(ERROR_SUCCESS) << JFIELD_CONT
        << JFIELD_ORG("urls", JOBJECT_START)
            << JFIELD_STR("api", "the api root")
        << JOBJECT_END
        << JOBJECT_END;
    
    return res_json(skt, req, ss.str());
}

SrsApiApi::SrsApiApi()
{
    handlers.push_back(new SrsApiV1());
}

SrsApiApi::~SrsApiApi()
{
}

bool SrsApiApi::can_handle(const char* path, int length, const char** /*pchild*/)
{
    return srs_path_equals("/api", path, length);
}

int SrsApiApi::do_process_request(SrsSocket* skt, SrsHttpMessage* req)
{
    std::stringstream ss;
    
    ss << JOBJECT_START
        << JFIELD_ERROR(ERROR_SUCCESS) << JFIELD_CONT
        << JFIELD_ORG("urls", JOBJECT_START)
            << JFIELD_STR("v1", "the api version 1.0")
        << JOBJECT_END
        << JOBJECT_END;
    
    return res_json(skt, req, ss.str());
}

SrsApiV1::SrsApiV1()
{
    handlers.push_back(new SrsApiVersion());
    handlers.push_back(new SrsApiSummaries());
    handlers.push_back(new SrsApiRusages());
    handlers.push_back(new SrsApiSelfProcStats());
    handlers.push_back(new SrsApiSystemProcStats());
    handlers.push_back(new SrsApiMemInfos());
    handlers.push_back(new SrsApiAuthors());
    handlers.push_back(new SrsApiConfigs());
    handlers.push_back(new SrsApiRequests());
}

SrsApiV1::~SrsApiV1()
{
}

bool SrsApiV1::can_handle(const char* path, int length, const char** /*pchild*/)
{
    return srs_path_equals("/v1", path, length);
}

int SrsApiV1::do_process_request(SrsSocket* skt, SrsHttpMessage* req)
{
    std::stringstream ss;
    
    ss << JOBJECT_START
        << JFIELD_ERROR(ERROR_SUCCESS) << JFIELD_CONT
        << JFIELD_ORG("urls", JOBJECT_START)
            << JFIELD_STR("versions", "the version of SRS") << JFIELD_CONT
            << JFIELD_STR("summaries", "the summary(pid, argv, pwd, cpu, mem) of SRS") << JFIELD_CONT
            << JFIELD_STR("rusages", "the rusage of SRS") << JFIELD_CONT
            << JFIELD_STR("self_proc_stats", "the self process stats") << JFIELD_CONT
            << JFIELD_STR("system_proc_stats", "the system process stats") << JFIELD_CONT
            << JFIELD_STR("meminfos", "the meminfo of system") << JFIELD_CONT
            << JFIELD_STR("configs", "to query or modify the config of srs") << JFIELD_CONT
            << JFIELD_STR("authors", "the primary authors and contributors") << JFIELD_CONT
            << JFIELD_STR("requests", "the request itself, for http debug")
        << JOBJECT_END
        << JOBJECT_END;
    
    return res_json(skt, req, ss.str());
}

SrsApiRequests::SrsApiRequests()
{
}

SrsApiRequests::~SrsApiRequests()
{
}

bool SrsApiRequests::can_handle(const char* path, int length, const char** /*pchild*/)
{
    return srs_path_equals("/requests", path, length);
}

int SrsApiRequests::do_process_request(SrsSocket* skt, SrsHttpMessage* req)
{
    std::stringstream ss;
    
    ss << JOBJECT_START
        << JFIELD_ERROR(ERROR_SUCCESS) << JFIELD_CONT
        << JFIELD_ORG("data", JOBJECT_START)
            << JFIELD_STR("uri", req->uri()) << JFIELD_CONT
            << JFIELD_STR("path", req->path()) << JFIELD_CONT;
    
    // method
    if (req->is_http_get()) {
        ss  << JFIELD_STR("METHOD", "GET");
    } else if (req->is_http_post()) {
        ss  << JFIELD_STR("METHOD", "POST");
    } else if (req->is_http_put()) {
        ss  << JFIELD_STR("METHOD", "PUT");
    } else if (req->is_http_delete()) {
        ss  << JFIELD_STR("METHOD", "DELETE");
    } else {
        ss  << JFIELD_ORG("METHOD", req->method());
    }
    ss << JFIELD_CONT;
    
    // request headers
    ss      << JFIELD_NAME("headers") << JOBJECT_START;
    for (int i = 0; i < req->request_header_count(); i++) {
        std::string key = req->request_header_key_at(i);
        std::string value = req->request_header_value_at(i);
        if ( i < req->request_header_count() - 1) {
            ss      << JFIELD_STR(key, value) << JFIELD_CONT;
        } else {
            ss      << JFIELD_STR(key, value);
        }
    }
    ss      << JOBJECT_END << JFIELD_CONT;
    
    // server informations
    ss      << JFIELD_NAME("server") << JOBJECT_START
                << JFIELD_STR("sigature", RTMP_SIG_SRS_KEY) << JFIELD_CONT
                << JFIELD_STR("name", RTMP_SIG_SRS_NAME) << JFIELD_CONT
                << JFIELD_STR("version", RTMP_SIG_SRS_VERSION) << JFIELD_CONT
                << JFIELD_STR("link", RTMP_SIG_SRS_URL) << JFIELD_CONT
                << JFIELD_ORG("time", srs_get_system_time_ms())
            << JOBJECT_END
        << JOBJECT_END
        << JOBJECT_END;
    
    return res_json(skt, req, ss.str());
}

SrsApiConfigs::SrsApiConfigs()
{
    handlers.push_back(new SrsApiConfigsLogs());
}

SrsApiConfigs::~SrsApiConfigs()
{
}

bool SrsApiConfigs::can_handle(const char* path, int length, const char** /*pchild*/)
{
    return srs_path_equals("/configs", path, length);
}

int SrsApiConfigs::do_process_request(SrsSocket* skt, SrsHttpMessage* req)
{
    std::stringstream ss;
    
    ss << JOBJECT_START
        << JFIELD_ERROR(ERROR_SUCCESS) << JFIELD_CONT
        << JFIELD_ORG("urls", JOBJECT_START)
            << JFIELD_NAME("logs") << JOBJECT_START
                << JFIELD_STR("uri", req->uri()+"/logs") << JFIELD_CONT
                << JFIELD_STR("desc", "system log settings") << JFIELD_CONT
                << JFIELD_STR("GET", "query logs tank/level/file") << JFIELD_CONT
                << JFIELD_STR("PUT", "update logs tank/level/file")
            << JOBJECT_END
        << JOBJECT_END
        << JOBJECT_END;
    
    return res_json(skt, req, ss.str());
}

SrsApiConfigsLogs::SrsApiConfigsLogs()
{
}

SrsApiConfigsLogs::~SrsApiConfigsLogs()
{
}

bool SrsApiConfigsLogs::can_handle(const char* path, int length, const char** /*pchild*/)
{
    return srs_path_equals("/logs", path, length);
}

bool SrsApiConfigsLogs::is_handler_valid(SrsHttpMessage* req, int& status_code, string& reason_phrase) 
{
    if (!req->is_http_get() && !req->is_http_put()) {
        status_code = HTTP_MethodNotAllowed;
        reason_phrase = HTTP_MethodNotAllowed_str;
        
        return false;
    }
    
    return SrsHttpHandler::is_handler_valid(req, status_code, reason_phrase);
}

int SrsApiConfigsLogs::do_process_request(SrsSocket* skt, SrsHttpMessage* req)
{
    int ret = ERROR_SUCCESS;
    
    // HTTP GET
    if (req->is_http_get()) {
        std::stringstream ss;
        ss << JOBJECT_START
            << JFIELD_ERROR(ERROR_SUCCESS) << JFIELD_CONT
            << JFIELD_ORG("data", JOBJECT_START)
                << JFIELD_STR("tank", (_srs_config->get_log_tank_file()? "file":"console")) << JFIELD_CONT
                << JFIELD_STR("level", _srs_config->get_log_level()) << JFIELD_CONT
                << JFIELD_STR("cwd", _srs_config->cwd()) << JFIELD_CONT
                << JFIELD_STR("file", _srs_config->get_log_file())
            << JOBJECT_END
            << JOBJECT_END;
        
        return res_json(skt, req, ss.str());
    }
    
    // HTTP PUT
    srs_trace("http api PUT logs, req is: %s", req->body().c_str());
    
    SrsJsonAny* json = SrsJsonAny::loads(req->body_raw());
    SrsAutoFree(SrsJsonAny, json);
    
    if (!json) {
        return response_error(skt, req, ERROR_HTTP_API_LOGS, "invalid PUT json");
    } else if (!json->is_object()) {
        return response_error(skt, req, ERROR_HTTP_API_LOGS, "invalid PUT json logs params");
    }
    
    SrsJsonObject* o = json->to_object();
    SrsJsonAny* prop = NULL;
    if ((prop = o->ensure_property_string("file")) != NULL && _srs_config->set_log_file(prop->to_str())) {
        if ((ret = _srs_config->force_reload_log_file()) != ERROR_SUCCESS) {
            return response_error(skt, req, ret, "reload log file failed");
        }
        srs_warn("http api reload log file to %s", prop->to_str().c_str());
    }
    if ((prop = o->ensure_property_string("tank")) != NULL && _srs_config->set_log_tank(prop->to_str())) {
        if ((ret = _srs_config->force_reload_log_tank()) != ERROR_SUCCESS) {
            return response_error(skt, req, ret, "reload log tank failed");
        }
        srs_warn("http api reload log tank to %s", prop->to_str().c_str());
    }
    if ((prop = o->ensure_property_string("level")) != NULL && _srs_config->set_log_level(prop->to_str())) {
        if ((ret = _srs_config->force_reload_log_level()) != ERROR_SUCCESS) {
            return response_error(skt, req, ret, "reload log level failed");
        }
        srs_warn("http api reload log level to %s", prop->to_str().c_str());
    }
    
    return response_error(skt, req, ret, "PUT logs success.");
}

SrsApiVersion::SrsApiVersion()
{
}

SrsApiVersion::~SrsApiVersion()
{
}

bool SrsApiVersion::can_handle(const char* path, int length, const char** /*pchild*/)
{
    return srs_path_equals("/versions", path, length);
}

int SrsApiVersion::do_process_request(SrsSocket* skt, SrsHttpMessage* req)
{
    std::stringstream ss;
    
    ss << JOBJECT_START
        << JFIELD_ERROR(ERROR_SUCCESS) << JFIELD_CONT
        << JFIELD_ORG("data", JOBJECT_START)
            << JFIELD_ORG("major", VERSION_MAJOR) << JFIELD_CONT
            << JFIELD_ORG("minor", VERSION_MINOR) << JFIELD_CONT
            << JFIELD_ORG("revision", VERSION_REVISION) << JFIELD_CONT
            << JFIELD_STR("version", RTMP_SIG_SRS_VERSION)
        << JOBJECT_END
        << JOBJECT_END;
    
    return res_json(skt, req, ss.str());
}

SrsApiSummaries::SrsApiSummaries()
{
}

SrsApiSummaries::~SrsApiSummaries()
{
}

bool SrsApiSummaries::can_handle(const char* path, int length, const char** /*pchild*/)
{
    return srs_path_equals("/summaries", path, length);
}

int SrsApiSummaries::do_process_request(SrsSocket* skt, SrsHttpMessage* req)
{
    std::stringstream ss;
    
    SrsRusage* r = srs_get_system_rusage();
    SrsProcSelfStat* u = srs_get_self_proc_stat();
    SrsProcSystemStat* s = srs_get_system_proc_stat();
    SrsCpuInfo* c = srs_get_cpuinfo();
    SrsMemInfo* m = srs_get_meminfo();
    SrsPlatformInfo* p = srs_get_platform_info();
    SrsNetworkDevices* n = srs_get_network_devices();
    
    float self_mem_percent = 0;
    if (m->MemTotal > 0) {
        self_mem_percent = (float)(r->r.ru_maxrss / (double)m->MemTotal);
    }
    
    int64_t now = srs_get_system_time_ms();
    double srs_uptime = (now - p->srs_startup_time) / 100 / 10.0;
    
    bool n_ok = false;
    int64_t n_sample_time = 0;
    int64_t nr_bytes = 0;
    int64_t ns_bytes = 0;
    int nb_n = srs_get_network_devices_count();
    for (int i = 0; i < nb_n; i++) {
        SrsNetworkDevices& o = n[i];
        if (o.ok) {
            n_ok = true;
            nr_bytes += o.rbytes;
            ns_bytes += o.sbytes;
            n_sample_time = o.sample_time;
        }
    }
    
    ss << JOBJECT_START
        << JFIELD_ERROR(ERROR_SUCCESS) << JFIELD_CONT
        << JFIELD_ORG("data", JOBJECT_START)
            << JFIELD_ORG("rusage_ok", (r->ok? "true":"false")) << JFIELD_CONT
            << JFIELD_ORG("self_cpu_stat_ok", (u->ok? "true":"false")) << JFIELD_CONT
            << JFIELD_ORG("system_cpu_stat_ok", (s->ok? "true":"false")) << JFIELD_CONT
            << JFIELD_ORG("cpuinfo_ok", (c->ok? "true":"false")) << JFIELD_CONT
            << JFIELD_ORG("meminfo_ok", (m->ok? "true":"false")) << JFIELD_CONT
            << JFIELD_ORG("platform_ok", (p->ok? "true":"false")) << JFIELD_CONT
            << JFIELD_ORG("network_ok", (n_ok? "true":"false")) << JFIELD_CONT
            << JFIELD_ORG("now_ms", now) << JFIELD_CONT
            << JFIELD_ORG("self", JOBJECT_START)
                << JFIELD_ORG("pid", getpid()) << JFIELD_CONT
                << JFIELD_ORG("ppid", u->ppid) << JFIELD_CONT
                << JFIELD_STR("argv", _srs_config->argv()) << JFIELD_CONT
                << JFIELD_STR("cwd", _srs_config->cwd()) << JFIELD_CONT
                << JFIELD_ORG("mem_kbyte", r->r.ru_maxrss) << JFIELD_CONT
                << JFIELD_ORG("mem_percent", self_mem_percent) << JFIELD_CONT
                << JFIELD_ORG("cpu_percent", u->percent) << JFIELD_CONT
                << JFIELD_ORG("srs_uptime", srs_uptime)
            << JOBJECT_END << JFIELD_CONT
            << JFIELD_ORG("system", JOBJECT_START)
                << JFIELD_ORG("cpu_percent", s->percent) << JFIELD_CONT
                << JFIELD_ORG("mem_ram_kbyte", m->MemTotal) << JFIELD_CONT
                << JFIELD_ORG("mem_ram_percent", m->percent_ram) << JFIELD_CONT
                << JFIELD_ORG("mem_swap_kbyte", m->SwapTotal) << JFIELD_CONT
                << JFIELD_ORG("mem_swap_percent", m->percent_swap) << JFIELD_CONT
                << JFIELD_ORG("cpus", c->nb_processors) << JFIELD_CONT
                << JFIELD_ORG("cpus_online", c->nb_processors_online) << JFIELD_CONT
                << JFIELD_ORG("uptime", p->os_uptime) << JFIELD_CONT
                << JFIELD_ORG("ilde_time", p->os_ilde_time) << JFIELD_CONT
                << JFIELD_ORG("load_1m", p->load_one_minutes) << JFIELD_CONT
                << JFIELD_ORG("load_5m", p->load_five_minutes) << JFIELD_CONT
                << JFIELD_ORG("load_15m", p->load_fifteen_minutes) << JFIELD_CONT
                << JFIELD_ORG("net_sample_time", n_sample_time) << JFIELD_CONT
                << JFIELD_ORG("net_recv_bytes", nr_bytes) << JFIELD_CONT
                << JFIELD_ORG("net_send_bytes", ns_bytes)
            << JOBJECT_END
        << JOBJECT_END
        << JOBJECT_END;
    
    return res_json(skt, req, ss.str());
}

SrsApiRusages::SrsApiRusages()
{
}

SrsApiRusages::~SrsApiRusages()
{
}

bool SrsApiRusages::can_handle(const char* path, int length, const char** /*pchild*/)
{
    return srs_path_equals("/rusages", path, length);
}

int SrsApiRusages::do_process_request(SrsSocket* skt, SrsHttpMessage* req)
{
    std::stringstream ss;
    
    SrsRusage* r = srs_get_system_rusage();
    
    ss << JOBJECT_START
        << JFIELD_ERROR(ERROR_SUCCESS) << JFIELD_CONT
        << JFIELD_ORG("data", JOBJECT_START)
            << JFIELD_ORG("ok", (r->ok? "true":"false")) << JFIELD_CONT
            << JFIELD_ORG("sample_time", r->sample_time) << JFIELD_CONT
            << JFIELD_ORG("ru_utime", r->r.ru_utime.tv_sec) << JFIELD_CONT
            << JFIELD_ORG("ru_stime", r->r.ru_stime.tv_sec) << JFIELD_CONT
            << JFIELD_ORG("ru_maxrss", r->r.ru_maxrss) << JFIELD_CONT
            << JFIELD_ORG("ru_ixrss", r->r.ru_ixrss) << JFIELD_CONT
            << JFIELD_ORG("ru_idrss", r->r.ru_idrss) << JFIELD_CONT
            << JFIELD_ORG("ru_isrss", r->r.ru_isrss) << JFIELD_CONT
            << JFIELD_ORG("ru_minflt", r->r.ru_minflt) << JFIELD_CONT
            << JFIELD_ORG("ru_majflt", r->r.ru_majflt) << JFIELD_CONT
            << JFIELD_ORG("ru_nswap", r->r.ru_nswap) << JFIELD_CONT
            << JFIELD_ORG("ru_inblock", r->r.ru_inblock) << JFIELD_CONT
            << JFIELD_ORG("ru_oublock", r->r.ru_oublock) << JFIELD_CONT
            << JFIELD_ORG("ru_msgsnd", r->r.ru_msgsnd) << JFIELD_CONT
            << JFIELD_ORG("ru_msgrcv", r->r.ru_msgrcv) << JFIELD_CONT
            << JFIELD_ORG("ru_nsignals", r->r.ru_nsignals) << JFIELD_CONT
            << JFIELD_ORG("ru_nvcsw", r->r.ru_nvcsw) << JFIELD_CONT
            << JFIELD_ORG("ru_nivcsw", r->r.ru_nivcsw)
        << JOBJECT_END
        << JOBJECT_END;
    
    return res_json(skt, req, ss.str());
}

SrsApiSelfProcStats::SrsApiSelfProcStats()
{
}

SrsApiSelfProcStats::~SrsApiSelfProcStats()
{
}

bool SrsApiSelfProcStats::can_handle(const char* path, int length, const char** /*pchild*/)
{
    return srs_path_equals("/self_proc_stats", path, length);
}

int SrsApiSelfProcStats::do_process_request(SrsSocket* skt, SrsHttpMessage* req)
{
    std::stringstream ss;
    
    SrsProcSelfStat* u = srs_get_self_proc_stat();
    
    ss << JOBJECT_START
        << JFIELD_ERROR(ERROR_SUCCESS) << JFIELD_CONT
        << JFIELD_ORG("data", JOBJECT_START)
            << JFIELD_ORG("ok", (u->ok? "true":"false")) << JFIELD_CONT
            << JFIELD_ORG("sample_time", u->sample_time) << JFIELD_CONT
            << JFIELD_ORG("percent", u->percent) << JFIELD_CONT
            << JFIELD_ORG("pid", u->pid) << JFIELD_CONT
            << JFIELD_STR("comm", u->comm) << JFIELD_CONT
            << JFIELD_STR("state", u->state) << JFIELD_CONT
            << JFIELD_ORG("ppid", u->ppid) << JFIELD_CONT
            << JFIELD_ORG("pgrp", u->pgrp) << JFIELD_CONT
            << JFIELD_ORG("session", u->session) << JFIELD_CONT
            << JFIELD_ORG("tty_nr", u->tty_nr) << JFIELD_CONT
            << JFIELD_ORG("tpgid", u->tpgid) << JFIELD_CONT
            << JFIELD_ORG("flags", u->flags) << JFIELD_CONT
            << JFIELD_ORG("minflt", u->minflt) << JFIELD_CONT
            << JFIELD_ORG("cminflt", u->cminflt) << JFIELD_CONT
            << JFIELD_ORG("majflt", u->majflt) << JFIELD_CONT
            << JFIELD_ORG("cmajflt", u->cmajflt) << JFIELD_CONT
            << JFIELD_ORG("utime", u->utime) << JFIELD_CONT
            << JFIELD_ORG("stime", u->stime) << JFIELD_CONT
            << JFIELD_ORG("cutime", u->cutime) << JFIELD_CONT
            << JFIELD_ORG("cstime", u->cstime) << JFIELD_CONT
            << JFIELD_ORG("priority", u->priority) << JFIELD_CONT
            << JFIELD_ORG("nice", u->nice) << JFIELD_CONT
            << JFIELD_ORG("num_threads", u->num_threads) << JFIELD_CONT
            << JFIELD_ORG("itrealvalue", u->itrealvalue) << JFIELD_CONT
            << JFIELD_ORG("starttime", u->starttime) << JFIELD_CONT
            << JFIELD_ORG("vsize", u->vsize) << JFIELD_CONT
            << JFIELD_ORG("rss", u->rss) << JFIELD_CONT
            << JFIELD_ORG("rsslim", u->rsslim) << JFIELD_CONT
            << JFIELD_ORG("startcode", u->startcode) << JFIELD_CONT
            << JFIELD_ORG("endcode", u->endcode) << JFIELD_CONT
            << JFIELD_ORG("startstack", u->startstack) << JFIELD_CONT
            << JFIELD_ORG("kstkesp", u->kstkesp) << JFIELD_CONT
            << JFIELD_ORG("kstkeip", u->kstkeip) << JFIELD_CONT
            << JFIELD_ORG("signal", u->signal) << JFIELD_CONT
            << JFIELD_ORG("blocked", u->blocked) << JFIELD_CONT
            << JFIELD_ORG("sigignore", u->sigignore) << JFIELD_CONT
            << JFIELD_ORG("sigcatch", u->sigcatch) << JFIELD_CONT
            << JFIELD_ORG("wchan", u->wchan) << JFIELD_CONT
            << JFIELD_ORG("nswap", u->nswap) << JFIELD_CONT
            << JFIELD_ORG("cnswap", u->cnswap) << JFIELD_CONT
            << JFIELD_ORG("exit_signal", u->exit_signal) << JFIELD_CONT
            << JFIELD_ORG("processor", u->processor) << JFIELD_CONT
            << JFIELD_ORG("rt_priority", u->rt_priority) << JFIELD_CONT
            << JFIELD_ORG("policy", u->policy) << JFIELD_CONT
            << JFIELD_ORG("delayacct_blkio_ticks", u->delayacct_blkio_ticks) << JFIELD_CONT
            << JFIELD_ORG("guest_time", u->guest_time) << JFIELD_CONT
            << JFIELD_ORG("cguest_time", u->cguest_time)
        << JOBJECT_END
        << JOBJECT_END;
    
    return res_json(skt, req, ss.str());
}

SrsApiSystemProcStats::SrsApiSystemProcStats()
{
}

SrsApiSystemProcStats::~SrsApiSystemProcStats()
{
}

bool SrsApiSystemProcStats::can_handle(const char* path, int length, const char** /*pchild*/)
{
    return srs_path_equals("/system_proc_stats", path, length);
}

int SrsApiSystemProcStats::do_process_request(SrsSocket* skt, SrsHttpMessage* req)
{
    std::stringstream ss;
    
    SrsProcSystemStat* s = srs_get_system_proc_stat();
    
    ss << JOBJECT_START
        << JFIELD_ERROR(ERROR_SUCCESS) << JFIELD_CONT
        << JFIELD_ORG("data", JOBJECT_START)
            << JFIELD_ORG("ok", (s->ok? "true":"false")) << JFIELD_CONT
            << JFIELD_ORG("sample_time", s->sample_time) << JFIELD_CONT
            << JFIELD_ORG("percent", s->percent) << JFIELD_CONT
            << JFIELD_ORG("user", s->user) << JFIELD_CONT
            << JFIELD_ORG("nice", s->nice) << JFIELD_CONT
            << JFIELD_ORG("sys", s->sys) << JFIELD_CONT
            << JFIELD_ORG("idle", s->idle) << JFIELD_CONT
            << JFIELD_ORG("iowait", s->iowait) << JFIELD_CONT
            << JFIELD_ORG("irq", s->irq) << JFIELD_CONT
            << JFIELD_ORG("softirq", s->softirq) << JFIELD_CONT
            << JFIELD_ORG("steal", s->steal) << JFIELD_CONT
            << JFIELD_ORG("guest", s->guest)
        << JOBJECT_END
        << JOBJECT_END;
    
    return res_json(skt, req, ss.str());
}

SrsApiMemInfos::SrsApiMemInfos()
{
}

SrsApiMemInfos::~SrsApiMemInfos()
{
}

bool SrsApiMemInfos::can_handle(const char* path, int length, const char** /*pchild*/)
{
    return srs_path_equals("/meminfos", path, length);
}

int SrsApiMemInfos::do_process_request(SrsSocket* skt, SrsHttpMessage* req)
{
    std::stringstream ss;
    
    SrsMemInfo* m = srs_get_meminfo();
    
    ss << JOBJECT_START
        << JFIELD_ERROR(ERROR_SUCCESS) << JFIELD_CONT
        << JFIELD_ORG("data", JOBJECT_START)
            << JFIELD_ORG("ok", (m->ok? "true":"false")) << JFIELD_CONT
            << JFIELD_ORG("sample_time", m->sample_time) << JFIELD_CONT
            << JFIELD_ORG("percent_ram", m->percent_ram) << JFIELD_CONT
            << JFIELD_ORG("percent_swap", m->percent_swap) << JFIELD_CONT
            << JFIELD_ORG("MemActive", m->MemActive) << JFIELD_CONT
            << JFIELD_ORG("RealInUse", m->RealInUse) << JFIELD_CONT
            << JFIELD_ORG("NotInUse", m->NotInUse) << JFIELD_CONT
            << JFIELD_ORG("MemTotal", m->MemTotal) << JFIELD_CONT
            << JFIELD_ORG("MemFree", m->MemFree) << JFIELD_CONT
            << JFIELD_ORG("Buffers", m->Buffers) << JFIELD_CONT
            << JFIELD_ORG("Cached", m->Cached) << JFIELD_CONT
            << JFIELD_ORG("SwapTotal", m->SwapTotal) << JFIELD_CONT
            << JFIELD_ORG("SwapFree", m->SwapFree)
        << JOBJECT_END
        << JOBJECT_END;
    
    return res_json(skt, req, ss.str());
}

SrsApiAuthors::SrsApiAuthors()
{
}

SrsApiAuthors::~SrsApiAuthors()
{
}

bool SrsApiAuthors::can_handle(const char* path, int length, const char** /*pchild*/)
{
    return srs_path_equals("/authors", path, length);
}

int SrsApiAuthors::do_process_request(SrsSocket* skt, SrsHttpMessage* req)
{
    std::stringstream ss;
    
    ss << JOBJECT_START
        << JFIELD_ERROR(ERROR_SUCCESS) << JFIELD_CONT
        << JFIELD_ORG("data", JOBJECT_START)
            << JFIELD_STR("primary_authors", RTMP_SIG_SRS_PRIMARY_AUTHROS) << JFIELD_CONT
            << JFIELD_STR("contributors_link", RTMP_SIG_SRS_CONTRIBUTORS_URL) << JFIELD_CONT
            << JFIELD_STR("contributors", SRS_AUTO_CONSTRIBUTORS)
        << JOBJECT_END
        << JOBJECT_END;
    
    return res_json(skt, req, ss.str());
}

SrsHttpApi::SrsHttpApi(SrsServer* srs_server, st_netfd_t client_stfd, SrsHttpHandler* _handler) 
    : SrsConnection(srs_server, client_stfd)
{
    parser = new SrsHttpParser();
    handler = _handler;
    requires_crossdomain = false;
}

SrsHttpApi::~SrsHttpApi()
{
    srs_freep(parser);
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
    SrsSocket skt(stfd);
    
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
        if ((ret = process_request(&skt, req)) != ERROR_SUCCESS) {
            return ret;
        }
    }
        
    return ret;
}

int SrsHttpApi::process_request(SrsSocket* skt, SrsHttpMessage* req) 
{
    int ret = ERROR_SUCCESS;

    // parse uri to schema/server:port/path?query
    if ((ret = req->parse_uri()) != ERROR_SUCCESS) {
        return ret;
    }
    
    srs_trace("HTTP %s %s, content-length=%"PRId64"", 
        req->method_str().c_str(), req->url().c_str(), req->content_length());
    
    // TODO: maybe need to parse the url.
    std::string url = req->path();
    
    SrsHttpHandlerMatch* p = NULL;
    if ((ret = handler->best_match(url.data(), url.length(), &p)) != ERROR_SUCCESS) {
        srs_warn("failed to find the best match handler for url. ret=%d", ret);
        return ret;
    }
    
    // if success, p and pstart should be valid.
    srs_assert(p);
    srs_assert(p->handler);
    srs_assert(p->matched_url.length() <= url.length());
    srs_info("best match handler, matched_url=%s", p->matched_url.c_str());
    
    req->set_match(p);
    req->set_requires_crossdomain(requires_crossdomain);
    
    // use handler to process request.
    if ((ret = p->handler->process_request(skt, req)) != ERROR_SUCCESS) {
        srs_warn("handler failed to process http request. ret=%d", ret);
        return ret;
    }
    
    if (req->requires_crossdomain()) {
        requires_crossdomain = true;
    }
    
    return ret;
}

#endif
