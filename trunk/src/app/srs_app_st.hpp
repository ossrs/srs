/*
The MIT License (MIT)

Copyright (c) 2013-2015 SRS(ossrs)

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

#ifndef SRS_APP_ST_HPP
#define SRS_APP_ST_HPP

/*
#include <srs_app_st.hpp>
*/

#include <srs_core.hpp>

#include <string>

#include <st.h>

#include <srs_app_st.hpp>
#include <srs_protocol_io.hpp>

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
        st_thread_t tid;
        int _cid;
        bool loop;
        bool can_run;
        bool really_terminated;
        bool _joinable;
        const char* _name;
        bool disposed;
    private:
        ISrsThreadHandler* handler;
        int64_t cycle_interval_us;
    public:
        /**
         * initialize the thread.
         * @param name, human readable name for st debug.
         * @param thread_handler, the cycle handler for the thread.
         * @param interval_us, the sleep interval when cycle finished.
         * @param joinable, if joinable, other thread must stop the thread.
         * @remark if joinable, thread never quit itself, or memory leak.
         * @see: https://github.com/ossrs/srs/issues/78
         * @remark about st debug, see st-1.9/README, _st_iterate_threads_flag
         */
        /**
         * TODO: FIXME: maybe all thread must be reap by others threads,
         * @see: https://github.com/ossrs/srs/issues/77
         */
        SrsThread(const char* name, ISrsThreadHandler* thread_handler, int64_t interval_us, bool joinable);
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
        virtual void dispose();
        virtual void thread_cycle();
        static void* thread_fun(void* arg);
    };
}

/**
 * the socket provides TCP socket over st,
 * that is, the sync socket mechanism.
 */
class SrsStSocket : public ISrsProtocolReaderWriter
{
private:
    int64_t recv_timeout;
    int64_t send_timeout;
    int64_t recv_bytes;
    int64_t send_bytes;
    st_netfd_t stfd;
public:
    SrsStSocket(st_netfd_t client_stfd);
    virtual ~SrsStSocket();
public:
    virtual bool is_never_timeout(int64_t timeout_us);
    virtual void set_recv_timeout(int64_t timeout_us);
    virtual int64_t get_recv_timeout();
    virtual void set_send_timeout(int64_t timeout_us);
    virtual int64_t get_send_timeout();
    virtual int64_t get_recv_bytes();
    virtual int64_t get_send_bytes();
public:
    /**
     * @param nread, the actual read bytes, ignore if NULL.
     */
    virtual int read(void* buf, size_t size, ssize_t* nread);
    virtual int read_fully(void* buf, size_t size, ssize_t* nread);
    /**
     * @param nwrite, the actual write bytes, ignore if NULL.
     */
    virtual int write(void* buf, size_t size, ssize_t* nwrite);
    virtual int writev(const iovec *iov, int iov_size, ssize_t* nwrite);
};

/**
 * the common tcp client, to connect to specified TCP server,
 * reconnect and close the connection.
 */
class SrsTcpClient : public ISrsProtocolReaderWriter
{
private:
    st_netfd_t stfd;
    SrsStSocket* io;
public:
    SrsTcpClient();
    virtual ~SrsTcpClient();
public:
    /**
     * whether connected to server.
     */
    virtual bool connected();
public:
    /**
     * connect to server over TCP.
     * @param host the ip or hostname of server.
     * @param port the port to connect to.
     * @param timeout the timeout in us.
     * @remark ignore when connected.
     */
    virtual int connect(std::string host, int port, int64_t timeout);
    /**
     * close the connection.
     * @remark ignore when closed.
     */
    virtual void close();
// interface ISrsProtocolReaderWriter
public:
    virtual bool is_never_timeout(int64_t timeout_us);
    virtual void set_recv_timeout(int64_t timeout_us);
    virtual int64_t get_recv_timeout();
    virtual void set_send_timeout(int64_t timeout_us);
    virtual int64_t get_send_timeout();
    virtual int64_t get_recv_bytes();
    virtual int64_t get_send_bytes();
    virtual int read(void* buf, size_t size, ssize_t* nread);
    virtual int read_fully(void* buf, size_t size, ssize_t* nread);
    virtual int write(void* buf, size_t size, ssize_t* nwrite);
    virtual int writev(const iovec *iov, int iov_size, ssize_t* nwrite);
};

// initialize st, requires epoll.
extern int srs_st_init();

// close the netfd, and close the underlayer fd.
extern void srs_close_stfd(st_netfd_t& stfd);

#endif

