//
// Copyright (c) 2013-2026 The SRS Authors
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
class ISrsAppFactory;

// A placeholder to use before a real coroutine is created.
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

// Unlike a goroutine, which may run on any thread, an ST coroutine runs on one thread only,
// since it switches with setjmp and longjmp. SRS runs on one thread, like NGINX, with
// non-blocking sockets.
// @remark For multiple processes, use go-oryx to fork many SRS processes.
//      https://github.com/ossrs/go-oryx
// @remark For debugging, read _st_iterate_threads_flag in the ST README.
//      https://github.com/ossrs/state-threads/blob/st-1.9/README#L115
// @remark The thread is joinable, so it must be joined, or its memory leaks.
//      https://github.com/ossrs/srs/issues/78
class SrsSTCoroutine : public ISrsCoroutine
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    SrsFastCoroutine *impl_;

public:
    SrsSTCoroutine(std::string n, ISrsCoroutineHandler *h);
    SrsSTCoroutine(std::string n, ISrsCoroutineHandler *h, SrsContextId cid);
    virtual ~SrsSTCoroutine();

public:
    void set_stack_size(int v);

public:
    virtual srs_error_t start();
    // To stop many coroutines, such as the encoder's, interrupt them all first, then stop each one.
    virtual void stop();
    // It wakes the coroutine when it blocks in an ST call, such as st_read or st_write, so it can quit.
    virtual void interrupt();
    virtual srs_error_t pull();
    virtual const SrsContextId &cid();
    virtual void set_cid(const SrsContextId &cid);
};

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
    bool cycle_done_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
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
    void add(int n);
    void done();
    void wait();
};

class ISrsExecutorHandler
{
public:
    ISrsExecutorHandler();
    virtual ~ISrsExecutorHandler();

public:
    virtual void on_executor_done(ISrsInterruptable *executor) = 0;
};

// The handler, resource and callback are usually one object, but they are separate arguments, because a
// shared resource is a special interface.
//
// A shared resource may live longer than its executor, so it must handle the callback, and never use the
// executor after the executor is deleted.
class SrsExecutorCoroutine : public ISrsResource,
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

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsAppFactory *app_factory_;

public:
    SrsExecutorCoroutine(ISrsResourceManager *m, ISrsResource *r, ISrsCoroutineHandler *h, ISrsExecutorHandler *cb);
    void assemble(); // Creates the members, to avoid calling functions in the constructor.
    virtual ~SrsExecutorCoroutine();

public:
    virtual srs_error_t start();

public:
    virtual void interrupt();
    virtual srs_error_t pull();

public:
    virtual const SrsContextId &cid();
    virtual void set_cid(const SrsContextId &cid);

public:
    virtual srs_error_t cycle();

public:
    virtual const SrsContextId &get_id();
    virtual std::string desc();
};

#endif
