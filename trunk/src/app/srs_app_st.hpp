//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_APP_ST_HPP
#define SRS_APP_ST_HPP

#include <srs_core.hpp>

#include <string>

#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_kernel_st.hpp>
#include <srs_protocol_conn.hpp>
#include <srs_protocol_io.hpp>
#include <srs_protocol_st.hpp>

class SrsFastCoroutine;
class SrsExecutorCoroutine;

// An empty coroutine, user can default to this object before create any real coroutine.
// @see https://github.com/ossrs/srs/pull/908
class SrsDummyCoroutine : public ISrsCoroutine
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    SrsContextId cid_;

public:
    SrsDummyCoroutine();
    virtual ~SrsDummyCoroutine();

public:
    virtual srs_error_t start();
    virtual void stop();
    virtual void interrupt();
    virtual srs_error_t pull();
    virtual const SrsContextId &cid();
    virtual void set_cid(const SrsContextId &cid);
};

// A ST-coroutine is a lightweight thread, just like the goroutine.
// But the goroutine maybe run on different thread, while ST-coroutine only
// run in single thread, because it use setjmp and longjmp, so it may cause
// problem in multiple threads. For SRS, we only use single thread module,
// like NGINX to get very high performance, with asynchronous and non-blocking
// sockets.
// @reamrk For multiple processes, please use go-oryx to fork many SRS processes.
//      Please read https://github.com/ossrs/go-oryx
// @remark For debugging of ST-coroutine, read _st_iterate_threads_flag of ST/README
//      https://github.com/ossrs/state-threads/blob/st-1.9/README#L115
// @remark We always create joinable thread, so we must join it or memory leak,
//      Please read https://github.com/ossrs/srs/issues/78
class SrsSTCoroutine : public ISrsCoroutine
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    SrsFastCoroutine *impl_;

public:
    // Create a thread with name n and handler h.
    // @remark User can specify a cid for thread to use, or we will allocate a new one.
    SrsSTCoroutine(std::string n, ISrsCoroutineHandler *h);
    SrsSTCoroutine(std::string n, ISrsCoroutineHandler *h, SrsContextId cid);
    virtual ~SrsSTCoroutine();

public:
    // Set the stack size of coroutine, default to 0(64KB).
    void set_stack_size(int v);

public:
    // Start the thread.
    // @remark Should never start it when stopped or terminated.
    virtual srs_error_t start();
    // Interrupt the thread then wait to terminated.
    // @remark If user want to notify thread to quit async, for example if there are
    //      many threads to stop like the encoder, use the interrupt to notify all threads
    //      to terminate then use stop to wait for each to terminate.
    virtual void stop();
    // Interrupt the thread and notify it to terminate, it will be wakeup if it's blocked
    // in some IO operations, such as st_read or st_write, then it will found should quit,
    // finally the thread should terminated normally, user can use the stop to join it.
    virtual void interrupt();
    // Check whether thread is terminated normally or error(stopped or termianted with error),
    // and the thread should be running if it return ERROR_SUCCESS.
    // @remark Return specified error when thread terminated normally with error.
    // @remark Return ERROR_THREAD_TERMINATED when thread terminated normally without error.
    // @remark Return ERROR_THREAD_INTERRUPED when thread is interrupted.
    virtual srs_error_t pull();
    // Get and set the context id of thread.
    virtual const SrsContextId &cid();
    virtual void set_cid(const SrsContextId &cid);
};

// High performance coroutine.
class SrsFastCoroutine : public ISrsCoroutine
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    std::string name_;
    int stack_size_;
    ISrsCoroutineHandler *handler_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srs_thread_t trd_;
    SrsContextId cid_;
    srs_error_t trd_err_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    bool started_;
    bool interrupted_;
    bool disposed_;
    // Cycle done, no need to interrupt it.
    bool cycle_done_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // Sub state in disposed, we need to wait for thread to quit.
    bool stopping_;
    SrsContextId stopping_cid_;

public:
    SrsFastCoroutine(std::string n, ISrsCoroutineHandler *h);
    SrsFastCoroutine(std::string n, ISrsCoroutineHandler *h, SrsContextId cid);
    virtual ~SrsFastCoroutine();

public:
    void set_stack_size(int v);

public:
    srs_error_t start();
    void stop();
    void interrupt();
    inline srs_error_t pull()
    {
        if (trd_err_ == srs_success) {
            return srs_success;
        }
        return srs_error_copy(trd_err_);
    }
    const SrsContextId &cid();
    virtual void set_cid(const SrsContextId &cid);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srs_error_t cycle();
    static void *pfn(void *arg);
};

// Like goroutine sync.WaitGroup.
class SrsWaitGroup
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    int nn_;
    srs_cond_t done_;

public:
    SrsWaitGroup();
    virtual ~SrsWaitGroup();

public:
    // When start for n coroutines.
    void add(int n);
    // When coroutine is done.
    void done();
    // Wait for all coroutine to be done.
    void wait();
};

// The callback when executor cycle done.
class ISrsExecutorHandler
{
public:
    ISrsExecutorHandler();
    virtual ~ISrsExecutorHandler();

public:
    virtual void on_executor_done(ISrsInterruptable *executor) = 0;
};

// Start a coroutine for resource executor, to execute the handler and delete resource and itself when
// handler cycle done.
//
// Note that the executor will free itself by manager, then free the resource by manager. This is a helper
// that is used for a goroutine to execute a handler and free itself after the cycle is done.
//
// Generally, the handler, resource, and callback generally are the same object. But we do not define a single
// interface, because shared resource is a special interface.
//
// Note that the resource may live longer than executor, because it is shared resource, so you should process
// the callback. For example, you should never use the executor after it's stopped and deleted.
//
// Usage:
//      ISrsResourceManager* manager = ...;
//      ISrsResource* resource, ISrsCoroutineHandler* handler, ISrsExecutorHandler* callback = ...; // Resource, handler, and callback are the same object.
//      SrsExecutorCoroutine* executor = new SrsExecutorCoroutine(manager, resource, handler);
//      if ((err = executor->start()) != srs_success) {
//          srs_freep(executor);
//          return err;
//      }
class SrsExecutorCoroutine : public ISrsResource, // It's a resource.
                             public ISrsStartable,
                             public ISrsInterruptable,
                             public ISrsContextIdSetter,
                             public ISrsContextIdGetter,
                             public ISrsCoroutineHandler
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsResourceManager *manager_;
    ISrsResource *resource_;
    ISrsCoroutineHandler *handler_;
    ISrsExecutorHandler *callback_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsCoroutine *trd_;

public:
    SrsExecutorCoroutine(ISrsResourceManager *m, ISrsResource *r, ISrsCoroutineHandler *h, ISrsExecutorHandler *cb);
    virtual ~SrsExecutorCoroutine();
    // Interface ISrsStartable
public:
    virtual srs_error_t start();
    // Interface ISrsInterruptable
public:
    virtual void interrupt();
    virtual srs_error_t pull();
    // Interface ISrsContextId
public:
    virtual const SrsContextId &cid();
    virtual void set_cid(const SrsContextId &cid);
    // Interface ISrsCoroutineHandler
public:
    virtual srs_error_t cycle();
    // Interface ISrsResource
public:
    virtual const SrsContextId &get_id();
    virtual std::string desc();
};

#endif
