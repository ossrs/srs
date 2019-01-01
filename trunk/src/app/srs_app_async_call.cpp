/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2019 Winlin
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

#include <srs_app_async_call.hpp>

using namespace std;

#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>

ISrsAsyncCallTask::ISrsAsyncCallTask()
{
}

ISrsAsyncCallTask::~ISrsAsyncCallTask()
{
}

SrsAsyncCallWorker::SrsAsyncCallWorker()
{
    trd = new SrsDummyCoroutine();
    wait = srs_cond_new();
}

SrsAsyncCallWorker::~SrsAsyncCallWorker()
{
    srs_freep(trd);
    
    std::vector<ISrsAsyncCallTask*>::iterator it;
    for (it = tasks.begin(); it != tasks.end(); ++it) {
        ISrsAsyncCallTask* task = *it;
        srs_freep(task);
    }
    tasks.clear();
    
    srs_cond_destroy(wait);
}

srs_error_t SrsAsyncCallWorker::execute(ISrsAsyncCallTask* t)
{
    srs_error_t err = srs_success;
    
    tasks.push_back(t);
    srs_cond_signal(wait);
    
    return err;
}

int SrsAsyncCallWorker::count()
{
    return (int)tasks.size();
}

srs_error_t SrsAsyncCallWorker::start()
{
    srs_error_t err = srs_success;
    
    srs_freep(trd);
    trd = new SrsSTCoroutine("async", this, _srs_context->get_id());
    
    if ((err = trd->start()) != srs_success) {
        return srs_error_wrap(err, "coroutine");
    }
    
    return err;
}

void SrsAsyncCallWorker::stop()
{
    srs_cond_signal(wait);
    trd->stop();
}

srs_error_t SrsAsyncCallWorker::cycle()
{
    srs_error_t err = srs_success;
    
    while (true) {
        if ((err = trd->pull()) != srs_success) {
            return srs_error_wrap(err, "async call worker");
        }
        
        if (tasks.empty()) {
            srs_cond_wait(wait);
        }
        
        std::vector<ISrsAsyncCallTask*> copy = tasks;
        tasks.clear();
        
        std::vector<ISrsAsyncCallTask*>::iterator it;
        for (it = copy.begin(); it != copy.end(); ++it) {
            ISrsAsyncCallTask* task = *it;
            
            if ((err = task->call()) != srs_success) {
                srs_warn("ignore task failed %s", srs_error_desc(err).c_str());
                srs_freep(err);
            }
            srs_freep(task);
        }
    }
    
    return err;
}


