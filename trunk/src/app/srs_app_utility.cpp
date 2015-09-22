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

#include <srs_app_utility.hpp>

#include <sys/types.h>
#include <unistd.h>
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <signal.h>
#include <sys/wait.h>

#ifdef SRS_OSX
#include <sys/sysctl.h>
#endif
#include <stdlib.h>
#include <sys/time.h>
#include <math.h>
#include <map>
using namespace std;

#include <srs_kernel_log.hpp>
#include <srs_app_config.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_kernel_error.hpp>
#include <srs_protocol_kbps.hpp>
#include <srs_protocol_json.hpp>
#include <srs_kernel_buffer.hpp>
#include <srs_rtmp_amf0.hpp>

// the longest time to wait for a process to quit.
#define SRS_PROCESS_QUIT_TIMEOUT_MS 1000

int srs_socket_connect(string server, int port, int64_t timeout, st_netfd_t* pstfd)
{
    int ret = ERROR_SUCCESS;
    
    *pstfd = NULL;
    st_netfd_t stfd = NULL;
    sockaddr_in addr;
    
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if(sock == -1){
        ret = ERROR_SOCKET_CREATE;
        srs_error("create socket error. ret=%d", ret);
        return ret;
    }
    
    srs_assert(!stfd);
    stfd = st_netfd_open_socket(sock);
    if(stfd == NULL){
        ret = ERROR_ST_OPEN_SOCKET;
        srs_error("st_netfd_open_socket failed. ret=%d", ret);
        return ret;
    }
    
    // connect to server.
    std::string ip = srs_dns_resolve(server);
    if (ip.empty()) {
        ret = ERROR_SYSTEM_IP_INVALID;
        srs_error("dns resolve server error, ip empty. ret=%d", ret);
        goto failed;
    }
    
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(ip.c_str());
    
    if (st_connect(stfd, (const struct sockaddr*)&addr, sizeof(sockaddr_in), timeout) == -1){
        ret = ERROR_ST_CONNECT;
        srs_error("connect to server error. ip=%s, port=%d, ret=%d", ip.c_str(), port, ret);
        goto failed;
    }
    srs_info("connect ok. server=%s, ip=%s, port=%d", server.c_str(), ip.c_str(), port);
    
    *pstfd = stfd;
    return ret;
    
failed:
    if (stfd) {
        srs_close_stfd(stfd);
    }
    return ret;
}

int srs_get_log_level(string level)
{
    if ("verbose" == level) {
        return SrsLogLevel::Verbose;
    } else if ("info" == level) {
        return SrsLogLevel::Info;
    } else if ("trace" == level) {
        return SrsLogLevel::Trace;
    } else if ("warn" == level) {
        return SrsLogLevel::Warn;
    } else if ("error" == level) {
        return SrsLogLevel::Error;
    } else {
        return SrsLogLevel::Disabled;
    }
}

string srs_path_build_stream(string template_path, string vhost, string app, string stream)
{
    std::string path = template_path;
    
    // variable [vhost]
    path = srs_string_replace(path, "[vhost]", vhost);
    // variable [app]
    path = srs_string_replace(path, "[app]", app);
    // variable [stream]
    path = srs_string_replace(path, "[stream]", stream);
    
    return path;
}

string srs_path_build_timestamp(string template_path)
{
    std::string path = template_path;
    
    
    // date and time substitude
    // clock time
    timeval tv;
    if (gettimeofday(&tv, NULL) == -1) {
        return path;
    }
    
    // to calendar time
    struct tm* tm;
    if (_srs_config->get_utc_time()) {
        if ((tm = gmtime(&tv.tv_sec)) == NULL) {
            return path;
        }
    } else {
        if ((tm = localtime(&tv.tv_sec)) == NULL) {
            return path;
        }
    }
    
    // the buffer to format the date and time.
    char buf[64];
    
    // [2006], replace with current year.
    if (true) {
        snprintf(buf, sizeof(buf), "%d", 1900 + tm->tm_year);
        path = srs_string_replace(path, "[2006]", buf);
    }
    // [2006], replace with current year.
    if (true) {
        snprintf(buf, sizeof(buf), "%04d", 1900 + tm->tm_year);
        path = srs_string_replace(path, "[2006]", buf);
    }
    // [01], replace this const to current month.
    if (true) {
        snprintf(buf, sizeof(buf), "%02d", 1 + tm->tm_mon);
        path = srs_string_replace(path, "[01]", buf);
    }
    // [02], replace this const to current date.
    if (true) {
        snprintf(buf, sizeof(buf), "%02d", tm->tm_mday);
        path = srs_string_replace(path, "[02]", buf);
    }
    // [15], replace this const to current hour.
    if (true) {
        snprintf(buf, sizeof(buf), "%02d", tm->tm_hour);
        path = srs_string_replace(path, "[15]", buf);
    }
    // [04], repleace this const to current minute.
    if (true) {
        snprintf(buf, sizeof(buf), "%02d", tm->tm_min);
        path = srs_string_replace(path, "[04]", buf);
    }
    // [05], repleace this const to current second.
    if (true) {
        snprintf(buf, sizeof(buf), "%02d", tm->tm_sec);
        path = srs_string_replace(path, "[05]", buf);
    }
    // [999], repleace this const to current millisecond.
    if (true) {
        snprintf(buf, sizeof(buf), "%03d", (int)(tv.tv_usec / 1000));
        path = srs_string_replace(path, "[999]", buf);
    }
    // [timestamp],replace this const to current UNIX timestamp in ms.
    if (true) {
        int64_t now_us = ((int64_t)tv.tv_sec) * 1000 * 1000 + (int64_t)tv.tv_usec;
        snprintf(buf, sizeof(buf), "%"PRId64, now_us / 1000);
        path = srs_string_replace(path, "[timestamp]", buf);
    }
    
    return path;
}

void srs_parse_endpoint(string ip_port, string& ip, string& port)
{
    ip = "0.0.0.0";
    port = ip_port;
    
    size_t pos = string::npos;
    if ((pos = port.find(":")) != string::npos) {
        ip = port.substr(0, pos);
        port = port.substr(pos + 1);
    }
}

void srs_parse_endpoint(string ip_port, string& ip, int& port)
{
    std::string the_port;
    srs_parse_endpoint(ip_port, ip, the_port);
    port = ::atoi(the_port.c_str());
}

int srs_kill_forced(int& pid)
{
    int ret = ERROR_SUCCESS;
    
    if (pid <= 0) {
        return ret;
    }
    
    // first, try kill by SIGTERM.
    if (kill(pid, SIGTERM) < 0) {
        return ERROR_SYSTEM_KILL;
    }
    
    // wait to quit.
    srs_trace("send SIGTERM to pid=%d", pid);
    for (int i = 0; i < SRS_PROCESS_QUIT_TIMEOUT_MS / 10; i++) {
        int status = 0;
        pid_t qpid = -1;
        if ((qpid = waitpid(pid, &status, WNOHANG)) < 0) {
            return ERROR_SYSTEM_KILL;
        }
        
        // 0 is not quit yet.
        if (qpid == 0) {
            st_usleep(10 * 1000);
            continue;
        }
        
        // killed, set pid to -1.
        srs_trace("SIGTERM stop process pid=%d ok.", pid);
        pid = -1;
        
        return ret;
    }

    // then, try kill by SIGKILL.
    if (kill(pid, SIGKILL) < 0) {
        return ERROR_SYSTEM_KILL;
    }
    
    // wait for the process to quit.
    // for example, ffmpeg will gracefully quit if signal is:
    //         1) SIGHUP     2) SIGINT     3) SIGQUIT
    // other signals, directly exit(123), for example:
    //        9) SIGKILL    15) SIGTERM
    int status = 0;
    // @remark when we use SIGKILL to kill process, it must be killed,
    //      so we always wait it to quit by infinite loop.
    while (waitpid(pid, &status, 0) < 0) {
        st_usleep(10 * 1000);
        continue;
    }
    
    srs_trace("SIGKILL stop process pid=%d ok.", pid);
    pid = -1;
    
    return ret;
}

static SrsRusage _srs_system_rusage;

SrsRusage::SrsRusage()
{
    ok = false;
    sample_time = 0;
    memset(&r, 0, sizeof(rusage));
}

SrsRusage* srs_get_system_rusage()
{
    return &_srs_system_rusage;
}

void srs_update_system_rusage()
{
    if (getrusage(RUSAGE_SELF, &_srs_system_rusage.r) < 0) {
        srs_warn("getrusage failed, ignore");
        return;
    }
    
    _srs_system_rusage.sample_time = srs_get_system_time_ms();
    
    _srs_system_rusage.ok = true;
}

static SrsProcSelfStat _srs_system_cpu_self_stat;
static SrsProcSystemStat _srs_system_cpu_system_stat;

SrsProcSelfStat::SrsProcSelfStat()
{
    ok = false;
    sample_time = 0;
    percent = 0;
    
    pid = 0;
    memset(comm, 0, sizeof(comm));
    state = '0';
    ppid = 0;
    pgrp = 0;
    session = 0;
    tty_nr = 0;
    tpgid = 0;
    flags = 0;
    minflt = 0;
    cminflt = 0;
    majflt = 0;
    cmajflt = 0;
    utime = 0;
    stime = 0;
    cutime = 0;
    cstime = 0;
    priority = 0;
    nice = 0;
    num_threads = 0;
    itrealvalue = 0;
    starttime = 0;
    vsize = 0;
    rss = 0;
    rsslim = 0;
    startcode = 0;
    endcode = 0;
    startstack = 0;
    kstkesp = 0;
    kstkeip = 0;
    signal = 0;
    blocked = 0;
    sigignore = 0;
    sigcatch = 0;
    wchan = 0;
    nswap = 0;
    cnswap = 0;
    exit_signal = 0;
    processor = 0;
    rt_priority = 0;
    policy = 0;
    delayacct_blkio_ticks = 0;
    guest_time = 0;
    cguest_time = 0;
}

SrsProcSystemStat::SrsProcSystemStat()
{
    ok = false;
    sample_time = 0;
    percent = 0;
    total_delta = 0;
    user = 0;
    nice = 0;
    sys = 0;
    idle = 0;
    iowait = 0;
    irq = 0;
    softirq = 0;
    steal = 0;
    guest = 0;
}

int64_t SrsProcSystemStat::total()
{
    return user + nice + sys + idle + iowait + irq + softirq + steal + guest;
}

SrsProcSelfStat* srs_get_self_proc_stat()
{
    return &_srs_system_cpu_self_stat;
}

SrsProcSystemStat* srs_get_system_proc_stat()
{
    return &_srs_system_cpu_system_stat;
}

bool get_proc_system_stat(SrsProcSystemStat& r)
{
#ifndef SRS_OSX
    FILE* f = fopen("/proc/stat", "r");
    if (f == NULL) {
        srs_warn("open system cpu stat failed, ignore");
        return false;
    }
    
    static char buf[1024];
    while (fgets(buf, sizeof(buf), f)) {
        if (strncmp(buf, "cpu ", 4) != 0) {
            continue;
        }
        
        // @see: read_stat_cpu() from https://github.com/sysstat/sysstat/blob/master/rd_stats.c#L88
        // @remark, ignore the filed 10 cpu_guest_nice
        sscanf(buf + 5, "%llu %llu %llu %llu %llu %llu %llu %llu %llu\n", 
            &r.user, 
            &r.nice, 
            &r.sys, 
            &r.idle, 
            &r.iowait, 
            &r.irq, 
            &r.softirq, 
            &r.steal, 
            &r.guest);

        break;
    }
    
    fclose(f);
#else
    // TODO: FIXME: impelments it.
    // Fuck all of you who use osx for a long time and never patch the osx features for srs.
#endif

    r.ok = true;
    
    return true;
}

bool get_proc_self_stat(SrsProcSelfStat& r)
{
#ifndef SRS_OSX
    FILE* f = fopen("/proc/self/stat", "r");
    if (f == NULL) {
        srs_warn("open self cpu stat failed, ignore");
        return false;
    }
    
    fscanf(f, "%d %32s %c %d %d %d %d "
        "%d %u %lu %lu %lu %lu "
        "%lu %lu %ld %ld %ld %ld "
        "%ld %ld %llu %lu %ld "
        "%lu %lu %lu %lu %lu "
        "%lu %lu %lu %lu %lu "
        "%lu %lu %lu %d %d "
        "%u %u %llu "
        "%lu %ld", 
        &r.pid, r.comm, &r.state, &r.ppid, &r.pgrp, &r.session, &r.tty_nr,
        &r.tpgid, &r.flags, &r.minflt, &r.cminflt, &r.majflt, &r.cmajflt,
        &r.utime, &r.stime, &r.cutime, &r.cstime, &r.priority, &r.nice,
        &r.num_threads, &r.itrealvalue, &r.starttime, &r.vsize, &r.rss,
        &r.rsslim, &r.startcode, &r.endcode, &r.startstack, &r.kstkesp,
        &r.kstkeip, &r.signal, &r.blocked, &r.sigignore, &r.sigcatch,
        &r.wchan, &r.nswap, &r.cnswap, &r.exit_signal, &r.processor,
        &r.rt_priority, &r.policy, &r.delayacct_blkio_ticks, 
        &r.guest_time, &r.cguest_time);
    
    fclose(f);
#else
    // TODO: FIXME: impelments it.
    // Fuck all of you who use osx for a long time and never patch the osx features for srs.
#endif
    
    r.ok = true;
    
    return true;
}

void srs_update_proc_stat()
{
    // @see: http://stackoverflow.com/questions/7298646/calculating-user-nice-sys-idle-iowait-irq-and-sirq-from-proc-stat/7298711
    // @see https://github.com/simple-rtmp-server/srs/issues/397
    static int user_hz = 0;
    if (user_hz <= 0) {
        user_hz = (int)sysconf(_SC_CLK_TCK);
        srs_trace("USER_HZ=%d", user_hz);
        srs_assert(user_hz > 0);
    }
    
    // system cpu stat
    if (true) {
        SrsProcSystemStat r;
        if (!get_proc_system_stat(r)) {
            return;
        }
        
        r.sample_time = srs_get_system_time_ms();
        
        // calc usage in percent
        SrsProcSystemStat& o = _srs_system_cpu_system_stat;
        
        // @see: http://blog.csdn.net/nineday/article/details/1928847
        // @see: http://stackoverflow.com/questions/16011677/calculating-cpu-usage-using-proc-files
        if (o.total() > 0) {
            r.total_delta = r.total() - o.total();
        }
        if (r.total_delta > 0) {
            int64_t idle = r.idle - o.idle;
            r.percent = (float)(1 - idle / (double)r.total_delta);
        }
        
        // upate cache.
        _srs_system_cpu_system_stat = r;
    }
    
    // self cpu stat
    if (true) {
        SrsProcSelfStat r;
        if (!get_proc_self_stat(r)) {
            return;
        }
        
        r.sample_time = srs_get_system_time_ms();
        
        // calc usage in percent
        SrsProcSelfStat& o = _srs_system_cpu_self_stat;
        
        // @see: http://stackoverflow.com/questions/16011677/calculating-cpu-usage-using-proc-files
        int64_t total = r.sample_time - o.sample_time;
        int64_t usage = (r.utime + r.stime) - (o.utime + o.stime);
        if (total > 0) {
            r.percent = (float)(usage * 1000 / (double)total / user_hz);
        }
        
        // upate cache.
        _srs_system_cpu_self_stat = r;
    }
}

SrsDiskStat::SrsDiskStat()
{
    ok = false;
    sample_time = 0;
    in_KBps = out_KBps = 0;
    busy = 0;
    
    pgpgin = 0;
    pgpgout = 0;
    
    rd_ios = rd_merges = 0;
    rd_sectors = 0;
    rd_ticks = 0;
    
    wr_ios = wr_merges = 0;
    wr_sectors = 0;
    wr_ticks = nb_current = ticks = aveq = 0;
}

static SrsDiskStat _srs_disk_stat;

SrsDiskStat* srs_get_disk_stat()
{
    return &_srs_disk_stat;
}

bool srs_get_disk_vmstat_stat(SrsDiskStat& r)
{
#ifndef SRS_OSX
    FILE* f = fopen("/proc/vmstat", "r");
    if (f == NULL) {
        srs_warn("open vmstat failed, ignore");
        return false;
    }
    
    r.sample_time = srs_get_system_time_ms();
    
    static char buf[1024];
    while (fgets(buf, sizeof(buf), f)) {
        // @see: read_vmstat_paging() from https://github.com/sysstat/sysstat/blob/master/rd_stats.c#L495
        if (strncmp(buf, "pgpgin ", 7) == 0) {
            sscanf(buf + 7, "%lu\n", &r.pgpgin);
        } else if (strncmp(buf, "pgpgout ", 8) == 0) {
            sscanf(buf + 8, "%lu\n", &r.pgpgout);
        }
    }
    
    fclose(f);
#else
    // TODO: FIXME: impelments it.
    // Fuck all of you who use osx for a long time and never patch the osx features for srs.
#endif
    
    r.ok = true;
    
    return true;
}

bool srs_get_disk_diskstats_stat(SrsDiskStat& r)
{
    r.ok = true;
    r.sample_time = srs_get_system_time_ms();
    
    // if disabled, ignore all devices.
    SrsConfDirective* conf = _srs_config->get_stats_disk_device();
    if (conf == NULL) {
        return true;
    }
    
#ifndef SRS_OSX
    FILE* f = fopen("/proc/diskstats", "r");
    if (f == NULL) {
        srs_warn("open vmstat failed, ignore");
        return false;
    }
    
    static char buf[1024];
    while (fgets(buf, sizeof(buf), f)) {
        unsigned int major = 0;
        unsigned int minor = 0;
        static char name[32];
        unsigned int rd_ios = 0;
        unsigned int rd_merges = 0;
        unsigned long long rd_sectors = 0;
        unsigned int rd_ticks = 0;
        unsigned int wr_ios = 0;
        unsigned int wr_merges = 0;
        unsigned long long wr_sectors = 0;
        unsigned int wr_ticks = 0;
        unsigned int nb_current = 0;
        unsigned int ticks = 0;
        unsigned int aveq = 0;
        memset(name, 0, sizeof(name));
        
        sscanf(buf, "%4d %4d %31s %u %u %llu %u %u %u %llu %u %u %u %u", 
            &major, 
            &minor, 
            name, 
            &rd_ios, 
            &rd_merges,
            &rd_sectors, 
            &rd_ticks, 
            &wr_ios, 
            &wr_merges,
            &wr_sectors, 
            &wr_ticks, 
            &nb_current, 
            &ticks, 
            &aveq);

        for (int i = 0; i < (int)conf->args.size(); i++) {
            string name_ok = conf->args.at(i);
            
            if (strcmp(name_ok.c_str(), name) != 0) {
                continue;
            }
            
            r.rd_ios += rd_ios;
            r.rd_merges += rd_merges;
            r.rd_sectors += rd_sectors;
            r.rd_ticks += rd_ticks;
            r.wr_ios += wr_ios;
            r.wr_merges += wr_merges;
            r.wr_sectors += wr_sectors;
            r.wr_ticks += wr_ticks;
            r.nb_current += nb_current;
            r.ticks += ticks;
            r.aveq += aveq;
            
            break;
        }
    }
    
    fclose(f);
#else
    // TODO: FIXME: impelments it.
    // Fuck all of you who use osx for a long time and never patch the osx features for srs.
#endif
    
    r.ok = true;
    
    return true;
}

void srs_update_disk_stat()
{
    SrsDiskStat r;
    if (!srs_get_disk_vmstat_stat(r)) {
        return;
    }
    if (!srs_get_disk_diskstats_stat(r)) {
        return;
    }
    if (!get_proc_system_stat(r.cpu)) {
        return;
    }
    
    SrsDiskStat& o = _srs_disk_stat;
    if (!o.ok) {
        _srs_disk_stat = r;
        return;
    }
    
    // vmstat
    if (true) {
        int64_t duration_ms = r.sample_time - o.sample_time;
        
        if (o.pgpgin > 0 && r.pgpgin > o.pgpgin && duration_ms > 0) {
            // KBps = KB * 1000 / ms = KB/s
            r.in_KBps = (int)((r.pgpgin - o.pgpgin) * 1000 / duration_ms);
        }
        
        if (o.pgpgout > 0 && r.pgpgout > o.pgpgout && duration_ms > 0) {
            // KBps = KB * 1000 / ms = KB/s
            r.out_KBps = (int)((r.pgpgout - o.pgpgout) * 1000 / duration_ms);
        }
    }
    
    // diskstats
    if (r.cpu.ok && o.cpu.ok) {
        SrsCpuInfo* cpuinfo = srs_get_cpuinfo();
        r.cpu.total_delta = r.cpu.total() - o.cpu.total();
        
        if (r.cpu.ok && r.cpu.total_delta > 0
            && cpuinfo->ok && cpuinfo->nb_processors > 0
            && o.ticks < r.ticks
        ) {
            // @see: write_ext_stat() from https://github.com/sysstat/sysstat/blob/master/iostat.c#L979
            // TODO: FIXME: the USER_HZ assert to 100, so the total_delta ticks *10 is ms.
            double delta_ms = r.cpu.total_delta * 10 / cpuinfo->nb_processors;
            unsigned int ticks = r.ticks - o.ticks;
            
            // busy in [0, 1], where 0.1532 means 15.32%
            r.busy = (float)(ticks / delta_ms);
        }
    }
    
    _srs_disk_stat = r;
}

SrsMemInfo::SrsMemInfo()
{
    ok = false;
    sample_time = 0;
    
    percent_ram = 0;
    percent_swap = 0;
    
    MemActive = 0;
    RealInUse = 0;
    NotInUse = 0;
    MemTotal = 0;
    MemFree = 0;
    Buffers = 0;
    Cached = 0;
    SwapTotal = 0;
    SwapFree = 0;
}

static SrsMemInfo _srs_system_meminfo;

SrsMemInfo* srs_get_meminfo()
{
    return &_srs_system_meminfo;
}

void srs_update_meminfo()
{
    SrsMemInfo& r = _srs_system_meminfo;
    
#ifndef SRS_OSX
    FILE* f = fopen("/proc/meminfo", "r");
    if (f == NULL) {
        srs_warn("open meminfo failed, ignore");
        return;
    }
    
    static char buf[1024];
    while (fgets(buf, sizeof(buf), f)) {
        // @see: read_meminfo() from https://github.com/sysstat/sysstat/blob/master/rd_stats.c#L227
        if (strncmp(buf, "MemTotal:", 9) == 0) {
            sscanf(buf + 9, "%lu", &r.MemTotal);
        } else if (strncmp(buf, "MemFree:", 8) == 0) {
            sscanf(buf + 8, "%lu", &r.MemFree);
        } else if (strncmp(buf, "Buffers:", 8) == 0) {
            sscanf(buf + 8, "%lu", &r.Buffers);
        } else if (strncmp(buf, "Cached:", 7) == 0) {
            sscanf(buf + 7, "%lu", &r.Cached);
        } else if (strncmp(buf, "SwapTotal:", 10) == 0) {
            sscanf(buf + 10, "%lu", &r.SwapTotal);
        } else if (strncmp(buf, "SwapFree:", 9) == 0) {
            sscanf(buf + 9, "%lu", &r.SwapFree);
        }
    }
    
    fclose(f);
#else
    // TODO: FIXME: impelments it.
    // Fuck all of you who use osx for a long time and never patch the osx features for srs.
#endif
    
    r.sample_time = srs_get_system_time_ms();
    r.MemActive = r.MemTotal - r.MemFree;
    r.RealInUse = r.MemActive - r.Buffers - r.Cached;
    r.NotInUse = r.MemTotal - r.RealInUse;
    
    if (r.MemTotal > 0) {
        r.percent_ram = (float)(r.RealInUse / (double)r.MemTotal);
    }
    if (r.SwapTotal > 0) {
        r.percent_swap = (float)((r.SwapTotal - r.SwapFree) / (double)r.SwapTotal);
    }
    
    r.ok = true;
}

SrsCpuInfo::SrsCpuInfo()
{
    ok = false;
    
    nb_processors = 0;
    nb_processors_online = 0;
}

SrsCpuInfo* srs_get_cpuinfo()
{
    static SrsCpuInfo* cpu = NULL;
    if (cpu != NULL) {
        return cpu;
    }
    
    // initialize cpu info.
    cpu = new SrsCpuInfo();
    cpu->ok = true;
    cpu->nb_processors = (int)sysconf(_SC_NPROCESSORS_CONF);
    cpu->nb_processors_online = (int)sysconf(_SC_NPROCESSORS_ONLN);
    
    return cpu;
}

SrsPlatformInfo::SrsPlatformInfo()
{
    ok = false;
    
    srs_startup_time = 0;
    
    os_uptime = 0;
    os_ilde_time = 0;
    
    load_one_minutes = 0;
    load_five_minutes = 0;
    load_fifteen_minutes = 0;
}

static SrsPlatformInfo _srs_system_platform_info;

SrsPlatformInfo* srs_get_platform_info()
{
    return &_srs_system_platform_info;
}

void srs_update_platform_info()
{
    SrsPlatformInfo& r = _srs_system_platform_info;
    
    r.srs_startup_time = srs_get_system_startup_time_ms();
    
#ifndef SRS_OSX
    if (true) {
        FILE* f = fopen("/proc/uptime", "r");
        if (f == NULL) {
            srs_warn("open uptime failed, ignore");
            return;
        }
        
        fscanf(f, "%lf %lf\n", &r.os_uptime, &r.os_ilde_time);
    
        fclose(f);
    }
    
    if (true) {
        FILE* f = fopen("/proc/loadavg", "r");
        if (f == NULL) {
            srs_warn("open loadavg failed, ignore");
            return;
        }
        
        // @see: read_loadavg() from https://github.com/sysstat/sysstat/blob/master/rd_stats.c#L402
        // @remark, we use our algorithm, not sysstat.
        fscanf(f, "%lf %lf %lf\n", 
            &r.load_one_minutes, 
            &r.load_five_minutes, 
            &r.load_fifteen_minutes);
    
        fclose(f);
    }
#else
    // man 3 sysctl
    if (true) {
        struct timeval tv;
        size_t len = sizeof(timeval);
        
        int mib[2];
        mib[0] = CTL_KERN;
        mib[1] = KERN_BOOTTIME;
        if (sysctl(mib, 2, &tv, &len, NULL, 0) < 0) {
            srs_warn("sysctl boottime failed, ignore");
            return;
        }
        
        time_t bsec = tv.tv_sec;
        time_t csec = ::time(NULL);
        r.os_uptime = difftime(csec, bsec);
    }
    
    // man 3 sysctl
    if (true) {
        struct loadavg la;
        size_t len = sizeof(loadavg);
        
        int mib[2];
        mib[0] = CTL_VM;
        mib[1] = VM_LOADAVG;
        if (sysctl(mib, 2, &la, &len, NULL, 0) < 0) {
            srs_warn("sysctl loadavg failed, ignore");
            return;
        }
        
        r.load_one_minutes = (double)la.ldavg[0] / la.fscale;
        r.load_five_minutes = (double)la.ldavg[1] / la.fscale;
        r.load_fifteen_minutes = (double)la.ldavg[2] / la.fscale;
    }
#endif
    
    r.ok = true;
}

SrsNetworkDevices::SrsNetworkDevices()
{
    ok = false;
    
    memset(name, 0, sizeof(name));
    sample_time = 0;
    
    rbytes = 0;
    rpackets = 0;
    rerrs = 0;
    rdrop = 0;
    rfifo = 0;
    rframe = 0;
    rcompressed = 0;
    rmulticast = 0;
    
    sbytes = 0;
    spackets = 0;
    serrs = 0;
    sdrop = 0;
    sfifo = 0;
    scolls = 0;
    scarrier = 0;
    scompressed = 0;
}

#define MAX_NETWORK_DEVICES_COUNT 16
static SrsNetworkDevices _srs_system_network_devices[MAX_NETWORK_DEVICES_COUNT];
static int _nb_srs_system_network_devices = -1;

SrsNetworkDevices* srs_get_network_devices()
{
    return _srs_system_network_devices;
}

int srs_get_network_devices_count()
{
    return _nb_srs_system_network_devices;
}

void srs_update_network_devices()
{
#ifndef SRS_OSX
    if (true) {
        FILE* f = fopen("/proc/net/dev", "r");
        if (f == NULL) {
            srs_warn("open proc network devices failed, ignore");
            return;
        }
        
        // ignore title.
        static char buf[1024];
        fgets(buf, sizeof(buf), f);
        fgets(buf, sizeof(buf), f);
    
        for (int i = 0; i < MAX_NETWORK_DEVICES_COUNT; i++) {
            if (!fgets(buf, sizeof(buf), f)) {
                break;
            }
            
            SrsNetworkDevices& r = _srs_system_network_devices[i];
    
            // @see: read_net_dev() from https://github.com/sysstat/sysstat/blob/master/rd_stats.c#L786
            // @remark, we use our algorithm, not sysstat.
            char fname[7];
            sscanf(buf, "%6[^:]:%llu %lu %lu %lu %lu %lu %lu %lu %llu %lu %lu %lu %lu %lu %lu %lu\n",
                fname, &r.rbytes, &r.rpackets, &r.rerrs, &r.rdrop, &r.rfifo, &r.rframe, &r.rcompressed, &r.rmulticast,
                &r.sbytes, &r.spackets, &r.serrs, &r.sdrop, &r.sfifo, &r.scolls, &r.scarrier, &r.scompressed);
                
            sscanf(fname, "%s", r.name);
            _nb_srs_system_network_devices = i + 1;
            srs_info("scan network device ifname=%s, total=%d", r.name, _nb_srs_system_network_devices);
            
            r.sample_time = srs_get_system_time_ms();
            r.ok = true;
        }
    
        fclose(f);
    }
#else
    // TODO: FIXME: impelments it.
    // Fuck all of you who use osx for a long time and never patch the osx features for srs.
#endif
}

// we detect all network device as internet or intranet device, by its ip address.
//      key is device name, for instance, eth0
//      value is whether internet, for instance, true.
static std::map<std::string, bool> _srs_device_ifs;

bool srs_net_device_is_internet(string ifname)
{
    srs_info("check ifname=%s", ifname.c_str());

    if (_srs_device_ifs.find(ifname) == _srs_device_ifs.end()) {
        return false;
    }
    return _srs_device_ifs[ifname];
}

bool srs_net_device_is_internet(in_addr_t addr)
{
    u_int32_t addr_h = ntohl(addr);
    
    // lo, 127.0.0.0-127.0.0.1
    if (addr_h >= 0x7f000000 && addr_h <= 0x7f000001) {
        return false;
    }
    
    // Class A 10.0.0.0-10.255.255.255
    if (addr_h >= 0x0a000000 && addr_h <= 0x0affffff) {
        return false;
    }
    
    // Class B 172.16.0.0-172.31.255.255
    if (addr_h >= 0xac100000 && addr_h <= 0xac1fffff) {
        return false;
    }
    
    // Class C 192.168.0.0-192.168.255.255
    if (addr_h >= 0xc0a80000 && addr_h <= 0xc0a8ffff) {
        return false;
    }
    
    return true;
}

SrsNetworkRtmpServer::SrsNetworkRtmpServer()
{
    ok = false;
    sample_time = rbytes = sbytes = 0;
    nb_conn_sys = nb_conn_srs = 0;
    nb_conn_sys_et = nb_conn_sys_tw = 0;
    nb_conn_sys_udp = 0;
}

static SrsNetworkRtmpServer _srs_network_rtmp_server;

SrsNetworkRtmpServer* srs_get_network_rtmp_server()
{
    return &_srs_network_rtmp_server;
}

// @see: http://stackoverflow.com/questions/5992211/list-of-possible-internal-socket-statuses-from-proc
enum {
    SYS_TCP_ESTABLISHED =      0x01,
    SYS_TCP_SYN_SENT,       // 0x02
    SYS_TCP_SYN_RECV,       // 0x03
    SYS_TCP_FIN_WAIT1,      // 0x04
    SYS_TCP_FIN_WAIT2,      // 0x05
    SYS_TCP_TIME_WAIT,      // 0x06
    SYS_TCP_CLOSE,          // 0x07
    SYS_TCP_CLOSE_WAIT,     // 0x08
    SYS_TCP_LAST_ACK,       // 0x09
    SYS_TCP_LISTEN,         // 0x0A
    SYS_TCP_CLOSING,        // 0x0B /* Now a valid state */

    SYS_TCP_MAX_STATES      // 0x0C /* Leave at the end! */
};

void srs_update_rtmp_server(int nb_conn, SrsKbps* kbps)
{
    SrsNetworkRtmpServer& r = _srs_network_rtmp_server;
        
    int nb_socks = 0;
    int nb_tcp4_hashed = 0;
    int nb_tcp_orphans = 0;
    int nb_tcp_tws = 0;
    int nb_tcp_total = 0;
    int nb_tcp_mem = 0;
    int nb_udp4 = 0;
    
#ifndef SRS_OSX
    if (true) {
        FILE* f = fopen("/proc/net/sockstat", "r");
        if (f == NULL) {
            srs_warn("open proc network sockstat failed, ignore");
            return;
        }
        
        // ignore title.
        static char buf[1024];
        fgets(buf, sizeof(buf), f);
        
        while (fgets(buf, sizeof(buf), f)) {
            // @see: et_sockstat_line() from https://github.com/shemminger/iproute2/blob/master/misc/ss.c
            if (strncmp(buf, "sockets: used ", 14) == 0) {
                sscanf(buf + 14, "%d\n", &nb_socks);
            } else if (strncmp(buf, "TCP: ", 5) == 0) {
                sscanf(buf + 5, "%*s %d %*s %d %*s %d %*s %d %*s %d\n", 
                    &nb_tcp4_hashed, 
                    &nb_tcp_orphans, 
                    &nb_tcp_tws, 
                    &nb_tcp_total, 
                    &nb_tcp_mem);
            } else if (strncmp(buf, "UDP: ", 5) == 0) {
                sscanf(buf + 5, "%*s %d\n", &nb_udp4);
            }
        }
    
        fclose(f);
    }
#else
    // TODO: FIXME: impelments it.
    // Fuck all of you who use osx for a long time and never patch the osx features for srs.
    nb_socks = 0;
    nb_tcp4_hashed = 0;
    nb_tcp_orphans = 0;
    nb_tcp_tws = 0;
    nb_tcp_total = 0;
    nb_tcp_mem = 0;
    nb_udp4 = 0;
#endif
    
    int nb_tcp_estab = 0;
    
#ifndef SRS_OSX
    if (true) {
        FILE* f = fopen("/proc/net/snmp", "r");
        if (f == NULL) {
            srs_warn("open proc network snmp failed, ignore");
            return;
        }
        
        // ignore title.
        static char buf[1024];
        fgets(buf, sizeof(buf), f);
        
        // @see: https://github.com/shemminger/iproute2/blob/master/misc/ss.c
        while (fgets(buf, sizeof(buf), f)) {
            // @see: get_snmp_int("Tcp:", "CurrEstab", &sn.tcp_estab)
            // tcp stat title
            if (strncmp(buf, "Tcp: ", 5) == 0) {
                // read tcp stat data
                if (!fgets(buf, sizeof(buf), f)) {
                    break;
                }
                // parse tcp stat data
                if (strncmp(buf, "Tcp: ", 5) == 0) {
                    sscanf(buf + 5, "%*d %*d %*d %*d %*d %*d %*d %*d %d\n", &nb_tcp_estab);
                }
            }
        }
    
        fclose(f);
    }
#else
    // TODO: FIXME: impelments it.
    // Fuck all of you who use osx for a long time and never patch the osx features for srs.
#endif
    
    // @see: https://github.com/shemminger/iproute2/blob/master/misc/ss.c
    // TODO: FIXME: ignore the slabstat, @see: get_slabstat()
    if (true) {
        // @see: print_summary()
        r.nb_conn_sys = nb_tcp_total + nb_tcp_tws;
        r.nb_conn_sys_et = nb_tcp_estab;
        r.nb_conn_sys_tw = nb_tcp_tws;
        r.nb_conn_sys_udp = nb_udp4;
    }
    
    if (true) {
        r.ok = true;
        
        r.nb_conn_srs = nb_conn;
        r.sample_time = srs_get_system_time_ms();
        
        r.rbytes = kbps->get_recv_bytes();
        r.rkbps = kbps->get_recv_kbps();
        r.rkbps_30s = kbps->get_recv_kbps_30s();
        r.rkbps_5m = kbps->get_recv_kbps_5m();
        
        r.sbytes = kbps->get_send_bytes();
        r.skbps = kbps->get_send_kbps();
        r.skbps_30s = kbps->get_send_kbps_30s();
        r.skbps_5m = kbps->get_send_kbps_5m();
    }
}

vector<string> _srs_system_ipv4_ips;

void retrieve_local_ipv4_ips()
{
    vector<string>& ips = _srs_system_ipv4_ips;
    
    ips.clear();
    
    ifaddrs* ifap;
    if (getifaddrs(&ifap) == -1) {
        srs_warn("retrieve local ips, ini ifaddrs failed.");
        return;
    }
    
    ifaddrs* p = ifap;
    while (p != NULL) {
        ifaddrs* cur = p;
        sockaddr* addr = cur->ifa_addr;
        p = p->ifa_next;
        
        // retrieve ipv4 addr
        // ignore the tun0 network device, 
        // which addr is NULL.
        // @see: https://github.com/simple-rtmp-server/srs/issues/141
        if (addr && addr->sa_family == AF_INET) {
            in_addr* inaddr = &((sockaddr_in*)addr)->sin_addr;
            
            char buf[16];
            memset(buf, 0, sizeof(buf));
            
            if ((inet_ntop(addr->sa_family, inaddr, buf, sizeof(buf))) == NULL) {
                srs_warn("convert local ip failed");
                break;
            }
            
            std::string ip = buf;
            if (ip != SRS_CONSTS_LOCALHOST) {
                srs_trace("retrieve local ipv4 ip=%s, index=%d", ip.c_str(), (int)ips.size());
                ips.push_back(ip);
            }
            
            // set the device internet status.
            if (!srs_net_device_is_internet(inaddr->s_addr)) {
                srs_trace("detect intranet address: %s, ifname=%s", ip.c_str(), cur->ifa_name);
                _srs_device_ifs[cur->ifa_name] = false;
            } else {
                srs_trace("detect internet address: %s, ifname=%s", ip.c_str(), cur->ifa_name);
                _srs_device_ifs[cur->ifa_name] = true;
            }
        }
    }

    freeifaddrs(ifap);
}

vector<string>& srs_get_local_ipv4_ips()
{
    if (_srs_system_ipv4_ips.empty()) {
        retrieve_local_ipv4_ips();
    }

    return _srs_system_ipv4_ips;
}

string srs_get_local_ip(int fd)
{
    std::string ip;

    // discovery client information
    sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);
    if (getsockname(fd, (sockaddr*)&addr, &addrlen) == -1) {
        return ip;
    }
    srs_verbose("get local ip success.");

    // ip v4 or v6
    char buf[INET6_ADDRSTRLEN];
    memset(buf, 0, sizeof(buf));

    if ((inet_ntop(addr.sin_family, &addr.sin_addr, buf, sizeof(buf))) == NULL) {
        return ip;
    }

    ip = buf;

    srs_verbose("get local ip of client ip=%s, fd=%d", buf, fd);

    return ip;
}

int srs_get_local_port(int fd)
{
    // discovery client information
    sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);
    if (getsockname(fd, (sockaddr*)&addr, &addrlen) == -1) {
        return 0;
    }
    srs_verbose("get local ip success.");
    
    int port = ntohs(addr.sin_port);

    srs_verbose("get local ip of client port=%s, fd=%d", port, fd);

    return port;
}

string srs_get_peer_ip(int fd)
{
    std::string ip;
    
    // discovery client information
    sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);
    if (getpeername(fd, (sockaddr*)&addr, &addrlen) == -1) {
        return ip;
    }
    srs_verbose("get peer name success.");

    // ip v4 or v6
    char buf[INET6_ADDRSTRLEN];
    memset(buf, 0, sizeof(buf));
    
    if ((inet_ntop(addr.sin_family, &addr.sin_addr, buf, sizeof(buf))) == NULL) {
        return ip;
    }
    srs_verbose("get peer ip of client ip=%s, fd=%d", buf, fd);
    
    ip = buf;
    
    srs_verbose("get peer ip success. ip=%s, fd=%d", ip.c_str(), fd);
    
    return ip;
}

bool srs_is_digit_number(const string& str)
{
    if (str.empty()) {
        return false;
    }
    
    int v = ::atoi(str.c_str());
    int powv = (int)pow(10, str.length() - 1);
    return  v / powv >= 1 && v / powv <= 9;
}

bool srs_is_boolean(const string& str)
{
    return str == "true" || str == "false";
}

void srs_api_dump_summaries(SrsJsonObject* obj)
{
    SrsRusage* r = srs_get_system_rusage();
    SrsProcSelfStat* u = srs_get_self_proc_stat();
    SrsProcSystemStat* s = srs_get_system_proc_stat();
    SrsCpuInfo* c = srs_get_cpuinfo();
    SrsMemInfo* m = srs_get_meminfo();
    SrsPlatformInfo* p = srs_get_platform_info();
    SrsNetworkDevices* n = srs_get_network_devices();
    SrsNetworkRtmpServer* nrs = srs_get_network_rtmp_server();
    SrsDiskStat* d = srs_get_disk_stat();
    
    float self_mem_percent = 0;
    if (m->MemTotal > 0) {
        self_mem_percent = (float)(r->r.ru_maxrss / (double)m->MemTotal);
    }
    
    int64_t now = srs_get_system_time_ms();
    double srs_uptime = (now - p->srs_startup_time) / 100 / 10.0;
    
    int64_t n_sample_time = 0;
    int64_t nr_bytes = 0;
    int64_t ns_bytes = 0;
    int64_t nri_bytes = 0;
    int64_t nsi_bytes = 0;
    int nb_n = srs_get_network_devices_count();
    for (int i = 0; i < nb_n; i++) {
        SrsNetworkDevices& o = n[i];
        
        // ignore the lo interface.
        std::string inter = o.name;
        if (!o.ok) {
            continue;
        }
        
        // update the sample time.
        n_sample_time = o.sample_time;
        
        // stat the intranet bytes.
        if (inter == "lo" || !srs_net_device_is_internet(inter)) {
            nri_bytes += o.rbytes;
            nsi_bytes += o.sbytes;
            continue;
        }
        
        nr_bytes += o.rbytes;
        ns_bytes += o.sbytes;
    }
    
    // all data is ok?
    bool ok = (r->ok && u->ok && s->ok && c->ok 
        && d->ok && m->ok && p->ok && nrs->ok);
    
    SrsJsonObject* data = SrsJsonAny::object();
    obj->set("data", data);
    
    data->set("ok", SrsJsonAny::boolean(ok));
    data->set("now_ms", SrsJsonAny::integer(now));
    
    // self
    SrsJsonObject* self = SrsJsonAny::object();
    data->set("self", self);
    
    self->set("version", SrsJsonAny::str(RTMP_SIG_SRS_VERSION));
    self->set("pid", SrsJsonAny::integer(getpid()));
    self->set("ppid", SrsJsonAny::integer(u->ppid));
    self->set("argv", SrsJsonAny::str(_srs_config->argv().c_str()));
    self->set("cwd", SrsJsonAny::str(_srs_config->cwd().c_str()));
    self->set("mem_kbyte", SrsJsonAny::integer(r->r.ru_maxrss));
    self->set("mem_percent", SrsJsonAny::number(self_mem_percent));
    self->set("cpu_percent", SrsJsonAny::number(u->percent));
    self->set("srs_uptime", SrsJsonAny::integer(srs_uptime));
    
    // system
    SrsJsonObject* sys = SrsJsonAny::object();
    data->set("system", sys);
    
    sys->set("cpu_percent", SrsJsonAny::number(s->percent));
    sys->set("disk_read_KBps", SrsJsonAny::integer(d->in_KBps));
    sys->set("disk_write_KBps", SrsJsonAny::integer(d->out_KBps));
    sys->set("disk_busy_percent", SrsJsonAny::number(d->busy));
    sys->set("mem_ram_kbyte", SrsJsonAny::integer(m->MemTotal));
    sys->set("mem_ram_percent", SrsJsonAny::number(m->percent_ram));
    sys->set("mem_swap_kbyte", SrsJsonAny::integer(m->SwapTotal));
    sys->set("mem_swap_percent", SrsJsonAny::number(m->percent_swap));
    sys->set("cpus", SrsJsonAny::integer(c->nb_processors));
    sys->set("cpus_online", SrsJsonAny::integer(c->nb_processors_online));
    sys->set("uptime", SrsJsonAny::number(p->os_uptime));
    sys->set("ilde_time", SrsJsonAny::number(p->os_ilde_time));
    sys->set("load_1m", SrsJsonAny::number(p->load_one_minutes));
    sys->set("load_5m", SrsJsonAny::number(p->load_five_minutes));
    sys->set("load_15m", SrsJsonAny::number(p->load_fifteen_minutes));
    // system network bytes stat.
    sys->set("net_sample_time", SrsJsonAny::integer(n_sample_time));
    // internet public address network device bytes.
    sys->set("net_recv_bytes", SrsJsonAny::integer(nr_bytes));
    sys->set("net_send_bytes", SrsJsonAny::integer(ns_bytes));
    // intranet private address network device bytes.
    sys->set("net_recvi_bytes", SrsJsonAny::integer(nri_bytes));
    sys->set("net_sendi_bytes", SrsJsonAny::integer(nsi_bytes));
    // srs network bytes stat.
    sys->set("srs_sample_time", SrsJsonAny::integer(nrs->sample_time));
    sys->set("srs_recv_bytes", SrsJsonAny::integer(nrs->rbytes));
    sys->set("srs_send_bytes", SrsJsonAny::integer(nrs->sbytes));
    sys->set("conn_sys", SrsJsonAny::integer(nrs->nb_conn_sys));
    sys->set("conn_sys_et", SrsJsonAny::integer(nrs->nb_conn_sys_et));
    sys->set("conn_sys_tw", SrsJsonAny::integer(nrs->nb_conn_sys_tw));
    sys->set("conn_sys_udp", SrsJsonAny::integer(nrs->nb_conn_sys_udp));
    sys->set("conn_srs", SrsJsonAny::integer(nrs->nb_conn_srs));
}

string srs_join_vector_string(vector<string>& vs, string separator)
{
    string str = "";

    for (int i = 0; i < (int)vs.size(); i++) {
        str += vs.at(i);
        if (i != (int)vs.size() - 1) {
            str += separator;
        }
    }

    return str;
}

