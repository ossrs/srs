/*
The MIT License (MIT)

Copyright (c) 2013-2016 SRS(ossrs)

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

#ifndef SRS_APP_THREAD_HPP
#define SRS_APP_THREAD_HPP

/*
#include <srs_app_thread.hpp>
*/
#include <srs_core.hpp>

#include <srs_app_st.hpp>

/**
 * the endless thread is a loop thread never quit.
 *      user can create thread always running util server terminate.
 *      the step to create a thread never stop:
 *      1. create SrsEndlessThread field.
 *      for example:
 *          class SrsBufferCache : public ISrsEndlessThreadHandler {
 *               public: SrsBufferCache() { pthread = new SrsEndlessThread("http-stream", this); }
 *               public: virtual int cycle() {
 *                   // do some work never end.
 *               }
 *          }
 * @remark user must use block method in cycle method, for example, sleep or socket io.
 */
class ISrsEndlessThreadHandler
{
public:
    ISrsEndlessThreadHandler();
    virtual ~ISrsEndlessThreadHandler();
public:
    /**
     * the cycle method for the common thread.
     * @remark user must use block method in cycle method, for example, sleep or socket io.
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
class SrsEndlessThread : public internal::ISrsThreadHandler
{
private:
    internal::SrsThread* pthread;
    ISrsEndlessThreadHandler* handler;
public:
    SrsEndlessThread(const char* n, ISrsEndlessThreadHandler* h);
    virtual ~SrsEndlessThread();
public:
    /**
     * for the endless thread, never quit.
     */
    virtual int start();
// interface internal::ISrsThreadHandler
public:
    virtual int cycle();
    virtual void on_thread_start();
    virtual int on_before_cycle();
    virtual int on_end_cycle();
    virtual void on_thread_stop();
};

/**
 * the one cycle thread is a thread do the cycle only one time,
 * that is, the thread will quit when return from the cycle.
 *       user can create thread which stop itself,
 *       generally only need to provides a start method,
 *       the object will destroy itself then terminate the thread, @see SrsConnection
 *       1. create SrsThread field
 *       2. the thread quit when return from cycle.
 *       for example:
 *           class SrsConnection : public ISrsOneCycleThreadHandler {
 *               public: SrsConnection() { pthread = new SrsOneCycleThread("conn", this); }
 *               public: virtual int start() { return pthread->start(); }
 *               public: virtual int cycle() {
 *                   // serve client.
 *                   // set loop to stop to quit, stop thread itself.
 *                   pthread->stop_loop();
 *               }
 *               public: virtual void on_thread_stop() {
 *                   // remove the connection in thread itself.
 *                   server->remove(this);
 *               }
 *           };
 */
class ISrsOneCycleThreadHandler
{
public:
    ISrsOneCycleThreadHandler();
    virtual ~ISrsOneCycleThreadHandler();
public:
    /**
     * the cycle method for the one cycle thread.
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
class SrsOneCycleThread : public internal::ISrsThreadHandler
{
private:
    internal::SrsThread* pthread;
    ISrsOneCycleThreadHandler* handler;
public:
    SrsOneCycleThread(const char* n, ISrsOneCycleThreadHandler* h);
    virtual ~SrsOneCycleThread();
public:
    /**
     * for the one cycle thread, quit when cycle return.
     */
    virtual int start();
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
 *       user can create thread and stop then start again and again,
 *       generally must provides a start and stop method, @see SrsIngester.
 *       the step to create a thread stop by other thread:
 *       1. create SrsReusableThread field.
 *       2. must manually stop the thread when started it.
 *       for example:
 *           class SrsIngester : public ISrsReusableThreadHandler {
 *               public: SrsIngester() { pthread = new SrsReusableThread("ingest", this, SRS_AUTO_INGESTER_SLEEP_US); }
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
    SrsReusableThread(const char* n, ISrsReusableThreadHandler* h, int64_t interval_us = 0);
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
 *               public: SrsIngester() { pthread = new SrsReusableThread("ingest", this, SRS_AUTO_INGESTER_SLEEP_US); }
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
    SrsReusableThread2(const char* n, ISrsReusableThread2Handler* h, int64_t interval_us = 0);
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

