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
    cond = srs_cond_new();
    trd = new SrsCoroutine("manager", this);
}

SrsCoroutineManager::~SrsCoroutineManager()
{
    srs_freep(trd);
    srs_cond_destroy(cond);
    
    clear();
}

int SrsCoroutineManager::start()
{
    return trd->start();
}

int SrsCoroutineManager::cycle()
{
    while (!trd->pull()) {
        srs_cond_wait(cond);
        clear();
    }
    
    return ERROR_SUCCESS;
}

void SrsCoroutineManager::remove(ISrsConnection* c)
{
    conns.push_back(c);
    srs_cond_signal(cond);
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

