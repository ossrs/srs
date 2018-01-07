/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2018 Winlin
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

#ifndef SRS_APP_ST_HPP
#define SRS_APP_ST_HPP

#include <srs_core.hpp>

#include <string>

#include <srs_service_st.hpp>
#include <srs_protocol_io.hpp>

/**
 * Each ST-coroutine must implements this interface,
 * to do the cycle job and handle some events.
 *
 * Thread do a job then terminated normally, it's a SrsOneCycleThread:
 *      class SrsOneCycleThread : public ISrsCoroutineHandler {
 *          public: SrsCoroutine trd;
 *          public: virtual srs_error_t cycle() {
 *              // Do something, then return this cycle and thread terminated normally.
 *          }
 *      };
 *
 * Thread has its inside loop, such as the RTMP receive thread:
 *      class SrsReceiveThread : public ISrsCoroutineHandler {
 *          public: SrsCoroutine* trd;
 *          public: virtual srs_error_t cycle() {
 *              while (true) {
 *                  // Check whether thread interrupted.
 *                  if ((err = trd->pull()) != srs_success) {
 *                      return err;
 *                  }
 *                  // Do something, such as st_read() packets, it'll be wakeup
 *                  // when user stop or interrupt the thread.
 *              }
 *          }
 *      };
 */
class ISrsCoroutineHandler
{
public:
    ISrsCoroutineHandler();
    virtual ~ISrsCoroutineHandler();
public:
    /**
     * Do the work. The ST-coroutine will terminated normally if it returned.
     * @remark If the cycle has its own loop, it must check the thread pull.
     */
    virtual srs_error_t cycle() = 0;
};

/**
 * The corotine object.
 */
class SrsCoroutine
{
public:
    SrsCoroutine();
    virtual ~SrsCoroutine();
public:
    virtual srs_error_t start() = 0;
    virtual void stop() = 0;
    virtual void interrupt() = 0;
    // @return a copy of error, which should be freed by user.
    //      NULL if not terminated and user should pull again.
    virtual srs_error_t pull() = 0;
    virtual int cid() = 0;
};

/**
 * An empty coroutine, user can default to this object before create any real coroutine.
 * @see https://github.com/ossrs/srs/pull/908
 */
class SrsDummyCoroutine : public SrsCoroutine
{
public:
    SrsDummyCoroutine();
    virtual ~SrsDummyCoroutine();
public:
    virtual srs_error_t start();
    virtual void stop();
    virtual void interrupt();
    virtual srs_error_t pull();
    virtual int cid();
};

/**
 * A ST-coroutine is a lightweight thread, just like the goroutine.
 * But the goroutine maybe run on different thread, while ST-coroutine only
 * run in single thread, because it use setjmp and longjmp, so it may cause
 * problem in multiple threads. For SRS, we only use single thread module,
 * like NGINX to get very high performance, with asynchronous and non-blocking
 * sockets.
 * @reamrk For multiple processes, please use go-oryx to fork many SRS processes.
 *      Please read https://github.com/ossrs/go-oryx
 * @remark For debugging of ST-coroutine, read _st_iterate_threads_flag of ST/README 
 *      https://github.com/ossrs/state-threads/blob/st-1.9/README#L115
 * @remark We always create joinable thread, so we must join it or memory leak,
 *      Please read https://github.com/ossrs/srs/issues/78
 */
class SrsSTCoroutine : public SrsCoroutine
{
private:
    std::string name;
    ISrsCoroutineHandler* handler;
private:
    srs_thread_t trd;
    int context;
    srs_error_t trd_err;
private:
    bool started;
    bool interrupted;
    bool disposed;
public:
    // Create a thread with name n and handler h.
    // @remark User can specify a cid for thread to use, or we will allocate a new one.
    SrsSTCoroutine(const std::string& n, ISrsCoroutineHandler* h, int cid = 0);
    virtual ~SrsSTCoroutine();
public:
    /**
     * Start the thread.
     * @remark Should never start it when stopped or terminated.
     */
    virtual srs_error_t start();
    /**
     * Interrupt the thread then wait to terminated.
     * @remark If user want to notify thread to quit async, for example if there are
     *      many threads to stop like the encoder, use the interrupt to notify all threads
     *      to terminate then use stop to wait for each to terminate.
     */
    virtual void stop();
    /**
     * Interrupt the thread and notify it to terminate, it will be wakeup if it's blocked
     * in some IO operations, such as st_read or st_write, then it will found should quit,
     * finally the thread should terminated normally, user can use the stop to join it.
     */
    virtual void interrupt();
    /**
     * Check whether thread is terminated normally or error(stopped or termianted with error),
     * and the thread should be running if it return ERROR_SUCCESS.
     * @remark Return specified error when thread terminated normally with error.
     * @remark Return ERROR_THREAD_TERMINATED when thread terminated normally without error.
     * @remark Return ERROR_THREAD_INTERRUPED when thread is interrupted.
     */
    virtual srs_error_t pull();
    /**
     * Get the context id of thread.
     */
    virtual int cid();
private:
    virtual srs_error_t cycle();
    static void* pfn(void* arg);
};

#endif

