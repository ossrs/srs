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

#include <srs_app_utility.hpp>

#include <sys/types.h>
#include <unistd.h>
#include <ifaddrs.h>
#include <arpa/inet.h>

using namespace std;

#include <srs_kernel_log.hpp>
#include <srs_app_config.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_kernel_error.hpp>

#define SRS_LOCAL_LOOP_IP "127.0.0.1"

int srs_get_log_level(std::string level)
{
    if ("verbose" == _srs_config->get_log_level()) {
        return SrsLogLevel::Verbose;
    } else if ("info" == _srs_config->get_log_level()) {
        return SrsLogLevel::Info;
    } else if ("trace" == _srs_config->get_log_level()) {
        return SrsLogLevel::Trace;
    } else if ("warn" == _srs_config->get_log_level()) {
        return SrsLogLevel::Warn;
    } else if ("error" == _srs_config->get_log_level()) {
        return SrsLogLevel::Error;
    } else {
        return SrsLogLevel::Trace;
    }
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
    
    fclose(f);
        
    if (ret >= 0) {
        r.ok = true;
    }
    
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
    FILE* f = fopen("/proc/meminfo", "r");
    if (f == NULL) {
        srs_warn("open meminfo failed, ignore");
        return;
    }
    
    SrsMemInfo& r = _srs_system_meminfo;
    r.ok = false;
    
    for (;;) {
        static char label[64];
        static unsigned long value;
        static char postfix[64];
        int ret = fscanf(f, "%64s %lu %64s\n", label, &value, postfix);
        
        if (ret == EOF) {
            break;
        }
        
        if (strcmp("MemTotal:", label) == 0) {
            r.MemTotal = value;
        } else if (strcmp("MemFree:", label) == 0) {
            r.MemFree = value;
        } else if (strcmp("Buffers:", label) == 0) {
            r.Buffers = value;
        } else if (strcmp("Cached:", label) == 0) {
            r.Cached = value;
        } else if (strcmp("SwapTotal:", label) == 0) {
            r.SwapTotal = value;
        } else if (strcmp("SwapFree:", label) == 0) {
            r.SwapFree = value;
        }
    }
    
    fclose(f);
    
    r.sample_time = srs_get_system_time_ms();
    r.MemActive = r.MemTotal - r.MemFree;
    r.RealInUse = r.MemActive - r.Buffers - r.Cached;
    r.NotInUse = r.MemTotal - r.RealInUse;
    
    r.ok = true;
    if (r.MemTotal > 0) {
        r.percent_ram = (float)(r.RealInUse / (double)r.MemTotal);
    }
    if (r.SwapTotal > 0) {
        r.percent_swap = (float)((r.SwapTotal - r.SwapFree) / (double)r.SwapTotal);
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

SrsPlatformInfo::SrsPlatformInfo()
{
    ok = false;
    
    srs_startup_time = srs_get_system_time_ms();
    
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
    r.ok = true;
    
    if (true) {
        FILE* f = fopen("/proc/uptime", "r");
        if (f == NULL) {
            srs_warn("open uptime failed, ignore");
            return;
        }
        
        int ret = fscanf(f, "%lf %lf\n", &r.os_uptime, &r.os_ilde_time);
    
        fclose(f);

        if (ret < 0) {
            r.ok = false;
        }
    }
    
    if (true) {
        FILE* f = fopen("/proc/loadavg", "r");
        if (f == NULL) {
            srs_warn("open loadavg failed, ignore");
            return;
        }
        
        int ret = fscanf(f, "%lf %lf %lf\n", 
            &r.load_one_minutes, &r.load_five_minutes, &r.load_fifteen_minutes);
    
        fclose(f);

        if (ret < 0) {
            r.ok = false;
        }
    }
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
            SrsNetworkDevices& r = _srs_system_network_devices[i];
            r.ok = false;
            r.sample_time = 0;
    
            int ret = fscanf(f, "%6[^:]:%llu %lu %lu %lu %lu %lu %lu %lu %llu %lu %lu %lu %lu %lu %lu %lu\n", 
                r.name, &r.rbytes, &r.rpackets, &r.rerrs, &r.rdrop, &r.rfifo, &r.rframe, &r.rcompressed, &r.rmulticast,
                &r.sbytes, &r.spackets, &r.serrs, &r.sdrop, &r.sfifo, &r.scolls, &r.scarrier, &r.scompressed);
                
            if (ret == 17) {
                r.ok = true;
                _nb_srs_system_network_devices = i + 1;
                r.sample_time = srs_get_system_time_ms();
            }
            
            if (ret == EOF) {
                break;
            }
        }
    
        fclose(f);
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
        sockaddr* addr = p->ifa_addr;
        
        // retrieve ipv4 addr
        if (addr->sa_family == AF_INET) {
            in_addr* inaddr = &((sockaddr_in*)addr)->sin_addr;
            
            char buf[16];
            memset(buf, 0, sizeof(buf));
            
            if ((inet_ntop(addr->sa_family, inaddr, buf, sizeof(buf))) == NULL) {
                srs_warn("convert local ip failed");
                break;
            }
            
            std::string ip = buf;
            if (ip != SRS_LOCAL_LOOP_IP) {
                srs_trace("retrieve local ipv4 addresses: %s", ip.c_str());
                ips.push_back(ip);
            }
        }
        
        p = p->ifa_next;
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
    
    srs_verbose("get peer ip success. ip=%s, fd=%d", ip, fd);
    
    return ip;
}
