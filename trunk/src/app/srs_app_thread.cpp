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

#include <srs_app_thread.hpp>

#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>

#include <vector>
using namespace std;

SrsCoroutineManager::SrsCoroutineManager()
{
    cond = st_cond_new();
    trd = new SrsCoroutine("manager", this);
}

SrsCoroutineManager::~SrsCoroutineManager()
{
    srs_freep(trd);
    st_cond_destroy(cond);
    
    clear();
}

int SrsCoroutineManager::start()
{
    return trd->start();
}

int SrsCoroutineManager::cycle()
{
    while (!trd->pull()) {
        st_cond_wait(cond);
        clear();
    }
    
    return ERROR_SUCCESS;
}

void SrsCoroutineManager::remove(ISrsConnection* c)
{
    conns.push_back(c);
    st_cond_signal(cond);
}

void SrsCoroutineManager::clear()
{
    // To prevent thread switch when delete connection,
    // we copy all connections then free one by one.
    vector<ISrsConnection*> copy = conns;
    conns.clear();
    
    vector<ISrsConnection*>::iterator it;
    for (it = copy.begin(); it != copy.end(); ++it) {
        ISrsConnection* conn = *it;
        srs_freep(conn);
    }
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

SrsReusableThread2::SrsReusableThread2(const char* n, ISrsReusableThread2Handler* h, int64_t cims)
{
    handler = h;
    pthread = new internal::SrsThread(n, this, cims, true);
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

