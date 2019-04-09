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

#ifndef SRS_APP_CONN_HPP
#define SRS_APP_CONN_HPP

#include <srs_core.hpp>

#include <string>

#include <srs_app_st.hpp>
#include <srs_app_thread.hpp>
#include <srs_protocol_kbps.hpp>
#include <srs_app_reload.hpp>
#include <srs_service_conn.hpp>
#include <srs_core_time.hpp>

class SrsWallClock;

/**
 * the basic connection of SRS,
 * all connections accept from listener must extends from this base class,
 * server will add the connection to manager, and delete it when remove.
 */
class SrsConnection : virtual public ISrsConnection, virtual public ISrsCoroutineHandler
    , virtual public ISrsKbpsDelta, virtual public ISrsReloadHandler
{
protected:
    /**
     * each connection start a green thread,
     * when thread stop, the connection will be delete by server.
     */
    SrsCoroutine* trd;
    /**
     * the manager object to manage the connection.
     */
    IConnectionManager* manager;
    /**
     * the underlayer st fd handler.
     */
    srs_netfd_t stfd;
    /**
     * the ip of client.
     */
    std::string ip;
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
    SrsWallClock* clk;
    /**
     * the create time in milliseconds.
     * for current connection to log self create time and calculate the living time.
     */
    int64_t create_time;
public:
    SrsConnection(IConnectionManager* cm, srs_netfd_t c, std::string cip);
    virtual ~SrsConnection();
// interface ISrsKbpsDelta
public:
    virtual void remark(int64_t* in, int64_t* out);
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
    virtual srs_error_t start();
    // Set socket option TCP_NODELAY.
    virtual srs_error_t set_tcp_nodelay(bool v);
    // Set socket option SO_SNDBUF in ms.
    virtual srs_error_t set_socket_buffer(srs_utime_t buffer_v);
// interface ISrsOneCycleThreadHandler
public:
    /**
     * the thread cycle function,
     * when serve connection completed, terminate the loop which will terminate the thread,
     * thread will invoke the on_thread_stop() when it terminated.
     */
    virtual srs_error_t cycle();
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
    virtual srs_error_t do_cycle() = 0;
};

#endif
