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

#ifndef SRS_KERNEL_UTILITY_HPP
#define SRS_KERNEL_UTILITY_HPP

/*
#include <srs_kernel_utility.hpp>
*/

#include <srs_core.hpp>

#include <sys/resource.h>

// get current system time in ms, use cache to avoid performance problem
extern int64_t srs_get_system_time_ms();
// the deamon st-thread will update it.
extern void srs_update_system_time_ms();

// @see: man getrusage
struct SrsRusage
{
    bool ok;
    rusage r;
    
    SrsRusage();
};

// get system rusage, use cache to avoid performance problem.
extern SrsRusage* srs_get_system_rusage();
// the deamon st-thread will update it.
extern void srs_update_system_rusage();

// @see: man 5 proc, /proc/[pid]/stat
struct SrsCpuSelfStat
{
    // whether the data is ok.
    bool ok;
    
    // pid %d      The process ID.
    int pid;
    // comm %s     The  filename  of  the  executable,  in parentheses. This is visible whether or not the executable is
    //             swapped out.
    char comm[32];
    // state %c    One character from the string "RSDZTW" where R is running, S is sleeping in an interruptible  wait,  D
    //             is  waiting in uninterruptible disk sleep, Z is zombie, T is traced or stopped (on a signal), and W is
    //             paging.
    char state;
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
    // cminflt %lu The number of minor faults that the process’s waited-for children have made.
    unsigned long cminflt;
    // majflt %lu  The number of major faults the process has made which have required loading a memory page from disk.
    unsigned long majflt;
    // cmajflt %lu The number of major faults that the process’s waited-for children have made.
    unsigned long cmajflt;
    // utime %lu   Amount  of  time that this process has been scheduled in user mode, measured in clock ticks (divide by
    //             sysconf(_SC_CLK_TCK).  This includes guest time, guest_time (time spent running  a  virtual  CPU,  see
    //             below),  so  that  applications  that are not aware of the guest time field do not lose that time from
    //             their calculations.
    unsigned long utime;
    // stime %lu   Amount of time that this process has been scheduled in kernel mode, measured in clock ticks (divide by
    //             sysconf(_SC_CLK_TCK).
    unsigned long stime;
    // cutime %ld  Amount  of  time that this process’s waited-for children have been scheduled in user mode, measured in
    //             clock ticks (divide  by  sysconf(_SC_CLK_TCK).   (See  also  times(2).)   This  includes  guest  time,
    //             cguest_time (time spent running a virtual CPU, see below).
    long cutime;
    // cstime %ld  Amount of time that this process’s waited-for children have been scheduled in kernel mode, measured in
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
    //             Guest time of the process’s children, measured in clock ticks (divide by sysconf(_SC_CLK_TCK).
    long cguest_time;
    
    SrsCpuSelfStat();
};

// @see: man 5 proc, /proc/stat
struct SrsCpuSystemStat
{
    // whether the data is ok.
    bool ok;
    
    // always be cpu
    char label[32];
    
    //The amount of time, measured in units  of  USER_HZ  (1/100ths  of  a  second  on  most  architectures,  use
    // sysconf(_SC_CLK_TCK)  to  obtain  the  right value)
    //
    // the system spent in user mode, 
    unsigned long user;
    // user mode with low priority (nice), 
    unsigned long nice;
    // system mode, 
    unsigned long sys;
    // and the idle task, respectively.
    unsigned long idle;

    // In  Linux 2.6 this line includes three additional columns:
    //
    // iowait - time waiting for I/O to complete (since 2.5.41);
    unsigned long iowait;
    // irq - time servicing interrupts (since 2.6.0-test4); 
    unsigned long irq;
    // softirq  -  time  servicing  softirqs  (since 2.6.0-test4).
    unsigned long softirq;
    
    // Since  Linux 2.6.11, there is an eighth column,
    // steal - stolen time, which is the time spent in other oper-
    // ating systems when running in a virtualized environment
    unsigned long steal;
    
    // Since Linux 2.6.24, there is a ninth column, 
    // guest, which is the time spent running a virtual CPU for guest
    // operating systems under the control of the Linux kernel.
    unsigned long guest;

    SrsCpuSystemStat();
};

// get system cpu stat, use cache to avoid performance problem.
extern SrsCpuSelfStat* srs_get_self_cpu_stat();
// get system cpu stat, use cache to avoid performance problem.
extern SrsCpuSystemStat* srs_get_system_cpu_stat();
// the deamon st-thread will update it.
extern void srs_update_system_cpu_stat();

#endif
