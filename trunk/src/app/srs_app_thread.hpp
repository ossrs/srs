/*
The MIT License (MIT)

Copyright (c) 2013-2014 winlin

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
* when thread interrupt, the socket maybe not got EINT,
* espectially on st_usleep(), so the cycle must check the loop,
* when handler->cycle() has loop itself, for example:
*         handler->cycle() is:
*             while (true):
*                 st_usleep(0);
*                 if (read_from_socket(skt) < 0) break;
* if thread stop when read_from_socket, it's ok, the loop will break,
* but when thread stop interrupt the s_usleep(0), then the loop is
* death loop.
* in a word, the handler->cycle() must:
*        handler->cycle() is:
*             while (pthread->can_loop()):
*                st_usleep(0);
*                 if (read_from_socket(skt) < 0) break;
* check the loop, then it works.
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
private:
    ISrsThreadHandler* handler;
    int64_t cycle_interval_us;
public:
    /**
    * initialize the thread.
    * @param thread_handler, the cycle handler for the thread.
    * @param interval_us, the sleep interval when cycle finished.
    */
    SrsThread(ISrsThreadHandler* thread_handler, int64_t interval_us);
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
private:
    virtual void thread_cycle();
    static void* thread_fun(void* arg);
};

#endif
