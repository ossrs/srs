//
// Copyright (c) 2013-2024 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_app_st.hpp>

#include <string>
using namespace std;

#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_app_utility.hpp>
#include <srs_app_log.hpp>

ISrsCoroutineHandler::ISrsCoroutineHandler()
{
}

ISrsCoroutineHandler::~ISrsCoroutineHandler()
{
}

ISrsStartable::ISrsStartable()
{
}

ISrsStartable::~ISrsStartable()
{
}

ISrsInterruptable::ISrsInterruptable()
{
}

ISrsInterruptable::~ISrsInterruptable()
{
}

ISrsContextIdSetter::ISrsContextIdSetter()
{
}

ISrsContextIdSetter::~ISrsContextIdSetter()
{
}

ISrsContextIdGetter::ISrsContextIdGetter()
{
}

ISrsContextIdGetter::~ISrsContextIdGetter()
{
}

SrsCoroutine::SrsCoroutine()
{
}

SrsCoroutine::~SrsCoroutine()
{
}

SrsDummyCoroutine::SrsDummyCoroutine()
{
}

SrsDummyCoroutine::~SrsDummyCoroutine()
{
}

srs_error_t SrsDummyCoroutine::start()
{
    return srs_error_new(ERROR_THREAD_DUMMY, "dummy coroutine");
}

void SrsDummyCoroutine::stop()
{
}

void SrsDummyCoroutine::interrupt()
{
}

srs_error_t SrsDummyCoroutine::pull()
{
    return srs_error_new(ERROR_THREAD_DUMMY, "dummy pull");
}

const SrsContextId& SrsDummyCoroutine::cid()
{
    return cid_;
}

void SrsDummyCoroutine::set_cid(const SrsContextId& cid)
{
    cid_ = cid;
}

SrsSTCoroutine::SrsSTCoroutine(string n, ISrsCoroutineHandler* h)
{
    impl_ = new SrsFastCoroutine(n, h);
}

SrsSTCoroutine::SrsSTCoroutine(string n, ISrsCoroutineHandler* h, SrsContextId cid)
{
    impl_ = new SrsFastCoroutine(n, h, cid);
}

SrsSTCoroutine::~SrsSTCoroutine()
{
    srs_freep(impl_);
}

void SrsSTCoroutine::set_stack_size(int v)
{
    impl_->set_stack_size(v);
}

srs_error_t SrsSTCoroutine::start()
{
    return impl_->start();
}

void SrsSTCoroutine::stop()
{
    impl_->stop();
}

void SrsSTCoroutine::interrupt()
{
    impl_->interrupt();
}

srs_error_t SrsSTCoroutine::pull()
{
    return impl_->pull();
}

const SrsContextId& SrsSTCoroutine::cid()
{
    return impl_->cid();
}

void SrsSTCoroutine::set_cid(const SrsContextId& cid)
{
    impl_->set_cid(cid);
}

SrsFastCoroutine::SrsFastCoroutine(string n, ISrsCoroutineHandler* h)
{
    // TODO: FIXME: Reduce duplicated code.
    name = n;
    handler = h;
    trd = NULL;
    trd_err = srs_success;
    started = interrupted = disposed = cycle_done = false;
    stopping_ = false;

    //  0 use default, default is 64K.
    stack_size = 0;
}

SrsFastCoroutine::SrsFastCoroutine(string n, ISrsCoroutineHandler* h, SrsContextId cid)
{
    name = n;
    handler = h;
    cid_ = cid;
    trd = NULL;
    trd_err = srs_success;
    started = interrupted = disposed = cycle_done = false;
    stopping_ = false;

    //  0 use default, default is 64K.
    stack_size = 0;
}

SrsFastCoroutine::~SrsFastCoroutine()
{
    stop();

    // TODO: FIXME: We must assert the cycle is done.
    
    srs_freep(trd_err);
}

void SrsFastCoroutine::set_stack_size(int v)
{
    stack_size = v;
}

srs_error_t SrsFastCoroutine::start()
{
    srs_error_t err = srs_success;
    
    if (started || disposed) {
        if (disposed) {
            err = srs_error_new(ERROR_THREAD_DISPOSED, "disposed");
        } else {
            err = srs_error_new(ERROR_THREAD_STARTED, "started");
        }

        if (trd_err == srs_success) {
            trd_err = srs_error_copy(err);
        }
        
        return err;
    }

    if ((trd = (srs_thread_t)_pfn_st_thread_create(pfn, this, 1, stack_size)) == NULL) {
        err = srs_error_new(ERROR_ST_CREATE_CYCLE_THREAD, "create failed");
        
        srs_freep(trd_err);
        trd_err = srs_error_copy(err);
        
        return err;
    }
    
    started = true;

    return err;
}

void SrsFastCoroutine::stop()
{
    if (disposed) {
        if (stopping_) {
            srs_error("thread is stopping by %s", stopping_cid_.c_str());
            srs_assert(!stopping_);
        }
        return;
    }
    disposed = true;
    stopping_ = true;
    
    interrupt();

    // When not started, the trd is NULL.
    if (trd) {
        void* res = NULL;
        int r0 = srs_thread_join(trd, &res);
        if (r0) {
            // By st_thread_join
            if (errno == EINVAL) srs_assert(!r0);
            if (errno == EDEADLK) srs_assert(!r0);
            // By st_cond_timedwait
            if (errno == EINTR) srs_assert(!r0);
            if (errno == ETIME) srs_assert(!r0);
            // Others
            srs_assert(!r0);
        }

        srs_error_t err_res = (srs_error_t)res;
        if (err_res != srs_success) {
            // When worker cycle done, the error has already been overrided,
            // so the trd_err should be equal to err_res.
            srs_assert(trd_err == err_res);
        }
    }
    
    // If there's no error occur from worker, try to set to terminated error.
    if (trd_err == srs_success && !cycle_done) {
        trd_err = srs_error_new(ERROR_THREAD_TERMINATED, "terminated");
    }

    // Now, we'are stopped.
    stopping_ = false;
    
    return;
}

void SrsFastCoroutine::interrupt()
{
    if (!started || interrupted || cycle_done) {
        return;
    }
    interrupted = true;
    
    if (trd_err == srs_success) {
        trd_err = srs_error_new(ERROR_THREAD_INTERRUPED, "interrupted");
    }

    // Note that if another thread is stopping thread and waiting in st_thread_join,
    // the interrupt will make the st_thread_join fail.
    srs_thread_interrupt(trd);
}

const SrsContextId& SrsFastCoroutine::cid()
{
    return cid_;
}

void SrsFastCoroutine::set_cid(const SrsContextId& cid)
{
    cid_ = cid;
    srs_context_set_cid_of(trd, cid);
}

srs_error_t SrsFastCoroutine::cycle()
{
    if (_srs_context) {
        if (cid_.empty()) {
            cid_ = _srs_context->generate_id();
        }
        _srs_context->set_id(cid_);
    }
    
    srs_error_t err = handler->cycle();
    if (err != srs_success) {
        return srs_error_wrap(err, "coroutine cycle");
    }

    // Set cycle done, no need to interrupt it.
    cycle_done = true;
    
    return err;
}

void* SrsFastCoroutine::pfn(void* arg)
{
    SrsFastCoroutine* p = (SrsFastCoroutine*)arg;

    srs_error_t err = p->cycle();

    // Set the err for function pull to fetch it.
    // @see https://github.com/ossrs/srs/pull/1304#issuecomment-480484151
    if (err != srs_success) {
        srs_freep(p->trd_err);
        // It's ok to directly use it, because it's returned by st_thread_join.
        p->trd_err = err;
    }

    return (void*)err;
}

SrsWaitGroup::SrsWaitGroup()
{
    nn_ = 0;
    done_ = srs_cond_new();
}

SrsWaitGroup::~SrsWaitGroup()
{
    // In the destructor, we should NOT wait for all coroutines to be done, because user should decide
    // to wait or not. Similar to the Go's sync.WaitGroup, it also requires user to wait explicitly. For
    // some special use scenarios, such as error handling, for example, if we started three servers with
    // wait group, and one of them failed, user may want to return error and quit directly, without wait
    // for other running servers to be done. If we wait in the destructor, it will continue to run without
    // some servers, in unknown behaviors.
    srs_cond_destroy(done_);
}

void SrsWaitGroup::add(int n)
{
    nn_ += n;
}

void SrsWaitGroup::done()
{
    nn_--;
    if (nn_ <= 0) {
        srs_cond_signal(done_);
    }
}

void SrsWaitGroup::wait()
{
    if (nn_ > 0) {
        srs_cond_wait(done_);
    }
}

ISrsExecutorHandler::ISrsExecutorHandler()
{
}

ISrsExecutorHandler::~ISrsExecutorHandler()
{
}

SrsExecutorCoroutine::SrsExecutorCoroutine(ISrsResourceManager* m, ISrsResource* r, ISrsCoroutineHandler* h, ISrsExecutorHandler* cb)
{
    resource_ = r;
    handler_ = h;
    manager_ = m;
    callback_ = cb;
    trd_ = new SrsSTCoroutine("ar", this, resource_->get_id());
}

SrsExecutorCoroutine::~SrsExecutorCoroutine()
{
    manager_->remove(resource_);
    srs_freep(trd_);
}

srs_error_t SrsExecutorCoroutine::start()
{
    return trd_->start();
}

void SrsExecutorCoroutine::interrupt()
{
    trd_->interrupt();
}

srs_error_t SrsExecutorCoroutine::pull()
{
    return trd_->pull();
}

const SrsContextId& SrsExecutorCoroutine::cid()
{
    return trd_->cid();
}

void SrsExecutorCoroutine::set_cid(const SrsContextId& cid)
{
    trd_->set_cid(cid);
}

srs_error_t SrsExecutorCoroutine::cycle()
{
    srs_error_t err = handler_->cycle();
    if (callback_) callback_->on_executor_done(this);
    manager_->remove(this);
    return err;
}

const SrsContextId& SrsExecutorCoroutine::get_id()
{
    return resource_->get_id();
}

std::string SrsExecutorCoroutine::desc()
{
    return resource_->desc();
}

