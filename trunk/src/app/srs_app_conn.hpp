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

#ifndef SRS_APP_CONN_HPP
#define SRS_APP_CONN_HPP

#include <srs_core.hpp>

#include <string>

#include <srs_app_st.hpp>
#include <srs_app_thread.hpp>
#include <srs_protocol_kbps.hpp>
#include <srs_app_reload.hpp>
#include <srs_service_conn.hpp>

/**
 * the basic connection of SRS,
 * all connections accept from listener must extends from this base class,
 * server will add the connection to manager, and delete it when remove.
 */
class SrsConnection : virtual public ISrsConnection, virtual public ISrsOneCycleThreadHandler
    , virtual public IKbpsDelta, virtual public ISrsReloadHandler
{
private:
    /**
     * each connection start a green thread,
     * when thread stop, the connection will be delete by server.
     */
    SrsOneCycleThread* pthread;
    /**
     * the id of connection.
     */
    int id;
protected:
    /**
     * the manager object to manage the connection.
     */
    IConnectionManager* manager;
    /**
     * the underlayer st fd handler.
     */
    st_netfd_t stfd;
    /**
     * the ip of client.
     */
    std::string ip;
    /**
     * whether the connection is disposed,
     * when disposed, connection should stop cycle and cleanup itself.
     */
    bool disposed;
    /**
     * whether connection is expired, application definition.
     * when expired, the connection must never be served and quit ASAP.
     */
    bool expired;
    /**
     * the underlayer socket.
     */
    SrsStSocket* skt;
    /**
     * connection total kbps.
     * not only the rtmp or http connection, all type of connection are
     * need to statistic the kbps of io.
     * the SrsStatistic will use it indirectly to statistic the bytes delta of current connection.
     */
    SrsKbps* kbps;
    /**
     * the create time in milliseconds.
     * for current connection to log self create time and calculate the living time.
     */
    int64_t create_time;
public:
    SrsConnection(IConnectionManager* cm, st_netfd_t c, std::string cip);
    virtual ~SrsConnection();
// interface IKbpsDelta
public:
    virtual void resample();
    virtual int64_t get_send_bytes_delta();
    virtual int64_t get_recv_bytes_delta();
    virtual void cleanup();
public:
    /**
     * to dipose the connection.
     */
    virtual void dispose();
    /**
     * start the client green thread.
     * when server get a client from listener,
     * 1. server will create an concrete connection(for instance, RTMP connection),
     * 2. then add connection to its connection manager,
     * 3. start the client thread by invoke this start()
     * when client cycle thread stop, invoke the on_thread_stop(), which will use server
     * to remove the client by server->remove(this).
     */
    virtual int start();
// interface ISrsOneCycleThreadHandler
public:
    /**
     * the thread cycle function,
     * when serve connection completed, terminate the loop which will terminate the thread,
     * thread will invoke the on_thread_stop() when it terminated.
     */
    virtual int cycle();
public:
    /**
     * get the srs id which identify the client.
     */
    virtual int srs_id();
    /**
     * set connection to expired.
     */
    virtual void expire();
protected:
    /**
     * for concrete connection to do the cycle.
     */
    virtual int do_cycle() = 0;
};

#endif
