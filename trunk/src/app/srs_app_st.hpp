//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#ifndef SRS_APP_ST_HPP
#define SRS_APP_ST_HPP

#include <srs_core.hpp>

#include <string>

#include <srs_kernel_log.hpp>
#include <srs_kernel_error.hpp>
#include <srs_protocol_st.hpp>
#include <srs_protocol_io.hpp>

class SrsFastCoroutine;

// Each ST-coroutine must implements this interface,
// to do the cycle job and handle some events.
//
// Thread do a job then terminated normally, it's a SrsOneCycleThread:
//      class SrsOneCycleThread : public ISrsCoroutineHandler {
//          public: SrsCoroutine trd;
//          public: virtual srs_error_t cycle() {
//              // Do something, then return this cycle and thread terminated normally.
//          }
//      };
//
// Thread has its inside loop, such as the RTMP receive thread:
//      class SrsReceiveThread : public ISrsCoroutineHandler {
//          public: SrsCoroutine* trd;
//          public: virtual srs_error_t cycle() {
//              while (true) {
//                  // Check whether thread interrupted.
//                  if ((err = trd->pull()) != srs_success) {
//                      return err;
//                  }
//                  // Do something, such as st_read() packets, it'll be wakeup
//                  // when user stop or interrupt the thread.
//              }
//          }
//      };
class ISrsCoroutineHandler
{
public:
    ISrsCoroutineHandler();
    virtual ~ISrsCoroutineHandler();
public:
    // Do the work. The ST-coroutine will terminated normally if it returned.
    // @remark If the cycle has its own loop, it must check the thread pull.
    virtual srs_error_t cycle() = 0;
};

// Start the object, generally a croutine.
class ISrsStartable
{
public:
    ISrsStartable();
    virtual ~ISrsStartable();
public:
    virtual srs_error_t start() = 0;
};

// The corotine object.
class SrsCoroutine : public ISrsStartable
{
public:
    SrsCoroutine();
    virtual ~SrsCoroutine();
public:
    virtual void stop() = 0;
    virtual void interrupt() = 0;
    // @return a copy of error, which should be freed by user.
    //      NULL if not terminated and user should pull again.
    virtual srs_error_t pull() = 0;
    // Get and set the context id of coroutine.
    virtual const SrsContextId& cid() = 0;
    virtual void set_cid(const SrsContextId& cid) = 0;
};

// An empty coroutine, user can default to this object before create any real coroutine.
// @see https://github.com/ossrs/srs/pull/908
class SrsDummyCoroutine : public SrsCoroutine
{
private:
    SrsContextId cid_;
public:
    SrsDummyCoroutine();
    virtual ~SrsDummyCoroutine();
public:
    virtual srs_error_t start();
    virtual void stop();
    virtual void interrupt();
    virtual srs_error_t pull();
    virtual const SrsContextId& cid();
    virtual void set_cid(const SrsContextId& cid);
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
class SrsSTCoroutine : public SrsCoroutine
{
private:
    SrsFastCoroutine* impl_;
public:
    // Create a thread with name n and handler h.
    // @remark User can specify a cid for thread to use, or we will allocate a new one.
    SrsSTCoroutine(std::string n, ISrsCoroutineHandler* h);
    SrsSTCoroutine(std::string n, ISrsCoroutineHandler* h, SrsContextId cid);
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
    virtual const SrsContextId& cid();
    virtual void set_cid(const SrsContextId& cid);
};

// High performance coroutine.
class SrsFastCoroutine
{
private:
    std::string name;
    int stack_size;
    ISrsCoroutineHandler* handler;
private:
    srs_thread_t trd;
    SrsContextId cid_;
    srs_error_t trd_err;
private:
    bool started;
    bool interrupted;
    bool disposed;
    // Cycle done, no need to interrupt it.
    bool cycle_done;
private:
    // Sub state in disposed, we need to wait for thread to quit.
    bool stopping_;
    SrsContextId stopping_cid_;
public:
    SrsFastCoroutine(std::string n, ISrsCoroutineHandler* h);
    SrsFastCoroutine(std::string n, ISrsCoroutineHandler* h, SrsContextId cid);
    virtual ~SrsFastCoroutine();
public:
    void set_stack_size(int v);
public:
    srs_error_t start();
    void stop();
    void interrupt();
    inline srs_error_t pull() {
        if (trd_err == srs_success) {
            return srs_success;
        }
        return srs_error_copy(trd_err);
    }
    const SrsContextId& cid();
    virtual void set_cid(const SrsContextId& cid);
private:
    srs_error_t cycle();
    static void* pfn(void* arg);
};

// Like goroytine sync.WaitGroup.
class SrsWaitGroup
{
private:
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
    // Wait for all corotine to be done.
    void wait();
};

#endif

