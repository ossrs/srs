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
    
    if((trd = st_thread_create(pfn, this, 1, 0)) == NULL){
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
    int ret = st_thread_join(trd, &res);
    srs_trace("Thread.stop: Terminated, ret=%d, err=%d", ret, err);
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
    
    srs_trace("Thread.interrupt: Interrupt thread, err=%d", err);
    err = (err == ERROR_SUCCESS? ERROR_THREAD_INTERRUPED:err);
    st_thread_interrupt(trd);
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
    if (!context && _srs_context) {
        context = _srs_context->generate_id();
    }
    srs_trace("Thread.cycle: Start with cid=%d, err=%d", context, err);
    
    int ret = handler->cycle();
    srs_trace("Thread.cycle: Finished with ret=%d, err=%d", ret, err);
    return ret;
}

void* SrsCoroutine::pfn(void* arg)
{
    SrsCoroutine* p = (SrsCoroutine*)arg;
    void*res = (void*)(uint64_t)p->cycle();
    return res;
}

namespace internal
{
    ISrsThreadHandler::ISrsThreadHandler()
    {
    }
    
    ISrsThreadHandler::~ISrsThreadHandler()
    {
    }
    
    void ISrsThreadHandler::on_thread_start()
    {
    }
    
    int ISrsThreadHandler::on_before_cycle()
    {
        int ret = ERROR_SUCCESS;
        return ret;
    }
    
    int ISrsThreadHandler::on_end_cycle()
    {
        int ret = ERROR_SUCCESS;
        return ret;
    }
    
    void ISrsThreadHandler::on_thread_stop()
    {
    }
    
    SrsThread::SrsThread(const char* n, ISrsThreadHandler* h, int64_t ims, bool j)
    {
        name = n;
        handler = h;
        cims = ims;
        
        trd = NULL;
        loop = false;
        context_id = -1;
        joinable = j;
    }
    
    SrsThread::~SrsThread()
    {
        stop();
    }
    
    int SrsThread::cid()
    {
        return context_id;
    }
    
    int SrsThread::start()
    {
        int ret = ERROR_SUCCESS;
        
        if(trd) {
            srs_info("thread %s already running.", name);
            return ret;
        }
        
        loop = true;
        
        if((trd = st_thread_create(pfn, this, (joinable? 1:0), 0)) == NULL){
            ret = ERROR_ST_CREATE_CYCLE_THREAD;
            srs_error("st_thread_create failed. ret=%d", ret);
            return ret;
        }
        
        return ret;
    }
    
    void SrsThread::stop()
    {
        if (!trd) {
            return;
        }
        
        // notify the cycle to stop loop.
        loop = false;
        
        // the interrupt will cause the socket to read/write error,
        // which will terminate the cycle thread.
        st_thread_interrupt(trd);
        
        // when joinable, wait util quit.
        if (joinable) {
            // wait the thread to exit.
            int ret = st_thread_join(trd, NULL);
            srs_assert(ret == ERROR_SUCCESS);
        }
        
        trd = NULL;
    }
    
    bool SrsThread::can_loop()
    {
        return loop;
    }
    
    void SrsThread::stop_loop()
    {
        loop = false;
    }
    
    void SrsThread::cycle()
    {
        int ret = ERROR_SUCCESS;
        
        // TODO: FIXME: it's better for user to specifies the cid,
        //      because sometimes we need to merge cid, for example,
        //      the publish thread should use the same cid of connection.
        _srs_context->generate_id();
        srs_info("thread %s cycle start", name);
        context_id = _srs_context->get_id();
        
        srs_assert(handler);
        handler->on_thread_start();
        
        while (loop) {
            if ((ret = handler->on_before_cycle()) != ERROR_SUCCESS) {
                srs_warn("thread %s on before cycle failed, ignored and retry, ret=%d", name, ret);
                goto failed;
            }
            srs_info("thread %s on before cycle success", name);
            
            if ((ret = handler->cycle()) != ERROR_SUCCESS) {
                if (!srs_is_client_gracefully_close(ret) && !srs_is_system_control_error(ret)) {
                    srs_warn("thread %s cycle failed, ignored and retry, ret=%d", name, ret);
                }
                goto failed;
            }
            srs_info("thread %s cycle success", name);
            
            if ((ret = handler->on_end_cycle()) != ERROR_SUCCESS) {
                srs_warn("thread %s on end cycle failed, ignored and retry, ret=%d", name, ret);
                goto failed;
            }
            srs_info("thread %s on end cycle success", name);
            
        failed:
            if (!loop) {
                break;
            }
            
            // Should never use no timeout, just ignore it.
            // to improve performance, donot sleep when interval is zero.
            // @see: https://github.com/ossrs/srs/issues/237
            if (cims != 0 && cims != SRS_CONSTS_NO_TMMS) {
                st_usleep(cims * 1000);
            }
        }
        
        srs_info("thread %s cycle finished", name);
        // @remark in this callback, user may delete this, so never use this->xxx anymore.
        handler->on_thread_stop();
    }
    
    void* SrsThread::pfn(void* arg)
    {
        SrsThread* obj = (SrsThread*)arg;
        srs_assert(obj);
        
        obj->cycle();
        
        // delete cid for valgrind to detect memory leak.
        SrsThreadContext* ctx = dynamic_cast<SrsThreadContext*>(_srs_context);
        if (ctx) {
            ctx->clear_cid();
        }
        
        st_thread_exit(NULL);
        
        return NULL;
    }
}

