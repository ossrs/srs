//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#include <srs_app_threads.hpp>

#include <srs_app_config.hpp>
#include <srs_app_hybrid.hpp>
#include <srs_app_utility.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_app_rtc_source.hpp>
#include <srs_app_source.hpp>
#include <srs_app_pithy_print.hpp>
#include <srs_app_rtc_server.hpp>
#include <srs_app_log.hpp>
#include <srs_app_async_call.hpp>
#include <srs_app_tencentcloud.hpp>
#include <srs_app_conn.hpp>
#ifdef SRS_RTC
#include <srs_app_rtc_dtls.hpp>
#include <srs_app_rtc_conn.hpp>
#endif
#ifdef SRS_SRT
#include <srs_app_srt_source.hpp>
#endif
#ifdef SRS_GB28181
#include <srs_app_gb28181.hpp>
#endif

#include <stdlib.h>
#include <string>
using namespace std;

#include <unistd.h>
#include <fcntl.h>

#if defined(SRS_OSX) || defined(SRS_CYGWIN64)
    pid_t gettid() {
        return 0;
    }
#else
    #if __GLIBC__ == 2 && __GLIBC_MINOR__ < 30
        #include <sys/syscall.h>
        #define gettid() syscall(SYS_gettid)
    #endif
#endif

// These functions first appeared in glibc in version 2.12.
// See https://man7.org/linux/man-pages/man3/pthread_setname_np.3.html
#if defined(SRS_CYGWIN64) || (defined(SRS_CROSSBUILD) && ((__GLIBC__ < 2) || (__GLIBC__ == 2 && __GLIBC_MINOR__ < 12)))
    void pthread_setname_np(pthread_t trd, const char* name) {
    }
#endif

extern ISrsLog* _srs_log;
extern ISrsContext* _srs_context;
extern SrsConfig* _srs_config;

extern SrsStageManager* _srs_stages;

#ifdef SRS_RTC
extern SrsRtcBlackhole* _srs_blackhole;
extern SrsResourceManager* _srs_rtc_manager;

extern SrsDtlsCertificate* _srs_rtc_dtls_certificate;
#endif

#include <srs_protocol_kbps.hpp>

SrsPps* _srs_pps_aloss2 = NULL;

extern SrsPps* _srs_pps_ids;
extern SrsPps* _srs_pps_fids;
extern SrsPps* _srs_pps_fids_level0;
extern SrsPps* _srs_pps_dispose;

extern SrsPps* _srs_pps_timer;

extern SrsPps* _srs_pps_snack;
extern SrsPps* _srs_pps_snack2;
extern SrsPps* _srs_pps_snack3;
extern SrsPps* _srs_pps_snack4;
extern SrsPps* _srs_pps_sanack;
extern SrsPps* _srs_pps_svnack;

extern SrsPps* _srs_pps_rnack;
extern SrsPps* _srs_pps_rnack2;
extern SrsPps* _srs_pps_rhnack;
extern SrsPps* _srs_pps_rmnack;

#if defined(SRS_DEBUG) && defined(SRS_DEBUG_STATS)
extern SrsPps* _srs_pps_recvfrom;
extern SrsPps* _srs_pps_recvfrom_eagain;
extern SrsPps* _srs_pps_sendto;
extern SrsPps* _srs_pps_sendto_eagain;

extern SrsPps* _srs_pps_read;
extern SrsPps* _srs_pps_read_eagain;
extern SrsPps* _srs_pps_readv;
extern SrsPps* _srs_pps_readv_eagain;
extern SrsPps* _srs_pps_writev;
extern SrsPps* _srs_pps_writev_eagain;

extern SrsPps* _srs_pps_recvmsg;
extern SrsPps* _srs_pps_recvmsg_eagain;
extern SrsPps* _srs_pps_sendmsg;
extern SrsPps* _srs_pps_sendmsg_eagain;

extern SrsPps* _srs_pps_epoll;
extern SrsPps* _srs_pps_epoll_zero;
extern SrsPps* _srs_pps_epoll_shake;
extern SrsPps* _srs_pps_epoll_spin;

extern SrsPps* _srs_pps_sched_15ms;
extern SrsPps* _srs_pps_sched_20ms;
extern SrsPps* _srs_pps_sched_25ms;
extern SrsPps* _srs_pps_sched_30ms;
extern SrsPps* _srs_pps_sched_35ms;
extern SrsPps* _srs_pps_sched_40ms;
extern SrsPps* _srs_pps_sched_80ms;
extern SrsPps* _srs_pps_sched_160ms;
extern SrsPps* _srs_pps_sched_s;
#endif

extern SrsPps* _srs_pps_clock_15ms;
extern SrsPps* _srs_pps_clock_20ms;
extern SrsPps* _srs_pps_clock_25ms;
extern SrsPps* _srs_pps_clock_30ms;
extern SrsPps* _srs_pps_clock_35ms;
extern SrsPps* _srs_pps_clock_40ms;
extern SrsPps* _srs_pps_clock_80ms;
extern SrsPps* _srs_pps_clock_160ms;
extern SrsPps* _srs_pps_timer_s;

#if defined(SRS_DEBUG) && defined(SRS_DEBUG_STATS)
extern SrsPps* _srs_pps_thread_run;
extern SrsPps* _srs_pps_thread_idle;
extern SrsPps* _srs_pps_thread_yield;
extern SrsPps* _srs_pps_thread_yield2;
#endif

extern SrsPps* _srs_pps_rpkts;
extern SrsPps* _srs_pps_addrs;
extern SrsPps* _srs_pps_fast_addrs;

extern SrsPps* _srs_pps_spkts;

extern SrsPps* _srs_pps_sstuns;
extern SrsPps* _srs_pps_srtcps;
extern SrsPps* _srs_pps_srtps;

extern SrsPps* _srs_pps_pli;
extern SrsPps* _srs_pps_twcc;
extern SrsPps* _srs_pps_rr;
extern SrsPps* _srs_pps_pub;
extern SrsPps* _srs_pps_conn;

extern SrsPps* _srs_pps_rstuns;
extern SrsPps* _srs_pps_rrtps;
extern SrsPps* _srs_pps_rrtcps;

extern SrsPps* _srs_pps_aloss2;

extern SrsPps* _srs_pps_cids_get;
extern SrsPps* _srs_pps_cids_set;

extern SrsPps* _srs_pps_objs_msgs;

extern SrsPps* _srs_pps_objs_rtps;
extern SrsPps* _srs_pps_objs_rraw;
extern SrsPps* _srs_pps_objs_rfua;
extern SrsPps* _srs_pps_objs_rbuf;
extern SrsPps* _srs_pps_objs_rothers;

SrsCircuitBreaker::SrsCircuitBreaker()
{
    enabled_ = false;
    high_threshold_ = 0;
    high_pulse_ = 0;
    critical_threshold_ = 0;
    critical_pulse_ = 0;
    dying_threshold_ = 0;
    dying_pulse_ = 0;

    hybrid_high_water_level_ = 0;
    hybrid_critical_water_level_ = 0;
    hybrid_dying_water_level_ = 0;
}

SrsCircuitBreaker::~SrsCircuitBreaker()
{
}

srs_error_t SrsCircuitBreaker::initialize()
{
    srs_error_t err = srs_success;

    enabled_ = _srs_config->get_circuit_breaker();
    high_threshold_ = _srs_config->get_high_threshold();
    high_pulse_ = _srs_config->get_high_pulse();
    critical_threshold_ = _srs_config->get_critical_threshold();
    critical_pulse_ = _srs_config->get_critical_pulse();
    dying_threshold_ = _srs_config->get_dying_threshold();
    dying_pulse_ = _srs_config->get_dying_pulse();

    // Update the water level for circuit breaker.
    // @see SrsCircuitBreaker::on_timer()
    _srs_hybrid->timer1s()->subscribe(this);

    srs_trace("CircuitBreaker: enabled=%d, high=%dx%d, critical=%dx%d, dying=%dx%d", enabled_,
        high_pulse_, high_threshold_, critical_pulse_, critical_threshold_,
        dying_pulse_, dying_threshold_);

    return err;
}

bool SrsCircuitBreaker::hybrid_high_water_level()
{
    return enabled_ && (hybrid_critical_water_level() || hybrid_high_water_level_);
}

bool SrsCircuitBreaker::hybrid_critical_water_level()
{
    return enabled_ && (hybrid_dying_water_level() || hybrid_critical_water_level_);
}

bool SrsCircuitBreaker::hybrid_dying_water_level()
{
    return enabled_ && dying_pulse_ && hybrid_dying_water_level_ >= dying_pulse_;
}

srs_error_t SrsCircuitBreaker::on_timer(srs_utime_t interval)
{
    srs_error_t err = srs_success;

    // Update the CPU usage.
    srs_update_proc_stat();
    SrsProcSelfStat* stat = srs_get_self_proc_stat();

    // Reset the high water-level when CPU is low for N times.
    if (stat->percent * 100 > high_threshold_) {
        hybrid_high_water_level_ = high_pulse_;
    } else if (hybrid_high_water_level_ > 0) {
        hybrid_high_water_level_--;
    }

    // Reset the critical water-level when CPU is low for N times.
    if (stat->percent * 100 > critical_threshold_) {
        hybrid_critical_water_level_ = critical_pulse_;
    } else if (hybrid_critical_water_level_ > 0) {
        hybrid_critical_water_level_--;
    }

    // Reset the dying water-level when CPU is low for N times.
    if (stat->percent * 100 > dying_threshold_) {
        hybrid_dying_water_level_ = srs_min(dying_pulse_ + 1, hybrid_dying_water_level_ + 1);
    } else if (hybrid_dying_water_level_ > 0) {
        hybrid_dying_water_level_ = 0;
    }

    // Show statistics for RTC server.
    SrsProcSelfStat* u = srs_get_self_proc_stat();
    // Resident Set Size: number of pages the process has in real memory.
    int memory = (int)(u->rss * 4 / 1024);

    // The hybrid thread cpu and memory.
    float thread_percent = stat->percent * 100;

    static char buf[128];

    string snk_desc;
#ifdef SRS_RTC
    if (_srs_pps_snack2->r10s()) {
        snprintf(buf, sizeof(buf), ", snk=%d,%d,%d",
            _srs_pps_snack2->r10s(), _srs_pps_snack3->r10s(), _srs_pps_snack4->r10s() // NACK packet,seqs sent.
        );
        snk_desc = buf;
    }
#endif

    if (enabled_ && (hybrid_high_water_level() || hybrid_critical_water_level())) {
        srs_trace("CircuitBreaker: cpu=%.2f%%,%dMB, break=%d,%d,%d, cond=%.2f%%%s",
            u->percent * 100, memory,
            hybrid_high_water_level(), hybrid_critical_water_level(), hybrid_dying_water_level(), // Whether Circuit-Break is enable.
            thread_percent, // The conditions to enable Circuit-Breaker.
            snk_desc.c_str()
        );
    }

    return err;
}

SrsCircuitBreaker* _srs_circuit_breaker = NULL;
SrsAsyncCallWorker* _srs_dvr_async = NULL;

extern srs_error_t _srs_reload_err;
extern SrsReloadState _srs_reload_state;
extern std::string _srs_reload_id;

srs_error_t srs_global_initialize()
{
    srs_error_t err = srs_success;

    // Root global objects.
    _srs_log = new SrsFileLog();
    _srs_context = new SrsThreadContext();
    _srs_config = new SrsConfig();

    // The clock wall object.
    _srs_clock = new SrsWallClock();

    // The pps cids depends by st init.
    _srs_pps_cids_get = new SrsPps();
    _srs_pps_cids_set = new SrsPps();

    // The global objects which depends on ST.
    _srs_hybrid = new SrsHybridServer();
    _srs_sources = new SrsLiveSourceManager();
    _srs_stages = new SrsStageManager();
    _srs_circuit_breaker = new SrsCircuitBreaker();

#ifdef SRS_SRT
    _srs_srt_sources = new SrsSrtSourceManager();
#endif

#ifdef SRS_RTC
    _srs_rtc_sources = new SrsRtcSourceManager();
    _srs_blackhole = new SrsRtcBlackhole();

    _srs_rtc_manager = new SrsResourceManager("RTC", true);
    _srs_rtc_dtls_certificate = new SrsDtlsCertificate();
#endif
#ifdef SRS_GB28181
    _srs_gb_manager = new SrsResourceManager("GB", true);
#endif
    _srs_gc = new SrsLazySweepGc();

    // Initialize global pps, which depends on _srs_clock
    _srs_pps_ids = new SrsPps();
    _srs_pps_fids = new SrsPps();
    _srs_pps_fids_level0 = new SrsPps();
    _srs_pps_dispose = new SrsPps();

    _srs_pps_timer = new SrsPps();
    _srs_pps_conn = new SrsPps();
    _srs_pps_pub = new SrsPps();

#ifdef SRS_RTC
    _srs_pps_snack = new SrsPps();
    _srs_pps_snack2 = new SrsPps();
    _srs_pps_snack3 = new SrsPps();
    _srs_pps_snack4 = new SrsPps();
    _srs_pps_sanack = new SrsPps();
    _srs_pps_svnack = new SrsPps();

    _srs_pps_rnack = new SrsPps();
    _srs_pps_rnack2 = new SrsPps();
    _srs_pps_rhnack = new SrsPps();
    _srs_pps_rmnack = new SrsPps();
#endif

#if defined(SRS_DEBUG) && defined(SRS_DEBUG_STATS)
    _srs_pps_recvfrom = new SrsPps();
    _srs_pps_recvfrom_eagain = new SrsPps();
    _srs_pps_sendto = new SrsPps();
    _srs_pps_sendto_eagain = new SrsPps();

    _srs_pps_read = new SrsPps();
    _srs_pps_read_eagain = new SrsPps();
    _srs_pps_readv = new SrsPps();
    _srs_pps_readv_eagain = new SrsPps();
    _srs_pps_writev = new SrsPps();
    _srs_pps_writev_eagain = new SrsPps();

    _srs_pps_recvmsg = new SrsPps();
    _srs_pps_recvmsg_eagain = new SrsPps();
    _srs_pps_sendmsg = new SrsPps();
    _srs_pps_sendmsg_eagain = new SrsPps();

    _srs_pps_epoll = new SrsPps();
    _srs_pps_epoll_zero = new SrsPps();
    _srs_pps_epoll_shake = new SrsPps();
    _srs_pps_epoll_spin = new SrsPps();

    _srs_pps_sched_15ms = new SrsPps();
    _srs_pps_sched_20ms = new SrsPps();
    _srs_pps_sched_25ms = new SrsPps();
    _srs_pps_sched_30ms = new SrsPps();
    _srs_pps_sched_35ms = new SrsPps();
    _srs_pps_sched_40ms = new SrsPps();
    _srs_pps_sched_80ms = new SrsPps();
    _srs_pps_sched_160ms = new SrsPps();
    _srs_pps_sched_s = new SrsPps();
#endif

    _srs_pps_clock_15ms = new SrsPps();
    _srs_pps_clock_20ms = new SrsPps();
    _srs_pps_clock_25ms = new SrsPps();
    _srs_pps_clock_30ms = new SrsPps();
    _srs_pps_clock_35ms = new SrsPps();
    _srs_pps_clock_40ms = new SrsPps();
    _srs_pps_clock_80ms = new SrsPps();
    _srs_pps_clock_160ms = new SrsPps();
    _srs_pps_timer_s = new SrsPps();

#if defined(SRS_DEBUG) && defined(SRS_DEBUG_STATS)
    _srs_pps_thread_run = new SrsPps();
    _srs_pps_thread_idle = new SrsPps();
    _srs_pps_thread_yield = new SrsPps();
    _srs_pps_thread_yield2 = new SrsPps();
#endif

    _srs_pps_rpkts = new SrsPps();
    _srs_pps_addrs = new SrsPps();
    _srs_pps_fast_addrs = new SrsPps();

    _srs_pps_spkts = new SrsPps();
    _srs_pps_objs_msgs = new SrsPps();

#ifdef SRS_RTC
    _srs_pps_sstuns = new SrsPps();
    _srs_pps_srtcps = new SrsPps();
    _srs_pps_srtps = new SrsPps();

    _srs_pps_rstuns = new SrsPps();
    _srs_pps_rrtps = new SrsPps();
    _srs_pps_rrtcps = new SrsPps();

    _srs_pps_aloss2 = new SrsPps();

    _srs_pps_pli = new SrsPps();
    _srs_pps_twcc = new SrsPps();
    _srs_pps_rr = new SrsPps();

    _srs_pps_objs_rtps = new SrsPps();
    _srs_pps_objs_rraw = new SrsPps();
    _srs_pps_objs_rfua = new SrsPps();
    _srs_pps_objs_rbuf = new SrsPps();
    _srs_pps_objs_rothers = new SrsPps();
#endif

    // Create global async worker for DVR.
    _srs_dvr_async = new SrsAsyncCallWorker();

#ifdef SRS_APM
    // Initialize global TencentCloud CLS object.
    _srs_cls = new SrsClsClient();
    _srs_apm = new SrsApmClient();
#endif

    _srs_reload_err = srs_success;
    _srs_reload_state = SrsReloadStateInit;
    _srs_reload_id = srs_random_str(7);

    return err;
}

SrsThreadMutex::SrsThreadMutex()
{
    // https://man7.org/linux/man-pages/man3/pthread_mutexattr_init.3.html
    int r0 = pthread_mutexattr_init(&attr_);
    srs_assert(!r0);

    // https://man7.org/linux/man-pages/man3/pthread_mutexattr_gettype.3p.html
    r0 = pthread_mutexattr_settype(&attr_, PTHREAD_MUTEX_ERRORCHECK);
    srs_assert(!r0);

    // https://michaelkerrisk.com/linux/man-pages/man3/pthread_mutex_init.3p.html
    r0 = pthread_mutex_init(&lock_, &attr_);
    srs_assert(!r0);
}

SrsThreadMutex::~SrsThreadMutex()
{
    int r0 = pthread_mutex_destroy(&lock_);
    srs_assert(!r0);

    r0 = pthread_mutexattr_destroy(&attr_);
    srs_assert(!r0);
}

void SrsThreadMutex::lock()
{
    // https://man7.org/linux/man-pages/man3/pthread_mutex_lock.3p.html
    //        EDEADLK
    //                 The mutex type is PTHREAD_MUTEX_ERRORCHECK and the current
    //                 thread already owns the mutex.
    int r0 = pthread_mutex_lock(&lock_);
    srs_assert(!r0);
}

void SrsThreadMutex::unlock()
{
    int r0 = pthread_mutex_unlock(&lock_);
    srs_assert(!r0);
}

SrsThreadEntry::SrsThreadEntry()
{
    pool = NULL;
    start = NULL;
    arg = NULL;
    num = 0;
    tid = 0;

    err = srs_success;
}

SrsThreadEntry::~SrsThreadEntry()
{
    srs_freep(err);

    // TODO: FIXME: Should dispose trd.
}

SrsThreadPool::SrsThreadPool()
{
    entry_ = NULL;
    lock_ = new SrsThreadMutex();
    hybrid_ = NULL;

    // Add primordial thread, current thread itself.
    SrsThreadEntry* entry = new SrsThreadEntry();
    threads_.push_back(entry);
    entry_ = entry;

    entry->pool = this;
    entry->label = "primordial";
    entry->start = NULL;
    entry->arg = NULL;
    entry->num = 1;
    entry->trd = pthread_self();
    entry->tid = gettid();

    char buf[256];
    snprintf(buf, sizeof(buf), "srs-master-%d", entry->num);
    entry->name = buf;

    pid_fd = -1;
}

// TODO: FIMXE: If free the pool, we should stop all threads.
SrsThreadPool::~SrsThreadPool()
{
    srs_freep(lock_);

    if (pid_fd > 0) {
        ::close(pid_fd);
        pid_fd = -1;
    }
}

// Setup the thread-local variables, MUST call when each thread starting.
srs_error_t SrsThreadPool::setup_thread_locals()
{
    srs_error_t err = srs_success;

    // Initialize ST, which depends on pps cids.
    if ((err = srs_st_init()) != srs_success) {
        return srs_error_wrap(err, "initialize st failed");
    }

    return err;
}

srs_error_t SrsThreadPool::initialize()
{
    srs_error_t err = srs_success;

    if ((err = acquire_pid_file()) != srs_success) {
        return srs_error_wrap(err, "acquire pid file");
    }

    // Initialize the master primordial thread.
    SrsThreadEntry* entry = (SrsThreadEntry*)entry_;

    interval_ = _srs_config->get_threads_interval();

    srs_trace("Thread #%d(%s): init name=%s, interval=%dms", entry->num, entry->label.c_str(), entry->name.c_str(), srsu2msi(interval_));

    return err;
}

srs_error_t SrsThreadPool::acquire_pid_file()
{
    std::string pid_file = _srs_config->get_pid_file();

    // -rw-r--r--
    // 644
    int mode = S_IRUSR | S_IWUSR |  S_IRGRP | S_IROTH;

    int fd;
    // open pid file
    if ((fd = ::open(pid_file.c_str(), O_WRONLY | O_CREAT, mode)) == -1) {
        return srs_error_new(ERROR_SYSTEM_PID_ACQUIRE, "open pid file=%s", pid_file.c_str());
    }

    // require write lock
    struct flock lock;

    lock.l_type = F_WRLCK; // F_RDLCK, F_WRLCK, F_UNLCK
    lock.l_start = 0; // type offset, relative to l_whence
    lock.l_whence = SEEK_SET;  // SEEK_SET, SEEK_CUR, SEEK_END
    lock.l_len = 0;

    if (fcntl(fd, F_SETLK, &lock) == -1) {
        if(errno == EACCES || errno == EAGAIN) {
            ::close(fd);
            srs_error("srs is already running!");
            return srs_error_new(ERROR_SYSTEM_PID_ALREADY_RUNNING, "srs is already running");
        }
        return srs_error_new(ERROR_SYSTEM_PID_LOCK, "access to pid=%s", pid_file.c_str());
    }

    // truncate file
    if (ftruncate(fd, 0) != 0) {
        return srs_error_new(ERROR_SYSTEM_PID_TRUNCATE_FILE, "truncate pid file=%s", pid_file.c_str());
    }

    // write the pid
    string pid = srs_int2str(getpid());
    if (write(fd, pid.c_str(), pid.length()) != (int)pid.length()) {
        return srs_error_new(ERROR_SYSTEM_PID_WRITE_FILE, "write pid=%s to file=%s", pid.c_str(), pid_file.c_str());
    }

    // auto close when fork child process.
    int val;
    if ((val = fcntl(fd, F_GETFD, 0)) < 0) {
        return srs_error_new(ERROR_SYSTEM_PID_GET_FILE_INFO, "fcntl fd=%d", fd);
    }
    val |= FD_CLOEXEC;
    if (fcntl(fd, F_SETFD, val) < 0) {
        return srs_error_new(ERROR_SYSTEM_PID_SET_FILE_INFO, "lock file=%s fd=%d", pid_file.c_str(), fd);
    }

    srs_trace("write pid=%s to %s success!", pid.c_str(), pid_file.c_str());
    pid_fd = fd;

    return srs_success;
}

srs_error_t SrsThreadPool::execute(string label, srs_error_t (*start)(void* arg), void* arg)
{
    srs_error_t err = srs_success;

    SrsThreadEntry* entry = new SrsThreadEntry();

    // Update the hybrid thread entry for circuit breaker.
    if (label == "hybrid") {
        hybrid_ = entry;
        hybrids_.push_back(entry);
    }

    // To protect the threads_ for executing thread-safe.
    if (true) {
        SrsThreadLocker(lock_);
        threads_.push_back(entry);
    }

    entry->pool = this;
    entry->label = label;
    entry->start = start;
    entry->arg = arg;

    // The id of thread, should equal to the debugger thread id.
    // For gdb, it's: info threads
    // For lldb, it's: thread list
    static int num = entry_->num + 1;
    entry->num = num++;

    char buf[256];
    snprintf(buf, sizeof(buf), "srs-%s-%d", entry->label.c_str(), entry->num);
    entry->name = buf;

    // https://man7.org/linux/man-pages/man3/pthread_create.3.html
    pthread_t trd;
    int r0 = pthread_create(&trd, NULL, SrsThreadPool::start, entry);
    if (r0 != 0) {
        entry->err = srs_error_new(ERROR_THREAD_CREATE, "create thread %s, r0=%d", label.c_str(), r0);
        return srs_error_copy(entry->err);
    }

    entry->trd = trd;

    return err;
}

srs_error_t SrsThreadPool::run()
{
    srs_error_t err = srs_success;

    while (true) {
        vector<SrsThreadEntry*> threads;
        if (true) {
            SrsThreadLocker(lock_);
            threads = threads_;
        }

        // Check the threads status fastly.
        int loops = (int)(interval_ / SRS_UTIME_SECONDS);
        for (int i = 0; i < loops; i++) {
            for (int j = 0; j < (int)threads.size(); j++) {
                SrsThreadEntry* entry = threads.at(j);
                if (entry->err != srs_success) {
                    // Quit with success.
                    if (srs_error_code(entry->err) == ERROR_THREAD_FINISHED) {
                        srs_trace("quit for thread #%d(%s) finished", entry->num, entry->label.c_str());
                        srs_freep(err);
                        return srs_success;
                    }

                    // Quit with specified error.
                    err = srs_error_copy(entry->err);
                    err = srs_error_wrap(err, "thread #%d(%s)", entry->num, entry->label.c_str());
                    return err;
                }
            }

            srs_usleep(1 * SRS_UTIME_SECONDS);
        }

        // Show statistics for RTC server.
        SrsProcSelfStat* u = srs_get_self_proc_stat();
        // Resident Set Size: number of pages the process has in real memory.
        int memory = (int)(u->rss * 4 / 1024);

        srs_trace("Process: cpu=%.2f%%,%dMB, threads=%d", u->percent * 100, memory, (int)threads_.size());
    }

    return err;
}

void SrsThreadPool::stop()
{
    // TODO: FIXME: Should notify other threads to do cleanup and quit.
}

SrsThreadEntry* SrsThreadPool::self()
{
    std::vector<SrsThreadEntry*> threads;

    if (true) {
        SrsThreadLocker(lock_);
        threads = threads_;
    }

    for (int i = 0; i < (int)threads.size(); i++) {
        SrsThreadEntry* entry = threads.at(i);
        if (entry->trd == pthread_self()) {
            return entry;
        }
    }

    return NULL;
}

SrsThreadEntry* SrsThreadPool::hybrid()
{
    return hybrid_;
}

vector<SrsThreadEntry*> SrsThreadPool::hybrids()
{
    return hybrids_;
}

void* SrsThreadPool::start(void* arg)
{
    srs_error_t err = srs_success;

    SrsThreadEntry* entry = (SrsThreadEntry*)arg;

    // Initialize thread-local variables.
    if ((err = SrsThreadPool::setup_thread_locals()) != srs_success) {
        entry->err = err;
        return NULL;
    }

    // Set the thread local fields.
    entry->tid = gettid();

#ifndef SRS_OSX
    // https://man7.org/linux/man-pages/man3/pthread_setname_np.3.html
    pthread_setname_np(pthread_self(), entry->name.c_str());
#else
    pthread_setname_np(entry->name.c_str());
#endif

    srs_trace("Thread #%d: run with tid=%d, entry=%p, label=%s, name=%s", entry->num, (int)entry->tid, entry, entry->label.c_str(), entry->name.c_str());

    if ((err = entry->start(entry->arg)) != srs_success) {
        entry->err = err;
    }

    // We use a special error to indicates the normally done.
    if (entry->err == srs_success) {
        entry->err = srs_error_new(ERROR_THREAD_FINISHED, "finished normally");
    }

    // We do not use the return value, the err has been set to entry->err.
    return NULL;
}

// It MUST be thread-safe, global and shared object.
SrsThreadPool* _srs_thread_pool = new SrsThreadPool();

