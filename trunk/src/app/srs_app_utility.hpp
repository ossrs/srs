//
// Copyright (c) 2013-2024 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_APP_UTILITY_HPP
#define SRS_APP_UTILITY_HPP

#include <srs_core.hpp>

#include <vector>
#include <string>
#include <sstream>

#include <limits.h>
#include <arpa/inet.h>
#include <sys/resource.h>

#include <srs_app_st.hpp>
#include <srs_kernel_log.hpp>
#include <srs_protocol_utility.hpp>

class SrsKbps;
class SrsBuffer;
class SrsJsonObject;

// Convert level in string to log level in int.
// @return the log level defined in SrsLogLevel.
extern SrsLogLevel srs_get_log_level(std::string level);
extern SrsLogLevel srs_get_log_level_v2(std::string level);

// Build the path according to vhost/app/stream, where replace variables:
//       [vhost], the vhost of stream.
//       [app], the app of stream.
//       [stream], the stream name of stream.
// @return the replaced path.
extern std::string srs_path_build_stream(std::string template_path, std::string vhost, std::string app, std::string stream);

// Build the path according to timestamp, where replace variables:
//       [2006], replace this const to current year.
//       [01], replace this const to current month.
//       [02], replace this const to current date.
//       [15], replace this const to current hour.
//       [04], repleace this const to current minute.
//       [05], repleace this const to current second.
//       [999], repleace this const to current millisecond.
//       [timestamp],replace this const to current UNIX timestamp in ms.
// @return the replaced path.
extern std::string srs_path_build_timestamp(std::string template_path);

// Kill the pid by SIGINT, then wait to quit,
// Kill the pid by SIGKILL again when exceed the timeout.
// @param pid the pid to kill. ignore for -1. set to -1 when killed.
// @return an int error code.
extern srs_error_t srs_kill_forced(int& pid);

// Current process resource usage.
// @see: man getrusage
class SrsRusage
{
public:
    // Whether the data is ok.
    bool ok;
    // The time in ms when sample.
    int64_t sample_time;
    
public:
    rusage r;
    
public:
    SrsRusage();
};

// Get system rusage, use cache to avoid performance problem.
extern SrsRusage* srs_get_system_rusage();
// The daemon st-thread will update it.
extern void srs_update_system_rusage();

// To stat the process info.
// @see: man 5 proc, /proc/[pid]/stat
class SrsProcSelfStat
{
public:
    // Whether the data is ok.
    bool ok;
    // The time in ms when sample.
    int64_t sample_time;
    // The percent of usage. 0.153 is 15.3%.
    float percent;
    
    // data of /proc/[pid]/stat
public:
    // pid %d      The process ID.
    int pid;
    // comm %s     The  filename  of  the  executable,  in parentheses. This is visible whether or not the executable is
    //             swapped out.
    char comm[32];
    // state %c    One character from the string "RSDZTW" where R is running, S is sleeping in an interruptible  wait,  D
    //             is  waiting in uninterruptible disk sleep, Z is zombie, T is traced or stopped (on a signal), and W is
    //             paging.
    unsigned char state;
    // ppid %d     The PID of the parent.
    int ppid;
    // pgrp %d     The process group ID of the process.
    int pgrp;
    // session %d  The session ID of the process.
    int session;
    // tty_nr %d   The controlling terminal of the process.  (The minor device number is contained in the combination  of
    //             bits 31 to 20 and 7 to 0; the major device number is in bits 15 t0 8.)
    int tty_nr;
    // tpgid %d    The ID of the foreground process group of the controlling terminal of the process.
    int tpgid;
    // flags %u (%lu before Linux 2.6.22)
    //             The  kernel  flags  word  of  the process.  For bit meanings, see the PF_* defines in <linux/sched.h>.
    //             Details depend on the kernel version.
    unsigned int flags;
    // minflt %lu  The number of minor faults the process has made which have not required loading  a  memory  page  from
    //             disk.
    unsigned long minflt;
    // cminflt %lu The number of minor faults that the process's waited-for children have made.
    unsigned long cminflt;
    // majflt %lu  The number of major faults the process has made which have required loading a memory page from disk.
    unsigned long majflt;
    // cmajflt %lu The number of major faults that the process's waited-for children have made.
    unsigned long cmajflt;
    // utime %lu   Amount  of  time that this process has been scheduled in user mode, measured in clock ticks (divide by
    //             sysconf(_SC_CLK_TCK).  This includes guest time, guest_time (time spent running  a  virtual  CPU,  see
    //             below),  so  that  applications  that are not aware of the guest time field do not lose that time from
    //             their calculations.
    unsigned long utime;
    // stime %lu   Amount of time that this process has been scheduled in kernel mode, measured in clock ticks (divide by
    //             sysconf(_SC_CLK_TCK).
    unsigned long stime;
    // cutime %ld  Amount  of  time that this process's waited-for children have been scheduled in user mode, measured in
    //             clock ticks (divide  by  sysconf(_SC_CLK_TCK).   (See  also  times(2).)   This  includes  guest  time,
    //             cguest_time (time spent running a virtual CPU, see below).
    long cutime;
    // cstime %ld  Amount of time that this process's waited-for children have been scheduled in kernel mode, measured in
    //             clock ticks (divide by sysconf(_SC_CLK_TCK).
    long cstime;
    // priority %ld
    //          (Explanation for Linux 2.6) For processes running a real-time scheduling  policy  (policy  below;  see
    //          sched_setscheduler(2)),  this  is the negated scheduling priority, minus one; that is, a number in the
    //          range -2 to -100, corresponding to real-time priorities 1 to 99.  For processes running under  a  non-
    //          real-time scheduling policy, this is the raw nice value (setpriority(2)) as represented in the kernel.
    //          The kernel stores nice values as numbers in the range 0 (high) to 39 (low), corresponding to the user-
    //          visible nice range of -20 to 19.
    //
    //          Before Linux 2.6, this was a scaled value based on the scheduler weighting given to this process.
    long priority;
    // nice %ld    The nice value (see setpriority(2)), a value in the range 19 (low priority) to -20 (high priority).
    long nice;
    // num_threads %ld
    //          Number  of threads in this process (since Linux 2.6).  Before kernel 2.6, this field was hard coded to
    //          0 as a placeholder for an earlier removed field.
    long num_threads;
    // itrealvalue %ld
    //          The time in jiffies before the next SIGALRM is sent to the process due to an  interval  timer.   Since
    //          kernel 2.6.17, this field is no longer maintained, and is hard coded as 0.
    long itrealvalue;
    // starttime %llu (was %lu before Linux 2.6)
    //          The time in jiffies the process started after system boot.
    long long starttime;
    // vsize %lu   Virtual memory size in bytes.
    unsigned long vsize;
    // rss %ld     Resident Set Size: number of pages the process has in real memory.  This is just the pages which count
    //             towards text, data, or stack space.  This does not include pages which have not been demand-loaded in,
    //             or which are swapped out.
    long rss;
    // rsslim %lu  Current  soft limit in bytes on the rss of the process; see the description of RLIMIT_RSS in getprior-
    //             ity(2).
    unsigned long rsslim;
    // startcode %lu
    //             The address above which program text can run.
    unsigned long startcode;
    // endcode %lu The address below which program text can run.
    unsigned long endcode;
    // startstack %lu
    //             The address of the start (i.e., bottom) of the stack.
    unsigned long startstack;
    // kstkesp %lu The current value of ESP (stack pointer), as found in the kernel stack page for the process.
    unsigned long kstkesp;
    // kstkeip %lu The current EIP (instruction pointer).
    unsigned long kstkeip;
    // signal %lu  The bitmap of pending signals, displayed as a decimal number.  Obsolete, because it does  not  provide
    //             information on real-time signals; use /proc/[pid]/status instead.
    unsigned long signal;
    // blocked %lu The  bitmap  of blocked signals, displayed as a decimal number.  Obsolete, because it does not provide
    //             information on real-time signals; use /proc/[pid]/status instead.
    unsigned long blocked;
    // sigignore %lu
    //             The bitmap of ignored signals, displayed as a decimal number.  Obsolete, because it does  not  provide
    //             information on real-time signals; use /proc/[pid]/status instead.
    unsigned long sigignore;
    // sigcatch %lu
    //             The  bitmap  of  caught signals, displayed as a decimal number.  Obsolete, because it does not provide
    //             information on real-time signals; use /proc/[pid]/status instead.
    unsigned long sigcatch;
    // wchan %lu   This is the "channel" in which the process is waiting.  It is the address of a system call, and can be
    //             looked  up in a namelist if you need a textual name.  (If you have an up-to-date /etc/psdatabase, then
    //             try ps -l to see the WCHAN field in action.)
    unsigned long wchan;
    // nswap %lu   Number of pages swapped (not maintained).
    unsigned long nswap;
    // cnswap %lu  Cumulative nswap for child processes (not maintained).
    unsigned long cnswap;
    // exit_signal %d (since Linux 2.1.22)
    //             Signal to be sent to parent when we die.
    int exit_signal;
    // processor %d (since Linux 2.2.8)
    //             CPU number last executed on.
    int processor;
    // rt_priority %u (since Linux 2.5.19; was %lu before Linux 2.6.22)
    //             Real-time scheduling priority, a number in the range 1 to 99 for processes scheduled under a real-time
    //             policy, or 0, for non-real-time processes (see sched_setscheduler(2)).
    unsigned int rt_priority;
    // policy %u (since Linux 2.5.19; was %lu before Linux 2.6.22)
    //             Scheduling policy (see sched_setscheduler(2)).  Decode using the SCHED_* constants in linux/sched.h.
    unsigned int policy;
    // delayacct_blkio_ticks %llu (since Linux 2.6.18)
    //             Aggregated block I/O delays, measured in clock ticks (centiseconds).
    unsigned long long delayacct_blkio_ticks;
    // guest_time %lu (since Linux 2.6.24)
    //             Guest time of the process (time spent running a virtual CPU for a guest operating system), measured in
    //             clock ticks (divide by sysconf(_SC_CLK_TCK).
    unsigned long guest_time;
    // cguest_time %ld (since Linux 2.6.24)
    //             Guest time of the process's children, measured in clock ticks (divide by sysconf(_SC_CLK_TCK).
    long cguest_time;
    
public:
    SrsProcSelfStat();
};

// To stat the cpu time.
// @see: man 5 proc, /proc/stat
// about the cpu time, @see: http://stackoverflow.com/questions/16011677/calculating-cpu-usage-using-proc-files
// for example, for ossrs.net, a single cpu machine:
//       [winlin@SRS ~]$ cat /proc/uptime && cat /proc/stat
//           5275153.01 4699624.99
//           cpu  43506750 973 8545744 466133337 4149365 190852 804666 0 0
// Where the uptime is 5275153.01s
// generally, USER_HZ sysconf(_SC_CLK_TCK)=100, which means the unit of /proc/stat is "1/100ths seconds"
//       that is, USER_HZ=1/100 seconds
// cpu total = 43506750+973+8545744+466133337+4149365+190852+804666+0+0 (USER_HZ)
//           = 523331687 (USER_HZ)
//           = 523331687 * 1/100 (seconds)
//           = 5233316.87 seconds
// The cpu total seconds almost the uptime, the delta is more precise.
//
// we run the command about 26minutes:
//       [winlin@SRS ~]$ cat /proc/uptime && cat /proc/stat
//           5276739.83 4701090.76
//           cpu  43514105 973 8548948 466278556 4150480 190899 804937 0 0
// Where the uptime is 5276739.83s
// cpu total = 43514105+973+8548948+466278556+4150480+190899+804937+0+0 (USER_HZ)
//           = 523488898 (USER_HZ)
//           = 523488898 * 1/100 (seconds)
//           = 5234888.98 seconds
// where:
//       uptime delta = 1586.82s
//       cpu total delta = 1572.11s
// The deviation is more smaller.
class SrsProcSystemStat
{
public:
    // Whether the data is ok.
    bool ok;
    // The time in ms when sample.
    int64_t sample_time;
    // The percent of usage. 0.153 is 15.3%.
    // The percent is in [0, 1], where 1 is 100%.
    // for multiple core cpu, max also is 100%.
    float percent;
    // The total cpu time units
    // @remark, zero for the previous total() is zero.
    //          the usaged_cpu_delta = total_delta * percent
    //          previous cpu total = this->total() - total_delta
    int64_t total_delta;
    
    // data of /proc/stat
public:
    // The amount of time, measured in units  of  USER_HZ
    // (1/100ths  of  a  second  on  most  architectures,  use
    // sysconf(_SC_CLK_TCK)  to  obtain  the  right value)
    //
    // The system spent in user mode,
    unsigned long long user;
    // user mode with low priority (nice),
    unsigned long long nice;
    // system mode,
    unsigned long long sys;
    // and the idle task, respectively.
    unsigned long long idle;
    
    // In  Linux 2.6 this line includes three additional columns:
    //
    // iowait - time waiting for I/O to complete (since 2.5.41);
    unsigned long long iowait;
    // irq - time servicing interrupts (since 2.6.0-test4);
    unsigned long long irq;
    // softirq  -  time  servicing  softirqs  (since 2.6.0-test4).
    unsigned long long softirq;
    
    // Since  Linux 2.6.11, there is an eighth column,
    // steal - stolen time, which is the time spent in other oper-
    // ating systems when running in a virtualized environment
    unsigned long long steal;
    
    // Since Linux 2.6.24, there is a ninth column,
    // guest, which is the time spent running a virtual CPU for guest
    // operating systems under the control of the Linux kernel.
    unsigned long long guest;
    
public:
    SrsProcSystemStat();
    
    // Get total cpu units.
    int64_t total();
};

// Get system cpu stat, use cache to avoid performance problem.
extern SrsProcSelfStat* srs_get_self_proc_stat();
// Get system cpu stat, use cache to avoid performance problem.
extern SrsProcSystemStat* srs_get_system_proc_stat();
// The daemon st-thread will update it.
extern void srs_update_proc_stat();

// Stat disk iops
// @see: http://stackoverflow.com/questions/4458183/how-the-util-of-iostat-is-computed
// for total disk io, @see: cat /proc/vmstat |grep pgpg
// for device disk io, @see: cat /proc/diskstats
// @remark, user can use command to test the disk io:
//      time dd if=/dev/zero bs=1M count=2048 of=file_2G
// @remark, the iotop is right, the dstat result seems not ok,
//      while the iostat only show the number of writes, not the bytes,
//      where the dd command will give the write MBps, it's absolutely right.
class SrsDiskStat
{
public:
    // Whether the data is ok.
    bool ok;
    // The time in ms when sample.
    int64_t sample_time;
    
    // input(read) KBytes per seconds
    int in_KBps;
    // output(write) KBytes per seconds
    int out_KBps;
    
    // @see: print_partition_stats() of iostat.c
    // but its value is [0, +], for instance, 0.1532 means 15.32%.
    float busy;
    // for stat the busy%
    SrsProcSystemStat cpu;
    
public:
    // @see: cat /proc/vmstat
    // The in(read) page count, pgpgin*1024 is the read bytes.
    // Total number of kilobytes the system paged in from disk per second.
    unsigned long pgpgin;
    // The out(write) page count, pgpgout*1024 is the write bytes.
    // Total number of kilobytes the system paged out to disk per second.
    unsigned long pgpgout;
    
    // @see: https://www.kernel.org/doc/Documentation/iostats.txt
    // @see: http://tester-higkoo.googlecode.com/svn-history/r14/trunk/Tools/iostat/iostat.c
    // @see: cat /proc/diskstats
    //
    // Number of issued reads.
    // This is the total number of reads completed successfully.
    // Read I/O operations
    unsigned int rd_ios;
    // Number of reads merged
    // Reads merged
    unsigned int rd_merges;
    // Number of sectors read.
    // This is the total number of sectors read successfully.
    // Sectors read
    unsigned long long rd_sectors;
    // Number of milliseconds spent reading.
    // This is the total number of milliseconds spent by all reads
    // (as measured from make_request() to end_that_request_last()).
    // Time in queue + service for read
    unsigned int rd_ticks;
    //
    // Number of writes completed.
    // This is the total number of writes completed successfully
    // Write I/O operations
    unsigned int wr_ios;
    // Number of writes merged Reads and writes which are adjacent
    // To each other may be merged for efficiency. Thus two 4K
    // reads may become one 8K read before it is ultimately
    // handed to the disk, and so it will be counted (and queued)
    // as only one I/O. This field lets you know how often this was done.
    // Writes merged
    unsigned int wr_merges;
    // Number of sectors written.
    // This is the total number of sectors written successfully.
    // Sectors written
    unsigned long long wr_sectors;
    // Number of milliseconds spent writing .
    // This is the total number of milliseconds spent by all writes
    // (as measured from make_request() to end_that_request_last()).
    // Time in queue + service for write
    unsigned int wr_ticks;
    //
    // Number of I/Os currently in progress.
    // The only field that should go to zero.
    // Incremented as requests are given to appropriate request_queue_t
    // and decremented as they finish.
    unsigned int nb_current;
    // Number of milliseconds spent doing I/Os.
    // This field is increased so long as field 9 is nonzero.
    // Time of requests in queue
    unsigned int ticks;
    // Number of milliseconds spent doing I/Os.
    // This field is incremented at each I/O start, I/O completion,
    // I/O merge, or read of these stats by the number of I/Os in
    // progress (field 9) times the number of milliseconds spent
    // doing I/O since the last update of this field. This can
    // provide an easy measure of both I/O completion time and
    // The backlog that may be accumulating.
    // Average queue length
    unsigned int aveq;
    
public:
    SrsDiskStat();
};

// Get disk stat, use cache to avoid performance problem.
extern SrsDiskStat* srs_get_disk_stat();
// The daemon st-thread will update it.
extern void srs_update_disk_stat();

// Stat system memory info
// @see: cat /proc/meminfo
class SrsMemInfo
{
public:
    // Whether the data is ok.
    bool ok;
    // The time in ms when sample.
    int64_t sample_time;
    // The percent of usage. 0.153 is 15.3%.
    float percent_ram;
    float percent_swap;
    
    // data of /proc/meminfo
public:
    // MemActive = MemTotal - MemFree
    uint64_t MemActive;
    // RealInUse = MemActive - Buffers - Cached
    uint64_t RealInUse;
    // NotInUse = MemTotal - RealInUse
    //          = MemTotal - MemActive + Buffers + Cached
    //          = MemTotal - MemTotal + MemFree + Buffers + Cached
    //          = MemFree + Buffers + Cached
    uint64_t NotInUse;
    
    unsigned long MemTotal;
    unsigned long MemFree;
    unsigned long Buffers;
    unsigned long Cached;
    unsigned long SwapTotal;
    unsigned long SwapFree;
    
public:
    SrsMemInfo();
};

// Get system meminfo, use cache to avoid performance problem.
extern SrsMemInfo* srs_get_meminfo();
// The daemon st-thread will update it.
extern void srs_update_meminfo();

// system cpu hardware info.
// @see: cat /proc/cpuinfo
// @remark, we use sysconf(_SC_NPROCESSORS_CONF) to get the cpu count.
class SrsCpuInfo
{
public:
    // Whether the data is ok.
    bool ok;
    
    // data of /proc/cpuinfo
public:
    // The number of processors configured.
    int nb_processors;
    // The number of processors currently online (available).
    int nb_processors_online;
    
public:
    SrsCpuInfo();
};

// Get system cpu info, use cache to avoid performance problem.
extern SrsCpuInfo* srs_get_cpuinfo();

// The platform(os, srs) uptime/load summary
class SrsPlatformInfo
{
public:
    // Whether the data is ok.
    bool ok;
    
    // srs startup time, in ms.
    int64_t srs_startup_time;
    
public:
    // @see: cat /proc/uptime
    // system startup time in seconds.
    double os_uptime;
    // system all cpu idle time in seconds.
    // @remark to cal the cpu ustime percent:
    //      os_ilde_time % (os_uptime * SrsCpuInfo.nb_processors_online)
    double os_ilde_time;
    
    // @see: cat /proc/loadavg
    double load_one_minutes;
    double load_five_minutes;
    double load_fifteen_minutes;
    
public:
    SrsPlatformInfo();
};

// Get platform info, use cache to avoid performance problem.
extern SrsPlatformInfo* srs_get_platform_info();
// The daemon st-thread will update it.
extern void srs_update_platform_info();

class SrsSnmpUdpStat
{
public:
    // Whether the data is ok.
    bool ok;
    // send and recv buffer error delta
    int rcv_buf_errors_delta;
    int snd_buf_errors_delta;

public:
    // @see: cat /proc/uptimecat /proc/net/snmp|grep 'Udp:'
    // @see: https://blog.packagecloud.io/eng/2017/02/06/monitoring-tuning-linux-networking-stack-sending-data/#procnetsnmp
    // InDatagrams: incremented when recvmsg was used by a userland program to read datagram.
    // also incremented when a UDP packet is encapsulated and sent back for processing.
    unsigned long long in_datagrams;
    // NoPorts: incremented when UDP packets arrive destined for a port where no program is listening.
    unsigned long long no_ports;
    // InErrors: incremented in several cases: no memory in the receive queue, when a bad checksum is seen,
    // and if sk_add_backlog fails to add the datagram.
    unsigned long long in_errors;
    // OutDatagrams: incremented when a UDP packet is handed down without error to the IP protocol layer to be sent.
    unsigned long long out_datagrams;
    // RcvbufErrors: incremented when sock_queue_rcv_skb reports that no memory is available;
    // this happens if sk->sk_rmem_alloc is greater than or equal to sk->sk_rcvbuf.
    unsigned long long rcv_buf_errors;
    // SndbufErrors: incremented if the IP protocol layer reported an error when trying to send the packet
    // and no error queue has been setup. also incremented if no send queue space or kernel memory are available.
    unsigned long long snd_buf_errors;
    // InCsumErrors: incremented when a UDP checksum failure is detected.
    // Note that in all cases I could find, InCsumErrors is incremented at the same time as InErrors.
    // Thus, InErrors - InCsumErros should yield the count of memory related errors on the receive side.
    unsigned long long in_csum_errors;
public:
    SrsSnmpUdpStat();
    ~SrsSnmpUdpStat();
};

// Get SNMP udp statistic, use cache to avoid performance problem.
extern SrsSnmpUdpStat* srs_get_udp_snmp_stat();
// The daemon st-thread will update it.
void srs_update_udp_snmp_statistic();

// The network device summary for each network device, for example, eth0, eth1, ethN
class SrsNetworkDevices
{
public:
    // Whether the network device is ok.
    bool ok;
    
    // 6-chars interfaces name
    char name[7];
    // The sample time in ms.
    int64_t sample_time;
    
public:
    // data for receive.
    unsigned long long rbytes;
    unsigned long rpackets;
    unsigned long rerrs;
    unsigned long rdrop;
    unsigned long rfifo;
    unsigned long rframe;
    unsigned long rcompressed;
    unsigned long rmulticast;
    
    // data for transmit
    unsigned long long sbytes;
    unsigned long spackets;
    unsigned long serrs;
    unsigned long sdrop;
    unsigned long sfifo;
    unsigned long scolls;
    unsigned long scarrier;
    unsigned long scompressed;
    
public:
    SrsNetworkDevices();
};

// Get network devices info, use cache to avoid performance problem.
extern SrsNetworkDevices* srs_get_network_devices();
extern int srs_get_network_devices_count();
// The daemon st-thread will update it.
extern void srs_update_network_devices();

// The system connections, and srs rtmp network summary
class SrsNetworkRtmpServer
{
public:
    // Whether the network device is ok.
    bool ok;
    
    // The sample time in ms.
    int64_t sample_time;
    
public:
    // data for receive.
    int64_t rbytes;
    int rkbps;
    int rkbps_30s;
    int rkbps_5m;
    
    // data for transmit
    int64_t sbytes;
    int skbps;
    int skbps_30s;
    int skbps_5m;
    
    // connections
    // @see: /proc/net/snmp
    // @see: /proc/net/sockstat
    int nb_conn_sys;
    int nb_conn_sys_et; // established
    int nb_conn_sys_tw; // time wait
    int nb_conn_sys_udp; // udp
    
    // retrieve from srs interface
    int nb_conn_srs;
    
public:
    SrsNetworkRtmpServer();
};

// Get network devices info, use cache to avoid performance problem.
extern SrsNetworkRtmpServer* srs_get_network_rtmp_server();
// The daemon st-thread will update it.
extern void srs_update_rtmp_server(int nb_conn, SrsKbps* kbps);

// Get local or peer ip.
// Where local ip is the server ip which client connected.
extern std::string srs_get_local_ip(int fd);
// Get the local id port.
extern int srs_get_local_port(int fd);
// Where peer ip is the client public ip which connected to server.
extern std::string srs_get_peer_ip(int fd);
extern int srs_get_peer_port(int fd);

// Whether string is boolean
//      is_bool("true") == true
//      is_bool("false") == true
//      otherwise, false.
extern bool srs_is_boolean(std::string str);

// Dump summaries for /api/v1/summaries.
extern void srs_api_dump_summaries(SrsJsonObject* obj);

// Get ENV variable, which may starts with $.
//      srs_getenv("EIP") is srs_getenv("$EIP")
extern std::string srs_getenv(const std::string& key);

#endif

