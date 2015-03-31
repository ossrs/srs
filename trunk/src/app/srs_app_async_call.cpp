/*
The MIT License (MIT)

Copyright (c) 2013-2015 winlin

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

#include <srs_app_async_call.hpp>

using namespace std;

#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>

// the sleep interval for http async callback.
#define SRS_AUTO_ASYNC_CALLBACL_SLEEP_US 300000

ISrsDvrAsyncCall::ISrsDvrAsyncCall()
{
}

ISrsDvrAsyncCall::~ISrsDvrAsyncCall()
{
}

SrsDvrAsyncCallThread::SrsDvrAsyncCallThread()
{
    pthread = new SrsThread("async", this, SRS_AUTO_ASYNC_CALLBACL_SLEEP_US, true);
}

SrsDvrAsyncCallThread::~SrsDvrAsyncCallThread()
{
    stop();
    srs_freep(pthread);

    std::vector<ISrsDvrAsyncCall*>::iterator it;
    for (it = callbacks.begin(); it != callbacks.end(); ++it) {
        ISrsDvrAsyncCall* call = *it;
        srs_freep(call);
    }
    callbacks.clear();
}

int SrsDvrAsyncCallThread::call(ISrsDvrAsyncCall* c)
{
    int ret = ERROR_SUCCESS;

    callbacks.push_back(c);

    return ret;
}

int SrsDvrAsyncCallThread::start()
{
    return pthread->start();
}

void SrsDvrAsyncCallThread::stop()
{
    pthread->stop();
}

int SrsDvrAsyncCallThread::cycle()
{
    int ret = ERROR_SUCCESS;
    
    std::vector<ISrsDvrAsyncCall*> copies = callbacks;
    callbacks.clear();

    std::vector<ISrsDvrAsyncCall*>::iterator it;
    for (it = copies.begin(); it != copies.end(); ++it) {
        ISrsDvrAsyncCall* call = *it;
        if ((ret = call->call()) != ERROR_SUCCESS) {
            srs_warn("dvr: ignore callback %s, ret=%d", call->to_string().c_str(), ret);
        }
        srs_freep(call);
    }

    return ret;
}


