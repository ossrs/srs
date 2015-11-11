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

#include <srs_app_thread.hpp>

#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>

ISrsEndlessThreadHandler::ISrsEndlessThreadHandler()
{
}

ISrsEndlessThreadHandler::~ISrsEndlessThreadHandler()
{
}

void ISrsEndlessThreadHandler::on_thread_start()
{
}

int ISrsEndlessThreadHandler::on_before_cycle()
{
    return ERROR_SUCCESS;
}

int ISrsEndlessThreadHandler::on_end_cycle()
{
    return ERROR_SUCCESS;
}

void ISrsEndlessThreadHandler::on_thread_stop()
{
}

SrsEndlessThread::SrsEndlessThread(const char* n, ISrsEndlessThreadHandler* h)
{
    handler = h;
    pthread = new internal::SrsThread(n, this, 0, false);
}

SrsEndlessThread::~SrsEndlessThread()
{
    pthread->stop();
    srs_freep(pthread);
}

int SrsEndlessThread::start()
{
    return pthread->start();
}

int SrsEndlessThread::cycle()
{
    return handler->cycle();
}

void SrsEndlessThread::on_thread_start()
{
    handler->on_thread_start();
}

int SrsEndlessThread::on_before_cycle()
{
    return handler->on_before_cycle();
}

int SrsEndlessThread::on_end_cycle()
{
    return handler->on_end_cycle();
}

void SrsEndlessThread::on_thread_stop()
{
    handler->on_thread_stop();
}

ISrsOneCycleThreadHandler::ISrsOneCycleThreadHandler()
{
}

ISrsOneCycleThreadHandler::~ISrsOneCycleThreadHandler()
{
}

void ISrsOneCycleThreadHandler::on_thread_start()
{
}

int ISrsOneCycleThreadHandler::on_before_cycle()
{
    return ERROR_SUCCESS;
}

int ISrsOneCycleThreadHandler::on_end_cycle()
{
    return ERROR_SUCCESS;
}

void ISrsOneCycleThreadHandler::on_thread_stop()
{
}

SrsOneCycleThread::SrsOneCycleThread(const char* n, ISrsOneCycleThreadHandler* h)
{
    handler = h;
    pthread = new internal::SrsThread(n, this, 0, false);
}

SrsOneCycleThread::~SrsOneCycleThread()
{
    pthread->stop();
    srs_freep(pthread);
}

int SrsOneCycleThread::start()
{
    return pthread->start();
}

int SrsOneCycleThread::cycle()
{
    int ret = handler->cycle();
    pthread->stop_loop();
    return ret;
}

void SrsOneCycleThread::on_thread_start()
{
    handler->on_thread_start();
}

int SrsOneCycleThread::on_before_cycle()
{
    return handler->on_before_cycle();
}

int SrsOneCycleThread::on_end_cycle()
{
    return handler->on_end_cycle();
}

void SrsOneCycleThread::on_thread_stop()
{
    handler->on_thread_stop();
}

ISrsReusableThreadHandler::ISrsReusableThreadHandler()
{
}

ISrsReusableThreadHandler::~ISrsReusableThreadHandler()
{
}

void ISrsReusableThreadHandler::on_thread_start()
{
}

int ISrsReusableThreadHandler::on_before_cycle()
{
    return ERROR_SUCCESS;
}

int ISrsReusableThreadHandler::on_end_cycle()
{
    return ERROR_SUCCESS;
}

void ISrsReusableThreadHandler::on_thread_stop()
{
}

SrsReusableThread::SrsReusableThread(const char* n, ISrsReusableThreadHandler* h, int64_t interval_us)
{
    handler = h;
    pthread = new internal::SrsThread(n, this, interval_us, true);
}

SrsReusableThread::~SrsReusableThread()
{
    pthread->stop();
    srs_freep(pthread);
}

int SrsReusableThread::start()
{
    return pthread->start();
}

void SrsReusableThread::stop()
{
    pthread->stop();
}

bool SrsReusableThread::can_loop()
{
    return pthread->can_loop();
}

int SrsReusableThread::cid()
{
    return pthread->cid();
}

int SrsReusableThread::cycle()
{
    return handler->cycle();
}

void SrsReusableThread::on_thread_start()
{
    handler->on_thread_start();
}

int SrsReusableThread::on_before_cycle()
{
    return handler->on_before_cycle();
}

int SrsReusableThread::on_end_cycle()
{
    return handler->on_end_cycle();
}

void SrsReusableThread::on_thread_stop()
{
    handler->on_thread_stop();
}

ISrsReusableThread2Handler::ISrsReusableThread2Handler()
{
}

ISrsReusableThread2Handler::~ISrsReusableThread2Handler()
{
}

void ISrsReusableThread2Handler::on_thread_start()
{
}

int ISrsReusableThread2Handler::on_before_cycle()
{
    return ERROR_SUCCESS;
}

int ISrsReusableThread2Handler::on_end_cycle()
{
    return ERROR_SUCCESS;
}

void ISrsReusableThread2Handler::on_thread_stop()
{
}

SrsReusableThread2::SrsReusableThread2(const char* n, ISrsReusableThread2Handler* h, int64_t interval_us)
{
    handler = h;
    pthread = new internal::SrsThread(n, this, interval_us, true);
}

SrsReusableThread2::~SrsReusableThread2()
{
    pthread->stop();
    srs_freep(pthread);
}

int SrsReusableThread2::start()
{
    return pthread->start();
}

void SrsReusableThread2::stop()
{
    pthread->stop();
}

int SrsReusableThread2::cid()
{
    return pthread->cid();
}

void SrsReusableThread2::interrupt()
{
    pthread->stop_loop();
}

bool SrsReusableThread2::interrupted()
{
    return !pthread->can_loop();
}

int SrsReusableThread2::cycle()
{
    return handler->cycle();
}

void SrsReusableThread2::on_thread_start()
{
    handler->on_thread_start();
}

int SrsReusableThread2::on_before_cycle()
{
    return handler->on_before_cycle();
}

int SrsReusableThread2::on_end_cycle()
{
    return handler->on_end_cycle();
}

void SrsReusableThread2::on_thread_stop()
{
    handler->on_thread_stop();
}

