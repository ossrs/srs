/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2020 Winlin
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <srs_app_threads.hpp>

#include <srs_kernel_error.hpp>
#include <srs_app_config.hpp>
#include <srs_app_log.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_utility.hpp>

#include <unistd.h>

using namespace std;

#include <srs_protocol_kbps.hpp>

SrsPps* _srs_thread_sync_10us = new SrsPps();
SrsPps* _srs_thread_sync_100us = new SrsPps();
SrsPps* _srs_thread_sync_1000us = new SrsPps();
SrsPps* _srs_thread_sync_plus = new SrsPps();

extern SrsPps* _srs_pps_aloss;

uint64_t srs_covert_cpuset(cpu_set_t v)
{
#ifdef SRS_OSX
    return v;
#else
    uint64_t iv = 0;
    for (int i = 0; i <= 63; i++) {
        if (CPU_ISSET(i, &v)) {
            iv |= uint64_t(1) << i;
        }
    }
    return iv;
#endif
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

    err = srs_success;

    // Set affinity mask to include CPUs 0 to 7
    CPU_ZERO(&cpuset);
    CPU_ZERO(&cpuset2);
    cpuset_ok = false;
}

SrsThreadEntry::~SrsThreadEntry()
{
    // TODO: FIXME: Should dispose err and trd.
}

SrsThreadPool::SrsThreadPool()
{
    entry_ = NULL;
    lock_ = new SrsThreadMutex();

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

    char buf[256];
    snprintf(buf, sizeof(buf), "srs-master-%d", entry->num);
    entry->name = buf;
}

// TODO: FIMXE: If free the pool, we should stop all threads.
SrsThreadPool::~SrsThreadPool()
{
    srs_freep(lock_);
}

srs_error_t SrsThreadPool::initialize()
{
    srs_error_t err = srs_success;

    // TODO: FIXME: Should init ST for each thread.
    if ((err = srs_st_init()) != srs_success) {
        return srs_error_wrap(err, "initialize st failed");
    }

    SrsThreadEntry* entry = (SrsThreadEntry*)entry_;
#ifndef SRS_OSX
    // Load CPU affinity from config.
    int cpu_start = 0, cpu_end = 0;
    entry->cpuset_ok = _srs_config->get_threads_cpu_affinity("master", &cpu_start, &cpu_end);
    for (int i = cpu_start; entry->cpuset_ok && i <= cpu_end; i++) {
        CPU_SET(i, &entry->cpuset);
    }
#endif

    int r0 = 0, r1 = 0;
#ifndef SRS_OSX
    if (entry->cpuset_ok) {
        r0 = pthread_setaffinity_np(pthread_self(), sizeof(entry->cpuset), &entry->cpuset);
    }
    r1 = pthread_getaffinity_np(pthread_self(), sizeof(entry->cpuset2), &entry->cpuset2);
#endif

    if ((err = _srs_async_recv->initialize()) != srs_success) {
        return srs_error_wrap(err, "init async recv");
    }

    interval_ = _srs_config->get_threads_interval();
    bool async_srtp = _srs_config->get_threads_async_srtp();
    srs_trace("Thread #%d(%s): init name=%s, interval=%dms, async_srtp=%d, cpuset=%d/%d-0x%" PRIx64 "/%d-0x%" PRIx64,
        entry->num, entry->label.c_str(), entry->name.c_str(), srsu2msi(interval_), async_srtp,
        entry->cpuset_ok, r0, srs_covert_cpuset(entry->cpuset), r1, srs_covert_cpuset(entry->cpuset2));

    return err;
}

srs_error_t SrsThreadPool::execute(string label, srs_error_t (*start)(void* arg), void* arg)
{
    srs_error_t err = srs_success;

    SrsThreadEntry* entry = new SrsThreadEntry();

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

#ifndef SRS_OSX
    // Load CPU affinity from config.
    int cpu_start = 0, cpu_end = 0;
    entry->cpuset_ok = _srs_config->get_threads_cpu_affinity(label, &cpu_start, &cpu_end);
    for (int i = cpu_start; entry->cpuset_ok && i <= cpu_end; i++) {
        CPU_SET(i, &entry->cpuset);
    }
#endif

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
        // Check the threads status fastly.
        int loops = (int)(interval_ / SRS_UTIME_SECONDS);
        for (int i = 0; i < loops; i++) {
            if (true) {
                SrsThreadLocker(lock_);
                for (int i = 0; i < (int)threads_.size(); i++) {
                    SrsThreadEntry* entry = threads_.at(i);
                    if (entry->err != srs_success) {
                        err = srs_error_wrap(entry->err, "thread #%d(%s)", entry->num, entry->label.c_str());
                        return srs_error_copy(err);
                    }
                }
            }

            sleep(1);
        }

        // In normal state, gather status and log it.
        static char buf[128];
        string async_logs = _srs_async_log->description();

        string queue_desc;
        if (true) {
            snprintf(buf, sizeof(buf), ", queue=%d,%d,%d", _srs_async_recv->size(), _srs_async_srtp->size(), _srs_async_srtp->cooked_size());
            queue_desc = buf;
        }

        string sync_desc;
        _srs_thread_sync_10us->update(); _srs_thread_sync_100us->update();
        _srs_thread_sync_1000us->update(); _srs_thread_sync_plus->update();
        if (_srs_thread_sync_10us->r10s() || _srs_thread_sync_100us->r10s() || _srs_thread_sync_1000us->r10s() || _srs_thread_sync_plus->r10s()) {
            snprintf(buf, sizeof(buf), ", sync=%d,%d,%d,%d", _srs_thread_sync_10us->r10s(), _srs_thread_sync_100us->r10s(), _srs_thread_sync_1000us->r10s(), _srs_thread_sync_plus->r10s());
            sync_desc = buf;
        }

        srs_trace("Thread: %s cycle threads=%d%s%s%s", entry_->name.c_str(), (int)threads_.size(),
            async_logs.c_str(), sync_desc.c_str(), queue_desc.c_str());
    }

    return err;
}

void SrsThreadPool::stop()
{
    // TODO: FIXME: Should notify other threads to do cleanup and quit.
}

void* SrsThreadPool::start(void* arg)
{
    srs_error_t err = srs_success;

    SrsThreadEntry* entry = (SrsThreadEntry*)arg;

    int r0 = 0, r1 = 0;
#ifndef SRS_OSX
    // https://man7.org/linux/man-pages/man3/pthread_setname_np.3.html
    pthread_setname_np(pthread_self(), entry->name.c_str());
    if (entry->cpuset_ok) {
        r0 = pthread_setaffinity_np(pthread_self(), sizeof(entry->cpuset), &entry->cpuset);
    }
    r1 = pthread_getaffinity_np(pthread_self(), sizeof(entry->cpuset2), &entry->cpuset2);
#endif

    srs_trace("Thread #%d: run with label=%s, name=%s, cpuset=%d/%d-0x%" PRIx64 "/%d-0x%" PRIx64, entry->num,
        entry->label.c_str(), entry->name.c_str(), entry->cpuset_ok, r0, srs_covert_cpuset(entry->cpuset),
        r1, srs_covert_cpuset(entry->cpuset2));

    if ((err = entry->start(entry->arg)) != srs_success) {
        entry->err = err;
    }

    // We do not use the return value, the err has been set to entry->err.
    return NULL;
}

// TODO: FIXME: It should be thread-local or thread-safe.
SrsThreadPool* _srs_thread_pool = new SrsThreadPool();

SrsAsyncFileWriter::SrsAsyncFileWriter(std::string p)
{
    filename_ = p;
    writer_ = new SrsFileWriter();
    queue_ = new SrsThreadQueue<SrsSharedPtrMessage>();
}

// TODO: FIXME: Before free the writer, we must remove it from the manager.
SrsAsyncFileWriter::~SrsAsyncFileWriter()
{
    // TODO: FIXME: Should we flush dirty logs?
    srs_freep(writer_);
    srs_freep(queue_);
}

srs_error_t SrsAsyncFileWriter::open()
{
    return writer_->open(filename_);
}

srs_error_t SrsAsyncFileWriter::open_append()
{
    return writer_->open_append(filename_);
}

void SrsAsyncFileWriter::close()
{
    writer_->close();
}

srs_error_t SrsAsyncFileWriter::write(void* buf, size_t count, ssize_t* pnwrite)
{
    srs_error_t err = srs_success;

    if (count <= 0) {
        return err;
    }

    char* cp = new char[count];
    memcpy(cp, buf, count);

    SrsSharedPtrMessage* msg = new SrsSharedPtrMessage();
    msg->wrap(cp, count);

    queue_->push_back(msg);

    if (pnwrite) {
        *pnwrite = count;
    }

    return err;
}

srs_error_t SrsAsyncFileWriter::writev(const iovec* iov, int iovcnt, ssize_t* pnwrite)
{
    srs_error_t err = srs_success;

    for (int i = 0; i < iovcnt; i++) {
        const iovec* p = iov + i;

        ssize_t nn = 0;
        if ((err = write(p->iov_base, p->iov_len, &nn)) != srs_success) {
            return srs_error_wrap(err, "write %d iov %d bytes", i, p->iov_len);
        }

        if (pnwrite) {
            *pnwrite += nn;
        }
    }

    return err;
}

srs_error_t SrsAsyncFileWriter::flush()
{
    srs_error_t err = srs_success;

    // The time to wait here, is the time to wait there, because they wait for the same lock
    // at queue to push_back or swap all messages.
    srs_utime_t now = srs_update_system_time();

    vector<SrsSharedPtrMessage*> flying;
    if (true) {
        queue_->swap(flying);
    }

    // Stat the sync wait of locks.
    srs_utime_t elapsed = srs_update_system_time() - now;
    if (elapsed <= 10) {
        ++_srs_thread_sync_10us->sugar;
    } else if (elapsed <= 100) {
        ++_srs_thread_sync_100us->sugar;
    } else if (elapsed <= 1000) {
        ++_srs_thread_sync_1000us->sugar;
    } else {
        ++_srs_thread_sync_plus->sugar;
    }

    // Flush the flying messages to disk.
    for (int i = 0; i < (int)flying.size(); i++) {
        SrsSharedPtrMessage* msg = flying.at(i);

        srs_error_t r0 = writer_->write(msg->payload, msg->size, NULL);

        // Choose a random error to return.
        if (err == srs_success) {
            err = r0;
        } else {
            srs_freep(r0);
        }

        srs_freep(msg);
    }

    return err;
}

SrsAsyncLogManager::SrsAsyncLogManager()
{
    interval_ = 0;

    reopen_ = false;
    lock_ = new SrsThreadMutex();
}

// TODO: FIXME: We should stop the thread first, then free the manager.
SrsAsyncLogManager::~SrsAsyncLogManager()
{
    srs_freep(lock_);

    for (int i = 0; i < (int)writers_.size(); i++) {
        SrsAsyncFileWriter* writer = writers_.at(i);
        srs_freep(writer);
    }
}

// @remark Note that we should never write logs, because log is not ready not.
srs_error_t SrsAsyncLogManager::initialize()
{
    srs_error_t err =  srs_success;

    interval_ = _srs_config->srs_log_flush_interval();
    if (interval_ <= 0) {
        return srs_error_new(ERROR_SYSTEM_LOGFILE, "invalid interval=%dms", srsu2msi(interval_));
    }

    return err;
}

// @remark Now, log is ready, and we can print logs.
srs_error_t SrsAsyncLogManager::start(void* arg)
{
    SrsAsyncLogManager* log = (SrsAsyncLogManager*)arg;
    return log->do_start();
}

srs_error_t SrsAsyncLogManager::create_writer(std::string filename, SrsAsyncFileWriter** ppwriter)
{
    srs_error_t err = srs_success;

    SrsAsyncFileWriter* writer = new SrsAsyncFileWriter(filename);

    if (true) {
        SrsThreadLocker(lock_);
        writers_.push_back(writer);
    }

    if ((err = writer->open()) != srs_success) {
        return srs_error_wrap(err, "open file %s fail", filename.c_str());
    }

    *ppwriter = writer;
    return err;
}

void SrsAsyncLogManager::reopen()
{
    SrsThreadLocker(lock_);
    reopen_ = true;
}

std::string SrsAsyncLogManager::description()
{
    SrsThreadLocker(lock_);

    int nn_logs = 0;
    int max_logs = 0;
    for (int i = 0; i < (int)writers_.size(); i++) {
        SrsAsyncFileWriter* writer = writers_.at(i);

        int nn = (int)writer->queue_->size();
        nn_logs += nn;
        max_logs = srs_max(max_logs, nn);
    }

    static char buf[128];
    snprintf(buf, sizeof(buf), ", logs=%d/%d/%d", (int)writers_.size(), nn_logs, max_logs);

    return buf;
}

srs_error_t SrsAsyncLogManager::do_start()
{
    srs_error_t err = srs_success;

    // Never quit for this thread.
    while (true) {
        // Reopen all log files.
        if (reopen_) {
            SrsThreadLocker(lock_);
            reopen_ = false;

            for (int i = 0; i < (int)writers_.size(); i++) {
                SrsAsyncFileWriter* writer = writers_.at(i);

                writer->close();
                if ((err = writer->open()) != srs_success) {
                    srs_error_reset(err); // Ignore any error for reopen logs.
                }
            }
        }

        // Flush all logs from cache to disk.
        if (true) {
            SrsThreadLocker(lock_);

            for (int i = 0; i < (int)writers_.size(); i++) {
                SrsAsyncFileWriter* writer = writers_.at(i);

                if ((err = writer->flush()) != srs_success) {
                    srs_error_reset(err); // Ignore any error for flushing logs.
                }
            }
        }

        // We use the system primordial sleep, not the ST sleep, because
        // this is a system thread, not a coroutine.
        timespec tv = {0};
        tv.tv_sec = interval_ / SRS_UTIME_SECONDS;
        tv.tv_nsec = (interval_ % SRS_UTIME_SECONDS) * 1000;
        nanosleep(&tv, NULL);
    }

    return err;
}

// TODO: FIXME: It should be thread-local or thread-safe.
SrsAsyncLogManager* _srs_async_log = new SrsAsyncLogManager();

SrsAsyncSRTP::SrsAsyncSRTP(SrsSecurityTransport* transport)
{
    task_ = NULL;
    transport_ = transport;
}

SrsAsyncSRTP::~SrsAsyncSRTP()
{
    // TODO: FIXME: Check it carefully.
    _srs_async_srtp->remove_task(task_);
}

srs_error_t SrsAsyncSRTP::initialize(std::string recv_key, std::string send_key)
{
    srs_error_t err = srs_success;

    srs_assert(!task_);
    task_ = new SrsAsyncSRTPTask(this);
    _srs_async_srtp->register_task(task_);

    if ((err = task_->initialize(recv_key, send_key)) != srs_success) {
        return srs_error_wrap(err, "init async srtp");
    }

    // TODO: FIMXE: Remove it.
    return SrsSRTP::initialize(recv_key, send_key);
}

srs_error_t SrsAsyncSRTP::protect_rtp(void* packet, int* nb_cipher)
{
    // TODO: FIMXE: Remove it.
    return SrsSRTP::protect_rtp(packet, nb_cipher);
}

srs_error_t SrsAsyncSRTP::protect_rtcp(void* packet, int* nb_cipher)
{
    // TODO: FIMXE: Remove it.
    return SrsSRTP::protect_rtcp(packet, nb_cipher);
}

srs_error_t SrsAsyncSRTP::unprotect_rtp(void* packet, int* nb_plaintext)
{
    int nb_cipher = *nb_plaintext;
    char* buf = new char[nb_cipher];
    memcpy(buf, packet, nb_cipher);

    SrsAsyncSRTPPacket* pkt = new SrsAsyncSRTPPacket(task_);
    pkt->msg_->wrap(buf, nb_cipher);
    pkt->is_rtp_ = true;
    pkt->do_decrypt_ = true;
    _srs_async_srtp->add_packet(pkt);

    // Do the job asynchronously.
    *nb_plaintext = 0;

    return srs_success;
}

srs_error_t SrsAsyncSRTP::unprotect_rtcp(void* packet, int* nb_plaintext)
{
    // TODO: FIMXE: Remove it.
    return SrsSRTP::unprotect_rtcp(packet, nb_plaintext);
}

SrsAsyncSRTPTask::SrsAsyncSRTPTask(SrsAsyncSRTP* codec)
{
    codec_ = codec;
    impl_ = new SrsSRTP();
}

SrsAsyncSRTPTask::~SrsAsyncSRTPTask()
{
    srs_freep(impl_);
}

srs_error_t SrsAsyncSRTPTask::initialize(std::string recv_key, std::string send_key)
{
    srs_error_t err = srs_success;

    if ((err = impl_->initialize(recv_key, send_key)) != srs_success) {
        return srs_error_wrap(err, "init srtp impl");
    }

    return err;
}

srs_error_t SrsAsyncSRTPTask::cook(SrsAsyncSRTPPacket* pkt)
{
    srs_error_t err = srs_success;

    if (pkt->do_decrypt_) {
        if (pkt->is_rtp_) {
            pkt->nb_consumed_ = pkt->msg_->size;
            err = impl_->unprotect_rtp(pkt->msg_->payload, &pkt->nb_consumed_);
        }
    }
    if (err != srs_success) {
        return err;
    }

    return err;
}

SrsAsyncSRTPPacket::SrsAsyncSRTPPacket(SrsAsyncSRTPTask* task)
{
    task_ = task;
    msg_ = new SrsSharedPtrMessage();
    is_rtp_ = false;
    do_decrypt_ = false;
    nb_consumed_ = 0;
}

SrsAsyncSRTPPacket::~SrsAsyncSRTPPacket()
{
    srs_freep(msg_);
}

SrsAsyncSRTPManager::SrsAsyncSRTPManager()
{
    lock_ = new SrsThreadMutex();
    packets_ = new SrsThreadQueue<SrsAsyncSRTPPacket>();
    cooked_packets_ = new SrsThreadQueue<SrsAsyncSRTPPacket>();
}

// TODO: FIXME: We should stop the thread first, then free the manager.
SrsAsyncSRTPManager::~SrsAsyncSRTPManager()
{
    srs_freep(lock_);
    srs_freep(packets_);
    srs_freep(cooked_packets_);

    vector<SrsAsyncSRTPTask*>::iterator it;
    for (it = tasks_.begin(); it != tasks_.end(); ++it) {
        SrsAsyncSRTPTask* task = *it;
        srs_freep(task);
    }
}

void SrsAsyncSRTPManager::register_task(SrsAsyncSRTPTask* task)
{
    if (!task) {
        return;
    }

    SrsThreadLocker(lock_);
    tasks_.push_back(task);
}

void SrsAsyncSRTPManager::remove_task(SrsAsyncSRTPTask* task)
{
    if (!task) {
        return;
    }

    SrsThreadLocker(lock_);
    vector<SrsAsyncSRTPTask*>::iterator it;
    if ((it = std::find(tasks_.begin(), tasks_.end(), task)) != tasks_.end()) {
        tasks_.erase(it);
        srs_freep(task);
    }
}

// TODO: FIXME: We could use a coroutine queue, then cook all packet in RTC server timer.
void SrsAsyncSRTPManager::add_packet(SrsAsyncSRTPPacket* pkt)
{
    packets_->push_back(pkt);
}

int SrsAsyncSRTPManager::size()
{
    return packets_->size();
}
int SrsAsyncSRTPManager::cooked_size()
{
    return cooked_packets_->size();
}

srs_error_t SrsAsyncSRTPManager::start(void* arg)
{
    SrsAsyncSRTPManager* srtp = (SrsAsyncSRTPManager*)arg;
    return srtp->do_start();
}

srs_error_t SrsAsyncSRTPManager::do_start()
{
    srs_error_t err = srs_success;

    // TODO: FIXME: Config it?
    srs_utime_t interval = 10 * SRS_UTIME_MILLISECONDS;

    while (true) {
        vector<SrsAsyncSRTPPacket*> flying;
        packets_->swap(flying);

        for (int i = 0; i < (int)flying.size(); i++) {
            SrsAsyncSRTPPacket* pkt = flying.at(i);

            if ((err = pkt->task_->cook(pkt)) != srs_success) {
                srs_error_reset(err); // Ignore any error.
            }

            cooked_packets_->push_back(pkt);
        }

        // If got packets, maybe more packets in queue.
        if (!flying.empty()) {
            continue;
        }

        // TODO: FIXME: Maybe we should use cond wait?
        timespec tv = {0};
        tv.tv_sec = interval / SRS_UTIME_SECONDS;
        tv.tv_nsec = (interval % SRS_UTIME_SECONDS) * 1000;
        nanosleep(&tv, NULL);
    }

    return err;
}

srs_error_t SrsAsyncSRTPManager::consume()
{
    srs_error_t err = srs_success;

    vector<SrsAsyncSRTPPacket*> flying;
    cooked_packets_->swap(flying);

    for (int i = 0; i < (int)flying.size(); i++) {
        SrsAsyncSRTPPacket* pkt = flying.at(i);
        SrsSecurityTransport* transport = pkt->task_->codec_->transport_;
        char* payload = pkt->msg_->payload;

        if (pkt->do_decrypt_) {
            if (pkt->is_rtp_) {
                err = transport->on_rtp_plaintext(payload, pkt->nb_consumed_);
            }
        }
        if (err != srs_success) {
            srs_error_reset(err); // Ignore any error.
        }

        srs_freep(pkt);
    }

    return err;
}

SrsAsyncSRTPManager* _srs_async_srtp = new SrsAsyncSRTPManager();

SrsThreadUdpListener::SrsThreadUdpListener(srs_netfd_t fd)
{
    skt_ = new SrsUdpMuxSocket(fd);
}

SrsThreadUdpListener::~SrsThreadUdpListener()
{
}

SrsAsyncRecvManager::SrsAsyncRecvManager()
{
    lock_ = new SrsThreadMutex();
    packets_ = new SrsThreadQueue<SrsUdpMuxSocket>();
    handler_ = NULL;
    max_recv_queue_ = 0;
}

// TODO: FIXME: We should stop the thread first, then free the manager.
SrsAsyncRecvManager::~SrsAsyncRecvManager()
{
    srs_freep(lock_);
    srs_freep(packets_);

    vector<SrsThreadUdpListener*>::iterator it;
    for (it = listeners_.begin(); it != listeners_.end(); ++it) {
        SrsThreadUdpListener* listener = *it;
        srs_freep(listener);
    }
}

srs_error_t SrsAsyncRecvManager::initialize()
{
    srs_error_t err = srs_success;

    max_recv_queue_ = _srs_config->get_threads_max_recv_queue();
    srs_trace("AsyncRecv: Set max_queue=%d", max_recv_queue_);

    return err;
}

void SrsAsyncRecvManager::set_handler(ISrsUdpMuxHandler* v)
{
    handler_ = v;
}

void SrsAsyncRecvManager::add_listener(SrsThreadUdpListener* listener)
{
    SrsThreadLocker(lock_);
    listeners_.push_back(listener);
}

int SrsAsyncRecvManager::size()
{
    return packets_->size();
}

srs_error_t SrsAsyncRecvManager::start(void* arg)
{
    SrsAsyncRecvManager* recv = (SrsAsyncRecvManager*)arg;
    return recv->do_start();
}

srs_error_t SrsAsyncRecvManager::do_start()
{
    srs_error_t err = srs_success;

    // TODO: FIXME: Config it?
    srs_utime_t interval = 10 * SRS_UTIME_MILLISECONDS;

    while (true) {
        vector<SrsThreadUdpListener*> listeners;
        if (true) {
            SrsThreadLocker(lock_);
            listeners = listeners_;
        }

        bool got_packet = false;
        for (int i = 0; i < (int)listeners.size(); i++) {
            SrsThreadUdpListener* listener = listeners.at(i);

            // TODO: FIXME: Use st_recvfrom to recv if thread-safe ST is ok.
            int nread = listener->skt_->raw_recvfrom();

            // Drop packet if exceed max recv queue size.
            if ((int)packets_->size() >= max_recv_queue_) {
                ++_srs_pps_aloss->sugar;
                continue;
            }

            // If got packet, copy to the queue.
            if (nread > 0) {
                got_packet = true;
                packets_->push_back(listener->skt_->copy());
            }
        }

        // If got packets, maybe more packets in queue.
        if (got_packet) {
            continue;
        }

        // TODO: FIXME: Maybe we should use cond wait?
        timespec tv = {0};
        tv.tv_sec = interval / SRS_UTIME_SECONDS;
        tv.tv_nsec = (interval % SRS_UTIME_SECONDS) * 1000;
        nanosleep(&tv, NULL);
    }

    return err;
}

srs_error_t SrsAsyncRecvManager::consume()
{
    srs_error_t err = srs_success;

    vector<SrsUdpMuxSocket*> flying;
    packets_->swap(flying);

    for (int i = 0; i < (int)flying.size(); i++) {
        SrsUdpMuxSocket* pkt = flying.at(i);

        if (handler_ && (err = handler_->on_udp_packet(pkt)) != srs_success) {
            srs_error_reset(err); // Ignore any error.
        }

        srs_freep(pkt);
    }

    return err;
}

SrsAsyncRecvManager* _srs_async_recv = new SrsAsyncRecvManager();
