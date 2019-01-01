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

#ifndef SRS_APP_THREAD_HPP
#define SRS_APP_THREAD_HPP

#include <srs_core.hpp>

#include <vector>

#include <srs_app_st.hpp>
#include <srs_service_conn.hpp>

/**
 * The coroutine manager use a thread to delete a connection, which will stop the service
 * thread, for example, when the RTMP connection thread cycle terminated, it will notify
 * the manager(the server) to remove the connection from list of server and push it to
 * the manager thread to delete it, finally the thread of connection will stop.
 */
class SrsCoroutineManager : virtual public ISrsCoroutineHandler, virtual public IConnectionManager
{
private:
    SrsCoroutine* trd;
    std::vector<ISrsConnection*> conns;
    srs_cond_t cond;
public:
    SrsCoroutineManager();
    virtual ~SrsCoroutineManager();
public:
    srs_error_t start();
// ISrsCoroutineHandler
public:
    virtual srs_error_t cycle();
// IConnectionManager
public:
    virtual void remove(ISrsConnection* c);
private:
    void clear();
};

#endif

