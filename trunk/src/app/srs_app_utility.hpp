//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_APP_UTILITY_HPP
#define SRS_APP_UTILITY_HPP

#include <srs_core.hpp>

#include <sstream>
#include <string>
#include <vector>

#include <limits.h>

#include <srs_app_st.hpp>
#include <srs_kernel_log.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_utility.hpp>

class SrsKbps;
class SrsBuffer;
class SrsJsonObject;
class ISrsAppConfig;

extern SrsLogLevel srs_get_log_level(std::string level);
extern SrsLogLevel srs_get_log_level_v2(std::string level);

extern std::string srs_path_build_stream(std::string template_path, std::string vhost, std::string app, std::string stream);

// Parses what srs_path_build_stream built, such as live/livestream/index.m3u8 from [app]/[stream]/index.m3u8.
extern bool srs_path_parse_stream(std::string template_path, std::string vhost, std::string path, std::string &app, std::string &stream);

extern std::string srs_path_build_timestamp(ISrsAppConfig *config, std::string template_path);

class SrsAppUtility
{
public:
    SrsAppUtility();
    virtual ~SrsAppUtility();

public:
    virtual srs_error_t kill(int &pid);
};

// @see: man getrusage
class SrsRusage
{
public:
    bool ok_;
    // In ms.
    int64_t sample_time_;

public:
    rusage r_;

public:
    SrsRusage();

public:
    // The peak resident set size in kilobytes. The ru_maxrss of getrusage is in
    // kilobytes on Linux, but in bytes on macOS.
    int64_t maxrss_kbyte();
};

// Cached for performance.
extern SrsRusage *srs_get_system_rusage();
extern void srs_update_system_rusage();

// @see: man 5 proc, /proc/[pid]/stat
class SrsProcSelfStat
{
public:
    bool ok_;
    // In ms.
    int64_t sample_time_;
    // 0.153 is 15.3%.
    float percent_;

public:
    int pid_;
    char comm_[32];
    unsigned char state_;
    int ppid_;
    int pgrp_;
    int session_;
    int tty_nr_;
    int tpgid_;
    unsigned int flags_;
    unsigned long minflt_;
    unsigned long cminflt_;
    unsigned long majflt_;
    unsigned long cmajflt_;
    // The CPU times are in clock ticks, sysconf(_SC_CLK_TCK). The utime includes the guest time.
    unsigned long utime_;
    unsigned long stime_;
    long cutime_;
    long cstime_;
    long priority_;
    long nice_;
    long num_threads_;
    // Not maintained since Linux 2.6.17, always 0.
    long itrealvalue_;
    // In jiffies after the system boot.
    long long starttime_;
    // In bytes.
    unsigned long vsize_;
    // In pages, not bytes. Pages not demand-loaded in, or swapped out, are not counted.
    long rss_;
    // In bytes.
    unsigned long rsslim_;
    unsigned long startcode_;
    unsigned long endcode_;
    unsigned long startstack_;
    unsigned long kstkesp_;
    unsigned long kstkeip_;
    // The signal bitmaps are obsolete, since they lack the real-time signals; use /proc/[pid]/status instead.
    unsigned long signal_;
    unsigned long blocked_;
    unsigned long sigignore_;
    unsigned long sigcatch_;
    unsigned long wchan_;
    // Not maintained.
    unsigned long nswap_;
    unsigned long cnswap_;
    int exit_signal_;
    int processor_;
    unsigned int rt_priority_;
    unsigned int policy_;
    // In clock ticks.
    unsigned long long delayacct_blkio_ticks_;
    // Since Linux 2.6.24, in clock ticks.
    unsigned long guest_time_;
    long cguest_time_;

public:
    SrsProcSelfStat();
};

// @see: man 5 proc, /proc/stat
// @see: http://stackoverflow.com/questions/16011677/calculating-cpu-usage-using-proc-files
// The total of the cpu times is close to the uptime, such as 5233316.87s for an uptime of 5275153.01s on a
// single cpu machine, and the delta between two samples is closer: 1572.11s for an uptime delta of 1586.82s.
class SrsProcSystemStat
{
public:
    bool ok_;
    // In ms.
    int64_t sample_time_;
    // In [0, 1], where 0.153 is 15.3%, also for many cores.
    float percent_;
    // Zero when there is no previous sample.
    int64_t total_delta_;

public:
    // In USER_HZ, 1/100 of a second on most architectures; sysconf(_SC_CLK_TCK) gives the right value.
    unsigned long long user_;
    unsigned long long nice_;
    unsigned long long sys_;
    unsigned long long idle_;

    // The iowait, irq and softirq columns are since Linux 2.6.
    unsigned long long iowait_;
    unsigned long long irq_;
    unsigned long long softirq_;

    // Since Linux 2.6.11, the time spent in other operating systems when running in a virtualized environment.
    unsigned long long steal_;

    // Since Linux 2.6.24.
    unsigned long long guest_;

public:
    SrsProcSystemStat();

    int64_t total();
};

class ISrsHost
{
public:
    ISrsHost();
    virtual ~ISrsHost();

public:
    virtual SrsProcSelfStat *self_proc_stat() = 0;
};

class SrsHost : public ISrsHost
{
public:
    SrsHost();
    virtual ~SrsHost();

public:
    virtual SrsProcSelfStat *self_proc_stat();
};

// Cached for performance.
extern SrsProcSelfStat *srs_get_self_proc_stat();
// Cached for performance.
extern SrsProcSystemStat *srs_get_system_proc_stat();
// Without /proc, as on macOS, the stats stay not ok.
extern void srs_update_proc_stat();
extern bool get_proc_self_stat(SrsProcSelfStat &r);
extern bool srs_parse_proc_self_stat(const std::string &line, SrsProcSelfStat &r);

// @see: http://stackoverflow.com/questions/4458183/how-the-util-of-iostat-is-computed
// For the total disk io, @see: cat /proc/vmstat |grep pgpg
// For the device disk io, @see: cat /proc/diskstats
// @remark To test the disk io, run: time dd if=/dev/zero bs=1M count=2048 of=file_2G
//      The dd write MBps is right, and iotop agrees with it, the dstat result seems wrong, and iostat
//      shows only the number of writes, not the bytes.
class SrsDiskStat
{
public:
    bool ok_;
    // In ms.
    int64_t sample_time_;

    // Read KB per second.
    int in_KBps_;
    // Write KB per second.
    int out_KBps_;

    // @see: print_partition_stats() of iostat.c
    // But its value is in [0, +], such as 0.1532 for 15.32%.
    float busy_;
    // To compute busy_.
    SrsProcSystemStat cpu_;

public:
    // @see: cat /proc/vmstat
    // The total KB the system paged in from disk.
    unsigned long pgpgin_;
    // The total KB the system paged out to disk.
    unsigned long pgpgout_;

    // @see: https://www.kernel.org/doc/Documentation/iostats.txt
    // @see: http://tester-higkoo.googlecode.com/svn-history/r14/trunk/Tools/iostat/iostat.c
    // @see: cat /proc/diskstats
    //
    // Reads completed.
    unsigned int rd_ios_;
    // Adjacent reads merged into one I/O.
    unsigned int rd_merges_;
    unsigned long long rd_sectors_;
    // Milliseconds spent by all reads, from make_request() to end_that_request_last().
    unsigned int rd_ticks_;
    // Writes completed.
    unsigned int wr_ios_;
    // Adjacent writes merged into one I/O, as two 4K reads may become one 8K read.
    unsigned int wr_merges_;
    unsigned long long wr_sectors_;
    // Milliseconds spent by all writes, from make_request() to end_that_request_last().
    unsigned int wr_ticks_;
    // I/Os in progress, the only field that should go to zero.
    unsigned int nb_current_;
    // Milliseconds spent doing I/Os, increased while field 9 (nb_current_) is nonzero.
    unsigned int ticks_;
    // Weighted milliseconds spent doing I/Os: at each I/O start, completion, merge, or read of these
    // stats, it adds the I/Os in progress times the ms since its last update. It gives the average
    // queue length.
    unsigned int aveq_;

public:
    SrsDiskStat();
};

// Cached for performance.
extern SrsDiskStat *srs_get_disk_stat();
extern void srs_update_disk_stat();
extern bool srs_get_disk_diskstats_stat(SrsDiskStat &r);

// @see: cat /proc/meminfo
class SrsMemInfo
{
public:
    bool ok_;
    // In ms.
    int64_t sample_time_;
    // 0.153 is 15.3%.
    float percent_ram_;
    float percent_swap_;

public:
    // In KB, as /proc/meminfo gives.
    uint64_t MemActive_;
    uint64_t RealInUse_;
    uint64_t NotInUse_;

    unsigned long MemTotal_;
    unsigned long MemFree_;
    unsigned long Buffers_;
    unsigned long Cached_;
    unsigned long SwapTotal_;
    unsigned long SwapFree_;

public:
    SrsMemInfo();
};

// Cached for performance.
extern SrsMemInfo *srs_get_meminfo();
// Without /proc, as on macOS, the meminfo stays not ok.
extern void srs_update_meminfo();

class SrsCpuInfo
{
public:
    bool ok_;

public:
    // Configured.
    int nb_processors_;
    // Online, available now.
    int nb_processors_online_;

public:
    SrsCpuInfo();
};

// Cached for performance.
extern SrsCpuInfo *srs_get_cpuinfo();

class SrsPlatformInfo
{
public:
    bool ok_;

    // In ms.
    int64_t srs_startup_time_;

public:
    // @see: cat /proc/uptime
    // In seconds.
    double os_uptime_;
    // The idle time of all cpus in seconds, so the idle percent is
    // os_ilde_time / (os_uptime * SrsCpuInfo.nb_processors_online).
    double os_ilde_time_;

    // @see: cat /proc/loadavg
    double load_one_minutes_;
    double load_five_minutes_;
    double load_fifteen_minutes_;

public:
    SrsPlatformInfo();
};

// Cached for performance.
extern SrsPlatformInfo *srs_get_platform_info();
extern void srs_update_platform_info();

class SrsSnmpUdpStat
{
public:
    bool ok_;
    int rcv_buf_errors_delta_;
    int snd_buf_errors_delta_;

public:
    // @see: cat /proc/net/snmp | grep 'Udp:'
    // @see: https://blog.packagecloud.io/eng/2017/02/06/monitoring-tuning-linux-networking-stack-sending-data/#procnetsnmp
    // InDatagrams: incremented when a userland program reads a datagram with recvmsg, and when a UDP
    // packet is encapsulated and sent back for processing.
    unsigned long long in_datagrams_;
    // NoPorts: incremented when UDP packets arrive for a port where no program is listening.
    unsigned long long no_ports_;
    // InErrors: incremented when the receive queue has no memory, when a bad checksum is seen, and
    // when sk_add_backlog fails to add the datagram.
    unsigned long long in_errors_;
    // OutDatagrams: incremented when a UDP packet is handed down without error to the IP layer to send.
    unsigned long long out_datagrams_;
    // RcvbufErrors: incremented when sock_queue_rcv_skb reports no memory, which happens when
    // sk->sk_rmem_alloc is greater than or equal to sk->sk_rcvbuf.
    unsigned long long rcv_buf_errors_;
    // SndbufErrors: incremented when the IP layer reports an error sending the packet and no error queue
    // is set up, and when no send queue space or kernel memory is available.
    unsigned long long snd_buf_errors_;
    // InCsumErrors: incremented when a UDP checksum failure is detected, always together with InErrors,
    // so InErrors - InCsumErrors should be the count of memory errors on the receive side.
    unsigned long long in_csum_errors_;

public:
    SrsSnmpUdpStat();
    ~SrsSnmpUdpStat();
};

// Cached for performance.
extern SrsSnmpUdpStat *srs_get_udp_snmp_stat();
void srs_update_udp_snmp_statistic();

class SrsNetworkDevices
{
public:
    bool ok_;

    // The interface name, up to 15 chars as Linux IFNAMSIZ allows, and the NUL.
    char name_[16];
    // In ms.
    int64_t sample_time_;

public:
    unsigned long long rbytes_;
    unsigned long rpackets_;
    unsigned long rerrs_;
    unsigned long rdrop_;
    unsigned long rfifo_;
    unsigned long rframe_;
    unsigned long rcompressed_;
    unsigned long rmulticast_;

    unsigned long long sbytes_;
    unsigned long spackets_;
    unsigned long serrs_;
    unsigned long sdrop_;
    unsigned long sfifo_;
    unsigned long scolls_;
    unsigned long scarrier_;
    unsigned long scompressed_;

public:
    SrsNetworkDevices();
};

// Cached for performance.
extern SrsNetworkDevices *srs_get_network_devices();
extern int srs_get_network_devices_count();
extern void srs_update_network_devices();

// The system connections, and the network summary of all SRS clients, not only RTMP.
class SrsNetworkRtmpServer
{
public:
    bool ok_;

    // In ms.
    int64_t sample_time_;

public:
    int64_t rbytes_;
    int rkbps_;
    int rkbps_30s_;
    int rkbps_5m_;

    int64_t sbytes_;
    int skbps_;
    int skbps_30s_;
    int skbps_5m_;

    // @see: /proc/net/snmp
    // @see: /proc/net/sockstat
    int nb_conn_sys_;
    int nb_conn_sys_et_; // established
    int nb_conn_sys_tw_; // time wait
    int nb_conn_sys_udp_;

    int nb_conn_srs_;

public:
    SrsNetworkRtmpServer();
};

// Cached for performance.
extern SrsNetworkRtmpServer *srs_get_network_rtmp_server();
extern void srs_update_rtmp_server(int nb_conn, SrsKbps *kbps);

extern std::string srs_get_local_ip(int fd);
extern int srs_get_local_port(int fd);
extern std::string srs_get_peer_ip(int fd);
extern int srs_get_peer_port(int fd);

extern bool srs_is_boolean(std::string str);

// Dump summaries for /api/v1/summaries.
extern void srs_api_dump_summaries(ISrsAppConfig *config, SrsJsonObject *obj);

extern std::string srs_getenv(const std::string &key);

#endif
