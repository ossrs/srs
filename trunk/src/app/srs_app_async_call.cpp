//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_app_async_call.hpp>

using namespace std;

#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>

ISrsAsyncCallTask::ISrsAsyncCallTask()
{
}

ISrsAsyncCallTask::~ISrsAsyncCallTask()
{
}

ISrsAsyncCallWorker::ISrsAsyncCallWorker()
{
}

ISrsAsyncCallWorker::~ISrsAsyncCallWorker()
{
}

SrsAsyncCallWorker::SrsAsyncCallWorker()
{
    trd_ = new SrsDummyCoroutine();
    wait_ = srs_cond_new();
    lock_ = srs_mutex_new();
}

SrsAsyncCallWorker::~SrsAsyncCallWorker()
{
    srs_freep(trd_);

    std::vector<ISrsAsyncCallTask *>::iterator it;
    for (it = tasks_.begin(); it != tasks_.end(); ++it) {
        ISrsAsyncCallTask *task = *it;
        srs_freep(task);
    }
    tasks_.clear();

    srs_cond_destroy(wait_);
    srs_mutex_destroy(lock_);
}

srs_error_t SrsAsyncCallWorker::execute(ISrsAsyncCallTask *t)
{
    srs_error_t err = srs_success;

    tasks_.push_back(t);
    srs_cond_signal(wait_);

    return err;
}

int SrsAsyncCallWorker::count()
{
    return (int)tasks_.size();
}

srs_error_t SrsAsyncCallWorker::start()
{
    srs_error_t err = srs_success;

    srs_freep(trd_);
    trd_ = new SrsSTCoroutine("async", this, _srs_context->get_id());

    if ((err = trd_->start()) != srs_success) {
        return srs_error_wrap(err, "coroutine");
    }

    return err;
}

void SrsAsyncCallWorker::stop()
{
    flush_tasks();
    srs_cond_signal(wait_);
    trd_->stop();
}

srs_error_t SrsAsyncCallWorker::cycle()
{
    srs_error_t err = srs_success;

    while (true) {
        if ((err = trd_->pull()) != srs_success) {
            return srs_error_wrap(err, "async call worker");
        }

        if (tasks_.empty()) {
            srs_cond_wait(wait_);
        }

        flush_tasks();
    }

    return err;
}

void SrsAsyncCallWorker::flush_tasks()
{
    srs_error_t err = srs_success;

    // Avoid the async call blocking other coroutines.
    std::vector<ISrsAsyncCallTask *> copy;
    if (true) {
        SrsLocker(&lock_);

        if (tasks_.empty()) {
            return;
        }

        copy = tasks_;
        tasks_.clear();
    }

    std::vector<ISrsAsyncCallTask *>::iterator it;
    for (it = copy.begin(); it != copy.end(); ++it) {
        ISrsAsyncCallTask *task = *it;

        if ((err = task->call()) != srs_success) {
            srs_warn("ignore task failed %s", srs_error_desc(err).c_str());
            srs_freep(err);
        }
        srs_freep(task);
    }
}
