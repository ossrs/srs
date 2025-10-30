//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_app_utility.hpp>

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <map>
#include <math.h>
#include <stdlib.h>
#include <sys/time.h>
#ifdef SRS_OSX
#include <sys/sysctl.h>
#endif
using namespace std;

#include <srs_app_config.hpp>
#include <srs_kernel_buffer.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_kbps.hpp>
#include <srs_kernel_log.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_amf0.hpp>
#include <srs_protocol_json.hpp>

// the longest time to wait for a process to quit.
#define SRS_PROCESS_QUIT_TIMEOUT_MS 1000

SrsLogLevel srs_get_log_level(string level)
{
    if ("verbose" == level) {
        return SrsLogLevelVerbose;
    } else if ("info" == level) {
        return SrsLogLevelInfo;
    } else if ("trace" == level) {
        return SrsLogLevelTrace;
    } else if ("warn" == level) {
        return SrsLogLevelWarn;
    } else if ("error" == level) {
        return SrsLogLevelError;
    } else {
        return SrsLogLevelDisabled;
    }
}

SrsLogLevel srs_get_log_level_v2(string level)
{
    if ("trace" == level) {
        return SrsLogLevelVerbose;
    } else if ("debug" == level) {
        return SrsLogLevelInfo;
    } else if ("info" == level) {
        return SrsLogLevelTrace;
    } else if ("warn" == level) {
        return SrsLogLevelWarn;
    } else if ("error" == level) {
        return SrsLogLevelError;
    } else {
        return SrsLogLevelDisabled;
    }
}

string srs_path_build_stream(string template_path, string vhost, string app, string stream)
{
    std::string path = template_path;

    // variable [vhost]
    path = srs_strings_replace(path, "[vhost]", vhost);
    // variable [app]
    path = srs_strings_replace(path, "[app]", app);
    // variable [stream]
    path = srs_strings_replace(path, "[stream]", stream);

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
    struct tm now;
    // Each of these functions returns NULL in case an error was detected. @see https://linux.die.net/man/3/localtime_r
    if (_srs_config->get_utc_time()) {
        if (gmtime_r(&tv.tv_sec, &now) == NULL) {
            return path;
        }
    } else {
        if (localtime_r(&tv.tv_sec, &now) == NULL) {
            return path;
        }
    }

    // the buffer to format the date and time.
    char buf[64];

    // [2006], replace with current year.
    if (true) {
        snprintf(buf, sizeof(buf), "%04d", 1900 + now.tm_year);
        path = srs_strings_replace(path, "[2006]", buf);
    }
    // [01], replace this const to current month.
    if (true) {
        snprintf(buf, sizeof(buf), "%02d", 1 + now.tm_mon);
        path = srs_strings_replace(path, "[01]", buf);
    }
    // [02], replace this const to current date.
    if (true) {
        snprintf(buf, sizeof(buf), "%02d", now.tm_mday);
        path = srs_strings_replace(path, "[02]", buf);
    }
    // [15], replace this const to current hour.
    if (true) {
        snprintf(buf, sizeof(buf), "%02d", now.tm_hour);
        path = srs_strings_replace(path, "[15]", buf);
    }
    // [04], repleace this const to current minute.
    if (true) {
        snprintf(buf, sizeof(buf), "%02d", now.tm_min);
        path = srs_strings_replace(path, "[04]", buf);
    }
    // [05], repleace this const to current second.
    if (true) {
        snprintf(buf, sizeof(buf), "%02d", now.tm_sec);
        path = srs_strings_replace(path, "[05]", buf);
    }
    // [999], repleace this const to current millisecond.
    if (true) {
        snprintf(buf, sizeof(buf), "%03d", (int)(tv.tv_usec / 1000));
        path = srs_strings_replace(path, "[999]", buf);
    }
    // [timestamp],replace this const to current UNIX timestamp in ms.
    if (true) {
        int64_t now_us = ((int64_t)tv.tv_sec) * 1000 * 1000 + (int64_t)tv.tv_usec;
        path = srs_strings_replace(path, "[timestamp]", srs_strconv_format_int(now_us / 1000));
    }

    return path;
}

SrsAppUtility::SrsAppUtility()
{
}

SrsAppUtility::~SrsAppUtility()
{
}

// LCOV_EXCL_START
srs_error_t SrsAppUtility::kill(int &pid)
{
    srs_error_t err = srs_success;

    if (pid <= 0) {
        return err;
    }

    // first, try kill by SIGTERM.
    if (::kill(pid, SIGTERM) < 0) {
        return srs_error_new(ERROR_SYSTEM_KILL, "kill");
    }

    // wait to quit.
    srs_trace("send SIGTERM to pid=%d", pid);
    for (int i = 0; i < SRS_PROCESS_QUIT_TIMEOUT_MS / 10; i++) {
        int status = 0;
        pid_t qpid = -1;
        if ((qpid = waitpid(pid, &status, WNOHANG)) < 0) {
            return srs_error_new(ERROR_SYSTEM_KILL, "kill");
        }

        // 0 is not quit yet.
        if (qpid == 0) {
            srs_usleep(10 * 1000);
            continue;
        }

        // killed, set pid to -1.
        srs_trace("SIGTERM stop process pid=%d ok.", pid);
        pid = -1;

        return err;
    }

    // then, try kill by SIGKILL.
    if (::kill(pid, SIGKILL) < 0) {
        return srs_error_new(ERROR_SYSTEM_KILL, "kill");
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
        srs_usleep(10 * 1000);
        continue;
    }

    srs_trace("SIGKILL stop process pid=%d ok.", pid);
    pid = -1;

    return err;
}
// LCOV_EXCL_STOP

static SrsRusage _srs_system_rusage;

SrsRusage::SrsRusage()
{
    ok_ = false;
    sample_time_ = 0;
    memset(&r_, 0, sizeof(rusage));
}

SrsRusage *srs_get_system_rusage()
{
    return &_srs_system_rusage;
}

void srs_update_system_rusage()
{
    if (getrusage(RUSAGE_SELF, &_srs_system_rusage.r_) < 0) {
        srs_warn("getrusage failed, ignore");
        return;
    }

    _srs_system_rusage.sample_time_ = srsu2ms(srs_time_now_realtime());

    _srs_system_rusage.ok_ = true;
}

static SrsProcSelfStat _srs_system_cpu_self_stat;
static SrsProcSystemStat _srs_system_cpu_system_stat;

SrsProcSelfStat::SrsProcSelfStat()
{
    ok_ = false;
    sample_time_ = 0;
    percent_ = 0;

    pid_ = 0;
    memset(comm_, 0, sizeof(comm_));
    state_ = '0';
    ppid_ = 0;
    pgrp_ = 0;
    session_ = 0;
    tty_nr_ = 0;
    tpgid_ = 0;
    flags_ = 0;
    minflt_ = 0;
    cminflt_ = 0;
    majflt_ = 0;
    cmajflt_ = 0;
    utime_ = 0;
    stime_ = 0;
    cutime_ = 0;
    cstime_ = 0;
    priority_ = 0;
    nice_ = 0;
    num_threads_ = 0;
    itrealvalue_ = 0;
    starttime_ = 0;
    vsize_ = 0;
    rss_ = 0;
    rsslim_ = 0;
    startcode_ = 0;
    endcode_ = 0;
    startstack_ = 0;
    kstkesp_ = 0;
    kstkeip_ = 0;
    signal_ = 0;
    blocked_ = 0;
    sigignore_ = 0;
    sigcatch_ = 0;
    wchan_ = 0;
    nswap_ = 0;
    cnswap_ = 0;
    exit_signal_ = 0;
    processor_ = 0;
    rt_priority_ = 0;
    policy_ = 0;
    delayacct_blkio_ticks_ = 0;
    guest_time_ = 0;
    cguest_time_ = 0;
}

SrsProcSystemStat::SrsProcSystemStat()
{
    ok_ = false;
    sample_time_ = 0;
    percent_ = 0;
    total_delta_ = 0;
    user_ = 0;
    nice_ = 0;
    sys_ = 0;
    idle_ = 0;
    iowait_ = 0;
    irq_ = 0;
    softirq_ = 0;
    steal_ = 0;
    guest_ = 0;
}

int64_t SrsProcSystemStat::total()
{
    return user_ + nice_ + sys_ + idle_ + iowait_ + irq_ + softirq_ + steal_ + guest_;
}

ISrsHost::ISrsHost()
{
}

ISrsHost::~ISrsHost()
{
}

SrsHost::SrsHost()
{
}

SrsHost::~SrsHost()
{
}

// LCOV_EXCL_START
SrsProcSelfStat *SrsHost::self_proc_stat()
{
    return srs_get_self_proc_stat();
}

SrsProcSelfStat *srs_get_self_proc_stat()
{
    return &_srs_system_cpu_self_stat;
}

SrsProcSystemStat *srs_get_system_proc_stat()
{
    return &_srs_system_cpu_system_stat;
}

bool get_proc_system_stat(SrsProcSystemStat &r)
{
#if !defined(SRS_OSX)
    FILE *f = fopen("/proc/stat", "r");
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
               &r.user_,
               &r.nice_,
               &r.sys_,
               &r.idle_,
               &r.iowait_,
               &r.irq_,
               &r.softirq_,
               &r.steal_,
               &r.guest_);

        break;
    }

    fclose(f);
#endif

    r.ok_ = true;

    return true;
}

bool get_proc_self_stat(SrsProcSelfStat &r)
{
#if !defined(SRS_OSX)
    FILE *f = fopen("/proc/self/stat", "r");
    if (f == NULL) {
        srs_warn("open self cpu stat failed, ignore");
        return false;
    }

    // Note that we must read less than the size of r.comm_, such as %31s for r.comm_ is char[32].
    fscanf(f, "%d %31s %c %d %d %d %d "
              "%d %u %lu %lu %lu %lu "
              "%lu %lu %ld %ld %ld %ld "
              "%ld %ld %llu %lu %ld "
              "%lu %lu %lu %lu %lu "
              "%lu %lu %lu %lu %lu "
              "%lu %lu %lu %d %d "
              "%u %u %llu "
              "%lu %ld",
           &r.pid_, r.comm_, &r.state_, &r.ppid_, &r.pgrp_, &r.session_, &r.tty_nr_,
           &r.tpgid_, &r.flags_, &r.minflt_, &r.cminflt_, &r.majflt_, &r.cmajflt_,
           &r.utime_, &r.stime_, &r.cutime_, &r.cstime_, &r.priority_, &r.nice_,
           &r.num_threads_, &r.itrealvalue_, &r.starttime_, &r.vsize_, &r.rss_,
           &r.rsslim_, &r.startcode_, &r.endcode_, &r.startstack_, &r.kstkesp_,
           &r.kstkeip_, &r.signal_, &r.blocked_, &r.sigignore_, &r.sigcatch_,
           &r.wchan_, &r.nswap_, &r.cnswap_, &r.exit_signal_, &r.processor_,
           &r.rt_priority_, &r.policy_, &r.delayacct_blkio_ticks_,
           &r.guest_time_, &r.cguest_time_);

    fclose(f);
#endif

    r.ok_ = true;

    return true;
}
// LCOV_EXCL_STOP

void srs_update_proc_stat()
{
    // @see: http://stackoverflow.com/questions/7298646/calculating-user-nice-sys-idle-iowait-irq-and-sirq-from-proc-stat/7298711
    // @see https://github.com/ossrs/srs/issues/397
    static int user_hz = 0;
    if (user_hz <= 0) {
        user_hz = (int)sysconf(_SC_CLK_TCK);
        srs_info("USER_HZ=%d", user_hz);
        srs_assert(user_hz > 0);
    }

    // system cpu stat
    if (true) {
        SrsProcSystemStat r;
        if (!get_proc_system_stat(r)) {
            return;
        }

        r.sample_time_ = srsu2ms(srs_time_now_realtime());

        // calc usage in percent
        SrsProcSystemStat &o = _srs_system_cpu_system_stat;

        // @see: http://blog.csdn.net/nineday/article/details/1928847
        // @see: http://stackoverflow.com/questions/16011677/calculating-cpu-usage-using-proc-files
        if (o.total() > 0) {
            r.total_delta_ = r.total() - o.total();
        }
        if (r.total_delta_ > 0) {
            int64_t idle = r.idle_ - o.idle_;
            r.percent_ = (float)(1 - idle / (double)r.total_delta_);
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

        r.sample_time_ = srsu2ms(srs_time_now_realtime());

        // calc usage in percent
        SrsProcSelfStat &o = _srs_system_cpu_self_stat;

        // @see: http://stackoverflow.com/questions/16011677/calculating-cpu-usage-using-proc-files
        int64_t total = r.sample_time_ - o.sample_time_;
        int64_t usage = (r.utime_ + r.stime_) - (o.utime_ + o.stime_);
        if (total > 0) {
            r.percent_ = (float)(usage * 1000 / (double)total / user_hz);
        }

        // upate cache.
        _srs_system_cpu_self_stat = r;
    }
}

SrsDiskStat::SrsDiskStat()
{
    ok_ = false;
    sample_time_ = 0;
    in_KBps_ = out_KBps_ = 0;
    busy_ = 0;

    pgpgin_ = 0;
    pgpgout_ = 0;

    rd_ios_ = rd_merges_ = 0;
    rd_sectors_ = 0;
    rd_ticks_ = 0;

    wr_ios_ = wr_merges_ = 0;
    wr_sectors_ = 0;
    wr_ticks_ = nb_current_ = ticks_ = aveq_ = 0;
}

static SrsDiskStat _srs_disk_stat;

SrsDiskStat *srs_get_disk_stat()
{
    return &_srs_disk_stat;
}

bool srs_get_disk_vmstat_stat(SrsDiskStat &r)
{
#if !defined(SRS_OSX) && !defined(SRS_CYGWIN64)
    FILE *f = fopen("/proc/vmstat", "r");
    if (f == NULL) {
        srs_warn("open vmstat failed, ignore");
        return false;
    }

    r.sample_time_ = srsu2ms(srs_time_now_realtime());

    static char buf[1024];
    while (fgets(buf, sizeof(buf), f)) {
        // @see: read_vmstat_paging() from https://github.com/sysstat/sysstat/blob/master/rd_stats.c#L495
        if (strncmp(buf, "pgpgin ", 7) == 0) {
            sscanf(buf + 7, "%lu\n", &r.pgpgin_);
        } else if (strncmp(buf, "pgpgout ", 8) == 0) {
            sscanf(buf + 8, "%lu\n", &r.pgpgout_);
        }
    }

    fclose(f);
#endif

    r.ok_ = true;

    return true;
}

// LCOV_EXCL_START
bool srs_get_disk_diskstats_stat(SrsDiskStat &r)
{
    r.ok_ = true;
    r.sample_time_ = srsu2ms(srs_time_now_realtime());

#if !defined(SRS_OSX)
    // if disabled, ignore all devices.
    SrsConfDirective *conf = _srs_config->get_stats_disk_device();
    if (conf == NULL) {
        return true;
    }

    FILE *f = fopen("/proc/diskstats", "r");
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

        for (int i = 0; i < (int)conf->args_.size(); i++) {
            string name_ok = conf->args_.at(i);

            if (strcmp(name_ok.c_str(), name) != 0) {
                continue;
            }

            r.rd_ios_ += rd_ios;
            r.rd_merges_ += rd_merges;
            r.rd_sectors_ += rd_sectors;
            r.rd_ticks_ += rd_ticks;
            r.wr_ios_ += wr_ios;
            r.wr_merges_ += wr_merges;
            r.wr_sectors_ += wr_sectors;
            r.wr_ticks_ += wr_ticks;
            r.nb_current_ += nb_current;
            r.ticks_ += ticks;
            r.aveq_ += aveq;

            break;
        }
    }

    fclose(f);
#endif

    r.ok_ = true;

    return true;
}
// LCOV_EXCL_STOP

// LCOV_EXCL_START
void srs_update_disk_stat()
{
    SrsDiskStat r;
    if (!srs_get_disk_vmstat_stat(r)) {
        return;
    }
    if (!srs_get_disk_diskstats_stat(r)) {
        return;
    }
    if (!get_proc_system_stat(r.cpu_)) {
        return;
    }

    SrsDiskStat &o = _srs_disk_stat;
    if (!o.ok_) {
        _srs_disk_stat = r;
        return;
    }

    // vmstat
    if (true) {
        int64_t duration_ms = r.sample_time_ - o.sample_time_;

        if (o.pgpgin_ > 0 && r.pgpgin_ > o.pgpgin_ && duration_ms > 0) {
            // KBps = KB * 1000 / ms = KB/s
            r.in_KBps_ = (int)((r.pgpgin_ - o.pgpgin_) * 1000 / duration_ms);
        }

        if (o.pgpgout_ > 0 && r.pgpgout_ > o.pgpgout_ && duration_ms > 0) {
            // KBps = KB * 1000 / ms = KB/s
            r.out_KBps_ = (int)((r.pgpgout_ - o.pgpgout_) * 1000 / duration_ms);
        }
    }

    // diskstats
    if (r.cpu_.ok_ && o.cpu_.ok_) {
        SrsCpuInfo *cpuinfo = srs_get_cpuinfo();
        r.cpu_.total_delta_ = r.cpu_.total() - o.cpu_.total();

        if (r.cpu_.ok_ && r.cpu_.total_delta_ > 0 && cpuinfo->ok_ && cpuinfo->nb_processors_ > 0 && o.ticks_ < r.ticks_) {
            // @see: write_ext_stat() from https://github.com/sysstat/sysstat/blob/master/iostat.c#L979
            // TODO: FIXME: the USER_HZ assert to 100, so the total_delta ticks *10 is ms.
            double delta_ms = r.cpu_.total_delta_ * 10 / cpuinfo->nb_processors_;
            unsigned int ticks = r.ticks_ - o.ticks_;

            // busy in [0, 1], where 0.1532 means 15.32%
            r.busy_ = (float)(ticks / delta_ms);
        }
    }

    _srs_disk_stat = r;
}
// LCOV_EXCL_STOP

SrsMemInfo::SrsMemInfo()
{
    ok_ = false;
    sample_time_ = 0;

    percent_ram_ = 0;
    percent_swap_ = 0;

    MemActive_ = 0;
    RealInUse_ = 0;
    NotInUse_ = 0;
    MemTotal_ = 0;
    MemFree_ = 0;
    Buffers_ = 0;
    Cached_ = 0;
    SwapTotal_ = 0;
    SwapFree_ = 0;
}

static SrsMemInfo _srs_system_meminfo;

SrsMemInfo *srs_get_meminfo()
{
    return &_srs_system_meminfo;
}

void srs_update_meminfo()
{
    SrsMemInfo &r = _srs_system_meminfo;

#if !defined(SRS_OSX)
    FILE *f = fopen("/proc/meminfo", "r");
    if (f == NULL) {
        srs_warn("open meminfo failed, ignore");
        return;
    }

    static char buf[1024];
    while (fgets(buf, sizeof(buf), f)) {
        // @see: read_meminfo() from https://github.com/sysstat/sysstat/blob/master/rd_stats.c#L227
        if (strncmp(buf, "MemTotal:", 9) == 0) {
            sscanf(buf + 9, "%lu", &r.MemTotal_);
        } else if (strncmp(buf, "MemFree:", 8) == 0) {
            sscanf(buf + 8, "%lu", &r.MemFree_);
        } else if (strncmp(buf, "Buffers:", 8) == 0) {
            sscanf(buf + 8, "%lu", &r.Buffers_);
        } else if (strncmp(buf, "Cached:", 7) == 0) {
            sscanf(buf + 7, "%lu", &r.Cached_);
        } else if (strncmp(buf, "SwapTotal:", 10) == 0) {
            sscanf(buf + 10, "%lu", &r.SwapTotal_);
        } else if (strncmp(buf, "SwapFree:", 9) == 0) {
            sscanf(buf + 9, "%lu", &r.SwapFree_);
        }
    }

    fclose(f);
#endif

    r.sample_time_ = srsu2ms(srs_time_now_realtime());
    r.MemActive_ = r.MemTotal_ - r.MemFree_;
    r.RealInUse_ = r.MemActive_ - r.Buffers_ - r.Cached_;
    r.NotInUse_ = r.MemTotal_ - r.RealInUse_;

    if (r.MemTotal_ > 0) {
        r.percent_ram_ = (float)(r.RealInUse_ / (double)r.MemTotal_);
    }
    if (r.SwapTotal_ > 0) {
        r.percent_swap_ = (float)((r.SwapTotal_ - r.SwapFree_) / (double)r.SwapTotal_);
    }

    r.ok_ = true;
}

SrsCpuInfo::SrsCpuInfo()
{
    ok_ = false;

    nb_processors_ = 0;
    nb_processors_online_ = 0;
}

SrsCpuInfo *srs_get_cpuinfo()
{
    static SrsCpuInfo *cpu = NULL;
    if (cpu != NULL) {
        return cpu;
    }

    // initialize cpu info.
    cpu = new SrsCpuInfo();
    cpu->ok_ = true;
    cpu->nb_processors_ = (int)sysconf(_SC_NPROCESSORS_CONF);
    cpu->nb_processors_online_ = (int)sysconf(_SC_NPROCESSORS_ONLN);

    return cpu;
}

SrsPlatformInfo::SrsPlatformInfo()
{
    ok_ = false;

    srs_startup_time_ = 0;

    os_uptime_ = 0;
    os_ilde_time_ = 0;

    load_one_minutes_ = 0;
    load_five_minutes_ = 0;
    load_fifteen_minutes_ = 0;
}

static SrsPlatformInfo _srs_system_platform_info;

SrsPlatformInfo *srs_get_platform_info()
{
    return &_srs_system_platform_info;
}

void srs_update_platform_info()
{
    SrsPlatformInfo &r = _srs_system_platform_info;

    r.srs_startup_time_ = srsu2ms(srs_time_since_startup());

#if !defined(SRS_OSX)
    if (true) {
        FILE *f = fopen("/proc/uptime", "r");
        if (f == NULL) {
            srs_warn("open uptime failed, ignore");
            return;
        }

        fscanf(f, "%lf %lf\n", &r.os_uptime_, &r.os_ilde_time_);

        fclose(f);
    }

    if (true) {
        FILE *f = fopen("/proc/loadavg", "r");
        if (f == NULL) {
            srs_warn("open loadavg failed, ignore");
            return;
        }

        // @see: read_loadavg() from https://github.com/sysstat/sysstat/blob/master/rd_stats.c#L402
        // @remark, we use our algorithm, not sysstat.
        fscanf(f, "%lf %lf %lf\n",
               &r.load_one_minutes_,
               &r.load_five_minutes_,
               &r.load_fifteen_minutes_);

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
        r.os_uptime_ = difftime(csec, bsec);
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

        r.load_one_minutes_ = (double)la.ldavg[0] / la.fscale;
        r.load_five_minutes_ = (double)la.ldavg[1] / la.fscale;
        r.load_fifteen_minutes_ = (double)la.ldavg[2] / la.fscale;
    }
#endif

    r.ok_ = true;
}

SrsSnmpUdpStat::SrsSnmpUdpStat()
{
    ok_ = false;

    in_datagrams_ = 0;
    no_ports_ = 0;
    in_errors_ = 0;
    out_datagrams_ = 0;
    rcv_buf_errors_ = 0;
    snd_buf_errors_ = 0;
    in_csum_errors_ = 0;

    rcv_buf_errors_delta_ = 0;
    snd_buf_errors_delta_ = 0;
}

SrsSnmpUdpStat::~SrsSnmpUdpStat()
{
}

static SrsSnmpUdpStat _srs_snmp_udp_stat;

bool get_udp_snmp_statistic(SrsSnmpUdpStat &r)
{
#if !defined(SRS_OSX) && !defined(SRS_CYGWIN64)
    if (true) {
        FILE *f = fopen("/proc/net/snmp", "r");
        if (f == NULL) {
            srs_warn("open proc network snmp failed, ignore");
            return false;
        }

        // ignore title.
        static char buf[1024];
        fgets(buf, sizeof(buf), f);

        while (fgets(buf, sizeof(buf), f)) {
            // udp stat title
            if (strncmp(buf, "Udp: ", 5) == 0) {
                // read tcp stat data
                if (!fgets(buf, sizeof(buf), f)) {
                    break;
                }
                // parse tcp stat data
                if (strncmp(buf, "Udp: ", 5) == 0) {
                    sscanf(buf + 5, "%llu %llu %llu %llu %llu %llu %llu\n",
                           &r.in_datagrams_,
                           &r.no_ports_,
                           &r.in_errors_,
                           &r.out_datagrams_,
                           &r.rcv_buf_errors_,
                           &r.snd_buf_errors_,
                           &r.in_csum_errors_);
                }
            }
        }
        fclose(f);
    }
#endif
    r.ok_ = true;

    return true;
}

SrsSnmpUdpStat *srs_get_udp_snmp_stat()
{
    return &_srs_snmp_udp_stat;
}

void srs_update_udp_snmp_statistic()
{
    SrsSnmpUdpStat r;
    if (!get_udp_snmp_statistic(r)) {
        return;
    }

    SrsSnmpUdpStat &o = _srs_snmp_udp_stat;
    if (o.rcv_buf_errors_ > 0) {
        r.rcv_buf_errors_delta_ = int(r.rcv_buf_errors_ - o.rcv_buf_errors_);
    }

    if (o.snd_buf_errors_ > 0) {
        r.snd_buf_errors_delta_ = int(r.snd_buf_errors_ - o.snd_buf_errors_);
    }

    _srs_snmp_udp_stat = r;
}

SrsNetworkDevices::SrsNetworkDevices()
{
    ok_ = false;

    memset(name_, 0, sizeof(name_));
    sample_time_ = 0;

    rbytes_ = 0;
    rpackets_ = 0;
    rerrs_ = 0;
    rdrop_ = 0;
    rfifo_ = 0;
    rframe_ = 0;
    rcompressed_ = 0;
    rmulticast_ = 0;

    sbytes_ = 0;
    spackets_ = 0;
    serrs_ = 0;
    sdrop_ = 0;
    sfifo_ = 0;
    scolls_ = 0;
    scarrier_ = 0;
    scompressed_ = 0;
}

#define MAX_NETWORK_DEVICES_COUNT 16
static SrsNetworkDevices _srs_system_network_devices[MAX_NETWORK_DEVICES_COUNT];
static int _nb_srs_system_network_devices = -1;

SrsNetworkDevices *srs_get_network_devices()
{
    return _srs_system_network_devices;
}

int srs_get_network_devices_count()
{
    return _nb_srs_system_network_devices;
}

void srs_update_network_devices()
{
#if !defined(SRS_OSX) && !defined(SRS_CYGWIN64)
    if (true) {
        FILE *f = fopen("/proc/net/dev", "r");
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

            SrsNetworkDevices &r = _srs_system_network_devices[i];

            // @see: read_net_dev() from https://github.com/sysstat/sysstat/blob/master/rd_stats.c#L786
            // @remark, we use our algorithm, not sysstat.
            char fname[7];
            sscanf(buf, "%6[^:]:%llu %lu %lu %lu %lu %lu %lu %lu %llu %lu %lu %lu %lu %lu %lu %lu\n",
                   fname, &r.rbytes_, &r.rpackets_, &r.rerrs_, &r.rdrop_, &r.rfifo_, &r.rframe_, &r.rcompressed_, &r.rmulticast_,
                   &r.sbytes_, &r.spackets_, &r.serrs_, &r.sdrop_, &r.sfifo_, &r.scolls_, &r.scarrier_, &r.scompressed_);

            sscanf(fname, "%s", r.name_);
            _nb_srs_system_network_devices = i + 1;
            srs_info("scan network device ifname=%s, total=%d", r.name_, _nb_srs_system_network_devices);

            r.sample_time_ = srsu2ms(srs_time_now_realtime());
            r.ok_ = true;
        }

        fclose(f);
    }
#endif
}

SrsNetworkRtmpServer::SrsNetworkRtmpServer()
{
    ok_ = false;
    sample_time_ = rbytes_ = sbytes_ = 0;
    nb_conn_sys_ = nb_conn_srs_ = 0;
    nb_conn_sys_et_ = nb_conn_sys_tw_ = 0;
    nb_conn_sys_udp_ = 0;
    rkbps_ = skbps_ = 0;
    rkbps_30s_ = skbps_30s_ = 0;
    rkbps_5m_ = skbps_5m_ = 0;
}

static SrsNetworkRtmpServer _srs_network_rtmp_server;

SrsNetworkRtmpServer *srs_get_network_rtmp_server()
{
    return &_srs_network_rtmp_server;
}

// @see: http://stackoverflow.com/questions/5992211/list-of-possible-internal-socket-statuses-from-proc
enum {
    SYS_TCP_ESTABLISHED = 0x01,
    SYS_TCP_SYN_SENT,   // 0x02
    SYS_TCP_SYN_RECV,   // 0x03
    SYS_TCP_FIN_WAIT1,  // 0x04
    SYS_TCP_FIN_WAIT2,  // 0x05
    SYS_TCP_TIME_WAIT,  // 0x06
    SYS_TCP_CLOSE,      // 0x07
    SYS_TCP_CLOSE_WAIT, // 0x08
    SYS_TCP_LAST_ACK,   // 0x09
    SYS_TCP_LISTEN,     // 0x0A
    SYS_TCP_CLOSING,    // 0x0B /* Now a valid state */

    SYS_TCP_MAX_STATES // 0x0C /* Leave at the end! */
};

void srs_update_rtmp_server(int nb_conn, SrsKbps *kbps)
{
    SrsNetworkRtmpServer &r = _srs_network_rtmp_server;

    int nb_socks = 0;
    int nb_tcp4_hashed = 0;
    int nb_tcp_orphans = 0;
    int nb_tcp_tws = 0;
    int nb_tcp_total = 0;
    int nb_tcp_mem = 0;
    int nb_udp4 = 0;

#if !defined(SRS_OSX) && !defined(SRS_CYGWIN64)
    if (true) {
        FILE *f = fopen("/proc/net/sockstat", "r");
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
    nb_socks = 0;
    nb_tcp4_hashed = 0;
    nb_tcp_orphans = 0;
    nb_tcp_tws = 0;
    nb_tcp_total = 0;
    nb_tcp_mem = 0;
    nb_udp4 = 0;

    (void)nb_socks;
    (void)nb_tcp_mem;
    (void)nb_tcp4_hashed;
    (void)nb_tcp_orphans;
#endif

    int nb_tcp_estab = 0;

#if !defined(SRS_OSX) && !defined(SRS_CYGWIN64)
    if (true) {
        FILE *f = fopen("/proc/net/snmp", "r");
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
#endif

    // @see: https://github.com/shemminger/iproute2/blob/master/misc/ss.c
    // TODO: FIXME: ignore the slabstat, @see: get_slabstat()
    if (true) {
        // @see: print_summary()
        r.nb_conn_sys_ = nb_tcp_total + nb_tcp_tws;
        r.nb_conn_sys_et_ = nb_tcp_estab;
        r.nb_conn_sys_tw_ = nb_tcp_tws;
        r.nb_conn_sys_udp_ = nb_udp4;
    }

    if (true) {
        r.ok_ = true;

        r.nb_conn_srs_ = nb_conn;
        r.sample_time_ = srsu2ms(srs_time_now_realtime());

        r.rbytes_ = kbps->get_recv_bytes();
        r.rkbps_ = kbps->get_recv_kbps();
        r.rkbps_30s_ = kbps->get_recv_kbps_30s();
        r.rkbps_5m_ = kbps->get_recv_kbps_5m();

        r.sbytes_ = kbps->get_send_bytes();
        r.skbps_ = kbps->get_send_kbps();
        r.skbps_30s_ = kbps->get_send_kbps_30s();
        r.skbps_5m_ = kbps->get_send_kbps_5m();
    }
}

// LCOV_EXCL_START
string srs_get_local_ip(int fd)
{
    // discovery client information
    sockaddr_storage addr;
    socklen_t addrlen = sizeof(addr);
    if (getsockname(fd, (sockaddr *)&addr, &addrlen) == -1) {
        return "";
    }

    char saddr[64];
    char *h = (char *)saddr;
    socklen_t nbh = (socklen_t)sizeof(saddr);
    const int r0 = getnameinfo((const sockaddr *)&addr, addrlen, h, nbh, NULL, 0, NI_NUMERICHOST);
    if (r0) {
        return "";
    }

    return std::string(saddr);
}
// LCOV_EXCL_STOP

int srs_get_local_port(int fd)
{
    // discovery client information
    sockaddr_storage addr;
    socklen_t addrlen = sizeof(addr);
    if (getsockname(fd, (sockaddr *)&addr, &addrlen) == -1) {
        return 0;
    }

    int port = 0;
    switch (addr.ss_family) {
    case AF_INET:
        port = ntohs(((sockaddr_in *)&addr)->sin_port);
        break;
    case AF_INET6:
        port = ntohs(((sockaddr_in6 *)&addr)->sin6_port);
        break;
    }

    return port;
}

// LCOV_EXCL_START
string srs_get_peer_ip(int fd)
{
    // discovery client information
    sockaddr_storage addr;
    socklen_t addrlen = sizeof(addr);
    if (getpeername(fd, (sockaddr *)&addr, &addrlen) == -1) {
        return "";
    }

    char saddr[64];
    char *h = (char *)saddr;
    socklen_t nbh = (socklen_t)sizeof(saddr);
    const int r0 = getnameinfo((const sockaddr *)&addr, addrlen, h, nbh, NULL, 0, NI_NUMERICHOST);
    if (r0) {
        return "";
    }

    return std::string(saddr);
}

int srs_get_peer_port(int fd)
{
    // discovery client information
    sockaddr_storage addr;
    socklen_t addrlen = sizeof(addr);
    if (getpeername(fd, (sockaddr *)&addr, &addrlen) == -1) {
        return 0;
    }

    int port = 0;
    switch (addr.ss_family) {
    case AF_INET:
        port = ntohs(((sockaddr_in *)&addr)->sin_port);
        break;
    case AF_INET6:
        port = ntohs(((sockaddr_in6 *)&addr)->sin6_port);
        break;
    }

    return port;
}
// LCOV_EXCL_STOP

bool srs_is_boolean(string str)
{
    return str == "true" || str == "false";
}

void srs_api_dump_summaries(SrsJsonObject *obj)
{
    SrsRusage *r = srs_get_system_rusage();
    SrsProcSelfStat *u = srs_get_self_proc_stat();
    SrsProcSystemStat *s = srs_get_system_proc_stat();
    SrsCpuInfo *c = srs_get_cpuinfo();
    SrsMemInfo *m = srs_get_meminfo();
    SrsPlatformInfo *p = srs_get_platform_info();
    SrsNetworkDevices *n = srs_get_network_devices();
    SrsNetworkRtmpServer *nrs = srs_get_network_rtmp_server();
    SrsDiskStat *d = srs_get_disk_stat();

    float self_mem_percent = 0;
    if (m->MemTotal_ > 0) {
        self_mem_percent = (float)(r->r_.ru_maxrss / (double)m->MemTotal_);
    }

    int64_t now = srsu2ms(srs_time_now_realtime());
    double srs_uptime = (now - p->srs_startup_time_) / 100 / 10.0;

    int64_t n_sample_time = 0;
    int64_t nr_bytes = 0;
    int64_t ns_bytes = 0;
    int64_t nri_bytes = 0;
    int64_t nsi_bytes = 0;
    int nb_n = srs_get_network_devices_count();
    for (int i = 0; i < nb_n; i++) {
        SrsNetworkDevices &o = n[i];

        // ignore the lo interface.
        std::string inter = o.name_;
        if (!o.ok_) {
            continue;
        }

        // update the sample time.
        n_sample_time = o.sample_time_;

        // stat the intranet bytes.
        SrsProtocolUtility utility;
        if (inter == "lo" || !utility.is_internet(inter)) {
            nri_bytes += o.rbytes_;
            nsi_bytes += o.sbytes_;
            continue;
        }

        nr_bytes += o.rbytes_;
        ns_bytes += o.sbytes_;
    }

    // all data is ok?
    bool ok = (r->ok_ && u->ok_ && s->ok_ && c->ok_ && d->ok_ && m->ok_ && p->ok_ && nrs->ok_);

    SrsJsonObject *data = SrsJsonAny::object();
    obj->set("data", data);

    data->set("ok", SrsJsonAny::boolean(ok));
    data->set("now_ms", SrsJsonAny::integer(now));

    // self
    SrsJsonObject *self = SrsJsonAny::object();
    data->set("self", self);

    self->set("version", SrsJsonAny::str(RTMP_SIG_SRS_VERSION));
    self->set("pid", SrsJsonAny::integer(getpid()));
    self->set("ppid", SrsJsonAny::integer(u->ppid_));
    self->set("argv", SrsJsonAny::str(_srs_config->argv().c_str()));
    self->set("cwd", SrsJsonAny::str(_srs_config->cwd().c_str()));
    self->set("mem_kbyte", SrsJsonAny::integer(r->r_.ru_maxrss));
    self->set("mem_percent", SrsJsonAny::number(self_mem_percent));
    self->set("cpu_percent", SrsJsonAny::number(u->percent_));
    self->set("srs_uptime", SrsJsonAny::integer(srs_uptime));

    // system
    SrsJsonObject *sys = SrsJsonAny::object();
    data->set("system", sys);

    sys->set("cpu_percent", SrsJsonAny::number(s->percent_));
    sys->set("disk_read_KBps", SrsJsonAny::integer(d->in_KBps_));
    sys->set("disk_write_KBps", SrsJsonAny::integer(d->out_KBps_));
    sys->set("disk_busy_percent", SrsJsonAny::number(d->busy_));
    sys->set("mem_ram_kbyte", SrsJsonAny::integer(m->MemTotal_));
    sys->set("mem_ram_percent", SrsJsonAny::number(m->percent_ram_));
    sys->set("mem_swap_kbyte", SrsJsonAny::integer(m->SwapTotal_));
    sys->set("mem_swap_percent", SrsJsonAny::number(m->percent_swap_));
    sys->set("cpus", SrsJsonAny::integer(c->nb_processors_));
    sys->set("cpus_online", SrsJsonAny::integer(c->nb_processors_online_));
    sys->set("uptime", SrsJsonAny::number(p->os_uptime_));
    sys->set("ilde_time", SrsJsonAny::number(p->os_ilde_time_));
    sys->set("load_1m", SrsJsonAny::number(p->load_one_minutes_));
    sys->set("load_5m", SrsJsonAny::number(p->load_five_minutes_));
    sys->set("load_15m", SrsJsonAny::number(p->load_fifteen_minutes_));
    // system network bytes stat.
    sys->set("net_sample_time", SrsJsonAny::integer(n_sample_time));
    // internet public address network device bytes.
    sys->set("net_recv_bytes", SrsJsonAny::integer(nr_bytes));
    sys->set("net_send_bytes", SrsJsonAny::integer(ns_bytes));
    // intranet private address network device bytes.
    sys->set("net_recvi_bytes", SrsJsonAny::integer(nri_bytes));
    sys->set("net_sendi_bytes", SrsJsonAny::integer(nsi_bytes));
    // srs network bytes stat.
    sys->set("srs_sample_time", SrsJsonAny::integer(nrs->sample_time_));
    sys->set("srs_recv_bytes", SrsJsonAny::integer(nrs->rbytes_));
    sys->set("srs_send_bytes", SrsJsonAny::integer(nrs->sbytes_));
    sys->set("conn_sys", SrsJsonAny::integer(nrs->nb_conn_sys_));
    sys->set("conn_sys_et", SrsJsonAny::integer(nrs->nb_conn_sys_et_));
    sys->set("conn_sys_tw", SrsJsonAny::integer(nrs->nb_conn_sys_tw_));
    sys->set("conn_sys_udp", SrsJsonAny::integer(nrs->nb_conn_sys_udp_));
    sys->set("conn_srs", SrsJsonAny::integer(nrs->nb_conn_srs_));
}

string srs_getenv(const string &key)
{
    string ekey = key;
    if (srs_strings_starts_with(key, "$")) {
        ekey = key.substr(1);
    }

    if (ekey.empty()) {
        return "";
    }

    std::string::iterator it;
    for (it = ekey.begin(); it != ekey.end(); ++it) {
        if (*it >= 'a' && *it <= 'z') {
            *it += ('A' - 'a');
        } else if (*it == '.') {
            *it = '_';
        }
    }

    char *value = ::getenv(ekey.c_str());
    if (value) {
        return value;
    }

    return "";
}
