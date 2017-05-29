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
 *          public: virtual int cycle() {
 *              // Do something, then return this cycle and thread terminated normally.
 *          }
 *      };
 *
 * Thread has its inside loop, such as the RTMP receive thread:
 *      class SrsReceiveThread : public ISrsCoroutineHandler {
 *          public: SrsCoroutine trd;
 *          public: virtual int cycle() {
 *              while (!trd.pull()) { // Check whether thread interrupted.
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
    virtual int cycle() = 0;
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
class SrsCoroutine
{
private:
    std::string name;
    ISrsCoroutineHandler* handler;
private:
    st_thread_t trd;
    int context;
    int err;
private:
    bool started;
    bool interrupted;
    bool disposed;
public:
    // Create a thread with name n and handler h.
    // @remark User can specify a cid for thread to use, or we will allocate a new one.
    SrsCoroutine(const std::string& n, ISrsCoroutineHandler* h, int cid = 0);
    virtual ~SrsCoroutine();
public:
    /**
     * Start the thread.
     * @remark Should never start it when stopped or terminated.
     */
    virtual int start();
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
    virtual int pull();
    /**
     * Get the context id of thread.
     */
    virtual int cid();
private:
    virtual int cycle();
    static void* pfn(void* arg);
};

// the internal classes, user should never use it.
// user should use the public classes at the bellow:
// @see SrsEndlessThread, SrsOneCycleThread, SrsReusableThread
namespace internal
{
    /**
     * the handler for the thread, callback interface.
     * the thread model defines as:
     *     handler->on_thread_start()
     *     while loop:
     *        handler->on_before_cycle()
     *        handler->cycle()
     *        handler->on_end_cycle()
     *        if !loop then break for user stop thread.
     *        sleep(CycleIntervalMilliseconds)
     *     handler->on_thread_stop()
     * when stop, the thread will interrupt the st_thread,
     * which will cause the socket to return error and
     * terminate the cycle thread.
     *
     * @remark why should check can_loop() in cycle method?
     *       when thread interrupt, the socket maybe not got EINT,
     *       espectially on st_usleep(), so the cycle must check the loop,
     *       when handler->cycle() has loop itself, for example:
     *               while (true):
     *                   if (read_from_socket(skt) < 0) break;
     *       if thread stop when read_from_socket, it's ok, the loop will break,
     *       but when thread stop interrupt the s_usleep(0), then the loop is
     *       death loop.
     *       in a word, the handler->cycle() must:
     *               while (pthread->can_loop()):
     *                   if (read_from_socket(skt) < 0) break;
     *       check the loop, then it works.
     *
     * @remark why should use stop_loop() to terminate thread in itself?
     *       in the thread itself, that is the cycle method,
     *       if itself want to terminate the thread, should never use stop(),
     *       but use stop_loop() to set the loop to false and terminate normally.
     *
     * @remark when should set the interval_us, and when not?
     *       the cycle will invoke util cannot loop, eventhough the return code of cycle is error,
     *       so the interval_us used to sleep for each cycle.
     */
    class ISrsThreadHandler
    {
    public:
        ISrsThreadHandler();
        virtual ~ISrsThreadHandler();
    public:
        virtual void on_thread_start();
        virtual int on_before_cycle();
        virtual int cycle() = 0;
        virtual int on_end_cycle();
        virtual void on_thread_stop();
    };
    
    /**
     * provides servies from st_thread_t,
     * for common thread usage.
     */
    class SrsThread
    {
    private:
        st_thread_t trd;
        int context_id;
        bool loop;
        bool joinable;
        const char* name;
    private:
        ISrsThreadHandler* handler;
        // The cycle interval in ms.
        int64_t cims;
    public:
        /**
         * initialize the thread.
         * @param n, human readable name for st debug.
         * @param h, the cycle handler for the thread.
         * @param ims, the sleep interval in ms when cycle finished.
         * @param j, if joinable, other thread must stop the thread.
         * @remark if joinable, thread never quit itself, or memory leak.
         * @see: https://github.com/ossrs/srs/issues/78
         * @remark about st debug, see st-1.9/README, _st_iterate_threads_flag
         */
        /**
         * TODO: FIXME: maybe all thread must be reap by others threads,
         * @see: https://github.com/ossrs/srs/issues/77
         */
        SrsThread(const char* n, ISrsThreadHandler* h, int64_t ims, bool j);
        virtual ~SrsThread();
    public:
        /**
         * get the context id. @see: ISrsThreadContext.get_id().
         * used for parent thread to get the id.
         * @remark when start thread, parent thread will block and wait for this id ready.
         */
        virtual int cid();
        /**
         * start the thread, invoke the cycle of handler util
         * user stop the thread.
         * @remark ignore any error of cycle of handler.
         * @remark user can start multiple times, ignore if already started.
         * @remark wait for the cid is set by thread pfn.
         */
        virtual int start();
        /**
         * stop the thread, wait for the thread to terminate.
         * @remark user can stop multiple times, ignore if already stopped.
         */
        virtual void stop();
    public:
        /**
         * whether the thread should loop,
         * used for handler->cycle() which has a loop method,
         * to check this method, break if false.
         */
        virtual bool can_loop();
        /**
         * for the loop thread to stop the loop.
         * other thread can directly use stop() to stop loop and wait for quit.
         * this stop loop method only set loop to false.
         */
        virtual void stop_loop();
    private:
        virtual void cycle();
        static void* pfn(void* arg);
    };
}

#endif

