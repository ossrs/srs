/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2017 OSSRS(winlin)
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

#ifndef SRS_APP_THREAD_HPP
#define SRS_APP_THREAD_HPP

#include <srs_core.hpp>

#include <vector>

#include <srs_app_st.hpp>
#include <srs_service_conn.hpp>

/**
 * The coroutine manager use a thread to delete a connection, which will stop the service
 * thread, for example, when the RTMP connection thread cycle terminated, it will notify
 * the manager(the server) to remove the connection from list of server and push it to
 * the manager thread to delete it, finally the thread of connection will stop.
 */
class SrsCoroutineManager : virtual public ISrsCoroutineHandler, virtual public IConnectionManager
{
private:
    SrsCoroutine* trd;
    std::vector<ISrsConnection*> conns;
    st_cond_t cond;
public:
    SrsCoroutineManager();
    virtual ~SrsCoroutineManager();
public:
    int start();
// ISrsCoroutineHandler
public:
    virtual int cycle();
// IConnectionManager
public:
    virtual void remove(ISrsConnection* c);
private:
    void clear();
};

/**
 * the reuse thread is a thread stop and start by other thread.
 *       user can create thread and stop then start again and again,
 *       generally must provides a start and stop method, @see SrsIngester.
 *       the step to create a thread stop by other thread:
 *       1. create SrsReusableThread field.
 *       2. must manually stop the thread when started it.
 *       for example:
 *           class SrsIngester : public ISrsReusableThreadHandler {
 *               public: SrsIngester() { pthread = new SrsReusableThread("ingest", this, SRS_AUTO_INGESTER_CIMS); }
 *               public: virtual int start() { return pthread->start(); }
 *               public: virtual void stop() { pthread->stop(); }
 *               public: virtual int cycle() {
 *                   // check status, start ffmpeg when stopped.
 *               }
 *           };
 */
class ISrsReusableThreadHandler
{
public:
    ISrsReusableThreadHandler();
    virtual ~ISrsReusableThreadHandler();
public:
    /**
     * the cycle method for the one cycle thread.
     * @remark when the cycle has its inner loop, it must check whether
     * the thread is interrupted.
     */
    virtual int cycle() = 0;
public:
    /**
     * other callback for handler.
     * @remark all callback is optional, handler can ignore it.
     */
    virtual void on_thread_start();
    virtual int on_before_cycle();
    virtual int on_end_cycle();
    virtual void on_thread_stop();
};
class SrsReusableThread : public internal::ISrsThreadHandler
{
private:
    internal::SrsThread* pthread;
    ISrsReusableThreadHandler* handler;
public:
    SrsReusableThread(const char* n, ISrsReusableThreadHandler* h, int64_t cims = 0);
    virtual ~SrsReusableThread();
public:
    /**
     * for the reusable thread, start and stop by user.
     */
    virtual int start();
    /**
     * stop the thread, wait for the thread to terminate.
     * @remark user can stop multiple times, ignore if already stopped.
     */
    virtual void stop();
    /**
     * whether the thread should loop,
     * used for handler->cycle() which has a loop method,
     * to check this method, break if false.
     */
    virtual bool can_loop();
public:
    /**
     * get the context id. @see: ISrsThreadContext.get_id().
     * used for parent thread to get the id.
     * @remark when start thread, parent thread will block and wait for this id ready.
     */
    virtual int cid();
// interface internal::ISrsThreadHandler
public:
    virtual int cycle();
    virtual void on_thread_start();
    virtual int on_before_cycle();
    virtual int on_end_cycle();
    virtual void on_thread_stop();
};

/**
 * the reuse thread is a thread stop and start by other thread.
 * the version 2, is the thread cycle has its inner loop, which should
 * check the intterrupt, and should interrupt thread when the inner loop want
 * to quit the thread.
 *       user can create thread and stop then start again and again,
 *       generally must provides a start and stop method, @see SrsIngester.
 *       the step to create a thread stop by other thread:
 *       1. create SrsReusableThread field.
 *       2. must manually stop the thread when started it.
 *       for example:
 *           class SrsIngester : public ISrsReusableThreadHandler {
 *               public: SrsIngester() { pthread = new SrsReusableThread("ingest", this, SRS_AUTO_INGESTER_CIMS); }
 *               public: virtual int start() { return pthread->start(); }
 *               public: virtual void stop() { pthread->stop(); }
 *               public: virtual int cycle() {
 *                  while (!pthread->interrupted()) {
 *                      // quit thread when error.
 *                      if (ret != ERROR_SUCCESS) {
 *                          pthread->interrupt();
 *                      }
 *
 *                      // do something.
 *                  }
 *               }
 *           };
 */
class ISrsReusableThread2Handler
{
public:
    ISrsReusableThread2Handler();
    virtual ~ISrsReusableThread2Handler();
public:
    /**
     * the cycle method for the one cycle thread.
     * @remark when the cycle has its inner loop, it must check whether
     * the thread is interrupted.
     */
    virtual int cycle() = 0;
public:
    /**
     * other callback for handler.
     * @remark all callback is optional, handler can ignore it.
     */
    virtual void on_thread_start();
    virtual int on_before_cycle();
    virtual int on_end_cycle();
    virtual void on_thread_stop();
};
class SrsReusableThread2 : public internal::ISrsThreadHandler
{
private:
    internal::SrsThread* pthread;
    ISrsReusableThread2Handler* handler;
public:
    SrsReusableThread2(const char* n, ISrsReusableThread2Handler* h, int64_t cims = 0);
    virtual ~SrsReusableThread2();
public:
    /**
     * for the reusable thread, start and stop by user.
     */
    virtual int start();
    /**
     * stop the thread, wait for the thread to terminate.
     * @remark user can stop multiple times, ignore if already stopped.
     */
    virtual void stop();
public:
    /**
     * get the context id. @see: ISrsThreadContext.get_id().
     * used for parent thread to get the id.
     * @remark when start thread, parent thread will block and wait for this id ready.
     */
    virtual int cid();
    /**
     * interrupt the thread to stop loop.
     * we only set the loop flag to false, not really interrupt the thread.
     */
    virtual void interrupt();
    /**
     * whether the thread is interrupted,
     * for the cycle has its loop, the inner loop should quit when thread
     * is interrupted.
     */
    virtual bool interrupted();
// interface internal::ISrsThreadHandler
public:
    virtual int cycle();
    virtual void on_thread_start();
    virtual int on_before_cycle();
    virtual int on_end_cycle();
    virtual void on_thread_stop();
};

#endif

