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

#include <srs_kernel_utility.hpp>

#include <sys/time.h>

#include <srs_kernel_log.hpp>

static int64_t _srs_system_time_us_cache = 0;

int64_t srs_get_system_time_ms()
{
    if (_srs_system_time_us_cache <= 0) {
        srs_update_system_time_ms();
    }
    
    return _srs_system_time_us_cache / 1000;
}

void srs_update_system_time_ms()
{
    timeval now;
    
    if (gettimeofday(&now, NULL) < 0) {
        srs_warn("gettimeofday failed, ignore");
        return;
    }

    // @see: https://github.com/winlinvip/simple-rtmp-server/issues/35
    // we must convert the tv_sec/tv_usec to int64_t.
    int64_t now_us = ((int64_t)now.tv_sec) * 1000 * 1000 + (int64_t)now.tv_usec;
    if (now_us < _srs_system_time_us_cache) {
        srs_warn("system time negative, "
            "history=%"PRId64"us, now=%"PRId64"", _srs_system_time_us_cache, now_us);
    }
    
    _srs_system_time_us_cache = now_us;
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
        
    srs_update_system_time_ms();
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
    state = 0;
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
    memset(label, 0, sizeof(label));
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
    FILE* f = fopen("/proc/stat", "r");
    if (f == NULL) {
        srs_warn("open system cpu stat failed, ignore");
        return false;
    }
    
    for (;;) {
        int ret = fscanf(f, "%4s %lu %lu %lu %lu %lu "
            "%lu %lu %lu %lu\n", 
            r.label, &r.user, &r.nice, &r.sys, &r.idle, &r.iowait,
            &r.irq, &r.softirq, &r.steal, &r.guest);
        r.ok = false;
        
        if (ret == EOF) {
            break;
        }
        
        if (strcmp("cpu", r.label) == 0) {
            r.ok = true;
            break;
        }
    }
    
    fclose(f);
    
    return r.ok;
}

bool get_proc_self_stat(SrsProcSelfStat& r)
{
    FILE* f = fopen("/proc/self/stat", "r");
    if (f == NULL) {
        srs_warn("open self cpu stat failed, ignore");
        return false;
    }
    
    int ret = fscanf(f, "%d %32s %c %d %d %d %d "
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
        
    if (ret >= 0) {
        r.ok = true;
    }
    
    fclose(f);
    
    return r.ok;
}

void srs_update_proc_stat()
{
    srs_update_system_time_ms();
    
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
        int64_t total = (r.user + r.nice + r.sys + r.idle + r.iowait + r.irq + r.softirq + r.steal + r.guest) 
            - (o.user + o.nice + o.sys + o.idle + o.iowait + o.irq + o.softirq + o.steal + o.guest);
        int64_t idle = r.idle - o.idle;
        if (total > 0) {
            r.percent = (float)(1 - idle / (double)total);
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
        
        srs_update_system_time_ms();
        r.sample_time = srs_get_system_time_ms();
        
        // calc usage in percent
        SrsProcSelfStat& o = _srs_system_cpu_self_stat;
        
        // @see: http://stackoverflow.com/questions/16011677/calculating-cpu-usage-using-proc-files
        int64_t total = r.sample_time - o.sample_time;
        int64_t usage = (r.utime + r.stime) - (o.utime + o.stime);
        if (total > 0) {
            r.percent = (float)(usage * 1000 / (double)total / 100);
        }
        
        // upate cache.
        _srs_system_cpu_self_stat = r;
    }
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
    cpu->nb_processors = sysconf(_SC_NPROCESSORS_CONF);
    cpu->nb_processors_online = sysconf(_SC_NPROCESSORS_ONLN);
    
    return cpu;
}
