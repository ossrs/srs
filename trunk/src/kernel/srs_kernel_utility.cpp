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
    
    _srs_system_rusage.ok = true;
}

static SrsCpuSelfStat _srs_system_cpu_self_stat;
static SrsCpuSystemStat _srs_system_cpu_system_stat;

SrsCpuSelfStat::SrsCpuSelfStat()
{
    ok = false;
}

SrsCpuSystemStat::SrsCpuSystemStat()
{
    ok = false;
}

SrsCpuSelfStat* srs_get_self_cpu_stat()
{
    return &_srs_system_cpu_self_stat;
}

SrsCpuSystemStat* srs_get_system_cpu_stat()
{
    return &_srs_system_cpu_system_stat;
}

void srs_update_system_cpu_stat()
{
    // system cpu stat
    if (true) {
        FILE* f = fopen("/proc/stat", "r");
        if (f == NULL) {
            srs_warn("open system cpu stat failed, ignore");
            return;
        }
        
        SrsCpuSystemStat& r = _srs_system_cpu_system_stat;
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
    }
    
    // self cpu stat
    if (true) {
        FILE* f = fopen("/proc/self/stat", "r");
        if (f == NULL) {
            srs_warn("open self cpu stat failed, ignore");
            return;
        }
        
        SrsCpuSelfStat& r = _srs_system_cpu_self_stat;
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
    }
}
