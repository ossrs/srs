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

#include <srs_app_st.hpp>

#include <st.h>
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

SrsCoroutine::SrsCoroutine(const string& n, ISrsCoroutineHandler* h, int cid)
{
    name = n;
    handler = h;
    context = cid;
    trd = NULL;
    err = ERROR_SUCCESS;
    started = interrupted = disposed = false;
}

SrsCoroutine::~SrsCoroutine()
{
    stop();
}

int SrsCoroutine::start()
{
    int ret = ERROR_SUCCESS;
    
    if (started || disposed) {
        ret = ERROR_THREAD_DISPOSED;
        err = (err == ERROR_SUCCESS? ret:err);
        srs_error("Thread.start: Failed, started=%d, disposed=%d, ret=%d", started, disposed, ret);
        return ret;
    }
    
    if((trd = (srs_thread_t)st_thread_create(pfn, this, 1, 0)) == NULL){
        ret = ERROR_ST_CREATE_CYCLE_THREAD;
        srs_error("Thread.start: Create thread failed. ret=%d", ret);
        return ret;
    }
    
    started = true;

    return ret;
}

void SrsCoroutine::stop()
{
    if (!started || disposed) {
        return;
    }
    disposed = true;
    
    interrupt();
    
    void* res = NULL;
    int ret = st_thread_join((st_thread_t)trd, &res);
    srs_info("Thread.stop: Terminated, ret=%d, err=%d", ret, err);
    srs_assert(!ret);
    
    // Always override the error by the worker.
    if (!res) {
        err = (int)(uint64_t)res;
    } else {
        err = ERROR_THREAD_TERMINATED;
    }
    
    return;
}

void SrsCoroutine::interrupt()
{
    if (!started || interrupted) {
        return;
    }
    interrupted = true;
    
    srs_info("Thread.interrupt: Interrupt thread, err=%d", err);
    err = (err == ERROR_SUCCESS? ERROR_THREAD_INTERRUPED:err);
    st_thread_interrupt((st_thread_t)trd);
}

int SrsCoroutine::pull()
{
    return err;
}

int SrsCoroutine::cid()
{
    return context;
}

int SrsCoroutine::cycle()
{
    if (_srs_context) {
        if (context) {
            _srs_context->set_id(context);
        } else {
            context = _srs_context->generate_id();
        }
    }
    srs_info("Thread.cycle: Start with cid=%d, err=%d", context, err);
    
    int ret = handler->cycle();
    srs_info("Thread.cycle: Finished with ret=%d, err=%d", ret, err);
    return ret;
}

void* SrsCoroutine::pfn(void* arg)
{
    SrsCoroutine* p = (SrsCoroutine*)arg;
    void*res = (void*)(uint64_t)p->cycle();
    return res;
}

